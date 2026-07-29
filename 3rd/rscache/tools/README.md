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

## `find_named`

Locate records by **name**, and dump what they reference. Every other tool here
takes an id and works outward; that is the wrong end when the only thing you
know about an asset is what the wiki calls it.

```sh
./find_named/find_named --rev osrs230 cache.osrs230 --name "tormented"
./find_named/find_named --rev osrs230 cache.osrs230 --name undead_demon
./find_named/find_named --rev osrs239 cache.osrs239 --name "dragon claws" --type obj
```

`--type` narrows to `npc`, `obj`, `seq`, `spotanim` or `all` (default).

Sequences and spotanims carry the content team's own debug names in these
revisions — `luc2_undead_demon_melee`, `zuk_attack` — and that name is the only
handle on an asset nothing points at. An npc record names its idle and its walk
and nothing else, so attack, death and spawn animations cannot be reached by
walking ids; `find_anims` finds them by framemap, this finds them by name, and
projectiles usually need this one because they share no framemap with anything.

Note that `--name` over `obj` or `spotanim` walks every record in the group and
is slow (minutes on a full OSRS cache) — it reloads the archive per id. Dumping
a single record is instant:

```sh
./find_named/find_named --rev osrs230 cache.osrs230 --npc 13599
./find_named/find_named --rev osrs239 cache.osrs239 --obj 13652
./find_named/find_named --rev osrs230 cache.osrs230 --seq 11392
./find_named/find_named --rev osrs230 cache.osrs230 --spotanim 2853
./find_named/find_named --rev osrs230 cache.osrs230 --scan-spotanim-model 50027
```

Rig inspection, for porting animations across eras — a framemap is the rig an
animation is authored against, and the two dumps below are what show whether two
eras number their joints the same way (they do not; see `RIGGING_OSRS_RS2.md`):

```sh
./find_named/find_named --rev osrs239 cache.osrs239 --framemap 0     # dat2 rig
./find_named/find_named --dat1-anim <content>/models/anim_80.anim    # dat1 rig
./find_named/find_named --rev osrs239 cache.osrs239 --idk-centroids  # joint positions
```

`--idk-centroids` walks the identikit body models and prints each label's vertex
centroid, which is how the joint correspondence between two rigs is derived.

Sequence dumps report duration in **both** client cycles and server ticks, which
is the number a script actually needs — 30 client cycles is one server tick.

## `anim_compare`

Play one animation on two rigs, side by side, frame by frame — the loop for
refining a cross-era joint correspondence.

```sh
./anim_compare/anim_compare \
  --a-rev osrs239 --a-cache ../../cache.osrs239 --a-seq 7514 \
  --b-models <content>/models/human/man \
  --b-anim   <content>/models/dclaws/dclaws_animset_0.anim \
  --out /tmp/cmp --sheet --by-label
```

Flags: `--frames LO-HI`, `--size WxH`, `--yaw N` (0..2047), `--scale N`,
`--by-label` (colour by joint rather than material — the mode that finds rig
bugs), `--sheet` (contact sheet of the whole run), `--report` (which transforms
the animation drives, and their live destination labels).

`--a-model ID` with `--b-model FILE.ob2` poses a single model instead of the
player body, for animations that are not player animations — a spotanim rigged
to its own framemap, say. When both sides share a model and a rig the two panels
should come out pixel-identical, which turns the comparison into a pass/fail.

Its animation kernel is a line-for-line port of the client's `Model.animate2`,
including the ORIGIN fallback that causes stretching, so what it draws is what
the client would draw. See `RIGGING_OSRS_RS2.md` at the repo root.

## `poser-gl`

An animation editor: load a cache, pick an entity and a sequence, watch it play,
drag its joints, and write the result back.

```sh
make -C 3rd/rscache/tools poser-gl

./poser-gl-c/poser-gl --rev osrs230 ../../cache.osrs230
```

A C port of [fglass/poser-gl](https://github.com/fglass/poser-gl), reading
through rscache where the original reads through a per-revision JAR plugin, and
drawing through SDL2 and OpenGL 3.3 where the original uses LWJGL and LEGUI. It
exports poser-gl's own `.pgl` files byte-for-byte, so the two editors interchange.

Not part of `all`: it is the only tool here that needs SDL2, and a machine
without it should still build the rest.

Every other tool in this directory answers a question about a cache; this one is
the only one that lets you *author* into it. `--self-test`, `--shot` and the rest
of its harness flags are documented in `poser-gl-c/README.md`, along with the two
places it deliberately departs from the reference's behaviour.

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

`--overlay-backing N` (manifest key `overlay_backing`) writes source underlay
`N` under every tile that carries an overlay but no underlay. Modern OSRS data
uses overlay-only tiles freely; the 2004 client draws the backing half of a
shaped overlay tile from the tile's own underlay and renders black when there
is none, so a faithful export of such an area looks moth-eaten in the
reference client. Pick the area's ordinary ground underlay; blending does the
rest. Tiles with neither overlay nor underlay are left void on purpose.

`--maploc X_Z,level,x,z,locid,shape,angle` (manifest section `[export:maploc]`,
lines written `place = 35_83,1,28,52,30346,10,3`) appends a static placement to
the square's `.jm2` after the source ones. For arena dressing that is not in any
square's static data — content the original places with dynamic spawns on a
plane the player is not standing on, which a rev-254 zone update cannot express.
The loc is exported like any `--loc`, and a failed export drops the placement
rather than writing an id that names nothing.

`--obj ID[=name]` (manifest section `[export:obj]`) exports an item: the `.obj`
config, the model the inventory icon is rendered from, and the male and female
wield models. LostCity resolves `manwear`/`womanwear` by model *name*, so there
is no naming convention to satisfy — whatever the model exporter called them is
what the config references.

### Rigged models need their labels remapped

A rigged model does not name the joints it bends at. Each vertex carries a
one-byte **label**, and an animation's framemap says which labels each transform
moves. Both halves are era-specific, and they do not agree across a port:

```
rev 254 player rig     labels 0..72          (measured over 236 stock equipment models)
OSRS framemap 0        245 transforms, labels up to 217
```

So a wield model ported straight across is tagged for a skeleton the destination
client does not have. Two symptoms, both silent:

- **Equipment that never moves.** A label the destination rig never addresses is
  skipped — `label < labelVertices.length` in `Model.animate2` — so those
  vertices sit still while the arm they belong to swings.
- **A model that stretches.** The ORIGIN transform averages the position of its
  labelled vertices to find a pivot. Match nothing and it falls back to using
  the raw frame value as an absolute pivot, so the following rotate spins the
  mesh around a point in space instead of a joint.

`--label-map FROM=TO`, or an `[export:label_map]` manifest section, retags the
model on the way out. It applies only to models merged into the *player* — a
spotanim's model carries labels too, but they are rigged to the spotanim's own
framemap, which ports alongside it and stays self-consistent.

Work the correspondence out from geometry rather than guessing. Per-label
centroids of a native model beside the ported one line the joints up:

```
rune claws  (native)   label  16   x = +34.5      dragon claws (OSRS)  label  50   x = -35.3
                       label  17   x = -34.5                           label 161   x = +35.3
```

Same sides, same heights, so `161 = 16` and `50 = 17`.

**Animations need the other map.** `--label-map` fixes a *model*; an animation
needs `[export:rig_map]`, which renumbers every exported framemap's label sets
from the source rig into the destination's. The frame data is untouched — frames
address transforms by index — so only the labels move.

Derive that correspondence by matching per-label vertex centroids of each era's
identikit body models (`find_named --idk-centroids` for the dat2 side). Park
every source label with no counterpart on a number the destination never uses:
leaving them is worse than dropping them, because OSRS label 50 is not rev-254
joint 50 and identity quietly bends a thigh with a cape transform.

The full method, the measured OSRS -> rev-254 joint table, and the verification
procedure are in `RIGGING_OSRS_RS2.md` at the repo root.

What it deliberately does **not** derive is the combat block: bonuses live in the
source item's params table against param ids this revision does not share, and
`category` is an id into a table that is not ported either, so writing them
through would emit lines naming nothing.

Put those in the manifest, **not** in the emitted config:

```ini
[extra:dragon_claws]
category = weapon_claws
param = attackrate,4
param = specwep,^true
param = sa_energy,500
```

Every line of an `[extra:<config name>]` section is appended verbatim to that
config's generated block. This is not a style preference. **The exporter owns
the file it writes** — append a combat block to the `.obj` by hand, re-run the
export, and the whole block is gone with no error. Losing `category` alone is
enough to break the combat tab outright: `switch_category` falls through to
`case default`, which installs the *unarmed* interface, so the tab shows
Punch/Kick/Block under the correct weapon name and the spec bar disappears.
Keeping the lines in the manifest makes a re-export reproduce the file byte for
byte instead of destroying it.

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

---

## `cachepack` — unpack an OldSchool cache into source, and pack it back

```sh
make -C 3rd/rscache/tools cachepack

cachepack unpack --cache DIR --rev NAME --src SRCDIR [--types a,b] [--binary[=1,2]]
cachepack pack   --src SRCDIR --out DIR [--base DIR] [--rev NAME] [--types a,b] [--binary]
cachepack verify --cache DIR --rev NAME --src SRCDIR [--types a,b]
cachepack --list
```

Every other tool here answers a question about a cache or moves one asset between
two. This one turns a whole cache's configs into editable text and turns that text
back into a cache.

```sh
cachepack unpack --cache cache.osrs230 --rev osrs230 --src /tmp/src230
# -> /tmp/src230/pack/npc.pack      3028=goblin
#    /tmp/src230/configs/all.npc    [goblin] name=Goblin  model1=24458 ...
#    /tmp/src230/meta.ini           the identity, so `pack` needs no --rev

$EDITOR /tmp/src230/configs/all.npc

cachepack pack --src /tmp/src230 --base cache.osrs230 --out /tmp/out230
```

### The architecture is LostCity's

Deliberately, because it is a shape that works: `<type>.pack` files are the id
authority, `[name]` blocks of `key=value` lines are the record format, and one
module per type maps between a record and its text. The mapping is:

| LostCity_Server | here |
|---|---|
| `engine/tools/unpack/config/Unpack.ts` | `cp_unpack.c` |
| `engine/tools/pack/config/PackShared.ts` | `cp_pack.c` + `cp_text.c` |
| `engine/tools/pack/PackFileBase.ts` | `port_lostcity/lc_pack.c`, reused |
| `engine/tools/{un,}pack/config/NpcConfig.ts` | `config/cp_npc.c` |

Two things depart from the reference.

**The opcode readers and writers are the library's, not this tool's.** LostCity
hand-writes three mirrors of every record layout — an unpacker, a validator and a
packer — and they can drift. Here both directions pass through rscache's decoder
and encoder structs, the same ones the client uses. What is left per type is only
the mapping between a struct and its text, which is half the code and none of the
opportunity for the two directions to disagree about a field's width.

**Names come from the cache.** LostCity has no name table, so it invents
`npc_1234`. An OldSchool cache ships **table 24, the gameval index** — the content
team's own names — and `unpack` seeds the pack files from it, so records come out
as `[goblin]` and a reference reads `readyanim=slice_surface_goblin_squat_ready`.
Ten of the twenty types are covered (obj, npc, inv, varp, varbit, loc, seq,
spotanim, dbrow, dbtable); the rest fall back to `<type>_<id>`.

The archive-id-to-type mapping is not recorded anywhere in the cache, so it is
**verified rather than trusted**: an archive is accepted only when at least 90% of
the file ids it names are ids the config group actually holds. A mismatch is
refused with a message and that type keeps synthetic names.

### What round-trips, and what does not

`verify` measures it, on the cache in front of you rather than from this table:

```
type         records      exact same-len   differ   codec-ex lost-here
npc            14205         94     7925     6186         94        0
obj            30891      13967    14200     2724      13967        0
loc            56376        508    29159    26709        508        0
varbit         17426      17426        0        0      17426        0
...
```

`codec-ex` is what the library's own decode→encode manages on the same records with
no text in between, and **`lost-here` counts records it reproduced byte-exactly and
the text did not**. That last column is the one this tool owns and it should be
zero; everything else is the library's own fidelity, already measured and recorded
in `EXCEPTIONS.md` B2/B3. A non-zero `lost-here` fails the run.

Measured on `cache.osrs230` (157,253 records over 18 encodable types) and
`cache.osrs239`: `lost-here` is **0 everywhere**, and `exact` equals `codec-ex` for
every type. The two bugs that column was built to find — an inverted default on the
overlay's `hideunderlay` and on the param's `autodisable`, both flags whose *absence*
means true — were 188 and 122 records wide against codec baselines of 4 and 294, and
would have read as "this type is a bit lossy" without it.

Three known limits, all reported rather than silent:

- **`dbrow` and `dbtable` are unpack-only.** rscache decodes them and has no
  encoder, so `pack` skips them and the base cache's records stay as they were.
  The text is still worth having — it is the only readable view of what a client
  database table declares.
- **`mapelement` is 0% byte-exact by construction.** Its decoder keeps four of two
  dozen fields (see `dat2_config_mapelement.h`), so a repack is a readable map
  element rather than a copy of the source.
- **`loc`, `npc`, `obj`, `seq`, `enum`, `spotanim` are lossy** for the reasons
  EXCEPTIONS.md B2 lists — fields consumed without being stored, aliased opcodes
  collapsed to their lowest spelling, ascending opcode order rather than Jagex's.
  A repack is a valid record the client reads identically; it is not the same bytes.

`cachepack --list` prints the register with each type's config group, gameval
archive and flags.

### The end-to-end check

```sh
cachepack unpack --cache cache.osrs230 --rev osrs230 --src A
cachepack pack   --src A --base cache.osrs230 --out OUT
cachepack unpack --cache OUT --rev osrs230 --src B
diff -r A B        # A == B is the bar
```

That is a **fixed point** on `cache.osrs230`: all twenty `all.<type>` files and all
twenty pack files come back byte-identical, and the client boots the packed cache
with a boot log identical to the original's (`world_load: 1 chunks, 9 underlays,
8 overlays, 23 textures, 430 locs, 399 models, 17 seqs`). It is a stronger check
than per-record byte-exactness, because it holds even where a record re-encodes to
different bytes: what it says is that nothing the client can see was lost.

### The asset tree

`--assets` lays every non-config table out the way LostCity's `content/` is — one
named file per asset, in a directory that says what it is:

```sh
cachepack unpack --cache cache.osrs239 --rev osrs239 --src A --assets
cachepack pack   --src A --base cache.osrs239 --out OUT --assets
```

```
A/models/npc/goblin.model      A/songs/song_12.jmid       A/binary/title.jpg
A/models/loc/oak_tree8.model   A/synth/synth_401.synth    A/worldmap/ground/*.png
A/animsets/animset_77.anim     A/maps/map_10016.map       A/textures/texture_0/3.texture
```

`cachepack --list-assets` prints the register: 20 tables, their directory, their
pack file and their fallback extension.

**Extensions come from the bytes.** LostCity stores models as `.ob2` because a
rev-254 model *is* an OB2; an OldSchool 239 model is not. A census over all 61,615
finds 26,990 osrs-v2 and 34,625 osrs-v3 and **zero** ob2 or ob3, so writing them as
`.ob2` would name the file after a format it does not contain. The extension is
therefore per file and read off the magic trailer — a real ob2 gets `.ob2`, a real
ob3 gets `.ob3`, the OldSchool formats get `.model`.

The same rule pays off in both directions. Table 10 is JPEG and table 20 is PNG, so
those are detected and get their real extension for free — which is how
`binary/title.jpg` comes out right with no converter. And the music tables are
**not** raw MIDI (they open `17 07 f6 02`, Jagex's own container), so they get
`.jmid` rather than a `.mid` no player would open.

**Nothing is transcoded.** The bytes in the file are the bytes in the archive. Going
to LostCity's actual formats would mean converting, and that is lossy for exactly
the assets anyone would want it for — EXCEPTIONS.md A5 records that dat2 → dat1
`.ob2` drops OB3/V2/V3 texture render types and animaya skinning. `port_lostcity` is
the converter and it is a different job.

**Models are named after the configs that reference them**, as LostCity's
`unpackModelNames`/`renameModel` does — otherwise every model is `model_24458` and
the tree is no more readable than the raw idx dump. On `cache.osrs239` that names
9,553 under `npc/`, 11,278 under `obj/`, 30,761 under `loc/`, 1,631 under `spot/`
and 167 under `idk/`, leaving 8,225 unreferenced as `model_<id>`.

**Most archives stay whole.** The first cut exploded every multi-file archive into a
directory and produced 352,849 files, 209,295 of them individual animation frames.
That is not what LostCity does — an `.anim` *is* the archive, which is why its
`models/` holds 4,229 files and not a quarter of a million — and a frame is not
independently useful anyway. Only `textures` and `dbindex` explode now, where the
files inside really are separately addressed assets. The tree is 117,086 files.

The round trip is exact at the **payload** level: unpack → pack → unpack over
`cache.osrs239` returns the entire tree byte-identical, and the client's boot log
against the repacked cache matches the original's line for line. The *container*
bytes differ, because the payload is recompressed on the way in — use `--binary` if
you need container-level byte-exactness.

### Readable forms for five of them

`--assets` decodes five kinds into something you can open and edit, and leaves the
rest as their payload. `cachepack --list-assets` prints which is which.

| kind | form | round-trips |
|---|---|---|
| `maps` | `.jm2`, LostCity's format — `==== MAP ====` tiles, `==== LOC ====` scenery | all 2,934 squares |
| `interfaces` | `.if`, one `[com <id>]` block per component | all 968 |
| `textures` | `.texture`, a text record | all 210 |
| `scripts` | `.cs2` source, via the library's own decompiler | 5,586 of 9,725 |
| `sprites` | `<name>/<n>.bmp` plus `pack.meta` | all 8,534 |

Two of these needed encoders the library did not have, and both are now in it and
measured: `RSCache_MapLocsEncode` (2,933 / 2,933 loc streams byte-exact, 4,968,456
locs) and `RSCache_Dat2ComponentEncodeIf3`, which dispatches IF1 and IF3
(**74,719 / 74,719 components byte-exact** across osrs239, osrs230 and osrs184).

**Declining is normal and nothing is lost.** A record the decoder cannot handle
falls back to its raw payload under a different extension — 4,139 clientscripts do,
which is why `scripts/` holds both `.cs2` and `.bin`. `--raw-assets` turns the
decoders off entirely.

A few things worth knowing:

- **A texture is not an image.** LostCity stores textures as PNG because a rev-254
  texture *is* a bitmap; an OldSchool one is a record naming a sprite and its
  animation, so the readable form is text. A PNG would mean rendering something
  the cache never stored.
- **A map archive is five files, not two.** Terrain is file 0 and locs file 1, but
  osrs239 squares also carry three the library does not decode. They sit beside the
  `.jm2` as `.extra<N>.bin` and go back on import — a readable map format that
  dropped a third of the archive would be the worse trade.
- **Height comes from `authored_height`.** The terrain decode rewrites `height` for
  every tile, so by the time anyone sees it there is nothing writable left
  (EXCEPTIONS.md B8).
- **Sprite palettes are written out, not re-derived.** Two entries can hold the
  same colour, so RGB does not determine the index. Alpha rides in the BMP, which
  is 32-bit BGRA.
- **CS2 name tables are optional but worth having.** Without them a decompile is
  correct but reads `obj_995` rather than `coins_995`, and the four types with no
  numeric spelling (`boolean`, `stat`, `maparea`, `fontmetrics`) fail outright —
  which is most of the 4,139 fallbacks. Point `CACHEPACK_CS2_NAMES` at a clone of
  [RuneStar/cs2](https://github.com/RuneStar/cs2)'s resources directory.

Measured end to end on `cache.osrs239`: unpack → pack → unpack returns all
**137,652 files** byte-identical, and the client's boot log against the repacked
cache matches the original's line for line.

### Raw containers

`--binary` is the other half of the trade: the same tables as `binary/idx7/1234.bin`,
never decompressed.

```sh
cachepack unpack --cache cache.osrs230 --rev osrs230 --src A --binary=7,8
cachepack pack --src A --base cache.osrs230 --out OUT --binary
```

Byte-exact with no encoder to be wrong, XTEA-encrypted map archives pass through
untouched, and the bzip2 encoder's known non-identity to Jagex's (EXCEPTIONS.md A1)
never enters the picture — at the cost of being unreadable. It also reaches tables
the asset register does not name, which `--assets` skips.

One thing worth knowing if you touch either importer: **the reference table's CRC
covers the container minus its 2-byte version trailer.** Measured, not assumed —
over `cache.osrs230` idx13, the whole-archive CRC matches on 0 of 10 archives and
the `size - 2` CRC on 10 of 10.

### Packing writes into a copy of a cache

A cache is far more than its configs, so `pack` starts from a base cache and writes
the config table into it — exactly as LostCity's `cache.write(0, 2, config)` writes
one archive into an existing cache and leaves the rest alone. `--base` copies first;
without it `--out` is edited in place, which grows the file because the dat2
container appends rather than compacts (EXCEPTIONS.md B4).

---

## `cs2` — decompile and compile clientscripts

```sh
cs2 decompile (--cache DIR --rev NAME | --raw DIR) [--names DIR] [--out DIR] [id …]
cs2 compile   --src (DIR) [--names DIR] [--out DIR]
cs2 roundtrip (--cache DIR --rev NAME | --raw DIR) [--names DIR] [id …]
```

Turns a cache's table 12 into CS2 source and back. The language layer itself
lives in the library (`src/cs2/`); this is the front end.

```sh
# One script, out to ./decompiled/.
git clone https://github.com/RuneStar/cs2 ~/cs2      # the name tables; see --names below
NAMES=~/cs2/src/main/resources/org/runestar/cs2

3rd/rscache/tools/cs2/cs2 decompile \
    --cache cache.osrs230 --rev osrs230 \
    --names "$NAMES" --out decompiled 2319
# decompiled 1, failed 0, total 1
# -> decompiled/[clientscript,tob_partydetails_kickmember].cs2
```

Omit the id list to decompile every script in the cache. **Omitting `--names`,
though, is what produces `FAIL 2319: no source spelling for 1 as int-bool`** —
`1` is `^true`, and `true` lives only in `boolean-names.tsv`. See `--names`
below; that failure is by design, not a decode error.

Two script sources, because they answer different questions:

- `--cache` reads a real cache, which is what the tool is *for*. It also reads
  the cache's param config, which is the only reliable answer to "does
  `oc_param` push an int or a string" — a stack-shape question, not a cosmetic
  one.
- `--raw` reads a directory of bare script files named by id. That is the shape
  [RuneStar/cs2](https://github.com/RuneStar/cs2)'s `input/` dump uses, and it
  is what makes the decompiler checkable against a corpus of known-good output
  produced by the implementation this was ported from. `test/test_cs2.c` is that
  check.

`--names` points at a directory of RuneStar's `*-names.tsv` files. They are
optional and purely legibility — without them a decompile is still correct, just
`obj_995` instead of `coins_995`. Four types are the exception: `boolean`,
`stat`, `maparea` and `fontmetrics` have no numeric spelling in the language at
all, so a script using an id those tables do not name cannot be printed.

`roundtrip` is the standing gate on the compiler: decompile, compile the result,
and compare against the bytes the cache held. It reports the same
exact/same-length shape as the library's other round-trip suites, for the same
reason — high same-length with low exact means a re-encoding, low both means a
loss.

Byte-exactness is capped by design, though, because the decompiler discards an
`else` it can prove unnecessary. The sharper check is the **source fixed point**:

```sh
cs2 decompile --cache DIR --rev NAME --names N --out A
cp original_bytes/* B/            # so callees still resolve
cs2 compile --src A --names N --out B
cs2 decompile --raw B --names N --out C
diff -r A C                       # A == C is the bar
```

That is what found the three compiler bugs recorded in EXCEPTIONS.md G2 — a
call resolving to the wrong script, `&`/`|` precedence, and duplicated string
text — none of which the byte comparison had isolated.

### Regenerating the command table

```sh
python3 3rd/rscache/tools/cs2/gen_cs2_tables.py
```

Reads three things and writes `src/cs2/cs2_command.gen.h`:

| Source | Supplies |
|---|---|
| `tools/cs2_gen_opcodes/vendor/Opcodes.kt` | opcode id ↔ name, shared with the client's CS2 VM so there is one answer to "what is opcode 105" |
| `tools/cs2/vendor/Command.kt` | per-opcode signatures — how many values, and each one's type and identifier hint |
| `src/cs2vm2/cs2vm2_opcode_stack.gen.h` | pop/push counts for the opcodes the 2021 Command.kt predates |

The last one is what makes a modern cache decompile at all. An opcode with no
recorded arity does not fail quietly: it desynchronises the operand stack, so
the decompiler refuses the whole script rather than emit a confident reading of
a different program. Layering the client's own stack table over the vendored one
took OSRS 230 from 6,634 to 7,553 scripts, and solving the remainder from the
corpus (`cs2 infer-arity`, below) took it to 7,657. The stack table records *how many*
ints and strings, not what they mean, so those opcodes get plain `int`/`string`
prototypes — that costs identifier quality (`$int3` rather than `$width3`),
never correctness.

Anything this repo knows that neither source does goes in `local_commands.py`,
so regenerating keeps it. Additions there must be **established, not guessed**:
a wrong pop count is the one error that produces a plausible decompile of the
wrong program.

### Solving an unknown opcode's arity

```sh
cs2 infer-arity --cache DIR --rev NAME [--names DIR]
```

An opcode's pop/push counts are not in the bytecode, but they are implied by it:
a script only interprets to the end if every arity keeps the operand stack
balanced. For each unknown opcode the tool takes the scripts where it is the
*only* unknown, tries every plausible (int in, str in, int out, str out), keeps
the ones that interpret, and intersects across scripts. A third phase solves
pairs, for opcodes that never appear alone.

It prints a block ready to paste into `local_commands.py`, annotated with the
number of scripts each solution was established against — thirty is not one, and
the file says which. Re-run after pasting: every solved opcode turns
two-unknown scripts into one-unknown ones, so it converges over a few rounds.

Two things make this evidence rather than guesswork:

- Arities are judged on **interpretation alone**, not on a full decompile, so an
  unrelated later failure cannot make a correct arity look wrong.
- The reference comparison (`test_cs2`) is the control. It stayed at exactly
  6,489 identical / 2 different through every round, so no inferred signature
  changed an output the RuneStar implementation also produced.

What it cannot do is name things. The prototypes are plain `int`/`string`,
because the method establishes counts, not meanings.
