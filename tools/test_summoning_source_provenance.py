#!/usr/bin/env python3
"""Prove every enabled Summoning special retains a local source class."""

from __future__ import annotations

import sys
from pathlib import Path

from summoning_special_registry import IMPLEMENTED, RECONSTRUCTED_TYPES, SOURCE_GAPS


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
    "SpiritScorpionNPC.java": (9,),
    "SpiritTzKihNPC.java": (10,),
    "SpiritKalphiteNPC.java": (12,),
    "GiantChinchompaNPC.java": (14,),
    "AlbinoRatNPC.java": (11,),
    "CompostMoundNPC.java": (13,),
    "VampireBatNPC.java": (15,),
    "HoneyBadgerNPC.java": (16,),
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
    "SpiritGraahkNPC.kt": (40,),
    "KaramthulhuOverlordNPC.java": (41,),
    "SmokeDevilNPC.java": (42,),
    "TalonBeastNPC.java": (57,),
    "SpiritDagannothNPC.java": (63,),
    "StrangerPlantNPC.java": (45,),
    "AbyssalLurkerNPC.java": (43,),
    "SpiritCobraNPC.java": (44,),
    "BarkerToadNPC.java": (46,),
    "WarTortoiseNPC.java": (47,),
    "BunyipNPC.java": (48,),
    "FruitBatNPC.java": (49,),
    "RavenousLocustNPC.java": (50,),
    "ArcticBearNPC.java": (51,),
    "ObsidianGolemNPC.java": (53,),
    "GraniteLobsterNPC.java": (54,),
    "PrayingMantisNPC.java": (55,),
    "ForgeRegentNPC.java": (56,),
    "GiantEntNPC.java": (58,),
    "ElementalTitanNPC.java": (59, 60, 61),
    "HydraNPC.java": (62,),
    "LavaTitanNPC.java": (64,),
    "SwampTitanNPC.java": (65,),
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
    # Phoenix (52) is the single deliberate exception: 2009scape has a pouch
    # and commented scroll row but no Phoenix Familiar class. Its period Wiki,
    # patch-note and release-report evidence is fixture-owned instead.
    expected_local = IMPLEMENTED - {52}
    if referenced != expected_local:
        errors.append(
            "enabled-type source map drift: "
            f"missing={sorted(expected_local - referenced)}, extra={sorted(referenced - expected_local)}"
        )
    if SOURCE_GAPS:
        errors.append("a completed reconstruction remains in SOURCE_GAPS")
    if RECONSTRUCTED_TYPES != {16, 50, 52, 56, 58, 64, 65}:
        errors.append("the audited reconstruction set drifted")
    for source in SOURCE_TYPES:
        if not (FAMILIARS / source).is_file():
            errors.append(f"missing cited familiar source class: {source}")
    if errors:
        print("test_summoning_source_provenance: error: " + "; ".join(errors), file=sys.stderr)
        return 1
    print(
        "test_summoning_source_provenance: "
        f"{len(expected_local)} enabled rows retain local source classes; "
        "Phoenix is fixture-owned; 7 audited reconstructions, 0 errors"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
