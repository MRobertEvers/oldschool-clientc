#!/usr/bin/env python3
"""Cutscene comparison sheet: the recording's frames above the port's frames, per cutscene.

    build_cutscene_sheet.py <out_dir> <test_id> [<test_id> ...] [--clips DIR] [--title T]

For each quest it reads docs/quests/cutscenes/PORTS.tsv (which ledger row and
clip belong to which cutscene), VIDEOS.tsv (the cutscene's range, wiki summary
and source), the quest's last run at build/quest_gate/<test_id>/ledger.tsv, and
the clip under --clips/<test_id>/ (default ~/Documents/osrs_cutscenes, the
extracted clips with their meta-*.txt sidecars).

Per cutscene it writes side-by-side pairs -- for every shot on the rows that
share the cutscene's step prefix (`tog.bowl.cutscene` collects `tog.bowl.page1`,
`tog.bowl.glide`, ...), the recording's frame at the same number of seconds
into the cutscene (from the ledger's per-row ticks x 0.6) on the left and the
port's shot on the right -- followed by the recording's own timeline (start,
every 4 s, the last second; the clip carries 3 s of padding, so the cutscene
starts at 3.0 s).
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
/* Layout: one column of cutscene cards; each holds paired rows (recording | port) that stay two-up at every width, then the recording's timeline as a scrolling strip. Tap a pair to see it large. */
:root { --bg:#f3efe6; --card:#fbf9f4; --fg:#22201b; --muted:#6b655a; --line:#d9d2c3; --accent:#8a4b1e; --video:#3b5f8a; --game:#4d7a3a; --pass:#2e7d32; --fail:#b3261e;
  --display:"Source Serif 4", Georgia, serif; --body:"IBM Plex Sans", system-ui, sans-serif; --mono:"IBM Plex Mono", ui-monospace, monospace; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { --bg:#191817; --card:#22211f; --fg:#ece7dc; --muted:#a39c8e; --line:#3a3733; --accent:#e0a26b; --video:#8ab4e8; --game:#9ccf7f; --pass:#7bc67e; --fail:#f28b82; color-scheme:dark } }
:root[data-theme="dark"] { --bg:#191817; --card:#22211f; --fg:#ece7dc; --muted:#a39c8e; --line:#3a3733; --accent:#e0a26b; --video:#8ab4e8; --game:#9ccf7f; --pass:#7bc67e; --fail:#f28b82; color-scheme:dark }
* { box-sizing:border-box }
body { background:var(--bg); color:var(--fg); font-family:var(--body); margin:0; padding-block:20px; padding-inline:16px; line-height:1.45; font-size:15px }
main { max-width:1100px; margin:0 auto; display:grid; gap:22px; min-width:0 }
h1 { font-family:var(--display); font-weight:600; font-size:clamp(24px,5vw,36px); margin:0; text-wrap:balance }
h2 { font-family:var(--display); font-weight:600; font-size:clamp(18px,4vw,22px); margin:0; text-wrap:balance; overflow-wrap:anywhere }
.lead { color:var(--muted); max-width:70ch; margin:6px 0 0; font-size:14px }
.card { background:var(--card); border:1px solid var(--line); border-radius:6px; padding:14px; display:grid; gap:12px; min-width:0 }
.meta { display:flex; flex-wrap:wrap; gap:6px 14px; font-size:12px; color:var(--muted); min-width:0 }
.meta > span { min-width:0; overflow-wrap:anywhere }
.meta b { color:var(--fg); font-weight:600 }
.meta a { color:var(--accent) }
.pill { display:inline-block; padding:1px 8px; border-radius:999px; font-size:11px; font-weight:600; letter-spacing:.02em; text-transform:uppercase; border:1px solid currentColor }
.pill.pass { color:var(--pass) } .pill.fail { color:var(--fail) }
.summary { font-size:14px; max-width:80ch; margin:0 }
.keyframes { font-family:var(--mono); font-size:11px; color:var(--muted); overflow-wrap:anywhere; line-height:1.6 }
.pairs { display:grid; gap:10px; min-width:0 }
.pairhead { display:grid; grid-template-columns:1fr 1fr; gap:8px; font-size:11px; letter-spacing:.06em; text-transform:uppercase; font-weight:600 }
.pairhead .video { color:var(--video) } .pairhead .game { color:var(--game) }
.pair { display:grid; grid-template-columns:1fr 1fr; gap:8px; min-width:0; cursor:zoom-in; border:0; padding:0; background:none; text-align:left; color:inherit; font:inherit; width:100% }
.pair:focus-visible { outline:2px solid var(--accent); outline-offset:3px; border-radius:4px }
figure { margin:0; min-width:0 }
figure img { width:100%; max-width:100%; display:block; border-radius:3px; border:2px solid var(--line); background:#000 }
figure.video img, .strip.video figure img { border-color:var(--video) } figure.game img { border-color:var(--game) }
figcaption { font-family:var(--mono); font-size:11px; color:var(--muted); margin-top:3px; overflow-wrap:anywhere }
.strip { display:grid; gap:6px; min-width:0 }
.strip h3 { margin:0; font-size:11px; letter-spacing:.06em; text-transform:uppercase; font-weight:600; color:var(--video) }
.row { display:flex; gap:8px; overflow-x:auto; padding-bottom:6px; -webkit-overflow-scrolling:touch }
.row figure { flex:0 0 auto; width:min(300px, 62vw) }
dialog { border:0; border-radius:8px; padding:0; max-width:min(96vw, 1000px); width:min(96vw, 1000px); background:var(--card); color:var(--fg) }
dialog::backdrop { background:rgba(0,0,0,.72) }
.zoom { display:grid; gap:10px; padding:12px }
.zoom .pair { cursor:default }
@media (max-width: 640px) { .zoom .pair { grid-template-columns:1fr } }
.zoom .bar { display:flex; justify-content:space-between; align-items:center; gap:10px; font-size:13px; color:var(--muted) }
.zoom button { font:inherit; padding:6px 14px; border-radius:6px; border:1px solid var(--line); background:var(--bg); color:var(--fg); cursor:pointer }
.zoom button:focus-visible { outline:2px solid var(--accent) }
@media (prefers-reduced-motion: no-preference) { .pair img { transition:transform .12s } .pair:hover img { transform:translateY(-1px) } }
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


def game_shots(rows, await_step, first_tick, reset_tick):
    """[(shot, step, offset_s)] for every row that shares the cutscene's prefix: `tog.bowl.cutscene`
    collects `tog.bowl.page1`, `tog.bowl.glide`, `tog.bowl.page2`, `tog.bowl.cutscene` in ledger order.
    offset_s is seconds from the first camera packet to the row's end, when its shots were taken: the
    ledger's `ticks` column summed from the run's start is on the same clock as the await detail's
    `t=` keyframe ticks (checked on both pilots, within a tick). Negative = before the cutscene;
    past the reset = after it. Name a cutscene's rows that way in the test (AUTHORING.md, section 6)."""
    prefix = await_step.rsplit(".cutscene", 1)[0]
    shots, cum = [], 0
    for r in rows:
        cum += int(r["ticks"] or 0)
        mine = r["step"] == prefix or r["step"].startswith(prefix + ".") or r["step"].startswith(prefix + "-") or r["step"] == await_step
        if mine:
            for sh in r["shots"]:
                if not sh.endswith("-FAIL"):
                    shots.append((sh, r["step"], (cum - first_tick) * 0.6))
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
        kf_ticks = [t for t, op, _ in kfs]
        first_tick = kf_ticks[0] if kf_ticks else 0
        reset_tick = next((t for t, op, _ in kfs if op == "reset"), kf_ticks[-1] if kf_ticks else 0)
        gshots = game_shots(rows, await_row["step"], first_tick, reset_tick)
        # the recording's own timeline: start, every 4 s, the last second
        times = [CLIP_PAD]
        t = CLIP_PAD + 4.0
        while t < CLIP_PAD + cut_len - 1.0:
            times.append(t)
            t += 4.0
        times.append(CLIP_PAD + max(0.0, cut_len - 1.0))
        vfigs = []
        for t in sorted({round(t, 1) for t in times}):
            n += 1
            raw = os.path.join(out_dir, "v%03d.png" % n)
            frames.frame(clip, t, raw)
            im = picture(Image.open(raw).convert("RGB"), recording=True)
            os.remove(raw)
            im = im.resize((TILE[0], int(TILE[0] * im.size[1] / im.size[0])))
            name = "v%03d.webp" % n
            webp(im, os.path.join(out_dir, name))
            vfigs.append((name, "+%.1f s" % (t - CLIP_PAD)))
        # paired rows: for each port shot, the recording at the same offset; shots before the first
        # packet pair with the clip's lead-in, shots after the reset with its tail
        pairs = []
        for shot, step, off in gshots:
            src = os.path.join(REPO, "build", "quest_gate", p["test_id"], "shots", shot + ".png")
            if not os.path.isfile(src):
                continue
            reset_off = (reset_tick - first_tick) * 0.6
            if off < 0:
                vt, when = CLIP_PAD - 1.5, "before the cutscene"
            elif off > reset_off + 0.3:
                vt, when = min(CLIP_PAD + cut_len + 1.5, frames.duration(clip) - 0.2), "after the cutscene"
            else:
                vt, when = CLIP_PAD + min(max(off, 0.3), cut_len - 0.3), "cutscene +%.1f s" % off
            n += 1
            raw = os.path.join(out_dir, "p%03d.png" % n)
            frames.frame(clip, vt, raw)
            vim = picture(Image.open(raw).convert("RGB"), recording=True)
            os.remove(raw)
            vim = vim.resize((TILE[0], int(TILE[0] * vim.size[1] / vim.size[0])))
            vname = "p%03d.webp" % n
            webp(vim, os.path.join(out_dir, vname))
            n += 1
            gim = picture(Image.open(src).convert("RGB"))
            gim = gim.resize((TILE[0], int(TILE[0] * gim.size[1] / gim.size[0])))
            gname = "g%03d.webp" % n
            webp(gim, os.path.join(out_dir, gname))
            pairs.append({"video": (vname, "recording, %s" % when), "game": (gname, "port, %s: %s" % (when, shot))})
        cards.append({"port": p, "video": v, "await": await_row, "kfs": kfs, "vfigs": vfigs, "pairs": pairs})
    # page
    parts = ["<title>%s</title>" % html.escape(title), STYLE,
             '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Source+Serif+4:wght@600&family=IBM+Plex+Sans:wght@400;600&family=IBM+Plex+Mono&display=swap">',
             "<main>", "<header><h1>%s</h1><p class=lead>Each cutscene side by side: on the left the recording it was ported from (blue), on the right the port as its quest test photographed it (green), paired at the same moment into the cutscene. Both are the client's full canvas in the same layout. The recording's own timeline follows each set of pairs. The test clicks through dialogue faster than a person reads, so the port's moments are earlier than the recording's; the pairing is by elapsed time, not by page.</p></header>" % html.escape(title)]
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
        parts.append('<div class="pairs"><div class="pairhead"><span class="video">Recording</span><span class="game">Port (quest test)</span></div>')
        for pr in c["pairs"]:
            parts.append('<button type="button" class="pair" data-v="%s" data-g="%s" data-vc="%s" data-gc="%s" aria-label="Show this pair larger">'
                         '<figure class="video"><img src="%s" alt="%s" loading="lazy"><figcaption>%s</figcaption></figure>'
                         '<figure class="game"><img src="%s" alt="%s" loading="lazy"><figcaption>%s</figcaption></figure></button>' % (
                             pr["video"][0], pr["game"][0], html.escape(pr["video"][1], quote=True), html.escape(pr["game"][1], quote=True),
                             pr["video"][0], html.escape(pr["video"][1]), html.escape(pr["video"][1]),
                             pr["game"][0], html.escape(pr["game"][1]), html.escape(pr["game"][1])))
        parts.append("</div>")
        parts.append('<div class="strip video"><h3>Recording timeline</h3><div class=row>')
        for name, cap in c["vfigs"]:
            parts.append('<figure><img src="%s" alt="%s" loading="lazy"><figcaption>%s</figcaption></figure>' % (name, html.escape(cap), html.escape(cap)))
        parts.append("</div></div>")
        if p.get("note"):
            parts.append("<p class=summary>%s</p>" % html.escape(p["note"]))
        parts.append("</section>")
    parts.append("</main>")
    parts.append('''<dialog id="zoom"><div class="zoom"><div class="bar"><span id="zoom-title"></span><button type="button" id="zoom-close">Close</button></div>
<div class="pair"><figure class="video"><img id="zoom-v" alt=""><figcaption id="zoom-vc"></figcaption></figure><figure class="game"><img id="zoom-g" alt=""><figcaption id="zoom-gc"></figcaption></figure></div></div></dialog>
<script>
(function(){
  var dlg=document.getElementById('zoom'); if(!dlg||!dlg.showModal) return;
  document.querySelectorAll('button.pair').forEach(function(b){
    b.addEventListener('click',function(){
      document.getElementById('zoom-v').src=b.dataset.v; document.getElementById('zoom-g').src=b.dataset.g;
      document.getElementById('zoom-vc').textContent=b.dataset.vc; document.getElementById('zoom-gc').textContent=b.dataset.gc;
      var card=b.closest('.card'); document.getElementById('zoom-title').textContent=card?card.querySelector('h2').textContent:'';
      dlg.showModal();
    });
  });
  document.getElementById('zoom-close').addEventListener('click',function(){dlg.close();});
  dlg.addEventListener('click',function(e){ if(e.target===dlg) dlg.close(); });
})();
</script>''')
    open(os.path.join(out_dir, "index.html"), "w", encoding="utf-8").write("\n".join(parts) + "\n")
    json.dump({"quests": test_ids, "cutscenes": [{"test_id": c["port"]["test_id"], "index": c["port"]["index"], "verdict": c["await"]["verdict"],
                                                  "video_frames": len(c["vfigs"]), "pairs": len(c["pairs"])} for c in cards]},
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
