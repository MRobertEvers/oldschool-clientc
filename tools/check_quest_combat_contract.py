#!/usr/bin/env python3
"""Structural regression checks for quest encounters that implementation has begun."""

from __future__ import annotations

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "OSRS-Content/osrs239-content/server/scripts"
MANIFEST = ROOT / "docs/bosses/quest_combat_manifest.json"
DELRITH = CONTENT / "quests/quest_demon/scripts/delrith.rs2"
DELRITH_NPC = CONTENT / "quests/quest_demon/configs/quest_demon.npc"
DELRITH_VARP = CONTENT / "quests/quest_demon/configs/quest_demon.varp"
ARIS = CONTENT / "areas/varrock/scripts/aris.rs2"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def require_text(text: str, needles: tuple[str, ...], scope: str) -> None:
    for needle in needles:
        require(needle in text, f"{scope}: missing {needle!r}")


def check_manifest() -> None:
    data = json.loads(MANIFEST.read_text())
    rows = data["encounters"]
    require(len(rows) == 145, "manifest: expected 145 quest/miniquest units")
    matches = [row for row in rows if row["id"] == "quest-demon-slayer"]
    require(len(matches) == 1, "manifest: expected exactly one Demon Slayer row")
    row = matches[0]
    require(row["implementation_status"] == "implementation-in-progress", "Demon Slayer: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(row[key]), f"Demon Slayer: empty evidence field {key}")


def check_delrith() -> None:
    script = DELRITH.read_text()
    require_text(
        script,
        (
            "[zone,0_50_52_24_32]",
            "[zone,0_50_52_24_40]",
            "[proc,demon_slayer_init_incantation]",
            "[proc,demon_slayer_incantation_word](int $word)(string)",
            "[proc,demon_slayer_incantation_text]()(string)",
            "[proc,demon_slayer_choose_incantation_word](string $position)(int)",
            "add(random(5), 1)",
            "def_int $word_5 = ~demon_slayer_choose_incantation_word(\"fifth\");",
            "$word_5 = %demon_incantation_5",
            "[opnpc2,delrith]",
            "[apnpc2,delrith]",
            "[ai_queue2,delrith]",
            "$damage = calc($damage * 2);",
            "[ai_queue3,delrith]",
            "npc_setowner;",
            "%demonstart = ^demon_silverlight",
            "inv_total(worn, silverlight) > 0",
            "npc_changetype(delrith_weakened, 500);",
            "[opnpc1,delrith_weakened]",
            "queue(demon_slayer_start_incantation, 0, npc_uid);",
            "[queue,demon_slayer_start_incantation](npc_uid $delrith)",
            "npc_finduid($delrith)",
            "npc_statheal(hitpoints, 0, 100);",
            "npc_del;",
            "queue(demon_slayer_complete, 1, 0);",
        ),
        "Delrith",
    )
    require("npc_find(coord, delrith_weakened" not in script, "Delrith: ambiguous radius lookup restored")
    require("~p_choice4(" not in script, "Delrith: fixed whole-chant menu restored")
    require("Zaree" not in script, "Delrith: non-incantation word restored")

    npc = DELRITH_NPC.read_text()
    require_text(
        npc,
        ("[delrith]", "hitpoints=7", "param=attackrate,6", "param=damagetype,2", "[delrith_weakened]"),
        "Delrith NPC config",
    )
    require(npc.count("param=death_drop,null") == 2, "Delrith NPC config: both forms must have null drops")

    varp = DELRITH_VARP.read_text()
    require_text(
        varp,
        (
            "[demon_delrith_engaged]",
            "scope=temp",
            "[demon_incantation_1]",
            "[demon_incantation_2]",
            "[demon_incantation_3]",
            "[demon_incantation_4]",
            "[demon_incantation_5]",
        ),
        "Delrith engagement/incantation state",
    )

    aris = ARIS.read_text()
    require_text(
        aris,
        (
            "[label,demon_slayer_aris_quest_start]",
            "~demon_slayer_init_incantation;",
            "[label,demon_slayer_aris_incantation]",
            "~demon_slayer_incantation_text",
            "~chatnpc_anim(^chat_happy, $incantation);",
        ),
        "Aris incantation reminder",
    )
    require(
        "Carlem... Aber... Camerinthum... Purchai... Gabindo" not in aris,
        "Aris: fixed incantation reminder restored",
    )


def main() -> int:
    try:
        check_manifest()
        check_delrith()
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"quest combat contract: {error}", file=sys.stderr)
        return 1
    print("quest combat contract: 145-unit ledger and Delrith proving slice (ok)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
