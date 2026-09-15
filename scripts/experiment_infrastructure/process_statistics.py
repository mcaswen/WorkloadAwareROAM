"""从统一analysis归约独立进程和质量观察，不读取新的实验结果。"""
import math
import statistics
from .analysis import STAGES, PASS, COUNTS
from .runner import digest
from .quality import valid_quality


GROUPS = ("warm", "moving", "return-recovery", "stationary", "cold", "warmup", "all")
METRICS = ("cpuMs", "frameMs", "uploadMs", "uploadBytes", "faces", "utilization",
           "unattributedCpuMs", *STAGES, *PASS, *COUNTS)


def distribution(values):
    finite = [value for value in values if value is not None]
    return {
        "count": len(finite), "values": finite,
        "mean": statistics.mean(finite) if finite else None,
        "minimum": min(finite) if finite else None,
        "maximum": max(finite) if finite else None,
    }


def configuration_key(run):
    return digest({key: run.get(key) for key in
        ("taskId", "sourceId", "cpuOnly") } | {
        "binary": run["binary"]["sha256"], "workers": run["case"]["workers"],
    })


def gpu_metric(run, group, kind, column):
    value = run.get("gpu", {}).get("groups", {}).get(group, {}).get(kind, {}).get(column, {})
    return value.get("mean")


def aggregate(data):
    configurations = {}
    processes = []
    failures = []
    for run in data["runs"]:
        key = configuration_key(run)
        config = configurations.setdefault(key, {
            "key": key, "case": run["case"], "device": run["executionDevice"],
            "cpuTimeMeaning": run["cpuTimeMeaning"], "runs": [], "quality": [],
            "groups": {}, "visualRuns": [],
        })
        if run["status"] != "ok":
            failures.append({"run": run["id"], "mode": run["mode"], "status": run["status"]})
            continue
        if run["mode"] == "visual":
            config["visualRuns"].append(run["id"])
        if run["mode"] != "timing":
            continue
        config["runs"].append(run)
        warm = [frame for frame in run["frames"] if not frame["warmup"]]
        cpu_values = sorted(frame["cpuMs"] for frame in warm)
        faces = [frame["faces"] for frame in run["frames"] if frame["faces"] is not None]
        processes.append({
            "run": run["id"], "configuration": key, "case": run["case"]["id"],
            "algorithm": run["case"]["algorithm"], "cpuTimeMeaning": run["cpuTimeMeaning"],
            "warmCpuMs": run["groups"].get("warm", {}).get("cpuMs"),
            "warmFrameMs": run["groups"].get("warm", {}).get("frameMs"),
            "warmP95CpuMs": cpu_values[max(0, math.ceil(.95 * len(cpu_values)) - 1)] if cpu_values else None,
            "warmMaxCpuMs": max(cpu_values) if cpu_values else None,
            "gpuComputeMs": gpu_metric(run, "warm", "compute", "computeMs"),
            "gpuDrawMs": gpu_metric(run, "warm", "draw", "drawMs"),
            "initialCapturedN": run["frames"][0]["faces"] if run["frames"] else None,
            "finalCapturedN": run["frames"][-1]["faces"] if run["frames"] else None,
            "minimumCapturedN": min(faces) if faces else None,
            "maximumCapturedN": max(faces) if faces else None,
            **run.get("totals", {}),
        })

    by_path = {run["path"]: run for run in data["runs"]}
    quality_rows = []
    for index in data["quality"]:
        source = by_path.get(index["run"])
        if source is None:
            continue
        key = configuration_key(source)
        case = source["case"]
        for frame in index["frames"]:
            result = frame.get("result", {})
            valid = valid_quality(frame)
            observation = {
                "configuration": key, "case": case["id"], "run": source["id"],
                "terrain": case["terrain"], "algorithm": case["algorithm"],
                "budget": case["budget"] if case["algorithm"] != "cbt" else None,
                "area": case.get("cbtArea"), "capacity": case.get("cbtCapacity"),
                "frame": frame["frame"], "status": frame["status"], "valid": valid,
                "N": frame.get("faces"), "Emax": result.get("screenMax") if valid else None,
                "RMS": result.get("terrainSampleRms") if valid else None,
                "Hmax": result.get("heightMax") if valid else None,
                "Q": result.get("q"), "missing": result.get("missing"),
                "ambiguous": result.get("ambiguous"), "witness": result.get("screenWitness"),
                "meshHash": frame.get("meshHash"), "poseHash": frame.get("poseHash"),
                "sampleHash": frame.get("sampleHash"), "qualityPath": index["path"],
            }
            quality_rows.append(observation)
            configurations[key]["quality"].append(observation)

    for config in configurations.values():
        runs = config["runs"]
        for group in GROUPS:
            selected = [run for run in runs if group in run["groups"]]
            values = {metric: distribution([run["groups"][group].get(metric) for run in selected])
                      for metric in METRICS}
            for name, kind, column in (("gpuComputeMs", "compute", "computeMs"), ("gpuDrawMs", "draw", "drawMs")):
                values[name] = distribution([gpu_metric(run, group, kind, column) for run in selected])
            values["processCount"] = len(selected)
            values["opportunities"] = [run["groups"][group]["count"] for run in selected]
            values["gpuCoverage"] = [run["gpu"]["groups"][group] for run in selected if "gpu" in run]
            config["groups"][group] = values
        config["processCount"] = len(runs)
        # 固定输入顺序的第一进程供时序显示；完整重复始终保存在processes
        config["representativeRun"] = runs[0]["id"] if runs else None
        config["runIds"] = [run["id"] for run in runs]
        del config["runs"]

    matches = []
    for row in quality_rows:
        if row["algorithm"] == "cbt" or row["N"] is None:
            continue
        candidates = [other for other in quality_rows if other["algorithm"] == "cbt"
                      and other["terrain"] == row["terrain"] and other["frame"] == row["frame"]
                      and other["poseHash"] == row["poseHash"] and other["N"]]
        if not candidates:
            continue
        nearest = min(candidates, key=lambda other: (abs(other["N"] - row["N"]), other["case"]))
        difference = abs(row["N"] / nearest["N"] - 1)
        matches.append(dict(case=row["case"], frame=row["frame"], N=row["N"],
                            reference=nearest["case"], referenceN=nearest["N"],
                            relativeCountDifference=difference, withinTenPercent=difference <= .1,
                            status="ok" if row["valid"] and nearest["valid"] else "invalid-quality",
                            Emax=row["Emax"], referenceEmax=nearest["Emax"]))

    return {
        "numberMatches": matches,
        "schemaVersion": "eip-process-statistics-v1", "configurations": list(configurations.values()),
        "processes": processes, "quality": quality_rows, "failures": failures,
        "statisticalUnit": "independent timing process; visual/quality excluded from timing distribution",
        "uncertainty": "observed process range, not confidence interval",
        "gpuQualityCostAssociation": "configuration only; no unobserved cross-process mesh equivalence assumed",
    }
