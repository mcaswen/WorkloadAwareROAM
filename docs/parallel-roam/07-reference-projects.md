# Reference Projects

> 本文件保留 Classic ROAM、数据导向设计和渲染工程的参考资料。

检索日期：2026-07-03（Asia/Shanghai）  
数据来源：GitHub Search API 与项目 README。Star 数会变化，后续引用报告时需要重新确认。

## 结论先行

没有找到 star 很高、同时满足“ROAM + 地形编辑器 + SDL2/OpenGL C++”的完整项目。更现实的参考路线是分开借鉴：

1. SDL2 迁移与 Classic ROAM 最小实现：优先看 `roverdiani/ROAMSimple`。
2. 无限地形、chunk、split-only ROAM、多线程 LOD：优先看 `sksharan/roam-terrain-generator`。
3. SDL2/OpenGL 地形编辑器 UI 和项目结构：优先看 `christt105/Elit3D`。
4. 大型地形/赛道编辑器工程组织：可以旁看 `stuntrally/stuntrally3`，但不建议深度迁移其引擎架构。

## 推荐优先级

| 优先级 | 仓库 | Stars | 技术 / 主题 | 适合借鉴 | 注意事项 |
|---|---|---:|---|---|---|
| A | [roverdiani/ROAMSimple](https://github.com/roverdiani/ROAMSimple) | 0 | C++、SDL2、OpenGL、CMake、Classic ROAM | GLUT -> SDL2 迁移方式、事件循环、ROAM demo 保留方式 | star 低，但命中度最高；代码主要用于算法和迁移参考，不适合作为现代架构模板 |
| A | [sksharan/roam-terrain-generator](https://github.com/sksharan/roam-terrain-generator) | 13 | C++、SDL2、OpenGL 3.3、GLEW、GLM、CMake、libnoise、split-only ROAM | chunk 化无限地形、ROAM LOD、wireframe、纹理按坡度混合、多线程 LOD 更新 | GPL-2.0；README 明确提到 chunk 间 LOD 裂缝 |
| B | [christt105/Elit3D](https://github.com/christt105/Elit3D) | 131 | C++、SDL2、OpenGL、3D tile map editor | SDL2/OpenGL 编辑器结构、brush / layer / export 类功能的产品思路 | README 标注当前不维护；它是 tile-based editor，不是 ROAM |
| B | [Illation/PlanetRenderer](https://github.com/Illation/PlanetRenderer) | 100 | C++、OpenGL、CDLOD、frustum culling、terrain LOD | 现代 terrain LOD 对照、camera/frustum/LOD 工程组织 | 不是 ROAM，不是编辑器；更多用于报告里的替代算法对比 |
| C | [stuntrally/stuntrally3](https://github.com/stuntrally/stuntrally3) | 207 | C++、Ogre-Next、Track Editor、terrain、racing game | 大型编辑器功能分层、资源组织、赛道/地形编辑工作流 | 依赖 Ogre-Next，体量很大；不适合直接迁移 |
| C | [stuntrally/stuntrally](https://github.com/stuntrally/stuntrally) | 643 | C++、OGRE、Track Editor、terrain | 老版成熟工程参考 | GPL-3.0，仓库说明开发已转向 `stuntrally3` |
| C | [karthikeysaxena2507/RealTimeTerrainRendering](https://github.com/karthikeysaxena2507/RealTimeTerrainRendering) | 4 | C++、Delaunay、ROAM | ROAM 与 Delaunay 的课程项目式对照 | star 低、无明确许可证 |
| C | [AmolSamota/Terrain-Rendering-using-DSA](https://github.com/AmolSamota/Terrain-Rendering-using-DSA) | 2 | C++、Delaunay、ROAM | 另一份课程项目式 ROAM 参考 | star 低、无明确许可证 |

## 直接参考：ROAMSimple

`ROAMSimple` 的 README 说明它是 Bryan Turner 经典 ROAM demo 的 SDL2 移植版，主要把旧 GLUT 窗口和事件管理替换为 SDL2，同时尽量保留原 ROAM 代码。

适合我们看：

- `SDL_Init`、window、OpenGL context 创建；
- SDL2 event loop 如何替代 GLUT callback；
- FPS 相机输入如何接入；
- 旧式 ROAM demo 的数据结构和渲染流程；
- CMake + SDL2 的最小组织方式。

不建议照搬：

- 旧 OpenGL immediate mode 或过时状态机风格；
- 全局变量式 demo 架构；
- 缺少现代 shader / VBO / VAO 分层的部分。

## 算法参考：roam-terrain-generator

`roam-terrain-generator` 的 README 描述它使用 modified split-only ROAM 做无限地形 LOD，并包含 chunk、多线程 LOD 更新、程序化纹理和 grass instancing。

适合我们看：

- chunk 与 ROAM tree 的关系；
- split-only 在工程上如何先跑通；
- LOD 更新如何拆到多线程；
- wireframe debug 输出；
- CMake 查找 SDL2、GLEW、GLM 等依赖的方式。

需要警惕：

- GPL-2.0，不要复制实现代码进作业仓库；
- README 明确提到不同 chunk LOD 之间可能产生裂缝；
- 它偏无限 procedural terrain，和我们的 Height Map benchmark 主线不完全一致。

## 编辑器参考：Elit3D

`Elit3D` 是 SDL2/OpenGL 相关性最高的地形/地图编辑器候选。README 描述它是 3D tile-based map editor，支持多层 tile、3D object 和导出不同格式。

适合我们看：

- SDL2/OpenGL 编辑器主循环和工程组织；
- brush、layer、selection、export 这类编辑器概念怎么组织；
- release、docs、roadmap 的项目材料组织；
- 如果后续要做 Height Map brush，可借鉴它的工具面板思路。

需要警惕：

- README 标注当前不维护；
- 它是 tile-based map editor，不是 ROAM terrain editor；
- 使用 Premake，不是我们计划里的 CMake。

## 大型工程参考：Stunt Rally

`stuntrally/stuntrally` 和 `stuntrally/stuntrally3` star 更高，而且都带自己的 track editor，但它们主要基于 OGRE/Ogre-Next，不是 SDL2 + raw OpenGL 项目。

适合我们看：

- 编辑器模式与游戏模式分离；
- 地形、赛道、物体、材质等资源组织；
- 大型项目如何把工具链和 runtime 放在同一仓库。

不建议：

- 不要迁移 OGRE 架构；
- 不要把赛道编辑器目标混进 ROAM 主线；
- 不要让大型工程复杂度影响结课项目范围。

## 对本项目的迁移建议

首轮迁移只参考 `ROAMSimple` 和 `roam-terrain-generator`：

1. 先搭 SDL2 window + OpenGL context + event loop；
2. 用现代 OpenGL 封装替换旧 demo 的渲染层；
3. 把 ROAM 核心数据结构和渲染上传分离；
4. 先做 split-only 和 wireframe；
5. 再引入 Height Map sampling、error metric、neighbor / forced split；
6. 编辑器功能只保留 debug UI，不做完整 terrain editor。

等 ROAM 和 benchmark 稳定后，再参考 `Elit3D` 的 brush/layer/export 思路扩展工具界面。

## 检索链接

- ROAM terrain C++：https://api.github.com/search/repositories?q=ROAM+terrain+language:C%2B%2B&sort=stars&order=desc&per_page=10
- ROAM SDL2 C++：https://api.github.com/search/repositories?q=ROAM+SDL2+language:C%2B%2B&sort=stars&order=desc&per_page=10
- terrain editor SDL2 C++：https://api.github.com/search/repositories?q=terrain+editor+SDL2+language:C%2B%2B&sort=stars&order=desc&per_page=10
- terrain LOD OpenGL C++：https://api.github.com/search/repositories?q=terrain+lod+OpenGL+language:C%2B%2B&sort=stars&order=desc&per_page=10

