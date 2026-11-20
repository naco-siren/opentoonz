# NextToonz 仓库与构建布局：现状与现代化方案

版本 0.1，2026-11-19。本文是一份设计稿，不含实现：盘点仓库与本机构建目录现在的样子，给出目标布局和一张可以逐步合并的迁移表，最后是拟加进 `doc/nexttoonz_plan.md` 的路线图条目和需要所有者决定的问题。

调查基线：分支 `nexttoonz/phase0`，提交 `84f17b336`；Phase 2 分支 `wip/phase2-noqt`，提交 `dbd8118e7`（在 `84f17b336` 上 51 个提交，未推送）；上游 `upstream/master`，提交 `075e0329c`（2026-11-19）。标着“实测”的数字来自 git 命令或 `du`，测量方法列在附录；“模拟”是在草稿目录里的一次性克隆上做的搬家加合并，没有碰版本库里的任何分支。本文与同时进行的 `doc/nexttoonz_cli_ci.md`（命令行与 CI/CD）有交集，交集只在第 6 节列出，不替它做决定。

---

## 目录

1. 结论摘要
2. 现状（实测）
3. 约束
4. 目标布局
5. 迁移步骤
6. 与 CLI/CI 文档的交点
7. 路线图条目（拟加进计划的原文）
8. 需要所有者决定的问题
9. 风险与未验证事项
10. 附录：测量方法

---

## 1. 结论摘要

- **最乱的不是继承的源码树，而是我们自己堆出来的忽略目录。** 主检出里不进版本库的东西约 10 GB（`toonz/build-phase0` 3.0 GB、`toonz/build-phase1` 5.7 GB、两个 Phase 0 之前的构建目录 0.55 GB、`.claude/worktrees` 0.9 GB），是版本库工作树的 30 多倍。按阶段命名的目录（`build-phase0`、`build-phase1`）早已不是“某个阶段的构建”，而是缓存、工作树、对照工具、对照输出、app 快照、虚拟机脚本的混合体；Phase 2 的工作树就嵌在一个看上去可以删掉的构建目录里。这些都能在不碰源码树的情况下整理掉，是第 0 步。
- **目录搬家本身对合并上游几乎没有代价，代价在 CMake 里的相对路径。** 实测：把上游一年前的树整体搬家，再合并之后一年的 336 个上游提交（817 个文件），**内容冲突为 0**，只有 54 到 69 个“上游新加的文件该放到哪”的提示（git 已经自动放到搬家后的位置，确认即可）和 1 对上游自己也挪过的文件。把 Phase 2 分支合进一棵搬过家的树，同样 0 个内容冲突。真正会冲突的是我们为搬家改写的 CMake 路径行：继承的 CMakeLists 用 `../common/...`、`../include/...`、`../../sources/...` 列源文件，上游一年有 36 个提交改 CMakeLists。
- **建议的目标**：源码按层分成 `core/`（不链接 Qt 的库）、`ui/`（Qt 桌面库）、`apps/`（可执行程序），公共头按库分目录但文件名不变（`#include` 行一行不动），测试、开发者工具（`tools/`）、运行时资源（`stuff/`，原名保留）、第三方库各在顶层；仓库根有 `CMakeLists.txt` 和按用途的 `CMakePresets.json`；机器本地的一切在一个忽略的根 `local/` 下，按“是什么、什么版本”命名，不按阶段命名；构建目录统一为 `build/<预设名>/`。
- **时机分三段，与 Phase 2 和上游错开**：现在就做第 0 到 3 步（本机整理、忽略规则、工具入库、审阅图片入库），它们不碰 Phase 2 改的源文件；Phase 2 第 1 到 8 步的里程碑合并之后，在 Phase 2 两步之间做 CMake 解耦、预设、根入口和测试目录上移（第 4 到 7 步）；核心库搬进 `core/` 放在 Phase 2 收口（第 20 步，D9 的分叉点）；`ui/` 与 `apps/` 放在 Phase 3 里程碑一合并上游 Qt6 通道之后。
- 之后合并上游统一用 `-X find-renames=25%`：实测上游 Qt6 草案 #6919 合进搬过家的树时，默认阈值下有 7 个文件退化成“修改/删除”冲突，降到 25% 后是 2 个，与不搬家时（1 个）基本相同。

---

## 2. 现状（实测）

### 2.1 继承的树

版本库跟踪 12,059 个文件。按顶层目录：

| 目录 | 文件数 | 大小 | 内容 |
|---|---|---|---|
| `thirdparty/` | 5,785 | 196 MB | 第三方库源码与预编译包；其中 67 个预编译二进制共 111 MB，几乎都是 Windows 用的（55 个 `.lib`/`.dll` 走 LFS） |
| `toonz/` | 4,458 | 75 MB | `toonz/sources/`（全部源码，4,446 个文件）、`toonz/cmake/`（5 个 Find 模块与 plist 模板）、`toonz/install/`（7 个打包脚本与 `SystemVar.ini`，最后一次改动 2018 年） |
| `stuff/` | 1,760 | 28 MB | 运行时资源（TOONZROOT）：`library` 20 MB、`doc` 6 MB、`config` 1.9 MB、`profiles` 0.9 MB |
| `doc/` | 28 | 0.5 MB | 上游的构建文档（多语言 10 份）、测试 PR 指南与 8 张截图；我们的 `nexttoonz_*.md` 5 份 |
| `plugins/` | 11 | 76 KB | 插件 SDK 示例 |
| `.github/`、`ci-scripts/` | 7、4 | | 5 个 workflow；`ci-scripts/skia/`（取 Skia）、`ci-scripts/windows/` |
| 根目录 | 6 | | `README.md`、`appveyor.yml`（上游的 AppVeyor Windows CI）等 |

`toonz/sources/` 下是一层平铺的 28 个目录，库、程序、头文件、测试、翻译混在同一层：

| 类别 | 目录（文件数） |
|---|---|
| 核心库 | `common`（225，tnzcore 与 tnzbase 的实现都在这里）、`tnzcore`（1，只有 CMakeLists）、`tnzbase`（20）、`tnzext`（39）、`toonzlib`（194）、`image`（101）、`sound`（14）、`colorfx`（13）、`stdfx`（307） |
| Phase 1 新加的库 | `tnzcanvas`（3）、`tnzskia`（2）、`tnzglcanvas`（2）；Phase 2 分支还加了 `tnzglview` |
| UI 库 | `toonzqt`（459）、`tnztools`（227） |
| 程序 | `toonz`（1,457，主程序，含 538 个图标和 502 个资源文件）、`tcomposer`、`tconverter`、`tcleanupper`、`toonzpreview`、`toonzfarm`（72）、`t32bitsrv`（8）、`stopmotion`（14，编进主程序）、`mousedragfilter`、`superlu41`、`xdg-data` |
| 公共头 | `include`（623）：平铺 186 个，子目录 `toonz` 134、`toonzqt` 88、`tcg` 65、`tw` 45、`tools` 35、`ext` 30、`toonz4.6` 24、`tlin` 8、`canvas` 5、`stdfx` 3 |
| 测试 | `tests`（410，我们的，见 2.3） |
| 翻译 | `translations`（79，10 MB，`.ts` 源）、`translations_docs`（152，16 MB，特效说明的 LaTeX，没有任何构建或脚本引用，上游最后一次改动 2022 年） |
| 散件 | `CMakeLists.txt`（820 行）、`CMakePresets.json`、`beautification.sh`（对改动行跑 clang-format） |

**生成的东西写进了源码树**：

- 翻译开着时，`.qm` 写到 `${CMAKE_BINARY_DIR}/../../stuff/config/loc`（`toonz/sources/CMakeLists.txt` 736、739 行），也就是假定构建目录在 `toonz/<某目录>`，产物落进版本库的 `stuff/`，靠 `.gitignore` 的 `/stuff/config/loc` 藏起来。
- 自带的 libtiff 要在源码目录里 `./configure && make`（CI 的 Linux、macOS、清洁剂三个 job 都这样做；每个新工作树都要先做一次），产物混在 `thirdparty/tiff-4.0.3` 的 543 个被跟踪文件中间，靠 `.gitignore` 的 `/thirdparty/tiff-4.0.3` 藏起来。
- Windows 上 CMake 把 `pnglibconf.h`、`tif_config.h`、`tiffconf.h` 复制进 `thirdparty/`（161 到 169 行）。
- 参考工程 `tests/fixtures/reference_project` 和基线 `tests/golden/expected` 是生成后提交的，这是有意的（见 2.3）。

**死的或只属于 Windows、遗留功能的**：`translations_docs/`（无引用）；`appveyor.yml`（上游的 AppVeyor，两年 24 个上游提交，本 fork 的 workflow 只是在 `paths-ignore` 里排除它）；`t32bitsrv`（Phase 2 第 13 步删除）；`include/toonz4.6`（11 个文件仍在包含）；`thirdparty/quicktime`、`thirdparty/canon` 各 8 KB 的头；Windows 预编译包 111 MB（glew、glut、openblas、superlu、zlib、libpng、libtiff、libjpeg-turbo、lzo、lz4 的 `.lib`，libmypaint 的 `dist`）。`toonzfarm`、扫描仪、stopmotion 的去留是计划里的待定事项，本文不处理。`toonz/install/` 不是死的：`toonz/CMakeLists.txt` 671 行把其中的 `SystemVar.ini` 复制进 macOS 包。

### 2.2 CMake 结构与预设

- 入口是 `toonz/sources/CMakeLists.txt`（`project(OpenToonz)`，最低 CMake 3.10），仓库根没有 CMakeLists。各库的 CMakeLists 用相对路径列源文件和头：`tnzcore/CMakeLists.txt` 里 252 处 `../`（源文件实际在 `../common/...`），`toonzlib` 134 处、`tnzbase` 108 处、`toonzqt` 88 处。
- 依赖目录名和目录深度的写法：
  - `SDKROOT` 是 `../../thirdparty/`（149 行）；
  - `${CMAKE_SOURCE_DIR}/../../stuff` 共 4 处（`tests/golden`、`tests/toonzlib`、`toonz/CMakeLists.txt` 两处）；
  - `toonz/CMakeLists.txt` 495 到 510 行用 `../../sources/toonzqt` 这类写法，假定上一级目录**名叫** `sources`；
  - `${CMAKE_SOURCE_DIR}/../install/SystemVar.ini`、`${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../../../qss`；
  - `.qm` 的输出路径假定构建目录深两级（上一节）。
- `CMakeLists.txt` 里 `CMAKE_SOURCE_DIR` 共 13 处。在 `toonz/sources` 之上再加一层根 CMakeLists，这 13 处都要改成项目自己的根变量。
- 预设 `toonz/sources/CMakePresets.json`（版本 3，CMake 3.21）：`base`（隐藏）、`release`、`debug`、`asan`、`macos-brew`。构建目录是 `${sourceDir}/../build`（即 `toonz/build`），`asan` 是 `toonz/build-asan`。CI 只有清洁剂 job 用预设；Linux、macOS、Windows job 都是 `cd toonz && mkdir build && cmake ../sources ...` 手写参数。本机的实际构建都不用预设：OpenCV、skia-pack、Python 的路径和历史上的 libtiff 绕行参数手打进各自的 CMakeCache。
- 上游 #6919（Qt6 草案）也加了一个 `toonz/sources/CMakePresets.json`：与我们的是 add/add 冲突，不论布局怎么定，合并它时都要处理。

### 2.3 测试、夹具与基线

`toonz/sources/tests/`（410 个文件，7.7 MB）：

| 路径 | 内容 |
|---|---|
| `core/`、`toonzlib/`、`canvas/`、`canvas_skia/`、`canvas_gl/` | GoogleTest 单测，按被测库分（Phase 2 分支在 `core/` 下又加了 `legacy/` 差分对照用的旧实现） |
| `skia_spike/` | Phase 1 的 Skia 试点，默认不编 |
| `golden/` | 驱动 `run_golden.py`、比较 `compare_images.py`、`contact_sheet.py`；基线 `golden/expected/<场景>/`，30 个目录 286 个文件，5.6 MB |
| `fixtures/` | 参考工程生成器 `gen_reference_project.cpp` 和生成后提交的 `reference_project/`（74 个文件，1.5 MB） |
| `ios_gtest.sh`、各目录的 `run_ios.sh` | 在 iPad 模拟器里跑单测 |
| `lsan.supp`、`README.md` | |

基线、夹具、单测都在版本库里，跟着代码走，这一部分本身是健康的。问题在它的外围：README 要求每台机器手动往 `.git/info/exclude` 加一行 `/samples/`；“打开参考工程看一看”要先复制到不进版本库的 `samples/`。上游也开始在树里放测试：`toonzqt/tests`、`toonzqt/pixelparam_tests` 已在上游 master，#6919 又加了 `toonz/sources/tests/scriptengine/`（70 个文件），正好落在我们的测试目录里。

### 2.4 CI、脚本和文档里的路径

| 字符串 | 出现 | 分布 |
|---|---|---|
| `toonz/sources` | 26 个文件 57 行 | clang-format workflow 11 行（路径过滤）、清洁剂 workflow 4 行、上游构建文档 13 行、`CONTRIBUTING.md` 4 行、3 个插件的 CMakeLists、`toonz/install/package_sdk.sh` 3 行、我们的文档 8 行 |
| `toonz/build` | 11 个文件 53 行 | Linux、macOS、清洁剂 3 个 workflow（构建、上传 golden 产物、打包）、5 份上游构建文档、`nexttoonz_canvas_ir.md`、测试 README、翻译 README |
| `../../` 相对路径 | `translations/update.sh` 7 行 | lupdate 从 `translations/` 往上找源码 |

Windows workflow 用反斜杠写 `cd toonz\build`、`..\..\thirdparty\...`，而它只在推 master、PR 和手动触发时跑，路径改错了推分支看不出来。

### 2.5 我们自己堆出来的布局

主检出 `/Users/naco_siren/Workspace/opentoonz` 里不进版本库的部分（实测，`du`）：

| 路径 | 大小 | 是什么 |
|---|---|---|
| `toonz/build` | 399 MB | 2026-10-07 配置的 x86_64 Release，Phase 0 开工之前 |
| `toonz/build-arm64` | 148 MB | 2026-10-05 的 arm64 交叉构建 |
| `toonz/build-phase0` | 3.0 GB | Phase 0 的构建（Phase 1 对照时的“改动前”），另外塞着：`venv/`（66 MB，golden 脚本要的 numpy、Pillow，其他会话也在用）、`deps/`（384 MB，静态 OpenCV 4.10 的源码、构建和安装）、`base-src/`（293 MB，一个分离 HEAD 在 `77d43ea1f` 的 **git 工作树**，2026-10-09 查 arm64 空白帧时的“修复前”）、`base-build/`（488 MB，它的构建）、`golden/`（1.1 GB）、`verify/`（439 MB，`TcBase.app`、`TcVerify.app`）、`review/`（32 MB，接触印样）、`vm/`（4 个 lima 脚本：provision、build、sync、test），顶层还有 18 个 `*.log` |
| `toonz/build-phase1` | 5.7 GB | 名为 Phase 1，实际是：`src/`（4.0 GB，**Phase 2 分支 `wip/phase2-noqt` 的工作树**，里面又有 4 个构建目录 `toonz/build` 3.5 GB、`build-noqt`、`build-noqt-ci`、`build-noqt-tsan`）、`ab/`（833 MB，见下）、`skia-pack/`（543 MB，JetBrains 预编译 Skia m144）、`vcpkg/`（345 MB，钉在 `ec48c11e`）、`vcpkg-bincache/`（51 MB）、`ios-work/`（23 MB）、`vm/`（37 个 lima 脚本） |
| `samples/` | 6.3 MB | 参考工程的工作副本 1.5 MB；`review/step2` 到 `step9` 共 4.8 MB 的对照图 |
| `.claude/worktrees/` | 900 MB | Claude 会话的工作树，每个 300 MB，由应用管理 |
| `~/.lima/nt-asan` | 28 GB | Linux 虚拟机（在仓库外，记忆里写的是 9 GB，已经涨了） |

`toonz/build-phase1/ab/` 有 72 项：27 个源文件和脚本（约 1,700 行：`ab_*.cpp`、`run_*.sh`、`cmpdirs.py`、`raw2png.py`、`sbs3.py`、`skia_metrics.py`、`gltrace/` 等），10 个 `last_*.log`，其余是输出目录（`out_before`、`rout_after3`、`tout_after` 之类，合计约 450 MB）。其中 `ab_offscreen.cpp`、`ab_regions.cpp`、`ab_strokes.cpp`、`ab_styles.cpp`、`ab_vector.cpp` 在 `../vm/` 里各有一份 Linux 用的副本。`ab/phase2/`（382 MB）是 Phase 2 的：`ab_tcomposer.py`、README，`bundles/` 下两个 app 包快照（`before_phase2_84f17b336`、`after_tidy_0d2f9497c`，131 MB），`d0`、`d8`、`glseams`、`qtadapters`、`tpl`、`translations` 是各步的对照输出（约 250 MB），`manual/ACCEPTANCE.md` 是给所有者的人工验收清单。

版本库里的文档引用这些不进版本库的东西（实测，`git grep`）：`nexttoonz/phase0` 上 `samples/` 14 行、`vm/` 5 行、`skia-pack` 22 行、`ios-work` 4 行；Phase 2 分支上另有 `build-phase1` 15 行、`ab/` 14 行、`vm/` 9 行。画布 IR 文档 8.1 节写明“脚本和程序不在版本库里，做法记在这里”，每一步“默认路径没有变”的证明都靠这些脚本。

### 2.6 痛点

1. **路径不说它是什么。** `toonz/build-phase1/ab/phase2/bundles/after_tidy_0d2f9497c/OpenToonz.app` 七层里有两层是阶段名，一层是工具名缩写；这是一个 app 快照，却放在“Phase 1 的构建目录”下。`toonz/build-phase1/src` 是 Phase 2 的工作树。
2. **按阶段命名的目录活得比阶段长。** `build-phase0` 里的 venv 和 OpenCV 是所有后续工作的依赖；Phase 1 合并后 `build-phase1` 成了 Phase 2 的家。删掉“旧阶段的构建目录”会拆掉现在的工作。
3. **工作树嵌在看上去可删的构建目录里。** `toonz/build-phase0/base-src` 和 `toonz/build-phase1/src` 都是 git 工作树；`rm -rf toonz/build-phase1` 会删掉一棵有 51 个未推送提交的工作树的检出（提交本身在公共的 `.git` 里，但未提交的改动、四个构建目录和工作树登记一起没了）。
4. **缓存、成果、垃圾混在一起。** 可以随时重下的（skia-pack、vcpkg 缓存）、不能重来的（“改动前”的 app 快照、所有者看过的对照图）、纯粹的输出（`rout_after3`）在同一层，没有一个目录可以放心整个删。
5. **工具不进版本库。** 每一步迁移的验收方法依赖的约 70 个脚本和程序（`ab/` 27 个、`ab/phase2` 1 个、`gltrace` 1 个、两个 `vm/` 共 39 个）不在版本库里，其中 5 个 `ab_*.cpp` 有两份副本，只存在于这一台机器；文档引用它们却无法复现。
6. **版本库文档引用不进版本库的图。** 所有者据以拍板的对照图（Q2 线重、第 3 到 9 步）只在主检出的 `samples/review/`；换一台机器、或者在 GitHub 上读文档，链接全是断的。
7. **构建目录的位置被 CMake 绑死。** 翻译产物写到 `构建目录/../../stuff`，预设写死 `toonz/build`，`toonz/CMakeLists.txt` 假定上级目录叫 `sources`。构建目录不能随意放，源码树也不能随意挪。
8. **每台机器、每个工作树都要手工准备。** `.git/info/exclude` 加一行；在源码里编 libtiff；手打 OpenCV、skia-pack、Python 路径；PATH 里 Python 3.10 要在 `/usr/bin` 前面、`/usr/bin` 又要在 Homebrew llvm 前面。这些只记在会话记忆里。
9. **`.gitignore` 的 `build*` 太宽。** 它忽略任何路径段以 build 开头的文件，例如一个名叫 `build_noqt.sh` 的脚本会被悄悄忽略；Phase 2 的工作记录里专门提醒过这一点。
10. **源码树平铺。** 28 个目录一层，从目录看不出哪些是核心、哪些是 UI、哪些是程序；623 个公共头在一个 `include/` 里，任何库都能包含任何头，分层只能靠测试里扫源文件（`canvas.sources_are_self_contained`）和 Phase 2 的“无 Qt 构建”来检查。

---

## 3. 约束

### 3.1 上游合并（D9）：搬家到底贵不贵

上游的活跃度（实测，非合并提交）：2022 年 240、2023 年 265、2024 年 144、2025 年 490、2026 年到 11 月 19 日 297。最近两年 847 个提交，触及 2,796 个不同的文件。按目录的触及次数（一个提交改一个文件算一次，共 4,647 次）：

| 目录 | 次数 | 占比 |
|---|---|---|
| `toonz/sources/toonz` | 1,909 | 41% |
| `thirdparty` | 525 | 11% |
| `stuff` | 440 | 9% |
| `toonz/sources/toonzqt` | 354 | 8% |
| `toonz/sources/tnztools` | 296 | 6% |
| `toonz/sources/include` | 286 | 6% |
| `toonz/sources/toonzlib` | 259 | 6% |
| `toonz/sources/translations` | 124 | 3% |
| `common`、`image`、`stdfx`、`tnzbase`、`tnzext` 等其余核心库 | 约 220 | 5% |
| `.github` | 68 | |

847 个提交里 395 个（47%）只改 UI 一侧（`toonz`、`toonzqt`、`tnztools`、`translations`、`stopmotion`、`stuff`），287 个（34%）碰到核心库或其头文件。我们与上游的交集：我们改过的 735 个文件里有 110 个在上游这两年也改过，集中在 `common`（21）、`tnztools`（16）、`toonzlib`（12）、`include`（11）。实际合并的体验：2026-11-04 合并 84 个上游提交，重放出来 0 个冲突；现在 `upstream/master` 比合并基多出的 13 个提交，合进 `nexttoonz/phase0` 和 `wip/phase2-noqt` 都是 0 个冲突。

**模拟一：搬家后合并一年的上游。** 从上游一年前的提交 `60c92b3d3`（2025-11-19）开一个分支，做一次纯搬家（`git mv`，不改内容），再合并到 `075e0329c` 为止的 336 个上游提交（817 个文件，其中 108 个新增），`merge.renameLimit=0`：

| 搬法 | 内容冲突 | “新文件放哪”的提示 | rename/rename |
|---|---|---|---|
| A：`toonz/sources/*` 整体搬到 `src/` | 0 | 69 | 1 对（2 个文件） |
| B：库搬到 `libs/`、程序搬到 `apps/`（`translations` 不动） | 0 | 54 | 1 对 |
| C：B 再把 `include/toonz`、`include/toonzqt`、`include/tools` 和平铺的头分到各库的 `include/` | 0 | 54 | 1 对 |

“新文件放哪”（`CONFLICT (file location)`）是 git 发现上游往一个被搬走的目录里加了文件，已经按目录改名推断放到了新位置，只要求人确认（`git add`）。rename/rename 那一对是 `insertfxpopup.{h,cpp}`：上游自己把它从 `toonz/` 挪进了 `toonzqt/`，与我们的搬家撞在一起，要手选一个位置。**修改已有文件的上游提交全部自动合进了新位置。**

**模拟二：把 Phase 2 分支合进搬过家的树。** 在 `nexttoonz/phase0` 上做 A 或 B，再合并 `wip/phase2-noqt`（51 个提交，271 个文件）：两种都是 0 个内容冲突、63 个“新文件放哪”。

**模拟三：上游 Qt6 草案 #6919。** 它有 170 个提交、541 个文件（基于 2026-07-12 的上游），Phase 3 开头要合并。合进今天的 `nexttoonz/phase0`，**不搬家**也有 32 个文件冲突（30 个内容冲突，`stdfx/changecolorfx.cpp` 一个修改/删除，`CMakePresets.json` 一个 add/add），合进 `wip/phase2-noqt` 是 47 个。这些冲突来自它的基点落后上游四个月和我们自己的改动，与布局无关。在 B 布局上合并它：默认改名阈值（50%）下是 26 个内容冲突、7 个修改/删除、5 个“放哪”；`-X find-renames=25%` 下是 29 个内容冲突、2 个修改/删除、5 个“放哪”。多出来的修改/删除是两边都大改过的文件相似度掉到 50% 以下、改名配不上对；降低阈值后与不搬家基本持平。#6919 还新加了顶层 `scripts/`（56 个文件：`qt6` 49、`macos` 4、`windows` 2、`nix` 1）和 `toonz/sources/tests/scriptengine/`。

**模拟没有覆盖的部分：CMake 路径改写。** B 和 C 的模拟只做了 `git mv`，没有改 CMakeLists 里的相对路径，所以那两棵树其实配置不起来。搬家之后这些路径行必须改，改过的行会和上游对同一处的改动冲突。上游这一年有 36 个提交改 CMakeLists：`toonz/CMakeLists.txt` 15 个、根 `CMakeLists.txt` 10 个、`toonzqt` 和 `tnztools` 各 4 个、`toonzlib` 和 `stdfx` 各 3 个。一个上游提交通常是往源文件列表里加一行，冲突的处理是“把这一行按新路径加上”，机械但每次都要人看。估计：只搬核心库时每年约 6 到 10 次这样的冲突（核心库的 CMakeLists 改得少），连 UI 和程序一起搬时约 35 次（根和 `toonz/` 的 CMakeLists 是上游最常改的两个）。

**结论。** 目录搬家本身对合并上游几乎没有代价，代价集中在两处：CMake 路径改写的行，以及忘记降低改名阈值。所以分叉的时机不必由“搬家”本身决定，而由两件事决定：库的边界什么时候定型（Phase 2 还在移文件、新建 `tnzglview`、拆源文件），和上游最常改的 CMakeLists 什么时候不再需要频繁合并（Phase 3 里程碑一合完 Qt6 通道之后，D9 已经不承诺可合并）。

### 3.2 Phase 2 在飞

`wip/phase2-noqt` 在 `84f17b336` 上 51 个提交、271 个文件（新增 23,933 行），第 1 到 8 步已经整理成 `tidy/phase2-steps1-8`（32 个提交），等所有者决定里程碑合并；之后还有第 9 到 21 步，每一步都改核心库的源文件、头和 CMakeLists（`<库>_NOQT_SOURCES` 列表到第 20 步才删）。它还改了下列本文也要改的共享文件：根 `toonz/sources/CMakeLists.txt`（120 行）、`toonz/sources/tests/README.md`（55 行）、`workflow_macos.yml`（70 行）、`ci-scripts/skia/skia_pack.sh`，并新增 `ci-scripts/noqt/`。所以：

- 不碰这些文件的步骤（本机整理、工具入库、审阅图片）现在就能做。
- 改这些文件的步骤放在 Phase 2 第 1 到 8 步合并进 `nexttoonz/phase0` 之后，并且每一步在 Phase 2 的两步之间合并，Phase 2 的下一步从合并后的 `nexttoonz/phase0` 开始。
- 核心库搬家放在 Phase 2 第 20 步之后：那时库边界定型、无 Qt 的源文件列表已删，搬一次就是最终位置。

### 3.3 路径引用

搬一个目录，要一起改的东西列在 2.2 和 2.4：CMakeLists 的相对路径、预设、3 个 workflow 加 clang-format 的路径过滤、`ci-scripts`、测试与翻译的 README、上游的构建文档（多语言 10 份，上游也在改，留到分叉后统一改写或删掉）、我们自己的文档、会话记忆里的路径（5 份记忆文件写着 `toonz/build-phase0`、`toonz/build-phase1`）。

### 3.4 平台

第一目标是 x86_64 macOS 加 iPadOS；Linux（gcc、clang、Fedora、清洁剂）和 Windows 的 CI 要一直绿，但不是产品目标。所以本机工具只为这台 Intel Mac 和 lima 里的 Linux 虚拟机设计；Windows 只需要“CI 还能编”，它的预编译包不动。Windows workflow 推分支不触发，凡是改到它路径的步骤都要手动触发一次（Phase 2 已经给它加了 `workflow_dispatch`）。

---

## 4. 目标布局

### 4.1 总图

迁移全部完成后（Phase 3 里程碑一之后）的样子：

```
/                         仓库根
├── CMakeLists.txt        唯一入口
├── CMakePresets.json     按用途的预设（4.9）
├── core/                 不链接 Qt 的库（Phase 2 的完成判据所指的那些）
│   ├── tnzcore/          今 common/ 里属于 tnzcore 的子目录，加 include/（它的公共头，文件名不变）
│   ├── tnzbase/  tnzext/  toonzlib/  image/  sound/  colorfx/  stdfx/
│   ├── canvas/           今 tnzcanvas
│   ├── canvas-skia/      今 tnzskia
│   └── engine/           引擎 API（Phase 2 第 11 步）；C ABI（Phase 4）放在它旁边
├── ui/                   Qt 桌面一侧的库：toonzqt/  tnztools/  glview/（tnzglview 与 tnzglcanvas）
├── apps/                 opentoonz/（今 toonz/）  tcomposer/  tconverter/  tcleanupper/  farm/  …
├── platform/             平台壳与打包：macos/（今 toonz/cmake 的 plist、toonz/install）、ios/（Phase 4）
├── tests/                unit/<库>/、golden/（含 expected/）、fixtures/、ios/
├── tools/                开发者工具（4.5）
├── stuff/                运行时资源，名字不变
├── thirdparty/
├── plugins/
├── doc/                  含 doc/review/（4.7）
├── .github/
│
├── build/                忽略：本检出的构建目录，build/<预设名>/
└── local/                忽略：本机的缓存、快照、对照输出（4.8），只在主检出里有
```

### 4.2 源码树

| 方案 | 做法 | 好处 | 代价 |
|---|---|---|---|
| S1 不动 | 永远是 `toonz/sources/` 平铺 | 零成本，与上游一致 | 痛点 10 不解决；改名为 NextToonz 之后顶层还叫 `toonz` |
| S2 整体平移 | `toonz/sources/` 整体改名为 `src/`，内部不变 | CMake 内部的相对路径大多不用改（只改 2.2 列的那十几处），合并上游最便宜 | 只换了个名字，平铺、分层看不出来这两个问题都在 |
| **S3 按层分组（建议）** | `core/`、`ui/`、`apps/`，每个库自带公共头 | 目录即分层：D1、D10 的“核心不链接 Qt”从目录上就能看出，配合 4.3 由编译器强制；iPad 只需要 `core/` | 每个库的 CMakeLists 都要改写路径；分两次做（核心、UI）才能与 Phase 2 和 Qt6 合并错开 |

另一种常被提议的做法是“新代码进新的顶层树，旧树冻结”：例如 `tnzcanvas`、引擎 API 现在就建在 `core/` 下，`toonz/sources` 不再动。不建议：新库都很小（Phase 1 的三个库合计 7 个文件），却要为它们在两棵树之间搭 CMake 胶水、维持两套约定，持续到分叉；而 3.1 的测量说明旧树并不是“搬不动”，只是“现在搬还会再搬一次”。在分叉之前，新库仍按现在的约定放在 `toonz/sources/` 下，分叉时随核心库一起搬。没有继承对应物、也不参与 CMake 构建的东西（`tools/`、`doc/review/`）现在就放到最终位置。

### 4.3 公共头

| 方案 | 做法 | 评价 |
|---|---|---|
| H1 不动 | 一个 `include/`，623 个头 | 任何库都能包含任何头，分层靠约定 |
| **H2 按库分目录、文件名不变（建议）** | 每个头放进拥有它的库的 `include/`，`target_include_directories(<库> PUBLIC …)`；18,915 行 `#include` 一行不改 | 一个核心库的包含路径里根本没有 `ui/` 的目录，越层包含直接编不过，`sources_are_self_contained` 那类扫描可以退役一部分。归属可以机械判定：头文件里的导出宏（`TNZCORE_EXPORT`、`TOONZLIB_EXPORT` 这类 `DVAPI` 切换）说明它属于哪个库；`include/toonz` 归 toonzlib、`toonzqt` 归 toonzqt、`tools` 归 tnztools、`ext` 归 tnzext；`tcg`、`tlin`、`tw` 这类只有头的小库各自成目录 |
| H3 带前缀包含 | `#include <tnzcore/tfilepath.h>` | 最规范，但要改写约 19k 行包含，与上游的每个提交都冲突；否决 |

`include/toonz4.6`（24 个头，11 个使用者）在 H2 下归到使用它的库，或者在分叉时清理。

### 4.4 测试、夹具与基线

- 测试上移到顶层 `tests/`：`unit/<库名>/`（今 `core/` 对应 tnzcore，`canvas/`、`canvas_skia/`、`canvas_gl/`、`toonzlib/` 按库名改）、`golden/`（驱动脚本和 `expected/`）、`fixtures/`（生成器和 `reference_project/`）、`ios/`（`ios_gtest.sh` 与 `run_ios*.sh` 的公共部分）。`skia_spike/` 是 Phase 1 的试点，建议删除（它的结论已在 `doc/nexttoonz_phase1_skia.md`），见第 8 节。
- 测试上移可以比核心库搬家早得多：测试目录是我们自己的，上游只有 #6919 的 `scriptengine` 会落进来（目录改名推断会把它放进新的 `tests/`）。它要等根入口（第 6 步），因为测试应当从仓库根 `add_subdirectory`，不再挂在 `toonz/sources` 下面。
- 基线和夹具继续以普通 git 文件提交，不进 LFS：它们是测试数据，必须与代码同一个提交；5.6 MB 加 1.5 MB 不需要 LFS，而 LFS 会让 CI 和虚拟机的检出多一个依赖、受配额约束。给基线一个预算：一个场景的基线不超过 1 MB，新场景保持 320x180、帧数少。
- 参考工程的工作副本（今 `samples/reference_project`）是一次性的，放 `local/scratch/`；“不要直接打开夹具”的规矩不变。

### 4.5 开发者工具

顶层 `tools/`。不叫 `scripts/`，因为 #6919 会在顶层加一个上游的 `scripts/`。

```
tools/
├── README.md        每个工具一行：做什么、怎样调用、要什么前置
├── setup/           bootstrap.sh：建 local/，建 venv（numpy、Pillow），经 ci-scripts/skia/skia_pack.sh 取 skia-pack，
│                    编静态 OpenCV，生成 CMakeUserPresets.json（本机路径），检查 PATH 上的 Python 与 clang；
│                    hooks/：可选的 pre-commit（只格式化改动行，同 beautification.sh）
├── ab/              改动前后对照：ab_tcomposer.py、cmpdirs.py、cmpraw.sh、raw2png.py、sbs3.py、skia_metrics.py、
│                    contact sheet；ab/canvas/：Phase 1 的对照程序与 run_*.sh（ab_*.cpp、skia_*.cpp、dbg_*.cpp、dump_style.cpp）
├── gltrace/         libgltrace 的源码
├── vm/              lima：provision.sh、build.sh、sync.sh、test.sh（今 build-phase0/vm），通用的 Linux 对照脚本；
│                    vm/records/：各步一次性的验收脚本（step1.sh 到 step9_after.sh、legacy_*.sh、phase2_step1.sh）
└── upstream/        merge.sh：用 merge.renameLimit=0、-X find-renames=25% 合并上游，列出“新文件放哪”的提示待确认
```

规则：

- 进版本库的只有源码和脚本；输出一律写到 `local/runs/<工具>/<日期>_<标签>/`，脚本不再往自己所在的目录写东西（今天的 `run_*.sh` 要求“从这个目录的副本里跑”，正是因为它们往旁边写 `bin_*/`、`out_*/`）。
- 一次性的验收脚本也入库（放 `records/`），因为设计文档用它们证明“默认路径没有变”；它们不必保证以后还能跑，但要能读到当时做了什么。
- `ab/` 的程序先原样入库、仍按 README 手工编译；改成 CMake 的可选目标（例如 `WITH_AB_TOOLS`）是后续工作。
- 现在两份的 `ab_*.cpp`（`ab/` 一份、`vm/` 一份）合并为一份，平台差别用参数表达。
- 参考工程生成器留在 `tests/fixtures/`：它是 CMake 目标，产物是测试数据。
- `ci-scripts/` 是否并进 `tools/ci/`，由 CLI/CI 文档决定（第 6 节）。

另一种做法是工具留在机器本地、只把用法写进文档（今天的做法）：已被痛点 5 否定。

### 4.6 运行时资源与第三方库

- `stuff/` 保留名字和位置。它是 TOONZROOT，名字烙在安装布局里（`portablestuff`、`~/.config/OpenToonz/stuff`、`SystemVar.ini`），上游两年改它 51 次；改名应当与产品改名（计划待定事项“改名为 NextToonz 的时机”）一起做，本文不提前做。要改的只有“构建往里写东西”：`.qm` 写进构建目录，再由打包步骤复制进包（第 4b 步）。
- `thirdparty/` 保留在顶层。Windows 预编译包 111 MB 在 Windows 仍要编的前提下不动；Windows 是否作为产品目标定下来之后再议。自带 libtiff 的源码内构建改成在构建目录里构建（第 12 步，可选），之后新工作树不再需要先 `./configure && make`。

### 4.7 文档与审阅材料

我们的文档留在 `doc/`，文件名不变（改名会打断进行中会话的引用，收益很小）。审阅材料是要定的：所有者据以做决定的对照图，今天在 `samples/review/`（4.8 MB，第 2 到 9 步），被版本库里 4 份文档 14 行引用。

| 方案 | 永久性 | 在 GitHub 和 iPad 上看得到 | 克隆和 CI 的代价 | 额外的东西 |
|---|---|---|---|---|
| **R1 版本库里的 `doc/review/`，普通 git，有预算（建议）** | 与提交同生同灭 | 能，链接直接是相对路径 | 每个克隆多几 MB；`doc/**` 被 workflow 的 `paths-ignore` 排除，不触发构建 | 无 |
| R2 版本库里，走 LFS | 同上 | 能 | CI、虚拟机要装 git-lfs；受 GitHub 的 LFS 存储与流量配额约束（上游的 `appveyor.yml` 为了配额跳过了 LFS 下载） | LFS 配额 |
| R3 单独的仓库 `nexttoonz-review` | 取决于那个仓库 | 能，用固定到提交的 URL | 主仓库零代价 | 两个仓库；链接要带提交号；所有者多管一个仓库 |
| R4 Actions 产物 | 最多保留 90 天 | 要登录下载 | 无 | 不适合当决策记录 |
| R5 Release 附件 | 永久 | 能 | 无 | 不能 diff，不随分支走，挂在一个假的发布上 |

建议 R1：`doc/review/<主题>/<步>/`（例如 `doc/review/canvas/step2/`），规矩是：只放所有者要看的成品（三联图、接触印样、放大局部），单张不超过 300 KB，每个主题的总量写进它的 README；原始的逐帧输出放 `local/runs/`，不进版本库。按今天的量，一个阶段几 MB，全部阶段估计几十 MB，相对 `thirdparty/` 的 196 MB 和 `.git` 的 351 MB 可以接受。超出预算时转 R3。

`samples/README.md` 的内容并入 `doc/review/README.md` 和 `tools/README.md`；`.git/info/exclude` 里的 `/samples/` 一行不再需要。

### 4.8 机器本地状态

两层，各一个根：

- **每个检出各自的构建目录：`build/<预设名>/`**，在仓库根，`.gitignore` 里写 `/build/`。主检出、每个工作树都有自己的。这是 CMake 生态里最常见的位置，预设的 `binaryDir` 写成 `${sourceDir}/build/${presetName}`。
- **全机共用的：`local/`**，只在主检出的根，`.gitignore` 里写 `/local/`。工具用 `git rev-parse --path-format=absolute --git-common-dir` 的上一级找到它，所以任何工作树（包括 `.claude/worktrees/` 下的）都指向同一个；环境变量 `NEXTTOONZ_LOCAL` 可以改。

```
local/
├── cache/        可以整个删，tools/setup/bootstrap.sh 能重建
│   ├── skia-pack/m144-e2e6623374-4/{macos-x64,iosSim-x64,ios-arm64}/
│   ├── vcpkg/                      钉在 ec48c11e 的检出
│   ├── vcpkg-bincache/
│   ├── opencv/4.10-static-x64/     只留安装结果
│   ├── venv/                       numpy、Pillow
│   └── googletest/                 给 iPad 模拟器用的源码
├── snapshots/    不能随手删：做对照用的“改动前”
│   └── <YYYY-MM-DD>_<提交>_<说明>/OpenToonz.app
│       例：2026-11-17_84f17b336_before-phase2/、2026-11-19_0d2f9497c_after-tidy/
├── runs/         可以删：对照、手工 golden、逐帧原图
│   └── <工具>/<YYYY-MM-DD>_<说明>/
└── scratch/      可以删：参考工程的工作副本之类
```

命名规则：目录名说它是什么、什么版本、哪个提交，**不说哪个阶段产生了它**；“能删”和“不能删”不在同一个子目录里。

另一种做法是把 `local/` 放在仓库外（例如 `~/Workspace/opentoonz-local/`）：检出更干净，`rm -rf` 检出不会带走快照；但多一个要记的路径，Finder 里也不在一起。两者差别不大，建议放在主检出里，由所有者定（第 8 节 Q2）。无论放哪，Spotlight 和 Time Machine 都应排除 `build/` 和 `local/cache/`。

构建目录不跨位置搬：CMakeCache 里记着绝对路径，搬过去的构建目录要重新配置、全量重编。第 0 步里移动的工作树因此要重编一次。

### 4.9 CMake 入口与预设

- 仓库根加 `CMakeLists.txt`，它设一个 `NEXTTOONZ_ROOT`，再 `add_subdirectory(toonz/sources)`（分叉后改为 `core`、`ui`、`apps`），测试从根挂 `add_subdirectory(tests)`。`toonz/sources` 里的 `CMAKE_SOURCE_DIR`（13 处）和 2.2 列的相对路径改用 `NEXTTOONZ_ROOT`。`project()` 的名字随产品改名再改。
- 预设移到仓库根，`toonz/sources/CMakePresets.json` 删除。附带的好处：#6919 往 `toonz/sources/` 加的同名文件不再与我们的冲突。
- 本机路径不进预设：`tools/setup/bootstrap.sh` 生成忽略的 `CMakeUserPresets.json`，里面的预设继承版本库的预设、只加 `OpenCV_DIR`、`NEXTTOONZ_SKIA_PACK_DIR`、`Python3_EXECUTABLE`。这是 CMake 为这件事设计的机制，取代今天手打进各个 CMakeCache 的参数。

按用途的预设（名字即构建目录名）：

| 预设 | 用途 | 与 base 的差别 |
|---|---|---|
| `dev` | 本机日常：x86_64 macOS，Release，Skia，测试，ccache | `WITH_SKIA=ON` |
| `dev-debug` | 调试 | Debug |
| `gl-tests` | 加 OpenGL 画布后端的测试 | `WITH_CANVAS_GL_TESTS=ON` |
| `noqt` | 不带 Qt 的核心（CI 的 macOS-noqt job 同此） | `WITH_QT=OFF`，`WITH_SKIA=ON` |
| `noqt-tsan` | 核心的 ThreadSanitizer | Debug，`-fsanitize=thread` |
| `asan` | ASan 加 UBSan（Linux、虚拟机；CI 的 Sanitizers job 同此） | 同今天的 `asan` |
| `release` | 出包 | `WITH_TRANSLATION=ON` |
| `ios-sim`、`ios-device` | Phase 4 | 工具链文件 |
| `ci-<job>` | 每个 CI job 一个，继承上面的预设，只加运行器上的差别 | 由 CLI/CI 文档定 |

今天的 `macos-brew` 并入 `dev`。

### 4.10 工作树放在哪

- Claude 会话的工作树留在 `.claude/worktrees/`，由应用管理。
- 人手建的、长期的工作树（今天的 `toonz/build-phase1/src`、`toonz/build-phase0/base-src`）放到主检出**之外**的同级目录：`~/Workspace/opentoonz-wt/<分支名>/`。规则：工作树不放在任何“设计上可以删”的目录里，也不嵌在另一棵检出里面（Spotlight、Xcode 索引、不读 `.gitignore` 的 `find` 都会看到第二份源码）。
- 另一种做法是 `local/worktrees/`：东西都在一个根下，但它把“不能删”的检出放进了以缓存为主的目录，正是痛点 3 的形状，不建议。
- 只为一次对照用的“改动前”的树，优先用 app 快照（`local/snapshots/`）或 `git archive` 出头文件，而不是再开一棵工作树加一个构建目录（`base-src` 加 `base-build` 是 781 MB）。

---

## 5. 迁移步骤

每一步都能单独合并，合并时 CI 是绿的。“性质”一栏：**纯搬**指只移动文件、改引用它们的路径，构建产物不变；**工具**指只增删开发者工具、忽略规则和文档；**行为**指构建或 CI 的结果会变（产物位置、CI 路径）。

| 步 | 内容 | 性质 | 时机 | 风险 | 工作量 | 验证 |
|---|---|---|---|---|---|---|
| 0 | 本机整理（5.1 节），不碰版本库 | 纯搬（仓库外） | 现在；Phase 2 会话空闲时 | 中：在飞的会话按旧路径找 venv、快照、工作树。旧路径留兼容软链接到下一个里程碑 | 0.5 到 1 天，加 Phase 2 工作树全量重编 | 见 5.2 |
| 1 | `.gitignore`：加 `/build/`、`/local/`；`build*` 收窄为 `/build/`、`/toonz/build*/`（过渡期）与原有的几个具体名字 | 工具 | 现在 | 低：收窄可能放出原本被忽略的文件 | 0.5 天 | 改前改后 `git status --porcelain --ignored` 的差别只有预期的几项；CI 绿 |
| 2 | `tools/` 入库（4.5 节）：对照工具、gltrace、虚拟机脚本、`setup/bootstrap.sh`、`upstream/merge.sh`；输出改写到 `local/runs/` | 工具 | 现在（只新增文件，Phase 2 不碰） | 低 | 1 到 2 天 | 在空目录上跑 bootstrap 重建出 `local/cache`；用入库的 `run_sty.sh` 重跑样式总表，70 张与上一次的输出逐字节相同；`tools/vm/provision.sh` 在新建的虚拟机上能跑到 `test.sh` |
| 3 | 审阅图片入库（4.7 节，按 Q1 的决定），文档里 14 行链接改指新位置；`samples/README.md` 并入 | 纯搬 | 现在（`tests/README.md` 那几行等第 1 到 8 步合并后再改） | 低 | 0.5 天 | 一个检查 `doc/**/*.md` 里每个相对链接和图片路径都存在的小脚本（放 `tools/`，以后每次改文档都跑） |
| 4a | CMake 去掉对位置的假设：`NEXTTOONZ_ROOT`，替换 `../../thirdparty`、`${CMAKE_SOURCE_DIR}/../../stuff`（4 处）、`../../sources/<x>`（5 处）、`../install`、`../../../qss` | 纯搬（路径算法变，结果不变） | Phase 2 第 1 到 8 步合并之后、第 9 步开始之前 | 低 | 1 天 | 改前改后的 `compile_commands.json` 与 `build.ninja` 逐字相同（Phase 2 的验法）；Linux、macOS、Windows CI 绿（Windows 手动触发） |
| 4b | `.qm` 不再写进源码树的 `stuff/`，写进构建目录，打包时复制 | 行为（产物位置） | 紧接 4a | 中：Linux 安装树和 macOS 包里的翻译文件位置要对 | 0.5 天 | `WITH_TRANSLATION=ON` 构建的 macOS 包，`Contents/` 下的文件列表和哈希与改动前相同；Linux 安装树同；源码树 `git status --ignored` 不再出现 `stuff/config/loc` |
| 5 | 按用途的预设（4.9 节），`binaryDir` 为 `build/<预设名>`；`CMakeUserPresets.json` 由 bootstrap 生成；CI 改用预设（与 CLI/CI 文档一起定） | 行为（CI 的构建目录与产物路径） | 紧接 4 | 中：workflow 里 53 行 `toonz/build` 路径、产物上传路径 | 1 到 2 天 | 每个 CI job 改前改后编译命令一致（下载两边的 `compile_commands.json`，归一化路径后比较）；全部 job 绿；产物名不变 |
| 6 | 仓库根的 `CMakeLists.txt` 与 `CMakePresets.json`；`toonz/sources/CMakePresets.json` 删除；`CMAKE_SOURCE_DIR` 13 处改为 `NEXTTOONZ_ROOT` | 纯搬 | 紧接 5 | 低 | 1 天 | 从根配置与从 `toonz/sources` 配置（保留一个里程碑）得到相同的编译命令；ctest 列表（`ctest -N`）相同 |
| 7 | 测试上移到 `tests/`，按 4.4 节改子目录名；clang-format workflow 的路径过滤跟着改 | 纯搬 | 紧接 6，在 Phase 2 的两步之间；Phase 2 的下一步从合并后开始 | 低到中：Phase 2 每步都加测试，要协调（模拟二：跨搬家合并 0 个内容冲突） | 1 天 | `ctest -N` 的测试名和标签与改动前相同；全部测试通过；golden 帧逐字节相同 |
| 8 | 清理遗留：`translations_docs/`、`appveyor.yml`、`tests/skia_spike/`、`include/toonz4.6`（若已无用户）、`toonz/sources/beautification.sh`（由 hooks 取代） | 纯删 | Phase 2 第 20 步、D9 分叉时（在那之前上游还在改 `appveyor.yml`，删了每次合并都是修改/删除冲突） | 低 | 0.5 天 | 构建与 CI 绿；`git grep` 无引用 |
| 9 | 核心库搬进 `core/`，头按 H2 分到各库；新增一个检查：核心库的包含路径里没有 `ui/`、`apps/` 的目录 | 纯搬（加一个检查） | Phase 2 第 20 步之后（库边界定型，`<库>_NOQT_SOURCES` 已删）；之后合并上游一律走 `tools/upstream/merge.sh` | 中：改写所有核心库 CMakeLists；搬家提交只含 `git mv`，CMake 改写放紧接的另一个提交，保证改名相似度 100%、`git log --follow` 和 blame 不断 | 3 到 5 天 | 归一化路径后的编译命令集合与改动前相同；全部测试、golden 通过；macOS 包的文件列表相同；无 Qt 构建与 iPad 模拟器测试通过 |
| 10 | UI 库与程序搬进 `ui/`、`apps/`；`toonz/cmake`、`toonz/install` 搬进 `platform/macos/`；`toonz/` 顶层目录消失 | 纯搬 | Phase 3 里程碑一（合并上游 Qt6 通道）之后 | 中：`toonz/CMakeLists.txt` 是上游最常改的文件之一，搬在 Qt6 合并之后，就不再有大块的上游改动要跨搬家合并 | 2 到 3 天 | 同第 9 步；应用人工冒烟（启动、打开参考工程的副本、渲一帧） |
| 11 | 上游构建文档（多语言 10 份）、`CONTRIBUTING.md` 的路径：改写为我们的或删除 | 工具 | 第 10 步之后，或随改名一起 | 低 | 0.5 天 | 第 3 步的链接检查 |
| 12（可选） | 自带 libtiff 在构建目录里构建，不再在源码里 `./configure && make` | 行为（构建方式） | 第 5 步之后任何时候 | 中：三个平台的 CI 都依赖今天的做法 | 1 到 2 天 | CI 绿；新工作树不做准备即可配置；`thirdparty/` 下 `git status --ignored` 为空 |

工作量合计约 2 到 3 人周，分散在 Phase 2 与 Phase 3 之间。第 0 到 3 步是本文能立即带来的大部分可读性收益；第 9、10 步是结构上的收益，按时机等。

### 5.1 第 0 步明细

做之前：确认 Phase 2 会话空闲，它的工作树没有未提交的改动；其他会话不在跑构建或 golden。删除的每一项先列给所有者确认（第 8 节 Q7）。

| 现在 | 大小 | 去处 | 动作 |
|---|---|---|---|
| `toonz/build`、`toonz/build-arm64` | 547 MB | 无 | 删（Phase 0 开工之前的构建，文档和会话记忆里都已不再提到它们） |
| `toonz/build-phase0/base-src`（工作树）、`base-build` | 781 MB | 无 | `git worktree remove`，删构建；`77d43ea1f` 这个提交还在，要时再建 |
| `toonz/build-phase0/venv` | 66 MB | `local/cache/venv` | 重建比搬可靠（venv 里有绝对路径）；旧路径留软链接 |
| `toonz/build-phase0/deps/opencv-install` | 37 MB | `local/cache/opencv/4.10-static-x64` | 搬；`opencv-src`、`opencv-build` 删 |
| `toonz/build-phase0/deps/tiff-ot-include` | | 无 | 删（70fcb48f7 之后不再需要） |
| `toonz/build-phase0/vm` | 4 个脚本 | `tools/vm/`（第 2 步） | 入库 |
| `toonz/build-phase0` 的 `golden`、`verify`、`*.log` | 1.5 GB | 无 | 删 |
| `toonz/build-phase0/review` | 32 MB | `doc/review/phase0/`（挑成品，按预算）或删 | 所有者定 |
| `toonz/build-phase0` 本身（Phase 0 的构建） | 约 0.2 GB | `local/snapshots/<日期>_<提交>_phase0/OpenToonz.app` | 只留 app 包快照；对应的提交先从它的 CMakeCache 和时间确认 |
| `toonz/build-phase1/src`（Phase 2 工作树） | 4.0 GB | `~/Workspace/opentoonz-wt/phase2-noqt` | `git worktree move`；里面的 4 个构建目录重新配置为 `build/{dev,noqt,noqt-ci,noqt-tsan}` 并全量重编 |
| `toonz/build-phase1/ab` 的源码与脚本 | 27 个文件 | `tools/ab/`（第 2 步） | 入库 |
| `toonz/build-phase1/ab` 的输出与日志 | 约 450 MB | `local/runs/ab-canvas/2026-11_phase1/` 或删 | 所有者定 |
| `toonz/build-phase1/ab/phase2/bundles/*` | 131 MB | `local/snapshots/2026-11-17_84f17b336_before-phase2/`、`…_0d2f9497c_after-tidy/` | 搬（app 包可以从任何位置运行） |
| `toonz/build-phase1/ab/phase2` 的其余输出 | 约 250 MB | `local/runs/ab-tcomposer/` | 搬 |
| `toonz/build-phase1/ab/phase2/manual/ACCEPTANCE.md` | | 进版本库：`doc/` 下 Phase 2 的验收清单 | 由 Phase 2 会话在它的分支上做 |
| `toonz/build-phase1/vm` | 37 个脚本 | `tools/vm/` 与 `tools/vm/records/` | 入库（第 2 步） |
| `toonz/build-phase1/skia-pack` | 543 MB | `local/cache/skia-pack/m144-e2e6623374-4/` | 搬（已配置的构建目录引用它，重编时改路径） |
| `toonz/build-phase1/vcpkg`、`vcpkg-bincache` | 396 MB | `local/cache/vcpkg`、`local/cache/vcpkg-bincache` | 搬 |
| `toonz/build-phase1/ios-work` | 23 MB | 无 | 删（随构建重建） |
| `samples/reference_project` | 1.5 MB | `local/scratch/reference_project` | 搬 |
| `samples/review`、`samples/README.md` | 4.8 MB | `doc/review/`（第 3 步） | 先搬到 `local/scratch/review-staging`，第 3 步入库 |
| `.claude/worktrees` | 900 MB | 不动 | 应用管理 |

结果：主检出的忽略部分从约 10 GB 降到约 2.4 GB（`local/` 约 1.5 GB：缓存 1.0 GB、快照与 Phase 2 的对照输出约 0.5 GB；`.claude/worktrees` 0.9 GB），另有约 4 GB 在主检出外的 Phase 2 工作树里；删掉的约 3.8 GB（其中要所有者确认的 3.3 GB 见 Q7，其余是 OpenCV 的源码与构建目录、`ios-work` 和 Phase 0 构建目录里 app 之外的部分）。兼容软链接（`toonz/build-phase0/venv`、`toonz/build-phase1/ab/phase2/bundles` 等指向新位置）保留到下一个里程碑，同时改 5 份会话记忆里的路径。

### 5.2 每一步怎样验证

沿用画布 IR 文档 8.1 节和 Phase 2 文档第 8 节的做法，按步骤性质挑：

- **纯搬**：改前改后各存一份 `compile_commands.json`、`build.ninja`、`ctest -N` 的输出，归一化掉被搬的路径前缀后逐字比较；构建产物（macOS 包 `Contents/` 下的文件列表与哈希）相同。这一项能做到逐字相同，做不到就说明搬家带进了行为变化。
- **行为**：说明预期的差别是什么，只允许那些差别出现；CI 全部 job 绿，Windows 手动触发一次。
- **第 0 步**：`git worktree list` 显示新位置，`git worktree prune --dry-run` 无输出；Phase 2 工作树重编后 ctest 全过、无 Qt 构建与 iPad 模拟器测试通过；兼容软链接都能解析；`du` 前后对比。
- **所有步骤**：第 3 步的文档链接检查通过。

---

## 6. 与 CLI/CI 文档的交点

本文不替 `doc/nexttoonz_cli_ci.md` 做决定，下面是两边要对齐的地方：

- **构建目录和预设是 CLI 的词汇。** 若 CLI 有“构建、测试、golden、对照”之类的子命令，它应当按预设名找构建目录（`build/<预设>`），按 `local/` 找缓存与快照。本文建议的预设名（4.9 节）和 `local/` 的结构可以由那份文档调整，但两边要用同一套。
- **CI 用预设**（第 5 步）：每个 job 一个 `ci-<job>` 预设，本机能用同一个预设复现 CI。job 名、产物路径（`build/<预设>/golden/*/diff/*.png`）由那份文档定。
- **`ci-scripts/` 的位置**：建议与 `tools/` 合并为 `tools/ci/`，并让 CI 和本机调同一个脚本（例如取 Skia 已经是这样：`ci-scripts/skia/skia_pack.sh`）。上游两年只改过 `ci-scripts` 4 次，合并成本可忽略。
- **CLI 程序本身放在哪**：脚本形态的入口放 `tools/`；如果是 C++ 程序（例如基于引擎 API 的命令行渲染器），分叉前放 `toonz/sources/<名字>`，分叉后在 `apps/`。
- **workflow 的路径过滤**：clang-format 的 `toonz/sources/**` 要跟随第 7、9、10 步；`paths-ignore` 的 `doc/**` 让 `doc/review/` 的图片不触发构建，这是 R1 的前提之一。
- **`.github/` 的上游冲突**与布局无关：上游两年有 54 个提交改 `.github`，我们的 workflow 已经大改，合并时这里的冲突一直会有。

---

## 7. 路线图条目（拟加进计划的原文）

建议挂在第 4 节“分阶段计划”下，作为一条横跨 Phase 2 与 Phase 3 的横向工作，不单独成阶段；Phase 2、Phase 3 的完成判据各加一条。以下是拟加的原文。

> ### 横向工作：仓库与构建布局
>
> 设计稿：`doc/nexttoonz_layout.md`。目标是让目录说明它是什么：源码按层分成 `core/`、`ui/`、`apps/`，测试、工具、运行时资源、第三方库在顶层，机器本地的东西在一个忽略的根下，不再有按阶段命名的目录。实测表明目录搬家本身不增加上游合并的冲突（一年的上游提交合进搬过家的树，内容冲突为 0），代价在 CMake 路径行，所以搬家的时机按库边界何时定型、上游 Qt6 通道何时合完来定。
>
> 工作项（编号对应设计稿第 5 节）：
>
> - 现在：本机整理（第 0 步，不碰版本库）；`.gitignore` 收窄并加 `/build/`、`/local/`（第 1 步）；开发者工具入库到 `tools/`（第 2 步）；审阅图片入库（第 3 步）。
> - Phase 2 第 1 到 8 步合并之后、在 Phase 2 的两步之间：CMake 去掉对源码与构建目录位置的假设（第 4 步）；按用途的预设，CI 改用预设（第 5 步，与 `doc/nexttoonz_cli_ci.md` 一起定）；仓库根的 CMake 入口（第 6 步）；测试上移到顶层 `tests/`（第 7 步）。
> - Phase 2 收口（D9 分叉）时：清理遗留目录（第 8 步）；核心库搬进 `core/`，公共头分到各库（第 9 步）。
> - Phase 3 里程碑一之后：UI 库与程序搬进 `ui/`、`apps/`（第 10 步）。
>
> 完成判据：
>
> - 仓库根 `cmake --preset <名字>` 可以配置每一种构建，构建目录是 `build/<名字>`；本机路径只在忽略的 `CMakeUserPresets.json` 里。
> - 主检出里不进版本库的东西只在 `build/`、`local/`、`.claude/` 下；没有工作树嵌在可删的目录里。
> - 版本库文档引用的工具和图片都在版本库里，文档链接检查通过。
> - Phase 2 的完成判据加一条：核心库在 `core/` 下，核心库的包含路径里没有 `ui/`、`apps/` 的目录。
> - Phase 3 的完成判据加一条：`toonz/sources/` 不再存在。
>
> 每一步单独合并，纯搬的步骤以编译命令逐字相同验收（见设计稿 5.2 节）；之后合并上游一律用 `tools/upstream/merge.sh`（改名阈值 25%）。

待定事项表拟加一行：

> | 仓库布局的目标与时机 | 按层分 `core/`、`ui/`、`apps/`；核心在 Phase 2 收口时搬，UI 在 Phase 3 里程碑一之后搬 | 所有者决定，见 `doc/nexttoonz_layout.md` 第 8 节 |

---

## 8. 需要所有者决定的问题

**决定（2026-11-19，所有者委托主会话拍板的部分）：** Q1 取 (a)（`doc/review/`，普通 git，单张 300 KB、按主题定预算，超预算转 (c)）；Q2 取 (a)（主检出里忽略的 `local/`）；Q3 取 (a)（长期工作树放 `~/Workspace/opentoonz-wt/<分支>/`，Phase 2 工作树在其会话空闲、工作区干净时移动并全量重编）；Q6 取 (a)（一次性验收脚本入库到 `tools/*/records/`，不保证以后能跑）；Q8 同意按清单在分叉时删除（toonzfarm、扫描仪、stopmotion 仍按计划待定事项另议）。**留给所有者的：** Q4（源码树的目标形状）、Q5（搬家时机）、Q7（第 0 步的删除清单：约 3.3 GB 本机数据和保留哪几个 app 快照）。下面保留原文作为依据。

**Q1 审阅图片放在哪。** (a) 版本库的 `doc/review/`，普通 git，单张 300 KB、按主题定预算；(b) 版本库里走 LFS；(c) 单独的仓库。倾向 (a)，超预算时转 (c)。比较见 4.7 节。

**Q2 全机共用的本地根放在哪。** (a) 主检出里的 `local/`（忽略）；(b) 仓库外的同级目录，例如 `~/Workspace/opentoonz-local/`。倾向 (a)，两者差别不大。

**Q3 长期工作树放在哪。** (a) 主检出外的 `~/Workspace/opentoonz-wt/<分支>/`；(b) `local/worktrees/`。倾向 (a)。Phase 2 的工作树按此移动后要全量重编一次。

**Q4 源码树的目标形状。** (a) 按层分组（S3：`core/`、`ui/`、`apps/`，头按库分、文件名不变）；(b) 整体改名为 `src/`（S2）；(c) 不动（S1）。倾向 (a)。

**Q5 搬家的时机。** (a) 核心在 Phase 2 收口时、UI 在 Phase 3 里程碑一之后（两次）；(b) Phase 2 收口时一次搬完，UI 一侧的 CMake 改写与 #6919 的合并相撞；(c) 现在就搬，与 Phase 2 后面 13 步的文件移动重叠。倾向 (a)。注意：不论选哪个，合并 #6919 本身今天就有 32 个文件冲突，与布局无关。

**Q6 一次性的验收脚本入不入库。** (a) 入库到 `tools/*/records/`，作为设计文档里验收方法的出处，不保证以后能跑；(b) 只入库通用的工具，一次性的留在本机或删掉。倾向 (a)。

**Q7 第 0 步的删除清单。** 5.1 节标着“删”的各项（合计约 3.3 GB：两个 Phase 0 之前的构建、`base-src` 工作树和它的构建、Phase 0 的 golden 与 verify 输出、`ab/` 的输出），以及要保留哪几个 app 快照。`toonz/build-phase0/review` 和 `ab/` 的输出是删还是挑一部分进 `doc/review/`。

**Q8 第 8 步的遗留清单。** `translations_docs/`（16 MB，无引用）、`appveyor.yml`、`tests/skia_spike/`、`include/toonz4.6` 在分叉时删除，是否同意；`toonzfarm`、扫描仪、stopmotion 仍按计划的待定事项另议。

---

## 9. 风险与未验证事项

- **移动 Phase 2 工作树时丢掉未提交的改动。** 只在它的会话空闲、`git status` 干净时做；先在它的分支上提交。
- **兼容软链接变成永久的杂物。** 第 0 步的软链接在下一个里程碑删除，删除写进那时的检查单。
- **合并上游时忘了降低改名阈值。** 实测默认阈值下 #6919 有 7 个文件退化成修改/删除冲突，25% 时是 2 个。`tools/upstream/merge.sh` 固定这些参数；第 9 步之后不经它合并上游算错误。
- **搬家提交混进内容修改。** 改名相似度低于 100% 会让 `git log --follow` 和合并时的改名配对变差。搬家与 CMake 改写分成两个提交。
- **模拟的局限。** 第 3.1 节的 B、C 两种搬法只做了 `git mv`，没有改写 CMake，树配置不起来；CMake 改写带来的冲突数是按上游改 CMakeLists 的提交数估的，没有实测。头文件按库分（H2）的归属没有逐个核对，只确认了导出宏可以作为判据。
- **Windows 没有验证。** Windows workflow 推分支不跑，凡是改到它路径的步骤都要手动触发；本机没有 Windows 环境。
- **GitHub 的 LFS 配额**没有核实具体数额，只依据上游 `appveyor.yml` 为配额跳过 LFS 的事实；它影响的是 Q1 的 (b)。
- **本 fork 是否接了 AppVeyor**没有核实（没有看账户设置）；本文按 workflow 只是排除 `appveyor.yml` 推断它没有在用。
- **第 0 步的重编耗时**没有测；Phase 2 工作树的默认构建目录 3.5 GB，是一次完整的冷构建。
- **#6919 的数字会变。** 它还在上游开发，冲突数是 2026-11-19 的快照。

---

## 10. 附录：测量方法

- 大小：`du -sh`；版本库内按目录的大小与文件数：`git ls-files -z <目录> | xargs -0 du -k` 求和。
- 上游活跃度：`git rev-list --no-merges --count --since=… --until=… upstream/master`；按目录：`git log --no-merges --since=2024-11-19 --name-only --format= upstream/master`，按前两级或前三级路径计数。
- 我们与上游的交集：`git diff --name-only $(git merge-base nexttoonz/phase0 upstream/master) wip/phase2-noqt` 与上游两年触及的文件取交集。
- 真实合并的重放：`git merge-tree --write-tree --name-only <ours> <theirs>`。
- 模拟：在草稿目录里 `git clone --shared` 一份，在一次性分支上 `git mv` 后提交，再 `git -c merge.renameLimit=0 merge [-X find-renames=25%] <对方>`，统计输出里的 `CONFLICT (…)` 种类，然后 `merge --abort`。#6919 取自 `pull/6919/head`（`6f87dd453`）。
- 路径引用：`git grep -c` 与 `git grep -h`，排除 `thirdparty/`。
