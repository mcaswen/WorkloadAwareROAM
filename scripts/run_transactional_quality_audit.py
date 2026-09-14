"""有限持续质量调查：复用平台启动、输出身份和独立质量评价，不扩实验矩阵。"""

import argparse
import json
from pathlib import Path
import subprocess
import time

from run_transactional_platform import ROOT, identity, native_run, quality, win, write
from transactional_platform_report import SEMANTICS, analyze, compare, rows

TPI = ROOT / "benchmark-output/cpu-refinement/tpi-05/run-02"
CORE_FIELDS = ("frame", "sample", "hash", "faces", "raw", "examined", "receivers", "need",
               "feasible", "exchanges", "free", "pairs", "conflicts", "donorReuse")


def reuse_dod(output):
    # 只有导出证据复用旧文件，正常性能仍由当前进程单列记录
    link = output / "opengl-peking-dod8-export"
    source = TPI / link.name
    for frame in (0, 2, 15, 16, 23):
        if not (source / f"quality-{frame}/errors.f64").is_file():
            raise RuntimeError("Reused DOD evidence is incomplete; never write through the link")
    if not link.exists():
        link.symlink_to(source, target_is_directory=True)


def collect(output, app, probe, baseline):
    output.mkdir(parents=True, exist_ok=True)
    freeze = output / "quality-freeze.json"
    if not freeze.exists():
        names = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard",
                                         "-z", "src", "tests", "scripts", "cmake"], cwd=ROOT).decode().split("\0")
        write(freeze, {"protocol": "pq01-v1",
                      "manifestScope": "post-review source inventory; per-run measured binary identity remains in commands",
                      "app": identity(app), "probe": identity(probe),
                      "baseline": identity(baseline), "commit": subprocess.check_output(
                          ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                      "sourceFiles": {name: identity(ROOT / name) for name in sorted(set(names))
                                      if name and (ROOT / name).is_file()},
                      "sourceAsset": identity(ROOT / "assets/heightmaps/Hm_Terrain_Peking_513.png"),
                      "reusedDodQuality": str(TPI / "opengl-peking-dod8-export"),
                      "survivorPolicy": "new point fit unchanged; no HeightGuard; old heights immutable"})
    else:
        old = json.loads(freeze.read_text())
        if old["app"]["sha256"] != identity(app)["sha256"] or old["probe"]["sha256"] != identity(probe)["sha256"]:
            raise RuntimeError("Frozen executable changed; use another output")
    tasks = (
        ("before-normal", baseline, "transactional", "normal"),
        ("after-normal", app, "transactional", "normal"),
        ("opengl-peking-transactional8-normal", app, "transactional", "normal-immutable"),
        ("opengl-peking-transactional8-export", app, "transactional", "export-immutable"),
        ("opengl-peking-dod8-normal", app, "dod", "normal"),
    )
    for name, exe, algorithm, mode in tasks:
        if native_run(output, name, exe, "peking", algorithm, 8, mode)["status"] != "ok":
            raise RuntimeError("Native run failed: " + name)
    # 已完成的原轨迹诊断可复用；新运行从当前默认策略重现它
    for name, extra in (("a-provenance-support", []), ("b-provenance", ["immutable"])):
        if native_run(output, name, probe, "peking", "transactional", 8, "audit",
                      arguments=[win(output / name), *extra])["status"] != "ok":
            raise RuntimeError("Provenance run failed: " + name)
    reuse_dod(output)


def report(output):
    started = time.monotonic()
    before = rows(output / "before-normal/frames.csv")
    after = rows(output / "after-normal/frames.csv")
    old = rows(TPI / "opengl-peking-transactional8-normal/frames.csv")
    compare(before, old, SEMANTICS, "original platform reproduction")
    compare(before, after, SEMANTICS, "default behavior before/after")
    compare(rows(output / "a-provenance-support/frames.csv"), after, CORE_FIELDS, "A independent core/platform")
    frozen = rows(output / "opengl-peking-transactional8-normal/frames.csv")
    compare(rows(output / "b-provenance/frames.csv"), frozen, CORE_FIELDS, "B independent core/platform")
    reuse_dod(output)
    result = analyze(output)
    expected_frames = {"0", "2", "15", "16", "23"}
    for name in ("opengl-peking-transactional8-export", "opengl-peking-dod8-export"):
        if set(result["quality"].get(name, {})) != expected_frames:
            raise RuntimeError("All five independent quality frames are required: " + name)
    result["qualityPolicy"] = "preserve_surviving_heights"
    result["auditIdentityChecks"] = ["24-frame original reproduction", "24-frame default before/after",
                                    "24-frame A direct/platform", "24-frame B direct/platform"]
    result["provenance"] = {}
    for policy, name in (("A", "a-provenance-support"), ("B", "b-provenance")):
        transactions = [json.loads(line) for line in (output / name / "transactions.jsonl").read_text().splitlines()]
        changes = []
        for transaction in transactions:
            for witness in transaction["witnesses"]:
                if abs(witness["heightAfter"] - witness["heightBefore"]) > 1e-8:
                    changes.append({key: transaction[key] for key in ("frame", "exchange", "kind", "receiverRoot",
                                     "center", "sampleCount", "targetPx", "free", "freeVisibleSupport")} |
                                   {"witness": witness, "changedPoints": [p for p in transaction["points"]
                                    if p[3] is None or p[3] != p[4]]})
        result["provenance"][policy] = {
            "heightChanges": changes,
            "witnessStates": rows(output / name / "witnesses.csv"),
            "recovery": [json.loads(line) for line in (output / name / "recovery.jsonl").read_text().splitlines()],
        }
    result["baselineQuality"] = {str(frame): json.loads(
        (TPI / f"opengl-peking-transactional8-export/quality-{frame}/quality.json").read_text())
        for frame in (0, 2, 15, 16, 23)}
    result["reportSeconds"] = time.monotonic() - started
    write(output / "quality-audit.json", result)
    for name, run in result["runs"].items():
        print(name, "交换", run["totalExchanges"], "暖CPU毫秒", round(run["groups"]["warm"]["cpuMs"], 3))
    for frame, q in result["quality"]["opengl-peking-transactional8-export"].items():
        print("B帧", frame, "Emax", q["screenMax"], "Hmax", q["heightMax"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("collect", "quality", "report"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--app", type=Path)
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    required = ("app", "probe", "baseline") if args.mode == "collect" else (("probe",) if args.mode == "quality" else ())
    for field in required:
        if getattr(args, field) is None:
            parser.error(f"{args.mode} requires --{field}")
    out = args.output.resolve()
    if args.mode == "collect":
        collect(out, args.app.resolve(), args.probe.resolve(), args.baseline.resolve())
    elif args.mode == "quality":
        reuse_dod(out)
        quality(out, args.probe.resolve())
    else:
        report(out)
