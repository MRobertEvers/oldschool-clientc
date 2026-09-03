#!/usr/bin/env python3
"""
Cut the Stone Drawer's torn parchment into pieces that TILE, with several
real variants per repeating piece so the pattern does not read as a stamp.

    python3 tools/cut_chat_sheet_tiles.py

`chat_sheet_rs289.png` is one 517x130 picture of a torn sheet, and the frame
draws it as a nine-patch: four corners cut once, four edges that repeat along
their own axis, and a middle that repeats both ways. @see mobile_compose_paper
in src/plugin/plugins/mobile_gameframe.c, and this file's own first commit for
why a nine-patch at all -- a scaled tear stops looking like a tear.

A SINGLE repeating tile is still a stamp: lay the same 174-column strip end to
end across a 900-wide sheet and the eye catches the interval before it catches
anything else about the paper. So each repeating position gets several real,
DISTINCT variants cut from different parts of the source, and the plugin picks
between them per repeat with a cheap position hash -- deterministic (the sheet
does not shimmer between two draws of the same box), but not one fixed cycle
(an A-B-C-A-B-C round-robin is its own kind of stamp).

Two different techniques back the two different kinds of repeating piece,
because they fail differently:

  EDGE variants (top, bottom, left -- right is the left variants mirrored) are
  a SHAPE, the tear's silhouette, and a shape survives no blending -- so each
  variant is a distinct wrapping (start, period) pair, found by the same
  alpha-weighted search as the original single tile, but with the period
  FIXED to the best one found and only the start position varying. Fixing the
  period is what keeps every variant of one edge the same size, which is what
  lets the tiling loop step by a constant regardless of which variant lands in
  a given cell.

  The FILL variant is grain and has no shape to lose, so it is built the way
  the original single fill was: a low-blotch patch, detrended (a heavy blur
  subtracted) to a shared target tone so every variant sits at the same shade,
  then feather-wrapped so each tiles against itself. Multiple variants come
  from disjoint patches of the core, chosen by the same flattest-patch search
  run repeatedly against a shrinking search area.

Corners get none of this and stay exactly four: a corner is the one piece that
is not repeated, so there is no stamping to break up, and there is only one
real top-left tear in the source to cut it from -- a second "variant" would
have to be invented rather than found, which is the one thing this cutter does
not do to art it did not draw.

`--proof <dir>` composes a sheet at a few sizes with a fixed deterministic
selection (so the output is reproducible) and prints the fringe it measures
back, which is the number `MOBILE_PAPER_FRINGE_*` in mobile_gameframe.c has to
agree with.
"""

import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "script/plugins/assets/mobile-gameframe/chat_sheet_rs289.png")
OUT = os.path.join(ROOT, "script/plugins/assets/mobile-gameframe")

# The corner box. Larger than the fringe it has to contain (17 columns left,
# 21 right, 17 rows top and bottom) so a corner reads as a corner and not as a
# lone strip of tear -- what is left over is core, and the core is what makes
# the piece read as the sheet's own angle. Unlike every other piece, a corner
# gets no variants: it is the only one that is not repeated, so there is
# nothing here for a second copy to break up, and there is exactly one real
# top-left tear in the source to cut it from.
CORNER_W = 48
CORNER_H = 26

# The MOST real, distinct pieces the cutter will look for at each repeating
# position. A ceiling on ambition, not a promise -- the search answers with
# however many pass its quality gate, which is sometimes fewer.
EDGE_VARIANTS_MAX = 3
FILL_VARIANTS = 3
# Below this alpha-weighted seam cost a wrap is not a visible one. @see
# wrap_variants -- past it, a piece is left out rather than shipped anyway.
EDGE_QUALITY_CEILING = 6.0

# The middle tile's size. Fixed across every fill variant, the same way the
# corner box is fixed across the sheet -- a tiling loop steps by a constant,
# and a constant needs every candidate to agree on it. Small enough that
# three genuinely different low-blotch patches fit in the 477x94 core without
# a large overlap between them -- @see fill_variants.
FILL_W = 96
FILL_H = 40
FILL_FEATHER = 10
FILL_DETREND_RADIUS = 10

# The core, as the ORIGINAL sheet measures it: (18,18)-(495,112) is the
# maximal fully-opaque rectangle in `chat_sheet_rs289.png` with one column of
# slack inside it, and is the closest any fill variant may come to a tear.
CORE_L, CORE_T, CORE_R, CORE_B = 18, 18, 495, 112


def wrap_cost(band, axis, start, period):
    """How badly `start + period` butts against `start`. Lower tiles cleaner."""
    if axis == "x":
        a, b = band[:, start : start + 4], band[:, start + period : start + period + 4]
    else:
        a, b = band[start : start + 4, :], band[start + period : start + period + 4, :]
    if a.shape != b.shape:
        return float("inf")
    d = a.astype(float) - b.astype(float)
    return float(np.abs(d[..., :3]).mean() + 6.0 * np.abs(d[..., 3]).mean())


def wrap_variants(band, axis, lo, hi, pmin, pmax, max_count, ceiling):
    """Up to `max_count` distinct, GOOD self-wrapping strips, ranked best first.

    Each variant is free to have its OWN period -- an earlier version of this
    cutter fixed the period to the single best one found and only varied the
    start, on the theory that a shared size makes the tiling loop simpler.
    It does, but the theory about the ART was wrong: on this sheet the tear
    only wraps cleanly at one exact spacing per neighbourhood, so holding the
    period fixed and hunting for other good starts at that SAME spacing found
    nothing nearby and one outright bad seam (cost 33 against a ceiling of a
    few) far away. Letting each variant pick its own best period is what the
    top edge needed to find a real third piece instead of a padded one.

    Ranked by cost and kept greedily while a candidate's source range does not
    overlap one already kept, so two variants are never the same tear read
    twice. Capped by `ceiling`: past it a wrap is a visible seam, and this
    cutter answers with fewer real variants rather than padding a fixed count
    with one. Some edges only have room for one -- @see the left edge, whose
    only good wrap is a single spot this sheet happens to have -- and that is
    the sheet's own limit, not a shortfall in the search.

    Returns [(cost, start, period), ...], length between 1 and `max_count`.
    """
    ranked = []
    for period in range(pmin, pmax + 1):
        for start in range(lo, hi - period + 1):
            ranked.append((wrap_cost(band, axis, start, period), start, period))
    ranked.sort(key=lambda t: t[0])
    assert ranked, "no candidate period fits the range"

    chosen = []
    for cost, start, period in ranked:
        if cost > ceiling:
            break
        lo2, hi2 = start, start + period
        if any(not (hi2 <= s or lo2 >= s + p) for _, s, p in chosen):
            continue
        chosen.append((cost, start, period))
        if len(chosen) >= max_count:
            break
    assert chosen, "no candidate wraps under the quality ceiling"
    return chosen


def box_blur(a, radius):
    """A separable box blur -- all the low-pass `detrend` needs."""
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
    dark washes across the sheet, which come back every tile width as a row of
    identical stains. Subtracting a heavy blur removes exactly those and keeps
    everything finer than `radius`.

    The tone is not the patch's own: it is set to the SAME target for every
    fill variant, so two different variants placed side by side (which the
    per-cell hash will do) sit at one shade rather than showing a step where
    a lighter cut meets a darker one.
    """
    wash = box_blur(patch.astype(float), radius)
    return patch.astype(float) - wash + target


def feather_wrap(source, pw, ph, feather):
    """Make a patch tile by fading each far edge back into what preceded it.

    `source` is the tile plus a feather's margin all round; the tile's last
    `feather` columns (then rows) are cross-faded into the `feather` that come
    immediately BEFORE the tile started, so by the last column/row the fade is
    complete and column pw-1 is column 0's neighbour in the original sheet
    again -- continuous by construction, not by a search that got a cost down.

    Grain only. A shape -- the edge strips' tear -- would smear under this;
    @see wrap_variants for what those get instead.
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


def flattest_patch(grey, pw, ph, excluded):
    """Where the paper is least blotchy, outside pixels `excluded` marks.

    The middle tile repeats, so it gets the most chances of anything here for
    the eye to catch a pattern -- and what repeats visibly is not the grain,
    it is the slow washes, so the patch is scored on the spread of its 8x8
    block means, which averages the grain away and sees only the wash.
    `excluded` keeps a second call from picking the same paper the first one
    did; returns None when nothing unexcluded is big enough.
    """
    h, w = grey.shape
    best = None
    for y in range(0, h - ph + 1, 2):
        for x in range(0, w - pw + 1, 4):
            if excluded[y : y + ph, x : x + pw].any():
                continue
            blocks = grey[y : y + ph, x : x + pw].reshape(ph // 4, 4, pw // 4, 4)
            spread = float(blocks.mean((1, 3)).std())
            if best is None or spread < best[0]:
                best = (spread, x, y)
    return best


def fill_variants(im, count):
    """`count` middle tiles, each a real disjoint patch of the core.

    Each is independently detrended to ONE shared target tone -- what the four
    edge strips end on -- so any two variants the per-cell hash lands next to
    each other still meet at one shade, and feather-wrapped so each tiles
    against itself. Returns a list of (spread, fx, fy, pixels).
    """
    h, w, _ = im.shape
    core = im[CORE_T:CORE_B, CORE_L:CORE_R, :]
    grey = core[..., :3].mean(2)
    margin = FILL_FEATHER
    excluded = np.zeros(grey.shape, dtype=bool)

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

    out = []
    for _ in range(count):
        found = flattest_patch(grey, FILL_W + 2 * margin, FILL_H + 2 * margin, excluded)
        if found is None:
            break
        spread, fx, fy = found
        patch = core[fy : fy + FILL_H + 2 * margin, fx : fx + FILL_W + 2 * margin]
        tile = feather_wrap(
            detrend(patch, target, FILL_DETREND_RADIUS), FILL_W, FILL_H, margin
        )
        out.append((spread, fx, fy, tile))
        # Exclude this patch's own footprint (not its margin) so the next
        # search cannot reuse it, but leaves the margin free for a neighbour
        # to feather into -- the core is only 477x94 and three 184x72 patches
        # do not fit disjoint by their full margins.
        excluded[fy : fy + FILL_H + 2 * margin, fx : fx + FILL_W + 2 * margin] = True
    return out, target


def save(name, pixels):
    path = os.path.join(OUT, name)
    Image.fromarray(np.asarray(pixels).astype(np.uint8), "RGBA").save(path)
    print("  %-28s %3dx%-3d" % (name, pixels.shape[1], pixels.shape[0]))
    return path


# ---------------------------------------------------------------------------
# The composition, as `mobile_compose_paper` performs it -- kept here so the
# cut can be PROVEN rather than argued. `--proof <dir>` writes sheets at a few
# sizes and prints the fringe measured back off them.
# ---------------------------------------------------------------------------


def _variant_hash(cell, salt):
    h = (cell * 2654435761 + salt * 40503) & 0xFFFFFFFF
    h ^= h >> 15
    h = (h * 0x85EBCA6B) & 0xFFFFFFFF
    h ^= h >> 13
    return h


def _tile_variants(dst, pieces, salt, x0, y0, x1, y1, axis):
    """Repeat, picking a variant per cell by a position hash.

    Steps by the CHOSEN piece's own size and not a shared constant: two
    variants of one edge are not guaranteed the same period any more --
    @see wrap_variants -- so the walk has to ask each piece its size as it
    goes rather than assume one. `axis` says which dimension is the repeating
    one (top/bottom repeat along x, left/right along y); the cross axis is
    always the strip's fixed thickness (every variant of one kind shares
    that, because it is read straight off one edge band of one height/width).
    """
    cell = 0
    if axis == "x":
        x = x0
        while x < x1:
            p = pieces[_variant_hash(cell, salt) % len(pieces)]
            w = min(p.shape[1], x1 - x)
            h = min(p.shape[0], y1 - y0)
            dst[y0 : y0 + h, x : x + w] = p[:h, :w]
            cell += 1
            x += p.shape[1]
    else:
        y = y0
        while y < y1:
            p = pieces[_variant_hash(cell, salt) % len(pieces)]
            h = min(p.shape[0], y1 - y)
            w = min(p.shape[1], x1 - x0)
            dst[y : y + h, x0 : x0 + w] = p[:h, :w]
            cell += 1
            y += p.shape[0]


def compose(width, height, pieces):
    assert width >= 2 * CORNER_W
    assert height >= 2 * CORNER_H
    out = np.zeros((height, width, 4), np.int32)
    _tile_variants(out, pieces["fill"], 0, CORNER_W, CORNER_H, width - CORNER_W, height - CORNER_H, "x")
    _tile_variants(out, pieces["top"], 1, CORNER_W, 0, width - CORNER_W, CORNER_H, "x")
    _tile_variants(
        out, pieces["bottom"], 2, CORNER_W, height - CORNER_H, width - CORNER_W, height, "x"
    )
    _tile_variants(out, pieces["left"], 3, 0, CORNER_H, CORNER_W, height - CORNER_H, "y")
    _tile_variants(
        out, pieces["right"], 4, width - CORNER_W, CORNER_H, width, height - CORNER_H, "y"
    )
    out[0:CORNER_H, 0:CORNER_W] = pieces["tl"]
    out[0:CORNER_H, width - CORNER_W :] = pieces["tr"]
    out[height - CORNER_H :, 0:CORNER_W] = pieces["bl"]
    out[height - CORNER_H :, width - CORNER_W :] = pieces["br"]
    return out


def _load_pieces():
    def load(name):
        return np.array(Image.open(os.path.join(OUT, name)).convert("RGBA")).astype(np.int32)

    p = {}
    for corner in ("tl", "tr", "bl", "br"):
        p[corner] = load("chat_paper_%s.png" % corner)
    for kind in ("top", "bottom", "left", "right"):
        variants = []
        i = 1
        while os.path.exists(os.path.join(OUT, "chat_paper_%s_%d.png" % (kind, i))):
            variants.append(load("chat_paper_%s_%d.png" % (kind, i)))
            i += 1
        assert variants, "no %s variants on disk -- run the cutter first" % kind
        p[kind] = variants
    p["fill"] = [load("chat_paper_fill_%d.png" % (i + 1)) for i in range(FILL_VARIANTS)]
    return p


def opaque_inset(sheet):
    """How far in from each edge the paper is solid -- the fringe."""
    solid = sheet[:, :, 3] == 255
    h, w = solid.shape
    best = None
    for left in range(CORNER_W):
        for right in range(CORNER_W):
            if w - right <= left:
                continue
            rows = solid[:, left : w - right].all(1)
            run = top = 0
            span = (0, 0, 0)
            for y in range(h + 1):
                if y < h and rows[y]:
                    if run == 0:
                        top = y
                    run += 1
                else:
                    if run > span[0]:
                        span = (run, top, y)
                    run = 0
            if span[0] <= 0:
                continue
            cand = (span[1] + h - span[2], left + right, left, span[1], right, h - span[2])
            if best is None or cand < best:
                best = cand
    assert best is not None, "the sheet has no solid rectangle in it"
    return best[2], best[3], best[4], best[5]


def main():
    im = np.array(Image.open(SRC).convert("RGBA")).astype(np.int32)
    h, w, _ = im.shape
    print("%s  %dx%d" % (os.path.basename(SRC), w, h))

    top_picks = wrap_variants(
        im[0:CORNER_H, :, :], "x", CORNER_W, w - CORNER_W, 50, 256,
        EDGE_VARIANTS_MAX, EDGE_QUALITY_CEILING,
    )
    bot_picks = wrap_variants(
        im[h - CORNER_H : h, :, :], "x", CORNER_W, w - CORNER_W, 50, 256,
        EDGE_VARIANTS_MAX, EDGE_QUALITY_CEILING,
    )
    left_picks = wrap_variants(
        im[:, 0:CORNER_W, :], "y", CORNER_H, h - CORNER_H, 14, 44,
        EDGE_VARIANTS_MAX, EDGE_QUALITY_CEILING,
    )

    print("edge variants (alpha-weighted seam cost; 0 is a perfect wrap; ceiling %.1f)" % EDGE_QUALITY_CEILING)
    for name, picks in (("top", top_picks), ("bottom", bot_picks), ("left", left_picks)):
        print(
            "  %-7s %d variant%s  "
            % (name, len(picks), "" if len(picks) == 1 else "s")
            + " ".join("start%d/period%d@%.2f" % (s, p, c) for c, s, p in picks)
        )

    fills, target = fill_variants(im, FILL_VARIANTS)
    print(
        "  fill    %d variants  " % len(fills)
        + " ".join("(%d,%d)@%.2f" % (fx, fy, spread) for spread, fx, fy, _ in fills)
        + "  tone %s" % np.round(target[:3]).astype(int)
    )

    print("pieces")
    save("chat_paper_tl.png", im[0:CORNER_H, 0:CORNER_W])
    save("chat_paper_tr.png", im[0:CORNER_H, w - CORNER_W : w])
    save("chat_paper_bl.png", im[h - CORNER_H : h, 0:CORNER_W])
    save("chat_paper_br.png", im[h - CORNER_H : h, w - CORNER_W : w])

    for i, (cost, start, period) in enumerate(top_picks):
        save("chat_paper_top_%d.png" % (i + 1), im[0:CORNER_H, start : start + period])
    for i, (cost, start, period) in enumerate(bot_picks):
        save(
            "chat_paper_bottom_%d.png" % (i + 1),
            im[h - CORNER_H : h, start : start + period],
        )
    left_tiles = []
    for i, (cost, start, period) in enumerate(left_picks):
        tile = im[start : start + period, 0:CORNER_W]
        left_tiles.append(tile)
        save("chat_paper_left_%d.png" % (i + 1), tile)
    # Mirrored, not cut: the sheet's own right edge drifts and will not wrap
    # (its silhouette runs 508 at the top to 496 in the middle and back to
    # 505, so no two rows sit at the same depth), so the right strip is the
    # left strip's variants flipped -- the same tears read from the other
    # side of the sheet.
    for i, tile in enumerate(left_tiles):
        save("chat_paper_right_%d.png" % (i + 1), tile[:, ::-1])

    # A stale variant from a PREVIOUS run at a higher count would sit on disk
    # forever otherwise -- a cutter that only ever adds files can leave a
    # fourth top variant behind after a rerun finds the source only supports
    # three, and the plugin would go on loading it.
    for kind in ("top", "bottom", "left", "right"):
        i = {"top": len(top_picks), "bottom": len(bot_picks), "left": len(left_picks), "right": len(left_picks)}[kind]
        i += 1
        while True:
            stale = os.path.join(OUT, "chat_paper_%s_%d.png" % (kind, i))
            if not os.path.exists(stale):
                break
            os.remove(stale)
            print("  removed stale %s" % os.path.basename(stale))
            i += 1

    for i, (spread, fx, fy, tile) in enumerate(fills):
        save("chat_paper_fill_%d.png" % (i + 1), tile)

    print(
        "\ncorner %dx%d -- mobile_gameframe.c reads this off the top-left piece"
        % (CORNER_W, CORNER_H)
    )
    total = 4 + len(top_picks) + len(bot_picks) + 2 * len(left_picks) + len(fills)
    print(
        "%d top, %d bottom, %d left/right each, %d fill -- %d pieces in all"
        % (len(top_picks), len(bot_picks), len(left_picks), len(fills), total)
    )

    if "--proof" in sys.argv:
        out_dir = sys.argv[sys.argv.index("--proof") + 1]
        os.makedirs(out_dir, exist_ok=True)
        pieces = _load_pieces()
        print("\nproof sheets ->", out_dir)
        for pw, ph in ((517, 130), (900, 260), (1200, 180), (300, 120)):
            sheet = compose(pw, ph, pieces)
            l, t, r, b = opaque_inset(sheet)
            print(
                "  %4dx%-4d  fringe l%-2d t%-2d r%-2d b%-2d  surface %dx%d"
                % (pw, ph, l, t, r, b, pw - l - r, ph - t - b)
            )
            Image.fromarray(sheet.astype(np.uint8), "RGBA").save(
                os.path.join(out_dir, "sheet_%dx%d.png" % (pw, ph))
            )
    return 0


if __name__ == "__main__":
    sys.exit(main())
