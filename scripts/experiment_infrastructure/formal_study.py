"""冻结协议驱动的有限实验；保留FER-01默认路径并复用共享工具。"""
from __future__ import annotations
import argparse
import csv
import json
import random
import time
from pathlib import Path
from .catalog import ROOT, resolve_case, content_hash, load_json
from .runner import run, save, compare_modes

CONFIG = ROOT / "configs/experiments/formal/fer_01"
RAW = ROOT / "benchmark-output/experiment-infrastructure/fer-01"
APP = ROOT / "build/relwithdebinfo-fetch/bin/ParallelROAM.exe"
PROBE = Path("/home/mcaswen/workload-roam-nmp01p-build/tests/parallel_roam_transactional_platform_quality_probe")


def prepare():
    CONFIG.mkdir(parents=True, exist_ok=False)
    cells = [("peking547", b, "pq-return24", 24, .25, .1) for b in (50000,100000,200000)]
    cells += [("generated-ridge",50000,"overview-orbit",96,4,2),
              ("dem-canyon",50000,"reveal-return",96,4,2),
              ("dem-sierra",50000,"reveal-return",96,4,2)]
    entries=[]
    for terrain,budget,camera,frames,split,merge in cells:
        variants=[("classic",1),("dod",8),("transactional",8)]
        if budget==50000 and terrain in ("peking547","dem-canyon"):
            variants.append(("transactional",1))
        for algorithm,workers in variants:
            ident=f"{terrain}-b{budget}-{algorithm}-t{workers}"
            case=dict(schemaVersion=1,id=ident,terrain=terrain,algorithm=algorithm,
                heightPolicy="immutable" if algorithm=="transactional" else "fit",
                flipRecovery=algorithm=="transactional",camera=camera,material="neutral",
                mode="timing",prefix="scaled",budget=budget,workers=workers,maxDepth=20,
                viewWidth=1280,viewHeight=720,splitPixels=split,mergePixels=merge,
                backend="opengl",warmup=3,captureStride=8,maxFrames=frames)
            path=CONFIG/(ident+".json");save(path,case)
            resolved=resolve_case(path)
            entries.append(dict(id=ident,path=str(path.relative_to(ROOT)),sha256=content_hash(path),
                terrain=terrain,budget=budget,algorithm=algorithm,workers=workers,
                visual=not(algorithm=="transactional" and workers==1),
                qualityFrames=[2,15,16,23] if frames==24 else [2,48,80,95],
                cameraSha256=resolved["cameraSha256"],sampleSha256=resolved["sampleSha256"]))
    rng=random.Random(20260915);schedule=[]
    for block in (1,2,3):
        order=list(range(len(entries)));rng.shuffle(order)
        schedule += [dict(block=block,id=entries[i]["id"],runId=f"r{block}-"+entries[i]["id"]) for i in order]
    save(CONFIG/"protocol.json",dict(schemaVersion="fer-01-v1",seed=20260915,
        repetitions=3,statisticalUnit="independent process; frames correlated",cases=entries,schedule=schedule,
        qualityBudgetsPx=[0,.1,.25,.5,1],noResultDependentChanges=True,
        backend="opengl",limits=dict(secondsPerProcess=180,gibPerProcess=8),
        mainMetric="mean cpuMs over opportunities >=3, then mean across 3 processes",
        uncertainty="all 3 process values and min/max; no CI or significance claim"))


def freeze():
    protocol = load_json(CONFIG / "protocol.json")
    for entry in protocol["cases"]:
        case = ROOT / entry["path"]
        resolved = resolve_case(case)
        if content_hash(case) != entry["sha256"]:
            raise ValueError("case与协议不符")
        if any(resolved[key] != entry[key] for key in ("cameraSha256", "sampleSha256")):
            raise ValueError("资产或路线与协议不符")
    RAW.mkdir(parents=True,exist_ok=False)
    save(RAW/"freeze.json",dict(protocolSha256=content_hash(CONFIG/"protocol.json"),
        executableSha256=content_hash(APP),probeSha256=content_hash(PROBE),
        recordedUtc=time.strftime("%Y-%m-%dT%H:%M:%SZ",time.gmtime())))


def collect(mode):
    protocol=load_json(CONFIG/"protocol.json");frozen=load_json(RAW/"freeze.json")
    if content_hash(CONFIG/"protocol.json")!=frozen["protocolSha256"] or content_hash(APP)!=frozen["executableSha256"]:
        raise ValueError("冻结协议或程序已改变")
    cases={e["id"]:e for e in protocol["cases"]}
    tasks=protocol["schedule"] if mode=="timing" else [dict(id=e["id"],runId="visual-"+e["id"]) for e in cases.values() if e["visual"]]
    for index,task in enumerate(tasks):
        entry=cases[task["id"]];case=ROOT/entry["path"];target=RAW/task["runId"]
        if content_hash(case)!=entry["sha256"]: raise ValueError("case与冻结清单不符")
        if target.exists():
            manifest = target / "manifest.json"
            if not manifest.exists():
                print("保留未完成目录", target.name, flush=True)
                continue
            old = load_json(manifest)
            if old["binary"]["sha256"] != frozen["executableSha256"] or old["case"]["id"] != entry["id"]:
                raise ValueError("已有运行身份与冻结配置不符")
            print("保留已有运行", target.name, flush=True)
            continue
        print(f"{mode} {index+1}/{len(tasks)} {target.name}",flush=True)
        try:
            run(case,target,APP,mode)
        except Exception as error:
            save(RAW/(task["runId"]+"-failure.json"),dict(error=str(error),case=entry,status="failed-or-censored"))
            print("失败保留",str(error),flush=True)


def verify():
    protocol=load_json(CONFIG/"protocol.json");checks=[]
    for entry in protocol["cases"]:
        paths=[RAW/f"r{i}-{entry['id']}" for i in range(1, protocol["repetitions"] + 1)]
        if entry["visual"]: paths.append(RAW/("visual-"+entry["id"]))
        try:
            result=compare_modes(paths)
            flip_equal = None
            if entry["algorithm"] == "transactional":
                flips = [list(csv.DictReader((path / "run/flip-recovery.csv").open())) for path in paths]
                if any(rows != flips[0] for rows in flips):
                    raise ValueError("翻边计数在重复或采集模式间不符")
                flip_equal = True
            checks.append(dict(id=entry["id"], result=result, flipCountsEqual=flip_equal))
        except Exception as error: checks.append(dict(id=entry["id"],error=str(error)))
    save(RAW/"mode-and-repeat-checks.json",checks)
    if any("error" in row for row in checks): print("存在失败或结果差异，必须在报告中保留",flush=True)


def quality():
    from .quality import evaluate, pointwise_pair
    protocol=load_json(CONFIG/"protocol.json")
    if content_hash(PROBE)!=load_json(RAW/"freeze.json")["probeSha256"]: raise ValueError("评价器改变")
    for entry in protocol["cases"]:
        if not entry["visual"]: continue
        source=RAW/("visual-"+entry["id"]);target=RAW/("quality-"+entry["id"])
        if target.exists(): continue
        manifest=source/"manifest.json"
        if not manifest.exists() or load_json(manifest)["status"]!="ok": continue
        print("quality",entry["id"],flush=True)
        evaluate(source,target,PROBE,entry["qualityFrames"])
    pairings = protocol.get("pairs")
    if pairings is None:
        pairings = [dict(candidate=entry["id"],
                        reference=f"{entry['terrain']}-b{entry['budget']}-dod-t8")
                    for entry in protocol["cases"]
                    if entry["algorithm"] == "transactional" and entry["visual"]]
    for pair in pairings:
        candidate = pair["candidate"]
        reference = pair["reference"]
        output = RAW / ("dmax-" + candidate + "--" + reference + ".json")
        if output.exists():
            continue
        try:
            pointwise_pair(RAW / ("quality-" + candidate), RAW / ("quality-" + reference),
                           output, allow_different_budget=pair.get("differentBudget", False))
        except Exception as error:
            save(output, dict(status="unpaired", candidate=candidate, reference=reference, error=str(error)))


def analyze():
    from .analysis import build
    runs=sorted(p.parent for p in RAW.glob("*/manifest.json"))
    quality_paths=sorted(p.parent for p in RAW.glob("quality-*/quality-index.json"))
    pairs=[p for p in sorted(RAW.glob("dmax-*.json")) if "frames" in load_json(p)]
    protocol = load_json(CONFIG / "protocol.json")
    expected = protocol["schedule"] + [
        dict(id=entry["id"], runId="visual-" + entry["id"])
        for entry in protocol["cases"] if entry["visual"]]
    protocol["runCompleteness"] = [
        dict(runId=task["runId"], case=task["id"],
             status=load_json(RAW / task["runId"] / "manifest.json")["status"]
             if (RAW / task["runId"] / "manifest.json").exists() else "missing-manifest")
        for task in expected]
    return build(runs, RAW / "analysis", quality_paths, pair_paths=pairs, study=protocol)


def main():
    global CONFIG, RAW, APP, PROBE
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["prepare", "freeze", "timing", "visual", "verify", "quality", "analyze"])
    parser.add_argument("--config", type=Path, default=CONFIG)
    parser.add_argument("--raw", type=Path, default=RAW)
    parser.add_argument("--executable", type=Path, default=APP)
    parser.add_argument("--probe", type=Path, default=PROBE)
    arguments = parser.parse_args()
    CONFIG, RAW = arguments.config.resolve(), arguments.raw.resolve()
    APP, PROBE = arguments.executable.resolve(), arguments.probe.resolve()
    if arguments.action == "prepare" and CONFIG != (ROOT / "configs/experiments/formal/fer_01"):
        parser.error("新协议使用独立准备模块；prepare只保留FER-01历史定义")
    if arguments.action in ("timing", "visual"):
        collect(arguments.action)
    else:
        print(globals()[arguments.action]())


if __name__ == "__main__":
    main()
