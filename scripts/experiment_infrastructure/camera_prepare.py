"""调用同一C++相机实现冻结路线；不在Python重算投影。"""
from pathlib import Path
import json
import hashlib
from run_transactional_platform import native_run, ROOT, win


def prepare(executable: Path, output: Path) -> None:
    frozen = ROOT / "configs/experiments/cameras/frozen"
    frozen.mkdir(parents=True, exist_ok=True)
    catalog = json.loads((ROOT/"assets/experiments/terrain_catalog.json").read_text())
    index = []
    for asset in catalog["terrains"]:
        for recipe in sorted((ROOT/"configs/experiments/cameras/recipes").glob("*.json")):
            identity = asset["id"] + "-" + recipe.stem
            path = frozen / (identity + ".csv")
            result = native_run(output, identity, executable, "", "", 1, "",
                arguments=["--experiment-camera", "build", asset["id"], win(recipe), win(path)])
            if result["status"] != "ok":
                raise RuntimeError(identity + ": camera freeze failed")
            index.append(dict(id=identity, asset=asset["id"], recipe=recipe.relative_to(ROOT).as_posix(),
                assetSampleSha256=asset["sampleSha256"],
                path=path.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    for asset, kind in (("test129","historical-a64"),("peking547","historical-orbit"),
                        ("test129","pq-return24"),("peking547","pq-return24"),("peking547","sve8")):
        identity = asset + "-" + kind
        path = frozen / (identity + ".csv")
        result = native_run(output, identity, executable, "", "", 1, "",
            arguments=["--experiment-camera", "legacy", kind, "peking" if asset=="peking547" else "test129", win(path)])
        if result["status"] != "ok": raise RuntimeError(identity + ": legacy freeze failed")
        index.append(dict(id=identity, asset=asset, recipe="legacy C++ export",
            path=path.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    (ROOT/"configs/experiments/cameras/catalog.json").write_text(
        json.dumps(dict(schemaVersion=1,cameras=index),ensure_ascii=False,indent=2)+"\n")
