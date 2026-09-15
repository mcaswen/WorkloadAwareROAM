# EIP-03 简单材质与浏览小规划

日期：2026-09-15；状态：完成；依据[大规划](experiment_infrastructure_major_plan.md)

## 目标与范围

新增中性、方向UV与岩/土/草漫反射预设；历史预设保留12倍平铺和0.35高度染色。材质改变不进入LOD配置，不重置持续状态。加载失败保持旧有效纹理。素材来自Poly Haven CC0原资产，读取官方API仅取3项1K diffuse，不使用网页预览。

## 文件归属与设计

- Create：`MaterialCatalog.h/.cpp`保存预设数据及输入校验；`material_assets.py`生成诊断图、获取三张漫反射并登记SHA/许可。资产位于textures/experiment、catalog位于assets/experiments。
- Extend：TerrainRenderer独立保存平铺/染色参数并公开窄ApplyMaterial，避免普通GUI设置覆盖已选材质；两后端独立安全替换GPU纹理。D3D12临时texture/SRV上传成功后等待旧资源排空再交换，不重构mesh uploader。
- Extend：SessionController管理材质数据，ExperimentPanel返回选择命令；Application在帧边界调用渲染接口。
- Reuse：现有光照、纹理采样、调试线框。GLSL/HLSL常量一致，保持旧UNORM/历史gamma行为；不声称新PBR颜色管理。
- 两后端视觉是本阶段硬验收。原生计算机观察工具仍不可用，因此继续沿已批准FrameCapture边界，把EIP-05后端请求/回收及D3D12最小读回前移，EIP-05再完成通用回放身份。GPU资源由各后端负责，关闭采集无额外等待/读回；CPU编码由VisualArtifacts负责。

## 核查与出口

一个山脊输入、同一实际网格/相机，legacy/neutral/UV/地表在GL/D3D12各采一组，检查真实画面、UV方向、颜色、材质失败恢复。切换前后buildSequence/topologyHash/meshHash不变；禁用采集的旧Peking24机会只做必要前后快验。记录材质加载时间，和正常帧时间分开。无需算法测试全集。

首版不引入材质插件、PBR、GPU算法或质量策略。复杂度：切换O(texture pixels)，正常shader每像素仍一次diffuse采样；catalog只初始化一次。

## 实施结果

六预设、两后端24幅真实画面与材质失效恢复完成；切换不改变LOD。旧GL外观逐像素相同。相邻旧/新正常路径36.03/34.72ms，无工程回归信号，详细边界见[事实](../../codebase/experiment_infrastructure/eip_03_materials_facts.md)与[审查](../../reviews/experiment_infrastructure/eip_03_materials_review.md)。
