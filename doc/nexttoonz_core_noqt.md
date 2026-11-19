# NextToonz 核心去 Qt 设计草案

版本 0.3，2026-11-18。本文是 `doc/nexttoonz_plan.md` Phase 2（核心去 Qt 与抽出）的设计草案：先实测核心库现在怎样依赖 Qt，再定目标状态、每项 Qt 设施的替代、一张每步都能单独合并的迁移表、每步怎样验证、引擎 API（D3）的第一版和出现时机，最后是需要所有者决定的问题。做法沿用 Phase 1（`doc/nexttoonz_canvas_ir.md`）：先有设计，再一步一步做，每一步都证明默认路径没有变，做完一步就把"实际做成什么样"补回本文。

迁移第 1 步（递归互斥锁与无 Qt 构建的骨架）、第 2 步（无 Qt 构建的 CI job）和第 3 步（字符串转换）已经实现（第 7 节，做成的样子在 8.1 到 8.3 节），在本地分支 `wip/phase2-noqt` 上，没有合并、没有推送；第 2 步的 CI job 因此还没有在 GitHub 上跑过。其余都是设计。所有者 2026-11-18 审过第 10 节的 Q1 到 Q8，都按本稿的倾向定了，其中 Q4、Q5 标为要跟进（第 10 节开头）。

调查基线：分支 `nexttoonz/phase0`，提交 `84f17b336`（Phase 1 合并之后）。所有路径相对于 `toonz/sources/`。标着"实测"的数字来自编译器和链接器，不是读代码：预处理器的包含树（`-E -H`，用构建目录里 `compile_commands.json` 的原样参数）、已构建的 dylib 的未定义符号（`nm -m`）和链接的框架（`otool -L`）。按类分的行数来自正则统计，有噪声，只当量级看。测量脚本不在版本库里，做法记在 2.1 节。

---

## 目录

1. 结论摘要
2. 实测：核心库怎样依赖 Qt
3. iPadOS 需要哪些库不带 Qt
4. 目标状态
5. 每项 Qt 设施的替代
6. 给界面留的 Qt 便利接口
7. 迁移顺序
8. 每一步怎样验证"默认路径没有变"
9. 引擎 API 的第一版
10. 需要所有者决定的问题
11. 风险与未验证事项
12. 附录：泄漏 Qt 的公共头

---

## 1. 结论摘要

- **Qt 进核心库主要是从头文件进来的，不是从各库自己的代码。** 八个核心库一共 662 个编译单元（不含 tnzcanvas、tnzskia），改动前 555 个经包含链碰到 Qt 的头，其中只有 131 个自己写了 `#include <Q...>`。把 Qt 带进来的公共头一共 122 个，排第一的是 `tthreadmessage.h`（为了 `TThread::Mutex` 包含 `<QMutex>`，470 个编译单元经它碰到 Qt），其后是 `tfilepath.h`（419）、`tconvert.h`（301）、`timagecache.h`（225）、`tundo.h`（208）。所以迁移顺序由头文件决定：先清 tnzcore 最底层的几个头，上面的库才有可能不带 Qt 编译。
- **计划附录 8.3 的难度分级要改。** 它把 tnzext（"仅互斥锁与 QString"）和 colorfx（"仅 translate"）列为"小"。按各库自己的代码看确实小（tnzext 链接时只用到 28 个 Qt 符号，colorfx 只有 6 个），但它们的每个编译单元都经 `tstroke.h`、`tfilepath.h`、`tstream.h`、`tcolorstyles.h` 碰到 Qt，在 tnzcore 的这些头清干净之前，哪一个都不可能在不带 Qt 的构建里编译。真正的叶子只在 tnzcore 内部：几何、颜色、随机数、智能指针这些目录。
- **第 1 步做的是排第一的那个头。** `TThread::Mutex` 由 `QMutex(QMutex::Recursive)` 换成 `std::recursive_mutex`，`TThread::MutexLocker` 保留 `QMutexLocker` 的行为（接受空指针、作用域内 `unlock()`/`relock()`），全树另外六处 `QMutex::Recursive` 一并换掉（D8 要求 Phase 2 消掉的 Qt6 阻塞项）。改了 68 个源文件（其中界面 27 个），绝大多数只是把 `QMutexLocker` 换成 `TThread::MutexLocker`，或者补上原来经 `tthreadmessage.h` 间接得到的 `#include <QMutex>`。碰到 Qt 的编译单元从 555 降到 510。新的 CMake 选项 `WITH_QT=OFF` 配出一个完全不找 Qt 的构建：画布库加 tnzcore 里已经不用 Qt 的 10 个源文件和它们的单测，`libtnzcore.dylib` 只链接 libc++ 和 libSystem；同样这些文件和单测在 iPad 模拟器里也跑通了。默认构建的编译命令除新增的两个测试文件外逐字相同，golden 帧和多线程渲染逐字节相同，Linux 虚拟机里清洁剂 CI 的全部测试照常通过（第 8 节）。
- **iPadOS 要的是全部八个库，但不是每个库的全部文件**（第 3 节）。扫描仪、`tipc`/`t32bitsrv`/QuickTime、外部 ffmpeg 进程、GLSL 特效、QtScript 绑定不进 iPad 构建，要删或移出核心；字体、声音输出、视频编解码改成注入的平台服务。
- **还有一个 Phase 1 留下的前提**：tnzcore、toonzlib、colorfx、tnzext 仍然链接桌面 OpenGL（交互 viewer 一侧的代码，按画布文档 Q3、Q4 留到 Phase 3）。不带 Qt 的核心在 macOS 上可以继续链 GL，但在 iPad 上不行。所有者定了在 Phase 2 里一并把这部分移出核心库（第 10 节 Q1），所以 Phase 2 结束时核心的单测要在 iPad 模拟器里跑；迁移表为此多了第 8 步。
- 引擎 API 的第一版建议在线程那一步之后出现，先只覆盖 tcomposer 的流程（开场景、渲帧、写文件），由 tcomposer 改用它，用 golden 逐位证明（第 9 节）。

---

## 2. 实测：核心库怎样依赖 Qt

### 2.1 测量方法

- **链接**：CMake 里每个目标 `target_link_libraries` 写的 Qt 模块；已构建的 dylib 实际链接的框架（`otool -L`）；dylib 里从 Qt 框架解析的未定义符号（`nm -m`，按 `(from QtXxx)` 分模块，`c++filt` 后按类归并）。符号数量是"这个库的二进制真正需要 Qt 的哪些东西"，包括头文件里内联进来的部分。
- **包含**：对每个核心库的每个编译单元，用它在 `compile_commands.json` 里的原样参数跑一遍 `-E -H`，从包含树里找出所有 Qt 头，以及每个 Qt 头的直接上级。上级是项目里的头，就记为"泄漏者"；上级是编译单元本身，就记为"直接包含"。一个编译单元的所有泄漏者都清干净、自己也不直接包含，它就能不带 Qt 编译。第 7 节的"这一步让哪些源文件进入无 Qt 构建"就是这样模拟出来的。
- **按类统计**：各库源码目录（不含 `include/`，不含 moc 生成的文件）里匹配各类 Qt 标识符的行数和文件数。和计划附录 8.3 的口径相同，有噪声（比如注释、同名的非 Qt 标识符）。

脚本放在 `toonz/build-phase1/ab/phase2/`（不进版本库），改动前后的渲染对照也在那里（第 8 节）。

### 2.2 链接的 Qt 模块与真正用到的符号（改动前，`84f17b336`）

| 库 | CMake 链接 | dylib 实际链接的 Qt 框架 | 从 Qt 解析的符号 | 用得最多的类 |
|---|---|---|---|---|
| tnzcore | Core、OpenGL、Network、Multimedia | Core、Gui、Multimedia、Network、OpenGL、Widgets | 415（Core 270，Gui 65，Network 34，Multimedia 27，OpenGL 19） | QString、QObject、QFileInfo、QDir、QGLFormat、QThread、QSharedMemory、QDateTime、QAudioOutput、QLocalServer |
| tnzbase | Core、Gui | Core、Gui | 91（Core 72，Gui 19） | QObject、QString、QRegion、QMutex、QReadWriteLock、QOffscreenSurface、QFont、`qCompress` |
| tnzext | Core、Gui、Network | Core、Gui、Network | 28（全是 Core） | QString（作 QHash 的键）、QMutex、QReadWriteLock |
| toonzlib | Core、Gui、Widgets、Script、Multimedia | Core、Gui、Multimedia、Network、Script、Widgets | 523（Core 307，Gui 104，Script 65，Multimedia 35，Widgets 12） | QString、QScriptValue、QObject、QVariant、QScriptEngine、QPainter、QAudioFormat、QImage、QColor、QSettings、QProcess |
| stdfx | Core、Gui、OpenGL | Core、Gui、OpenGL、Widgets | 286（Gui 131，Core 100，Widgets 55） | QWidget、QPainter、QOpenGLWidget、QObject、QOpenGLShaderProgram、QImage、QThread、QFont |
| image | Core、Gui、Network（另在 Windows 加 Widgets） | Core、Gui、Network、Widgets | 160（Core 132，Gui 26，Network 2） | QString、QImage、QTextStream、QDataStream、QSharedMemory、QProcess |
| sound | Core | Core | 6 | QProcess、QFileInfo、QString |
| colorfx | Core、Gui | Core、Gui | 6 | `QCoreApplication::translate`、QMutex |
| tnzcanvas、tnzskia | 无 | 无 | 0 | |

几点：

- **CMake 写的和实际用的不一致。** tnzext 声明了 Gui 和 Network，二进制只用 Core；colorfx 声明了 Gui，只用 Core 的 6 个符号。dylib 链接的框架比 CMake 写的多（Widgets 出现在 tnzcore 和 image 里），是 Qt 的 CMake 包把依赖传递下来的。
- **stdfx 的 55 个 Widgets 符号和 17 个 `QOpenGLWidget` 符号**都来自 ShaderFx 的 `ShadingContext`（一个隐藏的 GL 控件）。
- **sound 和 colorfx 自己几乎不用 Qt**，但二进制仍然链接 Qt，因为它们的编译单元经头文件内联了 QString 的析构等函数。
- OpenGL：tnzcore 56 个、tnzext 33 个、toonzlib 41 个、colorfx 16 个、stdfx 37 个未定义符号来自 OpenGL 框架（Phase 1 之后剩下的都是 viewer 一侧和 ShaderFx）；tnzbase、image、sound 不用 GL。

### 2.3 按用途分类（行数/文件数，第 1 步之后）

| 用途 | tnzcore | tnzbase | tnzext | toonzlib | stdfx | image | sound | colorfx |
|---|---|---|---|---|---|---|---|---|
| 字符串（QString、QChar、QByteArray、QRegularExpression、QTextCodec、QTextStream…） | 414/30 | 96/11 | 35/3 | 674/62 | 94/11 | 281/26 | 1/1 | 148/7 |
| 容器（QList、QMap、QHash、QVector、QStack、QVariant…） | 11/5 | 20/5 | 0 | 171/24 | 159/25 | 7/3 | 0 | 0 |
| 文件（QDir、QFile、QFileInfo、QTemporaryFile、QStandardPaths、QDataStream…） | 125/10 | 2/1 | 0 | 78/14 | 11/2 | 33/6 | 1/1 | 0 |
| 设置（QSettings） | 5/2 | 6/2 | 0 | 16/2 | 0 | 0 | 0 | 0 |
| 时间（QDateTime、QTime、QElapsedTimer、QTimer） | 35/7 | 6/2 | 0 | 19/7 | 11/3 | 3/1 | 0 | 0 |
| 线程与锁（第 1 步之前） | 115/20 | 63/6 | 45/4 | 69/15 | 66/21 | 7/5 | 0 | 0 |
| 线程与锁（第 1 步之后） | 82/15 | 40/4 | 9/1 | 57/12 | 66/21 | 6/4 | 0 | 0 |
| QObject 与信号 | 29/9 | 7/1 | 0 | 365/26 | 3/1 | 22/1 | 0 | 0 |
| 应用对象（QCoreApplication、qApp、qGuiApp） | 48/8 | 7/1 | 0 | 20/8 | 6/3 | 13/13 | 0 | 155/7 |
| QImage 与绘制（QImage、QPainter、QFont、QRect、QPoint、QRegion…） | 41/2 | 37/4 | 0 | 562/18 | 362/17 | 23/3 | 0 | 1/1 |
| 进程与 IPC（QProcess、QLocalSocket、QSharedMemory、QTcpServer…） | 68/8 | 0 | 0 | 11/1 | 0 | 38/7 | 2/1 | 0 |
| QtScript | 0 | 0 | 0 | 215/12 | 0 | 0 | 0 | 0 |
| OpenGL 的 Qt 类（QOpenGL*、QOffscreenSurface、QGL*） | 16/2 | 5/1 | 0 | 41/4 | 85/4 | 0 | 0 | 0 |
| 多媒体（QAudio*） | 14/1 | 0 | 0 | 24/2 | 0 | 0 | 0 | 0 |
| 控件（QWidget、QMessageBox、QProgressDialog、QAction、QCursor…） | 2/1 | 0 | 0 | 16/6 | 0 | 1/1 | 0 | 0 |
| 翻译（`QCoreApplication::translate`、`tr(`、`QT_TR_NOOP`…） | 26/4 | 1/1 | 0 | 223/27 | 2/1 | 132/13 | 0 | 153/7 |

和附录 8.3 的数字大体一致（例如 QtScript 214/12 对 215/12）。新增的两行说明了两件事：colorfx 的 Qt 几乎全是翻译（每个样式的 `getDescription()` 用 `QCoreApplication::translate`），image 的 Qt 有一半是翻译和字符串。

### 2.4 编译单元：碰到 Qt 的、直接包含 Qt 的、天然不带 Qt 的

| 库 | 编译单元 | 经包含链碰到 Qt（第 1 步前 → 后） | 自己直接包含 Qt（第 1 步前 → 后） | 第 1 步之后不碰 Qt 的 |
|---|---|---|---|---|
| tnzcore | 131 | 112 → 87 | 29 → 30 | 44：tcolor、tgeometry、trop 的大部分、tcore 的随机数与计时、tstroke 及笔触几何、tsmartpointer… |
| tnzbase | 59 | 44 → 43 | 7 → 8 | 16：tparam 的基础、表达式解析、twain 的 C 文件 |
| tnzext | 29 | 25 → 13 | 3 → 2 | 16：变形器、势函数、meshbuilder、plasticdeformer |
| toonzlib | 200 | 183 → 178 | 50 | 22：IK、sandor_fxs 的 C++ 部分 |
| stdfx | 196 | 154 → 153 | 27 | 43：`igs_*` 算法全部、两个噪声 |
| image | 35 | 26 → 26 | 13 | 9：tzp、quantel、兼容层的 C 文件 |
| sound | 6 | 5 → 5 | 1 | 1 |
| colorfx | 6 | 6 → 5 | 1 | 1 |

直接包含的数目在 tnzcore 和 tnzbase 各多了一个，是第 1 步给 `tofflinegl.cpp` 和 `tpredictivecachemanager.cpp` 补的 `#include <QMutex>`（它们本来就经头文件碰到 Qt，8.1 节）。"不碰 Qt"只说明能编译，不说明能链接：比如 trop 的文件不碰 Qt 了，但它们要 `traster.cpp`，而 `traster.cpp` 还经 `timagecache.h` 碰到 QString。第 1 步进入无 Qt 构建的是其中链接闭合的 10 个文件（8.1 节）。

### 2.5 泄漏 Qt 的公共头

完整的前 40 个见附录。排前面的，以及它们为什么包含 Qt：

| 头 | 用到的 Qt 类型 | 经它碰到 Qt 的编译单元（第 1 步前 → 后） |
|---|---|---|
| `tthreadmessage.h` | QMutex（`TThread::Mutex` 继承它，`MutexLocker` 是 `QMutexLocker`） | 470 → 0（第 1 步） |
| `tfilepath.h` | QString（构造、`withName`、`withType` 等的重载，`TFrameId::m_letter`），QChar | 419 → 419 |
| `tconvert.h` | QString（`to_wstring(QString)` 之类的重载） | 301 → 301 |
| `timagecache.h` | QString（`add`/`get`/`remove` 的三个重载） | 225 → 225 |
| `tundo.h` | QObject（`TUndoManager` 的信号）、QString（`getHistoryString`） | 208 → 208 |
| `tnotanimatableparam.h` | QFont（`TFontParam`） | 195 → 195 |
| `trasterfx.h` | QOffscreenSurface（`TRenderSettings::m_offScreenSurface`） | 193 → 193 |
| `tstream.h` | QString（`operator<<`/`>>` 的重载） | 181 → 181 |
| `tcolorstyles.h` | QString、QStringList（`getDescription`、参数名） | 154 → 154 |
| `tpalette.h`、`tstroke.h`、`tgldisplaylistsmanager.h` | QMutex | 140、76、13 → 0（第 1 步；仍经别的头碰到） |
| `toonz/txshlevel.h` | QObject（`TXshLevel` 是 QObject） | 85 → 85 |
| `tsystem.h` | QString、QDateTime、QFileInfo、QStringList | 84 → 84 |
| `timage_io.h`、`tiio.h`、`tsound_io.h` | QStringList（格式列表） | 67、26、7 |
| `toonz/preferences.h` | QObject、QSettings、QString、QColor… | 40 |
| `tfxattributes.h` | QStack、QList | 39 |
| `ext/plasticskeleton.h`、`ext/plasticskeletondeformation.h` | QString（骨骼名） | 28、28 |
| `toonz/scriptbinding.h` | QtScript | 23 |
| `tmsgcore.h`、`tipc.h`、`orientation.h`、`tcacheresource.h` | QTcpServer、QLocalSocket、QPainterPath、QRegion… | 13、7、13、11 |

这些头还被界面一侧（toonz、toonzqt、tnztools，约 650 个编译单元）包含；计划里"约 730 个包含者"指的是全树。头文件里每去掉一个 Qt 类型，界面那边用到这个重载的地方都要跟着改，第 1 步因此改到了 tnztools（19 个文件）、toonzqt（5 个）和 toonz（3 个）。第 6 节是减少这种波及的办法。

### 2.6 各项设施的现状

- **线程**：`TThread::Runnable` 是 QObject，带 `started`/`finished`/`canceled`/`exception`/`progressChanged` 信号，`Executor` 的工作线程是 `QThread` 的子类，分配靠排队的信号槽（`tthread.cpp`）。`TThread::Message` 把任务送到主线程靠 QObject 的排队连接（`threadmessage.cpp`），主线程由 `QThread::currentThread()` 在静态初始化时记下。`TRenderer` 在等待时 `QCoreApplication::processEvents` 自旋（`trenderer.cpp` 1454、1491、1510 行），用 `QThreadStorage` 存线程局部数据，并在 GUI 线程上为 plastic 和流动笔刷特效建 `QOffscreenSurface`（1425 行）。Runnable 的信号在核心外被连接 7 处（toonz 3、toonzqt 4）。
- **Handle**：toonzlib 的 9 个 Handle 加 `PaletteController` 一共 56 个信号，toonzlib 内部 emit 44 行、connect 36 处；界面连接这些信号 337 处（toonz 209、toonzqt 86、tnztools 31、stopmotion 11，只按信号名粗数）。
- **QObject 模型类**：include 下 38 个带 `Q_OBJECT` 的核心头（不含 toonzqt、tools），除 9 个 Handle 外有 `TXshLevel`、`TXshSimpleLevel`、`TXshSoundColumn`、`Preferences`、`MovieRenderer`、`MultimediaRenderer`、`VectorizerCore`、`StyleManager`、`FullColorPalette`、`PaletteController`、`TUndoManager`、`TFunctorInvoker`、`TCacheResourcePool`、`TMsgCore`、`TNZCamera`、`Runnable`、tipc 的服务器，以及 12 个脚本绑定类。
- **Preferences**：QSettings 的 INI 文件。`Preferences::instance()` 在 toonzlib 里 95 处，image 3 处（QuickTime 后端开关、图层名编码、预计算），sound 只包含了头没有调用，tcomposer、tconverter、tcleanupper 各一两处；其余 600 多处在界面。
- **QImage 与文字**：`tfont_qt.cpp`（字体关卡的字形，QRawFont、QPainterPath）、toonzlib 的 `stage.cpp`/`stagevisitor.cpp`/`boardsettings.cpp`/`trasterimageutils.cpp`（用 QPainter 往栅格上写字：片头板、帧号、场景信息）、`stylemanager.cpp`/`imagebuilders.cpp`/`toonzscene.cpp`/`plasticdeformerfx.cpp`（图标与离屏的读回）、stdfx 的文字与时间码特效（QFont、QPainter），image 的 sprite 写出器和 ffmpeg 读入器。
- **外部进程**：LZO 压缩经 `QProcess` 调 `lzocompress`/`lzodecompress`（`common/trasterimage/tcodec.cpp`）；视频格式经 `QProcess` 调 ffmpeg（`image/ffmpeg/`、`sound/ffmpeg/`、`toonzlib/thirdparty.cpp`）；`tcontenthistory.cpp` 和 `tsystem.cpp` 各有一处 QProcess。
- **tipc、t32bitsrv、QuickTime**：`tipc` 只被 mov/3gp 的代理读写器（`image/mov/tiio_mov_proxy.cpp`、`image/3gp/tiio_3gp_proxy.cpp`）、`movsettings.cpp` 和 `toonz/main.cpp` 的两处使用。`t32bitsrv` 只在 32 位 Windows 或 32 位 Apple 上构建；64 位 macOS 编译的 `tiio_movM.cpp`、`tiio_3gpM.cpp` 整个包在 `#ifndef __LP64__` 里，是空文件；mov 实际走 ffmpeg。三者在当前所有目标平台上都是死代码。
- **tmsgcore**：`TMsgCore`（QTcpServer）是应用和 tcomposer、tcleanupper 之间的消息通道，核心里 14 处使用，属于应用一侧的设施。
- **翻译**：colorfx 153 行、toonzlib 223 行、image 132 行、tnzcore 26 行用 Qt 的翻译函数产生给用户看的文字（样式名、参数名、格式属性名、默认调色板名）。
- **QtScript**：toonzlib 的 12 个 `scriptbinding*.cpp` 和 `scriptengine.cpp`，215 行。

### 2.7 对附录 8.3 难度分级的修正

- tnzext、colorfx、sound 不是叶子。它们自己的 Qt 用量小，但要等 tnzcore 的 `tstroke.h`、`tfilepath.h`、`tstream.h`、`tcolorstyles.h`、`tsound_io.h` 等头清干净才能不带 Qt 编译；colorfx 还要等翻译的办法（第 5 节）。
- tnzcore 的算法目录确实是叶子：tgeometry、tcolor 在第 1 步之前就不碰 Qt；trop、tstroke 及笔触几何在第 1 步之后不碰 Qt，但链接要等 `traster.cpp`（`timagecache.h`）和 `tmathutil.cpp`（`tconvert.h`）。
- stdfx 不难：196 个编译单元里 153 个只是经 tnzbase 的头碰到 Qt（`trasterfx.h`、`tundo.h`、`tnotanimatableparam.h`、`tfxattributes.h`）。这几个头清掉以后，按模拟 162 个可以不带 Qt 编译，剩下的是 GL、文字和粒子特效。
- tnzbase 的难点不在数量，在 `TRenderer`：它的等待方式依赖 Qt 的事件循环。

---

## 3. iPadOS 需要哪些库不带 Qt

**结论：八个核心库都要，每个库里有一部分文件不进 iPad 构建。** 这一节是本稿的判断，下面写依据；其中 OpenGL 那一部分由所有者定为移出核心库（Q1）。

依据是 Phase 4 的 MVP 范围"画、播、xsheet 基本编辑、导入导出"，以及 D10 的"核心库不链接 Qt"：

| 库 | iPad 为什么要 | 不进 iPad 构建的部分 |
|---|---|---|
| tnzcore | 一切的基础：栅格、矢量、调色板、流、路径、图像缓存、图像读写的框架 | `tipc`、`t32bitsrv_wrap`、`tmsgcore`（删或移到应用一侧）；`tfont_qt`（换成字体服务，iPad 实现走 CoreText）；`tsound_qt`（换成声音输出服务，iPad 实现走 AVAudio）；`tofflinegl`、`qtofflinegl` 与其余桌面 GL（按 Q1 移出） |
| tnzbase | 参数、特效图、渲染调度（`TRenderer`）、缓存、表达式 | 扫描仪（`tscanner/`、`twain/`） |
| tnzext | plastic 骨骼与变形：场景里有 plastic 列就要它；toonzlib 也链接它 | 网格贴图的 GL 部分（按 Q1 移出） |
| toonzlib | 场景模型、xsheet、关卡读写、渲染管线、命令 | QtScript 绑定（移出成单独的库）；viewer 一侧的 GL 绘制器（按 Q1 移出） |
| colorfx | 矢量样式：调色板里有任何非纯色样式，画出来就要它 | 无（GL 入口层按 Q1 移出） |
| stdfx | 播放带特效的场景要渲染特效；绝大多数是纯 CPU 代码，去 Qt 的成本低（2.7 节） | ShaderFx 与其余 GLSL 特效（Phase 3 再迁）；文字类特效先接字体服务 |
| image | 关卡与图像格式：pli、tlv、tzl、png、tif、psd… | 外部 ffmpeg 的读写器（换成编解码服务，iPad 实现走 VideoToolbox）、mov/3gp 代理、avi |
| sound | 声音列要读 wav、aiff | 经 ffmpeg 读 mp3 的那条路（同上） |

tnzcanvas、tnzskia 本来就不带 Qt，已经在 iPad 模拟器里跑过单测（Phase 1）；第 1 步之后 tnzcore 里不带 Qt 的那 10 个源文件也跑过（8.1 节）。

为什么不只做一部分库：iPad 壳要打开桌面版存的场景并把它画出来，场景文件里任何一种对象（plastic 骨骼、colorfx 样式、特效节点）的类都必须能被构造出来，否则加载就失败（`TPersist` 按名字找类，找不到抛异常）。所以"先不带 stdfx"这类取舍只能做到"类在、渲染时不生效"，库本身仍然要编进去；而 stdfx 去 Qt 的成本本来就低，没有理由拆。

不进 iPad 构建的文件不意味着要先删：它们可以留在桌面构建里，由无 Qt 构建的源文件清单把它们排除（第 4 节）。真正要删的只有 D10 已经定了的 `t32bitsrv` 与 QuickTime，以及只为它们存在的 `tipc`。

---

## 4. 目标状态

**过渡期（现在起到 Phase 2 结束前）**：CMake 选项 `WITH_QT`，默认 `ON`。

- `WITH_QT=ON` 是今天的构建，什么都不变。
- `WITH_QT=OFF` 配出一个不找 Qt 的构建：Qt 的包不查、`QT_*_LIB` 不定义、翻译关掉；只加入已经迁移完的库，以及还没迁移完的库里已经不用 Qt 的源文件（每个库一个 `<库>_NOQT_SOURCES` 清单，第 1 步只有 tnzcore 有）；单测只编不需要更多东西的那些。界面、命令行工具、golden、fixture 生成器都不进这个构建。
- 防线是构建本身：不找 Qt 就没有 Qt 的头文件目录，清单里的源文件只要经任何一个头碰到 Qt 就编译失败；库链接不到 Qt，任何隐含的 Qt 依赖都在链接时失败（macOS 的动态库默认不允许未定义符号）。不需要另外写检查脚本。
- 默认构建不受这个选项影响：配置时只多出新加的源文件，第 1 步实测编译命令逐字相同。

**Phase 2 结束时**（计划的完成判据，加上 iPad）：

1. 核心库的默认 CMake 目标不链接 Qt：每个 `<库>_NOQT_SOURCES` 等于它的全部源文件，清单和 `WITH_QT=OFF` 下的特殊处理就删掉；`WITH_QT=OFF` 从此只意味着"不构建界面"。
2. 无 Qt 构建是阻塞的 CI job，编译、链接并通过全部单测与 round-trip 测试。按所有者的平台重点，放在 macOS x86_64 上（Q8）。
3. 核心与渲染路径没有 `QProcess` 或等价的子进程调用（D10 第 2 条）。
4. 桌面 Qt UI 经引擎 API 调用核心的入口（范围见 Q7），golden 全部通过。
5. 同一个无 Qt 构建也不链接桌面 GL，能用 iOS 工具链为 iPad 模拟器编译，核心单测在模拟器里通过（Q1）。

第 4 条按 Q7 的决定只要求入口：tcomposer、tconverter、tcleanupper 和应用的打开、保存、渲染经引擎 API；编辑命令只搬 iPad MVP 需要的那部分，其余随 Phase 3 的 viewer 上画布一起做。

---

## 5. 每项 Qt 设施的替代

| Qt 设施 | 替代 | 说明 |
|---|---|---|
| `QMutex(Recursive)`、`QMutexLocker`（`TThread::Mutex`） | `std::recursive_mutex` 加自己的 `MutexLocker` | 第 1 步，已完成 |
| 其余 `QMutex`、`QReadWriteLock`、`QWaitCondition`、`QSemaphore`、`QThreadStorage`、`QAtomicInt` | `std::mutex`、`std::shared_mutex`、`std::condition_variable_any`、计数信号量、`thread_local`、`std::atomic` | `QThreadStorage` 在线程退出时析构的语义要和 `thread_local` 对齐（11 节） |
| `TThread::Executor`/`Runnable`（QThread、QObject 信号） | `std::thread` 线程池；`Runnable` 不再是 QObject，状态变化经回调通知；回调在哪个线程执行由注入的主线程派发器决定 | 见下面"线程" |
| `TThread::Message`（排队连接到主线程） | 注入的主线程派发器 `post(std::function<void()>)` | 桌面由 toonzqt 安装一个投递到 Qt 事件循环的实现；tcomposer 和单测用一个在等待处主动执行的队列 |
| `TRenderer` 的 `processEvents` 自旋、`qGuiApp` | 等待改为条件变量；GUI 线程上的工作经派发器 | 见下面"线程" |
| `TRenderSettings` 里的 `QOffscreenSurface` | 不透明的 GPU 上下文句柄（`std::shared_ptr<void>` 或一个小接口），由界面一侧创建 | 只剩 ShaderFx 用 |
| `QString` 与 std 字符串的转换（`tstring.cpp`、`tconvert.h`） | 自己写的 UTF-8、UTF-16、UTF-32 转换 | 对无效序列、代理对、BOM 的处理必须和 QString 逐字相同，用差分测试证明（第 8 节） |
| `TFilePath`、`TFrameId` 里的 QString、QChar、QRegularExpression | `std::wstring` 与手写的解析 | 路径规则（帧号、字母、序列、点号）有大量细节，现有 `test_tfilepath.cpp` 加一个路径语料的差分测试 |
| `tsystem`、`tenv`、`tfilepath_io` 的 QDir、QFile、QFileInfo、QDateTime、QStandardPaths | `std::filesystem`、`std::chrono`；标准目录按平台取（Apple 上用 Foundation） | `TSystem` 的接口返回 QString、QDateTime，界面大量使用，接口改造见第 6 节；iPad 的安全作用域文件访问要求文件系统能被注入，留接口 |
| `QSettings`（`Preferences`、`tenv`、`tcacheresourcepool`） | 注入的键值存储接口；桌面实现仍用 QSettings、放在 toonzqt，文件格式不变 | 见 Q4 |
| Handle 的 Qt 信号 | 观察者回调（一个小的、只在核心内部用的信号类型）；toonzqt 里每个 Handle 配一个 QObject 适配器，把回调转成原来的 Qt 信号 | 界面那 337 处 `connect` 原样不动，只是连到适配器上 |
| QObject 模型类（`TXshLevel`、`TXshSoundColumn`、`TUndoManager`、`MovieRenderer`…） | 普通类加观察者回调；需要 Qt 信号的地方同上配适配器 | `TUndoManager` 的 `historyChanged` 之类被界面大量连接 |
| `QMessageBox`、`QApplication::setOverrideCursor`（toonzlib） | 注入的"询问用户"和"忙碌提示"回调 | 2 处 |
| `orientation.cpp` 的 UI 几何（QRect、QPainterPath、QPixmap） | 移到 toonzqt | 它只服务于 xsheet 控件 |
| `TFontParam` 的 QFont | 一个字体描述结构（字族、样式、大小……），界面转换 | 存档格式按 `QFont::toString()` 的字符串保持不变 |
| `TCacheResource` 的 QRegion、QRect | 一个只做矩形并集与求交的小区域类 | 缓存命中会影响渲染是否重算，结果要逐位相同，golden 加多线程对照 |
| `QStack`、`QList`、`QMap`、`QHash`、`QCache` | std 容器 | 机械替换；`QHash` 的遍历顺序与 `std::unordered_map` 不同，凡是按遍历顺序写文件或画图的地方要检查 |
| QImage、QPainter 写字，`tfont_qt` | 注入的文字与字体服务；桌面实现继续用 Qt，放在界面一侧；iPad 实现走 CoreText | 桌面像素不变（见 Q5） |
| `QProcess` 调 lzocompress | 直接链接 LZO（`thirdparty/lzo` 已经在树里，`lzodriver` 也是它编的） | 输出字节必须相同，tlv/tzl 的 round-trip 和 golden 证明 |
| `QProcess` 调 ffmpeg | 注入的编解码服务接口；桌面实现保留外部进程（Q6），iPad 实现走 VideoToolbox | |
| QtScript | 绑定移出 toonzlib，成为单独的可选库 | 见 Q3 之外的 Q7；脚本引擎的长期选择计划定在 Phase 2 末 |
| `tipc`、`t32bitsrv`、mov/3gp 代理、QuickTime | 删除 | D10 已定 |
| `TMsgCore` | 移到应用一侧（toonzqt 或 toonz） | 核心里只留一个"发消息"的回调 |
| 翻译（`QCoreApplication::translate`、`tr`） | 核心只产生未翻译的源文本加上下文，界面翻译 | 见 Q2；lupdate 照样能从源码里提取，现有 .ts 文件不变 |
| `QAudioOutput`（`tsound_qt`） | 注入的声音输出服务；桌面实现保留 Qt Multimedia、放在界面一侧 | |
| OpenGL | 不是 Qt 设施，但决定 iPad 能不能编。按 Q1，viewer 一侧的 GL 入口移到界面一侧的新库 `tnzglview`；离线渲染的 GL 退路（`NEXTTOONZ_VECTOR_RASTERIZER=gl`）改成由 `tnzglview` 注册的栅格化后端，核心只认 `tvectorrasterizer.h` 的接口 | 第 8 步起 |

**线程**这一项展开说，因为它决定引擎 API 的形状：

- 线程池：`Executor` 的语义（按优先级排队、按依赖等待、取消、每个执行器的并发上限）原样保留，工作线程换成 `std::thread`。`Runnable::runningPriority()` 返回的 `QThread::Priority` 换成自己的枚举；macOS 上线程优先级经 pthread 的 QoS 设定，Linux 上 Qt 本来就基本不设，行为差异记在风险里。
- 派发器：核心声明一个接口 `TMainThreadDispatcher`，只有 `post(task)` 和 `isMainThread()` 两个方法，进程启动时注入一次。桌面应用注入一个投递到 Qt 事件循环的实现（行为和现在的排队连接相同）；tcomposer、tconverter、单测、将来的 iPad 壳各注入自己的。没有注入时，退回到"在调用线程上立刻执行"，这是今天 `TThread::Message::send` 在主线程上调用时的行为。
- `Runnable` 的信号：`started`/`finished`/`canceled`/`exception`/`progressChanged` 换成回调，回调经派发器在主线程执行（和今天的排队连接一样）。核心外的 7 处 `connect` 改成在 toonzqt 的适配器上连。
- `TRenderer` 的等待：今天在 GUI 线程上 `processEvents` 自旋，等渲染结束时界面不冻结。改成条件变量以后，GUI 线程上调用阻塞等待就会冻结界面。所以桌面要保留"等的时候处理事件"的能力：派发器接口多一个可选的 `waitUntil(predicate)`，Qt 的实现在里面跑事件循环，其他实现直接在条件变量上等。这是唯一一处语义上要小心的地方。

---

## 6. 给界面留的 Qt 便利接口

去掉核心头里的 Qt 类型，最直接的办法是把重载删掉、让界面显式转换，但那会让一个头的改动波及几百个界面文件（第 1 步一个互斥锁就改了 27 个界面文件）。本稿建议（Q2）：

- 核心头里的 Qt 便利重载改成**内联适配**，放在同一个头的末尾，只在定义了 `NEXTTOONZ_QT_INTEROP` 时编译。这个宏由一个 INTERFACE 目标 `nexttoonz_qt_interop` 的使用要求带出来，只有界面一侧的目标链接它。例如 `tfilepath.h` 末尾：`inline TFilePath toTFilePath(const QString &s)`，以及保持现有调用写法不变的非成员重载。
- 适配只能是内联的、只能做转换，不能有状态，也不能改变类的布局：同一个类在带宏和不带宏的编译单元里必须完全相同（否则违反 ODR）。成员函数形式的重载（比如 `TFilePath(const QString &)` 构造函数）要改成非成员的转换函数，界面那边机械替换；能改成非成员运算符的尽量改，以减少界面的改动。
- 核心库自己的源文件不链接这个目标，因此在核心库里用 QString 重载会编译失败，这正是想要的。
- 到 Phase 3 合并 Qt6 时，适配集中在少数几个头里，改起来也集中。

---

## 7. 迁移顺序

每一步都在默认构建里 golden 和单测不变的前提下单独合并；"进入无 Qt 构建"一栏是这一步之后可以加进 `<库>_NOQT_SOURCES` 的源文件（按 2.1 节的方法模拟，做的时候以实际链接为准）。验收的做法见第 8 节，下面只写每步特有的部分。

| 步 | 内容 | 进入无 Qt 构建 | 验收 |
|---|---|---|---|
| 0（本稿） | 实测清单与设计 | | |
| 1（已完成） | `TThread::Mutex`/`MutexLocker` 换成 std；另外六处 `QMutex::Recursive`；`tpalette.h`、`tstroke.h`、`tgldisplaylistsmanager.h` 去掉 `<QMutex>`；`WITH_QT` 选项与无 Qt 构建的骨架 | tnzcore 10 个：tgeometry、tcolor（5）、trandom、tstopwatch、tdebugmessage、tsmartpointer | 8.1 节 |
| 2（已完成，未推送） | 无 Qt 构建进 CI（Q8）：macOS x86_64 上一个不装 Qt 的 job，构建、检查没有任何东西碰到 Qt、跑单测，再在 iPad 模拟器里跑核心单测 | | 8.2 节；推送后 CI 的新 job 绿，要所有者同意推送 |
| 3（已完成） | 字符串转换：`tstring.cpp` 不用 QString，`tconvert.h` 的 QString 重载改成第 6 节的适配 | tnzcore 8 个：`tstring`、`tmathutil`、`texception`、`tcurves`、`tcurveutil`、`tstrokeoutline`、`cornerdetector`、`tsound`（`tstroke` 要等第 8 步，见 8.3 节） | 8.3 节 |
| 4 | `TFilePath`、`TFrameId` 不用 QString、QChar、QRegularExpression；QString 重载改成适配 | `tfilepath.cpp`、`tfiletype.cpp`、`tdata.cpp`… | 现有 `test_tfilepath.cpp`；加一个路径语料（参考工程、stuff 目录里的所有文件名，加构造的边界情况）的差分测试，新旧 `getName`/`getFrame`/`withFrame`/`getLevelName` 等逐字相同 |
| 5 | `tstream.h`、`timagecache.h`、`timage_io.h`、`tiio.h`、`tsound_io.h` 的 QString、QStringList | 栅格（`traster.cpp`）、trop 全部、图像读写框架、`tstream.cpp` | 现有 tstream、traster 的单测进入无 Qt 构建；round-trip 测试 |
| 6 | `tsystem`、`tenv`、`tfilepath_io`：文件系统与时间换成 std；`TSystem` 接口里的 QString、QDateTime 改适配；`tenv` 的 QSettings 接到第 15 步的键值存储前先用一个过渡实现 | `tsystem*`、`tenv`、`tbigmemorymanager`、`tlogger`… | `TSystem` 的单测（新写：列目录、时间戳、复制、删除到临时目录）；`stuff` 目录的初始化在新旧构建里产生相同的文件树 |
| 7 | 样式描述与翻译：`tcolorstyles.h` 的 `getDescription`、参数名改成源文本加上下文（Q2）；`tsimplecolorstyles`、`tpalette`、colorfx 的 `translate` 一并改 | 矢量图（`tvectorimage`、`tregion`、`tcomputeregions`…）、样式；colorfx 的非 GL 部分（GL 入口在第 8 步移出） | 每个注册样式的名字在界面里显示的译文与改动前相同（一个测试：遍历注册表，经 Qt 翻译后与旧实现逐字比较） |
| 8 | tnzcore 与 colorfx 的桌面 GL 移出核心（Q1）：`tgl.cpp`、`tstencilcontrol`、`tgldisplaylistsmanager`、`tofflinegl`/`qtofflinegl`、`tglcanvasbridge`、`tglregions` 的 GL 画法、`drawutil` 的 `drawStrokeCenterline`、样式的 GL 入口层，放进界面一侧的新库 `tnzglview`（toonzqt、tnztools、toonz 链接它，也由它链接 `tnzglcanvas`）；离线的 GL 退路改成 `tnzglview` 注册的栅格化后端 | tnzcore 的矢量与样式部分；colorfx 全部；tnzcore 不再链接 GL；核心单测加进 iPad 模拟器（`run_ios_noqt.sh`） | 默认 Skia 路径逐字节相同；GL 退路单线程逐字节相同（tcomposer 链接 `tnzglview`）；viewer 的绘制只能人工验收 |
| 9 | 线程：`TThread::Executor`/`Runnable` 换成线程池，`TThread::Message` 换成派发器，`QThreadStorage`、`QReadWriteLock`、`QWaitCondition` 换成 std；toonzqt 加 Runnable 的 Qt 适配和 Qt 派发器 | `tthread.cpp`、`threadmessage.cpp`、`timagecache.cpp`… | golden 用 `-nthreads 1` 和 `-nthreads 8` 各渲一遍，与改动前逐字节相同；线程池的单测在 ThreadSanitizer 下跑；应用里人工验收（预览、渲染进度、取消） |
| 10 | tnzbase：`TRenderer` 去掉自旋与 `qGuiApp`，`TRenderSettings` 的离屏表面改成不透明句柄；`tundo.h` 的 `TUndoManager` 去 QObject；`TFontParam`；`tfxattributes`；`TCacheResource` 的 QRegion；`tcacheresourcepool` 的 QSettings、QDir | tnzbase 全部（扫描仪除外）；stdfx 的大部分（按模拟 162 个） | 同第 9 步的多线程 golden；预测缓存与被动缓存的单测（新写）；`.tnz` 里字体参数的读写不变 |
| 11 | 引擎 API 第一版（第 9 节），tcomposer 改用它 | | tcomposer 的全部 golden 与改动前逐字节相同 |
| 12 | tnzext：`plasticskeleton` 的骨骼名、`ttexturesstorage` 的 QCache 与 QString 键、`meshtexturizer` 的 QReadWriteLock；它们的 GL 部分移到 `tnzglview`（Q1） | tnzext 全部 | `plastic_basic` 的 golden；plastic 场景的读写 round-trip |
| 13 | 外部进程：LZO 直接链接；ffmpeg 换成编解码服务接口（桌面实现仍是外部进程）；删除 `t32bitsrv`、mov/3gp 代理、QuickTime、`tipc`；`TMsgCore` 移到应用一侧；`tsound_qt` 换成声音输出服务 | sound 全部；image 的格式框架 | tlv、tzl 写出的字节与改动前相同（新写的 round-trip 测试）；含 tlv 的 golden；D10 第 2 条在核心里满足 |
| 14 | image 的其余：sprite 的 QImage、psd 的 QTextCodec、`Preferences` 的 3 处 | image 全部 | 各格式的读写测试（`test_level_formats.cpp` 加 psd、sprite） |
| 15 | `Preferences` 换成注入的键值存储（Q4），桌面实现放进 toonzqt、文件格式不变 | `preferences.cpp` 移出或变薄 | 用改动前的构建写一份 preferences.ini，新构建读进再写出，逐字节相同 |
| 16 | Handle 的信号换成观察者回调，toonzqt 配 Qt 适配器 | 9 个 Handle、`PaletteController` | toonzlib 的单测加 Handle 通知的测试；应用里人工验收（切换场景、帧、列、调色板） |
| 17 | 其余 QObject 模型类去 QObject；`QMessageBox`、光标改成回调；`orientation.cpp` 移到 toonzqt；toonzlib 的 viewer 一侧 GL（`Stage::RasterPainter` 的 GL 分支、`OpenGlPainter`、`ImagePainter`、第二个 `renderFrame`）移到 `tnzglview`（Q1） | toonzlib 的大部分 | 同第 16 步 |
| 18 | 文字与字体：`tfont_qt`、toonzlib 里写字的 QPainter、stdfx 的文字特效改成注入的文字服务（Q5），桌面实现仍用 Qt | 剩下的 toonzlib、stdfx 的文字特效 | 字体场景（`fx_sweep_font_*`，只在 Linux 上比较）和片头板在新旧构建下逐字节相同 |
| 19 | QtScript 绑定移出成单独的可选库（Q7 之外的 Q3） | toonzlib 全部 | 脚本控制台的测试（新写：跑一段脚本加载场景、栅格化） |
| 20 | 收口：核心库的默认目标不链接 Qt；`<库>_NOQT_SOURCES` 删掉；无 Qt 的 CI job 改为阻塞，核心库不链接桌面 GL，全部核心单测在 iPad 模拟器里通过（Q1） | 全部 | Phase 2 的完成判据（第 4 节） |
| 21 | 界面里的编辑命令搬进库、配 characterization 测试；插件宿主的渲染适配器搬到核心。按 Q7 只做 iPad MVP 需要的命令（cell、column、level 的基本编辑），其余放到 Phase 3 | | 命令的单测；应用里人工验收 |

几点说明：

- 第 3 到第 7 步都是改头文件，每一步都会波及界面；按第 6 节的适配做，波及应当是"加一个 include 或换一个函数名"那种机械改动。
- 第 9、10 步是整个 Phase 2 风险最大的两步：自动测试覆盖的是 tcomposer，应用的交互（预览、取消、进度）只能人工验收。
- 第 11 步可以提前到第 9 步之后的任何时候：引擎 API 的头不带 Qt，实现开始时可以照样链接 Qt。放在这里是因为它需要派发器（第 9 步）。
- 第 21 步按 Q7 缩小了范围，其余命令在 Phase 3。
- 第 8 步是 Q1 的决定带来的，原来的表里没有；它之后的编号都比 0.1 版大一。

---

## 8. 每一步怎样验证"默认路径没有变"

每一步都做下面几项，做法沿用 Phase 1 的 8.1 节：

- **默认构建的编译命令**：改动前后各存一份 `compile_commands.json` 和 `build.ninja`，比较。CMake 改动必须只带来预期的差别。这一项能做到逐字相同。
- **单测**：改动前后都全部通过（`golden_plastic_basic` 之外；第 1 步实测它现在也通过了，Phase 1 改用 Skia 之后本机显卡不再影响它）。
- **golden 场景**：ctest 的 25 个场景，改动后的帧与改动前逐字节相同（不只是容差内）。
- **全部 28 个场景的单线程与 8 线程渲染**：用改动前构建的 tcomposer 副本和改动后的 tcomposer 各渲一遍，`-nthreads 1` 和 `-nthreads 8`，逐字节比较（`toonz/build-phase1/ab/phase2/ab_tcomposer.py`）。比 ctest 多了只在 Linux 上比较的场景（字体、`rand()`、`vector_styles`）和多线程。跳过会在 macOS 上崩溃的 GLSL 场景。
- **OpenGL 退路**：`NEXTTOONZ_VECTOR_RASTERIZER=gl` 下同样渲一遍。单线程能逐字节比较；多线程不能，因为这条路本来就不确定（8.1 节）。
- **无 Qt 构建**：`WITH_QT=OFF` 配置、编译、跑单测；它的编译命令里不能出现任何 Qt 路径或 `QT_` 宏。
- **ThreadSanitizer**：碰线程的步骤，无 Qt 构建另配一个 Debug 加 `-fsanitize=thread` 的版本跑核心单测（Debug 下 `TSmartObject` 的实例计数也打开）。
- **Linux 虚拟机**：`nt-asan` 里同步分支、构建全部目标、按清洁剂 CI 的环境跑全部测试，再配一个无 Qt 构建（`toonz/build-phase1/vm/phase2_step1.sh`）。macOS 上编不到的 `#ifdef LINUX` 代码只有这里能查到（第 1 步就查出一处）。
- **差分测试**：替换字符串转换、路径解析、设置文件这类有精确语义的东西时，测试同时链接旧的 Qt 实现和新实现，在语料上逐字比较。旧实现只留在测试里，Qt 构建才编；它同时是以后的回归测试。

能做到逐位相同的地方：编译命令、golden 帧、多线程渲染（默认 Skia 路径）、单线程的 GL 退路、字符串与路径的差分、设置文件和 tlv 写出的字节。做不到的：GL 退路的多线程渲染（本来就不确定）、线程的调度顺序、应用的交互行为（只能人工）。

### 8.1 第 1 步实际做成的样子（2026-11-17）

**代码。** `include/tthreadmessage.h` 里 `TThread::Mutex` 包一个 `std::recursive_mutex`，有 `lock`、`tryLock`、`unlock`，另有 `try_lock` 让 `std::lock_guard`、`std::unique_lock` 也能用；不可复制。`TThread::MutexLocker` 照 `QMutexLocker` 的行为写：空指针什么也不做，`unlock()` 之后再 `unlock()` 无效果，`relock()` 只在未锁时加锁，析构时只释放自己持有的那一次。原来那段只在没人定义的 `TNZCORE_LIGHT` 下编译的 Windows 句柄实现是死代码，删了。界面和库里凡是用 `QMutexLocker` 锁这些互斥锁的地方由编译器找出来（换掉基类后它们编译失败），连同下面几处一共 52 个文件改用 `TThread::MutexLocker`，另有 `movierenderer.cpp` 里一个指向 `QMutexLocker` 的指针类型。另外：

- `tpalette.h` 的互斥锁原来就是 `QMutex(QMutex::Recursive)`，换成 `TThread::Mutex`，`mutex()` 的返回类型随之改变，`tcolumnfx.cpp` 里直接 `lock()`/`unlock()` 的用法不受影响。
- `tgldisplaylistsmanager.h` 的是非递归的 `QMutex`，换成 `std::mutex`，唯一的使用方（`ttexturesstorage.cpp`）用 `std::lock_guard`。
- `tstroke.h` 包含了 `<QMutex>` 但没用，删掉了包含。
- 另外六处 `QMutex::Recursive`（tnzext 的贴图存储和 plastic 变形器存储、tnzbase 的被动缓存管理器和缓存资源池、tnzcore 的 Qt 声音输出）都换成 `TThread::Mutex`。全树不再有 `QMutex::Recursive`。
- `toonzlib/tbinarizer.cpp` 原来经 `<QMutex>` 间接用到 `qSwap`，换成 `std::swap`。
- Windows 专用的 `tiio_avi.cpp`、`tiio_movW.cpp`、`tiio_3gpW.cpp` 和 Linux 专用的 `tofflinegl.cpp` 的 GLX 部分也改了；前三个没有编译过（11 节），最后一个是 Linux 虚拟机的构建查出来的。
- 13 个文件（`trenderer.cpp`、`timagecache.cpp`、`shaderfx.cpp`、`scriptbinding_renderer.cpp`、toonzqt 和 toonz 的几个）用 `QMutex` 却没有自己包含它，原来是经 `tthreadmessage.h` 得到的。macOS 上 Qt 框架的其他头会把它带进来，所以本机编译通过；Linux 的 Qt 头不会，虚拟机里编译失败。给这 13 个文件补上了 `#include <QMutex>`（另外两个只在 32 位 Apple 上编译的 QuickTime 文件没有补，它们在第 13 步删除）。

**构建。** 顶层 `CMakeLists.txt` 加了 `WITH_QT`（默认 `ON`）。`OFF` 时：Homebrew 的 Qt 不查、`find_package(Qt5 ...)` 和版本检查跳过、`QT_*_LIB` 不定义、翻译关掉；子目录只加 tnzcanvas、tnzskia（`WITH_SKIA` 时）和 tnzcore，然后是 tests，之后 `return()`，不往下加别的库和程序。`tnzcore/CMakeLists.txt` 里加了 `TNZCORE_NOQT_SOURCES` 清单，配置时检查清单里的每一项都在 `SOURCES` 里；`OFF` 时 tnzcore 只由这些文件组成，不跑 moc、不链接任何东西。`tests/CMakeLists.txt` 里加了 `NEXTTOONZ_CORE_NOQT_TEST_SOURCES`（`test_tgeometry.cpp` 和两个新测试），`OFF` 时核心测试程序只由它们组成，测试名仍是 `core.*`；toonzlib、GL 画布、Skia spike、golden、fixture 生成器在 `OFF` 时不加。第三方库照常查找（OpenCV、TIFF、Boost 等仍然是必需的，`superlu41` 也照常编译），因为后面迁移的库要用；这一步没有为此另开一条配置路径。

**新测试。** `tests/core/test_tthreadmutex.cpp`（10 个）：同一线程重入、别的线程 `tryLock` 失败、8 个线程各加锁 2 万次不重叠且计数正确、`std::lock_guard`/`std::unique_lock` 可用、`MutexLocker` 的作用域、嵌套、空指针、`unlock`/`relock` 的幂等和析构。`tests/core/test_tsmartpointer.cpp`（3 个，Debug 下 4 个）：引用计数与删除、移动不改计数、8 个线程同时复制同一个指针后计数复原；Debug 下还测 8 个线程同时创建同一类的第一个对象时实例计数器只建出一个（这个计数器就是 `tsmartpointer.cpp` 里用互斥锁保护的东西）。两个文件在默认构建里也编进 `nexttoonz_core_tests`。`tests/core/run_ios_noqt.sh` 用 `tests/ios_gtest.sh` 把无 Qt 构建里的 tnzcore 源文件和核心单测编给 iPadOS，文件清单直接从两个 CMake 清单里读。

**结果（x86_64 macOS）。**

- 默认构建：重新配置后编译命令与改动前逐字相同，只多了两个新测试文件；`build.ninja` 的差别也只有这两个文件的构建规则。
- 全部测试：434 个全部通过（改动前 421 个加新的 13 个），包括 `golden_plastic_basic`。
- golden：ctest 的 25 个场景 266 帧与改动前逐字节相同。
- 28 个场景（跳过 GLSL 的）单线程和 8 线程各 277 帧，一共 554 帧，与改动前构建的 tcomposer 渲的逐字节相同。
- OpenGL 退路：单线程 277 帧逐字节相同。8 线程不能比较：用改动前的构建连渲两遍，两遍之间就有 18 帧不同，而且 `vector_cmapped_fx` 会间歇地段错误；改动后的构建表现相同。这是原有的问题，记为已知问题第 26 条，不是这一步引入的；默认的 Skia 路径在 8 线程下是确定的。
- 无 Qt 构建（`toonz/build-noqt`，带 Skia）：编译命令里没有任何 Qt 路径或 `QT_` 宏；`libtnzcore.dylib` 只链接 libc++ 和 libSystem；114 个测试（核心 26、画布 31、Skia 画布 57）全部通过。
- ThreadSanitizer：无 Qt 构建的 Debug 加 TSan 版本，核心单测 27 个全部通过，没有报告。
- iPad 模拟器（iOS 15.5 运行时，x86_64 模拟器）：`run_ios_noqt.sh simulator` 编出的 26 个核心单测全部通过；`device` 模式为 arm64 编译并链接成功（真机没有跑，要签名的应用包）。需要 `-DMACOSX`：`tnztypes.h` 按平台宏选整数类型，没有为 iOS 准备分支，Apple 的那一支合用。

**结果（Linux 虚拟机，Ubuntu 24.04，clang，ASan + UBSan + LSan）。** 同步分支、构建全部目标（不只是清洁剂 CI 编的那几个，以便查出界面里只在 Linux 上编译的代码），按清洁剂 CI 的环境跑全部测试（`toonz/build-phase1/vm/phase2_step1.sh`）。前两次同步各查出一类问题（上面的 `tofflinegl.cpp` 和 13 个缺 `#include <QMutex>` 的文件），改掉后：全部目标编译通过；408 个测试里 407 个通过，失败的是 `golden_fx_sweep_font_a`，它在这个虚拟机里一直失败（基线要 GitHub runner 的字体，记在 Phase 0 的说明里），与本步无关；GL 画布的 32 个测试在 xvfb 下全部通过；无 Qt 构建（clang，带 Skia）编译命令里没有 Qt，`libtnzcore.so` 只依赖 libstdc++、libm、libgcc_s、libc，114 个测试全部通过。Linux 上没有做 golden 帧的逐字节前后对照：golden 测试本身按容差通过，这一步不碰像素计算。

**没有做的。** 没有推送，所以 CI 没有跑（Ubuntu gcc、Fedora、macOS arm64 这几个 job 本机没有对应环境）；Windows 专用的三个文件没有编译过；arm64 只在 iPad 的 device 模式下编译链接过，没有运行；应用没有打开过（互斥锁的改动不改行为，交互路径只靠编译和测试覆盖）。互斥锁的性能没有单独测：`std::recursive_mutex` 在 macOS 上是 pthread 的递归锁，和 Qt 5 的递归 QMutex 的实现方式不同，golden 的总时间没有可见变化，但没有做基准。

### 8.2 第 2 步实际做成的样子（2026-11-18）

`.github/workflows/workflow_macos.yml` 加了一个 job `macOS-noqt`，跑在 `macos-15-intel` 上。它装 macOS job 装的那些库，唯独不装 Qt；编 tiff、取 Skia 预编译包（和 macOS job 共用缓存的键），浅克隆 GoogleTest v1.17.0 的源码供 iPad 用，然后调 `ci-scripts/noqt/noqt_test.sh`。这个脚本本机也能原样跑，做四件事：

1. 用 `WITH_QT=OFF` 配置并构建。
2. 检查没有任何东西碰到 Qt，三道：`compile_commands.json` 里没有 Qt 的目录或 `QT_*_LIB` 宏；`ninja -t deps` 记下的每个目标文件依赖的头里没有 Qt 的头，这一道连"Qt 恰好装在默认包含目录里、编译命令里看不出来"的情况也能查出；构建出的 `libtnz*` 没有一个链接 Qt。三道检查拿默认构建试过，都会报错。
3. `ctest`。
4. 给了 GoogleTest 源码时，`run_ios_noqt.sh simulator` 在 iPad 模拟器里跑核心单测。

本机（x86_64 macOS，带 Skia）：脚本退出码 0，7 个库没有一个碰到 Qt，114 个测试通过，iPad 模拟器里 26 个通过。CI 上没有跑过：要推送，推送要所有者同意。runner 上会不会已经装了 Qt、iPad 模拟器是否可用，都只能推送后才知道；前一件 job 会在日志里打印出来，后一件失败时 job 失败。

这个 job 现在还不是阻塞的：仓库没有分支保护（计划的待定事项"CI 阻塞合并的落实方式"），第 20 步收口时再定。

### 8.3 第 3 步实际做成的样子（2026-11-18）

**代码。** `common/tcore/tstring.cpp` 的 `to_wstring(std::string)` 和 `to_string(std::wstring)` 不再经过 QString，改成自己的 UTF-8、UTF-16、UTF-32 转换。原来的代码表面上在"判断是不是 UTF-8"，实际上那段判断永远不成立（Qt 5 的 `fromStdString` 本来就按 UTF-8 解码），所以真正的语义是 Qt 5 的解码与编码规则本身。先写了一个链接 Qt 的探针程序，把 Qt 在边界情况上的行为一条条量出来，再照着实现，最后用差分测试证明逐字相同。量出来的规则（都写进了 `tstring.cpp` 开头的注释）：

- UTF-8 到宽字符：字符串开头的一个 BOM 丢掉（只丢第一个）；解不出的序列，首字节换成一个 U+FFFD，再从下一个字节接着解，所以每个孤立的续字节各得一个；被字符串结尾截断的序列，每个字节各一个 U+FFFD；超长形式、代理区、超过 U+10FFFF 都算解不出；非字符（U+FFFE 等）照常解出；NUL 保留。
- 宽字符到 UTF-8：Qt 按 UTF-32 读宽字符串，并且做字节序检测：第一个是 U+FEFF 就丢掉，第一个是 0xFFFE0000 就丢掉并把后面所有的值字节反转；0x10000 以上的值不检查范围地拆成代理对，所以超过 U+10FFFF 的值会变成坏的代理，甚至变成 NUL；转 UTF-8 时不成对的代理变成 `?`；结果在第一个 NUL 处截断——ASCII 那条分支本来不截断，但它判断"是不是 ASCII"时用的是按 `qstrnlen` 量长度的比较，含 NUL 的字符串必然走到截断的那条分支。

`to_string(const TFilePath&)` 的定义从 `tstring.cpp` 搬到 `tfilepath.cpp`（声明不变），`tstring.cpp` 于是不再包含 `tfilepath.h`。死代码 `TNZCORE_LIGHT` 分支删掉。

`tconvert.h` 的两个 `fromStr(…, QString)` 重载放进 `#ifdef NEXTTOONZ_QT_INTEROP`，这是第 6 节适配机制的第一次使用：顶层 `CMakeLists.txt` 在 `WITH_QT` 时定义 INTERFACE 目标 `nexttoonz_qt_interop`（带这个宏、链接 `Qt5::Core`），用到这两个重载的只有应用（`toonz/batches.cpp`）和两个渲染农场程序，它们链接这个目标。`tconvert.h` 不再包含 `<QString>` 以后，有两个头原来是经它拿到 QString 的：`tproperty.h` 和 `toonzfarm/include/tfarmproxy.h`，各自补了 `#include <QString>`（`tproperty.h` 本身就用 QString，是个泄漏者，后面的步骤再处理）。

**进入无 Qt 构建的。** 按包含树，这一步之后又有 7 个编译单元不再碰到 Qt（tnzcore 4 个，tnzbase 的 `tunit.cpp`，toonzlib 2 个），核心库里碰到 Qt 的从 510 降到 503。链接闭合的是 tnzcore 的 8 个：`tstring`、`tmathutil`、`texception`、`tcurves`、`tcurveutil`、`tstrokeoutline`、`cornerdetector`、`tsound`，`TNZCORE_NOQT_SOURCES` 从 10 个变成 18 个。`tstroke.cpp` 编译时已经不碰 Qt，但链接要 `TOutlineUtil::computeBBox`，它和 `makeOutline` 在 `tellipticbrush.cpp` 里，而 `makeOutline` 在调用方不给像素大小时会去问 OpenGL（`tglGetPixelSize2`，画布文档第 10 节记过的隐藏读取）；所以笔触等第 8 步。tnzbase 的 `tunit.cpp` 也不碰 Qt 了，等 tnzbase 有了自己的清单再进。

**新测试。** `tests/core/test_tstring_qt.cpp`（只在 Qt 构建里编）把改动前的两个函数原样留在测试里，与新实现逐字比较：两字节以内的全部 65,792 个字节串；从 33 个"有意义"的字节（ASCII、NUL、续字节与首字节的每个边界、BOM 的字节、非法首字节）里取的全部 3 到 5 字节组合，约 120 万个；U+0000 到 U+10FFFF 每个码点的合法编码，部分再加 BOM 或夹在 ASCII 之间；30 万个随机字节串；宽字符方向，U+0000 到 U+10FFFF 的每个值以及一批越界值，各在单独、前面有字符、后面有字符、重复两次四种位置；30 万个随机宽字符串（混入代理、BOM、反字节序的 BOM 和任意 32 位值）。6 个测试一共约 4 秒，全部逐字相同。`tests/core/test_tstring.cpp`（8 个）把探针量出的那些规则写成固定的期望值，不需要 Qt，进了无 Qt 构建和 iPad 模拟器。

**结果（x86_64 macOS）。**

- 默认构建的编译命令：只多了 `test_tstring.cpp`；变了的 353 个编译单元全是应用（351 个）和两个农场程序，变化只有一处，多了 `-DNEXTTOONZ_QT_INTEROP`。核心库的编译命令一个都没变。
- 全部测试：448 个通过（第 1 步之后的 434 个加 14 个新的）。
- golden：25 个场景 266 帧与 Phase 2 之前逐字节相同。
- 28 个场景单线程和 8 线程的 554 帧：550 帧相同，`fx_sweep_font_a` 的 2 帧（两种线程数各 2 帧）不同。原因不在这一步：用 Phase 2 之前构建的 tcomposer 副本当天重渲这个场景，结果与这一步的构建逐字节相同，而与前一天它自己渲的不同。这个场景的文字特效没有指定字族，用的是系统默认字体，本机的字体状态隔夜变了（计划里它本来就只在 Linux 上比较）。以后对照字体场景，"改动前"要当天重渲。
- OpenGL 退路单线程 277 帧：同上，只有 `fx_sweep_font_a` 的 2 帧不同，原因相同。
- 无 Qt 构建（`ci-scripts/noqt/noqt_test.sh`）：三道检查通过，122 个测试通过，iPad 模拟器里 34 个通过。

**结果（Linux 虚拟机）。** 又是 macOS 编得过、Linux 编不过：`tsound.cpp` 和 `toonzlib/sandor_fxs/toonz4_6staff.cpp` 用的 `memset`、`strlen`、`strcpy` 原来经 `<QString>` 间接得到，各补了 `#include <cstring>`。会出这种问题的正好是这一步之后不再包含任何 Qt 头的那几个编译单元，所以以后每步都可以先把这批文件挑出来查。改掉以后：全部目标编译通过；422 个测试里 421 个通过，失败的仍是虚拟机里一直失败的 `golden_fx_sweep_font_a`；差分测试的 6 个和固定值测试的 8 个在 Linux 的 Qt 5.15.13 加 libstdc++ 上也全部通过；GL 画布 32 个通过；无 Qt 构建 122 个通过。

---

## 9. 引擎 API 的第一版

D3 定了形状：无模板、`noexcept`、具体类型的 C++ 头，错误用返回值，不暴露 Qt 类型和 `TRasterPT<T>` 之类的模板实例；C ABI 等第一个非 C++ 消费者出现时从它机械派生。本稿只定第一版覆盖什么、什么时候出现。

**第一版只覆盖离线渲染**，也就是 tcomposer 今天做的事：

- `nt::Engine`：用一个配置创建（stuff 目录、键值存储、主线程派发器、线程数、日志回调），进程里一个。它负责注册特效和格式（今天 tcomposer 的 `main` 里那一串初始化）。
- `nt::Scene`：打开、保存、帧数、摄像机尺寸、输出设置；打开失败返回错误码和一段消息。
- `nt::RenderJob`：渲染一段帧，结果要么写文件（按场景的输出设置），要么交给调用方一块像素缓冲（宽、高、行距、像素格式，平铺的 `uint8_t*`，不是 `TRaster32P`）；进度和取消用回调。

**为什么从这里开始。** tcomposer 是现成的、被 golden 测着的唯一消费者：把它改成只经引擎 API 调核心，渲染结果必须逐字节不变，这一步就有了最强的验收。iPad 的第一个垂直切片（打开一个桌面存的场景、画一帧）用的也正是这一组接口。界面的其余部分（xsheet 编辑、工具、Handle）要等第 16、17 步之后才有稳定的东西可以包。

**什么时候出现。** 第 11 步，在线程那一步之后：引擎要持有派发器和线程池，这两样第 9 步才有。头文件从第一天起不带 Qt；实现可以在库还没迁移完时照样链接 Qt，随着后面的步骤自然变成不带 Qt。头文件放在 `include/engine/`，实现是一个新库 `nexttoonzengine`，tcomposer、tconverter、tcleanupper 先后改用它。

**以后的版本**：第 16 步之后加 xsheet 的读与基本编辑、关卡与帧的访问、变更通知（观察者），第 21 步之后加命令。到 Phase 4 由这些头派生 C ABI。

---

## 10. 需要所有者决定的问题

**所有者的决定（2026-11-18）：Q1 到 Q8 都按本节写的倾向执行，Q4、Q5 标为要跟进。** 所有者的原话是逐条的"可以"，Q4 加了"但 mark 一下需要 followup"，Q5 同上。即：

- Q1 取 (b)：每迁移一个库就把它 viewer 一侧的 GL 入口移到界面一侧的新库 `tnzglview`，无 Qt 构建同时不带桌面 GL，Phase 2 结束时核心单测在 iPad 模拟器里通过。迁移表为此加了第 8 步（tnzcore 与 colorfx 的 GL），第 12、17 步各加了 tnzext、toonzlib 的那一部分；离线的 GL 退路改成 `tnzglview` 注册的栅格化后端（第 5 节）。
- Q2 取两个 (a)：Qt 便利重载做成由 `NEXTTOONZ_QT_INTEROP` 打开的内联适配；核心只产生源文本加上下文，界面翻译，现有 .ts 不变。
- Q3 取 (a)：QtScript 绑定移出 toonzlib 成单独的可选库，不换引擎；换不换、换成什么到 Phase 3 合并 Qt6 时再定。
- Q4 取 (a)，**要跟进**：注入的键值存储，桌面实现仍用 QSettings、文件不变。跟进的是两件事：要不要在 (a) 之后继续收缩到 (c)（`Preferences` 整个搬到 toonzqt，核心只收显式传入的值）；iPad 壳把设置存在哪里、用什么格式。第 15 步做完时带着这两个问题回到所有者。
- Q5 取 (a)，**要跟进**：注入的文字与字体服务，桌面实现仍用 Qt，桌面像素不变。跟进的是：iPad 的 CoreText 实现与桌面的 Qt 文字必然画得不一样，同一个场景在两个平台上文字的像素不会一致，要不要、什么时候改成 Skia 文字让各平台一致（会改变桌面的文字像素，要像 Phase 1 那样出对照图）。第 18 步做完时带着这个问题回到所有者。
- Q6 取 (a)：桌面的编解码服务保留外部 ffmpeg 进程；LZO 直接链接。
- Q7 取 (b)：Phase 2 只要求 tcomposer、tconverter、tcleanupper 和应用的打开、保存、渲染入口经引擎 API；编辑命令只搬 iPad MVP 需要的那部分，其余放到 Phase 3。计划里 Phase 2 的工作项和完成判据已经照此修改。
- Q8 取 (a)：无 Qt 构建的 CI job 在下一次推送时加，只在 macOS x86_64 上；Linux 不加。因为 tnzcore 里不带 Qt 的部分第 1 步就能在 iPad 模拟器里跑了，同一个 job 从第 2 步起也跑 `run_ios_noqt.sh`。

下面保留原文，作为决定的依据。原文里的步号已经按加了第 8 步之后的编号改过。

下面每个问题都写了选项和本稿的倾向。

**Q1 桌面 OpenGL 在无 Qt 构建里怎么办。** Phase 1 之后 tnzcore、toonzlib、colorfx、tnzext 里还有交互 viewer 一侧的 GL 代码（画布文档 Q3、Q4 定的是留到 Phase 3）。macOS 有 GL，无 Qt 构建链接它没有问题；iPadOS 没有桌面 GL，带着它就编不过。选项：

- (a) Phase 2 的无 Qt 构建只在 macOS 上做，允许链接 GL；核心在 iPad 上编译等 Phase 3 把 viewer 移到画布之后。最省事，但 iPad 的第一个能跑的核心要等到 Phase 3 结束。
- (b) Phase 2 里，每迁移一个库，就把它的 viewer 一侧的 GL 入口移出核心库，放进一个界面一侧的新库（比如 `tnzglview`，toonzqt、tnztools、toonz 链接它），无 Qt 构建同时不带 GL；Phase 2 结束时核心单测在 iPad 模拟器里通过。多出来的工作主要是拆文件，粗估两到四周，没有细算。
- (c) 无 Qt 构建用源文件清单把带 GL 的文件排除出去，不移动；混在同一个文件里的 GL 代码用条件编译隔开。比 (b) 省事，但条件编译会留在代码里，到 Phase 3 再清。

倾向 (b)：所有者的第一个目标就是 x86_64 macOS 加 iPadOS，"核心在 iPad 模拟器里跑"是 Phase 2 最有意义的里程碑；这些 GL 入口在 Phase 3 本来就要离开核心，提前挪位置不算浪费。

**Q2 核心头里的 Qt 便利接口和翻译怎么处理。** 两件相关的事：

- 便利重载（`TFilePath(QString)` 之类）：(a) 第 6 节的内联适配，由界面一侧的宏打开；(b) 单独的 `*_qt.h` 适配头，界面机械地加 include；(c) 直接删掉，界面显式转换。倾向 (a)，波及最小，适配集中。
- 核心产生的、给用户看的文字（样式名、参数名、格式属性名）：(a) 核心只产生源文本加上下文，界面用 `QCoreApplication::translate` 翻译；核心里用一个与 `QT_TRANSLATE_NOOP` 同名的标记宏，lupdate 照样能提取，现有 .ts 文件和译文不变；iPad 以后自己决定怎么翻；(b) 核心带一个自己的翻译查表，读 .qm 文件；(c) 核心只给不翻译的英文。倾向 (a)。

**Q3 QtScript 绑定。** 计划写的是"Phase 2 变为可选编译"，脚本引擎的长期选择在 Phase 2 末。选项：(a) 绑定移出 toonzlib，成为单独的库，桌面默认编、无 Qt 构建不编，引擎仍是 QtScript，到 Phase 3 合并 Qt6 时再定换什么；(b) Phase 2 里就换掉 QtScript（QJSEngine、QuickJS 等）。倾向 (a)：换引擎会改变脚本的行为，和去 Qt 的"行为不变"目标冲突，应当单独做。

**Q4 Preferences。** 核心里有约 100 处读设置。选项：(a) 核心经一个注入的键值存储接口读写，桌面实现放在 toonzqt、仍用 QSettings，用户的 preferences.ini 一个字节都不变；tcomposer 与 iPad 各有自己的实现；(b) 核心自己读写 INI 文件，必须与 QSettings 的 INI 方言（转义、`@Variant`、编码）逐字兼容，风险高；(c) `Preferences` 整个移到 toonzqt，核心需要的值由调用方显式传入。倾向 (a)，并在第 15 步用改动前后的 preferences.ini 逐字节对照验收。(c) 更干净，但要改的调用点多，可以作为 (a) 之后的逐步收缩。

**Q5 文字、字体和 QImage。** 核心里用 QPainter 往栅格上写字（片头板、帧号）、用 QRawFont 取字形（字体关卡）、文字特效用 QFont。选项：(a) 注入的文字与字体服务，桌面实现继续用 Qt、放在界面一侧，桌面像素逐位不变；iPad 实现走 CoreText；(b) 改用 Skia 的文字（已经是依赖），各平台一致，但桌面的文字像素会变，要像 Phase 1 那样出对照图给所有者看。倾向 (a)：Phase 2 的原则是行为不变；(b) 可以以后作为外观改进单独做。

**Q6 ffmpeg 在桌面上的形态**（计划待定事项之一）。选项：(a) 编解码服务接口的桌面实现保留外部 ffmpeg 进程，行为不变；(b) 改为链接 libav。倾向 (a)：(b) 带来许可证（GPL/LGPL 组件）和打包的问题，而 iPad 反正要另写 VideoToolbox 的实现，接口定下来以后桌面随时可以换。LZO 不在此列，直接链接（`thirdparty/lzo` 已在树里）。

**Q7 Phase 2 在界面一侧做到哪里为止。** 计划的完成判据有"桌面 Qt UI 通过引擎 API 调用核心"，工作项有"把 GUI 里约 25k 行编辑命令搬进库"。按字面，这意味着 `TApp::instance()` 的 2,388 处调用都要经过引擎 API，工作量很可能超过 Phase 2 的其余部分之和。选项：(a) 照计划全做；(b) Phase 2 只要求 tcomposer、tconverter、tcleanupper 和应用的"打开、保存、渲染"入口经引擎 API，命令只搬 iPad MVP 需要的那部分（cell、column、level 的基本编辑），其余放到 Phase 3 和 viewer 上画布一起做；(c) Phase 2 不搬命令。倾向 (b)，并相应修改计划里 Phase 2 的完成判据。

**Q8 无 Qt 构建的 CI job 什么时候加、加在哪。** 计划写"在 Linux 与 macOS 上编译并跑单测"。选项：(a) 下一次推送时在 macOS x86_64 的 workflow 里加一个 job（配置 `WITH_QT=OFF`，几十个编译单元，一两分钟）；Q1 选 (b) 时再加 iPad 模拟器；Linux 不加；(b) Linux 和 macOS 都加；(c) 到 Phase 2 末尾再加。倾向 (a)：符合"先做 x86_64 macOS 和 iPadOS"的平台重点；Linux 的全量构建和清洁剂 job 已经覆盖了 Linux 上的编译，无 Qt 构建在 Linux 上本机虚拟机里测过（第 8 节）。

---

## 11. 风险与未验证事项

- **应用本身没有自动测试。** golden 和单测覆盖的是 tcomposer 和库；线程、Handle、Preferences 这几步改的是应用每天都走的路径，只能靠人工验收。第 9、10、15、16、17 步在合并前都要所有者在应用里试用。
- **std 与 Qt 的细微语义差别。** 字符串：无效 UTF-8 的替换字符、大小写不敏感比较（QString 用 Unicode 大小写折叠）。路径：`QDir::cleanPath` 的规则、大小写不敏感的文件系统。时间：`QDateTime` 的时区与夏令时。线程：`QThreadStorage` 在线程退出时析构，`thread_local` 的析构顺序不同；线程优先级（Qt 在 macOS 上设 QoS）。容器：`QHash` 的遍历顺序。这些都要靠差分测试逐一钉住，不能靠读代码。
- **头文件改动的波及。** 第 1 步一个互斥锁改了 68 个源文件，其中界面 27 个；而且第一次只在 macOS 上编译，Linux 上又查出 14 个文件要改（一处只在 Linux 编译的代码，13 处间接包含），每一步都必须在 Linux 虚拟机里全量编译一次。字符串和路径那几步的波及会大一个量级；第 6 节的适配能减少，但减不到零。界面文件只在本机和 CI 的全量构建里编译，Windows 只在 master 上跑 CI。
- **Windows 是盲改的。** 第 1 步改了三个只在 Windows 上编译的文件（avi、mov、3gp 的读写器），本机和 Linux 都编不到。D5 里 Windows 的产品地位待定，CI 的 Windows workflow 只在 master 上跑，这些文件可能长期没人编译。
- **OpenGL 退路在多线程下不确定，还会崩溃**（已知问题第 26 条，第 1 步验证时发现）。用 `NEXTTOONZ_VECTOR_RASTERIZER=gl` 加 `-nthreads 8` 渲染，同一个构建两次的结果不同，`vector_cmapped_fx` 间歇段错误；改动前的构建也一样。默认的 Skia 路径在 8 线程下确定。它的后果是：碰线程的步骤在 GL 退路上只能做单线程对照。
- **`TRenderer` 的等待方式**（第 5 节"线程"）：桌面靠"等的时候处理事件"保持响应。新的派发器接口要保留这个能力，否则界面会在渲染时冻结；只有人工能验证。
- **arm64 没有跑过。** 第 1 步在 x86_64 macOS、x86_64 Linux 和 x86_64 的 iPad 模拟器上测过；arm64 只编译链接过（iPad device 模式）。
- **CI 没有跑过第 1 步。** 分支没有推送。macOS arm64、Ubuntu gcc、Fedora 三个 job 本机没有对应的环境（Linux 虚拟机是 Ubuntu 24.04 的 clang 清洁剂构建）。
- **无 Qt 构建仍然要求全部第三方库。** 它走同一个顶层配置，OpenCV、TIFF、Boost、SuperLU 等都要找到，`superlu41` 也会编译，虽然第 1 步的那 10 个文件一个都不用。iPad 构建时第三方库的来源是另一回事（Phase 1 的 Skia 用的是预编译包），本稿没有设计。
- **要跟进的两个决定。** Q4（设置存储收缩到哪一步、iPad 存在哪）和 Q5（各平台文字像素不一致、要不要改用 Skia 文字）是按"行为不变"先做的，所有者要求跟进，分别在第 15 步和第 18 步做完时回到所有者；计划的待定事项表里各有一行。
- **GL 退路搬家以后的验证。** 第 8 步之后，离线渲染的 GL 退路要由 tcomposer 链接 `tnzglview` 才有；Phase 1 以来用它证明"OpenGL 路径逐位不变"的那套对照（画布文档 8.1 节）从此只在链接了它的程序里能做。
- **工作量。** 计划给 Phase 2 估的是 6 到 9 人月；第 7 节的 21 步里，第 9、10、16、17、21 步各自可能就是几周。Q7 的选择对总量影响最大。

---

## 12. 附录：泄漏 Qt 的公共头

第 1 步之后，八个核心库的编译单元里经各个头碰到 Qt 的数量（一个编译单元可以经多个头碰到）。"直接包含"是编译单元自己包含 Qt 的头。

| 头 | 编译单元 | 头 | 编译单元 |
|---|---|---|---|
| `tfilepath.h` | 419 | `toonz/tstageobject.h` | 25 |
| `tconvert.h` | 301 | `toonz/scriptbinding.h` | 23 |
| `timagecache.h` | 225 | `toonz/sceneproperties.h` | 16 |
| `tundo.h` | 208 | `toonz/tcenterlinevectorizer.h` | 16 |
| `tnotanimatableparam.h` | 195 | `tmsgcore.h` | 13 |
| `trasterfx.h` | 193 | `orientation.h` | 13 |
| `tstream.h` | 181 | `tcacheresource.h` | 11 |
| `tcolorstyles.h` | 154 | `toonz/txshsoundlevel.h` | 11 |
| 直接包含 | 132 | `tcontenthistory.h` | 9 |
| `toonz/txshlevel.h` | 85 | `toonz/txshsoundcolumn.h` | 9 |
| `tsystem.h` | 84 | `tthread.h` | 8 |
| `timage_io.h` | 67 | `toonz/imagestyles.h` | 8 |
| `toonz/imagemanager.h` | 58 | `toonz/txsheethandle.h` | 8 |
| `toonz/txshsimplelevel.h` | 57 | `tsound_io.h` | 7 |
| `toonz/txshcolumn.h` | 48 | `tipc.h` | 7 |
| `toonz/preferences.h` | 40 | `toonz/tscenehandle.h` | 7 |
| `tfxattributes.h` | 39 | `toonz/scriptbinding_files.h` | 7 |
| `ext/plasticskeleton.h` | 28 | `image/ffmpeg/tiio_ffmpeg.h` | 7 |
| `ext/plasticskeletondeformation.h` | 28 | `toonz/ttileset.h` | 6 |
| `tsimplecolorstyles.h` | 27 | `tiio.h` | 26 |

第 1 步之前排第一的 `tthreadmessage.h`（470）和 `tpalette.h`（140）、`tstroke.h`（76）、`tgldisplaylistsmanager.h`（13）已经不在表里：它们自己不再包含 Qt，包含它们的编译单元仍可能经别的头碰到 Qt。一共有 122 个头在第 1 步之前是泄漏者。
