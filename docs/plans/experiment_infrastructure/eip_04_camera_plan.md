# EIP-04 相机路线沉淀小规划

日期：2026-09-15；完成；依[大规划](experiment_infrastructure_major_plan.md)，前三阶段已闭环

## 契约与职责

CameraSequence保存实际float Position/Forward/View/NO/ZO矩阵、帧序号、名义时间、事件及规范化位哈希。CSV以max_digits10输出，读入逐行校验；不让Python重算GLM矩阵。相机在不同算法间由同一冻结行驱动，不受wall-clock影响。

Create：`CameraSequence.h/.cpp`负责冻结读写、姿态构造和有限序列验证；`CameraRecipe.h/.cpp`负责五模板/用户关键帧插值及reference clearance，分离文件格式与路线设计。`ExperimentCameraTools.h/.cpp`是CLI编排，历史导出在`ExperimentLegacyCameraExport.cpp`调用旧A/TPI公式；无experiment→benchmark反向依赖。`camera_preview.py`只画实际冻结位置/事件，不重算相机。

Extend：SessionController保存关键帧/播放游标，Panel只发录制、删最后点、停留、导出、加载、播放、暂停、单步、重放起点请求。Application将当前冻结行直接映射RenderContext，不通过yaw往返重建历史矩阵。资产切换退出旧路线，材质切换保留路线。暂停只画现状，不继续更新；恢复停留则每机会正常更新。回到起点显式重置，返回事件不重置。

## 冻结范围

五模板：overview-orbit96、approach-return96、terrain-traverse128、reveal-return96、stationary-recovery32；1280×720，FOV60，near0.1，far按max(500,10×size)。模板绑定全部12资产，构造不看算法结果。路径眼高从固定source reference加离地余量，方向非退化，所有机会及连段插值检查穿地。历史A64、orbit64、PQ24和SVE8由原公式导出并按float位核对，历史文件与调用不改。

用户配方保存关键姿态和停留机会，最终CSV保存展开行；GUI不提供虚假的随机seek，只有起点重放。录制输出位于独占时间戳目录，不能覆盖旧证据。

## 最小验证与性能

冻结→读回→逐位hash相等；篡改/缺行/非有限/退化拒绝；historical输入逐项等于旧函数。路线图实际打开；一个生成资产跑五短序列关键帧（新预览工具仅用于视觉，不作为性能），检查返回/停留仍保持同一LOD会话。

旧公式生成64行对比新包装，生成/加载时间单列，新增离线能力不与算法帧时间混比。CLI与序列专项测试，不跑算法全矩阵。完成后事实、视觉审查、计划回填；再进入通用回放。

实现细化：播放状态独立为app/ExperimentCameraSession，SessionController组合它；不把游标与录制逻辑堆入资产加载。真实路线预览在benchmark/experiment/ExperimentCameraVisualReplay，每四机会截图，所有机会仍更新；EIP05复用公共序列，不把此诊断计时当正常性能。

## 实施结果

65条冻结路线、128行历史矩阵位审计、录制/播放状态测试和五组真实路线画面完成。旧/新64点构造费用差值均小于0.05ms；新文件IO另计。详见[事实](../../codebase/experiment_infrastructure/eip_04_camera_facts.md)与[审查](../../reviews/experiment_infrastructure/eip_04_camera_review.md)。

提交归档补充：冻结CSV保留原导出的CRLF字节，清单SHA/FNV包含换行；.gitattributes将该目录标为不做文本换行转换，避免Windows/Ubuntu检出改变输入身份。65条清单的暂存文件字节与原SHA逐项核对；没有重写路线或历史证据，不影响运行成本。
