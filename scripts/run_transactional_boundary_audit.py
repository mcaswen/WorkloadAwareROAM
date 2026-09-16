"""QPC-04A 有限边界调查：冻结、原生采集和独立几何归约。"""

import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import time

from run_transactional_platform import ROOT, identity, native_run, win, write
from transactional_recovery_geometry import self_test, validate


CASES = ("peking547-b50000-transactional-t8", "peking547-b200000-transactional-t8",
         "dem-canyon-b50000-transactional-t8")
PRIOR = ROOT / "benchmark-output/cpu-refinement/qpc-01/run-01"


def statistics_for(rows, key):
    values = sorted(float(row[key]) for row in rows)
    position = .95 * (len(values) - 1)
    lower, upper = math.floor(position), math.ceil(position)
    return {"mean": statistics.mean(values), "median": statistics.median(values),
            "p95": values[lower] + (values[upper] - values[lower]) * (position - lower), "max": values[-1]}


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def collect(output, probe):
    """选样只使用旧冻结第一见证，有限调查不替换实际批次。"""
    if (output / "freeze.json").exists():
        raise RuntimeError("拒绝覆盖已有边界调查，请使用新的输出目录")
    previous = read(PRIOR / "selection.json")["cases"]
    frozen = {"protocol": "qpc04a-boundary-midpoint-v1", "binary": identity(probe), "cases": {},
              "limits": {"edges": 6, "variants": 2, "sampleVisits": 2000000, "seconds": 60},
              "observerSources": {path: identity(ROOT / path) for path in (
                  "src/experiment/greedy_transactional_lod/TransactionalBoundaryAudit.h",
                  "src/experiment/greedy_transactional_lod/TransactionalBoundaryAudit.cpp",
                  "src/benchmark/experiment/TransactionalRecoveryTrace.cpp")}}
    for case in CASES:
        item = previous[case]
        witness = item["witnesses"][0]
        table = output / (case + ".txt")
        table.write_text(f'{witness["frame"]} {witness["returnFrame"]} {witness["ordinal"]} '
                         f'{witness["u"]:.17g} {witness["v"]:.17g}\n', encoding="utf-8")
        resolved = Path(item["resolved"]["path"])
        if identity(resolved)["sha256"] != item["resolved"]["sha256"]:
            raise RuntimeError("旧冻结输入已变化：" + case)
        frozen["cases"][case] = {"resolved": identity(resolved), "witnessTable": identity(table), "witness": witness}
    write(output / "freeze.json", frozen)
    # 正常路径前后对照使用同一环境，不能拿诊断观察费用冒充算法时间。
    resolved = Path(previous[CASES[0]]["resolved"]["path"])
    for name, binary in (("normal-before", output / "experiment-cpu-before.exe"), ("normal-after", probe)):
        result = native_run(output, name, binary, "peking", "transactional", 8, "audit",
                            arguments=["--experiment-cpu", win(resolved), win(output / name)])
        if result["status"] != "ok":
            raise RuntimeError("正常路径快验失败：" + name)
    for case, item in frozen["cases"].items():
        result = native_run(output, case, probe, case, "transactional", 8, "audit",
                            arguments=["--recovery-trace", win(Path(item["resolved"]["path"])),
                                       win(output / (case + ".txt")), win(output / case), "--boundary-audit"])
        if result["status"] != "ok":
            raise RuntimeError("边界调查失败：" + case)


def figure(output, cases):
    from experiment_infrastructure.asset_preview import configure_font, plt
    configure_font()
    fig, axes = plt.subplots(1, len(cases), figsize=(14, 4.8), layout="constrained")
    for ax, (case, entry) in zip(axes, cases.items()):
        row = next((value for value in entry["variants"] if value["variant"] == "source"), None)
        if row is None:
            ax.text(.5, .5, "没有完成的源高候选，结果未知", ha="center", va="center")
            ax.set_title(case)
            continue
        points = {p[0]: p[1:] for p in row["target"]["points"]}
        for face in row["target"]["faces"]:
            uv = [points[v][:2] for v in face]
            ax.fill(*zip(*uv), color="#d9e8eb", alpha=.7)
            ax.plot(*zip(*(uv + uv[:1])), color="#425e6e")
        for vertex, (u, v, height) in points.items():
            is_new = vertex == row["target"]["newVertex"]
            ax.scatter(u, v, color="#a94325" if is_new else "#425e6e", s=25)
            ax.annotate(("新点" if is_new else str(vertex)) + f"\nh={height:.4f}", (u, v),
                        xytext=(3, 5), textcoords="offset points", fontsize=8)
        ax.scatter(*row["witness"], marker="x", color="#a52c39", s=50, label="原最大见证")
        if "excessWitness" in row:
            ax.scatter(*row["excessWitness"]["uv"], marker="D", color="#b27b16", s=25, label="最大新增退化")
        label = "Peking 50k" if "peking" in case and "50000-" in case else "Peking 200k" if "peking" in case else "峡谷 50k"
        minimum = min(angle["degrees"] for face in row["geometryAudit"]["exactAngles"] for angle in face["angles"])
        status = "原认证接受" if row["reason"] == "certified" else "原认证拒绝"
        ax.set(title=f"{label}｜{status}\n最小角 {minimum:.2f}°", xlabel="U", ylabel="V", aspect="equal")
        ax.margins(.25)
        values = [point[1] for point in points.values()]
        ax.set_ylim(min(values) - .012, max(values) + .020)
        ax.grid(alpha=.15)
        ax.legend(loc="lower left", fontsize=8)
    fig.suptitle("QPC-04A｜只改变一条外边界的有限中点候选", fontsize=14)
    fig.supxlabel("固定旧点，新增高度来自双线性源；此图是私有提案，未发布到持续生产状态", fontsize=10)
    fig.savefig(output / "boundary-midpoints.png", dpi=160)
    plt.close(fig)


def report(output, make_figure=False):
    """独立检查允许的边界分段，并保留局部退化与前缀限制。"""
    started = time.monotonic()
    self_test()
    freeze = read(output / "freeze.json")
    baseline = read(output / "baseline.json")
    for path, expected in baseline["production"].items():
        if identity(ROOT / path)["sha256"] != expected["sha256"]:
            raise RuntimeError("生产算法在诊断中发生了修改：" + path)
    cases = {}
    for case, item in freeze["cases"].items():
        current = output / case
        # 原逐帧文件不含计时；逐字节相同覆盖完整原批次、面数和样本工作量。
        for name in ("frames.csv", "boundary.csv"):
            if (current / name).read_bytes() != (PRIOR / case / name).read_bytes():
                raise RuntimeError("有限调查改变了原轨迹：" + case + "/" + name)
        frame = item["witness"]["frame"]
        records = [json.loads(line) for line in (current / f"boundary-audit-{frame}.jsonl").read_text().splitlines()]
        variants = []
        unknown = []
        summary = None
        for row in records:
            if row.get("status") == "summary":
                summary = row
            elif row.get("status") == "unknown":
                unknown.append(row)
            else:
                audit = validate(row["old"], row["target"], row["edge"])
                if not audit["structuralValid"]:
                    raise RuntimeError("边界分段结构错误：" + str(audit))
                if audit["shapeValid"] == (row["reason"] == "shape_infeasible"):
                    raise RuntimeError("独立形状检查与原生判据不同")
                if audit["faceDelta"] != 1:
                    raise RuntimeError("单侧面数变化不等于一")
                if row["variant"] == "linear" and (abs(row["newError"] - row["oldError"]) > 1e-8 or row["reason"] == "certified"):
                    raise RuntimeError("原曲面对照异常获得进展")
                row["geometryAudit"] = audit
                variants.append(row)
        cases[case] = {"variants": variants, "unknown": unknown, "summary": summary,
                       "trajectoryUnchanged": True, "witness": item["witness"]}
    normal = {}
    normal_names = ["normal-before", "normal-after"]
    if (output / "normal-before-repeat").exists() and (output / "normal-after-repeat").exists():
        normal_names += ["normal-after-repeat", "normal-before-repeat"]
    for name in normal_names:
        with (output / name / "frames.csv").open(encoding="utf-8-sig", newline="") as stream:
            normal[name] = list(csv.DictReader(stream))
    stable_fields = ("frame", "poseHash", "projectionHash", "hash", "faces", "budget", "workers", "raw", "examined",
                     "receivers", "need", "feasible", "exchanges", "free", "pairs", "conflicts", "donorReuse",
                     "touches", "evaluations", "vertexWrites", "indexWrites", "status", "updated", "cold")
    performance = {}
    for name, rows in normal.items():
        if len(normal["normal-before"]) != len(rows):
            raise RuntimeError("正常路径机会数改变")
        for before, after in zip(normal["normal-before"], rows):
            if any(before[key] != after[key] for key in stable_fields):
                raise RuntimeError("正常输出或工作量不同：" + name)
        warm = [row for row in rows if row["cold"] == "0" and row["warmup"] == "0"]
        performance[name] = {"warmFrames": len(warm), "milliseconds": {key: statistics_for(warm, key) for key in (
            "cpuMs", "viewMs", "receiverMs", "donorMs", "reservationMs", "topologyPrepareMs", "topologyPublishMs",
            "sampleRepairMs", "meshPrepareMs", "continuationMs", "adapterMs")}}
    result = {"protocol": freeze["protocol"], "cases": cases, "productionUnchanged": True,
              "normal": normal, "performance": performance,
              "commands": {name: read(output / "commands" / (name + ".json")) for name in (*normal_names, *CASES)},
              "reportSeconds": time.monotonic() - started}
    write(output / "analysis.json", result)
    if make_figure:
        figure(output, cases)
    print(json.dumps({case: [{key: row.get(key) for key in ("variant", "reason", "rank", "oldMax", "newMaxUpper",
        "oldError", "newError", "sampledExcessMax", "budgetStatus", "pairChecks", "seconds")} for row in entry["variants"]]
        for case, entry in cases.items()}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("collect", "report"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--figure", action="store_true")
    args = parser.parse_args()
    if args.mode == "collect":
        if args.probe is None:
            parser.error("collect requires --probe")
        collect(args.output.resolve(), args.probe.resolve())
    else:
        report(args.output.resolve(), args.figure)
