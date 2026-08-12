# OSRS Stylizer — AI Optimization Loop for Retro Low-Poly Conversion

This tool takes a modern, high-polygon 3D model and automatically converts it
into a low-poly asset that looks like it belongs in Old School RuneScape. It
does this with a **heuristic search**: a mutation engine (Blender) applies
retro-fication with tunable strength, and two learned "judges" score every
attempt. An optimizer (Optuna) then searches for the parameter settings that
score best on both judges at once.

If you have never trained a model before, read the **Concepts** section first —
everything else builds on it.

---

## Concepts (for first-timers)

**The image classifier (the "style judge").** A classifier is a neural network
that looks at an image and outputs a probability for each of a fixed set of
categories. Ours has exactly two categories: *"this render looks like an OSRS
asset"* and *"this render looks like a standard modern 3D model."* We teach it
the difference by showing it thousands of labeled example images (the
*training set*). We use **ResNet-18**, a small, well-understood network design
from 2015 — 11.2 million parameters, ~45 MB on disk — and we start from a
version already pre-trained on millions of everyday photos (ImageNet), so it
only has to *fine-tune* its existing visual knowledge to our two categories.
That is why a few thousand images and a few epochs are enough.

**Training, epochs, validation.** Training shows the network batches of
images, measures how wrong its guesses are (the *loss*), and nudges the
weights to be less wrong. One pass through the whole training set is an
*epoch*. Before training starts we set aside a random 10% of images the
network will never train on (the *validation set*); accuracy on those held-out
images is the honest measure of whether the network learned the concept
rather than memorizing its training images.

**The content judge (CLIP).** If we optimized for "looks OSRS" alone, the
search would discover the degenerate answer: crush any model into a
flat-shaded blob — maximally retro, but no longer *your* model. To prevent
this we add a second judge: **CLIP**, a network OpenAI trained to embed
images into vectors where similar-looking/similar-meaning images land close
together. We embed the original render and the stylized render and measure
the cosine similarity between the two vectors. High similarity = still
recognizably the same object. We do not train CLIP; we use it off the shelf.

**Multi-objective optimization and the Pareto front.** The two scores pull in
opposite directions — more retro-fication raises the style score and lowers
the content score. There is no single "best" answer, only a family of best
trade-offs: the **Pareto front** — every parameter setting where you cannot
improve one score without giving up some of the other. Optuna's NSGA-II
sampler (a genetic algorithm) finds this front, and you pick the point on it
that looks right to you.

---

## Directory structure

```
tools/osrs_stylizer/
├── README.md                  # this file
├── requirements.txt           # pip deps for the Windows/driver side
├── setup_wsl_env.sh           # one-time WSL setup (venv + CPU PyTorch)
├── train_in_wsl.sh            # stages the dataset & launches training in WSL
│
│   # ---- dataset generation ----
├── gen_osrs_corpus.py         # Class 1: renders OSRS models via the repo engine
├── fetch_highpoly.py          # downloads free/open high-poly source models
├── render_highpoly_corpus.py  # (runs INSIDE Blender) renders one model
├── gen_highpoly_corpus.py     # Class 2 batch driver: loops Blender over models
│
│   # ---- the four pipeline components ----
├── train_classifier.py        # Component 1a: trains the style judge
├── osrs_scorer.py             # Component 1b: get_osrs_score() inference
├── modifier.py                # Component 2: Blender mutation + render (headless)
├── content_scorer.py          # Component 3: get_content_score() via CLIP
├── optimize.py                # Component 4: Optuna multi-objective loop
│
│   # ---- the Content Preserver (trained replacement for CLIP) ----
├── gen_preserver_triplets.py  # builds the triplet corpus via modifier.py
├── preserver_dataset.py       # TripletDataset + shared image transforms
├── preserver_model.py         # Siamese ResNet-18 embedder (unit sphere)
├── train_preserver.py         # triplet-loss training loop
├── preserver_scorer.py        # get_identity_score() 0..100 inference
│
│   # ---- the RS2012 backport regime (engine-only judge + search driver) ----
├── gen_engine_poly_corpus.py  # both classes rendered via rs2012_model_view
├── osrsify.py                 # reduce/sculpt search over real .ob3 parts
│
│   # ---- generated locally, NEVER committed (.gitignore) ----
├── data/osrs/                 # 20,336 OSRS render tiles (Class 1)
├── data/highpoly/             # 19,669 high-poly render tiles (Class 2)
├── data/preserver/            # triplet renders + dataset.csv
├── highpoly_src/              # downloaded OBJ/GLB models + ModelNet10 subset
├── models/osrs_classifier.pt  # the trained style-judge checkpoint
├── models/osrs_engine_judge.pt# engine-only style judge (see below)
├── models/content_preserver.pt# the trained content-judge checkpoint
└── runs/                      # optimization outputs (per study)
```

---

## What each tool does

### `gen_osrs_corpus.py` — build the OSRS training class
Renders raw OSRS `.model` files from `OSRS-Content/osrs239-content/models/`
through **`rs2012_model_view.exe`** — this repo's own software-rendered engine
harness (build it with `make -C src rs2012-model-view`). This matters: the
corpus shows the *actual* OSRS rasterizer output (flat shading, palette
lighting, no anti-aliasing), not an approximation.

- Samples models deterministically (seed 1337, `--count`; this run kept 5,812
  models), skipping files < 600 bytes (fragment meshes with no style signal).
- Each model renders a 4-angle contact sheet (yaws 0°/90°/180°/270°, pitch 128
  ≈ 22° down, 256×256 tiles, background `#808080`), which is split into 4
  separate PNGs.
- Tiles that are ≥98% background (a single quad seen edge-on) are discarded.
- **Output:** `data/osrs/model_<id>_y<0-3>.png` — 20,336 tiles from 5,812
  models this run.

### `fetch_highpoly.py` — download the counter-class source models
Standard library only. Downloads permissively-licensed models (this run:
2,632 objects total):
- 21 classic research meshes (OBJ) from `alecjacobson/common-3d-test-models`
  — Stanford bunny, armadillo, Lucy, Nefertiti, XYZ-RGB dragon, etc.
- 117 textured PBR showcase assets (GLB) from `KhronosGroup/glTF-Sample-Models`
  — DamagedHelmet, Duck, Lantern, Fox, etc.
- ModelNet10 (Princeton): thousands of category-labeled CAD meshes for volume,
  extracted under `highpoly_src/modelnet/` (`--modelnet-limit 0` to skip).
- **Output:** `highpoly_src/*.obj|.glb` + `highpoly_src/modelnet/*.obj`.

### `gen_highpoly_corpus.py` + `render_highpoly_corpus.py` — build Class 2
The driver (`gen_highpoly_corpus.py`, run on Windows Python) launches headless
Blender (a native install when one is found, else WSL) over the source models;
the worker script
(`render_highpoly_corpus.py`) runs inside Blender and renders 36 views
(12 yaws × 3 elevations, deterministic per-model zoom jitter), **smooth-shaded**
— the visual opposite of OSRS's flat shading. Untextured scans get a
deterministic per-model color so the class isn't uniformly white.

Renders use a transparent film and the driver composites them onto the exact
same `#808080` gray as Class 1. This is deliberate: **the two classes must
differ only in style** — same resolution, same background, similar framing —
or the classifier will learn a shortcut (e.g. "gray background = OSRS") instead
of the actual art style. Frames under 2% coverage are purged, mirroring Class 1.
- **Output:** `data/highpoly/<name>_p<pitch>_y<yaw>.png` — 19,669 tiles from
  2,632 objects this run.

### `train_classifier.py` — train the style judge (Component 1a)
Fine-tunes ImageNet-pretrained ResNet-18 for 2 classes.
- Split: 90% train / 10% validation (seeded, so reruns get the same split).
- Augmentation on the training side only (random crops, flips, color jitter)
  so the judge tolerates framing/lighting differences at inference time.
- Tracks validation accuracy each epoch and keeps the **best-on-validation**
  weights, not the last epoch's.
- **Output:** `models/osrs_classifier.pt` — a dict with `state_dict` (weights),
  `class_to_idx` (which output neuron means "osrs"), and `val_acc`.

### `osrs_scorer.py` — use the style judge (Component 1b)
`get_osrs_score(image_path) -> 0..100`: the classifier's softmax confidence
that an image is OSRS-styled, as a percentage. Loads the checkpoint once and
caches it, so calling it thousands of times in the optimizer loop is cheap.
Also runnable directly: `python osrs_scorer.py render.png`.

### `modifier.py` — the mutation engine (Component 2)
Runs inside headless Blender: `blender -b -P modifier.py -- --input m.fbx
--outdir out --decimate 0.05 --grid 0.08 --colors 8`. Pipeline:
1. import (`.obj`/`.fbx`) → join everything into one mesh
2. **decimate** to a fraction of the original face count (collapse mode, which
   also triangulates — OSRS models are triangle soup)
3. **vertex grid quantization** — snap every vertex to a lattice and weld the
   verts that land on the same point; the welding is where the janky retro
   silhouette actually comes from
4. **color quantization** — sample each face corner's color from the original
   textures/materials via UVs, k-means it down to N palette colors, bake as
   vertex colors, strip all materials/textures
5. force **flat shading**
6. export the stylized model as `.glb` (glTF round-trips vertex colors
   reliably; OBJ support for them varies by Blender version)
7. render **front / side / isometric** PNGs (Workbench engine — deterministic
   and CPU-headless-safe)

The special call `--decimate 1.0 --grid 0 --colors 0` is a pass-through: no
mutation, textures kept — used to produce the *baseline* renders that anchor
the content score.

### `content_scorer.py` — the content judge (Component 3)
`get_content_score(original_png, stylized_png) -> cosine similarity` between
CLIP ViT-B/32 embeddings. Same-object renders typically score 0.85–0.99;
unrelated blobs 0.5–0.7. Treat it as a relative signal, not a percentage.

### `optimize.py` — the search loop (Component 4)
Ties it all together. For each Optuna trial: pick parameters → run `modifier.py`
via subprocess → score the 3 renders with both judges (averaged over views) →
report `(osrs_score, content_score)` with `directions=["maximize","maximize"]`.
Search space (all log-scale, since perceptual impact is multiplicative):
`decimate_ratio` ∈ [0.01, 1.0], `quantization_grid` ∈ [0.01, 0.5],
`color_quantize` ∈ [2, 64]. Failed/degenerate Blender runs are *pruned*
(excluded) rather than scored zero, so they can't poison the front.
- **Output per study:** `runs/<study>/baseline/` (anchor renders),
  `runs/<study>/trials/trial_NNNN/` (renders + `view.glb` per trial),
  `runs/<study>/study.db` (resumable SQLite storage), and
  `runs/<study>/pareto.json` — the final front: every non-dominated trial with
  its params, both scores, and the path to its stylized `.glb`.

### The Content Preserver — a trained content judge (optional)

CLIP is a generalist: it knows two renders show "the same object" but was
never told that palette baking is acceptable while mesh collapse is not. The
Content Preserver is a **Siamese network** (one shared ResNet-18 tower,
~11.4M parameters, projection head to a 256-d unit-sphere embedding) trained
with **triplet loss** on this pipeline's own renders to encode exactly that
distinction:

- `gen_preserver_triplets.py` — for each `highpoly_src/` model (fetched via
  `fetch_highpoly.py` if absent), renders an *anchor* (untouched), a
  *positive* (moderate decimation, ratio 0.15–0.45, sometimes palette-baked)
  and a *negative* (destructive decimation, ratio 0.003–0.015) through
  `modifier.py`, then writes `data/preserver/dataset.csv`. Geometry damage is
  expressed only through the scale-free decimate ratio (never `--grid`, which
  is in absolute units the corpus's wildly varied model scales would break),
  and palette baking appears on both sides of the margin so the network
  learns to ignore it. Sources under 1,500 faces are skipped — "moderate"
  decimation would destroy them and mislabel the triplet.
- `train_preserver.py` — `nn.TripletMarginLoss(margin=1.0, p=2)`, Adam +
  cosine schedule, train/val split **by source model** (so validation
  measures unseen objects, not memorized ones), keeps the checkpoint with the
  best validation *triplet accuracy* (fraction of triplets ranked correctly).
- `preserver_scorer.py` — `get_identity_score(anchor_png, candidate_png)
  -> 0.0..100.0` (100 = identical embedding, 0 = maximal destruction; the
  linear map of unit-sphere distance). Checkpoint loading and anchor
  embeddings are cached, so hammering it from the optimizer loop is cheap.

```bash
python gen_preserver_triplets.py --limit 200      # ~600 Blender passes
python train_preserver.py --csv data/preserver/dataset.csv
python optimize.py --input your_model.fbx --content-scorer preserver
```

CLIP remains the default content judge (`--content-scorer clip`); the
preserver is opt-in per study, and `pareto.json` keeps the same 0–100 scale
either way.

**Trained checkpoint (run of 2026-08-11).** Corpus: 7,203 triplets from
2,077 source models (showcase + ModelNet10 + ModelNet40 + Google Scanned
Objects). Best epoch by validation triplet accuracy: **98.19%** over 720
triplets from 240 held-out source models. Score distributions on that split:
anchor-positive mean 88.7 / median 91.5, anchor-negative mean 55.4 /
median 56.4, anchor-vs-itself 100.0. Calibration guidance: the *ranking* is
what's reliable (98% of triplets ordered correctly) — use it to compare
candidate decimations of the same model, and treat ≥85 as "identity safe",
≤60 as "identity broken". Absolute scores overlap in the middle band partly
by construction: brutally decimating an already-simple shape (a curtain
plane, a boxy dresser) genuinely preserves its look, so those "negatives"
legitimately score high.

---

## The training set (this run)

| | Class 1: `osrs` | Class 2: `highpoly` |
|---|---|---|
| images | 20,336 | 19,669 |
| source | 5,812 sampled OSRS cache models | 2,632 objects (ModelNet10 + showcase/research models) |
| renderer | `rs2012_model_view.exe` (repo engine) | Blender 4.2.23 Workbench (native, headless) |
| views/model | 4 yaws × 1 pitch | 12 yaws × 3 pitches |
| shading | flat, no AA (authentic engine output) | smooth, studio-lit, AA |
| size / bg | 256×256 on `#808080` | 256×256 on `#808080` (composited) |

Split at train time: 36,005 train / 4,000 validation (seed 1337), with
inverse-frequency class weights (1.017 highpoly / 0.984 osrs).

Known biases worth understanding:
- The judge generalizes best on renders that resemble these two distributions
  — which is exactly how it's used in the loop (both scored render types come
  from the same two pipelines). See the Demo 2 benchmark below for the sharp
  edge of this: the judge keys on renderer fingerprint as much as geometry.
- Classes are near 1:1 in this run, but per-class validation accuracy is still
  printed every epoch — watch that neither class is being sacrificed.

## Benchmarks (run of 2026-08-11)

Trained natively on Windows (CPU, Python 3.14), 3 epochs over the 40,005-image
corpus:

| epoch | train loss | val acc | `highpoly` acc | `osrs` acc |
|---|---|---|---|---|
| **1** | 0.0302 | **98.925%** | 97.946% | 99.900% |
| 2 | 0.0096 | 97.200% | 94.439% | 99.950% |
| 3 | 0.0031 | 97.775% | 95.541% | 100.000% |

The checkpoint stores the best-on-validation epoch (epoch 1, **98.925%**);
epochs 2–3 kept driving train loss down while validation slipped — mild
overfitting, which best-on-val checkpointing exists to absorb.

`example_usage.py` demo 1 (200 images per class sampled from the whole corpus
— a smoke test, since the sample includes training images):

| | `osrs` sample | `highpoly` sample |
|---|---|---|
| mean OSRS score | 99.97 | 3.91 |
| median | 100.00 | 0.00 |
| correct at the >50 threshold | 200/200 | 194/200 |

Overall agreement 394/400 (98.5%), mean class separation +96.1 points.

Demo 2 (DamagedHelmet.glb through the modifier sweep, both judges, mean of 3
views):

| step | decimate | grid | colors | faces | osrs% | content% |
|---|---|---|---|---|---|---|
| original | 1.00 | 0.000 | – | 15,452 | 0.00 | 100.00 |
| light | 0.50 | 0.010 | 32 | 7,309 | 0.00 | 95.35 |
| medium | 0.15 | 0.030 | 16 | 2,090 | 0.00 | 94.14 |
| heavy | 0.05 | 0.060 | 8 | 724 | 0.00 | 86.77 |
| extreme | 0.02 | 0.120 | 4 | 255 | 0.00 | 77.64 |

One lesson per judge:
- **Content judge: works as designed.** Monotonically falls as the mesh gets
  cruder, still 77.6 at 255 faces — recognizably the same helmet throughout.
- **Style judge: saturated at 0 across the whole Blender sweep** — even the
  255-face flat-shaded step. At 98.9% validation accuracy it has learned the
  *renderer's* fingerprint (Workbench anti-aliasing, studio lighting, tonal
  response) as hard as the geometry style, so any Blender render lands at the
  highpoly extreme regardless of poly count. Renders from the repo engine are
  in-distribution and cleanly separated (demo 1). Use the judge to compare
  engine renders against engine renders — which is how the RS2012 backport
  pipeline uses it — and treat demo 2's osrs% column as a documented caveat
  of the Blender loop, not a bug in the checkpoint.

To re-measure yourself: rerun `train_in_wsl.sh` (below) — every epoch prints
`val_acc` plus per-class accuracy, and the checkpoint stores its `val_acc`.
Quick sanity check of a saved checkpoint against a handful of images:

```bash
# scores should be high (near 100) for data/osrs images, low for data/highpoly
python osrs_scorer.py data/osrs/model_10_y0.png data/highpoly/Duck_p0_y02.png
```

---

## The RS2012 backport regime: `osrsify.py`

The Demo 2 lesson above (the classifier keys on renderer fingerprint) sets up
this half of the tool: making **real imported `.ob3` parts** — e.g. the RS2012
Queen Black Dragon backport — *look* OSRS while keeping their rigging and
animations working. Everything here scores renders from the repo's own
`rs2012_model_view`, never Blender.

### The engine-only judge

`gen_engine_poly_corpus.py` renders BOTH classes through `rs2012_model_view`
(4 yaws, pitch 128, 256px on `#808080` — the classic corpus recipe): class
`osrs` = classic small cache natives, class `highpoly` = the imported
high-poly lane (`models/ported/**` plus the densest natives). Because the
renderer is identical for both, the only separable signal left is the *mesh
style itself* — poly density, silhouette, faceting.

Benchmarks (run of 2026-08-11, 7,583 train / 842 val tiles, 3 epochs, CPU):

| epoch | val acc | `highpoly` acc | `osrs` acc |
|---|---|---|---|
| 1 | 87.055% | — | — |
| 2 | 91.093% | — | — |
| **3** | **95.131%** | 95.211% | 95.072% |

Discrimination check on renders it never trained on (softmax %):
classic natives score **99.5–99.7**, dense natives **0.0–0.4**, the QBD part
70260 **0.02** — and decimating that part moves it **0.24** at half vertices,
**12.39** at a quarter. The gradient is alive, but softmax squashes the
interesting range, so `osrsify.py` optimizes the **logit margin**
(`logit[osrs] − logit[highpoly]`) instead.

### The mesh tools (C, in `src/engine/proctex/test/`)

- **`rs2012_model_defight`** (`make -C src rs2012-model-defight`) — the
  Z-FIGHTING pre-pass. The z-buffer renderer drops face priorities by design
  (a priority pins a face into a draw band regardless of depth — the
  painter's-algorithm crutch the depth test replaces), so coplanar decal
  layers that priorities kept apart tie in depth and speckle (the QBD's neck
  markings). The tool renders the merged part set from a yaw × pitch view
  grid (default 16 × 3) with a perspective-correct top-two depth raster,
  collects face pairs that tie within `--fight-eps` and share no vertex
  position (mesh seams and double-sided walls are not fights), and pushes
  the authored-on-top layer — higher priority wins; equal priorities fall
  back to smaller-area-on-top — outward along its normal, escalating through
  a `--deltas` ladder and re-rendering every view until the ties die.
  Vertices shared with faces that stay put are duplicated with their bone
  label and animaya skin copied, so the rig and every old animation still
  drive the moved layer, and texture projection vertices are never moved.
  Outputs are re-encoded in the input's own format, decode-back verified,
  and summarized in a JSON report (pairs, layers, before/after tie pixels).
- **`rs2012_model_decimate`** (`make -C src rs2012-model-decimate`) — the
  REDUCE half: rig-preserving stochastic edge collapse (quadric error +
  same-rig-label constraint + attribute penalties, jittered candidate pool,
  seedable). Emits an honest version-1 `.ob3` (version-13+ inputs get their
  4x coordinate scale baked down) **plus a mapping JSON**
  (`vertex_map`/`vertex_merged_into`/`face_map`, per-label counts) that ports
  the old animations onto the reduced mesh. Every output is decode-back
  verified.
- **`rs2012_model_nudge`** — the SCULPT half (shared with the z-fighting
  audit): per-rig-group `inflate`/`translate`, per-face-patch normal moves,
  priority ops, and a `quantize <group|-1> <step>` op that snaps vertices to
  a coarse grid as a final pass — the hand-built-on-a-lattice look of real
  OSRS meshes.

### The search driver

```bash
cd tools/osrs_stylizer
python osrsify.py --model <part1.ob3> --model <part2.ob3> \
    --seq 16715 --seq 16714 --seqcfg <rs2012.seq> --cache <dat2-dir> \
    --regime reduce --time-budget 14400
python osrsify.py ... --regime sculpt     # or: --regime both
```

Before either regime starts, the driver runs the **defight pre-pass**
(`--defight auto|on|off`, auto = on when `--zbuffer`): the base models are
repaired in `<run>/defight/` and the fixed parts become the baseline, every
candidate's input, and the `base_models` recorded in `results.json`. Fixing
z-fighting *before* decimation matters — the collapse search would otherwise
inherit (and could freely reshuffle) depth ties the judges can't see. A
defight failure is logged and the run continues on the original models.

Two regimes over one judge:

- **reduce**: a ladder of vertex fractions × random seeds through the
  decimator, then seed/fraction refinement around the best candidate for the
  rest of the time budget.
- **sculpt**: simulated annealing over nudge move lists (per-group inflate
  amounts + a global quantize step).

The judge, per candidate:

1. **Style** — mean logit margin of the engine judge over the covered views
   of a 4-angle bind render; fitness is the margin *gain* over baseline.
2. **Identity** — `content_preserver.pt` score between baseline and candidate
   tiles, paired per yaw (a yaw that goes empty scores 0 — that IS collapse).
   Hard gate (default ≥ 55) plus a soft fitness term.
3. **Region close-ups** — on big models the whole-body camera leaves detail
   regions (the QBD's face is a few dozen pixels in the full frame) too small
   for either judge to defend, so deletion there went unpunished. The
   decimator's mapping JSON reports per-rig-label vertex counts, centroids,
   and bounding boxes; osrsify picks up to `--regions` (8) of the *smallest*
   labels with ≥ `--region-min-verts` (30) whose extent is well under the
   model's (big labels add nothing over the global render), banks baseline
   close-ups under a fixed `--focus`/`--radius` camera, and re-renders the
   same regions for every candidate. Scored by the preserver only (close-ups
   are far outside the style judge's training distribution): hard gate on the
   worst region (default ≥ 45) plus a soft fitness term on the mean.
4. **Animation sweep** — every sampled frame of every sequence is prescanned
   with `--pose-stats-only`; one camera per sequence is fitted to the union
   of the *baseline's* posed bounding boxes (the audit tool's shared-camera
   protocol) and reused for every candidate, so coverage and identity are
   comparable across candidates. Heuristic gates: the frame must decode, the
   posed/bind size ratio must stay within 2× of the baseline's (explosion /
   collapse detection), per-view silhouette coverage must stay inside a band
   (default 0.5–2.0×), and the posed identity score must clear a floor
   (default ≥ 50). **No pixel diffing anywhere** — the audit's pixel verdict
   ("always enable z-buffering") optimizes nothing about style.

Long runs are the intended shape (`--time-budget` seconds): `results.json`
in the run directory is atomically rewritten after every candidate, every
rejected candidate stays recorded with its reason, and the best passing
candidate's `.ob3` + mapping/moves files are mirrored to `<run>/best/`.

### Watching a run: `watch_osrsify.py`

```bash
python watch_osrsify.py            # serves http://localhost:8765/, watches runs/osrsify_*
```

Stdlib-only dashboard over the run directories: per-run summary strip with a
fraction-range filter, fitness chart, best/recent candidate cards, and a
detail view per candidate (bind renders, region close-ups and posed frames
paired against the baseline's).

The detail view's **Open in viewer** button (and the strip's *baseline
viewer* link) opens a live orbit viewer: `rs2012_model_view --wire-out`
serializes the candidate's merged, lit model — and any sequence's frames
with their real tick delays — into the entity viewer's `ev_wire` format, and
the page renders it with toridraw compiled to WebAssembly
(`make -C tools/entity_viewer wasm`, needs emcc). Drag to orbit, wheel to
zoom, pick a sequence to play it client-accurately in the browser; no server
round-trip per frame. Wire files are cached under `<run>/wire/`.

---

## Reproducing the model from scratch

This run was done natively on Windows: Windows Python (3.14, CPU torch) runs
everything, with a portable native Blender for Class 2 and the repo engine for
Class 1. The WSL scripts (`setup_wsl_env.sh`, `train_in_wsl.sh`) remain a
supported alternative when no native Blender/Python is available.

```powershell
# 0. One-time: the repo's engine viewer must exist
#    (already built in this repo at src/build_win64_opt/rs2012_model_view.exe)
make -C src rs2012-model-view

# 1. One-time: WSL tooling (Blender for rendering, numpy for its glTF importer)
wsl -u root -e sh -c "apt-get update && apt-get install -y blender python3-venv python3-pip python3-numpy"

# 2. One-time: training environment (venv + CPU PyTorch in /root/osrs_venv)
wsl -u root -e sh tools/osrs_stylizer/setup_wsl_env.sh

# 3. Class 1 — render 600 OSRS cache models through the repo engine (~2 min)
python tools/osrs_stylizer/gen_osrs_corpus.py --count 600

# 4. Class 2 — download 31 open models, render 36 views each in WSL Blender
python tools/osrs_stylizer/fetch_highpoly.py
python tools/osrs_stylizer/gen_highpoly_corpus.py     # idempotent; rerun-safe

# 5. Train (this run: 3 epochs on CPU, native Windows Python)
python tools/osrs_stylizer/train_classifier.py --epochs 3
#    (WSL alternative: wsl -u root -e sh tools/osrs_stylizer/train_in_wsl.sh)
# -> writes tools/osrs_stylizer/models/osrs_classifier.pt
```

Determinism notes: corpus sampling, train/val split, and view jitter are all
seeded, so a from-scratch rerun produces the same dataset and split. CPU
training itself has minor nondeterminism (thread scheduling), so validation
accuracy can vary by a fraction of a percent between reruns.

The generated `data/`, `highpoly_src/`, `models/`, and `runs/` directories are
all gitignored — **do not commit them**; anyone can regenerate them with the
five steps above.

---

## Using the trained model

**As a library** (what `optimize.py` does):

```python
from osrs_scorer import get_osrs_score
score = get_osrs_score("some_render.png")        # 0..100, higher = more OSRS
score = get_osrs_score("r.png", checkpoint_path="models/osrs_classifier.pt")
```

**From the command line:**

```bash
python osrs_scorer.py render1.png render2.png    # prints one score per image
```

**Inside the full optimization loop** — this is the intended use. The judge
scores every mutation attempt, CLIP guards content, Optuna searches:

```bash
# Windows: point --blender at WSL's blender or a native install
python optimize.py --input your_model.fbx --n-trials 60 \
    --classifier models/osrs_classifier.pt
```

When it finishes, open `runs/<study>/pareto.json`, look at each front trial's
renders (`render_dir`), and take the `.glb` of the trade-off you like. The
study is resumable — rerunning with the same study name adds trials — and
browsable live with `optuna-dashboard sqlite:///runs/<study>/study.db`.

Caveats when scoring arbitrary images: the judge was trained on 256px renders
of single objects on flat gray. Scores on screenshots, scenes, or photos are
extrapolation and shouldn't be trusted; scores on renders produced by
`modifier.py`/`gen_*_corpus.py` are in-distribution and meaningful.

---

## Design decisions worth knowing

- **Workbench render engine** everywhere in Blender: deterministic, fast,
  needs no GPU/GL headlessly (EEVEE headless is flaky), and its faceted studio
  look is a reasonable stand-in for how the game shades.
- **Same camera/background pipeline for baseline and trials** so the content
  score measures the mesh change, not rendering differences.
- **Renders are scored, never the mesh directly** — both judges only ever see
  images, which is what "looks OSRS" actually means.
- **Prune, don't zero** failed mutations in the optimizer: a crashed Blender
  run is missing data, not evidence of a bad parameter region.

## Troubleshooting

- `blender: command not found` in WSL → step 1 above.
- glTF import fails with `No module named 'numpy'` → `apt-get install
  python3-numpy` (Ubuntu's Blender uses system Python).
- Training is slow → it's CPU-only here (expect hours, not minutes, for the
  full 40k-image corpus); a CUDA machine drops that to minutes. The scripts
  auto-use CUDA when `torch.cuda.is_available()`.
- Renders through `/mnt/c` are slow → expected (9p filesystem);
  `train_in_wsl.sh` stages the dataset to `/root/osrs_data` for this reason.
- A model renders empty in Class 2 → its transform lived on a glTF parent
  node; `render_highpoly_corpus.py` clears parenting with keep-transform
  before applying transforms, which fixed this for the Duck. If it recurs for
  a new model, check for exotic node hierarchies.
