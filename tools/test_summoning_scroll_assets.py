#!/usr/bin/env python3
"""Structural acceptance for every active familiar's packed scroll asset."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

from summoning_script_sources import definition, read_module


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
        for source, name in ((8136, "call_to_arms_start"), (8137, "call_to_arms_end")):
            expect(f"{source}={name}" in special_text,
                   f"Call to Arms source sequence {source} is not imported")
        for source, name in ((1503, "call_to_arms_start_gfx"), (1502, "call_to_arms_end_gfx")):
            expect(f"{source}={name}" in special_text,
                   f"Call to Arms source graphic {source} is not imported")
        expect("5387=dreadfowl_strike" in special_text,
               "Dreadfowl Strike's source animation is not imported")
        for source, name in ((1523, "dreadfowl_strike_gfx"),
                             (1318, "dreadfowl_strike_projectile")):
            expect(f"{source}={name}" in special_text,
                   f"Dreadfowl Strike source graphic {source} is not imported")
        expect("8148=thorny_snail_slime_spray" in special_text,
               "Slime Spray's source animation is not imported")
        for source, name in ((1385, "thorny_snail_slime_spray_gfx"),
                             (1386, "thorny_snail_slime_spray_projectile"),
                             (1387, "thorny_snail_slime_spray_impact_gfx")):
            expect(f"{source}={name}" in special_text,
                   f"Slime Spray source graphic {source} is not imported")
        expect("7795=desert_wyrm_electric_lash" in special_text,
               "Electric Lash's source animation is not imported")
        for source, name in ((1410, "desert_wyrm_electric_lash_gfx"),
                             (1411, "desert_wyrm_electric_lash_projectile")):
            expect(f"{source}={name}" in special_text,
                   f"Electric Lash source graphic {source} is not imported")
        expect("8275=vampire_bat_vampyre_touch" in special_text,
               "Vampyre Touch's source animation is not imported")
        expect("1323=vampire_bat_vampyre_touch_gfx" in special_text,
               "Vampyre Touch's source graphic is not imported")
        for source, name in ((7762, "petrifying_gaze"), (8026, "bull_rush")):
            expect(f"{source}={name}" in special_text,
                   f"shared direct-combat source sequence {source} is not imported")
        expect("5229=rending" in special_text,
               "Rending's source sequence is not imported")
        for source, name in ((1467, "petrifying_gaze_gfx"),
                             (1468, "petrifying_gaze_projectile"),
                             (1469, "petrifying_gaze_impact_gfx"),
                             (1496, "bull_rush_gfx"),
                             (1497, "bull_rush_projectile")):
            expect(f"{source}={name}" in special_text,
                   f"shared direct-combat source graphic {source} is not imported")
        for source, name in ((1370, "rending_gfx"), (1371, "rending_projectile")):
            expect(f"{source}={name}" in special_text,
                   f"Rending source graphic {source} is not imported")
        expect("7722=beaver_multichop" in special_text,
               "Multichop's source animation is not imported")
        for source, name in ((1393, "forge_regent_inferno_target_gfx"),
                             (1394, "forge_regent_inferno_gfx")):
            expect(f"{source}={name}" in special_text,
                   f"Forge Regent source graphic {source} is not imported")
        for source, name in ((1346, "ravenous_locust_famine_gfx"),
                             (1347, "ravenous_locust_famine_target_gfx")):
            expect(f"{source}={name}" in special_text,
                   f"Ravenous Locust source graphic {source} is not imported")
        for source, name in (
            (7928, "honey_badger_insane_ferocity"),
            (7998, "ravenous_locust_famine"),
            (7871, "forge_regent_inferno"),
            (7858, "giant_ent_acorn_missile"),
            (8223, "swamp_titan_swamp_plague"),
            (7963, "karamthulhu_doomsphere_device"),
            (8069, "praying_mantis_mantis_strike"),
            (5989, "talon_beast_deadly_claw"),
            (7786, "spirit_dagannoth_spike_shot"),
        ):
            expect(f"{source}={name}" in special_text,
                   f"reconstructed special sequence {source} is not imported")
        for source, name in (
            (1397, "honey_badger_insane_ferocity_gfx"),
            (1399, "honey_badger_insane_ferocity_owner_gfx"),
            (1348, "ravenous_locust_famine_impact_gfx"),
            (1362, "giant_ent_acorn_missile_projectile"),
            (1363, "giant_ent_acorn_missile_impact_gfx"),
            (1491, "lava_titan_ebon_thunder_gfx"),
            (1460, "swamp_titan_swamp_plague_gfx"),
            (1462, "swamp_titan_swamp_plague_projectile"),
        ):
            expect(f"{source}={name}" in special_text,
                   f"reconstructed special graphic {source} is not imported")
        expect("phoenix_rise_from_the_ashes" not in special_text,
               "Phoenix must retain its documented cache-native sequence fallback")
        expect("8183=iron_titan_swing" in special_text and
               "1450=iron_titan_iron_within_gfx" in special_text,
               "Iron Within's source normal-swing sequence or charge graphic is not imported")
        expect("8190=steel_titan_swing" in special_text and
               "1445=steel_titan_ranged_projectile" in special_text and
               "1449=steel_titan_steel_of_legends_gfx" in special_text,
               "Steel of Legends' source visual closure is not imported")
        expect("6261=spirit_scorpion_venom_shot" in special_text and
               "1354=spirit_scorpion_venom_shot_gfx" in special_text and
               "1355=spirit_scorpion_venom_shot_projectile" in special_text,
               "Venom Shot's source animation/graphics closure is not imported")
        for source, name in ((8257, "spirit_tz_kih_fireball_assault"),
                             (7758, "giant_chinchompa_explode"),
                             (7820, "smoke_devil_dust_cloud")):
            expect(f"{source}={name}" in special_text,
                   f"bounded-area familiar source sequence {source} is not imported")
        expect("1364=giant_chinchompa_explode_gfx" in special_text,
               "Giant Chinchompa's source graphic is not imported")
        expect("8517=spirit_kalphite_sandstorm" in special_text,
               "Sandstorm's source familiar sequence is not imported")
        for source, name in ((1350, "spirit_kalphite_sandstorm_gfx"),
                             (1349, "spirit_kalphite_sandstorm_projectile")):
            expect(f"{source}={name}" in special_text,
                   f"Sandstorm source graphic {source} is not imported")
        for source, name in ((1511, "beaver_logs"), (2862, "beaver_achey_tree_logs"),
                             (1521, "beaver_oak_logs"), (1519, "beaver_willow_logs"),
                             (6333, "beaver_teak_logs"), (10810, "beaver_arctic_pine_logs"),
                             (1517, "beaver_maple_logs"), (6332, "beaver_mahogany_logs"),
                             (12581, "beaver_eucalyptus_logs"), (960, "beaver_yew_logs"),
                             (8778, "beaver_magic_logs")):
            expect(f"{source}={name}" in special_text,
                   f"Multichop source log {source} is not imported")
        special_ledger = (args.tree / "port/summoning_special_moves_530.map").read_text(
            encoding="utf-8"
        )
        expect(re.search(r"^seq\t7660\t.*\t25500\tsummoning_special_move_cast\t",
                         special_ledger, re.MULTILINE) is not None,
               "shared special-move sequence is absent from its translation ledger")
        expect(re.search(r"^spotanim\t1316\t.*\t20003\tsummoning_special_move_gfx\t",
                         special_ledger, re.MULTILINE) is not None,
               "shared special-move graphic is absent from its translation ledger")
        for kind, source, runtime in (
            ("spotanim", 1350, "summoning_special_move_spirit_kalphite_sandstorm_gfx"),
            ("spotanim", 1349, "summoning_special_move_spirit_kalphite_sandstorm_projectile"),
            ("seq", 8517, "summoning_special_move_spirit_kalphite_sandstorm"),
        ):
            expect(re.search(rf"^{kind}\t{source}\t.*\t{runtime}\t",
                             special_ledger, re.MULTILINE) is not None,
                   f"Sandstorm {kind} source {source} is absent from its translation ledger")
        for source, name in ((8136, "call_to_arms_start"), (8137, "call_to_arms_end")):
            expect(re.search(rf"^seq\t{source}\t.*\tsummoning_special_move_{name}\t",
                             special_ledger, re.MULTILINE) is not None,
                   f"Call to Arms source sequence {source} is absent from its ledger")
        for source, name in ((1503, "call_to_arms_start_gfx"), (1502, "call_to_arms_end_gfx")):
            expect(re.search(rf"^spotanim\t{source}\t.*\tsummoning_special_move_{name}\t",
                             special_ledger, re.MULTILINE) is not None,
                   f"Call to Arms source graphic {source} is absent from its ledger")
        expect(re.search(r"^seq\t5387\t.*\tsummoning_special_move_dreadfowl_strike\t",
                         special_ledger, re.MULTILINE) is not None,
               "Dreadfowl Strike animation is absent from its ledger")
        for source, name in ((1523, "dreadfowl_strike_gfx"),
                             (1318, "dreadfowl_strike_projectile")):
            expect(re.search(rf"^spotanim\t{source}\t.*\tsummoning_special_move_{name}\t",
                             special_ledger, re.MULTILINE) is not None,
                   f"Dreadfowl Strike graphic {source} is absent from its ledger")
        expect(re.search(r"^seq\t8148\t.*\tsummoning_special_move_thorny_snail_slime_spray\t",
                         special_ledger, re.MULTILINE) is not None,
               "Slime Spray animation is absent from its ledger")
        for source, name in ((1385, "thorny_snail_slime_spray_gfx"),
                             (1386, "thorny_snail_slime_spray_projectile"),
                             (1387, "thorny_snail_slime_spray_impact_gfx")):
            expect(re.search(rf"^spotanim\t{source}\t.*\tsummoning_special_move_{name}\t",
                             special_ledger, re.MULTILINE) is not None,
                   f"Slime Spray graphic {source} is absent from its ledger")
        expect(re.search(r"^seq\t7795\t.*\tsummoning_special_move_desert_wyrm_electric_lash\t",
                         special_ledger, re.MULTILINE) is not None,
               "Electric Lash animation is absent from its ledger")
        for source, name in ((1410, "desert_wyrm_electric_lash_gfx"),
                             (1411, "desert_wyrm_electric_lash_projectile")):
            expect(re.search(rf"^spotanim\t{source}\t.*\tsummoning_special_move_{name}\t",
                             special_ledger, re.MULTILINE) is not None,
                   f"Electric Lash graphic {source} is absent from its ledger")
        expect(re.search(r"^seq\t8275\t.*\tsummoning_special_move_vampire_bat_vampyre_touch\t",
                         special_ledger, re.MULTILINE) is not None,
               "Vampyre Touch animation is absent from its ledger")
        expect(re.search(r"^spotanim\t1323\t.*\tsummoning_special_move_vampire_bat_vampyre_touch_gfx\t",
                         special_ledger, re.MULTILINE) is not None,
               "Vampyre Touch graphic is absent from its ledger")

        for source, name in ((7762, "petrifying_gaze"), (8026, "bull_rush")):
            expect(re.search(rf"^seq\t{source}\t.*\tsummoning_special_move_{name}\t",
                             special_ledger, re.MULTILINE) is not None,
                   f"shared direct-combat sequence {source} is absent from its ledger")
        expect(re.search(r"^seq\t5229\t.*\tsummoning_special_move_rending\t",
                         special_ledger, re.MULTILINE) is not None,
               "Rending sequence is absent from its ledger")
        for source, name in ((1467, "petrifying_gaze_gfx"),
                             (1468, "petrifying_gaze_projectile"),
                             (1469, "petrifying_gaze_impact_gfx"),
                             (1496, "bull_rush_gfx"),
                             (1497, "bull_rush_projectile")):
            expect(re.search(rf"^spotanim\t{source}\t.*\tsummoning_special_move_{name}\t",
                             special_ledger, re.MULTILINE) is not None,
                   f"shared direct-combat graphic {source} is absent from its ledger")
        for source, name in ((1370, "rending_gfx"), (1371, "rending_projectile")):
            expect(re.search(rf"^spotanim\t{source}\t.*\tsummoning_special_move_{name}\t",
                             special_ledger, re.MULTILINE) is not None,
                   f"Rending graphic {source} is absent from its ledger")
        expect(re.search(r"^seq\t7722\t.*\tsummoning_special_move_beaver_multichop\t",
                         special_ledger, re.MULTILINE) is not None,
               "Multichop animation is absent from its translation ledger")
        for source, name in ((1393, "forge_regent_inferno_target_gfx"),
                             (1394, "forge_regent_inferno_gfx")):
            expect(re.search(rf"^spotanim\t{source}\t.*\tsummoning_special_move_{name}\t",
                             special_ledger, re.MULTILINE) is not None,
                   f"Forge Regent graphic {source} is absent from its translation ledger")
        for source, name in ((1346, "ravenous_locust_famine_gfx"),
                             (1347, "ravenous_locust_famine_target_gfx")):
            expect(re.search(rf"^spotanim\t{source}\t.*\tsummoning_special_move_{name}\t",
                             special_ledger, re.MULTILINE) is not None,
                   f"Ravenous Locust graphic {source} is absent from its translation ledger")
        for kind, source, name in (
            ("seq", 7928, "honey_badger_insane_ferocity"),
            ("seq", 7998, "ravenous_locust_famine"),
            ("seq", 7871, "forge_regent_inferno"),
            ("seq", 7858, "giant_ent_acorn_missile"),
            ("seq", 8223, "swamp_titan_swamp_plague"),
            ("seq", 7963, "karamthulhu_doomsphere_device"),
            ("seq", 8069, "praying_mantis_mantis_strike"),
            ("seq", 5989, "talon_beast_deadly_claw"),
            ("seq", 7786, "spirit_dagannoth_spike_shot"),
            ("spotanim", 1397, "honey_badger_insane_ferocity_gfx"),
            ("spotanim", 1399, "honey_badger_insane_ferocity_owner_gfx"),
            ("spotanim", 1348, "ravenous_locust_famine_impact_gfx"),
            ("spotanim", 1362, "giant_ent_acorn_missile_projectile"),
            ("spotanim", 1363, "giant_ent_acorn_missile_impact_gfx"),
            ("spotanim", 1491, "lava_titan_ebon_thunder_gfx"),
            ("spotanim", 1460, "swamp_titan_swamp_plague_gfx"),
            ("spotanim", 1462, "swamp_titan_swamp_plague_projectile"),
        ):
            expect(re.search(
                rf"^{kind}\t{source}\t.*\tsummoning_special_move_{name}\t",
                special_ledger,
                re.MULTILINE,
            ) is not None, f"reconstructed {kind} source {source} is absent from its ledger")
        expect(re.search(r"^seq\t8183\t.*\tsummoning_special_move_iron_titan_swing\t",
                         special_ledger, re.MULTILINE) is not None and
               re.search(r"^spotanim\t1450\t.*\tsummoning_special_move_iron_titan_iron_within_gfx\t",
                         special_ledger, re.MULTILINE) is not None,
               "Iron Within's source animation closure is absent from its ledger")
        expect(re.search(r"^seq\t8190\t.*\tsummoning_special_move_steel_titan_swing\t",
                         special_ledger, re.MULTILINE) is not None and
               re.search(r"^spotanim\t1445\t.*\tsummoning_special_move_steel_titan_ranged_projectile\t",
                         special_ledger, re.MULTILINE) is not None and
               re.search(r"^spotanim\t1449\t.*\tsummoning_special_move_steel_titan_steel_of_legends_gfx\t",
                         special_ledger, re.MULTILINE) is not None,
               "Steel of Legends' source visual closure is absent from its ledger")
        expect(re.search(r"^seq\t6261\t.*\tsummoning_special_move_spirit_scorpion_venom_shot\t",
                         special_ledger, re.MULTILINE) is not None and
               re.search(r"^spotanim\t1354\t.*\tsummoning_special_move_spirit_scorpion_venom_shot_gfx\t",
                         special_ledger, re.MULTILINE) is not None and
               re.search(r"^spotanim\t1355\t.*\tsummoning_special_move_spirit_scorpion_venom_shot_projectile\t",
                         special_ledger, re.MULTILINE) is not None,
               "Venom Shot's source visual closure is absent from its ledger")
        expect(re.search(r"^seq\t8211\t.*\tsummoning_special_move_stranger_plant_poisonous_blast\t",
                         special_ledger, re.MULTILINE) is not None,
               "Poisonous Blast's source sequence is absent from its ledger")
        for source, name in ((1508, "stranger_plant_poisonous_blast_projectile"),
                             (1511, "stranger_plant_poisonous_blast_impact_gfx")):
            expect(re.search(rf"^spotanim\t{source}\t.*\tsummoning_special_move_{name}\t",
                             special_ledger, re.MULTILINE) is not None,
                   f"Poisonous Blast graphic {source} is absent from its ledger")
        for source, name in ((8257, "spirit_tz_kih_fireball_assault"),
                             (7758, "giant_chinchompa_explode"),
                             (7820, "smoke_devil_dust_cloud")):
            expect(re.search(rf"^seq\t{source}\t.*\tsummoning_special_move_{name}\t",
                             special_ledger, re.MULTILINE) is not None,
                   f"bounded-area familiar source sequence {source} is absent from its ledger")
        expect(re.search(r"^spotanim\t1364\t.*\tsummoning_special_move_giant_chinchompa_explode_gfx\t",
                         special_ledger, re.MULTILINE) is not None,
               "Giant Chinchompa's source graphic is absent from its ledger")
        for source, name in ((1511, "beaver_logs"), (2862, "beaver_achey_tree_logs"),
                             (1521, "beaver_oak_logs"), (1519, "beaver_willow_logs"),
                             (6333, "beaver_teak_logs"), (10810, "beaver_arctic_pine_logs"),
                             (1517, "beaver_maple_logs"), (6332, "beaver_mahogany_logs"),
                             (12581, "beaver_eucalyptus_logs"), (960, "beaver_yew_logs"),
                             (8778, "beaver_magic_logs")):
            expect(re.search(rf"^obj\t{source}\t.*\tsummoning_special_move_{name}\t",
                             special_ledger, re.MULTILINE) is not None,
                   f"Multichop log {source} is absent from its ledger")

        script_root = args.tree / "server/scripts/ported_scape2009_summoning/scripts"
        special_core = read_module(script_root, "summoning_special_core.rs2")
        scroll_dispatch = re.findall(
            r"^if \(\$type = (\d+)\) return\((summoning_scroll_[a-z0-9_]+)\);$",
            definition(script_root, "proc,summoning_familiar_scroll"),
            re.MULTILINE,
        )
        expect([int(kind) for kind, _scroll in scroll_dispatch] == list(range(1, 79)),
               "special-move scroll dispatch does not cover familiar types 1..78 exactly once")
        expect({scroll for _kind, scroll in scroll_dispatch} == expected_names,
               "special-move dispatch is not the exact 67-object familiar scroll closure")
        cost_dispatch = re.findall(
            r"^if \(\$type = (\d+)\) return\((\d+)\);$",
            definition(script_root, "proc,summoning_familiar_special_cost"),
            re.MULTILINE,
        )
        expect([int(kind) for kind, _cost in cost_dispatch] == list(range(1, 79)),
               "special-move costs do not cover familiar types 1..78 exactly once")
        expect(all(int(cost) > 0 for _kind, cost in cost_dispatch),
               "every familiar must have a positive special-move cost")
        expect("anim(summoning_special_move_cast, 0);" in special_core and
               "spotanim_pl(summoning_special_move_gfx, 0, 0);" in special_core,
               "successful scroll use does not play the shared special visualization")
        expect("inv_del(inv, $scroll, 1);" in special_core and
               "~summoning_familiar_special_cost($type)" in special_core,
               "successful scroll use does not consume its scroll and special points")
    except (AssertionError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"test_summoning_scroll_assets: error: {exc}", file=sys.stderr)
        return 1

    print("test_summoning_scroll_assets: 78 familiar specials, 68 guide objs, shared animation, 0 errors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
