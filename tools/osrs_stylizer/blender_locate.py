#!/usr/bin/env python3
"""Finding and invoking headless Blender, shared by every driver script.

Three scripts outside Blender need to launch it — gen_highpoly_corpus.py,
optimize.py, and example_usage.py — and they all face the same two problems:
Blender is not on PATH on a typical Windows box, and it may live inside WSL,
in which case every path handed to the child process must be translated to
/mnt/<drive>/... . Both problems are solved once here.

(modifier.py and render_highpoly_corpus.py run *inside* Blender and must not
import this module — Blender's bundled Python has no access to it.)
"""

import glob
import os
import shutil

# Where a Windows Blender usually lives. The portable-zip location comes first
# because it needs no installer and no admin rights, which is what the README
# recommends for this pipeline.
WINDOWS_BLENDER_GLOBS = [
    os.path.join(os.environ.get("LOCALAPPDATA", ""),
                 "blender-portable", "blender-*", "blender.exe"),
    r"C:\Program Files\Blender Foundation\Blender*\blender.exe",
    r"C:\Program Files (x86)\Blender Foundation\Blender*\blender.exe",
]

NOT_FOUND_HELP = (
    "no Blender found. Pass --blender <path>, set the BLENDER environment "
    "variable, or install it. The no-admin option is the portable zip from "
    "https://www.blender.org/download/ extracted into "
    r"%LOCALAPPDATA%\blender-portable\ , which is searched automatically."
)


def to_wsl_path(win_path: str) -> str:
    """C:\\foo\\bar -> /mnt/c/foo/bar"""
    drive, rest = os.path.splitdrive(os.path.abspath(win_path))
    return f"/mnt/{drive[0].lower()}{rest.replace(os.sep, '/')}"


class Blender:
    """How to invoke headless Blender, and how to spell a path for it.

    Two routes exist because Blender may be a native binary (paths pass
    through untouched) or live inside WSL (every path the child sees must be
    translated). Everything else about a render is identical, so the whole
    difference is confined to this class: callers build their command line as
    `[*blender.launcher, ...]` and wrap every path in `blender.path(...)`.
    """

    def __init__(self, launcher: list[str], translate):
        self.launcher = launcher
        self.path = translate

    @classmethod
    def locate(cls, explicit: str | None = None) -> "Blender":
        """Resolve a Blender to use, most specific source first: an explicit
        argument, $BLENDER, PATH, the known Windows install locations, then
        WSL. Raises SystemExit with instructions if nothing works."""
        if explicit and explicit.lower() == "wsl":
            return cls.wsl()
        for candidate in (explicit, os.environ.get("BLENDER"),
                          shutil.which("blender")):
            if not candidate:
                continue
            resolved = candidate if os.path.isfile(candidate) else shutil.which(candidate)
            if resolved:
                return cls([resolved], os.path.abspath)
        # An explicit path that did not resolve is a typo, not a cue to guess.
        if explicit:
            raise SystemExit(f"--blender: not an executable: {explicit}")
        for pattern in WINDOWS_BLENDER_GLOBS:
            found = sorted(glob.glob(pattern))
            if found:
                return cls([found[-1]], os.path.abspath)  # newest version wins
        if shutil.which("wsl.exe"):
            return cls.wsl()
        raise SystemExit(NOT_FOUND_HELP)

    @classmethod
    def wsl(cls) -> "Blender":
        return cls(["wsl.exe", "-u", "root", "-e", "blender"], to_wsl_path)

    def describe(self) -> str:
        return " ".join(self.launcher)
