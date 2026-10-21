#!/usr/bin/env python3
"""
Check a revision's C wire tables against RSProx, prot by prot.

    tools/rsprox_prot_compare.py 239          # src/net/rev/osrs239 vs RSProx osrs-239
    tools/rsprox_prot_compare.py 241
    tools/rsprox_prot_compare.py 241 --dir src/net/rev/osrs241

Reads the three generated tables a revision module carries --

    packetin.h   server -> client  { code, length, PKT_NAME_*, "PROT" }
    packetout.h  client -> server  { PKTOUT_NAME_*, code, length, "PROT" }
    zoneprot.h   UPDATE_ZONE_PARTIAL_ENCLOSED sub-packet ordinals

-- and compares every row to RSProx's GameServerProt / GameClientProt enums and
its IndexedZoneProtDecoder order (tools/rsprox_prot.py). The tables are C; this
reads them as text, so it checks exactly what the compiler sees rather than the
generator's intent.

Exit status is 1 when any prot RSProx defines is missing from the table, or
present with a different opcode or size, or when a zone ordinal disagrees. Rows
only the C table has are listed but do not fail the check: the 239 table
carries framing rows from the deob's own size table (ANTICHEAT_*) and one RSProt
prot RSProx does not model, and saying so is the point.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import rsprox_prot  # noqa: E402

SIZE = {
    "PKTIN_LENGTH_VARU8": -1, "PKTIN_LENGTH_VARU16": -2,
    "PKTOUT_LENGTH_VARU8": -1, "PKTOUT_LENGTH_VARU16": -2,
}

IN_ROW = re.compile(r'\{\s*(-?\d+)\s*,\s*(\w+|-?\d+)\s*,\s*(\w+)\s*,\s*"(\w+)"\s*\}')
OUT_ROW = re.compile(r'\{\s*(\w+)\s*,\s*(-?\d+)\s*,\s*(\w+|-?\d+)\s*,\s*"(\w+)"\s*\}')


def _size(tok):
    return SIZE[tok] if tok in SIZE else int(tok)


def read_c_tables(rev_dir):
    with open(os.path.join(rev_dir, "packetin.h")) as fh:
        src_in = fh.read()
    with open(os.path.join(rev_dir, "packetout.h")) as fh:
        src_out = fh.read()
    server = [{"code": int(m.group(1)), "size": _size(m.group(2)), "canon": m.group(3),
               "name": m.group(4)} for m in IN_ROW.finditer(src_in)]
    client = [{"canon": m.group(1), "code": int(m.group(2)), "size": _size(m.group(3)),
               "name": m.group(4)} for m in OUT_ROW.finditer(src_out)]
    zone = []
    zpath = os.path.join(rev_dir, "zoneprot.h")
    if os.path.exists(zpath):
        with open(zpath) as fh:
            for m in re.finditer(r"OSRS\d+_ZONE_(\w+)\s*=\s*(\d+)", fh.read()):
                if m.group(1) != "COUNT":
                    zone.append((int(m.group(2)), m.group(1)))
    zone.sort()
    return server, client, [n for _i, n in zone]


def compare_side(label, ours, theirs):
    """Returns (failures, report lines)."""
    lines = []
    fails = 0
    by_name = {}
    for r in ours:
        by_name[rsprox_prot.RSPROT_ALIASES.get(r["name"], r["name"])] = r
    seen = set()
    matched = 0
    for t in theirs:
        o = by_name.get(t["name"])
        if o is None:
            lines.append(f"  MISSING  {label} {t['name']:<44} rsprox ({t['code']}, {t['size']})")
            fails += 1
            continue
        seen.add(t["name"])
        if (o["code"], o["size"]) != (t["code"], t["size"]):
            lines.append(
                f"  MISMATCH {label} {t['name']:<44} ours ({o['code']}, {o['size']}) "
                f"rsprox ({t['code']}, {t['size']})")
            fails += 1
        else:
            matched += 1
    extra = [r for n, r in by_name.items() if n not in seen]
    for r in extra:
        lines.append(f"  ours-only {label} {r['name']:<43} ({r['code']}, {r['size']}) "
                     f"-- not an RSProx prot")
    # Two rows on one opcode is a framing ambiguity whatever RSProx says.
    codes = {}
    for r in ours:
        if r["code"] < 0:
            continue
        if r["code"] in codes:
            lines.append(f"  DUPLICATE {label} opcode {r['code']}: {codes[r['code']]} and {r['name']}")
            fails += 1
        codes[r["code"]] = r["name"]
    head = (f"{label}: {len(theirs)} rsprox prots, {len(ours)} table rows, "
            f"{matched} match, {fails} fail, {len(extra)} ours-only")
    return fails, [head] + lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rev", type=int)
    ap.add_argument("--dir", default=None, help="revision module (default src/net/rev/osrs<rev>)")
    args = ap.parse_args()

    rev_dir = args.dir or os.path.join(REPO_ROOT, "src", "net", "rev", f"osrs{args.rev}")
    server, client, zone = read_c_tables(rev_dir)
    rx = rsprox_prot.load_prots(args.rev)

    print(f"rsprox {rsprox_prot.source_hash()} osrs-{args.rev}  vs  "
          f"{os.path.relpath(rev_dir, REPO_ROOT)}")
    fails = 0
    for label, ours, theirs in (("server", server, rx["server"]),
                                ("client", client, rx["client"])):
        f, lines = compare_side(label, ours, theirs)
        fails += f
        for line in lines:
            print(line)

    rx_zone = rsprox_prot.zone_order(args.rev)
    if zone == rx_zone:
        print(f"zone: {len(zone)} enclosed sub-packet ordinals match")
    else:
        fails += 1
        print(f"zone: ORDER MISMATCH")
        for i in range(max(len(zone), len(rx_zone))):
            a = zone[i] if i < len(zone) else "-"
            b = rx_zone[i] if i < len(rx_zone) else "-"
            print(f"  {i:>2} ours {a:<28} rsprox {b}{'' if a == b else '   <--'}")

    print("RESULT:", "OK" if fails == 0 else f"{fails} FAILURE(S)")
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
