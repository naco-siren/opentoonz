# NextToonz 命令行契约

版本 1，2026-11-19。本文把 `doc/nexttoonz_cli_ci.md` 第 3 节写成契约：子命令、参数、JSON 文档、退出码。JSON 文档的结构以 `tools/nt/schemas/*.schema.json` 为准，本文只说它们的意思。路径相对于仓库根目录。

调用方（CI、Claude 会话、脚本）只依赖本文和那些 schema。实现可以换：今天是包着 tcomposer、tconverter 的 Python 前端 `tools/nt/nexttoonz`，引擎 API（Phase 2 第 11 步）之后是 C++ 的 `nexttoonz`（设计文档 6.2 节 C1），调用方不改。

---

## 1. 调用

```bash
tools/nt/nexttoonz <子命令> [参数] [--json]
python3 tools/nt <子命令> …          # 同上
```

- Python 3.10 以上，要 numpy 和 Pillow（golden 测试用的那个 Python 就有）。不要别的包。
- 要跑的程序用 `--program` 给，或者用环境变量 `NEXTTOONZ_TCOMPOSER`、`NEXTTOONZ_TCONVERTER`。
- 输入默认取仓库里的 `stuff/` 和参考工程，`--stuff`、`--project` 可以改。测试目录先找 `tests/`（布局文档第 7 步之后的位置），再找 `toonz/sources/tests/`。
- 不给 `--out`、`--bundle` 时，输出写到 `local/runs/<工具>/<日期>_<标签>/`（布局文档 4.8 节；`local/` 在主检出的根，经 git 的公共目录找到，所有工作树共用，`NEXTTOONZ_LOCAL` 可以改）。

## 2. 通用约定

**输出。** 带 `--json` 时，标准输出只有一个 JSON 文档；给人看的一律写标准错误。不带 `--json` 时标准输出只有一行摘要。

**信封。** 每个子命令输出的文档，顶层都是：

| 字段 | 意思 |
|---|---|
| `schema` | `nexttoonz.<种类>/<主版本>`，例如 `nexttoonz.render/1`；对应 `tools/nt/schemas/<种类>.schema.json` |
| `tool` | `name`（Python 前端是 `nexttoonz-py`，C++ 版是 `nexttoonz`）、`version`、`git`（工具所在检出的提交）、`build`（被调用的程序：路径、sha256、是否带 Skia、默认栅格化后端） |
| `host` | `os`、`arch` |
| `ok` | 完成且比较（如有）通过 |
| `result` | 子命令自己的结果 |
| `warnings` | 字符串列表 |
| `timing_ms` | 各阶段的毫秒数 |

子命令失败、来不及产生结果时（用法错、文件不存在），带 `--json` 也输出一个文档：`nexttoonz.error/1`，`result` 里有 `command`、`exit_code`、`message`。

结果包里的成员文件（`ab/<线程>/<场景>.json`、`golden/<场景>.json`、`gates.json`、`manifest.json`、`triage.json`）也带 `schema` 字段，但不套信封：工具、主机写在包的 `manifest.json` 里一次。

**退出码**（所有子命令一致；tcomposer 等旧程序保留各自的码）：

| 码 | 意思 |
|---|---|
| 0 | 完成，比较（如有）通过 |
| 1 | 完成，但比较或断言没通过（"不同"，不是"出错"） |
| 2 | 用法错误 |
| 3 | 输入有问题：文件不存在、场景加载失败、不属于工程 |
| 4 | 环境不满足：stuff 目录、要 GL 或窗口而没有；**Python 前端还没有的功能也是 4** |
| 5 | 内部错误：异常、部分帧没完成、被调用的程序崩溃 |
| 124 | 超时（`--timeout`，由子命令自己执行） |

**不等人。** 任何子命令都不弹对话框、不读标准输入。每次调用 tcomposer 都带超时、`QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM=1`（不进 Dock），在 stuff 与工程的临时副本上跑，绝不写仓库。

**版本。** 加可选字段不升版本；删、改名、改意思升主版本（`/2`），旧的读者按 `schema` 字段分辨。

## 3. 像素哈希与确定性

**像素哈希**（字段名 `pixels`，以及 `a`、`b`、`expected_pixels`）：

```
"sha256:" + sha256( "RGBA8p <宽> <高>\n" 的 ASCII 字节 + 预乘后的 RGBA，每通道 8 位，逐行 )
```

预乘与 golden 测试相同（`tests/golden/compare_images.py` 的 `load_rgba`：颜色乘 alpha/255 后四舍六入五取偶）。定义只有一处，就是那个文件里的 `pixel_hash()`。比较一律用它：PNG 编码器的变化（压缩级别、块）不算不同。文件本身的哈希另给（字段 `png`）。

**确定性级别**（设计文档 3.4 节），写在每帧和每个场景上：

| 级别 | 意思 | 同机 A/B |
|---|---|---|
| `exact` | 同平台、同 Skia 版本逐字节相同 | 逐字节 |
| `tolerant` | 跨平台只在容差内一致（`vector_styles`） | 逐字节 |
| `pinned` | 依赖机器状态：字体、`rand()`、GLSL（`fx_sweep_font_*`、`fx_sweep_rand_*`、`fx_sweep_gpu*`） | 逐字节，但两边要同一天同一台机器渲 |
| `unstable` | 本来就不确定：OpenGL 退路多线程（已知问题 26） | 只报告，不判 |

在基线目录有 `BASELINE.json`（设计文档 5.4 节）之前，级别由场景名和栅格化后端决定，规则在 `tools/nt/ntpy/determinism.py`，与 `tests/golden/CMakeLists.txt` 的"只在 Linux 比较"名单一致。

**比较档位**（`diff images --profile`）：`exact` 任何差都算；`golden` 与 golden 测试相同（超过 2 个色阶的像素不多于 0.5%）；`edge` 截图与跨机器的反走样边（16 个色阶、1%，暂定，G1 时校准）。

## 4. 子命令

"前端"一栏是今天的 Python 实现的状态。

| 子命令 | 参数 | 输出 schema | 前端 |
|---|---|---|---|
| `probe` | `[--program tcomposer] [--stuff 目录]` | `probe/1` | 部分：Python 与 numpy/Pillow 版本、CPU 数、stuff 的解析结果、`NEXTTOONZ_VECTOR_RASTERIZER` 等环境变量、C 库 `rand()` 在 `srand(1)` 后的前 8 个值、`xvfb-run`、被测程序是否带 Skia。没有的（Skia 版本与参考画面哈希、GL 与 Metal 设备、默认字体指纹、便携模式）列在 `not_available` |
| `render` | `<场景名或 <工程>/scenes/<名>.tnz> [--frames N 或 A-B] [--out 目录] [--threads N] [--rasterizer default\|skia\|gl] [--hash-only] [--format png] [--camera WxH]` | `render/1` | 有。`--camera` 和 PNG 以外的格式退出码 4 |
| `convert` | `<源> <目标> [--width] [--scene] [--frames]` | `convert/1` | 有（tconverter）。全部帧为空白时给警告；tconverter 被信号杀掉时退出码 5 |
| `cleanup` | `<场景> [--columns] [--overwrite all\|nopaint]` | | 退出码 4（已知问题 34；C3） |
| `inspect` | `<文件> <scene\|xsheet\|levels\|palette\|fx\|stage\|output\|dir> [--frame] [--lang]` | | 退出码 4（C4、C5） |
| `diff images` | `<a> <b> [--profile exact\|golden\|edge] [--grid 8] [--heatmap 输出.png]` | `diff-images/1` | 有。差在哪：按 8×8 格子列出超阈像素比例最高的 4 格（原点左上） |
| `diff tree` | `<树 A> <树 B> [--out 目录] [--heatmaps 目录]` | `diff-tree/1`；`--out` 下每个场景与线程数一个 `ab-scene/1` | 有（与 `tools/ab/ab_tcomposer.py compare` 同一份代码） |
| `diff scene`、`diff palette` | `<a> <b>` | | 退出码 4（C5） |
| `sheet` | `<目录> --out 输出.png [--scale 0.25] [--title]` | `sheet/1` | 有：一行一个场景（和线程数），每帧一个缩略图；读渲染树和 golden 工作目录 |
| `thumbs` | `<场景或目录> --out` | | 退出码 4（C8） |
| `run` | `<scenario.json> [--bundle 目录]` | | 退出码 4（C6、C7） |
| `test` | `--suite unit\|golden\|ab\|pr --program tcomposer [--base tcomposer 或渲染树] [--bundle 目录] [--threads 1 8] [--rasterizer] [--scene …] [--ctest 构建目录]` | `test/1`，并写结果包 | 有：`pr` = `unit`（给了 `--ctest` 才跑）+ `golden` + `ab` |
| `validate` | `<文件> …` | `validate/1` | 有：按文档的 `schema` 字段找到 schema 校验（设计里没有，前端附加） |

**渲染树**（`render` 一批场景、`test` 的 `ab`、`tools/ab/ab_tcomposer.py render` 的输出）：`<树>/<线程数>/<场景>/<场景>.<4 位帧号>.png`，加 `<树>/render.json`（`render-set/1`）。没有 `render.json` 的旧树（旧 `ab_tcomposer.py` 的输出）也能比较，哈希现算。

**`diff tree` 怎样判。** 只比两棵树都有的线程数；`unstable` 场景的不同帧单独计数、不算不同；一边渲染失败的场景算不同，两边都失败、被跳过的不算。有判为不同的帧或缺帧时退出码 1。macOS 上默认跳过 `fx_sweep_gpu*`（GLSL 特效让 tcomposer 崩溃，已知问题 15），记为 `skipped`。

## 5. 结果包

`test` 写的目录，与设计文档 4.3 节的布局相同（托管 runner 的 `results-<平台>-<sha>` 是它的子集：`junit/`、`golden/`、`sheets/`、`ccache.txt`）：

| 路径 | schema | 内容 |
|---|---|---|
| `manifest.json` | `manifest/1` | 套件、起止时间、工具、主机、被测与基准（程序路径、sha256、构建选项）、各阶段的状态 |
| `gates.json` | `gates/1` | 每道关卡一条：`pass`、`fail`、`known`（不同之处全在已知差异登记表内）、`skipped`，附阈值与证据路径 |
| `junit/ctest.xml` | JUnit | `ctest -LE golden --output-junit` |
| `golden/<场景>.json` | `golden-scene/1` | `run_golden.py --json`：逐帧的像素哈希、对基线的指标、是否通过 |
| `golden/<场景>/out/`、`diff/golden/<场景>/` | | golden 的渲染与差异图 |
| `renders/test/`、`renders/base/` | `render-set/1` | A/B 两边的渲染树 |
| `ab/<线程数>/<场景>.json`、`ab/summary.json` | `ab-scene/1`、`diff-tree/1` | A/B 的逐帧结果 |
| `diff/<场景>/<线程数>t_<帧>.png` | | 不同帧的热力图（灰：差放大 8 倍；红：超过 2 个色阶） |
| `sheets/overview.png` | | 被测构建全部帧的小图 |
| `logs/` | | 失败项最后 200 行 |
| `triage.json`、`triage.key`、`accept/` | `triage/1`、`accept/1` | AI 验收（`tools/ci/README.md`） |

## 6. 与设计文档不同或设计文档没定的

- **入口的路径**是 `tools/nt/nexttoonz`（或 `python3 tools/nt`），不是设计文档 6.1 节写的 `tools/nt`：按布局文档 4.5 节 `tools/nt/` 是目录。
- **已知差异登记表**在 `toonz/sources/tests/known_differences.yaml`：设计文档的 `tests/known_differences.yaml` 按它的约定省略了 `toonz/sources/` 前缀；布局文档第 7 步把测试目录整体上移到 `tests/` 时它跟着走，工具两处都找。
- **`validate`** 是前端附加的子命令，给 CI 和 `check_verdict.py` 用。
- **`render` 只渲 `<工程>/scenes/` 下的场景**（工程要有 `*_otprj.xml`），与 tcomposer 的要求一致。
