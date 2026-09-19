#!/usr/bin/env python3
"""Every selftest stanza must actually run somewhere.

Written after a false pass that cost real confidence: `selftest_corp_core`
reported green on its first run and then SURVIVED a mutation that should have
failed it. The stanza was not in the binary at all -- the edit registering it in
the self-test's table had been clobbered between the write and the build.

An unregistered stanza cannot fail, so it reads exactly like a passing one.
Neither existing guard catches it:

  * the C loop's `SELFTEST_CHECK(script != NULL, ...)` fires only for a proc
    that IS in the table but missing from the compiled pack -- the opposite
    direction;
  * `tools/trail_selftest_check.sh` asks whether any FAIL line matches an owned
    expectation, and a stanza that never ran emits no line at all.

So the check has to run from the outside: enumerate every `[proc,selftest_*]`
in the content tree and require each one to be reachable. A stanza is reachable
when it is either

  1. named in a registration table in src/torirsserver/*.c, or
  2. called with `~name` by another script (it is a helper, not a row).

Anything else is dead: compiled, shipped, and never executed.

There is a second shape of the same hole, found while reconciling the Tormented
Demons: a file whose assertions live behind `[debugproc,name]` and are driven
from C by `ToriRSServer_ScriptsRunDebugproc(srv, "name")`. Those DO run -- but only
when C names them, and C names some and not others. `coxrun`, `fishingrun` and
`fletchingrun` are driven; `runecraftrun` and `miningrun` were not, and their 24
and 13 assertions had never executed in a build.

So debugprocs carrying assertions are checked too, against the bare-name form C
actually uses. Note the first version of this check looked for the bracketed
`"[debugproc,name]"` spelling and reported 124 unrun files, most of which were
fine -- the CoX suite among them. Matching the wrong string reads as a much
bigger problem than exists.

Usage:
    tools/check_selftest_registration.py [--check]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(ROOT, "OSRS-Content", "osrs239-content", "server", "scripts")
# The C server tree was renamed from src/net/mock to src/torirsserver on
# 2026-08-20. Both are checked so the tool works either side of the rename and
# in a worktree that predates it.
CSRC_CANDIDATES = [
    os.path.join(ROOT, "src", "torirsserver"),
    os.path.join(ROOT, "src", "net", "mock"),
]
CSRC = next((d for d in CSRC_CANDIDATES if os.path.isdir(d)),
            CSRC_CANDIDATES[0])

# Lanes that are compiled off carry stanzas that legitimately never run. The
# lane list is printed by sscompile; keeping the names here rather than parsing
# its output means this tool still works when the compiler has not been run.
DISABLED_LANE_DIRS = ("ported_rs558_ancient_curses", "ported_scape2009_summoning",
                      "rs558_ancient_curses", "scape2009_summoning")

# Known-dead stanzas, recorded rather than deleted. Each line says who should
# decide. Removing a name from this list without either registering the stanza
# or deleting it will fail the check, which is the point.
KNOWN_DEAD = {
    "selftest_npc_mode_none":
        "selftest.rs2 -- writes no %mock_quest_progress at all, so it has no "
        "success code to register against. It predates the k_* tables and "
        "`defaultmode=none` is covered by defaultmode-none-was-a-noop.md; "
        "someone should decide whether to give it a code or delete it",
}


def rs2_files():
    for base, _dirs, files in os.walk(CONTENT):
        if os.path.basename(base) == "build":
            continue
        for name in files:
            if name.endswith(".rs2"):
                yield os.path.join(base, name)


def read_or_none(path):
    """Read a script, or return None if the tree cannot hand it to us.

    This gates the build, so it must not become the reason nobody can compile.
    A dangling or looping symlink in the content tree is a real thing that
    happens while another session is moving files around -- it cost a build
    here, raising `OSError: Too many levels of symbolic links` out of a tool
    whose whole job is to be a cheap precondition. Unreadable files are
    reported and skipped, never fatal.
    """
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError as exc:
        UNREADABLE.append((path, exc.strerror or str(exc)))
        return None


UNREADABLE = []


def in_disabled_lane(path):
    """True when path is inside a lane excluded from the stock script pack."""
    return any(part in DISABLED_LANE_DIRS
               for part in os.path.normpath(path).split(os.sep))


def main():
    defined = {}
    called = set()
    for path in rs2_files():
        text = read_or_none(path)
        if text is None:
            continue
        for name in re.findall(r"^\[proc,(selftest_[a-z0-9_]+)\]", text, re.M):
            defined[name] = path
        called.update(re.findall(r"~(selftest_[a-z0-9_]+)\b", text))

    registered = set()
    for name in os.listdir(CSRC):
        if not name.endswith(".c"):
            continue
        with open(os.path.join(CSRC, name), encoding="utf-8",
                  errors="replace") as handle:
            registered.update(
                re.findall(r'"\[proc,(selftest_[a-z0-9_]+)\]"', handle.read()))

    # Second shape: a file whose assertions sit behind [debugproc,name], driven
    # from C by `ToriRSServer_ScriptsRunDebugproc(srv, "name")`. C names some and
    # not others, and the ones it does not name have never run.
    driven = set()
    for name in os.listdir(CSRC):
        if not name.endswith(".c"):
            continue
        with open(os.path.join(CSRC, name), encoding="utf-8",
                  errors="replace") as handle:
            text = handle.read()
        driven.update(re.findall(
            r'run_debugproc\(\s*\w+\s*,\s*"([a-z0-9_]+)"', text))

    undriven = []
    for path in rs2_files():
        text = read_or_none(path)
        if text is None:
            continue
        if "FAIL" not in text:
            continue
        if re.search(r"^\[proc,selftest_", text, re.M):
            continue
        procs = set(re.findall(r"^\[debugproc,([a-z0-9_]+)\]", text, re.M))
        if not procs or (procs & driven):
            continue
        if in_disabled_lane(path):
            continue
        undriven.append((len(re.findall("FAIL", text)), path, sorted(procs)))
    undriven.sort(reverse=True)

    dead = []
    for name, path in sorted(defined.items()):
        if name in registered or name in called:
            continue
        if in_disabled_lane(path):
            continue
        dead.append((name, path))

    unexpected = [(n, p) for n, p in dead if n not in KNOWN_DEAD]
    stale = sorted(set(KNOWN_DEAD) - {n for n, _ in dead})

    print("selftest stanzas: %d defined, %d registered in C, %d called as "
          "helpers" % (len(defined), len(registered & set(defined)),
                       len(called & set(defined))))

    if UNREADABLE:
        print("\n%d file(s) could not be read and were skipped:"
              % len(UNREADABLE))
        for path, why in UNREADABLE[:5]:
            print("    %-64s %s" % (os.path.relpath(path, ROOT), why))

    if stale:
        print("\nThese are listed as known-dead but now run. Remove them from "
              "KNOWN_DEAD:")
        for name in stale:
            print("    %s" % name)

    if unexpected:
        print("\nThese stanzas run NOWHERE -- not registered, not called:")
        for name, path in unexpected:
            print("    %-52s %s" % (name, os.path.relpath(path, ROOT)))
        print("\nA stanza that never runs cannot fail, so it reads exactly "
              "like a passing one.\nRegister it in a k_* table in "
              "src/torirsserver/torirs_server_world_selftest.c, or delete it.")

    if dead:
        print("\n%d stanza(s) known dead and recorded:" % len(dead))
        for name, _path in sorted(dead):
            if name in KNOWN_DEAD:
                print("    %-52s %s" % (name, KNOWN_DEAD[name]))

    if undriven:
        print("\n%d file(s) hold FAIL assertions behind a debugproc that C "
              "never names." % len(undriven))
        print("These run only when a person types the command. Not an error "
              "-- some are deliberate\nmanual harnesses -- but each is "
              "coverage no build exercises:")
        for count, path, procs in undriven[:10]:
            print("    %3d assertions  %-64s ::%s"
                  % (count, os.path.relpath(path, ROOT), procs[0]))
        if len(undriven) > 10:
            print("    ... and %d more" % (len(undriven) - 10))

    if unexpected or stale:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
