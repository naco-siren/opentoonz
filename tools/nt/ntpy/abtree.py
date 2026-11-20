"""Render trees and their comparison: the A/B of two builds.

A render tree is what `render_tree` writes for one build:

    <tree>/render.json                  nexttoonz.render-set/1
    <tree>/<threads>/<scene>/<scene>.<frame>.png

`compare_trees` compares two of them frame by frame on the pixel hash
(decoded, premultiplied pixels: a change of PNG encoder alone is not a
difference) and writes

    <out>/<threads>/<scene>.json        nexttoonz.ab-scene/1
    <out>/summary.json                  nexttoonz.diff-tree/1
    <heatmaps>/<scene>/<threads>t_<frame>.png   only for frames that differ

Trees without render.json (those of the old ab_tcomposer.py) are read by
listing their PNG files.
"""

from __future__ import annotations

import datetime
import json
import sys
from pathlib import Path

from . import determinism, images, jsonout, paths, tcomposer

DEFAULT_SKIP_DARWIN = ("fx_sweep_gpu",)  # GLSL fxs crash tcomposer on macOS (known issue 15)


def default_skip() -> tuple[str, ...]:
    return DEFAULT_SKIP_DARWIN if sys.platform == "darwin" else ()


def render_tree(program: Path, out: Path, threads: list[int], *, rasterizer: str = "default",
                scenes: list[str] | None = None, skip: tuple[str, ...] = (), timeout: float = 300,
                xvfb: bool = False, project: Path | None = None, stuff: Path | None = None,
                label: str | None = None) -> tuple[dict, int]:
    project = Path(project or paths.fixture_project())
    stuff = Path(stuff or paths.stuff_dir())
    if not Path(program).is_file():
        raise jsonout.NtError(jsonout.INPUT, f"no such program: {program}")
    for what, p in (("project", project), ("stuff", stuff)):
        if not p.is_dir():
            raise jsonout.NtError(jsonout.ENVIRONMENT if what == "stuff" else jsonout.INPUT, f"no {what} at {p}")
    timer = jsonout.Timer()
    rendered = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
    build = tcomposer.build_info(Path(program))
    effective = tcomposer.effective_rasterizer(rasterizer, build)
    all_scenes = tcomposer.list_scenes(project)
    chosen = [s for s in all_scenes if not scenes or s in scenes]
    unknown = sorted(set(scenes or ()) - set(all_scenes))
    if unknown:
        raise jsonout.NtError(jsonout.INPUT, f"no such scene(s) in {project}: {' '.join(unknown)}")
    out.mkdir(parents=True, exist_ok=True)
    records = []
    with tcomposer.Workspace(stuff, project) as ws:
        timer.mark("setup")
        for n in threads:
            for s in chosen:
                level = determinism.level(s, effective, n)
                if s.startswith(tuple(skip)):
                    records.append({"scene": s, "threads": n, "skipped": "excluded on this platform",
                                    "determinism": level, "ok": True, "frames": []})
                    continue
                rec = tcomposer.render_scene(Path(program), ws, s, out / str(n) / s, threads=n,
                                             rasterizer=rasterizer, timeout=timeout, xvfb=xvfb)
                rec["determinism"] = level
                for f in rec["frames"]:
                    f["file"] = f"{n}/{s}/{f['file']}"
                    f["determinism"] = level
                records.append(rec)
                jsonout.log(f"{n} {s}: rc={rec['rc']} frames={len(rec['frames'])} {rec['seconds']}s")
    timer.mark("render")
    failed = [f"{r['threads']} {r['scene']}" for r in records if not r["ok"]]
    result = {
        "label": label,
        "rendered": rendered,
        "project": str(project),
        "threads": threads,
        "rasterizer": {"requested": rasterizer, "effective": effective},
        "scenes": records,
        "failed": failed,
    }
    doc = jsonout.envelope("nexttoonz.render-set/1", not failed, result, build=build, timing=timer,
                           warnings=[f"render failed: {f}" for f in failed])
    jsonout.write(doc, out / "render.json")
    return doc, (jsonout.INTERNAL if failed else jsonout.OK)


def _read_tree(tree: Path) -> dict:
    """{(threads, scene): {"rec": scene record or None, "frames": {frame: record}}}"""
    tree = Path(tree)
    meta = tree / "render.json"
    side: dict = {"tree": str(tree), "manifest": None, "entries": {}}
    if meta.is_file():
        doc = json.loads(meta.read_text())
        side["manifest"] = {"build": doc["tool"].get("build"), "rasterizer": doc["result"]["rasterizer"],
                            "label": doc["result"].get("label"), "host": doc.get("host")}
        for rec in doc["result"]["scenes"]:
            frames = {f["frame"]: f for f in rec.get("frames", [])}
            side["entries"][(rec["threads"], rec["scene"])] = {"rec": rec, "frames": frames}
        return side
    for tdir in sorted(p for p in tree.iterdir() if p.is_dir() and p.name.isdigit()):
        for sdir in sorted(p for p in tdir.iterdir() if p.is_dir()):
            frames = {}
            for png in sorted(sdir.glob("*.png")):
                fr = images.frame_number(png)
                if fr is not None:
                    frames[fr] = {"frame": fr, "file": str(png.relative_to(tree))}
            side["entries"][(int(tdir.name), sdir.name)] = {"rec": None, "frames": frames}
    return side


def _pixels(side: dict, rec: dict) -> str:
    if "pixels" not in rec:
        rec["pixels"] = images.pixel_hash(images.load(Path(side["tree"]) / rec["file"]))
    return rec["pixels"]


def _png(side: dict, rec: dict) -> str:
    if "png" not in rec:
        rec["png"] = images.file_hash(Path(side["tree"]) / rec["file"])
    return rec["png"]


def _rendered(entry: dict | None) -> bool:
    if entry is None:
        return False
    rec = entry["rec"]
    if rec is None:
        return bool(entry["frames"])
    return not rec.get("skipped") and rec.get("ok", False)


def compare_trees(a: Path, b: Path, out: Path | None = None, heatmaps: Path | None = None,
                  max_channel_diff: int = 2) -> tuple[dict, int]:
    timer = jsonout.Timer()
    A, B = _read_tree(Path(a)), _read_tree(Path(b))
    # Only the thread counts both trees have are compared; the others are
    # reported, not counted as missing.
    ta = {t for t, _ in A["entries"]}
    tb = {t for t, _ in B["entries"]}
    warnings = [f"threads {t} only in {side}" for side, only in (("A", ta - tb), ("B", tb - ta)) for t in sorted(only)]
    keys = sorted(k for k in set(A["entries"]) | set(B["entries"]) if k[0] in ta & tb)
    rasterizers = {}
    for name, side in (("a", A), ("b", B)):
        m = side["manifest"]
        rasterizers[name] = m["rasterizer"]["effective"] if m else None
    counts = {"frames": 0, "identical": 0, "different": 0, "different_unstable": 0, "missing_in_b": 0,
              "only_in_b": 0, "png_bytes_differ_pixels_same": 0, "scenes_failed_in_a": 0,
              "scenes_failed_in_b": 0, "scenes_skipped": 0}
    scenes_out, different_frames = [], []
    for threads, scene in keys:
        ea, eb = A["entries"].get((threads, scene)), B["entries"].get((threads, scene))
        level = max((determinism.level(scene, r, threads) for r in rasterizers.values() if r),
                    key=determinism.LEVELS.index, default=determinism.level(scene, None, threads))
        srec = {"schema": "nexttoonz.ab-scene/1", "scene": scene, "threads": threads, "determinism": level,
                "a": {"tree": A["tree"], "rasterizer": rasterizers["a"]},
                "b": {"tree": B["tree"], "rasterizer": rasterizers["b"]},
                "frames": [], "counts": {"identical": 0, "different": 0, "missing_in_b": 0, "only_in_b": 0}}
        skipped = any(e and e["rec"] and e["rec"].get("skipped") for e in (ea, eb))
        ra, rb = _rendered(ea), _rendered(eb)
        for side, e in (("a", ea), ("b", eb)):
            if e and e["rec"]:
                srec[side].update({k: e["rec"].get(k) for k in ("rc", "seconds", "timed_out", "skipped") if k in e["rec"]})
        if skipped:
            srec["status"] = "skipped"
            counts["scenes_skipped"] += 1
        elif not ra or not rb:
            srec["status"] = "render-failed" if (ra or rb) else "both-failed"
            counts["scenes_failed_in_a"] += not ra
            counts["scenes_failed_in_b"] += not rb
        if not skipped and (ra or rb):
            fa = ea["frames"] if ea else {}
            fb = eb["frames"] if eb else {}
            for fr in sorted(set(fa) | set(fb)):
                ra_, rb_ = fa.get(fr), fb.get(fr)
                frec = {"frame": fr}
                if ra_ is None:
                    frec["status"] = "only-in-b"
                    srec["counts"]["only_in_b"] += 1
                    counts["only_in_b"] += 1
                elif rb_ is None:
                    frec["status"] = "missing-in-b"
                    srec["counts"]["missing_in_b"] += 1
                    counts["missing_in_b"] += 1
                else:
                    counts["frames"] += 1
                    pa, pb = _pixels(A, ra_), _pixels(B, rb_)
                    frec.update({"a": pa, "b": pb, "a_file": ra_["file"], "b_file": rb_["file"]})
                    if pa == pb:
                        frec["status"] = "identical"
                        srec["counts"]["identical"] += 1
                        counts["identical"] += 1
                        if _png(A, ra_) != _png(B, rb_):
                            counts["png_bytes_differ_pixels_same"] += 1
                    else:
                        ia = images.load(Path(A["tree"]) / ra_["file"])
                        ib = images.load(Path(B["tree"]) / rb_["file"])
                        frec["status"] = "different"
                        frec["diff"] = images.diff_metrics(ia, ib, max_channel_diff)
                        srec["counts"]["different"] += 1
                        if level == "unstable":
                            counts["different_unstable"] += 1
                        else:
                            counts["different"] += 1
                        if heatmaps is not None and frec["diff"].get("same_size"):
                            hm = Path(heatmaps) / scene / f"{threads}t_{fr:04d}.png"
                            images.heatmap(ia, ib, hm, max_channel_diff)
                            frec["heatmap"] = str(hm)
                        different_frames.append({
                            "scene": scene, "threads": threads, "frame": fr, "determinism": level,
                            "fraction_over_tolerance": frec["diff"].get("fraction_over_tolerance"),
                            "fraction_differing": frec["diff"].get("fraction_differing"),
                            "peak": frec["diff"].get("peak"),
                        })
                srec["frames"].append(frec)
            if "status" not in srec:
                c = srec["counts"]
                srec["status"] = ("different" if c["different"] or c["missing_in_b"] or c["only_in_b"]
                                  else "identical")
        if out is not None:
            jsonout.write(srec, Path(out) / str(threads) / f"{scene}.json")
        scenes_out.append({k: srec[k] for k in ("scene", "threads", "determinism", "status", "counts")})
    timer.mark("compare")
    different_frames.sort(key=lambda d: -(d["fraction_differing"] or 0))
    judged_bad = counts["different"] + counts["missing_in_b"] + counts["only_in_b"] + \
        sum(1 for s in scenes_out if s["status"] == "render-failed")
    result = {
        "a": {"tree": A["tree"], **(A["manifest"] or {})},
        "b": {"tree": B["tree"], **(B["manifest"] or {})},
        "counts": counts,
        "scenes": scenes_out,
        "different_frames": different_frames,
    }
    result["threads"] = sorted(ta & tb)
    doc = jsonout.envelope("nexttoonz.diff-tree/1", judged_bad == 0, result, timing=timer, warnings=warnings)
    if out is not None:
        jsonout.write(doc, Path(out) / "summary.json")
    jsonout.log(f"identical {counts['identical']}, different {counts['different']}"
                f" (+{counts['different_unstable']} in unstable scenes), missing in B {counts['missing_in_b']},"
                f" only in B {counts['only_in_b']}; scenes failed in A {counts['scenes_failed_in_a']},"
                f" in B {counts['scenes_failed_in_b']}, skipped {counts['scenes_skipped']}")
    return doc, (jsonout.DIFFERENT if judged_bad else jsonout.OK)
