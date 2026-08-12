#!/usr/bin/env python3
"""Component 4: Optuna multi-objective optimization loop.

Ties everything together:

    trial params --(subprocess: blender -b -P modifier.py)--> renders + .glb
                 --(osrs_scorer.get_osrs_score)-------------> style objective
                 --(content_scorer.get_content_score)-------> content objective

Both objectives are MAXIMIZED simultaneously (directions=["maximize",
"maximize"]) so Optuna's NSGA-II sampler finds the Pareto front: the set of
parameter combinations where you cannot look more OSRS without losing more
content, and vice versa. You then pick the trade-off you like from
runs/<study>/pareto.json.

Usage:
    python optimize.py --input dragon.fbx --n-trials 60
    # resume later with the same --study-name / --storage: trials accumulate.
"""

import argparse
import json
import os
import statistics
import subprocess
import sys

import optuna

from blender_locate import Blender
from content_scorer import get_content_score
from osrs_scorer import get_osrs_score

VIEWS = ("front", "side", "iso")   # must match modifier.py's render names
BLENDER_TIMEOUT_S = 900            # kill runaway Blender processes


def run_blender(blender: Blender, input_path: str, outdir: str, *,
                decimate: float, grid: float, colors: int,
                resolution: int) -> None:
    """Invoke the headless Blender modifier. Raises on failure so the caller
    can prune the trial instead of scoring garbage renders."""
    modifier = os.path.join(os.path.dirname(os.path.abspath(__file__)), "modifier.py")
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
    result = subprocess.run(cmd, capture_output=True, text=True,
                            timeout=BLENDER_TIMEOUT_S)
    # Blender sometimes exits 0 after a Python exception, so also verify that
    # every expected render actually landed on disk.
    missing = [v for v in VIEWS
               if not os.path.isfile(os.path.join(outdir, f"view_{v}.png"))]
    if result.returncode != 0 or missing:
        raise RuntimeError(
            f"Blender failed (rc={result.returncode}, missing={missing}).\n"
            f"--- stdout tail ---\n{result.stdout[-2000:]}\n"
            f"--- stderr tail ---\n{result.stderr[-2000:]}")


def ensure_baseline(blender: Blender, input_path: str, baseline_dir: str,
                    resolution: int) -> None:
    """Render the UNMODIFIED model once (decimate=1, grid=0, colors=0 keeps
    geometry and textures intact). These renders are the content anchor every
    trial is compared against, so they go through the exact same camera /
    background / engine pipeline as the trials."""
    if all(os.path.isfile(os.path.join(baseline_dir, f"view_{v}.png")) for v in VIEWS):
        return  # already rendered on a previous run — baselines are deterministic
    os.makedirs(baseline_dir, exist_ok=True)
    print("[optimize] rendering baseline (unmodified) views...")
    run_blender(blender, input_path, baseline_dir,
                decimate=1.0, grid=0.0, colors=0, resolution=resolution)


def make_objective(args: argparse.Namespace, baseline_dir: str, trials_dir: str):
    """Build the Optuna objective closure over the fixed run configuration."""

    if args.content_scorer == "preserver":
        # Imported lazily so the CLIP path never pays for (or requires) the
        # preserver checkpoint, and vice versa.
        from preserver_scorer import get_identity_score

        def content_fn(baseline: str, render: str) -> float:
            # get_identity_score is already 0..100; renormalize to the 0..1
            # range get_content_score reports so the shared *100 below and
            # the pareto.json scale stay identical across both scorers.
            return get_identity_score(baseline, render,
                                      checkpoint_path=args.preserver) / 100.0
    else:
        content_fn = get_content_score

    def objective(trial: optuna.trial.Trial) -> tuple[float, float]:
        # ------------------------- search space --------------------------
        # log=True on all three: perceptual impact is roughly multiplicative
        # (0.02 -> 0.04 decimation matters far more than 0.90 -> 0.92).
        decimate_ratio = trial.suggest_float("decimate_ratio", 0.01, 1.0, log=True)
        quantization_grid = trial.suggest_float("quantization_grid", 0.01, 0.5, log=True)
        color_quantize = trial.suggest_int("color_quantize", 2, 64, log=True)

        trial_dir = os.path.join(trials_dir, f"trial_{trial.number:04d}")
        os.makedirs(trial_dir, exist_ok=True)

        # ------------------------- mutate + render -----------------------
        try:
            run_blender(args.blender, args.input, trial_dir,
                        decimate=decimate_ratio, grid=quantization_grid,
                        colors=color_quantize, resolution=args.resolution)
        except (RuntimeError, subprocess.TimeoutExpired) as exc:
            # A failed/degenerate mutation (e.g. grid so coarse the mesh
            # vanished) is not a data point — prune rather than poison the
            # Pareto front with fake zeros.
            print(f"[trial {trial.number}] pruned: {exc}", file=sys.stderr)
            raise optuna.TrialPruned() from exc

        # ------------------------- evaluate ------------------------------
        # Average over the three views so a single lucky/unlucky angle can't
        # dominate either objective.
        osrs_scores, content_scores = [], []
        for view in VIEWS:
            render = os.path.join(trial_dir, f"view_{view}.png")
            baseline = os.path.join(baseline_dir, f"view_{view}.png")
            osrs_scores.append(get_osrs_score(render, checkpoint_path=args.classifier))
            content_scores.append(content_fn(baseline, render))

        osrs_score = statistics.mean(osrs_scores)             # 0..100
        content_score = statistics.mean(content_scores) * 100  # cosine -> 0..100 scale

        # Stash artifacts on the trial so pareto.json is self-describing.
        trial.set_user_attr("render_dir", trial_dir)
        trial.set_user_attr("glb", os.path.join(trial_dir, "view.glb"))
        trial.set_user_attr("per_view_osrs", osrs_scores)
        trial.set_user_attr("per_view_content", content_scores)

        print(f"[trial {trial.number}] decimate={decimate_ratio:.3f} "
              f"grid={quantization_grid:.3f} colors={color_quantize} "
              f"-> osrs={osrs_score:.1f} content={content_score:.1f}")
        return osrs_score, content_score

    return objective


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input", required=True, help="high-poly .obj/.fbx to stylize")
    ap.add_argument("--classifier", default="models/osrs_classifier.pt",
                    help="checkpoint from train_classifier.py")
    ap.add_argument("--content-scorer", choices=("clip", "preserver"),
                    default="clip",
                    help="content judge: off-the-shelf CLIP similarity, or "
                         "the trained Content Preserver (train_preserver.py)")
    ap.add_argument("--preserver", default="models/content_preserver.pt",
                    help="checkpoint for --content-scorer preserver")
    ap.add_argument("--n-trials", type=int, default=50)
    ap.add_argument("--resolution", type=int, default=512)
    ap.add_argument("--blender", default=None,
                    help="path to the Blender executable; 'wsl' forces the "
                         "WSL route. Default: autodetect.")
    ap.add_argument("--outdir", default="runs")
    ap.add_argument("--study-name", default=None,
                    help="defaults to the input file's base name")
    args = ap.parse_args()
    args.input = os.path.abspath(args.input)
    args.blender = Blender.locate(args.blender)
    print(f"[optimize] blender: {args.blender.describe()}")

    study_name = args.study_name or os.path.splitext(os.path.basename(args.input))[0]
    run_dir = os.path.join(args.outdir, study_name)
    baseline_dir = os.path.join(run_dir, "baseline")
    trials_dir = os.path.join(run_dir, "trials")
    os.makedirs(trials_dir, exist_ok=True)

    # Content anchor first — every trial's second objective depends on it.
    ensure_baseline(args.blender, args.input, baseline_dir, args.resolution)

    # SQLite storage makes the study resumable and inspectable with
    # `optuna-dashboard sqlite:///runs/<study>/study.db`.
    storage = f"sqlite:///{os.path.join(run_dir, 'study.db')}"
    study = optuna.create_study(
        study_name=study_name,
        storage=storage,
        load_if_exists=True,
        directions=["maximize", "maximize"],   # (osrs_score, content_score)
        # NSGA-II is Optuna's standard multi-objective sampler; seeded for
        # reproducibility of the search trajectory.
        sampler=optuna.samplers.NSGAIISampler(seed=1337),
    )
    study.optimize(make_objective(args, baseline_dir, trials_dir),
                   n_trials=args.n_trials)

    # ------------------------- report the Pareto front -------------------
    pareto = sorted(study.best_trials, key=lambda t: -t.values[0])
    print(f"\n=== Pareto front: {len(pareto)} non-dominated trials ===")
    print(f"{'trial':>5}  {'osrs':>6}  {'content':>7}  params")
    report = []
    for t in pareto:
        print(f"{t.number:>5}  {t.values[0]:>6.1f}  {t.values[1]:>7.1f}  {t.params}")
        report.append({
            "trial": t.number,
            "osrs_score": t.values[0],
            "content_score": t.values[1],
            "params": t.params,
            "glb": t.user_attrs.get("glb"),
            "render_dir": t.user_attrs.get("render_dir"),
        })

    pareto_path = os.path.join(run_dir, "pareto.json")
    with open(pareto_path, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2)
    print(f"\nWrote {pareto_path}")
    print("Pick a trial whose trade-off you like — its stylized model is the "
          "'glb' path, and its renders are in 'render_dir'.")


if __name__ == "__main__":
    main()
