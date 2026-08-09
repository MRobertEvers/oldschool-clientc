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
