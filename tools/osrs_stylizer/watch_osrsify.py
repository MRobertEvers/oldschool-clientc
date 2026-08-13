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
  - per-run summary strip (candidates, pass rate, best, baseline margin)
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

Stdlib only. Read-only over the run directories themselves (except the PAUSE
flag), but the kill and start controls do manage osrsify.py processes on this
machine.
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
    return {"run": name, "pid": p.pid, "log": log_path,
            "cmd": subprocess.list2cmdline(cmd)}


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
  a.viewbtn { display:inline-block; background:#26405f; color:#cfe2f7;
              border:1px solid #3a5f8a; border-radius:5px; padding:.05em .6em;
              text-decoration:none; font-size:.8em; vertical-align:middle; }
  a.viewbtn:hover { background:#2f4f75; }
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
  <div class="inner">
    <span class="close" onclick="closeNewRun()">✕</span>
    <h2>start a new run</h2>
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
<div id="lightbox"><img id="lb-img"><div class="hint">scroll to zoom &nbsp;·&nbsp; drag to pan &nbsp;·&nbsp; Esc / click to close</div></div>
<script>
const esc = s => String(s).replace(/[&<>"]/g,
  c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
// every osrsify.py option (server-injected), for the new-run form
const OPTS = __OPTS__;
const GROUPS = __GROUPS__;
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
           target="_blank" onclick="event.stopPropagation()">viewer</a>`
      : `<span class="muted" style="font-size:.75em">run offline — models kept in
           saves/${esc(f._key)}</span>`) +
    `</div>`;
}

// per-run fraction-range filter; survives the 5s refresh because render()
// repaints the inputs from this map
let FILTERS = {};
function fltOf(run) { return FILTERS[run] || {min: '', max: ''}; }
function setFilter(run, which, val) {
  FILTERS[run] = FILTERS[run] || {min: '', max: ''};
  FILTERS[run][which] = val;
  render();
}
function inRange(run, c) {
  const f = fltOf(run), fr = (c.params || {}).frac;
  if (fr === undefined) return true;   // sculpt candidates have no frac
  if (f.min !== '' && fr < parseFloat(f.min) - 1e-9) return false;
  if (f.max !== '' && fr > parseFloat(f.max) + 1e-9) return false;
  return true;
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
function card(run, c, best) {
  const rej = c.fitness === null || c.fitness === undefined;
  const cls = 'card' + (c.id === best ? ' best' : '') + (rej ? ' rej' : '');
  const fav = isFav(run, c.id);
  return `<div class="${cls}" onclick="openModal('${run}','${esc(c.id)}')">
    <span class="star${fav ? ' fav' : ''}" title="save to favorites"
      onclick="event.stopPropagation();toggleFav('${run}','${esc(c.id)}')">${fav ? '★' : '☆'}</span>
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
}
function closeNewRun() { document.getElementById('newrun').style.display = 'none'; }
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
      h += `<section><div class="runhead"><h2>${esc(run)}${stateBadge}</h2></div>
            <p class="muted">${doc && (doc._starting || ls.state === 'running')
              ? 'starting — waiting for the baseline'
              : 'no results.json yet'}</p></section>`;
      continue;
    }
    annotateDurations(doc);
    const all = doc.candidates || [], best = doc.best && doc.best.id;
    const rates = rateSummary(all);
    const cands = all.filter(c => inRange(run, c));
    const pass = cands.filter(c => c.fitness !== null && c.fitness !== undefined);
    const top = pass.slice().sort((a, b) => b.fitness - a.fitness).slice(0, 3);
    const f = fltOf(run);
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
        ${pauseBtn}
        <span class="muted">frac
          <input class="flt" type="number" step="0.05" min="0" max="1"
                 placeholder="min" value="${f.min}"
                 onchange="setFilter('${esc(run)}','min',this.value)">
          –
          <input class="flt" type="number" step="0.05" min="0" max="1"
                 placeholder="max" value="${f.max}"
                 onchange="setFilter('${esc(run)}','max',this.value)"></span>
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
      h += `<h3>best so far</h3><div class="row">` +
           top.map(c => card(run, c, best)).join('') + `</div>`;
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
const PROJ_SCALE = 512;           // TORIDRAW_PROJ_SCALE_DEFAULT

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
                        .replace("__GROUPS__", json.dumps(GROUPS)))
            return self.send(200, "text/html; charset=utf-8", page.encode())
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
                    out[name] = {"_meta": {"state": st}}
                else:
                    out[name].setdefault("_meta", {})["state"] = st
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
    if not RUNS:
        sys.exit("watch_osrsify: no run directories found")
    print("watching: %s" % ", ".join(RUNS))
    print("open http://localhost:%d/ (bound to %s)" % (o.port, o.host), flush=True)
    ThreadingHTTPServer((o.host, o.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
