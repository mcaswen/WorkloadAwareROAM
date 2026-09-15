# EIP-02 地形资产与视觉代码事实

> 2026-09-15；FACT为已落地能力；阶段出口与性能对照以审查记录为准

## 资产生成与来源

`terrain_generation.py`固定六个数值函数，seed=20260915；平面129、四项513、多尺度1001。均经最近偶数舍入量化U16，生成器重跑必须与已存样本一致，不覆盖不同内容。
`dem_import.py`从官方3DEP ImageServer请求None渲染函数、F32、513²、UTM十公里窗口。以官方GeometryServer投影预冻结中心；bbox补半格，首末样本中心跨度10km。TIFF北到南行翻转为v向北。无NoData，高程减去最小值后量化，世界跨度80、HeightScale=(max-min)*80/10000，不夸张垂直方向。
源TIFF、响应和元数据在忽略目录dem-source；运行PNG、来源/请求/源SHA与派生SHA在assets。服务多分辨率镶嵌/默认datum转换明确保留，不称原始测量点。低地包含水面拼接痕迹，已在预览与许可说明中注明。
12个运行输入总计3697137字节；原有2个未改写。采样原件为冻结U16，primary reference保持双线性。

## C++输入与应用职责

`TerrainAssetCatalog.h/.cpp`在实验输入层保存TerrainAsset值记录，LoadTerrainAssetCatalog复用Boost，检查版本/唯一ID/尺度/方形范围/存在性。目录根从assets/experiments向上推导。当前GUI目录读取是可信项目清单路径；文件内容哈希由Python验证流程交叉核对，不在每帧扫描。
`ExperimentSessionController`拥有asset数组、可选pending索引和error；Initialize一次读目录，Request只登记，TakeSelection消费后清空。
`ExperimentPanel`借用只读asset span，返回选中索引；不依赖Application类、算法或GPU资源。
`Application::ApplyExperimentSelection`调用LoadHeightMap，再更新面板尺度/深度、应用已有设置及固定观察相机。材质/相机录制尚未接入。旧高度图菜单保持；实验面板默认显示于启用基础设施的交互构建，运行benchmark时隐藏。
`--experiment-asset ID`选择初始资产；未开启构建能力时显式报错。无参数旧入口继续存在。

## 真实帧采集前置

computer-use native pipe不可用，按已授权大规划提前引入最小OpenGL回读。
`FrameCapture`拥有从上到下的RGBA8与ID/尺寸/颜色说明；`CaptureOpenGlBackBuffer`只在当前上下文Present前调用，恢复pack alignment、read framebuffer/read buffer及pack buffer，最后翻转行。它没有常驻GPU资源或后台线程。
`WriteCapturedFrame`输出原始PPM，拒绝覆盖；Python联系页只转码/排版，原件保留。
`RunExperimentAssetPreview`独立创建窗口/backend/renderer，DOD2万预算、960×540、两个固定观察角度，每角度4次更新后读回。stdout/CSV/实际图像来自此独立运行。不是正式轨迹、质量比较或正常帧计时。
D3D12捕获仍PLANNED；当前入口在该后端明确拒绝。后续EIP-05扩展已有文件，不再创建平行截图系统。

## 数据流与生命周期

目录→asset值→pending→Application受控选择→原HeightMap/renderer→持续LOD。
独立预览：LoadCatalog→Window/Backend→Renderer→两角度更新→Render→BackBuffer回读→PPM→Present→WaitIdle/Shutdown。
失败走统一清理；normal/TPI回放不调用任何帧回读。没有算法层到GUI、Python或benchmark的反向依赖。

## 验证

两项旧输入和十项新输入的C++实际U16字节全部匹配Python样本SHA。
十项新输入已各取两个真实OpenGL角度；Agent查看两张完整联系图及前序各资产高度/阴影图，地貌、朝向、完整边界可辨。尚未对任意GUI交互作自动点击验收，连接不可用不伪称成功。
CLI原定向测试通过；无全CTest。初始关闭回读路径前后差值触发一次复测，当前继续进行功能开关控制分析，不能先写无性能问题。

## 未确认项

UNCERTAIN：两项历史来源许可；低地服务镶嵌的原始水面处理细节。
PLANNED：材质切换、GUI相机录制、完整同帧哈希/两后端统一捕获接口、正式报告归约。
