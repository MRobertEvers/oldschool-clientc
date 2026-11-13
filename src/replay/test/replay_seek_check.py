#!/usr/bin/env python3
"""
The replay scrubber's proof: a fast seek leaves the client's authoritative
state as linear play does (the design's "What 'the same as playing there'
means").

For each tick T the private client binary runs twice, headless:

    TORIRS_REPLAY_SEEK=fullsim  --replay-at T --replay-digest   (linear play)
    TORIRS_REPLAY_SEEK=fast     --replay-at T --replay-digest   (chase, keyframe, pre-roll)

Each prints `replay: digest tick=T ...` when its seek lands and again 64 ticks
later (a wrong npc order or decoder table shows only when the next info packet
misreads). Every domain is compared at both. With dumps on, the interface and
clientscript-variable items behind a differing digest are listed.

    make -C src replay-seek-check RECORDING=<file.bin> TICKS="t1 t2 ..."
        [REPLAY_SEEK_CHECK_BINARY=build_seek_opt/torirs] [MANIFEST=...]
        [CHASE_CYCLES=30] [OUT=<dir>]

Exit 0 when every domain matches at every tick.
"""
import os
import re
import subprocess
import sys
import tempfile
import time

DIGEST = re.compile(r"replay: digest tick=(\d+) (.*?) \(clientclock (\d+), logic cycle (\d+)\)")
BREAKDOWN = re.compile(r"replay: seek breakdown .*")


def run(binary, manifest, recording, tick, mode, out_dir, cycles):
    tag = "%s_%d" % (mode, tick)
    env = dict(os.environ)
    env.update({
        "SDL_VIDEODRIVER": "dummy",
        "TORIRS_PLUGINS": "0",
        "TORIRS_PREFS": os.path.join(out_dir, tag + "_prefs.ini"),
        "TORIRS_MAX_FRAMES": "2000000",
        "TORIRS_REPLAY_SEEK": mode,
        "TORIRS_REPLAY_CHASE_CYCLES": str(cycles),
        "TORIRS_REPLAY_DIGEST_DUMP": os.path.join(out_dir, tag),
    })
    log = os.path.join(out_dir, tag + ".log")
    t0 = time.time()
    with open(log, "wb") as f:
        subprocess.run([binary, "--manifest", manifest, "--replay", recording, "--unlocked",
                        "--replay-at", str(tick), "--replay-digest"],
                       stdout=f, stderr=subprocess.STDOUT, env=env, check=False)
    took = time.time() - t0
    digests = {}
    breakdown = ""
    with open(log, "r", encoding="latin-1") as f:
        for line in f:
            m = DIGEST.search(line)
            if m:
                fields = dict(kv.split("=", 1) for kv in m.group(2).split())
                digests[int(m.group(1))] = (fields, int(m.group(3)))
            m = BREAKDOWN.search(line)
            if m:
                breakdown = m.group(0)
    return digests, breakdown, took, log


def dump_items(path):
    items = {}
    if not os.path.exists(path):
        return items
    with open(path, "r", encoding="latin-1") as f:
        for line in f:
            parts = line.rstrip("\n").split(" ")
            if parts[0] == "ui":
                key = ("ui", parts[3], parts[2]) if parts[2] != "dyn=-1" else ("ui", parts[1])
                items.setdefault(key, []).append(" ".join(p for p in parts[4:] if not p.startswith("hash=")))
            elif parts[0] in ("varc", "varcstr"):
                items[(parts[0], parts[1])] = [" ".join(parts[2:])]
    return {k: sorted(v) for k, v in items.items()}


def explain(out_dir, tick, at):
    a = dump_items(os.path.join(out_dir, "fullsim_%d.%d.txt" % (tick, at)))
    b = dump_items(os.path.join(out_dir, "fast_%d.%d.txt" % (tick, at)))
    diffs = [k for k in set(a) | set(b) if a.get(k) != b.get(k)]
    for k in sorted(diffs)[:12]:
        print("        %-40s linear %s | fast %s" % (" ".join(k), a.get(k, ["-"])[0][:70],
                                                      b.get(k, ["-"])[0][:70]))
    if len(diffs) > 12:
        print("        ... %d items in all" % len(diffs))


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    binary, recording, ticks = sys.argv[1], sys.argv[2], [int(t) for t in sys.argv[3].split()]
    manifest = sys.argv[4] if len(sys.argv) > 4 and sys.argv[4] else "manifests/manifest_osrs242_replay.ini"
    cycles = int(sys.argv[5]) if len(sys.argv) > 5 and sys.argv[5] else 30
    out_dir = sys.argv[6] if len(sys.argv) > 6 and sys.argv[6] else tempfile.mkdtemp(prefix="replay_seek_check_")
    os.makedirs(out_dir, exist_ok=True)
    print("replay-seek-check: %s, ticks %s, chase %d cycles a tick, logs in %s"
          % (recording, " ".join(map(str, ticks)), cycles, out_dir))
    failed = 0
    for tick in ticks:
        lin, lin_bd, lin_s, lin_log = run(binary, manifest, recording, tick, "fullsim", out_dir, cycles)
        fast, fast_bd, fast_s, fast_log = run(binary, manifest, recording, tick, "fast", out_dir, cycles)
        print("tick %d: linear %.1f s, fast %.1f s" % (tick, lin_s, fast_s))
        print("    linear: %s" % lin_bd)
        print("    fast:   %s" % fast_bd)
        for at in sorted(set(lin) | set(fast)):
            if at not in lin or at not in fast:
                print("  tick %d: no digest at %d in %s" % (tick, at, lin_log if at not in lin else fast_log))
                failed += 1
                continue
            lf, lclock = lin[at]
            ff, fclock = fast[at]
            bad = [d for d in lf if lf[d] != ff.get(d)]
            print("  at %d: %s%s (clientclock linear %d, fast %d)"
                  % (at, "all %d domains equal" % len(lf) if not bad else "DIFFER: " + " ".join(bad),
                     "" if not bad else "; equal: " + " ".join(d for d in lf if d not in bad),
                     lclock, fclock))
            if bad:
                failed += 1
                if "ui" in bad or "cs2" in bad:
                    explain(out_dir, tick, at)
        if len(lin) < 1 or len(fast) < 1:
            print("  tick %d: a run printed no digest (logs: %s, %s)" % (tick, lin_log, fast_log))
            failed += 1
    print("replay-seek-check: %s" % ("PASS" if not failed else "%d comparison(s) differ" % failed))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
