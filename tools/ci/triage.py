#!/usr/bin/env python3
"""Machine triage of a results bundle, and the blind view the AI acceptance reads.

    triage.py <bundle> [--known FILE] [--seed N] [--max-sheets 10] [--criteria FILE]
    triage.py round2 <bundle> --pr FILE [--diffstat FILE]

doc/nexttoonz_cli_ci.md, section 4.4. From the bundle that `nexttoonz test`
wrote (manifest.json, gates.json, ab/, golden/) it

1. matches every difference against the known-differences registry by
   program (tests/known_differences.yaml): a matched difference within its
   bounds is "known", anything else is "unexplained";
2. re-derives the gates with that: a gate whose only failures are known is
   "known", not "pass";
3. picks the frames a reader should look at (unexplained first, by how much
   changed; one example per matched registry entry), at most --max-sheets,
   and draws each as "X | Y | heatmap" where X and Y are base and test in
   random order;
4. adds one canary: a frame of the bundle that did not change, with a defect
   made on purpose (a one-pixel shift, a colour one step off, or a missing
   stroke, chosen by the seed), drawn and listed exactly like a real one;
5. writes triage.json (what the AI may read), triage.key (what it may not:
   the X/Y mapping and the canary), and accept/view/, the only directory the
   AI acceptance is given: triage.json, gates.json, manifest.json, the
   review sheets, the registry, the failure logs and the step's acceptance
   criteria. triage.json and gates.json there count the canary like a real
   difference, so it cannot be told apart; the real numbers are in the key.

`round2` adds the pull request's description (and diffstat) to the view once
round 1 has written accept/round1.json, and freezes round 1 by its hash.

Run it with the golden tests' Python (numpy, Pillow).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import random
import shutil
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "nt"))
from ntpy import determinism, images, jsonout, known, paths, sheets  # noqa: E402

CANARY_KINDS = ("shift", "colour-step", "missing-stroke")


def _read(path: Path):
    return json.loads(path.read_text()) if path.is_file() else None


def _platform(manifest: dict) -> str:
    host = manifest.get("host", {})
    return f"{host.get('os', '').split('-')[0]}-{host.get('arch', '')}"


def _day(render_json: Path) -> str | None:
    doc = _read(render_json)
    return (doc or {}).get("result", {}).get("rendered", "")[:10] or None


# ------------------------------------------------------------ collect items

def collect(bundle: Path, manifest: dict, entries: list[dict]) -> list[dict]:
    """Every difference of the bundle, matched against the registry."""
    platform = _platform(manifest)
    items = []
    summary = _read(bundle / "ab" / "summary.json")
    if summary:
        rasterizers = [summary["result"]["a"].get("rasterizer", {}).get("effective"),
                       summary["result"]["b"].get("rasterizer", {}).get("effective")]
        base_tree, test_tree = Path(summary["result"]["a"]["tree"]), Path(summary["result"]["b"]["tree"])
        days = (_day(base_tree / "render.json"), _day(test_tree / "render.json"))
        same_day = days[0] == days[1] if all(days) else None
        for scene_file in sorted((bundle / "ab").glob("*/*.json")):
            s = _read(scene_file)
            if s.get("status") == "skipped":
                continue  # a policy, not a difference: listed under not_covered
            if s.get("status") in ("render-failed", "both-failed"):
                item = {"comparison": "ab", "scene": s["scene"], "threads": s["threads"], "platform": platform,
                        "rasterizers": rasterizers, "same_day": same_day, "status": s["status"],
                        "determinism": s["determinism"], "source": str(scene_file.relative_to(bundle)),
                        "detail": _failure(s)}
                if s["status"] != "both-failed":
                    item["known"] = known.first_match(entries, item)
                    items.append(item)
            for f in s["frames"]:
                if f["status"] == "identical":
                    continue
                if f["status"] != "different" and s["status"] in ("render-failed", "both-failed"):
                    continue  # the frames a failed render did not write: the failure is the item
                item = {"comparison": "ab", "scene": s["scene"], "threads": s["threads"], "frame": f["frame"],
                        "platform": platform, "rasterizers": rasterizers, "same_day": same_day,
                        "status": "different" if f["status"] == "different" else f["status"],
                        "determinism": s["determinism"], "metrics": f.get("diff", {}),
                        "a_file": f.get("a_file"), "b_file": f.get("b_file"),
                        "base_tree": str(base_tree), "test_tree": str(test_tree),
                        "heatmap": f.get("heatmap"),
                        "source": f"{scene_file.relative_to(bundle)}#frames[{f['frame']}]"}
                item["known"] = known.first_match(entries, item)
                items.append(item)
    for g in sorted((bundle / "golden").glob("*.json")):
        d = _read(g)
        rast = [d.get("env", {}).get("NEXTTOONZ_VECTOR_RASTERIZER") or
                (manifest.get("test", {}).get("build") or {}).get("default_rasterizer")]
        if d["status"] == "error":
            item = {"comparison": "golden", "scene": d["scene"], "platform": platform, "rasterizers": rast,
                    "status": "render-failed", "source": str(g.relative_to(bundle)), "error": d.get("error")}
            item["known"] = known.first_match(entries, item)
            items.append(item)
            continue
        for f in d["frames"]:
            # Within the golden tolerance is not a difference: baselines come
            # from other platforms (Linux for the raster scenes).
            if f.get("identical", False) or f.get("passed", False):
                continue
            metrics = {k: f[k] for k in ("fraction_over_tolerance", "peak", "pixels_differing") if k in f}
            item = {"comparison": "golden", "scene": d["scene"], "frame": f["frame"], "platform": platform,
                    "rasterizers": rast, "status": "different", "passed": f.get("passed"), "metrics": metrics,
                    "golden_file": str(Path("golden") / d["scene"] / f["file"]),
                    "expected_file": str(Path(d["expected_dir"]) / Path(f["file"]).name),
                    "source": f"{g.relative_to(bundle)}#frames[{f['frame']}]"}
            item["known"] = known.first_match(entries, item)
            items.append(item)
    return items


def _failure(s: dict) -> str:
    """Which side failed to render, and how (from the ab-scene record)."""
    out = []
    for side, name in (("a", "base"), ("b", "test")):
        r = s.get(side) or {}
        if r.get("timed_out"):
            out.append(f"{name}: timed out after {r.get('seconds')} s")
        elif r.get("rc") not in (0, None):
            out.append(f"{name}: exit status {r.get('rc')}")
    return "; ".join(out) or s["status"]


def _size(item: dict) -> float:
    m = item.get("metrics") or {}
    return float(m.get("fraction_differing") or m.get("fraction_over_tolerance") or 0.0)


# ------------------------------------------------------------ gates

def regate(gates: list[dict], items: list[dict]) -> list[dict]:
    out = []
    for g in gates:
        g = dict(g)
        if g["name"] == "ab" and g["status"] == "fail":
            judged = [i for i in items if i["comparison"] == "ab" and i.get("determinism") != "unstable"]
            if judged and all(i["known"] for i in judged):
                g["status"] = "known"
                g["known"] = sorted({i["known"] for i in judged})
        if g["name"] == "golden" and g["status"] == "fail":
            failing = [i for i in items if i["comparison"] == "golden" and
                       (i["status"] == "render-failed" or i.get("passed") is False)]
            if failing and all(i["known"] for i in failing):
                g["status"] = "known"
                g["known"] = sorted({i["known"] for i in failing})
        out.append(g)
    return out


# ------------------------------------------------------------ canary

def make_canary(clean: np.ndarray, kind: str, rng: random.Random) -> np.ndarray:
    """A real-looking defect in a premultiplied frame."""
    bad = clean.copy()
    alpha = clean[..., 3]
    if kind == "shift":
        ys, xs = np.nonzero(alpha > 0)
        y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
        bad[y0:y1, x0:x1] = np.roll(clean[y0:y1, x0:x1], 1, axis=1)
        return bad
    if kind == "colour-step":
        opaque = clean[alpha == 255][:, :3]
        colours, counts = np.unique(opaque, axis=0, return_counts=True)
        # the most common colour that is neither white-ish nor black-ish
        order = np.argsort(-counts)
        pick = next((colours[i] for i in order if 30 < int(colours[i].mean()) < 225), colours[order[0]])
        mask = np.all(clean[..., :3] == pick, axis=2) & (alpha == 255)
        channel = int(np.argmin(pick))
        bad[mask, channel] = np.clip(bad[mask, channel] + 24, 0, 255)
        return bad
    # missing-stroke: the darkest ink in the grid cell that has most of it
    # goes, replaced by what is around it
    lum = clean[..., :3].mean(axis=2)
    ink = (lum < 110) & (alpha > 128)
    h, w = ink.shape
    best, cell = -1, (0, 0)
    for gy in range(4):
        for gx in range(4):
            n = int(ink[gy * h // 4:(gy + 1) * h // 4, gx * w // 4:(gx + 1) * w // 4].sum())
            if n > best:
                best, cell = n, (gx, gy)
    gx, gy = cell
    ys, xs = slice(gy * h // 4, (gy + 1) * h // 4), slice(gx * w // 4, (gx + 1) * w // 4)
    region = clean[ys, xs]
    fill = np.median(region[~ink[ys, xs]], axis=0) if (~ink[ys, xs]).any() else np.array([0, 0, 0, 0])
    sub = bad[ys, xs]
    sub[ink[ys, xs]] = np.rint(fill).astype(sub.dtype)
    return bad


# ------------------------------------------------------------ main

def triage(bundle: Path, known_path: Path, seed: int | None, max_sheets: int, criteria: Path | None) -> int:
    manifest = _read(bundle / "manifest.json")
    gates_doc = _read(bundle / "gates.json")
    if manifest is None or gates_doc is None:
        jsonout.log(f"triage: {bundle} has no manifest.json or gates.json (made by `nexttoonz test`)")
        return jsonout.INPUT
    entries = known.load(known_path)
    seed = seed if seed is not None else random.SystemRandom().randrange(1 << 30)
    rng = random.Random(seed)
    items = collect(bundle, manifest, entries)
    gates = regate(gates_doc["gates"], items)

    judged = [i for i in items if i.get("determinism") != "unstable"]
    unexplained = sorted([i for i in judged if not i["known"]], key=lambda i: -_size(i))
    known_items = [i for i in judged if i["known"]]
    by_entry: dict[str, list[dict]] = {}
    for i in known_items:
        by_entry.setdefault(i["known"], []).append(i)

    # what to look at: unexplained first, then one example per known entry
    picks = [i for i in unexplained if "metrics" in i and i.get("status") == "different"][: max_sheets - 1]
    # up to two examples per entry: its largest difference and another one
    for round_ in (0, 1):
        for kd, group in sorted(by_entry.items()):
            if len(picks) >= max_sheets - 1:
                break
            frames = sorted((i for i in group if "metrics" in i), key=_size, reverse=True)
            if round_ == 0 and frames:
                picks.append(frames[0])
            elif round_ == 1 and len(frames) > 1:
                picks.append(rng.choice(frames[1:]))

    # the canary: an unchanged frame of the test renders, with a defect
    identical = []
    for scene_file in sorted((bundle / "ab").glob("1/*.json")):
        s = _read(scene_file)
        if s["determinism"] != "exact":
            continue
        identical += [(s, f) for f in s["frames"] if f["status"] == "identical"]
    summary = _read(bundle / "ab" / "summary.json")
    canary = None
    if identical and summary:
        s, f = rng.choice(identical)
        kind = rng.choice(CANARY_KINDS)
        base_tree = Path(summary["result"]["a"]["tree"])
        clean = images.load(base_tree / f["a_file"])
        bad = make_canary(clean, kind, rng)
        canary = {"comparison": "ab", "scene": s["scene"], "threads": s["threads"], "frame": f["frame"],
                  "status": "different", "known": None, "determinism": "exact",
                  "metrics": images.diff_metrics(clean, bad), "_pixels": (clean, bad), "_kind": kind}
    if canary is None:
        # no A/B in the bundle: a golden frame identical to its baseline
        golden_same = []
        for g in sorted((bundle / "golden").glob("*.json")):
            d = _read(g)
            if determinism.level(d["scene"]) == "exact":
                golden_same += [(d, f) for f in d["frames"] if f.get("identical")]
        if golden_same:
            d, f = rng.choice(golden_same)
            kind = rng.choice(CANARY_KINDS)
            clean = images.load(Path(d["expected_dir"]) / Path(f["file"]).name)
            bad = make_canary(clean, kind, rng)
            m = images.diff_metrics(clean, bad)
            canary = {"comparison": "golden", "scene": d["scene"], "threads": None, "frame": f["frame"],
                      "status": "different", "known": None, "determinism": "exact",
                      "metrics": {k: m[k] for k in ("fraction_over_tolerance", "peak", "bbox", "grid") if k in m},
                      "_pixels": (clean, bad), "_kind": kind}

    review = list(picks)
    if canary is not None:
        review.insert(rng.randrange(len(review) + 1), canary)
    review_dir = bundle / "sheets" / "review"
    if review_dir.exists():
        shutil.rmtree(review_dir)
    key = {"schema": "nexttoonz.triage-key/1", "seed": seed, "mapping": {}, "canary": None}
    listed = []
    for n, item in enumerate(review, 1):
        rid = f"R{n:02d}"
        item["id"] = rid
        if item is canary:
            base_px, test_px = canary["_pixels"]
        elif item["comparison"] == "ab":
            base_px = images.load(Path(item["base_tree"]) / item["a_file"])
            test_px = images.load(Path(item["test_tree"]) / item["b_file"])
        else:
            base_px = images.load(Path(item["expected_file"]))
            test_px = images.load(bundle / item["golden_file"])
        x_is_base = rng.random() < 0.5
        x, y = (base_px, test_px) if x_is_base else (test_px, base_px)
        caption = f"{rid}  {item['scene']} frame {item['frame']}" + \
            (f"  {item['threads']} thread(s)" if item.get("threads") else "") + f"  [{item['comparison']}]"
        sheets.pair(x, y, review_dir / f"{rid}.png", caption)
        key["mapping"][rid] = {"X": "base" if x_is_base else "test", "Y": "test" if x_is_base else "base"}
        if item is canary:
            key["canary"] = {"id": rid, "kind": canary["_kind"], "scene": item["scene"], "frame": item["frame"],
                             "threads": item["threads"]}
        m = item["metrics"]
        listed.append({
            "id": rid, "comparison": item["comparison"], "scene": item["scene"], "frame": item["frame"],
            "threads": item.get("threads"), "known": item["known"],
            "metrics": {k: m[k] for k in ("fraction_differing", "fraction_over_tolerance", "peak", "bbox", "grid")
                        if k in m},
            "sheet": f"sheets/review/{rid}.png",
        })

    def brief_item(i: dict) -> dict:
        m = i.get("metrics") or {}
        return {"id": i.get("id"), "comparison": i["comparison"], "scene": i["scene"], "frame": i.get("frame"),
                "threads": i.get("threads"), "status": i["status"], **({"detail": i["detail"]} if "detail" in i else {}),
                "fraction_differing": m.get("fraction_differing"),
                "fraction_over_tolerance": m.get("fraction_over_tolerance"), "peak": m.get("peak")}

    summary_counts = (summary or {}).get("result", {}).get("counts")
    golden = [_read(g) for g in sorted((bundle / "golden").glob("*.json"))]
    real = {
        "gates": gates,
        "counts": {"differences": len(judged), "known": len(known_items), "unexplained": len(unexplained),
                   "unstable_not_judged": len(items) - len(judged)},
        "ab": summary_counts,
        "golden": {"scenes": len(golden), "passed": sum(1 for d in golden if d["status"] == "pass"),
                   "failed": [d["scene"] for d in golden if d["status"] != "pass"]},
        "known": [{"id": kd, "frames": len(g), "scenes": sorted({i["scene"] for i in g}),
                   "largest_fraction_over_tolerance": max((i.get("metrics", {}).get("fraction_over_tolerance") or 0)
                                                          for i in g)}
                  for kd, g in sorted(by_entry.items())],
        "unexplained": [brief_item(i) for i in unexplained],
    }
    key["real"] = real

    # What the AI sees has the canary in it, consistently: counted, listed as
    # unexplained, and failing its gate. Otherwise it could be told apart.
    seen = json.loads(json.dumps(real))
    if canary is not None:
        seen["counts"]["differences"] += 1
        seen["counts"]["unexplained"] += 1
        seen["unexplained"].append(brief_item(canary))
        seen["unexplained"].sort(key=lambda i: -(i["fraction_differing"] or i["fraction_over_tolerance"] or 0))
        if canary["comparison"] == "ab" and seen["ab"]:
            seen["ab"]["identical"] -= 1
            seen["ab"]["different"] += 1
        elif canary["comparison"] == "golden" and canary["scene"] not in seen["golden"]["failed"]:
            seen["golden"]["passed"] -= 1
            seen["golden"]["failed"].append(canary["scene"])
        for g in seen["gates"]:
            if g["name"] == canary["comparison"]:
                g["status"] = "fail"
                g.pop("known", None)
                if g["name"] == "ab":
                    c = seen["ab"]
                    if "counts" in g:
                        g["counts"] = dict(c)  # every copy of the numbers agrees
                    g["summary"] = (f"{c['identical']}/{c['frames']} frames identical, {c['different']} different"
                                    f" (+{c['different_unstable']} unstable, not judged)")
                else:
                    gd = seen["golden"]
                    g["summary"] = f"{gd['passed']}/{gd['scenes']} scenes pass"
                    g["failed"] = sorted(set(g.get("failed", [])) | {canary["scene"]})
    for i in seen["unexplained"]:
        i.pop("id", None)
    not_covered = []
    if real["counts"]["unstable_not_judged"]:
        not_covered.append(f"{real['counts']['unstable_not_judged']} differing frames of the OpenGL fallback with"
                           " several threads (unstable, known issue 26) are not judged")
    skipped = sorted({(s["scene"]) for s in (summary or {}).get("result", {}).get("scenes", [])
                      if s["status"] == "skipped"})
    if skipped:
        not_covered.append("not rendered on this platform: " + ", ".join(skipped))
    for g in gates:
        if g.get("skipped"):
            not_covered.append(f"{g['name']}: compared on Linux only: " + ", ".join(g["skipped"]))
        if g["status"] == "skipped":
            not_covered.append(f"{g['name']}: {g['summary']}")
    triage_doc = {
        "schema": "nexttoonz.triage/1",
        "suite": manifest.get("suite"), "label": manifest.get("label"),
        "test": manifest.get("test"), "base": manifest.get("base"), "platform": _platform(manifest),
        **seen,
        "review": listed,
        "not_covered": not_covered,
    }
    jsonout.write(triage_doc, bundle / "triage.json")
    jsonout.write(key, bundle / "triage.key")

    view = bundle / "accept" / "view"
    if view.exists():
        shutil.rmtree(view)
    (view / "sheets").mkdir(parents=True)
    shutil.copy2(bundle / "triage.json", view / "triage.json")
    jsonout.write({"schema": "nexttoonz.gates/1", "gates": seen["gates"],
                   "ok": all(g["status"] in ("pass", "known", "skipped") for g in seen["gates"])},
                  view / "gates.json")
    shutil.copy2(bundle / "manifest.json", view / "manifest.json")
    shutil.copytree(review_dir, view / "sheets" / "review")
    # Not the overview of every frame: it shows the test build's frames, the
    # canary's undamaged one among them.
    shutil.copy2(known_path, view / "known_differences.yaml")
    if (bundle / "logs").is_dir():
        shutil.copytree(bundle / "logs", view / "logs")
    if criteria is not None:
        shutil.copy2(criteria, view / "criteria.md")
    c = real["counts"]
    jsonout.log(f"triage: {c['differences']} differences, {c['known']} known, {c['unexplained']} unexplained, "
                f"{c['unstable_not_judged']} unstable; {len(listed)} sheets for review; view in {view}")
    return jsonout.OK


def round2(bundle: Path, pr: Path, diffstat: Path | None) -> int:
    r1 = bundle / "accept" / "round1.json"
    if not r1.is_file():
        jsonout.log("triage round2: accept/round1.json is not there yet")
        return jsonout.INPUT
    view = bundle / "accept" / "view" / "round2"
    view.mkdir(parents=True, exist_ok=True)
    shutil.copy2(r1, view / "round1.json")
    shutil.copy2(pr, view / "pr.md")
    if diffstat is not None:
        shutil.copy2(diffstat, view / "diffstat.txt")
    key = _read(bundle / "triage.key")
    key["round1_sha256"] = hashlib.sha256(r1.read_bytes()).hexdigest()
    jsonout.write(key, bundle / "triage.key")
    jsonout.log(f"triage round2: round 1 frozen, PR description in {view}")
    return jsonout.OK


def main(argv: list[str]) -> int:
    if argv[:1] == ["round2"]:
        p = argparse.ArgumentParser(prog="triage.py round2")
        p.add_argument("bundle", type=Path)
        p.add_argument("--pr", type=Path, required=True)
        p.add_argument("--diffstat", type=Path)
        a = p.parse_args(argv[1:])
        return round2(a.bundle, a.pr, a.diffstat)
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("bundle", type=Path)
    p.add_argument("--known", type=Path, default=paths.known_differences())
    p.add_argument("--seed", type=int)
    p.add_argument("--max-sheets", type=int, default=10)
    p.add_argument("--criteria", type=Path, help="the step's acceptance criteria, copied into the view")
    a = p.parse_args(argv)
    return triage(a.bundle, a.known, a.seed, a.max_sheets, a.criteria)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
