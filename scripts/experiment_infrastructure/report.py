"""可离线浏览的中文实验报告，正文和图表只引用唯一analysis。"""
from __future__ import annotations
import csv
import html
import json
import os
import time
from pathlib import Path
from .catalog import ROOT,load_json,content_hash
from .runner import save
from .figures import build_figures
from .visual_report import build_visuals
from .process_statistics import aggregate


def escape(value): return html.escape(str(value))
def numeric(value): return "未测/不适用" if value is None else f"{value:.4f}"
def link(path,output):
    return escape(os.path.relpath(Path(path).resolve(),output.resolve()).replace(os.sep,"/"))


def build(analysis_path,output):
    started=time.perf_counter();analysis_path=Path(analysis_path).resolve();output=Path(output).resolve()
    output.mkdir(parents=True,exist_ok=False)
    data=load_json(analysis_path)
    if data.get("schemaVersion")!="eip-analysis-v1": raise ValueError("未知analysis版本")
    statistics = aggregate(data)
    save(output / "process-statistics.json", statistics)
    figures=build_figures(data,output/"figures",analysis_path,statistics)
    players,visual_figures=build_visuals(data,output,analysis_path)
    save(output/"analysis.json",data)
    for name in ("processes", "quality", "failures", "numberMatches"):
        records = statistics[name]
        with (output / (name + ".csv")).open("w", newline="", encoding="utf-8") as stream:
            if records:
                writer = csv.DictWriter(stream, fieldnames=list(records[0]))
                writer.writeheader()
                writer.writerows(records)
    rows = []
    for config in statistics["configurations"]:
        case = config["case"]
        group = config["groups"]["warm"]
        cpu = group["cpuMs"]
        compute = group["gpuComputeMs"]
        draw = group["gpuDrawMs"]
        rows.append([
            case["id"], case["algorithm"], config["processCount"], config["cpuTimeMeaning"],
            numeric(cpu["mean"]), "/".join(numeric(value) for value in cpu["values"]),
            numeric(group["frameMs"]["mean"]), numeric(group["uploadMs"]["mean"]),
            numeric(compute["mean"]), numeric(draw["mean"]), numeric(group["utilization"]["mean"]),
            case.get("cbtArea", "不适用"), case.get("cbtCapacity", "不适用"),
        ])
    headings = ["配置", "算法", "正常进程n", "CPU边界", "CPU均值ms", "各进程CPU均值",
                "帧包络ms", "上传ms", "GPU计算ms", "GPU绘制ms", "CPU平均N/B", "CBT面积", "CBT容量"]
    study = data.get("study")
    scope = ("本报告使用预冻结正式协议，独立进程为统计单位；所有原值及缺失重复分别保留。"
             if study else "本报告描述所提供的独立进程；没有协议的重复次数不构造统计显著性。")
    scope += " 进程均值范围不是置信区间。CPU更新、GPU主机录制和GPU时间戳具有不同边界。"
    def table(head,rows):
        return "<div class='table-scroll'><table><thead><tr>"+"".join("<th>"+escape(x)+"</th>" for x in head)+"</tr></thead><tbody>"+            "".join("<tr>"+"".join("<td>"+escape(x)+"</td>" for x in row)+"</tr>" for row in rows)+"</tbody></table></div>"
    gallery=[]
    for directory,items in [("figures",figures),("visual-figures",visual_figures)]:
        for spec in items:
            stem=directory+"/"+spec["id"]
            gallery.append(f"<figure id='figure-{escape(spec['id'])}'><a href='{stem}.png'><img loading='lazy' src='{stem}.png' alt='{escape(spec['title'])}'></a><figcaption>{escape(spec['note'])} · <a href='{stem}.svg'>SVG</a> · <a href='{stem}.pdf'>PDF</a> · <a href='{stem}.spec.json'>FigureSpec</a></figcaption></figure>")
    costs=load_json(ROOT/"configs/experiments/analysis/function_costs.json")["entries"]
    functions=[]
    for h in data["history"]:
        if h["schema"]!="fpr-perf-v1": continue
        for i,row in enumerate(h["summary"].get("functions",[])):
            annotation=next((a for a in costs if a["match"] in row["function"]),None)
            source=annotation["source"] if annotation else ""
            valid=bool(annotation and content_hash(ROOT/source)==annotation["sourceSha256"])
            functions.append([i+1,row["function"],numeric(row["selfPercent"]),numeric(row["inclusivePercent"]),row["selfSamples"],
                annotation["stage"] if valid else "未单独审计",
                annotation["cost"] if valid else "不由采样符号猜测复杂度；库/内联调用应结合调用栈",
                source if valid else ""])
    with (output/"functions.csv").open("x",newline="") as f:
        writer=csv.writer(f);writer.writerow(["rank","function","selfPercent","inclusivePercent","selfSamples","stage","cost","source"]);writer.writerows(functions)
    function_note=("此次CPU暖ROI样本不足2,000，仅验证采集链路。" if functions
        else "本报告没有函数采样；函数/历史时序示例见EIP-07报告，不能为本轮自动补齐归因。")
    quality_rows = []
    for observation in statistics["quality"]:
        quality_rows.append([
            observation["case"], observation["frame"], observation["status"], observation["N"],
            numeric(observation["Emax"]), numeric(observation["Hmax"]), numeric(observation["RMS"]),
            observation["Q"], observation["missing"], observation["ambiguous"], observation["witness"],
        ])
    pair_rows = []
    for pair in data["qualityPairs"]:
        result = pair["result"]
        for frame in result.get("frames", []):
            pair_rows.append([
                Path(result["candidate"]).name, Path(result["reference"]).name,
                frame["frame"], frame["status"], frame.get("candidateFaces"), frame.get("referenceFaces"),
                numeric(frame.get("DmaxSamplePx")), frame.get("witnessOrdinal"),
            ])
    coverage_rows = []
    for config in statistics["configurations"]:
        if config["device"] != "gpu":
            continue
        for position, coverage in enumerate(config["groups"]["warm"]["gpuCoverage"]):
            coverage_rows.append([
                config["case"]["id"], position + 1, coverage["sourceOpportunities"],
                coverage["compute"]["sampleCount"], coverage["compute"]["unobserved"],
                coverage["draw"]["sampleCount"], coverage["draw"]["unobserved"],
            ])
    match_rows = [[row["case"], row["frame"], row["N"], row["reference"], row["referenceN"],
                   numeric(row["relativeCountDifference"]), row["status"]]
                  for row in statistics["numberMatches"]]
    failure_rows = [[item["run"], item["mode"], item["status"]] for item in statistics["failures"]]
    for item in (study or {}).get("runCompleteness", []):
        if item["status"] != "ok" and not any(row[0] == item["runId"] for row in failure_rows):
            failure_rows.append([item["runId"], "registered", item["status"]])
    player_html=[]
    for i,p in enumerate(players):
        if not p["frames"]: continue
        player_html.append(f"<article class='player' data-player='{i}'><h3>{escape(p['title'])}</h3><p>{escape(p['backend'])} · {escape(p['material'])} · 关键帧按冻结名义时间播放，非实测FPS</p><img src='{p['frames'][0]['image']}' alt='实际平台帧'><div class='controls'><button class='play'>播放</button><button class='prev'>上一帧</button><button class='next'>下一帧</button><input class='slider' aria-label='关键帧' type='range' min='0' max='{len(p['frames'])-1}' value='0'></div><p class='caption'></p></article>")
    links="<ul>"+"".join(f"<li>{escape(r['id'])}：<a href='{link(Path(r['path'])/'manifest.json',output)}'>manifest</a> · <a href='{link(Path(r['path'])/'run/frames.csv',output)}'>原始帧表</a> · <a href='{link(Path(r['path'])/'sources.zip',output)}'>源码快照</a></li>" for r in data["runs"])+"</ul>"
    css="""*{box-sizing:border-box}body{margin:0;background:#f2f5f7;color:#1f2c35;font:16px/1.65 system-ui,'Microsoft YaHei',sans-serif}main{max-width:1320px;margin:auto;padding:32px 24px 90px}header{background:#183345;color:white;padding:32px;border-radius:14px}h1{font-size:30px;line-height:1.3;margin:0 0 14px}h2{margin-top:48px;border-bottom:2px solid #d5e0e8;padding-bottom:10px}h3{font-size:19px}a{color:#1c6799}header a{color:#bfe2f8}.meta{font:13px/1.6 ui-monospace,monospace;overflow-wrap:anywhere}.notice{background:#fff4d8;border-left:5px solid #cb8423;padding:18px 24px;margin:24px 0}.badges{display:flex;flex-wrap:wrap;gap:9px}.badges span{padding:6px 12px;border-radius:20px;background:#e5f1ef;color:#28564f}.badges .open{background:#fff1d0;color:#895c1e}.table-scroll{overflow:auto;background:white;border:1px solid #dbe2e7;border-radius:8px;max-height:660px}table{border-collapse:collapse;font-size:13px;min-width:100%;white-space:nowrap}th{background:#e5edf2;position:sticky;top:0;text-align:left}th,td{padding:9px 12px;border-bottom:1px solid #e1e7eb}tr:nth-child(even){background:#f7fafb}.functions td:nth-child(2){white-space:normal;min-width:380px;max-width:650px;overflow-wrap:anywhere}.functions td:nth-child(7){white-space:normal;min-width:250px}figure{margin:26px 0;padding:12px;background:white;border:1px solid #dbe2e7;border-radius:10px}figure img{width:100%;height:auto}figcaption{padding:8px 12px;font-size:14px;color:#536773}.player{background:white;border:1px solid #dbe2e7;padding:22px;border-radius:12px;margin:22px 0}.player img{width:100%;max-height:650px;object-fit:contain;background:#090c0f}.controls{display:flex;gap:10px;padding:10px 0}.slider{flex:1;min-width:70px}button{border:1px solid #86a6bc;background:#eef6fb;color:#204f70;padding:8px 14px;border-radius:6px;cursor:pointer}.caption{font-size:13px;overflow-wrap:anywhere}nav{display:flex;gap:16px;flex-wrap:wrap}details{margin:20px 0}summary{cursor:pointer;font-weight:600}@media(max-width:650px){main{padding:12px}header{padding:24px}h1{font-size:25px}.controls{flex-wrap:wrap}.slider{width:100%}}"""
    js="""const players=PLAYER_DATA;
document.querySelectorAll('.player').forEach(el=>{
 const p=players[Number(el.dataset.player)];let i=0,timer=null;
 const image=el.querySelector('img'),slider=el.querySelector('.slider'),button=el.querySelector('.play');
 function show(){const f=p.frames[i];image.src=f.image;slider.value=i;el.querySelector('.caption').textContent=
 'frame '+f.frame+' · '+f.event+' · 名义 '+f.seconds.toFixed(3)+' s · N='+f.faces+' · mesh '+f.meshHash+' · pose '+f.poseHash;}
 function stop(){clearTimeout(timer);timer=null;button.textContent='播放';}
 function tick(){if(i>=p.frames.length-1){stop();return;}const delay=Math.max(80,(p.frames[i+1].seconds-p.frames[i].seconds)*1000);
 timer=setTimeout(()=>{i++;show();tick();},delay);}
 button.onclick=()=>{if(timer){stop();return;}if(i>=p.frames.length-1)i=0;show();button.textContent='暂停';tick();};
 slider.oninput=()=>{stop();i=Number(slider.value);show();};
 el.querySelector('.prev').onclick=()=>{stop();i=Math.max(0,i-1);show();};
 el.querySelector('.next').onclick=()=>{stop();i=Math.min(p.frames.length-1,i+1);show();};show();
});""".replace("PLAYER_DATA",json.dumps(players,ensure_ascii=False).replace("<","\\u003c"))
    page=f"""<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ROAM 实验报告</title><style>{css}</style><body><main>
<header><h1>ROAM 实验基础设施 · 可追溯报告</h1><p>真实输入、冻结相机、实际画面、正常性能与独立质量放在同一证据链中。</p><div class="meta">analysis SHA256：{content_hash(analysis_path)}</div><nav><a href="#protocol">输入协议</a><a href="#visual">实际画面</a><a href="#metrics">性能与质量</a><a href="#functions">函数证据</a><a href="#sources">原始数据</a></nav></header>
<div class="notice">{escape(scope)} 不同算法质量与行为有差异，时间比不能直接称为同质量加速。</div>
<div class="badges"><span>输入与采集身份：已检查</span><span>持续回放：已执行</span><span class="open">Agent视觉：见 visual-review.json</span><span class="open">用户视觉：待评审</span><span class="open">算法质量：独立列示，保留残余</span></div>
<h2 id="protocol">问题与输入协议</h2><p>本轮验证资产→冻结路线→运行/截图/实际mesh→独立评价→分析/作图的完整流程。运行按更新机会推进，相机返回不重置。timing不读回画面，visual的Present包含读回，quality在独立进程求值。CPU每机会全mesh检查位于计时之外；GPU普通计时不全量读回，只在证据帧捕获同代实际网格。</p>
<p>Primary reference为原始U16灰度样本与冻结双线性插值。sampled Emax不是连续曲面严格最大；RMS为可见参数域样本等权，Dmax保留逐点退化，不替代绝对质量。报告未声明“视觉不可感知”。</p>
{table(headings,rows)}
<p>GPU质量与计时只作配置级关联，不假定不同进程有相同mesh；CBT主机录制不是GPU完成时间。完整各进程/分组见<a href="process-statistics.json">统计JSON</a>及<a href="processes.csv">进程CSV</a>。</p>
{table(["CBT配置","进程序号","暖来源机会","计算采样","计算缺测","绘制采样","绘制缺测"],coverage_rows)}
{table(["失败运行","模式","状态"],failure_rows)}
<h2 id="visual">实际平台画面与固定时间线</h2><p>下方均为实际OpenGL/D3D12输出，没有离线重渲染替代。播放速度来自冻结名义时间，不能据此判断实时帧率；可暂停、逐帧和打开原图。</p>{''.join(player_html)}
<h2 id="metrics">性能、工作量与独立质量</h2><p>正常性能与视觉运行分栏；历史SVE、FPR捕获明确标记原程序和环境。质量缺失、零事务及未归属成本均保留。</p>
{table(["运行","帧","状态","实际N","Emax px","Hmax","参数域RMS px","Q","缺失覆盖","歧义覆盖","最大见证 ordinal/u/v"],quality_rows)}
<h3>最近实际数量的CBT参考</h3><p>只按实际N选最近者；即使最近者质量无效也不换点。数量接近不意味着同质量或同任务。</p>
{table(["候选","帧","N","最近CBT","参考N","相对N差","质量状态"],match_rows)}
<h3>同参考域逐点超额</h3><p>无效质量指标留空，不把部分成功样本最大值用于比较。Dmax是逐点差的最大值，可以为负；双方N不同不视为公平性能胜出。</p>
{table(["候选","参考","帧","状态","候选N","参考N","Dmax px","见证序号"],pair_rows)}
{''.join(gallery)}
<h2 id="functions">全符号表与复杂度边界</h2><p>self是栈顶事件权重，inclusive包含子调用，彼此重叠；selfSamples不是函数精确调用次数。{function_note}复杂度条目来自有限源码审查，不从百分比反推指令数或CPU时间定理；没有映射的条目保留“未审计”。</p>
<p><a href="functions.csv">下载全部符号CSV</a> · <a href="{link(ROOT/'docs/research/profiling/fpr_function_cost_analysis.md',output)}">历史FPR详细推导（旧版本，不代替当前实现）</a></p><div class="functions">{table(["序号","函数","self %","inclusive %","self样本","阶段","工作/复杂度","源码"],functions)}</div>
<h2>限制与下一步</h2><ul><li>本报告不把跨算法低耗时解释为质量等价，不将零事务解释为收敛。</li><li>稀疏热图灰格没有导出点；格内最大值不是该参数格的严格上界，见证取自全Q。</li><li>历史Tracy与当前perf是不同进程、程序和时钟，不作精确对齐。</li><li>正式协议、有效质量覆盖、实际数量与设备计时边界共同限制结论；实验执行完成不代表论文竞争性成立。</li></ul>
<h2 id="sources">原始输入与复现</h2><p><a href="analysis.json">同源analysis</a> · <a href="report.md">Markdown报告</a> · <a href="report-manifest.json">报告manifest</a> · <a href="visual-review.json">视觉评审记录</a></p>{links}
</main><script>{js}</script></body></html>"""
    (output/"index.html").write_text(page,encoding="utf-8")
    markdown=["# ROAM 实验基础设施报告","",f"analysis SHA256: {content_hash(analysis_path)}","",
        scope,"",
        "## 输入与正常性能","", "| "+" | ".join(headings)+" |", "|"+ "|".join(["---"]*len(headings))+"|"]
    markdown += ["| "+" | ".join(map(str,row))+" |" for row in rows]
    markdown += ["","## 独立质量","","Primary reference = raw U16 + bilinear；sampled maximum；RMS为可见参数域等权。","",
        "各质量点、Dmax、工作量、全部符号及历史时序见同目录HTML、process-statistics.json和analysis。无效质量不参与数值优势判断。用户视觉尚未签收。",""]
    for title, headers, records in [
        ("质量观察", ["配置", "帧", "状态", "实际N", "Emax", "Hmax", "RMS", "Q", "缺失", "歧义", "见证"], quality_rows),
        ("逐点超额", ["候选", "参考", "帧", "状态", "候选N", "参考N", "Dmax", "见证"], pair_rows),
        ("最近实际数量", ["候选", "帧", "N", "最近CBT", "参考N", "相对N差", "状态"], match_rows),
    ]:
        markdown += ["", "## " + title, "", "| " + " | ".join(headers) + " |",
                     "|" + "|".join(["---"] * len(headers)) + "|"]
        markdown += ["| " + " | ".join(map(str, row)) + " |" for row in records]
    for directory,items in [("figures",figures),("visual-figures",visual_figures)]:
        for spec in items:markdown += [f"![{spec['title']}]({directory}/{spec['id']}.png)",spec["note"],""]
    (output/"report.md").write_text("\n".join(markdown),encoding="utf-8")
    save(output/"visual-review.json",{"automatedChecks":"data and source hashes validated","agentVisualReview":"pending actual inspection",
        "userVisualReview":"pending user","algorithmQualityStatus":"not granted; see numerical residuals"})
    result={"schemaVersion":"eip-report-v1","analysis":str(analysis_path),"analysisSha256":content_hash(analysis_path),
        "figures":len(figures)+len(visual_figures),"players":len(players),"functionRows":len(functions),
        "reportSeconds":time.perf_counter()-started,"dependencies":"relative raw links require retained evidence tree; images copied locally"}
    result["artifacts"]={str(p.relative_to(output)):content_hash(p) for p in output.rglob("*") if p.is_file()}
    save(output/"report-manifest.json",result)
    return output/"index.html"
