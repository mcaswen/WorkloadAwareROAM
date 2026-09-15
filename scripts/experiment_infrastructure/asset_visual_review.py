"""将真实平台原图与目录关联；拼版不替代各帧原件。"""
from pathlib import Path
import json
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from PIL import Image
from .catalog import CATALOG, load_json
from .asset_preview import configure_font


def contact(platform: Path):
    configure_font()
    assets = [a for a in load_json(CATALOG)["terrains"] if a["kind"] != "legacy"]
    for angle in range(2):
        fig, axes = plt.subplots(5, 2, figsize=(12, 17), layout="constrained")
        for asset, ax in zip(assets, axes.flat):
            path = platform / asset["id"] / f"view-{angle}.ppm"
            with Image.open(path) as image:
                ax.imshow(image)
                image.save(path.with_suffix(".png"))
            ax.set_title(f"{asset['name']} | {asset['width']}² | 视角{angle}", fontsize=12)
            ax.set_axis_off()
        fig.suptitle("真实OpenGL资产回放；DOD，2万预算，截图回读不用于计时",fontsize=15)
        fig.savefig(platform / f"contact-{angle}.png", dpi=110)
        plt.close(fig)


if __name__ == "__main__":
    import sys
    contact(Path(sys.argv[1]))
