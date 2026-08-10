# Audio accuracy — where every sound is specified, and what we do with it

The audio *engine* works (`AUDIO_SYSTEM_OPUS.md`, [[audio-system-retained]]):
effects, area sounds, music and jingles all reach the speaker from a real cache.
This document is about the layer above that — **which sound should play, where,
when, and how loud**.

It began as a findings register and is now also the record of the work those
findings produced. Every deviation §1–§3 describes has been fixed unless it is
explicitly marked **open**; §4 is what is left.

Sources, in order of authority:

1. the deobfuscated OldSchool 239 client (`Deobfuscator/src_osrs239_rl1_12_33`),
2. Kronos's bundled `runescape-client` (OldSchool **184**, RuneLite's exported
   names — `queueSoundEffect`, `addSequenceSoundEffect`, `calculateDelay` all
   readable; the effect queue and the sequence-sound path are unchanged between
   184 and 239, so this is the authority for §3),
3. `rt4-client` (RuneTek-4, readable names, same area-sound mechanism at an
   earlier rev),
4. measurements of `cache.osrs230` / `cache.osrs239` taken with the scratch
   tools listed in §5.

Every count below is measured, not estimated. Anything I could not confirm is
labelled **unconfirmed**.

---

## 0. The map: the sources of sound, and who owns each

| Source | Specified in | Reaches the client as | Status |
|---|---|---|---|
| Loc area sound | `LocType` opcodes 78/79/91/93/95 | cache, read at scene build | **done** — box distance, inner radius, both streams, live loc updates (§1) |
| NPC / player area sound | `NpcType` 134/140 (RS3), 148–152 (OSRS 239) | cache | **open**, and absent from both OSRS caches — nothing to validate against (§1.5) |
| Region ambience ("the bed") | config **group 15** soundscape record | `AMBIENTSOUND_START <flag><id>` | **done** — group 15 decoded, N loops + 8 timed sets (§1.6) |
| Region soundtrack | server-side region table; names/unlocks in **DBTable 44** | `MIDI_SONG <id>` | **done** for 433 map squares, from a join no cache carries (§2) |
| Combat / weapon | obj params, read by the **server** | `SYNTH_SOUND` | **done** — the chain was complete, the *timing* was not (§3.1, §3.2) |
| Any positional server sound | server script | `SOUND_AREA` (zone sub 14 / opcode 32) | **done** — was undecoded, and was truncating zone batches (§3.4) |
| Skilling / loc animation | `SeqType` frame sounds | cache, played on frame advance | **done** — radius and weighted alternatives (§3.3) |
| Spotanim | — | — | spotanims carry no sound in any era we decode |

The single most important structural fact, and the one that shaped §2: **the
client cache never says which music plays where.** It names tracks, describes
where they unlock in prose, and says which varp bit records the unlock — but the
coordinate → track mapping is server data.

---

## 1. Area / ambient sound

### 1.1 The reference algorithm

`rt4-client`'s `AreaSoundManager` is the readable form; osrs239's `class91` is
the authority where they differ. Both agree on the shape:

- Three independent lists of emitters: **locs, NPCs and players**. Locs are
  added when the scene is built and removed when the loc is removed; NPC and
  player emitters follow the entity.
- An emitter is a **box**, not a point: `[minX,maxX] × [minZ,maxZ]` in fine
  units (1 tile = 128), derived from the loc's `width`/`length` **after**
  orientation swap. A 4×1 waterfall is audible from anywhere along its length.
- Distance is the listener clamped against that box, the two axis distances
  **summed** (`Statics.method10796`, osrs239 `deob/Statics.java:15309`), then
  `max(d - 64, 0)` — i.e. Manhattan on the box with a half-tile dead zone.
- Volume, osrs239:
  `vol = master × curve((radius − d) / (radius − inner))`, clamped to `[0,1]`,
  and the voice is stopped outright when `d > radius`. `curve` is the opcode-91
  easing (0 = linear, 1 = `1−cos`, 2 = `sin`, …). rt4 is the same formula with
  `inner = 0` and no curve.
- **Two streams per emitter, both live at once.** The *primary* is
  `bgsound` looping forever (`setLoops(-1)`); the *secondary* fires one shot
  chosen uniformly at random from `bgsounds[]` every
  `min + rand()·(max − min)` ticks. A loc with both fields gets both.
- Going out of range **fades to zero over 150 ms**; it does not hard-stop.
- A multiloc re-resolves its sound whenever varps change (`updateMulti()`), so a
  machine that is switched on starts humming without a scene rebuild.
- There is **no cap** on concurrent emitters.

### 1.2 What the cache actually carries

`LocType` opcode 78 is `u16 soundId, u8 radius, u8 inner` and opcode 79 is
`u16 minTicks, u16 maxTicks, u8 radius, u8 inner, u8 count, u16 ids[count]`.

> **Naming correction.** `RSCache_Dat2ConfigLoc.ambient_sound_retain` is the
> fourth field of opcode 78 / 79. In `class91` it is used as the **inner
> full-volume radius** (`field6573` → `field1296`), not as a retain/linger time:
> volume is flat inside it and eases to zero between it and `radius`. The field
> is decoded correctly; only the name and the use are wrong.

Measured (`soundscan`, §5):

| | osrs230 | osrs239 |
|---|---|---|
| loc records | 56,376 | 62,194 |
| continuous ambient (op 78) | 1,365 | 1,506 |
| random set (op 79) | 155 | 183 |
| **both at once** | **30** | **33** |
| inner radius ≠ 0 | 27 | 54 |
| distance-fade curve (op 91) | 0 | 0 |
| non-default fade in/out (op 93) | — | 11 |
| non-default sound visibility (op 95) | — | 134 |
| max radius | 31 tiles | 31 tiles |

Radius distribution is dominated by 3 (766 locs) and 5 (374); 20-tile emitters
(105) are the region-scale ones.

### 1.3 What we do — and the seven deviations that are now closed

`world_builder_add_loc_area_sound` ([world_scenery.u.c](src/engine/world_builder/world_scenery.u.c))
records the footprint's **south-west tile plus its size**, the loc id, the
continuous sound *and* the random set, the tick range, and both radii.
`tick_area_sounds` ([rs_audio.c](src/game/rs_audio.c)) drives up to
`RS_AUDIO_MAX_AREA_VOICES` (12) of them.

What was wrong, and what it is now:

1. **Both streams, not one.** `RS_AudioAreaVoice` used a single `random_set`
   flag, which made a continuous sound and a random set mutually exclusive — so
   the 33 osrs239 locs (30 on osrs230) declaring both were the *busiest*
   emitters and heard the least. The voice now holds `primary_sound` and
   `set_ids` independently and runs both, as `AreaSoundManager` does with its
   `primaryStream`/`secondaryStream` pair.
2. **Distance to the box, not to a point.** `World_AreaSound` now carries
   `size_x`/`size_z` (orientation applied) and `box_distance_fine` clamps the
   listener against `[min,max]` on each axis. A four-tile waterfall no longer
   gets quieter towards its ends.
3. **One metric.** Acquisition and gain both use the reference's
   Manhattan-on-the-box minus a half-tile dead zone. The previous split
   (Manhattan to acquire, Chebyshev to attenuate) meant an emitter could be
   audible by one rule and never selected by the other.
4. **The invented quarter-volume floor is gone**, replaced by the thing it was
   standing in for: `falloff_volume` is flat out to `inner` and eases to zero at
   `radius`, which is what the fourth field of opcode 78/79 is for. 54 osrs239
   locs state one.
5. **Fades.** Start, stop and out-of-range all ramp over
   `RS_AUDIO_AREA_FADE_MS` (150, the reference's figure) instead of cutting.
6. **The random set re-rolls.** Both the sound and the gap are drawn from a
   per-emitter LCG seeded from `(x, z, loc_id)`, so two identical emitters do not
   fire in lockstep and a headless run is still reproducible. The previous code
   fixed the sound to `sound_ids[0]` for the emitter's whole life and cycled the
   gap off the global tick — one bird, on a metronome, for the whole forest.
7. **Emitters survive a rebuild.** A voice is re-bound to the matching emitter
   in the new generation by `(loc_id, level, x, z)` rather than stopped and
   restarted, so crossing a chunk boundary no longer restarts every waterfall.
   A multiloc that changed state stops only its primary stream, which is
   `AreaSound.update()`'s rule.

Two deviations remain, both deliberate:

- **The 12-voice cap** is ours; the reference has no cap. It is a first-come cap
  over the nearest emitters, so a nearer one appearing later does not displace a
  further one already playing. **Open**, and not obviously worth fixing: 12
  simultaneous emitters is already more than any scene in the two caches puts
  within earshot.
- **`sound_visibility` (op 95, 134 locs) is decoded and unused.** Semantics
  **unconfirmed** — `class596.field6577` is an enum `class91` itself never reads,
  so it is consumed by the emitter's owner rather than by the voice.

### 1.4 Loc changes move sound

`World_AddAreaSound` used to have exactly one call site — the scene builder — so
`LOC_ADD_CHANGE` and friends updated geometry and collision but never the
emitter list. A door that opened or a machine that was switched off kept
sounding until the next full rebuild.

`WorldBuilder_ApplyLocChange` now drops the emitter at the tile before it
removes the loc, and registers the replacement's emitter after the spawn, from
the *resolved* multiloc config — a lever's two states can name different sounds.
`World_RemoveAreaSoundAt` bumps `area_sound_generation`, which is what makes the
audio layer re-resolve its live voices.

### 1.5 NPC and player area sound: absent, and absent from the data too

`NpcType` opcode 134 (idle/crawl/walk/run sound + radius) and 140 (volume) are
decoded into `ToriRS_NpcType` and **have no consumer** — nothing in `src/`
outside `torirs_npctype_from_rscache.c` reads `sound_idle`/`sound_walk`/… The
reference picks between them by comparing the entity's current movement seq
against its `BasType` (`Npc.getSound`, rt4), re-evaluating when the movement
state changes.

Before implementing that, note the measurement: **zero** npc records in either
osrs230 or osrs239 set opcode 134 or 140. In osrs239 the npc ambient descriptor
moved to opcodes **148–152**, mirroring the loc one (sound id + radius + inner
radius at 148, distance curve at 149, fade in/out at 150, easing enum at 151,
random set at 152 — `deob/class405.java:644`). Those are also unused in the
shipped data: all 16,292 osrs239 and 14,205 osrs230 npc records decode
byte-exactly with our decoder, which stops on any unknown opcode, so none of
148–152 occurs.

> **Latent decode conflict, no live symptom.** `dat2_config_npc.c` decodes
> opcodes 150–154 as members-only action strings. In osrs239 that range is the
> ambient-sound fade config. Both readings are attested for *some* era; today
> neither cache exercises them, so nothing is broken — but a newer cache would
> mis-parse every byte after the first npc that used one, and it would look like
> a random field corruption, not a decode gap.

Player-emitted sound (`Player.getSound`, `player.soundRadius`) has no
counterpart at all in this tree.

### 1.6 The region bed is a config record, and now it is read as one

This was the largest single fidelity gap in the ambient system.

`AMBIENTSOUND_START` in osrs239 is `u8 flag, u16 id` and the client does
(`deob/client.java:3744`):

```java
boolean var624 = g1() == 1;
int     var625 = g2();
class410 var626 = Statics.method4309(var625);   // config archive, group 15, file id
field907.method10558(var626, var624, <setting>);
```

`class410` (group 15) is an **ambient soundscape**, not a sound effect:

| opcode | payload | meaning |
|---|---|---|
| 1 | `u8 n, u16 ids[n]` | continuous loops, all playing at once |
| 2 | `u16 min, u16 max, u8 n, u16 ids[n]` | a random set; `min`/`max` are **×20 = milliseconds** |
| 3 | `u8 curve, u16 dur×20` | fade in |
| 4 | `u8 curve, u16 dur×20` | fade out |

Up to **8** random sets per record (`field5221` is `new ArrayList(8)`, and
opcode 2 is skipped past `n > 48` or a full list). `class471` holds one stream
per continuous id and one per random set, each set with its own independent
next-play deadline. Weighting is by **repetition**: a set listing silence six
times and a drip three times drips a third of the time.

Measured: **group 15 exists in `cache.osrs239` with 8 records** (mean 58 bytes)
and is **absent from `cache.osrs230`** — the type is a 231–239 addition.

**Implemented.** `3rd/rscache/src/datatypes/dat2_config_soundscape.{c,h}` decodes
and re-encodes it (7 of 8 records byte-exact; the eighth carries opcode 1 twice
and the reference's decode discards the superseded list — rscache
`EXCEPTIONS.md` B22). `CreateTask_Dat2SoundscapeLoad` loads the group once at
boot into `RS_Soundscapes` and preloads the continuous clips.
`RS_Audio_SetAmbient` resolves the packet's id through that table and runs the
bed as N loops plus M independently-timed sets.

**The era gate is the empty table, and it is a supported reading rather than a
fallback.** A cache with no group 15 leaves `RS_Soundscapes.count == 0`, and the
bed then treats the id as a single looping sound effect — which is the only
thing revisions before 231 can have meant by it. `RSCACHE_DAT2_CONFIG_KIND_SOUNDSCAPE`
and `..._VARCLIENT_STRING` are both 15, distinguished by era, not by sniffing.

**mock230 now sends it** (`MOCK230_AMBIENT=<id>`, default soundscape 1), because
nothing did before and an unreachable subsystem is one nobody notices is broken.
A live run against the embed server:

```
soundscape load: 9 ids (8 records)
rs_audio: ambient soundscape 1 (fade 20ms)
rs_audio: ambient bed 1 -> 1 loops, 4 sets
rs_audio: ambient loop 11601 playing
rs_audio: ambient one-shot 2411,  next in 718 ticks
rs_audio: ambient one-shot 11593, next in 573 ticks
rs_audio: ambient one-shot 2411,  next in 264 ticks
```

— one drone under four sets firing on unrelated timers, which is the whole point
of the type.

## 2. Region soundtracks

### 2.1 How it actually works

Music is **server-driven**. The server watches which map square the player is
standing in, and on entry it (a) sets the track's unlock bit if unset and
announces "You have unlocked a new music track: …", and (b) if the player's
music preference is AUTO, sends `MIDI_SONG <archiveId>` and pushes the track name
into the music-player widget. The client just plays what it is told; it has no
region table.

### 2.1.1 The rev-239 transition envelope

`MIDI_SONG` in rev 239 is the fixed ten-byte `MIDI_SONG_V2` packet, not merely a
song id.  Its five `p2` values are, in wire order:

| wire field | reference scheduler argument | meaning |
|---|---|---|
| fade-in delay | `fadeInDelay` | client cycles before the incoming song starts |
| fade-out delay | `fadeOutDelay` | client cycles before the outgoing song starts fading |
| song id | song archive id | incoming track |
| fade-in speed | `fadeInSpeed` | incoming ramp duration in client cycles |
| fade-out speed | `fadeOutSpeed` | outgoing ramp duration in client cycles |

The supplied rev-239 deob's packet handler (`client.java`) passes those values
to `Statics.method10330` as `(fadeOutDelay, fadeOutSpeed, fadeInDelay,
fadeInSpeed)`.  Its scheduler first loads the incoming song and patches, then
starts two independent branches: outgoing delay → fade-out → removal, and
incoming delay → start at zero gain → fade-in.  The tasks run once per nominal
20 ms client cycle, so a speed of 30 is a 600 ms linear ramp.  With separate
players, the reference can therefore crossfade two songs when the delays
overlap.

There are two useful reference profiles in the cache scripts.  `script_9630`
uses the fallback `0/60/60/0`: fade the old song for 1.2 s, start the next song
after 1.2 s, and give it full gain immediately.  `script_9628`, used by
`script_9632` and `script_9633` with duration 30, explicitly requests
`0/30/0/30`: simultaneous 600 ms fade-out and fade-in.  The live map server is
outside this reference client, so its chosen packet profile cannot be inferred
from client code alone.

Region id is the map square: `regionId = ((x >> 6) << 8) | (z >> 6)`, so region
12854 = square (50, 54) = the tile block at (3200, 3456) — Varrock.

### 2.2 What the cache knows: DBTable 44

Modern OldSchool moved music metadata out of the enum pair older servers use
(`enum 812` = names, `enum 819` = packed unlock hash — **both absent from
cache.osrs239**) into **DBTable 44**, one DBROW per track, **876 rows** in
osrs239:

| column | type | contents | example (Sea Shanty) |
|---|---|---|---|
| 0 | string | display name | `Sea Shanty` |
| 1 | string | sort name (article moved to front) | `Sea Shanty` |
| 2 | string | unlock hint, appended to "This track unlocks …" | `at Musa Point.` |
| 3 | int | **unidentified** | `186` |
| 4 | type 11 | **js5 index-6 song archive id** | `92` |
| 5 | int,int | unlock **(varp slot, bit)** | `4, 10` |
| 6–14 | mixed | sparse: 7 (180 rows), 8 (49), 10 (18), 11–12 (type 74, 3 rows) | |

Column 4 is confirmed two ways: the index-6 reference table resolves the archive
*name* `"sea shanty"` to id 92, `"adventure"` to 177 and `"al kharid"` to 50, and
each matches column 4 of the corresponding row; and the OSRS Wiki lists Sea
Shanty's "Cache ID" as 92. Column 5 is the same `(varpPos, varpShift)` pair that
Kronos derives from `hash >> 14` / `hash & 0x3fff`.

Column 3 is **unidentified** — it is present on all 876 rows and its values
(186, 259, 480, 514, 527, 535 …) do not order chronologically the way column 4
does, so it is not a second song id. Not worth guessing.

Dumped to **[docs/audio/music_tracks_osrs239.tsv](audio/music_tracks_osrs239.tsv)**
(876 rows: name, sort name, unlock hint, col 3, song archive id, unlock varp+bit).

### 2.3 The region → track table

Nothing in any cache has one, so it is built from two sources and committed:

| file | what it gives | from |
|---|---|---|
| [docs/audio/music_tracks_osrs239.tsv](audio/music_tracks_osrs239.tsv) | 876 tracks: name, unlock-hint prose, song archive id, unlock (varp, bit) | DBTable 44 of `cache.osrs239` |
| [docs/audio/music_regions.tsv](audio/music_regions.tsv) | 440 region → track-name entries over 435 map squares | Kronos's `MusicPlayer.java` (OSRS 184) |

`tools/gen_music_regions.py` joins them into
`src/net/mock/mock230_music_regions.gen.h` — **433 map squares** with a song
archive id and an unlock bit each. The join matches names case-insensitively
against both the display and sort forms, because the two sources disagree on the
capitalisation of small words and on where the article goes; two names
(`Castlewars`, `Duel Arena`) still do not match and five squares are claimed by
two tracks upstream, all reported by the generator rather than silently dropped.

Spot-validated against the OSRS Wiki and the cache's own hint text:

| track | region | → tile | cache hint | wiki |
|---|---|---|---|---|
| Sea Shanty | 11569 | (2880, 3136) | *at Musa Point.* | Musa Point, Cache ID 92 ✓ |
| Adventure | 12854 | (3200, 3456) | *in Varrock.* | Varrock ✓ |
| Al Kharid | 13105 | (3264, 3136) | *in Al Kharid.* | Al Kharid ✓ |
| Harmony | 12850 | (3200, 3200) | *in Lumbridge.* | Lumbridge ✓ |
| Barbarianism | 12341 + 12441 | (3072, 3392), (3072, 9792) | *in Barbarian Village.* | village + its dungeon ✓ |

The dual-square Barbarianism entry is the shape to expect: an overworld square
plus the `+6400` underground square.

**Coverage is the honest limit.** Kronos maps 364 of its own 535 tracks; the
osrs239 cache has 876. So roughly half the modern track list has no square, and
the unmapped half of the world is silent rather than wrong. To extend it, in
order of usefulness: the wiki's `Map:Music_tracks` data page (not the rendered
map), its `/Classic` one-square-per-track variant, the cache's own 876 hint
strings as the checklist, and the 634-era coordinate dump on Rune-Server.

### 2.4 The server side

`mock230_music_enter_region` hangs off the **map-square** latch in
`mock230_world_update_map` — the same granularity music is keyed at, and next to
the `[mapzone]` trigger it shares the latch with. On entering a mapped square it
unlocks the track if the bit is clear, then plays it if the song differs from
what is already playing (`player->music_track`, so a track does not restart
every 64 tiles).

> **The unlock is a bit write, and that distinction is enforced.** Music unlock
> flags share varps with other varbits, so the first version — which used
> `mock230_world_set_varp` — wiped neighbouring bits. mock230's selftest counts
> whole-varp writes that land on a carrier varp and failed immediately. It now
> patches `varps[]` and calls `mock230_world_mark_varp`, which is the path the
> varbit writers take. See [[varp-two-writers-side-effects]].

`MIDI_SONG` / `_WITHSECONDARY` / `MIDI_SWAP` / `MIDI_JINGLE` / `MIDI_SONG_STOP`
were already parsed and played client-side; nothing there needed changing.

For mapped region changes mock230 now emits the reference-proven `0/30/0/30`
envelope and updates the music-tab label before the packet.  The local backend
has one generator synth/voice, unlike the reference's concurrently active
players: it fades the outgoing voice, hands the synth to the loaded incoming
song, then fades that voice in.  This gives an audible 600 ms fade-out and
600 ms fade-in, but it is a serialized approximation rather than a true
overlapping crossfade; a faithful overlap needs a second synth, asset and voice
with separate lifetime handling.  The generic `mock230_send_midi_song` keeps
the `script_9630` **wire** fallback for callers that did not request a
transition profile; the local common packet representation intentionally keeps
only the two ramp lengths.

## 3. Combat, weapon and loc sounds

### 3.1 Weapon and combat sound is server-driven — the chain was complete, the data was not

`docs/WEAPON_FX.md` §6 established the chain and it is now closed:
weapon obj params (`sound_stance1..4`, `equipment_sound`; 2,307 objs carry one)
→ server script `sound_synth` (246 call sites in `OSRS-Content/osrs239-content`)
→ `SS_OP_SOUND_SYNTH` ([mock230_scripts.c:7634](src/net/mock/mock230_scripts.c#L7634))
→ `SYNTH_SOUND` encoder ([mock230_encode.c:1734](src/net/mock/mock230_encode.c#L1734))
→ opcode 77 on osrs239 / 102 on osrs230 → `RS_Audio_Synth`.

The corollary matters for anyone hunting a missing swing sound: **attack
animations do not carry the sound.** Seq 422 (unarmed punch) has no frame
sounds, and neither do `slayer_abyssal_whip_attack` or `human_scythe_slash`. A
silent swing is a content/param gap, not a cache-decode gap — and per
[[weapon-fx-and-rsmod-reference]], a missing param falls through to a *default*
rather than erroring, so count the defaults, don't grep for failures.

> **And that is exactly what happened here.** Every link above worked and every
> weapon still sounded wrong, because `attack_sound_stanceN` declared
> `default=0`, sound effect 0 is a real clip, and no weapon in the tree stated
> the param — so all 1,083 of them swung with the same noise. Reported as "the
> bow of faerdhinen sounds like a regular bow"; it was not a bow sound, it was
> effect 0 for everything. Fixed in three parts (`docs/WEAPON_FX.md` §6.6): the
> sentinel is `-1` and guarded, `[proc,combat_attack_sound]` gained LostCity's
> damage-type fallback, and 831 weapons now state their real sound. The lesson
> is the one the memory already carried and this still evaded — a sentinel that
> collides with real data is not a sentinel, and "the pipeline is complete" says
> nothing about what is flowing through it.

> **Correction to an earlier note.** `SYNTH_SOUND` really is unpositioned, and
> that is the reference's behaviour, not a gap in ours. `Message.queueSoundEffect`
> sets `soundLocations[i] = 0`, and the drain reads location 0 as "use
> `soundEffectVolume`" — full volume, no pan. Nothing more to do here.

### 3.2 Effect timing — the bug behind "weapon sounds are mistimed"

This is the answer to "weapon sounds are not timed correctly", and both faults
are in `tick_effect_queue` ([rs_audio.c:441](src/game/rs_audio.c#L441)).

The reference queue is `HealthBarUpdate.method1769`
(`kronos-osrs-184/…/runescape-client/…/HealthBarUpdate.java:73`, readable names;
called once per **20 ms client cycle** from `Client.doCycle`, `Client.java:3104`).
Stripped of obfuscation it is:

```java
for (i = 0; i < soundEffectCount; i++) {
    queuedSoundEffectDelays[i]--;                      // 1. ALWAYS, first thing
    if (queuedSoundEffectDelays[i] >= -10) {           // 2. give up 10 cycles late
        if (soundEffects[i] == null) {
            soundEffects[i] = readSoundEffect(archive4, soundEffectIds[i], 0);
            if (soundEffects[i] == null) continue;     //    not resident yet
            queuedSoundEffectDelays[i] += soundEffects[i].calculateDelay();
        }
        if (queuedSoundEffectDelays[i] < 0) { play(i); delays[i] = -100; }
    } else { remove(i); }
}
```

Our cadence matches — `RS_Audio_Tick` runs from `app_logic_tick`, `APP_LOGIC_TICK_MS = 20`
([app.c:66](src/app.c#L66), [app.c:5647](src/app.c#L5647)) — and
`calculateDelay()` matches too: it is the smallest instrument offset in ms
divided by 20, subtracted from the clip and handed back as cycles, which is
exactly `RSCache_SoundEffectTrim` → `ToriDraw_Sound.queue_delay`
([torirs_sound_from_rscache.c:22](src/engine/torirs_sound_from_rscache.c#L22)).
So the arithmetic is right. The scheduling is not:

1. **The countdown is frozen while the clip loads.** The reference decrements
   *before* it looks at residency; we decrement only after `publish_sound`
   succeeds ([rs_audio.c:452-487](src/game/rs_audio.c#L452-L487)), so every 20 ms
   the clip spends loading is 20 ms of lateness added to the sound. The in-code
   comment ("Not counting down while waiting keeps the server's delay
   meaningful") is the opposite of what the reference does. **This is the bug you
   are hearing**: a warm clip is on time, a cold one is late, so the *first*
   swing of a weapon, the first cast of a spell, the first hit from a new npc all
   land behind the animation and everything after them is fine — which reads as
   "sometimes late" rather than "always late".

2. **There is no lateness cap.** The reference's `>= -10` discards an effect that
   is more than 10 cycles (200 ms) past its due time — better silent than wrong.
   We wait `RS_AUDIO_LOAD_WAIT_TICKS = 100` ([rs_audio.h:62](src/game/rs_audio.h#L62))
   — two seconds — and then play it *whenever* it turns up. Combined with (1),
   a slow first load does not get skipped; it fires up to 2 s late, on top of the
   next attack. The two faults compound: (1) creates the lateness, (2) refuses to
   throw it away.

Two smaller deviations in the same function:

3. **`loops == 0` should not queue at all.** `queueSoundEffect` requires
   `var1 != 0`; `queue_effect` ([rs_audio.c:83](src/game/rs_audio.c#L83)) accepts
   it and plays the clip once.
4. `entry->waited` counts *residency* attempts, not lateness, so the give-up
   condition is unrelated to when the sound was supposed to happen.

**Fixed.** `tick_effect_queue` now decrements unconditionally at the top of the
loop, discards the entry once `delay < -RS_AUDIO_LATE_LIMIT_TICKS` (10, the
reference's window), and `RS_AUDIO_LOAD_WAIT_TICKS` is gone — the −10 window
replaces it. `queue_effect` also refuses `loops == 0` outright, as
`Message.queueSoundEffect` does.

The behaviour change worth stating: a cold clip whose load takes longer than ten
ticks is now **silent rather than late**. That is the reference's bargain (its
archive reads are synchronous and in-memory, so it never hits the window), and
the next request for the same effect finds it resident and plays on time. A
swing sound that lands on the *next* swing is worse than no swing sound.

> **Why the tests never caught it, and what now does.** `rs_audio_test.c` asserts
> an effect plays on pass `delay + trim + 1`, which is the reference's schedule —
> and it stays true under the fix, because for a **resident** clip the
> unconditional decrement and the deferred one give the same answer. The whole
> defect lives in the not-yet-resident case, and every other test in the file
> publishes its clip before it queues.
>
> `test_late_load_timing` is the one that sees it: it queues an effect with a
> delay, then runs frames *without* draining the task runner so the clip cannot
> become resident, and asserts the effect fires on the single tick its clip
> finally arrives — not `delay` ticks later. Its second half queues an
> unresolvable id and asserts the entry is discarded on schedule and counted.

### 3.3 Frame sounds: radius, and one of several by weight

The cache's per-frame sounds *are* wired — `docs/WEAPON_FX.md` §6.5's claim that
"nothing plays them" is stale. `seq_copy_frame_sounds`
([task_dat2_sequence_load.c](src/engine/dat2/task_dat2_sequence_load.c)) copies
them into the animation and `app_play_frame_sounds`
([app.c](src/app.c)) fires on frame advance, positioned by the element's world
coordinates.

Measured:

| | osrs230 | osrs239 |
|---|---|---|
| seqs with frame sounds | 1,212 | 1,588 |
| sound entries | 2,963 | 4,329 |
| frames carrying **>1** sound | 9 (max 3) | **67 (max 6)** |
| entries with `weight ≥ 0` | 2,963 (all) | 4,329 (all) |
| entries with `retain ≠ 0` | 621 | 1,076 |
| entries with `location ≠ 0` | 2,840 | 3,906 |

> **`location` is the audible radius in tiles**, and the field is now named
> `radius` in `ToriDraw_AnimFrameSound` because that is what the client does with
> it. `class30.addSequenceSoundEffect` packs
> `soundLocations[i] = location | (tileZ << 8) | (tileX << 16)`, and the sound
> queue reads `(word & 255) * 128` back as a range in fine units, *discarding*
> the sound outside it rather than attenuating. It is not a position or a
> placement mode. A frame sound with `location == 0` has radius 0 and is audible
> essentially nowhere, which is what osrs239's 423 zero entries mean.

Two deviations closed:

1. **The radius survives the port.** `location`, `retain` and `weight` were all
   dropped at the copy, so every positional effect fell back to
   `AUDIO_DEFAULT_DISTANCE` (12 tiles) — a chop meant to carry three tiles
   carried twelve. `RS_Audio_SynthAt` now takes a radius (and an inner radius,
   which `SOUND_AREA` supplies and frame sounds do not).
2. **A frame's alternatives are picked by weight.** The map is sorted by frame
   index *with repeats*, so a frame's alternatives are a contiguous run;
   `app_play_frame_sounds` binary-searches to a member of that run, widens to the
   run's ends, and picks in proportion to the entries' weights. It used to
   `return` on the first hit, which made the choice a function of where the
   binary search happened to land.

Still open, both small:

- **`retain` is carried but unused.** Semantics **unconfirmed**.
- **`sounds_cross_world_view` (seq opcode 19) is decoded and unused** — the flag
  that says a sound is audible beyond the normal cull.
- Frame sounds fire when `anim_seq_id != old_seq_id || anim_frame != old_frame`,
  which is correct for advance but also fires on a *re-application* that changes
  the frame — worth checking against [[chathead-anim-reapply-reset]], the same
  class of bug in the UI path.

### 3.4 `SOUND_AREA`, which was silent *and* was eating zone batches

The server's *positional* sound packet — the one that makes a door three squares
away sound like it is three squares away — was not decoded:

```
src/net/rev/osrs239/zoneprot.h   OSRS239_ZONE_SOUND_AREA = 14
src/net/rev/osrs239/packetin.h   {  32, 7, PKT_NAME_NONE, "SOUND_AREA" }
```

Two consequences, and the second was not an audio bug at all:

- Every server-driven positional sound was silent.
- Ordinal 14 inside an `UPDATE_ZONE_PARTIAL_ENCLOSED` batch had no case in
  `osrs239_zone_name`, so `osrs239_read_zone_sub` failed and the enclosed loop
  **`break`ed**, discarding every remaining sub-packet in that batch — loc
  changes, obj spawns, projectiles. One area sound from a live server silently
  dropped the rest of that zone update, logging only
  `osrs239: ZONE_ENCLOSED unknown sub-ordinal 14`.

**Fixed.** The generated codec for it already existed (`3rd/rsprot/packets/sound_area.c`);
what was missing was the ordinal case, the payload struct and the route. The
rev-239 layout is richer than the 2004-era one and splits the two radii the way
loc ambient sounds do:

```
p1Alt3 coordInZone   p1Alt2 dropOffRange (inner)   p1 range (radius)
p1Alt2 delay         p1Alt1 loops                  p2 id
```

It goes through the same effect queue as `SYNTH_SOUND` (§3.2), positioned and
radius-limited, via `App_PlaySoundAt`.

`test_sound_area_does_not_truncate_a_zone_batch` in `midi_packet_test.c` is the
regression: a SOUND_AREA followed by a MAP_ANIM in one enclosed batch, asserting
the **MAP_ANIM** arrives. Deleting the ordinal case fails it, which is the check
that it can fail.

This stays latent against `mock230`, which never sends it: `[proc,sound_area]`
in the content tree fans out plain `sound_synth` to every player found by
`huntall` — the LostCity 254-era emulation. So against the mock an area sound
still arrives unpositioned and plays centred. **Open**, and content-side.

### 3.5 Loc interaction sounds

There is no "sound when you use this loc" field in `LocType` in any era we
decode. Door creaks, lever pulls and furnace roars are all server-sent —
`SYNTH_SOUND` for the actor, `SOUND_AREA` (§3.4) for everyone else nearby. Only
the *ambient* loc sound (§1) is cache-side.

---

## 4. What is left

Everything ranked in the original work order is done. What remains, in the order
it is worth doing:

1. **§3.4 — mock230 should send `SOUND_AREA`.** The client decodes it; the mock
   still emulates area sound the LostCity way by fanning out unpositioned
   `sound_synth`. Content-side work, and the only reason the new decode path is
   not exercised end-to-end against the embed server.
2. **§2.3 — extend the region table.** 433 of ~876 tracks have a square. The
   cache's own 876 unlock-hint strings are the checklist.
3. **§1.5 — npc and player movement sounds.** No cache in this tree carries the
   data (zero records use npc opcode 134/140, and osrs239's 148–152 are unused
   too), so this cannot be validated by playing it. Last for that reason, not
   because it is hard.
4. **§1.3 — the 12-voice cap**, and **§3.3 — `retain`** and
   `sounds_cross_world_view`. Small, and none of them is currently audible.
5. **§1.3 — `sound_visibility` (op 95)**, whose semantics are still unconfirmed.

## 5. Reproducing the measurements

The numbers above come from four scratch tools built against
`3rd/rscache/tools/build/*.o`. They are not in-tree; rebuild with:

```sh
make -C 3rd/rscache/tools find_named          # builds the shared objects
cc -std=c11 -O2 -w -I3rd/rscache/include -I3rd/rscache/src -I3rd/rscache \
   -I3rd/bmp -I3rd/bzip -I3rd/miniz -I3rd/xteas -I3rd/ini \
   -I3rd/rscache/tools/common -o /tmp/soundscan soundscan.c \
   3rd/rscache/tools/build/common_*.o 3rd/rscache/tools/build/rscache_unity.o \
   3rd/rscache/tools/build/{bzip,bzip_encode,miniz,bmp,xteas,ini}.o -lm -lpthread
```

| tool | question it answers |
|---|---|
| `soundscan --rev R <cache>` | loc/npc/seq sound-field population, and whether npcs decode exactly |
| `grpprobe --rev R [--table T] --group G --dump K <cache>` | does this config group exist, how many records, what do the bytes look like |
| `dbdump --rev R [--group 38\|39] (--file ID \| --sweep TABLE [--tsv]) <cache>` | DBROW/DBTABLE contents; `--sweep 44 --tsv` regenerates the music track table |
| `namefind --rev R --table 6 --name "sea shanty" <cache>` | js5 archive name → id, via the reference table's name hashes |

Sources kept under the session scratchpad; copy them into
`3rd/rscache/tools/` if any of them earns a second run.

The data they produced is committed, and one of them has a permanent generator:

| committed | produced by |
|---|---|
| `docs/audio/music_tracks_osrs239.tsv` | `dbdump --sweep 44 --tsv` |
| `docs/audio/music_regions.tsv` | a parse of Kronos's `MusicPlayer.java` |
| `src/net/mock/mock230_music_regions.gen.h` | **`tools/gen_music_regions.py`** — rerun it when either TSV changes |

Tests that hold this work:

| target | what it would catch |
|---|---|
| `make -C src test-sound` | the effect queue's schedule, including `test_late_load_timing` — the one that sees a clip arriving after its due time |
| `make -C src test-audio` | the generator-backed music voice receives real outgoing and incoming `VOICE_UPDATE` ramps rather than the retired stream-volume command |
| `make -C src test-midi-packets` | the V2 MIDI field transforms and the SOUND_AREA layout, including that a SOUND_AREA does not truncate the zone batch after it |
| `MOCK230_REV=osrs239 ./src/build/mock230 --selftest` | among much else, the carrier-varp rule, music-tab label ordering, and the region's `0/30/0/30` V2 envelope |
| `3rd/rscache/build/test_soundscape <root>` | the group-15 format, byte-exact over the whole cache |

Two traps worth repeating from [[audio-harness-and-measurement-traps]]:

- **A field being non-zero is not the same as the opcode being present.** The
  first run of `soundscan` reported opcode 93 on all 62,194 locs; the defaults
  are `300`/`300`/`2`, so it was measuring the default. Always compare against
  the type's declared defaults.
- **A sweep that stops early still prints a total.** The first table-44 sweep
  reported 742 rows because it broke out on the first coord-bearing row; the
  real count is 876.

---

## 6. Related

- `AUDIO_SYSTEM_OPUS.md` — the engine: mixer, synth, MIDI, soundbank, harnesses.
- `docs/WEAPON_FX.md` §6 — the weapon-sound chain (§6.5 is superseded by §3.3 here).
- `3rd/rscache/EXCEPTIONS.md` B22 — config group 15, and why it was filed as `varclient_string` for so long.
- `docs/RSPROT_OSRS239_PORT.md` — where the sound packets are declared.
- Memories: [[audio-system-retained]], [[sound-audio-session]],
  [[audio-harness-and-measurement-traps]], [[weapon-fx-and-rsmod-reference]].

### Web sources

- [Music — OSRS Wiki](https://oldschool.runescape.wiki/w/Music)
- [Map:Music tracks — OSRS Wiki](https://oldschool.runescape.wiki/w/Map:Music_tracks)
- [Map:Music tracks/Classic — OSRS Wiki](https://oldschool.runescape.wiki/w/Map:Music_tracks/Classic)
- [Sea Shanty — OSRS Wiki](https://oldschool.runescape.wiki/w/Sea_Shanty) (used to confirm Cache ID 92)
- [\[634\] All music track map coordinates — Rune-Server](https://rune-server.org/threads/634-all-music-track-map-coordinates.698625/)
- [Runescape Music Map](https://rsmusicmap.corymartin.net/)
