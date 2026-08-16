#!/usr/bin/env python3
"""Static contract test for the production Grotworm Lair route handlers."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = (
    ROOT
    / "OSRS-Content/osrs239-content/server/scripts/minigames/minigame_rs2012_qbd"
)
CONSTANTS = COMPONENT / "configs/rs2012_grotworm_route.constant"
ROUTES = COMPONENT / "scripts/rs2012_grotworm_route.rs2"
QBD_SESSION = COMPONENT / "scripts/rs2012_qbd_session.rs2"
MAPS = ROOT / "OSRS-Content/osrs239-content/ported/rs2012_qbd_td/maps"
BASE_PLACEMENTS = (
    ROOT / "OSRS-Content/osrs239-content/ported/rs2012_qbd_td/BASE_PLACEMENTS.tsv"
)
LEDGER = ROOT / "OSRS-Content/osrs239-content/port/rs2012_qbd_td.map"


# name: (world plane, x, z)
EXPECTED_CONSTANTS = {
    "rs2012_grotworm_surface_arrival": (0, 2988, 3235),
    "rs2012_grotworm_level1_arrival": (0, 1206, 6371),
    "rs2012_grotworm_level1_deeper_arrival": (0, 1090, 6360),
    "rs2012_grotworm_middle_upper_arrival": (0, 1340, 6488),
    "rs2012_grotworm_middle_lower_arrival": (0, 1340, 6380),
    "rs2012_grotworm_mature_arrival": (0, 1090, 6497),
    "rs2012_grotworm_shortcut_top_arrival": (0, 1178, 6355),
    "rs2012_grotworm_shortcut_bottom_arrival": (0, 1206, 6506),
}

# (trigger slot, source loc): destination constant (None means non-traversal).
EXPECTED_HANDLERS = {
    (1, 70792): "rs2012_grotworm_level1_arrival",
    (1, 70793): "rs2012_grotworm_surface_arrival",
    (1, 70794): "rs2012_grotworm_middle_upper_arrival",
    (1, 70796): "rs2012_grotworm_level1_deeper_arrival",
    (1, 70797): "rs2012_grotworm_mature_arrival",
    (1, 70798): "rs2012_grotworm_middle_lower_arrival",
    (1, 70795): None,
    (2, 70795): "rs2012_grotworm_shortcut_bottom_arrival",
    (1, 70799): "rs2012_grotworm_shortcut_top_arrival",
}

# source loc: (plane, origin x, origin z, unrotated width, length, angle)
SOURCE_LOCS = {
    70792: (0, 2988, 3236, 1, 3, 0),
    70793: (0, 1207, 6370, 4, 4, 1),
    70794: (0, 1088, 6359, 3, 2, 3),
    70795: (0, 1179, 6355, 1, 2, 0),
    70796: (0, 1341, 6487, 3, 2, 1),
    70797: (0, 1341, 6379, 3, 2, 1),
    70798: (0, 1088, 6496, 3, 2, 3),
    70799: (0, 1207, 6506, 2, 2, 0),
}

# Destination constant reached when operating each loc.
DESTINATION_FOR_LOC = {
    70792: "rs2012_grotworm_level1_arrival",
    70793: "rs2012_grotworm_surface_arrival",
    70794: "rs2012_grotworm_middle_upper_arrival",
    70796: "rs2012_grotworm_level1_deeper_arrival",
    70797: "rs2012_grotworm_mature_arrival",
    70798: "rs2012_grotworm_middle_lower_arrival",
    70795: "rs2012_grotworm_shortcut_bottom_arrival",
    70799: "rs2012_grotworm_shortcut_top_arrival",
}

TARGET_LOC_FOR_LOC = {
    70792: 70793,
    70793: 70792,
    70794: 70796,
    70796: 70794,
    70797: 70798,
    70798: 70797,
    70795: 70799,
    70799: 70795,
}


def parse_coord(raw: str) -> tuple[int, int, int]:
    plane, square_x, square_z, local_x, local_z = map(int, raw.split("_"))
    return plane, square_x * 64 + local_x, square_z * 64 + local_z


def parse_constants(text: str) -> dict[str, tuple[int, int, int]]:
    found: dict[str, tuple[int, int, int]] = {}
    pattern = re.compile(
        r"^\^(rs2012_grotworm_[a-z0-9_]+)\s*=\s*([0-3](?:_[0-9]+){4})\s*$",
        re.MULTILINE,
    )
    for name, raw in pattern.findall(text):
        found[name] = parse_coord(raw)
    return found


def parse_handlers(text: str) -> dict[tuple[int, int], str]:
    headers = list(
        re.finditer(r"^\[oploc([1-5]),rs2012_loc_(\d+)\]\s*$", text, re.MULTILINE)
    )
    found: dict[tuple[int, int], str] = {}
    for index, match in enumerate(headers):
        end = headers[index + 1].start() if index + 1 < len(headers) else len(text)
        found[(int(match.group(1)), int(match.group(2)))] = text[match.end() : end]
    return found


def rotated_footprint(width: int, length: int, angle: int) -> tuple[int, int]:
    return (length, width) if angle & 1 else (width, length)


def assert_adjacent(loc_id: int, arrival: tuple[int, int, int]) -> None:
    plane, origin_x, origin_z, width, length, angle = SOURCE_LOCS[loc_id]
    actual_plane, x, z = arrival
    assert actual_plane == plane, f"loc {loc_id}: arrival plane drifted"
    width, length = rotated_footprint(width, length, angle)
    min_x, max_x = origin_x, origin_x + width - 1
    min_z, max_z = origin_z, origin_z + length - 1
    inside = min_x <= x <= max_x and min_z <= z <= max_z
    assert not inside, f"loc {loc_id}: arrival is inside its {width}x{length} footprint"
    dx = max(min_x - x, 0, x - max_x)
    dz = max(min_z - z, 0, z - max_z)
    assert max(dx, dz) == 1, f"loc {loc_id}: arrival is not one tile from footprint"


def assert_source_placements() -> None:
    ledger: dict[int, int] = {}
    for row in LEDGER.read_text().splitlines():
        fields = row.split("\t")
        if len(fields) >= 4 and fields[0] == "loc" and fields[1].isdigit():
            ledger[int(fields[1])] = int(fields[3])
    for loc_id, (plane, x, z, _width, _length, angle) in SOURCE_LOCS.items():
        if loc_id == 70792:
            rows = [
                row.split("\t")
                for row in BASE_PLACEMENTS.read_text().splitlines()
                if row and not row.startswith("#") and not row.startswith("square\t")
            ]
            expected = ["46_50", str(plane), str(x % 64), str(z % 64), "70792", "10", str(angle)]
            assert expected in [row[:7] for row in rows], "surface entrance placement drifted"
            continue
        square = f"{x // 64}_{z // 64}"
        expected = f"{plane} {x % 64} {z % 64}: {ledger[loc_id]} 10 {angle}"
        rows = (MAPS / f"m{square}.jl2").read_text().splitlines()
        assert expected in rows, f"loc {loc_id}: placement/id/angle drifted"


def main() -> None:
    constants = parse_constants(CONSTANTS.read_text())
    assert constants == EXPECTED_CONSTANTS, "route destination constant drift"

    route_text = ROUTES.read_text()
    handlers = parse_handlers(route_text)
    assert set(handlers) == set(EXPECTED_HANDLERS), "route trigger set drift"
    for trigger, destination in EXPECTED_HANDLERS.items():
        body = handlers[trigger]
        teleports = re.findall(r"p_teleport\(\^([a-z0-9_]+)\);", body)
        if destination is None:
            assert teleports == [], f"{trigger}: Investigate must not traverse"
        else:
            assert teleports == [destination], f"{trigger}: destination drift"

    assert not re.search(
        r"^\[oploc[1-5],rs2012_loc_70812\]$", route_text, re.MULTILINE
    ), "ordinary route must not own the QBD portal"
    session_text = QBD_SESSION.read_text()
    assert session_text.count("[oploc1,rs2012_loc_70812]") == 1
    assert session_text.count("[oploc2,rs2012_loc_70812]") == 1

    assert_source_placements()
    for loc_id, destination in DESTINATION_FOR_LOC.items():
        assert_adjacent(TARGET_LOC_FOR_LOC[loc_id], constants[destination])

    print(
        "rs2012 Grotworm routes: PASS "
        "(4 bidirectional links, 9 triggers, 8 one-tile-safe arrivals)"
    )


if __name__ == "__main__":
    main()
