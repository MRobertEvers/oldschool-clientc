#!/usr/bin/env python3
"""
Cut the 2004 frame's fourteen LIT tab stones, one PNG per stone position.

Run by SOURCES.sh after the dat1 sprites are dumped; it reads only files that
script has just written beside it.

    classic_tabstone_<i>_red.png  the redstone Client-TS plots when the tab at
                                  screen position <i> is selected, turned the
                                  way it turns it, as a cut-out on a 37-row
                                  cell of its band's hollow row.

<i> is the SCREEN position -- 0..6 along the top band left to right, 7..13
along the bottom -- not a panel number.

Nine of the fourteen are flips of three sprites, and the client flips them at
load. Shipping them turned means the plugin places one picture per position
at one origin and composes nothing.

The geometry is Client-TS's (src/client/Client.ts): each redstone's plotSprite
origin and flip, on `backhmid1` at 516,160 and `backbase2` at 496,466. I checked
it against real renders of the dat1 lane's native frame with each of the
thirteen selectable tabs pressed: the band plus the redstone at these origins
matched the render on every pixel the tab icon does not cover. The fourteenth
(bottom-left) is the 2004 frame's unused slot, and the client draws the top-left
stone turned over for it.

Each cell starts on its band's hollow row (y 168 for the top band, 466 for the
bottom) with the stone at its own row inside it. The frame places every cell at
(stone x, hollow row). The top-left redstone's last row falls below the band;
this script clips it, the same way the band does.
"""
import os
import sys

from PIL import Image, ImageOps

HERE = os.path.dirname(os.path.abspath(__file__))

# name -> (band sprite, band origin on the 765x503 canvas, hollow row top)
BAND = {
    "top": ("classic_backhmid1.png", (516, 160), 168),
    "bottom": ("classic_backbase2.png", (496, 466), 466),
}
ROWS = 37

# position -> (band, redstone 1|2|3, flip, redstone origin on the canvas)
STONE = [
    ("top", 1, "", (538, 170)),
    ("top", 2, "", (570, 168)),
    ("top", 2, "", (598, 168)),
    ("top", 3, "", (626, 168)),
    ("top", 2, "h", (669, 168)),
    ("top", 2, "h", (697, 168)),
    ("top", 1, "h", (725, 169)),
    ("bottom", 1, "v", (538, 466)),
    ("bottom", 2, "v", (570, 466)),
    ("bottom", 2, "v", (598, 466)),
    ("bottom", 3, "v", (626, 467)),
    ("bottom", 2, "hv", (669, 466)),
    ("bottom", 2, "hv", (697, 466)),
    ("bottom", 1, "hv", (725, 466)),
]


def load(name):
    path = os.path.join(HERE, name)
    if not os.path.exists(path):
        sys.exit(f"cut_tab_stones: {name} is missing -- run SOURCES.sh, which dumps it first")
    return Image.open(path).convert("RGBA")


def redstone(stone, flip):
    red = load(f"classic_redstone{stone}.png")
    if "h" in flip:
        red = ImageOps.mirror(red)
    if "v" in flip:
        red = ImageOps.flip(red)
    return red


def main():
    for i, (band_name, stone, flip, (sx, sy)) in enumerate(STONE):
        _, _, top = BAND[band_name]
        red = redstone(stone, flip)
        red_px = red.load()
        dy = sy - top

        lit = Image.new("RGBA", (red.width, ROWS), (0, 0, 0, 0))
        lit_px = lit.load()
        for y in range(red.height):
            if not 0 <= dy + y < ROWS:
                continue
            for x in range(red.width):
                if red_px[x, y][3] != 0:
                    lit_px[x, dy + y] = red_px[x, y]
        lit.save(os.path.join(HERE, f"classic_tabstone_{i}_red.png"))


if __name__ == "__main__":
    main()
