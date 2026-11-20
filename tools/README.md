# tools/：开发者工具

不参与产品构建的脚本和小程序（`doc/nexttoonz_layout.md` 4.5 节）。规矩：进版本库的只有源码和脚本；输出写到本机根 `local/` 下（`local/runs/<工具>/<日期>_<说明>/`），编出来的东西写到本检出的 `build/`，脚本不往自己所在的目录写东西。`local/` 的位置：环境变量 `NEXTTOONZ_LOCAL`，没有就是主检出（`git rev-parse --git-common-dir` 的上一级）里的 `local/`，所以每个工作树用同一个。

| 工具 | 做什么 | 怎样调用 | 前置 |
|---|---|---|---|
| `setup/bootstrap.sh` | 建 `local/`，把 `local/cache` 补齐：venv（numpy、Pillow）、skia-pack（经 `ci-scripts/skia/skia_pack.sh`，校验 SHA-256）、钉住的 vcpkg 和它的二进制缓存，可选静态 OpenCV 4.10；可选在本检出里编自带的 libtiff；最后打印配置构建要用的 CMake 参数。已有的项不动，可以反复跑 | `tools/setup/bootstrap.sh [--opencv] [--tiff] [--check]`，`--help` 看全部 | Python 3.11 以上（默认挑 3.14），curl、git、cmake、ninja |
| `upstream/merge.sh` | 合并上游：`merge.renameLimit=0`、`-X find-renames=25%`，停在提交之前；按种类数冲突，列出“新文件放哪”（file location）的提示待确认；日志在 `local/runs/upstream-merge/` | `tools/upstream/merge.sh [--dry-run] [<ref>]`（默认先取 `upstream/master`） | 工作区干净（`--dry-run` 不要求） |
| `docs/check_links.py` | 检查 `doc/**/*.md` 里每个相对链接和图片路径都存在、并且不是被忽略的文件；跳过代码片段和代码块 | `tools/docs/check_links.py [--all] [文件…]`；改文档后跑 | Python 3 |
| `vm/` | 在 lima 虚拟机里复现 Linux 的 Sanitizers CI 作业：建虚拟机、装依赖并克隆、配置构建、同步重编、跑测试；`vm/records/` 是各步一次性的验收脚本 | 见 `vm/README.md` | lima（`limactl`） |
| `gltrace/` | 截获一个程序的 OpenGL 画线调用（点画、线宽、开关、begin/end 和每个顶点、颜色掩码）写成日志，用来对比两次构建画法的差别 | 编：`clang -dynamiclib -Wno-deprecated-declarations -framework OpenGL -o build/tools/libgltrace.dylib tools/gltrace/gltrace.c`；用：`DYLD_INSERT_LIBRARIES=build/tools/libgltrace.dylib GLTRACE=<日志> <程序>` | macOS |
| `setup-hooks.sh` | 装夹具与基线的保护钩子（每个克隆一次） | `tools/setup-hooks.sh` | |

`ab/`（改动前后对照）、`ci/`、`nt/` 由命令行与 CI 的工作入库（`doc/nexttoonz_cli_ci.md`），说明在它们各自的目录里。
