"""Running the existing tcomposer and tconverter safely, without a desktop.

Every run gets a timeout, keeps out of the macOS Dock
(QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM=1) and works on copies of
the stuff directory and the project in a scratch directory, so nothing is
ever written into the repository (run_golden.py does the same).
"""

from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from . import images

RASTERIZERS = ("default", "skia", "gl")


def _sha256(path: Path) -> str:
    return "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()


def _linked_libraries(binary: Path) -> str:
    tool = ["otool", "-L"] if sys.platform == "darwin" else ["ldd"]
    try:
        return subprocess.run([*tool, str(binary)], capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def build_info(binary: Path) -> dict:
    """What can be told about a build from its files (a partial `probe`).

    `skia` is true when the Skia backend library is next to the program or
    its libraries (the bundle, an install tree, a build tree); in such a
    build the default vector rasteriser is Skia (Phase 1 step 9), otherwise
    it is OpenGL.
    """
    binary = Path(binary).resolve()
    here = binary.parent
    places = [here, here.parent / "Frameworks", here.parent / "lib", here.parent / "lib" / "opentoonz",
              here.parent.parent / "lib"]
    skia = any(any(p.glob("libtnzskia.*")) for p in places if p.is_dir())
    if not skia and "libtnzskia" in _linked_libraries(binary):
        skia = True
    return {
        "program": str(binary),
        "program_sha256": _sha256(binary),
        "skia": skia,
        "default_rasterizer": "skia" if skia else "gl",
    }


def effective_rasterizer(requested: str, build: dict) -> str:
    if not build.get("skia"):
        return "gl"  # a build without Skia has nothing else
    return build["default_rasterizer"] if requested == "default" else requested


class Workspace:
    """Scratch copies of stuff/ and of the project, as the golden tests make."""

    def __init__(self, stuff: Path, project: Path, parent: Path | None = None):
        self.root = Path(tempfile.mkdtemp(prefix="nt_", dir=parent))
        self.stuff = self.root / "stuff"
        self.project = self.root / "project"
        shutil.copytree(stuff, self.stuff, symlinks=True)
        shutil.copytree(project, self.project, symlinks=True)
        # The projects root must exist for TProjectManager to accept the
        # fixture project.
        (self.stuff / "projects").mkdir(exist_ok=True)

    def scene(self, name: str) -> Path:
        return self.project / "scenes" / f"{name}.tnz"

    def close(self) -> None:
        shutil.rmtree(self.root, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def environment(rasterizer: str, xvfb: bool) -> dict:
    env = dict(os.environ)
    env["QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM"] = "1"
    env.pop("NEXTTOONZ_VECTOR_RASTERIZER", None)
    if rasterizer != "default":
        env["NEXTTOONZ_VECTOR_RASTERIZER"] = rasterizer
    if sys.platform.startswith("linux"):
        env.setdefault("QT_QPA_PLATFORM", "offscreen")
        if xvfb:
            env["QT_QPA_PLATFORM"] = "xcb"
            env["LIBGL_ALWAYS_SOFTWARE"] = "1"
            env.setdefault("GALLIUM_DRIVER", "llvmpipe")
    return env


def _tail(text: str, lines: int = 40) -> list[str]:
    return text.splitlines()[-lines:]


def render_scene(tcomposer: Path, ws: Workspace, scene: str, out_dir: Path, *, threads: int = 1,
                 rasterizer: str = "default", frames: tuple[int, int] | None = None,
                 timeout: float = 300, xvfb: bool = False) -> dict:
    """Render one scene of the workspace's project into out_dir.

    Returns the per-scene part of a nexttoonz.render/1 result: the exit
    status, the time, and one record per frame written.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    for old in out_dir.glob(f"{scene}.*.png"):
        old.unlink()
    cmd = [str(tcomposer), str(ws.scene(scene)), "-o", str(out_dir / f"{scene}.png"),
           "-nthreads", str(threads), "-TOONZROOT", str(ws.stuff)]
    if frames:
        cmd += ["-range", str(frames[0]), str(frames[1])]
    if xvfb and sys.platform.startswith("linux"):
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24", *cmd]
    start = time.monotonic()
    rec: dict = {"scene": scene, "threads": threads}
    try:
        proc = subprocess.run(cmd, env=environment(rasterizer, xvfb), cwd=ws.root, timeout=timeout,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        rec["rc"] = proc.returncode
        rec["log_tail"] = _tail(proc.stdout) if proc.returncode != 0 else []
    except subprocess.TimeoutExpired as e:
        rec["rc"] = None
        rec["timed_out"] = True
        out = e.stdout.decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        rec["log_tail"] = _tail(out)
    rec["seconds"] = round(time.monotonic() - start, 2)
    rec["frames"] = [images.frame_record(p) for p in sorted(out_dir.glob(f"{scene}.*.png"))]
    rec["ok"] = rec["rc"] == 0 and bool(rec["frames"])
    return rec


def list_scenes(project: Path) -> list[str]:
    return sorted(p.stem for p in (project / "scenes").glob("*.tnz"))
