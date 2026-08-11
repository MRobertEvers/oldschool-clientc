# Summoning texture review

Rev-530 textures are procedural material graphs; osrs239 textures are sprite-backed records.
There is no lossless id conversion.  This file is the approval record for the deliberately
human part of the Summoning port: no `signoff` value becomes `ok` without a reviewer looking
at both the rig-labelled and material renders.

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
make -C src mock230-cache-summoning
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
loaded automatically by every Summoning manifest. `cachepack import` refuses to write a model
if even one textured face lacks a row, so the approved import cannot leak a source material id
into the destination cache.

Generated artifacts are intentionally kept under `build/summoning-texture-review/`; they are
review evidence, not authored cache content. The packet contains 235 model pairs (470 sheets),
all approved by the global mapping decision.
