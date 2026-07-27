# rscache tools

Offline utilities that sit on top of `3rd/rscache` for discovering and porting
assets between cache revisions.

## Build

```sh
make -C 3rd/rscache tools
# binaries land in 3rd/rscache/tools/find_anims/find_anims
#                 and 3rd/rscache/tools/port_npc/port_npc
```

Or from this directory:

```sh
make
```

## `find_anims`

Identify every animation that might apply to an NPC's models by walking from the
NPC's base animation set (opcodes 13/14 and, on RS2, BasType via opcode 127) to
the framemap shared by those sequences, then collecting every other sequence
that references the same framemap.

```sh
# dat2
./find_anims/find_anims --rev osrs230 ../../cache.osrs230 --npc 1
./find_anims/find_anims --rev rs643 ../../cache.rs643 --npc 7343 --strict --json

# dat1
./find_anims/find_anims --rev lc254 ../../cache254 --npc 1
```

Flags: `--npc ID`, `--model ID`, `--seq ID` (exactly one), `--strict` (drop
candidates whose framemap does not cover the model's bone labels), `--json`.

## `port_npc`

Port an NPC and its asset closure (models, sequences, frames, framemaps, and
optionally related animations / BasType) between cache revisions. Dry-run by
default; pass `--apply` to write.

```sh
./port_npc/port_npc \
  --from-rev osrs230 ../../cache.osrs230 \
  --to-rev rs643 ../../cache.rs643 \
  --npc 1 --out /tmp/cache.out --apply
```

Ids are kept when free in the destination and remapped on collision. See the
tool's `--help` for `--include-related-anims`, `--emit-bas`, `--strict-models`,
and `--texture-map`.

The port is refused when the source NPC record does not decode byte-exactly,
which the decoder reports by consuming less than the whole record. This is a
per-record check: revisions with known decode gaps (osrs239 among them, see
`3rd/rscache/README.md`) still hold records that decode exactly, and those port
fine. `RSCACHE_NPC_DEBUG=1` names the opcode that stopped a refused record.

## `port_lostcity`

Export dat2 cache assets as LostCity **source files** rather than as a binary
destination cache. Where `port_npc` writes archives into another cache, this
writes what a LostCity build takes as input — `.ob2` models, `.anim` animset
archives, text `.npc` / `.seq` / `.spotanim` / `.loc` / `.flo` configs, a text
`.jm2` map square, and the `content/pack/*.pack` id lines that make any of it
exist.

```sh
make -C 3rd/rscache/tools port_lostcity

./port_lostcity/port_lostcity \
  --rev osrs239 ../../cache.osrs239 \
  --content /path/to/LostCity_Server/content \
  --area areas/area_inferno --prefix inferno \
  --npc 7706=zuk --seq 7566=zuk_attack --spotanim 1375=zuk_proj \
  --loc 30356 --map 35_83 --apply
```

Dry run by default; `--apply` writes. It writes into the server's content tree
directly rather than staging elsewhere, because the pack files are the id
authority — staging would let the ids the emitted configs were written against
drift from the ids the build reads.

Every exporter is idempotent through `lc_pack_alloc`: a name already listed
keeps its id, so a re-run adds only what is new and the closure can be walked
naively (an npc asks for its sequences, a sequence for its frames).

Notes on what does and does not carry over:

- **Sequences requested by name go first.** An npc pulls in its own idle and
  walk sequences under generated names, and whichever exporter asks first
  decides the name — so `--seq 2863=..._defend` must be honoured before the npc
  that also uses 2863 as its walk.
- **Frames are grouped into animsets by framemap**, capped at
  `LC_ANIMSET_MAX_FRAMES` per archive because the dat1 anim trailer stores its
  section lengths as u16. The AnimBase is repeated in each part, which is what
  makes the split invisible: the client registers frames by the id embedded in
  the head, not by archive.
- **Models export untextured.** OB2 can carry textures, but its ids index the
  destination cache's own table and there is no shared numbering with OSRS
  materials, so a textured face is flattened to that texture's average colour
  (which the dat2 texture record stores outright).
- **Floors are ported, not matched.** A `.flo` record is little more than a
  colour and LostCity reads floors by name, so new entries carrying the source
  colours cost the same as a nearest-colour table and lose nothing but the
  texture. Note that a map tile stores its floor as *config id + 1*, 0 meaning
  none — both reference clients do the `-1` on resolve, so the exporter has to
  as well. Skipping it exports every tile with its neighbour's colour: still a
  valid id, still a plausible map, just uniformly one record off, which is how
  a lava arena came out grass green the first time.
- **Loc models are named by shape.** LostCity recovers a loc model's shape from
  the file name suffix, so each (shape, model) pair becomes its own `modelN`
  entry; five is the limit and the rest are dropped with a warning.
- **Records that do not decode byte-exactly are refused**, per record rather
  than per revision, since the decoder's short consume is its own signal that it
  stopped on an opcode it does not know.

Assets whose gameplay fields LostCity needs (npc combat stats, hunt modes,
params) are *not* emitted — those are authored beside the generated config and
have to be merged back after a re-run.
