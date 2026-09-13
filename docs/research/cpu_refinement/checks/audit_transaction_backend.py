"""编排冻结自然快照的前置覆盖与成本审计，后端按门禁需要再执行。"""

import argparse
import hashlib
import json
import time
from pathlib import Path

from transaction_gate_contract import Snapshot


def check_contract():
    """用可手算曲面独立核对采样身份、投影和局部认证，不运行自然矩阵。"""
    from collections import Counter
    from fractions import Fraction as F
    from transaction_gate_geometry import proposals,certify,exact_error2,exact_old_height
    from transaction_gate_reclamation import donor
    data={"vertices":[[0,0,0,0],[1,1,0,0],[2,1,1,0],[3,0,1,0]],
        "faces":[[0,0,1,2],[1,0,2,3]],"budget":2,"scale":1,"size":1,
        "matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"width":100,"height":100,"splitPixels":4}
    source={"width":3,"height":3,"values":[0,0,0,0,65535,0,0,0,0]}
    s=Snapshot(data,source);s.validate();s.refresh(time.monotonic()+10)
    assert s.sample_count==33 and s.work["sampleFaceContributions"]==38
    for sid in range(33):
        x,y=s.decode(sid)
        assert s.owner[sid]==(0 if y<=x else 1)
        assert (sid in s.face_samples[0])==(y<=x)
        assert (sid in s.face_samples[1])==(x<=y)
    assert exact_error2(s,4,exact_old_height(s,4))==2500
    proposal=next(proposals(s,0));counts=Counter()
    assert certify(s,proposal,counts,time.monotonic()+10)=="certified"
    s.matrix[3][1]=.25
    assert exact_error2(s,4,F(0))==1600
    grid_vertices=[[y*3+x,F(x,2),F(y,2),0] for y in range(3) for x in range(3)]
    grid_faces=[]
    for y in range(2):
        for x in range(2):
            a=y*3+x;b=a+1;d=a+3;c=d+1
            grid_faces.extend([[len(grid_faces),a,b,c],[len(grid_faces)+1,a,c,d]])
    grid=dict(data,vertices=grid_vertices,faces=grid_faces,budget=8)
    flat={"width":3,"height":3,"values":[0]*9}
    s=Snapshot(grid,flat);s.validate();s.refresh(time.monotonic()+10)
    for oracle in (False,True):
        result=donor(s,4,Counter(),time.monotonic()+10,oracle)
        assert result["reason"]=="certified" and result["error2"]==0 and len(result["faces"])==4
        target=dict(grid,vertices=[v for v in grid_vertices if v[0]!=4],
            faces=[row for i,row in enumerate(grid_faces) if i not in result["support"]]+[[-i-1,*f] for i,f in enumerate(result["faces"])])
        following=Snapshot(target,flat);following.validate();following.refresh(time.monotonic()+10)
        assert len(following.faces)==6 and max(following.errors2)==0
    return {"status":"通过","sampleCount":33,"sharedContributions":38,
        "orthographicSquaredError":2500,"perspectiveSquaredError":1600,"degreeSixFastAndOracle":"通过"}


def audit(path):
    started=time.monotonic()
    data=json.loads(path.read_text())
    assert data["sampleIndex"] in (14,46)
    assert data["scenario"] in ("test129-a-b4096","peking547-a-b20000")
    source_path=path.with_name(data["scenario"]+"-source.json")
    source=json.loads(source_path.read_text())
    snapshot=Snapshot(data,source)
    loaded=time.monotonic()
    snapshot.validate()
    validated=time.monotonic()
    snapshot.refresh(started+180)
    refreshed=time.monotonic()
    result=snapshot.summary()
    result.update({"scenario":data["scenario"],"sampleIndex":data["sampleIndex"],
        "inputHash":data["inputHash"],"snapshotSha256":hashlib.sha256(path.read_bytes()).hexdigest(),
        "sourceSha256":hashlib.sha256(source_path.read_bytes()).hexdigest(),
        "seconds":{"load":loaded-started,"validate":validated-loaded,"refresh":refreshed-validated}})
    if result["freeCredits"]>=result["D_raw"]:
        result.update({"status":"无预算竞争覆盖","D_need":0,"D_feasible":0,"D_executed":0,
            "backend":"未运行；初始空额度足以覆盖全部原始需求","R_local":None,"R_execution":None})
    else:
        from transaction_gate_geometry import receiver_prefix
        receivers, receiver_result=receiver_prefix(snapshot,refreshed+90)
        result.update(receiver_result)
        result["seconds"]["receiverCertification"]=time.monotonic()-refreshed
        free_count=min(result["freeCredits"],len(receivers))
        result.update({"status":"共同接收前缀完成","backend":"接收前缀没有需回收需求，后端未运行" if free_count==len(receivers) else "回收认证待实施",
            "D_free_prefix":free_count,"D_need_prefix":len(receivers)-free_count,
            "D_need":None,"D_feasible":None,"D_executed":None})
        result["scope"]="预冻结全局前 64 原始需求；尾部未认证，不推断全域分母或否定一般后端"
        if len(receivers)>free_count:
            from transaction_gate_reclamation import run_backends
            result["backends"]=run_backends(snapshot,receivers,refreshed+90,receiver_result["receiverWork"].get("sampleTouches",0))
            result["backend"]="三价/一般快路径同池对照及限定 oracle"
            result["status"]="有限前缀兑现审计完成" if all(result["backends"].get(k,{}).get("status")!="cap_unknown" for k in ("degree3","general")) else "有限前缀部分完成，回收配额用尽"
    result["seconds"]["total"]=time.monotonic()-started
    result["codeSha256"]={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in
        (Path(__file__),Path(__file__).with_name("transaction_gate_contract.py"),Path(__file__).with_name("transaction_gate_geometry.py"),Path(__file__).with_name("transaction_gate_reclamation.py"))}
    return result


if __name__=="__main__":
    parser=argparse.ArgumentParser(description="冻结快照单轮离线审计")
    parser.add_argument("--check",action="store_true")
    parser.add_argument("snapshot",type=Path,nargs="?")
    parser.add_argument("output",type=Path,nargs="?")
    args=parser.parse_args()
    if args.check:
        if args.snapshot or args.output: parser.error("解析检查不接受快照路径")
        result=check_contract()
    else:
        if args.snapshot is None or args.output is None: parser.error("需要快照和输出路径")
        if args.output.exists(): raise RuntimeError("拒绝覆盖已有结果")
        result=audit(args.snapshot)
        args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n")
    print(json.dumps(result,ensure_ascii=False))
