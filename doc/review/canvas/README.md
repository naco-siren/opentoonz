# 画布 IR 迁移的对照图

`doc/nexttoonz_canvas_ir.md` 第 5、8 节各步的 OpenGL 与 Skia 对照图；所有者据此在 2026-11-16 认可了矢量外观的变化（`doc/nexttoonz_phase1_release_note.md`）。2026-11-19 从主检出的 `samples/review/` 原样搬进版本库（逐字节相同）。

| 步 | 内容 |
|---|---|
| [step2](step2/README.md) | 笔触线重对照（Q2）：OpenGL、Skia 真实宽度、Skia 外扩半像素，6 个场景加 3 张局部放大 |
| [step3](step3/README.md) | 五类样式（贴图、栅格图案、矢量图案、矢量笔刷、MyPaint） |
| [step4](step4/README.md) | colorfx 16 个区域样式，每个两组参数 |
| [step5](step5/README.md) | colorfx 25 个笔触样式；`vector_styles` 整个场景 7 帧 |
| [step6](step6/README.md) | 新场景 `vector_cmapped_fx`；第 6 步接上 Skia 的各函数 |
| [step7](step7/README.md) | Plastic；流动笔刷（有无 mipmap），附两个放大用的场景 `flow_a.tnz`、`flow_b.tnz` |
| [step8](step8/README.md) | 摄影表画法（场景图标、缩略图），六个场景 |
| [step9](step9/README.md) | 默认切到 Skia 后，golden 场景里每个被改变的场景差得最多的一帧 |

## 总量与预算

29 个文件，共 4.99 MB（2026-11-19）。这个主题已经结束（Phase 1 合并），预算就定为现在的量，不再加图。

单张超过 300 KB 的有 4 张，合计 3.39 MB；它们在单独的一个提交里，缩小还是保留由主会话和所有者定：

| 图 | 大小 |
|---|---|
| `step5/step5_stroke_styles.png` | 1,747,249 字节 |
| `step4/step4_region_styles.png` | 791,303 字节 |
| `step5/step5_vector_styles_scene.png` | 462,518 字节 |
| `step8/step8_camstand.png` | 384,744 字节 |

其余 25 个文件合计 1.61 MB，最大的是 `step3/step3_styles.png`（260,747 字节）。
