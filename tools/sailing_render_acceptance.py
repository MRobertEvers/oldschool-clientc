#!/usr/bin/env python3
"""Measure the zero-boat cost of painter descent with a compiled-out control.

Both executables link the same current application objects. Only the control's
temporary painter source removes world-entity branches. No production files or
shared build outputs are changed. This is a causal control for C3's hot-loop
cost, not a comparison against an old client with a different renderer.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import statistics
import subprocess
import sys

from launcher.bench import Run, load_suite, read_windows, summarise
from launcher.profiles import Manifest

ROOT = Path(__file__).resolve().parents[1]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build_pair(output: Path) -> dict:
    dry = subprocess.run(
        # objverify otherwise deletes stale objects even during make -n.
        ["make", "-n", "EMBED_SERVER=1", "OBJVERIFY_REPORT=", "-W", "painters/painters.c", "torirs"],
        cwd=ROOT / "src", text=True, capture_output=True, check=True,
    )
    commands = [shlex.split(line) for line in dry.stdout.splitlines()
                if line.startswith(("cc ", "clang ", "gcc "))]
    compile_command = next(c for c in commands if "painters/painters.c" in c and "-c" in c)
    link_command = next(c for c in commands if "-o" in c and c[c.index("-o") + 1] == "torirs")
    painter_object = compile_command[compile_command.index("-o") + 1]
    source = ROOT / "src/painters"
    main = (source / "painters.c").read_text()
    bucket = (source / "painters_bucket.u.c").read_text()
    condition = "if( scenery_is_world_entity(element) )"
    if bucket.count(condition) != 2:
        raise RuntimeError("Painter branch shape changed; review the control before using it")
    objects = [ROOT / "src" / a for a in link_command if a.endswith(".o") and a != painter_object]
    object_hashes = {str(p.relative_to(ROOT)): sha(p) for p in objects}
    frozen = output / "objects"
    frozen.mkdir(exist_ok=True)
    replacements = {}
    for p in objects:
        dest = frozen / p.name
        shutil.copyfile(p, dest)
        if sha(dest) != object_hashes[str(p.relative_to(ROOT))]:
            raise RuntimeError("Shared objects changed while freezing the benchmark inputs")
        replacements[str(p.relative_to(ROOT / "src"))] = str(dest)
    evidence = {"kind": "current application with descent compiled out vs enabled",
                "painter_sha256": sha(source / "painters.c"),
                "bucket_sha256": sha(source / "painters_bucket.u.c"),
                "shared_object_sha256": object_hashes, "executables": {}}
    for variant in ("control", "sailing"):
        directory = output / variant
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "painters.c").write_text(main)
        (directory / "painters_bucket.u.c").write_text(
            bucket.replace(condition, "if( 0 ) /* zero-boat control */") if variant == "control" else bucket)
        command = list(compile_command)
        command[command.index("painters/painters.c")] = str(directory / "painters.c")
        command[command.index("-o") + 1] = str(directory / "painters.o")
        link = [str(directory / "painters.o") if a == painter_object else replacements.get(a, a)
                for a in link_command]
        link[link.index("-o") + 1] = str(directory / "torirs")
        with (directory / "build.log").open("w") as log:
            subprocess.run(command, cwd=ROOT / "src", stdout=log, stderr=log, check=True)
            subprocess.run(link, cwd=ROOT / "src", stdout=log, stderr=log, check=True)
        evidence["executables"][variant] = {"sha256": sha(directory / "torirs"),
                                              "compile": command, "link": link}
    return evidence


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("/tmp/sailing-render-acceptance"))
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--reuse-build", action="store_true", help="Reuse a previously frozen pair after verifying its recorded executable hashes")
    parser.add_argument("--scenes", nargs="+", help="Named scenes; defaults to the complete manifest suite")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    build_path = output / "build.json"
    if args.reuse_build:
        build = json.loads(build_path.read_text())
        for variant, metadata in build["executables"].items():
            if sha(output / variant / "torirs") != metadata["sha256"]:
                raise RuntimeError("Frozen executable no longer matches the recorded build")
    else:
        build = build_pair(output)
        build_path.write_text(json.dumps(build, indent=2) + "\n")
    report = {"build": build, "runs": [], "comparisons": []}
    manifest = ROOT / "manifests/manifest_osrs239_bench.ini"
    report["manifest_sha256"] = sha(manifest)
    suite = load_suite(Manifest.load(str(manifest)))
    scenes = {s.name: s for s in suite.scenes}
    for name in args.scenes or scenes:
        scene = scenes[name]
        for repetition in range(args.repeat):
            # Alternate order to reduce thermal/order bias.
            for variant in (("control", "sailing") if repetition % 2 == 0 else ("sailing", "control")):
                run = Run(scene, "soft3d", repetition, str(output / variant))
                env = {k: v for k, v in os.environ.items()
                       if not k.startswith(("TORIRS_", "TORIDRAW_", "TORIRSSERVER_"))}
                env.update(run.env(shots=True))
                Path(run.shot_dir).mkdir(parents=True, exist_ok=True)
                command = [str(output / variant / "torirs"), "--manifest", str(manifest), *run.args()]
                with open(run.log_path, "w") as log:
                    subprocess.run(command, env=env, cwd=ROOT, stdout=log, stderr=log, check=True, timeout=90)
                row = summarise(run, read_windows(run.windows_csv_path))
                if row["samples_kept"] != scene.samples:
                    raise RuntimeError(f"Incomplete measured windows: {name} {variant}")
                shots = list(Path(run.shot_dir).glob("*.bmp"))
                if len(shots) != 1:
                    raise RuntimeError(f"Expected one actual rendered image: {name} {variant}")
                row.update(variant=variant, image_sha256=sha(shots[0]), image=str(shots[0]))
                report["runs"].append(row)
                print(f"{name} {variant} {repetition + 1}: render {row['stages']['render']['p50_ms']:.4f} ms", flush=True)
        pair = {v: [r for r in report["runs"] if r["scene"] == name and r["variant"] == v]
                for v in ("control", "sailing")}
        if len({r["image_sha256"] for rows in pair.values() for r in rows}) != 1:
            raise RuntimeError(f"Rendered pixels differ for boat-free scene {name}")
        counters = [r["counters"] for rows in pair.values() for r in rows]
        if any(c != counters[0] for c in counters):
            raise RuntimeError(f"Different rendering workloads for boat-free scene {name}")
        comparison = {"scene": name, "pixels_identical": True, "counters": counters[0], "stages": {}}
        for stage in ("render", "paint", "build", "frame"):
            medians = {v: statistics.median(r["stages"][stage]["p50_ms"] for r in pair[v]) for v in pair}
            comparison["stages"][stage] = {**medians, "delta_percent": (medians["sailing"] / medians["control"] - 1) * 100 if medians["control"] else None}
        report["comparisons"].append(comparison)
        (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(str(output / "results.json"))


if __name__ == "__main__":
    main()
