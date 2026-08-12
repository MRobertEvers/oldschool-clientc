#!/usr/bin/env python3
"""Contact sheet of every model in the rs2012 QBD/TD lane, with true alpha.

The point is translucency. A model rendered once against one background cannot
be judged - a dark ghost on a dark background just looks dark. So every model is
rendered TWICE, against black and against white, and the per-pixel alpha is
recovered exactly:

    out = a*src + (1-a)*bg   =>   a = 1 - (out_white - out_black)/255

The recovered RGBA is then composited over a checkerboard, so anything the bake
made see-through reads as see-through at a glance. Each tile is labelled with
the model id, the name the lane's configs give it, and `a<90%` - the share of
covered pixels that are not solid.

    python tools/rs2012_qbd_model_sheet.py --models DIR --out sheet.png

Nothing here writes the lane or packs a cache.
"""

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageDraw, ImageFont

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LANE = "ported/rs2012_qbd_td"


# --- naming ----------------------------------------------------------------
def load_names(tree):
    """dest model id -> a human label, from the lane's pack + text configs."""
    lane = os.path.join(tree, LANE)
    pack = os.path.join(lane, "pack", "7_models.pack")
    src_of = {}   # dest id -> source model id
    for line in open(pack, encoding="utf-8", errors="replace"):
        line = line.strip()
        if "=" not in line:
            continue
        dest, path = line.split("=", 1)
        m = re.search(r"rs2012_model_(\d+)", path)
        if dest.isdigit() and m:
            src_of[int(dest)] = int(m.group(1))

    # Any config value mentioning a dest model id lends that block's name.
    names = {}
    cfgdir = os.path.join(lane, "configs")
    for fn in sorted(os.listdir(cfgdir)):
        if not fn.endswith((".loc", ".npc", ".obj", ".spotanim", ".inv")):
            continue
        block, label = None, None
        kind = fn.rsplit(".", 1)[1]
        for line in open(os.path.join(cfgdir, fn), encoding="utf-8", errors="replace"):
            line = line.strip()
            if line.startswith("["):
                block, label = line[1:-1], None
                continue
            if line.startswith("name="):
                label = line[5:]
                continue
            if "=" not in line or line.startswith("//"):
                continue
            key, val = line.split("=", 1)
            if not re.match(r"^(model|models|model\d+|shape\d+|manwear|womanwear"
                            r"|manhead|womanhead|inv_model)", key):
                continue
            for num in re.findall(r"\d+", val):
                n = int(num)
                if n in src_of:
                    text = label or (block or "")
                    text = re.sub(r"^rs2012_", "", text)
                    names.setdefault(n, []).append((kind, text))
    out = {}
    for dest, src in src_of.items():
        refs = names.get(dest, [])
        seen, parts = set(), []
        for kind, text in refs:
            t = f"{text}"
            if t.lower() in seen:
                continue
            seen.add(t.lower())
            parts.append(t)
        out[src] = {
            "dest": dest,
            "src": src,
            "label": (parts[0] if parts else "-")[:26],
            "kinds": sorted({k for k, _ in refs}),
            "refs": len(refs),
        }
    return out


# --- rendering -------------------------------------------------------------
def render_rgba(view, path, tile, angles, extra, near):
    """Render on black and on white; recover exact per-pixel RGBA."""
    outs = []
    with tempfile.TemporaryDirectory() as td:
        for bg in ("000000", "ffffff"):
            dst = os.path.join(td, f"{bg}.bmp")
            cmd = [view, "--model", path, "--out", dst, "--tile", str(tile),
                   "--angles", str(angles), "--near", str(near), "--bg", bg] + extra
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0 or not os.path.exists(dst):
                return None, (r.stderr or r.stdout or "").strip()[:200]
            outs.append(np.asarray(Image.open(dst).convert("RGB")).astype(np.float64))
    black, white = outs
    # (1-a)*255 per channel; average the channels to damp rounding.
    alpha = np.clip(1.0 - (white - black).mean(axis=2) / 255.0, 0.0, 1.0)
    with np.errstate(divide="ignore", invalid="ignore"):
        rgb = np.where(alpha[..., None] > 1e-3, black / np.maximum(alpha[..., None], 1e-3), 0.0)
    return (np.clip(rgb, 0, 255).astype(np.uint8), alpha), None


def checker(w, h, size=8, a=(74, 78, 86), b=(96, 101, 110)):
    yy, xx = np.mgrid[0:h, 0:w]
    m = (((xx // size) + (yy // size)) % 2)[..., None]
    return (np.array(a) * (1 - m) + np.array(b) * m).astype(np.uint8)


def compose(rgba, size=8):
    rgb, alpha = rgba
    h, w = alpha.shape
    bg = checker(w, h, size).astype(np.float64)
    out = rgb.astype(np.float64) * alpha[..., None] + bg * (1.0 - alpha[..., None])
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))


def stats(rgba):
    _, alpha = rgba
    covered = alpha > 0.02
    n = int(covered.sum())
    if n == 0:
        return {"covered": 0, "soft_frac": 0.0, "mean_alpha": 1.0, "ghost_frac": 0.0}
    a = alpha[covered]
    return {
        "covered": n,
        "soft_frac": float((a < 0.90).mean()),
        "ghost_frac": float((a < 0.50).mean()),
        "mean_alpha": float(a.mean()),
    }


# --- sheet -----------------------------------------------------------------
def get_font(size):
    for name in ("consola.ttf", "arial.ttf", "seguisb.ttf", "DejaVuSans.ttf"):
        for base in (r"C:\Windows\Fonts", "/usr/share/fonts/truetype/dejavu", "/Library/Fonts"):
            p = os.path.join(base, name)
            if os.path.exists(p):
                try:
                    return ImageFont.truetype(p, size)
                except Exception:
                    pass
    return ImageFont.load_default()


def build_sheet(cells, cols, tile, title):
    lab_h = 26
    pad = 3
    cw, ch = tile + pad * 2, tile + lab_h + pad
    rows = (len(cells) + cols - 1) // cols
    head = 46
    sheet = Image.new("RGB", (cols * cw, head + rows * ch), (26, 28, 33))
    draw = ImageDraw.Draw(sheet)
    f_title = get_font(22)
    f_id = get_font(12)
    f_sub = get_font(11)
    draw.text((10, 12), title, font=f_title, fill=(235, 238, 245))

    for i, c in enumerate(cells):
        x = (i % cols) * cw
        y = head + (i // cols) * ch
        if c["image"] is not None:
            sheet.paste(c["image"], (x + pad, y + pad))
        else:
            draw.rectangle([x + pad, y + pad, x + pad + tile, y + pad + tile],
                           fill=(60, 30, 30))
            draw.text((x + pad + 6, y + pad + 6), "FAIL", font=f_id, fill=(255, 140, 140))
        st = c["stats"]
        soft = st["soft_frac"]
        # colour the badge by how much of the model is see-through
        if soft >= 0.35:
            col = (255, 96, 96)
        elif soft >= 0.10:
            col = (255, 190, 90)
        elif soft > 0.0:
            col = (150, 200, 255)
        else:
            col = (140, 146, 158)
        ty = y + pad + tile + 1
        draw.text((x + pad + 1, ty), f"{c['src']}", font=f_id, fill=(214, 220, 230))
        badge = f"a<90 {soft*100:4.0f}%" if st["covered"] else "empty"
        bw = draw.textlength(badge, font=f_sub)
        draw.text((x + pad + tile - bw, ty), badge, font=f_sub, fill=col)
        draw.text((x + pad + 1, ty + 13), c["label"][:22], font=f_sub, fill=(150, 156, 168))
    return sheet


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--models", required=True, help="dir of baked .ob3 models")
    ap.add_argument("--out", required=True, help="output PNG")
    ap.add_argument("--tree", default=os.path.join(REPO, "build/osrs-content-rs2012/osrs239-content"))
    ap.add_argument("--view", default=os.path.join(REPO, "src/build_win64_opt/rs2012_model_view.exe"))
    ap.add_argument("--tile", type=int, default=128)
    ap.add_argument("--angles", type=int, default=1)
    # The QBD parts stand 386 units tall and the engine's fast cull throws the
    # whole model away at the stock near plane of 50; the audit harness uses 49
    # for the same reason.
    ap.add_argument("--near", type=int, default=49)
    ap.add_argument("--cols", type=int, default=24)
    ap.add_argument("--title", default=None)
    ap.add_argument("--jobs", type=int, default=max(4, (os.cpu_count() or 8) - 2))
    ap.add_argument("--sort", choices=["id", "soft"], default="id",
                    help="id keeps tiles aligned across attempts; soft ranks the "
                         "most see-through models first")
    ap.add_argument("--only-soft", action="store_true",
                    help="keep only models that carry any translucency")
    ap.add_argument("--stats-out", default=None)
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()

    view = args.view
    if not os.path.exists(view):
        view = view[:-4]
    if not os.path.exists(view):
        sys.exit("no rs2012_model_view; make -C src rs2012-model-view")

    names = load_names(args.tree)
    files = sorted(f for f in os.listdir(args.models) if f.endswith(".ob3"))
    items = []
    for fn in files:
        m = re.search(r"rs2012_model_(\d+)", fn)
        src = int(m.group(1)) if m else -1
        info = names.get(src, {"dest": 0, "label": "-"})
        items.append({"src": src, "file": os.path.join(args.models, fn), **info})
    items.sort(key=lambda it: it.get("dest") or 10**9)
    if args.limit:
        items = items[:args.limit]

    # --lane-textures wants the TREE root; it appends the lane path itself.
    extra = []
    if os.path.exists(os.path.join(args.tree, LANE, "textures", "texture_0.texture")):
        extra += ["--textures", "--lane-textures", args.tree]

    done = [0]
    total = len(items)

    def work(it):
        rgba, err = render_rgba(view, it["file"], args.tile, args.angles, extra, args.near)
        done[0] += 1
        if done[0] % 50 == 0:
            print(f"  rendered {done[0]}/{total}", file=sys.stderr, flush=True)
        if rgba is None:
            return {**it, "image": None, "stats": {"covered": 0, "soft_frac": 0.0,
                                                   "ghost_frac": 0.0, "mean_alpha": 1.0},
                    "error": err}
        return {**it, "image": compose(rgba), "stats": stats(rgba), "error": None}

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        cells = list(ex.map(work, items))

    if args.only_soft:
        cells = [c for c in cells if c["stats"]["soft_frac"] > 0.0]
    if args.sort == "soft":
        cells.sort(key=lambda c: -c["stats"]["soft_frac"])

    soft_models = sum(1 for c in cells if c["stats"]["soft_frac"] > 0.001)
    heavy = sum(1 for c in cells if c["stats"]["soft_frac"] >= 0.35)
    title = args.title or os.path.basename(args.models)
    title = (f"{title}   -   {len(cells)} models   |   "
             f"{soft_models} carry translucency   |   {heavy} are >=35% see-through   "
             f"(alpha recovered from black/white pair, shown over checkerboard)")
    sheet = build_sheet(cells, args.cols, args.tile, title)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)) or ".", exist_ok=True)
    sheet.save(args.out)
    print(f"wrote {args.out}  ({sheet.width}x{sheet.height})  "
          f"{soft_models} translucent, {heavy} heavy")

    if args.stats_out:
        with open(args.stats_out, "w") as fh:
            json.dump([{k: v for k, v in c.items() if k != "image"} for c in cells],
                      fh, indent=1)
        print(f"wrote {args.stats_out}")

    fails = [c for c in cells if c.get("error")]
    if fails:
        print(f"{len(fails)} model(s) failed to render:", file=sys.stderr)
        for c in fails[:10]:
            print(f"  {c['src']}: {c['error']}", file=sys.stderr)


if __name__ == "__main__":
    main()
