# entity_viewer — which animations apply to an npc, and what they look like

Two things: a catalog of npc→animation matches, and a browser viewer that plays
them.

```sh
make -C tools/entity_viewer                     # ev_catalog + ev_server
make -C tools/entity_viewer wasm                # web/ev_wasm.js + .wasm (needs emcc)

# 1. build the catalog (about 5 minutes over cache.osrs239)
mkdir -p out/osrs239_anims
tools/entity_viewer/ev_catalog --rev osrs239 cache.osrs239 \
    --names OSRS-Content/osrs239-content --out out/osrs239_anims

# 2. serve it
tools/entity_viewer/ev_server --rev osrs239 cache.osrs239 \
    --catalog out/osrs239_anims --web tools/entity_viewer/web
# -> http://127.0.0.1:8099/
```

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
| `npc_catalog.csv` | one per npc | counts and ids — the browsable table |
| `npc_rigs.csv` | one per (npc, rig) | an npc's seed sequences and the framemaps they use |
| `framemap_seqs.csv` | one per sequence | framemap → every sequence built on it |
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

## Configuring npcs from the catalog

`gen_npc_anims.py` turns the catalog into content. Every npc in OSRS-Content is
seeded from `[default]` in `server/scripts/general/configs/npc_default.npc`,
which hands out the human unarmed set — right as a universal fallback, wrong for
every non-human npc. This gives 3,185 of them their own.

```sh
python3 tools/entity_viewer/gen_npc_anims.py \
    --catalog out/osrs239_anims \
    --content OSRS-Content/osrs239-content            # dry run
python3 tools/entity_viewer/gen_npc_anims.py ... --write
make -C src mock230-servpack                          # repack the server band
```

A sequence is only taken when the rig *and* the name agree: it must be built on
one of the npc's own framemaps, and its gameval name must share a distinctive
word with the npc's. The name test alone is not enough — `slayer_nechryael_spawn`
sits on the shared human rig, the cache holds no nechryael animation at all, and
matching on `slayer` handed it the player's abyssal whip swing. So a third gate
asks whether the npc's name accounts for at least 10% of its own rig: `gnome` is
40 of its rig's 98 sequences and `zombie` 47 of 50, while `slayer` is about 1% of
framemap 0's 3,905. That is what separates a creature from a namespace, and no
property of the word itself does.

Where a creature has several variants of an action, the npc's own name decides —
`skeleton_armed` takes `skeleton_update_attack_sword`, `zombie_unarmed4` takes
the unarmed form — and with no hint the unarmed variant wins, for the same reason
npc_default.npc gives for the global fallback. This is the one genuinely
judgemental step and `WIELD_HINTS` is where it is written down.

It writes three things:

| What | Why |
|---|---|
| `server/scripts/npc/configs/npc_anims.generated.npc` | the blocks |
| `pack/npc.server` (merge, never remove) | `cachepack pack` gives a record a server band only if this names it; stating a server field without it is a hard error |
| `param=attackrate` on 503 blocks | not animation work — an npc with no block is answered from the cache, and gaining one drops `[default]`'s `attackrate=4` on top, so the cache's value is restated to keep it |

Npcs that already have an authored block anywhere in `server/scripts` are skipped,
so a hand-checked port always wins.

## Testing without a browser

```sh
tools/entity_viewer/ev_server --rev osrs239 cache.osrs239 \
    --catalog out/osrs239_anims --selftest 2042 5804
```

Bakes one npc and one sequence, round-trips both through the wire format,
applies frame 0 and reports how many vertices moved, then runs the browser's
exact render path natively and counts drawn pixels:

```
  model: 988 vertices, 1934 faces (0 textured), vertex_bones yes, face_priorities no
  model: 94664 bytes, round-trip 988/988 vertices, 1934/1934 faces
  anim: 49 frames on rig 184, base length 171
  frame 0 moves 988 of 988 vertices
  render: height 1318, zoom 3954, cull 0, 2000 of 65536 pixels drawn
  wire round-trip: 0 byte(s) differ from the directly built model
```

Four numbers, none implied by the others:

- **vertices moved by frame 0.** Zero means the model and the animation do not
  share a rig, whatever the catalog said.
- **cull.** `3` is `TORIDRAW_CULL_ERROR` and means the model has no bounds
  cylinder, which renders as a blank canvas with no error anywhere.
- **pixels drawn.** Separates "the module never ran" from "it ran and drew
  nothing".
- **wire round-trip.** Renders the model this process built and the model
  rebuilt from the bytes the browser receives, and compares the two images. The
  browser can only draw what `ev_wire` carries, so a field the format drops is
  an artefact there and invisible here. Anything but 0 means the two halves are
  not looking at the same model.

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
