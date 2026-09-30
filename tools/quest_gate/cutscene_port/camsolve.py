#!/usr/bin/env python3
"""Photograph candidate camera framings next to a video frame, in one client run.

    camsolve.py --quest <test_id> --stand <x,z,level> --clip <clip.mp4> --at <seconds>
                --cand NAME=ex,ez,eh>lx,lz,lh [--cand ...] [--speed rate,rate2]
                [--landmark <loc symbol> ...] [--name <run name>]

Each --cand is a cam_moveto eye (world tile x,z and height) and a cam_lookat
target (world tile x,z and height). The player is put at --stand (where the
real cutscene's player stands, so the loaded scene is the right one), each
candidate is framed with the ::cam debugproc (general/scripts/misc/
cheat_cam.rs2), photographed, and released with ::camreset. --landmark symbols
are looked up with t.world.loc_near so the look-at can be anchored on real
tiles.

Output: build/quest_gate/<run name>/compare.png, plus a copy per round in
build/quest_gate/<run name>.rounds/compare_NN.png (run.py wipes the run
directory itself on every run) -- the video frame's game
canvas first, then every candidate's client canvas, labelled -- plus the
ledger's yaw/pitch line per candidate on stdout. A run takes about ten
seconds; iterate in rounds of four. --hud hidden photographs in the state a
hidden-HUD cutscene draws (wider view, no panel); use it for template C/D
shots. The client runs at --canvas (default
1024x768, which lays out in resizable-classic mode like most recordings; pass
765x503 for a fixed-mode recording) so the two pictures share a layout.

The winning candidate goes into the .rs2 as
    cam_moveto(<eye coord>, <eh>, 100, 100); cam_lookat(<look coord>, <lh>, 100, 100);
with the coord literals this tool prints.
"""
import argparse
import csv
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import frames  # noqa: E402

from PIL import Image  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def coord(x, z, level):
    return "%d_%d_%d_%d_%d" % (level, x // 64, z // 64, x % 64, z % 64)


def parse_cand(text):
    m = re.fullmatch(r"([\w-]+)=(\d+),(\d+),(-?\d+)>(\d+),(\d+),(-?\d+)", text.strip())
    assert m, "candidate must be NAME=ex,ez,eh>lx,lz,lh, not " + text
    return {"name": m.group(1), "eye": tuple(int(m.group(i)) for i in (2, 3, 4)), "look": tuple(int(m.group(i)) for i in (5, 6, 7))}


def lua(args, cands):
    sx, sz, lvl = args.stand
    lines = ["return {", '    id = "%s",' % args.name, '    fixture = "fresh_lumbridge.ini",',
             '    setup = { "::goto %d %d %d" },' % (sx, sz, lvl), "    run = function(t)", "        t.ticks(8)",
             "        local r, tile = t.world.tile()",
             '        t.check("solve.standing", r == "ok", "world.tile() -> " .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z) .. "," .. tostring(tile and tile.level))']
    for sym in args.landmark:
        lines.append('        do local r2, row = t.world.loc_near("%s", 60); t.note("%s: " .. tostring(r2) .. " " .. (type(row) == "table" and (tostring(row.tile_x) .. "," .. tostring(row.tile_z) .. "," .. tostring(row.level)) or tostring(row))) end' % (sym, sym))
    if args.landmark:
        lines.append('        t.check("solve.landmarks", true, "see notes")')
    rate, rate2 = args.speed
    if args.hud == "hidden":
        lines += ['        t.cheat("::camhud 1", true)', "        t.ticks(2)"]
    for c in cands:
        cmd = "::cam %s %d %s %d %d %d" % (coord(c["eye"][0], c["eye"][1], lvl), c["eye"][2], coord(c["look"][0], c["look"][1], lvl), c["look"][2], rate, rate2)
        lines += ['        t.cheat("%s", true)' % cmd, "        t.ticks(%d)" % (3 if rate2 >= 100 else 12),
                  "        do local cam = t.world.camera()",
                  '        t.check("cand.%s", cam ~= nil and cam.server_driven == true, "%s -> eye " .. tostring(cam and cam.x) .. "," .. tostring(cam and cam.z) .. " yaw " .. tostring(cam and cam.yaw) .. " pitch " .. tostring(cam and cam.pitch)) end' % (c["name"], cmd),
                  '        t.cheat("::camreset", true)', "        t.ticks(2)"]
    if args.hud == "hidden":
        lines += ['        t.cheat("::camhud 0", true)', "        t.ticks(1)"]
    lines += ["        t.finish(0)", "    end,", "}"]
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--quest", required=True)
    ap.add_argument("--stand", required=True, type=lambda s: tuple(int(v) for v in s.split(",")))
    ap.add_argument("--clip", required=True)
    ap.add_argument("--at", required=True, type=float, help="video time (seconds into the clip) of the frame to match")
    ap.add_argument("--cand", action="append", default=[], help="NAME=ex,ez,eh>lx,lz,lh")
    ap.add_argument("--speed", default="100,100", type=lambda s: tuple(int(v) for v in s.split(",")), help="rate,rate2 (100,100 = cut)")
    ap.add_argument("--landmark", action="append", default=[], help="loc symbol whose tile to report")
    ap.add_argument("--name")
    ap.add_argument("--canvas", default="1024x768", help="client canvas WxH; 1024x768 lays out like a resizable-classic recording, 765x503 like a fixed one")
    ap.add_argument("--hud", default="up", choices=("up", "hidden"), help="hidden: photograph with the HUD in cutscene state (%%cutscene_status etc., ::camhud), as a hidden-HUD cutscene draws")
    args = ap.parse_args()
    assert args.cand, "at least one --cand"
    assert len(args.stand) == 3, "--stand x,z,level"
    args.name = args.name or "camsolve_" + args.quest
    cands = [parse_cand(c) for c in args.cand]
    out_dir = os.path.join(REPO, "build", "quest_gate", args.name)
    script = os.path.join(REPO, "build", "quest_gate", "%s.solve.lua" % args.name)
    os.makedirs(os.path.dirname(script), exist_ok=True)
    open(script, "w").write(lua(args, cands))
    env = dict(os.environ, TORIRS_ROOT_SIZE=args.canvas)
    r = subprocess.run([sys.executable, os.path.join(REPO, "tools/quest_gate/run.py"), "--script", script, "--name", args.name,
                        "--no-build", "--no-publish"], capture_output=True, text=True, cwd=REPO, env=env)
    ledger = os.path.join(out_dir, "ledger.tsv")
    assert os.path.isfile(ledger), "no ledger: " + r.stdout[-800:] + r.stderr[-400:]
    rows = list(csv.DictReader((l for l in open(ledger) if not l.startswith("quest-ledger") and not l.startswith("SUMMARY")), delimiter="\t"))
    shots = {}
    for row in rows:
        print("%-28s %-5s %s" % (row["step"], row["verdict"], row["detail"][:200]))
        if row["step"].startswith("cand.") and row["shots"]:
            shots[row["step"][5:]] = os.path.join(out_dir, "shots", row["shots"].split(",")[-1] + ".png")
    # compare sheet: video world view, then each candidate's
    vf = os.path.join(out_dir, "video_%.1fs.png" % args.at)
    frames.frame(args.clip, args.at, vf)
    # like for like: the recording's whole game canvas next to the client's whole canvas (both 4:3 at the default canvas)
    tiles = [frames.label(frames.game_area(Image.open(vf).convert("RGB")).resize((640, 480)), "video %.1fs" % args.at)]
    for c in cands:
        p = shots.get(c["name"])
        if p and os.path.isfile(p):
            tiles.append(frames.label(Image.open(p).convert("RGB").resize((640, 480)),
                                      "%s eye %d,%d h%d > %d,%d h%d" % ((c["name"],) + c["eye"] + c["look"])))
    cols = 3
    page = Image.new("RGB", (cols * 640, ((len(tiles) + cols - 1) // cols) * 480))
    for i, im in enumerate(tiles):
        page.paste(im, ((i % cols) * 640, (i // cols) * 480))
    cmp_path = os.path.join(out_dir, "compare.png")
    page.save(cmp_path)
    # every round's image is kept in a sibling directory that run.py never wipes:
    # build/quest_gate/<name>.rounds/compare_01.png, compare_02.png, ...
    rounds_dir = out_dir + ".rounds"
    os.makedirs(rounds_dir, exist_ok=True)
    kept = sorted(f for f in os.listdir(rounds_dir) if re.fullmatch(r"compare_\d+\.png", f))
    round_path = os.path.join(rounds_dir, "compare_%02d.png" % (len(kept) + 1))
    page.save(round_path)
    print("\ncompare sheet:", cmp_path, "(kept as %s)" % round_path)
    lvl = args.stand[2]
    for c in cands:
        print("%s: cam_moveto(%s, %d, 100, 100); cam_lookat(%s, %d, 100, 100);" % (
            c["name"], coord(c["eye"][0], c["eye"][1], lvl), c["eye"][2], coord(c["look"][0], c["look"][1], lvl), c["look"][2]))


if __name__ == "__main__":
    main()
