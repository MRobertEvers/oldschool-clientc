#!/usr/bin/env python3
"""frame_count -- dump the frames of a video section so a tick can be counted
(docs/RAID_ORCHESTRATOR.md section 3.1: anchor on the attack animation's first
frame, count frames, not seconds; one game tick is 0.6 s).

    python3 tools/raid_gate/frame_count.py <url> <start> <end> [--out DIR] [--every N]

<start>/<end> are mm:ss or seconds. The whole video is downloaded once with yt-dlp
into build/frames/<video id>/full.mp4, the section cut locally with ffmpeg, and every
frame (or every Nth) is written as <out>/f%06d.png at the stream's own frame
rate. Prints: the video id, fps, the frame count, and the directory, so that
frame k is at start + k/fps seconds and a gap of g frames is g/fps/0.6 ticks.
Then Read the PNGs (the attack's first frame per the cache seq record) and write
the row to docs/minigames/<raid>/sources/videos.tsv:
    url  timestamp  fps  frames  ticks  mechanic_id  measured_by
"""
import argparse
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
YTDLP = os.path.expanduser("~/.local/bin/yt-dlp")


def seconds(text):
    if re.match(r"^\d+(\.\d+)?$", text):
        return float(text)
    parts = [float(p) for p in text.split(":")]
    total = 0.0
    for p in parts:
        total = total * 60 + p
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("url")
    ap.add_argument("start")
    ap.add_argument("end")
    ap.add_argument("--out", default=None)
    ap.add_argument("--every", type=int, default=1, help="keep every Nth frame")
    a = ap.parse_args()
    vid = re.search(r"(?:v=|youtu\.be/)([A-Za-z0-9_-]{11})", a.url)
    assert vid, "no YouTube id in %s" % a.url
    vid = vid.group(1)
    s, e = seconds(a.start), seconds(a.end)
    assert e > s
    base = os.path.join(ROOT, "build", "frames", vid)
    os.makedirs(base, exist_ok=True)
    # The whole video is fetched once with yt-dlp's own downloader (a section
    # download hands ffmpeg a googlevideo url that answers 403) and cut locally.
    full = os.path.join(base, "full.mp4")
    if not os.path.exists(full):
        # HLS (m3u8) variants download whole; the direct https streams answer
        # 403 part-way through (measured 2026-10-02, 15 % in, every client).
        cmd = [YTDLP, "--quiet", "--no-warnings", "--hls-prefer-native",
               "-f", "bv*[protocol*=m3u8][height<=1080]/bv*[protocol*=m3u8]/bv*[height<=1080]/best",
               "-o", full, "https://www.youtube.com/watch?v=%s" % vid]
        subprocess.run(cmd, check=True)
    clip = os.path.join(base, "%d-%d.mp4" % (int(s), int(e)))
    if not os.path.exists(clip):
        subprocess.run(["ffmpeg", "-v", "quiet", "-y", "-ss", "%.3f" % s, "-to", "%.3f" % e, "-i", full,
                        "-c:v", "libx264", "-preset", "ultrafast", "-an", clip], check=True)
    probe = subprocess.run(["ffprobe", "-v", "quiet", "-print_format", "json", "-show_streams", clip],
                           capture_output=True, text=True, check=True)
    stream = next(st for st in json.loads(probe.stdout)["streams"] if st["codec_type"] == "video")
    num, den = stream["r_frame_rate"].split("/")
    fps = float(num) / float(den)
    out = a.out or os.path.join(base, "%d-%d_frames" % (int(s), int(e)))
    os.makedirs(out, exist_ok=True)
    vf = "select=not(mod(n\\,%d))" % a.every if a.every > 1 else "copy"
    cmd = ["ffmpeg", "-v", "quiet", "-y", "-i", clip]
    if a.every > 1:
        cmd += ["-vf", vf, "-fps_mode", "vfr"]
    cmd += [os.path.join(out, "f%06d.png")]
    subprocess.run(cmd, check=True)
    count = len([f for f in os.listdir(out) if f.endswith(".png")])
    print("video %s section %s-%s fps %.3f frames %d every %d dir %s" % (
        vid, a.start, a.end, fps, count, a.every, os.path.relpath(out, ROOT)))
    print("one tick = %.2f frames; frame k is at %s + k/%.3f s" % (0.6 * fps, a.start, fps))


if __name__ == "__main__":
    sys.exit(main())
