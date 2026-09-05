# Plugin engine implementation plan: rs289lc and OSRS239

Status: proposed implementation sequence. This document creates a plan; it does not mark the implementation complete.

## Goal and scope

Build one plugin contract that integrates naturally with both supported UI generations. Plugins can arrange, decorate, and extend live UI without disabling the revision's native behavior or having to maintain numeric component tables. Native tree mutations must remain correct through rendering, interaction, caching, and plugin lifecycle changes.

- **rs289lc:** revconfig frame structure, cache interfaces, native engine behavior, CS1 evaluation, and server packets.
- **OSRS239:** cache interfaces, authored semantic metadata, CS2 layout/behavior, native engine behavior, and server packets. Cover toplevels 548, 161, 164, and 601.
- **All plugins move forward.** Introduce a breaking API major version, port every repository consumer, and remove superseded APIs. No compatibility wrappers, old plugin execution path, or requirements to preserve old ABI layouts.
- Preserve useful user configuration and tracker data through explicit data migrations where necessary. Data preservation does not require running old plugin code.
- Keep re-positioning and re-skinning. The recorded requirement is “re-skin AND re-position … cooperating with its scripts.”

A plugin may depend on a capability that a revision does not provide. Its port must state that requirement and produce an explicit unavailable result for that feature. It must not fabricate native controls or quietly lose previously supported functionality.

## Starting point and evidence boundary

Build on the existing tree, typed setters, role resolver, frame declarations, UI contributions, and regression tests. A new API version is an opportunity to simplify these mechanisms, not a requirement to replace the whole renderer.

Relevant implementation entry points, inspected while preparing this plan:

| Area | Starting files |
|---|---|
| Runtime mutation API | [uitree.h](../src/ui/uitree.h#L2024), [uitree.c](../src/ui/uitree.c) |
| Effective layout and replacement eligibility | [uitree_frame.c](../src/ui/uitree_frame.c#L1355), [uitree_layout.c](../src/ui/uitree_layout.c) |
| Native visibility and availability | [uitree_host.c](../src/ui/uitree_host.c) |
| Script and packet writers | `src/game/rs_cs2_host.h`, `src/cs2vm2/`, `src/cs1vm/`, `src/game/rs_gameproto_exec.c`, `src/app.c` |
| Role binding and app integration | [torirs_plugin_bridge.u.c](../src/plugin/torirs_plugin_bridge.u.c#L4612), `src/ui/uitree_role.*` |
| Plugin contract and runtime | `src/plugin/torirs_plugin_v2.h`, `torirs_plugin_host.c`, `torirs_plugin_runtime.inc`, `torirs_plugin_ui.*`, `torirs_plugin_frame.*` |
| Consumers and scripting | [torirs_plugin_registry.c](../src/plugin/torirs_plugin_registry.c#L46), `src/plugin/torirs_plugin_lua.c`, `script/plugins/` |
| Existing verification | `src/ui/test/`, `src/plugin/test/`, `src/cs1vm/test/`, `tools/gameframe_matrix.sh`, `tools/gameframe_pixels.py` |

PR #82's hide/state/depth/remount results are a baseline. They do not establish full mutation coverage. During implementation, every behavior claim must link its source and an instrumented run. Historical architecture notes are navigation aids; conflicting statements must be checked against current code and execution.

## Contract to establish

### Authority and scope

Native state and plugin presentation must be represented separately. A contribution must identify the exact properties it owns; acquiring one property must not acquire unrelated behavior.

| Family | Required decision and invariant |
|---|---|
| Identity/topology | Define create, delete, reparent, mount, close, reorder, and node reuse. References identify an incarnation. Presentation placement must not silently reparent the native tree or change IDs seen by scripts/server packets. |
| Suppression/availability | Native hide, unavailable content, selected-panel state, and revision-specific restrictions limit presentation and actions. Record whether each restriction affects self, descendants, or a controller. A hidden map's contents and its surrounding housing are different responsibilities. |
| Geometry | Separate authored/native values, plugin allocation, and resolved bounds. Define parent/local/canvas coordinates and every geometry getter's read-after-write semantics. Unclaimed descendants continue native layout inside the effective allocation. |
| Scrolling/clipping | Preserve content extent, scroll position, masks, and clipping at their native scope. Attached popups/tooltips may escape a clip only through an explicit policy; moving an element must not accidentally remove its clipping or input restrictions. |
| Content/state | Keep text, inventory contents, counters, selected state, CS1 active values, and other native content live. Skins must not substitute startup snapshots for live values. |
| Appearance/animation | Specify image, color, opacity, text style, model pose, animation, and state variants separately. A sprite substitution does not implicitly override all of them. Native opacity must retain its self/subtree semantics. Theme mappings may change presentation while preserving state transitions and message semantics. |
| Operations/hooks | Native action availability, click masks, operation labels, listeners, and hotkeys remain current. Behavior plugins invoke declared actions or explicit permitted mutations; frame decoration conveys no additional authority. |
| Retained interaction | Revalidate menus, presses/releases, drag captures, keyboard focus, tooltips, and queued callbacks when their node, capability, or presentation changes. |
| Plugin lifecycle | Enable, disable, reload, failure, frame switching, and resource replacement are transactional. Releasing a contribution exposes current native values. |

For each property/mutation, record: producers, scope, ownership rule, observable readers, invalidation dependencies, allowed callback phases, release behavior, revision differences, and tests. Include native builtin updates and async interface completion, not just CS2 opcodes.

### Revision adapters

The adapters expose semantic elements, collections, current state, capabilities, native actions, and layout constraints through the same contract.

- rs289lc obtains frame bindings from revconfig/builtins and mounted interfaces. Server updates and CS1 enter the common mutation path with their original semantics.
- OSRS239 obtains bindings from authored metadata and live structure. CS2 retains native IDs, hooks, content, and layout execution. A changed toplevel invalidates the previous binding by identity.
- Plugins ask for the available chat filters, sidebar actions, or map surfaces. They do not branch on root IDs or guess child indices.
- A compatible new CS2 root can provide the same semantic mapping. A changed vocabulary or layout behavior needs an adapter/profile change and conformance tests. Required missing capabilities reject activation before partial publication; optional capabilities permit a declared reduced feature set.

### Layout and publication

Trace and specify the existing script/layout/publication sequence before changing its scheduling. The target contract is:

1. Apply native mutations with their proper scope and identity changes.
2. Resolve semantic bindings and the frame's proposed allocations.
3. Supply supported allocation constraints to native layout. Native computed-geometry getters must observe a coherent layout immediately when required by the revision, including within one script.
4. Resolve contributions and settle required layout/resize callbacks. Detect repeated conflicting changes; report the responsible properties and participants rather than oscillating indefinitely.
5. Publish one coherent layout/presentation revision for paint and interaction. Reentrant or async changes must be incorporated at a defined safe point; script-visible updates must not be arbitrarily delayed to the next frame.

Define behavior when a script writes a dimension a frame has explicitly allocated. Preserve the native requested value for release, and make effective constraint/readback behavior explicit. Do not use an invisible last-writer override that causes scripts to measure one box while users interact with another.

## Milestones and acceptance gates

Conformance tests accompany every milestone. The final test milestone expands coverage; it is not the first time the contract is tested.

### M0 — Close the inventory and capture both native baselines

**Work:** enumerate every mutable tree field, structural operation, script opcode, packet writer, engine writer, and consumer/invalidation path. Identify direct runtime writes bypassing typed mutation APIs. Enumerate every plugin/API consumer, including hidden features, disabled manifests, metadata, examples, and tests.

Provision reproducible local rs289lc and OSRS239 fixtures. Record cache/content/server versions, matching checksums, build flavor, assets, preferences, input sequence, and expected native behavior. The rs289lc fixture must exercise its actual packet and CS1 paths.

**Deliverables:** mutation/consumer register, complete plugin migration checklist, measured surface/capability tables, native reference captures and interaction traces for both generations.

**Exit gate:** every mutation family and every repository plugin consumer has a disposition; missing prerequisites and unknown semantics are explicit blocking entries, not silently skipped tests.

### M1 — Specify the breaking contract and conformance cases

**Work:** settle the ownership table above, geometry getter semantics, clipping/opacity scope, action authority, callback phases, contribution conflict policy, and replacement/anchor lifecycle. Define the new API major version for C and Lua together.

Require explicit capability declarations and typed, incarnation-checked references. Keep raw tree mutation and revision-specific lookup machinery internal. Define state-to-style mappings for skins, including readable native chat on alternate backgrounds without erasing semantic colors.

**Deliverables:** normative contract, API declarations, error/result vocabulary, and executable failing cases for the decisions.

**Exit gate:** a reviewer can determine the outcome of each conflicting mutation from the contract. No unresolved ownership question may be deferred to a plugin author or solved by registration order.

### M2 — Enforce mutation, identity, and invalidation centrally

**Work:** route runtime mutations through classified operations. Ensure changes invalidate all affected layout, paint, role, hit/menu, focus, and resource caches. Handle subtree reuse, reparenting, async mount completion, queued callbacks, and release to current native state.

Add development checks for unclassified/direct writes after publication. Constructors/deserialization require a defined initialization boundary, not blanket exceptions for arbitrary runtime changes.

**Deliverables:** audited mutation paths and dependency declarations; structural and retained-interaction regression tests.

**Exit gate:** mutation sequences produce the expected native state and consistent paint/input; stale references cannot act on recycled nodes. Restoring a frame never restores obsolete native values.

### M3 — Complete both adapters and layout integration

**Work:** implement the shared semantic/capability interface for rs289lc and OSRS239. Establish coherent native layout under frame allocations, dynamic element collections, native action dispatch, resize/remount rebinding, and transactional fallback.

Audit revision-specific protocol semantics directly. Do not apply OSRS239's minimap state interpretation to an older revision without verifying that revision's contract.

**Deliverables:** two adapters, capability fixtures, geometry read-after-write tests through real native writers, and compatible/incompatible layout tests.

**Exit gate:** a frame can change placement without losing native content or actions on either generation. Unsupported reflow is rejected with a useful reason. Native hidden/closed content remains hidden/closed.

### M4 — Complete composition, chrome, and authoring UX

**Work:** implement narrowly scoped presentation claims, state-aware styling, deterministic additive ordering, and explicit exclusive-claim conflicts. Apply identical identity/availability/order rules to native drawing, attached overlays, and retained interactions. Keep native subtrees with operations intact when changing their decoration.

Use one panel/action model for plugin settings and supported chrome executors. Define logical, window, and drawable coordinates, clipping, scaling, focus transfer, and plugin enable/disable semantics. Provide a developer inspector for native state, effective state, claims, capabilities, dirty reasons, and rejected declarations. User-facing errors should explain the consequence and available choice.

**Deliverables:** complete C/Lua API surfaces, authoring metadata, representative frame/orb/panel/Lua ports, and examples using the same public contract as production plugins.

**Exit gate:** the representative ports work on both adapters without component-ID hacks or private host access. Multi-plugin conflicts are deterministic and diagnosable. Styling fixes the known mobile parchment/text contrast without freezing native text/state updates.

### M5 — Port every plugin and remove old contracts

Use the migration inventory below. Port each plugin's configuration, data/events, rendering, native actions, resource lifetime, and tests. Preserve supported behavior; explicitly gate features absent from a revision.

For every port record: requirements/capabilities, old APIs removed, native state dependencies, persistence migration, both-revision results, interaction/lifecycle cases, and negative-control evidence.

**Exit gate:** all product plugins, the Lua runtime, all shipped scripts, probes/demos, manifests, metadata, and API test fixtures use the new contract. No deprecated loader, compatibility adapter, old callback builder, or executable old API remains. Repository-wide discovery and all build configurations agree with the inventory.

### M6 — Prove the complete contract and release

Extend `tools/gameframe_matrix.sh` and its existing pixel checker to support both generations and all mutation families. Do not create a second capture harness. Run targeted scenarios, systematic cross-feature combinations, and deterministic generated sequences with failure shrinking/replay.

Compare retained output against a forced fresh resolve/rebuild, while also checking independent native semantic assertions, expected actions, and pixels. Two execution paths agreeing is insufficient if both share the same bug. Test ordinary native output with no presentation claims as a baseline.

Cover no plugin, one frame, attached overlays, conflicting contributors, user enable/disable/reload, and unsupported capabilities. Repeat critical mutations during open menus, pressed controls, dragging, focused text entry, and pending mounts. Exercise software rendering and each supported frontend/executor affected by the API cutover.

**Exit gate:** both real revision paths pass; each family has meaningful observed negative controls; visuals are inspected at 2x with control counts; exact final commits build in clean checkouts. Native launch and UI-required content preparation are reproducible without unexplained stale-pack bypasses. Existing broad content-bake failures must be repaired where required for that path or treated as release blockers, not accepted by the earlier targeted-bake evidence alone.

Measure quiet-frame, mutation-heavy, and multi-plugin costs on the two pinned fixtures. Set explicit budgets from M0 measurements before accepting regressions. A correct design must also avoid unnecessary whole-tree rebuilding and callback churn.

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

Representative ports in M4 validate the API before the bulk conversion. Every row is completed in M5, including hidden plugins and scripts normally disabled by default.

## Execution order and integration policy

Critical path: **M0 → M1 → M2 → M3 → M4 → M5 → M6**. Baseline capture, conformance development, and documentation are continuous parts of those milestones.

Use an isolated migration branch/worktree for the coordinated breaking change. The final integration must include the host, C plugins, Lua runtime/scripts, metadata, manifests, and tests together. Temporary migration commits may be incomplete; do not ship a dual-API compatibility layer to make them look complete.

Keep each engineering pass bounded: reproduce a mutation/interaction failure, fix the shared mechanism, inspect output, demonstrate a failing negative control, and rerun the appropriate two-generation checks. Preserve unrelated shared-tree edits and never use `git stash`.

The first implementation task is **M0**: close the mutation and plugin inventories and establish the actual rs289lc baseline alongside OSRS239. The first API-changing task is **M1**, driven by the measured gaps in that inventory.

## Completion checklist

- [ ] Every native mutation family has an ownership/scope/invalidation specification and executable cases.
- [ ] Both revision adapters preserve their own script, packet, layout, and action semantics.
- [ ] Geometry getters, publication, clipping, opacity, animation, and retained interactions are coherent under contributions.
- [ ] Every listed/discovered plugin and API consumer is ported; old execution APIs are removed.
- [ ] Plugin configuration/chrome and failure diagnostics are usable on supported frontends.
- [ ] Same-contract tests run through actual rs289lc and OSRS239 paths, including multi-plugin lifecycle sequences.
- [ ] Negative controls, native reference assertions, interaction checks, and inspected pixels support the result.
- [ ] Clean builds, reproducible native launch/content fixtures, and agreed performance budgets pass.

Applicable failure-ledger safeguards: L01 requirement before design; L06 retain layout and skinning; L11 preserve recorded decisions; L12 preserve operational subtrees; L22 observed negative controls; L24 clean complete builds; L29/L30 native mutation authority across both eras; L36 retained actions; L37 explicit runtime fixtures.
