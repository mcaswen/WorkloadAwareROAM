"""按显式协议读取证据；旧数据保留历史分组，不推测缺失字段。"""
from __future__ import annotations
import csv
import math
from pathlib import Path
from .catalog import content_hash,load_json,local_path

TEXT={"event","poseHash","projectionHash","hash","image","artifact"}
FIELDS="frame,sample,event,warmup,poseHash,projectionHash,faces,budget,workers,sequence,hash,cpuMs,uploadMs,uploadBytes,beginMs,waitMs,renderMs,presentMs,frameMs,evidenceMs,split,merge,splitScoreMs,mergeScoreMs,splitTopologyMs,mergeTopologyMs,meshEmitMs,status,updated,cold,seedFaces,samples,raw,examined,receivers,need,feasible,exchanges,free,pairs,conflicts,donorReuse,touches,evaluations,vertexWrites,indexWrites,seedMs,initializeMs,viewMs,receiverMs,donorMs,reservationMs,topologyPrepareMs,topologyPublishMs,sampleRepairMs,meshPrepareMs,continuationMs,adapterMs,image,artifact".split(",")


def rows(path,required,exact=False):
    with path.open(newline="") as stream:
        reader=csv.DictReader(stream)
        if not reader.fieldnames or len(reader.fieldnames)!=len(set(reader.fieldnames)):
            raise ValueError("表头缺失或重复")
        if (exact and reader.fieldnames!=required) or not set(required).issubset(reader.fieldnames):
            raise ValueError(f"协议列不符: {path}")
        result=list(reader)
    if any(None in r or any(v is None for v in r.values()) for r in result):
        raise ValueError(f"截断/多列CSV: {path}")
    return result


def number(value):
    if value=="": return None
    value=float(value)
    if not math.isfinite(value): raise ValueError("非有限指标")
    return value


def verify(run):
    manifest=load_json(run/"manifest.json")
    if manifest.get("schemaVersion")!="eip-run-v1": raise ValueError("未知运行协议")
    for name,identity in manifest.get("artifacts",{}).items():
        path=local_path(run,name)
        if not path.is_file() or content_hash(path)!=identity["sha256"]:
            raise ValueError(f"产物内容被修改: {name}")
    return manifest


def load_run(run):
    run=run.resolve();manifest=verify(run)
    result={"schema":"eip-run-v1","path":str(run),"manifest":manifest,"frames":[]}
    if manifest["status"]!="ok": return result
    raw=rows(run/"run/frames.csv",FIELDS,True)
    if len(raw)!=manifest["frameCount"]: raise ValueError("记录机会数不符")
    cbt = manifest["case"]["algorithm"] == "cbt"
    cbt_rows = None
    if cbt:
        cbt_rows = rows(run/"run/cbt.csv", ["frame", "resourceGeneration", "topologyGeneration",
            "captureResource", "captureGeneration", "actualFaces", "faults"])
        if len(cbt_rows) != len(raw):
            raise ValueError("CBT记录与机会数不符")
        result["cbt"] = cbt_rows
    for i,row in enumerate(raw):
        parsed={k:v if k in TEXT else number(v) for k,v in row.items()}
        if parsed["frame"] != i:
            raise ValueError("机会序号不符")
        if cbt:
            record = cbt_rows[i]
            if int(record["frame"]) != i or int(record["faults"]) != 0:
                raise ValueError("CBT机会/故障恢复无效")
            if parsed["faces"] is not None:
                if (record["captureResource"] != record["resourceGeneration"] or
                    record["captureGeneration"] != record["topologyGeneration"] or
                    int(record["actualFaces"]) != parsed["faces"] or
                    parsed["faces"] > manifest["case"]["cbtCapacity"] + 6):
                    raise ValueError("CBT实际捕获代/数量无效")
            elif parsed["hash"] or parsed["artifact"] or record["actualFaces"]:
                raise ValueError("CBT缺网格却存在虚假证据")
        elif parsed["faces"] is None or parsed["faces"] > parsed["budget"]:
            raise ValueError("CPU硬预算不符")
        # OpenGL现有后端未测GPU wait，零占位不能当成测量
        if manifest["case"]["backend"]=="opengl": parsed["waitMs"]=None
        result["frames"].append(parsed)
    return result


def historical(spec):
    """历史数据必须由调用者指定schema，不能和本轮统计总体自动合并。"""
    root=Path(spec["path"]).resolve();kind=spec["schema"]
    if kind=="sve-timing-v1":
        table=root/"analysis/timing.csv"
        data=rows(table,["policy","budget","mode","meanFaces","meanMs","movingMs","staticMs"])
        freeze=root/"freeze/manifest.json"
        if not freeze.is_file(): raise ValueError("SVE输入冻结缺失")
        return {"schema":kind,"path":str(root),"sha256":content_hash(table),
            "freezeSha256":content_hash(freeze),"rows":data,"scope":"historical-only",
            "qualityStatus":"历史continuous quality残余，不能作为同质量胜出"}
    if kind=="tpi-frames-v1":
        table=root/"frames.csv"
        data=rows(table,["frame","sample","ok","faces","sequence","hash","cpuMs","uploadMs","exchanges"])
        return {"schema":kind,"path":str(root),"sha256":content_hash(table),"rows":data,
                "scope":"historical-only","identity":spec.get("identity","caller must supply original command manifest")}
    if kind=="fpr-perf-v1":
        summary=load_json(root/"summary.json")
        if not {"roi_samples","self","inclusive"}.issubset(summary):
            raise ValueError("FPR perf summary不符")
        from profiling.report import read_windows,summarize_perf
        from .profile_adapter import full_symbols
        windows_path=root/"windows.csv"
        if windows_path.is_file() and (root/"perf-script.txt").is_file():
            windows=read_windows(windows_path.read_text())
            selected=[w for w in windows if w["round"]>=spec.get("warmup",0)]
            if not selected: raise ValueError("所选ROI为空")
            text=(root/"perf-script.txt").read_text()
            summary=summarize_perf(text,selected)
            summary["functions"]=full_symbols(text,selected)
            summary["selectedWarmup"]=spec.get("warmup",0)
        return {"schema":kind,"path":str(root),"sha256":content_hash(root/"summary.json"),"summary":summary,
                "scope":spec.get("scope","explicit profile ROI; not Windows timing")}
    if kind=="fpr-tracy-v1":
        from profiling.tracy_report import read_zones,summarize_tracy
        text=(root/"zones.csv").read_text()
        frames=[z for z in read_zones(text) if z["name"]=="profile.frame"]
        return {"schema":kind,"path":str(root),"sha256":content_hash(root/"zones.csv"),
            "summary":summarize_tracy(text,len(frames)),"zones":read_zones(text),
            "scope":"one actual capture; not Windows time"}
    raise ValueError("不支持的历史协议")
