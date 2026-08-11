#!/usr/bin/env python3
"""Render every admitted Summoning ledger model for human texture review.

The source and target texture systems are incompatible, so this command creates
evidence only.  It never changes a texture map or a ledger signoff.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT / "OSRS-Content/osrs239-content/port"
SOURCE_CACHE = Path("/Users/matthewevers/Documents/git_repos/2009scape/Server/data/cache")
TARGET_CACHE = ROOT / "cache.osrs239.summoning"
ANIM_COMPARE = ROOT / "3rd/rscache/tools/anim_compare/anim_compare"


def rows(path: Path) -> list[dict[str, str]]:
    lines = [line for line in path.read_text().splitlines() if line and not line.startswith("#")]
    header = lines[0].split("\t")
    return [dict(zip(header, line.split("\t"), strict=True)) for line in lines[1:]]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/summoning-texture-review")
    parser.add_argument("--limit", type=int, help="render at most this many model pairs")
    args = parser.parse_args()

    required = (ANIM_COMPARE, SOURCE_CACHE / "main_file_cache.dat2", TARGET_CACHE / "main_file_cache.dat2")
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        parser.error("missing required review input(s): " + ", ".join(missing))

    ledgers = [path for path in sorted(PORT.glob("summoning_*_530.map")) if path.name != "summoning_530.map"]
    jobs: list[dict[str, str]] = []
    for ledger in ledgers:
        entries = rows(ledger)
        models = [row for row in entries if row["kind"] == "model"]
        sequences = [row for row in entries if row["kind"] == "seq"]
        if not models:
            continue
        if not sequences:
            raise ValueError(f"{ledger}: model rows have no sequence for review")
        sequence = sequences[0]
        for model in models:
            jobs.append({
                "ledger": ledger.name,
                "name": model["dst_name"],
                "source_model": model["src_id"],
                "target_model": model["dst_id"],
                "source_seq": sequence["src_id"],
                "target_seq": sequence["dst_id"],
                "signoff": model["signoff"],
            })
    if args.limit is not None:
        jobs = jobs[: args.limit]
    if not jobs:
        raise ValueError("no dedicated Summoning model rows selected")

    args.out.mkdir(parents=True, exist_ok=True)
    evidence: list[dict[str, str]] = []
    for job in jobs:
        model_dir = args.out / job["name"]
        if model_dir.exists():
            shutil.rmtree(model_dir)
        for mode in ("by-label", "material"):
            out = model_dir / mode
            out.mkdir(parents=True, exist_ok=True)
            command = [
                str(ANIM_COMPARE), "--a-rev", "rs530", "--a-cache", str(SOURCE_CACHE),
                "--a-seq", job["source_seq"], "--a-model", job["source_model"],
                "--b-cache", str(TARGET_CACHE), "--b-rev", "osrs239",
                "--b-seq", job["target_seq"], "--b-model", job["target_model"],
                "--frames", "0-0", "--size", "256x256", "--sheet", "--out", str(out),
            ]
            if mode == "by-label":
                command.append("--by-label")
            completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if completed.returncode:
                raise RuntimeError(f"{job['ledger']} {job['source_model']}->{job['target_model']} ({mode}):\n{completed.stdout}")
            if not (out / "sheet.bmp").is_file():
                raise RuntimeError(f"{out}: anim_compare produced no sheet")
        evidence.append(job)

    index = args.out / "index.json"
    index.write_text(json.dumps({"schema": 1, "review_state": "unreviewed", "pairs": evidence}, indent=2) + "\n")
    print(f"summoning_texture_review: rendered {len(evidence)} model pairs; all remain unreviewed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
