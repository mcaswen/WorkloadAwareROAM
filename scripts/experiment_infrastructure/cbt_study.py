"""冻结有限CBT面板并复用EIP执行；数量校准在任何质量评价之前封存。"""
from pathlib import Path
import csv
import shutil

from .catalog import ROOT, load_json, content_hash
from .runner import run, save
from .quality import evaluate


PROTOCOL = ROOT / "configs/experiments/cbt_2024/comparison_protocol.json"


def prepare(output, cbt_exe, cpu_exe, probe, cbt_provenance, cpu_provenance):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    protocol = load_json(PROTOCOL)
    protocol["programs"] = {}
    for name, exe, origin in (("cbt", cbt_exe, cbt_provenance), ("cpu", cpu_exe, cpu_provenance)):
        exe = Path(exe).resolve()
        origin = Path(origin).resolve()
        manifest = load_json(origin)
        if content_hash(exe) != manifest["binary"]["sha256"]:
            raise ValueError("二进制与声明的构建来源不符")
        directory = output / "provenance" / name
        directory.mkdir(parents=True)
        for file in ("manifest.json", "sources.json", "sources.zip"):
            shutil.copy2(origin.parent / file, directory / file)
        shutil.copy2(exe, directory / "program.exe")
        protocol["programs"][name] = {
            "path": str(exe), "sha256": content_hash(exe),
            "sourceManifest": str(directory / "manifest.json"),
            "sourceManifestSha256": content_hash(directory / "manifest.json"),
        }
    protocol["probe"] = {"path": str(Path(probe).resolve()), "sha256": content_hash(Path(probe))}
    cases = output / "cases"
    cases.mkdir()
    protocol["cases"] = []
    for asset in protocol["assets"]:
        name = asset["id"]
        base = load_json(ROOT / f"configs/experiments/formal/fer_01/{name}-b50000-dod-t8.json")
        base.update(backend="d3d12", camera=asset["camera"], maxFrames=asset["maxFrames"])
        for area in protocol["cbtAreas"]:
            case = {**base, "id": f"{name}-cbt-a{area}", "algorithm": "cbt", "workers": 1,
                "cbtArea": area, "cbtCapacity": protocol["cbtCapacity"],
                "cbtValidation": "off", "cbtGeometry": "modified"}
            save(cases / (case["id"] + ".json"), case)
            protocol["cases"].append(case["id"])
        for budget in protocol["cpuBudgets"]:
            for algorithm in ("dod", "transactional"):
                case = {**base, "id": f"{name}-{algorithm}-b{budget}", "algorithm": algorithm,
                    "budget": budget, "heightPolicy": "immutable" if algorithm == "transactional" else "fit",
                    "flipRecovery": algorithm == "transactional"}
                save(cases / (case["id"] + ".json"), case)
                protocol["cases"].append(case["id"])
    protocol["caseHashes"] = {name: content_hash(cases / (name + ".json")) for name in protocol["cases"]}
    save(output / "protocol.json", protocol)
    return output


def execute(output):
    output = Path(output).resolve()
    protocol = load_json(output / "protocol.json")
    for identity in (*protocol["programs"].values(), protocol["probe"]):
        if content_hash(Path(identity["path"])) != identity["sha256"]:
            raise ValueError("冻结程序内容已改变")
    cases = []
    for name in protocol["cases"]:
        path = output / "cases" / (name + ".json")
        if content_hash(path) != protocol["caseHashes"][name]:
            raise ValueError("冻结case被修改")
        cases.append(load_json(path))

    def run_one(case, mode):
        name = case["id"]
        target = output / "runs" / name / mode
        program = protocol["programs"]["cbt" if case["algorithm"] == "cbt" else "cpu"]
        manifest_path = target / "manifest.json"
        if manifest_path.exists():
            manifest = load_json(manifest_path)
            if manifest["status"] != "ok" or manifest["binary"]["sha256"] != program["sha256"]:
                raise ValueError("已有失败/不同程序运行，禁止覆盖或静默重跑")
            return target
        run(output / "cases" / (name + ".json"), target, Path(program["path"]), mode=mode)
        manifest = load_json(manifest_path)
        # runner归档当前编排源码；旧CPU程序的真实构建来源必须另行绑定
        manifest["sourceSnapshotRole"] = "current orchestration tree, not necessarily binary build source"
        manifest["binaryProvenance"] = program
        save(manifest_path, manifest)
        print(f"完成 {name} / {mode}", flush=True)
        return target

    calibration = output / "calibration.json"
    if not calibration.exists():
        records = []
        for case in cases:
            if case["algorithm"] != "cbt":
                continue
            target = run_one(case, "visual")
            with (target / "run/cbt.csv").open() as stream:
                rows = list(csv.DictReader(stream))
            records.append({"case": case["id"], "area": case["cbtArea"], "capacity": case["cbtCapacity"],
                "actual": [{"frame": int(r["frame"]), "N": int(r["actualFaces"])}
                    for r in rows if r["actualFaces"]],
                "faults": max(int(r["faults"]) for r in rows)})
        save(calibration, {"purpose": "N-only calibration before any quality evaluation",
            "selection": "all three frozen points retained without retuning", "records": records})
        print("六个CBT数量校准已封存，开始其余运行", flush=True)

    for case in cases:
        run_one(case, "timing")
        run_one(case, "visual")

    frames = {a["id"]: a["qualityFrames"] for a in protocol["assets"]}
    for case in cases:
        target = output / "quality" / case["id"]
        if target.exists():
            index = load_json(target / "quality-index.json")
            if any(item["status"] != "ok" for item in index["frames"]):
                raise ValueError("已有未通过质量证据，禁止覆盖")
            continue
        evaluate(output / "runs" / case["id"] / "visual", target,
            Path(protocol["probe"]["path"]), frames=frames[case["terrain"]], locations=True)
        print(f"质量评价 {case['id']}", flush=True)
    return output
