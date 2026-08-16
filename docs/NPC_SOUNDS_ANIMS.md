# NPC combat sounds — hunting a source for `attack_sound` / `defend_sound` / `death_sound`

Every monster in this game is silent. It swings without a noise, takes a hit
without a noise, and dies without a noise. The *call sites* exist — ported quest
scripts already read `npc_param(attack_sound)` — and the data does not: **zero**
npcs in `OSRS-Content/osrs239-content` state a sound param of any kind.

This document is the search for a source that does state them, kept as a log
rather than a conclusion, because most of the routes failed and the reasons they
failed are the useful part.

Companion to `docs/WEAPON_FX.md` §6 (the player half of the same problem, now
closed) and `docs/AUDIO_ACCURACY.md` §3.

---

## The bar a source has to clear

Not "does it have numbers". Three things, in this order:

1. **Names, not ids.** §4.1 rule 4 of the weapon-FX port: ids never cross trees.
   A source that states sound *ids* from some other revision has to be
   independently proven id-stable against this cache before any of it can be
   used — which is possible (it was done for rsmod's weapon sounds, see
   `WEAPON_FX.md` §6.7) but is per-source work, not a given.
2. **Resolvable here.** A name is only useful if it exists in
   `pack/4_soundeffects.pack`. That pack now carries Jagex's own config names
   (`gen_sound_names.py`), so this is a real test with a real pass mark rather
   than a formality.
3. **Attributable.** Where the source got it, so a wrong row can be traced
   rather than argued about.

---

## Route 0 — the cache itself *(failed, and worth knowing why)*

**Checked first because it would need no cross-tree reasoning at all.**

```
npc records in cache.osrs239           16,292
distinct npc param names in the cache      40
params with "sound" in the name          NONE
```

The cache's npc records carry combat bonuses, attack rate, and 40 param kinds —
and no sound. This matches what the client-side decode already said
(`AUDIO_ACCURACY.md` §1.5): npc opcodes 134/140 and 148–152, which *are* the
npc sound fields in their respective eras, are used by **zero** records in either
`cache.osrs230` or `cache.osrs239`.

So npc combat sound is not client data in this era at all. It is server data,
the same as the player's swing sound, and has to come from a server.

## Route 1 — LostCity *(succeeded, for 678 npcs)*

`~/Documents/git_repos/LostCity_Server/content/scripts/**/*.npc`:

| param | rows |
|---|---|
| `death_sound` | 556 |
| `defend_sound` | 554 |
| `attack_sound` | 536 |
| `rangeattack_sound` | 43 |

**678 distinct npcs**, referencing **234 distinct sound names**.

### Why this is trustworthy

The names resolve, and that is not a weak test — it is a collision between two
independent datasets that were built decades apart:

```
LostCity npc sound names            234
  resolve in our 239 sound pack     233
  do not resolve                      1   (the literal `null` sentinel)
```

LostCity's authors wrote those names from the 2004-era cache. The names in
`pack/4_soundeffects.pack` came from Jagex's own config names, published by the
OSRS Wiki after Jagex accidentally transmitted them in February 2025
(`WEAPON_FX.md` §6.7). Neither was derived from the other. They agree on 233 of
234 spellings — `2H_crush` → 2502, `air_elemental_attack` → 1527,
`babydragon_death`, `arrowlaunch2`, and so on.

That does two things at once: it validates LostCity's rows as usable here, and
it independently corroborates the wiki's leak as genuinely Jagex's naming.

### What it does not cover

LostCity stops at September 2004. 678 npcs out of this cache's 16,292 — every
monster added since is still silent. So this route is necessary and not
sufficient, which is what sent the search on to the routes below.

---
## Route 2 — rsmod *(succeeded, small, and it validated Route 1)*

`api/cache-enricher/.../npc/npcs.toml`: **53 npcs**, stating `attack_sound`,
`defend_sound`, `death_sound` as **raw ids**, not names. On its own that trips
the "ids never cross trees" rule — rsmod targets revision 231, this tree is 239.

So the two sources were used to check each other. 19 npcs are stated by both;
48 of their rows are directly comparable (same npc, same param, both sides
present). Resolving LostCity's *name* through `pack/4_soundeffects.pack` and
comparing to rsmod's *id*:

```
comparable rows                                       48
  agree (LostCity name -> the id rsmod states)        41
  disagree                                             7
```

**41 exact agreements across three independently-built datasets** — LostCity's
2004-era names, Jagex's 2025-leaked name table, and rsmod's rev-231 ids — is
strong evidence that rsmod's npc sound ids live in the same id space this cache
uses, which is what makes its 53 rows usable.

The 7 disagreements are worth reading, because none of them is corruption:

| npc | param | LostCity | rsmod |
|---|---|---|---|
| `goblin_armed` | attack | `goblin_attack` (469) | `goblin_spear_attack` (3520) |
| `spider` | attack/defend/death | `insect_*` (537/539/538) | `small_spider_*` (3604/3609/3608) |
| `unicorn` | defend | `horse_hit` (657) | `cow_hit` (371) |
| `weaponsmaster` | attack | `stabsword_slash` (2548) | `hacksword_slash` (2500) |
| `weaponsmaster` | defend | `blade3` (23) | `steel` (15) |

Five of the seven are rsmod being *more specific* than 2004 was — an armed
goblin got its own spear sound, a spider stopped using the generic insect one.
That is content drift between eras, not a bad row, and it means the two sources
disagree about what the game *should* sound like rather than about the data. On
a modern cache rsmod is the better answer where it has one.

## Route 3 — the OSRS Wiki *(failed, checked two ways)*

The wiki has an excellent sound *catalogue* and no npc mapping.

- `Template:Infobox monster` / `Bucket:Infobox monster` — 40-odd fields, none of
  them a sound. A spot check of `Goblin?action=raw` finds the word "sound" zero
  times.
- `Bucket:Sound effect` is `{name, id}` only. The wiki's own "full list of their
  respective files" link selects `id, name, page_name`, and `page_name` turns out
  to be the **audio file's** page (`File:Armadyl Eye sound.ogg`), not the
  creature's. Queried through `action=bucket` to be certain rather than reading
  the rendered table.

So the wiki can tell you what sound 3892 is called and let you listen to it. It
cannot tell you which monster plays it.

**Useful side-finding, for a different problem.** Two buckets exist that bear on
`AUDIO_ACCURACY.md` §2: `Bucket:Music` carries `title`, `unlock_hint` and
**`cacheid`**, and `Bucket:Music_map` carries `location_json` + `music_tracks`.
That is the region→track mapping this project reconstructed from Kronos at 433
squares, available from the wiki's own data layer. Worth taking when the music
table is next touched.

## Route 4 — the RuneScape (RS3) Wiki *(failed)*

`Template:Infobox Monster` on runescape.wiki mentions sound zero times, and a
full-text search for the obvious phrasings surfaces one unrelated page. RS3's
wiki documents the same things OSRS's does, and sound is not one of them.

## Route 5 — the other servers on this machine *(nothing new)*

| tree | npcs with sounds | distinct names |
|---|---|---|
| LostCity_Server | **678** | 234 |
| LostCity_Content2 | 553 | 176 |
| 2004scapeServer | 469 | 129 |
| RS2004Server | 464 | 123 |

All four are the same lineage and the later three are subsets. `2009scape`,
`rscsundae`, `OpenRune-Server`, `Kronos184-Client` and `OS1` state none at all.
LostCity_Server is the ceiling for this family.

## Route 6 — other-era caches *(failed for combat, but a real finding)*

If npc sound ever lived in a cache, an older cache would show it. Swept every
dat2 cache here for npc opcodes 134 (movement sounds) and 140 (volume):

| cache | npc records | op 134 | op 140 |
|---|---|---|---|
| `cache.rs643` | 13,636 | **318** | 18 |
| `cache.osrs184` | 9,306 | 0 | 0 |
| `cache.kronos` | 9,326 | 0 | 0 |
| `cache.osrs230` | 14,205 | 0 | 0 |
| `cache.osrs239` | 16,292 | 0 | 0 |

So npc sound *is* cache data — in RS2 at revision 643, where 318 npcs carry it.
But opcode 134 is the **movement** quartet (idle / crawl / walk / run), not
attack, defend or death. Right shape, wrong category: it would make a dragon's
footsteps, not its bite. OldSchool carries neither.

---

## Conclusion

**No public source states npc combat sounds for a modern OldSchool cache.** The
reason is structural rather than accidental: in this era the sound is chosen by
the *server* and sent as `SYNTH_SOUND`, so it is server data, and the only
parties who have it are servers. Jagex's is not public; the wikis catalogue
sounds without attributing them; and the caches stopped carrying npc sound after
the RS2 line.

What exists, and is verified usable:

| source | npcs | that exist in this cache | how it was validated |
|---|---|---|---|
| LostCity | 678 | 612 | 233 of its 234 sound names resolve in `pack/4_soundeffects.pack` |
| rsmod | 53 | 53 | 41 of 48 overlapping rows agree exactly with LostCity via that pack |
| **union** | **712** | **646** | — |

646 of 16,292 npcs — **4%**. Every 2004-era monster, and almost nothing since.

### What to do with that

1. **Port the 646.** Both sources are name-resolvable or id-validated, and the
   overlap rule should be rsmod-wins-where-it-differs: its five specific
   disagreements are all modernisations, and this tree targets 239.
2. **Do not synthesise the other 96%.** Guessing a monster's sound from its
   family would produce exactly the failure the player half of this problem just
   suffered — a plausible wrong answer that nothing reports (`WEAPON_FX.md`
   §6.6). Silence is the honest default and the sentinel already supports it.
3. **The remaining route not yet tried** is deriving it from the game itself:
   `SYNTH_SOUND` packets observed from a live server, keyed by the npc that was
   attacking. That is what rsmod's 53 rows almost certainly are, and it is the
   only route that scales to modern content. It needs a capture, not a lookup.

---

> **Built.** `tools/gen_npc_combat.py` implements the pipeline sketched below and
> `docs/DEATH_ATK_DEF_ANIMS.md` is its documentation, with the measured results.
> Two things changed once it was run against real data, and both are recorded
> there rather than here: the rig gate turned out to apply to *authored* rows too
> (457 of LostCity's 1,303 name a sequence the npc's model can no longer play),
> and grading the layers against one blended "truth" hid the fact that they
> disagree with LostCity mostly by being correct about a 239 cache.

# Automating it — the layered pipeline

*(added after the search above concluded; this is the design for identifying
attack / defend / death animations for every npc, and their sounds.)*

The conclusion above — "no public source states npc combat sounds" — turns out
to be only half the problem, and the easier half at that. Four measurements
change the picture:

```
seq records in this content tree, all carrying Jagex's 2025 names   14,413
  named *attack*  1,277     *death*  758     *block/defend/hit*  642
npcs anchored to a rig (readyanim= in the cache -> framemap)        14,053 of 16,292
seqs carrying their sound IN-BAND (sound=frame,id,loops,...)         1,588
  of which attack-named 283, death-named 135
LostCity combat-anim names that resolve against the 2025 seq names  240 of 263
```

And the client already plays frame sounds (`app_play_frame_sounds`,
`src/app.c`) with the weight-based selection. So the shape of the solution is:

> **Animations are cache-decidable** — the rig closes the candidate set, the
> leaked names classify within it. **Sounds ride the animations** — 1,588 seqs
> carry theirs in-band and the client plays them for free; the rest fall back
> to the 646 server rows found above, then silence.

## What already exists

- `out/osrs239_anims/` — the entity-viewer catalog: `npc_rigs.csv` (npc →
  framemap), `framemap_seqs.csv` (framemap → seqs, 13,470 rows),
  `npc_name_matches.csv` (scored npc↔seq name affinity, 314k rows).
- `tools/entity_viewer/gen_npc_anims.py` — rig gate ∧ name gate → suffix
  classify. Today: **3,185 npcs** given anims; skips 10,073 as "its name is not
  what its rig is for", 483 as "its own sequences name no action", 115 no
  distinctive word.

## Animation layers

Each layer fills only what earlier layers left empty; each row records which
layer produced it; every layer's precision is measured against held-out ground
truth before it is allowed to write.

- **A0 — authored truth.** LostCity's `attack_anim`/`defend_anim`/`death_anim`
  params: 550/557/564 rows, 240 of 263 distinct names resolving directly
  against the 2025 seq names (the same two-independent-datasets collision that
  validated the sounds). rsmod's toml likewise if it states anims. These rows
  are data, not inference — and they double as the validation set for every
  layer below.
- **A1 — rig ∧ name-share** (the existing generator, unchanged): 3,185 npcs.
- **A2 — rig-unique suffix.** Drop the name gate where the rig itself is
  unambiguous: if an npc's rig(s) offer exactly one `*_death` seq, that is its
  death — every npc on that rig dies that way, whatever it is called. Measured
  potential among the 7,093 npcs on ordinary (≤200-seq) rigs:

  | action | unique candidate | multiple | none |
  |---|---|---|---|
  | death | **2,487** | 3,219 | 1,387 |
  | attack | **1,455** | 4,242 | 1,396 |
  | defend | **2,161** | 2,824 | 2,108 |

  The name gate stays for mega-rigs (6,960 npcs sit on rigs with >200 seqs —
  the human pile, framemap 0's 3,905 seqs) — but those are mostly humans, for
  whom the `npc_default.npc` human set is already *correct*, and armed humans
  already get weapon-carried anims from the weapon-FX work.
- **A3 — tie-breaks for the multis.** Within a rig, several `*_attack` seqs are
  usually variants. Break ties in order: (1) name-affinity score from
  `npc_name_matches.csv` — the existing scorer, demoted from gate to
  tie-break; (2) shared name stem with the npc's own `readyanim`
  (`swarm_ready` → `swarm_attack`); (3) the unsuffixed base over numbered
  variants (`X_attack` over `X_attack2`); (4) still tied → review file, keep
  default.
- **A4 — kinematic classify** for rigs whose seqs name no action (~1,400–2,100
  npcs per action). Features per seq, all computable with existing machinery
  (`anim_compare`, poser-gl-c frame application): duration in server ticks,
  loop/replay mode, final-frame collapse (bbox height of last frame vs
  readyanim — death anims end prone), returns-to-start-pose (attack/defend do,
  death does not), and the seq's frame-sound *name* (a seq whose in-band sound
  resolves to `*_death` is a death anim whatever the seq is called). Classify
  only when signals agree; precision measured on A0 before it may write.
- **A5 — default + report.** Whatever survives keeps the human default and the
  report says so, per npc, with the reason. No family guessing.

## Sound layers

- **S0 — in-band frame sounds.** If the chosen seq carries `sound=`, done: the
  client plays it when the anim plays. No data to port, no server change beyond
  playing the right seq. 283 attack-named and 135 death-named seqs qualify
  today, and A2/A4 will pick more.
- **S1 — server params.** The validated LostCity ∪ rsmod union: 646 npcs
  (rsmod wins its 7 disagreements). Fills 2004-era rigs, whose seqs predate
  in-band sounds.
- **S2 — name join.** 1,254 sound names carry combat suffixes. Candidate: the
  sound whose name equals/prefixes the chosen seq's name (`swarm_attack` seq →
  `swarm_attack` sound). Ship only if its precision on the S1 overlap measures
  high; otherwise drop the layer.
- **S3 — silence.** The honest default, already supported by the −1 sentinel.

## Validation harness

Hold out A0 (the 240 resolvable LostCity names): run A1–A4 blind, compare
prediction to truth per layer, report precision/recall and the full
disagreement table (expect a handful of genuine era drifts, like rsmod's 7).
Same for S0/S2 against S1's overlap: where LostCity states the sound *and* the
chosen seq carries one in-band, do they agree? Only layers that clear the bar
write rows; every written row carries a provenance comment
(`// a2: rig 1392's only *_death`).

## Write-layer traps (already learned once)

- An npc gaining an Attack option needs `attackrate` and combat stats, or its
  pacing is wrong (`npc-anims-from-rig-catalog`).
- New param rows must respect npc.server membership and the pack/names
  two-layer namespace.
- The generator owns its output block; the 197 authored blocks always win.
