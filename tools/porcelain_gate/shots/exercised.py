#!/usr/bin/env python3
"""Did this shot actually put the plugin's work on screen?

A drive that runs without error and puts NOTHING in front of its plugin is the
most expensive failure in this harness, because it does not look like a
failure. The client boots, the command is sent, the log says applied=1, a PNG
is written -- and the plugin correctly draws nothing, because the hover landed
on empty chrome, or the item under the pointer had no stats, or the provider
was never switched on, or the award fired on the frame the run ended.

Correct code drawing nothing is PIXEL-IDENTICAL to broken code. That is the
whole problem: the shot is then read as evidence about the plugin when it is
only evidence about the drive.

Six separate drives in this set were inert and every one was caught downstream
by somebody reading pixels, days late:

  * gf-mobile-*   photographed a different provider entirely -- preferred_frame
                  is the master switch for a frame provider, not `enabled`.
  * screenshot-*  drew nothing: the plugin's `camera` config defaults to "off".
  * nxthl-*       reused 548's hover pixel on every toplevel; it lands 99-110px
                  from the npc on the others.
  * itemstats-*   hovered inventory slot 0, which holds runes -- no stats, so
                  correctly no tooltip.
  * xporbs-*      awarded xp on the very frame the run ended.
  * highlighter-* drove the hulls on all five lanes and held no key and opened
                  no menu, so the Tag/Untag rows -- the half the port was FOR
                  -- were never built in any of the five.

They have nothing in common at the knob level, which is why remembering them
one at a time does not work. They have everything in common HERE: the shot is
indistinguishable from the same lane with the plugin absent.

    python3 exercised.py <shot.png> --control <control.png> [--box x,y,w,h]
    python3 exercised.py --menu-row <label> --log runs/<shot>/log.txt
    python3 exercised.py --selftest
    python3 exercised.py --jobs jobs/cs2_toplevels.txt --shots plugins/

`--selftest` runs the log readers above on bodies whose answer is known and
takes a second; run it after touching one of them, because the state each of
them reports is SILENCE in the log, and a reader that answered "found" for the
empty case would put back exactly the blindness it was written to remove.

Four signals, because no one of them is sufficient alone, and each was added
the day the ones before it were shown to be blind to something:

  INK   the shot against a no-plugin control, inside a box, against a floor
        measured from a SECOND control in that same box. Sound when --box names
        the plugin's own region; whole-frame it is dominated by scene animation
        (two no-plugin runs differ by 532 px on classic548 but 2226 on
        classic161 and 3669 on stone601 -- more than a small overlay draws).
  LOG   whether the plugin printed anything of its own. Decisive where a plugin
        has diagnostics: that asymmetry is how the loot-beam lane defect was
        found, CS2 logging its beam count and CS1 logging nothing.

  MENU  whether a row the PLUGIN added to the right-click menu was actually
        built. The only one of the four that can see a retained menu row, and
        the reason it exists:

          * INK cannot: the plugin's row is drawn by the client's own minimenu
            in the client's own font, so a menu with the row and a menu without
            it differ by a few hundred pixels of ordinary chrome inside a box
            that moves with the click -- and a menu that is open at all swamps
            any box you could scope the comparison to.
          * LOG cannot: entity-highlighter prints nothing of its own, and the
            plugins that do print from their draw pass, not their menu build.
          * EMIT cannot: a menu row is not an owned widget and carries no
            scene id.

        So the entity highlighter's whole menu half -- the Tag/Untag rows its
        own header calls the point of the port -- was photographed five times
        with no key held and no menu open, and every one of those shots was
        pixel-identical and log-identical to the same plugin with its whole
        menu half deleted. TORIRS_SIM_MENU_ROW prints the label it FOUND, so
        naming the label here turns "the row was never built" from a shot
        nobody can read into a line this tool refuses.

  EMIT  whether a widget the plugin OWNS put a draw command in the exit draw
        list. The only one of the four that is sound for a FRAME PROVIDER,
        and the reason it exists:

          * INK is zero by construction. gameframe-layout's Classic Fixed on a
            2004 dat1 lane is a deliberate pixel-for-pixel reproduction of that
            lane's own frame -- the geometry is copied from the lane's own
            `[layout:fixed]` and the art is cut from the same media jagfile --
            so the provider drawing the whole frame and the provider never
            starting are THE SAME PICTURE, to 0 pixels outside the minimap.
          * LOG is printed by the code that INTENDED the frame. The layout line
            said "15 chrome pieces, 14 tabs" off a plan filled before the two
            fences that can make a pass state nothing.

        So a working provider was reported on this branch as reaching the
        screen with nothing at all, and neither signal here could contradict
        it. EMIT can: 56 of the plugin's own scene ids in the exit draw list is
        the frame buffer's own answer. Needs a run with TORIRS_TRACE_NATIVE_UI
        (for the OWNED_WIDGET lines) and TORIRS_DUMP_EMIT_EXIT=all.

WHAT THIS CANNOT DO, stated because a check that overclaims is worse than none:
performance-display, tile-indicator-c, nxt-highlight and screenshot print NO
diagnostic line, so on a busy lane, whole-frame, neither of the first two
signals decides and the verdict is UNCLEAR unless the run carried the emit
traces. That is a real state and NOT a pass -- re-run with --box for that
plugin's region. Supplying per-plugin boxes is the completion of this tool and
is not done yet.

EMIT does not name WHICH plugin owned the draw: the owner on an OWNED_WIDGET
line is the host's numeric owner id and nothing in the log maps it to an id.
pshot.sh runs one plugin at a time, which is what makes the answer that
plugin's; a log from a run with several enabled is answering about all of them
at once, and says so.
"""

import argparse
import os
import re
import sys

from PIL import Image

# Two no-plugin runs of the same lane are not identical: npcs walk, water moves,
# flames flicker. Measured whole-frame, that animation is 532 px on classic548,
# 2226 on classic161, 2321 on modern164 and 3669 on stone601 -- far more than a
# small overlay draws, so a whole-frame comparison cannot see one at all.
#
# The first version of this script tried to dodge that by excluding the 3D
# viewport and counting only chrome. That was worse: performance-display, the
# tile indicators, the entity highlighter and every plugin whose page opens over
# the world draw INSIDE the viewport, so the exclusion discarded precisely their
# output and called eight working plugins inert. A check that cries wolf on a
# third of the set does not get used.
#
# So the floor is measured LOCALLY, in the same box, from two controls. Whatever
# the animation does in that region it does in both control runs; a plugin's ink
# is what the shot has and neither control does.


def diff_in(a, b, box):
    """Pixels differing between two images inside box=(x0,y0,x1,y1)."""
    pa, pb = a.load(), b.load()
    width, height = a.size
    x0, y0, x1, y1 = box
    return sum(
        1
        for y in range(max(0, y0), min(height, y1))
        for x in range(max(0, x0), min(width, x1))
        if pa[x, y] != pb[x, y]
    )


def owned_draws(body):
    """(owned scene ids, how many of them the exit draw list carries).

    An owned graphic's picture is a `scene=` id on its OWNED_WIDGET line, and a
    draw command that put that picture on the frame buffer carries the same id
    on its EMIT_EXIT line. Scene 0 and -1 are "no picture" and name nothing.
    """
    owned = {
        int(m)
        for m in re.findall(r"^OWNED_WIDGET .* scene=(-?\d+) ", body, re.M)
        if int(m) > 0
    }
    if not owned:
        return owned, 0
    drawn = [int(m) for m in re.findall(r"^EMIT_EXIT\[\d+\] .* scene=(-?\d+) ", body, re.M)]
    return owned, sum(1 for scene in drawn if scene in owned)


def menu_row_built(body, prefix):
    """The label TORIRS_SIM_MENU_ROW found for `prefix`, or None.

    `sim_menu_row: frame=600 row 'Tag @yel@Romeo' move 326,220` is printed at
    the moment the harness finds a row whose text starts with the prefix it was
    given, BEFORE it clicks it -- so the line is evidence that the row existed,
    not merely that a click was attempted. No line means no such row was ever
    on an open menu, which is a drive that proved nothing and must not read as
    a pass.

    The prefix is matched against the label the harness reports rather than
    trusted from the drive, because a row that starts with the prefix by
    accident is a different row: "Tag" and "Untag" both end in the same three
    letters and only one of them is a prefix of the other.
    """
    assert prefix
    for label in re.findall(r"^sim_menu_row: frame=\d+ row '(.*)' move ", body, re.M):
        if label.startswith(prefix):
            return label
    return None


def reveal_key_declared_absent(body):
    """The detail of a DECLARED key_edge absence, or None.

    A reveal-gated row cannot be built on a lane whose key the plugin has been
    told does not exist, and that is not the same state as a broken drive: 601
    logs in as a phone, Porcelain_KeyEdge answers ABSENT at the call, and the
    plugin turns its rows off and says so. Without this the check would report
    the touch lane INERT and a reader would be invited to read a correct
    picture as a defect -- which is the mistake this whole file exists against.

    `expected=1` is required, so only an absence the plugin DECLARED excuses
    the missing row. An undeclared one is a surprise and stays a failure.
    """
    for detail in re.findall(
            r"^PORCELAIN_FINDING .*verb=key_edge .*detail=(.*?) expected=1 ", body, re.M):
        return detail
    return None


def selftest():
    """The two log readers above, on bodies whose answer is known.

    It exists because the state each of them reports is SILENCE: a drive that
    builds no row prints no line, and a reader that answered "found" for the
    empty case would put back exactly the blindness they were written to
    remove -- with nothing on screen to contradict it.
    """
    found = "sim_menu_row: frame=600 row 'Tag @yel@Romeo' move 326,220\n"
    untag = "sim_menu_row: frame=600 row 'Untag @yel@Romeo' move 326,220\n"
    cases = [
        ("a built row is reported with its label", found, "Tag", "Tag @yel@Romeo"),
        ("no line at all is no row", "boot\nSIM_READY elapsed_ms=1\n", "Tag", None),
        ("Untag is not a Tag row", untag, "Tag", None),
        ("Untag is its own row", untag, "Untag", "Untag @yel@Romeo"),
        ("a row for another prefix is not this one", found, "Mark", None),
    ]
    for why, body, prefix, want in cases:
        got = menu_row_built(body, prefix)
        assert got == want, f"{why}: got {got!r}, want {want!r}"

    declared = ("PORCELAIN_FINDING plugin=entity-highlighter verb=key_edge "
                "element=role(reveal_key) result=1 detail=touch lane has no key "
                "expected=1 why=a touch lane has no keyboard frame first_frame=0 count=1\n")
    undeclared = declared.replace("expected=1", "expected=0")
    absences = [
        ("a declared key absence is named", declared, "touch lane has no key"),
        ("an UNdeclared one excuses nothing", undeclared, None),
        ("a run with no such finding declares nothing", found, None),
    ]
    for why, body, want in absences:
        got = reveal_key_declared_absent(body)
        assert got == want, f"{why}: got {got!r}, want {want!r}"

    print("exercised.py selftest: %d menu-row and %d absence cases pass"
          % (len(cases), len(absences)))
    return 0


def main(argv):
    if argv and argv[0] == "--selftest":
        return selftest()
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("shot", nargs="?")
    parser.add_argument("--control", help="the same lane with no plugins")
    parser.add_argument("--control2", help="a SECOND no-plugin run, for the local animation floor")
    parser.add_argument("--log", help="runs/<shot>/log.txt -- the plugin's own diagnostics")
    parser.add_argument("--plugin", help="plugin id, to find its lines in --log")
    parser.add_argument("--box", help="x,y,w,h to restrict the comparison to")
    parser.add_argument("--menu-row", metavar="PREFIX",
                        help="the drive picked a plugin menu row starting with PREFIX; "
                             "fail unless the log shows that row was built")
    parser.add_argument("--threshold", type=int, default=40,
                        help="chrome pixels below which the drive is called inert")
    args = parser.parse_args(argv)

    # MENU is asked of the LOG alone, so it is answered before the pixels and
    # without them: a drive whose plugin row was never built has nothing for a
    # picture to be evidence about, and saying so needs no control shot.
    menu_label = None
    if args.menu_row:
        assert args.log, "--menu-row is a question about the run log; give --log"
        if not os.path.exists(args.log):
            print(f"CANNOT COMPARE: no log at {args.log}")
            return 2
        with open(args.log, errors="ignore") as handle:
            log_body = handle.read()
        menu_label = menu_row_built(log_body, args.menu_row)
        declared_off = reveal_key_declared_absent(log_body)
        if menu_label is None and declared_off:
            print(f"{os.path.basename(args.log)}: NO MENU ROW '{args.menu_row}*' "
                  f"DECLARED OFF ({declared_off})")
            print("   The plugin declared this lane cannot answer its reveal key and")
            print("   turned the rows off. The menu WITHOUT them is the evidence for")
            print("   that, not a defect -- the job line says NO_KEYBOARD for the same")
            print("   reason. An UNdeclared absence would still have failed here.")
            return 0
        if menu_label is None:
            print(f"{os.path.basename(args.log)}: NO MENU ROW '{args.menu_row}*' INERT")
            print("   The harness never found a row with that label on an open menu, so")
            print("   nothing it clicked was this plugin's and the shot is evidence about")
            print("   the drive and not the plugin. Check the DRIVE first: did the menu")
            print("   open at all (TORIRS_SIM_CLICK_NPC ... ,1 -- the trailing 1 is the")
            print("   right button), was the state that REVEALS the row reached (a reveal")
            print("   key is held with TORIRS_SIM_KEYHOLD and pressed exactly once, after")
            print("   the title screen), and can this lane answer that key at all?")
            return 1
        print(f"{os.path.basename(args.log)}: menu row {menu_label!r} BUILT")

    if not args.shot or not args.control:
        if menu_label is not None:
            return 0
        print(__doc__)
        return 2

    from PIL import Image as _I
    shot = _I.open(args.shot).convert("RGB")
    control = _I.open(args.control).convert("RGB")
    if shot.size != control.size:
        print(f"CANNOT COMPARE: size {shot.size} vs {control.size}")
        return 2

    if args.box:
        x, y, w, h = (int(v) for v in args.box.split(","))
        box = (x, y, x + w, y + h)
    else:
        box = (0, 0, shot.size[0], shot.size[1])

    drew = diff_in(shot, control, box)
    floor = None
    if args.control2 and os.path.exists(args.control2):
        floor = diff_in(control, _I.open(args.control2).convert("RGB"), box)

    # The second signal, and on a busy lane the DECISIVE one.
    #
    # A whole-frame diff cannot see a small overlay where the scene animates:
    # two no-plugin runs of classic161 differ by 2226 px on their own, which is
    # more than performance-display's entire readout. Scoping with --box fixes
    # that, but only where the region is known. Most plugins here print their
    # own diagnostics, and a plugin that ran and drew says so in the log while
    # one whose drive put nothing in front of it is silent -- which is how the
    # loot-beam lane defect was found (CS2 logged its beam count, CS1 logged
    # nothing at all).
    spoke = None
    drew_owned = None
    if args.log and os.path.exists(args.log):
        with open(args.log, errors="ignore") as handle:
            body = handle.read()
        if args.plugin:
            spoke = f"[{args.plugin}]" in body
        owned, drawn = owned_draws(body)
        if owned:
            drew_owned = drawn

    name = os.path.basename(args.shot)
    emit = "-" if drew_owned is None else str(drew_owned)
    if floor is None:
        print(f"{name:34} differs={drew:6} emit={emit:5} (no --control2: no floor, judge by eye)")
        return 0
    # A plugin has to beat the animation by a clear margin in its own region,
    # not merely exceed it: the floor is itself a sample of a noisy quantity.
    ink = drew > max(floor * 2, floor + args.threshold)
    # EMIT decides in ONE direction and says so in the other. A draw command in
    # the exit list is the frame buffer's own answer and settles it; none is
    # not the mirror of that, because an owned picture is only half of what a
    # plugin can put on screen -- a frame provider also MOVES the lane's own
    # surfaces and RE-SKINS them, and neither leaves a scene id of its own.
    if drew_owned:
        verdict = "EXERCISED"
    elif ink or spoke:
        verdict = "EXERCISED"
    elif spoke is False:
        verdict = "INERT"
    else:
        verdict = "UNCLEAR"
    said = "-" if spoke is None else ("said" if spoke else "silent")
    print(f"{name:34} differs={drew:6} floor={floor:6} log={said:6} emit={emit:5} {verdict}")
    if verdict == "UNCLEAR":
        print("   Ink did not beat this lane's animation and no --log/--plugin was")
        print("   given. Re-run with --box for the plugin's own region, or with")
        print("   --log runs/<shot>/log.txt --plugin <id>. Do not read this as a pass.")
        return 0
    if drew_owned == 0 and verdict == "EXERCISED":
        print("   NOTE: this plugin owns pictures and not one of them is in the exit")
        print("   draw list, so whatever reached the screen was its moves and skins")
        print("   rather than its own art. Read the picture before believing it.")
    if verdict == "INERT":
        print("   Nothing this plugin drew beat the animation in its own region.")
        print("   Before reading the picture as a defect, check the DRIVE: did the")
        print("   pointer land on something with the property, was the provider")
        print("   switched on (preferred_frame, not enabled), is the config")
        print("   default 'off', did the command fire inside the frame budget?")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
