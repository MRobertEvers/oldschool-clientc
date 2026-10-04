# LEDGER: guide transcripts (YouTube), fetched 2026-10-03

Host: youtube.com only, through `/Users/matthewevers/.local/bin/yt-dlp`, one download at a time, 20-30 s apart after HTTP 429 on the translated `en` track. No file in this part is copied from a repository.

| date | url | revision | file | for |
|---|---|---|---|---|
| 2026-10-03 | ytsearch8: "fortis colosseum guide osrs", "colosseum wave guide osrs", "sol heredit guide osrs", "colosseum manticore guide osrs", "colosseum modifiers guide osrs", "colosseum line of sight guide osrs", "colosseum shockwave colossus guide", "colosseum javelin thrower serpent shaman guide", "colosseum minotaur jaguar warrior guide", "colosseum tick counter osrs", "colosseum speedrun osrs", "colosseum reward glory cash out guide" | n/a (search results) | build/logs/ytsearch.txt (not in git) | choosing videos; 69 distinct ids, several unrelated (a Zelda and a Rome result) |
| 2026-10-03 | https://www.youtube.com/watch?v=<id> for each id in transcripts/README.md (26 videos with captions) | YouTube auto-captions `en-orig`, metadata in `<id>.info.json` | docs/minigames/colosseum/sources/transcripts/yt_<id>.md (converted by tools/waves_gate/vtt_to_md.py --game "Fortis Colosseum"); raw .vtt and .info.json under build/corpus_tmp/yt/ (not in git) | machine transcripts, lowest evidence rank |
| 2026-10-03 | derived | n/a | transcripts/README.md, transcripts/STATED.md | the video table with frame-count ranges, and the narrators' numbers |

Five of the chosen videos have no English auto-captions (hOMVTeauUHg, Si1RqVxUvI8, _PzEVzfdQG8, -CA4em2msA4, t1qDqa_SXpk) and two more produced no caption file (6WKX_cxgOQg, lgJieqwWgYg): listed in transcripts/README.md. The `[mm:ss]` ranges in the README are keyword-density pointers, not statements.

Every quote in STATED.md was machine-checked to occur verbatim in the transcripts (112 quotes, 0 misses; a quote with "..." or "[...]" was checked in pieces).

## What this part states (numbers), file transcripts/STATED.md

All grade D, narrator claims, nothing watched. STATED.md line numbers:

- Reinforcements spawn 40 s into the wave (five guides), 45 s (one): lines 7-12, disagreement 118.
- Wave starts five ticks (3 s) after pressing continue; stand on tile 1 / B by then: line 13.
- Javelin special every five attacks (two guides), every four (one); lands about five ticks later; 15-tile range for non-shaman enemies; max hit 48: lines 29-35, disagreement 117.
- Serpent shaman: magic only, 10-tile range (four guides): line 46. Shockwave colossus: max hit 56, 125 HP, at most one per wave: lines 64-67.
- Heals: a monster below 75% HP healed to full by the Minotaur, within six tiles with line of sight (Sun fish M7sIf6mx4vw [15:04]): line 47.
- Jaguar warrior melee every five ticks, a multi-hit attack (Noodles hCYGP0teGEU [01:06]): lines 15 and 39.
- Manticore: three orbs, the last always melee; one tick per orb, then a seven-tick recharge (Sun fish): lines 53-54. Two Manticores alternate every five ticks: line 57. The dragged ("dstacked") mob attacks three ticks after the front mobs: line 59. Manti Mayhem doubles each orb (ItsBrianOSRS 52aMbMJ5cZw [06:13]).
- Sol Heredit: 1500 HP (Sun fish [54:00]); regular attacks give two ticks to react; phases at 90%, 75%, 50%, 25%, 10%; lasers give two ticks under 10%; parry combo three ticks per hit and one tick slower on the third under 50%; equipment grab four ticks (five in one guide); sand one tick after a phase: lines 79-90, disagreements 119-120.
- Modifiers: Blasphemy level 1 starts at 20%; Frailty 3 cap 60 HP at 99; Relentless 33% to 100% defence bypass; Totemic heals from 50% HP; Volatility level 1 one tile: lines 93-98.
- Safe-tick odds quoted: 80% (a single ranger with a shaman, Sun fish [38:14]; Minotaur, Wizzy YWg2PWHdyts [14:29]) and 50% (Pen Sir, Proudest Dad). These are guide authors' estimates.
- Thin or absent: the reward pool, cash-out, glory rules, pet and Minimus numbers; the wave table with spawn tiles (guides say only that tiles A and B exist and that a Jaguar Warrior arrives with every reinforcement wave); Sol Heredit attack animation ids.

## Caveat for the spec workers

Every sampled guide narrates its own run; several wave-by-wave stack solutions in the captions depend on the player's gear and on which modifiers they took. Use the README ranges to frame-count a section, and use STATED.md only to know what to look for.
