#!/usr/bin/env python3
"""Structural acceptance test for the RS2012 QBD/TD whole-map lane.

The byte codecs have their own C round-trip suite. This test holds the port's
cross-file contract: inventory -> manifest -> import ledger -> rewritten jl2,
plus widened floor bands, map archive metadata and preservation of OSRS auxiliary
members on the three identity squares that already exist in the base tree.
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


SQUARES = (
    "22_99",
    "20_95",
    "18_101",
    "40_89",
    "39_89",
    "39_90",
    "39_91",
    "40_90",
)
BASE_EXTRA_SQUARES = {"39_90", "39_91", "40_90"}
UNDERLAY_BASE = 500
OVERLAY_BASE = 1000


def read_inventory(
    path: Path,
) -> tuple[
    dict[str, int],
    set[int],
    set[int],
    set[int],
    dict[str, list[tuple[int, int, int, int, int, int]]],
]:
    square_counts: dict[str, int] = {}
    locs: set[int] = set()
    underlays: set[int] = set()
    overlays: set[int] = set()
    placements: dict[str, list[tuple[int, int, int, int, int, int]]] = {}
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            kind, ident = row["kind"], row["id"]
            if kind == "square":
                match = re.search(r"(?:^|;)locs=(\d+)(?:;|$)", row["detail"])
                assert match, f"inventory square {ident} has no loc count"
                square_counts[ident] = int(match.group(1))
            elif kind == "loc":
                locs.add(int(ident))
            elif kind == "underlay":
                underlays.add(int(ident))
            elif kind == "overlay":
                overlays.add(int(ident))
            elif kind == "placement":
                square, ordinal_text = ident.split(":", 1)
                ordinal = int(ordinal_text)
                values = {
                    key: int(value)
                    for key, value in re.findall(r"([a-z]+)=(-?\d+)", row["detail"])
                }
                record = (
                    values["loc"],
                    values["level"],
                    values["x"],
                    values["z"],
                    values["shape"],
                    values["angle"],
                )
                target = placements.setdefault(square, [])
                assert ordinal == len(target), f"placement order gap in {square}"
                target.append(record)
    return square_counts, locs, underlays, overlays, placements


def manifest_locs(path: Path) -> set[int]:
    section = ""
    found: set[int] = set()
    for raw in path.read_text().splitlines():
        line = raw.split(";", 1)[0].strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
        elif section == "export:loc" and re.match(r"^\d+=", line):
            found.add(int(line.split("=", 1)[0]))
    return found


def ledger_locs(path: Path) -> tuple[dict[int, int], set[int]]:
    mapping: dict[int, int] = {}
    destinations: set[int] = set()
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            if row["kind"] != "loc":
                continue
            source, dest = int(row["source_id"]), int(row["dest_id"])
            assert source not in mapping, f"duplicate ledger loc {source}"
            assert dest not in destinations, f"duplicate destination loc {dest}"
            mapping[source] = dest
            destinations.add(dest)
    return mapping, destinations


def config_blocks(path: Path) -> list[str]:
    return re.findall(r"^\[([^]]+)]$", path.read_text(), re.MULTILINE)


def pack_rows(path: Path) -> dict[int, str]:
    rows: dict[int, str] = {}
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].split(";", 1)[0].strip()
        if not line or "=" not in line:
            continue
        ident, name = line.split("=", 1)
        rows[int(ident)] = name.strip()
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--inventory", type=Path)
    args = parser.parse_args()

    repo = args.repo.resolve()
    tree = repo / "OSRS-Content/osrs239-content"
    lane = tree / "ported/rs2012_qbd_td"
    inventory_path = args.inventory or repo / "build/rs2012_map_inventory.tsv"
    square_counts, source_locs, underlays, overlays, placements = read_inventory(inventory_path)
    assert set(square_counts) == set(SQUARES), "inventory square set drifted"

    manifest = manifest_locs(repo / "ports/rs2012_qbd_td.ini")
    assert source_locs <= manifest, f"manifest missing {sorted(source_locs - manifest)}"
    ledger, destination_locs = ledger_locs(tree / "port/rs2012_qbd_td.map")
    assert source_locs <= ledger.keys(), f"ledger missing {sorted(source_locs - ledger.keys())}"

    map_pack = pack_rows(lane / "pack/5_maps.pack")
    actual_loc_lines = 0
    seen_destination_locs: set[int] = set()
    for square in SQUARES:
        x, z = map(int, square.split("_"))
        assert map_pack[(x << 8) | z] == f"m{square}"

        jm2 = lane / f"maps/m{square}.jm2"
        jl2 = lane / f"maps/m{square}.jl2"
        assert "==== MAP ====" in jm2.read_text()
        loc_text = jl2.read_text()
        assert "==== LOC ====" in loc_text
        loc_lines = [line for line in loc_text.splitlines() if re.match(r"^\d+ \d+ \d+: ", line)]
        assert len(loc_lines) == square_counts[square], (
            square,
            len(loc_lines),
            square_counts[square],
        )
        assert len(placements[square]) == len(loc_lines)
        actual_loc_lines += len(loc_lines)
        for line, source_record in zip(loc_lines, placements[square], strict=True):
            left, right = line.split(":", 1)
            level, local_x, local_z = map(int, left.split())
            loc_id, shape, angle = map(int, right.split())
            assert 0 <= level <= 3 and 0 <= local_x <= 63 and 0 <= local_z <= 63
            assert 0 <= shape <= 63 and 0 <= angle <= 3
            assert loc_id in destination_locs, f"unmapped loc {loc_id} in {square}"
            source_id, src_level, src_x, src_z, src_shape, src_angle = source_record
            assert (level, local_x, local_z, shape, angle) == (
                src_level,
                src_x,
                src_z,
                src_shape,
                src_angle,
            ), f"placement geometry drifted in {square}"
            assert loc_id == ledger[source_id], f"placement id was not ledger-remapped in {square}"
            seen_destination_locs.add(loc_id)

        terrain_text = jm2.read_text()
        for value in re.findall(r"(?:^|\s)u(\d+)", terrain_text, re.MULTILINE):
            assert UNDERLAY_BASE + 1 <= int(value) <= UNDERLAY_BASE + len(underlays)
        for value in re.findall(r"(?:^|\s)o(\d+);", terrain_text, re.MULTILINE):
            assert OVERLAY_BASE + 1 <= int(value) <= OVERLAY_BASE + len(overlays)

        members = pack_rows(lane / f"maps/m{square}.filepack")
        assert members[0] == f"m{square}.jm2" and members[1] == f"m{square}.jl2"
        if square in BASE_EXTRA_SQUARES:
            assert set(members) >= {0, 1, 2, 3, 4}, f"base members lost for {square}"
            for member_id in (2, 3, 4):
                assert (tree / "maps" / members[member_id]).is_file()
        else:
            assert set(members) == {0, 1}

    assert actual_loc_lines == sum(square_counts.values()) == 27_921
    assert seen_destination_locs <= destination_locs
    assert len(config_blocks(lane / "configs/rs2012.underlay")) == len(underlays) == 11
    assert len(config_blocks(lane / "configs/rs2012.overlay")) == len(overlays) == 12
    assert len(pack_rows(lane / "pack/underlay.alloc")) == len(underlays)
    assert len(pack_rows(lane / "pack/overlay.alloc")) == len(overlays)

    print(
        "rs2012 map port: PASS "
        f"({len(SQUARES)} squares, {actual_loc_lines} placements, "
        f"{len(source_locs)} loc configs, {len(underlays)} underlays, {len(overlays)} overlays)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
