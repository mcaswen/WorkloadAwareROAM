# EIP-01 输入与证据代码事实

> 2026-09-15；FACT仅限当前输入模块；后续相机、平台采集与完整报告为PLANNED

## 文件、职责与调用

`scripts/run_experiment.py`提供catalog/resolve；前者调用`catalog.validate_catalog→asset_preview.build_preview`，后者调用`resolve_case`写独立JSON。
`catalog.py`负责重复JSON字段、schema、路径边界、文件SHA、真实灰度样本和U16-LE-row-major SHA。8位L灰度乘257，与stb范围一致；不做gamma、地理转换或算法调用。资产/配置无持久缓存，每次离线调用重新核对。
`asset_preview.py`通过Matplotlib生成源高度、归一化光照阴影、直方图及HTML，依赖项目字体；这些是离线预览，不是平台画面。
`verify_inputs.py`对两项历史配置生成resolved JSON，调用C++测试并比较实际样本字节哈希。

`src/experiment/infrastructure/ExperimentCase.h/.cpp`公开值类型及LoadResolved；读取运行字段，校验枚举、有限尺度、文件存在、尺寸/预算/线程/视口。Boost仅在cpp包含。它不重新计算文件SHA，文件内容身份由Python入口验证，真实加载仍须由消费者核对。
`ExperimentRecords.h/.cpp`独占创建目录、拒绝已有目录，复用ExperimentCsvCodec写三列kind/identity/value，每次flush显式暴露IO错误。析构关闭流，无删除/后台线程。
`cmake/ExperimentInfrastructure.cmake`开关默认OFF；开启时创建静态库，复用已有Boost头文件。未接应用，没有每帧开销。

## 数据与状态

asset目录为来源真值；case仅引用asset并冻结算法/预算/视口等。Python展开成resolved case，C++实例拥有路径/字符串，不借用JSON树。
原始图片SHA与解码样本SHA分别记录；Peking真实547²，test129129²。历史来源未确认，不能据此宣布再分发许可。
记录器为Created→Writing→Closed；目录冲突/文件打开/flush失败抛异常，不覆盖旧运行。
无跨线程共享、GPU资源、算法队列或每帧清单读取。

## 验证与性能

`tests/ExperimentInfrastructureTests.cpp`验证实际HeightMap加载、输出U16原始字节、损坏字段与重复目录拒绝。
`benchmark-output/experiment-infrastructure/eip-01/`包含before、after、一次recheck和visual-reviewed。
实际样本哈希均一致，新增目录检查为离线工作。窗口、算法、renderer均未修改；尚未形成正常帧时间证据。

## 不确定与后续边界

UNCERTAIN：两历史资产原始许可。
PLANNED：通用路线、实际帧采集、全运行manifest及统计；当前evidence.csv只是底层具名记录，不应称完整报告系统。
风险：未来直接从C++打开外部resolved case时仍需补内容核对，不能因SHA字符串格式合法就认为文件内容一致。
