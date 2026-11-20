#!/usr/bin/env python3
"""Render the one-page brief for a person from a checked acceptance.

    brief.py <bundle> [--template FILE]

doc/nexttoonz_cli_ci.md, section 4.5. Every number comes from the bundle
(triage.key's real gates and counts, manifest.json) and the outcome from
accept/check.json (check_verdict.py); the AI's words appear only in "请看"
(its findings' claims and what to check) and "需要你决定" (its questions).
The planted defect is left out, also from the AI's questions and lists.
Writes accept/brief.md: at most 15 lines, up to five items to look at,
each one sentence with its evidence (the details stay in verdict.json).
"""

from __future__ import annotations

import argparse
import datetime
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

STATUS = {"pass": "✓", "fail": "✗", "known": "已登记", "skipped": "跳过"}


def _name(side: dict | None) -> str:
    if not side:
        return "?"
    p = side.get("program") or side.get("render_tree") or "?"
    parts = Path(p).parts
    for marker in ("snapshots", "bundles"):
        if marker in parts:
            return parts[parts.index(marker) + 1]
    return Path(p).parent.name if Path(p).name == "tcomposer" else Path(p).name


def _short(text: str, width: int) -> str:
    """The first sentence, at most `width` characters."""
    for stop in ("。", "；", "; "):
        if stop in text[:width]:
            text = text[: text.index(stop)]
            break
    return text if len(text) <= width else text[: width - 1] + "…"


def _gate(g: dict, real: dict) -> str:
    name = {"golden": "golden", "ab": "A/B 逐字节", "unit": "单测"}.get(g["name"], g["name"])
    if g["name"] == "golden" and real.get("golden"):
        gd = real["golden"]
        text = f"golden {gd['passed']}/{gd['scenes']}"
        return text + ("" if g["status"] == "pass" else f"（{STATUS[g['status']]}）")
    if g["name"] == "ab" and real.get("ab"):
        c = real["ab"]
        text = f"{name} {c['identical']}/{c['frames']}"
        if g["status"] == "known":
            text += f"（其余 {c['different']} 帧为已登记差异 {'、'.join(g.get('known', []))}）"
        elif g["status"] == "fail":
            text += f"（{c['different']} 帧不同，{real['counts']['unexplained']} 处未登记）"
        if c.get("different_unstable"):
            text += f"，另 {c['different_unstable']} 帧不确定、不判"
        failed = c.get("scenes_failed_in_b", 0) + c.get("scenes_failed_in_a", 0)
        if failed:
            text += f"，{failed} 个场景渲染失败"
        return text
    return f"{name} {STATUS.get(g['status'], g['status'])}"


def render(bundle: Path, template: Path) -> str:
    manifest = json.loads((bundle / "manifest.json").read_text())
    key = json.loads((bundle / "triage.key").read_text())
    check = json.loads((bundle / "accept" / "check.json").read_text())
    verdict = json.loads((bundle / "accept" / "verdict.json").read_text())
    real = key["real"]
    cid = (key.get("canary") or {}).get("id")
    findings = [f for f in sorted(verdict["findings"], key=lambda f: f["rank"]) if f["item"] != cid]
    blocking = set(check.get("blocking", []))
    look = [f for f in findings if f["rank"] in blocking]
    lines = []
    for n, f in enumerate(look[:5], 1):
        lines.append(f"{n}. {_short(f['claim'], 90)} → {f['evidence'][0]}")
    if len(look) > 5:
        lines.append(f"（另有 {len(look) - 5} 处，见 accept/verdict.json）")
    outcome = check["outcome"]
    if outcome == "accept":
        conclusion = f"可以合并（{check['why']}）"
    elif outcome == "reject":
        conclusion = f"不建议合并（{check['why']}）"
    else:
        n = f" {len(look)} 处" if look and check["valid"] and check["canary_found"] else ""
        conclusion = f"需要你看{n}（{check['why']}）"
    if not check["valid"]:
        check_line = "注意：AI 的结论没有通过核对（" + "；".join(check["errors"][:2]) + "），以上请看一项不可信"
    elif not check["canary_found"]:
        check_line = "注意：本次 AI 验收作废（漏掉了预埋的缺陷帧），请按机器关卡与差异列表人工判断"
    else:
        check_line = "核对：证据、数字、排序与盲比一致"
    # The planted defect never reaches the brief, not even through the AI's
    # questions or lists.
    canary = key.get("canary") or {}
    marks = [m for m in (canary.get("id"), canary.get("scene")) if m]

    def clean(texts, n, width):
        kept = [t for t in texts or [] if not any(m in t for m in marks)]
        out = [_short(t, width) for t in kept[:n]]
        if len(kept) > n:
            out.append(f"另 {len(kept) - n} 项见 verdict.json")
        return out

    known = "；".join(f"{k['id']} {k['frames']} 帧（{len(k['scenes'])} 个场景，最大 "
                     f"{k['largest_fraction_over_tolerance'] * 100:.1f}% 超阈）" for k in real["known"]) or "无"
    values = {
        "label": manifest.get("label") or manifest.get("suite"),
        "base": _name(manifest.get("base")), "test": _name(manifest.get("test")),
        "platform": f"{manifest['host']['os']} {manifest['host']['arch']}",
        "date": (manifest.get("started") or datetime.date.today().isoformat())[:10],
        "conclusion": conclusion,
        "gates": " · ".join(_gate(g, real) for g in real["gates"]),
        "known": known,
        "look": "\n".join(lines) or "（无）",
        "not_verified": "、".join(clean(verdict.get("not_verified"), 6, 24)) or "（未列）",
        "decide": "；".join(clean(verdict.get("questions_for_owner"), 2, 60)) or "无",
        "check": check_line,
    }
    return template.read_text().format(**values).rstrip() + "\n"


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("bundle", type=Path)
    p.add_argument("--template", type=Path, default=HERE / "brief_template.md")
    a = p.parse_args(argv)
    for f in ("manifest.json", "triage.key", "accept/check.json", "accept/verdict.json"):
        if not (a.bundle / f).is_file():
            print(f"brief: {a.bundle / f} missing (run triage.py and check_verdict.py first)", file=sys.stderr)
            return 3
    text = render(a.bundle, a.template)
    (a.bundle / "accept" / "brief.md").write_text(text)
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
