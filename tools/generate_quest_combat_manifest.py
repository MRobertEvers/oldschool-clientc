#!/usr/bin/env python3
"""Generate and validate the pinned quest-combat implementation ledger.

The human-readable catalogue lives in docs/bosses/quest_bosses.md.  This tool
turns its two authoritative inventory tables into a deterministic JSON file
that build tooling can consume without contacting the Wiki.  The roster count
and digest are pinned deliberately: changing, removing, or renaming a unit is
an explicit source-audit event, not a silent regeneration.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLAN = ROOT / "docs/bosses/quest_bosses.md"
OUT = ROOT / "docs/bosses/quest_combat_manifest.json"

CATALOGUE_URL = (
    "https://oldschool.runescape.wiki/w/Quests/Requirements_by_quest"
    "?oldid=15281241"
)
CATALOGUE_REVISION = 15281241
EXPECTED_QUESTS = 133
EXPECTED_MINIQUESTS = 12
# sha256 of kind, name, URL, and normalized encounter text for all 145 rows.
# Updating this is part of intentionally accepting a newly audited Wiki roster.
EXPECTED_ROSTER_SHA256 = "b35f9e543e54632bb4394e06971aaaa38e02a839cb6688506ae75c70fdd23883"

POST_REVISION_239 = {
    "Death on the Isle",
    "The Blood Moon Rises",
    "The Final Dawn",
    "Prying Times",
    "Troubled Tortugans",
    "The Red Reef",
    "Shadows of Custodia",
    "Scrambled!",
    "Learning the Ropes",
    "The Ides of Milk",
    "Fallen From Grace",
}

AUDITED_OVERRIDES: dict[str, dict[str, object]] = {
    "Demon Slayer": {
        "source_audits": [
            {
                "url": "https://oldschool.runescape.wiki/w/Demon_Slayer?oldid=15291214",
                "revision": 15291214,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Delrith?oldid=15216579",
                "revision": 15216579,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Demon_Slayer/Quick_guide?oldid=15109448",
                "revision": 15109448,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Transcript:Demon_Slayer?oldid=15263169",
                "revision": 15263169,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Silverlight?oldid=15286297",
                "revision": 15286297,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Dark_wizard?oldid=15289844",
                "revision": 15289844,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Stone_table_(Delrith)?oldid=15201851",
                "revision": 15201851,
                "retrieved": "2026-08-17",
            },
        ],
        "npc_gamevals": [
            "delrith",
            "delrith_weakened",
            "qip_ds_dark_wizard_denath",
            "qip_ds_young_dark_wizard1",
            "qip_ds_young_dark_wizard2",
            "qip_ds_young_dark_wizard3",
            "qip_ds_young_dark_wizard4",
        ],
        "item_gamevals": ["silverlight"],
        "loc_gamevals": ["qip_ds_stone_table"],
        "trigger_handlers": [
            "zone:0_50_52_24_32",
            "zone:0_50_52_24_40",
            "opnpc2:delrith",
            "apnpc2:delrith",
            "ai_queue2:delrith",
            "ai_queue3:delrith",
            "opnpc1:delrith_weakened",
        ],
        "loot_contract": "No ordinary or bones drop; successful banishment grants quest completion only.",
        "test_ids": [
            "quest-combat-contract:delrith",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The summoning ritual is narrated; full NPC/camera/audio choreography is pending.",
            "A real-client concurrent-player, death, and relog smoke is still pending.",
        ],
    },
    "Witch's House": {
        "source_audits": [
            {
                "url": "https://oldschool.runescape.wiki/w/Witch%27s_House?oldid=15168391",
                "revision": 15168391,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Witch%27s_House/Quick_guide?oldid=15291737",
                "revision": 15291737,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Witch%27s_experiment?oldid=15206938",
                "revision": 15206938,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Transcript:Witch%27s_House?oldid=15263233",
                "revision": 15263233,
                "retrieved": "2026-08-17",
            },
        ],
        "npc_gamevals": [
            "nora_t_hagg",
            "shapeshifterglob",
            "shapeshifterspider",
            "shapeshifterbear",
            "shapeshifterwolf",
            "witchrat",
        ],
        "item_gamevals": [
            "witches_doorkey",
            "witches_shedkey",
            "magnet",
            "cheese",
            "leather_gloves",
            "ball",
        ],
        "loc_gamevals": [
            "witchpot",
            "witchhousedoor",
            "magnetcbshut",
            "magnetcbopen",
            "witchmousehole",
            "witchbackdoor",
            "witchsheddoor",
            "witchfountain",
        ],
        "trigger_handlers": [
            "oplocu:witchsheddoor",
            "opobj3:ball",
            "ai_queue3:shapeshifterglob",
            "ai_queue3:shapeshifterspider",
            "ai_queue3:shapeshifterbear",
            "ai_queue3:shapeshifterwolf",
            "ai_timer:nora_t_hagg",
        ],
        "loot_contract": "All four forms have explicit null death drops; the ball is a gated post-fight ground objective, not combat loot.",
        "test_ids": ["quest-combat-contract:witches-experiment"],
        "known_gaps": [
            "The shed's live 'someone is inside' admission refusal is not implemented.",
            "The shed-specific Dwarf multicannon restriction awaits the shared cannon placement gate.",
            "A real-client concurrent-player, flee, death, and relog smoke is still pending.",
        ],
    },
}

ROW = re.compile(
    r"^\| \[(?P<name>[^]]+)]\((?P<url>https://oldschool\.runescape\.wiki/w/[^)]+)\)"
    r" \| (?P<encounters>.+) \|$"
)


def normalize_markdown(value: str) -> str:
    return re.sub(r"\s+", " ", value.strip())


def slug(value: str) -> str:
    result = re.sub(r"[^a-z0-9]+", "-", value.casefold()).strip("-")
    if not result:
        raise ValueError(f"cannot create id for {value!r}")
    return result


def inventory() -> list[dict[str, object]]:
    section: str | None = None
    rows: list[dict[str, object]] = []
    for line_number, raw in enumerate(PLAN.read_text().splitlines(), 1):
        line = raw.strip()
        if line == "## 2. Combat-bearing quest inventory":
            section = "quest"
            continue
        if line == "### Miniquests with combat encounters":
            section = "miniquest"
            continue
        if line.startswith("## 3."):
            section = None
        if section is None:
            continue
        match = ROW.fullmatch(line)
        if not match:
            continue
        name = match.group("name")
        encounter = normalize_markdown(match.group("encounters"))
        blocked = name in POST_REVISION_239
        status = "blocked-cache-version" if blocked else "audit-pending"
        if name in AUDITED_OVERRIDES:
            status = "implementation-in-progress"
        row: dict[str, object] = {
            "id": f"{section}-{slug(name)}",
            "kind": section,
            "name": name,
            "wiki_url": match.group("url"),
            "encounter_summary": encounter,
            "plan_line": line_number,
            "cache_scope": "post-revision-239" if blocked else "osrs239",
            "implementation_status": status,
            "source_audits": [],
            "npc_gamevals": [],
            "item_gamevals": [],
            "loc_gamevals": [],
            "trigger_handlers": [],
            "loot_contract": "",
            "test_ids": [],
            "known_gaps": [],
        }
        row.update(AUDITED_OVERRIDES.get(name, {}))
        rows.append(row)
    return rows


def roster_digest(rows: list[dict[str, object]]) -> str:
    fields = [
        "\t".join(
            str(row[key])
            for key in ("kind", "name", "wiki_url", "encounter_summary")
        )
        for row in rows
    ]
    return hashlib.sha256("\n".join(fields).encode()).hexdigest()


def validate(rows: list[dict[str, object]]) -> str:
    counts = {
        kind: sum(row["kind"] == kind for row in rows)
        for kind in ("quest", "miniquest")
    }
    expected = {"quest": EXPECTED_QUESTS, "miniquest": EXPECTED_MINIQUESTS}
    if counts != expected:
        raise ValueError(f"inventory count changed: expected {expected}, found {counts}")

    ids = [str(row["id"]) for row in rows]
    names = [(str(row["kind"]), str(row["name"])) for row in rows]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate manifest id")
    if len(names) != len(set(names)):
        raise ValueError("duplicate inventory row")

    statuses = {
        "audit-pending",
        "implementation-in-progress",
        "verified-modern",
        "blocked-cache-version",
    }
    required_for_verified = (
        "source_audits",
        "npc_gamevals",
        "trigger_handlers",
        "loot_contract",
        "test_ids",
    )
    for row in rows:
        if row["implementation_status"] not in statuses:
            raise ValueError(f"{row['id']}: invalid implementation status")
        if row["implementation_status"] == "blocked-cache-version":
            if row["name"] not in POST_REVISION_239:
                raise ValueError(f"{row['id']}: unexpected cache-version blocker")
        if row["implementation_status"] == "verified-modern":
            missing = [key for key in required_for_verified if not row[key]]
            if missing:
                raise ValueError(f"{row['id']}: verified row missing {missing}")

    digest = roster_digest(rows)
    if EXPECTED_ROSTER_SHA256 != "TO_BE_PINNED" and digest != EXPECTED_ROSTER_SHA256:
        raise ValueError(
            "quest-combat roster changed; audit the pinned Wiki source and update "
            f"EXPECTED_ROSTER_SHA256 intentionally (found {digest})"
        )
    return digest


def payload(rows: list[dict[str, object]], digest: str) -> dict[str, object]:
    return {
        "schema_version": 1,
        "catalogue": {
            "wiki_url": CATALOGUE_URL,
            "wiki_revision": CATALOGUE_REVISION,
            "roster_sha256": digest,
            "quest_count": EXPECTED_QUESTS,
            "miniquest_count": EXPECTED_MINIQUESTS,
        },
        "status_contract": {
            "audit-pending": "Wiki/cache/runtime audit has not been completed.",
            "implementation-in-progress": "Audited implementation is being built but is not accepted.",
            "verified-modern": "All required evidence fields are populated and build/tests pass.",
            "blocked-cache-version": "Required content is newer than the accepted revision-239 cache.",
        },
        "encounters": rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail unless the checked-in manifest exactly matches the plan",
    )
    parser.add_argument(
        "--print-digest",
        action="store_true",
        help="print the normalized roster digest",
    )
    args = parser.parse_args()

    try:
        rows = inventory()
        digest = validate(rows)
    except (OSError, ValueError) as error:
        print(f"quest combat manifest: {error}", file=sys.stderr)
        return 1

    if args.print_digest:
        print(digest)

    rendered = json.dumps(payload(rows, digest), indent=2, ensure_ascii=False) + "\n"
    if args.check:
        if not OUT.exists():
            print(f"quest combat manifest: missing {OUT.relative_to(ROOT)}", file=sys.stderr)
            return 1
        if OUT.read_text() != rendered:
            print(
                "quest combat manifest: generated output is stale; run "
                "python3 tools/generate_quest_combat_manifest.py",
                file=sys.stderr,
            )
            return 1
        print(
            f"quest combat manifest: {len(rows)} inventory units, digest {digest[:12]} (ok)"
        )
        return 0

    OUT.write_text(rendered)
    print(f"wrote {OUT.relative_to(ROOT)} ({len(rows)} inventory units)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
