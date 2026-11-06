# NextToonz：测试固化下来的已知库问题

Phase 0 的测试在编写过程中发现了下面这些库行为。它们目前都被测试**按现状固化**（characterisation），
也就是说测试断言的是"现在就是这样"，而不是"应该这样"。修复任何一条时，先改对应的测试断言，
再改库代码。每条标注了发现它的测试文件，便于定位。

修复时机：除非阻塞当前阶段，否则放到 Phase 2（核心去 Qt 与抽出）里一并处理，
因为那时这些模块本来就要被触碰。

## 真实 bug

| # | 位置 | 现象 | 发现于 |
|---|---|---|---|
| 1 | `common/expressions/tparser.cpp`，`Parser::Imp::parseExpression` | 三元运算符的比较作用在错误的操作数上：`2 > 1 ? 10 : 20` 得 0，`frame > 5 ? 10 : 20` 恒为 0。条件加括号可绕过。 | `tests/core/test_texpression.cpp` |
| 2 | `common/tstream/tstream.cpp` 与 `TFilePath` 的流读写 | 写入时把 `'` 转义为 `\'`，读回 `TFilePath` 时保留了反斜杠并被当成路径分隔符：`it's/a.png` 读回成 `it/'s/a.png`。纯字符串读回正常。Linux 与 FreeBSD 上可见。 | `tests/core/test_tstream.cpp` |
| 3 | `include/trangeparam.h` | `TRangeParam::getKeyframeCount()` 只有声明没有定义，调用即链接失败。 | `tests/core/test_tparam.cpp` |
| 4 | `include/tintparam.h`（或其实现） | `TIntParam` 的拷贝构造函数不初始化 min/max 范围和 wheel 标志，属未定义行为。 | `tests/core/test_tparam.cpp`（仅注释，未断言） |
| 6 | `toonzlib/fxdag.cpp`，`FxDag::getFxById` | id 表按小写 id 建键，查找时却不把参数转小写：`getFxById(L"Blur01")` 返回空，只有 `"blur01"` 能找到。 | `tests/toonzlib/test_scene_io.cpp` |
| 7 | `toonzlib/txsheet.cpp`，`insertColumn`/`removeColumn` | 插入已填充的列或删除列后不更新帧数，直到调用 `updateFrameCount()`。生成器已在保存前显式调用，否则由零元列构成的场景头部会写成 `framecount="0"`。 | `tests/toonzlib/test_xsheet_ops.cpp` |
| 8 | `include/toonz/txsheet.h` 文档注释 | `clearCells()` 原地清空、`removeCells()` 上移下方单元格，头文件注释写反了。 | `tests/toonzlib/test_xsheet_ops.cpp` |
| 9 | `include/tvectorimage.h` | `TVectorImage::areValidRegions()` 只有声明没有定义。 | `tests/toonzlib/test_level_formats.cpp` |
| 10 | `common/tvectorimage/tvectorimage.cpp`，`Imp::removeStroke` | 从 `m_strokes` 移除 `VIStroke*` 包装对象并返回内部笔触，但从不删除包装对象，每次 removeStroke 泄漏一个。LeakSanitizer 可见。 | `tests/core/test_tvectorimage.cpp` |
| 11 | `common/tvectorimage/tl2lautocloser.cpp`，`Imp::getIntersection` | 对同一笔触求自交（`tcomputeregions.cpp` 的循环从 `j = i` 开始）时同一 map 键写两次，第一个 `StrokesIntersection` 泄漏。 | `tests/toonzlib/test_level_formats.cpp` |
| 12 | `toonzlib/outputproperties.cpp` | `~TOutputProperties()` 不删除构造函数和 `operator=` 里 new 出来的 `m_boardSettings`，每次加载场景泄漏一到两个。 | `tests/toonzlib/test_scene_io.cpp` |
| 13 | `stdfx/changecolorfx.cpp` | `ChangeColorFx` 的 `FX_PLUGIN_IDENTIFIER` 被注释掉，`getDeclaration()` 只声明未定义，vtable 和 typeinfo 从不生成；`stdfx/changecolorfx.h` 是过期副本。开启 UBSan 的 vptr 检查时 `libtnzstdfx` 无法链接，asan preset 因此暂时带 `-fno-sanitize=vptr`。应删除这段死代码。 | 清洁剂 CI |
| 14 | `include/tutil.h` 的 `tfloor`/`tceil`，`include/tcommon.h` 的 `tround`/`troundp`，`include/tgeometry.h` 的 `convert(const TRectD&)` | 这些辅助函数用 `(int)x` 转换，`x` 超出 int 范围或为 NaN 时是未定义行为，而结果在 x86-64（`cvttsd2si` 给 INT_MIN）和 arm64（标量 `fcvtzs` 饱和；向量化后先转 64 位再截断低 32 位）上不同。`tnzbase/trasterfx.cpp` 与 `common/tfx/tfxcachemanager.cpp` 的 `enlargeToI()` 曾把 `TConsts::infiniteRectD`（零元 fx 与 over 链的 bbox）交给它们：x86-64 上恰好得到空矩形于是原样保留，arm64 上变成 (-1,-1) 处的 1×1 矩形，tcomposer 的每一帧只剩一个像素。Phase 0 已把两处 `enlargeToI()` 改为 `std::floor`/`std::ceil` 的双精度运算，并让 `common/tvrender/tellipticbrush.cpp` 的 `buildAngularSubdivision()` 对细于半个像素的笔触（`acos` 得 NaN）明确返回 0 个细分而不是把 NaN 交给 `tceil`（UBSan 在 12 个 golden 场景上再无 float-cast-overflow 报告），辅助函数本身仍是 UB，Phase 2 触碰 `tgeometry` 时应改成定义明确的饱和转换，并逐个确认调用方不依赖越界输入。 | macOS arm64 CI 的 golden；清洁剂构建下的 tcomposer |
| 15 | `stdfx/shaderinterface.cpp`，`ShaderInterface::makeProgram` | 带输入端口的 shader 特效（HSLBlendGPU、glitter、radialblurGPU、spinblurGPU）在 macOS 上让 tcomposer 段错误：计算输入区域的程序要用 transform feedback，而 `glTransformFeedbackVaryings` 在 macOS 的 GL 上下文里是空指针，代码直接调用。没有输入端口的 shader 特效不受影响。Linux（Mesa llvmpipe）上正常；GPU shader 的扫场场景只在 Linux 上比较。 | `tests/fixtures/gen_reference_project.cpp`（特效扫场） |
| 16 | `common/tvrender/tvectorbrushstyle.cpp`，`TVectorBrushStyle::saveData` | 没有笔刷的矢量笔刷样式（默认构造）一保存就解引用空指针崩溃。生成器给它配了 `stuff/library/vector brushes` 里的笔刷来绕开。 | `tests/fixtures/gen_reference_project.cpp`（矢量样式场景） |
| 17 | `common/tparam/ttonecurveparam.cpp`，`TToneCurveParam::setDefaultValue` | 传入的控制点个数与参数已有的不同就解引用空指针崩溃，没有任何检查。生成器只移动已有的控制点。 | `tests/fixtures/gen_reference_project.cpp`（特效扫场） |
| 18 | `stdfx/iwa_soapbubblefx.cpp`，`toonzlib/sandor_fxs/`（artContourFx 等），以及矢量样式的 OpenGL 绘制 | 渲染结果随平台变化的三个来源，都已实测：(a) `iwa_SoapBubbleFx` 和 `artContourFx` 用 C 库的 `rand()` 取随机数，序列因平台而异，同一场景在 Linux 和 macOS 上图案不同；(b) GLSL shader 特效的结果取决于驱动，连默认参数下的 sunflare 光芒条数都不同；(c) 特殊颜色样式用 OpenGL 的线、纹理和平滑绘制，Mesa llvmpipe 与 AMD 显卡之间每个样式格有中位数 3.4%、最多 28% 的像素不同。其余 154 个 CPU 特效在 Linux 与 macOS（arm64、x86_64）上的差异都在容差内。Phase 1 换成确定性的 CPU 光栅化并统一随机数来源后应当消失；在那之前这些场景只在 Linux 上比较。 | macOS 本机与 Linux CI 渲染结果的对比 |
| 19 | `stdfx/ino_line_blur.cpp` | 整个文件包在 `#ifdef NDEBUG` 里，Debug 构建里没有 `inoLineBlurFx` 这个特效：含它的场景在 Debug 构建下加载失败（"unable to create a persistent 'STD_inoLineBlurFx'"）。原因不明。特效扫场因此不包含它。 | 清洁剂 CI（Debug 构建）加载 `fx_sweep_ino_b` |
| 5 | `tnzext/ttexturemesh` 与 `plasticdeformer.cpp` | `TTextureMesh::faceContains` 用严格符号判断，正好落在网格边上的骨骼句柄不属于任何面，`PlasticDeformer::compile()` 会静默丢掉它，网格只做刚体旋转。 | `tests/fixtures/gen_reference_project.cpp`（生成器把关节放在 y=3.3 避开边） |

清洁剂 CI（`workflow_sanitizers.yml`）目前关闭了泄漏检测（`detect_leaks=0`），修掉第 10 到 12 条后可以打开。它除单测外也用清洁剂构建的 tcomposer 跑全部 golden 场景并以 `halt_on_error=1` 阻塞，第 14 条这类“只在另一种架构上出错”的未定义行为因此会先在 Linux x86-64 上被 UBSan 抓住。

## 值得知道但不算 bug 的行为

- **插值**：`TDoubleParam::setValue` 创建的关键帧段是线性插值；单个关键帧处处取该值；范围外取端点值。Exponential 在任一端值不大于 0 时退化为线性。`cycle` 每轮累加（末值减首值），所以斜坡会持续上升而不是循环。
- **`TDoubleParam::isDefault()`** 只在"无关键帧且默认值为 0"时为真。
- **参数里的表达式**：`frame` 以 1 为基传入；新建的 `TDoubleParam` 没有语法对象，所有表达式求值为 0，直到调用 `setGrammar`。
- **表达式语言**：三角函数用角度；`%` 是向下取整的模，`x % 0` 为 0，且优先级低于加减；`^` 左结合，一元负号优先级更高（`-2^2` 为 4）；负数 `sqrt` 得 0；`sqr` 算的是平方，帮助文本却写着 square root。完整表达式之后的多余文本被静默忽略。
- **流里的 double** 只写 6 位有效数字，不能精确往返；绝对值小于 1e-8 的写成 0。属性按字母序输出；只含 `[A-Za-z0-9_%]` 的字符串不加引号；压缩文件在流对象析构时才真正写出。
- **笔触几何**：`thick` 是半宽；新笔触默认样式 id 为 1；`getAverageThickness()` 只是存储值，工具不设置时为 0。
- **矢量图像**：没有调色板时 `getBBox()` 为空；`addStroke` 对单点笔触返回 -1 且不接管所有权；`removeStroke` 把笔触交还调用方但泄漏其内部包装对象。
- **区域**：一个闭合笔触一个区域，开放笔触没有区域；区域包围盒包含笔触厚度；两个相交圆产生 3 个区域；内含圆成为外圆的子区域，`getRegion(point)` 返回最内层，外层 `TRegion::contains` 在洞内也为真。
- **TFilePath 帧号解析**依赖 `TFileType::declare` 注册表，未注册的扩展名不被视为序列帧；单文件关卡格式（pli、tlv）的文件名永远不带帧号。
- **重新保存已加载的场景不是字节恒等的**：加载时会给输出设置补上 png 格式属性；连接到 xsheet 节点的 fx 列表是 `std::set<TFx*>`，按指针顺序写出，两次连续保存之间都可能变化。
- **`.tnz` 序列化不是字节确定的**：fx 集合、终端集合和 cast 文件夹按指针顺序写出，同一场景重新生成可能行序不同。比较渲染结果而不是字节。
- **场景名以 `_数字` 结尾会被当成帧号**：tcomposer 生成输出文件名时把 `fx_sweep_01` 解析成关卡 `fx_sweep` 的第 1 帧，输出变成 `fx_sweep_0001.png`。扫场场景因此用字母后缀。
- **`ToonzScene::createNewLevel()` 在同名文件已存在时会另找名字**：同一个项目里第二个场景再创建同名关卡，得到的是改名后的副本，放在别的文件夹里；第三次调用在生成器里观察到长时间空转（十分钟未返回，原因未查）。共享关卡要用 `loadLevel()`。
- **ino_ 系列特效的长度参数**以毫米计，按 640 像素对应 12 英寸换算，再乘以摄像机 dpi；参考场景是 20 dpi，所以数值要比直觉大六倍左右才看得出效果。
- **错切（shear）是系数不是角度**：1 相当于倾斜 45 度。**叠放顺序（SO）是全局的**：小于背景列的 SO 就会被背景盖住。
- **ParticlesFx 默认精灵忽略绝对 `scale`**，只有相对范围起作用。**GlowFx** 的 `fade` 必须大于 0 辉光颜色才生效。**非动画参数**（`TIntParam`）渲染和保存的是 `getValue()` 而非默认值。
