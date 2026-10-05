#!/usr/bin/env python3
"""vtt_to_md -- turn a yt-dlp auto-caption .vtt into a readable transcript markdown.

    python3 tools/waves_gate/vtt_to_md.py <video.vtt> <video.info.json> <out.md> [--game "the Inferno"]

`--raid NAME` is the same flag under its raid-loop name (this file was copied from
tools/raid_gate/vtt_to_md.py at 94f55b306) and keeps working; with neither flag the
header says "wave minigame".

YouTube auto-captions repeat each line across rolling cues (and carry inline
<c> / <00:00:01.000> word-timing tags). This strips the tags, drops every line
that repeats the previous cue's tail, joins the rest into paragraphs of roughly
30 s each, and prefixes each paragraph with its [H:MM:SS] start (a link back to
the video at that second). Chapters from the info json, if the uploader set any,
become the index. The header names the channel, title, upload date and length.

A machine transcript is the lowest-ranked evidence (docs/WAVES_ORCHESTRATOR.md
section 9; docs/RAID_ORCHESTRATOR.md section 2). Auto-captions mangle numbers and boss names; cross-check before use.
"""
import json, re, sys

def ts(s):
    h, m, rest = s.split(":")
    return int(h) * 3600 + int(m) * 60 + float(rest)

def fmt(sec):
    sec = int(sec)
    return f"{sec//3600}:{sec%3600//60:02d}:{sec%60:02d}"

def cues(path):
    out, cur = [], None
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        m = re.match(r"(\d+:\d\d:\d\d\.\d+) --> (\d+:\d\d:\d\d\.\d+)", line)
        if m:
            cur = [ts(m.group(1)), []]
            out.append(cur)
        elif cur is not None and line.strip() and not line.startswith(("WEBVTT", "Kind:", "Language:", "NOTE")):
            t = re.sub(r"<[^>]+>", "", line).strip()
            if t:
                cur[1].append(t)
    return out

def main():
    vtt, info, dst = sys.argv[1:4]
    if "--game" in sys.argv:
        raid = sys.argv[sys.argv.index("--game") + 1]
    elif "--raid" in sys.argv:
        raid = sys.argv[sys.argv.index("--raid") + 1]
    else:
        raid = "wave minigame"
    meta = json.load(open(info))
    vid = meta["id"]
    words, last = [], ""
    for start, lines in cues(vtt):
        for l in lines:
            if l == last:
                continue
            # rolling captions: a line that merely extends/repeats the previous one
            if last and l.startswith(last):
                l2 = l[len(last):].strip()
            else:
                l2 = l
            last = l
            if l2:
                words.append((start, l2))
    paras, cur, t0 = [], [], None
    for start, txt in words:
        if t0 is None:
            t0 = start
        cur.append(txt)
        if start - t0 >= 30:
            paras.append((t0, " ".join(cur))); cur, t0 = [], None
    if cur:
        paras.append((t0, " ".join(cur)))
    d = meta.get("upload_date") or ""
    d = f"{d[:4]}-{d[4:6]}-{d[6:]}" if len(d) == 8 else "n/a"
    link = lambda s: f"https://www.youtube.com/watch?v={vid}&t={int(s)}"
    o = [f"# {meta['title']} — transcript\n",
         f"Auto-generated captions from <https://www.youtube.com/watch?v={vid}>",
         f"({meta.get('channel') or meta.get('uploader')}, *{meta['title']}*, uploaded {d}, {meta.get('duration_string','?')}, {meta.get('fps','?')} fps source).\n",
         f"Downloaded with `yt-dlp --write-auto-subs` and converted by `tools/waves_gate/vtt_to_md.py` for the {raid} source corpus. "
         "Timestamps are `H:MM:SS` and link back to the video. Machine transcription: every tick count, npc name and item name must be cross-checked against the wiki, the cache or a recording before it is encoded as a constant.\n"]
    ch = meta.get("chapters") or []
    if ch:
        o.append("## Chapter index\n")
        for c in ch:
            o.append(f"- [{fmt(c['start_time'])}]({link(c['start_time'])}) — {c['title']}")
        o.append("")
    o.append("## Transcript\n")
    for t, p in paras:
        o.append(f"*[{fmt(t)}]({link(t)})* — {p}\n")
    open(dst, "w", encoding="utf-8").write("\n".join(o))
    print(dst, len(paras), "paragraphs")

main()
