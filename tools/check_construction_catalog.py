#!/usr/bin/env python3
"""Validate the revision-239 cache data that drives Construction.

The server imports DBTABLE/DBROW records from the binary cache at boot. This
check guards the source-side contract before RuneScript relies on it: counts,
schemas, row membership, hotspot/furniture references, upgrade references, and
the complete Construction skill-guide inventory.

The machine-exported config files are Windows-1252, not UTF-8. Reading them
with the wrong codec is deliberately a hard failure elsewhere; this tool names
the encoding explicitly so a non-breaking space cannot make catalog validation
depend on the caller's locale.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


EXPECTED_TABLE_SCHEMAS = {
    "furniture": {
        0: ("model_obj", ("obj",)),
        1: ("name", ("string",)),
        2: ("material_cost", ("obj", "int")),
        3: ("level_requirement", ("stat", "int")),
        5: ("hidden_in_build_menu", ("int",)),
        6: ("upgrade_source_relative", ("int",)),
        7: ("upgrade_source_absolute", ("int",)),
    },
    "poh_room": {
        0: ("name", ("string",)),
        1: ("name_uppercase", ("string",)),
        2: ("cost", ("int",)),
        3: ("room_type", ("int",)),
        4: ("level_requirement", ("stat", "int")),
        5: ("source_offset", ("int", "int")),
        6: ("door_locations", ("int",)),
        7: ("hotspot", ("dbrow",)),
        8: ("floor_restriction", ("int",)),
        9: ("has_roof", ("int",)),
        10: ("room_obj", ("obj",)),
        11: ("button", ("component",)),
    },
    "poh_hotspot": {0: ("builddata", ("dbrow",))},
    "skill_features": {
        0: ("icon", ("obj",)),
        1: ("sprite", ("graphic", "int", "int", "int", "int")),
        2: ("text", ("string",)),
        3: ("skill", ("int", "int", "int")),
        4: ("quest", ("dbrow",)),
        5: ("otherreq", ("string",)),
        6: ("membersonly", ("boolean",)),
        7: ("otherdata_magic", ("obj",)),
        8: ("otherdata_sailing", ("obj",)),
        9: ("otherdata_construction", ("obj",)),
    },
}

EXPECTED_COUNTS = {
    "furniture": 525,
    "poh_room": 30,
    "poh_hotspot": 123,
    "construction_skill_features": 537,
}

EXPECTED_GUIDE_SECTIONS = {
    "Overview": 5,
    "Rooms": 25,
    "Skills": 35,
    "Surfaces": 38,
    "Storage": 73,
    "Decorative": 39,
    "Trophies": 60,
    "Games": 29,
    "Garden": 64,
    "Dungeon": 40,
    "Chapel": 31,
    "Other": 65,
    "Servants": 6,
    "House Size": 20,
    "Boats": 7,
}

CONSTRUCTION_STAT_ID = 22


class CatalogError(RuntimeError):
    pass


@dataclass(frozen=True)
class Block:
    name: str
    entries: tuple[tuple[str, str], ...]

    def first(self, key: str) -> str | None:
        return next((value for candidate, value in self.entries if candidate == key), None)

    def all(self, key: str) -> list[str]:
        return [value for candidate, value in self.entries if candidate == key]

    def column_values(self, column: int) -> list[str]:
        prefix = f"{column}:"
        result: list[str] = []
        for raw in self.all("values"):
            if not raw.startswith(prefix):
                continue
            _, _, payload = raw.split(":", 2)
            result.extend(split_cache_csv(payload))
        return result


def split_cache_csv(value: str) -> list[str]:
    """Split cache CSV where a comma may be escaped by one or more slashes."""
    fields: list[str] = []
    current: list[str] = []
    slash_count = 0
    for char in value:
        if char == "\\":
            slash_count += 1
            continue
        if char == "," and slash_count:
            current.append(",")
        elif char == ",":
            fields.append("".join(current))
            current = []
        else:
            current.extend("\\" * slash_count)
            current.append(char)
        slash_count = 0
    current.extend("\\" * slash_count)
    fields.append("".join(current))
    return fields


def parse_blocks(text: str) -> list[Block]:
    blocks: list[Block] = []
    name: str | None = None
    entries: list[tuple[str, str]] = []
    for line_number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("//") or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            if name is not None:
                blocks.append(Block(name, tuple(entries)))
            name = line[1:-1]
            entries = []
            continue
        if name is None:
            continue
        if "=" not in line:
            raise CatalogError(f"line {line_number}: expected key=value inside [{name}]")
        key, value = line.split("=", 1)
        entries.append((key.strip(), value.strip()))
    if name is not None:
        blocks.append(Block(name, tuple(entries)))
    return blocks


def read_blocks(path: Path) -> list[Block]:
    return parse_blocks(path.read_text(encoding="cp1252"))


def read_compack(path: Path) -> tuple[dict[int, str], dict[str, int]]:
    by_id: dict[int, str] = {}
    by_name: dict[str, int] = {}
    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("//"):
            continue
        if "=" not in line:
            raise CatalogError(f"{path}:{line_number}: expected id=name")
        raw_id, name = line.split("=", 1)
        record_id = int(raw_id)
        if record_id in by_id or name in by_name:
            raise CatalogError(f"{path}:{line_number}: duplicate DB symbol {line}")
        by_id[record_id] = name
        by_name[name] = record_id
    return by_id, by_name


def parse_schema(block: Block) -> dict[int, tuple[str, tuple[str, ...]]]:
    schema: dict[int, tuple[str, tuple[str, ...]]] = {}
    for raw in block.all("columndef"):
        raw_column, definition = raw.split(":", 1)
        parts = split_cache_csv(definition)
        if len(parts) < 2:
            raise CatalogError(f"{block.name}: malformed columndef {raw}")
        schema[int(raw_column)] = (parts[0], tuple(parts[1:]))
    return schema


def require_ints(block: Block, column: int) -> list[int]:
    result: list[int] = []
    for value in block.column_values(column):
        try:
            result.append(int(value))
        except ValueError as exc:
            raise CatalogError(
                f"{block.name}: column {column} expected integers, found {value!r}"
            ) from exc
    return result


def table_rows(blocks: Iterable[Block], table: str) -> list[Block]:
    return [block for block in blocks if block.first("table") == table]


def validate(content: Path) -> dict[str, object]:
    configs = content / "configs"
    tables = {block.name: block for block in read_blocks(configs / "all.dbtable")}
    rows = read_blocks(configs / "all.dbrow")
    row_id_to_name, row_name_to_id = read_compack(configs / "all.dbrow.compack")
    table_id_to_name, table_name_to_id = read_compack(configs / "all.dbtable.compack")

    errors: list[str] = []
    for table, expected in EXPECTED_TABLE_SCHEMAS.items():
        actual = parse_schema(tables[table]) if table in tables else None
        if actual != expected:
            errors.append(f"{table}: schema mismatch\n  expected {expected}\n  actual   {actual}")
        if table not in table_name_to_id:
            errors.append(f"{table}: missing from all.dbtable.compack")

    by_name = {block.name: block for block in rows}
    for block in rows:
        if block.name not in row_name_to_id:
            errors.append(f"{block.name}: missing from all.dbrow.compack")

    furniture = table_rows(rows, "furniture")
    rooms = table_rows(rows, "poh_room")
    hotspots = table_rows(rows, "poh_hotspot")
    skill_features = table_rows(rows, "skill_features")
    construction_features = []
    construction_feature_meta: dict[str, tuple[int, int, int]] = {}
    for block in skill_features:
        values = require_ints(block, 3)
        if len(values) % 3:
            errors.append(f"{block.name}: skill metadata is not skill/level/subsection tuples")
            continue
        tuples = [tuple(values[index : index + 3]) for index in range(0, len(values), 3)]
        # A cross-skill unlock is shown in the guide of its first tuple. Some
        # Construction rows then carry a Sailing/Hunter prerequisite as a
        # second tuple; conversely, Sailing rows may carry Construction second.
        # Counting any tuple would merge those other guides into this one.
        construction = tuples[0] if tuples and tuples[0][0] == CONSTRUCTION_STAT_ID else None
        if construction is not None:
            construction_features.append(block)
            construction_feature_meta[block.name] = construction

    actual_counts = {
        "furniture": len(furniture),
        "poh_room": len(rooms),
        "poh_hotspot": len(hotspots),
        "construction_skill_features": len(construction_features),
    }
    if actual_counts != EXPECTED_COUNTS:
        errors.append(f"catalog counts mismatch: expected {EXPECTED_COUNTS}, got {actual_counts}")

    furniture_ids = {row_name_to_id[block.name] for block in furniture}
    hotspot_ids = {row_name_to_id[block.name] for block in hotspots}

    for block in furniture:
        materials = require_ints(block, 2)
        if len(materials) % 2:
            errors.append(f"{block.name}: material_cost is not obj/count tuples")
        if any(quantity <= 0 for quantity in materials[1::2]):
            errors.append(f"{block.name}: material quantity must be positive")
        requirement = require_ints(block, 3)
        if len(requirement) % 2 or (
            requirement
            and CONSTRUCTION_STAT_ID not in requirement[0::2]
        ):
            errors.append(f"{block.name}: invalid Construction requirement {requirement}")

        relative = require_ints(block, 6)
        absolute = require_ints(block, 7)
        if relative and absolute:
            errors.append(f"{block.name}: both relative and absolute upgrade sources are set")
        # These columns are positions in the active hotspot's builddata list,
        # not DBROW ids. Their names are unfortunately easy to misread as row
        # references; non-negative positions plus mutual exclusion are the
        # source-level invariants available without evaluating each client menu.
        if any(source < 0 for source in relative + absolute):
            errors.append(f"{block.name}: upgrade source positions must be non-negative")

    room_hotspot_refs = 0
    for block in rooms:
        requirement = require_ints(block, 4)
        if len(requirement) % 2 or (
            requirement
            and CONSTRUCTION_STAT_ID not in requirement[0::2]
        ):
            errors.append(f"{block.name}: invalid Construction requirement {requirement}")
        offsets = require_ints(block, 5)
        if len(offsets) != 2:
            errors.append(f"{block.name}: source_offset must be one int/int tuple")
        for hotspot in require_ints(block, 7):
            room_hotspot_refs += 1
            if hotspot not in hotspot_ids:
                errors.append(
                    f"{block.name}: hotspot {hotspot} resolves to "
                    f"{row_id_to_name.get(hotspot, 'no row')}, not poh_hotspot"
                )

    builddata_refs = 0
    referenced_furniture: set[int] = set()
    for block in hotspots:
        for furniture_id in require_ints(block, 0):
            builddata_refs += 1
            referenced_furniture.add(furniture_id)
            if furniture_id not in furniture_ids:
                errors.append(
                    f"{block.name}: builddata {furniture_id} resolves to "
                    f"{row_id_to_name.get(furniture_id, 'no row')}, not furniture"
                )

    subsection_headers: dict[int, str] = {}
    for block in table_rows(rows, "skill_guide_subsections"):
        skill = require_ints(block, 0)
        subsection = require_ints(block, 1)
        header = block.column_values(2)
        if skill and skill[0] == CONSTRUCTION_STAT_ID and subsection and header:
            subsection_headers[subsection[0]] = header[0]
    guide_counts: Counter[str] = Counter()
    for block in construction_features:
        metadata = construction_feature_meta[block.name]
        header = subsection_headers.get(metadata[2])
        if header is None:
            errors.append(f"{block.name}: unknown Construction subsection {metadata[2]}")
        else:
            guide_counts[header] += 1
    if dict(guide_counts) != EXPECTED_GUIDE_SECTIONS:
        errors.append(
            f"skill-guide section counts mismatch: expected {EXPECTED_GUIDE_SECTIONS}, "
            f"got {dict(guide_counts)}"
        )

    if errors:
        raise CatalogError("\n".join(errors[:100]))

    return {
        "cache_revision": (content / "meta.ini").read_text(encoding="utf-8").strip(),
        "tables": {name: table_name_to_id[name] for name in EXPECTED_TABLE_SCHEMAS},
        "counts": actual_counts,
        "guide_sections": dict(guide_counts),
        "room_hotspot_references": room_hotspot_refs,
        "hotspot_builddata_references": builddata_refs,
        "referenced_furniture": len(referenced_furniture),
        "unreferenced_furniture": len(furniture_ids - referenced_furniture),
    }


def self_test() -> None:
    fixture = """\
[row]
table=furniture
columndef=2:material_cost,obj,int
values=2:0:960,2
values=1:0:Name\\\\, with comma
"""
    blocks = parse_blocks(fixture)
    assert len(blocks) == 1
    assert blocks[0].first("table") == "furniture"
    assert blocks[0].column_values(2) == ["960", "2"]
    assert blocks[0].column_values(1) == ["Name, with comma"]
    assert split_cache_csv(r"a\\,b,c") == ["a,b", "c"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--content",
        type=Path,
        default=Path("OSRS-Content/osrs239-content"),
        help="revision content root",
    )
    parser.add_argument("--json", action="store_true", help="emit the validated summary as JSON")
    parser.add_argument("--self-test", action="store_true", help="run parser tests first")
    args = parser.parse_args()

    try:
        if args.self_test:
            self_test()
        summary = validate(args.content)
    except (CatalogError, KeyError, OSError, ValueError) as exc:
        print(f"construction catalog: FAIL: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        counts = summary["counts"]
        print(
            "construction catalog: OK — "
            f"{counts['furniture']} furniture, "
            f"{counts['poh_room']} rooms, "
            f"{counts['poh_hotspot']} hotspots, "
            f"{counts['construction_skill_features']} guide rows"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
