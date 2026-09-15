"""首轮正式实验的进程统计与展示；输入始终是已校验的 EIP analysis。"""
from __future__ import annotations
import csv
import html
import math
import statistics
from pathlib import Path
import numpy as np
import matplotlib.pyplot as plt
from PIL import Image
from .catalog import ROOT, load_json, content_hash
from .runner import save
from .analysis import STAGES, PASS, COUNTS
from .figures import FigureWriter, COLORS, STAGE_LABELS
from .formal_study import RAW, CONFIG

LABEL={"classic":"Classic 1t","dod":"DOD 8t","transactional":"事务 8t"}


def summarize(values):
    values=[v for v in values if v is not None]
    return dict(n=len(values),values=values,mean=statistics.mean(values) if values else None,
                minimum=min(values) if values else None,maximum=max(values) if values else None)


def percentile(values,p=.95):
    return sorted(values)[max(0,math.ceil(len(values)*p)-1)] if values else None


def aggregate():
    source=RAW/"analysis/analysis.json";data=load_json(source)
    protocol=load_json(CONFIG/"protocol.json");by={r["id"]:r for r in data["runs"]}
    result=dict(schemaVersion="fer-01-analysis-v1",analysisSha256=content_hash(source),
        protocolSha256=content_hash(CONFIG/"protocol.json"),cases=[],comparisons=[],
        uncertainty="three independent processes; min/max are observed range, not CI",
        missingRuns=[],modeChecks=load_json(RAW/"mode-and-repeat-checks.json"))
    per_process=[]
    for entry in protocol["cases"]:
        runs=[by.get(f"r{i}-{entry['id']}") for i in (1,2,3)]
        ok=[r for r in runs if r and r["status"]=="ok"]
        result["missingRuns"] += [f"r{i+1}-{entry['id']}" for i,r in enumerate(runs) if not r or r["status"]!="ok"]
        item=dict(entry,groups={},complete=len(ok)==3,flips=[],processes=[r["id"] for r in ok])
        for group in ("warm","moving","return-recovery","stationary","cold","warmup","all"):
            g=[r["groups"][group] for r in ok if group in r["groups"]]
            item["groups"][group]={k:summarize([v[k] for v in g]) for k in
                ("cpuMs","frameMs","uploadMs","uploadBytes","faces","utilization","unattributedCpuMs",*STAGES,*PASS)}
            item["groups"][group]["frameCountPerProcess"]=[v["count"] for v in g]
        for run in ok:
            frames=run["frames"];warm=[f for f in frames if not f["warmup"]]
            flip_file=Path(run["path"])/"run/flip-recovery.csv"
            flips=list(csv.DictReader(flip_file.open()))
            item["flips"].append({k:sum(int(v[k]) for v in flips) for k in ("triggered","attempts","certified","conflicts","executed")})
            row=dict(run=run["id"],case=entry["id"],warmCpuMs=statistics.mean(f["cpuMs"] for f in warm),
                warmP95Ms=percentile([f["cpuMs"] for f in warm]),warmMaxMs=max(f["cpuMs"] for f in warm),
                coldMs=frames[0]["cpuMs"],initialFaces=frames[0]["faces"],finalFaces=frames[-1]["faces"],
                minFaces=min(f["faces"] for f in frames),maxFaces=max(f["faces"] for f in frames),
                meanUploadMs=statistics.mean(f["uploadMs"] for f in warm),
                **run["totals"],flipExecuted=item["flips"][-1]["executed"])
            per_process.append(row)
        if ok:
            item["totals"]=ok[0]["totals"]
            item["initialFaces"]=ok[0]["frames"][0]["faces"]
            item["finalFaces"]=ok[0]["frames"][-1]["faces"]
            item["frameSeries"]=ok[0]["frames"]
            item["sameResultsAcrossRepeats"]=all(
                [(f["hash"],*(f[k] for k in COUNTS)) for f in r["frames"]]==
                [(f["hash"],*(f[k] for k in COUNTS)) for f in ok[0]["frames"]] for r in ok)
        q=next((q for q in data["quality"] if Path(q["run"]).name=="visual-"+entry["id"]),None)
        item["quality"]=q["frames"] if q else []
        result["cases"].append(item)
    for item in result["cases"]:
        if item["algorithm"]!="transactional" or item["workers"]!=8:continue
        for reference in ("dod-t8","transactional-t1"):
            ref=next((r for r in result["cases"] if r["id"]==f"{item['terrain']}-b{item['budget']}-{reference}"),None)
            if not ref:continue
            row=dict(candidate=item["id"],reference=ref["id"],sameTask=reference=="transactional-t1")
            for g in ("warm","moving","return-recovery"):
                a=item["groups"][g]["cpuMs"]["values"];b=ref["groups"][g]["cpuMs"]["values"]
                row[g]=summarize([x/y for x,y in zip(b,a)] if len(a)==len(b)==3 else [])
            row["ratioMeaning"]="reference / candidate; >1 means candidate lower cost"
            row["meshAndCountersEqual"]=item.get("frameSeries") is not None and ref.get("frameSeries") is not None and len(item["frameSeries"])==len(ref["frameSeries"]) and all(
                a["hash"]==b["hash"] and all(a[k]==b[k] for k in COUNTS)
                for a,b in zip(item["frameSeries"],ref["frameSeries"]))
            result["comparisons"].append(row)
    result["qualityPairs"]=[p["result"] for p in data["qualityPairs"]]
    for item in result["cases"]:
        if item["algorithm"]!="transactional" or item["workers"]!=8:continue
        ref=next(r for r in result["cases"] if r["id"]==f"{item['terrain']}-b{item['budget']}-dod-t8")
        qref={f["frame"]:f for f in ref["quality"] if f.get("result")}
        excess=[q["result"]["screenMax"]-qref[q["frame"]]["result"]["screenMax"] for q in item["quality"]
                if q.get("result") and q["frame"] in qref and q["status"]=="ok"]
        pair=next((p for p in result["qualityPairs"] if Path(p["candidate"]).name=="quality-"+item["id"]),{})
        deltas=[f["DmaxSamplePx"] for f in pair.get("frames",[]) if f["status"]=="paired"]
        item["qualityBudgetCoverage"]=[dict(budgetPx=b,emaxCount=sum(x<=b+1e-9 for x in excess),
            dmaxCount=sum(x<=b+1e-9 for x in deltas),evaluated=len(excess),paired=len(deltas)) for b in protocol["qualityBudgetsPx"]]
    save(RAW/"formal-analysis.json",result)
    with (RAW/"process-statistics.csv").open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=list(per_process[0]));w.writeheader();w.writerows(per_process)
    return result,data


def fmt(value):return "未测" if value is None else f"{value:.3f}"
def short(item):return f"{item['terrain']} / {item['budget']//1000}k"


def figures(result,data,output):
    writer=FigureWriter(output,RAW/"formal-analysis.json")
    cases=result["cases"];primary=[c for c in cases if c["budget"]==50000 and (c["workers"]==8 or c["algorithm"]=="classic")]
    terrains=list(dict.fromkeys(c["terrain"] for c in primary))
    fig,ax=plt.subplots(figsize=(12,5));x=np.arange(len(terrains));width=.25
    for j,algorithm in enumerate(("classic","dod","transactional")):
        rows=[next(c for c in primary if c["terrain"]==t and c["algorithm"]==algorithm) for t in terrains]
        means=[c["groups"]["warm"]["cpuMs"]["mean"] for c in rows]
        error=np.array([[m-c["groups"]["warm"]["cpuMs"]["minimum"] for m,c in zip(means,rows)],
                        [c["groups"]["warm"]["cpuMs"]["maximum"]-m for m,c in zip(means,rows)]])
        ax.bar(x+(j-1)*width,means,width,label=LABEL[algorithm],color=COLORS[algorithm],yerr=error,capsize=3)
    ax.set(xticks=x,xticklabels=terrains,ylabel="暖 CPU-ready / ms",yscale="log");ax.legend()
    writer.emit(fig,"runtime","50k预算：三个独立进程的平均成本",{"cases":[c["id"] for c in primary]},
        "对数纵轴；误差线为进程均值的实际最小/最大，不是置信区间；不同算法质量另列")
    fig,axes=plt.subplots(1,2,figsize=(12,4.8))
    for algorithm in ("classic","dod","transactional"):
        rows=sorted([c for c in cases if c["terrain"]=="peking547" and c["algorithm"]==algorithm and (c["workers"]==8 or algorithm=="classic")],key=lambda c:c["budget"])
        for ax,g in zip(axes,("warm","moving")):
            ax.plot([c["budget"]/1000 for c in rows],[c["groups"][g]["cpuMs"]["mean"] for c in rows],"o-",label=LABEL[algorithm],color=COLORS[algorithm])
            ax.set(xlabel="预算上限 / 千三角形",ylabel="CPU-ready / ms",title="全部暖机会" if g=="warm" else "移动机会");ax.legend()
    writer.emit(fig,"scaling","Peking：相同冻结路线的预算规模对照",{"terrain":"peking547","budgets":[50000,100000,200000]},
        "增长前缀160/320/640；预算与前缀同时变化，不能当作单变量n复杂度拟合")
    tx=[c for c in cases if c["algorithm"]=="transactional" and c["workers"]==8]
    fig,axes=plt.subplots(2,3,figsize=(15,8))
    for ax,c in zip(axes.flat,tx):
        g=c["groups"]["warm"]
        keys=[k for k in STAGES if k not in ("seedMs","initializeMs") and (g[k]["mean"] or 0)>0]+["unattributedCpuMs"]
        ax.barh([STAGE_LABELS[k] for k in keys],[g[k]["mean"] or 0 for k in keys],color=COLORS['transactional'])
        ax.set(title=short(c),xlabel="ms / 暖机会")
    writer.emit(fig,"stages","事务8线程：完整阶段成本",{"cases":[c["id"] for c in tx]},
        "互斥阶段来自公开计时；保留未归属项，上传在CPU-ready之外，不叠加任务墙钟和父级阶段")
    fig,axes=plt.subplots(2,3,figsize=(15,7))
    for ax,c in zip(axes.flat,tx):
        group=[v for v in cases if v["terrain"]==c["terrain"] and v["budget"]==c["budget"] and (v["workers"]==8 or v["algorithm"]=="classic")]
        for v in group:
            frames=v["frameSeries"][3:]
            ax.plot([f["frame"] for f in frames],[f["cpuMs"] for f in frames],label=LABEL[v["algorithm"]],color=COLORS[v["algorithm"]])
        ax.set(title=short(c),xlabel="更新机会",ylabel="CPU-ready / ms");ax.legend(fontsize=7)
    writer.emit(fig,"timeline","冻结轨迹的逐机会成本",{"repeat":1},
        "展示预指定第1重复；所有重复在CSV中。这里是阶段/机会时序，不是新的perf/Tracy函数采样")
    fig,axes=plt.subplots(2,3,figsize=(15,7))
    for ax,c in zip(axes.flat,tx):
        for v in [v for v in cases if v["terrain"]==c["terrain"] and v["budget"]==c["budget"] and v["quality"]]:
            q=[q for q in v["quality"] if q.get("result") and q["status"]=="ok"]
            ax.plot([q['frame'] for q in q],[q['result']['screenMax'] for q in q],"o-",label=LABEL[v['algorithm']],color=COLORS[v['algorithm']])
        ax.set(title=short(c),xlabel="已评价关键帧",ylabel="sampled Emax / px");ax.legend(fontsize=7)
    writer.emit(fig,"quality","独立双线性参考下的最大屏幕误差",{"frames":"pre-registered four per case"},
        "线段只连接已评价帧，不保证中间误差；不同地形各用自己的纵轴，完整RMS/Hmax/Dmax见表")
    fig,axes=plt.subplots(2,3,figsize=(15,7))
    for ax,c in zip(axes.flat,tx):
        for v in [v for v in cases if v['terrain']==c['terrain'] and v['budget']==c['budget'] and v['quality']]:
            observed=[q['result']['screenMax'] for q in v['quality'] if q.get('result') and q['status']=='ok']
            if not observed:continue
            x=v['groups']['warm']['cpuMs'];y=max(observed)
            ax.errorbar(x['mean'],y,xerr=[[x['mean']-x['minimum']],[x['maximum']-x['mean']]],
                fmt='o',color=COLORS[v['algorithm']],label=LABEL[v['algorithm']],capsize=3)
            offset={'classic':(6,16),'dod':(6,-15),'transactional':(5,6)}[v['algorithm']]
            ax.annotate(LABEL[v['algorithm']],(x['mean'],y),xytext=offset,textcoords='offset points',fontsize=7)
        ax.set(title=short(c),xlabel='暖CPU进程均值 / ms',ylabel='四关键帧最大Emax / px')
        ax.margins(.2)
    writer.emit(fig,'quality-cost','成本与质量的观测设计点',{'frames':'same pre-registered four per case'},
        '各子图坐标范围独立；越左/越下越好。横线为进程均值范围，非CI；不是完整Pareto最优证明')
    fig,axes=plt.subplots(2,3,figsize=(15,7))
    for ax,c in zip(axes.flat,tx):
        f=c['frameSeries'];ax.plot([v['frame'] for v in f],[v['pairs'] for v in f],label='配对检查',color='#256eb0')
        ax.plot([v['frame'] for v in f],[v['conflicts'] for v in f],label='冲突拒绝',color='#a94442')
        ax.set(title=short(c),xlabel="更新机会",ylabel="次数");ax.legend(fontsize=8)
    writer.emit(fig,"work","配对与冲突的真实工作量",{"cases":[c['id'] for c in tx]},
        "第1重复；已核对全部重复结果。零交换不等于无工作，净零翻边计数另外保留")
    for c in tx:
        group=[v for v in cases if v['terrain']==c['terrain'] and v['budget']==c['budget'] and v['quality']]
        frames=c['qualityFrames'];fig,axes=plt.subplots(3,4,figsize=(16,9))
        for row,v in zip(axes,group):
            for ax,frame in zip(row,frames):
                path=RAW/('visual-'+v['id'])/'run'/f'frame-{frame}.png'
                if path.exists():
                    with Image.open(path) as image:ax.imshow(image)
                q=next((q for q in v['quality'] if q['frame']==frame),{})
                ax.set_title(f"{LABEL[v['algorithm']]} / f{frame} / E={fmt(q.get('result',{}).get('screenMax'))}",fontsize=8);ax.axis('off')
        writer.emit(fig,'actual-'+c['terrain']+'-'+str(c['budget']),short(c)+'｜真实OpenGL关键帧',
            {'cases':[v['id'] for v in group],'frames':frames},
            '相同中性材质/相机/视口；原始1280×720保留。缩略图用于对照，不替代数值质量和原像素检查')
    for c in tx:
        valid=[q for q in c['quality'] if q.get('result') and q['status']=='ok']
        if not valid:continue
        q=max(valid,key=lambda q:q['result']['screenMax']);frame=q['frame']
        locations=RAW/('quality-'+c['id'])/q['locations']
        with locations.open() as stream:
            witness=next((r for r in csv.DictReader(stream) if r['role']=='screen-witness'),None)
        if witness is None:continue
        x,y=float(witness['pixelX']),float(witness['pixelY'])
        left=min(max(0,int(x)-100),1080);top=min(max(0,int(y)-100),520)
        ref=f"{c['terrain']}-b{c['budget']}-dod-t8"
        fig,axes=plt.subplots(1,3,figsize=(12,4.8))
        for ax,ident,label in zip(axes[:2],(c['id'],ref),('事务：同一见证','DOD：同一像素区域')):
            with Image.open(RAW/('visual-'+ident)/'run'/f'frame-{frame}.png') as image:ax.imshow(image)
            ax.set_xlim(left,left+200);ax.set_ylim(top+200,top);ax.scatter([x],[y],s=160,facecolors='none',edgecolors='red')
            ax.set_title(label);ax.axis('off')
        with Image.open(RAW/('visual-'+c['id'])/'run'/f'frame-{frame}.png') as image:axes[2].imshow(image)
        axes[2].scatter([x],[y],s=120,facecolors='none',edgecolors='red');axes[2].axis('off');axes[2].set_title('事务完整画面 / 参考投影位置')
        writer.emit(fig,'witness-'+c['terrain']+'-'+str(c['budget']),short(c)+f"｜f{frame} / Emax={q['result']['screenMax']:.4f}px",
            {'case':c['id'],'frame':frame,'locations':str(locations),'witness':witness,'crop':[left,top,200,200]},
            '仅从四个已评价关键帧选最大者展示；两侧同一参考投影裁剪，不新增采样，不以画面相似推断误差相同')
    save(output/'figure-index.json',writer.items)
    return writer.items


def tables(result,output):
    rows=[]
    for c in result['cases']:
        for q in c['quality']:
            r=q.get('result',{})
            rows.append(dict(case=c['id'],frame=q['frame'],status=q['status'],faces=q.get('faces'),
                Emax=r.get('screenMax'),RMS=r.get('terrainSampleRms'),Hmax=r.get('heightMax'),
                Q=r.get('q'),missing=r.get('missing'),ambiguous=r.get('ambiguous'),
                screenWitness=str(r.get('screenWitness'))))
    with (output/'quality.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    return rows


def appendix(result,output,specs,quality_rows):
    """正式数表与画面浏览；不复用开发验收报告的单进程判词。"""
    import shutil
    sections=[];markdown=['# FER-01 正式实验数据附录','',
        '三个独立正常进程；进程均值的范围不是置信区间。时间与质量从同一份formal-analysis归约。','']
    def table(title,head,rows):
        markdown.extend(['## '+title,'','| '+' | '.join(head)+' |','|'+'|'.join(['---']*len(head))+'|'])
        markdown.extend('| '+' | '.join(str(v) for v in row)+' |' for row in rows);markdown.append('')
        sections.append('<h2>'+html.escape(title)+'</h2><div class="scroll"><table><thead><tr>'+''.join('<th>'+html.escape(x)+'</th>' for x in head)+
            '</tr></thead><tbody>'+''.join('<tr>'+''.join('<td>'+html.escape(str(v))+'</td>' for v in row)+'</tr>' for row in rows)+'</tbody></table></div>')
    rows=[]
    for c in result['cases']:
        g=c['groups']['warm'];v=g['cpuMs']
        rows.append([c['id'],v['n'],fmt(v['mean']),fmt(v['minimum']),fmt(v['maximum']),
            '/'.join(fmt(x) for x in v['values']),fmt(g['frameMs']['mean']),fmt(g['uploadMs']['mean']),
            c.get('initialFaces'),c.get('finalFaces'),fmt(g['utilization']['mean'])])
    table('正常成本与实际预算利用率',['配置','进程n','CPU均值ms','最小','最大','三个进程均值','帧包络ms','上传ms','初始N','最终N','平均N/B'],rows)
    rows=[]
    for c in result['cases']:
        rows.append([c['id']]+[fmt(c['groups'][g]['cpuMs']['mean']) for g in ('cold','warmup','moving','return-recovery','stationary')])
    table('冷启动和路线阶段',['配置','冷启动ms','预热ms','移动ms','返回/恢复ms','静止ms'],rows)
    table('同编号重复的成本比',['参考/新算法','相同任务','结果一致','暖均值','暖范围','移动均值','恢复均值'],
        [[c['reference']+' / '+c['candidate'],c['sameTask'],c['meshAndCountersEqual'],fmt(c['warm']['mean']),
          fmt(c['warm']['minimum'])+'–'+fmt(c['warm']['maximum']),fmt(c['moving']['mean']),fmt(c['return-recovery']['mean'])] for c in result['comparisons']])
    tx=[c for c in result['cases'] if c['algorithm']=='transactional' and c['workers']==8]
    table('事务工作量（结果一致时列第1重复）',['配置','总需求','总可行','预算交换','空额度细分','净零翻边','配对检查','冲突拒绝','样本求值'],
        [[c['id'],c['totals']['need'],c['totals']['feasible'],c['totals']['exchanges'],c['totals']['free'],
          c['flips'][0]['executed'],c['totals']['pairs'],c['totals']['conflicts'],c['totals']['evaluations']] for c in tx])
    table('暖路径分阶段成本',['配置','阶段','均值ms','占CPU比例%'],
        [[c['id'],STAGE_LABELS[k],fmt(c['groups']['warm'][k]['mean']),
          fmt(100*c['groups']['warm'][k]['mean']/c['groups']['warm']['cpuMs']['mean'])]
         for c in tx for k in (*STAGES,'unattributedCpuMs') if c['groups']['warm'][k]['mean'] is not None])
    table('独立关键帧质量',['配置','帧','状态','N','Emax px','RMS px','Hmax','Q','缺失','歧义','见证'],
        [[q[k] if k not in ('Emax','RMS','Hmax') else fmt(q[k]) for k in ('case','frame','status','faces','Emax','RMS','Hmax','Q','missing','ambiguous','screenWitness')] for q in quality_rows])
    table('相对DOD逐点超额',['配置','帧','状态','Dmax px','见证序号'],
        [[Path(p['candidate']).name,q['frame'],q['status'],fmt(q.get('DmaxSamplePx')),q.get('witnessOrdinal')] for p in result['qualityPairs'] for q in p['frames']])
    table('预注册质量预算覆盖（四个观测帧）',['配置','允许超额px','Emax满足/评价','Dmax满足/配对'],
        [[c['id'],b['budgetPx'],f"{b['emaxCount']}/{b['evaluated']}",f"{b['dmaxCount']}/{b['paired']}"] for c in tx for b in c['qualityBudgetCoverage']])
    gallery=[]
    for spec in specs:
        name='figures/'+spec['id']
        markdown.extend(['!['+spec['title']+']('+name+'.png)',spec['note'],''])
        gallery.append(f'<figure><a href="{name}.png"><img loading="lazy" src="{name}.png"></a><figcaption>{html.escape(spec["note"])} · <a href="{name}.svg">SVG</a> · <a href="{name}.pdf">PDF</a></figcaption></figure>')
    players=[];media=output/'media';media.mkdir()
    for c in result['cases']:
        if not c['visual']:continue
        folder=RAW/('visual-'+c['id'])/'run'
        frames=list(csv.DictReader((folder/'frames.csv').open()))
        imgs=[f for f in frames if f['image']]
        if not imgs:continue
        options=[]
        for f in imgs:
            source=folder/Path(f['image']).with_suffix('.png');name=c['id']+'-'+source.name
            shutil.copy2(source,media/name)
            options.append(f'<option value="media/{name}">frame {f["frame"]} · {html.escape(f["event"])} · N={f["faces"]}</option>')
        players.append('<article><h3>'+html.escape(c['id'])+'</h3><select aria-label="实际关键帧" onchange="this.nextElementSibling.src=this.value">'+''.join(options)+
            '</select><img src="'+options[0].split('value="')[1].split('"')[0]+'" alt="实际OpenGL画面"></article>')
    css="body{font:16px/1.65 system-ui,'Microsoft YaHei',sans-serif;background:#f2f5f7;color:#243440;margin:auto;max-width:1440px;padding:30px}h1,h2{color:#173e59}h2{margin-top:42px}.notice{background:#fff1cf;padding:20px;border-left:5px solid #b97a22}.scroll{overflow:auto;max-height:640px;background:white}table{border-collapse:collapse;font-size:12px;white-space:nowrap}th,td{padding:8px;border:1px solid #d6e0e7}th{background:#e0eaf1;position:sticky;top:0}figure,article{background:white;padding:16px;margin:24px 0}img{width:100%;height:auto}figcaption{font-size:14px;color:#526878}select{padding:8px;margin-bottom:12px}a{color:#166598}"
    page='<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>FER-01 正式实验数据与视觉附录</title><style>'+css+'</style><h1>FER-01 首轮正式实验</h1><div class="notice">有限预注册矩阵，三个独立进程。跨算法是成本与质量设计点，相同任务且结果一致的1→8线程才是并行对照。质量只评价预指定四帧，不代表全轨迹或连续曲面保证。</div><p><a href="data-report.md">完整数表Markdown</a> · <a href="quality.csv">质量CSV</a> · <a href="../process-statistics.csv">全部进程统计</a> · <a href="../formal-analysis.json">同源分析</a> · <a href="../analysis/analysis.json">原始EIP归约</a></p>'+''.join(sections)+''.join(gallery)+'<h2>实际渲染关键帧</h2><p>可切换原始1280×720截图；不表示实时播放速度，也不以纹理观感替代独立几何误差。</p>'+''.join(players)+'</html>'
    (output/'index.html').write_text(page,encoding='utf-8')
    (output/'data-report.md').write_text('\n'.join(markdown),encoding='utf-8')


def build():
    result,data=aggregate();output=RAW/'report';output.mkdir(exist_ok=False)
    specs=figures(result,data,output/'figures');quality_rows=tables(result,output)
    appendix(result,output,specs,quality_rows)
    save(output/'report-evidence.json',dict(analysisSha256=content_hash(RAW/'formal-analysis.json'),
        figures=specs,visualReview='pending actual inspection',userVisualReview='pending'))
    return output


if __name__=='__main__':print(build())
