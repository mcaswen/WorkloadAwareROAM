# CBT 有限比较执行与报告事实

日期：2026-09-16。范围：CBI-04 的有限协议、执行脚本、离线归约和报告；GPU输出与捕获事实另见 [gpu_experiment_evidence.md](gpu_experiment_evidence.md)。

## 文件、职责和依赖

| 文件 | 实际职责 | 主要依赖 |
|---|---|---|
| `configs/experiments/cbt_2024/comparison_protocol.json` | 两资产、三面积、两CPU预算、关键机会与比较边界 | 原FER资产和路线身份 |
| `scripts/run_cbt_comparison.py` | `prepare/run/report`命令分派和显式程序参数 | `cbt_study`、按需导入`cbt_report` |
| `scripts/experiment_infrastructure/cbt_study.py` | 冻结程序/来源/case，先封存数量校准，再运行其余配置与质量评价 | 既有`catalog`、`runner`、`quality` |
| `scripts/experiment_infrastructure/cbt_report.py` | 校验质量身份，按实际N配对，按GPU采样代归约，输出CSV/科学图/中文报告 | `result_adapters`、`FigureWriter`、NumPy、Matplotlib |
| `tests/test_cbt_comparison.py` | 逐点差、身份/可见域拒绝、延迟采样归属、无效最近数量参考 | 报告纯函数；不启动图形程序 |

FACT：所有新增代码均为离线实验编排/分析，不进入C++更新、渲染或着色器调用链。没有新增通用实验框架。执行器不调用报告器，离线报告不调用算法。

## 冻结与执行控制流

`prepare(output, cbt_exe, cpu_exe, probe, cbt_provenance, cpu_provenance)`要求新目录；程序SHA必须与各自旧运行的构建清单一致。保存两份程序、清单和源码快照，记录质量probe路径/SHA。14个case从既有FER配置生成，case哈希随协议保存。

CBI-04当前CPU使用CBI-02程序，CBT使用CBI-03程序。`run_one`保留runner对当前编排树的归档，但增加`sourceSnapshotRole`与`binaryProvenance`，不把当前脚本源码身份伪称为旧CPU二进制的构建身份。

`execute`先核对程序/case内容，再按以下顺序串行运行：

```text
六个CBT visual配置
→ calibration.json：只含实际N、容量、故障
→ 全部timing配置与剩余CPU visual配置
→ 四关键机会的独立质量评价
```

已有运行仅在状态成功、程序身份相符时复用；已有失败或不同程序记录拒绝覆盖。已有未通过质量索引会阻止重跑，不自动替换失败帧。当前完整运行含5个无效评价点，后续直接离线`report`，不再调用`run`寻找更好结果。

FACT：程序/原始网格/误差数组/完整截图位于忽略目录`benchmark-output/cbt-2024/cbi-04/`。小型聚合报告、CSV、PNG和图显示规则进入`docs/research/cbt_2024/`。原始证据没有删除，未提交大体积程序、网格与矢量点云图。

## 质量证据与配对

`collect`通过既有`load_run`读取visual/timing产物，复用GPU捕获代次、容量与实际N的检查。随后核对质量索引所绑定的运行清单，以及quality/errors/locations三个文件的SHA。

有效性同时要求评价状态成功，且没有缺覆盖、覆盖歧义、无效几何或投影。无效点保留实际N、帧和失败数量，Emax/RMS/Hmax写空值；不得把局部成功样本的最大值伪装成完整有效评价。

`pointwise_excess`逐项比较source、Q、样本数量、相机和投影身份，核对误差数组长度、有限值及负值可见域标记。实际计算`max(e_Transactional(q)-e_reference(q))`，返回采样序号；不以两个全局最大值之差代替。

`nearest_count_reference`只按同资产、同机会实际N距离与冻结面积顺序选CBT。随后才检查双方质量有效性，因此无效的最近点不会被更远的有效点替换。CPU DOD参考固定同预算标签，报告另列实际N比值，不能据同预算标签假定同数量。

主指标仍为原始高度样本+双线性参考上的采样最大屏幕误差。辅助RMS为可见参数域样本等权，非屏幕面积加权；Hmax与逐点Dmax分别保留。

## 时间与图表

`gpu_samples`建立`(resourceGeneration, topologyGeneration)→机会`映射，按计算或绘制的各自采样代筛除预热，再以资源/采样代去重。旧诊断重复返回不重复计数；未返回的尾部样本不补帧。

CPU更新、CPU上传、帧包络、GPU计算、GPU绘制分别列示。CBT CPU更新是主机命令录制，不是GPU计算完成时间；不得与DOD CPU时间直接相除称同任务加速。

`make_figures`每资产生成四张图：同视图质量—实际N、关键机会质量/N轨迹、预声明代表配置真实画面、统一色标的参数域误差见证。无效质量点留空，不补线；见证图显示实际Q最大点及用于显示的样本位置，不宣称连续误差场。八张PNG已实际打开检查。

INFERENCE：离线逐点配对工作量随被比较的Q数组总长度线性增长；报告不是实时路径。矢量散点图可较大，完整SVG/PDF保存在原始分析目录，提交只保留可审阅PNG、CSV与显示规则。

## 复现入口

在Ubuntu中设置现有实验依赖路径后调用：

```bash
export PYTHONPATH=scripts:/home/mcaswen/.cache/roam-experiments/packages
python3 scripts/run_cbt_comparison.py prepare <新输出目录> \
  --cbt-exe <CBI03程序> --cpu-exe <CBI02程序> \
  --probe <parallel_roam_transactional_platform_quality_probe> \
  --cbt-provenance <CBI03正常计时manifest.json> \
  --cpu-provenance <CBI02计时manifest.json>
python3 scripts/run_cbt_comparison.py run <新输出目录>
python3 scripts/run_cbt_comparison.py report <新输出目录>
```

当前冻结路径及SHA见报告目录的`protocol.json`。原始目录已有`analysis`时，`report`拒绝覆盖；只改正文可调用`write_report`，修改显示规则则先保留旧分析目录再离线重建，不能重跑算法改变结果。

## 当前结果与未知

FACT：28个原生运行完成，56个关键评价中51个有效、5个canyon CBT点因覆盖歧义无效。有限顶点/边检查未发现同世界位置的高度/UV分裂或未配对内部边；不能据此证明评价器错误。

UNCERTAIN：5个无效点的根因、CBI-03 CPU工程性能回归根因、跨算法同质量性能结论仍未解决。CBI-04不改算法/容差，不增加试验参数，也不将有限接入完成写成所有质量与性能要求通过。
