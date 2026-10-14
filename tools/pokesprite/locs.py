#!/usr/bin/env python3
"""Scenery (locs) as PokeSprite sheets: loc/<id>/<shape>/<seq>.

A loc is drawn where the map placed it, rotated by its placement (0..3), so
its sheet's four rows are the four ROTATIONS, not facings -- the client picks
the row by the scenery entity's angle. Everything is at one world scale, the
ground's own (PS_TILE_PX = 32 per 128-unit tile): a tree has to fit its
tiles, which is not true of a creature, whose size is a style choice.

    locs.py --loc 1276 --shape 10 --store assets/pokesprite
"""

import argparse
import math
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pokesprite as ps  # noqa: E402

F = 3
TILE_PX = 32
# Sprite pixels per world unit = 512 / (zoom * F); the ground is 32 / 128.
ZOOM = round(512 * 128 / (TILE_PX * F))
PITCH = 176
SIDE = 1200
FRAME_HEIGHT = 200
ROTATIONS = [("r0", 0), ("r1", 512), ("r2", 1024), ("r3", 1536)]
# Shape 11 is the diagonal centrepiece: the same model turned a further 45.
DIAGONAL_SHAPE = 11


def ground_px():
    """Where the model's origin (the footprint centre at ground level) lands on
    the --full canvas: ev_render lifts the model by frame_height/2 and orbits at
    `zoom` with the camera at `pitch`, so the origin sits lift*cos below centre
    at depth zoom + lift*sin."""
    lift = FRAME_HEIGHT / 2.0
    a = PITCH * 2 * math.pi / 2048
    return SIDE / 2.0, SIDE / 2.0 + lift * math.cos(a) * 512.0 / (ZOOM + lift * math.sin(a))


def render(loc, shape, seq, raw):
    label = "s%d" % seq
    for rname, yaw in ROTATIONS:
        d = os.path.join(raw, "%s_%s" % (label, rname))
        if os.path.exists(os.path.join(d, "faces.txt")):
            continue
        os.makedirs(d, exist_ok=True)
        cmd = [ps.EV_SHEET, "--rev", "osrs239", ps.CACHE, "--loc", str(loc), "--shape", str(shape),
               "--yaw", str((yaw + (256 if shape == DIAGONAL_SHAPE else 0)) & 2047),
               "--zoom", str(ZOOM), "--side", str(SIDE), "--fit", "1",
               "--frame-height", str(FRAME_HEIGHT), "--full", "--pitch", str(PITCH),
               "--materials", "--out", d]
        if seq >= 0:
            cmd += ["--seq", str(seq)]
        _o, err = ps.run(cmd)
        if "canvas edge" in err:
            print("warning: loc %d shape %d %s touches the canvas edge" % (loc, shape, rname))
    return label


def generate(loc, shape, seq, work, store_root):
    from store import Store
    raw = os.path.join(work, "loc_%d_%d" % (loc, shape))
    label = render(loc, shape, seq, raw)
    frames, boxes, _ncol = ps.stylise(raw, [(label, seq)], ROTATIONS, F, ZOOM, SIDE, 48, None)
    b = boxes[label]
    gx, gy = ground_px()
    origin = ((SIDE // 2 - b[0]) // F, (SIDE // 2 - b[1]) // F)
    ground = (int(round((gx - b[0]) / F)), int(round((gy - b[1]) / F)))
    rows = [frames[(label, rname)][0] for rname, _ in ROTATIONS]
    st = Store(store_root)
    st.write_rgba("loc/%d/%d/%d" % (loc, shape, seq), rows, frames[(label, "r0")][1], origin,
                  ground)
    st.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--loc", type=int, required=True)
    ap.add_argument("--shape", type=int, default=10)
    ap.add_argument("--seq", type=int, default=-1)
    ap.add_argument("--store", required=True)
    ap.add_argument("--work", default="build/poke/gen")
    a = ap.parse_args()
    generate(a.loc, a.shape, a.seq, a.work, a.store)
    print("stored loc/%d/%d/%d" % (a.loc, a.shape, a.seq))


if __name__ == "__main__":
    main()
