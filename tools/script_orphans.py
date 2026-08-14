#!/usr/bin/env python3
"""Name-addressed scripts nothing can reach.

`proc`, `label`, `queue`, `timer`, `softtimer` and `walktrigger` compile with
`lookup_key -1` -- no trigger index entry, so a **call site is the only way in**.
A label nothing calls is dead content that compiles clean, loads clean, and has
no symptom beyond the quest step simply never happening.

This is the one silent-loss class `sscompile` cannot catch on its own, because a
caller may live in any file and "declared but uncalled" is legal for a library
proc. The other three are closed in the compiler:

  duplicate script name       -> hard error (see docs/SCRIPT_NAME_COLLISIONS.md)
  subject of the wrong kind   -> hard error, same file
  stacked headers over a body -> aliased, `alias_script` in ssc_compile.c

What it found when it was written: A Tail of Two Cats had no route to its own
completion step, and nothing in the tree wrote Observatory Quest's
`^itgronigen_complete`.

    tools/script_orphans.py --tree OSRS-Content/osrs239-content
    tools/script_orphans.py --tree OSRS-Content/osrs239-content --all

Two things this has to get right, both of which produced badly wrong numbers
first time round:

  * `[if_button6,bankmain:items] @bank_withdraw_x(last_slot);` puts a real call
    on the SAME line as a header. Dropping whole header lines when scanning for
    callers loses it, and every button-dispatched label then reads as an orphan
    (287 reported, 83 real).
  * `debugproc` is reached by a player typing `::name`, never by a call site, so
    it is uncallable by construction and excluded unless --all.
"""
import argparse
import collections
import glob
import os
import re
import sys

# Name-addressed: no trigger finds these, only a call site.
NAMED = ("proc", "label", "queue", "weakqueue", "timer", "softtimer", "walktrigger")
HEADER = re.compile(r"^\[([a-z_0-9]+)\s*,\s*([^\]\(]+)\]")
# Any identifier, with or without the ~proc / @label sigil.
TOKEN = re.compile(r"[~@]?\b([A-Za-z_][A-Za-z_0-9]*)\b")


def strip_headers(line):
    """Leading `[trigger,subject]` tokens only -- the rest of the line is code."""
    t = line.strip()
    while True:
        m = HEADER.match(t)
        if not m:
            break
        t = t[t.index("]") + 1:].strip()
    return t


def scan(root, include_debugproc=False):
    kinds = NAMED + (("debugproc",) if include_debugproc else ())
    decls, refs = {}, collections.Counter()
    for path in glob.glob(os.path.join(root, "**", "*.rs2"), recursive=True):
        if os.sep + "build" in path:
            continue
        text = open(path, encoding="utf8", errors="replace").read()
        rel = os.path.relpath(path, root)
        for i, line in enumerate(text.split("\n"), 1):
            m = HEADER.match(line.strip())
            if m and m.group(1) in kinds:
                decls.setdefault(m.group(2).strip(), (m.group(1), rel, i))
            for name in TOKEN.findall(strip_headers(line)):
                refs[name] += 1
    return [(k, n, p, l) for n, (k, p, l) in decls.items() if refs[n] == 0], len(decls)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", required=True, help="osrs239-content tree")
    ap.add_argument("--all", action="store_true",
                    help="include debugproc, which is reached by typing ::name")
    ap.add_argument("--fail-on-find", action="store_true",
                    help="exit 1 if any orphan is found (for a CI gate)")
    args = ap.parse_args()

    root = os.path.join(args.tree, "server", "scripts")
    orphans, total = scan(root, args.all)
    print(f"{total} name-addressed scripts; {len(orphans)} with no call site\n")
    for kind, count in collections.Counter(k for k, _, _, _ in orphans).most_common():
        print(f"  {kind:<12} {count}")
    print()
    for kind, name, path, line in sorted(orphans):
        print(f"  [{kind},{name}]\n      {path}:{line}")
    print()
    print("A `proc` under general/scripts/misc/ is usually a LostCity parity helper "
          "kept deliberately uncalled, the same convention doors/scripts/door_procs.rs2 "
          "documents for its mirrored pair. A `label` almost never is.")
    return 1 if (args.fail_on_find and orphans) else 0


if __name__ == "__main__":
    sys.exit(main())
