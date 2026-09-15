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
    analysis = sub.add_parser("analyze",help="校验证据并归约到唯一analysis")
    analysis.add_argument("--runs",nargs="+",type=Path,required=True)
    analysis.add_argument("--quality",nargs="*",type=Path,default=[])
    analysis.add_argument("--history",type=Path)
    analysis.add_argument("--pairs",type=Path,nargs="*",default=[])
    analysis.add_argument("--output",type=Path,required=True)
    quality = sub.add_parser("quality",help="离线评价实际输出")
    quality.add_argument("--run",type=Path,required=True)
    quality.add_argument("--output",type=Path,required=True)
    quality.add_argument("--probe",type=Path,required=True)
    quality.add_argument("--frames",type=int,nargs="+",default=[2,15,16,23])
    profile = sub.add_parser("profile",help="复用FPR采集新CPU ROI")
    profile.add_argument("collector",choices=["perf","tracy"])
    profile.add_argument("--case",type=Path,required=True)
    profile.add_argument("--output",type=Path,required=True)
    profile.add_argument("--executable",type=Path,required=True)
    profile.add_argument("--perf",default="perf")
    profile.add_argument("--capture",type=Path)
    profile.add_argument("--csvexport",type=Path)
    report = sub.add_parser("report",help="由analysis制作图表/HTML/Markdown")
    report.add_argument("--analysis",type=Path,required=True)
    report.add_argument("--output",type=Path,required=True)
    suite = sub.add_parser("suite",help="展开有限矩阵清单，不执行")
    suite.add_argument("--case",type=Path,required=True)
    suite.add_argument("--output",type=Path,required=True)
    suite.add_argument("--budgets",type=int,nargs="+",default=[50000])
    suite.add_argument("--workers",type=int,nargs="+",default=[8])
    suite.add_argument("--algorithms",choices=["classic","dod","transactional"],nargs="+",default=["transactional"])
    suite.add_argument("--prefixes",choices=["fixed64","scaled"],nargs="+",default=["scaled"])
    args = parser.parse_args()
    if args.command == "suite":
        from experiment_infrastructure.suite import expand
        print(expand(args.case,args.output,args.budgets,args.workers,args.algorithms,args.prefixes))
    elif args.command == "report":
        from experiment_infrastructure.report import build
        print(build(args.analysis,args.output))
    elif args.command == "analyze":
        from experiment_infrastructure.analysis import build
        print(build(args.runs,args.output,args.quality,json.loads(args.history.read_text()) if args.history else [],args.pairs))
    elif args.command == "quality":
        from experiment_infrastructure.quality import evaluate
        print(evaluate(args.run,args.output,args.probe,args.frames))
    elif args.command == "profile":
        from experiment_infrastructure.profile_adapter import collect
        print(collect(args.case,args.output,args.executable,args.collector,args.perf,args.capture,args.csvexport))
    elif args.command == "run":
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
