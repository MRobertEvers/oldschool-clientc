#!/usr/bin/env python3
"""Preserver component 1: build the triplet training corpus for the Content
Preserver (the trained replacement for the off-the-shelf CLIP content judge).

For every source model under highpoly_src/ (downloaded by fetch_highpoly.py —
run automatically here if the folder is empty), three headless-Blender passes
through modifier.py produce:

    anchor    decimate=1.0 grid=0 colors=0   the untouched model, textures kept
    positive  moderate decimation (+ maybe palette bake) — the "safe" stylize
    negative  extreme decimation — deliberate mesh collapse / identity loss

Each pass renders the front/side/iso views, so one model yields up to three
(anchor, positive, negative) rows. The corpus teaches the Siamese network the
one distinction the optimizer needs: "stylized but still the same object"
(pull together) vs "topologically destroyed" (push apart).

Two deliberate parameter choices:
- The geometry axis uses ONLY the decimate ratio, never --grid: the grid is in
  absolute object units, and source models (research scans vs ModelNet CAD)
  differ in scale by orders of magnitude, so no fixed grid value is "safe" or
  "destructive" across the corpus. The decimate ratio is scale-free.
- --colors is randomized on positives AND negatives alike, so palette baking /
  flat shading appears on both sides of every margin and carries no signal:
  the network learns to *ignore* the intended style changes and score only
  structural identity.

Outputs (gitignored, like every generated corpus):
    data/preserver/renders/<model>/{anchor,positive,negative}_{front,side,iso}.png
    data/preserver/renders/<model>/params.json     (provenance per model)
    data/preserver/dataset.csv                     (anchor_path,positive_path,negative_path)

Idempotent: models whose nine renders already exist are skipped, so rerun
freely after adding sources or after a partial failure.

Usage:
    python gen_preserver_triplets.py                       # everything
    python gen_preserver_triplets.py --limit 200 --workers 4
    python gen_preserver_triplets.py --blender wsl
"""

import argparse
import concurrent.futures
import csv
import json
import os
import random
import re
import subprocess
import sys
import time

from blender_locate import Blender

HERE = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.join(HERE, "highpoly_src")
OUT_DIR = os.path.join(HERE, "data", "preserver")
RENDER_DIR = os.path.join(OUT_DIR, "renders")
CSV_PATH = os.path.join(OUT_DIR, "dataset.csv")

VIEWS = ("front", "side", "iso")          # must match modifier.py's render names
ROLES = ("anchor", "positive", "negative")
MODEL_EXTS = (".obj", ".fbx", ".glb", ".gltf")   # what modifier.py can import
BLENDER_TIMEOUT_S = 900

# Below this face count the source is already low-poly, and "moderate"
# decimation would itself destroy it — the triplet's labels would be wrong.
DEFAULT_MIN_FACES = 1500


def sample_params(stem: str, seed: int) -> dict[str, dict[str, float | int]]:
    """Per-model mutation parameters, deterministic in (seed, model name) so
    reruns regenerate byte-identical corpora. Colors are sampled independently
    per role — see the module docstring for why that matters."""
    rng = random.Random(f"{seed}:{stem}")

    def colors() -> int:
        # 0 keeps textures; the rest match the optimizer's palette range.
        return rng.choice([0, 8, 16, 32])

    return {
        "anchor": {"decimate": 1.0, "grid": 0.0, "colors": 0},
        "positive": {"decimate": round(rng.uniform(0.15, 0.45), 4),
                     "grid": 0.0, "colors": colors()},
        "negative": {"decimate": round(rng.uniform(0.003, 0.015), 4),
                     "grid": 0.0, "colors": colors()},
    }


def run_modifier(blender: Blender, input_path: str, outdir: str, *,
                 decimate: float, grid: float, colors: int, prefix: str,
                 resolution: int) -> str:
    """One headless-Blender pass through modifier.py. Returns Blender's stdout
    (the driver parses the imported face count from it). Raises on failure —
    including the exit-0-after-Python-exception case, caught by checking that
    every expected render actually landed on disk."""
    modifier = os.path.join(HERE, "modifier.py")
    cmd = [*blender.launcher, "-b", "--factory-startup",
           "-P", blender.path(modifier), "--",
           "--input", blender.path(input_path),
           "--outdir", blender.path(outdir),
           "--decimate", str(decimate),
           "--grid", str(grid),
           "--colors", str(colors),
           "--resolution", str(resolution),
           "--prefix", prefix]
    result = subprocess.run(cmd, capture_output=True, text=True,
                            timeout=BLENDER_TIMEOUT_S)
    missing = [v for v in VIEWS
               if not os.path.isfile(os.path.join(outdir, f"{prefix}_{v}.png"))]
    if result.returncode != 0 or missing:
        raise RuntimeError(
            f"Blender failed (rc={result.returncode}, missing={missing}).\n"
            f"--- stdout tail ---\n{result.stdout[-1500:]}\n"
            f"--- stderr tail ---\n{result.stderr[-1500:]}")
    return result.stdout


def imported_faces(blender_stdout: str) -> int | None:
    """Face count of the model as imported, from modifier.py's own log line
    ('[modifier] imported: N faces, M verts')."""
    m = re.search(r"\[modifier\] imported: (\d+) faces", blender_stdout)
    return int(m.group(1)) if m else None


def generate_one(blender: Blender, model_path: str, stem: str, seed: int,
                 resolution: int, min_faces: int) -> tuple[str, str]:
    """Render the full anchor/positive/negative set for one model.
    Returns (status, message) where status is 'done', 'cached', or 'skipped'.
    `stem` names the render directory — it is NOT always the filename stem,
    because GSO models are all literally called model.obj."""
    outdir = os.path.join(RENDER_DIR, stem)
    expected = [os.path.join(outdir, f"{role}_{view}.png")
                for role in ROLES for view in VIEWS]
    if all(os.path.isfile(p) for p in expected):
        return "cached", stem
    os.makedirs(outdir, exist_ok=True)

    params = sample_params(stem, seed)

    # Anchor first: its log tells us the true face count, which gates whether
    # "moderate decimation" is actually safe for this model (see DEFAULT_MIN_FACES).
    stdout = run_modifier(blender, model_path, outdir,
                          **params["anchor"], prefix="anchor",
                          resolution=resolution)
    faces = imported_faces(stdout)
    if faces is not None and faces < min_faces:
        # Remove the partial output so the CSV scan and idempotency check
        # never see a half-generated model directory. Best-effort: on Windows
        # a just-written PNG can still be held open by the AV scanner/indexer
        # (WinError 32); a leftover anchor render is harmless because the
        # cache check above demands ALL nine renders, so this model can never
        # masquerade as complete.
        for p in expected:
            for attempt in range(3):
                try:
                    if os.path.isfile(p):
                        os.remove(p)
                    break
                except OSError:
                    time.sleep(0.5 * (attempt + 1))
        return "skipped", f"{stem} ({faces} faces < {min_faces})"

    for role in ("positive", "negative"):
        run_modifier(blender, model_path, outdir,
                     **params[role], prefix=role, resolution=resolution)

    with open(os.path.join(outdir, "params.json"), "w", encoding="utf-8") as fh:
        json.dump({"source": os.path.basename(model_path), "faces": faces,
                   "seed": seed, "params": params}, fh, indent=2)
    return "done", stem


def source_models(limit: int, seed: int) -> list[tuple[str, str]]:
    """Every importable model under highpoly_src/, as (path, stem) pairs,
    deterministically subsampled to `limit` when set.

    Flat tiers (top level, modelnet/, modelnet40/) use the filename stem —
    ModelNet10/40 share names for their overlapping meshes, which makes the
    render cache dedup them for free. GSO models live one-per-directory
    (OBJ + MTL + textures must stay together) and are all named model.obj,
    so their stem is the prefixed directory name instead."""
    found: list[tuple[str, str]] = []
    for sub in ("", "modelnet", "modelnet40"):
        d = os.path.join(SRC_DIR, sub) if sub else SRC_DIR
        if not os.path.isdir(d):
            continue
        found += [(os.path.join(d, n), os.path.splitext(n)[0])
                  for n in sorted(os.listdir(d))
                  if n.lower().endswith(MODEL_EXTS)
                  and os.path.isfile(os.path.join(d, n))]
    gso = os.path.join(SRC_DIR, "gso")
    if os.path.isdir(gso):
        for name in sorted(os.listdir(gso)):
            obj = os.path.join(gso, name, "meshes", "model.obj")
            if os.path.isfile(obj):
                found.append((obj, f"gso_{name}"))
    if limit and limit < len(found):
        found = sorted(random.Random(seed).sample(found, limit))
    return found


def ensure_sources(modelnet_limit: int) -> None:
    """If highpoly_src/ is empty, run the repo's own fetcher to populate it —
    the corpus tooling already solved model sourcing; no need for a second
    download path here."""
    if source_models(limit=0, seed=0):
        return
    print("highpoly_src/ is empty — running fetch_highpoly.py first...")
    subprocess.run([sys.executable, os.path.join(HERE, "fetch_highpoly.py"),
                    "--modelnet-limit", str(modelnet_limit)], check=True)


def write_csv(csv_path: str) -> int:
    """Scan the render tree and (re)write dataset.csv: one row per view whose
    anchor, positive, AND negative renders all exist. Paths are stored
    relative to the CSV so the corpus survives being staged into WSL."""
    rows = []
    base = os.path.dirname(csv_path)
    if os.path.isdir(RENDER_DIR):
        for stem in sorted(os.listdir(RENDER_DIR)):
            outdir = os.path.join(RENDER_DIR, stem)
            for view in VIEWS:
                paths = [os.path.join(outdir, f"{role}_{view}.png")
                         for role in ROLES]
                if all(os.path.isfile(p) for p in paths):
                    rows.append([os.path.relpath(p, base).replace(os.sep, "/")
                                 for p in paths])
    with open(csv_path, "w", encoding="utf-8", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["anchor_path", "positive_path", "negative_path"])
        writer.writerows(rows)
    return len(rows)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--limit", type=int, default=0,
                    help="max source models to use (0 = all found)")
    ap.add_argument("--workers", type=int, default=4,
                    help="concurrent Blender processes")
    ap.add_argument("--resolution", type=int, default=512,
                    help="render size; 512 matches what optimize.py scores")
    ap.add_argument("--min-faces", type=int, default=DEFAULT_MIN_FACES,
                    help="skip sources below this imported face count")
    ap.add_argument("--modelnet-limit", type=int, default=300,
                    help="passed to fetch_highpoly.py if sources are missing")
    ap.add_argument("--seed", type=int, default=1337)
    ap.add_argument("--blender", default=None,
                    help="path to the Blender executable; 'wsl' forces the "
                         "WSL route. Default: autodetect.")
    args = ap.parse_args()

    blender = Blender.locate(args.blender)
    print(f"blender: {blender.describe()}")

    ensure_sources(args.modelnet_limit)
    models = source_models(args.limit, args.seed)
    if not models:
        raise SystemExit(f"no importable models under {SRC_DIR}")
    print(f"{len(models)} source models "
          f"(3 Blender passes x {len(VIEWS)} views each)")
    os.makedirs(RENDER_DIR, exist_ok=True)

    # Threads, not processes: the real work happens in child Blender
    # processes, the driver just shepherds subprocess calls.
    counts = {"done": 0, "cached": 0, "skipped": 0, "failed": 0}
    with concurrent.futures.ThreadPoolExecutor(args.workers) as pool:
        futures = {pool.submit(generate_one, blender, path, stem, args.seed,
                               args.resolution, args.min_faces): path
                   for path, stem in models}
        for i, fut in enumerate(concurrent.futures.as_completed(futures), 1):
            model = futures[fut]
            try:
                status, msg = fut.result()
            except (RuntimeError, subprocess.TimeoutExpired, OSError) as exc:
                # OSError: transient Windows file locks etc. — one lost model
                # must never abort the whole multi-hour generation run.
                counts["failed"] += 1
                print(f"  FAILED {os.path.basename(model)}: "
                      f"{str(exc).splitlines()[0]}", file=sys.stderr)
                continue
            counts[status] += 1
            if status == "skipped":
                print(f"  skipped {msg}")
            if i % 10 == 0 or i == len(models):
                print(f"  progress: {i}/{len(models)} models", flush=True)

    n_rows = write_csv(CSV_PATH)
    print(f"done: {counts['done']} rendered, {counts['cached']} cached, "
          f"{counts['skipped']} skipped (low-poly), {counts['failed']} failed")
    print(f"wrote {n_rows} triplets -> {CSV_PATH}")
    if n_rows == 0:
        raise SystemExit("no triplets generated — check the failures above")


if __name__ == "__main__":
    main()
