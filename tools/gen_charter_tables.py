#!/usr/bin/env python3
"""
gen_charter_tables — author the charter ship port and fare dbrows.

    tools/gen_charter_tables.py            # dry run, prints a diff summary
    tools/gen_charter_tables.py --write

Writes, into OSRS-Content/osrs239-content/server/scripts/transport_charter/configs/:

    charter_port.dbrow    16 rows — one per reachable port
    charter_fare.dbrow    240 rows — one per ordered (from, to) pair

See docs/transport/CHARTER_SHIPS.md. This tool authors data only; the
mechanics live in that directory's `.rs2` files.

## Where each number comes from

**Fares** are the wiki's own table, transcribed into FARES below as 120
unordered pairs and emitted in both directions. They are NOT taken from
tools/data/shortest_path/transports/charter_ships.tsv, which disagrees with the
wiki on cost — Musa Point to Brimhaven 200 vs 480, Karamja Shipyard to Port
Khazard 720 vs 1600, and the whole Sunset Coast row exactly halved. That reads
as a plugin-side data bug, so the tsv is used only to cross-check the arrival
tiles and the quest gates (--check-tsv), never the money.

**Arrival tiles** are `chartering_destination_port_coord` (column 3) of the
cache's own dbtable 206, read back out of configs/all.dbrow. The raw ints carry
a 1 in bits 28+ on every row that has a coord at all, while `inzone` on the same
row decodes cleanly at plane 0 — a `level + 1` bias so that 0 can mean null
(Crandor's row is exactly 0). PLANE_BIAS below subtracts it, and the tool
asserts the bias is uniform rather than masking blind.

**Destination ids** are `chartering_destination_id` (column 0), which is what
`chartering_previous_destination` (varbit 11209) holds and what the multinpc
children are indexed by. They are not ours to choose.

## Two ports the cache has and this tool skips

Tempestus  — clientscript 9104 has `case 4163: return(0)` with no condition.
Crandor    — `port_coord` is literally 0; there is nowhere to arrive.

The six Sailing ports (Pandemonium, Summer Shore, Red Rock, Barracuda HQ,
Deepfin Point, Port Roberts) carry related_content=1 and are switched off by the
cache's own restriction system (clientscript 8943), so they are skipped too.
"""

from __future__ import annotations

import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
ALL_DBROW = os.path.join(CONTENT, "configs", "all.dbrow")
OUT_DIR = os.path.join(
    CONTENT, "server", "scripts", "transport_charter", "configs")
TSV = os.path.join(
    REPO, "tools", "data", "shortest_path", "transports", "charter_ships.tsv")

WIKI_URL = "https://oldschool.runescape.wiki/w/Charter_ship"
FARES_URL = "https://oldschool.runescape.wiki/w/Template:Charter_ship_fares"

# The cache dbrow suffix -> the symbol this tool emits, in destination-id order.
# Suffix is what follows `chartering_destination_` in configs/all.dbrow.
PORTS = [
    ("brimhaven",      "brimhaven",  "Brimhaven"),
    ("catherby",       "catherby",   "Catherby"),
    ("mosleharmless",  "mosle",      "Mos Le'Harmless"),
    ("musapoint",      "musa",       "Musa Point"),
    ("portkhazard",    "khazard",    "Port Khazard"),
    ("portphasmatys",  "phasmatys",  "Port Phasmatys"),
    ("portsarim",      "sarim",      "Port Sarim"),
    ("shipyard",       "shipyard",   "Karamja Shipyard"),
    ("porttyras",      "tyras",      "Port Tyras"),
    ("corsaircove",    "corsair",    "Corsair Cove"),
    ("prifddinas",     "prifddinas", "Prifddinas"),
    ("piscarilius",    "piscarilius", "Port Piscarilius"),
    ("landsend",       "landsend",   "Land's End"),
    ("fortis",         "fortis",     "Civitas illa Fortis"),
    ("aldarin",        "aldarin",    "Aldarin"),
    ("sunsetcoast",    "sunset",     "Sunset Coast"),
]

# Cache rows deliberately not emitted, and why. Asserted present so that a cache
# update which removes one of them fails here instead of silently shrinking the
# network.
SKIPPED = {
    "crandor":       "port_coord is 0 — no arrival tile exists",
    "tempestus":     "clientscript 9104 case 4163 returns 0 unconditionally",
    "pandemonium":   "related_content=1 — Sailing, restricted off by clientscript 8943",
    "summer_shore":  "related_content=1 — Sailing",
    "red_rock":      "related_content=1 — Sailing",
    "barracuda_hq":  "related_content=1 — Sailing",
    "deepfin_point": "related_content=1 — Sailing",
    "port_roberts":  "related_content=1 — Sailing",
}

# Every offered route, once. The reverse direction costs the same: the wiki
# matrix is symmetric across all 16 mainline ports (checked cell by cell, 0
# mismatches), and check_symmetry() below re-derives that rather than trusting
# this comment. A pair absent from this table is a route the game does not
# offer — those are the "NA" cells of the wiki matrix, and they agree with the
# same-port-pair exclusions hard-coded in clientscript 9104:
#
#   sarim  <-> musa / piscarilius / landsend
#   mosle  <-> phasmatys
#   piscarilius <-> landsend
#   aldarin <-> sunset
#
# Costs marked HALVED are already halved by the wiki, because reaching Mos
# Le'Harmless requires Cabin Fever anyway. Do not halve again.
FARES = {
    ("sarim", "brimhaven"): 1600,
    ("sarim", "catherby"): 1000,
    ("sarim", "mosle"): 650,          # HALVED
    ("sarim", "khazard"): 1280,
    ("sarim", "phasmatys"): 1300,
    ("sarim", "shipyard"): 400,
    ("sarim", "tyras"): 3200,
    ("sarim", "corsair"): 1200,
    ("sarim", "prifddinas"): 4800,
    ("sarim", "fortis"): 3000,
    ("sarim", "aldarin"): 3100,
    ("sarim", "sunset"): 3100,

    ("brimhaven", "catherby"): 480,
    ("brimhaven", "mosle"): 1950,     # HALVED
    ("brimhaven", "musa"): 480,
    ("brimhaven", "khazard"): 400,
    ("brimhaven", "phasmatys"): 2900,
    ("brimhaven", "shipyard"): 400,
    ("brimhaven", "tyras"): 3200,
    ("brimhaven", "corsair"): 680,
    ("brimhaven", "prifddinas"): 3450,
    ("brimhaven", "piscarilius"): 2000,
    ("brimhaven", "landsend"): 2200,
    ("brimhaven", "fortis"): 2400,
    ("brimhaven", "aldarin"): 2500,
    ("brimhaven", "sunset"): 2500,

    ("catherby", "mosle"): 1250,      # HALVED
    ("catherby", "musa"): 480,
    ("catherby", "khazard"): 1600,
    ("catherby", "phasmatys"): 3500,
    ("catherby", "shipyard"): 1600,
    ("catherby", "tyras"): 3200,
    ("catherby", "corsair"): 1000,
    ("catherby", "prifddinas"): 3560,
    ("catherby", "piscarilius"): 2000,
    ("catherby", "landsend"): 2200,
    ("catherby", "fortis"): 2400,
    ("catherby", "aldarin"): 2500,
    ("catherby", "sunset"): 2500,

    ("mosle", "musa"): 2050,          # HALVED
    ("mosle", "khazard"): 550,        # HALVED
    ("mosle", "shipyard"): 550,       # HALVED
    ("mosle", "tyras"): 1600,         # HALVED
    ("mosle", "corsair"): 2040,       # HALVED
    ("mosle", "prifddinas"): 2475,    # HALVED
    ("mosle", "piscarilius"): 2100,   # HALVED
    ("mosle", "landsend"): 2200,      # HALVED
    ("mosle", "fortis"): 2300,        # HALVED
    ("mosle", "aldarin"): 2350,       # HALVED
    ("mosle", "sunset"): 2350,        # HALVED

    ("musa", "khazard"): 400,
    ("musa", "phasmatys"): 1100,
    ("musa", "shipyard"): 200,
    ("musa", "tyras"): 3200,
    ("musa", "corsair"): 800,
    ("musa", "prifddinas"): 4400,
    ("musa", "piscarilius"): 2500,
    ("musa", "landsend"): 2700,
    ("musa", "fortis"): 2900,
    ("musa", "aldarin"): 3000,
    ("musa", "sunset"): 3000,

    ("khazard", "phasmatys"): 4100,
    ("khazard", "shipyard"): 1600,
    ("khazard", "tyras"): 3200,
    ("khazard", "corsair"): 600,
    ("khazard", "prifddinas"): 2800,
    ("khazard", "piscarilius"): 1800,
    ("khazard", "landsend"): 2000,
    ("khazard", "fortis"): 2200,
    ("khazard", "aldarin"): 2300,
    ("khazard", "sunset"): 2300,

    ("phasmatys", "shipyard"): 3200,
    ("phasmatys", "tyras"): 3200,
    ("phasmatys", "corsair"): 4040,
    ("phasmatys", "prifddinas"): 5200,
    ("phasmatys", "piscarilius"): 4000,
    ("phasmatys", "landsend"): 4200,
    ("phasmatys", "fortis"): 4400,
    ("phasmatys", "aldarin"): 4500,
    ("phasmatys", "sunset"): 4500,

    ("shipyard", "tyras"): 3200,
    ("shipyard", "corsair"): 800,
    ("shipyard", "prifddinas"): 4000,
    ("shipyard", "piscarilius"): 2600,
    ("shipyard", "landsend"): 2800,
    ("shipyard", "fortis"): 3000,
    ("shipyard", "aldarin"): 3100,
    ("shipyard", "sunset"): 3100,

    ("tyras", "corsair"): 3200,
    ("tyras", "prifddinas"): 3200,
    ("tyras", "piscarilius"): 3200,
    ("tyras", "landsend"): 3200,
    ("tyras", "fortis"): 3200,
    ("tyras", "aldarin"): 3200,
    ("tyras", "sunset"): 3200,

    ("corsair", "prifddinas"): 1420,
    ("corsair", "piscarilius"): 1500,
    ("corsair", "landsend"): 1800,
    ("corsair", "fortis"): 2000,
    ("corsair", "aldarin"): 2100,
    ("corsair", "sunset"): 2100,

    ("prifddinas", "piscarilius"): 1200,
    ("prifddinas", "landsend"): 1500,
    ("prifddinas", "fortis"): 1700,
    ("prifddinas", "aldarin"): 1800,
    ("prifddinas", "sunset"): 1800,

    ("piscarilius", "fortis"): 1000,
    ("piscarilius", "aldarin"): 1100,
    ("piscarilius", "sunset"): 1100,

    ("landsend", "fortis"): 800,
    ("landsend", "aldarin"): 900,
    ("landsend", "sunset"): 900,

    ("fortis", "aldarin"): 600,
    ("fortis", "sunset"): 500,
}

# The bias found on chartering_destination_port_coord. See the module docstring.
PLANE_BIAS = 1 << 28

# Routes the game does not offer, as pairs. Mirrors clientscript 9104's
# same-port-pair exclusions; asserted against FARES so the two cannot drift.
NOT_OFFERED = {
    frozenset(("sarim", "musa")),
    frozenset(("sarim", "piscarilius")),
    frozenset(("sarim", "landsend")),
    frozenset(("mosle", "phasmatys")),
    frozenset(("piscarilius", "landsend")),
    frozenset(("aldarin", "sunset")),
}


def die(msg: str) -> None:
    sys.stderr.write("gen_charter_tables: %s\n" % msg)
    raise SystemExit(1)


def read_zone(suffix: str, raw: str):
    """The `inzone` tuple as ((sw_x, sw_z), (ne_x, ne_z)), or None if unset."""
    parts = [p for p in raw.split(",") if p.strip()]
    if len(parts) != 2:
        return None
    corners = []
    for value in (int(parts[0]), int(parts[1])):
        if value >> 28 != 0:
            die("inzone for %s carries plane nibble %d — the tuple columns were "
                "assumed unbiased; re-derive before using them"
                % (suffix, value >> 28))
        corners.append(((value >> 14) & 0x3FFF, value & 0x3FFF))
    if corners[0] == (0, 0) and corners[1] == (0, 0):
        return None
    if corners[1][0] < corners[0][0] or corners[1][1] < corners[0][1]:
        die("inzone for %s is not south-west then north-east: %r"
            % (suffix, corners))
    return (corners[0], corners[1])


def read_cache_destinations() -> dict:
    """suffix -> {id, name, coord:(x,z) or None, related_content}."""
    with open(ALL_DBROW, "rb") as f:
        text = f.read().decode("utf-8", "replace")
    out = {}
    pattern = r"\[chartering_destination_([a-z_0-9]+)\]\n(.*?)(?=\n\[|\Z)"
    for suffix, body in re.findall(pattern, text, re.S):
        values = {}
        for m in re.finditer(r"^values=(\d+):0:(.*)$", body, re.M):
            values[int(m.group(1))] = m.group(2).strip()
        raw = int(values.get(3, "0"))
        nibble = raw >> 28
        if raw == 0:
            coord = None
        else:
            coord = (((raw - nibble * PLANE_BIAS) >> 14) & 0x3FFF, raw & 0x3FFF)
        # `inzone` is a two-value column and, unlike the single `port_coord`,
        # carries no plane bias — a tuple is null-or-both and needs no sentinel.
        # Asserted rather than assumed, in read_zone().
        zone = read_zone(suffix, values.get(6, ""))
        out[suffix] = {
            "id": int(values.get(0, "-1")),
            "name": values.get(1, ""),
            "coord": coord,
            "nibble": nibble,
            "zone": zone,
            "related_content": int(values.get(8, "0")),
        }
    if not out:
        die("no chartering_destination_* rows in %s" % ALL_DBROW)
    return out


def coord_literal(x: int, z: int, level: int = 0) -> str:
    return "%d_%d_%d_%d_%d" % (level, x // 64, z // 64, x % 64, z % 64)


def check_symmetry() -> None:
    """Re-derive the claim that the wiki matrix is symmetric."""
    for (a, b) in FARES:
        if (b, a) in FARES:
            die("FARES holds both directions of %s<->%s; author each pair once"
                % (a, b))


def check_not_offered() -> None:
    symbols = [sym for _, sym, _ in PORTS]
    for pair in NOT_OFFERED:
        a, b = sorted(pair)
        if (a, b) in FARES or (b, a) in FARES:
            die("%s<->%s is in NOT_OFFERED but also has a fare" % (a, b))
    for i, a in enumerate(symbols):
        for b in symbols[i + 1:]:
            has = (a, b) in FARES or (b, a) in FARES
            excluded = frozenset((a, b)) in NOT_OFFERED
            if not has and not excluded:
                die("no fare and no exclusion for %s<->%s" % (a, b))


def check_tsv(ports: dict) -> list:
    """Cross-check arrival tiles against the vendored transport dump."""
    notes = []
    if not os.path.exists(TSV):
        return ["%s absent — arrival tiles not cross-checked" % TSV]
    seen = {}
    with open(TSV, "r", encoding="utf-8") as f:
        for line in f:
            fields = line.rstrip("\n").split("\t")
            if len(fields) < 7 or fields[0].startswith("#") or not fields[0].strip():
                continue
            label = fields[6].strip()
            parts = fields[1].split()
            if len(parts) != 3:
                continue
            seen.setdefault(label, (int(parts[0]), int(parts[1])))
    by_name = {p["name"]: p for p in ports.values()}
    by_name["Shipyard"] = by_name.get("Karamja Shipyard", {})
    for label, (x, z) in sorted(seen.items()):
        port = by_name.get(label)
        if not port or not port.get("coord"):
            continue
        cx, cz = port["coord"]
        drift = abs(cx - x) + abs(cz - z)
        if drift > 5:
            notes.append(
                "%s: cache tile (%d,%d) is %d tiles from the tsv's (%d,%d)"
                % (label, cx, cz, drift, x, z))
    return notes


def emit_port_dbrow(ports: dict) -> str:
    lines = [
        "// The sixteen reachable charter ship ports. Generated by",
        "// tools/gen_charter_tables.py — do not hand-edit; see",
        "// docs/transport/CHARTER_SHIPS.md §3.",
        "//",
        "// `port` is chartering_destination_id (column 0 of the cache's own",
        "// dbtable 206), which is what chartering_previous_destination (varbit",
        "// 11209) holds and what the multinpc children are indexed by. It is not",
        "// ours to choose.",
        "//",
        "// `arrive` is chartering_destination_port_coord (column 3) of the same",
        "// row, with the level+1 bias its raw int carries removed.",
        "//",
        "// `name` is column 1 verbatim — it is the string the fare prompt and the",
        "// arrival message name, so it must read as the game writes it.",
        "//",
        "// `zone_sw` / `zone_ne` are the two halves of chartering_destination_inzone",
        "// (column 6), the box clientscript 7334 uses to answer \"which port is the",
        "// player standing at\". They are wide — Port Sarim's is 64x128 tiles — and",
        "// they do not overlap, so a player is at one port or at none.",
        "//",
        "// Two cache rows are deliberately absent:",
    ]
    for suffix, why in sorted(SKIPPED.items()):
        if ports[suffix]["related_content"]:
            continue
        lines.append("//   %-14s %s" % (suffix, why))
    lines.append("// and the six Sailing ports, which the cache restricts off itself.")
    lines.append("")
    for suffix, sym, _ in PORTS:
        row = ports[suffix]
        x, z = row["coord"]
        lines.append("[charter_port_%s]" % sym)
        lines.append("table=charter_port")
        lines.append("data=port,%d" % row["id"])
        lines.append("data=name,%s" % row["name"])
        lines.append("data=arrive,%s" % coord_literal(x, z))
        (sw_x, sw_z), (ne_x, ne_z) = row["zone"]
        lines.append("data=zone_sw,%s" % coord_literal(sw_x, sw_z))
        lines.append("data=zone_ne,%s" % coord_literal(ne_x, ne_z))
        lines.append("")
    return "\n".join(lines)


def emit_fare_dbrow(ports: dict) -> str:
    by_symbol = {sym: ports[suffix]["id"] for suffix, sym, _ in PORTS}
    label = {sym: name for _, sym, name in PORTS}
    lines = [
        "// Every charter fare, both directions. Generated by",
        "// tools/gen_charter_tables.py — do not hand-edit; see",
        "// docs/transport/CHARTER_SHIPS.md §6.",
        "//",
        "// Costs are the wiki's own table (%s)," % FARES_URL,
        "// NOT tools/data/shortest_path/transports/charter_ships.tsv, which",
        "// disagrees on money — Musa Point to Brimhaven 200 vs 480, Karamja",
        "// Shipyard to Port Khazard 720 vs 1600, and the whole Sunset Coast row",
        "// exactly halved. The tsv is used for arrival tiles and quest gates only.",
        "//",
        "// `route` is from_id * 32 + to_id. Destination ids run 1..16 here, so 32",
        "// is comfortably clear of collision and the key stays readable in a log.",
        "//",
        "// Fares to and from Mos Le'Harmless are already halved by the wiki,",
        "// because being there at all requires Cabin Fever. They are transcribed",
        "// as published; nothing halves them a second time.",
        "//",
        "// A pair with no row here is a route the game does not offer. Those are",
        "// the NA cells of the wiki matrix and they agree with the same-port-pair",
        "// exclusions in clientscript 9104.",
        "//",
        "@@ROWS@@",
        "",
    ]
    count = 0
    for i, (_, a, _) in enumerate(PORTS):
        for _, b, _ in PORTS:
            if a == b:
                continue
            cost = FARES.get((a, b), FARES.get((b, a)))
            if cost is None:
                continue
            route = by_symbol[a] * 32 + by_symbol[b]
            lines.append("// %s -> %s" % (label[a], label[b]))
            lines.append("[charter_fare_%s_%s]" % (a, b))
            lines.append("table=charter_fare")
            lines.append("data=route,%d" % route)
            lines.append("data=cost,%d" % cost)
            lines.append("")
            count += 1
    offered = len(PORTS) * (len(PORTS) - 1) - 2 * len(NOT_OFFERED)
    if count != offered:
        die("emitted %d fare rows, expected %d" % (count, offered))
    lines[lines.index("@@ROWS@@")] = (
        "// %d rows: %d ports, %d ordered pairs, less %d the game never offers."
        % (count, len(PORTS), len(PORTS) * (len(PORTS) - 1), 2 * len(NOT_OFFERED)))
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()

    check_symmetry()
    check_not_offered()

    ports = read_cache_destinations()
    for suffix, _, name in PORTS:
        if suffix not in ports:
            die("cache has no chartering_destination_%s" % suffix)
        if ports[suffix]["coord"] is None:
            die("chartering_destination_%s has no port_coord" % suffix)
        if ports[suffix]["nibble"] != 1:
            die("port_coord for %s carries plane nibble %d, not the 1 that the "
                "level+1 bias rests on — re-derive it before shipping this row"
                % (suffix, ports[suffix]["nibble"]))
        if ports[suffix]["name"] != name:
            die("cache calls %s %r, this tool calls it %r"
                % (suffix, ports[suffix]["name"], name))
        if ports[suffix]["related_content"]:
            die("chartering_destination_%s is content-restricted" % suffix)
        if ports[suffix]["zone"] is None:
            die("chartering_destination_%s has no inzone box — there would be no "
                "way to tell that a player is standing at it" % suffix)

    # The zones answer "which port am I at", so an overlap would make that
    # question ambiguous and the answer order-dependent.
    boxes = [(s, ports[s]["zone"]) for s, _, _ in PORTS]
    for i, (a, ((ax0, az0), (ax1, az1))) in enumerate(boxes):
        for b, ((bx0, bz0), (bx1, bz1)) in boxes[i + 1:]:
            if ax0 <= bx1 and bx0 <= ax1 and az0 <= bz1 and bz0 <= az1:
                die("inzone boxes for %s and %s overlap" % (a, b))
    for suffix in SKIPPED:
        if suffix not in ports:
            die("SKIPPED names chartering_destination_%s, which the cache no "
                "longer has — re-check the destination set" % suffix)

    ids = [ports[s]["id"] for s, _, _ in PORTS]
    if len(set(ids)) != len(ids):
        die("duplicate destination ids: %r" % ids)

    for note in check_tsv(ports):
        sys.stderr.write("  note: %s\n" % note)

    outputs = {
        "charter_port.dbrow": emit_port_dbrow(ports),
        "charter_fare.dbrow": emit_fare_dbrow(ports),
    }

    changed = 0
    for name, body in sorted(outputs.items()):
        path = os.path.join(OUT_DIR, name)
        old = None
        if os.path.exists(path):
            with open(path, "r", encoding="utf-8") as f:
                old = f.read()
        if old == body:
            print("  unchanged  %s" % name)
            continue
        changed += 1
        if args.write:
            os.makedirs(OUT_DIR, exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                f.write(body)
            print("  wrote      %s (%d lines)" % (name, body.count("\n")))
        else:
            print("  would write %s (%d lines)" % (name, body.count("\n")))

    print("%d port(s), %d route(s), %d file(s) %s"
          % (len(PORTS), outputs["charter_fare.dbrow"].count("[charter_fare_"),
             changed, "written" if args.write else "changed (dry run)"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
