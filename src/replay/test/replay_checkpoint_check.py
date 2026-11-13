"""replay_checkpoint_check.py <binary> <recording> <manifest> [K,N ...]

Session checkpoints (replay/replay_checkpoint.h), proved on a real recording
with the real client, headless:

  1. TORIRS_REPLAY_CHECKPOINT_CHECK=K,N for each window: take A at K,
     restore it, A == A2; then seek to K+N twice through the scrubber's
     RESTORE (from K, then from K+N); B == B2. Choose windows with no REBUILD
     in them (replay_checkpoint_check.h says why).
  2. A backward seek and a one-tick back-step through checkpoints, the
     second restoring one taken in the middle of a chase (the browser hang of
     2026-11-12): every seek lands, no IF_OPENSUB misses its target, and the
     session digests at T and T+64 equal a fast seek from the login.
  3. A restore that never happens (TORIRS_REPLAY_TEST_REFUSE_RESTORE): the
     seek's watchdog says so and falls back to a restart, and the seek lands.

Prints one line per case and exits non-zero on any failure.
"""
import os
import re
import subprocess
import sys
import tempfile

BIN, REC, MANIFEST = sys.argv[1:4]
WINDOWS = sys.argv[4:] or ["1000,150", "2110,130"]
SESSION = ("varps", "invs", "stats", "ui", "cs2")
# A varc read as a clientclock stamp: a clock value (the clock starts at 100
# and a session is minutes in) that two runs disagree on by at most 100 ticks.
CLOCK_FLOOR = 100
CLOCK_SLACK = 3000


def run(name, args, env_extra, frames=400000):
    tmp = tempfile.mkdtemp(prefix="ckpt_")
    env = dict(os.environ)
    env.update({"SDL_VIDEODRIVER": "dummy", "TORIRS_PLUGINS": "0",
                "TORIRS_PREFS": os.path.join(tmp, "prefs.ini"),
                "TORIRS_MAX_FRAMES": str(frames), "TORIRS_REPLAY_SEEK": "fast"})
    env.update(env_extra)
    out = subprocess.run([BIN, "--manifest", MANIFEST, "--replay", REC, "--unlocked"] + args,
                         env=env, capture_output=True, text=True, errors="replace")
    log = out.stdout + out.stderr
    with open(os.path.join(tmp, name + ".log"), "w") as f:
        f.write(log)
    return out.returncode, log, os.path.join(tmp, name + ".log")


def digests(log):
    rows = []
    for line in log.splitlines():
        m = re.match(r"replay: digest tick=(\d+) (.*)$", line)
        if m:
            rows.append((int(m.group(1)), dict(kv.split("=", 1) for kv in m.group(2).split() if "=" in kv)))
    return rows


failed = 0

for window in WINDOWS:
    k, n = window.split(",")
    rc, log, path = run("window_" + k, ["--replay-at", k], {"TORIRS_REPLAY_CHECKPOINT_CHECK": window})
    rt = re.search(r"replay: checkpoint roundtrip tick=\d+ (PASS|FAIL.*)", log)
    ck = re.search(r"replay: checkpoint check tick=\d+ n=\d+ (PASS|FAIL.*)", log)
    ok = rc == 0 and rt and ck and rt.group(1) == "PASS" and ck.group(1).startswith("PASS")
    failed += not ok
    print("checkpoint %s: %s; roundtrip %s; check %s (%s)" % (
        window, "PASS" if ok else "FAIL", rt.group(1) if rt else "missing", ck.group(1) if ck else "missing", path))

# The browser's sequence: a far chase, a backward seek through a checkpoint,
# then a one-tick back-step that restores one taken during that chase.
def clock_offset_only(dump_a, dump_b):
    """The cs2 digest's items (varc / varcstr lines of a digest dump) differ
    only by clientclock stamps: no string differs, and every differing varc
    int holds a clock value in both runs that moved by at most CLOCK_SLACK
    cycles. The client clock after a seek is not promised to match
    (replay_digest.h: a chase charges each tick's cycles instead of running
    them), and a script that stores `clientclock` in a varc carries the
    offset the clock had when it ran, which differs from stamp to stamp."""
    def items(path):
        with open(path, "rb") as f:
            return [l.decode("latin-1").rstrip("\n") for l in f if l.startswith((b"varc ", b"varcstr "))]
    a, b = items(dump_a), items(dump_b)
    if len(a) != len(b):
        return False, None
    offsets = set()
    for la, lb in zip(a, b):
        if la == lb:
            continue
        pa, pb = la.split(" "), lb.split(" ")
        if pa[0] != "varc" or pa[1] != pb[1]:
            return False, None
        va, vb = int(pa[2]), int(pb[2])
        if va < CLOCK_FLOOR or vb < CLOCK_FLOOR or abs(vb - va) > CLOCK_SLACK:
            return False, None
        offsets.add(vb - va)
    return True, sorted(offsets)


tmpd = tempfile.mkdtemp(prefix="ckpt_dump_")
rc, log, path = run("backstep", ["--replay-at", "178,3000,1000,999", "--replay-digest"],
                    {"TORIRS_REPLAY_CHECKPOINT_MB": "64",
                     "TORIRS_REPLAY_DIGEST_DUMP": os.path.join(tmpd, "back")})
landed = len(re.findall(r"replay: seek landed at tick", log))
restores = re.findall(r"replay: restored the checkpoint at tick (\d+)", log)
missing = len(re.findall(r"missing after boot", log))
rc2, log2, path2 = run("login999", ["--replay-at", "999", "--replay-digest"],
                       {"TORIRS_REPLAY_CHECKPOINT_MB": "0", "TORIRS_REPLAY_DIGEST_DUMP": os.path.join(tmpd, "login")})
a, b = digests(log), digests(log2)
same = len(a) == 2 and len(b) == 2
offsets = []
for i in range(2 if same else 0):
    t = a[i][0]
    same = same and t == b[i][0] and all(a[i][1].get(d) == b[i][1].get(d) for d in SESSION if d != "cs2")
    if a[i][1].get("cs2") != b[i][1].get("cs2"):
        ok_cs2, off = clock_offset_only(os.path.join(tmpd, "back.%d.txt" % t), os.path.join(tmpd, "login.%d.txt" % t))
        same = same and ok_cs2
        offsets.append(off)
ok = rc == 0 and landed == 4 and len(restores) >= 2 and missing == 0 and same
failed += not ok
print("back-step through checkpoints %s: %d of 4 seeks landed, restored %s, %d IF_OPENSUB misses, session "
      "digests at 999 and 1063 %s the login's%s (%s, %s)" % (
          "PASS" if ok else "FAIL", landed, ",".join(restores) or "none", missing,
          "equal" if same else "DIFFER from",
          " (cs2 apart from clientclock stamps, offset %s cycles)" % offsets if offsets else "", path, path2))

rc, log, path = run("stall", ["--replay-at", "1000,500", "--replay-digest"], {"TORIRS_REPLAY_TEST_REFUSE_RESTORE": "1"})
said = "made no progress" in log
landed = len(re.findall(r"replay: seek landed at tick", log))
ok = said and landed == 2
failed += not ok
print("stalled restore %s: watchdog %s, %d of 2 seeks landed (%s)" % (
    "PASS" if ok else "FAIL", "fired" if said else "silent", landed, path))

sys.exit(1 if failed else 0)
