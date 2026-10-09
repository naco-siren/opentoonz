#!/usr/bin/env python3
"""Compare a rendered image against a golden image with a tolerance.

Exact per-pixel equality is not a useful criterion for OpenToonz renders:
vector levels are rasterised through OpenGL today, so antialiasing differs
between platforms and drivers, and the planned Skia CPU rasteriser will
change it again. Instead a frame passes when the fraction of pixels whose
largest channel difference exceeds ``--max-channel-diff`` is at most
``--max-diff-fraction``.

Usage:
    compare_images.py EXPECTED ACTUAL [--diff OUT.png]
                      [--max-channel-diff N] [--max-diff-fraction F]

Exit status is 0 on pass, 1 on fail, 2 on usage or I/O error.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

DEFAULT_MAX_CHANNEL_DIFF = 2
DEFAULT_MAX_DIFF_FRACTION = 0.001


def load_rgba(path: Path) -> np.ndarray:
    with Image.open(path) as im:
        return np.asarray(im.convert("RGBA"), dtype=np.int16)


def compare(
    expected: Path,
    actual: Path,
    max_channel_diff: int = DEFAULT_MAX_CHANNEL_DIFF,
    max_diff_fraction: float = DEFAULT_MAX_DIFF_FRACTION,
    diff_out: Path | None = None,
) -> tuple[bool, str]:
    """Return (passed, human readable summary)."""
    e = load_rgba(expected)
    a = load_rgba(actual)
    if e.shape != a.shape:
        return False, f"size mismatch: expected {e.shape[1]}x{e.shape[0]}, actual {a.shape[1]}x{a.shape[0]}"

    per_pixel = np.abs(e - a).max(axis=2)
    over = per_pixel > max_channel_diff
    fraction = float(over.mean()) if over.size else 0.0
    peak = int(per_pixel.max()) if per_pixel.size else 0
    passed = fraction <= max_diff_fraction

    if diff_out is not None and not passed:
        diff_out.parent.mkdir(parents=True, exist_ok=True)
        # Amplify small differences so they are visible; mark pixels over the
        # tolerance in red.
        vis = np.zeros(e.shape[:2] + (3,), dtype=np.uint8)
        amplified = np.clip(per_pixel * 8, 0, 255).astype(np.uint8)
        vis[..., 0] = amplified
        vis[..., 1] = amplified
        vis[..., 2] = amplified
        vis[over, 0] = 255
        vis[over, 1] = 0
        vis[over, 2] = 0
        Image.fromarray(vis).save(diff_out)

    summary = (
        f"peak channel diff {peak}, {fraction * 100:.4f}% of pixels over "
        f"tolerance {max_channel_diff} (limit {max_diff_fraction * 100:.4f}%)"
    )
    return passed, summary


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("expected", type=Path)
    parser.add_argument("actual", type=Path)
    parser.add_argument("--diff", type=Path, default=None, help="write a diff visualisation here on failure")
    parser.add_argument("--max-channel-diff", type=int, default=DEFAULT_MAX_CHANNEL_DIFF)
    parser.add_argument("--max-diff-fraction", type=float, default=DEFAULT_MAX_DIFF_FRACTION)
    args = parser.parse_args(argv)

    for p in (args.expected, args.actual):
        if not p.is_file():
            print(f"compare_images: missing file {p}", file=sys.stderr)
            return 2

    passed, summary = compare(args.expected, args.actual, args.max_channel_diff, args.max_diff_fraction, args.diff)
    print(f"{'PASS' if passed else 'FAIL'} {args.actual.name}: {summary}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
