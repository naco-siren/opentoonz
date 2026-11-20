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
4. with ``--update`` copies the renders over the goldens instead;
5. with ``--json`` writes the outcome as one JSON document (schema
   ``nexttoonz.golden-scene/1``, tools/nt/schemas/golden-scene.schema.json):
   per frame the pixel hashes of the render and of the golden, the metrics
   behind PASS/FAIL, and the diff image if one was written. It is written
   whatever the outcome, also when tcomposer fails.

Frame file names follow tcomposer's convention ``<name>.<frame 4 digits>.png``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_images  # noqa: E402


def run(cmd: list[str], env: dict[str, str], cwd: Path) -> int:
    print("+", " ".join(cmd), flush=True)
    proc = subprocess.run(cmd, env=env, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if proc.stdout:
        print(proc.stdout, flush=True)
    return proc.returncode


class Result:
    """The JSON outcome of one scene (--json); a no-op without a path."""

    def __init__(self, path: Path | None, scene: str, args: argparse.Namespace):
        self.path = path
        self.start = time.monotonic()
        self.doc = {
            "schema": "nexttoonz.golden-scene/1",
            "scene": scene,
            "host": {"os": platform.system().lower(), "arch": platform.machine()},
            "env": {k: os.environ[k] for k in ("NEXTTOONZ_VECTOR_RASTERIZER",) if k in os.environ},
            "tolerance": {"max_channel_diff": args.max_channel_diff, "max_diff_fraction": args.max_diff_fraction},
            "expected_dir": str(args.expected),
            "work_dir": str(args.work.resolve()),
            "status": "error",
            "frames": [],
        }

    def finish(self, code: int, status: str, **fields) -> int:
        self.doc.update(fields)
        self.doc["status"] = status
        self.doc["exit_code"] = code
        self.doc["seconds"] = round(time.monotonic() - self.start, 2)
        if self.path is not None:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            self.path.write_text(json.dumps(self.doc, indent=1) + "\n")
        return code


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
    parser.add_argument("--json", type=Path, default=None, help="write the outcome as JSON here")
    args = parser.parse_args(argv)

    scene_name = Path(args.scene).stem
    result = Result(args.json, scene_name, args)
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
        return result.finish(2, "error", error=f"scene not found: {args.scene}")

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
        tail = []
        if log.is_file():
            text = log.read_text(errors="replace")
            print(text)
            tail = text.splitlines()[-40:]
        return result.finish(1, "error", error=f"tcomposer exited with {rc}", tcomposer_rc=rc, log_tail=tail)

    frames = sorted(out_dir.glob(f"{scene_name}.*.png"))
    if not frames:
        print(f"run_golden: tcomposer produced no frames in {out_dir}", file=sys.stderr)
        return result.finish(1, "error", error="tcomposer produced no frames", tcomposer_rc=rc)

    if args.update:
        args.expected.mkdir(parents=True, exist_ok=True)
        for old in args.expected.glob(f"{scene_name}.*.png"):
            old.unlink()
        for f in frames:
            shutil.copy2(f, args.expected / f.name)
        print(f"run_golden: updated {len(frames)} golden frame(s) in {args.expected}")
        return result.finish(0, "updated", tcomposer_rc=rc, frames=[
            {"frame": int(f.name.split(".")[-2]), "file": f.name,
             "pixels": compare_images.pixel_hash(compare_images.load_rgba(f))} for f in frames])

    failures = 0
    records = []
    expected_frames = sorted(args.expected.glob(f"{scene_name}.*.png"))
    frame_set_matches = [f.name for f in expected_frames] == [f.name for f in frames]
    if not frame_set_matches:
        print(
            "run_golden: frame set differs. expected "
            f"{[f.name for f in expected_frames]}, got {[f.name for f in frames]}",
            file=sys.stderr,
        )
        failures += 1

    for f in frames:
        record = {"frame": int(f.name.split(".")[-2]), "file": f"out/{f.name}",
                  "png": "sha256:" + hashlib.sha256(f.read_bytes()).hexdigest()}
        records.append(record)
        golden = args.expected / f.name
        if not golden.is_file():
            print(f"FAIL {f.name}: no golden image (run with --update to create it)")
            record.update(passed=False, reason="no golden image",
                          pixels=compare_images.pixel_hash(compare_images.load_rgba(f)))
            failures += 1
            continue
        metrics: dict = {}
        diff = work / "diff" / f.name
        passed, summary = compare_images.compare(
            golden, f, args.max_channel_diff, args.max_diff_fraction, diff, metrics
        )
        print(f"{'PASS' if passed else 'FAIL'} {f.name}: {summary}")
        record.update(metrics)
        record["passed"] = passed
        if diff.is_file():
            record["diff"] = f"diff/{f.name}"
        if not passed:
            failures += 1
    missing = sorted({f.name for f in expected_frames} - {f.name for f in frames})

    common = dict(tcomposer_rc=rc, frames=records, frame_set_matches=frame_set_matches,
                  missing_frames=missing, failures=failures)
    if failures:
        print(f"run_golden: {failures} failure(s); renders and diffs are in {work}", file=sys.stderr)
        return result.finish(1, "fail", **common)
    return result.finish(0, "pass", **common)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
