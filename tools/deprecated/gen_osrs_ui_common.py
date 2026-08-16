"""Shared OSRS dat2 UI revconfig generator (kronos-style interface dumps)."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = REPO_ROOT / "src/osrs/revconfig/configs/rev_245_2"
DUMP_BIN = REPO_ROOT / "build/dump_interface_layout"

TAB_NAMES = [
    "combat", "stats", "quests", "inventory", "equipment", "prayer", "magic",
    "clan", "account", "friends", "logout", "options", "emotes", "music",
]

# Standard OSRS sidebar iface archives (InterfaceTab 0-13).
TAB_PANEL_IFACE = [593, 320, 720, 149, 387, 541, 218, 7, 720, 429, 182, 261, 216, 239]

ICON_CHILD = {
    "fixed": [70, 71, 72, 73, 74, 75, 76, 54, 55, 56, 57, 58, 59, 60],
    "resizable_box": [66, 67, 68, 69, 70, 71, 72, 50, 51, 52, 53, 54, 55, 56],
    "resizable_bottom": [59, 60, 61, 62, 63, 64, 65, 44, 45, 46, 47, 48, 49, None],
}

STONE_CHILD = {
    "fixed": [63, 64, 65, 66, 67, 68, 69, 47, 48, 49, 50, 51, 52, 53],
    "resizable_box": [59, 60, 61, 62, 63, 64, 65, 43, 44, 45, 46, 47, 48, 49],
    "resizable_bottom": [None] * 14,
}

STONE_SPRITE = [
    "tab_stone_tl", "tab_stone_mid", "tab_stone_mid", "tab_stone_mid",
    "tab_stone_mid", "tab_stone_mid", "tab_stone_tr",
    "tab_stone_bl", "tab_stone_mid", "tab_stone_mid", "tab_stone_mid",
    "tab_stone_mid", "tab_stone_mid", "tab_stone_br",
]

IFACE = {"fixed": 548, "resizable_box": 161, "resizable_bottom": 164}
CHAT_IFACE = 162
CHAT_BUTTONS_ROOT = (CHAT_IFACE << 16) | 1
CHAT_ROOT = (765, 503)
ROOT = {"fixed": (765, 503), "resizable_box": (800, 600), "resizable_bottom": (800, 600)}

CHROME_BACKS_FIXED = [
    ("backleft1", 1),
    ("backtop1", 6),
    ("backtop_right", 7),
    ("backvmid1", 18),
    ("backright_top", 19),
    ("backhmid1", 17),
    ("backvmid2", 11),
    ("backleft2", 12),
    ("backright1", 13),
]

CHROME_PANEL_FIXED = [
    ("tabs_top_row", 61),
    ("side_panel_bg", 77),
    ("tabs_bottom_row", 45),
    ("chatback", 10),
]

CHROME_RESIZE = [
    ("side_panel_bg", 73),
    ("tabs_top_row", 61),
    ("tabs_bottom_row", 45),
]

DEFAULT_COMPONENT_HEADER = """\
; OSRS sidebar tab mappings (InterfaceTab 0-13).
; tabno | Name              | iface
; ------|-------------------|-------
;     0 | Combat            |   593
;     1 | Stats             |   320
;     2 | Quest journal     |   720
;     3 | Inventory         |   149
;     4 | Equipment         |   387
;     5 | Prayer            |   541
;     6 | Magic             |   218
;     7 | Clan chat         |     7
;     8 | Account           |   720
;     9 | Friends           |   429
;    10 | Logout            |   182
;    11 | Options           |   261
;    12 | Emotes            |   216
;    13 | Music             |   239
"""


def run_dump(cache_dir: Path, iface: int, root_w: int, root_h: int, dump_bin: Path) -> dict:
    cmd = [
        str(dump_bin), str(cache_dir), "--iface", str(iface),
        "--json", "--root-w", str(root_w), "--root-h", str(root_h),
    ]
    out = subprocess.check_output(cmd, text=True)
    return json.loads(out)


def child_lookup(dump: dict) -> dict[int, dict]:
    return {c["child_id"]: c for c in dump["children"]}


def emit_cache_ini(header: str) -> str:
    lines = [f"# {header}\n"]
    for i in range(14):
        lines.append(f"[sprite:sideicon_{i}]")
        lines.append("table=sprites")
        lines.append(f"archive_id={774 + i}")
        lines.append("atlas_index=0")
        lines.append("")
    stones = {
        "tab_stone_tl": 1026, "tab_stone_tr": 1027, "tab_stone_bl": 1028,
        "tab_stone_br": 1029, "tab_stone_mid": 1030,
        "tab_stone_mid_r": 1180, "tab_stone_mid_r2": 1181,
    }
    for name, aid in stones.items():
        lines.append(f"[sprite:{name}]")
        lines.append("table=sprites")
        lines.append(f"archive_id={aid}")
        lines.append("atlas_index=0")
        lines.append("")
    panels = {
        "invback": 297, "chatback": 1017, "mapback": 1182, "compass": 169,
        "side_panel_bg": 1031, "side_panel_bg_r": 897,
        "tabs_top_row": 1036, "tabs_bottom_row": 1032,
        "tabs_top_row_r": 1173, "tabs_bottom_row_r": 1174,
        "minimap_frame": 1182, "minimap_mask": 1183, "compass_mask": 1184,
        "backleft1": 4, "backleft2": 1034, "backright1": 1035,
        "backtop1": 1039, "backtop_right": 1441, "backright_top": 1038,
        "backvmid1": 1037, "backvmid2": 1033,
        "backhmid1": 1611,
    }
    for name, aid in panels.items():
        lines.append(f"[sprite:{name}]")
        lines.append("table=sprites")
        lines.append(f"archive_id={aid}")
        lines.append("atlas_index=0")
        lines.append("")
    lines += [
        "[sprite:cross]",
        "table=sprites",
        "archive_id=299",
        "atlas_count=8",
        "",
        "[font:p11]",
        "table=fonts",
        "archive_id=494",
        "cache_font_id=0",
        "",
        "[font:p12]",
        "table=fonts",
        "archive_id=495",
        "cache_font_id=1",
        "",
        "[font:b12]",
        "table=fonts",
        "archive_id=496",
        "cache_font_id=2",
        "",
        "[font:q8]",
        "table=fonts",
        "archive_id=497",
        "cache_font_id=3",
        "",
    ]
    return "\n".join(lines)


def emit_components(component_header: str = DEFAULT_COMPONENT_HEADER) -> str:
    lines = [
        component_header.rstrip(),
        "",
        "[component:fixed_shell]",
        "type=rs_layer", "w=765", "h=503", "",
        "[component:resizable_shell]",
        "type=rs_layer", "w=800", "h=600", "",
        "[component:world]", "type=world", "w=513", "h=335", "paint_levels=", "",
        "[component:minimap]", "type=minimap", "w=213", "h=190",
        "anchor_x=105", "anchor_y=101", "",
        "[component:compass]", "type=compass", "sprite=compass", "w=34", "h=34",
        "anchor_x=16", "anchor_y=16", "",
        "[component:cross]", "type=cross", "sprite=cross", "w=32", "h=32",
        "anchor_x=12", "anchor_y=12", "",
        "[component:minimenu]", "type=minimenu", "font=b12", "w=200", "h=200", "",
        "[component:chat_region]", "type=chat", f"componentno={CHAT_BUTTONS_ROOT}",
        f"w={CHAT_ROOT[0]}", f"h={CHAT_ROOT[1]}", "",
    ]
    for sprite in (
        "invback", "mapback", "backleft1", "backleft2", "backright1",
        "backtop1", "backtop_right", "backright_top", "backvmid1", "backvmid2",
        "backhmid1", "chatback", "side_panel_bg", "tabs_top_row", "tabs_bottom_row",
    ):
        lines += [f"[component:{sprite}]", "type=sprite", f"sprite={sprite}", ""]
    for i, name in enumerate(TAB_NAMES):
        lines += [
            f"[component:sideicon_{name}]",
            "type=tab_icon", f"tabno={i}", f"sprite=sideicon_{i}", "",
        ]
        lines += [
            f"[component:redstone_tab_{name}]",
            "type=redstone_tab", f"tabno={i}",
            f"sprite_active={STONE_SPRITE[i]}", "",
        ]
        sidebar_lines = [
            f"[component:sidebar_tab_{i}]",
            "type=sidebar", f"tabno={i}", f"componentno={TAB_PANEL_IFACE[i]}",
        ]
        if i == 3:
            sidebar_lines += [
                "; Interface.INVENTORY (149); archive file 0 is the INV grid.",
                "inv=inventory",
            ]
        sidebar_lines.append("")
        lines += sidebar_lines
    lines += ["[inv:inventory]", "item=1333", ""]
    return "\n".join(lines)


def layout_entry(
    node: str,
    component: str,
    x: int,
    y: int,
    parent: str = "",
    dirty: bool = False,
    w: int = 0,
    h: int = 0,
) -> list[str]:
    rows = [f"n={node}" if node else None, f"p={parent}" if parent else None,
            f"c={component}", f"x={x}", f"y={y}"]
    if w > 0:
        rows.append(f"w={w}")
    if h > 0:
        rows.append(f"h={h}")
    if dirty:
        rows.append("dirty=true")
    rows.append("=")
    return [r for r in rows if r is not None]


def layout_from_child(
    node: str,
    component: str,
    child: dict,
    parent: str = "",
    dirty: bool = False,
) -> list[str]:
    return layout_entry(
        node, component, child["x"], child["y"], parent, dirty,
        child.get("w", 0), child.get("h", 0),
    )


def emit_minimap_underlay(
    lines: list[str],
    lookup: dict[int, dict],
    shell: str,
    group: str,
) -> None:
    if group == "fixed":
        mini = lookup.get(21) or lookup.get(24) or lookup.get(8)
        compass = lookup.get(23)
    else:
        mini = lookup.get(30) or lookup.get(22) or lookup.get(17) or lookup.get(8)
        compass = lookup.get(31)

    if mini:
        lines += layout_from_child("minimap_viewport", "minimap", mini, shell)
    if compass:
        lines += layout_from_child("compass_widget", "compass", compass, shell)
    elif mini:
        lines += layout_from_child("compass_widget", "compass", mini, shell)


def emit_mapback_overlay(
    lines: list[str],
    lookup: dict[int, dict],
    shell: str,
    group: str,
) -> None:
    if group == "fixed":
        mapback = lookup.get(22)
    else:
        mapback = lookup.get(32) or lookup.get(22)
    if mapback:
        lines += layout_from_child("", "mapback", mapback, shell)


def emit_layout_group(
    group: str,
    dump: dict,
    lookup: dict[int, dict],
) -> str:
    shell = "fixed_shell" if group == "fixed" else "resizable_shell"
    lines = [f"[layout:{group}]"]
    lines += layout_entry(shell, shell, 0, 0)
    if group == "fixed":
        world = lookup.get(25) or lookup.get(9)
        if world:
            lines += layout_from_child("world_viewport", "world", world, shell)
        emit_minimap_underlay(lines, lookup, shell, group)
        for comp, cid in CHROME_BACKS_FIXED:
            c = lookup.get(cid)
            if c:
                lines += layout_from_child(
                    "", comp, c, shell, dirty=comp.startswith("back"))
        emit_mapback_overlay(lines, lookup, shell, group)
        for comp, cid in CHROME_PANEL_FIXED:
            c = lookup.get(cid)
            if c:
                lines += layout_from_child("", comp, c, shell)
    else:
        world = lookup.get(16) or lookup.get(12)
        if world:
            lines += layout_from_child("world_viewport", "world", world, shell)
        emit_minimap_underlay(lines, lookup, shell, group)
        emit_mapback_overlay(lines, lookup, shell, group)
        for comp, cid in CHROME_RESIZE:
            c = lookup.get(cid)
            if c:
                lines += layout_from_child("", comp, c, shell)
    for i, name in enumerate(TAB_NAMES):
        cid = STONE_CHILD[group][i]
        if cid is not None:
            c = lookup.get(cid)
            if c:
                lines += layout_from_child("", f"redstone_tab_{name}", c, shell)
    for i, name in enumerate(TAB_NAMES):
        cid = ICON_CHILD[group][i]
        if cid is not None:
            c = lookup.get(cid)
            if c:
                lines += layout_from_child("", f"sideicon_{name}", c, shell)
    sb = lookup.get(16) or lookup.get(78)
    if sb:
        for i in range(14):
            lines += layout_from_child("", f"sidebar_tab_{i}", sb, shell)
    lines += layout_entry(
        "chat_region", "chat_region", 0, 0, shell, w=CHAT_ROOT[0], h=CHAT_ROOT[1])
    lines += layout_entry("cross_widget", "cross", 0, 0, shell, dirty=True)
    lines += layout_entry("minimenu_widget", "minimenu", 0, 0, shell, dirty=True)
    return "\n".join(lines) + "\n"


def generate(
    cache_dir: Path,
    dump_bin: Path,
    out_dir: Path,
    prefix: str,
    cache_header: str,
    component_header: str = DEFAULT_COMPONENT_HEADER,
) -> int:
    if not dump_bin.is_file():
        print(f"dump tool not found: {dump_bin}", file=sys.stderr)
        return 1

    dumps = {}
    for group, iface in IFACE.items():
        rw, rh = ROOT[group]
        dumps[group] = run_dump(cache_dir, iface, rw, rh, dump_bin)

    cache_ini = emit_cache_ini(cache_header)
    ui_parts = [emit_components(component_header)]
    for group in IFACE:
        ui_parts.append(emit_layout_group(group, dumps[group], child_lookup(dumps[group])))

    out_dir.mkdir(parents=True, exist_ok=True)
    cache_path = out_dir / f"{prefix}_ui_cache.ini"
    ui_path = out_dir / f"{prefix}_ui.ini"
    cache_path.write_text(cache_ini)
    ui_path.write_text("\n".join(ui_parts))
    print(f"Wrote {cache_path}")
    print(f"Wrote {ui_path}")
    return 0
