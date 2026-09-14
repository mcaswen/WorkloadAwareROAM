# FPR-02 窗口与采样代码事实

2026-09-14；仅描述已实现代码，后续 Tracy 会话尚未接入。

- `ProfileSession` 位于工具层，拥有 FIFO 描述符、窗口文件、单在途命令状态。构造必须取得控制环境；Begin/End 使用 CLOCK_MONOTONIC 与 perf ack；支持 `ack\n` 及可分段结尾 NUL，5 秒超时。析构尝试停止活动采集并吞掉清理异常；Finish 才写完整标志。
- `GreedyTransactionalLodProbe` 保留原 CLI，另接受 `--profile 1..32`，仅允许轨迹计时模式。新增外层重建循环复用原算法控制流；会话包围既有 frame_update，文件和独立诊断在外。普通模式不构造会话，但探针本身链接工具代码。
- `perf_backend` 负责软件事件、FP/DWARF 选项、FIFO、外部子进程期限及 512MiB 上限；官方导出与身份数据分开保留。`process.py` 只清理本工具启动的进程组，为能力和自然后端共用。
- `report.py` 按 ROI 二分过滤、实际 period 加权，缺栈仍留分母；inclusive 对同样本同符号去重。self 是机器符号自身采样，并非精确调用次数；无内联扩展主表避免未限定内联名混合。
- `run_cpu_profile.py` 保留源码、可执行文件、构建、输入与工具环境；先写原生缓存再归档，失败也留下不完整 manifest，不覆盖已有目录。不将 raw data 提交到 Git。
- 当前核心算法和生产线程池没有任何插桩。默认探针的短测性能疑点见阶段报告，不能从算法未变推导探针计时必然不变。
