#!/usr/bin/env python3
"""Prove every enabled Summoning special retains a local source class."""

from __future__ import annotations

import sys
from pathlib import Path

from summoning_special_registry import IMPLEMENTED


REPO = Path(__file__).resolve().parents[1]
FAMILIARS = REPO.parent / "2009scape/Server/src/main/content/global/skill/summoning/familiar"

# Shared classes deliberately own several scroll rows in the source tree. This
# is an evidence map, not a behavior table: a handler still needs its own
# source-specific assertions in test_summoning_specials.py.
SOURCE_TYPES = {
    "SpiritWolfNPC.java": (1,),
    "DreadfowlNPC.java": (2,),
    "SpiritTerrorbirdNPC.java": (3,),
    "SpiritSpiderNPC.java": (4,),
    "ThornySnailNPC.java": (5,),
    "GraniteCrabNPC.java": (6,),
    "SpiritMosquitoNPC.java": (7,),
    "DesertWyrmNPC.java": (8,),
    "SpiritKalphiteNPC.java": (12,),
    "AlbinoRatNPC.java": (11,),
    "CompostMoundNPC.java": (13,),
    "VampireBatNPC.java": (15,),
    "BeaverNPC.java": (17,),
    "VoidFamiliarNPC.java": (18, 19, 20, 21),
    "BullAntNPC.java": (22,),
    "MacawNPC.java": (23,),
    "EvilTurnipNPC.java": (24,),
    "CockatriceFamiliarNPC.java": (25, 26, 27, 28, 29, 30, 31),
    "PyreLordNPC.java": (32,),
    "MagpieNPC.java": (33,),
    "BloatedLeechNPC.java": (34,),
    "AbyssalParasiteNPC.java": (35,),
    "SpiritJellyNPC.java": (36,),
    "IbisNPC.java": (37,),
    "SpiritKyattNPC.java": (38,),
    "SpiritLarupiaNPC.java": (39,),
    "SmokeDevilNPC.java": (42,),
    "AbyssalLurkerNPC.java": (43,),
    "SpiritCobraNPC.java": (44,),
    "BarkerToadNPC.java": (46,),
    "WarTortoiseNPC.java": (47,),
    "BunyipNPC.java": (48,),
    "FruitBatNPC.java": (49,),
    "ArcticBearNPC.java": (51,),
    "ObsidianGolemNPC.java": (53,),
    "GraniteLobsterNPC.java": (54,),
    "ElementalTitanNPC.java": (59, 60, 61),
    "HydraNPC.java": (62,),
    "MinotaurFamiliarNPC.java": (66, 67, 68, 69, 70, 71),
    "UnicornStallionNPC.java": (72,),
    "GeyserTitanNPC.java": (73,),
    "WolpertingerNPC.java": (74,),
    "AbyssalTitanNPC.kt": (75,),
    "IronTitanNPC.java": (76,),
    "PackYakNPC.java": (77,),
    "SteelTitanNPC.java": (78,),
}


def main() -> int:
    referenced = {kind for kinds in SOURCE_TYPES.values() for kind in kinds}
    errors: list[str] = []
    if referenced != IMPLEMENTED:
        errors.append(
            "enabled-type source map drift: "
            f"missing={sorted(IMPLEMENTED - referenced)}, extra={sorted(referenced - IMPLEMENTED)}"
        )
    for source in SOURCE_TYPES:
        if not (FAMILIARS / source).is_file():
            errors.append(f"missing cited familiar source class: {source}")
    if errors:
        print("test_summoning_source_provenance: error: " + "; ".join(errors), file=sys.stderr)
        return 1
    print(f"test_summoning_source_provenance: {len(IMPLEMENTED)} enabled rows cite local familiar classes, 0 errors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
