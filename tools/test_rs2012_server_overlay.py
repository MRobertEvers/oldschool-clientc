#!/usr/bin/env python3
"""Hermetic cachepack test for imported config ids used by server overlays."""

from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
DEFAULT_CACHEPACK = REPO / "3rd/rscache/tools/cachepack/cachepack"


def put(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def invoke(cachepack: Path, tree: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            str(cachepack),
            "pack",
            "--src",
            str(tree),
            "--server-only",
            "--rev",
            "osrs239",
        ],
        cwd=REPO,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def seed(tree: Path) -> None:
    # An existing (even empty) server-membership file switches the namespace to
    # explicit routing. The imported allocation is the server-side claim for a
    # record that intentionally does not enter configs/all.npc.compack.
    put(tree / "pack/npc.server", "")
    put(
        tree / "fields/npc.ini",
        "[npc.hitpoints]\n"
        "scope = server\n"
        "client = drop\n"
        "server = opcode:77:u2\n",
    )
    put(tree / "ported/fixture/PROVENANCE.md", "fixture lane\n")
    put(tree / "ported/fixture/pack/npc.alloc", "25000=foreign_npc\n")
    put(
        tree / "server/scripts/fixture/configs/foreign.npc",
        "[foreign_npc]\n"
        "hitpoints=18750\n",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cachepack", type=Path, default=DEFAULT_CACHEPACK)
    args = parser.parse_args()
    cachepack = args.cachepack.resolve()
    if not cachepack.is_file():
        raise SystemExit(f"missing cachepack binary: {cachepack}")

    with tempfile.TemporaryDirectory(prefix="rs2012-server-overlay-") as tmp:
        tree = Path(tmp) / "content"
        seed(tree)

        packed = invoke(cachepack, tree)
        combined = packed.stdout + packed.stderr
        assert packed.returncode == 0, combined
        assert "1 band(s) claimed by pack/npc.alloc" in combined, combined
        assert "Server pack: 1 record(s)" in combined, combined
        assert "0 unresolved names" in combined, combined
        assert (tree / "server/pack/main_file_cache.dat2").is_file()
        assert (tree / "server/pack/main_file_cache.idx9").is_file()

        # A second lane cannot silently steal the same destination id. This is
        # the negative control that keeps the overlay support from weakening the
        # namespace diagnostics it extends.
        put(tree / "ported/collision/PROVENANCE.md", "collision fixture\n")
        put(tree / "ported/collision/pack/npc.alloc", "25000=other_foreign_npc\n")
        rejected = invoke(cachepack, tree)
        combined = rejected.stdout + rejected.stderr
        assert rejected.returncode != 0, combined
        assert "imported allocation ids must be disjoint" in combined, combined

    print("rs2012_server_overlay: PASS (lane id routed; collision rejected)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
