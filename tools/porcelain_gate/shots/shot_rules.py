#!/usr/bin/env python3
"""What a tracked capture has to be TRUE of, measured rather than remembered.

A capture in `plugins/` is read as a statement about how the client behaves
now.  Nothing made that true.  A shot is a file, a fix lands in `src/`, and the
file goes on showing the repaired bug until somebody re-takes it -- at which
point the picture is evidence FOR a defect that no longer exists, and the next
reader opens it and re-reports what was fixed a day earlier.

That is not hypothetical and it is not rare.  `nxthl-tile-cs2.png` and its
control were taken at 21:31 and 22:02; `190648c80` (the tile border is the
cache's thickness, and 0 is no border) landed at 22:32 and `74fc1f13f` (the
parked pointer stopped costing a session) at 22:35.  Neither re-took them, so
for a day the headline picture of nxt-highlight showed a hard opaque rim around
a group whose thickness is zero, over a torn-down session -- two fixed bugs,
photographed as though they were live, and re-reported as NXTHL-TILE-CS2-
STALE-PREFIX-548.

So the measurement that closed a defect becomes a ROW here.  The row is cheap,
it runs in a second, and it fails in both directions that matter:

  * the pixel comes back  -- the fix regressed;
  * the pixel never comes -- the shot is stale, or was taken with the plugin
    off, or the drive stopped exercising it.

Every rule is stated against the CACHE's own numbers, which is what makes it a
rule and not a hash of a picture.  `NATIVE_HIGHLIGHT kind=7 group=5
colour=beba6e outline=0 opacity=70 flags=10` says: a wash at 70/255 and NO
border.  The two rules that follow are exactly those two halves, and they are
both needed -- a plugin that drew nothing at all satisfies "no border" and
fails "the wash is there", which is the whole reason the positive rule is
written down next to the negative one.

    python3 shot_rules.py              # every rule, fails on the first miss
    python3 shot_rules.py --list       # what is pinned, and why
"""

import argparse
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGINS = os.path.join(HERE, "plugins")

# The hovered-tile group, read off the run log rather than guessed:
#   NATIVE_HIGHLIGHT kind=7 group=5 colour=beba6e outline=0 opacity=70 flags=10
HOVER_TILE_RGB = (0xBE, 0xBA, 0x6E)
# The lane's ground under that tile, and the same ground washed at 70/255:
#   74 + (190 - 74) * 70 / 255 = 105, and so on per channel.
LANE_GROUND = (74, 72, 67)
HOVER_TILE_WASHED = (105, 103, 78)
# One pixel INSIDE the hovered tile (x289..363 y183..212 on the CS2 lane), far
# enough from every edge that a one-pixel difference in the projection cannot
# move it off the face.
HOVER_TILE_INSIDE = (293, 200)

# The viewport band the teardown banner's text crossed.  White is the right
# probe because the world raster never produces it: the 3D pass shades every
# face, so a pure 255/255/255 run here is UI text over the scene, and the only
# UI text that lands there is "Connection lost / Attempting to reestablish".
VIEWPORT_BAND = (0, 150, 505, 220)


def rule_pixel(image, x, y, rgb):
    """This pixel is exactly this colour."""
    got = image.getpixel((x, y))[:3]
    if got == tuple(rgb):
        return None
    return "pixel (%d,%d) is %s, want %s" % (x, y, got, tuple(rgb))


def rule_colour_absent(image, rgb, box=None):
    """No pixel in `box` (default: the whole frame) is exactly this colour."""
    region = image if box is None else image.crop(box)
    want = tuple(rgb)
    hits = [
        (x, y)
        for y in range(region.height)
        for x in range(region.width)
        if region.getpixel((x, y))[:3] == want
    ]
    if not hits:
        return None
    origin = (0, 0) if box is None else (box[0], box[1])
    first = [(x + origin[0], y + origin[1]) for x, y in hits[:6]]
    return "%d pixels are exactly %s; first %s" % (len(hits), want, first)


# shot -> list of (rule, kwargs, why).  `why` is the sentence a reader gets
# when the rule fails, so it says what the pixel MEANS, never what it is.
RULES = {
    "nxthl-tile-cs2.png": [
        (
            rule_colour_absent,
            dict(rgb=HOVER_TILE_RGB),
            "the hovered-tile group states outline=0, so no pixel anywhere may be "
            "its colour at full strength. 389 of them were the rim 190648c80 "
            "removed; a capture with any is pre-fix or a regression",
        ),
        (
            rule_pixel,
            dict(x=HOVER_TILE_INSIDE[0], y=HOVER_TILE_INSIDE[1], rgb=HOVER_TILE_WASHED),
            "and the WASH survives: the same group states opacity=70, so the tile's "
            "interior is the lane's ground blended 70/255 toward beba6e. Without "
            "this, a plugin that drew nothing at all would satisfy the rule above",
        ),
        (
            rule_colour_absent,
            dict(rgb=(255, 255, 255), box=VIEWPORT_BAND),
            "and the session is ALIVE: pure white across the viewport is the "
            "'Connection lost' banner, which the parked-pointer teardown put over "
            "every TORIRS_SIM_HOVER capture until 74fc1f13f. 649 such pixels in "
            "the pre-fix pair, none in any capture since",
        ),
    ],
    "nxthl-tile-none-cs2.png": [
        (
            rule_pixel,
            dict(x=HOVER_TILE_INSIDE[0], y=HOVER_TILE_INSIDE[1], rgb=LANE_GROUND),
            "the control is the same drive with nxt-highlight OFF, so this pixel is "
            "bare ground. It is what makes the shot's washed value the PLUGIN's "
            "doing rather than the lane's: a control that is already washed cannot "
            "tell the two apart",
        ),
        (
            rule_colour_absent,
            dict(rgb=(255, 255, 255), box=VIEWPORT_BAND),
            "and the control was taken against a live session too -- a torn-down "
            "control is not a control, it is a second picture of the teardown",
        ),
    ],
}


def check(shot, rules):
    path = os.path.join(PLUGINS, shot)
    # A rule whose capture is gone is a FAILURE, never a skip: "the file moved"
    # and "the file is fine" must not print the same thing.
    assert os.path.exists(path), path
    image = Image.open(path).convert("RGB")
    failures = []
    for rule, kwargs, why in rules:
        problem = rule(image, **kwargs)
        if problem is not None:
            failures.append((rule.__name__, problem, why))
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    if args.list:
        for shot, rules in sorted(RULES.items()):
            print(shot)
            for rule, kwargs, why in rules:
                print("    %-20s %s" % (rule.__name__, why))
        return 0

    bad = 0
    total = 0
    for shot, rules in sorted(RULES.items()):
        total += len(rules)
        failures = check(shot, rules)
        for name, problem, why in failures:
            bad += 1
            print("FAIL %s: %s" % (shot, problem))
            print("     %s" % why)
    if bad:
        print()
        print("shot rules: FAIL -- %d of %d" % (bad, total))
        print("Re-take the capture, or repair what moved. A rule here is a "
              "measurement that closed a defect.")
        return 1
    print("shot rules: PASS -- %d rules over %d captures" % (total, len(RULES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
