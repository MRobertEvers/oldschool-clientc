# Painter sweeps: bucket vs world3d

Same-frame A/B of `painter_paint_bucket` against `painter_paint_world3d` (the
reference cascade), 2026-08-19, after the seam exception was narrowed to the
lateral gate + ground-only release (`src/painters/painters_bucket.u.c`,
`docs/painter_bucket_vs_world3d.md` "The seam exception"). Every frame here
was rendered twice in the SAME tick — world3d, then bucket — so a pixel diff is
the painter alone; since every final sweep is 0 px, one frame per view is kept
(the world3d one; the bucket frame is byte-identical).

## How they were made

```
SDL_VIDEODRIVER=dummy TORIRSSERVER_ALLOW_STALE_SCRIPTS=1 TORIRSSERVER_SAVES=<scratch> \
TORIRS_SIM_CMD="200,tob <room>;280,tobgo" TORIRS_MAX_FRAMES=430 \
TORIRS_PAINTER_ALT=1 TORIRS_BMP_SERIES=<dir>,420,1,1 \
TORIRS_WEDGE_CAM=<x,y,z,pitch,yaw> \
./src/torirs --manifest manifests/manifest_osrs239_torirs.ini --user probe1 --pass test
```

`TORIRS_PAINTER_ALT=1` makes `TORIRS_BMP_SERIES` also write
`frame_N_bucket.bmp` from the same frame. QBD uses
`TORIRS_SIM_CMD="200,rs2012qbdmanifest"`. Cameras: `n<yaw>` is the in-game
geometry (player - (0,-538,1139) rotated by yaw, pitch 130), `f<yaw>` is zoomed
out (D 2600, 1500 up, pitch 220). Yaw is 0..2047, 0 = looking north.

## Final sweeps (unified rule) — all 0 px

| scene | image | TORIRS_WEDGE_CAM | diff px |
|---|---|---|---|
| Xarpus (player L1, 50,52) | [tob/xarpus_n0.png](tob/xarpus_n0.png) | `6464,-1322,5581,130,0` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_n256.png](tob/xarpus_n256.png) | `5658,-1322,5914,130,256` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_n512.png](tob/xarpus_n512.png) | `5325,-1322,6720,130,512` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_n768.png](tob/xarpus_n768.png) | `5658,-1322,7525,130,768` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_n1024.png](tob/xarpus_n1024.png) | `6464,-1322,7859,130,1024` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_n1280.png](tob/xarpus_n1280.png) | `7269,-1322,7525,130,1280` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_n1536.png](tob/xarpus_n1536.png) | `7603,-1322,6720,130,1536` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_n1792.png](tob/xarpus_n1792.png) | `7269,-1322,5914,130,1792` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_f0.png](tob/xarpus_f0.png) | `6464,-2284,4120,220,0` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_f256.png](tob/xarpus_f256.png) | `4625,-2284,4881,220,256` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_f512.png](tob/xarpus_f512.png) | `3864,-2284,6720,220,512` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_f768.png](tob/xarpus_f768.png) | `4625,-2284,8558,220,768` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_f1024.png](tob/xarpus_f1024.png) | `6464,-2284,9320,220,1024` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_f1280.png](tob/xarpus_f1280.png) | `8302,-2284,8558,220,1280` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_f1536.png](tob/xarpus_f1536.png) | `9064,-2284,6720,220,1536` | 0 |
| Xarpus (player L1, 50,52) | [tob/xarpus_f1792.png](tob/xarpus_f1792.png) | `8302,-2284,4881,220,1792` | 0 |
| Bloat (54,50) | [tob/bloat_n0.png](tob/bloat_n0.png) | `6976,-778,5325,130,0` | 0 |
| Bloat (54,50) | [tob/bloat_n256.png](tob/bloat_n256.png) | `6170,-778,5658,130,256` | 0 |
| Bloat (54,50) | [tob/bloat_n512.png](tob/bloat_n512.png) | `5837,-778,6464,130,512` | 0 |
| Bloat (54,50) | [tob/bloat_n768.png](tob/bloat_n768.png) | `6170,-778,7269,130,768` | 0 |
| Bloat (54,50) | [tob/bloat_n1024.png](tob/bloat_n1024.png) | `6976,-778,7603,130,1024` | 0 |
| Bloat (54,50) | [tob/bloat_n1280.png](tob/bloat_n1280.png) | `7781,-778,7269,130,1280` | 0 |
| Bloat (54,50) | [tob/bloat_n1536.png](tob/bloat_n1536.png) | `8115,-778,6464,130,1536` | 0 |
| Bloat (54,50) | [tob/bloat_n1792.png](tob/bloat_n1792.png) | `7781,-778,5658,130,1792` | 0 |
| Bloat (54,50) | [tob/bloat_f0.png](tob/bloat_f0.png) | `6976,-1740,3864,220,0` | 0 |
| Bloat (54,50) | [tob/bloat_f256.png](tob/bloat_f256.png) | `5137,-1740,4625,220,256` | 0 |
| Bloat (54,50) | [tob/bloat_f512.png](tob/bloat_f512.png) | `4376,-1740,6464,220,512` | 0 |
| Bloat (54,50) | [tob/bloat_f768.png](tob/bloat_f768.png) | `5137,-1740,8302,220,768` | 0 |
| Bloat (54,50) | [tob/bloat_f1024.png](tob/bloat_f1024.png) | `6976,-1740,9064,220,1024` | 0 |
| Bloat (54,50) | [tob/bloat_f1280.png](tob/bloat_f1280.png) | `8814,-1740,8302,220,1280` | 0 |
| Bloat (54,50) | [tob/bloat_f1536.png](tob/bloat_f1536.png) | `9576,-1740,6464,220,1536` | 0 |
| Bloat (54,50) | [tob/bloat_f1792.png](tob/bloat_f1792.png) | `8814,-1740,4625,220,1792` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_n0.png](tob/sotetseg_n0.png) | `7104,-818,5581,130,0` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_n256.png](tob/sotetseg_n256.png) | `6298,-818,5914,130,256` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_n512.png](tob/sotetseg_n512.png) | `5965,-818,6720,130,512` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_n768.png](tob/sotetseg_n768.png) | `6298,-818,7525,130,768` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_n1024.png](tob/sotetseg_n1024.png) | `7104,-818,7859,130,1024` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_n1280.png](tob/sotetseg_n1280.png) | `7909,-818,7525,130,1280` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_n1536.png](tob/sotetseg_n1536.png) | `8243,-818,6720,130,1536` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_n1792.png](tob/sotetseg_n1792.png) | `7909,-818,5914,130,1792` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_f0.png](tob/sotetseg_f0.png) | `7104,-1780,4120,220,0` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_f256.png](tob/sotetseg_f256.png) | `5265,-1780,4881,220,256` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_f512.png](tob/sotetseg_f512.png) | `4504,-1780,6720,220,512` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_f768.png](tob/sotetseg_f768.png) | `5265,-1780,8558,220,768` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_f1024.png](tob/sotetseg_f1024.png) | `7104,-1780,9320,220,1024` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_f1280.png](tob/sotetseg_f1280.png) | `8942,-1780,8558,220,1280` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_f1536.png](tob/sotetseg_f1536.png) | `9704,-1780,6720,220,1536` | 0 |
| Sotetseg (55,52) | [tob/sotetseg_f1792.png](tob/sotetseg_f1792.png) | `8942,-1780,4881,220,1792` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_n0.png](qbd/arena_n0.png) | `6336,-826,5581,130,0` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_n256.png](qbd/arena_n256.png) | `5530,-826,5914,130,256` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_n512.png](qbd/arena_n512.png) | `5197,-826,6720,130,512` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_n768.png](qbd/arena_n768.png) | `5530,-826,7525,130,768` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_n1024.png](qbd/arena_n1024.png) | `6336,-826,7859,130,1024` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_n1280.png](qbd/arena_n1280.png) | `7141,-826,7525,130,1280` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_n1536.png](qbd/arena_n1536.png) | `7475,-826,6720,130,1536` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_n1792.png](qbd/arena_n1792.png) | `7141,-826,5914,130,1792` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_f0.png](qbd/arena_f0.png) | `6336,-1788,4120,220,0` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_f256.png](qbd/arena_f256.png) | `4497,-1788,4881,220,256` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_f512.png](qbd/arena_f512.png) | `3736,-1788,6720,220,512` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_f768.png](qbd/arena_f768.png) | `4497,-1788,8558,220,768` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_f1024.png](qbd/arena_f1024.png) | `6336,-1788,9320,220,1024` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_f1280.png](qbd/arena_f1280.png) | `8174,-1788,8558,220,1280` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_f1536.png](qbd/arena_f1536.png) | `8936,-1788,6720,220,1536` | 0 |
| QBD arena (player L1, 49,52) | [qbd/arena_f1792.png](qbd/arena_f1792.png) | `8174,-1788,4881,220,1792` | 0 |
| Maiden, stair camera | [tob/maiden_stairs_cam.png](tob/maiden_stairs_cam.png) | `6208,-818,8115,130,1024` | 0 |
| Nylocas, default camera | [tob/nylocas_default.png](tob/nylocas_default.png) | `(in-game)` | 0 |
| Xarpus, default camera | [tob/xarpus_default.png](tob/xarpus_default.png) | `(in-game)` | 0 |
| Verzik, default camera | [tob/verzik_default.png](tob/verzik_default.png) | `(in-game)` | 0 |
| Sotetseg, far yaw 256 | [tob/sotetseg_f256.png](tob/sotetseg_f256.png) | `6299,-1780,4120,220,256` | 0 |
| QBD arena, default camera | [qbd/qbd_default.png](qbd/qbd_default.png) | `(in-game)` | 0 |

## Defect evidence (`defects/`)

What the bucket painter drew BEFORE the fix, same-frame against world3d:

| image | what it shows |
|---|---|
| `xarpus_default_world3d.png` / `xarpus_default_bucket_old_exception.png` / `xarpus_default_diff.png` | Xarpus default camera. Old any-axis seam exception: 2041 px differ. The 6x5 ledge `32748` (x[43,48] z[67,71]) paints over the floor in front of its z=67 row. |
| `xarpus_ledge_zoom_world3d_over_bucket.png` | Zoom of that ledge: top world3d (mossy floor), bottom bucket (grey ledge slab covering it). |
| `maiden_stairs_world3d.png` / `maiden_stairs_bucket_old_exception.png` / `maiden_stairs_diff.png` | Maiden, camera `6208,-818,8115,130,1024`. 586 px: the 4x1 stair landing `32804` paints over the 1x1 steps in front of it. |
| `maiden_stairs_zoom_world3d_left_bucket_right.png` | Zoom: world3d's jagged step ends over the landing vs the bucket's landing over the steps. |
| `xarpus_default_bucket_lateral_only_no_hold.png` / `xarpus_barrier_edge_zoom_lateral_only_no_hold.png` | Lateral-only rule WITHOUT the ground-only hold: 757 px — the barrier `(49,51)` drew before the 6x5 ledge beside it whose far row abuts it, and the ledge covered the barrier's edge (zoom: left pair world3d/bucket at the barrier's west end, right pair at its east end). This is why `TilePaint.seam_relaxed` exists. |

