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
F = 3                      # humanoid pixelation factor: ~48px figures
ZOOM = 650                 # one humanoid scale for every layer
PITCH = 112
CHIBI = "260,17,66,135,4"  # head x2.6, body 66% tall, 135% wide, limbs inflated 4 units
SIDE = 800
# One cell for every humanoid layer: the anchor (canvas centre) is the same
# pixel in all of them, which is what lets the client stack any set.
# Measured over every layer of the example looks (a staff and a bow reach
# furthest): left 153, right 148, top 135, bottom 116 canvas px with the
# limbs inflated, plus the outline's pixel.
HALF_W, TOP, BOT = 53 * F, 47 * F, 40 * F
BOX = (SIDE // 2 - HALF_W, SIDE // 2 - TOP, SIDE // 2 + HALF_W, SIDE // 2 + BOT)
CELL = ((BOX[2] - BOX[0]) // F, (BOX[3] - BOX[1]) // F)
ORIGIN = (HALF_W // F, TOP // F)
DEPTH_NONE = 255
# Where a humanoid stands: the feet's bottom row in the cell (measured over the
# default body's feet kit, idle and walk), on the anchor column. The client
# puts this pixel on the actor's tile position.
GROUND = (HALF_W // F, 78)
# One set of light thresholds for EVERY humanoid layer ever generated, so a
# layer rendered today composites with one rendered last month. Taken from the
# pooled light of the tuned example run (inflate 4); see shared_cuts().
LIGHT_CUTS = {2: np.array([0.53]), 3: np.array([0.48, 0.79])}

# The client's design recolours (engine/entity_model_build.c, reference
# ClientPlayer.recol1d / recol2d): index 0 is the identity colour the kit
# models ship with.
RECOL = {
    "hair": [6798, 107, 10283, 16, 4797, 7744, 5799, 4634, 33697, 22433, 2983, 54193],
    "torso": [8741, 12, 64030, 43162, 7735, 8404, 1701, 38430, 24094, 10153, 56621, 4783,
              1341, 16578, 35003, 25239],
    "legs": [25238, 8742, 12, 64030, 43162, 7735, 8404, 1701, 38430, 24094, 10153, 56621,
             4783, 1341, 16578, 35003],
    "feet": [4626, 11146, 6439, 12, 4758, 10270],
    "skin": [4550, 4537, 5681, 5673, 5790, 6806, 8076, 4574],
}
RECOL2D = [9104, 10275, 7595, 3610, 7975, 8526, 918, 38802, 24466, 10145, 58654, 5027, 1457,
           16565, 34991, 25486]
SKIN_IDENTITY = RECOL["skin"][0]
# wearpos -> male kit body part (server: an obj there empties that kit slot).
WEARPOS_PART = {4: 2, 6: 3, 7: 5, 8: 0, 9: 4, 10: 6, 11: 1}


def hsl16_rgb(c):
    """The engine's HSL16 table (brightness 0.8), to within a level."""
    h = ((c >> 10) & 63) / 64 + 0.0078125
    s = ((c >> 7) & 7) / 8 + 0.0625
    l = (c & 127) / 128
    import colorsys
    return tuple(int(round((v ** 0.8) * 255)) for v in colorsys.hls_to_rgb(h % 1, l, s))


def design_recolour(hair=0, torso=0, legs=0, feet=0, skin=0):
    out = {}
    for part, idx in (("hair", hair), ("torso", torso), ("legs", legs), ("feet", feet),
                      ("skin", skin)):
        if idx:
            out[RECOL[part][0]] = RECOL[part][idx]
    if torso:
        out[RECOL2D[0]] = RECOL2D[torso]
    return out


def depth_q(z):
    return np.clip(np.round(128 + (z - ZOOM) / 2.0), 0, 254)


def render(layer, seq, yaw, out, gender="m"):
    os.makedirs(out, exist_ok=True)
    ps.run([ps.EV_SHEET, "--rev", "osrs239", ps.CACHE, "--player",
            "--gender", "1" if gender == "f" else "0", "--layer", layer,
            "--seq", str(seq), "--yaw", str(yaw), "--zoom", str(ZOOM), "--side", str(SIDE),
            "--fit", "1", "--frame-height", "200", "--full", "--pitch", str(PITCH),
            "--chibi", CHIBI, "--materials", "--out", out])
    ps.mark_rendered(out)


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
        full = np.asarray(Image.open(fp).convert("RGBA"))
        outside = full[..., 3].copy()
        outside[y0:y1, x0:x1] = 0
        assert not outside.any(), "%s/f%03d.png draws outside the humanoid cell" % (d, n)
        lit = full[y0:y1, x0:x1]
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


def min_neighbour(z):
    out = np.full(z.shape, np.inf)
    for s in ps.shifts(np.where(np.isinf(z), 1e18, z), 1e18):
        out = np.minimum(out, s)
    return np.where(out >= 1e17, np.inf, out)


def prepare_layer(name, clips, head_kit):
    """Materials, and per frame (material, LIGHT, depth) at sprite resolution.

    Light is lit brightness over the face's own base brightness: how lit the
    surface is, independent of what colour it is. Banding that (with cuts
    shared by every layer) gives a helm and the platebody under it one light
    source, where banding raw brightness per layer gave each its own."""
    _, _, base_hsl, base_rgb, head = next(iter(clips.values()))
    base_v = np.maximum(base_rgb.max(1) / 255.0, 0.04)
    seen = {}
    for frames, *_ in clips.values():
        for _lit, face, _z in frames:
            f = face[face >= 0]
            for c, n in zip(*np.unique(base_hsl[f], return_counts=True)):
                seen[int(c)] = seen.get(int(c), 0) + int(n)
    colours = [(c, tuple(int(v) for v in base_rgb[np.nonzero(base_hsl == c)[0][0]]), n)
               for c, n in seen.items()]
    # A design-recolour identity colour is its own material: the player's
    # colour replaces exactly it, and grouping it with a neighbouring shade
    # would recolour that shade too.
    identity = {RECOL[p][0] for p in RECOL} | {RECOL2D[0]}
    plain = [c for c in colours if c[0] not in identity]
    groups = (ps.group_materials(plain) if plain else []) + [[c[0]] for c in colours
                                                             if c[0] in identity]
    gid = {c: i + 1 for i, g in enumerate(groups) for c in g}
    K = len(groups) + 1
    tot = sum(n for _, _, n in colours) or 1
    share = np.zeros(K)
    for c, _, n in colours:
        share[gid[c]] += n / tot
    steps = np.array([0] + [3 if s > 0.25 else 2 if s > 0.04 else 1 for s in share[1:]])
    # A head kit keeps no small details: eyebrows and a mouth are three dark
    # pixels on a chibi face, which reads as noise. Eyes are drawn instead.
    feature = np.array([False] + [(not head_kit) and s < 0.04 for s in share[1:]])
    table = []
    for k in range(1, K):
        grp = groups[k - 1]
        w = np.array([n for c, _, n in colours if c in grp], float)
        rgbs = np.array([rgb for c, rgb, _ in colours if c in grp], float)
        table.append({"base_rgb": [int(v) for v in (rgbs * w[:, None]).sum(0) / w.sum()],
                      "hsl16": sorted(grp), "steps": int(steps[k]),
                      "head": bool(head_kit), "skin": SKIN_IDENTITY in grp})
    hsl_to_mat = np.zeros(1 << 16, np.int64)
    for c, k in gid.items():
        hsl_to_mat[c] = k
    sigma = 1.6 * F
    low, pool = {}, []
    for key, (frames, durs, *_rest) in clips.items():
        out = []
        for lit, face, z in frames:
            fc = np.maximum(face, 0)
            m = np.where(face >= 0, hsl_to_mat[base_hsl[fc]], 0)
            light = np.where(face >= 0, lit[..., :3].max(-1) / 255.0 / base_v[fc], 0)
            light = ps.smooth_value(np.clip(light, 0, 2.5).astype(np.float32), m, sigma)
            mm, ll = ps.downscale(m, light, lit[..., 3], F, feature, K)
            H, W = mm.shape
            zb = np.where(m > 0, z, np.inf)[:H * F, :W * F].reshape(H, F, W, F)
            mb = m[:H * F, :W * F].reshape(H, F, W, F)
            zz = np.where(mb == mm[:, None, :, None], zb, np.inf).min((1, 3))
            out.append((mm, ll, zz))
            pool.append(ll[mm > 0])
        low[key] = (out, durs)
    return {"low": low, "table": table, "steps": steps, "feature": feature, "K": K}, pool


def encode_layer(prep, cuts):
    K, steps, feature = prep["K"], prep["steps"], prep["feature"]
    enc = {}
    for key, (out, durs) in prep["low"].items():
        ims = []
        for mm, ll, zz in out:
            band = np.zeros(mm.shape, np.int64)
            for k in range(1, K):
                sel = mm == k
                band[sel] = np.searchsorted(cuts[steps[k]], ll[sel]) if steps[k] > 1 else 0
            mm, band = ps.clean(mm, band, K, feature)
            band = np.minimum(band, steps[mm] - 1).clip(0)
            # A pixel the cleanup filled has no depth of its own.
            zz = np.where(np.isinf(zz) & (mm > 0), min_neighbour(zz), zz)
            img = np.zeros(mm.shape + (4,), np.uint8)
            img[..., 0] = np.where(mm > 0, mm - 1, 0)
            img[..., 1] = band
            img[..., 2] = np.where(mm > 0, depth_q(np.where(np.isinf(zz), ZOOM, zz)), DEPTH_NONE)
            img[..., 3] = np.where(mm > 0, 255, 0)
            ims.append(img)
        enc[key] = (ims, durs)
    return enc


def shared_cuts(pool):
    """Light thresholds from a pool of layers -- how LIGHT_CUTS was measured.
    Generation uses LIGHT_CUTS, not this, so separate runs agree."""
    vals = np.concatenate(pool)
    return {2: np.quantile(vals, ps.CUTS[2]), 3: np.quantile(vals, ps.CUTS[3])}


# ---------------------------------------------------------------- compositor

def ramp_h(base_rgb, steps):
    """A player-colour ramp. The mob ramp (pokesprite.ramp) lifts midtones
    hard, which suits a creature's near-black reds but turns brown hair blonde
    and washes skin out; a player's design colour has to stay that colour.
    Same hue shift and saturation cap, a small lift."""
    h, l, s = ps.to_hls(base_rgb)
    # Dark colours lift a little more than light ones: rune armour stays
    # readable, skin does not wash out.
    mid = min(0.88, l + 0.02 + 0.09 * (1 - l))
    grey = s < 0.12
    out = []
    # A narrow highlight: front-lit hair is mostly highlight, and a wide one
    # turned brown hair blonde.
    step_l = {1: [0.0], 2: [-0.08, 0.07], 3: [-0.13, 0.0, 0.09]}[steps]
    for i, dl in enumerate(step_l):
        k = (i - (steps - 1) / 2) / max(1, (steps - 1) / 2) if steps > 1 else 0
        if grey:
            hh, ss = (0.64, 0.10) if k < 0 else (0.13, 0.06 * k)
        else:
            hh = ps.toward(h, 0.72, 0.03 * -k) if k < 0 else ps.toward(h, 0.15, 0.03 * k)
            ss = min(s, 0.75) * (1.06 if k < 0 else 1.0 - 0.2 * k)
        out.append(ps.from_hls(hh, mid + dl, ss))
    return out


def paint_eyes(img, skin, head, facing, rgb):
    """Eyes where the FACE is, drawn over whatever head kit is there.

    Placement comes from the skin of the head (the face rows: at least 60% of
    its widest); the eyes are then painted on any head-kit pixel, so a fringe
    of hair over one eye does not take it away. Never on anything but a head
    kit: a helm's visor stays a visor."""
    if facing == "up" or not skin.any():
        return
    ys, xs = np.nonzero(skin)
    widths = np.bincount(ys - ys.min())
    rows = np.nonzero(widths >= 0.6 * widths.max())[0] + ys.min()
    top, bot = rows.min(), rows.max()
    if bot - top < 2:
        return
    ey = int(round(top + 0.38 * (bot - top)))
    hx = np.nonzero(head[ey])[0]
    if len(hx) < 3:
        return
    x0, x1 = hx.min(), hx.max()
    w = x1 - x0 + 1
    if facing == "down":
        off = max(1, int(round(w * 0.18)))
        cols = [(x0 + x1) // 2 - off, (x0 + x1 + 1) // 2 + off]
    elif facing == "left":
        cols = [x0 + max(1, int(round(w * 0.25)))]
    else:
        cols = [x1 - max(1, int(round(w * 0.25)))]
    for c in cols:
        for y in (ey, ey + 1):
            if 0 <= y < img.shape[0] and head[y, c]:
                img[y, c, :3] = rgb

def darken(rgb, k):
    h, l, s = ps.to_hls(rgb)
    return ps.from_hls(ps.toward(h, 0.74, 0.03), l * k, min(1.0, s * 1.1))


def composite(layers, facing, frame_index, recolour=None):
    """layers: list of (encoded frames for this clip, material table). The client's job."""
    W, H = CELL
    depth = np.full((H, W), 999, np.int64)
    rgb = np.zeros((H, W, 3), np.uint8)
    shadow = np.zeros((H, W, 3), np.uint8)   # each pixel's material's darkest step
    head = np.zeros((H, W), bool)
    skin = np.zeros((H, W), bool)
    lid = np.full((H, W), -1, np.int64)
    for li, (frames, table) in enumerate(layers):
        ims = frames[min(frame_index, len(frames) - 1)]
        ramps = []
        for m in table:
            base = tuple(m["base_rgb"])
            if recolour:
                for h in m["hsl16"]:
                    if h in recolour:
                        base = hsl16_rgb(recolour[h])
            ramps.append(ramp_h(base, max(1, m["steps"])))
        cov = ims[..., 3] > 0
        win = cov & (ims[..., 2].astype(np.int64) < depth)
        depth = np.where(win, ims[..., 2], depth)
        lid = np.where(win, li, lid)
        for y, x in zip(*np.nonzero(win)):
            ms, band = ims[y, x, 0], ims[y, x, 1]
            r = ramps[ms]
            rgb[y, x] = r[min(band, len(r) - 1)]
            shadow[y, x] = r[0]
            head[y, x] = table[ms]["head"]
            skin[y, x] = table[ms]["skin"]
    op = lid >= 0
    img = np.zeros((H, W, 4), np.uint8)
    img[..., :3] = rgb
    img[..., 3] = np.where(op, 255, 0)
    # Overlap lines: only where one LAYER passes in front of another by a real
    # margin, on the far side, in a dark shade of the material it sits on.
    behind = np.zeros(op.shape, bool)
    for nd, nl, nop in zip(ps.shifts(depth, 999), ps.shifts(lid, -1), ps.shifts(op, False)):
        behind |= op & nop & (nl != lid) & (depth - nd > 6)
    for y, x in zip(*np.nonzero(behind)):
        img[y, x, :3] = darken(tuple(int(v) for v in shadow[y, x]), 0.62)
    # Outline: a dark tint of whatever it borders, the darkest neighbour.
    lum = shadow.astype(np.int64).sum(-1)
    nb_op = ps.shifts(op, False)
    touch = ~op & sum(nb_op).astype(bool)
    pick = np.full(op.shape, -1, np.int64)
    best = np.full(op.shape, 10 ** 9, np.int64)
    for i, (nop, nl) in enumerate(zip(nb_op, ps.shifts(lum, 10 ** 9))):
        take = touch & nop & (nl < best)
        best = np.where(take, nl, best)
        pick = np.where(take, i, pick)
    shifted = [ps.shifts(shadow[..., c], 0) for c in range(3)]
    for y, x in zip(*np.nonzero(touch)):
        i = pick[y, x]
        src = tuple(int(shifted[c][i][y, x]) for c in range(3))
        img[y, x, :3] = darken(src, 0.40)
        img[y, x, 3] = 255
    # Eyes: on the face only -- skin of a head kit, so a helm visor has none.
    paint_eyes(img, head & skin & op, head & op, facing, (28, 20, 36))
    return img


# ---------------------------------------------------------------- looks

def idk_bodypart(kit):
    """The kit's body part: 0..6 male, 7..13 female (the same seven slots, +7)."""
    txt = open(os.path.join(ps.CONTENT, "configs/all.idk"), "rb").read().decode("utf8", "replace")
    i = txt.index("[idk_%d]\n" % kit)
    j = txt.find("\n[", i + 2)
    for line in txt[i:j].splitlines():
        if line.startswith("bodypart="):
            return int(line.split("=")[1])
    raise AssertionError("idk %d has no bodypart" % kit)


def obj_wearpos(obj):
    names = ps.compack("obj")
    txt = open(os.path.join(ps.CONTENT, "configs/all.obj"), "rb").read().decode("utf8", "replace")
    i = txt.index("[%s]\n" % names[obj])
    j = txt.find("\n[", i + 2)
    out = []
    for line in txt[i:j].splitlines():
        k, _, v = line.partition("=")
        if k in ("wearpos", "wearpos2", "wearpos3") and v.strip().isdigit():
            out.append(int(v))
    return out


def look(kits, objs):
    """What the server sends: the objs, plus each kit whose body slot no obj
    covers (wearpos, wearpos2, wearpos3). Layers in that order."""
    covered = {WEARPOS_PART[p] for o in objs for p in obj_wearpos(o) if p in WEARPOS_PART}
    keep = ["kit:%d" % k for k in kits if idk_bodypart(k) % 7 not in covered]
    return keep + ["obj:%d" % o for o in objs]


# ---------------------------------------------------------------- example

def generate(layer_seqs, raw, store_root, gender="m"):
    """Render, stylise and store player layers: {"kit:18": [808, 819], ...}.
    `gender` "m" or "f": the reference body and the obj wear models follow it.

    What the generator runs for a region or for io_server's misses. Renders
    only what is not already on disk under `raw`."""
    from store import Store, layer_key
    preps = {}
    raw = os.path.join(raw, gender)
    for L, seqs in layer_seqs.items():
        for seq in seqs:
            for fname, yaw in FACINGS:
                d = os.path.join(raw, L.replace(":", "_"), str(seq), fname)
                if not ps.rendered(d):
                    render(L, seq, yaw, d, gender)
        clips = {(str(seq), fname): load_frames(os.path.join(raw, L.replace(":", "_"), str(seq),
                                                             fname))
                 for seq in seqs for fname, _ in FACINGS}
        kind, ident = L.split(":")
        head_kit = kind == "kit" and idk_bodypart(int(ident)) % 7 in (0, 1)
        preps[L], _ = prepare_layer(L, clips, head_kit)
    st = Store(store_root)
    n = 0
    for L, seqs in layer_seqs.items():
        enc = encode_layer(preps[L], LIGHT_CUTS)
        kind, ident = L.split(":")
        for seq in seqs:
            rows = [enc[(str(seq), fname)][0] for fname, _ in FACINGS]
            st.write_layer(layer_key(kind, int(ident), seq, gender), rows,
                           enc[(str(seq), "down")][1], preps[L]["table"])
            n += 1
    st.close()
    for L in layer_seqs:
        ps.drop_raw(os.path.join(raw, L.replace(":", "_")))
    return n


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
    ap.add_argument("--store", help="also write every layer x sequence into this PokeSprite "
                    "store (layer/m/<kit|obj>/<id>/<seq>), e.g. assets/pokesprite")
    a = ap.parse_args()
    SEQS = [("idle", 808), ("walk", 819)]
    BODY = [3, 14, 18, 26, 33, 36, 42]      # hair 3, clean-shaven jaw 14, torso, arms, hands, legs, feet
    LOOKS = [
        ("trainer", look(BODY, []), design_recolour(hair=6, torso=2, legs=4, feet=0, skin=0)),
        ("wizard", look([5] + BODY[1:], [579, 577, 1011, 1381]),
         design_recolour(hair=5, feet=3, skin=7)),
        ("ranger", look(BODY, [1167, 1129, 1095, 1061, 1059, 841]),
         design_recolour(torso=8, skin=2)),
        ("rune warrior", look(BODY, [1163, 1127, 1079, 1201, 1333]), design_recolour(skin=1)),
    ]
    layers = list(dict.fromkeys(L for _, lk, _ in LOOKS for L in lk))
    raw = os.path.join(a.out, "_raw")
    if not a.skip_render:
        for L in layers:
            for label, seq in SEQS:
                for fname, yaw in FACINGS:
                    d = os.path.join(raw, L.replace(":", "_"), label, fname)
                    if not ps.rendered(d):
                        render(L, seq, yaw, d)
    preps, pool = {}, []
    for L in layers:
        clips = {(label, fname): load_frames(os.path.join(raw, L.replace(":", "_"), label, fname))
                 for label, _ in SEQS for fname, _ in FACINGS}
        head_kit = L.startswith("kit:") and idk_bodypart(int(L[4:])) % 7 in (0, 1)
        preps[L], p = prepare_layer(L, clips, head_kit)
        pool += p
    cuts = LIGHT_CUTS
    print("pooled light cuts this run (LIGHT_CUTS is what is used):",
          {k: np.round(v, 3).tolist() for k, v in shared_cuts(pool).items()})
    enc = {L: encode_layer(preps[L], cuts) for L in layers}
    tables = {L: preps[L]["table"] for L in layers}

    if a.store:
        from store import Store, layer_key
        st = Store(a.store)
        for L in layers:
            kind, ident = L.split(":")
            for label, seq in SEQS:
                rows = [enc[L][(label, fname)][0] for fname, _ in FACINGS]
                durs = enc[L][(label, "down")][1]
                st.write_layer(layer_key(kind, int(ident), seq), rows, durs, tables[L])
        st.close()
        print("stored %d layer sheets in %s" % (len(layers) * len(SEQS), a.store))

    W, H = CELL
    S = 3
    os.makedirs(a.out, exist_ok=True)
    gifdir = os.path.join(a.out, "gif")
    shutil.rmtree(gifdir, ignore_errors=True)
    os.makedirs(gifdir)
    comp = Image.new("RGBA", (W * 9 * S + 20, (H * 4 * S + 30) * len(LOOKS)), (255, 255, 255, 255))
    dr = ImageDraw.Draw(comp)
    for gi, (title, lk, rec) in enumerate(LOOKS):
        y0 = gi * (H * 4 * S + 30)
        dr.text((4, y0 + 6), "%s  [%s]" % (title, " ".join(lk)), fill=(0, 0, 0, 255))
        for r, (fname, _) in enumerate(FACINGS):
            n = len(enc[lk[0]][("walk", fname)][0])
            for c in range(9):
                label, fi = ("idle", 0) if c == 0 else ("walk", (c - 1) * max(1, n // 8))
                img = composite([(enc[L][(label, fname)][0], tables[L]) for L in lk], fname, fi, rec)
                tile = grass(W, H)
                tile.alpha_composite(Image.fromarray(img, "RGBA"))
                comp.paste(tile.resize((W * S, H * S), Image.NEAREST), (10 + c * W * S, y0 + 24 + r * H * S))
            ims0, durs = enc[lk[0]][("walk", fname)]
            frames = []
            for fi in range(len(ims0)):
                img = composite([(enc[L][("walk", fname)][0], tables[L]) for L in lk], fname, fi, rec)
                tile = grass(W, H)
                tile.alpha_composite(Image.fromarray(img, "RGBA"))
                frames.append(tile.convert("RGB").resize((W * 4, H * 4), Image.NEAREST))
            frames[0].save(os.path.join(gifdir, "%s_walk_%s.gif" % (title.split()[0], fname)),
                           save_all=True, append_images=frames[1:],
                           duration=(durs + [20] * len(frames))[:len(frames)], loop=0)
    comp.save(os.path.join(a.out, "composites.png"))
    json.dump({"cell": CELL, "origin": ORIGIN, "cuts": {k: v.tolist() for k, v in cuts.items()},
               "tables": tables}, open(os.path.join(a.out, "material_tables.json"), "w"), indent=1)
    print("cell %dx%d, origin %s -> %s" % (W, H, ORIGIN, a.out))


if __name__ == "__main__":
    main()
