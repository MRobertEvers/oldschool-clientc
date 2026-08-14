#!/usr/bin/env python3
"""Port the revision-727 QBD HUD and Dragonkin coffer to OSRS239.

The source interfaces use revision-727 clientscript hooks which cannot be
executed by the OSRS239 client.  This bridge keeps their visual component
trees, remaps every cache reference, removes only those foreign hooks, and
gives the server descriptive component names for the replacement behaviour.

Binary models/sequences belong to ``import_rs2012``.  This tool owns the two
interface archives and their 32 sprite archives.  It merges its sprite rows
with the material-bake rows already present in the isolated RS2012 lane.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TREE = REPO_ROOT / "OSRS-Content" / "osrs239-content"
DEFAULT_CACHE = REPO_ROOT / "cache.rs727_preeoc"
DEFAULT_CACHEPACK = REPO_ROOT / "3rd/rscache/tools/cachepack/cachepack"
LANE = Path("ported/rs2012_qbd_td")

INTERFACES = {
    1284: "rs2012_qbd_coffer",
    1285: "rs2012_qbd_hud",
}
SPRITE_SOURCES = (
    7920,
    7921,
    7922,
    8278,
    *range(8384, 8399),
    8444,
    8445,
    8446,
    *range(10959, 10969),
)
SPRITE_BASE = 13000

COMPONENT_NAMES = {
    1284: {
        5: "frame",
        7: "contents",
        8: "bank_all",
        # These follow the visible children in the 727 archive: component 9
        # owns the "Abandon all" label and component 10 owns "Take all".
        # The original generic op strings were all "Bank-all", so deriving
        # the names from those strings silently swaps two destructive actions.
        9: "abandon_all",
        10: "take_all",
        29: "close",
    },
    1285: {
        0: "root",
        5: "green_bar",
        8: "artefact_1_dormant",
        9: "artefact_2_dormant",
        10: "artefact_3_dormant",
        11: "artefact_4_dormant",
        12: "artefact_1_active",
        13: "artefact_2_active",
        14: "artefact_3_active",
        15: "artefact_4_active",
        16: "artefact_1_restored",
        17: "artefact_2_restored",
        18: "artefact_3_restored",
        19: "artefact_4_restored",
        25: "damage_bar",
        26: "damage_left",
        27: "damage_fill",
        28: "damage_right",
        29: "status",
        33: "time_overlay",
        34: "time_fill",
    },
}


def fail(message: str) -> ValueError:
    return ValueError(f"port_rs2012_qbd_ui: {message}")


def parse_pack(path: Path) -> dict[int, str]:
    if not path.exists():
        return {}
    if not path.is_file() or path.is_symlink():
        raise fail(f"pack is not a plain file: {path}")
    result: dict[int, str] = {}
    names: set[str] = set()
    for line_number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.split("#", 1)[0].split(";", 1)[0].strip()
        if not line:
            continue
        if "=" not in line:
            raise fail(f"{path}:{line_number}: malformed pack row")
        ident_text, name = line.split("=", 1)
        try:
            ident = int(ident_text)
        except ValueError as exc:
            raise fail(f"{path}:{line_number}: non-numeric id") from exc
        name = name.strip()
        if ident in result or name in names:
            raise fail(f"{path}:{line_number}: duplicate id or name")
        result[ident] = name
        names.add(name)
    return result


def parse_ledger(path: Path) -> dict[tuple[str, int], int]:
    if not path.is_file() or path.is_symlink():
        raise fail(f"missing/non-plain import ledger: {path}")
    lines = path.read_text().splitlines()
    if not lines:
        raise fail(f"empty import ledger: {path}")
    header = lines[0].split("\t")
    required = ("kind", "source_id", "dest_id")
    if any(name not in header for name in required):
        raise fail(f"ledger lacks {required}: {path}")
    columns = {name: header.index(name) for name in required}
    result: dict[tuple[str, int], int] = {}
    for line_number, raw in enumerate(lines[1:], 2):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        fields = raw.split("\t")
        if max(columns.values()) >= len(fields):
            raise fail(f"{path}:{line_number}: incomplete ledger row")
        try:
            key = (fields[columns["kind"]], int(fields[columns["source_id"]]))
            destination = int(fields[columns["dest_id"]])
        except ValueError as exc:
            raise fail(f"{path}:{line_number}: non-numeric ledger id") from exc
        if key in result:
            # A second destination for one source is a fork authored beside the
            # import (the QBD priority comparison keeps an unported original as
            # npc 25010 off models 110660/110661). The first row is the
            # published mapping, which is what this bridge resolves against;
            # `cachepack import` reads the ledger the same way.
            continue
        result[key] = destination
    return result


def component_name(interface: int, component: int) -> str:
    return COMPONENT_NAMES.get(interface, {}).get(component, f"com_{component}")


def replace_numeric_field(text: str, field: str, mapping: dict[int, int]) -> str:
    pattern = re.compile(rf"(?m)^{re.escape(field)}=(\d+)\s*$")

    def replacement(match: re.Match[str]) -> str:
        source = int(match.group(1))
        if source not in mapping:
            raise fail(f"unmapped {field} reference {source}")
        return f"{field}={mapping[source]}"

    return pattern.sub(replacement, text)


def set_field(block: str, field: str, value: str) -> str:
    pattern = re.compile(rf"(?m)^{re.escape(field)}=.*$")
    if pattern.search(block):
        return pattern.sub(f"{field}={value}", block, count=1)
    lines = block.rstrip().splitlines()
    insert_at = 2 if len(lines) >= 2 else len(lines)
    lines.insert(insert_at, f"{field}={value}")
    return "\n".join(lines) + "\n"


def transform_interface(
    source_text: str,
    interface: int,
    sprite_map: dict[int, int],
    model_map: dict[int, int],
    sequence_map: dict[int, int],
) -> str:
    # Foreign hooks name revision-727 clientscripts and packed components.  The
    # replacement server scripts install inventory transmit and button events.
    text = re.sub(r"(?m)^on[a-z0-9_]*=.*\n?", "", source_text)
    text = replace_numeric_field(text, "graphic", sprite_map)
    text = replace_numeric_field(text, "model", model_map)
    text = replace_numeric_field(text, "modelanim", sequence_map)

    block_pattern = re.compile(r"(?ms)^\[com_(\d+)]\n.*?(?=^\[|\Z)")
    blocks: list[str] = []
    cursor = 0
    seen: set[int] = set()
    for match in block_pattern.finditer(text):
        blocks.append(text[cursor:match.start()])
        component = int(match.group(1))
        seen.add(component)
        block = match.group(0)
        block = block.replace(
            f"[com_{component}]", f"[{component_name(interface, component)}]", 1
        )
        if interface == 1284 and component in (8, 9, 10):
            label = {8: "Bank-all", 9: "Abandon-all", 10: "Take-all"}[component]
            block = set_field(block, "op1", label)
        if interface == 1285 and component == 33:
            block = set_field(block, "hidden", "yes")
        blocks.append(block)
        cursor = match.end()
    blocks.append(text[cursor:])
    transformed = "".join(blocks)

    expected_count = 47 if interface == 1284 else 35
    if seen != set(range(expected_count)):
        raise fail(
            f"interface {interface} component set differs: "
            f"missing={sorted(set(range(expected_count)) - seen)} "
            f"extra={sorted(seen - set(range(expected_count)))}"
        )
    if re.search(r"(?m)^on[a-z0-9_]*=", transformed):
        raise fail(f"interface {interface} retained a foreign hook")
    return transformed.rstrip() + "\n"


def write_pack(path: Path, rows: dict[int, str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(f"{ident}={rows[ident]}\n" for ident in sorted(rows)))


def locate_sources(root: Path) -> tuple[Path, Path]:
    interfaces = root / "interfaces"
    sprites = root / "sprites"
    if not interfaces.is_dir() or not sprites.is_dir():
        raise fail(f"decoded source lacks interfaces/sprites: {root}")
    return interfaces, sprites


def extract(cachepack: Path, cache: Path, destination: Path) -> tuple[Path, Path]:
    if not cachepack.is_file() or not cache.is_dir():
        raise fail(f"missing cachepack ({cachepack}) or source cache ({cache})")
    command = [
        str(cachepack),
        "unpack",
        "--cache",
        str(cache),
        "--rev",
        "rs727",
        "--src",
        str(destination),
        "--types",
        "inv",
        "--assets=interfaces,sprites",
        "--warn",
        "0",
    ]
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode != 0:
        raise fail(f"cachepack extraction failed:\n{result.stderr[-4000:]}")
    return locate_sources(destination)


def port(
    tree: Path,
    interface_source: Path,
    sprite_source: Path,
    apply: bool,
) -> None:
    tree = tree.resolve()
    lane = tree / LANE
    ledger = parse_ledger(tree / "port/rs2012_qbd_td.map")
    sprite_map = {
        source: SPRITE_BASE + index for index, source in enumerate(SPRITE_SOURCES)
    }
    model_map = {70127: ledger.get(("model", 70127), -1)}
    sequence_map = {9390: ledger.get(("seq", 9390), -1)}
    if -1 in model_map.values() or -1 in sequence_map.values():
        raise fail("model 70127 or sequence 9390 is absent from the import ledger")

    transformed: dict[int, str] = {}
    for interface in INTERFACES:
        source = interface_source / f"interface_{interface}.if"
        compack = interface_source / f"interface_{interface}.compack"
        if not source.is_file() or not compack.is_file():
            raise fail(f"source interface {interface} is incomplete")
        transformed[interface] = transform_interface(
            source.read_text(), interface, sprite_map, model_map, sequence_map
        )
        source_components = parse_pack(compack)
        expected_count = 47 if interface == 1284 else 35
        if set(source_components) != set(range(expected_count)):
            raise fail(f"source interface {interface} compack is incomplete")

    for source in SPRITE_SOURCES:
        directory = sprite_source / f"sprite_{source}"
        if not directory.is_dir() or not (directory / "pack.meta").is_file():
            raise fail(f"source sprite {source} is incomplete")
        if not list(directory.glob("*.bmp")):
            raise fail(f"source sprite {source} has no decoded frames")

    lane_interface_pack = lane / "pack/3_interfaces.pack"
    interface_rows = parse_pack(lane_interface_pack)
    for ident, name in INTERFACES.items():
        if ident in interface_rows and interface_rows[ident] != name:
            raise fail(f"interface id {ident} collides with {interface_rows[ident]}")
        if name in interface_rows.values() and interface_rows.get(ident) != name:
            raise fail(f"interface name {name} already has another id")
        interface_rows[ident] = name

    lane_sprite_pack = lane / "pack/8_sprites.pack"
    sprite_rows = parse_pack(lane_sprite_pack)
    for source, destination in sprite_map.items():
        name = f"ported/rs2012_qbd_td/rs2012_qbd_sprite_{source}"
        if destination in sprite_rows and sprite_rows[destination] != name:
            raise fail(f"sprite id {destination} collides with {sprite_rows[destination]}")
        if name in sprite_rows.values() and sprite_rows.get(destination) != name:
            raise fail(f"sprite name {name} already has another id")
        sprite_rows[destination] = name

    if not apply:
        print(
            "port_rs2012_qbd_ui: validated 2 interfaces, "
            f"{len(SPRITE_SOURCES)} sprites, model {model_map[70127]}, "
            f"sequence {sequence_map[9390]} (dry run)"
        )
        return

    interface_out = lane / "interfaces"
    interface_out.mkdir(parents=True, exist_ok=True)
    for interface, name in INTERFACES.items():
        (interface_out / f"{name}.if").write_text(transformed[interface])
        compack_rows = {
            component: component_name(interface, component)
            for component in range(47 if interface == 1284 else 35)
        }
        write_pack(interface_out / f"{name}.compack", compack_rows)

    sprite_out = tree / "sprites" / LANE
    sprite_out.mkdir(parents=True, exist_ok=True)
    for source in SPRITE_SOURCES:
        destination = sprite_out / f"rs2012_qbd_sprite_{source}"
        destination.mkdir(parents=True, exist_ok=True)
        for source_file in (sprite_source / f"sprite_{source}").iterdir():
            if source_file.is_symlink() or not source_file.is_file():
                raise fail(f"source sprite contains a non-plain file: {source_file}")
            shutil.copy2(source_file, destination / source_file.name)

    write_pack(lane_interface_pack, interface_rows)
    write_pack(lane_sprite_pack, sprite_rows)
    print(
        "port_rs2012_qbd_ui: wrote authentic QBD interfaces 1284/1285 and "
        f"sprites {SPRITE_BASE}..{SPRITE_BASE + len(SPRITE_SOURCES) - 1}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--cachepack", type=Path, default=DEFAULT_CACHEPACK)
    parser.add_argument(
        "--decoded-interfaces",
        type=Path,
        help="existing cachepack extraction's interfaces directory",
    )
    parser.add_argument(
        "--decoded-sprites",
        type=Path,
        help="existing cachepack extraction's sprites directory",
    )
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    try:
        if bool(args.decoded_interfaces) != bool(args.decoded_sprites):
            raise fail("provide both decoded source directories or neither")
        if args.decoded_interfaces:
            port(args.tree, args.decoded_interfaces, args.decoded_sprites, args.apply)
        else:
            with tempfile.TemporaryDirectory(prefix="rs2012-qbd-ui-") as temporary:
                interfaces, sprites = extract(
                    args.cachepack, args.cache, Path(temporary)
                )
                port(args.tree, interfaces, sprites, args.apply)
        return 0
    except (OSError, ValueError) as exc:
        print(exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
