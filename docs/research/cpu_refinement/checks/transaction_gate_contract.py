"""离线快照与公共样本评分；只保存本轮诊断值，不调用生产控制器。"""

from array import array
from bisect import bisect_right
from collections import defaultdict
from fractions import Fraction as F
import math
import time


def cross(a, b, c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


def shape(face, points):
    for i in range(3):
        a,b,c = (points[face[j % 3]] for j in (i,i+1,i+2))
        u,v = (b[0]-a[0],b[1]-a[1]),(c[0]-a[0],c[1]-a[1])
        dot = u[0]*v[0]+u[1]*v[1]
        if dot > 0 and 10*dot*dot > 9*(u[0]*u[0]+u[1]*u[1])*(v[0]*v[0]+v[1]*v[1]):
            return False
    return True


class Snapshot:
    """唯一格点身份与闭面关联并存，边界样本不会因 owner 归属漏掉评分。"""

    def __init__(self, data, source):
        self.data, self.source = data, source
        def coordinate(value):
            rational=F(value)*1048576
            return int(rational) if rational.denominator==1 else rational
        self.points = {int(v[0]):(coordinate(v[1]),coordinate(v[2]),F(v[3]) if isinstance(v[3],str) else v[3]) for v in data["vertices"]}
        self.ids, self.faces = [], []
        self.edges, self.incident = defaultdict(list), defaultdict(list)
        for row in sorted(data["faces"]):
            fid,a,b,c = row
            if cross(self.points[a],self.points[b],self.points[c]) < 0:
                b,c = c,b
            f = (a,b,c)
            index = len(self.faces)
            self.ids.append(fid); self.faces.append(f)
            for v in f:
                self.incident[v].append(index)
            for u,v in zip(f,f[1:]+f[:1]):
                self.edges[tuple(sorted((u,v)))].append((u,v,index))
        self.nx, self.ny = source["width"]-1,source["height"]-1
        assert self.nx == self.ny, "本次面板只支持方形参考栅格"
        self.groups, self.starts, offset = [],[],0
        for ox,oy,cols,rows in ((0,0,self.nx+1,self.ny+1),(3,0,self.nx,self.ny+1),
            (0,3,self.nx+1,self.ny),(3,3,self.nx,self.ny),(4,2,self.nx,self.ny),(2,4,self.nx,self.ny)):
            self.starts.append(offset)
            self.groups.append((ox,oy,cols,rows,offset))
            offset += cols*rows
        self.sample_count = offset
        self.errors2 = array('d',[0])*offset
        self.height_errors = array('d',[0])*offset
        self.old_heights = array('d',[0])*offset
        self.visibility = array('b',[-1])*offset
        self.owner = array('i',[-1])*offset
        self.face_samples = [array('I') for _ in self.faces]
        self.priority2, self.error_max2 = [],[]
        self.work = defaultdict(int)
        self.matrix = [data["matrix"][i:i+4] for i in range(0,16,4)]

    def validate(self):
        assert len(set(self.ids)) == len(self.faces)
        assert len(self.faces) <= self.data["budget"]
        assert set(self.incident) == set(self.points)
        assert sum(cross(*(self.points[v] for v in f)) for f in self.faces) == 2*1048576**2
        boundary_edges = []
        for edge, uses in self.edges.items():
            assert len(uses) in (1,2)
            if len(uses) == 2:
                assert uses[0][:2] == uses[1][:2][::-1]
            else:
                boundary_edges.append(edge)
                a,b = (self.points[v] for v in edge)
                assert (a[0]==b[0] and a[0] in (0,1048576)) or (a[1]==b[1] and a[1] in (0,1048576))
        boundary_v = set(v for e in boundary_edges for v in e)
        boundary_degree = defaultdict(int)
        for a,b in boundary_edges:
            boundary_degree[a]+=1; boundary_degree[b]+=1
        assert all(n==2 for n in boundary_degree.values())
        assert len(self.points)-len(self.edges)+len(self.faces)==1
        for vertex, indices in self.incident.items():
            link = defaultdict(set)
            for i in indices:
                a,b = [v for v in self.faces[i] if v!=vertex]
                link[a].add(b); link[b].add(a)
            degrees = sorted(map(len,link.values()))
            if vertex in boundary_v:
                assert degrees[:2]==[1,1] and all(d==2 for d in degrees[2:])
            else:
                assert all(d==2 for d in degrees) and len(link)<=19
            seen, todo = set(),[next(iter(link))]
            while todo:
                v = todo.pop()
                if v not in seen:
                    seen.add(v);todo.extend(link[v]-seen)
            assert len(seen)==len(link)
        assert all(cross(*(self.points[v] for v in f))>0 and shape(f,self.points) for f in self.faces)
        assert all(-self.data["scale"]<=p[2]<=2*self.data["scale"] for p in self.points.values())
        self.boundary_vertices=boundary_v

    def decode(self, sid):
        group = self.groups[bisect_right(self.starts,sid)-1]
        ox,oy,cols,_,start=group
        local=sid-start
        return (6*(local%cols)+ox,6*(local//cols)+oy)

    def clip(self, u, v, height):
        x,z=(u-.5)*self.data["size"],(v-.5)*self.data["size"]
        return tuple(((r[0]*x+r[1]*height)+r[2]*z)+r[3] for r in self.matrix)

    def source_height(self, ix, iy):
        x,y=min(ix//6,self.nx-1),min(iy//6,self.ny-1)
        tx,ty=(ix-6*x)/6,(iy-6*y)/6
        a=self.source["values"];stride=self.nx+1;i=y*stride+x
        bottom=(a[i]*(1-tx)+a[i+1]*tx)/65535
        top=(a[i+stride]*(1-tx)+a[i+stride+1]*tx)/65535
        return ((1-ty)*bottom+ty*top)*self.data["scale"]

    def evaluate(self, sid, ix, iy, height):
        u,v=ix/(6*self.nx),iy/(6*self.ny)
        reference=self.source_height(ix,iy)
        rc=self.clip(u,v,reference)
        visible=rc[3]>0 and all(-rc[3]<=rc[i]<=rc[3] for i in range(3))
        self.visibility[sid]=int(visible)
        self.height_errors[sid]=abs(height-reference)
        self.old_heights[sid]=height
        self.work["sampleEvaluations"]+=1
        if visible:
            mc=self.clip(u,v,height)
            assert mc[3]>0 and mc[2]>=-mc[3], "被测样本跨近面，输入评价不完整"
            dx=(mc[0]/mc[3]-rc[0]/rc[3])*(self.data["width"]*.5)
            dy=(mc[1]/mc[3]-rc[1]/rc[3])*(self.data["height"]*.5)
            self.errors2[sid]=dx*dx+dy*dy

    def refresh(self, deadline):
        factor=6*self.nx
        for index,face in enumerate(self.faces):
            if index%128==0 and time.monotonic()>deadline:
                raise TimeoutError("共同样本评分配额用尽")
            p=[self.points[v] for v in face]
            a,b,c=[(v[0]*factor,v[1]*factor) for v in p]
            area=cross(a,b,c)
            xmin,xmax=min(v[0] for v in p),max(v[0] for v in p)
            ymin,ymax=min(v[1] for v in p),max(v[1] for v in p)
            max_error=0.0
            for ox,oy,cols,rows,start in self.groups:
                # 包围格点使用整数除法，闭边上的点不会被浮点 epsilon 丢弃
                xl=max(0,-(-(xmin*factor-ox*1048576)//(6*1048576)))
                xr=min(cols-1,(xmax*factor-ox*1048576)//(6*1048576))
                yl=max(0,-(-(ymin*factor-oy*1048576)//(6*1048576)))
                yr=min(rows-1,(ymax*factor-oy*1048576)//(6*1048576))
                for y in range(yl,yr+1):
                    for x in range(xl,xr+1):
                        ix,iy=6*x+ox,6*y+oy
                        q=(ix*1048576,iy*1048576)
                        w0,w1=cross(q,b,c),cross(a,q,c)
                        w2=area-w0-w1
                        self.work["sampleLocationTests"]+=1
                        if min(w0,w1,w2)<0: continue
                        sid=start+y*cols+x
                        height=((w0/area)*p[0][2]+(w1/area)*p[1][2])+(w2/area)*p[2][2]
                        if self.owner[sid]<0:
                            self.owner[sid]=index
                            self.evaluate(sid,ix,iy,height)
                        else:
                            assert abs(height-self.old_heights[sid])<=1e-10*max(1,abs(height)), "共享面样本高度不一致"
                        self.face_samples[index].append(sid)
                        self.work["sampleFaceContributions"]+=1
                        if self.visibility[sid]: max_error=max(max_error,self.errors2[sid])
            clips=[self.clip(v[0]/1048576,v[1]/1048576,v[2]) for v in p]
            if any(v[3]<=0 for v in clips):
                self.priority2.append(None)
                self.work["projectionUnknownFaces"]+=1
            else:
                screen=[(v[0]/v[3]*self.data["width"]*.5,v[1]/v[3]*self.data["height"]*.5) for v in clips]
                longest=max((u[0]-v[0])**2+(u[1]-v[1])**2 for u,v in zip(screen,screen[1:]+screen[:1]))
                self.priority2.append(max(max_error,.04*longest))
            self.error_max2.append(max_error)
        assert all(o>=0 for o in self.owner), "公共样本存在覆盖缺失"
        self.work["screenSamples"]=sum(self.visibility)
        self.raw=sorted((i for i,p in enumerate(self.priority2) if p is not None and p>self.data["splitPixels"]**2),key=lambda i:(-self.priority2[i],self.ids[i]))

    def summary(self):
        maximum=max(range(self.sample_count),key=self.errors2.__getitem__)
        maxh=max(range(self.sample_count),key=self.height_errors.__getitem__)
        return {"faces":len(self.faces),"vertices":len(self.points),"budget":self.data["budget"],
            "freeCredits":(self.data["budget"]-len(self.faces))//2,"D_raw":len(self.raw),
            "sampledScreenMaxPx":math.sqrt(self.errors2[maximum]),"screenMaxSample":list(self.decode(maximum)),
            "sampledHeightMax":self.height_errors[maxh],"heightMaxSample":list(self.decode(maxh)),
            "sampleCoordinateDenominator":6*self.nx,"work":dict(self.work)}
