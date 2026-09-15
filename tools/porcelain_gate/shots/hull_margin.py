#!/usr/bin/env python3
"""Are the highlighter's hulls IN the picture, and by how much?

    python3 hull_margin.py <shot.png> --expect 3 [--colour '#FF00FF']
                           [--viewport x,y,w,h] [--min-margin 4]

The entity highlighter's live capture tagged three npcs and photographed two,
and the second of those two was cut by the viewport's right edge with eight
columns inside it. Nothing in the run said so. The config write reported
applied=1 -- tagging a species that is not in front of you is a perfectly valid
tag -- the plugin emitted three hulls every frame, and the picture, read whole
at 765x503, looked like a highlighter that works. It took a connected-component
scan to find that one of the three had never been in frame at all.

So the claim a capture of a world overlay makes is not "there is ink". It is
"there are N closed shapes and every one of them is clear of the edges", and
that is a measurement, not an impression:

  * an exact-colour mask, because the outline is drawn at the plugin's own
    configured colour and nothing else in a scene is #FF00FF;
  * 8-connected components, because a hull is one closed ring;
  * the margin from each side of the WORLD VIEWPORT, not from the window. A
    hull can sit inside a 765x503 frame and still be cut, because the 2004
    frame's viewport ends at x=515 and the sidebar starts there.

A cluster that touches an edge is reported as CUT and fails, because an outline
whose closing edge was clipped away is exactly what the defect looked like.

The viewport rect is an argument and has no default that fits every lane: the
2004 fixed frame is 4,4,512,334 and the resizable toplevels are not. Read it
off the lane rather than assuming this one.
"""

import argparse
import sys

from PIL import Image

# The 2004 fixed frame's world viewport -- the lane this was written for.
CS1_VIEWPORT = (4, 4, 512, 334)


def clusters(image, colour):
    """8-connected components of the exact-colour mask."""
    width, height = image.size
    pixels = image.load()
    mask = {
        (x, y)
        for y in range(height)
        for x in range(width)
        if pixels[x, y] == colour
    }
    seen = set()
    found = []
    for start in mask:
        if start in seen:
            continue
        stack = [start]
        seen.add(start)
        component = []
        while stack:
            cell = stack.pop()
            component.append(cell)
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    neighbour = (cell[0] + dx, cell[1] + dy)
                    if neighbour in mask and neighbour not in seen:
                        seen.add(neighbour)
                        stack.append(neighbour)
        xs = [cell[0] for cell in component]
        ys = [cell[1] for cell in component]
        found.append((len(component), min(xs), max(xs), min(ys), max(ys)))
    found.sort(key=lambda entry: (entry[1], entry[3]))
    return found


def parse_colour(text):
    text = text.lstrip("#")
    return tuple(int(text[i:i + 2], 16) for i in (0, 2, 4))


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("shot")
    parser.add_argument("--expect", type=int, required=True,
                        help="how many hulls the drive tagged")
    parser.add_argument("--colour", default="#FF00FF")
    parser.add_argument("--viewport", default=",".join(str(v) for v in CS1_VIEWPORT),
                        help="x,y,w,h of the world viewport on this lane")
    parser.add_argument("--min-margin", type=int, default=4,
                        help="pixels a hull must keep clear of every viewport edge")
    args = parser.parse_args(argv)

    view_x, view_y, view_w, view_h = (int(v) for v in args.viewport.split(","))
    right, bottom = view_x + view_w - 1, view_y + view_h - 1

    image = Image.open(args.shot).convert("RGB")
    found = clusters(image, parse_colour(args.colour))

    print(f"{args.shot} {image.size[0]}x{image.size[1]} "
          f"viewport {view_x},{view_y} {view_w}x{view_h}")
    failures = []
    for size, x0, x1, y0, y1 in found:
        margins = (x0 - view_x, right - x1, y0 - view_y, bottom - y1)
        worst = min(margins)
        state = "CUT" if worst < 0 else ("THIN" if worst < args.min_margin else "ok")
        print(f"  {size:5d} px  x {x0}-{x1}  y {y0}-{y1}  "
              f"margin l/r/t/b {margins[0]}/{margins[1]}/{margins[2]}/{margins[3]}  {state}")
        if state != "ok":
            failures.append(f"a hull at x {x0}-{x1} y {y0}-{y1} is {worst} px from an edge")

    if len(found) != args.expect:
        failures.append(f"expected {args.expect} hull(s), found {len(found)}")
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print(f"OK: {len(found)} hull(s), all clear of the viewport by "
          f"{args.min_margin} px or more")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
