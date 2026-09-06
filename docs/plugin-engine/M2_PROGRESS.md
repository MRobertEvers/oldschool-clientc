# M2 implementation evidence

M2 is in progress. This checkpoint fixes native identity, geometry readback,
content/hide independence, mount suppression and cycle rejection. It does not
complete the mutation audit or the major-3 production cutover.

Implemented in the shared production tree:

- `UITreeNodeRef` validates tree instance, slot and 64-bit incarnation. Focus
  and queued CS2 drag pickup use it. Existing UI retained-incarnation storage
  is widened consistently; resource tokens have their separate lifetime rules.
- A computed zero width/height remains zero in native getters.
- `ApplyObject` changes the addressed component's content. It neither clears
  native hiding nor redirects through guessed equipment children or changes a
  sibling's visibility. Native scripts retain their explicit decoration writes.
- `mount_hidden` is independent of native/cache hide. Async preparation,
  spillover, subinterface mounting and opentop use `SetMountHiddenAt`. Its
  mutation invalidates layout, paint and reachability. Availability, input,
  scrolling, hover and snapshot readers include that restriction.
- Reparent validates the proposed ancestor chain before touching links. Invalid
  targets and cycles fail atomically; valid moves preserve node identity.

The existing UITree harness runs the contract cases in its ordinary suite.
Cases cover recycled native IDs, matching slots/incarnations in different
trees, crossing the old 32-bit incarnation boundary, stale queued pickup,
explicit focus transfer, zero geometry, interleaved native/mount hiding,
behavior replacement during suppression and cyclic reparent attempts.

Verification uses `/private/tmp/plugin-engine-evidence`:

| Evidence | Result |
|---|---|
| `m2-negative-controls-final` | Temporary mutations are compiled into the existing UITree harness; tree identity, incarnation identity, zero geometry, content hiding, mount hiding and topology must each fail their named assertion. Run `python3 tools/plugin_engine_negative_controls.py NEW_DIRECTORY`. Compilation failures or crashes do not count. |
| `m2-osrs-native` | Four prepared native roots pass: 548, 161, 164, 601. Inspected at 2×: 8/8/8/7 chat controls and six green mode labels each. |
| `m2-lc-native` | Both real packet/CS1 scenarios pass. Two distinct accounts verified. Inspected at 2×: 19 skill cells, strength 20/20, four chat captions and three green modes; guide has Attack, Defence and Close controls. |
| `m2-osrs-equipment` | Actual equipment panel and native equip rejection remain operational: rune platebody is rejected by the Dragon Slayer requirement. This is denial evidence, not a successful equipment mutation. |
| `m2-osrs-equipment-live` | Real `item 1079 1`, `equip 3`, then click the worn legs slot. Server save has old legs 27241 and rune legs 1079 in backpack and worn slot 7 empty. Enlarged client capture shows the empty legs silhouette, nine occupied positions, three silhouettes and four equipment action buttons. |

The optimized embedded client builds. UITree passes; host/frame/mobile suites
pass 277/164/144 checks. The native runs use the prepared fixtures from M0,
readiness-based input timing and ordinary freshness preflights, with no stale
override. Native capture commands and final saves are retained in each run.

Remaining M2 obligations include classified deep copy and subtree reuse,
complete action/hook revisions and queued callback validation, async resource
completion, publication-boundary enforcement, remaining direct runtime writers,
and invalidation coverage for every property family. Focus visibility/lifecycle
and the other retained interactions need the complete presentation authority
from M3/M4. These runs do not establish those unimplemented guarantees.

## Native copy checkpoint

`CC_COPY` now preserves native behavior/hiding, script-visible colors and text,
parameters, scroll state, count display mode, drag constraints and hotkeys.
Owned strings, behavior scripts, parameters, inventory slot data and builtin
payloads are copied independently. Topology, identity, presentation claims and
active interactions remain those of the new node.

Nine native-copy assertions were observed failing before this fix
(`/private/tmp/plugin-engine-m2-copy-red.log`). Ordinary UITree tests now pass,
and `m2-copy-negative` proves removing parameter copying fails the semantic
assertions. The source-deletion cases cover independent ownership and CS1
live-set membership.

`make -C src OPT=0 ENABLE_ASAN=1 test-uitree` passes with actual address
instrumentation. The UITree make target now uses the existing macOS ASan shim,
matching other native test targets. Without it the sampled process recurred
through `AsanInitFromRtl`, dyld shared-cache iteration and `_Block_copy` before
main. The earlier command using `ASAN=1` was only an ordinary debug build and
is excluded as sanitizer evidence.

`m2-osrs-bank-copy-trace` passes the prepared native capture gate. With
`TORIRS_CS2_TRACE=1 TORIRS_CS2_TRACE_SCRIPT=505` and `500,bank`, the trace records
45 actual `CC_COPY(105)` operations in native script 505. The bank capture was
inspected at 2×: 48 visible item cells, three visible tab-strip boxes, twelve
bottom controls and the Close control. rs289lc has no CS2 `CC_COPY` producer;
its previous actual packet/CS1 runs cover the shared identity/hiding changes.
The complete mutation audit, allocation-failure policy and queued-callback
validation are still pending; native-copy success does not close those gates.

## Queued native callback checkpoint

The production CS2 task now retains the active/dot node identities while an
event waits to start, including a wait for its script. Unbound server scripts
still make explicit current-component queries. Once a script starts it may
legitimately delete its origin; a widget operation waiting on assets instead
retains the identity of its own already-resolved target. If that target becomes
stale, the resource-load child drains and is released without replaying the
callback's widget operation. Delayed-IO interruption coverage remains open.

Sub-change and misc/friend/chat snapshot dispatches retain node references.
Inventory, variable and stat registries retain references too; a recycled ID
cannot inherit an old listener or its already-seen/pending-unhide state.
Cache registration receives the actual tree being built. Native `CC_COPY`
copies the host-owned transmit registrations as well as tree-owned hooks,
with fresh initial-update state and the new node's identity.

The existing `test-cs2-transmit-pump` target now executes real CS2 tasks against
the native host. It observed a queued callback changing a replacement's color
before the fix. It covers stale queued events, both snapshot dispatch paths,
all three stale registries, initial delivery after re-registration and three
copied transmit listeners. All three copied-listener checks were observed red
before their implementation. These are small bytecode conformance fixtures;
the native revision runs below are separate evidence, not substitutes for the
remaining adversarial callback scenarios.

`m2-callback-negative-final` records expected assertion failures after disabling
the queued-origin, snapshot and registry identity checks. The helper reuses
the existing CS2 test target and links temporary mutated objects; source files
and native client objects are not modified. Reproduce with:

```sh
python3 tools/plugin_engine_negative_controls.py --suite cs2 \
  --make-arg OPT=1 --make-arg EMBED_SERVER=1 \
  --make-arg PLATFORM_OBJ_BASE=build_plugin_engine_perf NEW_DIRECTORY
```

`m2-callback-osrs` passes all four native-root captures; `m2-callback-lc` passes
both actual packet/CS1 scenarios using two distinct accounts. The inspected
2× contact sheet preserves 8/8/8/7 OSRS chat controls, 19 Lost City skill cells,
strength 20/20 and the three guide controls. `m2-callback-bank` passes after the
listener-copy change and records 45 native script-505 `CC_COPY` operations;
its enlarged capture retains 48 visible item cells and twelve bottom controls.
These baseline runs reported zero callback cancellations.

Remaining retained-state work includes queued resize/trigger operations,
cross-tree checks in older index/incarnation caches, registration changes and
registry compaction during dispatch, VM active/dot references across unrelated
asset yields, and task/resource teardown. The complete mutation/publication
audit and major-3 plugin lifecycle are still required before M2–M6 can close.

## Retained input and dispatch checkpoint

Sources: [tree identity](../../src/ui/uitree.c),
[native queues and keyboard routing](../../src/game/rs_cs2_host.c),
[cooperative dispatch](../../src/game/task_cs2_run.c), and
[executable conformance cases](../../src/game/test/rs_cs2_transmit_pump_test.c).

Component incarnations are now process-unique, closing cross-tree aliasing in
older native caches that retain only an index and incarnation. Resize and
operation queues retain checked references; local child operations retain the
child, not merely its still-live parent. Four assertions were observed red
before these changes (`m2-cross-tree-red` and `m2-queued-op-red` logs in `/private/tmp`).

The CS2 continuation keeps independent active/dot references across asset
loads. Reusing one context invalidates that context without cancelling work on
the other live context. The test drives the real dat2 script-load task and
cache encoder/decoder, with both valid and stale continuations. A stale active
context was observed writing its replacement before the fix.

All three transmit dispatchers now snapshot participant identities, using their
old registry positions as fast hints. Compaction during a yielded callback
cannot skip an original listener or introduce a newly registered participant
into the current pass. The new listener receives its initial update on the
next pass. Six assertions failed before this implementation, including the
already count-bounded stat dispatcher: a count alone did not protect identity.
The native tests exercise registry compaction while a real script load waits.

`m2-global-negative` and `m2-dispatch-negative` record the expected failures
when process identity, resumed context validation, participant relocation,
queued-origin, snapshot and registry checks are individually broken. UITree
and the expanded CS2 native suite pass under ASan. The IO fixture now explicitly
identifies OSRS239; its earlier missing-profile assertion is not counted as
sanitizer acceptance. The fixture also calls the production host destructor.

`m2-dispatch-lc` passes both packet/CS1 cases with distinct accounts.
`m2-dispatch-native-roots` passes the four native roots. The first optimized
OSRS scenario run failed eight groups because required opt-in trace lines
were compiled out, while its rendering assertions passed. Those diagnostics
now use gated reports. `m2-dispatch-osrs-instrumented` passes all 13 existing
native captures, including packet receipt, walk permission, hiding, resize and
remount assertions. Enlarged map captures show the four map-visible modes and
three compass-visible modes required by the OSRS state table.

### Actual focused-field/server-hide case

Logical focus is distinct from permission to receive keys. `RS_CS2_InputKey`
now receives the app's UI host and checks the same native availability plus
presentation suppression before editing. Native hiding, unmounting and input
suppression pause typing without moving logical focus to another field.
All three restrictions were observed accepting keys before the fix.

The existing gameframe harness now includes `focus-native-hide`: open the native
Hiscores pane (894), focus its type-12 search field, type `ab`, hide its parent
with a server packet, type `x`, unhide it, then type `c`. The checker requires
the ordered input/packet trace and a focused-field text fingerprint for `abc`.
`NATIVE_INPUT` records length/hash rather than the field's raw text.

`m2-native-focused-hide` passes. At 2× its four top controls remain intact and
the search field shows `abc`. In `m2-native-focused-hide-negative`, a temporary
native build with only the keyboard eligibility guard removed shows `abxc` and
fails the focused-input assertion, while the ordered packet/input sequence and
other capture assertions pass. The source mutation, binary and build log are
in `m2-native-focus-negative-build`; `m2-focus-positive-negative-2x.png` records
the inspected comparison. This is an actual native-cache UI and server-packet
negative control, in addition to the small bytecode conformance cases.

The default OSRS scenario set is now 11 groups / 14 captures. These results
close the listed retained-input cases, not the entire M2 gate. Outstanding work
includes complete mutation/dependency auditing and publication checks, retained
operation semantics when labels/masks/content change, async teardown and
allocation-failure behavior, and the remaining animation/resource writers.
M3–M6 and the production major-3 C/Lua cutover are still required.

## Animation and resource checkpoint

[UI model animation](../../src/engine/uitree_anim.c) previously posed the shared
scene asset in place. Two widgets using that asset therefore both rendered the
last widget's pose. Four observed failures covered asset mutation, shared
instances, wrong per-widget frames and unconditional sequence restatement dirties.

The native clock now uses typed animation setters. Rendering resolves a private
pose in a component-owned derived cache. The cache is keyed by the model's
process-unique registration revision, sequence and frame; it reuses unchanged
poses, refreshes after resource replacement, and releases its model on animation
removal or widget destruction. Native model IDs, angles, requested geometry and
animation state remain separate from the derived model. Copies do not inherit
another node's rendering cache. The [render translator](../../src/render/torirs_frame.c)
uses these instances, while [scene registration](../../3rd/toridraw/toridraw_scene.c)
provides model-specific revisions.

Both cache formats' active animation IDs were previously dropped between decode
and UI construction. The dat1 and dat2 conversion/build paths now preserve them;
CS1 active state selects the corresponding sequence. Pack preparation also
loads active model assets. UI skeletal animations now advance and use the
existing native skinning implementation rather than being treated as missing
classic frames. All four active-variant/skeletal checks were observed failing
before the fixes.

Preview binding, player-entity mirroring, client-code button/graphic/colour
writers and the CS2 animation setter use the shared typed mutations. A separate
native-host regression caught the generic CS2 wrapper adding a dirty mark after
a no-op animation setter; that wrapper now leaves invalidation to the typed API.

The existing CS2/native test target covers rendered model instances, classic and
skeletal poses, both cache conversion paths, resource replacement, cache release,
and repeat-set behavior. ASan passes. `m2-animation-negative-fixed` contains four
observed failing controls: bypassing private poses, ignoring model registration
changes, skipping skeletal clocks, and dropping the active sequence at emission.
They reuse the existing test target through `plugin_engine_negative_controls.py`.

Actual native evidence:

- `m2-animation-lc-design` and `m2-animation-lc-mutated`: new-account Lost City
  character creation, real model node 3650, followed by gender and torso-colour
  clicks. Both captures pass. At 2× there are 24 arrow controls, Male/Female and
  Accept; the preview changes to the selected female design and new colour.
- `m2-animation-osrs-preview` and `m2-animation-osrs-mutated`: native `equipstats`
  opens interface 84, model node 84:4. A real item/equip operation replaces worn
  legs with rune platelegs. Both captures pass; the avatar, item slot and bonus
  values change. At 2× the 12 equipment positions, Set Bonus and Close remain.

The harness now reserves every Lost City username in the isolated fixture's
account ledger, checks existing saves, and records `player.json` for unseeded
runs too. Seeded saves still use exclusive creation. A capture now requires
`GF_MATRIX_LC_SERVER` so account uniqueness has durable fixture provenance.

M2 is still open: the complete mutation/dependency audit, publication checks,
retained operation semantics for changed labels/masks/content, and general
async teardown/allocation-failure behavior remain. This checkpoint does not
claim completion of M3–M6 or the major-3 production plugin cutover.

A further animation case starts with an asset that has no saved bind vertices.
The private instance captures them before its first pose, preventing frame-to-frame
accumulation. This case was observed failing before the fix
(`/private/tmp/plugin-engine-m2-animation-bind-red.log`). The native account
ledger was exercised by `m2-animation-lc-ledger`, which passed the fresh-account
character-design launch and recorded its exclusive reservation in `player.json`.
