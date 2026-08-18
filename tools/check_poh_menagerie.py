#!/usr/bin/env python3
"""Static completeness contract for the cache-backed POH Menagerie."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "OSRS-Content" / "osrs239-content"
SCRIPT = CONTENT / "server/scripts/skill_construction/scripts/poh_menagerie.rs2"
VARBIT = CONTENT / "configs/all.varbit"
COMPACK = CONTENT / "configs/all.varbit.compack"


def block(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


def main() -> None:
    source = SCRIPT.read_text()
    unique_map = block(source, "[proc,poh_menagerie_npc]", "[proc,poh_menagerie_aux_npc]")
    indices = [int(value) for value in re.findall(r"case\s+(\d+)\s*:\s*return", unique_map)]
    if indices != list(range(69)):
        raise SystemExit(f"unique pet map is not exactly 0..68: {indices}")

    aux_map = block(source, "[proc,poh_menagerie_aux_npc]", "[proc,poh_menagerie_spawn_coord]")
    auxiliaries = re.findall(r"case\s+([a-zA-Z0-9_]+)\s*:\s*return", aux_map)
    if len(auxiliaries) != 44 or len(set(auxiliaries)) != 44:
        raise SystemExit(f"expected 44 distinct repeatable-pet objects, got {len(auxiliaries)}")

    timers = re.findall(r"^\[ai_timer,(poh_[a-zA-Z0-9_]+)\]$", source, re.MULTILINE)
    pickups = re.findall(r"^\[opnpc3,(poh_[a-zA-Z0-9_]+)\]$", source, re.MULTILINE)
    if len(timers) != 113 or len(set(timers)) != 113:
        raise SystemExit(f"expected 113 distinct roaming NPC timers, got {len(timers)}")
    if pickups != timers:
        raise SystemExit("roaming NPC timer and Pick-up binding sets/order differ")

    required = (
        "[if_button1,poh_menagerie:list]",
        "[if_button1,poh_menagerie:roaming]",
        "[oploc1,poh_menagerie_petlist_1]",
        "runclientscript*(647)",
        "inv_size(poh_menagerie_pets)",
        "[proc,poh_menagerie_has_feeder]",
        "[oplocu,poh_menagerie_combatring_mat]",
    )
    missing = [token for token in required if token not in source]
    if missing:
        raise SystemExit(f"missing Menagerie contracts: {missing}")

    varbits = VARBIT.read_text()
    compack = COMPACK.read_text()
    aliases = (
        "poh_menagerie_wardens_stored",
        "poh_menagerie_cow_stored",
        "poh_menagerie_maggot_stored",
    )
    for config_id, name in enumerate(aliases, start=20411):
        if f"[{name}]" not in varbits or f"{config_id}={name}" not in compack:
            raise SystemExit(f"missing safe packed-var alias: {name}")

    print("poh menagerie contract: 69 unique pets, 44 auxiliary objects, "
          "113 roaming/Pick-up NPC projections")


if __name__ == "__main__":
    main()
