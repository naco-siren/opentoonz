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
| 5 | `tnzext/ttexturemesh` 与 `plasticdeformer.cpp` | `TTextureMesh::faceContains` 用严格符号判断，正好落在网格边上的骨骼句柄不属于任何面，`PlasticDeformer::compile()` 会静默丢掉它，网格只做刚体旋转。 | `tests/fixtures/gen_reference_project.cpp`（生成器把关节放在 y=3.3 避开边） |

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
- **`.tnz` 序列化不是字节确定的**：fx 集合、终端集合和 cast 文件夹按指针顺序写出，同一场景重新生成可能行序不同。比较渲染结果而不是字节。
- **ParticlesFx 默认精灵忽略绝对 `scale`**，只有相对范围起作用。**GlowFx** 的 `fade` 必须大于 0 辉光颜色才生效。**非动画参数**（`TIntParam`）渲染和保存的是 `getValue()` 而非默认值。
