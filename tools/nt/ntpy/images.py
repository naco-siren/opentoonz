"""Frames: loading, hashing, and measuring how two of them differ.

Pixels are always compared premultiplied by alpha, as the golden tests do
(tests/golden/compare_images.py, which this module reuses so that there is
one definition of the premultiplication, the pixel hash and the golden
tolerance).
"""

from __future__ import annotations

import hashlib
import re
from pathlib import Path

import numpy as np
from PIL import Image

from . import paths

_ci = paths.import_compare_images()
load = _ci.load_rgba
pixel_hash = _ci.pixel_hash

# Comparison profiles of `diff images` (doc/nexttoonz_cli.md):
#   exact   any difference counts
#   golden  the golden tests' tolerance
#   edge    screenshots and antialiased edges across machines (provisional)
PROFILES = {
    "exact": {"max_channel_diff": 0, "max_diff_fraction": 0.0},
    "golden": {"max_channel_diff": _ci.DEFAULT_MAX_CHANNEL_DIFF, "max_diff_fraction": _ci.DEFAULT_MAX_DIFF_FRACTION},
    "edge": {"max_channel_diff": 16, "max_diff_fraction": 0.01},
}

FRAME_RE = re.compile(r"^(?P<name>.+)\.(?P<frame>\d{4})\.png$")


def frame_number(path: Path) -> int | None:
    m = FRAME_RE.match(path.name)
    return int(m.group("frame")) if m else None


def file_hash(path: Path) -> str:
    return "sha256:" + hashlib.sha256(Path(path).read_bytes()).hexdigest()


def nonblank(pixels: np.ndarray, tolerance: int = 2) -> int:
    """Pixels with any channel above `tolerance` (premultiplied)."""
    return int((pixels.max(axis=2) > tolerance).sum())


def frame_record(path: Path) -> dict:
    """What a render result says about one frame (schema nexttoonz.render/1)."""
    pixels = load(path)
    return {
        "frame": frame_number(path),
        "file": path.name,
        "width": int(pixels.shape[1]),
        "height": int(pixels.shape[0]),
        "pixels": pixel_hash(pixels),
        "png": file_hash(path),
        "nonblank": nonblank(pixels),
    }


def diff_metrics(a: np.ndarray, b: np.ndarray, max_channel_diff: int = 2, grid: int = 8, top: int = 4) -> dict:
    """How two premultiplied frames differ: counts, peak, where.

    `grid` splits the frame into grid x grid cells; the `top` cells with
    the largest share of pixels over the tolerance are listed, so a report
    can say "the lower-left of frame 3" without an image.
    """
    if a.shape != b.shape:
        return {"same_size": False, "a_size": [int(a.shape[1]), int(a.shape[0])],
                "b_size": [int(b.shape[1]), int(b.shape[0])]}
    per_pixel = np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=2)
    differing = per_pixel > 0
    over = per_pixel > max_channel_diff
    total = per_pixel.size
    out = {
        "same_size": True,
        "pixels_differing": int(differing.sum()),
        "fraction_differing": round(float(differing.mean()), 6) if total else 0.0,
        "max_channel_diff": max_channel_diff,
        "pixels_over_tolerance": int(over.sum()),
        "fraction_over_tolerance": round(float(over.mean()), 6) if total else 0.0,
        "peak": int(per_pixel.max()) if total else 0,
    }
    if differing.any():
        ys, xs = np.nonzero(differing)
        out["bbox"] = [int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1]
        h, w = per_pixel.shape
        # Where the differences over the tolerance are; where any are when
        # none is over it.
        mask = over if over.any() else differing
        cells = []
        for gy in range(grid):
            for gx in range(grid):
                y0, y1 = gy * h // grid, (gy + 1) * h // grid
                x0, x1 = gx * w // grid, (gx + 1) * w // grid
                cell = mask[y0:y1, x0:x1]
                if cell.size and cell.any():
                    cells.append({"cell": [gx, gy], "fraction": round(float(cell.mean()), 4)})
        cells.sort(key=lambda c: (-c["fraction"], c["cell"][1], c["cell"][0]))
        out["grid"] = {"size": grid, "origin": "top-left", "of": "over-tolerance" if over.any() else "differing",
                       "top": cells[:top]}
    return out


def passes(metrics: dict, profile: str) -> bool:
    p = PROFILES[profile]
    if not metrics.get("same_size", False):
        return False
    if p["max_channel_diff"] == 0:
        return metrics["pixels_differing"] == 0
    return metrics["fraction_over_tolerance"] <= p["max_diff_fraction"]


def heatmap(a: np.ndarray, b: np.ndarray, out: Path, max_channel_diff: int = 2) -> None:
    """Grey: differences amplified 8x; red: over the tolerance."""
    per_pixel = np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=2)
    amplified = np.clip(per_pixel * 8, 0, 255).astype(np.uint8)
    vis = np.stack([amplified] * 3, axis=2)
    over = per_pixel > max_channel_diff
    vis[over] = (255, 0, 0)
    out.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(vis).save(out)


def to_display(pixels: np.ndarray, checker: int = 10) -> Image.Image:
    """A premultiplied frame over a light checkerboard, as RGB, for sheets."""
    h, w = pixels.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    bg = np.where(((yy // checker + xx // checker) % 2)[..., None] == 1, 215, 255).astype(np.float64)
    alpha = pixels[..., 3:4].astype(np.float64) / 255.0
    rgb = pixels[..., :3].astype(np.float64) + bg * (1.0 - alpha)
    return Image.fromarray(np.clip(np.rint(rgb), 0, 255).astype(np.uint8), "RGB")
