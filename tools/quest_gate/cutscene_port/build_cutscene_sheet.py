#!/usr/bin/env python3
"""Cutscene comparison sheet: the recording's frames above the port's frames, per cutscene.

    build_cutscene_sheet.py <out_dir> <test_id> [<test_id> ...] [--clips DIR] [--title T]

For each quest it reads docs/quests/cutscenes/PORTS.tsv (which ledger row and
clip belong to which cutscene), VIDEOS.tsv (the cutscene's range, wiki summary
and source), the quest's last run at build/quest_gate/<test_id>/ledger.tsv, and
the clip under --clips/<test_id>/ (default ~/Documents/osrs_cutscenes, the
extracted clips with their meta-*.txt sidecars).

Per cutscene it writes two strips:
  video: the clip's world view at the cutscene start, at each keyframe's time
         ((tick - first tick) * 0.6 s after the start), every 4 s, and one second
         before the end -- the clip carries 3 s of padding, so the cutscene
         starts at 3.0 s;
  game:  every shot on the rows that share the cutscene's step prefix
         (`tog.bowl.cutscene` collects `tog.bowl.page1`, `tog.bowl.glide`, ...).
Both pictures are the client's world picture: a fixed 765x503 canvas is cropped
to its viewport, a resizable canvas (the 1024x768 the tests run at for
cutscenes, and most recordings) is shown whole.
The page is index.html with WebP images beside it; publish the directory as an
artifact and record the link in PORTS.tsv's note or BATCHES.tsv.
"""
import argparse
import csv
import html
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import frames  # noqa: E402

from PIL import Image  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
CLIP_PAD = 3.0
TILE = (512, 334)

STYLE = """
<style>
/* Layout: one column of cutscene cards; inside each, two horizontal strips (recording, port) that scroll sideways. */
:root { --bg:#f3efe6; --card:#fbf9f4; --fg:#22201b; --muted:#6b655a; --line:#d9d2c3; --accent:#8a4b1e; --video:#3b5f8a; --game:#4d7a3a; --pass:#2e7d32; --fail:#b3261e;
  --display:"Source Serif 4", Georgia, serif; --body:"IBM Plex Sans", system-ui, sans-serif; --mono:"IBM Plex Mono", ui-monospace, monospace; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { --bg:#191817; --card:#22211f; --fg:#ece7dc; --muted:#a39c8e; --line:#3a3733; --accent:#e0a26b; --video:#8ab4e8; --game:#9ccf7f; --pass:#7bc67e; --fail:#f28b82; color-scheme:dark } }
:root[data-theme="dark"] { --bg:#191817; --card:#22211f; --fg:#ece7dc; --muted:#a39c8e; --line:#3a3733; --accent:#e0a26b; --video:#8ab4e8; --game:#9ccf7f; --pass:#7bc67e; --fail:#f28b82; color-scheme:dark }
body { background:var(--bg); color:var(--fg); font-family:var(--body); margin:0; padding-block:24px; padding-inline:16px; line-height:1.45 }
main { max-width:1200px; margin:0 auto; display:grid; gap:28px }
h1 { font-family:var(--display); font-weight:600; font-size:clamp(26px,4vw,38px); margin:0; text-wrap:balance }
h2 { font-family:var(--display); font-weight:600; font-size:22px; margin:0; text-wrap:balance }
.lead { color:var(--muted); max-width:70ch; margin:6px 0 0 }
.card { background:var(--card); border:1px solid var(--line); border-radius:6px; padding:18px; display:grid; gap:14px; min-width:0 }
.meta { display:flex; flex-wrap:wrap; gap:8px 18px; font-size:13px; color:var(--muted) }
.meta b { color:var(--fg); font-weight:600 }
.meta a { color:var(--accent) }
.pill { display:inline-block; padding:1px 8px; border-radius:999px; font-size:12px; font-weight:600; letter-spacing:.02em; text-transform:uppercase; border:1px solid currentColor }
.pill.pass { color:var(--pass) } .pill.fail { color:var(--fail) }
.strip { display:grid; gap:6px; min-width:0 }
.strip h3 { margin:0; font-size:12px; letter-spacing:.06em; text-transform:uppercase; font-weight:600 }
.strip.video h3 { color:var(--video) } .strip.game h3 { color:var(--game) }
.row { display:flex; gap:10px; overflow-x:auto; padding-bottom:6px }
figure { margin:0; flex:0 0 auto; width:min(384px, 80vw) }
figure img { width:100%; display:block; border-radius:3px; border:2px solid var(--line); background:#000; max-width:100% }
.strip.video figure img { border-color:var(--video) } .strip.game figure img { border-color:var(--game) }
figcaption { font-family:var(--mono); font-size:12px; color:var(--muted); margin-top:4px; white-space:normal; overflow-wrap:anywhere }
.summary { font-size:14px; max-width:80ch }
.keyframes { font-family:var(--mono); font-size:12px; color:var(--muted); overflow-x:auto; white-space:nowrap }
</style>
"""


def read_tsv(path):
    with open(path, encoding="utf-8") as fh:
        return list(csv.DictReader(fh, delimiter="\t"))


def ledger(test_id):
    path = os.path.join(REPO, "build", "quest_gate", test_id, "ledger.tsv")
    assert os.path.isfile(path), "no run for %s: %s" % (test_id, path)
    rows = []
    for line in open(path, encoding="utf-8").read().splitlines()[2:]:
        c = line.split("\t")
        if c[0] == "SUMMARY" or len(c) < 6:
            continue
        rows.append({"index": int(c[0]), "step": c[1], "verdict": c[2], "ticks": c[3], "shots": [s for s in c[4].split(",") if s], "detail": c[5]})
    return rows


def keyframes(detail):
    """[(tick, op, x, z, h)] from a `cutscene:` detail."""
    return [(int(m.group(1)), m.group(2), m.group(3)) for m in re.finditer(r"#\d+ t=(\d+) (moveto|lookat|shake|reset)([^|;]*)", detail)]


def game_shots(rows, await_step):
    """Shots on every row that shares the cutscene's prefix: `tog.bowl.cutscene` collects
    `tog.bowl.page1`, `tog.bowl.glide`, `tog.bowl.page2`, `tog.bowl.cutscene` in ledger order.
    Name a cutscene's rows that way in the test (PORTING.md, section 6)."""
    prefix = await_step.rsplit(".cutscene", 1)[0]
    shots = []
    for r in rows:
        if r["step"] == prefix or r["step"].startswith(prefix + ".") or r["step"].startswith(prefix + "-") or r["step"] == await_step:
            for s in r["shots"]:
                if not s.endswith("-FAIL"):
                    shots.append((s, r["step"]))
    return shots


def layout_of(img):
    """'fixed' for a 765x503 canvas (world in the top-left viewport), else 'resizable' (world fills the canvas)."""
    return "fixed" if img.size == (765, 503) else "resizable"


def picture(img, recording=False):
    """The world picture to compare: a fixed canvas is cropped to its viewport, a resizable one is the whole game area."""
    if recording:
        img = frames.game_area(img)
        # a recording of a fixed client shows the stone side panel right of the world; a resizable one shows world there
        w, h = img.size
        strip = img.crop((int(0.70 * w), int(0.36 * h), int(0.95 * w), int(0.44 * h))).convert("L")
        hist = strip.getextrema()
        return frames.world_view(img) if (hist[1] - hist[0]) < 40 else img
    return frames.world_view(img) if layout_of(img) == "fixed" else img


def webp(img, path, quality=80):
    img.save(path, "WEBP", quality=quality)


def build(out_dir, test_ids, clips_root, title):
    os.makedirs(out_dir, exist_ok=True)
    ports = [r for r in read_tsv(os.path.join(REPO, "docs/quests/cutscenes/PORTS.tsv")) if r["test_id"] in test_ids]
    videos = {(r["test_id"], r["index"]): r for r in read_tsv(os.path.join(REPO, "docs/quests/cutscenes/VIDEOS.tsv"))}
    assert ports, "no PORTS.tsv rows for " + ", ".join(test_ids)
    cards = []
    n = 0
    for p in ports:
        v = videos[(p["test_id"], p["index"])]
        rows = ledger(p["test_id"])
        await_row = next((r for r in rows if r["step"] == p["ledger_step"]), None)
        assert await_row, "ledger of %s has no row %s" % (p["test_id"], p["ledger_step"])
        kfs = keyframes(await_row["detail"])
        clip = os.path.join(clips_root, p["test_id"], p["clip"])
        assert os.path.isfile(clip), clip
        cut_len = float(v["length_s"])
        # video sample times, seconds into the clip
        times = [CLIP_PAD]
        if kfs:
            t0 = kfs[0][0]
            times += [CLIP_PAD + (t - t0) * 0.6 for t, op, _ in kfs if op != "reset"]
        t = CLIP_PAD + 4.0
        while t < CLIP_PAD + cut_len - 1.0:
            times.append(t)
            t += 4.0
        times.append(CLIP_PAD + max(0.0, cut_len - 1.0))
        times = sorted({round(t, 1) for t in times if t <= CLIP_PAD + cut_len + 0.5})
        vfigs = []
        for t in times:
            n += 1
            raw = os.path.join(out_dir, "v%03d.png" % n)
            frames.frame(clip, t, raw)
            im = picture(Image.open(raw).convert("RGB"), recording=True)
            im = im.resize((TILE[0], int(TILE[0] * im.size[1] / im.size[0])))
            os.remove(raw)
            name = "v%03d.webp" % n
            webp(im, os.path.join(out_dir, name))
            vfigs.append((name, "%.1f s (cutscene +%.1f s)" % (t, t - CLIP_PAD)))
        gfigs = []
        for shot, step in game_shots(rows, await_row["step"]):
            src = os.path.join(REPO, "build", "quest_gate", p["test_id"], "shots", shot + ".png")
            if not os.path.isfile(src):
                continue
            n += 1
            im = picture(Image.open(src).convert("RGB"))
            im = im.resize((TILE[0], int(TILE[0] * im.size[1] / im.size[0])))
            name = "g%03d.webp" % n
            webp(im, os.path.join(out_dir, name))
            gfigs.append((name, "%s (%s)" % (shot, step)))
        cards.append({"port": p, "video": v, "await": await_row, "kfs": kfs, "vfigs": vfigs, "gfigs": gfigs})
    # page
    parts = ["<title>%s</title>" % html.escape(title), STYLE,
             '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Source+Serif+4:wght@600&family=IBM+Plex+Sans:wght@400;600&family=IBM+Plex+Mono&display=swap">',
             "<main>", "<header><h1>%s</h1><p class=lead>Each cutscene twice: the recording it was ported from (blue) and the port photographed by its quest test (green). Both strips are the client's world view; captions give the time into the clip and the ledger shot.</p></header>" % html.escape(title)]
    for c in cards:
        p, v, a = c["port"], c["video"], c["await"]
        verdict = "pass" if a["verdict"] == "PASS" else "fail"
        parts.append('<section class=card id="%s-%s">' % (p["test_id"], p["index"]))
        parts.append("<h2>%s, cutscene %s</h2>" % (html.escape(v["quest"]), html.escape(p["index"])))
        parts.append('<div class=meta><span><span class="pill %s">%s</span></span><span>test row <b>%s</b></span><span>site <b>%s</b></span>'
                     '<span>source <a href="%s">%s</a> %s to %s (%s s, %s)</span><span>range confidence <b>%s</b></span></div>' % (
                         verdict, html.escape(a["verdict"]), html.escape(p["ledger_step"]), html.escape(p["rs2_site"]), html.escape(v["url"]), html.escape(v["channel"]),
                         html.escape(v["start"]), html.escape(v["end"]), html.escape(v["length_s"]), html.escape(v["client"]), html.escape(v["confidence"])))
        parts.append("<p class=summary>%s</p>" % html.escape(v["wiki_summary"]))
        parts.append("<div class=keyframes>%s</div>" % html.escape(" | ".join("#%d t=%d %s%s" % (i + 1, t, op, rest) for i, (t, op, rest) in enumerate(c["kfs"]))))
        for cls, label, figs in (("video", "Recording", c["vfigs"]), ("game", "Port (quest test shots)", c["gfigs"])):
            parts.append('<div class="strip %s"><h3>%s</h3><div class=row>' % (cls, label))
            for name, cap in figs:
                parts.append('<figure><img src="%s" alt="%s" loading="lazy"><figcaption>%s</figcaption></figure>' % (name, html.escape(cap), html.escape(cap)))
            parts.append("</div></div>")
        if p.get("note"):
            parts.append("<p class=summary>%s</p>" % html.escape(p["note"]))
        parts.append("</section>")
    parts.append("</main>")
    open(os.path.join(out_dir, "index.html"), "w", encoding="utf-8").write("\n".join(parts) + "\n")
    json.dump({"quests": test_ids, "cutscenes": [{"test_id": c["port"]["test_id"], "index": c["port"]["index"], "verdict": c["await"]["verdict"],
                                                  "video_frames": len(c["vfigs"]), "game_frames": len(c["gfigs"])} for c in cards]},
              open(os.path.join(out_dir, "sheet.json"), "w"), indent=1)
    print(os.path.join(out_dir, "index.html"), "cutscenes", len(cards), "images", n)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out_dir")
    ap.add_argument("test_ids", nargs="+")
    ap.add_argument("--clips", default=os.path.expanduser("~/Documents/osrs_cutscenes"))
    ap.add_argument("--title", default="Cutscene Ports")
    a = ap.parse_args()
    build(a.out_dir, a.test_ids, a.clips, a.title)
