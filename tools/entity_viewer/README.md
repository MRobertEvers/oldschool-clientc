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
