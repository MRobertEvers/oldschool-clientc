"""Rebuild the server script pack only when what it is built from changed.

`make -C src torirsserver-scripts` is ~30 content contract checks (~20 s) plus
sscompile over every script in the tree (~26 s). run.py and conformance.py used
to run it unconditionally on every run, because the tree routinely carries
uncommitted OSRS-Content edits and the embedded server refuses to boot on a
stale pack. On the shortest quest that was 46 of 58 seconds, and an author
batch makes 56-76 runs.

This module keeps "never boot a stale pack" and drops the rebuild when nothing
the pack is built from changed. The test is a STAT fingerprint (path, size,
mtime_ns -- never file contents) over exactly what sscompile and
tools/ss_allocate.py read (INPUT_* below; derived from ssc_main.c,
ssc_symbols.c, ssc_lane.c and ss_allocate.py), plus the compiler's own
sources, the Makefile rules that drive them, and -- recorded after the build --
the sscompile binary and the pack files themselves, so a pack rewritten by
anything other than this helper (a direct make, a lane build into the same
directory) is never taken as current.

The fingerprint lives beside the pack, in
<content>/server/scripts/build/pack.fingerprint: OSRS-Content's .gitignore
ignores server/scripts/build/, and the server's own staleness scan skips
`build`. It is written only after a successful build, and holds the INPUT
digest computed BEFORE the build started, so an edit made while sscompile runs
forces the next run to rebuild.

Backstops, all kept:
  * the server's own STALE SCRIPT PACK refusal (torirs_server_scripts.c) is
    untouched; run.py reacts to it by invalidate() + a forced rebuild + one
    relaunch (stale_pack_refused), so a false "current" cannot strand a run;
  * TORIRS_QUEST_ALWAYS_BUILD=1 restores the unconditional rebuild;
  * force=True (run.py --rebuild-scripts) rebuilds and so re-runs the
    contract checks.

Concurrency: several runners share this checkout. A rebuild holds an
exclusive flock on build/quest_gate/.pack_build.lock; a check holds it
shared, so a runner arriving mid-rebuild waits for it and then re-checks
instead of compiling again.
"""

import ast
import fcntl
import hashlib
import os
import subprocess
import sys
import time

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
CONTENT = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content")
SCRIPTS = os.path.join(CONTENT, "server", "scripts")
PACK_DIR = os.path.join(SCRIPTS, "build")
PACK_FILES = [os.path.join(PACK_DIR, "script.dat"), os.path.join(PACK_DIR, "script.idx")]
FINGERPRINT_PATH = os.path.join(PACK_DIR, "pack.fingerprint")
LOCK_PATH = os.path.join(REPO_ROOT, "build", "quest_gate", ".pack_build.lock")
MAKEFILE = os.path.join(REPO_ROOT, "src", "Makefile")
# `make -C src torirsserver-scripts` with no variables: OPT=1, so OBJ_DIR is
# build_opt (the Makefile's own default).
SSCOMPILE = os.path.join(REPO_ROOT, "src", "build_opt", "sscompile")
ALLOCATE = os.path.join(REPO_ROOT, "tools", "ss_allocate.py")
ALWAYS_BUILD_ENV = "TORIRS_QUEST_ALWAYS_BUILD"
MARKER = "STALE SCRIPT PACK"


def allocate_namespaces():
    """ss_allocate.py's SERVER_NAMESPACES, read out of the file rather than
    restated: it sweeps server/scripts/**/*.<ns> for [block] names, so a
    `.enum` edit changes pack/<ns>.alloc and therefore the pack."""
    with open(ALLOCATE, "r", encoding="utf-8") as handle:
        tree = ast.parse(handle.read(), ALLOCATE)
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
                isinstance(t, ast.Name) and t.id == "SERVER_NAMESPACES" for t in node.targets):
            return tuple(ast.literal_eval(node.value))
    raise AssertionError("no SERVER_NAMESPACES in %s" % ALLOCATE)


# What sscompile compiles/loads under --src (ssc_compile.c: .rs2;
# ssc_symbols.c: .constant, .dbtable, .varp), what the server's own stale scan
# counts (.dbrow), and what ss_allocate declares blocks from.
def script_extensions():
    exts = {".rs2", ".constant", ".dbtable", ".dbrow", ".varp"}
    exts.update("." + ns for ns in allocate_namespaces())
    return frozenset(exts)


# Whole trees, every file: sscompile's --pack defaults (<content>/pack,
# <content>/configs, walked recursively), the lanes (ported/: lane.ini, lane
# packs, pack_files, component roots, lane configs ss_allocate reads).
INPUT_TREES = ["pack", "configs", "ported"]
# Single files: ss_allocate's register, the compiler's sources.
INPUT_FILES = [
    os.path.join(CONTENT, "content.ini"),
    ALLOCATE,
    os.path.join(REPO_ROOT, "src", "content", "content_register.c"),
    os.path.join(REPO_ROOT, "src", "content", "content_register.h"),
    os.path.join(REPO_ROOT, "src", "content", "content_fields.c"),
    os.path.join(REPO_ROOT, "src", "content", "content_fields.h"),
    os.path.join(REPO_ROOT, "3rd", "ini", "ini.c"),
    os.path.join(REPO_ROOT, "3rd", "ini", "ini.h"),
    os.path.join(REPO_ROOT, "3rd", "rsareabuf", "rsareabuf.c"),
    os.path.join(REPO_ROOT, "3rd", "rsareabuf", "rsareabuf.h"),
]
# The Makefile's rules for the pack: hashed as TEXT (the file's mtime moves
# with every unrelated edit to a 5,000-line Makefile).
MAKE_TARGETS = ["torirsserver-scripts", "torirsserver-scripts-lanes", "sscompile"]
MAKE_VARIABLES = ["SSC_SRCS", "SS_SRCS", "SSC_CFLAGS", "RSAREABUF_DIR", "INI_SRC",
                  "TORIRSSERVER_CONTENT_DIR", "TORIRSSERVER_SCRIPT_OUT",
                  "TORIRSSERVER_SCRIPT_LANES"]


def _stat_line(path, st):
    return "%s\t%d\t%d\n" % (os.path.relpath(path, REPO_ROOT), st.st_size, st.st_mtime_ns)


def _walk(root, digest, accept=None, skip_top=None):
    """Every regular file under root (dot entries skipped, as the compiler
    skips them), in sorted order so the digest is stable."""
    count = 0
    stack = [(root, True)]
    while stack:
        directory, top = stack.pop()
        try:
            entries = sorted(os.scandir(directory), key=lambda e: e.name)
        except FileNotFoundError:
            continue
        subdirs = []
        for entry in entries:
            if entry.name.startswith("."):
                continue
            if entry.is_dir(follow_symlinks=True):
                if top and skip_top and skip_top(entry.name):
                    continue
                subdirs.append(entry.path)
                continue
            if accept and not accept(entry.name):
                continue
            digest.update(_stat_line(entry.path, entry.stat()).encode("utf-8"))
            count += 1
        # Depth-first in name order: push reversed so the first name pops first.
        for path in reversed(subdirs):
            stack.append((path, False))
    return count


def _makefile_rules():
    """The text of the pack's rules and variables, not the Makefile's mtime."""
    with open(MAKEFILE, "r", encoding="utf-8", errors="replace") as handle:
        lines = handle.readlines()
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        head = line.split(":", 1)[0].strip() if ":" in line and not line.startswith("\t") else None
        name = line.split("=", 1)[0].rstrip(" ?:+").strip() if "=" in line else None
        if head in MAKE_TARGETS and not line.lstrip().startswith("#"):
            out.append(line)
            i += 1
            while i < len(lines) and (lines[i].startswith("\t") or out[-1].rstrip().endswith("\\")):
                out.append(lines[i])
                i += 1
            continue
        if name in MAKE_VARIABLES and not line.startswith("\t"):
            out.append(line)
            i += 1
            while out[-1].rstrip().endswith("\\") and i < len(lines):
                out.append(lines[i])
                i += 1
            continue
        i += 1
    return "".join(out)


def input_fingerprint():
    """(hex digest, files counted) over everything the pack is built FROM."""
    digest = hashlib.sha256()
    exts = script_extensions()
    count = _walk(SCRIPTS, digest,
                  accept=lambda name: os.path.splitext(name)[1] in exts,
                  skip_top=lambda name: name == "build" or name.startswith("build_"))
    for tree in INPUT_TREES:
        digest.update(("tree %s\n" % tree).encode("utf-8"))
        count += _walk(os.path.join(CONTENT, tree), digest)
    # sscompile opens interfaces/<name>.compack for every interface symbol.
    digest.update(b"interfaces\n")
    interfaces = os.path.join(CONTENT, "interfaces")
    for entry in sorted(os.scandir(interfaces), key=lambda e: e.name):
        if entry.name.endswith(".compack") and entry.is_file():
            digest.update(_stat_line(entry.path, entry.stat()).encode("utf-8"))
            count += 1
    serverscript = os.path.join(REPO_ROOT, "src", "serverscript")
    for entry in sorted(os.scandir(serverscript), key=lambda e: e.name):
        if entry.is_file() and os.path.splitext(entry.name)[1] in (".c", ".h"):
            digest.update(_stat_line(entry.path, entry.stat()).encode("utf-8"))
            count += 1
    for path in INPUT_FILES:
        if os.path.exists(path):
            digest.update(_stat_line(path, os.stat(path)).encode("utf-8"))
            count += 1
        else:
            digest.update(("missing %s\n" % path).encode("utf-8"))
    digest.update(_makefile_rules().encode("utf-8"))
    return digest.hexdigest(), count


def output_fingerprint():
    """The build's own products, stated after it: the compiler binary (the
    Makefile's sscompile is phony, so it is relinked on every build) and the
    pack. None when the pack is missing."""
    digest = hashlib.sha256()
    for path in [SSCOMPILE] + PACK_FILES:
        if not os.path.isfile(path):
            return None
        digest.update(_stat_line(path, os.stat(path)).encode("utf-8"))
    return digest.hexdigest()


def read_recorded():
    try:
        with open(FINGERPRINT_PATH, "r", encoding="utf-8") as handle:
            fields = dict(line.rstrip("\n").split("=", 1) for line in handle if "=" in line)
    except FileNotFoundError:
        return None
    return fields


def invalidate():
    try:
        os.unlink(FINGERPRINT_PATH)
    except FileNotFoundError:
        pass


def _current(inputs):
    """(True, short) when the recorded fingerprint names these inputs and the
    pack/compiler on disk now; (False, why) otherwise."""
    recorded = read_recorded()
    if recorded is None:
        return False, "no fingerprint at %s" % os.path.relpath(FINGERPRINT_PATH, REPO_ROOT)
    outputs = output_fingerprint()
    if outputs is None:
        return False, "no pack (or no sscompile) on disk"
    if recorded.get("inputs") != inputs:
        return False, "the pack's inputs changed"
    if recorded.get("outputs") != outputs:
        return False, "the pack or sscompile was rewritten outside this helper"
    return True, hashlib.sha256((inputs + outputs).encode("utf-8")).hexdigest()[:8]


def _lock(handle, mode, label):
    try:
        fcntl.flock(handle, mode | fcntl.LOCK_NB)
    except BlockingIOError:
        print("%s: another run is rebuilding the pack, waiting" % label, flush=True)
        fcntl.flock(handle, mode)


def ensure_pack(run, label="scripts", force=False):
    """Rebuild the pack (contract checks + sscompile, `make -C src
    torirsserver-scripts`, exactly as before) when it is not current; return
    make's exit code, or 0 when it was current. `run` is the caller's
    command runner (prints the command, returns its exit code)."""
    if os.environ.get(ALWAYS_BUILD_ENV) == "1":
        force = True
    os.makedirs(os.path.dirname(LOCK_PATH), exist_ok=True)
    with open(LOCK_PATH, "a+") as handle:
        if not force:
            _lock(handle, fcntl.LOCK_SH, label)
            started = time.monotonic()
            inputs, count = input_fingerprint()
            current, detail = _current(inputs)
            fcntl.flock(handle, fcntl.LOCK_UN)
            if current:
                print("%s: pack current (fingerprint %s; %d inputs stated in %.2f s)"
                      % (label, detail, count, time.monotonic() - started), flush=True)
                return 0
            print("%s: rebuilding the pack -- %s" % (label, detail), flush=True)
        _lock(handle, fcntl.LOCK_EX, label)
        try:
            inputs, count = input_fingerprint()
            if not force:
                # Somebody may have rebuilt it while this runner waited.
                current, detail = _current(inputs)
                if current:
                    print("%s: pack current (fingerprint %s; rebuilt by another run)"
                          % (label, detail), flush=True)
                    return 0
            invalidate()
            code = run(["make", "-C", os.path.join(REPO_ROOT, "src"), "torirsserver-scripts"])
            if code != 0:
                return code
            outputs = output_fingerprint()
            assert outputs, "make torirsserver-scripts succeeded and left no pack in %s" % PACK_DIR
            temporary = FINGERPRINT_PATH + ".tmp.%d" % os.getpid()
            with open(temporary, "w", encoding="utf-8") as out:
                out.write("inputs=%s\noutputs=%s\nfiles=%d\n" % (inputs, outputs, count))
            os.replace(temporary, FINGERPRINT_PATH)
            print("%s: pack rebuilt (fingerprint %s)"
                  % (label, hashlib.sha256((inputs + outputs).encode("utf-8")).hexdigest()[:8]),
                  flush=True)
            return 0
        finally:
            fcntl.flock(handle, fcntl.LOCK_UN)


def stale_pack_refused(log_path):
    """Did the embedded server refuse this pack as stale (the server's own
    backstop, torirs_server_scripts.c)?"""
    try:
        with open(log_path, "rb") as handle:
            return MARKER.encode("utf-8") in handle.read()
    except FileNotFoundError:
        return False


def rebuild_after_refusal(run, label="scripts"):
    """The server said the pack is stale although the fingerprint said
    current: forget the fingerprint and rebuild, once."""
    print("%s: the server refused the pack as STALE although the fingerprint said current -- "
          "deleting the fingerprint and rebuilding" % label, flush=True)
    invalidate()
    return ensure_pack(run, label=label, force=True)


if __name__ == "__main__":
    # `python3 tools/quest_gate/pack_fingerprint.py` prints the cost of one scan.
    started = time.monotonic()
    digest, files = input_fingerprint()
    print("inputs %s over %d files in %.3f s" % (digest[:8], files, time.monotonic() - started))
    print("recorded:", read_recorded())
    sys.exit(0)
