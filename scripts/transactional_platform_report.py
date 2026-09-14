"""只归约既有平台证据；不挑帧、不发起重测，也不把不同质量的时间差称为加速。"""

import argparse
from array import array
import csv
import json
import math
from pathlib import Path
import statistics
import sys

SEMANTICS = ("frame", "sample", "ok", "faces", "sequence", "hash", "seedFaces", "raw", "examined",
             "receivers", "need", "feasible", "exchanges", "free", "pairs", "conflicts", "donorReuse",
             "touches", "evaluations", "vertexWrites", "indexWrites")
STAGES = ("viewMs", "receiverMs", "donorMs", "reservationMs", "topologyPrepareMs", "topologyPublishMs",
          "sampleRepairMs", "meshPrepareMs", "continuationMs", "adapterMs")
METRICS = ("cpuMs", "uploadMs", "frameMs", "beginMs", "waitMs", "renderMs", "presentMs", "gpuDelayedMs",
           "uploadBytes", "faces", "pairs", "touches", "evaluations", *STAGES)


def rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def compare(a, b, fields, label):
    if len(a) != len(b):
        raise RuntimeError(f"Frame count differs: {label}")
    for i, (left, right) in enumerate(zip(a, b)):
        for field in fields:
            if left[field] != right[field]:
                raise RuntimeError(f"Identity/decision mismatch: {label}/{i}/{field}")


def analyze(root):
    result = {"identityChecks": [], "runs": {}, "quality": {}, "qualityPairs": {}, "sensitivity": {}}
    for path in sorted(root.glob("*-normal/frames.csv")):
        data = rows(path)
        name = path.parent.name
        if not data:
            result["runs"][name] = {"status": "no-samples"}
            continue
        groups = {"cold": data[:1], "warm": data[1:],
                  "stationary": [r for r in data if int(r["frame"]) in (1, 2) or int(r["frame"]) >= 17],
                  "moving-return": [r for r in data if 3 <= int(r["frame"]) <= 16]}
        entry = {"frames": len(data), "allUpdatesOk": all(r["ok"] == "1" for r in data), "groups": {},
                 "totalExchanges": sum(int(r["exchanges"]) for r in data),
                 "totalFree": sum(int(r["free"]) for r in data),
                 "transactionFrames": sum(int(r["exchanges"])+int(r["free"])>0 for r in data),
                 "lastFaces": int(data[-1]["faces"])}
        for group, subset in groups.items():
            if subset:
                entry["groups"][group] = {"count": len(subset), **{
                    metric: statistics.mean(float(r[metric]) for r in subset) for metric in METRICS}}
                if name.startswith("opengl-"):
                    # 当前 GL 后端以零占位未实现的 GPU 查询，不报告成 GPU 免费
                    entry["groups"][group]["gpuDelayedMs"] = None
                    entry["groups"][group]["waitMs"] = None
        result["runs"][name] = entry
        if "transactional8" in name:
            single = root / name.replace("transactional8", "transactional1") / "frames.csv"
            if single.exists():
                compare(rows(single), data, SEMANTICS, name+" B/C")
                result["identityChecks"].append(name+" B/C all frames")
            mean = entry["groups"]["warm"]
            result["sensitivity"][name] = {stage: {"share": mean[stage]/mean["cpuMs"],
                "halfMs": mean["cpuMs"]-.5*mean[stage], "freeMs": mean["cpuMs"]-mean[stage]}
                for stage in STAGES}
        exported = root / name.replace("-normal", "-export") / "frames.csv"
        if exported.exists():
            compare(data, rows(exported), SEMANTICS, name+" normal/export")
            result["identityChecks"].append(name+" normal/export all frames")
    for path in sorted(root.glob("*-export/quality-*/quality.json")):
        name = path.parent.parent.name
        frame = int(path.parent.name.split("-")[1])
        q = json.loads(path.read_text())
        source = rows(path.parent.parent / "frames.csv")[frame]
        if q["meshHash"] != source["hash"]:
            raise RuntimeError(f"Quality mesh differs: {path}")
        result["quality"].setdefault(name, {})[str(frame)] = q
    for name, qualities in result["quality"].items():
        if "transactional8" not in name:
            continue
        baseline = name.replace("transactional8", "dod8")
        for frame, q in qualities.items():
            other = result["quality"].get(baseline, {}).get(frame)
            if not other or q["status"] != "sampled_only" or other["status"] != "sampled_only":
                continue
            if (q["sampleHash"], q["q"], q["visible"]) != (other["sampleHash"], other["q"], other["visible"]):
                raise RuntimeError("Independent quality sample identity mismatch")
            a, b = array("d"), array("d")
            a.frombytes((root/name/f"quality-{frame}/errors.f64").read_bytes())
            b.frombytes((root/baseline/f"quality-{frame}/errors.f64").read_bytes())
            if sys.byteorder != "little":
                a.byteswap(); b.byteswap()
            if len(a) != q["q"] or len(b) != len(a):
                raise RuntimeError("Pointwise quality population invalid")
            maximum, difference = None, -math.inf
            for ordinal, (left, right) in enumerate(zip(a, b)):
                if not math.isfinite(left) or not math.isfinite(right) or (left >= 0) != (right >= 0):
                    raise RuntimeError("Pointwise quality population invalid")
                if left >= 0 and left-right > difference:
                    maximum, difference = ordinal, left-right
            result["qualityPairs"][f"{name}/{frame}"] = {"DmaxPx": difference if maximum is not None else None,
                "ordinal": maximum,
                "deltaEmaxPx": q["screenMax"]-other["screenMax"] if maximum is not None else None}
    (root/"analysis.json").write_text(json.dumps(result, ensure_ascii=False, indent=2)+"\n")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    report = analyze(args.root)
    for name, run in report["runs"].items():
        print(name, "frames", run.get("frames"), "exchanges", run.get("totalExchanges"),
              "free", run.get("totalFree"), "warmCPU", run.get("groups", {}).get("warm", {}).get("cpuMs"))
    print("identity checks", len(report["identityChecks"]), "quality pairs", len(report["qualityPairs"]))
