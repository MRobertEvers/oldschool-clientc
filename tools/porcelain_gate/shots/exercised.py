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

Five separate drives in this set were inert and every one was caught downstream
by somebody reading pixels, days late:

  * gf-mobile-*   photographed a different provider entirely -- preferred_frame
                  is the master switch for a frame provider, not `enabled`.
  * screenshot-*  drew nothing: the plugin's `camera` config defaults to "off".
  * nxthl-*       reused 548's hover pixel on every toplevel; it lands 99-110px
                  from the npc on the others.
  * itemstats-*   hovered inventory slot 0, which holds runes -- no stats, so
                  correctly no tooltip.
  * xporbs-*      awarded xp on the very frame the run ended.

They have nothing in common at the knob level, which is why remembering them
one at a time does not work. They have everything in common HERE: the shot is
indistinguishable from the same lane with the plugin absent.

    python3 exercised.py <shot.png> --control <control.png> [--box x,y,w,h]
    python3 exercised.py --jobs jobs/cs2_toplevels.txt --shots plugins/

Two signals, because neither is sufficient alone:

  INK   the shot against a no-plugin control, inside a box, against a floor
        measured from a SECOND control in that same box. Sound when --box names
        the plugin's own region; whole-frame it is dominated by scene animation
        (two no-plugin runs differ by 532 px on classic548 but 2226 on
        classic161 and 3669 on stone601 -- more than a small overlay draws).
  LOG   whether the plugin printed anything of its own. Decisive where a plugin
        has diagnostics: that asymmetry is how the loot-beam lane defect was
        found, CS2 logging its beam count and CS1 logging nothing.

WHAT THIS CANNOT DO, stated because a check that overclaims is worse than none:
performance-display, tile-indicator-c, nxt-highlight and screenshot print NO
diagnostic line, so on a busy lane, whole-frame, neither signal decides and the
verdict is UNCLEAR. That is a real state and NOT a pass -- re-run with --box for
that plugin's region. Supplying per-plugin boxes is the completion of this tool
and is not done yet.
"""

import argparse
import os
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


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("shot", nargs="?")
    parser.add_argument("--control", help="the same lane with no plugins")
    parser.add_argument("--control2", help="a SECOND no-plugin run, for the local animation floor")
    parser.add_argument("--log", help="runs/<shot>/log.txt -- the plugin's own diagnostics")
    parser.add_argument("--plugin", help="plugin id, to find its lines in --log")
    parser.add_argument("--box", help="x,y,w,h to restrict the comparison to")
    parser.add_argument("--threshold", type=int, default=40,
                        help="chrome pixels below which the drive is called inert")
    args = parser.parse_args(argv)

    if not args.shot or not args.control:
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
    if args.log and args.plugin and os.path.exists(args.log):
        with open(args.log, errors="ignore") as handle:
            body = handle.read()
        spoke = f"[{args.plugin}]" in body

    name = os.path.basename(args.shot)
    if floor is None:
        print(f"{name:34} differs={drew:6}  (no --control2: no floor, judge by eye)")
        return 0
    # A plugin has to beat the animation by a clear margin in its own region,
    # not merely exceed it: the floor is itself a sample of a noisy quantity.
    ink = drew > max(floor * 2, floor + args.threshold)
    if ink or spoke:
        verdict = "EXERCISED"
    elif spoke is False:
        verdict = "INERT"
    else:
        verdict = "UNCLEAR"
    said = "-" if spoke is None else ("said" if spoke else "silent")
    print(f"{name:34} differs={drew:6} floor={floor:6} log={said:6} {verdict}")
    if verdict == "UNCLEAR":
        print("   Ink did not beat this lane's animation and no --log/--plugin was")
        print("   given. Re-run with --box for the plugin's own region, or with")
        print("   --log runs/<shot>/log.txt --plugin <id>. Do not read this as a pass.")
        return 0
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
