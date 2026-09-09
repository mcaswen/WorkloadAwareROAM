"""按独立运行和冻结工作量层组织探索结果，不拟合正式阈值或决定研究继续。"""

from collections import Counter, defaultdict
from datetime import datetime
import hashlib
from itertools import combinations
import json
import statistics

from formal_experiments.input_validation import ACTIONS
from formal_experiments.paired_statistics import run_drift, summarize_pair

MEDIUM_SCENES = {"test129-a-b4096", "peking547-a-b80000"}


def stable_environment(metadata, field="environment"):
    return {key: value for key, value in metadata.get(field, {}).items() if key != "parentPid"}


def execution_identity(attempt):
    metadata = attempt.get("metadata", {})
    return (attempt["summary"]["backend"], metadata.get("source", {}).get("sha256"),
            (tuple((item["sha256"], item["bytes"]) for item in metadata.get("buildFiles", [])),
             tuple(sorted((item["sha256"], item["bytes"]) for item in metadata.get("frozenInputsAndAssets", []))),
             json.dumps(stable_environment(metadata), sort_keys=True),
             json.dumps(stable_environment(metadata, "environmentAfter") if "environmentAfter" in metadata else stable_environment(metadata), sort_keys=True)))


def comparison_group(attempt, row):
    """相同执行来源和测量配置才能核对跨运行方向；选择子集不改变组身份。"""
    identity = (execution_identity(attempt), row["warmupCount"], row["measuredRepeatCount"], row["parallelWorkerCount"])
    return hashlib.sha256(json.dumps(identity, sort_keys=True).encode("utf-8")).hexdigest()


def target_costs(attempt):
    """汇总已完整审计的目标成本，重建时间保留为到达该边界的累计值。"""
    costs = []
    for summary in attempt["targetSummaries"]:
        if summary["status"] != "valid":
            continue
        key = (summary["scenarioId"], summary["passId"], int(summary["sampleIndex"]))
        rows = attempt["groups"][key]
        item = {"runId": attempt["runId"], "scenarioId": key[0], "passId": key[1], "sampleIndex": key[2],
                "strategyRowCount": len(rows), "warmupRowCount": sum(row["isWarmup"] for row in rows),
                "rebuildToTargetMs": float(summary["rebuildMs"]),
                "targetWorkerPreparationMs": float(summary["workerPreparationMs"]),
                "inputFeatureProbeMs": rows[0]["featureCollectionMs"]}
        # 输入匹配探测只做一次，复制到每行的该值不能按策略行数累加
        for field in ("wallMs", "stateCloneMs", "inputCheckMs", "validationMs", "workerPreparationMs"):
            values = [row[field] for row in rows]
            item[field + "Sum"] = sum(values)
            item[field + "Median"] = statistics.median(values)
        costs.append(item)
    return costs


def short_run_stability(attempts):
    """三次短运行构成一轮，第二轮必须整轮保留；不足三次时不伪造稳定性证据。"""
    grouped = defaultdict(list)
    for attempt in attempts:
        if attempt.get("failed"):
            continue
        for key, rows in attempt["groups"].items():
            first = rows[0]
            if key[0] not in MEDIUM_SCENES or first["selectionRank"] != 0 or \
                    (first["warmupCount"], first["measuredRepeatCount"], first["parallelWorkerCount"]) != (2, 10, 8):
                continue
            for action in ACTIONS[key[1]]:
                measured = [row["wallMs"] for row in rows if not row["isWarmup"] and row["requestedAction"] == action]
                if len(measured) != 10:
                    raise ValueError("短运行目标缺少完整重复")
                group_key = (*execution_identity(attempt), *key, action)
                grouped[group_key].append((attempt.get("metadata", {}).get("processStartedUtc", ""),
                                           attempt["runId"], statistics.median(measured)))
    records = []
    blocked = set()
    rejected_runs = set()
    for key, runs in grouped.items():
        runs.sort()
        if len(runs) > 6:
            raise ValueError("短运行漂移只允许预定两轮，不能无限追加直到稳定")
        rounds = []
        if len(runs) not in (3, 6):
            status = "incomplete"
        else:
            for offset in range(0, len(runs), 3):
                current = runs[offset:offset + 3]
                rounds.append({**run_drift([run[2] for run in current]), "runIds": [run[1] for run in current]})
                if not rounds[-1]["stable"]:
                    rejected_runs.update(run[1] for run in current)
            status = "stable" if rounds[-1]["stable"] else ("repeat_required" if len(rounds) == 1 else "unstable")
            if status != "stable":
                blocked.add(key[:3])
        records.append({"backend": key[0], "sourceSha256": key[1], "scenarioId": key[3], "passId": key[4],
                        "sampleIndex": key[5], "action": key[6], "status": status, "rounds": rounds})
    return records, blocked, rejected_runs


def analyze_attempts(attempts):
    ids = set()
    processes = set()
    complete = []
    failures = []
    for attempt in attempts:
        if attempt.get("failed"):
            failures.append({"directory": attempt["directory"], "failure": attempt["failure"], "runId": attempt.get("runId")})
            continue
        if attempt["runId"] in ids:
            raise ValueError("重复运行身份；复制尝试目录不能成为新的独立运行")
        ids.add(attempt["runId"])
        metadata = attempt.get("metadata", {})
        process = (metadata.get("processPid"), metadata.get("processStartedUtc"))
        if process[0] is not None and process in processes:
            raise ValueError("多个尝试声明相同实际进程")
        processes.add(process)
        complete.append(attempt)
    stability, blocked, rejected_runs = short_run_stability(complete)
    results = []
    coverage = []
    runs = []
    costs = []
    for attempt in complete:
        run_id = attempt["runId"]
        backend = attempt["summary"]["backend"]
        calibration = attempt["calibration"]
        metadata = attempt.get("metadata", {})
        elapsed = ((datetime.fromisoformat(metadata["finishedUtc"]) - datetime.fromisoformat(metadata["startedUtc"])).total_seconds()
                   if "finishedUtc" in metadata and "startedUtc" in metadata else None)
        costs.extend(target_costs(attempt))
        environment_blocked = execution_identity(attempt) in blocked or run_id in rejected_runs
        environment_changed = "environmentAfter" in metadata and stable_environment(metadata) != stable_environment(metadata, "environmentAfter")
        runs.append({"runId": run_id, "backend": backend, "directory": attempt["directory"],
                     "targetCount": len(attempt["groups"]), "summary": attempt["summary"],
                     "calibration": calibration, "shortRunBlocked": environment_blocked,
                     "sourceSha256": metadata.get("source", {}).get("sha256"), "buildFiles": metadata.get("buildFiles", []),
                     "cppTotalMs": float(attempt["summary"]["totalMs"]) if "totalMs" in attempt["summary"] else None,
                     "processWallSeconds": metadata.get("processWallSeconds"), "attemptElapsedSeconds": elapsed,
                     "environmentChanged": environment_changed})
        for summary in attempt["targetSummaries"]:
            if summary["status"] == "targets_unavailable":
                coverage.append({"runId": run_id, "backend": backend, **summary})
        for key, all_rows in sorted(attempt["groups"].items()):
            first = all_rows[0]
            blocks = defaultdict(dict)
            for row in all_rows:
                if not row["isWarmup"]:
                    blocks[row["absoluteBlockIndex"]][row["requestedAction"]] = row
            ordered = [blocks[block] for block in sorted(blocks)]
            for action_a, action_b in combinations(ACTIONS[key[1]], 2):
                a = [block[action_a] for block in ordered]
                b = [block[action_b] for block in ordered]
                measured = summarize_pair([row["wallMs"] for row in a], [row["wallMs"] for row in b], calibration["noiseMs"])
                reasons = Counter()
                fallback_blocks = 0
                for row_a, row_b in zip(a, b):
                    invalid = False
                    for row in (row_a, row_b):
                        if row["fallbackReason"] != "none":
                            reasons[row["fallbackDetail"] or row["fallbackReason"]] += 1
                            invalid = True
                    fallback_blocks += invalid
                if not calibration["environmentValid"]:
                    reasons["empty_timer_environment_invalid"] += 1
                if environment_blocked:
                    reasons["short_run_drift_unstable"] += 1
                if environment_changed:
                    reasons["execution_environment_changed"] += 1
                # 任一块发生回退就排除整个目标策略对，其他完整串行网格比较仍可保留
                if reasons:
                    measured.update(winner="excluded", interpretation="排除胜负解释，保留原始量级")
                results.append({"runId": run_id, "backend": backend, "scenarioId": key[0], "passId": key[1],
                                "comparisonGroup": comparison_group(attempt, first),
                                "warmupCount": first["warmupCount"], "measuredRepeatCount": first["measuredRepeatCount"],
                                "parallelWorkerCount": first["parallelWorkerCount"],
                                "sampleIndex": key[2], "selectionRank": first["selectionRank"],
                                "selectionStratum": first["selectionStratum"], "primaryWorkValue": first["primaryWorkValue"],
                                "replayInputHash": first["replayInputHash"], "resultHash": first["resultHash"],
                                "actionA": action_a, "actionB": action_b,
                                "warmupRowCount": sum(row["isWarmup"] for row in all_rows),
                                "fallbackBlockCount": fallback_blocks,
                                "requestedWorkersA": a[0]["requestedWorkerCount"], "requestedWorkersB": b[0]["requestedWorkerCount"],
                                "actualWorkersMinA": min(row["actualWorkerCount"] for row in a),
                                "actualWorkersMaxA": max(row["actualWorkerCount"] for row in a),
                                "actualWorkersMinB": min(row["actualWorkerCount"] for row in b),
                                "actualWorkersMaxB": max(row["actualWorkerCount"] for row in b),
                                "exclusionReasons": dict(reasons), **measured})
    strata = []
    signals = []
    grouped_strata = defaultdict(list)
    grouped_signal = defaultdict(list)
    across_runs = defaultdict(list)
    for row in results:
        identity = (row["runId"], row["backend"], row["scenarioId"], row["passId"], row["actionA"], row["actionB"])
        grouped_strata[(*identity, row["selectionStratum"])].append(row)
        grouped_signal[identity].append(row)
        across_runs[(row["comparisonGroup"], row["backend"], row["scenarioId"], row["passId"], row["sampleIndex"], row["actionA"], row["actionB"])].append(row)
    for key, rows in sorted(grouped_strata.items()):
        counts = Counter(row["winner"] for row in rows)
        strata.append(dict(zip(("runId", "backend", "scenarioId", "passId", "actionA", "actionB", "stratum"), key),
                           targetCount=len(rows), validTargetCount=sum(row["winner"] != "excluded" for row in rows),
                           minimumWork=min(row["primaryWorkValue"] for row in rows), maximumWork=max(row["primaryWorkValue"] for row in rows),
                           winsA=counts["A"], winsB=counts["B"], ties=counts["tie"], excluded=counts["excluded"]))
    for key, rows in sorted(grouped_signal.items()):
        counts = Counter(row["winner"] for row in rows)
        signal = "观察到双向描述性信号" if counts["A"] and counts["B"] else (
            "仅观察到 A 方向优势" if counts["A"] else ("仅观察到 B 方向优势" if counts["B"] else "未观察到可解释的双向信号"))
        signals.append(dict(zip(("runId", "backend", "scenarioId", "passId", "actionA", "actionB"), key),
                            signal=signal, validTargetCount=sum(row["winner"] != "excluded" for row in rows),
                            winsA=counts["A"], winsB=counts["B"], ties=counts["tie"], excluded=counts["excluded"]))
    consistency = []
    for key, rows in sorted(across_runs.items()):
        if len({(row["replayInputHash"], row["resultHash"]) for row in rows}) != 1:
            raise ValueError("跨运行的相同冻结目标输入或规范化结果不同")
        directions = {row["winner"] for row in rows if row["winner"] in ("A", "B")}
        consistency.append(dict(zip(("comparisonGroup", "backend", "scenarioId", "passId", "sampleIndex", "actionA", "actionB"), key),
                                runCount=len(rows), runIds=[row["runId"] for row in rows],
                                winners=[row["winner"] for row in rows],
                                directionAgreement=len(directions) == 1 if len(rows) > 1 and directions else None))
    return {"dataPurpose": "exploratory", "noiseRuleVersion": 1, "runs": runs, "failedAttempts": failures,
            "pairs": results, "strata": strata, "signals": signals, "consistency": consistency,
            "unavailableGroups": coverage, "shortRunStability": stability, "targetCosts": costs}
