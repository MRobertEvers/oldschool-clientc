#!/usr/bin/env python3
"""Structural acceptance for every active familiar's packed scroll asset."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
DEFAULT_TREE = REPO / "OSRS-Content/osrs239-content"
DEFAULT_MANIFEST = REPO / "docs/summoning_port/scroll_assets_530.ini"
DEFAULT_POUCHES = REPO / "docs/summoning_port/pouches_530.json"
DEFAULT_SOURCE = (
    REPO.parent
    / "2009scape/Server/src/main/content/global/skill/summoning/SummoningScroll.java"
)
LANE = Path("ported/scape2009_summoning")
PREFIX = "summoning_scroll_"


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def parse_exports(path: Path) -> dict[int, str]:
    exports: dict[int, str] = {}
    section = ""
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        if section == "export:obj":
            source, name = line.split("=", 1)
            expect(int(source) not in exports, f"duplicate scroll source obj {source}")
            exports[int(source)] = name
    return exports


def source_scrolls(path: Path) -> list[tuple[str, int, float, int]]:
    pattern = re.compile(
        r"^\s*(?://\s*)?([A-Z][A-Z0-9_]+)_SCROLL\d*"
        r"\(-?\d+,\s*(\d+),\s*([0-9.]+),\s*\d+,\s*(-?\d+)\)",
        re.MULTILINE,
    )
    return [
        (name, int(scroll), float(xp), int(pouch))
        for name, scroll, xp, pouch in pattern.findall(path.read_text(encoding="utf-8"))
    ]


def named_records(path: Path) -> dict[str, dict[str, str]]:
    records: dict[str, dict[str, str]] = {}
    current: dict[str, str] | None = None
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("[") and line.endswith("]"):
            name = line[1:-1]
            expect(name not in records, f"duplicate config record {name}")
            current = records.setdefault(name, {})
        elif line and not line.startswith("//") and current is not None:
            key, value = line.split("=", 1)
            current[key] = value
    return records


def key_value_lines(path: Path) -> dict[int, str]:
    result: dict[int, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("//"):
            continue
        key, value = line.split("=", 1)
        result[int(key)] = value
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--pouches", type=Path, default=DEFAULT_POUCHES)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    args = parser.parse_args()

    try:
        exports = parse_exports(args.manifest)
        source_rows = source_scrolls(args.source)
        expect(len(source_rows) == 78, f"expected 78 active familiar scroll mappings, got {len(source_rows)}")

        # Correct the three source mapping defects documented by the port design.
        corrected_pouches = [
            ({"DOOMSPHERE": 12023, "DEADLY_CLAW": 12794,
              "RISE_FROM_THE_ASHES": 14623}.get(name, pouch))
            for name, _scroll, _xp, pouch in source_rows
        ]
        active_pouches = {
            int(row["pouch"])
            for row in json.loads(args.pouches.read_text(encoding="utf-8"))
            if int(row["slot"]) >= 0
        }
        expect(len(active_pouches) == 78, "pouch inventory no longer contains 78 active familiars")
        expect(set(corrected_pouches) == active_pouches,
               "the source scroll table does not cover every active familiar after documented corrections")

        source_ids = {scroll for _name, scroll, _xp, _pouch in source_rows}
        expect(len(source_ids) == 67, f"expected 67 distinct scroll objs, got {len(source_ids)}")
        expect(set(exports) == source_ids, "scroll import manifest is not the exact source object set")
        expect(exports.get(14622) == "rise_from_the_ashes_scroll",
               "Phoenix's commented source scroll is not explicitly imported")

        lane = args.tree / LANE
        records = named_records(lane / "configs/summoning_scroll.obj")
        expected_names = {PREFIX + name for name in exports.values()}
        expect(set(records) == expected_names, "ported scroll config records differ from the manifest")

        obj_alloc = key_value_lines(lane / "pack/obj.alloc")
        obj_client = {
            line.strip()
            for line in (lane / "pack/obj.client").read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("//")
        }
        allocated = {obj_id: name for obj_id, name in obj_alloc.items() if name in expected_names}
        expect(len(allocated) == 67 and set(allocated.values()) == expected_names,
               "all 67 scroll objs are not uniquely allocated")
        expect(expected_names <= obj_client, "one or more scroll objs are absent from obj.client")

        model_pack = key_value_lines(lane / "pack/7_models.pack")
        model_ids = {int(record["model"]) for record in records.values()}
        expect(len(model_ids) == 62, f"expected 62 shared inventory models, got {len(model_ids)}")
        for model_id in model_ids:
            packed = model_pack.get(model_id, "")
            expect(packed.startswith(f"{LANE.as_posix()}/summoning_scroll_model_"),
                   f"scroll inventory model {model_id} is not in 7_models.pack")
            expect((args.tree / "models" / f"{packed}.model").is_file(),
                   f"packed scroll inventory model is missing: {packed}.model")

        ledger = (args.tree / "port/summoning_scrolls_530.map").read_text(encoding="utf-8")
        ledger_objs = {int(match.group(1)) for match in re.finditer(r"^obj\t(\d+)\t", ledger, re.MULTILINE)}
        ledger_models = {int(match.group(1)) for match in re.finditer(r"^model\t(\d+)\t", ledger, re.MULTILINE)}
        expect(ledger_objs == source_ids, "scroll ledger object sources differ from the manifest")
        expect(len(ledger_models) == 62, "scroll ledger does not record all imported models")
    except (AssertionError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"test_summoning_scroll_assets: error: {exc}", file=sys.stderr)
        return 1

    print("test_summoning_scroll_assets: 78 familiar mappings, 67 objs, 62 models, 0 errors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
