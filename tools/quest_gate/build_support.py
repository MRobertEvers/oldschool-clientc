#!/usr/bin/env python3
"""Shared, safe build helper for tools/quest_gate/*.

Every quest_gate script builds its own binary into its own objdir
(CLAUDE.md: several sessions build from this checkout at once, so nobody may
share one), but this project links roughly 500 objects, so a script that
always starts that objdir from nothing pays several minutes before its first
client run.

conformance.py's original build() refused ever to seed a new objdir from an
existing warm one, and its docstring gave a real reason: a plain `cp -Rp`
preserves mtimes, GNU Make 3.81 on this machine compares whole-SECOND
mtimes, and make could then call a copied object fresh without anything
having checked it still matched its source -- a binary that ran but was
never proven to be what the source next to it said it was.

That hazard is real, but it is no longer unguarded: `src/makefile` now runs
`tools/objverify.py` at parse time, before make evaluates anything, on
EVERY build. objverify hashes each object's full prerequisite set (source
plus every header its .d reached) by CONTENT, not mtime, and deletes any
object whose hash no longer matches what was recorded for it -- which is
exactly the case a copy that lies about mtimes produces. This was verified
here, not assumed: seeding a brand-new objdir from `build_qd_check_opt_es`
(2026-09-19) made objverify report "rebuilding 70 stale object(s) make
thought were current" on the very next `make`, chosen purely by content
hash, before a single line of this module's code ran. So the seeding below
is a plain recursive copy (shutil.copytree, which -- like `cp -Rp` --
preserves mtimes) and nothing more: objverify is what makes that safe, and
it does its own job better than a hand-maintained list of "the files that
change" ever could, because it is right about EVERY object, not just the
ones somebody remembered to name.

If objverify.py is ever removed or its $(shell) hook taken out of
src/makefile, this module's safety argument goes with it -- `rm -rf` the
target objdir and build cold instead; CLAUDE.md's rule is that a build that
disagrees with its source is never to be trusted, and that call is objverify's
alone to make now.
"""

import os
import shutil
import subprocess


def default_run(command, **kwargs):
    print("+ " + " ".join(command), flush=True)
    return subprocess.call(command, **kwargs)


def seed_from_warm(objdir, warm_from):
    """Copy warm_from into objdir, which must not already exist.

    Nothing here decides what is stale -- that is `make`'s own objverify
    pass, which runs unconditionally on the build() call right after this
    and sees the copy for what it is by hashing content, not trusting the
    mtimes this copy preserves.
    """
    assert not os.path.exists(objdir)
    assert os.path.isdir(warm_from)
    shutil.copytree(warm_from, objdir, symlinks=True)


def objdir_path(repo_root, obj_base):
    assert repo_root
    assert obj_base
    return os.path.join(repo_root, "src", "%s_opt_es" % obj_base)


def find_warm_candidate(repo_root, exclude_obj_base=None):
    """The most recently modified sibling `*_opt_es` objdir under src/, to
    seed a new one from.

    Safe no matter which one this picks: make's own objverify pass (see the
    module docstring) is what decides what is actually stale, against the
    CURRENT source, every time. Picking a closer-to-current objdir only
    saves objverify more rebuilding; it can never make a build wrong.
    """
    assert repo_root
    src_dir = os.path.join(repo_root, "src")
    exclude = ("%s_opt_es" % exclude_obj_base) if exclude_obj_base else None
    candidates = []
    try:
        entries = os.listdir(src_dir)
    except OSError:
        return None
    for entry in entries:
        if not entry.endswith("_opt_es") or entry == exclude:
            continue
        path = os.path.join(src_dir, entry)
        if not os.path.isdir(path):
            continue
        try:
            mtime = os.path.getmtime(path)
        except OSError:
            continue
        candidates.append((mtime, path))
    if not candidates:
        return None
    candidates.sort(reverse=True)
    return candidates[0][1]


def build(repo_root, obj_base, target, warm_from=None, run=None):
    """Build `target` (OPT=1 EMBED_SERVER=1) into `obj_base`'s objdir.

    warm_from, when given, is a directory (absolute, or a name resolved
    under src/), or the literal string "auto" to pick the most recently
    modified sibling objdir (find_warm_candidate), used to seed the objdir
    IF IT DOES NOT ALREADY EXIST -- see seed_from_warm and the module
    docstring for why that copy is safe. An objdir that already exists is
    left exactly alone: make's own incremental build (objverify included)
    is already the fast, correct path there.

    Returns (exit_code, binary_path, seeded_from). seeded_from is the warm
    directory actually used, or None if this build was cold.
    """
    assert repo_root
    assert obj_base
    assert target
    run = run or default_run
    src_dir = os.path.join(repo_root, "src")
    objdir = objdir_path(repo_root, obj_base)
    seeded_from = None
    if warm_from == "auto":
        warm_from = find_warm_candidate(repo_root, exclude_obj_base=obj_base)
    if warm_from and not os.path.isdir(objdir):
        warm_path = warm_from if os.path.isabs(warm_from) else os.path.join(src_dir, warm_from)
        if os.path.isdir(warm_path):
            seed_from_warm(objdir, warm_path)
            seeded_from = warm_path
            print("build_support: seeded %s from %s; make's own objverify pass "
                  "decides what is actually stale" % (objdir, warm_path), flush=True)
        else:
            print("build_support: --warm-from %s does not exist, building cold" % warm_path,
                  flush=True)
    command = ["make", "-C", src_dir, "OPT=1", "EMBED_SERVER=1",
               "PLATFORM_OBJ_BASE=%s" % obj_base, "PLATFORM_TARGET=%s" % target, target]
    code = run(command)
    return code, os.path.join(src_dir, target), seeded_from
