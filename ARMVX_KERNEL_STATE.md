# ARMv7 / ARMv8 face-sort kernel state

The whole-frame budget plan (15 ms -> under 10) is `FRAME_BUDGET_PLAN.md`;
this file logs the face-sort steps of it.

Working notes for the bitonic+radix face sort lanes under
`3rd/toridraw/impl/facesort/`. Kept current while the work is in flight;
numbers are the sort bench (`toridraw_face_sort_bitonic_radix_test.c`,
`TORIDRAW_FACE_SORT_BENCH=1`, presort off, keys arm) in ns per INPUT face at
64 / 200 / 256 / 1000 / 2000 faces, then the two-face terrain tile.

Priority: **neon32 (Moto X, Krait, armeabi-v7a) first.** neon64 is parked at
the state below and gets picked up after neon32 is done.

## neon32 — `facesort.bitonic_radix.small.neon32.u.c`  (ACTIVE)

Current (both-arms run, 16K levels): ~44 / 38 / 37 / 41 / 47, tile ~98 ns
(bucket sort 68 / 60 / 59 / 59 / 80, tile 149); e34 build at REFERENCE
levels measured 41.5 / 37.1 / 34.8 / 38.3 / 44.7. Parity with the bucket sort PASS, 864
fixtures. Frame share on the phone: 18.4% -> 12.3% of samples before the
specialisation step; not yet re-measured in the client after it.

Done, in order, each measured on the phone (table in the lane's block4
comment):
1. Tile kernel routed (scalar tile): 117 -> 110 ns/face on the tile.
2. `TORIDRAW_FACE_SORT_BITONIC_MAX` 64 on this lane (256 elsewhere) — sweep
   table in the dispatch file.
3. Sentinel store + one scalar compaction pass instead of the ARM-side
   left-pack (the pack's NEON->ARM moves serialised every block):
   47.6/43.9/43.0/44.6/62.8 -> 46.6/40.7/40.7/42.1/49.6.
4. Interleaved `{x,y,z,0}` vertex copy per model (`scene->sm_vertex_xyz`) +
   VTRN transposes in place of twelve lane gathers — pays only together
   with 3.
5. Per-model specialisation: `block4` is `always_inline` with
   `int const spec_clipped, spec_stash`; `lane_blocks` dispatches four folded
   loops (`TORIDRAW_NEON32_BLOCK_LOOP`) like the bucket sort's
   `_plain/_clipped/_stash/_stash_clipped`: -> 44.3/39.4/36.1/39.4/46.1.

Rejected on measurement (kept as comments in the lane): NEON->ARM index
moves via vld1q_lane, pack through register moves, sorting the sentinels
instead of compacting, interleaved gather without 3.

Session 2026-09-01 (after the profile):
- Per-line profile of the lane on the phone (bench mix): gather + transposes
  ~25%, radix ~19% (count 4.5, prefix 4.1 FIXED per model, scatters 10.7),
  compaction 7.8%, winding/depth/key ~17%, AoS build 5.5%, bitonic ~1%,
  emit 0.6%. -> A1 (fuse emit) is worth < 1%: DROPPED.
6. E3/E4, kept: winding built negated so the front test is one shift (was
   shrn x2 + movn x2 + tst + cgt + ceq + and + or); key from a running
   `0xFFFF0000|face` lane vector, two ops. 41.8/38.0/36.0/39.2/45.8 ->
   41.5/37.1/34.8/38.3/44.7. Client: sort 12.3% -> 11.65% of samples.
7. E1, REJECTED: two independent block chains per loop trip, +5..10% —
   36 gathered q-registers against A32's 16, spills.
8. E2, IN (parity PASS phone + mac), client measurement pending:
   radix digits sized from depth_levels (7+7 at 16K, was 8+8: half the
   prefix/memset fixed cost) and a ONE-PASS radix for models whose depth
   range — bounded from the z min/max the neon32 interleave pass now
   collects for two ops a quad, published via scene->sm_sort_depth_lo/hi —
   fits in 256 levels. Sort returns which buffer holds the run
   (`out_keys`). `TORIDRAW_SORT_RADIX_LEGACY=1` = control arm. Bench:
   neutral (fixtures are random models: none shallow, prefix is a small
   share). Counters `g_toridraw_radix_{shallow,two_pass}_models` on the
   "gles2 sort/frame" logcat line say how many client models qualify.

Bench hygiene learned: the keys-only run (`_ARM=keys`) is ~15% slower per
face than the both-arms run (governor / cache warmth) — compare only within
one mode, and alternate arms on one binary. The bench scene is DEPTH_16K
now, as the client.

   Client: radix shallow 50 / two-pass 4 models per frame (93% shallow) —
   but only 54 of ~530 models reach the radix, so ~50 us/frame at best.
9. PRIORITY EMIT (found in the CLIENT profile, invisible in the bench):
   `partition_and_accumulate_faces_by_priority_keys` + the band merge were
   ~20% of the sort. (a) partition trimmed: band bases hoisted, branchless
   nibble, dead per-key `sm_prio_count` store dropped (nothing reads it);
   (b) `toridraw_face_priorities_uniform`: a model whose priorities are all
   one value emits in key order — one band occupied means the merge yields
   key order — checked per sort (word compare, ~0.1 instr/face; not cached
   on the model because the proctex tools rewrite priorities in place).
   Parity: 1265 fixtures incl. 401 uniform ones, PASS. Client census:
   367 uniform / 115 varied priced models per frame (76% fast path).
   Counters on the "gles2 sort/frame" line.

Client sort share (simpleperf, % of all samples, 10 s Lumbridge): 12.3
(pre-spec) -> 11.65 (e34) -> 12.06 (e2) -> 12.12 (prio): the live scene's
noise floor is ~±0.5%, so the per-line breakdown (`scratchpad/sort_lines.sh
<tag>`) is the measure for changes under that.

CLIENT A/B METHOD (the one that works): one binary, an env toggle, alternate
arms, metric = sort samples / frames-in-window = sort ms per frame, where
frames come from the 300-frame cadence of the "swap:" logcat line
(`scratchpad/client_ab.sh <tag> <on|off>`). Under the profiler the frame is
~20.4 ms. Uniform-priority fast path (`TORIDRAW_PRIO_UNIFORM=0` = off):
on 1.819 / 1.807 ms, off 2.000 ms -> -0.19 ms/frame (-9.5% of the sort).
A run whose frame cadence is off (e2: 25-31 ms) is disturbed: discard it.

Still to do (plan A/B):
- [ ] A4: classify the model's screen extent off `scene->aabb` (already
      computed before the sort by `toridraw_projected_bound`; x/y only, no z).
- [ ] B: K16 eight-face int16 block for models whose x/y extent fits int16
      after subtracting the model minimum (winding exact in vmull_s16, z
      widened with vaddl). Estimate says the gather dominates the block, so
      B is only worth it if the profile shows the winding/key arithmetic
      above ~30% of block time. Measure before building the whole thing.
- [ ] A2: reschedule the block by hand only if IPC is still < 1 after A1.
- [ ] Rebuild + install the client, profile a Lumbridge frame, record the
      sort's share after step 5.

Bench recipe: `scratchpad/phone_bench.sh <tag> [ENV=..]` (builds the armv7
unity, links the bench with the debug log's flags at -O3 -DNDEBUG, pushes,
runs). Client profile: `scratchpad/profile_device.sh <tag> <dur> <wait>`
(fresh account each run). simpleperf: <= 2 PMU events per stat, reboot if the
PMU wedges; TORIRS_PERF is never used.

## neon64 — `facesort.bitonic_radix.small.neon64.u.c`  (PARKED, consistent)

Current (M4 Max): 2.97 / 3.44 / 3.56 / 3.51 / 3.34, tile 5.07. Parity PASS.

Done: same specialisation as neon32 (`TORIDRAW_NEON64_BLOCK_LOOP`, neutral
here — the OoO core hid the compares), tile routed to the scalar kernel
(5.84 -> 5.09), sentinel store + compaction made permanent (vqtbl1q pack
table removed; 3.33 -> 2.94 at 64, -3..-9% above). Interleaved gather A/B run
and REJECTED (+10% at 200-256): neon64 keeps the lane-by-lane gather; the
A/B toggle, the `xyz` parameter and the AoS build are removed from the file.
`facesort.bitonic_radix.small.dispatch.h` describes both lanes as they are.

Not done on neon64: A1 (fused emit), A4/B, any measurement on an actual
AArch64 Android device — the M4 is the only aarch64 host measured.

## Shared contract changes already in

- `lane_blocks(..., int num_vertices, ..., int* out_accepted)`: returns keys
  WRITTEN, stores keys ACCEPTED; NEON lanes compact so the two are equal.
- `toridraw_face_sort_bitonic_radix(..., num_faces, num_vertices, ...)`;
  `facesort.dispatch.u.c` passes `sort_model_inputs.vertex_count`.
- `scene->sm_vertex_xyz`: `(max_vertices + 4) * 4` ints, allocated with the
  sort keys.
- sse2 / scalar lanes: signature only (`(void)num_vertices`,
  `*out_accepted`).

## Where the frame goes (Moto X, Lumbridge, painter, 2026-09-01, ab3 profile)

The loop is paced to 20 ms (50 fps, `frame_period_ms` in main.c); the main
thread is on-CPU 74% of the time = ~15.1 ms of work per frame, ~1.1 ms in
swap, the rest pacer sleep. Shares of on-CPU samples (ms of the 15.1):

| bucket | % | ms |
|---|---|---|
| GL driver + kernel (libGLESv2_adreno, libgsl, EGL, KGSL ioctls, faults) | 16.2 | 2.4 |
| face sort (compute_projected_face_order_small + one_abc + merge) | 14.0 | 2.1 |
| projection + cull (noclip/clip prepared, ProjectWithVTable, FastCull) | 13.6 | 2.05 |
| gles2 renderer CPU (dispatch, push_resident, reserve_model_indices, ...) | 9.8 | 1.5 |
| UI tree + fonts + ui draws | 9.1 | 1.35 |
| world paint walk (bucket_paint_world, occluders, painter dynamics) | 8.9 | 1.35 |
| frame command bus (ToriRS_FrameNextCommand, view apply, App_RunOnce) | 8.5 | 1.3 |
| libc (memcpy 1.5, strcmp 0.9, memset 0.6, mutexes 0.6, memmove/memcmp, getenv 0.3) | 5.1 | 0.8 |
| animation / texture animate / bake | 2.5 | 0.4 |
| scripting (Lua, CS1, revconfig) | 2.4 | 0.35 |
| PLT stubs | 1.3 | 0.2 |
| software 64/32-bit division (__udivmoddi4, __aeabi_uidiv) | 0.9 | 0.15 |
| long tail | ~7.8 | 1.2 |

Inside the sort (per-line, prio build): gather+transposes 17.6, arithmetic+
store 12.3, block loops+compaction 8.9, bitonic 7.2, radix 6.8, AoS build+
setup 5.2, partition+merge 6.0, caller/emit 10.8, dispatcher misc 13,
unattributed 9.

## 2026-09-01 evening: frame-level work moved to FRAME_BUDGET_PLAN.md

The sort stands at ~2.0 ms of a 13.8 ms frame (client), its internals
unchanged since step 9. Frame-level levers done today: model-line prefetch
in the emit loop, 32-bit hash maps, UI batching (64 → 30 draws), walk row
pre-fill, renderer peek-ahead prefetch, UI tree fixes. Rule recorded: no
lever may depend on a still camera. Next sort work if resumed: tile fast
path (bypass the dispatcher for two-face models), then K16.

## 2026-09-01 afternoon: K16 (eight-face int16 block) — IN

`facesort.bitonic_radix.small.neon32.u.c`: `block8_k16`. Per model, off the
raw screen box `toridraw_projected_bound` now publishes in
`scene->projected_box`: not near-clipped, no stash, >= 8 faces, box under
32767 wide and tall -> the vertices are rebuilt once as int16
`{x - min_x, y - min_y, z, 0}` quads (`scene->sm_vertex_xyz16`, the pass
also bounds z and falls back if it does not fit) and eight faces go per
block: D-register `vtrn` transposes, `vmull_s16`/`vmlsl_s16` winding (exact:
every delta an int16, every product < 2^30, the sum fits int32 with room),
z widened before the sum so the depth is the int32 block's bit for bit.
Keys identical -> the parity test is the gate: PASS, 1,265 fixtures, 522K
models through K16. `TORIDRAW_FACE_SORT_K16=0` = control arm. Census on the
"gles2 sort/frame" line.

Phone bench (fresh binary, keys ns/face at 64/200/256/1000/2000):
  K16 off  40.0 / 30.4 / 29.0 / 31.0 / 41.3
  K16 on   39.5 / 29.9 / 29.1 / 28.7 / 33.9    (-7.5% at 1000, -18% at 2000)

CAUTION on the morning's numbers: `phone_bench.sh` had been linking
against a stale binary (09:17) without saying so — every phone bench run
between e34 and this one measured the e34 build. So E1 (two blocks per
trip, "lost") and E2's bench verdict were never really measured; the
script now deletes the old binary and fails loudly. The client A/Bs were
unaffected (they build and install the client).

Client A/B (`TORIDRAW_FACE_SORT_K16`, interleaved pairs, sort ms/frame):
  on  2.066 / 2.134      off  2.165 / 2.308     -> about -0.13 ms (-6%)
468 models a frame take K16, 31 decline (the 763 terrain tiles never reach
lane_blocks: they have the tile kernel). Kept, default on.

Still open in the sort: the K16 tail (up to 7 faces a model go scalar; a
masked final block would take them), the tile fast path, and the K16 block
in the neon64 lane when that arch resumes.
