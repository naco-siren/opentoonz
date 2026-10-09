# NextToonz 架构重构计划

版本 0.1，2026-10-09。本文记录 NextToonz（本 fork 对 OpenToonz 的架构级重构）已经确定的决策、支撑这些决策的代码调查数据，以及分阶段计划。尚未确定的事项单独列在"待定事项"一节，不混入决策。

每条决策都附依据和"翻盘条件"。翻盘条件是指：出现什么证据时应当重新审议这条决策。这是为了让决策可以被推翻，而不是被遗忘。

调查基线：`naco-siren/opentoonz` 分支 `claude/busy-ptolemy-sulcsi`，HEAD `0f7b24a`，与上游 `opentoonz/opentoonz` master 同步。所有路径相对于仓库根目录，`toonz/sources/` 以下的路径省略该前缀。

---

## 目录

1. 目标与非目标
2. 现状诊断
3. 已确定的决策 D1 到 D10
4. 分阶段计划 Phase 0 到 Phase 5
5. 待定事项
6. 风险
7. 参考资料
8. 附录：调查数据

---

## 1. 目标与非目标

目标：

- 把 OpenToonz 从"连升级 Qt6 都费劲"的状态，重构为"核心与 UI 解耦、渲染走平台中立抽象、各平台用原生图形 API 实现"的现代架构。
- 桌面版（macOS 优先）在重构全程保持可用、可发布。
- 为 iPadOS 版本准备好一切架构前提，但 iPadOS 本身在桌面重构完成后再做。
- 建立自动化测试，使每一步重构都有可验证的"正确性基线"。

非目标：

- 不做大爆炸式重写。任何阶段都不允许出现"几个月没有可运行的产品"的窗口。
- 不以换语言为目的。核心语言保持 C++。
- 不追求与上游 OpenToonz 长期合并兼容。Phase 2 之后接受架构级分叉。
- aarch64 Linux 桌面不在目标内。
- Android 只作为可选的 Phase 5 记录技术路线，不承诺排期与人月。

---

## 2. 现状诊断

### 2.1 代码规模

`toonz/sources/` 下 .cpp/.h 合计约 85 万行，加上 .c/.hpp 约 88.6 万行。按模块（仅 .cpp/.h）：

| 模块 | 行数 | 角色 |
|---|---|---|
| toonz | 212k | 主程序 GUI，含大量本应在库里的业务逻辑 |
| common | 113k | tnzcore 与 tnzbase 的大部分实现 |
| include | 92k | 公共头文件 |
| toonzlib | 89k | 场景模型、xsheet、渲染调度 |
| stdfx | 82k | 标准特效 |
| tnztools | 81k | 绘图工具 |
| toonzqt | 80k | Qt 控件库 |
| image | 35k | 图像与关卡文件格式 I/O |
| toonzfarm | 23k | 渲染农场 |
| colorfx | 12k | 矢量笔触与区域样式 |
| tnzext | 11k | 网格、plastic 变形 |
| stopmotion | 10k | 定格动画，编译进主程序 |

粗略分层：UI 层（toonz + toonzqt）约 290k 行，工具层 81k 行，真正的领域核心约 200k 到 250k 行，加上特效与 I/O 约 350k 到 400k 行。

### 2.2 OpenGL 现状

| 指标 | 数值 |
|---|---|
| 裸 `gl*(` 调用 | 3,222 处，126 个文件 |
| `tgl*(` 辅助函数调用 | 1,362 处，102 个文件 |
| `glu*(` / `glut*(` 调用 | 75 / 8 处 |
| 触及 GL 的文件总数 | 179 |
| `glBegin`/`glEnd` | 263 处，57 个文件 |
| `glPushMatrix`/`glPopMatrix` | 167 处，50 个文件 |
| 显示列表 `glNewList`/`glCallList` | 8 / 8 处 |
| `QOpenGLShaderProgram` | 41 处，7 个文件 |
| VAO、`glDrawElements`、`glGenFramebuffers`/`glBindFramebuffer` | 0；裸 FBO 调用仅 `sceneviewer.cpp` 的 2 处 `glCheckFramebufferStatus` 能力检测 |
| `glGetDoublev` 矩阵回读 | 37 处，14 个文件 |
| `GL_SELECT` 拾取 | 1 处入口，46 处 `glPushName` |

结论：约 95% 是 OpenGL 1.x 到 2.1 的固定管线与立即模式，运行在兼容性上下文里。着色器只在两处使用：stdfx 的 ShaderFx（GLSL 1.x，带 transform feedback）和 toonzqt 的 LutCalibrator（GLSL 330 core）。

按模块的 GL 调用分布：

| 模块 | gl* | tgl* | 文件数 |
|---|---|---|---|
| tnztools | 1,101 | 707 | 56 |
| toonz | 528 | 77 | 26 |
| common（tnzcore） | 471 | 153 | 25 |
| toonzlib | 363 | 66 | 17 |
| colorfx | 342 | 245 | 4 |
| tnzext | 167 | 47 | 9 |
| stdfx | 125 | 8 | 8 |
| toonzqt | 113 | 5 | 7 |

GL 耦合最深的三处：

1. **矢量样式系统本身就是渲染器。** `TColorStyle` 子类的 `drawStroke`/`drawRegion` 直接发 GL 立即模式。colorfx 注册了 45 个样式类，其中 57 个 `drawStroke`/`drawRegion` 实现手写 `glBegin`，`strokestyles.cpp` 一个文件 220 处 GL 调用。区域填充走 GLU 细分器（`common/tvrender/ttessellator.cpp`），回调直接接 `glBegin`/`glVertex3dv`，三角形从不被捕获。
2. **矢量层的最终渲染没有 CPU 路径。** `TLevelColumnFx::doCompute` 的矢量分支（`toonzlib/tcolumnfx.cpp` 约 1036 到 1084 行）每帧通过 `TOfflineGL` 离屏绘制再 `glReadPixels`。`TOfflineGL` 的后端在 Windows 上是 WGL 加 `PFD_DRAW_TO_BITMAP`，即微软 GDI 软件 OpenGL 1.1；在 Linux 和 macOS 上是 `QOpenGLContext` 加 `QOffscreenSurface` 加 FBO。同一场景在不同平台的像素结果因此几乎必然不一致，尚未实测，Phase 0 的分平台基线会量化。
3. **工具层靠 GL 状态机。** 37 个工具重载 `draw()`，通过 `glGetDoublev` 回读矩阵来计算像素大小，拾取用 `GL_SELECT` 名字栈。`TToolViewer` 继承 `GLWidgetForHighDpi`，后者是 `QOpenGLWidget` 的子类。

需要 GL 上下文的渲染路径：矢量层渲染、`vectorToToonzImage`、图标生成、PlasticDeformerFx、ShaderFx、iwa_FlowPaintBrushFx、粒子特效的默认精灵、tconverter 的 pli 光栅化。因此 tcomposer 批量渲染在多数场景下需要一个 GL 上下文。

`TOfflineGL` 的使用方约 22 个文件（不含其自身实现）：`toonzlib/tcolumnfx.cpp`、`toonzlib/toonzscene.cpp`、`toonzlib/txshsimplelevel.cpp`、`toonzlib/imagebuilders.cpp`、`toonzlib/toonzimageutils.cpp`、`toonzlib/trasterimageutils.cpp`、`toonzlib/stylemanager.cpp`、`toonzlib/scriptbinding_rasterizer.cpp`、`toonzqt/icongenerator.cpp`、`toonzqt/imageutils.cpp`、`common/tvrender/tcolorstyles.cpp`（样式图标）、`common/tvrender/tsimplecolorstyles.cpp`（光栅图案样式）、`common/tvectorimage/tvectorimage.cpp`（`TVectorImage::render`）、`toonz/moviegenerator.cpp`、`toonz/exportlevelcommand.cpp`、`toonz/trackerpopup.cpp`、`toonz/meshifypopup.cpp`、`stdfx/particlesfx.cpp`、`stdfx/iwa_particlesfx.cpp`、`tconverter/tconverter.cpp`、`toonzpreview/`，以及未编译的 `toonzfarm/tfarmclient/thumbnail.cpp`。

已有的可复用抽象种子：`TOfflineGL` 的 `Imp`/`ImpGenerator` 可插拔后端；`TTessellator` 接口（目前只有 GLU 实现）；`TVectorRenderData` 是纯数据；`TStrokeProp`/`TRegionProp::draw(rd)` 多态样式绘制接口；`Stage::Visitor` 把场景遍历与绘制分开；`TStencilControl` 的 mask API 不暴露 stencil 细节；`TGLDisplayListsManager` 跟踪上下文共享组。

### 2.3 Qt 耦合现状

构建只支持 Qt5，最低 5.5.0，CI 用 5.15.x。本仓库没有任何 Qt6 适配：0 处 `QT_VERSION_CHECK`，0 处 "Qt6"。

全树 2,250 个源文件中 834 个直接包含 Qt 头。以下 14 个核心头文件把 Qt 类型泄漏给约 730 个包含者：`tfilepath.h`（QString）、`tconvert.h`、`tstream.h`、`tsystem.h`（QDateTime、QFileInfo）、`tcolorstyles.h`、`tsimplecolorstyles.h`（QCoreApplication）、`tstroke.h` 和 `tpalette.h`（QMutex）、`tundo.h`（QObject）、`tthread.h`（QThread）、`tipc.h`、`orientation.h`、`qtofflinegl.h`、`trasterfx.h`（QOffscreenSurface）。

Qt 在核心里不是"用了几个类"，而是模型本身：

- `TXshLevel`、`TXshSoundColumn`、`TUndoManager`、`Preferences`、`MovieRenderer`、`VectorizerCore` 都是 QObject。
- 11 个 Handle 类靠 Qt 信号通知变更：toonzlib 的 `TXsheetHandle`、`TSceneHandle`、`TFrameHandle`、`TColumnHandle`、`TXshLevelHandle`、`TFxHandle`、`TObjectHandle`、`TOnionSkinMaskHandle`、`TPaletteHandle`，toonzqt 的 `TSelectionHandle`，tnztools 的 `ToolHandle`。
- `TThread` 是 QThread 加信号的薄封装，`Runnable` 是 QObject。`TRenderer` 用 `QCoreApplication::processEvents` 自旋，需要 Qt 事件循环。原生实现 `tthread_nt.cpp` 和 `tthread_x.cpp` 没有编译。
- `TThread::Mutex` 继承 `QMutex` 并用 `QMutex::Recursive` 构造，102 处使用；`QMutex::Recursive` 全树共 7 处，除该构造函数外另有 6 处（tnzext 的 ttexturesstorage 与 plasticdeformerstorage，common 的 tcacheresourcepool、tpassivecachemanager、tsound_qt、tpalette）。两者在 Qt6 都不存在。
- `common/tcore/tstring.cpp` 的 `to_wstring`/`to_string` 转换走 QString。`TFrameId::m_letter` 是 QString。
- toonzlib 里直接调用 `QMessageBox::warning`（`txshsimplelevel.cpp`）和 `QApplication::setOverrideCursor`（`studiopalettecmd.cpp`）。
- image 和 sound 库反向依赖 toonzlib 的 `Preferences`。

Qt6 移除或废弃的 API 计数（全树）：

| API | 计数 | 位置 |
|---|---|---|
| QtScript（QScriptEngine 等） | 348 处，26 个文件 | toonzlib 215，include/toonz 97，toonz 36 |
| 旧 Qt Multimedia（QAudioOutput、QCamera 等） | 约 170 处，17 个文件 | toonz、stopmotion、tsound_qt、txshsoundcolumn |
| QGL*（QGLWidget、QGLFormat、QGLContext、QGLPixelBuffer） | 38 处，13 个文件 | qtofflinegl、tgl.cpp、main.cpp、mainwindow.cpp、skeletontool、toolutils、shadingcontext |
| QTouchDevice | 48 处，10 个文件 | toonzqt、toonz |
| QDesktopWidget / QApplication::desktop | 24 / 16 处 | toonz、toonzqt、tnztools、stopmotion、image |
| 布局 `setMargin` | 56 处 | toonz 54，toonzqt 2 |
| QRegExp / QRegExpValidator | 17 / 10 处 | toonz |
| QTextCodec | 15 处 | sxfio、txshsimplelevel、tiio_psd |
| `enterEvent(QEvent*)` 重载 | 32 处 | 28 个文件 |
| 字符串式 `SIGNAL(`/`SLOT(` 连接 | 2,297 / 2,128 处 | 全树，其中 23 处连接到 Qt6 已删除的 QString 重载，运行时静默失效 |
| QMatrix | 4 处 | stage、iwa_floorbumpfx |
| QFontMetrics::width | 11 处 | stopmotion、penciltestpopup_qt |

已经做过的 5.15 清理：`Qt::SkipEmptyParts` 35 处，`horizontalAdvance` 75 处，`angleDelta` 39 处。旧写法中 `QString::SkipEmptyParts` 为 0；`QFontMetrics::width(` 仍有 11 处（stopmotion 9 处，`toonz/penciltestpopup_qt.cpp` 2 处）；`QWheelEvent::delta()` 1 处，仅在 `_SSDEBUG` 下编译。

Qt 外的平台特定代码：`_WIN32` 554 行 235 文件，`MACOSX` 201 行 95 文件，`LINUX` 87 行 50 文件。热点在 `common/tsystem/`（内存、磁盘、动态库加载）、`tsound_nt.cpp`（2,193 行 winmm）、`twain/`（29 个文件扫描仪）、`tofflinegl.cpp`。唯一的 Objective-C++ 文件是 `mousedragfilter/mousedragfilter.mm`。

插件 SDK（`toonzqt/toonz_plugin.h`、`toonz_hostif.h`、`toonz_params.h`）已经是纯 C 函数指针 ABI：packed 结构体、UUID `query_interface`、`extern "C"` 入口，不依赖 Qt。宿主侧 `toonzqt/pluginhost.cpp` 却在 Qt 控件库里，这是 tcomposer 必须链接 toonzqt 的原因。

### 2.4 领域模型与渲染管线

库依赖图（均为共享库，按 `target_link_libraries`）：tnzcore ← tnzbase ← tnzext ← toonzlib ← {tfarm, tnzstdfx, image, sound}；colorfx 只依赖 tnzcore 与 tnzbase；toonzqt 依赖 toonzlib 与 sound；tnztools 依赖 toonzqt；OpenToonz 与 tcomposer 才把 tfarm、tnzstdfx、image、colorfx、toonzqt 全部链接进来。分层在"包含关系"上基本被遵守：toonzlib 不包含 toonzqt 或 tools 的头；common、tnzbase、tnzext 不包含 toonz 的头。但每一个库，包括 tnzcore，都链接 Qt；除 tnzbase 和 sound 之外的库都直接链接 OpenGL，这两个也经 tnzcore 传递依赖 GL。

模型类：`ToonzScene` 持有 `TSceneProperties`、`TLevelSet`、xsheet 栈；`TXsheet` 持有列集合、`TStageObjectTree`、`FxDag`、声音轨；`TXshCell` 只是 `{TXshLevelP, TFrameId}`。持久化走 `TPersist` 加 `TOStream`/`TIStream` 的类 XML 文本流，可选 LZ4 压缩，42 处 `PERSIST_IDENTIFIER` 注册，148 处 `FX_PLUGIN_IDENTIFIER`。

文件格式：.tnz 场景为文本流；.tpl 调色板为 XML；.pli 矢量为二进制 tag 格式；.tlv/.tzl 光栅为 LZO 压缩二进制，而 LZO 压缩通过 QProcess 调用外部可执行文件 `lzocompress`/`lzodecompress`（`common/trasterimage/tcodec.cpp` 约 572 到 615 行）。视频格式 mov、mp4、webm、gif、apng 通过 QProcess 调用运行时检测到的外部 ffmpeg。mov/3gp 在 Linux 和 BSD 构建里编译进一条经 `t32bitsrv` 进程和 QLocalSocket 的代理客户端，但 `t32bitsrv` 只在 32 位 Windows MSVC 或 Apple 上构建，Linux 上这条路径没有服务端，实际是死代码；macOS 走 `tiio_movM` 的 QuickTime 实现。

渲染管线：`buildSceneFx` 把 xsheet 编译成 `TFx` 树；`TRenderer` 用 `TThread::Executor` 调度 `RenderTask`；`MovieRenderer` 驱动并通过 `LevelUpdater` 写出。stdfx 共 203 个 .cpp，其中约 74 个 `TStandardRasterFx` 加 24 个 `TStandardZeraryFx` 的老特效、47 个 `ino_*` 包装与 46 个 `igs_*` 算法 .cpp（CMake 实际编译其中 40 个）、35 个 `iwa_*` 特效，绝大多数是纯 CPU。需要 GL 的特效见 2.2。`TRenderSettings` 携带一个 `std::shared_ptr<QOffscreenSurface>`，`trenderer.cpp` 约 1425 行有一处硬编码：特效别名含 `plasticDeformerFx` 或 `iwa_FlowPaintBrushFx` 时在 GUI 线程创建离屏表面。

业务逻辑泄漏到 GUI 可执行文件：约 25k 行 xsheet、cell、column、level 编辑命令直接写在 `toonz/` 下（`cellselection.cpp` 3.9k 行、`iocommand.cpp` 3.5k 行、`xsheetcmd.cpp`、`columncommand.cpp`、`filmstripcommand.cpp`、`subscenecommand.cpp`、`levelcommand.cpp`、`mergecolumns.cpp`、`matchline*.cpp`、`rasterizecommand.cpp`、`inbetweencommand.cpp` 等），与 QClipboard 和 `TApp::instance()`（2,388 处使用）缠在一起。`CommandManager` 和 `TSelection` 在 toonzqt 里，基于 QAction。Undo 类分布：toonz 141 个、tnztools 78、toonzlib 58、toonzqt 19。

进程级单例：`TEnv`、`TSystem`、`ToonzFolder`、`Preferences`（QSettings INI，全树 772 处调用，toonzlib 内 94 处）、`TProjectManager`、`TImageCache`、`TUndoManager`、`TMsgCore`（QTcpServer）、`TFxCacheManager`、`CommandManager`、`TApp`。

### 2.5 测试与 CI 现状

- **自动化测试：0。** 没有 gtest、Catch2、QtTest、`enable_testing`、`add_test`。`common/ttest/ttest.cpp` 是一个 517 行的遗留自研框架，编译进 tnzbase，但没有任何 `TTest` 子类，也没有人调用 `runTests`。5,323 处 `assert(` 在 Release 构建下被编译掉。
- **示例数据：0。** 全仓库没有 .tnz、.tlv、.tzl、.pal、.xdts 文件。`stuff/projects/` 只有 `.gitkeep`。84 个 .pli 全是 `stuff/library` 里的笔刷与样式图案，9 个 .tpl 是 studio palette。
- **CI 只编译。** Linux job 有一个 tconverter 的冒烟测试（`QT_QPA_PLATFORM=offscreen`，检查 ldd 和首次运行生成 stuff 目录），其余 job 没有运行过任何构建产物。

| Job | Runner | Qt | 测试 |
|---|---|---|---|
| Linux Ubuntu | ubuntu-22.04，gcc 与 clang 矩阵 | apt Qt 5.15.3 | tconverter 冒烟 |
| Linux Fedora | fedora:latest 容器 | dnf qt5 | 无，带 `-Werror=return-type` |
| macOS | macos-15-intel | brew qt@5 | 无 |
| Windows | windows-2025，VS 2026 | 自定义 Qt 5.15.2 WinTab 版 | 无 |
| clang-format | ubuntu-latest，仅 PR | | 仅检查改动行，clang-format-14 |

- Linux 和 macOS workflow 对任何分支的 push 都触发，但排除 `doc/**`、README、`.github/**` 的改动；Windows 只在 master 的 push 和 PR 上触发。
- 本 fork 有 4 个 workflow 文件但 0 次运行，匿名访问 Actions 页面时侧边栏不列出任何 workflow。这与"fork 继承的 workflow 默认禁用，需要仓库所有者在 Actions 标签页启用"的状态一致。Settings 里的 Actions permissions 是另一个开关，已经是 Allow all。
- 没有 `.clang-tidy`、sanitizer、覆盖率、`CMakePresets.json`。只有 GCC 得到少量警告标志，Clang 没有任何额外警告，没有 `-Wall`。
- `tcomposer` 是唯一可驱动 golden 测试的命令行渲染器：`tcomposer <scene.tnz> [-o dst] [-range a b] [-step n] [-nthreads N] [-TOONZROOT dir]`。它创建完整的 `QApplication`，要求 stuff 目录（缺失时非 Windows 直接 abort，Windows 以 1 退出）和项目文件（找不到项目或场景、场景加载异常时返回 -2），矢量层和 GL 特效需要 GL 上下文。退出码：0 全部完成，-1 有帧未完成或未捕获异常，-2 项目或场景加载失败，1 参数错误，2 连续内存耗尽。
- 上游 `doc/development_checklist.md` 规定对现有特效和渲染的改动"不得改变现有场景的渲染结果"。这是 golden 测试的直接依据。

### 2.6 Apple 平台与 iPad 约束

- 仓库预编译的 `thirdparty/superlu/libsuperlu_4.1.a` 只含 i386 和 x86_64 两个切片，没有 arm64。Apple 构建默认 `WITH_SYSTEM_SUPERLU=OFF`，所以 Apple Silicon 原生构建原本需要 `-DWITH_SYSTEM_SUPERLU=ON` 加 `brew install superlu`，而 macOS 构建文档没有写。mac CI 跑在 Intel runner 上。仓库从未在 Apple Silicon 上验证过构建。（Phase 0 已改为在 Apple 上从源码编译同版本的 SuperLU 4.1，两种架构链接同一份求解器。）
- iOS 的 OpenGL ES 自 iOS 12 起废弃，框架仍随系统分发但已冻结。只有 ES 1.1 有固定管线，而它没有 `glBegin`/`glEnd`、显示列表、`glPushAttrib`、`GL_QUADS`、`GL_POLYGON`、`GL_SELECT`、GLU、GLUT。现有 GL 代码在任何 iOS GL ES 版本上都无法运行。
- iPadOS 不允许 spawn 子进程。现有的 `lzocompress`、ffmpeg、`t32bitsrv` 外部进程路径在 iPad 上全部不可用。
- 以上三条是"iPad 约束必须提前烧入架构"的依据，见 D10。

### 2.7 上游状态

- 上游 [opentoonz/opentoonz#6919](https://github.com/opentoonz/opentoonz/pull/6919)：bgyss 的 "Moving from Qt5 -> Qt6" 草案，2026 年 6 月提出，170 个 commit。采用 `OPENTOONZ_QT_MAJOR` 双通道构建，Qt5 仍为默认；Qt6 通道新增 QJSEngine 路径而 Qt5 通道仍用 QScriptEngine，Qt6 目标彻底移除 Qt5::Script 仍在待办；Multimedia 已移植；macOS Qt6 打包已有；作者自述"not release-ready"，Windows 打包、WinTab 替代、Linux Wayland 验证、翻译生成仍未完成。
- 上游 [opentoonz/opentoonz#5754](https://github.com/opentoonz/opentoonz/issues/5754) "Plans for Qt6?" 把摆脱 Qt5Script 称为重要的第一步，该 issue 已关闭。
- 本 fork 与上游 master 同步，无本地 Qt6 改动。

---

## 3. 已确定的决策

### D1 领域核心保持 C++，目标是去 Qt，不重写为 Rust

决策：核心库（tnzcore、tnzbase、tnzext、toonzlib、stdfx、image、sound、colorfx）继续用可移植 C++17 或更高，逐步去掉 Qt 依赖，不用 Rust 重写。

依据：

- 核心连同特效与 I/O 约 350k 到 400k 行，含 150 多个特效和四种有二十年历史的文件格式兼容细节。重写期间没有任何用户可见收益，而"全部测试通过"的验收标准要求先给每个特效做 golden。
- C++ 本身就能在 iPadOS 和 Android 上编译。痛点是 Qt 渗入核心，不是语言。
- 去 Qt 需要触碰的文件和 Rust 重写需要触碰的文件高度重合，但去 Qt 是机械替换，Rust 重写是重新实现并重新验证。

反方论点与回应：反方举证 librsvg、fish、Stylo 的 strangler-fig 式渐进移植可以边做边发布。这个论点成立，但它要求 UI 通过一个稳定 facade 调用核心，而现有 UI 直接调用 C++ 类（`TApp::instance()` 2,388 处）。在 D3 的引擎 API 建成之前，Rust 无处可插。D6 保留了日后在叶子模块引入 Rust 的可能。

翻盘条件：D3 的引擎 API 建成后，某个边界清晰的子系统（如 tsystem/tfilepath、某个文件格式读取器）用 Rust 重写并在 golden 测试下通过，且带来可测量的崩溃减少或性能收益。届时按子系统逐个评估，仍不做整体重写。

### D2 桌面 UI 保留 Qt；底层换完后升级到 Qt6 作为解耦验证

决策：桌面 UI 继续使用 Qt。在核心去 Qt（Phase 2）与离线渲染换成画布 IR（Phase 1）之后、viewer 上画布之前，把 Qt 升级到 6.x 及以上，作为"UI 层与业务层确实解耦"的验证，判据是核心库零改动、Qt6 构建绿色、golden 全部通过。这是 Phase 3 的第一个里程碑。不用 egui、iced、gpui 等 Rust UI 框架重写桌面 UI。

依据（经反方审查后修正，只保留站得住的三条）：

- 据我们所知，没有 Rust UI 工具包交付过 100 个以上面板的 DCC 软件。Zed 1.0（gpui，2026 年 4 月）发布版仍无屏幕阅读器支持，AccessKit 集成只以实验开关存在于 main 分支，Windows 上完全不可用；CJK 输入法问题几十个 open。Rerun 是我们所知 egui 上最专业的应用，但它是可视化查看器。Graphite 是最成熟的 Rust 2D 创作工具，它刻意用 Svelte 网页 UI 嵌 CEF，Rust 原生 UI 列为长期计划。
- egui 自己的 README 把"最强 GUI 库"和"原生观感"列为 non-goals，明说每个版本都有破坏性变更，"如果你想要升级不 break 的东西，egui 不适合你"。IME 相关 issue 52 个，2026 年 3 月仍有 Linux Fcitx5 预编辑和 macOS 韩文候选框的 open issue。本项目的主要用户群假定为日本和中国动画人，IME 是一票否决项。
- 290k 行能用的 UI 就是产品，它的 Qt6 移植上游正在做。

上一版理由中被反方驳倒、已删除的三条：OpenToonz 并没有用 Qt 的 docking，它在 toonzqt 自写了约 3.6k 行 DockLayout；xsheet、viewer、fx 节点图、函数编辑器都是自绘 paintEvent 和 GL，不是 Qt 控件；现有自绘面板对辅助技术的暴露本来就是零。

被否决的替代方案："保留 Qt 壳，自绘面板用 Rust+wgpu 重写后嵌进 Qt 窗口"。这些面板与 Qt 的选择、undo、菜单、剪贴板缠在一起，我们判断跨语言做事件和状态同步的成本会高于在原 C++ 类里把 GL 调用换成 IR 调用。

Rust UI 成熟度门槛（四条全部达标时重新评估换壳）：

1. CJK IME 行为与 Qt 对齐，无 open 的 CJK 输入 issue。
2. AccessKit 覆盖 Windows、macOS、Linux 三个桌面平台。
3. 存在一个已交付的 100 面板以上的专业应用。
4. 公共 API 连续 12 个月无破坏性变更。

候选：egui_tiles、gpui-component、Slint。画布 IR（D4）让自绘面板与 Qt 解耦，是将来换壳时最大的资产。

### D3 跨语言边界：C++ 引擎 API，按需派生 C ABI

决策：核心对外的边界分两步。现在建一套"引擎 API"：无模板、`noexcept`、具体类型的 C++ 头文件，桌面 Qt UI 直接调用它。当第一个非 C++ 消费者出现时（iPad 的 Swift 壳），从引擎 API 机械派生 C ABI（`extern "C"`，不透明句柄，整数错误码）。Swift 通过 C interop 调用；Android 通过 JNI 调用，JNI 入口本身就是 C ABI。

依据：

- C++ 没有跨编译器的稳定 ABI。MSVC 只在 14.x 工具集家族内保证 C++ 二进制兼容；libstdc++ 的 GCC 5 双 ABI 改变了 `std::string` 的符号名；Qt 为 MSVC 和 MinGW 分发不同二进制。
- 每种语言 FFI 的地板都是 C。Android JNI 入口必须 `extern "C"`，C++ 异常不得跨界，NDK 只允许一个 STL 但允许混用纯 C 库。Kotlin/Native 的 cinterop 只认 C 和 Objective-C。
- Swift C++ interop 自 5.9 起可用，到当前的 6.4 仍有边界：类模板只能预实例化，C++ 异常到 Swift 帧直接终止进程，Windows 上不支持 `shared_ptr`。cxx 是手写桥；autocxx 已于 2026 年 9 月被 Google 归档。
- 先例：libgit2、webgpu.h、SQLite、OpenToonz 自己的插件 SDK。
- 反方指出的正确一点：没有外语言消费者时 C ABI 是死代码。因此先建 Qt UI 每天都在调用的引擎 API，C ABI 后生成。

设计约束：引擎 API 的头文件不含模板、不抛异常、不暴露 Qt 类型、不暴露 `TRasterPT<T>` 之类的模板实例；错误用返回值。iPad 壳只经 C ABI 调用核心，Swift 走 C interop；Swift C++ interop 不作为边界方案。`TSmartObject` 的 `addRef`/`release` 与 Swift 的 `SWIFT_SHARED_REFERENCE` 语义匹配这一点只记录为将来可选的优化，不改变边界。

翻盘条件：无。这是边界设计，不是技术选型。

### D4 渲染：两层画布 IR；离线用 Skia CPU；桌面交互用 QRhi；iPad 用 Metal 后端

决策：

- 定义一个平台中立的 2D 画布 IR，分两层。
  - **路径级**：样式、工具、viewer 发出的是 path（来自现有的 `TStrokeOutline` 和 `TRegion` 几何）、fill/stroke paint、image、clip/mask（替代 `TStencilControl`）、显式 transform、文字、拾取 ID。
  - **网格级**：一个 CPU 阶段把路径级降为三角形（带顶点色和 uv）、光栅 tile、遮罩、MSAA 提示。核心本来就在 CPU 上算描边轮廓和区域，这一步是搬家而不是新写。
- 离线渲染、golden 测试、图标、格式转换：用 Skia 的 CPU 光栅直接消费路径级 IR，得到解析式反走样和跨平台确定性。
- 桌面交互 viewer：用 Qt6 的 `QRhi`（通过 `QRhiWidget`，Qt 6.7 引入且在 6.7 为 tech preview，建议 Qt 6.8 LTS 或更高；`QRhi` 是有限兼容 API，需链接 `Qt6::GuiPrivate`）消费网格级 IR。macOS 走 Metal；若 Windows 和 Linux 保留为产品目标（待定，见第 5 节），分别走 D3D11 与 OpenGL 或 Vulkan。
- iPad：手写一个 Metal 后端消费网格级 IR。预计网格级后端只有一两千行（待 Phase 1 原型验证），所以可以有多个。
- 不手写 Vulkan 后端，不裸用 wgpu。

依据：

- OpenToonz 需要的是 2D 画布，不是 3D 管线。wgpu 本身不画带反走样的路径，用它意味着还要在上面写 vello 那一层。
- Skia GPU 侧的证据不支持把它作为桌面交互后端：Ganesh 是 Skia 维护者计划弃用的组件；Graphite 只有 Dawn、Metal、Vulkan 三个后端，没有 GL 和 D3D；Graphite 加 Vulkan 默认只在 Android 实验性启用；Chrome 在 Windows 走 Dawn 加 D3D11。Zed 在 Windows 放弃 Vulkan 改用 DX11，因为用户机器跑不起来。
- `QRhiWidget` 与 Qt 共享 swapchain、着色器管线（qsb）、HiDPI 和屏幕切换处理，不会出现第二个 GPU 抽象与 Qt 争抢窗口图层的那类崩溃。
- Skia CPU 光栅跨平台语义一致，适合 golden；上游 Skia 只支持 GN 构建、不发布二进制，获取方式见待定事项。
- 代价：viewer 的 GPU 后端依赖 Qt6，所以 Phase 3 要先合上游 Qt6 工作。IR 和工具代码本身不依赖 Qt。

被否决的方案：手写 Metal 加 Vulkan 两套路径级后端（重造 Skia）；vello+wgpu（Linebender 2026 年 4 月月报称 vello_gpu 约为 beta 质量，mask layer 等特性仍会 panic；wgpu 每三个月一个破坏性大版本；wgpu-native 自 2026 年起已提供 iOS aarch64 官方二进制，这一点不再是否决理由）；Qt 的 QPainter（无 GPU 路径光栅）。

翻盘条件：原型证明 QRhi 加核心细分在交互帧率下达不到笔刷预览所需的描边反走样质量；或 Chrome 稳定版在 2026 到 2027 年于 Windows 和 Linux 对用户默认启用 Graphite 渲染（运行时特性，而非 `skia_use_dawn` 构建开关）。

### D5 平台与 CI 矩阵

决策：

- macOS arm64 与 macOS x86_64 保持 parity：两者都是阻塞 CI，都出发布产物。arm64 用 `macos-15` 或 `macos-26` runner，x86_64 用 `macos-15-intel`，后者在 2027 年 8 月退役前保持。
- x64 Linux 与 x64 Windows 的产品支持状态待定（见第 5 节）。在决定之前，继承的 Linux 和 Windows workflow 继续运行，但不阻塞。
- aarch64 Linux 不做。所有者判断消费级 arm Linux 桌面没有市场。
- CI 只用 GitHub 托管的标准 runner。公开仓库上标准 runner 免费且不限时长，含 Linux x64、Windows x64、macOS arm64（M1，3 vCPU，7 GB）与 macOS Intel；只有 larger runner 收费。免费账户并发 20 个 job，其中 macOS 最多 5 个；单 job 6 小时；cache 每仓库 10 GB。不使用自托管 runner（公开仓库上有安全风险），不付费。
- 不使用 `macos-14`，它在 2026 年 11 月 2 日下线。
- iOS 模拟器 job 到 iPad 排期时再加。macos-26 runner 自带 Xcode 26.x 与 iOS 26.x 模拟器运行时（含 iPad 设备类型），Xcode 27 目前只在 `xcode-27` 公开预览标签上；`CMAKE_SYSTEM_NAME=iOS` 加 `xcrun simctl spawn` 可跑 ctest，同样免费。

前置动作（需要仓库所有者）：在 fork 的 Actions 标签页启用 workflow。验证方法：push 一个非 `doc/**` 的改动，查看是否出现 run。

### D6 Rust：默认不用，三个触发条件之一开工时再引入

决策：Rust 不进入主构建。只有在下列三件事之一真正开工时才引入，并且只用于该模块：

1. Skia 的获取：如果 vcpkg 或自建 GN 构建被证明不可维护，用 rust-skia 的预编译 skia-binaries（覆盖 iOS 真机与模拟器、Android、macOS、Windows、Linux，含 metal、vulkan、graphite 特性）包一层 crate 作为画布后端。
2. 新特效或新编解码器以 Rust 插件形式接入现有的纯 C 插件 ABI，零宿主改动。
3. 对不可信文件的解析器（image 目录 22 种格式读取器）用 Rust 重写并接入 cargo-fuzz。

依据：反方证明 Rust 叶子在这三处有实际收益；但在 D1、D2 之下，Rust 没有必须出现的理由。本项目的原则是"有必要上就上，有很大帮助就上，没有好理由就不上"。

翻盘条件：无需翻盘，三个触发条件本身就是开关。

### D7 测试策略

决策：

- 测试框架 GoogleTest，经 CTest 接入 CMake，CI 阻塞。核心单测开 ASan 和 UBSan。
- 三类测试：
  1. **golden 渲染**：用 tcomposer 渲染自建的 reference project，输出 PNG 与基线比较。比较用容差：差异像素比例加峰值误差，必要时加 SSIM，不做逐位。基线按平台分开，直到 Phase 1 完成后才能合并为一份。
  2. **格式 round-trip**：.tnz、.pli、.tlv、.tpl 的加载、保存、再加载，比较结构化输出。tconverter 可做 pli 到光栅、光栅到 tlv 的转换测试。
  3. **纯算法单测**：tgeometry、trop（resample、quickput、over）、tvectorimage（outline、region、autoclose）、tstream、TFilePath、tparam 与表达式、txsheet 操作。
- reference project 自建，提交到仓库。每种层类型、每族特效、子场景、pegbar 层级、相机动画、声音列、plastic、粒子、shader fx 各至少一个场景；小尺寸、短帧数以控制 golden 体积。生成器类特效（colorCard、checkboard、gradients、noise）可做无素材场景。
- Linux CI 用 Xvfb 加 Mesa llvmpipe 提供无头 GL，直到 Phase 1 去掉离线渲染对 GL 的依赖。
- golden 的 `-nthreads 1` 以保证确定性。
- 必须接受的事实：Phase 1 换成 Skia CPU 光栅后，矢量外观会有小幅变化（GL_LINE_SMOOTH 这类实现相关的反走样被解析式反走样取代）。届时人工审图后更新 golden，并对用户群提前说明。

### D8 Qt6 不作为前置条件

决策：不单独做 Qt6 移植阶段。Phase 1 和 Phase 3 顺手消掉 QGL*，Phase 2 消掉 `QMutex::Recursive` 和 `TThread::Mutex`。剩余的 Qt6 阻塞项（QtScript、Multimedia、QDesktopWidget、`setMargin`、QRegExp、QTextCodec、`enterEvent` 签名、字符串式信号连接）全在 UI 侧，交给上游 #6919，在 Phase 3 开始时合并上游的 Qt6 通道。QtScript 绑定在 Phase 2 变为可选编译。

### D9 与上游的关系

决策：Phase 0 以新增模块为主；Phase 1 虽然改写样式与离线渲染路径并改变矢量渲染结果，但仍持续合并上游 master，接受与上游的渲染结果分歧。Phase 2 开始是架构级分叉，之后不再承诺可合并。分叉时机与改名动作见待定事项。

### D10 iPad 与 Android 后置，但三条约束前置

决策：iPadOS 在桌面重构完成后再做；Android 在 iPad 之后视情况。但以下三条约束从 Phase 1 起就是硬规则，因为事后补救代价远高于提前遵守：

1. 核心库不链接 Qt（Phase 2 完成判据）。
2. 核心与渲染路径不 spawn 子进程：LZO 直接链库；ffmpeg 改为注入的编解码服务接口，桌面接 libav 或继续外部进程，iPad 接 VideoToolbox；`t32bitsrv` 与 QuickTime 路径删除。
3. 画布 IR 分层，GPU 后端只消费网格级（D4）。

修正说明：上一版把 "arm64 macOS CI" 描述为 iPad 的落点，这是错的。arm64 macOS 构建链接的是 AppKit、桌面 GL、GLUT、Qt，验证不了任何 iPad 特有的东西。arm64 macOS 进 CI 的理由是桌面本身（D5）。

---

## 4. 分阶段计划

工作量按一个熟手全职估算，单位人月，仅供排期参考。

| 阶段 | 人月 |
|---|---|
| Phase 0 基线 | 1 到 2 |
| Phase 1 离线渲染去 GL | 4 到 6 |
| Phase 2 核心去 Qt 与抽出 | 6 到 9 |
| Phase 3 viewer 与工具上画布，Qt6 | 6 到 9 |
| Phase 4 iPadOS MVP | 6 到 12 |
| Phase 5（可选）Android | 未估 |

### Phase 0 基线

工作项：

- 启用 fork 的 Actions。新增 macOS arm64 job，与 x86_64 job 并列。Apple 构建改为从 `thirdparty/superlu/SuperLU_4.1` 的源码编译 SuperLU（`superlu41` 静态库），不再依赖只有 x86 切片的预编译包，也不要求 `brew install superlu`。
- GoogleTest 加 CTest 接入 CMake；`CMakePresets.json`；导出 compile_commands；核心单测开 ASan 和 UBSan。
- Linux job 加 Xvfb 加 llvmpipe，使 tcomposer 可无头渲染矢量场景。
- 自建 reference project 并提交。
- golden 比较工具（容差、报告、基线更新流程）。
- 格式 round-trip 测试与第一批纯算法单测。
- 修 Apple Silicon 构建文档。

完成判据：

- macOS arm64 与 x86_64 两个 job 绿色并产出 app 包。
- CI 跑 golden、round-trip、单测三类测试且阻塞合并。
- reference project 覆盖 D7 列出的全部场景类型。

### Phase 1 离线渲染去 GL

工作项：

- 定义路径级画布 IR 与网格级 IR。
- CPU 后端：Skia CPU 光栅消费路径级 IR。GLU 细分器不再需要。
- 一个 IR 到旧 GL 的桥接后端，用于 A/B 对照。它放在独立的可选模块里，不被核心库链接，Phase 1 结束后删除或仅作开发工具。
- 把约 50 个样式类（tnzcore 的简单样式加 colorfx 注册的 45 个）、60 余个 `drawStroke`/`drawRegion` 实现从"发 GL"改为"发 IR"。
- 把约 22 个 `TOfflineGL` 使用方切到 CPU 后端。
- PlasticDeformerFx 与 iwa_FlowPaintBrushFx 改为 CPU 变形。粒子默认精灵走 CPU 后端。
- ShaderFx 暂留 GL，标记"需要 GPU 后端"，Phase 3 迁移。

完成判据：

- tcomposer 在 `QT_QPA_PLATFORM=offscreen` 且无 GL 的环境下完成 reference project 全部场景（ShaderFx 场景除外）。
- tnzcore、toonzlib、colorfx、tnzext 不再链接 GL、GLU、GLUT（ShaderFx 所在的 stdfx 除外，受编译选项控制）。
- golden 在 macOS arm64 与 x86_64 上一致，基线合并为一份。
- 人工审图确认矢量外观变化可接受，并发布说明。

### Phase 2 核心去 Qt 与抽出

工作项：

- `TThread` 换为 `std::thread` 线程池加注入式主线程 dispatcher；`TRenderer` 去掉 `processEvents` 自旋与 `qGuiApp`；消掉 `QMutex::Recursive` 与 `TThread::Mutex`。
- `TFilePath`、`tstring`、`tsystem`、`tenv` 去掉 QString、QDir、QSettings、QProcess；14 个泄漏 Qt 的核心头清理；约 730 个包含者机械扫。
- 模型去 QObject：Handle 的信号改为 observer 回调；`Preferences` 换普通键值存储；`TUndoManager` 去 QObject；`TXshLevel`、`TXshSoundColumn` 去 QObject。
- 把 GUI 里约 25k 行 xsheet 编辑命令搬进库，配 characterization 测试。
- 插件宿主的渲染适配器从 toonzqt 搬到核心库。
- LZO 直接链库；ffmpeg 改为注入接口；删除 `t32bitsrv`、QuickTime、tipc。
- 建引擎 API（D3）。
- QtScript 绑定变为可选编译。
- 新增构建选项使核心库不链接 Qt，在 Linux 与 macOS 上编译并跑单测。

完成判据：

- 核心库的默认 CMake 目标不链接 Qt；"无 Qt"构建作为阻塞 CI job 常驻，编译、链接并通过全部单测与 round-trip 测试。
- 核心与渲染路径无任何 `QProcess` 或等价的子进程调用。
- 桌面 Qt UI 通过引擎 API 调用核心，golden 全部通过。

### Phase 3 viewer 与工具上画布，Qt6

工作项：

- 里程碑一：合并上游 Qt6 通道，切换为 Qt 6.8 或更高的构建，核心库零改动、golden 全部通过，作为 D2 要求的解耦验证。
- `SceneViewer::paintGL`、`Stage::Visitor` 的绘制器、`ImagePainter`、`GLRasterPainter` 改为发 IR。LUT 校准作为画布后处理。`GL_SELECT` 改为 ID 命中测试。
- 37 个工具的 `draw()` 与约 1.8k 处 GL 调用改为 IR，变换显式传入；`TMouseEvent` 去掉 Qt 类型。
- `QRhiWidget` 后端消费网格级 IR。
- ShaderFx 迁到新后端或改为 CPU 实现。
- 15 个 GL 控件类（viewer、flipbook、PlaneViewer 及 9 个 swatch、色轮、30-bit 检测器）切到新后端。

完成判据：

- 全树 `gl*(` 调用为 0（第三方代码除外），不链接 GL、GLU、GLUT、GLEW。
- Qt6 构建为默认且唯一通道。
- golden 与单测全部通过。

### Phase 4 iPadOS MVP

工作项：Swift/UIKit 壳；从引擎 API 派生 C ABI；Metal 后端消费网格级 IR；MTKView 承载画布；Pencil 输入进工具层；文档浏览与 security-scoped 文件访问；CoreText 字体；AVAudio 声音；VideoToolbox 编解码；iOS 模拟器 CI job。

MVP 范围：画、播、xsheet 基本编辑、导入导出。不追求与桌面功能对等。

### Phase 5（可选）Android

复用 C ABI，JNI 绑定，Compose UI，Vulkan 或 GLES 的网格级后端。仅在 Phase 4 完成且有需求时启动。

---

## 5. 待定事项

| 事项 | 当前倾向 | 需要的输入 |
|---|---|---|
| x64 Linux 作为产品目标 | 不论是否作为产品，Linux x64 都是最便宜的无头 golden 测试平台，建议至少保留为 CI 测试床 | 所有者决定 |
| x64 Windows 作为产品目标 | 继承的 workflow 继续跑，不阻塞 | 所有者决定 |
| Skia 的获取方式 | 优先 vcpkg 的 skia port（metal、vulkan、graphite 特性），不可行时转 D6 的触发条件 1 | Phase 1 开工时试 |
| Qt6 最低版本 | 6.7 是 QRhiWidget 的下限，倾向 6.8 LTS 或更高 | 合并 #6919 时确认其目标版本 |
| 脚本引擎 | QtScript 绑定在 Phase 2 变可选；长期通过 C ABI 绑定 QuickJS、Lua 或 Python | Phase 2 末 |
| 改名为 NextToonz 的时机 | Phase 2 分叉时统一改产品名、二进制名、stuff 路径 | 所有者决定 |
| ffmpeg 在桌面的形态 | 保留外部进程或改链 libav | Phase 2 |
| toonzfarm 的去留 | 暂不处理 | Phase 2 |
| 扫描仪 twain、Canon SDK、stopmotion 的去留 | 暂不处理 | Phase 2 |
| 本文档的英文版 | 未定 | 有外部贡献者时 |

---

## 6. 风险

- **Phase 1 的像素变化。** 矢量反走样方式改变后，所有矢量场景的渲染结果都会有小幅变化。这违反上游"不得改变现有场景渲染"的规则，意味着 Phase 1 之后的渲染改动不可上游。
- **Qt6 依赖上游进度。** Phase 3 的 QRhi 后端需要 Qt6。若上游 #6919 长期不合并，需自行接手该分支。
- **macOS runner 性能。** M1 标准 runner 为 3 vCPU 7 GB，预计对 88 万行 C++ 的全量构建偏慢，Phase 0 启用后实测。缓解：ccache、分模块构建、必要时拆 job。
- **Skia 构建重量。** 上游只支持 GN 构建。缓解：vcpkg 或预编译二进制，见待定事项。
- **单人项目的阶段跨度。** Phase 2 与 Phase 3 并列最长，各 6 到 9 人月，且 Phase 2 全程用户不可见。缓解：每个子项（线程、路径、模型、命令搬迁）独立合并，每次合并后 golden 必须通过。
- **x86-64 与 arm64 对未定义行为的不同结果。** 代码里大量 `(int)double` 转换在越界或 NaN 时是未定义行为，两种架构给出不同结果；Phase 0 已遇到一例让 arm64 macOS 的每一帧渲染变成空白（已知问题第 14 条）。缓解：清洁剂 CI 用 UBSan 跑全部 golden 场景并阻塞，macOS arm64 与 x86_64 的 golden 都阻塞合并。
- **GPU 相关 golden 在 CI 上不可验证。** 托管 runner 的 Metal 可用性未验证。缓解：golden 只走 CPU 路径，GPU 后端用本地截图人工审查。

---

## 7. 参考资料

- 上游 Qt6 草案：https://github.com/opentoonz/opentoonz/pull/6919
- 上游 Qt6 讨论：https://github.com/opentoonz/opentoonz/issues/5754
- GitHub 托管 runner 与公开仓库免费政策：https://docs.github.com/en/actions/reference/runners/github-hosted-runners
- Intel macOS runner 退役：https://github.com/actions/runner-images/issues/13045
- macos-14 下线：https://github.com/actions/runner-images/issues/13518
- egui README（non-goals、API 稳定性声明）：https://github.com/emilk/egui/blob/main/README.md
- egui IME issue 列表：https://github.com/emilk/egui/issues?q=is%3Aissue%20IME%20sort%3Aupdated-desc
- Swift C++ interop 状态：https://swift.org/documentation/cxx-interop/status/
- Linebender 2026 年 4 月月报（vello_gpu 成熟度）：https://linebender.org/blog/tmil-25/
- autocxx 归档：https://github.com/google/autocxx
- Kotlin/Native C interop：https://kotlinlang.org/docs/native-c-interop.html
- Android NDK C++ 支持：https://developer.android.com/ndk/guides/cpp-support
- Chromium 的 Skia 后端配置：https://raw.githubusercontent.com/chromium/chromium/main/skia/features.gni
- rust-skia 预编译二进制：https://github.com/rust-skia/rust-skia
- wgpu-native 发布页（含 iOS 二进制）：https://github.com/gfx-rs/wgpu-native/releases
- Qt QRhiWidget：https://doc.qt.io/qt-6/qrhiwidget.html

---

## 8. 附录：调查数据

### 8.1 GL 调用最多的文件

| 文件 | gl* 调用 |
|---|---|
| tnztools/edittoolgadgets.cpp | 434 |
| colorfx/strokestyles.cpp | 220 |
| toonz/sceneviewer.cpp | 209 |
| tnztools/edittool.cpp | 166 |
| toonz/viewerdraw.cpp | 157 |
| tnztools/skeletontool.cpp | 107 |
| colorfx/regionstyles.cpp | 103 |
| common/tvrender/tsimplecolorstyles.cpp | 95 |
| toonzlib/stagevisitor.cpp | 89 |
| common/tgl/tgl.cpp | 86 |
| tnzext/meshutils.cpp | 78 |
| toonzlib/imagepainter.cpp | 71 |
| tnztools/toolutils.cpp | 64 |
| toonzqt/styleeditor.cpp | 61 |
| common/tvrender/tofflinegl.cpp | 60 |
| tnztools/plastictool.cpp | 57 |
| common/tvrender/tglregions.cpp | 56 |
| stdfx/pins.cpp | 54 |
| toonz/imageviewer.cpp | 50 |
| common/tvectorrenderer.cpp | 49 |

噪声说明：正则也会匹配名为 `glContext(` 的局部变量；`common/tvectorimage/` 下的 GL 除 `drawutil.cpp` 的 `drawStrokeCenterline`（被 tnztools 与 tnzext 调用）外都是调试绘制；`tnzext/plasticdeformer.cpp` 的 GL 只在 `GL_DEBUG` 下编译。

死代码或未编译的 GL 代码：`common/tvectorrenderer.cpp`（OSMesa/GLX）、`common/tvrender/macofflinegl.cpp`（AGL）、`stdfx/offscreengl.h`（WGL）、`stdfx/pins.cpp` 的 `subCompute` 无调用者、`tofflinegl.cpp` 的 GLX pixmap 实现在 Linux 上编译但默认不用、`QtOfflineGLPBuffer`、`TQOpenGLWidget`。

### 8.2 GL 控件与离屏机制

GL 控件基类：`include/toonzqt/glwidget_for_highdpi.h` 的 `GLWidgetForHighDpi : QOpenGLWidget, QOpenGLFunctions`。`TToolViewer`（`include/tools/tool.h`）继承它，`SceneViewer` 再继承 `TToolViewer`。其他 GL 控件：`ImageViewer`、`PlaneViewer` 及 9 个 Swatch 子类、`HexagonalColorWheel`、`PreferencesPopup` 的 30-bit 检测视图。

`SceneViewer` 的 `paintGL` 顺序：drawBuildVars、scissor、drawBackground、drawCameraStand（drawScene）、drawPreview、drawOverlay（工具）、drawViewerIndicators。启用 LUT 时渲染到 `QOpenGLFramebufferObject` 再由 `LutCalibrator` 做后处理。冻结模式用 `grabFramebuffer` 后 `glDrawPixels` 重绘。

离屏机制并存三套：`TOfflineGL`（WGL DIB 或 Qt 离屏 FBO）；直接的 `QOpenGLContext` 加 `QOffscreenSurface` 加 FBO（`imagebuilders.cpp`、`toonzscene.cpp`，这两个文件同时也用 `TOfflineGL`、`stylemanager.cpp`、`plasticdeformerfx.cpp`、`iwa_flowpaintbrushfx.cpp`、`ShadingContext`）；`QGLPixelBuffer` 仅作能力检测。

光栅图像上屏：2D viewer 的 `Stage::RasterPainter` 在 CPU 上用 `TRop::quickPut` 合成后 `glDrawPixels`；3D 视图的 `OpenGlPainter` 走 `GLRasterPainter::drawRaster` 的 `glTexSubImage2D`；网格与 plastic 走 `TTexturesStorage` 加 `MeshTexturizer` 加立即模式三角形。没有通用纹理图集。

### 8.3 核心库中的 Qt 类使用（行数 / 文件数）

| 类 | tnzcore | tnzbase | toonzlib | image | stdfx | tnzext |
|---|---|---|---|---|---|---|
| QString | 268/28 | 101/11 | 752/89 | 205/28 | 92/11 | 35/3 |
| QObject | 18/8 | 2/1 | 210/47 | 22/1 | 3/1 | 0 |
| QThread | 20/6 | 2/1 | 10/5 | 0 | 22/11 | 0 |
| QMutexLocker | 75/16 | 39/6 | 15/6 | 14/8 | 15/8 | 31/3 |
| QSettings | 5/2 | 6/2 | 19/3 | 0 | 0 | 0 |
| QDir | 70/6 | 1/1 | 18/6 | 13/2 | 9/2 | 0 |
| QImage | 12/2 | 0 | 56/10 | 21/3 | 19/6 | 0 |
| QProcess | 16/4 | 0 | 11/1 | 9/4 | 0 | 0 |
| QCoreApplication | 37/8 | 5/1 | 8/3 | 13/13 | 5/3 | 0 |
| QScriptEngine 与 QScriptValue | 0 | 0 | 214/12 | 0 | 0 | 0 |
| QOpenGLContext 与 QOffscreenSurface | 8/3 | 4/1 | 25/6 | 0 | 21/4 | 0 |

统计方法：按各库的源码目录计，tnzcore 取 `tnzcore/CMakeLists.txt` 引用的 `common/` 子目录，不含 `include/` 下的头文件。

`common/` 下完全无 Qt 的子目录：trop、twain、tgeometry、tcolor、timage、traster、tmeshimage、tmetaimage、ttest、tunit、txsheet。stdfx 203 个 .cpp 中 172 个没有直接的 Qt 标识符。

去 Qt 难度分级：

- 小：tnzext（仅互斥锁与 QString）、colorfx（仅 translate）、sound（QProcess 调 ffmpeg）、tnzbase 的大部分、tnzcore 的算法目录、stdfx 的大部分。
- 中：tnzcore 的基础设施（TFilePath、tstring、tsystem、tenv、TThread、TImageCache、tfont_qt、qtofflinegl、tsound_qt、tipc、tmsgcore、TUndoManager）；TRenderer；image。
- 大：toonzlib 的模型层（QObject 级别、Handle、Preferences、脚本绑定、`orientation.cpp` 的 UI 几何、QPainter 渲染）。

### 8.4 字符串与转换

`TFilePath::m_path`、`TXshLevel::m_name`、调色板和样式名是 `std::wstring`；`TStageObject`、`TParam` 的名字是 `std::string`。QString 出现在 `TFrameId::m_letter`、`TUndo::getHistoryString`、`TColorStyle::getDescription`、`TSystem` 的返回值、`TIStream`/`TOStream` 的运算符、整个 `Preferences`。

QString 与 std 字符串的转换调用（行数）：toonz 1,184，toonzqt 371，toonzlib 206，common 126，tnztools 105。`TNZCORE_LIGHT` 的非 Qt 回退存在于 16 个文件但没有任何 CMakeLists 定义它，是死代码。

### 8.5 tcomposer 的输出格式

渲染输出格式，始终可用：png、tga、tif、sgi/rgb、exr、spritesheet、jpg、bmp、nol。仅当外部 ffmpeg 存在：webm、gif、mp4、apng、mov。仅 Windows：avi。另有注册了 writer 但 `isRenderFormat` 为 false、不出现在渲染输出列表里的 Toonz 原生格式：pli、svg、tlv/tzl、tzp/tzu、plt、psd、mesh、tzm，其中 pli、svg、mesh、tzm 的 writer 接收的是矢量、网格或元数据图像，不是光栅帧。

### 8.6 可复用的第三方测试图像

`thirdparty/libjpeg-turbo/libjpeg-turbo-2.0.6/testimages`、`thirdparty/libpng-1.6.21/contrib/testpngs`（104 个 png）、`thirdparty/tiff-4.0.3/test/images` 可作为图像 I/O 单测的输入。
