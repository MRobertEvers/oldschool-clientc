#!/usr/bin/env python3
"""watch_osrsify.py — live browser dashboard for running osrsify searches.

Reads each run's results.json (atomically rewritten by osrsify.py after every
candidate) and serves an exploration page:

  - left sidebar: every run with a liveness dot (the /data poll cross-checks
    each search's heartbeat.json against its pid, so a dead, paused or
    finished search shows within seconds), candidate count, show/hide
    filtering, per-run pause/resume and kill buttons, and a "new run" form
    exposing every osrsify.py option so searches can be launched from the
    browser. Pause is cooperative: the button drops <run>/PAUSE, the search
    idles at the next candidate boundary until it is lifted, and the paused
    time is refunded to the run's time budget.
  - per-run summary strip (candidates, pass rate, best, baseline margin) with
    archive and delete controls. Archive zips the run and its launch log into
    runs/archive/<run>.zip at deflate 9 — worth doing: a wave's bulk is
    uncompressed .bmp render intermediates, so 500 MB routinely lands under
    50 MB. Delete asks first, naming every path and its size, then hands the
    lot to the Recycle Bin. Both run on server threads and report progress
    into /api/jobs, which the page polls for a bar; neither is offered while
    the search is still alive.
  - "best so far" podium and a recent-candidates strip (not the full history)
  - fitness chart: every candidate as a point (reduce: x = kept vertex
    fraction; sculpt: x = iteration), rejected marked along the bottom
  - click any candidate to open a detail view with its bind renders and its
    animation-sweep frames side by side with the baseline's same frames
  - star any candidate to save it as a favorite: its models, renders and
    scores are copied to ~/Documents/osrsify/saves and shown at the top of
    the page, surviving restarts and runs/ cleanups

    python watch_osrsify.py                       # watches runs/osrsify_*
    python watch_osrsify.py --port 9000 runs/osrsify_qbd_reduce

Stdlib only. Read-only over the run directories themselves, except for the
PAUSE flag and the explicit archive/delete controls; the kill and start
controls manage osrsify.py processes on this machine.
"""

import argparse
import glob
import json
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import threading
import time
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))

RUNS = {}  # name -> absolute run dir

# The browser-side renderer: toridraw compiled to wasm by tools/entity_viewer.
# Built with `make -C tools/entity_viewer wasm`; the page loads it and renders
# ev_wire bytes locally, so orbiting costs no server round-trips at all.
EV_WEB = os.path.realpath(os.path.join(HERE, "..", "entity_viewer", "web"))

# Favorites live outside the run tree so a runs/ cleanup can't take them:
# each save is a folder holding the candidate's .ob3 models, its renders, and
# a meta.json snapshot of the scores it had when it was starred.
SAVES = os.path.expanduser(os.path.join("~", "Documents", "osrsify", "saves"))

# One wire build at a time: concurrent requests for the same file would race
# the viewer subprocess over the same output path.
WIRE_LOCK = threading.Lock()


def run_config(run):
    with open(os.path.join(RUNS[run], "results.json"), "r",
              encoding="utf-8") as f:
        return json.load(f).get("config", {})


def seq_frames(seqcfg_path, name):
    """rs2012.seq: [name] sections with frame=packed,len lines.

    Same format osrsify.parse_seq_config reads, but keeping the per-frame
    delay — the live player needs real timing, where the search only needed
    the frame ids. (Copied, not imported: importing osrsify drags in torch.)
    """
    current = None
    frames = []
    with open(seqcfg_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                current = line[1:-1]
            elif current == name and line.startswith("frame="):
                body = line[len("frame="):].split(",")
                try:
                    frames.append((int(body[0]),
                                   int(body[1]) if len(body) > 1 else 1))
                except (ValueError, IndexError):
                    continue
    return frames


def safe_tag(tag):
    return tag and "/" not in tag and "\\" not in tag and ".." not in tag


def run_meta(run, doc):
    """Facts about a run that live outside results.json: whether the defight
    pre-pass rewrote the parts (visible in the swapped base_models paths — the
    swap only happens after a successful repair), and wall-clock timestamps.
    Start time is the run dir's creation time, not results.json's: the
    checkpoint file is atomically replaced after every candidate, so its
    creation time is whatever the last rewrite says it is."""
    d = RUNS[run]
    bm = (doc.get("config") or {}).get("base_models") or []
    meta = {"defight": any("defight" in os.path.normpath(m).split(os.sep)
                           for m in bm)}
    try:
        meta["started"] = os.path.getctime(d)
        meta["updated"] = os.path.getmtime(os.path.join(d, "results.json"))
    except OSError:
        pass
    if meta["defight"]:
        try:
            with open(os.path.join(d, "defight", "report.json"), "r",
                      encoding="utf-8") as f:
                rep = json.load(f)
            meta["defight_summary"] = (
                "fight pixels %s -> %s, +%s verts"
                % (rep.get("fight_pixels_before", "?"),
                   rep.get("fight_pixels_after", "?"),
                   sum(p.get("vertices_added", 0)
                       for p in rep.get("parts", []))))
        except (OSError, ValueError):
            pass
    return meta


def stamp_candidates(run, doc):
    """Runs recorded before osrsify.py stamped candidates with 'ts' still have
    a wall-clock trace on disk: the bind render written right after each
    evaluation. Fill missing timestamps from that mtime so old runs show
    times in the UI too."""
    work = os.path.join(RUNS[run], "work")
    for c in doc.get("candidates") or []:
        if c.get("ts") or not c.get("id") or not safe_tag(c["id"]):
            continue
        try:
            c["ts"] = os.path.getmtime(
                os.path.join(work, "%s_bind_y0.png" % c["id"]))
        except OSError:
            pass


def candidate_models(run, tag, cfg):
    """The .ob3 set a wire build should merge, in the baseline's part order —
    merge order affects face order, so the viewer should merge the way the
    search's own renders did."""
    if tag == "baseline":
        return [m for m in cfg.get("base_models", []) if os.path.isfile(m)]
    cand = os.path.join(RUNS[run], "cand", tag)
    models = [os.path.join(cand, os.path.basename(m))
              for m in cfg.get("base_models", [])]
    models = [m for m in models if os.path.isfile(m)]
    if not models:
        models = sorted(glob.glob(os.path.join(cand, "*.ob3")))
    return models


def save_fav(run, tag):
    """Copy a candidate out of its run dir into SAVES: models, every render
    with its tag prefix, and a meta.json snapshot of its candidate record.
    Returns the meta written. Re-saving an existing favorite refreshes it."""
    with open(os.path.join(RUNS[run], "results.json"), "r",
              encoding="utf-8") as f:
        doc = json.load(f)
    cfg = doc.get("config", {})
    cand = next((c for c in doc.get("candidates") or []
                 if c.get("id") == tag), None)
    d = os.path.join(SAVES, "%s__%s" % (run, tag))
    os.makedirs(d, exist_ok=True)
    models = candidate_models(run, tag, cfg)
    for m in models:
        shutil.copy2(m, os.path.join(d, os.path.basename(m)))
    imgs = []
    work = os.path.join(RUNS[run], "work")
    try:
        for name in sorted(os.listdir(work)):
            if name.startswith(tag + "_") and name.endswith(".png"):
                shutil.copy2(os.path.join(work, name),
                             os.path.join(d, name))
                imgs.append(name)
    except OSError:
        pass
    meta = {"run": run, "tag": tag, "saved": time.time(), "candidate": cand,
            "models": [os.path.basename(m) for m in models], "imgs": imgs}
    tmp = os.path.join(d, "meta.json.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(meta, f)
    os.replace(tmp, os.path.join(d, "meta.json"))
    return meta


def list_favs():
    """Every saved favorite's meta, newest first. A folder without a
    readable meta.json is somebody else's; leave it alone and skip it."""
    out = []
    try:
        names = os.listdir(SAVES)
    except OSError:
        return out
    for n in names:
        try:
            with open(os.path.join(SAVES, n, "meta.json"), "r",
                      encoding="utf-8") as f:
                meta = json.load(f)
        except (OSError, ValueError):
            continue
        meta["_key"] = n
        out.append(meta)
    out.sort(key=lambda m: m.get("saved", 0), reverse=True)
    return out


def build_wire(run, tag=None, seq=None):
    """Ensure <run>/wire/<name>.{model,anim} exists; return its path or
    (None, why). Model wires are per candidate tag; anim wires are per
    sequence (frames come from the cache, so every candidate shares them)."""
    cfg = run_config(run)
    viewer = cfg.get("viewer")
    if not viewer or not os.path.isfile(viewer):
        return None, "run config has no viewer binary"
    wire_dir = os.path.join(RUNS[run], "wire")

    if seq is None:
        prefix = os.path.join(wire_dir, tag)
        out = prefix + ".model"
        models = candidate_models(run, tag, cfg)
        if not models:
            return None, "no models for %r" % tag
        extra = []
    else:
        prefix = os.path.join(wire_dir, "seq_" + seq)
        out = prefix + ".anim"
        models = candidate_models(run, "baseline", cfg)
        frames = seq_frames(cfg.get("seqcfg", ""), seq)
        if not models or not frames:
            return None, "no frames for %r" % seq
        extra = ["--cache", cfg["cache"],
                 "--frames", ",".join(str(f) for f, _ in frames),
                 "--delays", ",".join(str(d) for _, d in frames)]

    with WIRE_LOCK:
        if os.path.isfile(out):
            return out, None
        os.makedirs(wire_dir, exist_ok=True)
        cmd = [viewer]
        for m in models:
            cmd += ["--model", m]
        cmd += extra + ["--wire-out", prefix]
        try:
            r = subprocess.run(cmd, capture_output=True, text=True,
                               timeout=120)
        except (OSError, subprocess.TimeoutExpired) as e:
            return None, str(e)
        if r.returncode != 0 or not os.path.isfile(out):
            return None, (r.stderr or r.stdout or "viewer failed").strip()
    return out, None


def wire_priorities(path):
    """Face-priority census of a wire .model file (ev_wire.h layout: every
    scalar a little-endian int32; priorities packed two 4-bit values per
    byte, even face in the low nibble). Returns {face_count, model_priority,
    counts, flat} where counts is None when the model carries no per-face
    array and flat is the single priority every face draws at (None when the
    model really is banded). Returns None outright when the file does not
    parse."""
    try:
        with open(path, "rb") as f:
            data = f.read()
        vals = struct.unpack_from("<%di" % (len(data) // 4), data)
        if not vals or vals[0] != 0x314D5645:  # "EVM1"
            return None
        pos = 1
        _flags, vcount, fcount, mprio = vals[pos:pos + 4]
        pos += 4
        pos += 3 * vcount + 3 * fcount
        for _ in range(4):  # face colors, alphas, infos, textures
            present = vals[pos]
            pos += 1
            if present:
                pos += fcount
        counts = None
        if vals[pos]:
            packed = vals[pos + 1:pos + 1 + (fcount + 1) // 2]
            counts = {}
            for i in range(fcount):
                b = packed[i >> 1] & 0xFF
                p = (b >> 4) if (i & 1) else (b & 0x0F)
                counts[p] = counts.get(p, 0) + 1
        # Merging parts into one wire always materialises a per-face array,
        # filling it from each part's header priority -- so a *stripped* model
        # arrives here as a uniform array rather than as no array at all.
        # Both mean one flat bucket to the renderer, so report them alike;
        # otherwise a strip reads as a 100%-of-one-band table.
        flat = mprio if counts is None else (
            next(iter(counts)) if len(counts) == 1 else None)
        return {"face_count": fcount, "model_priority": mprio,
                "counts": counts, "flat": flat}
    except (OSError, IndexError, struct.error):
        return None


# ---------------------------------------------------------------------------
# run control: kill a run's search process, or start a new one.

# Every osrsify.py knob, mirrored for the new-run form. kind: 'str' | 'int' |
# 'float' | 'flag' | 'choice' | 'list' (repeatable flag, one value per line).
# default '' means "leave the flag out and let osrsify.py pick its own
# default" (tool paths, optional overrides).
OPTS = [
    dict(flag="mode", kind="choice", choices=["search", "author"],
         default="search", group="inputs",
         help="search for a variant, or author a chosen one into the content "
              "tree and a pack file"),
    dict(flag="preset", kind="str", default="", group="inputs",
         help="name from rs2012_backport_audit/presets.json"),
    dict(flag="model", kind="list", default="", group="inputs",
         help="part .ob3 paths, one per line (overrides preset)"),
    dict(flag="seq", kind="list", default="", group="inputs",
         help="sequence names or bare ids, one per line"),
    dict(flag="seqcfg", kind="str", default="", group="inputs",
         help="rs2012.seq config path"),
    dict(flag="cache", kind="str", default="", group="inputs",
         help="packed dat2 cache dir"),
    dict(flag="out-dir", kind="str", default="", group="inputs",
         help="run directory (blank = runs/osrsify_web_<timestamp>)"),
    dict(flag="backport", kind="flag", default="", group="backport",
         help="re-run the material backport on the lane before the search "
              "sees the parts; the baked OB3s land in <run>/backport and the "
              "content tree is never touched"),
    dict(flag="matte", kind="int", default="", group="backport",
         help="compress each baked face's lightness toward its material's "
              "mean by N% (0-100); 60 is the recipe the current lane carries"),
    dict(flag="face-color-bake", kind="choice",
         choices=["", "modulate", "tint", "off"], default="", group="backport",
         help="how an erased material's frame reaches the face colours it "
              "falls back to (tool default modulate; 'off' is the bare "
              "erase-only fallback)"),
    dict(flag="face-color-strength", kind="int", default="", group="backport",
         help="face-colour bake strength 0-100 (tool default 100)"),
    dict(flag="no-face-alpha-bake", kind="flag", default="", group="backport",
         help="keep erased faces opaque instead of turning the frame's alpha "
              "coverage into face translucency"),
    dict(flag="wisp-alpha", kind="choice", choices=["", "off", "capped", "screen"],
         default="", group="backport",
         help="bound the wisp alpha inference that overrides a material row's "
              "own alpha_mode 0 (tool default off)"),
    dict(flag="backport-cache", kind="str", default="cache.rs727_preeoc",
         group="backport",
         help="HD source cache the bake reads materials out of — NOT --cache, "
              "which is the dat2 cache the sequences decode from"),
    dict(flag="backport-tree", kind="str", default="OSRS-Content/osrs239-content",
         group="backport", help="content tree whose ported lane gets baked"),
    dict(flag="bake-tool", kind="str", default="", group="backport",
         help="material bake binary (blank = default)"),
    dict(flag="bake-timeout", kind="float", default="600", group="backport",
         help="seconds before the bake is abandoned"),
    dict(flag="regime", kind="choice", choices=["reduce", "sculpt", "both"],
         default="reduce", group="search", help="search regime"),
    dict(flag="time-budget", kind="float", default="3600", group="search",
         help="seconds of search after the baseline is banked"),
    dict(flag="fracs", kind="str", default="0.85,0.70,0.55,0.45,0.35,0.20",
         group="search", help="reduce ladder of vertex fractions"),
    dict(flag="seeds", kind="int", default="3", group="search",
         help="seeds per fraction"),
    dict(flag="jitter", kind="int", default="8", group="search",
         help="decimator candidate-pool jitter"),
    dict(flag="max-cost", kind="float", default="0.0", group="search",
         help="decimator collapse cost ceiling (0 = judges decide)"),
    dict(flag="sa-iters", kind="int", default="400", group="search",
         help="sculpt anneal iterations"),
    dict(flag="sa-temp", kind="float", default="0.6", group="search",
         help="sculpt anneal temperature"),
    dict(flag="sa-seed", kind="int", default="1", group="search",
         help="sculpt anneal seed"),
    dict(flag="max-verts", kind="int", default="", group="budget",
         help="reject candidates whose MERGED vertex count across all parts "
              "exceeds this (blank/0 = no budget). Merged, because the parts "
              "are merged before they reach the scene's scratch tier"),
    dict(flag="max-faces", kind="int", default="", group="budget",
         help="reject candidates whose MERGED face count across all parts "
              "exceeds this (blank/0 = no budget)"),
    dict(flag="zbuffer", kind="flag", default="", group="render",
         help="render depth-tested instead of painter's order "
              "(content authored against the z-buffer, e.g. QBD)"),
    dict(flag="frames-per-seq", kind="int", default="4", group="render",
         help="posed frames judged per sequence"),
    dict(flag="angles", kind="int", default="4", group="render",
         help="bind render yaws"),
    dict(flag="pose-angles", kind="int", default="2", group="render",
         help="posed render yaws"),
    dict(flag="pitch", kind="int", default="128", group="render",
         help="camera pitch"),
    dict(flag="tile", kind="int", default="256", group="render",
         help="render tile size"),
    dict(flag="bg", kind="str", default="808080", group="render",
         help="background colour (hex)"),
    dict(flag="id-gate", kind="float", default="55.0", group="gates",
         help="bind identity floor"),
    dict(flag="pose-id-gate", kind="float", default="50.0", group="gates",
         help="posed identity floor"),
    dict(flag="cov-band", kind="str", default="0.5,2.0", group="gates",
         help="allowed candidate/baseline coverage ratio per view"),
    dict(flag="regions", kind="int", default="8", group="regions",
         help="max close-up regions to judge (0 disables)"),
    dict(flag="region-min-verts", kind="int", default="30", group="regions",
         help="ignore rig labels smaller than this"),
    dict(flag="region-pad", kind="float", default="1.3", group="regions",
         help="close-up camera radius = region half-diagonal x pad"),
    dict(flag="region-id-gate", kind="float", default="45.0", group="regions",
         help="minimum per-region close-up identity"),
    dict(flag="defight", kind="choice", choices=["auto", "on", "off"],
         default="auto", group="defight",
         help="repair z-fighting on the base models before the search "
              "(auto = on when zbuffer)"),
    dict(flag="defight-min-pixels", kind="int", default="", group="defight",
         help="pair qualification threshold (tool default 24)"),
    dict(flag="defight-yaws", kind="int", default="", group="defight",
         help="view sweep yaw count (tool default 16)"),
    dict(flag="defight-fight-eps", kind="float", default="", group="defight",
         help="depth-tie epsilon (tool default 2.5)"),
    dict(flag="defight-deltas", kind="str", default="", group="defight",
         help="displacement ladder CSV (tool default 3,6,9,12)"),
    dict(flag="defight-timeout", kind="float", default="600", group="defight",
         help="seconds before the defight tool is abandoned"),
    dict(flag="force-priorities", kind="choice",
         choices=["off", "solve", "keep", "strip"], default="off",
         group="priorities",
         help="painter's priority buckets instead of the z-buffer: 'solve' "
              "re-derives the bands, 'keep' audits the inherited ones, "
              "'strip' removes them so every model depth-sorts as one flat "
              "bucket (run without priorities)"),
    dict(flag="prio-views", kind="int", default="16", group="priorities",
         help="priority solve yaw sweep"),
    dict(flag="prio-pitches", kind="int", default="6", group="priorities",
         help="priority solve elevation sweep"),
    dict(flag="prio-slow", kind="int", default="800", group="priorities",
         help="slow-anneal iterations (0 disables)"),
    dict(flag="prio-slow-views", kind="int", default="8", group="priorities",
         help="slow-anneal yaw sweep"),
    dict(flag="prio-slow-pitches", kind="int", default="4", group="priorities",
         help="slow-anneal elevation sweep"),
    dict(flag="prio-repair", kind="int", default="256", group="priorities",
         help="engine-judged greedy re-banding trials (0 disables)"),
    dict(flag="prio-measure-views", kind="int", default="16",
         group="priorities", help="z-violation audit yaw sweep"),
    dict(flag="prio-measure-pitches", kind="int", default="5",
         group="priorities", help="z-violation audit elevation sweep"),
    dict(flag="prio-measure-res", kind="int", default="192",
         group="priorities", help="z-violation audit render size"),
    dict(flag="prio-zviol-tol", kind="float", default="1.15",
         group="priorities",
         help="reject when violation fraction exceeds baseline*tol + 0.25pp"),
    dict(flag="prio-ghost-tol", kind="float", default="1.25",
         group="priorities",
         help="reject when ghost faces exceed baseline*tol + 16"),
    dict(flag="prio-zviol-weight", kind="float", default="0.4",
         group="priorities",
         help="fitness penalty per pp of violation above baseline"),
    dict(flag="prio-timeout", kind="float", default="3600",
         group="priorities", help="seconds per priority-tool invocation"),
    dict(flag="style-ckpt", kind="str", default="", group="tools",
         help="style judge checkpoint (blank = default)"),
    dict(flag="viewer", kind="str", default="", group="tools",
         help="viewer binary (blank = default)"),
    dict(flag="decimator", kind="str", default="", group="tools",
         help="decimator binary (blank = default)"),
    dict(flag="nudge", kind="str", default="", group="tools",
         help="nudge binary (blank = default)"),
    dict(flag="defight-tool", kind="str", default="", group="tools",
         help="defight binary (blank = default)"),
    dict(flag="prio-tool", kind="str", default="", group="tools",
         help="priority solver binary (blank = default)"),
    dict(flag="viewer-timeout", kind="float", default="300", group="tools",
         help="seconds per viewer render"),
    dict(flag="decimate-timeout", kind="float", default="600", group="tools",
         help="seconds per decimator call"),
    dict(flag="author-from", kind="str", default="", group="author",
         help="directory holding the chosen candidate, e.g. "
              "runs/<study>/cand/<tag>; each part is taken from it by "
              "filename so the parts keep their order"),
    dict(flag="author-model", kind="list", default="", group="author",
         help=".ob3 paths to author, one per line (pairs with --pack-id in "
              "order); leave blank to author the --model/--preset parts"),
    dict(flag="author-out", kind="str", default="", group="author",
         help="where the authored model lands: a directory under the content "
              "tree's models/, or a single .ob3 path when authoring one part"),
    dict(flag="author-suffix", kind="str", default="", group="author",
         help="inserted before .ob3 in the authored filename (e.g. _lowpoly) "
              "so the original stays in place"),
    dict(flag="pack", kind="str", default="", group="author",
         help="pack file to register in, e.g. <lane>/pack/7_models.pack"),
    dict(flag="pack-id", kind="list", default="", group="author",
         help="pack id for each authored model, one per line, paired with "
              "the models in order"),
    dict(flag="no-author-verify", kind="flag", default="", group="author",
         help="skip the post-author render + sequence decode check "
              "(verification is on by default)"),
    dict(flag="dry-run", kind="flag", default="", group="author",
         help="print what authoring would do and touch nothing"),
]
GROUPS = [
    ["inputs", "inputs"],
    ["backport", "material backport"],
    ["search", "search"],
    ["budget", "poly budget"],
    ["render", "render"],
    ["gates", "identity gates"],
    ["regions", "region close-ups"],
    ["defight", "defight pre-pass"],
    ["priorities", "priority solve & z-violation audit"],
    ["author", "author into the content tree"],
    ["tools", "tools & timeouts"],
]

# The guided new-run flow: one decision per screen, in the order the pipeline
# actually applies them. Steps and fields carry `when` gates as
# [name, "in", [values...]] so the whole decision tree lives here as data and
# the browser never has to know what depends on what. A gate may also be a
# *list* of those triples, in which case any one of them passing opens the
# screen -- for the branches two different answers can arrive at.
#
# A field is either {opt: "<flag>"} — mirror that OPTS entry as an input — or
# a {name, cards: [...]} choice rendered as big option cards. A card's `sets`
# map is merged into the launch payload when it is chosen, which is how a
# plain-English answer ("yes, re-bake it") becomes a flag (--backport). Note
# that `sets` reaches the *payload* only: gates read answers, so a step that
# has to open for a `sets` value must gate on the answer that set it. Names
# starting with "_" are wizard-local: they gate later steps and never reach
# the command line on their own.
#
# Screens come from three builders:
#   dict(...)  a decision, written out by hand
#   ask(...)   one input alone on a screen, the step's title asking for it
#   tuned(...) a section of knobs: first "recommended, or walk me through
#              them?", then one screen per knob if they asked for it
# A tuned section's knob screens gate on that section's own answer, and that
# answer is only ever put when the section's parent step was shown -- so
# closing a branch closes everything under it without restating the parent's
# gate on every child.


def ask(key, opt, title, blurb, when=None, widget=None, default=None):
    """One input, one screen. `solo` tells the renderer the title is already
    this field's question, so the input draws bare underneath it. `default`
    overrides the flag's own argparse default, for the cases where the screen
    only makes sense with something already in the box."""
    f = dict(opt=opt)
    if widget:
        f["widget"] = widget
    if default is not None:
        f["default"] = default
    return dict(key=key, title=title, blurb=blurb, when=when, solo=True,
                fields=[f])


def tuned(key, title, blurb, knobs, when=None, auto=None, manual=None,
          extra=None):
    """A section of knobs, as a decision first and numbers second. Nobody
    should have to answer six numbers to start a search, but the numbers still
    have to be reachable without dropping out to the all-options form."""
    gate = "_tune_" + key
    cards = [dict(value="auto", label="Use the recommended settings",
                  desc=auto or "The defaults the existing runs were made with.")]
    cards += extra or []
    cards.append(dict(value="manual", label="Walk me through them",
                      desc=manual or "One knob per screen, with what each one "
                                     "costs you."))
    steps = [dict(key=key, title=title, blurb=blurb, when=when,
                  fields=[dict(name=gate, cards=cards)])]
    steps += [ask("%s:%s" % (key, flag), flag, label, why,
                  when=[gate, "in", ["manual"]])
              for flag, label, why in knobs]
    return steps


def _flat(items):
    out = []
    for it in items:
        if isinstance(it, list):
            out.extend(it)
        else:
            out.append(it)
    return out


WIZARD = _flat([
    dict(key="mode", title="What do you want to do?",
         blurb="A <b>search</b> explores variants of a model and scores each "
               "one against the judges. <b>Authoring</b> is the step after: "
               "you have already picked a winner and want it written into the "
               "content tree and registered in a pack file.",
         fields=[dict(name="mode", cards=[
             dict(value="search", label="Search for a variant",
                  desc="Reduce or sculpt the model, judge every candidate, "
                       "and leave the winners in the run directory."),
             dict(value="author", label="Author a chosen candidate",
                  desc="Copy a candidate you already picked into the content "
                       "tree and register it in a pack."),
         ])]),
    dict(key="source", title="Which model are you working on?",
         blurb="osrsify needs four things: the model's parts, the sequences "
               "to pose it with, the seq config that names them, and the dat2 "
               "cache they all decode from. A preset bundles all four under "
               "one name, so pick one unless you are pointing at something "
               "that has never been searched before.",
         fields=[dict(name="_source", cards=[
             dict(value="preset", label="A saved preset",
                  desc="Named bundles from "
                       "rs2012_backport_audit/presets.json."),
             dict(value="manual", label="Paths I type myself",
                  desc="For a model with no preset yet — you will be asked "
                       "for all four."),
         ])]),
    ask("source-preset", "preset", "Which preset?",
        "Choosing one fills in every part, sequence and cache path it names. "
        "What it resolves to is shown under the picker, so you can see you "
        "picked the right one before anything runs.",
        when=["_source", "in", ["preset"]], widget="preset"),
    ask("source-model", "model", "Which .ob3 parts make up the model?",
        "One path per line, and the order matters: it is the order the parts "
        "are merged in, and later on it is the order pack ids pair with. An "
        "NPC built from a body and a head is two lines here, not two runs.",
        when=["_source", "in", ["manual"]]),
    ask("source-seq", "seq", "Which sequences should it be posed with?",
        "One sequence name per line. Every candidate is rendered across these "
        "as well as in bind pose, because reduction damage usually shows up "
        "in motion first — a shoulder that survives a T-pose can still tear "
        "open mid-swing.",
        when=["_source", "in", ["manual"]]),
    ask("source-seqcfg", "seqcfg", "Where is the seq config?",
        "The config that maps those sequence names onto the frames in the "
        "cache. Without it the names above cannot be resolved and the "
        "animation sweep is skipped.",
        when=["_source", "in", ["manual"]]),
    ask("source-cache", "cache", "Which dat2 cache do the sequences live in?",
        "The packed cache the frames decode from. This is the client's own "
        "cache — not the HD cache the material bake reads, which is asked for "
        "separately if you turn the bake on.",
        when=["_source", "in", ["manual"]]),
    dict(key="where", title="Where should this run write?",
         when=["mode", "in", ["search"]],
         blurb="Every render, candidate and score lands in one run directory, "
               "and the dashboard names the run after it. Leave it blank and "
               "you get runs/osrsify_web_&lt;timestamp&gt;. Give it a "
               "readable name if you plan to compare waves later — the run "
               "name is all you will ever see in the sidebar.",
         fields=[dict(opt="out-dir")]),
    dict(key="where-author", title="Where should the log of this write go?",
         when=["mode", "in", ["author"]],
         blurb="Authoring writes into the content tree, not into a run "
               "directory, so this only decides where the record of what it "
               "did lands — the source and destination of every part, the "
               "pack ids it registered, the merged totals, and whether "
               "verification passed. Leave it blank and you get "
               "runs/osrsify_web_&lt;timestamp&gt;, which the dashboard will "
               "list like any other run. Name it if you want to find this "
               "particular write again.",
         fields=[dict(opt="out-dir")]),
    dict(key="author-src", title="Where is the candidate you picked?",
         when=["mode", "in", ["author"]],
         blurb="Authoring copies models that already exist, so the first "
               "thing it needs is which ones. Usually that is a whole "
               "candidate directory — every part at once, in the order they "
               "were searched in. Arriving here from a result's "
               "<b>author</b> button fills this in, and most of what "
               "follows, from the run that produced it.",
         fields=[dict(name="_authsrc", cards=[
             dict(value="dir", label="A candidate directory",
                  desc="runs/&lt;study&gt;/cand/&lt;tag&gt;, or "
                       "runs/&lt;study&gt;/best for the run's winner."),
             dict(value="files", label="Specific .ob3 files",
                  desc="Name the paths yourself, for authoring parts that "
                       "came from different places."),
         ])]),
    ask("author-src-dir", "author-from", "Which candidate directory?",
        "Point at the directory holding the winner — "
        "runs/&lt;study&gt;/cand/&lt;tag&gt;, or runs/&lt;study&gt;/best for "
        "whatever the run scored highest. Each part is taken out of it by "
        "filename, so the parts keep the order the preset gave them and stay "
        "paired with their pack ids. A path starting with runs/ is resolved "
        "against this tool's directory, not the repo root.",
        when=["_authsrc", "in", ["dir"]]),
    ask("author-src-files", "author-model", "Which .ob3 files?",
        "One path per line. The order is what pairs them with the pack ids "
        "you give later, so keep it the same as the parts order.",
        when=["_authsrc", "in", ["files"]]),
    ask("author-out", "author-out", "Where should the authored model land?",
        "A directory under the content tree's models/, or a single .ob3 path "
        "when there is only one part. This is a real write into the content "
        "tree — the only step in osrsify that is. Coming from a result this "
        "is already the directory the original parts were read from, which "
        "is almost always where you want the new ones.",
        when=["mode", "in", ["author"]]),
    dict(key="author-write", title="Does the original model survive this?",
         when=["mode", "in", ["author"]],
         blurb="The parts being authored carry the same filenames as the "
               "originals they were reduced from, so writing them into the "
               "same directory is a question about those originals. Either "
               "way the pack id you give in a moment ends up pointing at the "
               "new file, so the client loads the new one — what this decides "
               "is whether the old one is still on disk to go back to.",
         fields=[dict(name="_authwrite", cards=[
             dict(value="beside", label="Keep it — write alongside",
                  desc="A suffix goes in before <code>.ob3</code>, so "
                       "<code>rs2012_model_70260.ob3</code> becomes "
                       "<code>rs2012_model_70260_lowpoly.ob3</code> and the "
                       "original is untouched. You name the suffix next."),
             dict(value="over", label="Overwrite the original",
                  desc="The authored model replaces the file it came from. "
                       "Nothing in this tool puts it back — recovering means "
                       "git, or porting the model again."),
         ])]),
    ask("author-suffix", "author-suffix", "What marks the new file?",
        "Inserted before .ob3 in the authored filename. Anything readable "
        "works; _lowpoly is what reduced parts already in the lane use. This "
        "changes the filename only — which pack id it answers to, and so what "
        "the NPC record ends up loading, is the question after next.",
        when=["_authwrite", "in", ["beside"]], default="_lowpoly"),
    ask("author-pack", "pack", "Which pack file registers it?",
        "The client finds a model through a pack file, so a model copied in "
        "without one is invisible. Usually &lt;lane&gt;/pack/7_models.pack, "
        "and that is what this is filled with when you come from a result. "
        "An id already in the pack is repointed; a new one is appended.",
        when=["mode", "in", ["author"]]),
    ask("author-pack-id", "pack-id", "Which pack id does each part get?",
        "One id per line, paired with the models in order. Reuse the ids the "
        "parts already had — which is what this is prefilled with, read off "
        "the original filenames — and the NPC record keeps working "
        "untouched; use new ones and you have to repoint the record's "
        "model&lt;N&gt;= yourself afterwards.",
        when=["mode", "in", ["author"]]),
    dict(key="author-verify", title="Check the models after writing them?",
         when=["mode", "in", ["author"]],
         blurb="Verification re-renders each authored model and decodes every "
               "sequence for it out of the cache. That is what catches a pack "
               "id pointing at the wrong file, or a part the rig can no "
               "longer pose — both of which look perfectly fine on disk and "
               "only break once the client loads them.",
         fields=[dict(name="_authverify", cards=[
             dict(value="verify", label="Verify after writing",
                  desc="Costs seconds. A failure leaves the files where they "
                       "are and says so, so you fix or revert deliberately "
                       "rather than finding out in-game."),
             dict(value="skip", label="Skip the check",
                  desc="Only worth it when you have already verified these "
                       "exact files once.",
                  sets={"no-author-verify": True}),
         ])]),
    dict(key="author-dry", title="Dry run first, or write for real?",
         when=["mode", "in", ["author"]],
         blurb="This is the one step in osrsify that changes the content "
               "tree. A dry run prints every file it would write and where, "
               "the id it would register in which pack, and the merged vertex "
               "and face totals — and touches nothing.",
         fields=[dict(name="_authdry", cards=[
             dict(value="dry", label="Dry run — show me the plan",
                  desc="Nothing is written. Read the log it leaves, then come "
                       "back through <b>use these answers</b> and pick the "
                       "other card.",
                  sets={"dry-run": True}),
             dict(value="real", label="Write it for real",
                  desc="Copies the models in, repoints the pack, and verifies "
                       "unless you turned that off above."),
         ])]),
    dict(key="backport", title="Re-bake the materials first?",
         when=["mode", "in", ["search"]],
         blurb="The backport bakes HD materials down to flat face colours. "
               "It changes face <i>counts</i> as well as colours — fully "
               "transparent faces are dropped — so it has to run before the "
               "baseline is banked, or every score afterwards is measured "
               "against the wrong model. The bake writes into the run "
               "directory and never touches the content lane.",
         fields=[dict(name="_backport", cards=[
             dict(value="no", label="No — search the lane as it stands",
                  desc="Use the OB3s already in the ported lane. Right when "
                       "the lane was baked with the recipe you want."),
             dict(value="yes", label="Yes — bake, then search the result",
                  desc="Re-run the material bake with your own recipe first.",
                  sets={"backport": True}),
         ])]),
    tuned("backport-recipe", "How should the bake look?",
          "<b>matte</b> is the main dial: it compresses each face's lightness "
          "toward its material's mean, so 0 keeps the HD texture's full "
          "contrast and 100 flattens every face to one colour. The lane you "
          "are searching was baked at <b>60</b> — matching it keeps this run "
          "comparable to the earlier waves. The rest decide what happens to "
          "faces whose material was erased.",
          when=["_backport", "in", ["yes"]],
          auto="The bake's own defaults, which is not the same recipe the "
               "lane carries — set matte yourself if you want to match it.",
          knobs=[
              ("matte", "How flat should the colours go?",
               "0&ndash;100. Each baked face's lightness is compressed toward "
               "its material's mean by this much, so 0 keeps the HD texture's "
               "full contrast and 100 flattens every face of a material to a "
               "single colour. The current lane was baked at 60. Note this "
               "only moves faces whose texture was erased — a face that kept "
               "its texture looks identical at every setting."),
              ("face-color-bake", "How does an erased material reach the face?",
               "When a material is erased the face falls back to its own "
               "colour. <b>modulate</b> multiplies that colour by the frame's "
               "and keeps the shading detail; <b>tint</b> replaces it "
               "outright; <b>off</b> leaves the bare erase-only fallback, "
               "which is flatter than anything the engine ever drew."),
              ("face-color-strength", "How strongly?",
               "0&ndash;100, how far each face is pulled toward the baked "
               "colour. Only does anything when the mode above is not off."),
              ("no-face-alpha-bake", "Keep erased faces fully opaque?",
               "Normally the frame's alpha coverage becomes face "
               "translucency, which is how wisps, smoke and glass survive the "
               "bake. Turn this on when that reads as holes punched in solid "
               "geometry instead."),
              ("wisp-alpha", "Bound the wisp alpha inference?",
               "The bake can infer translucency for a material whose row "
               "claims alpha_mode 0. <b>capped</b> and <b>screen</b> bound how "
               "far that inference may go; <b>off</b> lets the row's own alpha "
               "stand unchallenged."),
              ("backport-cache", "Which HD cache holds the materials?",
               "The source cache the bake reads materials out of. This is "
               "<i>not</i> the dat2 cache the sequences decode from — "
               "confusing the two is the usual reason a bake reports finding "
               "no materials at all."),
              ("backport-tree", "Which content tree gets baked?",
               "The bake reads this tree's ported lane and writes the result "
               "into the run directory. The lane itself is never modified, "
               "whatever recipe you pick."),
          ]),
    dict(key="render", title="How does this content draw?",
         when=["mode", "in", ["search"]],
         blurb="This is the single most consequential answer here, because "
               "it decides what the judges are looking at. The 2012 engine "
               "sorts faces by painter's priority; some content — the QBD "
               "among it — was authored against a depth buffer instead and "
               "falls apart under painter's order. Judge it the way it will "
               "actually be drawn.",
         fields=[dict(name="_render", cards=[
             dict(value="painter", label="Painter's order, with priority bands",
                  desc="Faces draw in priority bands, depth-sorted inside "
                       "each. The 2012 default. What happens to the bands "
                       "themselves is the next question."),
             dict(value="flat", label="Painter's order, no priorities",
                  desc="Still painter's order, but the per-face bands are "
                       "stripped first, so the whole model depth-sorts as one "
                       "flat bucket. The honest baseline when the inherited "
                       "bands are meaningless. Answers the next question for "
                       "you — this is <code>--force-priorities strip</code>.",
                  sets={"force-priorities": "strip"}),
             dict(value="zbuffer", label="Depth-tested (z-buffer)",
                  desc="Per-pixel depth. For content authored against a "
                       "z-buffer, where priorities were never solved.",
                  sets={"zbuffer": True}),
         ])]),
    dict(key="priorities", title="What should happen to the face priorities?",
         when=["_render", "in", ["painter"]],
         blurb="You kept the bands, so this decides what the search does with "
               "them. Priorities are the bands the engine draws in; ported "
               "content usually inherits bands that meant something in the "
               "source engine and nothing here.",
         fields=[dict(name="force-priorities", cards=[
             dict(value="off", label="Leave them alone",
                  desc="Search the priorities the models already carry, "
                       "unaudited."),
             dict(value="solve", label="Re-derive the bands",
                  desc="Search for a banding that minimises z-violations. "
                       "Slow, but it is the only option that can improve a "
                       "badly-banded model."),
             dict(value="keep", label="Keep, but audit",
                  desc="Inherit the bands and measure how badly they "
                       "violate depth, so candidates cannot make it worse."),
         ])]),
    tuned("prio-solve", "How hard should the solver look?",
          "The solver sweeps the model from many directions, counts the faces "
          "that draw in front of something they are actually behind, and "
          "anneals the bands to reduce them. More views and more iterations "
          "buys a better banding and a much longer wait — but this runs "
          "<i>once</i>, before the search starts, so it is a fixed cost rather "
          "than a per-candidate one.",
          when=["force-priorities", "in", ["solve"]],
          auto="16 yaws, 6 pitches, an 800-iteration slow anneal and 256 "
               "repair trials. Minutes, not hours, on a model this size.",
          knobs=[
              ("prio-views", "How many yaws does the solver sweep?",
               "Each yaw is one more direction the banding has to be correct "
               "from. Too few and the solver optimises for angles you will "
               "never see the model at; every extra one costs solve time "
               "linearly."),
              ("prio-pitches", "How many elevations?",
               "The same trade for camera height. Ground-level content needs "
               "fewer of these than something you fly around."),
              ("prio-slow", "How long should the slow anneal run?",
               "Iterations of the fine anneal that runs after the first "
               "pass. This is where most of the solve time goes and where "
               "most of the improvement comes from. 0 skips it entirely."),
              ("prio-slow-views", "How many yaws during the slow anneal?",
               "The slow pass re-renders on every iteration, so this "
               "multiplies straight into the cost — it is deliberately lower "
               "than the first-pass sweep."),
              ("prio-slow-pitches", "How many elevations during the slow anneal?",
               "Same multiplier, for camera height."),
              ("prio-repair", "How many engine-judged re-banding trials?",
               "A final greedy pass that moves individual faces between bands "
               "and keeps the move only if the engine's own render agrees it "
               "helped. Cheap next to the anneal; 0 disables it."),
              ("prio-timeout", "How long may one solver call take?",
               "Seconds before the priority tool is abandoned and the run "
               "gives up on solving. Raise it for a heavy model rather than "
               "letting a legitimate long solve get killed."),
          ]),
    tuned("prio-audit", "How tightly should depth errors be policed?",
          "Every candidate is swept for z-violations and ghost faces, then "
          "compared against the baseline's own count — so what is being "
          "policed is the <i>regression</i>, not the absolute number. The "
          "tolerances decide how much worse a candidate may get before it is "
          "rejected outright; the weight decides what a smaller regression "
          "costs it in fitness.",
          when=[["force-priorities", "in", ["solve", "keep"]],
                ["_render", "in", ["flat"]]],
          auto="16 yaws at 192px, rejecting above 1.15&times; the baseline's "
               "violations, with a 0.4 fitness penalty per point over.",
          knobs=[
              ("prio-measure-views", "How many yaws does the audit sweep?",
               "This one runs on <i>every</i> candidate, so unlike the solver "
               "sweep it multiplies into the whole search. Doubling it "
               "roughly halves how many candidates fit in the time budget."),
              ("prio-measure-pitches", "How many elevations?",
               "Same per-candidate multiplier, for camera height."),
              ("prio-measure-res", "At what resolution?",
               "Violations are counted in pixels, so a smaller render finds "
               "fewer of them and a larger one is slower. Changing this "
               "changes what the tolerances below mean, so move it before you "
               "tune them, not after."),
              ("prio-zviol-tol", "How much worse may the violations get?",
               "A candidate is rejected when its violation fraction exceeds "
               "baseline &times; this + 0.25 percentage points. 1.0 means "
               "\"never worse than the original\"; the slack above 1 is what "
               "lets a reduction trade a little depth error for a lot of "
               "geometry."),
              ("prio-ghost-tol", "How many ghost faces may appear?",
               "Same shape of rule for faces that draw where nothing should "
               "be visible at all: rejected above baseline &times; this + 16. "
               "Ghosts read worse to the eye than violations do, so this is "
               "the tolerance to tighten first."),
              ("prio-zviol-weight", "What does a small regression cost?",
               "Fitness penalty per percentage point of violation above the "
               "baseline. This is the soft version of the tolerance above: it "
               "does not reject anything, it just makes a slightly worse "
               "candidate lose to a slightly cleaner one."),
          ]),
    dict(key="defight", title="Repair z-fighting before the search?",
         when=["mode", "in", ["search"]],
         blurb="Coplanar faces that flicker against each other will confuse "
               "the judges on every single candidate, because the flicker "
               "moves with the camera and reads to a judge as a difference "
               "the reduction caused. The defight pre-pass nudges the "
               "offenders apart once, up front. <b>auto</b> turns it on "
               "exactly when you are rendering z-buffered, which is when it "
               "matters most.",
         fields=[dict(name="defight", cards=[
             dict(value="auto", label="Auto",
                  desc="On when rendering z-buffered, off otherwise."),
             dict(value="on", label="Always repair",
                  desc="Run the pre-pass whatever the render mode."),
             dict(value="off", label="Never",
                  desc="Leave coplanar faces exactly as authored."),
         ])]),
    tuned("defight-knobs", "How aggressive should the repair be?",
          "The pre-pass renders the model from a ring of yaws, finds face "
          "pairs that tie on depth over enough pixels to be visible, and "
          "walks a ladder of displacements until they stop tying. Every knob "
          "here trades how many pairs it catches against how far it is "
          "willing to move your geometry to fix them.",
          when=["defight", "in", ["on", "auto"]],
          auto="24-pixel threshold, 16 yaws, and a 3/6/9/12 displacement "
               "ladder — enough for the QBD without visibly moving anything.",
          knobs=[
              ("defight-min-pixels", "How big must a fight be to count?",
               "A pair has to tie on depth across at least this many pixels "
               "before it is worth moving. Lower catches subtler flicker and "
               "nudges far more geometry; higher only fixes what you would "
               "actually notice."),
              ("defight-yaws", "How many directions is it looked for from?",
               "Fights are view-dependent, so a pair that is invisible from "
               "the front can flicker badly from the side. More yaws find "
               "more of them, at a linear cost in pre-pass time."),
              ("defight-fight-eps", "How close counts as a depth tie?",
               "The depth difference below which two faces are treated as "
               "coplanar. Raise it to catch near-coplanar pairs that only "
               "fight at some distances; raise it too far and ordinary "
               "neighbouring surfaces start qualifying."),
              ("defight-deltas", "How far may faces be moved?",
               "The displacement ladder, in model units, tried smallest "
               "first. The pre-pass stops at the first rung that resolves the "
               "fight, so a long ladder is not a large move — it is a "
               "fallback for pairs the small nudges could not separate."),
              ("defight-timeout", "How long may the pre-pass run?",
               "Seconds before the defight tool is abandoned. It runs once, "
               "before the clock on the search starts."),
          ]),
    dict(key="regime", title="How should the search change the model?",
         when=["mode", "in", ["search"]],
         blurb="<b>Reduce</b> collapses edges to remove geometry — this is "
               "what you want if the model is too heavy. <b>Sculpt</b> keeps "
               "the vertex count and anneals positions to look more like the "
               "engine's own style. They answer different questions; run "
               "both only if you have the time budget for it.",
         fields=[dict(name="regime", cards=[
             dict(value="reduce", label="Reduce — fewer faces",
                  desc="Edge collapse down a ladder of vertex fractions."),
             dict(value="sculpt", label="Sculpt — same count, better shape",
                  desc="Simulated annealing over vertex nudges."),
             dict(value="both", label="Both",
                  desc="Reduce first, then sculpt. Costs roughly double."),
         ])]),
    tuned("reduce-knobs", "How far should the reduction go?",
          "The ladder is the fractions of the original vertex count to aim "
          "for, each rung a separate candidate. This is the knob that decides "
          "what the search is even <i>able</i> to find: a ladder that stops "
          "at 0.55 can never produce a model half the size, however long you "
          "let it run.",
          when=["regime", "in", ["reduce", "both"]],
          auto="The ladder 0.85 / 0.70 / 0.55 / 0.45 / 0.35 / 0.20 at three "
               "seeds a rung — a wide first sweep you can narrow later.",
          knobs=[
              ("fracs", "Which vertex fractions should it try?",
               "Comma-separated fractions of the original vertex count. A "
               "wide ladder tells you where the quality cliff is; a narrow "
               "one spends every candidate near a target you already trust. "
               "If you are searching against a poly budget, put the rungs "
               "around the fraction that lands just under it — rungs above "
               "it are rejected unjudged and cost you nothing but they find "
               "you nothing either."),
              ("seeds", "How many seeds per rung?",
               "The decimator is randomised, so different seeds at the same "
               "target collapse different edges and score differently — often "
               "by more than a whole rung's worth. More seeds is the cheapest "
               "quality you can buy here, since every one is a real "
               "candidate."),
              ("jitter", "How freely may it pick edges?",
               "How far the decimator may stray from collapsing the "
               "strictly-cheapest edge. 0 makes every seed produce the same "
               "model; higher spreads the seeds further apart and finds "
               "collapses the greedy order would never reach."),
              ("max-cost", "Should there be a hard cost ceiling?",
               "Refuse any collapse whose error exceeds this, whatever the "
               "target says. 0 leaves it to the judges, which is usually "
               "right — the judges see the rendered result and this only sees "
               "the geometry."),
          ]),
    tuned("sculpt-knobs", "How should the anneal behave?",
          "Sculpting keeps every vertex and moves them, so its knobs control "
          "how far from the original shape the search is willing to wander "
          "and how long it spends wandering.",
          when=["regime", "in", ["sculpt", "both"]],
          auto="400 iterations at temperature 0.6 — a conservative anneal "
               "that stays close to the original silhouette.",
          knobs=[
              ("sa-iters", "How many iterations per candidate?",
               "More iterations means a finer result and a proportionally "
               "longer wait for <i>each</i> candidate, so this trades "
               "directly against how many candidates the time budget fits."),
              ("sa-temp", "How adventurous should it be?",
               "Temperature is how readily the anneal accepts a move that "
               "scores worse, in the hope of escaping a local optimum. Higher "
               "explores further from the original shape and risks drifting "
               "past the identity gates; lower polishes what is already "
               "there."),
              ("sa-seed", "Which seed?",
               "Change it to get a different anneal from identical settings. "
               "Keep it to reproduce a run exactly."),
          ]),
    dict(key="budget", title="Is there a hard poly budget?",
         when=["mode", "in", ["search"]],
         blurb="A budget rejects any candidate over the limit before it is "
               "judged at all, which stops the search spending its clock on "
               "variants you could never ship. The counts are <b>merged</b> "
               "totals across all parts, because an NPC's parts are merged "
               "before they reach the scene's scratch tier — that merged "
               "total is what actually has to fit.",
         fields=[dict(name="_budget", cards=[
             dict(value="none", label="No budget",
                  desc="Judge every candidate on looks alone."),
             dict(value="limit", label="Cap the merged counts",
                  desc="Reject anything above a vertex or face ceiling, "
                       "before it costs any render time."),
         ])]),
    ask("budget-verts", "max-verts", "What is the vertex ceiling?",
        "Merged across every part. Blank or 0 means no vertex limit. Watch "
        "the scene's scratch tier here: exceeding the tier does not clip the "
        "model, it makes the whole NPC silently fail to draw.",
        when=["_budget", "in", ["limit"]]),
    ask("budget-faces", "max-faces", "What is the face ceiling?",
        "Merged across every part, same as above. Faces and vertices do not "
        "fall at the same rate under reduction, so a candidate can clear one "
        "ceiling and fail the other — the run's rejection list will tell you "
        "which of the two is actually binding.",
        when=["_budget", "in", ["limit"]]),
    tuned("judging", "How much drift from the original is allowed?",
          "The content preserver scores 0&ndash;100 for how much of the "
          "original model survives. The gates are floors, not weights: a "
          "candidate below one is thrown out however good its style score "
          "is. Raise them when you are protecting a character people will "
          "recognise, lower them when you want the search to range further "
          "than it currently does.",
          when=["mode", "in", ["search"]],
          auto="Identity floors of 55 in bind pose and 50 posed — loose "
               "enough that the style score does most of the deciding.",
          knobs=[
              ("id-gate", "Identity floor in bind pose",
               "The straightforward one: how much of the model has to survive "
               "when it is standing still. A candidate under this is rejected "
               "outright."),
              ("pose-id-gate", "Identity floor across the animations",
               "The same floor applied to the posed frames, and usually the "
               "one that actually bites — damage that hides in a T-pose shows "
               "up the moment a limb bends. It is set lower than the bind "
               "gate because posing costs identity even on an untouched "
               "model."),
              ("cov-band", "How much may the silhouette's area change?",
               "Low,high ratio of candidate to baseline coverage per view. "
               "This is the cheap sanity check that catches a candidate that "
               "collapsed into nothing or exploded into stray geometry, "
               "before the expensive judges are asked about it."),
          ]),
    tuned("regions", "Should small details be judged close up?",
          "A whole-model render is dominated by its big surfaces, so a ruined "
          "face or a melted hand can pass a whole-model score without "
          "trouble. Region close-ups re-judge the model rig-part by rig-part, "
          "each filling the frame. This is the most expensive part of judging "
          "and the part most likely to catch the failure you would actually "
          "have noticed in game.",
          when=["mode", "in", ["search"]],
          auto="Up to 8 close-ups per candidate, at an identity floor of 45.",
          extra=[dict(value="off", label="Skip the close-ups",
                      desc="Judge whole-model renders only. Much faster per "
                           "candidate, and blind to local damage.",
                      sets={"regions": "0"})],
          knobs=[
              ("regions", "How many close-ups per candidate?",
               "The largest rig parts are judged first, up to this many. Each "
               "one is a full extra render and judge pass, so this multiplies "
               "straight into per-candidate cost. 0 turns close-ups off."),
              ("region-min-verts", "How small a part is worth judging?",
               "Rig labels with fewer vertices than this are skipped. Raise "
               "it to stop spending close-ups on parts too small to matter; "
               "lower it if the detail you care about is genuinely tiny."),
              ("region-pad", "How tightly should the camera frame a part?",
               "The close-up radius is the part's half-diagonal times this. "
               "Just above 1 crops hard to the part alone; higher keeps some "
               "surrounding geometry in shot, which is often what makes the "
               "damage legible."),
              ("region-id-gate", "Identity floor per close-up",
               "The worst single region has to clear this. It is set below "
               "the whole-model gate because a part judged alone, filling the "
               "frame, is scored far more harshly than the same part seen in "
               "context."),
          ]),
    tuned("renderdetail", "What do the judges get to see?",
          "Every candidate is rendered from several yaws in bind pose and "
          "again across the animation sweep. More angles and more frames make "
          "the scores steadier and every candidate slower — which trades "
          "directly against how many candidates fit in the time budget. Noisy "
          "scores and few candidates are both ways to lose the same search.",
          when=["mode", "in", ["search"]],
          auto="4 bind yaws, 2 posed yaws and 4 frames per sequence at 256px.",
          knobs=[
              ("frames-per-seq", "How many frames per sequence?",
               "Frames sampled across each animation. More of them catch "
               "damage that only appears at one point in a swing; fewer let "
               "you afford more candidates."),
              ("angles", "How many yaws in bind pose?",
               "Bind-pose renders are the cheapest views you get, and they "
               "are what the style judge leans on hardest."),
              ("pose-angles", "How many yaws per posed frame?",
               "This one multiplies against the frame count above, so it is "
               "the most expensive number on this screen — 2 yaws over 4 "
               "frames is already 8 renders per sequence."),
              ("pitch", "What camera pitch?",
               "The elevation everything is judged from. The default looks at "
               "the model roughly the way the game's camera does; change it "
               "only if this content is normally seen from somewhere else."),
              ("tile", "At what resolution?",
               "Render size in pixels. Larger sees finer damage and costs "
               "quadratically. It also changes what every identity number "
               "means, so a run at a different tile size is not directly "
               "comparable to the ones already on the dashboard."),
              ("bg", "What background colour?",
               "Hex, no #. Matters more than it sounds: a model that shares "
               "its background's value loses silhouette to it, and the "
               "coverage check is measured against exactly this colour."),
          ]),
    dict(key="time", title="How long should it search?",
         when=["mode", "in", ["search"]],
         blurb="The clock starts after the baseline is banked and the "
               "pre-passes are done, and it stops while the run is paused. "
               "The search stops at the budget wherever it has got to — "
               "everything judged so far is already on disk and already "
               "ranked, so a short budget costs you candidates, never "
               "results. You can always start a second wave from the answers "
               "this one used.",
         fields=[dict(opt="time-budget")]),
])

# The guided flow saves what it launched so you can re-open a past run's exact
# answers. Lives beside the favorites, not in the repo: these are one user's
# habits, not project state.
RECENTS = os.path.join(os.path.dirname(SAVES), "recents.json")
RECENTS_MAX = 40
PRESETS = os.path.join(os.path.dirname(HERE), "rs2012_backport_audit",
                       "presets.json")


def load_recents():
    try:
        with open(RECENTS, "r", encoding="utf-8") as f:
            got = json.load(f)
        return got if isinstance(got, list) else []
    except (OSError, ValueError):
        return []


def record_recent(entry):
    """Push one launch onto the recents list, newest first. Best-effort: a
    failure here must never take down a launch that already succeeded."""
    try:
        items = [r for r in load_recents() if r.get("run") != entry.get("run")]
        items.insert(0, entry)
        os.makedirs(os.path.dirname(RECENTS), exist_ok=True)
        with open(RECENTS, "w", encoding="utf-8") as f:
            json.dump(items[:RECENTS_MAX], f, indent=1)
    except (OSError, ValueError, TypeError):
        pass


def load_presets():
    try:
        with open(PRESETS, "r", encoding="utf-8") as f:
            got = json.load(f)
        return got if isinstance(got, dict) else {}
    except (OSError, ValueError):
        return {}


PROCS = {}        # run name -> Popen, for searches this dashboard started
AUTO_SCAN = True  # rescan runs/osrsify_* on /data (off with explicit dirs)


def scan_runs():
    """Pick up run dirs created after startup — e.g. dashboard-started runs
    or waves launched from another terminal."""
    if not AUTO_SCAN:
        return
    for d in glob.glob(os.path.join(HERE, "runs", "osrsify_*")):
        d = os.path.abspath(d)
        if os.path.isdir(d):
            RUNS.setdefault(os.path.basename(d), d)


def run_pids(run):
    """PIDs of osrsify.py searches feeding this run, found by command line:
    the run dir's basename appears in the wave's --out-dir argument. The
    watcher itself is excluded ('osrsify.py' is a substring of its name)."""
    token = os.path.basename(RUNS[run])
    pids = []
    try:
        if os.name == "nt":
            ps = ("Get-CimInstance Win32_Process -Filter "
                  "\"Name='python.exe' or Name='py.exe'\" | ForEach-Object "
                  "{ '{0}|{1}' -f $_.ProcessId, $_.CommandLine }")
            r = subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                               capture_output=True, text=True, timeout=30)
        else:
            r = subprocess.run(["ps", "-eo", "pid=,args="],
                               capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return pids
    for line in (r.stdout or "").splitlines():
        line = line.strip()
        pid, _, cl = line.partition("|" if os.name == "nt" else " ")
        if (pid.isdigit() and "osrsify.py" in cl
                and "watch_osrsify" not in cl and token in cl):
            pids.append(int(pid))
    return pids


def pid_alive(pid):
    """Cheap liveness probe for one pid — no subprocess spawn, so it is safe
    to call on every dashboard poll. On Windows os.kill(pid, 0) would
    TERMINATE the process (sig is an exit code there), so use OpenProcess."""
    if not isinstance(pid, int) or pid <= 0:
        return False
    if os.name == "nt":
        import ctypes
        k32 = ctypes.windll.kernel32
        SYNCHRONIZE = 0x00100000
        h = k32.OpenProcess(SYNCHRONIZE, 0, pid)
        if not h:
            return False
        WAIT_TIMEOUT = 0x102  # still running
        r = k32.WaitForSingleObject(h, 0)
        k32.CloseHandle(h)
        return r == WAIT_TIMEOUT
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except (PermissionError, OSError):
        return True


def run_state(run):
    """Liveness verdict for a run, from <run>/heartbeat.json (osrsify.py
    beats every 5s from a daemon thread) cross-checked against its recorded
    pid. Waves started before the heartbeat existed report 'unknown' and the
    UI falls back to results.json freshness. 'pause_requested' means the
    PAUSE flag is down but the search has not reached a candidate boundary
    yet (or predates the pause feature and never will)."""
    d = RUNS[run]
    st = {"paused_flag": os.path.isfile(os.path.join(d, "PAUSE"))}
    try:
        with open(os.path.join(d, "heartbeat.json"), "r",
                  encoding="utf-8") as f:
            hb = json.load(f)
    except (OSError, ValueError):
        hb = None
    if hb is None:
        p = PROCS.get(run)
        if p is not None:
            st["state"] = "running" if p.poll() is None else "dead"
            st["pid"] = p.pid
        else:
            st["state"] = "unknown"
        return st
    st["pid"] = hb.get("pid")
    st["beat_age"] = max(0, round(time.time() - (hb.get("ts") or 0)))
    hb_state = hb.get("state", "running")
    if hb_state == "done":
        st["state"] = "finished"
    elif not pid_alive(st["pid"]):
        st["state"] = "dead"
    elif hb_state == "paused":
        st["state"] = "paused"
    elif st["paused_flag"]:
        st["state"] = "pause_requested"
    else:
        st["state"] = "running"
    return st


def set_paused(run, on):
    """Drop or lift the cooperative PAUSE flag osrsify.py checks between
    candidates. Works on any run dir, including waves launched outside the
    dashboard — but only searches new enough to know the flag honor it."""
    flag = os.path.join(RUNS[run], "PAUSE")
    if on:
        with open(flag, "w", encoding="utf-8") as f:
            f.write("paused via dashboard %s\n"
                    % time.strftime("%Y-%m-%d %H:%M:%S"))
        return ("pause requested — the search pauses at the next candidate "
                "boundary and its time budget stops burning while paused. "
                "(Searches started before the pause feature ignore this.)")
    if os.path.isfile(flag):
        os.remove(flag)
    return "resumed"


def kill_run(run):
    """Kill every osrsify process tree feeding the run; returns the pids."""
    pids = set(run_pids(run))
    p = PROCS.get(run)
    if p is not None and p.poll() is None:
        pids.add(p.pid)
    pids.discard(os.getpid())
    for pid in sorted(pids):
        try:
            if os.name == "nt":
                # /T takes the whole tree: the search plus whatever solver,
                # viewer or decimator subprocess it is inside at the moment
                subprocess.run(["taskkill", "/T", "/F", "/PID", str(pid)],
                               capture_output=True, timeout=30)
            else:
                os.kill(pid, signal.SIGTERM)
        except (OSError, subprocess.TimeoutExpired):
            pass
    # a killed run cannot resume, and a stale PAUSE flag would insta-pause
    # the next wave someone points at this run dir
    try:
        os.remove(os.path.join(RUNS[run], "PAUSE"))
    except OSError:
        pass
    return sorted(pids)


# ---- archive / delete ------------------------------------------------------
# A finished wave is routinely 60k files and 12 GB, so neither zipping one nor
# handing one to the Recycle Bin can happen inside a request: each POST starts
# a worker thread that publishes counters into JOBS, and the page polls
# /api/jobs for a progress bar until nothing is live.

JOBS = {}                    # id -> progress dict
JOBS_LOCK = threading.Lock()
JOB_SEQ = [0]
JOB_KEEP = 15 * 60           # a finished job's card lingers this long
ARCHIVE_DIR = os.path.join(HERE, "runs", "archive")


def run_paths(run):
    """Everything that belongs to a run: the run directory plus the sibling
    launch logs waves leave beside it (runs/<name>.log). Archive and delete
    both work from exactly this list and the confirm dialog prints it, so
    nothing is zipped or binned that the user did not see named."""
    d = RUNS[run]
    paths = [d] if os.path.isdir(d) else []
    for suffix in (".log", ".err.log"):
        if os.path.isfile(d + suffix):
            paths.append(d + suffix)
    return paths


def walk_files(paths):
    """(archive name, absolute path, size) for every file under `paths`.
    Names are rooted at each entry's own basename, so the zip unpacks as
    <run>/... with <run>.log beside it."""
    out = []
    for p in paths:
        if os.path.isfile(p):
            try:
                out.append((os.path.basename(p), p, os.path.getsize(p)))
            except OSError:
                pass
            continue
        base = os.path.dirname(os.path.abspath(p))
        for root, _dirs, names in os.walk(p, onerror=lambda e: None):
            for n in names:
                full = os.path.join(root, n)
                try:
                    size = os.path.getsize(full)
                except OSError:
                    continue
                out.append((os.path.relpath(full, base).replace("\\", "/"),
                            full, size))
    return out


def path_stats(paths):
    """(file count, total bytes). Cheap enough to call in a poll loop, which
    is how the delete gets a real progress bar: the tree shrinking under it
    is the only observable the shell operation offers."""
    n = tot = 0
    for p in paths:
        if os.path.isfile(p):
            try:
                tot += os.path.getsize(p)
            except OSError:
                continue
            n += 1
            continue
        for root, _dirs, names in os.walk(p, onerror=lambda e: None):
            for name in names:
                try:
                    tot += os.path.getsize(os.path.join(root, name))
                except OSError:
                    continue
                n += 1
    return n, tot


def archive_of(run):
    """The zip this run was last archived to, if it still exists."""
    z = os.path.join(ARCHIVE_DIR, run + ".zip")
    try:
        st = os.stat(z)
    except OSError:
        return None
    return {"path": z, "bytes": st.st_size, "ts": st.st_mtime}


def job_new(kind, run, **extra):
    with JOBS_LOCK:
        JOB_SEQ[0] += 1
        job = {"id": "%s-%d" % (kind, JOB_SEQ[0]), "kind": kind, "run": run,
               "state": "working", "phase": "measuring", "files": 0,
               "files_done": 0, "bytes": 0, "bytes_done": 0, "out_bytes": 0,
               "started": time.time(), "ended": None, "error": None,
               "cancel": False, "dest": None}
        job.update(extra)
        JOBS[job["id"]] = job
    return job


def job_set(job, **kw):
    with JOBS_LOCK:
        job.update(kw)


def job_busy(run):
    with JOBS_LOCK:
        return any(j["run"] == run and j["state"] == "working"
                   for j in JOBS.values())


def job_list():
    """Live jobs plus recently finished ones, oldest first. The page stops
    polling once none are live."""
    now = time.time()
    with JOBS_LOCK:
        for jid, j in list(JOBS.items()):
            if j["ended"] and now - j["ended"] > JOB_KEEP:
                del JOBS[jid]
        return sorted((dict(j) for j in JOBS.values()),
                      key=lambda j: j["started"])


def job_start(kind, run, fn, **extra):
    """Run fn(job) on a worker thread; return the job so the POST can answer
    immediately with something the page can start polling."""
    job = job_new(kind, run, **extra)

    def body():
        try:
            fn(job)
        except BaseException as e:
            job_set(job, state="error", phase="failed",
                    error="%s: %s" % (type(e).__name__, e))
        with JOBS_LOCK:
            if job["state"] == "working":
                job["state"] = "done"
                job["phase"] = "done"
            if job["ended"] is None:
                job["ended"] = time.time()

    threading.Thread(target=body, daemon=True, name="job-" + job["id"]).start()
    return job


def do_archive(job):
    """Zip a run at deflate level 9 into runs/archive/<run>.zip. Writes to a
    .part file and renames on success, so an interrupted archive never leaves
    a truncated zip that looks like a good backup. Non-destructive: the run
    dir is untouched, deleting it is a separate, confirmed step."""
    paths = run_paths(job["run"])
    items = walk_files(paths)
    total = sum(s for _n, _f, s in items)
    job_set(job, files=len(items), bytes=total, phase="compressing")
    os.makedirs(ARCHIVE_DIR, exist_ok=True)
    dest = os.path.join(ARCHIVE_DIR, job["run"] + ".zip")
    tmp = dest + ".part"
    job_set(job, dest=dest)
    done_n = done_b = skipped = 0
    try:
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED,
                             allowZip64=True, compresslevel=9) as z:
            for name, full, size in items:
                if job["cancel"]:
                    raise RuntimeError("cancelled")
                try:
                    z.write(full, name)
                except (OSError, ValueError) as e:
                    # a render still being written by a live search, say —
                    # skip it and keep going rather than lose the whole zip
                    skipped += 1
                    job_set(job, skipped=skipped, last_error=str(e))
                done_n += 1
                done_b += size
                if done_n % 16 == 0:
                    # the write position is the live compressed size; it is a
                    # readout, so never let it be the thing that kills the job
                    try:
                        out = z.fp.tell()
                    except (AttributeError, OSError):
                        out = job["out_bytes"]
                    job_set(job, files_done=done_n, bytes_done=done_b,
                            out_bytes=out)
    except BaseException:
        try:
            os.remove(tmp)
        except OSError:
            pass
        raise
    os.replace(tmp, dest)
    job_set(job, files_done=done_n, bytes_done=done_b, phase="done",
            out_bytes=os.path.getsize(dest))


def do_delete(job):
    """Send a run to the Recycle Bin, polling the shrinking tree for progress
    while the shell does the work."""
    paths = run_paths(job["run"])
    n, total = path_stats(paths)
    job_set(job, files=n, bytes=total, phase="recycling",
            bin="Recycle Bin" if os.name == "nt" else "Trash")
    box = []
    t = threading.Thread(target=lambda: box.append(recycle(paths)),
                         daemon=True, name="recycle-" + job["run"])
    t.start()
    while t.is_alive():
        t.join(0.4)
        left_n, left_b = path_stats(paths)
        job_set(job, files_done=max(0, n - left_n),
                bytes_done=max(0, total - left_b))
    err = box[0] if box else "the recycle thread died without a verdict"
    if err:
        raise RuntimeError(err)
    job_set(job, files_done=n, bytes_done=total, phase="done")
    RUNS.pop(job["run"], None)
    PROCS.pop(job["run"], None)


def recycle(paths):
    """Send paths to the Recycle Bin. Returns None, or a message on failure.

    Windows: SHFileOperationW, the same call Explorer's Delete makes, so a run
    lands in the bin as one restorable entry instead of 60k. FOF_NOCONFIRMATION
    answers every prompt with yes — including the one Windows raises when an
    item is too large for the bin, where the answer is a permanent delete. A
    headless server has no one to click a dialog, so that has to be decided up
    front; the confirm dialog says as much.
    """
    paths = [os.path.abspath(p).rstrip("\\/") for p in paths]
    if not paths:
        return "nothing to delete"
    if os.name != "nt":
        return _trash_posix(paths)
    import ctypes
    from ctypes import wintypes

    class SHFILEOPSTRUCTW(ctypes.Structure):
        _fields_ = [("hwnd", wintypes.HWND),
                    ("wFunc", wintypes.UINT),
                    ("pFrom", wintypes.LPCWSTR),
                    ("pTo", wintypes.LPCWSTR),
                    ("fFlags", ctypes.c_uint),
                    ("fAnyOperationsAborted", wintypes.BOOL),
                    ("hNameMappings", ctypes.c_void_p),
                    ("lpszProgressTitle", wintypes.LPCWSTR)]

    FO_DELETE = 0x0003
    FOF_SILENT, FOF_NOCONFIRMATION = 0x0004, 0x0010
    FOF_ALLOWUNDO, FOF_NOERRORUI = 0x0040, 0x0400
    # pFrom is a double-null-terminated list, which a plain Python str cannot
    # express (ctypes stops at the first NUL) — hand it a buffer instead
    buf = ctypes.create_unicode_buffer("\0".join(paths) + "\0")
    op = SHFILEOPSTRUCTW()
    op.wFunc = FO_DELETE
    op.pFrom = ctypes.cast(buf, wintypes.LPCWSTR)
    op.fFlags = (FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT
                 | FOF_NOERRORUI)
    ole = ctypes.windll.ole32
    hr = ole.OleInitialize(None)
    try:
        rc = ctypes.windll.shell32.SHFileOperationW(ctypes.byref(op))
    finally:
        if hr in (0, 1):
            ole.OleUninitialize()
    if rc:
        return "SHFileOperation failed (0x%X)" % (rc & 0xFFFFFFFF)
    if op.fAnyOperationsAborted:
        return "the shell aborted the delete part-way"
    return None


def _trash_posix(paths):
    """freedesktop.org trash: move the item into ~/.local/share/Trash/files
    and record where it came from, so the desktop's Restore works."""
    home = os.path.expanduser("~")
    root = os.environ.get("XDG_DATA_HOME") or os.path.join(home, ".local",
                                                           "share")
    files, info = (os.path.join(root, "Trash", "files"),
                   os.path.join(root, "Trash", "info"))
    stamp = time.strftime("%Y-%m-%dT%H:%M:%S")
    try:
        os.makedirs(files, exist_ok=True)
        os.makedirs(info, exist_ok=True)
        for p in paths:
            name = os.path.basename(p)
            n = 1
            while os.path.exists(os.path.join(files, name)):
                name = "%s.%d" % (os.path.basename(p), n)
                n += 1
            with open(os.path.join(info, name + ".trashinfo"), "w",
                      encoding="utf-8") as f:
                f.write("[Trash Info]\nPath=%s\nDeletionDate=%s\n"
                        % (p, stamp))
            shutil.move(p, os.path.join(files, name))
    except OSError as e:
        return "trash move failed: %s" % e
    return None


def start_run(form):
    """Assemble an osrsify.py command from the form's options and spawn it
    detached, logging to <out-dir>/launch.log. Returns the launch facts."""
    form = dict(form)
    if not form.get("preset") and not all(
            form.get(k) for k in ("model", "seq", "seqcfg", "cache")):
        raise ValueError("need model + seq + seqcfg + cache, or a preset")
    out_dir = form.get("out-dir") or os.path.join(
        "runs", "osrsify_web_%s" % time.strftime("%Y%m%d_%H%M%S"))
    if not os.path.isabs(out_dir):
        out_dir = os.path.join(HERE, out_dir)
    form["out-dir"] = out_dir = os.path.abspath(out_dir)
    cmd = [sys.executable, "-u", os.path.join(HERE, "osrsify.py")]
    for spec in OPTS:
        v = form.get(spec["flag"])
        if v in (None, False, ""):
            continue
        if spec["kind"] == "flag":
            cmd.append("--" + spec["flag"])
        elif spec["kind"] == "list":
            for item in str(v).replace("\r", "").split("\n"):
                if item.strip():
                    cmd += ["--" + spec["flag"], item.strip()]
        else:
            cmd += ["--" + spec["flag"], str(v)]
    os.makedirs(out_dir, exist_ok=True)
    log_path = os.path.join(out_dir, "launch.log")
    creation = 0
    if os.name == "nt":
        creation = (subprocess.CREATE_NEW_PROCESS_GROUP
                    | getattr(subprocess, "CREATE_NO_WINDOW", 0))
    with open(log_path, "ab") as logf:
        logf.write((subprocess.list2cmdline(cmd) + "\n").encode("utf-8"))
        logf.flush()
        p = subprocess.Popen(cmd, cwd=HERE, stdin=subprocess.DEVNULL,
                             stdout=logf, stderr=subprocess.STDOUT,
                             creationflags=creation)
    name = os.path.basename(out_dir)
    RUNS[name] = out_dir
    PROCS[name] = p
    # keep the raw form, wizard answers and all, so "start from this one"
    # can reopen the flow on the exact answers that produced this run
    record_recent({"run": name, "ts": time.time(), "form": form,
                   "mode": form.get("mode") or "search",
                   "label": recent_label(form),
                   "cmd": subprocess.list2cmdline(cmd)})
    return {"run": name, "pid": p.pid, "log": log_path,
            "cmd": subprocess.list2cmdline(cmd)}


def recent_label(form):
    """One line naming what a launch was pointed at — a preset name, or the
    model ids stripped out of the part paths."""
    if form.get("preset"):
        return "preset %s" % form["preset"]
    ids = []
    for line in str(form.get("model") or "").replace("\r", "").split("\n"):
        stem = os.path.splitext(os.path.basename(line.strip()))[0]
        if stem:
            ids.append(stem.replace("rs2012_model_", ""))
    return ", ".join(ids) if ids else "—"


PAGE = """<!doctype html>
<meta charset="utf-8">
<title>osrsify live</title>
<style>
  body { background:#17181c; color:#d8d9de; font:14px/1.45 system-ui, sans-serif;
         margin:0; padding:1rem 1.5rem 4rem; }
  h1 { font-size:1.15rem; } h2 { font-size:1rem; margin:1.4rem 0 .4rem; }
  h3 { font-size:.85rem; margin:.9rem 0 .3rem; color:#9aa;
       text-transform:uppercase; letter-spacing:.06em; }
  .muted { color:#8b8e99; } .row { display:flex; flex-wrap:wrap; gap:.5rem; }
  .strip { display:flex; gap:1.2rem; flex-wrap:wrap; background:#1d1f25;
           border:1px solid #2c2e36; border-radius:8px; padding:.5rem .9rem; }
  .strip b { color:#e6b450; }
  input.flt { width:4.2em; background:#14151a; color:#d8d9de;
              border:1px solid #2c2e36; border-radius:4px; padding:.1em .3em;
              font:inherit; }
  .strip.fltbar { margin-top:.35rem; gap:.9rem; align-items:center;
                  font-size:.9em; }
  a.viewbtn { display:inline-block; background:#26405f; color:#cfe2f7;
              border:1px solid #3a5f8a; border-radius:5px; padding:.05em .6em;
              text-decoration:none; font-size:.8em; vertical-align:middle; }
  a.viewbtn:hover { background:#2f4f75; }
  button.authbtn { background:#2b3a2b; color:#bfe0bf; border:1px solid #46703f;
                   border-radius:5px; padding:.05em .6em; font-size:.8em;
                   cursor:pointer; vertical-align:middle; }
  button.authbtn:hover { background:#35492f; color:#dff0df; }
  .card .cardops { float:right; margin-right:.35em; }
  .star { cursor:pointer; color:#8b8e99; font-size:1.1em; padding:0 .2em;
          user-select:none; }
  .star:hover { color:#e6b450; }
  .star.fav { color:#e6b450; }
  .card .star { float:right; }
  .card { border:1px solid #2c2e36; border-radius:8px; padding:.45rem .6rem;
          background:#1d1f25; cursor:pointer; max-width:290px; }
  .card:hover { border-color:#5a5e6c; }
  .card.best { border-color:#4f8f4f; }
  .card.rej { opacity:.5; }
  .card .tiles img { height:86px; image-rendering:pixelated; margin:1px;
                     border-radius:3px; }
  .tag { display:inline-block; background:#2a2d36; border-radius:4px;
         padding:0 .45em; margin-right:.4em; font-family:monospace;
         font-size:.85em; }
  .badge { display:inline-block; background:#274a2c; color:#9fd9a5;
           border:1px solid #3f7a48; border-radius:4px; padding:0 .5em;
           margin-left:.5em; font-size:.65em; vertical-align:middle;
           text-transform:uppercase; letter-spacing:.06em; }
  .badge.st-running { background:#1f3a22; color:#7ec97e; border-color:#2f5a35; }
  .badge.st-paused { background:#20304a; color:#7e9cc9; border-color:#31507a; }
  .badge.st-dead { background:#402226; color:#c96a6a; border-color:#6a3540; }
  .badge.st-finished { background:#2a2d36; color:#9aa; border-color:#3c404c; }
  .badge.st-pending { background:#3a3325; color:#e6b450; border-color:#5f5535; }
  .spinner { display:inline-block; width:.7em; height:.7em; flex:0 0 auto;
             box-sizing:border-box; border:2px solid #4a4e5c;
             border-top-color:#e6b450; border-radius:50%;
             animation:spin .8s linear infinite; vertical-align:-.1em; }
  .badge .spinner { width:.85em; height:.85em; margin-right:.3em; }
  @keyframes spin { to { transform:rotate(360deg); } }
  button.pausebtn { background:#20304a; color:#7e9cc9; border:1px solid #31507a;
           border-radius:4px; padding:.1em .6em; font:inherit; font-size:.85em;
           cursor:pointer; }
  button.pausebtn:hover { color:#b9d4f0; border-color:#4a6f9f; }
  button.pausebtn[disabled] { opacity:.45; cursor:default; }
  button.dangerbtn { background:#3a2226; color:#c98a8a; border:1px solid #6a3540;
           border-radius:4px; padding:.1em .6em; font:inherit; font-size:.85em;
           cursor:pointer; }
  button.dangerbtn:hover { color:#ffb3b3; border-color:#8f4550; }
  button.dangerbtn[disabled] { opacity:.45; cursor:default; }
  button.dangerbtn[disabled]:hover { color:#c98a8a; border-color:#6a3540; }
  /* job cards: archive/delete run on server threads, so the page watches
     them from a corner instead of blocking on the request */
  #jobs { position:fixed; right:1rem; bottom:1rem; width:350px; z-index:18;
          display:flex; flex-direction:column; gap:.5rem; }
  .job { background:#1d1f25; border:1px solid #3a3e4a; border-radius:8px;
         padding:.5rem .7rem .55rem; font-size:.85em;
         box-shadow:0 6px 20px rgba(0,0,0,.5); }
  .job.done { border-color:#3f7a48; } .job.error { border-color:#6a3540; }
  .job .jt { display:flex; gap:.45rem; align-items:baseline; }
  .job .jt .k { color:#e6b450; font-size:.72em; letter-spacing:.06em;
                text-transform:uppercase; flex:0 0 auto; }
  .job .jt .n { flex:1; min-width:0; overflow:hidden; white-space:nowrap;
                text-overflow:ellipsis; color:#d8d9de; }
  .job .jt .x { cursor:pointer; color:#8b8e99; flex:0 0 auto; padding:0 .1em; }
  .job .jt .x:hover { color:#e6e8ee; }
  .job .bar { height:6px; background:#2c2e36; border-radius:99px;
              margin:.42rem 0 .3rem; overflow:hidden; }
  .job .bar i { display:block; height:100%; background:#4f8f4f;
                transition:width .35s linear; }
  .job.delete .bar i { background:#b8737f; }
  .job.error .bar i { background:#c96a6a; }
  .job .sub { color:#8b8e99; font-size:.92em; }
  .job .sub b { color:#d8d9de; font-weight:600; }
  /* delete confirmation: names every path, states what the bin can and
     cannot give back, and stays disabled until the box is ticked */
  #confirm { position:fixed; inset:0; background:rgba(10,10,12,.9);
             display:none; overflow:auto; padding:3rem 2rem; z-index:19; }
  #confirm .inner { background:#1d1f25; border:1px solid #6a3540;
                    border-radius:10px; padding:1rem 1.4rem 1.1rem;
                    max-width:580px; margin:0 auto; }
  #confirm h2 { margin:.1rem 0 .5rem; color:#eceef4; font-size:1.2rem; }
  #confirm p { color:#a6a9b4; margin:.4rem 0; }
  #confirm .paths { background:#14151a; border:1px solid #2c2e36;
                    border-radius:6px; padding:.4rem .6rem; margin:.6rem 0;
                    font-family:monospace; font-size:.78em; color:#9aa;
                    max-height:7rem; overflow:auto; white-space:pre-wrap;
                    word-break:break-all; }
  #confirm .ack { display:flex; gap:.5em; align-items:flex-start;
                  color:#d8d9de; margin:.8rem 0 1rem; cursor:pointer;
                  font-size:.92em; }
  #confirm .btns { display:flex; gap:.6rem; align-items:center; }
  #confirm button { border-radius:6px; padding:.4em 1.1em; font:inherit;
                    cursor:pointer; }
  #confirm button.go { background:#5a2630; color:#ffc9c9;
                       border:1px solid #8f4550; }
  #confirm button.go[disabled] { opacity:.35; cursor:not-allowed; }
  #confirm button.no { background:transparent; color:#8b8e99;
                       border:1px solid #2c2e36; }
  #confirm button.no:hover { color:#d8d9de; border-color:#5a5e6c; }
  canvas.chart { background:#1d1f25; border:1px solid #2c2e36;
                 border-radius:8px; width:100%; max-width:820px; height:180px; }
  .halfrow { display:flex; gap:.8rem; flex-wrap:wrap; max-width:820px; }
  .halfrow canvas.chart { flex:1 1 300px; width:auto; min-width:0;
                          height:150px; }
  .legend { display:flex; gap:1rem; flex-wrap:wrap; align-items:center;
            font-size:.75em; color:#8b8e99; margin:.15rem 0 .3rem;
            max-width:820px; }
  .legend span { white-space:nowrap; }
  .legend i { display:inline-block; margin-right:.35em; }
  .legend .sw { width:.8em; height:.8em; border-radius:50%;
                vertical-align:-.1em; }
  .legend .swline { width:1.3em; height:0; border-top:2px solid;
                    vertical-align:.25em; }
  .legend .swdash { width:1.3em; height:0; border-top:2px dashed;
                    vertical-align:.25em; }
  .legend .swband { width:1.3em; height:.8em; vertical-align:-.1em; }
  .legend .swtick { width:.45em; height:.8em; vertical-align:-.1em; }
  /* Left sidebar holds run status/filtering; the main column scrolls beside
     it. Each run's own header sticks to the top of the viewport; sections
     scope the stickiness so the next run's header replaces the previous. */
  #layout { display:flex; align-items:flex-start; gap:1.4rem; }
  #sidebar { position:sticky; top:0; flex:0 0 250px; width:250px;
             max-height:100vh; overflow-y:auto; box-sizing:border-box;
             padding:.2rem 0 1rem; }
  #maincol { flex:1; min-width:0; }
  #sidectl { display:flex; gap:.4rem; align-items:center; flex-wrap:wrap;
             margin:.5rem 0 .6rem; }
  .runhead { position:sticky; top:0; z-index:9;
             background:#17181c; border-bottom:1px solid #2c2e36;
             padding:.15rem 0 .5rem; }
  .runhead h2 { margin:.4rem 0 .4rem; }
  .chip { cursor:pointer; border:1px solid #2c2e36; border-radius:999px;
          padding:.05em .7em; font-size:.8em; color:#8b8e99; background:#1d1f25;
          user-select:none; white-space:nowrap; }
  .chip.on { color:#d8d9de; border-color:#3a5f8a; background:#26405f; }
  .chip:hover { border-color:#5a5e6c; }
  .runitem { display:flex; align-items:center; gap:.4em; margin:.3rem 0;
             padding:.28em .5em; border:1px solid #2c2e36; border-radius:6px;
             background:#1d1f25; color:#8b8e99; font-size:.8em;
             cursor:pointer; user-select:none; }
  .runitem.on { color:#d8d9de; border-color:#3a5f8a; background:#26405f; }
  .runitem:hover { border-color:#5a5e6c; }
  .runitem .rname { flex:1; min-width:0; overflow:hidden;
                    text-overflow:ellipsis; white-space:nowrap; }
  .runitem .cnt { color:#8b8e99; font-size:.85em; }
  .runitem .kill, .runitem .clone, .runitem .pause {
                   visibility:hidden; background:none;
                   border:none; cursor:pointer; font:inherit;
                   padding:0 .15em; line-height:1; }
  .runitem .kill { color:#c96a6a; }
  .runitem .clone { color:#7ea6d9; }
  .runitem .pause { color:#c9b47e; }
  .runitem:hover .kill, .runitem:hover .clone,
  .runitem:hover .pause { visibility:visible; }
  .runitem .kill:hover { color:#ff9d9d; }
  .runitem .clone:hover { color:#b9d4f0; }
  .runitem .pause:hover { color:#ecd9a8; }
  .dot { display:inline-block; width:.55em; height:.55em;
         border-radius:50%; background:#8b8e99; flex:0 0 auto; }
  .dot.live { background:#7ec97e; box-shadow:0 0 5px #7ec97e; }
  .dot.stale { background:#e6b450; }
  .dot.idle { background:#8b8e99; }
  .dot.off { background:#c96a6a; }
  .dot.paused { background:#7e9cc9; box-shadow:0 0 5px #7e9cc9; }
  #newrunbtn { margin-top:.8rem; width:100%; background:#26405f;
               color:#cfe2f7; border:1px solid #3a5f8a; border-radius:6px;
               padding:.35em .6em; font:inherit; cursor:pointer; }
  #newrunbtn:hover { background:#2f4f75; }
  #newrun { position:fixed; inset:0; background:rgba(10,10,12,.88);
            overflow:auto; padding:2rem; display:none;
            z-index:16; /* above the candidate modal, below the lightbox */ }
  #newrun .inner { background:#1d1f25; border:1px solid #3a3e4a;
                   border-radius:10px; padding:1rem 1.4rem; max-width:1100px;
                   margin:0 auto; }
  #newrun .close { float:right; cursor:pointer; font-size:1.4rem;
                   color:#8b8e99; }
  #newrun fieldset { border:1px solid #2c2e36; border-radius:8px;
                     margin:.7rem 0; padding:.5rem .8rem .7rem; }
  #newrun legend { color:#9aa; text-transform:uppercase; font-size:.72rem;
                   letter-spacing:.06em; padding:0 .4em; }
  .optgrid { display:grid; gap:.45rem .9rem;
             grid-template-columns:repeat(auto-fill, minmax(230px, 1fr)); }
  .opt label { display:block; font-size:.75em; color:#8b8e99;
               margin-bottom:.12em; font-family:monospace; }
  .opt input[type=text], .opt input[type=number], .opt select,
  .opt textarea, #nr-prefill {
    width:100%; box-sizing:border-box; background:#14151a; color:#d8d9de;
    border:1px solid #2c2e36; border-radius:4px; padding:.25em .4em;
    font:inherit; font-size:.85em; }
  #nr-prefill { width:auto; }
  .opt textarea { min-height:3.4em; resize:vertical; font-family:monospace; }
  .opt.wide { grid-column:1 / -1; }
  .opt .chk { display:flex; gap:.4em; align-items:center; color:#d8d9de;
              font-size:.85em; }
  #nr-launch { background:#274a2c; color:#9fd9a5; border:1px solid #3f7a48;
               border-radius:6px; padding:.35em 1.1em; font:inherit;
               cursor:pointer; }
  #nr-launch:hover { background:#2f5a35; }
  /* ---- guided flow: one decision per screen ---- */
  #newrun .inner.wz { max-width:720px; }
  .wztabs { float:right; display:flex; gap:.4rem; margin-right:1.6rem; }
  .wzbar { height:4px; background:#2c2e36; border-radius:99px; margin:.7rem 0;
           overflow:hidden; }
  .wzbar i { display:block; height:100%; background:#4f8f4f;
             transition:width .25s ease; }
  .wzq { font-size:1.5rem; line-height:1.25; margin:.2rem 0 .5rem;
         color:#eceef4; font-weight:600; }
  .wzblurb { color:#a6a9b4; font-size:.95em; margin-bottom:1.1rem;
             max-width:62ch; }
  .wzblurb b { color:#d8d9de; }
  .wzcards { display:grid; gap:.5rem; margin-bottom:1rem; }
  .wzcard { border:1px solid #2c2e36; background:#1a1c21; border-radius:9px;
            padding:.6rem .85rem; cursor:pointer; display:flex; gap:.7rem;
            align-items:flex-start; }
  .wzcard:hover { border-color:#5a5e6c; background:#1f2229; }
  .wzcard.on { border-color:#4f8f4f; background:#1c2620; }
  .wzcard .pip { flex:0 0 auto; width:1.05em; height:1.05em; margin-top:.15em;
                 border-radius:50%; border:2px solid #4a4e5c; box-sizing:border-box; }
  .wzcard.on .pip { border-color:#4f8f4f; background:#4f8f4f;
                    box-shadow:inset 0 0 0 3px #1c2620; }
  .wzcard .lbl { color:#e3e5ec; font-weight:600; font-size:.95em; }
  .wzcard .desc { color:#8b8e99; font-size:.85em; margin-top:.1em; }
  .wzcard .desc code, .wzcard .desc b { color:#a8adbb; }
  .wzcard .desc code { font-family:monospace; }
  .wzf { margin:0 0 .85rem; }
  .wzf .flagname { font-family:monospace; font-size:.78em; color:#8b8e99; }
  .wzf .wzlbl { color:#d8d9de; font-size:.92em; font-weight:600; }
  .wzf .wzhelp { color:#8b8e99; font-size:.82em; margin-top:.25em;
                 max-width:62ch; }
  .wzf .wzhelp code { font-family:monospace; color:#7e9cc9; }
  .wzf .wzhelp .dflt { color:#6f727c; }
  .wzf input[type=text], .wzf input[type=number], .wzf select,
  .wzf textarea {
    width:100%; box-sizing:border-box; background:#14151a; color:#d8d9de;
    border:1px solid #2c2e36; border-radius:5px; padding:.32em .5em;
    font:inherit; font-size:.9em; margin-top:.15em; }
  .wzf textarea { min-height:4.2em; resize:vertical; font-family:monospace; }
  .wzf .chk { display:flex; gap:.45em; align-items:center; color:#d8d9de;
              font-size:.9em; margin-top:.2em; }
  .wznav { display:flex; gap:.6rem; align-items:center;
           border-top:1px solid #2c2e36; padding-top:.8rem; margin-top:.4rem; }
  .wznav button { background:#26405f; color:#cfe2f7; border:1px solid #3a5f8a;
                  border-radius:6px; padding:.4em 1.2em; font:inherit;
                  cursor:pointer; }
  .wznav button:hover { background:#2f4f75; }
  .wznav button.ghost { background:transparent; color:#8b8e99;
                        border-color:#2c2e36; }
  .wznav button.ghost:hover { color:#d8d9de; border-color:#5a5e6c; }
  .wznav button.go { background:#274a2c; color:#9fd9a5; border-color:#3f7a48; }
  .wznav button.go:hover { background:#2f5a35; }
  .wzrec { border:1px solid #2c2e36; background:#1a1c21; border-radius:8px;
           padding:.45rem .7rem; margin-bottom:.4rem; display:flex;
           gap:.7rem; align-items:center; }
  .wzrec:hover { border-color:#5a5e6c; }
  .wzrec .grow { flex:1; min-width:0; }
  .wzrec .nm { color:#e3e5ec; font-size:.9em; overflow:hidden;
               text-overflow:ellipsis; white-space:nowrap; }
  .wzrec .sub { color:#8b8e99; font-size:.78em; }
  .wzrec button { background:#26405f; color:#cfe2f7; border:1px solid #3a5f8a;
                  border-radius:5px; padding:.15em .7em; font:inherit;
                  font-size:.8em; cursor:pointer; white-space:nowrap; }
  .wzrec button:hover { background:#2f4f75; }
  .wzplan { border:1px solid #46703f; background:#1b2419; border-radius:9px;
            padding:.7rem .9rem; margin-bottom:1rem; font-size:.9em;
            line-height:1.55; max-width:62ch; }
  .wzplan.dry { border-color:#6b5a2c; background:#221f14; }
  .wzplan .hd { font-weight:600; color:#bfe0bf; margin-bottom:.35rem; }
  .wzplan.dry .hd { color:#e6b450; }
  .wzplan div + div { margin-top:.35rem; }
  .wzplan code { background:#14151a; border-radius:3px; padding:0 .3em;
                 font-size:.9em; word-break:break-all; }
  .wzplan .muted { font-size:.9em; }
  .wzsum { width:100%; border-collapse:collapse; font-size:.85em;
           margin-bottom:.8rem; }
  .wzsum td { border-top:1px solid #24262e; padding:.22em .5em .22em 0;
              vertical-align:top; }
  .wzsum td.k { font-family:monospace; color:#8b8e99; white-space:nowrap;
                width:1%; }
  .wzsum td.v { color:#d8d9de; word-break:break-all; }
  .wzcmd { background:#14151a; border:1px solid #2c2e36; border-radius:6px;
           padding:.5rem .6rem; font-family:monospace; font-size:.78em;
           color:#9aa; white-space:pre-wrap; word-break:break-all;
           max-height:9rem; overflow:auto; }
  @media (max-width: 900px) {
    #layout { display:block; }
    #sidebar { position:static; width:auto; max-height:none; }
  }
  #modal { position:fixed; inset:0; background:rgba(10,10,12,.88);
           overflow:auto; padding:2rem; display:none;
           z-index:15; /* above the sticky run bar/headers (10/9), below the lightbox (20) */ }
  #modal .inner { background:#1d1f25; border:1px solid #3a3e4a;
                  border-radius:10px; padding:1rem 1.4rem; max-width:1200px;
                  margin:0 auto; }
  #modal img { height:150px; image-rendering:pixelated; margin:2px;
               border-radius:4px; }
  #modal .close { float:right; cursor:pointer; font-size:1.4rem;
                  color:#8b8e99; }
  #modal .pair { margin:.2rem 0; }
  #modal .lbl { font-family:monospace; color:#8b8e99; font-size:.85em; }
  #modal img { cursor:zoom-in; }
  #lightbox { position:fixed; inset:0; background:rgba(5,5,6,.96);
              display:none; z-index:20; overflow:hidden; cursor:grab;
              user-select:none; }
  #lightbox img { position:absolute; left:50%; top:50%;
                  image-rendering:pixelated; pointer-events:none; }
  #lightbox .hint { position:absolute; bottom:.8rem; left:0; right:0;
                    text-align:center; color:#8b8e99; font-size:.85em; }
</style>
<div id="layout">
<aside id="sidebar">
  <h1>osrsify live</h1>
  <div class="muted" id="stamp" style="font-size:.75em"></div>
  <div id="sidectl">
    <span class="chip" onclick="selectAllRuns(true)" title="show every run">all</span>
    <span class="chip" onclick="selectAllRuns(false)" title="hide every run">none</span>
    <label class="muted" style="font-size:.75rem; cursor:pointer; white-space:nowrap">
      <input type="checkbox" id="latest-only"> latest only</label>
  </div>
  <div id="runchips"></div>
  <button id="newrunbtn" onclick="openNewRun()">＋ new run</button>
</aside>
<main id="maincol">
  <div id="root" class="muted">loading…</div>
</main>
</div>
<div id="modal" onclick="if(event.target===this)closeModal()"><div class="inner" id="modal-body"></div></div>
<div id="newrun" onclick="if(event.target===this)closeNewRun()">
  <div class="inner" id="nr-inner">
    <span class="close" onclick="closeNewRun()">✕</span>
    <span class="wztabs">
      <span class="chip" id="nr-tab-guided" onclick="setNrView('guided')">guided</span>
      <span class="chip" id="nr-tab-all" onclick="setNrView('all')">all options</span>
    </span>
    <h2>start a new run</h2>
    <div id="nr-guided"></div>
    <div id="nr-all" style="display:none">
      <div class="muted" style="font-size:.85em">
        every osrsify.py option — blank fields fall back to the tool's own
        defaults; hover a field for its meaning. Needs <b>model + seq + seqcfg +
        cache</b>, or a <b>preset</b>. &nbsp;prefill from
        <select id="nr-prefill"><option value="">— run —</option></select></div>
      <form id="nr-form" onsubmit="return false"></form>
      <div style="margin:.8rem 0 .2rem; display:flex; gap:.8rem; align-items:center">
        <button id="nr-launch" onclick="submitNewRun()">launch</button>
        <span id="nr-status" class="muted" style="font-size:.85em"></span>
      </div>
    </div>
  </div>
</div>
<div id="confirm" onclick="if(event.target===this)closeConfirm()">
  <div class="inner" id="confirm-body"></div>
</div>
<div id="jobs"></div>
<div id="lightbox"><img id="lb-img"><div class="hint">scroll to zoom &nbsp;·&nbsp; drag to pan &nbsp;·&nbsp; Esc / click to close</div></div>
<script>
const esc = s => String(s).replace(/[&<>"]/g,
  c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
// every osrsify.py option (server-injected), for the new-run form
const OPTS = __OPTS__;
const GROUPS = __GROUPS__;
const WIZARD = __WIZARD__;
const OPT_BY = {};
for (const o of OPTS) OPT_BY[o.flag] = o;
let DATA = {};
let FAVS = [];

// favorites: starred candidates copied server-side to ~/Documents/osrsify/saves
const favKey = (run, tag) => run + '__' + tag;
function isFav(run, tag) { return FAVS.some(f => f._key === favKey(run, tag)); }
async function refreshFavs() {
  try { FAVS = await (await fetch('/favs')).json(); } catch (e) {}
}
async function toggleFav(run, tag) {
  const url = isFav(run, tag)
    ? '/unfav/' + encodeURIComponent(favKey(run, tag))
    : `/fav/${encodeURIComponent(run)}/${encodeURIComponent(tag)}`;
  const res = await fetch(url, { method: 'POST' });
  if (!res.ok) alert(await res.text());
  await refreshFavs();
  render();
}
async function modalFav(run, tag, el) {
  await toggleFav(run, tag);
  el.textContent = isFav(run, tag) ? '★' : '☆';
  el.classList.toggle('fav', isFav(run, tag));
}
function favCard(f) {
  const c = f.candidate || { id: f.tag, status: 'saved' };
  const live = !!DATA[f.run];
  const imgs = (f.imgs || []).filter(i => i.startsWith(f.tag + '_bind_')).slice(0, 4);
  return `<div class="card">
    <span class="star fav" title="remove favorite"
      onclick="event.stopPropagation();toggleFav('${esc(f.run)}','${esc(f.tag)}')">★</span>
    <span class="tag">${esc(f.tag)}</span>${scoreline(c)}
    <div class="muted" style="font-size:.8em">${esc(f.run)} · saved ${fmtTime(f.saved)}</div>
    <div class="tiles">` +
    imgs.map(i => `<img loading="lazy" src="/savimg/${esc(f._key)}/${esc(i)}"
                     onerror="this.remove()">`).join('') + `</div>` +
    (live
      ? `<a class="viewbtn" href="/view/${esc(f.run)}/${encodeURIComponent(f.tag)}"
           target="_blank" onclick="event.stopPropagation()">viewer</a>
         ${authBtn(f.run, f.tag)}`
      : `<span class="muted" style="font-size:.75em">run offline — models kept in
           saves/${esc(f._key)}</span>`) +
    `</div>`;
}

// per-run fraction-range filter; survives the 5s refresh because render()
// repaints the inputs from this map
const FLT0 = {min: '', max: '', vmin: '', vmax: '', fmin: '', fmax: ''};
let FILTERS = {};
function fltOf(run) { return FILTERS[run] || FLT0; }
function setFilter(run, which, val) {
  FILTERS[run] = Object.assign({}, fltOf(run));
  FILTERS[run][which] = val;
  render();
}
function clearFilter(run) { delete FILTERS[run]; render(); }
const fltOn = f => Object.values(f).some(v => v !== '');
const polyOn = f => [f.vmin, f.vmax, f.fmin, f.fmax].some(v => v !== '');
// Merged vertex/face counts for a candidate. attempt() only stamps them once a
// candidate survives the budget, so an over-budget reject carries its counts
// in the status string instead — recover those too, since they are exactly the
// ones a poly filter is being used to hunt for.
function polyOf(c) {
  const p = c.params || {};
  if (p.verts !== undefined && p.faces !== undefined)
    return {v: p.verts, f: p.faces};
  const m = /over-budget:(\\d+)v\\/(\\d+)f/.exec(c.status || '');
  return m ? {v: +m[1], f: +m[2]} : null;
}
function inRange(run, c) {
  const f = fltOf(run), fr = (c.params || {}).frac;
  if (fr !== undefined) {   // sculpt candidates have no frac
    if (f.min !== '' && fr < parseFloat(f.min) - 1e-9) return false;
    if (f.max !== '' && fr > parseFloat(f.max) + 1e-9) return false;
  }
  if (!polyOn(f)) return true;
  const n = polyOf(c);
  if (!n) return false;     // unmeasured: it cannot be shown to be in range
  if (f.vmin !== '' && n.v < +f.vmin) return false;
  if (f.vmax !== '' && n.v > +f.vmax) return false;
  if (f.fmin !== '' && n.f < +f.fmin) return false;
  if (f.fmax !== '' && n.f > +f.fmax) return false;
  return true;
}
// one labelled min–max pair. class="flt" matters: refresh() refuses to repaint
// while one of these has focus, so a half-typed bound is never yanked away.
function fltPair(run, label, lo, hi, f, step) {
  const box = (which, ph) => `<input class="flt" type="number" step="${step}"
      min="0" placeholder="${ph}" value="${f[which]}"
      onchange="setFilter('${esc(run)}','${which}',this.value)">`;
  return `<span class="muted">${label} ${box(lo, 'min')}–${box(hi, 'max')}</span>`;
}
// span of merged counts across a candidate set, for the filter's own readout
function polySpan(cands) {
  const ns = cands.map(polyOf).filter(Boolean);
  if (!ns.length) return null;
  return {vlo: Math.min(...ns.map(n => n.v)), vhi: Math.max(...ns.map(n => n.v)),
          flo: Math.min(...ns.map(n => n.f)), fhi: Math.max(...ns.map(n => n.f))};
}

// epoch seconds -> local time; adds the date only when it isn't today's
function fmtTime(t) {
  if (!t) return '—';
  const d = new Date(t * 1000);
  const opts = { hour: '2-digit', minute: '2-digit', second: '2-digit' };
  return d.toDateString() === new Date().toDateString()
    ? d.toLocaleTimeString([], opts)
    : d.toLocaleDateString([], { month: 'short', day: 'numeric' }) + ' ' +
      d.toLocaleTimeString([], opts);
}

// Attempt durations: the run stamps wall_s (decimate/nudge + judge wall
// time) on every record it writes; candidates from before that stamp fall
// back to the gap since the previous record — approximate, shown with ~.
function annotateDurations(doc) {
  const cs = doc.candidates || [];
  cs.forEach((c, i) => {
    if (c.wall_s !== undefined) { c._dur = c.wall_s; c._durApprox = false; }
    else if (i > 0 && c.ts && cs[i - 1].ts && c.ts >= cs[i - 1].ts) {
      c._dur = c.ts - cs[i - 1].ts; c._durApprox = true;
    }
  });
}
function fmtDur(s) {
  if (s >= 90)
    return `${Math.floor(s / 60)}m ${String(Math.round(s % 60)).padStart(2, '0')}s`;
  return `${Math.round(s)}s`;
}

// Throughput: attempts = every candidate the run recorded (rejects
// included), solves = the ones that passed every gate and got a fitness.
// Rates are per second of wall clock across the run's records.
function rateSummary(all) {
  const ts = all.filter(c => c.ts).map(c => c.ts);
  const span = ts.length > 1 ? Math.max(...ts) - Math.min(...ts) : 0;
  const solved = all.filter(c => c.fitness !== null &&
                                 c.fitness !== undefined).length;
  const r = n => span > 0 ? (n / span).toPrecision(2) : '—';
  return { att: r(all.length), sol: r(solved) };
}
function drawRate(canvas, all, pred, col, label) {
  const ctx = canvas.getContext('2d');
  const W = canvas.width = canvas.clientWidth * devicePixelRatio;
  const H = canvas.height = canvas.clientHeight * devicePixelRatio;
  ctx.clearRect(0, 0, W, H);
  ctx.font = `${11 * devicePixelRatio}px system-ui`;
  const ts = all.filter(c => c.ts).map(c => c.ts);
  if (ts.length < 2) {
    ctx.fillStyle = '#8b8e99';
    ctx.fillText(label + ' — not enough data yet', 10, H / 2);
    return;
  }
  const t0 = Math.min(...ts), t1 = Math.max(...ts);
  // Rolling window: an eighth of the run so far, at least five minutes —
  // attempts take about a minute each, so anything shorter is all noise.
  const WIN = Math.max(300, (t1 - t0) / 8);
  const sel = all.filter(c => c.ts && pred(c)).map(c => c.ts)
                 .sort((a, b) => a - b);
  const N = 72, pts = [];
  for (let i = 0; i <= N; i++) {
    const t = t0 + (t1 - t0) * i / N;
    // Near the start the window sticks out before the run began; divide by
    // the overlap so early rates are not artificially deflated.
    const win = Math.min(WIN, t - t0 + 60);
    const n = sel.filter(v => v > t - win && v <= t).length;
    pts.push([t, n / win]);
  }
  const ymax = Math.max(...pts.map(p => p[1]), 1e-6);
  const px = t => 8 + (t - t0) / (t1 - t0 || 1) * (W - 16);
  const py = v => 24 * devicePixelRatio + (ymax - v) / ymax *
                  (H - 24 * devicePixelRatio - 22 * devicePixelRatio);
  ctx.strokeStyle = '#33363f'; ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(8, py(0)); ctx.lineTo(W - 8, py(0)); ctx.stroke();
  ctx.strokeStyle = col; ctx.lineWidth = devicePixelRatio;
  ctx.beginPath();
  pts.forEach(([t, v], i) =>
    i ? ctx.lineTo(px(t), py(v)) : ctx.moveTo(px(t), py(v)));
  ctx.stroke();
  const last = pts[pts.length - 1];
  ctx.fillStyle = col;
  ctx.beginPath();
  ctx.arc(px(last[0]), py(last[1]), 2.5 * devicePixelRatio, 0, 7);
  ctx.fill();
  ctx.fillStyle = '#8b8e99';
  ctx.fillText(`${label} — now ${last[1].toPrecision(2)}/s · peak ` +
               `${ymax.toPrecision(2)}/s`, 10, 14 * devicePixelRatio);
  ctx.fillText('run time →', 10, H - 6);
}

function tiles(run, tag, n, prefix) {
  let h = '<div class="tiles">';
  for (let y = 0; y < n; y++)
    h += `<img loading="lazy" src="/img/${run}/work/${prefix || tag + '_bind'}_y${y}.png"
           onerror="this.remove()">`;
  return h + '</div>';
}
function scoreline(c) {
  if (c.fitness === null || c.fitness === undefined)
    return `<span class="muted">${esc(c.status)}</span>`;
  const reg = c.region_min !== undefined ? ` · region min ${c.region_min}` : '';
  return `fitness <b style="color:#e6b450">${c.fitness}</b>
       <span class="muted">margin ${c.style_margin} · identity ${c.identity} ·
       pose ${c.pose_id}${reg}</span>`;
}
function paramline(c, run) {
  const p = c.params || {};
  const when = c.ts ? ` · ${fmtTime(c.ts)}` : '';
  const took = c._dur !== undefined
    ? ` · took ${c._durApprox ? '~' : ''}${fmtDur(c._dur)}` : '';
  let s = p.regime === 'sculpt' ? esc((p.moves || []).join('; '))
                                : `fraction ${p.frac} · seed ${p.seed}`;
  let parts = p.parts, orig = false;
  if (!parts && p.regime === 'sculpt' && run && DATA[run]) {
    // Sculpt only moves vertices, so its counts are the base models' —
    // recoverable from any reduce candidate's original-model stats.
    const donor = (DATA[run].candidates || []).find(x => (x.params || {}).parts);
    if (donor) { parts = donor.params.parts; orig = true; }
  }
  if (parts) {
    const faces = parts.reduce((a, x) => a + ((orig ? x.orig_faces : x.faces) || 0), 0);
    const verts = parts.reduce((a, x) => a + ((orig ? x.orig_vertices : x.vertices) || 0), 0);
    s += ` · ${faces.toLocaleString()} faces · ${verts.toLocaleString()} vertices`;
  }
  return s + took + when;
}
// chart legends: swatch shape ('sw' dot, 'swline'/'swdash' line, 'swband'
// area, 'swtick' reject marker), colour, label
function legend(items) {
  return `<div class="legend">` + items.map(([shape, color, label]) =>
    `<span><i class="${shape}" style="${
      shape === 'swline' || shape === 'swdash' ? 'border-color' : 'background'
    }:${color}"></i>${label}</span>`).join('') + `</div>`;
}
const authBtn = (run, tag, label) =>
  `<button class="authbtn" title="walk this candidate through the authoring flow"
     onclick="event.stopPropagation();authorFrom('${esc(run)}','${esc(tag)}')"
     >⎘ ${label || 'author'}</button>`;
function card(run, c, best) {
  const rej = c.fitness === null || c.fitness === undefined;
  const cls = 'card' + (c.id === best ? ' best' : '') + (rej ? ' rej' : '');
  const fav = isFav(run, c.id);
  return `<div class="${cls}" onclick="openModal('${run}','${esc(c.id)}')">
    <span class="star${fav ? ' fav' : ''}" title="save to favorites"
      onclick="event.stopPropagation();toggleFav('${run}','${esc(c.id)}')">${fav ? '★' : '☆'}</span>
    <span class="cardops">${authBtn(run, c.id)}</span>
    <span class="tag">${esc(c.id)}</span>${scoreline(c)}
    <div class="muted" style="font-size:.8em">${paramline(c, run)}</div>
    ${tiles(run, c.id, 4)}</div>`;
}

function drawChart(canvas, doc, cands) {
  const ctx = canvas.getContext('2d');
  const W = canvas.width = canvas.clientWidth * devicePixelRatio;
  const H = canvas.height = canvas.clientHeight * devicePixelRatio;
  ctx.clearRect(0, 0, W, H);
  const sculpt = cands.some(c => (c.params||{}).regime === 'sculpt');
  const xs = c => sculpt ? cands.indexOf(c)
                         : ((c.params||{}).frac !== undefined ? c.params.frac : 0);
  const pass = cands.filter(c => c.fitness !== null && c.fitness !== undefined);
  const xv = cands.map(xs), fv = pass.map(c => c.fitness).concat([0]);
  const xmin = Math.min(...xv, sculpt ? 0 : 0.3), xmax = Math.max(...xv, sculpt ? 10 : 0.9);
  const fmin = Math.min(...fv), fmax = Math.max(...fv);
  const px = x => 30 + (x - xmin) / (xmax - xmin || 1) * (W - 50);
  const py = f => 12 + (fmax - f) / (fmax - fmin || 1) * (H - 46);
  ctx.strokeStyle = '#33363f'; ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(30, py(0)); ctx.lineTo(W - 20, py(0)); ctx.stroke();
  ctx.fillStyle = '#8b8e99';
  ctx.font = `${11 * devicePixelRatio}px system-ui`;
  ctx.fillText('baseline', 34, py(0) - 4 * devicePixelRatio);
  ctx.fillText(sculpt ? 'iteration →' : 'kept vertex fraction →', 34, H - 6);
  for (const c of cands) {
    const rej = c.fitness === null || c.fitness === undefined;
    const x = px(xs(c));
    if (rej) {
      ctx.fillStyle = '#6a4a4a';
      ctx.fillRect(x - 2, H - 30, 4, 8);
    } else {
      const bestId = doc.best && doc.best.id;
      ctx.fillStyle = c.id === bestId ? '#7ec97e' : '#e6b450';
      ctx.beginPath(); ctx.arc(x, py(c.fitness), 4 * devicePixelRatio / 2 + 2, 0, 7); ctx.fill();
    }
  }
}

// Content preserver analysis: the search scores every candidate with the
// trained Siamese preserver (identity = bind pose, pose_id = animation
// sweep, region_min = worst close-up). Calibration from the held-out
// split: >=85 reads identity-safe, <=60 reads broken; between, trust the
// relative ranking. This panel plots all three per candidate against
// those bands and tallies which preserver gate killed each reject.
const PRES = { safe: 85, broken: 60, lo: 40 };
function preserverSummary(cands) {
  const pass = cands.filter(c => c.fitness !== null && c.fitness !== undefined);
  const kills = { identity: 0, pose_id: 0, region: 0 };
  for (const c of cands) {
    const s = c.status || '';
    if (s.startsWith('rejected:identity')) kills.identity++;
    else if (s.startsWith('rejected:pose_id')) kills.pose_id++;
    else if (s.startsWith('rejected:region')) kills.region++;
  }
  const vals = k => pass.map(c => c[k]).filter(v => v !== undefined);
  const stat = k => {
    const v = vals(k);
    if (!v.length) return '—';
    const mean = v.reduce((a, b) => a + b, 0) / v.length;
    return `${mean.toFixed(1)} <span class="muted">(min ${Math.min(...v).toFixed(1)})</span>`;
  };
  return `<div class="strip">
    <span>bind identity <b>${stat('identity')}</b></span>
    <span>pose identity <b>${stat('pose_id')}</b></span>
    <span>region min <b>${stat('region_min')}</b></span>
    <span>gate kills <b>${kills.identity}</b> id ·
      <b>${kills.pose_id}</b> pose · <b>${kills.region}</b> region</span>
    <span class="muted">calibration: &ge;85 safe · &le;60 broken ·
      ranking reliable in between</span>
  </div>`;
}
function drawPreserver(canvas, cands) {
  const ctx = canvas.getContext('2d');
  const W = canvas.width = canvas.clientWidth * devicePixelRatio;
  const H = canvas.height = canvas.clientHeight * devicePixelRatio;
  ctx.clearRect(0, 0, W, H);
  const py = v => 10 + (100 - Math.max(PRES.lo, v)) / (100 - PRES.lo) * (H - 40);
  const px = i => 30 + (cands.length > 1 ? i / (cands.length - 1) : 0) * (W - 55);
  // calibration bands
  ctx.fillStyle = 'rgba(126,201,126,.10)';
  ctx.fillRect(30, py(100), W - 50, py(PRES.safe) - py(100));
  ctx.fillStyle = 'rgba(201,106,106,.12)';
  ctx.fillRect(30, py(PRES.broken), W - 50, py(PRES.lo) - py(PRES.broken));
  ctx.font = `${11 * devicePixelRatio}px system-ui`;
  for (const [v, lbl] of [[PRES.safe, 'safe ≥85'], [PRES.broken, 'broken ≤60']]) {
    ctx.strokeStyle = '#33363f';
    ctx.beginPath(); ctx.moveTo(30, py(v)); ctx.lineTo(W - 25, py(v)); ctx.stroke();
    ctx.fillStyle = '#8b8e99';
    ctx.fillText(lbl, 34, py(v) - 3 * devicePixelRatio);
  }
  const series = [['identity', '#e6b450'], ['pose_id', '#7ec9c9'], ['region_min', '#c97ea8']];
  for (const [key, col] of series) {
    ctx.strokeStyle = col; ctx.fillStyle = col;
    ctx.lineWidth = devicePixelRatio;
    let started = false;
    ctx.beginPath();
    cands.forEach((c, i) => {
      if (c[key] === undefined) return;
      const x = px(i), y = py(c[key]);
      started ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      started = true;
    });
    ctx.stroke();
    cands.forEach((c, i) => {
      if (c[key] === undefined) return;
      ctx.beginPath(); ctx.arc(px(i), py(c[key]), 2 * devicePixelRatio, 0, 7); ctx.fill();
    });
  }
  ctx.fillStyle = '#8b8e99';
  ctx.fillText('candidates →', 34, H - 6);
}

// OSRS matcher (style judge): the ResNet18 osrs-vs-highpoly classifier that
// drives the search. style_margin = logit(osrs) - logit(highpoly); positive
// reads as OSRS. Fitness chases gain = margin - baseline margin, so the
// baseline line is the bar every candidate has to clear.
function matcherSummary(doc, cands) {
  const base = doc.baseline && doc.baseline.style_margin;
  const scored = cands.filter(c => c.style_margin !== undefined && c.style_margin !== null);
  const pass = scored.filter(c => c.fitness !== null && c.fitness !== undefined);
  if (!scored.length)
    return `<div class="strip"><span class="muted">no scored candidates yet</span></div>`;
  const ms = scored.map(c => c.style_margin);
  const bestM = Math.max(...ms);
  const mean = a => a.reduce((x, y) => x + y, 0) / a.length;
  const gain = base !== undefined ? (bestM - base) : null;
  const above = base !== undefined ? ms.filter(m => m > base).length : 0;
  return `<div class="strip">
    <span>baseline margin <b>${base !== undefined ? base.toFixed(3) : '—'}</b></span>
    <span>best margin <b>${bestM.toFixed(3)}</b>
      ${gain !== null ? `<span class="muted">(gain ${gain >= 0 ? '+' : ''}${gain.toFixed(3)})</span>` : ''}</span>
    <span>mean margin <b>${mean(ms).toFixed(3)}</b>
      <span class="muted">(passers ${pass.length ? mean(pass.map(c => c.style_margin)).toFixed(3) : '—'})</span></span>
    <span>beat baseline <b>${above}</b> / ${scored.length}</span>
    <span class="muted">margin &gt; 0 reads as OSRS; gain &gt; 0 beats the input</span>
  </div>`;
}
function drawMatcher(canvas, doc, cands) {
  const ctx = canvas.getContext('2d');
  const W = canvas.width = canvas.clientWidth * devicePixelRatio;
  const H = canvas.height = canvas.clientHeight * devicePixelRatio;
  ctx.clearRect(0, 0, W, H);
  const base = doc.baseline && doc.baseline.style_margin;
  const pts = [];
  cands.forEach((c, i) => {
    if (c.style_margin !== undefined && c.style_margin !== null)
      pts.push([i, c.style_margin, c.fitness !== null && c.fitness !== undefined]);
  });
  if (!pts.length) return;
  let lo = Math.min(...pts.map(p => p[1])), hi = Math.max(...pts.map(p => p[1]));
  if (base !== undefined) { lo = Math.min(lo, base); hi = Math.max(hi, base); }
  lo = Math.min(lo, 0); hi = Math.max(hi, 0);
  const pad = (hi - lo || 1) * 0.08;
  lo -= pad; hi += pad;
  const py = v => 10 + (hi - v) / (hi - lo) * (H - 40);
  const px = i => 30 + (cands.length > 1 ? i / (cands.length - 1) : 0) * (W - 55);
  ctx.font = `${11 * devicePixelRatio}px system-ui`;
  // zero line: positive side reads as OSRS
  ctx.strokeStyle = '#33363f';
  ctx.beginPath(); ctx.moveTo(30, py(0)); ctx.lineTo(W - 25, py(0)); ctx.stroke();
  ctx.fillStyle = '#8b8e99';
  ctx.fillText('0 (osrs ↑)', 34, py(0) - 3 * devicePixelRatio);
  // baseline margin: the bar to clear
  if (base !== undefined) {
    ctx.strokeStyle = '#c9b47e';
    ctx.setLineDash([6, 4]);
    ctx.beginPath(); ctx.moveTo(30, py(base)); ctx.lineTo(W - 25, py(base)); ctx.stroke();
    ctx.setLineDash([]);
    ctx.fillStyle = '#c9b47e';
    ctx.fillText(`baseline ${base.toFixed(2)}`, W - 130 * devicePixelRatio / 2, py(base) - 3 * devicePixelRatio);
  }
  ctx.strokeStyle = '#7e9cc9';
  ctx.lineWidth = devicePixelRatio;
  ctx.beginPath();
  pts.forEach(([i, m], k) => k ? ctx.lineTo(px(i), py(m)) : ctx.moveTo(px(i), py(m)));
  ctx.stroke();
  for (const [i, m, passed] of pts) {
    ctx.fillStyle = base !== undefined && m > base ? '#7ec97e'
                  : passed ? '#7e9cc9' : '#c96a6a';
    ctx.beginPath(); ctx.arc(px(i), py(m), 2 * devicePixelRatio, 0, 7); ctx.fill();
  }
  ctx.fillStyle = '#8b8e99';
  ctx.fillText('candidates →', 34, H - 6);
}

// "latest run only": collapse the page to the most recently started run.
// Survives reloads via localStorage; the checkbox lives outside #root so the
// 5s repaint never resets it.
const latestBox = document.getElementById('latest-only');
latestBox.checked = localStorage.getItem('osrsifyLatestOnly') === '1';
latestBox.onchange = () => {
  localStorage.setItem('osrsifyLatestOnly', latestBox.checked ? '1' : '0');
  render();
};
function visibleRuns() {
  const entries = Object.entries(DATA);
  if (latestBox.checked) {
    const live = entries.filter(([, doc]) => doc);
    if (!live.length) return entries;
    const t = ([, doc]) => (doc._meta || {}).started || (doc._meta || {}).updated || 0;
    return [live.reduce((a, b) => t(b) > t(a) ? b : a)];
  }
  return entries.filter(([name]) => runShown(name));
}

// run filter chips in the sticky banner; null selection = everything visible.
// New runs appearing later default to shown. Survives reloads.
let RUNSEL = null;
try { RUNSEL = JSON.parse(localStorage.getItem('osrsifyRunSel') || 'null'); }
catch (e) {}
function runShown(name) {
  return RUNSEL === null || RUNSEL[name] !== false;
}
function toggleRun(name) {
  if (RUNSEL === null) RUNSEL = {};
  RUNSEL[name] = !runShown(name);
  localStorage.setItem('osrsifyRunSel', JSON.stringify(RUNSEL));
  render();
}
function selectAllRuns(on) {
  if (on) {
    RUNSEL = null;
    localStorage.removeItem('osrsifyRunSel');
  } else {
    RUNSEL = {};
    for (const n of Object.keys(DATA)) RUNSEL[n] = false;
    localStorage.setItem('osrsifyRunSel', JSON.stringify(RUNSEL));
  }
  render();
}
// Actions the user fired that the poll has not confirmed yet, keyed by run:
// {action: 'stop'|'pause'|'resume', since: ms}. Set on click for instant
// feedback (spinner), cleared only when the server-observed state acks the
// action — or when the click handler learns the search can never ack it
// (pre-heartbeat waves have state 'unknown').
const PENDING = {};
function pendingInfo(run, meta) {
  const p = PENDING[run];
  if (!p) return null;
  const ls = meta.state || {};
  const acked =
    ls.state === 'dead' || ls.state === 'finished' ||
    (p.action === 'stop' && (meta.proc || '').indexOf('exited') === 0) ||
    (p.action === 'pause' && (ls.state === 'pause_requested' || ls.state === 'paused')) ||
    (p.action === 'resume' && ls.state === 'running' && !ls.paused_flag) ||
    // safety valve: never spin forever if the ack cannot be observed
    Date.now() - p.since > (p.action === 'stop' ? 120000 : 30000);
  if (acked) { delete PENDING[run]; return null; }
  return { stop: 'stop pending', pause: 'pause pending', resume: 'resume pending' }[p.action];
}
// The server-side liveness ping (heartbeat + pid probe, refreshed every
// poll) mapped to a [dot class, human label]. Returns null for runs from
// before heartbeats existed, so callers fall back to results.json age.
function liveState(meta) {
  const ls = meta.state || {};
  if (ls.state === 'running')
    return ['live', `alive — heartbeat ${ls.beat_age}s ago (pid ${ls.pid})`];
  if (ls.state === 'pause_requested')
    return ['paused', 'pause requested — the search pauses at the next candidate boundary (searches older than the pause feature ignore it)'];
  if (ls.state === 'paused')
    return ['paused', `paused — process alive (pid ${ls.pid}), budget clock stopped, waiting on resume`];
  if (ls.state === 'dead')
    return ['off', `search process is gone — last heartbeat ${ls.beat_age === undefined ? 'unknown' : ls.beat_age + 's ago'}`];
  if (ls.state === 'finished')
    return ['idle', 'finished — the search completed its budget'];
  return null;
}
function renderSidebar() {
  // newest runs first, so the list reads like the run history
  const started = n => {
    const m = (DATA[n] && DATA[n]._meta) || {};
    return m.started || m.updated || 0;
  };
  const names = Object.keys(DATA).sort((a, b) => started(b) - started(a));
  document.getElementById('runchips').innerHTML = names.map(n => {
    const doc = DATA[n];
    const meta = (doc && doc._meta) || {};
    const cands = ((doc && doc.candidates) || []).length;
    const age = doc ? Date.now() / 1000 - (meta.updated || 0) : Infinity;
    const fallback = !doc ? ['off', 'no results.json yet']
      : doc._starting ? ['stale', 'starting — no results.json yet']
      : !doc.baseline ? ['off', meta.proc || 'no results yet']
      : age < 180 ? ['live', `live — updated ${Math.max(0, Math.round(age))}s ago`]
      : age < 900 ? ['stale', `quiet — last update ${Math.round(age / 60)}m ago`]
      : ['idle', `idle — last update ${fmtTime(meta.updated)}`];
    const pend = pendingInfo(n, meta);
    const st = pend ? ['pending', pend + ' — waiting for the search to acknowledge']
                    : (liveState(meta) || fallback);
    const flagged = (meta.state || {}).paused_flag;
    const tip = st[1] +
      (meta.proc ? ` · ${meta.proc}` : '') +
      (doc && doc.best && doc.best.id ? ` · best ${doc.best.id}` : '') +
      (meta.started ? ` · started ${fmtTime(meta.started)}` : '');
    // while an action is pending the buttons vanish so it cannot double-fire
    const btns = pend ? '' : `
       <button class="pause" title="${flagged
         ? 'resume this search'
         : 'pause this search at the next candidate (budget clock stops)'}"
         onclick="event.stopPropagation();pauseRun('${esc(n)}', ${flagged ? 'false' : 'true'})">${flagged ? '▶' : '⏸'}</button>
       <button class="clone" title="new run prefilled from this run's options"
         onclick="event.stopPropagation();openNewRun('${esc(n)}')">⧉</button>
       <button class="kill" title="kill this run's search process tree"
         onclick="event.stopPropagation();killRun('${esc(n)}')">✕</button>`;
    return `<div class="runitem${runShown(n) && !latestBox.checked ? ' on' : ''}"
       title="${esc(tip)}" onclick="toggleRun('${esc(n)}')">
       ${pend ? '<span class="spinner"></span>' : `<span class="dot ${st[0]}"></span>`}<span class="rname">${esc(n)}</span>
       <span class="cnt">${cands}</span>` + btns + `</div>`;
  }).join('');
}

// A run whose search predates the heartbeat feature can never ack a pause
// or kill through the poll (its state stays 'unknown'), so the fetch
// response is the best confirmation we will ever get for it.
function canAck(run) {
  const st = ((DATA[run] || {})._meta || {}).state || {};
  return st.state && st.state !== 'unknown';
}

async function pauseRun(run, on) {
  PENDING[run] = { action: on ? 'pause' : 'resume', since: Date.now() };
  render();
  try {
    const res = await fetch('/api/' + (on ? 'pause' : 'resume') + '/' +
                            encodeURIComponent(run), { method: 'POST' });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    await res.json();
    if (!canAck(run)) delete PENDING[run];
  } catch (e) {
    delete PENDING[run];
    alert((on ? 'pause' : 'resume') + ' failed: ' + e);
  }
  refresh();
}

async function killRun(run) {
  if (!confirm(`Kill the search feeding "${run}"?\\nThis stops its whole process tree.`))
    return;
  PENDING[run] = { action: 'stop', since: Date.now() };
  render();
  try {
    const res = await fetch('/api/kill/' + encodeURIComponent(run), { method: 'POST' });
    const body = await res.json();
    if (!body.killed || !body.killed.length) {
      delete PENDING[run];
      alert(body.message || JSON.stringify(body));
    } else if (!canAck(run)) {
      // taskkill already returned; nothing will ever confirm it via heartbeat
      delete PENDING[run];
    }
  } catch (e) {
    delete PENDING[run];
    alert('kill failed: ' + e);
  }
  refresh();
}

// ---- archive / delete: server-side jobs, watched from the corner ----------
// A finished wave is routinely 60k files and 12 GB, so the POST only starts
// the work. While anything is live the page polls /api/jobs every 700ms for a
// progress bar; when the last job lands it refreshes once, so a deleted run
// leaves the sidebar and a fresh archive gets stamped into its strip.
let JOBS = [];
let jobTimer = null, jobsWereLive = false;

function fmtBytes(n) {
  n = Number(n) || 0;
  const u = ['B', 'KB', 'MB', 'GB', 'TB'];
  let i = 0;
  while (n >= 1024 && i < u.length - 1) { n /= 1024; i++; }
  return (i && n < 100 ? n.toFixed(1) : Math.round(n)) + ' ' + u[i];
}
const pctOf = (a, b) => b ? Math.round(100 * a / b) : 0;
const jobOf = run => JOBS.find(j => j.run === run && j.state === 'working');
const wasCancelled = j => /cancelled/.test(j.error || '');
const num = n => Number(n || 0).toLocaleString();

async function pollJobs() {
  try { JOBS = (await (await fetch('/api/jobs')).json()).jobs || []; }
  catch (e) { }
  renderJobs();
  const live = JOBS.some(j => j.state === 'working');
  clearTimeout(jobTimer);
  if (live) jobTimer = setTimeout(pollJobs, 700);
  else if (jobsWereLive) refresh();
  jobsWereLive = live;
}

function jobSub(j) {
  const secs = Math.max(1, (j.ended || Date.now() / 1000) - j.started);
  if (j.state === 'error')
    return wasCancelled(j)
      ? 'cancelled — the half-written zip was thrown away'
      : esc(j.error || 'failed');
  if (j.phase === 'measuring') return 'counting files…';
  if (j.kind === 'archive') {
    if (j.state === 'done')
      return `<b>${fmtBytes(j.bytes)} → ${fmtBytes(j.out_bytes)}</b>
              (${pctOf(j.out_bytes, j.bytes)}% of original) in ${fmtDur(secs)}`;
    const rate = j.bytes_done / secs;
    const left = rate > 0 ? '~' + fmtDur((j.bytes - j.bytes_done) / rate) : '—';
    return `${num(j.files_done)} / ${num(j.files)} files ·
            <b>${fmtBytes(j.bytes_done)}</b> of ${fmtBytes(j.bytes)} ·
            ${fmtBytes(rate)}/s · ${left} left`;
  }
  if (j.state === 'done')
    return `<b>${num(j.files)} files</b> (${fmtBytes(j.bytes)}) sent to the
            ${esc(j.bin || 'Recycle Bin')}`;
  return `${num(j.files_done)} / ${num(j.files)} files ·
          <b>${fmtBytes(j.bytes_done)}</b> of ${fmtBytes(j.bytes)}`;
}

function jobCard(j) {
  const pct = j.state !== 'working' ? 100
            : j.bytes ? 100 * j.bytes_done / j.bytes : 0;
  const x = j.state === 'working'
    ? (j.kind === 'archive'
        ? `<span class="x" title="stop compressing and discard the part file"
             onclick="jobAct('${esc(j.id)}','cancel')">✕</span>` : '')
    : `<span class="x" title="dismiss"
         onclick="jobAct('${esc(j.id)}','dismiss')">✕</span>`;
  return `<div class="job ${esc(j.kind)} ${j.state === 'working' ? '' : esc(j.state)}">
    <div class="jt"><span class="k">${esc(j.kind)}</span>
      <span class="n" title="${esc(j.run)}">${esc(j.run)}</span>${x}</div>
    <div class="bar"><i style="width:${pct.toFixed(1)}%"></i></div>
    <div class="sub">${jobSub(j)}</div>` +
    (j.kind === 'archive' && j.state === 'done' && j.dest
      ? `<div class="sub" style="margin-top:.15em">${esc(j.dest)}</div>` : '') +
    `</div>`;
}
function renderJobs() {
  document.getElementById('jobs').innerHTML = JOBS.map(jobCard).join('');
}
async function jobAct(id, what) {
  try {
    const r = await fetch(`/api/job/${encodeURIComponent(id)}/${what}`,
                          { method: 'POST' });
    if (!r.ok) alert(await r.text());
  } catch (e) { alert(what + ' failed: ' + e); }
  pollJobs();
}

async function archiveRun(run) {
  const j = jobOf(run);
  if (j) return alert(`already ${j.kind === 'archive' ? 'archiving' : 'deleting'} this run`);
  try {
    const r = await fetch('/api/archive/' + encodeURIComponent(run),
                          { method: 'POST' });
    if (!r.ok) return alert(await r.text());
  } catch (e) { return alert('archive failed: ' + e); }
  pollJobs();
}

// Delete asks first, and the dialog is built from a live measurement of the
// run: what it costs, where it goes, whether an archive of it exists.
let CONFIRM = null;
function closeConfirm() {
  document.getElementById('confirm').style.display = 'none';
  CONFIRM = null;
}
async function askDelete(run) {
  const body = document.getElementById('confirm-body');
  const head = `<h2>Delete ${esc(run)}?</h2>`;
  CONFIRM = run;
  body.innerHTML = head + `<p>measuring the run…</p>`;
  document.getElementById('confirm').style.display = 'block';
  let info;
  try {
    info = await (await fetch('/api/runsize/' + encodeURIComponent(run))).json();
  } catch (e) {
    body.innerHTML = head + `<p>could not measure the run: ${esc(e)}</p>
      <div class="btns"><button class="no" onclick="closeConfirm()">close</button></div>`;
    return;
  }
  if (CONFIRM !== run) return;  // dialog was closed or moved on while measuring
  const st = (info.state || {}).state;
  const live = ['running', 'paused', 'pause_requested'].indexOf(st) >= 0;
  const arch = info.archive, bin = esc(info.bin || 'Recycle Bin');
  body.innerHTML = head + `
    <p>This sends <b>${num(info.files)} files</b> (<b>${fmtBytes(info.bytes)}</b>)
       to the ${bin} and drops the run from the dashboard.</p>
    <div class="paths">${info.paths.map(esc).join('\\n')}</div>
    <p class="muted">Nothing here can undo it: restoring means opening the
      ${bin} yourself. If the run is bigger than its quota the shell deletes
      the run outright instead, and then nothing can.</p>
    ${arch
      ? `<p class="muted">Archived ${fmtTime(arch.ts)} — ${esc(arch.path)}
           (${fmtBytes(arch.bytes)}).</p>`
      : `<p class="muted">There is <b>no archive</b> of this run.</p>`}
    ${live ? `<p style="color:#c96a6a">The search is still ${esc(st)} — stop it
        first; Windows will not delete files it still holds open.</p>` : ''}
    <label class="ack"><input type="checkbox" id="cf-ack" ${live ? 'disabled' : ''}
        onchange="document.getElementById('cf-go').disabled = !this.checked">
      <span>I understand ${esc(run)} leaves this machine's working set.</span></label>
    <div class="btns">
      <button class="go" id="cf-go" disabled onclick="doDelete('${esc(run)}')">
        delete ${fmtBytes(info.bytes)}</button>
      ${arch ? '' : `<button class="no"
        onclick="closeConfirm();archiveRun('${esc(run)}')">archive it first</button>`}
      <button class="no" onclick="closeConfirm()">cancel</button>
    </div>`;
}
async function doDelete(run) {
  closeConfirm();
  try {
    const r = await fetch('/api/delete/' + encodeURIComponent(run),
                          { method: 'POST' });
    if (!r.ok) return alert(await r.text());
  } catch (e) { return alert('delete failed: ' + e); }
  pollJobs();
}

// The archive/delete pair for one run, shown in its summary strip. A live
// search blocks both: its files are open, and half a wave is not a backup.
function runOps(run, meta) {
  const j = jobOf(run);
  if (j)
    return `<span><button class="pausebtn" disabled><span class="spinner"></span>
      ${j.kind === 'archive' ? 'archiving' : 'deleting'}…</button></span>`;
  const ls = (meta && meta.state) || {};
  const live = ['running', 'paused', 'pause_requested'].indexOf(ls.state) >= 0;
  const stop = 'stop the search first';
  const arch = meta && meta.archive;
  return `<span><button class="pausebtn" ${live ? 'disabled' : ''}
      title="${live ? stop : 'zip the whole run into runs/archive/' + esc(run) + '.zip at deflate 9'}"
      onclick="archiveRun('${esc(run)}')">⤓ archive</button></span>
    <span><button class="dangerbtn" ${live ? 'disabled' : ''}
      title="${live ? stop : 'send the run to the Recycle Bin'}"
      onclick="askDelete('${esc(run)}')">🗑 delete</button></span>` +
    (arch ? `<span class="muted" title="${esc(arch.path)}">archived
       ${fmtTime(arch.ts)} · ${fmtBytes(arch.bytes)}</span>` : '');
}

// ---- new-run form: every osrsify.py knob, grouped; blank = tool default ----
function optField(o) {
  const id = 'nr-' + o.flag;
  let input;
  if (o.kind === 'flag')
    input = `<span class="chk"><input type="checkbox" name="${o.flag}" id="${id}"> on</span>`;
  else if (o.kind === 'choice')
    input = `<select name="${o.flag}" id="${id}">` +
      o.choices.map(c => `<option${c === o.default ? ' selected' : ''}>${c}</option>`).join('') +
      `</select>`;
  else if (o.kind === 'list')
    input = `<textarea name="${o.flag}" id="${id}" spellcheck="false"
              placeholder="one per line"></textarea>`;
  else
    input = `<input type="${o.kind === 'str' ? 'text' : 'number'}"
              ${o.kind === 'float' ? 'step="any"' : o.kind === 'int' ? 'step="1"' : ''}
              name="${o.flag}" id="${id}" value="${esc(o.default)}"
              placeholder="default" spellcheck="false">`;
  return `<div class="opt${o.kind === 'list' ? ' wide' : ''}"
      title="${esc(o.help || '')}">
      <label for="${id}">--${o.flag}</label>${input}</div>`;
}
function openNewRun(fromRun) {
  document.getElementById('nr-form').innerHTML = GROUPS.map(([key, label]) =>
    `<fieldset><legend>${esc(label)}</legend><div class="optgrid">` +
    OPTS.filter(o => o.group === key).map(optField).join('') +
    `</div></fieldset>`).join('');
  const sel = document.getElementById('nr-prefill');
  sel.innerHTML = '<option value="">— run —</option>' +
    Object.keys(DATA).map(n => `<option>${esc(n)}</option>`).join('');
  sel.onchange = () => prefillFrom(sel.value);
  if (fromRun && DATA[fromRun]) { sel.value = fromRun; prefillFrom(fromRun); }
  document.getElementById('nr-status').textContent = '';
  document.getElementById('newrun').style.display = 'block';
  if (!WZ) WZ = { i: -1, ans: {}, presets: null, recents: [] };
  // a clone means "this run's exact options", so it lands in the flat form
  setNrView(fromRun ? 'all' : 'guided');
  wzLoad();
}
function closeNewRun() { document.getElementById('newrun').style.display = 'none'; }
function setNrView(v) {
  NRVIEW = v;
  const on = k => document.getElementById(k).classList;
  document.getElementById('nr-guided').style.display = v === 'guided' ? '' : 'none';
  document.getElementById('nr-all').style.display = v === 'all' ? '' : 'none';
  on('nr-tab-guided').toggle('on', v === 'guided');
  on('nr-tab-all').toggle('on', v === 'all');
  on('nr-inner').toggle('wz', v === 'guided');
  if (v === 'guided') wzRender();
}

// ---- guided flow: one decision per screen, gated by the decision tree ----
// WZ.ans holds every answer, including the "_"-prefixed wizard-local ones that
// only exist to gate later questions. Nothing is committed until launch.
let WZ = null;
let NRVIEW = 'guided';
// a `when` is either one [name, op, values] test or a list of them; as a list
// it opens if ANY of them passes, which is how a screen serves two branches
const wzConds = w => (!w ? [] : Array.isArray(w[0]) ? w : [w]);
// names any `when` clause tests — the only ones whose edit has to repaint the
// step, so typing in a plain field never steals its own focus
const WZGATES = new Set();
for (const s of WIZARD) {
  for (const c of wzConds(s.when)) WZGATES.add(c[0]);
  for (const f of (s.fields || []))
    for (const c of wzConds(f.when)) WZGATES.add(c[0]);
}

const wzName = f => f.opt || f.name;
function wzDefault(f) {
  if (f.default !== undefined) return f.default;
  const spec = OPT_BY[wzName(f)];
  if (spec) {
    if (spec.kind === 'flag') return false;
    if (f.cards && spec.default === '') return f.cards[0].value;
    return spec.default;
  }
  return f.cards ? f.cards[0].value : '';
}
function wzGet(name) {
  if (WZ && WZ.ans[name] !== undefined) return WZ.ans[name];
  for (const s of WIZARD)
    for (const f of (s.fields || []))
      if (wzName(f) === name) return wzDefault(f);
  return OPT_BY[name] ? OPT_BY[name].default : '';
}
const wzVal = f => (WZ && WZ.ans[wzName(f)] !== undefined
  ? WZ.ans[wzName(f)] : wzDefault(f));
function wzWhen(w, asked) {
  if (!w) return true;
  return wzConds(w).some(([name, op, vals]) => {
    // a gate on a question that was never actually put closes, rather than
    // quietly reading that question's default
    if (asked && !asked.has(name)) return false;
    const v = String(wzGet(name)), set = vals.map(String);
    return op === 'notin' ? !set.includes(v) : set.includes(v);
  });
}
// The tree, resolved against the current answers: which steps get asked and
// which of their fields show. One forward pass, so gates can only look back.
function wzSteps() {
  const asked = new Set(), out = [];
  for (const s of WIZARD) {
    if (!wzWhen(s.when, asked)) continue;
    const fields = [];
    for (const f of (s.fields || [])) {
      if (!wzWhen(f.when, asked)) continue;
      fields.push(f);
      asked.add(wzName(f));
    }
    out.push({ s, fields });
  }
  return out;
}
// Only what was actually asked reaches the command line; a hidden field's
// value is left out entirely so osrsify falls back to its own default.
function wzPayload() {
  const out = {};
  for (const { fields } of wzSteps()) {
    for (const f of fields) {
      const n = wzName(f), v = wzVal(f), spec = OPT_BY[n];
      if (f.cards) {
        const card = f.cards.find(c => String(c.value) === String(v));
        if (card && card.sets) Object.assign(out, card.sets);
        if (n[0] !== '_' && String(v) !== '') out[n] = v;
      } else if (spec && spec.kind === 'flag') {
        if (v === true) out[n] = true;
      } else if (String(v).trim() !== '') {
        out[n] = v;
      }
    }
  }
  return out;
}
function wzCmd(p) {
  const parts = ['python -u osrsify.py'];
  for (const o of OPTS) {
    const v = p[o.flag];
    if (v === undefined || v === null || v === false || v === '') continue;
    if (o.kind === 'flag') parts.push('--' + o.flag);
    else if (o.kind === 'list')
      String(v).split('\\n').map(s => s.trim()).filter(Boolean)
        .forEach(s => parts.push('--' + o.flag + ' ' + s));
    else parts.push('--' + o.flag + ' ' + v);
  }
  return parts.join(' ');
}
function wzSet(name, el) {
  WZ.ans[name] = el.type === 'checkbox' ? el.checked : el.value;
  if (WZGATES.has(name)) wzRender();
}
function wzPick(name, value) { WZ.ans[name] = value; wzRender(); }
function wzGo(i) {
  WZ.i = i;
  wzRender();
  document.getElementById('newrun').scrollTop = 0;  // the modal scrolls, not the page
}
async function wzLoad() {
  if (!WZ.presets)
    try { WZ.presets = await (await fetch('/api/presets')).json(); }
    catch (e) { WZ.presets = {}; }
  try { WZ.recents = await (await fetch('/api/recents')).json(); }
  catch (e) { WZ.recents = []; }
  if (NRVIEW === 'guided') wzRender();
}
// Which flags each tuned section hides, read straight off the tree so adding a
// knob never needs a second list kept in sync.
const WZTUNED = (() => {
  const m = {};
  for (const s of WIZARD) {
    const w = wzConds(s.when)[0] || [];
    if (!String(w[0] || '').startsWith('_tune_')) continue;
    (m[w[0]] = m[w[0]] || []).push(...(s.fields || []).map(wzName));
  }
  return m;
})();
// rebuild the wizard answers from a flat option set — used to reopen a recent
// launch, and to recover the local decisions the flags imply
function wzInferAns(form) {
  if (form && form._wizard) return Object.assign({}, form._wizard);
  const a = {};
  for (const [k, v] of Object.entries(form || {})) if (k[0] !== '_') a[k] = v;
  a._source = form && form.preset ? 'preset' : 'manual';
  a._authsrc = form && form['author-model'] ? 'files' : 'dir';
  a._authwrite = form && form['author-suffix'] ? 'beside' : 'over';
  a._authverify = form && form['no-author-verify'] ? 'skip' : 'verify';
  a._authdry = form && form['dry-run'] ? 'dry' : 'real';
  a._backport = form && form.backport ? 'yes' : 'no';
  a._render = form && form.zbuffer ? 'zbuffer'
    : form && form['force-priorities'] === 'strip' ? 'flat' : 'painter';
  a._budget = form && (form['max-verts'] || form['max-faces']) ? 'limit' : 'none';
  // a launch that carries any of a section's knobs was tuned by hand, so
  // reopen that section open — otherwise its screens stay closed, and closed
  // screens are dropped from the payload, silently losing what was set
  for (const [gate, names] of Object.entries(WZTUNED))
    a[gate] = names.some(n => form && form[n] !== undefined && form[n] !== '')
      ? 'manual' : 'auto';
  return a;
}
// ---- authoring a candidate you are looking at -----------------------------
// The run that produced a candidate already knows everything the author flow
// needs — which parts, in what order, where they were read from and what ids
// they answer to — so the flow should be confirming those, not asking for
// them cold.
// configs carry both separators — presets are written repo-root relative with
// forward slashes, anything osrsify resolved itself comes back as a Windows
// path — so every path split here has to accept either
const dirOf = p => String(p).replace(/[\\\\/][^\\\\/]*$/, '');
function authorAns(run, tag) {
  const cfg = (DATA[run] || {}).config || {};
  const pre = ((WZ && WZ.presets) || {})[cfg.preset];
  // Where the parts came from, in falling order of trust: the explicit --model
  // list, then the preset that stood in for it (most runs pass only --preset,
  // so config.model is empty on them). config.base_models is the last resort
  // and usually the wrong answer — the bake, defight and priorities pre-passes
  // REWRITE it to their own copies inside the run directory, which is a
  // workspace, not a home to write back to.
  const models = (cfg.model && cfg.model.length ? cfg.model
                  : pre && (pre.models || []).length ? pre.models
                  : cfg.base_models) || [];
  const a = { mode: 'author', _authsrc: 'dir',
              'author-from': 'runs/' + run + '/' +
                (tag === 'best' ? 'best' : 'cand/' + tag),
              _authwrite: 'beside', 'author-suffix': '_lowpoly',
              _authverify: 'verify', _authdry: 'dry' };
  // --author-from pulls each part by filename, so it needs the same preset or
  // model list the search ran with — otherwise it cannot know the order
  if (cfg.preset) { a._source = 'preset'; a.preset = cfg.preset; }
  else {
    a._source = 'manual';
    a.model = models.join('\\n');
    a.seq = (cfg.seq || cfg.seqs || []).join('\\n');
    if (cfg.seqcfg) a.seqcfg = cfg.seqcfg;
    if (cfg.cache) a.cache = cfg.cache;
  }
  // a destination inside the run tree is a pre-pass artifact, not a home: leave
  // it blank and let the flow ask rather than proposing to write into runs/
  const dir = models.length ? dirOf(models[0]) : '';
  if (dir && !/[\\\\/]runs[\\\\/]/.test(dir)) a['author-out'] = dir;
  // the lane root is the parent of models/, and every model in a lane is
  // registered from that lane's own pack/
  const lane = dir.replace(/[\\\\/]models[\\\\/][\\s\\S]*$/, '');
  const pack = cfg.pack || (lane && lane !== dir
    ? lane + '/pack/7_models.pack' : '');
  if (pack) a.pack = pack;
  const ids = models.map(m => (m.match(/(\\d+)\\.ob3$/i) || [])[1]);
  if (ids.length && ids.every(Boolean)) a['pack-id'] = ids.join('\\n');
  return a;
}
async function authorFrom(run, tag) {
  closeModal();
  openNewRun();
  // the preset is where a --preset run's original paths live, so the prefill
  // has to wait for presets.json rather than guess without it
  await wzLoad();
  WZ.ans = authorAns(run, tag);
  WZ.i = 0;
  wzRender();
}
function wzReuse(i) {
  const r = (WZ.recents || [])[i];
  if (!r) return;
  WZ.ans = wzInferAns(r.form || {});
  delete WZ.ans['out-dir'];   // a new search must not write into the old run
  wzGo(0);
}

function wzPresetInput(f) {
  const v = String(wzVal(f)), ps = WZ.presets || {};
  const names = Object.keys(ps);
  if (!names.length)
    return `<div class="wzhelp">no presets.json found — go back and pick
            "paths I type myself".</div>`;
  const p = ps[v];
  const base = s => String(s).split('/').pop().split('\\\\').pop();
  return `<select id="wz-preset" onchange="wzSet('preset', this); wzRender()">` +
    ['<option value="">— pick one —</option>'].concat(names.map(n =>
      `<option${n === v ? ' selected' : ''}>${esc(n)}</option>`)).join('') +
    `</select>` + (p ? `<div class="wzhelp">
      <b>${(p.models || []).length}</b> part(s):
      ${esc((p.models || []).map(base).join(', '))}<br>
      <b>${(p.seqs || []).length}</b> sequence(s), cache
      <b>${esc(p.cache || '?')}</b></div>` : '');
}
function wzInput(f) {
  if (f.widget === 'preset') return wzPresetInput(f);
  const n = wzName(f), spec = OPT_BY[n] || {}, v = wzVal(f);
  const id = 'wz-' + n, bind = `onchange="wzSet('${n}', this)"`;
  if (spec.kind === 'flag')
    return `<span class="chk"><input type="checkbox" id="${id}"
            ${v === true ? 'checked' : ''} ${bind}> on</span>`;
  if (spec.kind === 'choice')
    return `<select id="${id}" ${bind}>` + spec.choices.map(c =>
      `<option value="${esc(c)}"${String(c) === String(v) ? ' selected' : ''}>` +
      `${c === '' ? '— tool default —' : esc(c)}</option>`).join('') + `</select>`;
  if (spec.kind === 'list')
    return `<textarea id="${id}" spellcheck="false" placeholder="one per line"
            oninput="wzSet('${n}', this)">${esc(v)}</textarea>`;
  return `<input type="${spec.kind === 'str' ? 'text' : 'number'}"
          ${spec.kind === 'float' ? 'step="any"' : ''} id="${id}"
          value="${esc(v)}" placeholder="tool default" spellcheck="false"
          oninput="wzSet('${n}', this)">`;
}
// `solo` means this field is the only one on its screen and the step title is
// already its question, so the input leads and the flag name drops to the
// footnote it deserves to be — you should not have to read `--prio-zviol-tol`
// to know what is being asked.
function wzFieldView(f, solo) {
  if (f.cards) {
    const v = String(wzVal(f));
    // label/desc are authored in WIZARD alongside the blurbs and carry the
    // same markup — escaping them here printed the tags instead
    return `<div class="wzcards">` + f.cards.map(c =>
      `<div class="wzcard${String(c.value) === v ? ' on' : ''}"
            onclick="wzPick('${wzName(f)}','${esc(c.value)}')">
         <div class="pip"></div>
         <div><div class="lbl">${c.label}</div>
              <div class="desc">${c.desc || ''}</div></div></div>`).join('') +
      `</div>`;
  }
  const n = wzName(f), spec = OPT_BY[n] || {};
  const dflt = spec.default !== undefined && spec.default !== ''
    ? ` <span class="dflt">(default ${esc(spec.default)})</span>`
    : ` <span class="dflt">(no default &mdash; blank leaves the flag off)</span>`;
  const head = solo ? '' : `<div class="wzlbl">${esc(f.label || n)}</div>`;
  return `<div class="wzf">${head}${wzInput(f)}
    <div class="wzhelp"><code>--${esc(n)}</code> &middot;
      ${esc(spec.help || '')}${dflt}</div></div>`;
}
function wzLanding() {
  const rec = WZ.recents || [];
  let h = WZ.launched ? `<div class="wzrec" style="border-color:#3f7a48;
      background:#1c2620"><div class="grow"><div class="nm">launched
      ${esc(WZ.launched.run)}</div><div class="sub">pid ${WZ.launched.pid}
      &middot; logging to ${esc(WZ.launched.log)}</div></div>
      <button onclick="closeNewRun()">watch it</button></div>` : '';
  h += `<div class="wzblurb">One question at a time, in the order the
    pipeline applies them. Every question has a working default, so
    <b>continue</b> is always a safe answer — the last screen shows the exact
    command before anything starts.</div>
    <div class="wznav" style="border:0; padding:0; margin:0 0 1.4rem">
      <button class="go" onclick="wzGo(0)">start a new run →</button></div>
    <h3>recent</h3>`;
  if (!rec.length)
    return h + `<div class="muted" style="font-size:.85em">nothing yet — every
      run launched from here is remembered, answers and all.</div>`;
  return h + rec.slice(0, 12).map((r, i) => `<div class="wzrec">
      <div class="grow"><div class="nm">${esc(r.run)}</div>
        <div class="sub">${esc(r.label || '—')} &middot; ${esc(r.mode || 'search')}
          &middot; ${fmtTime(r.ts)}</div></div>
      <button onclick="wzReuse(${i})"
        title="reopen the flow on this run's exact answers">use these answers</button>
    </div>`).join('');
}
// how many parts this launch is about — the pack ids have to pair with them
function partCount(p) {
  if (p.model) return String(p.model).split('\\n').filter(s => s.trim()).length;
  const pre = ((WZ && WZ.presets) || {})[p.preset];
  return pre ? (pre.models || []).length : 0;
}
const lines = v => String(v || '').split('\\n').map(s => s.trim()).filter(Boolean);
function wzWarnings(p) {
  const w = [];
  if (!p.preset && !(p.model && p.seq && p.seqcfg && p.cache))
    w.push('This needs a preset, or all four of model + seq + seqcfg + cache. ' +
           'Go back to the first question.');
  if (p.mode !== 'author') return w;
  if (!p['author-from'] && !p['author-model'])
    w.push('Nothing to author — go back and point at a candidate directory ' +
           'or name the .ob3 files.');
  if (!p['author-out']) w.push('No destination: --author-out is required.');
  if (!p.pack)
    w.push('No pack file, so nothing would ever load the authored model.');
  const n = partCount(p), ids = lines(p['pack-id']).length;
  if (!ids) w.push('No pack ids — each part needs one.');
  else if (n && ids !== n)
    w.push(`${n} part(s) but ${ids} pack id(s): they pair up in order, and ` +
           'osrsify refuses to guess.');
  return w;
}
// the author flow's payoff screen: what is about to be written, in English
function authorPlan(p) {
  const n = partCount(p), ids = lines(p['pack-id']);
  const suffix = p['author-suffix'] || '';
  const src = p['author-model'] ? lines(p['author-model']).join(', ')
                                : p['author-from'];
  const dry = p['dry-run'];
  return `<div class="wzplan${dry ? ' dry' : ''}">
    <div class="hd">${dry ? 'Dry run — nothing below actually happens yet'
                          : 'This writes into the content tree'}</div>
    <div>Takes <b>${n || '?'}</b> part(s) from <code>${esc(src || '?')}</code>
      and ${suffix
        ? `writes them into <code>${esc(p['author-out'] || '?')}</code> with
           <code>${esc(suffix)}</code> before the extension, leaving the
           originals in place`
        : `writes them over the originals in
           <code>${esc(p['author-out'] || '?')}</code>`}.</div>
    <div>Pack <code>${esc(p.pack || '?')}</code> then points
      id ${ids.map(i => `<b>${esc(i)}</b>`).join(', ') || '?'} at
      ${ids.length > 1 ? 'them' : 'it'}${p['no-author-verify']
        ? '' : ', and each one is re-rendered and re-posed to prove it loads'}.</div>
    <div class="muted">Afterwards: repoint the NPC record's
      <code>model&lt;N&gt;=</code> if it does not already use
      ${ids.length > 1 ? 'those ids' : 'that id'}, then re-pack the cache with
      <code>make -C src torirsserver-cache-rs2012</code>.</div></div>`;
}
function wzReview(steps) {
  const p = wzPayload(), keys = Object.keys(p);
  const warn = wzWarnings(p).map(t =>
    `<div style="color:#e6b450; font-size:.85em; margin-bottom:.7rem">${t}</div>`
  ).join('');
  const author = p.mode === 'author';
  return `<div class="wzbar"><i style="width:100%"></i></div>
    <div class="muted" style="font-size:.78em">last step</div>
    <div class="wzq">${author ? 'Ready to author' : 'Ready to launch'}</div>
    <div class="wzblurb">These are the only flags that get passed. Every
      question you left alone is absent on purpose, so osrsify keeps its own
      default for it.</div>${warn}${author ? authorPlan(p) : ''}
    <table class="wzsum">` + keys.map(k =>
      `<tr><td class="k">--${esc(k)}</td><td class="v">${p[k] === true
        ? '<span class="muted">on</span>'
        : esc(String(p[k])).split('\\n').join('<br>')}</td></tr>`).join('') +
    `</table><div class="wzcmd">${esc(wzCmd(p))}</div>
    <div class="wznav">
      <button class="ghost" onclick="wzGo(${steps.length - 1})">← back</button>
      <button class="go" onclick="wzLaunch()">${author
        ? (p['dry-run'] ? 'show me the plan' : 'author it') : 'launch this run'}</button>
      <span id="wz-status" class="muted" style="font-size:.85em"></span>
    </div>`;
}
function wzRender() {
  const host = document.getElementById('nr-guided');
  if (!host || !WZ) return;
  if (WZ.i < 0) { host.innerHTML = wzLanding(); return; }
  const steps = wzSteps();
  if (WZ.i > steps.length) WZ.i = steps.length;
  if (WZ.i === steps.length) { host.innerHTML = wzReview(steps); return; }
  const { s, fields } = steps[WZ.i];
  const pct = Math.round(100 * WZ.i / steps.length);
  host.innerHTML = `<div class="wzbar"><i style="width:${pct}%"></i></div>
    <div class="muted" style="font-size:.78em">step ${WZ.i + 1} of
      ${steps.length + 1}</div>
    <div class="wzq">${s.title}</div>
    <div class="wzblurb">${s.blurb}</div>` +
    fields.map(f => wzFieldView(f, s.solo)).join('') +
    `<div class="wznav">
      <button class="ghost" onclick="wzGo(${WZ.i - 1})">← back</button>
      <button onclick="wzGo(${WZ.i + 1})">continue →</button>
      <span class="muted" style="font-size:.8em">&nbsp;or
        <a href="#" style="color:#7e9cc9"
           onclick="wzGo(${steps.length}); return false">skip to review</a></span>
    </div>`;
}
async function wzLaunch() {
  const st = document.getElementById('wz-status');
  st.textContent = 'launching…';
  try {
    const body = Object.assign({}, wzPayload(), { _wizard: WZ.ans });
    const res = await fetch('/api/start', { method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body) });
    const text = await res.text();
    if (!res.ok) { st.textContent = text; return; }
    const info = JSON.parse(text);
    WZ.launched = info;
    WZ.i = -1;          // back to the landing, which reports what just started
    await wzLoad();
    refresh();
  } catch (e) { st.textContent = 'launch failed: ' + e; }
}
// copy an existing run's recorded config into matching fields (never its
// out-dir: a new search must not write into the source run's directory)
function prefillFrom(run) {
  const cfg = (DATA[run] || {}).config;
  if (!cfg) return;
  const map = { base_models: 'model', seqs: 'seq' };
  for (const [k, v] of Object.entries(cfg)) {
    if (v === null || (typeof v === 'object' && !Array.isArray(v))) continue;
    const flag = map[k] || k.replace(/_/g, '-');
    if (flag === 'out-dir') continue;
    const el = document.getElementById('nr-' + flag);
    if (!el) continue;
    if (el.type === 'checkbox') el.checked = !!v;
    else if (el.tagName === 'TEXTAREA') el.value = Array.isArray(v) ? v.join('\\n') : v;
    else el.value = Array.isArray(v) ? v.join(',') : v;
  }
}
async function submitNewRun() {
  const opts = {};
  for (const o of OPTS) {
    const el = document.getElementById('nr-' + o.flag);
    if (!el) continue;
    if (o.kind === 'flag') { if (el.checked) opts[o.flag] = true; }
    else if (el.value.trim() !== '') opts[o.flag] = el.value;
  }
  const status = document.getElementById('nr-status');
  status.textContent = 'launching…';
  try {
    const res = await fetch('/api/start', { method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(opts) });
    const text = await res.text();
    if (!res.ok) { status.textContent = text; return; }
    const info = JSON.parse(text);
    status.textContent =
      `launched ${info.run} (pid ${info.pid}) — logging to ${info.log}`;
  } catch (e) { status.textContent = 'launch failed: ' + e; }
}

function render() {
  renderSidebar();
  let h = '';
  if (FAVS.length) {
    h += `<h2>favorites <span class="muted" style="font-size:.75em">
            ~/Documents/osrsify/saves</span></h2>
          <div class="row">` + FAVS.map(favCard).join('') + `</div>`;
  }
  for (const [run, doc] of visibleRuns()) {
    const meta = (doc && doc._meta) || {};
    const ls = meta.state || {};
    const pend = pendingInfo(run, meta);
    const stLabel = { running: 'running', pause_requested: 'pausing…',
                      paused: 'paused', dead: 'dead', finished: 'finished' }[ls.state];
    const stateBadge = pend
      ? `<span class="badge st-pending"
           title="waiting for the search to acknowledge — searches older than the pause feature never will"><span class="spinner"></span>${pend}</span>`
      : stLabel
      ? `<span class="badge st-${ls.state === 'pause_requested' ? 'paused' : ls.state}"
           title="${esc((liveState(meta) || ['', ''])[1])}">${ls.state === 'pause_requested' ? '<span class="spinner"></span>' : ''}${stLabel}</span>`
      : '';
    const pauseBtn = pend || ls.state === 'dead' || ls.state === 'finished' ? '' :
      `<span><button class="pausebtn"
         title="${ls.paused_flag
           ? 'lift the pause flag — the search picks up where it left off'
           : 'pause at the next candidate boundary — the time budget stops burning while paused'}"
         onclick="pauseRun('${esc(run)}', ${ls.paused_flag ? 'false' : 'true'})">${ls.paused_flag ? '▶ resume' : '⏸ pause'}</button></span>`;
    if (!doc || !doc.baseline) {
      h += `<section><div class="runhead"><h2>${esc(run)}${stateBadge}</h2>
            <div class="strip"><span class="muted">${doc && (doc._starting || ls.state === 'running')
              ? 'starting — waiting for the baseline'
              : 'no results.json yet'}</span>${runOps(run, meta)}</div>
            </div></section>`;
      continue;
    }
    annotateDurations(doc);
    const all = doc.candidates || [], best = doc.best && doc.best.id;
    const rates = rateSummary(all);
    const cands = all.filter(c => inRange(run, c));
    const pass = cands.filter(c => c.fitness !== null && c.fitness !== undefined);
    const f = fltOf(run);
    // with a filter up this is the answer to "what are my best options at this
    // size", so it earns more than the usual three slots
    const top = pass.slice().sort((a, b) => b.fitness - a.fitness)
                    .slice(0, fltOn(f) ? 8 : 3);
    const span = polySpan(cands);
    const shown = cands.length !== all.length
      ? ` <span class="muted">(showing ${cands.length}/${all.length})</span>` : '';
    const badge = meta.defight
      ? `<span class="badge" title="${esc(meta.defight_summary || 'z-fighting repaired before the search')}">Defight</span>`
      : '';
    h += `<section><div class="runhead"><h2>${esc(run)}${badge}${stateBadge}</h2>
      <div class="strip">
        <span>candidates <b>${cands.length}</b>${shown}</span>
        <span>passed gates <b>${pass.length}</b></span>
        <span>attempts/sec <b>${rates.att}</b></span>
        <span>solves/sec <b>${rates.sol}</b></span>
        <span>best <b>${best ? esc(best) : '—'}</b></span>
        <span>baseline margin <b>${doc.baseline.style_margin}</b>
              <span class="muted">(gain > 0 is progress)</span></span>
        <span class="muted">started ${fmtTime(meta.started)}
              &nbsp;·&nbsp; updated ${fmtTime(meta.updated)}</span>
        <span><a class="viewbtn" href="/view/${esc(run)}/baseline"
                 target="_blank">baseline viewer</a></span>
        ${best ? `<span>${authBtn(run, 'best', 'author the winner')}</span>` : ''}
        ${pauseBtn}
        ${runOps(run, meta)}
      </div>
      <div class="strip fltbar">
        <span class="muted">filter</span>
        ${fltPair(run, 'frac', 'min', 'max', f, 0.05)}
        ${fltPair(run, 'verts', 'vmin', 'vmax', f, 1)}
        ${fltPair(run, 'faces', 'fmin', 'fmax', f, 1)}
        ${fltOn(f) ? `<span><button class="pausebtn"
            onclick="clearFilter('${esc(run)}')">clear</button></span>` : ''}
        ${span ? `<span class="muted">shown span
            <b>${span.vlo}–${span.vhi}</b>v
            <b>${span.flo}–${span.fhi}</b>f</span>` : ''}
      </div></div>
      <h3>throughput</h3>
      <div class="halfrow">
        <canvas class="chart" id="rate-att-${esc(run)}"></canvas>
        <canvas class="chart" id="rate-sol-${esc(run)}"></canvas>
      </div>
      <h3>fitness map</h3>
      ${legend([['sw', '#7ec97e', 'current best'],
                ['sw', '#e6b450', 'passed gates'],
                ['swtick', '#6a4a4a', 'rejected (tick at bottom)'],
                ['swline', '#33363f', 'baseline (fitness 0)']])}
      <canvas class="chart" id="chart-${esc(run)}"></canvas>
      <h3>content preserver</h3>
      ${preserverSummary(cands)}
      ${legend([['swline', '#e6b450', 'bind identity'],
                ['swline', '#7ec9c9', 'pose identity'],
                ['swline', '#c97ea8', 'region close-up min'],
                ['swband', 'rgba(126,201,126,.4)', 'safe &ge;85'],
                ['swband', 'rgba(201,106,106,.4)', 'broken &le;60']])}
      <canvas class="chart" id="pres-${esc(run)}"></canvas>
      <h3>OSRS matcher</h3>
      ${matcherSummary(doc, cands)}
      ${legend([['sw', '#7ec97e', 'beats baseline'],
                ['sw', '#7e9cc9', 'passed gates'],
                ['sw', '#c96a6a', 'rejected'],
                ['swdash', '#c9b47e', 'baseline margin'],
                ['swline', '#33363f', 'zero — OSRS above']])}
      <canvas class="chart" id="match-${esc(run)}"></canvas>`;
    if (top.length) {
      h += `<h3>${fltOn(f) ? 'top hits in filter' : 'best so far'}</h3>` +
           `<div class="row">` +
           top.map(c => card(run, c, best)).join('') + `</div>`;
    } else if (fltOn(f)) {
      h += `<h3>top hits in filter</h3><p class="muted">nothing passed the
            gates inside this range${cands.length ? ' — ' + cands.length +
            ' candidate(s) match it, all rejected' : ''}.</p>`;
    }
    h += `<h3>recent</h3><div class="row">` +
         cands.slice(-8).reverse().map(c => card(run, c, best)).join('') +
         `</div></section>`;
  }
  document.getElementById('root').innerHTML = h;
  for (const [run, doc] of visibleRuns())
    if (doc && doc.baseline) {
      const cands = (doc.candidates || []).filter(c => inRange(run, c));
      drawRate(document.getElementById('rate-att-' + run), doc.candidates || [],
               () => true, '#7e9cc9', 'attempts per second');
      drawRate(document.getElementById('rate-sol-' + run), doc.candidates || [],
               c => c.fitness !== null && c.fitness !== undefined, '#7ec97e',
               'solves per second');
      drawChart(document.getElementById('chart-' + run), doc, cands);
      drawPreserver(document.getElementById('pres-' + run), cands);
      drawMatcher(document.getElementById('match-' + run), doc, cands);
    }
  document.getElementById('stamp').textContent = '— ' + new Date().toLocaleTimeString();
}

async function openModal(run, tag) {
  const doc = DATA[run] || {};
  const c = (doc.candidates || []).find(x => x.id === tag) || {id: tag};
  const files = await (await fetch(`/files/${run}?prefix=${encodeURIComponent(tag)}_`)).json();
  const bind = files.filter(f => f.startsWith(tag + '_bind_'));
  const posed = files.filter(f => !f.startsWith(tag + '_bind_')).sort();
  let h = `<span class="close" onclick="closeModal()">✕</span>
    <h2><span class="tag">${esc(tag)}</span> ${scoreline(c)}
      <a class="viewbtn" href="/view/${run}/${encodeURIComponent(tag)}"
         target="_blank" onclick="event.stopPropagation()">Open in viewer</a>
      ${authBtn(run, tag, 'author this')}
      <span class="star${isFav(run, tag) ? ' fav' : ''}" title="save to favorites"
        onclick="modalFav('${run}','${esc(tag)}',this)">${isFav(run, tag) ? '★' : '☆'}</span></h2>
    <div class="muted">${paramline(c, run)}</div>
    <h3>bind render (style + identity judges)</h3><div>` +
    bind.map(f => `<img src="/img/${run}/work/${f}">`).join('') + `</div>`;
  if (c.anim) {
    h += `<h3>animation sweep — per-sequence scores</h3><div class="muted">` +
      Object.entries(c.anim).map(([s, v]) =>
        `${esc(s)}: pose_id ${v.pose_id_mean} (min ${v.pose_id_min}, cov ${v.worst_cov_sym})`
      ).join(' &nbsp;|&nbsp; ') + `</div>`;
  }
  if (c.regions) {
    h += `<h3>region close-up identity (min ${c.region_min}, mean ${c.region_mean})</h3>
      <div class="muted">` +
      Object.entries(c.regions).map(([l, v]) => `label ${esc(l)}: ${v}`)
        .join(' &nbsp;|&nbsp; ') + `</div>`;
  }
  if (posed.length) {
    h += `<h3>close-ups &amp; posed frames — candidate (top) vs baseline (bottom)</h3>`;
    const frames = [...new Set(posed.map(f => f.slice(tag.length + 1).replace(/_y\\d+\\.png$/, '')))];
    for (const fr of frames) {
      const cf = posed.filter(f => f.startsWith(tag + '_' + fr));
      h += `<div class="pair"><div class="lbl">${esc(fr)}</div>` +
        cf.map(f => `<img src="/img/${run}/work/${f}">`).join('') + '<br>' +
        cf.map(f => `<img src="/img/${run}/work/base_${f.slice(tag.length + 1)}"
                       onerror="this.remove()">`).join('') + `</div>`;
    }
  }
  document.getElementById('modal-body').innerHTML = h;
  document.getElementById('modal').style.display = 'block';
}
function closeModal() { document.getElementById('modal').style.display = 'none'; }

// ---- image lightbox: scroll-zoom + drag-pan over any detail-view image ----
const lbBox = document.getElementById('lightbox');
const lbImg = document.getElementById('lb-img');
let lb = { scale: 3, x: 0, y: 0, drag: null, moved: false };
function lbApply() {
  lbImg.style.transform =
    `translate(-50%,-50%) translate(${lb.x}px,${lb.y}px) scale(${lb.scale})`;
}
function openLightbox(src) {
  lbImg.src = src;
  lb = { scale: 3, x: 0, y: 0, drag: null, moved: false };
  lbApply();
  lbBox.style.display = 'block';
}
function closeLightbox() { lbBox.style.display = 'none'; }
document.getElementById('modal-body').addEventListener('click', e => {
  if (e.target.tagName === 'IMG') { openLightbox(e.target.src); e.stopPropagation(); }
});
lbBox.addEventListener('wheel', e => {
  e.preventDefault();
  const k = e.deltaY < 0 ? 1.25 : 0.8;
  const ns = Math.min(24, Math.max(1, lb.scale * k));
  // zoom about the cursor: keep the point under the mouse fixed
  const cx = e.clientX - innerWidth / 2, cy = e.clientY - innerHeight / 2;
  lb.x = cx - (cx - lb.x) * (ns / lb.scale);
  lb.y = cy - (cy - lb.y) * (ns / lb.scale);
  lb.scale = ns;
  lbApply();
}, { passive: false });
lbBox.addEventListener('mousedown', e => {
  lb.drag = { x: e.clientX - lb.x, y: e.clientY - lb.y };
  lb.moved = false;
  lbBox.style.cursor = 'grabbing';
});
window.addEventListener('mousemove', e => {
  if (!lb.drag) return;
  lb.x = e.clientX - lb.drag.x;
  lb.y = e.clientY - lb.drag.y;
  lb.moved = true;
  lbApply();
});
window.addEventListener('mouseup', () => {
  const wasDrag = lb.moved;
  lb.drag = null;
  lbBox.style.cursor = 'grab';
  if (!wasDrag && lbBox.style.display === 'block') closeLightbox();
});
document.addEventListener('keydown', e => {
  if (e.key !== 'Escape') return;
  if (lbBox.style.display === 'block') closeLightbox();
  else if (document.getElementById('confirm').style.display === 'block')
    closeConfirm();
  else if (document.getElementById('newrun').style.display === 'block')
    closeNewRun();
  else closeModal();
});

async function refresh() {
  try {
    DATA = await (await fetch('/data')).json();
    await refreshFavs();
    // don't repaint under an open detail view or while a filter is being typed
    const ae = document.activeElement;
    if (document.getElementById('modal').style.display !== 'block' &&
        !(ae && ae.classList && ae.classList.contains('flt'))) render();
  } catch (e) {}
}
async function tick() {
  await refresh();
  setTimeout(tick, 5000);
}
tick();
// jobs survive a page reload, so pick up anything already compressing
pollJobs();
</script>
"""


VIEWER_PAGE = """<!doctype html>
<meta charset="utf-8">
<title>__TAG__ — osrsify viewer</title>
<style>
  body { background:#17181c; color:#d8d9de; font:14px/1.45 system-ui, sans-serif;
         margin:0; padding:1rem 1.5rem; }
  h1 { font-size:1.05rem; margin:.2rem 0 .8rem; }
  .tag { background:#2a2d36; border-radius:4px; padding:0 .45em;
         font-family:monospace; }
  .muted { color:#8b8e99; }
  a { color:#7ea6d9; }
  #bar { display:flex; gap:.8rem; align-items:center; flex-wrap:wrap;
         margin:.6rem 0; }
  select, button { background:#1d1f25; color:#d8d9de; border:1px solid #2c2e36;
                   border-radius:5px; padding:.25em .6em; font:inherit;
                   cursor:pointer; }
  input[type=range] { width:260px; }
  canvas { background:#141821; border:1px solid #2c2e36; border-radius:8px;
           cursor:grab; touch-action:none; }
  canvas:active { cursor:grabbing; }
  #stage { display:flex; gap:1rem; align-items:flex-start; flex-wrap:wrap; }
  #prio { background:#1d1f25; border:1px solid #2c2e36; border-radius:8px;
          padding:.6rem .9rem; min-width:200px; }
  #prio h2 { font-size:.78rem; margin:0 0 .45rem; color:#8b8e99;
             font-weight:600; text-transform:uppercase; letter-spacing:.05em; }
  #prio table { border-collapse:collapse; font-family:monospace;
                font-size:.9em; font-variant-numeric:tabular-nums; }
  #prio th { color:#8b8e99; font-weight:normal; }
  #prio td, #prio th { padding:.12rem .55rem; text-align:right; }
  #prio td:first-child, #prio th:first-child { text-align:left;
                                               padding-left:0; }
  #prio tr.total td { border-top:1px solid #2c2e36; color:#8b8e99; }
  #prio .note { color:#8b8e99; font-size:.78em; max-width:220px;
                margin-top:.5rem; line-height:1.4; }
</style>
<h1><span class="tag">__TAG__</span> <span class="muted">__RUN__ — live toridraw
  (wasm) render · __MODE__</span>
  <span style="float:right"><a href="/view/__RUN__/baseline" target="_blank">open
  baseline</a></span></h1>
<div id="bar">
  <select id="seq"><option value="">bind pose</option></select>
  <button id="play" disabled>Play</button>
  <input type="range" id="frame" min="0" max="0" value="0" disabled>
  <button id="recenter">Recenter</button>
  <button id="reset">Reset view</button>
  <span id="label" class="muted">loading model…</span>
</div>
<div id="stage">
  <canvas id="view" width="720" height="720"></canvas>
  <div id="prio">
    <h2>faces per priority</h2>
    <div id="priobody" class="muted">loading&hellip;</div>
  </div>
</div>
<div class="muted" style="margin-top:.5rem">drag to look &nbsp;·&nbsp; wheel or
  W/S to fly forward/back &nbsp;·&nbsp; A/D strafe &nbsp;·&nbsp; R/F up/down
  &nbsp;·&nbsp; arrows turn the camera &nbsp;·&nbsp;
  frames play with the sequence's own tick delays</div>
<script src="/ev_wasm.js"></script>
<script>
'use strict';
const RUN = "__RUN__", TAG = "__TAG__", SEQS = __SEQS__;
// The run's render discipline, from its recorded config: z-buffered runs are
// judged depth-tested, everything else uses the painter's sort with face
// priorities (the authored path, and what --force-priorities relies on).
const ZBUFFER = __ZB__;
const TICK_MS = 20;
const CANVAS = document.getElementById('view');
const CTX = CANVAS.getContext('2d');
// Free camera, emulated on the fixed wasm camera (see frameLoop): world
// position `cam`, plus yaw/pitch in engine angle units (2048 per turn,
// y grows downward like RS model space). The model sits at the world origin.
const st = { wasm: null, frameCount: 0, frame: 0, playing: false,
             cam: { x: 0, y: 0, z: 0 }, camYaw: 0, camPitch: 200,
             zoom0: 1400, speed: 1200, keys: new Set(),
             frameAcc: 0, lastT: 0, seq: '' };
const U2R = Math.PI / 1024;       // engine angle units -> radians
const PROJ_SCALE = 512;           // TORIDRAW_PROJECTION_SCALE_DEFAULT

// Camera basis in world coordinates (matches the engine's rotation forms:
// yaw is x' = x cos + z sin, z' = z cos - x sin; pitch mixes y/z).
function camAxes() {
  const y = st.camYaw * U2R, p = st.camPitch * U2R;
  const cy = Math.cos(y), sy = Math.sin(y), cp = Math.cos(p), sp = Math.sin(p);
  return { forward: { x: -cp * sy, y: sp, z: cp * cy },
           right:   { x: cy,       y: 0,  z: sy } };
}

function placeCameraDefault() {
  const p = st.camPitch * U2R, h = st.wasm ? st.wasm.modelHeight() : 0;
  st.cam.x = 0;
  st.cam.y = -(Math.sin(p) * st.zoom0 + h / 2);
  st.cam.z = -Math.cos(p) * st.zoom0;
}

// Point the camera at the model's centre without moving it.
function aimAtModel() {
  const h = st.wasm ? st.wasm.modelHeight() : 0;
  const rx = -st.cam.x, ry = -h / 2 - st.cam.y, rz = -st.cam.z;
  const r = Math.hypot(rx, rz);
  if (r < 1 && Math.abs(ry) < 1) return;
  st.camYaw = Math.atan2(-rx, rz) / U2R;
  st.camPitch = Math.max(-500, Math.min(500, Math.atan2(ry, r) / U2R));
}

function setLabel(t) { document.getElementById('label').textContent = t; }

// Fill the faces-per-priority table from the census route. Runs after the
// model wire is built, so the server just re-parses the cached file.
async function loadPriorities() {
  const el = document.getElementById('priobody');
  const zbNote = ZBUFFER
    ? ' This run renders z-buffered, so priorities are not used here.' : '';
  try {
    const res = await fetch(`/wire/${RUN}/${encodeURIComponent(TAG)}.prio`);
    if (!res.ok) { el.textContent = 'census unavailable'; return; }
    const d = await res.json();
    const note = `<div class="note">0&ndash;10 draw as strict layers,` +
      ` depth-sorted within each; 11&ndash;12 interleave with neighbours by` +
      ` average depth.${zbNote}</div>`;
    if (d.flat !== null && d.flat !== undefined) {
      el.innerHTML = `one flat bucket &mdash; every one of ${d.face_count}` +
        ` faces draws at priority <b>${d.flat}</b>, so nothing is banded` +
        `${note}`;
      return;
    }
    const prios = Object.keys(d.counts).map(Number).sort((a, b) => a - b);
    let rows = '';
    for (const p of prios) {
      const n = d.counts[p];
      const pct = (100 * n / d.face_count).toFixed(1);
      rows += `<tr><td>${p}</td><td>${n}</td>` +
              `<td class="muted">${pct}%</td></tr>`;
    }
    el.innerHTML = `<table>` +
      `<tr><th>prio</th><th>faces</th><th>share</th></tr>${rows}` +
      `<tr class="total"><td>all</td><td>${d.face_count}</td>` +
      `<td>100%</td></tr></table>${note}`;
  } catch (e) {
    el.textContent = 'census unavailable';
  }
}

async function feed(url, fn) {
  const res = await fetch(url);
  if (!res.ok) { setLabel(await res.text()); return 0; }
  const bytes = new Uint8Array(await res.arrayBuffer());
  const ptr = st.wasm.alloc(bytes.length);
  if (!ptr) return 0;
  st.wasm.mod.HEAPU8.set(bytes, ptr);
  const out = fn(ptr, bytes.length);
  st.wasm.release(ptr);
  return out;
}

function updateUi() {
  const slider = document.getElementById('frame');
  slider.max = Math.max(0, st.frameCount - 1);
  slider.value = st.frame;
  slider.disabled = st.frameCount === 0;
  const play = document.getElementById('play');
  play.disabled = st.frameCount === 0;
  play.textContent = st.playing ? 'Pause' : 'Play';
  setLabel(st.frameCount
    ? `${st.seq} · frame ${st.frame + 1}/${st.frameCount}`
    : 'bind pose');
}

async function selectSeq(name) {
  st.seq = name;
  if (!name) {
    st.wasm.clearAnim();
    st.frameCount = 0; st.frame = 0; st.playing = false;
    updateUi();
    return;
  }
  setLabel('building animation…');
  const frames = await feed(`/wire/${RUN}/${encodeURIComponent(name)}.anim`,
                            (p, n) => st.wasm.setAnim(p, n));
  st.frameCount = frames; st.frame = 0; st.frameAcc = 0;
  st.playing = frames > 0;
  updateUi();
}

// Held-key camera movement, applied per frame so it is smooth and
// frame-rate independent. W/S fly along the view direction, A/D strafe,
// R/F ride world Y (R up — y is down-positive, so up subtracts); arrows
// turn the camera in place.
const TURN_U_S = 900, PITCH_U_S = 450;
function moveCamera(dt) {
  if (!st.keys.size || !dt) return;
  const s = Math.min(dt, 100) / 1000, k = st.keys;
  const { forward, right } = camAxes();
  const m = st.speed * s;
  if (k.has('KeyW')) { st.cam.x += forward.x * m; st.cam.y += forward.y * m; st.cam.z += forward.z * m; }
  if (k.has('KeyS')) { st.cam.x -= forward.x * m; st.cam.y -= forward.y * m; st.cam.z -= forward.z * m; }
  if (k.has('KeyD')) { st.cam.x += right.x * m; st.cam.z += right.z * m; }
  if (k.has('KeyA')) { st.cam.x -= right.x * m; st.cam.z -= right.z * m; }
  if (k.has('KeyR')) st.cam.y -= m;
  if (k.has('KeyF')) st.cam.y += m;
  if (k.has('ArrowLeft'))  st.camYaw += TURN_U_S * s;
  if (k.has('ArrowRight')) st.camYaw -= TURN_U_S * s;
  if (k.has('ArrowUp'))
    st.camPitch = Math.max(-500, st.camPitch - PITCH_U_S * s);
  if (k.has('ArrowDown'))
    st.camPitch = Math.min(500, st.camPitch + PITCH_U_S * s);
}

function frameLoop(t) {
  requestAnimationFrame(frameLoop);
  if (!st.wasm) return;
  const dt = st.lastT ? t - st.lastT : 0;
  st.lastT = t;
  moveCamera(dt);
  if (st.playing && st.frameCount > 0) {
    // delay 0 counts as one tick, or the animation spins at refresh rate
    const delay = Math.max(1, st.wasm.frameDelay(st.frame));
    st.frameAcc += dt;
    while (st.frameAcc >= delay * TICK_MS) {
      st.frameAcc -= delay * TICK_MS;
      st.frame = (st.frame + 1) % st.frameCount;
    }
    document.getElementById('frame').value = st.frame;
    setLabel(`${st.seq} · frame ${st.frame + 1}/${st.frameCount}`);
  }
  const w = CANVAS.width, h = CANVAS.height;
  // The wasm camera is fixed at the origin with yaw 0; it takes (model yaw,
  // pitch, zoom, pan). Emulate the free camera by handing it the model's
  // position in camera space. The engine's model-yaw and camera-yaw
  // rotations are the same matrix, so position.yaw = camYaw supplies the
  // yaw the camera lacks. It also lifts position.y by modelHeight/2 and
  // places the frame anchor at depth == zoom with pan as a pixel offset at
  // that depth, so solve zoom/pan back from the camera-space position.
  const yawU = Math.round(st.camYaw) & 2047;
  const pitU = Math.round(st.camPitch) & 2047;
  const yr = st.camYaw * U2R, pr = st.camPitch * U2R;
  const rx = -st.cam.x, ry = -st.cam.y, rz = -st.cam.z;   // model - camera
  const sx = rx * Math.cos(yr) + rz * Math.sin(yr);
  const sz = rz * Math.cos(yr) - rx * Math.sin(yr);
  const y1 = ry - st.wasm.modelHeight() / 2;
  const sp = Math.sin(pr), cp = Math.cos(pr);
  const zoom = Math.max(60, Math.round(y1 * sp + sz * cp));
  const dy = y1 * cp - sz * sp;
  const clampPan = v => Math.max(-30000, Math.min(30000, Math.round(v)));
  st.wasm.setPan(clampPan(sx * PROJ_SCALE / zoom),
                 clampPan(dy * PROJ_SCALE / zoom));
  const ptr = st.wasm.render(w, h, yawU, pitU, zoom,
                             st.frameCount > 0 ? st.frame : -1);
  if (!ptr) return;
  const bytes = st.wasm.mod.HEAPU8.subarray(ptr, ptr + w * h * 4);
  CTX.putImageData(new ImageData(new Uint8ClampedArray(bytes), w, h), 0, 0);
}

function wireInput() {
  let dragging = false, lastX = 0, lastY = 0;
  CANVAS.addEventListener('pointerdown', e => {
    dragging = true; lastX = e.clientX; lastY = e.clientY;
    CANVAS.setPointerCapture(e.pointerId);
  });
  CANVAS.addEventListener('pointerup', e => {
    dragging = false; CANVAS.releasePointerCapture(e.pointerId);
  });
  CANVAS.addEventListener('pointermove', e => {
    if (!dragging) return;
    st.camYaw -= (e.clientX - lastX) * 6;
    st.camPitch = Math.max(-500, Math.min(500,
      st.camPitch + (e.clientY - lastY) * 3));
    lastX = e.clientX; lastY = e.clientY;
  });
  CANVAS.addEventListener('wheel', e => {
    e.preventDefault();
    const { forward } = camAxes();
    const m = -e.deltaY * st.speed / 400;
    st.cam.x += forward.x * m; st.cam.y += forward.y * m;
    st.cam.z += forward.z * m;
  }, { passive: false });
  document.getElementById('play').onclick = () => {
    st.playing = !st.playing; updateUi();
  };
  document.getElementById('frame').oninput = e => {
    st.playing = false;
    st.frame = Number(e.target.value);
    updateUi();
  };
  document.getElementById('seq').onchange = e => selectSeq(e.target.value);
  document.getElementById('recenter').onclick = () => aimAtModel();
  document.getElementById('reset').onclick = () => {
    st.camYaw = 0; st.camPitch = 200;
    placeCameraDefault();
  };
  const MOVE_KEYS = new Set(['KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyR', 'KeyF',
                             'ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown']);
  window.addEventListener('keydown', e => {
    // leave the slider and the sequence picker their own keys
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
    if (MOVE_KEYS.has(e.code)) { st.keys.add(e.code); e.preventDefault(); }
  });
  window.addEventListener('keyup', e => st.keys.delete(e.code));
  window.addEventListener('blur', () => st.keys.clear());
}

(async function main() {
  const sel = document.getElementById('seq');
  for (const s of SEQS) {
    const o = document.createElement('option');
    o.value = s; o.textContent = s;
    sel.appendChild(o);
  }
  const mod = await EVModule();
  st.wasm = {
    init: mod.cwrap('ev_w_init', null, []),
    alloc: mod.cwrap('ev_w_alloc', 'number', ['number']),
    release: mod.cwrap('ev_w_release', null, ['number']),
    setModel: mod.cwrap('ev_w_set_model', 'number', ['number', 'number']),
    setAnim: mod.cwrap('ev_w_set_anim', 'number', ['number', 'number']),
    clearAnim: mod.cwrap('ev_w_clear_anim', null, []),
    frameCount: mod.cwrap('ev_w_frame_count', 'number', []),
    frameDelay: mod.cwrap('ev_w_frame_delay', 'number', ['number']),
    modelHeight: mod.cwrap('ev_w_model_height', 'number', []),
    render: mod.cwrap('ev_w_render', 'number',
      ['number', 'number', 'number', 'number', 'number', 'number']),
    setPan: mod.cwrap('ev_w_set_pan', null, ['number', 'number']),
    setZbuffer: mod.cwrap('ev_w_set_zbuffer', null, ['number']),
    mod,
  };
  st.wasm.init();
  st.wasm.setZbuffer(ZBUFFER ? 1 : 0);
  setLabel('building model…');
  const faces = await feed(`/wire/${RUN}/${encodeURIComponent(TAG)}.model`,
                           (p, n) => st.wasm.setModel(p, n));
  if (!faces) return;
  loadPriorities();
  const h = st.wasm.modelHeight();
  st.zoom0 = Math.max(260, Math.min(12000, Math.round(h * 1.6) || 900));
  st.speed = Math.max(500, st.zoom0);
  placeCameraDefault();
  setLabel(`${faces} faces · bind pose`);
  wireInput();
  requestAnimationFrame(frameLoop);
})();
</script>
"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, ctype, body):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlparse(unquote(self.path))
        path = url.path
        if path in ("/", "/index.html"):
            page = (PAGE.replace("__OPTS__", json.dumps(OPTS))
                        .replace("__GROUPS__", json.dumps(GROUPS))
                        .replace("__WIZARD__", json.dumps(WIZARD)))
            return self.send(200, "text/html; charset=utf-8", page.encode())
        if path == "/api/recents":
            return self.send(200, "application/json",
                             json.dumps(load_recents()).encode())
        if path == "/api/presets":
            return self.send(200, "application/json",
                             json.dumps(load_presets()).encode())
        if path == "/api/jobs":
            return self.send(200, "application/json",
                             json.dumps({"jobs": job_list()}).encode())
        m = re.match(r"^/api/runsize/([^/]+)$", path)
        if m and m.group(1) in RUNS:
            paths = run_paths(m.group(1))
            n, tot = path_stats(paths)
            return self.send(200, "application/json", json.dumps(
                {"files": n, "bytes": tot, "paths": paths,
                 "state": run_state(m.group(1)),
                 "bin": "Recycle Bin" if os.name == "nt" else "Trash",
                 "archive": archive_of(m.group(1))}).encode())
        if path == "/data":
            scan_runs()
            out = {}
            for name, d in list(RUNS.items()):
                try:
                    with open(os.path.join(d, "results.json"), "r",
                              encoding="utf-8") as f:
                        out[name] = json.load(f)
                except (OSError, ValueError):
                    out[name] = None
                if out[name] is not None:
                    out[name]["_meta"] = run_meta(name, out[name])
                    stamp_candidates(name, out[name])
                if name in PROCS:
                    p = PROCS[name]
                    alive = p.poll() is None
                    proc = ("running (pid %d)" % p.pid if alive
                            else "exited %s" % p.returncode)
                    if out[name] is None:
                        out[name] = {"_starting": alive,
                                     "_meta": {"proc": proc}}
                    else:
                        out[name]["_meta"]["proc"] = proc
                # liveness ping: heartbeat + pid probe on every poll, so the
                # UI reflects a dead/paused/finished search within seconds —
                # attached even before the first results.json checkpoint
                st = run_state(name)
                if out[name] is None:
                    out[name] = {"_meta": {}}
                m = out[name].setdefault("_meta", {})
                m["state"] = st
                m["archive"] = archive_of(name)
            return self.send(200, "application/json", json.dumps(out).encode())
        if path == "/favs":
            return self.send(200, "application/json",
                             json.dumps(list_favs()).encode())
        m = re.match(r"^/savimg/([^/]+)/([^/]+)$", path)
        if m and safe_tag(m.group(1)) and safe_tag(m.group(2)):
            full = os.path.join(SAVES, m.group(1), m.group(2))
            if os.path.isfile(full) and full.lower().endswith(".png"):
                with open(full, "rb") as f:
                    return self.send(200, "image/png", f.read())
        m = re.match(r"^/files/([^/]+)$", path)
        if m and m.group(1) in RUNS:
            prefix = parse_qs(url.query).get("prefix", [""])[0]
            work = os.path.join(RUNS[m.group(1)], "work")
            names = sorted(
                f for f in os.listdir(work)
                if f.endswith(".png") and (not prefix or f.startswith(prefix)))
            return self.send(200, "application/json", json.dumps(names).encode())
        m = re.match(r"^/img/([^/]+)/(.+)$", path)
        if m and m.group(1) in RUNS:
            # read-only, traversal-guarded file serving from inside the run dir
            base = RUNS[m.group(1)]
            full = os.path.realpath(os.path.join(base, m.group(2)))
            if full.startswith(os.path.realpath(base) + os.sep) and \
                    os.path.isfile(full) and full.lower().endswith(".png"):
                with open(full, "rb") as f:
                    return self.send(200, "image/png", f.read())
        if path in ("/ev_wasm.js", "/ev_wasm.wasm"):
            full = os.path.join(EV_WEB, path[1:])
            if os.path.isfile(full):
                ctype = ("application/wasm" if path.endswith(".wasm")
                         else "text/javascript; charset=utf-8")
                with open(full, "rb") as f:
                    return self.send(200, ctype, f.read())
            return self.send(503, "text/plain",
                             b"ev_wasm not built: make -C tools/entity_viewer wasm")
        m = re.match(r"^/view/([^/]+)/(.+)$", path)
        if m and m.group(1) in RUNS and safe_tag(m.group(2)):
            run, tag = m.group(1), m.group(2)
            try:
                cfg = run_config(run)
            except (OSError, ValueError):
                cfg = {}
            seqs = cfg.get("seqs", [])
            # Render the way the search judged: z mode only when the run
            # recorded --zbuffer; otherwise the painter path, which is what
            # honours the per-face priority bands.
            zb = bool(cfg.get("zbuffer"))
            page = (VIEWER_PAGE
                    .replace("__RUN__", run)
                    .replace("__TAG__", tag)
                    .replace("__SEQS__", json.dumps(seqs))
                    .replace("__ZB__", "1" if zb else "0")
                    .replace("__MODE__",
                             "z-buffered" if zb else "painter + priorities"))
            return self.send(200, "text/html; charset=utf-8", page.encode())
        m = re.match(r"^/wire/([^/]+)/(.+)\.(model|anim|prio)$", path)
        if m and m.group(1) in RUNS and safe_tag(m.group(2)):
            run, name, kind = m.group(1), m.group(2), m.group(3)
            try:
                if kind == "anim":
                    out, err = build_wire(run, seq=name)
                else:
                    out, err = build_wire(run, tag=name)
            except (OSError, ValueError) as e:
                out, err = None, str(e)
            if not out:
                return self.send(500, "text/plain; charset=utf-8",
                                 ("wire build failed: %s" % err).encode())
            if kind == "prio":
                census = wire_priorities(out)
                if census is None:
                    return self.send(500, "text/plain; charset=utf-8",
                                     b"wire model did not parse")
                return self.send(200, "application/json",
                                 json.dumps(census).encode())
            with open(out, "rb") as f:
                return self.send(200, "application/octet-stream", f.read())
        self.send(404, "text/plain", b"not found")

    def do_POST(self):
        path = urlparse(unquote(self.path)).path
        m = re.match(r"^/api/kill/([^/]+)$", path)
        if m and m.group(1) in RUNS:
            pids = kill_run(m.group(1))
            msg = ("killed process tree(s): %s" % ", ".join(map(str, pids))
                   if pids else "no running osrsify process found for this run")
            return self.send(200, "application/json",
                             json.dumps({"killed": pids,
                                         "message": msg}).encode())
        m = re.match(r"^/api/(pause|resume)/([^/]+)$", path)
        if m and m.group(2) in RUNS:
            try:
                msg = set_paused(m.group(2), m.group(1) == "pause")
            except OSError as e:
                return self.send(500, "text/plain; charset=utf-8",
                                 ("pause flag failed: %s" % e).encode())
            return self.send(200, "application/json",
                             json.dumps({"message": msg}).encode())
        m = re.match(r"^/api/(archive|delete)/([^/]+)$", path)
        if m and m.group(2) in RUNS:
            kind, run = m.group(1), m.group(2)
            if job_busy(run):
                return self.send(409, "text/plain; charset=utf-8",
                                 b"another archive/delete is already working "
                                 b"on this run")
            st = run_state(run).get("state")
            if st in ("running", "paused", "pause_requested"):
                # Windows will not unlink files the search still holds open,
                # and a half-zipped live run is not a backup either
                return self.send(409, "text/plain; charset=utf-8",
                                 ("the search is still %s — stop it first"
                                  % st).encode())
            job = job_start(kind, run,
                            do_archive if kind == "archive" else do_delete)
            return self.send(200, "application/json",
                             json.dumps({"job": dict(job)}).encode())
        m = re.match(r"^/api/job/([^/]+)/(cancel|dismiss)$", path)
        if m:
            with JOBS_LOCK:
                job = JOBS.get(m.group(1))
                if job is None:
                    return self.send(404, "text/plain", b"no such job")
                if m.group(2) == "cancel":
                    # the archive loop checks this between files; a delete is
                    # one shell call and cannot be taken back mid-flight
                    job["cancel"] = True
                    msg = ("cancelling" if job["kind"] == "archive" else
                           "a delete in progress cannot be cancelled")
                elif job["state"] == "working":
                    return self.send(409, "text/plain", b"job still running")
                else:
                    del JOBS[m.group(1)]
                    msg = "dismissed"
            return self.send(200, "application/json",
                             json.dumps({"message": msg}).encode())
        if path == "/api/start":
            try:
                n = int(self.headers.get("Content-Length") or 0)
                form = json.loads(self.rfile.read(n).decode("utf-8") or "{}")
                if not isinstance(form, dict):
                    raise ValueError("expected a JSON object of options")
                info = start_run(form)
            except (OSError, ValueError) as e:
                return self.send(400, "text/plain; charset=utf-8",
                                 ("start failed: %s" % e).encode())
            return self.send(200, "application/json",
                             json.dumps(info).encode())
        m = re.match(r"^/fav/([^/]+)/(.+)$", path)
        if m and m.group(1) in RUNS and safe_tag(m.group(2)):
            try:
                meta = save_fav(m.group(1), m.group(2))
            except (OSError, ValueError) as e:
                return self.send(500, "text/plain; charset=utf-8",
                                 ("save failed: %s" % e).encode())
            return self.send(200, "application/json",
                             json.dumps(meta).encode())
        m = re.match(r"^/unfav/([^/]+)$", path)
        if m and safe_tag(m.group(1)):
            d = os.path.join(SAVES, m.group(1))
            # only remove what save_fav wrote: a folder with its meta.json
            if os.path.isfile(os.path.join(d, "meta.json")):
                shutil.rmtree(d, ignore_errors=True)
                return self.send(200, "application/json", b"{}")
        self.send(404, "text/plain", b"not found")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("runs", nargs="*", help="run dirs (default: runs/osrsify_*)")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--host", default="0.0.0.0",
                    help="bind address (default all interfaces; use 127.0.0.1 for local-only)")
    o = ap.parse_args()
    global AUTO_SCAN
    AUTO_SCAN = not o.runs
    dirs = o.runs or sorted(glob.glob(os.path.join(HERE, "runs", "osrsify_*")))
    for d in dirs:
        d = os.path.abspath(d)
        if os.path.isdir(d):
            RUNS[os.path.basename(d)] = d
    if not RUNS and o.runs:
        sys.exit("watch_osrsify: none of those run directories exist")
    # an empty runs/ is not an error: the dashboard's guided flow is exactly
    # how you get your first run, so serve it rather than refusing to start
    print("watching: %s" % (", ".join(RUNS) or "(nothing yet)"))
    print("open http://localhost:%d/ (bound to %s)" % (o.port, o.host), flush=True)
    ThreadingHTTPServer((o.host, o.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
