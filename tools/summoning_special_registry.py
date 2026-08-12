#!/usr/bin/env python3
"""Authoritative, generated data for the 78 Summoning special moves.

The server uses numeric familiar types, while the source roster is keyed by
pouch IDs and shared scrolls.  Keeping those tables independently hand-edited
is how a valid-looking special can charge the wrong amount or award the wrong
XP.  This module is deliberately the single translation point used by the
special regression gate and future generated ServerScript tables.
"""

from __future__ import annotations

import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path

from summoning_script_sources import definition, script_dir


REPO = Path(__file__).resolve().parents[1]
POUCHES = REPO / "docs/summoning_port/pouches_530.json"
SCROLL_SOURCE = REPO.parent / (
    "2009scape/Server/src/main/content/global/skill/summoning/SummoningScroll.java"
)
SCROLL_MANIFEST = REPO / "docs/summoning_port/scroll_assets_530.ini"
SERVER_SCRIPTS = script_dir()

SOURCE_GAPS = frozenset()
RECONSTRUCTED_TYPES = frozenset((16, 50, 52, 56, 58, 64, 65))
IMPLEMENTED = frozenset(range(1, 79))
# Bunyip's Fish enum includes four non-cookable entries, then dereferences a
# null CookableItems row; the live handler corrects that known source defect.
SOURCE_DEFECTS = frozenset((45, 48))
# The final seven rows use the explicit, dated reconstruction contracts in the
# fixture. They are enabled only because each inference is now named and
# regression-locked rather than hidden behind an apparently authoritative
# number.
SOURCE_INCOMPLETE = frozenset()
SOURCE_GAP_FIXTURE = "docs/summoning_port/SPECIAL_SOURCE_GAP_FIXTURES.json"
TARGET_KINDS = {
    1: "npc", 13: "scenery", 16: "familiar_self", 17: "scenery",
    32: "inventory_item", 40: "npc", 44: "inventory_item",
    48: "inventory_item", 50: "player", 52: "ground_ashes",
    56: "player", 58: "combat_area", 62: "scenery",
    64: "combat_target", 65: "combat_area", 72: "player",
    77: "inventory_item",
}
CALL_TO_ARMS = frozenset((18, 19, 20, 21))


@dataclass(frozen=True)
class SpecialRecord:
    type: int
    pouch: int
    familiar: str
    scroll_source: int
    scroll_runtime: str
    cost: int
    xp_tenths: int
    target_kind: str
    handler: str
    state: str
    provenance: str
    asset_bundle: str


def _proc_body(name: str) -> str:
    return definition(SERVER_SCRIPTS, f"proc,{name}")


def _scroll_source() -> dict[int, tuple[int, int]]:
    # pouch -> (scroll source obj, XP in tenths).  Three historical source
    # defects are corrected in the existing scroll import gate as well.
    pattern = re.compile(
        r"^\s*(?://\s*)?[A-Z][A-Z0-9_]+_SCROLL\d*"
        r"\(-?\d+,\s*(\d+),\s*([0-9.]+),\s*\d+,\s*(-?\d+)\)", re.MULTILINE
    )
    corrections = {12023: 12023, -1: 12023, 12794: 12794, 14623: 14623}
    result: dict[int, tuple[int, int]] = {}
    for scroll, xp, pouch in pattern.findall(SCROLL_SOURCE.read_text(encoding="utf-8")):
        pouch_id = corrections.get(int(pouch), int(pouch))
        # The three duplicate/incorrect source entries are resolved by source
        # scroll identity below; a pouch may legitimately be encountered twice.
        result.setdefault(pouch_id, (int(scroll), round(float(xp) * 10)))
    return result


def _scroll_xp() -> dict[int, int]:
    result = {
        int(scroll): round(float(xp) * 10)
        for scroll, xp in re.findall(
            r"^\s*(?://\s*)?[A-Z][A-Z0-9_]+_SCROLL\d*"
            r"\(-?\d+,\s*(\d+),\s*([0-9.]+),", 
            SCROLL_SOURCE.read_text(encoding="utf-8"), re.MULTILINE
        )
    }
    # The archived 21 August 2009 Jagex scroll table is authoritative over two
    # defects in the local source enum: Phoenix Rise is 5.0 XP (not 8.0), and
    # Swamp Plague is 4.3 XP (not 4.1).
    result.update({14622: 50, 12832: 43})
    return result


def _manifest_exports() -> dict[int, str]:
    exports: dict[int, str] = {}
    section = ""
    for raw in SCROLL_MANIFEST.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
        elif section == "export:obj" and line and not line.startswith("#"):
            source, name = line.split("=", 1)
            exports[int(source)] = "summoning_scroll_" + name
    return exports


def _runtime_scrolls_and_costs() -> tuple[dict[int, str], dict[int, int]]:
    scrolls = {
        int(kind): name
        for kind, name in re.findall(
            r"if \(\$type = (\d+)\) return\((summoning_scroll_[a-z0-9_]+)\);",
            _proc_body("summoning_familiar_scroll"),
        )
    }
    costs = {
        int(kind): int(cost)
        for kind, cost in re.findall(
            r"if \(\$type = (\d+)\) return\((\d+)\);",
            _proc_body("summoning_familiar_special_cost"),
        )
    }
    return scrolls, costs


def _runtime_names() -> dict[int, str]:
    """Read the persisted type-to-name roster rather than guessing its order."""
    return {
        int(kind): name.upper().replace(" ", "_")
        for kind, name in re.findall(
            r'if \(\$type = (\d+)\) return\("([^"]+)"\);',
            _proc_body("summoning_familiar_name"),
        )
    }


def _familiar_key(name: str) -> str:
    """Normalize display names to the port roster's intentionally terse IDs."""
    key = name.upper().replace(" ", "_").replace(".", "")
    if key.startswith("SP_"):
        key = "SPIRIT_" + key[3:]
    return {
        "KARAMTHULHU_OVERLORD": "KARAMTHULHU",
        "ABYSSAL_LURKER": "ABYSSAL_LUKRER",  # retained source roster typo
        "FORGE_REGENT": "FORGE_REGENT_BEAST",
    }.get(key, key)


def records() -> list[SpecialRecord]:
    """Return exactly one generated record for each live familiar type."""
    scroll_sources = _scroll_source()
    scroll_xp = _scroll_xp()
    exports = _manifest_exports()
    runtime_scrolls, costs = _runtime_scrolls_and_costs()
    runtime_names = _runtime_names()
    pouches = {int(row["pouch"]): row for row in json.loads(POUCHES.read_text(encoding="utf-8"))}
    by_runtime = {runtime: source for source, runtime in exports.items()}
    result: list[SpecialRecord] = []
    for kind in range(1, 79):
        runtime = runtime_scrolls[kind]
        source_scroll = by_runtime[runtime]
        candidates = [
            (pouch, row) for pouch, row in pouches.items()
            if scroll_sources.get(pouch, (None, None))[0] == source_scroll
        ]
        # Phoenix and Talon beast have explicitly documented defects in the
        # local source's pouch column; its source scroll identity is valid.
        if not candidates:
            corrected = {14622: 14623, 12831: 12794}.get(source_scroll)
            if corrected is not None:
                candidates = [(corrected, pouches[corrected])]
        # Shared scrolls must preserve type ordering, but each type still has
        # a distinct pouch/familiar record.  Infer it from the source row only
        # when unique; the known shared families are disambiguated by order.
        if len(candidates) == 1:
            pouch, row = candidates[0]
        else:
            expected_name = _familiar_key(runtime_names[kind])
            matching = [
                candidate for candidate in candidates
                if candidate[1]["name"].removesuffix("_POUCH") == expected_name
            ]
            if len(matching) != 1:
                raise ValueError(f"cannot resolve shared scroll type {kind}: {runtime}")
            pouch, row = matching[0]
        xp = scroll_xp[source_scroll]
        state = "implemented_reconstructed" if kind in RECONSTRUCTED_TYPES else (
            "implemented" if kind in IMPLEMENTED else
            "source_gap" if kind in SOURCE_GAPS else
            "source_incomplete" if kind in SOURCE_INCOMPLETE else
            "source_defect" if kind in SOURCE_DEFECTS else "specified"
        )
        result.append(SpecialRecord(
            type=kind, pouch=pouch, familiar=row["name"], scroll_source=source_scroll,
            scroll_runtime=runtime, cost=costs[kind], xp_tenths=xp,
            target_kind=TARGET_KINDS.get(kind, "self_or_combat"),
            handler=(
                "summoning_familiar_special_target_execute"
                if kind in RECONSTRUCTED_TYPES - {16}
                else "summoning_familiar_special_execute"
                if kind in IMPLEMENTED else "unavailable"
            ),
            state=state,
            provenance=(
                SOURCE_GAP_FIXTURE if state in {"source_gap", "implemented_reconstructed"} else
                "2009scape" if state != "source_incomplete" else "needs_citation"
            ),
            asset_bundle=(
                "reconstructed_special_assets" if kind in RECONSTRUCTED_TYPES else
                "unavailable" if kind in SOURCE_GAPS else
                "call_to_arms" if kind in CALL_TO_ARMS else "shared_cast"
            ),
        ))
    return result


def main() -> int:
    print(json.dumps([asdict(record) for record in records()], indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
