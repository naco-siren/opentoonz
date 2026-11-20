# NextToonz 核心去 Qt 设计草案

版本 0.7，2026-11-18。本文是 `doc/nexttoonz_plan.md` Phase 2（核心去 Qt 与抽出）的设计草案：先实测核心库现在怎样依赖 Qt，再定目标状态、每项 Qt 设施的替代、一张每步都能单独合并的迁移表、每步怎样验证、引擎 API（D3）的第一版和出现时机，最后是需要所有者决定的问题。做法沿用 Phase 1（`doc/nexttoonz_canvas_ir.md`）：先有设计，再一步一步做，每一步都证明默认路径没有变，做完一步就把"实际做成什么样"补回本文。

迁移第 1 步（递归互斥锁与无 Qt 构建的骨架）、第 2 步（无 Qt 构建的 CI job）、第 3 步（字符串转换）、第 4 步（TFilePath 与 TFrameId）、第 5 步（流、图像缓存与图像读写框架的头）第 6a 步（文件状态、文件操作与目录列表）、第 6b 步（`SystemVar.ini` 经键值存储）、第 6c 步（应用与进程信息，`tenv` 进无 Qt 构建）、第 7 步（样式的界面文字由界面翻译）、第 8a 步（矢量图和样式里不带 GL 的 Qt）、第 8b 步（核心对 GL 的每处使用改经三种接缝）、第 8c 步（桌面 GL 搬进 tnzglview）、第 8d-0 步（矢量图依赖里剩下的 Qt）和第 8d 步（矢量图、样式、colorfx 进无 Qt 构建和 iPad 模拟器）已经实现（第 7 节，做成的样子在 8.1 到 8.15 节），在本地分支 `wip/phase2-noqt` 上，没有合并、没有推送；第 2 步的 CI job 因此还没有在 GitHub 上跑过。其余都是设计。所有者 2026-11-18 审过第 10 节的 Q1 到 Q8，都按本稿的倾向定了，其中 Q4、Q5 标为要跟进（第 10 节开头）。

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
| `tsystem`、`tenv`、`tfilepath_io` 的 QDir、QFile、QFileInfo、QDateTime、QStandardPaths | 直接调系统：POSIX（`lstat`、`stat`、`access`、`readdir`、`mkdir`、`renameatx_np`/`renameat2`……）加 Apple 的 CoreFoundation（名字的规范化、隐藏标志、替身）和 `clonefile`；Windows 暂时仍是 Qt。标准目录按平台取（Apple 上用 Foundation）。文件时间暂时仍是 Qt 一侧的 `QDateTime` 成员，第一个不带 Qt 的调用者出现时再加 `std::chrono` 的版本 | 原稿写的是 `std::filesystem`、`std::chrono`；第 6a 步发现 `std::filesystem` 表达不了 Qt 的规则（没有 `access(2)`、没有 `readdir` 的类型、`copy_file` 不克隆、`rename` 会覆盖、取不到 Apple 的标志），而这一阶段的验收标准是逐字节相同，所以改用系统调用（决定见第 10 节，做法见 8.6 节）；这些调用在 iPadOS 上都有。`TSystem` 的接口返回 QString、QDateTime，界面大量使用，接口改造见第 6 节；iPad 的安全作用域文件访问要求文件系统能被注入，留接口 |
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

**第 4 步时的补充（2026-11-18）。** 上面的宏只适合自由函数。成员函数（`TFilePath::getQString()`、`TFilePath(const QString &)`、`TFrameId::getLetter()`……）不能按宏有无出现或消失：同一个类在不同编译单元里的定义就不一样了，这违反 ODR。原来的打算是把它们改成非成员函数、界面机械替换，但仅 `getQString()` 就有约 310 处调用。第 4 步改用另一种做法：成员照样在核心头里**声明**，用前置声明的 `class QString; class QChar;`（`tfiletype.h` 早就这样做了），**定义**放进一个只在 Qt 构建里编译的源文件（tnzcore 的 `tfilepath_qt.cpp`）。类的定义在所有编译单元里都一样；不带 Qt 的代码看不到完整的 QString，调用这些成员就编译失败；界面和还没迁移的库原样调用，一处都不用改。宏 `NEXTTOONZ_QT_INTEROP` 仍用于自由函数（第 3 步的 `fromStr`）。这是对 Q2 (a) 的落实方式的改动，没有改变 Q2 的目的（界面的改动最小、适配集中）；它带来一个到第 20 步才要处理的问题，见第 11 节。2026-11-18 主会话受所有者委托认可了这个做法，以后同类接口都照此做（第 10 节开头）。

---

## 7. 迁移顺序

每一步都在默认构建里 golden 和单测不变的前提下单独合并；"进入无 Qt 构建"一栏是这一步之后可以加进 `<库>_NOQT_SOURCES` 的源文件（按 2.1 节的方法模拟，做的时候以实际链接为准）。验收的做法见第 8 节，下面只写每步特有的部分。

| 步 | 内容 | 进入无 Qt 构建 | 验收 |
|---|---|---|---|
| 0（本稿） | 实测清单与设计 | | |
| 1（已完成） | `TThread::Mutex`/`MutexLocker` 换成 std；另外六处 `QMutex::Recursive`；`tpalette.h`、`tstroke.h`、`tgldisplaylistsmanager.h` 去掉 `<QMutex>`；`WITH_QT` 选项与无 Qt 构建的骨架 | tnzcore 10 个：tgeometry、tcolor（5）、trandom、tstopwatch、tdebugmessage、tsmartpointer | 8.1 节 |
| 2（已完成，未推送） | 无 Qt 构建进 CI（Q8）：macOS x86_64 上一个不装 Qt 的 job，构建、检查没有任何东西碰到 Qt、跑单测，再在 iPad 模拟器里跑核心单测 | | 8.2 节；推送后 CI 的新 job 绿，要所有者同意推送 |
| 3（已完成） | 字符串转换：`tstring.cpp` 不用 QString，`tconvert.h` 的 QString 重载改成第 6 节的适配 | tnzcore 8 个：`tstring`、`tmathutil`、`texception`、`tcurves`、`tcurveutil`、`tstrokeoutline`、`cornerdetector`、`tsound`（`tstroke` 要等第 8 步，见 8.3 节） | 8.3 节 |
| 4（已完成） | `TFilePath`、`TFrameId` 不用 QString、QChar、QRegularExpression；带 Qt 类型的成员留给 Qt 一侧（8.4 节） | `tfilepath.cpp`、`tfiletype.cpp` | 8.4 节 |
| 5（已完成） | `tstream.h`、`timagecache.h`、`timage_io.h`、`tiio.h`、`tsound_io.h` 的 QString、QStringList（8.5 节） | `tsound_io.cpp`、`tdata.cpp`；栅格和 trop 要等第 6 步（`TBigMemoryManager`）和第 9 步（`TImageCache` 的 `QThreadStorage`） | 8.5 节 |
| 6（拆成 6a、6b、6c，见第 10 节；都已完成，8.6 到 8.8 节） | `tsystem`、`tenv`、`tfilepath_io`：文件系统与时间换成 std；`TSystem` 接口里的 QString、QDateTime 改适配；`tenv` 的 QSettings 接到第 15 步的键值存储前先用一个过渡实现 | `tsystem*`、`tenv`、`tbigmemorymanager`、`tlogger`… | `TSystem` 的单测（新写：列目录、时间戳、复制、删除到临时目录）；`stuff` 目录的初始化在新旧构建里产生相同的文件树 |
| 7（已完成，8.9 节） | 样式描述与翻译：`tcolorstyles.h` 的 `getDescription`、参数名、枚举项改成核心产生 `TTranslatableText`、界面翻译（Q2）；`TPalette` 第一页名字经窄的数据翻译服务；`tvectorimage.h` 的 `QSet` | `ttranslatabletext.cpp`；矢量图和样式的 16 个源文件从此编译时碰不到 Qt 的头，但链接要等第 8 步 | 每个注册样式的描述、参数名、枚举项在 11 种语言下经 Qt 翻译后与改动前逐字相同（导出对照）；lupdate 前后产出的 .ts 相同 |
| 8（已完成，8.10 到 8.15 节） | 开头先清矢量图和样式里不带 GL 的 Qt（第 7 步之后剩下的）：`tpalette.cpp` 的帧号区间解析与 `QString::number(…, 'g', 17)`、`toDouble`，`tsimplecolorstyles.cpp` 的 `QDir::match`、`sourceFilters`、`QMutexLocker`，`tl2lautocloser.cpp`、`tsegmentadjuster.cpp` 的 `qDebug`；然后 tnzcore 与 colorfx 的桌面 GL 移出核心（Q1）：`tgl.cpp`、`tstencilcontrol`、`tgldisplaylistsmanager`、`tofflinegl`/`qtofflinegl`、`tglcanvasbridge`、`tglregions` 的 GL 画法、`drawutil` 的 `drawStrokeCenterline`、样式的 GL 入口层，放进界面一侧的新库 `tnzglview`（toonzlib、tnzext、tnzstdfx、toonzqt、tnztools、toonz 和要画 GL 的几个程序链接它，也由它链接 `tnzglcanvas`；8c 的做成的样子有列表）；离线的 GL 退路改成 `tnzglview` 注册的栅格化后端 | tnzcore 的矢量与样式部分；colorfx 全部；tnzcore 不再链接 GL；核心单测加进 iPad 模拟器（`run_ios_noqt.sh`）；矢量图和样式（第 7 步留下的 16 个源文件和 `tpalette`、`tsimplecolorstyles`） | 默认 Skia 路径逐字节相同；GL 退路单线程逐字节相同（tcomposer 链接 `tnzglview`）；viewer 的绘制只能人工验收；`.tpl` 里的数字与 Qt 逐字节相同（大量随机 double 和边界值的差分测试），帧号区间解析与旧代码差分对照，`QDir::match` 的通配规则照 Qt 5.15 对照 |
| 9 | 线程：`TThread::Executor`/`Runnable` 换成线程池，`TThread::Message` 换成派发器，`QThreadStorage`、`QReadWriteLock`、`QWaitCondition` 换成 std（8d-0 已做一部分：`timagecache.cpp` 的 `QThreadStorage` 和互斥锁）；toonzqt 加 Runnable 的 Qt 适配和 Qt 派发器 | `tthread.cpp`、`threadmessage.cpp`、`timagecache.cpp`… | golden 用 `-nthreads 1` 和 `-nthreads 8` 各渲一遍，与改动前逐字节相同；线程池的单测在 ThreadSanitizer 下跑；应用里人工验收（预览、渲染进度、取消） |
| 10 | tnzbase：`TRenderer` 去掉自旋与 `qGuiApp`，`TRenderSettings` 的离屏表面改成不透明句柄；`tundo.h` 的 `TUndoManager` 去 QObject；`TFontParam`；`tfxattributes`；`TCacheResource` 的 QRegion；`tcacheresourcepool` 的 QSettings、QDir | tnzbase 全部（扫描仪除外）；stdfx 的大部分（按模拟 162 个） | 同第 9 步的多线程 golden；预测缓存与被动缓存的单测（新写）；`.tnz` 里字体参数的读写不变 |
| 11 | 引擎 API 第一版（第 9 节），tcomposer 改用它 | | tcomposer 的全部 golden 与改动前逐字节相同 |
| 12 | tnzext：`plasticskeleton` 的骨骼名、`ttexturesstorage` 的 QCache 与 QString 键、`meshtexturizer` 的 QReadWriteLock；它们的 GL 部分移到 `tnzglview`（Q1） | tnzext 全部 | `plastic_basic` 的 golden；plastic 场景的读写 round-trip |
| 13 | 外部进程：LZO 直接链接（8d-0 已做一部分：LZO 经注入的服务，桌面默认仍是外部进程，这一步接上链接库的实现；image 库的 pli 读写器在 8d 本体里已进无 Qt 构建）；ffmpeg 换成编解码服务接口（桌面实现仍是外部进程）；删除 `t32bitsrv`、mov/3gp 代理、QuickTime、`tipc`；`TMsgCore` 移到应用一侧；`tsound_qt` 换成声音输出服务 | sound 全部；image 的格式框架 | tlv、tzl 写出的字节与改动前相同（新写的 round-trip 测试）；含 tlv 的 golden；D10 第 2 条在核心里满足 |
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

### 8.4 第 4 步实际做成的样子（2026-11-18）

**代码。** `TFrameId` 的字母从 QString 换成 `std::wstring`。`tfilepath.cpp` 不再用 QString、QChar、QRegularExpression，`tfiletype.cpp` 不再用 QString。带 Qt 类型的成员按第 6 节的补充留在头里、定义在新文件 `common/tsystem/tfilepath_qt.cpp`：`TFilePath(const QString &)`、`getQString()`、`getSepChar()`（`QChar`）、`fidRegExpStr()`、`TFrameId(int, QString, ...)`、`getLetter()`（`QString`），以及 `TFileType::getInfoFromExtension(const QString &)`。核心要用的地方有了不带 Qt 的对应：`TFrameId(int, const std::wstring &, ...)`、`getLetterW()`、`getSepCharW()`。`TFilePathInfo`（`analyzePath()` 的结果）的成员改成 `std::wstring` 和 `wchar_t`；它只在 `tfilepath.cpp` 和 Windows 专用的 `toonzpreview` 里用（后者只取 `fId`，不受影响）。

要逐字保留的 Qt 行为，先用探针程序量过：

- **项目设置下的文件名解析**（`analyzePath()`，`m_useStandard` 为假时才用）原来是三个 QRegularExpression。改成手写的匹配，按 PCRE 的回溯顺序依次试各种切分（贪婪，先成功的为准），捕获的内容与原来相同；三个模式写在 `tfilepath.cpp` 的注释里。细节：`\d` 只认 ASCII 数字；`$` 也匹配结尾换行符之前的位置；帧号溢出 `int` 时 `QString::toInt()` 给 0；文件名先经过 `QString::fromStdWString` 的那些处理（开头的 BOM 丢掉、不成对的代理变成 U+FFFD），而不是合法 UTF-16 的文件名 QRegularExpression 一个都不匹配，落到"没有句点"的那一支。
- **小写**：原来的 `QString::toLower()` 按 Unicode 规则转小写（`std::toLower` 只认 ASCII），例如开尔文符号 K（U+212A）变成 ASCII 的 `k`，于是会被当成已注册的扩展名。新增的 `toLowerAsQString()` 用一张从 Qt 5.15.19 生成的 1,393 项映射表（`common/tcore/tlowercasetable.inc`，生成程序 `tests/core/legacy/gen_lowercase_table.cpp`），加上 U+0130 变成两个字符的特例；Qt 的小写不看上下文（没有词尾 σ 的规则），表就够了。
- **帧字母的顺序**：`QString::localeAwareCompare`。Homebrew 的 Qt 不带 ICU，在 macOS 上是"空串按码元比较，否则先规范成 NFC 再 `CFStringCompare(kCFCompareLocalized)`"；新代码在 Apple 平台上做同样的调用（tnzcore 因此在无 Qt 构建里链接 CoreFoundation，iPadOS 上也有）。Apple 以外的 Qt 构建仍然问 Qt；无 Qt 构建用 `wcscoll` 临时顶着，见 Q9。
- `qWarning` 换成 `fprintf(stderr, ...)`（第 11 节）。

**进入无 Qt 构建的。** `tfilepath.cpp` 和 `tfiletype.cpp`，`TNZCORE_NOQT_SOURCES` 从 18 个变成 20 个；原有的 `test_tfilepath.cpp` 不需要别的，也进了无 Qt 的测试清单。`tfilepath.h` 不再包含 `<QString>` 之后，本机只有 3 个文件原来是经它拿到 QString 的（`tfilepath_io.cpp`、`tdoubleparamfile.cpp`、`tcli.h`），补了包含。

**新测试。** `tests/core/test_tfilepath_qt.cpp`（只在 Qt 构建里编）把改动前的 `tfilepath.h`、`tfilepath.cpp` 原样放在 `tests/core/legacy/`（类名加 `Legacy` 前缀，去掉导出宏），新旧两个类吃同样的路径，比较它们对每条路径说的每一件事：名字、各种类型、帧（号、字母、补零、分隔符、四种展开）、`getDots`、分隔符、是不是关卡名、是不是 ffmpeg 类型，以及 `withType`、`withName`（含宽字符和 BOM）、`withFrame`（七种帧和格式）、`withNoFrame`、`getParentDir` 的结果，异常也算一种结果。语料是 9,000 个由有意义的片段拼成的文件名（关卡名、分隔符、帧号与字母、大小写不同和带开尔文符号、U+0130、结尾换行的扩展名）和随机串（混入孤立代理、表情符号、BOM、禁用字符），放在几个不同的目录下，在路径设置的 28 种组合下比较（标准与项目设置、非字母后缀、字母个数 0/1/3、无分隔符格式、下划线格式）。另外比较了从字符串解析帧号、30 个字母两两的先后与相等（大小写、重音、组合与分解形式、ß、开尔文符号、表情符号）、`fidRegExpStr()`。旧代码每次调用都重新编译正则，项目设置下每条路径要约 300 微秒，所以默认只取语料的一部分（约 30 秒）；`NEXTTOONZ_FULL_DIFFERENTIAL=1` 跑全部。`test_tfilepath.cpp` 加了两个不需要 Qt 的测试：项目设置下 13 个文件名的解析结果（期望值取自旧代码），以及字母的顺序。

**结果（x86_64 macOS）。**

- 差分测试：默认规模 6 个全部通过（约 30 秒）。全规模（`NEXTTOONZ_FULL_DIFFERENTIAL=1`）6 个也全部通过，约 16 分钟：9,000 个生成的文件名在两个目录下、5 组主要设置下完整比较，在 28 组设置下比较名字、帧和类型；4,000 个随机名在三个目录下完整比较，8,000 个在 28 组设置下比较。
- 速度：新代码在标准设置下每条路径约 6.1 微秒（旧 7.2），项目设置下约 6.7 微秒（旧 286，因为不再每次编译正则）。
- 默认构建的编译命令：库的编译单元一个都没变，多了 `tfilepath_qt.cpp` 和两个测试文件，测试程序的编译单元多了一个包含目录。
- 全部测试：456 个通过。
- golden 266 帧、28 个场景单线程和 8 线程的 554 帧、OpenGL 退路单线程 277 帧，都与 Phase 2 之前的构建当天渲的逐字节相同（参考工程用默认的标准设置，所以渲染没有走到 `analyzePath()`；那一支靠差分测试）。
- 无 Qt 构建：三道检查通过，133 个测试通过，iPad 模拟器里 45 个通过。

**结果（Linux 虚拟机）。** 这一步 Linux 上没有查出缺包含的文件，全部目标编译通过。清洁剂构建里差分测试先失败了：旧的和新的 `parseFrame` 在十位以上的帧号上都会让 `int` 溢出，UBSan 遇到就停（这是原有的问题，记为已知问题第 27 条，第 4 步原样保留）。把十位以上的帧号只留给项目设置下的比较（那条路照 `QString::toInt()` 溢出时给 0，没有未定义行为）以后：430 个测试里 429 个通过，失败的仍是虚拟机里一直失败的 `golden_fx_sweep_font_a`；差分测试在 Linux 的 Qt 5.15.13 上也全部通过（那里帧字母的顺序仍由 Qt 的 ICU 决定）；GL 画布 32 个、无 Qt 构建 135 个通过。

### 8.5 第 5 步实际做成的样子（2026-11-18）

**代码。** 五个头都改成第 6 节补充里的做法：不再包含 `<QString>`、`<QStringList>`，前置声明 `class QString; class QStringList;`，带 Qt 类型的成员照样声明，定义放进只在 Qt 构建里编译的新文件 `tstream_qt.cpp`、`timage_io_qt.cpp`（含 `tiio.h` 那一个）、`tsound_io_qt.cpp`；`timagecache.h` 的三个 QString 重载本来就定义在 `timagecache.cpp` 里，那个文件还有别的 Qt（`QThreadStorage`），留在原处。核心要用的地方有了 std 的对应：

- `TImageReader`、`TImageWriter`、`TSoundTrackReader`、`TSoundTrackWriter` 的 `define` 多了 `const std::string &` 和 `const char *` 两个重载（后者是为了让 `define("png", ...)` 这种字面量调用不在 QString 和 std::string 之间产生二义），`getSupportedFormats` 多了填 `std::vector<std::string>` 的版本；`Tiio::Writer::getSupportedFormats` 同样。注册表的键从 QString 换成 std::string：键都是 ASCII 的扩展名，`std::map` 的顺序与 QString 的顺序相同。QString 版本转成 std 版本再调用。
- 声音读写器原来用 `QString::fromStdString(toLower(getType()))` 查表，现在直接用 `toLower(getType())`。
- `TImageReader` 里"Old 4.1 Palette"那处异常消息的比较原来经过 QString，改成比较 `roundTripAsQString` 之后的宽字符串。
- `TOStream::operator<<(QString)` 原来和 `operator<<(std::string)` 一行一行相同，只多一次 `toStdString()`，现在就是这样转发。`TIStream::operator>>(QString &)` 原来和 `operator>>(std::string &)` 读同样的字节，区别只在每个字节怎样加进 QString。我先以为那是 Latin-1，前后对照证明不是：`QString::append(char)` 走的是 `QChar` 的整数构造函数，0x80 以上的字节变成 0xFF00 加该字节（`\xC3` 读回成 U+FFC3）。新代码照这个做，记为已知问题第 28 条：非 ASCII 的 QString 存进流读不回来。

`tstream.h`、`timagecache.h` 原来用 `#ifndef TNZCORE_LIGHT` 包着 Qt 的部分，这个宏没有人定义，一并去掉。去掉包含之后本机有 3 个文件原来是经这几个头拿到 Qt 类的：`psd.cpp`（QString、QStringList）、`tdoublekeyframe.cpp`、`tvectorimage.h`（`QSet`），补了包含；`tvectorimage.h` 本来就在接口里用 `QSet`，是第 7 步要处理的泄漏者。

**进入无 Qt 构建的。** 按包含树，核心库里碰到 Qt 的编译单元从第 3 步之后的 503 降到 481（toonzlib 从 178 降到 164，tnzbase 从 43 降到 39，tnzcore 降到 78）；tnzcore 里 `traster.cpp`、`trasterimage.cpp`、trop 的大部分、`tsound_io.cpp`、`tmeshimage.cpp` 都不再碰 Qt。但链接闭合的只有 `tsound_io.cpp` 和 `tdata.cpp`，`TNZCORE_NOQT_SOURCES` 从 20 个变成 22 个：`traster.cpp` 要 `TBigMemoryManager`（`tbigmemorymanager.cpp`，经 `tsystem.h` 碰到 Qt，第 6 步），trop 要 `TImageCache`（`timagecache.cpp` 用 `QThreadStorage`，第 9 步），`TIStream` 的实现 `tstream.cpp` 也经 `tsystem.h` 碰到 Qt（第 6 步）。所以栅格要到第 6 步之后、栅格运算要到第 9 步之后才能进无 Qt 构建；迁移表的顺序不用改，但这两步之后无 Qt 构建会一下子大很多。

**新测试。** `test_tsound_io.cpp`（3 个，不需要 Qt）：注册、按序列出、未知类型抛异常。`test_tstream.cpp` 加了一个，固化 QString 写成 UTF-8、读回按带符号字节的行为。

**前后对照。** 这一步的头没有改任何类的布局，符号也都还在，所以同一个对照程序（`toonz/build-phase1/ab/phase2/qtadapters/`，不进版本库）可以原样分别链接改动前（Phase 2 之前的应用包里的 dylib）和改动后的 tnzcore：用 QString 写 2,016 个字符串（含引号、反斜杠、NUL、换行、Latin-1、CJK、表情符号和随机串）再读回，并列出四个注册表、查两个未注册的类型。写出的文件逐字节相同，读回的每个 QString 逐码元相同，注册表的列表和查找结果相同。正是这个对照查出了上面那个 0xFF00 的问题，第一版实现在这里有 1,394 行不同。

**结果（x86_64 macOS）。**

- 默认构建的编译命令：已有的编译单元一个都没变，多了三个 `_qt.cpp` 和 `test_tsound_io.cpp`。
- 全部测试：459 个通过。
- golden 266 帧、28 个场景单线程和 8 线程的 554 帧、OpenGL 退路单线程 277 帧，都与 Phase 2 之前的构建同一天渲的逐字节相同。
- 无 Qt 构建：三道检查通过，137 个测试通过，iPad 模拟器里 49 个通过。

**结果（Linux 虚拟机）。** 又是那一类问题：这一步之后不再包含任何 Qt 头的编译单元，在 Linux 上失去了 Qt 头间接带来的 `<cstring>`。按 8.3 节的经验先把这批文件挑出来查了一遍（`traster.cpp`、`trop.cpp`、`EraseContour.cpp`），其余的由虚拟机的构建逐轮查出（`tfx.h`、`STPic.h`、toonzqt 的 `plugin_tile_interface.cpp`：它们是头文件，或者经头文件拿到的，单看编译单元列表查不到），一共 6 个文件补了 `#include <cstring>`。之后：全部目标编译通过；433 个测试里 432 个通过，失败的仍是 `golden_fx_sweep_font_a`；GL 画布 32 个、无 Qt 构建 137 个通过。

### 8.6 第 6a 步实际做成的样子（2026-11-18）

**代码。** `TFileStatus` 不再包着 `QFileInfo`：构造时问一次系统，存下存在与否、是不是文件夹、是不是链接、大小和权限。权限是核心自己的 `TFileStatus::Permission` 标志（数值与 `QFileDevice::Permission` 相同），`getPermissions()` 返回 `int`；用到它的只有头里内联的 `isWritable`、`isReadable`，一起改了，头外没有调用点。给出 Qt 类型的五个成员（三个 `QDateTime` 时间、属主和属组的 `QString`）照第 6 节补充的做法在头里声明、定义在新文件 `tsystem_qt.cpp`，调用时现场构造 `QFileInfo` 去问，与原来一样（原来的 `QFileInfo` 也是第一次问时才取时间）。`readDirectory_DirItems` 多了填 `std::vector<std::wstring>` 的版本，`QStringList` 版本转调它；`getUniqueFile` 的默认参数 `QString field = ""` 拆成两个重载。`tsystem.h` 不再包含 `<QDateTime>`、`<QFileInfo>`，`#ifndef TNZCORE_LIGHT` 一并去掉（没有人定义这个宏）。`tsystem.h` 里原来声明了一个 `operator<<(std::ostream &, const QDateTime &)`，前后都没有定义，原样留着。

**用的是 POSIX 调用，不是 `std::filesystem`。** 第 10 节的决定写的是"换成 `std::filesystem`、`std::chrono`"。做的时候发现 `std::filesystem` 表达不了 Qt 的几条规则，要逐字节相同只能直接调系统：它没有 `access(2)`（Qt 的"当前用户可读写执行"三个位来自它），没有 `readdir` 的 `d_type`（下面第二条），`copy_file` 不会用 `clonefile(2)`，`rename` 会覆盖已有文件，也取不到 Apple 的隐藏标志和替身。所以非 Windows 的实现用的是 `lstat`/`stat`/`access`/`opendir`/`readdir`/`mkdir`/`rmdir`/`unlink`/`rename` 加上 Apple 的 CoreFoundation。文件时间也还是经 `QFileInfo` 给 `QDateTime`：核心里还没有不带 Qt 的调用者，等第一个出现（大概是第 8 步或 toonzlib 的某一步）时再加 `std::chrono` 的版本。这两处与决定的字面不同，已报告主会话。

逐条照 Qt 5.15 做的规则（`tests/core/test_tsystem_qt.cpp` 一条条对照过）：

- **路径的字节。** Qt 交给系统的是 `QFile::encodeName`，读回来经 `QFile::decodeName`。在 Apple 平台上前者先分解成 NFD 再转 UTF-8，后者解 UTF-8 后合成 NFC（用本机的 Qt 5.15.19 验证过：`café` 写成 `cafe` 加 U+0301）。所以新建文件、文件夹的名字在磁盘上是 NFD，列目录读回的名字是 NFC，新代码照做，用 CoreFoundation 做规范化（纯 ASCII 的名字直接跳过）。路径里有 NUL 时 Qt 拒绝，新代码同样。Linux 上是 UTF-8 本身。
- **列目录。** 照 `QDirIterator` 的 `matchesFilters`：先看 `.`、`..`，再看隐藏，再排除"系统文件"（既不是文件也不是文件夹也不是链接的，和断了的链接），再看文件夹、文件、可读。条目的类型先取 `readdir` 给的 `d_type`（Qt 的 `fillFromDirEnt`），不问 `stat`；但在 Apple 平台上判断隐藏要问 `stat`（隐藏标志在 `st_flags` 里），问不到时（文件夹可读不可进）Qt 把 `d_type` 给的类型也忘了，这个条目就不算文件，被排除。只有过滤器带 `QDir::Hidden` 时才看得到这样的条目，差分测试正是在这里第一次不同。Apple 以外，名字不是合法 UTF-8 的条目 Qt 直接跳过（`QFileSystemIterator` 用本地编码解不出来的不要），新代码同样（Linux 虚拟机里的差分测试查出来的）。隐藏：名字以点开头，或在 Apple 平台上 `kCFURLIsHiddenKey` 为真（`chflags hidden`）。链接：符号链接，或在 Apple 平台上 Finder 替身（`kCFURLIsAliasFileKey`）。
- **排序。** `QDir::Name | QDir::IgnoreCase`（`entryInfoList()` 的默认）是两个名字各自 `QString::toLower` 之后按 UTF-16 码元比较，`QDir::Name | QDir::LocaleAware`（`readDirectory_DirItems`）是 `QString::localeAwareCompare`，也就是帧字母的顺序（第 4 步的 `TFrameId::compareLetters`，Linux 的 Qt 构建里仍然问 Qt）。Qt 用 `std::sort` 排 `readdir` 顺序的条目，新代码同样，所以比较相等的条目先后也一样。只有 `readDirectoryTree` 的结果依赖这个顺序（文件夹按它入列）；其余的列表最后都进了 `CaselessFilepathLess` 的 set。
- **`QFile::copy`。** 目标存在就失败；在 Apple 平台上先试 `clonefile(2)`，成功就结束：副本保留源文件的修改时间，源是文件夹时整个文件夹被克隆（`TSystem::copyFile` 拿文件夹当源也"成功"，差分测试里有这一条）；不成功才打开源文件（文件夹打不开）、在目标的文件夹里建 `qt_temp.XXXXXX`、4096 字节一块地复制、核对长度、不覆盖地改名过去，最后把源文件的权限（含"当前用户"的三个位，它们落到属主位上）设给副本。
- **`QFile::rename`。** 源不存在、源与目标同名时失败；"存在"用的是 `QFile` 的成员 `exists()`，它把断了的符号链接也算存在（静态的 `QFile::exists` 不算），所以断了的链接可以改名（Linux 虚拟机里查出来的：本机的那条断链接在前面的操作里已经被覆盖了，差分测试因此另加了一条只被改名的）；目标存在时，只有它与源是同一个文件（设备号和 inode 相同）、两个名字按 Qt 的大小写折叠相同时才改名，这一条要 Qt 的简单大小写折叠表，从 Qt 生成了一份（`common/tcore/tcasefoldtable.inc`，1,414 项，`tests/core/legacy/gen_casefold_table.cpp`），新函数 `equalsIgnoringCaseAsQString`；在 Linux 上这种改名照 Qt 经一个临时名分两步。其余情况先不覆盖地改名（Apple 的 `renameatx_np(RENAME_EXCL)`、Linux 的 `renameat2(RENAME_NOREPLACE)`，不支持时 `link` 再 `unlink`，再不行才 `rename`），跨设备时对普通文件逐块复制再删源。
- **其余。** `mkDir` 照 `QDir::mkpath` 逐级建、权限 0777，再把新建的每一级 `chmod` 成 0777，路径里有 `+` 抛异常；`rmDir` 照旧删的是 `getName()`（没有扩展名的名字）那个文件夹，父路径为空时是当前文件夹下的；`rmDirTree` 子项的路径照 `QFileInfo::absoluteFilePath` 先变成绝对路径并 `cleanPath`；`getDisks` 返回 `/`；`doesExistFileOrLevel` 的 psd 分支原来按 QString 的下标切，现在在宽字符串里按同样的字符切。
- **Windows** 不变：原来的 Qt 实现整段搬进 `tsystem_qt.cpp` 的 `#ifdef _WIN32`，`TFileStatus` 在那里仍由 `QFileInfo` 填，只是同样去掉了提前退出。本机编译不到。

另外：`TSystemException::getMessage` 从 `tsystempd.cpp` 挪进 `tsystem.cpp`（它不用 Qt，异常类的虚表要它）；`moveFileOrLevelToRecycleBin` 和 `_throw` 挪进 `tsystempd.cpp`，和它们调用的、还在用 Qt 的 `moveFileToRecycleBin` 放在一起；`hasMainLoop` 的 `qFatal`/`qWarning` 换成 `fprintf(stderr, ...)`（Debug 下 `abort()`），与第 4 步同类。去掉包含后，本机有 8 个文件原来经 `tsystem.h` 拿到 Qt 的类，补了包含：`tlogger.cpp`（QTime）、`tsystempd.cpp`（QDateTime、QDebug、QFileInfo）、`permissionsmanager.cpp`、`preferences.cpp`、`camerasettingswidget.cpp`、`infoviewer.cpp`、`audiorecordingpopup.h`、`.cpp`。

**进入无 Qt 构建的。** `tsystem.cpp`，`TNZCORE_NOQT_SOURCES` 从 22 个变成 23 个。它在无 Qt 构建里只有文件系统那一半：`getBinDir`、`getUserName` 这些还在 `tsystem_qt.cpp`（6c），`moveFileToRecycleBin` 在 `tsystempd.cpp`。`tbigmemorymanager.cpp`、`tstream.cpp` 还要等 6b、6c（`tsystempd.cpp`、`tenv`）。

**新测试。**

- `test_tsystem.cpp`（4 个，两个构建都有，也在 iPad 模拟器里跑）：存在的文件、文件夹、不存在的路径的 `TFileStatus`，以及建、列、复制、改名、删除、`doesExistFileOrLevel`。整个测试程序不建 `QCoreApplication`，所以它同时就是决定要的那个测试：没有应用对象时 `TFileStatus` 报告真实状态，无 Qt 构建以后也守着这一条。
- `test_tsystem_qt.cpp`（4 个，只在 Qt 构建的非 Windows 平台上）：旧代码原样留在 `tests/core/legacy/legacy_tsystem.*`（改名 `LegacyTSystem`、`LegacyTFileStatus`），测试先建一个 `QCoreApplication`（旧的 `TFileStatus` 没有它就什么都看不见）。一棵树里放了各种条目：普通文件、各种权限、读不了的文件和文件夹、可读不可进的文件夹、点开头的和带隐藏标志的文件与文件夹、指向文件和文件夹的符号链接、断了的链接、Finder 替身、fifo、NFC 和 NFD 的名字、`Ω`（U+2126，NFC 会变成 U+03A9）、`ſ`、`ß`、`İ`、合字、表情符号，以及两个各放一两百个名字的文件夹（排序用）。对树里的每个条目和若干别的写法（相对路径、`./`、`//`、末尾斜杠、`..`）问 `TFileStatus` 的全部成员和全部列目录函数的八种参数组合，新旧的输出（1.5 万行）必须逐行相同；再在两份一样的树上各跑 66 个操作（建、删、整树删、复制文件夹、各种复制和改名、替换、隐藏、关卡的复制改名删除隐藏），每个操作的结果或异常消息，和操作完以后整棵树（名字的字节、类型、权限、隐藏标志、内容、链接目标、修改时间是否保留）必须相同。第四个测试先断言没有 `QCoreApplication`，然后证明旧的 `TFileStatus` 说文件和文件夹都不存在、新的说存在。
- `test_tstring_qt.cpp` 加了一个：`equalsIgnoringCaseAsQString` 与 `QString::compare(..., Qt::CaseInsensitive)` 对照，每个码位与它的折叠、小写、大写和下一个码位配对，单独和夹在名字里各比一次（400 多万对）。

**`QCoreApplication` 之前的调用（决定要求列的）。** 用 lldb 在 macOS 的 6 个程序上断住 `TFileStatus` 的构造、`EnvGlobals` 的构造、`VariableSet` 的读写，一直跑到 `QApplication`/`QCoreApplication` 的构造：

- `OpenToonz`、`tcomposer`、`tcleanup`、`tfarmserver`、`tfarmcontroller`：一次都没有。`EnvGlobals` 是在 `TEnv::setRootVarName` 里第一次用到时才建的，这几个程序都在建好应用对象之后才调它；静态初始化里的 `TEnv::IntVar` 之类只登记变量，不建 `EnvGlobals`、不读写文件。tcomposer 在应用对象之前调的 `TEnv::getSystemPathMap()` 只返回一张静态表，不碰文件系统。所以对这些程序，两次 `setWorkingDirectory` 都在应用对象之后，6a 前后结果相同。两次之间确实有代码读第一次的结果：`EnvGlobals` 的构造函数在第一次之后接着 `init()` → `updateEnvFile()`，用 `m_isPortable` 和工作目录推出 env 文件的路径；第二次（`setApplicationFileName`）不重算，要到 `VariableSet::load` 才重算（非 Windows；Windows 不重算）。这是改动前就有的顺序，6a 没有动它。
- `tconverter`：在 macOS 和 Windows 上它**根本不建应用对象**（`main` 里只有 Linux 和其他 Unix 的分支建）。所以 6a 之前，在这两个平台上 tconverter 整个运行期间 `TFileStatus` 都说"不存在"：用 Phase 2 之前的应用包实测，`tconverter vec_circle.pli vec.tif` 和 `tconverter card_back..png card.tif` 都报"level doesn't exist"退出，什么都转不了（上游 `4600efb66` 之后一直如此）。6a 之后两个都转出来了（8 帧和 1 帧）。便携模式的判断在 tconverter 里因此也从"永远不是"变成了按 `portablestuff` 的真实情况。这不是"更早地正确"，是"从不能用到能用"，发布说明要写。转起来以后又碰到一个原来走不到的旧 bug：给矢量关卡加 `-w` 会段错误（已知问题第 29 条）。

**目录列表顺序的审计。** `readDirectory_DirItems` 的调用方（`filebrowser.cpp`、`filebrowsermodel.cpp`、`scenebrowser.cpp`、`startuppopup.cpp`、`projectpopup.cpp`）都只是把文件夹名显示在界面上：下拉框另存一份平行的路径表，行按名字找回，没有任何结果被写进文件或参与场景加载。序列帧的枚举、关卡扫描、调色板和库目录的浏览走的是 `readDirectory`，结果都先进了 `CaselessFilepathLess` 的 set 或以 `TFrameId` 为键的 map，与列表顺序无关。所以照决定不加显式排序。在 Apple 平台上顺序与 Qt 逐一相同（差分测试里的两个大文件夹）；Linux 的 Qt 构建里 `LocaleAware` 仍然问 Qt，也相同，无 Qt 构建里用 `wcscoll`（Q9 接受的差异）。

**人工验收清单**（自动测试覆盖不到应用自己的启动路径；到里程碑时交给所有者）：

1. 便携模式：macOS 上 `portablestuff` 放在 `OpenToonz.app` 旁边、放在 `OpenToonz.app` 里面，各启动一次，确认用的是它（偏好设置里的路径、新存的场景落在哪）；Linux 的 AppImage 旁边放 `portablestuff`。
2. 首次运行：Linux 上没有 `~/.config/OpenToonz` 时启动，确认从 `share/opentoonz/stuff` 复制出了 stuff、写了 `SystemVar.ini`；macOS 上照常用安装的 stuff。
3. stuff 目录缺失：`SystemVar.ini` 指向一个不存在的文件夹时启动，确认还是原来的报错对话框（macOS）或重新复制（Linux）。
4. 文件浏览器：带隐藏文件、Finder 替身、符号链接、中文和带重音的名字（NFD 的，如从 Finder 新建的）的文件夹，列表与 6a 之前的应用包一样；复制、改名（含只改大小写）、删除、新建文件夹、移到废纸篓都照常。
5. 存场景到一个还不存在的文件夹（会 `mkDir`）。
6. tconverter（macOS）：转一个 pli 和一个 png 序列，确认能转；这是 6a 改变的行为。

下面几条是 8c 并进来的（viewer 和工具经 OpenGL 画的东西，自动测试只覆盖到 tcomposer 和离线渲染）。比的是 8b 的应用包。每个程序从终端启动，标准错误上不应出现 `NextToonz: … TGLView::install() …` 那一行：

7. viewer：打开一个场景，它的矢量关卡用到各类样式：纯色、带点划线的中心线、栅格和矢量图案笔刷、矢量笔刷、colorfx 的描边（含 Sketch、FlowLine、凹凸 `TNormal2`）和区域（含点阴影、两种条纹、棋盘、渐变）。在相机视图、摄影台视图、3D 视图里看，打开和关闭"透明度检查""填色检查"各一次。
8. 图标：样式编辑器和调色板里这些样式的图标、关卡条和 xsheet 的缩略图、存场景时生成的场景图标。
9. 工具里画中心线（`drawStrokeCenterline`）的地方：几何工具旋转时的预览、弯曲工具，以及填充工具、矢量橡皮擦、栅格胶带工具选中的第一条笔画。
10. tnzext 的 GL：Plastic 骨骼和网格的显示与变形。
11. tcleanup 清理一个场景并存下，场景图标照常；tconverter 转一个 pli（与 6a 那条一起做）。

**结果（x86_64 macOS）。**

- 默认构建的编译命令：已有的编译单元一个都没变（与 Phase 2 之前比，只有第 4 步已知的 `-DNEXTTOONZ_QT_INTEROP` 和测试的 include 目录），多了 `tsystem_qt.cpp`、`legacy_tsystem.cpp` 和两个测试。
- 全部测试：469 个通过（第一次在 ctest 里并行跑时，读树的差分测试也问了根目录的 `..`，也就是公用的临时文件夹，别的测试同时在里面建删东西，结果不同；改成不问 `..` 之后通过）。
- golden 266 帧、28 个场景单线程和 8 线程的 554 帧、OpenGL 退路单线程 277 帧，都与 Phase 2 之前的构建同一天渲的逐字节相同。
- 无 Qt 构建：三道检查通过，142 个测试通过，iPad 模拟器里 54 个通过。

**结果（Linux 虚拟机）。** 照 8.3 节的经验，又有 4 个文件在 Linux 上失去了 Qt 头间接带来的 `<cstring>`（`tbigmemorymanager.cpp`、`tstream.cpp`、sound 的 `tsio_wav.cpp`、`tsio_aiff.cpp`），补了包含。差分测试在 Linux 上查出两条本机查不出的规则（上面的不可解码名字和断了的链接改名），改了之后通过；还查出测试自己的一处 `getcwd` 泄漏，也改了。最后：全部目标编译通过；442 个测试里 441 个通过，失败的仍是 `golden_fx_sweep_font_a`；GL 画布 32 个、无 Qt 构建 142 个通过。

**tcomposer 初始化 stuff 目录的前后对照（Linux 虚拟机）。** 初始化只在 Linux 上做（`TEnv::initUserStuffDir`，Windows 和 macOS 上是空的），所以在虚拟机里比。改动前用第 5 步构建的程序（同步分支前先存了一份 `bin`、`lib`），改动后用 6a 的，各自摆成安装后的样子（`<前缀>/bin/tcomposer`、`<前缀>/share/opentoonz/stuff`），在一个空的家目录里各跑四种情况：首次运行（没有 `~/.config/OpenToonz`）、第二次运行、删掉 stuff 目录留下 ini 再运行、从放着 `portablestuff` 的文件夹启动（便携模式）。每次记下整个家目录（名字、类型、权限、大小、内容的散列）、`SystemVar.ini` 的字节和渲出的帧（脚本 `toonz/build-phase1/vm/stuff_seed_6a.sh`，不进版本库）。结果：四种情况下家目录的文件树、`SystemVar.ini`、帧都逐字节相同；不同的只有 `toonzfarm/tcomposer.log` 和标准输出，它们记着输出路径（`seed_before` 比 `seed_after` 多一个字符，每帧一行）和耗时。

### 8.7 第 6b 步实际做成的样子（2026-11-18）

**接口。** 新的 `include/tkeyvaluestore.h`：抽象类 `TKeyValueStore`，现在只有字符串的读写——`contains`、`stringValue(key, 默认值)`（与 `QSettings::value(key, 默认值).toString()` 同义）、`setStringValue`、`sync`（析构时也写出）。键是 UTF-8，写法与 QSettings 一一对应：`key` 是 `[General]` 节里的键；`group/key` 是 `[group]` 节的键，group 可以嵌套成 `a/b/key`；`beginWriteArray` 写的数组是 `name/size`（长度）和从 1 起的 `name/1/key`、`name/2/key`。按类型读写（bool、int、double、颜色、尺寸、map）留给第 15 步加在这几个方法旁边，头文件的注释写明了，没有存根。

**注入。** 唯一的切换点是 `TKeyValueStore::setFactory(工厂)`：它设定 `open(路径)` 用的工厂，返回被替换的那个，传 `nullptr` 恢复内置的。内置的在 Qt 构建里是 QSettings 的实现（`tkeyvaluestore_qt.cpp`，只在 Qt 构建里编译），在无 Qt 构建里是什么都不存、什么都不写的空实现；用哪个由 `tkeyvaluestore.cpp` 编译时有没有 Qt Core（`QT_CORE_LIB`）决定，不靠静态初始化的顺序。各程序的 `main` 都没有改：`EnvGlobals` 在 `TEnv::setRootVarName` 里第一次用到时就要读 `SystemVar.ini`，比任何注入都早。

**桌面实现。** 每次 `open` 新建一个 `QSettings(路径, IniFormat)`，与原来每次读都新建一个相同；读是 `value(键, 默认值).toString()`，写是 `setValue(键, QString)`，`sync` 就是 `QSettings::sync`。默认值是空串时，键不存在的结果与原来的 `value(键).toString()` 一样是空串。

**`tenv` 的改动只有两处**：`EnvGlobals::getSystemVarPath` 读、`writeRootVar` 写（Linux 首次运行时记下 stuff 目录）。路径原来直接是 `getSystemVarFile()` 的 QString，现在先变成 `TFilePath` 再交给存储，存储里再变回 QString；下面的测试比较的正是这两种走法。`tenv.cpp` 不再包含 `<QSettings>`，其余的 Qt 照决定留给 6c。

**一个保留下来的老行为。** 手写进 `SystemVar.ini` 的、没有加引号又带逗号的值，QSettings 读成字符串列表，`toString()` 给空串，所以那样的路径读出来是空的（QSettings 自己写带逗号的值时会加引号，读得回来）。新旧都是这样，测试里有。

**进入无 Qt 构建的。** `tkeyvaluestore.cpp`，`TNZCORE_NOQT_SOURCES` 从 23 个变成 24 个。

**新测试。**

- `test_tkeyvaluestore.cpp`（两个构建都有，也在 iPad 模拟器里跑）：注入的工厂就是 `open` 用的；`setFactory` 返回被替换的；`nullptr` 恢复内置的；无 Qt 构建里内置的是空实现。
- `test_tkeyvaluestore_qt.cpp`（Qt 构建）：读——4 份手写的 ini（逗号、带逗号的引号串、反斜杠、`\x` 转义、`%25`、非 ASCII、前后空格、`@String`、分节、嵌套 group、数组、BOM、CRLF、空文件）和 8 份 QSettings 自己写的，每份问 27 个键，旧代码（`value(key).toString()` 再进 `TFilePath`）和存储取到的值逐字相同；写——5 种起始文件（含不存在）乘 8 个值乘 4 个键，用旧代码和用存储各写一份，160 对文件逐字节相同；`tenv`——注入一个内存实现后，`TEnv::getSystemVarStringValue`、`getSystemVarPathValue` 从它读，打开的是 `SystemVar.ini`；在 Linux 上 `TEnv::initUserStuffDir` 把 stuff 目录的根写进它，磁盘上不出现 `SystemVar.ini`。

**结果（x86_64 macOS）。**

- 默认构建的编译命令：已有的编译单元一个都没变，多了两个存储文件和两个测试。
- 全部测试：473 个通过。
- golden、28 个场景单线程和 8 线程的帧、OpenGL 退路单线程的帧：golden 266 帧、554 帧、277 帧都与 Phase 2 之前的构建同一天渲的逐字节相同。
- 无 Qt 构建：三道检查通过，144 个测试通过，iPad 模拟器里 56 个通过。

**结果（Linux 虚拟机）。** 全部目标编译通过（这一步没有新的 `<cstring>` 问题）；447 个测试里 446 个通过，失败的仍是 `golden_fx_sweep_font_a`，新测试里注入内存实现的那个在 Linux 上把写的那条路也走到了；GL 画布 32 个、无 Qt 构建 144 个通过。

**`SystemVar.ini` 的前后对照（Linux 虚拟机）。** 用 6a 的脚本（`toonz/build-phase1/vm/stuff_seed_6a.sh`），改动前仍是第 5 步的程序：首次运行、第二次运行、删掉 stuff 之后再运行这三种情况写出的 `SystemVar.ini` 与改动前逐字节相同；家目录的文件树、便携模式下的 `portablestuff`、渲出的帧也相同，不同的仍只有记着输出路径和耗时的日志与标准输出。

### 8.8 第 6c 步实际做成的样子（2026-11-18）

**内部头 `common/tsystem/tsystem_qtsemantics.h`。** 6a 在 `tsystem.cpp` 里照 Qt 写的辅助函数原来在匿名命名空间里，现在放进命名空间 `TSystemQtSemantics`，其中 `tenv.cpp` 要用的经这个头暴露。头在 `common/tsystem/` 里，不在 `include/`，注释写明照 Qt 5.15 的语义、不是公共接口。新加的有：

- `cleanPath`：照 `qt_normalizePathSegments` 逐行移植。一个 `..` 退不上去时 Qt 会留着（`/..` 还是 `/..`），6a 那个简化版把它丢了，这次的对照查了出来；`rmDirTree` 用的 `absoluteFilePath` 也换成了它。
- `absoluteFilePath`：照 `QFileSystemEngine::absoluteName`。已经干净的绝对路径原样返回；否则把当前文件夹和路径按字节拼起来，解码（Apple 上因此变成 NFC），清理，保留末尾的斜杠。`absolutePath` 是它的上一级。
- `canonicalFilePath`：照 `canonicalName`。根原样返回；Apple 上先确认存在；`realpath` 成功就解码再清理；不存在给空串；别的失败给路径本身。
- `currentPath`（`getcwd` 解码）、`homePath`（`$HOME`，空时是根，再清理）、`tempPath`。`tempPath` 先取 `$TMPDIR`，没有时在 Apple 上取 `confstr(_CS_DARWIN_USER_TEMP_DIR)`，也就是 `NSTemporaryDirectory` 给的那个，再没有就用 `/tmp/`；最后像 Qt 那样清理并取规范路径，所以 macOS 上 `TMPDIR=/tmp/` 给的是 `/private/tmp`。
- `applicationFilePath`、`applicationDirPath`：Apple 上是主 bundle 的可执行文件（`CFBundleCopyExecutableURL`），存在时取规范路径；Linux 上是 `/proc/<pid>/exe` 的规范路径，与 Qt 相同。Qt 在这些都失败时还会按 `argv[0]` 和 `PATH` 去找，核心拿不到 `argv`，没有这一步。照决定，没有 `QCoreApplication` 时也给真实路径。
- `toLatin1`、`environmentVariable`（`QString::fromLocal8Bit(qgetenv(...))`）、`mkpath`、`rename`（`QDir().rename` 就是两个 `QFile::rename`）、`removeRecursively`（照 Qt：先列出包括隐藏和系统条目在内的全部，指向文件夹的链接当文件删，删不掉而没有写权限时先加写权限再删一次，全部成功才删文件夹本身）、`copyDirTree`（就是 `tenv` 原来的 `copyDirOrFail`）。列目录多了 `QDir::System` 过滤器，条目多了 `isSymLink`。

在 Windows 上，`tenv.cpp` 要的那几个由 `tsystem_qt.cpp` 用 Qt 本身实现。没有应用对象时，可执行文件的路径改用 `GetModuleFileNameW`，这一段本机编译不到。

**`TSystem`。** 新的 `getHostNameW`（Linux 上是 `uname` 的 nodename，其他是 `gethostname`，与 `QSysInfo::machineHostName` 相同）、`getUserNameW`（按环境变量的顺序找第一个非空的 `USER=`，否则是 `none`）、`getUniqueFileW`（临时文件夹、一个反斜杠、field、32 个小写十六进制的 v4 UUID，随机数来自 `std::random_device`）。`getTempDir`、`getBinDir`、`sleep`（`std::this_thread::sleep_for`）、`getProcessorCount`（Apple 上 `sysctl(HW_NCPU)`，Linux 上 `sched_getaffinity`，与 `QThread::idealThreadCount` 相同）改成平台调用。返回 QString 的 `getHostName`、`getUserName`、`getUniqueFile(QString)` 留在 `tsystem_qt.cpp` 当包装，调用点一个都没改；`getSystemValue`（Windows 注册表）和 `isDLLBlackListed` 原样留在那里，`tenv` 的 Windows 分支改调新的 `getSystemValueUtf8`。Windows 上这些仍是原来的 Qt 代码。

`showDocument` 成了注入的服务：唯一的切换点是 `TSystem::setShowDocumentService`，用法与 6b 的存储一样。Qt 构建内置的是原来的代码（`QDesktopServices`，Windows 上是 `ShellExecute`），无 Qt 构建内置的不打开任何东西、返回 false。

**`tenv.cpp`** 不再包含 Qt 的头，只有 Haiku 分支还在 `#ifdef HAIKU` 里用 `QStandardPaths`，所以无 Qt 构建不支持 Haiku。`SystemVar.ini` 的位置改成宽字符串拼接。工作目录照旧先转 Latin-1：那是一个缺陷，记为已知问题第 31 条，原样保留。便携检查和首次运行都改用上面的辅助函数，`tfilepath_io.cpp` 打开文件用的 UTF-8 也去掉了 QString。

**行为的变化（照决定）。** 只在没有应用对象的时候：可执行文件旁边的那一处便携检查以前因为路径是空的所以从来不看，现在会看。实测只影响 macOS 和 Windows 上的 tconverter，发布说明的 tconverter 条目要写上。macOS 上 tconverter 转 pli 和 png 序列的结果与 6a 之后逐字节相同。核心里另外还有两处直接问 `QCoreApplication::applicationDirPath`：`tcodec.cpp`（找 LZO 的辅助程序）和 toonzlib 的 `thirdparty.cpp`（找 ffmpeg）。它们属于第 13 步，tconverter 运行时仍会打两行 Qt 的警告。

**进入无 Qt 构建的。** `tenv.cpp`、`tfilepath_io.cpp`，`TNZCORE_NOQT_SOURCES` 从 24 个变成 26 个，不需要别的改动就链接通了。

**新测试。**

- `test_tsystem_qt.cpp` 加了 6 个（Qt 构建）：
  - 进程信息与 Qt 逐字相同：可执行文件的路径和文件夹、`getBinDir`、主机名、用户名（`USER` 为普通值、空、非 ASCII、带 `=`、未设）、临时文件夹（8 种 `TMPDIR`：带末尾斜杠、带 `//` 和 `..`、带 `.`、相对路径、根、NFD、空、未设）、家目录（6 种 `HOME`）、环境变量、当前文件夹（含 NFD 和中文名的文件夹、经链接进入）及其 Latin-1、处理器数；`getUniqueFile` 只比格式。
  - `cleanPath` 与 `QDir::cleanPath` 对照：由 `a`、`/`、`.` 组成的长度 7 以内的全部字符串（3,279 个），外加几个特例。
  - 树里每条路径的 `absoluteFilePath`、`absolutePath`、`canonicalFilePath` 与 `QFileInfo` 对照。
  - 启动形态：新的小程序 `nexttoonz_appinfo_probe` 在应用对象建立前后各报一次核心和 Qt 的可执行文件路径。测试用 8 种方式启动它：绝对路径、相对路径、经 `PATH`、经符号链接（绝对、相对）、拷进一个名字带非 ASCII 的 `.app` 包里（绝对、相对），以及经指向包内的符号链接。每一种里核心的结果在应用对象前后都与 Qt 逐字相同。
  - `removeRecursively` 与 `QDir::removeRecursively`：在两份一样的树上比（嵌套文件夹、隐藏条目、只读文件、只读文件夹、指向树外文件夹和文件的链接、NFD 名字；另一组加断链接、读不了的文件夹和 fifo），比返回值和留下的整棵树，链接指向的树外文件夹必须还在。
  - 首次运行的复制与 `tenv` 原来的 `copyDirOrFail`（照抄进测试）：新复制、复制进已有的文件夹、复制到还不存在的多级文件夹，再接 `rename` 和 `mkpath`，比返回值和整棵树。树里放了一个按名字排在最后的断链接，所以第一次复制在最后一项失败。这里不放 fifo：`QFile::copy` 遇到它会一直等，新旧都一样。
- `test_tsystem.cpp` 加了 2 个（两个构建都有，也在 iPad 模拟器里跑）：不带 Qt 时这些函数照样给出合理的值；`showDocument` 走注入的服务，`nullptr` 恢复内置的那个，无 Qt 构建里内置的那个返回 false。
- `test_tkeyvaluestore.cpp` 加了 1 个（两个构建都有）：`TEnv` 经注入的存储读系统变量。它在无 Qt 构建和 iPad 模拟器里跑，证明 `tenv` 不带 Qt 能用。

**结果（x86_64 macOS）。**

- 默认构建的编译命令：库和程序的编译单元一个都没变；核心测试的编译单元多了一个宏（探测程序的路径）；多了探测程序本身。
- 全部测试：482 个通过。
- golden、单线程和 8 线程的帧、OpenGL 退路单线程的帧：554 帧和 277 帧都与 Phase 2 之前的构建同一天渲的逐字节相同，golden 也通过。
- 无 Qt 构建：三道检查通过，147 个测试通过，iPad 模拟器里 59 个通过。

**结果（Linux 虚拟机）。** 全部目标编译通过，没有新的 `<cstring>` 问题；456 个测试里 455 个通过，失败的仍是 `golden_fx_sweep_font_a`。差分测试在 Linux 上也全部通过，包括 5 种启动形态（`/proc/self/exe`）和 `removeRecursively`、复制的对照。GL 画布 32 个、无 Qt 构建 147 个通过。

**首次运行的四种情形（Linux 虚拟机）。** 用 6a 的脚本，改动前仍是第 5 步的程序。首次运行、第二次运行、删掉 stuff 之后、便携模式四种情形下，`SystemVar.ini`、家目录的文件树、`portablestuff`、渲出的帧都与改动前逐字节相同；不同的仍只有记着输出路径和耗时的日志与标准输出。

### 8.9 第 7 步实际做成的样子（2026-11-18）

**`include/ttranslatabletext.h`。**
- `TTranslatableText` 只有决定里的三项：上下文、UTF-8 的源文本、是否翻译。
- 要翻译的文字一律写成 `TTranslatableText("类名", QT_TRANSLATE_NOOP("类名", "文本"))`，没有另定宏；不翻译的写成 `TTranslatableText::untranslated(...)`。
- 头里在没有 Qt 时定义 `QT_TRANSLATE_NOOP(scope, x)`，定义与 Qt 的逐字相同，两者都出现也不会冲突。
- 窄的翻译服务：`translateForData` 和唯一的切换点 `setDataTranslator`，做法与 6b、6c 相同。头注释写明它只给"核心把译文存进数据"的地方用。Qt 构建内置的是 `QCoreApplication::translate`（`ttranslatabletext_qt.cpp`），无 Qt 构建内置的是原样返回。
- 不翻译的文字不经过翻译服务。

**样式。**
- `TColorStyle` 多了三个虚函数 `getDescriptionText`、`getParamNameText`、`getParamRangeText`。原来的 `getDescription`、`getParamNames`、`getParamRange(int, QStringList &)` 改成非虚的 Qt 包装，在新文件 `tcolorstyles_qt.cpp` 里：要翻译的走 `QCoreApplication::translate(上下文, 源文本)`，与原来各样式自己调的那个一字不差；不翻译的走 `QString::fromUtf8`，与原来从 `const char *` 构造 QString 相同。调用点一处都没改。
- tnzcore、colorfx、toonzlib 里重写这几个函数的样式全部改成重写新的虚函数：`getDescriptionText` 56 处、`getParamNameText` 50 处（含头里的内联定义），带翻译的串共 180 个。这一批是用脚本转的再逐个看过：脚本把 `items << a << b` 拆成几个 `push_back` 时，有一处原来是不带花括号的 `if`，拆开以后只有第一个还受 `if` 管（`TTextureStyle`，编译时就报了错），补了花括号，其余三处都不在条件句里。
- 原来就有的怪处原样保留：清理样式（`TColorCleanupStyle`）里有两句 `QObject::tr(...)` 的结果从来没被用过，现在是两句 `(void)QT_TRANSLATE_NOOP(...)`，lupdate 照样提取；MyPaint 的参数名来自 libmypaint，不翻译。
- `tcolorstyles.cpp` 里另外两处 Qt 也去掉了：存档时 `QString::number(m_flags)` 换成 `std::to_string`；读档时那个标志只有一位数字，原来的 `toUInt()` 换成直接减 `'0'`。
- 一个限制：派生类如果重写了别的 `getParamRange` 重载，经派生类的类型调用 QStringList 版本会被名字隐藏、编译不过。界面一直是经 `TColorStyle` 调用的，全树编译通过；新测试里经基类调用。

**调色板。** `TPalette` 构造时第一页的名字原来是 `QObject::tr("colors")`，现在是 `translateForData(TTranslatableText("QObject", QT_TRANSLATE_NOOP("QObject", "colors")))`。Qt 构建里结果不变。无 Qt 构建和 iPad 在壳注入翻译服务之前，这一页叫英文的 "colors"；注入它是 iPad 壳要做的事之一。

**顺带。** `tvectorimage.h` 的 `QSet<int>` 换成 `std::set<int>`：`addStrokeBelow` 只用它判断有没有，与顺序无关；tnztools 的 `toonzvectorbrushtool.cpp` 里 4 处跟着改。`tsimplecolorstyles.h` 不再包含 `<QCoreApplication>`，`tcolorstyles.h` 不再包含 `<QString>`。原来经它们拿到 Qt 类的 `timagecache.cpp` 和 `tpalette.cpp` 补了包含。改动行按仓库的 `.clang-format` 用 `git clang-format` 排过版。

**进入无 Qt 构建的，和没进去的。** 进去的只有 `ttranslatabletext.cpp`（26 个变成 27 个）。
- 这一步之后，矢量图和样式的 16 个源文件编译时已经碰不到 Qt 的头：`tvectorimage`、`tregion`、`tcomputeregions`、`tstroke`、`tsweepboundary`、`drawutil`、`outlineApproximation`、`tcolorstyles`、`tellipticbrush`、`tinbetween`、`tpaletteutil`、`tregionprop`、`tstrokedeformations`、`tstrokeprop`、`tstrokeutil`、`tvectorbrush`。
- 但它们链接不闭合，还要 `TPalette`、`tsimplecolorstyles.cpp` 里的样式、`TOfflineGL`、桌面 GL 的函数、`TRaster`、`TImage` 和 tnzcanvas。
- 这几处里，`tpalette.cpp`（帧号区间的解析、`QString::number(…, 'g', 17)` 和 `toDouble`）、`tsimplecolorstyles.cpp`（`QDir::match` 的通配符、`sourceFilters` 的 QString、两处 `QMutexLocker`）、`tl2lautocloser.cpp` 和 `tsegmentadjuster.cpp`（`qDebug`）还有不带 GL 的 Qt；`tsimplecolorstyles.cpp` 还带着桌面 GL。
- 所以迁移表第 7 步那一栏写的"矢量图、样式进入无 Qt 构建"要到第 8 步把 GL 移走时才做得到。上面那些不带 GL 的 Qt，建议放在第 8 步开头一起做，见报告。

**新测试。**
- `test_ttranslatabletext.cpp`（两个构建都有，也在 iPad 模拟器里跑）：三项字段；翻译服务走注入的那个；不翻译的文字不经过服务；`nullptr` 恢复内置的；无 Qt 构建里内置的那个原样返回。
- `test_ttranslatabletext_qt.cpp`（Qt 构建）：几个样式给出的上下文和源文本，经包装以后的结果；新调色板的页名经注入的服务得出，恢复以后又是 "colors"。

**前后对照**（工具在 `toonz/build-phase1/ab/phase2/translations/`，不进版本库，`README.md` 写了怎么重跑）。
- 导出程序：用仓库里没有改过的 .ts 经 lrelease 编出每种语言的 tnzcore、colorfx、toonzlib 三个 .qm，装上以后导出全部注册样式（tnzcore、colorfx、toonzlib 的）的描述、各参数名、枚举项和扩展名列表，外加新调色板的页名；先不装任何译文导出一遍，再按 11 种语言各导出一遍。
- 改动前：用 `84f17b336` 的头编译，链接 Phase 2 之前应用包里的库；改动后：用新的头和新构建的库。两份各 3,048 行，逐字节相同。
- lupdate：对改动前和改动后的源码，照 `translations/update.sh` 的做法，对 colorfx、common、toonzlib 各跑一遍，写进仓库里那些 .ts 的副本（用 `-locations none`，因为仓库里的 .ts 不带位置）。11 种语言的 33 个文件逐字节相同。
- 注意仓库里的 .ts 本来就不是今天的源码跑 lupdate 会得出的样子，所以比的是"改动前跑一遍"和"改动后跑一遍"，不是和仓库里的文件比。

**结果（x86_64 macOS）。**

- 默认构建的编译命令：已有的编译单元一个都没变，多了三个新源文件和两个测试。
- 全部测试：486 个通过。
- 渲染对照：golden 通过；单线程和 8 线程的 554 帧、OpenGL 退路单线程的 277 帧，都与 Phase 2 之前的构建同一天渲的逐字节相同。
- 无 Qt 构建：三道检查通过，149 个测试通过，iPad 模拟器里 61 个通过。

**结果（Linux 虚拟机）。** 又有 3 个文件在 Linux 上失去了间接带来的 `<cstring>`（trop 的 `tresample.cpp`、`tropcm.cpp`，toonzlib 的 `sandor_fxs/blend.cpp`），补了包含；虚拟机的构建是逐轮查出来的，每次只有一个库报错。之后：全部目标编译通过；460 个测试里 459 个通过，失败的仍是 `golden_fx_sweep_font_a`；GL 画布 32 个、无 Qt 构建 149 个通过。macOS 上补完这几处包含以后又跑了一遍全部测试，486 个通过。

### 8.10 第 8a 步实际做成的样子（2026-11-18）

**新函数**（`tconvert.h`、`tstring.cpp`，与第 3、4 步的那几个同类，在无 Qt 构建里也有）：

- `toStringAsQString(v)`：等于 `QString::number(v, 'g', 17)`。实测（Qt 5.15.19）与 printf 的 `%.17g` 有三处不同，都照 Qt 做了：
  - 正负零都写成 `"0"`（printf 写 `"-0"`）；
  - 任何 NaN 都写成 `"nan"`；
  - 第 18 位有效数字上正好是 5 的精确平局，Qt（double-conversion）远离零进位，printf 进到偶数。例如 `-121544296184850.125` 在 Qt 里是 `"-121544296184850.13"`，printf 给 `...850.12`。只在第 18 位是 5 时才走慢路：取完整的十进制展开，自己舍入。
- `toDoubleAsQString(s, ok)`：等于 `QString::fromStdString(s).toDouble(ok)`。实测的规则：
  - 两端的空白照 `QChar::isSpace` 去掉，U+FEFF 不算空白（开头那个是 BOM，读的时候本来就丢了）；
  - U+2212 MINUS SIGN 在任何能放 `-` 的地方都当 `-`，指数里也一样；
  - 不分大小写的 `nan` 和可带符号的 `inf` 可以，`+nan`、`infinity`、十六进制、千分位都不行；
  - 上溢给无穷大但 ok 为假，下溢（`1e-400`）给 0 也不 ok。
- `toIntAsQString(s, ok)`：等于 `QString::fromStdString(s).toInt(ok)`，空白和 U+2212 的规则同上，超出 int 时给 0。
- `matchesWildcardsAsQDir(filters, name)`：等于 `QDir::match`。照 Qt 5.15 的 `QRegularExpression::wildcardToRegularExpression` 加不分大小写：
  - `*` 和 `?` 不跨 `/`；
  - `[...]` 里 `!` 和 `^` 都表示取反，紧跟着的 `]` 是字面字符；
  - 没有闭合的 `[`、类里出现 `/`、倒过来的区间，都使正则表达式无效，因而什么都不匹配；
  - 反斜杠和正则表达式的元字符都是字面字符；
  - 大小写按 Unicode 的简单大小写折叠比，用第 6a 步的那张表，所以 `ſ.png` 匹配 `*.png`。

**用到的地方。**
- `tpalette.cpp`：参考帧列表的写法从 `QList` 换成 `std::vector`，读法换成上面的 split 和 `toIntAsQString`；拖尾大小倍数的写读换成 `toStringAsQString`、`toDoubleAsQString`；快捷键的 `Qt::Key_0`、`Key_1`、`Key_9` 换成同值的常量（调用方传进来的就是这些值）。
- `tsimplecolorstyles.cpp`：`sourceFilters` 多了 std 版本 `sourceFilterPatterns`，QString 版本搬到 `tcolorstyles_qt.cpp` 当包装（Q2）；`findSource` 用 `matchesWildcardsAsQDir`。
- 只在 Windows 上编译的显示列表那段里有两处 `QMutexLocker` 锁着 `TThread::Mutex`，是第 1 步漏掉的，在 Windows 上会编译不过，改成 `TThread::MutexLocker`。
- 两处 `<QDebug>` 的包含去掉了：用到它的两句 `qDebug()` 本来就在注释里。

这一步之后，矢量图和样式的源文件里只剩桌面 GL，没有 Qt 了。

**新测试。**
- `test_tstring_qt.cpp`（Qt 构建）加了 4 个对照：
  - 写：一百万个随机比特模式和一百万个随机的"好看"数，外加约两千个边界值（±0、次正规数、最大最小、inf、各种 NaN、10 的每个整数次幂附近）；
  - 读：语料、开头和结尾放上 U+3100 以下每个码位的 `2.5`、写出再读回的三十万个随机数、三十万个由数字字符随机拼成的串；
  - 整数：语料和二十万个随机串；
  - 通配符：TrailStyles 的 16 个模式对各种名字、扩展名的每个字母换成每个 BMP 码位、33 个带 `?`、`[]` 和各种角落的模式对 29 个名字。
  - 实测的样本（负零、NaN、平局、U+2212、BOM、下溢）都单独写成了断言。
- `test_tstring.cpp`（两个构建都有，也在 iPad 模拟器里跑）：同一批实测样本。

**调色板的前后对照**（工具在 `toonz/build-phase1/ab/phase2/tpl/`，不进版本库，`README.md` 写了怎么重跑）。
- 用改动前（`84f17b336` 的头加 Phase 2 之前的库）和改动后各编一个程序。
- 写：各自写出 161 个调色板，每个有 50 个拖尾样式，大小倍数取自 8,000 多个值（样式会把它夹到范围里），参考帧列表随机，两边逐字节相同。
- 读：各自读进再写出 206 个 `.tpl`（改动前写的那 161 个、参考工程的 4 个、30 个把大小倍数换成怪串的、11 个把帧列表换成怪串的），两边逐字节相同。
- 另有 3 个帧列表（`"3-2"`、`""`、`"-"`）会得到"已指定但为空"的参考帧，存档时两边都读空列表的第 0 个元素，是未定义行为：改动前写出垃圾数，改动后会崩溃。没有放进对照，记为已知问题第 32 条。

**结果（x86_64 macOS）。**

- 全部测试：491 个通过。
- 渲染对照：golden 通过；单线程和 8 线程的 554 帧、OpenGL 退路单线程的 277 帧都与 Phase 2 之前的构建同一天渲的逐字节相同。
- 无 Qt 构建：三道检查通过，150 个测试通过，iPad 模拟器里 62 个通过。

**结果（Linux 虚拟机）。** 全部目标编译通过，没有新的 `<cstring>` 问题；464 个测试里 463 个通过，失败的仍是 `golden_fx_sweep_font_a`；GL 画布 32 个通过。最后一次提交后又在虚拟机里跑了核心测试：清洁剂构建 211 个全部通过，其中包括上面那些与 Qt 的对照，所以在 glibc 的 printf 和 strtod 上也相同；无 Qt 构建 150 个通过。

### 8.11 第 8b 步实际做成的样子（2026-11-18）

核心里矢量图、样式和属性类对 GL 的每一处使用，现在都经 `tglentries.h` 的三种接缝走。GL 的实现暂时还在 tnzcore（`tglentries_gl.cpp`）和 colorfx（`colorfx_gl.cpp`）里，Qt 构建默认装上，所以默认路径和 GL 退路都没有变。8c 把这两个文件连同其余的 GL 搬进 tnzglview。

**三种接缝**（`namespace TGLEntries`，每种只有一个切换点，设置时交回被替换的那个，传 `nullptr` 回到内置的）：

1. **viewer 画布**，`setViewerCanvas(const ViewerCanvas *)`。钩子就是 8b 的决定里定下的那一组，一个不多：
   - `paint(painter, what, saveAllAttributes)`：在当前 GL 上下文上建一张画布，连同那里线条平滑开着没有，一起交给 `painter`；`saveAllAttributes` 为真时，前后是 `glPushAttrib(GL_ALL_ATTRIB_BITS)` 和 `glPopAttrib()`。这一个钩子包了决定里的"建画布、线条平滑的状态、在外面保存全部属性的开关"三项。
   - `pushTransform(aff)`：`glPushMatrix()` 加 `tglMultMatrix(aff)`。
   - `popTransform()`：`glPopMatrix()`。
   - `pixelSize2()`：`tglGetPixelSize2()`。

   头注释写明钩子到此为止，再加要先问。调用方用模板 `onViewerCanvas(lambda, saveAll)`。
2. **按类注册的 GL 画法**，`setDrawer(entry, drawer)`。以成员函数的全名为键，例如 `"TCenterLineStrokeStyle::drawStroke"`。画法是普通函数，参数是样式本身和那个成员的参数，四种签名（描边、带轮廓的描边、区域、图案描边）。
3. **离线上下文**，`setOfflineFactory(factory)`。`factory(size, stock)` 交出一个 `OfflineContext` 对象，它有 `clear`、`makeCurrent`、`draw(image, rd, doInitMatrix)`、`getRaster`（一份拷贝）。`stock` 为真时是 `TOfflineGL::getStock` 的共享上下文，对象不拥有它。内置的工厂把 `TOfflineGL` 包一层。

**没有装接缝时**（Q8-C）。画布钩子和画法什么都不画，`pixelSize2()` 给 1，`openOffline` 给空。四处离线调用因此有各自的退路：
- 图标给一张白图；
- 图案关卡的帧给一张清空的栅格；
- `TVectorImage::render` 给空。

在 Qt 构建里，接缝不会缺：内置的总在。只有删掉一个画法，或问一个没有的画法时，才往标准错误打一行（全程一次），Debug 构建里还会 assert。这一行提到 `TGLView::install()`，那是 8c 的入口。无 Qt 构建里什么都不打。iPad 上图标和图案笔刷与桌面不同的那条待办（Q8-C），等 8d 矢量图和样式进了无 Qt 构建、真能在 iPad 上跑到时再写。

**改了哪些地方。**

- **画布钩子**：
  - 样式里原来 `TLegacyGlCanvas gl; drawXxxOnCanvas(gl.canvas(), …, glIsEnabled(GL_LINE_SMOOTH))` 的入口层，一共 26 处：colorfx 描边 21 处、区域 2 处，tnzcore 的纯色样式 2 处，再加 `drawThroughGl` 一处，它就是带保存全部属性开关的 `onViewerCanvas`。经 `drawThroughGl` 画的 11 个区域样式里，有 8 个就这样完了；另外 3 个带自己的 GL 状态，是画法。
  - 6 个属性类的压入、弹出变换和像素大小：`TSimpleStrokeProp`、`OutlineStrokeProp`、`OutlineRegionProp`、`VectorBrushProp`、`TMatrioskaStrokeProp` 和 `TOptimizedStrokePropT<T>`，函数体留在核心。
  - 调查漏掉的一处：轮廓生成（`tellipticbrushP.h` 的 `OutlinizationData`）在调用方没给像素大小时，自己问 `tglGetPixelSize2()`。它在第一版注入测试里露了出来：没有 GL 上下文时，套娃样式在 `glMatrixMode` 里崩溃。现在改问 `TGLEntries::pixelSize2()`，用的是已有的钩子，没有加新钩子。
- **画法**，正好决定里的 9 个，函数体原样搬进 GL 文件，经友元结构访问私有成员，类里写了注释，说明那是 GL 一侧的实现细节：
  - tnzcore 的 3 个（`TGLEntriesAccess`）：中心线、栅格图案、矢量图案；
  - colorfx 的 6 个（`TColorFxGLAccess`，由 `initColorFx()` 注册）：Sketch、FlowLine、`TNormal2`、点阴影、两种条纹。
- **离线上下文**：四处都保留原来的调用顺序：
  - `TColorStyle::makeIcon`：共享上下文，清成白色，画；
  - 矢量图案的 `makeIcon`：同上；
  - `TVectorImage::render`：新上下文，清成透明，设为当前，画；
  - 栅格图案的 `loadLevel`：整个关卡一个新上下文，第一帧要用时才建，帧与帧之间不清空，每帧画在上一帧上面。
- **colorfx 的数据里不再有 GL 的值**：`DrawmodePointsMatrix` 的 `GLenum` 换成 `unsigned int`，`GL_LINES`、`GL_POINTS` 换成同值的 `DrawLines`、`DrawPoints`，类布局不变。`strokestyles.h`、`flowlinestrokestyle.h` 不再包含 `tgl.h`。
- `tglentries.cpp` 本身没有 GL，也没有 Qt，进了无 Qt 构建（`TNZCORE_NOQT_SOURCES` 现在 28 个）。

**还留着 GL、交给 8c 的**：
- Q8-D 的那几个调试画法；
- `#ifdef _DEBUG` 里的 `OutlineViewerStyle` 和 `drawAutocloses`，照 Q8-D 原样不动；
- `tsimplecolorstyles.cpp` 里只在 Windows 上编译的显示列表管理器。它的 `getDisplayListId` 没有调用者，只有 `clearLists` 被调用，清的是一张永远空着的表。8c 处理时再问是搬还是删。

**新测试。**
- `test_tglentries.cpp`（两种构建都有，也在 iPad 模拟器里跑）：三种接缝的设置、交回和转交。Qt 构建里，内置的都在，缺画法时只说一次；无 Qt 构建里什么都没装，问了也什么都不说。
- `test_gl_seams.cpp`（toonzlib 测试，没有 GL 上下文，假的钩子记下收到了什么）：
  - 属性类在压入和弹出变换之间画，中间问一次像素大小；
  - 象棋格子之类的区域在外面保存全部属性，渐变和纯色不保存；
  - colorfx 的 22 个描边样式和 13 个区域样式都画到交给它们的画布上。这一组本身也是检查：哪里还直接调 GL，没有上下文就会崩溃；
  - 9 个画法各有内置的，换成假的以后，入口带着样式本身走到假的那里，画布上什么也没有；
  - 四处离线调用的顺序。图案关卡用参考工程 8 帧的 `vec_circle.pli`：一次建上下文，8 次"画、取回"，一次也没清空。

**GL 的前后对照**（工具在 `toonz/build-phase1/ab/phase2/glseams/`，不进版本库，`README.md` 写了怎么重跑）。一个程序关掉 CPU 栅格化器，分别用 8a 和 8b 应用包里的库各跑一次，写出 19 张栅格，两边逐字节相同，而且每张都有画出来的像素：
- 栅格图案和矢量图案的描边，经 `tglDraw` 画。栅格图案的纹理就是在那个不清空的上下文里一帧叠一帧做出来的。
- 两种图案的图标；
- 9 个 colorfx 样式的图标，6 个画法和保存全部属性的区域都在里面；
- 其中 4 个描边样式的描边；
- 带点划线的中心线；
- `TVectorImage::render`。

**结果（x86_64 macOS）。**

- 全部测试：505 个通过（新增 14 个）。
- 渲染对照：golden 通过；与 8a 的应用包（改动前当天复制的）对照，单线程和 8 线程的 554 帧、OpenGL 退路单线程的 277 帧逐字节相同；上面那个 GL 程序的 19 张栅格逐字节相同。
- 无 Qt 构建：三道检查通过，154 个测试通过，iPad 模拟器里 66 个通过。

**结果（Linux 虚拟机）。** 全部目标编译通过（两个 colorfx 头去掉 `tgl.h` 也没有引出新的缺头问题）；清洁剂构建 478 个测试里 477 个通过，失败的仍是 `golden_fx_sweep_font_a`，新的接缝测试在 ASan、UBSan、LSan 下都通过；GL 画布 32 个通过；无 Qt 构建 154 个通过。

### 8.12 第 8c 步实际做成的样子（2026-11-18）

桌面 GL 搬进了新库 tnzglview。tnzcore 和 colorfx 不再链接 OpenGL，也不再链接 tnzglcanvas。源文件和头都留在原处，名字没变，调用点一处没改，变的只是哪个库编译它们、谁链接谁。

**tnzglview 里有什么**（`toonz/sources/tnzglview/CMakeLists.txt`）：
- GL 工具层：`tgl.cpp`、`tstencilcontrol.cpp`、`tgldisplaylistsmanager.cpp`、`tofflinegl.cpp`、`qtofflinegl.cpp`、`tglcanvasbridge.cpp`；
- 旧的 GL 矢量渲染：`tglregions.cpp` 里 `tglDraw` 那一半。另一半是画到画布上的遍历（`tvectorcanvas.h` 声明的 `tcanvasDraw`、`tcanvasCanDraw`），它不碰 GL，分出来成了 `tvectorcanvas.cpp`，留在 tnzcore。
- 接缝的 GL 实现和 9 个画法：`tglentries_gl.cpp`，以及 colorfx 的 `colorfx_gl.cpp`（文件还在 colorfx 目录里，由 tnzglview 编译）；
- Q8-D 要搬的 `drawStrokeCenterline`，连同它用的 `drawQuadraticCenterline`、`drawutil.h` 里没有调用者的 GL 纹理辅助函数 `prepareTexture`：从 `drawutil.cpp` 搬进新文件 `drawutil_gl.cpp`；
- 新的 `include/tglview.h`：`TGLView::install()` 把画布钩子、离线上下文的工厂和 9 个画法放进 `tglentries.h` 的接缝。

8b 的内置默认没有了：tnzcore 自己什么都不装，Qt 构建也一样。

**链接**：
- tnzglview 私有地链接 tnzcore、colorfx、tnzcanvas、tnzglcanvas、Qt 的 Core、Gui、OpenGL 和 GL 库。全部私有，所以链接它的库的链接行上只多它一个。
- 链接它的有：toonzlib、tnzext、tnzstdfx、toonzqt、tnztools、OpenToonz、tcomposer、tconverter、tcleanup、toonzpreview（只在 Windows 上编），还有 toonzlib 测试、GL 画布测试和夹具生成器。
- 迁移表原来只写了"toonzqt、tnztools、toonz 链接它"。可是 toonzlib、tnzext、tnzstdfx 自己就直接用 `TOfflineGL`、`tglDraw`、模板缓冲这些，调用点不改，就只能也链接它。

**每个程序装不装**（Q8-B，按"能不能走到 GL 路径"判断）：

| 程序 | 装 | 依据 |
|---|---|---|
| OpenToonz | 装 | viewer、图标、工具都经 GL 画 |
| tcomposer | 装 | GL 退路（`NEXTTOONZ_VECTOR_RASTERIZER=gl`）和 CPU 栅格化器画不了的样式 |
| tconverter | 装 | 转矢量关卡时直接用 `TOfflineGL` 和 `tglDraw` |
| tcleanup | 装 | 清理完存场景（`ToonzScene::save`），存的时候做场景图标（`makeSceneIcon`）；图标里有 CPU 画不了的东西时，退回 `TOfflineGL` 画整帧 |
| toonzpreview（Windows 的缩略图 DLL） | 装，放在它只做一次的初始化里 | 经 `IconGenerator` 用 `TOfflineGL` 画缩略图；本机编不到 |
| tfarmserver、tfarmcontroller | 不装 | 只链接 tfarm，只起 tcomposer 进程，不画 |
| lzocompress、lzodecompress | 不装 | 不链接 toonz 的库 |
| t32bitsrv | 不装 | 只在 32 位 macOS 上编，不画 |
| toonzlib 测试、夹具生成器 | 装 | 照 tcomposer 的样子初始化 |
| 核心测试、GL 画布测试 | 不装 | 核心测试测的就是"不装时什么都没有"；GL 画布测试只用 `TOfflineGL` 和模板缓冲，不经接缝 |

`tfarmclient` 的 `thumbnail.cpp` 也用 `TOfflineGL`，但这个目录不在构建里。

**没装时**（Q8-C）：照 8b，Qt 构建里第一次碰到缺的接缝时往标准错误打一行。前后对照程序不调 `install()` 时，19 张栅格全都什么也没画，标准错误上正好一行：`NextToonz: an offline OpenGL render needs the OpenGL view (TGLView::install()), which this program has not installed; nothing is drawn`。

**删掉的**（Q8-D，以及 8c 开工时的决定；提交信息里逐个列名），都没有调用者：
- `tsweepboundary.cpp` 的 `drawPointSquare`、`drawPointCross`；
- `tl2lautocloser` 的 `TL2LAutocloser::draw`，以及它的 `Imp::drawLinks`、`Imp::drawStroke`、`Imp::drawStrokes`；
- `tsegmentadjuster` 的 `TSegmentAdjuster::draw`；
- `outlineApproximation.cpp` 的 `drawQuadratic` 和 `drawOutline`；
- `tsimplecolorstyles.cpp` 里只在 Windows 上编的显示列表管理器，连同 `TVectorImagePatternStrokeStyle::clearGlDisplayLists`（只调它），还有只给它用的 `checkGlError`、`CHECK_GL_ERROR` 和没人用的 `QUAD_PRIMITIVE`。

**和 Q8-D 原文不同的一处**（主会话已认可，并更正了 Q8-D，见第 10 节"8c 做完后的决定"）。 Q8-D 说 `drawQuadratic` "有调用者，搬进 tnzglview"。查下来，它在匿名命名空间里，唯一的调用者 `drawOutline` 是一个没有调用者的 static 函数；Release 构建里它连目标代码都没有生成。照"没有调用者的调试画法删掉"的同一标准，把它和 `drawOutline` 一起删了。要改成搬，只是把这两个函数挪进 `drawutil_gl.cpp`。

`#ifdef _DEBUG` 里的 `OutlineViewerStyle`（colorfx）和 `TVectorImage::drawAutocloses` 照 Q8-D 原样没动：Release 里不编译，Debug 构建的 colorfx 和 tnzcore 编它们时还要 GL。8d 处理无 Qt 构建时再看。

**编译与链接命令的差异**（与 8b 的构建逐条比较，`ninja -t commands` 和 `compile_commands.json`）：
- 搬家的 10 个源文件换了目标（tnzcore 或 colorfx → tnzglview），多了 `tvectorcanvas.cpp`、`drawutil_gl.cpp`；
- tnzcore 的 135 个编译单元少了 `-DQT_OPENGL_LIB`、`-DQT_WIDGETS_LIB`、`-DTGL_EXPORTS`。核心测试从 tnzcore 那里继承的那两个 Qt 定义也没了；树里没有代码看这两个宏。colorfx 少了一个框架搜索路径。
- 链接：
  - tnzcore 少了 OpenGL、GLUT、QtOpenGL、QtWidgets 和 tnzglcanvas；
  - colorfx 少了 OpenGL；
  - 上面那些用户各多一个 `libtnzglview.dylib`；
  - 链接 tnzcore 的测试程序跟着少了那几个传递来的 GL 库。

  其余 30 个链接步骤里，没有别的差异。
- `otool -L`：`libtnzcore.dylib` 和 `libcolorfx.dylib` 不再引用 OpenGL、GLUT、QtOpenGL 和 tnzglcanvas。`nm -u` 列出的 `gl`、`glu`、`CGL` 开头的符号两者都是 0 个。

**新测试和改了的测试。**
- `test_tglentries.cpp`：内置默认的那条换成"tnzcore 自己什么都不装"，两种构建都有。
- `test_gl_seams.cpp`（toonzlib 测试，main 里调了 `install()`）：多一条，`install()` 把三种接缝都装上了，9 个画法也都在；假的钩子用完后放回装好的那个。

**前后对照**（与 8b 的应用包比，开工前复制的）：
- 渲染：golden 通过；单线程和 8 线程的 554 帧、OpenGL 退路单线程的 277 帧都逐字节相同。
- tconverter：4 个用普通样式的参考 pli，转成 17 帧 tif，逐字节相同，标准错误上没有那一行。（参考工程里带 colorfx 样式的 `vec_styles.pli` 两边都转不了：tconverter 从来不注册 colorfx 的样式，"Unknown color style id"，与本步无关。）
- tcomposer 走 GL 退路渲 `vector_styles`，标准错误上也没有那一行。
- GL 前后对照程序（`ab/phase2/glseams/`，8c 那一版加了 `-DGLAB_INSTALL`，会调 `install()`）：19 张栅格与 8b 逐字节相同；8b 的输出又与 8a 的相同。

**人工验收清单**并进了 6a 的那份（8.6 节第 7 到 11 条）。

**结果（x86_64 macOS）。**

- 全部测试：506 个通过。
- 无 Qt 构建：三道检查通过，155 个测试通过，iPad 模拟器里 67 个通过。

**结果（Linux 虚拟机）。** 全部目标编译通过，没有新的缺头或链接顺序问题。清洁剂构建 479 个测试里 478 个通过，失败的仍是 `golden_fx_sweep_font_a`；GL 画布 32 个通过；无 Qt 构建 155 个通过。`readelf -d`：`libtnzcore.so` 和 `libcolorfx.so` 的 NEEDED 里没有 libGL、libGLU、Qt5OpenGL 和 tnzglcanvas，`nm -D` 列出的未定义 GL 符号是 0 个；它们都在 `libtnzglview.so` 上。

### 8.13 第 8d-0 步实际做成的样子（2026-11-18）

矢量图、样式和 colorfx 要进无 Qt 构建，按符号算出的依赖闭包里还带 Qt 的东西，这一步都清掉了：6 个源文件和 3 个头，范围照 8d-0 的两条决定。8d 本体（把它们编进无 Qt 构建和 iPad）在这之后做。

**各处怎么做的。**

- **`tstringid.cpp`**：`QMutex` 换成 `std::mutex`，照第 1 步。
- **`tsystempd.cpp`**：
  - Linux 回收站那段（`QStorageInfo`、`QFile`、`QTextStream`、`QUrl`）原样搬进 `tsystem_qt.cpp`。照第 6c 步按 `QT_CORE_LIB` 选内置实现的做法：Qt 构建调它；无 Qt 构建在 Linux 上没有回收站，经 `outputDebug` 说一句就返回。
  - `outputDebug` 的 `qDebug("%s")` 换成往标准错误写一行，与第 4 步的 `qWarning` 一样（第 11 节那条风险同样适用）。
- **`timagecache.cpp`**：
  - "缓存开着没有"原来是 `Imp` 的一个 `QThreadStorage<bool *>`，现在是文件里的一个 `thread_local std::unique_ptr<bool>`。`TImageCache` 是单例，"每个对象每个线程一个值"就等于"每个线程一个值"。
  - 两者都在线程结束时释放这个值，时机不同：`QThreadStorage` 在 `QThread` 收尾时，`thread_local` 在线程的局部存储析构时。值只是一个 `bool`，谁也看不出差别。没有设过的线程照旧当作开着。
  - 互斥锁本来就是 `TThread::Mutex`（文件里剩下的 `QMutexLocker` 在 `#ifdef LEVO` 里）。
  - 三个 `QString` 重载照 Q2 搬进 `timagecache_qt.cpp`，经 std 版本做原来的事。
- **`tcodec.cpp`**：
  - LZO 改成注入的服务（`tcodec.h` 的 `TLzoService`、`setLzoService`），照 `showDocument`。唯一的切换点；传 `nullptr` 回到内置的。
  - Qt 构建内置的那个就是原来的代码，搬进 `tcodec_qt.cpp`：经 `QProcess` 调可执行文件旁边的 `lzocompress`、`lzodecompress`，Windows 上 ToonzPreview 的路径也照旧。
  - 无 Qt 构建没有内置的。编解码器抛 `TException("LZO compression/decompression is not available in this build")`，不会静默给空数据。第 13 步直接接上链接库的实现。
  - 一个 Linux 才看得出的问题：这个文件失去了 Qt 头间接带来的 `<cstring>`，补上了（第 6a 步起的老问题）。
- **`tlevel_io`**：
  - 格式表照第 5 步 `timage_io` 的先例以 `std::string` 为键。`define`、`getSupportedFormats` 有 std、`QString`、`const char *` 三种，`QString` 的在 `tlevel_io_qt.cpp`。
  - 扩展名都是 ASCII，`std::string` 键的顺序与 `QString` 键相同；用测试钉住：列出的格式按 `QString` 排序后不变。
  - `getCreator()` 照 Q7-A 改成非虚的 Qt 包装，包着新的虚函数 `getCreatorW()`；image 库的 tzl、pli、tzm 三个读取器改成重写它。
  - 写入器的 `m_creator` 换成 `std::wstring m_creatorW`，`setCreator(QString)` 和受保护的 `creator()` 是 Qt 一侧的访问函数。tzl、pli、tzm 三个写入器改成调它们；tzl 读出旧文件创建者的两处，经一个局部的 `QString` 照旧读。
  - `renumberFids()` 原来用 `QDir`、`QFile`，现在照 6c 的做法按构建选：
    - Qt 构建用原来的代码，搬进 `tlevel_io_qt.cpp`；
    - 无 Qt 构建用 std 写的一份，由 6a 照 Qt 写的读目录、改名、删除和 8a 的 `matchesWildcardsAsQDir` 拼成；
    - Qt 构建里有差分测试比较两者。
  - 没有用到的 `tmsgcore.h` 包含去掉了。
- **`tcontenthistory`**：
  - 成员换成 std：记录是"本地时间那一分钟的开始"的毫秒时刻，冻结的历史是 `std::wstring`。`serialize`、`deserialize` 是 Qt 包装，加了 `serializeW`、`deserializeW`。
  - 历史文本照原来的规则拼：长度按 UTF-16 单元数，补空格、截断都一样。截断落在代理对中间时，`QString::truncate` 留下前一半，这里也留下前一半（一个孤立的代理）；`QString::fromStdWString` 原样还回去，Qt 写出 `?`。
  - 用户名和机器名照旧从环境变量 `USERNAME`、`COMPUTERNAME` 读，后出现的为准。
  - 日期：Qt 构建照旧由 `QDateTime::toString("dd MMM yy   hh:mm")` 写，月份名是系统语言环境的。无 Qt 构建用 C 库的本地时间和英文月份名，所以只在英文（或 C）语言环境下与 Qt 相同；这是无 Qt 构建的已知差别，写进第 11 节。
- **`tproperty`**：
  - 名字都换成 `std::wstring`，两种构建的类布局相同。`getQStringName`、`setQStringName`、`addValueWithUIName`、`addValue(…, QString)`、`setItemUIName` 照 Q2 对前向声明的 `QString` 声明，定义在 `tproperty_qt.cpp`，约 480 个调用点不动。
  - 有默认实参的那几个拆成了重载（默认实参要完整的类型）。
  - `TEnumProperty::Item` 加了 Qt 一侧的 `getUIName()`、`getIconName()`。直接读字段的 18 行界面代码改成调它们：`tnztools/tooloptionscontrols.cpp` 6 行，`toonz/toolpropertiespanel.cpp` 11 行，`toonz/formatsettingspopups.cpp` 1 行。
  - 空名字还回一个空的 `QString()`（null）：默认构造的 `Item` 原来就是那样，对照导出里分得出来。
- **`tthread.h`**：只有 `tsimplecolorstyles.cpp` 包含它，那里已经用不着了。包含在 8d 本体里去掉。
- **image 库两个文件**：`tzl/tiio_tzl.h`、`tzp/tiio_plt.cpp` 原来经 `tproperty.h` 拿到 `QString`，现在自己包含。

**查过的语义差别。**

- **按名字的查找**：没有以 `QString` 名字作键、依赖 `QString` 比较语义的查找。树里只有 `tooloptions.cpp` 两处 `getQStringName() != ""`，经包装还是 `QString` 的比较。
- **名字的来回转换**：`QString` 进、`std::wstring` 存、`QString` 出，对合法的 UTF-16 逐字不变；界面文字都来自 `tr()`。
- **2037 年以后的日期**：Qt 5 借一个日历相同的早年份来算夏令时，C 库按时区自己的规则算，两边可能差一小时。内容历史只记"现在"，碰不到。

**新测试。** 都在 Qt 构建里，共 14 个：
- `test_tcontenthistory_qt.cpp`，对 `legacy/legacy_tcontenthistory.cpp`（原来的代码，只改了名字）：
  - 日期：UTC、东八区、纽约、悉尼和本机时区，各约两万个时刻（含两地夏令时切换前后），Qt 构建的写法与 `QDateTime` 相同；英文语言环境下无 Qt 的写法也相同。
  - "现在"：比 `QDateTime` 的算法晚不到一秒，落在同一分钟里。
  - 历史文本：五个时区各 200 轮随机编辑（带字母的帧、区间、冻结、读回的旧历史），用户名和机器名故意在代理对中间截断，新旧逐字相同。旧类自己读钟，一毫秒可能正好在新旧两次编辑之间结束；这一轮就用同样的编辑重做，真有差别会每次都不同。
- `test_tlevel_io_qt.cpp`：60 个随机文件夹（两种帧号写法、别的关卡、大小写不同的、隐藏的、留下的临时文件、像帧的文件夹、链接和断链），随机的改号表（互换、改到已有的帧、删除），Qt 版和 std 版留下的文件夹逐项相同。
- `test_tproperty.cpp`、`test_tproperty_qt.cpp`：名字和枚举项的存取与来回转换；`assignUINames`；没设过的是 null。
- `test_timagecache.cpp`：开关按线程。
- toonzlib 的 `test_d0_io.cpp`：
  - 格式的列出顺序；
  - LZO 服务的转交、失败时抛出、恢复内置；
  - 内置的经辅助程序压缩、解压一张栅格。

**前后对照**（与 8c 的应用包比，工具在 `toonz/build-phase1/ab/phase2/d0/`，不进版本库，`README.md` 写了怎么重跑）：
- **属性的界面文字**：一个程序装上每种语言七个模块的 .qm，导出全部工具和全部写出格式的每个属性：名字、界面名、取值、枚举项的界面名和图标名，以及是不是 null。12 组（无翻译加 11 种语言）共 9,790 行，逐字相同。
- **LZO 的 tlv**：参考工程的两个 tlv 都是 TLV14，帧是 LZO 压缩的。三个时区下，读出全部 pli 和 tlv（创建者、历史、帧的像素），再把 tlv 带着创建者和内容历史写一遍（LZO，加上 `.hst`），20 个输出逐字节相同。用户名和机器名用的是很长的非 ASCII 名字，截断落在 15 个和 12 个 UTF-16 单元上。
- **场景**：参考工程的 27 个场景读进再存出，输出设置是 `TPropertyGroup`，另有 8 个写出格式的属性文件。场景的存档在同一个构建里也不确定：层级列表的顺序、fx 的编号、哪一次引用写出完整的 fx，都随指针变。8c 自己跑两次就有 52 处这类差别。所以比较的范围是 fx 图和层级列表以外的部分，那里三个时区下全部相同；属性文件全部相同。
- **渲染**：golden 通过。单线程和 8 线程的 554 帧、OpenGL 退路单线程的 277 帧，逐字节相同。
- **Linux 虚拟机**：同步分支之前先存下 8c 的清洁剂构建，两边各用 8 线程渲 28 个场景。ASan 和 UBSan 设成遇到第一个报告就停，两边都没有报告，277 帧逐字节相同。ThreadSanitizer 现成的只有无 Qt 核心测试那个构建，图像缓存还不在里面，等 8d 本体以后顺带跑。

**结果（x86_64 macOS）。**

- 全部测试：520 个通过（新增 14 个）。
- 无 Qt 构建：三道检查通过，155 个测试通过，iPad 模拟器里 67 个通过。

**结果（Linux 虚拟机）。**
- 第一次编译，`tcodec.cpp` 缺 `<cstring>`，补上了。
- LeakSanitizer 报了 Qt 自己的一处一次性分配：`QString::localeAwareCompare` 留着的排序器。新的改号测试是第一个在 Linux 上比较帧字母的，把它带了出来，已加进 `tests/lsan.supp`。
- 最后：全部目标编译通过；清洁剂构建 493 个测试里 492 个通过，失败的仍是 `golden_fx_sweep_font_a`；GL 画布 32 个通过；无 Qt 构建 155 个通过。

### 8.14 第 8d 步实际做成的样子（2026-11-19）

矢量图、样式和 colorfx 进了无 Qt 构建，也在 iPad 模拟器里跑。四条决定照办：pli 读取器、CI 的 iPad 那一步、toonzlib 的样式、三处数据竞争。

**无 Qt 构建里现在有什么。**
- **tnzcore**：77 个源文件，Qt 构建是 132 个。8d-0 清掉 Qt 的那批，加上矢量图、样式、区域计算、调色板，加上栅格、图像和它们的读写框架、图像缓存和编解码器、栅格运算。链接 tnzcanvas、tnzskia（栅格化矢量图，与 Qt 构建相同）、zlib、lz4，macOS 上还链接 CoreFoundation 和 CoreServices（回收站）。
- **colorfx**：全部样式，只链接 tnzcore 和 tnzcanvas；它从来不用 tnzbase 的符号。
- **image**：照决定先只有 pli 读写器（`IMAGE_NOQT_SOURCES`）。注册函数 `initPliIo()`，`initImageIo()` 在原来注册 pli 的位置调它，Qt 构建里的注册顺序不变。
- **不在里面的**：tnzglcanvas、tnzglview 和别的图像格式；toonzlib 以上。

**pli 代码去掉 Qt**，用的都是现成的先例：
- 帧字母的字节用 `std::string`，经 `::to_wstring`、`::to_string` 转换。第 3 步的 UTF-8 编解码与 `QString::fromUtf8`、`toUtf8` 相同：跳过 BOM，孤立代理写成 `?`。
- 拖尾参数用 8a 的 `toIntAsQString`、`toDoubleAsQString`、`toStringAsQString`。
- 历史和创建者用 8d-0 的 `serializeW`、`getCreatorW`。
- `ParsedPli::getCreator`、`setCreator` 照 Q2 做成 `pli_io_qt.cpp` 里的包装。

**为了在 iPad 上编译而改的。** 这些对桌面行为没有影响：
- 四个矢量源文件和 `tsimplecolorstyles.cpp` 去掉了已经用不着的 `tgl.h`、`tvectorgl.h`、`tthread.h`；`tiio.cpp` 去掉了用不着的 `tiio_jpg.h`（它带 `<QCoreApplication>` 和 libjpeg）。
- `drawutil.h` 不再包含 `tgl.h`：`TextureInfoForGL` 的 GL 类型写成它们本来的类型（`int`、`unsigned int`、`const void *`），`drawutil_gl.cpp` 用 `static_assert` 钉住两者相同，类布局不变。原来经它拿到 `tgl.h` 的四个工具文件（tnztools 的弯曲、磁铁、栅格选择、矢量选择）自己包含。
- `tsystempd.cpp`：iOS 上没有 Carbon，也没有回收站。
- colorfx 里那几个用不着的 Qt 包含去掉了（8d-0 的决定）。
- Linux 上 `pli_io.cpp`、`tiio_pli.cpp` 失去了 Qt 间接带来的 `<cstring>`，补上了（单独一个提交）。

**新测试** `tests/core/test_vector_noqt.cpp`：
- 同一个文件编进无 Qt 构建的核心测试（也在 iPad 模拟器里跑）和 Qt 构建的 toonzlib 测试（那里 main 初始化 image 和 colorfx，与程序一样），各自对同一张校验和表比较。两边都通过，就是逐字节相同。
- 校验和表按平台分开：macOS 和 iPad 模拟器用 skia-pack，Linux 用 vcpkg 的 Skia，两边是不同的 Skia 构建，画面不同。118 项里画面的 68 项不同，矢量数据和写出的文件那 50 项两平台相同。没有表的平台跳过这组测试。
- 内容：
  - 交叉的闭合笔画算出区域，用纯色和 colorfx 的区域样式填充；纯色、点划线中心线和 colorfx 的描边样式；Skia 画出来，两种缩放。
  - 读参考工程全部 pli：调色板、帧列表、每帧的笔画和区域，每一帧按包围盒画出来。Qt 构建读 15 个，无 Qt 构建读 14 个。
  - 带创建者把 pli 写回，写出的字节，再读回创建者。
  - 两种图案笔刷，源文件是 pli，栅格图案的帧也由 Skia 栅格化。画在笔画上，也画图标。
  - 8 个线程同时读、画全部 pli，结果与单线程逐项相同。
  - 只在无 Qt 构建里：读 `vec_styles.pli` 抛出 "Unknown color style id"，既不崩溃，也不交回半成品。toonzlib 的样式进来以后，这条要改成"能读出"。
  - 表里的每一项都检查到了。
- 同一个测试换上 8d-0 的库（`ab/phase2/d8/`）：118 项全部与 8d 相同，所以 Qt 构建本身没有变。

**ThreadSanitizer。**
- 无 Qt 核心测试的 TSan 构建（Debug），配上了 Skia。跑法写进了 `tests/README.md` 的 Sanitizers 一节。
- 8 线程那个测试报出 17 处、三个地方的数据竞争，都是老代码，修法和摘要在单独的提交里（照决定，这是第 9 步提前的一小块）：
  - 笔画 id 的全局计数器：真问题，已知问题第 33 条，已修复，发布说明草稿里提了；
  - `TBigMemoryManager::instance()` 和它的分配统计；
  - `getInstanceCounter` 的双重检查。
- 修完以后，79 个测试在 TSan 下没有报告，多线程的那两个重复跑五遍也没有。
- Debug 构建里矢量测试的校验和也与表相同。

**LeakSanitizer 的抑制规则**：8d-0 新加的那一条（`QString::localeAwareCompare_helper`）像别的条目一样带注释：不是我们的代码，是 Qt 留着排序器的一次性分配。

**CI**（只改了文件，见 8.15 的未验证事项）：
- `skia_pack.sh` 多认 `iosSim-x64`。它的摘要取自 2026-11-08 与 x64 包一起下载的那个文件；x64 包的摘要与 GitHub 记录的相同，这个包没有与 GitHub 的记录核对过。
- macOS-noqt 作业缓存并下载 iOS 模拟器的包。
- `noqt_test.sh` 多一个 iOS Skia 目录的参数，Qt 检查也查 colorfx 和 image。
- `run_ios_noqt.sh` 读三张清单，加上 tnzcanvas、tnzskia、colorfx、从源码编的 LZ4，宏照桌面的苹果构建（`MACOSX`、`i386`、`__MACOS__`）。原来没有 `i386`，像素通道的顺序就不对；那时的测试不画画，所以没有暴露。boost 的头最后才搜索。

**结果（x86_64 macOS）。**
- Qt 构建：526 个测试通过，golden 在内。
- 与 8d-0 的应用包对照：单线程和 8 线程的 554 帧、OpenGL 退路单线程的 277 帧逐字节相同。这是改竞争之后的最终状态。
- 无 Qt 构建：三道检查通过（13 个库文件都不链接 Qt），166 个测试通过。
- iPad 模拟器：78 个测试通过。
- ThreadSanitizer：79 个通过，没有报告。

**结果（Linux 虚拟机）。**
- 全部目标编译通过（补了上面那两个 `<cstring>`）。
- 清洁剂构建 499 个测试里 498 个通过，失败的仍是 `golden_fx_sweep_font_a`；新的矢量测试和 8 线程测试在 ASan、UBSan、LSan 下都通过。
- GL 画布 32 个通过；无 Qt 构建 166 个通过，用的是 Linux 的表。
- 8 线程渲 28 个场景：与 8d-0 的清洁剂构建对照，ASan、UBSan 设成遇到报告就停，没有报告，277 帧逐字节相同。

### 8.15 第 8 步整体（2026-11-19）

第 8 步拆成 8a、8b、8c、8d-0、8d，见 8.10 到 8.14 节。合起来：
- 核心对 Qt 的依赖只剩下 `*_qt.cpp` 里的包装。
- 桌面 GL 在 tnzglview 里，tnzcore 和 colorfx 不链接 GL。
- 矢量图、样式、colorfx 和 pli 读写器不带 Qt，也不带 GL，在 macOS、Linux 和 iPad 模拟器上由 Skia 画出来，与桌面的 Qt 构建逐字节相同。
- 每一子步都验证过默认路径不变：golden、1 和 8 线程渲染、GL 退路单线程、存进文件的字节和界面文字的对照、Linux 虚拟机全量构建加清洁剂。

**人工验收清单**在 8.6 节，一份：6a 的第 1 到 6 条，加 8c 并进去的第 7 到 11 条（viewer、图标、工具的中心线、tnzext 的 GL、tcleanup 和 tconverter）。到里程碑时交给所有者。第 8 步其余的子步没有新的人工项：8d 的变化都在测试和对照里。

**iPad 待办**（核心在 iPad 上跑到时要处理的，第 8 步记下的）：
- 没有装 `TGLView` 的程序，GL 入口什么都不画（Q8-C）。iPad 上图标和图案笔刷都由 Skia 画，与桌面的 GL 路径不同；8d 的测试证明它与桌面的 Skia 路径相同。
- toonzlib 的样式（纹理、MyPaint、两种图像样式）随 toonzlib 的迁移进来。在那之前，读不了用这些样式的关卡：读的时候抛出 "Unknown color style id"，有测试钉住。
- 内容历史里的日期只有英文月份名（8d-0）。桌面按系统语言环境写；iPad 壳要不要按语言环境写，那时再定。
- 新建调色板的页名是英文的 "colors"，要由壳注入数据翻译服务（第 7 步）。
- 没有回收站，没有 LZO（tlv 的帧读不了，LZO 服务要由第 13 步接上链接库的实现）。
- 只测过模拟器（x86_64）；真机（arm64）只编译链接过。

**未验证事项。**
- **CI 没有跑过第 8 步。** workflow 只改了文件，分支没有推送。推送后第一件事是看 macOS-noqt 作业：
  - GitHub 的 macos-15-intel runner 上找不找得到 iPad 模拟器的运行时（本机只装了 iOS 15.5 的）；
  - iOS 模拟器的 Skia 包能不能下载，摘要对不对。
- **Windows 是盲改的。** Windows 的 workflow 只在推送到 master 或 PR 时自动跑，所以加了手动触发（`workflow_dispatch`），推送后手动跑一次。第 8 步改了只在 Windows 上编的 toonzpreview（`TGLView::install()`）和 tnzglview 的 Windows 链接，导出宏在 tnzglview 里也没有按 Windows 的需要细分（8c）。没有 Windows 的表，矢量测试在那里跳过。
- **桌面应用只能人工验收**：viewer 的绘制经接缝走，8.6 节的清单还没有人做过。

---

### 8.16 第 1 到 8 步合入之后的第一轮 CI（2026-11-19）

第 1 到 8 步整理成 32 个提交，与工具方案的分支一起合进 `nexttoonz/phase0`（合并提交 `c2dc224d6`）并推送。Linux 的三个作业、Sanitizers、macOS x86_64、macOS-noqt（iPad 模拟器里 78 个测试）都通过。没通过的三处：

**Fixture Guard（run 44652941799）。** 它对这次推送里的 `69e16bef7`（8d 的代码提交）报错：这个提交改了 `test_vector_noqt_hashes_linux.inc` 和 `test_vector_noqt_hashes_macos.inc`，提交信息里没有 `Fixtures-Update:`。这个提交早于夹具保护的规则（规则和它在同一次合并里进来），所以只此一次，不改写历史。手动触发的整树检查（run 44653075161）通过。以后改这些表的提交都带 `Fixtures-Update:` 尾注，本地由 `tools/setup-hooks.sh` 装的钩子先查。

**Windows（run 44652960368，手动触发，Phase 1 之后第一次跑）。** 停在三处，都是本机编不到的：
- `tnzglcanvas.dll` 缺 12 个 GLU 符号：MSVC 构建只设了 `GL_LIB`（`opengl32.lib`），没有 `GLU_LIB`，Phase 1 的这个库在 Windows 上没链 GLU。照 tnzglview 的做法按名字链 `glu32.lib`。
- `tstring.cpp`：8a 不带 Qt 的 `toDouble` 用了 `newlocale`、`LC_ALL_MASK`、`strtod_l`，Windows 的 C 库叫 `_create_locale(LC_ALL, "C")` 和 `_strtod_l`。接受哪些文本由前面的检查决定，不变。
- `uncpath.cpp`（只在 Windows 上编）：`QString` 原来经 `tfilepath.h` 拿到完整定义，第 4 步以后那里只有声明，改成自己包含 `<QString>`。

这三处修好之后，Windows 才会往下编到 tnzcore 以后的库，那里可能还有同类问题，要再跑一次才知道。

第二次手动触发（run 44660826672，在 `459d0bac8` 上）证实了这一点：tnzcore 编过了，停在两处链接错误：
- `tnzglview.dll` 缺 7 个符号。这就是 8c 记下的风险：导出宏没有按 Windows 的需要分开。tnzglview 为了导出搬进来的 GL 函数，定义了 `TVRENDER_EXPORTS` 和 `TVECTORIMAGE_EXPORTS`。可是 `tstroke.h`、`tpalette.h`、`tsimplecolorstyles.h` 里 tnzcore 自己的类也用这两个宏，MSVC 于是把它们当成 tnzglview 自己的：函数还能经导入库找到，静态数据（`TStroke::m_classCode`、`TPalette::m_declaration`、两个图案笔刷的 `m_rootDir`）找不到。修法：
  - tnzglview 只定义 `TNZGLVIEW_EXPORTS` 和 `TGL_EXPORTS`（`tgl.h` 那几个头声明的东西只有 tnzglview 定义）；
  - 整个搬进来的 `tofflinegl.h`、`tvectorgl.h`、`tglcanvasbridge.h` 改用 `TNZGLVIEW_EXPORTS`；
  - `drawutil.h` 里搬进来的 `prepareTexture` 和 `drawStrokeCenterline` 用一个新宏 `DVAPI_GLVIEW`，其余仍归 tnzcore。
  - 依据是 macOS 上 `libtnzglview.dylib` 导出的全部符号：每个都对到它的头，tnzcore 和 colorfx 里没有一个是这些头声明的。
- 同一个库里还缺两个 colorfx 的成员（`TSketchStrokeStyle`、`FlowLineStrokeStyle` 的 `drawStrokeOnCanvas`）：colorfx 不导出它的样式类，`colorfx_gl.cpp` 却按具体类直接调用。改成经 tnzcore 的基类 `TSimpleStrokeStyle` 的虚函数调用，不需要导出。
- `nexttoonz_appinfo_probe.exe`（6c 的测试程序）缺 `TSystemQtSemantics::applicationFilePath` 和 `applicationDirPath`：这个内部头的函数 tnzcore 从来没有导出。现在按 `TSYSTEM_EXPORTS` 导出（新宏 `DVAPI_QTSEMANTICS`），头仍然只在核心和测试里用。

在 macOS 和 Linux 上这些宏都是空的，所以这一轮只能证明那两个平台上没有变化；Windows 链不链得过，要等下一次运行。

第四次（run 44663830536 的第二次尝试，在 `d4c77eae8` 上；第一次尝试是 Chocolatey 的 504，与代码无关）编到了测试和 tnzext：
- tnzext.dll 缺 GLU 的 8 个符号（`meshbuilder.cpp` 的细分器）。Windows 上原来是 tnzcore 把 `opengl32.lib` 和 `glu32.lib` 带给所有库的，8c 把 OpenGL 从 tnzcore 拿走以后就没有了。逐个库查了直接调用 GL 和 GLU 的地方：`opengl32` 各库自己都链了；直接用 GLU 的只有 tnzext 和应用（`sceneviewer.cpp`），tnzglview 和 tnzglcanvas 前两轮已经补上。这一轮给 tnzext 和应用都按名字链 `glu32.lib`，应用这一处还没报错，是同一个原因提前补的。
- Qt 5.15.2 的 `qlist.h:915` 用 `stdext::make_checked_array_iterator`。运行器的 Visual Studio 18（MSVC 14.52）的标准库没有它了，Qt 后来的 5.15 版本才加上 `_MSC_VER < 1938` 的条件。触发它的只有两个测试：`test_tlevel_io_qt.cpp` 和 `test_ttranslatabletext_qt.cpp`，它们用 `==` 比较两个 `QStringList`，树里别的代码没有实例化这个比较。上游用同样的 VS 18，是绿的，原因也在这里。在 CMake 里定义宏没有用，Qt 5.15.2 在 MSVC 下无条件地定义它；也不改 thirdparty 里的 Qt 头。所以两个测试改成比较拼接后的文本。
- 测试里的 POSIX：
  - `test_tfilepath_qt.cpp` 屏蔽标准错误时用的 `dup`、`dup2`、`open`、`close`，Windows 上从 `<io.h>` 来，空设备是 `NUL`；
  - `test_tcontenthistory_qt.cpp` 的 `setenv`、`unsetenv` 在 Windows 上用 `_putenv_s`；
  - `test_tsmartpointer.cpp` 的 lambda 改为默认捕获（MSVC 不隐式捕获 `rounds`）。
- `legacy_tsystem.cpp`（旧 tsystem 的副本）的 Windows 分支本来就对不上 UNICODE 构建。它只给 `test_tsystem_qt.cpp` 用，而后者整个在 `#ifndef _WIN32` 里。所以在 Windows 上不编它：**这些与旧 tsystem 比较的差分测试只在 macOS 和 Linux 上编译。**

照所有者的时间盒，这是 Windows 的最后一轮。下一次运行如果仍然是红的，剩下的问题记在这里和计划的待定事项里，不再追：Windows 不是产品目标，它的 CI 不阻塞。

第五次（run 44670363369，在 `78ed1ce07` 上）：**所有的库和程序都编过并链过了**，剩下的全在测试里，三类：
- `nexttoonz_core_tests.exe` 链接缺 `renumberFidsWithQt` 和 `renumberFidsWithoutQt`：`test_tlevel_io_qt.cpp` 比较它们的测试体没有按 `_WIN32` 排除，它们自己在 Windows 上却不编；
- toonzlib 测试的 `test_d0_io.cpp` 里还有一处 `QStringList` 的 `==`，又碰到 `qlist.h` 的 `stdext`；
- `test_style_canvas.cpp:175`：局部变量叫 `small`，Windows 的 `rpcndr.h` 有 `#define small char`，那一行就成了 `std::vector<std::string> char = …`；后面 `count` 的报错都是它引起的。

**决定（2026-11-20，所有者委托主会话拍板）：Windows 的 CI 不编测试。** 照时间盒不再逐个追。Windows 的 CI 回到它在计划里的角色，只回答"产品能不能在 Windows 上构建"：`workflow_windows.yml` 配置时传 `-DWITH_TESTS=OFF`（`WITH_TESTS` 是我们自己的选项，关掉以后不加 `tests` 目录；树里别的地方不依赖它）。不再改测试源码。上面三类剩余项记在计划的待定事项"Windows 上的测试可移植性"里；改完这三类，后面可能还有没编到的测试。Windows 不是产品目标，它的 CI 不阻塞。

**macOS arm64（run 44652941765）。** toonzlib 测试里 `VectorWithoutQt` 的四个测试失败：矢量测试的 macOS 校验和表是 x86_64 上生成的。日志里有每一项的实际值，项名都对得上：
- 不同的有 70 项：68 个画面项（Linux 与 x86_64 本来就不同的那些，一个不少），加上 `regions/computed`、`regions/filled` 两个数据项；
- 其余 48 个数据项与 x86_64 相同，包括每个 pli 的读取、写出的文件和调色板。

那两项哈希的是测试自己生成的三个圈（`cx + r*cos(a)`）求出的区域、再填色以后的笔画控制点和区域，取的是 double 的原始位。arm64 的 clang 默认把乘加合成一条 FMA，结果的末位会不同。本机在测试的生成函数里用 `std::fma` 模拟，哈希变了，但没有变成 arm64 的值，可见库内部的浮点运算也有同样的差别。本机没有 arm64，证明不了区域的结构相同，所以照下面的决定拆开：
- 结构项 `regions/computed/structure`、`regions/filled/structure`：只含整数，即笔画数、每条笔画的样式和点数、区域数，以及每个区域（含子区域）的样式和围成它的边（哪条笔画、哪个方向、什么样式）。放在所有平台共用的新表 `test_vector_noqt_hashes_common.inc` 里，三个平台必须相同。本机用 `std::fma` 扰动坐标时它们不变，`exact` 两项则变了。
- 原来的两项改名为 `regions/computed/exact`、`regions/filled/exact`，按平台，值不变。Linux 和 macOS x86_64 的这两项本来就相同，差别只在处理器。
- 新表 `test_vector_noqt_hashes_macos_arm64.inc`：那 70 项取 CI 日志里的实际值，其余与 x86_64 相同；结构项按定义用 x86_64 本机的值。下一轮 CI 里，如果 arm64 的结构项不同，测试会失败并打出实际值，那就是真 bug。
- 测试多了一个开关 `NEXTTOONZ_HASHES_FILE=<文件>`：每个测试把它的校验和按表的格式追加进这个文件。macOS 的 workflow 在测试失败时把它作为产物上传，以后新平台的表可以直接从 CI 拿。说明在 `tests/README.md` 里。
- iPad 模拟器在 Apple 芯片的 Mac 上（arm64）会用到 arm64 的表，但那里用的是 iOS 的 Skia 包，没有验证过。CI 的 iPad 作业在 Intel 运行器上，用 x86_64 的表。

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

**决定（2026-11-18，所有者委托主会话拍板）。** 第 4 步之后又有两件事要定：第 6 节补充里 Q2 落实方式的改动，和新的 Q9。所有者的原话是"我没什么大意见，你拍板吧"，把这类设计层面的取舍交给了主会话。主会话的决定：

- **Q2 的落实方式认可第 4 步的做法**：带 Qt 类型的成员照样在核心头里声明（前置声明 `class QString;` 等），定义放进只在 Qt 构建里编译的 `*_qt.cpp`。理由：调用点不用改；类在每个编译单元里都一样，没有 ODR 问题；不带 Qt 的核心代码调用它们会在编译时失败。第 20 步在 Windows 上的 DLL 导入问题只影响 Windows，留在第 11 节的风险里，到那一步再处理。以后各步遇到同类接口照此做，不再另问。
- **Q9 取 (b)**：macOS 和 iPadOS 用 CoreFoundation（第 4 步已做，顺序不变）；Windows 自己调用 `CompareStringEx`；Linux 用 C 库的排序，不引入 ICU。理由：Apple 平台是首要目标，已与 Qt 一致；Linux 只是 CI 的测试床，不值得为帧字母的顺序多一个依赖。**接受的差异**：核心不再链接 Qt 之后，Linux 上非 ASCII 字母（以及大小写混合时）的帧顺序可能与今天 Qt 经 ICU 给出的顺序不同。做到第 20 步时要用差分测试量出 Linux 上与 Qt/ICU 顺序的差别并记下来，对 golden 或已有测试有影响就报告。帧字母的顺序由 `test_tfilepath.cpp` 的 `TFrameId.LetterOrderIsPinned` 钉住（ASCII 字母、大小写混合、多字母后缀；各平台共同的部分在所有平台上检查，Apple 的完整顺序只在 Apple 上检查）。

以后遇到同类的设计取舍，仍先停下来报告主会话，由主会话决定；涉及外观、产品范围、推送与合并的，仍由所有者本人决定。

**第 6 步的决定（2026-11-18，所有者委托主会话拍板）。** 第 6 步开工前报告了四个设计取舍，主会话都按本稿的建议定了，并加了要求：

1. **拆成 6a、6b、6c**，每个子步单独提交、单独验证：6a 是 `TFileStatus`、文件操作、文件时间和目录列表，换成 `std::filesystem`、`std::chrono`；6b 是 `tenv` 的设置文件；6c 是应用与进程信息。`TFileStatus::getPermissions()` 原来返回 `QFile::Permissions`（模板类型，不能前置声明），改成核心自己的标志类型，三个调用点一起改，Qt 一侧需要时按 Q2 的做法提供转换。
2. **目录列表的顺序按 Q9 的规则**：Apple 上与 Qt 逐一相同，用差分测试证明；Linux 上用 C 库的排序，接受差异。另外要先查一遍 `readDirectory_DirItems` 的调用方，确认没有持久化的输出或场景加载的结果依赖列表的顺序（序列帧的枚举是不是最后进了以 `TFrameId` 为键的 map、关卡扫描、调色板和库目录的浏览），结论写进 6a 的"做成的样子"；有依赖的地方要显式排序，不靠列表的顺序。
3. **`SystemVar.ini` 取 (a)**：把 Q4 的注入式键值存储提前到 6b 引入；桌面实现放在只在 Qt 构建里编译的文件里，文件逐字节不变（用改动前后写出的 ini 对照）；接口就是第 15 步要用的那个，第 15 步直接复用，Q4 的"要跟进"标记不变。
4. **应用与进程信息**按本稿列的四条：可执行文件的路径、环境变量、主机名、临时目录用平台调用，在 Qt 构建里与 Qt 的返回值逐字对照（macOS 上 `applicationDirPath` 在 .app 包里返回 `Contents/MacOS`，`TEnv` 由它推出 stuff 目录，符号链接解析与否也要一致）；`QUuid` 只要格式一致（带花括号的 v4 形式），加一个格式测试；`TSystem::showDocument` 做成注入的服务，桌面用 Qt 实现；`getSystemValue` 原样移到 Qt 一侧。

**6a 开工时的决定（2026-11-18，所有者委托主会话拍板）：`TFileStatus` 的提前退出取 (a)，去掉。** 原来的构造函数在还没有 `QCoreApplication` 时把任何路径都报告成不存在（上游 `4600efb66` 为了避开 QFileInfo 在静态初始化期的崩溃加的）。选项是 (a) 去掉，核心永远报告磁盘上的真实状态；(b) 由 Qt 一侧设一个"应用已存在"的钩子来保留这个行为。理由：那是给 QFileInfo 打的补丁，`std::filesystem` 没有这个问题；(b) 会把 Qt 的补丁带进核心，还要求无 Qt 构建和 iPad 在引擎启动时设钩子，违背去 Qt 的目的。附带要求：把 `QCoreApplication` 存在之前的调用逐个列进 6a 的"做成的样子"，并确认两次 `setWorkingDirectory` 之间有没有代码读到第一次的结果；加一个单元测试，证明没有 `QCoreApplication` 时 `TFileStatus` 对存在和不存在的路径都报告真实状态；验证 golden、多线程对照、tcomposer 初始化 stuff 目录的前后对照（目录内容和 `SystemVar.ini` 逐字节比较）；应用自身的启动路径（便携模式、首次运行、stuff 目录缺失时的创建）列成人工验收清单；这是 Phase 2 第一个会改变运行时行为的改动，风险节加一条，Phase 2 的发布说明到时也要提。

**6a 做完后的决定（2026-11-18，所有者委托主会话拍板）。** 6a 报告了两处与第 6 步决定字面不同的做法，主会话都认可：

1. **用 POSIX 加 CoreFoundation，不用 `std::filesystem`。** 理由：逐字节相同是这一阶段的验收标准，`std::filesystem` 做不到就不用；POSIX 和 CoreFoundation 在 iPadOS 上都可用（`clonefile` 也在），与首要目标一致。第 5 节的那一行已照此改写，后面的步骤按改写后的做。
2. **文件时间的 `QDateTime` 成员留在 Qt 一侧**，按 Q2 的做法；第一个不带 Qt 的调用者出现时再加 `std::chrono` 的版本。

另外：tconverter 在 macOS（和 Windows）上从不能用到能用这一条，Phase 2 的发布说明要单列；已知问题第 29、30 条记下了。

**6b 开工时的决定（2026-11-18，所有者委托主会话拍板）：键值存储的形状。** 6b 要引入的键值存储接口第 15 步直接复用，而 `Preferences` 存的值有 bool、int、double、字符串、颜色、尺寸、map，还有 group 和数组，QSettings 在 INI 里按类型写。主会话的决定：

1. **形状取 (b)**：6b 只定骨架（抽象类、工厂与注入、桌面实现的位置）和字符串的读写；别的类型到第 15 步按 `Preferences` 的实际需要加。约束：键的表示和 group、数组的路径写法现在就定，并且与 QSettings 的约定一一对应（`group/key`；数组是 `name/size` 和从 1 起的 `name/1/key`），因为 `SystemVar.ini` 和 `preferences.ini` 都要逐字节不变；接口给"按类型读写"留出扩展的位置，但不实现，也不留存根。
2. **注入**：核心声明工厂 `TKeyValueStore::open(路径)`；Qt 构建里 tnzcore 的 `*_qt.cpp` 提供 QSettings 的实现作为默认，各程序的 `main` 不改；无 Qt 构建的默认是不读也不写的空实现，iPad 壳以后自己注入。默认实现的切换点只有一处（一个可设置的工厂函数），并有一个单元测试证明注入内存实现之后 `tenv` 的读写走的是它。
3. **范围**：6b 只换 `SystemVar.ini` 的两处；`tenv` 其余的 Qt（当前目录、家目录、标准目录、可执行文件的目录、环境变量、首次运行时复制 stuff）放到 6c，Windows 注册表的 `getSystemValue` 也在 6c；`tenv.cpp` 等 6c 之后再进无 Qt 构建。

验证：Linux 首次运行写出的 `SystemVar.ini` 与改动前逐字节相同；读同一份含逗号、非 ASCII、引号的 ini，新旧取到的值逐字相同。

**6c 开工时的决定（2026-11-18，所有者委托主会话拍板）。** 6c 开工前报告了三个取舍，主会话都按建议定了：

1. **没有 Qt 应用对象时，可执行文件的路径给真实路径**（Q-A 取 (a)），与 6a 同一个思路；有应用对象时与 `applicationFilePath`、`applicationDirPath` 逐字相同。macOS 上 .app 包内、裸可执行文件、经符号链接启动三种形态都要对照（规范化、符号链接解析与否都照 Qt）。由此 tconverter 在 macOS 和 Windows 上的便携检查多看一处（可执行文件旁边的 `portablestuff`），并进发布说明的 tconverter 条目。
2. **首次运行复制 stuff 那段照 Qt 的语义改写**（Q-B 取 (a)）：6a 在 `tsystem.cpp` 里照 Qt 写的那些操作，加上照 Qt 写的 `removeRecursively`，经一个只给核心内部用的头 `tsystem_qtsemantics.h` 暴露（不进 `toonz/sources/include`，头注释写明"照 Qt 5.15 语义，不是公共接口"）；`removeRecursively` 和复制目录树进 6a 那套差分测试（至少嵌套目录、隐藏文件、指向目录的符号链接、只读文件）。
3. **返回 Qt 类型的函数照第 4 步的先例加 std 版本**（Q-C）：`getHostNameW`、`getUserNameW`、`getUniqueFileW`，原来的 Qt 版本留在 `tsystem_qt.cpp` 当包装，调用点不改；`getSystemValue`（Windows 注册表）和 `isDLLBlackListed` 留在 Qt 一侧。`showDocument` 照 6b 做成注入的服务，切换点只有一处，并有测试；Qt 构建默认用 `QDesktopServices`，无 Qt 构建默认返回 false。

顺带的两件事：`setWorkingDirectory` 把当前目录转成 Latin-1 的缺陷原样保留，记为已知问题；Haiku 分支留在 `#ifdef HAIKU` 里继续用 Qt，无 Qt 构建不支持 Haiku。

**第 7 步开工时的决定（2026-11-18，所有者委托主会话拍板）。**

1. **样式的界面文字（Q7-A 取 (a)）**：核心公共头里的 `TTranslatableText`，字段只有三项：上下文、UTF-8 的源文本、是否翻译。新增虚函数 `getDescriptionText`、`getParamNameText`、`getParamRangeText` 返回它；原来的 `getDescription`、`getParamNames`、`getParamRange(int, QStringList &)` 改成非虚的 Qt 包装，调用点不改（Q2 的做法）。要翻译的串写成 `TTranslatableText("类名", QT_TRANSLATE_NOOP("类名", "文本"))`，全树统一这一种写法；不改 lupdate 的参数，lupdate 产出的 .ts 必须逐字节不变。MyPaint 这类运行时的串标为不翻译，与现状一致。
2. **核心存进数据的译文（Q7-B 取 (a)）**：一个窄的翻译服务，照 6b、6c 的做法（可设置的函数指针，唯一切换点，有注入测试）。只给"核心把译文存进数据"的地方用，目前只有 `TPalette` 第一页的名字 "colors"；头注释写明这一限制，免得以后被当成通用的翻译入口。Qt 构建默认 `QCoreApplication::translate`，无 Qt 构建默认原样返回。所以无 Qt 构建和 iPad 在壳注入服务之前，新建调色板的页名是英文的 "colors"；注入它是 iPad 壳要做的事之一。
3. **验证**：一个导出程序装上每种语言的译文，导出全部注册样式的描述、参数名、枚举项和新调色板的页名。改动前用 Phase 2 之前应用包里的库和 `84f17b336` 的头，改动后用新构建，两份逐字节相同。对新源码跑 lupdate，产出的 .ts 与改动前一样。导出程序和结果放在 `toonz/build-phase1/ab/phase2/`，不进版本库，做成的样子里写明怎么重跑。

**第 7 步做完后的决定（2026-11-18，所有者委托主会话拍板）。** 第 7 步那一栏原来写的"矢量图、样式进入无 Qt 构建"做不到：那些源文件要链接 `tsimplecolorstyles.cpp` 和离线 GL，而它们带着桌面 GL。主会话认可的安排：矢量图和样式里剩下的不带 GL 的 Qt 放到第 8 步开头，和桌面 GL 迁出核心一起做，第 8 步结束时矢量图和样式进无 Qt 构建；迁移表的第 7、8 步已照此改写。要求：`QString::number(x, 'g', 17)` 和 `toDouble` 进了 `.tpl` 文件，替代实现要与 Qt 逐字节相同（负零、inf、nan、指数格式、小数点），用大量随机 double 和边界值的差分测试覆盖；帧号区间的解析也与旧代码差分对照；`QDir::match` 的通配规则（`*`、`?`、`[]`、大小写）照 Qt 5.15 移植并对照；GL 迁到 `tnzglview` 时核心库对外的类布局不变（无 Qt 构建和 Qt 构建的核心对象要能互换），样式的 GL 入口留在 viewer 一侧；开工前先把第 8 步的拆分和每个子步的验证方式报主会话确认。

**第 8 步开工前的决定（2026-11-18，所有者委托主会话拍板）。** 拆成 8a（矢量图和样式里不带 GL 的 Qt）、8b（核心对 GL 的每次使用都经接缝，实现暂留在 tnzcore）、8c（GL 搬进新库 `tnzglview`，tnzcore 和 colorfx 不再链接 GL）、8d（矢量图、样式、colorfx 进无 Qt 构建和 iPad 模拟器），每个子步验证完再进下一个。四件事都按建议：

1. **Q8-A 接缝**取三种：给薄壳的"viewer 画布"钩子；给直接画 GL 的样式函数和属性类 `draw` 的按类注册的 GL 画法；给图标、图案笔刷和 `TVectorRasterizer` GL 退路的离线栅格化后端。三种都做成可设置、唯一切换点、有注入测试；核心类的布局在两种构建里相同，GL 虚函数的声明留在原来的类里。函数体搬到 tnzglview 以后经友元结构访问私有成员，类注释里写明那是 tnzglview 的实现细节。
2. **Q8-B** 每个要 GL 的程序在 `main` 里显式调 `TGLView::install()`。逐个程序判断能否走到 GL 路径（OpenToonz、tcomposer 要；tconverter、tcleanup、两个农场程序查清），结论列成表写进做成的样子。
3. **Q8-C** 没装 tnzglview 时 GL 入口什么都不画，图标和图案笔刷改走默认的 Skia 栅格化器，结果与桌面不同；写进文档和 iPad 待办。保险：桌面 Qt 构建里接缝未装而 GL 入口被调用时，一次性往标准错误打一行（Debug 构建下 assert），免得漏了 install 的程序悄悄什么都不画；无 Qt 构建里不打。
4. **Q8-D** 没有调用者的调试画法（`drawPointSquare`、`drawPointCross`、`drawLinks`）删掉，提交信息里逐个列名；有调用者的 `drawQuadratic`、`drawStrokeCenterline` 搬进 tnzglview；`#ifdef` 里默认不编译的原样不动。

另外：8a 的数字对照里，`QString::number(x, 'g', 17)` 对 inf、nan、负零的输出以 Qt 5.15 实测为准，样本写进测试；8c 的 viewer 人工验收清单到里程碑时与 6a 的清单合并成一份交给所有者。

**8b 开工后的决定（2026-11-18，所有者委托主会话拍板）：接缝的细化。** 实测之后，属性类（`TSimpleStrokeProp`、`OutlineStrokeProp`、`OutlineRegionProp`、`VectorBrushProp`、colorfx 的 `TMatrioskaStrokeProp` 和模板 `TOptimizedStrokePropT<T>`）用到的 GL 只有压入变换、弹出变换和像素大小。主会话取了细化的做法：

- 属性类的函数体留在核心，这几处 GL 调用换成"viewer 画布"接缝的钩子。
- 画布钩子另带一个"在外面保存全部 GL 属性"的开关，给 colorfx 里 11 个经 `drawThroughGl` 画的区域样式。
- 按类注册的 GL 画法只留给 9 个带着样式自己 GL 状态的函数：中心线、两个图案笔刷、FlowLine、Sketch、两个条纹填充、点阴影、`TNormal2`。

理由：属性类的裁剪和缓存逻辑留在核心，8d 的无 Qt 单测才覆盖得到；模板不用按实例化逐个注册；搬走的代码少，逐字节相同也更好证明。代价是没装 tnzglview 时"压入、弹出变换"是空操作，按 Q8-C 无害。

**"viewer 画布"接缝的钩子到此为止**：建画布、线条平滑的状态、压入变换、弹出变换、像素大小的平方、在外面保存全部属性的开关。再要加新钩子要先问主会话，免得它慢慢长成一张 GL 函数表（那是 Q8-A 否决的 (b)）。

离线 GL 的接缝交出一个上下文对象（清空、设为当前、画、取回栅格）。图案关卡加载时跨帧重用同一个上下文、帧与帧之间不清空，这一行为用对照测试钉住。

**8c 开工时的决定（2026-11-18，所有者委托主会话拍板）。** 8b 留下的那一条：`tsimplecolorstyles.cpp` 里只在 Windows 上编译的显示列表管理器是死代码（`getDisplayListId` 没有调用者，`clearLists` 清的是一张永远空着的表），按 Q8-D 的同一标准删掉，提交信息里列名。8c 照第 8 步的决定做：新库 tnzglview 收下 GL 工具层、接缝的实现、9 个画法和 Q8-D 要搬的函数；tnzcore 和 colorfx 不再链接 GL 与 tnzglcanvas，用 nm 或 otool 证明；头留在 `include/`，名字不变，调用点不改，只改链接；各程序按"能否走到 GL 路径"逐个判断是否调 `TGLView::install()`，列表写进做成的样子；接缝未装时的告警照 Q8-C。

**8c 做完后的决定（2026-11-18，所有者委托主会话拍板）。** 两处与原来写的不同，都认可：

1. **更正 Q8-D**：原文说 `drawQuadratic` "有调用者，搬进 tnzglview"，那是当时看错了。有调用者的是 `magnettool.cpp` 里同名的文件内局部函数，与核心的不是同一个。`outlineApproximation.cpp` 的 `drawQuadratic` 只被没有调用者的 `drawOutline` 用，两个都删是对的。Q8-D 里要搬的因此只有 `drawStrokeCenterline`。
2. **链接 tnzglview 的库**：除了迁移表原来写的 toonzqt、tnztools、toonz，还有 toonzlib、tnzext、tnzstdfx。它们自己直接用 `TOfflineGL`、`tglDraw` 和模板缓冲，调用点不改就只能也链接它。迁移表已改成实际情况。

然后进 8d：矢量图、样式（含 `tpalette`、`tsimplecolorstyles`）和 colorfx 全部，连同它们要的 traster、timage、tnzcanvas，进无 Qt 构建和 iPad 模拟器。新写不带 Qt 的单测：建矢量图，算区域，经 Skia 画布画出来，与 Qt 构建逐字节相同。验证照旧：无 Qt 的三道检查、iPad、Linux 虚拟机。8d 完成后第 8 步收口，整理第 8 步整体的做成的样子和人工验收清单。

**8d 开工时的决定（2026-11-18，所有者委托主会话拍板）：前置一个 8d-0。** 按符号算依赖闭包，矢量图、样式、colorfx 进无 Qt 构建，要从 tnzcore 再拉 45 个源文件进来，其中 6 个还带 Qt：
- `timagecache.cpp`：栅格分配失败时压缩缓存，trop 也用缓存；
- `tcodec.cpp`：LZO 经 `QProcess`；
- `tlevel_io.cpp`；
- `tcontenthistory.cpp`；
- `tsystempd.cpp`；
- `tstringid.cpp`。

主会话取 (a)：在 8d 本体之前，照已有的做法把这 6 处清掉，范围只限这 6 个文件。不取 (b)，即用过渡钩子切断到图像缓存和关卡读写的边。逐条要求：

- `tstringid`：`std::mutex`，照第 1 步。
- `tsystempd`：回收站那段照 Q2 搬进 `_qt.cpp`，`qDebug` 照第 4 步。
- `timagecache`：`QThreadStorage` 换成 `thread_local`。两者都在线程结束时析构值，时机不同；缓存开关只是 `bool *`，应当没有影响，确认后写进做成的样子。`QString` 的重载照 Q2 处理。多线程渲染对照（8 线程那组）必须逐字节相同，并在清洁剂构建里跑一遍，ThreadSanitizer 现成的话顺带跑。
- `tcodec` 的 LZO：做成注入的服务，照 `showDocument` 的做法。Qt 构建默认用 `QProcess`，行为不变；无 Qt 构建默认"不支持"，要有明确的错误路径，不能静默返回空数据。读 LZO 压缩的 tlv 关卡，前后对照逐字节相同；参考工程里要是没有这样的 tlv，先生成一个加进对照语料。第 13 步到时直接接链接库的实现。
- `tlevel_io`：照第 5 步 `timage_io` 的先例。格式表要是有顺序依赖（扩展名的枚举顺序进了界面或输出），用对照钉住。
- `tcontenthistory`：它存进文件，要与 Qt 逐字节对照；本地时间的格式至少在 UTC、东八区和一个有夏令时的时区里各比一遍。

迁移表第 9、13 步的相应条目标为"8d-0 已做一部分"。

**8d-0 开工后的决定（2026-11-18，所有者委托主会话拍板）：清单封口。** 把整个闭包在无 Qt 构建里试编一遍，只有 7 个编译单元编不过，都卡在三个头上：`tcontenthistory.h`、`tthread.h`、`tproperty.h`。逐个定：

- `tthread.h`：只有 `tsimplecolorstyles.cpp` 包含它，那里已经用不着了，去掉包含。
- `tproperty.h` 取 (a)：
  - 成员换成 `std::wstring`，两种构建的类布局相同。`QString` 接口照 Q2，对前向声明的 `QString` 声明，定义在 `tproperty_qt.cpp`，约 480 个调用点不动。
  - `TEnumProperty::Item` 的两个公有字段，给 `Item` 加 Qt 一侧的访问函数（Q2）。直接读字段的约 10 行界面代码改成调这些函数，不在调用点手写转换，以后的调用者也走这条路。
  - 对照测试照第 7 步的翻译导出：每个工具和格式的属性名、枚举项的界面文字，改动前后导出逐字相同。
  - `TPropertyGroup` 存进文件（场景和输出设置里的图像读写属性），参考工程里含输出设置的场景，前后读写逐字节相同。
  - 有没有以 `QString` 名字作键的查找（例如按界面名找枚举项）依赖 `QString` 的比较语义（大小写、规范化），有的话用对照钉住。
- `tlevel_io.h` 的 `getCreator` 照 Q7-A：新加返回 `std::wstring` 的虚函数，`getCreator()` 改成非虚的 Qt 包装，image 库里的三个重写跟着改；`m_creator` 换成 `std::wstring`。
- colorfx 里已经用不着的 Qt 包含，在 8d 本体里去掉。

8d-0 的清单到此为止：6 个源文件，加上 `tproperty.h`、`tcontenthistory.h`、`tlevel_io.h` 三个头。再有新发现先问。

**8d 本体开工时的决定（2026-11-18，所有者委托主会话拍板）。**

1. **pli 读取器取 (a)。**
   - image 库照 tnzcore 的做法加一个 `IMAGE_NOQT_SOURCES` 清单，先只放 pli 的三个文件（`pli_io.cpp`、`tags.cpp`、`tiio_pli.cpp`）。
   - 新的注册函数 `initPliIo()` 由 `initImageIo()` 调用。Qt 构建里的注册顺序不变，格式表的顺序用测试钉住。
   - 三个文件照现成的先例清掉 Qt；`ParsedPli` 的 `getCreator`、`setCreator` 照 Q2 做成包装。
   - 验证：参考工程全部 pli 读出的矢量图（笔触、区域、调色板、帧列表）与 Qt 构建逐项相同，写回的字节逐字节相同，含带 creator 字段的文件。
   - 这是把迁移表第 13 步（image 库）提前做了一小块，表里已注明。
2. **CI 的 iPad 那一步取 (i)。**
   - `skia_pack.sh` 多认一个 `iosSim-x64`，同样缓存；`noqt_test.sh` 多一个参数，传 iOS 的 Skia 目录。CI 和本机跑同一套脚本。
   - workflow 只改文件，推送时才在 GitHub 上跑到。做成的样子的"未验证"里写明这一点，并列出"macOS-noqt 作业在 GitHub 的 runner 上找不找得到 iPad 模拟器的运行时"，推送后第一件事就是看它。

**8d 本体开工后的决定（2026-11-18，所有者委托主会话拍板）：toonzlib 的样式。** 参考工程的 `vec_styles.pli` 是"每种样式都放一个"的调色板，里面有 toonzlib 自己的样式：纹理（tag 4），还有 MyPaint 和两个图像样式。它们的类在 toonzlib，无 Qt 构建里没有注册，读这个文件会抛出 "Unknown color style id"。

主会话取 (a)：
- 无 Qt 构建测其余 12 个 pli，`vec_styles.pli` 只在 Qt 构建里测，做成的样子写明原因。
- 这几个样式随 toonzlib 的迁移进来，并进 iPad 待办：在那之前读不了用这些样式的关卡。
- 读到不认识的样式 id 时抛异常，这个行为不改；这也是否决 (c)（放占位样式继续读）的理由。
- 加一个测试把它钉住：无 Qt 构建里读 `vec_styles.pli`，应当抛出 "Unknown color style id"，不能崩溃，也不能交回半成品。toonzlib 的样式进来以后，这个测试会从"应当抛出"变成"应当读出"，提醒更新。

**8d 本体的决定（2026-11-19，所有者委托主会话拍板）：三处老的数据竞争。** 8 线程同时读并画 pli 的新测试在 ThreadSanitizer 下找出三处数据竞争，都是老代码，Qt 构建同样有：
1. 笔画 id（`tstroke.cpp` 的 `++maxStrokeId`，一个普通的全局 int）：并发建笔画会拿到重复的 id。
2. `TBigMemoryManager::instance()` 是不加锁的延迟构造；`putRaster` 里的统计计数器没有同步。
3. `tsmartpointer.cpp` 的 `getInstanceCounter` 在普通指针上做双重检查加锁。

主会话取 (a)，现在就修，TSan 跑干净。要求：
- 修复单独一个提交，提交信息逐条写明三处竞争、TSan 报告的摘要和修法；做成的样子写明这是第 9 步（线程）提前做的一小块。
- 单线程的行为逐字节不变：golden、554 帧、GL 退路照旧与 8d-0 对照；8 线程那组逐字节相同。
- 第 1 处是真问题：已知问题文档里加一条，标"已修复（Phase 2 第 8d 步）"，发布说明也提一句，因为它可能解释过去多线程渲染里偶发的怪现象。
- `getInstanceCounter` 用 `std::atomic`，可以用 acquire/release，但不为性能把双重检查写复杂，简单优先。
- 8 线程读画 pli 的测试留在无 Qt 和 Qt 两种构建里常跑；TSan 构建的跑法写进测试 README 的 Sanitizers 一节。

**第 1 到 8 步作为里程碑准备时的决定（2026-11-19，所有者委托主会话拍板）。** 整理后的分支 `tidy/phase2-steps1-8` 与 `wip/phase2-noqt` 末端内容相同；里程碑本身（推送、合并）等所有者定。准备中定了三件事：
1. tcleanup 在 macOS 上单独运行时找不到 `SystemVar.ini` 而中止：记为已知问题第 34 条（原有问题，注明崩溃报告的来源），不修。人工验收第 11 条改在应用里做清理。
2. Windows 的 workflow 加 `workflow_dispatch` 触发，只加这个，不加推送触发。推送后手动跑一次，验证 Windows 上的盲改，不必开 PR。
3. arm64 的矢量校验和表要不要再拆一张：等 CI 的结果出来再定，先不动。

**第一轮 CI 之后的决定（2026-11-20，所有者委托主会话拍板）：arm64 的校验和。** arm64 上除了画面，`regions/computed` 和 `regions/filled` 两个数据项也不同（8.16 节）。取 (B)：这两项各拆成两项。结构项只含整数（笔画数、区域数、样式、每个区域由哪些边围成），三个平台必须相同，不进分平台的表；几何项是 double 的原始位，按平台。另一个选项 (A) 是把这两项也当作随平台变，一轮 CI 就能填满，但 arm64 上区域的结构真错了也会被盖住，所以不取。为了少走一轮 CI，arm64 的表现在就填满：画面项和那两个几何项的值取自日志，结构项按定义用 x86_64 的值。arm64 的结构项若真的不同，下一轮会失败并打出实际值。`NEXTTOONZ_PRINT_HASHES` 的输出文件和 macOS workflow 失败时上传它，照样加上。三张表的提交都带 `Fixtures-Update:`。

做的时候有一处与上面的字面不同：几何项就是原来的整项，整数和坐标的位都在里面，改名为 `…/exact`。这样它在 arm64 上的值日志里已经有了。如果只哈希坐标的位，那是新的值，日志里没有，又要多走一轮 CI。结构项相同的平台上，整项不同就等于坐标不同，两种做法查出的东西一样。

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

**Q9 不带 Qt 时，Apple 以外的平台上帧字母怎么排序（第 4 步发现，2026-11-18 起待定）。** `TFrameId` 的字母（`pippo.0001a.tzp` 的 `a`）原来按 `QString::localeAwareCompare` 排序，它决定同一帧号下带字母的帧在关卡里的先后。Qt 在 macOS 上调用 CoreFoundation 的 `CFStringCompare`，第 4 步在 Apple 平台上直接做同一个调用，所以 macOS 和 iPadOS 上逐位相同，也不需要 Qt。在 Linux 上 Ubuntu 的 Qt 用 ICU 的排序规则，在 Windows 上用 `CompareStringEx`；第 4 步在这两个平台的 Qt 构建里仍然问 Qt，行为不变。问题在于核心最终不链接 Qt 之后（第 20 步），这两个平台怎么办；现在只有 Linux 的无 Qt 构建（本机虚拟机，不在 CI 上）用 C 库的 `wcscoll` 临时顶着。选项：(a) Linux 链接 ICU、Windows 调用 `CompareStringEx`，与今天逐位相同；(b) Windows 调用 `CompareStringEx`（与 Qt 相同），Linux 改用 C 库的排序，只有大小写或重音不同的字母顺序可能与今天不同；(c) 所有平台都改用一个固定的、与语言环境无关的顺序，各平台一致，但 macOS 上带字母的帧顺序会变。倾向 (b)：Linux 目前只是 CI 测试床（D5），为它多一个 ICU 依赖不划算；(a) 留到 Linux 成为产品目标时再说。不影响现在的任何一步，第 20 步之前定即可。（2026-11-18 已定，取 (b)，见本节开头。）

---

## 11. 风险与未验证事项

- **应用本身没有自动测试。** golden 和单测覆盖的是 tcomposer 和库；线程、Handle、Preferences 这几步改的是应用每天都走的路径，只能靠人工验收。第 9、10、15、16、17 步在合并前都要所有者在应用里试用。
- **std 与 Qt 的细微语义差别。** 字符串：无效 UTF-8 的替换字符、大小写不敏感比较（QString 用 Unicode 大小写折叠）。路径：`QDir::cleanPath` 的规则、大小写不敏感的文件系统。时间：`QDateTime` 的时区与夏令时。线程：`QThreadStorage` 在线程退出时析构，`thread_local` 的析构顺序不同；线程优先级（Qt 在 macOS 上设 QoS）。容器：`QHash` 的遍历顺序。这些都要靠差分测试逐一钉住，不能靠读代码。
- **头文件改动的波及。** 第 1 步一个互斥锁改了 68 个源文件，其中界面 27 个；而且第一次只在 macOS 上编译，Linux 上又查出 14 个文件要改（一处只在 Linux 编译的代码，13 处间接包含），每一步都必须在 Linux 虚拟机里全量编译一次。字符串和路径那几步的波及会大一个量级；第 6 节的适配能减少，但减不到零。界面文件只在本机和 CI 的全量构建里编译，Windows 只在 master 上跑 CI。
- **Windows 是盲改的。** 第 1 步改了三个只在 Windows 上编译的文件（avi、mov、3gp 的读写器），本机和 Linux 都编不到。D5 里 Windows 的产品地位待定，CI 的 Windows workflow 只在 master 上跑，这些文件可能长期没人编译。第 6a 步把 Windows 的 `TFileStatus` 也改了（仍由 `QFileInfo` 填，搬进 `tsystem_qt.cpp`，去掉了提前退出），同样没有编译过。
- **OpenGL 退路在多线程下不确定，还会崩溃**（已知问题第 26 条，第 1 步验证时发现）。用 `NEXTTOONZ_VECTOR_RASTERIZER=gl` 加 `-nthreads 8` 渲染，同一个构建两次的结果不同，`vector_cmapped_fx` 间歇段错误；改动前的构建也一样。默认的 Skia 路径在 8 线程下确定。它的后果是：碰线程的步骤在 GL 退路上只能做单线程对照。
- **`TRenderer` 的等待方式**（第 5 节"线程"）：桌面靠"等的时候处理事件"保持响应。新的派发器接口要保留这个能力，否则界面会在渲染时冻结；只有人工能验证。
- **arm64 没有跑过。** 第 1 步在 x86_64 macOS、x86_64 Linux 和 x86_64 的 iPad 模拟器上测过；arm64 只编译链接过（iPad device 模式）。
- **CI 没有跑过第 1 步。** 分支没有推送。macOS arm64、Ubuntu gcc、Fedora 三个 job 本机没有对应的环境（Linux 虚拟机是 Ubuntu 24.04 的 clang 清洁剂构建）。
- **无 Qt 构建仍然要求全部第三方库。** 它走同一个顶层配置，OpenCV、TIFF、Boost、SuperLU 等都要找到，`superlu41` 也会编译，虽然第 1 步的那 10 个文件一个都不用。iPad 构建时第三方库的来源是另一回事（Phase 1 的 Skia 用的是预编译包），本稿没有设计。
- **第一个改变运行时行为的改动**（6a，`TFileStatus` 去掉"没有 `QCoreApplication` 就当不存在"的检查）。只有在应用对象建立之前的调用会看到不同的结果：以前一律"不存在"，现在是真实状态。6a 的"做成的样子"逐个列了这些调用和判断；自动测试覆盖不到应用自己的启动路径（便携模式、首次运行、stuff 目录缺失时的创建），列成了人工验收清单，到里程碑时交给所有者。Phase 2 的发布说明要提这一条。实测（lldb，macOS 的 6 个程序）应用、tcomposer、tcleanup 和农场的两个程序在应用对象之前一次都不问；受影响的只有 tconverter，它在 macOS 和 Windows 上根本不建应用对象，6a 之前在这两个平台上什么都转不了，之后能用了。
- **要跟进的两个决定。** Q4（设置存储收缩到哪一步、iPad 存在哪）和 Q5（各平台文字像素不一致、要不要改用 Skia 文字）是按"行为不变"先做的，所有者要求跟进，分别在第 15 步和第 18 步做完时回到所有者；计划的待定事项表里各有一行。
- **GL 退路搬家以后的验证。** 第 8 步之后，离线渲染的 GL 退路要由 tcomposer 链接 `tnzglview` 才有；Phase 1 以来用它证明"OpenGL 路径逐位不变"的那套对照（画布文档 8.1 节）从此只在链接了它的程序里能做。
- **定义在别的库里的成员函数**（第 6 节的补充）。`tfilepath_qt.cpp` 现在在 tnzcore 里，到第 20 步核心不链接 Qt 时，它要搬进界面一侧的库。在 macOS 和 Linux 上，一个类的成员定义在另一个动态库里没有问题；在 Windows 上 `TFilePath` 整个类是 `__declspec(dllimport)` 的，成员必须从 tnzcore 的 DLL 导出，搬走就链接不上。那时要么给这几个成员单独去掉导入标记，要么在 Windows 上把它们留在一个链接 Qt 的小库里。Windows 的地位待定，记下，不在现在处理。
- **无 Qt 构建里内容历史的日期**（8d-0）：Qt 构建照旧用系统语言环境的月份名；无 Qt 构建用英文月份名，只在英文语言环境下与桌面相同，存进关卡和场景的历史记录会不同。iPad 壳要不要按语言环境写，到 iPad 时再定。
- **`qWarning` 换成了 `fprintf(stderr, ...)`**（第 4 步，`tfilepath.cpp` 的四处警告）。在默认的 Qt 消息处理下两者都写到标准错误；Qt 在 macOS 上没有控制台时会改写系统日志，这几条警告在应用里因此可能换了去处。只是警告，不是行为。
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
