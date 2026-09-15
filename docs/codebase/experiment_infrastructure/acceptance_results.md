# 实验基础设施验收与证据索引

2026-09-15；EIP-01～08已完成开发与定向验证。实际PNG视觉已检查；浏览器工具无可用surface，页面布局/真实交互及用户视觉尚未签收。本轮已按职责分为八组补交，映射见[大规划第17节](../../plans/experiment_infrastructure/experiment_infrastructure_major_plan.md#17-分阶段提交补交与后续约束2026-09-15)。

## 直接查看

- [完整自然/生成案例报告](../../../benchmark-output/experiment-infrastructure/eip-08/report/index.html)：96机会持续回放、实际帧播放器、正常性能、工作量、独立质量、固定裁剪与路线
- [Peking默认拟合反例报告](../../../benchmark-output/experiment-infrastructure/eip-08/counterexample-report/index.html)：66.08px峰值与返回残余
- [函数、时序及历史规模图示例](../../../benchmark-output/experiment-infrastructure/eip-07/report-v2/index.html)：完整符号表/复杂度附件、新perf有限样本、历史Tracy与SVE分栏
- [12地形图册](../../../benchmark-output/experiment-infrastructure/eip-02/atlas/index.html)、[实际角度0](../../../benchmark-output/experiment-infrastructure/eip-02/platform/contact-0.png)、[实际角度1](../../../benchmark-output/experiment-infrastructure/eip-02/platform/contact-1.png)
- [六材质对照](../../../benchmark-output/experiment-infrastructure/eip-03/materials.html)、[五类相机回放](../../../benchmark-output/experiment-infrastructure/eip-04/visual/index.html)
- [操作和重建命令](usage.md)

这些HTML本地打开即可；报告图像已复制，raw链接仍依赖原相对目录。原始证据在明确忽略目录中，不是已提交资产。

## 分项状态

| 事项 | 程序化证据 | Agent实际视觉 | 未完成/限制 |
|---|---|---|---|
| 地形 | 12份文件与C++加载U16样本身份；6数值配方、4 USGS冻结裁剪 | 高度/阴影/分布与新增10资产双角度已看 | 两历史输入许可待溯源；低地镶嵌特征保留 |
| 材质 | 原材质默认像素回归；材质选择不Reset | GL/DX分别六材质双视角已看 | UNORM沿用旧规则，不宣称两后端像素完全相同 |
| 相机 | 65路线；128历史行float矩阵一致；录制/暂停/单步/返回；完整CSV列名拒错 | 五类完整序列、位置/朝向与返回事件已看 | GUI和HTML真实人工交互待用户；采样离地检查不是连续碰撞定理 |
| 回放 | 三个新主case×3模式各96机会，逐帧mesh与公开工作计数一致 | 实际GL各15关键帧、DX4机会的3帧已看 | 不是完整逻辑decision trace证明 |
| 计量 | 正常/视觉/质量/profile分离；冷暖/运动/恢复、缺失与未归属保留 | 单位、轴、图例、函数短名/完整表、见证定位已看 | 单进程开发样本不提供正式CI；新perf暖739样本不足热点定论 |
| 图表与报告 | FigureSpec/analysis SHA、本地链接、播放器事件逻辑 | PNG/SVG同源，实际PNG与固定裁剪已看 | 无可用浏览器，页面排版与真实交互未验收；PDF为图表导出，未生成整本报告PDF |
| 算法质量 | 独立bilinear reference与完整逐点Dmax | 实际坏区保留 | 本阶段不授予算法质量/论文竞争力通过 |

## 完整案例结果

所有主case B=50k、1280×720、中性材质、96机会、warmup3。山脊为overview-orbit，峡谷为reveal-return；split4、merge2。按预冻结输入运行，没有换路线/阈值追正例。自动主采集171.57秒，DX补查另约1秒进程；源码归档、独立质量、构建、出图单列。48个正式矩阵case仅生成，未执行。

| 配置 | 暖CPU-ready ms/机会 | 总exchange | 最终N |
|---|---:|---:|---:|
| 山脊 DOD8 | 1.0213 | 不适用 | 8264 |
| 峡谷 DOD8 | 8.3176 | 不适用 | 50000 |
| 峡谷 Transactional immutable8 | 17.2324 | 77 | 50000 |
| 峡谷 Transactional immutable1 | 68.5806 | 77 | 50000 |

峡谷1/8线程全部96机会实际mesh与公开工作计数一致，本次独立进程比约3.98×。它是同任务线程对照；相对DOD仍慢，不能转写成跨算法胜出。

| 峡谷帧 | DOD Emax px | immutable Emax px | 同Q逐点Dmax px |
|---|---:|---:|---:|
| 2 | 2.315694 | 4.636925 | 4.636923 |
| 48 | 1.839557 | 9.274477 | 8.972327 |
| 80 | 2.348193 | 2.348193 | 1.729848 |
| 95 | 2.348193 | 2.348193 | 1.729848 |

返回Emax相同并未抹去局部Dmax。上述是已评价关键帧，不是96帧全域上界；不宣称人眼不可感知。山脊实际未用满50k预算，按原输入报告，不能与满预算峡谷合并判胜。

Peking默认fit原24机会在新入口复现Emax：frame2=0.441886、frame15=66.081137、frame16/23=31.125805px。真实图中可见尖峰。这个反例来自被测算法，没有作为基础设施错误“修掉”，也没有把immutable替换成默认策略。

## 性能与资源核查

EIP08同Windows GL、同Peking DOD24短回放前后暖均值33.9657/34.1559ms，约0.56%，低于本轮工程筛查门槛，不追加微小差值复测。最后相机列名校验仅发生于输入准备，格式/float值不变；Linux新/历史两个相机目标回归、原生两后端构建及CPU4机会入口通过；旧正式相机测试已恢复，新增测试使用独立文件。

EIP07首次完整报告142.47秒、约12.79MiB；EIP08主报告131.93秒，反例报告44.68秒。中文字体嵌入、SVG/PDF和真实图片在离线阶段付费，不计入运行帧。新正式矩阵48case生成未启动任何算法。新增地形/材质约4.88MiB，配置约3.40MiB，低于64MiB首轮范围。

## 遗留边界

1. browserVisualReview=未完成：计算机工具apps=[]、browsers=[]；播放器的软件事件测试不替代真实浏览器
2. userVisualReview=待用户；没有代签
3. algorithmQualityStatus=保留真实残余；50k以上同质量优于DOD仍待独立研究验证
4. 当前normal每机会仍有计时外完整mesh hash/check，缓存影响已记录；正式协议应沿同边界对照，不隐含忽略
5. 本轮不用新增场景扩算法，不修reservation，不扩大profiler或渲染后端范围

[大规划](../../plans/experiment_infrastructure/experiment_infrastructure_major_plan.md)包含各阶段事实与审查索引。
