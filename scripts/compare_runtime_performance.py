"""按显式版本与配置顺序启动性能进程，保存失败尝试和可离线复核的身份。"""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time
import uuid

from cpu_pilot_support import ROOT, basic_environment, file_identity, selected_assets, source_identity
from runtime_performance_analysis import DIAGNOSTICS, analyze_attempt, summarize_frames, write_json, write_report
from runtime_performance_environment import WindowsEnvironment


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def schedule(groups, warmup_blocks=1, measured_blocks=5):
    """各块反转配置顺序，块内使用 AB/BA 或 ABBA/BAAB；不按测量结果重排。"""
    for block in range(warmup_blocks + measured_blocks):
        for group in groups if block % 2 == 0 else reversed(groups):
            order = ("ABBA" if block % 2 == 0 else "BAAB") if group["layout"] == "abba" else ("AB" if block % 2 == 0 else "BA")
            for slot, label in enumerate(order):
                yield group, block, slot, label, block < warmup_blocks


def run_process(command, directory, environment, affinity, timeout=120):
    """拥有直接子进程及日志，超时或异常时先终止并回收，再恢复采集器亲和性。"""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=False)
    record = {"command": command, "cwd": str(ROOT), "status": "running", "startedUtc": utc_now(),
              "requestedAffinity": affinity}
    write_json(directory / "process.json", record)
    process = None
    started = time.perf_counter()
    try:
        with environment.scoped_affinity(affinity):
            before = environment.system_times()
            try:
                with (directory / "process.log").open("wb") as log:
                    process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
                    record["pid"] = process.pid
                    record["observedAffinity"] = environment.affinity(int(process._handle))[0]
                    if record["observedAffinity"] != affinity:
                        raise RuntimeError("子进程实际亲和性与请求不符")
                    record["exitCode"] = process.wait(timeout=timeout)
                    record["seconds"] = time.perf_counter() - started
                    record.update(environment.process_counters(int(process._handle)))
                    record["machineBusyPercent"] = environment.system_busy(before, environment.system_times())
                    if record["exitCode"]:
                        raise RuntimeError(f"进程退出码 {record['exitCode']}：{directory}")
            finally:
                if process is not None:
                    if process.poll() is None:
                        process.kill()
                    record["exitCode"] = process.wait()
            record["status"] = "complete"
    except BaseException as error:
        record["status"] = "failed"
        record["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        record["finishedUtc"] = utc_now()
        write_json(directory / "process.json", record)
    return record


def normalize_config(config, environment):
    """验证明确的矩阵和版本归属，不推断旧程序来自当前源码。"""
    if config.get("protocolVersion") != 1 or set(config["versions"]) != {"old", "new"}:
        raise ValueError("配置需要协议 1 及 old/new 两份明确版本")
    topology = environment.topology()
    groups, ids = [], set()
    for raw in config["groups"]:
        group = dict(raw)
        name = group["id"]
        if not name or not all(character.isalnum() or character in "-_" for character in name) or name in ids:
            raise ValueError("配置编号重复或含路径字符")
        ids.add(name)
        comparison = group["comparison"]
        if comparison not in {"AB", "AA", "BB"} or group["layout"] not in {"pair", "abba"}:
            raise ValueError("未知配对方式")
        group["versions"] = {"A": "new" if comparison == "BB" else "old",
                             "B": "old" if comparison == "AA" else "new"}
        mask = group["affinity"]
        if mask == "all":
            mask = topology["processAffinity"]
        elif isinstance(mask, str) and mask.startswith("l3:"):
            index = int(mask[3:])
            if index < 0 or index >= len(topology["l3Groups"]):
                raise ValueError("三级缓存分组索引越界")
            mask = topology["l3Groups"][index]["mask"]
        if isinstance(mask, bool) or not isinstance(mask, int) or mask <= 0 or mask & ~topology["processAffinity"]:
            raise ValueError("实验掩码必须属于本采集器原有处理器集合")
        group["affinity"] = mask
        if group["mode"] not in {"benchmark", *DIAGNOSTICS}:
            raise ValueError("未知测量模式")
        if group["policy"] not in {"default", "serial-incremental", "maximum-parallel-incremental"}:
            raise ValueError("未知执行策略")
        if group["backend"] not in {"OpenGL", "D3D12"}:
            raise ValueError("必须记录图形后端")
        if group["mode"] == "benchmark" and group["profile"] not in {"standard", "budget-saturation"}:
            raise ValueError("普通矩阵只支持已冻结的两类场景")
        groups.append(group)
    if not groups:
        raise ValueError("配置矩阵不能为空")
    return groups, topology


def capture_identities(config, groups):
    versions = {}
    for version, description in config["versions"].items():
        required = {"executable", "sourceArchive", "buildCache"}
        if any(group["mode"] != "benchmark" for group in groups):
            required.add("driver")
        if not required.issubset(description):
            raise ValueError("缺少可追溯的程序、源码、构建或探针驱动身份")
        versions[version] = {key: file_identity(Path(description[key]).resolve()) for key in sorted(required)}
    if any(group["mode"] != "benchmark" for group in groups):
        if versions["old"]["driver"]["sha256"] != versions["new"]["driver"]["sha256"]:
            raise ValueError("旧、新 Probe 必须使用完全相同的测试驱动")
    inputs = [file_identity(path) for path in sorted((ROOT / "assets/heightmaps").glob("*")) if path.is_file()]
    for group in groups:
        if group["mode"] != "benchmark":
            scenarios = Path(group["scenarios"]).resolve()
            inputs.extend([file_identity(scenarios), file_identity(Path(group["cameras"]).resolve())])
            _, assets = selected_assets(scenarios, [group["profile"]])
            inputs.extend(assets.values())
    return {"versions": versions, "inputs": inputs, "collectorSource": source_identity()}


def command_for(config, group, version, directory):
    executable = str(Path(config["versions"][version]["executable"]).resolve())
    if group["mode"] == "benchmark":
        return [executable, "--benchmark", "--algorithm", "dod", "--profile", group["profile"],
                "--pass-policy", group["policy"], "--csv", str(directory / "frames.csv")]
    return [executable, str(ROOT), str(Path(group["scenarios"]).resolve()), str(Path(group["cameras"]).resolve()),
            group["profile"], group["policy"], group["mode"], str(directory / "frames.csv")]


def run_attempt(config, output, cohort, environment=None):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    metadata = {"protocolVersion": 1, "attemptId": str(uuid.uuid4()), "cohort": cohort,
                "status": "running", "startedUtc": utc_now(), "warmupBlocks": 1, "measuredBlocks": 5,
                "diagnosticsProtocol": DIAGNOSTICS, "config": config}
    write_json(output / "metadata.json", metadata)
    runs = []
    try:
        environment = WindowsEnvironment() if environment is None else environment
        groups, topology = normalize_config(config, environment)
        metadata.update({"groups": groups, "processorTopology": topology,
                         "environmentBefore": basic_environment(), "identitiesBefore": capture_identities(config, groups)})
        if "environmentQueryError" in metadata["environmentBefore"]:
            raise RuntimeError("环境查询失败，不能开始验收采集")
        write_json(output / "metadata.json", metadata)
        for group, block, slot, label, warmup in schedule(groups):
            version = group["versions"][label]
            relative = f"{group['id']}/{block}-{slot}-{label}"
            directory = output / relative
            print(f"{cohort} {relative} {version}", flush=True)
            record = run_process(command_for(config, group, version, directory), directory,
                                 environment, group["affinity"])
            record.update({"group": group["id"], "block": block, "slot": slot, "label": label,
                           "version": version, "warmup": warmup, "relativeDirectory": relative,
                           "diagnostics": DIAGNOSTICS["diagnostics-on" if group["mode"] == "benchmark" else group["mode"]],
                           "executableSha256": metadata["identitiesBefore"]["versions"][version]["executable"]["sha256"]})
            # CSV 校验也属于进程完成条件，失败文件和进程计数仍保留在独占目录中
            try:
                summary = summarize_frames(directory / "frames.csv", group["mode"])
                record.update({"csv": summary["csv"], "rowCount": summary["rowCount"]})
            except BaseException as error:
                record.update({"status": "failed", "error": str(error)})
                raise
            finally:
                runs.append(record)
                write_json(directory / "process.json", record)
                write_json(output / "runs.json", runs)
        metadata["identitiesAfter"] = capture_identities(config, groups)
        metadata["environmentAfter"] = basic_environment()
        if metadata["identitiesBefore"] != metadata["identitiesAfter"]:
            raise RuntimeError("采集期间输入或实现身份变化")
        if metadata["environmentBefore"] != metadata["environmentAfter"]:
            raise RuntimeError("采集期间基础环境改变")
        metadata["status"] = "complete"
    except BaseException as error:
        metadata.update({"status": "failed", "error": f"{type(error).__name__}: {error}"})
        raise
    finally:
        metadata["finishedUtc"] = utc_now()
        write_json(output / "metadata.json", metadata)
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cohort", required=True)
    args = parser.parse_args()
    run_attempt(json.loads(args.config.read_text(encoding="utf-8")), args.output, args.cohort)
    write_report(analyze_attempt(args.output), args.output / "analysis")


if __name__ == "__main__":
    main()
