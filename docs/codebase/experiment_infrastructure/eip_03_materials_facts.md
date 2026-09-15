# EIP-03 材质与帧捕获事实

2026-09-15；[小规划](../../plans/experiment_infrastructure/eip_03_materials_plan.md)

MaterialCatalog提供legacy/neutral/uv/rock/soil/grass六预设，三张第三方JPEG各1024²，CC0来源、MD5与SHA已登记。材质独立保存于TerrainRenderer，不进入TerrainLodSettings；ApplyMaterial不设meshDirty。GUI请求在下一帧BeginFrame前消费，D3D12不会在开放绘制列表中释放旧描述符。

GL上传到临时texture后交换，D3D12使用临时资源/SRV、立即上传和等待旧帧排空后交换；失败保留当前路径/参数/资源。历史12倍平铺、0.35染色及UNORM解释保留，两后端仅窄参数化同一漫反射公式。

为视觉门禁提前完成FrameCapture：请求只允许一个待处理项，Present时GL读取未交换backbuffer并翻行；D3D12在正常列表内复制RT到readback，恢复RT再Present，经完成围栏后按RowPitch去填充。关闭捕获不分配、不回读、不等待。结果为紧密RGBA8，VisualArtifacts写PPM，Python转换PNG。未改mesh uploader。

实际证据：eip-03/opengl-materials、d3d12-materials，各2视角×6材质。每组LOD序号/拓扑hash不变，缺失材质失败保留旧状态。旧GL画面与EIP02相同场景view0逐像素一致。截图未宣称跨后端像素等价，D3D12远处纹理minification与GL mipmap存在既有差异。

性能：eip-03/performance，相邻旧/新二进制Peking24机会暖CPU36.025405/34.720877ms，24帧hash一致，无工程回归信号；一对进程仅描述快验。材质切换时间在各frames.csv单列，不计为正常帧。编译GL/D3D12，定向application_command_line测试通过。
