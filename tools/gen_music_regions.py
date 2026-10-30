#!/usr/bin/env python3
"""Generate the mock server's region -> music track table.

    tools/gen_music_regions.py > src/torirsserver/torirs_server_music_regions.gen.h

The client cache says which music tracks exist, what they are called, where the
game claims they unlock (as prose) and which varp bit records the unlock. It
does **not** say which map square plays which track -- that mapping is server
data, and there is no copy of it in any cache. So this table is a join of two
sources, both checked in under docs/audio/ (plus music_areas.tsv, the
hand-authored rectangles smaller than a square, each citing its recording):

  music_regions.tsv          region id -> track name, extracted from Kronos's
                             MusicPlayer.java (OSRS 184 era, 440 entries over
                             435 distinct map squares)
  music_tracks_osrs239.tsv   track name -> song archive id + unlock (variable,
                             bit), dumped from DBTable 44 of cache.osrs239

and a third, read from the content tree at generation time:

  interface_music/configs/music.varp
                             music VARIABLE N -> the varp id that holds it,
                             from the `[varp<id>_musicmulti_<N>]` declarations

The DBTable's pair is NOT a varp id. Its first half is the music variable,
1-27, the index CS2 7305/7306 and `[proc,music_unlock_var]` switch on;
variable 5 lives in varp 24 (`%varp24_musicmulti_5`), not varp 5. Emitting
the variable as a varp made every region unlock OR a bit into a quest varp:
Draynor Village (square 48,50, "Unknown Land" = variable 5 bit 5) turned
%varp5_grail's spoken_crone (4) into 36. So the table's `varp` column is
the musicmulti varp id, mapped here, and the generator refuses to write a
row whose variable has no musicmulti varp or whose varp lands below the
first musicmulti varp.

Names are matched case-insensitively and against both the display and sort
forms, because the two sources disagree on the capitalisation of small words
("Army Of Darkness" vs "Army of Darkness") and on where the article goes
("Fallen Empire, The" vs "The Fallen Empire").

The output is committed. Regenerate it when either input changes; do not hand
edit it.
"""

import argparse
import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REGIONS = ROOT / "docs/audio/music_regions.tsv"
# Rectangles inside a square that play their own track (hand-authored, each
# row citing its recording).
AREAS = ROOT / "docs/audio/music_areas.tsv"
TRACKS = ROOT / "docs/audio/music_tracks_osrs239.tsv"
# The content tree's own music table (cache 241's DBTable `music`): tracks
# newer than the 239 dump, matched only by names the dump lacks.
DBROWS = ROOT / "OSRS-Content/osrs-content/configs/all.dbrow"
MUSIC_VARP = (
    ROOT
    / "OSRS-Content/osrs-content/server/scripts/interface_music/configs/music.varp"
)


def load_musicmulti(path):
    """Music variable N -> varp id, from `[varp<id>_musicmulti_<N>]` lines."""
    pattern = re.compile(r"^\[varp(\d+)_musicmulti_(\d+)\]\s*$")
    variable_to_varp = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = pattern.match(line.strip())
            if not m:
                continue
            varp, variable = int(m.group(1)), int(m.group(2))
            if variable in variable_to_varp:
                sys.exit("gen_music_regions: musicmulti_%d declared twice in %s" % (variable, path))
            variable_to_varp[variable] = varp
    if not variable_to_varp:
        sys.exit("gen_music_regions: no [varp<id>_musicmulti_<N>] declarations in %s" % path)
    return variable_to_varp


def load_dbrow_music(path):
    """`table=music` rows of all.dbrow, shaped like the TSV's rows."""
    out = []
    row = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("["):
                row = {"display_name": "", "sort_name": "", "song_archive_id": "", "unlock_varp,bit": ""}
                continue
            if row is None:
                continue
            if line == "table=music":
                out.append(row)
            elif line.startswith("data=displayname,"):
                row["display_name"] = line.split(",", 1)[1]
            elif line.startswith("data=sortname,"):
                row["sort_name"] = line.split(",", 1)[1]
            elif line.startswith("data=midi,"):
                row["song_archive_id"] = line.split(",", 1)[1]
            elif line.startswith("data=variable,"):
                row["unlock_varp,bit"] = line.split(",", 1)[1]
    return out


def load(path):
    # The cache's own strings are windows-1252, which is what the dump carries.
    #
    # `#` lines are dropped before the reader sees them, so a row that did not
    # come from the bulk extraction can say where it did come from in the file
    # it lives in. Without this a comment becomes a row and `int(region_id)`
    # raises on it -- and putting the provenance only in this generator would
    # leave the data file looking uniformly sourced when it is not.
    with open(path, encoding="cp1252") as f:
        rows = (line for line in f if not line.lstrip().startswith("#"))
        return list(csv.DictReader(rows, delimiter="\t"))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument(
        "--music-varp",
        default=str(MUSIC_VARP),
        help="the content tree's interface_music/configs/music.varp",
    )
    args = parser.parse_args()

    regions = load(REGIONS)
    tracks = load(TRACKS)
    variable_to_varp = load_musicmulti(args.music_varp)
    first_musicmulti_varp = min(variable_to_varp.values())

    by_name = {}
    for row in tracks + load_dbrow_music(DBROWS):
        for key in (row["display_name"], row["sort_name"]):
            if key:
                by_name.setdefault(key.strip().lower(), row)

    def resolve(name):
        """(song, varp, bit, display name) for a track name, or None."""
        hit = by_name.get(name.lower())
        if not hit:
            return None
        varp, bit = -1, -1
        pair = hit.get("unlock_varp,bit") or ""
        if "," in pair:
            variable, bit = (int(x) for x in pair.split(",", 1))
            if variable not in variable_to_varp:
                sys.exit(
                    "gen_music_regions: %s unlocks music variable %d, which no "
                    "[varp<id>_musicmulti_%d] in %s declares"
                    % (name, variable, variable, args.music_varp)
                )
            varp = variable_to_varp[variable]
            if varp < first_musicmulti_varp:
                sys.exit("gen_music_regions: %s maps to varp %d, below musicmulti_1" % (name, varp))
        song = hit.get("song_archive_id")
        if not song:
            return None
        return int(song), varp, bit, hit["display_name"]

    rows = []
    unmatched = []
    for r in regions:
        name = r["track_name"].strip()
        got = resolve(name)
        if not got:
            unmatched.append(name)
            continue
        song, varp, bit, display = got
        rows.append(
            {
                "region": int(r["region_id"]),
                "song": song,
                "varp": varp,
                "bit": bit,
                "name": display,
            }
        )

    areas = []
    for a in load(AREAS):
        name = a["track_name"].strip()
        got = resolve(name)
        if not got:
            sys.exit("gen_music_regions: music_areas.tsv names %r, which no track table has" % name)
        song, varp, bit, display = got
        x1, z1, x2, z2 = (int(a[k]) for k in ("x1", "z1", "x2", "z2"))
        if x1 > x2 or z1 > z2 or (x1 >> 6) != (x2 >> 6) or (z1 >> 6) != (z2 >> 6):
            sys.exit("gen_music_regions: music area %s is not a rectangle inside one square" % name)
        areas.append(
            {
                "x1": x1, "z1": z1, "x2": x2, "z2": z2,
                "lmin": int(a["level_min"]), "lmax": int(a["level_max"]),
                "region": ((x1 >> 6) << 8) | (z1 >> 6),
                "song": song, "varp": varp, "bit": bit, "name": display,
            }
        )

    # One track per square. Five squares are claimed by two tracks upstream
    # (Courage/Long Way Home at 11826, and four more); the first wins and the
    # rest are reported rather than silently dropped.
    seen = {}
    dupes = []
    for row in rows:
        if row["region"] in seen:
            dupes.append((row["region"], seen[row["region"]]["name"], row["name"]))
            continue
        seen[row["region"]] = row

    out = sys.stdout
    out.write("/*\n")
    out.write(" * GENERATED by tools/gen_music_regions.py -- do not edit.\n")
    out.write(" *\n")
    out.write(" * Map square -> music track. See the generator for provenance: this is a\n")
    out.write(" * join of a community region table with the cache's own DBTable 44, because\n")
    out.write(" * no cache states the region mapping at all.\n")
    out.write(" *\n")
    out.write(" * region id = ((tile_x >> 6) << 8) | (tile_z >> 6)\n")
    out.write(" * song      = js5 index 6 archive id, what MIDI_SONG names\n")
    out.write(" * varp/bit  = the unlock flag the music player reads; -1 when unknown.\n")
    out.write(" *             `varp` is the musicmulti VARP ID (music.varp), never the\n")
    out.write(" *             DBTable's music variable index 1-27 -- variable 5 is varp 24.\n")
    out.write(" */\n\n")
    out.write("#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_MUSIC_REGIONS_GEN_H\n")
    out.write("#define SRC_TORIRSSERVER_TORIRS_SERVER_MUSIC_REGIONS_GEN_H\n\n")
    out.write("/* The lowest musicmulti varp id (musicmulti_1); no row's varp is below it. */\n")
    out.write("#define TORIRSSERVER_MUSIC_FIRST_UNLOCK_VARP %d\n\n" % first_musicmulti_varp)
    out.write("struct ToriRSServerMusicRegion\n{\n")
    out.write("    int region;\n    int song;\n    int varp;\n    int bit;\n")
    out.write("    const char* name;\n};\n\n")
    out.write("static const struct ToriRSServerMusicRegion k_ToriRSServer_MusicRegions[] = {\n")
    for region in sorted(seen):
        row = seen[region]
        name = row["name"].replace("\\", "\\\\").replace('"', '\\"')
        out.write(
            '    { %5d, %4d, %3d, %3d, "%s" },\n'
            % (row["region"], row["song"], row["varp"], row["bit"], name)
        )
    out.write("};\n\n")
    out.write(
        "static const int k_ToriRSServer_MusicRegionCount =\n"
        "    (int)(sizeof(k_ToriRSServer_MusicRegions) / sizeof(k_ToriRSServer_MusicRegions[0]));\n\n"
    )
    out.write(
        "/* Music areas inside a square (docs/audio/music_areas.tsv): tiles x1..x2,\n"
        " * z1..z2 on levels level_min..level_max play `track` over the square's. */\n"
    )
    out.write("struct ToriRSServerMusicArea\n{\n")
    out.write("    int x1;\n    int z1;\n    int x2;\n    int z2;\n")
    out.write("    int level_min;\n    int level_max;\n")
    out.write("    struct ToriRSServerMusicRegion track;\n};\n\n")
    out.write("static const struct ToriRSServerMusicArea k_ToriRSServer_MusicAreas[] = {\n")
    for a in areas:
        name = a["name"].replace("\\", "\\\\").replace('"', '\\"')
        out.write(
            '    { %d, %d, %d, %d, %d, %d, { %5d, %4d, %3d, %3d, "%s" } },\n'
            % (a["x1"], a["z1"], a["x2"], a["z2"], a["lmin"], a["lmax"],
               a["region"], a["song"], a["varp"], a["bit"], name)
        )
    out.write("};\n\n")
    out.write(
        "static const int k_ToriRSServer_MusicAreaCount =\n"
        "    (int)(sizeof(k_ToriRSServer_MusicAreas) / sizeof(k_ToriRSServer_MusicAreas[0]));\n\n"
    )
    out.write("#endif\n")

    sys.stderr.write(
        "gen_music_regions: %d squares, %d areas, %d unmatched names, %d duplicate squares\n"
        % (len(seen), len(areas), len(set(unmatched)), len(dupes))
    )
    for name in sorted(set(unmatched)):
        sys.stderr.write("  unmatched: %s\n" % name)
    for region, kept, dropped in dupes:
        sys.stderr.write("  square %d: kept %s, dropped %s\n" % (region, kept, dropped))


if __name__ == "__main__":
    main()
