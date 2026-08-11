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
The index is deliberately marked `unreviewed`; the command is a completeness check for the
review packet, not approval.

## Initial rendered pair — not an approval

| cohort | source model / seq | target model / seq | rig-labelled result | material result | ledger state |
|---|---|---|---|---|---|
| Spirit wolf | 30443 / 8297 | 100000 / 20000 | aligned, frame 0 | aligned, frame 0 | `unreviewed` |

Generated artifacts are intentionally kept under `build/summoning-texture-review/`; they are
review evidence, not authored cache content.  The first complete packet contains 235 model
pairs (470 sheets), all still `unreviewed`.  The source and target have equal geometry for this
initial pair (353 vertices, 702 faces, 76 transforms, 17 frames).  It establishes that the
renderer and pair selection work, not that a rev-530 material has been mapped to an osrs239
texture.
