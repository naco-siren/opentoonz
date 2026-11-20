#!/usr/bin/env python3
"""Before/after renders of the whole reference project with two builds.

    ab_tcomposer.py render <tcomposer> [--out DIR] [--threads 1 8] [--rasterizer default|skia|gl]
                           [--scene NAME ...] [--skip PREFIX ...] [--label TEXT] [--json]
    ab_tcomposer.py compare <tree A> <tree B> [--out DIR] [--heatmaps DIR] [--json]

`render` renders every scene of the reference project with one tcomposer,
once per thread count, into <out>/<threads>/<scene>/, and writes
<out>/render.json (schema nexttoonz.render-set/1): per frame the hash of the
decoded, premultiplied pixels, the hash of the PNG file and the number of
non-blank pixels, plus the build and the effective vector rasteriser.
--rasterizer sets NEXTTOONZ_VECTOR_RASTERIZER for tcomposer (gl is the
OpenGL fallback; several threads on it are not deterministic, known issue
26, and such scenes are reported as "unstable", never judged). On macOS the
GLSL scenes (fx_sweep_gpu*) are skipped by default: they crash tcomposer
there (known issue 15).

`compare` compares two such trees on the pixel hashes (old trees without
render.json are read from their PNG files) and writes, with --out, one
nexttoonz.ab-scene/1 file per scene and thread count plus summary.json
(nexttoonz.diff-tree/1). It exits 0 when everything that is judged is
identical, 1 otherwise; the last line on standard error is the old tool's
"identical N, different M, missing in B K, only in B L".

Every tcomposer run has a timeout (--timeout, 300 s) and stays out of the
Dock. Inputs default to the repository's stuff/ and reference project;
--project and --stuff override them. Without --out the output goes to
local/runs/ab-tcomposer/<date>_<label>/ (doc/nexttoonz_layout.md, 4.8).
The same code is `nexttoonz render`/`nexttoonz diff tree` (tools/nt).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "nt"))
from ntpy import abtree, jsonout, paths, tcomposer  # noqa: E402


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("render")
    r.add_argument("tcomposer", type=Path)
    r.add_argument("--out", type=Path)
    r.add_argument("--threads", type=int, nargs="+", default=[1])
    r.add_argument("--rasterizer", choices=tcomposer.RASTERIZERS, default="default")
    r.add_argument("--scene", action="append", dest="scenes")
    r.add_argument("--skip", action="append", help="scene name prefix to leave out (default on macOS: fx_sweep_gpu)")
    r.add_argument("--label", default="render")
    r.add_argument("--timeout", type=float, default=300)
    r.add_argument("--xvfb", action="store_true", help="Linux: run tcomposer under xvfb-run (GL paths)")
    r.add_argument("--project", type=Path)
    r.add_argument("--stuff", type=Path)
    r.add_argument("--json", action="store_true", help="print the JSON document on standard output")
    c = sub.add_parser("compare")
    c.add_argument("a", type=Path)
    c.add_argument("b", type=Path)
    c.add_argument("--out", type=Path)
    c.add_argument("--heatmaps", type=Path)
    c.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.cmd == "render":
            out = args.out or paths.default_run_dir("ab-tcomposer", args.label)
            skip = tuple(args.skip) if args.skip is not None else abtree.default_skip()
            doc, rc = abtree.render_tree(args.tcomposer, out, args.threads, rasterizer=args.rasterizer,
                                         scenes=args.scenes, skip=skip, timeout=args.timeout, xvfb=args.xvfb,
                                         project=args.project, stuff=args.stuff, label=args.label)
            jsonout.log(f"renders in {out}; failed: {doc['result']['failed']}")
        else:
            for t in (args.a, args.b):
                if not t.is_dir():
                    raise jsonout.NtError(jsonout.INPUT, f"no such tree: {t}")
            doc, rc = abtree.compare_trees(args.a, args.b, args.out, args.heatmaps)
    except jsonout.NtError as e:
        jsonout.log(f"ab_tcomposer: {e}")
        return e.code
    if args.json:
        jsonout.emit(doc)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
