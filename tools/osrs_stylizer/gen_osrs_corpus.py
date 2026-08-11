#!/usr/bin/env python3
"""Generate the OSRS-class training corpus (Class 1) for the style classifier.

Renders raw OSRS `.model` files from OSRS-Content through the repo's own
engine harness (`rs2012_model_view`) — so the corpus shows the *actual* OSRS
rasterizer look (flat shading, palette lighting, no AA) — then splits each
4-angle contact sheet into individual training tiles.

Tiles that are nearly all background (fragment models, single quads seen
edge-on) are discarded: they carry no style signal and would teach the
classifier that "empty gray square" means OSRS.

Usage (from tools/osrs_stylizer/):
    python gen_osrs_corpus.py --count 600
"""

import argparse
import concurrent.futures
import os
import random
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

DEFAULT_VIEWER = os.path.join(REPO, "src", "build_win64_opt", "rs2012_model_view.exe")
DEFAULT_MODELS = os.path.join(REPO, "OSRS-Content", "osrs239-content", "models")
DEFAULT_OUT = os.path.join(HERE, "data", "osrs")

# Shared with the high-poly corpus renderer: BOTH classes render on this same
# mid-gray so the classifier cannot learn "background color" as the style cue.
BACKGROUND_HEX = "808080"
BACKGROUND_RGB = (0x80, 0x80, 0x80)

TILE = 256          # square tile size; both corpora render at this resolution
ANGLES = 4          # yaws per model -> 4 training images per model
PITCH = 128         # ~22 degrees down, the classic RS inventory/viewer angle
MIN_MODEL_BYTES = 600   # skip fragment models with almost no geometry
MIN_COVERAGE = 0.02     # tile must be >=2% non-background pixels to keep


def list_models(models_dir: str) -> list[str]:
    """All root-level model_*.model files, big enough to be interesting."""
    out = []
    for name in os.listdir(models_dir):
        if not (name.startswith("model_") and name.endswith(".model")):
            continue
        path = os.path.join(models_dir, name)
        if os.path.getsize(path) >= MIN_MODEL_BYTES:
            out.append(path)
    return sorted(out)


def render_one(viewer: str, model_path: str, out_dir: str, tmp_dir: str) -> int:
    """Render one model's 4-angle sheet, split it, and save the tiles that
    actually contain geometry. Returns the number of tiles kept."""
    stem = os.path.splitext(os.path.basename(model_path))[0]  # model_1234
    # Rendering is deterministic, so any existing tile for this model means it
    # was already processed — makes reruns/corpus expansion incremental.
    if any(os.path.isfile(os.path.join(out_dir, f"{stem}_y{a}.png"))
           for a in range(ANGLES)):
        return 0
    sheet_path = os.path.join(tmp_dir, f"{stem}.bmp")

    proc = subprocess.run(
        [viewer, "--model", model_path, "--out", sheet_path,
         "--angles", str(ANGLES), "--pitch", str(PITCH),
         "--tile", str(TILE), "--bg", BACKGROUND_HEX],
        capture_output=True, text=True, timeout=120)
    if proc.returncode != 0 or not os.path.isfile(sheet_path):
        return 0  # undecodable/degenerate model — just skip it

    kept = 0
    try:
        with Image.open(sheet_path) as sheet:
            sheet = sheet.convert("RGB")
            for a in range(ANGLES):
                tile = sheet.crop((a * TILE, 0, (a + 1) * TILE, TILE))
                # Coverage check: fraction of pixels that differ from the
                # background clear color.
                bg_count = sum(1 for px in tile.getdata() if px == BACKGROUND_RGB)
                coverage = 1.0 - bg_count / (TILE * TILE)
                if coverage >= MIN_COVERAGE:
                    tile.save(os.path.join(out_dir, f"{stem}_y{a}.png"))
                    kept += 1
    finally:
        try:
            os.remove(sheet_path)
        except OSError:
            pass
    return kept


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--viewer", default=DEFAULT_VIEWER)
    ap.add_argument("--models-dir", default=DEFAULT_MODELS)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--count", type=int, default=0,
                    help="number of models to sample; 0 = ALL models in the "
                         "cache (the root model_*.model set is the deduplicated "
                         "model archive — the named loc/npc/obj subdirs are "
                         "per-config re-exports of the same geometry)")
    ap.add_argument("--seed", type=int, default=1337)
    ap.add_argument("--workers", type=int, default=8)
    args = ap.parse_args()

    if not os.path.isfile(args.viewer):
        raise SystemExit(f"viewer binary not found: {args.viewer}\n"
                         f"build it with: make -C src rs2012-model-view")

    models = list_models(args.models_dir)
    if not models:
        raise SystemExit(f"no model files under {args.models_dir}")
    print(f"{len(models)} candidate models >= {MIN_MODEL_BYTES} bytes")

    # Deterministic sample so reruns produce the same corpus; --count 0 takes
    # every model in the cache.
    if args.count and args.count < len(models):
        rng = random.Random(args.seed)
        sample = rng.sample(models, args.count)
    else:
        sample = models

    os.makedirs(args.out, exist_ok=True)
    total_tiles = 0
    failures = 0
    with tempfile.TemporaryDirectory(prefix="osrs_sheets_") as tmp_dir:
        with concurrent.futures.ThreadPoolExecutor(args.workers) as pool:
            futures = {pool.submit(render_one, args.viewer, m, args.out, tmp_dir): m
                       for m in sample}
            for i, fut in enumerate(concurrent.futures.as_completed(futures), 1):
                try:
                    kept = fut.result()
                except Exception as exc:  # keep the batch going on one bad model
                    print(f"  ! {os.path.basename(futures[fut])}: {exc}",
                          file=sys.stderr)
                    failures += 1
                    kept = 0
                total_tiles += kept
                if i % 50 == 0 or i == len(sample):
                    print(f"  {i}/{len(sample)} models -> {total_tiles} tiles")

    print(f"done: {total_tiles} OSRS tiles in {args.out} "
          f"({failures} models failed)")


if __name__ == "__main__":
    main()
