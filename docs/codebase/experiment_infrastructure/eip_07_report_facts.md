# EIP-07 图表与报告事实

2026-09-15；[规划](../../plans/experiment_infrastructure/eip_07_report_plan.md)

## 职责与数据流

FACT：figures.py消费analysis，FigureWriter统一写PNG/SVG/PDF、固定颜色/单位和FigureSpec；不重新判断算法胜负。visual_report.py从实际帧PNG、冻结相机CSV、独立C++位置证据构造路线、200×200像素裁剪和64²稀疏参数域热图。report.py组合离线HTML、Markdown、完整函数CSV及来源链接；播放器仅推进预先保存的图像。report_checks.py验证本地链接、唯一HTML ID和图文件/analysis哈希；它不代替视觉检查。

同executionId、同frame、同实际meshHash才连接正常时间和独立质量。热图只显示已导出点的格内最大，灰格未观测；全Q最大见证单独标记。RMS保持可见参数域等权。历史SVE/Tracy与当前Windows数据分开。函数表只把源码SHA仍相同的有限审计映射到复杂度，不从采样栈顶推导调用次数。

## 实测与核查

固定9运行、10个质量见证及历史附件生成22组PNG/SVG/PDF、3播放器、124个暖ROI符号条目。首次报告142.47秒，主要包含中文字体嵌入和PDF/SVG导出；这不是算法时间，也不存在与完整新报告等功能的旧一键基线。上一阶段归约6.73秒单列，新增报告费用完整计入。原图像文件保留。

实际查看9类图、路线、DOD/immutable见证与固定裁剪。修正函数名仅显示省略号、图例遮挡、重复HTML id；函数短名加序号可回查完整符号。最终151个本地引用、22份FigureSpec检查通过；Node的独立软件测试检查3个播放器的播放/暂停/前后/拖动/末帧停止，不启动或控制浏览器。

UNCERTAIN：计算机工具返回apps=[]、browsers=[]，无法检查真实浏览器页面布局和交互。此项明确待环境恢复；实际PNG已看不能替代该项。用户视觉待评审。没有修改算法，本阶段实现结束时未单独提交；此次依大规划第17节分组补交。

证据：benchmark-output/experiment-infrastructure/eip-07/report-v2；首次report与修订版分别保留。Peking frame15的2.351px与返回Dmax约0.101px仍展示，未宣称质量通过或同质量加速。
