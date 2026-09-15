# EIP-07 图表与报告小规划

2026-09-15；开发闭环，浏览器视觉待环境恢复；EIP-06已闭环

## 输出与边界

只消费已冻结analysis和它引用的原始证据，不在绘图函数里重算胜负/采样域。正常时间、视觉运行和历史数据分栏。固定颜色/线型/单位，PNG+SVG+PDF；每图带FigureSpec和输入analysis SHA。HTML离线打开，实际截图原图不改写，误差/见证另作可辨识叠层。

- Create figures.py：时间、互斥阶段、兑现链、质量/RMS/Hmax/Dmax、质量-时间、历史SVE规模与线程、完整函数表与同捕获时序
- Create visual_report.py：帧播放器、固定裁剪和见证页，Python读取真实截图/位置证据；稀疏热图按格内已观测最大显示，未观测格留空，不插值伪造曲面
- Create report.py：中文问题/协议/视觉/性能/函数/质量/限制模板；值只来自analysis，链接原表/manifest/Source ZIP
- Create FigureSpec配置与报告样式（专用模块内，不建Web服务框架）；可用浏览器本地HTTP仅用于验收，报告本身不用服务端
- Extend run_experiment.py report入口

## 验收

1. 现有9运行+质量/旧SVE+Tracy+新perf生成标准图；缺少当前1/4/8同任务点时明确未测，只用历史图演示样式
2. 函数表保留全部符号，self与inclusive不相加，739暖样本标不足；复杂度只链接已审计部分，其余未审计
3. 实际查看每类图PNG及浏览器HTML，检查中文字体/图例/轴/单位/截图比例/事件/帧链接；用户视觉状态待用户
4. 固定数据集归约/绘图耗时和体积单列；不再跑新的算法性能矩阵
5. 输出失败或图数据无法配对则标缺失/停止对应图，不填零，不以漂亮图制造质量通过

完成后EIP08再用一生成+一自然新场景走全流程，并沉淀操作入口。

## 实现结果

22组图、3播放器、124符号；151本地引用及22份来源校验通过，实际PNG已看并修正呈现问题。首次142.47秒为完整新增报告费用，未重复运行算法。浏览器不可用，交互只通过软件事件逻辑测试。详见[事实](../../codebase/experiment_infrastructure/eip_07_report_facts.md)与[审查](../../reviews/experiment_infrastructure/eip_07_report_review.md)。
