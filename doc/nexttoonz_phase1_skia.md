# NextToonz Phase 1：Skia 的获取方式

版本 0.4，2026-11-16（第 4 节改为 CI 的实际做法）。0.3 版 2026-11-09 补了 3.5 节 Skia 后端的跨平台像素对比。本文回答 `doc/nexttoonz_plan.md` 第 5 节的待定事项"Skia 的获取方式"：比较了几条路线，在本机把选中的一条走通，并记录实测数据。结论先行，数据和没有验证的部分在后面。

实测环境：Intel Mac（i7-10700K，8 核 16 线程），macOS 26.7，Xcode 26.5 的 Apple clang；Linux 部分在本机的 lima 虚拟机 `nt-asan` 里（Ubuntu 24.04 x86_64，14 核，GCC 13.3）。作为参照，同一台机器上 OpenToonz 的全量构建是 508 秒墙钟、4,890 秒 CPU。

---

## 1. 结论

**范围（所有者 2026-11-08）：一开始只要求一套完整的技术栈在 x86 macOS（开发用的这台 iMac）和 iPadOS 上正确运行。** 本文 0.1 版是按"覆盖 CI 的全部平台"写的，并据此推荐 vcpkg；那个前提不是所有者的要求。按现在的范围，结论改为：

**用 JetBrains skia-pack 的预编译包**（m144），三个目标各一个 zip：macOS x86_64、iPadOS 模拟器 x86_64、iPadOS arm64。不需要编译，不引入新的构建工具。

- 三个 zip 合计 128 MB，SHA-256 与 GitHub 记录的一致。
- macOS x86_64：链进本仓库，spike 的 8 个测试全部通过。
- iPad 模拟器（iPad Air 第 5 代，iOS 15.5 运行时）：7 个测试全部通过。没跑的那一个要用 tnzcore，而 tnzcore 链接 Qt，还不能为 iPadOS 构建。
- iPadOS arm64 真机：编译并链接成功，没有在真机上运行。
- 三处的场景哈希相同，并且与 vcpkg 编出的 m148 在 macOS 上的哈希相同。
- 许可证是 BSD-3-Clause。

第 7 节是这次验证的记录。**vcpkg 路线保留为后备**：需要自己控制 Skia 的编译参数，或者以后要在 Linux 上构建 Skia 后端时用它（预编译包在 Linux 上有 ABI 问题，见第 2 节）。第 2 到 6 节是 0.1 版关于 vcpkg 的内容，数据仍然有效，其中针对 CI 全平台的部分现在用不上。

落地的东西在分支 `wip/phase1-skia-spike` 上：`toonz/sources/tests/skia_spike/`（CMake 目标、8 个像素测试、iPadOS 的构建脚本、vcpkg 清单、说明），由 CMake 选项 `WITH_SKIA_SPIKE` 控制，默认关闭。现有渲染路径没有动。

---

## 2. 路线比较

| 路线 | 覆盖的平台 | 与本项目编译器的 ABI | 版本 | 维护成本 | 结论 |
|---|---|---|---|---|---|
| vcpkg `skia` port | 除 32 位 ARM Windows 和 MinGW 之外全部；有 iOS 三元组 | 就地从源码构建，与构建机一致 | m148（2026-11-07 的 vcpkg） | 钉一个 vcpkg 提交；port 由 vcpkg 社区跟进 Skia | 后备；要覆盖 Linux 时采用 |
| 预编译包：JetBrains skia-pack | macOS、Linux、Windows、iOS、iOS 模拟器、Android、tvOS、wasm；x64 与 arm64 | Linux 包用 GCC 10 加 `-D_GLIBCXX_USE_CXX11_ABI=0` 编译，与发行版默认的 libstdc++ ABI 不兼容 | m144（2026-02） | 依赖 JetBrains 为 Skiko 做的分叉和发布节奏 | 只做 Apple 平台时**采用**（第 7 节）；要覆盖 Linux 时否决 |
| 预编译包：aseprite/skia | macOS、Linux x64、Windows；没有 iOS | Linux 包绑定一种标准库 | m151（2026-07） | 是 Aseprite 自己的分叉 | 否决 |
| 预编译包：rust-skia 的 skia-binaries | 很全，含 iOS 真机与模拟器 | 为 Rust 绑定准备：只有静态库和 `bindings.rs`，不带 C++ 头文件 | 0.157（2026-11） | 要引入 Rust 工具链才用得顺 | 留作 D6 触发条件 1 的后备 |
| 自己用 GN 构建 | 全部 | 一致 | 任意 | 要自己取 depot_tools 或 `git-sync-deps`、自己维护各平台的 GN 参数、自己导出头文件和链接参数；这些正是 vcpkg port 已经做掉的事 | 否决，除非要改 Skia 本身 |
| 系统包 | 只有 Ubuntu 26.04 及以后（`libskia-dev` 146）和 Debian forky/sid | 一致 | 各发行版不同 | CI 用的 Ubuntu 22.04 和 Fedora 都没有；Homebrew 没有 | 不可行 |

依据：

- vcpkg port 的清单与构建脚本：`ports/skia/vcpkg.json`、`ports/skia/portfile.cmake`（vcpkg 提交 `ec48c11e`）。它用 vcpkg 自带的 GN 和 ninja，把 Skia 的第三方依赖换成 vcpkg 的包，并导出 CMake 配置。
- skia-pack 的 Linux 编译参数：其仓库 `script/build.py` 里写明 `cc="gcc-10"`、`-D_GLIBCXX_USE_CXX11_ABI=0`、`-fno-rtti`。
- 各预编译包的平台与版本来自它们的 GitHub release 列表（2026-11-08 查询）。
- 系统包的情况来自 Repology、Ubuntu 与 Fedora 的包搜索页（2026-11-08 查询），Homebrew 用本机 `brew info skia` 确认。

预编译包的共同问题不在平台覆盖，而在**它们是别人的产品的构建产物**：编译器、标准库、Skia 分支、打开的特性都由对方的需要决定。在 macOS 上这通常没事（只有一种 libc++），在 Linux 上 skia-pack 的旧 ABI 一条就足以否决。

vcpkg 路线的代价是第一次要编 3 到 5 分钟，以及 vcpkg 对 x86_64 macOS 和 iOS 不做持续集成（它的 CI 只有 arm64 macOS、x64 与 arm64 Linux、Windows、Android；这两个三元组属于"社区三元组"）。本次实测这两个目标都构建成功，但以后升级 vcpkg 基线时要自己验证。

---

## 3. 实测数据

### 3.1 构建时间

| 目标（三元组） | 方式 | 墙钟 | CPU（user） | 其中 Skia 本身 |
|---|---|---|---|---|
| x86_64 macOS（`x64-osx-release`） | 本机，冷 | 约 189 秒 | 约 1,800 秒 | 2.3 分钟 |
| arm64 macOS（`arm64-osx-release`） | 本机交叉编译，冷 | 189 秒 | 1,884 秒 | 2.5 分钟 |
| arm64 iOS（`arm64-ios-release`） | 本机交叉编译，冷 | 322 秒 | 3,552 秒 | 4.7 分钟 |
| x86_64 Linux（`x64-linux-release`，GCC 13.3） | 虚拟机（14 核），冷 | 252 秒 | 2,735 秒 | 3.4 分钟 |
| x86_64 macOS，二进制缓存已有 | 清单模式恢复 9 个包 | 4.8 秒 | | |
| x86_64 Linux，二进制缓存已有 | 清单模式恢复 12 个包 | 1.0 秒 | | |

说明：

- 第一行是两次运行相加（第一次在 Skia 之前因 Python 版本失败，见 5.1；已经编好的 abseil 和 expat 进了缓存，第二次没有重编）。第二行是一次完整的冷构建，数字更干净。
- 除 Skia 外还要编 abseil（27 秒）、expat（8 秒）、zlib（3 秒）和构建工具 pkgconf（9 秒）；GN 是下载的预编译版。
- 用 `-release` 三元组只编 Release。默认三元组（`x64-osx`、`x64-linux`）会把 Debug 也编一遍，时间和体积都翻倍。
- GitHub 托管 runner 是 3 到 4 个 vCPU。按 CPU 时间折算，冷构建估计 8 到 12 分钟（**推算，没有在 runner 上测**）。

### 3.2 体积

| 项目 | 大小 |
|---|---|
| `libskia.a`，x86_64 macOS | 12.7 MB |
| `libskia.a`，arm64 macOS | 10.4 MB |
| `libskia.a`，arm64 iOS | 21.0 MB |
| `libskia.a`，x86_64 Linux（GCC） | 21.9 MB |
| 安装目录（头文件、库、CMake 配置），每个三元组 | 54 MB，其中头文件约 20 MB |
| 顺带编出的模块库（skottie、sksg、svg 等，用不到） | 约 14 MB |
| 二进制缓存，x86_64 macOS 全部 9 个包 | 16 MB，其中 Skia 12.5 MB |
| 二进制缓存，三个 Apple 三元组合计 | 52 MB |
| 二进制缓存，x86_64 Linux 全部 12 个包 | 22 MB |
| vcpkg 下载的源码包 | 93 MB（Linux 上是 365 MB，多出来的是 vcpkg 自己下载的 CMake、ninja 等工具） |
| vcpkg 构建目录（可删） | 约 390 MB，其中 Skia 225 MB |
| spike 测试可执行文件（含 GoogleTest，链接后） | 4.0 MB，代码段 2.9 MB |

链接是静态的，链接器只留下用到的部分：8 个测试用到路径填充、描边、网格、裁剪所需的光栅管线，连同 GoogleTest 一共 2.9 MB 代码。

"关掉全部可选特性"指 port 的 `default-features: false`：没有 GL、Metal、Vulkan 后端，没有 jpeg、png、webp 编解码，没有 freetype、harfbuzz、icu。Skia 的 Ganesh（GPU 渲染的公共部分）仍然被编进了库里（导出的宏里有 `SK_GANESH`），只是没有任何 GPU 后端可用；port 没有提供关掉它的开关。iOS 的库大一倍，原因没有追查。

### 3.3 许可证

Skia 是 BSD-3-Clause。核心构建牵涉的第三方代码：port 的硬依赖 abseil（Apache-2.0）、expat（MIT）、zlib（zlib 许可证），以及 port 另行取来的 wuffs 和 piex（都是 Apache-2.0）。其中导出的链接行里只有 expat；abseil 会被编出来但核心构建不链接它。vcpkg 把 Skia 及其第三方的许可证文本装在 `share/skia/copyright`，发布时随包附带即可。都是宽松许可证，与本项目的 BSD-3-Clause 没有冲突。

### 3.4 链进本仓库

`toonz/sources/tests/skia_spike/CMakeLists.txt` 用 `find_package(unofficial-skia CONFIG REQUIRED)` 加 `-Dunofficial-skia_DIR=...` 找到 Skia，不使用 vcpkg 的工具链文件（那会把这次构建里所有其他的 `find_package` 也引到 vcpkg 去）。

测试可执行文件 `nexttoonz_skia_spike_tests` 链接 Skia、tnzcore 和 GoogleTest，8 个测试全部通过：

| 测试 | 断言的事实 |
|---|---|
| `FilledPathCoversItsInteriorOnly` | 带洞的填充路径：环内是纯色，洞和外部是透明 |
| `FillAntialiasingIsAreaCoverage` | 一条边切掉半个像素，该像素 alpha 是 128；关掉反走样则只有 0 和 255 |
| `StrokedPathCoversTheOutlineNotTheInterior` | 描边的开放线段（含圆头）与闭合圆：线上有色，内部透明 |
| `PiecesOfOnePathFillAsTheirUnion` | 同向的多块拼成一条路径，半透明填充无接缝、不重复混合；反向的一块会打出洞 |
| `AbuttingShapesLeaveASeamUnlessTheFirstIsGrown` | 相邻两个形状分别反走样会留缝（alpha 191）；先画的外扩半像素后无缝 |
| `MeshesInterpolateColoursButAreNotAntialiased` | 网格插值顶点颜色，但边缘不反走样 |
| `DrawsIntoTRaster32WithoutACopy` | 直接画进 `TRaster32P` 的内存：通道顺序一致、预乘一致、不需要翻转 |
| `RenderingIsRepeatable` | 同一场景画两次逐位相同；打印场景哈希供跨平台比较 |

中间三个不是在测"能不能链接"，而是在给画布 IR 草案（`doc/nexttoonz_canvas_ir.md`）取证。

### 3.5 macOS 与 Linux 的像素对比

最后一个测试画一个固定的小场景（带洞的反走样填充，上面压一条半透明的、含二次和三次曲线的圆角描边），并可以把像素导出来。把 x86_64 macOS（Skia 由 Apple clang 编）和 x86_64 Ubuntu 24.04（Skia 由 GCC 13 编）的结果逐像素比较：

| 目标像素格式 | 结果 |
|---|---|
| 各自平台上 Skia 的"本机"32 位格式 | 4,096 个像素**逐位相同**（对齐通道顺序之后） |
| 两边都指定 BGRA（`TPixel32` 的顺序） | 5 个像素的一个通道相差 1，都在半透明描边压过填充的地方 |

原因是 Skia 的本机 32 位格式在 macOS 上是 RGBA，在 Linux 上是 BGRA。BGRA 在 macOS 上不是本机格式；我的推测是混合因此走了另一条代码路径、舍入略有不同（没有读 Skia 的源码确认）。差 1 远在 golden 的容差之内（相差超过 2 才计数），但它说明"直接画进 `TRaster32P`"在 macOS 上走的不是 Skia 最常用的那条路径。如果以后想要两个平台逐位一致，可以试试构建 Skia 时定义 `SK_R32_SHIFT=16`（`SkTypes.h` 用它选择本机格式是 RGBA 还是 BGRA），让 BGRA 在 macOS 上也成为本机格式；这需要一个覆盖三元组，没有试。

场景哈希（m148，指定 BGRA）：Ubuntu 是 `22f238120169907e`，macOS 是 `5653731d3fe1721d`。arm64 没有跑过。

**补充（2026-11-09，Skia 后端做出来之后）。** 后端的测试里有一张用到全部画布操作的图（64x48，有渐变、图层、图像、逐顶点颜色的网格），比上面那个场景复杂得多。同样指定 BGRA：

| 比较 | 结果 |
|---|---|
| macOS 上 m144（预编译包）对 m148（vcpkg） | 逐位相同，哈希都是 `8be1d3f71a65e061` |
| macOS 对 iPad 模拟器（都是 m144 预编译包） | 逐位相同 |
| macOS 对 Ubuntu 24.04（都是 m148；Apple clang 对 GCC 13） | 3,072 个像素里 355 个不同，最大相差 3（满值 255）；Ubuntu 的哈希是 `f69648f7e30e5f0f` |

所以这两个版本的 Skia 之间没有差别，差别在平台之间，而且图一复杂就不止"5 个像素差 1"了。相差 3 仍然在"容差内一致"的范围里，但 golden 的像素阈值现在是"超过 2 才计数"，两个平台共用一份基线时这个阈值要重新定。这是以后的事：按现在的范围，Linux 不在第一批目标里。

### 3.6 原有测试

打开 `WITH_SKIA_SPIKE` 后，原有的 230 个测试里 229 个通过，唯一失败的是 `golden_plastic_basic`（本机显卡上的已知情况，与本次改动无关）。golden 场景按约定只完整跑了一遍，那时 spike 有 6 个测试；之后补了两个测试，又把 golden 以外的 214 个（原有 206 个加 spike 的 8 个）重跑一遍，全部通过。

---

## 4. CI 里怎样获取和缓存

**已实现（2026-11-16，分支 `ci/skia-in-ci`，未推送）。** 下面是实际做法；本节 0.3 版的设计（缓存 vcpkg 的二进制缓存目录）改成了缓存安装目录，理由见第 2 点。

脚本在 `ci-scripts/skia/`：

- `vcpkg.json`：清单（原来在 `tests/skia_spike/` 下），`builtin-baseline` 钉住 vcpkg 提交 `ec48c11e`，也就钉住了 Skia m148。升级 Skia 等于改这一行。
- `vcpkg_skia.sh <安装目录> [三元组]`：把 vcpkg 浅取到该提交、bootstrap、`vcpkg install`，三元组默认 `x64-linux-release`。
- `skia_pack.sh <arm64|x64> <目录>`：下载 skia-pack m144 的 macOS 包，按钉住的 SHA-256 校验（两个值取自 GitHub 为 release 资产记录的摘要），只解出头文件和 `libskia.a`。

各 job：

| Job | Skia 从哪来 | 缓存什么、键是什么 |
|---|---|---|
| Ubuntu 22.04，gcc 与 clang | vcpkg 从源码构建，**两个 job 都用 GCC 编**（clang 也链接 libstdc++，ABI 相同），共用一份 | 安装目录（81 MB，未压缩）。键：镜像（`$ImageOS`）、三元组、GCC 版本、两个脚本文件的哈希 |
| ASan/UBSan（Ubuntu 24.04） | 同上，GCC 13 编，Skia 本身不带清洁剂 | 同上，镜像和 GCC 版本不同所以是另一份 |
| macOS arm64、x86_64 | skia-pack 预编译包，按 runner 的架构取 | 只缓存 zip（36 到 38 MB），每次都校验后再解包。键：架构和 `skia_pack.sh` 的哈希 |
| Fedora 容器 | 不用 Skia（`WITH_SKIA` 默认关闭） | 无 |

说明：

1. 配置时用 `-Dunofficial-skia_DIR=<安装目录>/<三元组>/share/unofficial-skia`，**不**把安装目录放进 `CMAKE_PREFIX_PATH`：安装目录里还有 vcpkg 的静态 zlib 和 expat，放进去的话其余的 `find_package()` 会优先找到它们而不是系统的。
2. 缓存安装目录而不是 vcpkg 的二进制缓存：命中时连 vcpkg 的克隆和 bootstrap 都省了（二进制缓存仍然要先克隆、bootstrap、下载 vcpkg 自己用的 CMake）。代价是失去 vcpkg 的 ABI 哈希，所以编译器版本写进了键里；runner 镜像换 GCC 时自动重编一次。
3. 缓存用 `actions/cache/restore` 加 `actions/cache/save`，构建完 Skia 立刻保存，后面的测试失败也不会丢掉它。两个 Ubuntu job 第一次会同时冷构建，后保存的那个会报"缓存已存在"的警告，不影响结果。
4. 未命中时的构建时间：虚拟机（14 核）里 GCC 11 冷构建 213 秒；runner 只有 4 个 vCPU，估计 8 到 12 分钟（**推算**）。
5. `actions/cache` 会清掉 7 天没被访问的条目。

推送之前在本机验证过的：

- Ubuntu 22.04 的两个编译器：在虚拟机里装 GCC 11 和 clang 14 冷构建 m148。GCC 11 成功；clang 14 失败（它配 24.04 的 libstdc++ 13 时编不过 `<chrono>` 的 C++20 部分，22.04 上的组合不同，没有验证），这是让两个 job 都用 GCC 编 Skia 的另一个理由。
- ASan/UBSan job：在虚拟机（Ubuntu 24.04，GCC 13 编 Skia，clang 18 编树）里按 workflow 的步骤从头跑一遍：`vcpkg_skia.sh` 新克隆 vcpkg 并冷构建 Skia 238 秒，`asan` 预设加 job 的构建目标 282 秒，`ctest` 386 个测试里只有 `golden_fx_sweep_font_a` 失败（虚拟机里的已知情况，基线要 runner 的字体），1 个按设计跳过，泄漏检测打开。
- macOS x86_64：用 `skia_pack.sh` 解出的目录重新配置，`canvas_skia` 57 个测试通过，场景哈希仍是 `00f0bc48d7b9d098`。

只能推送后才知道的：GitHub runner 上的构建时间；Ubuntu 22.04 上真实的 GCC 11 和 clang 14（树本身用 clang 14 编、链接 GCC 编的 Skia）；arm64 的 skia-pack（没有下载，SHA-256 来自 GitHub 的记录）和 arm64 上 golden 的像素；workflow 文件本身的语法（本机没有 actionlint）。

---

## 5. 走通过程中发现的问题

### 5.1 需要 Python 3.10 以上

vcpkg 的 Skia port 依赖 pkgconf，pkgconf 用 meson 构建，meson 要求 Python 3.10 以上。macOS 自带的 `/usr/bin/python3` 是 3.9.6，它在 PATH 里排在前面时构建失败（"Found Python version '3.9.6' … is insufficient for meson"）。把 Homebrew 的 Python 放到 PATH 前面即可。

### 5.2 导出的链接行在 macOS 上带着 OpenGL 和 AppKit

port 导出的 `unofficial::skia::skia` 目标在 macOS 上的链接依赖是 `-framework ApplicationServices -framework OpenGL -framework AppKit`，是从 Skia 的 GN 文件里原样搬来的。实际上库里**没有任何未定义的 GL 符号**（`nm -u libskia.a` 里没有 `gl*` 或 `CGL*`），但链接了这个目标的可执行文件会带上 OpenGL.framework 的加载命令。

Phase 1 的完成判据之一是核心库不再链接 GL。正式接入时不能直接用导出的目标，要自己包一层：只取 `libskia.a`、头文件目录和编译宏，链接依赖只写 CoreGraphics、CoreText、CoreFoundation 和 expat。这是十几行 CMake，没有做。

### 5.3 Skia 自己用 C++20 编译

m148 的公共头文件在本仓库的 C++17 下可以编译（spike 就是这样编的）。但 Skia 的 GN 文件给自己设的是 `-std=c++20`，它的头文件随时可能开始要求 C++20。应对办法就是画布 IR 草案里的安排：只有 Skia 后端那一个库包含 Skia 的头文件，到时只需把这个库的标准调高。

### 5.4 Skia 不带 RTTI

官方构建配置下 Skia 用 `-fno-rtti` 编译。普通构建里只调用它没有问题。两种情况会在链接时找不到它的 typeinfo：

- **UBSan 的 vptr 检查**（asan 预设里开着）。它给每个虚函数调用都加一次类型检查，需要被调用类的 typeinfo。实测：spike 在虚拟机的 asan 构建里第一次链接失败，报 `undefined reference to typeinfo for SkCanvas`。处理办法是给包含 Skia 头文件的目标加 `-fno-sanitize=vptr`，spike 的 CMake 里已经加了。Skia 后端库以后也要这样，这意味着后端库里对 Skia 对象的虚调用不受 vptr 检查保护，其余检查不受影响。
- 在开了 RTTI 的代码里**继承** Skia 的类。后端实现时不要继承 Skia 的类。

另一条路是让 Skia 带 RTTI 编译（skia-pack 在 macOS 上就加了 `-frtti`），这需要改 port 的参数，没有试。

### 5.5 部署目标没有设

`libskia.a` 的最低系统版本：macOS 两个架构是 11.0，iOS 是 26.5（等于 SDK 版本）。vcpkg 自带的三元组不设部署目标。发布构建需要自己的覆盖三元组（overlay triplet），里面写 `VCPKG_OSX_DEPLOYMENT_TARGET`，并与 OpenToonz 自己的部署目标一致。本仓库现在的 macOS 构建也没有显式设部署目标，这是同一个待办。

### 5.6 网格没有反走样，相邻形状有接缝

这两条是 Skia CPU 后端的行为，不是获取方式的问题，但对 Phase 1 的外观影响最大，详见画布 IR 草案的第 5.5 节和 Q1。

---

## 6. 没有验证的事项

- **arm64 上没有运行过。** arm64 macOS 和 arm64 iOS 只证明了能交叉编译出库，没有链接进任何程序，也没有运行。x86_64 与 arm64 的像素是否一致因此未知；测试会打印场景哈希并能导出像素，推到 CI 上跑一次 arm64 job 就能比较。
- **GitHub runner 上的构建时间**是按 CPU 时间推算的。
- **Ubuntu 22.04 加 GCC 11、Fedora 加当前 GCC** 没有试。Skia 官方只支持用 clang 构建，GCC 能过是 vcpkg port 的补丁和它在 x64-linux 上的持续集成在保证；GCC 11 比实测的 13 旧，Fedora 的比它新，两头都有可能遇到编译错误。真遇到的话，退路是给 vcpkg 指定用 clang 编 Skia（链接 libstdc++，ABI 不受影响）。
- **Windows** 完全没有碰。vcpkg 的 CI 覆盖它；等 x64 Windows 是否作为产品目标定下来再说。
- **iOS 上的字体、Metal 特性**（port 的 `metal` 特性声明支持 iOS 和 macOS）没有试，Phase 4 再验证。
- **需要文字时的依赖。** 核心构建在 macOS 和 iOS 上用 CoreText，Linux 上没有任何字体后端。Phase 1 的离线渲染不需要通过 Skia 画文字（画布 IR 草案第 3 节的清单里没有文字绘制）；Phase 3 的 viewer 需要时，Linux 要加 `freetype` 和 `fontconfig` 特性，构建时间和依赖都会增加。
- **链接进共享库。** spike 把静态的 `libskia.a` 链进一个可执行文件。正式的后端是一个共享库（`.dylib`、`.so`），Skia 的符号可见性和 Linux 上是否需要 `-fPIC`（vcpkg 在 Linux 上默认加）没有单独验证。
- **vcpkg 基线升级的代价**没有经验数据。Skia 不保证 API 稳定，m148 里 `SkPath` 的编辑方法和 `SkGradientShader` 都已经不在了。建议 Phase 1 期间钉住 m148 不动。

---

## 7. 预编译包（skia-pack）的验证

发布：`JetBrains/skia-pack` 的 `m144-e2e6623374-4`（2026-02-20）。每个 zip 里是 Skia 的头文件（`include/`、`modules/`、`src/` 下的 `.h`）和 `out/<配置>/` 下的静态库。

| 目标 | zip | 解包后 | `libskia.a` | 最低系统版本 | 验证 |
|---|---|---|---|---|---|
| macOS x86_64 | 38 MB | 126 MB | 23.2 MB | macOS 11.0 | 链进仓库，8 个测试通过，另外 206 个非 golden 测试通过 |
| iPadOS 模拟器 x86_64 | 32 MB | 114 MB | 21.7 MB | iOS 12.0 | 在 iPad 模拟器里运行，7 个测试通过 |
| iPadOS arm64（库里有 arm64 和 arm64e 两个切片） | 58 MB | 181 MB | 40.1 MB | 两个切片分别是 iOS 12.0 和 14.0 | 编译并链接成一个 arm64 可执行文件，没有运行 |

怎样做的：

- macOS：`tests/skia_spike/CMakeLists.txt` 新增缓存变量 `NEXTTOONZ_SKIA_PACK_DIR`，指向解包后的目录即可，不设时仍然找 vcpkg 的 Skia。两种来源包成同一个目标 `nexttoonz_skia`。
- iPadOS：`tests/skia_spike/run_ios.sh` 用 Xcode 的 clang 直接编译测试、GoogleTest 和链接，模拟器版用 `xcrun simctl spawn` 在模拟器里运行。从零编译到跑完约 15 秒。

实测到的事实：

- **只需要链接 `libskia.a` 一个库**，加 CoreFoundation、CoreGraphics、CoreText、Foundation、Metal（iPadOS 上还有 UIKit）这几个系统框架。包里另外二十多个库（icu、harfbuzz、png、jpeg、webp、skottie 等）用不到。
- **不链接 OpenGL，也不链接 OpenGL ES。** `libskia.a` 里没有未定义的 GL 符号，iPadOS 的可执行文件的依赖里没有 OpenGLES.framework。我在这次验证之前根据它的构建脚本推断"iPadOS 上会链接已废弃的 OpenGL ES"，这个推断是错的。
- **带 RTTI。** 库里有 `SkCanvas` 的 typeinfo，所以 5.4 节的 vptr 问题在这个包上不存在（没有在清洁剂构建里实际试）。
- **像素与 vcpkg 的 m148 相同。** 场景哈希在 macOS、iPad 模拟器上都是 `5653731d3fe1721d`，和 3.5 节里 vcpkg 版在 macOS 上的值一样。
- **API 没有差别到要改测试。** 同一份测试源码对 m144 和 m148 都能编译。
- 链接后的测试可执行文件：macOS 4.1 MB，iPad 模拟器 4.0 MB，iPadOS arm64 3.3 MB。

需要知道的代价和没有验证的地方：

- **包里没有"编译时用了哪些宏"的清单。** vcpkg 的 CMake 配置会导出一串 `SK_*` 宏，这个包没有。现在按它的构建脚本定义了 `SK_GANESH` 和 `SK_METAL`，另外显式定义 `SK_RELEASE`（库是 Release 构建；不定义的话，在没有 `NDEBUG` 的构建里 Skia 的头文件会切到调试布局）。测试通过说明用到的这部分接口没有问题，但这不是证明：以后用到新的 Skia 接口时，头文件与库不一致的风险要记着。
- **它是别人的产品的构建产物。** 版本（现在是 m144）、发布时间、打开哪些特性都由 JetBrains 的 Skiko 决定。它上一次发布是 2026-02-20，到现在八个多月没有更新。
- **没有 x86_64 以外的 macOS 验证**，arm64 macOS 的包存在但没有下载。
- **没有在真机上运行。** 真机需要应用包和代码签名；arm64 上的像素是否与 x86_64 一致仍然未知。
- **模拟器运行时是 iOS 15.5**（本机只装了这一个），不是当前的 iPadOS 26。
- 下载的 zip 没有放进仓库；正式使用时需要一个按固定地址下载并校验 SHA-256 的步骤，还没有写。三个 zip 的 SHA-256：
  - macOS x86_64：`077513011356f9d96c596def3c5b292e43c705dc6902812daa82bc147303a9f4`
  - iPadOS 模拟器 x86_64：`fe4a092491720c1c8332c0e96aaec7c12174ed191fb32341a4d0926d325b045b`
  - iPadOS arm64：`d8f7140066f0637c292b31c1ada8bfec30ff6e229f41e527c9690708b6b8a173`
