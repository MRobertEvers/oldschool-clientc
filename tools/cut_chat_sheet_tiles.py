#!/usr/bin/env python3
"""
Cut the Stone Drawer's torn parchment into a set of pieces that TILE.

    python3 tools/cut_chat_sheet_tiles.py

`chat_sheet_rs289.png` is one 517x130 picture of a torn sheet, and the frame
drew it by SCALING it to whatever box the chatbox had.  That is fine while the
box is the size the sheet was drawn at and wrong the moment it is not: scaling
a torn edge stretches the tears, so a wide chatbox gets long smeared fingers of
paper along its top and a tall one gets them down its sides.  The fringe is the
one part of this picture that cannot be resampled -- its whole job is to look
like an edge that was TORN, and a torn edge has a grain size.

So the sheet becomes a nine-patch whose edges and middle are tiled rather than
stretched: the four corners are cut once and never resized, the four edges
repeat along their own axis, and the middle repeats in both.  The grain stays
the grain at every size.

The pieces are cut FROM the sheet, not drawn: everything below is a rectangle
of `chat_sheet_rs289.png` and, for the right edge, a mirrored one.  What this
script contributes is WHERE to cut, and that is the whole difficulty -- an edge
tile has to butt against a copy of itself without a notch, so the cut has to
land where the tear happens to be at the same depth twice.  `wrap_cost` scores
a candidate (start, period) by how much the four columns at `start` differ from
the four at `start + period` -- alpha weighted six times the colour, because a
one-pixel alpha step in a silhouette reads as a nick and a one-level colour
step does not -- and the cut is the argmin over every pair in range.

The right edge is the one piece not cut in place.  Its tear DRIFTS: the
silhouette runs from x=508 at the top to x=496 in the middle and back to 505,
so no two rows of it are at the same depth and no period wraps (the best is
~37, against 1.7 for the left).  A drifting edge is not a tiling edge, so the
right strip is the left strip mirrored -- the same tear read the other way,
which is what a tear looks like from the other side of a sheet anyway.  The
right CORNERS are still cut in place, so the sheet keeps its own distinctive
right-hand bite where it is largest.

The numbers this prints are the ones `mobile_gameframe.c` has to agree with:
the corner box is what MOBILE_PAPER_CORNER_W/H say, and a recut with different
ones is a sheet composed with the wrong insets.
"""

import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "script/plugins/assets/mobile-gameframe/chat_sheet_rs289.png")
OUT = os.path.join(ROOT, "script/plugins/assets/mobile-gameframe")

# The corner box.  Both are larger than the fringe they have to contain (17
# columns left, 21 right, 17 rows top and bottom) so that a corner is a corner
# and not a lone strip of tear: what is left over is core, and it is the core
# that makes the piece read as the sheet's own angle rather than as an edge
# that happens to stop.
CORNER_W = 48
CORNER_H = 26


def wrap_cost(band, axis, start, period):
    """How badly `start + period` butts against `start`.  Lower is a cleaner tile."""
    if axis == "x":
        a, b = band[:, start : start + 4], band[:, start + period : start + period + 4]
    else:
        a, b = band[start : start + 4, :], band[start + period : start + period + 4, :]
    if a.shape != b.shape:
        return float("inf")
    d = a.astype(float) - b.astype(float)
    return float(np.abs(d[..., :3]).mean() + 6.0 * np.abs(d[..., 3]).mean())


def best_wrap(band, axis, lo, hi, pmin, pmax):
    """The (start, period) in [lo, hi) whose wrap seam is the least visible."""
    best = None
    for period in range(pmin, pmax + 1):
        for start in range(lo, hi - period + 1):
            cost = wrap_cost(band, axis, start, period)
            if best is None or cost < best[0]:
                best = (cost, start, period)
    assert best is not None, "no candidate period fits the range"
    return best


# The middle tile.  It repeats across the whole sheet, so its period is the
# thing the eye gets the most chances to find, and the sheet is only 130 rows
# tall -- everything below has to come out of a 94-row band of core with a
# feather's worth spare at each end.
FILL_W = 320
FILL_H = 60
FILL_FEATHER = 16
# How far a wash has to spread before the tile stops carrying it.
FILL_DETREND_RADIUS = 12

# The core, as the ORIGINAL sheet measures it: (17,17)-(495,112) is the maximal
# fully-opaque rectangle in `chat_sheet_rs289.png`, and one column inside that
# is the closest the middle tile may come to a tear.
CORE_L, CORE_T, CORE_R, CORE_B = 18, 18, 495, 112


def flattest_patch(core, pw, ph):
    """Where in the core the paper is least blotchy.

    The tile is going to repeat, and what repeats visibly is not the grain --
    which is noise, and reads as paper at any period -- but the BLOTCHES, the
    slow light and dark washes across the sheet.  One of those caught in a tile
    becomes a row of identical stains.  So the patch is scored on its
    low-frequency content alone: the spread of its 8x8 block means, which
    averages the grain away and sees only the wash.
    """
    grey = core[..., :3].mean(2)
    h, w = grey.shape
    best = None
    for y in range(0, h - ph + 1, 2):
        for x in range(0, w - pw + 1, 4):
            blocks = grey[y : y + ph, x : x + pw].reshape(ph // 4, 4, pw // 4, 4)
            spread = float(blocks.mean((1, 3)).std())
            if best is None or spread < best[0]:
                best = (spread, x, y)
    assert best is not None, "the core is smaller than the fill patch"
    return best


def box_blur(a, radius):
    """A separable box blur, which is all the low-pass this needs to be."""
    pad = np.pad(a, ((radius, radius), (radius, radius), (0, 0)), mode="edge")
    k = 2 * radius + 1
    acc = np.cumsum(pad, axis=0)
    acc = np.concatenate([acc[:1] * 0, acc], axis=0)
    a = (acc[k:] - acc[:-k]) / k
    acc = np.cumsum(a, axis=1)
    acc = np.concatenate([acc[:, :1] * 0, acc], axis=1)
    return (acc[:, k:] - acc[:, :-k]) / k


def detrend(patch, target, radius):
    """Take the WASH out of a patch and leave the grain, at a stated tone.

    What repeats visibly in a tiled texture is never the grain -- grain is
    noise, and noise reads as paper at any period -- it is the slow light and
    dark washes across the sheet, which come back every `FILL_W` pixels as a
    row of identical stains.  Subtracting a heavy blur removes exactly those
    and keeps everything finer than `radius`.

    The same subtraction is what makes the middle MEET the edges: with the wash
    gone there is a single tone left to choose, and it is set to the tone of
    the core rows and columns the edge strips end on, so the nine-patch has no
    step across its inner joins.  A patch left at its own mean is a couple of
    levels off whichever part of the sheet it was cut from, and two levels
    across a straight line is a visible panel outline on paper this flat.
    """
    wash = box_blur(patch.astype(float), radius)
    return patch.astype(float) - wash + target


def feather_wrap(source, pw, ph, feather):
    """Make a patch tile by fading each far edge back into what preceded it.

    The edges of the sheet were cut where the tear happened to repeat, because
    a tear is a shape and a shape cannot be dissolved into another one without
    turning to mush.  The middle has no shape -- it is grain -- so it gets the
    construction that needs no luck: `source` is the tile plus a feather's
    margin all round, and the tile's last `feather` columns are cross-faded
    into the `feather` columns that come immediately BEFORE the tile started.
    By the last column the fade is complete, so column pw-1 and column 0 are
    neighbours in the original sheet and butt together as they always did.

    Mirroring would also tile exactly, and was tried: a mirror is invisible in
    grain and unmissable in a wash, and it quilted the sheet with diamonds.
    """
    assert source.shape[0] >= ph + 2 * feather
    assert source.shape[1] >= pw + 2 * feather
    tile = source[feather : feather + ph, feather : feather + pw].astype(float).copy()
    ramp = (np.arange(feather) + 1.0) / feather
    for k in range(feather):
        t = ramp[k]
        tile[:, pw - feather + k] *= 1.0 - t
        tile[:, pw - feather + k] += t * source[feather : feather + ph, k]
    for k in range(feather):
        t = ramp[k]
        tile[ph - feather + k, : pw - feather] *= 1.0 - t
        tile[ph - feather + k, : pw - feather] += t * source[k, feather : pw]
    return np.clip(np.rint(tile), 0, 255)


def save(name, pixels):
    path = os.path.join(OUT, name)
    Image.fromarray(pixels.astype(np.uint8), "RGBA").save(path)
    print("  %-28s %3dx%-3d" % (name, pixels.shape[1], pixels.shape[0]))
    return path


def main():
    im = np.array(Image.open(SRC).convert("RGBA")).astype(np.int32)
    h, w, _ = im.shape
    print("%s  %dx%d" % (os.path.basename(SRC), w, h))

    # The top and bottom edges tile along x, cut from the run between the
    # corners.  A long period is wanted for its own sake -- it is how many
    # pixels of tear go by before the eye can catch the repeat -- so the search
    # is given a wide band and the winner is whichever long period happens to
    # wrap cleanly, not the shortest one that does.
    top_cost, top_x, top_p = best_wrap(
        im[0:CORNER_H, :, :], "x", CORNER_W, w - CORNER_W, 128, 256
    )
    bot_cost, bot_x, bot_p = best_wrap(
        im[h - CORNER_H : h, :, :], "x", CORNER_W, w - CORNER_W, 128, 256
    )
    # The left edge tiles along y, and there is far less of it to choose from:
    # the sheet is 130 rows tall against 517 wide, so the period is a few tens
    # of pixels rather than a couple of hundred.  A narrow ragged band repeating
    # every forty rows is not something the eye picks out; the same period on
    # the top edge would be.
    left_cost, left_y, left_p = best_wrap(
        im[:, 0:CORNER_W, :], "y", CORNER_H, h - CORNER_H, 32, 60
    )

    print("seams (alpha-weighted; 0 is a perfect wrap)")
    print("  top     start %3d period %3d  cost %5.2f" % (top_x, top_p, top_cost))
    print("  bottom  start %3d period %3d  cost %5.2f" % (bot_x, bot_p, bot_cost))
    print("  left    start %3d period %3d  cost %5.2f" % (left_y, left_p, left_cost))

    # The middle, from the core -- and from well inside it, because a middle
    # tile with one pixel of tear in it stamps that pixel across the sheet.
    core = im[CORE_T:CORE_B, CORE_L:CORE_R, :]
    blot, fx, fy = flattest_patch(
        core, FILL_W + 2 * FILL_FEATHER, FILL_H + 2 * FILL_FEATHER
    )
    # The tone the middle has to arrive at: what the four edge strips END on,
    # measured over the last four rows and columns of core each one carries.
    joins = np.concatenate(
        [
            im[CORNER_H - 4 : CORNER_H, CORNER_W : w - CORNER_W, :3].reshape(-1, 3),
            im[h - CORNER_H : h - CORNER_H + 4, CORNER_W : w - CORNER_W, :3].reshape(-1, 3),
            im[CORNER_H : h - CORNER_H, CORNER_W - 4 : CORNER_W, :3].reshape(-1, 3),
        ]
    )
    target = np.zeros(4)
    target[:3] = joins.mean(0)
    target[3] = 255.0
    patch = core[
        fy : fy + FILL_H + 2 * FILL_FEATHER,
        fx : fx + FILL_W + 2 * FILL_FEATHER,
    ]
    fill = feather_wrap(
        detrend(patch, target, FILL_DETREND_RADIUS), FILL_W, FILL_H, FILL_FEATHER
    )
    print(
        "  fill    %3dx%-3d at core (%d,%d), feather %d, tone %s  blotch %4.2f"
        % (FILL_W, FILL_H, fx, fy, FILL_FEATHER, np.round(target[:3]).astype(int), blot)
    )

    print("pieces")
    save("chat_paper_tl.png", im[0:CORNER_H, 0:CORNER_W])
    save("chat_paper_tr.png", im[0:CORNER_H, w - CORNER_W : w])
    save("chat_paper_bl.png", im[h - CORNER_H : h, 0:CORNER_W])
    save("chat_paper_br.png", im[h - CORNER_H : h, w - CORNER_W : w])
    save("chat_paper_top.png", im[0:CORNER_H, top_x : top_x + top_p])
    save("chat_paper_bottom.png", im[h - CORNER_H : h, bot_x : bot_x + bot_p])
    left = im[left_y : left_y + left_p, 0:CORNER_W]
    save("chat_paper_left.png", left)
    # Mirrored, not cut: the sheet's own right edge drifts and will not wrap.
    save("chat_paper_right.png", left[:, ::-1])
    save("chat_paper_fill.png", fill)

    print(
        "\nmobile_gameframe.c must agree: corner %dx%d" % (CORNER_W, CORNER_H)
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
