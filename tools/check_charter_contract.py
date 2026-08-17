#!/usr/bin/env python3
"""Structural checks for charter ships.

Run by the mock230-scripts build. What it defends, in order of how badly each
would fail in the game rather than in the compiler:

  1. Every port row still agrees with the cache's own dbtable 206
     (`chartering_destinations`) on id, display name and arrival tile. Those are
     copied out of the cache by tools/gen_charter_tables.py, and a cache refresh
     that moved a jetty would otherwise sail players into the sea.

  2. Every arrival tile lands inside its own port's `inzone` box, and no two
     boxes overlap. `~charter_port_here` walks those boxes to answer "which port
     am I at". If an arrival tile fell outside its box, the return trip would
     report "there's no jetty here" from the deck of the ship that just docked.

  3. The fare matrix is complete and symmetric: 240 ordered pairs less the
     twelve the game never offers, every one present, every one matching its
     reverse. A missing row reads as "route not offered" at runtime, which is a
     silent failure -- the destination simply stops appearing.

  4. The six not-offered pairs are exactly the ones clientscript 9104 excludes.
     The client hides those pins; if the server offered them anyway the phase-3
     map picker and the chat menu would disagree.

  5. Every quest gate in ~charter_port_unlocked still names the threshold
     constant the cache's own clientscript 9104 tests, and those constants still
     hold the values it compares against. A quest that renumbers its stages must
     break this, not the charter network.

  6. The two cache rows deliberately left out (Crandor, Tempestus) are still
     absent from charter_port.dbrow, and still present in the cache with the
     properties that justify leaving them out.

  7. Spawned traders are multinpc PARENTS, never resolved leaves.

  8. The row order the picker decodes against is still the order the CLIENT
     walks. `charter_map.rs2` turns a pin's sub-id into a row index and reads
     that row out of dbtable 206 with `db_listall`/`db_findbyindex`; the client
     builds the pins by walking the same table with `db_findall`. The cache
     states that order itself, in `dbindex/dbindex_206.dbi`'s `[master]` block —
     "every row id in the table, which DB_FINDALL returns". This asserts that
     block still matches the order `configs/all.dbrow` presents the rows in,
     which is the order this engine's `db_listall` produces. If they ever
     diverge the player clicks Catherby and sails to Prifddinas, and nothing
     else in the tree would notice.

Doc-vs-code agreement is checked too: docs/transport/CHARTER_SHIPS.md carries
the destination table and the fare matrix in prose, and a plan that has drifted
from the data is worse than no plan.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "OSRS-Content/osrs239-content"
FEATURE = BASE / "server/scripts/transport_charter"

ALL_DBROW = (BASE / "configs/all.dbrow").read_bytes().decode("utf-8", "replace")
PORT_DBROW = (FEATURE / "configs/charter_port.dbrow").read_text()
FARE_DBROW = (FEATURE / "configs/charter_fare.dbrow").read_text()
PORT_DBTABLE = (FEATURE / "configs/charter_port.dbtable").read_text()
FARE_DBTABLE = (FEATURE / "configs/charter_fare.dbtable").read_text()
CONSTANT = (FEATURE / "configs/charter.constant").read_text()
SPAWN = (FEATURE / "configs/charter.spawn").read_text()
PORT_RS2 = (FEATURE / "scripts/charter_port.rs2").read_text()
MAP_RS2 = (FEATURE / "scripts/charter_map.rs2").read_text()
DBINDEX = BASE / "dbindex/dbindex_206.dbi"
TRAVEL_RS2 = (FEATURE / "scripts/charter_travel.rs2").read_text()
DOC = (ROOT / "docs/transport/CHARTER_SHIPS.md").read_text()

# clientscript 9104's own exclusions, by cache dbrow id. Transcribed from
# OSRS-Content/osrs239-content/scripts/script_9104.cs2.
CLIENT_EXCLUSIONS = {
    frozenset(("Port Sarim", "Musa Point")),
    frozenset(("Port Sarim", "Port Piscarilius")),
    frozenset(("Port Sarim", "Land's End")),
    frozenset(("Mos Le'Harmless", "Port Phasmatys")),
    frozenset(("Port Piscarilius", "Land's End")),
    frozenset(("Aldarin", "Sunset Coast")),
}

# ~charter_port_unlocked's gates: port symbol -> (constant, expected value,
# the varp/varbit clientscript 9104 reads and the threshold it compares).
GATES = {
    "charter_mosle": [("fever_complete", 140), ("priestperil_access_holy_barrier", 61)],
    "charter_phasmatys": [("priestperil_access_holy_barrier", 61)],
    "charter_shipyard": [("monkeymadness_shown_seal", 2), ("grandtree_complete", 160)],
    "charter_tyras": [("regicide_complete", 15)],
    "charter_prifddinas": [("sote_complete", 200)],
}

CONSTANT_SOURCES = [
    "server/scripts/quests/quest_cabinfever/configs/cabinfever.constant",
    "server/scripts/quests/quest_priestperil/configs/quest_priestperil.constant",
    "server/scripts/quests/quest_mm/configs/quest_mm.constant",
    "server/scripts/quests/quest_grandtree/configs/quest_grandtree.constant",
    "server/scripts/quests/quest_regicide/configs/quest_regicide.constant",
    "server/scripts/quests/quest_songoftheelves/configs/songoftheelves.constant",
]

PLANE_BIAS = 1 << 28
FAILURES: list[str] = []


def fail(msg: str) -> None:
    FAILURES.append(msg)


def unlit(literal: str) -> tuple[int, int, int]:
    level, mx, mz, lx, lz = (int(v) for v in literal.split("_"))
    return level, mx * 64 + lx, mz * 64 + lz


def parse_dbrows(text: str, prefix: str) -> dict:
    out = {}
    pattern = r"\[%s(\w+)\]\n(.*?)(?=\n\[|\Z)" % re.escape(prefix)
    for name, body in re.findall(pattern, text, re.S):
        out[name] = dict(re.findall(r"^data=(\w+),(.*)$", body, re.M))
    return out


def parse_cache_destinations() -> dict:
    out = {}
    pattern = r"\[chartering_destination_([a-z_0-9]+)\]\n(.*?)(?=\n\[|\Z)"
    for suffix, body in re.findall(pattern, ALL_DBROW, re.S):
        values = {int(m.group(1)): m.group(2).strip()
                  for m in re.finditer(r"^values=(\d+):0:(.*)$", body, re.M)}
        raw = int(values.get(3, "0"))
        coord = None
        if raw:
            coord = (((raw - (raw >> 28) * PLANE_BIAS) >> 14) & 0x3FFF, raw & 0x3FFF)
        zone = None
        parts = [p for p in values.get(6, "").split(",") if p.strip()]
        if len(parts) == 2:
            a, b = int(parts[0]), int(parts[1])
            if (a, b) != (0, 0):
                zone = (((a >> 14) & 0x3FFF, a & 0x3FFF), ((b >> 14) & 0x3FFF, b & 0x3FFF))
        out[suffix] = {
            "id": int(values.get(0, "-1")),
            "name": values.get(1, ""),
            "coord": coord,
            "zone": zone,
            "related_content": int(values.get(8, "0")),
        }
    return out


def main() -> int:
    cache = parse_cache_destinations()
    ports = parse_dbrows(PORT_DBROW, "charter_port_")
    fares = parse_dbrows(FARE_DBROW, "charter_fare_")

    if not ports:
        fail("charter_port.dbrow has no rows")
        return report()

    # --- 1. every port still agrees with dbtable 206 ---------------------
    by_name = {c["name"]: c for c in cache.values()}
    ids, names = {}, {}
    for sym, row in ports.items():
        pid = int(row["port"])
        name = row["name"]
        ids[sym] = pid
        names[pid] = name
        src = by_name.get(name)
        if src is None:
            fail("charter_port_%s: %r is not a chartering_destinations name" % (sym, name))
            continue
        if src["id"] != pid:
            fail("charter_port_%s: id %d, cache says %d" % (sym, pid, src["id"]))
        if src["coord"] is None:
            fail("charter_port_%s: cache row has no port_coord" % sym)
            continue
        _, x, z = unlit(row["arrive"])
        if (x, z) != src["coord"]:
            fail("charter_port_%s: arrive (%d,%d), cache port_coord %r"
                 % (sym, x, z, src["coord"]))
        if src["zone"] is None:
            fail("charter_port_%s: cache row has no inzone" % sym)
        else:
            (csw, cne) = src["zone"]
            _, sx, sz = unlit(row["zone_sw"])
            _, nx, nz = unlit(row["zone_ne"])
            if (sx, sz) != csw or (nx, nz) != cne:
                fail("charter_port_%s: inzone (%d,%d)-(%d,%d), cache %r"
                     % (sym, sx, sz, nx, nz, src["zone"]))
        if src["related_content"]:
            fail("charter_port_%s: cache marks it content-restricted" % sym)

    # --- 2. arrival tiles inside their own box, boxes disjoint -----------
    boxes = {}
    for sym, row in ports.items():
        _, sx, sz = unlit(row["zone_sw"])
        _, nx, nz = unlit(row["zone_ne"])
        _, ax, az = unlit(row["arrive"])
        boxes[sym] = (sx, sz, nx, nz)
        if not (sx <= ax <= nx and sz <= az <= nz):
            fail("charter_port_%s: arrival (%d,%d) is outside its own inzone box "
                 "— ~charter_port_here would not recognise the port after landing"
                 % (sym, ax, az))
    items = sorted(boxes.items())
    for i, (a, (ax0, az0, ax1, az1)) in enumerate(items):
        for b, (bx0, bz0, bx1, bz1) in items[i + 1:]:
            if ax0 <= bx1 and bx0 <= ax1 and az0 <= bz1 and bz0 <= az1:
                fail("inzone boxes for %s and %s overlap" % (a, b))

    # --- 3. fare matrix complete and symmetric ---------------------------
    stride = int(re.search(r"\^charter_route_stride = (\d+)", CONSTANT).group(1))
    cost = {}
    for sym, row in fares.items():
        route, c = int(row["route"]), int(row["cost"])
        frm, to = divmod(route, stride)
        if frm not in names or to not in names:
            fail("charter_fare_%s: route %d names a port not in charter_port" % (sym, route))
            continue
        if c <= 0:
            fail("charter_fare_%s: cost %d" % (sym, c))
        cost[(frm, to)] = c
    n = len(ports)
    expected = n * (n - 1) - 2 * len(CLIENT_EXCLUSIONS)
    if len(cost) != expected:
        fail("%d fare rows, expected %d (%d ports, less %d excluded pairs both ways)"
             % (len(cost), expected, n, len(CLIENT_EXCLUSIONS)))
    for (frm, to), c in sorted(cost.items()):
        back = cost.get((to, frm))
        if back is None:
            fail("%s -> %s has a fare but %s -> %s does not"
                 % (names[frm], names[to], names[to], names[frm]))
        elif back != c:
            fail("%s -> %s is %d but the reverse is %d"
                 % (names[frm], names[to], c, back))

    # --- 4. the missing pairs are exactly the client's exclusions --------
    missing = set()
    idlist = sorted(names)
    for frm in idlist:
        for to in idlist:
            if frm != to and (frm, to) not in cost:
                missing.add(frozenset((names[frm], names[to])))
    if missing != CLIENT_EXCLUSIONS:
        fail("pairs with no fare %r do not match clientscript 9104's exclusions %r"
             % (sorted(map(sorted, missing)), sorted(map(sorted, CLIENT_EXCLUSIONS))))

    # --- 5. quest gates still name live constants with live values ------
    defined = {}
    for rel in CONSTANT_SOURCES:
        text = (BASE / rel).read_text()
        defined.update({m.group(1): int(m.group(2))
                        for m in re.finditer(r"^\^(\w+) = (-?\d+)$", text, re.M)})
    for port_symbol, gates in GATES.items():
        # The gate opens `if ($port = ^charter_x) {` and closes on a `}` in
        # column 0; every `}` inside it is indented.
        block = re.search(r"if \(\$port = \^%s[^\n]*\n(.*?)\n\}" % port_symbol,
                          PORT_RS2, re.S)
        if not block:
            fail("~charter_port_unlocked has no gate for ^%s" % port_symbol)
            continue
        for const, value in gates:
            if "^" + const not in block.group(1):
                fail("^%s gate does not test ^%s" % (port_symbol, const))
            if defined.get(const) != value:
                fail("^%s is %r, clientscript 9104 compares against %d"
                     % (const, defined.get(const), value))

    # --- 6. the two deliberate omissions --------------------------------
    for suffix, why in (("crandor", "port_coord"), ("tempestus", "clientscript 9104")):
        if suffix not in cache:
            fail("cache no longer has chartering_destination_%s — re-check the "
                 "destination set before trusting the omission" % suffix)
        if any(int(r["port"]) == cache.get(suffix, {}).get("id") for r in ports.values()):
            fail("chartering_destination_%s is in charter_port.dbrow but was "
                 "excluded for a reason that still holds (%s)" % (suffix, why))
    if cache.get("crandor", {}).get("coord") is not None:
        fail("Crandor now has a port_coord — it can be a real destination, and "
             "the exclusion in charter.constant should go")

    # --- 7. spawns are parents, not leaves -------------------------------
    parents = {
        "sailing_transport_trader_stan",
        "sailing_transport_trader_stan_crew_man1",
        "sailing_transport_trader_stan_crew_man2",
        "sailing_transport_trader_stan_crew_man3",
        "sailing_transport_trader_stan_crew_woman1",
        "sailing_transport_trader_stan_crew_woman2",
        "sailing_transport_trader_stan_crew_woman3",
    }
    spawned = re.findall(r"^(sailing_transport_\S+)\s+(\d+)\s+(\d+)\s+(\d+)$",
                         SPAWN, re.M)
    if not spawned:
        fail("charter.spawn has no npc rows")
    for name, x, z, level in spawned:
        if name not in parents:
            fail("charter.spawn spawns %r, which is a resolved leaf — it would "
                 "show one fixed Charter-to op for every player" % name)
        inside = [s for s, (sx, sz, nx, nz) in boxes.items()
                  if sx <= int(x) <= nx and sz <= int(z) <= nz]
        if len(inside) != 1:
            fail("charter.spawn tile (%s,%s) is inside %d port zones, not 1"
                 % (x, z, len(inside)))
        if level != "0":
            fail("charter.spawn tile (%s,%s) is on level %s" % (x, z, level))
    for op in ("opnpc3", "opnpc4", "opnpc5"):
        bound = set(re.findall(r"^\[%s,(\S+)\]$" % op,
                               (FEATURE / "scripts/charter_npc.rs2").read_text()
                               + (FEATURE / "scripts/charter_shop.rs2").read_text(), re.M))
        if not parents <= bound:
            fail("%s is not bound on every multinpc parent: missing %r"
                 % (op, sorted(parents - bound)))

    # --- 8. picker row order still matches the client's ------------------
    if not DBINDEX.exists():
        fail("%s is missing — the picker's row order cannot be checked" % DBINDEX)
    else:
        master = re.search(r"\[master\].*?index=0:0:([0-9,]+)", DBINDEX.read_text(), re.S)
        if not master:
            fail("dbindex_206.dbi has no [master] index block")
        else:
            want = [int(v) for v in master.group(1).split(",")]
            row_ids = {}
            for line in (BASE / "configs/all.dbrow.compack").read_bytes().decode(
                    "utf-8", "replace").splitlines():
                m = re.match(r"^(\d+)=(\S+)$", line.strip())
                if m:
                    row_ids[m.group(2)] = int(m.group(1))
            got = []
            for name, body in re.findall(
                    r"\[(\w+)\]\n(.*?)(?=\n\[|\Z)", ALL_DBROW, re.S):
                if re.search(r"^table=chartering_destinations$", body, re.M):
                    got.append(row_ids.get(name))
            if got != want:
                fail("dbtable 206 row order: all.dbrow presents %r but the cache's "
                     "own [master] index says the client walks %r — charter_map.rs2 "
                     "decodes pin sub-ids against the first and the client builds "
                     "them from the second" % (got[:6], want[:6]))
            # The sub-id arithmetic the picker and the selftest both rely on.
            for name, expect in (("catherby", 5), ("porttyras", 17)):
                rid = row_ids.get("chartering_destination_" + name)
                if rid in want:
                    sub = want.index(rid) * 2 + 1
                    if sub != expect:
                        fail("%s's pin sub-id is %d, not the %d the selftest clicks"
                             % (name, sub, expect))

    for needle, label in (
        ("db_listall(chartering_destinations)", "charter_map.rs2 walks the cache table"),
        ("db_findbyindex", "charter_map.rs2 reads the row by index"),
        ("if_openmain_side(sailing_menu, chartering_menu_side)",
         "charter_map.rs2 opens both halves of the cache picker"),
    ):
        if needle not in MAP_RS2:
            fail("%s — %r is gone" % (label, needle))
    # Speech needs an active entity; an [if_button1] has none. See the comment on
    # ~charter_locked_mes. This is the check that a future edit cannot quietly
    # reintroduce the abort.
    for path, text in (("charter_port.rs2", PORT_RS2), ("charter_travel.rs2", TRAVEL_RS2),
                       ("charter_map.rs2", MAP_RS2)):
        for line in text.split("\n"):
            if line.strip().startswith("~chatnpc"):
                fail("%s calls %s on a path an [if_button1] can reach — NPC_TYPE "
                     "aborts there" % (path, line.strip().split("(")[0]))

    # --- doc agreement ---------------------------------------------------
    for sym, row in sorted(ports.items()):
        if row["name"] not in DOC:
            fail("docs/transport/CHARTER_SHIPS.md does not mention %r" % row["name"])
        if row["arrive"] not in DOC:
            fail("docs/transport/CHARTER_SHIPS.md does not carry %s's arrival "
                 "literal %s" % (row["name"], row["arrive"]))
    for needle, label in (
        ("INDEXED", "charter_port.dbtable keeps port INDEXED"),
        ("column=zone_sw,coord", "charter_port.dbtable declares zone_sw"),
    ):
        if needle not in PORT_DBTABLE:
            fail(label)
    if "INDEXED" not in FARE_DBTABLE:
        fail("charter_fare.dbtable no longer indexes route — ~charter_fare "
             "would degrade to a scan")

    return report()


def report() -> int:
    if FAILURES:
        for line in FAILURES:
            sys.stderr.write("check_charter_contract: %s\n" % line)
        sys.stderr.write("check_charter_contract: %d failure(s)\n" % len(FAILURES))
        return 1
    print("check_charter_contract: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
