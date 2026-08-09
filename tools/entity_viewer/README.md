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

## Testing without a browser

```sh
tools/entity_viewer/ev_server --rev osrs239 cache.osrs239 \
    --catalog out/osrs239_anims --selftest 2042 5804
```

Bakes one npc and one sequence, round-trips both through the wire format,
applies frame 0 and reports how many vertices moved, then runs the browser's
exact render path natively and counts drawn pixels:

```
  model: 988 vertices, 1934 faces, vertex_bones yes
  model: 94664 bytes, round-trip 988/988 vertices, 1934/1934 faces
  anim: 49 frames on rig 184, base length 171
  frame 0 moves 988 of 988 vertices
  render: height 1318, zoom 3954, cull 0, 2000 of 65536 pixels drawn
```

Both numbers matter and neither is implied by the other. "0 vertices moved"
means the model and the animation do not share a rig, whatever the catalog said.
"cull 3" is `TORIDRAW_CULL_ERROR` and means the model has no bounds cylinder —
which renders as a blank canvas with no error anywhere, and is what this caught
the first time.
