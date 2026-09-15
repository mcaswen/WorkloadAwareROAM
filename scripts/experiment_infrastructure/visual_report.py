"""真实帧播放器与质量见证图；原始画面保留，叠层和裁剪明确标注。"""
from __future__ import annotations
import csv
import html
import os
import shutil
from pathlib import Path
import numpy as np
from PIL import Image
import matplotlib.pyplot as plt
from .figures import FigureWriter
from .runner import save
from .catalog import source_samples
from .quality import valid_quality


def build_visuals(data,output,analysis_path):
    output=Path(output);media=output/"media";media.mkdir()
    players=[]
    w=FigureWriter(output/"visual-figures",analysis_path)
    routes_seen = set()
    for run in data["runs"]:
        if run["mode"]!="visual" or run["status"]!="ok": continue
        folder=Path(run["path"]);frames=[]
        camera=list(csv.DictReader((folder/"inputs/camera.csv").open()))
        for f in run["frames"]:
            if not f["image"]: continue
            source=folder/"run"/Path(f["image"]).with_suffix(".png")
            name=run["id"]+"-"+source.name
            shutil.copy2(source,media/name)
            frames.append({"frame":int(f["frame"]),"event":f["event"],"seconds":float(camera[int(f["frame"])]["nominalSeconds"]),
                "image":"media/"+name,"meshHash":f["hash"],"faces":f["faces"],"poseHash":f["poseHash"],
                "cpuMs":f["cpuMs"],"timingMode":"visual includes capture; not normal performance"})
        players.append({"id":run["id"],"title":run["case"]["terrain"]+" / "+run["case"]["algorithm"]+" / "+(f"area={run['case']['cbtArea']:g}" if run["case"]["algorithm"] == "cbt" else run["case"]["heightPolicy"]),
                        "frames":frames,"material":run["case"]["material"],"backend":run["case"]["backend"]})
        route_key = (run["case"]["sampleSha256"], run["case"]["cameraSha256"], len(run["frames"]))
        if route_key in routes_seen:
            continue
        routes_seen.add(route_key)
        # 路线图直接读取实际C++冻结行，背景仅是原高度样本预览
        source=source_samples(Path(run["case"]["heightMap"]))*run["case"]["heightScale"]/65535
        fig,axes=plt.subplots(1,2,figsize=(10,4))
        half=run["case"]["terrainSize"]/2
        axes[0].imshow(source,origin="lower",extent=(-half,half,-half,half),cmap="terrain")
        chosen=camera[:len(run["frames"])]
        axes[0].plot([float(r["px"]) for r in chosen],[float(r["pz"]) for r in chosen],color="#c23b56")
        axes[0].plot(float(chosen[0]["px"]),float(chosen[0]["pz"]),"ko",label="起点")
        heading=chosen[::max(1,len(chosen)//8)]
        axes[0].quiver([float(r["px"]) for r in heading],[float(r["pz"]) for r in heading],
            [float(r["fx"])*half*.35 for r in heading],[float(r["fz"])*half*.35 for r in heading],
            angles="xy",scale_units="xy",scale=1,color="#256eb0",alpha=.7,width=.005,label="朝向XZ投影")
        axes[0].set(xlabel="世界X",ylabel="世界Z");axes[0].legend(fontsize=8)
        axes[1].plot([int(r["frame"]) for r in chosen],[float(r["py"]) for r in chosen],color="#256eb0")
        axes[1].set(xlabel="更新机会",ylabel="相机世界Y")
        for i,r in enumerate(chosen):
            if i>0 and r["event"]!=chosen[i-1]["event"]:
                axes[1].axvline(i,color="#aaa",ls=":")
                axes[1].annotate(r["event"],(i,float(r["py"])),fontsize=7,rotation=25)
        w.emit(fig,"route-"+run["id"],run["case"]["terrain"]+"｜实际冻结路线",
            {"run":run["id"],"cameraSha256":run["case"]["cameraSha256"]},
            "位置/朝向从冻结CSV读取；箭头是定长缩放XZ方向，右侧事件不按算法耗时推进")
    for q in data["quality"]:
        quality_run=next((r for r in data["runs"] if r["path"]==q["run"]),None)
        if quality_run is None:continue
        visual=next((r for r in data["runs"] if r["mode"]=="visual" and r["executionId"]==quality_run["executionId"]),None)
        if visual is None:continue
        valid = [row for row in q["frames"] if valid_quality(row)]
        if (data.get("study") or {}).get("report", {}).get("witnessMode") == "worst-registered" and valid:
            valid = [max(valid, key=lambda row: row["result"]["screenMax"])]
        # 同一参考源的所有比较共用全域色标
        vmax=max((i["result"]["screenMax"] or 0 for source in data["quality"] if source["sourceSha256"]==q["sourceSha256"]
                  for i in source["frames"] if valid_quality(i)),default=1)
        vmax=max(vmax,1e-12)
        for item in valid:
            f=next((f for f in visual["frames"] if f["frame"]==item["frame"] and f["hash"]==item["meshHash"] and f["image"]),None)
            if not f or not item.get("locations"):continue
            records=list(csv.DictReader((Path(q["path"])/item["locations"]).open()))
            visible=[p for p in records if p["visible"]=="1" and float(p["screenError"])>=0]
            witness=next((p for p in records if p["role"]=="screen-witness"),None)
            if witness is None:continue
            grid=np.full((64,64),np.nan);counts=np.zeros((64,64),dtype=int)
            for p in visible:
                x=min(63,max(0,int(float(p["u"])*64)));y=min(63,max(0,int(float(p["v"])*64)))
                value=float(p["screenError"]);grid[y,x]=value if np.isnan(grid[y,x]) else max(grid[y,x],value);counts[y,x]+=1
            screenshot=Path(visual["path"])/"run"/Path(f["image"]).with_suffix(".png")
            raw=Image.open(screenshot);x,y=float(witness["pixelX"]),float(witness["pixelY"])
            fig,axes=plt.subplots(1,3,figsize=(14,4.8),gridspec_kw={"width_ratios":[1.4,1,1]})
            axes[0].imshow(raw);axes[0].scatter([x],[y],s=160,facecolors="none",edgecolors="#ed4949",linewidths=1.7)
            axes[0].set_title("真实画面＋参考投影见证");axes[0].axis("off")
            radius=100;left=min(max(0,int(x)-radius),raw.width-2*radius);top=min(max(0,int(y)-radius),raw.height-2*radius)
            axes[1].imshow(raw);axes[1].set_xlim(left,left+2*radius);axes[1].set_ylim(top+2*radius,top)
            axes[1].scatter([x],[y],s=100,facecolors="none",edgecolors="#ed4949",linewidths=1.5)
            axes[1].set_title("同图固定200×200像素裁剪");axes[1].axis("off")
            cmap=plt.get_cmap("magma").copy();cmap.set_bad("#dddddd")
            p=axes[2].imshow(grid,origin="lower",extent=(0,1,0,1),cmap=cmap,vmin=0,vmax=vmax,interpolation="nearest")
            axes[2].plot(float(witness["u"]),float(witness["v"]),"co",fillstyle="none")
            axes[2].set(xlabel="u",ylabel="v",title="已导出点的格内最大误差")
            fig.colorbar(p,ax=axes[2],label="px",fraction=.04)
            policy = f"area={quality_run['case']['cbtArea']:g}" if q["algorithm"] == "cbt" else q["heightPolicy"]
            title=f'{quality_run["case"]["terrain"]} / {q["algorithm"]} / {policy} / frame {item["frame"]} / Emax {item["result"]["screenMax"]:.4f}px'
            w.emit(fig,"witness-"+visual["id"]+"-"+str(item["frame"]),title,
                {"run":visual["id"],"frame":item["frame"],"meshHash":item["meshHash"],
                 "locations":str(Path(q["path"])/item["locations"]),"bins":64,"displayAggregation":"max of exported finite points",
                 "displayVmax":vmax,"crop":[left,top,200,200]},
                "灰格为没有导出点，未插值补全；完整Emax来自全Q。原图与叠层分开保存，裁剪不是新一次截图")
    save(output/"players.json",players)
    save(output/"visual-figures/figure-index.json",w.items)
    return players,w.items
