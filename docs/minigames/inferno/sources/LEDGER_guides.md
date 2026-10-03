# LEDGER: guide transcripts (part "guides")

Spec pass: waves loop, the Inferno. Worker: guides. Branch matthew-mbp-m4-waves-b1. Host: YouTube only, through `/Users/matthewevers/.local/bin/yt-dlp`, one request at a time with a 3 s gap (15 s gap on the retry batch after the host answered HTTP 429 to the `en` auto-sub track on part of the first batch; the `en-orig` track downloaded). Searches `ytsearch8:` listed below; every search and download was run on 2026-10-03. The yt-dlp warning "No supported JavaScript runtime" appeared on every call and did not stop extraction.

Revision of a video: YouTube serves no revision id; the identity of a transcript is the video id plus the upload date in the info json (`build/corpus_tmp/yt/<id>.info.json`).

## Fetches

| date | url | revision | file | for |
|---|---|---|---|---|
| 2026-10-03 | yt search "inferno guide osrs", "inferno waves guide tick", "inferno zuk guide", "inferno triple jad guide", "inferno blob flick guide", "inferno pillar safespot stack guide", "inferno nibblers pillar guide", "inferno wave 69 zuk tick counter", "inferno ranger mager wave guide", "inferno jad healers guide" (8 results each) | n/a | build/corpus_tmp/search1.txt | candidate list |
| 2026-10-03 | https://www.youtube.com/watch?v=HnUr5zcF4fU | upload 2019-03-31 | docs/minigames/inferno/sources/transcripts/yt_HnUr5zcF4fU.md | xzact part 1: spawns, bat, blob, melee, ranger, mager, stacks |
| 2026-10-03 | https://www.youtube.com/watch?v=uaoSaUT4SZc | upload 2019-06-04 | .../transcripts/yt_uaoSaUT4SZc.md | xzact part 2: Jad, triples, Zuk |
| 2026-10-03 | https://www.youtube.com/watch?v=qnw3kGlpiuQ | upload 2021-05-21 | .../transcripts/yt_qnw3kGlpiuQ.md | Gnomonkey: stats of every monster, Zuk, pet |
| 2026-10-03 | https://www.youtube.com/watch?v=6trKOSUr4EM | upload 2024-10-03 | .../transcripts/yt_6trKOSUr4EM.md | Gnomonkey 2024: pillars 255, dig, respawn |
| 2026-10-03 | https://www.youtube.com/watch?v=H-Iup_IFUVc | upload 2025-02-26 | .../transcripts/yt_H-Iup_IFUVc.md | Gnomonkey 2025: full run (not mined line by line) |
| 2026-10-03 | https://www.youtube.com/watch?v=bybdjfCgcG4 | upload 2026-03-02 | .../transcripts/yt_bybdjfCgcG4.md | Kamahkaze: ticks, blob, melee, Zuk |
| 2026-10-03 | https://www.youtube.com/watch?v=BbJCzcMcMbE | upload 2025-10-06 | .../transcripts/yt_BbJCzcMcMbE.md | Kaoz: entry, monsters, Jad, Zuk |
| 2026-10-03 | https://www.youtube.com/watch?v=oKVmC7Rb1BY | upload 2025-07-25 | .../transcripts/yt_oKVmC7Rb1BY.md | Rob: 1-tick alternate, melee safespots |
| 2026-10-03 | https://www.youtube.com/watch?v=7gb8lDjX3hQ | upload 2025-05-05 | .../transcripts/yt_7gb8lDjX3hQ.md | Rob: full run, Zuk |
| 2026-10-03 | https://www.youtube.com/watch?v=GVnFERtla0E | upload 2026-08-05 | .../transcripts/yt_GVnFERtla0E.md | Rob: mage-tank run (not mined line by line) |
| 2026-10-03 | https://www.youtube.com/watch?v=EMqBmTq6Rbo | upload 2024-04-07 | .../transcripts/yt_EMqBmTq6Rbo.md | VideoGameBot part 1: blob, flinch, melee |
| 2026-10-03 | https://www.youtube.com/watch?v=yT-YtEZ7tsU | upload 2024-04-11 | .../transcripts/yt_yT-YtEZ7tsU.md | VideoGameBot part 3: triples, Zuk |
| 2026-10-03 | https://www.youtube.com/watch?v=E08It1hMHeg | upload 2023-06-03 | .../transcripts/yt_E08It1hMHeg.md | aatykon: wave by wave |
| 2026-10-03 | https://www.youtube.com/watch?v=c47wPpvPVJs | upload 2021-03-17 | .../transcripts/yt_c47wPpvPVJs.md | aatykon: blob/melee dig reset |
| 2026-10-03 | https://www.youtube.com/watch?v=Q46vyoHfF4Q | upload 2023-01-05 | .../transcripts/yt_Q46vyoHfF4Q.md | Nairy: monsters, hp, max hit |
| 2026-10-03 | https://www.youtube.com/watch?v=r3s4rbTd4QU | upload 2026-05-13 | .../transcripts/yt_r3s4rbTd4QU.md | dearlola1: full run (not mined line by line) |
| 2026-10-03 | https://www.youtube.com/watch?v=-w3WGOZ_Yeo | upload 2025-08-30 | .../transcripts/yt_-w3WGOZ_Yeo.md | Reynold: mage-tank run (not mined line by line) |
| 2026-10-03 | https://www.youtube.com/watch?v=XMYWmidc6hA | upload 2025-04-04 | .../transcripts/yt_XMYWmidc6hA.md | triple Jad cadence |
| 2026-10-03 | https://www.youtube.com/watch?v=2D4Zrp5iN3Y | upload 2026-07-30 | .../transcripts/yt_2D4Zrp5iN3Y.md | RS Mina: blob cycle |
| 2026-10-03 | https://www.youtube.com/watch?v=nppNxCucrY0 | upload 2023-02-14 | .../transcripts/yt_nppNxCucrY0.md | flick vocabulary |
| 2026-10-03 | https://www.youtube.com/watch?v=uP3DU21K3xE | upload 2026-04-23 | .../transcripts/yt_uP3DU21K3xE.md | Zuk healers safe spot |
| 2026-10-03 | https://www.youtube.com/watch?v=UpOw3xB9Bjo, zTQdupqm-lM, a_0ze3AS7Ak | n/a | none: "There are no subtitles for the requested languages" | UNAVAILABLE |

The data files of this part: README.md (table, frame-count ranges), STATED.md (the quoted statements), the 21 `yt_<id>.md` transcripts, all in `docs/minigames/inferno/sources/transcripts/`. Raw `.vtt` and `.info.json` are in `build/corpus_tmp/yt/` (not in git).

Not mined line by line: H-Iup_IFUVc, GVnFERtla0E, r3s4rbTd4QU, -w3WGOZ_Yeo (the full-run videos, 1.5-2.5 hours each) were read only through keyword searches for nibblers, ticks, pillars, flicks and metronomes. A spec worker mining mid-run asides (wave-by-wave solves) should read them. The coverage of units by two players was met from the shorter guides.

## What this part states (numbers), with where

All in `docs/minigames/inferno/sources/transcripts/STATED.md` under the unit named; the quote and video id and [mm:ss] are in that file. Grade D each. Find a sentence with `grep -n '<id> \[<mm:ss>\]' STATED.md` or open the transcript at the timestamp.

- Waves: 69 total, in-between six-nibbler wave before each new monster, two extra after Jad (triples, Zuk) (qnw3kGlpiuQ [0:10:02]); nine spawn positions (HnUr5zcF4fU [0:01:40]); first blob after wave 4, melee after 9, ranger 18, mager 35, mager+ranger from 50, double mager 66 (qnw3kGlpiuQ); nibbler waves of 3, 6 on in-between waves, none in the last three waves (qnw3kGlpiuQ [0:11:31]).
- Nibblers: max 4; 10 hit points (Q46vyoHfF4Q [0:02:13]); defence 15; pillars 255 hit points each (6trKOSUr4EM [0:07:45]); manual cast, phantom heal at 8+ tiles with a 10 tile range (qnw3kGlpiuQ [0:22:23]).
- Bat: attack every three ticks; range four tiles; max 19; run drain 3 per hit; stat drain 1 (not if praying range, per the "pole") (qnw3kGlpiuQ [0:12:03]-[0:12:37]); defence 55.
- Blob: six-tick cycle, reads prayer at tick 1 and fires opposite style at tick 4 (three ticks after the read); max 29; splits three mini blobs max 18, hp 15 (Gnomonkey) or 20 (Nairy), defence 95; the guides' disagreement on a two-tick versus three-tick gap is in STATED.md.
- Melee: max 49; four-tick cycle; dig after 20 s (qnw3kGlpiuQ [0:14:47]) or 30 s / 50 ticks (bybdjfCgcG4 [0:18:36], 6trKOSUr4EM [0:12:43], BbJCzcMcMbE [0:13:17]); delay after dig "three or four ticks" (EMqBmTq6Rbo [0:59:28]).
- Ranger: four-tick cycle (2.4 s); max 46; defence 60; damage calculated when the animation starts.
- Mager: max 70 (qnw3kGlpiuQ) or 71 (Q46vyoHfF4Q); attack tick when the light under it flickers; resurrection takes eight ticks, brings back a killed monster at half hp, once per monster, not nibblers (qnw3kGlpiuQ [0:16:23]).
- Jad: eight-tick cycle; five healers at half hit points (single Jad); three each (triple Jad and Zuk's Jad); triples nine ticks, offset three; appears in the north-west corner.
- Zuk: 1200 hit points; at 600 the set timer pauses and 1:45 is added; at 480 a Jad spawns; at 240 four healers spawn and he enrages; attack every 10 ticks, 7 enraged; sets every 3:30 (a ranger and a mager); shield at three positions; heal to 800 if healers untagged (6trKOSUr4EM [0:50:37]).
- Pet: 1/100 completion, 1/50 on task, 1/100 with the cape sacrificed (qnw3kGlpiuQ [0:44:44]).
