#!/usr/bin/env python3
"""Emit one jobs line per (plugin x CS2 toplevel).

The per-plugin set used to be toplevel 548 and nothing else, so three quarters
of the CS2 lane was never photographed. The lane has four toplevels and they do
not merely look different -- they put things in different places, and two of
them do not show the sidebar at all until something is tapped:

    548  classic-fixed    inventory VISIBLE   report VISIBLE
    161  classic-fixed    inventory VISIBLE   report VISIBLE
    164  modern-resizable inventory HIDDEN    report visible, 17px left of 548
    601  stone-drawer     inventory HIDDEN    report HIDDEN (in the drawer)

Those boxes are not guesses. They are read out of a capture's own ROLE_WIDGET
lines by lane_coords.py, because a coordinate copied from 548 onto another
toplevel does not fail loudly: the pointer lands on empty chrome, the plugin
correctly draws nothing, and the shot is indistinguishable from a plugin that
is broken. That is worse than a crash -- it is a wrong answer that looks like a
finding.

Where a drive needs something opened before it can point at anything, the line
is emitted with NEEDS_OPEN in a trailing comment rather than silently wrong, so
the inspection pass knows the shot is expected to be inert until that is
written. Do not read those as defects.

    python3 gen_matrix.py > jobs/cs2_toplevels.txt
"""

import sys

# Resolved by lane_coords.py from tools/porcelain_gate gate captures.
LANES = {
    "classic548": {"inv": "615,228", "inv_hidden": False, "report": "493,491", "report_hidden": False, "npc": "330,120"},
    "classic161": {"inv": "615,228", "inv_hidden": False, "report": "493,491", "report_hidden": False, "npc": "330,120"},
    "modern164":  {"inv": "607,224", "inv_hidden": True,  "report": "476,491", "report_hidden": False, "npc": "456,228"},
    "stone601":   {"inv": "534,231", "inv_hidden": True,  "report": "476,370", "report_hidden": True, "npc": "456,150"},
}

# plugin id -> drive. {inv} and {report} are substituted per lane; a drive
# naming {inv} on a lane whose inventory is hidden gets NEEDS_OPEN.
PLUGINS = [
    ("none",                "control",        ""),
    ("minimap-orbs",        "orbs",           ""),
    ("gameframe-layout",    "gf-desktop",     "TORIRS_SHOT_FRAME=gameframe-layout/classic-fixed"),
    # preferred_frame is the MASTER SWITCH for a frame provider, not `enabled`.
    # Without it the ini says enabled=1, the host says enabled=0, and the shot
    # photographs whichever provider the lane preference already named --
    # gf-mobile-classic548 came back byte-identical to gf-desktop.
    ("mobile-gameframe",    "gf-mobile",      "TORIRS_SHOT_FRAME=mobile-gameframe/stone-drawer"),
    ("performance-display", "perf",           ""),
    ("screenshot",          "screenshot",     "-- camera=report-button"),
    ("widget-demo",         "widgetdemo",     "TORIRS_WIDGET_DEMO=only TORIRS_PLUGIN_LOG=1"),
    ("tile-indicator-c",    "tileind-c",      "TORIRS_SIM_HOVER=300,180"),
    ("tile-indicator-lua",  "tileind-lua",    "TORIRS_SIM_HOVER=300,180"),
    # setlevel lands on a level threshold EXACTLY, so xp == level_xp, progress
    # computes to 0, and xp_orbs.c gates the arc behind `progress > 0`. The
    # globe drew and the arc it exists to show never did, on any toplevel. An
    # added award must name the SAME skill setlevel moved -- index 10 is
    # fishing, not attack, and an award to another skill raises no gain here.
    # It must also land WELL INSIDE the frame budget: the default is 700 and a
    # command scheduled at 700 is sent on the frame the run ends on, so the
    # gain it causes never arrives.
    # added xp award afterwards puts the orb mid-level where the arc is real.
    ("xp-drop-orbs",        "xporbs",         "'TORIRS_SIM_CMD=560,setlevel 10 45;620,xp fishing 8000' TORIRS_MAX_FRAMES=900"),
    ("xp-tracker",          "xptracker",      "TORIRS_SIM_PLUGIN_PANEL=600,xp-tracker,page 'TORIRS_SIM_CMD=300,setlevel 10 45;400,xp fishing 30000'"),
    ("client-settings",     "clientsettings", "TORIRS_SIM_PLUGIN_PANEL=600,client-settings,page"),
    ("feature-flags",       "featureflags",   "TORIRS_SIM_PLUGIN_PANEL=600,feature-flags,page"),
    ("item-stats",          "itemstats",      "TORIRS_SIM_HOVER={inv}"),
    ("loot-tracker",        "loottracker",    "TORIRS_SIM_PLUGIN_PANEL=620,loot-tracker,page 'TORIRS_SIM_CMD=560,lootkill Goblin 995 5000;570,lootkill Goblin 526 1'"),
    ("ground-items",        "grounditems",    "'TORIRS_SIM_CMD=60,dropobj abyssal_tentacle 1;70,dropobj ags 1;80,dropobj abyssal_whip 1' TORIRS_GROUND_ITEMS_DEBUG=1"),
    ("loot-beam",           "lootbeam",       "'TORIRS_SIM_CMD=60,dropobj abyssal_tentacle 1'"),
    ("entity-highlighter",  "highlighter",    "'TORIRS_SIM_PLUGIN_CONFIG=60,entity-highlighter,tags,5037,6708,2880,2899,3106,3108'"),
    ("nxt-highlight",       "nxthl",          "TORIRS_SIM_MOVE_AT=600,{npc} TORIRS_SIM_HOVER={npc} TORIRS_HIGHLIGHT_DEBUG=1"),
    ("nxt-bird-nest",       "birdnest",       "TORIRS_SIM_VARBIT=450,13087,0 'TORIRS_SIM_CMD=600,dropobj bird_nest_egg_red 1'"),
    # TWO cannon rows, because the builtin has two edges and one drive cannot
    # reach both.
    #
    # `cannon` is the EMPTY edge: ::cannon loads fifteen, loc op 3 ("Empty")
    # takes fifteen to zero in a single tick, and the picture is the lane's own
    # "Your cannon is out of ammunition!" with NO plugin line beside it --
    # nxt_cannon_chat cancelling the held announcement.
    #
    # All three settings rows are written now, and 14176 is the one that was
    # missing. Setting 249 (`cannon_low_amount`) is a 0..310 slider whose
    # default is 0, and nxt_cannon_sample refuses `threshold > 0`, so the low
    # test was unreachable on every lane and the shot could not tell "the
    # precedence rule works" from "the row does nothing". It costs this row
    # nothing to state -- measured, chat band pixel-identical with it at 12 --
    # and it turns the absence of a low line into an assertion: a jump from 15
    # to 0 crosses twelve AND reaches empty, and `ammo == 0` claims it.
    #
    # `cannonlow` is the LOW edge, and it needs a second thing the empty drive
    # cannot give it: an ammunition movement that is not a jump to zero.
    # cannon_fire_once spends ONE ball per tick and only when a live npc is
    # inside ^cannon_range, and the fixture tile has none -- which is why the
    # empty row still had its full fifteen at frame 1400. Five goblins are five
    # guaranteed firing ticks (one shot a tick, so at most one dies per tick),
    # so 15 -> 12 happens on the third and the crossing is deterministic
    # whatever the damage roll. No loc op: the cannon must NOT reach zero here,
    # or the empty line lands on top of the one being photographed.
    ("nxt-cannon-ammo",     "cannon",         "'TORIRS_SIM_VARBIT=450,14175,1;452,14176,12;454,14177,1' 'TORIRS_SIM_CMD=500,cannon' TORIRS_SIM_OPLOC=1400,3,3210,3424,6 TORIRS_MAX_FRAMES=1500"),
    ("nxt-cannon-ammo",     "cannonlow",      "'TORIRS_SIM_VARBIT=450,14175,1;452,14176,12;454,14177,1' 'TORIRS_SIM_CMD=460,spawn goblin 5;500,cannon' TORIRS_MAX_FRAMES=1500"),
]

TRACE = "TORIRS_TRACE_NATIVE_UI=1 TORIRS_DUMP_ROLES=1 TORIRS_DUMP_BOUNDS=all"


def main():
    print("# Every plugin on every CS2 toplevel. GENERATED by gen_matrix.py.")
    print("# 548 and 161 show the sidebar; 164 and 601 hide it, so a drive that")
    print("# points at the inventory is inert there until something opens it --")
    print("# those lines carry NEEDS_OPEN and are NOT defects.")
    print("# Coordinates come from lane_coords.py, never from another toplevel.")
    for lane, box in LANES.items():
        print(f"\n# ---- {lane} " + "-" * 56)
        for plugin, stem, drive in PLUGINS:
            needs = []
            if "{inv}" in drive and box["inv_hidden"]:
                needs.append("inventory hidden on this toplevel")
            if plugin == "screenshot" and box["report_hidden"]:
                needs.append("report button hidden on this toplevel")
            body = drive.format(inv=box["inv"], report=box["report"], npc=box.get("npc", "330,120"))
            line = f"{stem}-{lane} {plugin} {lane} {body} {TRACE}".replace("  ", " ").strip()
            if needs:
                line += "   # NEEDS_OPEN: " + "; ".join(needs)
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
