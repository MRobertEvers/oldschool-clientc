# M2 implementation evidence

Milestone numbering below records work under the original plan. The user has
since selected a RuneLite-style widget/event API; see the revised implementation
plan and `CONTRACT.md`. Native correctness evidence remains applicable. Earlier
claim/bundle policy results are historical and do not validate the new API.

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

## Geometry audit and canonical content checkpoint

`TORIRS_UI_MUTATION_AUDIT=1` enables native geometry auditing. `UITree_Push` and
`UITree_CcCopy` establish the constructor boundary. Typed geometry writes update
recorded authored values, including writes underneath a frame allocation and
scroll-offset canonicalization. A typed write checks the prior state, and emit
publication rejects an unclassified change with component, field, old/new values
and the observation point. Computed layout coordinates and presentation fields
are excluded. Records follow incarnations through slot reuse and exist only when
auditing is enabled. The focused tests detect raw position and scroll writes;
`m2-geometry-audit-negative` confirms disabling the comparison fails the assertion.
This is a geometry-domain audit, not yet a complete native-property audit.

The CS2 scalar-property dispatcher, direct drag/graphic/fill variants, input
caret/wrapping updates and inventory-source binding now use typed tree operations.
Native no-ops no longer acquire an extra wrapper paint invalidation. The runtime
hook setter validates that its slot belongs to the named live component before
reading or changing it, and real binding changes invalidate reachability. Two
ownership assertions were observed failing before that check.

Dynamic object widgets previously stored item data twice: readback and optimistic
swapping used common `item_*` fields, while drawing used `u.cc_obj`. The duplicate
runtime state is removed; specs initialize the canonical fields, and drawing,
setobject, server inventory clearing and item movement use them. Item swaps keep
native hiding and count-display policy with the cell. The corrected regression
filters actual item descriptors rather than their count-text companions. The
same test was run against commit `5fe670bb8` in
`/private/tmp/plugin-engine-object-negative` and failed both rendered-state and
hide-ownership assertions (`plugin-engine-m2-object-swap-old-verified.log`).

`m2-canonical-osrs` passes all 14 native captures with auditing enabled;
`m2-canonical-lc` passes both actual packet/CS1 cases with auditing enabled and
fresh reserved accounts. `m2-canonical-drag` drives a real inventory drag; the
isolated server save records slots 0/1 changing to platebody/body-rune, matching
three visible item icons in the inspected 2× capture. Native menu tests and UI/
CS2 ASan suites pass. The focused audit run before the canonical-state changes
also passed both revisions (`m2-geometry-audit-osrs`, `m2-geometry-audit-lc`).

Remaining M2 work includes auditing other native property domains, operation
freshness when labels/masks/content change, and general async teardown and
allocation-failure behavior. The production major-3 plugin cutover and M3–M6
remain open.

### RuneLite CS2 source comparison

Inspected upstream RuneLite commit `ac79ed8bd8926bec7bf172aa291574b4d944b0e7`
in `/private/tmp/plugin-engine-runelite-reference`. This is source evidence;
RuneLite was not launched for this comparison.

- `BankPlugin.onScriptPreFired/onScriptPostFired` surround specific native
  rebuilds; the price calculation explicitly precedes Bank Tags by subscriber
  priority. `BankSearchFilter.rs2asm` inserts a synchronous named callback with
  a return slot and native fallback. An after-frame notification cannot provide
  the same in-script behavior.
- `LayoutResizableStones.rs2asm` asks `forceStackStones` before choosing native
  layout. `InterfaceStylesPlugin` answers through the VM stack, and separately
  reapplies selected computed widget dimensions on `PostClientTick`.
- `Widget` separates original layout inputs from computed coordinates, exposes
  `revalidate` and `revalidateScroll`, and documents forced positioning across
  revalidation. `WidgetOverlay` clears that force and revalidates on reset.
- `TabInterface` creates real widget children and Java operation/scroll
  listeners, defers work until native resize has completed, and clears retained
  widgets on unload. `ChatboxTextMenuInput` provides a small fluent title/options
  builder over the lower-level widget/listener implementation.
- `Client.runScript` requires the client thread and is non-reentrant. The
  official client-script guide requires original-script hashes for cache script
  replacements. Current code uses an object stack where the guide still calls
  it a string stack; raw VM details are a revision dependency.

M3/M4 implications to verify in executable representative ports: expose useful
native rebuild/decision points with typed arguments and explicitly defined
timing; support native fallback and composition at those points; keep script IDs
and VM stack offsets inside revision adapters; make layout revalidation and
attachment cleanup host responsibilities; provide small C/Lua authoring helpers
over the same checked contract. The current major-3 header's `NATIVE_EVENT` phase
alone does not implement these facilities. A routine presentation declaration
must not require each plugin to repair native layout every tick.

Primary sources (paths relative to the pinned upstream commit):
`runelite-client/src/main/java/net/runelite/client/plugins/bank/BankPlugin.java`,
`plugins/banktags/tabs/TabInterface.java`,
`plugins/interfacestyles/InterfaceStylesPlugin.java`,
`ui/overlay/WidgetOverlay.java`, `game/chatbox/ChatboxTextMenuInput.java`;
`runelite-client/src/main/scripts/BankSearchFilter.rs2asm` and
`LayoutResizableStones.rs2asm`;
`runelite-api/src/main/java/net/runelite/api/widgets/Widget.java` and `Client.java`.
Guide: https://github.com/runelite/runelite/wiki/Working-with-client-scripts.

### Retained native operation checkpoint

Menu picks now capture a native action signature in addition to node incarnation.
Changes to labels/opbase, masks, action hooks, parameters, item state and native
text targets invalidate old picks; geometry, color, opacity and unrelated timer
hooks do not. App dispatch also captures/rechecks effective server IF_SETEVENTS,
which live outside the widget click mask. Plugin boundary incarnations now remain
64-bit in menu picks instead of passing through an integer field.

The existing UI suite passes. `m2-menu-policy-negative` observes the expected
assertion failures after separately removing action freshness and native text
from the signature. This checkpoint has not yet completed actual open-menu
packet scenarios, inventory-count freshness or the broader lifecycle gate.
Do not treat these focused tests as comprehensive operation correctness.

### First production major-3 widget path (revised M2, partial)

The public aggregate/registration names are now `ToriRS_Api`,
`ToriRS_PluginDef` and `PluginHost_Register` in `torirs_plugin_api.h`, reporting
major 3. Repository C consumers and the Lua runtime compile against this same
host; no second runtime or old-ABI registration wrapper was added. Former
frame/UI builders remain for the still-pending product ports. Their internal
V2-named implementation storage is not yet fully cleaned up.

`api.widgets` routes live role/native lookup, children, current text input,
drawn-canvas and parent-local geometry, position/size setters, revalidation and
owner reset. Lua uses checked widget userdata with equivalent methods and
metadata inventory checks. Native geometry remains authoritative under a small
per-widget owner edit list: writes use call order, reset reveals another owner's
remaining edit or latest native input. Native copies do not acquire these edits,
node deletion frees them, and plugin teardown drops its owner edits. There is no
claim/bundle arbitration. The old frame builder's active canvas allocations are
explicitly unsupported by the new native-parent setters until those providers
are ported; the new probes run against native frames.

Tree regression and ASan tests pass for current-native reset, independent
position/size ownership, zero-size layout, native mutation under a forced box,
copying and stale references. `live-widget-geometry-negative` observes the intended
failure with the override mechanism disabled. Host tests pass 284 checks including
callback-only routing and automatic teardown reset. Lua runtime/metadata tests
and frame/mobile suites pass. A previously omitted C frame-anchor operation was
also bound in Lua while those remaining frame consumers are being migrated.

Actual evidence in `/private/tmp/plugin-engine-evidence`:
- `live-widget-c-isolated-osrs` and `live-widget-lua-osrs`: exactly one logged
  12-pixel parent-local sidebar move; native inventory remains visible.
- `live-widget-c-stats-lc` and `live-widget-lua-stats-lc`: identical plugin sources
  move the revconfig sidebar, then a real tab click mounts the stats interface;
  real server updates drive CS1 to Strength 20/20. Each connection reserves a
  fresh account. Both show 19 skill cells, 13 sidebar tabs and 4 chat controls in
  inspected 2x captures. OSRS captures show 3 inventory icons, 14 sidebar tabs,
  8 chat filters and 4 native orbs.

The first C captures also loaded the default Lua manifest. They proved the C
call ran, but are not isolated references. The harness now suppresses that
manifest for C-only probes and requires explicit manifests for Lua probes.
Opt-in plugin logging survives optimized builds, and the existing pixel checker
requires exactly one successful before/after geometry trace. `TORIRS_SCRIPT_DIR`
selects isolated Lua source without changing the shared runtime assets; fixture
receipts hash that source directory and record the manifest.

This does not close revised M2: the examples currently retry initial readiness
from a tick callback. Native load/rebuild subscriptions, synchronous script hooks,
owned controls, re-skinning, live state binding and action invocation still need
their actual two-generation examples. No general lifecycle, conflict-order or
full plugin-port acceptance is claimed by these geometry probes.

### Binding subscriptions and corrected visible-sidebar verification

C and Lua probes now subscribe through `widgets.watch` at startup; neither polls
from a tick callback. The host observes native publication epochs after frame
binding, skips unchanged epochs, and emits only actual binding transitions.
Subscription serials fence dispatch snapshots across replacement, unsubscribe
and disable/re-enable. Callback order uses explicit priority, stable plugin ID
and registration serial. Shutdown releases host registrations and Lua closure
references. Nested plugin enable/disable previously clobbered the outer callback
context; two assertions were observed failing before saving/restoring it.

The new probe is now in the Lua smoke-test list. That exposed the old 16-script
ceiling; the Lua table now matches the host's 32-plugin bound, and its lookup table
is sized accordingly. All 17 shipped scripts/probes compile. Host and Lua ASan
runs required completing the widget tests' fake engine (release builds had hidden
its missing required callbacks); production constructor assertions remain intact.

**Correction to earlier OSRS movement evidence:** the initial `sidebar` lookup
used the old frame slot's arbitrary representative. This could be an unused
side-modal or a hidden tab. Immediate API geometry changed, but the visible
inventory did not. Enlarged comparison with the disabled-hook native reference
exposed this. Those earlier OSRS captures prove transport/readback only, not
visible repositioning, and the local evidence receipt now marks that limitation.

The new widget helper resolves the common native parent of numbered tab mounts.
It validates topology rather than selecting a member by ordering or hidden state.
The existing old frame representative API remains unchanged for its pending
ports. Its shared cache now also checks tree instance identity. A native-tree
regression and an observed `sidebar_group` negative control cover selection.

The gameframe checker independently requires the actual mounted inventory to
move relative to its untouched native side-modal sibling, and checks 46 blue
rune-sprite pixels from a native reference at the expected translated position.
Both new assertions fail on the formerly passing OSRS capture. They pass for C
and Lua in `widget-sidebar-{c,lua}-fixed` and `widget-sidebar-{c,lua}-remount`.
The remount uses the real `layout 2` server path, from root 548 to 164; each probe
unbinds/reset its old target and places the new target exactly once. C/Lua
`widget-watch-{c,lua}-lc` retain the real stats/CS1/server scenario and fresh
accounts, with 19 visible skill cells and Strength 20/20.

`widget-watch-negative-checked` observes failures when subscription-serial
validation or binding-change comparison is removed. Disabling the App's native
publication seam in a separately compiled native binary makes the real OSRS
remount scenario fail with zero callbacks, while launch, root selection and
ordinary native pixel checks still pass. `widget-sidebar-old-red.log` separately
records the incorrect-selection visual failure.

`widget-sidebar-{c,lua}-edge-drag` drives an actual inventory drag at shifted
control coordinates. The existing pixel checker places the rune in slot 1, and
both isolated server saves confirm slots 0/1 are now platebody/body-rune while
slot 2 remains platebody. These are native operations, not direct inventory edits.
Fixed captures show 3 inventory icons, 14 tab icons, 8 chat filters and 4 native
orbs. Root 164 has 13 tab icons plus the logout X; its special orb is partially
covered at this small native window size, also in the disabled-hook reference.
That existing native overlap is not accepted as comprehensive layout correctness.

A separate major-3 migration regression was caught in the minimap-orbs test:
minor-version-3 guards from API 2 disabled native actions under API 3.0. Those
obsolete guards are removed; the unchanged test was observed failing then passing.

Revised M2 still needs owned controls, styling, live state bindings and explicit
native action invocation in the public widget API. Script-internal hooks, full
composition/lifecycle coverage, all product ports and M3–M6 remain open.

The final focused host suite passes 311 checks, and host/Lua/UI suites also pass
with AddressSanitizer. The updated Lua smoke list includes all 17 scripts rather
than leaving the new probe out. Remaining native-overlap and full-port gates
above are unchanged.

### Owned text widgets and live data (revised M2, partial)

C/Lua now create keyed owned text widgets under a live parent, set their text/color,
and remove them. Native component IDs and dynamic sub-IDs are not assigned to
these widgets. The constructor sets ownership before native child indexing;
lookup and iteration exclude owned children. Native slot replacement preserves
attached owned children, while deleting the parent reclaims them. Cross-owner
writes/removal and moving native nodes into an owned subtree are rejected.
Owned geometry uses the ordinary typed setters; native geometry still uses the
separate resettable edits. The host rejects new owned widgets during shutdown.

The two probes add one Strength label to the native viewport, away from native
controls, and update it on the existing server-tick callback through the shared
skill API. Both actual revision paths render the label; rs289lc's real server
stat changes update it to 20 alongside native CS1 skill cells. OSRS's actual
`setlevel strength 20` path updates the label and isolated server save to 20.
The examples also survive the native 548-to-164 remount without duplicate labels.

The existing gameframe harness now drives plugin settings enable/disable through
`TORIRS_SIM_PLUGIN_TOGGLE`. C/Lua disabled captures have zero owned labels and
native sidebar placement. Re-enabling after a server-driven remount produces
exactly one label on the new viewport. Text hashes, native text descriptors,
painted ink and enlarged inspections support these assertions.

Evidence: `owned-widget-{c,lua}-{osrs,lc,disabled,reenabled,updated}` under the
local evidence directory, plus `owned-widget-label-inspection.png`. Inspected
label counts are one in each enabled capture and zero in both disabled captures.
The native inventory/skill controls remain present. Lost City captures used
separate reserved accounts. `owned-widget-negative` observes failures when native
child-key exclusion or preservation during slot replacement is broken. UI,
host and Lua tests also pass under AddressSanitizer.

This is a passive owned-control path, not completion of revised M2. Interactive
owned controls/native action invocation, native styling, script-internal hooks,
paint-time failure/teardown combinations, allocation-failure coverage and the
remaining product/front-end ports still require implementation and acceptance.
