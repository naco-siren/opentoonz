"""The JSON envelope and the exit codes of doc/nexttoonz_cli.md, section 3."""

from __future__ import annotations

import json
import platform
import sys
import time
from pathlib import Path

from . import VERSION, paths

# Exit codes, the same for every sub-command.
OK = 0            # done; the comparison, if any, passed
DIFFERENT = 1     # done, but a comparison or an assertion did not pass
USAGE = 2         # bad arguments
INPUT = 3         # a file is missing, a scene does not load, not in a project
ENVIRONMENT = 4   # stuff dir, GL or a window needed and missing, a plugin missing
INTERNAL = 5      # an exception, frames not rendered
TIMEOUT = 124     # --timeout expired


class NtError(Exception):
    """An error with its exit code; sub-commands raise it, main() reports it."""

    def __init__(self, code: int, message: str):
        super().__init__(message)
        self.code = code


def host() -> dict:
    system = platform.system().lower()
    version = platform.mac_ver()[0] if system == "darwin" else platform.release()
    return {"os": f"{system}-{version}", "arch": platform.machine(), "python": platform.python_version()}


def tool(build: dict | None = None) -> dict:
    t = {"name": "nexttoonz-py", "version": VERSION, "git": paths.git_head()}
    if build is not None:
        t["build"] = build
    return t


class Timer:
    def __init__(self):
        self.marks: dict[str, int] = {}
        self._start = time.monotonic()

    def mark(self, name: str) -> None:
        now = time.monotonic()
        self.marks[name] = int(round((now - self._start) * 1000))
        self._start = now


def envelope(schema: str, ok: bool, result, *, build: dict | None = None, warnings=None, timing=None) -> dict:
    doc = {"schema": schema, "tool": tool(build), "host": host(), "ok": bool(ok), "result": result,
           "warnings": list(warnings or [])}
    if timing is not None:
        doc["timing_ms"] = timing.marks if isinstance(timing, Timer) else timing
    return doc


def write(doc, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=1, sort_keys=False) + "\n")


def emit(doc) -> None:
    """The one JSON document on standard output."""
    sys.stdout.write(json.dumps(doc, indent=1) + "\n")
    sys.stdout.flush()


def log(*args) -> None:
    """Everything for humans goes to standard error."""
    print(*args, file=sys.stderr, flush=True)
