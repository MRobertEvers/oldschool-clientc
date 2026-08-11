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
│   # ---- generated locally, NEVER committed (.gitignore) ----
├── data/osrs/                 # 2,105 OSRS render tiles (Class 1)
├── data/highpoly/             # 1,108 high-poly render tiles (Class 2)
├── highpoly_src/              # 31 downloaded OBJ/GLB source models
├── models/osrs_classifier.pt  # the trained checkpoint
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

- Samples 600 models deterministically (seed 1337), skipping files < 600 bytes
  (fragment meshes with no style signal).
- Each model renders a 4-angle contact sheet (yaws 0°/90°/180°/270°, pitch 128
  ≈ 22° down, 256×256 tiles, background `#808080`), which is split into 4
  separate PNGs.
- Tiles that are ≥98% background (a single quad seen edge-on) are discarded.
- **Output:** `data/osrs/model_<id>_y<0-3>.png` — 2,105 tiles from this run.

### `fetch_highpoly.py` — download the counter-class source models
Standard library only. Downloads 31 permissively-licensed models:
- 21 classic research meshes (OBJ) from `alecjacobson/common-3d-test-models`
  — Stanford bunny, armadillo, Lucy, Nefertiti, XYZ-RGB dragon, etc.
- 10 textured PBR showcase assets (GLB) from `KhronosGroup/glTF-Sample-Models`
  — DamagedHelmet, Duck, Lantern, Fox, etc.
- **Output:** `highpoly_src/*.obj|.glb`.

### `gen_highpoly_corpus.py` + `render_highpoly_corpus.py` — build Class 2
The driver (`gen_highpoly_corpus.py`, run on Windows Python) launches headless
Blender **inside WSL** once per source model; the worker script
(`render_highpoly_corpus.py`) runs inside Blender and renders 36 views
(12 yaws × 3 elevations, deterministic per-model zoom jitter), **smooth-shaded**
— the visual opposite of OSRS's flat shading. Untextured scans get a
deterministic per-model color so the class isn't uniformly white.

Renders use a transparent film and the driver composites them onto the exact
same `#808080` gray as Class 1. This is deliberate: **the two classes must
differ only in style** — same resolution, same background, similar framing —
or the classifier will learn a shortcut (e.g. "gray background = OSRS") instead
of the actual art style. Frames under 2% coverage are purged, mirroring Class 1.
- **Output:** `data/highpoly/<name>_p<pitch>_y<yaw>.png` — 1,108 tiles.

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

---

## The training set (this run)

| | Class 1: `osrs` | Class 2: `highpoly` |
|---|---|---|
| images | 2,105 | 1,108 |
| source | 600 sampled OSRS cache models | 31 downloaded OBJ/GLB models |
| renderer | `rs2012_model_view.exe` (repo engine) | Blender 4.0.2 Workbench (WSL) |
| views/model | 4 yaws × 1 pitch | 12 yaws × 3 pitches |
| shading | flat, no AA (authentic engine output) | smooth, studio-lit, AA |
| size / bg | 256×256 on `#808080` | 256×256 on `#808080` (composited) |

Split at train time: 2,892 train / 321 validation (seed 1337).

Known biases worth understanding:
- Only 31 distinct objects in Class 2 (vs 600 in Class 1). The augmentation
  and view variety mitigate this, but the judge generalizes best on renders
  that resemble these two distributions — which is exactly how it's used in
  the loop (both scored render types come from the same two pipelines).
- Class ratio is ~2:1, so per-class validation accuracy is printed every epoch
  — watch that the minority (`highpoly`) class isn't being sacrificed.

## Benchmarks

Training (10 epochs, CPU, WSL) is running as of 2026-08-11; this table records
the result of that run once it completes:

| metric | value |
|---|---|
| best validation accuracy (overall) | *pending — run in progress* |
| validation accuracy, `osrs` class | *pending* |
| validation accuracy, `highpoly` class | *pending* |

To re-measure yourself: rerun `train_in_wsl.sh` (below) — every epoch prints
`val_acc` plus per-class accuracy, and the checkpoint stores its `val_acc`.
Quick sanity check of a saved checkpoint against a handful of images:

```bash
# scores should be high (near 100) for data/osrs images, low for data/highpoly
python osrs_scorer.py data/osrs/model_10_y0.png data/highpoly/Duck_p0_y02.png
```

---

## Reproducing the model from scratch

Everything below was done on Windows + WSL2 (Ubuntu 24.04). Windows Python
runs the drivers and rendering of Class 1; WSL runs Blender and training.

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

# 5. Train (stages data to WSL-native disk, then 10 epochs on CPU)
wsl -u root -e sh tools/osrs_stylizer/train_in_wsl.sh
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
- Training is slow → it's CPU-only here (~10 min/epoch on this dataset); a
  CUDA machine drops that to seconds per epoch. The scripts auto-use CUDA
  when `torch.cuda.is_available()`.
- Renders through `/mnt/c` are slow → expected (9p filesystem);
  `train_in_wsl.sh` stages the dataset to `/root/osrs_data` for this reason.
- A model renders empty in Class 2 → its transform lived on a glTF parent
  node; `render_highpoly_corpus.py` clears parenting with keep-transform
  before applying transforms, which fixed this for the Duck. If it recurs for
  a new model, check for exotic node hierarchies.
