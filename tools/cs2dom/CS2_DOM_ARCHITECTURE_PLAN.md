# CS2 DOM Architecture Plan

> **SUPERSEDED (2026-08-26), twice over.** `CS2_DOM_REDESIGN_PLAN.md` replaced
> this plan's C/WASM bridge and React-DOM renderer with a JavaScript-native
> engine; `CS2_DOM_CLIENT_PLAN.md` then replaced that engine with the official C
> client compiled to WebAssembly. Every file this plan describes has been
> deleted — the bridge, the engine router, the TypeScript interpreter, the
> worker protocol, the tree store and the React renderers.
>
> Kept for its measurements: the 10.7–12.1 ms bridged transaction against 3.9 ms
> unbridged, and the 22,622 packed mutations per tick that explained it.

Status: **Superseded.** (Historical: transactional renderer and reviewed
TypeScript execution slices landed; whole-interface cutover never happened.)  
Scope: `tools/cs2dom`, `src/cs2vm2`, and the CS2 opcode generators  
Primary objective: execute cache CS2 accurately and quickly against an in-memory UI tree, then publish settled changes to a real React renderer.

## Implementation snapshot

Landed:

- canonical bank fingerprint and raw-dispatch latency gates;
- transactional `UITreeStore`/`ViewTreeStore` with working-versus-committed
  state, immutable deltas, dirty categories, stable render keys, signed dynamic
  slots, and targeted subscriptions;
- atomic worker/controller stage publication with no subscriber-visible partial
  chunks;
- a real local React 19 external-store renderer plus a retained
  cache-accurate surface for large interfaces;
- preview-only hot reload and explicit Host-state Save/Revert behavior;
- one reviewed opcode-semantics source with 48 executable core rows that emits
  C declarations and TypeScript dispatch metadata, plus a browser-built
  TypeScript VM with C/WASM differential tests;
- a strict browser decoder for exact modern/legacy Dat2 clientscript records,
  backed by a generated 1,088-opcode wire catalogue;
- fail-closed, whole-closure engine routing: every opcode-40 dependency is
  checked before Host/tree construction, the production default remains
  C/WASM, and no invocation can fall back after partially mutating state;
- a generated 633-request typed Host catalogue with exact operands, stack
  records, barriers, targets, and results. Catalogue membership is not treated
  as executable TypeScript behavior. An explicit review manifest currently
  authorizes 57 positional handlers, while every other row remains fail-closed;
- an injected synchronous external-opcode seam in the TypeScript VM and an
  independent whole-closure router gate. The initially visible `ca_tasks`
  closure reaches 53 reviewed Host rows, but its runtime `SETON` operations can
  install further script roots. Until those roots are proven, `auto` selects
  C/WASM and explicit TypeScript selection fails closed;
- a production direct HostRuntime surface for all 57 reviewed Host rows, using
  fixed-arity positional calls and one nested-safe invocation transaction. It
  bypasses tagged request allocation and the generic `request()` path;
- native HostRuntime dirty-delta publication for paint/interaction-only commits,
  with exact single-node projection and a full-layout oracle for geometry,
  visibility, topology, state, viewport, order, and unclassified mutations;
- a deterministic real-cache TypeScript coverage audit for complete hook
  closures, including separate registry/reachable and opcode-frequency counts;
- retained sprite/font/model painting, delegated pointer/wheel/keyboard input,
  and bounded main-thread reconciliation tasks;
- a retained 4,096-row C/WASM all-child snapshot buffer, which removes one JS
  query and allocation/growth cycle from mass dynamic redraws without changing
  snapshot order or the bank fingerprint.

Still gated before cutover:

- add a sound static declaration/data-flow proof for script roots installed by
  runtime `SETON` operations; unknown or computed roots must remain fail-closed;
- prove that every interface group referenced by `CC_CREATE`/`CC_FIND` is
  synchronously preloaded, or retain C/WASM so native load/yield semantics are
  not silently weakened;
- migrate and differential-test every dynamically installed closure, beginning
  with `ca_tasks` timer script 5244, against C/WASM;
- migrate the remaining bank, pirate-combilock, and corpus-reached VM and HOST
  opcodes into reviewed generated semantics;
- replace the transitional flat stage projector/full-layout oracle with native
  hierarchical `TreeDelta` production for geometry, visibility, topology, and
  paint-order changes from the HostKernel;
- complete C-client screenshot/pixel parity for every reference interface and
  certify the pathological `ca_tasks` redraw below the hard raw-dispatch limit;
- make TypeScript the default only after full trace/tree parity and measured
  corpus performance justify it.

## Decision

Adopt this architecture:

```text
CS2 bytecode
    │
    ├── generated C VM compiled to WASM (reference/fallback)
    └── generated TypeScript VM compiled to JavaScript (direct browser path)
                         │
                         ▼
              synchronous HostKernel API
                         │
                         ▼
              authoritative WorkingTree
                         │
              settle the complete CS2 fixed point
                         │
                         ▼
               immutable TreeDelta commit
                         │
               worker → main-thread mirror
                         │
                         ▼
            React external-store subscriptions
                         │
                         ▼
                 DOM/canvas/toridraw
```

The VM must never operate on the DOM or on React component instances. Both VM backends operate on the same logical Host API and the same in-memory tree semantics. React observes committed snapshots only.

The TypeScript backend should be generated from the same explicit opcode semantics as the C backend. It should not be a second handwritten VM. The C backend remains the parity oracle and a whole-session fallback until the TypeScript backend passes the complete corpus.

## Why this plan

A TypeScript VM can remove Emscripten callback, request-marshalling, string-copy, and packed-replay costs by calling the JavaScript HostKernel directly. It is not automatically faster at bytecode execution than optimized C/WASM, and it does not remove tree mutation, layout, rendering, or model costs. The decision to make it the default must therefore be benchmark-driven.

The tree boundary is valuable independently of the VM language:

- VM writes become ordinary in-memory mutations rather than DOM operations.
- VM reads observe prior writes synchronously and exactly.
- React receives one settled revision rather than one update per HOST opcode.
- Dirty nodes and dirty subtrees can replace whole-tree snapshots and comparisons.
- The C/WASM and TypeScript engines can be differentially tested against the same HostKernel.
- Authored React presentation remains separate from CS2 execution semantics.

## Current state and remaining gaps

CS2Dom already contains useful pieces of the target design:

- `src/host_runtime.js` owns a mutable component IR and indexes components by key, name, file id, packed id, and parent/sub-id.
- `wasm/cs2vm_wasm.c` batches common C HOST writes and flushes them before observable reads.
- `src/runtime_worker.js` keeps VM execution and layout away from the browser input thread.
- `src/worker_runtime_controller.js` assembles worker stage transactions before notifying the page.
- the preview is mounted through React 19 and `useSyncExternalStore`; the
  retained cache painter remains a React-owned surface for interfaces where
  thousands of individual DOM nodes would exceed the budget.
- HostRuntime now emits exact node deltas for scalar paint/interaction commits,
  while retaining the old full projector as an explicit correctness oracle.

The remaining gaps are:

1. `HostRuntime` still combines VM-visible state, event settlement, history,
   layout invalidation, serialization, and renderer projection.
2. Geometry, visibility, topology, and paint-order changes still use the full
   projector; hierarchical dirty-subtree projection has not landed.
3. The generator emits reviewed C declarations and a TypeScript dispatch table,
   but production C behavior still lives in the large handwritten switch in
   `src/cs2vm2/cs2vm2.c`.
4. Most bank/pirate and wider-corpus Host behavior remains outside reviewed
   TypeScript execution. Exactly 57 Host rows are reviewed and implemented by
   the positional adapter and direct HostRuntime surface; inferred stack
   metadata is evidence for the audit, never authorization to execute.
5. The C/WASM fast path avoids one object per request, but still pays for
   encoding, borrowed-memory decoding, and JavaScript replay.

The current bank stress test is normally below the 10 ms raw-dispatch ceiling
on this machine and has canonical final snapshot fingerprint
`a5d399ff3197ba36957b0f5f2f7ecf0dea1e80f7da59b879ff19a55b2156a340`.
A recent broad-suite run still produced one 11.329 ms C/WASM bank outlier, so
the hard maximum is not yet certified.
A pathological `ca_tasks` redraw executes 35,595 HOST calls, emits 22,622
packed mutations (about 1.25 MiB of wire data), and creates roughly 3,230
components in one logical tick. Seven-run profiling measured a 11.100 ms
median before the retained child-snapshot change and 10.977 ms after it. Five
hard runs still produced 10.710--12.137 ms maxima, so the global hard gate is
still open. A controlled no-tree-apply run measured 5.218 ms: approximately
4.4--4.7 ms remains in the C VM/record bridge and 4.3--4.6 ms in packed JS tree
application, with roughly 0.5 ms of dispatch/boundary overhead.

The direct TypeScript-to-HostRuntime shape has also been measured independently:
a synthetic transaction that creates 4,000 rows and makes 24,000 fixed-arity
Host calls completes in about 4.13 ms median and publishes one commit. Lazy
private child targets also remove 4,000 public key/ref entries and improved a
controlled eager-versus-lazy comparison by about 8.9%. This is
not a substitute for real-cache differential timing, but it demonstrates that a
browser CS2 engine is not intrinsically slow; the generic bridge and tree work
are the dominant removable costs.

The current exact-cache migration audit for `bankmain`, `pirate_combilock`, and
`ca_tasks` decodes all three interfaces and all 21 entry closures without a
missing GOSUB target. It finds 231 uniquely reachable scripts / 29,933
instructions out of 2,363 loaded scripts / 208,251 instructions. One of the 21
entry closures is currently TypeScript-eligible, but no complete interface is:
all three interfaces contain at least one unresolved runtime hook installer or
unproven group-load operation, and `ca_tasks` is 0/3. Across the three-interface union, the remaining queue is 25
unreviewed core opcode kinds (132 instructions), 107 schema-only Host kinds
(1,189 instructions), 24 unresolved hook-installer kinds (370 instructions),
and two group-sensitive operations (230 instructions); no reviewed Host row
lacks an implementation. These numbers are an implementation queue, not a
reason to weaken routing.

## Required semantic model

### Working state versus committed state

The runtime has two views:

- `WorkingTree` is authoritative for the VM. HOST mutations update it immediately.
- `CommittedTree` is the stable renderer view. React sees it only after settlement.

This is publication isolation, not rollback isolation. Earlier successful opcode mutations remain in the working state if a later operation yields. A yielding opcode itself must not partially mutate state, because the C VM restores its checkpoint and retries that opcode.

Maintain separate counters:

- `mutationVersion`: preserves exact HOST mutation/history semantics.
- `commitRevision`: advances once per published fixed point.

React subscribes to `commitRevision`, never `mutationVersion`.

### Immediate barriers

The following operations require an immediate answer from the working tree or Host state and may affect later bytecode control flow:

- every component getter (`CC_GET*`, `IF_GET*`);
- component find, child iteration, collect, copy, create, and delete operations;
- geometry reads after position, size, scroll, hide, or topology writes;
- var, varbit, varc, stat, inventory, option, input, DB, enum, social, chat, world-map, minimenu, entity, and config reads;
- active-component and dot-component target changes;
- GOSUB, RETURN, arrays, switches, dynamic DB values, and trigger-hook argument parsing.

These operations must not wait for a React commit. A buffered C backend may flush earlier writes into `WorkingTree` at these barriers. A TypeScript backend can call the tree directly.

### Publication boundary

Do not commit after every script. Commit after the outer CS2 fixed point:

1. Dispatch the normalized input event.
2. Execute its hook FIFO.
3. Run nested GOSUB frames without publishing.
4. Apply queued resize calls and local trigger operations.
5. Dispatch due transmit hooks and widget-loaded hooks.
6. Repeat layout/follow-up processing until no new work is queued.
7. Reconcile hover, pressed, drag, focus, and visibility state.
8. Commit one `TreeDelta` and one `commitRevision`.

If execution waits on an asset or other asynchronous dependency, keep displaying the previous committed tree. Do not accept input against a partially settled working tree. Resume the same VM transaction, finish settlement, and then publish.

### Ordering and coalescing

Topology operations remain ordered. Create/delete/find and active/dot target changes cannot be reduced to last-write-wins records.

Scalar presentation writes may be coalesced only between observer barriers, and only when doing so preserves:

- intermediate getter results;
- no-op/version behavior;
- hidden-to-visible widget-loaded effects;
- hook installation/removal order;
- stale-reference generations;
- error and yield boundaries.

Snapshot fingerprint equality with the unbatched C path is a mandatory gate for every coalescing optimization.

## In-memory tree design

Introduce a focused store rather than adding more responsibilities to `HostRuntime`.

```ts
type NodeId = number;
type RenderKey = string;

interface UITreeNode {
    id: NodeId;
    renderKey: RenderKey;
    generation: number;
    parentId: NodeId | null;
    subId: number;
    fileId: number | string;
    name: string;
    type: number;
    props: WidgetProps;
    ops: readonly WidgetOp[];
    hooks: HookTable;
    runtime: WidgetRuntimeState;
}

interface TreeDelta {
    revision: number;
    upsert: readonly NodeSnapshot[];
    remove: readonly RenderKey[];
    reorderParents: readonly RenderKey[];
    dirtyGeometryRoots: readonly RenderKey[];
    viewport?: Viewport;
    interaction?: InteractionSnapshot;
}
```

The worker-side store should maintain:

- `nodesById` for VM component ids;
- `nodesByRenderKey` for stable presentation identity;
- `childrenByParent` keyed by signed sub-id;
- static file-id and authored-name indexes;
- active and dot targets with generation fences;
- cached layout records by node id;
- dirty sets for self-paint, subtree geometry, visibility, topology, and order;
- a transaction-local change accumulator;
- a committed render projection.

`renderKey` must remain stable when a script deletes and recreates the same logical parent/sub-id slot, while VM references still receive a new `generation`. This lets React reuse presentation safely without allowing stale CS2 references to resolve.

### Dirty projection

Replace the unconditional `layout() → scan every box → deep compare every box` publication path with dirty projection:

- color, text, sprite, model parameters, ops, and hooks dirty only the node's presentation snapshot;
- position, size, scroll, and visibility dirty the affected subtree;
- create/delete dirty the node, descendants, parent child order, hit-test index, and clips;
- viewport or unknown global dependencies may initially fall back to full layout;
- state writes use an expression dependency index to dirty only consumers;
- deleted keys are emitted directly rather than rediscovered by a full-tree comparison.

Keep the existing full layout/projector as a correctness fallback and differential oracle until dirty projection is proven across the corpus.

## Real React renderer

Add an actual React runtime for the preview. React is a renderer over the committed mirror; it is not the Host and does not own CS2 state.

Recommended public shape:

```tsx
function InterfacePreview({ store }: { store: ViewTreeStore }) {
    const roots = useUIRoots(store);
    return roots.map((key) => <Widget key={key} store={store} nodeKey={key} />);
}

function Widget({ store, nodeKey }: WidgetProps) {
    const node = useUINode(store, nodeKey); // useSyncExternalStore + selector
    return renderWidget(node, store);
}
```

Requirements:

- Use `useSyncExternalStore` or an equivalent selector-based external-store adapter.
- Preserve immutable per-node snapshots between commits so an unchanged node does not re-render.
- Publish only after the last chunk of one `TreeDelta` is installed in the main-thread mirror.
- Use stable `renderKey` values, not transient component ids, as React keys.
- Keep the picker, host-state draft editor, records panel, and other developer chrome in a separate React root or store so preview hot reload cannot steal focus or reset drafts.
- Keep bitmap fonts, sprites, and model rendering as specialized components. Toridraw may remain a worker/WASM-backed `<ModelSurface>` component.
- Permit authored renderer overrides by widget type, component name, or explicit renderer id.
- Send user interaction as typed actions to the runtime worker; do not mutate the authoritative tree from DOM event handlers.

The existing build-time TSX compiler remains useful and should not be discarded. It continues to generate `.if` and CS2 source. The new React layer is the live renderer for the resulting schema. Preview-only local React state is allowed for tooling/presentation, but cache-equivalent behavior must live in the Host/CS2 state model.

For very large grids, the default renderer may use a retained canvas surface managed by React rather than thousands of DOM elements. This still preserves React ownership of the presentation component while avoiding an impossible per-node DOM cost. Accessibility or editor overlays can remain DOM nodes.

## Dual C and TypeScript VM generation

### Do not generate from the current metadata alone

`tools/cs2_gen_opcodes/gen_opcodes.py` currently knows opcode ids, operand forms, broad dispatch groups, and whether an opcode is VM- or HOST-routed. `src/cs2vm2/gen_opcode_stack.py` knows stack shapes, but some entries are inferred. Neither describes enough behavior to generate a correct VM.

Create an explicit, reviewed semantics IR. Every supported opcode must state:

```ts
interface OpcodeSemantics {
    id: number;
    name: string;
    operand: 'none' | 'int8' | 'int32' | 'string';
    intPops: readonly ValueRole[];
    stringPops: readonly ValueRole[];
    intPushes: readonly ValueRole[];
    stringPushes: readonly ValueRole[];
    intrinsic: IntrinsicName;
    hostRequest?: HostRequestName;
    targetEffect: 'none' | 'active' | 'dot' | 'both' | 'dynamic';
    barrier: 'none' | 'read' | 'topology' | 'geometry' | 'external';
    mayYield: boolean;
    replay: 'pure' | 'checkpoint' | 'undo-log';
    dialects: readonly DialectName[];
}
```

No executable opcode may rely on a naming heuristic. Generation fails if an opcode used by the selected cache closure lacks an explicit semantic record.

### Generated artifacts

The generator should emit:

- C opcode constants and metadata, as today;
- a generated C dispatch include calling a small set of C intrinsics;
- TypeScript opcode constants and metadata;
- a generated TypeScript dispatch table calling matching TS intrinsics;
- typed Host request/result definitions;
- read/topology/geometry barrier tables;
- stack-effect tables used by validation and diagnostics;
- trace names and source maps from opcode id to semantic record.

Proposed outputs:

```text
src/cs2vm2/cs2_opcode.h
src/cs2vm2/cs2_opcode_meta.c
src/cs2vm2/cs2vm2_dispatch.gen.inc
tools/cs2dom/src/generated/cs2_opcodes.ts
tools/cs2dom/src/generated/cs2_dispatch.ts
tools/cs2dom/src/generated/cs2_host.ts
tools/cs2dom/web/generated/cs2_dispatch.js
```

Generate TypeScript source for review and types, then compile it to browser JavaScript. Do not generate two independently maintained JS and TS implementations.

### Intrinsics

Most opcodes can be described by reusable intrinsics such as:

- stack constant/local load/store;
- integer arithmetic with explicit signed-32-bit behavior;
- branch and compare;
- string join and conversion;
- HOST request with typed arguments/results;
- component target selection;
- script call/return;
- switch lookup;
- array define/load/store;
- hook-argument parse;
- DB polymorphic result handling.

Complex behavior remains handwritten once per backend as a named intrinsic. The generator selects the intrinsic and validates its declared stack/result contract. This avoids embedding arbitrary C and TypeScript snippets in the semantics data.

Both backends must match Java/C details explicitly:

- signed 32-bit overflow (`Math.imul`, `| 0`, and explicit truncation);
- division/modulo and divide-by-zero behavior;
- string null/empty conventions and pool lifetime;
- random/time providers;
- array bounds and undo logging;
- active/dot target updates;
- frame, local, return-value, and recursion limits;
- yield checkpoints and replay;
- cache dialect opcode aliases.

### Host API generation

Unify opcode semantics with `src/cs2vm2/cs2vm2_host_request_kinds.def`, which currently describes request structures but not complete stack or result semantics.

Generate a typed interface similar to:

```ts
interface CS2Host {
    ccSetPosition(component: NodeId, x: number, y: number,
                  xMode: number, yMode: number): void;
    ccGetWidth(component: NodeId): number;
    ccCreate(parent: NodeId, type: number, subId: number,
             target: TargetSlot): NodeId;
    // Generated complete surface.
}
```

The TypeScript VM calls specialized methods directly and must not allocate a generic request object per opcode. The C/WASM adapter may retain compact transactions, but it targets the same logical methods and barrier metadata.

### Engine selection and fallback

Support three runtime modes during migration:

- `wasm`: existing C VM, required reference path;
- `typescript`: generated TS VM;
- `differential`: run both engines against isolated copies of the same initial store and compare traces/results.

Select one engine for an entire session or complete script dependency closure. Never switch engines halfway through an invocation; frames, stacks, arrays, string storage, targets, and undo state cannot be migrated safely at an arbitrary opcode.

If a closure contains unsupported TS semantics, use C/WASM for that closure before execution begins.

### AOT compilation is optional and later

The first TypeScript backend should interpret the original raw Dat2 bytecode. This preserves exact fallback coverage and makes C/TS traces directly comparable.

After parity, an optional AOT backend may compile validated bytecode/IR into specialized JavaScript functions. It must use the same intrinsics and HostKernel. AOT should be pursued only if profiles show interpreter dispatch remains material after boundary and tree costs are removed.

## Worker and message architecture

Keep the authoritative VM, HostKernel, WorkingTree, settlement loop, hit testing, and layout in the runtime worker.

The main thread owns:

- the committed `ViewTreeStore` mirror;
- React roots and authored renderer modules;
- DOM/canvas/model presentation;
- normalized browser input collection;
- developer controls and drafts.

The worker sends compact binary or structured-clone `TreeDelta` chunks. The controller installs chunks into an unpublished transaction map and exposes them to React only when the final chunk arrives. A half-applied delta must never be visible to a subscriber.

Avoid sending whole render snapshots or JSON-serializing the entire interface after every interaction. Transfer only changed node records, removals, order changes, viewport changes, and interaction state.

## Hot reload

Hot reload should preserve the browser page, picker selection, host-state draft, scroll state, and focus.

Classify changes:

- renderer-only change: swap the React module and re-render from the current committed store;
- authored interface/schema or script change: initialize a new worker session, mount it off-screen, then atomically replace the preview store/tree/records when ready;
- immutable cache asset change: invalidate only affected sprite/font/model entries;
- runtime worker/VM ABI change: intentionally restart the worker, not the whole page.

Do not let HMR publish a partially mounted interface.

## Performance contract

Measure these phases separately:

1. browser event enqueue;
2. VM execution;
3. HostKernel/tree mutation;
4. settlement and layout;
5. delta encoding/transfer;
6. main-thread mirror commit;
7. React commit;
8. model/sprite/font paint.

Hard gates:

- no input-thread task at or above 10 ms;
- every audited raw runtime dispatch below 10 ms after warm-up;
- no whole-tree serialization on ordinary interactions;
- a no-op interaction produces no React notification;
- unchanged nodes retain snapshot identity and do not render;
- bank and every interface in the corpus pass the hard latency audit;
- pathological mass redraws meet the logical transaction budget or use an explicitly measured bulk/retained rendering path—slicing must not be reported as a faster transaction;
- memory remains bounded across repeated delete/recreate and hot-reload loops.

Use medians, p95, p99, and hard maxima. Run cold and warm measurements separately. Keep C/WASM, TypeScript, and differential instrumentation out of production timing runs.

## Correctness and parity gates

### VM differential trace

For each invocation, compare:

- script id, program counter, opcode, and operand;
- integer and string stack depths plus stable content hashes;
- call-frame/script ids and return state;
- active and dot component ids;
- HOST request name and ordered typed arguments;
- HOST return values;
- yield/error/done result;
- array and persistent VM state hashes.

Stop on the first mismatch and report both traces around that opcode.

### Tree differential trace

Compare after every observer barrier and final commit:

- component topology and child order;
- node generations and stale-reference behavior;
- resolved props, ops, hooks, and model parameters;
- geometry and clips;
- interaction state;
- mutation version and commit revision;
- final snapshot/tree fingerprint.

### Required test interfaces

At minimum:

- `bankmain`, exercising every quantity, mode, search, deposit, scroll, drag, menu, and close action;
- `pirate_combilock`, including model parameters and input behavior;
- `ca_tasks`, as the mass-create/mass-delete performance case;
- sailing, clans, group ironman, world-map, DB-backed, input-field, and model-heavy interfaces;
- both OSRS-Content `.if` input and direct Dat2 input.

Retain C-client screenshot/pixel/tree comparisons for borders, clipping, conditionals, font metrics, sprite state, and model camera parameters. VM parity alone cannot prove presentation parity.

## Implementation workstreams

### Workstream A: TreeStore and React

1. Specify `UITreeNode`, `WorkingTree`, `TreeDelta`, and `ViewTreeStore` schemas.
2. Extract component/index/topology operations from `HostRuntime` into `UITreeStore` without changing behavior.
3. Add dirty-category recording while retaining the full-layout fallback.
4. Add explicit begin/settle/commit transaction APIs around the current outer boundary.
5. Produce deltas directly from dirty state.
6. Update the worker/controller protocol to commit deltas atomically.
7. Add React and implement the external-store renderer.
8. Port sprite, bitmap-font, line/rect, inventory, and model presentation components.
9. Move the existing direct DOM renderer behind a temporary comparison flag, then remove it after visual parity.

### Workstream B: Shared CS2 semantics and code generation

1. Inventory every opcode reached by the rev-239 corpus and every currently implemented C handler.
2. Define explicit semantics records and eliminate stack-shape heuristics for executable opcodes.
3. Generate existing constants/meta/stack tables from that record without changing C behavior.
4. Generate typed Host request/result definitions and barrier tables.
5. Generate C dispatch calls to named intrinsics.
6. Generate the TypeScript dispatch table from the same records.
7. Implement and unit-test TypeScript VM state, stacks, frames, strings, arrays, switch tables, calls, checkpoints, and errors.
8. Add whole-closure engine selection and differential mode.

### Workstream C: Integration and optimization

1. Bind the TypeScript VM directly to `UITreeStore` and the other Host services.
2. Run mount and interaction differential tests for every interface.
3. Profile `bankmain`, `pirate_combilock`, and `ca_tasks` before selecting a default engine.
4. Optimize only measured costs: typed storage, string interning, stable keys, dirty layout, retained rendering, or opcode superinstructions.
5. Make TypeScript the default only if it wins the agreed latency gates and has complete parity; otherwise keep C/WASM as default while retaining TS for development/differential testing.

## Phased delivery and exit criteria

### Phase 0 — lock the baseline

- Record canonical C/WASM traces, fingerprints, screenshots, and latency results.
- Fail CI on a bank fingerprint change or any newly unsupported script.
- Exit when the reference corpus is repeatable.

### Phase 1 — transactional TreeStore

- Extract the tree without changing rendering.
- Add `mutationVersion`, `commitRevision`, dirty sets, and atomic deltas.
- Keep full projection available for comparison.
- Exit when old and new tree snapshots are identical across all tests.

### Phase 2 — React renderer

- Add the main-thread mirror and actual React components.
- Preserve picker/state-editor focus and preview-only HMR.
- Exit when screenshot/tree/input tests match the current renderer and no ordinary interaction transfers a full snapshot.

### Phase 3 — authoritative semantics IR

- Make all corpus-reached opcode stack effects and behaviors explicit.
- Generate the current C metadata/dispatch through the new pipeline.
- Exit when native C and C/WASM traces/fingerprints remain unchanged.

### Phase 4 — TypeScript VM

- Implement generated TS dispatch and shared intrinsics.
- Add C-versus-TS differential execution.
- Exit when the full selected script closure runs with zero trace/tree mismatches.

### Phase 5 — performance cutover

- Run uninstrumented corpus and browser interaction audits.
- Resolve any regression in execution, delta transfer, React commit, or paint.
- Select the default backend based on results, not assumption.
- Exit only when correctness, visual parity, memory, and latency gates all pass.

### Phase 6 — optional AOT/superinstructions

- Add only if profiles still identify interpreter dispatch as material.
- Generate specialized JavaScript from validated bytecode/IR while retaining the interpreter fallback.
- Exit when it produces identical differential traces and a meaningful benchmark win.

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| Two VM implementations drift | Generate both from one explicit semantic record and run differential traces continuously. |
| Stack metadata is wrong | Remove heuristics from executable coverage; require reviewed typed pops/pushes. |
| Deferred publication changes CS2 behavior | Mutate `WorkingTree` immediately; defer only renderer notification. |
| A getter observes stale geometry | Treat geometry reads as barriers and keep worker-side lazy root-to-target layout. |
| React re-renders the entire interface | Use stable per-node snapshots, selector subscriptions, and dirty deltas. |
| Dynamic recreation destroys DOM identity | Separate stable `renderKey` from VM id/generation. |
| A yielded transaction leaks partial UI | Keep the prior committed mirror visible and suppress input until settlement resumes. |
| JavaScript number behavior differs from C | Centralize signed-int arithmetic/conversion intrinsics and differential-test edge values. |
| JS VM is slower | Keep C/WASM selectable; cut over only after corpus benchmarks. |
| Large grids cannot meet DOM budgets | Use retained canvas/default virtualization while React owns the surface component. |
| HMR resets tools or input focus | Keep dev chrome outside the preview session and atomically swap only preview state. |

## Definition of done

The architecture is complete when:

- raw Dat2 and OSRS-Content interfaces use the same tree, Host, event, and renderer path;
- the generator emits C and TypeScript VM dispatch from one explicit semantics source;
- both engines can execute complete script closures and differential mode reports no mismatches;
- VM-visible mutations and reads are synchronous while React publication is atomic;
- the live preview is rendered by actual React from a committed external store;
- React/authored presentation can be extended without changing VM semantics;
- hot reload replaces only preview/tree/scripts/records and retains page controls/focus;
- sprites, bitmap fonts, borders, clipping, conditionals, input, and toridraw models match the C client reference;
- bank, pirate combilock, `ca_tasks`, and the complete interface corpus pass correctness and hard latency gates;
- the C/WASM backend remains available as an oracle and fallback until the TypeScript backend has proven equal or better performance.

## Immediate next actions

1. Add sound runtime-hook root discovery to the closure audit. Prefer compiler
   declarations or a proven stack/data-flow analysis; never guess a script id
   from an adjacent constant.
2. Add a real preload proof for interface groups used by `CC_CREATE`/`CC_FIND`;
   do not model native synchronous loading as an asynchronous mid-script retry.
3. Review and migrate the closure rooted at `ca_tasks` timer script 5244, then
   rerun exact C/WASM-versus-TypeScript mount, tick, and button traces.
4. Remove remaining allocation from direct dynamic-child creation and benchmark
   the exact `ca_tasks` transaction rather than only a synthetic workload.
5. Generate native hierarchical `TreeDelta` records for geometry, visibility,
   topology, and paint order while retaining the full-projector oracle.
6. Expand reviewed generated semantics over bank, pirate combilock, and then the
   complete cache corpus, preserving fail-closed whole-closure routing.
7. Complete C-client screenshot/pixel parity for borders, clipping, sprites,
   text, and toridraw models.
8. Make TypeScript the default only after exact trace/tree parity and the hard
   interaction-latency corpus both pass.
