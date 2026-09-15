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

COLORS={"classic":"#587391","dod":"#256eb0","transactional":"#ce7130"}
LABELS={"classic":"Classic","dod":"DOD","transactional":"Transactional"}
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


def build_figures(data,output,analysis_path):
    w=FigureWriter(output,analysis_path)
    runs=[r for r in data["runs"] if r["mode"]=="timing" and r["status"]=="ok"]
    if runs:
        fig,ax=plt.subplots(figsize=(10,4.5))
        y=np.arange(len(runs))
        ax.barh(y,[r["groups"]["warm"]["cpuMs"] for r in runs],color=[COLORS[r["case"]["algorithm"]] for r in runs])
        ax.set_yticks(y,[label(r) for r in runs],fontsize=9);ax.set_xlabel("CPU-ready (ms / 更新机会)")
        for i,r in enumerate(runs):ax.text(r["groups"]["warm"]["cpuMs"],i,f'  {r["groups"]["warm"]["cpuMs"]:.3f}',va="center")
        fig.subplots_adjust(left=.4)
        w.emit(fig,"runtime","正常回放成本｜每配置一个独立进程",{"runs":[r["id"] for r in runs],"group":"warm"},
            "跨算法成本比较，非同质量加速；冷启动/预热另外保留，短轨迹不提供置信区间")
        fig,axes=plt.subplots(1,len(runs),figsize=(max(9,len(runs)*3.5),4),squeeze=False)
        for ax,r in zip(axes[0],runs):
            g=r["groups"]["warm"];keys=list(STAGES if r["case"]["algorithm"]=="transactional" else PASS)+["unattributedCpuMs"]
            if g["stageAdditivity"]=="do not stack":
                ax.text(.5,.5,"计时嵌套待审计",ha="center");continue
            selected=[k for k in keys if g[k] is not None and g[k]>0]
            ax.barh([STAGE_LABELS[k] for k in selected],[g[k] for k in selected],color=COLORS[r["case"]["algorithm"]])
            ax.set_title(r["case"]["terrain"]+" / "+LABELS[r["case"]["algorithm"]]+" / "+str(r["case"]["workers"])+"t",fontsize=10);ax.set_xlabel("ms / 暖机会")
        w.emit(fig,"stages","阶段成本与未归属部分",{"runs":[r["id"] for r in runs],"group":"warm","exclusiveKeys":[*STAGES,*PASS]},
            "父级计时不与子级重复累加；未归属保留；图形上传/等待不包含在这些CPU阶段中")
    tx=[r for r in runs if r["case"]["algorithm"]=="transactional"]
    if tx:
        fig,axes=plt.subplots(1,2,figsize=(11,4))
        unique_work={}
        for r in tx:
            signature=tuple(tuple(f[k] for k in ("frame","pairs","conflicts","exchanges","need")) for f in r["frames"])
            unique_work.setdefault(signature,r)
        for r in unique_work.values():
            f=r["frames"];x=[v["frame"] for v in f]
            axes[0].plot(x,[v["pairs"] for v in f],label="pair checks",color="#256eb0")
            axes[0].plot(x,[v["conflicts"] for v in f],label="conflict rejects",ls="--",color="#a94442")
            axes[1].plot(x,[v["exchanges"] for v in f],label="exchanges",marker="o",color="#ce7130")
            axes[1].plot(x,[v["need"] for v in f],label="D_need",ls="--",color="#587391")
        for ax in axes:ax.set_xlabel("更新机会");ax.legend();ax.set_ylabel("真实计数")
        w.emit(fig,"work","需求、配对与兑现",{"runs":[r["id"] for r in tx],"columns":["pairs","conflicts","need","exchanges"]},
            "零事务仍保留；相同逐帧工作曲线只绘一次，全部运行见analysis；不是收敛证明")
    if data["quality"]:
        fig,axes=plt.subplots(2,2,figsize=(11,7))
        keys=[("screenMax","sampled Emax (px)"),("heightMax","sampled Hmax (世界单位)"),
              ("terrainSampleRms","可见参数域等权 RMS (px)"),("faces","实际三角形数")]
        for q in data["quality"]:
            items=[i for i in q["frames"] if i.get("result") and i["status"]=="ok"]
            if not items:continue
            text=q["algorithm"]+" / "+q["heightPolicy"]+" / "+Path(q["run"]).name
            for ax,(key,ylabel) in zip(axes.flat,keys):
                ax.plot([i["frame"] for i in items],[i.get(key,i["result"].get(key)) for i in items],
                        marker="o",label=text,color=COLORS[q["algorithm"]])
                ax.set_xlabel("更新机会");ax.set_ylabel(ylabel)
        handles,labels=axes[0,0].get_legend_handles_labels()
        fig.legend(handles,labels,fontsize=7,loc="upper center",bbox_to_anchor=(.5,.945),ncol=3)
        w.emit(fig,"quality","独立双线性参考下的连续质量见证",{"quality":[q["path"] for q in data["quality"]],"sampling":"k=0"},
            "只连接实际已评价关键帧；连线不是中间质量保证。RMS不是屏幕面积权重；不同地形不混合判胜")
    if data.get("qualityPairs"):
        fig,ax=plt.subplots(figsize=(9,4))
        for pair in data["qualityPairs"]:
            rows=[r for r in pair["result"]["frames"] if r["status"]=="paired"]
            ax.plot([r["frame"] for r in rows],[r["DmaxSamplePx"] for r in rows],"o-",color="#ce7130",label="逐点Dmax")
        ax.axhline(0,color="#555",lw=.8);ax.set_xlabel("更新机会");ax.set_ylabel("max(e_new(q) − e_DOD(q)) (px)");ax.legend()
        w.emit(fig,"dmax","共同表示误差之外的局部退化",{"pairs":[p["path"] for p in data["qualityPairs"]]},
            "同一reference-visible Q逐点相减；可为负，不以两个全局最大值相减替代")
    # 质量运行与正常时间只能按相同任务/实际mesh身份关联
    scatter=[]
    for q in data["quality"]:
        source=next((r for r in data["runs"] if r["path"]==q["run"]),None)
        if not source:continue
        timing=next((r for r in runs if r["executionId"]==source["executionId"]),None)
        if timing:
            for item in q["frames"]:
                f=next((f for f in timing["frames"] if f["frame"]==item["frame"]),None)
                if f and item.get("result") and f["hash"]==item["meshHash"]:
                    scatter.append((timing,item,f))
    if scatter:
        fig,ax=plt.subplots(figsize=(9,4))
        for r,item,f in scatter:
            ax.scatter(f["cpuMs"],item["result"]["screenMax"],color=COLORS[r["case"]["algorithm"]],label=r["case"]["terrain"]+" / "+LABELS[r["case"]["algorithm"]],marker="s" if r["case"]["terrain"].startswith("generated") else "o")
        handles,labels=ax.get_legend_handles_labels();unique=dict(zip(labels,handles));ax.legend(unique.values(),unique.keys())
        ax.set_xlabel("相同机会的正常 CPU-ready (ms)");ax.set_ylabel("sampled Emax (px)")
        w.emit(fig,"quality-cost","质量与正常时间：同实际mesh关联",{"points":[[r["id"],i["frame"]] for r,i,f in scatter]},
            "不同语义的观测设计点，未作Pareto最优证明；质量评价和计时为不同进程")
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
