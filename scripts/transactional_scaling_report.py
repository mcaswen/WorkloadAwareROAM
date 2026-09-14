"""汇总已完成的规模实验；只读取原始结果，不挑选较快重复或改变 Gate 分母。"""

import json
import math
from pathlib import Path
import statistics


def read(path):
    return json.loads(path.read_text())


def write_details(output, records, budgets, policies, figures):
    details=[]
    for policy in policies:
        for budget in budgets:
            folder=output/f"b{budget}"/("fixed64" if budget==20000 else policy)
            if not (folder/"b1/round-7/summary.json").exists():continue
            normal=[read(folder/f"b1/round-{i}/summary.json") for i in range(8)]
            diagnostic=[read(folder/f"diagnostic/round-{i}/summary.json") for i in range(8)]
            configuration=[read(folder/f"b1/round-{i}/configuration.json") for i in range(8)]
            trajectory=[json.loads(line) for line in (folder/"diagnostic/trajectory.jsonl").read_text().splitlines()]
            for a,b in zip(normal,diagnostic):
                if a["D_feasible"]!=b["D_feasible"]:raise ValueError("正常/完整审计的可行需求不同")
            row={"policy":policy,"budget":budget,"limit":configuration[0]["prefixLimit"],
                 "meanFaces":statistics.mean(s["faces"] for s in normal),
                 "q":configuration[0]["q"],"aMin":min(c["associations"] for c in configuration),
                 "aMax":max(c["associations"] for c in configuration),"faceSlots":max(c["faceSlots"] for c in configuration),
                 "vertexSlots":max(c["vertexSlots"] for c in configuration),"trajectory":trajectory}
            for name in ("D_raw","examined","receivers","D_need","D_feasible","D_executed","freeExecuted","batchWidth"):
                row[name]=sum(s[name] for s in normal)
            row["batchByFrame"]=[s["batchWidth"] for s in normal]
            row["work"]={key:sum(s["work"].get(key,0) for s in normal) for key in normal[0]["work"]}
            row["diagnosticPairChecks"]=sum(s["work"]["pairChecks"] for s in diagnostic)
            reasons={}
            for s in normal:
                for key,value in s["reasons"].items():reasons[key]=reasons.get(key,0)+value
            row["reasons"]=reasons
            row["stages"]={};row["lifecycle"]={}
            for mode in ("b1","c4","c8"):
                frames=[read(folder/f"{mode}/round-{i}/summary.json") for i in range(8)]
                names=set().union(*(set(s["seconds"]) for s in frames))
                row["stages"][mode]={key:statistics.mean(s["seconds"].get(key,0)*1000 for s in frames[3:]) for key in names}
                init=read(folder/mode/"initialize.json")
                life=read(folder/mode/"lifetime.json")
                row["lifecycle"][mode]={"initialization":init,"destruction":life,
                    "coldMs":1000*sum(init.get(k,0) for k in ("input","view_file","executor_initialize","state_initialize","sample_initialize","mesh_initialize"))}
            details.append(row)
    audit=read(output/"analysis/audit.json")
    quality=[]
    for policy in policies:
        for budget in budgets:
            for workers in (4,8):
                rows=[x for x in audit["quality"] if (x["policy"],x["budget"],x["workers"])==(policy,budget,workers)]
                if not rows:continue
                worst=max(rows,key=lambda r:r["DmaxSamplePx"])
                quality.append({"policy":policy,"budget":budget,"workers":workers,
                    "maxDmaxPx":worst["DmaxSamplePx"],"worstRound":worst["round"],
                    "oursMaxPx":max(x["ours"]["sampledScreenMaxPx"] for x in rows),
                    "dodMaxPx":max(x["dod"]["sampledScreenMaxPx"] for x in rows),
                    "oursRmsMaxPx":max(x["ours"]["terrainSampleScreenRms"] for x in rows),
                    "dodRmsMaxPx":max(x["dod"]["terrainSampleScreenRms"] for x in rows),
                    "oursHeightMax":max(x["ours"]["sampledHeightMax"] for x in rows),
                    "dodHeightMax":max(x["dod"]["sampledHeightMax"] for x in rows)})
    elasticity=[]
    for policy in policies:
        for mode in ("b1","c4","c8","dod4","dod8"):
            selected=sorted((r for r in records if r["policy"]==policy and r["mode"]==mode),key=lambda r:r["budget"])
            for a,b in zip(selected,selected[1:]):
                if b["meanFaces"]>a["meanFaces"]:
                    elasticity.append({"policy":policy,"mode":mode,"from":a["budget"],"to":b["budget"],
                        "beta":math.log(b["movingMs"]/a["movingMs"])/math.log(b["meanFaces"]/a["meanFaces"])})
    result={"workloads":details,"quality":quality,"elasticity":elasticity}
    (output/"analysis/details.json").write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n")
    if figures:
        draw_figures(Path(figures),records,details,quality,policies)
    return result


def draw_figures(destination,records,details,quality,policies):
    """静态论文式图只表达本轮观察，不拟合增长曲线或生成置信区间。"""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    destination.mkdir(parents=True,exist_ok=True)
    plt.rcParams.update({"font.size":10,"svg.fonttype":"none"})
    colors={"b1":"#697789","c4":"#2673b8","c8":"#e17c24","dod4":"#a14363","dod8":"#438357"}
    def rows(policy,mode):
        return sorted((r for r in records if r["policy"]==policy and r["mode"]==mode),key=lambda r:r["budget"])
    def finish(fig,name):
        fig.tight_layout();fig.savefig(destination/(name+".svg"));fig.savefig(destination/(name+".png"),dpi=150);plt.close(fig)
    def axis(ax):
        ax.set_xscale("log");ax.set_xticks([20,50,100,200],["20k","50k","100k","200k"])
        ax.xaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
        ax.set_xlabel("Actual active triangles (near budget cap)");ax.grid(alpha=.25)
    fig,axes=plt.subplots(1,2,figsize=(13,4.6))
    for ax,policy in zip(axes,policies):
        for mode in colors:
            values=rows(policy,mode);ax.plot([r["meanFaces"]/1000 for r in values],[r["movingMs"] for r in values],
                marker="o",color=colors[mode],linestyle="--" if mode.startswith("dod") else "-",label=mode.upper())
        ax.set_title(policy+": moving-view ready time");ax.set_ylabel("ms / frame (rounds 3-7)");axis(ax);ax.legend(ncol=2)
    finish(fig,"sve_timing")
    fig,axes=plt.subplots(1,3,figsize=(16,4.5))
    for policy in policies:
        for mode in ("c4","c8"):
            values=rows(policy,mode);style="--" if policy=="fixed64" else "-"
            for ax,key in zip(axes[:2],("Rmoving","Smoving")):
                ax.plot([r["meanFaces"]/1000 for r in values],[r[key] for r in values],marker="o",ls=style,color=colors[mode],label=policy+"/"+mode)
        values=rows(policy,"c8");axes[2].plot([r["meanFaces"]/1000 for r in values],[r["S4toThisMoving"] for r in values],marker="o",label=policy)
    for ax,title in zip(axes,("Cost ratio C(p) / DOD(p)","Same-batch speedup B1 / C(p)","Thread scaling C4 / C8")):
        ax.set_title(title);ax.axhline(1,color="black",lw=.8);axis(ax);ax.legend(fontsize=8)
    finish(fig,"sve_ratios")
    fig,axes=plt.subplots(2,3,figsize=(16,8))
    for r,policy in enumerate(policies):
        group=sorted((d for d in details if d["policy"]==policy),key=lambda d:d["budget"])
        for col,mode in enumerate(("b1","c4","c8")):
            ax=axes[r][col]
            for key,label in (("view_refresh","view"),("receiver_stage","receiver"),("donor_stage","donor"),("reservation","reservation"),("sample_repair","sample repair")):
                ax.plot([d["meanFaces"]/1000 for d in group],[d["stages"][mode].get(key,0) for d in group],marker="o",label=label)
            axis(ax);ax.set_title(policy+" / "+mode.upper());ax.set_ylabel("Moving mean stage ms");ax.legend(fontsize=8)
    finish(fig,"sve_stages")
    fig,axes=plt.subplots(2,2,figsize=(12,8))
    for policy in policies:
        group=sorted((d for d in details if d["policy"]==policy),key=lambda d:d["budget"])
        xs=[d["meanFaces"]/1000 for d in group]
        axes[0][0].plot(xs,[d["batchWidth"] for d in group],marker="o",label=policy)
        axes[0][1].plot(xs,[d["work"]["pairChecks"] for d in group],marker="o",label=policy)
        qs=sorted((q for q in quality if q["policy"]==policy and q["workers"]==8),key=lambda q:q["budget"])
        axes[1][0].plot(xs,[q["oursMaxPx"] for q in qs],marker="o",label=policy)
        axes[1][1].plot(xs,[q["maxDmaxPx"] for q in qs],marker="o",label=policy)
    base=sorted((q for q in quality if q["policy"]=="fixed64" and q["workers"]==8),key=lambda q:q["budget"])
    axes[1][0].plot([q["budget"]/1000 for q in base],[q["dodMaxPx"] for q in base],marker="o",ls="--",label="DOD8")
    for ax,title in zip(axes.flat,("Executed transactions / 8 frames","Normal pair checks / 8 frames","Max sampled screen error over trajectory (px)","Worst pointwise excess vs DOD8 (px)")):
        axis(ax);ax.set_title(title);ax.legend()
    finish(fig,"sve_work_quality")
