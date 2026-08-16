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
    "16_99",
    "17_99",
    "18_99",
    "20_99",
    "21_99",
    "20_100",
    "21_100",
    "20_101",
    "21_101",
    "16_101",
    "17_100",
    "17_101",
    "18_101",
    "40_89",
    "39_89",
    "39_90",
    "39_91",
    "40_90",
)
BASE_EXTRA_SQUARES = {"39_90", "39_91", "40_90"}
UNDERLAY_BASE = 242
OVERLAY_BASE = 1000
SOURCE_ARCHIVES = {
    "22_99": (7174, 7175, (470121283, 1782907695, -131030477, -446458785)),
    "20_95": (7157, 7158, (558713239, 1417714157, -302553027, -158448671)),
    "16_99": (7117, 7118, (477331269, -1261056419, -1221718129, 1502185528)),
    "17_99": (7168, 7169, (-1909749566, 258386662, 8275989, 315008195)),
    "18_99": (7137, 7138, (-435342694, -1956791799, -570358413, -1529989129)),
    "20_99": (7139, 7140, (1817994005, -1939081852, 1483364481, 986296098)),
    "21_99": (7125, 7126, (1431497013, 549444996, -1138684586, -2048667815)),
    "20_100": (7141, 7142, (241390015, -1595154578, 639556659, -1137712732)),
    "21_100": (7121, 7122, (-1223211157, 362322168, 2034488860, 165451212)),
    "20_101": (7127, 7128, (-517702445, -1565676718, -1788593958, -1812459785)),
    "21_101": (7119, 7120, (737530024, 1576892563, 564068280, -1179796355)),
    "16_101": (7143, 7144, (-1426155942, -838747053, 96059630, -1889001428)),
    "17_100": (7123, 7124, (1905932215, -1526576869, -1403936829, 1608882513)),
    "17_101": (7149, 7150, (-1632718105, 2121579019, 1093321834, 1255552106)),
    "18_101": (7155, 7156, (928791872, 1258826681, -1528400880, 1334217208)),
    "40_89": (4159, 4160, (769370878, -1231940040, -395526277, -1563597554)),
    "39_89": (2648, 2649, (1714440207, 1297605064, 683265918, 1501779755)),
    "39_90": (2318, 2319, (274761362, 1679929293, -903581645, -999712194)),
    "39_91": (4141, 4142, (1551484478, 1577386038, -378705686, 547571118)),
    "40_90": (1167, 1168, (-907752098, 1554817455, -41175839, -2043434626)),
}
ROUTE_LINKS = {
    "18_99": {
        (70793, 0, 55, 34, 10, 1),  # surface exit
        (70795, 0, 27, 19, 10, 0),  # direct shortcut to level 3
    },
    "17_99": {(70794, 0, 0, 23, 10, 3)},
    "20_99": {(70797, 0, 61, 43, 10, 1)},
    "20_101": {(70796, 0, 61, 23, 10, 1)},
    "17_101": {(70798, 0, 0, 32, 10, 3)},
    "18_101": {
        (70799, 0, 55, 42, 10, 0),
        (70812, 0, 48, 34, 10, 0),
    },
}

# The imported RS727 arena has both static foreclaws on plane 0.  In the
# destination scene the hand-rock/platform geometry remains on plane 0, while
# the two animated QBD hands need to share the presentation plane with the
# Queen and player.  Keyed by the source placement identity so an import rerun
# cannot quietly undo this intentional destination composition adjustment.
PLACEMENT_LEVEL_OVERRIDES = {
    ("22_99", 70818, 39, 35, 10, 0): 1,  # right foreclaw
    ("22_99", 70822, 21, 35, 10, 0): 1,  # left foreclaw
}

# The two arena floor slabs draw half a tile west of the footprint they are
# anchored on: loc 70836's faces span x[-814.5..704] about its origin against a
# 12-tile (+/-768) footprint, and 70839's span x[-832..548].  A loc footprint
# always starts at its map anchor, so no authored `width` can cover that lip,
# and the painter -- which gives a loc one slot, at the ring of the footprint it
# is registered on -- then emits the seam column's ground on top of the slab: a
# one-tile strip of arena floor lying over the platform from any camera west of
# the seam (LARGE_LOCS_PAINTER.md).
#
# The anchor moves one tile west and the loc's `offsetx`/`offsetz` move the
# model back by the same amount, so the drawn geometry does not shift a single
# unit -- only the footprint the painter orders it by.  Both slabs are
# `blockwalk=0 blockrange=0` and both collision writers gate on
# `blocks_walk != 0` (world_collision.u.c, mock230_scene.c), so neither the walk
# map nor the collision map sees any of it.
#
# Keyed by the source placement identity, like the level overrides, so an import
# rerun cannot quietly undo it -- and so the next person reads why before they
# "restore" it to the source coordinate.
PLACEMENT_ANCHOR_OVERRIDES = {
    # source (x, z, width, length) -> new anchor (x, z); source size is what the
    # offset has to compensate against, so the invariant below is checkable.
    ("22_99", 70836, 22, 24, 10, 0): (21, 24, 12, 18),  # west 12x18 slab
    ("22_99", 70839, 34, 24, 10, 0): (33, 24, 12, 18),  # east 12x18 slab
}

# The other four states of those same two slabs. 70836/70839 are the intact
# floor the map places; 70837/70840 light the seams and 70838/70841 open the
# stairwell hole, and the encounter script `loc_add`s them onto the map slabs'
# own (tile, level, shape) so each one REPLACES the floor rather than stacking a
# second one on the arena. That makes them subject to the same origin invariant
# as the map placements -- and to a second one a map diff cannot see at all,
# because their anchor lives in a script constant rather than in the .jl2.
#
# Both drifted apart once already: the anchor override above moved the map a
# tile west in Aug 2026 and the script kept adding at the source coordinate on
# the wrong plane, so the completed arena wore two floors. Nothing downstream
# reported it.
#
# source_id -> constant names of the anchor the script places it on.
SCRIPT_PLACED_FLOOR_STATES = {
    70837: ("rs2012_qbd_floor_west_lx", "rs2012_qbd_floor_west_lz"),
    70838: ("rs2012_qbd_floor_west_lx", "rs2012_qbd_floor_west_lz"),
    70840: ("rs2012_qbd_floor_east_lx", "rs2012_qbd_floor_east_lz"),
    70841: ("rs2012_qbd_floor_east_lx", "rs2012_qbd_floor_east_lz"),
}
# Every one of the six is 12x18 at rev 727; §16 of LARGE_LOCS_PAINTER.md has the
# per-model overhang that decides which of them also needs the length.
FLOOR_STATE_SOURCE_SIZE = (12, 18)
QBD_SCRIPTS = Path("server/scripts/minigames/minigame_rs2012_qbd")


def read_inventory(
    path: Path,
) -> tuple[
    dict[str, int],
    dict[str, tuple[int, int, tuple[int, int, int, int]]],
    set[int],
    set[int],
    set[int],
    dict[str, list[tuple[int, int, int, int, int, int]]],
]:
    square_counts: dict[str, int] = {}
    square_metadata: dict[str, tuple[int, int, tuple[int, int, int, int]]] = {}
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
                terrain = re.search(r"(?:^|;)terrain-archive=(\d+)(?:;|$)", row["detail"])
                loc = re.search(r"(?:^|;)loc-archive=(\d+)(?:;|$)", row["detail"])
                xtea = re.search(r"(?:^|;)xtea=([-\d]+),([-\d]+),([-\d]+),([-\d]+)$", row["detail"])
                assert terrain and loc and xtea, f"inventory square {ident} has no source metadata"
                square_metadata[ident] = (
                    int(terrain.group(1)),
                    int(loc.group(1)),
                    tuple(int(value) for value in xtea.groups()),
                )
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
    return square_counts, square_metadata, locs, underlays, overlays, placements


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


def config_fields(path: Path) -> dict[str, dict[str, str]]:
    """Every `[name]` block's key=value pairs. Last value wins, as the packer reads it."""
    blocks: dict[str, dict[str, str]] = {}
    current: dict[str, str] | None = None
    for raw in path.read_text().splitlines():
        line = raw.split("//", 1)[0].strip()
        if not line:
            continue
        header = re.match(r"^\[([^]]+)]$", line)
        if header:
            current = blocks.setdefault(header.group(1), {})
            continue
        if current is not None and "=" in line:
            key, value = line.split("=", 1)
            current[key.strip()] = value.strip()
    return blocks


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
    square_counts, square_metadata, source_locs, underlays, overlays, placements = read_inventory(inventory_path)
    assert set(square_counts) == set(SQUARES), "inventory square set drifted"
    assert square_metadata == SOURCE_ARCHIVES, "source archive/XTEA contract drifted"
    for square, links in ROUTE_LINKS.items():
        assert links <= set(placements[square]), f"Grotworm route link drifted in {square}"

    manifest = manifest_locs(repo / "ports/rs2012_qbd_td.ini")
    assert source_locs <= manifest, f"manifest missing {sorted(source_locs - manifest)}"
    ledger, destination_locs = ledger_locs(tree / "port/rs2012_qbd_td.map")
    assert source_locs <= ledger.keys(), f"ledger missing {sorted(source_locs - ledger.keys())}"

    map_pack = pack_rows(lane / "pack/5_maps.pack")
    actual_loc_lines = 0
    seen_destination_locs: set[int] = set()
    seen_level_overrides: set[tuple[str, int, int, int, int, int]] = set()
    seen_anchor_overrides: set[tuple[str, int, int, int, int, int]] = set()
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
            override_key = (square, source_id, src_x, src_z, src_shape, src_angle)
            expected_level = PLACEMENT_LEVEL_OVERRIDES.get(override_key, src_level)
            if override_key in PLACEMENT_LEVEL_OVERRIDES:
                assert src_level == 0, f"unexpected source level for {override_key}"
                seen_level_overrides.add(override_key)
            expected_x, expected_z = src_x, src_z
            if override_key in PLACEMENT_ANCHOR_OVERRIDES:
                expected_x, expected_z = PLACEMENT_ANCHOR_OVERRIDES[override_key][:2]
                seen_anchor_overrides.add(override_key)
            assert (level, local_x, local_z, shape, angle) == (
                expected_level,
                expected_x,
                expected_z,
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

    assert actual_loc_lines == sum(square_counts.values()) == 55_187
    assert seen_destination_locs <= destination_locs
    assert seen_level_overrides == set(PLACEMENT_LEVEL_OVERRIDES), "QBD hand level override missing"
    assert seen_anchor_overrides == set(PLACEMENT_ANCHOR_OVERRIDES), "slab anchor override missing"

    # The anchor override is only half of the correction: the loc's offset has
    # to move the model back by exactly what the anchor moved, or the arena's
    # art shifts and the interlocking floor pieces tear open at their seams.
    # Placement centres a model at anchor*128 + 64*size, so the invariant is
    # that the model ORIGIN is byte-identical to what the source placement gave
    # it. Asserted here because nothing downstream would notice a half-tile
    # slide -- it looks like content, not like a bug.
    loc_cfg = config_fields(lane / "configs/rs2012.loc")
    for (square, source_id, src_x, src_z, _shape, _angle), (
        new_x,
        new_z,
        src_width,
        src_length,
    ) in PLACEMENT_ANCHOR_OVERRIDES.items():
        block = loc_cfg[f"rs2012_loc_{source_id}"]
        width = int(block.get("width", 1))
        length = int(block.get("length", 1))
        offset_x = int(block.get("offsetx", 0))
        offset_z = int(block.get("offsetz", 0))
        assert new_x * 128 + 64 * width + offset_x == src_x * 128 + 64 * src_width, (
            f"{square} loc {source_id}: offsetx does not cancel the anchor move "
            f"(model origin would shift {new_x * 128 + 64 * width + offset_x - (src_x * 128 + 64 * src_width)} units)"
        )
        assert new_z * 128 + 64 * length + offset_z == src_z * 128 + 64 * src_length, (
            f"{square} loc {source_id}: offsetz does not cancel the anchor move "
            f"(model origin would shift {new_z * 128 + 64 * length + offset_z - (src_z * 128 + 64 * src_length)} units)"
        )
    # Same invariant for the four floor states the encounter script places, plus
    # the one the .jl2 cannot express: the script must add them on the map
    # slabs' anchor and plane, or the wire gives them their own slot and the
    # arena keeps both floors.
    constants = {
        match.group(1): int(match.group(2))
        for match in re.finditer(
            r"^\^(\w+)\s*=\s*(-?\d+)$",
            (tree / QBD_SCRIPTS / "configs/rs2012_qbd.constant").read_text(),
            re.MULTILINE,
        )
    }
    session = (tree / QBD_SCRIPTS / "scripts/rs2012_qbd_session.rs2").read_text()
    src_width, src_length = FLOOR_STATE_SOURCE_SIZE
    for (square, source_id, src_x, src_z, _shape, _angle), (
        map_x,
        map_z,
        _w,
        _l,
    ) in PLACEMENT_ANCHOR_OVERRIDES.items():
        assert (src_width, src_length) == (_w, _l), f"floor slab {source_id} is not 12x18 at source"
    for source_id, (lx_name, lz_name) in SCRIPT_PLACED_FLOOR_STATES.items():
        anchor_x, anchor_z = constants[lx_name], constants[lz_name]
        assert (anchor_x, anchor_z) in {
            (x, z) for x, z, _w, _l in PLACEMENT_ANCHOR_OVERRIDES.values()
        }, f"loc {source_id} is placed on ({anchor_x},{anchor_z}), which no floor slab is anchored on"
        block = loc_cfg[f"rs2012_loc_{source_id}"]
        width = int(block.get("width", 1))
        length = int(block.get("length", 1))
        offset_x = int(block.get("offsetx", 0))
        offset_z = int(block.get("offsetz", 0))
        # The state and the slab it replaces are the same picture, so they must
        # land on the same origin: the source placement's, one tile east.
        assert anchor_x * 128 + 64 * width + offset_x == (anchor_x + 1) * 128 + 64 * src_width, (
            f"loc {source_id}: footprint/offset move the art off the slab it replaces"
        )
        assert anchor_z * 128 + 64 * length + offset_z == anchor_z * 128 + 64 * src_length, (
            f"loc {source_id}: offsetz does not cancel the length bump"
        )
        # A state added on the arena level lands beside the slab, not on it.
        assert (
            f"~rs2012_qbd_floor_coord(^{lx_name}, ^{lz_name}), rs2012_loc_{source_id}" in session
            or f"rs2012_loc_{source_id}" not in session
        ), f"loc {source_id} is placed without ~rs2012_qbd_floor_coord"
    assert constants["rs2012_qbd_floor_level"] != constants["rs2012_qbd_arena_level"], (
        "the floor slabs are map-placed on plane 0; the arena level is the actors' plane"
    )

    assert len(config_blocks(lane / "configs/rs2012.underlay")) == len(underlays) == 12
    assert len(config_blocks(lane / "configs/rs2012.overlay")) == len(overlays) == 12
    assert len(pack_rows(lane / "pack/underlay.alloc")) == len(underlays)
    assert len(pack_rows(lane / "pack/overlay.alloc")) == len(overlays)
    assert pack_rows(lane / "pack/underlay.alloc") == {
        **{UNDERLAY_BASE + index: f"rs2012_underlay_{source}" for index, source in enumerate(
            (52, 54, 65, 81, 89, 93, 98, 102, 107, 162, 163)
        )},
        UNDERLAY_BASE + 11: "rs2012_underlay_6",
    }, "existing floor allocations shifted instead of appending"

    base_rows = (lane / "BASE_PLACEMENTS.tsv").read_text().splitlines()
    assert base_rows[1].split("\t")[:7] == ["46_50", "0", "44", "36", "70792", "10", "0"]
    assert 70792 in manifest and ledger[70792] in destination_locs

    print(
        "rs2012 map port: PASS "
        f"({len(SQUARES)} source squares + 1 base overlay, {actual_loc_lines} placements, "
        f"{len(source_locs)} loc configs, {len(underlays)} underlays, {len(overlays)} overlays)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
