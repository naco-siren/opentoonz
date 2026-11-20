"""Overview images: every frame of a render tree, and side-by-side pairs.

`overview` lays out one row per scene (and thread count) with every frame
scaled down, labelled with the scene; it reads render trees
(<dir>/<threads>/<scene>/*.png) and golden work directories
(<dir>/<scene>/out/*.png) alike, and skips diff images. `pair` makes the
"X | Y | heatmap" strip of one frame for a review.
"""

from __future__ import annotations

from collections import OrderedDict
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from . import images

BACKGROUND = (60, 60, 60)
TEXT = (235, 235, 235)


def _rows(root: Path) -> "OrderedDict[str, list[Path]]":
    rows: "OrderedDict[str, list[Path]]" = OrderedDict()
    for png in sorted(root.rglob("*.png")):
        rel = png.relative_to(root)
        if "diff" in rel.parts[:-1] or images.frame_number(png) is None:
            continue
        parts = [p for p in rel.parts[:-1] if p != "out"]
        key = "/".join(parts) or png.name.split(".")[0]
        rows.setdefault(key, []).append(png)
    return rows


def overview(root: Path, out: Path, scale: float = 0.25, label_width: int = 150, title: str | None = None) -> dict:
    rows = _rows(Path(root))
    if not rows:
        raise FileNotFoundError(f"no frames under {root}")
    first = Image.open(next(iter(rows.values()))[0])
    tw, th = max(1, int(first.width * scale)), max(1, int(first.height * scale))
    pad = 3
    columns = max(len(v) for v in rows.values())
    top = 18 if title else 0
    sheet = Image.new("RGB", (label_width + columns * (tw + pad) + pad, top + len(rows) * (th + pad) + pad), BACKGROUND)
    draw = ImageDraw.Draw(sheet)
    if title:
        draw.text((pad, 3), title, fill=TEXT)
    frames = 0
    for r, (key, pngs) in enumerate(rows.items()):
        y = top + pad + r * (th + pad)
        draw.text((pad, y + max(0, th // 2 - 6)), key[-24:], fill=TEXT)
        for c, png in enumerate(pngs):
            thumb = images.to_display(images.load(png), checker=max(2, int(10 * scale))).resize((tw, th), Image.BILINEAR)
            sheet.paste(thumb, (label_width + pad + c * (tw + pad), y))
            frames += 1
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out, optimize=True)
    return {"file": str(out), "rows": len(rows), "frames": frames, "width": sheet.width, "height": sheet.height}


def pair(x: np.ndarray, y: np.ndarray, out: Path, caption: str, zoom: int = 2, max_channel_diff: int = 2) -> None:
    """X | Y | heatmap of one frame, each zoomed, with a caption line."""
    h, w = x.shape[:2]
    per_pixel = np.abs(x.astype(np.int16) - y.astype(np.int16)).max(axis=2)
    amplified = np.clip(per_pixel * 8, 0, 255).astype(np.uint8)
    heat = np.stack([amplified] * 3, axis=2)
    heat[per_pixel > max_channel_diff] = (255, 0, 0)
    tiles = [images.to_display(x), images.to_display(y), Image.fromarray(heat, "RGB")]
    pad, top = 6, 20
    sheet = Image.new("RGB", (pad + 3 * (w * zoom + pad), top + h * zoom + 16 + pad), BACKGROUND)
    draw = ImageDraw.Draw(sheet)
    draw.text((pad, 4), caption, fill=TEXT)
    for i, (tile, label) in enumerate(zip(tiles, ("X", "Y", "|X-Y| x8, red > %d" % max_channel_diff))):
        x0 = pad + i * (w * zoom + pad)
        sheet.paste(tile.resize((w * zoom, h * zoom), Image.NEAREST), (x0, top))
        draw.text((x0, top + h * zoom + 2), label, fill=TEXT)
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out, optimize=True)
