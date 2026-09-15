"""CBT有限比较入口；执行与离线报告分别运行，避免重绘触发算法。"""
import argparse
from pathlib import Path
from experiment_infrastructure import cbt_study


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("prepare", "run", "report"))
    parser.add_argument("output", type=Path)
    for key in ("cbt-exe", "cpu-exe", "probe", "cbt-provenance", "cpu-provenance"):
        parser.add_argument("--" + key, type=Path)
    args = parser.parse_args()
    if args.action == "prepare":
        values = (args.cbt_exe, args.cpu_exe, args.probe, args.cbt_provenance, args.cpu_provenance)
        if any(value is None for value in values):
            parser.error("prepare必须显式指定程序、probe和两份构建来源")
        cbt_study.prepare(args.output, *values)
    elif args.action == "run":
        cbt_study.execute(args.output)
    else:
        from experiment_infrastructure.cbt_report import report
        report(args.output)


if __name__ == "__main__":
    main()
