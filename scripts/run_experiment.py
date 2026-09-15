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
    replay = sub.add_parser("run", help="冻结输入并运行独立平台/CPU进程")
    replay.add_argument("--case",type=Path,required=True)
    replay.add_argument("--output",type=Path,required=True)
    replay.add_argument("--executable",type=Path,required=True)
    replay.add_argument("--mode",choices=["timing","visual","quality"])
    replay.add_argument("--backend",choices=["opengl","d3d12"])
    replay.add_argument("--cpu",action="store_true")
    args = parser.parse_args()
    if args.command == "run":
        from experiment_infrastructure.runner import run
        print(run(args.case,args.output,args.executable,args.mode,args.backend,args.cpu))
    elif args.command == "catalog":
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
