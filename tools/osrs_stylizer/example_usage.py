#!/usr/bin/env python3
"""Worked example: what the trained OSRS style judge is for, and how to use it.

Two demonstrations, run both by default:

  verify   Score held-out images from both corpus classes and report how well
           the judge separates them. This is the "is the checkpoint any good?"
           check — it needs only the classifier and data/, no Blender.

  stylize  The real job. Take a modern, high-poly model and push it through
           modifier.py at increasing retro-fication strength, scoring every
           step with the style judge (and, if CLIP is installed, the content
           judge). The style score should climb as the mesh gets cruder while
           the content score falls — that trade-off is exactly what optimize.py
           searches. Writes a labeled contact sheet so you can see it.

Usage:
    python example_usage.py                       # both demos, defaults
    python example_usage.py --only verify
    python example_usage.py --only stylize --input highpoly_src/Fox.glb

Prerequisites: a trained models/osrs_classifier.pt (see README). `stylize`
additionally needs Blender, which is located automatically.
"""

import argparse
import os
import random
import re
import statistics
import subprocess
import sys

from PIL import Image, ImageDraw

from blender_locate import Blender
from osrs_scorer import get_osrs_score

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CHECKPOINT = os.path.join(HERE, "models", "osrs_classifier.pt")
DEFAULT_INPUT = os.path.join(HERE, "highpoly_src", "DamagedHelmet.glb")
DATA_DIR = os.path.join(HERE, "data")
VIEWS = ("front", "side", "iso")   # must match modifier.py's render names

# The stylization sweep: (label, decimate ratio, quantization grid, palette).
# The first row is modifier.py's documented pass-through (no mutation,
# textures kept), which anchors both judges. Grid spacing is in object units;
# these values suit the roughly 2-unit-wide models in highpoly_src/.
SWEEP = [
    ("original", 1.00, 0.000,  0),
    ("light",    0.50, 0.010, 32),
    ("medium",   0.15, 0.030, 16),
    ("heavy",    0.05, 0.060,  8),
    ("extreme",  0.02, 0.120,  4),
]


# ---------------------------------------------------------------------------
# Demo 1 — does the checkpoint actually separate the two classes?
# ---------------------------------------------------------------------------

def demo_verify(checkpoint: str, per_class: int, seed: int) -> bool:
    """Score a random held-out sample from each corpus class and report the
    separation. Returns True if the judge behaved as expected.

    Note this samples the whole corpus, which includes the images the model
    trained on, so treat it as a smoke test of the checkpoint — the honest
    generalization number is the val_acc printed during training and stored
    in the checkpoint."""
    print("=" * 72)
    print("DEMO 1 — verify the judge separates the two corpus classes")
    print("=" * 72)

    rng = random.Random(seed)
    results = {}
    for cls in ("osrs", "highpoly"):
        folder = os.path.join(DATA_DIR, cls)
        if not os.path.isdir(folder):
            raise SystemExit(f"missing corpus folder {folder} — see the README's "
                             f"dataset generation steps")
        names = [n for n in os.listdir(folder) if n.endswith(".png")]
        sample = rng.sample(names, min(per_class, len(names)))
        scores = [get_osrs_score(os.path.join(folder, n), checkpoint_path=checkpoint)
                  for n in sample]
        results[cls] = scores
        print(f"\n  {cls}: {len(scores)} images sampled from {len(names)}")
        print(f"    mean OSRS score : {statistics.mean(scores):6.2f}")
        print(f"    median          : {statistics.median(scores):6.2f}")
        print(f"    min / max       : {min(scores):6.2f} / {max(scores):6.2f}")

    # The decision rule the scorer implies: >50 means "this looks OSRS".
    osrs_right = sum(1 for s in results["osrs"] if s > 50)
    high_right = sum(1 for s in results["highpoly"] if s <= 50)
    total = len(results["osrs"]) + len(results["highpoly"])
    correct = osrs_right + high_right
    print(f"\n  agreement with the corpus labels at a >50 threshold: "
          f"{correct}/{total} ({correct / total:.1%})")
    print(f"    osrs      scored >50 : {osrs_right}/{len(results['osrs'])}")
    print(f"    highpoly  scored <=50: {high_right}/{len(results['highpoly'])}")

    gap = statistics.mean(results["osrs"]) - statistics.mean(results["highpoly"])
    print(f"  mean separation: {gap:+.1f} points")
    ok = gap > 50
    print(f"  verdict: {'PASS' if ok else 'FAIL'} — the judge "
          f"{'cleanly separates' if ok else 'does NOT separate'} the classes")
    return ok


# ---------------------------------------------------------------------------
# Demo 2 — score a real stylization sweep
# ---------------------------------------------------------------------------

def run_modifier(blender: Blender, input_path: str, outdir: str,
                 decimate: float, grid: float, colors: int,
                 resolution: int) -> int:
    """Run one modifier.py mutation+render. Returns the stylized face count
    (parsed from its log) so the sweep table can show the mesh shrinking."""
    os.makedirs(outdir, exist_ok=True)
    modifier = os.path.join(HERE, "modifier.py")
    cmd = [
        *blender.launcher, "-b", "--factory-startup",
        "-P", blender.path(modifier), "--",
        "--input", blender.path(input_path),
        "--outdir", blender.path(outdir),
        "--decimate", str(decimate),
        "--grid", str(grid),
        "--colors", str(colors),
        "--resolution", str(resolution),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
    # Blender can exit 0 after a Python traceback, so verify the renders landed
    # rather than trusting the return code.
    missing = [v for v in VIEWS
               if not os.path.isfile(os.path.join(outdir, f"view_{v}.png"))]
    if proc.returncode != 0 or missing:
        raise RuntimeError(
            f"blender failed (rc={proc.returncode}, missing={missing})\n"
            f"--- stdout tail ---\n{proc.stdout[-1500:]}\n"
            f"--- stderr tail ---\n{proc.stderr[-1500:]}")

    faces = re.findall(r"\[modifier\] stylized: (\d+) faces", proc.stdout)
    if not faces:   # the pass-through row never reports a "stylized" line
        faces = re.findall(r"\[modifier\] imported: (\d+) faces", proc.stdout)
    return int(faces[-1]) if faces else 0


def contact_sheet(rows: list[dict], outpath: str, view: str = "iso") -> None:
    """Stitch one render per sweep step into a labeled strip, so the trade-off
    is visible and not just tabular."""
    tiles = [Image.open(os.path.join(r["dir"], f"view_{view}.png")).convert("RGB")
             for r in rows]
    w, h = tiles[0].size
    band = 34   # caption strip under each tile
    sheet = Image.new("RGB", (w * len(tiles), h + band), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for i, (tile, row) in enumerate(zip(tiles, rows)):
        sheet.paste(tile, (i * w, 0))
        caption = f"{row['label']}  osrs={row['osrs']:.1f}"
        if row["content"] is not None:
            caption += f"  content={row['content']:.1f}"
        draw.text((i * w + 6, h + 8), caption, fill=(235, 235, 235))
    sheet.save(outpath)


def demo_stylize(checkpoint: str, input_path: str, outdir: str,
                 blender_arg: str | None, resolution: int) -> None:
    """Sweep retro-fication strength and watch both judges move."""
    print()
    print("=" * 72)
    print("DEMO 2 — stylization sweep, scored by the judge")
    print("=" * 72)

    if not os.path.isfile(input_path):
        raise SystemExit(f"input model not found: {input_path}\n"
                         f"run fetch_highpoly.py first, or pass --input")

    blender = Blender.locate(blender_arg)
    print(f"  blender: {blender.describe()}")
    print(f"  input  : {input_path}")

    # The content judge is optional: it pulls in transformers + a ~600 MB CLIP
    # download. The demo is still meaningful without it, so degrade rather
    # than fail.
    try:
        from content_scorer import get_content_score
    except ImportError:
        get_content_score = None
        print("  note: transformers not installed — skipping the content "
              "score (pip install transformers)")

    rows = []
    baseline_dir = None
    for label, decimate, grid, colors in SWEEP:
        step_dir = os.path.join(outdir, label)
        print(f"\n  [{label}] decimate={decimate} grid={grid} colors={colors} ...",
              flush=True)
        faces = run_modifier(blender, input_path, step_dir,
                             decimate, grid, colors, resolution)
        if baseline_dir is None:
            baseline_dir = step_dir   # the pass-through row anchors the content score

        # Average over the three views: one lucky angle should not decide it.
        osrs = statistics.mean(
            get_osrs_score(os.path.join(step_dir, f"view_{v}.png"),
                           checkpoint_path=checkpoint) for v in VIEWS)
        content = None
        if get_content_score is not None:
            content = statistics.mean(
                get_content_score(os.path.join(baseline_dir, f"view_{v}.png"),
                                  os.path.join(step_dir, f"view_{v}.png"))
                for v in VIEWS) * 100.0

        rows.append({"label": label, "decimate": decimate, "grid": grid,
                     "colors": colors, "faces": faces, "osrs": osrs,
                     "content": content, "dir": step_dir})
        print(f"      faces={faces:<8} osrs={osrs:6.2f}"
              + (f"  content={content:6.2f}" if content is not None else ""))

    print("\n  " + "-" * 68)
    print(f"  {'step':<9} {'decimate':>8} {'grid':>6} {'colors':>6} "
          f"{'faces':>8} {'osrs%':>7} {'content%':>9}")
    print("  " + "-" * 68)
    for r in rows:
        content = f"{r['content']:9.2f}" if r["content"] is not None else f"{'n/a':>9}"
        colors = r["colors"] if r["colors"] else "-"
        print(f"  {r['label']:<9} {r['decimate']:>8.2f} {r['grid']:>6.3f} "
              f"{str(colors):>6} {r['faces']:>8} {r['osrs']:>7.2f} {content}")
    print("  " + "-" * 68)

    sheet = os.path.join(outdir, "contact_sheet.png")
    contact_sheet(rows, sheet)
    print(f"\n  contact sheet -> {sheet}")
    print(f"  stylized meshes -> {outdir}/<step>/view.glb")
    print("\n  This hand-picked sweep is the manual version of what optimize.py "
          "\n  automates: it searches these same three knobs and returns the "
          "\n  Pareto front of the style/content trade-off you see above.")


# ---------------------------------------------------------------------------

def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", choices=["verify", "stylize"], default=None,
                    help="run just one demo (default: both)")
    ap.add_argument("--checkpoint", default=DEFAULT_CHECKPOINT)
    ap.add_argument("--input", default=DEFAULT_INPUT,
                    help="model to stylize in demo 2")
    ap.add_argument("--outdir", default=os.path.join(HERE, "runs", "example"))
    ap.add_argument("--blender", default=None,
                    help="path to blender; 'wsl' forces the WSL route")
    ap.add_argument("--resolution", type=int, default=512)
    ap.add_argument("--samples", type=int, default=200,
                    help="images per class in demo 1")
    ap.add_argument("--seed", type=int, default=1337)
    args = ap.parse_args()

    if not os.path.isfile(args.checkpoint):
        raise SystemExit(
            f"no checkpoint at {args.checkpoint}\n"
            f"train one first — see 'Reproducing the model' in the README.")
    print(f"checkpoint: {args.checkpoint}")

    ok = True
    if args.only in (None, "verify"):
        ok = demo_verify(args.checkpoint, args.samples, args.seed)
    if args.only in (None, "stylize"):
        demo_stylize(args.checkpoint, args.input, args.outdir,
                     args.blender, args.resolution)
    # Non-zero exit if the judge failed its own sanity check, so this script is
    # usable as a post-training gate in a script or CI job.
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
