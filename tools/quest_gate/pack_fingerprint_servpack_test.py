#!/usr/bin/env python3
"""Fixture test: the server pack is rebuilt under the pack lock, swapped in
whole, and never rewritten under a booting run.

    python3 tools/quest_gate/pack_fingerprint_servpack_test.py

b70: concurrent quest runs in one checkout died "net: embedded server failed
to start" because another session's `make torirsserver-servpack` cleared
<content>/server/pack/stamp.txt and rewrote the pack in place for ~150 s
(the server: "has no stamp"), or a content edit nobody packed left it STALE.
run.py's pack lock covered only the script pack.

Every case runs in a temporary tree: the module's paths (CONTENT,
SERVPACK_DIR, the lock files) are pointed there, and `run` is a fake
cachepack that writes a pack into SERVPACK_OUT -- no make, no cachepack, no
client.

  case                    want
  fingerprint_scope       the stat fingerprint moves for exactly the files
                          RSCache_ServerPackFingerprint reads (configs/,
                          fields/, pack/, interfaces/**.compack,
                          server/scripts/** config files, ported/*/pack/) and
                          not for others (.rs2, a lane's configs/, a .txt
                          in interfaces/)
  current_after_build     a build records the fingerprint; the next ensure
                          says current without calling cachepack; an input
                          edit makes it call cachepack again
  up_to_date_keeps_live   cachepack writing nothing into the staging dir
                          (its copied stamp was current) leaves the live pack
                          as it was -- same inode -- and no staging dir
  swap_is_whole           an observer polling the live pack throughout a slow
                          rebuild never sees it without a stamp, nor a stamp
                          from one build beside a dat2 from another; the
                          same with the two-rename fallback (no exchange)
  rebuild_waits_for_boot  ensure_server_pack waits while a boot hold is held
                          and builds only after release()
  boot_waits_for_rebuild  boot_hold started mid-rebuild returns only after
                          the rebuild finished
  refusal_markers         "has no stamp", "is STALE", "no server pack",
                          "was refused" and a bad archive are refusals; the
                          ALLOW_STALE "booting anyway" line and the script
                          pack's own STALE line are not
  release_twice           BootHold.release() twice is a no-op

Exit 0 when every case holds.
"""

import os
import shutil
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_fingerprint as pf  # noqa: E402

FAILURES = []


def check(case, condition, detail=""):
    if condition:
        print("ok    %s %s" % (case, detail))
    else:
        print("FAIL  %s %s" % (case, detail))
        FAILURES.append(case)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


class Tree:
    """A content tree and lock directory under a temp root, wired into pf."""

    def __init__(self):
        self.root = tempfile.mkdtemp(prefix="servpack_test.")
        self.content = os.path.join(self.root, "content")
        for rel in ("configs/all.obj", "fields/obj.ini", "pack/obj.pack",
                    "interfaces/bank.compack", "interfaces/notes.txt",
                    "server/scripts/a.rs2", "server/scripts/area/b.obj",
                    "ported/lane1/pack/x.pack", "ported/lane1/configs/y.obj"):
            write(os.path.join(self.content, rel), "v1\n")
        self.saved = {name: getattr(pf, name) for name in (
            "CONTENT", "SERVPACK_DIR", "SERVPACK_FINGERPRINT_PATH", "LOCK_PATH",
            "TURNSTILE_PATH", "_packs_not_current", "_exchange")}
        pf.CONTENT = self.content
        pf.SERVPACK_DIR = os.path.join(self.content, "server", "pack")
        pf.SERVPACK_FINGERPRINT_PATH = os.path.join(self.root, "lock", ".servpack.fingerprint")
        pf.LOCK_PATH = os.path.join(self.root, "lock", ".pack_build.lock")
        pf.TURNSTILE_PATH = os.path.join(self.root, "lock", ".pack_build.turnstile")
        os.makedirs(os.path.dirname(pf.LOCK_PATH), exist_ok=True)
        # The script pack is not under test: boot_hold sees it current.
        pf._packs_not_current = lambda: None

    def close(self):
        for name, value in self.saved.items():
            setattr(pf, name, value)
        shutil.rmtree(self.root, ignore_errors=True)


class FakeCachepack:
    """`run` for ensure_server_pack: emulates `cachepack pack --server-only
    --server-out <staging>` -- "up to date" when the copied stamp names the
    current generation, else clear, slow archive writes, stamp last."""

    def __init__(self, generation, steps=1, step_seconds=0.0):
        self.generation = generation
        self.steps = steps
        self.step_seconds = step_seconds
        self.calls = 0
        self.finished_at = None

    def __call__(self, command):
        self.calls += 1
        out = [a.split("=", 1)[1] for a in command if a.startswith("SERVPACK_OUT=")]
        assert out, command
        staging = out[0]
        stamp = os.path.join(staging, pf.SERVPACK_STAMP)
        if os.path.isfile(stamp):
            with open(stamp, encoding="utf-8") as handle:
                if handle.read() == "gen %s\n" % self.generation:
                    self.finished_at = time.monotonic()
                    return 0
            os.unlink(stamp)
        with open(os.path.join(staging, pf.SERVPACK_DAT2), "w", encoding="utf-8") as dat2:
            for _ in range(self.steps):
                dat2.write("gen %s\n" % self.generation)
                dat2.flush()
                time.sleep(self.step_seconds)
        write(stamp, "gen %s\n" % self.generation)
        self.finished_at = time.monotonic()
        return 0


def live_generation():
    """(stamp generation, dat2 generation) of the live pack; None for a
    missing file."""
    def first_line(name):
        try:
            with open(os.path.join(pf.SERVPACK_DIR, name), encoding="utf-8") as handle:
                return handle.readline().strip()
        except FileNotFoundError:
            return None
    return first_line(pf.SERVPACK_STAMP), first_line(pf.SERVPACK_DAT2)


def pinned_generation():
    """live_generation() read through one open handle on the live directory,
    so a swap between the two reads cannot mix two packs in the SAMPLE: the
    pinned directory is whole or the sample is dropped (it stopped being the
    live one while read). ("absent", None) when there is no live directory."""
    try:
        fd = os.open(pf.SERVPACK_DIR, os.O_RDONLY)
    except FileNotFoundError:
        return ("absent", None)
    try:
        out = []
        for name in (pf.SERVPACK_STAMP, pf.SERVPACK_DAT2):
            try:
                handle = os.open(name, os.O_RDONLY, dir_fd=fd)
            except FileNotFoundError:
                out.append(None)
                continue
            with os.fdopen(handle, encoding="utf-8") as text:
                out.append(text.readline().strip())
        try:
            still_live = os.stat(pf.SERVPACK_DIR).st_ino == os.fstat(fd).st_ino
        except FileNotFoundError:
            still_live = False
        return tuple(out) if still_live else None
    finally:
        os.close(fd)


def quiet(fn, *args, **kwargs):
    """Run fn with stdout discarded (the module narrates every step). Never
    while another thread is running: sys.stdout is process-wide."""
    saved = sys.stdout
    sys.stdout = open(os.devnull, "w")
    try:
        return fn(*args, **kwargs)
    finally:
        sys.stdout.close()
        sys.stdout = saved


def case_fingerprint_scope():
    tree = Tree()
    try:
        base, _ = pf.servpack_input_fingerprint()
        moves = ["configs/all.obj", "fields/obj.ini", "pack/obj.pack", "interfaces/bank.compack",
                 "server/scripts/area/b.obj", "ported/lane1/pack/x.pack"]
        stays = ["server/scripts/a.rs2", "ported/lane1/configs/y.obj", "interfaces/notes.txt"]
        for rel in moves + stays:
            before, _ = pf.servpack_input_fingerprint()
            write(os.path.join(tree.content, rel), "v2 %s\n" % rel)
            after, _ = pf.servpack_input_fingerprint()
            check("fingerprint_scope", (after != before) == (rel in moves),
                  "%s %s" % (rel, "moves" if rel in moves else "stays"))
        check("fingerprint_scope", base != pf.servpack_input_fingerprint()[0], "overall")
    finally:
        tree.close()


def case_current_after_build():
    tree = Tree()
    try:
        fake = FakeCachepack("1")
        check("current_after_build", quiet(pf.ensure_server_pack, fake) == 0, "first build")
        check("current_after_build", live_generation() == ("gen 1", "gen 1"), "live is gen 1")
        quiet(pf.ensure_server_pack, fake)
        check("current_after_build", fake.calls == 1, "second ensure did not call cachepack")
        write(os.path.join(tree.content, "configs/all.obj"), "edited\n")
        fake.generation = "2"
        quiet(pf.ensure_server_pack, fake)
        check("current_after_build", fake.calls == 2 and live_generation() == ("gen 2", "gen 2"),
              "an edit rebuilt it")
        parent = os.path.dirname(pf.SERVPACK_DIR)
        leftovers = [e for e in os.listdir(parent) if e.startswith(".pack.")]
        check("current_after_build", not leftovers, "no staging left: %s" % leftovers)
    finally:
        tree.close()


def case_up_to_date_keeps_live():
    tree = Tree()
    try:
        fake = FakeCachepack("1")
        quiet(pf.ensure_server_pack, fake)
        inode = os.stat(os.path.join(pf.SERVPACK_DIR, pf.SERVPACK_DAT2)).st_ino
        # Stat inputs moved (a touch), bytes did not: cachepack says up to date.
        os.utime(os.path.join(tree.content, "configs/all.obj"), None)
        os.utime(os.path.join(tree.content, "configs/all.obj"),
                 ns=(time.time_ns() + 10**9, time.time_ns() + 10**9))
        quiet(pf.ensure_server_pack, fake)
        after = os.stat(os.path.join(pf.SERVPACK_DIR, pf.SERVPACK_DAT2)).st_ino
        check("up_to_date_keeps_live", fake.calls == 2, "cachepack was asked")
        check("up_to_date_keeps_live", after == inode, "live dat2 untouched")
        parent = os.path.dirname(pf.SERVPACK_DIR)
        check("up_to_date_keeps_live",
              not [e for e in os.listdir(parent) if e.startswith(".pack.")], "no staging left")
        current, _ = pf._servpack_current(pf.servpack_input_fingerprint()[0])
        check("up_to_date_keeps_live", current, "recorded as current")
    finally:
        tree.close()


def observe_rebuild(case, exchange):
    tree = Tree()
    try:
        if not exchange:
            pf._exchange = lambda first, second: False
        quiet(pf.ensure_server_pack, FakeCachepack("1"))
        seen = []
        stop = threading.Event()

        def observer():
            while not stop.is_set():
                sample = pinned_generation()
                if sample is None:
                    continue
                stamp, dat2 = sample
                if stamp == "absent":
                    # Between the two renames of the fallback: no pack at all,
                    # which the server reports as "no server pack" -- counted.
                    seen.append(("absent", None))
                elif stamp is None or stamp != dat2:
                    seen.append((stamp, dat2))

        thread = threading.Thread(target=observer)
        thread.start()
        try:
            quiet(pf.ensure_server_pack, FakeCachepack("2", steps=20, step_seconds=0.01),
                  force=True)
        finally:
            stop.set()
            thread.join()
        partial = [s for s in seen if s[0] != "absent"]
        check(case, not partial, "never a stamp-less or mixed pack (%d bad reads)" % len(partial))
        if exchange:
            check(case, not seen, "never absent with an atomic exchange")
        check(case, live_generation() == ("gen 2", "gen 2"), "live is gen 2 after")
    finally:
        tree.close()


def case_swap_is_whole():
    observe_rebuild("swap_is_whole", exchange=True)
    observe_rebuild("swap_is_whole_fallback", exchange=False)


def case_rebuild_waits_for_boot():
    tree = Tree()
    try:
        quiet(pf.ensure_server_pack, FakeCachepack("1"))
        hold = quiet(pf.boot_hold, None, label="test")
        fake = FakeCachepack("2")
        thread = threading.Thread(target=pf.ensure_server_pack, args=(fake,),
                                  kwargs={"force": True})
        thread.start()
        time.sleep(0.4)
        check("rebuild_waits_for_boot", fake.calls == 0, "no build while the boot hold is held")
        released = time.monotonic()
        hold.release()
        thread.join(10)
        check("rebuild_waits_for_boot", fake.calls == 1 and fake.finished_at >= released,
              "built after release")
    finally:
        tree.close()


def case_boot_waits_for_rebuild():
    tree = Tree()
    try:
        quiet(pf.ensure_server_pack, FakeCachepack("1"))
        fake = FakeCachepack("2", steps=10, step_seconds=0.05)
        thread = threading.Thread(target=pf.ensure_server_pack, args=(fake,),
                                  kwargs={"force": True})
        thread.start()
        while fake.calls == 0:
            time.sleep(0.01)
        hold = pf.boot_hold(None, label="test")
        acquired = time.monotonic()
        thread.join(10)
        check("boot_waits_for_rebuild", fake.finished_at is not None and
              fake.finished_at <= acquired, "boot hold acquired after the build finished")
        check("boot_waits_for_rebuild", live_generation() == ("gen 2", "gen 2"),
              "and boots on the new pack")
        hold.release()
    finally:
        tree.close()


def case_refusal_markers():
    root = tempfile.mkdtemp(prefix="servpack_test_log.")
    try:
        cases = [
            ("torirsserver: the server pack at X has no stamp (an interrupted or partial write)",
             True),
            ("torirsserver: the server pack at X is STALE — Y changed since it was written; run",
             True),
            ("torirsserver: no server pack at X — the server reads its config records", True),
            ("torirsserver: the server pack at X was refused — rebuild it with", True),
            ("torirsserver: server pack X: archive (9, 0) does not validate", True),
            ("torirsserver: the server pack at X is STALE — Y changed since it was written; run "
             "`m` (TORIRSSERVER_ALLOW_STALE_PACK=1: booting anyway)", False),
            ("torirsserver: STALE SCRIPT PACK", False),
            ("net: embedded server failed to start", False),
        ]
        for text, want in cases:
            path = os.path.join(root, "client.log")
            write(path, "boot\n%s\nend\n" % text)
            check("refusal_markers", pf.server_pack_refused(path) == want, text[:60])
        check("refusal_markers", not pf.server_pack_refused(os.path.join(root, "absent.log")),
              "no log")
    finally:
        shutil.rmtree(root, ignore_errors=True)


def case_release_twice():
    tree = Tree()
    try:
        quiet(pf.ensure_server_pack, FakeCachepack("1"))
        hold = quiet(pf.boot_hold, None, label="test")
        check("release_twice", hold.held, "held")
        hold.release()
        hold.release()
        check("release_twice", not hold.held, "released")
    finally:
        tree.close()


def main():
    case_fingerprint_scope()
    case_current_after_build()
    case_up_to_date_keeps_live()
    case_swap_is_whole()
    case_rebuild_waits_for_boot()
    case_boot_waits_for_rebuild()
    case_refusal_markers()
    case_release_twice()
    if FAILURES:
        print("FAILED: %s" % ", ".join(sorted(set(FAILURES))))
        return 1
    print("all cases hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
