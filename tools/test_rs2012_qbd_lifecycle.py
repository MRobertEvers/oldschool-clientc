#!/usr/bin/env python3
"""Static contract for QBD-owned instance and manifest-QA lifecycle seams."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
QBD = (
    ROOT
    / "OSRS-Content/osrs239-content/server/scripts/minigames/minigame_rs2012_qbd"
)
SESSION = QBD / "scripts/rs2012_qbd_session.rs2"
ADDS = QBD / "scripts/rs2012_qbd_adds.rs2"
COMBAT = QBD / "scripts/rs2012_qbd_combat.rs2"
VARPS = QBD / "configs/rs2012_qbd.varp"
CONSTANTS = QBD / "configs/rs2012_qbd.constant"
MANIFEST = ROOT / "manifest_osrs239_rs2012.ini"
LOGOUT = ROOT / "OSRS-Content/osrs239-content/server/scripts/player/logout.rs2"
DEATH = ROOT / "OSRS-Content/osrs239-content/server/scripts/player/death.rs2"
WORLD = ROOT / "src/net/mock/mock230_world.c"


def block(text: str, header: str) -> str:
    match = re.search(
        rf"^\[{re.escape(header)}\][^\n]*\n(.*?)(?=^\[[^\]]+\][^\n]*$|\Z)",
        text,
        re.MULTILINE | re.DOTALL,
    )
    assert match, f"missing [{header}]"
    return match.group(1)


def main() -> None:
    session = SESSION.read_text(encoding="utf-8")
    varps = VARPS.read_text(encoding="utf-8")
    constants = CONSTANTS.read_text(encoding="utf-8")

    handle_varp = block(varps, "rs2012_qbd_handle")
    assert "scope=temp" in handle_varp and "transmit=no" in handle_varp
    assert "^rs2012_qbd_lifecycle_ticks = 1" in constants

    # Every QBD-relative lookup must retain the old owned handle even if a
    # destination activity has already replaced the shared handle.
    for proc in (
        "proc,rs2012_qbd_coord",
        "proc,rs2012_qbd_reward_coord",
        "proc,rs2012_qbd_lower_coord",
    ):
        body = block(session, proc)
        assert "%rs2012_qbd_handle" in body
        assert "%map_instance_handle" not in body
    for text in (
        session,
        ADDS.read_text(encoding="utf-8"),
        COMBAT.read_text(encoding="utf-8"),
    ):
        assert "map_instance_find(coord) ! %map_instance_handle" not in text

    enter = block(session, "proc,rs2012_qbd_enter_checked")
    assert "$summoning_bypass = false & stat_base(summoning) < 60" in enter
    assert enter.count("%rs2012_qbd_handle = ") == 2
    assert enter.count(
        "softtimer(rs2012_qbd_lifecycle, ^rs2012_qbd_lifecycle_ticks);"
    ) == 2
    assert "stat_add" not in enter and "stat_boost" not in enter
    assert "%rs2012_song_from_depths_complete =" not in enter

    production = block(session, "proc,rs2012_qbd_enter")
    assert "~rs2012_qbd_enter_checked($ruleset, false);" in production
    assert "~rs2012_qbd_enter(^rs2012_qbd_release_profile);" in block(
        session, "oploc2,rs2012_loc_70812"
    )
    assert "~rs2012_qbd_enter(^rs2012_qbd_release_profile);" in block(
        session, "debugproc,rs2012qbd"
    )
    manifest_entry = block(session, "debugproc,rs2012qbdmanifest")
    assert (
        "~rs2012_qbd_enter_checked(^rs2012_qbd_release_profile, true);"
        in manifest_entry
    )
    assert "cheat=rs2012qbdmanifest" in MANIFEST.read_text(encoding="utf-8")

    clear = block(session, "proc,rs2012_qbd_clear_state")
    for fragment in (
        "midi_song(-1);",
        "~rs2012_qbd_hud_close;",
        "inv_stoptransmit(rs2012_qbd_coffer:contents);",
        "if_closesub(toplevel_osrs_stretch:mainmodal);",
        "clearsofttimer(rs2012_qbd_lifecycle);",
        "~rs2012_qbd_stop_platform_mechanics;",
    ):
        assert fragment in clear, f"QBD cleanup lost {fragment}"
    for queue in (
        "wake",
        "begin",
        "wall_begin",
        "wall_step",
        "shadow_step",
        "time_channel",
        "time_end",
        "worm_spawn",
        "coffer_arrive",
        "damage_player",
        "extreme_pulse",
    ):
        assert f"clearqueue(rs2012_qbd_{queue});" in clear
    assert "%rs2012_qbd_reward_ready =" not in clear
    assert "inv_clear(rs2012_qbd_rewardinv)" not in clear

    release = block(session, "proc,rs2012_qbd_release_handle")
    assert "player_unlock();" in release
    assert "~rs2012_qbd_despawn_handle($handle);" in release
    assert "~rs2012_qbd_clear_state;" in release
    assert "map_instance_free($handle);" in release
    assert "%rs2012_qbd_handle = ^map_instance_none;" in release
    assert (
        "if (%map_instance_handle = $handle) %map_instance_handle = ^map_instance_none;"
        in release
    )
    assert "p_teleport" not in release

    lifecycle = block(session, "proc,rs2012_qbd_lifecycle_tick")
    assert "def_int $handle = %rs2012_qbd_handle;" in lifecycle
    assert "map_instance_find(coord) = $handle" in lifecycle
    assert "~rs2012_qbd_release_handle($handle);" in lifecycle
    assert "p_teleport" not in lifecycle
    assert (
        block(session, "softtimer,rs2012_qbd_lifecycle").strip()
        == "~rs2012_qbd_lifecycle_tick;"
    )

    stairs = block(session, "oploc1,rs2012_loc_70790")
    assert "def_int $old = %rs2012_qbd_handle;" in stairs
    assert "%rs2012_qbd_handle = $reward;" in stairs
    assert stairs.index("p_teleport(") < stairs.index(
        "~rs2012_qbd_despawn_handle($old);"
    ) < stairs.index("map_instance_free($old);")
    assert "softtimer(rs2012_qbd_lifecycle" in stairs

    leave = block(session, "proc,rs2012_qbd_leave")
    assert leave.index("p_teleport(^rs2012_qbd_outside_safe);") < leave.index(
        "~rs2012_qbd_release_handle($handle);"
    )
    logout = block(session, "proc,rs2012_qbd_on_logout")
    assert "%rs2012_qbd_handle" in logout
    assert "~rs2012_qbd_release_handle($handle);" in logout
    assert "~rs2012_qbd_on_logout;" in LOGOUT.read_text(encoding="utf-8")

    death = block(session, "proc,rs2012_qbd_on_death")
    finish = block(session, "proc,rs2012_qbd_finish_death")
    assert "%rs2012_qbd_handle" in death
    assert "map_instance_free" not in death
    assert "~rs2012_qbd_despawn_handle($handle);" in death
    assert "map_instance_free($handle);" in finish
    death_dispatch = DEATH.read_text(encoding="utf-8")
    assert "~rs2012_qbd_on_death;" in death_dispatch
    assert "~rs2012_qbd_finish_death($rs2012_qbd_death_handle);" in death_dispatch

    # The composed-cache host fixture must observe work performed on both
    # sides of inv_transmit.  Merely seeing the modal is insufficient because
    # rs2012_qbd_coffer_open mounts it immediately before transmitting ID 2000.
    world = WORLD.read_text(encoding="utf-8")
    for fragment in (
        "mock230_bank_inv_size(qbd_reward_inv) == 10",
        "contents_listeners == 1",
        "arrival_queues == 0 && close_armed == 1",
        "player->active_script == NULL",
    ):
        assert fragment in world, f"QBD host coffer completion fixture lost {fragment}"

    print(
        "rs2012 QBD lifecycle: PASS "
        "(owned handle, one-tick departure, preserved coffer, cache-backed coffer "
        "completion, manifest-only gate bypass)"
    )


if __name__ == "__main__":
    main()
