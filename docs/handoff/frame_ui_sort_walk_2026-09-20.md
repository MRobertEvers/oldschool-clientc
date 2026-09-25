# Moto X frame: the UI tree bucket, and where the face sort and scene walk can still move

Working notes, 2026-09-20. Device: Moto X XT1060 (Adreno 320, 2× Krait), `--gles3`,
uncapped, still camera, Lumbridge, the "benchmark" plugin set (performance-display
only). The starting profile is the one in the request (simpleperf cpu-clock 1 kHz,
25 s, frame thread only, 22,739 samples ≈ 16.2 CPU-ms per drawn frame):

| bucket | ms/frame | share |
|---|---:|---:|
| UI tree | 2.92 | 18.0% |
| face sort (painter order) | 2.35 | 14.5% |
| scene walk / visibility | 2.35 | 14.5% |
| ES3 renderer | 2.00 | 12.3% |
| projection / transform | 1.69 | 10.4% |
| command stream | 1.26 | 7.7% |

`scratchpad/bucket.py` reproduces this table from a report + the logcat of the same
window (frames = swap cadence × record duration), so the before/after below is in the
same units.

## 1. UI tree — what the 2.9 ms actually was

Per-symbol, the bucket is five walks over one tree:

| symbol | share | what it is |
|---|---:|---|
| `find_hovered_recursive` | 4.6% | the hover walk |
| `emit_walk_node` | 4.1% | the emit walk |
| `collect_nodes_recursive` | 1.9% | the input collect walk (world-click gate, per frame) |
| `UITree_CanvasMeasureCompact` | 1.9% | the canvas floor / chrome strip measure |
| `component_native_availability` + `UITree_NodeNativeVisible` | 2.5% | the native gate, mostly ancestor chains |
| `UITree_FrameReorder` | 0.6% | the anchor reorder of every walk's record list |

A headless Mac session on the phone's own manifest, prefs and account gives the per-frame
counts (TORIRS_PERF counters used for COUNTS only, not time):

- **7,170 components** in the tree once logged in; 3,222 of them are interface 162,
  the chatbox, and 2,654 of those are hidden dynamic chat-line children.
- The emit walk **enters 2,850 nodes a frame**, 766 pass the hidden gates, **113 draw**.
- The hover walk enters 2,770 a frame. With a single plugin widget anchor present
  (performance-display's window) the tree is on the "depth" path, where the hover and
  collect walks visit every node and record it so the anchor reorder can order the
  list; with no plugin the hover walk visits ~100.
- The retained-emit gate fires on 7 of 1,500 frames: `dirty_gen` moves ~49 times a
  frame and the topology generation ~19 times (the chat scripts `cc_create` ~6 nodes
  and `cc_deleteall` once per tick), so every consumer keyed on `generation` --
  the canvas candidate list, the anchor plan -- rebuilt every frame.

So the cost was not "the walk is slow", it was: ~2,000 hidden nodes per walk paying a
full ancestor-chain visibility walk (a host question per ancestor) before their own
hide bit was read; ~2,700 inert records per frame being sorted by the anchor reorder;
and a 7,000-component scan repeated twice a frame for a chrome measurement whose
inputs had not changed.

### What changed (all answer-preserving; `test-uitree` incl. the differential input-walk
proof and a new sidecar parity fuzz, `test-uitree-*`, `test-world`,
`test-painters-dynamic-pool`, plugin tests pass; `test-gameframe` has the same 17
pre-existing failures as HEAD)

1. **A reachable-children sidecar** (`UITreeComponent::visible_children`,
   `UITree_VisibleChildren`, `UITree_ChildHiddenForWalks`). Each container keeps, in
   sibling order, the children that no paint or input walk can reach: freed, screen/
   projection/widget/frame/mount hidden, native hide, or an IF3 script hide (an IF1
   script hide is hover-gated and stays in). Every child walk -- emit (both passes),
   hover, collect, interactive hit test, legacy hit test, subtree-paints-art, the
   overlay refresh -- iterates the sidecar instead of the sibling chain, so a hidden
   child is never entered. The walks keep their own flag checks, so a stale
   superset is only a missed saving, never a wrong answer. Maintained at the seams:
   append on link (tail of the chain = tail of the sidecar), remove on unlink or
   reclaim, reconcile in place when a reachability flag changes (`uitree_note_mutation`
   with `UITREE_IMPACT_REACHABILITY`: becoming hidden is a removal, becoming visible
   rebuilds), invalidate on sibling reorder, clear-children and a CcCopy's field copy;
   the storage is freed with the node. Built lazily on first use. Emit and hover
   entries on the gameframe: ~2,700 -> ~730 a frame, same 113 draw commands, same
   visited set (headless, TORIRS_DUMP_BOUNDS with a visited probe).
2. The hover walk no longer records inert entries (no candidate, no reset) and the
   collect walk drops all-flags-clear events. Exact for the anchor reorder because a
   unit's subtree is visited contiguously, so dropping records inside a unit's span
   cannot reorder survivors across units.
3. Scene walk side, found on the way: `World_CycleRegisterPainterDynamics` walked the
   ENTIRE scenery pool (every static loc of the scene, a pointer chase over tens of
   thousands of nodes) every frame to find runtime-spawned locs. It was 3.5% of the
   frame in a 6-instruction loop. `World::runtime_spawn_count` is maintained at the two
   flag seams and the pool reset; the walk is skipped while it is zero.

Not built yet: the scroll-range mode of the same sidecar (children sorted along the
scroll axis, binary search on the scrolled viewport). Exact under a scroll surface
because every descendant clip is intersected with it; it pays on the bank, settings
and world-map lists, not on this lane, where the chatbox's lines are hidden rather than
offscreen.

### Two things that were built, measured, and taken back out

- **A self-only native gate in the emit walk** (replacing the per-node ancestor chain of
  `UITree_NodeNativeVisible`). Reasoning said the chain was O(depth) per node; the phone
  said no: a hidden node fails the chain at ITSELF, so the chain only ran for the ~770
  visible nodes, whose ancestors are hot in cache. The replacement asked the host's
  availability of every entered node and measured +0.2..0.4 ms. Reverted. The emit
  walk's cost is entering ~2,850 nodes for 113 draws, and that is the tree's shape.
- **An appended-superset candidate list for `UITree_CanvasMeasureCompact`** instead of
  the per-frame rebuild the topology churn forces. -0.13 ms in one pair, +0.07 in the
  next: the two loops over ~4,000 candidates (every fixed-width child of a full-size
  layer, chat lines included) are the cost, not the rebuild, and a superset makes them
  longer. Reverted. The real fix is a smaller candidate set (strip candidates are
  right-anchored full-height nodes -- a handful; core candidates could be restricted
  to children whose authored width exceeds the parent's), or asking the question only
  when a frame bind or a window change happens rather than every frame.

### Measured on the phone (A = HEAD 8b2e9d37b, B = HEAD + the kept changes; the same
recipe as the request's profile, 25 s uncapped still camera, one build per arm in its
own worktree, arms alternated)

Final pair, with the sidecar (pair 5). Earlier pairs (1-3, hover/collect records and the
pool walk only) are in the scratchpad and agree symbol for symbol on what they share.

| CPU-ms per frame | A (HEAD) | B |
|---|---:|---:|
| whole frame | 15.93 | 13.74 |
| UI tree bucket | 2.82 | 1.34 |
| scene walk bucket | 2.55 | 1.93 |
| `find_hovered_recursive` | 0.765 | 0.067 |
| `emit_walk_node` | 0.650 | 0.093 |
| `collect_nodes_recursive` | 0.297 | 0.101 |
| `UITree_NodeNativeVisible` | 0.228 | 0.083 |
| `World_CycleRegisterPainterDynamics` | 0.574 | 0.001 |
| `UITree_FrameReorder` | 0.086 | 0.026 |
| `UITree_VisibleChildren` (new) | -- | 0.008 |
| `UITree_CanvasMeasureCompact` | 0.156 | 0.370 |

The whole-frame delta (-2.2 ms) is larger than the sum of the rows because runs of the
same arm drift ~1 ms; the rows are the evidence. The canvas measure's row varies
0.16-0.40 between runs of either arm and is now the largest UI item left: two loops
over ~4,000 candidates twice a frame, rebuilt whenever topology moves.

Reports, logs and the scripts that produced this are in the session scratchpad
(`sym_*.txt`, `prof_*.log`, `bucket.py`, `symdiff.py`, `prof_arm.sh`). The phone is left
with the HEAD build installed.

### What is left in the UI bucket, and why it was not taken

- The hover/collect walks still enter every REACHABLE node on the depth path (~730,
  they no longer record it). Pruning subtrees geometrically -- the legacy path's "children of a node
  the pointer is outside of are unreachable" -- would cut those walks to ~100 visits,
  but it is a semantic change: the depth path deliberately over-collects so anchored
  plugin widgets outside their container's box still take hover, and the differential
  test (`uitree_test_input_walk.c`, random scenes with children placed outside parents)
  pins that. An exact prune needs "no descendant clipping layer contains the pointer",
  which the walk cannot know locally; that is the next lever if you want one, and it
  is a design decision (whether anchored widgets may hover outside their parent).
- `UITree_CanvasMeasureCompact` (above) is now the first UI row.
- The emit walk still runs almost every frame because `dirty_gen` moves ~49 times a
  frame. `emit_dirty_topo` 19/frame is the chat scripts; the overlay-motion refresh
  path (`UITree_EmitOverlayMotionRefresh`) is gated `!app->plugins`, so with plugins on
  it never engages. Making retention fire on this lane is the larger structural win
  (~1 ms) and belongs to the redraw-plan work, not a walk tweak.
- `struct UITreeComponent` is 560 bytes and the walk-hot fields (type, parent,
  siblings, the six hide flags, position, behavior, hooks, `u`) span 8 of its 9 cache
  lines. Packing the hide flags and the walk fields into the first two lines would cut
  every walk's miss count; mechanical, but it touches every writer's field order and
  was not attempted here.

## 2. Face sort — where the 2.35 ms is and what can move it

Facts (Phase-0 census + ARMVX_KERNEL_STATE.md): ~1,330 models projected a frame, 763 of
them two-face terrain tiles that take the tile leaf (~57 ns each, ~45 µs total), ~500
real models averaging ~50 faces. The bench sorts at 25-30 ns/face; the client pays
~90 ns/face. The difference is per-MODEL fixed cost, which the previous sessions'
per-line profile put at roughly 35-40% of the sort: the dispatcher's union frame
(760 B + `vpush d8-d15`) and handle switches, `sort_model_inputs`, the radix count +
prefix per model, the priority partition + band merge (`sort_face_draw_order_small`,
0.95% on its own), and the `tmp_face_order` hand-off.

Levers, in order of what they are worth, none of which depends on a still camera:

1. **Do not sort what a depth buffer resolves.** `--gles3-zbuffer` draws opaque poses
   with `glDrawArrays`, no index stream, and sorts only genuinely blended faces. Your own
   runs this morning measured it: capped frame work 17.0-17.2 ms (`--gles3`) vs
   14.4-14.6 ms (`--gles3-zbuffer`), -2.6 ms, which is the whole sort bucket plus the
   index push. The question is only visual: RuneScape models rely on face PRIORITIES,
   not depth, for some intra-model layering, and the z-buffer arm must be checked for
   priority-dependent models (capes over bodies, hair, the transparent-face cases) before
   it is the default. If it passes, the sort bucket becomes "blended faces only".
2. **Per-model fixed cost on the painter path.** A leaf for small models (say ≤ 16
   faces: insertion sort on the depth key, no radix, no frame) the way the tile leaf
   bypasses the dispatcher; fold the priority partition into the radix scatter (one
   pass writes the band slices directly) and skip the merge when the model's
   priorities are uniform (already done for the key-order case); and hand the order to
   `es3_painter_push_indexed` without the `tmp_face_order` copy. Together this is the
   35-40% fixed share, so up to ~0.8 ms.
3. **The second Krait core.** Models are independent; the sort (and the projection) is
   embarrassingly parallel by model. The `--gles2-dualcore` lane already did exactly
   this for ES2 and got no speedup because its worker re-ran the frame bus and stalled
   the draw; the per-model claim arena it grew is the right seam. Moving just the sort
   of the ~500 non-tile models to the other core takes ~2 ms of CPU off the frame
   thread's critical path with no visual risk. This is the only lever that attacks the
   sort AND the walk AND the projection at once.
4. Not worth it: more NEON micro-work in the kernels (K16, the tail, the transposes).
   The bench per-face number is already 25-30 ns; the frame's per-face number is not
   dominated by the kernel.

## 3. Scene walk — where the 2.35 ms is and what can move it

`bucket_paint_world` is 10.3% of the frame, spread over 366 addresses -- there is no
hot spot, it is the algorithm: per frame ~1,200 tile pops, ~1,200 pushes, ~200 gate
rejects, 1,640 commands. At ~1.4 µs per pop the cost is in what each pop touches:
four neighbour `TilePaint`s for the reference gate (plus the seam exception's walk of
the neighbour's scenery chain), the tile's `PaintersTile`, its scenery chain, and for
every scenery element on the chain the footprint readiness scan over every tile of
its footprint -- repeated on every pop of every tile in that footprint until the
element draws. `World_CycleRegisterPainterDynamics` was the other 3.5% and is fixed
above (it was not the entities; it was the pool walk).

Levers:

1. **Footprint readiness in O(1).** Give each scenery element a per-frame countdown of
   footprint tiles not yet at GROUND (initialised when the element is first met, or from
   the footprint size in the per-frame `element_paints` clear); decrement when a
   footprint tile reaches GROUND (and again when a seam-relaxed tile un-relaxes, which
   is what makes the current scan re-check relaxed tiles). Ready == zero. This removes
   the O(footprint × pops) rescans, which for the multi-tile locs Lumbridge is full of
   is most of the per-pop work that is not memory traffic. Exact; camera-independent.
2. **Cheaper tile records.** `TilePaint` is 12 bytes and `PaintersTile` several
   tens, in separate arrays, and every gate test reads both for four neighbours: the walk's cache
   footprint per pop is ~10 lines. Packing the gate's inputs (`step`, `spans`,
   `seam_relaxed`, `in_queue`) into one byte array indexed like the tiles makes the
   four-neighbour gate one line and the row prefill one `memset`.
3. **`element_paints` clear.** `memset` of `element_count` bytes every frame (every
   loc, wall and decor of the loaded region -- tens of thousands); an epoch stamp (`drawn == frame_epoch`)
   makes it free. Small (~0.05 ms) but it is per frame and trivial.
4. **The seam exception's chain walk** (`bucket_neighbour_holds_only_nearer_scenery`,
   inside `bucket_gate_blocks`, 0.44% + inlined): it re-walks the neighbour's scenery
   chain per gate test; the chain is sorted once per tile per frame
   (`scenery_chain_sort_once`), so the "only nearer scenery" answer could be recorded
   at that sort and read back.
5. **The second core**, as above: the walk produces the command list the sort and
   projection consume; pipelining walk+project+sort one frame ahead is the plan's
   Phase 3, and it is the only way this bucket drops by more than a third without a
   still-camera cache.

Not taken and why: caching the walk per (camera tile, level, cullspan) -- the 2c lever
in FRAME_BUDGET_PLAN.md -- is exactly the still-camera optimisation the rule forbids,
even though this profile is a still camera and it would zero the bucket on it.

## 4. Measured: `--gles3` against `--gles3-zbuffer` on the committed HEAD (b3397fbd8)

Same phone, same lane, arms alternated, fresh install of the committed build.

Capped at the phone's 20 fps (frame work, medians of five 300-frame windows):

| arm | pair 1 | pair 2 |
|---|---:|---:|
| `--gles3` | 23.74 ms | 22.69 ms |
| `--gles3-zbuffer` | 20.79 ms | 21.11 ms |

Uncapped, 25 s simpleperf, CPU-ms per drawn frame:

| bucket | gles3 | gles3-zbuffer |
|---|---:|---:|
| whole frame | 13.94 | 11.36 |
| face sort | 2.49 | 0.00 |
| scene walk / visibility | 1.82 | 0.98 |
| ES3 renderer | 1.87 | 2.69 |
| projection | 1.59 | 1.30 |
| UI tree | 1.29 | 1.12 |
| command stream | 1.29 | 1.03 |

By symbol: the face sort is gone entirely (2.06 ms in the general body, 0.33 in the
priority merge and tile front); `bucket_paint_world` (1.69 ms) is replaced by
`painter_collect_visible_depth` (0.91 ms); the depth lane pays ~0.85 ms more inside the
renderer, almost all of it baking poses and faces into the stream
(`trspk_toridraw_bake_face_impl` 0.51, `es3_bake_pose_vertices` 0.34). Net about
2.6 ms a frame uncapped, 2.3 ms capped, ~18% of the frame.

Not measured here: the picture. The depth path resolves intra-model layering by depth
rather than the cache's face priorities, so priority-dependent models (capes over
bodies, hair, the transparent-face cases) need a visual pass before it is the default;
and once it is, its bake is the largest renderer-side row and the next thing to profile.
