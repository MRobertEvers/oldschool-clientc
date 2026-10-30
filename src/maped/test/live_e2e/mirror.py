#!/usr/bin/env python3
"""A writable mirror of a READ-ONLY content tree, for the live lane end to end.

    mirror.py <content> <mirror>

The editor server saves into its content tree and `make torirsserver-packs`
writes its allocation ledgers and stamp into it, so the live lane cannot point
either at OSRS-Content (read only by rule, docs/MAPED_IMPLEMENTATION.md
section 0). The mirror is real directories holding a symlink per file for the
trees a build or a save writes into or resolves paths under, and a directory
symlink for the large pure-asset trees nothing writes:

  - a save writes a temporary beside the target and renames it over the
    symlink, so the edited file becomes a real file in the mirror and the
    original is untouched;
  - `pack/` is real COPIES: tools/ss_allocate.py rewrites a ledger with
    open('w'), which would write through a symlink;
  - the pack outputs (`server/scripts/build*`, `server/pack`) are left out:
    the lane's packs live under the live dir;
  - every `*.lock` is left out: the tree's editor-session lock belongs to
    whoever serves OSRS-Content, not to the mirror's editor server.
"""
import os
import shutil
import sys

# Pure inputs nothing in a build or a save writes: one directory symlink each.
LINKED = {
    "animayas", "animsets", "binary", "fonts", "framemaps", "jingles", "models",
    "samples", "songs", "sprites", "synth", "textures", "wiki", "npc_combat",
}
# Copied, not linked (rewritten in place by the allocation).
COPIED = {"pack"}
# Pack outputs of the content tree: the lane has its own.
SKIPPED = {
    os.path.join("server", "pack"),
    os.path.join("server", "scripts", "build"),
}


def skipped(rel):
    # A lock is the holder's, never shared: the tree's .editor-session.lock
    # (ToriRSMapEd's writer lock) and every pack's .pack.lock stay behind.
    if rel.endswith(".lock"):
        return True
    if rel in SKIPPED:
        return True
    head, tail = os.path.split(rel)
    return head == os.path.join("server", "scripts") and tail.startswith("build_")


def mirror_tree(src, dst, rel, copy):
    os.makedirs(dst, exist_ok=True)
    count = 0
    for entry in os.scandir(src):
        sub_rel = os.path.join(rel, entry.name) if rel else entry.name
        target = os.path.join(dst, entry.name)
        if skipped(sub_rel):
            continue
        if entry.is_dir(follow_symlinks=True):
            count += mirror_tree(entry.path, target, sub_rel, copy)
        elif copy:
            shutil.copy2(entry.path, target)
            count += 1
        else:
            os.symlink(entry.path, target)
            count += 1
    return count


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    src = os.path.realpath(sys.argv[1])
    dst = os.path.abspath(sys.argv[2])
    if os.path.exists(dst):
        print("mirror.py: %s exists; remove it first" % dst, file=sys.stderr)
        return 2
    if dst.startswith(src + os.sep):
        print("mirror.py: the mirror may not be inside the tree", file=sys.stderr)
        return 2
    os.makedirs(dst)
    files = 0
    for entry in os.scandir(src):
        target = os.path.join(dst, entry.name)
        if skipped(entry.name):
            continue
        if not entry.is_dir(follow_symlinks=True) or entry.name in LINKED:
            os.symlink(entry.path, target)
        else:
            files += mirror_tree(entry.path, target, entry.name, entry.name in COPIED)
    print("mirror.py: %s -> %s (%d files mirrored)" % (src, dst, files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
