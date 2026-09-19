#!/usr/bin/env python3
"""Cut the mandatory crop set from a screenshot, so LOOKING is one command.

This exists because of a specific, expensive failure. Twenty plugins were
declared "visually verified on both lanes" and that went into a pull request.
A per-plugin pass then found 42 defects across 17 of those 20 -- among them a
plugin that drew NOTHING AT ALL, and a settings dropdown that opened downward
off the bottom of the window with 8 of its 9 options unreachable.

Nothing exotic was missed. The screenshots were opened at full size and
scanned whole, and the numbers around them -- OWNED_WIDGET counts, BOUNDS
counts, finding counts, pixel-diff percentages -- were read as if they were
evidence about the picture. They are not. They answer "did something change".
A 765x503 frame viewed whole cannot show a torn edge cut mid-tear, a progress
bar eating the last row of an icon, a 1px border overpainted, or a list that
runs past the window. Every one of those was found by cropping at 3-4x, and
none of them by looking at the whole frame.

So the crops are not a suggestion and they are not per-taste. This script cuts
the same regions every time, writes them next to a manifest, and prints the
list. The manifest is the point: it is the artifact that makes a skipped
inspection visible afterwards. There is no verbal equivalent of having looked.

    python3 inspect.py <shot.png> [more.png ...] [--plugin-box x,y,w,h]
                       [--out DIR] [--scale N]

Then OPEN EVERY FILE IT WRITES. Writing them is not inspecting them.

Three things this cannot do for you, each of which has already cost a defect:

  * Read the plugin's source first. Without knowing what it is meant to draw,
    "drew nothing because a config said no" and "drew nothing because it is
    broken" are the same picture.
  * Read runs/<shot>/log.txt. A plugin's own diagnostic line present on one
    lane and absent on the other is a defect even when the picture looks
    plausible. That is exactly how the loot-beam lane bug announced itself.
  * Confirm the shot exercised the plugin at all. Five shipped shots proved
    nothing: a hover over an item with no stats to show, XP globes caught after
    they had expired, a highlighter tagging CS2 npc ids on a LostCity lane, a
    frame provider that was never switched on (preferred_frame is the switch,
    not `enabled`), and a stale pre-fix capture.
"""

import argparse
import json
import os
import sys

from PIL import Image

# Fixed regions, in the order a reader should open them. Boxes are clamped to
# the image, so the same list serves 765x503 (CS1), 807x503 (CS2) and the
# taller touch frames without a per-lane table.
REGIONS = [
    ("chat",     (0, -115, 560, None), "chat band: pane, filter row, and the frame edge under it"),
    ("orbs",     (505, 0, 640, 200),   "minimap and orb column"),
    ("sidebar",  (505, 150, None, None), "sidebar: tabs, inventory, the panel well"),
    ("top",      (0, 0, None, 60),     "top strip: overlays that draw at the canvas ceiling"),
    ("left",     (0, 0, 260, None),    "left edge: readouts and anything anchored to it"),
    ("floor",    (0, -45, None, None), "the last rows: where a clipped piece shows"),
]


def cut(image, box):
    """Clamp a (x, y, w_or_x2, h_or_y2)-ish spec to the image and crop it.

    Negative y means "this many rows up from the bottom"; None means "to the
    edge". Written this way so one region table covers every canvas size.
    """
    width, height = image.size
    x, y, right, bottom = box
    if y < 0:
        y = max(0, height + y)
    right = width if right is None else min(right, width)
    bottom = height if bottom is None else min(bottom, height)
    x, y = max(0, x), max(0, y)
    if right <= x or bottom <= y:
        return None
    return image.crop((x, y, right, bottom))


def inspect(path, out_dir, scale, plugin_box):
    image = Image.open(path).convert("RGB")
    stem = os.path.splitext(os.path.basename(path))[0]
    regions = list(REGIONS)
    if plugin_box:
        x, y, w, h = plugin_box
        regions.append(("plugin", (x, y, x + w, y + h),
                        "the region THIS plugin draws in"))

    written = []
    for name, box, why in regions:
        crop = cut(image, box)
        if crop is None:
            continue
        crop = crop.resize((crop.width * scale, crop.height * scale), Image.NEAREST)
        dest = os.path.join(out_dir, f"{stem}.{name}.png")
        crop.save(dest)
        written.append({"region": name, "path": dest, "why": why,
                        "size": f"{crop.width}x{crop.height}"})
    return {"shot": path, "size": f"{image.size[0]}x{image.size[1]}", "crops": written}


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("shots", nargs="+")
    parser.add_argument("--out", default="/tmp/inspect")
    parser.add_argument("--scale", type=int, default=3)
    parser.add_argument("--plugin-box", default=None,
                        help="x,y,w,h of the region this plugin draws in")
    args = parser.parse_args(argv)

    plugin_box = None
    if args.plugin_box:
        plugin_box = tuple(int(v) for v in args.plugin_box.split(","))
        assert len(plugin_box) == 4, "--plugin-box wants x,y,w,h"

    os.makedirs(args.out, exist_ok=True)
    report = [inspect(s, args.out, args.scale, plugin_box) for s in args.shots]

    manifest = os.path.join(args.out, "manifest.json")
    with open(manifest, "w") as handle:
        json.dump(report, handle, indent=2)

    total = 0
    for entry in report:
        print(f"\n{entry['shot']}  ({entry['size']})")
        for crop in entry["crops"]:
            total += 1
            print(f"   {crop['region']:8} {crop['size']:12} {crop['path']}")
            print(f"            {crop['why']}")
    print(f"\n{total} crops -> {manifest}")
    print("OPEN EVERY ONE. Writing them is not inspecting them.")
    print("Then: read the plugin's source, read runs/<shot>/log.txt, and confirm")
    print("the shot actually exercised the plugin before judging what you see.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
