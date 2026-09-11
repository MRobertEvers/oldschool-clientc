"""Write a build stamp beside a binary. Never overwrites; always names the commit.

This exists because of a specific, expensive failure. A `git reset` moved a
worktree onto origin/v3 at 09:04 on 2026-08-24. Binaries built before it and
after it sat in the same directory under similar names, and a day of ablation
ladders was quoted across the boundary before anyone noticed the frame time had
gone from 39 ms to 98. Nothing about a 5.6 MB exe tells you which tree it came
from, so the answer has to be written down at the moment it is still known.

    python tools/buildstamp.py src/torirs-ship.exe build_win32_ship

writes `src/torirs-ship.exe.<commit8>.<utc>.build.txt`. The name carries the
commit and the time, so a second build of the same exe never lands on the first
one's stamp -- the directory accumulates the history instead of losing it. Pass
--note to record what the build was for.

What goes in, and why each line earns its place:

  commit / branch / subject   The question that started this.
  dirty + tree-hash           A commit is not enough: this repo builds from the
                              working tree, and two builds at the same commit
                              with different uncommitted edits are different
                              binaries. The hash of `git diff HEAD` distinguishes
                              them, and matching hashes prove two stamps really
                              are the same source.
  suspect commits             Named ancestry checks against HEAD. Read the
                              warning on them: ancestry describes the LABEL,
                              not the files. A mixed reset moves HEAD and
                              leaves every source file untouched, and then
                              "CONTAINS <slow commit>" is printed over a binary
                              that has none of it. That happened here on
                              2026-08-24 and cost most of a day.
  source identity             The fix for the above, and the line to trust:
                              the hot files are hashed as they sit on disk and
                              matched against the blob at each named commit.
                              "raster == 346819418" is a statement about what
                              the compiler read. Ancestry cannot make that
                              statement and should not be read as if it does.
  exe sha256 + size           Identity that survives being copied to another
                              machine under another name -- which is exactly
                              what happens to these, and where the confusion
                              started.
  objdir asm objects          The probe gate in src/makefile drops the handrolled
                              kernels from any build defining TORIDRAW_ABLATE,
                              TORIDRAW_SPAN_CENSUS or TORIDRAW_SPAN_TRACE. A
                              build missing gouraud_tri_asm.o measures the C
                              reference path, and its numbers are not shipping
                              numbers. Listing what actually linked makes that
                              visible instead of remembered.
  env                         The -D flags and OPT level that produced it.
"""

from __future__ import print_function

import hashlib
import os
import subprocess
import sys
import time

# Commits whose presence or absence changes what is being measured. Ancestry is
# cheap to test and worth testing by name -- "is 35afe10da in here?" answers in
# one line what a frame-time bisect answers in an afternoon.
SUSPECTS = [
    ("35afe10da", "v3 runtime-selectable raster kernels (~2.5x slower on XP)"),
    ("af4a8a7c9", "v3 split/streamline raster kernels"),
    ("6e393e969", "v3 specialize raster kernel passes"),
    ("026533702", "handrolled texture span kernel"),
    ("a933b37e5", "arm64 asm projection kernel"),
]

ASM_OBJECTS = ["fb_clear_asm.o", "gouraud_tri_asm.o",
               "tex_span_asm.o", "tex_tri_asm.o"]

# The files whose contents decide what the frame time will be. Each is hashed
# as it sits on disk and matched against the same path at each reference
# commit, so the stamp reports the renderer that actually compiled rather than
# whatever branch label HEAD happens to be carrying.
SOURCE_PROBES = [
    "3rd/toridraw/toridraw_raster.u.c",
    "3rd/toridraw/toridraw_render_hd.u.c",
    "3rd/toridraw/triangles/toridraw_triangle_gouraud.u.c",
    "src/painters/scene_occluders.c",
    "src/platform/platform_sdl2_renderer_soft3d.c",
]

# Trees worth recognising by name when a probe matches one exactly.
SOURCE_REFS = [
    ("346819418", "pre-v3 tuned (the 35.87 ms tree)"),
    ("7e2dab5af", "v3 merge"),
    ("f1655db18", "perf/xp-raster-setup"),
]

ENV_KEYS = ["TORIDRAW_PROBE_CFLAGS", "PLATFORM", "OPT", "CC", "OBJ_DIR",
            "BUILD_DIAGNOSTIC_CFLAGS"]


def git(repo, args):
    """Return git's stdout, or None -- a stamp is still worth writing without it."""
    try:
        p = subprocess.Popen(["git"] + args, cwd=repo, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        out, _ = p.communicate()
        if p.returncode != 0:
            return None
        return out.decode("latin-1", "replace").strip()
    except OSError:
        return None


def sha256_file(path):
    h = hashlib.sha256()
    f = open(path, "rb")
    try:
        while True:
            chunk = f.read(1 << 20)
            if not chunk:
                break
            h.update(chunk)
    finally:
        f.close()
    return h.hexdigest()


def describe_tree(repo, lines):
    commit = git(repo, ["rev-parse", "HEAD"])
    if commit is None:
        lines.append("commit       <not a git checkout>")
        return "nogit"

    lines.append("commit       %s" % commit)
    lines.append("branch       %s" % (git(repo, ["rev-parse", "--abbrev-ref", "HEAD"]) or "?"))
    lines.append("subject      %s" % (git(repo, ["log", "-1", "--format=%s"]) or "?"))
    lines.append("committed    %s" % (git(repo, ["log", "-1", "--format=%cI"]) or "?"))

    # A commit alone does not identify a build here: the tree is what compiles.
    diff = git(repo, ["diff", "HEAD"])
    if diff is None:
        lines.append("tree         <diff unavailable>")
    elif diff == "":
        lines.append("tree         clean (== commit)")
    else:
        digest = hashlib.sha256(diff.encode("latin-1", "replace")).hexdigest()
        files = git(repo, ["diff", "--name-only", "HEAD"]) or ""
        n = len([x for x in files.split("\n") if x])
        lines.append("tree         DIRTY  %d file(s), diff sha256 %s" % (n, digest[:16]))
        lines.append("             (two builds at one commit differ if these differ)")

    lines.append("")
    lines.append("ancestry  (of HEAD's LABEL -- see 'source' below for the")
    lines.append("           files that actually compiled; a mixed reset moves")
    lines.append("           HEAD without touching one line of source)")
    for sha, what in SUSPECTS:
        if git(repo, ["cat-file", "-e", sha + "^{commit}"]) is None:
            state = "unknown"
        else:
            p = subprocess.Popen(["git", "merge-base", "--is-ancestor", sha, "HEAD"],
                                 cwd=repo, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)
            p.communicate()
            state = "CONTAINS" if p.returncode == 0 else "clean of"
        lines.append("  %-8s %s  %s" % (state, sha, what))
    return commit[:8]


def describe_source(repo, lines):
    """Name the tree each hot file actually came from, by content.

    `git hash-object` applies the same clean filter git would, so a file
    checked out from commit X hashes to X's blob for that path even in a
    CRLF working tree. An exact match therefore names the source; anything
    else is a local edit and says so.
    """
    lines.append("")
    lines.append("source     (hashed from disk -- this is what compiled)")
    for path in SOURCE_PROBES:
        full = os.path.join(repo, path)
        if not os.path.isfile(full):
            lines.append("  %-46s MISSING" % path)
            continue
        mine = git(repo, ["hash-object", "--", path])
        if mine is None:
            lines.append("  %-46s <unhashable>" % path)
            continue
        where = None
        for sha, what in SOURCE_REFS:
            blob = git(repo, ["rev-parse", "%s:%s" % (sha, path)])
            if blob is not None and blob == mine:
                where = "== %s  %s" % (sha, what)
                break
        if where is None:
            # Not any reference tree verbatim. Report the closest one by
            # changed-line count, so "edited on top of the good tree" is
            # distinguishable from "a different renderer entirely".
            best = None
            for sha, what in SOURCE_REFS:
                d = git(repo, ["diff", "--numstat", sha, "--", path])
                if not d:
                    continue
                parts = d.split("\n")[0].split("\t")
                try:
                    n = int(parts[0]) + int(parts[1])
                except (ValueError, IndexError):
                    continue
                if best is None or n < best[0]:
                    best = (n, sha, what)
            if best is None:
                where = "EDITED, no reference to compare"
            else:
                where = "EDITED  %+d lines vs %s (%s)" % (best[0], best[1],
                                                          best[2])
        lines.append("  %-46s %s" % (path, where))


def describe_objdir(objdir, lines):
    lines.append("")
    if not objdir:
        lines.append("objects      <no objdir given -- asm linkage unknown>")
        return
    lines.append("objects      %s" % objdir)
    if not os.path.isdir(objdir):
        lines.append("  MISSING -- cannot say which kernels linked")
        return
    for name in ASM_OBJECTS:
        path = os.path.join(objdir, name)
        if os.path.isfile(path):
            lines.append("  linked   %-20s %d B" % (name, os.path.getsize(path)))
        else:
            lines.append("  ABSENT   %-20s (C reference path)" % name)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2

    exe = os.path.abspath(argv[1])
    assert os.path.isfile(exe), exe

    objdir = None
    note = None
    rest = argv[2:]
    i = 0
    while i < len(rest):
        if rest[i] == "--note" and i + 1 < len(rest):
            note = rest[i + 1]
            i += 2
            continue
        if objdir is None:
            objdir = os.path.abspath(rest[i])
        i += 1

    repo = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(repo)

    lines = []
    lines.append("build stamp for %s" % os.path.basename(exe))
    lines.append("=" * 60)
    if note:
        lines.append("note         %s" % note)
    lines.append("stamped      %s UTC" % time.strftime("%Y-%m-%dT%H:%M:%S",
                                                       time.gmtime()))
    lines.append("")
    short = describe_tree(repo, lines)
    describe_source(repo, lines)
    describe_objdir(objdir, lines)

    lines.append("")
    lines.append("binary")
    lines.append("  path       %s" % exe)
    lines.append("  size       %d B" % os.path.getsize(exe))
    lines.append("  sha256     %s" % sha256_file(exe))
    lines.append("  built      %s" % time.strftime(
        "%Y-%m-%dT%H:%M:%S", time.localtime(os.path.getmtime(exe))))

    lines.append("")
    lines.append("environment")
    any_env = False
    for k in ENV_KEYS:
        v = os.environ.get(k)
        if v:
            lines.append("  %-24s %s" % (k, v))
            any_env = True
    if not any_env:
        lines.append("  (none of %s set)" % ", ".join(ENV_KEYS))

    stamp = "%s.%s.%s.build.txt" % (
        exe, short, time.strftime("%Y%m%dT%H%M%S", time.gmtime()))

    # Never overwrite. The name already carries commit and second; a collision
    # means two builds in one second, so step aside rather than clobber.
    n = 0
    path = stamp
    while os.path.exists(path):
        n += 1
        path = "%s.%d" % (stamp, n)

    f = open(path, "wb")
    try:
        f.write(("\n".join(lines) + "\n").encode("latin-1", "replace"))
    finally:
        f.close()

    print("\n".join(lines))
    print("")
    print("-> %s" % path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
