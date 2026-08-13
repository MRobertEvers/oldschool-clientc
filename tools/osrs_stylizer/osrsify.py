#!/usr/bin/env python3
"""osrsify.py — search driver that pushes an imported high-poly model toward
the OSRS look, judged by the trained models instead of pixel diffs.

Two regimes over the same judge:

  reduce  (regime B): rig-preserving vertex removal via rs2012_model_decimate.
          A ladder of target fractions x random seeds, each candidate carrying
          a JSON mapping that ports the old animations onto the reduced mesh.
  sculpt  (regime A): bounded geometry moves via rs2012_model_nudge — per-group
          inflate plus a final coarse-grid quantize — explored by simulated
          annealing.

The judge has three parts, all render-based through rs2012_model_view (the
real engine renderer, so both classes of the style model saw identical
rasterization):

  style     osrs_engine_judge.pt LOGIT MARGIN (logit[osrs] - logit[highpoly]),
            averaged over the covered views of a 4-angle bind render. Margin,
            not softmax: the probability squashes the whole interesting range
            to ~0 for dense imports, the margin keeps the gradient alive.
  identity  content_preserver.pt embedding distance between baseline and
            candidate renders (0..100). Purpose-trained that palette baking
            and moderate decimation are FINE while mesh collapse is not.
  animation every sampled frame of every sequence is prescanned with
            --pose-stats-only and re-rendered under the BASELINE's shared
            camera (one camera per sequence fitted to the union of every
            baseline pose, exactly the audit tool's protocol) so coverage and
            identity are comparable across frames and candidates. Heuristic
            gates: frame must decode, posed/bind size ratio must stay near the
            baseline's, silhouette coverage per view must stay inside a band,
            and the pose-render identity score must clear a floor.

Pixel comparison is deliberately NOT part of the fitness — the audit tool's
verdict from pixel scoring is "always enable zbuffering", which optimizes
nothing about style.

Typical runs (from tools/osrs_stylizer):

  python osrsify.py --preset qbd --regime reduce --time-budget 7200
  python osrsify.py --preset qbd --regime sculpt --time-budget 3600

Progress and every candidate's verdict stream to stdout; results.json in the
run directory is rewritten after every candidate, so long runs can be
inspected (or killed) at any point. The best passing candidate's files are
copied to <run>/best/.
"""

import argparse
import json
import math
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from PIL import Image           # noqa: E402
import torch                    # noqa: E402
import torch.nn as nn           # noqa: E402
from torchvision import models as tv_models, transforms  # noqa: E402

from preserver_scorer import get_identity_score  # noqa: E402

BIN_DIR = os.path.join(ROOT, "src", "build_win64_opt")
DEFAULT_VIEWER = os.path.join(BIN_DIR, "rs2012_model_view.exe")
DEFAULT_DECIMATOR = os.path.join(BIN_DIR, "rs2012_model_decimate.exe")
DEFAULT_NUDGE = os.path.join(BIN_DIR, "rs2012_model_nudge.exe")
DEFAULT_DEFIGHT = os.path.join(BIN_DIR, "rs2012_model_defight.exe")
DEFAULT_PRIO = os.path.join(BIN_DIR, "rs2012_face_priorities.exe")
DEFAULT_BAKE = os.path.join(BIN_DIR, "rs2012_material_bake.exe")
PRESETS = os.path.join(ROOT, "tools", "rs2012_backport_audit", "presets.json")
STYLE_CKPT = os.path.join(HERE, "models", "osrs_engine_judge.pt")

# Same constants and output grammar as tools/rs2012_backport_audit/audit.py —
# the viewer's pose-stats contract is shared, the parser is deliberately
# reimplemented here so this tool has no import edge into the audit driver.
PROJ_SCALE = 512
DEPTH_BUDGET = 15000
RE_POSE_BIND = re.compile(
    r"pose stats: bind x (-?\d+)\.\.(-?\d+) y (-?\d+)\.\.(-?\d+) z (-?\d+)\.\.(-?\d+)")
RE_POSE_POSED = re.compile(
    r"pose stats: posed x (-?\d+)\.\.(-?\d+) y (-?\d+)\.\.(-?\d+) z (-?\d+)\.\.(-?\d+)"
    r" moved (\d+)/(\d+)")


def log(msg):
    print(msg, flush=True)


def run(cmd, timeout=300):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    return (r.stdout or "") + (r.stderr or ""), r.returncode


# ------------------------------------------------------- liveness + pausing

HEARTBEAT = {"state": "starting"}


def write_heartbeat(o):
    """Drop <run>/heartbeat.json so the dashboard can tell a live search from
    a dead one without scanning the process table. Swallows every error — a
    liveness beacon must not be able to kill the run it reports on."""
    path = os.path.join(o.out_dir, "heartbeat.json")
    tmp = path + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump({"pid": os.getpid(), "ts": round(time.time(), 1),
                       "state": HEARTBEAT["state"]}, f)
        for _ in range(5):
            try:
                os.replace(tmp, path)
                return
            except PermissionError:  # dashboard mid-read (Windows)
                time.sleep(0.05)
    except OSError:
        pass


def start_heartbeat(o):
    """Beat every 5s from a daemon thread, so the beacon stays fresh even
    while the main loop sits inside a long render/judge subprocess."""
    HEARTBEAT["state"] = "running"
    write_heartbeat(o)

    def beat():
        while HEARTBEAT["state"] != "done":
            time.sleep(5.0)
            write_heartbeat(o)
    threading.Thread(target=beat, daemon=True).start()


def pause_gate(o):
    """Cooperative pause: while <run>/PAUSE exists (the dashboard's pause
    button drops it), idle here between candidates. Paused time is refunded
    to the deadline so pausing does not eat the time budget. Returns the
    seconds spent paused (0.0 when the flag was absent)."""
    flag = os.path.join(o.out_dir, "PAUSE")
    if not os.path.isfile(flag):
        return 0.0
    log("paused — hit resume in the dashboard (or delete %s) to continue"
        % flag)
    HEARTBEAT["state"] = "paused"
    t0 = time.time()
    while os.path.isfile(flag):
        time.sleep(2.0)
    paused = time.time() - t0
    o.deadline += paused
    HEARTBEAT["state"] = "running"
    log("resumed after %.0fs pause (time budget extended to compensate)"
        % paused)
    return paused


def absify(path):
    return path if os.path.isabs(path) else os.path.join(ROOT, path)


# ---------------------------------------------------------------- seq config

def parse_seq_config(path, wanted):
    """rs2012.seq: [name] sections with frame=packed,len lines."""
    sections = {}
    current = None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                current = line[1:-1]
                sections.setdefault(current, [])
            elif current and line.startswith("frame="):
                body = line[len("frame="):].split(",")
                try:
                    sections[current].append(int(body[0]))
                except ValueError:
                    continue
    out = {}
    for name in wanted:
        if name not in sections:
            raise SystemExit("osrsify: sequence %r not in %s" % (name, path))
        out[name] = sections[name]
    return out


def sample_frames(n, cap):
    """Even spread of frame indices including first and last."""
    if n <= cap:
        return list(range(n))
    return sorted({round(i * (n - 1) / (cap - 1)) for i in range(cap)})


def solve_framing(radius, tile):
    """Audit's framing: zoom that fills 90% of a half-tile while keeping the
    far side of the model inside the depth table."""
    zoom = 1.0
    distance = radius * PROJ_SCALE / (0.45 * tile * zoom)
    if distance + radius > DEPTH_BUDGET:
        if radius >= DEPTH_BUDGET - 200:
            return None, None       # unframeable, skip the sequence
        zoom = radius * PROJ_SCALE / (0.45 * tile * (DEPTH_BUDGET - radius))
        distance = radius * PROJ_SCALE / (0.45 * tile * zoom)
    return round(zoom, 3), int(distance)


def diag(box):
    return math.sqrt(sum((hi - lo) ** 2 for lo, hi in box))


# -------------------------------------------------------------- style judge

class StyleJudge:
    """The engine-only classifier, read for its logits."""

    PREPROCESS = transforms.Compose([
        transforms.Resize(256),
        transforms.CenterCrop(224),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
    ])

    def __init__(self, checkpoint_path):
        self.device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        ckpt = torch.load(checkpoint_path, map_location=self.device)
        m = tv_models.resnet18(weights=None)
        m.fc = nn.Linear(m.fc.in_features, 2)
        m.load_state_dict(ckpt["state_dict"])
        self.model = m.to(self.device).eval()
        self.osrs = ckpt["class_to_idx"]["osrs"]

    def margin(self, image_path):
        """logit[osrs] - logit[highpoly]; positive means 'reads as OSRS'."""
        with Image.open(image_path) as im:
            batch = self.PREPROCESS(im.convert("RGB")).unsqueeze(0).to(self.device)
        with torch.no_grad():
            logits = self.model(batch)[0]
        return float(logits[self.osrs] - logits[1 - self.osrs])


# ------------------------------------------------------------------ renders

def render_sheet(o, model_paths, out_bmp, angles, extra=()):
    cmd = [o.viewer]
    for m in model_paths:
        cmd += ["--model", m]
    cmd += ["--out", out_bmp, "--angles", str(angles), "--pitch", str(o.pitch),
            "--tile", str(o.tile), "--bg", o.bg] + list(extra)
    if o.zbuffer:
        cmd.append("--zbuffer")
    out, rc = run(cmd, timeout=o.viewer_timeout)
    if rc != 0 or not os.path.isfile(out_bmp):
        return None, out
    return out_bmp, out


def split_tiles(o, sheet_bmp, prefix):
    """Cut the sheet into per-yaw tiles; keep only covered views.
    Returns {yaw: (png_path, coverage)} — the same coverage metric the judge
    corpus was filtered with, so scored tiles stay in-distribution."""
    tiles = {}
    with Image.open(sheet_bmp) as sheet:
        w, h = sheet.size
        for i in range(w // h):
            tile = sheet.crop((i * h, 0, (i + 1) * h, h)).convert("RGB")
            gray = tile.convert("L")
            cov = sum(1 for v in gray.getdata() if abs(v - 128) > 6) / float(h * h)
            if cov < 0.02:
                continue
            p = "%s_y%d.png" % (prefix, i)
            tile.save(p)
            tiles[i] = (p, cov)
    return tiles


def render_region(o, model_paths, region, out_bmp):
    """Close-up render of one rig-label region under its fixed camera. Big
    models defeat whole-body judging — a face that is 9 pixels tall in the
    full frame can be deleted without any score noticing. These renders give
    the identity judge the same region at full tile resolution."""
    extra = ["--focus", "%d,%d,%d" % tuple(region["focus"]),
             "--radius", str(region["radius"])]
    return render_sheet(o, model_paths, out_bmp, o.angles, extra)


def prescan_frame(o, model_paths, packed):
    """--pose-stats-only for one packed frame: bind/posed boxes or None."""
    cmd = [o.viewer]
    for m in model_paths:
        cmd += ["--model", m]
    cmd += ["--cache", o.cache, "--frames", str(packed), "--pose-stats-only"]
    out, rc = run(cmd, timeout=o.viewer_timeout)
    bind = RE_POSE_BIND.search(out)
    posed = RE_POSE_POSED.search(out)
    if rc != 0 or not posed or not bind:
        return None
    b = [int(x) for x in bind.groups()]
    p = [int(x) for x in posed.groups()[:6]]
    return {"bind": [(b[0], b[1]), (b[2], b[3]), (b[4], b[5])],
            "posed": [(p[0], p[1]), (p[2], p[3]), (p[4], p[5])]}


def render_posed(o, model_paths, packed, cam, out_bmp):
    """One frame under a sequence's SHARED camera (comparable across frames
    and candidates — a per-render auto-frame would hide scale damage)."""
    extra = ["--cache", o.cache, "--frames", str(packed),
             "--focus", "%d,%d,%d" % tuple(cam["focus"]),
             "--radius", str(cam["radius"]),
             "--zoom", str(cam["zoom"]), "--near", "49"]
    return render_sheet(o, model_paths, out_bmp, o.pose_angles, extra)


# ----------------------------------------------------------------- baseline

def probe_label_geometry(o):
    """Merged rig-label geometry across all parts, via a no-op decimate whose
    mapping JSON carries per-label counts, centroids, and bounding boxes (all
    in the viewer's analysis units)."""
    merged = {}
    for src in o.base_models:
        base = os.path.splitext(os.path.basename(src))[0]
        out_ob3 = os.path.join(o.out_dir, "work", "probe_%s.ob3" % base)
        out_map = os.path.join(o.out_dir, "work", "probe_%s.json" % base)
        out, rc = run([o.decimator, "--in", src, "--out", out_ob3, "--map",
                       out_map, "--target-frac", "1.0", "--quiet"],
                      timeout=o.decimate_timeout)
        if rc != 0:
            raise SystemExit("osrsify: label probe failed on %s" % src)
        with open(out_map, "r", encoding="utf-8") as f:
            for lab, e in json.load(f).get("labels", {}).items():
                lab = int(lab)
                n, c, b = e["before"], e["centroid"], e["bbox"]
                if lab not in merged:
                    merged[lab] = {"count": n, "centroid": list(c),
                                   "bbox": [list(ax) for ax in b]}
                else:
                    m = merged[lab]
                    tot = m["count"] + n
                    m["centroid"] = [(m["centroid"][a] * m["count"] +
                                      c[a] * n) // tot for a in range(3)]
                    for a in range(3):
                        m["bbox"][a][0] = min(m["bbox"][a][0], b[a][0])
                        m["bbox"][a][1] = max(m["bbox"][a][1], b[a][1])
                    m["count"] = tot
    return merged


def build_regions(o, ctx):
    """Pick the small, detail-carrying rig regions and bank their baseline
    close-ups. Small labels are where faces, horns, and hands live; labels
    whose extent approaches the whole model add nothing over the global
    render and are skipped."""
    labels = probe_label_geometry(o)
    ctx["labels"] = labels
    if not labels:
        log("no rig labels — region judging disabled")
        ctx["regions"] = []
        return
    model_box = [(min(e["bbox"][a][0] for e in labels.values()),
                  max(e["bbox"][a][1] for e in labels.values()))
                 for a in range(3)]
    model_radius = diag(model_box) / 2.0
    cands = []
    for lab, e in labels.items():
        if e["count"] < o.region_min_verts:
            continue
        radius = max(32, int(math.ceil(diag(e["bbox"]) / 2.0 * o.region_pad)))
        if radius > model_radius * 0.45:
            continue
        cands.append({"label": lab, "focus": e["centroid"], "radius": radius,
                      "verts": e["count"]})
    cands.sort(key=lambda r: r["radius"])
    regions = []
    for r in cands[:o.regions]:
        bmp = os.path.join(ctx["work"], "base_reg%d.bmp" % r["label"])
        sheet, _ = render_region(o, o.base_models, r, bmp)
        if sheet is None:
            continue
        r["tiles"] = split_tiles(o, sheet, bmp[:-4])
        if r["tiles"]:
            regions.append(r)
    ctx["regions"] = regions
    log("region judge: %d close-up regions: %s"
        % (len(regions), ", ".join("label %d (r=%d, %dv)"
                                   % (r["label"], r["radius"], r["verts"])
                                   for r in regions)))


def plan_sequences(o, ctx):
    """Prescan every sampled baseline frame, fit one camera per sequence, and
    bank the baseline's posed renders as the anchors every candidate is
    compared against."""
    seq_frames = parse_seq_config(o.seqcfg, o.seqs)
    plans = {}
    for name in o.seqs:
        frames = seq_frames[name]
        picked = [frames[i] for i in sample_frames(len(frames), o.frames_per_seq)]
        recs, boxes = [], []
        for packed in picked:
            ps = prescan_frame(o, o.base_models, packed)
            if ps is None:
                log("  %s frame %d: baseline prescan failed (skipped)" % (name, packed))
                continue
            recs.append({"packed": packed, "prescan": ps})
            boxes.append(ps["posed"])
        if not boxes:
            log("  %s: no usable frames, sequence skipped" % name)
            continue
        union = [(min(b[a][0] for b in boxes), max(b[a][1] for b in boxes))
                 for a in range(3)]
        focus = [(lo + hi) // 2 for lo, hi in union]
        radius = int(math.ceil(math.sqrt(sum(((hi - lo) / 2.0) ** 2
                                             for lo, hi in union))))
        zoom, _dist = solve_framing(radius, o.tile)
        if zoom is None:
            log("  %s: pose reach %d exceeds the depth budget, skipped" % (name, radius))
            continue
        cam = {"focus": focus, "radius": radius, "zoom": zoom}
        for rec in recs:
            bmp = os.path.join(ctx["work"], "base_%s_f%d.bmp" % (name, rec["packed"]))
            sheet, _ = render_posed(o, o.base_models, rec["packed"], cam, bmp)
            if sheet is None:
                rec["tiles"] = {}
                continue
            rec["tiles"] = split_tiles(o, sheet, bmp[:-4])
        recs = [r for r in recs if r["tiles"]]
        if recs:
            plans[name] = {"camera": cam, "frames": recs}
            log("  %s: %d frames, focus %s radius %d zoom %.3f"
                % (name, len(recs), focus, radius, zoom))
    return plans


# --------------------------------------------------------------- evaluation

def eval_bind(o, ctx, tag, cand_models):
    """Stage 1: bind-pose style + identity. Cheap; runs for every candidate."""
    bmp = os.path.join(ctx["work"], tag + "_bind.bmp")
    sheet, out = render_sheet(o, cand_models, bmp, o.angles)
    if sheet is None:
        return None, "render-failed", None
    tiles = split_tiles(o, sheet, bmp[:-4])
    if not tiles:
        return None, "no-covered-views", None
    margins = [ctx["judge"].margin(p) for p, _cov in tiles.values()]
    # Identity is paired per yaw; a yaw the baseline covers but the candidate
    # does not IS the signal of collapse, and scores 0 for that view.
    ids = []
    for yaw, (bpng, _bcov) in ctx["base_tiles"].items():
        if yaw in tiles:
            ids.append(get_identity_score(bpng, tiles[yaw][0]))
        else:
            ids.append(0.0)
    return {"margin": sum(margins) / len(margins),
            "identity": sum(ids) / len(ids) if ids else 0.0,
            "views": len(tiles)}, None, tiles


def eval_regions(o, ctx, tag, cand_models):
    """Stage 1.5: close-up identity per small rig region. The whole-body
    camera leaves detail regions (a face on a boss-sized model) too few
    pixels to move any global score, so deletion there goes unpunished.
    Each region is re-rendered at close range under the baseline's fixed
    camera and judged by the preserver alone — close-ups are far outside
    the style judge's training distribution, so no style term here."""
    if not ctx.get("regions"):
        return {"region_min": None, "region_mean": None, "per": {}}, None
    per = {}
    for reg in ctx["regions"]:
        bmp = os.path.join(ctx["work"], "%s_reg%d.bmp" % (tag, reg["label"]))
        sheet, _ = render_region(o, cand_models, reg, bmp)
        if sheet is None:
            return None, "region-render:%d" % reg["label"]
        tiles = split_tiles(o, sheet, bmp[:-4])
        scores = []
        for yaw, (bpng, _bcov) in reg["tiles"].items():
            if yaw in tiles:
                scores.append(get_identity_score(bpng, tiles[yaw][0]))
            else:
                scores.append(0.0)  # region gone from this view = the crime
        per[reg["label"]] = round(sum(scores) / len(scores), 2)
    vals = list(per.values())
    return {"region_min": min(vals), "region_mean": sum(vals) / len(vals),
            "per": per}, None


def eval_anim(o, ctx, tag, cand_models):
    """Stage 2: the animation sweep. Gates + a posed-identity average."""
    seq_out = {}
    pose_ids = []
    for name, plan in ctx["plans"].items():
        cam = plan["camera"]
        worst_cov = 1.0
        seq_pose_ids = []
        for rec in plan["frames"]:
            packed = rec["packed"]
            ps = prescan_frame(o, cand_models, packed)
            if ps is None:
                return None, "anim-decode:%s:%d" % (name, packed)
            # Explosion/collapse heuristics: the candidate's posed/bind size
            # ratio must stay near the BASELINE's ratio for the same frame
            # (decimation may shrink the bind box a little; what it must not
            # do is change how much the pose stretches the mesh).
            base_ps = rec["prescan"]
            base_ratio = diag(base_ps["posed"]) / max(1.0, diag(base_ps["bind"]))
            cand_ratio = diag(ps["posed"]) / max(1.0, diag(ps["bind"]))
            rel = cand_ratio / max(1e-6, base_ratio)
            if cand_ratio < 0.1 * base_ratio or rel < 0.5 or rel > 2.0:
                return None, "pose-blowup:%s:%d(rel=%.2f)" % (name, packed, rel)
            bmp = os.path.join(ctx["work"], "%s_%s_f%d.bmp" % (tag, name, packed))
            sheet, _ = render_posed(o, cand_models, packed, cam, bmp)
            if sheet is None:
                return None, "anim-render:%s:%d" % (name, packed)
            tiles = split_tiles(o, sheet, bmp[:-4])
            for yaw, (bpng, bcov) in rec["tiles"].items():
                if yaw not in tiles:
                    return None, "pose-vanished:%s:%d:y%d" % (name, packed, yaw)
                cpng, ccov = tiles[yaw]
                ratio = ccov / max(1e-6, bcov)
                worst_cov = min(worst_cov, min(ratio, 1.0 / max(1e-6, ratio)))
                if ratio < o.cov_band[0] or ratio > o.cov_band[1]:
                    return None, ("pose-coverage:%s:%d:y%d(%.2f)"
                                  % (name, packed, yaw, ratio))
                seq_pose_ids.append(get_identity_score(bpng, cpng))
        if seq_pose_ids:
            seq_out[name] = {
                "pose_id_mean": round(sum(seq_pose_ids) / len(seq_pose_ids), 2),
                "pose_id_min": round(min(seq_pose_ids), 2),
                "worst_cov_sym": round(worst_cov, 3),
            }
            pose_ids += seq_pose_ids
    if not pose_ids:
        return None, "anim-nothing-rendered"
    return {"pose_id": sum(pose_ids) / len(pose_ids),
            "pose_id_min": min(pose_ids),
            "seqs": seq_out}, None


def evaluate(o, ctx, tag, cand_models, params):
    """Full judge for one candidate. Returns a record; fitness is -inf when a
    gate fails, so rejected candidates are never adopted but stay recorded."""
    t0 = time.time()
    rec = {"id": tag, "params": params, "status": "ok", "ts": round(t0)}
    bind, err, _tiles = eval_bind(o, ctx, tag, cand_models)
    if err:
        rec.update(status="rejected:" + err, fitness=None)
        return rec
    rec["style_margin"] = round(bind["margin"], 3)
    rec["identity"] = round(bind["identity"], 2)
    rec["views"] = bind["views"]
    if bind["identity"] < o.id_gate:
        rec.update(status="rejected:identity<%g" % o.id_gate, fitness=None)
        return rec
    regs, err = eval_regions(o, ctx, tag, cand_models)
    if err:
        rec.update(status="rejected:" + err, fitness=None)
        return rec
    if regs["per"]:
        rec["regions"] = regs["per"]
        rec["region_min"] = round(regs["region_min"], 2)
        rec["region_mean"] = round(regs["region_mean"], 2)
        if regs["region_min"] < o.region_id_gate:
            worst = min(regs["per"], key=regs["per"].get)
            rec.update(status="rejected:region%s<%g" % (worst, o.region_id_gate),
                       fitness=None)
            return rec
    anim, err = eval_anim(o, ctx, tag, cand_models)
    if err:
        rec.update(status="rejected:" + err, fitness=None)
        return rec
    rec["pose_id"] = round(anim["pose_id"], 2)
    rec["pose_id_min"] = round(anim["pose_id_min"], 2)
    rec["anim"] = anim["seqs"]
    if anim["pose_id"] < o.pose_id_gate:
        rec.update(status="rejected:pose_id<%g" % o.pose_id_gate, fitness=None)
        return rec
    # Priority mode: the carried per-face priorities must not paint occluded
    # faces over nearer geometry any worse than the baseline models do. Hard
    # caps on the violation fraction and on ghost faces (faces the z-buffer
    # shows nowhere in a view but the painter draws anyway), then a soft
    # fitness penalty for anything between baseline and the cap.
    zpenalty = 0.0
    base_zv = ctx.get("base_zviol")
    if base_zv is not None:
        zv, zerr = measure_zviol(o, cand_models)
        if zerr:
            rec.update(status="rejected:zviol-measure:" + zerr, fitness=None)
            return rec
        frac = zv["internal.frac_pct"]
        ghosts = int(zv.get("ghost.faces", 0))
        rec["zviol"] = {
            "frac_pct": round(frac, 3),
            "depth_only_pct": round(zv.get("internal_depth_only.frac_pct", 0.0), 3),
            "ghost_faces": ghosts,
            "ghost_px": int(zv.get("ghost.px", 0)),
        }
        frac_cap = base_zv["frac_pct"] * o.prio_zviol_tol + 0.25
        ghost_cap = int(base_zv["ghost_faces"] * o.prio_ghost_tol) + 16
        if frac > frac_cap:
            rec.update(status="rejected:zviol:%.2f%%>cap%.2f%%" % (frac, frac_cap),
                       fitness=None)
            return rec
        if ghosts > ghost_cap:
            rec.update(status="rejected:zviol-ghosts:%d>cap%d" % (ghosts, ghost_cap),
                       fitness=None)
            return rec
        zpenalty = o.prio_zviol_weight * max(0.0, frac - base_zv["frac_pct"])
        rec["zviol"]["penalty"] = round(zpenalty, 4)
    # Fitness: primarily the style-margin gain over baseline, with soft
    # pressure to keep both identity scores near 100 rather than hovering at
    # the gates. The gates are the safety; the soft terms are the taste.
    gain = bind["margin"] - ctx["base_margin"]
    fit = gain + 0.03 * (bind["identity"] - 100.0) + 0.03 * (anim["pose_id"] - 100.0)
    if regs["region_mean"] is not None:
        fit += 0.02 * (regs["region_mean"] - 100.0)
    fit -= zpenalty
    rec["fitness"] = round(fit, 4)
    rec["wall_s"] = round(time.time() - t0, 1)
    return rec


def checkpoint(ctx):
    tmp = ctx["results_path"] + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(ctx["results"], f, indent=1)
    # The live dashboard polls results.json; on Windows a read that is in
    # flight makes os.replace fail with EACCES (this killed a 4h run once).
    # The reader is done in milliseconds, so a short retry loop is enough.
    last = None
    for _ in range(50):
        try:
            os.replace(tmp, ctx["results_path"])
            return
        except PermissionError as e:
            last = e
            time.sleep(0.1)
    raise last


def record(o, ctx, rec, cand_models, keep_paths):
    ctx["results"]["candidates"].append(rec)
    fit = rec.get("fitness")
    if fit is not None:
        tagline = ("fitness %+.3f margin %+.2f id %.1f pose_id %.1f"
                   % (fit, rec["style_margin"], rec["identity"], rec["pose_id"]))
        if rec.get("region_min") is not None:
            tagline += " reg_min %.1f" % rec["region_min"]
        if rec.get("params", {}).get("verts") is not None:
            tagline += " [%dv %df]" % (rec["params"]["verts"],
                                       rec["params"]["faces"])
    else:
        tagline = rec["status"]
    log("[%s] %s" % (rec["id"], tagline))
    best = ctx["results"].get("best")
    if fit is not None and (best is None or fit > best["fitness"]):
        ctx["results"]["best"] = {"id": rec["id"], "fitness": fit}
        best_dir = os.path.join(o.out_dir, "best")
        shutil.rmtree(best_dir, ignore_errors=True)
        os.makedirs(best_dir)
        for p in keep_paths:
            if os.path.isfile(p):
                shutil.copy2(p, best_dir)
        with open(os.path.join(best_dir, "candidate.json"), "w",
                  encoding="utf-8") as f:
            json.dump(rec, f, indent=1)
        log("  ^ new best -> %s" % best_dir)
    checkpoint(ctx)
    return fit


# ------------------------------------------------------------ regime REDUCE

def run_decimate(o, tag, frac, seed, jitter):
    """Decimate every part with the same parameters; returns (paths, stats)
    or (None, reason). Parts share the merged rig, so reducing them under the
    same settings keeps the merged look coherent."""
    cand_dir = os.path.join(o.out_dir, "cand", tag)
    os.makedirs(cand_dir, exist_ok=True)
    outs, stats = [], []
    for src in o.base_models:
        base = os.path.splitext(os.path.basename(src))[0]
        out_ob3 = os.path.join(cand_dir, base + ".ob3")
        out_map = os.path.join(cand_dir, base + ".map.json")
        cmd = [o.decimator, "--in", src, "--out", out_ob3, "--map", out_map,
               "--target-frac", "%g" % frac, "--seed", str(seed),
               "--jitter", str(jitter), "--quiet"]
        if o.max_cost > 0:
            cmd += ["--max-cost", "%g" % o.max_cost]
        out, rc = run(cmd, timeout=o.decimate_timeout)
        if rc != 0:
            return None, "decimate-failed:%s" % base, None
        with open(out_map, "r", encoding="utf-8") as f:
            m = json.load(f)
        outs.append(out_ob3)
        stats.append({"part": base,
                      "vertices": m["reduced"]["vertices"],
                      "faces": m["reduced"]["faces"],
                      "orig_vertices": m["original"]["vertices"],
                      "orig_faces": m["original"]["faces"],
                      "stop": m["search"]["stop"]})
    # Poly budget is on the MERGED totals, because that is what the engine
    # allocates: an NPC's parts are merged into one model before they reach
    # the scene's scratch tier, so a per-part budget would not bound anything
    # the renderer actually cares about.
    tot_v = sum(s["vertices"] for s in stats)
    tot_f = sum(s["faces"] for s in stats)
    if (o.max_verts and tot_v > o.max_verts) or (o.max_faces and tot_f > o.max_faces):
        return None, "over-budget:%dv/%df" % (tot_v, tot_f), None
    return outs, None, stats


def regime_reduce(o, ctx):
    """Ladder of fractions x seeds, then refinement around the best frac for
    as long as the time budget allows."""
    log("== regime REDUCE ==")
    tried = set()

    def attempt(frac, seed, jitter):
        pause_gate(o)
        tag = "r%.2f_s%d" % (frac, seed)
        if tag in tried or time.time() > o.deadline or frac > o.frac_ceiling:
            return
        tried.add(tag)
        t0 = time.time()
        outs, err, stats = run_decimate(o, tag, frac, seed, jitter)
        params = {"regime": "reduce", "frac": frac, "seed": seed,
                  "jitter": jitter}
        if err:
            # One over-budget fraction condemns every coarser one: the search
            # is monotone in frac, so lower the ceiling instead of paying for
            # the same rejection again on the next refinement draw.
            if err.startswith("over-budget") and frac <= o.frac_ceiling:
                o.frac_ceiling = round(frac - 0.01, 2)
                log("  poly budget: fraction ceiling now %.2f" % o.frac_ceiling)
            rec = {"id": tag, "params": params, "status": "rejected:" + err,
                   "fitness": None, "ts": round(time.time()),
                   "wall_s": round(time.time() - t0, 1)}
            ctx["results"]["candidates"].append(rec)
            log("[%s] %s" % (tag, rec["status"]))
            checkpoint(ctx)
            return
        params["parts"] = stats
        params["verts"] = sum(s["vertices"] for s in stats)
        params["faces"] = sum(s["faces"] for s in stats)
        rec = evaluate(o, ctx, tag, outs, params)
        # The judge stamps its own span; the attempt's is decimate + judge.
        rec["wall_s"] = round(time.time() - t0, 1)
        maps = [p[:-4] + ".map.json" for p in outs]
        record(o, ctx, rec, outs, outs + maps)

    for frac in o.fracs:
        for seed in range(1, o.seeds + 1):
            attempt(frac, seed, o.jitter)
            if time.time() > o.deadline:
                log("time budget reached during ladder")
                return

    # Refinement: densify seeds and nearby fractions around the current best.
    rng = random.Random(0xC0FFEE)
    while time.time() < o.deadline:
        best = ctx["results"].get("best")
        passing = [c for c in ctx["results"]["candidates"]
                   if c.get("fitness") is not None and
                   c["params"].get("regime") == "reduce"]
        if not passing:
            log("refinement: nothing passes the gates yet, widening seeds on "
                "the gentlest fraction")
            frac = max(o.fracs)
        else:
            best_rec = max(passing, key=lambda c: c["fitness"])
            frac = best_rec["params"]["frac"]
            frac = min(0.95, max(0.15, frac + rng.choice([-0.05, 0.0, 0.05])))
        frac = min(frac, o.frac_ceiling)
        attempt(round(frac, 2), rng.randrange(1, 10000), o.jitter)
        _ = best


# ------------------------------------------------------------ regime SCULPT

def regime_sculpt(o, ctx):
    """Simulated annealing over nudge move lists: per-group inflate amounts
    plus a global coarse-grid quantize step."""
    log("== regime SCULPT ==")
    labels = ctx["labels"]  # banked by build_regions at startup
    groups = sorted(g for g, e in labels.items() if e["count"] >= 8)
    log("sculptable groups (>=8 verts): %s" % groups)
    if not groups:
        log("no rig groups to sculpt; nothing to do")
        return

    rng = random.Random(o.sa_seed)
    state = {"quant": 0, "inflate": {}}
    cur_fit = 0.0            # baseline fitness is 0 by construction
    it = 0
    t0 = time.time()
    span = max(1.0, o.deadline - t0)

    def mutate(s):
        n = {"quant": s["quant"], "inflate": dict(s["inflate"])}
        if rng.random() < 0.35:
            n["quant"] = rng.choice([q for q in (0, 4, 6, 8, 12, 16)
                                     if q != s["quant"]])
        else:
            g = rng.choice(groups)
            amt = n["inflate"].get(g, 0) + rng.choice([-4, -3, -2, -1, 1, 2, 3, 4])
            amt = max(-12, min(12, amt))
            if amt == 0:
                n["inflate"].pop(g, None)
            else:
                n["inflate"][g] = amt
        return n

    def moves_text(s):
        lines = ["inflate %d %d" % (g, a) for g, a in sorted(s["inflate"].items())]
        if s["quant"]:
            lines.append("quantize -1 %d" % s["quant"])
        return "\n".join(lines) + "\n"

    while it < o.sa_iters and time.time() < o.deadline:
        # a pause shifts the anneal clock too, so the temperature schedule
        # resumes where it left off instead of jumping ahead
        t0 += pause_gate(o)
        it += 1
        cand = mutate(state)
        if not cand["inflate"] and not cand["quant"]:
            continue
        tag = "a%04d" % it
        a0 = time.time()
        cand_dir = os.path.join(o.out_dir, "cand", tag)
        os.makedirs(cand_dir, exist_ok=True)
        moves_path = os.path.join(cand_dir, "moves.txt")
        with open(moves_path, "w", encoding="utf-8") as f:
            f.write(moves_text(cand))
        outs = [os.path.join(cand_dir, os.path.basename(m))
                for m in o.base_models]
        cmd = [o.nudge]
        for m in o.base_models:
            cmd += ["--in", m]
        for out_path in outs:
            cmd += ["--out", out_path]
        cmd += ["--moves", moves_path]
        out, rc = run(cmd, timeout=o.decimate_timeout)
        params = {"regime": "sculpt", "moves": moves_text(cand).strip()
                  .split("\n")}
        if rc != 0:
            rec = {"id": tag, "params": params, "status": "rejected:nudge-failed",
                   "fitness": None, "ts": round(time.time()),
                   "wall_s": round(time.time() - a0, 1)}
            ctx["results"]["candidates"].append(rec)
            log("[%s] %s" % (tag, rec["status"]))
            checkpoint(ctx)
            continue
        rec = evaluate(o, ctx, tag, outs, params)
        rec["wall_s"] = round(time.time() - a0, 1)
        fit = record(o, ctx, rec, outs, outs + [moves_path])
        # Annealing acceptance on the SEARCH state (the global best is kept
        # separately by record()): temperature decays over the time budget.
        temp = max(0.02, o.sa_temp * (1.0 - (time.time() - t0) / span))
        if fit is not None and (fit > cur_fit or
                                rng.random() < math.exp((fit - cur_fit) / temp)):
            state, cur_fit = cand, fit
            log("  accepted (T=%.3f) state=%s" % (temp, moves_text(cand).strip()
                                                  .replace("\n", "; ")))


# --------------------------------------------------------------------- main

def run_backport(o):
    """Re-run the material backport on the lane before anything else touches
    the parts (rs2012_material_bake). The imported models carry OSRS material
    ids the 2012 engine has no textures for; the bake composites each erased
    material's baked frame into the face colours it falls back to, turns the
    frame's alpha coverage into face translucency, and --matte pulls the HD
    frames' specular gradients toward the flat OSRS look. It changes face
    colours AND face counts (fully transparent faces are dropped), so it has
    to run before the baseline, the poly budget and every candidate.

    Output goes to <run>/backport via --models-out; --apply is never passed,
    because a search must not rewrite the content lane it is searching.
    Failure is fatal, unlike defight: a run that was asked to backport must
    not quietly go on to search the un-baked models."""
    if not os.path.exists(o.bake_tool):
        raise SystemExit("osrsify: --backport needs %s — build with "
                         "'mingw32-make -C src rs2012-material-bake'"
                         % o.bake_tool)
    bdir = os.path.join(o.out_dir, "backport")
    os.makedirs(bdir, exist_ok=True)
    cmd = [o.bake_tool, "--cache", absify(o.backport_cache),
           "--tree", absify(o.backport_tree), "--models-out", bdir]
    if o.matte is not None:
        cmd += ["--matte", str(o.matte)]
    if o.face_color_bake:
        cmd += ["--face-color-bake", o.face_color_bake]
    if o.face_color_strength is not None:
        cmd += ["--face-color-strength", str(o.face_color_strength)]
    if o.no_face_alpha_bake:
        cmd.append("--no-face-alpha-bake")
    if o.wisp_alpha:
        cmd += ["--wisp-alpha", o.wisp_alpha]
    try:
        out, rc = run(cmd, timeout=o.bake_timeout)
    except subprocess.TimeoutExpired:
        raise SystemExit("osrsify: material bake exceeded --bake-timeout %gs"
                         % o.bake_timeout)
    with open(os.path.join(bdir, "report.txt"), "w", encoding="utf-8") as f:
        f.write(subprocess.list2cmdline(cmd) + "\n\n" + out)
    if rc != 0:
        raise SystemExit("osrsify: material bake failed (rc=%d):\n%s"
                         % (rc, out.strip()))
    baked = [os.path.join(bdir, os.path.basename(m)) for m in o.base_models]
    missing = [os.path.basename(b) for b in baked if not os.path.exists(b)]
    if missing:
        # The bake skips lane models with no source material row — authored
        # parts, for one. Falling back to the un-baked original for those
        # would silently mix baked and un-baked parts inside one merged NPC,
        # which is exactly the kind of split the judges cannot see.
        raise SystemExit(
            "osrsify: the bake produced no output for %s — those parts are "
            "not in the baked lane of %s (authored models have no source "
            "materials). Drop them from --model, or run without --backport."
            % (", ".join(missing), o.backport_tree))
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("rs2012_material_bake") or line.startswith("matte=") \
                or line.startswith("face_color_bake="):
            log("backport: " + line)
    log("backport: %d part(s) re-baked -> %s" % (len(baked), bdir))
    o.base_models = baked


def run_defight(o):
    """Repair z-fighting on the base models before anything renders or
    decimates them. The z-buffer path drops the priority layering that keeps
    coplanar decals apart in the painter's renderer, so authored layers tie
    in depth; rs2012_model_defight sweeps a yaw x pitch view grid, finds the
    tie pairs, and pushes the authored-on-top layer outward a few units
    (duplicating shared vertices so the rig and animations are untouched).
    Failure is non-fatal: the run continues on the original inputs."""
    if not os.path.exists(o.defight_tool):
        log("defight: tool missing (%s) — build with 'mingw32-make -C src "
            "rs2012-model-defight'; continuing on original models"
            % o.defight_tool)
        return
    ddir = os.path.join(o.out_dir, "defight")
    os.makedirs(ddir, exist_ok=True)
    fixed = [os.path.join(ddir, os.path.basename(m)) for m in o.base_models]
    report = os.path.join(ddir, "report.json")
    cmd = [o.defight_tool]
    for m in o.base_models:
        cmd += ["--in", m]
    for m in fixed:
        cmd += ["--out", m]
    cmd += ["--report", report]
    if o.defight_min_pixels is not None:
        cmd += ["--min-pixels", str(o.defight_min_pixels)]
    if o.defight_yaws is not None:
        cmd += ["--yaws", str(o.defight_yaws)]
    if o.defight_fight_eps is not None:
        cmd += ["--fight-eps", "%g" % o.defight_fight_eps]
    if o.defight_deltas:
        cmd += ["--deltas", o.defight_deltas]
    try:
        out, rc = run(cmd, timeout=o.defight_timeout)
    except Exception as exc:
        log("defight: failed to run (%s) — continuing on original models"
            % exc)
        return
    if rc != 0 or not all(os.path.exists(f) for f in fixed):
        log("defight: tool rc=%d — continuing on original models\n%s"
            % (rc, out.strip()))
        return
    try:
        with open(report, "r", encoding="utf-8") as f:
            rep = json.load(f)
        log("defight: %d/%d fighting pairs treated at delta %g, fight pixels"
            " %d -> %d (treated pairs %d -> %d), +%d verts"
            % (rep["pairs_qualified"], rep["pairs_fighting"],
               rep["delta_used"], rep["fight_pixels_before"],
               rep["fight_pixels_after"], rep["qualified_pixels_before"],
               rep["treated_pixels_after"],
               sum(p["vertices_added"] for p in rep["parts"])))
    except Exception:
        log("defight: done (report unreadable)")
    o.base_models = fixed


def run_priorities(o):
    """Solve per-face render priorities (bands 0-10) on the base models with
    rs2012_face_priorities, so every reduce/sculpt candidate inherits an
    authored-style priority layering and renders through the painter's
    priority buckets instead of the z-buffer. Decimate carries the per-face
    bytes through its face compaction and nudge passes them through
    untouched, so a single up-front solve covers the whole search."""
    if not os.path.exists(o.prio_tool):
        raise SystemExit("osrsify: --force-priorities needs %s — build with "
                         "'mingw32-make -C src rs2012-face-priorities'"
                         % o.prio_tool)
    pdir = os.path.join(o.out_dir, "priorities")
    os.makedirs(pdir, exist_ok=True)
    solved = [os.path.join(pdir, os.path.basename(m)) for m in o.base_models]
    cmd = [o.prio_tool]
    for m in o.base_models:
        cmd += ["--in", m]
    for m in solved:
        cmd += ["--out", m]
    # Hardened search space: a denser view/elevation sweep than the tool's
    # defaults, plus the slow anneal (bands AND sub-visual-threshold vertex
    # offsets against the pristine z-buffer) so orderings the fast rank
    # cannot reach are still explored.
    cmd += ["--report", "--views", str(o.prio_views),
            "--pitches", str(o.prio_pitches),
            "--repair", str(o.prio_repair)]
    if o.prio_slow > 0:
        cmd += ["--slow", str(o.prio_slow),
                "--slow-views", str(o.prio_slow_views),
                "--slow-pitches", str(o.prio_slow_pitches)]
    out, rc = run(cmd, timeout=o.prio_timeout)
    with open(os.path.join(pdir, "report.txt"), "w", encoding="utf-8") as f:
        f.write(out)
    if rc != 0 or not all(os.path.exists(f) for f in solved):
        raise SystemExit("osrsify: priority solve failed (rc=%d):\n%s"
                         % (rc, out.strip()))
    log("priorities: solved bands for %d parts -> %s" % (len(solved), pdir))
    o.base_models = solved


def run_strip_priorities(o):
    """Strip the per-face render priorities off the base models entirely
    (rs2012_face_priorities --strip): every face lands in the header's one
    flat bucket, so the painter depth-sorts the whole model instead of
    layering it. Decimate and nudge then have no per-face bytes to carry,
    and the z-violation audit measures pure depth-sort error."""
    if not os.path.exists(o.prio_tool):
        raise SystemExit("osrsify: --force-priorities needs %s — build with "
                         "'mingw32-make -C src rs2012-face-priorities'"
                         % o.prio_tool)
    pdir = os.path.join(o.out_dir, "priorities")
    os.makedirs(pdir, exist_ok=True)
    stripped = [os.path.join(pdir, os.path.basename(m)) for m in o.base_models]
    cmd = [o.prio_tool, "--strip"]
    for m in o.base_models:
        cmd += ["--in", m]
    for m in stripped:
        cmd += ["--out", m]
    out, rc = run(cmd, timeout=o.prio_timeout)
    with open(os.path.join(pdir, "report.txt"), "w", encoding="utf-8") as f:
        f.write(out)
    if rc != 0 or not all(os.path.exists(f) for f in stripped):
        raise SystemExit("osrsify: priority strip failed (rc=%d):\n%s"
                         % (rc, out.strip()))
    log("priorities: stripped %d parts to one flat bucket -> %s"
        % (len(stripped), pdir))
    o.base_models = stripped


def measure_zviol(o, models):
    """Audit how much the per-face priorities the models carry violate the
    z-buffer, via rs2012_face_priorities --measure: pixels where the painter
    shows a face that is behind the true surface (occlusion violations), and
    'ghost' faces the z-buffer shows nowhere in a view but the prioritized
    painter draws anyway — the artefact prio mode is known to produce.
    Returns ({metric: value}, None) or (None, reason)."""
    cmd = [o.prio_tool]
    for m in models:
        cmd += ["--in", m]
    cmd += ["--measure", "--views", str(o.prio_measure_views),
            "--pitches", str(o.prio_measure_pitches),
            "--res", str(o.prio_measure_res)]
    try:
        out, rc = run(cmd, timeout=min(o.prio_timeout, 600))
    except Exception as exc:
        return None, str(exc)
    if rc != 0:
        return None, "rc=%d" % rc
    zv = {}
    for line in out.splitlines():
        if not line.startswith("ZVIOL "):
            continue
        key, _, rest = line[6:].partition(":")
        for kv in rest.split():
            k, _, v = kv.partition("=")
            try:
                zv["%s.%s" % (key.strip(), k)] = float(v)
            except ValueError:
                pass
    if "internal.frac_pct" not in zv:
        return None, "unparseable"
    return zv, None


# ------------------------------------------------------------- mode: author
#
# The search half of this tool only ever writes into its own run directory.
# Authoring is the deliberate second step: take one chosen candidate and put
# it into the content tree as a real asset. Three things have to agree or the
# model is half-present in a way nothing reports —
#
#   models/<...>/<name>.ob3           the geometry itself
#   ported/<lane>/pack/7_models.pack  id -> path, how the packer finds it
#   configs/rs2012.npc                model<N>=<id>, already authored by hand
#
# — so this writes the first two and leaves the npc record alone: repointing a
# form at a new model id is an editorial decision, not a mechanical one. The
# cache is what the authored model is verified against (its sequences are
# decoded and posed straight out of it) and what has to be re-packed after.

def model_counts(o, path):
    """(vertices, faces) via a no-op decimate — the same probe the region
    judge uses, so the numbers match what the search reported."""
    tmp = tempfile.mkdtemp(prefix="osrsify_probe_")
    try:
        out_map = os.path.join(tmp, "probe.json")
        _out, rc = run([o.decimator, "--in", path,
                        "--out", os.path.join(tmp, "probe.ob3"),
                        "--map", out_map, "--target-frac", "1.0", "--quiet"],
                       timeout=o.decimate_timeout)
        if rc != 0 or not os.path.isfile(out_map):
            return None, None
        with open(out_map, "r", encoding="utf-8") as f:
            m = json.load(f)["original"]
        return m["vertices"], m["faces"]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def pack_entry_for(dest_path):
    """A pack file names a model by its path under the content tree's models/
    directory, without the extension: models/ported/x/y.ob3 -> ported/x/y."""
    parts = os.path.abspath(dest_path).replace("\\", "/").split("/")
    if "models" not in parts:
        raise SystemExit(
            "osrsify: --author-out must land under a content tree's models/ "
            "directory (a pack entry is the path relative to it); got %s"
            % dest_path)
    cut = len(parts) - 1 - parts[::-1].index("models")
    rel = "/".join(parts[cut + 1:])
    return rel[:-4] if rel.lower().endswith(".ob3") else rel


def update_pack(pack_path, entries, dry_run):
    """Register (pack_id, value) pairs. Idempotent: an id already in the file
    is rewritten where it stands, a new one is appended, and a path already
    registered under a DIFFERENT id is reported — that is the failure mode
    that silently leaves two ids pointing at one asset."""
    with open(pack_path, "rb") as f:
        raw = f.read()
    # These files are CRLF on disk. Rewriting them LF would bury two changed
    # lines under a whole-file diff, so keep whatever the file already uses.
    eol = "\r\n" if raw.count(b"\r\n") > raw.count(b"\n") - raw.count(b"\r\n") \
        else "\n"
    lines = raw.decode("utf-8").splitlines()
    notes = []
    for pid, value in entries:
        for other in lines:
            k, _, v = other.partition("=")
            if v.strip() == value and k.strip() != str(pid):
                notes.append("  ! %s is ALSO registered as id %s" % (value, k.strip()))
        line = "%d=%s" % (pid, value)
        prefix = "%d=" % pid
        for i, existing in enumerate(lines):
            if existing.startswith(prefix):
                notes.append("  %s (%s)" % (line, "unchanged" if existing == line
                                            else "replaced " + existing))
                lines[i] = line
                break
        else:
            notes.append("  %s (added)" % line)
            lines.append(line)
    for n in notes:
        log(n)
    if not dry_run:
        tmp = pack_path + ".tmp"
        with open(tmp, "wb") as f:
            f.write((eol.join(lines) + eol).encode("utf-8"))
        os.replace(tmp, pack_path)


def verify_authored(o, models):
    """Prove the authored files are loadable assets, not just bytes on disk:
    render them merged in bind pose, then decode and pose one frame of every
    sequence straight out of the cache the client will read."""
    tmp = tempfile.mkdtemp(prefix="osrsify_author_")
    try:
        sheet, out = render_sheet(o, models, os.path.join(tmp, "bind.bmp"), o.angles)
        if sheet is None:
            log("  VERIFY FAIL: authored models do not render:\n%s" % out.strip())
            return False
        tiles = split_tiles(o, sheet, os.path.join(tmp, "bind"))
        if not tiles:
            log("  VERIFY FAIL: authored models render empty in every view")
            return False
        log("  bind render OK (%d/%d covered views)" % (len(tiles), o.angles))
        if not o.seqs:
            log("  no sequences given — animation verify skipped")
            return True
        seq_frames = parse_seq_config(o.seqcfg, o.seqs)
        bad = []
        for name in o.seqs:
            frames = seq_frames[name]
            picked = [frames[i] for i in sample_frames(len(frames),
                                                       o.frames_per_seq)]
            failed = [p for p in picked if prescan_frame(o, models, p) is None]
            if failed:
                bad.append("%s (%d/%d frames)" % (name, len(failed), len(picked)))
        if bad:
            log("  VERIFY FAIL: frames did not decode: %s" % ", ".join(bad))
            return False
        log("  animation OK (%d sequences decode and pose from %s)"
            % (len(o.seqs), o.cache))
        return True
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def do_author(o):
    """--mode author: place chosen .ob3 files into the content tree and
    register them in a pack file."""
    if not o.pack:
        raise SystemExit("osrsify: --mode author needs --pack <N_models.pack>")
    if not o.author_out:
        raise SystemExit("osrsify: --mode author needs --author-out <dir|file>")
    o.pack = absify(o.pack)
    if not os.path.isfile(o.pack):
        raise SystemExit("osrsify: pack file missing: %s" % o.pack)

    # Sources, in the order the pack ids were given. --author-from resolves
    # each BASE part's filename inside a run's output directory, so the parts
    # keep the preset's order instead of a directory listing's.
    if o.author_model:
        srcs = [absify(m) for m in o.author_model]
    elif o.author_from:
        d = absify(o.author_from)
        if not o.model:
            raise SystemExit("osrsify: --author-from needs --model/--preset to "
                             "know which parts to pull (and in what order)")
        srcs = [os.path.join(d, os.path.basename(absify(m))) for m in o.model]
    else:
        raise SystemExit("osrsify: --mode author needs --author-model or "
                         "--author-from")
    for s in srcs:
        if not os.path.isfile(s):
            raise SystemExit("osrsify: missing source model %s" % s)
    if len(o.pack_id) != len(srcs):
        raise SystemExit("osrsify: %d model(s) but %d --pack-id (they pair up "
                         "in order)" % (len(srcs), len(o.pack_id)))

    # Destinations. A single model may name its file outright; several always
    # go into a directory, keeping their source names plus --author-suffix.
    out = absify(o.author_out)
    if len(srcs) == 1 and out.lower().endswith(".ob3"):
        dests = [out]
    else:
        dests = []
        for s in srcs:
            stem = os.path.splitext(os.path.basename(s))[0]
            dests.append(os.path.join(out, stem + o.author_suffix + ".ob3"))

    log("authoring %d model(s)%s" % (len(srcs), "  [DRY RUN]" if o.dry_run else ""))
    entries = []
    for src, dest, pid in zip(srcs, dests, o.pack_id):
        v, f = model_counts(o, src)
        counts = "%d verts / %d faces" % (v, f) if v is not None else "counts unavailable"
        over = []
        if o.max_verts and v is not None and v > o.max_verts:
            over.append("verts>%d" % o.max_verts)
        if o.max_faces and f is not None and f > o.max_faces:
            over.append("faces>%d" % o.max_faces)
        log("  %s\n    -> %s\n       id %d, %s%s"
            % (src, dest, pid, counts,
               ("  OVER BUDGET: " + ", ".join(over)) if over else ""))
        if over:
            raise SystemExit("osrsify: refusing to author a model over the "
                             "budget you asked for")
        if not o.dry_run:
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.copyfile(src, dest)
        entries.append((pid, pack_entry_for(dest)))

    log("pack %s:" % o.pack)
    update_pack(o.pack, entries, o.dry_run)

    if o.author_verify and not o.dry_run:
        log("verifying authored assets...")
        if not verify_authored(o, dests):
            raise SystemExit("osrsify: authored models failed verification "
                             "(they are on disk — fix or revert before packing)")

    total_v = total_f = 0
    for d in (srcs if o.dry_run else dests):
        v, f = model_counts(o, d)
        total_v += v or 0
        total_f += f or 0
    log("merged totals: %d vertices, %d faces" % (total_v, total_f))
    log("")
    log("next: repoint the npc record's model<N>= at %s if it does not already,"
        % ", ".join(str(p) for p in o.pack_id))
    log("      then re-pack the cache:  make -C src mock230-cache-rs2012")


def main():
    ap = argparse.ArgumentParser(
        description="Search for an OSRS-looking variant of an imported model, "
                    "judged by the trained style/identity models plus an "
                    "animation sweep.")
    ap.add_argument("--mode", choices=["search", "author"], default="search",
                    help="search for a variant (default), or author a chosen "
                         "one into the content tree and a pack file")
    ap.add_argument("--preset", help="name from rs2012_backport_audit/presets.json")
    ap.add_argument("--model", action="append", default=[],
                    help="part .ob3 (repeatable; overrides preset)")
    ap.add_argument("--seq", action="append", default=[],
                    help="sequence name or bare id (repeatable)")
    ap.add_argument("--seqcfg", help="rs2012.seq config path")
    ap.add_argument("--cache", help="packed dat2 cache dir")
    ap.add_argument("--regime", choices=["reduce", "sculpt", "both"],
                    default="reduce")
    ap.add_argument("--out-dir", help="run directory (default runs/osrsify_<tag>)")
    ap.add_argument("--time-budget", type=float, default=3600,
                    help="seconds of search after the baseline is banked")
    ap.add_argument("--frames-per-seq", type=int, default=4)
    ap.add_argument("--angles", type=int, default=4, help="bind render yaws")
    ap.add_argument("--pose-angles", type=int, default=2, help="posed render yaws")
    ap.add_argument("--zbuffer", action="store_true",
                    help="render depth-tested instead of painter's order "
                         "(content authored against the z-buffer, e.g. QBD)")
    ap.add_argument("--pitch", type=int, default=128)
    ap.add_argument("--tile", type=int, default=256)
    ap.add_argument("--bg", default="808080")
    ap.add_argument("--fracs", default="0.85,0.70,0.55,0.45,0.35,0.20",
                    help="reduce ladder of vertex fractions")
    ap.add_argument("--seeds", type=int, default=3, help="seeds per fraction")
    ap.add_argument("--jitter", type=int, default=8,
                    help="decimator candidate-pool jitter")
    ap.add_argument("--max-cost", type=float, default=0.0,
                    help="decimator collapse cost ceiling (0 = judges decide)")
    ap.add_argument("--max-verts", type=int, default=0,
                    help="reject reduce candidates whose MERGED vertex count "
                         "across all parts exceeds this (0 = no budget)")
    ap.add_argument("--max-faces", type=int, default=0,
                    help="reject reduce candidates whose MERGED face count "
                         "across all parts exceeds this (0 = no budget)")
    ap.add_argument("--id-gate", type=float, default=55.0)
    ap.add_argument("--pose-id-gate", type=float, default=50.0)
    ap.add_argument("--regions", type=int, default=8,
                    help="max close-up regions to judge (0 disables)")
    ap.add_argument("--region-min-verts", type=int, default=30,
                    help="ignore rig labels smaller than this")
    ap.add_argument("--region-pad", type=float, default=1.3,
                    help="close-up camera radius = region half-diagonal x pad")
    ap.add_argument("--region-id-gate", type=float, default=45.0,
                    help="minimum per-region close-up identity")
    ap.add_argument("--cov-band", default="0.5,2.0",
                    help="allowed candidate/baseline coverage ratio per view")
    ap.add_argument("--sa-iters", type=int, default=400)
    ap.add_argument("--sa-temp", type=float, default=0.6)
    ap.add_argument("--sa-seed", type=int, default=1)
    ap.add_argument("--style-ckpt", default=STYLE_CKPT)
    ap.add_argument("--viewer", default=DEFAULT_VIEWER)
    ap.add_argument("--decimator", default=DEFAULT_DECIMATOR)
    ap.add_argument("--nudge", default=DEFAULT_NUDGE)
    ap.add_argument("--backport", action="store_true",
                    help="re-run the material backport on the lane "
                         "(rs2012_material_bake) before the search sees the "
                         "parts; the baked OB3s land in <run>/backport and "
                         "the content tree is never touched")
    ap.add_argument("--backport-cache", default="cache.rs727_preeoc",
                    help="HD source cache the bake reads materials out of — "
                         "NOT --cache, which is the dat2 cache the sequences "
                         "decode from")
    ap.add_argument("--backport-tree", default="OSRS-Content/osrs239-content",
                    help="content tree whose ported lane gets baked")
    ap.add_argument("--matte", type=int, default=None,
                    help="compress each baked face's lightness toward its "
                         "material's mean by N%% and roll highlights off at "
                         "the gloss knee (0-100; tool default 0)")
    ap.add_argument("--face-color-bake", choices=("off", "tint", "modulate"),
                    default=None,
                    help="how an erased material's frame reaches the face "
                         "colours it falls back to (tool default modulate; "
                         "'off' is the bare erase-only fallback)")
    ap.add_argument("--face-color-strength", type=int, default=None,
                    help="face-colour bake strength 0-100 (tool default 100)")
    ap.add_argument("--no-face-alpha-bake", action="store_true",
                    help="keep erased faces opaque instead of turning the "
                         "frame's alpha coverage into face translucency")
    ap.add_argument("--wisp-alpha", choices=("screen", "capped", "off"),
                    default=None,
                    help="bound the wisp alpha inference that overrides a "
                         "material row's own alpha_mode 0 (tool default off)")
    ap.add_argument("--bake-tool", default=DEFAULT_BAKE)
    ap.add_argument("--bake-timeout", type=float, default=600)
    ap.add_argument("--defight", choices=("auto", "on", "off"), default="auto",
                    help="repair z-fighting on the base models before the "
                         "search touches them (auto = on when --zbuffer)")
    ap.add_argument("--defight-tool", default=DEFAULT_DEFIGHT)
    ap.add_argument("--defight-min-pixels", type=int, default=None,
                    help="defight pair qualification threshold (tool default "
                         "24; lower catches thin fight seams)")
    ap.add_argument("--defight-yaws", type=int, default=None,
                    help="defight view sweep yaw count (tool default 16)")
    ap.add_argument("--defight-fight-eps", type=float, default=None,
                    help="defight depth-tie epsilon (tool default 2.5)")
    ap.add_argument("--defight-deltas", default=None,
                    help="defight displacement ladder CSV (tool default 3,6,9,12)")
    ap.add_argument("--defight-timeout", type=float, default=600)
    ap.add_argument("--force-priorities",
                    choices=("off", "solve", "keep", "strip"),
                    default="off",
                    help="render every attempt through the painter's priority "
                         "buckets instead of the z-buffer: 'solve' re-derives "
                         "bands with rs2012_face_priorities first, 'keep' uses "
                         "the per-face bands the models already carry, 'strip' "
                         "removes the per-face bands so the painter depth-"
                         "sorts every model as one flat bucket (all three "
                         "disable --zbuffer)")
    ap.add_argument("--prio-tool", default=DEFAULT_PRIO)
    ap.add_argument("--prio-timeout", type=float, default=3600)
    ap.add_argument("--prio-views", type=int, default=16,
                    help="priority solve yaw sweep (tool default 12)")
    ap.add_argument("--prio-pitches", type=int, default=6,
                    help="priority solve elevation sweep (tool default 5)")
    ap.add_argument("--prio-slow", type=int, default=800,
                    help="priority solve slow-anneal iterations (0 disables); "
                         "widens the solve beyond the fast rank into joint "
                         "band+geometry space")
    ap.add_argument("--prio-slow-views", type=int, default=8)
    ap.add_argument("--prio-slow-pitches", type=int, default=4)
    ap.add_argument("--prio-repair", type=int, default=256,
                    help="engine-judged greedy re-banding trials on the worst "
                         "violating features, between ranking and the anneal "
                         "(0 disables)")
    ap.add_argument("--prio-measure-views", type=int, default=16,
                    help="z-violation audit yaw sweep (runs per candidate)")
    ap.add_argument("--prio-measure-pitches", type=int, default=5)
    ap.add_argument("--prio-measure-res", type=int, default=192)
    ap.add_argument("--prio-zviol-tol", type=float, default=1.15,
                    help="reject a candidate whose occlusion-violation pixel "
                         "fraction exceeds baseline*tol + 0.25pp")
    ap.add_argument("--prio-ghost-tol", type=float, default=1.25,
                    help="reject a candidate whose ghost-face count exceeds "
                         "baseline*tol + 16")
    ap.add_argument("--prio-zviol-weight", type=float, default=0.4,
                    help="fitness penalty per percentage point of occlusion "
                         "violation above the baseline")
    ap.add_argument("--viewer-timeout", type=float, default=300)
    ap.add_argument("--decimate-timeout", type=float, default=600)

    author = ap.add_argument_group(
        "author mode (--mode author)",
        "place a chosen candidate into the content tree and register it")
    author.add_argument("--author-model", action="append", default=[],
                        help=".ob3 to author (repeatable; pairs with "
                             "--pack-id in order)")
    author.add_argument("--author-from",
                        help="directory holding the chosen candidate (e.g. "
                             "runs/<study>/best or runs/<study>/cand/<tag>); "
                             "each --model/--preset part is taken from it by "
                             "filename, so the parts keep their order")
    author.add_argument("--author-out",
                        help="where to place the authored model: a directory "
                             "under the content tree's models/, or a single "
                             ".ob3 path when authoring one part")
    author.add_argument("--author-suffix", default="",
                        help="inserted before .ob3 in the authored filename "
                             "(e.g. _lowpoly) so the original stays in place")
    author.add_argument("--pack", help="pack file to register in, e.g. "
                                       "<lane>/pack/7_models.pack")
    author.add_argument("--pack-id", action="append", type=int, default=[],
                        help="pack id for each authored model (repeatable, "
                             "paired with the models in order)")
    author.add_argument("--author-verify", dest="author_verify",
                        action="store_true", default=True,
                        help="render the authored models and decode every "
                             "sequence out of --cache afterwards (default on)")
    author.add_argument("--no-author-verify", dest="author_verify",
                        action="store_false")
    author.add_argument("--dry-run", action="store_true",
                        help="print what authoring would write, touch nothing")
    o = ap.parse_args()

    if o.preset:
        with open(PRESETS, "r", encoding="utf-8") as f:
            p = json.load(f)[o.preset]
        o.model = o.model or p["models"]
        o.seq = o.seq or p["seqs"]
        o.seqcfg = o.seqcfg or p["seqcfg"]
        o.cache = o.cache or p["cache"]
    if o.mode == "author":
        # Authoring needs the cache (verification decodes sequences straight
        # out of it) but not the judges, the ladder, or a run directory.
        if not o.cache:
            ap.error("--mode author needs --cache (or --preset)")
        o.cache = absify(o.cache)
        if not os.path.isdir(o.cache):
            raise SystemExit("osrsify: cache dir missing: %s" % o.cache)
        o.seqcfg = absify(o.seqcfg) if o.seqcfg else None
        o.seqs = ([s if not s.isdigit() else "rs2012_seq_" + s for s in o.seq]
                  if (o.seq and o.seqcfg) else [])
        o.zbuffer = o.zbuffer and o.force_priorities == "off"
        do_author(o)
        return
    if not o.model or not o.seq or not o.seqcfg or not o.cache:
        ap.error("need --model/--seq/--seqcfg/--cache (or --preset)")
    o.base_models = [absify(m) for m in o.model]
    o.seqcfg = absify(o.seqcfg)
    o.cache = absify(o.cache)
    o.seqs = [s if not s.isdigit() else "rs2012_seq_" + s for s in o.seq]
    o.fracs = [float(x) for x in o.fracs.split(",") if x]
    o.cov_band = tuple(float(x) for x in o.cov_band.split(","))
    # Lowered in place whenever a fraction busts the poly budget, so neither
    # the ladder nor the refinement draw ever pays for that fraction twice.
    o.frac_ceiling = 1.0
    for path in o.base_models + [o.seqcfg, o.viewer, o.decimator, o.nudge,
                                 o.style_ckpt]:
        if not os.path.exists(path):
            raise SystemExit("osrsify: missing %s" % path)
    if not os.path.isdir(o.cache):
        raise SystemExit("osrsify: cache dir missing: %s" % o.cache)

    tag = "%s_%s" % (o.preset or "custom", o.regime)
    o.out_dir = os.path.abspath(o.out_dir or
                                os.path.join(HERE, "runs", "osrsify_" + tag))
    work = os.path.join(o.out_dir, "work")
    os.makedirs(work, exist_ok=True)

    log("osrsify: %d parts, %d seqs, regime=%s, budget=%.0fs"
        % (len(o.base_models), len(o.seqs), o.regime, o.time_budget))
    log("run dir: %s" % o.out_dir)
    start_heartbeat(o)

    # ---- material backport FIRST of all: it rewrites face colours and drops
    # invisible faces, so the baseline, the poly budget and every downstream
    # pre-pass have to see its output rather than the lane's current OB3s ----
    if o.backport:
        run_backport(o)

    # ---- z-fighting repair BEFORE anything else sees the models: the fixed
    # parts become the baseline, every reduce/sculpt candidate, and the
    # results.json config ----
    if o.defight == "on" or (o.defight == "auto" and o.zbuffer):
        run_defight(o)

    # ---- optional priority mode AFTER defight, BEFORE the baseline: the
    # bands (solved or authored) ride along through decimate/nudge into
    # every candidate, and rendering switches to the painter so the judges
    # score the priority look ----
    base_zviol = None
    if o.force_priorities != "off":
        if o.force_priorities == "solve":
            run_priorities(o)
        elif o.force_priorities == "strip":
            run_strip_priorities(o)
        if o.zbuffer:
            log("priorities: dropping --zbuffer so renders honour the "
                "per-face bands (the z-buffer path ignores them)")
            o.zbuffer = False
        # Bank the z-violation baseline of the models every candidate will be
        # judged against. Prio mode without this audit is exactly how the
        # ghost-face regressions slipped through, so a failed measurement is
        # fatal rather than a warning.
        zv, zerr = measure_zviol(o, o.base_models)
        if zerr:
            raise SystemExit("osrsify: z-violation baseline audit failed (%s); "
                             "refusing to run prio mode unmeasured" % zerr)
        base_zviol = {
            "frac_pct": zv["internal.frac_pct"],
            "depth_only_pct": zv.get("internal_depth_only.frac_pct", 0.0),
            "ghost_faces": int(zv.get("ghost.faces", 0)),
            "ghost_px": int(zv.get("ghost.px", 0)),
        }
        log("z-violation baseline: %.2f%% wrong px (depth-only %.2f%%), "
            "%d ghost faces / %d px; candidate caps: frac<=%.2f%% ghosts<=%d"
            % (base_zviol["frac_pct"], base_zviol["depth_only_pct"],
               base_zviol["ghost_faces"], base_zviol["ghost_px"],
               base_zviol["frac_pct"] * o.prio_zviol_tol + 0.25,
               int(base_zviol["ghost_faces"] * o.prio_ghost_tol) + 16))

    ctx = {"work": work, "judge": StyleJudge(o.style_ckpt),
           "results_path": os.path.join(o.out_dir, "results.json"),
           "base_zviol": base_zviol}

    # ---- baseline: bind style/identity anchors + per-seq shared cameras ----
    log("banking baseline...")
    bmp = os.path.join(work, "baseline_bind.bmp")
    sheet, out = render_sheet(o, o.base_models, bmp, o.angles)
    if sheet is None:
        raise SystemExit("osrsify: baseline render failed:\n" + out)
    ctx["base_tiles"] = split_tiles(o, sheet, bmp[:-4])
    if not ctx["base_tiles"]:
        raise SystemExit("osrsify: baseline has no covered views")
    base_margins = [ctx["judge"].margin(p) for p, _ in ctx["base_tiles"].values()]
    ctx["base_margin"] = sum(base_margins) / len(base_margins)
    log("baseline style margin %+.3f over %d views"
        % (ctx["base_margin"], len(ctx["base_tiles"])))
    if o.regions > 0:
        build_regions(o, ctx)
    else:
        ctx["labels"] = probe_label_geometry(o)
        ctx["regions"] = []
    ctx["plans"] = plan_sequences(o, ctx)
    if not ctx["plans"]:
        raise SystemExit("osrsify: no usable sequences — the animation sweep "
                         "is the point; refusing to run without it")

    ctx["results"] = {
        "config": {k: v for k, v in vars(o).items()
                   if isinstance(v, (str, int, float, list, tuple))},
        "baseline": {"style_margin": round(ctx["base_margin"], 3),
                     "zviol": base_zviol,
                     "views": len(ctx["base_tiles"]),
                     "seqs": {n: len(pl["frames"])
                              for n, pl in ctx["plans"].items()},
                     "regions": {r["label"]: {"radius": r["radius"],
                                              "verts": r["verts"]}
                                 for r in ctx["regions"]}},
        "candidates": [], "best": None,
    }
    checkpoint(ctx)

    if o.regime == "both":
        # reduce's refinement loop runs until the deadline, so a shared
        # budget would starve sculpt entirely: split it down the middle.
        half = o.time_budget / 2.0
        o.deadline = time.time() + half
        regime_reduce(o, ctx)
        o.deadline = time.time() + half
        regime_sculpt(o, ctx)
    else:
        o.deadline = time.time() + o.time_budget
        if o.regime == "reduce":
            regime_reduce(o, ctx)
        else:
            regime_sculpt(o, ctx)

    best = ctx["results"].get("best")
    n = len(ctx["results"]["candidates"])
    passing = sum(1 for c in ctx["results"]["candidates"]
                  if c.get("fitness") is not None)
    log("done: %d candidates (%d passed gates), best=%s"
        % (n, passing, best["id"] if best else "none"))
    log("results: %s" % ctx["results_path"])
    HEARTBEAT["state"] = "done"
    write_heartbeat(o)


if __name__ == "__main__":
    main()
