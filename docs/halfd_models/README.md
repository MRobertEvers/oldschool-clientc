# OB_TORI in the client — screenshots

Evidence for [`../HD_KERNELS.md`](../HD_KERNELS.md) §4: the non-stock OB_TORI
container travelling all the way from the porting tool into the packed cache and
out through the client's own model loader.

## The route

```
rs2012_qbd_obtori            per-face kernel from the mesh -> *.obtori in the lane
tools/stage_rs2012_overlay   staged with the other model assets
cachepack (cp_assets.c)      prefers <name>.obtori over <name>.ob3, packs bytes
RSCache_ModelNewDecode*      sniffs the magic, unwraps, attaches face_kernels
ToriRS_Model -> ToriDraw_Model
toridraw_raster.u.c          per-face switch overrides the texture's flags
```

Two of those are new cache routes: the packer preferring the container, and the
model decoder unwrapping it. Both are additive — a tree with no `.obtori` packs
and decodes exactly as before, and a stock cache never contains one.

## The pictures

| | |
|---|---|
| [qbd_ob3_vs_obtori.png](qbd_ob3_vs_obtori.png) | the model, routed per texture then per face, with the difference mask. **4.3% of pixels**, landing on the frill and crest cards. |
| [qbd_ob3.bmp](qbd_ob3.bmp) / [qbd_obtori.bmp](qbd_obtori.bmp) | the two halves at full size |
| [client_obtori/frame.png](client_obtori/frame.png) | the arena in the client, booted from a cache whose QBD models are OB_TORI containers |
| [client_ob3/frame.png](client_ob3/frame.png) | the same camera on the OB3 cache |

## Read the two client frames carefully

They are **not** a clean before/after, and it would be dishonest to present them
as one. They were captured either side of a bake change, and the earlier pair
shows a regression that has since been backed out (see below). What the
`client_obtori` frame does establish is that a cache containing OB_TORI models
boots, loads and renders normally — the container is not rejected anywhere along
the route.

What it does **not** show is the dragon: the harness camera is parked at a yaw
that frames the platform, and the QBD is asleep behind it at this angle. The
model-level comparison above is the evidence for what the routing changes; the
client frame is the evidence that it survives the cache.

## The regression these frames caught

An earlier pair in this folder's history was rendered from a lane baked with
`--detail-textures`, which lifts the ground-mesh fallback for every greyscale
material and routes all 204 HD programs through the detail kernel. On the QBD
that is an improvement; across the whole arena it is not — the floor and props
came back as green and white striping, exactly the failure
`RS2012_BACKPORT.md` §2 records for referencing those materials.

The lane is baked with `--alpha-textures` alone again, which is what the current
frames show. `--detail-textures` remains available and remains off: it needs the
per-material classification question answered first, not a blanket switch.

## Regenerating

```sh
src/build_win64_opt/rs2012_material_bake.exe --alpha-textures --apply
for m in 70260 69766 70267 70268 70761 69765; do \
  src/build_win64_opt/rs2012_qbd_obtori.exe --model $m \
    --out OSRS-Content/osrs239-content/models/ported/rs2012_qbd_td; done
python tools/stage_rs2012_overlay.py --tree OSRS-Content/osrs239-content --out build/rs2012-overlay
3rd/rscache/tools/cachepack/cachepack.exe pack --src build/rs2012-overlay \
  --base cache.osrs239 --out cache.osrs239.rs2012.obtori --rev osrs239 --assets --binary --gamevals
```

```powershell
.\tools\qbd_shot.ps1 -Manifest manifest_obtori.ini -Out docs\halfd_models\client_obtori -TexDebug
```
