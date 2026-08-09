# Audio accuracy — where every sound is specified, and what we are dropping

The audio *engine* works (`AUDIO_SYSTEM_OPUS.md`, [[audio-system-retained]]):
effects, area sounds, music and jingles all reach the speaker from a real cache.
This document is about the layer above that — **which sound should play, where,
and how loud** — and it is written from three sources, in this order of
authority:

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

## 0. The map: four sources of sound, and who owns each

| Source | Specified in | Reaches the client as | Our status |
|---|---|---|---|
| Loc area sound | `LocType` opcodes 78/79/91/93/95 | cache, read at scene build | plays; falloff and layering wrong (§1) |
| NPC / player area sound | `NpcType` opcodes 134/140 (RS3), 148–152 (OSRS 239) | cache | **not implemented**; absent from both OSRS caches (§1.5) |
| Region ambience ("the bed") | config **group 15** soundscape record | `AMBIENTSOUND_START <flag><id>` | **id is treated as a sound-effect id — it is a config id** (§1.6) |
| Region soundtrack | server-side region table; names/unlocks in **DBTable 44** | `MIDI_SONG <id>` | packet wired; **no region→track table exists in this tree** (§2) |
| Combat / weapon | obj params, read by the **server** | `SYNTH_SOUND` | wired end-to-end, but **mistimed** (§3.1, §3.2) |
| Any positional server sound | server script | `SOUND_AREA` (zone sub 14 / opcode 32) | **not decoded — and it truncates the zone batch** (§3.4) |
| Skilling / loc animation | `SeqType` frame sounds | cache, played on frame advance | plays; radius, weights and multi-sound frames dropped (§3.3) |
| Spotanim | — | — | spotanims carry no sound in any era we decode |

The single most important structural fact: **the client cache never says which
music plays where.** It names tracks, describes where they unlock in prose, and
says which varp bit records the unlock — but the coordinate → track mapping is
server data. §2 is about sourcing it.

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

### 1.3 What we do instead

`world_builder_add_loc_area_sound` ([world_scenery.u.c:1953](src/engine/world_builder/world_scenery.u.c#L1953))
records the footprint **centre tile**, the sound id *or* the set, the tick range
and the radius. `tick_area_sounds` ([rs_audio.c:530](src/game/rs_audio.c#L530))
then drives up to `RS_AUDIO_MAX_AREA_VOICES` (12) of them.

Ranked gaps:

1. **A loc with both a continuous sound and a random set only plays the
   continuous one.** `voice->random_set = source->sound_id < 0 && count > 0`
   ([rs_audio.c:606](src/game/rs_audio.c#L606)) makes the two mutually
   exclusive. 33 locs in osrs239, 30 in osrs230, and they are exactly the
   busiest emitters (a machine that hums *and* clanks).
2. **Distance is measured from the centre tile, not the footprint box.** A large
   loc is quietest where it is biggest. `World_AreaSound` does not carry
   `size_x`/`size_z` at all, so this cannot be fixed in the audio layer alone.
3. **Two different distance metrics.** Acquisition uses Manhattan
   ([rs_audio.c:582](src/game/rs_audio.c#L582)); `positional_gain` uses
   Chebyshev ([rs_audio.c:206](src/game/rs_audio.c#L206)). An emitter can be
   audible by one and never selected by the other. The reference uses Manhattan
   for both — but on the *box*, with the `−64` dead zone, which is what the
   Chebyshev change was really compensating for.
4. **The falloff has an invented floor.** `if (falloff < base/4) falloff = base/4`
   makes everything inside the radius at least a quarter volume and then cuts to
   silence at the edge — a step exactly where the reference is smoothest. The
   reference has no floor; it has an *inner radius* (54 locs use it) that does
   the job properly, plus an easing curve.
5. **Fades are not applied.** `sound_fade_in_curve/duration`,
   `sound_fade_out_curve/duration` (11 non-default locs) and the reference's
   unconditional 150 ms out-of-range ramp are all decoded and unused; we
   hard-stop.
6. **Random-set gaps are deterministic**, cycling `tick % (span+1)`
   ([rs_audio.c:668](src/game/rs_audio.c#L668)), and the *sound* is never
   re-picked — `voice->sound_id` is fixed to `sound_ids[0]` at acquisition. The
   reference re-rolls the id on every fire. This is the difference between a
   forest that sounds alive and one bird that repeats.
7. **Every voice is dropped on scene rebuild** (`area_generation` mismatch), so
   crossing a chunk boundary restarts every waterfall. The reference keys loc
   emitters by `(level, x, z, locType.id)` and removes only what actually left.
8. **12-voice cap** where the reference has none, and it is a first-come cap: a
   nearer emitter appearing later does not displace a further one already
   playing.
9. **Multilocs never re-resolve.** The sound is snapshotted at scene build.
10. **`sound_visibility` (op 95, 134 locs) is decoded and unused** — semantics
    **unconfirmed**; `class596.field6577` is an enum that `class91` itself never
    reads, so it is likely consumed by the emitter's owner rather than the voice.

### 1.4 Loc changes do not move sound

`World_AddAreaSound` has exactly one call site — the scene builder. `LOC_ADD_CHANGE`
and friends update geometry and collision but never the emitter list, so a door
that opens or a machine that is built is silent until the next full rebuild.

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

### 1.6 The region bed is a config record, and we treat it as a sound id

This is the largest single fidelity gap in the ambient system.

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
next-play deadline.

Measured: **group 15 exists in `cache.osrs239` with 8 records** (mean 58 bytes)
and is **absent from `cache.osrs230`** — the type is a 231–239 addition.
Record 1 decodes cleanly against that grammar:

```
02 015e 028a 04 2d46 2d47 2d48 2d49      set: every 7.0–13.0 s, one of 11590..11593
02 015e 0640 02 2d4f 096b                set: every 7.0–32.0 s, 11599 or 2411
02 00fa 03e8 05 2d4a 2d4b 096b 096b 096b set: every 5.0–20.0 s, weighted 2:3 toward 2411
02 00fa 0320 09 2d4c 2d4d 2d4e 096b ×6   set: every 5.0–16.0 s, weighted 3:6
```

Note the weighting idiom: repeating an id in the list biases the uniform pick.

We do none of this. `RS_Audio_SetAmbient` takes the packet's id straight to
`publish_sound` / `CreateTask_SoundLoad` ([rs_audio.c:776](src/game/rs_audio.c#L776)),
i.e. it plays **sound effect N** on a permanent loop. On osrs239 that is the
wrong asset entirely; on osrs230 there is no such config type, so the current
behaviour is the only thing possible there and should stay era-gated.

`rscache` has no decoder for group 15, and `RSCACHE_DAT2_CONFIG_KIND_VARCLIENT_STRING = 15`
claims the slot. Nothing reads that constant, so the collision is inert — but 15
in an OldSchool cache is the soundscape type, not varclient strings (group 15 has
8 large records; varplayer 16 has 5,705 one-byte ones and varclient 19 has 1,505).

Finally: **`mock230` never sends `AMBIENTSOUND_START`.** Nothing exercises this
path against the embedded server, which is why the gap survived the audio work.

---

## 2. Region soundtracks

### 2.1 How it actually works

Music is **server-driven**. The server watches which map square the player is
standing in, and on entry it (a) sets the track's unlock bit if unset and
announces "You have unlocked a new music track: …", and (b) if the player's
music preference is AUTO, sends `MIDI_SONG <archiveId>` and pushes the track name
into the music-player widget. The client just plays what it is told; it has no
region table.

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

Nothing in this tree has one. The best machine-readable source found is Kronos's
`MusicPlayer.java` (`~/Documents/git_repos/kronos-osrs-184/…/inter/handlers/MusicPlayer.java`),
which states `{displayName, songArchiveName, ...regionIds}` for 535 tracks.
Extracted to **[docs/audio/music_regions.tsv](audio/music_regions.tsv)**:

- 364 of 535 tracks carry regions; **440 region entries over 435 distinct map
  squares**.
- 5 squares are claimed by two tracks (Courage/Long Way Home at 11826,
  Dance of Death/Dance of the Undead at 14131, …) — these need a tiebreak the
  source does not provide.
- The remaining 171 tracks are marked `//todo` upstream, and osrs239 has 876
  tracks against Kronos's 535, so **coverage is roughly 40 %** of the modern
  track list.

Spot-validated against the OSRS Wiki and against the cache's own hint text:

| track | region | → tile | cache hint | wiki |
|---|---|---|---|---|
| Sea Shanty | 11569 | (2880, 3136) | *at Musa Point.* | Musa Point, Cache ID 92 ✓ |
| Adventure | 12854 | (3200, 3456) | *in Varrock.* | Varrock ✓ |
| Al Kharid | 13105 | (3264, 3136) | *in Al Kharid.* | Al Kharid ✓ |
| Harmony | 12850 | (3200, 3200) | *in Lumbridge.* | Lumbridge ✓ |
| Barbarianism | 12341 + 12441 | (3072, 3392), (3072, 9792) | *in Barbarian Village.* | village + its dungeon ✓ |

The dual-square Barbarianism entry is the shape to expect: an overworld square
plus the `+6400` underground square.

**To complete the table**, the sources in order of usefulness:

1. `https://oldschool.runescape.wiki/w/Map:Music_tracks` — the authoritative
   polygon-per-track map; the underlying data page is what to scrape, not the
   rendered map.
2. `https://oldschool.runescape.wiki/w/Map:Music_tracks/Classic` — the
   pre-rework, one-square-per-track mapping, which is *closer* to what a
   region-keyed server implementation wants.
3. The cache's own hint strings (column 2 above, all 876 of them) — prose, but
   they name the place for every track including the ones Kronos never mapped,
   so they are the checklist.
4. `https://rune-server.org/threads/634-all-music-track-map-coordinates.698625/`
   — a 634-era coordinate dump; wrong era, useful for the classic-era tracks.

### 2.4 Client-side gaps

- `MIDI_SONG` / `MIDI_SONG_WITHSECONDARY` / `MIDI_SWAP` / `MIDI_JINGLE` /
  `MIDI_SONG_STOP` are all parsed and played ([rs_gameproto_exec.c:1341](src/game/rs_gameproto_exec.c#L1341)).
  Nothing to do here.
- `mock230` sends a song **once, on login** (`MOCK230_SONG=<id>`). There is no
  zone-entry hook, no unlock varp write, and no music-player widget text.
- The unlock bits (column 5) are a varp range the content tree does not declare;
  this overlaps the undeclared-varp work in `docs/CS2_UNIMPLEMENTED_VARPS.md`.

---

## 3. Combat, weapon and loc sounds

### 3.1 Weapon and combat sound is server-driven — and that chain is complete

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

> **Correction to an earlier note.** `SYNTH_SOUND` really is unpositioned, and
> that is the reference's behaviour, not a gap in ours. `Message.queueSoundEffect`
> sets `soundLocations[i] = 0`, and the drain reads location 0 as "use
> `soundEffectVolume`" — full volume, no pan. Nothing more to do here.

### 3.2 Effect timing: the queue is right in shape and wrong in two places

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

The fix for (1) and (2) is small and belongs together: decrement unconditionally
at the top of the loop, drop the entry once `delay < -10`, and delete
`RS_AUDIO_LOAD_WAIT_TICKS` (the −10 window replaces it).

> **Why the tests never caught it.** `rs_audio_test.c:463` asserts an effect
> plays on pass `delay + trim + 1`, which is the reference's schedule — and it
> stays true under the fix, because for a **resident** clip the unconditional
> decrement and the deferred one give the same answer. The whole defect lives in
> the not-yet-resident case, and the harness publishes its clip before it
> queues. A test that reproduces this has to queue an effect whose asset is not
> yet in the scene and count passes, not just assert that it eventually plays.

### 3.3 Frame sounds: plumbed, but one sound per frame, no weighting, no radius

The cache's per-frame sounds *are* now wired — `docs/WEAPON_FX.md` §6.5's claim
that "nothing plays them" is stale. `seq_copy_frame_sounds`
([task_dat2_sequence_load.c:124](src/engine/dat2/task_dat2_sequence_load.c#L124))
copies them into the animation and `app_play_frame_sounds`
([app.c:6268](src/app.c#L6268)) fires on frame advance, positioned by the
element's world coordinates.

Measured:

| | osrs230 | osrs239 |
|---|---|---|
| seqs with frame sounds | 1,212 | 1,588 |
| sound entries | 2,963 | 4,329 |
| frames carrying **>1** sound | 9 (max 3) | **67 (max 6)** |
| entries with `weight ≥ 0` | 2,963 (all) | 4,329 (all) |
| entries with `retain ≠ 0` | 621 | 1,076 |
| entries with `location ≠ 0` | 2,840 | 3,906 |

> **`location` is the audible radius in tiles.** `class30.addSequenceSoundEffect`
> packs `soundLocations[i] = location | (tileZ << 8) | (tileX << 16)`, and the
> drain reads `(soundLocations & 255) * 128` as the radius in fine units, with
> the sound *discarded* (`delays = -100`) when the listener is outside it. So
> `location` is not a position flag; it is the range. A frame sound with
> `location == 0` has radius 0 and is audible essentially nowhere, which is what
> the 423 zero entries in osrs239 mean.

Gaps:

1. **Only `id` and `loops` survive the port.** `location` (= radius, 90 % of
   entries), `retain` and `weight` are decoded by `rscache` and dropped at
   [task_dat2_sequence_load.c:145](src/engine/dat2/task_dat2_sequence_load.c#L145).
   `RS_Audio_SynthAt` has no radius parameter at all, so every positional effect
   uses the hardcoded `AUDIO_DEFAULT_DISTANCE = 12`
   ([rs_audio.c:28](src/game/rs_audio.c#L28)) — a chop that should carry 3 tiles
   is heard across a quarter of the scene.
2. **A frame with several sounds plays exactly one, chosen by binary-search
   landing.** `app_play_frame_sounds` binary-searches `frame_indices` and
   `return`s on the first hit — its own comment says "queue all sounds for this
   frame" but it queues one, and *which* one depends on where the search lands,
   not on `weight`. Every entry in both caches carries a weight and 67 osrs239
   frames have up to 6 alternatives, so this is the weighted-random-pick
   mechanism going unused. `weight` semantics remain **unconfirmed** (the rev184
   client predates the field); the data shape implies weighted selection.
3. The positional volume for effects reuses `positional_gain`'s Chebyshev metric
   and quarter-volume floor, where the reference uses the same
   Manhattan-minus-128 falloff as area sounds and cuts to silence at the radius.
   Same fix as §1.3 (3)(4).
4. **`sounds_cross_world_view` (seq opcode 19) is decoded and unused** — the flag
   that says a sound is audible beyond the normal cull.
5. Frame sounds fire when `anim_seq_id != old_seq_id || anim_frame != old_frame`.
   Correct for advance, but it also fires on a *re-application* that changes the
   frame — worth checking against [[chathead-anim-reapply-reset]], the same class
   of bug in the UI path.

### 3.4 `SOUND_AREA` is unimplemented, and it truncates zone batches

The server's *positional* sound packet — the one that makes a door three squares
away sound like it is three squares away — is not decoded:

```
src/net/rev/osrs239/zoneprot.h:38   OSRS239_ZONE_SOUND_AREA = 14
src/net/rev/osrs239/packetin.h:92   {  32, 7, PKT_NAME_NONE, "SOUND_AREA" }
```

Reference payload (the zone form, from the rev184 client's zone reader):
`p1 coordInZone`, `p1 (radius << 4) | loops`, `p2 id`, `p1 delay` — 5 bytes, and
the client range-checks against `radius + 1` tiles before queueing it with
`soundLocations` set, i.e. it goes through the same effect queue as §3.2 but
positioned and radius-limited.

Two consequences, and the second is not an audio bug:

- Every server-driven positional sound is silent.
- Ordinal 14 inside an `UPDATE_ZONE_PARTIAL_ENCLOSED` batch has no case in
  `osrs239_zone_name`, so `osrs239_read_zone_sub` fails and the enclosed loop
  **`break`s** ([osrs239_parse.c:1337](src/net/rev/osrs239/osrs239_parse.c#L1337)),
  discarding every remaining sub-packet in that batch — loc changes, obj spawns,
  projectiles. One area sound from a live server silently drops the rest of that
  zone update. It prints `osrs239: ZONE_ENCLOSED unknown sub-ordinal 14` to
  stderr, which is the string to grep for.

This is latent against `mock230`, which never sends it: `[proc,sound_area]` in
the content tree fans out plain `sound_synth` to every player found by `huntall`
([mock230_scripts.c:4356](src/net/mock/mock230_scripts.c#L4356)) — the LostCity
254-era emulation. So against the mock, an area sound arrives unpositioned and
plays centred at full volume however far away it was.

### 3.5 Loc interaction sounds

There is no "sound when you use this loc" field in `LocType` in any era we
decode. Door creaks, lever pulls and furnace roars are all server-sent —
`SYNTH_SOUND` for the actor, `SOUND_AREA` (§3.4) for everyone else nearby. Only
the *ambient* loc sound (§1) is cache-side.

---

## 4. Suggested order of work

Ranked by audible effect per unit of risk:

1. **§3.2 (1)(2) — effect timing.** Decrement the delay unconditionally, drop at
   `< -10`, delete `RS_AUDIO_LOAD_WAIT_TICKS`. ~10 lines, fixes the reported
   mistimed weapon sounds, and it is the only item here that makes *existing*
   sounds land where they should rather than adding new ones.
2. §3.4 — decode `SOUND_AREA`. Do the zone-ordinal case even before the audio
   side: an unknown ordinal 14 currently discards the rest of the zone batch, so
   this is a correctness fix that happens to be in the audio chapter.
3. §1.3 (1) — play both streams for the 33 dual-source locs. Local change to
   `RS_AudioAreaVoice`.
4. §1.3 (6) — re-roll the random set's id on each fire, and use a real gap.
5. §1.3 (2)(3)(4) + §3.3 (1)(3) — thread `size_x`/`size_z`, the inner radius and
   the frame sound's own radius through to one shared falloff, then replace
   `positional_gain` with the reference formula. One coherent change; do them
   together or the metric stays inconsistent between the two callers.
6. §3.3 (2) — play every sound on a frame, or pick by weight. Cheap.
7. §1.6 — decode config group 15 and drive `AMBIENTSOUND_START` from it, era-gated
   (osrs239 only; osrs230 has no such group). Needs a new `rscache` datatype and
   a `mock230` sender to exercise it.
8. §2.3 — finish the region → track table from the wiki, then a zone-entry hook
   in `mock230` that unlocks and plays.
9. §1.3 (5)(7)(9), §1.4 — fades, emitter persistence across rebuilds, multiloc
   re-resolve, loc-change updates.
10. §1.5 — npc/player movement sounds. Last: no cache in this tree carries the
    data, so it cannot be validated by playing it.

---

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
