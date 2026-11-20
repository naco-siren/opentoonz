# doc/review：审阅材料

所有者据以做决定的对照图，跟着提交走，在 GitHub 和 iPad 上直接能看（`doc/nexttoonz_layout.md` 4.7 节，Q1 的决定 (a)）。

规矩：

- 位置：`doc/review/<主题>/<步>/`，每个主题一个 README（内容索引、总量和预算），每一步的 README 说明图怎么读、怎么重做。
- 只放所有者要看的成品：三联图、接触印样、放大局部。原始的逐帧输出放本机的 `local/runs/`，不进版本库。
- 单张不超过 300 KB；每个主题的总量写在它的 README 里。超出预算时转单独的仓库（Q1 的 (c)）。
- 普通 git，不走 LFS。`doc/**` 在 workflow 的 `paths-ignore` 里，加图不触发构建。
- 改了这里或任何文档后跑 `tools/docs/check_links.py`。

| 主题 | 内容 | 总量 |
|---|---|---|
| [canvas](canvas/README.md) | 画布 IR 迁移（Phase 1）第 2 到 9 步的 OpenGL 与 Skia 对照 | 见它的 README |
