#!/usr/bin/env python3
"""Make a chrome button out of a sprite, by borrowing the close button's plate.

The plugin window's title bar wears the interfaces' own close button — archives
831/832, a bevelled plate with an X on it, one of them lit from the opposite
corner so the hover state is in the art rather than in an outline drawn over
it. Any NEW button in that bar has to look like it came out of the same cache,
and the game has no art for the ones we invent (a pop-out arrow, say — no
interface window ever left its frame).

So: take the X button, wipe the mark off it, and put your sprite there.
Everything outside the middle eight-by-eight — frame, bevel, face, ink, both
lighting states — stays the cache's, and only the glyph is yours.

    tools/chrome_button_stamp.py arrow.png -o popout.png       # the pair
    tools/chrome_button_stamp.py arrow.png --ink --scale 8     # ...matching the X's ink
    tools/chrome_button_stamp.py arrow.png --rows              # as a spritebake glyph

WHICH COLOUR IS THE FACE. Not the commonest colour in the middle of the button:
the X covers more than half of its own box, so that reading gives you the INK,
and every glyph comes out inside-out — and still looks like a button, which is
how it survives review. Not the commonest colour outside it either: that is the
border, drawn in the same near-black as the mark. It is the one-pixel ring
around the mark box, which is plate on both the lit and the pressed variant.

WHAT THIS DOES NOT DO. A PNG is not a button the client can draw. Shipping one
means getting it into the bake, and there are two ways:

  * `--rows` prints the glyph as spritebake's own 8x8 table, for a mark that is
    a single ink over the face. Paste it beside `k_arrow_ne` in
    3rd/rscache/tools/spritebake/main.c, name it in `k_glyphs`, and the recipe
    line `--stamp CloseButton=YourButton:your_glyph` bakes it for real.
  * `--emit-c` prints the composited plate as the ARGB array spritebake writes.
    Useful for diffing against a bake — NOT for pasting into
    src/engine/torirs_chrome_skin_baked.c, which is generated and says so.

Self-check: `--selftest` rebuilds the baked PopoutButton out of the baked
CloseButton and its own middle, and fails if a single pixel differs. That is
this script and the C tool agreeing on the rule, which is the only thing that
keeps a preview honest.
"""

import argparse
import os
import re
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - the message is the whole handling
    sys.exit("this needs Pillow: python3 -m pip install pillow")

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BAKE = os.path.join(REPO, "src", "engine", "torirs_chrome_skin_baked.c")

# The two states every button in this chrome has: resting, and lit from the
# other corner for hover. A button baked without its partner has no hover at
# all, because the effect is the second sprite rather than a filter.
PLATE_PAIR = ("CloseButton", "CloseButtonOver")


def read_baked(symbol, path=BAKE):
    """The baked sprite `symbol` as (w, h, [0xAARRGGBB, ...]).

    The size comes from the bake's own table rather than from the square root
    of the pixel count: most of what is in there is not square (the panel tile
    is 88x60, the plugin icon 33x36), and guessing would quietly transpose them.
    """
    with open(path, encoding="utf-8") as f:
        src = f.read()
    body = re.search(
        r"ToriRSChromeSkin_%s_argb\[(\d+)\] = \{(.*?)\};" % re.escape(symbol),
        src,
        re.S,
    )
    if not body:
        raise SystemExit("no sprite named '%s' in %s" % (symbol, path))
    row = re.search(
        r"\{\s*(\d+),\s*(\d+),\s*ToriRSChromeSkin_%s_argb\s*\}" % re.escape(symbol), src
    )
    if not row:
        raise SystemExit("sprite '%s' has no row in the bake's table" % symbol)
    px = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{8})", body.group(2))]
    w, h = int(row.group(1)), int(row.group(2))
    if len(px) != int(body.group(1)) or len(px) != w * h:
        raise SystemExit("sprite '%s' is truncated in the bake" % symbol)
    return w, h, px


def mark_box(side):
    """The middle half of a plate — where its own mark sits, and yours goes."""
    inset = side // 4
    return inset, side - 2 * inset


def plate_face(px, w, h, inset, box):
    """The plate's face colour: the modal colour of the ring around the box.

    See the note at the top on the two readings that look right and are not.
    """
    ring = []
    for y in range(inset - 1, inset + box + 1):
        for x in range(inset - 1, inset + box + 1):
            edge = x in (inset - 1, inset + box) or y in (inset - 1, inset + box)
            if edge and 0 <= x < w and 0 <= y < h:
                ring.append(px[y * w + x])
    return max(set(ring), key=ring.count)


def plate_ink(px, w, inset, box, face):
    """The colour the plate's own mark is drawn in — the commonest non-face
    colour inside the box. Read off the art rather than named, so a plate from
    another revision brings its own ink."""
    inside = [
        px[y * w + x]
        for y in range(inset, inset + box)
        for x in range(inset, inset + box)
        if px[y * w + x] != face
    ]
    if not inside:
        raise SystemExit("this plate has no mark to replace")
    return max(set(inside), key=inside.count)


def load_glyph(path, box):
    """The sprite to stamp, as RGBA at most `box` on a side.

    Downscaled NEAREST and never up: this is pixel art on a 16-pixel button,
    and a smooth resample of it is a smudge. A glyph smaller than the box is
    centred, which is what an authored 8x8 mark wants.

    Which is also the honest limit of this: the box is eight pixels. A mark
    drawn for it reads; a detailed sprite squeezed into it is mush, however
    good the sprite was.
    """
    img = Image.open(path).convert("RGBA")
    if img.width > box or img.height > box:
        scale = min(box / img.width, box / img.height)
        img = img.resize(
            (max(1, int(img.width * scale)), max(1, int(img.height * scale))),
            Image.NEAREST,
        )
    return img


def stamp(plate, glyph, ink=False):
    """Wipe the plate's mark and put `glyph` in its place. Returns new pixels."""
    w, h, px = plate
    if w != h:
        # A plate is a button, and a button is square. Anything else is a
        # symbol picked by mistake -- the panel tile, a frame rail -- and
        # stamping the middle of it would produce a plausible-looking nothing.
        raise SystemExit("plate is %dx%d; a button plate is square" % (w, h))
    inset, box = mark_box(w)
    face = plate_face(px, w, h, inset, box)
    mark = plate_ink(px, w, inset, box, face) if ink else None
    out = list(px)

    for y in range(inset, inset + box):
        for x in range(inset, inset + box):
            out[y * w + x] = face

    ox = inset + (box - glyph.width) // 2
    oy = inset + (box - glyph.height) // 2
    for gy in range(glyph.height):
        for gx in range(glyph.width):
            r, g, b, a = glyph.getpixel((gx, gy))
            if a < 128:
                continue  # the face shows through, as it does around the X
            colour = mark if ink else (0xFF000000 | (r << 16) | (g << 8) | b)
            out[(oy + gy) * w + (ox + gx)] = colour
    return w, h, out


def to_image(sprite, scale=1):
    w, h, px = sprite
    img = Image.new("RGBA", (w, h))
    img.putdata(
        [
            ((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, (v >> 24) & 0xFF)
            for v in px
        ]
    )
    if scale > 1:
        img = img.resize((w * scale, h * scale), Image.NEAREST)
    return img


def emit_rows(sprite):
    """The stamped mark as spritebake's 8x8 glyph table — '#' is ink."""
    w, h, px = sprite
    inset, box = mark_box(w)
    face = plate_face(px, w, h, inset, box)
    print("static const char* const k_your_glyph[%d] = {" % box)
    for y in range(inset, inset + box):
        row = "".join(
            "#" if px[y * w + x] != face else "." for x in range(inset, inset + box)
        )
        print('    "%s",' % row)
    print("};")


def emit_c(symbol, sprite):
    """The plate as the array spritebake writes. For DIFFING against a bake."""
    w, h, px = sprite
    print("static const uint32_t ToriRSChromeSkin_%s_argb[%d] = {" % (symbol, w * h))
    for i in range(0, w * h, 8):
        print("   " + "".join(" 0x%08X," % v for v in px[i : i + 8]))
    print("};")


def selftest():
    """This script and spritebake, on the same plate, must agree exactly."""
    plate = read_baked("CloseButton")
    want = read_baked("PopoutButton")
    inset, box = mark_box(want[0])
    # The arrow, taken back out of the button the C tool made.
    glyph = to_image(want).crop((inset, inset, inset + box, inset + box))
    got = stamp(plate, glyph, ink=False)
    if got[2] != want[2]:
        bad = sum(1 for a, b in zip(got[2], want[2]) if a != b)
        sys.exit("selftest: %d pixel(s) differ from the baked PopoutButton" % bad)
    print("selftest: restamping PopoutButton reproduces the bake exactly")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("glyph", nargs="?", help="the sprite to stamp (any format PIL reads)")
    ap.add_argument("-o", "--out", help="output PNG (default: beside the glyph)")
    ap.add_argument(
        "--plate",
        default=None,
        help="baked symbol to borrow (default: both states of the close button)",
    )
    ap.add_argument("--ink", action="store_true", help="recolour the glyph to the plate's own ink")
    ap.add_argument("--scale", type=int, default=1, help="write the PNG blown up N times")
    ap.add_argument("--rows", action="store_true", help="print it as a spritebake glyph table")
    ap.add_argument("--emit-c", action="store_true", help="print the ARGB array, for diffing")
    ap.add_argument("--selftest", action="store_true", help="check against the shipped bake")
    args = ap.parse_args()

    if args.selftest:
        selftest()
        return
    if not args.glyph:
        ap.error("a glyph to stamp, or --selftest")

    plates = [args.plate] if args.plate else list(PLATE_PAIR)
    base = args.out or os.path.splitext(args.glyph)[0] + "-button.png"
    root, ext = os.path.splitext(base)

    for i, symbol in enumerate(plates):
        plate = read_baked(symbol)
        _, box = mark_box(plate[0])
        out = stamp(plate, load_glyph(args.glyph, box), ink=args.ink)
        # The pair is written as name.png and name-over.png, because the second
        # is not a variant of the first that anything can compute: it is the
        # other plate, and the two ship together or the button has no hover.
        path = base if len(plates) == 1 or i == 0 else root + "-over" + ext
        to_image(out, args.scale).save(path)
        print("wrote %s (%s plate)" % (path, symbol))
        if args.rows and i == 0:
            emit_rows(out)
        if args.emit_c:
            emit_c(symbol.replace("Close", "Stamped"), out)


if __name__ == "__main__":
    main()
