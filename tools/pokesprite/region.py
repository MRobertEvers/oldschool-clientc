#!/usr/bin/env python3
"""Fill the PokeSprite store with what players actually asked for.

    tools/pokesprite/region.py --store assets/pokesprite --from-missing

io_server records every sheet key it was asked for and lacked in
<store>/missing.db. This renders exactly those -- npc sequences through
pokesprite.py, player layers through layers.generate -- into the store, so
the next session finds them. Keys are grouped by subject so one npc's
sequences share one render setup.
"""

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import layers  # noqa: E402
from store import Store  # noqa: E402


def parse(keys):
    npcs, layer_seqs = {}, {}
    for key in keys:
        parts = key.split("/")
        if parts[0] == "npc" and len(parts) == 3:
            npc, seq = int(parts[1]), int(parts[2])
            if seq > 0:
                npcs.setdefault(npc, set()).add(seq)
        elif parts[0] == "layer" and len(parts) == 5 and parts[1] == "m":
            seq = int(parts[4])
            if seq > 0:
                layer_seqs.setdefault("%s:%s" % (parts[2], parts[3]), set()).add(seq)
        else:
            print("skip %s (no generator for it yet)" % key)
    return npcs, layer_seqs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--store", required=True)
    ap.add_argument("--from-missing", action="store_true", required=True)
    ap.add_argument("--work", default="build/poke/gen")
    ap.add_argument("--limit", type=int, default=0, help="at most this many subjects")
    a = ap.parse_args()

    st = Store(a.store)
    missing = st.missing()
    st.close()
    npcs, layer_seqs = parse(k for k, _ in missing)
    print("missing: %d keys -> %d npcs, %d player layers" % (len(missing), len(npcs),
                                                           len(layer_seqs)))
    done = 0
    for npc, seqs in sorted(npcs.items()):
        if a.limit and done >= a.limit:
            break
        anims = ",".join("s%d=%d" % (s, s) for s in sorted(seqs))
        out = os.path.join(a.work, "npc_%d" % npc)
        r = subprocess.run([sys.executable, os.path.join(HERE, "pokesprite.py"), "--npc", str(npc),
                            "--anims", anims, "--out", out, "--store", a.store],
                           capture_output=True, text=True)
        status = "ok" if r.returncode == 0 else "FAILED: " + (r.stderr.strip().splitlines() or
                                                              ["?"])[-1]
        print("npc %d seqs %s: %s" % (npc, sorted(seqs), status))
        done += 1
    if layer_seqs:
        todo = {L: sorted(s) for L, s in sorted(layer_seqs.items())}
        n = layers.generate(todo, os.path.join(a.work, "layers_raw"), a.store)
        print("player layers: %d sheets stored" % n)


if __name__ == "__main__":
    main()
