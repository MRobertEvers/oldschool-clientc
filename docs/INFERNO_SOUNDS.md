# Inferno sound — the search, the sources, and what is still open

The Inferno was silent. Sixty-nine waves, thirteen monsters, a cutscene with a
three-axis camera shake, and nothing in the player's ears but the generic
weapon swing. This is the record of finding sounds for it: where each id came
from, which routes failed, and — the part that matters most a year from now —
which rows are **stated by a source** and which are **derived**, because those
are not the same kind of thing and the game cannot tell you which is which.

Companion to `docs/NPC_SOUNDS_ANIMS.md` (the search for npc combat sound in
general, which concluded that no public source has it for modern content),
`docs/DEATH_ATK_DEF_ANIMS.md` (the animation half, and the layer vocabulary this
borrows), `docs/AUDIO_ACCURACY.md` (which sound plays where) and
`AUDIO_SYSTEM_OPUS.md` (the engine underneath).

---

## 0. The finding, in one paragraph

**Jagex built the Inferno out of sounds that already existed, and did not pick
them by lineage.** Zuk's attack is a Fire Blast. Zuk being hit is a dragon. A
blob's magic attack is a mind-control spell from *The Slug Menace*. The
Inferno's own assets carry no audio of any kind — not one of its sequences has
an in-band frame sound, not one of its 36 wall/prison/safespot locs uses the
`soundid=` field that 1,506 other locs in this cache do, and
`pack/4_soundeffects.pack` — Jagex's own config names, 10,200 of them — contains
no name matching `zuk`, `jal`, `inferno`, `tzkal` or `hurkot`. So there is
nothing to decode; there is only a table to reconstruct, and the only public
source that states any of it is five rows of a wiki's Notes column.

---

## 1. What the tree already had, and why none of it applied

`docs/NPC_SOUNDS_ANIMS.md` closed with a union of two usable sources — LostCity
(678 npcs) and rsmod (53) — covering 646 of the cache's 16,292 npcs. Neither
covers a single Inferno monster:

| source | why not |
|---|---|
| LostCity | content stops at September 2004. The Inferno is June 2017. |
| rsmod | states the Inferno roster — **examine text only**. `grep -i sound` over its `npcs.toml` rows for `inferno_*` returns nothing. |
| the cache | npc records carry no sound field in this era at all (opcodes 134/140 and 148–152: zero records in `cache.osrs230` *and* `cache.osrs239`). |
| Kronos | implements the whole encounter — and plays exactly **one** sound in it (§3). |
| the OSRS Wiki's `Bucket:Sound_effect` | 429 rows, `{id, name, page_name}`; `page_name` is the audio file's page. Filtering all 429 for `jal|zuk|tzhaar|inferno|jad` returns two rows, both unrelated (a bolt enchant and a Dragon warhammer). |
| the wiki's File namespace | `action=query&list=allpages&apnamespace=6&apprefix=Jal` returns fifteen pages, every one a `.png`. No Inferno monster has an uploaded sound. |
| the monster pages | `TzKal-Zuk?action=raw` mentions sound zero times. So does `Jal-MejJak`. |

## 2. The one source that does state it — the wiki's Notes column

`docs/audio/osrs_wiki_sound_ids.wikitext` is a local copy of the OldSchool
Wiki's [[List of sound IDs]] (re-fetched during this work and **byte-identical**
to the committed copy, so the snapshot is current). Its third column is free
prose, and 52 of its 10,138 rows carry any. Five of those 52 name an Inferno
monster — quoted here exactly as the page has them:

| id | name | the note, verbatim |
|---|---|---|
| 155 | `fireblast_cast_and_fire` | "Also used for \[\[TzKal-Zuk\]\]'s area sound effect when Zuk attacks." |
| 156 | `fireblast_hit` | "Also used for Zuk healer (\[\[Jal-MejJak\]\]) explosion area sound effect" |
| 410 | `dragon_hit` | "Also used for \[\[TzTok-Jad\]\] and \[\[TzKal-Zuk\]\] when they're hit." |
| 600 | `magmaquiris_hit` | "Also used for inferno mage getting hit sound." |
| 3528 | `surok_mindcontrol_impact` | "A high pitched noise. Also used for: \[\[Demonic gorilla\]\] / \[\[Tortured gorilla\]\] magic attack, \[\[Jal-Ak\]\] magic attack, \[\[Jal-AkRek-Mej\]\] magic attack" |

These are observations of the live game by people who played it, which is a
weaker provenance than a decode and a far stronger one than an inference. They
are layer **w** below and they are the spine of everything else: every other row
in this work is anchored to one of them, to the cache, or is honestly labelled
as a guess.

The notes are also the evidence for §0's claim that lineage does not predict
these. `surok_mindcontrol_impact` is a *Slug Menace* spell; `dragon_hit` is a
dragon; both `fireblast_*` are the standard combat spell. Nothing about the
TzHaar or the Fight Caves points at any of them.

## 3. Kronos

`Kronos-master/kronos-server/src/main/java/io/ruin/model/activities/inferno/`
implements the whole encounter across `Inferno.java` and eleven monster classes.
A case-insensitive grep for `sound` over that directory returns **two lines**,
both in `JalTokJad.java`:

```java
// :142 (magic attack) and :161 (ranged attack), in the postDamage of each
hit.postDamage(t -> {
    t.graphics(157);
    t.privateSound(163);
});
```

163 is `firewave_hit`. `t` is the *target*, and `privateSound` is per-player, so
this is the noise of a Jad projectile landing on you — the audio partner of the
impact graphic beside it, which this port already spells as
`spotanim_pl(firewave_impact, …)`. Layer **k**.

Kronos is also this tree's source for the Inferno's animations and combat
levels, so its silence about the other twelve monsters is informative: an
implementation that cared enough to state 7566/7565/7562 for Zuk's three
animations states no sound for him.

## 4. The cache — negative results worth keeping

Each of these looked like it should work. Recording them stops the next person
re-running them.

**Sequences carry no frame sound.** 1,588 of this cache's sequences embed their
own audio (`sound=frame,id,loops,radius,retain,weight`) and the client already
plays it (`app_play_frame_sounds`). Every Inferno sequence — `zuk_*`, `jal*`,
`moving_safe_spot_*`, `lizard_cleric_*`, `dagannoth_water_creature_*`, and both
collapsing-wall sequences — has **none**. This is what the `s3` note on each
`npc_combat/i/inferno_*.combat` ledger has been saying all along ("`zuk_attack`
carries none").

**Locs carry a sound field, and the Inferno's do not use it.** `all.loc` has
`soundid=` / `sounddistance=` / `soundmintick=` / `soundmaxtick=` /
`soundrandom10..13` and **1,506 records populate it**. Of the 36 `inferno_*`
wall, prison and safespot locs, zero do. The only TzHaar locs that do are the
city's ambience — `tzhaar_sulphar_vent`, `tzhaar_cave_door_closed`,
`tzhaar_fightpit_door_*`, `tzhaar_city_passthrough` all at 2066, and
`tzhaar_forge` at 2208. So the falling seal is silent *in the cache*, which
means its noise is server-driven, which means it is ours to choose.

**Spotanims have no sound field at all**, so the healer's `tzhaar_heal` graphic
cannot bring its own — and the sequence it runs, `lizard_cleric_heal_spot`, is
one of the sequences with no frame sound.

**There is no unnamed-id hiding place.** The wiki's names cover ids 0–10200;
everything above is `synth_<id>` in the pack. If the Inferno had its own audio
it would be named, and it is not.

## 5. What the cache *does* give — the asset-family join

Two Inferno monsters do not use Inferno assets, and for those the cache answers
directly. This is the same join `docs/DEATH_ATK_DEF_ANIMS.md` calls `s0`: one
Jagex asset name appearing on both sides of the cache, not a resemblance.

**Yt-HurKot** — `readyanim=lizard_cleric_ready`, `walkanim=lizard_cleric_walk`,
and the sound bank carries `lizard_cleric_attack` 608, `lizard_cleric_death`
609, `lizard_cleric_hit` 610. Same creature, same name, both tables.

**Jal-MejJak** — `readyanim=dagannoth_water_creature_ready`, and the bank spells
the same creature with two g's. Four members line up, not one:

```
dagannoth_water_creature_attack       <->  dagganoth_attack       1615
dagannoth_water_creature_defend       <->  dagganoth_hit          1622
dagannoth_water_creature_death        <->  dagganoth_death        1621
dagannoth_water_creature_spine_travel <->  dagganoth_spines       1623
```

`docs/BOSS_ASSETS.md` independently puts Jal-MejJak on framemap 19, the
Waterbirth dagannoth rig, which is *why* its animations carry that name.

**The heal has no such answer.** The `lizard_cleric` family is attack / death /
hit only. The healer's spotanim sound is therefore a choice: **166 `heal`**, the
bank's one unqualified heal (165 is `doubleheal`, 167 is `selfheal`, and this is
neither). Layer **d**.

## 6. The Fight Caves lineage — layer `f`, and why it survives

The Inferno's roster succeeds the Fight Caves' roster one for one, and — this is
the useful part — the *Fight Caves* monsters use asset families that **are**
named sound families in this cache. Read straight off `configs/all.npc`:

| Fight Caves npc | `readyanim` family | sound family | Inferno successor |
|---|---|---|---|
| Tz-Kih (bat, 22) | `firebat_*` | 291 / 296 / 295 | Jal-MejRah |
| Tz-Kek (blob, 45) | `lavabeast_*` | 595 / 597 / 596 | Jal-Ak, Jal-AkRek-\* |
| Tok-Xil (ranger, 90) | `magmaquris_*` | `magmaquiris_*` 598 / 600 / 599 | Jal-Xil |
| Yt-MejKot (melee, 180) | `lizard_cleric_*` | 608 / 610 / 609 | Jal-ImKot |
| Yt-HurKot (healer, 108) | `lizard_cleric_*` | 608 / 610 / 609 | Yt-HurKot (unchanged) |
| Ket-Zek (mager, 360) | `igniferum_*` | **none** | Jal-Zek |
| TzTok-Jad (702) | `lordmagmus_*` | **none** | JalTok-Jad |

(Tok-Xil's animation family is spelled `magmaquris` and its sound family
`magmaquiris` — Jagex's own drift, the same kind as dagannoth/dagganoth.)

`f` is the weakest layer here and it deserves the scrutiny, so:

- **It is corroborated once.** Yt-HurKot is in both encounters unchanged, so `f`
  predicts its three ids independently of the §5 join — and predicts them
  correctly. That is one test and it passed.
- **It is contradicted nowhere.** The two monsters where a source could have
  disagreed with it are exactly the two whose Fight Caves ancestors have *no*
  sound family, so `f` returns nothing for them and the wiki fills the gap
  instead. There is no row where `f` and a source give different answers.
- **It cannot be trusted on that basis.** One corroboration is one
  corroboration. `f` rows are the first thing a packet capture should replace.

**The one genuine ambiguity.** The wiki's `magmaquiris_hit` note says "inferno
mage", and `magmaquiris` is Tok-Xil's family — the *ranger*. Either the sound is
shared between Jal-Xil and Jal-Zek (ordinary; the Notes column is full of "also
used for"), or the editor was loose and meant the ranger, in which case `f` is
right and the Jal-Zek row is the guess. Both readings put these ids on Inferno
monsters, so the disagreement is about *which* monster, not about whether. Both
blocks carry the triple and both say so.

## 7. The cutscene

The Zuk cutscene fades to black, rebuilds the instance, locks the camera, shakes
it on three axes, drops the prison seal over six ticks and fades back in — in
silence. Nothing states what it should sound like (§4: the collapse sequences
have no frame sound and the wall locs no `soundid=`), so this is layer `f`
reasoning applied to scenery: pick from the family the asset belongs to.

Ids **2039–2046** of `pack/4_soundeffects.pack` are one contiguous authored
block of cave ambience — `cave_collapse_1`, `cave_bats`, `cave_bubbling_loop_1`,
`cave_growl_1`, `cave_insects_1`, `cave_insects_2`, `cave_rumbling_1`,
`cave_rumbling_2` — written for a volcanic cave. Three moments, three members of
that one family, so the cutscene is scored rather than decorated:

| moment | sound | where |
|---|---|---|
| camera locks, three-axis shake, "TzKal-Zuk's prison is breaking down" | 2045 `cave_rumbling_1` | `[proc,inferno_cutscene_lock]` |
| the two flanks let go, "The rocks holding TzKal-Zuk break loose!" | 2039 `cave_collapse_1` | `[proc,inferno_seal_collapse]` |
| six ticks later, state3 rubble lands, "The seal crumbles." | 2294 `cavein` | `[ai_queue6,inferno_tzkalzuk_placeholder]` |
| a nibbler chews through a rocky support | 2039 `cave_collapse_1` | `[proc,inferno_pillar_explode]` |

Passed over, and why: `roof_collapse` 1384 / `roof_collapse2` 1387 (a *roof*, and
the Inferno's roof is a separate loc that is removed rather than dropped);
`wall_destroy` 1769 and `wall_crushed` 1181 (both from construction/trap
content, and percussive rather than geological); `tob_pillar_collapse` 3969
(literally a collapsing pillar, but a 2018 boss's sound in a 2017 encounter —
the wrong direction to borrow).

## 8. The table, as built

Layers: **w** wiki Notes column · **k** Kronos source line · **c** cache
asset-family join · **t** the other members of a triple a source places one
member of · **f** Fight Caves lineage · **d** a choice with no source.

| npc | attack | defend | death | layers |
|---|---|---|---|---|
| TzKal-Zuk | 155 `fireblast_cast_and_fire` | 410 `dragon_hit` | 409 `dragon_death` | w / w / t |
| JalTok-Jad | 408 `dragon_attack` | 410 `dragon_hit` | 409 `dragon_death` | t / w / t |
| Yt-HurKot | 608 `lizard_cleric_attack` | 610 `lizard_cleric_hit` | 609 `lizard_cleric_death` | c (and f) |
| Jal-MejJak | 1615 `dagganoth_attack` | 1622 `dagganoth_hit` | 1621 `dagganoth_death` | c |
| Jal-Zek | 598 `magmaquiris_attack` | 600 `magmaquiris_hit` | 599 `magmaquiris_death` | t / w / t |
| Jal-Xil | 598 `magmaquiris_attack` | 600 `magmaquiris_hit` | 599 `magmaquiris_death` | f |
| Jal-ImKot | 608 `lizard_cleric_attack` | 610 `lizard_cleric_hit` | 609 `lizard_cleric_death` | f |
| Jal-Ak | 595 `lavabeast_attack` (melee) | 597 `lavabeast_hit` | 596 `lavabeast_death` | f |
| Jal-AkRek-Mej | 3528 `surok_mindcontrol_impact` | 597 | 596 | w / f / f |
| Jal-AkRek-Ket, -Xil | 595 | 597 | 596 | f |
| Jal-MejRah | 291 `firebat_attack` | 296 `firebat_hit` | 295 `firebat_death` | f |
| **Jal-Nib, Jal-Nib-Rek** | — | — | — | **open** |
| Ancestral Glyph | — | — | — | see below |

Non-combat and impact sounds, which do not fit a three-slot param:

| event | sound | layer | where |
|---|---|---|---|
| Jal-Ak / Jal-AkRek-Mej magic (and Jal-Ak's ranged twin) | 3528 `surok_mindcontrol_impact` | w | `inferno_ai.rs2` |
| a JalTok-Jad projectile lands on the player | 163 `firewave_hit` | k | `inferno_jad.rs2` |
| a JalTok-Jad projectile lands on the glyph | 163 `firewave_hit` | k | `inferno_jad.rs2` |
| Zuk's shot lands on the player | 156 `fireblast_hit` | t | `inferno_zuk.rs2` |
| **Zuk's shot lands on the Ancestral Glyph** | 156 `fireblast_hit` | t | `inferno_zuk.rs2` |
| Jal-MejJak's lava ball lands | 156 `fireblast_hit` | w | `inferno_adds.rs2` |
| Yt-HurKot heals (the `tzhaar_heal` spotanim) | 166 `heal` | d | `inferno_jad.rs2`, `inferno_zuk.rs2` |
| the cutscene | §7 | f | `inferno_zuk.rs2` |
| a rocky support collapses | 2039 `cave_collapse_1` | f | `inferno_pillars.rs2` |

**Zuk hitting the glyph** is `fireblast_hit` and not a `defend_sound` on the
glyph, deliberately. 156 is the impact half of the pair whose cast half the wiki
attributes to Zuk, and the wiki independently places 156 inside the Inferno on
another emitter. Putting it on the glyph's record instead would fire it every
time an add chewed on the glyph — which is every tick of the Zuk phase.

## 9. Where the code is, and the one thing that is easy to get wrong

An npc makes three combat noises and this engine plays them from **two
different sides**:

- **flinch and death are engine-side.** `mock230_combat.c` reads `block_sound`
  and `death_sound` off the record and broadcasts them itself
  (`npc_sound_nearby`, Chebyshev radius 12), beside the animations it plays for
  the same two events. Setting the params is enough.
- **the swing is content-side**, because the swing itself is:
  `skill_combat/combat.rs2`'s `[ai_opplayer2,_]` states `npc_anim` and
  `sound_synth` together.

**Every Inferno monster overrides that hook**, with its own
`[ai_applayer2,<npc>]` / `[ai_opplayer2,<npc>]` / `[ai_timer,<npc>]`. So
`attack_sound` alone reaches nothing here, and the calls sit beside each
`npc_anim` in `inferno_ai.rs2`, `inferno_adds.rs2`, `inferno_jad.rs2` and
`inferno_zuk.rs2`. That placement is also the only one that can tell a Jal-Ak's
melee swing from its magic cast — one param cannot hold two answers.

Two smaller traps:

- **loop count 1, never 0.** `RS_Audio_QueueEffect` refuses zero, matching the
  reference's `queueSoundEffect` requiring `var1 != 0`, because the count it
  hands the mixer is `loops - 1`. `~sound_area` and `~sound_within_distance` in
  `general/scripts/misc/sound.rs2` both pass 0 and would be silent; they have no
  live callers and were left as the port found them.
- **`sound_synth`'s delay is client cycles**, the same unit `~player_projectile`
  and `~npc_projectile` return — which is why the impact sounds pass the flight
  duration straight through while the damage queue beside them divides it by 30.

## 10. Music

The Inferno's track exists and was never reachable.

`docs/audio/music_tracks_osrs239.tsv` row 2811: display name **Inferno**,
js5 index-6 archive **500**, unlock varp 17 bit 13, and the game's own unlock
hint reads "in the Inferno." `docs/audio/music_track_names.tsv` independently
recovers archive 500's name from its reference-table hash as `inferno`.

Two things were missing.

**The square was not in the table.** `docs/audio/music_regions.tsv` comes from
Kronos's `MusicPlayer.java`, which has no Inferno row — because Kronos instances
the Inferno too, and its music player keys off the live map square, which an
instance never occupies. Added, with its provenance in the file (`load()` in
`tools/gen_music_regions.py` now strips `#` lines so a hand-added row can say
where it came from):

```
region id = (35 << 8) | 83 = 9043      ^inferno_template = 0_35_83_0_0
```

**And an instanced player's square describes nothing.** `mapinstance_scan_pool`
hands out squares from x >= 100, a band chosen precisely because the real map
does not reach it. So `mock230_music_enter_region` was being asked about a
square no music table has ever described — for the Inferno and for **every other
instanced encounter in the game**. The failure mode is why it went unnoticed:
not "wrong track", no track, and silence is indistinguishable from one of the
~65,000 squares that are silent on purpose.

`mock230_music_square_for` (`mock230_world.c`) now resolves an instanced
player's music through the square the instance was *copied from*, via a new
`mock230_mapinstance_source_tile`. The latch is unchanged — it still fires on the
destination square, so entering, leaving and being rebuilt into a different
instance all re-ask, and walking between two destination squares of one instance
re-asks and gets the same answer.

**And the room's music now goes out with the picture.** The cutscene fades to
black, shakes the camera on three axes and drops the prison seal — over a
four-minute loop that had not noticed. `midi_song(-1)` on the fade-out tick,
`midi_song(^inferno_music_track)` on the tick `cam_reset` brings the camera
home. The restore has to be named: `mock230_music_enter_region` only fires on a
map-square crossing and the player has not moved since the teleport, so nothing
would restart it until they left the Inferno.

Three engine pieces this needed, and the third is the one worth remembering:

1. `MIDI_SONG_STOP` had no encoder. It is a separate packet and not
   `midi_song(-1)` on the wire, because MIDI_SONG's id is two bytes with 65535
   as the sentinel and the 239 client turns that into -1 and then *starts*
   nothing — it never stops what is playing. `w239_midi_song_stop` is two
   `p2Alt3` fields, which is the writer side of the two `G2Le_add128` reads the
   client's own parse already does, so the pair is confirmed from both ends.
2. `SS_OP_MIDI_SONG` now routes a negative id to it — the same "a negative id is
   nothing, not entity -1" rule `SS_OP_SOUND_SYNTH` states a few cases above.
3. **`mock230_wire.c`'s `transcribed` allow-list.** A packet with a resolvable
   opcode *and* a registered writer is still refused unless its name is in
   `k_transcribed_osrs239`, and the refusal is a one-line stderr note nobody is
   reading. Symptom: `midi_song(-1)` reached the opcode (verified with a probe:
   the case ran eight times with a live player), called the encoder, and no
   packet appeared in the capture. Adding the writer is three edits, not two,
   and the third one has no compiler to catch it.

Covered by `mock230 --selftest`, "instanced music resolves the source square",
which asserts the *address* rather than the track id: what broke was the
address, and a track assertion would pass just as happily on a table row that
happened to sit at the instance's square. It also asserts the resolver is a
no-op outside an instance, and that the pool handed out a square that is not the
template's — without that check the test could pass while proving nothing.

## 11. Still open

- **Jal-Nib and Jal-Nib-Rek.** The one monster with no Fight Caves counterpart
  (there is no nibbler in the caves), no name join, and no mention in any
  source. Every layer returns nothing, so its record states nothing.
  `default=-1` in `npc_combat.param` is what silence is spelled as, and sound
  effect 0 is a real clip — so a guess here costs more than the gap.
  `mock230 --selftest` asserts it stays silent, which is what stops a later
  "fill in the gaps" pass from quietly converting an open slot into a guess.
- **The Ancestral Glyph's own death.** It shatters; nothing states with what.
- **Zuk's death.** 409 `dragon_death` is layer `t`, reached from the wiki's
  `dragon_hit` row. Plausible; unstated.
- **Every `f` row in §8.** One corroboration is not a proof.
- **The `npc_combat/i/inferno_*.combat` ledgers are stale in one respect.**
  They are `tools/gen_npc_combat.py`'s output and none of this work went
  through it — there is nothing for a generator to key on here, which is the
  whole point of §1. Their sound rows still read `-  // s3 ... carries none`,
  which remains *true about the sequence* and is now misleading about the game.
  The generator already has the fix and it is automatic: `write_ledger`'s
  `shadowed_by` branch stamps "NOT COMPILED ... an authored .npc block always
  wins" on any npc `inferno.npc` states, and blocks that previously stated only
  levels (`inferno_jad_healer`, `inferno_creature_harpie`, and the rest) now
  state sound. Re-running `tools/gen_npc_combat.py --write` stamps them. It was
  not run here because it rewrites all 6,460 ledgers and another session was
  working in this tree.
- **The route that would settle all of it** is the one
  `docs/NPC_SOUNDS_ANIMS.md` §"What to do with that" already named: observe
  `SYNTH_SOUND` from a live server, keyed to the npc that emitted it. `rsprox`
  decodes the packet. That is a capture, not a lookup, and it is the only route
  that scales to content newer than 2004.

---

## Files

| file | what changed |
|---|---|
| `.../minigame_inferno/configs/inferno.npc` | `attack_sound` / `defend_sound` / `death_sound` on eleven blocks, each with its layer and source |
| `.../minigame_inferno/scripts/inferno_ai.rs2` | attack sounds at nine sites; the placement rule in the header |
| `.../minigame_inferno/scripts/inferno_adds.rs2` | final-wave attacks, Jal-MejJak's barrage, the lava impact |
| `.../minigame_inferno/scripts/inferno_jad.rs2` | Jad's three attacks, the 163 impact, the Yt-HurKot heal |
| `.../minigame_inferno/scripts/inferno_zuk.rs2` | Zuk's roar, both shot impacts, the final-wave healer, the cutscene |
| `.../minigame_inferno/scripts/inferno_pillars.rs2` | the rocky-support collapse |
| `docs/audio/music_regions.tsv` | region 9043 -> Inferno, with provenance |
| `tools/gen_music_regions.py` | `load()` strips `#` lines so the data file can carry it |
| `src/net/mock/mock230_music_regions.gen.h` | regenerated: 434 squares |
| `src/net/mock/mock230_mapinstance.{c,h}` | `mock230_mapinstance_source_tile` |
| `src/net/mock/mock230_world.c` | `mock230_music_square_for`; two selftests |
