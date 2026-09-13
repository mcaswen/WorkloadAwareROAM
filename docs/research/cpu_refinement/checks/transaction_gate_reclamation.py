"""有限回收池、确定性耳切与批次预留，完整费用单列。"""

from collections import Counter
from fractions import Fraction as F
import itertools
import time

from transaction_gate_contract import cross, shape, Snapshot
from transaction_gate_geometry import barycentric, exact_error2
from verify_cavity_retriangulation import admissible, inside_triangle


def current_ring(s, center):
    following={}
    for index in s.incident[center]:
        face=s.faces[index];pos=face.index(center)
        a,b=face[(pos+1)%3],face[(pos+2)%3]
        assert a not in following
        following[a]=b
    ring=[min(following)]
    while following[ring[-1]]!=ring[0]:
        ring.append(following[ring[-1]])
        assert len(ring)<=len(following)
    assert len(ring)==len(following)
    return ring


def fast_faces(ring, points, counters):
    remaining=list(ring);result=[]
    while len(remaining)>3:
        chosen=None
        for vertex in sorted(remaining):
            i=remaining.index(vertex)
            face=(remaining[i-1],vertex,remaining[(i+1)%len(remaining)])
            counters["earTests"]+=1
            if (admissible(face,remaining,points) and shape(face,points) and
                not any(inside_triangle(points[v],face,points) for v in remaining if v not in face)):
                chosen=(i,face);break
        if chosen is None: return None
        i,face=chosen;result.append(face);remaining.pop(i)
    final=tuple(remaining)
    if cross(*(points[v] for v in final))<=0 or not shape(final,points): return None
    return tuple(result+[final])


def triangle_cost(s, face, points, samples, counters, deadline):
    maximum=F(0)
    for sid in samples:
        if time.monotonic()>deadline or counters["sampleTouches"]>=1000000:
            raise TimeoutError("局部认证配额用尽")
        counters["sampleTouches"]+=1
        ix,iy=s.decode(sid)
        q=(F(ix*1048576,6*s.nx),F(iy*1048576,6*s.ny))
        weights=barycentric(q,face,points)
        if min(weights)<0: continue
        h=sum(w*points[v][2] for w,v in zip(weights,face))
        e=exact_error2(s,sid,h)
        counters["exactSampleChecks"]+=1
        if e is None: return None
        maximum=max(maximum,e)
    return maximum


def donor(s, center, counters, deadline, oracle=False):
    ring=current_ring(s,center)
    counters["ringVisits"]+=len(ring)
    support=tuple(sorted(s.incident[center]))
    points={v:(F(s.points[v][0]),F(s.points[v][1]),F(s.points[v][2])) for v in ring}
    samples=tuple(sorted({sid for index in support for sid in s.face_samples[index] if s.visibility[sid]}))
    if not oracle:
        faces=fast_faces(ring,points,counters)
        if faces is None: return {"reason":"fast_shape_miss","center":center,"support":support}
        values=[triangle_cost(s,f,points,samples,counters,deadline) for f in faces]
        if any(v is None for v in values): return {"reason":"projection_unknown","center":center,"support":support}
        value=max(values,default=F(0))
    else:
        # 每个三角形的独立精确采样代价先付费，再执行标准多边形递推
        costs={}
        for i,k,j in itertools.combinations(range(len(ring)),3):
            face=(ring[i],ring[k],ring[j]);counters["oracleTriangleTests"]+=1
            if admissible(face,ring,points) and shape(face,points):
                cost=triangle_cost(s,face,points,samples,counters,deadline)
                if cost is None: return {"reason":"projection_unknown","center":center,"support":support}
                costs[i,k,j]=cost
        values={(i,i+1):F(0) for i in range(len(ring)-1)};choices={}
        for gap in range(2,len(ring)):
            for i in range(len(ring)-gap):
                j=i+gap;options=[]
                for k in range(i+1,j):
                    counters["oracleTransitions"]+=1
                    if (i,k,j) in costs and (i,k) in values and (k,j) in values:
                        options.append((max(costs[i,k,j],values[i,k],values[k,j]),k))
                if options: values[i,j],choices[i,j]=min(options)
        if (0,len(ring)-1) not in values: return {"reason":"shape_infeasible","center":center,"support":support}
        def reconstruct(i,j):
            if j==i+1: return []
            k=choices[i,j]
            return reconstruct(i,k)+reconstruct(k,j)+[(ring[i],ring[k],ring[j])]
        faces=tuple(reconstruct(0,len(ring)-1));value=values[0,len(ring)-1]
    return {"reason":"certified","center":center,"support":support,"faces":faces,
        "points":points,"samples":samples,"error2":value}


def footprint(s, support, faces, writes_height):
    old=[s.faces[i] for i in support]
    vertices=set(v for f in old for v in f)
    edges={tuple(sorted(e)) for f in old+list(faces) for e in zip(f,f[1:]+f[:1])}
    reads={("height",v) for v in vertices}|{("face",i) for i in support}|{("edge",e) for e in edges}
    writes={("height",v) for v in writes_height}|{("face",i) for i in support}|{("edge",e) for e in edges}
    return reads,writes


def overlap(first, second):
    ra,wa=first;rb,wb=second
    return bool(wa&(rb|wb) or wb&ra)


def backend(s, receivers, pool, kind, cache, counters, deadline):
    free=(s.data["budget"]-len(s.faces))//2
    approved=[];reserved=[];rows=[];used_donors=set();feasible=executed=free_accepted=0
    for index,r in enumerate(receivers):
        rf=footprint(s,r.support,r.faces,r.free)
        if index<free:
            blocked=any(overlap(rf,other) for other in reserved)
            if not blocked:
                approved.append((r,None));reserved.append(rf);free_accepted+=1
            rows.append({"faceId":s.ids[r.index],"freeCredit":True,"accepted":not blocked})
            continue
        possible=[];reasons=Counter()
        for center in pool:
            counters["pairChecks"]+=1
            if kind=="degree3" and len(s.incident[center])!=3:
                reasons["degree_not_three"]+=1;continue
            if center not in cache:
                started=time.monotonic()
                cache[center]=donor(s,center,counters,deadline)
                counters["donorGenerationCertificationSeconds"]+=time.monotonic()-started
            d=cache[center]
            if d["reason"]!="certified": reasons[d["reason"]]+=1;continue
            if d["error2"]>r.target*r.target: reasons["fast_quality_miss"]+=1;continue
            df=footprint(s,d["support"],d["faces"],(center,))
            if overlap(rf,df): reasons["internal_conflict"]+=1;continue
            possible.append((d,(rf[0]|df[0],rf[1]|df[1])))
        if possible: feasible+=1
        chosen=None
        for d,combined in possible:
            counters["reservationChecks"]+=1
            if d["center"] in used_donors:
                counters["donorReuseRejections"]+=1;continue
            if not any(overlap(combined,other) for other in reserved):
                chosen=d;reserved.append(combined);approved.append((r,d));used_donors.add(d["center"]);executed+=1;break
            counters["readWriteConflictRejections"]+=1
        rows.append({"faceId":s.ids[r.index],"freeCredit":False,"localFeasible":bool(possible),
            "accepted":chosen is not None,"donor":chosen["center"] if chosen else None,
            "reasons":dict(reasons),"reservedConflict":bool(possible) and chosen is None})
    return approved,{"D_need_prefix":max(0,len(receivers)-free),"D_feasible_prefix":feasible,
        "D_executed_prefix":executed,"acceptedFree":free_accepted,"batchWidth":len(approved),
        "unusedAssignedCredits":min(free,len(receivers))-free_accepted,"rows":rows}


def apply_batch(s, batch, deadline):
    """诊断副本按已批准记录直接替换，禁止在应用时重新寻找目标或回收方。"""
    started=time.monotonic()
    points=dict(s.points);removed=set();new_faces=[]
    for r,d in batch:
        removed.update(r.support)
        for v in r.free: points[v]=r.points[v]
        new_faces.extend(r.faces)
        if d is not None:
            removed.update(d["support"])
            del points[d["center"]]
            new_faces.extend(d["faces"])
    rows=[(s.ids[i],*face) for i,face in enumerate(s.faces) if i not in removed]
    # 新面 ID 由排序后的逻辑连接决定，应用排列不影响最终 owner 同分规则
    canonical=sorted(min(f[i:]+f[:i] for i in range(3)) for f in new_faces)
    first_id=min(0,min(s.ids))-1
    rows.extend((first_id-i,*f) for i,f in enumerate(canonical))
    data=dict(s.data,vertices=[(v,str(F(p[0])/1048576),str(F(p[1])/1048576),str(F(p[2]))) for v,p in points.items()],faces=rows)
    copied=time.monotonic()
    target=Snapshot(data,s.source);target.validate();target.refresh(deadline)
    completed=time.monotonic()
    assert max(target.errors2)<=max(s.errors2)+1e-8
    return data,{"faces":len(target.faces),"nextD_raw":len(target.raw),"sampledScreenMaxPx":max(target.errors2)**.5,
        "sampledHeightMax":max(target.height_errors),"copyAndApplySeconds":copied-started,
        "validationAndNextRefreshSeconds":completed-copied}


def run_backends(s, receivers, deadline, prior_touches=0):
    counters=Counter(sampleTouches=prior_touches);cache={}
    pool=sorted((v for v in s.incident if v not in s.boundary_vertices),
        key=lambda v:(max(s.priority2[i] if s.priority2[i] is not None else float('inf') for i in s.incident[v]),v))[:64]
    result={"poolCenters":pool,"poolDegreeThree":sum(len(s.incident[v])==3 for v in pool)}
    batches={}
    for kind in ("degree3","general"):
        started=time.monotonic()
        donor_before=counters["donorGenerationCertificationSeconds"]
        try:
            batch,values=backend(s,receivers,pool,kind,cache,counters,deadline)
        except TimeoutError:
            result[kind]={"status":"cap_unknown","seconds":time.monotonic()-started}
            result["work"]=dict(counters)
            result["priorReceiverTouches"]=prior_touches
            return result
        values["seconds"]=time.monotonic()-started
        values["donorGenerationCertificationSeconds"]=counters["donorGenerationCertificationSeconds"]-donor_before
        values["pairConflictReservationSeconds"]=values["seconds"]-values["donorGenerationCertificationSeconds"]
        result[kind]=values;batches[kind]=batch
    # 固定少量失败中心核查快路径损失；不将 oracle 解混入快路径批准批次
    oracle_started=time.monotonic()
    oracle_rows=[];oracle_cache={};free=(s.data["budget"]-len(s.faces))//2
    for r in receivers[free:free+4]:
        for center in pool[:8]:
            fast=cache.get(center)
            if fast and fast["reason"]=="certified" and fast["error2"]<=r.target*r.target: continue
            if center not in oracle_cache:
                try:
                    oracle_cache[center]=donor(s,center,counters,deadline,True)
                except TimeoutError:
                    oracle_rows.append({"faceId":s.ids[r.index],"center":center,"status":"cap_unknown"})
                    break
            d=oracle_cache[center]
            status=d["reason"]
            if status=="certified": status="fast_miss_oracle_cavity_feasible" if d["error2"]<=r.target*r.target else "quality_infeasible"
            compatible=None
            if status=="fast_miss_oracle_cavity_feasible":
                compatible=not overlap(footprint(s,r.support,r.faces,r.free),footprint(s,d["support"],d["faces"],(center,)))
            oracle_rows.append({"faceId":s.ids[r.index],"center":center,"status":status,"pairCompatible":compatible})
    result["oracleRows"]=oracle_rows
    result["oracleSeconds"]=time.monotonic()-oracle_started
    result["work"]=dict(counters)
    result["priorReceiverTouches"]=prior_touches
    for kind,batch in batches.items():
        if not batch: continue
        data,validation=apply_batch(s,batch,time.monotonic()+90)
        # 反序只比较局部记录合成的核心结果，不重复全域评分
        reversed_batch=list(reversed(batch))
        points=dict(s.points);removed=set();faces=[]
        for r,d in reversed_batch:
            removed.update(r.support);faces.extend(r.faces)
            points.update({v:r.points[v] for v in r.free})
            if d is not None:
                removed.update(d["support"]);faces.extend(d["faces"]);del points[d["center"]]
        assert sorted(tuple(sorted(f)) for f in faces)==sorted(tuple(sorted(row[1:])) for row in data["faces"] if row[0]<min(0,min(s.ids)))
        assert all(tuple(map(F,row[1:]))==(F(points[row[0]][0])/1048576,F(points[row[0]][1])/1048576,F(points[row[0]][2])) for row in data["vertices"])
        result[kind]["application"]=validation
        result[kind]["reverseCoreEqual"]=True
    return result
