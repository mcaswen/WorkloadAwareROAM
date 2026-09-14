"""冻结并顺序执行有限规模实验；预算、额度、线程和停止边界不可事后调参。"""

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
BUDGETS = (20000, 50000, 100000, 200000)
POLICIES = ("fixed64", "scaled")
CPUS = {1: "0", 4: "0,2,4,6", 8: "0,2,4,6,8,10,12,14"}
EXECUTABLES = {"new": "parallel_roam_transactional_lod_probe",
               "dod": "parallel_roam_transactional_lod_family_probe",
               "source": "parallel_roam_greedy_multipass_snapshot_probe"}
SEMANTICS = ("D_raw", "examined", "receivers", "D_need", "D_feasible", "D_executed", "freeExecuted",
             "batchWidth", "assignedCredits", "unusedCredits", "faces", "intents", "pool", "attempts", "exchanges")


def read(path):
    return json.loads(path.read_text())


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def identity(path):
    return {"path": str(path), "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def snapshot(output, budget):
    return output / "freeze" / f"b{budget}" / f"peking547-sve-orbit64-b{budget}-14.json"


def config_directory(output, budget, policy):
    return output / f"b{budget}" / ("fixed64" if budget == 20000 else policy)


def invoke(output, name, argv, workers):
    """进程互不并发；失败和截尾均保留，重复调用不覆盖原始结果。"""
    status = output / "commands" / (name + ".json")
    if status.exists():
        return read(status)
    run = read(output / "run.json")
    record = {"name": name, "argv": ["taskset", "-c", CPUS[workers], *map(str, argv)],
              "started": time.time(), "affinity": CPUS[workers], "status": "pending"}
    if record["started"] - run["collectionStart"] >= 2400:
        record["status"] = "not-run-total-time-cap"
        write(status, record)
        return record
    log = output / "logs" / (name + ".log")
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w") as stream:
        process = subprocess.Popen(record["argv"], cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        peak = 0
        while process.poll() is None:
            try:
                memory = Path(f"/proc/{process.pid}/status").read_text()
                high = next(int(line.split()[1]) for line in memory.splitlines() if line.startswith("VmHWM:"))
                peak = max(peak, high)
            except (OSError, StopIteration):
                pass
            elapsed = time.time() - record["started"]
            if elapsed >= 180 or peak >= 8 * 1024 * 1024 or time.time()-run["collectionStart"] >= 2400:
                record["status"] = "censored-rss" if peak >= 8*1024*1024 else "censored-time"
                os.killpg(process.pid, signal.SIGKILL)
                break
            time.sleep(.1)
        record.update(returncode=process.wait(), elapsedSeconds=time.time()-record["started"], peakRssKiB=peak)
    if record["status"] == "pending":
        record["status"] = "ok" if record["returncode"] == 0 else "failed"
    write(status, record)
    print(name, record["status"], round(record["elapsedSeconds"], 2), flush=True)
    return record


def freeze(output, build):
    if not (output / "run.json").exists():
        env = {"collectionStart": time.time(), "protocol": "sve-01", "affinity": CPUS,
               "budgets": BUDGETS, "limits": {"fixed64": [64]*4, "scaled": [64,160,320,640]},
               "sourceCommit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
               "asset": identity(ROOT / "assets/heightmaps/Hm_Terrain_Peking_513.png"),
               "binaries": {key: identity(build / "tests" / value) for key, value in EXECUTABLES.items()}}
        for key, command in (("cpu", ["lscpu"]), ("topology", ["lscpu", "-e=CPU,CORE,SOCKET,ONLINE"]),
                             ("memory", ["free", "-b"]), ("kernel", ["uname", "-a"])):
            env[key] = subprocess.check_output(command, text=True)
        for name in ("CMakeCache.txt", "compile_commands.json"):
            target = output / "environment" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((build/name).read_bytes())
        tracked = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z",
            "src", "tests", "scripts", "CMakeLists.txt", "cmake"], cwd=ROOT).decode().split("\0")
        write(output / "environment/source-files.json", {n: identity(ROOT/n) for n in sorted(set(tracked)) if n and (ROOT/n).is_file()})
        write(output / "run.json", env)
    for budget in BUDGETS:
        result = invoke(output, f"freeze-{budget}", [build/"tests"/EXECUTABLES["source"], ROOT,
            f"scaling-{budget}", snapshot(output,budget).parent], 4)
        if result["status"] != "ok":
            raise RuntimeError("来源未冻结："+str(result))
    seeds=[]
    camera = None
    for budget in BUDGETS:
        path=snapshot(output,budget);data=read(path);views=read(path.parent/"cameras.json")
        if camera is not None and camera != views:
            raise RuntimeError("不同预算的相机矩阵不同")
        camera=views
        seeds.append({"budget":budget,"faces":len(data["faces"]),"vertices":len(data["vertices"]),
                      "utilization":len(data["faces"])/budget,"snapshot":identity(path),
                      "source":identity(path.parent/(data["scenario"]+"-source.json"))})
    coverage=all(s["utilization"]>=.9 for s in seeds) and seeds[-1]["faces"]/seeds[0]["faces"]>=8
    write(output/"freeze/manifest.json", {"seeds":seeds,"nearSaturatedCoverage":coverage,"cameras":camera})
    print("input coverage", coverage, [s["faces"] for s in seeds], flush=True)


def measure(output, build):
    if not (output/"freeze/manifest.json").exists():
        raise RuntimeError("必须先冻结全部四档来源")
    current=read(output/"run.json")["binaries"]
    if any(identity(build/"tests"/name)["sha256"]!=current[key]["sha256"] for key,name in EXECUTABLES.items()):
        raise RuntimeError("程序身份变化，禁止拼接旧矩阵")
    stopped=False
    for policy in POLICIES:
        for budget in BUDGETS:
            if policy=="scaled" and budget==20000:
                write(output/"b20000/scaled/shared.json", {"reason":"identical configuration", "source":"../fixed64"})
                continue
            directory=config_directory(output,budget,policy);seed=snapshot(output,budget)
            base=[build/"tests"/EXECUTABLES["new"],seed]
            diag=invoke(output,f"{policy}-{budget}-diagnostic", [*base,"trajectory-b-diagnostic",directory/"diagnostic", "--limit-policy",policy],1)
            if diag["status"]!="ok":
                log=(output/"logs"/f"{policy}-{budget}-diagnostic.log")
                text=log.read_text() if log.exists() else ""
                quota=any(term in text for term in ("配额", "超时", "deadline", "额度耗尽"))
                if diag["status"]=="failed" and not quota:
                    stopped=True
                    write(output/"correctness-stop.json", {"configuration":[policy,budget],"log":text})
                    break
                continue
            if policy=="fixed64":
                order=["dod4","c4","b1","c8","dod8"]
                if budget in (50000,200000): order.reverse()
            else:
                order=["b1","c4","c8"]
                if budget==100000: order.reverse()
            for mode in order:
                workers=1 if mode=="b1" else int(mode[-1])
                if mode.startswith("dod"):
                    argv=[build/"tests"/EXECUTABLES["dod"],seed,"dod",output/f"b{budget}"/mode,"--workers",str(workers)]
                    name=f"{budget}-{mode}"
                else:
                    argv=[*base,"trajectory-b-timing" if mode=="b1" else "trajectory-c-timing", directory/mode,"--limit-policy",policy]
                    if mode!="b1": argv += ["--workers",str(workers)]
                    name=f"{policy}-{budget}-{mode}"
                result=invoke(output,name,argv,workers)
                if result["status"]=="failed":
                    write(output/"execution-stop.json", result)
                    raise RuntimeError("计时入口失败，先定位而不继续拼接性能")
        if stopped: break


def audit(output):
    """先逐帧确认同组结果，再复用一次独立 Q 评价；不把诊断开销算入正常速度。"""
    import numpy as np
    rows=[];checks=[]
    for policy in POLICIES:
        for budget in BUDGETS:
            directory=config_directory(output,budget,policy)
            if not (directory/"diagnostic/round-7/quality.json").exists(): continue
            for frame in range(8):
                suffix=f"round-{frame}";diagnostic=read(directory/"diagnostic"/suffix/"summary.json")
                for mode in ("b1","c4","c8"):
                    path=directory/mode/suffix
                    if not (path/"summary.json").exists():continue
                    value=read(path/"summary.json")
                    for name in SEMANTICS:
                        if value[name]!=diagnostic[name]: raise RuntimeError(f"决策不同 {policy}/{budget}/{mode}/{frame}/{name}")
                    if (path/"mesh.json").read_bytes()!=(directory/"diagnostic"/suffix/"mesh.json").read_bytes():
                        raise RuntimeError(f"几何不同 {policy}/{budget}/{mode}/{frame}")
                    checks.append([policy,budget,frame,mode])
                errors=np.fromfile(directory/"diagnostic"/suffix/"errors.f64",dtype="<f8")
                quality=read(directory/"diagnostic"/suffix/"quality.json")
                for workers in (4,8):
                    reference=output/f"b{budget}"/f"dod{workers}"/suffix
                    if not (reference/"quality.json").exists():continue
                    base=np.fromfile(reference/"errors.f64",dtype="<f8")
                    if errors.shape!=base.shape or not np.array_equal(errors>=0,base>=0):
                        raise RuntimeError("逐点误差的参考可见域不同")
                    visible=base>=0;excess=errors[visible]-base[visible]
                    rows.append({"policy":policy,"budget":budget,"round":frame,"workers":workers,
                        "DmaxSamplePx":float(excess.max()) if len(excess) else None,
                        "ours":quality,"dod":read(reference/"quality.json")})
    write(output/"analysis/audit.json", {"matchedFrames":checks,"quality":rows,"complete":len(checks)==192})
    print("audited frame comparisons",len(checks),flush=True)


def report(output, figures=None):
    records=[]
    for policy in POLICIES:
        for budget in BUDGETS:
            directory=config_directory(output,budget,policy)
            for mode in ("b1","c4","c8","dod4","dod8"):
                path=output/f"b{budget}"/mode if mode.startswith("dod") else directory/mode
                frames=[]
                for frame in range(8):
                    if mode.startswith("dod"):
                        if not (path/"trajectory.jsonl").exists():break
                        values=[json.loads(s) for s in (path/"trajectory.jsonl").read_text().splitlines()]
                        if len(values)!=8:break
                        row=values[frame];seconds={"ready":row["buildMs"]/1000};faces=row["faces"]
                    else:
                        if not (path/f"round-{frame}/summary.json").exists():break
                        row=read(path/f"round-{frame}/summary.json");seconds=row["seconds"];faces=row["faces"]
                    frames.append({"round":frame,"faces":faces,"ms":1000*(seconds.get("ready",seconds.get("frame_ready",0))),"seconds":seconds})
                if len(frames)!=8:continue
                times=[r["ms"] for r in frames]
                records.append({"policy":policy,"budget":budget,"mode":mode,"meanMs":statistics.mean(times),
                    "movingMs":statistics.mean(times[3:]),"staticMs":statistics.mean(times[1:3]),"firstMs":times[0],
                    "medianMs":statistics.median(times),"maxMs":max(times),"meanFaces":statistics.mean(r["faces"] for r in frames),
                    "frames":frames})
    def find(policy,budget,mode):
        return next((r for r in records if (r["policy"],r["budget"],r["mode"])==(policy,budget,mode)),None)
    for row in records:
        if row["mode"] in ("c4","c8"):
            baseline=find(row["policy"],row["budget"],"dod"+row["mode"][-1])
            serial=find(row["policy"],row["budget"],"b1")
            four=find(row["policy"],row["budget"],"c4")
            if baseline:row["Rmoving"]=row["movingMs"]/baseline["movingMs"]
            if serial:row["Smoving"]=serial["movingMs"]/row["movingMs"]
            if four:row["S4toThisMoving"]=four["movingMs"]/row["movingMs"]
    write(output/"analysis/timing.json",records)
    with (output/"analysis/timing.csv").open("w") as stream:
        columns=["policy","budget","mode","meanFaces","meanMs","movingMs","staticMs","firstMs","medianMs","maxMs","Rmoving","Smoving","S4toThisMoving"]
        writer=csv.DictWriter(stream,columns,extrasaction="ignore");writer.writeheader();writer.writerows(records)
    from transactional_scaling_report import write_details
    write_details(output, records, BUDGETS, POLICIES, figures)
    print(json.dumps([{k:v for k,v in r.items() if k!="frames"} for r in records],indent=2),flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase",choices=("freeze","measure","audit","report"))
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--build",type=Path,default=Path.home()/"workload-roam-nmp01p-build")
    parser.add_argument("--figures",type=Path,help="report 阶段的静态图输出目录")
    args=parser.parse_args()
    if args.figures and args.phase!="report":parser.error("--figures 只用于 report")
    args.output=args.output.resolve();args.build=args.build.resolve()
    if args.phase=="freeze":freeze(args.output,args.build)
    elif args.phase=="measure":measure(args.output,args.build)
    elif args.phase=="audit":audit(args.output)
    else:report(args.output,args.figures)


if __name__=="__main__":
    main()
