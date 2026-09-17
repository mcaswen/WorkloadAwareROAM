"""QPC-04G有限编排：只重放冻结轨迹，不把反事实高度写回生产状态。"""

import argparse
from collections import Counter
import csv
import json
from pathlib import Path
import resource
import statistics
import time

from run_transactional_platform import ROOT, identity, native_run, win, write
from transactional_proposal_feasibility import analyze


OUTPUT = ROOT / "benchmark-output/cpu-refinement/qpc-04g/run-01"
DATA = ROOT / "docs/research/cpu_refinement/data/qpc_04g_proposal_feasibility"
PROBE = ROOT / "build/relwithdebinfo-d3d12-fetch/tests/RelWithDebInfo/parallel_roam_experiment_cpu.exe"
ANALYSIS = "analysis-final"


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def rows(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def frozen_inputs(output):
    """离线核查只消费冻结输入，不要求当前磁盘上的程序仍是捕获版本。"""
    frozen = read(output/"freeze.json")
    for case in frozen["cases"]:
        for field in ("resolved", "witnesses"):
            if identity(Path(case[field]["path"])) != case[field]:
                raise RuntimeError("冻结输入发生变化")
    return frozen


def freeze(output):
    """原生运行才登记/检查程序身份，拒绝在同一捕获目录混用二进制。"""
    frozen = frozen_inputs(output)
    file = output/"audit-program.json"
    current = dict(binary=identity(PROBE), protocol=frozen["protocol"])
    if file.exists() and read(file) != current:
        raise RuntimeError("捕获后程序改变，禁止混合见证复核")
    write(file, current)
    return frozen


def consumed(output):
    native = sum(read(p).get("elapsedSeconds", 0) for p in (output/"commands").glob("*.json"))
    offline = sum(read(p).get("seconds", 0) for p in output.glob("analysis*/*.json"))
    return native+offline


def capture(output, verify=False):
    frozen = freeze(output)
    for case in frozen["cases"]:
        name = case["case"]
        witness = output/(name+"-witnesses.json")
        if verify and (not witness.exists() or not read(witness)["entries"]):
            continue
        if consumed(output) >= 1500:
            raise RuntimeError("本轮总时间配额已到，保留部分证据")
        spec = dict(protocol="qpc04g-v1", requests=[dict(frame=f, roots=r) for f,r in zip(case["frames"],case["roots"])])
        if verify:
            spec["witnessFile"] = win(witness)
        suffix = "verify" if verify else "capture"
        file = output/(name+"-"+suffix+"-spec.json")
        write(file, spec)
        target = output/(name+"-"+suffix)
        run = native_run(output, name+"-"+suffix, PROBE, name, "transactional", 8, "audit",
                         arguments=["--recovery-trace", win(Path(case["resolved"]["path"])),
                                    win(Path(case["witnesses"]["path"])), win(target),
                                    "--proposal-feasibility", win(file)])
        if run["status"] != "ok":
            raise RuntimeError(name+" "+run["status"])
        # 全部离散决策、工作计数和几何hash逐帧一致后，才复用04F的独立质量。
        prior = Path(case["resolved"]["path"]).parents[2]/(name+"-B-trace")
        for filename in ("frames.csv", "boundary-refinement.csv"):
            if rows(prior/filename) != rows(target/filename):
                raise RuntimeError("只读诊断改变原轨迹："+filename)
        write(output/(name+"-"+suffix+"-comparison.json"), dict(prior=str(prior), frames=len(rows(target/"frames.csv")),
              exactDecisionRows=True, hashAndBudget=True))


def analyze_all(output):
    frozen = frozen_inputs(output)
    analysis = output/ANALYSIS
    analysis.mkdir(exist_ok=True)
    started = time.monotonic()
    used = sum(read(p).get("seconds", 0) for p in output.glob("analysis*/*.json"))
    total_samples = 0
    executed_seconds = 0.0
    entries = []
    for case in frozen["cases"]:
        directory = output/(case["case"]+"-capture")
        source = read(directory/"feasibility-source.json")
        witnesses = []
        for frame, roots in zip(case["frames"], case["roots"]):
            for root in roots:
                files = sorted(directory.glob(f"proposal-{frame}-{root}-*.json"),
                               key=lambda p: int(p.stem.rsplit("-",1)[1]))
                if not files:
                    raise RuntimeError("冻结根没有完整目录")
                for file in files:
                    raw = read(file)
                    total_samples += len(raw["samples"])
                    target = analysis/(case["case"]+"-"+file.name)
                    if target.exists():
                        result = read(target)
                    elif total_samples > 2000000 or used+time.monotonic()-started >= 900 or consumed(output)>=1500:
                        result = dict(status="censored", reason="total_cap", seconds=0,
                                      **{k:raw["input"][k] for k in ("frame","root","ordinal","kind")})
                    else:
                        result = analyze(raw, source, time.monotonic()+20)
                        executed_seconds += result["seconds"]
                    result.update(input=identity(file), case=case["case"])
                    write(target, result)
                    entries.append(result)
                    print(case["case"], file.stem, result["status"], round(result["seconds"],3), flush=True)
                    if result["status"] == "feasible_witness" and not result.get("fixedHeight"):
                        witnesses.append(dict(frame=frame, root=root, ordinal=raw["input"]["ordinal"],
                                              height=result["witness"]["height"], binding=raw["binding"]))
        write(output/(case["case"]+"-witnesses.json"),
              dict(sourceFile=win(directory/"feasibility-source.json"), entries=witnesses))
    wall_seconds = time.monotonic()-started
    write(output/(ANALYSIS+"-summary.json"), dict(protocol="qpc04g-v1", records=entries,
          capturedSampleRecords=total_samples, seconds=sum(r["seconds"] for r in entries),
          wallSeconds=wall_seconds, executedSolverSeconds=executed_seconds,
          controlAndIoSeconds=wall_seconds-executed_seconds,
          peakRssKiB=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
          counts=dict(Counter(r["status"] for r in entries))))


def report(output):
    """精简证据进入文档，完整几何/样本继续留在忽略目录。"""
    summary = read(output/(ANALYSIS+"-summary.json"))
    write(DATA/"summary.json", summary)
    frozen = read(output/"freeze.json")
    write(DATA/"freeze.json", {k:v for k,v in frozen.items() if k!="sources"})
    write(DATA/"program.json", read(output/"audit-program.json"))
    repeated = {}
    native = []
    for case in frozen["cases"]:
        directory = output/(case["case"]+"-capture")
        data = rows(directory/"feasibility-inputs.csv")
        per_root = {}
        for root in sorted({row["root"] for row in data}, key=int):
            active = [r for r in data if r["root"]==root and r["active"]=="1"]
            full = Counter((r["geometryFingerprint"],r["viewFingerprint"]) for r in active)
            geometry = Counter(r["geometryFingerprint"] for r in active)
            per_root[root] = dict(activeFrames=len(active), uniqueGeometry=len(geometry),
                                  uniqueGeometryView=len(full), repeatedGeometryView=sum(n-1 for n in full.values()),
                                  longestIdenticalInputMultiplicity=max(full.values(),default=0))
        repeated[case["case"]] = per_root
        for file in sorted((output/(case["case"]+"-verify")).glob("proposal-*.json")):
            record = read(file)
            for witness in record["verifiedWitnesses"]:
                native.append(dict(case=case["case"], **{k:record["input"][k] for k in ("frame","root","ordinal","kind")},
                                   **witness))
    write(DATA/"repeated-inputs.json", repeated)
    write(DATA/"native-witnesses.json", native)
    write(DATA/"commands.json", {p.stem:read(p) for p in sorted((output/"commands").glob("*.json"))})
    expected = sum(len(read(p)["entries"]) for p in output.glob("*-witnesses.json"))
    if len(native) != expected:
        raise RuntimeError("原生复核遗漏冻结见证")
    # 分开归约语义、普通路径成本和只读捕获成本，不用诊断时间冒充帧时间。
    for file in ("frames.csv", "boundary-refinement.csv"):
        if rows(output/"normal-before"/file) != rows(output/"normal-after"/file):
            raise RuntimeError("普通路径前后语义不一致")
    timings = {}
    for arm in ("before", "after"):
        values = {}
        for row in rows(output/("normal-"+arm)/"pointwise-work.csv"):
            if row["type"] == "seconds":
                values.setdefault(row["key"], []).append(float(row["value"])*1000)
        timings[arm] = {key:dict(medianMs=statistics.median(v), p95Ms=sorted(v)[int(.95*(len(v)-1))])
                        for key,v in values.items()}
    captures = {}
    for case in frozen["cases"]:
        data = rows(output/(case["case"]+"-capture")/"feasibility-capture.csv")
        captures[case["case"]] = dict(seconds=sum(float(r["seconds"]) for r in data),
              identityScans=sum(int(r["identityScans"]) for r in data), sampleRecords=int(data[-1]["cumulativeSampleRecords"]))
    write(DATA/"costs.json", dict(normalSemanticsEqual=True, qualityStageTimes=timings, captures=captures,
          solverSeconds=summary["seconds"], peakPythonRssKiB=summary["peakRssKiB"],
          analysisWallSeconds=summary["wallSeconds"], controlAndIoSeconds=summary["controlAndIoSeconds"],
          initialSolverSeconds=read(output/"analysis-summary.json")["seconds"],
          closedDomainSolverSeconds=read(output/"analysis-closed-domain-summary.json")["seconds"],
          representativeFinal=read(output/"representative-final.json")))
    visuals(output, summary)


def visuals(output, summary):
    """画实际固定UV目录与安全高度域；区间端点只在展示时转成double。"""
    from experiment_infrastructure.asset_preview import configure_font, plt
    from fractions import Fraction as F
    configure_font()
    chosen = ((599,15,0),(-272,95,0),(4182,48,0),(803,48,1))
    figure, axes = plt.subplots(4, 2, figsize=(13, 14), layout="constrained")
    for row, key in enumerate(chosen):
        result = next(r for r in summary["records"] if (r["root"],r["frame"],r["ordinal"])==key)
        raw = read(Path(result["input"]["path"]))
        data = raw["input"]
        ax = axes[row,0]
        for mesh, color, style, label in ((data["old"],"#777777","--","旧连接"),
                                         (data["new"],"#276FBF","-","未拟合目录连接")):
            points = {p[0]:p[1:3] for p in mesh["points"]}
            for index, face in enumerate(mesh["faces"]):
                xy = [points[v] for v in face+[face[0]]]
                ax.plot([p[0] for p in xy],[p[1] for p in xy],style,color=color,
                        label=label if index==0 else None,alpha=.8)
        point = next(p for p in data["new"]["points"] if p[0]==data["newVertex"])
        ax.scatter([point[1]],[point[2]],marker="*",color="#C33C54",s=90,label="唯一新高度位置")
        ax.set_aspect("equal")
        ax.set_xlabel("u"); ax.set_ylabel("v")
        ax.set_title(f"{result['case'].split('-b')[0]} / f{key[1]} / root {key[0]} / {result['kind']}{key[2]}")
        ax.legend(fontsize=8)
        ax = axes[row,1]
        low, high = (float(F(result["outer"][v])) for v in ("low","high"))
        if low==high:
            ax.scatter([low],[0],s=70,color="#276FBF",label="必要域单点 = 旧曲面")
        else:
            ax.plot([low,high],[0,0],lw=9,color="#82B69A",label="核心安全域内/外界（此尺度重合）")
        ax.scatter([raw["fitHeight"]],[.25],marker="x",color="#C33C54",s=65,label="旧Fit返回/停留高度")
        if "witness" in result:
            witness = result["witness"]
            ax.scatter([witness["height"]],[0],marker="*",s=110,color="#9A6300",zorder=5,label="双域认证高度")
            delta = float(F(witness["domains"][0]["deltaLower"]))
            note = f"核心 ΔΨ ≥ {delta:.6g} px²；旧最大值门槛域为空"
        else:
            note = "完整Q上仅能复现旧曲面，ΔΨ = 0"
        ax.text(.02,.95,note,transform=ax.transAxes,va="top",fontsize=10)
        ax.set_ylim(-.2,.6); ax.set_yticks([]); ax.set_xlabel("新点世界高度 z")
        ax.legend(fontsize=8,loc="upper right",bbox_to_anchor=(1,.82))
        ax.grid(axis="x",alpha=.2)
    figure.suptitle("QPC-04G：固定目录的局部可行性，不是分叉轨迹恢复结果",fontsize=15)
    figure.savefig(DATA/"proposal-feasibility.png",dpi=150)
    plt.close(figure)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("freeze","capture","analyze","verify","report"))
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    {"freeze":freeze,"capture":capture,"analyze":analyze_all,
     "verify":lambda p:capture(p,True),"report":report}[args.action](args.output)
