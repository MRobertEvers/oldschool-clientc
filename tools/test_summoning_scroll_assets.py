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
DEFAULT_SPECIAL_MANIFEST = REPO / "docs/summoning_port/special_move_assets_530.ini"
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
    parser.add_argument("--special-manifest", type=Path, default=DEFAULT_SPECIAL_MANIFEST)
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
        records.update(named_records(lane / "configs/summoning_guide_scroll.obj"))
        expected_names = {PREFIX + name for name in exports.values()}
        guide_only_name = "summoning_guide_scroll_fetch_casket_scroll"
        packed_names = expected_names | {guide_only_name}
        expect(set(records) == packed_names,
               "ported scroll configs differ from the familiar and guide-only manifests")

        guide_text = (lane / "configs/summoning_guide.dbrow").read_text(encoding="utf-8")
        guide = {
            match.group(1): match.group(2)
            for match in re.finditer(r"^\[([^]]+)\]\n(.*?)(?=^\[|\Z)", guide_text,
                                     re.MULTILINE | re.DOTALL)
        }
        guide_exceptions = {
            "doomsphere": "doomsphere_device",
            "rise_from_the_ashes": "rise_from_the_ashes_after_in_pyre_need",
            "titans_constitution": "titan_s_constitution",
        }
        for obj_name in exports.values():
            stem = obj_name.removesuffix("_scroll")
            row_name = "summoning_skill_guide_feature_scroll_" + guide_exceptions.get(stem, stem)
            row = guide.get(row_name, "")
            expect("columndef=0:icon,obj\n" in row,
                   f"skill-guide row {row_name} does not declare its object icon")
            expect(f"values=0:0:{PREFIX}{obj_name}\n" in row,
                   f"skill-guide row {row_name} is not bound to {PREFIX}{obj_name}")
        fetch_row = guide.get("summoning_skill_guide_feature_scroll_fetch_casket", "")
        expect(f"values=0:0:{guide_only_name}\n" in fetch_row,
               "Fetch Casket's rev-727 guide row is not bound to its packed object")

        obj_alloc = key_value_lines(lane / "pack/obj.alloc")
        obj_client = {
            line.strip()
            for line in (lane / "pack/obj.client").read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("//")
        }
        allocated = {obj_id: name for obj_id, name in obj_alloc.items() if name in packed_names}
        expect(len(allocated) == 68 and set(allocated.values()) == packed_names,
               "all 68 visible guide scroll objs are not uniquely allocated")
        expect(packed_names <= obj_client, "one or more scroll objs are absent from obj.client")

        model_pack = key_value_lines(lane / "pack/7_models.pack")
        model_ids = {int(record["model"]) for record in records.values()}
        expect(len(model_ids) == 63, f"expected 63 shared inventory models, got {len(model_ids)}")
        for model_id in model_ids:
            packed = model_pack.get(model_id, "")
            expect(packed.startswith(
                (f"{LANE.as_posix()}/summoning_scroll_model_",
                 f"{LANE.as_posix()}/summoning_guide_scroll_model_")),
                   f"scroll inventory model {model_id} is not in 7_models.pack")
            model_files = [
                args.tree / "models" / f"{packed}{suffix}"
                for suffix in (".model", ".ob3")
            ]
            expect(any(path.is_file() for path in model_files),
                   f"packed scroll inventory model is missing: {packed}.model/.ob3")

        ledger = (args.tree / "port/summoning_scrolls_530.map").read_text(encoding="utf-8")
        ledger_objs = {int(match.group(1)) for match in re.finditer(r"^obj\t(\d+)\t", ledger, re.MULTILINE)}
        ledger_models = {int(match.group(1)) for match in re.finditer(r"^model\t(\d+)\t", ledger, re.MULTILINE)}
        expect(ledger_objs == source_ids, "scroll ledger object sources differ from the manifest")
        expect(len(ledger_models) == 62, "scroll ledger does not record all imported models")
        fetch_ledger = (args.tree / "port/summoning_fetch_casket_727.map").read_text(
            encoding="utf-8"
        )
        expect("obj\t19621\tfetch_casket_scroll\t47467\tsummoning_guide_scroll_" in fetch_ledger,
               "Fetch Casket object translation is absent from its rev-727 ledger")
        expect("model\t58228\tmodel_58228\t124062\tsummoning_guide_scroll_" in fetch_ledger,
               "Fetch Casket model translation is absent from its rev-727 ledger")

        # Familiar.java applies one shared owner animation/graphic after every
        # successful special. Pin both that source closure and all 78 dispatch
        # rows so future roster edits cannot regress to a placeholder button.
        special_text = args.special_manifest.read_text(encoding="utf-8")
        expect("[export:seq]\n7660=cast\n" in special_text,
               "shared source special-move sequence 7660 is not imported")
        expect("[export:spotanim]\n1316=gfx\n" in special_text,
               "shared source special-move spotanim 1316 is not imported")
        special_ledger = (args.tree / "port/summoning_special_moves_530.map").read_text(
            encoding="utf-8"
        )
        expect(re.search(r"^seq\t7660\t.*\t25500\tsummoning_special_move_cast\t",
                         special_ledger, re.MULTILINE) is not None,
               "shared special-move sequence is absent from its translation ledger")
        expect(re.search(r"^spotanim\t1316\t.*\t20003\tsummoning_special_move_gfx\t",
                         special_ledger, re.MULTILINE) is not None,
               "shared special-move graphic is absent from its translation ledger")

        server = (args.tree / "server/scripts/ported_scape2009_summoning/scripts/"
                  "summoning_spirit_wolf.rs2").read_text(encoding="utf-8")
        scroll_dispatch = re.findall(
            r"^if \(\$type = (\d+)\) return\((summoning_scroll_[a-z0-9_]+)\);$",
            server[server.index("[proc,summoning_familiar_scroll]"):
                   server.index("[proc,summoning_familiar_special_cost]")],
            re.MULTILINE,
        )
        expect([int(kind) for kind, _scroll in scroll_dispatch] == list(range(1, 79)),
               "special-move scroll dispatch does not cover familiar types 1..78 exactly once")
        expect({scroll for _kind, scroll in scroll_dispatch} == expected_names,
               "special-move dispatch is not the exact 67-object familiar scroll closure")
        cost_dispatch = re.findall(
            r"^if \(\$type = (\d+)\) return\((\d+)\);$",
            server[server.index("[proc,summoning_familiar_special_cost]"):
                   server.index("[proc,summoning_familiar_body_model]")],
            re.MULTILINE,
        )
        expect([int(kind) for kind, _cost in cost_dispatch] == list(range(1, 79)),
               "special-move costs do not cover familiar types 1..78 exactly once")
        expect(all(int(cost) > 0 for _kind, cost in cost_dispatch),
               "every familiar must have a positive special-move cost")
        expect("anim(summoning_special_move_cast, 0);" in server and
               "spotanim_pl(summoning_special_move_gfx, 0, 0);" in server,
               "successful scroll use does not play the shared special visualization")
        expect("inv_del(inv, $scroll, 1);" in server and
               "%summoning_familiar_special = sub(%summoning_familiar_special, $cost);" in server,
               "successful scroll use does not consume its scroll and special points")
    except (AssertionError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"test_summoning_scroll_assets: error: {exc}", file=sys.stderr)
        return 1

    print("test_summoning_scroll_assets: 78 familiar specials, 68 guide objs, shared animation, 0 errors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
