#!/usr/bin/env python3
"""vs_control.py <control.png> <shot.png> [shot.png ...]

How many pixels this plugin is responsible for, and where they are.

A picture of a plugin that draws nothing and a picture of a plugin whose drive
never fired look identical, and both look like the frame. Differencing against
the lane's own control -- the same lane with no optional plugin at all --
separates them: zero changed pixels means the plugin contributed nothing to
this frame, and the bounding box says where what it did contribute went.

The world animates (water, torches, the player's idle) and the readouts tick,
so a small count is not proof of anything on its own; the box is what to read.
"""
import pathlib
import sys
from PIL import Image, ImageChops


def main():
    control = Image.open(sys.argv[1]).convert("RGB")
    for path in sys.argv[2:]:
        shot = Image.open(path).convert("RGB")
        name = pathlib.Path(path).name
        if shot.size != control.size:
            print(f"{name:28} size {shot.size} != control {control.size}")
            continue
        diff = ImageChops.difference(shot, control).convert("L")
        box = diff.point(lambda v: 255 if v > 24 else 0).getbbox()
        count = sum(1 for v in diff.getdata() if v > 24)
        print(f"{name:28} {count:7d} px  box={box}")


main()
