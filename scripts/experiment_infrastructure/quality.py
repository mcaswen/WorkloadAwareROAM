"""从实际导出网格调用独立C++质量评价器；不重写几何误差公式。"""
from __future__ import annotations
import subprocess
import sys
import time
from pathlib import Path
import numpy as np
from .catalog import load_json,content_hash
from .result_adapters import load_run
from .runner import save


def evaluate(run,output,probe,frames=(2,15,16,23),locations=True):
    run=Path(run).resolve();output=Path(output).resolve();probe=Path(probe).resolve()
    data=load_run(run)
    if data["manifest"]["status"]!="ok": raise ValueError("不能评价失败运行")
    output.mkdir(parents=True,exist_ok=False)
    m=data["manifest"];c=m["case"]
    if sys.byteorder!="little": raise ValueError("旧errors.f64只支持已冻结的little-endian平台")
    index={"schemaVersion":"eip-quality-v1","run":str(run),"runManifestSha256":content_hash(run/"manifest.json"),
        "workloadId":m["workloadId"],"backend":c["backend"],"algorithm":c["algorithm"],"heightPolicy":c["heightPolicy"],
        "reference":"raw U16 samples / 65535 + bilinear interpolation",
        "sampleSemantics":"reference mesh vertices, edge midpoints and centroids, shared items deduplicated; k=0",
        "rmsSemantics":"equal weights on visible terrain-domain samples; not screen-area weighted",
        "heatmapSemantics":"uniform ordinal subset plus actual maxima; not all evaluator samples",
        "sourceSha256":c["sampleSha256"],"probe":{"path":str(probe),"sha256":content_hash(probe)},"frames":[]}
    by_frame={int(f["frame"]):f for f in data["frames"]}
    for frame in frames:
        item={"frame":frame}
        record=by_frame.get(frame)
        if not record or not record["artifact"]:
            item["status"]="not-exported";index["frames"].append(item);continue
        mesh=run/"run"/record["artifact"]
        target=output/f"frame-{frame}"
        args=[str(probe),str(mesh),c["heightMap"],str(target)]+(["--locations"] if locations else [])
        started=time.perf_counter()
        try:
            result=subprocess.run(args,cwd=run,capture_output=True,text=True,timeout=180)
            item.update(returncode=result.returncode,elapsedSeconds=time.perf_counter()-started,
                stdout=result.stdout,stderr=result.stderr,argv=args,
                poseHash=record["poseHash"],projectionHash=record["projectionHash"],meshHash=record["hash"],
                faces=record["faces"],budget=record["budget"],status="ok" if result.returncode==0 else "invalid-quality")
            if (target/"quality.json").is_file():
                q=load_json(target/"quality.json")
                if q["meshHash"]!=record["hash"]: raise ValueError("评价器和实际帧网格身份不同")
                item.update(quality=f"frame-{frame}/quality.json",qualitySha256=content_hash(target/"quality.json"),
                    errors=f"frame-{frame}/errors.f64",errorsSha256=content_hash(target/"errors.f64"),
                    locations=f"frame-{frame}/locations.csv" if locations else None,
                    sampleHash=q["sampleHash"],sampleCount=q["q"])
                if locations: item["locationsSha256"]=content_hash(target/"locations.csv")
        except subprocess.TimeoutExpired:
            item.update(status="censored",elapsedSeconds=time.perf_counter()-started)
        index["frames"].append(item);save(output/"quality-index.json",index)
    return output/"quality-index.json"


def pointwise_pair(candidate,reference,output):
    """Dmax为同域逐点误差差值的最大值；不以max差替代，不钳到零。"""
    candidate=Path(candidate).resolve();reference=Path(reference).resolve()
    a=load_json(candidate/"quality-index.json");b=load_json(reference/"quality-index.json")
    if any(a[k]!=b[k] for k in ("workloadId","backend","sourceSha256","sampleSemantics")):
        raise ValueError("独立质量输入/采样/深度约定不一致")
    rows=[];by={f["frame"]:f for f in b["frames"]}
    for left in a["frames"]:
        right=by.get(left["frame"]);item={"frame":left["frame"],"status":"unpaired"}
        if right and left["status"]==right["status"]=="ok":
            if any(left[k]!=right[k] for k in ("poseHash","projectionHash","sampleHash","sampleCount")):
                raise ValueError("逐点质量身份不符")
            for root,row in ((candidate,left),(reference,right)):
                if content_hash(root/row["errors"])!=row["errorsSha256"]: raise ValueError("逐点文件被改写")
            x=np.fromfile(candidate/left["errors"],dtype="<f8");y=np.fromfile(reference/right["errors"],dtype="<f8")
            if x.size!=left["sampleCount"] or y.size!=x.size: raise ValueError("逐点文件截断")
            if not np.all(np.isfinite(x)) or not np.all(np.isfinite(y)):
                item["status"]="invalid-domain"
            elif not np.array_equal(x<0,y<0):
                item["status"]="visibility-mismatch"
            else:
                mask=x>=0
                if not mask.any(): item["status"]="no-visible-samples"
                else:
                    ids=np.flatnonzero(mask);delta=x[mask]-y[mask];j=int(np.argmax(delta))
                    item.update(status="paired",DmaxSamplePx=float(delta[j]),witnessOrdinal=int(ids[j]),
                        visible=int(mask.sum()),candidateMesh=left["meshHash"],referenceMesh=right["meshHash"],
                        candidateFaces=left["faces"],referenceFaces=right["faces"])
        rows.append(item)
    result={"schemaVersion":"eip-dmax-v1","candidate":str(candidate),"reference":str(reference),
        "definition":"max_q(e_candidate(q)-e_reference(q)), same reference-visible sampled domain",
        "isAbsoluteQualityReplacement":False,"frames":rows}
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    with output.open("x") as f:
        import json;json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    return output
