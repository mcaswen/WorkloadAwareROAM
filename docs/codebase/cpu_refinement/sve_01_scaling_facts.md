# SVE-01 实验入口代码事实

> 2026-09-14；范围：四档压力来源、实验适配和离线比较。FACT 表示本次直接扫描/运行确认；INFERENCE 表示解释。算法核心仍为 `7425736` 的 GWR 保留路径。本页不将整个事务算法重复展开，相关核心见 [算法页](../../research/cpu_refinement/transactional_algorithm_overview.md)。

## 文件、职责与依赖

| 文件 | 真实职责 | 状态/依赖 |
|---|---|---|
| `src/experiment/greedy_transactional_lod/TransactionalScalingProtocol.h/.cpp` | 四预算白名单、r/m 映射、压力姿态和矩阵、DOD 公共参数、输入校验 | 无跨帧状态；依赖 FormalCamera/公共设置；不链接到核心库 |
| `TransactionalInput.h/.cpp` | 解析几何与独立原始样本；按协议选取视图/额度 | 返回自有 InitialMesh；旧两场景沿用旧清单和精确矩阵核对 |
| `TransactionalQualityReport.h/.cpp` | 导出当前公共 Q 绝对质量与逐点误差 | 只读 Samples；只链接探针，不依赖 DOD controller |
| `tests/GreedyMultipassSnapshotProbe.cpp` | `scaling-B` 完整来源生成，sample14 后只读导出 | 本地 DOD pipeline 0..14；输出不含后续目标/队列 |
| `tests/GreedyTransactionalLodProbe.cpp` | B1/C4/C8 同输入轨迹；计时/诊断分离；结果与配置输出 | 一条轨迹一个持续 Pipeline、一个可复用线程池 |
| `tests/GreedyTransactionalLodFamilyProbe.cpp` | DOD4/8 公共 BuildRenderData，对照来源与逐帧实际输出 | 本地独立家族状态；输出仅转为离线评价几何 |
| `scripts/run_transactional_scaling.py` | 冻结、去重、顺序运行、资源界、审计、汇总 | 仅进程编排/实验分析；不参与优先级算法 |
| `scripts/transactional_scaling_report.py` | 工作/质量/复杂度描述量与静态图 | 只读取已经完成的结果，不发起实验 |
| `scripts/run_cpu_profile.py` | 透传 workers/limit-policy、归档 SVE 来源 | 原 FPR backend/区间分析复用，无新采样平台 |
| `tests/TransactionalScalingProtocolTests.cpp`、`tests/test_cpu_profile_cli.py` | 协议冻结与非法组合拒绝 | 无渲染/GUI 依赖 |
| `tests/CMakeLists.txt` | 实验目标连线 | QualityReport/ScalingProtocol 不加入 transactional core |

FACT：没有修改 DOD 热路径或 TransactionalState/Samples/Certification/Reservation/Commit/Pipeline。SVE 是观测和配置入口，不是新性能候选。

## 输入与生命周期

`ScalingProtocol::Scenario(root,B,p)` 只接受 B∈{20k,50k,100k,200k} 和 p∈{4,8}。固定 Peking 547²、80/12、depth20、阈值 .25/.10；仅设置五项现有 PassPolicy 的线程请求，保留默认动作及回退。`Camera` 用既有 float 压力公式，`View` 用 Formal `BuildCameraView` 生成 RH_NO 行序矩阵。`Validate` 精确核对场景预算、尺度、视口、split 阈值与矩阵。r/m 在初始化前设定；View 复制原配置，保持额度。

来源导出遍历当前活动叶，以精确 dyadic 参数身份共享点，从原 HeightMap 获取与输出对应的 float 高度；导出前后 HashDataOrientedRoamPassInput 不变。四份种子完成后才开始新算法。公共 DOD4/8 重新推进同一前缀，将 sample14 输出的三角形按参数点/高度排序，忽略物理面号核对完整逻辑几何。

新算法输入只提供 M0。后八轮为 14,14,14,15,16,17,18,14，不再次导入来源。线程池初始化/销毁、状态构建/销毁分别计时。每轮正常执行 SetView→Update→ConsumeMesh；保存结果后下一轮继续使用同一状态。

## 计时、诊断和质量

FACT：`frame_update` 延续旧边界；新增 `consume` 与 `frame_ready`，后者包括实际消费返回包的临时释放。正常运行仍保留 WorkLedger 的既有分段计时；诊断额外的完整 Validator、质量扫描、首批反序、Pending 重复消费不在正常时间内。返回 Batch 的审计保留/序列化和调用方最终销毁在计时外；不是进程总成本。

每个独立策略/预算一次 B1 诊断。每帧计时几何及意图/尝试/交换与 B1 诊断一致后，复用该几何的独立质量结果。诊断 Pending 按原每两轮消费，正常按每轮消费；两者逻辑网格相同但 Pending 账本不可逐项当成同工作。

家族每帧导出公共实际 mesh，整条轨迹完成后才加载原始参考并全 Q 评价。`QualityReport::Write` 输出 quality.json 及按既有 Q 身份顺序的 little-endian float64 `errors.f64`；值为像素误差，参考不可见时为 -1。RMS 为可见 terrain-domain 样本均方根，不是屏幕面积加权 RMS。Python 核对 Q 长度及参考可见掩码相同，再计算 max(e_new−e_DOD)。这不是连续曲面最大值。

## 运行与停止

runner 只有 freeze/measure/audit/report 四动作，预算/策略/CPU 集合写死为本次冻结协议。普通配置每种仅一个独立进程，不把八帧当独立样本。20k fixed/scaled 共用、每预算 DOD4/8 共用；原目录存在则读取命令记录，不覆盖。

单进程180s、内部原120s/百万访问额度、RSS8GiB、整轮40min。每100ms读取 Linux VmHWM，达到界限则终止整个被测进程组；采样不到的极短进程峰值可能未完全捕获。失败/截尾保存日志、argv、状态和墙钟。发现诊断正确性失败则停止后续新算法运行。freeze 保存源码/二进制/资产/相机 SHA 和构建缓存；measure 拒绝已冻结二进制身份变化。

## 并发与适用边界

B1 无执行线程池；C4/C8 复用 MaterializationExecutor 和原分块/发布规则。协调线程不另计为可用 CPU，所有线程受相同 taskset 集合约束。真实任务参与由独立 Tracy 判断；配置 workers 不等于每个短阶段用了这么多核。外部导出和诊断可能影响缓存，WSL 不能保证主机调度隔离。

INFERENCE：固定 Q 下 N 增长会降低 q/N，但不自动增加可认证事务或质量响应。r/m 增长同时改变需求前缀和回收池，不能分离二者因果。DOD 与新算法的预算相同不代表输出任务/质量等价。
