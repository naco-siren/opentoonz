"""`nexttoonz`, the Python front end of the command-line contract.

The contract is doc/nexttoonz_cli.md: sub-commands, the JSON documents
(tools/nt/schemas/) and the exit codes. This implementation wraps the
programs that exist today (tcomposer, tconverter) and the comparison code
of tools/ab; the C++ `nexttoonz` built on the engine API replaces it later
without changing what callers see. Sub-commands of the contract that need
the engine API (cleanup, inspect, thumbs, run) exit with 4.

With --json the one JSON document goes to standard output and everything
else to standard error; without it a short summary is printed.
"""

from __future__ import annotations

import argparse
import ctypes
import os
import shutil
import sys
import tempfile
from pathlib import Path

from . import abtree, determinism, images, jsonout, paths, schema, sheets, suite, tcomposer

NEEDS_ENGINE = {
    "cleanup": "tcleanup aborts at start-up on macOS (known issue 34); C3 after the engine API",
    "inspect": "needs read-only views of the engine API (C4, C5)",
    "thumbs": "needs the engine API's renderFrame (C8)",
    "run": "scenario scripts need the engine API (C6, C7)",
}


def _program(arg: Path | None, env_name: str, name: str) -> Path:
    p = arg or (Path(os.environ[env_name]) if os.environ.get(env_name) else None)
    if p is None:
        raise jsonout.NtError(jsonout.USAGE, f"say which {name} to run: --program or ${env_name}")
    if not Path(p).is_file():
        raise jsonout.NtError(jsonout.INPUT, f"no such program: {p}")
    return Path(p).resolve()


def _frames(text: str | None) -> tuple[int, int] | None:
    if not text:
        return None
    try:
        a, _, b = text.partition("-")
        first, last = int(a), int(b or a)
    except ValueError:
        raise jsonout.NtError(jsonout.USAGE, f"--frames takes N or FROM-TO, not {text!r}")
    if first < 1 or last < first:
        raise jsonout.NtError(jsonout.USAGE, f"bad frame range {text!r}")
    return first, last


def _c_rand(n: int = 8) -> list[int] | None:
    try:
        libc = ctypes.CDLL(None)
        libc.srand(1)
        return [int(libc.rand()) for _ in range(n)]
    except (OSError, AttributeError):
        return None


def cmd_probe(args) -> tuple[dict, int]:
    import numpy
    import PIL

    stuff = Path(args.stuff) if args.stuff else paths.stuff_dir()
    result = {
        "front_end": "python",
        "python": {"version": sys.version.split()[0], "numpy": numpy.__version__, "pillow": PIL.__version__},
        "cpu_count": os.cpu_count(),
        "stuff": {"path": str(stuff), "exists": stuff.is_dir(), "from": "--stuff" if args.stuff else "repository"},
        "project": str(paths.fixture_project()),
        "local_root": str(paths.local_root()),
        "env": {k: os.environ[k] for k in ("NEXTTOONZ_VECTOR_RASTERIZER", "QT_QPA_PLATFORM", "TOONZROOT")
                if k in os.environ},
        "c_rand_seed1": _c_rand(),
        "xvfb_run": shutil.which("xvfb-run"),
        "not_available": ["skia version and reference picture hash", "GL and Metal devices",
                          "default font fingerprint", "portable mode", "git commit of the build"],
    }
    build = None
    if args.program:
        build = tcomposer.build_info(_program(args.program, "NEXTTOONZ_TCOMPOSER", "tcomposer"))
    ok = result["stuff"]["exists"]
    return jsonout.envelope("nexttoonz.probe/1", ok, result, build=build,
                            warnings=[] if ok else [f"no stuff directory at {stuff}"]), \
        (jsonout.OK if ok else jsonout.ENVIRONMENT)


def _resolve_scene(arg: str, project: Path | None) -> tuple[Path, str]:
    if arg.endswith(".tnz"):
        p = Path(arg).resolve()
        if not p.is_file():
            raise jsonout.NtError(jsonout.INPUT, f"no such scene: {arg}")
        proj = Path(project) if project else p.parent.parent
        if not any(proj.glob("*_otprj.xml")):
            raise jsonout.NtError(jsonout.INPUT, f"{p} is not in a project (no *_otprj.xml in {proj})")
        if p.parent != proj / "scenes":
            raise jsonout.NtError(jsonout.INPUT, f"the front end renders scenes from <project>/scenes/ only: {p}")
        return proj, p.stem
    proj = Path(project) if project else paths.fixture_project()
    if not (proj / "scenes" / f"{arg}.tnz").is_file():
        raise jsonout.NtError(jsonout.INPUT, f"no scene {arg!r} in {proj}/scenes")
    return proj, arg


def cmd_render(args) -> tuple[dict, int]:
    if args.camera:
        raise jsonout.NtError(jsonout.ENVIRONMENT, "--camera needs the engine API (not in the Python front end)")
    if args.format != "png":
        raise jsonout.NtError(jsonout.ENVIRONMENT, "the Python front end renders PNG only")
    program = _program(args.program, "NEXTTOONZ_TCOMPOSER", "tcomposer")
    project, scene = _resolve_scene(args.scene, args.project)
    stuff = Path(args.stuff) if args.stuff else paths.stuff_dir()
    if not stuff.is_dir():
        raise jsonout.NtError(jsonout.ENVIRONMENT, f"no stuff directory at {stuff}")
    timer = jsonout.Timer()
    build = tcomposer.build_info(program)
    effective = tcomposer.effective_rasterizer(args.rasterizer, build)
    tmp = None
    if args.hash_only or not args.out:
        tmp = Path(tempfile.mkdtemp(prefix="nt_render_"))
        out = tmp
    else:
        out = Path(args.out)
    try:
        with tcomposer.Workspace(stuff, project) as ws:
            timer.mark("init")
            rec = tcomposer.render_scene(program, ws, scene, out, threads=args.threads, rasterizer=args.rasterizer,
                                         frames=_frames(args.frames), timeout=args.timeout, xvfb=args.xvfb)
            timer.mark("render")
    finally:
        if tmp is not None:
            shutil.rmtree(tmp, ignore_errors=True)
    level = determinism.level(scene, effective, args.threads)
    for f in rec["frames"]:
        f["determinism"] = level
        if tmp is not None:
            f.pop("file", None)
    result = {"scene": scene, "project": str(project), "threads": args.threads,
              "rasterizer": {"requested": args.rasterizer, "effective": effective}, "determinism": level,
              "out": None if tmp is not None else str(out), **{k: rec[k] for k in ("rc", "seconds", "frames")}}
    if rec.get("timed_out"):
        result["timed_out"] = True
        code = jsonout.TIMEOUT
    elif rec["ok"]:
        code = jsonout.OK
    elif rec["rc"] in (-2, 254):
        code = jsonout.INPUT  # tcomposer: project or scene failed to load
    else:
        code = jsonout.INTERNAL
    if code != jsonout.OK:
        result["log_tail"] = rec.get("log_tail", [])
    return jsonout.envelope("nexttoonz.render/1", code == jsonout.OK, result, build=build, timing=timer), code


def cmd_convert(args) -> tuple[dict, int]:
    program = _program(args.program, "NEXTTOONZ_TCONVERTER", "tconverter")
    src, dst = Path(args.src).resolve(), Path(args.dst).resolve()
    if not src.exists():
        raise jsonout.NtError(jsonout.INPUT, f"no such source: {src}")
    if not dst.suffix:
        raise jsonout.NtError(jsonout.USAGE, "the destination needs an extension (tconverter would take it as a format)")
    dst.parent.mkdir(parents=True, exist_ok=True)
    before = set(dst.parent.iterdir())
    cmd = [str(program), str(src), str(dst)]
    if args.width:
        cmd += ["-w", str(args.width)]
    if args.scene:
        cmd += ["-s", str(Path(args.scene).resolve())]
    fr = _frames(args.frames)
    if fr:
        cmd += ["-range", str(fr[0]), str(fr[1])]
    stuff = Path(args.stuff) if args.stuff else paths.stuff_dir()
    timer = jsonout.Timer()
    work = Path(tempfile.mkdtemp(prefix="nt_convert_"))
    try:
        shutil.copytree(stuff, work / "stuff", symlinks=True)
        env = tcomposer.environment("default", False)
        env["TOONZROOT"] = str(work / "stuff")
        import subprocess
        try:
            proc = subprocess.run(cmd, env=env, cwd=work, capture_output=True, text=True, errors="replace",
                                  timeout=args.timeout)
            rc, log = proc.returncode, proc.stdout + proc.stderr
        except subprocess.TimeoutExpired:
            rc, log = None, ""
    finally:
        shutil.rmtree(work, ignore_errors=True)
    timer.mark("convert")
    written = sorted(set(dst.parent.iterdir()) - before)
    files, warnings = [], []
    for p in written:
        rec = {"file": str(p), "bytes": p.stat().st_size}
        if p.suffix.lower() == ".png":
            rec.update(images.frame_record(p))
            rec["file"] = str(p)
        files.append(rec)
    if files and all(f.get("nonblank") == 0 for f in files if "nonblank" in f) and any("nonblank" in f for f in files):
        warnings.append("every frame written is blank (a vector level without -s <scene> has no camera)")
    result = {"src": str(src), "dst": str(dst), "rc": rc, "files": files}
    if rc is None:
        code = jsonout.TIMEOUT
    elif rc == 0 and files:
        code = jsonout.OK
    elif rc == 1:
        code = jsonout.INPUT
    else:
        code = jsonout.INTERNAL
        if rc is not None and rc < 0:
            warnings.append(f"tconverter was killed by signal {-rc}")
    if code != jsonout.OK:
        result["log_tail"] = log.splitlines()[-40:]
    return jsonout.envelope("nexttoonz.convert/1", code == jsonout.OK, result, timing=timer, warnings=warnings), code


def cmd_diff_images(args) -> tuple[dict, int]:
    for p in (args.a, args.b):
        if not Path(p).is_file():
            raise jsonout.NtError(jsonout.INPUT, f"no such image: {p}")
    a, b = images.load(Path(args.a)), images.load(Path(args.b))
    prof = images.PROFILES[args.profile]
    metrics = images.diff_metrics(a, b, max(prof["max_channel_diff"], 0), grid=args.grid)
    passed = images.passes(metrics, args.profile)
    if args.heatmap and metrics.get("same_size"):
        images.heatmap(a, b, Path(args.heatmap), prof["max_channel_diff"])
    result = {"a": str(args.a), "b": str(args.b), "a_pixels": images.pixel_hash(a), "b_pixels": images.pixel_hash(b),
              "profile": args.profile, "limits": prof, "passed": passed, "metrics": metrics,
              "heatmap": str(args.heatmap) if args.heatmap else None}
    return jsonout.envelope("nexttoonz.diff-images/1", passed, result), (jsonout.OK if passed else jsonout.DIFFERENT)


def cmd_diff_tree(args) -> tuple[dict, int]:
    for t in (args.a, args.b):
        if not Path(t).is_dir():
            raise jsonout.NtError(jsonout.INPUT, f"no such tree: {t}")
    return abtree.compare_trees(Path(args.a), Path(args.b), args.out, args.heatmaps)


def cmd_sheet(args) -> tuple[dict, int]:
    if not Path(args.dir).is_dir():
        raise jsonout.NtError(jsonout.INPUT, f"no such directory: {args.dir}")
    try:
        info = sheets.overview(Path(args.dir), Path(args.out), scale=args.scale, title=args.title)
    except FileNotFoundError as e:
        raise jsonout.NtError(jsonout.INPUT, str(e))
    return jsonout.envelope("nexttoonz.sheet/1", True, info), jsonout.OK


def cmd_test(args) -> tuple[dict, int]:
    program = _program(args.program, "NEXTTOONZ_TCOMPOSER", "tcomposer")
    bundle = Path(args.bundle) if args.bundle else paths.default_run_dir("nt-test", args.label or args.suite)
    return suite.run(args.suite, program, bundle, base=args.base, threads=args.threads, rasterizer=args.rasterizer,
                     scenes=args.scenes, ctest_build=args.ctest, timeout=args.timeout, label=args.label)


def cmd_validate(args) -> tuple[dict, int]:
    report = {str(p): schema.validate_file(Path(p)) for p in args.files}
    ok = not any(report.values())
    return jsonout.envelope("nexttoonz.validate/1", ok, {"files": report}), (jsonout.OK if ok else jsonout.DIFFERENT)


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="nexttoonz", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--json", action="store_true", help="print the JSON document on standard output")
    sub = p.add_subparsers(dest="cmd", required=True)

    def common(sp):
        sp.add_argument("--json", action="store_true", default=argparse.SUPPRESS, help=argparse.SUPPRESS)

    sp = sub.add_parser("probe", help="the environment: build, stuff, threads, rand()")
    sp.add_argument("--program", type=Path, help="a tcomposer to describe ($NEXTTOONZ_TCOMPOSER)")
    sp.add_argument("--stuff", type=Path)
    common(sp)
    sp.set_defaults(func=cmd_probe)

    sp = sub.add_parser("render", help="render a scene's frames, write them or only hash them")
    sp.add_argument("scene", help="a scene name of the project, or <project>/scenes/<name>.tnz")
    sp.add_argument("--frames", help="N or FROM-TO")
    sp.add_argument("--out", type=Path)
    sp.add_argument("--threads", type=int, default=1)
    sp.add_argument("--format", default="png")
    sp.add_argument("--rasterizer", choices=tcomposer.RASTERIZERS, default="default")
    sp.add_argument("--camera", help="WxH (needs the engine API)")
    sp.add_argument("--hash-only", action="store_true")
    sp.add_argument("--program", type=Path, help="tcomposer ($NEXTTOONZ_TCOMPOSER)")
    sp.add_argument("--project", type=Path)
    sp.add_argument("--stuff", type=Path)
    sp.add_argument("--timeout", type=float, default=600)
    sp.add_argument("--xvfb", action="store_true")
    common(sp)
    sp.set_defaults(func=cmd_render)

    sp = sub.add_parser("convert", help="convert a level (tconverter)")
    sp.add_argument("src")
    sp.add_argument("dst")
    sp.add_argument("--width", type=int)
    sp.add_argument("--scene")
    sp.add_argument("--frames")
    sp.add_argument("--program", type=Path, help="tconverter ($NEXTTOONZ_TCONVERTER)")
    sp.add_argument("--stuff", type=Path)
    sp.add_argument("--timeout", type=float, default=300)
    common(sp)
    sp.set_defaults(func=cmd_convert)

    dp = sub.add_parser("diff", help="compare images or render trees")
    dsub = dp.add_subparsers(dest="what", required=True)
    sp = dsub.add_parser("images")
    sp.add_argument("a")
    sp.add_argument("b")
    sp.add_argument("--profile", choices=sorted(images.PROFILES), default="golden")
    sp.add_argument("--grid", type=int, default=8)
    sp.add_argument("--heatmap")
    common(sp)
    sp.set_defaults(func=cmd_diff_images)
    sp = dsub.add_parser("tree")
    sp.add_argument("a")
    sp.add_argument("b")
    sp.add_argument("--out", type=Path)
    sp.add_argument("--heatmaps", type=Path)
    common(sp)
    sp.set_defaults(func=cmd_diff_tree)

    sp = sub.add_parser("sheet", help="one overview image of every frame under a directory")
    sp.add_argument("dir")
    sp.add_argument("--out", required=True)
    sp.add_argument("--scale", type=float, default=0.25)
    sp.add_argument("--title")
    common(sp)
    sp.set_defaults(func=cmd_sheet)

    sp = sub.add_parser("test", help="run suites, write a results bundle")
    sp.add_argument("--suite", choices=sorted(suite.SUITES), default="pr")
    sp.add_argument("--program", type=Path, help="the tcomposer under test ($NEXTTOONZ_TCOMPOSER)")
    sp.add_argument("--base", type=Path, help="the base: a tcomposer, or a render tree it made")
    sp.add_argument("--bundle", type=Path)
    sp.add_argument("--threads", type=int, nargs="+")
    sp.add_argument("--rasterizer", choices=tcomposer.RASTERIZERS, default="default")
    sp.add_argument("--scene", action="append", dest="scenes")
    sp.add_argument("--ctest", type=Path, help="a build directory whose unit tests to run")
    sp.add_argument("--timeout", type=float, default=600)
    sp.add_argument("--label")
    common(sp)
    sp.set_defaults(func=cmd_test)

    sp = sub.add_parser("validate", help="check JSON documents against their schemas")
    sp.add_argument("files", nargs="+")
    common(sp)
    sp.set_defaults(func=cmd_validate)

    for name, why in NEEDS_ENGINE.items():
        sp = sub.add_parser(name, help=f"not in the Python front end: {why}")
        sp.add_argument("rest", nargs=argparse.REMAINDER)
        common(sp)
        sp.set_defaults(func=None, why=why)
    return p


def main(argv: list[str] | None = None) -> int:
    p = parser()
    try:
        args = p.parse_args(argv)
    except SystemExit as e:
        return jsonout.USAGE if e.code not in (0, None) else 0
    try:
        if args.func is None:
            raise jsonout.NtError(jsonout.ENVIRONMENT, f"{args.cmd}: {args.why}")
        doc, code = args.func(args)
    except jsonout.NtError as e:
        jsonout.log(f"nexttoonz: {e}")
        if args.json:
            jsonout.emit(jsonout.envelope(f"nexttoonz.error/1", False, {"command": args.cmd, "exit_code": e.code,
                                                                       "message": str(e)}))
        return e.code
    except KeyboardInterrupt:
        return 130
    if args.json:
        jsonout.emit(doc)
    else:
        jsonout.log(f"{args.cmd}: {'ok' if doc.get('ok') else 'not ok'} (exit {code})")
    return code
