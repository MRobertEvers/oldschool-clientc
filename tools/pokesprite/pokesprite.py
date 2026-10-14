#!/usr/bin/env python3
"""OSRS entity -> Pokemon-overworld sprite sheet.

    tools/pokesprite/pokesprite.py --npc 3127 --out build/poke/jad
    tools/pokesprite/pokesprite.py --npc 3106 --out build/poke/man
    tools/pokesprite/pokesprite.py --player --appearance 132235,132199,132151,132273,132405 \\
        --anims idle=808,walk=819,run=824,attack=390 --out build/poke/warrior

Writes sheet.png (one block per animation, one row per facing: down, left,
right, up), sheet.json (cells, anchors, per-frame durations), sheet@2x.png,
gif/<anim>_<facing>.gif previews on a grass tile, and preview.png.

## Two profiles

A Pokemon overworld draws creatures and people differently, so there are two.

  mob       the creature keeps its own proportions; a 3/4 camera (pitch 176)
            looking down on it; sprite height scales with the npc's tile size.
  humanoid  the trainer recipe: near-frontal camera (pitch 112), small sprite,
            and CHIBI proportions -- the head scaled 2.6x about the neck, the
            body shortened and widened, on the posed 3D vertices (ev_sheet
            --chibi), so an arm swinging across the face stays in front of it.
            Eyes are drawn on the face (ev_sheet's head pass says where it is):
            two facing down, one facing sideways, none from behind.

--profile auto picks humanoid for players and for npcs on a human_* rig.

## Pipeline

1. Render (ev_sheet). Every animation x facing at ONE zoom, lift and pitch,
   uncropped, so the model origin is one canvas pixel everywhere. Zoom is
   calibrated so the idle pose lands at the profile's height. Each frame is
   rendered lit (f*.png, real alpha), as a MATERIAL pass (m*.png: every face
   flat in its base HSL16 colour, unlit, untextured), with its DEPTH (d*.png),
   and for a chibi as a HEAD mask (h*.png).

2. Materials come from the model, not from guessing at lit colours. The base
   colours are grouped (near hue + saturation, lightness within a band: an
   artist's four shades of one skin are one material), and each group gets a
   ramp of 1-4 steps by its share of the pixels, within a 15-colour budget --
   the GBA/DS sprite limit with transparency as the 16th.

3. Shading. The lit value is blurred within each material before it is
   banded, which rounds off the flat-shaded polygons OSRS is made of into the
   soft forms a Pokemon sprite has. Band thresholds are quantiles taken once
   over every frame, so nothing shimmers between frames.

4. Downscale by blocks, with small features winning: a material under 2% of
   the subject (eyes, teeth, a buckle) takes any block it covers twice, where
   a majority vote would average it away.

5. The Pokemon rules: hue-shifted ramps (shadows toward violet, highlights
   toward yellow), brighter midtones than OSRS uses, no dithering, no orphan
   pixels (a pixel unlike all four neighbours takes their majority; spurs go,
   pinholes fill), a 1px 4-connected outline in one dark tinted colour rather
   than black, and the same line INSIDE the silhouette wherever one part
   passes in front of another (a depth jump), so a leg in front of the body
   reads as a leg.
"""

import argparse
import colorsys
import glob
import json
import os
import re
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# POKESPRITE_EV_SHEET names another build, e.g. a fresh one linked while a
# long batch still runs the installed binary.
EV_SHEET = os.environ.get("POKESPRITE_EV_SHEET") or os.path.join(ROOT, "tools/entity_viewer/ev_sheet")
CACHE = os.path.join(ROOT, "cache.osrs239")
CONTENT = os.path.join(ROOT, "OSRS-Content/osrs239-content")

FACINGS = [("down", 0), ("left", 512), ("right", 1536), ("up", 1024)]

# One scale per profile, so sizes in the 2D world are relative, not each
# creature's own: the ground is 0.25 sprite pixels per world unit (32 px a
# tile), i.e. zoom 683 at factor 3. Humanoids match the player layers
# (layers.py: zoom 650, the same chibi) so an npc man and a player are one
# size; mobs are the world scale x1.35, Pokemon's exaggeration, so a chicken
# stays small and Jad stays huge. --height still calibrates the old way.
PROFILES = {
    "mob": dict(pitch=176, factor=3, chibi=None, height=None, zoom=round(683 / 1.35)),
    "humanoid": dict(pitch=112, factor=3, chibi="260,17,66,135,4", height=None, zoom=650),
}
MOB_HEIGHT = {1: 32, 2: 44, 3: 56, 4: 64}  # by npc tile size; 5+ -> 72
BUDGET = 15  # colours, transparency is the 16th


# ------------------------------------------------------------------ helpers

_STAMP = []


def renderer_stamp():
    """The identity of the ev_sheet doing the rendering: a hash of the binary,
    so a rebuild that changes nothing invalidates nothing."""
    if not _STAMP:
        import hashlib
        _STAMP.append(hashlib.sha1(open(EV_SHEET, "rb").read()).hexdigest())
    return _STAMP[0]


def rendered(d):
    """Whether `d` holds a finished render by THIS ev_sheet. A render dir is
    reused across runs, and one from an older renderer (before textures, say)
    would otherwise be reused for good."""
    try:
        with open(os.path.join(d, ".ev_sheet")) as f:
            return f.read() == renderer_stamp() and os.path.exists(os.path.join(d, "faces.txt"))
    except OSError:
        return False


def mark_rendered(d):
    with open(os.path.join(d, ".ev_sheet"), "w") as f:
        f.write(renderer_stamp())


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("failed: %s\n%s%s" % (" ".join(cmd), r.stdout, r.stderr))
    return r.stdout, r.stderr


def timing(path):
    out = []
    for line in open(path, encoding="utf8"):
        if line.startswith("#") or not line.strip():
            continue
        out.append(int(line.split()[1]) * 20)
    return out


MIN_FRAME_MS = 80


def decimate(durs):
    """Frame indices to keep and their durations: consecutive frames merge
    until each lasts MIN_FRAME_MS, the total unchanged. A skeletal sequence
    has a frame per 20ms client tick -- 120 for an idle -- which is a film,
    not a sprite; a Pokemon sprite animates near 12 fps."""
    keep, out, acc = [], [], 0
    for i, d in enumerate(durs):
        if acc == 0:
            keep.append(i)
        acc += d
        if acc >= MIN_FRAME_MS:
            out.append(acc)
            acc = 0
    if acc:
        if out:
            out[-1] += acc
        else:
            out.append(acc)
    return keep[:len(out)], out


def compack(kind):
    table = {}
    with open(os.path.join(CONTENT, "configs/all.%s.compack" % kind), "rb") as f:
        for line in f:
            k, _, v = line.decode("utf8", "replace").rstrip("\n").partition("=")
            if k.isdigit():
                table[int(k)] = v
    return table


def npc_record(name):
    with open(os.path.join(CONTENT, "configs/all.npc"), "rb") as f:
        data = f.read().decode("utf8", "replace")
    i = data.find("\n[%s]\n" % name)
    assert i >= 0, name
    j = data.find("\n[", i + 2)
    rec = {}
    for line in data[i:j].splitlines():
        k, eq, v = line.partition("=")
        if eq:
            rec[k.strip()] = v.strip()
    return rec


def npc_combat(name):
    p = os.path.join(CONTENT, "npc_combat", name[0], name + ".combat")
    out = {}
    if os.path.exists(p):
        for line in open(p, encoding="utf8", errors="replace"):
            m = re.match(r"\s*(\w+_anim)\s*=\s*([\w]+)", line)
            if m and m.group(2) not in ("-", "default"):
                out[m.group(1)] = m.group(2)
    return out


def discover(npc_id):
    """npc -> (name, size, [(anim, seq)], humanoid?)"""
    npcs, seqs = compack("npc"), compack("seq")
    by_name = {v: k for k, v in seqs.items()}
    name = npcs[npc_id]
    rec = npc_record(name)
    combat = npc_combat(name)
    # Everything the client can ask for: the movement slot plays the walk for
    # the direction of travel relative to facing, and the turn-on-the-spot
    # sequences while turning.
    want = [("idle", rec.get("readyanim")), ("walk", rec.get("walkanim")),
            ("walk_b", rec.get("walkanim_b")), ("walk_l", rec.get("walkanim_l")),
            ("walk_r", rec.get("walkanim_r")), ("turn_l", rec.get("idleleftanim")),
            ("turn_r", rec.get("idlerightanim")),
            ("run", rec.get("runanim")), ("attack", combat.get("attack_anim")),
            ("defend", combat.get("defend_anim")), ("death", combat.get("death_anim"))]
    anims, used = [], set()
    for label, s in want:
        if s and s != "default" and s in by_name and s not in used:
            anims.append((label, by_name[s]))
            used.add(s)
    ready = rec.get("readyanim", "")
    humanoid = ready.startswith("human_")
    # A creature's own sequence family (lordmagmus_*) holds its other attacks.
    # A shared rig's family (human_*) holds thousands; skip it.
    # The family is what the ready and walk names share up to an underscore:
    # lordmagmus_ready/lordmagmus_walk -> lordmagmus_, and
    # npc_whisperer_01_idle_01/npc_whisperer_01_walk_01 -> npc_whisperer_01_.
    walk = rec.get("walkanim", "")
    common = os.path.commonprefix([ready, walk]) if walk and walk != "default" else ready
    fam = common[:common.rfind("_") + 1] if "_" in common else None
    if fam and not humanoid:
        # A pet's copy and a silent copy are the same motion again.
        members = sorted((k, v) for k, v in seqs.items() if v.startswith(fam)
                         and not v.endswith(("_pet", "_nosound")))
        if len(members) <= 40:
            for k, v in members:
                if v not in used:
                    anims.append((v[len(fam):], k))
                    used.add(v)
    size = int(rec.get("size", "1")) if rec.get("size", "1").isdigit() else 1
    return name, size, anims, humanoid


# ------------------------------------------------------------------ colour

def hsl16_parts(c):
    return (c >> 10) & 63, (c >> 7) & 7, c & 127


def group_materials(colours):
    """[(hsl16, rgb, faces)] -> list of groups (lists of hsl16).

    Single-linkage: near hue and saturation, lightness within a band. Greys
    (saturation <= 1) compare by lightness only, since their hue is noise."""
    cs = [c for c, _, _ in colours]
    parent = {c: c for c in cs}

    def find(c):
        while parent[c] != c:
            parent[c] = parent[parent[c]]
            c = parent[c]
        return c

    def near(a, b):
        ha, sa, la = hsl16_parts(a)
        hb, sb, lb = hsl16_parts(b)
        if abs(la - lb) > 14:
            return False
        if sa <= 1 and sb <= 1:
            return True
        dh = min(abs(ha - hb), 64 - abs(ha - hb))
        return dh <= 2 and abs(sa - sb) <= 2

    for i, a in enumerate(cs):
        for b in cs[i + 1:]:
            if near(a, b):
                parent[find(a)] = find(b)
    groups = {}
    for c in cs:
        groups.setdefault(find(c), []).append(c)
    return list(groups.values())


def to_hls(rgb):
    return colorsys.rgb_to_hls(*(v / 255 for v in rgb))


def from_hls(h, l, s):
    return tuple(int(round(v * 255)) for v in colorsys.hls_to_rgb(h % 1.0, min(max(l, 0), 1),
                                                                  min(max(s, 0), 1)))


def toward(h, target, amount):
    """Rotate hue h (0-1) toward target by at most amount, the short way."""
    d = (target - h + 0.5) % 1.0 - 0.5
    return h + max(-amount, min(amount, d))


CUTS = {2: [0.35], 3: [0.28, 0.84], 4: [0.20, 0.52, 0.87]}
STEP_L = {1: [0.0], 2: [-0.08, 0.10], 3: [-0.14, 0.0, 0.15], 4: [-0.18, -0.05, 0.08, 0.21]}


def ramp(base_rgb, steps):
    """Pokemon ramp: brighter midtone, shadows toward violet, highlights toward
    yellow; greys get a cool shadow and a warm highlight instead."""
    h, l, s = to_hls(base_rgb)
    mid = 0.20 + 0.75 * l ** 0.72
    grey = s < 0.12
    out = []
    offs = STEP_L[steps]
    for i, dl in enumerate(offs):
        k = (i - (steps - 1) / 2) / max(1, (steps - 1) / 2) if steps > 1 else 0
        if grey:
            hh, ss = (0.64, 0.10) if k < 0 else (0.13, 0.06 * k)
        else:
            hh = toward(h, 0.72, 0.035 * -k) if k < 0 else toward(h, 0.15, 0.035 * k)
            # OSRS runs near full saturation; a Pokemon midtone sits nearer
            # 0.7, shadows a little richer, highlights paler.
            ss = min(s, 0.72) * (1.08 if k < 0 else 1.0 - 0.22 * k)
        out.append(from_hls(hh, mid + dl, ss))
    hh0, ll0, ss0 = to_hls(out[0])
    outline = from_hls(toward(hh0, 0.74, 0.04), max(0.05, ll0 * 0.42), min(1, ss0 * 1.1 + 0.1))
    return out, outline


# ------------------------------------------------------------------ raster

N4 = [(-1, 0), (1, 0), (0, -1), (0, 1)]


def shifts(a, fill):
    p = np.pad(a, 1, constant_values=fill)
    H, W = a.shape
    return [p[1 + dy:1 + dy + H, 1 + dx:1 + dx + W] for dy, dx in N4]


def majority(nb, valid, k):
    votes = np.stack([sum(((n == c) & ok) for n, ok in zip(nb, valid)) for c in range(k)], -1)
    return votes.argmax(-1), votes.max(-1)


def clean(mat, band, nmat, feature):
    NONE = 0
    for _ in range(2):
        op = mat != NONE
        n_op = sum(n for n in shifts(op, False))
        nb = shifts(mat, NONE)
        fillm, _ = majority(nb, [n != NONE for n in nb], nmat)
        nbb = shifts(band, 0)
        bfill, _ = majority(nbb, [n != NONE for n in nb], 4)
        mat = np.where(op & (n_op <= 1), NONE, mat)
        band = np.where(~op & (n_op >= 3), bfill, band)
        mat = np.where(~op & (n_op >= 3), fillm, mat)
    nb = shifts(mat, NONE)
    same = sum((n == mat) for n in nb)
    m2, votes = majority(nb, [n != NONE for n in nb], nmat)
    orphan = (mat != NONE) & ~feature[mat] & (same == 0) & (votes >= 2)
    mat = np.where(orphan, m2, mat)
    for _ in range(2):
        nb = shifts(mat, NONE)
        nbb = shifts(band, -1)
        valid = [n == mat for n in nb]
        same = sum(((b == band) & ok) for b, ok in zip(nbb, valid))
        b2, votes = majority(nbb, valid, 4)
        band = np.where((mat != NONE) & (same == 0) & (votes >= 2), b2, band)
    return mat, band


def smooth_value(val, mat, sigma):
    """Blur the lit value within each material (normalised convolution), so a
    flat-shaded polygon's hard step becomes a rounded falloff."""
    out = np.zeros_like(val)
    for m in np.unique(mat):
        if m == 0:
            continue
        mask = (mat == m).astype(np.float32)
        num = ndimage.gaussian_filter(val * mask, sigma)
        den = ndimage.gaussian_filter(mask, sigma)
        sel = mask > 0
        out[sel] = num[sel] / np.maximum(den[sel], 1e-6)
    return out


def pixel_depth(face, vs, tri):
    """Each pixel's camera z on the face that won it; 1/z is linear in screen
    space. No z-buffer: the face comes from the painter's own draw."""
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


def downscale(mat, val, alpha, f, feature, nmat, thin=False):
    """One output pixel per f x f block. A block is covered when most of it
    is -- or, `thin`, when a third of it is: an arrow shaft or a spell's
    streak is a pixel or two wide in the render and would otherwise vanish."""
    H, W = mat.shape
    h, w = H // f, W // f

    def blocks(a):
        return a[:h * f, :w * f].reshape(h, f, w, f).transpose(0, 2, 1, 3).reshape(h, w, f * f)

    mb, vb, ab = blocks(mat), blocks(val), blocks(alpha)
    cnt = np.stack([(mb == m).sum(-1) for m in range(nmat)], -1)
    covered = (ab >= 128).sum(-1)
    opaque = covered * 3 >= f * f if thin else covered * 2 > f * f
    solid = cnt.copy()
    solid[..., 0] = 0
    out = solid.argmax(-1)
    feat = np.where(feature[None, None, :], cnt, 0)
    feat[..., 0] = 0
    fwin = feat.max(-1) >= 2
    out = np.where(fwin, feat.argmax(-1), out)
    out = np.where(opaque, out, 0)
    sel = mb == out[..., None]
    v = np.where(sel.any(-1), (vb * sel).sum(-1) / np.maximum(sel.sum(-1), 1), 0)
    return out.astype(np.int64), v


def paint_eyes(img, head, facing, rgb):
    """Pokemon trainer eyes: 1x2 dark pixels on the face rows of the head.

    The face rows are where the head mask is at least 60% of its widest, which
    skips a plume or a hat brim above and the chin taper below."""
    if facing == "up" or not head.any():
        return
    ys, xs = np.nonzero(head)
    widths = np.bincount(ys - ys.min())
    rows = np.nonzero(widths >= 0.6 * widths.max())[0] + ys.min()
    top, bot = rows.min(), rows.max()
    if bot - top < 3:
        return
    ey = int(round(top + 0.52 * (bot - top)))
    line = np.nonzero(head[ey])[0]
    if len(line) < 3:
        return
    x0, x1 = line.min(), line.max()
    w = x1 - x0 + 1
    if facing == "down":
        off = max(1, int(round(w * 0.2)))
        cols = [int((x0 + x1) // 2 - off), int((x0 + x1 + 1) // 2 + off)]
    elif facing == "left":
        cols = [int(x0 + max(1, round(w * 0.22)))]
    else:
        cols = [int(x1 - max(1, round(w * 0.22)))]
    for c in cols:
        for y in (ey, ey + 1):
            if 0 <= y < img.shape[0] and head[min(y, head.shape[0] - 1), c]:
                img[y, c, :3] = rgb


# ------------------------------------------------------------------ main

def stylise(raw, anims, facings, f, zoom, side, height, chibi, thin=False):
    """The Pokemon stage for one subject's renders: <raw>/<label>_<facing>/ for
    every (label, seq) in anims and (facing, yaw) in facings, all at one zoom
    on one canvas. Returns ({(label, facing): ([PIL frames], durations)},
    {label: crop box on the canvas}, colour count). `thin` keeps one-pixel
    detail through the downscale (see downscale)."""
    # ---- materials: the model's own base colours (ev_sheet faces.txt) -----
    # Every run renders the same model, so one face table serves them all:
    # face i has the same ID colour, base colour and head flag everywhere.
    first = os.path.join(raw, "%s_%s" % (anims[0][0], facings[0][0]))
    frows = [l.split() for l in open(first + "/faces.txt") if not l.startswith("#")]
    nf = len(frows)
    idrgb = np.full(1 << 24, -1, np.int32)
    tri = np.zeros((nf, 3), np.int64)
    base_hsl = np.zeros(nf, np.int64)
    head_face = np.zeros(nf, bool)
    nfaces, rgb_of = {}, {}
    for r in frows:
        i = int(r[0])
        idrgb[int(r[1], 16)] = i
        base_hsl[i] = int(r[2])
        head_face[i] = r[4] == "1"
        tri[i] = (int(r[5]), int(r[6]), int(r[7]))
        nfaces[int(r[2])] = nfaces.get(int(r[2]), 0) + 1
        rgb_of[int(r[2])] = tuple(bytes.fromhex(r[3]))
    colours = [(c, rgb_of[c], n) for c, n in nfaces.items()]
    groups = group_materials(colours)

    # ---- load; one box per animation ---------------------------------------
    margin = 2 * f
    cx = side // 2

    def up(v):
        return -(-v // f) * f

    clips, boxes = {}, {}
    for label, _seq in anims:
        x0 = y0 = 10 ** 9
        x1 = y1 = -1
        for fname, _yaw in facings:
            d = os.path.join(raw, "%s_%s" % (label, fname))
            lit = [np.asarray(Image.open(p).convert("RGBA")) for p in sorted(glob.glob(d + "/f*.png"))]
            ids = [np.asarray(Image.open(p).convert("RGBA")) for p in sorted(glob.glob(d + "/i*.png"))]
            vtx = [np.fromfile(p, np.int32).reshape(-1, 3).astype(np.float64)
                   for p in sorted(glob.glob(d + "/v*.bin"))]
            assert lit and len(lit) == len(ids) == len(vtx), d
            for im in lit:
                ys, xs = np.nonzero(im[..., 3] >= 128)
                if len(xs):
                    x0, x1 = min(x0, xs.min()), max(x1, xs.max() + 1)
                    y0, y1 = min(y0, ys.min()), max(y1, ys.max() + 1)
            durs = timing(d + "/frames.txt")
            keep, durs = decimate(durs)
            clips[(label, fname)] = ([lit[i] for i in keep], [ids[i] for i in keep],
                                     [vtx[i] for i in keep], durs)
        half = up(max(cx - x0, x1 - cx) + margin)
        top = up(cx - (y0 - margin))
        bot = up((y1 + margin) - cx)
        boxes[label] = (cx - half, cx - top, cx + half, cx + bot)

    # Material id per pixel (0 = none), from the face that won it in the
    # painter's own draw. Pixels are counted over every frame to weigh each
    # group by what is actually seen, not by face count.
    gid = np.zeros(1 << 16, np.int64)
    for i, grp in enumerate(groups):
        for c in grp:
            gid[c] = i + 1
    counts = np.zeros(len(groups) + 1, np.int64)
    full = {}
    for key, (lit, ids, vtx, durs) in clips.items():
        bx0, by0, bx1, by1 = boxes[key[0]]
        rows = []
        for li, ii, vs in zip(lit, ids, vtx):
            ii = ii.astype(np.int64)
            face = idrgb[(ii[..., 0] << 16) | (ii[..., 1] << 8) | ii[..., 2]]
            face[(ii[..., 3] < 128) | (li[..., 3] < 128)] = -1
            vs = vs.copy()
            vs[:, 2] += zoom  # screen z is relative to the model centre, at z == zoom
            z = pixel_depth(face, vs, tri)[by0:by1, bx0:bx1]
            face = face[by0:by1, bx0:bx1]
            li = li[by0:by1, bx0:bx1]
            m = np.where(face >= 0, gid[base_hsl[np.maximum(face, 0)]], 0)
            hi = (face >= 0) & head_face[np.maximum(face, 0)] if chibi else None
            counts += np.bincount(m.ravel(), minlength=len(groups) + 1)
            rows.append((m, li, z, hi))
        full[key] = (rows, durs)

    # Budget: steps by share; merge the closest pair while over budget.
    share = counts[1:] / max(1, counts[1:].sum())
    base = []
    for grp in groups:
        w = np.array([n for c, _, n in colours if c in grp], np.float64)
        rgbs = np.array([rgb for c, rgb, _ in colours if c in grp], np.float64)
        base.append(tuple((rgbs * w[:, None]).sum(0) / w.sum()))

    def steps_for(s):
        return 4 if s > 0.25 else 3 if s > 0.08 else 2 if s > 0.015 else 1

    alive = [i for i in range(len(groups)) if share[i] > 0]
    remap = {i: i for i in range(len(groups))}
    cap = {}
    while True:
        # One outline colour for the whole sprite, as Gen 4/5 overworld
        # sprites use; a 4th shade goes before two garments become one.
        need = sum(min(steps_for(share[i]), cap.get(i, 4)) for i in alive) + 1
        if need <= BUDGET or len(alive) <= 1:
            break
        fours = [i for i in alive if min(steps_for(share[i]), cap.get(i, 4)) == 4]
        if fours:
            cap[max(fours, key=lambda i: share[i])] = 3
            continue
        best = None
        for i in alive:
            for j in alive:
                if i < j:
                    d = sum((p - q) ** 2 for p, q in zip(base[i], base[j]))
                    if best is None or d < best[0]:
                        best = (d, i, j)
        _, i, j = best
        wi, wj = share[i], share[j]
        base[i] = tuple((p * wi + q * wj) / (wi + wj) for p, q in zip(base[i], base[j]))
        share[i] += share[j]
        share[j] = 0
        alive.remove(j)
        for k, v in remap.items():
            if v == j:
                remap[k] = i
    # Compact ids: 0 none, 1..K materials.
    order = sorted(alive, key=lambda i: -share[i])
    newid = {g: n + 1 for n, g in enumerate(order)}
    K = len(order) + 1
    idmap = np.zeros(len(groups) + 1, np.int64)
    for g in range(len(groups)):
        # A colour no frame ever shows has no pixels to map; any id will do.
        idmap[g + 1] = newid.get(remap[g], 1)
    steps = np.zeros(K, np.int64)
    feature = np.zeros(K, bool)
    pal = np.zeros((K, 4, 3), np.uint8)
    outline = np.zeros((K, 3), np.uint8)
    for g in order:
        k = newid[g]
        steps[k] = min(steps_for(share[g]), cap.get(g, 4))
        feature[k] = share[g] < 0.02
        r, o = ramp(base[g], steps[k])
        for s_, c in enumerate(r):
            pal[k, s_] = c
        outline[k] = o
        print("  material %d: %5.1f%%  %d step%s  base #%02x%02x%02x%s"
              % (k, 100 * share[g], steps[k], "" if steps[k] == 1 else "s",
                 *[int(v) for v in base[g]], "  (feature)" if feature[k] else ""))

    # Nothing on screen in any frame: an invisible subject (OSRS has locs and
    # npcs that are only there to be clicked). Say so; there is no sprite.
    if K == 1:
        raise ValueError("nothing visible in any frame")
    # The sprite's one outline: the dominant material's, which is what most of
    # the silhouette borders.
    line_rgb = outline[1].copy()

    # Overlap lines: a depth jump bigger than a tenth of the subject's height.
    world_h = height * f * zoom / 512.0
    jump = 0.10 * world_h

    # ---- downscale + shading cuts ------------------------------------------
    sigma = 1.2 * f
    low = {}
    pool = [[] for _ in range(K)]
    for key, (rows, durs) in full.items():
        out = []
        for m, li, z, hi in rows:
            m = idmap[m]
            v = li[..., :3].max(-1).astype(np.float32) / 255
            v = smooth_value(v, m, sigma)
            mm, vv = downscale(m, v, li[..., 3], f, feature, K, thin)
            # Far side of a depth jump: the line sits on what is behind, the
            # way the outer outline sits on the background.
            op = m != 0
            zz = np.where(op, z, 0)
            behind = np.zeros(m.shape, bool)
            for nz, nop in zip(shifts(zz, 0), shifts(op, False)):
                behind |= op & nop & (nz < zz - jump)
            H, W = mm.shape
            cnt = behind[:H * f, :W * f].reshape(H, f, W, f).sum((1, 3))
            line = (cnt >= 2) & (mm != 0) & ~feature[mm]
            head = None
            if hi is not None:
                head = hi[:H * f, :W * f].reshape(H, f, W, f).sum((1, 3)) * 2 > f * f
            out.append((mm, vv, line, head))
            for k in range(1, K):
                pool[k].append(vv[mm == k])
        low[key] = (out, durs)
    cuts = {}
    for k in range(1, K):
        vals = np.concatenate(pool[k])
        n = steps[k]
        # Uneven on purpose: a Pokemon form is mostly its base tone, a solid
        # shadow, and a small highlight -- not four equal slices.
        cuts[k] = np.quantile(vals, CUTS[n]) if n > 1 and vals.size else []

    # ---- stylise -------------------------------------------------------------
    frames = {}
    for key, (out, durs) in low.items():
        ims = []
        for mm, vv, line, head in out:
            band = np.zeros(mm.shape, np.int64)
            for k in range(1, K):
                sel = mm == k
                band[sel] = np.searchsorted(cuts[k], vv[sel])
            mm, band = clean(mm, band, K, feature)
            band = np.minimum(band, steps[mm] - 1).clip(0)
            img = np.zeros(mm.shape + (4,), np.uint8)
            img[..., :3] = pal[mm, band]
            img[..., 3] = np.where(mm != 0, 255, 0)
            # Overlap lines, thinned: drop a line pixel whose line neighbour
            # below or right already carries it, so two-pixel bands go to one.
            ln = line & (mm != 0)
            nbl = shifts(ln, False)
            ln = ln & ~(nbl[1] & nbl[3])
            img[ln, :3] = line_rgb
            if head is not None:
                paint_eyes(img, head & (mm != 0), key[1], line_rgb)
            nb = shifts(mm, 0)
            touch = (mm == 0) & sum((n != 0) for n in nb).astype(bool)
            img[touch, :3] = line_rgb
            img[touch, 3] = 255
            ims.append(Image.fromarray(img, "RGBA"))
        frames[key] = (ims, durs)
    ncol = len({p[:3] for ims, _ in frames.values() for im in ims
                for p in im.get_flattened_data() if p[3]})

    return frames, boxes, ncol


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--npc", type=int)
    g.add_argument("--player", action="store_true")
    ap.add_argument("--appearance")
    ap.add_argument("--wear", type=int, action="append", default=[])
    ap.add_argument("--gender", type=int, default=0)
    ap.add_argument("--anims", help="name=seq,... overriding discovery")
    ap.add_argument("--profile", choices=("auto", "mob", "humanoid"), default="auto")
    ap.add_argument("--height", type=int, help="idle sprite height in px")
    ap.add_argument("--pitch", type=int)
    ap.add_argument("--chibi", help="head%%,frac%%,body%%,width%% (humanoid default "
                    "260,17,72,125); 0 disables")
    ap.add_argument("--out", required=True)
    ap.add_argument("--skip-render", action="store_true")
    ap.add_argument("--store", help="also write each animation into this PokeSprite store "
                    "(npc/<type>/<seq>), e.g. assets/pokesprite")
    a = ap.parse_args()

    if a.player:
        name, size, anims, humanoid = "player", 1, [("idle", 808), ("walk", 819),
                                                    ("run", 824)], True
    else:
        name, size, anims, humanoid = discover(a.npc)
    if a.anims:
        anims = [(k, int(v)) for k, v in (p.split("=") for p in a.anims.split(","))]
    assert anims, "no animations found for %s; pass --anims" % name
    profile = a.profile if a.profile != "auto" else ("humanoid" if humanoid else "mob")
    P = dict(PROFILES[profile])
    if a.pitch is not None:
        P["pitch"] = a.pitch
    if a.chibi is not None:
        P["chibi"] = a.chibi if a.chibi != "0" else None
    height = a.height or P["height"] or MOB_HEIGHT.get(size, 72)  # --height / legacy only
    f = P["factor"]
    print("%s: profile %s, %dpx, anims %s" % (name, profile, height,
                                             " ".join("%s=%d" % x for x in anims)))

    subject = ["--player", "--gender", str(a.gender)] if a.player else ["--npc", str(a.npc)]
    if a.player and a.appearance:
        subject += ["--appearance", a.appearance]
    for w in a.wear:
        subject += ["--wear", str(w)]
    common = ["--rev", "osrs239", CACHE] + subject + ["--pitch", str(P["pitch"]),
                                                      "--frame-height", "200", "--full"]
    if P["chibi"]:
        common += ["--chibi", P["chibi"]]

    raw = os.path.join(a.out, "_raw")
    os.makedirs(a.out, exist_ok=True)
    cal_path = os.path.join(a.out, "_calibration.json")
    if not a.skip_render:
        # ---- calibrate: idle height at a probe zoom -> zoom for the target
        shutil.rmtree(raw, ignore_errors=True)
        probe = os.path.join(raw, "_probe")
        os.makedirs(probe)
        # The probe sizes the canvas; the profile's zoom sets the scale, unless
        # --height asked for a calibrated one.
        z0 = P["zoom"] if (P["zoom"] and not a.height) else 1000
        side0 = 1600
        run([EV_SHEET] + common + ["--seq", str(anims[0][1]), "--zoom", str(z0),
                                   "--side", str(side0), "--fit", "1", "--out", probe])
        hmax, reach = 0, 0
        for p in glob.glob(probe + "/f*.png"):
            al = np.asarray(Image.open(p))[..., 3]
            ys, xs = np.nonzero(al >= 128)
            if len(ys):
                hmax = max(hmax, ys.max() - ys.min() + 1)
                reach = max(reach, abs(xs - side0 // 2).max(), abs(ys - side0 // 2).max())
        if P["zoom"] and not a.height:
            zoom = z0
            height = int(max(1, hmax // f))
            # Room for the probe's reach, and for an attack that reaches
            # further than the idle pose did.
            side = int(min(2048, max(600, reach * 3)))
        else:
            zoom = int(z0 * hmax / (height * f))
            side = min(2048, max(600, height * f * 5))
        json.dump({"zoom": int(zoom), "side": int(side), "height": int(height)},
                  open(cal_path, "w"))
        for label, seq in anims:
            for fname, yaw in FACINGS:
                d = os.path.join(raw, "%s_%s" % (label, fname))
                os.makedirs(d)
                _o, err = run([EV_SHEET] + common + ["--seq", str(seq), "--yaw", str(yaw),
                                                      "--zoom", str(zoom), "--side", str(side),
                                                      "--fit", "1", "--materials", "--out", d])
                if "canvas edge" in err:
                    print("warning: %s %s touches the canvas edge (clipped)" % (label, fname))
    cal = json.load(open(cal_path))
    zoom, side = cal["zoom"], cal["side"]
    height = cal.get("height", height)

    frames, boxes, ncol = stylise(raw, anims, FACINGS, f, zoom, side, height, P["chibi"])
    cx = side // 2

    # ---- pack ------------------------------------------------------------------
    def cell(label):
        b = boxes[label]
        return tuple(int(v) for v in ((b[2] - b[0]) // f, (b[3] - b[1]) // f,
                                      (cx - b[0]) // f, (cx - b[1]) // f))

    sw = max(cell(l)[0] * len(frames[(l, "down")][0]) for l, _ in anims)
    sh = sum(cell(l)[1] * len(FACINGS) for l, _ in anims)
    sheet = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
    meta = {"subject": name, "profile": profile, "facings": [x for x, _ in FACINGS],
            "source": {"npc": a.npc, "pitch": P["pitch"], "zoom": zoom, "factor": f,
                       "chibi": P["chibi"], "colours": ncol}, "anims": []}
    y = 0
    for label, seq in anims:
        cw, ch, ax, ay = cell(label)
        rows = []
        for fname, _yaw in FACINGS:
            ims, durs = frames[(label, fname)]
            for i, im in enumerate(ims):
                sheet.alpha_composite(im, (i * cw, y))
            rows.append({"facing": fname, "y": y, "frames": len(ims),
                         "durations_ms": durs[:len(ims)]})
            y += ch
        meta["anims"].append({"name": label, "seq": seq, "cell": [cw, ch],
                              "origin": [ax, ay], "rows": rows})
    idle = frames[(anims[0][0], "down")][0]
    # Where it stands: the lowest drawn row of its idle pose. An idle that
    # draws nothing (an invisible npc) stands at its origin.
    boxes_seen = [im.getchannel("A").getbbox() for im in idle]
    boxes_seen = [b for b in boxes_seen if b]
    feet = max(b[3] for b in boxes_seen) if boxes_seen else meta["anims"][0]["origin"][1] + 1
    ground_dy = int(feet) - 1 - meta["anims"][0]["origin"][1]
    for an in meta["anims"]:
        an["ground"] = [an["origin"][0], an["origin"][1] + ground_dy]
    if a.store:
        assert a.npc is not None, "the store keys npc sheets by npc type; --player has none"
        from store import Store, npc_key
        st = Store(a.store)
        for an in meta["anims"]:
            rows = [frames[(an["name"], fname)][0] for fname, _ in FACINGS]
            st.write_rgba(npc_key(a.npc, an["seq"]), rows, an["rows"][0]["durations_ms"],
                          an["origin"], an["ground"])
        st.close()
        print("stored %d sheets in %s" % (len(meta["anims"]), a.store))
    sheet.save(os.path.join(a.out, "sheet.png"))
    sheet.resize((sw * 2, sh * 2), Image.NEAREST).save(os.path.join(a.out, "sheet@2x.png"))
    json.dump(meta, open(os.path.join(a.out, "sheet.json"), "w"), indent=1)

    # ---- previews --------------------------------------------------------------
    gifdir = os.path.join(a.out, "gif")
    shutil.rmtree(gifdir, ignore_errors=True)
    os.makedirs(gifdir)
    idle_w = max([b[2] - b[0] for b in (im.getchannel("A").getbbox() for im in idle) if b] or [16])
    rad = idle_w * 0.42

    def on_grass(im, gx, gy):
        cw, ch = im.size
        bg = Image.new("RGBA", (cw, ch), (136, 200, 112, 255))
        dr = ImageDraw.Draw(bg)
        for yy in range(0, ch, 8):
            for xx in range((yy // 8) % 2 * 8, cw, 16):
                dr.rectangle([xx, yy, xx + 7, yy + 7], fill=(124, 190, 100, 255))
        dr.ellipse([gx - rad, gy - rad * 0.30, gx + rad, gy + rad * 0.22], fill=(72, 128, 64, 255))
        bg.alpha_composite(im)
        return bg.convert("RGB")

    S = 3
    tiles = []
    for an in meta["anims"]:
        gx, gy = an["ground"]
        for row in an["rows"]:
            ims, durs = frames[(an["name"], row["facing"])]
            bgs = [on_grass(im, gx, gy).resize((im.width * S, im.height * S), Image.NEAREST)
                   for im in ims]
            d = (durs + [20] * len(ims))[:len(ims)]
            bgs[0].save(os.path.join(gifdir, "%s_%s.gif" % (an["name"], row["facing"])),
                        save_all=True, append_images=bgs[1:], duration=d, loop=0)
            tiles.append(on_grass(ims[len(ims) // 3], gx, gy))
    tw = max(t.width for t in tiles)
    th = max(t.height for t in tiles)
    pv = Image.new("RGB", (tw * 4, th * len(anims)), (136, 200, 112))
    for i, t in enumerate(tiles):
        pv.paste(t, ((i % 4) * tw, (i // 4) * th))
    pv.resize((pv.width * 2, pv.height * 2), Image.NEAREST).save(os.path.join(a.out, "preview.png"))
    for an in meta["anims"]:
        print("  %-8s cell %3dx%-3d %2d frames" % (an["name"], *an["cell"], an["rows"][0]["frames"]))
    print("sheet %dx%d, %d colours -> %s" % (sw, sh, ncol, a.out))


if __name__ == "__main__":
    main()
