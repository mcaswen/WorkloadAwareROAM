# USGS 3DEP 地形来源

首批四项裁剪于2026-09-15从[官方高程服务](https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer)获取。服务声明数据截至2026-08-24。
[USGS说明](https://www.usgs.gov/3dep-product-news)允许免费获取且无使用限制；目录保留官方来源说明。
导出为None渲染函数、F32、双线性重采样的513²高程，非灰度截图。UTM十公里采样中心范围，无垂直夸张。
源高程/导出响应保存在忽略的eip-02/dem-source；文件SHA、完整请求和坐标/量化记在terrain_catalog.json。
服务是多分辨率镶嵌，远端内容可能变化。再取得不同源SHA时必须创建新版本，不能覆盖本轮运行输入。
低地样本包含海岸/水面，存在服务镶嵌留下的低幅高程接缝；预览已显示，不将它解释为算法伪影或静默修平。
