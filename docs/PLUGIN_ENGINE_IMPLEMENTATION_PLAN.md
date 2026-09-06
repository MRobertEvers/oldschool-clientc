# Plugin engine implementation plan: RuneLite model across rs289lc and OSRS239

Status: revised by explicit user decision to adopt RuneLite-style live-widget APIs, events, listeners and explicit revalidation. This replaces the proposed property-claim/bundle API. It does not mark the new runtime complete.

## Goal and scope

Build one practical C/Lua plugin system using RuneLite's plugin lifecycle and execution model. Plugins read and modify live widgets, subscribe to native events, add controls/listeners, invoke native operations, and request native layout revalidation. Keep the engine's native identity, lifetime and visibility protections inside the host.

- **rs289lc:** actual revconfig frame layout, mounted cache interfaces, CS1 evaluation, engine client-code behavior, and server packets.
- **OSRS239:** actual CS2-driven widget construction/layout/listeners, native engine behavior, and server packets. Cover roots 548, 161, 164 and 601.
- Preserve repositioning, re-skinning and extensions of operational native UI. Native content, actions and animation remain live.
- Introduce a breaking API major version for C and Lua together. Port every product plugin, hidden feature, shipped script, probe/demo, manifest, metadata file, example and test. Remove superseded executable APIs and loaders; no legacy-plugin compatibility layer.
- Preserve useful settings and tracker data through explicit data migrations. Do not execute old plugin code to retain its data.
- Native hiding, including RS2 server hiding, remains authoritative. Plugin presentation cannot reveal or activate natively hidden/closed content.

An unchanged plugin is portable when its required APIs/capabilities exist on both revisions. Revision-specific plugin features are allowed, explicitly declared and gated. Supporting a new generation requires an adapter and native conformance evidence; the API does not automatically make every revision compatible or guarantee bug-free plugins.

## Keep the foundation; remove the abandoned abstraction

PR #82's layout, visibility, semantic binding, depth/input ordering and replacement eligibility remain a tested foundation. The existing 40-case matrix and 13 native scenarios do not establish comprehensive mutation correctness.

Retain subsequent native fixes for identity, zero-size geometry, copying, native/mount hiding, queued callbacks, input, item state, animation and mutation auditing. M0 already has inventories and reproducible native fixtures. Some mutation work remains incomplete; the production host still uses V2.

Remove the unshipped major-3 property claims, claim bundles, declaration transactions and whole-bundle conflict-suspension policy. Do not preserve them under convenience wrappers or as a second public API. Replace their declarations/tests with the lifecycle/widget/event contract below. Atomic enable/reload and safe cleanup remain host requirements; they do not require a transaction object around every widget change.

The user-approved change also replaces the blanket prohibition on public revision-specific widget/script IDs. Ordinary portable plugins use shared frame/widget helpers. Deliberately revision-specific plugins may use documented widget/script lookup and script APIs when their declared revision capability is available. Raw C tree pointers and unchecked retained indices remain private.

## Public model

### Lifecycle and client thread

A plugin has metadata, configuration and `startUp`/`shutDown`-equivalent callbacks. It subscribes to events while enabled and can schedule work on the client thread. Provide immediate-if-safe, later, and end-of-tick scheduling with explicit ordering and owner lifetime.

Native widget mutation, listener dispatch and script execution happen on the client thread. Script execution is non-reentrant. A callback that cannot safely execute a native script schedules it for the next documented safe point. Paint callbacks draw from settled state; they do not repair widget geometry during rendering.

The host owns registrations, created widgets, resources and queued work. Disable/failure revokes callbacks/actions before freeing their state. Reload validates the new plugin before activation and leaves a defined native fallback if activation fails. Widget references are checked against tree/instance/incarnation on every retained use. C callbacks remain trusted in-process code; do not claim that an arbitrary corrupt or hung C plugin can be safely recovered or preempted. Enforce Lua instruction limits and bounded queue/layout processing where the runtime permits them.

### Live widgets and native actions

Provide familiar widget operations in C and Lua:

- Get a widget or current children, read text/items/state/bounds, and inspect currently available actions/listeners.
- Set position/size inputs, text/style, sprites, scrolling and supported model presentation; call `revalidate()` or `revalidateScroll()` explicitly.
- Force a native widget's presentation position/size where supported, and clear that force to resume current native layout.
- Create owned children, assign operations and listeners, and remove owned children without deleting another plugin's or native children.
- Invoke a current native widget action or, on a supported revision, run a native script through the client API.

Use opaque checked widget/action references in C; Lua wraps the same references. A live child query can discover a replacement, but an old retained reference cannot silently turn into it. Expose numeric widget/script IDs for diagnostics and supported revision-specific code, with shared helpers for portable features such as chat filters, sidebar controls and map surfaces.

Native topology and addressing remain intact when moving/skinning widgets. Repositioning is not native reparenting. On native widgets, plugin hide is an additional restriction; clearing it does not clear native hide. Operations always pass current native visibility, masks and permission checks, even when invoked through an added control.

For native widget presentation, keep only the reset bookkeeping needed to distinguish current native values from plugin edits. Direct setter calls take effect in callback order; no claim acquisition, bundle resolver or conflict suspension. Multiple writers have a documented event priority and stable tie-break order, and the inspector identifies the last writer. Reset/removal reveals current native values or the remaining active override, never a startup snapshot. Implement and test this narrowly; do not rebuild a general declaration engine underneath the widget API.

Re-skin helpers can listen for native state changes and map current native style, including active/hover variants, opacity and semantic text colors. Ordinary repositioning uses forced geometry plus native revalidation. Plugin authors should not need arbitrary per-frame geometry repair loops. A plugin intentionally replacing text/content uses the corresponding setter and native events; merely changing a skin must not freeze native content.

### Events and native listeners

Use RuneLite-style event subscriptions and widget listener callbacks. Initial events cover widget load/close, native state changes, before/after layout, client tick, and user operations. OSRS additionally exposes script pre/post execution and named script callbacks. Event payloads identify the current widget and relevant native data; references and borrowed arguments remain valid only under their documented lifetimes.

Listeners execute in explicit priority order with a stable plugin-ID/registration tie-break. New subscriptions do not join an in-progress dispatch. Removed/disabled subscribers cannot receive later callbacks from that dispatch. Event mutation/consumption rules are per event type and tested. Do not introduce global event reducers or exclusive-claim arbitration.

A callback inside native execution can supply a result before that execution continues. Such callbacks are synchronous and cannot yield or recursively invoke the script VM. Native defaults apply when no plugin participates or the callback is unavailable. Expose arguments/results through scoped, type-checked access; do not expose dangling VM memory. A next-frame notification is not equivalent to an in-script decision.

The revision adapter owns the native hook wiring. Add generic engine hooks where they express the actual native operation; use identified script boundaries or authored script callbacks for behavior internal to CS2. A modified cached script must match its pinned source/hash and argument contract. Reject an incompatible integration instead of patching an unknown script. Do not require every ordinary plugin to ship modified native scripts.

### How the older generation fits

| Plugin need | rs289lc integration | OSRS239 integration |
|---|---|---|
| Find frame/widgets | Revconfig/builtin mappings and mounted interfaces | Semantic helpers plus live cache/CS2 widgets |
| Observe content/state | Server mutations, completed CS1 results, `RS_ClientCode_Tick` | Server/engine mutations and CS2 state/rebuild events |
| Move/resize/revalidate | Native revconfig/IF1 layout with supported forced allocations | Native layout and CS2 callbacks with supported forced allocations |
| Add controls/listeners | Owned widgets routed through common input and listener dispatch | The same owned widgets/input, alongside native CS2 listeners |
| Invoke native actions | Native button/client-code behavior and revision packets | Current widget operations, CS2 listeners and revision packets |
| Script-specific integration | Explicitly unavailable when it requires CS2 | Script pre/post/named callbacks and guarded native script execution |

CS1 is an expression evaluator, not a CS2-style interface scripting system. Hook the engine that consumes its active/value results, preserves `%N` text substitution and runs native actions. Do not synthesize fake CS2 scripts/events for the old client. Shared widget/lifecycle/state APIs make ordinary plugins portable; absent native features stay absent.

## Native correctness requirements

| Family | Required invariant and verification |
|---|---|
| Identity/topology | Create/delete/reuse/reparent/mount/close/reorder have classified effects. Stale menus, callbacks and widget handles cannot reach recycled nodes. Native addresses/parentage survive presentation edits. |
| Visibility/availability | Native hide, mount, selected-panel and revision restrictions keep their original self/subtree/controller scope. Housing and hidden map contents are distinct. Both paint and actions enforce native availability. |
| Geometry | Distinguish native requested inputs, forced presentation allocation and computed bounds. Explicit revalidation updates native layout synchronously; native computed getters observe coherent values within a script, including zero. Unclaimed descendants retain native layout. Unsupported reflow reports a useful error. |
| Scrolling/clipping | Preserve native content extents, offsets, clamping, masks and input clips. Escaping an anchor clip requires a supported popup policy. Moving a widget does not remove native scroll/mask restrictions. |
| Content/state | Text, inventory, counters, selected state, CS1 values, parameters and native operation labels remain live. Native writers and plugin setters invalidate all affected readers. |
| Appearance/animation | Sprite, color, opacity, font, model pose, sequence and state variants are separate effects. Native self-opacity remains self-opacity. Animation continues independently for each widget. |
| Input/retained work | Revalidate press/release, focus, drag sources/targets, retained menus/tooltips and queued callbacks on identity, action or availability changes. Focus transfer is explicit. Native listeners are not copied into stale action snapshots. |
| Multiple plugins | Defined callback ordering, scoped ownership of added widgets/listeners and observable last-writer behavior. Disable/reset cannot remove another owner's UI or restore obsolete native state. |
| Lifecycle/resources | Safe startup/failure/disable/reload/frame-switch/resource replacement, queue cancellation and allocation-failure behavior. Cleanup exposes current native behavior and preserves useful data. |

For every mutation, retain the inventory of producers, scope, readers, invalidation dependencies, allowed execution context and tests. Include server packets, builtin writers and asynchronous mounts. Constructors/deserialization have explicit initialization boundaries; development audits detect unclassified runtime writes after publication.

Document the actual native scheduling sequence. Native mutations and synchronous hooks run at their proper boundaries; revalidation settles required geometry/resize callbacks; completed native events run at defined safe points; paint/input consume one coherent publication. Bound repeated layout changes and report their origins. Do not fix native correctness by delaying all native reads until the next frame or rebuilding the whole tree every tick.

## Milestones and acceptance gates

### M0 — Reconcile inventory and native execution evidence

Reuse the existing mutation/consumer registers and prepared fixtures. Extend them with actual event/listener/layout/script hook points for both generations. Every native mutation family and every repository API consumer needs a disposition. Unknown prerequisites/semantics are explicit blockers, not skipped tests.

Record client/cache/content/server versions and hashes, build flavor, preferences, inputs and native expectations. Exercise actual rs289lc packet/CS1 paths and OSRS239 CS2 paths. Reserve a different Lost City account for every connection, including retries. Re-run preparation/baselines when relevant inputs change; do not restart successful preparation without cause.

**Gate:** complete migration checklist, measured native surfaces/capabilities and trustworthy launch/content receipts for both revisions. Preserve the existing baseline/performance evidence and its limitations.

### M1 — Replace the unshipped contract with the RuneLite model

Remove abandoned public claims/bundles/transactions and their obsolete policy tests. Update `docs/plugin-engine/CONTRACT.md` and major-3 declarations for lifecycle, client-thread scheduling, checked widgets, getters/setters/revalidation, events/listeners, resources and actions. Define native-versus-plugin reset behavior and callback ordering precisely. C and Lua expose the same authority and lifetime rules.

Keep this bounded by runnable authoring examples. Do not design a universal event schema/reducer framework before implementing a plugin. Add conformance cases for stale references, native hide, ordering, non-reentrant scripts, explicit revalidation and owner cleanup.

**Gate:** one small C frame extension and one small Lua widget extension can be expressed clearly with the proposed public API. No private host access, claim wrappers or plugin-specific native repair framework is required. Unsupported revision-specific operations return explicit unavailable results.

### M2 — Run the first plugins on both native generations

Pull enough host, adapter and Lua binding work forward to run the actual M1 example sources through production code. Use the existing tree, renderer, input path and gameframe harness.

The C example moves and re-skins an existing native surface while preserving its operational children. The Lua example creates an owned control bound to changing native state and invokes an available native operation. Run the same plugin sources against both revisions using shared helpers. Separately demonstrate that CS2-specific APIs are available on OSRS239 and explicitly unavailable on rs289lc.

Exercise native rebuild/remount, supported resize/revalidation, server hide/show, changing CS1/CS2 state, native actions and plugin disable/re-enable. Show one native synchronous hook influencing its intended operation before execution continues. Do not replace these with synthetic trees alone.

**Gate:** both real revisions pass instrumented runs and inspected 2x screenshots with visible control counts. Breaking required callback timing produces an observed regression failure. Native rebuilds do not duplicate controls or retain stale callbacks. Inspect actual C/Lua example source for simplicity before expanding the API.

### M3 — Finish native hardening and adapters

Complete the mutation and invalidation audits using the production plugin path established in M2. Expand the geometry-domain audit to other native property families. Finish retained interactions, asynchronous mount/teardown, resource replacement and allocation-failure coverage. Preserve existing native correctness fixes and extend their tests.

Complete shared frame/widget/action helpers and revision-specific script integrations. Cover OSRS roots 548/161/164/601 and rs289lc's actual layout restrictions. Native geometry read-after-write, scroll/clipping, opacity, animation, live text/items, operation masks/listeners and RS2 server hiding remain correct under live widget edits and reset.

Audit protocol differences directly, particularly minimap states. Do not apply OSRS state meanings to rs289lc without native evidence. Script/content integrations require matching preparation and compatibility checks. Test compatible helper/profile mappings with unchanged plugin source and useful failure on incompatible mappings.

**Gate:** audited mutation sequences have consistent native state, paint and input on both generations. No stale retained object acts on a replacement; reset exposes current native state. Unsupported native behavior is reported, not fabricated.

### M4 — Complete authoring helpers, composition and chrome

Finish familiar helpers for panels, controls, live state, overlays, menus and plugin settings. Use one panel/action model across supported chrome executors. Document logical/window/drawable coordinates, scaling, clipping, focus and drag behavior. Native widgets, owned widgets and overlays share visibility/depth/input rules.

Port representative frame, orb, panel and Lua products. Keep native operational subtrees intact when replacing decoration. Fix the mobile parchment/text contrast through current native styles without erasing semantic message colors or freezing text updates.

Provide a developer inspector for widget identity, native/effective values, source/last writer, event/listener registrations, layout invalidation and unsupported operations. User-facing errors explain the consequence. Plugin enable/disable/reload and multi-plugin callback order must be visible and testable.

**Gate:** representative ports work on both applicable adapters through ordinary public APIs. No universal claim/transaction layer reappears. Multiple writers, native refresh, owner cleanup and frame selection behave as documented. Metadata/examples match the running C/Lua APIs.

### M5 — Port every consumer and remove old execution APIs

Complete every row in the inventory below, including configuration, data/events, rendering, actions, resource lifetime and hidden features. Record capabilities, revision-specific usage, old APIs removed, persistence migration, both-revision results, lifecycle/interaction cases and negative-control evidence.

**Gate:** all C products, Lua runtime/products/probes/demos, manifests, metadata, examples and tests use major 3. No obsolete loader, execution callback builder, compatibility adapter or executable old ABI remains. Repository-wide discovery and all affected build configurations agree. A compiling port that loses supported behavior does not count.

### M6 — Prove and release the complete implementation

Extend `tools/gameframe_matrix.sh` and `tools/gameframe_pixels.py`; do not create a competing harness. Cover targeted mutations, cross-feature combinations and deterministic generated sequences with shrinking/replay. Compare retained output with forced fresh resolution, plus independent native state/actions and pixel assertions. Agreement between two paths sharing a bug is insufficient.

Run no plugin, one frame, attached overlays, multiple writers, enable/disable/reload and unsupported capabilities. Mutate while menus are open, buttons pressed, dragging, text focused and mounts pending. Break the relevant mechanism and observe the intended test failure for each family, including callback timing/order, stale references, visibility and script/profile mismatch. Run software rendering and every affected supported frontend/executor.

**Gate:** both real revisions pass; enlarged screenshots are inspected at 2x and visible controls counted; every family has meaningful observed negative controls. Exact final commits build in clean checkouts before pushing. Native launch and required content preparation are reproducible with matching freshness receipts and no unexplained stale-pack bypass. Required content-bake failures are repaired or remain release blockers.

Compare quiet-frame, mutation-heavy and multi-plugin costs on both pinned fixtures against M0's recorded budgets. Avoid unnecessary whole-tree rebuilding, per-tick widget repair and callback churn. Preserve useful user data and verify migrations with representative existing data.

## Initial plugin migration inventory

The inspected registry contains 13 C product plugins plus the Lua runtime. The default script manifest contains six Lua product plugins. This is the starting inventory; M0 closes it against the whole repository.

| Batch | Consumers | Primary contract exercised |
|---|---|---|
| Frame integration | `gameframe-layout`, `mobile-gameframe` | Surface allocation, decoration, native controls, state-aware theme, fallback, chrome |
| Attached and world overlays | `minimap-orbs`, `xp-drop-orbs`, `tile-indicator-c` | Native availability/actions, anchored depth, projection, transient events, resource lifetime |
| Panels and data views | `item-stats`, `xp-tracker`, `loot-tracker` | Live native data, selections, menus, scrolling, focus, persistence, panel executors |
| Essential/hidden native features | `client-settings`, `feature-flags`, `nxt-highlight`, `nxt-bird-nest`, `nxt-cannon-ammo` | Native setting/action adapters, capabilities, events, notifications, lifecycle |
| Lua runtime and language surface | `torirs_plugin_lua.c`, `plugin_api.meta.lua`, API inventory/compile tests | Same authority, phases, reference validation, errors, and capabilities as C |
| Lua product plugins | `tile-indicator-lua`, `entity-highlighter`, `loot-beam`, `ground-items`, `screenshot`, `performance-display` | World/entity/data access, input, overlays, resources, host services |
| Lua probes and demos | `_paneldemo`, `_hullprobe`, `_roleprobe`, `_probe`, `_gicount`, `_giprobe`, `_beamprobe`, `_drawprobe`, `_windemo`, `_hoverprobe`; their manifests | Authoring examples and test tools must compile/run against the same new API |

The small C/Lua ports in M2 validate the architecture early. Full representative ports in M4 validate the completed API before bulk conversion. Every row is completed in M5, including hidden plugins and scripts normally disabled by default.

## Execution policy and next step

Sequence: M0 reconciliation → M1 RuneLite-style contract → M2 real C/Lua examples on both revisions → M3 remaining hardening/adapters → M4 authoring/products → M5 all-consumer cutover → M6 release. These revised milestone definitions supersede the earlier numbering; historical M2 progress records describe native fixes, not completion of the new M2 production-plugin gate.

Inspect git state first. Use the isolated migration worktree, preserve unrelated dirty work and never stash or sweep files. Preserve the original repository's useful data and unrelated changes. Integration includes the host, C plugins, Lua/scripts, metadata, manifests and tests together; temporary migration commits may be incomplete, but no dual-ABI layer ships.

Next: preserve the bounded retained-menu regression work; remove the abandoned claim API and reconcile the contract/tests; implement the first production widget/event path and C/Lua examples. Keep engineering passes bounded by a reproduced behavior, implementation, observed negative control and relevant native verification. Do not substitute more general design documents for working plugins.

## Completion checklist

- [ ] Familiar lifecycle, client-thread scheduling, widgets, listeners and revalidation work through the production major-3 host in C and Lua.
- [ ] The same small C/Lua examples run on both real revisions; revision-specific APIs report unsupported features explicitly.
- [ ] Every mutation family has executable ownership/scope/invalidation cases, including identity/topology, scrolling/clipping, opacity, animation, content, operations and retained interactions.
- [ ] Both adapters preserve their own script, engine, packet, layout and native hiding semantics.
- [ ] Multiple plugins, queued work, focus, dragging, retained menus and lifecycle cleanup behave as documented.
- [ ] Every listed/discovered consumer is ported; abandoned claims and old execution APIs are removed; useful data is preserved.
- [ ] Authoring helpers, settings/chrome, inspector, metadata and examples work on supported frontends.
- [ ] Negative controls, independent native assertions and inspected pixels support both revisions.
- [ ] Exact-commit clean builds, reproducible content/native launch and agreed performance budgets pass.

## Inspected sources

Native integration entry points: `src/app.c`, `src/ui/uitree.*`, `uitree_layout.c`, `uitree_frame.c`, `uitree_host.c`, `src/game/task_cs1_run.c`, `rs_cs1_host.c`, `rs_clientcode.c`, `task_cs2_run.c`, `rs_cs2_host.h`, `rs_gameproto_exec.c`, `src/revconfig/`, `src/plugin/torirs_plugin_bridge.u.c` and the existing UI/plugin/CS1/native CS2 tests.

RuneLite source review was pinned to `ac79ed8bd8926bec7bf172aa291574b4d944b0e7`; RuneLite was not launched for that review. Relevant sources: [BankPlugin](https://github.com/runelite/runelite/blob/ac79ed8bd8926bec7bf172aa291574b4d944b0e7/runelite-client/src/main/java/net/runelite/client/plugins/bank/BankPlugin.java), [Widget](https://github.com/runelite/runelite/blob/ac79ed8bd8926bec7bf172aa291574b4d944b0e7/runelite-api/src/main/java/net/runelite/api/widgets/Widget.java), [ClientThread](https://github.com/runelite/runelite/blob/ac79ed8bd8926bec7bf172aa291574b4d944b0e7/runelite-client/src/main/java/net/runelite/client/callback/ClientThread.java), [WidgetOverlay](https://github.com/runelite/runelite/blob/ac79ed8bd8926bec7bf172aa291574b4d944b0e7/runelite-client/src/main/java/net/runelite/client/ui/overlay/WidgetOverlay.java), [ChatboxTextMenuInput](https://github.com/runelite/runelite/blob/ac79ed8bd8926bec7bf172aa291574b4d944b0e7/runelite-client/src/main/java/net/runelite/client/game/chatbox/ChatboxTextMenuInput.java) and [client-script guidance](https://github.com/runelite/runelite/wiki/Working-with-client-scripts). Use these mechanisms as implementation references, not as proof of our native correctness or cross-generation compatibility.
