# ARMv7 / ARMv8 face-sort kernel state

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
