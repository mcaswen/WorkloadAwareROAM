"""FER-02预注册配置生成；不执行算法，也不读取新实验结果。"""
import random
from .catalog import ROOT, resolve_case, content_hash, load_json
from .runner import save


def prepare():
    previous = ROOT / "configs/experiments/formal/fer_01"
    target = ROOT / "configs/experiments/formal/fer_02"
    target.mkdir(parents=True, exist_ok=False)
    cases = []
    for entry in load_json(previous / "protocol.json")["cases"]:
        case = load_json(ROOT / entry["path"])
        case["backend"] = "d3d12"
        cases.append(case)
    for terrain in ("peking547", "generated-ridge", "dem-canyon", "dem-sierra"):
        base = next(case for case in cases if case["terrain"] == terrain
                    and case["algorithm"] == "dod" and case["budget"] == 50000)
        for area in (16, 8, 4):
            cases.append({
                **base, "id": f"{terrain}-cbt-a{area}", "algorithm": "cbt",
                "workers": 1, "heightPolicy": "fit", "flipRecovery": False,
                "cbtCapacity": 524288, "cbtArea": area,
                "cbtValidation": "off", "cbtGeometry": "modified",
            })
    entries = []
    for case in cases:
        path = target / (case["id"] + ".json")
        save(path, case)
        resolved = resolve_case(path)
        entries.append({
            "id": case["id"], "path": str(path.relative_to(ROOT)), "sha256": content_hash(path),
            "terrain": case["terrain"], "budget": case["budget"],
            "algorithm": case["algorithm"], "workers": case["workers"],
            "visual": not (case["algorithm"] == "transactional" and case["workers"] == 1),
            "qualityFrames": [2, 15, 16, 23] if case["maxFrames"] == 24 else [2, 48, 80, 95],
            "cameraSha256": resolved["cameraSha256"], "sampleSha256": resolved["sampleSha256"],
        })
    generator = random.Random(20260916)
    schedule = []
    for block in (1, 2, 3):
        order = entries.copy()
        generator.shuffle(order)
        schedule.extend(dict(block=block, id=entry["id"], runId=f"r{block}-" + entry["id"])
                        for entry in order)
    pairs = []
    for entry in entries:
        if not entry["visual"] or entry["algorithm"] not in ("classic", "transactional"):
            continue
        pairs.append(dict(candidate=entry["id"],
                          reference=f"{entry['terrain']}-b{entry['budget']}-dod-t8"))
        if entry["algorithm"] == "transactional":
            pairs.extend(dict(candidate=entry["id"], reference=f"{entry['terrain']}-cbt-a{area}",
                              differentBudget=True) for area in (16, 8, 4))
    protocol = dict(
        schemaVersion="fer-02-v1", seed=20260916, repetitions=3, backend="d3d12",
        cases=entries, schedule=schedule, pairs=pairs,
        statisticalUnit="independent process; frames correlated",
        noResultDependentChanges=True, qualityBudgetsPx=[0, .1, .25, .5, 1],
        limits=dict(secondsPerProcess=180, gibPerProcess=8, secondsPerQualityFrame=180),
        mainMetric="mean per-process CPU update; GPU host recording and device timestamps separate",
        uncertainty="all 3 process values and min/max; no CI or significance claim",
        report=dict(witnessMode="worst-registered", timelineRepeat=1,
                    nearestN="minimum absolute N difference, then case ID; never skip invalid quality"),
        qualityCostAssociation="GPU configuration only, not cross-process mesh equivalence",
    )
    assert len(entries) == 32 and len(schedule) == 96
    assert sum(entry["visual"] for entry in entries) == 30
    save(target / "protocol.json", protocol)
    return target / "protocol.json"


if __name__ == "__main__":
    print(prepare())
