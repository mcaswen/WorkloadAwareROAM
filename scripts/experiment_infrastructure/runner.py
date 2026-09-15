"""独占实验目录、冻结真实输入并运行有限回放；不承担指标归约。"""
from __future__ import annotations
import csv
import hashlib
import json
import os
import platform
import shutil
import subprocess
import time
import zipfile
from pathlib import Path
from .catalog import ROOT, resolve_case, content_hash, load_json


def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(",",":"),ensure_ascii=False).encode()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")


def source_snapshot(output):
    """未提交源文件也归档；第三方依赖由CMake缓存和固定清单单列。"""
    names=subprocess.check_output(["git","ls-files","--cached","--others","--exclude-standard","-z",
        "src","tests","scripts","cmake","CMakeLists.txt","assets/shaders"],cwd=ROOT).decode().split("\0")
    entries={}
    with zipfile.ZipFile(output/"sources.zip","x",compression=zipfile.ZIP_DEFLATED) as archive:
        for name in sorted(set(names)):
            if name and (ROOT/name).is_file():
                entries[name]=content_hash(ROOT/name);archive.write(ROOT/name,name)
    save(output/"sources.json",entries)
    return digest(entries)


def prepare(case, output, executable, mode=None, backend=None, cpu=False):
    output=output.resolve();executable=executable.resolve()
    if not executable.is_file(): raise ValueError("可执行文件不存在")
    # 原目录绝不复用，失败产物也保持独立
    output.mkdir(parents=True,exist_ok=False)
    inputs=output/"inputs";inputs.mkdir()
    shutil.copy2(case,inputs/"case.json")
    resolved=resolve_case(case)
    resolved["backend"]=backend or resolved.get("backend","opengl")
    if mode: resolved["mode"]=mode
    if resolved["algorithm"] == "cbt" and (cpu or resolved["backend"] != "d3d12" or resolved["mode"] == "profile"):
        raise ValueError("CBT实验要求D3D12平台入口")
    for field,stem in (("heightMap","height"),("cameraFile","camera"),("materialFile","material")):
        original=Path(resolved[field]);target=inputs/(stem+original.suffix)
        shutil.copy2(original,target);resolved[field]=str(target)
    save(inputs/"resolved-portable.json",resolved)
    from run_transactional_platform import win
    native=dict(resolved)
    if not cpu:
        for field in ("heightMap","cameraFile","materialFile"): native[field]=win(Path(native[field]))
    save(inputs/"resolved.json",native)
    source_id=source_snapshot(output)
    for parent in (executable.parent, *list(executable.parents)[:4]):
        cache=parent/"CMakeCache.txt"
        if cache.is_file():
            shutil.copy2(cache,inputs/"CMakeCache.txt");break
    workload_keys=("terrain","fileSha256","sampleSha256","width","height","terrainSize","heightScale",
        "cameraSha256","viewWidth","viewHeight","budget","maxDepth","splitPixels","mergePixels","maxFrames")
    workload={key:resolved[key] for key in workload_keys}
    task={**workload,"algorithm":resolved["algorithm"],"heightPolicy":resolved["heightPolicy"],
          "prefix":resolved["prefix"],"backend":resolved["backend"]}
    # 未启用时保留已有任务身份；启用属于明确不同的决策策略
    if resolved.get("flipRecovery",False): task["flipRecovery"]=True
    if resolved["algorithm"] == "cbt":
        task["cbt"] = {key: resolved[key] for key in
            ("cbtCapacity", "cbtArea", "cbtValidation", "cbtGeometry")}
    manifest={
        "schemaVersion":"eip-run-v1","id":output.name,"status":"prepared",
        "workloadId":digest(workload),"taskId":digest(task),"sourceId":source_id,
        "executionId":digest({"task":task,"workers":resolved["workers"],"source":source_id,
            "binary":content_hash(executable),"cpuOnly":cpu}),
        "mode":resolved["mode"],"cpuOnly":cpu,"case":resolved,
        "binary":{"path":str(executable),"sha256":content_hash(executable),"bytes":executable.stat().st_size},
        "commit":subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
        "gitStatus":subprocess.check_output(["git","status","--short"],cwd=ROOT,text=True),
        "environment":{"orchestrator":platform.platform(),"cpuInfo":Path("/proc/cpuinfo").read_text(),
                       "uname":platform.uname()._asdict(),"affinity":sorted(os.sched_getaffinity(0)),
                       "affinityScope":"Linux orchestration only; native Windows default scheduling",
                       "powerPolicy":"uncontrolled; record independent processes, do not claim noise-free",
                       "python":platform.python_version()},
        "evidence":{"passEvidence":False,"decision":"public counters only; no complete logical decision trace",
                    "mesh":"actual renderer/algorithm output, full hash outside timed update",
                    "hashCacheEffect":"all modes hash after every opportunity; inter-frame cache may be affected",
                    "capture":"only visual mode; readback included in visual presentMs",
                    "quality":"offline from actual exported float mesh; no evaluator in timing mode"}}
    if resolved["algorithm"] == "cbt":
        manifest["cbtSourceManifest"] = content_hash(ROOT / "docs/codebase/cbt_2024/source_manifest.json")
        manifest["evidence"].update(
            mesh="same-generation actual GPU draw/active indices/render vertices; evidence frames only",
            hashCacheEffect="no full mesh hash/readback in timing mode",
            capacity="dynamic slot pool plus six base slots; budget is CPU comparison label only",
            counters="delayed, attributed by resource/classification generation in cbt.csv",
            gpuTiming="compute and draw timestamps have independent sample generations; never sum across generations")
    if not cpu:
        result=subprocess.run(["/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe","-NoProfile","-Command",
            "Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors | ConvertTo-Json -Compress"],
            capture_output=True,text=True,timeout=30)
        manifest["environment"]["nativeCpu"]=result.stdout.strip()
    save(output/"manifest.json",manifest)
    return manifest


def finalize(output, manifest, result):
    manifest["process"]=result
    manifest["status"]=result["status"]
    frames=output/"run/frames.csv"
    if result["status"]=="ok":
        with frames.open(newline="") as stream:
            reader=csv.DictReader(stream); rows=list(reader)
        if not rows or any(None in row or any(v is None for v in row.values()) for row in rows):
            manifest["status"]="invalid-record"
            save(output/"manifest.json",manifest);raise ValueError("帧记录缺列或错列")
        if [int(row["frame"]) for row in rows] != list(range(len(rows))):
            raise ValueError("机会序列不连续")
        from PIL import Image
        for path in (output/"run").glob("*.ppm"):
            with Image.open(path) as image: image.save(path.with_suffix(".png"))
        manifest["frameCount"]=len(rows)
    manifest["artifacts"]={str(p.relative_to(output)):{"sha256":content_hash(p),"bytes":p.stat().st_size}
        for p in sorted(output.rglob("*")) if p.is_file() and p.name!="manifest.json"}
    save(output/"manifest.json",manifest)
    return manifest


def cpu_resource_limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS,(8*1024**3,8*1024**3))


def run(case,output,executable,mode=None,backend=None,cpu=False):
    output=output.resolve();executable=executable.resolve()
    started=time.perf_counter()
    manifest=prepare(case,output,executable,mode,backend,cpu)
    manifest["prepareSeconds"]=time.perf_counter()-started
    save(output/"manifest.json",manifest)
    if manifest["mode"]=="profile":
        raise ValueError("profile需FPR采集器控制；已保留prepared输入，可通过profile子命令采集")
    if cpu:
        argv=[str(executable),"--experiment-cpu",str(output/"inputs/resolved.json"),str(output/"run")]
        start=time.perf_counter()
        try:
            result=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=180,preexec_fn=cpu_resource_limits)
            (output/"stdout.txt").write_text(result.stdout);(output/"stderr.txt").write_text(result.stderr)
            record={"status":"ok" if result.returncode==0 else "failed","returncode":result.returncode,
                    "elapsedSeconds":time.perf_counter()-start,"argv":argv}
        except subprocess.TimeoutExpired as error:
            record={"status":"censored","elapsedSeconds":time.perf_counter()-start,"reason":str(error),"argv":argv}
    else:
        from run_transactional_platform import native_run,win
        record=native_run(output,"run",executable,"custom",manifest["case"]["algorithm"],
            manifest["case"]["workers"],manifest["mode"],
            arguments=["--experiment-run",win(output/"inputs/resolved.json"),win(output/"run")])
    result=finalize(output,manifest,record)
    if result["status"]!="ok": raise RuntimeError(f"回放未通过，证据保留在 {output}")
    return output/"manifest.json"


def compare_modes(paths):
    """跨模式仅比较同任务、同二进制、同线程和同深度约定的真实结果。"""
    manifests=[load_json(p/"manifest.json") for p in paths]
    if any(m["status"]!="ok" for m in manifests): raise ValueError("存在失败运行")
    if len({m["executionId"] for m in manifests})!=1: raise ValueError("执行身份不同，不能合并")
    if manifests[0]["case"]["algorithm"] == "cbt":
        from .result_adapters import load_run
        loaded = [load_run(path) for path in paths]
        keys = ("frame", "sample", "poseHash", "projectionHash")
        sequences = [[tuple(frame[key] for key in keys) for frame in data["frames"]] for data in loaded]
        if any(sequence != sequences[0] for sequence in sequences):
            raise ValueError("CBT采集模式的输入/机会序列不同")
        return {
            "status": "input-and-generation-contracts-only", "frames": len(sequences[0]),
            "runs": [str(path) for path in paths], "checked": keys,
            "meshEquivalence": "not established across GPU processes; timing has no full mesh hash",
        }
    columns=("frame","sample","poseHash","projectionHash","faces","sequence","hash",
        "split","merge","status","updated","cold","seedFaces","samples","raw","examined","receivers","need",
        "feasible","exchanges","free","pairs","conflicts","donorReuse","touches","evaluations","vertexWrites","indexWrites")
    values=[]
    for path in paths:
        with (path/"run/frames.csv").open(newline="") as stream:
            values.append([[r[c] for c in columns] for r in csv.DictReader(stream)])
    if any(v!=values[0] for v in values): raise ValueError("采集模式改变了观测结果")
    return {"status":"pass","frames":len(values[0]),"runs":[str(p) for p in paths],
        "checked":columns,"notClaimed":"complete decision trace; cross-OS bit equivalence"}
