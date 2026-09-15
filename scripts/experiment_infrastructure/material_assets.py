"""准备少量观察材质；下载仅使用Poly Haven原始diffuse文件。"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import urllib.request
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "benchmark-output/experiment-infrastructure/eip-03/sources"
USER_AGENT = "WorkloadAwareROAM-ExperimentInfrastructure/1.0"


def fetch(url: str) -> bytes:
    """先使用当前系统网络；WSL可借用Windows curl的正常代理配置。"""
    curl = Path("/mnt/c/Windows/System32/curl.exe")
    if curl.exists():
        proxy = ["--proxy", os.environ["ROAM_DOWNLOAD_PROXY"]] if os.environ.get("ROAM_DOWNLOAD_PROXY") else []
        return subprocess.run([str(curl), *proxy, "-fLsS", "--max-time", "45", "-A",
                               USER_AGENT, url], capture_output=True, check=True).stdout
    return urllib.request.urlopen(urllib.request.Request(
        url, headers={"User-Agent": USER_AGENT}), timeout=45).read()


def prepare() -> None:
    generated = ROOT / "assets/textures/experiment/generated"
    external = ROOT / "assets/textures/experiment/external"
    for path in (SOURCE, generated, external):
        path.mkdir(parents=True, exist_ok=True)
    Image.new("RGB", (8, 8), (155, 155, 155)).save(generated / "Tex_Neutral.png")
    uv = Image.new("RGB", (512, 512))
    draw = ImageDraw.Draw(uv)
    font_path = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
    font = ImageFont.truetype(str(font_path), 22) if font_path.exists() else ImageFont.load_default()
    for y in range(8):
        for x in range(8):
            draw.rectangle((x*64, y*64, x*64+63, y*64+63),
                           fill=(175, 185, 195) if (x+y)%2 else (55, 65, 80))
    draw.line((24, 30, 200, 30), fill=(255, 80, 80), width=8)
    draw.polygon([(210, 30), (188, 18), (188, 42)], fill=(255, 80, 80))
    draw.text((32, 44), "+U / +X", fill="white", font=font)
    draw.line((30, 140, 30, 310), fill=(65, 240, 130), width=8)
    draw.polygon([(30, 320), (18, 296), (42, 296)], fill=(65, 240, 130))
    draw.text((45, 165), "+V / +Z", fill="white", font=font)
    uv.save(generated / "Tex_Uv_Direction.png")
    rows = []

    def entry(identity, name, path, tiling, tint, source):
        rows.append(dict(id=identity, name=name, path=path.relative_to(ROOT).as_posix(),
                         tiling=tiling, heightTint=tint,
                         sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                         colorEncoding="RGB8 interpreted as legacy UNORM; no new gamma transform",
                         source=source))
    entry("legacy", "历史棋盘/高度染色", ROOT/"assets/textures/Tex_Terrain_Debug_Diffuse.ppm",
          12, .35, {"kind": "existing-project"})
    entry("neutral", "中性光照", generated/"Tex_Neutral.png", 1, 0, {"kind": "generated", "rgb": [155]*3})
    entry("uv", "方向UV检查", generated/"Tex_Uv_Direction.png", 2, 0, {"kind": "generated", "version": 1})
    for asset, identity, name in (("rock_boulder_dry", "rock", "岩石 / Poly Haven"),
                                   ("brown_mud", "soil", "土壤 / Poly Haven"),
                                   ("aerial_grass_rock", "grass", "草岩 / Poly Haven")):
        metadata = SOURCE / (asset + ".json")
        if not metadata.exists():
            metadata.write_bytes(fetch("https://api.polyhaven.com/files/" + asset))
        info = json.loads(metadata.read_text())["Diffuse"]["1k"]["jpg"]
        path = external / ("Tex_" + asset + "_diff_1k.jpg")
        if not path.exists():
            path.write_bytes(fetch(info["url"]))
        data = path.read_bytes()
        assert len(data) == info["size"] and hashlib.md5(data).hexdigest() == info["md5"]
        assert Image.open(path).size == (1024, 1024)
        entry(identity, name, path, 12, 0, dict(kind="download", asset=asset,
              url=info["url"], date="2026-09-15", license="CC0",
              licenseUrl="https://polyhaven.com/license", apiMd5=info["md5"]))
    (ROOT/"assets/experiments/material_catalog.json").write_text(
        json.dumps({"schemaVersion": 1, "materials": rows}, ensure_ascii=False, indent=2)+"\n")


if __name__ == "__main__":
    prepare()
