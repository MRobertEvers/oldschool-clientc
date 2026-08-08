# AUDIO_SYSTEM_OPUS

The audio subsystem of the C client: cache decoding, synthesis, the retained
platform API, and the game-side triggers that decide what is heard.

Audio is built the same way rendering is. The game never touches a device: it
puts **commands** on a queue and the host drains that queue once per frame and
hands each command to a backend. Assets (sound clips) live in the
`ToriDraw_Scene` asset registry beside models, sprites and fonts, are loaded and
unloaded through scene events, and are referenced from play commands **by
handle** — a retained API, not a per-play buffer loan. That shape is not a
stylistic choice: playback in a browser may only start inside a user gesture and
the WebAssembly heap moves under you, so "the host owns the bytes, the game owns
the decision" is the only portable split.

Three sources of sound exist in the cache and all three are implemented here:

| Source | Cache index | Shape |
| --- | --- | --- |
| Sound effects | 4 (`SOUNDEFFECTS`) | FM-synth programs, and (modern OSRS) Vorbis samples |
| Music tracks | 6 (`MUSIC_TRACKS`) | column-packed MIDI |
| Music jingles | 11 (`MUSIC_JINGLES`) | column-packed MIDI |
| Music samples | 14 (`MUSIC_SAMPLES`) | Vorbis, with a shared setup header in archive 0 |
| Music patches | 15 (`MUSIC_PATCHES`) | soundfont instrument definitions |

---

## TODO

Status as of the current session. `[x]` = implemented **and** exercised by a
test or a harness run; `[~]` = implemented, not yet verified; `[ ]` = not
started.

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

Measured: 580/580 music samples, 187/187 patches (all consumed **exactly**), 881/881
tracks and 315/315 jingles on `cache.osrs239`; every emitted track re-parses to its
declared length.

### Phase 2 — retained asset API
- [ ] `ToriDraw_Scene` sound asset registry (`ToriDraw_SceneSoundAdd/Get/Has/Remove`)
- [ ] `TORIDRAW_EVENT_SOUND_LOAD` / `TORIDRAW_EVENT_SOUND_UNLOAD`
- [ ] `ToriRS_AudioCommand` rewritten as a retained command set (asset load/unload,
      voice play/stop/update, stream open/push/close, bus volume)
- [ ] `ToriRS_AudioFrame` — the audio analogue of `ToriRS_Frame`: drains scene
      events + game intents into a command stream
- [ ] Backends: SDL2 (mixing, voices, streams), null (recording), wasm

### Phase 3 — synthesis engine (`src/audio`)
- [ ] `torirs_pcm.{c,h}` — PcmStream graph: base, mixer, raw stream (resample,
      loop modes, ramped volume/pan)
- [ ] `torirs_midi_file.{c,h}` — MidiFileReader (track cursors, running status, tempo)
- [ ] `torirs_midi_synth.{c,h}` — MidiPcmStream: 16 channels, patch nodes, envelopes,
      vibrato, portamento, sustain/legato, note-off release
- [ ] `torirs_soundbank.{c,h}` — patch + sample cache with explicit lifetimes

### Phase 4 — game integration
- [ ] Music: `MIDI_SONG`, `MIDI_SONG_V2`, `MIDI_SONG_WITHSECONDARY`, `MIDI_SONG_STOP`
- [ ] Jingles: `MIDI_JINGLE` with resume-to-song on end
- [ ] Fade in / fade out / crossfade envelopes on the music bus
- [ ] Area sound effects from loc `ambient_sound_*` fields (scene-driven, positional)
- [ ] Area sound effects from npc ambient sound fields
- [ ] Sequence frame sounds, positional and distance-attenuated
- [ ] `SYNTH_SOUND` combat/interaction effects with the reference overlap rule
- [ ] Volume settings: effects / music / area buses via varps and varbits
- [ ] CS2 host opcodes for sound (`SOUND_SYNTH`, `SOUND_SONG`, `SOUND_JINGLE`)

### Phase 5 — verification
- [ ] `make -C src test-audio` — engine-side unit tests (no device)
- [ ] Leak audit: every asset load has a matching unload at shutdown
- [ ] Byte-compare a rendered song against the reference renderer

---

## Procedures

Short, repeatable things worth not re-deriving.

### Build and run the audio tests
```sh
make -C src test-audio          # engine + mixer + midi, null backend
make -C 3rd/rscache test        # includes test_vorbis / test_music
```

### Hear a specific sound effect
```sh
TORIRS_SIM_SOUND=<id>[,loops[,every_frames]] TORIRS_AUDIO_DEBUG=1 \
  ./src/build/torirs --manifest manifest_osrs239.ini
```

### Play a specific music track / jingle without a server
```sh
TORIRS_SIM_SONG=<id>   TORIRS_AUDIO_DEBUG=1 ./src/build/torirs --manifest manifest_osrs239.ini
TORIRS_SIM_JINGLE=<id> TORIRS_AUDIO_DEBUG=1 ./src/build/torirs --manifest manifest_osrs239.ini
```

### Dump a song to a `.mid` you can open in any sequencer
```sh
./3rd/rscache/tools/build/audioprobe cache.osrs239 osrs239 --song <id> --out /tmp/song.mid
```

### Dump a decoded sample to a `.wav`
```sh
./3rd/rscache/tools/build/audioprobe cache.osrs239 osrs239 --sample <id> --out /tmp/s.wav
./3rd/rscache/tools/build/audioprobe cache.osrs239 osrs239 --effect <id> --out /tmp/e.wav
```

### Trace asset lifetimes (find a leak)
`TORIRS_AUDIO_TRACE=1` logs every `LOAD`/`UNLOAD`/`PLAY`/`STOP` with the asset
handle and the backend's live-asset count. At shutdown the backend asserts its
asset table is empty; a non-zero count names every leaked handle.

### Survey the raw bytes of an audio table
```sh
./3rd/rscache/tools/build/audioprobe cache.osrs239 osrs239           # survey
./3rd/rscache/tools/build/audioprobe cache.osrs239 osrs239 14 1      # hexdump one archive
```

---

## Major discoveries

Each entry says how it was measured, not just what is true.

### D1 — index 14 archive 0 is the global Vorbis setup header

**How measured.** `audioprobe` hexdumps of `cache.osrs239` table 14. Archive 0
begins `aa 22 42 43 56 01 00 08 00 00 80 …`; archives 1..581 begin
`00 00 56 22 00 00 73 d4 …`. `56 22` is 22050 read as a big-endian `u16`, and
`42 43 56` is the Vorbis **codebook sync pattern** `0x564342` emitted LSB-first
by a bit-packed stream. Decoding archive 0 as a Vorbis setup header with the
reference's reader reproduces the whole structure: first two nibbles of `0xaa`
are `blocksize0 = blocksize1 = 1 << 10`, the next byte `0x22` is
`codebookCount - 1 = 34`, and codebook 0 then starts at its sync pattern.

**Why it matters.** There is exactly *one* setup header for every music sample
in the cache, and the samples carry only audio packets. A per-sample decoder
that expects a self-contained Vorbis stream finds no codebooks and produces
silence. The setup is decoded once and shared.

The sound-effects table (index 4) is different: when an effect has a second
group file, that file is `u4 setupLength`, the setup header, then the sample.
Each of those is self-contained, so it carries its own setup and must not
clobber the music one — which is why the setup is an explicit object here and a
process-wide static in the reference.

### D2 — the modern sound-effect record is 16-bit, and the old renderer was 8-bit

**How measured.** `class17` (RawSound) in the osrs239 deob holds `short[]`, and
`class28.method279` clamps to `-32768..32767`; the same class has an 8-bit
branch (`>> 8`, clamp `-128..127`) selected by a boolean, which is the
low-memory mode. The pre-existing `sound_render.c` in this repo renders to
8-bit unsigned because that is what the RS2-era client did.

**Consequence.** The mixer here is 16-bit signed internally, and the RS2-era
8-bit renderer's output is widened on the way in rather than the modern path
being narrowed. Nothing in the corpus loses precision.

### D3 — the sample header is four big-endian `u4`s and a packet count

**How measured.** Decoding table 14 archive 1's first 20 bytes as `u4`s gives
`22050, 29652, 23323, 29652, 59`. 22050 is the rate; 29652 samples at 22050 is
1.34 s, consistent with the archive's 4122 compressed bytes; 23323 < 29652 is a
loop point inside the sample. The reference (`class55.method1054`) reads exactly
four values then a count, and negative `end` means "loop" with `end = ~end`.
The 59 packets that follow are length-coded by 255-lacing, the same way Ogg
laces a page.

### D4 — a song is a **column-oriented** MIDI, not a stream

**How measured.** `class344`'s constructor reads the track count and division
from the *last three bytes* of the archive, then makes a first pass counting
events by kind, then computes a table of per-kind byte-stream offsets, then
re-reads and re-emits a standard `MThd`/`MTrk` file. Every field of every event
kind lives in its own contiguous run: all note-on note numbers together, all
velocities together, all controller values together, and so on, each delta-coded
against the previous value of that field. That is why the archives compress so
well and why a naive "parse events in order" reader gets garbage.

**Consequence.** The unpacker must be a faithful two-pass port; there is no
shortcut. It emits a real SMF, so `--song <id> --out x.mid` produces a file any
sequencer opens — which is also the cheapest way to check the port.

### D5 — the song carries its own instrument manifest

`class344` also builds a map from *banked program number* to the set of note
numbers that program ever plays (`field3701`, a `BitSet` per patch). The client
uses it to load only the samples a song actually needs, before starting
playback. Loading all 128 notes of every patch a song references costs several
times more memory and stalls the first bar.

### D6 — MusicPatch is run-length + delta coded against five side streams

**How measured.** Read `class340`'s constructor. It opens with three
NUL-terminated byte runs (three separate RLE count streams), then a note-tree
that reconstructs which of N distinct envelope sets each note uses, then a
fourth and fifth stream for volume/pan ramps. Each of the 128 notes gets its
sample id, pitch offset, volume, pan and envelope by walking the RLE counters.
Nothing is stored per-note directly.

**Consequence.** The decoder is order-sensitive in a way that fails silently:
mis-stepping a counter yields a patch that decodes without error and plays the
wrong instrument for the wrong notes. `test_music.c` therefore asserts the
buffer is consumed exactly, over every patch in the cache.

### D7 — the reference's effect overlap rule is a client-side monophony rule

The 2004-era client refuses to interrupt a longer sound with a shorter one, and
the SDL2 backend inherited "drop if anything is playing". That is correct for
the era but wrong for modern OSRS, which mixes freely. The rule now lives in the
game layer only, gated on the era feature table, and the backend always mixes.

### D8 — area sounds are loc-driven and never enter the network protocol

`LocType` carries `ambient_sound_id`, `ambient_sound_distance`,
`ambient_sound_retain`, a min/max tick range and an id list; the client walks
the built scene, finds every loc with a sound, and owns a looping voice per
*tile group* whose volume follows the camera. Nothing tells the client to start
them. A client that only plays `SYNTH_SOUND` is silent in exactly the places
the game is meant to be atmospheric.

---

## Reference class map

Obfuscated names from `/Users/matthewevers/Documents/git_repos/Deobfuscator`.
`rl1_12_33` is the osrs239 tree (authoritative for field layout but noisy —
methods are duplicated by a transplant pass and littered with opaque
predicates); `src_20260701` is an older revision whose decompilation is clean
and whose audio classes are structurally identical, so it is the one to *read*
and the 239 tree is the one to *check against*.

| Role | osrs239 (`src_osrs239_rl1_12_33`) | clean (`src_20260701`) |
| --- | --- | --- |
| SoundEnvelope | `class9` | — |
| SoundFilter | `class41` | — |
| SoundTone / Instrument | `class45` | — |
| SoundEffect | `class28` | `class40` |
| RawSound | `class17` (`short[]`) | `class41` (`byte[]`) |
| VorbisSample | `class10` | `class55` |
| Vorbis bit reader | `class51` | `class61` |
| Vorbis codebook / floor / residue / mapping | `class8`/`class40`/`class47`/`class16` | `class53`/`class42`/`class47`/`class51` |
| PcmStream | `class20` | `class50` |
| PcmStreamMixer | `class21` | `class39` |
| RawPcmStream | `class29` | `class49` |
| PcmPlayer | `class31` (+`class477` javax backend) | `class43` |
| MidiPcmStream | `class352`/`class356` | `class336` |
| MusicPatchPcmStream | — | `class343` |
| MusicPatchNode | — | `class345` |
| MusicPatch | — | `class340` |
| MusicPatchInfo (envelopes) | — | `class334` |
| MidiFileReader | — | `class337` |
| Song (packed MIDI) | `class355` | `class344` |
| SoundCache | — | `class46` |

---

## Log

Newest last.

### Session 1 — survey and architecture

Started from `src/audio/torirs_audio.h`, `src/game/rs_audio.c`,
`src/platform/platform_audio_*.c` and `docs`/memory: sound **effects** were
already end-to-end (cache → FM synth → 8-bit PCM → SDL2 queue), and music was
explicitly stubbed — `RS_Audio_Song` and `RS_Audio_Jingle` only record an id and
print "no midi backend". So the gap was not a bug to fix but three subsystems to
build: a Vorbis decoder, a MIDI sequencer with a cache-driven soundfont, and a
positional area-sound layer. On top of that the platform interface lent raw PCM
until the next drain, which is the opposite of the retained model asked for.

Rather than guess at the formats I built `audioprobe`, a scratch tool over the
existing `rscache` tool objects, and dumped every audio table of
`cache.osrs239`. That is what produced D1 and D3 — the `42 43 56` at offset 2 of
table 14 archive 0 is the Vorbis codebook sync pattern, and it is in exactly one
archive, so the setup header is shared. Reading it out of the bytes rather than
out of the deob mattered, because the osrs239 deob's Vorbis class has
transplanted duplicate methods that re-read the same field twice; taking it at
face value would have produced a header parser off by four bytes on every
sample.

Chose to read the reference from `src_20260701` and check field layouts against
`src_osrs239_rl1_12_33`, after finding the latter's `class10.method113` assigns
`field54` twice from two different buffer reads — an artifact, not the program.

Architecture decision: keep **synthesis engine-side** and **mixing
platform-side**. The alternative — render everything engine-side and hand the
platform one interleaved block per frame — is simpler but makes the retained
asset API pointless and puts a software mixer in the way of any backend that has
a good one. So: scene owns decoded clips (as assets), the backend owns copies
and voices, and music is a *stream* asset the engine feeds each frame because a
four-minute song rendered up front is ~21 MB of PCM.
