#!/usr/bin/env python3
"""
The chrome fonts' 2x and 3x tiers, regenerated from the 1x masks and checked.

The three faces (Small p11, Body p12, Menu b12) are baked by fontbake at 1x,
2x and 3x (src/engine/torirs_debug_font_baked.c, metrics in
src/ui/uitree_debug_font_metrics.h). The 2x and 3x tiers are DERIVED: every 1x
mask pixel multiplied, every metric with it. This tool:

  1. regenerates each derived tier from the 1x masks and checks the bake holds
     exactly that -- masks, offsets, advances, ascent, line box -- for every
     tier the metrics header does not mark `_TUNED 1`. A tuned tier
     (src/engine/torirs_chrome_font_tier.h) keeps the derived metrics, so for
     it only the metrics are checked;
  2. scales the same 1x masks with the glyph-safe scaler the skin's GLYPH slots
     use (scale2x / scale3x, tools/chrome_skin_tiers.py, outside the cell read
     as ink-free), and reports, per glyph, whether that differs from plain
     pixel multiplication. A glyph that differs has a diagonal or a corner that
     doubling turns into a staircase or a blot, and is a candidate for a
     hand-tuned tier: that list is the hand-tuning queue, written into
     docs/CHROME_SCALE.md between the generated markers. The scaler's output is
     NOT baked -- it is a pointer to where a person should look. Nothing here
     draws a glyph by hand.

Usage:
  chrome_font_tiers.py                 rewrite the queue in docs/CHROME_SCALE.md
  chrome_font_tiers.py --check         write nothing; exit 1 when the bake is
                                       not its derived tiers or the queue in
                                       the doc is stale
  chrome_font_tiers.py --sheet out.png also write a review sheet: per face and
                                       tier, each queued glyph doubled beside
                                       its scale2x / scale3x form

`make -C src chrome-skin` runs --check.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))

from spritebake_png import write_png  # noqa: E402
from torirs_chrome_fonts import SOURCE_CODEPOINTS  # noqa: E402

BAKE_C = REPO / "src/engine/torirs_debug_font_baked.c"
METRICS_H = REPO / "src/ui/uitree_debug_font_metrics.h"
DOC = REPO / "docs/CHROME_SCALE.md"
BEGIN = "<!-- chrome_font_tiers.py: begin generated queue -->"
END = "<!-- chrome_font_tiers.py: end generated queue -->"

FACES = (("Small", "p11"), ("Body", "p12"), ("Menu", "b12"))
TIERS = (2, 3)
GLYPHS = len(SOURCE_CODEPOINTS)


def numbers(text):
    return [int(v) for v in re.findall(r"-?\d+", text)]


def field(body, name):
    m = re.search(r"\.%s\s*=\s*\{(.*?)\}" % re.escape(name), body, re.S)
    if not m:
        raise SystemExit("chrome_font_tiers: no .%s" % name)
    return numbers(m.group(1))


def parse_font(c_text, symbol):
    bits = re.search(
        r"static const unsigned char %s_glyph_bits\[(\d+)\]\s*=\s*\{(.*?)\n\};" % symbol,
        c_text,
        re.S,
    )
    body = re.search(
        r"static struct ToriDraw_Font %s_font\s*=\s*\{(.*?)\n\};" % symbol, c_text, re.S
    )
    if not bits or not body:
        raise SystemExit("chrome_font_tiers: %s is not in %s" % (symbol, BAKE_C))
    blob = numbers(bits.group(2))
    if len(blob) != int(bits.group(1)):
        raise SystemExit("chrome_font_tiers: %s glyph bits are short" % symbol)
    b = body.group(1)
    font = {
        "w": field(b, "glyph_width"),
        "h": field(b, "glyph_height"),
        "ox": field(b, "offset_x"),
        "oy": field(b, "offset_y"),
        "advance": field(b, "advance"),
        "draw_width": field(b, "draw_width"),
        "line_height": int(re.search(r"\.line_height\s*=\s*(\d+)", b).group(1)),
        "masks": [],
    }
    at = 0
    for g in range(GLYPHS):
        n = font["w"][g] * font["h"][g]
        font["masks"].append([1 if v else 0 for v in blob[at : at + n]])
        at += n
    if at != len(blob):
        raise SystemExit("chrome_font_tiers: %s masks do not consume the blob" % symbol)
    return font


def parse_metrics(text, symbol):
    def define(suffix):
        m = re.search(
            r"^#define ToriRSChromeFont_%s_%s (\d+)$" % (symbol, suffix), text, re.M
        )
        if not m:
            raise SystemExit("chrome_font_tiers: no ToriRSChromeFont_%s_%s" % (symbol, suffix))
        return int(m.group(1))

    adv = re.search(
        r"ToriRSChromeFont_%s_advance_px\[256\]\s*=\s*\{(.*?)\};" % symbol, text, re.S
    )
    return {
        "line_height": define("LINE_HEIGHT"),
        "line_box": define("LINE_BOX"),
        "tuned": define("TUNED"),
        "advance_px": numbers(adv.group(1)),
    }


def multiply(mask, w, h, n):
    return [mask[(y // n) * w + x // n] for y in range(h * n) for x in range(w * n)]


def glyph_safe(mask, w, h, n):
    """scale2x / scale3x of a 1-bit mask, the cell's outside read as 0."""

    def at(x, y):
        return mask[y * w + x] if 0 <= x < w and 0 <= y < h else 0

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


def counters(mask, w, h):
    """Background pixels with ink on both sides along one axis: the 1-pixel
    counters (p12's a, e, s) that multiplication turns into slots exactly as
    thick as the strokes around them."""
    def ink(x, y):
        return 0 <= x < w and 0 <= y < h and mask[y * w + x]

    return sum(
        1
        for y in range(h)
        for x in range(w)
        if not mask[y * w + x]
        and ((ink(x, y - 1) and ink(x, y + 1)) or (ink(x - 1, y) and ink(x + 1, y)))
    )


def glyph_name(g):
    c = chr(SOURCE_CODEPOINTS[g])
    return "`%s`" % c if c != "`" else "`` ` ``"


def analyse():
    c_text = BAKE_C.read_text()
    m_text = METRICS_H.read_text()
    problems = []
    queue = []  # (face, label, tier, [(glyph, changed pixels)])
    for face, label in FACES:
        one = parse_font(c_text, face)
        one_metrics = parse_metrics(m_text, face)
        if one_metrics["tuned"]:
            problems.append("%s: the 1x face is the source and cannot be TUNED" % face)
        for n in TIERS:
            symbol = "%s%dx" % (face, n)
            baked = parse_font(c_text, symbol)
            metrics = parse_metrics(m_text, symbol)
            # The metrics a tier must keep whether it is derived or tuned.
            for key in ("ox", "oy", "advance"):
                want = [v * n for v in one[key]]
                if baked[key] != want:
                    problems.append("%s: %s is not the 1x %s times %d" % (symbol, key, key, n))
            if baked["draw_width"] != [v * n for v in one["draw_width"]]:
                problems.append("%s: draw_width is not the 1x table times %d" % (symbol, n))
            if baked["line_height"] != one["line_height"] * n:
                problems.append("%s: ascent is not the 1x ascent times %d" % (symbol, n))
            if metrics["line_height"] != one_metrics["line_height"] * n:
                problems.append("%s: LINE_HEIGHT is not the 1x one times %d" % (symbol, n))
            if metrics["line_box"] != one_metrics["line_box"] * n:
                problems.append("%s: LINE_BOX is not the 1x one times %d" % (symbol, n))
            if metrics["advance_px"] != [v * n for v in one_metrics["advance_px"]]:
                problems.append("%s: advance_px is not the 1x table times %d" % (symbol, n))
            differs = []
            for g in range(GLYPHS):
                w, h = one["w"][g], one["h"][g]
                doubled = multiply(one["masks"][g], w, h, n)
                if not metrics["tuned"]:
                    if baked["w"][g] != w * n or baked["h"][g] != h * n:
                        problems.append("%s: glyph %d cell is not the 1x cell times %d" % (symbol, g, n))
                    elif baked["masks"][g] != doubled:
                        problems.append("%s: glyph %d is not the 1x mask multiplied" % (symbol, g))
                scaled = glyph_safe(one["masks"][g], w, h, n)
                changed = sum(1 for a, b in zip(doubled, scaled) if a != b)
                if changed:
                    differs.append((g, changed, counters(one["masks"][g], w, h)))
            queue.append((face, label, n, metrics["tuned"], differs))
    return problems, queue


def render_queue(queue):
    lines = [
        BEGIN,
        "",
        "Generated by `tools/chrome_font_tiers.py` from the 1x masks; do not edit by",
        "hand. A glyph is listed when its scale2x (2x) or scale3x (3x) form differs",
        "from the mask multiplied, with the number of tier pixels that differ: that",
        "glyph has a diagonal, a corner or a 1-pixel counter that multiplication",
        "turns into a staircase or a blot, so it is where a hand-tuned tier would",
        "change what is read. The scaler's output is a pointer, not a bake.",
        "",
        "The head of each queue is the glyphs that also have a 1-pixel counter (a",
        "background pixel with ink on both sides along one axis: the 1-pixel",
        "counters of p12's a, e and s), written `glyph counter-pixels/changed`; the",
        "rest follow as `glyph changed`, most changed first.",
        "",
    ]
    for face, label, n, tuned, differs in queue:
        state = "tuned" if tuned else "derived"
        head = sorted((d for d in differs if d[2]), key=lambda d: (-d[2], -d[1], d[0]))
        rest = sorted((d for d in differs if not d[2]), key=lambda d: (-d[1], d[0]))
        lines.append(
            "- **%s (%s) %dx** (%s): %d of %d glyphs differ, %d with a 1-pixel counter"
            % (face, label, n, state, len(differs), GLYPHS, len(head))
        )
        if head:
            lines.append(
                "  - counters first: "
                + ", ".join("%s %d/%d" % (glyph_name(g), k, c) for g, c, k in head)
            )
        if rest:
            lines.append(
                "  - then: " + ", ".join("%s %d" % (glyph_name(g), c) for g, c, _k in rest)
            )
    lines += ["", END]
    return "\n".join(lines)


def write_sheet(path, queue):
    """Per face and tier, every queued glyph: multiplied (left) beside the
    glyph-safe form (right), white ink on the panel brown, one row per tier."""
    c_text = BAKE_C.read_text()
    ink, plate, gap = 0xFFFFFFFF, 0xFF3E3529, 0xFF5D5447
    rows = []
    for face, _label, n, _tuned, differs in queue:
        one = parse_font(c_text, face)
        cells = []
        for g, _changed, _counter in differs:
            w, h = one["w"][g], one["h"][g]
            cells.append((w * n, h * n, multiply(one["masks"][g], w, h, n),
                          glyph_safe(one["masks"][g], w, h, n)))
        rows.append(cells)
    width = 4 + max(
        (sum(cw * 2 + 6 for cw, _ch, _a, _b in cells) for cells in rows), default=0
    )
    heights = [max((ch for _cw, ch, _a, _b in cells), default=0) + 4 for cells in rows]
    height = sum(heights) + 4
    pixels = [gap] * (width * height)
    y0 = 4
    for cells, row_h in zip(rows, heights):
        x0 = 4
        for cw, ch, multiplied, scaled in cells:
            for k, mask in enumerate((multiplied, scaled)):
                for y in range(ch):
                    for x in range(cw):
                        pixels[(y0 + y) * width + x0 + k * cw + x] = ink if mask[y * cw + x] else plate
            x0 += cw * 2 + 6
        y0 += row_h
    write_png(str(path), width, height, pixels)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true", help="compare, write nothing")
    parser.add_argument("--sheet", type=Path, help="also write a review sheet PNG here")
    args = parser.parse_args(argv)

    problems, queue = analyse()
    section = render_queue(queue)
    if args.sheet:
        write_sheet(args.sheet, queue)
    doc = DOC.read_text() if DOC.exists() else ""
    if BEGIN not in doc or END not in doc:
        problems.append("%s has no generated-queue markers" % DOC.relative_to(REPO))
        updated = doc
    else:
        updated = doc[: doc.index(BEGIN)] + section + doc[doc.index(END) + len(END) :]

    if problems:
        print("chrome_font_tiers: the bake is not its derived tiers:")
        for p in problems:
            print("  " + p)
        return 1
    if args.check:
        if updated != doc:
            print("chrome_font_tiers: the hand-tuning queue in %s is stale" % DOC.relative_to(REPO))
            print("rerun: python3 tools/chrome_font_tiers.py")
            return 1
        print(
            "chrome_font_tiers: %d tiers match their derivation; queue current (%d entries)"
            % (len(queue), sum(len(q[4]) for q in queue))
        )
        return 0
    DOC.write_text(updated)
    for face, label, n, tuned, differs in queue:
        print("chrome_font_tiers: %s %dx: %d glyphs in the queue" % (face, n, len(differs)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
