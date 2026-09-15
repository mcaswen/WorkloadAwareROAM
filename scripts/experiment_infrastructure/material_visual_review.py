"""将同网格材质真帧整理为双后端对照，保留原始捕获。"""
from pathlib import Path
import csv
from PIL import Image, ImageDraw


def assemble(root: Path) -> None:
    links = []
    for backend in ("opengl", "d3d12"):
        path = root / (backend + "-materials")
        rows = list(csv.DictReader((path / "frames.csv").open()))
        for angle in ("0", "1"):
            group = [row for row in rows if row["view"] == angle]
            assert len({(row["sequence"], row["topologyHash"]) for row in group}) == 1
            canvas = Image.new("RGB", (1440, 590), (240, 242, 245))
            draw = ImageDraw.Draw(canvas)
            for i, row in enumerate(group):
                image = Image.open(path / row["image"])
                image.save((path / row["image"]).with_suffix(".png"))
                x, y = (i % 3) * 480, (i // 3) * 295
                canvas.paste(image.resize((480, 270)), (x, y+25))
                draw.text((x+10, y+7), backend + " / " + row["material"], fill="black")
            filename = "contact-" + angle + ".png"
            canvas.save(path / filename)
            links.append(f'<h2>{backend} / 视角{angle}</h2><img src="{path.name}/{filename}">')
    (root / "materials.html").write_text('<!doctype html><meta charset="utf-8">'
        '<style>body{font-family:system-ui;margin:2rem;background:#f4f5f8}img{max-width:100%}</style>'
        '<h1>同网格材质与后端对照</h1><p>原始GPU帧；同一后端/视角切换不更新LOD。'
        '保持历史UNORM颜色行为，不做跨后端像素等价要求。</p>' + "".join(links))
