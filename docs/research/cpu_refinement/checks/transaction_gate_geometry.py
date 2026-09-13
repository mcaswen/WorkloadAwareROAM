"""局部透视阈值提案与有理数验真，浮点拟合失败不作为数学否证。"""

from collections import Counter
from dataclasses import dataclass
from fractions import Fraction as F
import math
import time

from transaction_gate_contract import cross, shape


@dataclass
class Receiver:
    kind: str
    index: int
    support: tuple
    faces: tuple
    points: dict
    free: tuple
    samples: tuple
    target: F | None = None


def barycentric(q, face, points):
    a,b,c=(points[v] for v in face)
    area=cross(a,b,c)
    return (cross(q,b,c)/area,cross(a,q,c)/area,cross(a,b,q)/area)


def rational_reference(s, sid):
    ix,iy=s.decode(sid)
    x,y=min(ix//6,s.nx-1),min(iy//6,s.ny-1)
    tx,ty=F(ix-6*x,6),F(iy-6*y,6)
    values=s.source["values"];i=y*(s.nx+1)+x
    h=((values[i]*(1-tx)+values[i+1]*tx)*(1-ty)+
       (values[i+s.nx+1]*(1-tx)+values[i+s.nx+2]*tx)*ty)*F(s.data["scale"])/65535
    return F(ix,6*s.nx),F(iy,6*s.ny),h


def rational_clip(s, u, v, h):
    x,z=(u-F(1,2))*F(s.data["size"]),(v-F(1,2))*F(s.data["size"])
    return tuple(F(r[0])*x+F(r[1])*h+F(r[2])*z+F(r[3]) for r in s.matrix)


def exact_error2(s, sid, height):
    u,v,h=rational_reference(s,sid)
    rc,mc=rational_clip(s,u,v,h),rational_clip(s,u,v,height)
    if rc[3]<=0 or mc[3]<=0 or mc[2]<-mc[3]: return None
    dx=(mc[0]/mc[3]-rc[0]/rc[3])*F(s.data["width"],2)
    dy=(mc[1]/mc[3]-rc[1]/rc[3])*F(s.data["height"],2)
    return dx*dx+dy*dy


def exact_old_height(s, sid):
    ix,iy=s.decode(sid)
    q=(F(ix*1048576,6*s.nx),F(iy*1048576,6*s.ny))
    face=s.faces[s.owner[sid]]
    points={v:(F(s.points[v][0]),F(s.points[v][1]),F(s.points[v][2])) for v in face}
    return sum(w*points[v][2] for w,v in zip(barycentric(q,face,points),face))


def proposals(s, index):
    face=s.faces[index];a,b,c=face
    entries=[]
    for edge in sorted(tuple(sorted(e)) for e in zip(face,face[1:]+face[:1])):
        uses=s.edges[edge]
        if len(uses)!=2: continue
        q=tuple(F(s.points[edge[0]][axis]+s.points[edge[1]][axis],2) for axis in (0,1))
        entries.append(("E",tuple(u[2] for u in uses),q,None))
    candidates=[]
    visible=[sid for sid in s.face_samples[index] if s.visibility[sid]]
    if visible:
        sid=max(visible,key=lambda i:(s.errors2[i],-i))
        ix,iy=s.decode(sid)
        q=(F(ix*1048576,6*s.nx),F(iy*1048576,6*s.ny))
        weights=barycentric(q,face,s.points)
        if min(weights)>0: candidates.append(q)
    center=tuple(F(sum(s.points[v][axis] for v in face),3) for axis in (0,1))
    if center not in candidates: candidates.append(center)
    for q in candidates: entries.append(("F",(index,),q,None))
    for v in sorted(set(face)-s.boundary_vertices):
        entries.append(("H",tuple(s.incident[v]),center,v))
    assert len(entries)<=8
    for ordinal,(kind,support,q,old_vertex) in enumerate(entries):
        key=min(0,min(s.points))-index*8-ordinal-1
        points={v:(F(s.points[v][0]),F(s.points[v][1]),F(s.points[v][2])) for j in support for v in s.faces[j]}
        qh=sum(w*points[v][2] for w,v in zip(barycentric(q,face,points),face))
        points[key]=(*q,qh)
        new=[]
        for j in support:
            old=s.faces[j]
            if kind=="E":
                # q 严格在此旧面的一个边内部，另两边各形成一个新面
                for u,v in zip(old,old[1:]+old[:1]):
                    if cross(points[u],points[v],points[key])!=0: new.append((u,v,key))
            elif j==index:
                new.extend((u,v,key) for u,v in zip(old,old[1:]+old[:1]))
            else: new.append(old)
        samples=tuple(sorted({sid for j in support for sid in s.face_samples[j] if s.visibility[sid]}))
        free=(key,) if old_vertex is None else (old_vertex,key)
        yield Receiver(kind,index,tuple(sorted(support)),tuple(new),points,free,samples)


def clip_polygon(polygon, coefficients, rhs):
    result=[]
    if not polygon: return result
    previous=polygon[-1]
    pval=sum(a*b for a,b in zip(coefficients,previous))-rhs
    for current in polygon:
        val=sum(a*b for a,b in zip(coefficients,current))-rhs
        if (pval<=0)!=(val<=0):
            t=pval/(pval-val)
            result.append(tuple(a+t*(b-a) for a,b in zip(previous,current)))
        if val<=0: result.append(current)
        previous,pval=current,val
    return list(dict.fromkeys(result))


def constraint_rows(s, proposal, sid, target):
    ix,iy=s.decode(sid)
    q=(F(ix*1048576,6*s.nx),F(iy*1048576,6*s.ny))
    for face in proposal.faces:
        weights=barycentric(q,face,proposal.points)
        if min(weights)>=0: break
    else: raise AssertionError("提案缺失样本覆盖")
    beta=[sum(float(w) for w,v in zip(weights,face) if v==f) for f in proposal.free]
    old=s.old_heights[sid]
    ref=s.source_height(ix,iy)
    rc=s.clip(ix/(6*s.nx),iy/(6*s.ny),ref)
    mc=s.clip(ix/(6*s.nx),iy/(6*s.ny),old)
    cw=s.matrix[3][1]
    kx=(s.matrix[0][1]*mc[3]-cw*mc[0])*(s.data["width"]*.5)
    ky=(s.matrix[1][1]*mc[3]-cw*mc[1])*(s.data["height"]*.5)
    # 向上留一个网格单位；最终有理投影确认不依赖浮点上界一定正确
    k=(math.ceil(math.hypot(kx,ky)/rc[3]*1e6)+1)/1e6
    tau=float(target);f=ref-old;d=mc[3]
    yield [(-k-tau*cw)*b for b in beta],tau*d-k*f
    yield [(k-tau*cw)*b for b in beta],tau*d+k*f
    yield [-cw*b for b in beta],d-1e-9
    yield [-(s.matrix[2][1]+cw)*b for b in beta],mc[2]+mc[3]


def certify(s, proposal, counters, deadline):
    if time.monotonic()>deadline or counters["sampleTouches"]+len(proposal.samples)>1000000:
        return "cap"
    counters["proposals"]+=1
    if not all(shape(face,proposal.points) for face in proposal.faces): return "shape_infeasible"
    if not proposal.samples: return "no_screen_samples"
    counters["sampleTouches"]+=len(proposal.samples)
    witness=max(proposal.samples,key=lambda sid:(s.errors2[sid],-sid))
    error=exact_error2(s,witness,exact_old_height(s,witness))
    if error is None: return "projection_unknown"
    root=math.isqrt((error.numerator*10**12)//error.denominator)
    target=F(root,10**6)-F(1,100)
    if target<0: return "below_progress_margin"
    bounds=[(-s.data["scale"]-float(proposal.points[v][2]),2*s.data["scale"]-float(proposal.points[v][2])) for v in proposal.free]
    low,high=bounds[0]
    polygon=None
    if len(bounds)==2:
        a,b=bounds[1];polygon=[(low,a),(high,a),(high,b),(low,b)]
    for sid in proposal.samples:
        if time.monotonic()>deadline: return "cap"
        for coeff,rhs in constraint_rows(s,proposal,sid,target):
            counters["linearConstraints"]+=1
            if polygon is not None:
                polygon=clip_polygon(polygon,coeff,rhs)
                if not polygon: return "fit_bound_failed"
            elif coeff[0]>0: high=min(high,rhs/coeff[0])
            elif coeff[0]<0: low=max(low,rhs/coeff[0])
            elif rhs<0: return "fit_bound_failed"
            if polygon is None and low>high: return "fit_bound_failed"
    values=(min(high,max(low,0)),) if polygon is None else tuple(sum(p[i] for p in polygon)/len(polygon) for i in (0,1))
    for v,delta in zip(proposal.free,values):
        old=proposal.points[v];proposal.points[v]=(*old[:2],old[2]+F(delta))
        if not -s.data["scale"]<=proposal.points[v][2]<=2*s.data["scale"]: return "numeric_unknown"
    # 原始有理投影逐点验证，通过后才将提案算入共同接收集合
    for sid in proposal.samples:
        if time.monotonic()>deadline: return "cap"
        ix,iy=s.decode(sid);q=(F(ix*1048576,6*s.nx),F(iy*1048576,6*s.ny))
        for face in proposal.faces:
            weights=barycentric(q,face,proposal.points)
            if min(weights)>=0: break
        height=sum(w*proposal.points[v][2] for w,v in zip(weights,face))
        squared=exact_error2(s,sid,height)
        counters["exactSampleChecks"]+=1
        if squared is None or squared>target*target: return "numeric_unknown"
    proposal.target=target
    return "certified"


def receiver_prefix(s, deadline):
    counters=Counter(); reasons=Counter();accepted=[];rows=[]
    for index in s.raw[:64]:
        tried=[];receiver=None
        iterator=iter(proposals(s,index))
        while True:
            started=time.monotonic()
            try:
                proposal=next(iterator)
            except StopIteration:
                break
            counters["proposalGenerationSeconds"]+=time.monotonic()-started
            started=time.monotonic()
            reason=certify(s,proposal,counters,deadline)
            counters["certificationSeconds"]+=time.monotonic()-started
            reasons[reason]+=1;tried.append([proposal.kind,reason])
            if reason=="certified": receiver=proposal;accepted.append(proposal);break
            if reason=="cap": break
        rows.append({"faceId":s.ids[index],"attempts":tried,"receiver":receiver.kind if receiver else None})
        if tried and tried[-1][1]=="cap": break
    return accepted,{"examinedIntents":len(rows),"tailIntentsUnexamined":len(s.raw)-len(rows),
        "certifiedReceivers":len(accepted),"receiverReasons":dict(reasons),"receiverWork":dict(counters),"intentRows":rows}
