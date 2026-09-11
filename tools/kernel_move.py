#!/usr/bin/env python3
"""
Apply a kernel rename map: git mv the files, then repoint every #include.

MECHANICAL ON PURPOSE. This does two things and refuses to do a third. It moves
files and it rewrites the include lines that name them. It does not touch a line
of code inside a function, and it does not reformat anything it passes over --
so a commit produced by this script is reviewable by reading the map, and a
bisect that lands on it lands on a tree whose behaviour is identical to its
parent's.

That refusal is the whole point of splitting the rename out of the behaviour
work. A commit that both moves a file and changes it is unreviewable: `git log
--follow` sees a delete and an add, the diff is the entire file twice, and the
one line that mattered is somewhere inside it.

    python3 tools/kernel_move.py MAP.tsv --stage projection          # dry run
    python3 tools/kernel_move.py MAP.tsv --stage proj --apply

The map is old<TAB>new, relative to 3rd/toridraw, as produced by
`tools/kernel_names.py --map`. `--stage` filters to one stage's rows so the
moves land one PR at a time, which is what keeps a failure bisectable to a
family rather than to "the renames".

INCLUDE REWRITING, and why it is quoted-form only. Every include this touches is
`#include "..."` -- the angle-bracket form names system headers and nothing here
is one. A basename is rewritten only where the OLD basename is unambiguous
across the whole map; where two directories hold a file of the same name the
script says so and stops, because guessing which one a relative include meant is
exactly the kind of silent wrong answer this is supposed to avoid.
"""

import argparse
import os
import re
import subprocess
import sys
from collections import defaultdict

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
TORIDRAW = os.path.join(REPO, "3rd", "toridraw")

# Files anywhere in the repo that may name a moved file in an include.
# All of 3rd/, not just toridraw: trspk includes toridraw's projection header,
# and a search scoped to the library being renamed misses exactly the callers a
# rename is most likely to break -- the ones in another component.
#
# NOT v0/ and v1/. Those hold SEPARATE, OLDER COPIES of this library, with their
# own graphics/projection16_simd.u.c and no impl/ directory at all. A basename
# search cannot tell one tree's file from another's, so rewriting their includes
# to this tree's new layout points them at files that do not exist -- and does it
# silently, because nothing in the default build compiles them. Excluded by path,
# not by hoping the pattern misses them.
SEARCH_ROOTS = [os.path.join(REPO, "3rd"),
                os.path.join(REPO, "src"),
                os.path.join(REPO, "tools")]
SEARCH_EXT = (".c", ".h", ".u.c", ".inc", ".S", ".cpp")


def load_map(path, stage_filter, stage_of):
    rows = []
    with open(path) as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            old, new = line.split("\t")
            if stage_filter and stage_of(old, new) != stage_filter:
                continue
            rows.append((old, new))
    return rows


def stage_of(old, new):
    if "/projection" in new or new.startswith("bench/projection"):
        return "projection"
    if "/facesort" in new:
        return "facesort"
    if "/raster" in new:
        return "raster"
    return "other"


def iter_sources():
    for root in SEARCH_ROOTS:
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in ("build", "build_opt",
                                                            "build_asan", ".git")]
            for fn in filenames:
                if fn.endswith(SEARCH_EXT):
                    yield os.path.join(dirpath, fn)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("map")
    ap.add_argument("--stage", choices=("projection", "facesort", "raster"),
                    help="only move this stage's rows")
    ap.add_argument("--apply", action="store_true", help="actually move and rewrite")
    ap.add_argument("--includes-only", action="store_true",
                    help="skip the moves; only repoint includes (for a re-sweep "
                         "after the search roots widen)")
    args = ap.parse_args()

    rows = load_map(args.map, args.stage, stage_of)
    if not rows:
        print("no rows for that stage", file=sys.stderr)
        return 1

    # Basename -> the new path(s) it maps to. Ambiguity is a hard stop.
    by_base = defaultdict(set)
    for old, new in rows:
        by_base[os.path.basename(old)].add(new)
    ambiguous = {b: v for b, v in by_base.items() if len(v) > 1}
    if ambiguous:
        for b, v in sorted(ambiguous.items()):
            print("AMBIGUOUS basename %s -> %s" % (b, sorted(v)), file=sys.stderr)
        print("refusing to guess which one a relative include meant", file=sys.stderr)
        return 1

    print("%d file(s) to move%s" % (len(rows), "" if args.apply else "  [DRY RUN]"))

    # 1. the moves
    for old, new in rows if not args.includes_only else []:
        src = os.path.join(TORIDRAW, old)
        dst = os.path.join(TORIDRAW, new)
        if not os.path.exists(src):
            print("  MISSING %s" % old, file=sys.stderr)
            return 1
        print("  %s -> %s" % (old, new))
        if args.apply:
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            subprocess.check_call(["git", "mv", src, dst], cwd=REPO)

    # 2. the include rewrites
    #
    # An include may name the file by basename (a sibling include) or by a path
    # relative to 3rd/toridraw (the unity list does this). Both forms are
    # rewritten to the path-relative form of the NEW location, which is the one
    # that keeps working from any directory.
    patterns = []
    for old, new in rows:
        base = os.path.basename(old)
        patterns.append((re.compile(r'(#include\s+")([^"]*/)?%s(")' % re.escape(base)),
                         new))

    touched = 0
    for path in iter_sources():
        try:
            with open(path, encoding="utf-8", newline="") as fh:
                text = fh.read()
        except (UnicodeDecodeError, OSError):
            continue
        original = text
        for rx, new in patterns:
            text = rx.sub(lambda m, n=new: m.group(1) + n + m.group(3), text)
        if text != original:
            touched += 1
            rel = os.path.relpath(path, REPO)
            print("  include: %s" % rel)
            if args.apply:
                # newline="" throughout: some files in this tree have CRLF or
                # stranger line endings, and normalising them would turn a
                # rename into a whole-file diff.
                with open(path, "w", encoding="utf-8", newline="") as fh:
                    fh.write(text)

    # 3. the moved files' OWN relative includes
    #
    # A file that lived in graphics/ and included "dash_faceint.h" was naming a
    # SIBLING. Moved to impl/projection/ it is no longer a sibling, and the
    # include stops resolving -- with an error that names the header rather than
    # the move that broke it.
    #
    # Every quoted include a moved file makes is therefore re-resolved: if the
    # target existed next to the file's OLD location, it is rewritten to the
    # path-relative form, which keeps working wherever the file ends up. This is
    # the half of a rename that `git mv` cannot do for you.
    repaired = 0
    for old, new in rows:
        dst = os.path.join(TORIDRAW, new)
        if not (args.apply and os.path.exists(dst)):
            continue
        old_dir = os.path.dirname(old)
        with open(dst, encoding="utf-8", newline="") as fh:
            text = fh.read()
        before = text

        def fix(m, old_dir=old_dir):
            target = m.group(2)
            # ANY form that resolved against the old directory, not just a bare
            # basename: triangles/*.u.c reached the library root with "../x.h",
            # and a repair that only understood siblings left every one of them
            # pointing one level above the new home.
            #
            # The test is resolution, not spelling. If the target exists
            # relative to where the file USED to be, it was a relative include
            # and is rewritten to the root-relative form. If it does not, it was
            # already root-relative (the -I path finds it) and is left alone --
            # which is why "graphics/winding.h" survives untouched from a file
            # that was in graphics/raster/flat/.
            resolved = os.path.normpath(os.path.join(TORIDRAW, old_dir, target))
            if not os.path.exists(resolved):
                return m.group(0)
            rel = os.path.relpath(resolved, TORIDRAW)
            if rel == target:
                return m.group(0)          # already the form we would write
            return "%s%s%s" % (m.group(1), rel, m.group(3))

        text = re.sub(r'(#include\s+")([^"]+)(")', fix, text)
        if text != before:
            repaired += 1
            print("  repaired sibling includes: %s" % new)
            with open(dst, "w", encoding="utf-8", newline="") as fh:
                fh.write(text)

    print("%d file(s) had includes rewritten, %d had sibling includes repaired"
          % (touched, repaired))
    if not args.apply:
        print("\ndry run: nothing changed. re-run with --apply")
    return 0


if __name__ == "__main__":
    sys.exit(main())
