# CS2 and non-world optimization targets

This is the ordered backlog for making the quiet Soft3D client world-render
bound. Work on one numbered target at a time, measure it, and either keep or
revert it before starting the next target.

The Windows XP baseline in `winxp_profiles/analysis.md` is not world-render
bound. Steady state is 100-frame windows 2–9 of the TORIRS_PERF run (800
frames, 2,631 logic ticks, 3.289 ticks/frame; window 0 is launch and window 1
contains a mid-capture reconnect that re-ran every interface `onLoad`):

| steady-state measurement | baseline |
|---|---:|
| frame | 65.58 ms |
| logic | 24.06 ms/frame |
| CS2 stage | 23.40 ms/frame, 7.11 ms/logic tick |
| — of which settle loop (`cs2_settle`) | 9.16 ms/frame |
| world/render stage | 28.08 ms/frame |
| UI emit + paint | 4.51 ms/frame |
| scripts | 22.17/logic tick |
| opcodes | 1,211.0/logic tick (54.6 per script) |
| host operations | 174.4/logic tick (7.86 per script) |
| VM acquire+init | 0.22 ms/logic tick (9.94 µs each, 100% pool hits) |

An earlier revision of this table reported 2,340.91 opcodes/tick and 344.43
host ops/tick; those figures averaged the launch and reconnect windows in.
Scripts/tick barely changes without them — the quiet workload is the same ~22
timer hooks every tick — but opcode and host-op volume halves. Two
consequences for the backlog:

- **Per-invocation overhead dominates, not opcode execution.** 23.40 ms/frame
  over 72.9 scripts/frame is 0.321 ms per script (0.195 ms excluding the
  settle loop) for a mean script of 54.6 opcodes and 7.86 host ops — work
  worth roughly 40 µs on this hardware. The difference is the fixed cost of
  dispatching one hook: a ~9.9 KB `calloc` per invocation, FIFO traffic, VM
  acquire+init, hook-slot heap churn, and 1,336-byte request clears. Targets
  1–5 attack exactly this and stay first.
- **Bytecode-level targets shrink.** At 1,211 opcodes/tick, interpreter
  dispatch is third-order. Targets 8–9 stay late and their projected wins
  halve.

The numbered order below stands, with one insertion: run new target 14 (the
settle loop) after target 5 and before target 6, once Gate 0's settle split
says what the loop actually contains. The launch track (L1–L4) at the end is
independent of the numbered order and may proceed in parallel.

## End-state budget

On a warm-cache, quiet, fixed-camera Soft3D run:

- world rasterization should be at least 80% of measured main-thread work,
  excluding waits and the operating-system present;
- CS2 should be at most 1.0 ms per logic tick;
- layout + interaction + UI emit + UI paint should be at most 2.0 ms per
  rendered frame;
- steady timer dispatch should perform no general-purpose heap allocation;
- release behavior, host-request order, and rendered output must remain
  unchanged.

The local Win64 build is the iteration platform. Windows XP is the acceptance
platform after an optimization wins locally. Use the same warm cache, login,
camera, viewport, timer-hook set, and deterministic input replay for every
comparison. Report p50 and p95, not only a mean.

The local baseline has not actually been captured:
`winxp_profiles/local-soft3d-baseline.log` shows the Win64 client exiting with
`cannot create/open incremental dat2 cache … (the directory must already
exist)`. Create the sparse cache directory (or point `--cache` at a real
cache) and record the local column of the scorecard before starting target 1.

## Gate 0 - make the budget observable

This is instrumentation, not an optimization. Add timings that separate:

1. world collect/submit/raster;
2. UI layout, interaction, emit, and paint;
3. timer dispatch and all other CS2 origins;
4. framebuffer composition, scale/copy, and OS present;
5. allocation count and bytes by CS2 task, VM, host request, hook, and string
   pool;
6. the settle loop, split into task-pump time versus per-iteration
   `UITree_LayoutResolve` time, so the 9.16 ms/frame in `cs2_settle` is
   attributable (feeds target 14).

Gate 0 must also stop measuring itself. With TORIRS_PERF enabled, every logic
tick currently walks the whole interface tree in
`UITreeIfaceStats_SampleGauges` (`src/ui/uitree_iface_stats.c:99`) and counts
the layout free list by walking it (`src/ui/uitree_layout.c:315`) — roughly
0.4–0.5 ms of every measured tick, ~6% of the CS2 figure. Move gauge sampling
to 100-frame window boundaries before trusting sub-millisecond comparisons.

Also report per `(dispatch origin, entry script id)` calls, opcodes, host ops,
yields, time, and p95. While attributing, name the timer script behind the
measured once-per-tick `cc_deleteall` + `cc_create` pair (target 5 wants it).
Gate 0 is complete when the parts reconcile with total frame time closely
enough to enforce the end-state budget, the top scripts explain at least 90%
of CS2 time, and gauge sampling is out of the tick path.

## Optimization 1 - replace the giant per-invocation CS2 task

`Task_CS2Run` currently contains `16 * 512` bytes of string argument storage,
64 integer arguments, and a 1,336-byte pending host request. Those three fields
alone are 9,784 bytes. `task_cs2_run_new()` calls `calloc` for the whole object
for every hook invocation, including every quiet `onTimer`
(`src/game/task_cs2_run.c:1367`).

Measured: 72.9 of these calloc+free cycles per steady rendered frame. The
launch-heavy sampling capture independently shows ~1.7 s of CS2 hook-slot
alloc/copy churn (`rs_cs2_runtime_hook_slot` plus host-exec dispatch heap
tails) and a heap OS-tail on 52% of `RS_CS2Host_Exec`'s subtree, out of a
capture that is 20.6% heap overall.

Split it into:

- a small fixed invocation header containing script/component/event identity;
- exact-sized argument storage, or a reference-counted immutable hook argument
  blob for registered hooks;
- a cold continuation allocated only if execution actually yields;
- a slab/free-list for small invocation headers.

Do not borrow mutable hook memory without lifetime protection: an earlier
script in the FIFO may replace or delete a later component's hook.

Acceptance gate: no fixed 8 KiB string matrix and no 1,336-byte request in the
hot invocation object; steady timer dispatch has zero general-purpose
allocations; host logs and event snapshot tests are identical. Record CS2
ms/tick and allocator samples before and after.

## Optimization 2 - run warm, non-yielding timers synchronously

The normal timer case is an already-loaded script that completes without IO,
but it currently becomes a heap task, enters the generic FIFO, acquires a VM,
and is then destroyed. This is the 0.195–0.321 ms/script fixed cost from the
baseline section: the VM pool already hits 100% of the time, yet acquire+init
alone still costs 9.94 µs per script (0.22 ms/tick) on a ~2.9 MB pooled VM —
the win is not entering the task/acquire path at all, not improving the pool.

Add a synchronous fast path using one reusable VM/execution context. If the
script requests an unavailable resource or another genuine asynchronous
operation, promote its exact state to the cold continuation from target 1 and
enqueue it. Preserve serial ordering and event snapshots. A diagnostic counter
must show synchronous completions versus promotions.

Acceptance gate: at least 95% of quiet timer scripts complete without task
creation or queue traffic; yields and host-request order match the old path;
CS2 time improves independently of target 1.

**Landed: the pool half — and the premise above was wrong about where the cost
was.** "The VM pool already hits 100% of the time, yet acquire+init alone still
costs 9.94 µs per script … the win is not entering the task/acquire path at
all, not improving the pool." The hit rate is real (32,825 of 32,826), but a hit
was only ever saving the `malloc`: `CS2VM2_Acquire` pops a block and then runs
`CS2VM2_Init` in full, and `CS2VM2_Release` runs `CS2VM2_Free` in full before
parking it. The pool parks torn-down blocks, not warm VMs, so the per-script
setup and teardown were unaffected by how well it hit.

Instrumented both halves (`cs2_vm_init_ns` existed; `cs2_vm_release_ns` is new,
and release was the larger one at 1,353 ns against init's 526):

| | before | after | |
|---|---:|---:|---:|
| `cs2_vm_init_ns` (run total) | 17.27 ms | 6.36 ms | -63% |
| `cs2_vm_release_ns` (run total) | 44.41 ms | 12.74 ms | -71% |
| round trip per script | 1,879 ns | 582 ns | **-69%** |

The fix was not a fast path. `CS2VM2_MAX_THREADS` was 4 and three of the slots
were unreachable — `CS2VM2_ThreadMain` and `CS2VM2_Run` both hand out
`threads[0]`, and a nested script acquires its own VM from the pool, which is
why that pool is a free list rather than a singleton. But `CS2VM2_Init` and
`CS2VM2_Free` walk `thread_count`, so every script paid 4x the per-thread work:
384 `free(NULL)` calls and ~1,920 field stores per round trip over state
untouched since the identical pass before it. Setting it to 1 removed exactly
that, and shrank the parked block from ~2.9 MB to ~725 KB (the pool holds 16).

CS2 0.502 -> 0.482 ms/logic tick, p95 -8.0%, `cs2_settle` -6.8%, at a workload
held to 35,204 scripts / 4,328,154 opcodes / 629,747 host ops / 0 aborts. The
42.6 ms removed shows in the frame total (-52.2 ms); the `cs2` stage *total* is
not the metric to read here, because its invocation count varies ~5% run to run
while the mean does not.

### The re-scope, and what it says about the fast path

The paragraph this replaces asked for exactly one thing before building the
riskiest item in the backlog: measure the remaining prize. Four temporary
counters split the per-script fixed cost into the three places it is paid, plus
the number of scripts that yield at all — the fast path can only cover the ones
that do not. One 2000-frame embedded-server run, 35,202 scripts:

| | run total | per script |
|---|---:|---:|
| `cs2_fx_create_ns` — build the task, dup the string args | 16.56 ms | 470 ns |
| `cs2_fx_setup_ns` — first `Run`: acquire, bind, thread start, args | 25.91 ms | 736 ns |
| `cs2_fx_teardown_ns` — release, free the task | 18.74 ms | 532 ns |
| **total fixed** | **61.20 ms** | **1,739 ns** |

At 17.6 scripts/frame that is 30.6 µs/frame, against a `cs2` stage mean of
466.9 µs and a frame mean of 8.49 ms — **6.6% of the stage and 0.36% of the
frame, if the entire fixed cost went to zero.** It cannot: `CS2VM2_ThreadStart`
and populating the arguments are the script's own work and survive any fast
path. The 10% estimate above was the right order but measured against the
pre-Optimization-10 stage.

The applicability number is the striking one. **101 of 35,202 scripts yielded —
0.29%.** The acceptance gate ("at least 95% of quiet timer scripts complete
without task creation") is satisfiable with enormous margin. But the same number
condemns the design: the promotion path — reconstructing exact state into a cold
continuation and enqueuing it in order — would run 101 times per 2000 frames.
That is a rare, hard, ordering-sensitive path guarding a gain that is a third of
a percent of the frame, and it would be exercised too seldom for a bug in it to
surface in testing. **The synchronous fast path is rejected on prize size.** The
gate stands unmet by choice, not by failure.

### Landed instead: the pool stops re-clearing what teardown already cleaned

The re-scope did surface something worth taking, in the half of the fixed cost
that has no ordering risk at all. It is the wide-clear pattern from
Optimization 4 and Optimization 10, one level up: `CS2VM2_Free` walked all 128
array descriptors calling `free()` on each cell block, and `cs2vm2_thread_init`
then walked all 128 again writing NULL and zeroes over them. Scripts allocate
almost no arrays, so nearly every one of those 128 `free()` calls was
`free(NULL)` and nearly every store wrote a zero over a zero.

Arrays are handed out by bumping `array_alloc`, so everything at or past it was
never touched. Free now walks `array_alloc` instead of 128, and clears the
`defined`/`size`/`is_string` flags as well as the pointer — which is what lets
the other side disappear entirely: a block coming back out of the pool already
has a clean array table, so `CS2VM2_Acquire` runs `cs2vm2_init_warm`, which
skips the loop. A pool *miss* still runs the full `CS2VM2_Init`, because a fresh
`malloc`'d block's array table is indeterminate; `CS2VM2_Init` itself is
unchanged and still safe on any block, which matters because the tests and
`task_cs2_script_exec` call it directly.

#### Measured

Five interleaved pairs, both binaries built up front from the same tree and run
alternately in one batch, per the drift rule below. Averages over the five:

| | wide | narrow | |
|---|---:|---:|---:|
| `cs2_vm_init_ns` (run total) | 6.25 ms | 2.33 ms | -62.7% |
| `cs2_vm_release_ns` (run total) | 12.82 ms | 3.28 ms | -74.4% |
| round trip per script | 581 ns | 171 ns | **-70.6%** |
| `cs2` stage mean | 470,264 ns | 464,275 ns | -1.27% |
| `cs2` stage p50 | 325,640 ns | 321,980 ns | -1.12% |

13.46 ms of the 61.20 ms fixed cost is gone — a fifth of it, and 70.6% of the
part the VM round trip owns. Workload held at 629,745 host ops.

The stage figure needs saying honestly: **-1.27% is below the 2.9% within-set
spread of this batch**, so on its own it would not be a result. What makes it
one is that the two instruments agree on a number that was predicted before
either was read. 13.46 ms over 2000 frames is 6.73 µs/frame, which is 1.43% of
the wide stage mean; the stage moved 1.27%, in the predicted direction in 4 of 5
pairs on mean and 4 of 5 on p50. The counter is the measurement here and the
stage is the corroboration, not the other way round — which is the right shape
for any change small enough to hide under the noise floor.

All ten `test-cs2-*` targets pass. The four `cs2_fx_*` counters were temporary
and have been removed, as the host-op histogram was.

What is left of the fixed cost is 470 ns of task construction, ~550 ns of thread
start and argument population that is genuine work, and 171 ns of VM round trip.
Pooling the `Task_CS2Run` block the way frames are pooled is the only piece of
that with a mechanical answer, and it is worth ~470 ns/script — 0.1% of the
frame. It is not worth doing before Optimization 12, which has not been
instrumented yet and is a whole stage.

## Optimization 3 - batch one tick's timer executions

Walk the timer live set once and execute its visible hooks through the reusable
context from target 2. Rewind operand/local/string arenas between scripts
instead of acquiring and releasing a VM for each hook. Re-resolve component
identity before each hook so scripts that mutate the tree retain current
semantics.

If a hook promotes to an asynchronous continuation, preserve the existing FIFO
barrier. Do not allow later hooks to observe state that the old serial queue
would have hidden behind that yield.

Acceptance gate: one timer-batch entry per logic tick, the same hook order, no
VM acquire/release in the no-yield case, and identical differential logs.

### Rejected: the prize this was written against is 91% gone

This target's mechanism is "rewind arenas between scripts instead of acquiring
and releasing a VM for each hook", executed "through the reusable context from
target 2". That context was rejected under target 2 on measured prize size, and
the same measurement retires this item.

What batching would actually remove, on the current tree:

| | per script | run total | % of `cs2` |
|---|---:|---:|---:|
| VM acquire/release round trip | 171 ns | 5.73 ms | 0.63% |
| `Task_CS2Run` construction | 470 ns | 16.55 ms | 1.81% |
| **both** | **641 ns** | **22.3 ms** | **2.43%** |

Against the whole frame that is 0.13%. When this target was written the round
trip alone was 1,879 ns — the two halves of target 2 took 91% of it, and they
took it with changes that cannot reorder anything.

The 2.43% that remains is not free of the risk, either: batching a tick's hooks
through one context *is* the synchronous fast path, applied to a tick's worth of
scripts instead of one. It needs the same exact-state promotion on yield and the
same FIFO barrier, exercised by the same 0.29% of scripts. Rejected for the same
reason and on the same evidence.

If it is ever revisited, note which half the prize now sits in: task
construction, at 2.9x the VM round trip. Pooling the `Task_CS2Run` block is a
local change that captures most of it without touching ordering at all, and it
does not require this target.

## Optimization 4 - replace the wide generic host request on the hot path

About 174 host operations execute per quiet logic tick (573 per rendered
frame). The interpreter builds and often clears a 1,336-byte tagged union,
then `RS_CS2Host_Exec` enters a second large dispatch switch.

Measured: 11.8% of `RS_CS2Host_Exec`'s sampled subtree is `memset` — the wide
request clears. The interpreter's single hottest source line is the
PUSHSCRIPT request clear (`src/cs2vm2/cs2vm2.c:1470`), and the same
whole-union `memset(&request, 0, sizeof(request))` recurs at the ENUM_LOOKUP
(`:5947`), DB (`:6028`), varbit-read (`:816`), CC_CREATE (`:1617`), and sound
(`:2060`) builders. The sound builders alone ran 1.1 s of the sampling
capture on a client with no audio device (launch track L2 removes the work
behind them; this target removes the union construction).

Generate typed host handlers from the opcode metadata. Synchronous hot
operations should pass their few operands directly and return their result
directly. Retain the generic request only for cold, yield-capable operations
and debugging/differential capture.

Start with the dynamically hottest request kinds, not a guessed list. Component
getters/setters, var/varbit/varc operations, and clock queries are candidates
only if Gate 0 ranks them highly.

Acceptance gate: the hot request kinds construct and clear no wide union and
do not enter the generic host switch; host operations/tick and their ordering
remain identical.

**Landed (first half: the clears).** The union is 4,408 bytes for one reason —
the `if_seton*` arm carries a 16x256 inline string matrix — and all 188
builders in `cs2vm2.c` paid for the whole thing to set one or two ints. Since
`kind` says which arm is live and nothing reads an inactive arm, each builder
now clears only its own arm:

```c
/* was */                                   /* now */
memset(&request, 0, sizeof(request));       request.kind = K;
request.kind = K;                           memset(&request.u.arm, 0, sizeof(request.u.arm));
```

164 sites were rewritten mechanically and 7 by hand. The 17 left on the wide
clear are kind-only requests with no payload (`IF_GETTOP`, `CLIENTCLOCK`,
`IF_CLOSE`, …) and a few whose `kind` is a two-line ternary; none is hot. The
rewrite was verified by scanning each converted builder for a second
`request.u.<arm>` reachable before its `host_exec` — a branch writing an arm
other than the cleared one would be the way this breaks, and there are none.

Measured: CS2 0.576 -> 0.516 ms/logic tick (-10.3%) at 35,224 vs 35,205
scripts. Shrinking the union itself is the obvious follow-up and is *not* safe
as written — the seton arm's strings would have to become pointers into VM
string storage, which the request outlives whenever a script yields (see the
warning in Optimization 1).

### Measured and rejected: typed handlers for the two hottest kinds

The second half of this target — keep the hot kinds out of the generic switch
entirely — was implemented, measured, and reverted. It buys nothing on x86-64.

The target says to start from measured frequency rather than a guessed list, so
the first step was a temporary histogram over `request->kind` in
`RS_CS2Host_Exec`. Over a 2000-frame embedded-server run: **629,745 host ops
across 145 distinct kinds**, and the head is concentrated —
`VARS_READ_VARC_INT` **19.2%** and `PUSHSCRIPT` **16.8%**, together 36% of
every host op in the run. Nothing else clears 6%. Two handlers would therefore
cover a third of the traffic, which is as good as this shape gets.

The implementation gave `struct CS2VM2` two typed function pointers alongside
`host_exec`, taking the operand directly:

```c
typedef int (*CS2VM2_HostVarcIntFn)(struct CS2VM2_Thread* thread, int varc_id);
typedef int (*CS2VM2_HostPushScriptFn)(struct CS2VM2_Thread* thread, int script_id);
```

They were never NULL: `CS2VM2_BindHost` installed defaults that build the
generic request exactly as the call sites used to, so no call site needed a
capability test, the twelve test harnesses were untouched, and a host opted in
through a second `CS2VM2_BindHostTypedOps`. The host side reproduced the body
of each case plus the wrapper `RS_CS2Host_Exec` puts around every op — the same
`cs2_host_ops` count, the same await retirement — so the request mix and its
ordering were preserved as the acceptance gate requires. Because the whole
change reduced to one installer call at `task_cs2_run.c:995`, the A/B was two
binaries from one tree differing in nothing else.

#### Measured

Five interleaved pairs, `cs2` stage p50 in ns:

| pair | typed | generic | delta |
|---|---|---|---|
| 1 | 335,700 | 331,200 | +1.36% |
| 2 | 339,900 | 326,200 | +4.20% |
| 3 | 322,800 | 336,000 | -3.93% |
| 4 | 322,700 | 334,900 | -3.64% |
| 5 | 331,300 | 328,500 | +0.85% |
| **mean** | **330,480** | **331,360** | **-0.27%** |

The sign flips in three pairs of five and the mean difference is a fifth of the
within-set spread (5.3% typed, 3.0% generic — this batch was noisier than the
1.5% the Optimization 10 batch saw). That is a null result, not a small win.

It is also the result the shape predicts once the first half had landed. What
the typed path actually removes is a stack frame, an arm-sized clear, and a
jump-table dispatch. The wide clear — the part that was 11.8% of the sampled
subtree and the reason this target exists — was already gone. A 4,408-byte
stack frame costs a register add, not 4 KB of work, because nothing touches the
pages it spans; only the live arm is written. So the remaining envelope was
already close to free, and the measurement says so.

Reverted in full: `cs2vm2.c/.h`, `rs_cs2_host.c/.h`, `task_cs2_run.c` are back
to the generic path, and the ten `test-cs2-*` targets pass. Worth recording
rather than retrying, for two reasons. The histogram is the reusable part — any
future per-kind work should start from those two names. And the null is
evidence about **Optimization 8**: if removing the request envelope for a third
of host ops is unmeasurable, the case for predecoding to micro-ops has to rest
on the opcode fetch and dispatch loop itself, not on call-shape overhead around
it.

One caveat on scope. This was measured on x86-64 only, and the profiles that
motivated the whole backlog are WinXP/i686, where the stack frame and the
indirect call are relatively costlier. The change was still reverted rather than
kept-for-i686 (as the trace gate was): the trace gate is a one-line guard, while
this is two typedefs, two struct fields, a second bind entry point, and two
exported host functions to carry on an unmeasured hope.

## Optimization 5 - make UI writes and hook registration idempotent

Timer scripts frequently rewrite existing component state and re-register
hooks. Avoid downstream work when the new value is equal:

- compare scalar, text, model, geometry, menu, and scroll values before marking
  a node dirty;
- compare hook `(script, args, strings, triggers)` before freeing, allocating,
  copying, and resynchronizing live sets;
- return a `changed` result from host mutations;
- remove the unconditional `redraw = 1` associated with merely queueing a
  timer; redraw because a host mutation changed visible state.

The script still executes. This target removes redundant UI and allocator
work without assuming that a timer may be skipped.

Calibration: steady-state mutation volume is small — 6.29 geometry writes,
2.56 content writes, and 1.23 hook registrations per tick — so the quiet
upside here is bounded; most hook-churn heap traffic happens at interface
open (launch and reconnect). Two measured items to chase anyway: one timer
script performs a `cc_deleteall` + `cc_create` pair every single tick
(Gate 0 names it), and the unconditional `redraw = 1` still forces full
downstream passes on ticks whose writes were all equal.

Acceptance gate: the host-request stream is identical, the visible mutation
stream contains no equal writes, and quiet UI emit/paint falls without stale
pixels or missed hook membership changes.

**Landed, except the redraw item — and the calibration above understated it by
an order of magnitude.** The 6.29/2.56/1.23-per-tick figures were sampled per
tick under Gate 0's old regime; measured across a whole run the real steady-state
volume is 15.17 geometry, 16.68 content, 15.81 other and 9.07 hook registrations
per tick. So there was far more to remove than the calibration implied:

- **Hook re-registration: 11,583 of 18,144 (63.8%) now skip.** `UITree_HookEquals`
  (`src/ui/uitree_hook.h`) answers "would `UITree_HookSet` change this slot"
  against *post-clamp* values, so a true means byte-identical rather than merely
  similar. A skip avoids freeing both tails, mallocing them back at the same
  sizes, `strdup`ing every string, the `UITree_FindByComponentId` resolve, and
  the five-set membership resync. The resync is safe to skip because
  `uitree_sync_hook_sets` is a pure function of the slot: membership cannot
  drift while the slot holds still. CS2 0.516 -> 0.509 ms/logic tick.
- **Equal writes: 33,187 of 95,331 applies (34.8%) now leave the node clean.**
  Every applier compares first; `ApplyText` and `ApplyComponentParam` compare
  *before* the `strdup`, which is where the allocator traffic was. Two are
  deliberately not comparisons: `ApplyModelAnim` stays unconditional because
  `UITreeAnim_Advance` moves `anim_frame` without marking the node, so comparing
  would freeze animated chatheads on their last-dirtied frame; and `ApplyObject`
  tracks `changed` rather than returning early, because the sibling silhouette
  it reconciles is state the write owns as much as the item fields are.
  CS2 0.509 -> 0.502 ms/logic tick, p50 0.354 -> 0.338 (-4.6%).

**The `redraw = 1` item is not actionable yet, and the reason is the finding
that should reshape target 14.** `UITree_EmitWalk` is *dirty-unaware*:
`src/ui/uitree_emit.c` never reads `is_dirty` and never calls
`UITree_NodeNeedsEmit`. The bit is written in ~15 places and consumed by
nothing in the production emit path. Two consequences:

1. The "quiet UI emit/paint falls" half of the acceptance gate above could not
   be met by this target and was not: emit and paint are flat across the
   capture (0.407 -> 0.412, 0.581 -> 0.587 ms/frame, both inside run-to-run
   variance). Compare-before-write buys allocator and store traffic, not
   downstream passes, while the walk ignores what it decided.
2. `app->need_redraw` is therefore the *sole* gate on a full-tree emit.
   Narrowing the timer loop's `redraw = 1` (`src/app.c`, the `timer_hooks`
   dispatch) needs a mutation signal covering every field emit reads — and
   `src/game/rs_cs2_host.c` reaches into `tree->components[...]` in 45 places
   of its own, so a stamp bumped from `UITree_MarkNodeDirty` would cover the
   applier path and quietly miss those. That is a stale panel, not a missed
   optimization, so the line stands with that reasoning recorded at the site.

Make the emit walk honour `UITree_NodeNeedsEmit` first. That is the change that
turns 34.8% equal writes into skipped emits, and it belongs with target 14's
damage tracking rather than here.

## Optimization 6 - cache component resolution during a script/tick

UI lookup is visible below the CS2 host path: steady state runs 83 id
lookups with 382 hash probes per tick (4.6 probes per lookup) plus 3.5
find-child walks per tick. Keep active and dot component
slot references with a tree mutation generation. Cache other component-id to
slot resolutions for the current script or timer batch. Invalidate on component
array relocation, create/delete, reparent, pack mount/unmount, or id-map rebuild.

Acceptance gate: component lookup/probe counters fall materially and a forced
tree mutation between two hooks produces exactly the old result.

### Landed, and the cache was not the thing to build

The gate is met without a cache. `uitree_id_hash` was discarding the interface
id, and fixing that took the map from **3.59 probes per lookup to 1.54** —
2,026,663 probes down to 869,911 over a 2000-frame run, at an unchanged 564,380
lookups.

The hash multiplied by the Fibonacci constant and let callers mask the low bits:

```c
return (uint32_t)component_id * 2654435761u;   /* caller does & (cap - 1) */
```

The low n bits of `x * K` are a function of the low n bits of `x` alone —
multiplication does not carry information downwards — so with cap 16384 this
keyed on bits 0..13 and ignored everything above them. Fibonacci hashing puts
its quality in the high bits and is meant to be read as `>> (32 - n)`. Ids are
`(iface_id << 16) | child_id`, so the discarded half was the entire interface
id: every component sharing a child index, across every resident interface,
hashed to one slot. Adding `h ^= h >> 16` before the mask folds those bits back
in and lands on the ~1.4 the 0.44 load factor should give.

Correctness gate: a build with `UITREE_ID_INDEX_VERIFY` — which asserts every
lookup against the linear scan — ran 300 frames against the embedded server with
zero failures. Independently, the 2000-frame run reproduced the previous run's
workload counters exactly (35,204 scripts / 4,328,154 opcodes / 629,747 host
ops); a mis-resolution would have changed which components scripts touched and
cascaded into different counts.

Timing: cs2 p50 322.0 us against 326.2 and 330.8 for the two preceding runs.
That is a ~2% improvement sitting just at the 1.4% noise band, and consistent
with 1.16M fewer random reads into two 64 kB arrays. Per the note in the
scorecard, the counter is the result here, not the clock.

Still open on this target: the per-script/tick resolution cache the section
describes, and keeping active/dot slot references with a mutation generation.
Both are now worth less than they were — the lookup they would avoid costs 1.54
probes rather than 3.59 — so re-scope before building the invalidation
machinery, which is where the risk in this item lives.

## Optimization 7 - dependency-driven timer scheduling

Only after Gate 0 identifies the hot timer scripts, classify their observable
inputs and effects:

- stateful timers still run every logic tick;
- visual timers that depend only on final `clientclock` may run once after a
  group of catch-up ticks;
- proven pure/idempotent timers may be skipped while their varp, varbit, varc,
  container, component, clock bucket, and input dependencies are unchanged.

Begin with an explicit script allowlist and invalidate conservatively. Never
apply a blanket timer skip or reduce the logic catch-up count.

Acceptance gate: every skipped execution has a recorded dependency proof;
deterministic replays have identical host effects and final frames; scripts per
tick falls without changing game-clock behavior.

### Gate 0's precondition for this target is satisfied — here is the allowlist

This target was blocked on "only after Gate 0 identifies the hot timer scripts."
It no longer is. `TORIRS_CS2_PROFILE=1`, 2,000 frames, 309 distinct scripts,
199.745 ms total in `CS2VM2_RunScript`:

| script | total | calls | per frame | µs/call | share of script time |
|---|---|---|---|---|---|
| **4725** | 84.476 ms | 2,004 | **1.00** | 42.15 | **42.3%** |
| 4730 | 27.757 ms | 6,187 | 3.09 | 4.49 | 13.9% |
| 4520 | 22.272 ms | 4,006 | 2.00 | 5.56 | 11.1% |
| 3350 | 12.158 ms | 2,009 | 1.00 | 6.05 | 6.1% |
| 2100 | 6.617 ms | 2,049 | 1.02 | 3.23 | 3.3% |
| | | | | | **76.7% cumulative** |

Five scripts are **76.7% of all script execution, and every one of them runs
every single frame.** Script 4725 alone is 42.3% of script time — 42 µs/frame,
once per frame, 2,004 calls across 2,000 frames.

That is precisely the shape this target was written for, and the allowlist it
asks to "begin with" is these five, in this order. No further discovery work is
needed before implementing it.

**The connection to target 11 is real, but not the one first written here.**
The hot scripts were assumed to be the source of the per-frame tree rebuild.
`CS2VM2_RunScript` is now bracketed with `g_torirs_cc_create_seq`, attributing
every `UITree_CcCreate` to the entry script that caused it, and the answer is
flat:

| script | calls | creates |
|---|---|---|
| 4725, 4730, 4520, 3350, 2100 | 2,003–6,184 | **0** |
| 925 | 11 | 4,660 |
| 250 | 8 | 904 |
| 1350 | 7 | 242 |

**None of the five hot per-frame scripts creates a single component.** Creates
are bulk work from a handful of interface-open calls. The "~7.85 creates a
frame" under target 11 is a total divided by 2,000 frames — a rate that was
never a rate, and reading it as one was the ninth wrong hypothesis in this
document.

What survives, and is now better supported than before: the five scripts run
every frame and are 76.7% of script time, and they do it **without touching
topology at all** — so what they do is write properties. That matches target
11's other measurement exactly: 27.43 `MarkNodeDirty` bumps a frame against
21.92 topology bumps, where the topology half is startup-weighted and the
property half is not. Property writes from these five scripts are what keeps
`dirty_gen` moving every frame and what holds `emit_gen_quiet` at zero.

So targets 7 and 11 are still one problem from two ends, and target 7 is still
the end worth attacking — via the property writes, not the creates. Skipping a
proven-unchanged 4725 removes 42 µs of script execution *and* the property
damage that defeats target 11's gate, which unblocks target 12's blit skip and
target 14's emit half.

### Targets 7 and 11 are unrelated. The scripts do no per-frame tree damage at all.

The paragraph above was written, then measured, and it is also wrong — the tenth
and eleventh wrong hypotheses in this document, both killed in one build.

`CS2VM2_RunScript` is now bracketed with `g_torirs_dirty_mark_seq` as well,
counting *reached* `UITree_MarkNodeDirty` calls — the ones that bump `dirty_gen`
and therefore the ones that hold `emit_gen_quiet` at zero. Because host ops
execute inside `RunScript`, this attribution covers every `rs_cs2_host.c` mark
site too.

| script | calls | creates | marks | marks/frame |
|---|---|---|---|---|
| 4725 | 2,003 | 0 | **0** | 0 |
| 4730 | 6,184 | 0 | 628 | 0.31 |
| 4520 | 4,004 | 0 | 4 | ~0 |
| 3350 | 2,008 | 0 | 0 | 0 |
| 2100 | 2,048 | 0 | 2 | ~0 |
| 6697, 5452, 9625, 7052, 5327 | per-frame | 0 | 0 | 0 |

**Script 4725 — 42.3% of all script time, running exactly once per frame — marks
nothing and creates nothing.** Across all 309 scripts the per-frame total is
~0.32 marks. The counter says **27.43**. Scripts account for roughly 1% of the
per-frame tree damage.

The one remaining mark site that runs unconditionally every frame is model
rotation in `UITreeAnim_Advance`, so that was instrumented too
(`anim_marks`). It is **0.000 per frame** — no rotating model is on screen in
this scene.

#### Correction: the marks are CS2's, they are just not per-frame

The paragraph that stood here concluded "~99% of the marks are non-CS2" and
listed four client-code sites to hunt. That was an arithmetic error of mine —
the 0.32 was summed over the *per-frame* scripts only, not over all 309 — and
the hunt was unnecessary. `UITree_MarkNodeDirty` now records
`__builtin_return_address(0)` per reached mark (offsets from its own address, so
the dump survives ASLR; resolve with `nm` + `addr2line`). The site totals sum to
exactly 54,858, matching `emit_dirty_mark`, so the attribution is complete:

| site | marks |
|---|---|
| `UITree_ApplyHide` (uitree.c:2851) | 9,961 |
| `UITree_ApplySizeModes` (:3214) | 7,806 |
| `UITree_ApplyPositionModes` (:3183) | 4,474 |
| `UITree_ApplyText` (:3016) | 4,337 |
| `UITree_ApplyTextFont` (:3708) | 4,290 |
| `UITree_ApplyTextAlign` (:3732) | 4,286 |
| `UITree_ApplyOpBase` (:3895) | 2,079 |
| `UITree_ApplyColour` (:3083) | 1,681 |
| `UITree_ApplyTextShadow` (:3755) | 1,583 |
| `UITree_ApplyGraphic` (:3054) | 1,483 |

Every one is a `cc_*` setter, reached from a CS2 host op. Summing the `marks`
column of the script profile over the top 20 alone gives ≥39,350 — **≥72% of all
marks are CS2's**, and the remainder is the profile's tail plus the negative
offsets in the site dump (non-`uitree.c` callers), not a missing per-frame
source.

#### The gate's actual blocker: 7.8 hide flips a frame, each bumping topology

`dirty_gen` is bumped from thirteen sites in `uitree.c`; all thirteen now route
through `uitree_topo_bump(tree, __LINE__)` and report at exit. Measured over
2,000 frames:

| site | bumps | per frame | shape |
|---|---|---|---|
| `uitree.c:1019` — component allocation | 18,908 | 9.45 | bursty, tracks creates |
| `uitree.c:2921` — `UITree_ApplyHide` | 15,655 | **7.83** | **per-frame** |
| `uitree.c:1187`, `:1217` — child link/unlink | 3,176 each | 1.59 | bursty |
| `uitree.c:2831` | 2,391 | 1.20 | bursty |
| `uitree.c:3587`, `:1111`, `:1474` | 530 total | — | negligible |

`UITree_MarkAllDirty` (`uitree.c:1474`) is called **twice in the entire run**.
It looked like the obvious blanket-invalidation culprit and it is not.

### The cause is one per-frame script, and the fix is not a list of script ids

Removing script 4725 from the frame (2,000 frames, embedded server, same scene):

| | baseline | 4725 skipped |
|---|---|---|
| `cs2` p50 | 336,700 ns | **131,800 ns** (−61%) |
| `gate_tree_quiet` | 0 | **1,944** |
| `emit_list_same` | 1,994 | 1,994 |
| `uitree_cc_create` | 15,697 | 15,697 |

4725 is the per-frame `dirty_gen` source. It reported **zero** marks and zero
creates under per-script attribution because it does not write the tree inside
`RunScript` — it schedules work that lands after the bracket closes. Every
attempt in the sections above to find the per-frame mark source by instrumenting
`uitree.c` failed for that reason: the cause was never in the tree code, and no
amount of finer tree instrumentation would have found it.

**Do not turn this into an allowlist.** `TORIRS_CS2_SKIP` exists in
`CS2VM2_RunScript` as a measurement instrument and must not become the shipped
mechanism: a hardcoded script id is scoped to one scene and one content
revision, it silently rots when content changes, and it treats a measurement of
this replay as a property of the client. It answers "is this script's per-frame
work redundant?" — nothing more. The answer is yes.

The real defect is that a script whose output is already current is re-entered
every frame. The scalable fixes, in preference order:

1. **Make the invocation event-driven.** Find what schedules 4725 per frame and
   fire it on the state change it is reacting to instead of on the frame clock.
   This removes the cost for every script with the same shape, not one id.
2. **Make its scheduled work idempotent at the sink.** The `cc_*` setters
   already compare-before-mark; whatever 4725 schedules evidently does not.
   Applying the same discipline there makes the redundant re-entry cheap rather
   than eliminating it — weaker than (1), but general and low-risk.

Both need 4725's trigger identified first, which is a question about what
enqueues it, not about the VM.

#### Refuted: the trigger is not the call-on-resize queue

`if_call_on_resize` (rs_cs2_host.c:8548) re-arms the queue that
`app_cs2_enqueue_followups` drains, so a listener that calls it re-queues itself
forever — a self-perpetuating per-frame loop that fit the evidence exactly. Route
1 was built against it: cache each component's resolved `abs_w`/`abs_h` and
dispatch the `on_resize` hook only when the size actually differs, direct-mapped
by component id, first query always dispatching.

It was measured and **reverted**. `resize_hook_skip` is **0** — the gate never
fired once — and `cs2` p50 is 338,900 ns against a 336,700 ns baseline, i.e.
unchanged. The resize queue does not dispatch 4725 in this scene at all.

The revert is the point, not a footnote: an unfiring gate is not neutral. It
would silently drop any resize dispatch that a future scene needs without a
dimension change, buying a risk with no measured return. Do not re-land it
without a scene where `resize_hook_skip` is nonzero.

**Still unknown: what dispatches 4725 every frame.** The remaining followup
limbs in `app_cs2_enqueue_followups` (trigger-ops, social sends) and the task
queue itself are unexamined. Instrument the dispatch side — log the enqueue path
that leads to a `RunScript` of 4725 — rather than inspecting candidate queues,
which is the method that just failed here and failed eleven times above.

#### Measured, not inferred: the tree term never holds, on any frame

The gate is `dirty_gen == prev && hover == prev`. Splitting the two terms into
`gate_tree_quiet` / `gate_hover_quiet` over 2,000 frames:

| counter | total |
|---|---|
| `emit_list_same` | 1,994 |
| `gate_hover_quiet` | 1,998 |
| **`gate_tree_quiet`** | **0** |
| `emit_gen_quiet` | 0 |
| `emit_gen_unsound` | 0 |

`dirty_gen` is never unchanged between two frames — not once. Hover was never
the blocker.

**This also kills the "everything is bursty" reading**, which was drafted below
and is wrong. Zero frames at zero bumps means a steady per-frame trickle, so the
~9.45 allocations and ~7.83 hide flips a frame are genuine rates. The flip
census resolves consistently: ~15,000 distinct component ids at exactly one flip
each is not a burst, it is a client that continuously creates components and
hides each new one once. `uitree.c:1019` and `uitree.c:2921` are the same
behaviour counted twice.

That is the real defect, and it is upstream of every gate: **the client allocates
and hides ~8 components every frame and then emits a byte-identical list.**
Target 11's gate cannot be made to fire by narrowing a bump, because the bumps
describe work that genuinely happened. The fix is to stop the churn, and the
first question is which component ids are being allocated — the flip census
already prints them, and its 64-entry table needs raising to capture the tail.

Every allocation is now attributed by caller (`uitree_alloc_site_record`, offsets
from itself):

| caller | allocs |
|---|---|
| `UITree_CcCreate` (uitree.c:2720) | **15,697** |
| `UITree_PushBuildComponent` (uitree_build.c:242) | 3,190 |
| two others | 21 |

`UITree_CcCreate` accounts for all 15,697 — exactly the `uitree_cc_create`
total. So the churn is entirely CS2 script-driven component creation, and the
~5,200 creates called "unattributed outside RunScript" above are simply the tail
of the 309-script profile below its printed top 20, not a separate source.

**What is still not established** is which single source bumps `dirty_gen` on
*every* frame. `gate_tree_quiet == 0` proves one does; the enumerated site totals
narrow it to those exceeding 2,000 bumps (`CcCreate` alloc 15,697, `ApplyHide`
15,655, child link/unlink 3,176 each, `uitree.c:2831` 2,391), but a total cannot
distinguish "1 per frame for 2,000 frames" from "2,000 in one frame". The
measurement that settles it is a per-frame histogram of bumps-per-site — the
site tables already exist, and only need sampling at frame boundaries rather
than at exit. Do that before attributing the churn to any one of them; four
conclusions in this document have already been overturned by treating a total as
a rate.

`ApplyHide` returns early when `hide` already equals the requested value
(`uitree_apply_nochange` counts those), so all 15,655 are genuine flips — the
client hides or unhides ~7.8 components every frame. Each one bumps topology
*unconditionally* and deliberately: the comment at the site is correct that an
unhide cannot be filtered through `emit_visited`, because reachability is
exactly what the write is changing.

**So the gate is not too conservative and the workload is not idle.** With
`emit_list_same` at 0.997 and `emit_gen_unsound` at 0, the emit list is
byte-identical on 1,994 of 2,000 frames while 7.8 real visibility changes land
per frame. Those flips are either toggling back and forth within a frame, or
landing on nodes that do not reach the emit list. Both are answerable, and the
answer is the unlock for targets 11, 12 and 14's emit half:

1. Log `(component_id, old, new)` at `uitree.c:2921` for 60 frames and check
   whether the same id flips both directions. If it does, the fix is upstream of
   the tree — a script or clientcode writing `hide` twice a frame — and it is
   also more script work than target 7 would ever save.
2. If the flips are one-directional on nodes outside the emit list, the topo
   bump can be narrowed: bump unconditionally only when the node is currently
   hidden (an unhide, which genuinely abolishes reachability) and fall back to
   the ordinary reachability-filtered mark when hiding an already-reached node.
   That is a small, local change with `emit_gen_unsound` as its existing proof.

Do not attempt (2) without (1). The site's comment documents a correctness trap
that a plausible-looking narrowing walks straight into.

**The conclusion that matters is unchanged, and is now the same one twice.**
Marks, like creates, are *bursty interface-open work averaged across 2,000
frames* — 9290, 925, 1350, 250 and 664 raise thousands apiece across single-digit
call counts. `27.43 per frame` was never a per-frame rate, exactly as `7.85
creates per frame` was not. The scripts that actually run every frame — 4725
above all — raise essentially none.

So: target 7 does not unblock target 11, and target 11 is not blocked by a
mysterious non-CS2 mark source. Both halves of that framing are dead, for the
same reason: **this document's per-frame figures are totals divided by the frame
count, and three separate conclusions have now been drawn from reading one as a
rate.** Before any `per_frame` column in this file is used as evidence again,
check whether the underlying total is bursty. `emit_dirty_topo` at 21.918/frame
has not been checked and is the obvious next victim of the same error.

**Consequences for the backlog, both of which are real:**

- **Target 7 no longer unblocks anything.** Skipping 4725 is worth what it is
  worth on its own — 42 µs a frame, 8.7% of the `cs2` stage — and nothing more.
  It does not unblock target 11, 12 or 14. That is still the single largest
  identified win left in this document, but it must be justified on its own
  merits, and its dependency-proof gate is unchanged and still unbuilt.
- **Target 11's gate is not blocked by scripts.** Its write-up concluded the
  redundant per-frame rebuild was a defect caused by the UI scripts. It is not.
  Whatever bumps `dirty_gen` 27 times a frame is non-CS2 client code, and until
  that site is named, targets 11, 12 and 14's emit half stay blocked on a cause
  nobody has located yet.

Sequence from here: find the mark site (one build, one capture, per the list
above). Then decide target 7 on its own 42 µs. Do not skip on a timer heuristic;
this target's gate demands a recorded proof per skipped execution, and target
11's write-up documents why an under-approximated dirty signal freezes a panel
rather than merely losing a skip.

## Optimization 8 - predecode CS2 into compact micro-ops

At clientscript load, translate the three parallel opcode/operand arrays into
a compact execution form containing the resolved handler, operand, flags, and
direct constant/callee references. Resolve dialect selection, `CAN_YIELD`,
stack effects, and warm gosub targets once.

Keep `pc`, stack pointers, frame, and active/dot identities in interpreter
locals across ordinary micro-ops; spill them at calls, host boundaries, yields,
and tracing points. Use a generated handler table or GCC computed-goto backend
where supported.

Recalibrated by the corrected baseline: steady opcode volume is 1,211/tick
(54.6 per script), half the earlier estimate, and per-invocation overhead
dwarfs dispatch. Predecode pays only after targets 1–5 land; do not promote
it on the old 2,341/tick figure.

Acceptance gate: lower ns/opcode on an arithmetic VM benchmark and on the real
hot timers, with unchanged opcode count and host effects. Reject it if larger
micro-ops cause an instruction-cache regression on 32-bit XP.

## Optimization 9 - specialize frequent opcode sequences

Use the dynamic opcode-pair/triple histogram to add a small number of private
superinstructions during predecode. Fuse local loads, constants, arithmetic,
comparisons, branches, and result stores where stack/error semantics remain
identical. Keep original-PC mapping for errors and traces.

Acceptance gate: at least 20% fewer dispatches on the hot timer set, a smaller
CS2 ms/tick, and no excessive decoded-code growth.

## Optimization 10 - compact the VM working set

Reduce `CS2VM2_MAX_THREADS` from four to the one thread actually accessed.
Right-size locals and cold arrays, keep immutable constants borrowed, and use
rewindable arenas for produced strings and CS2 arrays. Separate frequently
accessed thread fields from cold diagnostics/yield state.

Acceptance gate: record `sizeof(CS2VM2)`, peak committed bytes, allocations,
and cache-miss samples; require a runtime win, not only a memory reduction.

### Landed: frame locals are grown, not reserved

`CS2VM2_MAX_THREADS` was already dropped to one under target 2, so the item this
section actually had left was "right-size locals". It was worth more than the
thread pool was.

A frame carried its locals inline: `int[CS2VM_MAX_LOCALS]` plus
`char*[CS2VM_MAX_LOCALS]`, 12,288 bytes of a 12,352-byte struct, sized for a
depth no cache script comes near. They are two grown buffers now, and the frame
is **96 bytes**. Blocks are pooled and buffers ride along with them, so a warm
pool reallocs only when a frame goes deeper than any script that has occupied it
before — the whole 2000-frame capture grows **4,032 bytes** of locals across
every block it ever allocates.

**The clearing was the bigger half, and it is per-script, not per-block.** The
pool counters said the allocation was already solved — 55,627 hits against 10
misses — and that is exactly what made the real cost invisible: a warm thread
reuses one block across many pushes, so a 100% hit rate says nothing about how
often a frame is *cleared*. `CS2VM2_PushCallScript` memset the whole 12,352
bytes on every script entry and every gosub. Instrumenting it first is what
turned the item from a memory tidy-up into a measurable one:

| | per run (2000 frames) | per frame |
| --- | --- | --- |
| frame pushes | 138,215 | 69 |
| bytes zeroed, before | 1,707,231,680 | 853 KB |
| bytes zeroed, after | 3,159,248 | 1.58 KB |

A mean of **22.9 bytes** per push is what a script's locals actually amount to.
The rest was rewriting zeros over zeros.

#### The invariant, and why it is a write high-water mark

Two rules carry it: slots in `[dirty, cap)` are zero, and slots at or above `cap`
are *logically* zero. So one compare against `dirty` answers a read — an
unwritten local never touches the buffer — and a push clears only `[0, dirty)`
to hand the next occupant the all-zero locals it is entitled to.

`dirty` counts **actual writes**, not the script's declared `local_int_count`,
and that distinction is the safety of the whole scheme rather than a detail. The
opcodes that write a local bound their index against `CS2VM_MAX_LOCALS`, not
against the declared count (`cs2vm2.c` had two sites checking the former and four
checking nothing at all). A script writing past what it declared would leave dirt
above a declared-count mark, and the next occupant would read the previous
script's values where it expects zero — a silent wrong-value bug in whatever UI
built next, with nothing logged. Every write raises the mark and grows the
buffer, so no write can escape the next clear.

The four unchecked sites now assert their index. That adds no failure mode: an
out-of-range index there has always been an out-of-bounds write landing in
`str_locals` or past the frame, so this only names it.

#### Measured

Between-batch drift on this machine is larger than the effect — the same lazy
binary measured 320.0 us p50 in one batch and 338.2 us in the next, 5.7% apart —
so single captures taken before and after a rebuild prove nothing here. The A/B
is two binaries built up front and run **interleaved**, the comparison binary
being the same source with the buffers forced back to full size so the accessor
path is identical on both sides:

| pair | lazy p50 (ns) | eager p50 (ns) | delta |
| --- | --- | --- | --- |
| 1 | 338,200 | 354,900 | 16,700 |
| 2 | 339,900 | 353,600 | 13,700 |
| 3 | 334,900 | 350,000 | 15,100 |
| 4 | 339,700 | 341,900 | 2,200 |
| 5 | 338,700 | 343,200 | 4,500 |
| mean | 338,280 | 348,720 | **10,440 (3.0%)** |

Lower in all five pairs, against a 1.5% intra-set spread. The gate asked for a
runtime win and not only a memory reduction, so: 3.0% of the cs2 stage, which is
itself ~4% of the frame — real, and small in absolute terms. The memory figure
is the louder one (12,352 → 96 bytes per frame) but it is not what the gate was
asking for and is not claimed as the result.

Correctness gates: all ten `test-cs2-*` targets pass, and the workload signature
is unchanged at 35,202 scripts / 4,328,146 opcodes / 629,745 host ops (against
35,203 / 4,328,150 / 629,744 on the pre-change binary in the same batch).

## Optimization 11 - retained non-world rendering

Once host writes report real change, retain UI command buffers or rendered
layers by dirty subtree. A quiet timer that changes nothing must not rebuild or
repaint the gameframe. Cursor, hover, minimenu, chat, and animated components
need narrow invalidation rather than a full UI pass. Keep world and UI damage
separate so the moving world does not invalidate static chrome.

Acceptance gate: quiet layout + interaction + emit + paint is at most 2.0
ms/frame on XP, and deterministic hover/menu/chat/resize replays match the
uncached renderer.

### Census: which per-frame tree walks already have a skip gate

Before building invalidation, count what is being walked. Per frame, from one
2000-frame embedded-server run (`uitree_components` puts the tree at **7,142**
components in a logged-in frame):

| walk | visited | skipped | real work | stage cost |
|---|---:|---:|---:|---:|
| layout (`uitree_layout_nodes`) | 7,649 | 7,444 | 205 | 190 ns |
| emit (`uitree_walk_emit`) | 2,840 | 2,204 | 636 | 417,873 ns |
| hit test (`uitree_walk_hit`) | 369 | | | 21,513 ns |
| hover (`uitree_walk_hover`) | 30 | | | (in `interact`) |
| fixed-chrome strip measure | 7,142 | **0** | | 171,851 ns |

Layout is the model to copy: a 97% skip rate reduces a full-tree walk to 190 ns.
Emit is the target 14 problem — it already skips 78% of what it visits and still
costs 418 us, because the 636 nodes it does visit each cost ~147 ns of real
command emission. That needs retained buffers, which is this target.

The last row had no gate at all, and it turned out to be the cheapest thing in
this document to fix.

### Landed ahead of the dependency work: the fixed-chrome measure was the tree's only ungated walk

`App_MeasureRightChromeStripWidth` (`src/app.c:22383`) runs from `window_sync`
every frame in fixed window mode. It looks for one component — the right-docked
popout strip — identified by geometry: fixed-width, parent-height,
right-anchored, and flush to the canvas edge. Almost nothing in a 7,142-node
tree matches that.

It was testing visibility *first*:

```c
if( component_hidden_or_orphaned(app->tree, (int32_t)i) )   /* walks to root */
    continue;
w = c->position.abs_w;
if( w <= 0 || c->position.abs_x <= 0 || ... )               /* one cache line */
    continue;
```

Every test in the loop is an independent reject, so the order was free to
choose — and it had chosen the expensive one. `component_hidden_or_orphaned`
chases `parent` to the root, a fresh cache line per hop, while the geometry
rejects read a record already in cache. Reordering them, and moving the
`w > best` comparison ahead of the walk as well (a candidate that cannot win
does not need to be asked whether it is visible), leaves the result identical
and the call count almost zero.

#### Measured

Four interleaved pairs, both binaries built up front, each carrying the same
diagnostic counters so the arms are comparable:

| pair | `window_sync` mean, before | after | p50 before | p50 after |
|---|---:|---:|---:|---:|
| 1 | 213,782 | 60,280 | 207,200 | 58,200 |
| 2 | 222,167 | 57,431 | 219,900 | 55,500 |
| 3 | 212,161 | 57,992 | 209,000 | 55,600 |
| 4 | 216,295 | 57,593 | 211,100 | 55,200 |
| **mean** | **216,101** | **58,324** | **211,800** | **56,125** |
| | | **-73.0%** | | **-73.5%** |

And the counter that says why: `chrome_strip_vischeck` went from **7,141.2 per
frame to 1.997** — the ancestor walk now runs twice per frame instead of once
per component.

This one is large enough to read on the frame clock directly. Frame p50 fell
6,061,150 -> 5,913,975, **-2.43%**, against -2.60% predicted from the stage
delta, in 4 of 4 pairs — and every `after` run is below every `before` run, so
the arms do not overlap at all. Workload held exactly: 629,744-629,745 host ops
in all eight runs.

Two things to keep straight when reading those absolute numbers. The `before`
arm here (216 us) is higher than the 172 us the uninstrumented probe reported,
because both arms carry a per-component counter increment that the shipped build
does not; the *delta* is what transfers, not the endpoints. And this is not a
CS2 win at all — it is 148 us/frame of UI-tree work, which is why it lands here
rather than in the scorecard's CS2 column.

The per-component counter has been removed from the tree since it only cost
anything in the arm it was measuring. `chrome_strip_vischeck` stays: at 2 per
frame it is free, and it is the regression alarm — if it climbs back toward the
tree size, someone has reordered the loop back.

With that counter gone, the shipped build measures `window_sync` at **42,930 and
42,770 ns** mean over two runs (p50 40,100 / 39,700), against **171,851** (p50
165,800) on the uninstrumented probe that started this — **-75.1%**. Those two
captures are in different batches, so treat the endpoints as corroboration and
the interleaved table above as the measurement; they agree, which is the point.

#### What this does and does not say about the target

It does not build damage tracking, and it does not move the gate's four stages
(layout, interact, emit, paint) at all — `window_sync` is a fifth thing, outside
the gate metric. What it establishes is that the tree is walked more often than
the stage names suggest, and that the census above is worth repeating before the
invalidation work starts: an ungated walk over 7,142 nodes is worth more than
any amount of cleverness inside a walk that is already skipping 97% of them.

### Measuring the prize before building the machinery: the emit list barely moves

The same question that killed target 3 and the target 2 fast path applies here.
Retaining the emit list is only worth building if the list actually repeats, so
measure the repeat rate first.

Every desc is `memset` before it is filled (`uitree_emit.c:194`), so the padding
is deterministic and a byte compare of the descriptor array is well defined. A
temporary diagnostic keeps the previous frame's array, compares, and counts.
Over 1999 measured frames of a logged-in idle capture:

| | count | per frame |
|---|---:|---:|
| `emit_list_same` (byte-identical to the previous frame) | 1994 | 0.997 |
| `emit_list_diff` | 5 | 0.0025 |
| `emit_desc_diff` (individual descs that changed) | 213 | 0.107 |

**99.75% of frames rebuild a list identical to the one they already had**, at
417,873 ns each. That is 6.5% of the p50 frame spent proving nothing changed.

Read the caveat with it: this is an idle capture. The camera does not move and
no entity is near, and several descs are world-derived — the compass rotation is
camera yaw, the minimap anchor is camera position, and the click cross animates
on the world clock. A walking player would move those, so 99.75% is the ceiling
for a quiet frame, not a number to expect while the world is in motion. It is
still the right number for this target, whose whole premise is the *quiet*
gameframe.

### The redraw gate already exists, and two world-clock sites defeat it

`app->need_redraw` is the sole gate on the emit block (`src/app.c:23889`), and it
is cleared at the end of every rebuild. Something sets it again on every frame.
A temporary tally — each of the 108 `app->need_redraw = 1` sites in `app.c`
recorded by `__LINE__`, folded into a run total on the frames whose list came
out identical — names exactly two, and both fire on all 1994 of them:

| site | what it is |
|---|---|
| `app_world_frame` tail | unconditional, after entity/texture animation |
| `app_logic_tick` returned true | per logic tick that did work |

Both are world damage written into the UI flag. That is the conflation this
target's own text calls out — *keep world and UI damage separate so the moving
world does not invalidate static chrome* — sitting in two lines.

What it is not is a two-line fix. The in-tree note at `src/app.c:12113` already
worked out why: the emit walk reads far beyond the component struct — 25 distinct
`UITREE_HOST_GET_*` kinds, plus inventory, hover, drag and varp state — so no
per-node dirty bit can gate it, and a partial version returns a stale panel
rather than a missed one. Deleting those two setters would expose every UI input
that has been riding on a free redraw every frame, and the failure mode is a
frozen panel, not a crash.

So the sound order is: build the dependency signal first, then remove the two
setters, and keep the byte compare above as the acceptance harness — run with the
skip live and the walk still executed into a scratch buffer, and the mismatch
count is the proof. Zero mismatches over a replay is what the target's
"deterministic hover/menu/chat/resize replays match the uncached renderer" gate
asks for, expressed as a number.

### Where the 418 us actually goes: not the descent, and not the host

Retention is a large build, so the two cheaper explanations were priced first and
both came back negative.

**Host round trips are not the prize.** A temporary counter taken as a delta
around the walk (`emit_host_calls`, backed by a `g_uitree_host_calls` tally in
`UITree_Host`) puts the emit walk at **876.67 host calls per frame** against 636
emitted descriptors — about 1.4 apiece. At the ~30 ns a call costs that is 20-26
us of a 418 us stage. Hoisting the invariant ones is real but small; it does not
pay for the machinery it would need.

**Tree descent is not the prize either.** `UITree_EmitWalk` already has a
second pass that descends the whole tree and emits nothing unless a drag is
active, so forcing it on prices a pure-traversal visit directly. With
`if( UITree_HasActiveDrag(tree) )` temporarily read as always-true
(`src/ui/uitree_emit.c:2548`), the walk added 3,490.89 descend-only visits per
frame and zero host calls, and the stage moved 411,242 -> 510,323 ns mean:

| visit kind | ns/visit |
|---|---|
| descend only (bounds + child iteration) | 28.4 |
| full draw pass (411,242 / 2,840.87) | 144.8 |

Read naively that says ~80% of the stage is `UITree_EmitFill` doing its own work
rather than walking to it, which would rule out reordering `UITreeComponent`'s
hot fields or splitting a parallel hot array. **It does not say that** — the
descend-only pass runs second, over a tree the draw pass has just pulled into
cache, so 28.4 ns is a warm-traversal number. See the census below, which
retracts this conclusion.

### The descriptor is 520 bytes, and a counter-attribution trap

`sizeof(struct UITreeEmitDesc)` is **520 bytes**, and `UITree_EmitFill` opened by
clearing all of it — *above* its two early-outs, neither of which reads `out`.
The clear now sits below them, and the host-evaluated `active` (an unconditional
`UITREE_HOST_IS_ACTIVE` round trip) sank with it.

The correctness argument is the part worth keeping: everything between the new
clear position and the type switch writes `out`, so the clear still precedes the
first field written and the untouched tail is still zero — which the
frame-to-frame byte compare above depends on. The sole production caller
(`src/ui/uitree_emit.c:2144`) reads `desc` only inside the branch where the
function returned true, and the four tests that assert a `false` return check the
return value, not the buffer.

**The prize, though, is much smaller than the counters first suggested, and the
reason is a trap worth recording.** `uitree_emit_skip` reads 2,204.80 per frame
against 2,840.87 visits, which invites "the guards reject three of four nodes".
It does not: that counter is incremented at **seven** sites — five of them in the
walk (hidden, sidebar, collapsed layer) that reject a node *before* `EmitFill` is
ever called, and only two inside `EmitFill` itself. The real bound comes from
`emit_host_calls` = **876.67**: `IsActiveHost` fires exactly once per `EmitFill`
entry, so `EmitFill` is entered at most 876 times a frame, not 2,840.

Sinking `active` past the guards then *measures the rejects directly*, because
the round trip now fires only for a node that will draw. Interleaved A/B/A/B,
2000 frames each:

| | clear above guards | clear sunk | delta |
|---|---|---|---|
| `emit` mean ns | 420,352 / 418,335 | 412,730 / 414,059 | **-1.42%** |
| `emit` p50 ns | 389,000 / 386,700 | 385,900 / 383,800 | **-0.77%** |
| `emit_host_calls` | 876.67 | 758.88 | **-117.79** |
| `emit_list_same` | 1994 | 1994 | unchanged |

So the guards reject **117.79 entries per frame**, and dropping their clear and
their host call is worth ~5.9 us of a 419 us stage — about 50 ns per avoided
pair, which is a 520-byte L1-hot clear plus a host round trip, and exactly the
order of magnitude the corrected arithmetic predicted. Both rounds agree in
direction and the between-arm gap clears the within-arm spread (2.0 us on A,
1.3 us on B). It lands because it is free and correct; it is not the prize.

The `emit_list_same` row is the point of the byte compare: 1994 identical frames
in both arms means the change is provably output-neutral, not merely believed to
be.

What the same arithmetic rules *in* is the descriptor's size: 108 bytes of
`debug_skin_atlas` plus 12 of `debug_font_id` serve the single DEBUG_OVERLAY
desc, and 128 bytes of inline `text_formatted` serve only TEXT. Moving the debug
pair behind a host-owned pointer takes 520 -> 400 and shrinks every clear and
every by-value push into the emit buffer by 23%.

### Census: the walk costs 413 us to produce 119 descriptors

That still left ~300 us unaccounted, and the obvious suspects were the five
multi-desc expansion helpers — they turn one visit into many descriptors and
never touch `EmitFill`, so no counter saw them. A stage around all five plus a
per-kind tally of the finished list, taken off `app->emit` once a frame so the
walk itself is untouched:

| | per frame |
|---|---|
| `uitree_walk_emit` (visits) | 2,840.87 |
| **descriptors emitted** | **118.83** |
| — SPRITE / TEXT / RECT / other | 79.89 / 26.96 / 7.99 / 4.00 |
| `emit_expand_calls` | 2.00 |
| `emit_expand` stage | **380 ns** |

Both suspects die here. The expansion helpers cost 380 ns of a 413,025 ns stage
— 0.09%, ruled out. And the list is **118.83 descriptors, not the ~636 assumed
up to this point**: an estimate carried forward from the byte-compare's memory
traffic that was never checked against a counter until now. Every per-descriptor
figure derived from 636 was wrong, and all of them were wrong in the direction of
making descriptor work look like the problem.

What the corrected number says is that descriptor work is *not* the problem at
all. The walk visits 2,840 nodes and emits 119 — **96% of visits produce
nothing** — at 3.5 us per descriptor produced. The cost is per-visit overhead,
which also retires the descriptor-shrink idea above: at 119 descriptors a frame,
taking `UITreeEmitDesc` from 520 bytes to 400 is worth a few microseconds at
most, and it is not worth touching the renderer for.

**And it undermines the 28.4 ns traversal figure.** The descend-only pass runs
immediately after the draw pass, over the same 7,142 components — 5.3 MB at 744
bytes each, well past L2. It therefore measured a *warm* traversal, and 28.4 ns
is a lower bound on what descent costs the draw pass, not an estimate of it. The
conclusion drawn from it — "descent is only 20%, so struct layout is irrelevant"
— does not follow, and the memory-bound hypothesis it was meant to kill is back.

The probe that actually separates the two is to run the identical walk twice, the
second time into a buffer nobody reads (`TORIRS_PERF_EMIT_WARM=1`, stage
`emit_warm`). Same path, same work, only the cache state differs: a warm repeat
much cheaper than the cold original means the walk is bound by pulling the
component array through cache and the fix is the struct layout; two roughly equal
numbers mean it is compute-bound and layout is beside the point.

### The walk is memory-bound: 59% of it is cache misses

| | mean ns | p50 ns |
|---|---|---|
| `emit` (cold, pass 1) | 389,682 | 376,500 |
| `emit_warm` (identical pass 2) | 161,277 | 143,900 |
| warm as a fraction of cold | **41.4%** | **38.2%** |

`uitree_walk_emit` reads 5,681.63 in this run, exactly twice the usual 2,840.87,
which confirms the second pass did the identical traversal and not a shortened
one. Same object code, same tree, same node count, same output discarded — the
only variable is that pass 1 finds the components in DRAM and pass 2 finds them
in L3. **~228 us, 59% of the stage, is the memory system.**

That settles the direction. `UITreeComponent` is 744 bytes and there are 7,142 of
them: **5.31 MB**, an order of magnitude past L2, and the walk chases
`first_child`/`next_sibling` indices through it in an order that has nothing to
do with the array's layout, so every visit is a random 744-byte stride and the
prefetcher gets nothing. The fields the walk actually needs — `type`,
`first_child`, `next_sibling`, `component_id`, `trans`, `if3`, and the four
`abs_*` bounds out of the 76-byte `position` — come to roughly 40 bytes. A
parallel hot array of those is ~286 KB for the whole tree, which fits in L2, and
is an ~18x cut in the traversal working set.

Two routes follow, and they are not exclusive:

1. **Prefetch one link ahead.** The child loop cannot run far ahead of a pointer
   chase, but on loading a child it can immediately prefetch
   `components[child].next_sibling`'s cache line while it does that child's ~145
   ns of work — enough to cover an ~80-100 ns DRAM miss. A handful of lines, no
   structural change, measurable on its own.
2. **The parallel hot array**, which is the real fix and a much larger build:
   the walk reads the small array and touches the 744-byte component only for
   the 119 nodes a frame that actually emit.

Neither competes with retention — retention avoids the walk entirely and is worth
all 390 us, whereas these are worth their share on the frames where a redraw is
genuinely needed. Route 1 is cheap enough to measure before committing to
anything, so it goes first.

#### Route 1 measured: prefetching one sibling ahead is a null

Built both arms and ran them interleaved A/B/A/B, `emit` mean ns:

| arm | run 1 | run 2 | within-arm spread |
|---|---|---|---|
| A (no prefetch) | 420,685 | 419,825 | 860 |
| B (prefetch `next_sibling`'s line) | 413,786 | 419,855 | **6,069** |

B's own spread is nearly twice the 3,434 ns gap between the arms. That is a
null, not a small win, and it was reverted — both the `UITREE_PREFETCH` macro
and the child-loop hunk.

Worth understanding why, because it constrains route 2. Prefetching hides
latency; it does not shrink the footprint. Every line pulled still has to come
from DRAM and still evicts something, so on a walk whose working set is 8x L2
the misses are simply moved, not removed. And the specific line this prefetched
was the wrong one: reading `components[child].next_sibling` to advance the loop
*already* requires the line the prefetch would have covered — offset 20 is on
line 0, the same line `type` and `first_child` sit on — so the prefetch was
issued for a line the very next iteration was going to demand-load anyway,
roughly 145 ns later. The three lines a visit touches beyond line 0 are the ones
that cost, and the loop does not know their addresses any earlier than it uses
them.

So the fix has to be fewer bytes, not earlier bytes. That is route 2.

#### Route 2 measured: packing the hot fields is worth 1.9%, and it refutes the 59%

Before touching anything, the per-visit reads were scattered across four of the
component's twelve cache lines:

| field | old offset | line |
|---|---|---|
| `type`, `parent`, `first_child`, `next_sibling`, `component_id` | 0-48 | 0 |
| `trans`, `if3`, `drag_active` | 96-120 | 1 |
| `behavior.hide` | 176 | 2 |
| `position` hot front (`x`/`y`/`width`/`height`/`layout_resolved`/`abs_*`) | 296-372 | 4-5 |

`UITree_LayoutGetBounds` turned out to read nine fields, not four — `x`, `y`,
`width`, `height` unconditionally and the four `abs_*` plus `layout_resolved`
behind the resolved flag — so position's hot front is 36 bytes, not 16. Three
reorders fix it, none of which changes a single call site (verified: no
positional initializers of `UITreeComponent`, `UITreeElemPosition` or
`UITreeBehavior` anywhere in `src/` or `v1/`, and nothing memcpys, memcmps,
freads or fwrites over them):

- `UITreeElemPosition` leads with `layout_resolved` + `abs_*` + `x`/`y`/`width`/`height`.
- `UITreeBehavior` leads with `hide`.
- `UITreeComponent` opens with a documented hot block: `type`, the three links,
  `component_id`, `trans`, four flag bytes, then `position` — 28 + 36 = exactly
  64 — with `behavior` placed immediately after so `hide` lands at offset 112.

Result: everything the walk reads on every visit is on line 0, plus `hide` on
line 1. Two lines instead of four, and the struct shrank 744 -> 736 bytes as a
bonus (`UITreeBehavior` packed 88 -> 80).

Measured over eight runs, order-balanced A/B/A/B then B/A/A/B because `frame`
mean drifts monotonically downward across a session and a fixed A-then-B order
silently credits that drift to B:

| arm | emit mean | within-arm spread | emit p50 | spread |
|---|---|---|---|---|
| A (base) | 416,994 | 3,027 | 388,175 | 3,200 |
| B (packed) | 409,213 | 13,238 | 382,150 | 16,900 |

**-1.87% mean, -1.55% p50.** B's spread is uncomfortably wide, but the effect is
order-consistent — 15 of the 16 pairwise A/B comparisons favour B, and A's own
spread is tight — so this is a small real win rather than a null. It lands:
`uitree_walk_emit` (2,840.87), `emit_host_calls` (758.88) and `emit_list_same`
(1994) are identical to every digit across all eight runs, so the walk provably
visits the same nodes and produces the same list. All ten `test-cs2-*` targets
pass.

**But the important result is the one that disappoints.** Halving the lines
pulled per visit should have recovered a large share of the 228 us the warm
probe attributed to cache misses. It recovered about 8 us — roughly 3% of it.
That is strong evidence the attribution was wrong: the warm walk was cheap
because running it once warms *everything* it touches — the host state, the
descriptor buffer, the sprite and font metadata, the branch predictors, the
instruction cache — and I charged the whole 59% to the component array on no
more than the array being the largest thing in sight. The component array is
worth ~2%, not ~59%.

Route 2 is the cheap version of route 3's hypothesis, and it returned a
thirtieth of what that hypothesis predicts. **Route 3 (the parallel hot array)
is therefore dropped, not deferred** — it is a large, invasive build resting on
an attribution this measurement just refuted, and the reorder has already
collected the part of the win that was actually there.

That leaves retention as the only thing on this target still worth its cost, and
it always was: it avoids the walk rather than making the walk cheaper, so it is
worth the whole ~390 us on the 1994 frames in 2000 where the emit list does not
change. The remaining sub-optimisations of the walk itself are exhausted.

### Retention, step 1: the tree's damage bit answers the wrong question

The dependency signal turned out to be half-built already. `UITree_MarkNodeDirty`
exists and is called from 54 sites across the CS2 host's `cc_*` ops, clientcode,
gameproto, the anim tick and `app.c` — but **nothing outside the tests consumes
it**. There is no non-test caller of `UITree_NodeNeedsEmit` or
`UITree_ClearNodeDirty`. The marks are maintained and unread, which also means
unvalidated: a missing mark has no symptom today, so completeness could not be
assumed.

So it was measured rather than trusted. `struct UITree` gained a `dirty_gen`,
bumped at all 13 sites in `uitree.c` that raise `is_dirty` (the 54 external
callers all funnel through those). `generation` was not reusable — it tracks
topology only, so a `cc_settext` that rewrites a label leaves it untouched. The
counter is deliberately conservative: a write of the same value still bumps,
because over-counting costs a missed skip while under-counting serves a stale
list and freezes a panel.

That signal then ran in **shadow mode** — the gate decides whether the walk
*could* have been skipped, the walk runs regardless, and the verdict is checked
against the byte compare. Two counters come out of it: `emit_gen_quiet` (frames
the gate called quiet whose list did repeat — the prize) and `emit_gen_unsound`
(frames it called quiet whose list moved anyway — skips that would have shown
stale chrome). Over 2,000 frames:

| counter | per frame | total |
|---|---|---|
| `emit_list_same` | 0.997 | 1994 |
| `emit_dirty_bumps` | 50.30 | 100,602 |
| `emit_gen_quiet` | **0.000** | **0** |
| `emit_gen_unsound` | 0.000 | 0 |

The signal never lies — but it never fires either. Fifty dirty marks land on
every single frame, so the gate is defeated on all 2,000. All three per-frame
markers — `uitree_anim.c:109`, `rs_clientcode.c:335`, `app.c:17809` — guard on
`UIELEM_RS_MODEL` and fire because a 3D model widget's rotation advanced.

The obvious reading is that these are repaint damage rather than emit damage.
**That reading is wrong, and it is worth recording why**, because it was written
here before it was checked: `struct UITreeEmitDesc` carries `model_xan`,
`model_yan` and `model_zan`, filled straight from `component->u.rs_model` at
`uitree_emit.c:507`. A rotating model that reaches the walk *does* change its
descriptor. If these marks were on emitted nodes the list could not be
byte-identical, so the marks must be landing somewhere the walk never gets to.

A first probe tested the marked node's own `behavior.hide` and found only 9.05
of the 50.30. That probe was too narrow — `UITree_ComponentOrAncestorHidden`
exists precisely because a node can have a clear hide bit inside a closed
container. Re-probed against ancestors:

| | per frame |
|---|---|
| bumps the walk cannot reach (self or ancestor hidden) | **32.29** |
| bumps on reachable nodes | 18.01 |
| `emit_gen_quiet` | 0 |
| `emit_gen_unsound` | 0 |

Two-thirds of every frame's UI damage is closed interfaces ticking their 3D
models — the equipment tab's player figure and the character-design preview
keep rotating behind a shut panel. Those are free to ignore, and a
"visited during the last walk" bitmap ignores them soundly: a node the walk did
not reach can only become reachable if some ancestor changed, and that ancestor
is itself marked.

That still leaves 18.01 marks a frame on reachable nodes with a byte-identical
list, and **that gap is not yet explained**. Reachable is not the same as
emitted — `UITree_ComponentShouldEmit`, the `trans >= 255` guard, the deferred
drag pass and the mount sweeps all reject reached nodes — and `dirty_gen` is
deliberately conservative enough to bump on a write of an unchanged value. Which
of those accounts for the 18 decides step 2, and guessing at it is what produced
the wrong conclusion above.

So step 2 is: have the walk record the set of nodes that actually produced a
descriptor, and re-run the same shadow measurement against that set instead of
against `dirty_gen` alone. That is a direct measurement rather than an
inference, it subsumes the visited-bitmap idea, and it answers the 18 rather
than theorising about it.

The infrastructure is in the tree now and is cheap — one `uint32_t` increment on
paths that were already writing a dirty byte. The shadow harness stays:
`emit_gen_unsound == 0` over a replay is the acceptance gate this target asks
for, expressed as a number, and it must be re-checked before the skip is ever
turned on. It is currently 0 across 2,000 frames, which says the signal is
sound; it is `emit_gen_quiet` at 0 that says it is not yet useful.

### Retention, step 2: reachability filtering, and six unsound hide writers

`UITree::emit_visited` is one byte per node, set by `emit_walk_node` for every
node the walk enters, cleared at the head of each walk. `UITree_MarkNodeDirty`
now skips the `dirty_gen` bump when the last walk never entered the node. The
`is_dirty` byte is still written unconditionally — it is the node's repaint bit
and the caller is right about it either way; only the retention generation is
filtered.

Placing the write correctly took two tries and the difference is instructive.
Set *before* the own-hide reject it filtered 11.24 marks/frame; set *after* it,
11.51. Both far below the 32.29 the hidden-ancestor probe predicted, because a
node with a hidden *ancestor* is never entered at all — the ancestor returns
before descending — so those were already being filtered and the earlier probe
was double-counting them against a filter that had them covered.

**Finding a genuine correctness hole.** Making the filter safe required auditing
every writer of `behavior.hide`, and six of them never marked the node at all:
`task_interface_open.c` (both mount sweeps), `uitree_builder_bake.c:956`,
`task_cs2_run.c:440`, `uitree.c` (the equipment-slot unhide), and the
`uitree_obj_cell.c` hide swap. Two more — `UITree_SetHide` and the obj-cell swap
— marked only through `MarkNodeDirty`, which is worse than it looks:

> **Every write to a `hide` bit must bump `dirty_gen` directly, never through
> `UITree_MarkNodeDirty`.** That function's bump is filtered by whether the last
> walk reached the node, which is precisely what a hide write changes. An unhide
> would be discarded as "unreachable" on the strength of the reachability it is
> abolishing, and the gate would call the frame quiet while a panel opened.

All eight now bump unconditionally. This is a latent bug fixed, not a
micro-optimisation: `dirty_gen` was unsound before the filter existed, and the
2,000-frame shadow run read `emit_gen_unsound = 0` only because no interface
opens or closes occur in that replay. Any dirty-tracking scheme layered on the
tree would have hit the same hole.

**The model-rotation hypothesis was wrong.** The natural reading of "50 marks a
frame, all three per-frame markers guard on `UIELEM_RS_MODEL`" is that rotating
3D widgets dominate. Probing the surviving marks by component type:

| | per frame |
|---|---|
| `emit_dirty_bumps` (after filtering) | 45.87 |
| of which `UIELEM_RS_MODEL` | **0.00** |
| `emit_dirty_unreached` (filtered out) | 11.51 |
| `emit_gen_quiet` / `emit_gen_unsound` | 0 / 0 |

Not one surviving mark is a model. Every model mark is already filtered — the
rotating figures are all behind closed panels — and the three per-frame model
markers are individually well behaved besides: `rs_clientcode.c:332` and
`app.c:17805` both compare before marking, and `uitree_anim.c:102` fires only
when a rotation speed is nonzero *and* cycles have elapsed.

The bumps went *up* from 39.06 to 45.87 across this step, which is the six new
unconditional hide bumps showing up as ~6.8/frame of real UI activity that the
signal was previously blind to.

**What the 34 actually are.** They are ordinary property writes. Per frame the
tree takes 15.17 geometry applies, 16.69 content applies and 15.81 other
applies that all report themselves as changes, against 16.59 that a per-setter
no-change fast path already catches (`uitree_apply_nochange`). So the tree
*has* compare-before-mark — `UITree_SetHide` does it, `set_node_text` does it,
`design_gender_button_tick` does it — it just is not applied uniformly across
the `cc_*` setters in `rs_cs2_host.c`. A CS2 script that rewrites the same
colour or the same position every frame marks the node every frame.

### Retention, step 3: compare-before-mark is not where the marks are either

Seven `cc_*` handlers in `rs_cs2_host.c` wrote a field and marked
unconditionally — `cc_setfill`, `cc_settrans`, `cc_setnoclickthrough`,
`cc_setdraggable`, `cc_setdraggablebehavior`, `cc_setdragdeadzone`,
`cc_setdragdeadtime`. All seven now compare first, matching what
`UITree_SetHide` and `set_node_text` already do. This is worth keeping on its
own terms — an unconditional mark is a repaint the client does not need — but
as a fix for the gate it is a rounding error:

| | before | after |
|---|---|---|
| `emit_dirty_bumps` | 45.87 | **45.35** |
| `emit_dirty_unreached` | 11.51 | 11.43 |
| `emit_gen_quiet` | 0 | 0 |

Half a mark per frame. The prediction that these setters held the bulk was
wrong, and the counters that would have shown it were already on screen:
`uitree_apply_geo/content/other` did not move at all, because they count a
different path. The ~47.7 applies a frame go through `UITree_Apply*`, which
raises `is_dirty` and bumps `dirty_gen` at thirteen sites *inside* `uitree.c`
— and those thirteen are exactly the ones step 2 deliberately left unfiltered.

So the reachability filter is currently doing nothing for the traffic that
matters. It is installed on `UITree_MarkNodeDirty`, which the per-frame load
barely uses. The exemption was justified as "these are the sites that move hide
bits and topology, and filtering them would break the soundness argument" — but
that reasoning only ever applied to the hide and topology writers, and those
now bump directly and unconditionally anyway (step 2). A geometry or content
apply on a node the last walk never entered is as safely ignorable as any other.

### Retention, step 4: stop guessing, attribute the bumps

Two more predictions died before this one was measured properly, and both are
worth keeping as warnings.

First, `UITree_MarkFrameAlwaysDirtyTypes` — which runs per frame over exactly
the "always repaints" node types (compass, cross, minimenu, hovertext) and
bumped per matching node — looked like the obvious culprit. Removing its bump
changed the total from 45.3535 to 45.3535, identical to the digit. It
contributes nothing in this replay. The bump is still gone, because raising a
node's always-repaint flag genuinely says nothing about its emit descriptor,
but it was not the problem.

Second, the step-3 note above predicted the traffic was in "geometry, content
and other apply sites inside `uitree.c`". It is not. Listing the enclosing
function of all thirteen inline bump sites shows every one of them is
creation, linking or deletion — `push_element_unlinked`, `UITree_Push` (×2),
`UITree_UnlinkChild`, `UITree_Reparent` (×2), `UITree_ClearChildren`,
`UITree_CcCopy`, `UITree_CcDelete`, `UITree_CcDeleteAll`, plus the two blanket
marks. There were no property applies among them to reroute. The `UITree_Apply*`
counters had not moved in step 3 for the plain reason that they never bumped
`dirty_gen` directly at all — they go through `UITree_MarkNodeDirty`.

Also fixed here: `UITree_MarkAllDirty`'s loop is braceless, so the bump appended
to it during step 1 landed *outside* the loop. That happens to be the correct
semantics — a blanket mark is one change to a tree-level generation, not 7,142 —
but it was right by accident and read as loop body. Braces and a comment now.

Attributing the bumps at their two real sources instead of predicting them:

| source | per frame |
|---|---|
| `emit_dirty_mark` — `MarkNodeDirty`, passing the reachability filter | **27.43** |
| `emit_dirty_topo` — the thirteen create/link/delete sites | **21.92** |
| `emit_dirty_unreached` — marks filtered out | 11.43 |

So the steady-state client **creates or relinks components ~22 times and writes
~27 properties to reachable nodes every frame, and produces a byte-identical
emit list**. With `uitree_cc_create` at 7.85/frame, a single create is bumping
about 2.8 times as it is pushed, linked and laid out.

That reframes the target. The retention gate is not failing because the signal
is too coarse; it is reporting, correctly, that the UI really does rebuild
itself every frame and arrive at the same answer. Making the gate fire by
loosening the signal would be papering over that. The work is to stop the
redundant rebuild — which is Opt 14's territory and worth more than the skip
the gate was going to enable, since it is CS2 execution, allocation, linking
and layout, not just the emit walk.

**Next step:** find which script(s) account for the 7.85 creates and 22 link
bumps per frame — `cs2_scripts` is 17.6/frame, so it is a small set — and
determine whether they are rebuilding a list whose contents did not change. If
so the fix is at the script-host boundary (reuse the existing children when the
inputs match), not in the dirty signal.

The acceptance condition for the skip is unchanged and non-negotiable:
`emit_gen_quiet` must rise **while `emit_gen_unsound` stays pinned at 0** over a
full replay. It is still 0 / 0.

The pattern across all four steps deserves stating plainly, because it caught me
four times: every hypothesis about *where* the marks came from — repaint-vs-emit
damage, model rotation, non-comparing `cc_*` setters, always-dirty frame types —
was wrong, and each was refuted in minutes by a counter. The one that finally
worked attributed the bumps at their sites instead of reasoning about which
sites looked likely. The dirty-tracking work has produced two real correctness
fixes and no measured speedup; it should not be called a win until
`emit_gen_quiet` is nonzero.

Cost of the machinery so far, measured over 2,000 frames: emit mean 414,796 /
p50 382,500 against 406,148 / 375,900 before, i.e. the shadow harness itself
costs ~2% of the stage. It is instrumentation, not the feature, and comes out
with the counters.

## Optimization 12 - eliminate framebuffer composition and present copies

Measure Soft3D raster, world/UI composition, format conversion, scaling,
`BitBlt`/`StretchDIBits`, and present separately. Prefer a persistent native
32-bit framebuffer/DIB section and render directly into the format consumed by
the Windows presenter. Avoid full-frame conversion or an extra copy.

Acceptance gate: the OS-present-excluded world number is unchanged while
composition/present CPU and memory bandwidth fall. Validate both fixed and
resizable modes on XP.

### The premise does not hold on the Windows lane

This target assumes there is a conversion or a composition copy to remove. On
`platform_win32gdi.c` there is neither, and has not been:

- `PlatformSDL2_Pixels` returns the pointer `CreateDIBSection` handed back
  (`platform_win32gdi.c:134`). `App_Render` rasterises straight into the DIB
  bits. There is no staging buffer.
- The DIB is `BI_RGB` 32bpp top-down, which on a little-endian box is
  byte-order BGRA / word-order ARGB — bit-identical to the `ARGB8888` the
  painter writes. There is no per-pixel conversion, and the file's header
  comment already said so.
- Present is one `BitBlt` of that DIB. World and UI are not separate layers
  being composed; they are one raster target both write into.

So "prefer a persistent native 32-bit framebuffer/DIB section and render
directly into the format consumed by the Windows presenter" describes what the
lane already does. What was left to do was measure the presenter and find out
whether anything in it is worth removing.

#### Measured: where the present stage actually goes

Two nested stages (`present_fill`, `present_blit`) and four counters, over one
2000-frame embedded-server run:

| | ns/frame | share of `present` |
|---|---:|---:|
| `present` | 158,990 | |
| `present_blit` | 134,182 | 84.4% |
| `present_fill` (letterbox bars) | 256 | 0.2% |
| residual — `GetDC` / `GetClientRect` / `ReleaseDC` | 24,552 | 15.4% |

`present` is **1.87% of the frame** at the mean and 2.6% against the p50 frame,
so the ceiling on this whole target is under three points before anything is
attempted.

The counters say the rest:

- `present_blit_1to1` = 2002, `present_blit_stretch` = **0**. Every present in
  the run was a 1:1 `BitBlt`; the `StretchBlt`/`HALFTONE` path never executed.
  Its cost is not part of the steady state and tuning it would measure nothing.
- `present_blit_pixels` / 2002 = **405,847 px/frame** = 1.62 MB, moved in
  134 us. That is **12.1 GB/s** — memcpy speed on this machine. GDI is not
  adding overhead per present; it is moving the frame as fast as the memory
  system allows, and no change to *how* it is blitted can beat that.
- `present_fill_pixels` totalled 21,126 for the run, all of it in the first
  window. Once the window settles, the letterbox rects are empty and the four
  `gdi_fill_black` calls return on their first comparison. The 256 ns charged
  to `present_fill` is the stage's own pair of `clock_gettime` calls, not work.

That leaves exactly one removable item: the 24.6 us the frame spends asking
user32 for a DC and giving it back.

#### Landed: the window DC is fetched once, not per frame

The window class has carried `CS_OWNDC` since it was written
(`platform_win32gdi.c:571`), which means the DC is permanently associated with
the window and survives resizes — the per-frame `GetDC`/`ReleaseDC` pair was
buying nothing. `struct PlatformSDL2` gained a `window_dc` field, the init path
fetches it once (reusing it for the `CreateCompatibleDC` that makes `mem_dc`,
so this replaces a call rather than adding one), `PlatformSDL2_Present` uses it
directly, and `PlatformSDL2_Free` releases it before `DestroyWindow`. The
`WM_PAINT` handler keeps using `BeginPaint`'s DC, which is a different handle
with a different clip region and is not interchangeable with this one.

Four interleaved pairs, both binaries built up front:

| pair | `present` mean, GetDC | cached | `present` p50, GetDC | cached |
|---|---:|---:|---:|---:|
| 1 | 159,543 | 144,629 | 160,100 | 144,500 |
| 2 | 162,350 | 142,102 | 160,000 | 141,700 |
| 3 | 160,636 | 145,970 | 160,200 | 145,500 |
| 4 | 167,138 | 141,983 | 161,500 | 141,800 |
| **mean** | **162,417** | **143,671** | **160,450** | **143,375** |
| | | **-11.5%** | | **-10.6%** |

Same sign in 4 of 4 pairs on both statistics, and the spread inside each arm is
under 4% against an 11.5% effect. Workload held: 629,7xx-629,8xx host ops in
every run, the usual bimodal pair, 0.02% apart.

The split says where it went, and it is not a clean subtraction:

| | GetDC | cached | delta |
|---|---:|---:|---:|
| residual (DC round trip) | 25,784 | 1,015 | **-96.1%** |
| `present_blit` | 136,408 | 142,469 | **+4.4%** |
| `present` | 162,417 | 143,671 | -11.5% |

The DC round trip is gone — 1,015 ns is the two nested stages' own timer
overhead, i.e. the residual is now empty. But `BitBlt` got 6.1 us slower, every
pair, consistently. **Why is not established.** The plausible reading is that
some validation GDI did at `GetDC` time now happens inside the blit, so part of
the cost moved rather than vanished; that is a guess, and the honest statement
is that 24.8 us of DC calls bought back 18.7 us of frame time. Do not quote the
residual delta as the win — the stage total is the win.

Against the whole frame this is 18.7 us on a 5.97 ms p50, **0.31%**, and frame
p50 moved -0.26% (5,969,425 -> 5,954,025), the right direction and roughly the
predicted size, in 3 of 4 pairs. As with target 2, the stage clock is the
measurement here and the frame number is only corroboration.

#### What is left, and where it belongs

Nothing else in this presenter is worth attacking. The blit is at memory
bandwidth, the fill is empty, the scaler never runs, and the DC calls are gone.
The one structural win remaining is **not presenting a frame that did not
change** — 2002 blits over 2000 frames means the client redraws unconditionally.
That is 1.62 MB/frame of pure bandwidth spent to show identical pixels, and on
XP it is the same 405,847 pixels against a far weaker memory system.

That win is not this target's to take: knowing a frame is unchanged requires the
damage tracking Optimization 11 is defined to build. Recorded here so the reader
who arrives at target 11 knows present is one of its beneficiaries, alongside
target 14's emit walk.

#### The SDL lane is where this target's premise is true

`platform_sdl2.c:1888` does what this target was written about: a row-by-row
`memcpy` of the whole frame into a locked SDL streaming texture, then
`SDL_RenderCopy` + `SDL_RenderPresent`. That is a real extra full-frame copy,
1.62 MB per frame on top of the present itself, and removing it means rendering
directly into the locked texture.

It is out of scope for the numbers in this document: the win32/win64 lanes build
`platform_win32gdi.c` instead (`src/platform/platform.mk` sets
`PLATFORM_WINDOW_SRC`), so `platform_sdl2.c` is not in the XP artifact or in any
measurement here. It matters for macOS and Linux, and the acceptance gate above
should be read as already met on Windows and untested elsewhere.

## Optimization 13 - native compilation for the remaining hot scripts

If targets 1-10 cannot reach the CS2 budget, add a tier for a small measured
hot set. Ahead-of-time compile supported, non-yielding CS2 scripts into native C
functions with the typed host ABI. Key each function to a cache/script
fingerprint and fall back to the interpreter for unsupported bytecode, a cache
mismatch, or a yield.

This is intentionally last: native dispatch cannot remove redundant scripts,
host work, UI invalidation, or framebuffer copies.

Acceptance gate: the selected scripts are substantially faster end to end,
including host time, while differential traces and fallback behavior remain
identical.

## Optimization 14 - make the settle loop pay for what it uses

Runs after target 5 in practice (see the ordering note at the top); it is
numbered here to avoid renumbering the established backlog.

`app_settle_cs2_frame` (`src/app.c:10811`) alternates
`TaskRunner_SettleFrame` with a **full-tree `UITree_LayoutResolve` every
iteration** until the task queue idles. Steady state measures 9.16 ms/frame
in `cs2_settle` — 39% of the CS2 stage — with 6.55 full-tree resolves and
23,527 layout-node visits per frame over a ~7,000-component tree, 99.9% of
them skips. None of this appears in the near-zero `layout` stage; it all
bills to `cs2`.

Much of the settle time is expected to be followup-script invocation
overhead that targets 1–3 already remove, which is why Gate 0 item 6 must
split task-pump time from layout time before this target is scoped. Then:

- resolve layout only when the preceding settle iteration actually dirtied a
  layout input, not unconditionally once per iteration;
- resolve the dirty subtree instead of the root once target 11's damage
  tracking exists;
- keep the settle loop's fixed-point semantics: followup order, final
  layout, and frame hashes must be identical.

Acceptance gate: `cs2_settle` falls with identical settle convergence, and
layout resolves per frame equal the number of dirtying iterations rather
than the number of iterations.

### The layout half of this target is already satisfied — do not implement it

Re-measured after targets 1–6, 10 and 11-route-2 landed (2,000 frames,
`EMBED_SERVER=1`):

| | target-14 baseline | now |
|---|---|---|
| `cs2_settle` | 9.16 ms | **270 µs** |
| `cs2_settle_layout` | — (unsplit) | **66 ns** |
| `uitree_layout_resolve` /frame | 6.55 | **2.16** (1.08 of them skips) |
| `uitree_layout_nodes` /frame | 23,527 | 7,649 |

Gate 0 item 6 asked for the task-pump/layout split before this target was
scoped, and now that the split exists it answers the question outright:
**66 ns**. Layout inside the settle loop is not a cost any more. `cs2_settle` is
still 58% of the CS2 stage, but essentially all of it is the task pump, so
"resolve layout only when an iteration dirtied a layout input" would optimise
something that already rounds to zero. Implementing the first bullet as written
would be motion without movement.

What the same numbers *do* show is a full-tree layout walk running ~1.08 times a
frame, visiting 7,649 nodes to skip 7,444 of them (97.3%). That is the identical
shape as the emit-walk problem in target 11 — traverse seven thousand nodes to
discover nothing changed — and it wants the same fix (the second bullet: resolve
the dirty subtree, not the root). It is not, however, where `cs2_settle`'s 270 µs
went.

**Re-scope, don't re-implement:** the live parts of this target are (a) the
subtree-resolve in bullet 2, and (b) the emit-walk half below. Bullet 1 is
closed by targets 1–6. The task pump itself is the remaining ~284 µs, and the
next section says what is and is not inside it.

### The settle loop is the largest CS2 cost, and it is not scripts

`cs2_host_op` was added to time every `RS_CS2Host_Exec` dispatch (depth-guarded,
so re-entrant handlers bill once). Same 2,000-frame capture:

| stage | mean | share of `cs2` |
|---|---|---|
| `cs2` | 483,370 ns | 100% |
| `cs2_settle` | 284,174 ns | 58.8% |
| `cs2_host_op` | 46,721 ns | **9.7%** |
| `cs2_settle_layout` | 64 ns | 0.01% |

314.87 host ops a frame at **148 ns each** — an unremarkable number, and the
fifth consecutive hypothesis about where this client's UI time goes to die on
contact with a counter. Now total the parts of `cs2` that are actually
attributed:

- host ops: 46.7 µs (measured)
- VM acquire + init + release: 2.8 µs (measured, 0.6%)
- 2,164 opcodes at any defensible interpreter cost: tens of µs
- layout inside settle: 64 ns

That is well under a third of 483 µs, and `cs2_settle` alone is 284 µs with at
most 46.7 µs of host work and 64 ns of layout inside it. **The settle loop's own
iteration is the single largest CS2 cost in the client** — not script dispatch,
not host effects, not layout.

Two consequences, both of which change the backlog:

The obvious reading of that — "17.6 scripts cannot fill 284 µs, so the loop must
be rescanning something" — was the sixth wrong guess, and the counters killed it
in one run. See below before acting on it.

### The pump is thin. The interpreter is the cost. Target 8 is back on.

Splitting the settle loop's three limbs and counting its iterations:

| | value |
|---|---|
| `cs2_settle` | 282,789 ns |
| `cs2_settle_layout` | 65 ns |
| `cs2_settle_followups` | 276 ns |
| `task_steps` /frame | **4.85** |
| `cs2_host_op` | 46,706 ns |

Both auxiliary limbs are noise, so essentially all of `cs2_settle` is inside
`ToriRS_TaskQueue_Run` — which holds the scheduler *and* the VM. 4.85 steps a
frame settles which: **the scheduler is not being rescanned.** There is no
task-list sweep to remove. 282.8 µs over 4.85 steps is 58 µs of work per step,
with only 17.6 scripts inside.

Subtract the measured host work and the remainder is the interpreter:

```
282.8 us settle - 46.7 us host ops = ~236 us for 2,164 opcodes
                                   = ~109 ns per opcode
```

**109 ns/opcode is 5–20x what a competent bytecode dispatch loop costs**, and
that is the number target 8 exists to attack. Its own caveat — "per-invocation
overhead dwarfs dispatch" — was written against the pre-targets-1–6 baseline and
is now falsified twice over: per-invocation setup is measured at 2.8 µs/frame
(0.6%), and there are only 17.6 invocations against 2,164 opcodes.

So the correct reading reverses the one above it, which is left in place
deliberately rather than deleted:

1. **Target 8 is the next move**, and for the first time on evidence rather than
   on the theory that interpreters are slow. The 109 ns is not pure dispatch —
   nothing dispatches that slowly — so the win is in what target 8 already
   specifies resolving once at load: dialect selection, `CAN_YIELD`, stack
   effects, operand decode, per-opcode branching that a predecoded form folds
   away. Instrument one hot script's opcode mix before writing the predecoder,
   so the micro-op set is chosen from this client's actual distribution.
2. **Target 9 follows target 8** as written, and its dependency is real.
3. **Target 14's remaining work is the emit half only.** Its layout bullet is
   closed above, and the pump bullet is closed here — there is nothing in the
   settle loop to restructure.

This document has now been wrong six times running about where cost lives, and
right every time it counted instead. The pattern is not incidental: every wrong
guess was plausible, and each cost one build and one 2.5-minute capture to
disprove. Counting is cheaper than arguing.

#### The 109 ns is inside the switch, not around it

Read before micro-optimising `cs2vm2_run_script_body` — its loop body is already
lean, and all three candidates a reader reaches for first are already dead:

- `CS2VM2_SaveYieldCheckpoint` per opcode is **six scalar stores** and no frame
  copy (the yield contract makes restore a pointer rollback).
- `CS2VM2_TraceOpcode` is behind `CS2VM2_TRACE_ARMED()`, and the comment at that
  gate records it as measured-worthless on win64 — kept for i686 cdecl only.
- `CS2VM2_DebugPrintOpCode` is entirely `#if CS2VM2_DEBUG_OPS`; it compiles to
  nothing.

What is left per opcode is the dispatch into `CS2VM2_RunOp` and the work inside
it. I guessed the work was inside the handlers. **That was the seventh wrong
guess, and the histogram below refutes it in one run.**

#### The opcode histogram: 80% of traffic is push, pop, and branch

`TORIRS_CS2_PROFILE=1` now also dumps an opcode histogram (same gate as the
per-script profile, counts rather than times — timing individual ops needs two
clock reads each and would cost more than the thing measured). 1,200 frames,
3,409,128 opcodes executed:

| op | name | share | cumulative |
|---|---|---|---|
| 0 | `PUSH_CONSTANT_INT` | 26.76% | 26.76% |
| 33 | `PUSH_INT_LOCAL` | 21.19% | 47.95% |
| 34 | `POP_INT_LOCAL` | 9.37% | 57.32% |
| 6 | `BRANCH` | 6.25% | 63.57% |
| 8 | `BRANCH_EQUALS` | 5.50% | 69.07% |
| 21 | `RETURN` | 3.05% | 72.12% |
| 40 | `GOSUB_WITH_PARAMS` | 2.48% | 74.60% |
| 42 | `PUSH_VARC_INT` | 2.18% | 76.79% |
| 3 | `PUSH_CONSTANT_STRING` | 1.94% | 78.72% |
| 7 | `BRANCH_NOT` | 1.56% | **80.29%** |

**Ten opcodes are 80% of the traffic, and every one of them is trivial.**
Push a constant, load a local, store a local, take a branch — one to three
instructions of actual work each. 57% of all traffic is just the first three.

That is the whole argument, and it inverts the paragraph above: if 80% of
executed ops do ~2 instructions of work and the measured mean is ~109 ns, then
essentially none of the 109 ns is handler work. **It is the dispatch.** Per
opcode the interpreter currently pays a five-argument call into `CS2VM2_RunOp`
plus a switch over an opcode space that runs 0…7502 in sparse clusters, which no
compiler turns into one jump table — it becomes a comparison tree, and the hot
ops pay it to reach a two-instruction body.

Consequences for target 8, now fully evidence-backed:

1. **Its "generated handler table or computed-goto backend" clause is the whole
   win, not a detail.** Dense-index the opcode at load, dispatch through a flat
   table, and inline the top ten as micro-ops in the loop body. The predecode of
   operands, dialect and `CAN_YIELD` matters less than killing the call and the
   comparison tree.
2. **The micro-op set is decided.** Ten ops for 80%, twenty for ~90%. This is
   the concentrated distribution superinstructions want, so target 9's premise
   holds too — `PUSH_CONSTANT_INT` followed by a compare/branch is visibly the
   pair to fuse first.
3. **Do not start with operand predecode.** It is the part of target 8 that
   addresses handler-side cost, and handler-side cost is what this table just
   ruled out.

Seven wrong guesses now, every one killed by a counter that took one build and
one capture. The instrument is in the tree; use it before the next hypothesis.

#### The fast path that changed nothing — and why target 8 is now in doubt

The obvious first move fell out of the table above, so it was built: an inline
fast path in `cs2vm2_run_script_body` for `PUSH_CONSTANT_INT`,
`PUSH_INT_LOCAL` and `POP_INT_LOCAL` — **57.3% of all executed opcodes** —
handling them in the loop body and skipping the call into `CS2VM2_RunOp`
entirely. It was correct (10/10 CS2 tests, `test-cs2-math` included) and it was
worth nothing:

| | cs2 p50 |
|---|---|
| baseline band, 3 runs | 326.1 / 335.2 / 350.5 µs |
| fast path, 2 runs | 328.8 / 338.6 µs |

Dead centre of the noise. **Reverted** — unmeasured complexity duplicating op
semantics in a VM core is exactly what this document's discipline exists to keep
out. Only the comment marking the dead end remains.

The reason is in the histogram, read more carefully than the first time: every
opcode in the top ten is **below 64**. That is a dense cluster, so GCC already
lowers it to a jump table, and the handlers are small `static` functions it had
already inlined. There was never any dispatch overhead on the hot path to
remove. The sparse 1000/4000/7502 clusters pay the comparison tree, but they are
the tail, not the traffic.

**This makes the eighth wrong guess, and the first one that should change the
plan rather than just redirect it.** The chain now reads: the ~109 ns/opcode is
not handler work (the hot ops are 3–4 instructions), and not dispatch (removing
it changed nothing). Both halves of target 8's thesis have now been measured and
both are empty. Before writing a predecoder, someone has to explain where
109 ns/opcode actually goes, because the two mechanisms target 8 proposes to fix
are both accounted for and neither is it.

Concrete next step, and it is a measurement, not a build: the ~236 µs figure is
a *subtraction* (`cs2_settle` minus host ops), and every term in it has been
verified except the assumption that the remainder is opcode execution at all.
Time `CS2VM2_RunScript` directly — 17.6 calls a frame is cheap enough to bracket
with real clock reads, unlike per-opcode timing. If it does not come back near
236 µs, the opcodes were never the cost and targets 8, 9 and 13 are all aimed at
the wrong thing.

### The VM was never the problem. Targets 8, 9 and 13 are retired.

That measurement was taken. `TORIRS_CS2_PROFILE=1` now reports a grand total
across every profile row — the direct cost of `CS2VM2_RunScript`, bracketed with
real clock reads:

```
=== cs2 RunScript total: 199.745 ms over 35217 calls, 309 distinct scripts ===
```

2,000 frames, so **99.9 µs/frame**, not 236. (35,217 / 2,000 = 17.6 calls a
frame, matching `cs2_scripts` exactly — the measurement is sound.)

The ~236 µs was wrong in a specific and instructive way: **host ops execute
inside `RunScript`**, so subtracting them from `cs2_settle` double-counted. With
the nesting respected, CS2 finally decomposes:

| | per frame | |
|---|---|---|
| `cs2_settle` | 283 µs | |
| └ `CS2VM2_RunScript` | **99.9 µs** | measured directly |
| &nbsp;&nbsp;└ host ops | 46.7 µs | measured directly |
| &nbsp;&nbsp;└ opcode execution | ~53 µs | remainder, 2,164 ops |
| **task machinery outside the VM** | **~183 µs** | remainder |

**~53 µs over 2,164 opcodes is ~24.6 ns/opcode** — an ordinary bytecode
interpreter, in the normal band, with nothing anomalous to recover. The 109 ns
figure that motivated this entire line of work was an artifact of the
double-counted subtraction, and every conclusion drawn from it (including the
fast path above, which is now explained: it measured neutral because there was
never anything there) was chasing a number that did not exist.

**Retire targets 8, 9 and 13.** All three attack opcode execution. Opcode
execution is 53 µs/frame — 11% of the `cs2` stage — and already efficient. A
perfect predecoder, a full superinstruction pass and a native compiler are
bidding for a slice that cannot repay any of them, and target 13 in particular
would be a large permanent complexity cost against ~53 µs of already-healthy
work. If they are ever revisited it must be on a fresh measurement, not on
anything written above this line.

**The remaining CS2 cost is the ~183 µs of task machinery** — `ToriRS_TaskQueue_Run`
and the CS2 task state machines, outside the VM entirely. That is 38% of the
`cs2` stage and the largest unattributed block left in the client. It is also
completely unscoped: no target in this document addresses it, because until this
measurement every target assumed the cost was in the VM.

That is the honest end state of this line of work: eight wrong hypotheses, three
retired targets, one real 1.87% win banked under target 11, and one large
correctly-located cost that nobody has looked at yet. Scope the task machinery
before writing another VM optimisation.

**Take the emit walk with it.** Target 5 established that
`UITree_EmitWalk` never reads `is_dirty` and never calls
`UITree_NodeNeedsEmit` — the whole tree re-emits on every frame that sets
`need_redraw`, and 34.8% of the applies feeding it now provably change
nothing. The dirty bit is already written correctly at ~15 sites and by every
applier; nothing consumes it. Teaching the walk to honour it is the same
"pay for what changed" change as the layout item above, shares target 11's
damage tracking, and is what unlocks the two things target 5 had to leave
behind: quiet-frame emit/paint, and narrowing the timer loop's unconditional
`redraw = 1`. Do it here rather than as a separate target.

**Correction: the blocker recorded under target 5 was the wrong one.** That
note said a `UITree_MarkNodeDirty`-based signal would "quietly miss" the 45
places `rs_cs2_host.c` reaches into `tree->components[...]`. It would not.
Thirty-nine of those 45 are reads. The six writes are all one handler,
`exec_widget_set_model_angle`, and it already calls `UITree_MarkNodeDirty` on
the line after them (rs_cs2_host.c:4789) — as does the handler above it. Across
the whole tree, component writes outside `ui/uitree.c` and outside tests are 12
lines in 5 files: those 6, two in `app.c` (minimenu `font_id`, a `behavior.hide`
clear), one layout bookkeeping write, one build-time `next_sibling`, and two
bake-time writes before the tree is live. Marking the two `app.c` sites is a
small, enumerable job, not the scattered-writers problem described.

**The real reason this is hard is that emit does not read only the component
struct.** `uitree_emit.c` makes 32 `UITree_Host(...)` calls and touches
host-side state on ~120 lines — inventory contents, hover and drag state,
varps. A widget's emitted output can go stale with no write to its component at
all: the inventory behind an inv widget changes and the node is still "clean".
So a per-node `is_dirty` bit cannot gate this walk on its own, no matter how
completely the writers mark it. What target 11's damage tracking has to supply
is invalidation keyed on the *host state each node read while emitting*, which
is a dependency record per node, not a bit.

Size the prize before building that: the walk visits 2,841 nodes per frame and
`uitree_emit_skip` already rejects 2,205 of them, so only ~636 nodes actually
emit. `emit` is 0.412 ms mean against a 6.03 ms frame p50 (6.4%), and that ~636
is what a correct dirty gate would shrink on quiet frames. Worth having, but it
is the dependency-tracking build, and a partial version returns stale panels —
the exact failure target 5 declined to risk.

## Launch track - off the steady-state path

The steady-state budget does not cover launch, but the same captures bound
it: window 0 averages 253.78 ms/frame (p95 1.31 s), and the 60-second
sampling capture is 29.4% file-io and 20.6% heap by OS-tail classification.
These items are independent of targets 1–14, may proceed in parallel, and
must not change steady-state behavior. They matter for cold start, world
hops, and the reconnect path that contaminated window 1.

**Deprioritised, by the owner's direction (2026-08-22): steady state is what
matters here, not launch.** L1 is landed and written up below; L3 and L4 stay on
the list but behind every remaining steady-state target. Read the L1 write-up as
the cautionary case — both of its changes remove real, counted work and neither
moved the launch clock outside noise on this machine, which is exactly the kind
of return this track has to offer.

### L1 - JS5 sparse-cache read path

`PlatformXIO_Js5Pump` is 21.79 s of the 60 s capture (36.4% inclusive,
16.9 s file-io tail). Of that, 6.85 s is `dat2disk_fopen_index` opening and
closing the index/dat files **per read** (`fopen` → `CreateFileA` chains),
and ~1.25 s is CRC32-validating groups on local reads
(`3rd/rscache/src/checksum.c:51`).

- Hold the index and dat file handles open for the life of the store; the
  pump already serializes access to them.
- Record per-group validation in a session bitmap so each group's CRC is
  checked once, not per read; a failed check keeps the existing refetch
  path.

Acceptance gate: cold-launch wall clock to playable falls materially; an
injected corrupt group still refetches; no handle leak across
disconnect/reconnect.

#### Landed (first bullet), and the acceptance gate did not fire locally

`struct RSCache_Dat2Disk` now holds a read handle per index file for the life
of the disk, exactly as it already held `dat2_file`
(`3rd/rscache/src/dat2disk.h`). `dat2disk_read_index` takes the disk rather
than a directory string and goes through `dat2disk_disk_index_handle`;
`dat2_file_store_has_table` asks the same cache instead of doing its own
open/close pair. Coherency is by invalidation, not by hope:
`dat2disk_disk_index_forget` drops a slot in `dat2_file_store_put` and in
`dat2_file_store_commit_table`, which are the only two places this disk
changes an index under itself, and the write path already `fflush`es the
record it wrote (`dat2disk.c:533`), so the reopened handle sees it.

Two things are worth writing down.

**idx255 is not a valid table id, and it is the one that matters.** The array
is indexed by `dat2disk_index_slot`, not by table id. The first version keyed
on the id and gated on `RSCache_Dat2DiskIsValidTableId`, which admits 0..36 —
that predicate bounds the *reference-table array*, indexed by the table a
reference table describes, and 255 is the file they are all stored in rather
than one of the things described. The result was that idx255 alone never
resolved a handle, and the client came up with `Failed to load referencetable`
for every table 0..24. The extra slot on the end of `index_files` exists for
it.

**The prize is real and it is small — here.** Counting the calls over a
300-frame embedded-server launch: 3,332 index lookups, of which 456 now open a
file and 2,876 are served from a held handle. An 86% cut in `fopen` calls, and
it does not move the clock on this machine — see the joint measurement below.
The lookup count is also *identical* at 300 and 2,000 frames (3,332 either
way), which confirms this is entirely a launch cost with nothing left at
steady state.

So the 6.85 s in the XP capture is not reproduced and is not claimed as
recovered. 2,876 avoided opens can only be worth 6.85 s if an open costs
~2.4 ms, which a warm Win11 NTFS `CreateFileA` is nowhere near but an XP box
behind an on-access scanner plausibly is; the alternative reading is that the
profiler's 6.85 s was inclusive of the read the open leads to, in which case
the open itself was never the 6.85 s. Deciding between those needs the XP
capture rerun, and the change is worth keeping either way — it cannot be
slower, and it is the precondition for the sparse-cache path being cheap on
the machine the number came from.

Correctness gates run: reference tables load with zero failures, and the
2,000-frame workload signature reproduces the previous run to within drift
(35,218 scripts / 4,329,213 opcodes / 629,898 host ops, against
35,204 / 4,328,154 / 629,747).

#### The second bullet was already done, so the kernel got the work instead

The prescription — "record per-group validation in a session bitmap so each
group's CRC is checked once, not per read" — describes machinery the client
already has. `group_states` guards every entry into `js5_validate_local_group`
(`src/js5/js5.c:1021`, `:1270`, `:1385`), and a group that validates is marked
`JS5_GROUP_READY` (`:834`), which both the scan and the request path skip. A
group's CRC therefore already runs at most once per session on the local path;
the only way to compute one twice is for the first to *fail*, which is the
refetch case the bullet wanted to preserve anyway. There is no bitmap to add.

What is left is the one unavoidable pass, so `RSCache_Crc32` itself became the
target. It is now slicing-by-4: `crc32_reflected_table` is four slices deep and
the main loop folds four input bytes per round. Measured directly, 641 MB/s →
2026 MB/s, a **3.16×** speedup on the bulk path.

Four slices and not the usual eight because the profile that motivated this is
i686/XP, and slicing-by-8's 8 KB of table is half the L1 data cache of the CPUs
that column describes. For the same reason the wide path is gated on
`length >= 64`: a 300-frame launch makes **20,165 CRC calls totalling 386 KB**,
a mean of 19 bytes each, and at that size the loop folds four or five times —
far too few rounds to pay back touching four tables instead of one. Without the
threshold this change would have been a pessimisation on exactly the target it
was written for.

Correctness is gated by a differential test against an independent
bit-at-a-time reference: every length 0..512 at eight start offsets, every
incremental split point, byte-at-a-time composition, the four published
vectors, and the bzip2 vector to catch disturbance of the shared table build.
Zero failures.

#### What the acceptance gate actually said

*"Cold-launch wall clock to playable falls materially"* — it does not, here.
Five settled warm 300-frame launches (the first two runs after a link are
discarded; they are still page-cache-bound and were what made an earlier
two-sample reading look like a 3% trend it was not):

| | runs (ms) | mean |
| --- | --- | --- |
| baseline | 11169, 11140, 11178, 11168, 11142 | 11159 |
| + both L1 changes | 11101, 11123, 11140, 11083, 11128 | 11115 |

44 ms, 0.4%, against a spread of 0.34% within the baseline set itself. Nothing.

The reason is visible in the two counts above and is worth stating plainly:
**this configuration never reaches the volume the XP capture measured.** A
complete local osrs239 cache CRCs 386 KB over a whole launch; the 1.25 s the
capture attributes to `checksum.c` would need roughly 800 MB at the old rate.
Likewise 2,876 avoided `fopen`s can only be worth 6.85 s if an open costs
~2.4 ms, which a warm Win11 NTFS `CreateFileA` is nowhere near. Either the XP
run was a sparse cache being fetched and validated over JS5 — a genuinely
different data path from a complete local one — or the profiler's figures were
inclusive of the reads those calls lead to. Both readings are consistent with
what is measured here; neither is settled without rerunning the XP capture.

So: both changes are kept, both remove real work, and **neither is claimed
against the launch budget**. They are the same posture as the trace gate under
Optimization 6 — correct, cheap, unmeasured on the target that motivated them,
and excluded from any cumulative figure. Rerunning the XP capture is what turns
them into a result.

### L2 - silence costs nothing

The client logs `audio: no device; running silent` yet still decodes music
and builds sound requests: 5.55 s of Vorbis decode, 1.1 s of CS2
`sound_request` builders (`src/cs2vm2/cs2vm2.c:2060`), and 0.35 s of
`App_PlaySound` in the sampling capture. When no output device exists:

- skip music fetch and decode entirely, keeping the track-selection state
  machine correct so behavior with a device is unchanged;
- early-out the sound host handlers after the request is recorded — the
  host-request stream stays identical for differential logs; only the
  synthesis and mixing work disappears.

Acceptance gate: host-request logs unchanged; zero Vorbis/synth CPU while
silent; audible behavior identical when a device exists.

### L3 - kill 64-bit division on i686

libgcc's `__udivmoddi4`/`__umoddi3` are 9.1% exclusive in the capture
(5.46 s): ~4.2 s under Vorbis decode, ~1.6 s under Dat2Disk offset math.
Replace 64/64 divides with shifts and masks where sector/block sizes are
powers of two, 32-bit math where ranges provably fit, or reciprocal
multiplication.

Acceptance gate: bit-identical decode output and store reads; the helpers
leave the exclusive top 20 on a fresh capture.

### L4 - world-build heap churn

`Task_WorldLoad_Run` is 7.72 s in the capture, 4.73 s of it allocator time,
dominated by `merge_column`. Reuse per-column scratch across columns and
size merge buffers once per region instead of growing them per merge.

Acceptance gate: identical built scenes; world-load allocator samples drop;
the reconnect world reload shortens accordingly.

## Per-target scorecard

Every optimization records this table before it is accepted:

| metric | local before | local after | XP before | XP after |
|---|---:|---:|---:|---:|
| world ms/frame | 3.928 | 3.913 | 28.08 | |
| non-world ms/frame and % | 4.712 / 54.5% | 4.608 / 54.1% | 37.50 / 57.2% | |
| CS2 ms/logic tick | 0.654 | 0.516 | 7.11 | |
| cs2_settle ms/frame | 0.349 | 0.300 | 9.16 | |
| UI layout+interact+emit+paint ms/frame | 1.001 | 1.006 | | |
| scripts/opcodes/host ops per tick | 17.49 / 2148 / 312.5 | 17.49 / 2150 / 312.8 | 22.17 / 1211 / 174.4 | |
| CS2 allocations and bytes per tick | 17.5 x 13,080 B = 228.8 kB | 17.5 x 616 B = 10.8 kB | | |
| host-request bytes cleared per op | 4,408 | 4-40 (arm only) | | |
| frame bytes zeroed per push | 12,352 | 22.9 | | |
| `sizeof(CS2VM2_Frame)` | 12,352 | 96 | | |
| id-map probes per lookup | 3.55 | 1.54 | | |
| frame p50/p95 | 6.088 / 6.678 | 6.037 / 6.640 | | |

`local after` is cumulative over the targets landed so far. Per target, on the
same capture and at a workload held constant to within 0.13%:

| target | CS2 ms/logic tick | delta |
|---|---:|---:|
| baseline (HEAD) | 0.654 | |
| + Gate 0 and Optimization 1 | 0.576 | -11.9% |
| + Optimization 4 (arm clears) | 0.516 | -10.3% |
| + Optimization 5 (hook re-registration) | 0.509 | -1.4% |
| + Optimization 5 (compare before write) | 0.502 | -1.4% |
| + Optimization 2 (warm VM pool: 4 threads -> 1) | 0.482 | -4.0% |
| + Optimization 6 (id hash keyed on the whole id) | 0.479 | -0.7% |
| cumulative | | **-26.8%** |

**Read the sub-2% rows with the noise band in mind.** Two 2000-frame runs of one
unchanged binary differ by 1.4% on this metric (measured below), so a single-run
delta of that size is not by itself evidence. The two Optimization 5 rows and
the Optimization 6 row are all at or under that floor. They are kept because the
counters, not the clock, carry the result — the changes removed 11,583 of 18,144
hook re-registrations, 33,187 of 95,331 UI writes, and 1,156,752 of 2,026,663
hash probes, and work that is provably not done cannot be a timing artifact.
Where a row's only support is a sub-2% ms delta, do not accept it.

Optimization 10 (frame locals) has no row in that table on purpose. It was
measured later, in a different metric (cs2 stage p50) and by a different method
(two binaries built up front and run interleaved), because between-batch drift on
this machine reached 5.7% on that metric — larger than any single-target effect
here. Its result is 3.0%, and it belongs with its own table rather than in a
column that means "same capture, workload held to 0.13%". The lesson generalises:
once the remaining wins are this size, before/after captures separated by a
rebuild are not a measurement, and every later target should be A/B'd
interleaved.

Optimization 2's warm-pool array clear is out of the table for the same reason,
and adds a corollary: at 1.27% on the stage it is *under* the batch's own spread,
and the thing that makes it a result is a direct counter (`cs2_vm_init_ns` +
`cs2_vm_release_ns`, -70.6% per script) whose arithmetic predicts the stage move
to within 0.16 points. Below the noise floor, the counter is the measurement and
the clock is corroboration.

Two changes have now been measured that way and rejected: the typed host
handlers (Optimization 4, second half) and the per-opcode trace gate below. Both
are recorded where the target that motivated them lives, so a later reader finds
the null result before re-deriving the idea. A third — Optimization 2's
synchronous fast path — was rejected before implementation, on a measurement of
the prize rather than of the change: 0.36% of the frame at best, guarded by a
promotion path that would run 101 times in 2000 frames.

Two more are out of the table for a third reason: they do not touch CS2 at all,
so no column here can hold them. Both are recorded under their targets:

| change | stage | stage delta | frame delta |
|---|---|---:|---:|
| Optimization 12, cached window DC | `present` | -11.5% | -0.31% |
| Optimization 11, fixed-chrome measure ordering | `window_sync` | -73.0% | **-2.43%** |

The second is the largest single steady-state result in this document, and it is
worth noticing what it was: not a CS2 change, not an algorithm, but a loop whose
cheap rejects were behind its expensive one. Nothing in the target list pointed
at it — it was found by reading the full stage table and asking why
`window_sync` cost more than `present`. Do that again before starting target 11
proper; the census under that target is where it led.

### Measured and rejected: gating the per-opcode trace call

`cs2vm2_run_script_body` called `CS2VM2_TraceOpcode` unconditionally for every
one of the 4.33M opcodes in a run, to load two globals and return. Hoisting that
test to the call site (`CS2VM2_TRACE_ARMED()`) is free and obviously correct,
and it does nothing here: cs2 p50 landed at 326.2 and 330.8 us across two runs
against 322.6 before, i.e. inside the noise band. GCC already had the whole
static callee in view and had made the untraced path cheap.

The gate is still in the tree, for i686 rather than for win64 — cdecl pushes all
six arguments for every opcode where win64 passes four in registers. That is
unmeasured. Do not re-try this on x86-64 expecting a win, and do not count it in
the cumulative figure.

### How the local column was captured

Both columns are one 2000-frame run of the same binary configuration against
the **embedded server** — an offline client settles almost nothing and reads as
a CS2 cost of roughly zero, which is not a measurement of anything:

```
mingw32-make -C src PLATFORM=win64 OPT=1 EMBED_SERVER=1 CC=gcc all
SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=2000 TORIRS_PERF=1 TORIRS_PERF_WINDOW=100 \
TORIRS_PERF_CSV=perf.csv \
  ./src/torirs_win64.exe --manifest manifests/manifest_osrs239.ini \
                         --user asdf --pass a --soft3d
```

Two things about this metric, both learned by getting them wrong:

- **`CS2 ms/logic tick` is the `cs2` stage's `mean_ns`, and its noise band is
  1.4%.** Two runs of one unchanged binary gave 481.9 and 488.7 us. Treat
  anything under about 3% as unresolved by a single run, and re-run the
  unchanged binary before believing a small delta either way.
- **In the stage rows of the CSV, the fourth number is `max_ns`, not a total.**
  The `total` and `per_frame` columns are populated for counters only and are
  empty for stages. `cs2`'s max is a ~189 ms span during login, so it is an
  outlier that sits in every run's mean; that is why mean runs about 1.49x p50
  here. Reading that column as a total inverts the conclusion of any comparison
  that uses it.

"before" is HEAD with the nine client files reverted. Neither column could be
captured at all until a separate server-side bug was fixed:
`ToriRSServer_BankInitPlayer` left every bank slot's `var_key[]` at its calloc
default of 0, and 0 is obj id 0, not the -1 that `ToriRSServer_ItemSetVar` reads
as "free" — so the first `inv_setvar` aimed at a banked item found no free
entry and aborted during boot. That fix (torirs_server_bank.c,
torirs_server_container.{c,h}) is present in both columns, so it cancels.

The two runs did the same work to
within 0.13% — 35,270 vs 35,224 scripts, 4,332,430 vs 4,329,237 opcodes,
630,361 vs 629,904 host ops — so the stage deltas are time, not workload.

Two caveats on reading these numbers:

- `SDL_VIDEODRIVER=dummy` means the world and present stages are not doing what
  they do on a real display. The CS2, settle and logic rows are the ones this
  capture is evidence for; `world ms/frame` is here only to show the
  optimizations did not move time into it.
- Per-tick gauge rows (`iface_groups_resident`, `uitree_hidden`,
  `host_*_hooks`, `cache_clientscript_size`) fall by ~39x between the columns.
  That is Gate 0 changing the *sampling* rate from every tick to once per
  100-frame window, not the underlying quantity changing. Their totals are sums
  of samples and are not comparable across the two columns.

The allocation row is `sizeof(struct Task_CS2Run)` measured on each revision,
times the observed script rate. It is the calloc'd-and-zeroed cost that every
invocation pays whether or not it uses the storage: the 16x512 inline string
matrix (8,192 B) became exact-sized heap copies of the strings actually passed,
and `struct CS2VM_HostRequest` (4,408 B) moved behind a pointer allocated on
the first yield — which most invocations never reach.

Correctness gates are host-request-log equality, task-order/yield tests,
CS2 unit tests, deterministic input replay, and final-frame hashes. An
optimization that only moves time to another non-world stage does not pass.

---

## Opt 7: LANDED. `cc_deleteall` on an empty parent is now a real no-op.

The general fix. No script-id list, no allowlist, nothing scene-specific: one
sink was made idempotent, and the per-frame tree damage that had blocked
targets 11, 12 and 14 disappeared.

### How the cause was finally named

Every earlier attempt guessed at a call site. This one instrumented three
things in sequence, each answering exactly one question:

1. **Who dispatches script 4725?** Caller attribution inside
   `RS_CS2_DispatchHook` (`TORIRS_CS2_TRACE=<script_id>`, return address
   offset resolved with `nm` + `addr2line`) reported a single site with 2,006
   dispatches in 2,000 frames: [app.c:12118](src/app.c#L12118) — the
   `onTimer` walk. So the dispatch is *by design*; the reference client's
   `processWidgetTimers` fires every client tick too. There is nothing to fix
   on the invocation side, which retires general fix (1).

2. **What does it dirty?** The per-script `marks` column had said 4725 causes
   zero dirty marks, which was true and useless: `UITree_MarkNodeDirty` is not
   the only thing that bumps `dirty_gen`. The *topology* path — link, unlink,
   allocate, hide — bumps it directly. Adding `g_torirs_dirty_topo_seq` and
   bracketing `CS2VM2_RunScript` with it produced the answer immediately:

   | script | calls | creates | marks | **topo** | topo/call |
   |---|---|---|---|---|---|
   | **4725** | 2,004 | 0 | 0 | **2,004** | **1.00** |
   | 4730 | 6,187 | 0 | 628 | 0 | 0.00 |
   | 4520 | 4,006 | 0 | 4 | 0 | 0.00 |
   | 3350 | 2,009 | 0 | 0 | 0 | 0.00 |
   | 2100 | 2,049 | 0 | 2 | 0 | 0.00 |

   Exactly one bump per call, and every other per-frame script contributes
   none. That is the whole reason `gate_tree_quiet` was 0 on every frame.

3. **Which line?** `g_torirs_dirty_topo_line`, recorded whenever a script's
   bracketed topo delta is nonzero, named
   [uitree.c:2989](src/ui/uitree.c#L2989) — the tail of `UITree_CcDeleteAll`.

### The bug

`UITree_CcDeleteAll` ran its invalidation tail unconditionally, including when
the walk found no `dynamic` child to delete:

```c
parent->last_child_hint = -1;
parent->child_key_max = UITREE_CHILD_KEY_UNKNOWN;
uitree_child_index_drop(parent);
parent->is_dirty  = 1;
uitree_topo_bump(tree, __LINE__);   /* <- dirty_gen++ on a no-op */
tree->generation++;
```

If nothing was removed, none of that state changed: no slot was recycled,
`first_child` still points where it did, and the key ceiling and key→child map
are still correct for the surviving static children. Dropping them was not
merely wasted work — the `dirty_gen` bump is the write that made the entire
tree read as modified on every single frame.

This is the steady-state shape, not an edge case. The reference client rebuilds
a list by clearing it and re-adding rows, so the overwhelmingly common call is
`deleteall` on a parent that is *already empty*. Over 2,000 frames there were
2,391 `deleteall` calls and only 3,656 rows deleted across all of them.

The fix is four lines: track whether the loop removed anything, and return
before the invalidation tail if it did not.

### Measured: clean A/B, one binary, env-toggled

2,000 frames, `EMBED_SERVER=1`, same scene, same executable — the fix behind
`TORIRS_DELETEALL_NOFIX` for the baseline column so the two runs differ in
nothing but the branch. (The switch was removed after the capture; the fix is
unconditional in the tree.)

| stage (p50 ns) | baseline | fixed | Δ |
|---|---|---|---|
| **cs2** | 355,100 | **187,800** | **−47.1%** |
| **cs2_settle** | 301,400 | **135,900** | **−54.9%** |
| **logic** | 481,500 | **323,800** | **−32.8%** |
| frame | 5,966,500 | 5,854,800 | −1.9% |
| emit | 379,100 | 423,300 | +11.7% |

| counter (2,000 frames) | baseline | fixed |
|---|---|---|
| `uitree_layout_nodes` | 15,305,955 | **5,293,315** (−65.4%) |
| `uitree_layout_node_skip` | 14,896,207 | 4,883,567 |
| `uitree_layout_depth_recompute` | 2,151 | **146** |
| `gate_tree_quiet` | **0** | **1,944** |
| `emit_gen_quiet` | **0** | **1,943** |
| `emit_dirty_topo` | 43,836 | 41,472 |
| `uitree_cc_deleteall_rows` | 3,656 | 3,656 |

`deleteall_rows` is identical in both columns: the same rows are deleted, so
the fix removes no work the tree actually asked for. The 10 M fewer layout node
visits are the direct consequence of the tree no longer looking modified.

The `emit` rise is real and is more than covered by the cs2/settle fall — net
frame p50 is down 1.9%. The emit walk itself does identical work in both
columns (`uitree_walk_emit` 5,681,744 vs 5,681,742, `uitree_emit_skip`
4,409,596 vs 4,409,594), so this is layout that used to be forced during
`cs2_settle` now being resolved lazily inside the emit stage. It is moved time,
not new time.

### What this unblocks, and the one thing it does not

`gate_tree_quiet` going 0 → 1,944 is the precondition targets 11, 12 and 14's
emit half were all waiting on. The gate now holds on 97% of frames.

**But the gate is not yet sound enough to retain on.** `emit_gen_unsound` went
0 → **1**: on one frame the gate called the tree quiet and the emitted list
changed anyway. This is not a regression from this fix — `gate_quiet` was never
true before it, so the counter could not have fired. The fix made the gate live
and thereby exposed a pre-existing hole in it: some emit input is covered by
neither `dirty_gen` nor `hover_com_id`.

One stale frame in 1,944 is a visible glitch, not a rounding error. Find that
input and fold it into the gate term *before* building retention on top. That
is now the top of the remaining work, ahead of the ~183 µs of task machinery.

### Method note

Three instruments, three questions, three answers, zero guesses — after twelve
consecutive refuted hypotheses reached by reading candidate call sites. The
second instrument is the one worth remembering: the `marks` column read zero
for the guilty script and was *correct*, because it measured a real thing that
was not the thing that mattered. A counter that says "not me" is only evidence
if you have checked that it covers every path to the effect you care about.

---

## The emit retention gate is now sound. Third term: `layout_resolve_seq`.

The hole the previous section left open is closed, and closed generally.

### What the unsound frame actually was

A byte-level dump on the unsound frame — first differing desc, first differing
byte within it — named it in one run:

```
[emit-unsound] emit#5 desc 0/119 kind 1 com 35913729 node 173
               first diff at byte 36   clip.w 765 -> 807
```

Byte 36 of `struct UITreeEmitDesc` is `clip.w` (`kind` 0, `node_index` 4,
`component_id` 8, `x`/`y`/`w`/`h` 12–27, `clip.x` 28, `clip.y` 32, `clip.w` 36).
The node is the group-548 root, and it happened at **emit #5** of 2,000 — the
startup geometry settle, not a steady-state event.

The root clip cannot explain it: `UITree_EmitWalk` passes the compile-time
constants `UITREE_LAYOUT_ROOT_W/H`, so the canvas never moves. The node's own
*resolved* box moved.

### The cause, and why it is general

`UITree_LayoutResolve` re-walks — and so can move every resolved box — under
four conditions:

```c
if( tree->layout_resolved_valid && !tree->layout_stale && !tree->layout_force_full &&
    tree->layout_resolved_gen == tree->generation &&
    tree->layout_resolved_root_x == root_x && ... )
    return;   /* skip */
```

`layout_stale`, `layout_force_full` and a changed root box raise **no**
component's `is_dirty`, so none of them touch `dirty_gen`. The emit output
depends on resolved boxes; the gate did not. That is a structural gap, not a
property of one frame — it just took a frame with a real layout settle to
expose it.

The fix is a third gate term, the same shape as the first:
`UITree.layout_resolve_seq`, bumped in `UITree_LayoutResolve` immediately past
the skip check (i.e. exactly when boxes may move), and compared in the gate
alongside `dirty_gen` and `hover_com_id`. Any future layout-invalidation source
is covered automatically, because the counter sits at the resolve, not at the
things that trigger it.

`UITree_EnsureLayout` / `EnsureLayoutFor` are called from CS2 host ops and the
uitree accessors — all before emit, never inside the emit walk — so a sequence
snapshotted at gate time sees the whole frame's layout work.

### Measured, 2,000 frames, `EMBED_SERVER=1`

| counter | 2-term gate | 3-term gate |
|---|---|---|
| **`emit_gen_unsound`** | **1** | **0** |
| `emit_gen_quiet` | 1,943 | 1,393 |
| `gate_tree_quiet` | 1,944 | 1,944 |
| `emit_list_same` | 1,994 | 1,994 |

The gate still holds on **70% of frames** and is now sound over the full
replay — which is the precondition Opts 11, 12 and 14's emit half were waiting
on. The 550 frames it gave up are frames where layout genuinely re-resolved and
the emitted list happened to come out identical anyway: conservative, which is
the correct direction for a retention gate to err. (`uitree_layout_resolve`
4,324 with `uitree_layout_skip` 3,553 — about 770 real resolves.) Avoiding
those resolves is a separate optimization, not a soundness question.

The `[emit-unsound]` dump stays in the tree. It costs nothing on a sound frame
and it is the regression detector for this gate: if a future change introduces
an emit input outside the three terms, it prints the desc and the byte.

### One thing retention still has to answer before it is built

`emit_list_same` is a `memcmp` over the desc structs, and several desc kinds
carry **host-owned pointers with same-frame lifetime** — `minimap_dots`,
`entity_overlays`, `worldmap_tiles`, `debug_prims`. Two descs can compare equal
byte-for-byte while the buffers they point at hold different contents. So the
byte-compare proof above is a proof about the *descs*, not about the pixels,
for those kinds specifically.

Retention must therefore either exclude those kinds from the skip or fold the
host buffers' own change signal into the gate. Establish which of them the
scene actually emits before choosing; the ordinary widget kinds carry no
pointers and are unaffected.

### Cumulative steady-state effect of this segment

p50, 2,000 frames, same scene, `EMBED_SERVER=1`:

| stage | before Opt 7 | now |
|---|---|---|
| **cs2** | 355,100 | **181,200** (−49.0%) |
| **logic** | 481,500 | **308,300** (−36.0%) |
| frame | 5,966,500 | **5,751,000** (−3.6%) |
| emit | 379,100 | 411,700 (+8.6%, moved layout) |

10/10 CS2 tests pass.

---

## Opts 11 / 12 / 14, emit half: LANDED.

The walk is skipped on 70% of frames. `uitree_walk_emit` falls from 5,681,744
node visits to **1,715,962** over 2,000 frames.

### The blocker was real, total, and not what "handle the edge case" would suggest

The previous section flagged that some desc kinds carry host-owned pointers with
same-frame lifetime, so a byte-identical desc list is not a byte-identical
picture. Building the whole-list skip first and measuring it settled how much
that mattered:

| | |
|---|---|
| `emit_gen_quiet` | 1,393 |
| `emit_retained` | **0** |
| `emit_retain_blocked` | **1,393** |

Every single quiet frame carried one. A one-shot dump named it:

```
[emit-volatile] first at desc 1/11 kind 12 com -1  dots=0 ents=1 tiles=0 prims=0
```

`UITREE_EMIT_ENTITY_OVERLAY` — health bars and hitsplats, `component_id -1`
because the host owns it, present in every frame's list. Its contents genuinely
change every frame no matter what the tree does. So the whole-list skip is not
"mostly right with an edge case"; on this scene it is worth exactly zero, and
shipping it would have been a measurable no-op dressed as an optimization.

### The fix: refresh the volatile descs, retain everything else

`emit_hoist_entity_overlays` *reorders* descs rather than appending them, so a
volatile desc sits at a stable index in the retained list and can be refreshed
where it lies. `UITree_EmitRefreshVolatile` re-issues just those host requests
in place — a few calls against a walk that visits every node in the tree.

Each of the three refreshable kinds is re-issuable from its own desc:
`ENTITY_OVERLAY` writes its own clip box out of the request, `MINIMAP_DOTS`
reads nothing from the walk, `DEBUG_OVERLAY` needs only the node. `WORLDMAP` is
the exception and is handled by refusing rather than guessing: its desc does not
record which of the two host requests (tiles vs overview) filled it, so
`volatile_unrefreshable` is set and the caller runs the full walk. The fallback
is the path that was always correct.

The scan that sets these flags tests the **pointers**, not a list of kinds, so a
kind added later cannot quietly opt itself out of the check.

### Measured, 2,000 frames, `EMBED_SERVER=1`

| | whole-list skip | refresh + retain |
|---|---|---|
| `emit_retained` | 0 | **1,394** |
| `emit_retain_blocked` | 1,393 | **0** |
| `uitree_walk_emit` | 5,681,744 | **1,715,962** (−69.8%) |
| `emit` mean | 411,700 | **128,738** (−68.7%) |
| `emit` p50 | 420,200 | **0** |
| `emit_gen_unsound` | 0 | **0** |

### The soundness proof is still runnable, which is the point

`TORIRS_EMIT_VERIFY=1` forces the walk even on a retainable frame, so the
`[emit-unsound]` detector — the thing that found the `layout_resolve_seq` hole —
keeps working now that the skip exists. Without it, turning the skip on would
have retired the proof along with the problem it proved.

Verify run, 2,000 frames: `emit_gen_quiet` 1,393, **`emit_gen_unsound` 0**, no
unsound lines. 10/10 CS2 tests pass.

### Segment total, steady state

p50, same scene, `EMBED_SERVER=1`:

| stage | segment start | now |
|---|---|---|
| **cs2** | 355,100 | **182,900** (−48.5%) |
| **logic** | 481,500 | **312,900** (−35.0%) |
| **emit** | 379,100 | **0** (mean 411,552 → 128,738) |
| **frame** | 5,966,500 | **5,721,600** (−4.1%) |

Both landed changes are general: one made a sink idempotent, one skipped work
whose inputs provably did not move. Neither names a script, a component or a
scene.

## The "~183 µs of task machinery" is retired. Measured, it is 5.5 µs.

The figure came from a subtraction: the `cs2` stage minus the sum of the CS2
script profile's rows. Everything that subtraction failed to name was filed as
"task machinery — `ToriRS_TaskQueue_Run` plus the CS2 task state machines", and
at 38% of the stage it was the largest unscoped item in this document.

It was never measured. Reconstructing a stage from per-script means times
per-frame call rates is the same arithmetic that produced twelve refuted
hypotheses earlier in this document, and it failed the same way here: the
subtraction attributed *all* unnamed time to machinery, when most of it is the
task bodies doing real host work.

### Three stages, one run

`cs2_script` brackets every `cs2vm2_run_script_body`, outermost-only — a script
can reach the task layer, which can run another script, and without the depth
guard the child's nanoseconds land in the stage twice. `task_queue_run` brackets
`ToriRS_TaskQueue_Run`; `task_io` brackets both `PlatformX_IO_Pending` calls per
settle iteration and both `PlatformX_IO_Process` calls. `cs2_script` nests inside
`task_queue_run` for the scripts the pump runs.

Median frame, 2,000 frames, `EMBED_SERVER=1`, login-screen scene:

| stage | p50 (ns) |
| --- | --- |
| `frame` | 5,729,500 |
| `cs2` | 186,600 |
| `cs2_settle` | 135,400 |
| `task_queue_run` | 129,900 |
| `cs2_script` | 104,000 |
| `task_io` | 1,200 |

| counter | per frame |
| --- | --- |
| `task_steps` | 4.85 |
| `task_resumes` | 18.90 |
| `task_ends` | 17.37 |
| `cs2_scripts` | 17.60 |

Read the mean columns with care and do not use them here: `task_io` means 89.5 µs
against a p50 of 1.2 µs, because IO is bursty. The steady state is the median.

### Where the 186.6 µs actually goes

    cs2                              186.6
      scripts run outside the pump    51.2   = cs2 - cs2_settle; the logic
                                              tick's own RunScript, which is
                                              script 4725 at ~51 us/call
      cs2_settle                     135.4
        pump loop + queue mechanics     4.3   = cs2_settle - task_queue_run
                                              - task_io
        IO pending/process              1.2
        task_queue_run                129.9
          scripts run inside the pump  52.8   = cs2_script - 51.2
          task bodies, non-script      77.1

**The machinery named by the item — the queue and the pump — is 5.5 µs a frame,
0.10% of the frame.** `TaskRunner_Step` is three calls and
`ToriRS_TaskQueue_Run` is a `while` over a 32-slot intrusive list; there was
never 183 µs in it. There is nothing here to optimize and the item is retired.

### What the residual actually is, for whoever picks it up

77.1 µs sits inside the task bodies and outside the script bodies. The counters
say what shape it has: 17.37 `task_ends` a frame against 17.60 `cs2_scripts`,
and 18.90 resumes against 17.37 ends. **One task is created, queued, run to
completion in a single resume, removed and freed per script dispatch, every
frame** — 1.09 resumes per task, i.e. almost nothing ever yields.

So the residual is not queueing and not scheduling. It is `Task_CS2Run`'s own
per-dispatch work — `task_cs2_bake_pack`, the `task_cs2_plan_*` state machine,
the host-request dispatch — at **77.1 / 18.90 = 4.1 µs per resume**, wrapped
around a script body that itself averages 5.9 µs. That is a ~70% surcharge on
every script dispatch, and it is the honest target.

It is also 1.3% of a 5.73 ms frame, which is why it is being handed over
described rather than attacked: the two optimizations that landed this session
were each worth more than everything remaining in this stage combined. Anyone
who does pick it up must attribute the 4.1 µs *before* touching it — the method
this document now mandates, and the one that turned 183 µs into 5.5 µs.

### One fix taken on the way past

`ToriRS_TaskQueue_Run`'s `PT_EXITED` case called `getenv("TORIRS_TASK_LOG")` on
every early task exit, while the `PT_ENDED` case two lines above it used the
cached `torirs_task_log_enabled()`. Now both use the cached helper. Not measured
as a win — at 17 ends a frame it is not one — but a live `getenv` on a
per-task-completion path is the same shape as the per-spawn `fprintf` that cost
6 ms elsewhere in this tree, and it costs nothing to not have.

## Correction, and the real answer: `cs2` is not a CS2 stage.

The section above sized the residual at 77 us by subtracting `cs2_script` from
`task_queue_run`. **That subtraction is invalid and the 77 us figure is wrong.**
`task_queue_run` totals 469 ms over the run against `cs2_settle`'s 288 ms � the
task pump is not contained in the CS2 stage, because the exec_runner settles its
own queue outside it. Subtracting stages that are not strictly nested is the same
error that manufactured the 183 us in the first place, committed a second time in
the act of retiring it.

The fix is to make the subtraction same-bracket by construction.
`g_torirs_cs2_script_ns` accumulates outermost script-body nanoseconds;
`TORIRS_PERF_SCOPE_CS2` brackets the CS2 stage and carries the delta across its
own entry and exit into `cs2_script_in`. All four CS2 brackets in `app.c` go
through the macro � a bare `TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_CS2)` would
report its scripts as machinery, so the macro exists to make forgetting
impossible.

| stage | mean (ns) | p50 (ns) |
| --- | --- | --- |
| `frame` | 8,103,178 | 5,643,700 |
| `cs2` | 351,150 | 189,400 |
| `cs2_settle` | 148,739 | 139,200 |
| `cs2_script_in` | 126,192 | 107,400 |
| `cs2_script` | 125,625 | 105,200 |
| `cs2_task_start` | 11,067 | 11,900 |

`cs2_script_in` (126,192) against `cs2_script` (125,625) says every script body in
the run executes inside the CS2 bracket � the exec_runner runs none in steady
state � so the two are interchangeable here and the subtraction is exact.

### The stage is misnamed, and that is the whole answer

One of the four brackets wraps `app_logic_tick(app)`. That is the entire game
logic tick � entity updates, movement, the lot � not CS2. So `cs2` minus scripts
was never "task machinery": it is mostly the game tick, measured under a label
that says otherwise, and every estimate built on that label inherited the error.

What is genuinely task machinery, measured directly rather than by subtraction:

| | per frame |
| --- | --- |
| pump loop + queue mechanics (`cs2_settle - task_queue_run - task_io`) | 5.5 us |
| `Task_CS2Run` per-dispatch prologue (`cs2_task_start`, p50) | 11.9 us |
| **total** | **17.4 us** |

**0.31% of a 5.64 ms frame.** The item asked to remove 183 us of task machinery
from a stage that contains 17 us of it. It is retired, and this time the
retirement rests on two direct measurements rather than a subtraction.

The remaining non-script time in the `cs2` bracket is the logic tick's own work.
Optimizing it is a real target, but it is a *game tick* target and does not
belong in a CS2 document; filing it here under the CS2 label is what produced
three successive wrong numbers.


## L3 / L4: both premises falsified. L4 is measured and closed; L3 needs a capture

Steady-state work is exhausted, so the launch tracks came up next. Neither is
implementable from here, and the reasons are worth recording so the next person
does not spend the same hours finding them.

**L3 says "replace 64/64 divides with shifts and masks where sector/block sizes
are powers of two". `SECTOR_SIZE` is 520.** (`3rd/rscache/src/dat2disk.c:14`,
and `DAT1_SECTOR_SIZE` in `dat1disk.c:14` is the same 520.) There is no shift-
and-mask form of a divide by 520, so that clause is empty for the Dat2Disk half
of the target. The one place the read path touches 64-bit sector math,
`dat2disk.c:182`, is a *multiply*, not a divide. The remaining `% SECTOR_SIZE`
sites are on the append/write path, not the hot read path. So whatever
`__udivmoddi4` samples the capture attributed to "Dat2Disk offset math", they are
not at the line the target assumes, and the target's other two escape hatches
(32-bit math where ranges provably fit, reciprocal multiplication) need the
capture to say which call site is actually hot before either can be aimed.

**L4 says the 4.73 s of allocator time is "dominated by `merge_column`".
`merge_column` allocates nothing** � no `malloc`, `calloc`, `realloc` or `free`
anywhere in its body (`world_sharelight.u.c:425-660`); its working buffer is a
stack array, which the comment at line 312 already says. The allocator time is
therefore in something it calls, and the target's prescription ("reuse per-column
scratch across columns, size merge buffers once per region") is aimed at scratch
that is not there.

**Resolved without the capture: the allocating callee is
`sharelight_ensure_normals` (`world_sharelight.u.c:373`), and it is not reached
from `merge_column` at all.** The column pipeline in `world_build_sharelight`
runs three separate passes over a sliding window:

```
alloc_normals_for_column(sx)    -> sharelight_ensure_normals per lightable model
merge_column(sx)                -> merge across SHARELIGHT_MERGE_LOOKAHEAD (6) columns
apply_and_free_column(sx - 1)   -> ToriDraw_LightModelScene + ToriDraw_ModelFreeNormals
```

`sharelight_ensure_normals` calls `ToriDraw_ModelAllocNormals` and
`ToriDraw_ModelAllocMergedNormals`; the frees are at `:361` and `:617`. That is
two allocations and one free per lightable model in the scene, with each model's
normals held live for six columns before the apply pass releases them. The
`if( dm->normals ) return;` guard means a model is allocated once per liveness
window, not once per column -- but a model whose extent crosses an apply boundary
is freed and reallocated.

The other two candidates in the file are both already amortised, which is why the
naive reading of L4 was wrong twice over. `merge_column`'s working buffer is a
stack array (the comment at `:312` says so). `merge_normals`' hash scratch
(`g_slh_head` / `g_slh_serial` / `g_slh_next`, `:138-151`) is a grow-only global
that reallocates only when a column needs more buckets than any column before it,
so it converges after the first few columns -- exactly what the comment at `:106`
claims ("merge_normals tens of thousands of times; per-call malloc would
dominate").

So a correctly aimed L4 is "pool the per-model normals arrays across the
lookahead window" -- not "reuse per-column scratch".

### L4, measured. The capture was never required.

`world_build_lighting` already carries an alloc/merge/apply timing split behind
`TORIRS_REBUILD_TIMING=1` (`wb_timing_on()`, `world_builder.c:40`). Turning it on
sizes the target without Very Sleepy. One region rebuild, win64, `EMBED_SERVER=1`:

```
rebuild_timing: lighting alloc=3.8ms merge=2.3ms apply=1.2ms tail=7.4ms
rebuild_timing: end=30.4ms ... terrain_mesh=8.9 lighting=14.7 ... scenery models: n=7668
rebuild_timing: total=56.4ms begin=1.4 terrain=1.2 scenery=23.2 end=30.4
```

This falsifies L4's premise a third time, now by timing rather than by reading:
the target says the allocator time is "dominated by `merge_column`", and
`merge_column` is the **smallest** of the three column stages (2.3 ms). The alloc
pass is the largest, which agrees with the attribution above -- but it is 3.8 ms
inside a 56.4 ms rebuild that runs once per region crossing, against a claimed
4.73 s.

**Not implemented, and now that is a decision rather than a blocker.** The pool
would have to change `ToriDraw_ModelAllocNormals` / `ToriDraw_ModelFreeNormals`,
which live in vendored third-party code (`3rd/toridraw/toridraw_model.c`), to buy
a fraction of 3.8 ms on a **world-load** path the owner explicitly deprioritised
("I'm less worried about the launch. I really only care about the steady state").
Nothing about it is blocked; it is not worth its cost, and the number above is
what says so.

Both acceptance gates require a fresh i686/XP Very Sleepy capture ("the helpers
leave the exclusive top 20 on a fresh capture"; "world-load allocator samples
drop"). This environment builds win64 and cannot take that capture, so even a
correct change could not be shown to satisfy its own gate.

**L4 is now closed on the measurement in the section above -- it needed no
capture, because the world builder times itself.** L3 is the only one still
wanting an i686 capture: resolve `__udivmoddi4`'s callers to lines, intersect
them with the ten-object list below, and narrow operands at whatever survives.
Its gate ("the helpers leave the exclusive top 20 on a fresh capture") cannot be
evaluated on win64 at all, and guessing at it would be the inspect-and-guess
method this document spent twelve refuted hypotheses learning not to use.


### L3, located. The divides are per-vertex in the projection kernel.

`objdump -dr` on the win32 objects attributes the libgcc divide helpers to
functions, which is the step `nm` could not take. The result is not spread
across ten files -- it is concentrated in the rasteriser and the projector:

```
src/build_win32_opt/toridraw_unity.o
     12  ToriDraw_TriangleFaceTextureFlat{Transparent,Opaque}NearClip
     12  ToriDraw_TriangleFaceTextureBlend{Transparent,Opaque}NearClip
     12  ToriDraw_TriangleFaceTexture{Flat,Blend}AffineV3NearClip
     12  ToriDraw_TriangleFaceGouraud
     12  ToriDraw_TriangleFaceFlat
     12  ToriDraw_RasterModelFace
     10  ToriDraw_Project
src/build_win32_opt/app.o
      1  app_logic_tick
      1  App_RunOnce
```

`app.o` has two. The renderer has well over a hundred, on the per-face and
per-vertex paths -- and `render` is 4.0 ms of a 5.7 ms frame, so this is the
steady-state path, not a launch one. The 2 in `app.o` are why the original
"launch track" framing missed it.

The source is one macro. `graphics/projection.h:9` defines

```c
#define SCALE_UNIT(x) ((((long long)x) << UNIT_SCALE_SHIFT))
```

and the kernels spell the reference projection as `SCALE_UNIT(x) / z`
(`projection.u.c:212-213`, `:730`, `:811-812`, `:879-880`). That is a 64/64
divide per projected vertex. The 64-bit numerator exists only so a coordinate
large enough to overflow `x << 9` survives; inside +/-2^22 it cannot overflow,
and **the reference client does this multiply in plain 32-bit arithmetic**, so
narrowing matches reference semantics rather than departing from them.

Truncation is identical either way: C integer division truncates toward zero in
both widths, so an in-range value divides to the same result.

### L3: LANDED. render -28%, frame -23% on i686.

`addr2line` over the 114 `__divdi3` call sites puts **110 of them on one line**,
`triangles/toridraw_triangle_clip.u.c:168`, in
`ToriDraw_TriangleLerpPlaneProjecti` -- paid per near-clipped vertex of every
clipped face. The remaining 4 are `toridraw_render.u.c:463-464`.

The 64-bit *product* there is load-bearing (`lerp_p * (camera_cot16 >> 1)`
overflows `int` at ordinary camera scales). The *divide* was not: after the
`>> 6` the value is back inside `int`. Narrowed with the wide divide kept as a
fallback branch, since a large `near_plane_z` can leave the numerator outside
`int` while the quotient still fits -- a legitimate camera, not a caller bug.

Two changes, both in vendored `3rd/toridraw`:

- `graphics/projection.h` -- `ToriDraw_ScaleUnitDiv`, used at the seven
  `SCALE_UNIT(v) / z` sites in `projection.u.c`. `ToriDraw_Project`: 10 -> 4.
- `triangles/toridraw_triangle_clip.u.c:168` -- the 110-site line.

Measured, win32 `OPT=1 EMBED_SERVER=1`, 2000 frames, p50:

| stage | before | after | delta |
|---|---|---|---|
| `render` | 8201.6 us | **5903.1 us** | **-28.0%** |
| `frame` | 9968.3 us | **7687.1 us** | **-22.9%** |
| `display` | 8971.8 us | 6664.7 us | -25.7% |
| `paint` (control) | 604.7 us | 601.4 us | flat |
| `cs2` (control) | 200.7 us | 204.3 us | flat |

The two flat controls are what separate this from run-to-run drift. win64 is
unchanged (`render` 4000.5 -> 3967.1, inside variance) and 10/10 `test-cs2-*`
pass.

Note the static call-site count does **not** drop -- the fallback branch is
still emitted per inline expansion, so `objdump` still reports 12 per rasteriser
function. Only the dynamic path changed. Counting relocations would have called
this a no-op; the timing is the gate.

**The target was mis-filed.** L3 sat in the *launch* track, and `app.o` has 2 of
the 114 sites. The renderer has the rest, and `render` is 70-77% of a steady
frame. The lesson for anything left in this document: a track label is not
evidence about which path the work is on.



The capture is not actually required to find the callers � a 64-bit divide on
i686 is a link-time reference to a libgcc helper, so it is statically visible.
Build the win32 lane (`toolchains/mingw32`, `PLATFORM=win32 OPT=1`, builds clean)
and ask every object which ones need the helpers:

    for o in $(find src/build_win32 -name '*.o'); do
      nm "$o" | grep -qE "U _?__u(divmoddi4|moddi3|divdi3)" && echo "$o"
    done

Ten objects reference them:

| object | note |
| --- | --- |
| `jbase37.o` | base37 name codec � divide by 37, not a power of two |
| `hmap.o` | `3rd/hmap` � third-party |
| `collision_map.o` | |
| `proctex_generator.o` | |
| `toridraw_unity.o` | |
| `app.o` | |
| `torirs_perf.o` | this document's own instrumentation |
| `platform_win32_timing.o` | ns/tick conversions; expected and correct |
| `tommath.o`, `lua_unity.o` | third-party |

**Neither Vorbis nor Dat2Disk appears.** Their objects are not in this build at
all � the win32 lane links them from elsewhere � so the capture's attribution
cannot be checked against this list, and L3's two named targets remain
unconfirmed rather than refuted.

What this does establish is the shape of the fix. `jbase37` divides by 37 and
`hmap` by a capacity; neither is a power of two, so L3's "shifts and masks"
clause is empty for these too. The applicable escape hatch is reciprocal
multiplication (a compile-time constant divisor like 37 is one the compiler will
already strength-reduce if the operands are narrowed to 32 bits) or proving the
ranges fit in 32 bits. Both need to know which of the ten is hot, and *that* is
the part the capture is genuinely needed for � not the call sites, which are
above.

Next step for L3 is therefore much smaller than the target implies: take one
i686 capture, intersect its `__udivmoddi4` callers with this ten-object list, and
narrow the operands at whichever sites survive.


## Rasterizer and blitting track (R1-R8) - measured attribution and targets

CS2 is closed. On i686 the whole `cs2` bracket is 204.3 us p50 of a 7,687.1 us
frame -- 2.7%, and that bracket is contaminated upward by an `app_logic_tick`
wrap it should not contain. `render` is 5,903.1 us, **76.8% of the frame**. This
track is about that number.

Everything below is measured, not inferred. Three captures back it:

- `p32_split.csv` -- 2000 frames, i686, `render` split into six disjoint
  sub-stages by command class.
- `bdgesas7t` -- 2000 frames, i686, per-face cycle census over all ten raster
  kernel dispatch sites.
- `bqvkalc3v` -- 2000 frames, i686, the same census plus shaded-pixel tallies in
  the span loops.

All three used the canonical steady-state capture on
`manifests/manifest_osrs239.ini` with `--soft3d`.

### Where `render` actually goes

`render` had been one opaque bracket, so every prior statement about "the
rasterizer" was inference. Split by command class (p50 us, 2000 frames):

| sub-stage | p50 us | share of `render` | share of `frame` | cmds/frame |
| --- | --- | --- | --- | --- |
| `r_model` | 4,908.0 | **85.5%** | **63.8%** | 1,510.3 |
| `r_sprite` | 336.2 | 5.9% | 4.4% | 90.9 |
| bus walk + dispatch | 383.7 | 6.7% | 5.0% | (1,674.9 total) |
| `r_font` | 56.4 | 1.0% | 0.7% | 38.9 |
| `r_clear` | 47.5 | 0.8% | 0.6% | 1 |
| `r_other` | 5.8 | 0.1% | 0.08% | 25.8 |
| `r_rect` | 2.4 | 0.04% | 0.03% | 9.0 |

The instrumented `render` p50 was 5,740 us against 5,903 us uninstrumented, so
two clock reads per command sits inside the noise and the split is trustworthy
as a ratio.

**3D model rasterization is 64% of the i686 frame.** 2D blitting is 4.4%.
Clearing is 0.6% -- 1.54 MB written in 47.5 us is already at memory bandwidth
and is not a target.

### Which kernel inside `r_model`

Per-face cycle census, 23,968,675 faces over 2000 frames (11,984 faces/frame):

| kernel | faces | % faces | % cycles | cyc/face |
| --- | --- | --- | --- | --- |
| `gouraud` | 19,971,968 | 83.3% | 19.9% | 206.2 |
| `tex_blend_opaq` | 2,566,154 | 10.7% | **66.2%** | **5,327.6** |
| `tex_blend_trans` | 483,274 | 2.0% | 11.1% | 4,755.6 |
| `flat` | 827,459 | 3.5% | 1.0% | 248.3 |
| `tex_flat_opaque` | 119,820 | 0.5% | 1.8% | 3,040.4 |
| `zbuf`, `gouraud_smooth`, `tex_flat_trans` | 0 | 0 | 0 | -- |

**Textured faces are 13.2% of faces and 79.1% of raster cycles.** A textured
face costs 26x a gouraud face.

Caveat on the two numbers this census inflates: the `rdtsc` pair is ~50-60
cycles, which is ~25% of `gouraud`'s 206 cyc/face and ~1% of
`tex_blend_opaq`'s 5,328. Gouraud's true share is therefore *below* 19.9% and
textured's is *above* 79.1%. The census error runs in the direction that
strengthens the conclusion, not against it.

### Area or per-pixel cost? -- the question that picks the fix

26x per face is equally consistent with "the inner loop is expensive" and "the
face is 26x bigger", and those have unrelated fixes. Shaded-pixel tallies in the
span loops settle it:

| kernel group | faces | shaded pixels | px/face | cyc/px |
| --- | --- | --- | --- | --- |
| textured blend (opaque + trans) | 3,049,428 | 293,231,523 | **96.2** | **54.5** |
| gouraud | 19,971,951 | 52,096,658 | **2.61** | (n/a -- see below) |

Two separate findings, and they point at opposite ends of the pipeline:

**Textured faces are large and each pixel is expensive.** 96 px/face at 54.5
cycles per pixel. 146,616 textured pixels per frame against a 765x503 = 384,795
px canvas is 38% coverage -- so this is *not* an overdraw problem. A
perspective-correct textured pixel should be 10-15 cycles. The 54.5 is the
inner loop, and the inner loop is the target.

**Gouraud faces are micro-triangles.** 2.61 shaded pixels each. Dividing 206
cyc/face by 2.61 px gives a meaningless "79 cyc/px" -- the span loop runs three
iterations and essentially the entire cost is per-face setup: edge init,
barycentric setup, dispatch, clipping, and the census's own `rdtsc`. Making the
gouraud span loop faster would be optimizing three iterations of a loop that is
already dwarfed by its own prologue. The lever is per-face overhead or face
count, not shading.

### The structural finding: the i686 lane cannot reach any SIMD span

`3rd/toridraw/graphics/raster/texture/span/tex.span.u.c` selects its span
implementation at *compile* time:

```c
#if defined(__ARM_NEON)      /* neon span   */
#elif defined(__AVX2__)      /* avx2 span   */
#elif defined(__SSE4_1__)    /* sse4.1 span */
#elif defined(__SSE2__)      /* sse2 span   */
#else                        /* scalar span */
#endif
```

NEON, AVX2, SSE4.1 and SSE2 spans all exist in this tree and are maintained.
`src/platform/platform.mk:185-215` pins the XP lane to `-march=i686
-mtune=generic -mfpmath=387`, documented as "Keep the executable usable on
pre-SSE2 XP machines." Win64 gets `-march=x86-64`, hence SSE2.

So the lane carrying 79% of its raster cycles in the textured span is the one
lane that always takes the scalar fallback -- and it takes it on *every*
machine, including the overwhelming majority that do have SSE2.

**Resolved by the owner: SSE2 is assumed present on every XP target.** The
"pre-SSE2 XP machines" note was removed, which turns R1 from CPUID dispatch into
a two-line flag change. See the R1 result section below.

### Targets

Ranked by (share of frame) x (plausible reduction). Every gate is a **timing**
gate. Static call-site counts are not a gate -- the last sweep established that
a guarded fallback branch is still emitted per inline expansion, so counts do
not move when the hot path does.

| # | target | owns | fix | gate |
| --- | --- | --- | --- | --- |
| **R1** | ~~Textured span takes the scalar path on every i686 machine~~ | 79% of raster cycles, ~50% of frame | **LANDED** -- the owner dropped the pre-SSE2 premise, so this became a flag change, not CPUID dispatch. See below | **PASSED**: `frame` -21.3%, `r_model` -31.3% |
| **R2** | ~~`float inv_w = 1.0f/(float)w` per 8-px block, on x87~~ | 12 dependent `fdiv`s per textured face | **MOOT on this lane.** `-mfpmath=sse` turned every x87 `fdiv` into `divss`, and the SSE2 span R1 unlocked does not run this loop at all. x87 in `toridraw_unity.o` fell from pervasive to 32 instructions | -- |
| **R3** | ~~19.97M gouraud faces averaging 2.61 px~~ | 83% of faces, <20% of cycles, ~all of it prologue | **CLOSED by measurement, not landed.** The prologue's cost was assumed to be its five divides. A probe that doubles them costs 37 us of a 5,958 us frame, so the whole divide population is 0.5%. Implemented the `dy == 1` fast path anyway (33% of edge divides) and it measured below the noise floor. Reverted. See below | **FAILED to move**: paired A/B/A/B put `r_model` -1.3% against a -1.5..-2.7% within-binary floor |
| **R4** | ~~`ToriDraw2D_BlitArgbAlpha` blends every pixel through a 3-branch ladder, including at `alpha == 255`~~ | `r_sprite`, 4.4% of frame | **LANDED** -- `alpha == 255` gets its own row walk that runs-detects and `memcpy`s. Sprite loading untouched; no flag needed. See below | **PASSED**: `r_sprite` -14.8% vs SSE2, -7.5% vs the original i686 lane |
| **R5** | `g_hsl16_to_rgb_table` is 65,536 entries / 256 KB, indexed per shaded pixel | unknown -- suspected L2 thrash on XP-class targets | Measure first. Then possibly a 16 KB table with low bits computed, or arithmetic conversion | **Investigate only.** Needs a cache-miss measurement before it is a target |
| **R6** | ~~Gouraud 4-wide unroll writes the same constant word four times~~ | negligible | Folded into R3, which is closed. Nothing to do | none; not independently actionable |
| **R7** | Residual 64-bit division: 10 `__umoddi3` in `ToriDraw_MapSearch`, 4 `__moddi3` in `BlitArgbTiledAlpha`, 2 `__divmoddi4` in `BlitArgbScaledAlpha` | none measured | -- | **Closed** unless a capture puts one on the hot path. `r_rect` is 0.03% of frame; map search is not per-face |
| **R8** | 383.7 us unaccounted inside `render` across 1,674.9 commands/frame | 5.0% of frame, minus the measurement's own cost | -- | **Needs a clean measure first.** ~40 us of that figure is the instrumentation's 3,350 clock reads |

### Ordered list

1. **R1 -- runtime SSE2 dispatch for the textured span.** The single largest
   lever, on the lane that needs it most, against code that already exists and
   is already maintained. The whole of the work is the dispatch plumbing and
   keeping `-msse2` from leaking into shared inline headers.
2. **R2 -- kill the per-block x87 divide.** This is what R1's *fallback* lane
   runs, i.e. the genuinely pre-SSE2 XP machines the ABI contract exists for.
   Independent of R1 and lands on top of it.
3. **R3 -- micro-triangle path for gouraud.** Second-largest raster share, and
   the only target here whose fix may lie upstream of the rasterizer entirely
   (11,984 faces/frame at 2.6 px each says the LOD is emitting sub-pixel
   geometry).
4. **R4 -- opaque fast path in the ARGB blit.** Smaller share, but the cheapest
   fix on the list and it touches the one part of this track that is actually
   "blitting" rather than 3D raster.
5. **R5 -- measure the HSL table's cache behaviour.** Gates whether it is a
   target at all.
6. **R8 -- re-measure the command-bus walk without instrumentation.** Gates
   whether it is a target at all.

R6 folds into R3. R7 is closed.

### What is explicitly *not* a target

- **`r_clear`** -- 1.54 MB in 47.5 us is memory bandwidth.
- **`r_rect`, `r_font`, `r_other`** -- 0.8% of frame combined.
- **Overdraw reduction for textured faces** -- 38% screen coverage refutes it.
- **The gouraud span loop** -- three iterations behind a dominant prologue.

## R1: LANDED. The XP lane now has an instruction set.

The owner dropped the pre-SSE2 premise, so R1 never needed CPUID dispatch. Two
lines:

- `src/platform/platform.mk` -- `-march=i686 -mtune=generic -mfpmath=387`
  becomes `-march=pentium4 -mtune=generic -mfpmath=sse`.
- `src/platform/platform_check.mk` -- `LANE_REQUIRE_win32` requires the new
  pair, and `-march=i686 -mfpmath=387` were added to `LANE_FORBID_win32`.

The forbid entry is the important half. The old flags cost this lane 31% of its
model raster *silently* -- nothing failed, nothing warned, the `#if` just
selected the scalar span. A future "restore the conservative baseline" edit must
fail loudly instead. Verified by negative test: restoring the i686 flags fails
`make lane-check` with exit 2.

### Measured, i686 lane, 2000 frames, p50

| stage | i686 + x87 | pentium4 + SSE | delta |
| --- | --- | --- | --- |
| `frame` | 7,687.1 us | **6,053.1 us** | **-21.3%** |
| `display` | 6,664.7 | 5,029.9 | -24.5% |
| `render` | 5,903.1 | 4,269.3 | **-27.7%** |
| `r_model` | 4,908.0 | 3,370.7 | **-31.3%** |
| `r_clear` | 47.5 | 41.5 | -12.6% |
| `r_rect` | 2.4 | 2.2 | -8.3% |
| `logic` | 321.9 | 297.1 | -7.7% |
| `cs2` | 204.3 | 189.9 | -7.0% |
| `cs2_script` | 110.0 | 90.4 | -17.8% |
| `task_queue_run` | 144.9 | 121.4 | -16.2% |
| `r_sprite` | 336.2 | 367.5 | **+9.3%** |
| `r_font` | 56.4 | 62.9 | **+11.5%** |

The `render` row compares against the *instrumented* i686 capture (5,740.0 us)
for the sub-stage rows, since only that run carries the `r_*` split.

### Codegen was verified, not assumed

The `#if` firing is a claim about the build, so it was checked against the
object rather than the source. `src/build_win32_opt_es/toridraw_unity.o` now
contains `pmullw`, `punpcklbw`/`punpckhbw`, `packuswb` and `psrlw` -- the 16-bit
blend ops that only `tex.span.sse2.u.c` emits -- and x87 fell from pervasive to
32 instructions.

### The win is wider than the textured span

`logic`, `cs2` and `task_queue_run` improved by 7-18% without a line of their
code changing. The same `#if defined(__SSE2__) && !defined(SSE2_DISABLED)` guard
also gates `projection16_simd`, `projection_zdiv_simd` and `projection_ortho`,
so the projection pipeline was taking scalar fallbacks for the same reason. R1
was scoped as "the textured span" and delivered the whole lane.

This is the general lesson worth keeping: the target list was built from a
profile, and the profile could only see *where* time went, never *that an entire
instruction set was switched off*. A compile-time `#if` selecting a slow path is
invisible to every timing measurement -- it does not show up as a hotspot,
because the slow path is the only path the profiler has ever seen.

### Accepted cost

`-mfpmath=sse` drops x87's 80-bit intermediates, so float results can differ in
the last bit from the old lane. Deliberate, recorded in the flag comment.

### Not explained by SSE2: `r_sprite` +9.3%, `r_font` +11.5%

Blitting and glyph rendering are integer work that `-mfpmath=sse` should not
touch, and both got slower. +31 us and +6.5 us respectively. "Code layout
shuffle" is the obvious guess and is not evidence; this is tracked as its own
investigation rather than rounded to zero.

## The `r_sprite` +9.3% after R1: real, but not blit codegen

R1's one bad row was written off in the moment as "code layout shuffle, not a
real regression". That was a guess presented as a conclusion, and only half of
it survived contact with a measurement.

**Is it noise?** No. Two 2000-frame captures of the *same* SSE2 binary put
`r_sprite` at 367.5 and 362.2 us -- a spread of -1.4%. `r_font` -1.0%,
`r_model` -0.2%, `render` -0.6%. A +9.3% move is six times the run-to-run
spread. The regression is real and the dismissal was wrong.

**Is it the blit code?** No. `objdump` of `ToriDraw2D_BlitArgbAlpha` under both
flag sets: 280 instructions each. `BlitArgbScaledAlpha` 346 vs 346.
`BlitArgbTiledAlpha` 295 vs 295. Diffing the three pairs turns up nothing but
branch *target addresses*. `-march=pentium4 -mfpmath=sse` did not change one
instruction of the blit -- as expected, since it is integer work, but now
established rather than assumed.

Identical code that runs 9% slower after the rest of the binary changed size is
final-link code layout: i-cache set conflicts and branch-predictor aliasing
against whatever now sits next to it. That is luck, not a defect, and chasing it
directly would mean tuning link order against one machine's cache geometry.

The useful response is to make the stage cheap enough that its layout stops
mattering, which is what R4 already existed to do.

## R4: LANDED. The opaque blit stops paying per-pixel for a per-run decision.

`ToriDraw2D_BlitArgb` -- what every piece of UI chrome calls -- forwards to
`ToriDraw2D_BlitArgbAlpha` with a literal 255, which then walked the general
per-pixel ladder: extract alpha, test against the blend factor, test against 0,
test against 255. The last two are data-dependent, so a sprite edge mispredicts
on every pixel.

Sprite rows are not edges, though. They are long runs of `a == 255` (interior)
and `a == 0` (surround) with a handful of blended pixels in between. The fast
path walks runs instead of pixels: one branch per run, and the opaque run goes
to `memcpy`, which this lane now vectorises (R1).

The copy is verbatim rather than a blend because `a == 255` already means the
source word's top byte is `0xFF`, so the `argb | 0xFF000000` the per-pixel path
applies is a no-op on exactly the pixels a run contains. Same bytes out.

### Measured, i686 lane, 2000 frames, p50

| stage | i686 + x87 | SSE2 (A) | SSE2 (B) | + R4 | R4 vs SSE2 | R4 vs i686 |
| --- | --- | --- | --- | --- | --- | --- |
| `frame` | 7,492.9 us | 6,053.1 | 6,029.9 | **5,982.7** | -1.0% | **-20.2%** |
| `render` | 5,740.0 | 4,269.3 | 4,245.1 | 4,206.5 | -1.2% | -26.7% |
| `r_model` | 4,908.0 | 3,370.7 | 3,363.0 | 3,384.2 | +0.5% | -31.0% |
| `r_sprite` | 336.2 | 367.5 | 362.2 | **310.9** | **-14.8%** | **-7.5%** |
| `r_font` | 56.4 | 62.9 | 62.3 | 64.9 | +3.7% | +15.1% |

R4 more than erases the layout regression: `r_sprite` finishes 7.5% below the
number it had *before* R1 made it worse.

`r_font` keeps its +3.7%. It is 65 us, about 1% of the frame, and the glyph path
does not go through this blit at all -- so it is the same layout luck with
nothing yet built to absorb it. Not worth a link-order experiment at 1%.

### Why the fast path is inlined rather than extracted

A `blit_opaque_row(...)` helper would be the tidier shape, but CLAUDE.md
requires asserts on its pointer parameters and asserts are live at `OPT=1`.
At ~45k rows/frame that is ~45k additional perfectly-predicted branches, roughly
15 us, against a 367 us stage -- 4% of the win spent on a contract check the
enclosing function's entry asserts already cover. Inlined, the existing asserts
dominate the whole walk.

### Correctness is proved, and the proof is not vacuous

`3rd/toridraw/toridraw_blit_opaque_test.c`, wired up as `make test-blit-opaque`.
It carries `opaque_reference()` -- the per-pixel loop copied verbatim as it stood
before R4, because the function under test now *returns before reaching* that
loop in exactly the case being tested, so comparing it against itself would
compare the fast path with the fast path.

4200 cases: 6 alpha shapes x 700 deterministic-RNG trials. The shapes are chosen
to hit the run walk's branches rather than its average -- all-opaque, all-clear,
alternating single pixels (defeats run detection entirely), opaque-interior /
clear-surround (the common sprite), partials-only (never takes a run), and mixed.
Destination offsets span +/-16 past every edge, the clip rect is randomised on
all four sides, and both buffers start from the same non-uniform contents so a
wrongly-skipped blend cannot match a cleared buffer by luck.

Result: **4200 cases, 0 mismatches.**

Negative-tested, because a test that has never failed has not been shown to
work. Loosening the run condition from `a == 255u` to `a >= 254u` -- so the fast
path wrongly `memcpy`s near-opaque pixels -- gives **4200 cases, 116 mismatches
(149 pixels), FAILED, exit 2**. The test bites.

`test-blit-scaled` passes both before and after, and is *not* coverage for this:
it exercises the scaled blit, which never reaches the new path.

## R3: CLOSED by measurement. The gouraud prologue is not where the cycles are.

R3 was the largest remaining raster target on paper: 83% of gouraud faces are
2.61 px, so nearly all of their cost is per-triangle setup rather than per-pixel
fill. The setup's most expensive-looking instruction is integer division -- five
per triangle, three edge slopes (`(dx << 16) / dy`) and two barycentric colour
steps (`(numerator << 8) / sarea`).

The target is dead. Not because the fix failed, but because the premise was
wrong: those divides are worth about half a percent of the frame.

### The census, so the shape is on record

1000 frames of the osrs239 steady state, 9,604,605 triangles:

| quantity | distribution |
| --- | --- |
| edge `dy == 1` | 33.05% |
| edge `dy <= 2` | 54.19% |
| edge `dy <= 9` | 88.90% |
| `abs(sarea) == 1` | 12.24% |
| triangle spans 1 row | 22.22% |

Per frame that is 9,605 triangles: 23,100 edge divides + 19,209 `sarea` divides
= **42,309 idivs**. The distribution is as lopsided as hoped -- a third of the
edge divides have a divisor of 1, where the quotient is just `dx << 16`.

### What the divides actually cost

Before building anything further, price the work. The technique: duplicate the
five divides with perturbed numerators and the same divisors, accumulate into a
`volatile` sink, and change nothing that reaches a pixel. The perturbation
defeats CSE, the `volatile` defeats hoisting, and rendered output stays
bit-identical -- so the frame delta is the cost of exactly five more divides per
triangle and nothing else.

| stage | base #1 | base #2 | probe #1 | probe #2 | base avg | probe avg | delta |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `frame` | 5,969.4 | 5,946.5 | 6,003.9 | 5,986.7 | 5,957.9 | 5,995.3 | **+37.3 us (0.6%)** |
| `render` | 4,157.2 | 4,153.7 | 4,170.1 | 4,179.2 | 4,155.4 | 4,174.6 | +19.2 us (0.5%) |
| `r_model` | 3,339.8 | 3,337.0 | 3,359.9 | 3,376.6 | 3,338.4 | 3,368.2 | +29.8 us (0.9%) |

48,000 divides for 37 us is about **2.7 cycles each**, not the ~26 an idiv
latency table gives. They are mutually independent, so an out-of-order core
overlaps them and the population is throughput-bound. This reads as a slight
*under*-estimate of the originals -- the duplicates overlap each other better
than the real ones do, which are threaded through dependent setup -- but not by
the order of magnitude that would make the target live again.

That figure is the ceiling on all of R3-by-divide-elimination. Removing *every*
divide in the prologue cannot buy more than 0.6% of frame.

### The fast path was built anyway, and measured below the noise floor

All 65 open-coded `(dx << 16) / dy` sites across 23 raster files were
consolidated into one `toridraw_edge_step_ish16()` carrying the exact `dy == 1`
arm. Paired A/B/A/B, 4 x 2000 frames, no builds between runs:

`r_model` **-1.3%**, `frame` **-0.8%** -- against a within-binary spread of -1.5%
to -2.7% on the same stages. The change is smaller than the measurement's own
noise. Consistent with the probe: 33% of the edge subset is ~7 us.

Reverted in full. The consolidation itself was arguably a readability win, but
it is not worth carrying a 23-file diff and a hand-written asm barrier to buy
nothing.

### Two findings worth keeping

**GCC deletes a value-identical fast path.** The `dy == 1` arm never appeared in
the object file -- zero guards before any `idivl`. Not an inlining accident: a
three-function probe TU at `-O2 -march=pentium4` compiles

```c
if (dy == 1) return dx << 16;
return (dx << 16) / dy;
```

byte-identical to the bare divide. The compiler proves the two arms compute the
same value and cross-jumps the special case away. It survives only behind
`__asm__("" : "+r"(v))`. The property that makes such a guard *safe* -- exact
value identity -- is the same property that makes it *removable*. Any future
"cheap exact special case" needs its presence verified in the disassembly before
it is measured, or the A/B is measuring one binary against itself.

**Machine drift exceeds the effects being chased.** The first R3 capture showed
`frame` +3.0% and `r_model` +2.0%, which would have read as a clear regression --
except the same run also moved `cs2_script` +14.5% and `task_queue_run` +13.8%,
which three rasterizer branches cannot touch. Captures an hour apart differ by
more than any optimization in this track. Only paired A/B/A/B inside one
uninterrupted batch, with no builds in between, is admissible from here on.

### What this implies for the rest of the R-track

The R-track's remaining items are micro-targets of the same class, and R3 just
put a number on that class: an instruction-level win inside the raster prologue
is worth well under 1% of frame. R5 should not be attempted on an argument about
table size; it needs an actual cache-miss measurement, and the probe technique
above is how to get one -- duplicate the table lookups with perturbed indices
into a volatile sink and read the delta.
