#!/usr/bin/env python3
"""Player LAYERS: one sheet per identity kit / worn obj, composited by depth.

A player's look is 12 appearance slots, each a kit, an obj or empty, so there
is no "player sheet" to pre-render -- the combinations are astronomical. What
is rendered instead is each component on its own, per (sequence, facing),
inside the default reference body (ev_sheet --layer), so every layer shares one
pose, one chibi head line and one anchor. The client composites whatever
appearance it is sent.

A layer pixel is DATA, not colour:

    R  material slot   index into the layer's material table
    G  shade band      0 darkest .. steps-1
    B  depth           128 + (camera z - zoom) / 2, 0 nearest .. 254; 255 none
    A  255 covered, 0 not

and the layer carries its material table: per slot the base colour (and the
HSL16 values it came from, so a kit recolour finds it), the ramp step count,
and whether it is head (eyes go on head pixels). The client builds each ramp
from the possibly-recoloured base -- a Pokemon palette swap -- so one hair
layer serves every hair colour.

Compositing (what the client does, and composite() here does):
  nearest depth wins per pixel -> colour from that layer's ramp -> overlap
  lines where neighbouring pixels jump in depth -> 1px outline -> eyes.
"""

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pokesprite as ps  # noqa: E402

ROOT = ps.ROOT
FACINGS = ps.FACINGS
F = 4                      # humanoid pixelation factor
ZOOM = 650                 # one humanoid scale for every layer
PITCH = 112
CHIBI = "260,17,72,125"
SIDE = 800
# One cell for every humanoid layer: the anchor (canvas centre) is the same
# pixel in all of them, which is what lets the client stack any set.
HALF_W, TOP, BOT = 30 * F, 52 * F, 20 * F
BOX = (SIDE // 2 - HALF_W, SIDE // 2 - TOP, SIDE // 2 + HALF_W, SIDE // 2 + BOT)
CELL = ((BOX[2] - BOX[0]) // F, (BOX[3] - BOX[1]) // F)
ORIGIN = (HALF_W // F, TOP // F)
DEPTH_NONE = 255


def depth_q(z):
    return np.clip(np.round(128 + (z - ZOOM) / 2.0), 0, 254)


def render(layer, seq, yaw, out):
    os.makedirs(out, exist_ok=True)
    ps.run([ps.EV_SHEET, "--rev", "osrs239", ps.CACHE, "--player", "--layer", layer,
            "--seq", str(seq), "--yaw", str(yaw), "--zoom", str(ZOOM), "--side", str(SIDE),
            "--fit", "1", "--frame-height", "200", "--full", "--pitch", str(PITCH),
            "--chibi", CHIBI, "--materials", "--out", out])


def load_frames(d):
    rows = [l.split() for l in open(d + "/faces.txt") if not l.startswith("#")]
    idrgb = np.full(1 << 24, -1, np.int32)
    tri = np.zeros((len(rows), 3), np.int64)
    base_hsl = np.zeros(len(rows), np.int64)
    base_rgb = np.zeros((len(rows), 3), np.int64)
    head = np.zeros(len(rows), bool)
    for r in rows:
        i = int(r[0])
        idrgb[int(r[1], 16)] = i
        base_hsl[i] = int(r[2])
        base_rgb[i] = tuple(bytes.fromhex(r[3]))
        head[i] = r[4] == "1"
        tri[i] = (int(r[5]), int(r[6]), int(r[7]))
    frames = []
    x0, y0, x1, y1 = BOX
    for fp in sorted(glob.glob(d + "/f*.png")):
        n = int(os.path.basename(fp)[1:4])
        lit = np.asarray(Image.open(fp).convert("RGBA"))[y0:y1, x0:x1]
        idi = np.asarray(Image.open("%s/i%03d.png" % (d, n)).convert("RGBA")).astype(np.int64)
        face = idrgb[(idi[..., 0] << 16) | (idi[..., 1] << 8) | idi[..., 2]]
        face[idi[..., 3] < 128] = -1
        vs = np.fromfile("%s/v%03d.bin" % (d, n), np.int32).reshape(-1, 3).astype(np.float64)
        vs[:, 2] += ZOOM  # relative to the model centre, which sits at z == zoom
        z = pixel_depth(face, vs, tri)
        frames.append((lit, face[y0:y1, x0:x1], z[y0:y1, x0:x1]))
    durs = ps.timing(d + "/frames.txt")
    return frames, durs, base_hsl, base_rgb, head


def pixel_depth(face, vs, tri):
    """Each pixel's camera z on its own face; 1/z is linear in screen space."""
    z = np.full(face.shape, np.inf)
    ys, xs = np.nonzero(face >= 0)
    if not len(ys):
        return z
    fc = face[ys, xs]
    A, B, C = vs[tri[fc, 0]], vs[tri[fc, 1]], vs[tri[fc, 2]]
    px, py = xs + 0.5, ys + 0.5
    den = (B[:, 1] - C[:, 1]) * (A[:, 0] - C[:, 0]) + (C[:, 0] - B[:, 0]) * (A[:, 1] - C[:, 1])
    den = np.where(np.abs(den) < 1e-9, 1e-9, den)
    w0 = ((B[:, 1] - C[:, 1]) * (px - C[:, 0]) + (C[:, 0] - B[:, 0]) * (py - C[:, 1])) / den
    w1 = ((C[:, 1] - A[:, 1]) * (px - C[:, 0]) + (A[:, 0] - C[:, 0]) * (py - C[:, 1])) / den
    w = np.clip(np.stack([w0, w1, 1 - w0 - w1], 1), 0, 1)
    w /= np.maximum(w.sum(1, keepdims=True), 1e-9)
    z[ys, xs] = 1.0 / (w[:, 0] / A[:, 2] + w[:, 1] / B[:, 2] + w[:, 2] / C[:, 2])
    return z


def stylise_layer(name, clips):
    """clips: {(seq_label, facing): (frames, durs, base_hsl, base_rgb, head)} for ONE layer
    -> encoded frames {(seq_label, facing): [RGBA uint8]} and the material table."""
    any_clip = next(iter(clips.values()))
    base_hsl, base_rgb, head = any_clip[2], any_clip[3], any_clip[4]
    # Materials: this layer's base colours, grouped; weighted by visible pixels.
    seen = {}
    for frames, *_ in clips.values():
        for _lit, face, _z in frames:
            f = face[face >= 0]
            for c, n in zip(*np.unique(base_hsl[f], return_counts=True)):
                seen[int(c)] = seen.get(int(c), 0) + int(n)
    colours = [(c, tuple(int(v) for v in ps_rgb(c, base_hsl, base_rgb)), n) for c, n in seen.items()]
    groups = ps.group_materials(colours) if colours else []
    gid = {}
    for i, g in enumerate(groups):
        for c in g:
            gid[c] = i + 1
    K = len(groups) + 1
    tot = sum(n for _, _, n in colours) or 1
    share = np.zeros(K)
    for c, _, n in colours:
        share[gid[c]] += n / tot
    # A layer is one garment or one body part: three steps is enough, and it
    # keeps a full outfit near the 16-colour sprite budget.
    steps = np.array([0] + [3 if s > 0.25 else 2 if s > 0.04 else 1 for s in share[1:]])
    feature = np.array([False] + [s < 0.04 for s in share[1:]])
    table = []
    for k in range(1, K):
        grp = groups[k - 1]
        w = np.array([n for c, _, n in colours if c in grp], float)
        rgbs = np.array([rgb for c, rgb, _ in colours if c in grp], float)
        hflags = [bool(head[np.nonzero(base_hsl == c)[0][0]]) for c in grp]
        table.append({"base_rgb": [int(v) for v in (rgbs * w[:, None]).sum(0) / w.sum()],
                      "hsl16": sorted(grp), "steps": int(steps[k]),
                      "head": sum(hflags) * 2 > len(hflags)})
    hsl_to_mat = np.zeros(1 << 16, np.int64)
    for c, k in gid.items():
        hsl_to_mat[c] = k

    sigma = 1.2 * F
    low, pool = {}, [[] for _ in range(K)]
    for key, (frames, durs, *_rest) in clips.items():
        out = []
        for lit, face, z in frames:
            m = np.where(face >= 0, hsl_to_mat[base_hsl[np.maximum(face, 0)]], 0)
            v = lit[..., :3].max(-1).astype(np.float32) / 255
            v = ps.smooth_value(v, m, sigma)
            mm, vv = ps.downscale(m, v, lit[..., 3], F, feature, K)
            H, W = mm.shape
            zb = np.where(m > 0, z, np.inf)[:H * F, :W * F].reshape(H, F, W, F)
            mb = m[:H * F, :W * F].reshape(H, F, W, F)
            zz = np.where(mb == mm[:, None, :, None], zb, np.inf).min((1, 3))
            out.append((mm, vv, zz))
            for k in range(1, K):
                pool[k].append(vv[mm == k])
        low[key] = (out, durs)
    cuts = {}
    for k in range(1, K):
        vals = np.concatenate(pool[k]) if pool[k] else np.zeros(0)
        n = steps[k]
        cuts[k] = np.quantile(vals, ps.CUTS[n]) if n > 1 and vals.size else []
    enc = {}
    for key, (out, durs) in low.items():
        ims = []
        for mm, vv, zz in out:
            band = np.zeros(mm.shape, np.int64)
            for k in range(1, K):
                sel = mm == k
                band[sel] = np.searchsorted(cuts[k], vv[sel])
            mm, band = ps.clean(mm, band, K, feature)
            band = np.minimum(band, steps[mm] - 1).clip(0)
            # A pixel cleanup filled has no depth of its own: take the
            # nearest neighbour's.
            zz = np.where(np.isinf(zz) & (mm > 0), min_neighbour(zz), zz)
            img = np.zeros(mm.shape + (4,), np.uint8)
            img[..., 0] = np.where(mm > 0, mm - 1, 0)
            img[..., 1] = band
            img[..., 2] = np.where(mm > 0, depth_q(np.where(np.isinf(zz), ZOOM, zz)), DEPTH_NONE)
            img[..., 3] = np.where(mm > 0, 255, 0)
            ims.append(img)
        enc[key] = (ims, durs)
    return enc, table


def ps_rgb(c, base_hsl, base_rgb):
    return base_rgb[np.nonzero(base_hsl == c)[0][0]]


def min_neighbour(z):
    out = np.full(z.shape, np.inf)
    for s in ps.shifts(np.where(np.isinf(z), 1e18, z), 1e18):
        out = np.minimum(out, s)
    return np.where(out >= 1e17, np.inf, out)


# ---------------------------------------------------------------- compositor

def composite(layers, facing, frame_index, recolour=None):
    """layers: list of (enc_frames_for_this_clip, table). The client's job."""
    W, H = CELL
    depth = np.full((H, W), 999, np.int64)
    rgb = np.zeros((H, W, 3), np.uint8)
    head = np.zeros((H, W), bool)
    lid = np.full((H, W), -1, np.int64)
    ramps = []
    for li, (frames, table) in enumerate(layers):
        ims = frames[min(frame_index, len(frames) - 1)]
        rt = []
        for m in table:
            base = m["base_rgb"]
            if recolour:
                for h in m["hsl16"]:
                    if h in recolour:
                        base = recolour[h]
            r, _ = ps.ramp(tuple(base), max(1, m["steps"]))
            rt.append(r)
        ramps.append(rt)
        cov = ims[..., 3] > 0
        d = ims[..., 2].astype(np.int64)
        win = cov & (d < depth)
        depth = np.where(win, d, depth)
        lid = np.where(win, li, lid)
        for y, x in zip(*np.nonzero(win)):
            mslot, band = ims[y, x, 0], ims[y, x, 1]
            rgb[y, x] = ramps[li][mslot][min(band, len(ramps[li][mslot]) - 1)]
            head[y, x] = table[mslot]["head"]
    op = lid >= 0
    img = np.zeros((H, W, 4), np.uint8)
    img[..., :3] = rgb
    img[..., 3] = np.where(op, 255, 0)
    line = (40, 24, 48)
    # Overlap lines: far side of a depth jump between different layers, or
    # of a big jump within one.
    behind = np.zeros(op.shape, bool)
    for nd, nl, nop in zip(ps.shifts(depth, 999), ps.shifts(lid, -1), ps.shifts(op, False)):
        jump = depth - nd
        behind |= op & nop & (((nl != lid) & (jump > 4)) | (jump > 12))
    img[behind, :3] = line
    nb = ps.shifts(op, False)
    touch = ~op & sum(nb).astype(bool)
    img[touch, :3] = line
    img[touch, 3] = 255
    ps.paint_eyes(img, head & op, facing, line)
    return img


# ---------------------------------------------------------------- example

def checker(w, h, s=4):
    bg = Image.new("RGBA", (w, h), (236, 236, 240, 255))
    d = ImageDraw.Draw(bg)
    for y in range(0, h, s):
        for x in range((y // s) % 2 * s, w, 2 * s):
            d.rectangle([x, y, x + s - 1, y + s - 1], fill=(222, 222, 228, 255))
    return bg


def grass(w, h):
    bg = Image.new("RGBA", (w, h), (136, 200, 112, 255))
    d = ImageDraw.Draw(bg)
    for y in range(0, h, 8):
        for x in range((y // 8) % 2 * 8, w, 16):
            d.rectangle([x, y, x + 7, y + 7], fill=(124, 190, 100, 255))
    return bg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--skip-render", action="store_true")
    a = ap.parse_args()
    SEQS = [("idle", 808), ("walk", 819)]
    MAN = ["kit:0", "kit:10", "kit:18", "kit:26", "kit:33", "kit:36", "kit:42"]
    RUNE = ["obj:1163", "obj:1127", "obj:1079", "obj:1201", "obj:1333", "kit:33", "kit:42"]
    layers = list(dict.fromkeys(MAN + RUNE))
    raw = os.path.join(a.out, "_raw")
    if not a.skip_render:
        shutil.rmtree(raw, ignore_errors=True)
        for L in layers:
            for label, seq in SEQS:
                for fname, yaw in FACINGS:
                    render(L, seq, yaw, os.path.join(raw, L.replace(":", "_"), label, fname))
    enc, tables = {}, {}
    for L in layers:
        clips = {}
        for label, _ in SEQS:
            for fname, _ in FACINGS:
                clips[(label, fname)] = load_frames(os.path.join(raw, L.replace(":", "_"), label, fname))
        enc[L], tables[L] = stylise_layer(L, clips)
        print("%-9s %d materials" % (L, len(tables[L])))

    W, H = CELL
    os.makedirs(a.out, exist_ok=True)
    # 1. each layer's sheet: idle frame 0 + walk frames, rows = facings
    cols = 1 + 5
    lw, lh = W * cols, H * 4
    S = 2
    sheet = Image.new("RGBA", (lw * S + 160, (lh * S + 30) * len(layers)), (255, 255, 255, 255))
    dr = ImageDraw.Draw(sheet)
    for li, L in enumerate(layers):
        y0 = li * (lh * S + 30)
        dr.text((4, y0 + 8), L, fill=(0, 0, 0, 255))
        cell_imgs = []
        for r, (fname, _) in enumerate(FACINGS):
            for c in range(cols):
                label, fi = ("idle", 0) if c == 0 else ("walk", (c - 1) * 2)
                ims, _ = enc[L][(label, fname)]
                one = composite([(ims, tables[L])], fname, fi)
                one[..., 3] = np.where(ims[min(fi, len(ims) - 1)][..., 3] > 0, 255, 0)
                tile = checker(W, H)
                tile.alpha_composite(Image.fromarray(one, "RGBA"))
                sheet.paste(tile.resize((W * S, H * S), Image.NEAREST), (160 + c * W * S, y0 + 24 + r * H * S))
        for r, (fname, _) in enumerate(FACINGS):
            dr.text((100, y0 + 24 + r * H * S + H * S // 2), fname, fill=(80, 80, 80, 255))
    sheet.save(os.path.join(a.out, "layer_sheets.png"))

    # 2. the encoding of one layer: material / band / depth channels
    ims, _ = enc["obj:1127"][("walk", "down")]
    e = ims[4]
    vis = [Image.fromarray(((e[..., 0] + 1) * 60 * (e[..., 3] > 0)).astype(np.uint8)),
           Image.fromarray((e[..., 1] * 90 * (e[..., 3] > 0)).astype(np.uint8)),
           Image.fromarray(np.where(e[..., 3] > 0, 255 - (e[..., 2].astype(int) - 60) * 3, 0).clip(0, 255).astype(np.uint8))]
    ch = Image.new("RGB", (W * 4 * 3 + 40, H * 4 + 20), (255, 255, 255))
    for i, (v, t) in enumerate(zip(vis, ("R: material slot", "G: shade band", "B: depth (near=bright)"))):
        ch.paste(v.convert("RGB").resize((W * 4, H * 4), Image.NEAREST), (i * (W * 4 + 20), 20))
        ImageDraw.Draw(ch).text((i * (W * 4 + 20) + 4, 4), t, fill=(0, 0, 0))
    ch.save(os.path.join(a.out, "layer_encoding_platebody.png"))

    # 3. composites: two appearances, idle + walk, 4 facings; and GIFs
    looks = {"man (default kits)": MAN, "rune warrior": RUNE}
    comp = Image.new("RGBA", (W * 9 * S + 20, (H * 4 * S + 30) * 2), (255, 255, 255, 255))
    dr = ImageDraw.Draw(comp)
    gifdir = os.path.join(a.out, "gif")
    os.makedirs(gifdir, exist_ok=True)
    for gi, (title, look) in enumerate(looks.items()):
        y0 = gi * (H * 4 * S + 30)
        dr.text((4, y0 + 6), title, fill=(0, 0, 0, 255))
        for r, (fname, _) in enumerate(FACINGS):
            walk_n = len(enc[look[0]][("walk", fname)][0])
            for c in range(9):
                label, fi = ("idle", 0) if c == 0 else ("walk", (c - 1) * max(1, walk_n // 8))
                img = composite([(enc[L][(label, fname)][0], tables[L]) for L in look], fname, fi)
                tile = grass(W, H)
                tile.alpha_composite(Image.fromarray(img, "RGBA"))
                comp.paste(tile.resize((W * S, H * S), Image.NEAREST), (10 + c * W * S, y0 + 24 + r * H * S))
            frames = []
            ims0, durs = enc[look[0]][("walk", fname)]
            for fi in range(len(ims0)):
                img = composite([(enc[L][("walk", fname)][0], tables[L]) for L in look], fname, fi)
                tile = grass(W, H)
                tile.alpha_composite(Image.fromarray(img, "RGBA"))
                frames.append(tile.convert("RGB").resize((W * 4, H * 4), Image.NEAREST))
            name = "%s_walk_%s.gif" % ("man" if gi == 0 else "rune", fname)
            frames[0].save(os.path.join(gifdir, name), save_all=True, append_images=frames[1:],
                           duration=(durs + [20] * len(frames))[:len(frames)], loop=0)
    comp.save(os.path.join(a.out, "composites.png"))
    json.dump({L: tables[L] for L in layers}, open(os.path.join(a.out, "material_tables.json"), "w"),
              indent=1)
    print("cell %dx%d, origin %s -> %s" % (W, H, ORIGIN, a.out))


if __name__ == "__main__":
    main()
