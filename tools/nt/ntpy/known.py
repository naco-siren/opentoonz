"""The registry of known differences (tests/known_differences.yaml) and its matcher.

A difference is "known" only when a program says so: an entry matches a
compared frame (or a failed scene) when every condition it states holds and
every measured value is within its bounds. Over a bound, it is not known.
Nobody, the AI acceptance included, decides it by looking.

The file is YAML, read with the small loader below (block mappings and
lists, flow lists, quoted and plain scalars, comments), because neither the
CI images nor the golden tests' Python have PyYAML.
"""

from __future__ import annotations

import fnmatch
import re
from pathlib import Path

# ---------------------------------------------------------------- YAML subset


def _strip_comment(line: str) -> str:
    out, quote = [], None
    for i, ch in enumerate(line):
        if quote:
            if ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
        elif ch == "#" and (i == 0 or line[i - 1] in " \t"):
            break
        out.append(ch)
    return "".join(out).rstrip()


def _scalar(text: str):
    t = text.strip()
    if t == "" or t in ("null", "~"):
        return None
    if t in ("true", "false"):
        return t == "true"
    if t[0] in "\"'" and t[-1] == t[0]:
        return t[1:-1].replace('\\"', '"') if t[0] == '"' else t[1:-1].replace("''", "'")
    if t.startswith("[") and t.endswith("]"):
        inner = t[1:-1].strip()
        if not inner:
            return []
        parts, cur, quote = [], [], None
        for ch in inner:
            if quote:
                cur.append(ch)
                if ch == quote:
                    quote = None
            elif ch in "\"'":
                quote = ch
                cur.append(ch)
            elif ch == ",":
                parts.append("".join(cur))
                cur = []
            else:
                cur.append(ch)
        parts.append("".join(cur))
        return [_scalar(p) for p in parts]
    if re.fullmatch(r"[-+]?\d+", t):
        return int(t)
    if re.fullmatch(r"[-+]?(\d+\.\d*|\.\d+|\d+)([eE][-+]?\d+)?", t):
        return float(t)
    return t


def load_yaml(text: str):
    lines = []
    for raw in text.splitlines():
        if raw.strip().startswith("---"):
            continue
        line = _strip_comment(raw)
        if line.strip():
            if "\t" in line[: len(line) - len(line.lstrip())]:
                raise ValueError(f"tab in indentation: {raw!r}")
            lines.append((len(line) - len(line.lstrip(" ")), line.strip()))
    pos = 0

    def block(indent: int):
        nonlocal pos
        if pos >= len(lines):
            return None
        if lines[pos][1].startswith("- ") or lines[pos][1] == "-":
            out = []
            while pos < len(lines) and lines[pos][0] == indent and (lines[pos][1].startswith("- ") or lines[pos][1] == "-"):
                rest = lines[pos][1][1:].strip()
                if not rest:
                    pos += 1
                    out.append(block(lines[pos][0]) if pos < len(lines) and lines[pos][0] > indent else None)
                elif re.match(r"^[A-Za-z_][\w-]*:(\s|$)", rest):
                    # a mapping that starts on the dash line: re-read it at the
                    # indentation of its first key
                    lines[pos] = (indent + 2, rest)
                    out.append(block(indent + 2))
                else:
                    pos += 1
                    out.append(_scalar(rest))
            return out
        out = {}
        while pos < len(lines) and lines[pos][0] == indent and not lines[pos][1].startswith("- "):
            m = re.match(r"^([A-Za-z_][\w-]*):(?:\s+(.*))?$", lines[pos][1])
            if not m:
                raise ValueError(f"cannot read line: {lines[pos][1]!r}")
            key, rest = m.group(1), m.group(2)
            pos += 1
            if rest is None or rest == "":
                if pos < len(lines) and (lines[pos][0] > indent or
                                         (lines[pos][0] == indent and lines[pos][1].startswith("- "))):
                    out[key] = block(lines[pos][0])
                else:
                    out[key] = None
            else:
                out[key] = _scalar(rest)
        return out

    doc = block(lines[0][0]) if lines else None
    if pos != len(lines):
        raise ValueError(f"unexpected indentation at: {lines[pos][1]!r}")
    return doc


# ---------------------------------------------------------------- registry

METRICS = ("fraction_over_tolerance", "fraction_differing", "peak")


def load(path: Path) -> list[dict]:
    doc = load_yaml(Path(path).read_text())
    entries = (doc or {}).get("entries") or []
    seen = set()
    for e in entries:
        for k in ("id", "title", "applies_to", "reason"):
            if k not in e:
                raise ValueError(f"known difference {e.get('id', '?')}: missing {k}")
        if e["id"] in seen:
            raise ValueError(f"known difference {e['id']} twice")
        seen.add(e["id"])
    return entries


def _listify(v):
    return v if isinstance(v, list) else [v]


def _platform_ok(cond: dict, platform: str | None) -> bool:
    pats = cond.get("platforms")
    return pats is None or (platform is not None and any(fnmatch.fnmatch(platform, p) for p in _listify(pats)))


def matches(entry: dict, item: dict) -> bool:
    """item: {comparison, scene, frame, threads, platform, rasterizers:[a, b],
    same_day, status, metrics:{...}} (absent keys are unknown and fail any
    condition that needs them)."""
    cond = entry["applies_to"]
    if cond.get("comparison") and item.get("comparison") not in _listify(cond["comparison"]):
        return False
    if not any(fnmatch.fnmatch(item.get("scene", ""), p) for p in _listify(cond.get("scenes", "*"))):
        return False
    if cond.get("frames") is not None and item.get("frame") not in _listify(cond["frames"]):
        return False
    if cond.get("threads") is not None and item.get("threads") not in _listify(cond["threads"]):
        return False
    if not _platform_ok(cond, item.get("platform")):
        return False
    if cond.get("rasterizers") is not None:
        want = sorted(_listify(cond["rasterizers"]))
        have = sorted(r for r in item.get("rasterizers") or [] if r)
        if have != want:
            return False
    if cond.get("same_day") is not None and item.get("same_day") is not cond["same_day"]:
        return False
    status = item.get("status", "different")
    if status not in _listify(cond.get("status", "different")):
        return False
    bounds = entry.get("bounds") or {}
    metrics = item.get("metrics") or {}
    for m in METRICS:
        if m in bounds:
            if m not in metrics or metrics[m] is None or metrics[m] > bounds[m]:
                return False
    return True


def first_match(entries: list[dict], item: dict) -> str | None:
    for e in entries:
        if matches(e, item):
            return e["id"]
    return None
