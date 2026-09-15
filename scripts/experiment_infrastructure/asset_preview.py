"""资产信息图只描述输入，不充当真实平台画面。"""
from __future__ import annotations
import html
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.font_manager as fonts
import numpy as np
from .catalog import ROOT, source_samples


def configure_font():
    font = ROOT / "assets/fonts/SourceHanSansSC-Regular.ttf"
    fonts.fontManager.addfont(str(font))
    plt.rcParams.update({"font.family": fonts.FontProperties(fname=str(font)).get_name(),
                         "axes.unicode_minus": False})


def build_preview(assets: list[dict], output: Path) -> Path:
    configure_font()
    output.mkdir(parents=True, exist_ok=True)
    cards = []
    for asset in assets:
        samples = source_samples(ROOT / asset["path"])
        heights = samples.astype(float) * asset["heightScale"] / 65535
        dz, dx = np.gradient(heights, asset["terrainSize"] / (asset["width"] - 1))
        shade = np.clip((1 - dx * 0.6 - dz * 0.8) / np.sqrt(2 * (1 + dx * dx + dz * dz)), 0, 1)
        figure, axes = plt.subplots(1, 3, figsize=(12, 3.5), layout="constrained")
        image = axes[0].imshow(heights, cmap="terrain", origin="lower")
        axes[0].set_title("源高度；U向右 / V向上")
        figure.colorbar(image, ax=axes[0], label="世界高度")
        axes[1].imshow(shade, cmap="gray", origin="lower", vmin=0, vmax=1)
        axes[1].set_title("源高度阴影预览")
        axes[2].hist(heights.ravel(), bins=40, color="#3b8c9e")
        axes[2].set(title="高度分布", xlabel="世界高度", ylabel="样本数量")
        figure.suptitle(f"{asset['name']} | {asset['width']}×{asset['height']} | "
                       f"跨度 {asset['terrainSize']} / 高度比例 {asset['heightScale']}")
        name = asset["id"] + ".png"
        figure.savefig(output / name, dpi=130)
        plt.close(figure)
        cards.append(f"<section><h2>{html.escape(asset['name'])}</h2>"
                     f"<img src='{name}'><p>来源/许可：{html.escape(asset['provenance']['licenseStatus'])}</p>"
                     f"<code>文件SHA256：{asset['fileSha256']}</code></section>")
    page = output / "index.html"
    page.write_text("<!doctype html><meta charset='utf-8'><title>地形资产目录</title>"
                    "<style>body{font:16px sans-serif;max-width:1300px;margin:32px auto;background:#f7f7f5}"
                    "img{width:100%}section{background:white;padding:20px;margin:20px 0}code{overflow-wrap:anywhere}</style>"
                    "<h1>地形资产目录</h1><p>这是离线输入预览，真实平台画面另行验收。</p>"
                    + "".join(cards), encoding="utf-8")
    return page
