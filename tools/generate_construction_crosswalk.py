#!/usr/bin/env python3
"""Generate the versioned Construction cache/Wiki implementation crosswalk.

The cache supplies menu objects, materials, requirements, room templates and
unbuilt hotspot placements. A separately refreshed, checked-in OSRS Wiki
snapshot supplies XP and built object IDs. The cache menu-object ID is the join
key, so unresolved runtime facts remain explicit rather than name-guessed.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path
from urllib.parse import quote

from check_construction_catalog import (
    CONSTRUCTION_STAT_ID,
    CatalogError,
    read_blocks,
    read_compack,
    require_ints,
    table_rows,
    validate,
)

WIKI_SEARCH = "https://oldschool.runescape.wiki/w/Special:Search?search="
STYLE_ORIGIN_X = 29 * 64
STYLE_ORIGIN_Z = 110 * 64
TEMPLATE_CATEGORIES = {207, 485}
MAP_LOC_RE = re.compile(
    r"^(?P<level>\d+) (?P<x>\d+) (?P<z>\d+): "
    r"(?P<loc>\d+) (?P<shape>\d+)(?: (?P<angle>\d+))?$"
)


def wiki_search(text: str) -> str:
    return WIKI_SEARCH + quote(text, safe="")


def first_value(block, column: int, default=None):
    values = block.column_values(column)
    return values[0] if values else default


def ints(block, column: int) -> list[int]:
    return require_ints(block, column)


def pairs(values: list[int]) -> list[list[int]]:
    return [values[index : index + 2] for index in range(0, len(values), 2)]


def room_template_placements(content: Path, room, loc_id_to_name, loc_by_name):
    offset = ints(room, 5)
    if len(offset) != 2:
        return []
    source_x = STYLE_ORIGIN_X + offset[0]
    source_z = STYLE_ORIGIN_Z + offset[1]
    map_path = content / "maps" / f"m{source_x // 64}_{source_z // 64}.jl2"
    if not map_path.exists():
        raise CatalogError(f"{room.name}: missing source map {map_path.name}")
    origin_x = source_x % 64
    origin_z = source_z % 64
    placements = []
    for line in map_path.read_text(encoding="utf-8").splitlines():
        match = MAP_LOC_RE.match(line)
        if not match or int(match.group("level")) != 0:
            continue
        x = int(match.group("x"))
        z = int(match.group("z"))
        if not (origin_x <= x < origin_x + 8 and origin_z <= z < origin_z + 8):
            continue
        loc_id = int(match.group("loc"))
        symbol = loc_id_to_name.get(loc_id)
        loc = loc_by_name.get(symbol)
        if loc is None:
            continue
        category = int(loc.first("category") or -1)
        if category not in TEMPLATE_CATEGORIES:
            continue
        placements.append(
            {
                "loc_id": loc_id,
                "loc": symbol,
                "name": loc.first("name") or "",
                "category": category,
                "local_x": x - origin_x,
                "local_z": z - origin_z,
                "shape": int(match.group("shape")),
                "angle": int(match.group("angle") or 0),
            }
        )
    return sorted(
        placements,
        key=lambda item: (item["local_x"], item["local_z"], item["loc_id"], item["angle"]),
    )


def load_wiki_snapshot(path: Path) -> tuple[dict[str, object], dict[int, list[dict[str, object]]]]:
    snapshot = json.loads(path.read_text(encoding="utf-8"))
    entries = snapshot.get("entries")
    if not isinstance(entries, list) or len(entries) < 400:
        raise CatalogError(f"{path}: expected at least 400 Wiki furniture entries")
    by_item_id: dict[int, list[dict[str, object]]] = defaultdict(list)
    for entry in entries:
        for item_id in entry.get("item_ids", []):
            by_item_id[int(item_id)].append(entry)
    return snapshot, by_item_id


def load_reconciliations(path: Path) -> dict[str, dict[str, object]]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    rows = payload.get("rows")
    if not isinstance(rows, dict):
        raise CatalogError(f"{path}: missing rows object")
    return rows


def load_auxiliary_locs(
    path: Path, furniture_symbols: set[str], loc_by_name: dict[str, object]
) -> tuple[dict[str, list[dict[str, str]]], dict[str, object]]:
    """Load reviewed multi-loc scene pairs absent from furniture infoboxes.

    The Wiki's constructed-item row identifies the primary furniture object,
    while a few room templates also replace a separate decorative loc (the
    Workshop stool is the first such case). Keep those pairs symbolic and
    validate both sides against the revision-239 cache.
    """
    payload = json.loads(path.read_text(encoding="utf-8"))
    raw_rows = payload.get("rows")
    if not isinstance(raw_rows, dict):
        raise CatalogError(f"{path}: missing rows object")
    unknown = set(raw_rows) - furniture_symbols
    if unknown:
        raise CatalogError(f"{path}: unknown furniture rows {sorted(unknown)}")
    result: dict[str, list[dict[str, str]]] = {}
    for furniture, pairs_value in raw_rows.items():
        if not isinstance(pairs_value, list) or not pairs_value:
            raise CatalogError(f"{path}: {furniture} needs auxiliary loc pairs")
        pairs_result = []
        for pair in pairs_value:
            if not isinstance(pair, list) or len(pair) != 2:
                raise CatalogError(f"{path}: {furniture} has malformed auxiliary pair")
            empty, built = map(str, pair)
            if empty not in loc_by_name or built not in loc_by_name:
                raise CatalogError(
                    f"{path}: {furniture} references unknown loc pair {empty}, {built}"
                )
            if int(loc_by_name[empty].first("category") or -1) != 207:
                raise CatalogError(f"{path}: {empty} is not a furniture hotspot loc")
            if int(loc_by_name[built].first("category") or -1) != 206:
                raise CatalogError(f"{path}: {built} is not built POH furniture")
            pairs_result.append({"empty_loc": empty, "built_loc": built})
        result[furniture] = pairs_result
    return result, payload.get("wiki_source", {})


def load_flatpacks(
    path: Path, furniture_symbols: set[str], obj_name_to_id: dict[str, int]
) -> tuple[dict[str, str], dict[str, object]]:
    """Load the reviewed furniture-row to revision-239 flatpack-object map."""
    payload = json.loads(path.read_text(encoding="utf-8"))
    raw_rows = payload.get("rows")
    if not isinstance(raw_rows, dict):
        raise CatalogError(f"{path}: missing rows object")
    if len(raw_rows) != 82:
        raise CatalogError(
            f"{path}: expected all 82 obtainable revision-239 flatpacks; "
            "the Wiki's 20 cache-only Study flatpacks must remain excluded"
        )
    unknown_furniture = set(raw_rows) - furniture_symbols
    if unknown_furniture:
        raise CatalogError(f"{path}: unknown furniture rows {sorted(unknown_furniture)}")
    unknown_objects = sorted(set(map(str, raw_rows.values())) - set(obj_name_to_id))
    if unknown_objects:
        raise CatalogError(f"{path}: unknown flatpack objects {unknown_objects}")
    if len(set(raw_rows.values())) != len(raw_rows):
        raise CatalogError(f"{path}: a flatpack object is mapped more than once")
    return {str(key): str(value) for key, value in raw_rows.items()}, payload.get(
        "wiki_source", {}
    )


def wiki_title_index(snapshot: dict[str, object]) -> dict[str, list[dict[str, object]]]:
    result: dict[str, list[dict[str, object]]] = defaultdict(list)
    for entry in snapshot["entries"]:
        for title in {entry.get("source_title"), entry.get("page")} - {None}:
            result[str(title).casefold()].append(entry)
    return result


def unique_wiki_entry(
    index: dict[str, list[dict[str, object]]], title: str
) -> dict[str, object]:
    candidates = index.get(title.casefold(), [])
    by_revision = {
        (entry.get("page"), entry.get("revision_id")): entry for entry in candidates
    }
    if len(by_revision) != 1:
        raise CatalogError(f"Wiki reconciliation page {title!r} resolved {len(by_revision)} ways")
    return next(iter(by_revision.values()))


def wiki_experience(
    entry: dict[str, object], level: int | None, strategy: str | None = None
) -> int | float:
    if entry.get("experience") is not None and strategy is None:
        return entry["experience"]
    recipes = [recipe for recipe in entry.get("recipes", []) if recipe.get("experience") is not None]
    matching = [recipe for recipe in recipes if recipe.get("level") == level]
    if matching:
        recipes = matching
    values = {recipe["experience"] for recipe in recipes}
    if strategy == "upgrade_recipe" and values:
        return min(values)
    if len(values) != 1:
        raise CatalogError(
            f"{entry.get('page')}: cannot select one Construction XP value at level {level}"
        )
    return next(iter(values))


def reconciled_wiki_data(
    row_symbol: str,
    level: int | None,
    reconciliation: dict[str, object],
    index: dict[str, list[dict[str, object]]],
    loc_id_to_name: dict[int, str],
) -> tuple[dict[str, object], list[int], int | float, list[dict[str, object]]]:
    entry = unique_wiki_entry(index, str(reconciliation["page"]))
    loc_name_to_id = {name: loc_id for loc_id, name in loc_id_to_name.items()}
    explicit_locs = reconciliation.get("built_loc_symbols")
    variant = reconciliation.get("variant")
    if explicit_locs is not None:
        missing = [name for name in explicit_locs if name not in loc_name_to_id]
        if missing:
            raise CatalogError(f"{row_symbol}: unknown reconciled built loc symbols {missing}")
        built_ids = [loc_name_to_id[name] for name in explicit_locs]
    elif variant is None:
        built_ids = [int(value) for value in entry.get("object_ids", [])]
    else:
        matching = [
            item
            for item in entry.get("object_variants", [])
            if str(item.get("version", "")).casefold() == str(variant).casefold()
        ]
        if len(matching) != 1:
            raise CatalogError(
                f"{row_symbol}: Wiki variant {variant!r} resolved {len(matching)} ways"
            )
        built_ids = [int(value) for value in matching[0].get("object_ids", [])]
    built_ids = [value for value in built_ids if value in loc_id_to_name]
    if not built_ids:
        raise CatalogError(f"{row_symbol}: reconciled Wiki page has no revision-239 built loc")

    experience_sources = []
    if reconciliation.get("experience_strategy") == "cosmetic_no_xp":
        experience = 0
        experience_sources.append(
            {
                "page": entry.get("page"),
                "revision_id": entry.get("revision_id"),
                "experience": 0,
                "policy": "cosmetic transform consumes an unlock item and grants no build XP",
            }
        )
    else:
        experience_title = str(reconciliation.get("experience_page", reconciliation["page"]))
        experience_entry = unique_wiki_entry(index, experience_title)
        experience = wiki_experience(
            experience_entry, level, reconciliation.get("experience_strategy")
        )
        experience_sources.append(
            {
                "page": experience_entry.get("page"),
                "revision_id": experience_entry.get("revision_id"),
                "experience": experience,
            }
        )
    for title in reconciliation.get("experience_add_pages", []):
        additional = unique_wiki_entry(index, str(title))
        value = wiki_experience(additional, level)
        experience += value
        experience_sources.append(
            {
                "page": additional.get("page"),
                "revision_id": additional.get("revision_id"),
                "experience": value,
            }
        )
    return entry, built_ids, experience, experience_sources


def generate(
    content: Path,
    wiki_snapshot_path: Path,
    reconciliations_path: Path,
    auxiliary_locs_path: Path,
    flatpacks_path: Path,
) -> dict[str, object]:
    summary = validate(content)
    configs = content / "configs"
    rows = read_blocks(configs / "all.dbrow")
    row_id_to_name, row_name_to_id = read_compack(configs / "all.dbrow.compack")
    obj_id_to_name, obj_name_to_id = read_compack(configs / "all.obj.compack")
    loc_id_to_name, _ = read_compack(configs / "all.loc.compack")
    loc_by_name = {block.name: block for block in read_blocks(configs / "all.loc")}
    wiki_snapshot, wiki_by_item_id = load_wiki_snapshot(wiki_snapshot_path)
    reconciliations = load_reconciliations(reconciliations_path)
    wiki_by_title = wiki_title_index(wiki_snapshot)

    furniture = table_rows(rows, "furniture")
    rooms = table_rows(rows, "poh_room")
    hotspots = table_rows(rows, "poh_hotspot")
    furniture_to_hotspots: dict[int, list[str]] = defaultdict(list)
    furniture_symbols = {row.name for row in furniture}
    auxiliary_locs, auxiliary_wiki_source = load_auxiliary_locs(
        auxiliary_locs_path, furniture_symbols, loc_by_name
    )
    flatpacks, flatpack_wiki_source = load_flatpacks(
        flatpacks_path, furniture_symbols, obj_name_to_id
    )
    unknown_reconciliations = set(reconciliations) - furniture_symbols
    if unknown_reconciliations:
        raise CatalogError(
            f"Wiki reconciliations name unknown furniture rows: {sorted(unknown_reconciliations)}"
        )

    hotspot_records = []
    for hotspot in hotspots:
        builddata = ints(hotspot, 0)
        for furniture_id in builddata:
            furniture_to_hotspots[furniture_id].append(hotspot.name)
        hotspot_records.append(
            {
                "id": row_name_to_id[hotspot.name],
                "symbol": hotspot.name,
                "builddata": [
                    {"id": item, "symbol": row_id_to_name[item]} for item in builddata
                ],
            }
        )

    furniture_records = []
    wiki_matches = 0
    wiki_direct_matches = 0
    wiki_reconciled_matches = 0
    for row in furniture:
        row_id = row_name_to_id[row.name]
        material_values = ints(row, 2)
        requirement = ints(row, 3)
        menu_obj = int(first_value(row, 0, -1))
        display_name = first_value(row, 1, row.name)
        wiki_candidates = []
        for candidate in wiki_by_item_id.get(menu_obj, []):
            built_ids = [
                int(loc_id)
                for loc_id in candidate.get("object_ids", [])
                if int(loc_id) in loc_id_to_name
            ]
            if built_ids:
                wiki_candidates.append((candidate, built_ids))
        if len(wiki_candidates) > 1:
            level = requirement[1] if len(requirement) >= 2 else None
            wiki_candidates = [
                item for item in wiki_candidates if item[0].get("level") == level
            ]
        if len(wiki_candidates) > 1:
            equivalent = {}
            for candidate, candidate_built_ids in wiki_candidates:
                key = (
                    candidate.get("page"),
                    candidate.get("revision_id"),
                    candidate.get("experience"),
                    tuple(sorted(set(candidate_built_ids))),
                )
                equivalent[key] = (candidate, candidate_built_ids)
            wiki_candidates = list(equivalent.values())
        if len(wiki_candidates) > 1:
            raise CatalogError(
                f"{row.name}: Wiki item id {menu_obj} has ambiguous built-loc matches"
            )
        wiki_entry = wiki_candidates[0][0] if wiki_candidates else None
        built_ids = wiki_candidates[0][1] if wiki_candidates else []
        level = requirement[1] if len(requirement) >= 2 else None
        experience = None
        experience_sources = []
        join = "cache menu_obj id = Wiki infobox itemid"
        # Reviewed cache/Wiki drift entries deliberately override a newly
        # discovered item-id join: they may select one scenery version or
        # compose display + mounted-trophy XP from multiple cited pages.
        if row.name in reconciliations:
            wiki_entry, built_ids, experience, experience_sources = reconciled_wiki_data(
                row.name,
                level,
                reconciliations[row.name],
                wiki_by_title,
                loc_id_to_name,
            )
            join = "reviewed symbolic reconciliation + Wiki page revision"
            wiki_reconciled_matches += 1
        elif wiki_entry is not None:
            experience = wiki_experience(wiki_entry, level)
            wiki_direct_matches += 1
        if wiki_entry is not None:
            wiki_matches += 1
        furniture_records.append(
            {
                "id": row_id,
                "symbol": row.name,
                "name": display_name,
                "menu_obj": {"id": menu_obj, "symbol": obj_id_to_name.get(menu_obj)},
                "materials": [
                    {
                        "obj_id": obj_id,
                        "obj": obj_id_to_name.get(obj_id),
                        "quantity": quantity,
                    }
                    for obj_id, quantity in pairs(material_values)
                ],
                "requirements": [
                    {"stat_id": stat_id, "level": level}
                    for stat_id, level in pairs(requirement)
                ],
                "hidden_in_build_menu": int(first_value(row, 5, 0)),
                "upgrade_source_relative": ints(row, 6),
                "upgrade_source_absolute": ints(row, 7),
                "hotspots": sorted(furniture_to_hotspots[row_id]),
                "built_locs": [
                    {"id": loc_id, "symbol": loc_id_to_name[loc_id]}
                    for loc_id in sorted(set(built_ids))
                ],
                "auxiliary_locs": auxiliary_locs.get(row.name, []),
                "flatpack": (
                    {
                        "id": obj_name_to_id[flatpacks[row.name]],
                        "symbol": flatpacks[row.name],
                    }
                    if row.name in flatpacks
                    else None
                ),
                "experience": experience,
                "wiki": wiki_entry.get("wiki") if wiki_entry else wiki_search(display_name),
                "wiki_match": (
                    {
                        "join": join,
                        "page": wiki_entry.get("page"),
                        "revision_id": wiki_entry.get("revision_id"),
                        "revision_timestamp": wiki_entry.get("revision_timestamp"),
                        "experience_sources": experience_sources,
                    }
                    if wiki_entry
                    else None
                ),
                "implementation_gap": (
                    None
                    if wiki_entry
                    else "built loc/variant and XP are absent from the current Wiki snapshot"
                ),
            }
        )

    unresolved = [
        item["symbol"]
        for item in furniture_records
        if not item["built_locs"] or item["experience"] is None
    ]
    if unresolved:
        raise CatalogError(
            f"furniture rows still lack deterministic built locs or XP: {unresolved}"
        )

    room_records = []
    for room in rooms:
        hotspot_ids = ints(room, 7)
        requirement = ints(room, 4)
        display_name = first_value(room, 1, room.name)
        room_records.append(
            {
                "id": row_name_to_id[room.name],
                "symbol": room.name,
                "name": first_value(room, 0, ""),
                "display_name": display_name,
                "cost": int(first_value(room, 2, 0)),
                "room_type": int(first_value(room, 3, -1)),
                "requirements": [
                    {"stat_id": stat_id, "level": level}
                    for stat_id, level in pairs(requirement)
                ],
                "source_offset": ints(room, 5),
                "door_locations": ints(room, 6),
                "hotspots": [
                    {"id": item, "symbol": row_id_to_name[item]} for item in hotspot_ids
                ],
                "floor_restriction": int(first_value(room, 8, 0)),
                "has_roof": int(first_value(room, 9, 1)),
                "room_obj_id": int(first_value(room, 10, -1)),
                "button_component": int(first_value(room, 11, -1)),
                "template_hotspot_placements": room_template_placements(
                    content, room, loc_id_to_name, loc_by_name
                ),
                "wiki": wiki_search(display_name),
            }
        )

    subsection_headers = {}
    for subsection in table_rows(rows, "skill_guide_subsections"):
        skill = ints(subsection, 0)
        section_id = ints(subsection, 1)
        header = subsection.column_values(2)
        if skill and skill[0] == CONSTRUCTION_STAT_ID and section_id and header:
            subsection_headers[section_id[0]] = header[0]

    guide_records = []
    for row in table_rows(rows, "skill_features"):
        metadata = ints(row, 3)
        if len(metadata) < 3 or metadata[0] != CONSTRUCTION_STAT_ID:
            continue
        text = first_value(row, 2, row.name)
        guide_records.append(
            {
                "id": row_name_to_id[row.name],
                "symbol": row.name,
                "level": metadata[1],
                "subsection_id": metadata[2],
                "subsection": subsection_headers[metadata[2]],
                "text": text,
                "icon_obj_ids": ints(row, 0),
                "other_requirements": row.column_values(5),
                "members_only": [bool(item) for item in ints(row, 6)],
                "wiki": wiki_search(text),
            }
        )

    template_placements = [
        placement
        for room in room_records
        for placement in room["template_hotspot_placements"]
    ]

    return {
        "generated_from": "revision-239 cache exports",
        "wiki_contract": {
            "policy": "Wiki links are review targets; cache IDs remain implementation authority.",
            "snapshot": str(wiki_snapshot_path),
            "snapshot_source": wiki_snapshot.get("source"),
            "snapshot_retrieved_at": wiki_snapshot.get("retrieved_at"),
            "reconciliations": str(reconciliations_path),
            "auxiliary_locs": str(auxiliary_locs_path),
            "auxiliary_locs_wiki_source": auxiliary_wiki_source,
            "flatpacks": str(flatpacks_path),
            "flatpacks_wiki_source": flatpack_wiki_source,
            "construction": "https://oldschool.runescape.wiki/w/Construction",
            "constructed_items": "https://oldschool.runescape.wiki/w/Constructed_items",
            "level_up_table": "https://oldschool.runescape.wiki/w/Construction/Level_up_table",
            "player_owned_house": "https://oldschool.runescape.wiki/w/Player-owned_house",
        },
        "summary": {
            **summary,
            "wiki_furniture_matches": wiki_matches,
            "wiki_furniture_direct_id_matches": wiki_direct_matches,
            "wiki_furniture_symbolic_reconciliations": wiki_reconciled_matches,
            "wiki_furniture_unresolved": len(furniture) - wiki_matches,
            "wiki_snapshot_entries": len(wiki_snapshot["entries"]),
            "runtime_furniture_rows": len(furniture_records),
            "runtime_hotspot_loc_rows": len(
                {placement["loc"] for placement in template_placements}
            ),
            "room_template_placements": len(template_placements),
            "runtime_auxiliary_loc_pairs": sum(map(len, auxiliary_locs.values())),
            "runtime_flatpack_rows": len(flatpacks),
        },
        "furniture": sorted(furniture_records, key=lambda item: item["id"]),
        "hotspots": sorted(hotspot_records, key=lambda item: item["id"]),
        "rooms": sorted(room_records, key=lambda item: item["id"]),
        "skill_guide": sorted(
            guide_records,
            key=lambda item: (item["subsection_id"], item["level"], item["id"]),
        ),
    }


def render(
    content: Path, wiki_snapshot: Path, reconciliations: Path,
    auxiliary_locs: Path, flatpacks: Path
) -> str:
    return (
        json.dumps(
            generate(content, wiki_snapshot, reconciliations, auxiliary_locs, flatpacks),
            indent=2,
            ensure_ascii=False,
            sort_keys=True,
        )
        + "\n"
    )


def runtime_row_symbol(prefix: str, symbol: str) -> str:
    """Return a stable server-owned dbrow symbol for a cache symbol."""
    return f"poh_runtime_{prefix}_{re.sub(r'[^a-z0-9_]+', '_', symbol.casefold())}"


def fixed_point_experience(value: int | float) -> int:
    """Convert Wiki display XP to the server's one-decimal fixed-point unit."""
    scaled = float(value) * 10
    if not scaled.is_integer():
        raise CatalogError(f"Construction XP {value!r} is not representable at 0.1 XP")
    return int(scaled)


# Revision 239 contains the Demonic Pacts League Hall scenery, but the checked-in
# Wiki item snapshot predates that League and therefore cannot discover these
# display-only locs through infobox object IDs. Append them after the historical
# variants so existing durable variant indices remain stable. The functional
# League Hall contract and current Wiki are the review authority for this small
# cache-era bridge.
LEAGUE_HALL_RUNTIME_LOC_EXTENSIONS = {
    "poh_leaguehall_trophy_pedestal_simple": [
        f"poh_leaguehall_pedestal_{position}_simple_league_6_{tier}"
        for position in range(1, 4)
        for tier in ("bronze", "iron", "steel", "mithril", "adamant", "rune", "dragon")
    ],
    "poh_leaguehall_trophy_pedestal_decorative": [
        f"poh_leaguehall_pedestal_{position}_decorative_league_6_{tier}"
        for position in range(1, 4)
        for tier in ("bronze", "iron", "steel", "mithril", "adamant", "rune", "dragon")
    ],
    "poh_leaguehall_outfitstand_oak": [
        f"poh_leaguehall_outfitstand_oak_league_6_t{tier}"
        for tier in range(1, 4)
    ],
    "poh_leaguehall_outfitstand_mahogany": [
        f"poh_leaguehall_outfitstand_mahogany_league_6_t{tier}"
        for tier in range(1, 4)
    ],
}


def render_runtime_rows(crosswalk: dict[str, object]) -> str:
    """Render the cache/Wiki join as RuneScript-readable runtime DB rows.

    The furniture table is keyed by the cache's existing furniture dbrow. The
    hotspot table is keyed by an unbuilt loc type and retains every room-local
    placement. Keeping all placements is important for multi-loc furnishings
    (rugs, curtains and doors) and prevents the runtime from guessing by name.
    """
    lines = [
        "// Generated by tools/generate_construction_crosswalk.py.",
        "// Do not hand-edit; refresh the checked-in Wiki snapshot/reconciliations",
        "// and rerun the generator. Cache symbols remain the runtime authority.",
        "",
    ]
    furniture_symbols = [
        runtime_row_symbol("furniture", item["symbol"])
        for item in crosswalk["furniture"]
    ]
    if len(set(furniture_symbols)) != len(furniture_symbols):
        raise CatalogError("runtime furniture row symbols are not unique")
    for furniture, row_symbol in zip(crosswalk["furniture"], furniture_symbols):
        lines.extend(
            [
                f"[{row_symbol}]",
                "table=poh_furniture_runtime",
                f"data=furniture,{furniture['symbol']}",
                f"data=experience,{fixed_point_experience(furniture['experience'])}",
            ]
        )
        for built_loc in furniture["built_locs"]:
            lines.append(f"data=built_loc,{built_loc['symbol']}")
        for built_loc in LEAGUE_HALL_RUNTIME_LOC_EXTENSIONS.get(
            furniture["symbol"], []
        ):
            lines.append(f"data=built_loc,{built_loc}")
        for auxiliary in furniture["auxiliary_locs"]:
            lines.append(
                "data=auxiliary_loc,"
                f"{auxiliary['empty_loc']},{auxiliary['built_loc']}"
            )
        if furniture["flatpack"] is not None:
            lines.append(f"data=flatpack,{furniture['flatpack']['symbol']}")
        lines.append("")

    placements_by_loc: dict[str, list[tuple[str, int, int, int, int, int]]] = defaultdict(list)
    for room in crosswalk["rooms"]:
        for slot, placement in enumerate(room["template_hotspot_placements"]):
            placements_by_loc[placement["loc"]].append(
                (
                    room["symbol"],
                    slot,
                    placement["local_x"],
                    placement["local_z"],
                    placement["shape"],
                    placement["angle"],
                )
            )
    hotspot_symbols = [
        runtime_row_symbol("hotspot", loc_symbol)
        for loc_symbol in sorted(placements_by_loc)
    ]
    if len(set(hotspot_symbols)) != len(hotspot_symbols):
        raise CatalogError("runtime hotspot row symbols are not unique")
    for loc_symbol, row_symbol in zip(sorted(placements_by_loc), hotspot_symbols):
        lines.extend(
            [
                f"[{row_symbol}]",
                "table=poh_hotspot_runtime",
                f"data=unbuilt_loc,{loc_symbol}",
            ]
        )
        for room, slot, local_x, local_z, shape, angle in placements_by_loc[loc_symbol]:
            lines.append(
                f"data=placement,{room},{slot},{local_x},{local_z},{shape},{angle}"
            )
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--content", type=Path, default=Path("OSRS-Content/osrs239-content"))
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("docs/generated/construction_catalog_crosswalk.json"),
    )
    parser.add_argument(
        "--wiki-snapshot",
        type=Path,
        default=Path("tools/data/construction_wiki_items.json"),
    )
    parser.add_argument(
        "--reconciliations",
        type=Path,
        default=Path("tools/data/construction_wiki_reconciliations.json"),
    )
    parser.add_argument(
        "--auxiliary-locs",
        type=Path,
        default=Path("tools/data/construction_runtime_auxiliary_locs.json"),
    )
    parser.add_argument(
        "--flatpacks",
        type=Path,
        default=Path("tools/data/construction_runtime_flatpacks.json"),
    )
    parser.add_argument(
        "--runtime-output",
        type=Path,
        default=Path(
            "OSRS-Content/osrs239-content/server/scripts/skill_construction/"
            "configs/poh_runtime_generated.dbrow"
        ),
    )
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        crosswalk = generate(
            args.content,
            args.wiki_snapshot,
            args.reconciliations,
            args.auxiliary_locs,
            args.flatpacks,
        )
        generated = json.dumps(
            crosswalk, indent=2, ensure_ascii=False, sort_keys=True
        ) + "\n"
        runtime_generated = render_runtime_rows(crosswalk)
        if args.check:
            if args.output.read_text(encoding="utf-8") != generated:
                print(f"construction crosswalk: FAIL: regenerate {args.output}", file=sys.stderr)
                return 1
            if args.runtime_output.read_text(encoding="utf-8") != runtime_generated:
                print(
                    f"construction crosswalk: FAIL: regenerate {args.runtime_output}",
                    file=sys.stderr,
                )
                return 1
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(generated, encoding="utf-8")
            args.runtime_output.parent.mkdir(parents=True, exist_ok=True)
            args.runtime_output.write_text(runtime_generated, encoding="utf-8")
    except (CatalogError, KeyError, OSError, ValueError) as exc:
        print(f"construction crosswalk: FAIL: {exc}", file=sys.stderr)
        return 1
    print(
        "construction crosswalk: OK — "
        f"{args.output}; {args.runtime_output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
