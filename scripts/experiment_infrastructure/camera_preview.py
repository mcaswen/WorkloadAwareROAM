"""显示冻结相机位置、事件和真实回放帧；不替代相机矩阵构造。"""
from pathlib import Path
import csv
import json
import html
import numpy as np
from PIL import Image, ImageDraw
import matplotlib.pyplot as plt
from .asset_preview import configure_font
from .catalog import ROOT, source_samples


def build_camera_review(asset_id: str, output: Path) -> None:
    configure_font()
    catalog = json.loads((ROOT/"assets/experiments/terrain_catalog.json").read_text())
    asset = next(a for a in catalog["terrains"] if a["id"] == asset_id)
    heights = source_samples(ROOT/asset["path"])*float(asset["heightScale"])/65535
    cards = []
    for kind in ("overview-orbit","approach-return","terrain-traverse","reveal-return","stationary-recovery"):
        route = ROOT/"configs/experiments/cameras/frozen"/(asset_id+"-"+kind+".csv")
        rows = list(csv.DictReader(route.open()))
        x, z = ([float(r[k]) for r in rows] for k in ("px","pz"))
        figure, axes = plt.subplots(1, 2, figsize=(10, 4), layout="constrained")
        half = asset["terrainSize"]/2
        axes[0].imshow(heights, extent=(-half,half,-half,half),origin="lower",cmap="terrain")
        axes[0].plot(x,z,color="#be3560",lw=1.8)
        axes[0].scatter([x[0]],[z[0]],marker="o",color="#293b75",label="起点")
        axes[0].set(xlabel="世界X",ylabel="世界Z",title="冻结路线；参考源高度")
        axes[0].legend()
        axes[1].plot([r["frame"] for r in rows],[float(r["py"]) for r in rows],color="#23758c")
        axes[1].set_xticks([0,len(rows)//2,len(rows)-1])
        axes[1].set(xlabel="更新机会",ylabel="世界Y",title="离地轨迹与事件")
        for i in range(1,len(rows)):
            if rows[i]["event"]!=rows[i-1]["event"]:
                axes[1].axvline(i,color="#9c9c9c",ls=":")
                axes[1].annotate(rows[i]["event"],(i,float(rows[i]["py"])),rotation=20)
        figure.suptitle(kind+" | "+asset_id)
        figure.savefig(output/(kind+"-route.png"),dpi=130)
        plt.close(figure)
        frames = list(csv.DictReader((output/kind/"frames.csv").open()))
        captured = [f for f in frames if f["image"]]
        for f in captured:
            path = output/kind/f["image"]
            Image.open(path).save(path.with_suffix(".png"))
            f["png"] = kind+"/"+path.with_suffix(".png").name
        picks = [captured[i] for i in sorted(set([0,len(captured)//4,len(captured)//2,3*len(captured)//4,len(captured)-1]))]
        canvas = Image.new("RGB",(1920,770),(242,244,248))
        draw = ImageDraw.Draw(canvas)
        for i,f in enumerate(picks):
            image = Image.open(output/f["png"]).resize((640,360))
            # 联系图固定缩放、不裁掉异常区域
            column,row=i%3,i//3
            canvas.paste(image,(column*640,row*385+25))
            draw.text((column*640+10,row*385+7),f"frame {f['frame']} / {f['event']} / N={f['faces']}",fill="black")
        canvas.save(output/(kind+"-frames.png"))
        identifier=kind.replace("-","_")
        data=json.dumps([{"src":f["png"],"frame":f["frame"],"event":f["event"]} for f in captured])
        cards.append(f'<section><h2>{html.escape(kind)}</h2><img src="{kind}-route.png">'
            f'<img src="{kind}-frames.png"><p>所有机会均更新；每四机会采一帧，7.5张/名义秒。未测实时30FPS。</p>'
            f'<img id="{identifier}_image" src="{captured[0]["png"]}"><input type="range" min="0" max="{len(captured)-1}" value="0" '
            f'oninput="show_{identifier}(Number(this.value))"><button onclick="play_{identifier}()">播放/暂停</button>'
            f'<span id="{identifier}_label"></span><script>const data_{identifier}={data};let timer_{identifier}=null,i_{identifier}=0;'
            f'function show_{identifier}(i){{i_{identifier}=i;let f=data_{identifier}[i];document.getElementById("{identifier}_image").src=f.src;'
            f'document.getElementById("{identifier}_label").textContent="机会 "+f.frame+" / "+f.event;}}'
            f'function play_{identifier}(){{if(timer_{identifier}){{clearInterval(timer_{identifier});timer_{identifier}=null;}}'
            f'else timer_{identifier}=setInterval(()=>show_{identifier}((i_{identifier}+1)%data_{identifier}.length),133);}}</script></section>')
    (output/"index.html").write_text('<!doctype html><meta charset="utf-8"><title>冻结相机视觉审查</title>'
        '<style>body{font:16px system-ui;max-width:1300px;margin:2rem auto;background:#f3f5f8}section{background:white;padding:1rem;margin:2rem 0}'
        'img{display:block;max-width:100%}input{width:70%}</style><h1>路线、事件与实际平台画面</h1>'+"".join(cards))
