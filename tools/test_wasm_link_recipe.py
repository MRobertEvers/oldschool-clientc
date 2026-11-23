#!/usr/bin/env python3
"""Prove a failed web OPT=1 link step leaves no module for the next make to ship.

The web lane's $(TARGET) recipe (src/makefile) links, moves emcc's symbol map
into $(OBJ_DIR), and runs tools/wasm_func_size_check.py. The makefile has no
.DELETE_ON_ERROR, so a step after the link that fails without deleting the
module leaves a complete torirs.js newer than its objects: the next make calls
it up to date and ships a module whose function sizes were never checked.
That happened to the map move: a command-line LDFLAGS drops
--emit-symbol-map, emcc writes no map, `mv` failed, and the module stayed.

This runs the real recipe from the real makefile with a stub compiler, in a
temp dir: OBJS is emptied on the command line, so nothing is compiled and no
emcc is needed. The stub writes the module emcc would (torirs.js plus a
torirs.wasm whose code section has the sizes a case wants) and writes the
symbol map only when --emit-symbol-map is on its command line, as emcc does.

Run: python3 tools/test_wasm_link_recipe.py   (or make -C src test-wasm-link-recipe)
"""

import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), "src")
LAST_FLAVOR = os.path.join(SRC, ".last_flavor")

failures = []

STUB = r'''#!%(python)s -I
import os, sys
args = sys.argv[1:]
if "-E" in args or "-o" not in args:
    sys.exit(0)
out = args[args.index("-o") + 1]

def leb(n):
    b = bytearray()
    while True:
        byte = n & 0x7F
        n >>= 7
        if n:
            b.append(byte | 0x80)
        else:
            b.append(byte)
            return bytes(b)

# One function per name the hot list requires, in its order; STUB_BODY_SIZES
# sizes the first ones and the rest are small.
names = os.environ["STUB_REQUIRED"].split(",")
sizes = [int(s) for s in os.environ["STUB_BODY_SIZES"].split(",")]
sizes += [1000] * (len(names) - len(sizes))
bodies = b"".join(leb(s) + b"\x00" + b"\x01" * (s - 2) + b"\x0b" for s in sizes)
payload = leb(len(sizes)) + bodies
with open(os.path.splitext(out)[0] + ".wasm", "wb") as f:
    f.write(b"\0asm\x01\0\0\0" + b"\x0a" + leb(len(payload)) + payload)
with open(out, "w") as f:
    f.write("// stub module\n")
if "--emit-symbol-map" in args:
    with open(out + ".symbols", "w") as f:
        f.write("".join("%%d:%%s\n" %% (i, n) for i, n in enumerate(names)))
'''


def check(cond, what):
    if cond:
        print("  ok   %s" % what)
    else:
        print("  FAIL %s" % what)
        failures.append(what)


def make(tmp, stub, extra, dry=False):
    env = dict(os.environ)
    # Run from `make test-wasm-link-recipe`, the parent's command-line
    # variables would reach this make through MAKEFLAGS.
    for key in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES"):
        env.pop(key, None)
    target = os.path.join(tmp, "out", "torirs.js")
    cmd = ["make", "-C", SRC] + (["-n"] if dry else []) + [
        "PLATFORM=web", "OPT=1", "OBJS=", "OBJVERIFY_SKIP=1",
        "CC=" + stub, "PLATFORM_CC=" + stub,
        "PLATFORM_OBJ_BASE=" + os.path.join(tmp, "obj"),
        "PLATFORM_TARGET=" + target,
    ] + extra + [target]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    return r.returncode, r.stdout + r.stderr


def main():
    saved_flavor = None
    if os.path.exists(LAST_FLAVOR):
        with open(LAST_FLAVOR) as f:
            saved_flavor = f.read()
    try:
        with tempfile.TemporaryDirectory(prefix="wasm_link_recipe_") as tmp:
            run_cases(tmp)
    finally:
        # The passing link writes src/.last_flavor; put the owner's back.
        if saved_flavor is None:
            if os.path.exists(LAST_FLAVOR):
                os.remove(LAST_FLAVOR)
        else:
            with open(LAST_FLAVOR, "w") as f:
                f.write(saved_flavor)
    if failures:
        print("test-wasm-link-recipe: %d FAILED" % len(failures))
        return 1
    print("test-wasm-link-recipe: all passed")
    return 0


def required_names():
    """The hot list's `required` names: the stub module must carry them all."""
    names = []
    with open(os.path.join(HERE, "wasm_hot_functions.txt")) as f:
        for line in f:
            words = line.split("#")[0].split()
            if len(words) == 2 and words[1] == "required":
                names.append(words[0])
    return names


def run_cases(tmp):
    names = required_names()
    check(names[:1] == ["cs2vm2_run_script_body"], "the hot list requires cs2vm2_run_script_body first")
    os.environ["STUB_REQUIRED"] = ",".join(names)
    stub = os.path.join(tmp, "stub_cc")
    with open(stub, "w") as f:
        f.write(STUB % {"python": sys.executable})
    os.chmod(stub, 0o755)
    target = os.path.join(tmp, "out", "torirs.js")

    code, out = make(tmp, stub, [], dry=True)
    m = re.search(r"mv -f \S+\.symbols (\S+)", out)
    check(code == 0 and m is not None, "the web OPT=1 link moves a symbol map (dry run)")
    if not m:
        print(out[-2000:])
        return
    objmap = m.group(1)
    os.makedirs(os.path.dirname(objmap), exist_ok=True)

    print("case: small module, default flags -> links")
    os.environ["STUB_BODY_SIZES"] = "1000,1000"
    code, out = make(tmp, stub, [])
    check(code == 0, "make succeeds")
    check(os.path.exists(target), "module kept")
    check(os.path.exists(objmap), "symbol map moved into the object directory")
    check(not os.path.exists(target + ".symbols"), "no symbol map left beside the module")
    if code != 0:
        print(out[-2000:])

    # After a passing link src/.last_flavor names this object directory, and
    # with OBJS empty the module would be up to date: remove it so each case
    # below really links.
    def relink():
        if os.path.exists(target):
            os.remove(target)

    print("case: cs2vm2_run_script_body over the limit -> no module")
    relink()
    os.environ["STUB_BODY_SIZES"] = "95000,1000"
    code, out = make(tmp, stub, [])
    check(code != 0, "make fails")
    check(not os.path.exists(target), "module deleted, so the next make relinks")

    print("case: command-line LDFLAGS drops --emit-symbol-map -> no module")
    relink()
    os.environ["STUB_BODY_SIZES"] = "1000,1000"
    code, out = make(tmp, stub, ["LDFLAGS=-O3"])
    check(code != 0, "make fails")
    check(not os.path.exists(target), "module deleted, so the next make relinks")
    check(not os.path.exists(objmap), "the previous link's symbol map is gone")


if __name__ == "__main__":
    sys.exit(main())
