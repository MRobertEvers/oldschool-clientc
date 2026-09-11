#!/usr/bin/env python3
"""
rs2012 backport audit — animation-aware painter-vs-zbuffer audit with a live web UI.

A 2012-era model was authored against a z-buffered renderer; this engine is a
face-sorting painter. The gap between the two shows up as a small set of
recurring artifacts, and every one of them is worse under animation than at
bind pose, because the priorities baked into the file were chosen (by Jagex's
tools or by ours) against ONE pose and the animation visits many.

This driver takes a model (one or more .ob3 parts, merged the way an npc's
model1/model2 are) plus a list of sequences, and grinds through every sampled
frame measuring:

  * depth-sort violations  - pixels where the painter shows a surface that is
                             genuinely behind the z-buffer's answer, per frame,
                             per priority candidate (rs2012_model_view --score)
  * sort flicker           - pixels that change between consecutive frames in
                             the painter render while the z-buffer render is
                             stable there: the SD analog of z-fighting
  * coverage cracks        - silhouette pixels covered by one render and not
                             the other (nonzero only when candidate geometry
                             was fuzzed, or when something is actually broken)
  * structure clipping     - violations attributed to (bone group over bone
                             group) pairs via the scorer's per-pixel face-id
                             dumps; pairs absent at bind pose are the
                             "clipping that doesn't normally happen"
  * depth-table demand     - per-frame bounding reach vs the 1,500-level
                             reference depth sort and the 16,384-level table
  * scratch-limit demand   - vertex/face counts vs the scene scratch tiers
  * translucency exposure  - alpha faces are order-dependent BY DESIGN in both
                             pipelines; their count bounds the error floor
  * order-check residual   - depth-tested render vs itself with face order
                             reversed (grades the z-buffer path itself)

It also (optionally) runs the rs2012_face_priorities fast search with a pose
corpus sampled from the same sequences, so the adopted bands are judged against
animation and not just the bind pose, then re-scores the search output as one
more candidate.

Everything runs progressively: after every unit of work the tool rewrites
status.json and drops screenshots, and a small built-in HTTP server feeds
ui/index.html, which polls and live-updates. The tool is expected to be slow;
the UI is how you watch it be slow.

Usage (QBD, everything default):

    python tools/rs2012_backport_audit/audit.py --preset qbd --open

Custom:

    python tools/rs2012_backport_audit/audit.py \
        --model path/to/a.ob3 --model path/to/b.ob3 \
        --seq rs2012_seq_16715 --seq 16714 \
        --candidate authored=path/a2.ob3,path/b2.ob3 \
        --out build/backport_audit/mymodel --port 8151 --open

Requires the patched rs2012_model_view (make -C src rs2012-model-view) and,
for the search stage, rs2012-face-priorities. Pillow is optional but strongly
recommended (PNG shots, flicker/crack metrics); without it the UI serves BMPs
and skips the pixel diffs.
"""

import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.parse
import webbrowser
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler

try:
    from PIL import Image, ImageChops
except ImportError:  # degraded but functional
    Image = None
    ImageChops = None

TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(TOOL_DIR))

# Engine constants the findings are judged against (3rd/toridraw/toridraw.c).
DEPTH_LEVELS_REFERENCE = 1500
DEPTH_LEVELS_16K = 16384
# Keep the far side of the model comfortably inside the 16K table: faces whose
# projected depth lands outside [0, depth_levels) are silently never drawn.
DEPTH_BUDGET = 15000
# Scene scratch tiers (vertices, faces) from toridraw.c resolve_caps.
SCRATCH_TIERS = [("LOW_2K", 2048, 4096), ("MED_4K", 4096, 8192), ("HIGH_8K", 8192, 16384)]
PROJ_SCALE = 512  # TORIDRAW_PROJECTION_SCALE_DEFAULT
BG_HEX = 0x202430

RE_VIEW_SCORE = re.compile(
    r"view score: view=(\d+) yaw=(\d+) pitch=(\d+) wrong=(\d+) covered=(\d+)")
RE_SORT_SCORE = re.compile(
    r"sort score: (\d+)/(\d+) pixels behind the true surface \(([\d.]+)%\)")
RE_DEPTH_TEST = re.compile(
    r"depth test: (\d+) pixels changed, (\d+) of them on sort-wrong pixels")
RE_ORDER_CHECK = re.compile(
    r"order check: (\d+)/(\d+) pixels differ")
RE_POSE_BIND = re.compile(
    r"pose stats: bind x (-?\d+)\.\.(-?\d+) y (-?\d+)\.\.(-?\d+) z (-?\d+)\.\.(-?\d+)")
RE_POSE_POSED = re.compile(
    r"pose stats: posed x (-?\d+)\.\.(-?\d+) y (-?\d+)\.\.(-?\d+) z (-?\d+)\.\.(-?\d+)"
    r" moved (\d+)/(\d+)")
RE_POSE_RADIUS = re.compile(r"pose stats: radius (\d+) depth_levels_needed (\d+)")
RE_CULLED = re.compile(r"yaw (\d+) culled \(code (\d+)\)")
RE_MODEL_STATS = re.compile(
    r"model: (\d+) vertices, (\d+) faces, y (-?\d+)\.\.(-?\d+), textured (\d+), alpha (\d+), "
    r"per-face priorities (yes|no) \(model_priority (\d+)\)")
RE_PRIO_HIST = re.compile(r"priority histogram:((?: \d+=\d+)+)")
RE_PRIO_TABLE_ROW = re.compile(
    r"^(.{1,30}?)\s+(\d+)\s+([\d.]+)%\s+(\d+|-)\s*(<- chosen)?\s*$")


# --------------------------------------------------------------------- status


class Status:
    """The single JSON document the UI polls. Every mutation rewrites the file
    atomically so a half-written poll can never happen."""

    def __init__(self, out_dir):
        self.path = os.path.join(out_dir, "status.json")
        self.lock = threading.Lock()
        self.doc = {
            "meta": {
                "started": time.time(),
                "updated": time.time(),
                "stage": "starting",
                "stage_label": "Starting",
                "progress": {"done": 0, "total": 0},
                "finished": False,
                "error": None,
            },
            "model_card": None,
            "candidates": [],
            "static": {},
            "seqs": {},
            "seq_order": [],
            "attribution": None,
            "search": None,
            "findings": [],
            "verdict": None,
            "log": [],
        }
        self.update(lambda doc: None)  # the UI polls from the first second

    def update(self, fn):
        with self.lock:
            fn(self.doc)
            self.doc["meta"]["updated"] = time.time()
            tmp = self.path + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(self.doc, f)
            # On Windows the rename is denied while the HTTP thread has the
            # old file open for a poll read; the window is microseconds, so
            # retry briefly and, in the worst case, drop this tick — the next
            # update carries the same document forward.
            for _ in range(50):
                try:
                    os.replace(tmp, self.path)
                    break
                except PermissionError:
                    time.sleep(0.02)
            else:
                try:
                    os.remove(tmp)
                except OSError:
                    pass

    def log(self, message):
        line = {"t": time.time(), "m": message}
        print("audit: " + message, flush=True)

        def apply(doc):
            doc["log"].append(line)
            del doc["log"][:-80]

        self.update(apply)

    def stage(self, stage, label, total=0):
        def apply(doc):
            doc["meta"]["stage"] = stage
            doc["meta"]["stage_label"] = label
            doc["meta"]["progress"] = {"done": 0, "total": total}

        self.update(apply)
        self.log(label)

    def tick(self, done=None):
        def apply(doc):
            p = doc["meta"]["progress"]
            p["done"] = p["done"] + 1 if done is None else done

        self.update(apply)


# ------------------------------------------------------------------- helpers


def run_tool(status, args, timeout=600):
    """Run a native tool, return (stdout+stderr, returncode). The tools print
    scores on stdout and cull warnings on stderr; both matter."""
    proc = subprocess.run(
        args,
        cwd=REPO_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
    )
    if proc.returncode != 0:
        status.log("tool exited %d: %s" % (proc.returncode, " ".join(args[:4])))
    return proc.stdout or "", proc.returncode


def parse_seq_config(path, wanted):
    """rs2012.seq is a plain text config: [name] sections with frame=packed,len
    lines. Returns {name: [(packed_id, delay), ...]} for the wanted names."""
    sections = {}
    current = None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                current = line[1:-1]
                sections.setdefault(current, [])
            elif current and line.startswith("frame="):
                body = line[len("frame="):]
                parts = body.split(",")
                try:
                    packed = int(parts[0])
                    delay = int(parts[1]) if len(parts) > 1 else 1
                except ValueError:
                    continue
                sections[current].append((packed, delay))
    out = {}
    for name in wanted:
        if name not in sections:
            raise SystemExit("audit: sequence %r not in %s (has %d sections)"
                             % (name, path, len(sections)))
        out[name] = sections[name]
    return out


def sample_frames(frames, cap):
    """Even spread including first and last; every frame when they fit."""
    if cap < 2 or len(frames) <= cap:
        return list(range(len(frames)))[:max(1, cap)] if cap < 2 else \
            list(range(len(frames)))
    idx = [round(i * (len(frames) - 1) / (cap - 1)) for i in range(cap)]
    return sorted(set(idx))


def bbox_union(boxes):
    mins = [min(b[a][0] for b in boxes) for a in range(3)]
    maxs = [max(b[a][1] for b in boxes) for a in range(3)]
    return [(mins[a], maxs[a]) for a in range(3)]


def bbox_center_radius(box):
    center = [(box[a][0] + box[a][1]) // 2 for a in range(3)]
    half = [(box[a][1] - box[a][0]) / 2.0 for a in range(3)]
    radius = int(math.ceil(math.sqrt(sum(h * h for h in half))))
    return center, radius


def solve_framing(radius, tile, status, label):
    """Pick a zoom that keeps the model in frame AND the far side of it inside
    the depth table. screen = world*scale/z, so the distance that makes the
    radius fill 90% of a half-tile is r*scale/(0.45*tile*zoom); the depth sort
    needs distance+radius < DEPTH_BUDGET. Returns (zoom, distance, finding)."""
    zoom = 1.0
    distance = radius * PROJ_SCALE / (0.45 * tile * zoom)
    finding = None
    if distance + radius > DEPTH_BUDGET:
        if radius >= DEPTH_BUDGET - 200:
            finding = ("depth-table", "critical",
                       "%s: posed reach %d cannot fit the %d-level depth table at any "
                       "framing; faces WILL be silently dropped" % (label, radius, DEPTH_LEVELS_16K))
            zoom = 4.0
        else:
            zoom = radius * PROJ_SCALE / (0.45 * tile * (DEPTH_BUDGET - radius))
            if zoom > 4.0:
                zoom = 4.0
                finding = ("depth-table", "warn",
                           "%s: framing cropped (zoom %.2f capped) to keep depth in table"
                           % (label, zoom))
        distance = radius * PROJ_SCALE / (0.45 * tile * zoom)
    return zoom, distance, finding


def load_bmp_rgb(path):
    if Image is None:
        return None
    return Image.open(path).convert("RGB")


def save_shot(bmp_path, out_dir, rel_name, thumb_width=420):
    """Convert a tool BMP into shots/<rel_name>.png (+ .thumb.png) and delete
    the BMP. Without Pillow, move the BMP as-is. Returns (full_rel, thumb_rel)."""
    shots = os.path.join(out_dir, "shots")
    os.makedirs(shots, exist_ok=True)
    if Image is None:
        dst = os.path.join(shots, rel_name + ".bmp")
        shutil.move(bmp_path, dst)
        rel = "shots/" + rel_name + ".bmp"
        return rel, rel
    img = Image.open(bmp_path)
    img.load()  # decode fully so PIL releases the file handle before the remove
    full = os.path.join(shots, rel_name + ".png")
    img.save(full)
    th = img.copy()
    if th.width > thumb_width:
        th = th.resize((thumb_width, max(1, round(th.height * thumb_width / th.width))))
    thumb = os.path.join(shots, rel_name + ".thumb.png")
    th.save(thumb)
    img.close()
    th.close()
    os.remove(bmp_path)
    return "shots/" + rel_name + ".png", "shots/" + rel_name + ".thumb.png"


def _changed_mask(a, b):
    """255 where two RGB images differ, 0 elsewhere (single-band)."""
    return ImageChops.difference(a, b).convert("L").point(
        lambda v: 255 if v else 0)


def _count_on(mask):
    return mask.histogram()[255]


def _coverage_mask(img, bg_rgb):
    bg = Image.new("RGB", img.size, bg_rgb)
    return _changed_mask(img, bg)


def diff_metrics(prev_painter, painter, prev_zbuf, zbuf, bg_rgb):
    """Flicker: pixels where the painter changed between consecutive frames but
    the z-buffer reference did not — geometry stable there, sort unstable.
    Returns (flicker, stable_covered). All mask math stays inside Pillow's C
    ops; a Python per-pixel loop here costs seconds per frame."""
    if Image is None or prev_painter is None:
        return None
    painter_changed = _changed_mask(prev_painter, painter)
    zbuf_stable = ImageChops.invert(_changed_mask(prev_zbuf, zbuf))
    covered = _coverage_mask(zbuf, bg_rgb)
    stable_covered_mask = ImageChops.multiply(zbuf_stable, covered)
    flicker_mask = ImageChops.multiply(painter_changed, stable_covered_mask)
    return _count_on(flicker_mask), _count_on(stable_covered_mask)


def coverage_cracks(painter, zbuf, bg_rgb):
    """Pixels one render covers and the other does not. The kernels share span
    rules, so on identical geometry this is ~0; it exists to catch fuzzed
    candidates opening seams (and genuine coverage bugs)."""
    if Image is None:
        return None
    pc = _coverage_mask(painter, bg_rgb)
    zc = _coverage_mask(zbuf, bg_rgb)
    union = ImageChops.lighter(pc, zc)
    mismatch = ImageChops.difference(pc, zc)
    return _count_on(mismatch), _count_on(union)


def read_id_dump(prefix_path, views):
    """Read the viewer's .vNN.idz dumps. IDZ1: 4 planes; IDZ2 adds the
    reversed-order painter ids (the stability plane)."""
    import struct
    out = []
    for v in range(views):
        path = "%s.v%02d.idz" % (prefix_path, v)
        if not os.path.exists(path):
            continue
        with open(path, "rb") as f:
            magic, tile, yaw, pitch = struct.unpack("<4i", f.read(16))
            if magic not in (0x315A4449, 0x325A4449):
                continue
            count = 5 if magic == 0x325A4449 else 4
            n = tile * tile
            planes = []
            for _ in range(count):
                planes.append(struct.unpack("<%di" % n, f.read(4 * n)))
            out.append({"tile": tile, "yaw": yaw, "pitch": pitch,
                        "id_painter": planes[0], "z_painter": planes[1],
                        "id_zbuf": planes[2], "z_zbuf": planes[3],
                        "id_rev": planes[4] if count == 5 else None})
    return out


def read_labels(path):
    """The viewer's --labels-out file: face->group, face->(a,b,c) topology,
    per-face alpha, and the structure table (per rig group: counts, priority
    bands, bind bbox)."""
    face_group = {}
    face_topo = {}
    face_alpha = {}
    groups = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("G,"):
                p = line.strip().split(",")
                if len(p) >= 17:
                    groups[int(p[1])] = {
                        "id": int(p[1]), "verts": int(p[2]), "faces": int(p[3]),
                        "alpha_faces": int(p[4]), "band_lo": int(p[5]),
                        "band_hi": int(p[6]), "band_major": int(p[7]),
                        "centroid": [int(p[8]), int(p[9]), int(p[10])],
                        "bbox": [[int(p[11]), int(p[14])], [int(p[12]), int(p[15])],
                                 [int(p[13]), int(p[16])]],
                    }
                continue
            if line.startswith("#"):
                continue
            p = line.strip().split(",")
            if len(p) >= 2:
                face_group[int(p[0])] = int(p[1])
            if len(p) >= 8:
                face_alpha[int(p[0])] = int(p[3])
                face_topo[int(p[0])] = (int(p[5]), int(p[6]), int(p[7]))
    return face_group, groups, face_topo, face_alpha


def build_patches(face_group, face_topo):
    """Split each rig group's faces into connected patches (faces joined by
    shared vertices). A rig group is often several shells stacked on top of
    each other — an armour plate over the body it covers — and the patch is
    the unit a z-fighting separation can actually move. Returns
    (face->patch_name, patch_name->face list, patch_name->group)."""
    parent = {}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    vertex_face = {}
    for face, (a, b, c) in face_topo.items():
        parent[face] = face
        g = face_group.get(face, -1)
        for v in (a, b, c):
            key = (g, v)
            if key in vertex_face:
                union(face, vertex_face[key])
            else:
                vertex_face[key] = face

    roots = {}
    face_patch = {}
    patch_faces = {}
    patch_group = {}
    for face in face_topo:
        root = find(face)
        g = face_group.get(face, -1)
        if root not in roots:
            roots[root] = "g%d#%d" % (g, sum(1 for r, n in roots.items()
                                             if n.startswith("g%d#" % g)))
        name = roots[root]
        face_patch[face] = name
        patch_faces.setdefault(name, []).append(face)
        patch_group[name] = g
    return face_patch, patch_faces, patch_group


TIE_SLACK = 2  # raster units; within this two faces are fighting, not layered


def pair_matrices(dumps, mapping, collect_faces=False):
    """Two aggregations off one id/z walk, keyed by whatever `mapping` maps a
    face to (rig group for reporting, patch for repair).

    violations: (painter side OVER z-buffer side), ordered — the scorer's own
    predicate (painter >TIE_SLACK behind), i.e. wrong layering.
    ties: unordered pairs whose contested pixels sit within TIE_SLACK of each
    other — no stable order exists there; this is where z-fighting lives.

    With collect_faces, also returns per tie pair the actual face sets doing
    the fighting, split into (true-front faces, painter-winner faces), AND the
    per-pixel (front, behind) face pairs — the conflict edges a bipartition
    needs to pull two interleaved copies apart.
    """
    violations = {}
    ties = {}
    tie_faces = {}
    tie_pairs = {}
    for d in dumps:
        ip, zp = d["id_painter"], d["z_painter"]
        iz, zz = d["id_zbuf"], d["z_zbuf"]
        rev = d.get("id_rev")
        for i in range(len(ip)):
            f = ip[i]
            if f < 0 or f == iz[i]:
                continue
            ga = mapping.get(f, -1)
            gb = mapping.get(iz[i], -1)
            dz = zp[i] - zz[i]
            if dz > TIE_SLACK:
                violations[(ga, gb)] = violations.get((ga, gb), 0) + 1
            elif -TIE_SLACK <= dz <= TIE_SLACK:
                # a contested pixel whose winner survives an order reversal
                # has a stable owner — that is layering, not fighting. Only
                # the UNSTABLE ones count as ties (a pin's whole job is to
                # turn unstable into stable, and the metric must see it).
                if rev is not None and rev[i] == f:
                    continue
                key = (ga, gb) if str(ga) <= str(gb) else (gb, ga)
                ties[key] = ties.get(key, 0) + 1
                if collect_faces:
                    front, behind = tie_faces.setdefault(key, ({}, {}))
                    front[iz[i]] = front.get(iz[i], 0) + 1
                    behind[f] = behind.get(f, 0) + 1
                    edges = tie_pairs.setdefault(key, set())
                    if len(edges) < 6000:
                        edges.add((iz[i], f))
    if collect_faces:
        return violations, ties, tie_faces, tie_pairs
    return violations, ties


def pin_cover(face_counts, cover=0.8, cap=600):
    """The smallest face set covering `cover` of the tie pixels, heaviest
    faces first. A pin's collateral damage scales with how many faces it
    drags into a band, so pin the few that carry the fighting, not the whole
    side."""
    total = sum(face_counts.values())
    if not total:
        return []
    picked = []
    got = 0
    for face, n in sorted(face_counts.items(), key=lambda kv: (-kv[1], kv[0])):
        picked.append(face)
        got += n
        if got >= cover * total or len(picked) >= cap:
            break
    return picked


def bipartition_conflicts(edges):
    """2-color the conflict graph: nodes are faces, an edge means the two
    faces contested the same pixel at the same depth. Two interleaved copies
    of one shell come out as the two colors. BFS from sorted nodes, so the
    coloring is deterministic; odd cycles just fall where the BFS puts them.
    Returns (side0, side1, front_votes) where front_votes counts how often
    each side was the z-buffer's answer — the side the pin should prefer."""
    adj = {}
    for a, b in edges:
        adj.setdefault(a, set()).add(b)
        adj.setdefault(b, set()).add(a)
    color = {}
    for start in sorted(adj):
        if start in color:
            continue
        color[start] = 0
        queue = [start]
        while queue:
            node = queue.pop()
            for nxt in sorted(adj[node]):
                if nxt not in color:
                    color[nxt] = 1 - color[node]
                    queue.append(nxt)
    votes = [0, 0]
    for a, _ in edges:
        if a in color:
            votes[color[a]] += 1
    side0 = sorted(f for f, c in color.items() if c == 0)
    side1 = sorted(f for f, c in color.items() if c == 1)
    return side0, side1, votes


def tie_mask_png(dumps, out_path):
    """A picture of WHERE the fighting is: contested pixels in magenta over the
    dim coverage, one panel per view, so 'group 94 vs 95' has a location."""
    if Image is None or not dumps:
        return False
    tile = dumps[0]["tile"]
    img = Image.new("RGB", (tile * len(dumps), tile), (13, 13, 13))
    px = img.load()
    for v, d in enumerate(dumps):
        ip, zp = d["id_painter"], d["z_painter"]
        iz, zz = d["id_zbuf"], d["z_zbuf"]
        rev = d.get("id_rev")
        for i in range(len(ip)):
            f = ip[i]
            if f < 0:
                continue
            x = v * tile + (i % tile)
            y = i // tile
            dz = zp[i] - zz[i]
            if f != iz[i] and -TIE_SLACK <= dz <= TIE_SLACK and \
                    (rev is None or rev[i] != f):
                px[x, y] = (233, 90, 200)   # unstable tie: z-fighting
            elif f != iz[i] and dz > TIE_SLACK:
                px[x, y] = (208, 59, 59)    # wrong layering
            else:
                px[x, y] = (44, 44, 42)
    img.save(out_path)
    img.close()
    return True


def bbox_overlap(a, b, pad):
    """Overlap volume of two [axis][min,max] boxes inflated by pad, else 0."""
    vol = 1
    for axis in range(3):
        lo = max(a[axis][0] - pad, b[axis][0] - pad)
        hi = min(a[axis][1] + pad, b[axis][1] + pad)
        if hi <= lo:
            return 0
        vol *= (hi - lo)
    return vol


# ------------------------------------------------------------------- live


class LivePool:
    """One persistent `--repl` viewer process per candidate, feeding the web
    UI's orbitable live view. The model set loads once per process, then each
    orbit/scrub/toggle request is a ~30ms re-pose + raster. Spawned lazily,
    serialized per candidate, kept alive after the run so the finished report
    stays inspectable."""

    def __init__(self, audit):
        self.audit = audit
        self.procs = {}
        self.spawn_lock = threading.Lock()
        self.tile = 384
        self.cfg = None
        self.tmp_dir = os.path.join(audit.out_dir, "live_tmp")
        os.makedirs(self.tmp_dir, exist_ok=True)

    def configure(self):
        """One camera that fits every pose of every sequence, so toggling
        candidates or scrubbing frames never reframes."""
        boxes = []
        card = getattr(self.audit, "card", None)
        if card and card.get("bind_bbox"):
            boxes.append([tuple(b) for b in card["bind_bbox"]])
        frames = []
        for name in self.audit.status.doc["seq_order"]:
            plan = self.audit.seq_plan.get(name)
            if not plan:
                continue
            for rec in plan["frames"]:
                if rec.get("bbox"):
                    boxes.append([tuple(b) for b in rec["bbox"]])
                if rec["decode"] == "ok":
                    frames.append({"seq": name, "index": rec["index"],
                                   "packed": rec["packed"]})
        if not boxes:
            return None
        union = bbox_union(boxes)
        focus, radius = bbox_center_radius(union)
        zoom, _, _ = solve_framing(radius, self.tile, None, "live")
        self.cfg = {"focus": focus, "radius": radius, "zoom": round(zoom, 3),
                    "frames": frames, "tile": self.tile}
        return self.cfg

    def _spawn(self, cand):
        spec = self.audit.candidates[cand]
        args = [self.audit.o.viewer]
        for m in spec["models"]:
            args += ["--model", m]
        args += spec["flags"]
        if self.cfg["frames"]:
            args += ["--cache", self.audit.o.cache,
                     "--frames", ",".join(str(f["packed"])
                                          for f in self.cfg["frames"])]
        args += ["--focus", "%d,%d,%d" % tuple(self.cfg["focus"]),
                 "--radius", str(self.cfg["radius"]),
                 "--near", "49", "--tile", str(self.tile),
                 "--bg", "%06x" % BG_HEX, "--repl"]
        proc = subprocess.Popen(args, cwd=REPO_ROOT, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, text=True)
        greeting = proc.stdout.readline()
        if not greeting.startswith("repl ready"):
            proc.kill()
            raise RuntimeError("live viewer failed to start for %s" % cand)
        return {"proc": proc, "lock": threading.Lock()}

    def render(self, cand, pose, yaw, pitch, zoom, mode):
        """PNG bytes for one live request, or None."""
        if self.cfg is None or cand not in self.audit.candidates:
            return None
        with self.spawn_lock:
            entry = self.procs.get(cand)
            if entry is None or entry["proc"].poll() is not None:
                entry = self._spawn(cand)
                self.procs[cand] = entry
        out = os.path.join(self.tmp_dir, "%s.bmp" % cand)
        cmd = "render pose=%d yaw=%d pitch=%d zoom=%.3f mode=%s out=%s\n" % (
            pose, yaw & 2047, pitch & 2047, zoom, mode, out)
        with entry["lock"]:
            proc = entry["proc"]
            try:
                proc.stdin.write(cmd)
                proc.stdin.flush()
                reply = proc.stdout.readline()
            except OSError:
                reply = ""
            if not reply.startswith("ok"):
                with self.spawn_lock:
                    proc.kill()
                    self.procs.pop(cand, None)
                return None
            # Served as BMP on purpose: browsers render it natively, localhost
            # bandwidth is free, and a PNG re-encode was most of the latency —
            # dropping it took an orbit step from ~80ms to ~35ms.
            with open(out, "rb") as f:
                return ("bmp", f.read())

    def prewarm(self):
        """Spawn every current candidate's viewer in the background so the
        first click on a candidate button is a ~35ms render, not a multi-second
        model+animation cold load."""
        if self.cfg is None:
            return

        def warm():
            for cand in list(self.audit.candidates):
                try:
                    self.render(cand, -1, 420, 140, self.cfg["zoom"], "painter")
                except Exception:
                    pass

        threading.Thread(target=warm, daemon=True).start()

    def shutdown(self):
        for entry in self.procs.values():
            try:
                entry["proc"].stdin.write("quit\n")
                entry["proc"].stdin.flush()
            except OSError:
                pass
            entry["proc"].terminate()
        self.procs.clear()


# ---------------------------------------------------------------- the audit


class Audit:
    def __init__(self, opts):
        self.o = opts
        self.out_dir = opts.out
        os.makedirs(self.out_dir, exist_ok=True)
        os.makedirs(os.path.join(self.out_dir, "shots"), exist_ok=True)
        os.makedirs(os.path.join(self.out_dir, "work"), exist_ok=True)
        self.status = Status(self.out_dir)
        self.bg_rgb = ((BG_HEX >> 16) & 255, (BG_HEX >> 8) & 255, BG_HEX & 255)
        self.findings = {}
        # candidates: name -> {"models": [paths], "flags": [extra viewer args]}
        self.candidates = {"inherited": {"models": opts.model, "flags": []},
                           "stripped": {"models": opts.model,
                                        "flags": ["--ignore-priorities"]}}
        for spec in opts.candidate:
            name, _, paths = spec.partition("=")
            self.candidates[name] = {"models": paths.split(","), "flags": []}
        self.seq_frames = {}   # name -> [(packed, delay)]
        self.seq_plan = {}     # name -> plan dict (focus, radius, zoom, ...)
        self.face_group = {}   # face -> rig group (from --labels-out)
        self.face_topo = {}    # face -> (a, b, c) vertex indices
        self.face_alpha = {}
        self.face_patch = {}   # face -> patch name ("g75#2")
        self.patch_faces = {}  # patch name -> [faces]
        self.patch_group = {}
        self.groups = {}       # group id -> structure record
        self.part_vertex_offsets = [0]
        self.part_face_offsets = [0]
        self.labels_path = os.path.join(self.out_dir, "work", "labels.csv")
        self.attr_baseline = None  # set by attribution, consumed by repair
        self.live = LivePool(self)

    # -- findings are keyed so a repeat observation updates instead of spams
    def find(self, key, severity, title, detail):
        self.findings[key] = {"key": key, "severity": severity,
                              "title": title, "detail": detail}
        ranked = sorted(self.findings.values(),
                        key=lambda x: {"critical": 0, "warn": 1, "info": 2}[x["severity"]])

        def apply(doc):
            doc["findings"] = ranked

        self.status.update(apply)

    def viewer_args(self, cand, extra):
        args = [self.o.viewer]
        for m in self.candidates[cand]["models"]:
            args += ["--model", m]
        args += self.candidates[cand]["flags"]
        args += extra
        return args

    # ---------------------------------------------------------------- stages

    def stage_decode(self):
        self.status.stage("decode", "Decoding model + sequences", 1)
        out, rc = run_tool(self.status, self.viewer_args(
            "inherited", ["--stats", "--pose-stats-only",
                          "--labels-out", self.labels_path]))
        if rc != 0:
            raise SystemExit("audit: viewer failed on the model: " + out[-400:])
        card = {"models": [os.path.relpath(m, REPO_ROOT) for m in self.o.model]}
        m = RE_MODEL_STATS.search(out)
        if m:
            card.update(vertices=int(m.group(1)), faces=int(m.group(2)),
                        textured=int(m.group(5)), alpha_faces=int(m.group(6)),
                        has_priorities=m.group(7) == "yes",
                        model_priority=int(m.group(8)))
        h = RE_PRIO_HIST.search(out)
        if h:
            card["priority_histogram"] = {
                k: int(v) for k, v in
                (p.split("=") for p in h.group(1).split())}
        b = RE_POSE_BIND.search(out)
        if b:
            g = [int(x) for x in b.groups()]
            card["bind_bbox"] = [[g[0], g[1]], [g[2], g[3]], [g[4], g[5]]]
        r = RE_POSE_RADIUS.search(out)
        if r:
            card["bind_radius"] = int(r.group(1))
            card["bind_depth_needed"] = int(r.group(2))

        # scratch tier + depth table demands are static findings
        if "vertices" in card:
            tier = next((t for t in SCRATCH_TIERS
                         if card["vertices"] <= t[1] and card["faces"] <= t[2]), None)
            card["scratch_tier"] = tier[0] if tier else "OVER"
            if tier is None:
                self.find("scratch", "critical", "Model exceeds every scratch tier",
                          "%d vertices / %d faces exceed HIGH_8K (8192/16384)."
                          % (card["vertices"], card["faces"]))
            elif tier[0] == "HIGH_8K":
                self.find("scratch", "info", "Needs HIGH_8K scratch tier",
                          "%d vertices / %d faces fit only the largest scratch "
                          "profile." % (card["vertices"], card["faces"]))
        if card.get("bind_depth_needed", 0) > DEPTH_LEVELS_REFERENCE:
            self.find("depth-ref", "warn",
                      "Exceeds the 1,500-level reference depth sort at bind",
                      "Bind pose needs %d levels; the scene must run "
                      "TORIDRAW_SCENE_DEPTH_16K or faces drop silently."
                      % card["bind_depth_needed"])
        if card.get("alpha_faces"):
            self.find("alpha", "info",
                      "%d translucent faces" % card["alpha_faces"],
                      "Alpha faces composite in sort order in BOTH pipelines; "
                      "they set the floor for the order-check residual and can "
                      "never be fixed by priorities alone.")
        if card.get("textured"):
            self.find("textures", "info", "%d textured faces" % card["textured"],
                      "The depth-tested texture path divides per pixel and has "
                      "no affine twin; textured faces are the slowest to depth-"
                      "test and shimmer differently between the two pipelines.")

        seq_names = list(self.seq_frames)
        card["seqs"] = {n: len(self.seq_frames[n]) for n in seq_names}
        if os.path.exists(self.labels_path):
            (self.face_group, self.groups,
             self.face_topo, self.face_alpha) = read_labels(self.labels_path)
            card["rig_groups"] = len(self.groups)
            if self.face_topo:
                (self.face_patch, self.patch_faces,
                 self.patch_group) = build_patches(self.face_group, self.face_topo)
                card["patches"] = len(self.patch_faces)

        # merged->part index mapping: the merge concatenates parts in order, so
        # per-part counts are enough to send part-local face/vertex lists to
        # the nudge tool.
        self.part_vertex_offsets = [0]
        self.part_face_offsets = [0]
        for path in self.o.model:
            pout, _ = run_tool(self.status, [
                self.o.viewer, "--model", path, "--stats", "--pose-stats-only"])
            pm = RE_MODEL_STATS.search(pout)
            if pm:
                self.part_vertex_offsets.append(
                    self.part_vertex_offsets[-1] + int(pm.group(1)))
                self.part_face_offsets.append(
                    self.part_face_offsets[-1] + int(pm.group(2)))

        def apply(doc):
            doc["model_card"] = card
            doc["candidates"] = list(self.candidates)
            doc["seq_order"] = seq_names

        self.status.update(apply)
        self.card = card

    def stage_structures(self):
        """Suspects from the rig and the shipped priorities alone, before a
        single sweep render: two structures whose bind boxes overlap while
        their priority bands tie are where z-fighting will live; overlapping
        structures pinned to DIFFERENT bands are where the priority forces a
        wrong order. Measurement later confirms or clears them — this ranks
        where to look."""
        if not self.groups:
            self.status.log("structures skipped: no rig groups")
            return
        self.status.stage("structures", "Analyzing rig structures + priorities", 1)
        has_prio = bool(self.card.get("has_priorities"))
        suspects = []
        ids = [g for g in self.groups.values() if g["faces"] >= 4]
        for i, a in enumerate(ids):
            for b in ids[i + 1:]:
                vol = bbox_overlap(a["bbox"], b["bbox"], pad=6)
                if not vol:
                    continue
                weight = (vol ** (1.0 / 3.0)) * min(a["faces"], b["faces"])
                if has_prio and a["band_major"] != b["band_major"]:
                    kind = "banded-overlap"   # priorities force an order here
                elif has_prio:
                    kind = "tie-prone"        # same band, contact: no order
                else:
                    kind = "contact"
                suspects.append({"a": a["id"], "b": b["id"], "kind": kind,
                                 "weight": round(weight, 1)})
        suspects.sort(key=lambda s: -s["weight"])
        suspects = suspects[:24]
        top_groups = sorted(self.groups.values(), key=lambda g: -g["faces"])[:30]
        result = {
            "groups": [{k: g[k] for k in
                        ("id", "verts", "faces", "alpha_faces", "band_lo",
                         "band_hi", "band_major")} for g in top_groups],
            "suspects": suspects,
        }
        self.structure_suspects = suspects

        def apply(doc):
            doc["structures"] = result

        self.status.update(apply)
        self.status.tick()
        tie_prone = sum(1 for s in suspects if s["kind"] == "tie-prone")
        banded = sum(1 for s in suspects if s["kind"] == "banded-overlap")
        if tie_prone or banded:
            self.find("structures", "info",
                      "%d structure pairs overlap at bind" % len(suspects),
                      "%d share a priority band (z-fight prone), %d are pinned "
                      "to different bands across the overlap (forced-order "
                      "prone). These seed the repair stage's suspect pool "
                      "alongside the measured tie pixels." % (tie_prone, banded))

    def stage_static(self):
        self.status.stage("static", "Static baseline (bind pose)",
                          len(self.candidates))
        work = os.path.join(self.out_dir, "work")
        for cand in self.candidates:
            base = os.path.join(work, "static_%s" % cand)
            extra = ["--angles", str(self.o.static_angles),
                     "--pitch", self.o.static_pitches,
                     "--tile", str(self.o.static_tile),
                     "--score", "--compare", "--order-check",
                     "--out", base + "_painter.bmp",
                     "--out-zbuffer", base + "_zbuf.bmp",
                     "--diff-out", base + "_diff.bmp",
                     "--score-out", base + "_mask.bmp"]
            out, rc = run_tool(self.status, self.viewer_args(cand, extra))
            entry = {"error": None if rc == 0 else "viewer exit %d" % rc}
            s = RE_SORT_SCORE.search(out)
            if s:
                entry.update(wrong=int(s.group(1)), covered=int(s.group(2)),
                             pct=float(s.group(3)))
            oc = RE_ORDER_CHECK.search(out)
            if oc and int(oc.group(2)):
                entry["order_check_pct"] = round(
                    100.0 * int(oc.group(1)) / int(oc.group(2)), 3)
            dt = RE_DEPTH_TEST.search(out)
            if dt:
                entry["depth_changed"] = int(dt.group(1))
            shots = {}
            for kind in ("painter", "zbuf", "diff", "mask"):
                bmp = "%s_%s.bmp" % (base, kind)
                if os.path.exists(bmp):
                    full, thumb = save_shot(bmp, self.out_dir,
                                            "static_%s_%s" % (cand, kind))
                    shots[kind] = {"full": full, "thumb": thumb}
            entry["shots"] = shots

            def apply(doc, cand=cand, entry=entry):
                doc["static"][cand] = entry

            self.status.update(apply)
            self.status.tick()
            if "pct" in entry:
                self.status.log("static %s: %.2f%% sort error" % (cand, entry["pct"]))
        st = self.status.doc["static"]
        oc = st.get("inherited", {}).get("order_check_pct")
        if oc is not None and oc > 3.0:
            self.find("order-check", "warn",
                      "Order-check residual %.2f%%" % oc,
                      "The depth-tested render itself is order-dependent on "
                      "more than translucency alone would explain.")

    def stage_prescan(self):
        total = sum(len(s["indices"]) for s in self.seq_plan.values())
        self.status.stage("prescan", "Prescanning frames (bounds, camera)", total)
        bind_box = self.card.get("bind_bbox")
        bind_size = None
        if bind_box:
            bind_size = math.sqrt(sum((b[1] - b[0]) ** 2 for b in bind_box))
        for name, plan in self.seq_plan.items():
            frames = self.seq_frames[name]
            boxes = []
            for idx in plan["indices"]:
                packed = frames[idx][0]
                out, rc = run_tool(self.status, self.viewer_args("inherited", [
                    "--cache", self.o.cache, "--frames", str(packed),
                    "--pose-stats-only"]))
                rec = {"index": idx, "packed": packed, "delay": frames[idx][1],
                       "decode": "ok"}
                p = RE_POSE_POSED.search(out)
                r = RE_POSE_RADIUS.search(out)
                if rc != 0 or not p:
                    rec["decode"] = "failed"
                else:
                    g = [int(x) for x in p.groups()]
                    box = [(g[0], g[1]), (g[2], g[3]), (g[4], g[5])]
                    rec["bbox"] = [list(b) for b in box]
                    rec["moved"] = g[6]
                    if r:
                        rec["depth_needed"] = int(r.group(2))
                    size = math.sqrt(sum((b[1] - b[0]) ** 2 for b in box))
                    if bind_size:
                        ratio = size / bind_size if bind_size else 1.0
                        rec["size_ratio"] = round(ratio, 3)
                        if ratio < 0.1:
                            rec["decode"] = "collapsed"
                        elif ratio > 12.0:
                            rec["decode"] = "exploded"
                    if rec["decode"] == "ok":
                        boxes.append(box)
                plan["frames"].append(rec)
                self.status.tick()
            bad = [f for f in plan["frames"] if f["decode"] != "ok"]
            if bad:
                self.find("frame-decode-%s" % name, "critical",
                          "%s: %d/%d frames decode wrong" % (
                              name, len(bad), len(plan["frames"])),
                          "Frames %s came out %s; they are excluded from "
                          "scoring but the sequence will misrender in game." % (
                              ",".join(str(f["index"]) for f in bad),
                              "/".join(sorted({f["decode"] for f in bad}))))
            if not boxes:
                plan["skip"] = True
                continue
            union = bbox_union(boxes)
            focus, radius = bbox_center_radius(union)
            plan["focus"] = focus
            plan["radius"] = radius
            depth_needed = 2 * radius + 1
            plan["depth_needed"] = depth_needed
            zoom, distance, finding = solve_framing(
                radius, self.o.tile, self.status, name)
            plan["zoom"] = round(zoom, 3)
            plan["distance"] = int(distance)
            if finding:
                self.find("framing-%s" % name, finding[1], "Framing: " + name,
                          finding[2])
            if depth_needed > DEPTH_LEVELS_16K:
                self.find("depth16k-%s" % name, "critical",
                          "%s exceeds the 16,384-level depth table" % name,
                          "Posed reach %d needs %d levels; faces will drop "
                          "silently even on the large table." % (radius, depth_needed))
            elif depth_needed > DEPTH_LEVELS_REFERENCE:
                self.find("depthref-%s" % name, "warn",
                          "%s exceeds the reference 1,500-level depth sort" % name,
                          "Posed frames need up to %d levels; this content "
                          "requires TORIDRAW_SCENE_DEPTH_16K." % depth_needed)

            def apply(doc, name=name, plan=plan):
                doc["seqs"][name] = {
                    "frames_total": len(self.seq_frames[name]),
                    "sampled": len(plan["indices"]),
                    "focus": plan.get("focus"), "radius": plan.get("radius"),
                    "zoom": plan.get("zoom"), "depth_needed": plan.get("depth_needed"),
                    "prescan": plan["frames"],
                    "per_frame": [],
                }

            self.status.update(apply)

    def render_frame(self, cand, name, plan, rec, want_ids=False):
        """One frame, one candidate: painter+zbuffer render with the sequence's
        shared camera, scored. Returns (entry, painter_img, zbuf_img)."""
        work = os.path.join(self.out_dir, "work")
        tag = "%s_f%03d_%s" % (name, rec["index"], cand)
        base = os.path.join(work, tag)
        extra = ["--cache", self.o.cache, "--frames", str(rec["packed"]),
                 "--focus", "%d,%d,%d" % tuple(plan["focus"]),
                 "--radius", str(plan["radius"]),
                 "--zoom", str(plan["zoom"]),
                 "--near", "49",
                 "--angles", str(self.o.angles),
                 "--pitch", str(self.o.pitch),
                 "--tile", str(self.o.tile),
                 "--score", "--compare",
                 "--out", base + "_painter.bmp",
                 "--out-zbuffer", base + "_zbuf.bmp",
                 "--diff-out", base + "_diff.bmp"]
        if want_ids:
            extra += ["--id-dump", base]
        out, _ = run_tool(self.status, self.viewer_args(cand, extra))
        entry = {}
        s = RE_SORT_SCORE.search(out)
        if s:
            entry = {"wrong": int(s.group(1)), "covered": int(s.group(2)),
                     "pct": float(s.group(3))}
        culled = RE_CULLED.findall(out)
        if culled:
            entry["culled_views"] = len(culled)
        painter = zbuf = None
        shots = {}
        p_bmp, z_bmp, d_bmp = (base + "_painter.bmp", base + "_zbuf.bmp",
                               base + "_diff.bmp")
        if Image is not None and os.path.exists(p_bmp) and os.path.exists(z_bmp):
            painter = load_bmp_rgb(p_bmp)
            zbuf = load_bmp_rgb(z_bmp)
        for kind, bmp in (("painter", p_bmp), ("zbuf", z_bmp), ("diff", d_bmp)):
            if os.path.exists(bmp):
                full, thumb = save_shot(bmp, self.out_dir, tag + "_" + kind)
                shots[kind] = {"full": full, "thumb": thumb}
        entry["shots"] = shots
        return entry, painter, zbuf

    def stage_sweep(self):
        cands = list(self.candidates)
        live = [(n, p) for n, p in self.seq_plan.items() if not p.get("skip")]
        total = sum(len([f for f in p["frames"] if f["decode"] == "ok"])
                    for _, p in live) * len(cands)
        self.status.stage("sweep", "Animation sweep (%d renders)" % total, total)
        for name, plan in live:
            prev = {c: (None, None) for c in cands}
            ok_frames = [f for f in plan["frames"] if f["decode"] == "ok"]
            for rec in ok_frames:
                frame_entry = {"index": rec["index"], "packed": rec["packed"],
                               "delay": rec["delay"], "cands": {}}
                for cand in cands:
                    entry, painter, zbuf = self.render_frame(cand, name, plan, rec)
                    pp, pz = prev[cand]
                    if painter is not None and pp is not None:
                        fl = diff_metrics(pp, painter, pz, zbuf, self.bg_rgb)
                        if fl and fl[1]:
                            entry["flicker_pct"] = round(100.0 * fl[0] / fl[1], 3)
                    if painter is not None:
                        cr = coverage_cracks(painter, zbuf, self.bg_rgb)
                        if cr and cr[1]:
                            entry["crack_pct"] = round(100.0 * cr[0] / cr[1], 3)
                    if pp is not None:
                        pp.close()
                    if pz is not None:
                        pz.close()
                    prev[cand] = (painter, zbuf)
                    frame_entry["cands"][cand] = entry
                    self.status.tick()

                def apply(doc, name=name, fe=frame_entry):
                    doc["seqs"][name]["per_frame"].append(fe)

                self.status.update(apply)
            for painter, zbuf in prev.values():
                if painter is not None:
                    painter.close()
                if zbuf is not None:
                    zbuf.close()
            self.summarize_seq(name)

    def summarize_seq(self, name):
        doc_seq = self.status.doc["seqs"][name]
        summary = {}
        for cand in self.candidates:
            pcts = [f["cands"][cand]["pct"] for f in doc_seq["per_frame"]
                    if "pct" in f["cands"].get(cand, {})]
            fl = [f["cands"][cand].get("flicker_pct") for f in doc_seq["per_frame"]]
            fl = [x for x in fl if x is not None]
            cr = [f["cands"][cand].get("crack_pct") for f in doc_seq["per_frame"]]
            cr = [x for x in cr if x is not None]
            if pcts:
                summary[cand] = {
                    "mean_pct": round(sum(pcts) / len(pcts), 3),
                    "max_pct": round(max(pcts), 3),
                    "worst_frame": doc_seq["per_frame"][
                        max(range(len(pcts)), key=lambda i: pcts[i])]["index"],
                    "mean_flicker_pct": round(sum(fl) / len(fl), 3) if fl else None,
                    "max_crack_pct": round(max(cr), 3) if cr else None,
                }

        def apply(doc):
            doc["seqs"][name]["summary"] = summary

        self.status.update(apply)
        inh = summary.get("inherited", {})
        if inh.get("max_pct", 0) > 2 * max(0.001, inh.get("mean_pct", 0)):
            self.find("anim-spike-%s" % name, "warn",
                      "%s: sort error spikes at frame %d" % (
                          name, inh.get("worst_frame", -1)),
                      "Max %.2f%% vs mean %.2f%% — a pose-specific violation "
                      "the bind-pose numbers never show." % (
                          inh.get("max_pct", 0), inh.get("mean_pct", 0)))
        for cand, s in summary.items():
            if s.get("max_crack_pct") and s["max_crack_pct"] > 0.2:
                self.find("cracks-%s-%s" % (name, cand), "warn",
                          "%s/%s: coverage cracks %.2f%%" % (name, cand,
                                                             s["max_crack_pct"]),
                          "Painter and z-buffer silhouettes disagree; if this "
                          "candidate carries vertex fuzz, the fuzz is opening "
                          "seams in animated poses.")

    def render_for_ids(self, models_spec, tag, frame=None, extra_flags=()):
        """One scoring render whose product is the id/z dumps: returns the
        parsed dumps (and deletes them). models_spec = (model paths, flags)."""
        work = os.path.join(self.out_dir, "work")
        base = os.path.join(work, tag)
        args = [self.o.viewer]
        for m in models_spec[0]:
            args += ["--model", m]
        args += list(models_spec[1]) + list(extra_flags)
        if frame is not None:
            name, packed = frame
            plan = self.seq_plan[name]
            args += ["--cache", self.o.cache, "--frames", str(packed),
                     "--focus", "%d,%d,%d" % tuple(plan["focus"]),
                     "--radius", str(plan["radius"]),
                     "--zoom", str(plan["zoom"]), "--near", "49"]
        args += ["--angles", str(self.o.angles), "--pitch", str(self.o.pitch),
                 "--tile", str(self.o.attr_tile), "--score",
                 "--id-dump", base, "--out", base + ".bmp"]
        run_tool(self.status, args)
        dumps = read_id_dump(base, self.o.angles)
        for leftover in [base + ".bmp"] + \
                ["%s.v%02d.idz" % (base, v) for v in range(self.o.angles)]:
            if os.path.exists(leftover):
                os.remove(leftover)
        return dumps

    def stage_attribution(self):
        """Where the artifacts actually are: worst frames re-rendered for their
        per-pixel id/z maps, joined to rig groups. Violations say which
        structure is drawn over which; ties say where two structures sit at
        the same depth with no stable order — the z-fighting sites the repair
        stage will try to separate. Both are compared against the bind pose so
        animation-only artifacts stand out."""
        live = [(n, p) for n, p in self.seq_plan.items() if not p.get("skip")]
        if not live or not self.face_group:
            self.status.log("attribution skipped: %s" %
                            ("no frames" if not live else "no rig labels"))
            return
        self.status.stage("attribution", "Attributing artifacts to structures",
                          self.o.attr_poses + 1)
        # patch granularity when topology is available: "g75#2 over g75#7" is
        # actionable (a shell over the body inside one rig group) where
        # "g75 over g75" is a shrug
        mapping = self.face_patch or self.face_group
        stripped = (self.candidates["stripped"]["models"],
                    self.candidates["stripped"]["flags"])
        bind_dumps = self.render_for_ids(stripped, "attr_bind")
        bind_viol, bind_ties, bind_tie_faces, bind_tie_pairs = pair_matrices(
            bind_dumps, mapping, collect_faces=True)
        mask_rel = None
        if tie_mask_png(bind_dumps, os.path.join(self.out_dir, "work", "bind_ties.bmp")):
            mask_rel, _ = save_shot(os.path.join(self.out_dir, "work", "bind_ties.bmp"),
                                    self.out_dir, "attr_bind_ties")
        self.status.tick()

        scored = []
        for name, plan in live:
            for fe in self.status.doc["seqs"][name]["per_frame"]:
                pct = fe["cands"].get("stripped", {}).get("pct")
                if pct is not None:
                    scored.append((pct, name, fe["index"], fe["packed"]))
        scored.sort(reverse=True)
        judge_frames = scored[:self.o.attr_poses]

        def label(x):
            return x if isinstance(x, str) else "g%s" % x

        result = {
            "bind_pairs": [{"over": label(a), "under": label(b), "pixels": n}
                           for (a, b), n in sorted(bind_viol.items(),
                                                   key=lambda kv: -kv[1])[:12]],
            "bind_ties": [{"a": label(a), "b": label(b), "pixels": n}
                          for (a, b), n in sorted(bind_ties.items(),
                                                  key=lambda kv: -kv[1])[:12]],
            "bind_tie_mask": mask_rel,
            "frames": [],
        }
        total_ties = dict(bind_ties)
        total_tie_faces = {k: (set(v[0]), set(v[1]))
                           for k, v in bind_tie_faces.items()}
        total_tie_pairs = {k: set(v) for k, v in bind_tie_pairs.items()}
        total_wrong = sum(bind_viol.values())
        for pct, name, index, packed in judge_frames:
            tag = "attr_%s_f%03d" % (name, index)
            dumps = self.render_for_ids(stripped, tag, frame=(name, packed))
            viol, ties, tie_faces, tie_pairs = pair_matrices(
                dumps, mapping, collect_faces=True)
            total_wrong += sum(viol.values())
            for k, v in ties.items():
                total_ties[k] = total_ties.get(k, 0) + v
            for k, (front, behind) in tie_faces.items():
                tf = total_tie_faces.setdefault(k, (set(), set()))
                tf[0].update(front)
                tf[1].update(behind)
            for k, edges in tie_pairs.items():
                total_tie_pairs.setdefault(k, set()).update(edges)
            new_pairs = {k: v for k, v in viol.items()
                         if v >= 30 and bind_viol.get(k, 0) < max(10, v // 10)}
            mask_rel = None
            mask_bmp = os.path.join(self.out_dir, "work", tag + "_ties.bmp")
            if tie_mask_png(dumps, mask_bmp):
                mask_rel, _ = save_shot(mask_bmp, self.out_dir, tag + "_ties")
            result["frames"].append({
                "seq": name, "frame": index, "pct": pct,
                "tie_mask": mask_rel,
                "pairs": [{"over": label(a), "under": label(b), "pixels": n}
                          for (a, b), n in
                          sorted(viol.items(), key=lambda kv: -kv[1])[:12]],
                "ties": [{"a": label(a), "b": label(b), "pixels": n}
                         for (a, b), n in
                         sorted(ties.items(), key=lambda kv: -kv[1])[:12]],
                "new_pairs": [{"over": label(a), "under": label(b), "pixels": n}
                              for (a, b), n in
                              sorted(new_pairs.items(), key=lambda kv: -kv[1])[:12]],
            })
            self.status.tick()

        # what the repair stage judges against, at patch granularity
        self.attr_baseline = {
            "frames": [(name, index, packed) for _, name, index, packed in judge_frames],
            "ties": total_ties,
            "tie_faces": total_tie_faces,
            "tie_pairs": total_tie_pairs,
            "wrong": total_wrong,
        }

        novel = {}
        for fr in result["frames"]:
            for p in fr["new_pairs"]:
                key = (p["over"], p["under"])
                novel[key] = novel.get(key, 0) + p["pixels"]
        if novel:
            top = sorted(novel.items(), key=lambda kv: -kv[1])[:5]
            self.find("anim-clipping", "warn",
                      "Animation-only clipping between %d structure pairs" % len(novel),
                      "; ".join("%s over %s (%d px)" % (a, b, n)
                                for (a, b), n in top) +
                      ". These interpenetrations do not exist at bind pose — "
                      "no static priority assignment can be right for them in "
                      "every frame.")
        intra = {k: v for k, v in total_ties.items()
                 if k[0] != k[1] and isinstance(k[0], str) and isinstance(k[1], str)
                 and k[0].split("#")[0] == k[1].split("#")[0] and v >= 40}
        same_patch = {k: v for k, v in total_ties.items() if k[0] == k[1] and v >= 40}
        if intra:
            top = sorted(intra.items(), key=lambda kv: -kv[1])[:4]
            self.find("self-fighting", "info",
                      "%d shell pairs fight INSIDE their own rig group" % len(intra),
                      ", ".join("%s vs %s (%d px)" % (k[0], k[1], n)
                                for k, n in top) +
                      ". These are separable: the patches are distinct "
                      "connected shells, so the repair stage can move them "
                      "apart along their contact normal.")
        if same_patch:
            top = sorted(same_patch.items(), key=lambda kv: -kv[1])[:4]
            self.find("coplanar-fighting", "info",
                      "%d patches carry coincident faces within one shell"
                      % len(same_patch),
                      ", ".join("%s (%d px)" % (label(k[0]), n) for k, n in top) +
                      ". Geometry moves cannot separate a shell from itself; "
                      "the repair stage tries a priority pin on the measured "
                      "front faces instead.")

        def apply(doc):
            doc["attribution"] = result

        self.status.update(apply)

    def split_by_part(self, ids, offsets):
        """merged indices -> {part: [local indices]}, deterministic order."""
        out = {}
        for merged in sorted(ids):
            part = 0
            while part + 1 < len(offsets) - 0 and merged >= offsets[part + 1]:
                part += 1
            out.setdefault(part, []).append(merged - offsets[part])
        return out

    def write_moves_file(self, moves, path):
        """Serialize move dicts into the nudge tool's line format. Any pin
        implies the clean priority ground (strip + bulk-flex) first."""
        lines = []
        if any(mv["kind"] == "pin" for mv in moves):
            lines.append("strip-priorities")
            lines.append("bulk-flex")
        for mv in moves:
            if mv["kind"] == "inflate":
                lines.append("inflate %d %g" % (mv["group"], mv["amount"]))
            elif mv["kind"] == "patch-normal":
                for part, local in self.split_by_part(
                        self.patch_faces[mv["patch"]],
                        self.part_face_offsets).items():
                    lines.append("patch-normal %d %g %s" % (
                        part, mv["amount"], ",".join(map(str, local))))
            elif mv["kind"] == "faces-normal":
                # same native op as patch-normal, just an explicit face set (a
                # bipartition side rather than a whole connected patch)
                for part, local in self.split_by_part(
                        mv["faces"], self.part_face_offsets).items():
                    lines.append("patch-normal %d %g %s" % (
                        part, mv["amount"], ",".join(map(str, local))))
            elif mv["kind"] == "verts-normal":
                for part, local in self.split_by_part(
                        mv["verts"], self.part_vertex_offsets).items():
                    lines.append("verts-normal %d %g %s" % (
                        part, mv["amount"], ",".join(map(str, local))))
            elif mv["kind"] == "pin":
                for part, local in self.split_by_part(
                        mv["faces"], self.part_face_offsets).items():
                    lines.append("priority-faces %d %d %s" % (
                        part, mv["band"], ",".join(map(str, local))))
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")

    @staticmethod
    def describe_move(mv):
        if mv["kind"] == "inflate":
            return "inflate g%d %+g" % (mv["group"], mv["amount"])
        if mv["kind"] == "patch-normal":
            return "normal %s %+g" % (mv["patch"], mv["amount"])
        if mv["kind"] == "faces-normal":
            return "normal %d-face side %+g" % (len(mv["faces"]), mv["amount"])
        if mv["kind"] == "verts-normal":
            return "vert %s %+g" % (
                ",".join(map(str, mv["verts"][:3])) +
                ("…" if len(mv["verts"]) > 3 else ""), mv["amount"])
        if mv["kind"] == "pin":
            return "pin %d faces band %d" % (len(mv["faces"]), mv["band"])
        return str(mv)

    def measure_moves(self, moves, tag):
        """Judge a move list end-to-end: nudge fresh copies of the originals,
        render the attribution judge frames (bind + worst poses) through the
        stock kernels, return patch-keyed ties (+ their face sets), total tie
        pixels and total wrong pixels. None on failure. Deterministic: same
        moves, same numbers."""
        work = os.path.join(self.out_dir, "work")
        moves_path = os.path.join(work, tag + ".moves")
        outs = [os.path.join(work, "%s_%d.ob3" % (tag, i))
                for i in range(len(self.o.model))]
        self.write_moves_file(moves, moves_path)
        args = [self.o.nudge_tool]
        for m in self.o.model:
            args += ["--in", m]
        for p in outs:
            args += ["--out", p]
        args += ["--moves", moves_path]
        _, rc = run_tool(self.status, args)
        if rc != 0 or not all(os.path.exists(p) for p in outs):
            return None
        # a pinned candidate must HONOUR priorities or the pin is invisible
        has_pin = any(mv["kind"] == "pin" for mv in moves)
        spec = (outs, [] if has_pin else ["--ignore-priorities"])
        mapping = self.face_patch or self.face_group
        ties = {}
        tie_faces = {}
        tie_pairs = {}
        wrong = 0
        frames = [(None, None)] + [(name, packed) for name, _, packed
                                   in self.attr_baseline["frames"]]
        for fi, (name, packed) in enumerate(frames):
            dumps = self.render_for_ids(
                spec, "%s_j%d" % (tag, fi),
                frame=(name, packed) if name else None)
            v, t, tf, tp = pair_matrices(dumps, mapping, collect_faces=True)
            wrong += sum(v.values())
            for k, n in t.items():
                ties[k] = ties.get(k, 0) + n
            for k, (front, behind) in tf.items():
                dst = tie_faces.setdefault(k, (set(), set()))
                dst[0].update(front)
                dst[1].update(behind)
            for k, edges in tp.items():
                tie_pairs.setdefault(k, set()).update(edges)
        for p in outs:
            os.remove(p)
        os.remove(moves_path)
        return {"ties": ties, "tie_faces": tie_faces, "tie_pairs": tie_pairs,
                "tie_total": sum(ties.values()), "wrong": wrong}

    def pair_trials(self, pair, baseline):
        """The deterministic proposal set for one fighting pair.

        Cross-patch pairs: normal-direction separations at every configured
        amount, both sides, both signs.

        Every pair also gets BIPARTITION trials: 2-color the measured
        conflict graph (which face contested which at the same depth) to
        recover the two interleaved sides — for a shell fighting itself this
        is the only decomposition that means anything, because every face is
        "front" somewhere and the raw front/behind sets nearly coincide. Each
        side is then tried as a normal-direction geometry move and (when
        enabled) as a priority pin, the pin preferring the side the z-buffer
        voted for."""
        pa, pb = pair
        trials = []
        if pa != pb:
            for patch in (pa, pb):
                if patch in self.patch_faces:
                    for amount in self.o.repair_amount_list:
                        trials.append({"kind": "patch-normal", "patch": patch,
                                       "amount": amount})
                        trials.append({"kind": "patch-normal", "patch": patch,
                                       "amount": -amount})
        edges = baseline.get("tie_pairs", {}).get(pair, set())
        side0 = side1 = None
        if edges:
            side0, side1, votes = bipartition_conflicts(edges)
            if side0 and side1:
                small_amounts = [a for a in self.o.repair_amount_list if a <= 2] or [1]
                for side in (side0, side1):
                    for amount in small_amounts:
                        trials.append({"kind": "faces-normal",
                                       "faces": side[:600], "amount": amount})
                        trials.append({"kind": "faces-normal",
                                       "faces": side[:600], "amount": -amount})
                # interleaved shells often share vertices along a fold; a
                # whole-side move drags both sides there. Moving only the
                # vertices EXCLUSIVE to one side separates what can separate.
                def side_verts(side):
                    return {v for f in side for v in self.face_topo.get(f, ())}
                v0, v1 = side_verts(side0), side_verts(side1)
                for exclusive in (sorted(v0 - v1)[:400], sorted(v1 - v0)[:400]):
                    if exclusive:
                        for amount in small_amounts:
                            trials.append({"kind": "verts-normal",
                                           "verts": exclusive, "amount": amount})
                            trials.append({"kind": "verts-normal",
                                           "verts": exclusive, "amount": -amount})
                if self.o.repair_priority_trials:
                    preferred = side0 if votes[0] >= votes[1] else side1
                    other = side1 if preferred is side0 else side0
                    # band 11 first: both flexible bands depth-sort with the
                    # bulk, so an 11-over-10 split can stabilize an exact-depth
                    # tie without pinning the faces into a hard layer (the
                    # hard band-5 pin fixes ties but wrecks layering wherever
                    # the pinned faces are NOT tied — measured, not theory)
                    trials.append({"kind": "pin", "band": 11,
                                   "faces": preferred[:600]})
                    trials.append({"kind": "pin", "band": 11,
                                   "faces": other[:600]})
                    trials.append({"kind": "pin", "band": 5,
                                   "faces": preferred[:600]})
                    trials.append({"kind": "pin", "band": 5,
                                   "faces": other[:600]})
        if self.o.repair_priority_trials and not (side0 and side1):
            front, behind = baseline["tie_faces"].get(pair, (set(), set()))
            if front:
                trials.append({"kind": "pin", "band": 5,
                               "faces": sorted(front)[:600]})
            if behind and behind != front:
                trials.append({"kind": "pin", "band": 5,
                               "faces": sorted(behind)[:600]})
        return trials

    def try_trials(self, pair, trials, adopted, baseline, entry, budget_state):
        """Measure each trial against the current baseline; return the best
        passing (trial, measurement) or None. Early-stops when a trial all but
        eliminates the pair."""
        pair_base = baseline["ties"].get(pair, 0)
        best = None
        for trial in trials:
            if budget_state["used"] >= self.o.repair_trial_budget:
                entry["trials"].append({"move": "(budget exhausted)"})
                return best
            tag = "rep%d" % budget_state["used"]
            m = self.measure_moves(adopted + [trial], tag)
            budget_state["used"] += 1
            self.status.tick()
            if m is None:
                entry["trials"].append({"move": self.describe_move(trial),
                                        "error": "nudge/render failed"})
                continue
            pair_now = m["ties"].get(pair, 0)
            ok = (pair_now <= 0.8 * max(1, pair_base) and
                  m["tie_total"] <= baseline["tie_total"] and
                  m["wrong"] <= baseline["wrong"] * 1.02)
            entry["trials"].append({
                "move": self.describe_move(trial), "pair_ties": pair_now,
                "tie_total": m["tie_total"], "wrong": m["wrong"], "ok": ok})
            if ok and (best is None or
                       (pair_now, m["tie_total"]) <
                       (best[1]["ties"].get(pair, 0), best[1]["tie_total"])):
                best = (trial, m)
                if pair_now <= max(2, int(0.05 * pair_base)):
                    break
        return best

    def anneal_tier(self, adopted, baseline, budget_state, result, apply_doc):
        """Simulated annealing over compound move states, guided by the
        measurements: patches are proposed in proportion to the tie pixels
        still on them, pins toggle on the pairs still fighting, and the
        occasional single vertex moves. Seeded (--repair-seed), so 'random'
        reruns identically. Returns (adopted, baseline) only when the walk's
        best state STRICTLY beats the deterministic tiers; otherwise None and
        the deterministic result stands."""
        import random
        rng = random.Random(self.o.repair_seed)
        iters = self.o.repair_anneal
        self.status.log("repair: anneal tier, %d iterations, seed %d"
                        % (iters, self.o.repair_seed))

        def score(m):
            return m["tie_total"] + 2 * max(0, m["wrong"] - baseline0_wrong)

        baseline0_wrong = baseline["wrong"]
        current = list(adopted)
        cur_m = baseline
        best = (score(baseline), list(current), baseline)
        t0 = max(10.0, 0.05 * baseline["tie_total"])
        accepted = 0
        anneal_doc = {"iterations": iters, "seed": self.o.repair_seed,
                      "accepted": 0, "start_ties": baseline["tie_total"],
                      "best_ties": baseline["tie_total"], "adopted": False}
        result["anneal"] = anneal_doc

        def weighted_patch_pairs(m):
            pairs = sorted(((k, n) for k, n in m["ties"].items() if n > 0),
                           key=lambda kv: str(kv[0]))
            return pairs

        for it in range(iters):
            temp = t0 * (1.0 - it / max(1, iters)) + 1e-6
            pairs = weighted_patch_pairs(cur_m)
            if not pairs:
                break
            keys = [k for k, _ in pairs]
            weights = [n for _, n in pairs]
            pair = rng.choices(keys, weights=weights)[0]
            roll = rng.random()
            proposal = list(current)
            if roll < 0.6 and isinstance(pair[0], str):
                patch = pair[0] if rng.random() < 0.5 else pair[1]
                if patch not in self.patch_faces:
                    continue
                existing = next((i for i, mv in enumerate(proposal)
                                 if mv["kind"] == "patch-normal" and
                                 mv["patch"] == patch), None)
                delta = rng.choice([-2, -1, 1, 2])
                if existing is not None:
                    amount = proposal[existing]["amount"] + delta
                    if amount == 0 or abs(amount) > 8:
                        continue
                    proposal[existing] = dict(proposal[existing], amount=amount)
                else:
                    proposal.append({"kind": "patch-normal", "patch": patch,
                                     "amount": delta})
            elif roll < 0.8:
                front, behind = cur_m["tie_faces"].get(pair, (set(), set()))
                faces = sorted(front)[:600]
                if not faces:
                    continue
                existing = next((i for i, mv in enumerate(proposal)
                                 if mv["kind"] == "pin" and
                                 mv["faces"] == faces), None)
                if existing is not None:
                    proposal.pop(existing)
                else:
                    proposal.append({"kind": "pin", "band": 5, "faces": faces})
            else:
                front, behind = cur_m["tie_faces"].get(pair, (set(), set()))
                side = sorted(front if len(front) <= len(behind) else behind)
                verts = sorted({v for f in side[:50]
                                for v in self.face_topo.get(f, ())})
                if not verts:
                    continue
                proposal.append({"kind": "verts-normal",
                                 "verts": [rng.choice(verts)],
                                 "amount": rng.choice([-2, -1, 1, 2])})
            m = self.measure_moves(proposal, "repa%d" % budget_state["used"])
            budget_state["used"] += 1
            if m is None:
                continue
            s_cur, s_new = score(cur_m), score(m)
            if s_new < s_cur or rng.random() < math.exp((s_cur - s_new) / temp):
                current, cur_m = proposal, m
                accepted += 1
                if s_new < best[0]:
                    best = (s_new, list(proposal), m)
            if it % 10 == 9:
                anneal_doc.update(accepted=accepted,
                                  best_ties=best[2]["tie_total"],
                                  iteration=it + 1)
                self.status.update(apply_doc)
        anneal_doc.update(accepted=accepted, best_ties=best[2]["tie_total"])
        _, best_moves, best_m = best
        if (best_m["tie_total"] < baseline["tie_total"] and
                best_m["wrong"] <= baseline["wrong"] * 1.02):
            anneal_doc["adopted"] = True
            self.status.log("repair: anneal improved ties %d→%d; adopting"
                            % (baseline["tie_total"], best_m["tie_total"]))
            self.status.update(apply_doc)
            return best_moves, best_m
        self.status.log("repair: anneal did not beat the deterministic result")
        self.status.update(apply_doc)
        return None

    def stage_repair(self):
        """Identify where z-fighting is happening, then move the fighters
        apart — a deterministic measure→nudge→look loop. The suspect pool
        fuses measured tie pixels (patch-level, so shells fighting inside one
        rig group are targets too), measured animation clipping, and the
        structure analysis's rig+priority overlaps. Each round takes the
        worst pairs, tries the configured proposal set (normal-direction
        separations at --repair-amounts on both sides; priority pins of the
        measured tying faces), adopts a move only when its pair's ties drop
        ≥20% while total ties and sort error hold, and re-ranks off the new
        measurements; rounds repeat until one adopts nothing. A final vertex
        pass does the same loop one vertex at a time on the worst survivor.
        Everything is judged through the stock kernels; nothing is random."""
        if not self.o.repair:
            return
        if not self.attr_baseline:
            self.status.log("repair skipped: no attribution baseline")
            return
        if not os.path.exists(self.o.nudge_tool):
            self.status.log("repair skipped: %s not built "
                            "(make -C src rs2012-model-nudge)" % self.o.nudge_tool)
            return

        baseline = {"ties": dict(self.attr_baseline["ties"]),
                    "tie_faces": self.attr_baseline["tie_faces"],
                    "tie_pairs": self.attr_baseline["tie_pairs"],
                    "tie_total": sum(self.attr_baseline["ties"].values()),
                    "wrong": self.attr_baseline["wrong"]}
        budget_state = {"used": 0}
        adopted = []
        result = {"baseline": {"tie_total": baseline["tie_total"],
                               "wrong": baseline["wrong"]},
                  "config": {"amounts": self.o.repair_amount_list,
                             "rounds": self.o.repair_rounds,
                             "pairs_per_round": self.o.repair_pairs,
                             "trial_budget": self.o.repair_trial_budget,
                             "priority_trials": self.o.repair_priority_trials,
                             "vertex_pass": self.o.repair_vertex_pass,
                             "anneal": self.o.repair_anneal,
                             "seed": self.o.repair_seed},
                  "pairs": [], "moves": [], "vertex_pass": None}

        def apply_doc(doc):
            doc["repair"] = result

        self.status.update(apply_doc)
        self.status.stage("repair", "Z-fighting repair loop (budget %d trials)"
                          % self.o.repair_trial_budget,
                          self.o.repair_trial_budget)

        structure_seed = {}
        for s in getattr(self, "structure_suspects", []):
            main = {}
            for g in (s["a"], s["b"]):
                patches = [p for p, grp in self.patch_group.items() if grp == g]
                if patches:
                    main[g] = max(patches, key=lambda p: len(self.patch_faces[p]))
            if len(main) == 2:
                key = tuple(sorted(main.values()))
                structure_seed[key] = structure_seed.get(key, 0) + \
                    min(30.0, s["weight"] / 50.0)

        attempted = set()
        for round_no in range(self.o.repair_rounds):
            pool = {}
            for (a, b), n in baseline["ties"].items():
                if isinstance(a, int) and a < 0:
                    continue
                if isinstance(b, int) and b < 0:
                    continue
                pool[(a, b)] = pool.get((a, b), 0) + float(n)
            if round_no == 0:
                for key, w in structure_seed.items():
                    pool[key] = pool.get(key, 0) + w
            ranked = sorted(((k, w) for k, w in pool.items()
                             if w >= 15 and k not in attempted),
                            key=lambda kv: (-kv[1], str(kv[0])))
            ranked = ranked[:self.o.repair_pairs]
            if not ranked or budget_state["used"] >= self.o.repair_trial_budget:
                break
            adopted_this_round = 0
            for pair, weight in ranked:
                attempted.add(pair)
                entry = {"round": round_no, "a": str(pair[0]), "b": str(pair[1]),
                         "evidence": round(weight, 1),
                         "base_ties": baseline["ties"].get(pair, 0),
                         "trials": [], "adopted": None}
                result["pairs"].append(entry)
                trials = self.pair_trials(pair, baseline)
                best = self.try_trials(pair, trials, adopted, baseline, entry,
                                       budget_state)
                if best:
                    trial, m = best
                    adopted.append(trial)
                    baseline = {"ties": m["ties"], "tie_faces": m["tie_faces"],
                                "tie_total": m["tie_total"], "wrong": m["wrong"]}
                    entry["adopted"] = {"move": self.describe_move(trial),
                                        "pair_ties": m["ties"].get(pair, 0)}
                    result["moves"].append(self.describe_move(trial))
                    adopted_this_round += 1
                    self.status.log("repair: %s — pair %s/%s ties %d→%d" % (
                        self.describe_move(trial), pair[0], pair[1],
                        entry["base_ties"], m["ties"].get(pair, 0)))
                else:
                    self.status.log("repair: no safe move for %s/%s"
                                    % (pair[0], pair[1]))
                self.status.update(apply_doc)
            if adopted_this_round == 0:
                break

        # vertex tier: one vertex at a time on the worst survivor
        if (self.o.repair_vertex_pass and self.face_topo and
                budget_state["used"] < self.o.repair_trial_budget):
            survivors = sorted(
                ((k, n) for k, n in baseline["ties"].items() if n >= 30),
                key=lambda kv: (-kv[1], str(kv[0])))
            if survivors:
                pair = survivors[0][0]
                edges = baseline.get("tie_pairs", {}).get(pair, set())
                if edges:
                    side0, side1, _ = bipartition_conflicts(edges)
                    side = side0 if 0 < len(side0) <= max(1, len(side1)) else side1
                else:
                    front, behind = baseline["tie_faces"].get(pair, (set(), set()))
                    side = sorted(front if len(front) <= len(behind) else behind)
                verts = sorted({v for f in list(side)[:200]
                                for v in self.face_topo.get(f, ())})
                verts = verts[:self.o.repair_vertex_cap]
                vp = {"pair": [str(pair[0]), str(pair[1])],
                      "base_ties": baseline["ties"].get(pair, 0),
                      "verts_tried": len(verts), "adopted": [], "sweeps": 0}
                result["vertex_pass"] = vp
                self.status.log("repair: vertex pass on %s/%s (%d verts)"
                                % (pair[0], pair[1], len(verts)))
                for sweep in range(3):
                    vp["sweeps"] = sweep + 1
                    adopted_this_sweep = 0
                    for v in verts:
                        if budget_state["used"] >= self.o.repair_trial_budget:
                            break
                        pair_base = baseline["ties"].get(pair, 0)
                        if pair_base < 10:
                            break
                        for amount in (1, -1, 2, -2):
                            trial = {"kind": "verts-normal", "verts": [v],
                                     "amount": amount}
                            tag = "repv%d" % budget_state["used"]
                            m = self.measure_moves(adopted + [trial], tag)
                            budget_state["used"] += 1
                            self.status.tick()
                            if m is None:
                                continue
                            pair_now = m["ties"].get(pair, 0)
                            if (pair_now <= pair_base - 3 and
                                    m["tie_total"] <= baseline["tie_total"] and
                                    m["wrong"] <= baseline["wrong"] * 1.02):
                                adopted.append(trial)
                                baseline = {"ties": m["ties"],
                                            "tie_faces": m["tie_faces"],
                                            "tie_total": m["tie_total"],
                                            "wrong": m["wrong"]}
                                vp["adopted"].append("v%d %+d (ties %d→%d)" % (
                                    v, amount, pair_base, pair_now))
                                adopted_this_sweep += 1
                                break
                    self.status.update(apply_doc)
                    if adopted_this_sweep == 0:
                        break
                vp["final_ties"] = baseline["ties"].get(pair, 0)

        # anneal tier (off by default): a seeded random walk over compound
        # move states, guided by where the ties still are — it can trade one
        # pair against another in ways the greedy tiers cannot. Its result is
        # thrown away unless it strictly beats the deterministic one.
        if self.o.repair_anneal > 0:
            det_baseline = baseline
            det_adopted = list(adopted)
            annealed = self.anneal_tier(adopted, baseline, budget_state, result,
                                        apply_doc)
            if annealed is not None:
                adopted, baseline = annealed
            else:
                adopted, baseline = det_adopted, det_baseline

        result["trials_used"] = budget_state["used"]
        if not adopted:
            result["outcome"] = "no move both separated a pair and held the line"
            self.status.update(apply_doc)
            return

        outs = [os.path.join(self.out_dir, "repaired_%d.ob3" % i)
                for i in range(len(self.o.model))]
        self.write_moves_file(adopted, os.path.join(self.out_dir, "repaired.moves"))
        args = [self.o.nudge_tool]
        for m in self.o.model:
            args += ["--in", m]
        for p in outs:
            args += ["--out", p]
        args += ["--moves", os.path.join(self.out_dir, "repaired.moves")]
        _, rc = run_tool(self.status, args)
        if rc != 0:
            result["outcome"] = "final nudge failed"
            self.status.update(apply_doc)
            return
        result["outcome"] = (
            "adopted %d moves in %d trials; judge-frame ties %d→%d (−%.0f%%), "
            "wrong %d→%d" % (
                len(adopted), budget_state["used"],
                result["baseline"]["tie_total"], baseline["tie_total"],
                100.0 * (1 - baseline["tie_total"] /
                         max(1, result["baseline"]["tie_total"])),
                result["baseline"]["wrong"], baseline["wrong"]))
        result["models"] = [os.path.relpath(p, REPO_ROOT) for p in outs]
        self.status.update(apply_doc)
        self.find("repair", "info",
                  "Repair adopted %d moves" % len(adopted),
                  result["outcome"] + ". Repaired models at repaired_*.ob3; "
                  "flip the live view between 'stripped' and 'repaired' to "
                  "compare before/after.")
        has_pin = any(mv["kind"] == "pin" for mv in adopted)
        self.candidates["repaired"] = {
            "models": outs, "flags": [] if has_pin else ["--ignore-priorities"]}

        def apply2(doc):
            doc["candidates"] = list(self.candidates)

        self.status.update(apply2)
        self.live.prewarm()
        self.rescore_candidate("repaired")

    def stage_search(self):
        if not self.o.search:
            return
        if not os.path.exists(self.o.prio_tool):
            self.status.log("search skipped: %s not built" % self.o.prio_tool)
            return
        live = [(n, p) for n, p in self.seq_plan.items() if not p.get("skip")]
        corpus = []
        for name, plan in live:
            ok = [f for f in plan["frames"] if f["decode"] == "ok"]
            for f in ok[:: max(1, len(ok) // 3)][:3]:
                corpus.append(f["packed"])
        corpus = corpus[:10]
        self.status.stage("search", "Priority search over %d poses%s" % (
            len(corpus), " + slow anneal" if self.o.slow else ""), 1)
        # NOT under work/: the searched models are a deliverable (the verdict
        # may name them best) and work/ is deleted when the run ends.
        outs = [os.path.join(self.out_dir, "searched_%d.ob3" % i)
                for i in range(len(self.o.model))]
        args = [self.o.prio_tool]
        for m in self.o.model:
            args += ["--in", m]
        for p in outs:
            args += ["--out", p]
        if corpus:
            args += ["--cache", self.o.cache,
                     "--frames", ",".join(str(c) for c in corpus)]
        if self.o.slow:
            args += ["--slow", str(self.o.slow)]
        args += ["--report"]
        proc = subprocess.Popen(args, cwd=REPO_ROOT, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        table = []
        for line in proc.stdout:
            line = line.rstrip()
            if not line:
                continue
            self.status.log("search: " + line[:160])
            row = RE_PRIO_TABLE_ROW.match(line)
            if row and "strategy" not in line:
                table.append({"strategy": row.group(1).strip(),
                              "wrong": int(row.group(2)),
                              "pct": float(row.group(3)),
                              "bands": row.group(4),
                              "chosen": bool(row.group(5))})
        proc.wait()
        ok = proc.returncode == 0 and all(os.path.exists(p) for p in outs)

        def apply(doc):
            doc["search"] = {"ok": ok, "table": table, "corpus": corpus,
                             "slow": self.o.slow}

        self.status.update(apply)
        self.status.tick()
        if ok:
            self.candidates["searched"] = {"models": outs, "flags": []}

            def apply2(doc):
                doc["candidates"] = list(self.candidates)

            self.status.update(apply2)
            self.live.prewarm()
            self.rescore_candidate("searched")
        else:
            self.status.log("search did not produce output (exit %d)"
                            % proc.returncode)

    def rescore_candidate(self, cand):
        """Score a late-added candidate across the already-planned sweep."""
        live = [(n, p) for n, p in self.seq_plan.items() if not p.get("skip")]
        total = sum(len([f for f in p["frames"] if f["decode"] == "ok"])
                    for _, p in live) + 1
        self.status.stage("rescore", "Scoring '%s' across the sweep" % cand, total)
        work = os.path.join(self.out_dir, "work")
        base = os.path.join(work, "static_%s" % cand)
        out, _ = run_tool(self.status, self.viewer_args(cand, [
            "--angles", str(self.o.static_angles), "--pitch", self.o.static_pitches,
            "--tile", str(self.o.static_tile), "--score", "--compare",
            "--out", base + "_painter.bmp", "--out-zbuffer", base + "_zbuf.bmp"]))
        entry = {}
        s = RE_SORT_SCORE.search(out)
        if s:
            entry = {"wrong": int(s.group(1)), "covered": int(s.group(2)),
                     "pct": float(s.group(3))}
        shots = {}
        for kind in ("painter", "zbuf"):
            bmp = "%s_%s.bmp" % (base, kind)
            if os.path.exists(bmp):
                full, thumb = save_shot(bmp, self.out_dir,
                                        "static_%s_%s" % (cand, kind))
                shots[kind] = {"full": full, "thumb": thumb}
        entry["shots"] = shots

        def apply(doc):
            doc["static"][cand] = entry

        self.status.update(apply)
        self.status.tick()
        for name, plan in live:
            prev = (None, None)
            for fe_doc, rec in zip(self.status.doc["seqs"][name]["per_frame"],
                                   [f for f in plan["frames"] if f["decode"] == "ok"]):
                entry, painter, zbuf = self.render_frame(cand, name, plan, rec)
                pp, pz = prev
                if painter is not None and pp is not None:
                    fl = diff_metrics(pp, painter, pz, zbuf, self.bg_rgb)
                    if fl and fl[1]:
                        entry["flicker_pct"] = round(100.0 * fl[0] / fl[1], 3)
                if painter is not None:
                    cr = coverage_cracks(painter, zbuf, self.bg_rgb)
                    if cr and cr[1]:
                        entry["crack_pct"] = round(100.0 * cr[0] / cr[1], 3)
                if pp is not None:
                    pp.close()
                if pz is not None:
                    pz.close()
                prev = (painter, zbuf)

                def apply(doc, name=name, index=rec["index"], entry=entry,
                          cand=cand):
                    for fe in doc["seqs"][name]["per_frame"]:
                        if fe["index"] == index:
                            fe["cands"][cand] = entry

                self.status.update(apply)
                self.status.tick()
            for img in prev:
                if img is not None:
                    img.close()
            self.summarize_seq(name)

    def stage_verdict(self):
        self.status.stage("verdict", "Writing verdict", 1)
        doc = self.status.doc
        table = {}
        for cand in self.candidates:
            means, maxes, flicks = [], [], []
            for name in doc["seq_order"]:
                s = doc["seqs"].get(name, {}).get("summary", {}).get(cand)
                if s:
                    means.append(s["mean_pct"])
                    maxes.append(s["max_pct"])
                    if s.get("mean_flicker_pct") is not None:
                        flicks.append(s["mean_flicker_pct"])
            st = doc["static"].get(cand, {})
            table[cand] = {
                "static_pct": st.get("pct"),
                "anim_mean_pct": round(sum(means) / len(means), 3) if means else None,
                "anim_max_pct": round(max(maxes), 3) if maxes else None,
                "mean_flicker_pct": round(sum(flicks) / len(flicks), 3) if flicks else None,
            }
        scored = {c: v for c, v in table.items() if v["anim_mean_pct"] is not None}
        best = min(scored, key=lambda c: scored[c]["anim_mean_pct"]) if scored else None
        recommendation = None
        if best:
            b = scored[best]
            if b["anim_mean_pct"] <= 2.0 and (b["anim_max_pct"] or 0) <= 6.0:
                recommendation = (
                    "Ship '%s' priorities through the painter: %.2f%% mean / "
                    "%.2f%% worst-frame sort error across the animation set."
                    % (best, b["anim_mean_pct"], b["anim_max_pct"]))
            else:
                recommendation = (
                    "No priority assignment holds up under animation (best is "
                    "'%s' at %.2f%% mean / %.2f%% worst frame). Opt the npc "
                    "into the per-model depth test instead "
                    "(param=zbuffer_model,int,1 / TORIDRAW_MODEL_FLAG_ZBUFFER)."
                    % (best, b["anim_mean_pct"], b["anim_max_pct"]))

        def apply(doc):
            doc["verdict"] = {"table": table, "best": best,
                              "recommendation": recommendation}
            doc["meta"]["finished"] = True

        self.status.update(apply)
        if recommendation:
            self.status.log("verdict: " + recommendation)

    # ------------------------------------------------------------------ run

    def run(self):
        seq_cfg = parse_seq_config(self.o.seqcfg, self.o.seq)
        self.seq_frames = seq_cfg
        for name, frames in seq_cfg.items():
            self.seq_plan[name] = {
                "indices": sample_frames(frames, self.o.max_frames),
                "frames": [],
            }
        self.stage_decode()
        self.stage_structures()
        self.stage_static()
        self.stage_prescan()
        if self.live.configure():
            cfg = self.live.cfg

            def apply(doc):
                doc["live"] = {"frames": cfg["frames"], "tile": cfg["tile"],
                               "zoom": cfg["zoom"]}

            self.status.update(apply)
            self.live.prewarm()
        self.stage_sweep()
        self.stage_attribution()
        self.stage_repair()
        self.stage_search()
        self.stage_verdict()
        # the sweep work dir holds only intermediates; shots/ has the keepers
        shutil.rmtree(os.path.join(self.out_dir, "work"), ignore_errors=True)


# ------------------------------------------------------------------- server


def serve(audit, port):
    ui_dir = os.path.join(TOOL_DIR, "ui")
    out_dir = audit.out_dir

    class Handler(SimpleHTTPRequestHandler):
        def do_GET(self):
            if self.path.startswith("/live"):
                return self.do_live()
            return SimpleHTTPRequestHandler.do_GET(self)

        def do_live(self):
            qs = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)

            def arg(key, default, cast):
                try:
                    return cast(qs[key][0])
                except (KeyError, ValueError, IndexError):
                    return default

            try:
                result = audit.live.render(
                    arg("cand", "inherited", str),
                    arg("pose", -1, int),
                    arg("yaw", 0, int),
                    arg("pitch", 0, int),
                    max(0.1, min(12.0, arg("zoom", 1.0, float))),
                    "zbuf" if arg("mode", "painter", str) == "zbuf" else "painter")
            except Exception as e:
                result = None
                audit.status.log("live render error: %s" % e)
            if not result:
                self.send_response(503)
                self.end_headers()
                return
            kind, data = result
            self.send_response(200)
            self.send_header("Content-Type", "image/" + kind)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def translate_path(self, path):
            path = path.split("?", 1)[0].split("#", 1)[0]
            if path in ("/", "/index.html"):
                return os.path.join(ui_dir, "index.html")
            rel = os.path.normpath(path.lstrip("/"))
            if rel.startswith(".."):
                return os.path.join(out_dir, "nope")
            return os.path.join(out_dir, rel)

        def end_headers(self):
            self.send_header("Cache-Control", "no-store")
            SimpleHTTPRequestHandler.end_headers(self)

        def log_message(self, *args):
            pass

    class Server(ThreadingHTTPServer):
        # On Windows SO_REUSEADDR lets a second process silently steal a bound
        # port from a still-running server (a stale report server, typically),
        # after which requests land on WHICHEVER process the stack favours.
        # Fail loudly instead; the except below turns it into a log line.
        allow_reuse_address = sys.platform != "win32"

    try:
        httpd = Server(("127.0.0.1", port), Handler)
    except OSError as e:
        audit.status.log("UI server not started (port %d busy? %s) - "
                         "auditing anyway; results land in status.json" % (port, e))
        return None
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    audit.status.log("live UI on http://127.0.0.1:%d/" % port)
    return httpd


# --------------------------------------------------------------------- main


def load_presets():
    path = os.path.join(TOOL_DIR, "presets.json")
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    return {}


def main():
    presets = load_presets()
    ap = argparse.ArgumentParser(
        description="Animation-aware painter-vs-zbuffer audit for backported "
                    "RS2012 models, with a live web UI.")
    ap.add_argument("--preset", choices=sorted(presets), default=None,
                    help="fill --model/--seq/--cache/--seqcfg from presets.json")
    ap.add_argument("--model", action="append", default=[],
                    help=".ob3 part, repeatable; parts merge like npc model1/model2")
    ap.add_argument("--seq", action="append", default=[],
                    help="sequence section name from the seq config "
                         "(rs2012_seq_16715 or bare 16715), repeatable")
    ap.add_argument("--seqcfg", default=None, help="text seq config path")
    ap.add_argument("--cache", default=None, help="packed dat2 cache dir")
    ap.add_argument("--candidate", action="append", default=[],
                    metavar="NAME=a.ob3[,b.ob3]",
                    help="extra priority candidate (model part list), repeatable")
    ap.add_argument("--out", default=None, help="output/report directory")
    ap.add_argument("--viewer",
                    default=os.path.join("src", "build_win64_opt",
                                         "rs2012_model_view.exe"))
    ap.add_argument("--prio-tool",
                    default=os.path.join("src", "build_win64_opt",
                                         "rs2012_face_priorities.exe"))
    ap.add_argument("--max-frames", type=int, default=12,
                    help="frames sampled per sequence (default 12)")
    ap.add_argument("--angles", type=int, default=4)
    ap.add_argument("--pitch", default="0")
    ap.add_argument("--tile", type=int, default=256)
    ap.add_argument("--static-angles", type=int, default=6)
    ap.add_argument("--static-pitches", default="0,128")
    ap.add_argument("--static-tile", type=int, default=320)
    ap.add_argument("--attr-poses", type=int, default=6,
                    help="worst poses to attribute to bone-group pairs")
    ap.add_argument("--attr-tile", type=int, default=224,
                    help="raster size for attribution/repair id maps")
    ap.add_argument("--repair", action=argparse.BooleanOptionalAction, default=True,
                    help="run the measure->nudge->look repair loop "
                         "(needs rs2012-model-nudge)")
    ap.add_argument("--repair-pairs", type=int, default=5,
                    help="fighting pairs attempted per round (default 5)")
    ap.add_argument("--repair-rounds", type=int, default=3,
                    help="re-rank and retry rounds; stops early when a round "
                         "adopts nothing (default 3)")
    ap.add_argument("--repair-amounts", default="1,2,4",
                    help="normal-direction separation amounts to try, comma "
                         "list; each is tried +/- on both sides of a pair "
                         "(default 1,2,4)")
    ap.add_argument("--repair-trial-budget", type=int, default=400,
                    help="hard cap on measured trials across the whole stage "
                         "(default 400; each trial renders the judge frames)")
    ap.add_argument("--repair-priority-trials",
                    action=argparse.BooleanOptionalAction, default=True,
                    help="also try pinning the measured tying faces into a "
                         "priority band (the painter-native z-fighting fix)")
    ap.add_argument("--repair-vertex-pass",
                    action=argparse.BooleanOptionalAction, default=True,
                    help="finish with a vertex-at-a-time descent on the worst "
                         "surviving pair")
    ap.add_argument("--repair-vertex-cap", type=int, default=16,
                    help="vertices considered by the vertex pass (default 16)")
    ap.add_argument("--repair-anneal", type=int, default=0,
                    help="OFF by default: after the deterministic tiers, run "
                         "N seeded simulated-annealing iterations over "
                         "compound move states, proposals guided by where the "
                         "measured ties still are; adopted only if it "
                         "strictly beats the deterministic result")
    ap.add_argument("--repair-seed", type=int, default=1,
                    help="RNG seed for --repair-anneal (reruns identically)")
    ap.add_argument("--nudge-tool",
                    default=os.path.join("src", "build_win64_opt",
                                         "rs2012_model_nudge.exe"))
    ap.add_argument("--search", action=argparse.BooleanOptionalAction, default=True,
                    help="run rs2012_face_priorities over a pose corpus")
    ap.add_argument("--slow", type=int, default=0,
                    help="slow-anneal iterations for the search (default off)")
    ap.add_argument("--port", type=int, default=8151)
    ap.add_argument("--serve", action=argparse.BooleanOptionalAction, default=True)
    ap.add_argument("--open", action="store_true", help="open the UI in a browser")
    ap.add_argument("--keep-serving", action=argparse.BooleanOptionalAction,
                    default=True, help="keep the UI server alive after the run")
    o = ap.parse_args()

    if o.preset:
        p = presets[o.preset]
        o.model = o.model or p.get("models", [])
        o.seq = o.seq or p.get("seqs", [])
        o.seqcfg = o.seqcfg or p.get("seqcfg")
        o.cache = o.cache or p.get("cache")
        o.out = o.out or os.path.join("build", "backport_audit", o.preset)
    if not o.model or not o.seq:
        ap.error("need --model and --seq (or --preset)")
    o.seqcfg = o.seqcfg or os.path.join(
        "OSRS-Content", "osrs239-content", "ported", "rs2012_qbd_td",
        "configs", "rs2012.seq")
    o.cache = o.cache or "cache.osrs239.rs2012"
    o.seq = [s if not s.isdigit() else "rs2012_seq_" + s for s in o.seq]
    if not o.out:
        stem = os.path.splitext(os.path.basename(o.model[0]))[0]
        o.out = os.path.join("build", "backport_audit", stem)

    def absify(path):
        return path if os.path.isabs(path) else os.path.join(REPO_ROOT, path)

    o.model = [absify(m) for m in o.model]
    o.candidate = [
        "%s=%s" % (spec.partition("=")[0],
                   ",".join(absify(p) for p in spec.partition("=")[2].split(",")))
        for spec in o.candidate]
    o.seqcfg = absify(o.seqcfg)
    o.cache = absify(o.cache)
    o.out = absify(o.out)
    o.viewer = absify(o.viewer)
    o.prio_tool = absify(o.prio_tool)
    o.nudge_tool = absify(o.nudge_tool)
    try:
        o.repair_amount_list = sorted({abs(float(a))
                                       for a in o.repair_amounts.split(",")
                                       if float(a) != 0})
    except ValueError:
        ap.error("--repair-amounts must be a comma list of numbers")
    if not o.repair_amount_list:
        o.repair_amount_list = [1, 2, 4]
    if not os.path.exists(o.viewer):
        ap.error("viewer not built: %s (make -C src rs2012-model-view)" % o.viewer)
    for m in o.model:
        if not os.path.exists(m):
            ap.error("no such model: %s" % m)
    if Image is None:
        print("audit: Pillow not found - shots stay BMP, flicker/crack metrics off",
              file=sys.stderr)

    audit = Audit(o)
    httpd = None
    if o.serve:
        httpd = serve(audit, o.port)
        if o.open:
            webbrowser.open("http://127.0.0.1:%d/" % o.port)
    try:
        audit.run()
    except KeyboardInterrupt:
        audit.status.log("interrupted")
        raise
    except Exception as e:  # surface into the UI, then re-raise for the console
        def apply(doc):
            doc["meta"]["error"] = "%s: %s" % (type(e).__name__, e)

        audit.status.update(apply)
        raise
    finally:
        try:
            if httpd and o.keep_serving and audit.status.doc["meta"].get("finished"):
                audit.status.log("run complete - UI (with the live view) stays "
                                 "on http://127.0.0.1:%d/ (Ctrl+C to stop)" % o.port)
                try:
                    while True:
                        time.sleep(3600)
                except KeyboardInterrupt:
                    pass
        finally:
            audit.live.shutdown()


if __name__ == "__main__":
    main()
