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
    "classic548": {"inv": "615,228", "inv_hidden": False, "report": "493,491", "report_hidden": False, "npc": "330,120", "keyboard": True},
    "classic161": {"inv": "615,228", "inv_hidden": False, "report": "493,491", "report_hidden": False, "npc": "330,120", "keyboard": True},
    "modern164":  {"inv": "607,224", "inv_hidden": True,  "report": "476,491", "report_hidden": False, "npc": "456,228", "keyboard": True},
    # 601 logs in as a phone, so Porcelain_KeyEdge answers ABSENT and every
    # reveal-key feature turns itself off there. Measured rather than assumed:
    # the run logs one key_edge/absent finding at first_frame=0 whose detail is
    # "touch lane has no key", which is the branch inside the CALL and not the
    # fence's later poll of the binding.
    "stone601":   {"inv": "534,231", "inv_hidden": True,  "report": "476,370", "report_hidden": True, "npc": "456,150", "keyboard": False},
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
    # The hull is HALF of this plugin. The other half is the Tag/Untag row on
    # the right-click menu, and every row of it is gated on the reveal key
    # being HELD -- so the drive above, which holds no key and opens no menu,
    # photographs a plugin whose entire menu half could be deleted without
    # moving one pixel or one line of log. The port's own header calls the
    # on_key forward "the whole fix" and says the rows were unreachable on
    # EVERY lane before it; nothing in the capture set could tell.
    #
    # Both halves have a drive now, and each one proves itself:
    #
    #   ehreveal  the key held and a right-click on a TAGGED npc, left open, so
    #             the final frame carries the row. The npc is named by TYPE and
    #             not by coordinate for two reasons: it has to be one of the
    #             tagged ids or the row reads "Tag" and the picture is of a
    #             different claim, and a wandering npc makes a fixed pixel a
    #             coin toss (which is why TORIRS_SIM_CLICK_NPC exists).
    #   ehtag     the reverse, and the one that proves the SELECT: no tags at
    #             all, so the row reads "Tag", TORIRS_SIM_MENU_ROW finds it by
    #             its label and clicks it -- printing the label it found, so a
    #             row that was never built cannot pass silently -- and the hull
    #             in the last frame exists ONLY because that row was picked.
    ("entity-highlighter",  "ehreveal",
     "'TORIRS_SIM_PLUGIN_CONFIG=60,entity-highlighter,tags,5037,6708,2880,2899,3106,3108'"
     " {reveal} TORIRS_SIM_CLICK_NPC=620,5037,1"),
    ("entity-highlighter",  "ehtag",
     "{reveal} TORIRS_SIM_CLICK_NPC=560,5037,1 TORIRS_SIM_MENU_ROW=600,Tag"),
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

# Hold the reveal key for the rest of the run. 42 is TORIRS_KEY_SHIFT, which is
# the code porcelain_key_code maps the config name "shift" to and the code the
# host puts in a ToriRS_KeyEvent -- ONE numbering, not the OSRS key space the
# cache's own event stream carries.
#
# Deferred to frame 500 for a reason that has already cost a capture elsewhere:
# a key pressed on frame 1 lands on the title screen. And it is pressed exactly
# once -- the sim never releases it -- so a handler that misses that single
# transition never gets a second chance, which is precisely the failure this
# drive exists to be able to see.
REVEAL = "TORIRS_SIM_KEYHOLD=42 TORIRS_SIM_KEYHOLD_FRAME=500"


def main():
    print("# Every plugin on every CS2 toplevel. GENERATED by gen_matrix.py.")
    print("# 548 and 161 show the sidebar; 164 and 601 hide it, so a drive that")
    print("# points at the inventory is inert there until something opens it --")
    print("# those lines carry NEEDS_OPEN and are NOT defects. A drive that holds")
    print("# the reveal key carries NO_KEYBOARD on 601, where the plugin declares")
    print("# the key absent and turns its rows off; that is not a defect either.")
    print("# Coordinates come from lane_coords.py, never from another toplevel.")
    for lane, box in LANES.items():
        print(f"\n# ---- {lane} " + "-" * 56)
        for plugin, stem, drive in PLUGINS:
            needs = []
            if "{inv}" in drive and box["inv_hidden"]:
                needs.append("inventory hidden on this toplevel")
            if plugin == "screenshot" and box["report_hidden"]:
                needs.append("report button hidden on this toplevel")
            # A reveal-key drive on a lane with no keyboard frame is not a
            # broken drive. The plugin DECLARES that absence and turns the rows
            # off, and the shot showing a menu without them is the evidence for
            # it -- so it is marked for the same reason NEEDS_OPEN is, and with
            # its own word, because "this lane cannot answer" and "something
            # has to be opened first" are different facts about a shot.
            no_keyboard = "{reveal}" in drive and not box["keyboard"]
            if no_keyboard:
                needs.append("no keyboard frame: the reveal rows are declared off here")
            body = drive.format(inv=box["inv"], report=box["report"],
                                npc=box.get("npc", "330,120"), reveal=REVEAL)
            line = f"{stem}-{lane} {plugin} {lane} {body} {TRACE}".replace("  ", " ").strip()
            if needs:
                line += "   # " + ("NO_KEYBOARD" if no_keyboard else "NEEDS_OPEN") + ": " \
                    + "; ".join(needs)
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
