"""输入边界的定向验收，包含实际C++样本交叉核对和离线费用。"""
from __future__ import annotations
import argparse
import hashlib
import json
import subprocess
import time
from pathlib import Path
from .catalog import ROOT, validate_catalog, source_samples, resolve_case, load_json, local_path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    start = time.perf_counter()
    assets = validate_catalog()
    validation_ms = (time.perf_counter() - start) * 1000
    rows = []
    for asset in assets:
        case_path = ROOT / "configs/experiments/suites" / f"Scenario_{asset['id']}_Input.json"
        case = resolve_case(case_path)
        resolved = args.output / (asset["id"] + ".json")
        resolved.write_text(json.dumps(case, indent=2), encoding="utf-8")
        result = subprocess.run([str(args.probe), str(resolved.resolve()),
                                 str((args.output / asset["id"]).resolve())],
                                capture_output=True, text=True, timeout=30, check=True)
        raw = args.output / asset["id"] / "samples.u16"
        assert hashlib.sha256(raw.read_bytes()).hexdigest() == asset["sampleSha256"]
        # 同一原始加载函数重复；新增清单校验与JSON解析费用独立列出
        source_samples(ROOT / asset["path"])
        begin = time.perf_counter()
        source_samples(ROOT / asset["path"])
        rows.append({"id": asset["id"], "decodeMs": (time.perf_counter() - begin) * 1000,
                     "cpp": result.stdout.strip(), "sampleMatch": True})
    bad = args.output / "duplicate.json"
    bad.write_text('{"a": 1, "a": 2}')
    for operation in (lambda: load_json(bad), lambda: local_path(ROOT, "../outside")):
        try:
            operation()
        except ValueError:
            pass
        else:
            raise AssertionError("损坏输入未拒绝")
    summary = {"catalogValidationMs": validation_ms, "rows": rows,
               "scope": "单进程开发快验；并非帧时间或统计显著性"}
    (args.output / "validation.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2))
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
