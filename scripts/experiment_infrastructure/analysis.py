"""唯一统计归约；图表不得重新定义分母或从相关帧制造统计重复。"""
from __future__ import annotations
import statistics
import time
from pathlib import Path
from .catalog import content_hash,load_json
from .runner import save
from .result_adapters import load_run,historical

STAGES=("seedMs","initializeMs","viewMs","receiverMs","donorMs","reservationMs",
        "topologyPrepareMs","topologyPublishMs","sampleRepairMs","meshPrepareMs","continuationMs","adapterMs")
PASS=("splitScoreMs","mergeScoreMs","splitTopologyMs","mergeTopologyMs","meshEmitMs")
COUNTS=("raw","examined","receivers","need","feasible","exchanges","free","pairs","conflicts","donorReuse",
        "touches","evaluations","vertexWrites","indexWrites")
METRICS=("cpuMs","frameMs","uploadMs","uploadBytes","beginMs","waitMs","renderMs","presentMs","evidenceMs","faces",*STAGES,*PASS,*COUNTS)


def mean(values):
    values=[x for x in values if x is not None]
    return statistics.mean(values) if values else None


def phase(frames,i):
    f=frames[i];event=f["event"].lower()
    if i==0: return "cold"
    if f["warmup"]: return "warmup"
    if "return" in event or "recover" in event or "hold" in event: return "return-recovery"
    if f["poseHash"]==frames[i-1]["poseHash"]: return "stationary"
    return "moving"


def summarize(run):
    m=run["manifest"];frames=run["frames"]
    entry={"id":m["id"],"path":run["path"],"status":m["status"],"mode":m["mode"],
        "taskId":m["taskId"],"executionId":m["executionId"],"workloadId":m["workloadId"],
        "case":m["case"],"binary":m["binary"],"sourceId":m["sourceId"],"cpuOnly":m["cpuOnly"],
        "groups":{},"frames":frames,"statisticalUnit":"one independent process",
        "qualityStatus":"not established by timing"}
    if not frames: return entry
    for i,frame in enumerate(frames): frame["phase"]=phase(frames,i);frame["utilization"]=frame["faces"]/frame["budget"]
    groups={"all":frames,"warm":[f for f in frames if not f["warmup"]]}
    for name in ("cold","warmup","stationary","moving","return-recovery"):
        groups[name]=[f for f in frames if f["phase"]==name]
    for name,data in groups.items():
        if not data: continue
        summary={"count":len(data),**{metric:mean([f[metric] for f in data]) for metric in METRICS}}
        summary["medianCpuMs"]=statistics.median(f["cpuMs"] for f in data)
        summary["maxCpuMs"]=max(f["cpuMs"] for f in data)
        summary["utilization"]=mean([f["utilization"] for f in data])
        costs=STAGES if m["case"]["algorithm"]=="transactional" else PASS
        known=sum(summary[x] or 0 for x in costs)
        summary["unattributedCpuMs"]=summary["cpuMs"]-known
        summary["stageAdditivity"]="do not stack" if summary["unattributedCpuMs"] < -max(.05,.01*summary["cpuMs"]) else "compatible within timer resolution"
        entry["groups"][name]=summary
    entry["totals"]={c:sum(f[c] for f in frames if f[c] is not None) if any(f[c] is not None for f in frames) else None for c in COUNTS}
    entry["transactionFrames"]=sum((f["exchanges"] or 0)+(f["free"] or 0)>0 for f in frames)
    entry["zeroTransactionMeaning"]="not convergence evidence"
    return entry


def build(runs,output,quality_paths=(),history_specs=(),pair_paths=()):
    started=time.perf_counter();output=output.resolve();output.mkdir(parents=True,exist_ok=False)
    loaded=[load_run(Path(p)) for p in runs]
    analysis={"schemaVersion":"eip-analysis-v1","runs":[summarize(r) for r in loaded],
        "history":[historical(s) for s in history_specs],"quality":[],
        "qualityPairs":[{"path":str(Path(p).resolve()),"sha256":content_hash(Path(p)),"result":load_json(Path(p))} for p in pair_paths],
        "comparisons":[],"statisticalUnit":"independent process; frames are correlated",
        "confidenceIntervals":None,"p95Policy":"not reported for this development sample"}
    if len({r["id"] for r in analysis["runs"]})!=len(analysis["runs"]): raise ValueError("报告运行ID重复")
    for p in quality_paths:
        p=Path(p).resolve();q=load_json(p/"quality-index.json")
        if q.get("schemaVersion")!="eip-quality-v1": raise ValueError("未知质量协议")
        if content_hash(Path(q["run"])/"manifest.json")!=q["runManifestSha256"]:
            raise ValueError("质量与运行manifest不匹配")
        for item in q["frames"]:
            if item.get("quality"):
                file=p/item["quality"]
                if content_hash(file)!=item["qualitySha256"]: raise ValueError("质量结果被改写")
                for field,hash_field in (("errors","errorsSha256"),("locations","locationsSha256")):
                    if item.get(field) and content_hash(p/item[field])!=item[hash_field]:
                        raise ValueError("质量逐点/定位产物被改写")
                item["result"]=load_json(file)
        q["path"]=str(p);analysis["quality"].append(q)
    candidates=[r for r in analysis["runs"] if r["status"]=="ok" and r["mode"]=="timing"]
    for i,a in enumerate(candidates):
        for b in candidates[i+1:]:
            if a["workloadId"]!=b["workloadId"] or a["case"]["backend"]!=b["case"]["backend"] or a["cpuOnly"]!=b["cpuOnly"]: continue
            if a["binary"]["sha256"]!=b["binary"]["sha256"]: continue
            if len(a["frames"])!=len(b["frames"]): continue
            same=a["taskId"]==b["taskId"]
            checked=("hash","poseHash","projectionHash",*COUNTS)
            equivalent=all(all(x[k]==y[k] for k in checked) for x,y in zip(a["frames"],b["frames"]))
            am=a["groups"]["warm"]["cpuMs"];bm=b["groups"]["warm"]["cpuMs"]
            analysis["comparisons"].append({"a":a["id"],"b":b["id"],
                "kind":"same-task thread cost ratio" if same and equivalent else "cross-strategy cost ratio; quality separate",
                "sameTask":same,"meshAndCountersEqual":equivalent,"ratioAOverB":am/bm if bm>0 else None,
                "speedupClaimAllowed":same and equivalent and a["case"]["workers"]!=b["case"]["workers"]})
    analysis["analysisSeconds"]=time.perf_counter()-started
    save(output/"analysis.json",analysis)
    return output/"analysis.json"
