#!/usr/bin/env python3
"""
Bake the 2x and 3x tiers of the chrome skin from its 1x bake.

The 1x skin is spritebake's (src/engine/torirs_chrome_skin_baked.c): the
cache's own pixels, decoded by this tree's decoder, at the size the cache holds
them. The chrome draws at 1x, 2x and 3x (and at the fractional scales between,
from the nearest tier at or above), so each slot needs art at 2x and 3x that was
DRAWN at that size rather than stretched at draw time. The cache holds no larger
art for these slots, and the cache is not something this step may need, so the
tiers are derived from the 1x bake, the way tools/torirs_chrome_fonts.py derives
the web fonts from the font bake: the checked-in C is the source.

Two routes, chosen per slot by src/engine/torirs_chrome_skin_class.h:

  GLYPH     the mask route. Alpha >= 128 is the glyph's 1-bit mask; the ink
            inside it is split into two tones by k-means in RGB (three tones
            when the slot has no transparent plate: then the plate is one of
            them), seeded by the most frequent colour and the colour farthest
            from it. Every tone is the rounded mean colour of its pixels. The label image is scaled
            with scale2x (2x) or scale3x (3x), which copies labels and never
            drops a 1-pixel feature, and painted back. A GLYPH tier therefore
            holds at most three colours: the plate and two inks.

  TEXTURED  xBR at 2x and 3x (Hyllian's rules after libavfilter's vf_xbr:
            the 21-pixel neighbourhood, YUV pixel distance, the four
            rotated corner filters per pixel), extended to alpha: the distance
            adds |dA| and every blend runs in premultiplied alpha, so an edge
            against a transparent plate fades rather than darkening. Slots the
            drawer tiles wrap across the tiled axis (the WRAP column), so a
            tile does not grow a seam.

Everything is integer arithmetic; a rerun is byte-identical.

Outputs:
  src/engine/torirs_chrome_skin_tiers.c   per-slot per-tier ARGB arrays and
                                          ToriRSChromeSkin_TierGet
  src/engine/torirs_chrome_skin_tiers.h   its declarations and this recipe
  res/plugin_chrome/skin@2x/<Slot>.png    the DOM executor's 2x tier
  res/plugin_chrome/skin@3x/<Slot>.png    and its 3x tier

Usage:
  chrome_skin_tiers.py                 write every output
  chrome_skin_tiers.py --check         write nothing; exit 1 naming every
                                       committed output that differs
  chrome_skin_tiers.py --sheet out.png also write a review sheet: each slot at
                                       1x (nearest, x3), 2x (x1.5) and 3x

`make -C src chrome-skin` runs --check (with tools/chrome_font_tiers.py) and the
tier test; a stale tier fails it.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))

from spritebake_png import parse_bake, write_png  # noqa: E402

BAKE_C = REPO / "src/engine/torirs_chrome_skin_baked.c"
BAKE_H = REPO / "src/engine/torirs_chrome_skin_baked.h"
CLASS_H = REPO / "src/engine/torirs_chrome_skin_class.h"
OUT_C = REPO / "src/engine/torirs_chrome_skin_tiers.c"
OUT_H = REPO / "src/engine/torirs_chrome_skin_tiers.h"
OUT_PNG = {2: REPO / "res/plugin_chrome/skin@2x", 3: REPO / "res/plugin_chrome/skin@3x"}
TIERS = (2, 3)

# ---- inputs -----------------------------------------------------------------


def read_slots():
    """[(slot, name, w, h, argb)] in slot order, checked against the enum."""
    enum = {}
    for m in re.finditer(r"ToriRSChromeSkin_SLOT_(\w+)\s*=\s*(\d+)", BAKE_H.read_text()):
        if m.group(1) != "COUNT":
            enum[m.group(1)] = int(m.group(2))
    count = re.search(r"ToriRSChromeSkin_SLOT_COUNT\s*=\s*(\d+)", BAKE_H.read_text())
    if not count:
        raise SystemExit("chrome_skin_tiers: no ToriRSChromeSkin_SLOT_COUNT in %s" % BAKE_H)
    sprites = list(parse_bake(BAKE_C.read_text()))
    if len(sprites) != int(count.group(1)) or len(enum) != len(sprites):
        raise SystemExit(
            "chrome_skin_tiers: the bake has %d arrays, the enum %d slots, COUNT %s"
            % (len(sprites), len(enum), count.group(1))
        )
    slots = []
    for index, (name, w, h, argb) in enumerate(sprites):
        if enum.get(name) != index:
            raise SystemExit(
                "chrome_skin_tiers: array %d is %s but the enum puts it at %s"
                % (index, name, enum.get(name))
            )
        # A transparent pixel is transparent whatever colour it carries; one
        # spelling of it keeps "PE != PH" and the palette honest.
        argb = [p if (p >> 24) else 0 for p in argb]
        slots.append((index, name, w, h, argb))
    return slots


def read_classes(names):
    rows = re.findall(
        r"^\s*ROW\(\s*(\w+)\s*,\s*(GLYPH|TEXTURED)\s*,\s*(NONE|XY|X|Y)\s*\)",
        CLASS_H.read_text(),
        re.M,
    )
    table = {}
    for name, cls, wrap in rows:
        if name in table:
            raise SystemExit("chrome_skin_tiers: %s is classified twice" % name)
        table[name] = (cls, wrap)
    missing = [n for n in names if n not in table]
    extra = [n for n in table if n not in names]
    if missing or extra:
        raise SystemExit(
            "chrome_skin_tiers: class table and bake disagree: missing %s, unknown %s"
            % (missing, extra)
        )
    return table


# ---- GLYPH: the mask route ----------------------------------------------------


def luma(p):
    return (299 * ((p >> 16) & 255) + 587 * ((p >> 8) & 255) + 114 * (p & 255)) // 1000


def mean_colour(pixels):
    n = len(pixels)
    out = 0xFF000000
    for shift in (16, 8, 0):
        s = sum((p >> shift) & 255 for p in pixels)
        out |= ((s + n // 2) // n) << shift
    return out


def rgb_distance(a, b):
    dr = ((a >> 16) & 255) - ((b >> 16) & 255)
    dg = ((a >> 8) & 255) - ((b >> 8) & 255)
    db = (a & 255) - (b & 255)
    return dr * dr + dg * dg + db * db


def ink_tones(opaque, k):
    """`k` tones for the opaque pixels: Lloyd's k-means in RGB, seeded by the
    most frequent colour and then, each next seed, the colour farthest from
    every seed so far (ties to the smaller ARGB value), so the seeds are the
    plate or body and the mark that differs from it most -- a red cross on a
    dark disk stays a red cross, where a split on luma alone mixes its darker
    reds into the disk. Integer means, at most 64 rounds: deterministic."""
    counts = {}
    for p in opaque:
        counts[p] = counts.get(p, 0) + 1
    colours = sorted(counts)
    seeds = [min(colours, key=lambda c: (-counts[c], c))]
    while len(seeds) < k:
        far = max(colours, key=lambda c: (min(rgb_distance(c, s) for s in seeds), -c))
        if min(rgb_distance(far, s) for s in seeds) == 0:
            break
        seeds.append(far)
    centres = seeds
    assign = None
    for _ in range(64):
        nearest = {
            c: min(range(len(centres)), key=lambda t: (rgb_distance(c, centres[t]), t))
            for c in colours
        }
        if nearest == assign:
            break
        assign = nearest
        groups = [[] for _ in centres]
        for c in colours:
            groups[assign[c]].extend([c] * counts[c])
        centres = [mean_colour(g) if g else centres[t] for t, g in enumerate(groups)]
    # Darkest first, so a label reads the same way in every slot.
    order = sorted(range(len(centres)), key=lambda t: (luma(centres[t]), centres[t]))
    rank = {t: r for r, t in enumerate(order)}
    return [centres[t] for t in order], {c: rank[assign[c]] for c in colours}


def glyph_labels(w, h, argb):
    """(labels, palette): label 0 is the transparent plate when the slot has
    one, then the tones darkest first."""
    opaque = [p for p in argb if (p >> 24) >= 128]
    has_plate = len(opaque) != len(argb)
    if not opaque:
        return [0] * len(argb), [0x00000000]
    palette, tone = ink_tones(opaque, 2 if has_plate else 3)
    base = 1 if has_plate else 0
    labels = [base + tone[p] if (p >> 24) >= 128 else 0 for p in argb]
    return labels, ([0x00000000] if has_plate else []) + palette


def scale_labels(w, h, src, n):
    """scale2x (n == 2) / scale3x (n == 3), edges replicated."""

    def at(x, y):
        x = 0 if x < 0 else (w - 1 if x >= w else x)
        y = 0 if y < 0 else (h - 1 if y >= h else y)
        return src[y * w + x]

    ow = w * n
    out = [0] * (ow * h * n)
    for y in range(h):
        for x in range(w):
            A, B, C = at(x - 1, y - 1), at(x, y - 1), at(x + 1, y - 1)
            D, E, F = at(x - 1, y), at(x, y), at(x + 1, y)
            G, H, I = at(x - 1, y + 1), at(x, y + 1), at(x + 1, y + 1)
            if B != H and D != F:
                if n == 2:
                    block = [
                        D if D == B else E,
                        F if B == F else E,
                        D if D == H else E,
                        F if H == F else E,
                    ]
                else:
                    block = [
                        D if D == B else E,
                        B if (D == B and E != C) or (B == F and E != A) else E,
                        F if B == F else E,
                        D if (D == B and E != G) or (D == H and E != A) else E,
                        E,
                        F if (B == F and E != I) or (H == F and E != C) else E,
                        D if D == H else E,
                        H if (D == H and E != I) or (H == F and E != G) else E,
                        F if H == F else E,
                    ]
            else:
                block = [E] * (n * n)
            for sy in range(n):
                row = (y * n + sy) * ow + x * n
                out[row : row + n] = block[sy * n : sy * n + n]
    return out


def glyph_tier(w, h, argb, n):
    labels, palette = glyph_labels(w, h, argb)
    return [palette[v] for v in scale_labels(w, h, labels, n)]


# ---- TEXTURED: xBR ------------------------------------------------------------

# The neighbourhood in the frame of the bottom-right corner filter.
#            A1 B1 C1
#         A0 PA PB PC C4
#         D0 PD PE PF F4
#         G0 PG PH PI I4
#            G5 H5 I5
NAMES = {
    "PA": (-1, -1), "PB": (0, -1), "PC": (1, -1),
    "PD": (-1, 0), "PE": (0, 0), "PF": (1, 0),
    "PG": (-1, 1), "PH": (0, 1), "PI": (1, 1),
    "A1": (-1, -2), "B1": (0, -2), "C1": (1, -2),
    "A0": (-2, -1), "D0": (-2, 0), "G0": (-2, 1),
    "C4": (2, -1), "F4": (2, 0), "I4": (2, 1),
    "G5": (-1, 2), "H5": (0, 2), "I5": (1, 2),
}  # fmt: skip
ORDER = tuple(NAMES)


def rotate(x, y, k):
    """k quarter turns of (x, y) -> (y, -x): the frame of corner k (0 bottom
    right, 1 top right, 2 top left, 3 bottom left), in the order the filters
    run."""
    for _ in range(k):
        x, y = y, -x
    return x, y


def yuv(p):
    r, g, b = (p >> 16) & 255, (p >> 8) & 255, p & 255
    return (
        (299 * r + 587 * g + 114 * b) // 1000,
        (-169 * r - 331 * g + 500 * b) // 1000 + 128,
        (500 * r - 419 * g - 81 * b) // 1000 + 128,
        p >> 24,
    )


class Distance:
    def __init__(self):
        self.yuv = {}
        self.memo = {}

    def __call__(self, a, b):
        if a == b:
            return 0
        key = (a, b) if a < b else (b, a)
        d = self.memo.get(key)
        if d is None:
            ya = self.yuv.get(a) or self.yuv.setdefault(a, yuv(a))
            yb = self.yuv.get(b) or self.yuv.setdefault(b, yuv(b))
            d = sum(abs(i - j) for i, j in zip(ya, yb))
            self.memo[key] = d
        return d


def blend(a, b, m, s):
    """a + (b - a) * m / 2^s per channel, in premultiplied alpha, integers."""
    aa, ab = a >> 24, b >> 24
    alpha = aa + (((ab - aa) * m) >> s)
    if alpha <= 0:
        return 0
    out = alpha << 24
    for shift in (16, 8, 0):
        pa = ((a >> shift) & 255) * aa
        pb = ((b >> shift) & 255) * ab
        p = pa + (((pb - pa) * m) >> s)
        c = (p + alpha // 2) // alpha
        out |= (0 if c < 0 else 255 if c > 255 else c) << shift
    return out


def xbr(w, h, argb, n, wrap):
    df = Distance()

    def eq(a, b):
        return df(a, b) < 155

    wrap_x = "X" in wrap
    wrap_y = "Y" in wrap

    def at(x, y):
        x = x % w if wrap_x else (0 if x < 0 else (w - 1 if x >= w else x))
        y = y % h if wrap_y else (0 if y < 0 else (h - 1 if y >= h else y))
        return argb[y * w + x]

    # Per corner: the actual offset of every named neighbour, and the block
    # index of every frame sub-pixel (row-major in the frame).
    frames = []
    for k in range(4):
        offsets = [rotate(*NAMES[name], k) for name in ORDER]
        subs = []
        for i in range(n * n):
            cx, cy = 2 * (i % n) - (n - 1), 2 * (i // n) - (n - 1)
            rx, ry = rotate(cx, cy, k)
            subs.append(((ry + n - 1) // 2) * n + (rx + n - 1) // 2)
        frames.append((offsets, subs))

    ow = w * n
    out = [0] * (ow * h * n)
    for y in range(h):
        for x in range(w):
            PE = argb[y * w + x]
            E = [PE] * (n * n)
            for offsets, N in frames:
                P = dict(zip(ORDER, (at(x + dx, y + dy) for dx, dy in offsets)))
                PH, PF = P["PH"], P["PF"]
                if PE == PH or PE == PF:
                    continue
                PI, PG, PC, PD, PB = P["PI"], P["PG"], P["PC"], P["PD"], P["PB"]
                F4, I4, H5, I5 = P["F4"], P["I4"], P["H5"], P["I5"]
                e = df(PE, PC) + df(PE, PG) + df(PI, H5) + df(PI, F4) + (df(PH, PF) << 2)
                i = df(PH, PD) + df(PH, I5) + df(PF, I4) + df(PF, PB) + (df(PE, PI) << 2)
                if e > i:
                    continue
                px = PF if df(PE, PF) <= df(PE, PH) else PH
                if n == 2:
                    strong = e < i and (
                        (not eq(PF, PB) and not eq(PH, PD))
                        or (eq(PE, PI) and (not eq(PF, I4) and not eq(PH, I5)))
                        or eq(PE, PG)
                        or eq(PE, PC)
                    )
                else:
                    strong = e < i and (
                        (not eq(PF, PB) and not eq(PF, PC))
                        or (not eq(PH, PD) and not eq(PH, PG))
                        or (
                            eq(PE, PI)
                            and (
                                (not eq(PF, F4) and not eq(PF, I4))
                                or (not eq(PH, H5) and not eq(PH, I5))
                            )
                        )
                        or eq(PE, PG)
                        or eq(PE, PC)
                    )
                last = N[n * n - 1]
                if not strong:
                    E[last] = blend(E[last], px, 1, 1)
                    continue
                ke = df(PF, PG)
                ki = df(PH, PC)
                left = (ke << 1) <= ki and PE != PG and PD != PG
                up = ke >= (ki << 1) and PE != PC and PB != PC
                if n == 2:
                    n1, n2, n3 = N[1], N[2], N[3]
                    if left and up:
                        E[n3] = blend(E[n3], px, 7, 3)
                        E[n2] = blend(E[n2], px, 1, 2)
                        E[n1] = E[n2]
                    elif left:
                        E[n3] = blend(E[n3], px, 3, 2)
                        E[n2] = blend(E[n2], px, 1, 2)
                    elif up:
                        E[n3] = blend(E[n3], px, 3, 2)
                        E[n1] = blend(E[n1], px, 1, 2)
                    else:
                        E[n3] = blend(E[n3], px, 1, 1)
                else:
                    n2, n5, n6, n7, n8 = N[2], N[5], N[6], N[7], N[8]
                    if left and up:
                        E[n7] = blend(E[n7], px, 3, 2)
                        E[n6] = blend(E[n6], px, 1, 2)
                        E[n5] = E[n7]
                        E[n2] = E[n6]
                        E[n8] = px
                    elif left:
                        E[n7] = blend(E[n7], px, 3, 2)
                        E[n5] = blend(E[n5], px, 1, 2)
                        E[n6] = blend(E[n6], px, 1, 2)
                        E[n8] = px
                    elif up:
                        E[n5] = blend(E[n5], px, 3, 2)
                        E[n7] = blend(E[n7], px, 1, 2)
                        E[n2] = blend(E[n2], px, 1, 2)
                        E[n8] = px
                    else:
                        E[n8] = blend(E[n8], px, 7, 3)
                        E[n5] = blend(E[n5], px, 1, 3)
                        E[n7] = blend(E[n7], px, 1, 3)
            for sy in range(n):
                row = (y * n + sy) * ow + x * n
                out[row : row + n] = E[sy * n : sy * n + n]
    return out


# ---- the bake ---------------------------------------------------------------


def fnv1a(argb):
    """FNV-1a 32 over the words' little-endian bytes: what the C test hashes."""
    h = 0x811C9DC5
    for p in argb:
        for shift in (0, 8, 16, 24):
            h ^= (p >> shift) & 255
            h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def bake():
    slots = read_slots()
    classes = read_classes([s[1] for s in slots])
    tiers = []
    for slot, name, w, h, argb in slots:
        cls, wrap = classes[name]
        by_tier = {}
        for n in TIERS:
            if cls == "GLYPH":
                by_tier[n] = glyph_tier(w, h, argb, n)
            else:
                by_tier[n] = xbr(w, h, argb, n, wrap)
        tiers.append((slot, name, w, h, argb, cls, wrap, by_tier))
    return tiers


RECIPE = """\
 * Recipe (tools/chrome_skin_tiers.py; docs/CHROME_SCALE.md):
 *   - input: the 1x arrays of src/engine/torirs_chrome_skin_baked.c, alpha 0
 *     normalised to 0x00000000; the class of each slot from
 *     src/engine/torirs_chrome_skin_class.h.
 *   - GLYPH: mask = alpha >= 128; the ink inside it split into two tones (three
 *     when the slot has no transparent plate) by Lloyd's k-means in RGB,
 *     seeded by the most frequent colour, then each colour farthest from the
 *     seeds so far (ties to the smaller ARGB), at most 64 rounds; each tone is
 *     the rounded mean of its pixels, darkest first. The label image is scaled by scale2x /
 *     scale3x (edges replicated) and painted back: at most three colours.
 *   - TEXTURED: xBR 2x / 3x after libavfilter vf_xbr's rules: distance |dY| + |dU|
 *     + |dV| over integer YUV, plus |dA|; eq is distance < 155; four rotated
 *     corner filters per pixel (bottom-right, top-right, top-left,
 *     bottom-left); blends of m / 2^s in premultiplied alpha, integer floor.
 *     Edges replicated, or wrapped along the axes the class table's WRAP
 *     column names (the drawer tiles the slot along them).
 *   - the 1x tier is the bake itself; tiers 2 and 3 are here.
 *   - a rerun is byte-identical; `make -C src chrome-skin` fails on a stale
 *     tier."""


def emit_c(tiers):
    count = len(tiers)
    lines = [
        "/*",
        " * GENERATED by tools/chrome_skin_tiers.py -- do not edit.",
        " *",
        " * The 2x and 3x tiers of every baked chrome skin slot. The recipe is in",
        " * torirs_chrome_skin_tiers.h.",
        " *",
        " * Slots:",
    ]
    for slot, name, w, h, _argb, cls, wrap, _by in tiers:
        lines.append(
            " *   %2d %-20s %-8s wrap %-4s %3dx%-3d -> %3dx%-3d %3dx%d"
            % (slot, name, cls, wrap, w, h, w * 2, h * 2, w * 3, h * 3)
        )
    lines += [
        " */",
        '#include "torirs_chrome_skin_tiers.h"',
        "",
        "#include <assert.h>",
        "#include <stddef.h>",
        "",
    ]
    for _slot, name, w, h, _argb, _cls, _wrap, by_tier in tiers:
        for n in TIERS:
            pixels = by_tier[n]
            lines.append(
                "static const uint32_t ToriRSChromeSkinTier_%s_%dx_argb[%d] = {"
                % (name, n, len(pixels))
            )
            for at in range(0, len(pixels), 8):
                lines.append(
                    "    " + " ".join("0x%08X," % p for p in pixels[at : at + 8])
                )
            lines.append("};")
            lines.append("")
    lines.append(
        "static const struct ToriRSChromeSkin_Sprite ToriRSChromeSkinTier_table[%d][%d] = {"
        % (len(TIERS), count)
    )
    for n in TIERS:
        lines.append("    {")
        for _slot, name, w, h, _argb, _cls, _wrap, _by in tiers:
            lines.append(
                "        { %d, %d, ToriRSChromeSkinTier_%s_%dx_argb }," % (w * n, h * n, name, n)
            )
        lines.append("    },")
    lines += ["};", ""]
    lines.append("static const uint32_t ToriRSChromeSkinTier_source_fnv[%d] = {" % count)
    for slot, name, _w, _h, argb, _cls, _wrap, _by in tiers:
        lines.append("    0x%08XU, /* %s */" % (fnv1a(argb), name))
    lines += [
        "};",
        "",
        "const struct ToriRSChromeSkin_Sprite*",
        "ToriRSChromeSkin_TierGet(int slot, int tier)",
        "{",
        "    assert(slot >= 0);",
        "    assert(tier >= 2);",
        "    assert(tier <= TORIRS_CHROME_SKIN_TIER_MAX);",
        "    /* A slot the 1x bake gained after these tiers were baked has none. */",
        "    if( slot >= TORIRS_CHROME_SKIN_TIERS_BAKED )",
        "        return NULL;",
        "    return &ToriRSChromeSkinTier_table[tier - 2][slot];",
        "}",
        "",
        "uint32_t",
        "ToriRSChromeSkin_TierSourceHash(int slot)",
        "{",
        "    assert(slot >= 0);",
        "    assert(slot < TORIRS_CHROME_SKIN_TIERS_BAKED);",
        "    return ToriRSChromeSkinTier_source_fnv[slot];",
        "}",
        "",
    ]
    return "\n".join(lines)


def emit_h(tiers):
    return "\n".join(
        [
            "/*",
            " * GENERATED by tools/chrome_skin_tiers.py -- do not edit.",
            " *",
            " * The 2x and 3x tiers of the baked chrome skin. The 1x tier is",
            " * torirs_chrome_skin_baked.c itself; callers reach every tier through",
            " * ToriRSChromeSkin_SpriteTier (ui/torirs_chrome_skin.h), which falls",
            " * back to 1x for a slot these tiers do not cover.",
            " *",
            RECIPE,
            " *",
            " * The arrays are `static const` and live for the life of the process.",
            " */",
            "#ifndef TORIRS_CHROME_SKIN_TIERS_H",
            "#define TORIRS_CHROME_SKIN_TIERS_H",
            "",
            '#include "torirs_chrome_skin_baked.h"',
            "",
            "#include <stdint.h>",
            "",
            "/** The highest tier baked. Tier n is n times the 1x size on each axis. */",
            "#define TORIRS_CHROME_SKIN_TIER_MAX %d" % TIERS[-1],
            "/** How many slots (from 0) these tiers were baked for. */",
            "#define TORIRS_CHROME_SKIN_TIERS_BAKED %d" % len(tiers),
            "",
            "/**",
            " * Tier `tier` (2..TORIRS_CHROME_SKIN_TIER_MAX) of `slot`, or NULL when the",
            " * slot is past TORIRS_CHROME_SKIN_TIERS_BAKED. Tier 1 is not here: it is",
            " * ToriRSChromeSkin_Get.",
            " */",
            "const struct ToriRSChromeSkin_Sprite*",
            "ToriRSChromeSkin_TierGet(int slot, int tier);",
            "",
            "/**",
            " * FNV-1a 32 of the 1x pixels (each word's little-endian bytes, alpha-0",
            " * pixels as 0) these tiers were baked from. The tier test compares it",
            " * with the 1x bake, so a re-bake that forgot the tiers fails there.",
            " */",
            "uint32_t",
            "ToriRSChromeSkin_TierSourceHash(int slot);",
            "",
            "#endif",
            "",
        ]
    )


# ---- PNG reading (for --check) -----------------------------------------------


def read_png(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    at = 8
    idat = b""
    width = height = None
    while at < len(data):
        (length,) = struct.unpack(">I", data[at : at + 4])
        tag = data[at + 4 : at + 8]
        body = data[at + 8 : at + 8 + length]
        at += 12 + length
        if tag == b"IHDR":
            width, height, depth, colour, _c, _f, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or colour != 6 or interlace != 0:
                raise ValueError("not 8-bit RGBA, non-interlaced")
        elif tag == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    stride = width * 4
    rows = []
    prev = bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b = prev[i]
            c = prev[i - 4] if i >= 4 else 0
            if kind == 1:
                line[i] = (line[i] + a) & 255
            elif kind == 2:
                line[i] = (line[i] + b) & 255
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 255
        rows.append(line)
        prev = line
    pixels = []
    for line in rows:
        for x in range(width):
            r, g, b, a = line[x * 4 : x * 4 + 4]
            pixels.append((a << 24) | (r << 16) | (g << 8) | b)
    return width, height, pixels


# ---- the review sheet -------------------------------------------------------


def write_sheet(path, tiers):
    """Each slot as a row of three crops on a grey plate: 1x drawn nearest at
    x3, the 2x tier drawn nearest at x1.5 (every other pixel doubled), and the
    3x tier as baked -- the three at one size, which is how a crop is read."""
    pad = 4
    cells = []
    for _slot, _name, w, h, argb, _cls, _wrap, by_tier in tiers:
        one = [argb[(y // 3) * w + x // 3] for y in range(h * 3) for x in range(w * 3)]
        two_src = by_tier[2]
        two = [
            two_src[((y * 2) // 3) * (w * 2) + (x * 2) // 3]
            for y in range(h * 3)
            for x in range(w * 3)
        ]
        cells.append((w * 3, h * 3, [one, two, by_tier[3]]))
    width = max(cw * 3 + pad * 4 for cw, _ch, _c in cells)
    height = sum(ch + pad for _cw, ch, _c in cells) + pad
    plate = 0xFF5D5447
    pixels = [plate] * (width * height)
    y0 = pad
    for cw, ch, crops in cells:
        for k, crop in enumerate(crops):
            x0 = pad + k * (cw + pad)
            for y in range(ch):
                for x in range(cw):
                    p = crop[y * cw + x]
                    a = p >> 24
                    if a == 0:
                        continue
                    q = pixels[(y0 + y) * width + x0 + x]
                    mix = 0xFF000000
                    for shift in (16, 8, 0):
                        c = ((p >> shift) & 255) * a + ((q >> shift) & 255) * (255 - a)
                        mix |= ((c + 127) // 255) << shift
                    pixels[(y0 + y) * width + x0 + x] = mix
        y0 += ch + pad
    write_png(str(path), width, height, pixels)


# ---- main -------------------------------------------------------------------


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true", help="compare, write nothing")
    parser.add_argument("--sheet", type=Path, help="also write a review sheet PNG here")
    args = parser.parse_args(argv)

    tiers = bake()
    c_text = emit_c(tiers)
    h_text = emit_h(tiers)
    if args.sheet:
        write_sheet(args.sheet, tiers)

    if not args.check:
        OUT_C.write_text(c_text)
        OUT_H.write_text(h_text)
        for n in TIERS:
            OUT_PNG[n].mkdir(parents=True, exist_ok=True)
            for _slot, name, w, h, _argb, _cls, _wrap, by_tier in tiers:
                write_png(str(OUT_PNG[n] / (name + ".png")), w * n, h * n, by_tier[n])
        glyph = sum(1 for t in tiers if t[5] == "GLYPH")
        print(
            "chrome_skin_tiers: %d slots (%d GLYPH, %d TEXTURED) at %s"
            % (len(tiers), glyph, len(tiers) - glyph, ", ".join("%dx" % n for n in TIERS))
        )
        return 0

    stale = []
    for path, text in ((OUT_C, c_text), (OUT_H, h_text)):
        if not path.exists() or path.read_text() != text:
            stale.append(str(path.relative_to(REPO)))
    for n in TIERS:
        names = set()
        for _slot, name, w, h, _argb, _cls, _wrap, by_tier in tiers:
            path = OUT_PNG[n] / (name + ".png")
            names.add(path.name)
            try:
                got = read_png(path)
            except (OSError, ValueError, zlib.error) as error:
                stale.append("%s (%s)" % (path.relative_to(REPO), error))
                continue
            if got != (w * n, h * n, by_tier[n]):
                stale.append(str(path.relative_to(REPO)))
        if OUT_PNG[n].is_dir():
            for extra in sorted(os.listdir(OUT_PNG[n])):
                if extra.endswith(".png") and extra not in names:
                    stale.append("%s (no such slot)" % (OUT_PNG[n] / extra).relative_to(REPO))
    if stale:
        print("chrome_skin_tiers: the committed tiers are not what the recipe bakes:")
        for item in stale:
            print("  " + item)
        print("rerun: python3 tools/chrome_skin_tiers.py")
        return 1
    print("chrome_skin_tiers: %d slots x %d tiers match the bake" % (len(tiers), len(TIERS)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
