#!/usr/bin/env python3
"""Build the ENGINE-ONLY poly-density corpus for the second (in-engine) judge.

Why this exists: the first judge (osrs engine renders vs Blender high-poly
renders) turned out to key on the RENDERER fingerprint as hard as the art
style — measured 2026-08-11, every engine render scores ~100 and every
Blender render ~0 regardless of geometry, and the raw logit margins overlap
too. That judge cleanly answers "is this an engine render?", which is useless
for pushing an already-imported high-poly model toward the classic look.

So the backport pipeline needs a judge where BOTH classes come through
`rs2012_model_view` and the only thing left to learn is the geometry style:

  classic/: the low-poly classic look — natives sampled from the small end
            of the cache's size distribution
  import/:  the high-poly look — the largest natives (modern OSRS remaster
            art) plus every RS2012 ported-lane model (the exact imports the
            backporter is trying to make look classic)

File size is the poly-count proxy (bytes scale with faces/verts); a guard
band between the classes keeps labels clean. Tiles/flags are identical to
gen_osrs_corpus.py, so the trained judge shares osrs_scorer.py's inference
path — train with:

    python train_classifier.py --data-root data_engine --epochs 3

and read scores with get_osrs_score(..., checkpoint_path=<new ckpt>), where
"osrs" maps to the `classic` class below via folder naming.
"""

import argparse
import concurrent.futures
import glob
import os
import random
import sys
import tempfile

from gen_osrs_corpus import render_one, list_models

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULT_VIEWER = os.path.join(REPO, "src", "build_win64_opt", "rs2012_model_view.exe")
DEFAULT_MODELS = os.path.join(REPO, "OSRS-Content", "osrs239-content", "models")
DEFAULT_LANE = os.path.join(DEFAULT_MODELS, "ported", "rs2012_qbd_td")
DEFAULT_OUT = os.path.join(HERE, "data_engine")

# Percentile bands of the size-sorted native model list. The gap between
# CLASSIC_HI and IMPORT_LO is the guard band — models there are ambiguous
# (mid-density) and train neither class.
CLASSIC_LO, CLASSIC_HI = 0.02, 0.40
IMPORT_LO = 0.92

# The folder names ARE the labels, and train_classifier.py hard-requires the
# pair 'osrs'/'highpoly' — so the classic band trains as "osrs" (which also
# keeps osrs_scorer's class_to_idx["osrs"] lookup working) and the dense band
# trains as "highpoly" (which it literally is).
CLASS_CLASSIC = "osrs"
CLASS_IMPORT = "highpoly"


def render_class(viewer, paths, out_dir, workers, label):
    os.makedirs(out_dir, exist_ok=True)
    total, failures = 0, 0
    with tempfile.TemporaryDirectory(prefix="engine_sheets_") as tmp_dir:
        with concurrent.futures.ThreadPoolExecutor(workers) as pool:
            futures = {pool.submit(render_one, viewer, m, out_dir, tmp_dir): m
                       for m in paths}
            for i, fut in enumerate(concurrent.futures.as_completed(futures), 1):
                try:
                    total += fut.result()
                except Exception as exc:
                    print(f"  ! {os.path.basename(futures[fut])}: {exc}",
                          file=sys.stderr)
                    failures += 1
                if i % 100 == 0 or i == len(futures):
                    print(f"  [{label}] {i}/{len(futures)} models -> {total} tiles")
    print(f"[{label}] done: {total} tiles ({failures} failed)")
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--viewer", default=DEFAULT_VIEWER)
    ap.add_argument("--models-dir", default=DEFAULT_MODELS)
    ap.add_argument("--lane", default=DEFAULT_LANE,
                    help="ported-lane dir whose .ob3 models all join the "
                         "import class (empty string to skip)")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--per-class", type=int, default=1400,
                    help="native models sampled per class")
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--seed", type=int, default=1337)
    args = ap.parse_args()

    if not os.path.isfile(args.viewer):
        raise SystemExit(f"viewer binary not found: {args.viewer}")

    natives = sorted(list_models(args.models_dir), key=os.path.getsize)
    if len(natives) < 100:
        raise SystemExit(f"only {len(natives)} native models under "
                         f"{args.models_dir} — wrong tree?")
    n = len(natives)
    classic_band = natives[int(n * CLASSIC_LO):int(n * CLASSIC_HI)]
    import_band = natives[int(n * IMPORT_LO):]
    rng = random.Random(args.seed)
    classic = (rng.sample(classic_band, args.per_class)
               if len(classic_band) > args.per_class else classic_band)
    imports = (rng.sample(import_band, args.per_class)
               if len(import_band) > args.per_class else import_band)

    lane_models = []
    if args.lane:
        lane_models = sorted(glob.glob(os.path.join(args.lane, "*.ob3")))
        imports = imports + lane_models

    def size_range(paths):
        sizes = [os.path.getsize(p) for p in paths]
        return f"{min(sizes)}..{max(sizes)}B" if sizes else "-"

    print(f"{n} natives; classic: {len(classic)} sampled of "
          f"{len(classic_band)} in band ({size_range(classic)}); "
          f"import: {len(imports)} ({len(lane_models)} lane models, "
          f"{size_range(imports)})")

    render_class(args.viewer, classic,
                 os.path.join(args.out, CLASS_CLASSIC), args.workers, "classic")
    render_class(args.viewer, imports,
                 os.path.join(args.out, CLASS_IMPORT), args.workers, "import")
    print(f"corpus ready under {args.out} — train with:\n"
          f"  python train_classifier.py --data-root {os.path.basename(args.out)}"
          f" --epochs 3")


if __name__ == "__main__":
    main()
