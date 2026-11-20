"""`nexttoonz test`: run suites and write a results bundle.

The bundle is the layout of doc/nexttoonz_cli_ci.md, section 4.3, as far as
the suites below fill it:

    manifest.json          nexttoonz.manifest/1: what was tested against what, stages
    gates.json             nexttoonz.gates/1: the machine's verdict per gate
    junit/ctest.xml        unit suite (ctest -LE golden --output-junit)
    golden/<scene>.json    golden suite (run_golden.py --json)
    golden/<scene>/out/    the golden renders; diff/golden/<scene>/ their diff images
    renders/{test,base}/   ab suite: render trees (nexttoonz.render-set/1)
    ab/<threads>/<scene>.json, ab/summary.json
    diff/<scene>/<threads>t_<frame>.png   heatmaps of the frames that differ
    sheets/overview.png    every frame of the tested build, small
    logs/                  the last lines of whatever failed

Suites: unit, golden, ab; "pr" is golden + ab (+ unit with --ctest).
"""

from __future__ import annotations

import datetime
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from . import abtree, determinism, jsonout, paths, sheets, tcomposer

SUITES = {"unit": ["unit"], "golden": ["golden"], "ab": ["ab"], "pr": ["unit", "golden", "ab"]}


def _now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")


def _golden(program: Path, bundle: Path, scenes: list[str] | None, timeout: float) -> dict:
    run_golden = paths.golden_dir() / "run_golden.py"
    expected = paths.expected_dir()
    project = paths.fixture_project()
    all_scenes = tcomposer.list_scenes(project)
    linux = sys.platform.startswith("linux")
    results, skipped = [], []
    work_root = Path(tempfile.mkdtemp(prefix="nt_golden_"))
    try:
        for s in all_scenes:
            if scenes and s not in scenes:
                continue
            if not linux and determinism.GOLDEN_LINUX_ONLY.match(s):
                skipped.append(s)  # tests/golden/CMakeLists.txt compares these on Linux only
                continue
            out_json = bundle / "golden" / f"{s}.json"
            work = work_root / s
            cmd = [sys.executable, str(run_golden), "--tcomposer", str(program), "--stuff", str(paths.stuff_dir()),
                   "--project", str(project), "--scene", f"scenes/{s}.tnz", "--expected", str(expected / s),
                   "--work", str(work), "--json", str(out_json)]
            env = tcomposer.environment("default", False)
            try:
                proc = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=timeout)
                rc, log = proc.returncode, proc.stdout + proc.stderr
            except subprocess.TimeoutExpired:
                rc, log = jsonout.TIMEOUT, f"timeout after {timeout} s"
            if (work / "out").is_dir():
                shutil.copytree(work / "out", bundle / "golden" / s / "out", dirs_exist_ok=True)
            if (work / "diff").is_dir():
                shutil.copytree(work / "diff", bundle / "diff" / "golden" / s, dirs_exist_ok=True)
            shutil.rmtree(work, ignore_errors=True)
            status = "pass" if rc == 0 else "fail"
            if rc != 0:
                (bundle / "logs").mkdir(parents=True, exist_ok=True)
                (bundle / "logs" / f"golden_{s}.log").write_text("\n".join(log.splitlines()[-200:]) + "\n")
            results.append({"scene": s, "status": status, "rc": rc})
            jsonout.log(f"golden {s}: {status}")
    finally:
        shutil.rmtree(work_root, ignore_errors=True)
    failed = [r["scene"] for r in results if r["status"] != "pass"]
    return {"name": "golden", "status": "fail" if failed else "pass",
            "summary": f"{len(results) - len(failed)}/{len(results)} scenes pass",
            "threshold": "tests/golden/compare_images.py defaults (2 levels, 0.5% of the pixels)",
            "failed": failed, "skipped": skipped, "evidence": ["golden/"]}


def _unit(build: Path, bundle: Path) -> dict:
    junit = bundle / "junit" / "ctest.xml"
    junit.parent.mkdir(parents=True, exist_ok=True)
    proc = subprocess.run(["ctest", "--test-dir", str(build), "-LE", "golden", "--output-on-failure",
                           "--output-junit", str(junit)], capture_output=True, text=True)
    if proc.returncode != 0:
        (bundle / "logs").mkdir(parents=True, exist_ok=True)
        (bundle / "logs" / "unit.log").write_text("\n".join(proc.stdout.splitlines()[-200:]) + "\n")
    tail = [line for line in proc.stdout.splitlines() if "tests passed" in line]
    return {"name": "unit", "status": "pass" if proc.returncode == 0 else "fail",
            "summary": tail[-1].strip() if tail else f"ctest exited with {proc.returncode}",
            "threshold": "every test passes", "evidence": ["junit/ctest.xml"]}


def _ab(program: Path, base: Path, bundle: Path, threads: list[int], rasterizer: str,
        scenes: list[str] | None, timeout: float) -> tuple[dict, dict]:
    skip = abtree.default_skip()
    test_tree = bundle / "renders" / "test"
    abtree.render_tree(program, test_tree, threads, rasterizer=rasterizer, scenes=scenes, skip=skip,
                       timeout=timeout, label="test")
    if (base / "render.json").is_file():
        base_tree = base
    else:
        base_tree = bundle / "renders" / "base"
        abtree.render_tree(base, base_tree, threads, rasterizer=rasterizer, scenes=scenes, skip=skip,
                           timeout=timeout, label="base")
    doc, _ = abtree.compare_trees(base_tree, test_tree, bundle / "ab", bundle / "diff")
    c = doc["result"]["counts"]
    failed_scenes = [f"{s['threads']} {s['scene']}" for s in doc["result"]["scenes"] if s["status"] == "render-failed"]
    bad = c["different"] + c["missing_in_b"] + c["only_in_b"] + len(failed_scenes)
    gate = {"name": "ab", "status": "fail" if bad else "pass",
            "summary": (f"{c['identical']}/{c['frames']} frames identical, {c['different']} different"
                        f" (+{c['different_unstable']} unstable, not judged), threads {doc['result']['threads']}"),
            "threshold": "byte for byte on the pixel hash (same machine)",
            "counts": c, "render_failed": failed_scenes, "evidence": ["ab/summary.json"]}
    return gate, {"base_tree": str(base_tree), "test_tree": str(test_tree)}


def run(suite: str, program: Path, bundle: Path, *, base: Path | None = None, threads: list[int] | None = None,
        rasterizer: str = "default", scenes: list[str] | None = None, ctest_build: Path | None = None,
        timeout: float = 600, label: str | None = None) -> tuple[dict, int]:
    program = Path(program).resolve()
    if not program.is_file():
        raise jsonout.NtError(jsonout.INPUT, f"no such program: {program}")
    stages = SUITES[suite]
    if "ab" in stages and base is None:
        raise jsonout.NtError(jsonout.USAGE, "the ab suite needs --base (a tcomposer or a render tree)")
    bundle.mkdir(parents=True, exist_ok=True)
    build = tcomposer.build_info(program)
    manifest = {"schema": "nexttoonz.manifest/1", "label": label, "suite": suite, "started": _now(),
                "tool": jsonout.tool(), "host": jsonout.host(),
                "test": {"program": str(program), "build": build},
                "base": None, "stages": []}
    if base is not None:
        base = Path(base).resolve()
        if base.is_file():
            manifest["base"] = {"program": str(base), "build": tcomposer.build_info(base)}
        elif (base / "render.json").is_file():
            manifest["base"] = {"render_tree": str(base),
                                "build": json.loads((base / "render.json").read_text())["tool"].get("build")}
        else:
            raise jsonout.NtError(jsonout.INPUT, f"--base is neither a program nor a render tree: {base}")
    gates = []
    for stage in stages:
        start = _now()
        if stage == "unit":
            if ctest_build is None:
                gate = {"name": "unit", "status": "skipped", "summary": "no --ctest build directory given"}
            else:
                gate = _unit(Path(ctest_build), bundle)
        elif stage == "golden":
            gate = _golden(program, bundle, scenes, timeout)
        else:
            gate, trees = _ab(program, base, bundle, threads or [1, 8], rasterizer, scenes, timeout)
            manifest["ab"] = {**trees, "threads": threads or [1, 8], "rasterizer": rasterizer}
        gates.append(gate)
        manifest["stages"].append({"name": stage, "started": start, "ended": _now(), "status": gate["status"]})
    overview_src = bundle / "renders" / "test" if (bundle / "renders" / "test").is_dir() else bundle / "golden"
    if overview_src.is_dir():
        try:
            manifest["overview"] = sheets.overview(overview_src, bundle / "sheets" / "overview.png",
                                                   title=f"{label or suite}: {program}")
        except FileNotFoundError:
            pass
    manifest["ended"] = _now()
    gates_doc = {"schema": "nexttoonz.gates/1", "gates": gates,
                 "ok": all(g["status"] in ("pass", "known", "skipped") for g in gates)}
    jsonout.write(manifest, bundle / "manifest.json")
    jsonout.write(gates_doc, bundle / "gates.json")
    doc = jsonout.envelope("nexttoonz.test/1", gates_doc["ok"], {"bundle": str(bundle), "gates": gates},
                           build=build)
    return doc, (jsonout.OK if gates_doc["ok"] else jsonout.DIFFERENT)
