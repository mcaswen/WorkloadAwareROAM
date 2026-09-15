"""以现有FPR后端控制新回放ROI；不更改perf/Tracy采集协议。"""
from __future__ import annotations
from bisect import bisect_right
from collections import Counter
import shutil
import tempfile
from pathlib import Path
from .runner import prepare,finalize,save
from profiling import perf_backend,tracy_backend
from profiling.report import read_windows,summarize_perf,samples
from profiling.tracy_report import summarize_tracy
from .catalog import content_hash


def full_symbols(text,windows):
    starts=[w["start_ns"] for w in windows];own=Counter();inclusive=Counter();counts=Counter();weight=0
    for sample in samples(text):
        j=bisect_right(starts,sample["time_ns"])-1
        if j<0 or sample["time_ns"]>=windows[j]["end_ns"]: continue
        frames=sample["frames"];event=sample["period"];weight+=event
        leaf=frames[0] if frames else "[missing stack]";own[leaf]+=event;counts[leaf]+=1
        for frame in set(frames): inclusive[frame]+=event
    return [{"function":name,"selfWeight":own[name],"inclusiveWeight":inclusive[name],
        "selfSamples":counts[name],"selfPercent":100*own[name]/weight if weight else None,
        "inclusivePercent":100*inclusive[name]/weight if weight else None}
        for name in sorted(set(own)|set(inclusive),key=lambda n:(-own[n],n))]


def collect(case,output,executable,backend="perf",perf="perf",capture=None,csvexport=None):
    output=Path(output).resolve();executable=Path(executable).resolve()
    manifest=prepare(case,output,executable,mode="profile",cpu=True)
    work=Path(tempfile.mkdtemp(prefix="eip-profile-"))
    recorded=work/"recorded-executable";shutil.copy2(executable,recorded)
    args=["--experiment-cpu",str(output/"inputs/resolved.json"),str(output/"run")]
    result={}
    try:
        if backend=="perf":
            result["record"]=perf_backend.record(perf,recorded,args,work,output)
            if not result["record"]["complete"]: raise RuntimeError("perf进程未完整结束")
            result["exports"]=perf_backend.export(perf,work)
        else:
            if not capture or not csvexport: raise ValueError("Tracy需要已冻结0.14.1工具")
            from profiling.environment import command
            versions=[command([str(capture),"--help"]),command([str(csvexport),"-V"])]
            if any("0.14.1" not in x["stdout"]+x["stderr"] for x in versions): raise ValueError("Tracy工具版本不符")
            result["record"]=tracy_backend.capture_tracy(capture,recorded,args,work,cwd=output)
            if not result["record"]["complete_exit"]: raise RuntimeError("Tracy进程未完整结束")
            result["exports"]=tracy_backend.export(csvexport,work)
        if any(v["status"]!="ok" for v in result["exports"].values()): raise RuntimeError("采集导出失败")
        windows=read_windows((output/"run/windows.csv").read_text())
        shutil.copy2(output/"run/windows.csv",work/"windows.csv")
        if backend=="perf":
            text=(work/"perf-script.txt").read_text()
            summary=summarize_perf(text,windows)
            summary["functions"]=full_symbols(text,windows)
            summary["functionCount"]=len(summary["functions"])
        else:
            summary=summarize_tracy((work/"zones.csv").read_text(),len(windows))
        save(work/"summary.json",summary)
        result["status"]="ok"
        result["sampleScreeningSufficient"]=summary.get("sample_screening_sufficient")
        result["collector"]=backend
    except (OSError,ValueError,RuntimeError) as e:
        result.update(status="failed",error=str(e))
    finally:
        shutil.copytree(work,output/"profile",ignore=shutil.ignore_patterns("*.fifo"))
        result["profileBinarySha256"]=content_hash(recorded)
        # 采集结果先落盘，再进入完整产物哈希清单
        save(output/"profile-result.json",result)
        finalize(output,manifest,result)
    if result["status"]!="ok": raise RuntimeError(result["error"])
    return output/"manifest.json"
