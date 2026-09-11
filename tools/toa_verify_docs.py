#!/usr/bin/env python3
"""toa_verify_docs — check the Tombs of Amascut docs against the cache.

    tools/toa_verify_docs.py

The plan, the encounter log and the asset index quote several hundred numeric
ids. A wrong one is invisible in prose and fatal in a script, so every id the
docs state in an `id`-shaped position is re-read out of
`docs/minigames/tombs_of_amascut/sources/` — which is itself a mechanical dump of
`OSRS-Content/osrs239-content/` — and reported if it does not exist there.

This is a documentation test, not a lint: it is expected to fail loudly when a
doc drifts from the cache, and it is expected to fail if `toa_cache_dump.py` is
re-run against a cache that no longer carries the raid.

Exit status 0 if every check passes, 1 otherwise.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs/minigames/tombs_of_amascut"
SRC = DOCS / "sources"
CONTENT = ROOT / "OSRS-Content/osrs239-content"

failures = []
checks = 0


def check(ok, what):
    global checks
    checks += 1
    if not ok:
        failures.append(what)


def ids_from(name, column=0):
    """Every numeric id in a `cache_*_toa.txt` dump, either flavour of layout."""
    out = set()
    for line in (SRC / name).read_text(encoding="utf-8").splitlines():
        if line.startswith("#"):
            continue
        m = re.match(r"\[(\d+)\] ", line) or re.match(r"(\d+)\t", line)
        if m:
            out.add(int(m.group(1)))
    return out


def doc_text():
    return "\n".join((DOCS / f).read_text(encoding="utf-8")
                     for f in ("TOMBS_OF_AMASCUT_PLAN.md", "ENCOUNTERS.md",
                               "ASSET_INDEX.md", "README.md"))


# --- 1. the twelve map squares -------------------------------------------
REGIONS = {14160: "Nexus", 14162: "Scabaras", 14164: "Kephri", 14672: "vault",
           14674: "Het", 14676: "Akkha", 15184: "Wardens P1", 15186: "Apmeken",
           15188: "Ba-Ba", 15696: "Wardens P2/P3", 15698: "Crondis",
           15700: "Zebak"}
maps = (CONTENT / "pack/5_maps.pack").read_text(encoding="cp1252",
                                                errors="replace")
present = set(re.findall(r"=(m\d+_\d+)$", maps, re.M))
for region, room in REGIONS.items():
    square = f"m{region >> 8}_{region & 0xFF}"
    check(square in present, f"map square {square} ({room}, region {region}) missing")

# --- 2. the invocation table ---------------------------------------------
rows = [line.split("\t") for line in
        (SRC / "cache_invocations_toa.tsv").read_text(encoding="utf-8").splitlines()
        if line and not line.startswith("#")][1:]
check(len(rows) == 46, f"expected 46 invocations, dump has {len(rows)}")

EXCLUSIVE = {3, 4, 5, 6}          # attempts, time limit, helpful spirit, path level
best, rest = {}, 0
for r in rows:
    category, raid_level = int(r[3]), int(r[4])
    if category == 15:            # Leagues, out of scope
        continue
    if category in EXCLUSIVE:
        best[category] = max(best.get(category, 0), raid_level)
    else:
        rest += raid_level
ceiling = rest + sum(best.values())
check(ceiling == 600, f"raid-level ceiling is {ceiling}, wiki says 600")
check(set(best) == EXCLUSIVE, f"exclusive categories present: {sorted(best)}")

prereqs = {int(r[6]) for r in rows if r[6]}
structs = {int(r[1]) for r in rows}
check(prereqs <= structs, f"prerequisite structs not in the table: {prereqs - structs}")

# --- 3. every id the docs quote exists -----------------------------------
text = doc_text()
NPCS = ids_from("cache_npc_toa.txt")
LOCS = ids_from("cache_loc_toa.txt")
OBJS = ids_from("cache_obj_toa.txt")
SEQS = ids_from("cache_seq_toa.txt")

# `npc **11730**`, `loc **45455**`, `obj 27295`, `seq 9490` and the table forms.
for kind, universe in (("npc", NPCS), ("loc", LOCS), ("obj", OBJS), ("seq", SEQS)):
    quoted = {int(m) for m in
              re.findall(rf"\b{kind}s? \*?\*?(\d{{3,5}})\*?\*?", text, re.I)}
    missing = quoted - universe
    check(not missing, f"{kind} ids quoted in the docs but absent from the cache dump: {sorted(missing)}")

# Bare `**NNNNN**` ids in the asset index's id tables must resolve somewhere.
index = (DOCS / "ASSET_INDEX.md").read_text(encoding="utf-8")
known = NPCS | LOCS | OBJS | SEQS | ids_from("cache_spotanim_toa.txt") \
    | ids_from("cache_sound_toa.txt") | ids_from("cache_varbit_toa.txt")
for section in index.split("\n## ")[1:]:
    if not section.startswith(("2.", "3.", "4.")):
        continue
    stray = {int(m) for m in re.findall(r"\*\*(\d{5})\*\*", section)} - known
    check(not stray, f"unresolvable id in ASSET_INDEX section {section[:4]}: {sorted(stray)}")

# --- 4. music -------------------------------------------------------------
music = (ROOT / "docs/audio/music_tracks_osrs239.tsv").read_bytes().decode(
    "cp1252", errors="replace")
toa_tracks = [line.split("\t") for line in music.splitlines()
              if "Tombs of Amascut" in line]
check(len(toa_tracks) == 12, f"expected 12 ToA music tracks, found {len(toa_tracks)}")
archives = {int(t[5]) for t in toa_tracks}
for archive in archives:
    check(f"| {archive} |" in text,
          f"music archive {archive} is in the cache but not in the docs")

# --- 5. combat achievements ----------------------------------------------
ca = [line.split("\t") for line in
      (SRC / "wiki_combat_achievements_toa.tsv").read_text(encoding="utf-8").splitlines()][1:]
check(len(ca) == 51, f"expected 51 combat achievements, have {len(ca)}")
ca_ids = sorted(int(r[0]) for r in ca)
check(ca_ids == list(range(421, 472)),
      f"combat achievement ids are not 421-471: {ca_ids[0]}-{ca_ids[-1]}")

# --- 6. every open task is declared in the plan's collected table ---------
plan = (DOCS / "TOMBS_OF_AMASCUT_PLAN.md").read_text(encoding="utf-8")
collected = set(re.findall(r"\| `(M\d+|D\d+)` \|", plan))
used = set(re.findall(r"`(M\d+|D\d+)`", text)) - {"M0"}
check(used <= collected,
      f"open tasks used but not collected in the plan's table: {sorted(used - collected)}")

# --- report ---------------------------------------------------------------
for f in failures:
    print(f"FAIL  {f}")
print(f"{checks - len(failures)}/{checks} checks passed")
sys.exit(1 if failures else 0)
