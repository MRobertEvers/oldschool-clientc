# CS2 DOM Redesign Plan

Status: **Phases 0–5 landed; Phase 6 closed by its own profile gate (1.8%)** (see *Implementation status* below) —
supersedes `CS2_DOM_ARCHITECTURE_PLAN.md` (kept for its measurements and its
record of what the incremental migration learned).
Scope: `tools/cs2dom`, plus generators in `tools/cs2_gen_opcodes` and the
build-time C tools (`cachepack`, `cs2`).

---

## Implementation status

### Landed

**The AOT compiler, end to end on the real cache.**

| piece | where | gate |
|---|---|---|
| AST emit from the C decompiler | `3rd/rscache/src/cs2/cs2_gen_json.{c,h}`, `cs2 decompile --emit ast-json` | **9,724 / 9,724** scripts — exact parity with the source generator, every document valid JSON |
| CS2 → JavaScript | `src/cs2_js_emit.js` | **9,724 / 9,724** lower; all parse; zero undeclared locals |
| Signed-32 intrinsics | `src/cs2_intrinsics.js` | checked against the `CS2VM2_Op_*` handlers at the edges |
| Park classification | `scripts/gen_host_park.py` → `src/generated/cs2_host_park.js` | derived from `task_cs2_plan_yield`; 174 park-capable opcodes |
| HostKernel surface | `scripts/gen_host_surface.py` → `src/generated/cs2_host_surface.js` | derived from the decompiler's command table; 999 methods |
| UITreeJS | `src/uitree.js` | 17 tests; container fill measured at **O(1) per row** |
| HostKernel | `src/host_kernel.js` | tree/geometry/paint/hook/var ops; everything else throws by name |
| Settlement driver | `src/cs2_driver.js` | serial FIFO, park/resume, fixed point, non-convergence refused |

**Phase 2's semantics.**

| piece | where | what it pins |
|---|---|---|
| Config services | `src/host_config.js` | enum/struct/param/obj/inv/stat, and every MISS answer the C host gives |
| Widget services | `src/host_widgets.js` | getters, remaining setters, runtime param table, `cc_copy`, `if_getlayer`'s interface boundary |
| Existing services adapted | `src/host_bridge.js` | `host_db.js` on the generated surface; text measurement; recorded intents |
| Transmit pump | `src/transmit_pump.js` | the three gates — gone / hidden-defers / per-hook serial — plus timers |
| Layout | `src/layout.js` | IF3 modes, 64-bit fractional product, truncate-toward-zero centring, scroll extents, the getter barrier |
| Emit walk | `src/emit.js` | ONE interleaved pass in tree order plus a drag pass, clip intersection, retention gate |
| Hit tests | `src/hit_test.js` | click / hover / drop, sharing ONE prune rule with the draw walk |
| Painter | `src/painter.js` | one canvas; a recording surface makes it testable in Node |
| Session | `src/session.js` | the frame contract: tick → settle → interact → settle → paint |
| Asset stores | `src/assets.js` | sprite/glyph/model stores; markup neither drawn nor measured; advances are data |
| Park loader | `src/asset_loader.js` | synchronous-first servicing; a load that cannot succeed still completes |

**Phase 5's round trip.**

| piece | where | what it pins |
|---|---|---|
| `.if` / `.compack` records | `src/if_record.js` | **byte-identical** re-emit; edits in place; unmodelled fields untouched |
| JavaScript → CS2 | `src/js_to_cs2.js` | a stated subset that refuses by name rather than approximating |
| Export | `src/export.js` | a no-op save writes nothing; nothing reaches the tree that the real compiler refused; ids only ever go past the highest |
| Content assets | `src/content_assets.js` | a sprite is its CANVAS plus an offset, not its bitmap; a font is glyphs plus data advances |
| Editable TSX import | `src/tsx_import.js` | **968 / 968** interfaces import as TSX and export unchanged; unmodelled fields ride `raw`; hooks stay bindings, sentinels intact |
| toridraw seam | `src/model_source.js` | one render per pose; a spin supersedes rather than queues; an unfinished render draws nothing |
| Parity oracle | `src/emit_parity.js` | this runtime's draw list against the C client's `TORIRS_DUMP_EMIT_EXIT`; kind numbers checked against the C enum |
| Client state | `src/host_client.js` | arrays, the interface stack, options, coords, seeded randomness; operations a preview records rather than fakes |

**Phase 6 is closed, by its own gate.** The plan makes it conditional — *"add
only if profiles still identify interpreter dispatch as material"* — and
`make bench-phase6` measures that share on a real 3,230-row transaction:

| | |
|---|---|
| whole transaction | 1.885 ms |
| the script's own dispatch and arithmetic | **0.034 ms** |
| share | **1.8%** |

An AOT/superinstruction pass would attack 1.8% of the time. The phase is not
justified and building it would be doing work the plan says not to do. The
measurement is a make target so the answer can be re-checked rather than
remembered.

**The chain, proven end to end.** `make roundtrip-chain` edits one field of a
real interface, bakes it with `cachepack`, and reads the value back out of the
packed bytes:

```
width=30  ->  [one changed line in the .if]  ->  966 archives packed, exit 0
          ->  width=37 read back from the binary
```

`if_binary → if_text → edit → if_text → if_binary`, with the edit intact and
the diff one line long.

**Phase 4's browser app.**

| piece | where | what it does |
|---|---|---|
| Browser entry | `src/browser_runtime.js` | canvas + rAF + normalised input; the AOT cache keyed by cache identity and generator version |
| Dev page | `src/dev_page_canvas.js` | one canvas, chrome as plain DOM outside the mount so drafts and focus survive a remount |
| Dev server | `src/dev_canvas.js` | catalogue, records, closure lowering, modules straight from disk — no bundler |

Measured against the real tree: 968 interfaces catalogued; selecting
`vm_kudos_info` decompiles and lowers its **44-script closure** to 70 KB of
JavaScript with zero errors. `make dev` serves it; `make test` is the
JavaScript-native path and touches none of the old stack.

Gates: `make -C tools/cs2dom test` (**273 tests, 19 suites**, plus
`roundtrip-if`), `corpus-aot` (the 9,724), `roundtrip-if` (**968 interfaces +
968 compacks, 0 differing**), `roundtrip-chain` (the full binary round trip),
`generated-check` (tables versus their C sources), `bench-rebuild`.

**The performance claim, measured twice.**

| workload | old C/WASM bridge | this |
|---|---|---|
| host calls only — 35,531 calls, 3,232 components | 10.7–12.1 ms | **2.88 ms median / 5.1 ms max** |
| **the whole frame** — execute, settle, layout, emit, paint | (never reached) | **3.82 ms median / 8.13 ms max** |

The old runtime emitted 22,622 packed mutations (~1.25 MiB) per tick and
replayed them in JavaScript; it could not meet the 10 ms gate. The full-frame
number matters more than the first: it is what a browser actually has to do in
16.7 ms, and it proves none of layout, emit or paint is quietly expensive.

### What the work changed about the plan

Three things the plan got wrong or left implicit, corrected here because they
are load-bearing:

- **Generators subsume the checkpoint machinery, but the sentinel matters more
  than the `yield`.** A parking call lowers to
  `while ((t = H.op(...)) === PARK) yield;` — attempt, suspend, retry. The
  cost in the common case is one comparison, and only park-capable opcodes
  carry it. The plan said "generators"; it did not say the host answers with a
  sentinel rather than the driver inspecting a request, and that is what keeps
  the mass-rebuild path free of allocation.
- **An argument node is not an argument slot.** `scale(~script5787, 32)` is
  real: the proc returns two ints, so two nodes fill three slots. Lowering
  must count stack slots and spread multi-valued expressions.
- **A rev-239 array handle and the string local of the same index are one
  slot.** The decompiler prints `$lengtharray1(...)` for elements and
  `$string1` at a call site. Treating them as two variables declares one and
  reads the other — silent at emit time, a `ReferenceError` on the first sort.

### Bugs the gates caught

Each of these was silent — none would have failed a build:

- a real quadratic in the child index (500,500 walk steps for 1,000 rows);
  now O(1) per row at any size, proven by a step counter rather than a clock;
- `escape` rewriting the `>` it had just inserted (`<lt<gt>col=f<gt>`);
- an argument node counted as an argument slot, rejecting `scale(~proc, 32)`;
- array and string locals of the same index treated as two variables;
- the hook setters absent from the generated surface entirely, because their
  command kind is named `clientscript` and reads structural — caught only once
  the completeness test checked *the emitter against the surface* rather than
  the surface against itself;
- `noClickThrough` overriding its own children instead of only what was behind;
- the driver clearing `layoutStale` without laying anything out, so every box
  kept its authored value and a resize changed nothing;
- an `event` data field on the kernel shadowing the `event()` accessor scripts
  call — `H.event is not a function`, on the first hook to read a coordinate;
- `settle()` awaiting a load inside the drain loop, which would have made a
  frame block on I/O rather than leaving the previous frame up;
- **trailing whitespace stripped from string values** — the parser trimmed
  each line before matching, so `text=Reward: ` came back a character short and
  thirty interfaces carrying a `name= ` lost their only content. Invisible
  until the value was written somewhere;
- the dedupe key and the supersede key conflated in the model source, which
  would have frozen a spinning model at whatever pose it first asked for;
- the painter decoding a sprite's canvas offset and then never using it — the
  content tree's own sprite 0 is a 26x18 bitmap at (7, 11) on a 40x40 canvas,
  so every trimmed icon would have drawn eleven pixels high;
- **an infinite park loop**: the kernel's asset source and the painter's stores
  were different objects, so the loader satisfied one and the kernel re-checked
  the other — a retry loop that spun with no error and no frame. Fixed three
  ways (one source, `_awaitSpent` on asset ops, and a driver that refuses to
  re-park on something it just serviced).

### Still to build

**Host coverage: every operation the reference answers, 736 of 736.**

"813 of 1,141" is the other true number and it is the misleading one. The
surface lists every command the DECOMPILER can name, and the reference client
does not implement most of them either — the clan channel, the trading post,
the stock market, the hiscores and the world-map editor all reach
`CS2VM2_Op_StackMetaStub`, which balances the stack, pushes zeros and empty
strings, and announces the opcode once on stderr. This runtime reproduces that
exactly, records what it faked in `host.fakedOps`, and reports it per
interface in the parity run.

So the number to track is coverage of what the reference ANSWERS, and
`scripts/host_coverage_report.mjs` computes it against the C's own manifest
(`src/cs2vm2/cs2vm2_host_request_kinds.def`):

```
referenceAnswers: 736 / 736   (100.0%)
referenceStubs:    77 / 405   — the C stubs these too
```

Implementing a stubbed one would mean inventing behaviour the reference does
not have, and a fabricated clan roster is worse than an empty one.

Three of the families came from the OLD stack rather than being rewritten —
the world map, the loot tracker and the entity overlays are `host_worldmap.js`,
`host_loot.js` and `host_overlay.js` bridged onto the generated surface in
`host_bridge.js`, the same way `host_db.js` was. Their session state and
coordinate conversions were worked out against the real cache; only the calling
convention changed.

**Phase 0 — the parity oracle is captured and running, and it is not green.**
`make emit-reference` runs the headless C client over four interfaces and
stores its draw lists under `test/fixtures/emit`; `make emit-parity` bakes the
same interfaces here, runs their onload closures, lays out, walks emit and
diffs. Neither is part of `make test`, on purpose: the comparison is a
MEASUREMENT, not a gate, and wiring an unfinished measurement into the default
target teaches people to ignore a red build.

Where it stands (commands emitted; `prefix` is how many agree from the top):

| interface | C | this runtime | prefix | note |
|---|---|---|---|---|
| 218 `magic_spellbook` | 79 | **79** | **69** | the last 10 are one dynamically built tooltip |
| 600 `vm_kudos_info` | 158 | **158** | 14 | the border matches; the scrollbar's thumb is sized from a different scroll extent |
| 162 `chatbox` | 32 | **32** | 1 | the tab strip sits 2px left of the reference's |
| 12 `bankmain` | 66 | 64 | 1 | |

Three of the four now emit exactly the right number of draw commands.

Below the emit list, glyph antialiasing and blit compositing belong to toridraw
and the browser, and this port does not claim them byte for byte.

**What the comparison found, in the order it found it.** Every one of these was
silent — no throw, no log, a plausible wrong picture:

| defect | effect |
|---|---|
| `cc_deleteall` deleted STATIC children | the spellbook's onload cleared all 199 of its own spell icons; 211 nodes became 10 |
| hook arguments were passed unsubstituted | `-2147483645` is `event_com`; `cc_create` got it as a parent id, and every framed interface's stone border silently did not exist |
| the emit walk had a TEXT pass | every text in the tree drew above every non-text — the C client's own fixed bug, its comment still in `uitree_emit.c` |
| dynamic components had no component id | 79 of the spellbook's draw commands were anonymous where the reference named every one |
| `parawidth(text, fontId, width)` | the surface says `(string, width, fontmetrics)`; font 190 does not exist, so every measurement answered 0 and every widget sized from one laid out at its padding |
| the child-iteration family read a parent off the stack | the reference keeps a CURSOR; the walkers take no arguments at all |
| `define_array` filled 0 | -1 is `null` for every reference type; script1090 takes its error branch on iteration zero and three Slayer Rewards tabs draw nothing |
| enums were never read from the content tree | `enum_getoutputcount` answered 0, so the spellbook's spell loop never ran |
| an empty text and an absent model still emitted a command | the chatbox builds 500 scrollback rows up front; emitting the empty ones put 532 commands where the reference has 32 |
| a font's glyph pack is named by the SPRITE pack, not the font pack | 18 of 21 fonts did not decode, so every `parawidth` on one answered 0 |
| `if_input_setcursorwidth` took the component as a weight | caught by the arity gate, not by anything failing |
| the driver refused a script that re-parked on the same enum | a loop walking an enum key by key is progress, not a spin |
| `if_close(componentId)`, `array_new(size,type)`, `if_getcomponentparam(param,com)`, `highlight_*(id)` | all transposed or short against their call sites |

The last group is why `test/host_surface_arity_test.js` exists: it compares
every implemented method's signature against the generated surface, which is
the call site's own push order. It would have caught `parawidth`.

**Phase 4's deletions — the decision, fully stated, and it is the user's.**
The cutover is done: `make test`, `make dev` and every gate reach only the new
stack. The files are still on disk, and `RETIRED.md` now carries what a
decision needs rather than only a recipe:

- **eleven are TRACKED** and `git` can restore them;
- **ten are UNTRACKED**, so `git rm` on one is an unrecoverable delete of work
  nobody committed;
- **`make dev-legacy` and `make test-legacy` still reach several on purpose** —
  "unreachable from the build" meant unreachable from `make test` and `make
  dev`, not from every target, so deleting the files means deleting those two
  targets in the same change.

Destroying uncommitted work on an inference about intent is not a call to make
silently, and it is the only item here that is a judgement rather than a task.

**Phase 5 is complete.** The record, the editable TSX import, the JS→CS2
lowering and the bake are all in place and gated end to end, on the real tree
and a real cache.

**A screenshot oracle is still not captured.** The emit comparison above is the
better one for this boundary and it is running; a screenshot comparison remains
worth having on top of it, for the things only pixels show.

---

## The goal, stated as the pipeline

```text
if_binary + cs2_binaries          (a real cache, table 3 + table 12)
        ↕  cachepack / cs2                     [C kernels, exist, byte-exact]
if_text + cs2_text                (OSRS-Content .if + .cs2 records)
        ↕  cs2dom import / export             [this plan]
TSX + JavaScript                  (react-like components + scripts as JS)
```

Every arrow runs in both directions. The browser opens either end of the
chain, edits in the middle, and saves back to either end. The runtime that
executes the interface in the browser is **JavaScript/TypeScript native** —
no WASM CS2 VM, no engine router, no fail-closed migration gate. The C
client remains the correctness oracle (headless traces, screenshots) and the
C tools remain the format kernels, but neither is on the browser's hot path.

Non-goals: IF1/CS1 interfaces (dat1 lineage), the live game client's chrome
(revconfig builtins), and server-driven state beyond what the preview's host
slices already model. Model rendering stays on toridraw WASM — that is a
raster kernel, not a VM, and it is the one WASM dependency that earns its
keep.

---

# Part I — How the C client actually executes interfaces

This is the reference semantics the browser runtime must reproduce. It was
traced through `src/ui/uitree*.c`, `src/game/rs_cs2_host.c`,
`src/game/task_cs2_run.c`, `src/app.c`, and the two standing docs
(`docs/UI_RENDERER_ARCHITECTURE.md`, `docs/CS2_EXECUTION.md`). The numbers
are measured, not estimated.

## 1. The tree is a flat arena, and identity is layered

`struct UITree` (src/ui/uitree.h) holds one array of ~744-byte components
(a loaded rev-230 gameframe is ~7,100 nodes ≈ 5.3 MB). Everything else is an
index into it:

- **Topology** is `parent` / `first_child` / `next_sibling` int32 links, with
  a `last_child_hint` so appending is O(1) (cc_create fills containers one
  child at a time — without the hint a rebuild is quadratic).
- **Storage is not identity.** A reclaimed slot goes on a free-list and the
  next `cc_create` reuses it for an unrelated node, so every slot carries an
  `incarnation` counter and long-lived references pair index+incarnation.
- **Three lookup layers**, each lazily accelerated because scripts hammer
  them: `component_id -> index` (open-addressed map, incrementally
  maintained — a full rebuild per mutation was ~45% of frame time on
  rev-230); per-parent `sub_id -> child` index with a `child_key_max`
  ceiling (the chatbox's 500 message rows made every chat line ~1300 × 500
  walk steps before this existed); and group buckets
  (`component_id >> 16 -> live nodes`).
- **Dynamic vs static children share a keyspace**: a `cc_find` for sub-id k
  prefers the dynamic child, falls back to the cache-baked one. Sub-ids are
  signed; ≥ 0x8000 children are script-created.
- **Cold state hangs off pointers**: runtime hooks, menu options/submenus,
  op-key bindings, params, inv slot overrides are all lazily allocated
  side-blocks, because inlining them made every node pay for the widest one
  (a hook block used to be 13,200 bytes inline at a 9% fill rate).
- **Live-set indexes** (`UITreeNodeSet`) track which nodes carry timers, key
  hooks, models, resize hooks, scroll layers — so per-tick passes iterate
  the dozen nodes that matter instead of scanning 7,100.

## 2. Scripts run on a serial task FIFO, mutate synchronously, and can park

Nothing calls the VM inline. Input, timers, transmits, and the server's
RUNCLIENTSCRIPT all enqueue a `Task_CS2Run` on a **serial FIFO** the app
drains. Inside a task:

- The VM executes bytecode; every state-touching opcode issues **one
  synchronous host call** (`RS_CS2Host_Exec`, an exact per-opcode request
  struct from `cs2vm2_host_request_kinds.def`). The host mutates the UITree
  **immediately**. The next opcode observes the write. There is no queue, no
  diff, no commit between opcodes.
- If the host needs an asset that is not loaded (script, sprite, font, enum,
  struct, obj, component pack, model, DB row…), it returns **YIELD**. The VM
  rolls the current opcode back to a pointer-only checkpoint (stack tops,
  frame count, pc — plus an undo log for the few persistent-field mutators),
  the task parks on exactly one loader (`task_cs2_plan_yield`: **one opcode,
  one yield**, asserted), and on completion the same opcode re-executes.
  Earlier opcodes' mutations stay applied — a suspended script leaves the
  tree validly *intermediate*, which is why publication is gated (below).
- Strings live in a per-invocation bump pool; arrays are first-class handles
  carried in string locals (rev-239 encoding), capacity 5,000; hook argument
  lists go up to 44 mixed int/string positions (`str_mask` bit i = position
  i is a string).

## 3. The frame settles to a fixed point before anything is shown or clicked

`docs/CS2_EXECUTION.md` is the contract:

```text
enqueue CS2
  → run every ready task → resolve layout → enqueue follow-ups ┐
  ↑                                                            │
  └──────────────── repeat until no work remains ──────────────┘
  → interact → emit → render one committed frame
```

- Finishing a task can create more work: onResize, trigger-ops, transmit
  hooks, widget-loaded hooks. The settlement loop repeats until the runner
  is idle *and* no follow-up flag is dirty.
- While a task is parked on real I/O, the client **keeps the previous
  frame**, runs no interaction against the transient tree, and preserves
  unconsumed input.
- Server packets that touch UI/CS2 state are held and applied as one atomic
  transaction at `SERVER_TICK_END`, so a script observes every var/inv/if
  update from the same server tick regardless of packet order.

The cadence around it (`App_RunOnce`): logic ticks at fixed 20 ms with
bounded catch-up (≤ 5); `on_timer` hooks and the transmit pump run per tick;
interaction runs per frame and returns **intents** (component + hook + event
context) that the app dispatches — UI code never runs a script itself.

## 4. The transmit pump is the reactivity model, and it is aggressively gated

State → screen works by re-running registered hooks, not by diffing:

- var/varbit writes bump a change serial; `RS_CS2_PumpTransmits` (once per
  tick, early-out on a coarse dirty flag) walks registered
  `on_var_transmit` hooks and re-runs each whose trigger list intersects the
  changed vars and whose `last_seen_serial < change_serial`.
- A hook on a **hidden** component is skipped *without advancing its
  serial*, so it fires exactly once on reveal.
- varc has no authorable transmit in the .if grammar — the writer of a varc
  re-runs its readers itself (this is why cs2dom's compiler emits the
  "writer carries the readers' updates" path).
- Re-arming hooks is rampant: scripts wholesale re-`if_seton*` on every
  rebuild, so `UITree_HookEquals` skips identical re-registrations rather
  than free/strdup-ing the same 44 arguments back.

## 5. Layout is lazy and mid-script-consistent; emit is retained

- Every write to a layout input sets `layout_stale`; `UITree_LayoutResolve`
  is invalidation-driven and incremental (topology-keyed order/depth caches,
  `layout_changed` propagation). **Geometry getters are barriers**: a script
  reads `cc_getwidth` immediately after `cc_setsize` and must see the
  resolved value, so getters lazily re-resolve mid-script.
- Rendering is a **retained 4-pass emit walk** (non-text, text, drag
  non-text, drag text) producing a flat command buffer; a frame with no
  `dirty_gen` bump, no layout resolve, and no hover change **skips the walk
  entirely** (`emit_visited` reachability makes a closed interface's ticking
  model unable to defeat the gate). The component's hot fields are packed
  into its first cache line because the walk is memory-bound (~59% DRAM on
  the warm tree).
- The three hit tests (click, hover, drop target) must prune **identically**
  to emit — hidden subtrees, unselected tabs — or you get invisible click
  targets and click-through ghosts. This is a standing trap class.

## 6. The mutation patterns a browser runtime must survive

Measured shapes, worst first:

| pattern | example | scale |
|---|---|---|
| **container rebuild burst** | `ca_tasks` filter click | 35,595 host calls, ~3,230 components created, one logical tick |
| | bank rebuild | ~24k host calls over ~4,000 rows |
| | chatbox per message | 500 row components re-found + rewritten |
| | music list | 852 rows created, positioned, hooked |
| **steady tick chatter** | gameframe clock varc | every frame → transmit hooks re-run |
| | `on_timer` hooks | every 20 ms tick, mostly no visible change |
| **same-value writes** | re-run update scripts | most `if_set*` calls write what is already there; C treats dirty conservatively but *renders* nothing extra because emit is retention-gated |
| **read-after-write barriers** | dropdown sizing | `cc_getx/getwidth` immediately after `setposition/setsize` |
| **find/param control flow** | gameframe click routing | `cc_find` + `cc_getcomponentparam` decide which script runs next |
| **delete/recreate identity** | every rebuild | slots recycle instantly; stale refs must fail via incarnation, presentation identity must survive via the logical (parent, sub-id) slot |

The essential observation: **the C client absorbs tens of thousands of
mutations per tick because mutation is a plain in-memory write, and pays for
presentation exactly once per settled frame, gated by retention.** Any
browser design that pays a per-mutation cost anywhere — wire encoding, DOM
node, React reconciliation, structured clone — loses by orders of magnitude
on these bursts.

---

# Part II — What the current cs2dom is, and where it hurts

The current tool grew an incremental migration architecture:
C VM compiled to WASM as the production engine, a reviewed-slice TypeScript
VM behind a fail-closed router, a worker that owns execution and layout, a
packed-mutation bridge, chunked tree deltas to the main thread, a React
external-store renderer over per-widget DOM, plus a retained "stage" painter
with cooperatively-sliced font/sprite work. Every piece is defensible in
isolation; the composition is the mess.

### II.1 Three engines, and the safe one never runs

`wasm` (production), `typescript` (48 reviewed core opcode rows, 57 of 633
host rows), and `differential`. Routing is fail-closed per *whole closure*:
one unproven opcode-40 target, SETON-installed hook root, or group-load in a
closure keeps the entire interface on C/WASM. The audit itself reports the
outcome: across bankmain, pirate_combilock and ca_tasks — 21 entry closures,
231 reachable scripts — **zero complete interfaces are TypeScript-eligible**.
The migration cannot converge opcode-by-opcode because eligibility is
all-or-nothing per closure; meanwhile three engines, a router
(`cs2_engine_router.ts`, 829 lines), a coverage auditor
(`cs2_backend_coverage.ts` + `scripts/audit_ts_backend.js`), a positional
adapter (`cs2_host_adapter.ts`, 680 lines) and a review manifest all have to
be maintained forever.

### II.2 The WASM bridge pays per mutation, which Part I says is fatal

`wasm/cs2vm_wasm.c` batches host writes into packed records and flushes them
before every observable read (every getter, find, create is a flush
barrier). The pathological `ca_tasks` redraw emits **22,622 packed
mutations ≈ 1.25 MiB of wire data in one tick**; profiling splits the
~11 ms tick into ~4.4–4.7 ms VM + record bridge and ~4.3–4.6 ms packed JS
tree replay. The bridge has already sprouted band-aids (a retained 4,096-row
all-child snapshot buffer, borrowed-memory fast paths, a 500k-entry scalar
preload) and still misses the 10 ms hard gate (10.7–12.1 ms maxima). The
control experiment is decisive: the same workload as **direct fixed-arity JS
host calls runs in ~4.13 ms** — the boundary is the cost, not JavaScript.

### II.3 One 7,000-line HostRuntime, and it hand-duplicates rs_cs2_host.c

`src/host_runtime.js` (6,951 lines) owns the component IR, five identity
indexes, event settlement, history, layout invalidation, serialization,
renderer projection, and the dispatch of every host service. It is a
hand-written re-implementation of `src/game/rs_cs2_host.c` (10,727 lines) —
633 request kinds re-specified by hand in a second language with nothing but
tests to keep them aligned. The generated host *catalogue* exists
(`cs2_host_requests.js`, schema from `wasm/gen_host_schema.py`) but is used
as metadata, not as the implementation surface.

### II.4 Four copies of the tree, glued by a chunk protocol

Working IR (worker HostRuntime) → committed `UITreeStore` projection →
structured-clone `TreeDelta` chunks over the worker boundary → main-thread
stage map / `ViewTreeStore` mirror → immutable per-node React snapshots.
Each hop needs its own invariants (atomic chunk installation so no
subscriber sees a partial delta, stable render keys vs transient ids,
generation fences) and its own tests. The worker protocol
(`runtime_worker.js` 888 + `worker_runtime_controller.js` 696 +
`runtime_worker_protocol.js` 193 lines) exists *only* to move mutations of
copy 1 into copy 4.

### II.5 DOM churn: the renderer is one DOM node per widget

`react_tree_renderer.js` renders a `div` per widget (a `div`+`span` per
text, a `canvas` per model). Part I's mutation patterns then translate
directly into DOM life-cycle churn: a ca_tasks filter click creates ~3,230
components → ~3,230+ DOM elements built, styled, laid out by the browser,
and thrown away on the next rebuild; the chatbox pattern rewrites 500 rows
per message. React's reconciliation is *on the hot path of a VM's output*.
The mitigation — the retained stage painter with sprite/font/model work
sliced into ≤ 4 ms cooperative macrotask chunks (`font_runtime.js`, 824
lines of queue/cancellation/LRU machinery) — trades churn for multi-frame
visual settling and a second rendering architecture living beside the first
(plus a third: the old direct-DOM renderer kept behind a comparison flag).

### II.6 The worker boundary is in the wrong place

Execution and layout live in a worker to protect the input thread — but
that forces *every* interaction through an async post → coalesce → execute →
delta → chunk → commit round trip, with budget-violation tracking bolted on
to notice when it stalls anyway. The C client is single-threaded and settles
synchronously inside a frame; the measured direct-call cost (~4 ms for the
worst real transaction) fits a main-thread frame budget once the bridge and
DOM are gone. The worker added latency, a protocol, and partial-publication
hazards to avoid a cost that the redesign removes at the source.

### II.7 Layout/emit are re-derived, not ported

`preview.js` ports the C layout rules, but dirty projection is fail-closed:
only node-local paint/interaction commits take the exact-delta path;
geometry, visibility, topology and order changes still run the **full
projector** — `layout() → scan every box → deep-compare` — as the
correctness oracle. The C client's actual mechanisms (`layout_stale` +
incremental resolve, `dirty_gen` + `emit_visited` retention) were never
ported, so the tool keeps paying full-tree costs the client solved years
ago.

### II.8 The import path is read-only, so the round trip doesn't exist

`content.js` decompiles `.if` + `.compack` into the preview IR and renders a
**read-only** TSX view; hook bindings and out-of-vocabulary fields are
emitted as comments. There is no path from an edited imported interface back
to `.if`/`.cs2`, and no path at all from cache CS2 to editable JavaScript.
The authoring pipeline (TSX → IR → `.if` + `.cs2`) only covers interfaces
born in cs2dom.

### II.9 Assorted structural drag

- `dev_page.js` is a 2,488-line single-string HTML document owning picker,
  records, host controls, and preview wiring; hot-reload correctness
  (draft/focus survival) is threaded by hand through it.
- The dat2 import retains original clientscript bytes *only for the C VM*
  (`bytecode.js`, 671 lines of exact transport) — a whole subsystem that
  exists because the browser engine can't own the scripts.
- Process artifacts outweigh product in places: review manifests,
  `executableReviewed` gates, coverage audits, differential harnesses — for
  a single-user dev tool, the machinery that decides *whether the TS engine
  may run* is larger than several of the engines' actual services.
- Test/latency gates are red: the 10 ms hard dispatch ceiling still sees
  11–12 ms outliers; slicing hides latency in the UI instead of removing it.

---

# Part III — The redesign

## Principles

1. **One engine.** The browser runs JavaScript/TypeScript, period. The C VM
   is an *offline oracle* (native, in CI), never a browser fallback. No
   router, no review manifest, no per-closure eligibility.
2. **One tree, one thread.** A direct TS port of the UITree semantics is the
   single working state; scripts mutate it synchronously on the main thread
   exactly as C does; presentation reads it once per settled frame.
3. **Canvas for the interface, DOM for the chrome.** The preview surface is
   one canvas painted from a ported emit walk. No per-widget DOM exists.
   React keeps the dev chrome, inspector overlays, and records panes.
4. **Generate, don't hand-write, every mirrored surface.** Opcode semantics,
   host request kinds, stack effects, and dialect tables come from the
   single Python sources that already emit the C side.
5. **The C tools are the format kernels.** `cachepack` and `cs2` already
   round-trip byte-exactly; cs2dom shells to them (dev server) rather than
   reimplementing formats.
6. **Round-trip fidelity by pass-through.** Anything not edited re-emits
   byte-identical because we keep the original bytes/text and only recompile
   what changed.

## Architecture

```text
                    dev server (node)
   cache ⇄ cachepack ⇄ if_text/cs2_text ⇄ cs2 (compile/decompile)
                    │  import/export, id ledger, watch
                    ▼
 ┌───────────────────────── browser, main thread ─────────────────────────┐
 │  ScriptEngine (JS)                                                     │
 │   ├─ AOT: cs2 → generated JS generator functions (per cache, cached)   │
 │   └─ interpreter fallback (generated from opcode_semantics.py)         │
 │        │ direct, typed, synchronous calls                              │
 │        ▼                                                               │
 │  HostKernel (generated surface) ──► UITreeJS (uitree.c port)           │
 │   ├─ vars/varc/stat/inv   ├─ chat/social  ├─ db/enum/struct            │
 │   └─ options/worldmap/overlay/loot/… (existing host_* modules, kept)   │
 │        │                                                               │
 │  Settlement loop (CS2_EXECUTION.md port): task FIFO, parks on async    │
 │  asset loads, layout, transmit pump, fixed point, publication gate     │
 │        ▼                                                               │
 │  LayoutJS (invalidation port) → EmitJS (4-pass walk port, retained)    │
 │        ▼                                    ▼                          │
 │  hit tests (3 ports, prune-identical)   Canvas painter (atlases)       │
 │                                             │ model boxes only         │
 │  React dev chrome (picker/records/state/inspector — separate roots)    │
 └──────────────────────────────┬─────────────────────────────────────────┘
                                ▼
                    toridraw WASM model worker (kept as-is)
```

## A. The round-trip pipeline

Status of each arrow, and the work:

| arrow | kernel | status | work |
|---|---|---|---|
| if_binary ⇄ if_text | `cachepack` | exists; byte-exact round-trip proven | wire `pack` into the save path (unpack already used by `dat2.js`) |
| cs2_binary ⇄ cs2_text | `3rd/rscache/tools/cs2` | **9,724 / 9,725 scripts byte-exact** on cache.osrs239 (`cs2 roundtrip`) | none for the formats; add a `--json`/AST emit mode (below) |
| if_text → TSX | `content.js` | read-only view, hooks as comments | make it faithful and editable (A.1) |
| TSX → if_text | `ir.js` + `emit_if.js` | works for authored components | extend prop vocabulary to cover every IF3 field; unknown-field pass-through |
| cs2_text → JS | — | **new** (A.2) | transpiler on the decompiler's AST |
| JS → cs2_text | `emit_cs2.js` (expressions only) | authored subset only | extend to a statement subset (A.3) |

### A.1 Faithful, editable TSX for imported interfaces

An imported interface becomes a real module, not a comment-studded view:

- Every component maps to its JSX element with typed props for every field
  cs2dom's vocabulary knows; fields outside the vocabulary ride a `raw`
  prop (exact key/value strings, preserved verbatim on export).
- Cache hook bindings become props referencing script values:
  `onLoad={scripts.toplevel_init(3, -2147483645, 'Kudos List', 0)}` — a
  *binding record*, not a JS closure, so sentinel arguments
  (`event_opbase`, component-id placeholders) survive unchanged.
- **Export is pass-through-first**: the exporter diffs the edited component
  tree against the imported one and re-emits only changed blocks; untouched
  `.if` blocks and `.cs2` files are written back from the retained
  originals, byte-identical. Gate: import → export with zero edits must be
  `diff`-clean across the whole content tree, and (via cachepack/cs2)
  byte-exact against the source cache.

### A.2 cs2_text → JavaScript (the runtime's food, and the editable form)

Add an AST-emit mode to the C decompiler (`cs2 decompile --emit ast-json`) —
it already builds structured if/while/switch output to print the RuneStar
dialect; serializing that tree is a small, C-side change and is maximum
reuse of the kernel that just reached 9,724/9,725. A JS emitter in cs2dom
then generates, per script:

```js
// scripts/gen/script_9727.mjs — generated from clientscript 9727, do not edit
export function* script_9727(H, $int0, $int1) {
    if (((H.varp(300) / 100) | 0) <= 20) { $int0 = 16750623; }
    else { $int0 = 65280; }
    H.if_setcolour($int0, 0x03c90001);
    H.if_settext(`${(H.varp(300) / 100) | 0}`, 0x03c90001);
}
```

Rules that make this exact:

- **Generator functions are the yield mechanism.** Where the C VM
  checkpoints and re-executes an opcode after an asset load, the JS script
  simply suspends: any host call that may need an asset is
  `yield H.op(...)` and the driver resumes it with the result. The C
  design's rollback/undo-log complexity exists only because C cannot
  suspend mid-opcode; a generator preserves the same observable contract
  (no partial mutation at a suspension point) for free. The driver awaits
  the loader, then `gen.next(result)`.
- **Int semantics are pinned**: `| 0` on every arithmetic result,
  `Math.imul` for multiply, explicit C-truncating division/modulo helpers,
  the client's divide-by-zero behavior. These come from the same intrinsic
  definitions the interpreter uses (one module, both consumers).
- Locals become JS locals; the int/string operand stacks disappear into
  expressions (the decompiler already did that recovery); arrays become JS
  arrays with the 5,000 cap asserted; `gosub` becomes
  `yield* script_N(H, ...)` so parking propagates through call frames.
- **`.cc` targeting state** (active/dot component) stays in `H`, exactly as
  the C host holds it — scripts pass through it implicitly.
- Compiled output is cached per (cache CRC, script id, generator version) in
  IndexedDB; a cold cache open AOT-compiles the reachable closure of the
  selected interface, not all 9,725 scripts.
- The **generated interpreter is the fallback** for the residue: the script
  that doesn't decompile (1 in osrs239), plus a differential mode. It is
  generated from `tools/cs2_gen_opcodes/opcode_semantics.py` — which must
  grow from the current 48 reviewed rows to the full corpus-reached core
  set, replacing the stack-shape heuristics for executable coverage. Same
  host surface, same intrinsics module, directly trace-comparable with both
  the AOT output and the C client.

### A.3 Edited JavaScript → cs2_text

Editing generated script JS and saving re-enters through a **statement
subset** compiler (extension of today's expression pipeline): assignments,
if/else, while, switch, calc arithmetic, host calls from the vocabulary,
`yield*` proc calls. It emits RuneStar-dialect `.cs2` and hands it to the
real compiler (`verify.js`, exists) before anything is written. Out-of-
subset JS is a build error naming the construct — not a silent drop.
Authored TSX keeps the existing static/dynamic split (`ir.js`): plain props
become `.if` fields, expression props become generated update scripts and
transmit triggers. That compiler is the part of the current tool that is
right; it is kept intact.

## B. The runtime

### B.1 UITreeJS — port, don't re-derive

One module, a direct translation of the semantics in `uitree.c`/`uitree.h`
that Part I documents: flat node array + free list + incarnation; signed
sub-id child index with dynamic-wins precedence and the `child_key_max`
ceiling; incremental id→index map; group buckets; live node sets; lazy hook
blocks with `HookEquals` re-arm skipping; `layout_stale` /
`dirty_gen` / visibility-dirty exactly as C defines them. Plain objects in a
pre-sized array are fine to start (the C hot-block/cache-line work is a
DRAM optimization JS can't express and doesn't need at 7k nodes); if
profiles say otherwise later, hot fields can move to typed arrays behind
the same accessors. What must **not** be re-derived is the behavior: slot
recycling, incarnation checks, find precedence, and the dirty rules are the
part the current tool got wrong by approximation.

### B.2 HostKernel — generated surface, existing services

`cs2vm2_host_request_kinds.def` (633 kinds, exact per-opcode payloads)
already generates a JSON schema (`wasm/gen_host_schema.py`). Generate from
it a typed TS interface of **fixed-arity methods** — `ccSetPosition(id, x,
y, xMode, yMode)`, `ccGetWidth(id)` — plus barrier/target metadata. The AOT
compiler and interpreter call these directly; no request objects, no
tagging, no replay. Implementations:

- Tree-touching kinds: new thin methods over UITreeJS (ported from the
  corresponding `rs_cs2_host.c` handlers — this is a *transcription* task,
  handler by handler, with the C file open in the other pane).
- Everything else: the existing `host_*` modules (activity, chat/social,
  db, worldmap, overlay, loot, subject, options) are the good part of the
  current tool and are kept, re-plumbed behind the generated surface.
- A generated completeness check: every kind either has an implementation
  or throws `UnimplementedHostOp(name)` loudly — the C tree's own
  convention (assert, don't no-op) applied here.

### B.3 The settlement loop

A direct port of the `CS2_EXECUTION.md` contract onto async JS:

- Serial task FIFO; dispatching a hook enqueues, never runs inline.
- The drain loop runs tasks to completion or park; a parked generator
  awaits its loader promise; the loop continues with other *ready* work
  only where C's runner would (serial order preserved).
- After each drain: resolve layout, run resize/trigger/transmit follow-ups
  (transmit pump ported with its three gates and serials), repeat to the
  fixed point.
- **Publication gate**: hit testing and painting only see settled state;
  while parked on I/O the previous canvas frame simply stays (nothing to
  do — the canvas already shows it), and input queues.
- Cadence: 20 ms logic ticks (timers, pump) on a steady timer; paint on
  rAF only when emit retention says the frame changed.

Main thread by design. The core is thread-agnostic (no DOM access below the
painter), so if a future profile demands it, the *entire* engine —
tree, layout, emit — can move into a worker and transfer only the emit
buffer/bitmap; what is ruled out is the current split where the tree lives
on one side and its consumers on the other.

### B.4 What this deletes outright

`wasm/cs2vm_wasm.c` and the wasm build, `wasm_runtime.js` (1,610),
`cs2_engine_router.ts` (829), `cs2_host_adapter.ts` (680),
`cs2_backend_coverage.ts` + `audit_ts_backend.js`, `bytecode.js` exact
transport (671), `runtime_worker.js` (888) + protocol (193) +
`worker_runtime_controller.js` (696), the review manifests, the packed
mutation vocabulary, and the whole-closure eligibility concept. The C VM
stays in `src/cs2vm2` for the game client and the CI oracle — nothing in
the browser links it.

## C. Rendering

### C.1 One canvas, ported emit

Port `UITree_EmitWalk`'s four passes and gating table to TS: hide pruning
(with hover-gated reveal), scroll clamp and accumulated offsets,
screen-space clip intersection, drag deferral and ghost transparency,
per-kind fills. Output is the same flat descriptor list; the painter walks
it into **one 2D canvas** using:

- a sprite atlas (existing decoded sprites; the current caches in
  `asset_cache.js` are reusable),
- a glyph atlas per font/colour built from the existing font decoding
  (`font.js` metrics stay; `font_runtime.js`'s slicing queue dies — glyph
  blits into a canvas at this scale are sub-millisecond, the slicing existed
  to amortize *DOM/tint-canvas* costs),
- model widgets as reserved boxes composited from the toridraw worker's
  offscreen canvases (existing `model_render_worker.js` kept).

Retention: the `dirty_gen` + layout-resolve-seq + hover-id gate from C
decides whether the walk (and hence the paint) runs at all; a quiet tick
paints nothing. Full-surface repaint on change is the baseline (the C
client's own model — at 765×503 it is cheap); per-clip-rect partial repaint
is a later optimization if profiles ask.

### C.2 Hit testing

Port the three C hit tests (`HitTestInteractive`, hover-id resolution, drop
target) with the documented prune-identical rule, over the same tree the
emit walk reads. Input handlers on the canvas produce normalized events →
intents → hook dispatch, mirroring `UITree_InteractFrame`'s split (UI
returns intents; the app dispatches).

### C.3 React's remaining job

Dev chrome only: picker, records panes (`.if`/`.cs2`/generated JS side by
side), host-state controls, and an **inspector overlay** — a DOM layer over
the canvas that draws selection boxes/badges for the hovered tree node and
links to the records. This is where DOM belongs: dozens of nodes, human
cadence. Separate React roots per pane so preview session swaps cannot
steal focus or drafts (the one hot-reload rule worth keeping from today).

## D. Reuse-from-C inventory

| C asset | how it is reused |
|---|---|
| `cachepack`, `cs2` tools | shelled by the dev server for every binary⇄text arrow; `cs2` gains an AST-JSON emit |
| `opcode_semantics.py`, `gen_opcodes.py` | single source generating C tables **and** TS intrinsic/interpreter dispatch; grow to full corpus coverage |
| `cs2vm2_host_request_kinds.def` + `gen_host_schema.py` | generates the typed HostKernel surface + completeness check |
| `uitree.c` semantics (arena, indices, dirty, hooks) | transcribed to UITreeJS with C file as the reference; behaviors, not lines |
| `uitree_layout.c`, `ui_if3_layout.h` | layout port (seeded by today's `preview.js`, corrected to the invalidation model) |
| `uitree_emit.c`, `uitree_interact.c` | emit walk + hit test ports |
| `rs_cs2_host.c` | handler-by-handler transcription reference for tree-touching host ops |
| `task_cs2_run.c`, `rs_cs2_dispatch.c`, `CS2_EXECUTION.md` | settlement loop + transmit pump contract |
| native C client (headless) | CI oracle: script traces, tree fingerprints, screenshots (`test:native` harness exists) |
| toridraw WASM | model rendering, unchanged |

---

# Part IV — Phases

Each phase lands green and is independently useful; nothing gates on a
final cutover because there is no second engine to cut over from.

### Phase 0 — lock the oracles (small)

- Freeze the C-client reference corpus: headless traces (script id, pc,
  host call + args + results), tree fingerprints, and screenshots for
  bankmain, pirate_combilock, ca_tasks, chatbox, music list, worldmap, and
  a DB-backed interface; both OSRS-Content and raw Dat2 inputs.
- Round-trip gates in CI: `cs2 roundtrip` exact-count pinned;
  cachepack unpack→pack byte-exact pinned.

### Phase 1 — the engine in Node (the heart)

- Generate the HostKernel surface; build UITreeJS; transcribe the
  tree-touching host handlers; re-plumb existing host services.
- `cs2 --emit ast-json` + the JS emitter; generator driver; intrinsics
  module; interpreter generated from the (grown) semantics source.
- Exit: the Phase-0 corpus closures execute in Node with **zero trace
  mismatches** against the C oracle (mount + scripted interaction
  sequences), and the ca_tasks transaction (35,595 host calls) completes
  under ~5 ms in Node on this machine — i.e., beat the measured
  direct-call synthetic, with no wire and no replay.

### Phase 2 — settlement, transmits, input (still headless)

- Settlement loop, transmit pump with serials/gating, timer cadence,
  intent dispatch; the three hit tests.
- Exit: interaction differentials (click/hover/drag/scroll scripted
  sequences) match C traces and final tree fingerprints; a quiet tick
  performs zero host calls and zero emit work.

### Phase 3 — pixels

- Emit walk port + canvas painter + atlases; model worker composition.
- Exit: screenshot parity against the Phase-0 C reference images
  (borders, clipping, fonts, sprite states, hover variants, model
  camera); a full ca_tasks rebuild **including paint** inside one 16 ms
  frame, no cooperative slicing anywhere.

### Phase 4 — the browser app, rebuilt small

- New dev page: canvas preview + separate-root chrome; worker deleted;
  all three sources (authored TSX / OSRS-Content / Dat2) on the one
  runtime; IndexedDB AOT cache.
- Exit: current dev workflows (pick, edit host state, hot reload with
  draft/focus survival) work; the interaction latency corpus passes its
  10 ms hard gate with zero outliers; the deleted-file list in §B.4 is
  actually deleted.

### Phase 5 — the editable round trip

- Faithful TSX import (raw-prop pass-through, binding records), diff-based
  exporter, JS statement-subset compiler for edited scripts, cachepack
  repack path.
- Exit: (a) import→export with no edits is byte-identical from cache to
  cache; (b) edit one component field + one script statement in the
  browser, save, bake, and the C client renders the change; (c) an
  authored-from-scratch interface still ships as before.

### Phase 6 — polish that profiles must justify (optional)

Partial-repaint clip regions; typed-array hot fields in UITreeJS; opcode
superinstructions in the interpreter; pre-AOT of whole caches at import.
None of these are scheduled until a measurement names them.

---

# Risks

| risk | mitigation |
|---|---|
| Generator suspension differs observably from C's checkpoint-replay | The observable contract is "no partial mutation at a suspension point"; host methods that can park take effect only via their returned value. Differential traces across parked closures (bank's oc_param yields, PUSHSCRIPT) are a Phase-1 exit item. |
| JS number semantics drift from C int32 | One intrinsics module used by AOT and interpreter; edge-value differential tests (`INT_MIN` division, overflow, shifts) against the C VM. |
| Hand-transcribed host handlers drift from rs_cs2_host.c | The generated surface pins names/arity/stack effects; trace differentials pin behavior; the completeness check makes the unimplemented set explicit and loud. |
| The decompiler-AST route misses a script | Interpreter fallback runs raw bytecode for the residue (currently 1/9,725); the AOT/interpreter split is per script, not per closure — no eligibility cliff. |
| Main-thread execution jank on pathological content | The worst measured real transaction is ~4 ms without the bridge; the settlement loop already tolerates parking; if a profile ever exceeds budget, the whole engine moves to a worker behind the same synchronous core (emit buffer is the only crossing). |
| Canvas text/sprite fidelity regressions | Screenshot parity gate vs the C client is a phase exit, not a follow-up; glyph metrics come from the same decoded fonts. |
| Round-trip surprises in .if fields cs2dom doesn't model | `raw` pass-through + diff-based export mean unmodeled fields can't be lost; the zero-edit byte-exact gate catches the rest. |
| Scope creep back into incrementalism | §B.4's deletion list is a phase exit. The old plan's lesson stands: a migration that keeps both worlds alive pays for both worlds forever. |

# Definition of done

- The full chain `if_binary+cs2_binary ⇄ if_text+cs2_text ⇄ TSX+JS` runs in
  both directions, with byte-exact pass-through for everything untouched.
- The browser executes cache CS2 as native JavaScript (AOT generators with
  a generated-interpreter fallback), against one TS UITree, on one thread,
  with one canvas — no WASM VM, no worker protocol, no per-widget DOM.
- The Phase-0 corpus matches the C client on traces, tree fingerprints, and
  screenshots; the latency corpus passes its 10 ms hard gate with zero
  outliers; a quiet tick does zero work.
- Editing an imported interface or script in the browser and saving
  produces content the real compiler accepts, cachepack bakes, and the C
  client renders.
