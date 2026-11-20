# tools/vm：Linux 虚拟机里的 Sanitizers 作业

在 Mac 上用 lima 虚拟机（Ubuntu 24.04，x86_64）复现 `workflow_sanitizers.yml`：LeakSanitizer 在 Apple clang 上不能用，推分支等 CI 又慢。主检出只读地挂进虚拟机，路径与宿主相同；虚拟机在自己的磁盘上有一份克隆（`origin` 就是那个挂载），所以先在宿主上提交到本地分支，再在虚拟机里取。

| 脚本 | 在哪跑 | 做什么 |
|---|---|---|
| `create.sh` | 宿主 | 建虚拟机（`NT_VM`，默认 `nt-asan`；`NT_CPUS`、`NT_MEMORY`、`NT_DISK`），挂载主检出和（存在时）`~/Workspace/opentoonz-wt` |
| `run.sh <脚本> [参数…]` | 宿主 | 在虚拟机里跑下面的某个脚本，传过去宿主上设的 `NT_*` 变量 |
| `provision.sh <分支>` | 虚拟机 | 装 CI 装的包，克隆（`NT_SRC`，默认挂载的主检出），检出分支，在克隆里编自带的 libtiff |
| `build.sh <分支>` | 虚拟机 | 用 `asan` 预设配置（clang、ccache、vcpkg 的 Skia：`NT_SKIA`，默认 `~/skia-vcpkg`，没有就用 `ci-scripts/skia/vcpkg_skia.sh` 编一份），编 CI 编的目标（`NT_TARGETS`） |
| `sync.sh <分支>` | 虚拟机 | 取分支、重编已配置的构建目录 |
| `test.sh [ASAN_OPTIONS [LSAN_OPTIONS或- [ctest 参数…]]]` | 虚拟机 | CI 的测试步：默认开泄漏检查、用版本库的 `lsan.supp`、`-j 7` |

`env.sh` 是它们共用的设置：`NT_CLONE`（虚拟机里的克隆，默认 `~/opentoonz`）、`NT_BUILD`（默认 `$NT_CLONE/toonz/build-asan`）、`NT_LOGS`（默认 `/tmp/nt-vm`）。用另一份克隆就不会动到别人正在用的那份：

```bash
NT_CLONE='~/nt-check' tools/vm/run.sh provision.sh my-branch
```

典型的一轮：

```bash
limactl start nt-asan
tools/vm/run.sh provision.sh my-branch   # 第一次
tools/vm/run.sh build.sh my-branch
tools/vm/run.sh test.sh
limactl stop nt-asan
```

已知：`golden_fx_sweep_font_a` 在虚拟机里不过（基线要 GitHub 运行器上的字体）；其余与 CI 一致。冷构建约 10 分钟，测试约 90 秒。把文件拷出来：`limactl copy nt-asan:<路径> <宿主路径>`。
