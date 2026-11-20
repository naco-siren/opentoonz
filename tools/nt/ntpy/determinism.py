"""Determinism levels of doc/nexttoonz_cli_ci.md, section 3.4.

  exact     same platform and Skia version: byte for byte
  tolerant  across platforms only within the golden tolerance
  pinned    depends on the machine's state (fonts, rand(), GLSL):
            compare only renders made the same day on the same machine
  unstable  not deterministic (the OpenGL fallback with several threads):
            report, never judge

Until baselines carry a BASELINE.json (section 5.4) the level comes from the
scene name, with the same patterns as tests/golden/CMakeLists.txt.
"""

from __future__ import annotations

import re

PINNED = re.compile(r"^(fx_sweep_font_|fx_sweep_rand_|fx_sweep_gpu_|fx_sweep_gpuin_)")
TOLERANT = re.compile(r"^vector_styles$")

LEVELS = ("exact", "tolerant", "pinned", "unstable")


def level(scene: str, rasterizer: str | None = None, threads: int = 1) -> str:
    if rasterizer == "gl" and threads > 1:
        return "unstable"  # doc/nexttoonz_known_issues.md, item 26
    if PINNED.match(scene):
        return "pinned"
    if TOLERANT.match(scene):
        return "tolerant"
    return "exact"
