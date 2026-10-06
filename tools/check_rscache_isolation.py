#!/usr/bin/env python3
"""The rscache library and its tools (cachepack) include nothing from `src/`.

rscache is vendored and its tools are meant to be usable apart from the game, so
the dependency runs one way: the game links rscache, never the reverse. What
crosses between them is data (`fields/<type>.ini`, read by rscache_register) and
an injected binding (rscache_band.h's RSCache_BandBinding), not a header.

This resolves every `#include "..."` under 3rd/rscache against the include
directories rscache's own builds use, and fails if any resolves into the repo's
`src/`, or if an rscache makefile adds an include path into it.

    python3 tools/check_rscache_isolation.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RSCACHE = os.path.join(ROOT, "3rd", "rscache")
GAME_SRC = os.path.join(ROOT, "src") + os.sep
THIRD = os.path.join(ROOT, "3rd")

INCLUDE_DIRS = [
    RSCACHE,
    os.path.join(RSCACHE, "src"),
    os.path.join(RSCACHE, "include"),
    os.path.join(RSCACHE, "test"),
    os.path.join(RSCACHE, "tools", "common"),
    os.path.join(RSCACHE, "tools", "cachepack"),
    os.path.join(RSCACHE, "tools", "port_lostcity"),
    os.path.join(THIRD, "bzip"),
    os.path.join(THIRD, "miniz"),
    os.path.join(THIRD, "xteas"),
    os.path.join(THIRD, "ini"),
    THIRD,
]

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')


def resolve(including_dir, name):
    for base in [including_dir] + INCLUDE_DIRS:
        path = os.path.normpath(os.path.join(base, name))
        if os.path.isfile(path):
            return path
    return None


def main():
    violations = []
    for dirpath, dirnames, filenames in os.walk(RSCACHE):
        dirnames[:] = [d for d in dirnames if d not in ("build", ".git")]
        for filename in filenames:
            path = os.path.join(dirpath, filename)
            if filename.endswith((".c", ".h")):
                with open(path, encoding="latin-1") as f:
                    for number, line in enumerate(f, 1):
                        match = INCLUDE.match(line)
                        if not match:
                            continue
                        target = resolve(dirpath, match.group(1))
                        if target and target.startswith(GAME_SRC):
                            violations.append("%s:%d includes %s" % (
                                os.path.relpath(path, ROOT), number,
                                os.path.relpath(target, ROOT)))
            elif filename.lower() in ("makefile", "makefile.inc") or filename.endswith(".mk"):
                with open(path, encoding="latin-1") as f:
                    for number, line in enumerate(f, 1):
                        for flag in re.findall(r"-I\s*(\S+)", line):
                            resolved = os.path.normpath(os.path.join(dirpath, flag))
                            if resolved.startswith(GAME_SRC) or re.search(r"(^|/)\.\./\.\./src\b", flag):
                                violations.append("%s:%d adds include path %s" % (
                                    os.path.relpath(path, ROOT), number, flag))
    for v in violations:
        print("rscache isolation: " + v)
    print("rscache isolation: %d violation(s)" % len(violations))
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
