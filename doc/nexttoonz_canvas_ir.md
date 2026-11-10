# NextToonz 画布 IR 设计草案

版本 0.4，2026-11-09。所有者已审过第 9 节的问题，迁移第 0 步（4.6 节）、第 1 步（第 7 节的两个后端）和第 2 步（第 8 节，纯色笔触与区域）已经实现；其余仍是设计。Q2（笔触线重）的对照图已经渲出，等所有者看图决定。本文是 `doc/nexttoonz_plan.md` Phase 1 第一个工作项"定义路径级画布 IR"的设计草案，以及支撑它的绘制操作清单。第 2 步起渲染路径开始改动：默认仍走 OpenGL，逐像素与改动前相同；Skia 路径要显式打开。

调查基线：分支 `nexttoonz/phase0`，提交 `6d043a367`。所有路径相对于 `toonz/sources/`。标着"实测"的结论来自 `tests/skia_spike/`、`tests/canvas_skia/`、`tests/canvas_gl/` 里的测试（Skia m144 和 m148，x86_64 macOS），其余来自读代码；清单的逐类明细由自动化代码调查产出，带文件与行号，我抽查过其中的关键几处，没有逐行复核全部。

---

## 目录

1. 结论摘要
2. 现状：离线路径里的绘制怎样到达 OpenGL
3. 绘制操作清单
4. IR 设计
5. 样式与区域怎样映射到 IR
6. 什么留在栅格侧
7. 两个后端怎样消费 IR
8. 调用点的迁移顺序
9. 需要所有者决定的问题
10. 风险与未验证事项
11. 附录 A：样式类清单
12. 附录 B：离屏 GL 使用方清单

---

## 1. 结论摘要

- 离线路径里真正需要的绘制操作很少。50 余个样式类加上七处离屏 GL 用到的全部东西，可以归并成 **6 个绘制操作加 3 个状态操作**：填充路径、描边路径、画网格、画图像，保存、恢复、变换，以及裁剪。OpenGL 里其余的用法（两遍颜色掩码、stencil、显示列表、矩阵回读、平滑线镶边）都是实现手段，不是绘制意图，IR 里不出现。
- 现有代码的反走样不是光栅器做的，而是**在不反走样的填充上再叠一圈一像素的平滑线**。全树没有 MSAA。换成解析式反走样的光栅器后，这圈镶边必须从样式代码里删掉，改成画笔上的一个属性，否则形状会被加粗。这是 Phase 1 里像素变化的主要来源，也带出两个必须由所有者拍板的外观问题（第 9 节 Q1、Q2）。
- IR 的几何只有两种：**路径**（若干条轮廓加填充规则）和**网格**（三角形，可带逐顶点颜色和贴图坐标）。现有 57 个 `drawStroke`/`drawRegion` 里，带状四边形、三角扇、多边形都是网格；区域是路径。
- Skia 的 CPU 后端能直接画进 `TRaster32P` 的内存，不需要拷贝、不需要翻转、不需要换通道顺序（实测；macOS 上有一个舍入上的小差别，见 `doc/nexttoonz_phase1_skia.md` 3.5）。`TOfflineGL` 的"建上下文、切线程、画、读回"整套流程可以换成"在栅格上建画布、画"。
- 计划里"IR 到旧 GL 的桥接后端……Phase 1 结束后删除或仅作开发工具"这一条**做不到**：样式类改成发 IR 之后，Phase 3 之前的交互 viewer 仍然是 GL，它画矢量样式只能靠这个桥接后端。见 Q3。

---

## 2. 现状：离线路径里的绘制怎样到达 OpenGL

### 2.1 tcomposer 能走到的 GL 一共七处

| # | 位置 | 画什么 | 怎样读回 |
|---|---|---|---|
| 1 | `toonzlib/tcolumnfx.cpp:1037-1088`，`TLevelColumnFx::doCompute` 的矢量分支 | 一张矢量图，经 `TOfflineGL::draw` 到 `tglDraw` | `getRaster` 读进 tile |
| 2 | `toonzlib/toonzimageutils.cpp`，`vectorToToonzImage`（矢量层带 Sandor 特效或调色板滤镜时由 `tcolumnfx.cpp:1336` 调用） | 每条笔触、每个区域各建一个 `TOfflineGL`，黑色画在白底上 | 取 R 通道当作 CM32 的 tone，区域按 128 取阈值 |
| 3 | `stdfx/particlesfx.cpp:458`、`stdfx/iwa_particlesfx.cpp:516` | 没接贴图时的默认粒子：10x10 的一条椭圆笔触 | `getRaster()->clone()` |
| 4 | `common/tvrender/tsimplecolorstyles.cpp:1385`，`TRasterImagePatternStrokeStyle::loadLevel`（从调色板加载到这里的调用链没有逐步追过） | 图案样式的矢量源帧，栅格化成 256x256 贴图 | `getRaster` |
| 5 | `toonzlib/plasticdeformerfx.cpp:346-449` | 贴图三角网格（`tnzext/meshutils.cpp:319`），输入先在 CPU 上算好 | FBO `toImage` 逐行拷贝 |
| 6 | `stdfx/iwa_flowpaintbrushfx.cpp:890-1009` | 贴图三角带，预乘混合 | FBO `toImage` 逐像素拷贝 |
| 7 | `stdfx/shaderfx.cpp`、`stdfx/shadingcontext.cpp` | 全屏四边形加 GLSL 片元程序，还用 transform feedback | `glReadPixels` |

第 7 处按计划留到 Phase 3。图标生成、tconverter 和只在界面里走到的使用方列在附录 B。

`toonzlib/imagebuilders.cpp` 和 `toonzpreview/` 出现在计划 2.2 节的使用方名单里，实际上前者只有一行没人用的 `extern` 声明，后者只是注释。`TOfflineGL` 留下的可插拔后端接口（`defineImpGenerator`、`setContextManager`）没有任何调用者。

### 2.2 主路径逐步

1. `TLevelColumnFx::doCompute` 取到矢量图，算出"世界坐标到 tile 像素"的仿射：`TTranslation(-tile.m_pos) * TScale(1/shrink) * info.m_affine`。矢量因此直接按最终分辨率栅格化，之后不再重采样。
2. 用 `ProductionSettings` 建 `TVectorRenderData`：带 alpha、反走样、画区域、区域边界反走样、不画零粗细笔触、`m_cf` 为空。只有重采样质量设成 Closest Pixel 时才关掉反走样。
3. `TOfflineGL`：建或复用离屏上下文，清成透明，视口和正交投影设成"一个单位一个像素、原点在左下"。
4. `tglDraw(rd, vectorImage)`（`common/tvrender/tglregions.cpp:682`）按组遍历：每组先画属于它的区域，再画它的笔触。样式的所有颜色经 `m_cf` 之后 alpha 都是 0 的图元直接跳过。
5. 每个图元：按样式建或取缓存的 prop，然后 `prop->draw(rd)`。带 alpha 时 **prop 被画两遍**：第一遍只写 RGB，混合因子 `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`；第二遍只写 alpha，混合因子 `ONE, ONE_MINUS_SRC_ALPHA`。两遍合起来的结果是"非预乘的源色，以 source-over 画到预乘的目标上"。
6. prop 压入 `rd.m_aff`，从 modelview 矩阵回读出像素大小（`tglGetPixelSize2`），像素大小变了就重算缓存的几何，然后调样式的 `drawStroke` 或 `drawRegion`。
7. 纯色笔触（`TSolidColorStyle::doDrawStroke`）：轮廓是一串左右成对的点，用 `GL_QUAD_STRIP` 填；再用 `AntialiasingOutlinePainter` 在窗口坐标里沿轮廓画一圈一像素宽、alpha 从满到 0 的三角带。半透明时先用 stencil 保证每个像素只画一次，再只在 stencil 之外画那圈镶边。
8. 纯色区域（`TglTessellator::tessellate`）：GLU 细分器按正环绕数规则出三角形，回调直接接 `glBegin`/`glVertex3dv`；然后把每条边界用 `GL_LINE_LOOP` 加 `GL_LINE_SMOOTH` 再描一遍。
9. `getRaster` 读回。tile 不是 8 位时先读进一张临时的 8 位栅格，再用 `TRop::convert` 展宽。

列的颜色滤镜和不透明度不在这次绘制里：渲染时它们是下游的一个特效节点（`toonzlib/scenefx.cpp:972-985`），摄影表视图里才是通过 `m_cf` 在绘制时做。

### 2.3 贯穿全局的六个事实

1. **反走样靠镶边。** 填充本身从不反走样；笔触靠窗口空间的渐变三角带，区域靠平滑线。没有 MSAA，FBO 没有设采样数。
2. **预乘结果来自两遍绘制**，不是来自某个预乘步骤。`TOfflineGL` 里没有任何 premultiply 调用。
3. **像素大小靠回读 GL 矩阵**，共 20 多处（`tglGetPixelSize2`，以及纯色笔触镶边里对视口、投影、modelview 的三次回读）。样式用它决定曲线采样密度、最小粗细和细节层次切换。
4. **样式自己不设混合模式和线平滑**，都是调用方设好的。唯一自己调 `glBlendFunc` 的是栅格图案笔触。
5. **所有 `GL_LINES` 和 `GL_LINE_STRIP` 都是一个设备像素宽的线**，与缩放无关。唯一设线宽的是 `TCenterLineStrokeStyle`。这些线一多半是带状填充两侧的反走样镶边，另一小半本身就是内容（毛发、素描线、编织线）。
6. **矢量永远是 8 位、非线性空间。** 64 位和浮点渲染里，矢量先按 8 位栅格化再展宽；tile 标成线性时框架先转回 sRGB 再调 `doCompute`。

---

## 3. 绘制操作清单

下表把样式类和离屏 GL 使用方里出现的图元与状态归并成类，右列是它在 IR 里的去处。"类数"是直接发出该组合的样式类个数，明细见附录 A。

| 现有 GL 用法 | 类数 | 用途 | IR 里的去处 |
|---|---|---|---|
| GLU 细分的多边形填充，带洞，正环绕数 | 1 个直接用，16 个区域样式和矢量笔刷复用 | 区域填充 | 填充路径 |
| 纯色 `GL_QUAD_STRIP`、`GL_QUADS`、`GL_TRIANGLES`、`GL_TRIANGLE_FAN`、`GL_POLYGON`、`gluDisk` | 9 + 7 + 2 + 1 + 2 + 2 | 笔触轮廓、带子、图案块、圆点 | 画网格（纯色），或填充路径 |
| 逐顶点颜色的 `GL_QUAD_STRIP`、`GL_QUADS` | 8 + 1 | 沿宽度或沿长度的渐变、边缘淡出 | 画网格（逐顶点颜色），两个渐变填充样式改用渐变着色器 |
| 固定管线光照的 `GL_QUAD_STRIP` | 1 | `TNormal2StrokeStyle` 的管状高光 | 在 CPU 上把光照算成逐顶点颜色，再画网格 |
| 窗口空间的渐变三角带、`GL_LINE_LOOP` 平滑线 | 2 | 反走样镶边 | **不进 IR**，由画笔的反走样属性取代 |
| 纯色的一像素线（线带、线段、圆） | 17 + 9 + 2 | 一部分是镶边，一部分是内容 | 是镶边的删掉；是内容的用描边路径，笔宽以设备像素计 |
| 逐顶点颜色的一像素线 | 3 | 毛发、阴影排线 | 描边路径加线性渐变着色器 |
| 虚线（`glLineStipple`） | 2 | 中心线样式、图案太小时的替代画法 | 描边路径的虚线模式 |
| 设备像素宽度的线（`glLineWidth`） | 1 | 中心线样式 | 描边路径，笔宽以设备像素计 |
| `GL_POINTS`，含平滑点和点大小 | 3 | 点状阴影、交叉线退化 | 填充路径（一批小圆），半径以设备像素计 |
| 贴图四边形 | 1 | 栅格图案笔触的印章 | 画图像 |
| 贴图的细分多边形、贴图的四边形带 | 1（`TTextureStyle`） | 纹理填充、纹理笔触 | 填充路径加图像着色器；画网格加贴图坐标 |
| 递归 `tglDraw` | 1 | 矢量图案笔触的印章 | 发射端递归，IR 只需要保存、变换、恢复 |
| stencil：只在区域内画 | 13 | 区域里的图案 | 裁剪到路径 |
| stencil：只在区域外画、嵌套掩码 | 4、2 | 一半是真的反向裁剪，一半只是防止镶边重复混合 | 反向裁剪；防重复混合的那部分随镶边一起消失 |
| stencil：每个像素只画一次 | 1（及复用它的样式） | 半透明笔触自相交时不加深 | 不需要：一条路径按非零规则填充就是并集（实测） |
| 两遍颜色掩码 | 调用方 | 得到预乘结果 | 不需要：目标是预乘的，source-over 一次完成 |
| 显示列表、顶点数组、`glPushAttrib` | 4、6 | 性能与状态保存 | 不进 IR |
| 矩阵压栈、平移、乘矩阵 | 6 加全部 prop | 逐实例定位 | 保存、变换、恢复 |
| 回读 GL 状态 | 20 多处 | 像素大小、线宽范围、点大小范围、当前颜色 | 画布提供当前变换和像素大小；驱动相关的范围改成常量 |
| CPU 合成后 `glDrawPixels`（摄影表画法） | 图标路径 | 栅格层上屏 | 画图像 |
| 贴图三角网格（plastic、流动笔刷） | 2 处特效 | 网格变形、贴图笔刷 | 画网格加贴图坐标 |

没有出现过的东西：`glClipPlane`、`glScissor`（裁剪矩形 `m_clippingRect` 只用来做包围盒剔除）、除 source-over 之外的混合模式、文字（`tglDrawText` 只在调试代码里）。

---

## 4. IR 设计

### 4.1 原则

1. **描述意图，不描述手法。** "一个填了色、边缘反走样的形状"是意图；"不反走样的填充加一圈平滑线"是 OpenGL 1.x 下的手法。手法留给后端。
2. **没有隐含状态。** 颜色、混合、反走样随每次绘制调用传入。画布上只有两样状态：当前变换和当前裁剪，都由保存和恢复成对管理。
3. **发射端不知道后端是谁。** 样式代码拿到的只有画布接口，里面查不到 GL 也查不到 Skia。
4. **变换显式。** 需要像素大小的代码向画布问，不回读任何全局状态。
5. **确定性。** 同一串 IR 调用在同一个后端上得到同样的像素。样式里的随机数沿用现在的 `TRandom`，调用次序不变。
6. **Phase 1 只做用得上的。** 图层、拾取 ID、文字是 Phase 3 的 viewer 才需要的，本稿只留出位置。

### 4.2 几何

**路径**：若干条轮廓，每条由"移动到、直线到、二次曲线到、闭合"组成，外加填充规则（非零或奇偶）。

- 区域的边界本来就是笔触中心线上的一段段二次贝塞尔（`TEdge` 是笔触加起止参数），所以路径里保留二次曲线，区域可以精确表达，不必像现在这样先按像素大小打散成折线（`stroke2polyline`）。第一步迁移仍然发折线，便于和 GL 做对照；改发精确曲线是之后单独的一步，用 golden 衡量。
- 现在的区域填充用的是"正环绕数"规则，路径只提供非零和奇偶。外轮廓逆时针、洞顺时针时两者等价。自相交的区域边界上两者会不同，见第 10 节。

**网格**：三角形列表、三角带或三角扇，顶点位置必有，逐顶点颜色和贴图坐标可选，再加一个"轮廓要不要反走样"的标志。语义是**所有三角形的并集**：重叠的地方只算一次覆盖。

- 笔触轮廓（`TStrokeOutline`，左右成对的点）就是一条三角带。变粗细的笔触没有精确的曲线形式，它继续由 `tellipticbrush.cpp` 算成折线对。
- 四边形带、四边形、多边形在发射端转成三角带或三角扇，IR 里不设四边形。

这两种几何的分工：**轮廓是数据本身的用路径，由许多小块拼成的用网格**。后端可以自由转换，比如 Skia 后端把纯色网格转成一条多轮廓路径来画（第 7 节）。

### 4.3 画笔

每次绘制调用带一个画笔，值类型：

| 字段 | 取值 | 说明 |
|---|---|---|
| 颜色 | RGBA，非预乘 | 样式现在算出来的就是非预乘的 `TPixel32`。位深见 Q5。网格带逐顶点颜色时，画笔只提供 alpha |
| 着色器 | 无；线性渐变；径向渐变；图像 | 有着色器时颜色只提供 alpha。图像着色器带平铺方式（重复、夹边）、采样方式（最近、双线性）和局部矩阵 |
| 反走样 | 开或关 | 对应 `rd.m_antiAliasing`；区域还要与上 `rd.m_regionAntialias` |
| 外扩 | 以设备像素计，默认 0 | 把形状向外长出这么多再填，拐角是圆的。用来重现现有镶边"长出半个像素"的效果，见 Q1、Q2。只对路径和纯色网格有效 |
| 混合 | 只有 source-over | 留一个枚举位置，Phase 3 再加 |

描边另带一支笔：

| 字段 | 取值 |
|---|---|
| 宽度与单位 | 本地单位，或设备像素。后者用于所有"一像素线"和中心线样式 |
| 端点与拐角 | 平头、圆头；尖角、圆角。现有 GL 线没有拐角处理，默认取平头加尖角 |
| 虚线 | 一串以设备像素计的线段长度；16 位 stipple 图案换算成它 |

描边只表达**等宽**的线。变粗细的笔触永远是网格。

### 4.4 状态

- **变换**：二维仿射（`TAffine`）。`concat` 右乘到当前变换上。3D 摄影表视图是 viewer 的事，Phase 3 在网格级处理，不进路径级。
- **裁剪**：路径加"交"或"差"两种运算，可反走样。嵌套靠保存和恢复。
- **保存与恢复**：成对，保存的是变换和裁剪。

### 4.5 操作表

| 操作 | 参数 | Phase 1 是否实现 |
|---|---|---|
| 保存、恢复 | 无 | 是 |
| 变换 | 仿射 | 是 |
| 裁剪到路径 | 路径、填充规则、交或差、是否反走样 | 是 |
| 填充路径 | 路径、填充规则、画笔 | 是 |
| 描边路径 | 路径、笔、画笔 | 是 |
| 画网格 | 网格、画笔 | 是 |
| 画图像 | 栅格、图像到本地坐标的仿射、采样方式、整体 alpha、像素是否已预乘 | 是 |
| 开始图层、结束图层 | 整体不透明度 | 否，Phase 3（洋葱皮、整组半透明） |
| 设拾取 ID | 整数 | 否，Phase 3（替代 `GL_SELECT`） |
| 画文字 | 字符串、字体、位置 | 否，Phase 3 |

查询：当前变换；像素大小（当前变换下一个设备像素合多少本地单位，替代 `tglGetPixelSize2`）；目标的整数边界（替代 `m_clippingRect` 的剔除用途）。

### 4.6 接口形态

**已实现（迁移第 0 步，2026-11-08）。** 接口在一个新的库 `tnzcanvas` 里，头文件是 `include/canvas/tcanvastypes.h`、`tcanvas.h`、`tcanvasrecorder.h`。本稿 0.1 版写的是"放在 tnzcore"，改掉的原因是所有者定下的首要目标（x86 macOS 和 iPadOS 上先跑通一套完整的技术栈）：tnzcore 链接 Qt，要到 Phase 2 才去得掉，放在里面的东西在那之前上不了 iPadOS。

- `tnzcanvas` 只用 C++ 标准库，不链接任何东西，不包含 Qt、OpenGL 和 tnzcore 的头文件。有一个测试专门读它的源码，出现别的 `#include` 就失败。
- 因此它不能用 `TAffine`、`TPointD`、`TPixel32`、`TRaster`（它们的实现在 tnzcore 里，头文件牵着 Qt）。库里有自己的一套朴素值类型：`TCanvasPoint`、`TCanvasAffine`（成员布局和乘法约定与 `TAffine` 相同）、`TCanvasColor`（非预乘、浮点，即 Q5 的决定）、`TCanvasImage`（指向 `TRaster32` 布局的像素的视图）。发射端在调用处转换；转换函数放在 tnzcore 一侧，第 2 步再加。
- `TCanvas` 的公开函数不是虚函数：它们维护变换栈，丢掉画不出东西的调用（空路径、无效网格、全透明的画笔），再转给受保护的虚函数 `onFillPath` 等，后端实现后者。这样"当前变换"和"像素大小"对所有后端只有一份实现。空路径的裁剪不丢，因为"裁到什么都不剩"和"不裁剪"不是一回事。
- 同一套单元测试在 macOS 上和 iPad 模拟器里各跑了一遍，并为 iPadOS arm64 链接成功（没有在真机上运行）。

接口的要点如下（节选自 `tcanvas.h`）：

```cpp
class TCanvas {
public:
  void save();
  void restore();
  void concat(const TCanvasAffine &aff);  // like glMultMatrix
  void clipPath(const TCanvasPath &, TCanvasFillRule, TCanvasClipOp, bool aa);

  void fillPath(const TCanvasPath &, TCanvasFillRule, const TCanvasPaint &);
  void strokePath(const TCanvasPath &, const TCanvasPen &,
                  const TCanvasPaint &);
  void drawMesh(const TCanvasMesh &, const TCanvasPaint &);
  void drawImage(const TCanvasImage &, const TCanvasAffine &imageToLocal,
                 const TCanvasImageOptions &);

  const TCanvasAffine &transform() const;
  double pixelSize() const;  // what tglGetPixelSize2() gave
  virtual TCanvasIRect deviceBounds() const = 0;

protected:
  virtual void onFillPath(const TCanvasPath &, TCanvasFillRule,
                          const TCanvasPaint &) = 0;
  // ... one on...() per call above
};
```

**tnzcore 一侧怎样接上（迁移第 2 步，2026-11-09）。**

- 样式类多了一个查询 `TColorStyle::canvasSupport()`，回答"我的笔触、我的区域能不能画在画布上"。**每个类只替自己回答，不继承**：`TSolidColorStyle` 有三十多个派生类（colorfx 的区域样式几乎都是），它们各自重写了 OpenGL 的绘制函数，如果继承了基类的"能"，就会被画成纯色。所以 `TSolidColorStyle` 只在对象恰好是它自己时回答"能"。
- 画在画布上的函数另起名字：样式的 `drawStrokeOnCanvas`、`drawRegionOnCanvas`，prop 的 `drawOnCanvas`。旧的 `drawStroke`、`drawRegion`、`draw` 还在，交互 viewer 和回退路径用它们。
- 已迁移的样式不再有自己的 OpenGL 代码：旧函数的实现就是"在当前 GL 上下文上建一个 `TGlCanvas`，调画布版本"。这样派生类调用 `TSolidColorStyle::drawRegion` 画底色时，走的也是同一份代码。
- `tcanvasDraw(canvas, rd, image)`（`tvectorcanvas.h`）是不碰 OpenGL 的 `tglDraw`：分组、区域在前笔触在后、检查模式、颜色全透明时跳过，都照原样。`tcanvasCanDraw(rd, image)` 判断一张图能不能这样画：它用到的每个样式都得"能"，而且设置里不能有只有 GL viewer 才有的东西（3D 视图、引导绘制）。不能的图整张走旧路径，见第 8 节。
- `TVectorRasterizer::rasterize(raster, image, rd)`（`tvectorrasterizer.h`）把一张矢量图用 Skia 画进栅格，画不了就返回 false，调用方照旧用 `TOfflineGL`。`TLevelColumnFx` 的矢量分支、两个粒子特效的默认粒子、tconverter 现在都先问它。它默认不启用：要在带 Skia 的构建里设环境变量 `NEXTTOONZ_VECTOR_RASTERIZER=skia`。

这是**立即式**接口：样式直接调画布的方法，和现在直接调 GL 的写法一一对应，57 个绘制函数改起来是逐行替换。另外有一个把调用录成列表的实现（`TCanvasRecorder`，录成 `TCanvasDisplayList`，可以回放到任何画布上，也可以打印成文本），它有三个用途：

- 把一串 IR 回放到另一个后端上，做 A/B 对照；
- 把 IR 打印成文本，写**不需要任何光栅器的测试**（"这个样式对这条笔触发出这些操作"）；
- 以后给 prop 做缓存（现在 prop 缓存的是轮廓点，将来可以缓存整段 IR）。

被否决的形态：只有保留式的显示列表（每次绘制都要分配，样式代码要先建列表再提交，改动更大）；把后端做成模板参数（样式类是多态的，模板会把后端类型带进每个头文件）。

### 4.7 语义约定

- **坐标**：设备坐标一个单位一个像素，原点在左下，y 向上，与 `TRaster` 和现在的 `TOfflineGL` 一致。实测：Skia 把内存里第一行当作 y=0，而 `TRaster` 的第一行正是最下面一行，所以 Skia 的设备 y 直接就是 `TRaster` 的 y，不需要翻转。
- **像素中心**在半整数处，和现在一样（`initMatrix` 里那个 0.375 的偏移早已被注释掉）。
- **目标是预乘的，画笔颜色是非预乘的。** source-over 一次完成现在两遍颜色掩码做的事。图像带一个"像素是否已预乘"的标志，对应栅格图案笔触里 `GL_ONE` 和 `GL_SRC_ALPHA` 的区别。
- **反走样是覆盖率。** 一条边把一个像素切掉一半，这个像素的 alpha 就是一半（实测 128）。关掉反走样时是全有或全无（实测）。
- **颜色函数（`TColorFunction`）留在发射端。** 现在每个样式自己对颜色调 `(*cf)(color)`，迁移时原样保留。四个样式现在不应用它（附录 A 的"备注"列），迁移时不顺手修，保持行为不变。
- **alpha 测试没有对应物。** `glAlphaFunc(GL_GREATER, 0)` 丢弃的是 alpha 为 0 的片元，在 source-over 下本来就不产生影响。

---

## 5. 样式与区域怎样映射到 IR

### 5.1 核心遍历

`tglDraw(rd, 矢量图)` 变成一个不碰 GL 的函数，结构不变：按组遍历，区域在前笔触在后，检查模式替换样式，颜色全透明时跳过。两遍颜色掩码、`glPushAttrib`、alpha 测试删掉。prop 的 `draw(rd)` 多一个画布参数；`rd.m_aff` 用保存、变换、恢复包住；像素大小向画布问。

### 5.2 纯色笔触

一次画网格：`TStrokeOutline` 的点对作为三角带，纯色，轮廓反走样。镶边和 stencil 都不再需要：并集语义保证自相交处不加深（实测：三个首尾相接的四边形再叠一块，半透明填充，沿途每个像素的 alpha 都恰好是画笔的 alpha；共享边穿过像素中间也没有接缝）。

要注意的一点：三角带里方向翻转的小块（笔触急转弯内侧会出现）必须先统一绕向再合并，否则会互相抵消成洞（实测）。这件事由后端在"网格转路径"时做，发射端不管。

### 5.3 纯色区域

一次填充路径：外轮廓和各个洞是同一条路径的多条轮廓。边界那圈平滑线删掉，换成画笔的反走样属性，是否外扩由 Q1 决定。

### 5.4 各族样式

| 族 | 样式 | 映射 |
|---|---|---|
| 只改轮廓 | `MovingSolidColor`、`TRubberFillStyle`、`ArtisticSolidColor`、`TMatrioskaStrokeStyle`、`TVectorBrushStyle`、MyPaint 笔刷的矢量画法 | 修改器照旧在 CPU 上改轮廓，然后走 5.2 或 5.3 |
| 纯色块，多数带一像素线 | `TRope`、`TBraid`、`TZigzag`、`TSin`、`TFrieze2`、`TDualColor2`、`TSawTooth`、`TChalkStroke2`、`TMultiLine2` | 色块是网格。两侧的一像素线和 `GL_POLYGON_SMOOTH` 是镶边，删掉 |
| 一像素线就是内容 | `TSketch`、`TTissue`、`TGraphicPen`、`TFur`、`FlowLine`、`TChain`、`TSpray`、`TBubble` | 描边路径，笔宽一个设备像素。逐顶点颜色的（`TFur`）加线性渐变 |
| 逐顶点颜色的带子 | `TDottedLine`、`TCrystallize`、`TBiColor`、`TBlend2`、`TTwirl`、`TLongBlend2` | 画网格，带逐顶点颜色。边缘 alpha 本来就淡到 0 的不需要轮廓反走样 |
| 光照 | `TNormal2StrokeStyle` | 把固定管线的光照公式在 CPU 上算成逐顶点颜色，再画网格 |
| 区域里的图案 | `TDottedFill`、`TCheckedFill`、`TChalkFill`、`TChessFill`、`TMosaicFill`、`TPatchFill`、`TStripeFill`、`TCircleStripeFill` | 先填充区域（底色），保存，裁剪到区域，画图案（网格或一批小路径），恢复。后两个的嵌套掩码随镶边一起消失 |
| 渐变填充 | `TLinGradFillStyle`、`TRadGradFillStyle` | 填充区域路径，画笔带线性或径向渐变着色器。不再需要裁剪 |
| 区域阴影 | `ShadowStyle`、`ShadowStyle2`、`TPointShadowFillStyle` | 裁剪（第一个还要反向裁剪）加描边路径、网格、一批小圆 |
| 贴图 | `TTextureStyle`、`TRasterImagePatternStrokeStyle` | 填充路径或画网格加图像着色器；印章用画图像 |
| 递归 | `TVectorImagePatternStrokeStyle` | 每个印章：保存、变换、递归调用核心遍历、恢复。太小时的替代画法是带虚线的描边路径 |
| 中心线 | `TCenterLineStrokeStyle` | 描边路径，笔宽以设备像素计，可带虚线；宽的分支是网格。**已迁移，照搬了今天的画法**：带子逐个四边形画、硬边，两侧各一条一像素线（按本表的分类这两条线是镶边，本该删掉换成反走样的网格）。先照搬是为了让 GL 路径逐像素不变；Skia 下它也是这个样子，改不改等看过图再说 |
| 栅格样式 | `TAirbrushRasterStyle`、`TBlendRasterStyle`、`TNoColorRasterStyle` | 不经过 IR，本来就在 CPU 上（`common/trop/tropcm.cpp`） |

依赖驱动的两处要改成常量：`TPointShadowFillStyle` 把参数映射到驱动报告的点大小范围上，`TCenterLineStrokeStyle` 把线宽夹到驱动报告的线宽范围里。这正是同一场景在不同显卡上结果不同的来源之一（已知问题第 18 条 c）。

### 5.5 逐顶点颜色的网格怎样反走样

实测：Skia 的 CPU 后端画网格时会插值颜色，但不反走样边缘，画笔上开了也没用；每个三角形还是各自混合的，重叠处会混两次。

本稿 0.2 版写的办法是"先用网格的并集轮廓做一次反走样的裁剪，再画网格"。实现时发现这只对了一半：裁剪给出了边缘像素的覆盖率，但 Skia 只给**中心落在三角形里**的像素上色。被并集盖住不到一半的边缘像素中心在外面，拿不到颜色，结果是形状缩了四分之一个像素，外侧那一半的反走样丢了。

Skia 后端现在的做法（`tnzskia/tskiacanvas.cpp` 里的 `drawShadedMesh`，有像素测试）：

1. 把网格画进一个图层，三角形之间用"替换"而不是"混合"，重叠处后画的盖掉先画的；图层最后整体混合一次，画笔的 alpha 在这时乘上去。这样重叠处只算一次。
2. 需要轮廓反走样时，图层先裁剪到并集轮廓（反走样）。
3. 在画三角形之前，先沿每条边铺一条两像素宽的带子、在每个顶点铺一个两像素见方的小块，颜色取边上和顶点上的颜色。边缘像素的中心落在这些带子里，于是有了颜色；内部的像素随后被三角形盖掉，带子只在边缘露出来。

这样边缘像素的颜色是"离它最近的边上的颜色"，不会外推出范围。代价是每个三角形多画十来个小三角形和一个图层；性能没有测过。纯色网格不走这条路，直接当路径画。

---

## 6. 什么留在栅格侧

不经过 IR、Phase 1 也不动的：

- 特效树本身：`TRasterFx::compute`、tile、150 多个 CPU 特效。
- 栅格层的装载和重采样（`TRop::resample`、`quickPut`）。
- 色指数栅格（CM32）到全彩的转换，以及其中的栅格样式特效（`TRop::convert`、`computePaletteFx`）。
- Sandor 特效（`toonzlib/sandor_fxs/`）。

经过 IR 但产物是栅格的：

- 矢量层栅格化（2.1 的第 1 处）：产物是预乘的 8 位 tile，和现在一样。
- `vectorToToonzImage`（第 2 处）：它要的不是颜色，而是每条笔触的覆盖率（tone）和每个区域的归属。后端需要能画到**只有覆盖率的 8 位目标**上；现在"画黑色、读 R 通道"的做法随之消失，每条笔触建一个 GL 上下文的开销也消失。
- 默认粒子（第 3 处）、图案源帧（第 4 处）：普通的矢量绘制。

特效改为用 IR 的：

- `PlasticDeformerFx`（第 5 处）：输入 tile 作为图像着色器，变形后的三角网格带贴图坐标，一次画网格。现有实现另外用贴图线段描网格的外边，属于镶边，由网格的轮廓反走样取代。
- `Iwa_FlowPaintBrushFx`（第 6 处）：每条笔刷是一条贴图三角带，颜色与贴图相乘，预乘混合。同样是画网格。

`ShaderFx`（第 7 处）不动。

---

## 7. 两个后端怎样消费 IR

### 7.1 Skia CPU 后端

**已实现（迁移第 1 步，2026-11-09）。** 库 `tnzskia`，类 `TSkiaCanvas`（`include/canvas/tskiacanvas.h`），只有它包含 Skia 的头文件。和 `tnzcanvas` 一样不依赖 Qt 和 tnzcore，所以它的输入不是 `TRaster32P`，而是一块调用者自己的像素：指针、宽、高、行距，布局就是 `TRaster32` 的布局（BGRA、预乘、第一行在最下面）。画完即得结果，没有拷贝。用 `WITH_SKIA` 打开构建，Skia 从哪里来见 `doc/nexttoonz_phase1_skia.md`。

| IR 操作 | Skia 的做法 |
|---|---|
| 填充路径 | `SkPathBuilder` 建路径，`drawPath`，解析式反走样 |
| 外扩 | 把路径变到设备坐标再做，所以外扩量在任何变换下都是设备像素。非零规则用 Skia 的"描边并填充"，一次覆盖计算；奇偶规则先把描边带做成路径，再用 PathOps 和形状求并 |
| 描边路径 | `drawPath` 加描边画笔。笔宽以设备像素计时，路径先变到设备坐标再描，不均匀缩放不会把线压扁；虚线长度始终按设备像素算 |
| 画网格，纯色 | 每个三角形统一绕向后作为一条轮廓，全部放进一条路径，非零规则填充。三角带里每隔一个三角形绕向就相反，所以这一步对普通的带子也是必需的 |
| 画网格，逐顶点颜色或贴图坐标 | 见 5.5 |
| 画图像 | `drawImage`，像素不拷贝；图像自己说明是预乘还是非预乘 |
| 裁剪 | `clipPath`，交或差，可反走样 |
| 渐变、图像着色器 | Skia 的着色器；坐标是形状所在的坐标。m144 里渐变在 `SkGradientShader`，m148 里搬到了 `SkShaders`，两边都支持 |

实现时碰到、测试里钉住的三件事：

- **"描边并填充"认第一条轮廓的方向。** Skia 把第一条轮廓的绕向当成整个形状的绕向。洞排在前面时，外扩的那一圈会和填充互相抵消，沿轮廓留下一圈没填的缝。后端在做外扩时把面积最大的轮廓排到最前面。剩下一个条件：非零规则下，一条路径被填充的各部分要绕向一致（外轮廓一个方向，洞相反）。区域的轮廓本来就是这样；发射端把现在的"正环绕数"规则转成非零规则时也要保证这一点（见第 10 节）。
- **一条路径里相邻三角形的公共边穿过像素时，两半加起来是 254 而不是 255。** 差 1，肉眼看不出，golden 的容差之内；但"纯色网格内部严格等于画笔的 alpha"这句话要打这个折扣。
- **圆拐角是近似曲线。** 外扩一个像素的矩形，紧邻的外侧像素 alpha 是 1 而不是 0。

没有上下文、没有线程亲和性、没有全局锁。现在每个 `TLevelColumnFx` 实例用一把互斥锁把矢量渲染串行化，换后端之后这个限制可以去掉（是否去掉另议）。

45 个像素测试在 macOS 和 iPad 模拟器（iOS 15.5）里都通过，一张用到全部操作的小图在两边逐位相同（哈希 `8be1d3f71a65e061`）；为 iPadOS arm64 链接成功，没有在真机上运行。同一张图在 macOS 上用 m144 和 m148 画也逐位相同；Linux（GCC 13，m148，带 ASan/UBSan/LSan）上 45 个测试同样通过，但那张图有 355 个像素和 macOS 不同，最大相差 3（`doc/nexttoonz_phase1_skia.md` 3.5）。

`vectorToToonzImage` 需要的"只有覆盖率的 8 位目标"还没有做，留到迁移第 6 步。

### 7.2 GL 桥接后端

**已实现（迁移第 1 步；第 2 步改过，2026-11-09）。** 库 `tnzglcanvas`，类 `TGlCanvas`（`include/canvas/tglcanvas.h`）。它只链接 OpenGL 和 `tnzcanvas`，不链接 tnzcore：stencil 掩码通过一个四个函数的接口交给它，tnzcore 用 `TStencilControl` 实现这个接口。现在是 tnzcore 链接它（旧的 `tglDraw` 要靠它画已迁移的样式）；到第 9 步核心库去掉 GL 时，它连同 `tglDraw` 一起留在 viewer 一侧。第 1 步写的是反过来的依赖方向，行不通：tnzcore 里的样式要用它。

它画进当前的 GL 上下文，坐标系就是创建时的 modelview 矩阵，像素大小和 `tglGetPixelSize2()` 一样从 modelview 读回（GL 内部按单精度存矩阵，样式按这个数决定曲线切多细，要逐像素一致就得用同一个数）。

它有两种工作方式：

- **自己管状态**：自己做两遍混合得到预乘结果，按每个画笔设置线的平滑，画完把所有 GL 属性恢复原样。测试和以后独立使用时是这种。
- **用调用者的状态**：旧的绘制代码（`tglDraw` 及其下游）自己在样式外面设好了混合、颜色掩码、线平滑，并且自己画两遍。这时画布什么都不设，只做被它取代的那段代码做过的事。已迁移的样式经由 `tglDraw` 到达时用的是这种，这是"逐像素与改动前相同"的前提。

| IR 操作 | GL 的做法（即今天的做法） |
|---|---|
| 填充路径 | 曲线打成折线，GLU 细分，不反走样地填；开反走样时沿每条轮廓再画一圈一像素的平滑线 |
| 外扩 | 不看。那圈平滑线有一半在形状外面，等于固定外扩了半个像素 |
| 画网格，纯色 | 三角带的偶数和奇数顶点是它的两条边，沿这两条边画一条一像素宽、向外淡出的带子（拐角规则从 `tsimplecolorstyles.cpp` 的 `OutlineBuilder` 原样搬来）；半透明时用 stencil 保证每个像素只画一次。三角形列表和三角扇没有这条带子 |
| 画网格，逐顶点颜色或贴图坐标 | 逐个三角形画，没有带子，重叠处混两次。这是今天这些样式的行为 |
| 描边路径 | `GL_LINE_STRIP` 加线宽；虚线换算成 16 位 stipple（总长是 16 像素的整数倍时精确）；没有端点和拐角 |
| 画图像 | 贴图四边形 |
| 裁剪 | `TStencilControl` 的掩码，不反走样；和它外面的代码用的掩码可以嵌套 |
| 渐变、图像着色器 | 都变成贴图：图像原样上传，渐变打成 256 级，查找位置由贴图矩阵算 |
| 预乘的目标 | 每个操作画两遍：先按 alpha 混合颜色通道，再累加 alpha 通道。源本身已预乘的图像只要一遍 |

另有两处是为了和旧渲染器逐像素一致加的：偶数个顶点的三角带按四边形带（`GL_QUAD_STRIP`）发出，因为旧代码发的就是它，不凸的四边形怎样拆成三角形由驱动决定；不反走样时笔触两侧画的是两条线而不是一圈。

**它和改动前的渲染器逐像素相同（第 2 步验证，2026-11-09）。** `TSolidColorStyle` 和 `TCenterLineStrokeStyle` 自己的 GL 代码已经删掉，它们现在只经过这个后端到达 OpenGL。在本机（AMD Radeon Pro 5700）上：

- 能在 macOS 上跑的 24 个 golden 场景，全部 262 帧，与改动前的渲染逐位相同；
- `vector_styles`（全部注册的样式类，其中派生自 `TSolidColorStyle` 的都要调基类画底色或笔触）7 帧逐位相同，对照的是 Phase 0 的构建；
- 一张专门的测试图在 9 种设置下逐位相同：半透明并自相交的笔触、关掉反走样、虚线和宽的"固定宽度线"、零粗细笔触、透明度检查和墨线检查、viewer 的设置、不均匀缩放。对照的同样是 Phase 0 的构建。

Linux 虚拟机（Mesa 软件光栅器）上做了同样的对照，对照的是改动前的同一个构建目录：全部 29 个 golden 场景的 282 帧，改动前后的文件逐字节相同；测试图的 9 种设置逐位相同。

Linux 上第一次对照时有一种设置不同：不均匀缩放下，带虚线的"固定宽度线"两侧的虚线相位偏了几个像素。原因是 GL 状态的先后：旧函数在画带子之前就打开了 stipple，画布只在画那两条线时打开。按 OpenGL 规范这没有区别，本机显卡上也确实没有区别，但 Mesa 画一条起点在画面之外的虚线时，虚线从哪里开始会受它影响（为什么，我没有查清）。现在 `TCenterLineStrokeStyle` 的 GL 入口照旧在最前面打开 stipple，两条线也合成一次调用，之后 Linux 上逐位相同。这件事说明两点：一是"逐像素不变"只能靠对照得出，靠读代码推不出来；二是 macOS 上不跑的那五类场景以外，Linux 的对照不能省。

### 7.3 两个后端差多少

同一段 IR 录一次，分别回放到两个后端，逐像素比（`tests/canvas_gl/test_backends_agree.cpp`）。两个后端本来就不该逐像素相同：一个算覆盖率，一个整像素地填再用线或带子把轮廓弄软。它们必须一致的是东西在哪里、是什么颜色。本机（AMD Radeon Pro 5700，macOS）的实测，每张图 240x120：

| 图 | 每像素最大通道差的平均 | 差超过 32 的像素 | 差超过 96 的像素 |
|---|---|---|---|
| 相邻区域、带洞的区域 | 1.43 | 0.23% | 0 |
| 笔触轮廓，不透明 | 0.50 | 0.04% | 0 |
| 笔触轮廓，半透明 | 0.21 | 0 | 0 |
| 一像素线、三像素线、虚线 | 0.91 | 1.08% | 0.02% |
| 渐变、图像着色器、图像、逐顶点颜色 | 2.89 | 2.57% | 1.10% |
| 嵌套裁剪 | 1.09 | 0.66% | 0.18% |
| 全部操作的一张图（256x192） | 3.05 | 2.61% | 0.77% |

差异全部在边缘上。这些图里 Skia 一侧的画笔都带半个像素的外扩，对应 GL 那圈固定的镶边；不带外扩时，GL 的形状比 Skia 的胖半个像素，笔触轮廓那一行会从 0.04% 变成 3.9%。这就是 Q1、Q2 的那个参数在图上的样子。

已知的、不属于"边缘软硬"的差别：

- **逐顶点颜色的插值空间不同。** Skia 先把顶点颜色乘上 alpha 再插值，GL 插值之后才混合。相邻顶点颜色不同、alpha 也不同时，三角形内部的颜色不一样；颜色相同只是 alpha 淡到 0（多数淡出样式的情况）时两边一致。上表的图里避开了这种情况；第 5 步迁移逐顶点颜色的样式时要逐个看图。
- **宽线的端点和拐角。** GL 的线没有端点和拐角处理，折线的各段在拐角处重叠，半透明时混两次；Skia 按笔的设置画。
- **裁剪的边缘。** GL 是 stencil，一像素的锯齿；Skia 可以反走样。
- **图像和贴图的边缘。** Skia 反走样，GL 不。

这套测试默认不构建（`WITH_CANVAS_GL_TESTS`），因为它要窗口系统给的 GL 上下文，而且平滑线画成什么样随驱动而变；阈值取的是本机数字的两倍左右。在 Linux 虚拟机里（Mesa 的软件光栅器，`xvfb-run`）它也全部通过，数字和本机几乎一样：差超过 32 的像素依次是 0.22%、0.04%、0、0.90%、2.52%、0.66%、2.64%。

### 7.4 以后的网格级后端

Phase 3 的 QRhi 后端和 Phase 4 的 Metal 后端消费的是网格级 IR。从路径级降到网格级的那一步（细分路径、把反走样变成几何镶边或交给 MSAA）就是 7.2 里 GL 桥接后端做的事，只是输出从 GL 调用变成三角形缓冲。所以桥接后端不是一次性的脚手架：它的细分和镶边逻辑是网格级降级器的前身。本稿不展开网格级。

---

## 8. 调用点的迁移顺序

每一步都要 golden 通过才进下一步。步骤 2 之后的每一步都可以单独合并。

| 步 | 内容 | 验收 |
|---|---|---|
| 0（已完成） | IR 类型、画布接口、记录器和文本打印，放进不依赖 Qt 的新库 `tnzcanvas` | 单测，不需要光栅器：macOS 上 25 个通过；iPad 模拟器里 24 个中 23 个通过、1 个跳过 |
| 1（已完成） | Skia 后端库 `tnzskia`（同样不依赖 Qt）和 GL 桥接后端 `tnzglcanvas`；同一段 IR 分别过两个后端的一致性测试 | Skia 后端 45 个像素测试，macOS 和 iPad 模拟器都通过；GL 后端 15 个测试；两个后端的差异见 7.3 |
| 2（已完成，待看图） | 核心遍历、三个 prop、`TSolidColorStyle`、`TCenterLineStrokeStyle` 改发 IR。`TLevelColumnFx` 矢量分支、默认粒子、tconverter 加运行时开关在 GL 和 Skia 之间切换 | 默认路径（GL，经桥接后端）与改动前逐位相同，见 7.2；Skia 路径在 `vector_basic`、`pegbar_hierarchy`、`subxsheet`、`sound_column`、`stage_showcase`、`particles_basic` 上出了对照图，两种线重各一套，等所有者定 Q2 |
| 3 | tnzcore 与 toonzlib 里的图案和贴图样式：栅格图案、矢量图案、矢量笔刷、`TTextureStyle`、MyPaint | 同上，加 `vector_styles`（它已经覆盖全部注册的样式类） |
| 4 | colorfx 的 16 个区域样式，按 5.4 的族分批 | `vector_styles` |
| 5 | colorfx 的 25 个笔触样式，按 5.4 的族分批；`TNormal2` 和 `TPointShadow` 最后 | `vector_styles` |
| 6 | `vectorToToonzImage`（覆盖率目标）、`TVectorImage::render`、`trasterimageutils`、导出、追踪、网格化弹窗、脚本绑定、样式图标 | 需要新场景：带调色板滤镜的矢量层（现有场景没有走到 `applyTzpFxsOnVector`） |
| 7 | `PlasticDeformerFx`、`Iwa_FlowPaintBrushFx` 改用画网格 | `plastic_basic`、特效扫场里的对应帧 |
| 8 | 摄影表画法 `ToonzScene::renderFrame`（场景图标、缩略图） | 见 Q4 |
| 9 | 默认后端切到 Skia；基线换成本机可生成的；tnzcore、toonzlib、colorfx、tnzext 去掉 GL 链接 | Phase 1 的完成判据 |

第 2 步到第 5 步之间，调色板里可能同时有已迁移和未迁移的样式。做法是**整张图回退**：一张矢量图用到任何未迁移的样式，就整张走旧的 `TOfflineGL` 路径。这样任何时刻每张图只由一个光栅器画，不会出现半张 GL 半张 Skia。

第 2 步是整个 Phase 1 的关键：它覆盖了实际作品里绝大多数的矢量内容（纯色笔触和纯色区域），外观问题 Q1、Q2 在这一步就要定。

---

## 9. 需要所有者决定的问题

**所有者的决定（2026-11-08）：Q1 到 Q6 都按本节写的倾向执行。** 即 Q1 取 (a) 区域外扩半个像素；Q2 在迁移第 2 步把两种线重都渲出来再看图定；Q3 取 (a)，GL 桥接后端留到 Phase 3、放在 viewer 一侧；Q4 没有写倾向，暂按 (b) 留到 Phase 3，到 Phase 1 末尾再确认；Q5 取 (b)；Q6 迁移时原样保留并另记一条已知问题。下面保留原文，作为决定的依据。

**Q1 相邻区域的接缝怎样处理。** 两个共用边界的区域是两次绘制。解析式反走样下，边界像素各被盖住一半，后画的混在先画的上面，背景会透出四分之一，形成一条发丝缝（实测：两个各盖半个像素的黑色矩形，边界像素 alpha 只有 191）。Toonz 的作业习惯里，用零粗细的线分隔上色区域很常见，这种地方没有笔触盖住缝。现有 GL 代码没有这个问题，因为它的填充不反走样，又在边界上叠了一条平滑线。选项：

- (a) 区域外扩半个像素，即画笔的"外扩"属性（实测可消除接缝）。外观最接近现状：区域略微偏胖，被后画的邻居和笔触盖住。半透明区域的重叠处会略深，现状也是如此。
- (b) 一组区域先不反走样地画到每边 4 倍超采样的图层上再缩小。没有接缝也没有偏胖，代价是这一步要填 16 倍的像素（速度没有测过）。
- (c) 不处理，接受接缝。

倾向 (a)，并把 (b) 留作以后的质量选项。

**Q2 的对照图和数字（迁移第 2 步，2026-11-09）。** 六个场景各用三种方式渲了一遍：今天的 OpenGL、Skia 按真实宽度、Skia 把笔触外扩半个像素。参考场景是 320x180，笔触只有一两个像素宽，差别在这个分辨率下最明显。和 OpenGL 相比（叠在白底上看，逐像素取最大通道差）：

| 场景 | 真实宽度：差超过 32 的像素 | 真实宽度：墨量变化 | 外扩半像素：差超过 32 的像素 | 外扩半像素：墨量变化 |
|---|---|---|---|---|
| `vector_basic` | 2.08% | -4.7% | 0.01% | +0.1% |
| `pegbar_hierarchy` | 0.80% | -2.6% | 0.01% | +0.1% |
| `subxsheet` | 0.95% | -3.9% | 0.01% | 0 |
| `sound_column` | 1.81% | -17.4% | 0.01% | +0.3% |
| `stage_showcase` | 3.23% | （背景不透明，量不出） | 0.08% | |
| `particles_basic` | 0.48% | （同上） | 0.10% | |

"墨量"是整张图 alpha 的总和。按真实宽度画，线明显变细变淡，细线（`sound_column`）少了六分之一的墨；外扩半个像素之后和今天的样子几乎分不出来。对照图在 `toonz/build-phase1/review/step2/`（不进版本库）：每个场景一张三联图，另有三张放大的局部。

开关是环境变量 `NEXTTOONZ_STROKE_GROW`（像素数，默认 0），只对 Skia 路径起作用。所有者定下之后，把选中的值写成默认值，开关可以留着也可以去掉。

**Q2 笔触要不要保持现在的"线重"。** 现在的镶边画在轮廓之外，等于每侧多出大约半个像素。解析式反走样按几何真实宽度画，同一条线会比现在细一点、淡一点；在 320x180 的参考场景上看得出来，在 2K 成片上不明显。选项：(a) 按几何真实宽度；(b) 笔触也外扩半个像素，保持现在的线重。两种都只是一个参数。建议第 2 步把两种都渲出来，由所有者看图决定，而不是现在凭推测定。

**Q3 GL 桥接后端的定位。** 计划写的是"放在独立的可选模块里……Phase 1 结束后删除或仅作开发工具"。但样式类改成发 IR 之后就不再发 GL，而交互 viewer 到 Phase 3 才离开 GL。这期间 viewer 画矢量样式只有三条路：

- (a) viewer 通过 GL 桥接后端消费 IR。桥接后端因此不是可选的，它要活到 Phase 3，位置在 toonzqt 或一个新的 viewer 侧库里，核心库不链接它。
- (b) viewer 把矢量先用 Skia 画成栅格再贴图上屏。缩放、洋葱皮、编辑中的笔触都要重新栅格化，交互性能有风险。
- (c) 样式类同时保留 GL 和 IR 两套绘制代码直到 Phase 3。两套代码必然走样。

倾向 (a)，并相应修改计划里 Phase 1 的那一条和"tnzcore、toonzlib、colorfx、tnzext 不再链接 GL"的判据（判据本身不受影响，因为桥接后端不在这四个库里）。

**Q4 摄影表画法的图标路径算 Phase 1 还是 Phase 3。** 场景图标和 xsheet 缩略图走 `ToonzScene::renderFrame`，它用的是 viewer 的 `Stage::RasterPainter`。把它切到 CPU 后端就要让 `Stage::Visitor` 的绘制器发 IR，而这是计划里 Phase 3 的工作项。选项：(a) 提前到 Phase 1 末尾做，工作量粗估两到三周；(b) 留到 Phase 3，Phase 1 结束时 toonzlib 里还剩这一处 GL，"toonzlib 不再链接 GL"的判据要打折扣；(c) Phase 1 里给图标另写一条走渲染管线的路（用 `TRenderer` 渲一帧小图），代价是图标外观从摄影表视图变成渲染结果。tcomposer 不走这条路，所以它不影响"tcomposer 无 GL 完成全部场景"的判据。

**Q5 矢量栅格化的位深和色彩空间。** 现在矢量永远按 8 位、非线性空间栅格化，64 位和线性渲染里也是先 8 位再展宽。Skia 的 CPU 后端可以直接画 16 位或浮点目标。选项：(a) IR 的颜色定为 8 位，行为与现在完全一致；(b) IR 的颜色用浮点，Phase 1 的后端仍按 8 位画，但留出以后在 64 位渲染里直接画 16 位的余地；(c) Phase 1 就让 64 位渲染里的矢量按 16 位画，这会改变 64 位渲染的结果（渐变样式的色带会减轻）。倾向 (b)。

**Q6 样式里依赖帧号的随机。** `TBubbleStrokeStyle` 用静态变量 `TColorStyle::m_currentFrame` 给随机数播种，等于通过全局状态把帧号传进绘制。迁移时可以原样保留，也可以改成从 `TVectorRenderData` 显式传入。后者更干净，但 `m_currentFrame` 由谁在何时设置我还没有追查，改之前要先确认多线程渲染下现在的行为是否本来就有问题。倾向迁移时原样保留，另开一条已知问题。

---

## 10. 风险与未验证事项

- **交互 viewer 的窗口没有人看过。** 第 2 步之后，应用里画纯色笔触和区域走的也是桥接后端。能离屏验证的都验证了：viewer 的设置下逐像素没有变（7.2）；摄影表画法（`ToonzScene::renderFrame`，用的就是 viewer 的绘制器）在五个场景的十帧上与 Phase 0 的构建逐位相同，其中包括 `vector_styles`。但真正的 viewer 窗口（缩放、洋葱皮、工具绘制中的笔触、3D 视图）我没有办法操作，没有验证；每画一条笔触要多建一个画布对象、多读两次矩阵，对交互帧率的影响也没有测。
- **轮廓计算曾经背着调用者从 OpenGL 读像素大小。** `TOutlineUtil::makeOutline` 内部调 `tglGetPixelSize2()`，没有 GL 上下文时直接崩溃（Skia 路径第一次运行时就是这样发现的）。现在由 prop 把像素大小传进去。别的样式里还有同类的隐藏读取（`tvectorbrushstyle.cpp`、栅格图案样式），迁移到它们时要逐个改。
- **"固定宽度线"样式的宽度下限来自驱动。** `TCenterLineStrokeStyle` 把宽度夹到驱动报告的线宽范围里。本机和 Mesa 报告的下限都是 1，于是它"宽度不超过半个像素时画成一条线"的分支在这两个驱动上从来走不到，画出来的总是一条带子，宽度参数小于 1 时也按 1 算。画布版本把下限固定为 1，和这两个驱动的结果一致；这是不是样式作者的本意我不知道。
- **正环绕数换成非零规则，在现有场景里没有造成差别**，但 `TRegion` 的边界会不会出现负环绕数的小块仍然没有调查（下一条）。
- **逐顶点颜色在两个后端里插值空间不同**（7.3）。要让 Skia 一侧按非预乘插值，得自己把三角形拆细或者换一种画法，没有做。
- **Skia 路径现在比 OpenGL 慢，只有一个数据点。** 一张测试图（4 条笔触、4 个区域，半透明的占一半）放大到 1920x1760，本机单线程：OpenGL 画加读回每帧约 11 毫秒，Skia 约 40 毫秒（外扩半像素时 36 到 40 毫秒）。去掉"固定宽度线"之后数字不变，所以时间花在纯色笔触和区域上。我的判断是笔触网格的画法：每个三角形一条轮廓，放大后一条笔触有几千条轮廓，扫描线上同时活动的边很多；这是推测，没有做剖析。可以试的办法：按四边形而不是三角形出轮廓，或者只出带子的外边界。另外发射代码每次绘制都把轮廓点拷进一个新的路径或网格。Skia 路径没有上下文、可以多线程，这一点还没有利用。这些都没有做；在默认后端切到 Skia（第 9 步）之前要用真实场景量过并处理。
- **正环绕数与非零规则的差别。** 现有区域填充用 `GLU_TESS_WINDING_POSITIVE`。区域边界自相交、出现负环绕数的小块时，现在不填，换成非零规则会填。我没有调查 `TRegion` 的边界是否保证不出现这种情况。第 2 步用 GL 桥接后端对照时会暴露出来；真出现的话，在发射端用符号面积规整轮廓方向，或者在 Skia 后端用 PathOps 处理。
- **arm64 上的像素没有验证。** x86_64 上，macOS（Apple clang）与 Ubuntu（GCC 13）画同一个小场景，在各自的本机像素格式下逐位相同，指定 BGRA 时有 5 个像素差 1；换成用到全部操作的图，有一成多的像素不同，最大相差 3（`doc/nexttoonz_phase1_skia.md` 3.5）。但 Skia 的光栅管线按 CPU 指令集分派（SSE、AVX2、NEON），arm64 没有跑过。计划对 golden 的要求是容差内一致，不要求逐位；"基线合并成一份、本机可生成"仍然需要 arm64 上的实测。spike 的最后一个测试会打印场景哈希并能导出像素，可以拿来比。
- **网格并集转路径的规模。** 一条长笔触的轮廓可以有上千个四边形，全部作为独立轮廓放进一条路径。Skia 的扫描线填充对边数是线性到线性对数的，预计不成问题，但没有测过大场景的性能。备选：先把三角带的外边界提取成一条轮廓（自相交时仍需并集语义）。
- **光照样式的精确重现。** `TNormal2StrokeStyle` 依赖固定管线的光照公式（环境光、漫反射、镜面反射、shininess）。在 CPU 上重算可以做到公式一致，但 GL 实现之间本来就不一致，没有"正确答案"可对。
- **一像素线从镶边里分辨出来要逐类判断。** 5.4 的表按代码意图把线分成"镶边"和"内容"两类，分错的后果是某个样式变粗或者丢线。每个样式迁移时都要看图确认。
- **`vectorToToonzImage` 没有 golden 覆盖。** 现有 29 个场景没有带调色板滤镜或 Sandor 特效的矢量层，第 6 步之前要先补场景并从当前实现取基线。
- **Skia 的 API 变动。** m148 里 `SkPath` 已经没有编辑方法，路径要用 `SkPathBuilder` 建；渐变的工厂函数也从 `SkGradientShader` 搬到了 `SkShaders`。Skia 没有稳定 API 的承诺，所以只有 `tnzskia` 一个库接触它，升级时改动范围限于这个库。
- **图案样式的贴图现在每次绘制都重新上传、从不缓存。** 换成图像着色器后是否需要缓存、缓存在哪一层，本稿没有设计。

---

## 11. 附录 A：样式类清单

来源：自动化代码调查（逐个读过每个绘制函数体），文件与行号指向 `6d043a367`。SS 是 `colorfx/strokestyles.cpp`，RS 是 `colorfx/regionstyles.cpp`，TS 是 `common/tvrender/tsimplecolorstyles.cpp`。"标准掩码"指：先在 stencil 里画出区域（同时画或不画底色），再只在区域内画图案。

### A.1 colorfx 笔触样式（26 个，其中 1 个仅调试构建）

| 类（tag） | 绘制入口 | GL 图元与状态 | 备注 |
|---|---|---|---|
| TFurStrokeStyle (103) | SS:236 | 每根毛一条两顶点线带，颜色到透明白的逐顶点渐变 | `TRandom` 在预计算里 |
| TChainStrokeStyle (104) | SS:323 | 显示列表里的八边形线带，逐环压栈变换；线段连接 | 太细时改画中心线（按像素大小切换） |
| TSprayStrokeStyle (106) | SS:511 | 每个粒子一个线圈（`tglDrawCircle`，分段数随像素大小变） | 每圈随机 alpha；最小粗细随像素大小 |
| TGraphicPenStrokeStyle (107) | SS:687 | `GL_LINES`；粗细为 0 时 `GL_POINTS` | GL 枚举值存在缓存数据里 |
| TDottedLineStrokeStyle (111) | SS:937 | 每段一条 8 顶点四边形带，跨宽度 alpha 为 0、满、满、0 | 最小粗细随像素大小 |
| TRopeStrokeStyle (108) | SS:1074 | 顶点数组：8 边形 `GL_POLYGON` 加线带 | 填充用样式色，线用黑色 |
| TCrystallizeStrokeStyle (109) | SS:1246 | 整条一条四边形带，每个截面随机 alpha；`GL_LINES` 边 | 绘制时也取随机数 |
| TBraidStrokeStyle (136) | SS:1466 | 每股一个四边形 `GL_POLYGON` 加两条线带 | 三种颜色 |
| TSketchStrokeStyle (113) | SS:1608 | 每条随机子笔触一条线带 | |
| TBubbleStrokeStyle (114) | SS:1699 | 线圈 | 第二个随机数发生器由 `m_currentFrame` 播种，随帧变化 |
| TTissueStrokeStyle (117) | SS:1882 | `GL_LINES` | |
| TBiColorStrokeStyle (135) | SS:1945 | 两条线带；四边形带，两侧各一种颜色 | 跨宽度渐变 |
| TNormal2StrokeStyle (120) | SS:2136 | 两条线带；每段 6 顶点四边形带；固定管线光照、材质、逐顶点法线 | 不调 `glColor` |
| TChalkStrokeStyle2 (123) | SS:2425 | 显示列表里的方块，逐块平移 | 每块随机 alpha |
| TBlendStrokeStyle2 (125) | SS:2696 | 一条四边形带，逐顶点 alpha | 最小粗细随像素大小 |
| TTwirlStrokeStyle (126) | SS:2828 | 四边形带，沿长度在黑与样式色之间渐变；`GL_LINES` | |
| TSawToothStrokeStyle (127) | SS:2899 | 线带；`GL_TRIANGLES` | |
| TMultiLineStrokeStyle2 (138) | SS:3213 | 每条子线一条四边形带，开 `GL_POLYGON_SMOOTH` | 最小粗细随像素大小 |
| TZigzagStrokeStyle (129) | SS:3453 | 顶点数组：四边形带、两条线带、`GL_LINES` | **不应用颜色函数**（用的是 `m_color`） |
| TSinStrokeStyle (130) | SS:3594 | 四边形带加两条线带 | |
| TFriezeStrokeStyle2 (133) | SS:3756 | 四边形带加两条线带 | |
| TDualColorStrokeStyle2 (132) | SS:3808 | 两条线带；四边形带，换色处顶点重复（硬边） | |
| TLongBlendStrokeStyle2 (134) | SS:3944 | 两条线带；四边形带，沿长度逐顶点渐变 | 渐变沿曲线走，不是直线轴 |
| TMatrioskaStrokeStyle (141) | SS:4444 | 自己不发图元，多次调纯色笔触 | |
| FlowLineStrokeStyle (201) | `flowlinestrokestyle.cpp:215` | 每条平行线一条线带（浮点顶点数组） | |
| OutlineViewerStyle (99) | SS:4355 | 线带、线段、4 像素的点 | 仅 `_DEBUG` 注册 |

`colorfx/zigzagstyles.cpp` 整个文件包在一个没有任何地方定义的宏里，是死代码。

### A.2 colorfx 区域样式（16 个）

| 类（tag） | 绘制入口 | GL 图元与状态 | 备注 |
|---|---|---|---|
| MovingSolidColor (1125) | RS:155 | 无，平移轮廓后调纯色填充 | |
| ShadowStyle (1127) | RS:370 | 标准掩码；每条排线一条 3 顶点线带，alpha 为 0、满、0；洞用区域外的反向掩码 | |
| ShadowStyle2 (1135) | RS:689 | 标准掩码；每条边一条 11 截面的四边形带，三次方渐变 | |
| TRubberFillStyle (1128) | RS:851 | 无，变形轮廓后调纯色填充 | |
| TPointShadowFillStyle (1129) | RS:1103 | 标准掩码；平滑 `GL_POINTS`，逐点 alpha | 点大小取自驱动的范围；**阴影色不应用颜色函数** |
| TDottedFillStyle (1130) | RS:1291 | 标准掩码；每个格点一个 `gluDisk` | |
| TCheckedFillStyle (1131) | RS:1576 | 标准掩码；一批 `GL_QUADS` | |
| ArtisticSolidColor (1132) | RS:1876 | 无，扰动轮廓后调纯色填充 | |
| TChalkFillStyle (1143) | RS:2025 | 标准掩码；显示列表里的方块，逐块平移、随机 alpha | |
| TChessFillStyle (1136) | RS:2266 | 标准掩码；显示列表里的格子 | |
| TStripeFillStyle (1137) | RS:2529 | 标准掩码；`GL_QUADS` 条纹；嵌套掩码后在条纹外画平滑线 | 第二层掩码只为防止镶边重复混合 |
| TLinGradFillStyle (1138) | RS:2904 | 仅掩码；3 个四边形，中间那个两端颜色不同 | 两色线性渐变 |
| TRadGradFillStyle (1139) | RS:3127 | 仅掩码；三角扇圆盘、逐顶点颜色的环、纯色外环 | 径向渐变 |
| TCircleStripeFillStyle (1140) | RS:3419 | 标准掩码；每环一条四边形带加嵌套掩码外的两条线圈 | |
| TMosaicFillStyle (1141) | RS:3701 | 标准掩码；一批 `GL_QUADS`，每块随机取 4 色之一 | |
| TPatchFillStyle (1142) | RS:3981 | 标准掩码；每个六边形 4 个三角形；每条缝一个四边形加两个圆盘 | |

### A.3 colorfx 栅格样式（3 个）

`TAirbrushRasterStyle` (1150)、`TBlendRasterStyle` (1160)、`TNoColorRasterStyle` (1151)。不用 GL，不画矢量，在 `TRop::convert` 里对色指数栅格的单色图层做模糊。

### A.4 tnzcore 与 toonzlib 的样式

| 类（tag） | 绘制入口 | GL 图元与状态 | 备注 |
|---|---|---|---|
| TSolidColorStyle (3) | TS:645、TS:655 | 见 2.2 的第 7、8 步 | 镶边颜色通过回读 GL 当前颜色传递 |
| TCenterLineStrokeStyle (2) | TS:799 | 细：线带，线宽以像素计，可带 stipple。宽：四边形带加两条线带 | 线宽夹到驱动的范围 |
| TRasterImagePatternStrokeStyle (2000) | TS:1444 | 每个印章一个贴图四边形；自己设混合因子 | 每次绘制重新上传贴图；**不应用颜色函数** |
| TVectorImagePatternStrokeStyle (2800) | TS:1991 | 每个印章递归 `tglDraw`；太小时画带 stipple 的线带 | |
| TVectorBrushStyle (3000) | `tvectorbrushstyle.cpp:71` | 自己不发图元，调纯色填充和纯色笔触 | **不传颜色函数** |
| TTextureStyle (4) | `toonzlib/imagestyles.cpp:215`、`:351` | 笔触：带贴图坐标的四边形带加两条线带。区域：贴图的 GLU 细分 | 只粗看过 |
| MyPaint 笔刷样式 | `toonzlib/mypaintbrushstyle.cpp:61` | 自己不发图元，调纯色笔触 | |

---

## 12. 附录 B：离屏 GL 使用方清单

tcomposer 能走到的七处见 2.1。其余：

| 位置 | 用途 | 画什么 | 从哪里走到 |
|---|---|---|---|
| `toonzlib/toonzscene.cpp:726`，`renderFrame` | 摄影表画法渲一帧 | `Stage::RasterPainter`：栅格层在 CPU 上合成后 `glDrawPixels`，矢量走 `tglDraw`，遮罩用 stencil | 场景图标、xsheet 缩略图、打印 |
| `toonzlib/toonzscene.cpp:781-852`，另一个 `renderFrame` | 子场景贴图（plastic 用） | 同上，Windows 走 `TOfflineGL`，其余平台走 FBO | 界面 |
| `toonzqt/icongenerator.cpp:272` | 矢量关卡图标 | 一张矢量图，带各种检查模式 | 图标 |
| `toonzqt/icongenerator.cpp:435`、`:665` | 网格图标、运动路径图标 | 直接的 GL 线 | 图标 |
| `common/tvrender/tcolorstyles.cpp:212`，`TColorStyle::makeIcon` | 样式色块 | 一张合成的矢量图 | 界面 |
| `common/tvrender/tsimplecolorstyles.cpp:1752` | 矢量图案样式的色块 | 一张矢量图 | 界面 |
| `toonzlib/stylemanager.cpp:387` | 自定义样式浏览器的色块 | 一张矢量图（Linux 走 `TOfflineGL`，其余走 FBO） | 界面 |
| `tconverter/tconverter.cpp:237` | pli 转栅格 | 一张矢量图 | tconverter |
| `toonzqt/imageutils.cpp:452` | 界面里的格式转换 | 一张矢量图 | 界面 |
| `toonzlib/scriptbinding_rasterizer.cpp:126` | 脚本的栅格化 | 一张矢量图 | 脚本 |
| `common/tvectorimage/tvectorimage.cpp:691`，`TVectorImage::render` | 剪贴板、直方图 | 一张矢量图 | 界面 |
| `toonzlib/trasterimageutils.cpp:50`、`:130` | 把笔触或区域画进全彩栅格 | 一条笔触或一个区域 | 工具 |
| `toonz/exportlevelcommand.cpp:307` | 导出关卡 | 一张矢量图 | 界面 |
| `toonz/trackerpopup.cpp:124`、`:459` | 追踪 | 一张矢量图 | 界面 |
| `toonz/meshifypopup.cpp:123` | 生成网格前的栅格化 | 一张矢量图 | 界面 |
| `toonz/moviegenerator.cpp:210` | 导出场景影片 | `Stage::OpenGlPainter` | 界面 |
| `toonzlib/imagebuilders.cpp:333` | "把矢量显示成栅格" | 一张矢量图，FBO | 界面 |

除了两个 `renderFrame`、`moviegenerator` 和两处直接画线的图标，其余全部是"把一张矢量图（或一条笔触、一个区域）画进一张栅格"，换成"在栅格上建画布、调核心遍历"即可。

死代码或未编译：`stdfx/pins.cpp:136` 的 `subCompute`、`stdfx/offscreengl.h`、`toonzlib/txshsimplelevel.cpp:721` 的 `buildIcon`、`toonzfarm/tfarmclient/thumbnail.cpp`、`common/tvectorrenderer.cpp`、`tofflinegl.cpp` 里的 `XImplementation` 与 `QtOfflineGLPBuffer`、`macofflinegl.cpp`。迁移时直接删。
