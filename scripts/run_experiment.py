"""项目实验入口；具体工作委托专用模块。"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from experiment_infrastructure.catalog import validate_catalog, resolve_case


def main():
    parser = argparse.ArgumentParser(description="实验资产、输入与证据工具")
    sub = parser.add_subparsers(dest="command", required=True)
    preview = sub.add_parser("catalog", help="校验资产并生成预览")
    preview.add_argument("--output", type=Path, required=True)
    resolve = sub.add_parser("resolve", help="解析case并核对资产")
    resolve.add_argument("--case", type=Path, required=True)
    resolve.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "catalog":
        from experiment_infrastructure.asset_preview import build_preview
        if args.output.exists():
            raise ValueError("产物目录已存在，拒绝覆盖")
        print(build_preview(validate_catalog(), args.output))
    elif args.command == "resolve":
        data = resolve_case(args.case)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x", encoding="utf-8") as stream:
            json.dump(data, stream, ensure_ascii=False, indent=2)


if __name__ == "__main__":
    main()
