"""展开算法适用的有限清单；GPU容量不作为CPU硬预算维度。"""
from itertools import product
from pathlib import Path
from .catalog import load_json, resolve_case, content_hash
from .runner import save


def case_variants(original, budgets, workers, algorithms, prefixes, cbt_areas, cbt_capacities):
    dimensions = [budgets, workers, algorithms, prefixes, cbt_areas, cbt_capacities]
    if any(not values or len(values) != len(set(values)) for values in dimensions):
        raise ValueError("维度必须非空且没有重复")
    result = []
    for algorithm in algorithms:
        base = {key: value for key, value in original.items() if not key.startswith("cbt")}
        base.update(algorithm=algorithm, heightPolicy="fit", flipRecovery=False, boundaryRefinement=False,
                    receiverOrder="composite")
        for key in ("qualityPolicy", "qualityTargetPixels", "qualityHeightRatio"):
            base.pop(key, None)
        if algorithm == "cbt":
            for area, capacity in product(cbt_areas, cbt_capacities):
                result.append({**base,
                    "id": f"{original['id']}-cbt-a{area:g}-c{capacity}",
                    "workers": 1, "prefix": "scaled", "backend": "d3d12",
                    "cbtArea": area, "cbtCapacity": capacity,
                    "cbtValidation": "off", "cbtGeometry": "modified"})
            continue
        if algorithm not in ("classic", "dod", "transactional"):
            raise ValueError("未知算法: " + algorithm)
        selected_workers = [1] if algorithm == "classic" else workers
        selected_prefixes = prefixes if algorithm == "transactional" else ["scaled"]
        if algorithm == "transactional":
            base["heightPolicy"] = original["heightPolicy"]
            base["flipRecovery"] = original.get("flipRecovery", False)
            base["boundaryRefinement"] = original.get("boundaryRefinement", False)
            base["receiverOrder"] = original.get("receiverOrder", "composite")
            for key in ("qualityPolicy", "qualityTargetPixels", "qualityHeightRatio"):
                if key in original:
                    base[key] = original[key]
        for budget, worker, prefix in product(budgets, selected_workers, selected_prefixes):
            result.append({**base,
                "id": f"{original['id']}-b{budget}-{algorithm}-t{worker}-{prefix}",
                "budget": budget, "workers": worker, "prefix": prefix})
    if len(result) > 128:
        raise ValueError("一次最多生成128个适用配置，不自动扩大矩阵")
    return result


def expand(base, output, budgets, workers, algorithms, prefixes, cbt_areas=(16, 8, 4), cbt_capacities=(524288,)):
    base = Path(base).resolve()
    output = Path(output).resolve()
    resolve_case(base)
    variants = case_variants(load_json(base), budgets, workers, algorithms, prefixes, cbt_areas, cbt_capacities)
    output.mkdir(parents=True, exist_ok=False)
    entries = []
    for case in variants:
        path = output / (case["id"] + ".json")
        save(path, case)
        resolve_case(path)
        entries.append({"path": path.name, "sha256": content_hash(path)})
    result = {
        "schemaVersion": "eip-suite-v2", "base": str(base), "baseSha256": content_hash(base),
        "order": "algorithm, applicable dimensions; generation only, not randomized run order",
        "execution": "not started; explicit run commands and unique directories required",
        "dimensionPolicy": {
            "classic": "budgets; one thread; no duplicate prefixes",
            "dod": "budgets x workers; no duplicate prefixes",
            "transactional": "budgets x workers x prefixes; inherited height/flip policy",
            "cbt": "areas x capacities; D3D12; CPU budget is inherited comparison label only",
        },
        "cases": entries,
    }
    save(output / "suite.json", result)
    return output / "suite.json"
