# entity_viewer — which animations apply to an npc, and what they look like

Three things: a catalog of npc→animation matches, a browser viewer that plays
them, and a command-line harness that measures a weapon swing against the
graphic attached to it.

```sh
make -C tools/entity_viewer                     # ev_catalog + ev_server + ev_swing
make -C tools/entity_viewer wasm                # web/ev_wasm.js + .wasm (needs emcc)

tools/entity_viewer/run.sh                      # checks freshness, then serves
# -> http://127.0.0.1:8099/

# The catalog is optional and takes about five minutes; it adds rig matching.
mkdir -p out/osrs239_anims
tools/entity_viewer/ev_catalog --rev osrs239 cache.osrs239 \
    --names OSRS-Content/osrs239-content --out out/osrs239_anims
```

`--names` is what turns the **player half** on: it supplies the gameval names
the equipment and graphic pickers need, and it points at the content tree so an
exported asset that has since been *edited* is drawn as the game draws it rather
than as the cache still holds it. Without it the npc half works exactly as
before.

## Caches

The **Caches** panel lists every cache the viewer knows about and switches
between them; each is reopened and re-indexed on the spot. The registry persists
in `web/.ev_caches`, and `--cache-root DIR` (which `run.sh` points at the repo
root) is scanned one level deep at startup so the repo's `cache.*` directories
are there to click without typing a path.

**The revision beside each cache is a detected guess.** Detection separates the
families confidently — an RS2-era cache has a materials table and shards its
configs, an OldSchool one does not — but it cannot tell osrs184 from osrs239, or
rs558 from rs643, because their npc records barely differ. That matters because
the wrong profile does not fail loudly: it decodes records at the wrong field
widths and produces plausible nonsense. So the field is editable, and the value
is what the entry is stored with.

Searching npcs, sequences and models runs against a per-cache **index** built
directly from the cache in about a tenth of a second, not against the catalog.
That is the whole reason adding a cache is instant: requiring a catalog first
would make it a five-minute wait. Without a catalog the npc list and the model
still work; what is missing is the rig matching.

### The index is cached in the cache

A built index is written to **`<cache_dir>/.ev/index-<rev>.evi`**, which turns
the ~110 ms build into a ~2 ms load — the difference between switching caches
being free and being a pause. It lives inside the cache directory because it
describes *that* cache: move the directory and the index travels with it, delete
it and nothing stale is left behind. The directory writes a `.gitignore` of `*`
on creation, so it never shows up in `git status` wherever it lands.

It is **keyed by revision**, because the profile decides how every record
decodes — the same bytes read as osrs184 and as osrs239 give different npc
records. One file per directory would hand back the previous profile's answers
after you corrected the revision, which is the exact silent-wrong-answer the
correction was for.

**Staleness is a fingerprint** over the size and mtime of `main_file_cache.dat2`
and every `idxN`, stored in the file and re-checked on load. Not a content hash:
the data file is hundreds of megabytes, and hashing it would cost more than the
indexing it saves. That trade is real — a rewrite preserving every size and
mtime is not caught — but any ordinary edit, repack or partial download changes
at least one. A cache directory that cannot be written to is not an error; the
index is just rebuilt each time, and the server says so once.

```sh
make -C tools/entity_viewer ev_index_cache_probe
tools/entity_viewer/ev_index_cache_probe cache.osrs239 osrs239
```

That probe checks the property that actually matters — a cache which never goes
stale is *worse* than no cache, because it answers confidently with the previous
contents. So it builds, reloads (expecting a hit), touches an idx file, and
reloads again (expecting a miss), comparing the full contents each time rather
than just the timing: a load returning an empty index is also fast. It is how
the empty-string-vs-absent name bug below was caught.

## Textures

Both texture systems are loaded and drawn:

- **sprite-backed** (OldSchool, and RS2 before materials) — texture definitions
  name palette-indexed sprites, layered into 128×128 ARGB.
- **procedural** (RS2 from about 2010, which is what the QBD's source cache is)
  — the texture *is* a program over noise, gradients and other textures,
  evaluated per texel. The whole RS727 table is 2315 textures and bakes in about
  three seconds.

Neither is reimplemented here: `ev_textures.c` is a synchronous driver over the
client's own evaluator and palette rules, because a texture that is subtly the
wrong brightness in the viewer and right in the client is worse than no viewer.
Which system a cache uses is *probed* — the materials table existing is the gate
— rather than inferred from the revision.

The browser has no cache, so the server ships the textures a model names as an
`EVT1` blob, with the ids in an `X-Texture-Ids` header on the model response. A
full RS727 set would be 151 MB; a model names a couple of dozen.

**An RS2-era npc is served as an HD model.** Its textured faces are mostly
cylinder- and cube-mapped, and the classic raster can only plane-map — it
*skips* every face it cannot map, so such a model comes out untextured or, when
nearly all its faces are mapped, invisible. `ev_build_npc_model_hd` returns NULL
for models that do not need this, which is every OldSchool npc.

### Textures modulate the face colour

This era's textures are often luminance **masks**, not pictures: three of the
seven TzTok-Jad uses in RS727 have a maximum chroma of exactly 0, and the faces
carrying them are authored deep red. So the texel multiplies the face's colour,
and drawing it untinted produces a grey model — a wrong answer that looks like a
plausible one.

The tint comes from `face_colors`, the flat authored HSL16. It cannot come from
`face_colors_a`: the lighting pass overwrites colors_a/b/c of a textured face
with plain 0..127 lightness, which is what reaches the kernel as the *shade*, and
masking `hsl16 & 0xFF80` on a value below 128 yields 0 — hue 0, saturation 0, a
tint that does nothing. Modulate is on for the material system and off for the
sprite-backed one, where the texel is the colour outright.

### Matching the reference render

The HD compositing rules are being recovered by comparison, so there is a
harness for it rather than a rebuild-and-squint loop:

```sh
make -C tools/entity_viewer ev_hd_sheet ev_uv_span
tools/entity_viewer/ev_hd_sheet cache.rs727_preeoc rs727 2745 --out /tmp/sheet.bmp
tools/entity_viewer/ev_uv_span  cache.void634 rs643 2745
```

`ev_hd_sheet` renders every hypothesis in one process into one contact sheet at
identical framing — a wrong variant is obvious beside its neighbours in a way it
never is alone — and prints metrics, because "looks better" and "is closer"
diverge. **clip%** (channels pinned at 255) is the one that catches damage the
mean cannot see: a clipped channel has lost the texture's variation entirely.

`ev_uv_span` also counts faces straddling the **atan2 seam**, before and after
the unwrap — after must be 0. The cylinder and sphere projections wrap, so a
triangle across the branch cut gets 0.98 / 0.99 / 0.02, and the rasteriser
interpolates the long way round and smears the entire texture across that one
face. The direct generator had always unwrapped it; the kernels had not.

`ev_uv_span` measures uv spans through the **kernel's** matrix path. The existing
`rs2012_qbd_kernel_survey` measures the *direct* generator, and the two are
separate implementations of one projection — so a matrix-form bug hid behind a
survey reporting healthy spans. That is exactly how the cylinder `scale_z`
collapse survived: with `u` pinned to zero the span still looked fine, because
`v` still varied.

### Ignoring face priorities

**ignore priorities** in the sidebar (`--no-priorities` on `ev_hd_sheet`) draws
as if the model carried none. The painter's sort ranks priority ahead of depth,
so a model whose priorities are wrong stacks faces in front of things they sit
behind — which is not distinguishable from a broken depth sort by looking at it.

The flag hides the arrays for the duration of a render rather than freeing them,
so it toggles without reloading the subject.

What it has established so far: the OB3/HD models (rs727 TzTok-Jad, the QBD) do
carry priorities that change the sort, while the rs643 and OldSchool models here
have no per-face array at all. It also ruled a theory out — Jad's missing eyes
were not a priority burial, because turning priorities off did not bring them
back. They were the lava/eye texture being tinted and clamping to white.

## Moving around

Drag orbits, the wheel zooms, **WASD** flies the viewpoint over the ground
plane, **R**/**F** go up and down, and **Re-centre** (or **X**) undoes all of it
and re-frames the model. Flying is not the drag-pan: pan slides the image on the
canvas without changing depth, while this moves through the scene, so distance,
culling and apparent size all follow.

**The step carries no yaw term, and that is not an oversight.** `camera.yaw` is
fixed at 0 in this viewer — dragging spins the *model*, it does not orbit the
eye. The two look identical and are not: the screen axes never rotate, so right
on screen is world x at every yaw. Rotating the step by yaw, which is what this
did first, makes the keys correct near yaw 0 and **inverted half a turn away**.

That bug survived a unit test, because the test asserted the convention I had
assumed rather than the one the projection implements. `ev_move_screen_probe`
replaced it and asserts nothing about internals — it renders, steps, renders
again, and measures where the subject actually went (D moves it left, W makes it
bigger, R moves it down) at eight yaws:

```sh
make -C tools/entity_viewer ev_move_screen_probe
tools/entity_viewer/ev_move_screen_probe cache.osrs239 osrs239 3127
```

Speed is per *second*, not per frame: a per-frame step travels twice as far on a
120 Hz display and changes distance whenever the framerate stutters.

## A model file, through the HD kernels

The npc and player pickers ask "what does this entity look like". The **Select
model file** button asks a different question: *what does this file contain, and
how does the HD router treat it*.

```sh
tools/entity_viewer/run.sh          # or run.bat on Windows
# -> http://127.0.0.1:8099/, then "Select model file…"
```

Pick any model archive off disk — dumped from a cache, produced by a tool. The
browser cannot decode one (that is rscache, and rscache is server-side), so the
bytes are POSTed to `/api/modelfile`, decoded, lit, and returned as an ev_wire
**EVH1** blob: an ordinary model blob plus the per-face-group texture mappings.
The page then draws it through `ToriDraw_RenderHD`.

The response says what the file *was*, in headers, so the page names the format
rather than inferring it from whether the decode happened to work:

```
X-Model-Format: OB3      (or V2 / V3 / OB2 / unknown)
X-Model-Faces: 6863
X-Model-Textured: 233
X-Model-HD: 1            (1 = it carries texture mappings)
```

### The routing readout is the point

A mis-routed face still draws. A cube-mapped face rendered through the plane
kernel produces pixels, and nothing about the image says which kernel made
them — so the panel reports `ToriDraw_HDRenderStats` directly: how many faces
reached `texplane`, `texcylinder`, `texcube`, `texsphere`, which gate each
material selected, and every fallback. On the QBD's source model (70260) that
reads 2,438 texplane and 432 texcube, with no fallbacks.

### `placeholder texture`, and why it is off by default

A bare model file describes no textures. The faithful thing is to draw every
textured face as its flat colour, which is what happens with the box unchecked —
and it means the mapped kernels never run, so the cylinder/cube/sphere counters
sit at zero whatever the file contains.

Checking the box supplies a synthetic checkerboard for every texture id the
model names. It is a lie about the *asset* and the label says so; it is the
truth about the *routing*, which is what this view is for.

## Running it, and staleness

`run.sh` / `run.bat` exist for one reason: the viewer compiles the same C twice,
once into the native server and once into `web/ev_wasm.wasm`. Edit
`ev_render.c`, rebuild with `make`, and the server is current while the browser
silently keeps running the old renderer — the page loads, the model draws, and
nothing says the two halves disagree. You change a kernel, see no difference,
and conclude the change did nothing.

Both scripts check each artefact against every source both halves compile, and
by default rebuild what is stale. If the wasm is stale and `emcc` is not on
PATH they **refuse to serve** rather than warn, because a warning scrolls past.

```sh
tools/entity_viewer/run.sh --check-only   # report freshness, exit 1 if stale
tools/entity_viewer/run.sh --no-wasm      # accept a stale wasm deliberately
tools/entity_viewer/run.sh --port 8100 --cache ../cache.osrs239
```

## The player half

An npc is one config with a model list on it. A player is not, and neither is a
player-attached graphic:

> `spotanim_pl` does not draw a graphic in the world. The client poses the
> graphic's model, **strips its labels**, lifts it by the spotanim height, and
> MERGES it into the player's own model (`app_world_sync_one_entity_spotanim`,
> `src/app.c`). The merged mesh is what the scene draws, and the body's sequence
> animates it from there.

Two consequences shape everything here. Drawing the player and the graphic as
two models side by side is not what the game does, so the viewer merges. And
because the graphic's labels are gone, the body's sequence cannot move a single
one of its vertices — a player-attached graphic physically **cannot** track a
swinging blade. Its position in the player's local XZ plane is a property of the
model's own vertices and of the spotanim record's rotation; the only knobs a
script has are the height and the delay.

In the page: switch **Player**, click objs to equip them, pick any sequence from
the human rig (framemap 0), then choose a graphic and set its height, delay and
rotation. Playback is counted in **client cycles**, not frames, because a delay
in cycles has no frame number in the other sequence to name.

**Top-down** is the button that matters for placement: it is the one view where
where the graphic sits relative to the player reads as a distance rather than
being inferred from foreshortening.

The viewer state lives in the URL, so a finding is a link rather than a list of
boxes to fill in:

```
http://127.0.0.1:8099/#player&wear=22325&seq=8056&fx=1231&delay=16&height=100&orient=3&pitch=512&paused&cycle=44
```

Recognised: `player`, `wear=` (comma-separated obj ids), `npc=`, `seq=`, `fx=`
(spotanim id), `delay=`, `height=`, `orient=` (0–3 quarter turns, overriding the
spotanim record), `pitch=`, `yaw=`, `zoom=`, `cycle=`, `paused`. It is also how
the page gets tested at all — a headless browser can open it already configured.

## ev_swing — the same thing, measured

A picture shows that a graphic is off. It cannot say which of the three things
is off, because all three look the same:

| | what is wrong | the knob |
|---|---|---|
| TIME | plays early or late against the swing | `spotanim_pl` delay, in client cycles |
| PLACE | sits in the wrong part of the player's local space | the model's vertices, or the record's rotation |
| HEIGHT | rides too high or low | `spotanim_pl` height |

`ev_swing` separates them. It builds the player, merges the graphic through the
same `ev_render` the browser uses, walks the swing a cycle at a time, and prints
where the blade is and where the *lit* part of the graphic is at each one.

```sh
tools/entity_viewer/ev_swing --rev osrs239 cache.osrs239 \
    --arc-model OSRS-Content/osrs239-content/models/spot/dragon_halberd_special_west_red.model \
    --orient 3 --out /tmp/scythe
```

Defaults are the scythe of vitur's, as `scythe_of_vitur.rs2` ships them. What it
reports:

- **the two frame tables**, in cycles — which is worth having on its own: this
  sequence holds every segment of its graphic fully transparent for its first
  four frames, so a graphic played "at delay 16" does not appear until cycle 26.
- **the graphic's pose**, as words: whether its long axis lies TANGENT across the
  player's facing (as a slash does) or RADIAL front-to-back through them, and
  how far in front or to the side its centre sits. No delay and no translation
  fixes a wrong one — it is the record's rotation.
- **a delay sweep** scored on the ALONG-axis mismatch only, so a constant lateral
  offset cannot drown the timing signal.
- **two landmark checks** — the cycle the blade comes in front versus the cycle
  the graphic first shows anything, and the strike versus the graphic's brightest
  moment. Each is checkable by hand against the tables, and agreeing with the
  sweep is the reason to believe any of them.
- **the residual offset**, split into the across-axis part (a rigid offset the
  asset can absorb) and the along-axis part (left to the delay).

`--out` writes three pictures: `_top.bmp` (straight down) and `_side.bmp` (the
game's camera) contact sheets with the measured points drawn on them as crosses,
and `_plot.bmp` — an overhead trace of the blade's path and the lit graphic's
path on a one-tile grid, which is where "do these two coincide" is answered at a
glance.

For a **spritesheet** rather than a diagnosis: `--rows 0` gives one cell per
client cycle with no sampling (the default caps at four rows and samples, which
is right for a quick look and wrong for a record of the animation),
`--no-markers` leaves the crosses off, and `--yaw` picks the facing — 0 south,
512 west, 1024 north, 1536 east, `world_cycle.c`'s own numbers. The measurements
are taken in the player's local space and do not change with the facing; only the
pictures do. Markers are dropped, and said to be dropped, at any yaw but 0, since
the projection that places them is solved for yaw 0 only.

`docs/scythe_of_vitur_charged/` is a worked example: the scythe's swing from each
of the four facings, plus the top-down comparison that shows which of the
graphic's four compass copies is the right one.

## Two kinds of rig

An animation is bound to a rig one of two ways, and both had to be walked:

- **Classic** — the sequence names frames, and the rig is the *framemap* those
  frames were built against.
- **Skeletal (Animaya)** — the sequence names no frames at all. Seq opcode 13
  gives an idx22 curve set, and the rig is that curve set's `base_id`, which
  lives in the *tail* of an idx1 framemap file. Every modern OldSchool npc — the
  Tombs of Amascut bosses and everything after — animates this way.

The two share one id space: `RSCache_Dat2SkeletalBase.id` **is** the framemap id.
So one reverse index covers both, and an npc's rig set is the union — which is
what `tool_dat2_seq_rig_id` resolves. Asking the old question of a skeletal
sequence returned -1, so 943 sequences and 422 npcs had no rig at all.

Playing one needs more than the rig: a skeletal sequence poses through the
model's per-vertex bone influences, so a model with no Animaya skin is left in
its bind pose rather than mis-animated. `framemap_seqs.csv` carries `kind` and
`npc_catalog.csv` carries `animaya_skinned`; they have to be read together, and
the viewer greys out a skeletal row an npc cannot play.

## The two passes

**Rigging matches — concrete.** An animation frame addresses bones by index into
a *framemap* (the rig). A sequence built against one rig, applied to a model
skinned for another, moves the wrong vertices — so sharing a rig is the hard
precondition for an animation applying at all. `ev_catalog` walks from an npc's
own idle/walk/turn/run/crawl sequences (and its BasType on RS2) to the framemaps
those use, then collects every other sequence built on the same framemaps.

This is a *possibility* set, and its selectivity depends entirely on the rig.
Framemap 0 is the shared human rig with 3,905 sequences on it, so every human
npc matches all of them — true, and not very useful. A boss with its own rig
returns a handful, and those are almost certainly its complete animation set.

**Name guesses — a guess, labelled as one.** Sequences and npcs carry the
content team's gameval names (`snakeboss_boss_ranged`, `snakeboss_death`), which
is the only handle on an animation nothing points at. A sequence whose name
shares a distinctive word with the npc's is probably that npc's. This reaches
attack, death and spawn animations that no id walk can find, and it produces
false positives — which is why it is a separate column carrying its score, and
why the viewer shows it in a separate list.

Words describing what an animation *does* (`walk`, `attack`, `death`, …) are
excluded from matching, or `dragon_attack` would match every npc with `attack`
in its name. Tokens shorter than four letters go too.

## The files ev_catalog writes

Normalised, because the un-normalised join is 16,292 npcs × up to 3,905
sequences — about 27 million rows, nearly all of them repeating the same rig
membership.

| File | Rows | What |
|---|---|---|
| `npc_catalog.csv` | one per npc | counts and ids — the browsable table; `rig_match_skeletal` and `animaya_skinned` say whether the skeletal half is playable |
| `npc_rigs.csv` | one per (npc, rig) | an npc's seed sequences and the framemaps they use |
| `framemap_seqs.csv` | one per sequence | framemap → every sequence on it, `kind` = classic or skeletal |
| `npc_name_matches.csv` | one per guess | npc → guessed sequence, score, and whether the rig walk also found it |

An npc's rigging matches are `npc_rigs ⋈ framemap_seqs` on framemap id.

## How the viewer is split

```
  browser                                  your machine
  ┌────────────────────────────┐           ┌──────────────────────────┐
  │ ev_wasm.wasm (138 KB)      │           │ ev_server                │
  │   toridraw, nothing else   │           │   rscache + the catalog  │
  │   ev_render.c              │           │   ev_build.c             │
  │      ▲                     │           │        ▲                 │
  │ ev.js│                     │           │        │                 │
  └──────┼─────────────────────┘           └────────┼─────────────────┘
         │  GET /api/npc/<id>.model  (ev_wire bytes)│
         │  GET /api/seq/<id>.anim   (ev_wire bytes)│
         │  GET /api/npc/<id>.json   (its two lists)│
         └──────────────────────────────────────────┘
```

`cache.osrs239` is 216 MB and a viewer only ever needs the few records on
screen, so the cache stays on the server: it decodes, merges the npc's model
parts, applies recolours and lighting, and sends the built model. The browser
half links toridraw alone, which is what keeps the module at 138 KB and means a
bug there can only ever be a rendering bug.

`ev_wire.c` is compiled into both, so the format they speak has one definition.

Both lists are searchable. The npc box matches display name, gameval or id; the
animation box matches gameval name, a full sequence id, or the words `skeletal`
/ `classic`. The animation one is not a nicety — a human-rigged npc lists 3,905
sequences, and typing `death` is the difference between that and the 46 worth
looking at. The query survives changing npc on purpose, so "what is each of
these creatures' death animation" is one keystroke per npc.

## Configuring npcs from the catalog

`tools/gen_npc_combat.py` turns this catalog into content — per-npc attack /
defend / death animations under `OSRS-Content/.../npc_combat/`, compiled into
`server/scripts/npc/configs/npc_anims.generated.npc`. See
`docs/DEATH_ATK_DEF_ANIMS.md`.

It reads `framemap_seqs.csv` and `npc_rigs.csv` by column name, so extending the
catalog reaches it without any change there: adding skeletal rigs put 943 more
sequences and 422 more npcs in front of it automatically.

`gen_npc_anims.py` in this directory is the earlier, simpler generator and
writes **the same file**. Two generators over one output path is a race — run
each once in the wrong order and the tree keeps whichever finished last. Use
`tools/gen_npc_combat.py`; this one is kept only because it is committed, and
retiring it is a call for whoever owns the successor.

## Nothing here converts cache data

Every cache→renderer step is the client's or the tool library's:

| Step | Whose |
|---|---|
| model | `ToriRS_ModelFromRSCache` → `ToriDraw_ModelFromToriRS` (`src/engine/`) |
| animation | `ToriDraw_AnimationFromRSCache` (`src/engine/`) |
| frame decode | `tool_dat2_frame_load` (`3rd/rscache/tools/common/`) |
| framemap, models, seqs, npcs | `tool_dat2_*` (`3rd/rscache/tools/common/`) |
| rig walk | `tool_dat2_build_framemap_index` (`anim_affinity.c`) |
| `id=name` packs | `lc_pack_load` (`3rd/rscache/tools/port_lostcity/`) |
| animation free | `ToriDraw_AnimationFree` |

This was not true at first, and the cost was a rendering bug with no visible
cause. The hand-written model converter got face priority wrong — the cache
stores **one byte per face**, the renderer **two 4-bit fields per byte** — so a
verbatim copy gave every face some other face's priority. Priority is the
primary key of `ToriDraw_RenderModel2SortFaces`, so the painter's sort ran on
nonsense and models drew with holes that looked like near-plane clipping. The
copy even carried a comment asserting the layouts matched.

The npc build follows app.c's order step for step, which is also where the
missing `ToriDraw_ModelScale` came from: npc opcodes 97/98 were never applied,
so every npc with a scale of its own drew at the model's raw size.

What is genuinely this directory's, because no equivalent exists:

- **`ev_wire.c`** — a ToriDraw model/animation as bytes. `trspk` is GPU vertex
  packing and rscache's encoders are cache format; neither is transport.
- **`ev_render.c`** — the orbit framing. It matches
  `ToriDraw_SpriteNewFromModelRaster` (same `sin_pitch`/`cos_pitch` orbit, same
  `near_plane_z = 1` for a close-up) minus that path's widget-rect offset, since
  this one centres via the viewport instead. The world's `near_plane_z = 50`
  is wrong here: at a camera orbiting one model it sits *inside* big models and
  clips their nearest faces.
- **`ev_server.c`** — the HTTP surface.
