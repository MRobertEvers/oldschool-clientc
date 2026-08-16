#!/usr/bin/env python3
"""Delete object files whose contents no longer match the sources they claim.

Make decides "up to date" by comparing mtimes, and mtimes in this tree are not
trustworthy. A tree copy -- rsync, tar, a worktree restore, an agent's isolated
checkout being synced back -- rewrites sources AND objects with whole-second
timestamps taken from the copy, not from the compile. An object can then end up
with an mtime greater than or equal to its source while holding pre-copy code,
and make will never rebuild it. Nothing warns; the stale object links, and the
binary runs code that is not in the tree.

That is not theoretical. On 2026-08-15 `run-live.sh` aborted inside
ToriRS_ModelAssertPnmTextureInvariant on an assert the source on disk cannot
reach -- src/build_asan_opt_asan_ubsan_nosimd_es/torirs_types.o predated the
commit that added the render-type gate, while every neighbouring object in the
same directory was current. The tell was a backtrace line number that pointed at
a different statement than the file on disk.

What this does, run before make evaluates anything:

  * Reads each object's .d file (written by -MMD) for its full prerequisite set,
    headers included.
  * Hashes that set, plus the compiler and flag signature, and compares against
    the digest recorded for that object in <objdir>/.objverify.
  * Digest differs, or a prerequisite has vanished  -> delete the .o (and .d) so
    make must rebuild it.
  * No usable record -> fall back to the invariant make itself relies on, but
    strictly and at nanosecond resolution: the object must be NEWER than every
    prerequisite. Equal counts as stale, which is the case make gets wrong.
  * Anything that passes gets its digest recorded, so from then on the check is
    by content and the timestamps stop mattering.

Each record also carries the object's own size and mtime, so a record only
speaks for the exact object that earned it. An .o swapped in underneath one --
what a tree copy does -- falls back to the strict mtime test rather than
inheriting the old verdict.

An object with no .d cannot be verified and is left alone (only tommath.o, which
is vendored and compiled without -MMD); it is reported in the summary so it is
never a silent exemption.

What it cannot catch: an object compiled from content that was overwritten
while the compiler was reading it, and written out afterwards, so that it is
genuinely newer than a source it does not match. Nothing outside the compile
step can see that. Recording the digest inside every compile recipe would, at
the price of a hash per object per build.

Usage:
    objverify.py <objdir> [--sig <string>] [--quiet]

Prints one line naming what it removed, or nothing at all when the directory is
clean, so it can be wired straight into a makefile's $(info).
"""

import hashlib
import os
import sys

STAMP_NAME = ".objverify"


def parse_depfile(path):
    r"""Prerequisites listed in a .d, or None if it cannot be read.

    -MMD writes `foo.o: foo.c bar.h \` with continuations, then -MP appends a
    phony `bar.h:` rule per header. Only the first rule carries prerequisites.
    """
    try:
        with open(path, "r", errors="replace") as f:
            text = f.read()
    except OSError:
        return None

    text = text.replace("\\\n", " ")
    first = None
    for line in text.splitlines():
        if ":" in line:
            first = line
            break
    if first is None:
        return None

    # Split on the colon that ends the target, not one inside a path.
    _, _, rhs = first.partition(":")

    # Unescape "\ " (a space inside a path) without losing ordinary separators.
    prereqs = []
    for token in rhs.replace("\\ ", "\x00").split():
        prereqs.append(token.replace("\x00", " "))
    return prereqs


def file_digest(path, cache):
    """sha256 of a file's contents, or None when it does not exist."""
    if path in cache:
        return cache[path]
    try:
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        digest = h.hexdigest()
    except OSError:
        digest = None
    cache[path] = digest
    return digest


def read_stamps(path):
    """name -> (prereq digest, object size, object mtime_ns)."""
    stamps = {}
    try:
        with open(path, "r") as f:
            for line in f:
                fields = line.strip().split(" ", 3)
                if len(fields) != 4:
                    continue
                digest, size, mtime, name = fields
                try:
                    stamps[name] = (digest, int(size), int(mtime))
                except ValueError:
                    continue
    except OSError:
        pass
    return stamps


def write_stamps(path, stamps):
    tmp = path + ".tmp"
    try:
        with open(tmp, "w") as f:
            for name in sorted(stamps):
                digest, size, mtime = stamps[name]
                f.write("%s %d %d %s\n" % (digest, size, mtime, name))
        os.replace(tmp, path)
    except OSError:
        # A read-only or racing objdir must not fail the build; the worst case
        # is that the next run falls back to the strict mtime check.
        try:
            os.unlink(tmp)
        except OSError:
            pass


def main(argv):
    if len(argv) < 2:
        sys.stderr.write("usage: objverify.py <objdir> [--sig <string>]\n")
        return 2

    objdir = argv[1]
    sig = ""
    quiet = False
    i = 2
    while i < len(argv):
        if argv[i] == "--sig" and i + 1 < len(argv):
            sig = argv[i + 1]
            i += 2
        elif argv[i] == "--quiet":
            quiet = True
            i += 1
        else:
            i += 1

    if not os.path.isdir(objdir):
        return 0

    stamp_path = os.path.join(objdir, STAMP_NAME)
    stamps = read_stamps(stamp_path)
    content_cache = {}

    removed = []
    unverifiable = []
    changed = False

    for name in sorted(os.listdir(objdir)):
        if not name.endswith(".o"):
            continue
        obj = os.path.join(objdir, name)
        dep = os.path.join(objdir, name[:-2] + ".d")

        prereqs = parse_depfile(dep)
        if prereqs is None:
            unverifiable.append(name)
            continue

        try:
            st = os.stat(obj)
        except OSError:
            continue
        identity = (st.st_size, st.st_mtime_ns)

        h = hashlib.sha256()
        h.update(sig.encode("utf-8", "replace"))
        h.update(b"\0")
        missing = False
        for p in sorted(set(prereqs)):
            d = file_digest(p, content_cache)
            if d is None:
                missing = True
                d = "<missing>"
            h.update(p.encode("utf-8", "replace"))
            h.update(b"\0")
            h.update(d.encode("ascii"))
            h.update(b"\0")
        digest = h.hexdigest()

        recorded = stamps.get(name)
        # A record speaks only for the object it was written for. If the .o has
        # been replaced since, the verdict has to be earned again.
        speaks_for_this_object = recorded is not None and recorded[1:] == identity

        if missing:
            stale = True
        elif speaks_for_this_object:
            stale = recorded[0] != digest
        else:
            # Nothing usable recorded: demand what make should have demanded.
            # Equal mtimes are the failure this whole file exists for, so `>`
            # and not `>=`.
            stale = False
            for p in set(prereqs):
                try:
                    if os.stat(p).st_mtime_ns >= identity[1]:
                        stale = True
                        break
                except OSError:
                    stale = True
                    break

        if stale:
            try:
                os.unlink(obj)
            except OSError:
                continue
            try:
                os.unlink(dep)
            except OSError:
                pass
            stamps.pop(name, None)
            removed.append(name)
            changed = True
        elif recorded != (digest,) + identity:
            stamps[name] = (digest,) + identity
            changed = True

    # Stamps for objects that no longer exist are noise; drop them.
    for name in list(stamps):
        if not os.path.exists(os.path.join(objdir, name)):
            del stamps[name]
            changed = True

    if changed:
        write_stamps(stamp_path, stamps)

    if removed and not quiet:
        shown = ", ".join(removed[:8])
        if len(removed) > 8:
            shown += ", +%d more" % (len(removed) - 8)
        sys.stdout.write(
            "objverify: %s: rebuilding %d stale object(s) make thought were "
            "current: %s\n" % (objdir, len(removed), shown)
        )
    if unverifiable and removed and not quiet:
        sys.stdout.write(
            "objverify: %s: %d object(s) have no .d and were not checked: %s\n"
            % (objdir, len(unverifiable), ", ".join(sorted(unverifiable)))
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
