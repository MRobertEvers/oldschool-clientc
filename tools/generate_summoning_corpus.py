#!/usr/bin/env python3
"""Generate the narrow Phase-5c Summoning policy artifacts.

The allowlist is intentionally data in this generator, rather than a
predicate over ``pouches_530.json``.  The broad 530 import is only used as a
checked extraction input for model/animation closure; it is never rewritten.
"""
from __future__ import annotations

import argparse
import configparser
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
POUCHES = ROOT / "docs/summoning_port/pouches_530.json"
PRIMARY = ROOT / "OSRS-Content/osrs239-content/port/summoning_530.map"
ROSTER_NPC = ROOT / "OSRS-Content/osrs239-content/ported/scape2009_summoning/configs/summoning_roster_530.npc"
ROSTER_OBJ = ROOT / "OSRS-Content/osrs239-content/ported/scape2009_summoning/configs/summoning_roster_530.obj"
OUT_CATALOG = ROOT / "docs/summoning_port/corpus_cohort_530.json"
OUT_MANIFEST = ROOT / "docs/summoning_port/corpus_cohort_530.ini"
OUT_LEDGER = ROOT / "OSRS-Content/osrs239-content/port/summoning_corpus_530.map"

PAIRS = [
    (6841, 12059), (7331, 12778), (6837, 12055), (7361, 12808),
    (7353, 12800), (6835, 12053), (6845, 12065), (7333, 12780),
    (7351, 12798), (7367, 12814), (6853, 12073), (6855, 12075),
    (6857, 12077), (6859, 12079), (6861, 12081), (6863, 12083),
    (7377, 12816), (6843, 12061), (6992, 12027), (7365, 12812),
    (7337, 12784), (7363, 12810), (6809, 12023), (6865, 12085),
    (6802, 12015), (6889, 12123), (6813, 12029), (7372, 12820),
    (6839, 12057), (7345, 12792), (6798, 12011), (7335, 12782),
    (7347, 12794), (6811, 12025), (6804, 12017), (6822, 12039),
    (6869, 12089), (7355, 12802), (7357, 12804), (7359, 12806),
    (7341, 12788), (7329, 12776), (7339, 12786), (7375, 12822),
    (7343, 12790),
]

# Source Familiar scheduling values, in game ticks (the source table reports
# minutes; the rev239 implementation consumes one hundred ticks per minute).
MINUTES = {
    "spirit_spider": 15, "spirit_mosquito": 12, "spirit_scorpion": 17,
    "spirit_tz_kih": 16, "giant_chinchompa": 31, "vampire_bat": 33,
    "honey_badger": 25, "void_spinner": 27, "void_torcher": 94,
    "void_shifter": 94, "bronze_minotaur": 30, "iron_minotaur": 37,
    "steel_minotaur": 46, "mithril_minotaur": 55, "adamant_minotaur": 66,
    "rune_minotaur": 151, "pyrelord": 32, "bloated_leech": 34,
    "spirit_jelly": 43, "spirit_kyatt": 49, "spirit_larupia": 49,
    "spirit_graahk": 49, "karamthulhu": 44, "smoke_devil": 41,
    "spirit_cobra": 56, "barker_toad": 8, "bunyip": 44,
    "ravenous_locust": 24, "arctic_bear": 28, "obsidian_golem": 55,
    "praying_mantis": 69, "forge_regent_beast": 45, "talon_beast": 49,
    "hydra": 49, "spirit_dagannoth": 57, "unicorn_stallion": 54,
    "wolpertinger": 62, "fire_titan": 62, "moss_titan": 64,
    "ice_titan": 62, "lava_titan": 61, "swamp_titan": 56,
    "geyser_titan": 69, "iron_titan": 60, "steel_titan": 64,
}

def rows(path: Path):
    for line_no, line in enumerate(path.read_text().splitlines(), 1):
        if not line or line.startswith("#") or line.startswith("kind"):
            continue
        fields = line.split("\t")
        if len(fields) == 7:
            yield fields, line_no

def primary_by(kind: str):
    return {int(f[3]): (int(f[1]), f[2], f[4]) for f, _ in rows(PRIMARY)
            if f[0] == kind and f[3].isdigit()}

def records(path: Path):
    current = None
    result = {}
    for line in path.read_text().splitlines():
        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1]
            result[current] = {}
        elif current and "=" in line:
            k, v = line.split("=", 1)
            result[current][k] = v
    return result

def source_name(value: str) -> str:
    return value.removeprefix("summoning_roster_530_")

def build(check: bool = False):
    pouch_rows = json.loads(POUCHES.read_text())
    by_pair = {(int(x["npc"]), int(x["pouch"])): x for x in pouch_rows}
    npc_records, obj_records = records(ROSTER_NPC), records(ROSTER_OBJ)
    npc_map, obj_map = primary_by("npc"), primary_by("obj")
    # Broad generated records are a checked extraction input, never output.
    profiles = []
    for index, (npc, pouch) in enumerate(PAIRS):
        raw = by_pair[(npc, pouch)]
        name = source_name(next(v[2] for v in npc_map.values() if v[0] == npc))
        familiar_source_name = raw["name"].lower().removesuffix("_pouch").replace("_pouch_", "_")
        pouch_source_name = raw["name"].lower()
        rec = npc_records[f"summoning_roster_530_{name}"]
        pouch_rec = obj_records.get(f"summoning_roster_530_{name}_pouch", {})
        model_fields = [k for k in rec if k.startswith("model") or k.startswith("head")]
        # The source extraction records the already-decoded target names; the
        # importer maps those names back to source IDs in the dedicated ledger.
        model_sources = []
        for key in model_fields:
            model_sources.append(int(rec[key]))
        pouch_target_suffix = "_pouch" if pouch_source_name.endswith("_pouch") else ""
        profiles.append({
            "type": index + 3,
            "source_npc": npc,
            "source_pouch": pouch,
            "source_name": familiar_source_name,
            "slot": int(raw["slot"]),
            "display_name": rec.get("name", name.replace("_", " ").title()),
            "level": int(raw["lvl"]),
            "cost": int(raw["cost"]),
            "summon_xp": raw["sxp"],
            "lifetime_ticks": MINUTES[name] * 100,
            "drain": {"model": "accumulator_mod_100", "interval_ticks": 100, "points": 1},
            "target": {
                "npc": 26016 + index,
                "pouch": 46016 + index,
                "name": f"summoning_cohort_corpus_{name}",
                "pouch_name": f"summoning_cohort_corpus_{name}{pouch_target_suffix}",
            },
            "source_fields": {
                "pouch": f"docs/summoning_port/pouches_530.json#entry[{pouch_rows.index(raw) + 1}]",
                "render": f"ported/scape2009_summoning/configs/summoning_roster_530.npc:[summoning_roster_530_{name}]",
                "closure": "OSRS-Content/osrs239-content/port/summoning_530.map (checked extraction input)",
            },
            "body_head_source_targets": model_sources,
            "ready_source_target": rec.get("readyanim", ""),
            "walk_source_target": rec.get("walkanim", ""),
            "deferred_capabilities": ["special", "scroll", "combat", "audio", "tertiary", "potion"],
        })
    assert len(profiles) == 45
    catalog = {
        "schema": 1, "phase": "5c", "prefix": "summoning_cohort_corpus",
        "source_manifest": "pouches_530.json", "source_policy": "fixed_pair_allowlist",
        "preserved_type_ids": {"none": 0, "spirit_wolf": 1, "dreadfowl": 2},
        "profiles": profiles,
        "deferred": {"phase_6_roots": [6794,6796,6800,6806,6808,6815,6817,6818,6820,6824,6827,6831,6833,6847,6849,6851,6867,6871,6873,6875,6877,6879,6881,6883,6885,6887,6991,6994,7349,7370], "phoenix": 7369, "sacred_clay": 4},
        "expected_closure": {"npc": 45, "obj": 45},
    }
    manifest = ["# Generated by tools/generate_summoning_corpus.py", "[import:scape2009]", "from_rev=rs530", "from_cache=../../../2009scape/Server/data/cache", "to_rev=osrs239", "to_tree=../../OSRS-Content/osrs239-content", "lane=ported/scape2009_summoning", "ledger=port/summoning_corpus_530.map", "prefix=summoning_cohort_corpus", "npc_base=26016", "obj_base=46016", "model_base=120032", "seq_base=23032", "animset_base=23032", "framemap_base=10032", "npc_sounds=no", ""]
    manifest += ["[export:npc]"] + [f"{x['source_npc']}={source_name(x['target']['name'])}" for x in profiles]
    manifest += ["", "[export:obj]"] + [f"{x['source_pouch']}={source_name(x['target']['pouch_name'])}" for x in profiles]
    ledger = ["# Generated root closure; asset closure remains blocked until the rev530 source cache is restored.", "kind\tsrc_id\tsrc_name\tdst_id\tdst_name\tdisposition\tsignoff"]
    for x in profiles:
        name = x["source_name"]
        pouch_name = next(raw["name"].lower() for raw in pouch_rows if int(raw["pouch"]) == x["source_pouch"])
        ledger.append(f"npc\t{x['source_npc']}\t{name}\t{x['target']['npc']}\t{x['target']['name']}\tminted\tunreviewed")
        ledger.append(f"obj\t{x['source_pouch']}\t{pouch_name}\t{x['target']['pouch']}\t{x['target']['pouch_name']}\tminted\tunreviewed")
    if check:
        actual = json.loads(OUT_CATALOG.read_text())
        if actual != catalog or OUT_MANIFEST.read_text().splitlines() != manifest or OUT_LEDGER.read_text().splitlines() != ledger:
            raise SystemExit("corpus catalog/manifest is stale; run without --check")
    else:
        OUT_CATALOG.write_text(json.dumps(catalog, indent=2) + "\n")
        OUT_MANIFEST.write_text("\n".join(manifest) + "\n")
        OUT_LEDGER.write_text("\n".join(ledger) + "\n")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    build(parser.parse_args().check)
    print("generate_summoning_corpus: 45 profiles, deterministic policy OK")

if __name__ == "__main__":
    main()
