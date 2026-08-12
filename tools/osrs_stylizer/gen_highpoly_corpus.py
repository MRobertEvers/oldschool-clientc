#!/usr/bin/env python3
"""Batch driver for the high-poly counter-class corpus (Class 2).

Scales to thousands of source models:
- models are grouped into MANIFEST CHUNKS and each chunk renders inside ONE
  headless Blender process (startup cost paid once per ~40 models, not per
  model), with several chunk processes running concurrently;
- the showcase set (scans + Khronos assets, few models, lots of visual value)
  gets 36 views each, while the ModelNet bulk set (thousands of models) gets
  6 views each — diversity across objects beats more angles of the same one;
- after rendering, every RGBA frame is composited onto the exact 0x808080
  background shared with the OSRS corpus, and near-empty frames (<2%
  coverage) are purged, mirroring the Class 1 rules.

Needs Pillow + numpy on the driver side and a Blender that can run headless.
Blender is located automatically (PATH, the usual Windows install locations,
then WSL); `--blender` overrides, and `--blender wsl` forces the WSL route.
Idempotent: models whose full view set already exists are skipped, so rerun
freely after adding sources or after a partial failure.

Usage:
    python gen_highpoly_corpus.py [--workers 4] [--chunk 40]
    python gen_highpoly_corpus.py --blender "C:\\path\\to\\blender.exe"
"""

import argparse
import concurrent.futures
import os
import subprocess
import sys
import tempfile

import numpy
from PIL import Image

from blender_locate import Blender

HERE = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.join(HERE, "highpoly_src")
OUT_DIR = os.path.join(HERE, "data", "highpoly")
BACKGROUND_RGB = (0x80, 0x80, 0x80)   # must match gen_osrs_corpus.py
MODEL_EXTS = (".obj", ".fbx", ".glb", ".gltf", ".ply", ".stl")
MIN_COVERAGE = 0.02                   # same near-empty rule as Class 1

# (subdir under highpoly_src, yaws, pitches) — view budget per source tier.
SOURCES = [
    ("", 12, [5.0, 22.0, 40.0]),      # showcase: scans + Khronos, 36 views
    ("modelnet", 3, [10.0, 30.0]),    # bulk: ModelNet, 6 views
]


def pending_models(subdir: str, yaws: int, pitches: list[float],
                   existing: set[str]) -> list[str]:
    """Models in a source tier whose full view set is not on disk yet.
    `existing` is the set of PNG names already in OUT_DIR (scanned once)."""
    src = os.path.join(SRC_DIR, subdir) if subdir else SRC_DIR
    if not os.path.isdir(src):
        return []
    todo = []
    for name in sorted(os.listdir(src)):
        path = os.path.join(src, name)
        if not (os.path.isfile(path) and name.lower().endswith(MODEL_EXTS)):
            continue
        stem = os.path.splitext(name)[0]
        have = sum(1 for n in existing
                   if n.startswith(f"{stem}_p") and n.endswith(".png"))
        # A model can legitimately have fewer views than `expected` if some
        # were purged as near-empty — so "any views present" counts as done.
        if have == 0:
            todo.append(path)
    return todo


def run_chunk(blender: Blender, chunk: list[str], yaws: int,
              pitches: list[float], manifest_dir: str,
              chunk_id: int) -> tuple[int, str]:
    """Render one manifest chunk in a single headless Blender process."""
    manifest = os.path.join(manifest_dir, f"chunk_{chunk_id:04d}.txt")
    with open(manifest, "w", encoding="utf-8") as fh:
        fh.write("\n".join(blender.path(p) for p in chunk))

    script = blender.path(os.path.join(HERE, "render_highpoly_corpus.py"))
    cmd = [*blender.launcher, "-b", "--factory-startup",
           "-P", script, "--",
           "--manifest", blender.path(manifest),
           "--outdir", blender.path(OUT_DIR),
           "--yaws", str(yaws),
           "--pitches", *[str(p) for p in pitches]]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=3600)
    # Per-model failures are already logged inside the batch; surface them.
    fails = [ln for ln in (proc.stdout + proc.stderr).splitlines()
             if "FAILED" in ln]
    return len(chunk) - len(fails), "\n".join(fails)


def _flatten_one(path: str) -> tuple[int, int]:
    """Composite one RGBA render onto the shared gray, then keep or purge it.
    Returns (composited, purged) as 0/1 counters."""
    with Image.open(path) as im:
        if im.mode != "RGBA":
            return 0, 0          # already flattened on a previous run
        flat = Image.new("RGB", im.size, BACKGROUND_RGB)
        flat.paste(im, mask=im.getchannel("A"))

    # Vectorized coverage test — the corpus runs to tens of thousands of
    # frames, and a per-pixel Python loop here costs more than the rendering.
    px = numpy.asarray(flat)
    n_bg = int((px == BACKGROUND_RGB).all(axis=2).sum())
    if 1.0 - n_bg / (flat.width * flat.height) < MIN_COVERAGE:
        os.remove(path)
        return 1, 1
    flat.save(path)
    return 1, 0


def flatten_and_purge(workers: int) -> tuple[int, int]:
    """Composite RGBA renders onto the shared gray and delete near-empty
    frames. Returns (composited, purged)."""
    paths = [os.path.join(OUT_DIR, n) for n in os.listdir(OUT_DIR)
             if n.endswith(".png")]
    composited = purged = 0
    # Threads, not processes: the work is PNG decode/encode plus numpy, both
    # of which release the GIL.
    with concurrent.futures.ThreadPoolExecutor(workers) as pool:
        for c, p in pool.map(_flatten_one, paths):
            composited += c
            purged += p
    return composited, purged


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--workers", type=int, default=4,
                    help="concurrent Blender processes")
    ap.add_argument("--chunk", type=int, default=40,
                    help="models per Blender process")
    ap.add_argument("--blender", default=None,
                    help="path to the blender executable; 'wsl' forces the "
                         "WSL route. Default: autodetect.")
    args = ap.parse_args()

    blender = Blender.locate(args.blender)
    print(f"blender: {blender.describe()}")

    os.makedirs(OUT_DIR, exist_ok=True)
    existing = set(os.listdir(OUT_DIR))

    # Build (chunk, yaws, pitches) work items across all source tiers.
    work = []
    for subdir, yaws, pitches in SOURCES:
        todo = pending_models(subdir, yaws, pitches, existing)
        label = subdir or "showcase"
        print(f"{label}: {len(todo)} models to render "
              f"({yaws * len(pitches)} views each)")
        for i in range(0, len(todo), args.chunk):
            work.append((todo[i:i + args.chunk], yaws, pitches))

    done_models = 0
    total_models = sum(len(c) for c, _, _ in work)
    with tempfile.TemporaryDirectory(prefix="hp_manifests_") as mdir:
        with concurrent.futures.ThreadPoolExecutor(args.workers) as pool:
            futures = [pool.submit(run_chunk, blender, c, y, p, mdir, i)
                       for i, (c, y, p) in enumerate(work)]
            for fut in concurrent.futures.as_completed(futures):
                try:
                    ok, fails = fut.result()
                except Exception as exc:
                    print(f"  chunk crashed: {exc}", file=sys.stderr)
                    continue
                done_models += ok
                if fails:
                    print(fails, file=sys.stderr)
                print(f"  progress: {done_models}/{total_models} models",
                      flush=True)

    composited, purged = flatten_and_purge(args.workers * 2)
    total = len([n for n in os.listdir(OUT_DIR) if n.endswith(".png")])
    print(f"done: {done_models}/{total_models} models rendered, "
          f"{composited} composited, {purged} near-empty purged, "
          f"{total} images total in {OUT_DIR}")


if __name__ == "__main__":
    main()
