# EIP-02 地形资产库小规划

> 2026-09-15；承接已闭环EIP-01；用户授权在大规划范围内自主实施

## 目标与输入冻结

新增六类数值地形及四类真实DEM裁剪，完整登记转换配方；将资产选择接入已有应用，不改变算法。
生成：平面129、非对称斜坡513、局部尖峰513、山脊513、深谷513、多尺度起伏1001；固定种子20260915。
自然区域预选：Sierra Nevada（37.65,-119.65）、Colorado河谷（36.10,-112.10）、Appalachian缓丘（36.20,-81.70）、沿海低地（35.00,-76.95）；方形米制范围10km，513²。USGS3DEP优先，具体产品ID在取得元数据后锁定；不运行新算法再挑范围。
网络/数据不可得按大规划资源上限记录，不以合成数据替代自然输入。

## 归属与实现

- Create `terrain_generation.py`：六个冻结数值函数及16位量化，输出PNG与生成元数据
- Create `dem_import.py`：调用已有地理库读取/重投影/裁剪，记录产品、CRS、bbox、样本中心与端点、量化、NoData；地理库不进入C++运行时
- Extend catalog/asset_preview：追加真实文件记录、坡度分布和离线视图
- Create `ExperimentSessionController`：读取目录/选择请求，复用LoadHeightMap与设置接口；Create `ExperimentPanel`：只展示资产并提交请求
- Extend Application/main/CLI：显式实验配置入口与面板挂接；默认关闭不增加逐帧扫描，历史两资产仍可使用
- EIP-02新增资产和配置范围不超大规划；后端回读原则上由EIP-05完善

## 验证、视觉和性能

所有资产重生成/重导入的样本哈希、实际C++加载尺寸、上下方向、源高程范围均检查。自然NoData首版必须为0，否则记录失败并只按地理完整性修正裁剪，不看算法结果。
视觉必须逐项查看高度/阴影图，生成与自然各类保存真实应用画面。精确帧回读尚未实现，当前窗口截图注明近似定位。
保存导入/加载时间与RSS；旧输入关闭实验功能的路径复用EIP-01，接入后采集最小旧场景前后；不执行正式资产×算法×预算矩阵。
本阶段先登记来源与配方、再生成/获取、再接入、再验收。事实/审查闭环后进入EIP-03。

## 导入实现细化

Ubuntu当前Python3.14无可直接安装的rasterio/pyproj包。为避免引入GIS安装工程，首批复用官方USGS ImageServer的F32高程导出与坐标投影服务，仍在离线步骤完成米制重投影/裁剪。保存请求、响应、源TIFF和SHA；不下载着色地形图当高程。513样本中心跨度10km、服务默认datum转换与多分辨率镶嵌边界均明确记录。该替代不改变算法/运行依赖或已冻结区域。
参考：[USGS服务](https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer)、[exportImage契约](https://developers.arcgis.com/rest/services-reference/enterprise/export-image/)。

实现情况：进行中。

## 视觉前置调整

2026-09-15：computer-use初始化、重复窗口发现及重置恢复均报native pipe不可用。为满足用户要求，不将离线预览冒充平台图；提前落地已批准的FrameCapture CPU契约、OpenGlFrameCapture和ExperimentVisualArtifacts最小实现，由独立资产预览入口调用。EIP-05复用这些文件并完成两后端公共挂接；不新增第二套捕获实现，不改变正常计时。EIP-02仍先闭环再启动EIP-03。
预览入口只验证图形资产，使用明确的DOD/4次更新/两角度/960×540；不作为冻结性能或质量实验。

## 实施结果与出口

完成6个生成、4个真实DEM和2个历史资产。12项C++样本逐点一致；10个新资产两角度OpenGL真帧已观看。新增输入约3.7MB，低地源拼接痕迹保留。实际helper不可用后按已批准边界前移最小OpenGL采集，D3D12通用采集仍归EIP-05。性能控制与限制见[独立调查](../../reviews/experiment_infrastructure/eip_02_performance_investigation.md)。

automatedChecks=通过；agentVisualReview=完成；userVisualReview=待用户；algorithmQualityStatus=未在本阶段评价。阶段完成。
