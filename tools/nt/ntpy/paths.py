"""Where things are: the repository, its fixtures, and the machine-local root.

Nothing is hard-coded to one machine. The repository root is found from this
file's location; the tests directory is looked up both where it is today
(toonz/sources/tests) and where doc/nexttoonz_layout.md (step 7) moves it
(tests/), so the tools keep working across that move. The machine-local root
is local/ in the main checkout (doc/nexttoonz_layout.md, section 4.8), found
through git's common directory so that every worktree shares it, or
$NEXTTOONZ_LOCAL.
"""

from __future__ import annotations

import datetime
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]


def _first(*candidates: Path) -> Path:
    for c in candidates:
        if c.exists():
            return c
    return candidates[0]


def tests_dir() -> Path:
    return _first(REPO / "tests" / "golden", REPO / "toonz" / "sources" / "tests" / "golden").parent


def golden_dir() -> Path:
    return tests_dir() / "golden"


def expected_dir() -> Path:
    return golden_dir() / "expected"


def fixture_project() -> Path:
    return tests_dir() / "fixtures" / "reference_project"


def stuff_dir() -> Path:
    return REPO / "stuff"


def known_differences() -> Path:
    return tests_dir() / "known_differences.yaml"


def schemas_dir() -> Path:
    return REPO / "tools" / "nt" / "schemas"


def local_root() -> Path:
    """local/ of the main checkout, shared by all worktrees of the clone."""
    env = os.environ.get("NEXTTOONZ_LOCAL")
    if env:
        return Path(env)
    try:
        common = subprocess.run(
            ["git", "-C", str(REPO), "rev-parse", "--path-format=absolute", "--git-common-dir"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
        return Path(common).parent / "local"
    except (OSError, subprocess.CalledProcessError):
        return REPO / "local"


def default_run_dir(tool: str, label: str) -> Path:
    """local/runs/<tool>/<date>_<label>/ (doc/nexttoonz_layout.md, section 4.5)."""
    day = datetime.date.today().isoformat()
    return local_root() / "runs" / tool / f"{day}_{label}"


def import_compare_images():
    """The golden tests' comparison module: one definition of the tolerance."""
    sys.path.insert(0, str(golden_dir()))
    try:
        import compare_images  # noqa: WPS433
    finally:
        sys.path.pop(0)
    return compare_images


def git_head() -> str | None:
    try:
        return subprocess.run(
            ["git", "-C", str(REPO), "rev-parse", "--short=9", "HEAD"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None
