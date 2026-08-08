# AUDIO_SYSTEM_OPUS

The audio subsystem of the C client: cache decoding, synthesis, the retained
platform API, and the game-side triggers that decide what is heard.

Audio is built the same way rendering is. The game never touches a device: it
puts **commands** on a queue, and the host drains that queue once per frame and
hands each command to a backend. Clips are **assets** — loaded once under an id
into the `ToriDraw_Scene` asset registry beside models, sprites and fonts, and
referenced from every play *by handle*. That is a retained API, not a per-play
buffer loan, and the shape is forced rather than chosen: playback in a browser
may only start inside a user gesture and the WebAssembly heap moves under any
pointer you hand out, so "the host owns the bytes, the game owns the decision"
is the only portable split.

Five things in the cache make sound, and all five are implemented:

| Source | Cache index | Shape |
| --- | --- | --- |
| Sound effects | 4 (`SOUNDEFFECTS`) | FM-synth programs, and (modern OSRS) Vorbis samples |
| Music tracks | 6 (`MUSIC_TRACKS`) | column-packed MIDI |
| Music jingles | 11 (`MUSIC_JINGLES`) | column-packed MIDI |
| Music samples | 14 (`MUSIC_SAMPLES`) | Vorbis, with **one shared setup header in archive 0** |
| Music patches | 15 (`MUSIC_PATCHES`) | soundfont instrument definitions |

### Where the code lives

```
3rd/rscache/src/datatypes/
    sound_synth.{c,h}      FM-synth program decode (pre-existing)
    sound_render.{c,h}     synth -> 8-bit PCM       (pre-existing)
    sound_vorbis.{c,h}     Vorbis setup + sample decode -> 16-bit PCM
    music_patch.{c,h}      index 15 instrument patches
    music_song.{c,h}       index 6/11 column-packed MIDI -> standard MIDI
3rd/rscache/tools/audioprobe/   survey/dump/decode/sweep the audio tables
3rd/rscache/test/test_music.c   corpus sweep over osrs184/230/239

src/audio/
    torirs_audio.h         the platform <-> game command interface
    torirs_pcm.{c,h}       one playing sample: resample, loop, ramped stereo gain
    torirs_mixer.{c,h}     retained assets + voices + streams + buses (shared by backends)
    torirs_midi_file.{c,h} SMF reader: per-track cursors, running status, tempo
    torirs_soundbank.{c,h} patches + samples with reference-counted lifetimes
    torirs_midi_synth.{c,h} the sequencer and sampler: MidiPcmStream
    torirs_music.{c,h}     what is playing, what is loading, and how loud
    test/audio_test.c      the engine with no device attached

src/platform/platform_audio{,_sdl2,_null,_wasm}.{c,h}   device I/O only
src/game/rs_audio.{c,h}   effects, area sounds, asset publication, music routing
src/engine/dat2/task_dat2_music_load.c   the song -> patches -> samples load chain
3rd/toridraw/toridraw_scene.c            the sound asset registry
```

---

## TODO

`[x]` = implemented **and** exercised by a test or a harness run; `[~]` =
implemented, not yet verified; `[ ]` = not started.

### Phase 0 — research
- [x] Map the reference audio class family in the osrs239 deob (see *Reference class map*)
- [x] Identify the idx14 container empirically (Vorbis; archive 0 is the shared setup header)
- [x] Recover the column-packed MIDI format (idx6/idx11)
- [x] Recover the MusicPatch (idx15) format

### Phase 1 — cache decoding (`3rd/rscache`)
- [x] `sound_vorbis.{c,h}` — Vorbis setup header + per-sample packet decode → PCM
- [x] `music_patch.{c,h}` — idx15 instrument patch decode
- [x] `music_song.{c,h}` — idx6/idx11 column-packed → standard MIDI + patch/note usage map
- [x] `tools/audioprobe` — survey, per-archive dump, `--sample/--effect/--song/--jingle/--patch`, `--sweep`
- [x] `test/test_music.c` — corpus sweep over osrs184/230/239

Measured on `cache.osrs239`: 580/580 music samples decode, 187/187 patches
consume their record **exactly**, 881/881 tracks and 315/315 jingles unpack and
re-parse to their declared track lengths.

### Phase 2 — retained asset API
- [x] `ToriDraw_Scene` sound registry (`ToriDraw_SceneSoundAdd/Get/Has/Remove/ReemitLoads`)
- [x] `TORIDRAW_EVENT_SOUND_LOAD` / `TORIDRAW_EVENT_SOUND_UNLOAD`
- [x] `ToriRS_AudioCommand` as a retained command set (asset load/unload/clear,
      voice start/update/stop, stream open/push/close/volume, bus volume, stop-all)
- [x] `ToriRS_AudioFeedback` — the one thing the host tells the game back
- [x] Backends rebuilt on the shared mixer: SDL2, null, wasm

### Phase 3 — synthesis engine (`src/audio`)
- [x] `torirs_pcm` — resampling voice: 8.8 position, linear interpolation,
      forward/backward, loop and ping-pong, ramped constant-power pan
- [x] `torirs_mixer` — asset table, voice pool with stealing, stream rings, buses
- [x] `torirs_midi_file` — track cursors, running status, sysex, tempo folding
- [x] `torirs_midi_synth` — 16 channels, patch nodes, amplitude/release envelopes,
      exponential decay, vibrato with delay, portamento, sustain/legato, exclusive
      groups, start offset, retrigger
- [x] `torirs_soundbank` — patches and samples keyed by (table, id), ref-counted

### Phase 4 — game integration
- [x] `SYNTH_SOUND` effects with the reference's delay + trim + overlap rule
- [x] Positional effects (`RS_Audio_SynthAt`) — attenuated and panned by tile
- [x] Sequence frame sounds, now positional from the element's world position
- [x] Music: `MIDI_SONG` / `MIDI_SONG_V2`, with the V2 **fade envelope** decoded
      rather than discarded
- [x] Jingles: `MIDI_JINGLE`, resuming the interrupted track when they end
- [x] Fade in / fade out on the music stream
- [x] Area sounds from loc `ambient_sound_*` — gathered at scene build, one
      looping voice per emitter, gain and pan following the camera
- [x] Volume: effects / music / area as three independent buses; varp
      clientcode 4 drives the effects and area buses
- [x] Async load chain: song → patches → the samples the used notes reference
- [x] `TORIRS_SIM_SONG` / `TORIRS_SIM_JINGLE` harnesses
- [ ] Random-set area emitters use a deterministic cycle, not the reference's RNG
- [ ] `MIDI_SONG_WITHSECONDARY` and `MIDI_SONG_STOP` are parsed as `PKT_NAME_NONE`
      by the osrs239 table, so they never reach the player
- [ ] CS2 host opcodes for sound (`SOUND_SYNTH`, `SOUND_SONG`, `SOUND_JINGLE`)
- [ ] NPC sounds: the npc decoder *consumes and discards* opcode 134 (idle /
      crawl / walk / run sound ids plus a radius) and 140 (ambient sound volume),
      so they never reach a struct the game could read

### Phase 5 — verification
- [x] `make -C src test-audio` — 68 checks: mixer contract + a real song rendered
- [x] `make -C src test-sound` — game layer end to end on dat1 254 and dat2 230
- [x] `make -C 3rd/rscache test` — includes `test_music`, 239,486 checks
- [x] Leak audit under `leaks`: zero audio leaks (found and fixed two, see D9)
- [x] Live headless run: 251k frames pushed, 0 starved, 0 dropped
- [x] Output capture (`TORIRS_AUDIO_WAV`) analysed: 16.4 s of music + effects
      with 0 clipped samples and 0 discontinuities
- [ ] Byte-compare a rendered song against the reference renderer

---

## Procedures

### Build and run the tests
```sh
make -C src test-audio                 # engine + mixer + a real song, no device
make -C src test-sound                 # game layer -> null backend, real caches
(cd 3rd/rscache && make test)          # includes test_music; run from that dir
make -C 3rd/rscache/tools audioprobe   # the cache-inspection tool
```
`test_music` and `audio_test` resolve caches relative to the **working
directory**; run them the way the makefile does or they silently SKIP.

### Hear something without a server
```sh
TORIRS_SIM_SOUND=<id>[,loops[,every_frames]] TORIRS_AUDIO_DEBUG=1 \
  ./src/torirs --manifest manifest_osrs239.ini
TORIRS_SIM_SONG=<id>   TORIRS_AUDIO_DEBUG=1 ./src/torirs --manifest manifest_osrs239.ini
TORIRS_SIM_JINGLE=<id> TORIRS_AUDIO_DEBUG=1 ./src/torirs --manifest manifest_osrs239.ini
```

### Read the audio ledger
`TORIRS_AUDIO_DEBUG=1` (or `TORIRS_AUDIO_TRACE=1`) traces every asset load,
voice start and stream open, and prints a summary at exit:

```
audio: 493 commands, 13 voices started (0 stolen, 0 rejected), 251108 frames
       played, stream 0 dropped / 0 starved, 1 assets still live
```

What each number means when it is wrong:
- **voices rejected** — a `VOICE_START` named an asset the backend does not have.
  An ordering bug, not a missing file. See D10.
- **stream starved** — the synth is not keeping up, or `App_SetAudioFeedback` is
  not being called before the tick.
- **stream dropped** — the game is synthesising further ahead than the ring holds.
- **assets still live** — expected to be small and *stable*; a count that climbs
  across a session means ids are being reloaded rather than reused.

### Look at the cache's audio tables
```sh
P=./3rd/rscache/tools/audioprobe/audioprobe
$P cache.osrs239 osrs239                      # survey every audio table
$P cache.osrs239 osrs239 14 1                 # hexdump one archive
$P cache.osrs239 osrs239 --sample 1  --out /tmp/s.wav
$P cache.osrs239 osrs239 --effect 12 --out /tmp/e.wav
$P cache.osrs239 osrs239 --song 0    --out /tmp/song.mid   # opens in any sequencer
$P cache.osrs239 osrs239 --patch 0
$P cache.osrs239 osrs239 --sweep               # decode everything, report failures
```

### Capture what the client actually played
```sh
TORIRS_AUDIO_WAV=/tmp/client.wav ./src/torirs --manifest manifest_osrs239.ini
```
The SDL2 backend tees every block it gives the device into a WAV, header patched
on close. This is the tool for "it sounds wrong": counters cannot tell a click
from a clean play, and a spectrum or a sample-difference scan over the capture
can. What to look for:

- **large sample-to-sample jumps** — a discontinuity, i.e. a clip being cut off
  mid-waveform. This is how D11 was confirmed fixed (0 jumps over 16 s).
- **clipped samples** (`|x| >= 32767`) — gain staging, see D7.
- **silent runs longer than a frame** — stream starvation; cross-check the
  `starved` counter in the audio ledger.

### Listen to what the test rendered
```sh
TORIRS_AUDIO_TEST_WAV=/tmp make -C src test-audio
# writes /tmp/osrs239_track0.wav and /tmp/osrs230_track2.wav
```
The statistical checks catch a *broken* synth; only listening catches a subtly
wrong one, so the test leaves its output on disk when asked.

### Prove a decoded sample is music and not noise
Decode it to WAV, then look for a fundamental with harmonics:
```python
import wave, struct, math, cmath
w = wave.open('/tmp/s.wav'); n = w.getnframes()
d = struct.unpack('<%dh' % n, w.readframes(n))
N = 4096; seg = d[n//3:n//3+N]
best = sorted(((abs(sum(seg[i]*cmath.exp(-2j*math.pi*k*i/N) for i in range(0,N,4))),
                k*22050/N) for k in range(1,400)), reverse=True)
print([round(f,1) for _, f in best[:6]])
```
A working decode gives a fundamental and integer multiples of it (music sample 1
is 247.6 / 495.3 / 742.9 Hz — B3 and its harmonics). A broken one gives a flat
spread across the band.

---

## Major discoveries

Each entry says how it was measured, not just what is true.

### D1 — index 14 archive 0 is the global Vorbis setup header

**How measured.** `audioprobe` hexdumps of `cache.osrs239` table 14. Archive 0
begins `aa 22 42 43 56 01 00 08 00 00 80 …`; archives 1..581 begin
`00 00 56 22 00 00 73 d4 …`. `42 43 56` is the Vorbis **codebook sync pattern**
`0x564342` emitted LSB-first by a bit-packed stream, and it appears in exactly
one archive. Decoding archive 0 as a setup header reproduces the whole
structure: the two nibbles of `0xaa` are `blocksize0 = blocksize1 = 1 << 10`,
the next byte `0x22` is `codebookCount - 1 = 34`, and codebook 0 then starts at
its sync pattern. The reference agrees (`class55.method1044` loads archive 0,0
once and sets a static flag).

**Why it matters.** There is one setup header for every music sample in the
cache, and the samples carry only audio packets. A per-sample decoder that
expects a self-contained Vorbis stream finds no codebooks and produces silence —
and silence is exactly what "music doesn't work" looks like.

The sound-effects table is different: when an effect archive has a second group
file, that file is `u4 setupLength`, a setup header, then a sample. Each is
self-contained and must not clobber the music one, which is why setup is an
explicit object here and a process-wide static in the reference.

### D2 — the modern sound-effect record is 16-bit; the old renderer was 8-bit

**How measured.** `class17` (RawSound) in the osrs239 deob holds `short[]`, and
`class28.method279` clamps to `-32768..32767`; the same method has an 8-bit
branch (`>> 8`, clamp `-128..127`) selected by a boolean, which is the low-memory
mode. The pre-existing `sound_render.c` renders to 8-bit unsigned because that is
what the RS2-era client did.

**Consequence.** Everything downstream is 16-bit signed, and the RS2-era
renderer's output is *widened* on the way in (`(sample - 128) << 8`) rather than
the modern path being narrowed. Nothing in the corpus loses precision.

### D3 — the sample header is four big-endian `u4`s and a packet count

**How measured.** Table 14 archive 1's first 20 bytes as `u4`s are
`22050, 29652, 23323, 29652, 59`. 22050 is the rate; 29652 samples at 22050 is
1.34 s, consistent with 4122 compressed bytes; 23323 < 29652 is a loop point
inside the sample. The reference (`class55.method1054`) reads exactly four
values then a count, and a **negative end means "this sample loops"** with
`end = ~end`. The 59 packets that follow are length-coded by 255-lacing, the way
Ogg laces a page.

### D4 — a song is a **column-oriented** MIDI, not a stream

**How measured.** `class344`'s constructor reads the track count and division
from the *last three bytes*, makes a first pass counting events by kind, computes
a table of per-kind byte-run offsets, then re-reads and re-emits a standard
`MThd`/`MTrk` file. Every field of every event kind lives in its own contiguous
run, delta-coded against the previous value of that field: all note numbers
together, all velocities together, controller values split by controller number.
That is why the archives compress so well and why a naive "parse events in order"
reader gets garbage.

**Consequence.** The unpacker is a faithful two-pass port; there is no shortcut.
It emits a real SMF, which is also the cheapest way to check it —
`--song 0 --out x.mid` opens in any sequencer, and re-parsing it here proves
every track's declared length matches its body.

### D5 — the song carries its own instrument manifest

`class344` also builds a map from *banked program number* to the set of notes
that program ever plays. The client loads only those samples. Track 0 of
osrs239 names 11 patches and needs 23 samples; loading all 128 notes of 11
patches instead would be several times the memory and would stall the first bar.

### D6 — MusicPatch is run-length coded against five side streams

**How measured.** Read `class340`'s constructor. Three NUL-terminated byte runs
open the record — each a *count* stream paired with a *value* stream immediately
after it — then a note-tree saying which of N envelope sets each run uses, then a
fourth count stream read **twice with independent cursors** (once for sample ids,
once for volumes), then optional volume and pan ramps applied over note ranges.
Nothing is stored per note.

**Consequence.** Mis-stepping a counter still decodes and still plays — just the
wrong instrument on the wrong notes. `test_music.c` therefore asserts the decode
consumes the record *exactly*, over every patch in three caches. That is the only
cheap check that the walk stayed in step, and it passes 187/187 on osrs239.

A related trap: the reference stores per-note volume in a Java `byte` and does
its ramp arithmetic in signed byte, so a value above 127 becomes **negative**.
`RSCache_MusicPatch.volume` is `int8_t` to match; widening it to unsigned would
make a handful of notes twice as loud as the game plays them.

### D7 — the synth's volume domain is `volume << 6`, and clamping it to 255 clips everything

**How measured.** After the first end-to-end render the output had `peak 32768`
and `mean |x| 24579` — saturated. Instrumenting `channel_note_on` showed note
volumes of 370–1160 where the mixer's API domain tops out at 255. The reference
resolves it in `class49.method874`: a 0..255 volume enters a stream as
`volume << 6`, so **unity is 16320**, and `MidiPcmStream.method5890` computes in
that domain directly.

**Consequence.** `TORIRS_PCM_VOLUME_UNITY` is 16320 and the command layer's
0..255 is shifted into it. After the fix the same render peaks at 4772 with a
mean of 1000 — around −17 dBFS peak, with headroom for a full arrangement. This
is also why the extra six bits are kept rather than rounding: a typical note sits
near 1000/16320, and quantising that to 15/255 turns a twelve-voice chord into
audible steps.

### D8 — area sounds are loc-driven and never enter the network protocol

`LocType` carries `ambient_sound_id`, `ambient_sound_distance`,
`ambient_sound_retain`, a min/max tick range and an id list. Nothing tells the
client to start them: it walks the scene it just built. So the fields have to
survive the config adaptor (they were being dropped), the *placed* loc has to be
recorded at build time next to the minimap gathers (only that point has both the
placed loc and its resolved config — and a multiloc's varbit transform can change
which sound it emits), and the audio layer owns one looping voice per emitter
whose gain and pan follow the camera.

A client that only plays `SYNTH_SOUND` is silent in exactly the places the game
is meant to be atmospheric, and nothing in a packet log would tell you why.

### D9 — the scene's hash map lost entries on growth, silently

**How measured.** `leaks` on `rs_audio_test` reported two leaked
`ToriDraw_SoundNew` allocations. Counting adds against the shutdown iteration
showed 103 adds but 101 entries, and instrumenting per id named them: **48 and
96** — precisely the growth thresholds for an initial capacity of 64.

The cause is in the registry's insert shape, which every asset registry shared:

```c
td_scene_prepare_hmap_insert(map);
entry = ToriDraw_MapSearch(map, &id, INSERT);
td_scene_maybe_grow_hmap(map);      /* <- reallocates the slot buffer */
entry->id = id;                     /* <- writes through a dangling pointer */
entry->sound = sound;
```

`td_scene_maybe_grow_hmap` reallocates and frees the old buffer, so the writes
land in freed memory. The copy that survived the rehash keeps the key (INSERT
wrote it) but an *uninitialised* payload — the map's buffer is `malloc`'d, and
INSERT only writes the key. So the next lookup misses, the caller re-adds, and
the first clip leaks. No crash, no assertion: two entries out of a hundred just
quietly are not there.

**Fixed** by never growing after the search (`prepare` already guarantees room),
and by doing a `FIND` before the `INSERT` so a fresh slot's uninitialised
`sound` pointer is never read. The same post-search grow was removed from the
sprite, font, model and animation registries, which had the identical hazard.

A second, unrelated leak remains and is **pre-existing**: `CacheProvider_*Cleanup`
reallocates each hmap after freeing it, and the reallocation at teardown is not
freed. It is not on the audio path and was left alone.

### D11 — the loop span is not the end of playback

**How reported.** "The audio sounds corrupted", while the tests were green.

**How found.** The reference's `RawPcmStream` bounds playback by the loop span
only *while passes remain*; when the count is exhausted it falls out of the loop
branch into a final pass bounded by `samples.length`. So a sound plays: the loop
span N times, and then **the tail out to the end of the clip**. My `advance()`
treated the loop span as the end of playback outright, so any effect whose loop
span ends before its samples do was cut off mid-waveform — a step discontinuity,
which is a click on every play.

**How measured.** A scan of `cache.osrs239`'s sound-effect table: of 11,882
effects that render, 911 carry a loop span and **345 of those end before the clip
does**, discarding 7,588,210 samples — about five and a half minutes of audio,
every one of them ending on a hard edge. `cache.osrs230` is 285 of 813.

Effect 100 is typical: 53,361 samples with a loop span of 22,491..26,901. Before
the fix a single play was 26,901 samples and stopped dead; after it, capturing
the client's own output shows five plays of **53,358** samples each and zero
sample-to-sample jumps above 10,000 across the whole 14-second capture.

The regression test uses a fixture that is *silent inside the loop span and full
scale after it*, so the two behaviours are trivially distinguishable — the
previous fixture looped over the whole clip and could not tell them apart. That
is the actual lesson: a loop test whose loop covers everything tests nothing
about loop bounds.

### D12 — bus gain must not be folded into ramp state

Found while fixing D11. The mixer applied a non-unity bus volume by scaling a
voice's `left_gain`/`right_gain` before a block and restoring them after — but
those fields *are* the ramp: `ToriRS_PcmVoice_Mix` steps them toward a target
every frame, and restoring the saved value threw that progress away. A fade under
a bus below full volume would tick one step per block and be undone, so it never
reached silence and the voice never ended. Bus gain is now a separate multiplier
applied inside the mix, and `test_fade_under_bus` covers it.

### D10 — publish-then-play must hold *within* one tick

**How measured.** `test-sound` failed on the dat1 254 cache and passed on dat2
230: three voices started, all three rejected by the backend. The difference is
the trim. A dat2 effect has a non-zero lead-in, so its queue entry waits a tick
after becoming resident; a dat1 effect with no trim and no server delay is played
on the *same* tick it is published. The scene's `SOUND_LOAD` event was being
drained at the top of the next tick, so `VOICE_START` reached the backend before
`ASSET_LOAD`.

**Consequence.** `publish_sound` drains the scene's sound events immediately
after adding, not at the next tick boundary. The retained model makes this
class of bug loud — the backend counts a rejected voice — where an
immediate-mode API would just have played silence.

---

## Reference class map

Obfuscated names from `/Users/matthewevers/Documents/git_repos/Deobfuscator`.
`src_osrs239_rl1_12_33` is the osrs239 tree: authoritative for field layout but
noisy — methods are duplicated by a transplant pass and littered with opaque
predicates, and `class10.method113` assigns the same field twice from two
different buffer reads. `src_20260701` is an older revision whose decompilation
is clean and whose audio classes are structurally identical. **Read
`src_20260701`, check field layouts against `rl1_12_33`.**

| Role | osrs239 (`rl1_12_33`) | clean (`src_20260701`) |
| --- | --- | --- |
| SoundEnvelope / SoundFilter / SoundTone | `class9` / `class41` / `class45` | — |
| SoundEffect | `class28` | `class40` |
| RawSound | `class17` (`short[]`) | `class41` (`byte[]`) |
| VorbisSample | `class10` | `class55` |
| Vorbis bit reader | `class51` | `class61` |
| Vorbis codebook / floor / residue / mapping | `class8`/`class40`/`class47`/`class16` | `class53`/`class42`/`class47`/`class51` |
| Floor curve state | — | `class60` |
| PcmStream / PcmStreamMixer / RawPcmStream | `class20` / `class21` / `class29` | `class50` / `class39` / `class49` |
| PcmPlayer | `class31` (+ `class477` javax device) | `class43` |
| MidiPcmStream | `class352` / `class356` | `class336` |
| MusicPatchPcmStream / MusicPatchNode | — | `class343` / `class345` |
| MusicPatch / its envelopes | — | `class340` / `class334` |
| MidiFileReader | — | `class337` |
| Song (packed MIDI) | `class355` | `class344` |
| SoundCache | — | `class46` |

---

## Log

Newest last.

### Survey

Started from `src/audio/torirs_audio.h`, `src/game/rs_audio.c` and
`src/platform/platform_audio_*.c`. Sound **effects** were already end to end
(cache → FM synth → 8-bit PCM → SDL2 queue). Music was explicitly stubbed:
`RS_Audio_Song` and `RS_Audio_Jingle` recorded an id and printed "no midi
backend". So the gap was not a bug to fix but three subsystems to build — a
Vorbis decoder, a MIDI sequencer with a cache-driven soundfont, and a positional
area-sound layer — on top of a platform interface that lent raw PCM until the
next drain, which is the opposite of the retained model asked for.

### Reading the formats out of the bytes, not the deob

Rather than guess, built `audioprobe` over the existing `rscache` tool objects
and dumped every audio table of `cache.osrs239`. That produced D1 and D3
directly. It mattered: the osrs239 deob's Vorbis class has transplanted duplicate
methods that re-read the same field twice, and taking it at face value would have
produced a header parser off by four bytes on every sample. Switched to reading
`src_20260701` and checking layouts against the 239 tree.

### Architecture

Kept **synthesis engine-side** and **mixing platform-side**. The alternative —
render everything engine-side and hand the platform one interleaved block per
frame — is simpler but makes the retained asset API pointless and puts a software
mixer in front of any backend that has a better one. So: the scene owns decoded
clips as assets, the backend owns copies and voices, and music is a *stream* the
engine feeds each frame because a four-minute song rendered up front is ~21 MB.

The mixer itself is shared by all three backends rather than reimplemented in
each, which is what makes `make -C src test-audio` possible at all: it drives the
mix directly with no device and inspects the samples.

One consequence worth stating: the game needs to know how far ahead to
synthesise, so `ToriRS_AudioFeedback` is the single piece of information that
flows *back* across the boundary. Everything else is one-way.

### Verifying the synth

The decisive test is not a unit test. `audio_test` loads a real song out of a
real cache — decoding the packed MIDI, the 11 patches its manifest names, and the
23 samples those patches' used notes reference — synthesises four seconds and
looks at the samples. The first run clipped flat (D7). After the volume-domain
fix, a spectrum of the output showed 64.6 / 131.9 / 393.0 / 524.9 Hz: C2, C3, G4,
C5, a C-major voicing. That is the check that everything — Vorbis, patch decode,
sequencer, envelopes, mixer — is connected the right way round, and no assertion
about buffer sizes would have caught the alternative.

### "The audio sounds corrupted"

Reported while every test was green, which is the useful part. Two real bugs
(D11, D12), and the reason the tests missed them was the fixtures: the loop test
looped over the whole clip, so it could not distinguish "stop at loop_end" from
"play to the end", and no test faded a voice under a non-unity bus. Both now have
fixtures shaped so the wrong behaviour is impossible to pass.

It also produced `TORIRS_AUDIO_WAV`. There was no way to answer "does it sound
right" with evidence before that, and the counters cannot: a clipped, truncated,
clicking mix and a clean one have identical statistics at the command level.

### Two bugs the retained model exposed

D9 and D10 are both bugs that an immediate-mode API would have hidden. The
dangling-pointer map bug (D9) had been latent in every asset registry in the
scene; it only surfaced because sounds are the first registry that routinely
crosses a growth threshold in a short session, and because the leak checker had
something to point at. The publish-then-play ordering bug (D10) surfaced because
the backend can *count* a voice that named an asset it does not hold — an
interface that lent a buffer per play has no way to notice.

### What is not done

Listed in the TODO above and worth repeating here: `MIDI_SONG_WITHSECONDARY` and
`MIDI_SONG_STOP` are present in the osrs239 packet table but mapped to
`PKT_NAME_NONE`, so a server sending them changes nothing; NPC ambient sounds
decode but nothing consumes them; CS2 has no host opcodes for sound; and the
random-set area emitters cycle their re-trigger gap deterministically rather than
drawing it, so a headless run stays reproducible.
