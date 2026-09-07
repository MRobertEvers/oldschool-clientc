# Sailing unit gates

**Three sections, oldest first. The last one is current** — the final pair is
`src/torirs 5e34b81f…` / `src/build_opt/torirsserver 6a0e7f04…` against pack
`902a6b8d…`, rebuilt 09:35 on 2026-09-07, twelve targets all exit 0. Jump to
*"Final pair `5e34b81f`"* at the end.

This first section is the 25-target run against the **2026-09-06 21:38 pair**
`c7e62cce` / `a53899eb`; the middle section is the ten-target re-stamp against
the **08:08 pair** `4854e303` / `214b43d7`.

Machine-readable form: `docs/sailing_validation/client/gates.json`
(read the `final_pair_5e34b81f_2026_09_07` block for the current run).
Raw logs for this first section: `/tmp/sailing-final-gates/<target>.log`.

Run by the `gates` worker, the one worker permitted to run make test targets in
the shared objdirs. **Every make ran serially — never two at once.**

## Provenance — the 2026-09-06 21:38 pair (superseded twice)

| artifact | sha256 |
|---|---|
| `src/torirs` | `c7e62cce180aaeb6bc4cd044818b0209ca99edb478f5280c3ebd10d6b79b1d8d` |
| `src/build_opt/torirsserver` | `a53899eb8ce5d086e36df3653aaccfd2ee93c05f8e767ece05ecc40a94b9c3db` |
| `OSRS-Content/osrs239-content/server/scripts/build/script.dat` | `902a6b8d242bba01990b3955100e301c2a0edb6b1f18fc6ca83fd703a880df59` |

All three matched the hashes handed to this worker **before the first make and
again after the last**. No gate relinked a shared binary: each unit gate compiles
its own executable into the objdir, and the shared client/server link rules were
never invoked.

- `make -C src EMBED_SERVER=1 <target>` → objdir `src/build_opt_es` (OPT defaults to 1).
- `make -C src OPT=0 EMBED_SERVER=1 <target>` → objdir `src/build_es`, asserts live.

> **Durations are not performance evidence.** Several workers were driving clients
> and servers on this machine throughout. They are recorded only so a later run can
> tell a 2-second gate from a 20-second one. A later phase measures alone.

## Results

25 of 25 requested targets exist in `src/makefile`; **none is missing**. **23 PASS, 2 FAIL.** Both OPT=0 reruns PASS.

| # | target | exit | status | dur (s) | pass line / first failure |
|---|---|---|---|---|---|
| 1 | `test-wev` | 0 | PASS | 2.6 | wev_test: all passed |
| 2 | `test-wev-rebuild` | 0 | PASS | 5.8 | wev_rebuild_test: all passed |
| 3 | `test-net-exec` | 0 | PASS | 7.1 | net-exec: all tests passed |
| 4 | `test-wev-visibility` | 0 | PASS | 0.7 | native world-entity visibility/camera: 0 failures |
| 5 | `test-wev-population` | 0 | PASS | 2.2 | flat view population: own/borrowed actors and graphics removed; runtime scenery retained; next-frame restoration PASS |
| 6 | `test-painters-world-entity` | 0 | PASS | 1.4 | all painter world-entity tests passed |
| 7 | `test-frame-flat` | 0 | PASS | 1.1 | frame flat live geometry, scale, colour, no-pick, replay lifetime and root identity PASS |
| 8 | `test-sailing-paint-order` | 0 | PASS | 0.6 | scene dependencies: flat before full, parent ground before boat; nested ownership, raised shore, loc order and zero-boat bytes PASS |
| 9 | `test-pick-level` | 0 | PASS | 1.6 | OK: pick_level_test |
| 10 | `test-minimenu-world` | 0 | PASS | 2.4 | All tests passed. |
| 11 | `test-sailing-navigation` | 0 | PASS | 2.0 | PASS sailing compass sectors and camera-plane ray projection |
| 12 | `test-cs2-worldentity-limit` | 0 | PASS | 3.4 | native 7900/7901 default30, setter/getter, zero and nonnegative clamp PASS |
| 13 | `test-cs2-array-fill` | 0 | PASS | 1.5 | native ARRAY_FILL int/string/null, partial/clamped/overflow ranges and stack balance PASS |
| 14 | `test-cs2-sailing-settings` | 0 | PASS | 3.3 | PASS native cargo privacy0..2 through script3852 label writes, the8830/3967 paths, isolated script/varbit/struct scope and last-choice coalescing |
| 15 | `test-cs2-host-request-kinds` | 0 | PASS | 0.6 | host request kind contract: 655 exact, unique kinds in opcode order |
| 16 | `test-cs2-host-request-producers` | 0 | PASS | 1.5 | host request producer replay: 655 exact kinds and CC/IF input payloads preserved across yield/retry |
| 17 | `test-mock239-playerinfo` | 0 | PASS | 2.8 | mock239-playerinfo: all tests passed |
| 18 | `test-sailing-collision` | 0 | PASS | 6.8 | sailing_collision_test: passed (0 failures) |
| 19 | `test-entity-info-shrink` | 0 | PASS | 11.8 | ALL PASS |
| 20 | `test-scanline` | 0 | PASS | 2.7 | all scanline variants agree with their branching counterparts |
| 21 | `test-rsprot-bridge` | 0 | PASS | 1.4 | rsprot-bridge: rsprot and osrs239_parse agree on every migrated packet |
| 22 | `test-social` | 0 | PASS | 6.0 | social: all checks passed |
| 23 | `test-uitree` | 0 | PASS | 6.9 | All UITree tests passed. |
| 24 | `test-world` | 2 | FAIL | 2.7 | FAIL: sealed dest: ring3 gives up (world/test/world_test_route.c:131) |
| 25 | `test-torirsserver-embed` | 2 | FAIL | 18.2 | embed: SYNTH_SOUND reached alice's stream (packetin.h:102 is no longer PKT_NAME_NONE) FAILED |

### OPT=0 reruns (asserts live)

| target | exit | status | dur (s) | pass line |
|---|---|---|---|---|
| `test-minimenu-world` | 0 | PASS | 17.2 | All tests passed. |
| `test-wev-rebuild` | 0 | PASS | 1.1 | wev_rebuild_test: all passed |

`test-world` was also rerun at OPT=0 as a diagnostic (not part of the requested
set): **exit 0, `All tests passed.`** — see the failure section below for why that
matters.

## Failures

### `test-world` — exit 2 — newly recorded here, defect pre-existing at HEAD

First failure:

```
FAIL: sealed dest: ring3 gives up (world/test/world_test_route.c:131)
```

All three failures, in order:

```
FAIL: sealed dest: ring3 gives up (world/test/world_test_route.c:131)
FAIL: sealed 5x5: ring3 finds nothing (world/test/world_test_route.c:186)
FAIL: far sealed dest: box10_rect declines rather than half-walking (world/test/world_test_route.c:217)
3 failure(s)
```

**Diagnosis.** collision_nearest_opts_from_model() in src/engine/world_builder/collision_map.c fills range, max_dist and rank_by_rect_distance in every arm but never writes out->unbounded. Callers that declare `struct CollisionNearestOpts` on the stack and do not set the field themselves read indeterminate memory. All three failures are exactly the 'the click declines' cases that a non-zero unbounded flips into a route.

**Proof.** Private probe /tmp/sailing-final-gates/probe/probe.c (compiled against the tree's own collision_map.c + features.c, no repo source edited): poisoning the opts struct with 0xAB before the call yields unbounded=-1414812757 and len=2 for the sealed 3x3 destination; memset-zeroing the same struct yields unbounded=0 and len=-1. test-world also passes at OPT=0 (make -C src OPT=0 EMBED_SERVER=1 test-world, exit 0, 'All tests passed.') and fails at OPT=1 - the signature of an uninitialised read, not a logic change.

**Not a sailing regression.** The defect is in committed code: `git show HEAD:src/engine/world_builder/collision_map.c` has the same function with no `unbounded` write, and collision_map.c is clean in the working tree (git status reports no modification). It is NOT introduced by the uncommitted sailing work. The last commit to touch collision_map.c, d2ebd63f2 'sailing: footprint reads ops 8/9 as size, exact-angle AABB', only ADDS collision_map_set_water and does not touch this function.

**Production reach** (this is not a test-only defect):

- src/world/entity_pathing.c:280 - collision_nearest_opts_from_model(TORIRS_NEAREST_RING3_STEPS, &nearest) on an uninitialised local, `unbounded` never set. This is the client's entity-pathing jump route.
- src/torirsserver/torirs_server_scene.c:2434 ToriRSServer_SceneOpNearestOpts - sets range/max_dist/rank_by_rect_distance on the caller's struct and never writes unbounded.
- src/app.c:20178 and src/torirsserver/torirs_server_scene.c:2451 ToriRSServer_SceneGroundNearestOpts are SAFE: both assign out->unbounded explicitly right after the call.

**Fix, not applied here.** Set out->unbounded = 0 in every arm of collision_nearest_opts_from_model (the header calls it THE place the numeric encoding of each model lives), and have ToriRSServer_SceneOpNearestOpts write the field too. This worker is not permitted to edit C sources; root must make the change and rebuild the shared binaries.

Owner: **root**.

### `test-torirsserver-embed` — exit 2 — known, unchanged

First failure:

```
embed: SYNTH_SOUND reached alice's stream (packetin.h:102 is no longer PKT_NAME_NONE) FAILED
```

This is the long-standing decode break named in the task. The first failure is byte-for-byte the expected one and nothing before it fails: no line before it is marked FAILED (the first FAILED in the log is line 116), and the checks immediately ahead of it pass, including 'SS_OP_SOUND_SYNTH dispatched (torirs_server_scripts.c's own case, not a stand-in)'. Counts in this run: 80 ok, 47 FAILED. The whole tail is downstream of the same broken decode - once alice's stream mis-parses, every later 'reached X's stream' check fails with it.

**Changed since the handoff: no.** The first failure is byte-for-byte the one
the task names, and nothing ahead of it fails.

Owner: **not this phase**.

## Source provenance per gate

Per gate: the sha256 of the gate's own TEST source(s), and of the production
sources it exercises. The production list is derived by resolving each test
source's project-local `#include`s to their implementation file — a `.h` to its
sibling `.c`, and a unity `.u.h` or an X-macro `.def` to itself, because for those
the header *is* the production source. Files a gate names directly in its makefile
recipe are included too.

Gates that link the whole client object set (`$(OBJS)`) are not listed here as
"everything": the include closure is what the test actually reaches for, and that
is the list that tells a future reader whether a source change should have moved
this gate.

### `test-wev`

| role | file | sha256 |
|---|---|---|
| test | `src/world/test/wev_test.c` | `17706250b7c74760ef955a103bfdc2a5fbeac2106e66f0ba5b66aa3d3c295cd4` |
| production | `3rd/rscache/src/dat2disk.c` | `20943bcd34311d04c9c9b923c383806955b6c3a84598e34059dab73d1d8c8310` |
| production | `3rd/rscache/src/filelist.c` | `20aba433fa92aaee1680b89d9f216fc66c792db5baeab22bd6f05994de9218c7` |
| production | `3rd/rscache/src/revisions/revisions.c` | `b47fa94201853f5032ca55f80fecc825a2f40d14713c549dbcb076b8241cbb30` |
| production | `src/world/wev.c` | `8dedfec17ce9d058b3cb685d12ebb93eaf88a0a61faea56670b4d684c5d33d27` |

### `test-wev-rebuild`

| role | file | sha256 |
|---|---|---|
| test | `src/world/test/wev_rebuild_test.c` | `fe6ffe17fffd33da4b3d54a1a18d69e82226a56085b885694a77f3402c2c6d91` |
| production | `src/app.c` | `09ab5dd24504426b71e1a8d27c155f0cd72db51a2b8388c088697d88e63b645a` |
| production | `src/engine/cache_provider.c` | `34b0b1c4ff8e8ae7ee156ca7dc7c15de95df3f36e4ab94feb68e4aab046758bc` |
| production | `src/engine/dat2/dat2_buildcache.c` | `443eb6cacd0ae7ace88cae50fa1a8a6efb0459bbc47c8413d282242f9395ac4e` |
| production | `src/engine/torirs_types.c` | `9fba9a458e1dafc40d5de31b0a9e52b94eae4d13c3b88ec0185f2ca89880763f` |
| production | `src/engine/world_builder/heightmap.c` | `ac5e8c5c5b70a935d2123d4c608d45ec7026cc42e7f2ca73e317655dbff27e40` |
| production | `src/engine/world_builder/task_world_load.c` | `1fb3eacd82affe4378f4975efe270939cb06f74d0220d768ddf2ec68dcf632b0` |
| production | `src/engine/world_builder/world_builder.c` | `1199419e9a642800e582304d3be7a041e78bb17b65cf1c3f385f910d08026d8a` |
| production | `src/net/rev/gameproto_parse.c` | `433d1404b82f74032848547c9f59f11b43c245eaa206d4070118f170b70f98c1` |
| production | `src/platform/platform_x_io.c` | `e5f29aa335f8f0a80048bba414654628297d07d975096b7ec28ac9c432ca662a` |
| production | `src/varp/varp_manager.c` | `c488eade717b192c816b85341ab1d869db194d01508228f559875d1a3f5cfcdb` |
| production | `src/world/world.c` | `6ca78175b23ebdaf3027b3a07975bb846765ae212ffc6daf6994ea6ecda04d4e` |
| production | `src/world/worldview.c` | `cbee6c5f934019f6f92329a0101c8f9276497df6166655145d1aa2c4e61628e0` |

### `test-net-exec`

| role | file | sha256 |
|---|---|---|
| test | `src/game/test/rs_gameproto_exec_test.c` | `e13821343923bd6fd8f20d74d86bac8e3fe090846c38daf677e232db449271af` |
| production | `src/app.c` | `09ab5dd24504426b71e1a8d27c155f0cd72db51a2b8388c088697d88e63b645a` |
| production | `src/game/rs_chat.c` | `36d0f21e9d24c6f14f2d7aa6cc47b78c60b8b8de895504cfaa499a8792f7f0e6` |
| production | `src/game/rs_clientcode.c` | `c58cafc2640f3d1480570f6f72ff50b737ec532936e4f3815853098899a3c548` |
| production | `src/game/rs_gameproto_exec.c` | `e3634918e5604105b28098f8ec3b0ec60a16413759f1349244f0c6488065f571` |
| production | `src/game/rs_player_stats.c` | `de309925009d67bf062c3edf6291ac67daa138f265343a180a0f9d38ea7d3410` |
| production | `src/game/task_gameproto_exec.c` | `750706526aed1f61eb00a91fe1512176315a667449fd0ed84460a2e56b85cd8b` |
| production | `src/inv/inv_manager.c` | `b72c33b2e28b3f866a5b57d1c0708f7983ba1ec78040fadb0a33c33102f6ec33` |
| production | `src/net/rev/gameproto_parse.c` | `433d1404b82f74032848547c9f59f11b43c245eaa206d4070118f170b70f98c1` |
| production | `src/ui/uitree.c` | `68c14a0507d9c9292dc6eb5b58a6b2c6a65b9d432164a5ec70400f47874905e5` |
| production | `src/varp/varp_manager.c` | `c488eade717b192c816b85341ab1d869db194d01508228f559875d1a3f5cfcdb` |
| production | `src/world/world.c` | `6ca78175b23ebdaf3027b3a07975bb846765ae212ffc6daf6994ea6ecda04d4e` |

### `test-wev-visibility`

| role | file | sha256 |
|---|---|---|
| test | `src/world/test/wev_visibility_test.c` | `2ea13b24da836be88a71c98507474f925cc9f6e1a63c98ec598426b3b318cd86` |
| production | `src/world/wev.c` | `8dedfec17ce9d058b3cb685d12ebb93eaf88a0a61faea56670b4d684c5d33d27` |

### `test-wev-population`

| role | file | sha256 |
|---|---|---|
| test | `src/world/test/wev_population_test.c` | `808e210d58e550aa21ae36410e55b76bac4f547e3f024ec1051bfbb625a108b4` |
| production | `src/engine/world_builder/collision_map.c` | `b18fd644502fe4fe2ea5a41e00413519b3b1304240d5798dfa3f2e4d58ded974` |
| production | `src/engine/world_builder/heightmap.c` | `ac5e8c5c5b70a935d2123d4c608d45ec7026cc42e7f2ca73e317655dbff27e40` |
| production | `src/engine/world_builder/minimap.c` | `137dc3bc296e6945cf7365d5c7dc58d344d1e0edd2dc1fe0b45b10b3e564488c` |
| production | `src/features/features.c` | `8555d8325bab11b1955dd919194ed4fc08a6cdf977c5f526523a43c916310e65` |
| production | `src/painters/painters.c` | `b0743db585f2060fe20667a9d44a98fbf0f5003bd045cf2841c474eb0d8a51d4` |
| production | `src/painters/scene_occluders.c` | `d47b72cfb36e6d0500b9ed2c014278dbdcef8bb40a5bf9c251bd91f9616c2d2a` |
| production | `src/perf/torirs_perf.c` | `278bab111e96ad1061dcfe408a9bfce91aca78e9f5cb8eeeacc492eb35ad7271` |
| production | `src/world/entity_pathing.c` | `f61331cb4b1bbebc26ca6c23e98dcd4033aaff4a6f695f158762d6d4449e6370` |
| production | `src/world/entity_pool.c` | `2e174842a6ad181ffbec8a5cbe6f2d12bb8f8263380a4670a610b3099833f121` |
| production | `src/world/entity_registry.c` | `084b846bd19d5f1d580547254399284ebebc36fe2fca6e3758c5d173407fc322` |
| production | `src/world/wev.c` | `8dedfec17ce9d058b3cb685d12ebb93eaf88a0a61faea56670b4d684c5d33d27` |
| production | `src/world/world.c` | `6ca78175b23ebdaf3027b3a07975bb846765ae212ffc6daf6994ea6ecda04d4e` |
| production | `src/world/world_cycle.c` | `b9e4983d256aec7eed6cc967ec3605715b4867cda901a1025d77bf81db5e1972` |
| production | `src/world/world_entity.c` | `f9194c6bda36632858f0eddda40d47f9994341a22263143f7c4941cdfeb13661` |
| production | `src/world/world_pickset.c` | `1a6bfc6869a2afde214e8173b180bc9ea061a055472798f7d5477ad07fbd2fdf` |
| production | `src/world/worldview.c` | `cbee6c5f934019f6f92329a0101c8f9276497df6166655145d1aa2c4e61628e0` |

### `test-painters-world-entity`

| role | file | sha256 |
|---|---|---|
| test | `src/painters/test/painters_test_world_entity.c` | `22445fe6c47a0af94b5232f7b0408ab95ab82cb7241d20fe84be92b54fd5c447` |
| production | `3rd/toridraw/graphics/shared_tables.c` | `995ff3dcdd210d057bd8de22a19702980cd46a960a0ceb1ae79e20ecb7b600c3` |
| production | `src/painters/painters.c` | `b0743db585f2060fe20667a9d44a98fbf0f5003bd045cf2841c474eb0d8a51d4` |
| production | `src/painters/painters_cull_project.c` | `f350abece36e67ca8aca5c5524f29bf5285f1f9a528171704606cac00911a50c` |
| production | `src/painters/scene_occluders.c` | `d47b72cfb36e6d0500b9ed2c014278dbdcef8bb40a5bf9c251bd91f9616c2d2a` |
| production | `src/perf/torirs_perf.c` | `278bab111e96ad1061dcfe408a9bfce91aca78e9f5cb8eeeacc492eb35ad7271` |
| production | `src/world/wev.c` | `8dedfec17ce9d058b3cb685d12ebb93eaf88a0a61faea56670b4d684c5d33d27` |
| production | `src/world/wev_deck.c` | `a17c1692edc64b44f04a40c93cc02fe5cd35f60cf6af7b39275f97663912f356` |

### `test-frame-flat`

| role | file | sha256 |
|---|---|---|
| test | `src/render/test/torirs_frame_flat_test.c` | `ef20d65ac9196b8c246b96b1bc2dd4af0d88f144a0ffa57bcb71532e8e406dd7` |
| production | `3rd/toridraw/toridraw_model.c` | `166564daf7fac5a5c2264fc563b1550aa9e2c2961aba5068d8cff5ab1aa0aff7` |
| production | `3rd/toridraw/toridraw_scene.c` | `96b4f7c44a657b6eb820b2f00abafb71c05f0ca146a408ebc69c8520da0960c8` |
| production | `src/painters/painters.c` | `b0743db585f2060fe20667a9d44a98fbf0f5003bd045cf2841c474eb0d8a51d4` |
| production | `src/perf/torirs_perf.c` | `278bab111e96ad1061dcfe408a9bfce91aca78e9f5cb8eeeacc492eb35ad7271` |
| production | `src/render/torirs_arc.c` | `cc71e37bfaf0f5da25e81a1deadb33813d4b7d460df374a1681c62a733dc0a49` |
| production | `src/render/torirs_frame.c` | `0d70d4c7b20eb8e4844b2b259edde590346d45c76c692fc584ece32394cb814f` |
| production | `src/render/torirs_frame_flat.u.h` | `d1e914c833c96c6a52c525393d482331cac11b9cfb897290836e6cc50b7e9c03` |
| production | `src/ui/uitree_emit.c` | `9b56475e2a654beaad6b60b92b7ad7f6dce0b4f49aba8f1e53f7f46dcb5212ac` |
| production | `src/world/world.c` | `6ca78175b23ebdaf3027b3a07975bb846765ae212ffc6daf6994ea6ecda04d4e` |

### `test-sailing-paint-order`

| role | file | sha256 |
|---|---|---|
| test | `src/game/test/sailing_paint_order_test.c` | `0e3b3abf87372e5c93fab51f4f0c7d813be7d56c30a7c52e63bd37786b871571` |
| production | `src/game/sailing_paint_order.u.h` | `6758aacc08149331fe1921fe442b8f1db77f164355436d5a62c4bf7c3da59ee4` |

### `test-pick-level`

| role | file | sha256 |
|---|---|---|
| test | `src/render/test/pick_level_test.c` | `7a24f23dd871d96f140d60ed4bfb6ccfae3d10033893e778b8d16383576e0a0a` |
| production | `src/engine/world_builder/collision_map.c` | `b18fd644502fe4fe2ea5a41e00413519b3b1304240d5798dfa3f2e4d58ded974` |
| production | `src/engine/world_builder/heightmap.c` | `ac5e8c5c5b70a935d2123d4c608d45ec7026cc42e7f2ca73e317655dbff27e40` |
| production | `src/engine/world_builder/minimap.c` | `137dc3bc296e6945cf7365d5c7dc58d344d1e0edd2dc1fe0b45b10b3e564488c` |
| production | `src/features/features.c` | `8555d8325bab11b1955dd919194ed4fc08a6cdf977c5f526523a43c916310e65` |
| production | `src/painters/painters.c` | `b0743db585f2060fe20667a9d44a98fbf0f5003bd045cf2841c474eb0d8a51d4` |
| production | `src/painters/scene_occluders.c` | `d47b72cfb36e6d0500b9ed2c014278dbdcef8bb40a5bf9c251bd91f9616c2d2a` |
| production | `src/render/torirs_pick.c` | `d85dcc902a6b34e11c44dce0cb6a34b9b0bbe65eae424bfc1ac218b44a67234c` |
| production | `src/world/entity_pathing.c` | `f61331cb4b1bbebc26ca6c23e98dcd4033aaff4a6f695f158762d6d4449e6370` |
| production | `src/world/entity_pool.c` | `2e174842a6ad181ffbec8a5cbe6f2d12bb8f8263380a4670a610b3099833f121` |
| production | `src/world/entity_registry.c` | `084b846bd19d5f1d580547254399284ebebc36fe2fca6e3758c5d173407fc322` |
| production | `src/world/wev.c` | `8dedfec17ce9d058b3cb685d12ebb93eaf88a0a61faea56670b4d684c5d33d27` |
| production | `src/world/world.c` | `6ca78175b23ebdaf3027b3a07975bb846765ae212ffc6daf6994ea6ecda04d4e` |
| production | `src/world/world_cycle.c` | `b9e4983d256aec7eed6cc967ec3605715b4867cda901a1025d77bf81db5e1972` |
| production | `src/world/world_entity.c` | `f9194c6bda36632858f0eddda40d47f9994341a22263143f7c4941cdfeb13661` |
| production | `src/world/world_pickset.c` | `1a6bfc6869a2afde214e8173b180bc9ea061a055472798f7d5477ad07fbd2fdf` |
| production | `src/world/worldview.c` | `cbee6c5f934019f6f92329a0101c8f9276497df6166655145d1aa2c4e61628e0` |

### `test-minimenu-world`

| role | file | sha256 |
|---|---|---|
| test | `src/game/test/rs_minimenu_world_test.c` | `f6afb668814545732a6184b460ddcf42a27a3a3f49d80ffe0c32c318baf8a250` |
| production | `src/engine/torirs_objtype_from_rscache.c` | `7db85acb702e230859366066c177a136dd67ed5fbccdf4177477150ca0dea6aa` |
| production | `src/game/rs_minimenu_build.c` | `fc3b54753d4f01b852fd19c624075b4678c424347d80c658268072099ce37bc5` |
| production | `src/game/rs_minimenu_world.c` | `3f07b0c01e72d1cfd52424e8111fa4663bed8d449f915bc3f0cf065b9f284afc` |
| production | `src/revconfig/revconfig.c` | `d7d69dd9a5a221c3a76108cb0355ed89a2710ed358f99bb995b692702e07cf0d` |
| production | `src/ui/uitree_layout.c` | `7a77870f4bae68675af37fecbd13c5bb7a0afeb41a6a43e4ed13c9db5b04c08d` |
| production | `src/ui/uitree_minimenu.c` | `4a45bfcd5b4cfe57e4cb2d4ddc983712ec51a3ce32a2a88740184ef3377fbf47` |
| production | `src/world/wev.c` | `8dedfec17ce9d058b3cb685d12ebb93eaf88a0a61faea56670b4d684c5d33d27` |
| production | `src/world/world.c` | `6ca78175b23ebdaf3027b3a07975bb846765ae212ffc6daf6994ea6ecda04d4e` |
| production | `src/world/world_pickset.c` | `1a6bfc6869a2afde214e8173b180bc9ea061a055472798f7d5477ad07fbd2fdf` |

### `test-sailing-navigation`

| role | file | sha256 |
|---|---|---|
| test | `src/game/test/sailing_navigation_test.c` | `b9f1437b41d9f085890b4fe9af8e9d8745d6b8cf3f9fdc123b7106cc99ef5ffa` |
| production | `src/world/wev.c` | `8dedfec17ce9d058b3cb685d12ebb93eaf88a0a61faea56670b4d684c5d33d27` |

### `test-cs2-worldentity-limit`

| role | file | sha256 |
|---|---|---|
| test | `src/cs2vm2/test/worldentity_limit_test.c` | `2339541a5e001532878ee81b0aabb039ad2b3a8a33fc3b9a6d23874d527b40dc` |
| production | `src/cs2vm2/cs2vm2.c` | `0d3724e765b3851d11595bfc59feac426542beba7f0fba0438df9d58802e1bfe` |
| production | `src/cs2vm2/cs2vm2_script.c` | `e8c53b0d3692c25abb7cc8efc65607ce1b7c12fe364e0946d5453f5ac2e316cc` |
| production | `src/engine/cache_provider.c` | `34b0b1c4ff8e8ae7ee156ca7dc7c15de95df3f36e4ab94feb68e4aab046758bc` |
| production | `src/game/rs_cs2_host.c` | `7851f2617b6bde35733823a7dcf0c5aeecede89d7639a4243c27c74d83c0ca89` |
| production | `src/game/rs_worldmap.c` | `3b245d10d6051b4fa5d5dfed7f77e34e8460d5c40d98822c05037a28f46e54f3` |
| production | `src/inv/inv_manager.c` | `b72c33b2e28b3f866a5b57d1c0708f7983ba1ec78040fadb0a33c33102f6ec33` |
| production | `src/ui/uitree.c` | `68c14a0507d9c9292dc6eb5b58a6b2c6a65b9d432164a5ec70400f47874905e5` |

### `test-cs2-array-fill`

| role | file | sha256 |
|---|---|---|
| test | `src/cs2vm2/test/array_fill_test.c` | `84890a85273888aa45cf44051fce0077db2788595f6bb69bee93f7051ab72476` |
| production | `src/cs2vm2/cs2_opcode_meta.c` | `1fc8565e02df8362b328784a17938aa0983977cdd45917556d6e27e57d77caed` |
| production | `src/cs2vm2/cs2vm2.c` | `0d3724e765b3851d11595bfc59feac426542beba7f0fba0438df9d58802e1bfe` |
| production | `src/cs2vm2/cs2vm2_strpool.c` | `0d2eff84a28268b1e5b0bc8c3dadd712238fa1431e759d251e087d781b403043` |
| production | `src/perf/torirs_perf.c` | `278bab111e96ad1061dcfe408a9bfce91aca78e9f5cb8eeeacc492eb35ad7271` |

### `test-cs2-sailing-settings`

| role | file | sha256 |
|---|---|---|
| test | `src/cs2vm2/test/sailing_settings_test.c` | `d329424e8eb4d0a2e84914e8c9d960eff4f74b114e85bf5bbd489a2381a39852` |
| production | `src/cs2vm2/cs2vm2.c` | `0d3724e765b3851d11595bfc59feac426542beba7f0fba0438df9d58802e1bfe` |
| production | `src/cs2vm2/cs2vm2_script.c` | `e8c53b0d3692c25abb7cc8efc65607ce1b7c12fe364e0946d5453f5ac2e316cc` |
| production | `src/engine/cache_provider.c` | `34b0b1c4ff8e8ae7ee156ca7dc7c15de95df3f36e4ab94feb68e4aab046758bc` |
| production | `src/game/rs_cs2_host.c` | `7851f2617b6bde35733823a7dcf0c5aeecede89d7639a4243c27c74d83c0ca89` |
| production | `src/game/rs_worldmap.c` | `3b245d10d6051b4fa5d5dfed7f77e34e8460d5c40d98822c05037a28f46e54f3` |
| production | `src/inv/inv_manager.c` | `b72c33b2e28b3f866a5b57d1c0708f7983ba1ec78040fadb0a33c33102f6ec33` |
| production | `src/ui/uitree.c` | `68c14a0507d9c9292dc6eb5b58a6b2c6a65b9d432164a5ec70400f47874905e5` |
| production | `src/varp/varp_manager.c` | `c488eade717b192c816b85341ab1d869db194d01508228f559875d1a3f5cfcdb` |

### `test-cs2-host-request-kinds`

| role | file | sha256 |
|---|---|---|
| test | `src/cs2vm2/test/host_request_kinds_test.c` | `91894325dc25751015b833bdd39b528e9ab92da12a29e83c283b12857e16ba2e` |
| production | `src/cs2vm2/cs2vm2_host_request_kinds.def` | `42528067ef03424dbea2e1762c001f8fa50c32e56d281b9fe35dbd006a470436` |

### `test-cs2-host-request-producers`

| role | file | sha256 |
|---|---|---|
| test | `src/cs2vm2/test/host_request_producers_test.c` | `539a98ab33a18fe99d0fd19a1ac9eaf4755456893b801e096e7e0f4436cc8d20` |
| production | `src/cs2vm2/cs2_opcode_meta.c` | `1fc8565e02df8362b328784a17938aa0983977cdd45917556d6e27e57d77caed` |
| production | `src/cs2vm2/cs2vm2.c` | `0d3724e765b3851d11595bfc59feac426542beba7f0fba0438df9d58802e1bfe` |
| production | `src/cs2vm2/cs2vm2_host_request_kinds.def` | `42528067ef03424dbea2e1762c001f8fa50c32e56d281b9fe35dbd006a470436` |
| production | `src/cs2vm2/cs2vm2_strpool.c` | `0d2eff84a28268b1e5b0bc8c3dadd712238fa1431e759d251e087d781b403043` |
| production | `src/perf/torirs_perf.c` | `278bab111e96ad1061dcfe408a9bfce91aca78e9f5cb8eeeacc492eb35ad7271` |

### `test-mock239-playerinfo`

| role | file | sha256 |
|---|---|---|
| test | `src/torirsserver/test/mock239_playerinfo_test.c` | `96dbf90349a168efead3f6b76d7bb85498fd717614f3cc587e5adefc22c390a7` |
| production | `src/net/bitbuffer.c` | `9938cfda6c8442ac3e45c76d6540b6e1c1cf11b7b10565800fb90409f405d29c` |
| production | `src/net/rev/osrs239/osrs239_entity_info.c` | `36b643f2301c13fb79669479dfe07eac87fad2799f4737fae7442e98cbe25923` |
| production | `src/net/rev/packets/pkt_npc_info.c` | `99858d3df65183df063fddee120c6fede3814940c3fb05e56e4666f7fb022deb` |
| production | `src/net/rev/packets/pkt_player_info.c` | `c2e38a36e4b7de47b0031177cd2c0e505d3179f9c28a65728e765c8a4bdf0855` |
| production | `src/torirsserver/mock239_playerinfo.c` | `fa3d4d2c0707588c2cd79800eb99e047552d0ba5a92666ab5928f795d7c9a5be` |
| production | `src/torirsserver/torirs_server_wire.c` | `eaa360b421e38250b44000292e6ef7276146b2390f3cfac7ac567002938e3250` |

### `test-sailing-collision`

| role | file | sha256 |
|---|---|---|
| test | `src/torirsserver/test/sailing_collision_test.c` | `68002a674cc06a385ef5618538dacd9e1f75a00da21b119cacae08e9bd84be60` |
| production | `3rd/toridraw/graphics/shared_tables.c` | `995ff3dcdd210d057bd8de22a19702980cd46a960a0ceb1ae79e20ecb7b600c3` |
| production | `3rd/toridraw/toridraw_math.c` | `5ddc930d99e535dfba7553e24fe0c4c4161f36c777638cd72f7a3176ff9eec1f` |
| production | `src/engine/world_builder/collision_map.c` | `b18fd644502fe4fe2ea5a41e00413519b3b1304240d5798dfa3f2e4d58ded974` |
| production | `src/features/features.c` | `8555d8325bab11b1955dd919194ed4fc08a6cdf977c5f526523a43c916310e65` |
| production | `src/torirsserver/torirs_server_loc_ops.c` | `62fa29f0fdbd7a29b8f4596867d23180b40050726cfbade018d5944cf7eb0689` |
| production | `src/torirsserver/torirs_server_mapinstance.c` | `211a517247c5d7e29261a0a42c352aa441528ae84ff53bb70664489bce773ed5` |
| production | `src/torirsserver/torirs_server_scene.c` | `fd95c061654bc199287d02e85bccfde28aef1efe7a4e089ab4e544e1dcb25637` |
| production | `src/torirsserver/torirs_server_vessel.c` | `ebf2cc79474beb0bc3da454bd822da7aff46f683d606d5932deb221803b7d68c` |
| production | `src/world/wev.c` | `8dedfec17ce9d058b3cb685d12ebb93eaf88a0a61faea56670b4d684c5d33d27` |
| production | `src/world/wev_deck.c` | `a17c1692edc64b44f04a40c93cc02fe5cd35f60cf6af7b39275f97663912f356` |

### `test-entity-info-shrink`

| role | file | sha256 |
|---|---|---|
| test | `src/game/test/task_exec_entity_info_test.c` | `6b8fd2dda2ca23655cf014fa2bce543a56f7a97bb8270b57ab5725c7ed66acc3` |
| production | `3rd/toridraw/toridraw_scene.c` | `96b4f7c44a657b6eb820b2f00abafb71c05f0ca146a408ebc69c8520da0960c8` |
| production | `src/app.c` | `09ab5dd24504426b71e1a8d27c155f0cd72db51a2b8388c088697d88e63b645a` |
| production | `src/engine/cache_provider.c` | `34b0b1c4ff8e8ae7ee156ca7dc7c15de95df3f36e4ab94feb68e4aab046758bc` |
| production | `src/engine/uitree_scene_bridge.c` | `dad01dc9fc57a0d8bf8a0bc798ab4b02b70af3563fa7b5c8050884047ad294b1` |
| production | `src/game/rs_entity_sync.c` | `85ec35de349ee1ab04ceb5e7143dff25a0ffd62b69103acb5aa120b758a87bb0` |
| production | `src/game/task_exec_entity_info.c` | `39765d66ed3dd660cc3ffdaecd76dab57fa64a61c2818e38ccb87446a0c5f312` |
| production | `src/world/world.c` | `6ca78175b23ebdaf3027b3a07975bb846765ae212ffc6daf6994ea6ecda04d4e` |

### `test-scanline`

| role | file | sha256 |
|---|---|---|
| test | `3rd/toridraw/toridraw_scanline_parity_test.c` | `10366c6e214d83d343c7a28bbfbf46d9ddc04e677da1f71b941c7ffe7f66e21c` |
| production | `3rd/toridraw/graphics/shared_tables.c` | `995ff3dcdd210d057bd8de22a19702980cd46a960a0ceb1ae79e20ecb7b600c3` |
| production | `3rd/toridraw/impl/projection/projection.scalar_reference.u.c` | `cd16cb292153656ac37b6c0471e7223f1974e69d4d4a9a14e72b3f63e6910456` |
| production | `3rd/toridraw/impl/raster/flat/raster.flat.alpha.nofacealpha.nomodulate.painter.branching.s4.scalar.c` | `f0fc8edf21314edf6a4911fc93a50800e89c4fdd4bc079702e998bdd03b42e79` |
| production | `3rd/toridraw/impl/raster/flat/raster.flat.opaque.nofacealpha.nomodulate.painter.branching.s4.scalar.c` | `eb5c6a0a6fabb18323e75419cd9c8898e404a8000eba275d2e8ac1b675cf020b` |
| production | `3rd/toridraw/impl/raster/gouraudhsllightness/raster.gouraudhsllightness.alpha.nofacealpha.nomodulate.painter.branching.s4.scalar.c` | `c8b363ce7ab730431378a3fff5b52095f0303b2ceba8535fee19cd634d391b21` |
| production | `3rd/toridraw/impl/raster/gouraudhsllightness/raster.gouraudhsllightness.opaque.nofacealpha.nomodulate.painter.branching.s4.scalar.c` | `2842ef88c0e6d772a935417d7618c3602e8351e6dd826cb6cc8c722823c21c24` |
| production | `3rd/toridraw/impl/raster/scanline/scanline.dispatch.u.c` | `235885318a370fd3f1c2c973eed4e4b1bb068c275ab66856e8c690dade5fee4b` |
| production | `3rd/toridraw/impl/raster/span/span.tex.dispatch.u.c` | `869b7b184fa558cc7c962b050e0a7b832d7c010bc3fc0cb9685c93be09487391` |
| production | `3rd/toridraw/impl/raster/tex/raster.texshadeblend.affine.texopaque.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c` | `02e00f42569b7c7627d58aa13ef145a329ae4d81ed99cb7241f9afed8944c1c8` |
| production | `3rd/toridraw/impl/raster/tex/raster.texshadeblend.affine.textrans.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c` | `323390ad4585e4e89e1663d3c90f74f86608e6805cd8283c247ebc2eb1860aea` |
| production | `3rd/toridraw/impl/raster/tex/raster.texshadeblend.perspective.texopaque.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c` | `bd773cca5394f5c9dcd33741e42e7c14c1a04959c6127c5c5f103646f1691112` |
| production | `3rd/toridraw/impl/raster/tex/raster.texshadeblend.perspective.textrans.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c` | `f03ef8c4f0bf6f176e4578a494d65fc9bf12df0ca9009f9aba0f3e26ca20747c` |
| production | `3rd/toridraw/impl/raster/tex/raster.texshadeflat.perspective.texopaque.nofacealpha.nomodulate.painter.branching.lerp8.scalar.u.c` | `2043787c6ab9399e0ef6f1a19819c8ea2d23ee63665ced38ab75f403c4894a8d` |
| production | `3rd/toridraw/impl/raster/tex/raster.texshadeflat.perspective.texopaque.nofacealpha.nomodulate.painter.scanline.lerp8.scalar.u.c` | `66ba111d6b2682c3b9389230d8eb9086f3bdd18878675c78fb867a38128e04d1` |
| production | `3rd/toridraw/impl/raster/tex/raster.texshadeflat.perspective.textrans.nofacealpha.nomodulate.painter.branching.lerp8.scalar.u.c` | `2733c7fbabfe0211cdd60aa328ddfc121007277fb273425be45045ff94de52af` |
| production | `3rd/toridraw/impl/raster/tex/raster.texshadeflat.perspective.textrans.nofacealpha.nomodulate.painter.scanline.lerp8.scalar.u.c` | `5d7f34ad017524cd86b0e355388eadc18fb7defbb42c06ba6163abe1c884d54d` |

### `test-rsprot-bridge`

| role | file | sha256 |
|---|---|---|
| test | `src/net/rev/test/rsprot_bridge_test.c` | `66bac112f9e2a3e06b5f93aeda4320f420b52bf8e85b90230d4d66a03091f33e` |
| production | `3rd/rsprot/packets/cam_lookat_v2.c` | `dd0533cb20db66747a5f0ac7c1c70acbc3b03fe3baab8d5cc26e3c4c0400f6d6` |
| production | `3rd/rsprot/packets/cam_moveto_v2.c` | `7ebd3bdadfb5dbbaa6c11755610e1079b8549e59232be66b8fe104ac3e9e7e45` |
| production | `3rd/rsprot/packets/cam_shake.c` | `6c01414cba816b1abecb318cfdc9310980e0ac203e09104869c2edfba84a7a63` |
| production | `3rd/rsprot/packets/chat_filter_settings.c` | `a5e364bcc29984490331d728e85492b9f642a18b2f36ac7902c257f0175f99e6` |
| production | `3rd/rsprot/packets/friendlist_loaded.c` | `13e0f9f4c601bc7f5a974b7989f1ffb1e9ca233bcbed04f851ae5029eac9470a` |
| production | `3rd/rsprot/packets/if_clearinv.c` | `8bdea4d3e1a3b0edea15c059eab43c352eba7de8dfa955f2b51080e80270097f` |
| production | `3rd/rsprot/packets/if_closesub.c` | `eb13b623d84a3a908dd87c9e3c35510dd33e992c44e3697c4712b12b64d750df` |
| production | `3rd/rsprot/packets/if_movesub.c` | `184b55c339e513c3bb86724beb5ebc6f099e7f9871c5eac4a5a9150e1e7c4c01` |
| production | `3rd/rsprot/packets/if_opensub.c` | `80cb26b0acc838b6e339fd4fe427645f72b6f72e204d8883240607587c51d9db` |
| production | `3rd/rsprot/packets/if_setangle.c` | `e384ecc2ce7e9dac4f3e752794722a8dee86ce442a1d6d2a6e9b94e1ac9f7567` |
| production | `3rd/rsprot/packets/if_setanim.c` | `3187be039a8489654cf4e76c6baba76c01c7d44f5abe4d0a622f4aeeaa7ddfcf` |
| production | `3rd/rsprot/packets/if_setcolour.c` | `88ecebd08dc6f1e916cd5764e6f28a6f1e4b95e939388588eb5db1037fc0b7c3` |
| production | `3rd/rsprot/packets/if_setevents_v2.c` | `cd2aace754ba4875a8741a660140990adf571d282553e9f301e48d36415fb886` |
| production | `3rd/rsprot/packets/if_sethide.c` | `c5b488859ce560187623cf57f6c8c67bcc4ffd9aab24c2620b8e30e593ff5358` |
| production | `3rd/rsprot/packets/if_setmodel_v2.c` | `59b0f42d136f0174b4091faa578af960061dc1df0d2f4571aa9369a16165915c` |
| production | `3rd/rsprot/packets/if_setnpchead.c` | `b6b7ca9a1c58859dd2f08e9407f4b16ac00b667cd15aad0ec055b1a9e3235c5a` |
| production | `3rd/rsprot/packets/if_setnpchead_active.c` | `c6e196dad95dd366a7961d8e99aca9e4f365631a212cae774120284516597580` |
| production | `3rd/rsprot/packets/if_setobject.c` | `d3525d8f6f0ce0231e0d554b3f7ac3cdbac15c47d3e19a36a0ebb06adffdf5e0` |
| production | `3rd/rsprot/packets/if_setplayerhead.c` | `5590cddd482dd797718f58469c6a2324921ecc9cc5808104ca58804741a21731` |
| production | `3rd/rsprot/packets/if_setplayermodel_basecolour.c` | `8330aa315da9e6f21d46c0ea668f891729596a8cb0b787abe929efa24591b6e5` |
| production | `3rd/rsprot/packets/if_setplayermodel_bodytype.c` | `62edca0dd415831ffdbf4d57ee19f53085be4a33569e1d3f27cde781e73e71eb` |
| production | `3rd/rsprot/packets/if_setplayermodel_obj.c` | `92fa269a5675df836aadd47b496b89ae4704b62e272b65e0762dd72cdd74d1c0` |
| production | `3rd/rsprot/packets/if_setplayermodel_self.c` | `7323fa4a8c94dc6a237398811b273ff67127f8c29ee666de0e1ec3659defbb86` |
| production | `3rd/rsprot/packets/if_setposition.c` | `3c5924eebd06c54cc489bb490ff62eb6a3683563ed1b113a0ae5db936e3b1282` |
| production | `3rd/rsprot/packets/if_setrotatespeed.c` | `d40bed68ffa16fd49cd6417a25287544e07206a4e37f52c0bddb6da55f6f3b88` |
| production | `3rd/rsprot/packets/if_setscrollpos.c` | `e0735adbe55a350c3c5bb06e3708872542da76dab219d56811c1d1a951fa725a` |
| production | `3rd/rsprot/packets/if_settext.c` | `ca6480511548cb2c11431c00ffcf0c42415b7ccc315c5782b41ba2b665e5b216` |
| production | `3rd/rsprot/packets/message_game.c` | `41d11bbcd9d37c7c9ee95cf6aaba320e9484e0398580a47fa5545009edc99c27` |
| production | `3rd/rsprot/packets/midi_jingle.c` | `426a47feac7d4acb672add0aa5b9c643949f4508e13863a287c91b60bafd987e` |
| production | `3rd/rsprot/packets/midi_song_v2.c` | `6133b3e60dd247dcdf353c9fba99b8454aabc1c2976919f9f3b5e506c6eb4176` |
| production | `3rd/rsprot/packets/server_tick_end.c` | `1654de3a51b927df099f6b3709b3f86f5fc53e44bc518f4aa8de93ea26d54487` |
| production | `3rd/rsprot/packets/set_map_flag_v2.c` | `58ea9dcc5cf86831d107e52faf7f0eba4548c147b6943970c02dda16072052c3` |
| production | `3rd/rsprot/packets/set_npc_update_origin.c` | `9f3f7952ef0b0e679df44b014b6a6cb093e56a6310cd9a785e77d5b9f2d0e723` |
| production | `3rd/rsprot/packets/synth_sound.c` | `e932c41eb65027b32672707a386bf3e29db50d557eff7c2b2094ad727c62f3c2` |
| production | `3rd/rsprot/packets/update_inv_stoptransmit.c` | `b89f72882c19439a3be6dcf210345d7a3e3d3d75972d0f9c123f90b446461dcd` |
| production | `3rd/rsprot/packets/update_runenergy.c` | `37ed4525c0fd87b1b567af76292b1eda06e42135ec67dc3ff808561f39711516` |
| production | `3rd/rsprot/packets/update_stat_v2.c` | `21a610c5269e2735ed214297f7405cfda27c1b95d19197956f65a1a4bdf4a5e3` |
| production | `3rd/rsprot/packets/update_zone_full_follows.c` | `37ccfefcc71889b9d1005c0706a9e0247c204d159704e2c811aefb2e30081e17` |
| production | `3rd/rsprot/packets/update_zone_partial_follows.c` | `971330a1a47b41f66e6467fb228a67d9c4458a09468cda86fdf853d7d67179d9` |
| production | `3rd/rsprot/packets/varp_large.c` | `c6ed10c6b34345d7b8a5db4f7f0f6ea9a1b8d756ebf0d361cd4fd43be09d6323` |
| production | `3rd/rsprot/packets/varp_small.c` | `086d1f28c2056238d8a3a9c93f7c30a784035b406c1a3db609883186877a4e4b` |
| production | `src/net/rev/rsprot_bridge.c` | `c8813d19f14da594cfe141389132b068cf394ea132d3157caf311f236a03dfd1` |

### `test-social`

| role | file | sha256 |
|---|---|---|
| test | `src/game/test/rs_social_test.c` | `b2b1423c448d0dc8a513f5594be525bbc2eae4d79bfb21ed88960d19ed2c100d` |
| production | `src/cs2vm2/cs2vm2.c` | `0d3724e765b3851d11595bfc59feac426542beba7f0fba0438df9d58802e1bfe` |
| production | `src/cs2vm2/cs2vm2_script.c` | `e8c53b0d3692c25abb7cc8efc65607ce1b7c12fe364e0946d5453f5ac2e316cc` |
| production | `src/engine/dat2/dat2_buildcache.c` | `443eb6cacd0ae7ace88cae50fa1a8a6efb0459bbc47c8413d282242f9395ac4e` |
| production | `src/game/rs_chat.c` | `36d0f21e9d24c6f14f2d7aa6cc47b78c60b8b8de895504cfaa499a8792f7f0e6` |
| production | `src/game/rs_cs2_host.c` | `7851f2617b6bde35733823a7dcf0c5aeecede89d7639a4243c27c74d83c0ca89` |
| production | `src/game/rs_social.c` | `35c4ab9fdf441ec3e41b3b5e8f6804b65ca4ef8b5db59939529e58c463773487` |
| production | `src/game/rs_ui_slots.c` | `23ecb562045772b71691595c8586bfdba4199cd778b0279692d8c6dba0ef6430` |
| production | `src/inv/inv_manager.c` | `b72c33b2e28b3f866a5b57d1c0708f7983ba1ec78040fadb0a33c33102f6ec33` |
| production | `src/ui/uitree.c` | `68c14a0507d9c9292dc6eb5b58a6b2c6a65b9d432164a5ec70400f47874905e5` |

### `test-uitree`

| role | file | sha256 |
|---|---|---|
| test | `src/ui/test/uitree_test_canvas.c` | `f41901ecf6a735e548262b0c58f9460b7ea4d5795487d2c7c2edad4d784b9527` |
| test | `src/ui/test/uitree_test_chrome_exec.c` | `ba1433a355943a0e04f30b44dbece3e880e24db13e1e4fd4cb143ef440d97012` |
| test | `src/ui/test/uitree_test_chrome_panel_draw.c` | `59f16132af91d9b4f1f8c297e61cb8ab456eb7829627d80791e56ef47e7a4e26` |
| test | `src/ui/test/uitree_test_chrome_shell.c` | `8d8d3e1a66038e6dba94ffa1118fc4b4633f2bd18ff9d682708dd4aae580965e` |
| test | `src/ui/test/uitree_test_component_params.c` | `69f48ddaede5eeb49b572bb45a4ef2d4432885eb163a331c3940d49b8157a1c3` |
| test | `src/ui/test/uitree_test_debug_overlay.c` | `c3c56befb043dc048258e39f8e7b3f1a3420898c5f406bf52bcddac8b2b0b383` |
| test | `src/ui/test/uitree_test_dirty.c` | `b2506714954e66b9fa1e42306fb4080ead88b31217a3727e1a58027a82adc478` |
| test | `src/ui/test/uitree_test_drag.c` | `eff20497d86ab21717774a1ee122d75c5057b812a783dc1ff1ba63eb95037d1d` |
| test | `src/ui/test/uitree_test_drag_scrolled.c` | `72239b3598fc2417a06a250592a48ebf88b516f8d3bc697cb312291eb4ce6f7e` |
| test | `src/ui/test/uitree_test_emit_golden.c` | `abdb8386a557f7a53a05f451ff6c7e9b4762034d21fce62f79f50491ec661270` |
| test | `src/ui/test/uitree_test_emit_icons.c` | `e107dbcea68a9baaf0aca097017553d87b7d337ede04c4c43549198869b069e5` |
| test | `src/ui/test/uitree_test_entity_overlay_order.c` | `b8c9d8c2487e9e1c2eae69b094392931cd4e5d6622189577029a45144b9b551b` |
| test | `src/ui/test/uitree_test_frame.c` | `5d63fc12781d115773ccfd50b46f70dd28161b8324df3ded6deac2ed64a0ffe4` |
| test | `src/ui/test/uitree_test_frame_depth.c` | `5716dd46430ccc18739e3c9b4d1ef64514a43bc9d9db9b944084cef9ced0ce75` |
| test | `src/ui/test/uitree_test_frame_metadata.c` | `0d10090b3d9c3e609934abeaec48954a5948ebfb940a36b25ae28d2d77eff6a3` |
| test | `src/ui/test/uitree_test_hover.c` | `8599ed68e967f685d41333648b1f534090e1eb1a6539cf33bbd6440ef48df547` |
| test | `src/ui/test/uitree_test_id_index.c` | `562ef92ad02bfc74d32accc63027baf712d538f2dcf4ea64580b9e55a7d63032` |
| test | `src/ui/test/uitree_test_input_field.c` | `16c152c79de249772bf3e9d25d37b4d3999412b47eed07e13af1ef437fded587` |
| test | `src/ui/test/uitree_test_keys.c` | `3658198d5f88df52f42ffc9416f373ebe1fff993a705e375a34339756f4e1c54` |
| test | `src/ui/test/uitree_test_layout_build.c` | `9276a7f58aa37547dbb9d2aa78c6757e893724d0ae36b3047ab8a48b2267cdc3` |
| test | `src/ui/test/uitree_test_live_sets.c` | `8c74a866721c0dc1abdc2c49d53afdd77f4b634f03b5b8a4ee4e27c94348a3d1` |
| test | `src/ui/test/uitree_test_main.c` | `014b847657d83930809b34caa6f75fc699c946b8175ae82f71c7a5cf947b4ac0` |
| test | `src/ui/test/uitree_test_minimenu.c` | `52b4b4d6b4add0bcb2f9352a2d99dae3e8f5629e6e2c33a18777482dcb670b13` |
| test | `src/ui/test/uitree_test_mounted_world.c` | `1f01f0c253c2cc40ecfbd8bd649c0975bc6ef5d4e493ff7ca0fc2f5b15d81356` |
| test | `src/ui/test/uitree_test_mutate_emit.c` | `0b1128ee70aa6e62324e6c10a6ee25b250bfdd454bfb9c95ebc46a629f814d2c` |
| test | `src/ui/test/uitree_test_open_close.c` | `01109566ed1a4210629c16a4e36441ffe19321b3a46ca46cf1266b40c35eb856` |
| test | `src/ui/test/uitree_test_overlay_retain.c` | `d260b800c350c8a46dbd1392a165d8828dec94a247c7a4d4e3bc1df95b516ca6` |
| test | `src/ui/test/uitree_test_roles.c` | `b65e2846a8577eaab8e3c0cf38b9178a9e53130ebb2863327bb08d75d12e8104` |
| test | `src/ui/test/uitree_test_scripted_overlay.c` | `79419faa35ac743812eb64acaf95d82df1cc3abcec7e2cfa949759b966b39f66` |
| test | `src/ui/test/uitree_test_scroll_hit.c` | `364ab99422c5f61c0118eb1ec49a9a53c66782dc5ef8b565e37eff6d5dc7d142` |
| test | `src/ui/test/uitree_test_server_viewport.c` | `9ca2367721b74946c2dc9b5180dc19415f276ae1683d4ec83cd11fb201a0495e` |
| test | `src/ui/test/uitree_test_walk.c` | `0edc07d62a31f07a9f93d135398d1fe287c5ae61722743d43d6d3b4ad743f580` |
| production | `src/engine/torirs_component_hook.c` | `4eaf3be71cc01f24f6f74ae7f7bf30cdb14f6a0c034f83ce8f4a28c1b6ae8f61` |
| production | `src/engine/torirs_types.c` | `9fba9a458e1dafc40d5de31b0a9e52b94eae4d13c3b88ec0185f2ca89880763f` |
| production | `src/input/torirs_input.c` | `3c214a45a5c84a620275af546b1f4c87179d42861b6c60b8d826ed8dc5eedd16` |
| production | `src/input/torirs_keymap.c` | `41def734b445012d54538e8a3a3cd14734bc86f5c7164af5f9e18e031740d880` |
| production | `src/perf/torirs_perf.c` | `278bab111e96ad1061dcfe408a9bfce91aca78e9f5cb8eeeacc492eb35ad7271` |
| production | `src/render/torirs_arc.c` | `cc71e37bfaf0f5da25e81a1deadb33813d4b7d460df374a1681c62a733dc0a49` |
| production | `src/ui/torirs_chrome_exec.c` | `ca9e721b14bd96c26d9fe12136f532d429f5167be8b50b9165222704c33f1692` |
| production | `src/ui/torirs_chrome_exec_kind.c` | `495f297756b2956c82808c739ee43173026a7e3de802800f846859dd520dc31a` |
| production | `src/ui/torirs_chrome_inkwell.c` | `2938627cc66ee91e91cabd0fec9ac746e5c668406e976d8b6b7dd246ffecefa9` |
| production | `src/ui/torirs_chrome_mirror.c` | `d0263913ac1550fb79d8a9b2b8cfc3543bf250aee069f562fba3200433f8cb9f` |
| production | `src/ui/torirs_chrome_panel_draw.c` | `e8dea33fd135ac0a07dbb8958f81ca2af2028b2c1592060b2bc2a728e21ccea4` |
| production | `src/ui/torirs_chrome_rail.c` | `09ccbc4796b1fdd2e489754cb3941c269a9503485db533e6f6acf7f68dc43028` |
| production | `src/ui/torirs_chrome_shell.c` | `cf2687791fead0445ef05421b159fe161305b81350386f501e48da9d0cc740b4` |
| production | `src/ui/uitree.c` | `68c14a0507d9c9292dc6eb5b58a6b2c6a65b9d432164a5ec70400f47874905e5` |
| production | `src/ui/uitree_build.c` | `3cb1ef25245690fb7b2f49561ad6fd9a771efbf2ed1353d70b3c4576a2f84e65` |
| production | `src/ui/uitree_component_options.c` | `e6ee09c6fcdd83673cdcb0b6c9b1dfddaf9d1312270cd63ef383979938cd12d0` |
| production | `src/ui/uitree_cross.c` | `6562fac867693d8e9e318ddf554c7ff1c24de590f1ec18c289f552c6d5293bfc` |
| production | `src/ui/uitree_debug_overlay.c` | `6c878b949eb5890ef23a29d49d8d5bdcf7aec95c6e179c9395eeb574f939aa02` |
| production | `src/ui/uitree_emit.c` | `9b56475e2a654beaad6b60b92b7ad7f6dce0b4f49aba8f1e53f7f46dcb5212ac` |
| production | `src/ui/uitree_frame.c` | `0b7614b7e3d9c65b41ca1d46e5d0ed65de8766b3988cc1b1b7586762a4c3f9ee` |
| production | `src/ui/uitree_hook.c` | `8f0b7a7e34d5ab87b3fe20865657e6c97c95b1172a472a9a98149d936d8417a1` |
| production | `src/ui/uitree_host.c` | `4ddf6e355c58fe13a1b841f8d5fa8c96b0f03fe32eaab3eba5a2d008672aa4c9` |
| production | `src/ui/uitree_hover.c` | `b0824be3b94ecdf23aed3fc96f87b766f6244c4e3fdd95dd8b76b72bce831651` |
| production | `src/ui/uitree_hovertext.c` | `7bfa1cb1958473cd93d17523f6ab44886f168c48f656b35ca63fcce1ef7bd214` |
| production | `src/ui/uitree_ink.c` | `b49f428a7cce93099de1973b31a8ca5fb8db0a75b5dd28f229d487c0e53d1dd9` |
| production | `src/ui/uitree_input.c` | `259cc6c94bacc27ac7d2419cf221cab79ac8d86e88aeb20debd218aceae0d1dd` |
| production | `src/ui/uitree_interact.c` | `2d7c75097067b6f34f8908330b0d9f05066275465b6358257b488c0067c4d755` |
| production | `src/ui/uitree_inv_view.c` | `86772cdf823f3003767df3ae2069e8ab57a36001fd784b03898bd65dc5c5ed27` |
| production | `src/ui/uitree_layout.c` | `7a77870f4bae68675af37fecbd13c5bb7a0afeb41a6a43e4ed13c9db5b04c08d` |
| production | `src/ui/uitree_minimenu.c` | `4a45bfcd5b4cfe57e4cb2d4ddc983712ec51a3ce32a2a88740184ef3377fbf47` |
| production | `src/ui/uitree_obj_cell.c` | `79a24815063ff7dd43e7429b349690680a02cbc2c597093644364d97f8046271` |
| production | `src/ui/uitree_role.c` | `097659ca34378ad2f86ea52622d4ef83dbf76fbe472af045d9524913299063fd` |
| production | `src/ui/uitree_scroll.c` | `dd857054b0b7fcbc7decb40748f6565cc71b4d8870afefcffd349315d8ca12cb` |

### `test-world`

| role | file | sha256 |
|---|---|---|
| test | `src/world/test/world_test_main.c` | `71e507ae13a247b66bb5d0c3c10134625d4d7dec819093d07f31033a2746f860` |
| test | `src/world/test/world_test_route.c` | `d84e759fd9004b66bd8f0b6ea261b881c233fd8ce7d70293aa2725a8f385eb21` |
| test | `src/world/test/world_test_sim.c` | `b07cee5ad38826072fa88f9f8693119dd0e70f73874883c0ce551a2311a23a8c` |
| test | `src/world/test/world_test_unit.c` | `7df9aaea14d42eea48f1ef8f4a95e3808ec041f3d5144196af8c98d05d302f42` |
| production | `src/engine/world_builder/collision_map.c` | `b18fd644502fe4fe2ea5a41e00413519b3b1304240d5798dfa3f2e4d58ded974` |
| production | `src/engine/world_builder/heightmap.c` | `ac5e8c5c5b70a935d2123d4c608d45ec7026cc42e7f2ca73e317655dbff27e40` |
| production | `src/engine/world_builder/minimap.c` | `137dc3bc296e6945cf7365d5c7dc58d344d1e0edd2dc1fe0b45b10b3e564488c` |
| production | `src/features/features.c` | `8555d8325bab11b1955dd919194ed4fc08a6cdf977c5f526523a43c916310e65` |
| production | `src/painters/painters.c` | `b0743db585f2060fe20667a9d44a98fbf0f5003bd045cf2841c474eb0d8a51d4` |
| production | `src/painters/scene_occluders.c` | `d47b72cfb36e6d0500b9ed2c014278dbdcef8bb40a5bf9c251bd91f9616c2d2a` |
| production | `src/world/entity_pathing.c` | `f61331cb4b1bbebc26ca6c23e98dcd4033aaff4a6f695f158762d6d4449e6370` |
| production | `src/world/entity_pool.c` | `2e174842a6ad181ffbec8a5cbe6f2d12bb8f8263380a4670a610b3099833f121` |
| production | `src/world/entity_registry.c` | `084b846bd19d5f1d580547254399284ebebc36fe2fca6e3758c5d173407fc322` |
| production | `src/world/world.c` | `6ca78175b23ebdaf3027b3a07975bb846765ae212ffc6daf6994ea6ecda04d4e` |
| production | `src/world/world_cycle.c` | `b9e4983d256aec7eed6cc967ec3605715b4867cda901a1025d77bf81db5e1972` |
| production | `src/world/world_entity.c` | `f9194c6bda36632858f0eddda40d47f9994341a22263143f7c4941cdfeb13661` |
| production | `src/world/world_pickset.c` | `1a6bfc6869a2afde214e8173b180bc9ea061a055472798f7d5477ad07fbd2fdf` |

### `test-torirsserver-embed`

| role | file | sha256 |
|---|---|---|
| test | `src/torirsserver/test/embed_test.c` | `28b36e3ce9d9660f1de269051461aa1a1b7162282be9f8d7b66d8dd986c30e07` |
| production | `src/cmd/cmdbus.c` | `042bd84b3f8c7c5ccc3de54ae2bab0433f43f6a312d3376ed6a7cf977f2768a7` |
| production | `src/engine/world_builder/collision_map.c` | `b18fd644502fe4fe2ea5a41e00413519b3b1304240d5798dfa3f2e4d58ded974` |
| production | `src/net/bitbuffer.c` | `9938cfda6c8442ac3e45c76d6540b6e1c1cf11b7b10565800fb90409f405d29c` |
| production | `src/net/jbase37.c` | `a38c5128a8cef1abd681eee10b585ae0ec4a0643b79b99909ea204ba1f6ad124` |
| production | `src/net/net.c` | `0876dfac87137a7e3ee0c63db44b09c3abd671d6c763da1a7b6f34a02c2b8c2e` |
| production | `src/net/net_out.c` | `f18f580fc42cb257087a7e02660b2dc49ef507004a85b82fc12c17f90ba0eb10` |
| production | `src/net/rev/gameproto_parse.c` | `433d1404b82f74032848547c9f59f11b43c245eaa206d4070118f170b70f98c1` |
| production | `src/net/rev/osrs239/osrs239_entity_info.c` | `36b643f2301c13fb79669479dfe07eac87fad2799f4737fae7442e98cbe25923` |
| production | `src/net/rev/packets/pkt_npc_info.c` | `99858d3df65183df063fddee120c6fede3814940c3fb05e56e4666f7fb022deb` |
| production | `src/net/rev/packets/pkt_player_appearance.c` | `c8a597e1df9c34f37348f17d08389511da7240eeae31e78d9aee425b23fccfb3` |
| production | `src/net/rev/packets/pkt_player_info.c` | `c2e38a36e4b7de47b0031177cd2c0e505d3179f9c28a65728e765c8a4bdf0855` |
| production | `src/serverscript/ssvm.c` | `48453dc116fe7f6444b38c70df2dcdb97612a148ef572e5fadb5b98c7560bddf` |
| production | `src/torirsserver/torirs_server_content.c` | `f2b913d44c556f9473a23a657abeff5dddddb9b507fce71efca5ea5ee9907074` |
| production | `src/torirsserver/torirs_server_embed.c` | `0355d823847d70b6b4e8276e96a0d4b17ec530f036c4a3b1817cf8ef48147e7a` |
| production | `src/torirsserver/torirs_server_friends.c` | `6903b9dbece7a3b3df36febb0424d5e64e2d31f44b8077f7751219bdeb0c97ef` |
| production | `src/torirsserver/torirs_server_save.c` | `58ab31b0258d02df97b5a03feeb5d0f8e046345ed7cc9cc66157569930064ad6` |
| production | `src/torirsserver/torirs_server_scene.c` | `fd95c061654bc199287d02e85bccfde28aef1efe7a4e089ab4e544e1dcb25637` |
| production | `src/torirsserver/torirs_server_session.c` | `81d0c5a02c416c58638d07a53512c5522f1c38d7fde2a9bf1eea7ee8646e747f` |


---

# Final-binary re-stamp — 2026-09-07, worker `restamp`

Everything above was run against the **previous** shared pair
(`src/torirs c7e62cce…`, `src/build_opt/torirsserver a53899eb…`). Root rebuilt
both from the final sources at 08:08. This section re-runs the ten gates the
final phase names, on the new shared objdir, **serially, one make at a time**.

## Provenance (verified on disk before the first make and again after the last)

| artifact | sha256 |
|---|---|
| `src/torirs` | `4854e3036abf814d07ce97649845d30206bac647b49adce560e3e9b28fa927a4` |
| `src/build_opt/torirsserver` | `214b43d7a30dd47b67b47693e1b9820d299c2154eeb2193aeb2c4ec0b7082186` |
| `OSRS-Content/osrs239-content/server/scripts/build/script.dat` | `902a6b8d242bba01990b3955100e301c2a0edb6b1f18fc6ca83fd703a880df59` |

`make -C src EMBED_SERVER=1 <target>` → objdir `src/build_opt_es` (OPT defaults
to 1). No shared client or server link rule was invoked; each gate builds its
own executable in the objdir. Logs: `/tmp/sailing-fin-gates/<target>.log`.

## Results — 10 of 10 PASS

| # | target | exit | status | dur (s) | pass line |
|---|---|---|---|---|---|
| 1 | `test-world` | 0 | **PASS** | 2.9 | All tests passed. |
| 2 | `test-wev-rebuild` | 0 | PASS | 5.9 | wev_rebuild_test: all passed |
| 3 | `test-minimenu-world` | 0 | PASS | 2.6 | All tests passed. |
| 4 | `test-sailing-collision` | 0 | PASS | 2.0 | sailing_collision_test: passed (0 failures) |
| 5 | `test-mock239-playerinfo` | 0 | PASS | 1.1 | mock239-playerinfo: all tests passed |
| 6 | `test-cs2-sailing-settings` | 0 | PASS | 3.2 | PASS native cargo privacy0..2 through script3852 label writes, the8830/3967 paths, isolated script/varbit/struct scope and last-choice coalescing |
| 7 | `test-frame-flat` | 0 | PASS | 1.0 | frame flat live geometry, scale, colour, no-pick, replay lifetime and root identity PASS |
| 8 | `test-sailing-paint-order` | 0 | PASS | 0.6 | scene dependencies: flat before full, parent ground before boat; nested ownership, raised shore, loc order and zero-boat bytes PASS |
| 9 | `test-rsprot-bridge` | 0 | PASS | 1.2 | rsprot-bridge: rsprot and osrs239_parse agree on every migrated packet |
| 10 | `test-social` | 0 | PASS | 5.8 | social: all checks passed |

Durations are wall clock with other workers active; they are **not** performance
evidence.

## `test-world` — the previous FAIL is fixed

The earlier run's three route failures (`world_test_route.c:131/186/217`) are
gone: exit 0, `All tests passed.`, at `OPT=1`, which is the configuration that
used to fail. The cause named in the failure analysis above has been corrected
in the tree — `collision_nearest_opts_from_model` in
`src/engine/world_builder/collision_map.c` now writes `out->unbounded = 0` in
**every** arm (three assignments, at lines 975, 989 and 1000, under a comment
saying every arm writes every field). `git diff --stat` on that file shows the
six added lines. `src/world/entity_pathing.c:280`, the client entity-pathing
jump route, is therefore safe now: it initialises its `struct
CollisionNearestOpts` entirely through that function.

### Half of the recommended fix was NOT applied — still open, still in production

The failure analysis above asked for two changes. Only the first landed.
`ToriRSServer_SceneOpNearestOpts` (`src/torirsserver/torirs_server_scene.c:2434`)
still writes only `range`, `max_dist` and `rank_by_rect_distance`, and **never
writes `unbounded`**. Two production callers hand it a bare, uninitialised
stack struct and then pass that struct into `collision_map_route_tiles`, which
reads `opts->unbounded` unconditionally at `collision_map.c:1130`:

- `src/torirsserver/torirs_server_scene.c:2558` — `struct CollisionNearestOpts
  nearest_opts;` declared uninitialised inside `ToriRSServer_SceneRouteOp`, the
  server's op-click routing, then filled at line 2591.
- `src/torirsserver/torirs_server_world.c:3993` — `struct CollisionNearestOpts
  nearest;` declared uninitialised, filled at line 3994, used for NPC routing.

`ToriRSServer_SceneGroundNearestOpts` remains safe: it assigns `out->unbounded`
explicitly right after its `collision_nearest_opts_from_model` call.

No unit gate covers this: `test-world` exercises
`collision_nearest_opts_from_model` only, which is why all ten gates pass while
the defect stands. It is a **read of indeterminate memory in the shared
server**, it is pre-existing rather than a sailing regression, and fixing it
requires a C edit plus a shared rebuild. This worker did not edit C source.
Owner: **root**.

> **Closed 2026-09-07 09:35.** Root made the edit —
> `ToriRSServer_SceneOpNearestOpts` writes `out->unbounded = 0` at
> `torirs_server_scene.c:2446` — and rebuilt. The residual is that **no gate
> exercises either server caller**, so the fix is held by source inspection
> plus the 563/0 sailing suite, not by a test that would fail on a revert. See
> the final section of this file.

## Not re-run by this worker

The other fifteen targets in the 25-target table above were not re-run here;
only the ten the final phase names were. In particular
`test-torirsserver-embed` was **not** re-run, so its long-standing decode break
(`embed: SYNTH_SOUND reached alice's stream`) is neither confirmed nor cleared
on `4854e303`; treat the earlier row as its last known state.

---

# Final pair `5e34b81f` — 2026-09-07 09:35, run by root

Both sections above ran against earlier pairs — the 25-target table against
`c7e62cce` / `a53899eb`, the ten-target re-stamp against `4854e303` /
`214b43d7`. Root rebuilt both binaries at 09:35 from the current tree, after the
LIFE-1 reconnect guard, the SRV-1 field initialiser and the 09:40 review's
asserts and test controls, and re-ran **twelve** targets on the new pair.

## Provenance

| artifact | sha256 |
|---|---|
| `src/torirs` | `5e34b81f9d2a45eae7306e8ddf93de5c590a0b3cd475d726a7745eb0deffd03b` |
| `src/build_opt/torirsserver` | `6a0e7f0441c25f1249bc8342a5103744a4dbd991d721b67eaea1846db7d910d4` |
| `OSRS-Content/osrs239-content/server/scripts/build/script.dat` | `902a6b8d242bba01990b3955100e301c2a0edb6b1f18fc6ca83fd703a880df59` |

`/tmp/sailing-fin3/hashes.txt`. Build logs `/tmp/sailing-fin3/server-build.log`
and `client-build.log`, both `exit=0`. The server hash equals the intermediate
09:34 build's because `OPT=1` compiles the newly added asserts out.

`make -C src EMBED_SERVER=1 <target>` → objdir `src/build_opt_es`, **serially,
one make at a time**. No shared client or server link rule was invoked by a gate.
Logs: `/tmp/sailing-fin3/<target>.log`; exit codes: `/tmp/sailing-fin3/status.txt`.

## Results — 12 of 12 exit 0

| # | target | exit | status | pass line |
|---|---|---|---|---|
| 1 | `test-sailing-paint-order` | 0 | PASS | scene dependencies: flat before full, parent ground before boat; nested ownership, raised shore, loc order and zero-boat bytes PASS |
| 2 | `test-painters-world-entity` | 0 | PASS | all painter world-entity tests passed |
| 3 | `test-frame-flat` | 0 | PASS | frame flat live geometry, scale, colour, no-pick, replay lifetime and root identity PASS |
| 4 | `test-world` | 0 | PASS | All tests passed. |
| 5 | `test-minimenu-world` | 0 | PASS | All tests passed. |
| 6 | `test-cs2-sailing-settings` | 0 | PASS | PASS native cargo privacy0..2 through script3852 label writes, the8830/3967 paths, isolated script/varbit/struct scope and last-choice coalescing |
| 7 | `test-sailing-collision` | 0 | PASS | sailing_collision_test: passed (0 failures) |
| 8 | `test-mock239-playerinfo` | 0 | PASS | mock239-playerinfo: all tests passed |
| 9 | `test-wev-rebuild` | 0 | PASS | wev_rebuild_test: all passed |
| 10 | `test-wev` | 0 | PASS | wev_test: all passed |
| 11 | `test-rsprot-bridge` | 0 | PASS | rsprot-bridge: rsprot and osrs239_parse agree on every migrated packet |
| 12 | `test-social` | 0 | PASS | social: all checks passed |

Two targets more than the ten-target re-stamp: `test-painters-world-entity` and
`test-wev`. Durations were not recorded and are not claimed.

## What these twelve do and do not settle

- **`test-sailing-paint-order` is the direct gate on review item M-1**, the
  paint-order pass that no longer `malloc`s per frame. The test gained `memcmp`
  byte-identity controls for the flat pass, including `count == 0` (review item
  L-6), so a scratch-buffer regression that changed one byte of the emitted
  stream would fail it.
- **`test-world` is the gate on the half of SRV-1 that landed earlier**
  (`collision_nearest_opts_from_model`). It exits 0 at `OPT=1`, the
  configuration that used to fail. **It does not exercise the two server
  callers** of `ToriRSServer_SceneOpNearestOpts`; that half of the fix is
  verified by source inspection, not by a gate.
- **The other thirteen targets from the 25-target table were not re-run**, so
  their state on the final pair is unknown. `test-torirsserver-embed` in
  particular is still the long-standing decode break (**EMB-1**), neither
  confirmed nor cleared on `5e34b81f`.
