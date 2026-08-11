#!/usr/bin/env python3
"""Regression gate for the first transaction-safe Summoning specials."""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
SERVER = REPO / (
    "OSRS-Content/osrs239-content/server/scripts/ported_scape2009_summoning/"
    "scripts/summoning_spirit_wolf.rs2"
)
MOCK_HOST = REPO / "src/net/mock/mock230_scripts.c"
MOCK_WORLD = REPO / "src/net/mock/mock230_world.c"
MOCK_HEADER = REPO / "src/net/mock/mock230.h"
INTERFACE = REPO / (
    "OSRS-Content/osrs239-content/ported/scape2009_summoning/"
    "interfaces/summoning_familiar.if"
)


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    try:
        text = SERVER.read_text(encoding="utf-8")
        mock_host = MOCK_HOST.read_text(encoding="utf-8")
        mock_world = MOCK_WORLD.read_text(encoding="utf-8")
        mock_header = MOCK_HEADER.read_text(encoding="utf-8")
        interface = INTERFACE.read_text(encoding="utf-8")
        execute = text[
            text.index("[proc,summoning_familiar_special_execute]") : text.index("[proc,summoning_familiar_special_xp]")
        ]
        xp = text[
            text.index("[proc,summoning_familiar_special_xp]") : text.index("[proc,summoning_familiar_special_target_kinds]")
        ]
        handler = text[text.index("[if_button1,summoning_familiar:special]") : text.index("[opnpct,summoning_familiar:special_overlay]")]
        commit = text[text.index("[proc,summoning_familiar_special_commit]") : text.index("[proc,summoning_familiar_special_target_execute]")]
        validate = text[text.index("[proc,summoning_familiar_special_validate]") : text.index("[proc,summoning_familiar_special_commit]")]

        expected_xp = {
            3: 8, 6: 2, 11: 23,
            18: 7, 19: 7, 20: 7, 21: 7,
            22: 6, 43: 19, 47: 7, 53: 73,
            59: 79, 60: 79, 61: 79, 74: 45,
        }
        execute_types = {
            int(value)
            for condition in re.findall(r"if \(([^)]*)\) \{", execute)
            for value in re.findall(r"\$type = (\d+)", condition)
        }
        xp_rows = {
            int(kind): int(value)
            for kind, value in re.findall(r"if \(\$type = (\d+)\) return\((\d+)\);", xp)
        }
        expect(execute_types == set(expected_xp), "implemented special handlers drifted from their XP rows")
        expect(xp_rows == expected_xp, "special XP is not the configured tenths-of-XP value")
        expect("if (~summoning_familiar_special_execute(%summoning_familiar_type) = false) return;" in handler,
               "special resources can be committed before its operation accepts")
        expect("~summoning_familiar_special_commit(%summoning_familiar_type);" in handler,
               "the immediate path does not use the common special commit")
        expect(commit.index("inv_del(inv, $scroll, 1);") < commit.index("stat_advance(summoning,"),
               "Summoning XP is not part of the successful resource commit")
        expect("~summoning_familiar_special_validate($type)" in text and
               "~summoning_familiar_special_commit($type);" in text,
               "the targeted path is not validate/execute/revalidate/commit")
        expect("stat_add(defence, 4, 0);" in execute, "Stony Shell's Defence boost is missing")
        expect("healenergy(calc(divide(stat_base(agility), 2) * 100));" in execute,
               "Tireless Run's Agility-scaled energy restoration is missing")
        expect("if (runenergy >= 100)" in execute and
               "You already have full run energy." in execute,
               "Unburden does not reject a full run-energy bar")
        expect("if (inv_freespace(inv) < 4)" in execute and
               "inv_add(inv, cheese, 4);" in execute,
               "Cheese Feast does not atomically produce four non-stackable cheeses")
        expect("case SS_OP_RUNENERGY:" in mock_host and
               "SSVM_PushInt(state, player->run_energy / 100);" in mock_host,
               "the script-visible runenergy getter is not backed by the host")
        expect("stat_add(agility, 4, 0);" in execute and "stat_add(thieving, 4, 0);" in execute,
               "Abyssal Stealth's dual boost is missing")
        expect("add(stat_base(defence), 9)" in execute and "add(stat_base(magic), 7)" in execute,
               "capped Testudo or Magic Focus behavior is missing")
        expect("if ($type = 59 | $type = 60 | $type = 61)" in execute,
               "the Titan's Constitution family is not shared")
        expect("add(stat_base(hitpoints), 8)" in execute and
               "You are already at maximum hitpoints!" in execute,
               "Titan's Constitution does not validate its overheal ceiling")
        expect("if ($type = 18 | $type = 19 | $type = 20 | $type = 21)" in execute and
               "p_delay(2);" in execute and "p_telejump(0_41_35_41_34);" in execute,
               "Call to Arms does not retain its shared delayed Pest Control teleport")
        expect("summoning_special_move_call_to_arms_start" in execute and
               "summoning_special_move_call_to_arms_end" in execute and
               "npc_findowned2 = false" in execute,
               "Call to Arms lacks its remapped visuals or delayed familiar revalidation")
        overlay = interface[interface.index("[special_overlay]") : interface.index("[special_overlay_icon]")]
        expect("clickmask=129024" in overlay and "targetverb=Cast" in overlay,
               "the Summoning overlay is not a five-kind target component")
        for trigger in ("opnpct", "opplayert", "opheldt", "opobjt", "oploct"):
            expect(f"[{trigger},summoning_familiar:special_overlay]" in text,
                   f"the Summoning overlay does not route {trigger}")
        expect("^if_event_target_all" in text,
               "the server does not arm the target component's target mask")
        expect("PKTOUT_NAME_OPPLAYERT, handle_opplayert_packet" in mock_world and
               "handle_opplayert(" in mock_world,
               "decoded OPPLAYERT packets are still dropped by the world router")
        expect("uint16_t generation;" in mock_header and "target_generation" in mock_header,
               "NPC/interactions do not carry generation identity")
        expect("srv->npcs[slot].generation != generation" in mock_host,
               "npc_finduid accepts a reused NPC slot")
        expect("case SS_OP_NPC_FINDCOMBAT:" in mock_host,
               "content cannot resolve the owner's current combat target")
        expect("case SS_OP_NPC_FINDOWNED2:" in mock_host and
               "SSVM_ENT_NPC, SSVM_SECONDARY, npc" in mock_host and
               "npc_findowned2 = false" in validate,
               "target validation cannot retain a primary target while resolving the familiar")
    except (AssertionError, OSError, ValueError) as exc:
        print(f"test_summoning_specials: error: {exc}", file=sys.stderr)
        return 1

    print("test_summoning_specials: target surface, generation handles, transaction and 15 source-backed rows, 0 errors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
