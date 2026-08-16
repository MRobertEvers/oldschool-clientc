#!/usr/bin/env python3
"""Hermetic structural checks for the RS2012 QBD interface asset lane."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from port_rs2012_qbd_ui import (
    COMPONENT_NAMES,
    INTERFACES,
    LANE,
    REPO_ROOT,
    SPRITE_BASE,
    SPRITE_SOURCES,
    parse_ledger,
    parse_pack,
)


TREE = REPO_ROOT / "OSRS-Content" / "osrs239-content"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def component_blocks(text: str) -> dict[str, str]:
    matches = list(re.finditer(r"(?ms)^\[([^]]+)]\n.*?(?=^\[|\Z)", text))
    return {match.group(1): match.group(0) for match in matches}


def field_values(text: str, field: str) -> list[int]:
    return [
        int(value)
        for value in re.findall(rf"(?m)^{re.escape(field)}=(-?\d+)\s*$", text)
    ]


def main() -> int:
    lane = TREE / LANE
    interface_pack = parse_pack(lane / "pack/3_interfaces.pack")
    sprite_pack = parse_pack(lane / "pack/8_sprites.pack")
    base_interfaces = parse_pack(TREE / "pack/3_interfaces.pack")
    base_sprites = parse_pack(TREE / "pack/8_sprites.pack")
    ledger = parse_ledger(TREE / "port/rs2012_qbd_td.map")
    clientscript_pack = parse_pack(lane / "pack/12_clientscripts.pack")
    require(
        clientscript_pack == {
            13000: "rs2012_qbd_hud_pool",
            13001: "rs2012_qbd_hud_pool_fade",
        },
        "native QBD HUD clientscript allocation drifted",
    )
    hud_script = lane / "scripts/rs2012_qbd_hud_pool.cs2"
    require(hud_script.is_file() and not hud_script.is_symlink(), "HUD pool clientscript is missing")

    for ident, name in INTERFACES.items():
        require(interface_pack.get(ident) == name, f"missing interface {ident}={name}")
        require(ident not in base_interfaces, f"interface {ident} collides with OSRS239")

    expected_sprite_ids = set(range(SPRITE_BASE, SPRITE_BASE + len(SPRITE_SOURCES)))
    require(not (expected_sprite_ids & set(base_sprites)), "QBD sprite range collides with OSRS239")
    for index, source in enumerate(SPRITE_SOURCES):
        destination = SPRITE_BASE + index
        name = f"ported/rs2012_qbd_td/rs2012_qbd_sprite_{source}"
        require(sprite_pack.get(destination) == name, f"bad sprite row {destination}")
        directory = TREE / "sprites" / name
        require(directory.is_dir() and not directory.is_symlink(), f"missing sprite {source}")
        files = list(directory.iterdir())
        require(all(path.is_file() and not path.is_symlink() for path in files), f"unsafe sprite {source}")
        require((directory / "pack.meta").is_file(), f"sprite {source} lacks metadata")
        require(any(path.suffix == ".bmp" for path in files), f"sprite {source} lacks frames")

    expected_counts = {1284: 47, 1285: 35}
    for ident, name in INTERFACES.items():
        source = lane / "interfaces" / f"{name}.if"
        compack = lane / "interfaces" / f"{name}.compack"
        require(source.is_file() and compack.is_file(), f"interface {ident} is incomplete")
        text = source.read_text()
        names = parse_pack(compack)
        expected_names = {
            component: COMPONENT_NAMES.get(ident, {}).get(component, f"com_{component}")
            for component in range(expected_counts[ident])
        }
        require(names == expected_names, f"interface {ident} component names drifted")
        require(set(component_blocks(text)) == set(names.values()), f"interface {ident} blocks drifted")
        require(not re.search(r"(?m)^on[a-z0-9_]*=", text), f"interface {ident} retained RS727 hooks")
        require(set(field_values(text, "graphic")) <= expected_sprite_ids, f"interface {ident} has a foreign sprite")
        for layer in field_values(text, "layer"):
            require(layer >> 16 == ident, f"interface {ident} has a foreign layer {layer}")

    coffer = (lane / "interfaces/rs2012_qbd_coffer.if").read_text()
    coffer_blocks = component_blocks(coffer)
    require("op1=Bank-all" in coffer_blocks["bank_all"], "bank-all binding drifted")
    require("op1=Abandon-all" in coffer_blocks["abandon_all"], "abandon-all binding drifted")
    require("op1=Take-all" in coffer_blocks["take_all"], "take-all binding drifted")
    # The visible labels are separate children in the authentic archive.  Pin
    # these alongside the named click parents so destructive actions cannot be
    # silently swapped again.
    require("text=Abandon all" in coffer_blocks["com_42"], "visible abandon label drifted")
    require("text=Take all" in coffer_blocks["com_46"], "visible take label drifted")
    require(set(field_values(coffer, "font")) == {495}, "coffer font is not p12_full")

    hud = (lane / "interfaces/rs2012_qbd_hud.if").read_text()
    hud_blocks = component_blocks(hud)
    # Source component 0 is a full-screen, no-click-through sentinel. It has no
    # children; unhiding it would intercept every arena click while adding no
    # visible HUD content. The rootless status/time components render on their
    # own, exactly as in the source interface.
    require("hidden=yes" in hud_blocks["root"], "HUD root must remain hidden")
    require("hidden=yes" in hud_blocks["time_overlay"], "time-stop overlay starts visible")
    require("hidden=yes" in hud_blocks["damage_bar"], "damage-flash bar starts visible")
    require(field_values(hud, "model") == [ledger[("model", 70127)]], "HUD model mapping drifted")
    require(field_values(hud, "modelanim") == [ledger[("seq", 9390)]], "HUD sequence mapping drifted")

    print(
        "test_rs2012_qbd_ui_port: 2 interfaces, 82 named components, "
        "32 sprite groups, 2 native HUD scripts, references, HUD safety and destructive-button bindings OK"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ValueError) as exc:
        print(f"test_rs2012_qbd_ui_port: {exc}", file=sys.stderr)
        raise SystemExit(1)
