#!/usr/bin/env python3
"""well_ink.py <shot.png> [shot.png ...]

Is the Loot Tracker's well painted, or is it an empty box?

The well is a PORCELAIN_ROW_CUSTOM strip at region 21,145,294,77 (the log says
so in EVERY run, drawn or blank -- which is the point: the widget is staged
either way and the difference is entirely in what its paint put inside). A
painted well carries the orange heading "No loot to display." and the white
totals; a blank one is flat fill. Counting ink inside that rectangle separates
them without anybody having to look at sixty pictures.
"""
import pathlib
import sys
from PIL import Image

WELL = (21, 145, 21 + 294, 145 + 77)


def main():
    for path in sys.argv[1:]:
        im = Image.open(path).convert("RGB").crop(WELL)
        px = list(im.getdata())
        # The well's own fill is a dark, near-neutral brown. Ink is anything
        # markedly brighter (white totals) or markedly saturated (orange head).
        ink = sum(1 for r, g, b in px
                  if (r + g + b) > 330 or (max(r, g, b) - min(r, g, b)) > 70)
        print(f"{pathlib.Path(path).name:28} ink={ink:6d}  "
              f"{'PAINTED' if ink > 400 else 'BLANK'}")


main()
