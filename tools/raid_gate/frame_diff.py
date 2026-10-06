#!/usr/bin/env python3
"""frame_diff -- find the frames where a small region of a video changes, so a
tick can be counted without reading frames by eye (docs/RAID_ORCHESTRATOR.md
section 3.1; the first frame-count pilot of 2026-10-03 measured nothing because
it tried to find a splat in whole frames).

    python3 tools/raid_gate/frame_diff.py <video id> <start> <end> --crop W:H:X:Y
            [--ref K] [--thresh T] [--sheet A:B]

<video id> is a video frame_count.py has already fetched (build/frames/<id>/full.mp4);
<start>/<end> are mm:ss or seconds; the crop is in source pixels (ffmpeg crop=W:H:X:Y)
and should hold ONE thing: the tile a splat lands on, the boss's body, a hitsplat.
Frame 0 is the first frame at <start>; a gap of g frames is g/fps/0.6 ticks.

Prints (always under 2 KB):
  - the fps and the frame count;
  - STEPS: the frames whose crop differs most from the frame before (a thing
    appearing, vanishing, or an animation starting), largest first, at most 24;
  - with --ref K: the RUNS of frames whose crop differs from frame K's by more
    than the threshold (the thing is "there" for a run when K is a frame without it).
--sheet A:B writes one contact sheet of the cropped frames A..B (6 per row,
numbered left to right, top to bottom from A) to build/frames/<id>/sheet_<A>_<B>.png:
Read that ONE image to confirm what the two anchor frames show. Never dump and
Read whole frames to search.

A moving camera defeats a fixed crop: pick footage where the camera is still
across the interval, or anchor on a screen-fixed thing (a hitsplat on the
player in the centre, the boss's health overlay, the chat line).
"""
import argparse
import json
import os
import subprocess
import sys

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SIDE = 48


def seconds(text):
    total = 0.0
    for p in text.split(":"):
        total = total * 60 + float(p)
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("start")
    ap.add_argument("end")
    ap.add_argument("--crop", required=True, help="W:H:X:Y in source pixels")
    ap.add_argument("--ref", type=int, default=None, help="frame to compare every frame against")
    ap.add_argument("--thresh", type=float, default=8.0, help="mean absolute grey difference (0-255)")
    ap.add_argument("--sheet", default=None, help="A:B frames to write as one contact sheet")
    a = ap.parse_args()
    base = os.path.join(ROOT, "build", "frames", a.video)
    full = os.path.join(base, "full.mp4")
    assert os.path.exists(full), "fetch the video first: frame_count.py <url> <start> <end>"
    s, e = seconds(a.start), seconds(a.end)
    assert e > s
    assert e - s <= 120, "keep the window under two minutes"
    probe = subprocess.run(["ffprobe", "-v", "quiet", "-print_format", "json", "-show_streams", full],
                           capture_output=True, text=True, check=True)
    stream = next(st for st in json.loads(probe.stdout)["streams"] if st["codec_type"] == "video")
    num, den = stream["r_frame_rate"].split("/")
    fps = float(num) / float(den)
    cut = ["ffmpeg", "-v", "quiet", "-ss", "%.3f" % s, "-to", "%.3f" % e, "-i", full]
    raw = subprocess.run(cut + ["-vf", "crop=%s,scale=%d:%d,format=gray" % (a.crop, SIDE, SIDE),
                                "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
    n = len(raw) // (SIDE * SIDE)
    assert n > 1, "no frames decoded: check the crop is inside %sx%s" % (stream["width"], stream["height"])
    frames = np.frombuffer(raw[:n * SIDE * SIDE], dtype=np.uint8).reshape(n, SIDE * SIDE).astype(np.int16)
    print("video %s %s-%s crop %s: fps %.3f, %d frames, one tick = %.2f frames" % (
        a.video, a.start, a.end, a.crop, fps, n, 0.6 * fps))
    step = np.abs(frames[1:] - frames[:-1]).mean(axis=1)
    order = [int(i) for i in np.argsort(-step)[:24] if step[i] > a.thresh / 2]
    print("STEPS (frame: change from the frame before): " +
          (", ".join("%d: %.1f" % (i + 1, step[i]) for i in sorted(order)) or "none above %.1f" % (a.thresh / 2)))
    if a.ref is not None:
        assert 0 <= a.ref < n
        far = np.abs(frames - frames[a.ref]).mean(axis=1) > a.thresh
        runs, begin = [], None
        for i in range(n):
            if far[i] and begin is None:
                begin = i
            if not far[i] and begin is not None:
                runs.append((begin, i - 1))
                begin = None
        if begin is not None:
            runs.append((begin, n - 1))
        print("RUNS differing from frame %d by more than %.1f: " % (a.ref, a.thresh) + (", ".join(
            "%d-%d (%d frames = %.2f ticks)" % (b, c, c - b + 1, (c - b + 1) / fps / 0.6) for b, c in runs[:20])
            or "none") + (" ... %d more" % (len(runs) - 20) if len(runs) > 20 else ""))
    if a.sheet:
        first, last = (int(x) for x in a.sheet.split(":"))
        assert 0 <= first <= last < n
        assert last - first < 48, "at most 48 frames on one sheet"
        rows = (last - first + 6) // 6
        out = os.path.join(base, "sheet_%d_%d.png" % (first, last))
        vf = "crop=%s,select=between(n\\,%d\\,%d),scale=200:-1,tile=6x%d:padding=4" % (a.crop, first, last, rows)
        subprocess.run(cut + ["-vf", vf, "-frames:v", "1", "-fps_mode", "vfr", "-y", out], check=True)
        print("sheet %s (frames %d..%d, 6 per row)" % (os.path.relpath(out, ROOT), first, last))


if __name__ == "__main__":
    sys.exit(main())
