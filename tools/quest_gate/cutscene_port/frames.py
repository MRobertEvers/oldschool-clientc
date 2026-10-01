#!/usr/bin/env python3
"""Frames out of a cutscene clip, and world-view crops, for the cutscene port tools.

No third-party module is required: frames come from `ffmpeg` on PATH, else the
imageio_ffmpeg binary, else cv2 -- whichever is installed. Compositing is PIL.

    frames.py <clip.mp4> <seconds> <out.png>        one frame
    frames.py <clip.mp4> --sheet <step> <out.png>   a labelled contact sheet (5 columns)

The fixed 765x503 client draws the world in a 512x334 viewport at (4,4); a
recording is that canvas pillarboxed inside 16:9. `world_view()` crops both to
the viewport so a video frame and a run shot compare like for like.
"""
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw

VIEW = (4 / 765, 4 / 503, 516 / 765, 338 / 503)


def _ffmpeg():
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError:
        return None


def frame(clip, seconds, out):
    """Write the frame at `seconds` of `clip` to `out` (PNG). Returns out."""
    assert os.path.isfile(clip), clip
    exe = _ffmpeg()
    if exe:
        r = subprocess.run([exe, "-y", "-loglevel", "error", "-ss", "%.3f" % seconds, "-i", clip, "-frames:v", "1", out],
                           capture_output=True, text=True)
        assert r.returncode == 0 and os.path.isfile(out), r.stderr[-300:]
        return out
    import cv2
    cap = cv2.VideoCapture(clip)
    cap.set(cv2.CAP_PROP_POS_MSEC, seconds * 1000.0)
    ok, f = cap.read()
    assert ok, "no frame at %.1f s of %s" % (seconds, clip)
    cv2.imwrite(out, f)
    return out


def duration(clip):
    exe = _ffmpeg()
    if exe:
        probe = exe.replace("ffmpeg", "ffprobe")
        if os.path.isfile(probe):
            r = subprocess.run([probe, "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", clip],
                               capture_output=True, text=True)
            if r.returncode == 0 and r.stdout.strip():
                return float(r.stdout.strip())
        r = subprocess.run([exe, "-i", clip], capture_output=True, text=True)
        import re
        m = re.search(r"Duration: (\d+):(\d+):(\d+\.\d+)", r.stderr)
        assert m, "no duration in ffmpeg output for " + clip
        return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3))
    import cv2
    cap = cv2.VideoCapture(clip)
    return cap.get(cv2.CAP_PROP_FRAME_COUNT) / cap.get(cv2.CAP_PROP_FPS)


def game_area(img):
    """Crop the 4:3 game canvas out of a pillarboxed or letterboxed frame."""
    g = img.convert("L")
    bbox = g.point(lambda v: 255 if v > 16 else 0).getbbox()
    return img.crop(bbox) if bbox else img


def world_view(img, recording=False):
    """The world viewport of a client canvas (a run shot), or of a recording."""
    if recording:
        img = game_area(img)
    w, h = img.size
    return img.crop((int(VIEW[0] * w), int(VIEW[1] * h), int(VIEW[2] * w), int(VIEW[3] * h)))


def label(img, text):
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, min(img.size[0], 7 * len(text) + 10), 16), fill=(0, 0, 0))
    d.text((4, 2), text, fill=(255, 230, 0))
    return img


def sheet(clip, step, out, cols=5, width=384):
    """A contact sheet of the clip every `step` seconds, timestamps burned in."""
    n = duration(clip)
    tiles = []
    t = 0.0
    tmp = out + ".frame.png"
    while t <= n:
        frame(clip, t, tmp)
        im = Image.open(tmp).convert("RGB")
        im = im.resize((width, int(width * im.size[1] / im.size[0])))
        tiles.append(label(im, "%.1fs" % t))
        t += step
    os.remove(tmp)
    assert tiles, "empty clip " + clip
    th = max(i.size[1] for i in tiles)
    rows = (len(tiles) + cols - 1) // cols
    page = Image.new("RGB", (cols * width, rows * th))
    for i, im in enumerate(tiles):
        page.paste(im, ((i % cols) * width, (i // cols) * th))
    page.save(out)
    return out


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 4 and a[1] == "--sheet":
        print(sheet(a[0], float(a[2]), a[3]))
    elif len(a) == 3:
        print(frame(a[0], float(a[1]), a[2]))
    else:
        print(__doc__)
        sys.exit(2)
