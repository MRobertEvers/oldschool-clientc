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
- **Materials port too.** A dat2 texture names a sprite, and that sprite is
  already a palette plus a byte-per-pixel index buffer — the same shape rev 254
  stores a texture in. So `--texture ID[=name]` (and any model face or floor
  that references one) writes `content/textures/<name>.png` and a
  `texture.pack` line, and the face keeps its texture instead of collapsing to
  a flat colour. What the destination imposes:
  - the canvas must be **128x128 or 64x64** — every osrs239 material already is;
  - at most **128 palette entries, index 0 transparent**, because Pix8 reads its
    pixel indices through an `Int8Array` and anything past 127 comes back
    negative. Wider palettes are reduced by weighted nearest-pair merging, which
    only 20 of osrs239's 210 materials need;
  - **magenta and black are reserved** — LostCity's PNG packer takes `0xFF00FF`
    as the transparent entry, and the renderer skips any texel that is zero
    after `& 0xf8f8ff`. A source colour landing on either is nudged one step.

  **`--max-textures` decides how many actually land, and it defaults to 50** —
  what a stock rev-254 Pix3D unpacks. Ids are appended at `pack->max`, so a
  destination whose `texture.pack` already lists 0..49 has no room and every
  material is refused at that default; the ceiling is a property of the client,
  not the format (the widest field carrying a texture id is the flo config's
  one-byte `texture` opcode). Raise it only against a client whose table is
  wider, because an id past what the client unpacks is written and never read —
  the face renders untextured *and* unlit rather than falling back to a colour.
  `--no-textures` is the same as `--max-textures 0`.

  **A refusal is not a failure.** Whatever does not fit flattens to that
  texture's own `average_hsl`, straight out of the source record — the same
  colour an unported face has always taken, and measurably the mean of the
  material's own pixels. The refusal is memoised per texture id, so the warning
  appears once rather than once per face.

  Faces still flatten when the source uses **texture render types 1-3** —
  cube, cylindrical and scrolling mappings, whose payloads OB2 has no section
  for — and when a model needs more than the **64 texture triangles** the
  per-face index can address, which is measured after compacting the list to
  the triangles that surviving faces actually use.
- **Floors are ported, not matched.** A `.flo` record is little more than a
  colour and a texture, and LostCity reads floors by name, so new entries
  carrying the source values cost the same as a nearest-colour table. Two
  things bite here:
  - A map tile stores its floor as *config id + 1*, 0 meaning none — both
    reference clients do the `-1` on resolve, so the exporter has to as well.
    Skipping it exports every tile with its neighbour's colour: still a valid
    id, still a plausible map, just uniformly one record off, which is how a
    lava arena came out grass green the first time.
  - `0xFF00FF` is the "I have no colour of my own" sentinel, and it must be
    written through **verbatim**. Rev 254 implements it in the same three-way
    order OSRS does — texture, then magenta, then colour — and its magenta
    branch resolves the overlay to the `12345678` skip-this-face value. Omitting
    `colour=` instead leaves the record at rgb 0, which is not the sentinel but
    the colour *black*, so the tile takes the third branch and paints a black
    quad over the hole the reference leaves. That is what put black bands
    between the Inferno's lava tiles.
- **Loc models are named by shape.** LostCity recovers a loc model's shape from
  the file name suffix, so each (shape, model) pair becomes its own `modelN`
  entry; five is the limit and the rest are dropped with a warning.
- **Records that do not decode byte-exactly are refused**, per record rather
  than per revision, since the decoder's short consume is its own signal that it
  stopped on an opcode it does not know.

Assets whose gameplay fields LostCity needs (npc combat stats, hunt modes,
params) are *not* emitted — those are authored beside the generated config and
have to be merged back after a re-run.

**A partial re-run rewrites whole config files.** Idempotence is per *id*, not
per file: each `.npc` / `.seq` / `.loc` / `.flo` is written from what that
invocation accumulated, so re-running with only `--map X_Z` to refresh a floor
replaces the `.seq` with just the sequences that square's locs pulled in and
drops every animation an earlier `--npc` run had put there. Re-run with the
whole original argument list, or restore the files the run should not have
touched.
