# Summoning texture review

Rev-530 textures are procedural material graphs; osrs239 textures are sprite-backed records.
There is no lossless id conversion.  This file is the approval record for the deliberately
human part of the Summoning port: no `signoff` value becomes `ok` without a reviewer looking
at both the rig-labelled and material renders.

## The setting in force: `material_mode=face_colour`

**Every shipping cohort manifest states `material_mode=face_colour` inside its
`[import:...]` section, and the lane is authored with that mode.**  Read this section before
changing a manifest or re-running an import; the mode decides what the models look like, and
the wrong one is not obviously wrong at import time.

`cachepack import` has three modes, selected by the `material_mode` key:

| mode | what it does to a face carrying a source material |
|---|---|
| `face_colour` | drops the texture, keeps the model's **own** face colour |
| `average_hsl` | drops the texture, **replaces** the face colour with the material's representative HSL |
| `mapped_texture` | keeps the face textured, remapping the id through `texture_map_530_to_239.ini` |

`mapped_texture` is the default whenever a texture map is in scope — and for a legacy
`[import:scape2009]` manifest one always is, because `load_texture_map_file` loads
`texture_map_530_to_239.ini` automatically when the manifest states no `[texture_map]` of its
own.  So **omitting `material_mode` silently selects `mapped_texture`**, which is how the lane
acquired it without anyone choosing it.

That mode is wrong for this lane, twice over:

1. **It made the familiars invisible.**  A rev-530 model's textured faces are mapped by HD
   render types 1-3 (cylinder/cube/sphere), and OB3/V3 stores no p/m/n triangle for those —
   only render type 0 carries one.  Every such face therefore reads back with a zero-area
   mapping frame, and the destination's SD raster draws it to nothing.  The model still
   animates, still bounds, still picks, and the raster still counts the faces as *drawn*.
   `backport_texture_mapping_to_simple` in `cp_import.c` now converts those frames on import,
   so a textured model is at least visible; but see 2.
2. **Even visible, the result is wrong.**  A procedural material graph has no faithful
   sprite-texture equivalent, so the map resolves by average HSL and then the SD raster
   stretches that one texture across every face.  The Steel titan came out as brown rock, the
   unicorn as a dark blob, the pyrelord as a black smudge.  Under `face_colour` they are a
   grey steel titan, a white unicorn and a red pyrelord — the models as authored.

`face_colour` reproduces the pre-texture-map lane byte-for-byte (verified against commit
`3eb10a0d81` for 139 of the 145 re-authored models; the other six postdate it).

Re-author the lane with:

```sh
make -C 3rd/rscache/tools cachepack
for manifest in docs/summoning_port/*_530.ini; do
  case "$(basename "$manifest")" in corpus_cohort_530.ini|roster_assets_530.ini) continue;; esac
  3rd/rscache/tools/cachepack/cachepack import --manifest "$manifest" --apply --textures-only
done
make -C src torirsserver-cache-summoning
```

The two skipped manifests write **review-only** lanes and must not be re-run: `corpus_cohort`
authors 121 unadmitted models, and `roster_assets` rewrites the frozen roster, which
`test_summoning_phase5a` rejects as "preserved review-only source fingerprint changed".

`texture_map_530_to_239.ini` is retained, not deleted: it is still the approval record below,
and it is what a future `material_mode=mapped_texture` manifest would consume.  Nothing in the
shipping lane reads it while `face_colour` is in force.

## Review procedure

For each admitted model, render a source/target pair twice:

```sh
3rd/rscache/tools/anim_compare/anim_compare \
  --a-rev rs530 --a-cache /Users/matthewevers/Documents/git_repos/2009scape/Server/data/cache \
  --a-seq <source-seq> --a-model <source-model> \
  --b-cache cache.osrs239.summoning --b-rev osrs239 \
  --b-seq <target-seq> --b-model <target-model> \
  --frames 0-0 --size 256x256 --by-label --sheet --out build/summoning-texture-review/<name>/by-label

# Repeat without --by-label for the material comparison.
```

The rig-labelled pair must agree before material work begins.  A material candidate is
accepted only when it preserves the intended readable surface in the ordinary-material pair;
the visual reviewer writes the texture-map row and changes the corresponding model ledger
entry from `unreviewed` to `ok`.  Do not infer approval from a matching average HSL, a passing
cache round-trip, or an untextured render.

`make -C src summoning-texture-review` performs this procedure for every model in every
dedicated `summoning_*_530.map` ledger.  It writes paired `by-label/sheet.bmp` and
`material/sheet.bmp` files plus an `index.json` under `build/summoning-texture-review/`.
The generated index is evidence only; it does not change approval state.

## Updating an approved mapping

**Only reachable from a manifest that states `material_mode=mapped_texture`** — see the
setting section at the top.  No shipping cohort manifest does today, so editing the table
below changes nothing on its own.

Edit `texture_map_530_to_239.ini` (one `source_material=target_material` row per source id),
then reapply only the model materials. This intentionally does not regenerate configs, scripts,
or other closure assets:

```sh
make -C 3rd/rscache/tools cachepack
for manifest in docs/summoning_port/*_cohort_530.ini \
                docs/summoning_port/clockwork_cat_cohort_530.ini; do
  3rd/rscache/tools/cachepack/cachepack import \
    --manifest "$manifest" --apply --textures-only
done
make -C src torirsserver-cache-summoning
```

The importer requires exactly 680 rows, and refuses a textured model whose source material is
not mapped. Re-run `make -C src summoning-texture-review` when a visual comparison is needed.

## Approved Dreadfowl mapping

| cohort | source model / seq | target model / seq | rig-labelled result | material result | ledger state |
|---|---|---|---|---|---|
| Dreadfowl | 30429 / 5386 | 120000 / 23000 | reviewed | approved source-material map | `ok` |
| Dreadfowl | 31147 / 5386 | 120001 / 23000 | reviewed | approved source-material map | `ok` |
| Dreadfowl | 30664 / 5386 | 120002 / 23000 | reviewed | approved source-material map | `ok` |

The approved rows use the ten explicit mappings in `dreadfowl_cohort_530.ini`.  The complete
approval table is `texture_map_530_to_239.ini`; it covers all 680 source material ids and is
loaded automatically by every Summoning manifest that leaves `material_mode` unstated.
`cachepack import` refuses to write a model if even one textured face lacks a row, so the
approved import cannot leak a source material id into the destination cache.

**Superseded in practice.**  These rows were approved from `--by-label` and `material` sheets
rendered by `anim_compare`, which does not draw through the destination's SD raster — so they
could not show what the mapped textures actually did in the client, which was to erase the
model.  Dreadfowl survived the switch to `mapped_texture` looking correct only because almost
none of its faces are textured; the heavily-textured familiars did not.  The lane is authored
`material_mode=face_colour` and these mappings are inert.  Any future re-approval must judge
the material result from a **client screenshot**, not an `anim_compare` sheet — that is the
concrete form of the "do not infer approval from an untextured render" rule above.

Generated artifacts are intentionally kept under `build/summoning-texture-review/`; they are
review evidence, not authored cache content. The packet contains 235 model pairs (470 sheets),
all approved by the global mapping decision.
