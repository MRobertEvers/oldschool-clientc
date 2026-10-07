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

The SERVER pack (<content>/server/pack: every config record the embedded
server boots from, `cachepack pack --server-only`, stamped with a content
fingerprint in stamp.txt) is kept under the same lock (ensure_server_pack).
It used to be nobody's job here: a fixer's `make -C src torirsserver-servpack`
cleared the stamp and rewrote the pack in place for ~150 s, and every run that
booted in that window died "has no stamp" (or "is STALE" after a content edit
nobody packed) -> "net: embedded server failed to start" -> run.unfinished
with no rows (b70: dreammentor, troubledtortugans, taleoftherighteous,
sleepinggiants). Now:
  * the rebuild writes into a staging directory beside the pack and swaps it
    in with one atomic rename (RENAME_SWAP / RENAME_EXCHANGE), so no reader
    ever sees a stamp-less or half-written pack;
  * `make -C src torirsserver-servpack` (the default tree pack) goes through
    this module too, so a hand-run make takes the same lock;
  * a runner holds the lock SHARED from "both packs are current" until its
    client's first heartbeat (boot_hold), i.e. across the server's boot read
    of both packs, and a rebuild (exclusive) waits for every booting runner.
    A writer first takes a turnstile lock that new boot holds pass through,
    so a stream of overlapping boots cannot starve a rebuild.
A server that still refuses its server pack gets one rebuild under the lock
and one relaunch (server_pack_refused / rebuild_server_pack_after_refusal),
as the scripts pack already did.
"""

import ast
import ctypes
import errno
import fcntl
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
CONTENT = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content")
SCRIPTS = os.path.join(CONTENT, "server", "scripts")
PACK_DIR = os.path.join(SCRIPTS, "build")
PACK_FILES = [os.path.join(PACK_DIR, "script.dat"), os.path.join(PACK_DIR, "script.idx")]
FINGERPRINT_PATH = os.path.join(PACK_DIR, "pack.fingerprint")
LOCK_PATH = os.path.join(REPO_ROOT, "build", "quest_gate", ".pack_build.lock")
# Writers queue here before taking LOCK_PATH exclusive; a boot hold passes
# through it (shared, released at once) before taking LOCK_PATH shared.
TURNSTILE_PATH = os.path.join(REPO_ROOT, "build", "quest_gate", ".pack_build.turnstile")
MAKEFILE = os.path.join(REPO_ROOT, "src", "makefile")
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


def _lock(handle, mode, label, waiting="another run is rebuilding the pack, waiting"):
    try:
        fcntl.flock(handle, mode | fcntl.LOCK_NB)
    except BlockingIOError:
        print("%s: %s" % (label, waiting), flush=True)
        fcntl.flock(handle, mode)


def _lock_exclusive(handle, label):
    """LOCK_PATH exclusive, queued through the turnstile: while this writer
    waits, no NEW boot hold can take the lock shared (boot_hold passes the
    turnstile first), so overlapping boots cannot starve it. The turnstile is
    let go as soon as the lock is held."""
    with open(TURNSTILE_PATH, "a+") as turnstile:
        fcntl.flock(turnstile, fcntl.LOCK_EX)
        try:
            _lock(handle, fcntl.LOCK_EX, label,
                  "waiting for other runs to finish booting / rebuilding")
        finally:
            fcntl.flock(turnstile, fcntl.LOCK_UN)


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
        _lock_exclusive(handle, label)
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


# ---------------------------------------------------------------- server pack
#
# <content>/server/pack, the config records the embedded server boots from
# (torirs_server_boot.c). The server checks its stamp.txt against
# RSCache_ServerPackFingerprint (rscache_serverpack.c) -- a BYTE hash over the
# tree -- and refuses a pack with no stamp or a stamp that does not match.
# Here a STAT fingerprint over exactly the files that hash reads decides
# whether to ask cachepack at all; cachepack's own stamp check (the same hash
# the server runs) decides whether to write.

SERVPACK_DIR = os.path.join(CONTENT, "server", "pack")
SERVPACK_STAMP = "stamp.txt"
SERVPACK_DAT2 = "main_file_cache.dat2"
SERVPACK_FINGERPRINT_PATH = os.path.join(REPO_ROOT, "build", "quest_gate", ".servpack.fingerprint")
# Siblings of the pack in <content>/server/: a rebuild's staging directory and,
# where the filesystem has no atomic exchange, the pack it retires.
SERVPACK_STAGING_PREFIX = ".pack.staging."
SERVPACK_RETIRED_PREFIX = ".pack.retired."
# fingerprint_config_file() in rscache_serverpack.c: the server/scripts files
# the server pack's hash reads.
SERVPACK_SCRIPT_CONFIG_EXTENSIONS = frozenset(
    "." + ext for ext in (
        "underlay", "overlay", "idk", "inv", "loc", "enum", "npc", "obj", "param", "seq",
        "spotanim", "varbit", "varp", "varc", "hitsplat", "healthbar", "struct",
        "mapelement", "dbrow", "dbtable", "constant", "compack"))
# The refusals of torirs_server_servpack.c / torirs_server_boot.c.
SERVPACK_MARKERS = ("torirsserver: no server pack at",
                    "torirsserver: the server pack at",
                    "torirsserver: server pack ")
# TORIRSSERVER_ALLOW_STALE_PACK=1 prints the STALE line and boots anyway.
SERVPACK_ALLOWED = "booting anyway"


def servpack_input_fingerprint():
    """(hex digest, files counted) over the files RSCache_ServerPackFingerprint
    reads for the default (lane-less) pack: configs/, fields/, pack/ whole;
    interfaces/**.compack; server/scripts/** config files; ported/*/pack/."""
    digest = hashlib.sha256()
    count = 0
    for tree in ("configs", "fields", "pack"):
        digest.update(("tree %s\n" % tree).encode("utf-8"))
        count += _walk(os.path.join(CONTENT, tree), digest)
    digest.update(b"interfaces\n")
    count += _walk(os.path.join(CONTENT, "interfaces"), digest,
                   accept=lambda name: name.endswith(".compack"))
    digest.update(b"server/scripts\n")
    count += _walk(os.path.join(CONTENT, "server", "scripts"), digest,
                   accept=lambda name: os.path.splitext(name)[1]
                   in SERVPACK_SCRIPT_CONFIG_EXTENSIONS)
    ported = os.path.join(CONTENT, "ported")
    lanes = sorted(e.name for e in os.scandir(ported)
                   if not e.name.startswith(".")) if os.path.isdir(ported) else []
    for lane in lanes:
        digest.update(("ported %s\n" % lane).encode("utf-8"))
        count += _walk(os.path.join(ported, lane, "pack"), digest)
    return digest.hexdigest(), count


def servpack_output_fingerprint():
    """The live pack's stamp and dat2 (inode, size, mtime): a pack swapped in
    or rewritten by anything since the record is not taken as current. None
    when either is missing."""
    digest = hashlib.sha256()
    for name in (SERVPACK_STAMP, SERVPACK_DAT2):
        path = os.path.join(SERVPACK_DIR, name)
        try:
            st = os.stat(path)
        except FileNotFoundError:
            return None
        digest.update(("%s\t%d\t%d\t%d\n" % (name, st.st_ino, st.st_size, st.st_mtime_ns))
                      .encode("utf-8"))
    return digest.hexdigest()


def read_servpack_recorded():
    try:
        with open(SERVPACK_FINGERPRINT_PATH, "r", encoding="utf-8") as handle:
            return dict(line.rstrip("\n").split("=", 1) for line in handle if "=" in line)
    except FileNotFoundError:
        return None


def invalidate_server_pack():
    try:
        os.unlink(SERVPACK_FINGERPRINT_PATH)
    except FileNotFoundError:
        pass


def _servpack_current(inputs):
    recorded = read_servpack_recorded()
    if recorded is None:
        return False, "no fingerprint at %s" % os.path.relpath(SERVPACK_FINGERPRINT_PATH,
                                                                REPO_ROOT)
    outputs = servpack_output_fingerprint()
    if outputs is None:
        return False, "no stamped server pack at %s" % os.path.relpath(SERVPACK_DIR, REPO_ROOT)
    if recorded.get("inputs") != inputs:
        return False, "the server pack's inputs changed"
    if recorded.get("outputs") != outputs:
        return False, "the server pack was rewritten outside this helper"
    return True, hashlib.sha256((inputs + outputs).encode("utf-8")).hexdigest()[:8]


# renamex_np(RENAME_SWAP) on macOS, renameat2(RENAME_EXCHANGE) on Linux: both
# flags are 2. AT_FDCWD is Linux's -100.
_RENAME_EXCHANGE_FLAG = 2
_AT_FDCWD = -100


def _exchange(first, second):
    """Atomically exchange two directories. False when this platform or
    filesystem has no such call (the caller falls back to two renames)."""
    try:
        libc = ctypes.CDLL(None, use_errno=True)
    except OSError:
        return False
    a = os.fsencode(first)
    b = os.fsencode(second)
    if sys.platform == "darwin" and hasattr(libc, "renamex_np"):
        call = libc.renamex_np
        call.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
        result = call(a, b, _RENAME_EXCHANGE_FLAG)
    elif sys.platform.startswith("linux") and hasattr(libc, "renameat2"):
        call = libc.renameat2
        call.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p,
                         ctypes.c_uint]
        result = call(_AT_FDCWD, a, _AT_FDCWD, b, _RENAME_EXCHANGE_FLAG)
    else:
        return False
    if result == 0:
        return True
    error = ctypes.get_errno()
    if error in (errno.EINVAL, errno.ENOTSUP, errno.ENOSYS, getattr(errno, "EOPNOTSUPP", -1)):
        return False
    raise OSError(error, os.strerror(error), first, None, second)


def _swap_into_place(staging, live):
    """Put the finished pack at `live` without a moment in which `live` holds
    a partial one. After an exchange `staging` holds the old pack (the caller
    removes it); without one, the old pack is renamed away and the new one
    renamed in -- two renames, never a half-written directory."""
    if not os.path.isdir(live):
        os.rename(staging, live)
        return "renamed into place"
    if _exchange(staging, live):
        return "swapped into place"
    retired = os.path.join(os.path.dirname(live), "%s%d" % (SERVPACK_RETIRED_PREFIX, os.getpid()))
    os.rename(live, retired)
    os.rename(staging, live)
    shutil.rmtree(retired, ignore_errors=True)
    return "renamed into place (no atomic exchange here)"


def _servpack_build(run, label):
    """Called holding LOCK_PATH exclusive. cachepack writes into a staging
    directory beside the pack, seeded with the live pack's stamp: when that
    stamp is current, cachepack says "up to date" and writes nothing (the
    live pack stays); otherwise it builds a whole pack there, which is swapped
    in. The live pack is never cleared or written in place."""
    parent = os.path.dirname(SERVPACK_DIR)
    os.makedirs(parent, exist_ok=True)
    # A staging/retired directory left by a killed rebuild: nobody else
    # builds while this lock is held exclusive.
    for entry in os.scandir(parent):
        if entry.name.startswith((SERVPACK_STAGING_PREFIX, SERVPACK_RETIRED_PREFIX)):
            shutil.rmtree(entry.path, ignore_errors=True)
    staging = tempfile.mkdtemp(prefix=SERVPACK_STAGING_PREFIX, dir=parent)
    try:
        os.chmod(staging, 0o755)
        live_stamp = os.path.join(SERVPACK_DIR, SERVPACK_STAMP)
        if os.path.isfile(live_stamp):
            shutil.copyfile(live_stamp, os.path.join(staging, SERVPACK_STAMP))
        code = run(["make", "-C", os.path.join(REPO_ROOT, "src"), "torirsserver-servpack-direct",
                    "SERVPACK_OUT=%s" % staging])
        if code != 0:
            return code
        if not os.path.isfile(os.path.join(staging, SERVPACK_DAT2)):
            print("%s: cachepack found the server pack on disk current with its tree"
                  % label, flush=True)
            return 0
        assert os.path.isfile(os.path.join(staging, SERVPACK_STAMP)), \
            "cachepack succeeded and left an unstamped server pack in %s" % staging
        how = _swap_into_place(staging, SERVPACK_DIR)
        print("%s: server pack rebuilt and %s" % (label, how), flush=True)
        return 0
    finally:
        if os.path.isdir(staging):
            shutil.rmtree(staging, ignore_errors=True)


def ensure_server_pack(run, label="servpack", force=False):
    """Bring the server pack current under the pack lock: a shared-lock stat
    check, and only when that fails an exclusive rebuild (_servpack_build).
    Returns make's exit code, or 0. `force` skips the stat check (cachepack's
    own stamp check still decides whether to write)."""
    if os.environ.get(ALWAYS_BUILD_ENV) == "1":
        force = True
    os.makedirs(os.path.dirname(LOCK_PATH), exist_ok=True)
    with open(LOCK_PATH, "a+") as handle:
        if not force:
            _lock(handle, fcntl.LOCK_SH, label)
            started = time.monotonic()
            inputs, count = servpack_input_fingerprint()
            current, detail = _servpack_current(inputs)
            fcntl.flock(handle, fcntl.LOCK_UN)
            if current:
                print("%s: server pack current (fingerprint %s; %d inputs stated in %.2f s)"
                      % (label, detail, count, time.monotonic() - started), flush=True)
                return 0
            print("%s: checking the server pack -- %s" % (label, detail), flush=True)
        _lock_exclusive(handle, label)
        try:
            inputs, count = servpack_input_fingerprint()
            if not force:
                current, detail = _servpack_current(inputs)
                if current:
                    print("%s: server pack current (fingerprint %s; rebuilt by another run)"
                          % (label, detail), flush=True)
                    return 0
            invalidate_server_pack()
            code = _servpack_build(run, label)
            if code != 0:
                return code
            outputs = servpack_output_fingerprint()
            assert outputs, "the server pack build succeeded and left no stamped pack in %s" \
                % SERVPACK_DIR
            temporary = SERVPACK_FINGERPRINT_PATH + ".tmp.%d" % os.getpid()
            with open(temporary, "w", encoding="utf-8") as out:
                out.write("inputs=%s\noutputs=%s\nfiles=%d\n" % (inputs, outputs, count))
            os.replace(temporary, SERVPACK_FINGERPRINT_PATH)
            print("%s: server pack current (fingerprint %s)"
                  % (label, hashlib.sha256((inputs + outputs).encode("utf-8")).hexdigest()[:8]),
                  flush=True)
            return 0
        finally:
            fcntl.flock(handle, fcntl.LOCK_UN)


def server_pack_refused(log_path):
    """Did the embedded server refuse its server pack (absent, unstamped,
    STALE, or an archive that does not validate)?"""
    try:
        with open(log_path, "rb") as handle:
            text = handle.read().decode("utf-8", errors="replace")
    except FileNotFoundError:
        return False
    for line in text.splitlines():
        if any(marker in line for marker in SERVPACK_MARKERS) and SERVPACK_ALLOWED not in line:
            return True
    return False


def rebuild_server_pack_after_refusal(run, label="servpack"):
    """The server refused its server pack although this helper said current
    (or another session changed the tree since): rebuild under the lock,
    once."""
    print("%s: the server refused its server pack -- rebuilding it under the pack lock"
          % label, flush=True)
    invalidate_server_pack()
    return ensure_server_pack(run, label=label, force=True)


# ------------------------------------------------------------------ boot hold

class BootHold:
    """LOCK_PATH held shared while a client boots: no pack rebuild starts
    until release(). Releasing twice is a no-op (a deallocator)."""

    def __init__(self, handle):
        assert handle
        self._handle = handle

    @property
    def held(self):
        return self._handle is not None

    def release(self):
        if self._handle is None:
            return
        fcntl.flock(self._handle, fcntl.LOCK_UN)
        self._handle.close()
        self._handle = None


def _packs_not_current():
    """None when both packs are current, else why not. Call holding the lock."""
    inputs, _ = input_fingerprint()
    current, detail = _current(inputs)
    if not current:
        return "script pack: %s" % detail
    inputs, _ = servpack_input_fingerprint()
    current, detail = _servpack_current(inputs)
    if not current:
        return "server pack: %s" % detail
    return None


def boot_hold(run, label="run", attempts=3):
    """Take the pack lock SHARED with both packs (script.dat and server/pack)
    current, for the caller to hold across its client's boot -- the server
    reads both packs then and never again. A pack that is not current is
    rebuilt first (exclusive, so this waits for every other booting run) and
    the check repeated; after `attempts` the lock is held regardless and the
    server's own refusal is the last word."""
    os.makedirs(os.path.dirname(LOCK_PATH), exist_ok=True)
    for attempt in range(attempts + 1):
        with open(TURNSTILE_PATH, "a+") as turnstile:
            _lock(turnstile, fcntl.LOCK_SH, label, "a pack rebuild is queued, waiting")
            fcntl.flock(turnstile, fcntl.LOCK_UN)
        handle = open(LOCK_PATH, "a+")
        _lock(handle, fcntl.LOCK_SH, label)
        why = _packs_not_current()
        if why is None or attempt == attempts:
            if why is not None:
                print("%s: booting with %s (gave up after %d rebuilds)" % (label, why, attempts),
                      flush=True)
            return BootHold(handle)
        fcntl.flock(handle, fcntl.LOCK_UN)
        handle.close()
        print("%s: %s -- bringing it current before boot" % (label, why), flush=True)
        if why.startswith("script pack"):
            code = ensure_pack(run, label="scripts")
        else:
            code = ensure_server_pack(run, label="servpack")
        if code != 0:
            print("%s: the pack did not build (exit %d)" % (label, code), flush=True)
    raise AssertionError("unreachable")


def _cli_run(command):
    print("+ " + " ".join(command), flush=True)
    return subprocess.call(command)


if __name__ == "__main__":
    # `python3 tools/quest_gate/pack_fingerprint.py servpack [--force]` is
    # `make -C src torirsserver-servpack`: the server pack, under the lock.
    if sys.argv[1:2] == ["servpack"]:
        sys.exit(ensure_server_pack(_cli_run, force="--force" in sys.argv[2:]))
    # `python3 tools/quest_gate/pack_fingerprint.py` prints the cost of one scan.
    started = time.monotonic()
    digest, files = input_fingerprint()
    print("inputs %s over %d files in %.3f s" % (digest[:8], files, time.monotonic() - started))
    print("recorded:", read_recorded())
    sys.exit(0)
