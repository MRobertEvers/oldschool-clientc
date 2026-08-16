#!/usr/bin/env python3
"""Static regression for the RS2012 Tormented Demon instance lifecycle.

The live host selftest drives the timer and allocator.  This smaller check pins
the content seam around it: ownership must not be inferred from the player's
current coordinate, every terminal path must delete the six indefinite NPCs,
and the imported cave opening must remain the ordinary in-game exit.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AREA = (
    ROOT
    / "OSRS-Content/osrs239-content/server/scripts/areas/area_rs2012_tormented_demons"
)
SCRIPT = AREA / "scripts/rs2012_td_encounter.rs2"
CONSTANTS = AREA / "configs/rs2012_tormented_demons.constant"
VARPS = AREA / "configs/rs2012_tormented_demons.varp"
LOGOUT = ROOT / "OSRS-Content/osrs239-content/server/scripts/player/logout.rs2"
DEATH = ROOT / "OSRS-Content/osrs239-content/server/scripts/player/death.rs2"
SOURCE_MAP = (
    ROOT / "OSRS-Content/osrs239-content/ported/rs2012_qbd_td/maps/m39_91.jl2"
)
SOURCE_LOCS = (
    ROOT / "OSRS-Content/osrs239-content/ported/rs2012_qbd_td/configs/rs2012.loc"
)


def block(text: str, header: str) -> str:
    match = re.search(
        rf"^\[{re.escape(header)}\][^\n]*\n(.*?)(?=^\[[^\]]+\][^\n]*$|\Z)",
        text,
        re.MULTILINE | re.DOTALL,
    )
    assert match, f"missing [{header}]"
    return match.group(1)


def integer_constants(text: str) -> dict[str, int]:
    return {
        name: int(value)
        for name, value in re.findall(
            r"^\^([a-zA-Z0-9_]+)\s*=\s*(-?\d+)\s*$", text, re.MULTILINE
        )
    }


def packed_coord(text: str, name: str) -> tuple[int, int, int]:
    match = re.search(
        rf"^\^{re.escape(name)}\s*=\s*([0-3])_(\d+)_(\d+)_(\d+)_(\d+)\s*$",
        text,
        re.MULTILINE,
    )
    assert match, f"missing packed coord {name}"
    plane, square_x, square_z, local_x, local_z = map(int, match.groups())
    return plane, square_x * 64 + local_x, square_z * 64 + local_z


def main() -> None:
    script = SCRIPT.read_text(encoding="utf-8")
    constants_text = CONSTANTS.read_text(encoding="utf-8")
    constants = integer_constants(constants_text)
    varps = VARPS.read_text(encoding="utf-8")

    for name in ("rs2012_td_active", "rs2012_td_handle"):
        body = block(varps, name)
        assert "scope=temp" in body and "transmit=no" in body

    coord = block(script, "proc,rs2012_td_coord")
    assert "%rs2012_td_handle" in coord
    assert "%map_instance_handle" not in coord

    enter = block(script, "proc,rs2012_td_enter_checked")
    required_enter = (
        "%map_instance_handle = $handle;",
        "%rs2012_td_handle = $handle;",
        "%rs2012_td_active = 1;",
        "softtimer(rs2012_td_lifecycle, ^rs2012_td_lifecycle_ticks);",
        "softtimer(rs2012_td_install_exit, ^rs2012_td_lifecycle_ticks);",
    )
    for fragment in required_enter:
        assert fragment in enter, f"entry lost lifecycle step: {fragment}"

    release = block(script, "proc,rs2012_td_release_handle")
    ordered = (
        "clearsofttimer(rs2012_td_lifecycle);",
        "clearsofttimer(rs2012_td_install_exit);",
        "clearqueue(rs2012_td_damage_player);",
        "~rs2012_td_despawn_handle($handle);",
        "map_instance_free($handle);",
        "%rs2012_td_active = 0;",
        "%rs2012_td_handle = ^map_instance_none;",
    )
    cursor = -1
    for fragment in ordered:
        at = release.find(fragment)
        assert at > cursor, f"release order lost at {fragment}"
        cursor = at

    lifecycle = block(script, "proc,rs2012_td_lifecycle_tick")
    assert "map_instance_find(coord) = $handle" in lifecycle
    assert "~rs2012_td_release_handle($handle);" in lifecycle
    assert "p_teleport" not in lifecycle, "external teleports must keep their destination"
    assert block(script, "softtimer,rs2012_td_lifecycle").lstrip().startswith(
        "~rs2012_td_lifecycle_tick;\n"
    )
    install_exit = block(script, "softtimer,rs2012_td_install_exit")
    assert install_exit.index("clearsofttimer(rs2012_td_install_exit);") < (
        install_exit.index("loc_add(")
    )
    assert "%rs2012_td_active = 0" in install_exit
    assert "map_instance_find(coord) ! $handle" in install_exit
    assert (
        "loc_add(~rs2012_td_coord(^rs2012_td_exit_lx, ^rs2012_td_exit_lz), "
        "rs2012_loc_40260, ^loc_east, centrepiece_straight, ^max_32bit_int);"
        in install_exit
    )

    leave = block(script, "proc,rs2012_td_leave")
    assert leave.index("p_teleport(^rs2012_td_outside_safe);") < leave.index(
        "~rs2012_td_release_handle($handle);"
    )
    opening = block(script, "oploc1,rs2012_loc_40260")
    assert "%rs2012_td_active = 1" in opening
    assert "~rs2012_td_leave;" in opening
    assert "~rs2012_td_enter;" in opening

    logout = block(script, "proc,rs2012_td_on_logout")
    assert "%rs2012_td_active = 0" in logout
    assert "%rs2012_td_handle" in logout
    assert "~rs2012_td_present" not in logout
    assert "~rs2012_td_release_handle($handle);" in logout
    assert "~rs2012_td_on_logout;" in LOGOUT.read_text(encoding="utf-8")

    death = block(script, "proc,rs2012_td_on_death")
    finish = block(script, "proc,rs2012_td_finish_death")
    assert "%rs2012_td_active = 0" in death
    assert "%rs2012_td_handle" in death
    assert "clearsofttimer(rs2012_td_install_exit);" in death
    assert "map_instance_free" not in death
    assert "map_instance_free($handle);" in finish
    death_dispatch = DEATH.read_text(encoding="utf-8")
    assert "~rs2012_td_on_death;" in death_dispatch
    assert "~rs2012_td_finish_death($rs2012_td_death_handle);" in death_dispatch
    assert (
        "if ($rs2012_qbd_death_handle ! ^map_instance_none)" in death_dispatch
        and "else if ($rs2012_td_death_handle ! ^map_instance_none)" in death_dispatch
        and "$death_coord = ^rs2012_td_outside_safe;" in death_dispatch
    ), "TD gravestones/bones must be redirected before the instance is released"
    assert death_dispatch.index("$death_coord = ^rs2012_td_outside_safe;") < (
        death_dispatch.index("~rs2012_td_finish_death($rs2012_td_death_handle);")
    )

    assert constants["rs2012_td_lifecycle_ticks"] == 1
    assert (
        constants["rs2012_td_exit_lx"],
        constants["rs2012_td_exit_lz"],
        constants["rs2012_td_entry_lx"],
        constants["rs2012_td_entry_lz"],
    ) == (23, 29, 22, 30)

    # Source 48248 really is the p2 (2526,5828), angle-2 cave-mouth morph and
    # its post-puzzle child really is 40260.  The chosen return tile is one
    # square south of that 3x2 footprint, not inside the blocking loc.
    assert "2 30 4: 63203 10 2" in SOURCE_MAP.read_text(encoding="utf-8")
    source_locs = SOURCE_LOCS.read_text(encoding="utf-8")
    morph = block(source_locs, "rs2012_loc_48248")
    cave = block(source_locs, "rs2012_loc_40260")
    assert "multiloc1=rs2012_loc_40260" in morph
    assert "name=Cave opening" in cave and "op1=Climb-through" in cave
    assert packed_coord(constants_text, "rs2012_td_outside_safe") == (2, 2527, 5827)

    print(
        "rs2012 TD lifecycle: PASS "
        "(owned handle, cave exit, departure watchdog, logout/death teardown)"
    )


if __name__ == "__main__":
    main()
