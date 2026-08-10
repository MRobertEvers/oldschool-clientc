#!/usr/bin/env python3
"""Fail-loud contract checks for the rev-727 QBD audio bridge."""

from __future__ import annotations

import argparse
import csv
import hashlib
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
TREE = REPO / "OSRS-Content" / "osrs239-content"
LANE = TREE / "ported" / "rs2012_qbd_td"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"rs2012 audio bridge: {message}")


def parse_pack(root: Path, name: str) -> dict[int, str]:
    lane_pack = root / "ported" / "rs2012_qbd_td" / "pack"
    path = (lane_pack if lane_pack.is_dir() else root / "pack") / name
    require(path.is_file(), f"missing {path}")
    rows: dict[int, str] = {}
    values: set[str] = set()
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.split("#", 1)[0].split(";", 1)[0].strip()
        if not line:
            continue
        require("=" in line, f"{path}:{number}: malformed row")
        ident_text, value = line.split("=", 1)
        ident = int(ident_text)
        require(ident not in rows, f"{path}:{number}: duplicate archive {ident}")
        require(value not in values, f"{path}:{number}: duplicate payload {value}")
        rows[ident] = value
        values.add(value)
    return rows


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def asset_for(root: Path, kind: str, logical: str, extension: str) -> Path:
    return root / kind / f"{logical}.{extension}"


def verify_tree(root: Path) -> None:
    synths = parse_pack(root, "4_soundeffects.pack")
    songs = parse_pack(root, "6_musictracks.pack")
    samples = parse_pack(root, "14_musicsamples.pack")
    patches = parse_pack(root, "15_musicpatches.pack")

    require(len(synths) == 29, f"expected 29 synth archives, got {len(synths)}")
    require(set(songs) == {1118, 1119}, f"song ids drifted: {sorted(songs)}")
    require(patches == {1157: "ported/rs2012_qbd_td/rs2012_patch_1157"},
            f"patch closure drifted: {patches}")
    require(len(samples) == 84, f"expected setup + 83 samples, got {len(samples)}")
    require(samples.get(16000) == "ported/rs2012_qbd_td/rs2012_sample_setup_727",
            "foreign setup is not index 14:16000")
    require(samples.get(17000) == "ported/rs2012_qbd_td/rs2012_sample_6249",
            "colliding source sample 6249 is not remapped to 17000")
    require(6249 not in samples, "unsafe index-14 archive 6249 mapping survived")
    require(16029 not in synths, "misclassified index-4 source 6249 survived")

    roots = {
        "4_soundeffects.pack": ("synth", "synth"),
        "6_musictracks.pack": ("songs", "jmid"),
        "14_musicsamples.pack": ("samples", "sample"),
        "15_musicpatches.pack": ("patches", "patch"),
    }
    for pack_name, rows in (("4_soundeffects.pack", synths),
                            ("6_musictracks.pack", songs),
                            ("14_musicsamples.pack", samples),
                            ("15_musicpatches.pack", patches)):
        kind, extension = roots[pack_name]
        for ident, logical in rows.items():
            path = asset_for(root, kind, logical, extension)
            require(path.is_file() and path.stat().st_size > 0,
                    f"{pack_name} archive {ident} has no non-empty payload: {path}")

    fixed = {
        root / "songs/ported/rs2012_qbd_td/rs2012_song_1118.jmid":
            (40, "08a2ec4740fb55fc178bfdb72baf70236a27c2692f7d9a340231f275213d4253"),
        root / "songs/ported/rs2012_qbd_td/rs2012_song_1119.jmid":
            (29, "436117f6ba1ddc55d0e5567ffb36f2bcfc2768cfc60754602accc753cdedc0ab"),
        root / "patches/ported/rs2012_qbd_td/rs2012_patch_1157.patch":
            (290, "640ae787ce97e68bf81b2d3d16aeb8829dc5aab72619a7492bdf9ae70e0b04e7"),
        root / "samples/ported/rs2012_qbd_td/rs2012_sample_setup_727.sample":
            (3208, "877ed15883f5909d931b68764d07c034a72fada3efb25288ec40d1913036353e"),
    }
    for path, (size, sha256) in fixed.items():
        require(path.stat().st_size == size, f"unexpected size for {path}")
        require(digest(path) == sha256, f"SHA-256 mismatch for {path}")

    lane_configs = root / "ported/rs2012_qbd_td/configs"
    configs = lane_configs if lane_configs.is_dir() else root / "configs"
    seq_ids: list[int] = []
    for line in (configs / "rs2012.seq").read_text().splitlines():
        if line.startswith("sound="):
            seq_ids.append(int(line.split(",", 2)[1]))
    require(len(seq_ids) == 55, f"expected 55 sequence sound events, got {len(seq_ids)}")
    require(sum(ident in samples for ident in seq_ids) == 48,
            "sequence recorded-sample event count is not 48")
    require(sum(ident in synths for ident in seq_ids) == 7,
            "sequence synth event count is not 7")
    require(all(ident in samples or ident in synths for ident in seq_ids),
            "sequence contains an unresolved sound id")

    loc_ids: list[int] = []
    for line in (configs / "rs2012.loc").read_text().splitlines():
        if line.startswith("soundid=") or line.startswith("soundrandom"):
            loc_ids.append(int(line.split("=", 1)[1]))
    require(len(loc_ids) == 103 and len(set(loc_ids)) == 72,
            f"loc sound closure drifted: {len(loc_ids)} refs/{len(set(loc_ids))} ids")
    require(len(set(loc_ids) & set(samples)) == 52,
            "loc recorded-sample closure is not 52 ids")
    require(all(ident in samples or ident in synths for ident in loc_ids),
            "loc contains an unresolved sound id")


def verify_authority() -> None:
    ledger = TREE / "port/rs2012_qbd_td.map"
    counts: dict[str, int] = {}
    with ledger.open(newline="") as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            counts[row["kind"]] = counts.get(row["kind"], 0) + 1
            if row["kind"] == "sample" and row["source_id"] == "6249":
                require(row["dest_id"] == "17000", "ledger does not remap sample 6249")
    for kind, expected in {"synth": 29, "song": 2, "sample": 83,
                           "sample_setup": 1, "patch": 1}.items():
        require(counts.get(kind) == expected,
                f"ledger {kind} count {counts.get(kind, 0)} != {expected}")

    session = (TREE / "server/scripts/minigames/minigame_rs2012_qbd/scripts/"
               "rs2012_qbd_session.rs2").read_text()
    awoken = session.index("midi_song(1119);")
    combat = session.index("if ($expected = 1) midi_song(1118);")
    require(awoken < combat, "QBD music dispatch order is not 1119 then 1118")
    require("[proc,rs2012_qbd_clear_state]\nmidi_song(-1);" in session,
            "QBD music is not stopped on shared leave/death/logout cleanup")

    sound_runtime = (REPO / "src/engine/dat2/task_dat2_sound_load.c").read_text()
    music_runtime = (REPO / "src/engine/dat2/task_dat2_music_load.c").read_text()
    require("RS2012_SAMPLE_SETUP_ID 16000" in sound_runtime and
            "ToriRS_SoundFromRSCacheSample" in sound_runtime,
            "recorded-effect runtime fallback is missing")
    require("RS2012_SAMPLE_SETUP_ID 16000" in music_runtime and
            "rs2012_vorbis_setup" in music_runtime,
            "QBD music runtime setup selection is missing")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--staged", type=Path,
                        help="also verify a tree produced by stage_rs2012_overlay.py")
    args = parser.parse_args()
    verify_tree(TREE)
    verify_authority()
    if args.staged is not None:
        verify_tree(args.staged.resolve())
    print("rs2012 audio bridge: PASS (29 synths, 83 samples + setup, 2 songs, 1 patch)")


if __name__ == "__main__":
    main()
