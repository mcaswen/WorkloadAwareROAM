"""冻结数值配方，生成可重建的U16高度输入。"""
from __future__ import annotations
import json
from pathlib import Path
import numpy as np
from PIL import Image
from .catalog import ROOT, CATALOG, content_hash, source_samples, sample_hash, load_json

SEED = 20260915
RECIPES = [("flat", 129, "平面"), ("ramp", 513, "非对称斜坡"),
           ("spike", 513, "局部尖峰"), ("ridge", 513, "山脊"),
           ("valley", 513, "深谷"), ("multiscale", 1001, "多尺度起伏")]


def evaluate(kind: str, u: np.ndarray, v: np.ndarray) -> np.ndarray:
    if kind == "flat":
        return np.full_like(u, 0.3)
    if kind == "ramp":
        return 0.08 + 0.57*u + 0.23*v
    if kind == "spike":
        return 0.08 + 0.8*np.exp(-((u-0.63)**2+(v-0.42)**2)/0.0015)
    if kind == "ridge":
        return 0.12+0.72*np.exp(-((v-0.5-0.13*np.sin(7*u))/0.075)**2)*(0.7+0.3*np.sin(5*u)**2)
    if kind == "valley":
        return 0.8-0.67*np.exp(-((u-0.5-0.14*np.sin(6*v))/0.085)**2)+0.06*np.sin(4*v)
    if kind == "multiscale":
        result = np.full_like(u, 0.5)
        rng = np.random.default_rng(SEED)
        for frequency, amplitude in [(1, .18), (2, .1), (4, .055), (8, .03), (16, .015), (32, .008)]:
            phase = rng.uniform(0, 2*np.pi, 2)
            result += amplitude*np.sin(2*np.pi*frequency*u+phase[0])*np.cos(2*np.pi*frequency*v+phase[1])
        return result
    raise ValueError(kind)


def generate():
    catalog = load_json(CATALOG)
    known = {asset["id"]: asset for asset in catalog["terrains"]}
    directory = ROOT / "assets/heightmaps/experiment/generated"
    directory.mkdir(parents=True, exist_ok=True)
    for kind, size, name in RECIPES:
        u, v = np.meshgrid(np.linspace(0, 1, size), np.linspace(0, 1, size))
        values = evaluate(kind, u, v)
        if not np.isfinite(values).all() or values.min() < 0 or values.max() > 1:
            raise ValueError("配方超出冻结归一化范围")
        samples = np.rint(values*65535).astype("<u2")
        path = directory / f"Hm_Experiment_{kind.title()}_{size}.png"
        if path.exists():
            assert np.array_equal(source_samples(path), samples), "已有生成资产与配方不一致"
        else:
            Image.fromarray(samples).save(path)
        ident = "generated-" + kind
        known[ident] = dict(id=ident, name=name, path=path.relative_to(ROOT).as_posix(),
            width=size, height=size, terrainSize=80, heightScale=12,
            fileSha256=content_hash(path), sampleSha256=sample_hash(samples),
            sampleEncoding="uint16-le-row-major", kind="generated",
            provenance={"source":"项目数值函数", "licenseStatus":"项目自制",
                        "recipeVersion":1, "recipe":kind, "seed":SEED,
                        "numpy":np.__version__, "rounding":"nearest-even",
                        "axis":"row=v increases; column=u increases",
                        "maxQuantizationError":float(np.max(np.abs(values-samples/65535)))})
    catalog["terrains"] = list(known.values())
    CATALOG.write_text(json.dumps(catalog,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")


if __name__ == "__main__":
    generate()
