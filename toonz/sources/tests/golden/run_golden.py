#!/usr/bin/env python3
"""Render one reference scene with tcomposer and compare it with its goldens.

The script is what ctest runs for every ``golden_<scene>`` test. It

1. copies the ``stuff`` directory and the fixture project into a scratch
   directory, so tcomposer never writes into the source tree;
2. runs tcomposer headless, single-threaded, with the copied stuff dir as
   TOONZROOT (under ``xvfb-run`` when ``--xvfb`` is given, which the GL
   paths need until Phase 1 removes them);
3. compares every rendered frame with ``<expected>/<frame>.png`` using
   compare_images.py and writes diff images next to the renders;
4. with ``--update`` copies the renders over the goldens instead.

Frame file names follow tcomposer's convention ``<name>.<frame 4 digits>.png``.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_images  # noqa: E402


def run(cmd: list[str], env: dict[str, str], cwd: Path) -> int:
    print("+", " ".join(cmd), flush=True)
    proc = subprocess.run(cmd, env=env, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if proc.stdout:
        print(proc.stdout, flush=True)
    return proc.returncode


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tcomposer", required=True, type=Path)
    parser.add_argument("--stuff", required=True, type=Path, help="the repository's stuff/ directory")
    parser.add_argument("--project", required=True, type=Path, help="fixture project directory")
    parser.add_argument("--scene", required=True, help="scene path relative to the project directory")
    parser.add_argument("--expected", required=True, type=Path, help="directory with golden frames")
    parser.add_argument("--work", required=True, type=Path, help="scratch directory (inside the build tree)")
    parser.add_argument("--range", nargs=2, type=int, metavar=("FROM", "TO"), default=None)
    parser.add_argument("--xvfb", action="store_true", help="run tcomposer under xvfb-run with Mesa software GL")
    parser.add_argument("--update", action="store_true", help="overwrite the goldens with this render")
    parser.add_argument("--max-channel-diff", type=int, default=compare_images.DEFAULT_MAX_CHANNEL_DIFF)
    parser.add_argument("--max-diff-fraction", type=float, default=compare_images.DEFAULT_MAX_DIFF_FRACTION)
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args(argv)

    scene_name = Path(args.scene).stem
    work = args.work.resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    stuff = work / "stuff"
    project = work / "project"
    out_dir = work / "out"
    shutil.copytree(args.stuff, stuff, symlinks=True)
    shutil.copytree(args.project, project, symlinks=True)
    out_dir.mkdir()
    # The projects root must exist, and the fixture project must be inside it
    # for TProjectManager to accept the scene as belonging to a project.
    (stuff / "projects").mkdir(exist_ok=True)

    scene_path = project / args.scene
    if not scene_path.is_file():
        print(f"run_golden: scene not found: {scene_path}", file=sys.stderr)
        return 2

    cmd = [
        str(args.tcomposer.resolve()),
        str(scene_path),
        "-o", str(out_dir / f"{scene_name}.png"),
        "-nthreads", "1",
        "-TOONZROOT", str(stuff),
    ]
    if args.range:
        cmd += ["-range", str(args.range[0]), str(args.range[1])]

    env = dict(os.environ)
    if sys.platform.startswith("linux"):
        # Headless Linux: the offscreen plugin is enough for raster-only
        # scenes; GL scenes additionally need --xvfb below. On macOS the
        # default cocoa platform is kept because its offscreen plugin has no
        # OpenGL support and GitHub's macOS runners do have a window server.
        env.setdefault("QT_QPA_PLATFORM", "offscreen")
    if args.xvfb:
        # xcb on a virtual X server, with Mesa's software rasteriser for GL.
        env["QT_QPA_PLATFORM"] = "xcb"
        env["LIBGL_ALWAYS_SOFTWARE"] = "1"
        env.setdefault("GALLIUM_DRIVER", "llvmpipe")
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24", *cmd]

    rc = run(cmd, env, work)
    if rc != 0:
        print(f"run_golden: tcomposer exited with {rc}", file=sys.stderr)
        log = stuff / "toonzfarm" / "tcomposer.log"
        if log.is_file():
            print(log.read_text(errors="replace"))
        return 1

    frames = sorted(out_dir.glob(f"{scene_name}.*.png"))
    if not frames:
        print(f"run_golden: tcomposer produced no frames in {out_dir}", file=sys.stderr)
        return 1

    if args.update:
        args.expected.mkdir(parents=True, exist_ok=True)
        for old in args.expected.glob(f"{scene_name}.*.png"):
            old.unlink()
        for f in frames:
            shutil.copy2(f, args.expected / f.name)
        print(f"run_golden: updated {len(frames)} golden frame(s) in {args.expected}")
        return 0

    failures = 0
    expected_frames = sorted(args.expected.glob(f"{scene_name}.*.png"))
    if [f.name for f in expected_frames] != [f.name for f in frames]:
        print(
            "run_golden: frame set differs. expected "
            f"{[f.name for f in expected_frames]}, got {[f.name for f in frames]}",
            file=sys.stderr,
        )
        failures += 1

    for f in frames:
        golden = args.expected / f.name
        if not golden.is_file():
            print(f"FAIL {f.name}: no golden image (run with --update to create it)")
            failures += 1
            continue
        passed, summary = compare_images.compare(
            golden, f, args.max_channel_diff, args.max_diff_fraction, work / "diff" / f.name
        )
        print(f"{'PASS' if passed else 'FAIL'} {f.name}: {summary}")
        if not passed:
            failures += 1

    if failures:
        print(f"run_golden: {failures} failure(s); renders and diffs are in {work}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
