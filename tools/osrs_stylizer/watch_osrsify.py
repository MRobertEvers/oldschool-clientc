#!/usr/bin/env python3
"""watch_osrsify.py — live browser dashboard for running osrsify searches.

Reads each run's results.json (atomically rewritten by osrsify.py after every
candidate) and serves an exploration page:

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

Stdlib only; read-only over the run directories.
"""

import argparse
import glob
import json
import os
import re
import shutil
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
  canvas.chart { background:#1d1f25; border:1px solid #2c2e36;
                 border-radius:8px; width:100%; max-width:820px; height:180px; }
  #runbar { position:sticky; top:0; z-index:10; background:#17181c;
            border-bottom:1px solid #2c2e36; padding:.5rem 0; margin:0 0 .6rem;
            display:flex; gap:.4rem; align-items:center; flex-wrap:wrap; }
  .chip { cursor:pointer; border:1px solid #2c2e36; border-radius:999px;
          padding:.05em .7em; font-size:.8em; color:#8b8e99; background:#1d1f25;
          user-select:none; white-space:nowrap; }
  .chip.on { color:#d8d9de; border-color:#3a5f8a; background:#26405f; }
  .chip:hover { border-color:#5a5e6c; }
  .chip .dot { display:inline-block; width:.55em; height:.55em;
               border-radius:50%; margin-right:.35em; background:#8b8e99;
               vertical-align:baseline; }
  .dot.live { background:#7ec97e; box-shadow:0 0 5px #7ec97e; }
  .dot.stale { background:#e6b450; }
  .dot.idle { background:#8b8e99; }
  .dot.off { background:#c96a6a; }
  .chip .cnt { color:#8b8e99; font-size:.85em; margin-left:.3em; }
  #modal { position:fixed; inset:0; background:rgba(10,10,12,.88);
           overflow:auto; padding:2rem; display:none; }
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
<h1>osrsify live <span class="muted" id="stamp"></span></h1>
<div id="runbar"><span class="muted" style="font-size:.85em">runs</span>
  <span class="chip" onclick="selectAllRuns(true)" title="show every run">all</span>
  <span class="chip" onclick="selectAllRuns(false)" title="hide every run">none</span>
  <span class="muted" style="font-size:.85em">·</span>
  <span id="runchips" style="display:contents"></span>
  <label class="muted" style="font-size:.8rem; cursor:pointer;
                              margin-left:auto; white-space:nowrap">
    <input type="checkbox" id="latest-only"> latest run only</label></div>
<div id="root" class="muted">loading…</div>
<div id="modal" onclick="if(event.target===this)closeModal()"><div class="inner" id="modal-body"></div></div>
<div id="lightbox"><img id="lb-img"><div class="hint">scroll to zoom &nbsp;·&nbsp; drag to pan &nbsp;·&nbsp; Esc / click to close</div></div>
<script>
const esc = s => String(s).replace(/[&<>]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
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
  const reg = c.region_min !== undefined ? ` reg ${c.region_min}` : '';
  return `fitness <b style="color:#e6b450">${c.fitness}</b>
       <span class="muted">m ${c.style_margin} id ${c.identity} pose ${c.pose_id}${reg}</span>`;
}
function paramline(c) {
  const p = c.params || {};
  const when = c.ts ? ` · ${fmtTime(c.ts)}` : '';
  if (p.regime === 'sculpt') return esc((p.moves || []).join('; ')) + when;
  let s = `frac ${p.frac} seed ${p.seed}`;
  if (p.parts) s += ' — ' + p.parts.map(x => `${x.vertices}v`).join('+');
  return s + when;
}
function card(run, c, best) {
  const rej = c.fitness === null || c.fitness === undefined;
  const cls = 'card' + (c.id === best ? ' best' : '') + (rej ? ' rej' : '');
  const fav = isFav(run, c.id);
  return `<div class="${cls}" onclick="openModal('${run}','${esc(c.id)}')">
    <span class="star${fav ? ' fav' : ''}" title="save to favorites"
      onclick="event.stopPropagation();toggleFav('${run}','${esc(c.id)}')">${fav ? '★' : '☆'}</span>
    <span class="tag">${esc(c.id)}</span>${scoreline(c)}
    <div class="muted" style="font-size:.8em">${paramline(c)}</div>
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
  let lx = W - 260 * devicePixelRatio / 2;
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
    ctx.fillText(key, lx, H - 6);
    lx += (key.length + 3) * 7 * devicePixelRatio;
  }
  ctx.fillStyle = '#8b8e99';
  ctx.fillText('candidates →', 34, H - 6 - 14 * devicePixelRatio);
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
  ctx.fillText('candidates → · green beats baseline · red failed a gate', 34, H - 6);
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
function renderRunBar() {
  // newest runs first, so the chips read like the run history
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
    const st = !doc ? ['off', 'no results.json yet']
      : age < 180 ? ['live', `live — updated ${Math.max(0, Math.round(age))}s ago`]
      : age < 900 ? ['stale', `quiet — last update ${Math.round(age / 60)}m ago`]
      : ['idle', `idle — last update ${fmtTime(meta.updated)}`];
    const tip = st[1] +
      (doc && doc.best && doc.best.id ? ` · best ${doc.best.id}` : '') +
      (meta.started ? ` · started ${fmtTime(meta.started)}` : '');
    return `<span class="chip${runShown(n) && !latestBox.checked ? ' on' : ''}"
       title="${esc(tip)}" onclick="toggleRun('${esc(n)}')"><span
       class="dot ${st[0]}"></span>${esc(n)}<span class="cnt">${cands}</span></span>`;
  }).join('');
}

function render() {
  renderRunBar();
  let h = '';
  if (FAVS.length) {
    h += `<h2>favorites <span class="muted" style="font-size:.75em">
            ~/Documents/osrsify/saves</span></h2>
          <div class="row">` + FAVS.map(favCard).join('') + `</div>`;
  }
  for (const [run, doc] of visibleRuns()) {
    if (!doc) { h += `<h2>${esc(run)}</h2><p class="muted">no results.json yet</p>`; continue; }
    const all = doc.candidates || [], best = doc.best && doc.best.id;
    const cands = all.filter(c => inRange(run, c));
    const pass = cands.filter(c => c.fitness !== null && c.fitness !== undefined);
    const top = pass.slice().sort((a, b) => b.fitness - a.fitness).slice(0, 3);
    const f = fltOf(run);
    const shown = cands.length !== all.length
      ? ` <span class="muted">(showing ${cands.length}/${all.length})</span>` : '';
    const meta = doc._meta || {};
    const badge = meta.defight
      ? `<span class="badge" title="${esc(meta.defight_summary || 'z-fighting repaired before the search')}">Defight</span>`
      : '';
    h += `<h2>${esc(run)}${badge}</h2>
      <div class="strip">
        <span>candidates <b>${cands.length}</b>${shown}</span>
        <span>passed gates <b>${pass.length}</b></span>
        <span>best <b>${best ? esc(best) : '—'}</b></span>
        <span>baseline margin <b>${doc.baseline.style_margin}</b>
              <span class="muted">(gain > 0 is progress)</span></span>
        <span class="muted">started ${fmtTime(meta.started)}
              &nbsp;·&nbsp; updated ${fmtTime(meta.updated)}</span>
        <span><a class="viewbtn" href="/view/${esc(run)}/baseline"
                 target="_blank">baseline viewer</a></span>
        <span class="muted">frac
          <input class="flt" type="number" step="0.05" min="0" max="1"
                 placeholder="min" value="${f.min}"
                 onchange="setFilter('${esc(run)}','min',this.value)">
          –
          <input class="flt" type="number" step="0.05" min="0" max="1"
                 placeholder="max" value="${f.max}"
                 onchange="setFilter('${esc(run)}','max',this.value)"></span>
      </div>
      <h3>fitness map</h3><canvas class="chart" id="chart-${esc(run)}"></canvas>
      <h3>content preserver</h3>
      ${preserverSummary(cands)}
      <canvas class="chart" id="pres-${esc(run)}"></canvas>
      <h3>OSRS matcher</h3>
      ${matcherSummary(doc, cands)}
      <canvas class="chart" id="match-${esc(run)}"></canvas>`;
    if (top.length) {
      h += `<h3>best so far</h3><div class="row">` +
           top.map(c => card(run, c, best)).join('') + `</div>`;
    }
    h += `<h3>recent</h3><div class="row">` +
         cands.slice(-8).reverse().map(c => card(run, c, best)).join('') +
         `</div>`;
  }
  document.getElementById('root').innerHTML = h;
  for (const [run, doc] of visibleRuns())
    if (doc) {
      const cands = (doc.candidates || []).filter(c => inRange(run, c));
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
    <div class="muted">${paramline(c)}</div>
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
  else closeModal();
});

async function tick() {
  try {
    DATA = await (await fetch('/data')).json();
    await refreshFavs();
    // don't repaint under an open detail view or while a filter is being typed
    const ae = document.activeElement;
    if (document.getElementById('modal').style.display !== 'block' &&
        !(ae && ae.classList && ae.classList.contains('flt'))) render();
  } catch (e) {}
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
<canvas id="view" width="720" height="720"></canvas>
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
            return self.send(200, "text/html; charset=utf-8", PAGE.encode())
        if path == "/data":
            out = {}
            for name, d in RUNS.items():
                try:
                    with open(os.path.join(d, "results.json"), "r",
                              encoding="utf-8") as f:
                        out[name] = json.load(f)
                except (OSError, ValueError):
                    out[name] = None
                if out[name] is not None:
                    out[name]["_meta"] = run_meta(name, out[name])
                    stamp_candidates(name, out[name])
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
        m = re.match(r"^/wire/([^/]+)/(.+)\.(model|anim)$", path)
        if m and m.group(1) in RUNS and safe_tag(m.group(2)):
            run, name, kind = m.group(1), m.group(2), m.group(3)
            try:
                if kind == "model":
                    out, err = build_wire(run, tag=name)
                else:
                    out, err = build_wire(run, seq=name)
            except (OSError, ValueError) as e:
                out, err = None, str(e)
            if not out:
                return self.send(500, "text/plain; charset=utf-8",
                                 ("wire build failed: %s" % err).encode())
            with open(out, "rb") as f:
                return self.send(200, "application/octet-stream", f.read())
        self.send(404, "text/plain", b"not found")

    def do_POST(self):
        path = urlparse(unquote(self.path)).path
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
