# Queen Black Dragon — audio audit

What the 29-May-2012 encounter is supposed to *sound* like, where each piece
lives in `cache.rs727_preeoc`, and where it lands in `cache.osrs239.rs2012`.
Companion to `ENCOUNTER.md` (the encounter script) and `RESEARCH.md` (the
timing provenance); `RS2012_QBD_TD.md` and the lane's `PROVENANCE.md` are the
port ledger.

Every source id below was read out of `cache.rs727_preeoc` for this audit —
sequences from index 20 (128 records per group), locs from index 16, npcs from
index 18, samples from index 14, synths from index 4. Destination ids come from
`OSRS-Content/osrs239-content/port/rs2012_qbd_td.map`.

The encounter's audio has four independent layers, and they fail independently:

| Layer | Carrier | Who plays it |
|---|---|---|
| Music | songs 1119 / 1118 | server, `midi_song` |
| Sound effects | **sequence frame-sound events** | client, off the animation frame |
| Area sounds | **loc ambient emitters** on the arena scenery | client, off the built scene |
| Ambient bed | `AMBIENTSOUND_START` soundscape | server, per map square (`ambientsound`) |

Nothing in the encounter dispatches a sound from a script. Every effect below
rides an animation the server was already playing, which is why an animation
that is never bound is also an effect that is never heard (§4.3).

## 1. Music

Two tracks, both era-correct and both already ported:

| Song | Name | When |
|---:|---|---|
| 1119 | *Awoken* | arena entry (`rs2012_qbd_session.rs2`, on the teleport) |
| 1118 | *Queen Black Dragon* | first artefact restoration onward |

Both unlock "during the fight with the Queen Black Dragon" per the RS wiki, and
no third track is tied to the lair. `midi_song(-1)` in
`[proc,rs2012_qbd_clear_state]` stops the music on leave, death and logout.

Ids are packed at their source numbers (`preserve_audio_ids=yes`) with patch
1157 and the rev-727 Vorbis setup at index-14 archive 16000.

One deliberate divergence from retail, already recorded in `ENCOUNTER.md` §3:
the wiki says *Queen Black Dragon* "plays after Awoken finishes"; this port
switches on the first restoration instead, which is a content-legible cue
rather than a 59-second timer. Left as is.

## 2. Sound effects — the per-attack contract

Source sequence → destination sequence, and the frame each sound fires on.
`radius` is the emitter radius the record carries; `loops` is its repeat count.
Alternatives on one frame are a random set: the client rolls one per play.

| src | dest | What | Frame | Sound ids |
|---:|---:|---|---:|---|
| 16714 | 22000 | wake | 0/8/25/28/40/44/45/59/77/84 | 14969, 14991, 15022, 14832, 14989, 14975, 14912, 14940, 14992, 14914 |
| 16715 | 22021 | ready — "her breathing shakes the chamber" | 0 | **14915 / 15022 / 14969** |
| 16715 | 22021 | ” | 24 | **15015 / 14991 / 14972** |
| 16716 | 22022 | ready, sleeping form | 5/18/28/43 | same two triads, alternating |
| 16717 | 22001 | melee bite, centre | 3 | **14986 / 14930 / 15010** |
| 16743 | 22004 | melee bite, west swing | 3 | **14986 / 14930 / 15010** |
| 16744 | 22005 | melee bite, east swing | 3 | **14986 / 14930 / 15010** |
| 16718 | 22061 | ranged sweep | 0 | 14964 |
| 16721 | 22002 | ordinary dragonfire | 0 | 14908 |
| 16745 | 22006 | extreme dragonfire | 0 | 14988 |
| 16746 | 22007 | fire-wall wind-up | 0, 23 | 14984, 14896 |
| 16742 | 22003 | return to sleep | 0/1/15/23/40/53/54/57/58 | 14832, 15622, 14975, 14991, 14992, 14940, 14832, 15613, 15621 |
| 16782 | 22064 | giant worm attack | 7 | **14844 / 14979 / 14974 / 14967** |
| 16787 | 22065 | giant worm crawl | 10 | **14921 / 15000 / 15014 / 14966 / 14922** |

Silent in the source, and therefore silent here — this is the era record, not a
gap: the grotworm cough (16747) and stop-cough (16748), the fire-wall body
(16761), the tortured soul's ready/walk/teleport/shadow-cast/death
(16883/16884/16861/16864/16859), the giant worm's ready/defend/death
(16786/16779/16778), and all 25 encounter spotanims (3141–3165) except 3151 and
3153, whose sequences carry sample 15633. The wall makes noise once, on her
wind-up; it is silent in flight.

## 3. Area sounds — the arena bed

The 2012 lair has no soundscape record. Its ambience is emitted by the arena's
own scenery: seven loc types placed 45 times across map square (22,99), several
on all three levels. All are ported and all resolve.

| dest loc | src | Placements | Emitter |
|---:|---:|---:|---|
| 63055 | 226 | 18 | continuous loop, synth 16006 (src 4960), radius 7 |
| 63057 | 16455 | 2 | random set of 11 synths (16007–16016), every 200–300 ticks, radius 15 |
| 63204 | 67742 | 6 | random set of 10 synths (16018–16024), every 350–650 ticks, radius 10 |
| 63251 | 72474 | 3 | continuous loop, sample 15549, radius 33 |
| 63252 | 72475 | 3 | continuous loop, sample 15499, radius 33 |
| 63256 | 72537 | 3 | random set of 7 samples, every 200 ticks, radius 30 — at the platform centre (33,31) |
| 63257 | 72544 | 9 | random set of 6 samples, every 100 ticks, radius 15 |

The instance is built with `map_instance_from_square`, so the emitters come
with the square and `world_builder_add_loc_area_sound` registers each one.
Loc opcode 78 (single continuous id) and 79 (random set with a tick gap) both
decode, and both halves of the id namespace resolve: 16000+ hits index 4
directly, 14000+ falls through to the imported index-14 lane.

The glowing artefacts add one more: `rs2012_loc_70777/80/83/86` (the
"unguarded" forms) carry `soundid=15635, sounddistance=15`, so an artefact
waiting to be restored is audible from across the platform.

## 4. What this audit found

### 4.1 Frame-sound alternatives were decoded and thrown away — **fixed**

`decode_sequence_rs2` read rev-727 opcode 13 as "one sound per frame, consume
the rest for alignment". Rev 727 writes a frame's first sound packed into 24
bits and its **alternatives** as bare u16 ids after it; the client rolls one of
them per play. Keeping only the first flattened every varied sound in the
encounter to one fixed sample, and — because the importer's audio closure walks
`frame_sounds` — kept the alternative payloads out of the port entirely.

The destination could always carry them: the 226+ record stores an explicit
frame per entry, so alternatives are just repeated entries on one frame, and
`app_play_frame_sounds` already widens the run and rolls it by weight (67
osrs239 frames use exactly this). The lane's `PROVENANCE.md` claim that "the
OSRS239 destination record has no equivalent field" was wrong.

19 frames across 12 sequences declared alternatives; **23 sample payloads** were
missing. Her idle breathing and all three bite animations rolled 3 ways, the
worm's attack 4 and its crawl 5, and Sir Rebrum's four walk cycles 14 footsteps
each.

Fixed in `3rd/rscache/src/datatypes/dat2_config_sequence.c` (decoder keeps the
alternatives; the RS2 encoder writes them back), re-imported, and verified end
to end: 184 sequence sound events over 106 samples, all 106 decoding out of the
composed cache under `audioprobe --sample-with-setup 16000`.

### 4.2 Per-sound volume and rate jitter are still dropped — **evidence only**

Rev-727 opcodes 19 and 20 carry, per frame sound, a volume percentage and a
random playback-rate range. Across the encounter's sequences that is **32
volume overrides and 22 rate-jitter records**, e.g. her ready breath at 40%,
the fire-wall wind-up at 150%, the wake's frames ranging 75–150%, and a
±4% pitch wobble (246..266 of 256) on the bite, both dragonfires, the sweep and
the wall. The osrs239 frame-sound record has fields for id, weight, loops,
radius and retain and nowhere to put either, so they are recorded here and not
carried. Consequence: her sounds all play at one flat level and one exact
pitch.

### 4.3 The giant worm's crawl sound is unreachable — **open**

Rev-727 BAS 2500 gives the worm idle 16786, **walk = none**, and crawl (opcode
2) plus its turn/idle-left/right variants = 16787. The importer flattens only
opcode 1's (idle, walk) pair onto the destination npc, so
`rs2012_qbd_giant_worm` has `readyanim` and no `walkanim` — and 16787, the only
worm sequence with a movement sound (a 5-way set), is exported but bound to
nothing. A moving worm is silent and unanimated.

The fix belongs in the importer's BAS flattening (fall back to the crawl seq
when opcode 1's walk is the 0x7FFF sentinel), not in the generated npc config.
The soul is unaffected — BAS 2514 states both halves on opcode 1, and both
ported.

### 4.4 A foreign OldSchool ambient bed drone over the 2012 arena — **fixed**

`torirs_server_world.c` sent `AMBIENTSOUND_START(soundscape 1)` once at login and
never revised it; the comment said so plainly ("that is not authenticity, it is
*reachability*"). That made the bed a property of the *session* rather than of
the place — so an OldSchool soundscape (group 15 is a 231+ type rev 727 does
not have) played underneath the arena's authored loc emitters for the whole
fight, at full `area_volume`. Two ambiences, one of them from the wrong game.

Three changes:

- The bed now hangs off the **map-square latch**, beside the music that is
  keyed the same way, through the same instance-aware square resolver
  (`ToriRSServer_RegionSquareFor` — an instanced player's own square describes
  nothing, so both resolve through the square the instance was copied from).
  There is still no region→soundscape data in any cache, so the bed itself is
  still one placeholder for the whole world; what is now per-square is who
  owns it.
- A new ServerScript command, **`ambientsound(int $soundscape)`** — opcode
  11038, in the 11000+ band this tree allocates for surface the LostCity
  reference does not have. `AMBIENTSOUND_START` when the argument is >= 0,
  `AMBIENTSOUND_STOP` when it is negative, the same -1-means-stop rule
  `midi_song` and `sound_synth` state. It is not a spelling of `sound_synth`:
  the id names a soundscape record, not a sound effect.
- The arena claims its own bed: `ambientsound(-1)` beside the `midi_song(1119)`
  on entry.

Calling the command claims the caller's map square, and the claim is released
by leaving that square. That is what makes it order-independent against the
latch, which fires *later in the same tick* as the teleport that arrives in the
arena — and it means no exit path has to restore anything. Teleport out, die,
log out: the square changes, the claim lapses, the world bed returns. The QBD
teardown says nothing about ambience at all.

## 5. Verification

```sh
# Content tree + composed cache, including every sample payload
python3 tools/test_rs2012_audio_bridge.py
make -C src torirsserver-cache-rs2012

# The codec change itself
make -C 3rd/rscache build/test_rs530_codec && 3rd/rscache/build/test_rs530_codec

# The ambient bed: the claim, its release, and the command reaching its handler
make -C src test-ToriRSServer test-torirsserver-coverage
```

`test_rs2012_audio_bridge.py` is the contract: 29 synths, 106 samples + the
Vorbis setup, 2 songs, 1 patch; 184 sequence sound events of which 177 are
recorded samples and 7 are index-4 synths; 103 loc sound references over 72
unique ids; and every one of them resolving to a packed archive. With
`--cache` it re-decodes all 106 samples, both songs and the patch out of
`cache.osrs239.rs2012` through `audioprobe`.

Spot check that the alternatives really reached the cache:

```sh
3rd/rscache/tools/cachepack/cachepack unpack --cache cache.osrs239.rs2012 \
  --rev osrs239 --src /tmp/qbd --types seq
# the bite: three sounds on frame 3
awk '/^\[seq_22001\]$/{f=1;next} /^\[/{f=0} f && /^sound=/' /tmp/qbd/configs/all.seq
```

Still unproven by test and worth a listening pass: that the rolled alternative
is audibly different from the primary (the audit only proves the payloads
decode), the perceptual result of §4.2's flat volume, and that the arena's
seven loc emitters actually read as a cave once the OldSchool bed is out from
under them.
