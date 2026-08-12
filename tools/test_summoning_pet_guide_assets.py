#!/usr/bin/env python3
"""Structural acceptance for the Summoning guide's complete pet icon set."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
DEFAULT_TREE = REPO / "OSRS-Content/osrs239-content"
DEFAULT_MANIFEST = REPO / "docs/summoning_port/pet_guide_assets_727.ini"
LANE = Path("ported/scape2009_summoning")
PREFIX = "summoning_guide_pet_"


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def manifest_exports(path: Path) -> dict[int, str]:
    exports: dict[int, str] = {}
    section = ""
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
        elif section == "export:obj":
            source, name = line.split("=", 1)
            exports[int(source)] = name
    return exports


def records(path: Path) -> dict[str, str]:
    return {
        match.group(1): match.group(2)
        for match in re.finditer(
            r"^\[([^]]+)\]\n(.*?)(?=^\[|\Z)",
            path.read_text(encoding="utf-8"),
            re.MULTILINE | re.DOTALL,
        )
    }


def number_pack(path: Path) -> dict[int, str]:
    result: dict[int, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line and not line.startswith("//"):
            key, value = line.split("=", 1)
            result[int(key)] = value
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    args = parser.parse_args()

    try:
        exports = manifest_exports(args.manifest)
        expect(len(exports) == 33, f"expected 33 pet guide objects, got {len(exports)}")
        expect(len(set(exports.values())) == 33, "pet guide object names are not unique")
        manifest_text = args.manifest.read_text(encoding="utf-8")
        expect("material_mode=average_hsl\n" in manifest_text,
               "cross-revision pet materials are not flattened to source average HSL")

        lane = args.tree / LANE
        object_records = records(lane / "configs/summoning_guide_pet.obj")
        expected_names = {PREFIX + name for name in exports.values()}
        expect(set(object_records) == expected_names,
               "packed pet objects differ from the rev-727 guide manifest")
        tzrek = object_records[PREFIX + "tzrek_jad"]
        expect(all(f"resize{axis}=16\n" in tzrek for axis in "xyz"),
               "TzRek-Jad icon model is not scaled to the target icon projection")

        guide_records = records(lane / "configs/summoning_guide.dbrow")
        for name in exports.values():
            row_name = "summoning_skill_guide_feature_pet_" + name
            row = guide_records.get(row_name, "")
            expect("columndef=0:icon,obj\n" in row,
                   f"skill-guide row {row_name} does not declare an object icon")
            expect(f"values=0:0:{PREFIX}{name}\n" in row,
                   f"skill-guide row {row_name} is not bound to {PREFIX}{name}")

        obj_alloc = number_pack(lane / "pack/obj.alloc")
        allocated = {key: value for key, value in obj_alloc.items() if value in expected_names}
        expect(set(allocated) == set(range(47468, 47501)),
               "pet guide objects do not occupy their reserved allocation range")
        client_names = {
            line.strip()
            for line in (lane / "pack/obj.client").read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("//")
        }
        expect(expected_names <= client_names, "one or more pet objects are absent from obj.client")

        model_pack = number_pack(lane / "pack/7_models.pack")
        model_ids = {
            int(re.search(r"^model=(\d+)$", body, re.MULTILINE).group(1))
            for body in object_records.values()
        }
        expect(model_ids == set(range(124063, 124096)),
               "pet inventory models do not occupy their reserved allocation range")
        for model_id in model_ids:
            packed = model_pack.get(model_id, "")
            expect(packed.startswith(f"{LANE.as_posix()}/summoning_guide_pet_model_"),
                   f"pet inventory model {model_id} is not in 7_models.pack")
            expect(any((args.tree / "models" / f"{packed}{suffix}").is_file()
                       for suffix in (".model", ".ob3")),
                   f"pet inventory model payload is missing: {packed}")

        for source_model in (45568, 57357, 44751):
            flattened = (args.tree / "models" / LANE /
                         f"summoning_guide_pet_model_{source_model}.model")
            expect(flattened.is_file() and flattened.stat().st_size > 0,
                   f"pet model {source_model} is missing its flattened-material payload")

        ledger = (args.tree / "port/summoning_guide_pets_727.map").read_text(encoding="utf-8")
        ledger_objs = {int(value) for value in re.findall(r"^obj\t(\d+)\t", ledger, re.MULTILINE)}
        ledger_models = {int(value) for value in re.findall(r"^model\t(\d+)\t", ledger, re.MULTILINE)}
        expect(ledger_objs == set(exports), "pet ledger object sources differ from the manifest")
        expect(len(ledger_models) == 33, "pet ledger does not record every imported model")
    except (AssertionError, OSError, ValueError, AttributeError) as exc:
        print(f"test_summoning_pet_guide_assets: error: {exc}", file=sys.stderr)
        return 1

    print("test_summoning_pet_guide_assets: 33 pet icons, 33 models, 0 errors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
