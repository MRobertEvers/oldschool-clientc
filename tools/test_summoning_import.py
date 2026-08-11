#!/usr/bin/env python3
"""Permanent checks for the Spirit-wolf and familiar-arrival cache import."""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    value = hashlib.sha256()
    value.update(path.read_bytes())
    return value.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--importer", type=Path, default=REPO / "3rd/rscache/tools/cachepack/cachepack")
    parser.add_argument("--manifest", type=Path, default=REPO / "docs/summoning_port/spirit_wolf_import.ini")
    parser.add_argument("--tree", type=Path, default=REPO / "OSRS-Content/osrs239-content")
    args = parser.parse_args()

    lane = args.tree / "ported/scape2009_summoning"
    files = [
        lane / "configs/summoning.npc",
        lane / "configs/summoning.obj",
        lane / "configs/summoning.loc",
        lane / "configs/summoning.spotanim",
        lane / "configs/summoning.seq",
        lane / "pack/npc.alloc",
        lane / "pack/obj.alloc",
        lane / "pack/loc.alloc",
        lane / "pack/spotanim.alloc",
        lane / "pack/seq.alloc",
        lane / "pack/npc.client",
        lane / "pack/obj.client",
        lane / "pack/loc.client",
        lane / "pack/spotanim.client",
        lane / "pack/seq.client",
        lane / "pack/7_models.pack",
        lane / "pack/0_animations.pack",
        lane / "pack/1_skeletons.pack",
        args.tree / "models/ported/scape2009_summoning/summoning_model_30443.model",
        args.tree / "models/ported/scape2009_summoning/summoning_model_31211.model",
        args.tree / "models/ported/scape2009_summoning/summoning_model_30591.model",
        args.tree / "models/ported/scape2009_summoning/summoning_model_31279.model",
        args.tree / "models/ported/scape2009_summoning/summoning_model_31553.model",
        args.tree / "models/ported/scape2009_summoning/summoning_model_31686.model",
        args.tree / "models/ported/scape2009_summoning/summoning_model_31427.model",
        args.tree / "models/ported/scape2009_summoning/summoning_model_31388.model",
        args.tree / "models/ported/scape2009_summoning/summoning_model_30826.model",
        args.tree / "animsets/ported/scape2009_summoning/summoning_animset_1662.anim",
        args.tree / "animsets/ported/scape2009_summoning/summoning_animset_1662.memberpack",
        args.tree / "animsets/ported/scape2009_summoning/summoning_animset_2109.anim",
        args.tree / "animsets/ported/scape2009_summoning/summoning_animset_2109.memberpack",
        args.tree / "animsets/ported/scape2009_summoning/summoning_animset_1990.anim",
        args.tree / "animsets/ported/scape2009_summoning/summoning_animset_1990.memberpack",
        args.tree / "animsets/ported/scape2009_summoning/summoning_animset_2053.anim",
        args.tree / "animsets/ported/scape2009_summoning/summoning_animset_2053.memberpack",
        args.tree / "animsets/ported/scape2009_summoning/summoning_animset_2226.anim",
        args.tree / "animsets/ported/scape2009_summoning/summoning_animset_2226.memberpack",
        args.tree / "animsets/ported/scape2009_summoning/summoning_animset_1998.anim",
        args.tree / "animsets/ported/scape2009_summoning/summoning_animset_1998.memberpack",
        args.tree / "framemaps/ported/scape2009_summoning/summoning_framemap_1491.base",
        args.tree / "framemaps/ported/scape2009_summoning/summoning_framemap_1901.base",
        args.tree / "framemaps/ported/scape2009_summoning/summoning_framemap_0.base",
        args.tree / "framemaps/ported/scape2009_summoning/summoning_framemap_1775.base",
        args.tree / "framemaps/ported/scape2009_summoning/summoning_framemap_1836.base",
        args.tree / "port/summoning_530.map",
    ]
    errors: list[str] = []
    checked = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal checked
        checked += 1
        if not condition:
            errors.append(message)

    for path in files:
        expect(path.is_file() and path.stat().st_size > 0, f"missing or empty: {path}")
    if errors:
        for error in errors:
            print(f"test_summoning_import: error: {error}", file=sys.stderr)
        print(f"test_summoning_import: {checked} checks, {len(errors)} errors")
        return 1

    before = {path: digest(path) for path in files}
    result = subprocess.run(
        [str(args.importer), "import", "--manifest", str(args.manifest)],
        cwd=REPO,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    expect(result.returncode == 0, f"dry-run exited {result.returncode}: {result.stdout}")
    expect(
        "cachepack import (dry-run): npc=1 obj=4 loc=1 spotanim=3" in result.stdout
        and "model=9 seq=8 animset=6 framemap=5" in result.stdout,
        "dry-run closure changed or did not execute",
    )
    for path in files:
        expect(digest(path) == before[path], f"dry-run changed {path}")

    npc = (lane / "configs/summoning.npc").read_text(encoding="utf-8")
    expect("[summoning_spirit_wolf]" in npc, "wolf NPC name is not prefixed")
    expect("readyanim=summoning_seq_8297" in npc, "wolf idle animation is not explicit")
    expect("walkanim=summoning_seq_8291" in npc, "wolf walk animation is not explicit")
    expect("bastype=" not in npc and "swarm_walk" not in npc, "Bas/default animation leaked")
    expect("retex" not in npc, "cache-local NPC texture id leaked")

    obj = (lane / "configs/summoning.obj").read_text(encoding="utf-8")
    expect("[summoning_blank_pouch]" in obj, "blank pouch is not prefixed")
    expect("model=100423" in obj, "blank pouch does not retain model 30826 mapping")
    expect("stackable=1" in obj, "blank pouch did not retain its stackable policy")

    loc = (lane / "configs/summoning.loc").read_text(encoding="utf-8")
    expect("[summoning_obelisk]" in loc, "obelisk loc name is not prefixed")
    expect("models=100005" in loc, "obelisk did not retain its sole model 31686 mapping")
    expect("anim=summoning_seq_8510" in loc, "obelisk animation is not explicit")
    expect("op1=Infuse-pouch" in loc, "obelisk Infuse-pouch operation is absent")
    expect("op2=Renew-points" in loc, "obelisk Renew-points operation is absent")
    expect("shape1=" not in loc, "rev-643 nested-model decode leaked into the rev-530 loc")

    seq = (lane / "configs/summoning.seq").read_text(encoding="utf-8")
    expect("[summoning_obelisk_charge]" in seq, "obelisk charge sequence is absent")
    expect("[summoning_infuse_anim]" in seq, "pouch-infusion animation is absent")
    expect("framestep=27" in seq, "pouch-infusion sequence lost its source frame step")

    spotanim = (lane / "configs/summoning.spotanim").read_text(encoding="utf-8")
    expect("[summoning_renew_points_gfx]" in spotanim, "Renew-points gfx is not prefixed")
    expect("model=100006" in spotanim, "Renew-points gfx does not use model 31427 mapping")
    expect("anim=summoning_seq_7662" in spotanim, "Renew-points gfx sequence is absent")
    expect("[summoning_familiar_summon_small_gfx]" in spotanim,
           "small familiar-arrival graphic is absent")
    expect("[summoning_familiar_summon_large_gfx]" in spotanim,
           "large familiar-arrival graphic is absent")
    expect(spotanim.count("model=100424") == 2,
           "arrival graphics do not share source model 31388")
    expect(spotanim.count("anim=summoning_seq_7663") == 2,
           "arrival graphics do not share source sequence 7663")
    expect("resizeh=200" in spotanim and "resizev=200" in spotanim,
           "large familiar-arrival graphic lost its source scaling")

    for invalid_model in (0, 591, 25189, 27753, 29547):
        expect(
            not (args.tree / f"models/ported/scape2009_summoning/summoning_model_{invalid_model}.model").exists(),
            f"stale model from desynchronized loc decode remains: {invalid_model}",
        )

    member_lines = [
        line for line in (args.tree / "animsets/ported/scape2009_summoning/summoning_animset_1662.memberpack")
        .read_text(encoding="utf-8")
        .splitlines()
        if line and not line.startswith("//")
    ]
    expect(len(member_lines) > 0, "animation member metadata executed zero checks")
    expect(member_lines[0].startswith("0=frame_0"), "animation member ids do not start at zero")

    ledger = (args.tree / "port/summoning_530.map").read_text(encoding="utf-8")
    for row in (
        "npc\t6829\tspirit_wolf\t20000\tsummoning_spirit_wolf\tminted\tunreviewed",
        "obj\t12047\tspirit_wolf_pouch\t40000\tsummoning_spirit_wolf_pouch\tminted\tunreviewed",
        "obj\t12183\tshard\t40001\tsummoning_shard\tminted\tunreviewed",
        "obj\t12158\tcharm_gold\t40002\tsummoning_charm_gold\tminted\tunreviewed",
        "obj\t12155\tblank_pouch\t40255\tsummoning_blank_pouch\tminted\tunreviewed",
        "framemap\t1491\tframemap_1491\t8000\tsummoning_framemap_1491\tminted\tunreviewed",
        "loc\t28716\tobelisk\t62201\tsummoning_obelisk\tminted\tunreviewed",
        "model\t31686\tmodel_31686\t100005\tsummoning_model_31686\tminted\tunreviewed",
        "seq\t8510\tseq_8510\t20002\tsummoning_seq_8510\tminted\tunreviewed",
        "frame_archive\t2109\tanimset_2109\t20001\tsummoning_animset_2109\tminted\tunreviewed",
        "framemap\t1901\tframemap_1901\t8001\tsummoning_framemap_1901\tminted\tunreviewed",
        "spotanim\t1308\trenew_points_gfx\t20000\tsummoning_renew_points_gfx\tminted\tunreviewed",
        "spotanim\t1314\tfamiliar_summon_small_gfx\t20001\tsummoning_familiar_summon_small_gfx\tminted\tunreviewed",
        "spotanim\t1315\tfamiliar_summon_large_gfx\t20002\tsummoning_familiar_summon_large_gfx\tminted\tunreviewed",
        "model\t31427\tmodel_31427\t100006\tsummoning_model_31427\tminted\tunreviewed",
        "model\t31388\tmodel_31388\t100424\tsummoning_model_31388\tminted\tunreviewed",
        "seq\t8502\tseq_8502\t20003\tsummoning_renew_points_anim\tminted\tunreviewed",
        "seq\t7662\tseq_7662\t20004\tsummoning_seq_7662\tminted\tunreviewed",
        "frame_archive\t1990\tanimset_1990\t20002\tsummoning_animset_1990\tminted\tunreviewed",
        "frame_archive\t2053\tanimset_2053\t20003\tsummoning_animset_2053\tminted\tunreviewed",
        "framemap\t0\tframemap_0\t8002\tsummoning_framemap_0\tminted\tunreviewed",
        "framemap\t1775\tframemap_1775\t8003\tsummoning_framemap_1775\tminted\tunreviewed",
        "model\t30826\tmodel_30826\t100423\tsummoning_model_30826\tminted\tunreviewed",
        "seq\t8509\tseq_8509\t20310\tsummoning_obelisk_charge\tminted\tunreviewed",
        "seq\t9068\tseq_9068\t20311\tsummoning_infuse_anim\tminted\tunreviewed",
        "frame_archive\t2226\tanimset_2226\t20070\tsummoning_animset_2226\tminted\tunreviewed",
        "seq\t7663\tseq_7663\t20312\tsummoning_seq_7663\tminted\tunreviewed",
        "frame_archive\t1998\tanimset_1998\t20071\tsummoning_animset_1998\tminted\tunreviewed",
        "framemap\t1836\tframemap_1836\t8069\tsummoning_framemap_1836\tminted\tunreviewed",
    ):
        expect(row in ledger, f"ledger row absent: {row}")

    for error in errors:
        print(f"test_summoning_import: error: {error}", file=sys.stderr)
    print(f"test_summoning_import: {checked} checks, {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
