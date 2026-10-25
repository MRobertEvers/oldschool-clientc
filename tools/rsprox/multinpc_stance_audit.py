#!/usr/bin/env python3
"""Every multinpc a recording shows, with the stance the client must play:
the stance that rides in NO packet.

A multinpc goes out as its SHELL (`id=` in a transcript line) and the client
draws the RUNG its varbit selects (`multinpc=`). The deob takes size, turn
speed and the whole stance set off the shell (Statics.method4320 reads them
off class86.field1237, the untransformed type) and only the model, name and
ops off the rung (torirs_types.h ToriRS_NpcEntityFacts). So a shell that
states `readyanim=dream_cyrisus_sitting_up` over a rung in `human_ready` sends
no `[bas]` at all, and npc_block_inventory.py rightly shows none -- the stance
is the cache's. The Corsair Curse's sick crew (index 4352) passed every block
audit that way while our client stood all four up beside their beds
(2026-10-25).

For every (shell, rung) pair in the transcripts this prints the shell's and
the rung's size, turnspeed and stance set from configs/all.npc, and flags
each pair where they differ: those are npcs whose look depends on the rule.
Check each flagged one on the client (`t.npc.pose(sym)` -> `set ready N`
must be the shell's), and photograph it.

    python3 tools/rsprox/multinpc_stance_audit.py <transcript>... [--match '<regex>']
        [--npc-config OSRS-Content/osrs-content/configs/all.npc]
"""
import argparse
import os
import re
from collections import Counter

PAIR = re.compile(r"id=([a-z0-9_]+) \((\d+)\), multinpc=([a-z0-9_]+) \((\d+)\)")
FIELDS = ("size", "turnspeed", "readyanim", "walkanim", "walkanim_b", "walkanim_l", "walkanim_r",
          "idleleftanim", "idlerightanim", "runanim", "crawlanim")
DEFAULTS = {"size": "1", "turnspeed": "32"}


def load_npcs(path, wanted):
    recs = {}
    cur = None
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("[") and line.endswith("]"):
                name = line[1:-1]
                cur = recs.setdefault(name, {}) if name in wanted else None
                continue
            if cur is not None and "=" in line:
                k, v = line.split("=", 1)
                if k in FIELDS:
                    cur[k] = DEFAULTS.get(k, "-1") if v == "default" else v
    return recs


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("transcripts", nargs="+")
    ap.add_argument("--match", default=None, help="regex on the shell or rung name")
    ap.add_argument("--npc-config", default=os.path.join(root, "OSRS-Content/osrs-content/configs/all.npc"))
    args = ap.parse_args()

    pairs = Counter()
    for path in args.transcripts:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                for m in PAIR.finditer(line):
                    shell, rung = m.group(1), m.group(3)
                    if args.match and not (re.search(args.match, shell) or re.search(args.match, rung)):
                        continue
                    pairs[(shell, rung)] += 1
    if not pairs:
        print("no multinpc pairs in the transcripts")
        return
    recs = load_npcs(args.npc_config, {n for p in pairs for n in p})
    flagged = 0
    for (shell, rung), n in sorted(pairs.items()):
        s, r = recs.get(shell, {}), recs.get(rung, {})
        diffs = [k for k in FIELDS if s.get(k, DEFAULTS.get(k, "-1")) != r.get(k, DEFAULTS.get(k, "-1"))]
        mark = "DIFFERS" if diffs else "same"
        flagged += bool(diffs)
        print(f"== shell {shell} -> rung {rung}  ({n} lines)  {mark}")
        for k in diffs:
            print(f"   {k:14s} shell {s.get(k, DEFAULTS.get(k, '-1')):40s} rung {r.get(k, DEFAULTS.get(k, '-1'))}")
    print(f"{flagged} of {len(pairs)} pair(s) play the shell's stance over a different rung's: "
          "check each on the client (t.npc.pose) and in a photograph")


if __name__ == "__main__":
    main()
