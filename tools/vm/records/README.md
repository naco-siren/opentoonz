# tools/vm/records：各步一次性的 VM 验收脚本

画布迁移（`doc/nexttoonz_canvas_ir.md`）和无 Qt 核心（`doc/nexttoonz_core_noqt.md`）各步在 Linux 虚拟机里证明“默认路径没有变”时用的脚本，原样入库（布局文档 Q6）：它们**不保证以后还能跑**，作用是让文档里的验收方法有出处。

入库时只做了一处机械替换：原来写死的主检出路径换成了 `${NT_MAIN:?…}`，也就是要先设 `NT_MAIN`（主检出在虚拟机里的挂载路径）才能跑。脚本里引用的其他位置都是当时的样子：对照程序在 `toonz/build-phase1/vm/` 和 `toonz/build-phase1/ab/`（布局第 2 步之后对照程序入库到 `tools/ab/`），虚拟机里的克隆在 `~/opentoonz`，构建目录 `toonz/build-asan`。`ab_*.cpp` 的 Linux 副本不在这里，随对照工具一起入库到 `tools/ab/`；`linsk7/` 是输出，没有入库。

快照时间 2026-11-19：Phase 2 会话此后在 `toonz/build-phase1/vm/` 新写的脚本，到下一个里程碑一并补进来。

| 脚本 | 用在 |
|---|---|
| `step1.sh`、`step1_final.sh`、`step2_after.sh` 到 `step8_after.sh`、`step9_after.sh`、`step9_golden.sh`、`recheck.sh`、`experiment.sh`、`experiment.py` | 画布迁移第 1 到 9 步（`nexttoonz_canvas_ir.md` 第 8 节） |
| `ab_linux.sh`、`ab2_linux.sh`、`ab2_compare.sh`、`offscreen_linux.sh`、`regions_linux.sh`、`strokes_linux.sh`、`camstand_linux.sh`、`camstand_step8.sh`、`scene_linux.sh`、`legacy_*.sh` | 画布迁移各步在 Linux 上的改动前后对照 |
| `toonzlib_main/` | `offscreen_linux.sh`、`camstand_linux.sh` 编对照程序时包含的测试 main 与环境（从 toonzlib 测试的那份改出来的） |
| `phase2_step1.sh`、`stuff_seed_6a.sh`、`step8d0_render8.sh`、`ci_fixes.sh`、`step9.sh` | 无 Qt 核心第 1、6a、8d-0 步，里程碑之后的 CI 修复，第 9 步（进行中） |
