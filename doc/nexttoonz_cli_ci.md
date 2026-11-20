# NextToonz 命令行与 CI/CD 设计草案

版本 0.1，2026-11-19。本文设计两件互相依赖的东西：一个给人和 AI 代理都好用的命令行入口，和一条大部分验收都走"跑一条命令 → 机器算（托管 runner、所有者的 iMac）→ AI 验收出报告 → 人读一页简报"的流水线。目标是每个检查点、最好每个 PR 都这样验收，把今天 45 到 60 分钟、靠清单手点的人工 A/B 缩到几分钟的阅读。

本文只是设计和路线图，没有实现。第 2 节的数字是 2026-11-19 实测的（方法在附录 A）。路径相对于仓库根目录，`toonz/sources/` 以下的路径省略该前缀。引擎 API 和 Phase 2 的步号以 `wip/phase2-noqt` 上的 `doc/nexttoonz_core_noqt.md`（第 7 节的 21 步表）为准：引擎 API 第一版是**第 11 步**（Q1 加了第 8 步之后的编号，旧稿里是第 10 步）。

---

## 目录

1. 结论摘要
2. 现状实测
3. 命令行设计
4. 流水线
5. 每个 PR 与每个检查点
6. 路线图
7. 给 `doc/nexttoonz_plan.md` 的文字
8. 需要所有者决定的问题
9. 与其他文档的接触点
10. 未验证与风险
11. 附录 A：测量方法

---

## 1. 结论摘要

- **CI 的时间几乎全花在构建和打包上，测试本身很便宜。** 最近一次 `nexttoonz/phase0` 的推送（`84f17b336`）从推送到全部绿灯 61 分钟，关键路径是 macOS x86_64 作业的 55 分钟：编译 28 分钟、打 .app/.dmg 包 15 分钟、装依赖 6 分钟，跑全部 381 个测试（含 golden）只要 4.4 分钟。编译缓存基本没起作用：清洁剂作业 863 次编译 0 次命中。本机（iMac）上 tcomposer 渲完参考工程全部 28 个场景（277 帧）只要 27 秒。所以"多渲几遍、多比几种"几乎不花钱，省时间要从缓存和打包下手。
- **今天能自动跑的只有 tcomposer 和 tconverter，而它们不是为机器设计的。** 输出是给人看的文字，退出码混用 -1、-2、1、2 和 `abort()`，没有 JSON；tcleanup 在 macOS 上一启动就 `abort()`（已知问题 34），在出错路径上还会解引用空指针。QtScript 控制台的 `OpenToonz x.toonzscript` 模式要先建完整个主窗口，跑完一律返回 1，也碰不到调色板、特效、输出设置、清理、plastic。手工清单里的 10 条有 6 条的"数据部分"本可以由命令行比较，现在只能人看。
- **推荐的形状：一个 `nexttoonz` 程序加子命令，建在 Phase 2 的引擎 API 上；先把契约（子命令、JSON 结构、退出码）定下来，用现有程序包一层 Python 前端先用起来，C++ 实现到时替换进去。** 子命令：`probe`、`render`、`convert`、`cleanup`、`inspect`、`diff`、`thumbs`/`sheet`、`run`（场景脚本）、`test`。tcomposer、tconverter、tcleanupper 保留为兼容外壳（渲染农场和用户脚本在用它们），按 Q7 改走引擎 API 时和 `nexttoonz` 共用代码。
- **确定性分四级，比较规则跟着级别走。** Skia 默认路径在同一平台上逐字节确定（1 线程与 8 线程相同，x86_64 macOS 与 iPad 模拟器的画布哈希相同），所以同机 A/B 和"主机对同平台基线"可以要求逐字节相同；跨平台（macOS 对 Linux）保留容差；字体、`rand()`、GLSL、GL 退路的多线程是另外两级，只在指定平台比或只做报告。
- **流水线：托管 runner 跑一切能在 CPU 上算的，iMac 只做托管 runner 做不了的（真显卡、窗口里的 GUI、稳定的性能数字），而且不当自托管 runner。** D5 不变：iMac 上是一个由所有者或 Claude 会话手动启动的脚本，它用可信的 harness（取自基准分支，而不是被测的 PR）拉 PR 的代码、构建、跑、写结果包，再用 `gh` 发 commit status 和 PR 评论。
- **AI 验收不能把红灯变绿。** 通过与否由机器的阈值决定；AI 只做三件事：解释差异、把要人看的东西排序、在机器说"过"时仍可以要求人看。防止它随手盖章的办法：已知差异登记表由程序匹配而不是 AI 判断；对照图盲标（AI 先描述差异，后看 PR 的意图）；每次放一个预先埋好的"金丝雀"差异，漏报则这次验收作废；每条结论必须引用结果包里的文件和数字，由脚本核对。
- **现在就能做、不等引擎 API 的有 9 件（第 6.1 节）**，最值的是：把 A/B 脚本收进仓库并输出逐帧像素哈希、CTest 和 golden 出 JUnit/JSON、每次 CI 都传对照图、非检查点的推送不打包（每个 macOS 作业省 8 到 15 分钟）、修好编译缓存、主机运行脚本、AI 验收的提示词和简报模板。
- **需要所有者定的**（第 8 节）：主机怎样参与（推荐手动脚本，不用自托管 runner）；是否改走 PR 进 `nexttoonz/phase0`（缓存和每 PR 流水线都靠它）；AI 验收在哪里跑、能不能挡合并；GUI 自动化钩子是否进发布版；`nexttoonz` 这个程序名现在就用；基线能否由主机产生。

---

## 2. 现状实测

### 2.1 命令行入口

全树的可执行程序（不算测试）如下。下表的用法行是在 `wip/phase2-noqt`（`dbd8118e7`）的 macOS 构建上用 `-help` 打出来的，与 `nexttoonz/phase0` 相同；要求一栏来自源码。

| 程序 | 用法（TCli） | 要什么 | 退出码 |
|---|---|---|---|
| `tcomposer` | `tcomposer srcName [-o dstName] [-range from to \| -frame fr] [-step n] [-shrink n] [-multimedia n] [-farm data] [-id n] [-nthreads n] [-maxtilesize n] [-tmsg val] [-TOONZROOT folderpath] [-TOONZCONFIG …] [-TOONZFXPRESETS …] [-TOONZLIBRARY …] [-TOONZPROFILES …] [-TOONZPROJECTS …] [-TOONZSTUDIOPALETTE …]` | `QApplication`（macOS 上要窗口服务器；Linux 上 `offscreen` 就够，GLSL 场景和 GL 退路要 Xvfb）；stuff 目录（缺失时非 Windows `abort()`）；场景要属于一个工程；GLSL 特效要 GL 上下文 | 0 全部完成；-1（shell 里 255）有帧没完成或未捕获异常；-2（254）工程或场景加载失败；1 参数错误；2 连续内存耗尽；134 `abort()` |
| `tconverter` | `tconverter srcName dstName [-w width] [-s sceneName] [-range from to \| -frame fr]` | Linux 上 `QGuiApplication`；macOS 和 Windows 上不建任何 Qt 应用对象（`tconverter.cpp:466` 的条件编译）；pli 转栅格在 GL 退路下要 GL；stuff 目录（缺失只打印 `TOONZROOT not set`） | 0；1 参数、扩展名、源不存在；-1 异常、场景不存在。目标只写文件名时被当成格式名，输出放在源文件旁边 |
| `tcleanup`（源码 `tcleanupper/`） | `tcleanup tnzFile [-onlyvisible] [-overwriteAll] [-overwriteNoPaint] [-farm data] [-id n] [-tmsg n]` | `QApplication`；stuff 目录 | 0；-2 工程；-3 场景或清理设置读不到。**macOS 上一启动就 `abort()`**（找不到自己的 `SystemVar.ini`，已知问题 34，1.8.0 起就有）。`tcleanupper.cpp:596` 在找不到工程时先解引用空的 `project` 再报错（新发现，未登记） |
| `OpenToonz` | `OpenToonz [-layout filename] [-TOONZROOT …] [filePath]`，`filePath` 是场景或 `.toonzscript` | 完整的 GUI | 脚本模式跑完一律返回 1（`toonz/main.cpp:721`） |
| `lzocompress`、`lzodecompress` | 由 tlv/tzl 读写经 `QProcess` 调用（Phase 2 第 13 步改为直接链接） | | |
| `tfarmcontroller`、`tfarmserver` | 渲染农场；后者以 `-farm`、`-id`、`-tmsg` 调 tcomposer 和 tcleanup | | |
| `t32bitsrv` | 32 位的 QuickTime 服务进程（经 `tipc` 通信）；Phase 2 第 13 步删除（D10） | | |
| `nexttoonz_gen_fixtures`（测试目标） | `nexttoonz_gen_fixtures <stuff> <project>` | 不要显示器、不要 GL | 输出不逐字节确定（`.tnz` 里特效、终端集合、cast 按指针顺序写） |

这些程序都把日志写到标准输出和 `stuff/toonzfarm/*.log`，格式是给人看的句子（`Conversion terminated!`、`project: …`），没有结构化输出。tcomposer 的 `fatalError` 在 macOS 上用 `abort()`，会留下崩溃报告。

### 2.2 测试脚本

| 脚本 | 做什么 | 输出 |
|---|---|---|
| `tests/golden/run_golden.py` | ctest 的每个 `golden_<场景>` 调它一次：把 `stuff/`（28 MB）和参考工程复制进构建目录，单线程跑 tcomposer，逐帧调 `compare_images.py`；`--update` 覆盖基线 | 打印 `PASS/FAIL <帧>: <摘要>`；失败时写差异图；退出码 0/1/2 |
| `tests/golden/compare_images.py` | 预乘后比较：超过 2 个色阶的像素不多于 0.5% 为过；摘要里有非空白像素数 | 文字摘要，可选差异图 |
| `tests/golden/contact_sheet.py` | 把特效扫描场景的帧排成带标签的总览图，标出空白、平坦、与输入相同的帧 | PNG |
| `tests/fixtures/gen_reference_project.cpp` | 经 toonzlib API 生成参考工程（29 个场景，320×180） | 工程目录 |
| `tests/ios_gtest.sh`、`ci-scripts/noqt/noqt_test.sh`（wip） | 用 Xcode 的 clang 编 GoogleTest 程序，在 iPad 模拟器里 `simctl spawn`；无 Qt 构建加三项"没碰到 Qt"的检查 | gtest 文字输出 |
| `toonz/build-phase1/ab/`（不在仓库，被 `build*` 忽略） | Phase 1、2 的 A/B 工具：`phase2/ab_tcomposer.py` 用两个 tcomposer 各渲全部场景（可选多个线程数）再逐字节比较；`ab_*.cpp` 是专门的对照程序（样式、区域、描边、离屏、摄影台）；`vm/*.sh` 在 Linux 虚拟机里做同样的事 | 文字：`identical N, different M …` |

golden 共 286 个基线帧、5.6 MB；参考工程 1.5 MB。ctest 里有 4 类被跳过或只在 Linux 比较的场景（`fx_sweep_font_*`、`fx_sweep_rand_*`、`fx_sweep_gpu*_*`、`vector_styles`），原因在 `tests/golden/CMakeLists.txt`。

### 2.3 只能经 GUI 做的事

所有者做过的两份清单（`toonz/build-phase1/ab/phase2/manual/AB_TEST.md` 10 条约 45 到 60 分钟，`ACCEPTANCE.md` 11 条）是最好的"GUI 才能验什么"的样本。逐条看，每条里都混着两种东西：数据对不对（命令行能比较），和画面、交互对不对（只有界面能验）。

| 清单条目 | 今天怎么验 | 数据部分：命令行能接管的 | 只有界面能验的 |
|---|---|---|---|
| 1 资产路径与设置持久化、便携模式 | 启动、改偏好、重启、`find` 看 stuff | `probe` 报告解析出的 stuff 路径和便携模式；偏好的读写由第 15 步的键值存储单测 | 偏好对话框本身 |
| 2 viewer 里全部样式（摄影台、3D、透明度检查） | 两个包并排逐帧看 | 离线渲染已由 golden 覆盖 | viewer 仍是 OpenGL（Phase 3 前），只能截图对比 |
| 3 样式编辑器与翻译（英、日） | 点样式看名字 | `inspect styles --lang ja`：核心出 `TTranslatableText`、界面侧翻译，可以逐字导出比较（第 7 步就是这样验的） | 控件布局 |
| 4 工具的辅助线 | 手画 | 无 | 全部（Phase 3 工具上画布前） |
| 5 保存场景、调色板、图标 | 改色、保存、重开、看缩略图 | 场景脚本：改色 → 存关卡和场景 → 重开 → `inspect palette` 比较；`thumbs` 生成场景图标并比哈希 | 关卡条、单元格缩略图的显示 |
| 6 渲染含多线程 | Output Settings 渲染两遍、`cmp` | `render --threads 1,8` 逐字节比较（A/B 脚本已经在做） | 渲染进度对话框 |
| 7 输出设置的格式属性 | 改 tif 选项、存、重开 | 场景脚本：设输出属性 → 存 → 重开 → `inspect output` | 选项对话框 |
| 8 文件浏览器（隐藏文件、NFD、符号链接、序列） | 看列表、改名、删除 | 列目录的语义由 `TSystem` 单测和差分测试覆盖；可加 `inspect dir` 输出 JSON | 浏览器控件的显示 |
| 9 清理 | Cleanup 房间里执行 | `cleanup`（要先修已知问题 34），结果出哈希 | 清理设置面板 |
| 10 Plastic | 拖骨骼看变形 | 变形的渲染结果已由 `plastic_basic` golden 覆盖 | 骨骼与网格的显示、拖动 |

结论：10 条里 2、4 两条和每条的"显示"部分要界面；其余的数据部分可以由第 3 节的子命令接管。剩下的界面部分在 3.8 节。

### 2.4 QtScript 控制台能无头做什么

脚本引擎在 toonzlib（`toonzlib/scriptengine.cpp`、`scriptbinding*.cpp`，共约 3,700 行），绑定的对象：

- `FilePath`（名字、扩展名、父目录、列文件）、`Image`（读、写、尺寸、dpi）、`Level`（类型、帧数、名字、路径、取帧、设帧、读、写）、`Scene`（读、写、帧数、列数、`setCell`/`getCell`、插入删除列、列出和新建关卡、`loadLevel`）、`Transform` 与 `ImageBuilder`（拼图）、`Rasterizer`、两个矢量化器、`Renderer`（`renderScene`、`renderFrame`）；全局函数 `print`、`warning`、`run`。控制台另加 `view`（弹 FlipBook）。
- `ToonzRasterConverter` 只是个桩（`convert` 被注释掉，剩一个 `foo(x)`）。

无头的限制：

- 唯一的非交互入口是 `OpenToonz <文件>.toonzscript`：它在**建完 `MainWindow` 之后**才跑脚本（只是不显示启动画面），所以要窗口服务器和完整的 GUI 初始化；跑完一律返回 1；脚本文件必须属于一个工程。
- `ScriptEngine` 在 `QThread` 里求值，`evaluateOnMainThread` 断言 `qApp` 存在，`Renderer` 在 `QEventLoop` 上自旋：离开 Qt 事件循环就不能用。
- 碰不到的：调色板与样式、特效图、舞台对象与摄像机、输出设置、清理、plastic、撤销、工具；没有结构化输出。

所以它不适合作为命令行的基础。按 Q3 的决定它在第 19 步移出成单独的可选库、引擎不换；本文的场景脚本（3.5 节）是声明式的，与它无关。

### 2.5 CI 作业

| workflow | 作业 | runner | 构建 | 测试 | 触发 |
|---|---|---|---|---|---|
| Linux Build | Ubuntu gcc、Ubuntu clang | ubuntu-22.04 | 全部，`WITH_SKIA`（vcpkg 编的 Skia） | ctest 全部（386 个，golden 在 Xvfb 下）；装好的树上跑 tconverter 冒烟 | 任何分支推送、PR（排除 `doc/**` 等） |
| | Fedora | fedora:latest 容器 | 全部，不带 Skia，**无编译缓存** | 无 | 同上 |
| MacOS Build | arm64、x86_64 | macos-15、macos-15-intel | 全部，`WITH_SKIA`（skia-pack） | ctest 全部（381 个） | 同上 |
| | macOS-noqt（仅在 wip，未推送） | macos-15-intel | `WITH_QT=OFF` | 无 Qt 的单测，iPad 模拟器 | |
| Sanitizers | ASan + UBSan | ubuntu-24.04 | 测试程序和 tcomposer | ctest 全部（386 个，含 golden） | 同上 |
| Windows | | windows-2025 | 全部 | 无 | 只在 master 推送、PR、手动 |
| clang-format | | ubuntu-latest | | 只查改动的行 | 只在 PR |

`84f17b336` 那次（2026-11-17）各作业的时间，单位分钟，只列超过 20 秒的步骤：

| 作业 | 合计 | 装依赖 | Skia | 编译 | 测试 | 打包 | 
|---|---|---|---|---|---|---|
| macOS x86_64 | **55.0** | 5.7 | 0.3 | 28.2 | 4.4 | 14.6（拷库与 stuff 10.9，改库路径 1.4，dmg 2.3） |
| macOS arm64 | 30.2 | 4.2 | | 14.8 | 2.2 | 7.6 |
| Ubuntu gcc | 38.1 | 2.7 | 8.4（未命中缓存） | 23.4 | 0.8 | 2.0 |
| Ubuntu clang | 18.6 | 0.9 | 4.3（未命中） | 10.6 | 0.6 | 1.8 |
| Fedora | 21.1 | 1.1 | | 19.3 | | |
| ASan + UBSan | 22.0 | 1.3 | 6.6（未命中） | 8.5 | 5.0 | |

- 三个 workflow 并行，推送到全部绿灯 61 分钟（MacOS Build 61、Linux Build 38、Sanitizers 22）。
- ctest 的总时间：Ubuntu clang 36 秒，gcc 51 秒，macOS arm64 131 秒，x86_64 260 秒，ASan 296 秒。最慢的 golden 是 `fx_sweep_ino_b`（x86_64 macOS 8.6 秒，ASan 49 秒）。测试是串行跑的，`gtest_discover_tests` 让每个 `TEST()` 都是一个进程。
- 编译缓存：清洁剂作业打印了统计，这次 863 次可缓存的编译 0 次命中，上一次（11-06）850 次 20 次命中。其他作业不打印统计。原因有两个，都是查得到的：(1) GitHub 的缓存按分支隔离，一个分支只能读自己和默认分支（`master`）的缓存，所以 `ci/skia-in-ci` 上攒下的 ccache 和 Skia 缓存 `nexttoonz/phase0` 读不到，Skia 在三个 Linux 作业里又从源码编了一遍；(2) 每次推送 `nexttoonz/phase0` 都是一大批改动（这次是 Phase 1 的 39 个提交），改了被几百个文件包含的头。仓库现在有 18 个缓存共 1.2 GB（上限 10 GB），每个 ccache 59 到 179 MB。Linux 作业同时设了 `CC="ccache gcc"` 和 `CMAKE_CXX_COMPILER_LAUNCHER=ccache`，C++ 编译可能被 ccache 套了两层，需要看统计确认。
- 打包（`macdeployqt` 一类的拷库、改路径、做 dmg）在 macOS 上占作业时间的四分之一，而这些包只有发布和人工验收时才用。

### 2.6 本机测量

机器：所有者的 iMac，Intel Core i7-10700K（8 核 16 线程），128 GB 内存，AMD 显卡。构建：`wip/phase2-noqt` 的 Release 构建（Skia 默认）。

| 项 | 结果 |
|---|---|
| tcomposer 渲一帧的固定开销（启动、注册特效与格式、读场景） | 约 0.47 秒，所有场景几乎一样 |
| 28 个场景（不含 GLSL 两个）全部帧，单线程 | 26.6 秒，277 帧；最慢 `fx_sweep_ino_b` 7.1 秒，其余多在 0.5 到 2 秒 |
| `run_golden.py` 一个场景 | 1.8 秒，其中复制 `stuff/` 约 0.9 秒、tcomposer 约 0.6 秒 |
| `nexttoonz_core_tests`（wip 上 226 个） | 67 秒（大部分是 TFilePath 与字符串的差分测试） |
| `nexttoonz_toonzlib_tests`（147 个，单进程） | 3.8 秒 |
| 画布与 Skia 画布测试（86 个） | 0.05 秒 |
| 全量编译 | `.ninja_log` 里全部 2,114 个目标的编译时间之和 41 分钟；16 线程下估计 3 到 5 分钟墙钟（没有做干净构建实测） |

### 2.7 小结：时间花在哪

- 托管 runner 上：编译 50% 到 60%，打包（只 macOS）20% 到 27%，装依赖约 10%，测试不到 10%。
- 测试里：每个场景重复复制 `stuff/`、每个测试一个进程、串行，比渲染本身慢。
- 人：一次检查点的人工 A/B 45 到 60 分钟，加上准备包和材料（Claude 会话做，但每次手写清单）。

这意味着：把对照做得更全（多线程、GL 退路、A/B）几乎不增加 CI 时间；而把打包挪到检查点、让缓存跨分支可用、并行跑测试，可以把每次推送从 61 分钟压到 35 分钟上下（估计，第 5.2 节）。

---

## 3. 命令行设计

### 3.1 原则

1. **给机器读的输出是一等公民。** 每个子命令都有 `--json`：标准输出只有一个 JSON 文档，日志一律走标准错误。人看的文字是 JSON 的一种渲染，不是另一套逻辑。
2. **契约先行，实现可换。** 子命令名、参数、JSON 结构（带 `schema` 版本号）、退出码先写成文档和 JSON Schema。第一个实现是包着现有程序的 Python 前端（`tools/nt`），引擎 API 有了以后换成 C++ 的 `nexttoonz`，调用方（CI、Claude 会话、脚本）不用改。
3. **只经引擎 API 碰核心。** C++ 的 `nexttoonz` 是引擎 API 的又一个消费者，与 tcomposer、iPad 壳同一层，不直接包含 toonzlib 的头。暂时缺的读接口（xsheet、调色板、特效图的只读视图）由它推动引擎 API 加，而不是绕过去。唯一的例外是标为内部的调试工具（3.2 节末）。
4. **确定性写进输出。** 每个渲染结果都带像素哈希和它属于哪一级确定性（3.4 节），比较的一方据此选逐字节还是容差。
5. **一切可在无窗口、无 GPU 的机器上跑**，需要窗口或 GPU 的部分明确报错（退出码 4），不卡在对话框上。

### 3.2 一个入口与子命令

| 子命令 | 做什么 | 主要参数 | 替代今天的 |
|---|---|---|---|
| `probe` | 环境：版本、git 提交、构建选项（`WITH_SKIA`、`WITH_QT`）、Skia 版本与参考画面哈希、栅格化后端、stuff 目录的解析结果与便携模式、可用的 GL（GL 退路）和 Metal 设备、线程数、默认字体的指纹、C 库 `rand()` 前 8 个值 | `--stuff`、`--json` | 手工判断"这台机器能不能比" |
| `render` | 渲染场景的帧，写文件或只出哈希 | `<scene> [--frames 1-8] [--out dir] [--threads N] [--format png] [--rasterizer skia\|gl] [--camera WxH] [--hash-only]` | tcomposer、`run_golden.py` 的渲染部分 |
| `convert` | 关卡格式转换 | `<src> <dst> [--width] [--scene] [--frames]` | tconverter |
| `cleanup` | 按场景的清理设置清理 | `<scene> [--columns] [--overwrite all\|nopaint]` | tcleanup |
| `inspect` | 导出结构为 JSON：`scene`（摄像机、输出设置、帧数）、`xsheet`（列、单元格、子 xsheet）、`levels`、`palette`（样式、参数、名字，可选 `--lang`）、`fx`（特效图：节点、端口、参数在指定帧的值）、`stage`（pegbar、骨骼、运动路径）、`output`、`dir`（目录列表的语义） | `<file> <what> [--frame n] [--lang xx]` | 打开应用看 |
| `diff images` | 两张图或两组帧：预乘比较、超阈值像素比例、峰值、按格子（默认 8×8）或连通区域报告差在哪、可选热力图 | `<a> <b> [--profile exact\|golden\|edge] [--regions grid\|blobs] [--heatmap out.png]` | `compare_images.py` |
| `diff tree` | 两棵渲染输出树（`render --out` 的结果）逐帧比较，按场景汇总 | `<dirA> <dirB>` | `ab_tcomposer.py compare` |
| `diff scene` / `diff palette` | 结构差异：先 `inspect`，再按 id 对齐比较 JSON；不比 `.tnz` 的字节（指针顺序） | `<a> <b>` | 无 |
| `thumbs` / `sheet` | 场景图标、关卡缩略图、样式图标；把一组帧（或 A/B 两组、差异热力图）排成带标签的总览图，标签取自 `extras/*.txt` | `<scene\|dir> --out` | `contact_sheet.py`、手做的对照图 |
| `run` | 跑场景脚本（3.5 节）：一串引擎操作代替 GUI 步骤，带断言 | `<scenario.json> [--bundle dir]` | 手工清单的数据部分 |
| `test` | 编排：跑套件（`unit`、`golden`、`ab`、`noqt`、`perf`），写结果包（4.3 节） | `--suite pr\|checkpoint\|… --base <sha或包> --bundle dir [--junit] [--json]` | ctest 加各种 `ab/*.sh` |

`test` 以外的子命令都只做一件事、不写构建目录、可以并行跑。`test` 本身调用其他子命令和 ctest（`--output-junit`），不重新实现 ctest。

**内部工具。** 3.2 节的 `inspect` 要等引擎 API 有只读视图。在那之前，确有需要的话可以有一个 `nexttoonz-dump`（像 `nexttoonz_gen_fixtures` 一样直接链接 toonzlib 的测试目标），输出同样的 JSON 结构，只在 CI 和开发时用、不安装；引擎 API 的视图做出来后删掉它。

### 3.3 输出约定

**JSON。** 每个文档的顶层：

```json
{
  "schema": "nexttoonz.render/1",
  "tool": {"name": "nexttoonz", "version": "…", "git": "84f17b336", "build": {"skia": "m144", "qt": true, "rasterizer": "skia"}},
  "host": {"os": "macos-15.x", "arch": "x86_64", "probe": "sha256:…"},
  "ok": true,
  "result": { … },
  "warnings": [ … ],
  "timing_ms": {"init": 470, "load": 30, "render": 512}
}
```

`render` 的 `result` 逐帧给出 `{"frame": 3, "file": "…/vector_basic.0003.png", "pixels": "sha256:<解码后预乘 RGBA 的哈希>", "png": "sha256:<文件字节的哈希>", "nonblank": 12345, "determinism": "exact"}`。比较用 `pixels`：PNG 编码器的细节变化不应该让比较失败。`--progress` 另在标准错误上输出一行一个 JSON 事件（`{"event":"frame-done",…}`），给长任务用。

**退出码**（所有子命令一致；tcomposer 等外壳保留各自原来的码）：

| 码 | 意思 |
|---|---|
| 0 | 完成，比较（如有）通过 |
| 1 | 完成，但比较或断言没通过（结果是"不同"，不是"出错"） |
| 2 | 用法错误 |
| 3 | 输入有问题：文件不存在、场景加载失败、不属于工程 |
| 4 | 环境不满足：stuff 目录、需要 GL 或窗口而没有、需要的格式插件不在 |
| 5 | 内部错误：异常、部分帧没完成 |
| 124 | 超时（`--timeout`，子命令自己执行，不靠外部的 `timeout`，macOS 上没有） |

崩溃（信号）由 `test` 捕获，记成该项失败并附上 macOS 的崩溃报告路径。任何子命令都不弹对话框、不等输入。

### 3.4 确定性

Phase 1 把离线矢量换成 Skia 的 CPU 光栅化以后，"同一输入同一输出"在大部分地方成立了。实测过的事实：

- 默认 Skia 路径：1 线程与 8 线程的全部场景逐字节相同（Phase 2 每一步都这样验）；同一台机器不同构建、同一源码逐字节相同。
- x86_64 macOS 与 iPad 模拟器（x86_64）的 Skia 画布测试画面哈希相同（`00f0bc48d7b9d098`）。
- macOS 与 Linux：测试画面 3,072 个像素里有 355 个差，最多差 3；golden 的矢量场景在容差内（最多 0.24% 的像素超过 2）；`vector_styles` 差 2.8%、最多 49 个色阶，原因未查。
- arm64 macOS：CI 的 golden 在容差内通过；与 x86_64 是否逐字节相同未知（Phase 2 的 arm64 校验和表还没做）。
- 不确定的：字体（macOS 的系统默认字体状态隔夜就会变）、C 库 `rand()`、GLSL（驱动；带输入的在 macOS 上崩溃，已知问题 15）、GL 退路的多线程（已知问题 26）和 AMD 驱动的虚线相位；fixture 生成器和 `.tnz` 的写出顺序。

据此每个场景（必要时每帧）标一个级别，写在基线目录的 `BASELINE.json` 里（5.4 节），`render` 的输出也带它：

| 级别 | 含义 | 同机 A/B | 对基线 |
|---|---|---|---|
| `exact` | 同平台、同 Skia 版本逐字节相同 | 逐字节 | 有该平台的哈希清单时逐字节，否则容差 |
| `tolerant` | 跨平台只在容差内一致（Skia 的跨平台差、`vector_styles`） | 逐字节 | 容差（今天的 2 级、0.5%） |
| `pinned` | 依赖平台的外部状态（字体、`rand()`、GLSL） | 逐字节，但同一天同一台机器渲两边（字体会变） | 只在产生基线的平台比 |
| `unstable` | 本来就不确定（GL 退路多线程） | 只报告，不判 | 不比 |

基线目录里除 PNG 外，每个平台一份哈希清单（`hashes.<platform>.json`，文本，很小）：有清单的平台逐字节比，没有的退回容差。Phase 1 的矢量校验和表（按平台分开）已经是这个做法，这里推广到 golden。

### 3.5 场景脚本

`nexttoonz run` 读一个声明式的 JSON（或 YAML）文件：一串步骤，每步是一个引擎操作或一个断言。它代替的是清单里"在 GUI 里点几下，再看结果"的那一类。例子（清单第 5、7 条）：

```json
{
  "schema": "nexttoonz.scenario/1",
  "name": "palette-and-output-settings-persist",
  "project": "@fixtures/reference_project",
  "steps": [
    {"open": "scenes/vector_styles.tnz"},
    {"palette.setColor": {"level": "vec_styles", "style": 3, "rgba": [200, 30, 30, 255]}},
    {"output.set": {"format": "tif", "props": {"Compression Type": "LZW", "Bits Per Pixel": "64(RGBM)"}}},
    {"saveLevel": "vec_styles"}, {"saveScene": null},
    {"close": null}, {"open": "scenes/vector_styles.tnz"},
    {"assert.inspect": {"what": "palette", "level": "vec_styles", "path": "styles[3].rgba", "equals": [200, 30, 30, 255]}},
    {"assert.inspect": {"what": "output", "path": "props.Compression Type", "equals": "LZW"}},
    {"render": {"frames": "1-2", "as": "after"}},
    {"assert.diff": {"a": "@base:render/vector_styles", "b": "after", "profile": "exact", "except": "style 3"}},
    {"thumbs": {"sceneIcon": true, "as": "icon"}}
  ]
}
```

- 操作只能是引擎 API 已经提供的（打开、保存、读写调色板、输出设置、渲染在第 11 步的第一版或其后几步就有；单元格、列、关卡的编辑要第 21 步）。脚本运行器遇到还不支持的操作报退出码 4，而不是悄悄跳过。
- 每个脚本在一份工程的临时副本上跑（与 `run_golden.py` 相同），绝不写 `tests/fixtures`。
- 不做成通用脚本语言：没有循环和变量，只有步骤和断言。要编程的场合用 Python 调多个子命令。长期的用户脚本引擎（QuickJS、Lua、Python）是计划第 5 节的待定事项，与这个测试用的格式无关；将来真有了，场景脚本可以由它生成，不必反过来。
- 清单里的条目逐条写成脚本放在 `tests/scenarios/`，每条注明它代替清单的哪一条。

### 3.6 不带 Qt，以及 iPad

- `nexttoonz` 自己的代码不包含任何 Qt 头，只链接引擎库。引擎库在 Phase 2 结束（第 20 步）前内部仍链接 Qt，所以在那之前它在桌面上和 tcomposer 一样需要一个 Qt 应用对象；这个对象由引擎在初始化时按需创建（第 9、10 步之后只剩字体，第 18 步之后连字体也不需要），`nexttoonz` 的 `main` 不碰它。
- 第 20 步之后，`WITH_QT=OFF` 的构建里也有 `nexttoonz`，能在不装 Qt 的 macOS runner 上跑 `render`、`inspect`、`diff`。
- iPad 不能起子进程、没有终端。做法是把子命令实现成一个库（`ntcli`，入口 `int nt_cli_main(int argc, char **argv, nt_cli_io *io)`），桌面的 `nexttoonz` 只是它的 `main`；iPad 模拟器这条线用一个 iOS 测试宿主程序链接同一个库，经 `simctl spawn` 传参数、把结果写到模拟器的沙盒目录再取回，和 `tests/ios_gtest.sh` 跑单测的方式一样。这样"iPad 上的核心画出来的帧和 macOS 一样"就是一条 `render --hash-only` 加一次 `diff`。
- 图像比较（`diff images`）只需要解码 PNG 和数数，第一版用 Python（numpy、Pillow，CI 已经装了），C++ 版在 iPad 线需要时再做。

### 3.7 与 tcomposer、tconverter、tcleanupper 的关系

**保留为兼容外壳，内部改用与 `nexttoonz` 相同的代码。** 理由：

- 渲染农场（`tfarmserver`）以 `-farm`、`-id`、`-tmsg` 调 tcomposer 和 tcleanup，用户和第三方的批处理脚本也调它们；改名或改参数会无声地坏掉这些用法。
- Q7 已经要求这三个程序经引擎 API 调核心，那一步本来就要重写它们的 `main`；顺手让它们变成 `ntcli` 的薄外壳（解析旧的 TCli 参数、翻译成 `render`/`convert`/`cleanup` 的调用、保留旧的退出码和输出文字），不多花多少工夫，而且 golden 继续经 tcomposer 跑，逐字节相同就是这一步的验收。
- 新的测试、CI、Claude 会话只用 `nexttoonz`。农场相关的参数不进 `nexttoonz`。
- 那一步顺带修掉：tcleanup 在 macOS 上的 `abort()`（已知问题 34）和空指针解引用；tcomposer 的 `fatalError` 改为退出码而不是 `abort()`。
- 是否、何时废弃旧名字，跟计划里"改名为 NextToonz 的时机"一起定。

### 3.8 命令行替代不了的：真正的界面测试

viewer（Phase 3 之前是 OpenGL）、工具的辅助线和手感、对话框和面板、拖放、数位板压感，命令行都碰不到。建议：

1. **范围压到最小。** 只覆盖清单里命令行覆盖不了的显示部分，第一批 6 个场景：清单第 2 条（viewer 逐帧截图）、第 4 条（几何、填充、选择三个工具各一次拖动后截 viewer）、第 5 条（关卡条与单元格缩略图截图）、第 8 条（浏览器列表截图）、第 10 条（plastic 网格显示、拖一个骨骼后截图）、启动后日志里没有 `TGLView::install`。
2. **在 OpenToonz 里加一个薄的自动化钩子**，而不是引入 GUI 测试框架：`OpenToonz --automation <scenario.json> --out <dir>`，只在 `WITH_AUTOMATION` 的构建里编（默认开在开发构建、关在发布构建，所有者定，第 8 节）。它能做的只有：打开场景、切房间、按命令 id 执行菜单命令（应用的命令管理器本来就是按 id 注册的）、设当前帧与列、选工具、在 viewer 上合成鼠标拖动（Qt 的事件合成，不链接 QtTest 也能做）、`QWidget::grab` 截控件、读回 viewer 的帧缓冲、导出日志，然后退出并给出退出码。格式沿用 3.5 节的场景脚本，多几个只在 GUI 里有的操作。
3. **只在主机上跑。** 它需要窗口服务器和真显卡；GitHub 的 macOS runner 也有窗口服务器，但显卡是虚拟的，截图和主机不同，没有比较的价值。比较的对方是同一台机器上的"之前"包（与今天的 BEFORE 包做法相同），用 `diff images` 的 `edge` 档容差，差异交给 AI 验收排序。
4. **剩下的交给人，但写成 10 分钟以内的固定清单**：手感（工具跟手、延迟）、数位板、菜单与快捷键、中文和日文界面的排版。检查点才做，每个 PR 不做。

---

## 4. 流水线

### 4.1 阶段与在哪里跑

| 阶段 | 内容 | 托管 runner | 主机（iMac） |
|---|---|---|---|
| S0 格式 | clang-format-14 查改动的行 | 每个 PR | |
| S1 构建 | Linux gcc/clang、Fedora、macOS arm64/x86_64、无 Qt（macOS x86_64）、Windows（手动） | 每次推送和 PR | 增量构建被测提交；检查点时干净构建 |
| S2 单测 | ctest 去掉 golden，`--output-junit` | 每次 | 每次 |
| S3 golden | 对仓库里的基线；有哈希清单的平台逐字节 | 每次（Linux 在 Xvfb 下，macOS 两种架构） | 每次（x86_64 macOS 有哈希清单） |
| S4 A/B | 被测提交对基准提交：全部场景 × 线程 1 与 8，逐字节比较像素哈希；基准一边的结果取自基准提交的 CI 产物（不用再编一遍基准） | 每个 PR（同平台对同平台） | 每次；检查点加 GL 退路、加"之前"包的 GUI 截图 |
| S5 清洁剂 | ASan、UBSan、LSan，含 golden | 每次 | ThreadSanitizer（碰线程的步骤，Linux 托管 runner 也可以，按需） |
| S6 无 Qt 与 iPad | `noqt_test.sh`、iPad 模拟器里的单测；第 20 步后加 `render --hash-only` 对 macOS 哈希 | 每次（macOS x86_64） | 检查点 |
| S7 性能 | 每个场景 5 次中位数的墙钟、启动时间、峰值内存；另加 1080p 的性能场景 | 只记录趋势，不判 | 检查点判（4.5 节的阈值）；每个 PR 只报告 |
| S8 主机专有 | GL 退路、真显卡、GUI 自动化（3.8 节）、Linux 虚拟机（`nt-asan`，macOS 上编不到的 `#ifdef LINUX`） | | 每次（GUI 只做第一批 6 个里的 3 个）；检查点全做 |
| S9 AI 验收 | 读结果包，出结论和排序过的待看清单 | 可选（第 8 节问题 3） | 默认在这里 |
| S10 人 | 读简报，决定合并 | | |

托管 runner 能做的绝不放到主机上：主机不总是开着，而且是所有者的工作机。

### 4.2 主机怎样安全地参与

D5 说不在公开仓库上用自托管 runner。原因是任何人都能开 PR，PR 里的 workflow 改动可以让 runner 执行任意代码。这个风险来自"runner 自动执行别人推来的代码"，不来自"在主机上跑测试"。所以主机这一边设计成：

- **手动启动。** 一个脚本 `tools/ci/host_run.sh`，由所有者在终端里或由 Claude 会话启动：`host_run.sh --pr 123 --profile quick`（或 `--sha <提交>`、`--profile checkpoint`）。没有常驻进程，不监听任何东西，不从 GitHub 收任务。
- **只跑可信的代码。** 脚本先查 PR 的作者是仓库所有者、head 分支在 `naco-siren/opentoonz` 上（不是 fork），否则拒绝，除非加 `--i-have-read-the-diff`。
- **harness 与被测代码分开。** 跑测试的脚本、比较工具、阈值、已知差异登记表、AI 提示词，都取自主检出的基准分支（`nexttoonz/phase0`），而不是被测 PR 的那一份；PR 改了它们时，脚本在结果里标出"本 PR 修改了验收工具"，交给人看。否则一个 PR 可以通过放宽阈值让自己通过。
- **隔离的工作目录。** 被测提交检出到一个专用的 git worktree（例如 `~/nexttoonz-ci/work/<sha>`），用独立的构建目录和共享的本地 ccache（`CCACHE_BASEDIR` 设成 worktree 根，换路径也能命中）。不碰主检出、不碰 `samples/`、不碰别的会话的 worktree。可选的加固：用一个专门的 macOS 用户跑。
- **回报经最小权限的 token。** 结果的全文留在主机（`~/nexttoonz-ci/results/<sha>/`）；GitHub 上只发两样：commit status（`POST /repos/{owner}/{repo}/statuses/{sha}`，context 如 `host/quick`、`host/gui`、`host/accept`，描述里写一句结论）和一条 PR 评论（简报全文）。用一个只对这个仓库、只有 commit statuses 和 pull requests 写权限的细粒度 token。托管 runner 的 artifact 不能从外面上传，所以大的结果（图）不上 GitHub；简报里引用的图如果要给别人看，所有者可以手动附到评论里。
- **主机上的 status 不进分支保护的必需列表**（如果以后加分支保护）：主机不在线时 PR 不应当卡死。检查点之前由人确认主机那一项是绿的。

### 4.3 结果包的布局

托管 runner 和主机写同一种结构，托管 runner 作为 artifact 上传（`results-<platform>-<sha>`），主机写在本地：

```
results/<sha>/<platform>/
  manifest.json        被测与基准提交、构建选项、probe 的输出、各阶段的起止时间与退出码
  gates.json           各道关卡的机器判定：pass / fail / known / skipped，附阈值
  junit/ctest.xml      ctest --output-junit
  golden/<scene>.json  逐帧：像素哈希、对基线的指标（超阈像素比例、峰值、非空白像素）、确定性级别
  ab/<threads>/<scene>.json   逐帧：基准与被测的像素哈希，相同或不同
  diff/<scene>/<frame>.png    只给不同的帧：被测帧、热力图、按格子的差异表（.json）
  sheets/              对照总览图：不同的帧按"X / Y / 热力图"排（X、Y 随机对应基准与被测，见 4.4）；
                       每次都有一张参考工程全帧的小图
  gui/                 主机的 GUI 截图与对照（只在主机）
  perf/perf.json       每个场景的中位数、离散度，与基准的比值
  logs/                裁剪过的日志（每个失败项的最后 200 行），崩溃报告的路径
  triage.json          机器写的摘要：关卡、不同的帧按变化量排序、与已知差异登记表的匹配结果、金丝雀
  accept/verdict.json  AI 的结论（4.4）
  accept/brief.md      给人的简报（4.5）
```

大小：参考工程全部帧约 6 MB；只存不同的帧，一般的 PR 在 1 MB 以内。artifact 保留 30 天就够（检查点的包在主机上长期留）。

### 4.4 AI 验收

**它收到什么。** 只是文件，不是截图会话：`triage.json`、`gates.json`、`manifest.json`、PR 的标题和说明（第二轮才给，见下）、`git diff --stat` 与改动文件列表、不同帧的总览图（最多 10 张，按 `triage.json` 的排序取）、失败项的裁剪日志、已知差异登记表、这一步在计划里的验收标准（从设计文档摘出的那一段，例如 core_noqt 第 7 节的"验收"一栏）。

**它必须输出什么。** 一个按 schema 校验的 `verdict.json`：

```json
{
  "schema": "nexttoonz.accept/1",
  "sha": "…", "base": "…",
  "verdict": "accept | needs-human | reject",
  "gates_respected": true,
  "findings": [
    {"rank": 1, "kind": "unexpected-pixel-change | known-difference | flaky | perf | log-anomaly | tool-change | not-covered",
     "claim": "vector_styles 第 3 帧左下格的描边变细，基准和被测之间 1.9% 的像素差超过 2",
     "evidence": ["ab/1/vector_styles.json#frames[2]", "diff/vector_styles/0003.png"],
     "expected_by_pr": false,
     "human_check": "打开 sheets/ab_vector_styles.png 第 3 行对比左下格"}
  ],
  "canary": {"id": "…", "found": true},
  "not_verified": ["Windows", "真机 iPad", "交互工具"],
  "questions_for_owner": []
}
```

再由 `brief.md` 把它渲成给人读的一页。

**不让它随手盖章的办法。**

1. **机器先判，AI 不能翻案。** `gates.json` 里任何一道 `fail`，结论只能是 `reject` 或 `needs-human`；`verdict` 与 `gates.json` 矛盾时校验脚本拒收。AI 可以把全绿的结果升级为 `needs-human`，不能把红的降为 `accept`。
2. **已知差异由程序匹配。** 登记表 `tests/known_differences.yaml`，每条写：场景（和帧、区域）、平台、指标的上界、原因、对应的已知问题号、何时复查。第一批条目：Phase 1 矢量边缘的跨平台差、`vector_styles` 的 2.8%、字体场景的隔夜变化、AMD 驱动的虚线相位（GL 退路）、`fx_sweep_gpuin_a` 在 macOS 上崩溃。`triage.py` 做匹配，超过上界的不算已知。AI 只能建议新增条目，新增要人批准（PR 里改这个文件就是"修改了验收工具"，见 4.2）。
3. **盲比。** 总览图里两边标 X 和 Y，随机对应基准与被测，对应关系只写在 AI 看不到的 `triage.key`。第一轮只给图和数字，要它描述每处差异、猜哪边是改动后的；第二轮才给 PR 的说明，问差异是不是 PR 想要的。两轮说法对不上的条目自动列为 `needs-human`。这样它不会因为 PR 说"不改变渲染"就看不见差异。
4. **金丝雀。** `triage.py` 每次往要看的帧里混一张事先做好的、有真实缺陷的帧（少一笔、颜色错一档、整体平移一像素，从一个小库里轮换），编号写在 `triage.key`。`verdict.json` 没有把它列为问题，这次验收作废，结论按 `needs-human` 发。金丝雀不进给人的简报。
5. **证据可核对。** `check_verdict.py` 检查每条 `evidence` 路径存在、引用的数字与 `triage.json` 一致、排名第一的条目确实是变化量最大的未登记差异之一。核对不过就退回重写一次，再不过就按 `needs-human` 发并在简报里说明。
6. **不许说"看起来没问题"而不给依据。** 提示词要求：结论是 `accept` 时，也要列出它看过的最重要的三处以及为什么认为没问题。

### 4.5 给人的简报

一页，最多 15 行，先结论后证据，中文：

```
NextToonz 验收简报 · PR #123 · 84f17b3 → 9ab12cd · 主机 quick + CI · 2026-11-19
结论：需要你看 2 处（机器关卡全绿）
关卡：构建 7/7 · 单测 386/386 · golden 25/25 · A/B 逐字节 271/277（6 帧为已登记差异 KD-3）
      · 清洁剂 ✓ · 无 Qt + iPad ✓ · 性能 +2%（阈值 10%）· GUI 3/3 截图在容差内
请看：
1. vector_styles 第 3 帧左下格描边变细（1.9% 像素，PR 说明没提到）→ diff/vector_styles/0003.png
2. 本 PR 修改了 tests/known_differences.yaml（新增 KD-7）→ 需要你批准
没验证：Windows、真机 iPad、工具手感
需要你决定：KD-7 是否接受
```

- 每一行都能在结果包里找到出处；简报里的数字由脚本填，AI 只写"请看"和"需要你决定"两段的文字。
- 托管 runner 的部分在主机运行时一并取来（`gh run download`），所以简报只有一份。

性能的阈值（S7）：主机上 5 次中位数，被测比基准慢 10% 以上列入"请看"，慢 25% 以上在检查点算失败；托管 runner 的数字太抖，只画趋势。

### 4.6 Claude 会话怎样调用

全部是命令和文件，不看截图会话：

```bash
tools/ci/host_run.sh --pr 123 --profile quick          # 拉取、构建、跑；写 results/<sha>/
tools/ci/triage.py ~/nexttoonz-ci/results/<sha>         # 机器摘要、盲标、金丝雀
claude -p "$(cat tools/ci/accept_prompt.md)" --add-dir ~/nexttoonz-ci/results/<sha>
                                                        # 非交互的 Claude Code，写 accept/verdict.json 与 brief.md
tools/ci/check_verdict.py ~/nexttoonz-ci/results/<sha>  # schema、证据、金丝雀
tools/ci/post.sh ~/nexttoonz-ci/results/<sha> --pr 123  # commit status 与 PR 评论
```

- `host_run.sh` 默认把这五步串起来；分开写是为了某一步失败时能单独重跑。
- 做实现的会话（例如 Phase 2 的工作会话）在报告一个步骤做完时，直接附上 `brief.md`，不再手写验收清单；管理会话读简报和 `verdict.json` 做决定，需要时再打开引用的图。
- 验收用的会话与做实现的会话分开（新开一个，不继承实现的上下文），避免"自己验自己"。
- `claude -p` 的具体参数（输出格式、可用工具的限制、预算上限）在 Q6 落实时按当时的 Claude Code 版本确定；原则是验收会话只读结果包、只写 `accept/` 目录。

---

## 5. 每个 PR 与每个检查点

### 5.1 跑什么

| | 每个 PR（或推送） | 每个检查点（例如"Phase 2 第 1 到 8 步"） |
|---|---|---|
| 托管 runner | S0 到 S6 全部；S4 用基准提交的产物；对照总览图；不打包 | 同左，加打包（.app、.dmg、Linux 包）、Windows（手动触发）、S7 的趋势 |
| 主机 | `quick`：增量构建、单测、golden、A/B（线程 1 与 8）、GUI 3 个场景、AI 验收（一轮盲比） | `checkpoint`：干净构建、A/B 加 GL 退路与"之前"包、GUI 全部场景、性能判定、Linux 虚拟机、iPad 模拟器、AI 验收（两轮盲比，更多图） |
| 人 | 读简报，2 到 5 分钟 | 读简报，加 10 分钟以内的固定手动清单（3.8 节第 4 条） |

### 5.2 时间与成本预算

| 项 | 今天 | 目标 | 怎么做到 |
|---|---|---|---|
| 推送到托管 CI 全绿 | 61 分钟 | 35 到 40 分钟（估计） | 非检查点不打包（macOS x86_64 省约 15 分钟，arm64 约 8 分钟）；缓存能跨分支用（改走 PR，5.3 节）；`ctest -j`（要先确认测试互不干扰） |
| 主机 `quick` | 无（人工 45 到 60 分钟） | 15 分钟以内 | 增量构建 3 到 5 分钟、测试与 A/B 2 分钟、GUI 3 分钟、AI 5 分钟 |
| 主机 `checkpoint` | 无 | 90 分钟以内，加虚拟机 60 分钟，无人值守 | |
| 人 | 每个检查点 45 到 60 分钟 | 每个 PR 5 分钟，每个检查点 15 分钟 | 简报加固定的小清单 |
| 托管 runner 的钱 | 0（公开仓库的标准 runner） | 0 | 不用 larger runner；macOS 并发上限 5，同时最多 3 个 macOS 作业 |
| AI | 无 | 每次验收的输入控制在约 10 万 token 以内、图最多 10 张 | 只给摘要和排过序的图，不给全量日志；所有者定预算上限（第 8 节问题 3） |

### 5.3 缓存

- **ccache。** 现状见 2.5 节。要做的：每个作业结束打印 `ccache -s` 并写进结果包（先量清楚）；设 `CCACHE_BASEDIR`、压缩、`max_size`（比如 500 MB，按测到的大小定）；Linux 作业去掉重复的 ccache 包装；Fedora 作业加上。最关键的是分支隔离：GitHub 的缓存只能被同一分支和默认分支读取，PR 的运行还能读目标分支的缓存。所以**改走 PR 进 `nexttoonz/phase0`**（`nexttoonz/phase0` 的推送攒缓存，PR 读它）是让缓存起作用的前提；另一个办法是把默认分支改成 `nexttoonz/phase0`。两者都要所有者定（第 8 节问题 2）。
- **Skia。** macOS 用 skia-pack 的 zip（34 到 36 MB，按脚本的哈希做键），Linux 用 vcpkg 编出的安装目录（19 MB）。键已经合理，问题同样是分支隔离：这次三个 Linux 作业各花 4 到 8 分钟重编 Skia。改走 PR 后只在 `ci-scripts/skia/` 变化时重编。
- **vcpkg。** 只用来编 Skia，二进制缓存目录和 Skia 缓存是一回事。其余依赖走 apt、brew、dnf，装依赖 1 到 6 分钟；brew 的缓存收益不确定，暂不做。
- **基准渲染。** 基准分支每次推送上传一份 `results-<platform>-<sha>`（含全部帧的像素哈希，PNG 只存一份最新的）。PR 运行用 `actions/download-artifact` 带 `run-id` 取基准提交那次运行的产物，不用再编基准。主机按需 `gh run download`。
- **主机。** 一份共享的本地 ccache（所有 worktree 共用，`CCACHE_BASEDIR`），Skia 用已解开的 skia-pack（`toonz/build-phase1/skia-pack/`）。"之前"包按提交存（今天 `ab/phase2/bundles/` 的做法），检查点开始时自动建。

### 5.4 基线的产生与更新

今天：PNG 存在 `tests/golden/expected/`。纯栅格场景的基线来自 Ubuntu clang 的 CI 作业；有矢量的场景是 x86_64 macOS 上的 Skia 渲染（Phase 1 第 9 步）；`vector_styles` 来自 Linux。新场景的基线要推送后从 CI 产物里取，一轮半小时以上。

Skia 之后，主机可以产生大部分基线。需要的是：

1. **出处。** 每个场景的基线目录加 `BASELINE.json`：来源（`host-x86_64-macos`、`ci-ubuntu-clang`…）、提交、构建选项（Skia 版本、栅格化后端）、确定性级别（3.4 节）、为什么更新（一句话）、谁批准的。`nexttoonz test golden --update --reason "…"` 写它，不给理由就拒绝更新。
2. **按平台的哈希清单。** 主机更新基线时同时写 `hashes.macos-x86_64.json`；托管 runner 在同一个 PR 里给出 `hashes.linux-x86_64.json`、`hashes.macos-arm64.json`（作为产物，由更新基线的脚本收进 PR）。有清单的平台以后逐字节比较。
3. **交叉验证。** 主机产生的基线必须在同一个 PR 的托管 runner 上（Linux 两种编译器、macOS arm64）以容差通过，否则不收。
4. **外部状态依赖的场景不由主机产生**：`pinned` 级（字体、`rand()`、GLSL）的基线仍来自 Linux 作业，因为它们依赖 glibc、Mesa 和 Linux 上的字体。
5. **每次基线更新都有对照图和人的批准。** 更新基线的 PR 自动带"旧 / 新 / 热力图"的总览图，AI 验收照常做（盲比），人在简报上批准；commit message 写清是哪几个场景、为什么。

---

## 6. 路线图

每一项单独合并、单独验证。"现在就能做"的不依赖 Phase 2；其余标出依赖的 Phase 2 步（`doc/nexttoonz_core_noqt.md` 第 7 节）。工作量是一个熟手的人天，粗估。

### 6.1 现在就能做的（在现有代码上）

| 编号 | 内容 | 依赖 | 工作量 | 怎样验证 |
|---|---|---|---|---|
| Q1 | A/B 工具进仓库：把 `ab/phase2/ab_tcomposer.py` 收进 `tools/ab/`（位置见第 9 节），去掉写死的路径；输出 JSON，逐帧给出解码后预乘像素的哈希；支持多个线程数与 `NEXTTOONZ_VECTOR_RASTERIZER` | 无 | 1 | 同一构建对自己：全部相同；Skia 对 GL 退路：报告出 Phase 1 的已知差异；与旧脚本在同一对构建上结论一致 |
| Q2 | 结构化测试结果：所有 workflow 的 ctest 加 `--output-junit` 并上传；`run_golden.py` 每个场景写一个 JSON（逐帧指标与像素哈希） | 无 | 0.5 | 故意改坏一个基线帧，JSON 和 JUnit 里都标出那一帧；CI 产物里有这些文件 |
| Q3 | 每次 CI 都传对照总览图：渲染输出（只有 6 MB）与 `contact_sheet.py` 的图总是上传，不只在失败时 | Q2 | 0.5 | 一次绿的运行里能下载到总览图 |
| Q4 | 非检查点不打包：macOS 的拷库、改路径、dmg 和 Linux 的打包只在 `master`、带 `package` 标签的 PR 和手动触发时做 | 所有者同意（问题 7） | 0.5 | 一次推送的 macOS x86_64 作业比 55 分钟短 10 分钟以上；手动触发仍出包 |
| Q5 | 让编译缓存起作用并看得见：各作业打印 `ccache -s`、设 `CCACHE_BASEDIR`、去掉 Linux 的双重包装、Fedora 加 ccache；配合问题 2 的决定 | 问题 2 | 1 | 连续两次只改一个 `.cpp` 的推送（或 PR），第二次命中率在 90% 以上 |
| Q6 | AI 验收的骨架：`tools/ci/triage.py`（由 Q1、Q2 的 JSON 写 `triage.json`、盲标、金丝雀）、`accept_prompt.md`、`verdict` 的 JSON Schema、`check_verdict.py`、简报模板 | Q1、Q2 | 2 | 拿 Phase 2 第 1 到 8 步的已有结果做一次：简报与人工 A/B 的结论一致；金丝雀被找出；故意删掉证据路径时核对失败 |
| Q7 | 主机运行脚本 `tools/ci/host_run.sh`（4.2 节的全部约束）与 `post.sh`（commit status、PR 评论） | Q1、Q6 | 2 | 对一个所有者的 PR 跑 `quick`，GitHub 上出现 `host/quick` 状态与评论；对一个 fork 的 PR 拒绝；PR 改了 `tests/known_differences.yaml` 时简报里有"修改了验收工具" |
| Q8 | 已知差异登记表 `tests/known_differences.yaml` 与匹配器（在 `triage.py` 里），收入 4.4 节的第一批条目 | Q6 | 1 | 在 Linux 与 macOS 的结果上跑，这几条差异被标为已知，其余差异照常列出；把某条的上界调小后它重新出现 |
| Q9 | 基准渲染产物与 PR 的 A/B：`nexttoonz/phase0` 的推送上传逐帧哈希，PR 运行取它比较 | Q1、Q2；问题 2 | 1.5 | 一个只改文档注释的 PR：A/B 全部相同；一个故意改一个特效默认值的 PR：只有用到它的帧不同 |

另外，Q0（可选，推荐）：**命令行契约文档与 Python 前端 `tools/nt`**。把第 3 节写成 `doc/` 里的规格加 JSON Schema，`tools/nt` 用 tcomposer、tconverter 和 Q1 的比较代码实现 `probe`（一部分）、`render`、`convert`、`diff`、`sheet`、`test`。工作量 3 到 5 人天。好处是 Claude 会话和 CI 从现在起就用最终的命令和 JSON；C++ 版做出来以后只换实现。验证：`tools/nt test --suite pr` 的结果包与 Q7 的相同。

### 6.2 等引擎 API 的

| 编号 | 内容 | 依赖的 Phase 2 步 | 工作量 | 怎样验证 |
|---|---|---|---|---|
| C1 | `ntcli` 库与 `nexttoonz` 程序：`probe`、`render`、`convert`，退出码与 JSON 按契约 | 第 11 步（引擎 API 第一版：Engine、Scene、RenderJob） | 5 到 8 | `render` 的像素哈希与 tcomposer 的 golden 帧逐字节相同（全部场景，线程 1 与 8）；Q0 的调用方不改一行就能切到它 |
| C2 | tcomposer、tconverter、tcleanupper 变成 `ntcli` 的外壳，保留旧参数与退出码；修已知问题 34 与 tcleanup 的空指针、`fatalError` 不再 `abort()` | 第 11 步及其后让 tconverter、tcleanupper 走引擎 API 的工作（Q7） | 3 到 5 | golden 逐字节不变；旧用法的一组命令行（农场参数在内）输出与退出码不变；macOS 上 tcleanup 能跑完 `raster_png_basic` 的清理 |
| C3 | `cleanup` 子命令 | C2 | 1 | 清理结果与应用里清理的结果逐字节相同（同一场景） |
| C4 | `inspect`：scene、output、palette（含 `--lang`）、levels；按 id 排序、不受写出顺序影响 | 引擎 API 加只读视图（第 11 步之后的小版本；调色板与样式的文字已有 `TTranslatableText`，第 7 步） | 4 | 同一场景存两次（字节不同）`inspect` 相同；11 种语言的样式名与应用里一致（沿用第 7 步的对照数据） |
| C5 | `inspect xsheet/fx/stage`，`diff scene`、`diff palette` | 第 16、17 步（Handle 与模型类去 QObject 后才有稳定的只读接口） | 4 | 参考工程每个场景导出、再与生成器的意图对照（`extras/*.txt`）；改一个单元格，`diff scene` 只报那一处 |
| C6 | `run` 场景脚本，第一批：打开、保存、调色板、输出设置、渲染、缩略图（清单第 5、6、7 条） | 第 11 步与 C4 | 4 | 三个脚本在主机与 Linux 上都过；故意把保存改坏（不写调色板），脚本失败 |
| C7 | `run` 第二批：单元格、列、关卡的编辑 | 第 21 步（iPad MVP 的命令进库） | 3 | 命令的 characterization 测试与脚本的结论一致 |
| C8 | `thumbs`：场景图标、关卡与样式图标 | 第 11 步（场景图标经 `renderFrame` 的 CPU 路径，Phase 1 第 8 步已有） | 2 | 与应用存场景时写的 `sceneIcons/*.png` 逐字节相同 |
| C9 | iPad 模拟器线：iOS 宿主链接 `ntcli`，`render --hash-only` 与 macOS 哈希比较 | 第 20 步（核心不带 Qt）；第 13 步（LZO 直接链接，否则 tlv 场景在 iPad 上读不了） | 3 | 参考工程中不依赖字体与 GLSL 的场景，哈希与 x86_64 macOS 相同（或差异列入登记表并解释） |
| C10 | 无 Qt 的 `nexttoonz` 进 macOS-noqt 作业 | 第 20 步 | 1 | 不装 Qt 的 runner 上 `render` 全部场景与带 Qt 的构建逐字节相同 |

### 6.3 不依赖引擎 API、但比快速项大的

| 编号 | 内容 | 依赖 | 工作量 | 怎样验证 |
|---|---|---|---|---|
| G1 | GUI 自动化钩子（3.8 节）与第一批 6 个场景 | 无（用应用现有的命令管理器）；问题 5 | 7 到 10 | 在主机上对"之前"包与被测包各跑一遍，截图在 `edge` 容差内；故意把一个样式的图标弄坏，第 5 条的截图差异被列为第一位 |
| G2 | 性能线：1080p 的性能场景（参考工程的摄像机放大，`render --camera`）、5 次中位数、阈值 | Q1；C1 之前可以用 tcomposer | 2 | 同一构建跑两次，差距在 3% 以内；故意加一个睡眠，被判为变慢 |
| G3 | 主机产生基线（5.4 节的出处、哈希清单、交叉验证） | Q2、Q9 | 2 | 一个新场景从主机出基线，托管 runner 交叉验证通过；改一个 `pinned` 场景的基线被拒 |
| G4 | （可选）托管 runner 上的 AI 验收 | Q6；问题 3 | 1 到 2 | 与主机上的验收对同一结果包结论一致 |

### 6.4 先后

```
Q1 ─┬─ Q2 ─┬─ Q3
    │      ├─ Q6 ─┬─ Q7
    │      │      └─ Q8
    │      └─ Q9 ── G3
    └─ G2
Q4, Q5（各自独立；Q5、Q9 依赖问题 2 的决定）
Q0（可与 Q1 合做）
第 11 步 ── C1 ─┬─ C2 ── C3
                ├─ C4 ── C6 ── （第 21 步）C7
                ├─ C8
                └─ （第 16、17 步）C5
第 20 步 ── C9, C10
G1 随时，可与 Phase 2 并行
```

建议的顺序：Q1、Q2、Q4、Q5 一周内做完（CI 时间先降下来），然后 Q6、Q8、Q7（第一次用简报代替人工 A/B，目标是 Phase 2 第 9 步的验收），G1 在第 9、10 步之前做出第一批（那两步的界面交互只能人工验收，是最需要它的时候）。C 系列跟着 Phase 2 走。

---

## 7. 给 `doc/nexttoonz_plan.md` 的文字

建议加在第 4 节"分阶段计划"的末尾，作为横跨各阶段的一节，并在 D7 和第 5 节各加几行。原文如下，可直接粘贴。

**第 4 节新增：**

> ### 横向：命令行与 CI 验收
>
> 设计见 `doc/nexttoonz_cli_ci.md`。目标：每个检查点、最好每个 PR 的验收都是"一条命令 → 托管 runner 和所有者的主机计算 → AI 验收写报告 → 人读一页简报"，人工清单只留界面手感那一部分。
>
> 工作项：
>
> - 命令行入口 `nexttoonz`（子命令 `probe`、`render`、`convert`、`cleanup`、`inspect`、`diff`、`thumbs`/`sheet`、`run`、`test`），JSON 输出与统一的退出码，建在引擎 API 上；契约先定，先用包着现有程序的 Python 前端。tcomposer、tconverter、tcleanupper 保留为兼容外壳，与它共用代码。
> - 渲染结果带像素哈希与确定性级别；基线按平台存哈希清单，能逐字节的平台逐字节比较。
> - 流水线：托管 runner 跑构建、单测、golden、对基准提交的 A/B、清洁剂、无 Qt 与 iPad 模拟器；所有者的主机由手动启动的脚本参与（不是自托管 runner），跑 GL 退路、GUI 自动化截图、性能、Linux 虚拟机，并发 commit status 与 PR 评论。
> - AI 验收读结果包，输出带证据的结论和待看清单；机器关卡决定通过与否，AI 不能翻案；已知差异登记表、盲比、金丝雀防止它随手盖章。
> - OpenToonz 里一个薄的 GUI 自动化钩子，只覆盖命令行覆盖不了的显示部分。
>
> 完成判据：
>
> - Phase 2 的检查点由简报加不超过 15 分钟的人工清单验收（今天 45 到 60 分钟）。
> - 推送到托管 CI 全绿不超过 40 分钟。
> - `nexttoonz render` 的结果与 tcomposer 的 golden 逐字节相同；Phase 2 结束时它也在无 Qt 构建和 iPad 模拟器里跑。
>
> 依赖：Phase 2 第 11 步（引擎 API 第一版）之前只做不依赖引擎 API 的快速项（设计文档 6.1 节）；`inspect` 的 xsheet 部分等第 16、17 步，场景脚本的编辑操作等第 21 步，iPad 线等第 20 步。

**D7 末尾新增：**

> 补充（2026-11-19，`doc/nexttoonz_cli_ci.md`）：Skia 之后渲染分四级确定性（exact、tolerant、pinned、unstable）。同机 A/B 一律逐字节；对基线，有该平台哈希清单的逐字节，其余保留容差。基线可以由所有者的主机产生，但要写明出处、经托管 runner 交叉验证；依赖字体、`rand()`、GLSL 的场景仍由 Linux 作业产生。

**第 5 节"待定事项"新增几行：**

> | 主机参与 CI 的方式 | 手动启动的本地脚本，发 commit status 与 PR 评论；不用自托管 runner（D5 不变） | 所有者决定 |
> | 改走 PR 进 `nexttoonz/phase0` | 倾向改：缓存只能跨到目标分支，每 PR 的 A/B 与 clang-format 也靠 PR；与"CI 阻塞合并的落实方式"一起定 | 所有者决定 |
> | AI 验收在哪里跑、能否挡合并 | 默认在主机上的 Claude 会话；只能提出问题，不能让红灯通过；托管 runner 上的版本可选 | 所有者决定 |
> | GUI 自动化钩子是否进发布版 | 倾向只在开发构建里编 | 所有者决定 |

**第 6 节"风险"中"基线只能来自 CI"一条后补一句：**

> （2026-11-19）缓解的具体做法见 `doc/nexttoonz_cli_ci.md` 5.4 节：主机出基线、托管 runner 交叉验证。

---

## 8. 需要所有者决定的问题

**决定（2026-11-19，所有者委托主会话拍板的部分）：** 问题 5 取 (a)（GUI 自动化钩子只在开发构建里编，`WITH_AUTOMATION` 发布预设默认关）；问题 6 同意程序名现在就叫 `nexttoonz`，旧的三个程序名不变；问题 7 同意非检查点的推送和 PR 不打包，只在 `master`、手动触发、带 `package` 标签的 PR 上出包；问题 8 同意主机按 5.4 节的条件产生基线（出处、哈希清单、托管 runner 交叉验证、`pinned` 场景除外、每次有对照图和人的批准）；问题 9 同意先做 Python 前端 Q0，与 Q1 合做。**留给所有者的：** 问题 1（主机怎样参与，涉及 D5 的安全边界）、问题 2（是否改走 PR 进 `nexttoonz/phase0`，改变所有者的工作流）、问题 3（AI 验收在哪里跑、预算上限）、问题 4（AI 结论的权力；倾向机器关卡决定、AI 只能升级为"要人看"、合并永远由人定）。下面保留原文作为依据。

每个问题给出倾向。

**问题 1 主机怎样参与。** (a) 手动启动的本地脚本，拉 PR、跑、发 commit status 与评论，harness 取自基准分支（4.2 节）；(b) 自托管 runner，只对所有者的推送启用；(c) 主机不参与，GPU 与 GUI 继续人工。倾向 (a)。(b) 的风险正是 D5 说的，而且"只对所有者启用"要靠 workflow 里的条件，一个改了 workflow 的 PR 就能绕过；(c) 留下了今天最慢的那部分。

**问题 2 是否改走 PR 进 `nexttoonz/phase0`。** 今天各工作分支在本地整理后快进合并、直接推送。GitHub 的缓存只能从同一分支、默认分支和 PR 的目标分支读，所以直接推送拿不到 `nexttoonz/phase0` 的缓存（2.5 节的 0 命中），也没有"对基准提交的 A/B"可做，clang-format 也只在 PR 上跑。选项：(a) 每个检查点开一个 PR 进 `nexttoonz/phase0`，CI 绿、简报读过后合并；(b) 把仓库的默认分支改成 `nexttoonz/phase0`（缓存共享，但上游同步的 `master` 就不再是默认）；(c) 维持现状。倾向 (a)，也顺带落实计划里"CI 阻塞合并"那一条（可以加分支保护，也可以不加）。

**问题 3 AI 验收在哪里跑、花多少。** (a) 主机上的 Claude 会话，由 `host_run.sh` 调用，不在 GitHub 上放 API key；(b) 托管 runner 上的 GitHub Action，要在仓库里放 API key（fork 的 PR 拿不到 secret，所以只对所有者的 PR 有效）；(c) 两者都要。倾向 (a)，等用顺了再看要不要 (b)。另请定一个每次验收的预算上限（5.2 节按约 10 万输入 token、10 张图设计）。

**问题 4 AI 的结论有多大权力。** 倾向：机器关卡决定通过与否，AI 只能把绿的升级为"要人看"，不能把红的变绿；合并永远由人决定。如果所有者希望"关卡全绿且 AI 说可以"时自动合并，请明说，那需要分支保护和更多防线。

**问题 5 GUI 自动化钩子进不进发布版。** (a) 只在开发构建里编（`WITH_AUTOMATION` 默认在开发预设里开、发布里关）；(b) 发布版也带，给高级用户和第三方测试用；(c) 不做，界面全靠人工。倾向 (a)：钩子能按 id 执行任意命令，不该随便进用户手里。

**问题 6 程序名。** 倾向现在就叫 `nexttoonz`：它是新程序，不涉及计划里"改名的时机"所说的产品名、stuff 路径；旧的三个程序名不变。

**问题 7 非检查点不打包。** 倾向：每次推送和 PR 不再出 .app、.dmg 和 Linux 包，只在 `master`、手动触发、带 `package` 标签的 PR 上出；人工验收要包时手动触发一次，或用主机的构建。省 macOS 作业 8 到 15 分钟。

**问题 8 主机能否产生基线。** 倾向可以，按 5.4 节的条件（出处、哈希清单、托管 runner 交叉验证、`pinned` 场景除外、每次都有对照图和人的批准）。

**问题 9 要不要先做 Python 前端（Q0）。** 倾向做：3 到 5 人天，让 Claude 会话和 CI 从现在起就用最终的命令和 JSON，第 11 步之后只换实现。不做的话，快速项 Q1、Q2、Q6 照样可以做，只是将来调用方要改一次。

---

## 9. 与其他文档的接触点

- **仓库与构建布局（`doc/nexttoonz_layout.md`，另一个会话在写）。** 本文假设的位置要和它对齐：`tools/`（`tools/ab/`、`tools/ci/`、`tools/nt/`）放在仓库根还是 `toonz/sources/` 下；`nexttoonz` 与 `ntcli` 的 CMake 目标放在哪个目录；`tests/scenarios/`、`tests/known_differences.yaml` 的位置；`ci-scripts/` 是否并入 `tools/ci/`；`CMakePresets.json` 里加不加 `host-quick`、`host-checkpoint` 两个预设；主机的工作目录（`~/nexttoonz-ci/`）与仓库里被忽略的 `toonz/build*` 的关系；`toonz/build-phase1/ab/` 里哪些工具值得收进仓库。本文不决定这些，以布局文档为准。
- **核心去 Qt（`doc/nexttoonz_core_noqt.md`）。** 第 9 节引擎 API 的第一版要多考虑一个消费者：`nexttoonz` 需要"只出像素缓冲不写文件"的渲染（第 9 节已经有）、逐帧的进度回调、以及将来的只读视图。第 11 步的验收（tcomposer 逐字节不变）正好是 C1 的验收。
- **计划（`doc/nexttoonz_plan.md`）。** 第 7 节的文字。本文不改它。
- **已知问题（`doc/nexttoonz_known_issues.md`）。** 建议登记新发现：`tcleanupper.cpp:596` 在找不到工程时解引用空指针。

---

## 10. 未验证与风险

- **ccache 命中率低的原因**是从缓存的分支范围和改动规模推出来的，没有做"同一分支连推两次小改动"的实验；Linux 作业的双重包装是否真的生效也没看统计。Q5 的验证就是这个实验。
- **`ctest -j` 是否安全**没有验证：golden 各用自己的工作目录，但 toonzlib 的测试和 tcomposer 是否都只写自己的临时副本要逐个确认。
- **本机全量构建时间**是由 `.ninja_log` 的编译时间之和推算的，没有做干净构建。
- **AI 验收的效果**没有试过。Q6 的验证用 Phase 2 第 1 到 8 步的已有材料回放一次，看它是否得出与人工 A/B 相同的结论、是否找得到金丝雀。金丝雀和盲比能挡住多少"随手盖章"，要靠几次真实使用来校准。
- **GUI 截图比较的稳定性**：同一台机器、同一个包，viewer 的截图是否每次逐像素相同（字体渲染、窗口大小、高 DPI、动画）未验证；G1 的第一件事是量这个。
- **托管 runner 的 macOS 有窗口服务器，但 GPU 是虚拟的**，所以 GUI 截图只在主机上有意义；主机不在线时这部分就缺。
- **跨 runner 镜像更新**：GitHub 更新 runner 镜像（Xcode、系统字体、Mesa）会改变 `pinned` 场景，可能让一次与代码无关的推送变红。登记表要能按"镜像版本"匹配，`probe` 要把镜像版本写进 `manifest.json`。
- **arm64 macOS 的确定性**未知（3.4 节）；在有 arm64 的哈希清单之前，arm64 只做容差比较。
- **主机脚本发 PR 评论**是对外的动作，每次都由所有者或受委托的会话启动；token 的保管与轮换由所有者负责。

---

## 11. 附录 A：测量方法

- CI 时间：`gh run list -R naco-siren/opentoonz --branch nexttoonz/phase0`，再对 `84f17b336` 的三次运行（44138728148、44138728131、44138728162）取 `gh api repos/naco-siren/opentoonz/actions/runs/<id>/jobs` 的各步骤起止时间；ctest 总时间、最慢的 golden、ccache 统计取自 `gh run view <id> --log`。缓存列表取自 `gh api repos/naco-siren/opentoonz/actions/caches`。
- 用法行：在 `toonz/build-phase1/src/toonz/build/toonz/OpenToonz.app/Contents/MacOS/` 下以 `-help` 运行 tcomposer、tconverter，tcleanup 不带参数运行（它在解析参数前就 `abort()`），`QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM=1`，每次 20 秒超时。
- tcomposer 的时间：一个临时的 Python 脚本（不进仓库），把 `stuff/` 和参考工程复制到会话的临时目录，对每个场景（跳过 `fx_sweep_gpu*`）分别渲第 1 帧（`-range 1 1`）和全部帧，`-nthreads 1`，记墙钟。`run_golden.py` 的时间是对 `fx_sweep_std_a` 跑一次。
- 单测时间：直接运行测试程序（`--gtest_brief=1`），没有经 ctest，以免往共享的构建目录里写东西。
- 全量编译：`toonz/build-phase1/src/toonz/build/.ninja_log` 里每个目标最近一次的编译时长之和。
