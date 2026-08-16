#!/usr/bin/env python3
"""Prove objverify.py deletes exactly the objects make would wrongly keep.

Every case here is built by hand in a temp dir rather than by running a real
compile: the failure being tested is a timestamp relationship, and a real build
cannot be made to produce one on demand.

Run: python3 tools/test_objverify.py
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OBJVERIFY = os.path.join(HERE, "objverify.py")

failures = []


def check(cond, what):
    if cond:
        print("  ok   %s" % what)
    else:
        print("  FAIL %s" % what)
        failures.append(what)


def run(objdir, sig="sig-v1"):
    r = subprocess.run(
        [sys.executable, OBJVERIFY, objdir, "--sig", sig],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stderr
    return r.stdout


def write(path, text, mtime_ns=None):
    with open(path, "w") as f:
        f.write(text)
    if mtime_ns is not None:
        os.utime(path, ns=(mtime_ns, mtime_ns))


def make_pair(root, objdir, base, src_text, obj_mtime, src_mtime, hdr_text="/*h*/\n",
              hdr_mtime=None):
    """One .c + .h + .o + .d with the timestamps a test wants."""
    src = os.path.join(root, base + ".c")
    hdr = os.path.join(root, base + ".h")
    obj = os.path.join(objdir, base + ".o")
    dep = os.path.join(objdir, base + ".d")
    write(src, src_text, src_mtime)
    write(hdr, hdr_text, hdr_mtime if hdr_mtime is not None else src_mtime)
    write(obj, "OBJECT\n", obj_mtime)
    write(dep, "%s: %s \\\n  %s\n\n%s:\n" % (obj, src, hdr, hdr))
    os.utime(obj, ns=(obj_mtime, obj_mtime))
    return src, hdr, obj, dep


SEC = 1000000000


def main():
    with tempfile.TemporaryDirectory() as root:
        objdir = os.path.join(root, "build")
        os.mkdir(objdir)
        base_t = 1700000000 * SEC

        # The bug that started this: source and object stamped the same second
        # by a tree copy. make keeps it; it must not survive.
        _, _, equal_obj, _ = make_pair(
            root, objdir, "equal", "int equal;\n", base_t, base_t)
        # A healthy object, compiled after its sources.
        _, _, fresh_obj, _ = make_pair(
            root, objdir, "fresh", "int fresh;\n", base_t + 5 * SEC, base_t)
        # An object older than its source -- make already rebuilds this one, but
        # it must not be left behind either.
        _, _, older_obj, _ = make_pair(
            root, objdir, "older", "int older;\n", base_t, base_t + 5 * SEC)

        out = run(objdir)
        check(not os.path.exists(equal_obj), "equal mtime -> deleted")
        check(not os.path.exists(older_obj), "object older than source -> deleted")
        check(os.path.exists(fresh_obj), "object newer than sources -> kept")
        check("equal.o" in out and "older.o" in out, "report names what it removed")
        check(os.path.exists(os.path.join(objdir, ".objverify")), "record written")

        # Second run: nothing changed, so nothing more may be removed.
        out = run(objdir)
        check(out == "", "second run on a clean dir is silent")
        check(os.path.exists(fresh_obj), "fresh object survives a second pass")

        # A header edited in place, mtime pushed BACKWARDS -- invisible to make,
        # caught by content.
        hdr = os.path.join(root, "fresh.h")
        write(hdr, "/* edited */\n", base_t - 100 * SEC)
        out = run(objdir)
        check(not os.path.exists(fresh_obj), "header content change -> deleted")
        check("fresh.o" in out, "header change is reported")

        # Flags change -> every object is stale even though sources did not move.
        write(os.path.join(root, "flag.c"), "int flag;\n", base_t)
        write(os.path.join(root, "flag.h"), "/*h*/\n", base_t)
        flag_obj = os.path.join(objdir, "flag.o")
        write(flag_obj, "OBJECT\n", base_t + 5 * SEC)
        write(os.path.join(objdir, "flag.d"),
              "%s: %s %s\n" % (flag_obj, os.path.join(root, "flag.c"),
                               os.path.join(root, "flag.h")))
        os.utime(flag_obj, ns=(base_t + 5 * SEC, base_t + 5 * SEC))
        run(objdir)                      # record it under sig-v1
        check(os.path.exists(flag_obj), "flag object recorded under its signature")
        run(objdir, sig="sig-v2")
        check(not os.path.exists(flag_obj), "compile flags changed -> deleted")

        # A prerequisite that no longer exists. make errors out on this; here it
        # is just stale.
        gone_src = os.path.join(root, "gone.c")
        _, _, gone_obj, _ = make_pair(
            root, objdir, "gone", "int gone;\n", base_t + 5 * SEC, base_t)
        run(objdir)
        os.unlink(os.path.join(root, "gone.h"))
        run(objdir)
        check(not os.path.exists(gone_obj), "vanished header -> deleted")

        # An object swapped in under a record that already passed, with sources
        # untouched -- what a tree copy does. The record must not cover it.
        _, _, swap_obj, _ = make_pair(
            root, objdir, "swap", "int swap;\n", base_t + 5 * SEC, base_t)
        run(objdir)
        check(os.path.exists(swap_obj), "swap object recorded")
        write(swap_obj, "FOREIGN OBJECT\n", base_t)   # same-second as its source
        run(objdir)
        check(not os.path.exists(swap_obj),
              "object replaced under its own record -> re-tested and deleted")

        # No .d at all: cannot be verified, must be left alone rather than
        # deleted every run (that would rebuild it forever).
        nodep_obj = os.path.join(objdir, "vendored.o")
        write(nodep_obj, "OBJECT\n", base_t)
        run(objdir)
        check(os.path.exists(nodep_obj), "object without a .d is left alone")

        # A missing objdir is not an error -- make runs this before mkdir.
        out = run(os.path.join(root, "no-such-dir"))
        check(out == "", "missing objdir is a silent no-op")

    print("test_objverify: %s" % ("FAIL" if failures else "PASS"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
