"""从analysis绘制固定口径研究图，每图保存来源与显示规则。"""
from __future__ import annotations
import csv
import math
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from .asset_preview import configure_font
from .catalog import content_hash,ROOT
from .runner import save
from .analysis import STAGES,PASS

COLORS={"classic":"#587391","dod":"#256eb0","transactional":"#ce7130","cbt":"#6b4da2"}
LABELS={"classic":"Classic","dod":"DOD","transactional":"Transactional","cbt":"GPU CBT"}
STAGE_LABELS={"seedMs":"种子","initializeMs":"初始化","viewMs":"视图",
"receiverMs":"接收认证","donorMs":"回收认证","reservationMs":"预留",
"topologyPrepareMs":"拓扑准备","topologyPublishMs":"拓扑发布","sampleRepairMs":"样本续接",
"meshPrepareMs":"网格准备","continuationMs":"续接发布","adapterMs":"适配",
"splitScoreMs":"split评分","mergeScoreMs":"merge评分","splitTopologyMs":"split拓扑",
"mergeTopologyMs":"merge拓扑","meshEmitMs":"mesh输出","unattributedCpuMs":"未归属"}
STYLE={"axes.spines.top":False,"axes.spines.right":False,"axes.grid":True,"grid.alpha":.18,
       "font.size":10,"axes.titlesize":12,"figure.titlesize":14,"savefig.facecolor":"white",
       "pdf.fonttype":42,"ps.fonttype":42,"svg.fonttype":"path"}


class FigureWriter:
    def __init__(self,output,analysis_path):
        self.output=Path(output);self.output.mkdir(parents=True,exist_ok=False)
        self.analysis_path=str(Path(analysis_path).resolve());self.hash=content_hash(Path(analysis_path));self.items=[]
        configure_font();plt.rcParams.update(STYLE)
    def emit(self,fig,name,title,selection,note):
        fig.suptitle(title)
        fig.text(.015,.012,note,fontsize=8,color="#555")
        fig.subplots_adjust(bottom=.17,top=.84,wspace=.3,hspace=.4)
        for ext in ("png","svg","pdf"):
            fig.savefig(self.output/(name+"."+ext),dpi=150)
        spec={"schemaVersion":"eip-figure-v1","id":name,"title":title,"analysis":self.analysis_path,
            "analysisSha256":self.hash,"selection":selection,"note":note,"colors":COLORS,
            "statistics":"consume analysis; no new significance or outcome classifier",
            "font":str(ROOT/"assets/fonts/SourceHanSansSC-Regular.ttf"),
            "fontSha256":content_hash(ROOT/"assets/fonts/SourceHanSansSC-Regular.ttf"),
            "files":{e:{"path":name+"."+e,"sha256":content_hash(self.output/(name+"."+e))} for e in ("png","svg","pdf")}}
        save(self.output/(name+".spec.json"),spec);self.items.append(spec);plt.close(fig)


def short_symbol(symbol):
    """去掉模板参数以保留函数名；数字序号可回查完整符号。"""
    depth=0
    parts=[]
    for char in symbol:
        if char=="<": depth+=1
        elif char==">" and depth: depth-=1
        elif not depth: parts.append(char)
    name="".join(parts).split("(")[0]
    return name if len(name)<=66 else "…"+name[-65:]


def label(run):
    c=run["case"]
    policy = " / "+c["heightPolicy"] if c["algorithm"]=="transactional" else ""
    return f'{c["terrain"]} / {LABELS[c["algorithm"]]}{policy} / {c["workers"]}线程'


def build_figures(data, output, analysis_path, statistics=None):
    from .process_statistics import aggregate
    from .comparison_figures import build as build_comparison_figures
    writer = FigureWriter(output, analysis_path)
    statistics = aggregate(data) if statistics is None else statistics
    build_comparison_figures(writer, data, statistics)
    w = writer
    for h in data["history"]:
        if h["schema"]=="sve-timing-v1":
            rows=h["rows"];fig,axes=plt.subplots(1,2,figsize=(11,4))
            for policy,style in [("fixed64","--"),("scaled","-")]:
                for mode in ("c8","dod8"):
                    points=sorted([r for r in rows if r["policy"]==policy and r["mode"]==mode],key=lambda r:int(r["budget"]))
                    if not points:continue
                    axes[0].plot([int(r["budget"])/1000 for r in points],[float(r["movingMs"]) for r in points],
                        marker="o",ls=style,label=policy+" / "+mode,color="#ce7130" if mode=="c8" else "#256eb0")
            for budget in (50000,100000,200000):
                points=sorted([r for r in rows if r["policy"]=="scaled" and int(r["budget"])==budget and r["mode"] in ("b1","c4","c8")],
                    key=lambda r:int(r["mode"][1:]))
                axes[1].plot([int(r["mode"][1:]) for r in points],[float(r["movingMs"]) for r in points],"o-",label=f"{budget//1000}k")
            axes[0].set_xlabel("预算上限 (千三角形)");axes[1].set_xlabel("请求线程数");axes[1].set_xticks([1,4,8])
            for ax in axes:ax.set_ylabel("历史移动机会均值 (ms)");ax.legend(fontsize=8)
            w.emit(fig,"historical-scaling","历史SVE：规模与线程成本",{"source":h["path"],"sha256":h["sha256"]},
                "历史Linux原型程序，固定/增长前缀分开；质量未等价，不能与本轮Windows暖时间合并")
        elif h["schema"]=="fpr-perf-v1":
            s=h["summary"];rows=s.get("functions",[])
            save(w.output/"functions.json",{"source":h["path"],"functions":rows,"screening":s.get("sample_screening_sufficient")})
            if not rows:continue
            chosen=rows[:12];fig,ax=plt.subplots(figsize=(12,6))
            labels=[f"#{i+1}  {short_symbol(r['function'])}" for i,r in enumerate(chosen)][::-1]
            ax.barh(range(len(chosen)),[r["selfPercent"] for r in chosen][::-1],color="#256eb0")
            ax.set_yticks(range(len(chosen)),labels,fontsize=7);ax.set_xlabel("ROI cpu-clock 事件权重占比 (%)")
            fig.subplots_adjust(left=.48)
            w.emit(fig,"functions","函数热点：self权重（样本不足时仅作接口检查）",{"source":h["path"],"roiSamples":s["roi_samples"],"top":12},
                f'暖ROI样本 {s["roi_samples"]}；全符号见functions.json。inclusive重叠，不与self或彼此相加；不是精确调用次数')
        elif h["schema"]=="fpr-tracy-v1":
            zones=h["zones"];frames=sorted([z for z in zones if z["name"]=="profile.frame"],key=lambda z:z["start"])
            if not frames:continue
            frame=frames[min(3,len(frames)-1)];chosen=[z for z in zones if z["start"]>=frame["start"] and z["end"]<=frame["end"] and
                z["name"] in ("gtp.set_view","gtp.dispatch","gtp.task","pool.wait","gtp.reservation","gtp.samples.prepare","gtp.commit.publish")]
            threads=sorted({z["thread"] for z in chosen});fig,ax=plt.subplots(figsize=(11,4))
            palette={"gtp.set_view":"#587391","gtp.dispatch":"#b6c9df","gtp.task":"#256eb0","pool.wait":"#c4c4c4","gtp.reservation":"#ce7130","gtp.samples.prepare":"#458369","gtp.commit.publish":"#935d92"}
            for z in chosen:
                ax.broken_barh([((z["start"]-frame["start"])/1e6,z["duration"]/1e6)],(threads.index(z["thread"])-.35,.7),facecolors=palette[z["name"]])
            ax.set_yticks(range(len(threads)),["trace thread "+t for t in threads]);ax.set_xlabel("相对本捕获窗口开始 (ms)")
            from matplotlib.patches import Patch
            ax.legend(handles=[Patch(color=c,label=n) for n,c in palette.items() if any(z["name"]==n for z in chosen)],fontsize=7,ncol=3,loc="upper center",bbox_to_anchor=(.5,1.15))
            w.emit(fig,"timeline","历史Tracy：同一捕获内的线程时序",{"source":h["path"],"frame":frame["text"],"clockOriginNs":frame["start"]},
                "FPR-03历史20k/4线程；空白未必CPU空闲，灰色wait是标记区间墙钟；不与新perf时钟拼接")
    save(w.output/"figure-index.json",w.items)
    return w.items
