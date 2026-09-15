"""GPU异步观测按来源代归属；不将延迟计数回填成当前网格。"""
import math
import statistics


STAGE_NAMES = (
    "classificationGeometry", "reset", "classify", "split", "allocate",
    "neighborCopy", "bisect", "propagateBisect", "prepareSimplify", "simplify",
    "propagateSimplify", "reducePre", "reduceFirst", "reduceSecond", "indexation",
    "renderGeometry", "validation",
)
WORK_COLUMNS = (
    "delayedFaces", "activeSlots", "remainingSlots", "committedSlots", "splitPropagation",
    "preparedSimplification", "releasedSlots", "simplifyPropagation", "pairMerge", "quadMerge",
    "template0", "template1", "template2", "template3",
)


def samples(rows, generation_column, columns):
    origins = {}
    for row in rows:
        key = (row["resourceGeneration"], row["topologyGeneration"])
        frame = int(row["frame"])
        if key in origins and origins[key] != frame:
            raise ValueError("同一GPU输出代映射到多个机会")
        origins[key] = frame

    unique = {}
    missing = 0
    unknown = 0
    duplicates = 0
    for row in rows:
        generation = row.get(generation_column, "0")
        if generation in ("", "0"):
            missing += 1
            continue
        key = (row["resourceGeneration"], generation)
        if key not in origins:
            unknown += 1
            continue
        if origins[key] > int(row["frame"]):
            raise ValueError("GPU观测引用未来输出代")
        values = {}
        for column in columns:
            value = row.get(column, "")
            values[column] = float(value) if value != "" else None
            if values[column] is not None and (not math.isfinite(values[column]) or values[column] < 0):
                raise ValueError("无效GPU观测值: " + column)
        if key in unique:
            if unique[key]["values"] != values:
                raise ValueError("相同GPU采样代返回矛盾数值")
            duplicates += 1
            continue
        unique[key] = {
            "frame": origins[key], "observedAt": int(row["frame"]),
            "resourceGeneration": key[0], "generation": generation, "values": values,
        }
    return {
        "samples": list(unique.values()), "duplicateObservations": duplicates,
        "missingObservations": missing, "unknownGenerationObservations": unknown,
        "unobservedSourceFrames": len(origins) - len(unique),
    }


def gpu_samples(rows, generation_column, value_column, warmup):
    """兼容旧有限报告；完整基础设施另保留缺测与来源代。"""
    result = samples(rows, generation_column, (value_column,))
    return [sample["values"][value_column] for sample in result["samples"]
            if sample["frame"] >= warmup and sample["values"][value_column] is not None]


def summarize(rows, groups):
    observations = {
        "compute": samples(rows, "timingGeneration", ("computeMs", *[f"stage{i}Ms" for i in range(17)])),
        "draw": samples(rows, "drawGeneration", ("drawMs",)),
        "classification": samples(rows, "classificationGeneration", WORK_COLUMNS),
    }
    summaries = {}
    for name, frames in groups.items():
        indices = {int(frame["frame"]) for frame in frames}
        result = {"sourceOpportunities": len(indices)}
        for kind, record in observations.items():
            selected = [sample for sample in record["samples"] if sample["frame"] in indices]
            columns = {column for sample in selected for column in sample["values"]}
            result[kind] = {"sampleCount": len(selected), "unobserved": len(indices) - len(selected)}
            for column in sorted(columns):
                values = [sample["values"][column] for sample in selected if sample["values"][column] is not None]
                result[kind][column] = {
                    "mean": statistics.mean(values) if values else None, "count": len(values),
                    "maximum": max(values) if values else None,
                }
        summaries[name] = result
    return {
        "observations": observations, "groups": summaries, "stageNames": list(STAGE_NAMES),
        "clockContract": "GPU compute sum and draw have independent generations; never stack with CPU recording",
        "countContract": "classification counts belong to their source frame; current faces remain capture-only",
    }
