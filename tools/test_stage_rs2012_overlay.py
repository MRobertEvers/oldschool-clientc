#!/usr/bin/env python3
"""Hermetic safety/merge test for stage_rs2012_overlay.py."""

from __future__ import annotations

import hashlib
import importlib.util
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
STAGER = REPO / "tools/stage_rs2012_overlay.py"


def load_stager():
    spec = importlib.util.spec_from_file_location("stage_rs2012_overlay", STAGER)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def put(path: Path, value: str | bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(value, bytes):
        path.write_bytes(value)
    else:
        path.write_text(value)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    stage_module = load_stager()
    with tempfile.TemporaryDirectory(prefix="rs2012-stage-test-") as tmp:
        root = Path(tmp)
        tree = root / "content"
        out = root / "stage"
        lane = tree / "ported/rs2012_qbd_td"

        put(tree / "meta.ini", "[cache]\n")
        put(tree / "content.ini", "[content]\n")
        put(tree / "fields/loc.ini", "[name]\nclient=native\n")
        put(tree / "configs/all.loc.compack", "1=base_loc\n")
        put(lane / "PROVENANCE.md", "fixture\n")
        put(
            lane / "BASE_PLACEMENTS.tsv",
            "square\tlevel\tx\tz\tsource_loc\tshape\tangle\tevidence\n"
            "46_50\t0\t44\t36\t70792\t10\t0\tfixture\n",
        )
        put(
            tree / "port/rs2012_qbd_td.map",
            "kind\tsource_id\tsource_name\tdest_id\tdest_name\tdisposition\tsignoff\n"
            "loc\t70792\tloc_70792\t63001\trs2012_loc_70792\tminted\tunreviewed\n",
        )
        put(lane / "configs/rs2012.loc", "[rs2012_loc_1]\nname=Ported\n")
        put(lane / "configs/rs2012.overlay", "[rs2012_overlay_1]\ncolour=0x010203\n")
        put(lane / "pack/loc.alloc", "63000=rs2012_loc_1\n")
        put(lane / "pack/loc.client", "rs2012_loc_1\n")
        put(lane / "pack/5_maps.pack", "10074=m39_90\n")
        put(lane / "textures/texture_0.texture", "[rs2012_material_408]\nrgb=0x010203\n")
        put(lane / "textures/texture_0.compack", "200=rs2012_material_408\n")
        put(tree / "textures/texture_0.texture", "[base_material]\nrgb=0xAABBCC\n")
        put(tree / "textures/texture_0.compack", "0=base_material\n")
        put(lane / "maps/m39_90.jm2", "==== MAP ====\n0 0 0: h1\n")
        put(lane / "maps/m39_90.jl2", "==== LOC ====\n0 0 0: 63000 10 0\n")
        put(
            lane / "maps/m39_90.filepack",
            "0=m39_90.jm2\n1=m39_90.jl2\n2=m39_90/2.bin\n"
            "3=m39_90/3.bin\n4=m39_90/4.bin\n",
        )

        base_files = {
            tree / "maps/m39_90.jm2": b"modern terrain",
            tree / "maps/m39_90.jl2": b"modern locs",
            tree / "maps/m39_90/2.bin": b"aux two",
            tree / "maps/m39_90/3.bin": b"aux three",
            tree / "maps/m39_90/4.bin": b"aux four",
            tree / "maps/m46_50.jm2": b"==== MAP ====\n0 0 0: h1\n",
            tree / "maps/m46_50.jl2": b"==== LOC ====\n0 1 1: 10 10 0\n",
            tree / "maps/m46_50.filepack": (
                b"0=m46_50.jm2\n1=m46_50.jl2\n2=m46_50/2.bin\n"
                b"3=m46_50/3.bin\n4=m46_50/4.bin\n"
            ),
            tree / "maps/m46_50/2.bin": b"surface aux two",
            tree / "maps/m46_50/3.bin": b"surface aux three",
            tree / "maps/m46_50/4.bin": b"surface aux four",
        }
        for path, data in base_files.items():
            put(path, data)
        before = {path: sha(path) for path in base_files}

        assert stage_module.stage(tree, out) == 0

        # Historical members 0/1 shadow only the disposable output.
        assert (out / "maps/m39_90.jm2").read_text().startswith("==== MAP ====")
        assert (out / "maps/m39_90.jl2").read_text().startswith("==== LOC ====")
        # Modern auxiliary archive members survive alongside them.
        for ident in (2, 3, 4):
            assert (out / f"maps/m39_90/{ident}.bin").read_bytes() == base_files[
                tree / f"maps/m39_90/{ident}.bin"
            ]
        # The RS727 surface entrance was server-dynamic. The staged square
        # keeps all base members and appends only the ledger-remapped loc.
        assert (out / "maps/m46_50.jm2").read_bytes() == base_files[
            tree / "maps/m46_50.jm2"
        ]
        assert (out / "maps/m46_50.jl2").read_text().endswith(
            "0 44 36: 63001 10 0\n"
        )
        assert (out / "maps/m46_50.filepack").read_bytes() == base_files[
            tree / "maps/m46_50.filepack"
        ]
        for ident in (2, 3, 4):
            assert (out / f"maps/m46_50/{ident}.bin").read_bytes() == base_files[
                tree / f"maps/m46_50/{ident}.bin"
            ]
        assert "11826=m46_50\n" in (out / "pack/5_maps.pack").read_text()
        # Config layers and their id authorities coexist in the sparse stage.
        assert (out / "configs/all.loc.compack").read_text() == "1=base_loc\n"
        assert "[rs2012_loc_1]" in (out / "configs/rs2012.loc").read_text()
        assert (out / "pack/loc.alloc").read_text() == "63000=rs2012_loc_1\n"
        texture_text = (out / "textures/texture_0.texture").read_text()
        assert "[base_material]" in texture_text and "[rs2012_material_408]" in texture_text
        assert (out / "textures/texture_0.compack").read_text() == (
            "0=base_material\n200=rs2012_material_408\n"
        )
        # Base bytes are immutable, including paths shadowed by the lane.
        assert {path: sha(path) for path in base_files} == before

    print("stage_rs2012_overlay: PASS (map member merge + base immutability)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
