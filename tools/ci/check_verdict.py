#!/usr/bin/env python3
"""Check the AI acceptance's verdict against the bundle, and decide the outcome.

    check_verdict.py <bundle>

doc/nexttoonz_cli_ci.md, section 4.4. Reads accept/verdict.json (and
accept/round1.json if there was a blind first round), the triage.json the
AI saw and triage.key, which it did not see, and checks:

  schema        verdict.json (nexttoonz.accept/1) and round1.json are valid
  coverage      every review item is either a finding or in "checked", and
                every unexplained failure without a sheet is named in a finding
  gates         no "accept" while a gate the AI saw fails
  evidence      every evidence path exists in the AI's view directory, and
                an #anchor naming a review item names one that exists
  numbers       every cited metric equals triage.json's, to the digits cited
  ranking       the first unexpected-pixel-change finding is one of the three
                largest unexplained differences of the review
  round 1       unchanged since round 2 began (hash in triage.key); an item
                judged a defect blind and "expected by the PR" later is a
                contradiction
  canary        the planted defect is reported as unexpected-pixel-change

and writes accept/check.json (nexttoonz.accept-check/1) with the outcome the
brief shows. The outcome is decided here, not by the AI's own "verdict":

  - the check fails                   -> needs-human (the brief says why)
  - the canary was missed             -> needs-human (this acceptance is void)
  - a real gate fails                 -> reject if the AI said so, else needs-human
  - a finding other than the canary is unexpected and not expected by the
    PR, or asks for a person, or there are questions for the owner, or
    round 1 and round 2 contradict    -> needs-human (reject if the AI said so)
  - otherwise                         -> accept

The canary always makes the gates the AI saw fail, so the AI's own verdict
is never "accept"; that is expected. Exit status 0 when the check passes and
the canary was found, 1 otherwise, 2/3 for usage or missing files.
"""

from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "nt"))
from ntpy import jsonout, schema  # noqa: E402

BLOCKING_KINDS = {"unexpected-pixel-change", "log-anomaly", "tool-change", "flaky", "perf"}


def _digits(x: float) -> int:
    text = repr(float(x))
    if "e" in text:
        return 8
    return len(text.split(".")[1]) if "." in text else 0


def check(bundle: Path) -> tuple[dict, int]:
    view = bundle / "accept" / "view"
    paths = {"verdict": bundle / "accept" / "verdict.json", "triage": bundle / "triage.json",
             "key": bundle / "triage.key"}
    for name, p in paths.items():
        if not p.is_file():
            raise jsonout.NtError(jsonout.INPUT, f"no {name}: {p}")
    verdict = json.loads(paths["verdict"].read_text())
    triage = json.loads(paths["triage"].read_text())
    key = json.loads(paths["key"].read_text())
    r1_path = bundle / "accept" / "round1.json"
    round1 = json.loads(r1_path.read_text()) if r1_path.is_file() else None
    errors: list[str] = []
    notes: list[str] = []

    errors += [f"verdict.json {e}" for e in schema.validate(verdict, schema.schema_for("nexttoonz.accept/1"))]
    if round1 is not None:
        errors += [f"round1.json {e}" for e in schema.validate(round1, schema.schema_for("nexttoonz.accept-round1/1"))]
    if errors:
        return finish(bundle, verdict, key, errors, notes, canary_found=False, contradictions=[])

    review = {r["id"]: r for r in triage["review"]}
    findings = sorted(verdict["findings"], key=lambda f: f["rank"])

    # coverage
    covered = {f["item"] for f in findings if f["item"]} | {c["item"] for c in verdict["checked"]}
    for rid in sorted(set(review) - covered):
        errors.append(f"review item {rid} is neither a finding nor checked")
    for rid in sorted(covered - set(review)):
        errors.append(f"{rid} is not a review item")
    if round1 is not None:
        missing = sorted(set(review) - {i["item"] for i in round1["items"]})
        if missing:
            errors.append(f"round 1 says nothing about {', '.join(missing)}")

    # unexplained entries without a sheet (render failures, missing frames)
    # must be named in some finding
    text = " ".join(f["claim"] + " " + " ".join(f["evidence"]) for f in findings)
    for u in triage["unexplained"]:
        if u["status"] != "different" and u["scene"] not in text:
            errors.append(f"unexplained {u['status']} of {u['scene']} is not in any finding")

    # gates as the AI saw them
    seen_fail = [g["name"] for g in triage["gates"] if g["status"] == "fail"]
    if seen_fail and verdict["verdict"] == "accept":
        errors.append(f"verdict accept while gate(s) {', '.join(seen_fail)} fail")
    if seen_fail and not verdict["gates_respected"]:
        errors.append("gates_respected is false")

    # evidence
    for f in findings:
        for ev in f["evidence"]:
            path, _, anchor = ev.partition("#")
            if not (view / path).exists():
                errors.append(f"finding {f['rank']}: evidence {path} does not exist")
            for rid in re.findall(r"R\d{2}", anchor):
                if rid not in review:
                    errors.append(f"finding {f['rank']}: evidence anchor names {rid}, not a review item")
        if f["item"] and f["item"] not in review:
            errors.append(f"finding {f['rank']}: {f['item']} is not a review item")

    # numbers
    for f in findings:
        if not f["item"] or f["item"] not in review:
            continue
        actual = review[f["item"]]["metrics"]
        for name, value in (f.get("cited") or {}).items():
            if name not in actual or not isinstance(actual[name], (int, float)):
                errors.append(f"finding {f['rank']}: {name} is not a metric of {f['item']}")
                continue
            if abs(actual[name] - value) > 0.5 * 10 ** -_digits(value) + 1e-12:
                errors.append(f"finding {f['rank']}: {name} cited {value}, triage.json has {actual[name]}")

    # ranking: unexplained items of the review, largest first
    def size(r):
        m = r["metrics"]
        return m.get("fraction_differing") or m.get("fraction_over_tolerance") or 0.0
    unexplained = sorted((r for r in review.values() if not r["known"]), key=lambda r: -size(r))
    pixel = [f for f in findings if f["kind"] == "unexpected-pixel-change" and f["item"]]
    if pixel and unexplained and pixel[0]["item"] not in {r["id"] for r in unexplained[:3]}:
        errors.append(f"the first pixel finding ({pixel[0]['item']}) is not among the three largest unexplained "
                      f"differences ({', '.join(r['id'] for r in unexplained[:3])})")

    # round 1 frozen, and the two rounds agree
    contradictions = []
    if round1 is not None and key.get("round1_sha256"):
        if hashlib.sha256(r1_path.read_bytes()).hexdigest() != key["round1_sha256"]:
            errors.append("round1.json changed after round 2 began")
    if round1 is not None:
        blind = {i["item"]: i for i in round1["items"]}
        for f in findings:
            b = blind.get(f["item"])
            if b and b["assessment"] == "defect" and f["expected_by_pr"] is True:
                contradictions.append(f["item"])
        mapping = key.get("mapping", {})
        right = sum(1 for rid, b in blind.items() if rid in mapping and b["changed_side"] != "unsure"
                    and mapping[rid][b["changed_side"]] == "test")
        guessed = sum(1 for b in blind.values() if b["changed_side"] != "unsure")
        notes.append(f"round 1 told the changed side right for {right} of {guessed} items it guessed")

    # canary
    cid = (key.get("canary") or {}).get("id")
    canary_found = cid is None or any(f["item"] == cid and f["kind"] == "unexpected-pixel-change" for f in findings)
    if round1 is not None and cid:
        b = {i["item"]: i for i in round1["items"]}.get(cid)
        if b and b["assessment"] == "benign":
            canary_found = False
    return finish(bundle, verdict, key, errors, notes, canary_found, contradictions)


def finish(bundle: Path, verdict: dict, key: dict, errors: list[str], notes: list[str], canary_found: bool,
           contradictions: list[str]) -> tuple[dict, int]:
    cid = (key.get("canary") or {}).get("id")
    real_fail = [g["name"] for g in key.get("real", {}).get("gates", []) if g["status"] == "fail"]
    ai = verdict.get("verdict")
    blocking = [f for f in verdict.get("findings", []) if f.get("item") != cid and
                (f.get("needs_human") or (f.get("kind") in BLOCKING_KINDS and f.get("expected_by_pr") is not True))]
    if errors:
        outcome, why = "needs-human", "AI 的结论没有通过核对"
    elif not canary_found:
        outcome, why = "needs-human", "AI 漏掉了预埋的缺陷帧，本次 AI 验收作废"
    elif real_fail:
        outcome = "reject" if ai == "reject" else "needs-human"
        why = f"机器关卡没过：{'、'.join(real_fail)}"
    elif blocking or verdict.get("questions_for_owner") or contradictions:
        outcome = "reject" if ai == "reject" else "needs-human"
        why = "有要人看的发现" if blocking else \
            ("有要你决定的问题" if verdict.get("questions_for_owner") else "AI 两轮的说法对不上")
    else:
        outcome, why = "accept", "机器关卡通过或差异已登记，AI 没有要你看的"
    doc = {"schema": "nexttoonz.accept-check/1", "valid": not errors, "errors": errors, "notes": notes,
           "canary_found": canary_found, "contradictions": contradictions, "ai_verdict": ai,
           "outcome": outcome, "why": why,
           "blocking": [f["rank"] for f in blocking], "real_gates_failed": real_fail}
    jsonout.write(doc, bundle / "accept" / "check.json")
    return doc, (jsonout.OK if not errors and canary_found else jsonout.DIFFERENT)


def main(argv: list[str]) -> int:
    if len(argv) != 1 or argv[0].startswith("-"):
        print(__doc__, file=sys.stderr)
        return jsonout.USAGE
    try:
        doc, code = check(Path(argv[0]))
    except jsonout.NtError as e:
        jsonout.log(f"check_verdict: {e}")
        return e.code
    jsonout.log(f"check_verdict: {'valid' if doc['valid'] else 'INVALID'}, canary "
                f"{'found' if doc['canary_found'] else 'MISSED'}, outcome {doc['outcome']} ({doc['why']})")
    for e in doc["errors"]:
        jsonout.log(f"  - {e}")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
