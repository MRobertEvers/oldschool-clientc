# UITree redraw and invalidation plan

## Purpose

Make retained UITree rendering sound by construction and make future incremental
layout, emit, and paint work possible without asking every callsite to understand
all cache dependencies.

The central rule is:

> Typed setters describe mutations, impact flags describe pending cache work,
> and dependency versions describe ambient inputs read from outside the tree.

This deliberately separates four things that are easy to conflate:

1. a semantic input changed (camera, hover, inventory, varp, drag, clock);
2. a derived value must be recomputed (for example, a world anchor's screen x/y);
3. a tree property changed (position, text, hide, scroll, topology);
4. one or more caches became stale (layout, emit descriptors, traversal/hit-test,
   or retained pixels).

No invalidation design removes dependency knowledge. It can live in arbitrary
callers, typed mutation APIs, explicit subscriptions, or automatic read tracking.
For this C tree, typed setters plus recorded reads are the lowest-risk places to
centralize it.

## Implementation status in this worktree

This worktree implements the correctness foundation, not partial command-list or
framebuffer redraw. In particular, it now has:

- one internal mutation-impact routine and typed by-index setters for common
  geometry, visibility, scrolling, CS1, text/sprite/model, colour,
  transparency, font, and model-pose fields;
- migrated camera-projected overlay positions and projection-owned visibility,
  so a near-plane projection failure removes the overlay instead of freezing
  its last sprite;
- canonical scroll publication plus one pure effective-scroll clamp shared by
  emit, hit testing, hover, drag/drop, wheel, and scrollbar geometry;
- migrated CS1 publication, frame visibility, async model publication, and
  selected app/CS2 runtime writes;
- eight coarse host-input domains whose used epochs are captured during emit and
  checked at the settled publication fence before retaining an ordinary buffer;
- retention rejects pending stale/forced layout state as well as a changed
  completed layout sequence;
- request-by-request dependency recording, including calls that return no
  overlays, and source-correct refresh of entity, canvas, and frame overlays;
- one shared production retained-emit predicate used by the app, unit tests,
  and the A/B harness, bound to an exact tree/buffer publication and revalidated
  after volatile callbacks;
- direct consumption of tree-owned CS1 results after typed publication, plus
  indexed standing-overlay refresh that avoids whole-list scans when sources
  are empty;
- focused tests for mutation impacts, unchanged writes, invalid inputs,
  dependency masking, stale-stamp rejection, and volatile overlay provenance.
- a deterministic before/oracle/after raster and benchmark suite with exact RGB
  comparisons, statistical performance gates, screenshots, diffs, reports,
  JUnit, and artifact checksums.

The app currently derives coarse source signatures at the publication fence and
also bumps inventory/client-state domains from authoritative callbacks. This is
a conservative bridge to source-owned revisions. Asset signatures based on ids
and map cardinalities cannot detect every same-id/same-cardinality replacement,
so authoritative bridge/provider revisions and the forced-walk verifier remain
future correctness work.

Still deliberately deferred are typed drag begin/move/end mutations, mutation
transactions, a direct-write CI checker, keyed dependencies, per-node command
spans, partial emit, and compositor damage. The phase checklist below is the
source of truth for that remaining scope.

## Baseline before this worktree

At the audited baseline, UITree had several independent invalidation signals:

- `UITreeComponent::is_dirty` and `always_dirty` describe prospective per-node
  emit eligibility. The production `UITree_EmitWalk` still walks every drawable
  node when it runs, so these bits do not yet select a partial emit.
- `UITree::dirty_gen` is the component-mutation term of the whole-list retention
  gate. `UITree_MarkNodeDirty` filters its increment by whether the previous emit
  reached the node.
- `UITreeElemPosition::layout_resolved`, `layout_stale`, and
  `layout_resolve_seq` describe cached layout boxes and whether a resolve ran.
- `UITree::generation` describes topology.
- The hovered component id is compared independently because hover variants are
  read during emit without mutating a component.
- Host-owned same-frame pointers are refreshed through the volatile descriptor
  path; other host reads are not yet represented in the retention identity.

This is sound only when every live-tree writer reproduces the full contract for
the field it changes. A position write, for example, must clear the resolved box,
make layout stale, and advance emit identity. A hide write additionally changes
reachability and cannot use the last-emit reachability filter. That distributed
contract is the class of bug this plan removes.

## Invariants

The implementation and tests must preserve these invariants:

1. **No stale retain:** if a forced full walk would produce a different command
   list, the retention gate must not reuse the previous ordinary descriptors.
2. **No stale geometry:** every mutation to a layout input schedules a resolve;
   geometry getters in the same script observe the updated result.
3. **Reachability is conservative:** show, hide, mount, unmount, create, delete,
   and reparent changes cannot be filtered using reachability from the state they
   are replacing.
4. **One semantic write seam:** published/live nodes are mutated through typed
   setters. Direct struct writes are limited to construction of unpublished
   nodes and tightly documented cache internals.
5. **Compare before invalidate:** setters that receive the current value are
   no-ops. Correctness may over-invalidate, but steady-state scripts must not
   manufacture work by restating unchanged values.
6. **Propagation is cache-owned:** callers state which property changed. They do
   not choose `PARENT` or `SUBTREE`; the cache policy derives the affected scope.
7. **External reads are explicit:** any non-tree input used to build an ordinary
   descriptor participates in a versioned dependency stamp or is classified as
   a refreshable volatile input.
8. **Semantic hooks are not dirty flags:** `onMouseOver`, transmit hooks, and
   timers may mutate arbitrary nodes. The hook runs; its resulting typed writes
   record the real impacts.
9. **Pixel damage is not descriptor invalidation:** alpha/backdrop interactions
   belong to a future compositor damage layer, not a node-to-node subscription
   graph.

## Target architecture

```text
external source version changes
             |
             +--> binding/projector recomputes derived component state
             |        -> typed setter compares and writes
             |        -> central impacts invalidate layout/emit/traversal
             |
             +--> emit reads ambient host state
                      -> recorded dependency epoch changes
                      -> retained-buffer stamp fails; full emit runs

future cache work from a component mutation:
    layout roots | emit spans | hit-test roots | old/new paint bounds
```

### 1. Typed mutation layer

All existing component-id `UITree_Apply*` operations become lookup wrappers over
by-index setters. Systems that already hold a node index must not perform another
id lookup or write component storage directly.

Representative API:

```c
bool UITree_SetPositionAt(struct UITree*, int32_t node, int x, int y);
bool UITree_SetSizeAt(struct UITree*, int32_t node, int width, int height);
bool UITree_SetHideAt(struct UITree*, int32_t node, int hide);
bool UITree_SetScrollPosAt(struct UITree*, int32_t node, int x, int y);
bool UITree_SetTextAt(struct UITree*, int32_t node, char const* text);
```

The public setters express properties, not cache mechanics. Each successful
change calls one internal impact routine. A generic `Invalidate(mask, scope)` may
exist as a private escape hatch, but it is not the normal caller-facing API.

### 2. Impact policy

Impacts are internal cache facts. Scopes are per domain because one mutation can
affect layout descendants, the current node's scrollbar descriptor, an ancestor's
intrinsic extent, or a suffix of painter order. A single public
`INVALIDATE_SUBTREE` flag cannot express those cases safely.

Initial change kinds:

```c
enum UITreeChangeKind {
    UITREE_CHANGE_VISUAL,          /* text/colour/sprite/model/trans */
    UITREE_CHANGE_GEOMETRY,        /* position/size/modes */
    UITREE_CHANGE_CHILD_SPACE,     /* scroll position/extent */
    UITREE_CHANGE_REACHABILITY,    /* hide/show/frame visibility */
    UITREE_CHANGE_TOPOLOGY,        /* create/delete/reparent/order */
    UITREE_CHANGE_INTERACTION      /* hooks/click/menu only */
};
```

The centralized property/effect matrix begins as follows:

| Property mutation | Layout | Emit identity | Traversal / hit-test | Paint bounds |
|---|---|---|---|---|
| text, colour, graphic, model, own transparency | none | self | none unless hitbox derives from content | self |
| position, size, alignment modes | changed node; resolver propagates parent-box changes | affected branch | affected branch | old and new branch bounds |
| scroll position | no box-size resolve | layer and descendants | layer and descendants | layer clip |
| scroll extent | child coordinate space | layer and descendants | descendants | layer clip |
| hide/show | none unless layout policy says otherwise | affected branch, unfiltered | reachability branch | old/new visible bounds |
| create/delete/reparent/order | affected old/new branches | affected painter ranges | affected old/new branches | old/new branches |
| hook, operation, click-mask metadata | none | only if emit reads the field | interaction target | none |

Dirty subtree work is represented by roots in a set or queue. It is not an eager
walk setting a byte on every descendant. The existing parent-before-child layout
resolver already has the right place to propagate a changed parent box.

### 3. Mutation transactions

Multi-field CS2 operations and frame reconciliation should eventually run inside
a transaction:

```c
UITree_BeginMutation(tree);
UITree_SetPositionAt(tree, node, x, y);
UITree_SetSizeAt(tree, node, w, h);
UITree_EndMutation(tree);
```

Transactions coalesce generation increments, dirty roots, old bounds, and debug
audit work. Typed setters remain mandatory inside the transaction; a transaction
is not permission to take mutable component pointers.

### 4. External dependency versions

Use dependencies for ambient inputs, not for ordinary component writes:

```c
enum UITreeDependency {
    UITREE_DEP_CAMERA,
    UITREE_DEP_HOVER,
    UITREE_DEP_MOUSE_POSITION,
    UITREE_DEP_DRAG,
    UITREE_DEP_CLIENT_CYCLE,
    UITREE_DEP_INVENTORY,   /* eventually keyed by inventory id */
    UITREE_DEP_VARP,        /* eventually keyed by varp id */
    UITREE_DEP_ENTITY,      /* eventually keyed by uid */
    UITREE_DEP_ASSETS,
    UITREE_DEP_HOST_UI
};
```

Source owners advance monotonically increasing versions. During a full emit,
host reads record the dependency domains used and snapshot their versions into
the emit buffer. Retention is permitted only when every recorded version still
matches. Dynamic read recording is preferable to permanent manual subscriptions:
it handles branch-dependent reads, keyed resources, newly-created nodes, and
dependencies that disappear after a state transition.

The first implementation is deliberately coarse: a fixed set of domains and a
whole-buffer dependency mask/stamp. It improves soundness without claiming that
the variable-length ordered command list is already patchable per node. Keyed
versions and reverse node indexes are added only with partial emit.

Camera-projected overlays illustrate the division of responsibility:

```text
camera version changes
    -> entity-overlay projector reruns
    -> UITree_SetPositionAt computes a real component mutation
    -> geometry impact invalidates layout and emit identity
```

Marking the old sprite descriptor dirty without rerunning projection would emit
the same stale x/y and is therefore not a valid camera dependency response.

Hover has two separate paths: the old/new hovered ids affect visual variants;
CS2 mouse hooks execute behavior and rely on typed setters for whatever they
actually change.

### 5. Renderer damage and transparency

`TRANSPARENCY_COMPONENT_BEHIND_CHANGED` is not a UITree dependency in the current
renderer. Commands are rasterized again in painter order, so an unchanged alpha
command blends over newly-rendered background pixels correctly.

If framebuffer pixels or offscreen layers are retained later, maintain damaged
rectangles or stacking-context damage. Repaint commands intersecting the union of
old/new bounds in z-order. Do not construct a dynamic graph connecting every
translucent node to everything geometrically behind it.

## Direct-write policy

Direct writes to a live `tree->components[node]` are prohibited except in:

- UITree construction before the node is published;
- layout resolver output fields owned solely by the layout cache;
- emit reachability scratch owned solely by the emit walk;
- narrowly documented migration shims with an issue/phase reference.

Enforcement progresses from an audited allowlist to CI/lint. The highest-risk
production writes are geometry, `behavior.hide`, transparency, model pose,
scroll state, and fields read by `UITree_EmitFill`. Read-only component access
remains allowed. Long term, expose live components through const handles and keep
mutable storage private to UITree internals.

### Audited live-write inventory

The initial audit distinguishes correctness hazards from construction/cache-owned
writes. Line numbers move as this plan lands; paths and semantic owners are the
stable identifiers.

| Priority | Writer | Worktree status and remaining risk |
|---|---|---|
| P0 | `src/game/task_cs1_run.c` | **Migrated:** CS1 active/value publication uses typed setters and advances emit identity only on change. |
| P0 | `src/ui/uitree_scroll.c`, `src/ui/uitree_interact.c` | **Migrated:** scrollbar and wheel publication use `UITree_SetScrollPosAt`; scroll is canonicalized at publication and every render/input consumer uses the same pure effective clamp. |
| P0 | `src/ui/uitree_input.c`, `src/ui/uitree_interact.c`, drag helpers in `uitree.c` | **Pending:** drag active/offset/transparency changes affect a hoisted subtree and painter order. Specify typed begin/move/end semantics before migration. |
| P1 | `src/app.c` | **Migrated in audited paths:** minimenu font, force-show visibility, and local model binding use typed setters. Continue auditing unrelated runtime fields in Phase 3. |
| P1 | `src/game/task_cs2_run.c` | **Migrated:** speculative visibility and async model arrival use typed setters. Authoritative asset revisions remain pending. |
| P1 | `src/ui/uitree_frame.c` | **Migrated:** frame visibility uses the reachability-aware setter. |
| P1 | `src/ui/uitree_obj_cell.c` | **Partly migrated:** hide changes use the typed setter; atomic object-cell transactions remain pending. |
| P1 | `src/game/rs_cs2_host.c` | **Partly migrated:** arc angles, model pose, transparency, and scroll publication use typed setters; the wider host audit remains Phase 3. |
| P2 | `src/game/rs_gameproto_exec.c`, `src/game/rs_clientcode.c` | **Partly migrated:** `rs_clientcode.c` text, scroll extent, and design-preview model pose use typed setters; `rs_gameproto_exec.c` and the remaining runtime fields still need the Phase 3 audit. |
| Design cleanup | `src/ui/uitree_emit.c` | **Migrated:** emit computes a local clamped scroll value and does not mutate canonical tree state. |

Allowed categories are unpublished builder/bake initialization, layout-owned
`abs_*` / `layout_resolved` outputs, emit-owned reachability scratch, and confirmed
scene-model animation where descriptors intentionally reference a separately
versioned mutable render resource. “Published” means visible past the settled
frame/emit fence; linking a node during a multi-step bake does not by itself end
the construction exemption.

## Diagnostics and correctness oracles

1. Keep `TORIRS_EMIT_VERIFY=1`: force a full walk even when the gate is quiet and
   compare it with the retained result. Any `[emit-unsound]` report is a release
   blocker for retention changes.
2. Add a debug mutation audit that fingerprints fields read by layout/emit/hit
   testing. If the fingerprint changes without the corresponding mutation or
   dependency epoch, report the node, component id, field domain, and last writer
   when available.
3. Add randomized setter sequences that compare incremental state with a forced
   full layout plus full emit oracle.
4. Count invalidations by change kind, dirty root count, dependency domain,
   retained frames, forced walks, and false-positive rebuilds.
5. Keep compare-before-write counters so optimization work can distinguish real
   state changes from scripts restating current values.
6. Run `tools/uitree_redraw_suite.py` for a cache-independent four-way oracle:
   `origin/v3` forced and production-retained redraws plus candidate forced and
   production-retained redraws. Forced-vs-forced proves implementation parity;
   candidate retained-vs-forced proves retention soundness; legacy retained
   differences are accepted only at named host-input regressions and become the
   before/fix evidence. Benchmark timings compare production policy with
   production policy in separate processes, with screenshots and verification
   work outside timed regions.

## Risks and mitigations

- **Effect names can overpromise scope.** `EMIT_SELF` is safe today only because
  any reached-node bump invalidates the whole buffer. Before partial emit, replace
  it with explicit stable command-span/branch semantics and test painter order.
- **Reachability is self-invalidating evidence.** A previously unvisited node can
  become visited after unhide/mount/reparent, so those transitions always use an
  unfiltered epoch rather than the old `emit_visited` bitmap.
- **Epoch producers can forget to publish.** Versioned dependencies move the
  contract to authoritative source owners but do not make omissions impossible.
  Request classification tests, debug fingerprints, and forced-walk comparison
  remain required.
- **Epochs do not extend pointer lifetimes.** Same-frame host pointers stay on the
  refreshable/unrefreshable volatile path even when their source has a version.
- **Over-invalidation can erase the optimization.** Setters compare before write;
  transactions coalesce bumps; performance counters identify noisy domains and
  sources.
- **Node indexes are recycled.** Deferred dirty roots and subscriptions carry a
  node incarnation, not a naked array index.
- **Transactions can break same-script reads.** `UITree_EnsureLayout` must still
  observe geometry mutations before `EndMutation` when CS2 asks for computed
  dimensions in the same script.
- **Epoch wraparound is equality-safe only for bounded retention.** Emit stamps
  are short-lived frame artifacts; no stamp may survive long enough for a full
  counter wrap.
- **A direct-write lint can produce false confidence.** Keep an explicit allowlist
  for bake and cache-owned writes, and retain runtime fingerprint verification.
- **Animation ownership is mixed.** Confirm whether a descriptor copies a pose or
  references a mutable scene model before classifying animation as a component
  mutation versus a render-resource dependency.

## Implementation phases

### Phase 0: establish the oracle and vocabulary

- [x] Document current generations, reachability filtering, layout invalidation,
      volatile descriptors, and hover identity.
- [x] Define invariants, change kinds, dependency domains, and non-goals.
- [x] Add focused tests for the centralized change policy before migrating every
      setter.

### Phase 1: centralize component mutations

- [x] Add the internal impact routine.
- [x] Add by-index typed setters for geometry, visibility, scrolling, and common
      visual fields.
- [x] Convert the corresponding component-id `UITree_Apply*` functions into
      wrappers; migrate the remaining API surface with its writers in Phase 3.
- [x] Migrate camera overlay, frame-layout, and selected CS2 host callsites that
      already hold node indexes.
- [x] Preserve compare-before-write and frame-owned-position semantics.
- [x] Add tests for geometry, reachability, scrolling, unchanged writes, and
      invalid node/type rejection.
- [x] Separate camera/projection visibility from script-owned `behavior.hide`,
      including near-plane hide/reveal coverage.
- [x] Canonicalize scroll at typed publication and use one pure effective-scroll
      calculation across paint, hit testing, hover, drag/drop, and scrollbars.
- [x] Reject retention while layout has pending stale/forced work, before the
      completed layout sequence has advanced.
- [x] Migrate CS1 result publication and scrollbar/wheel scrolling, the two P0
      direct-write paths that can currently leave `dirty_gen` quiet.
- [ ] Specify and test drag begin/move/end impacts before migrating drag state;
      it changes subtree translation and top-pass ordering.

### Phase 2: whole-buffer external dependency stamps

- [x] Add coarse dependency versions and a fixed dependency mask/stamp.
- [x] Classify every host request made by emit as stable tree data, versioned
      external data, or refreshable/unrefreshable volatile data.
- [x] Record dependencies during a full walk and validate them before retention.
- [x] Publish coarse semantic signatures at the settled app fence and advance
      inventory/client-state versions at audited authoritative transitions.
- [x] Preserve zero-result volatile refresh records and refresh entity, canvas,
      and frame overlays through their original request kinds.
- [ ] Replace heuristic asset/world signatures with authoritative source-owned
      revisions for all same-id replacement and in-place mutation paths.
- [x] Test changed-used, changed-unused, stale stamps, request classification,
      and volatile interactions.
- [ ] Add an explicit wraparound test or adopt a wider/non-wrapping production
      generation policy.

### Phase 3: close direct-write escapes

- [ ] Migrate remaining live production writes.
- [ ] Add an allowlist-based direct-write checker to CI.
- [ ] Add debug domain fingerprints and actionable unsound-mutation reports.
- [ ] Make component access const by default where APIs permit it.

### Phase 4: split cache-owned epochs and dirty-root queues

- [ ] Replace ambiguous uses of `dirty_gen` with cache-owned identities for
      emit descriptors, layout inputs, traversal/hit testing, and topology.
- [ ] Queue minimal dirty roots per domain and coalesce them in transactions.
- [ ] Keep old/new resolved bounds for future paint damage.
- [ ] Prove the global retention gate equivalent to a forced full walk before
      enabling finer-grained skips.

### Phase 5: partial emit, only if measured worthwhile

- [ ] Give each node a stable emitted span identity or chunk handle.
- [ ] Record per-span dependency versions and reverse dependency indexes.
- [ ] Rebuild dirty spans and repair ordered offsets without violating the two
      non-text/text passes, subtree hoisting, clips, or painter order.
- [ ] Fall back to full walk for topology/order churn and large dirty sets.
- [ ] Benchmark command churn and memory overhead against the current full walk.

### Phase 6: compositor damage, only if pixels become retained

- [ ] Track old/new bounds and surface/stacking-context ownership.
- [ ] Union damaged rectangles and repaint intersecting command ranges in order.
- [ ] Handle alpha, filters/backdrops, clips, and offscreen surfaces in the
      compositor; do not add node-level "component behind changed" flags.

## Verification matrix

Every phase runs the narrow tests first, then the full native checks relevant to
the changed boundary:

| Area | Required checks |
|---|---|
| typed setters / impact policy | UITree dirty, layout, mutate-emit, frame-layout, scripted-overlay tests |
| dependency stamps | emit host-stub tests for every classified domain; retention verifier |
| CS2 writer migration | client-trigger and CS2 host tests |
| topology/reachability | create/copy/delete, interface mount, hide/unhide tests |
| integration | `make -C src test-uitree`, `make -C src test-client-trigger`, optimized native link |
| camera regression | deterministic orbit capture with fishing sprite aligned to its world marker |
| retention soundness | representative replay with `TORIRS_EMIT_VERIFY=1`, zero unsound reports |
| A/B appearance and speed | `python3 tools/uitree_redraw_suite.py --profile quick` for PRs; `--profile full` for release/nightly evidence |

Focused oracle cases also include: CS1-only active/value changes, wheel and
scrollbar child movement, drag begin/move/end ordering, unhide of a previously
unvisited node, frame-hidden release, asynchronous model arrival, unchanged
setter epochs, changed-used versus changed-unused host domains, volatile refresh,
and randomized typed mutations compared with forced full layout plus emit.

For performance work, record before/after values for full walks, retained frames,
layout nodes visited, dirty roots, emit descriptors rebuilt, host dependency
invalidations, and total frame time. Correctness gates land before performance
claims.

## Deterministic A/B redraw suite

The implementation in `test/uitree_redraw/` and
`tools/uitree_redraw_suite.py` makes appearance and speed claims reproducible
without a cache, save, network session, or interactive camera. The runner
creates a detached worktree at the resolved `origin/v3` commit and builds the
same C driver against both source revisions. A release run refuses a dirty
candidate worktree; `--allow-dirty` exists only for development evidence and is
labelled as such.

Four independent visual processes are required:

1. baseline with full walks forced — the old implementation's correct-picture
   oracle;
2. baseline with its actual App-local production retention policy — the
   user-visible before state;
3. candidate with full walks forced — the candidate correct-picture oracle;
4. candidate with its shared production retention predicate — the optimized
   after state.

Every frame records scenario/checkpoint, normalized pixel hash, semantic command
hash, descriptor count, full walks, and retained frames. Each 32-bit BMP is
decoded to top-to-bottom RGB. Release gates require:

```text
baseline forced RGB == candidate forced RGB
candidate retained RGB == candidate forced RGB
different pixels == 0 and maximum channel delta == 0
```

Legacy retained output must equal the oracle outside the explicitly named
`host-camera` and `host-input` frames, must differ in at least one such frame,
and must recover on the following tree mutation. The gallery consequently shows
the honest production-retained before image, forced oracle, candidate-retained
after image, and exact diff instead of pretending that `origin/v3` always did a
full walk.

The 24-state seeded trace covers initial/steady retention, hover enter/leave,
two-axis nested scrolling, text/colour/alpha changes, randomized dynamic child
replacement, host-only camera and typed CS1 publication changes, previously unreachable
hide/unhide, volatile overlay 0→1→2→0 refresh, arc/sprite transforms, and a real
fishing-marker orbit. `App` and the harness call the same pure integer orbit and
world-point projection leaf; fixed tile-centre goldens cover move, viewport
clip, near-plane hide, and reveal without assigning screen coordinates as test
inputs. The retained process uses an independent reference fixture, so its full
oracle cannot mutate production reachability scratch. A separate 46-image lane
compiles the real display-list translator, baked fonts, and Soft3D rasterizer
against both revisions.

Performance trials run three independent processes with the same seed:
baseline production retention, candidate production retention, and candidate
forced full walk. A balanced six-order schedule removes a fixed thermal/order
bias. The report uses paired candidate/baseline ratios and deterministic paired
bootstrap 95% confidence intervals. `steady` and `correct_aggregate` (steady,
overlay position, scroll, hover, content, and topology) must have an upper bound
below 1.0. The stale legacy camera workload is reported but excluded from the
speed claim; candidate retained-vs-forced measurements separately record the
cost of producing the correct camera frame and the benefit on steady frames.
Mutation workloads keep the 3%/100 ns non-regression guard.

Profiles are intentionally different sizes:

| Profile | Visual frames | Timed operations per scenario | Process pairs | Bootstrap samples |
|---|---:|---:|---:|---:|
| `quick` | 256 | 4,000 | 6 | 5,000 |
| `full` | 2,048 | 30,000 | 18 | 20,000 |

Every complete run first archives the full UITree tests, client-trigger tests,
CS2 frame-settlement tests, and an optimized native-client link. It then
preserves `manifest.json`, JSON/CSV summaries, a Markdown report, raw per-process
metrics, all before/oracle/after BMPs, decoded-RGB diff BMPs, an
HTML gallery, JUnit, command logs, and `SHA256SUMS`, including on failure. The
default artifact root is under ignored `build/uitree-redraw/`; large generated
evidence is uploaded by CI or retained locally rather than committed as source
goldens. The exact runner/output contract and optional cache-backed full-client
replay extension are documented in `test/uitree_redraw/README.md`.

### Recorded PR-profile result (2026-08-24)

The clean `quick` run compared candidate `93ca8ceef6e3e376c6c81e4077fdac05d63bf01c`
with the immutable baseline `e6a4364221d5bc8c54e98d5614d84f8b42871b16`.
The candidate tree was clean, all required lanes ran, the evidence was marked
complete and reproducible, and all **882/882 gates passed**.

- **Exact appearance parity:** all 256 baseline-forced/candidate-forced pairs
  and all 256 candidate-retained/candidate-forced pairs had zero differing RGB
  pixels and a maximum channel delta of zero. All 46 real Soft3D chrome pairs
  were also exact. The legacy production-retained baseline demonstrated the
  regression in 31 named host-only checkpoints (11 camera and 20 other host
  inputs), while every non-allowlisted checkpoint remained exact.
- **Primary steady-state speed:** 13,971.000 ns/op before versus 35.250 ns/op
  after; median paired ratio 0.00252 with 95% bootstrap CI
  `[0.00244, 0.00272]`.
- **Correctness-qualified aggregate speed:** 273,293.125 ns/op before versus
  263,298.062 ns/op after; median paired ratio 0.96187 with 95% bootstrap CI
  `[0.95944, 0.96466]`. This aggregate excludes the legacy camera result because
  that path rendered a stale frame. The candidate's correct camera path was
  separately guarded against its own forced-full oracle and passed at ratio
  1.00193 with CI `[0.99751, 1.00386]`.
- **Evidence volume:** six balanced process-order trials at 4,000 operations per
  scenario, 5,000 paired bootstrap samples, 2,022 BMP files, 2,114 checksummed
  artifacts, archived native build/test logs, JSON/CSV summaries, JUnit, and an
  HTML before/oracle/after/diff gallery.

The complete local artifact is
`build/uitree-redraw/quick-code-93ca/`; `report.md`, `manifest.json`,
`summary.json`, and `SHA256SUMS` contain the human-readable result, immutable
source/tool identity, machine-readable gates, and integrity hashes respectively.
Development runs may be used to tune the suite, but cannot support the PR's
appearance or performance claim.

## Verification performed in this worktree

- [x] `make -C src test-uitree` — all UITree tests passed, including the new
      mutation, dependency-stamp, volatile-provenance, projection, and scroll
      regressions.
- [x] `make -C src test-client-trigger` — 35 checks, 0 failures.
- [x] `make -C src test-cs2-frame-settle` — all settle/fence cases passed.
- [x] `make -C src -B OPT=1 all` — clean optimized rebuild and native link
      completed; only existing unrelated compiler/linker warnings were emitted.
- [x] Run the clean four-way `quick` profile and record its gate/statistics
      summary above.
- [x] `git diff --check` — no whitespace errors.
- [x] Run the deterministic App-shared fishing-overlay projection/orbit capture
      (move, clip, near-plane hide, and reveal goldens).
- [ ] Run a representative replay with `TORIRS_EMIT_VERIFY=1` and confirm zero
      `[emit-unsound]` reports before shipping retention broadly.

## Acceptance criteria for the foundation worktree

The initial implementation is complete when:

- common runtime mutations use typed by-index setters and a centralized impact
  policy;
- existing component-id APIs preserve behavior as wrappers;
- camera-projected overlay movement uses the generic mutation seam;
- the emit buffer can record coarse external dependency versions and reject a
  stale stamp;
- direct-write audit output is documented and the foundation-scope high-risk
  escapes are migrated, with drag and the wider audit explicitly staged;
- focused and full UITree tests pass, the client-trigger suite passes, and the
  optimized native client links;
- the full future migration remains explicitly staged rather than claiming that
  per-node emit or framebuffer damage already exists.

## Non-goals for the foundation worktree

- Do not implement a universal event bus for semantic hooks.
- Do not eagerly mark every descendant for every geometry mutation.
- Do not patch arbitrary slices of the variable-length emit list before stable
  span ownership and ordering rules exist.
- Do not retain framebuffer pixels or add alpha-backdrop dependency graphs.
- Do not sacrifice same-script layout reads or compatibility with frame-owned
  plugin positions for a cleaner-looking API.
- Do not remove the forced full-walk verifier until the replacement has an
  equally strong soundness oracle.

## Rollback and compatibility

Typed setters initially preserve the existing dirty/layout/topology operations;
centralization is a behavior-preserving refactor guarded by tests. Dependency
stamps are conservative: an unknown or changed dependency forces a full emit.
Partial emit and compositor damage remain behind later milestones and must retain
a full-walk/full-raster fallback. This makes each phase independently reversible
without changing CS2-visible component semantics.
