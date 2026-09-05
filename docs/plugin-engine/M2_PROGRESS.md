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
