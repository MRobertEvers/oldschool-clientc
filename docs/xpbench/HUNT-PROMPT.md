# XP frame hunt: 35.2 -> 15.2 ms. Use workflows.

Shave **20 ms** off the XP bench-wedge frame. Baseline is **35.2 ms/frame**;
the target is **15.2 ms**. That is a **2.3x speedup**.

**Find every optimization. Both axes, everywhere.**

- **Do the work faster** -- better kernels, better instruction selection, better
  cache behaviour, cheaper inner loops, fewer branches, better data layout.
- **Do less work** -- cull earlier, sort less, touch fewer faces, reuse last
  frame, cache across frames, cut overdraw, skip stages entirely.

Neither axis is privileged and neither is exempt. A 2.3x will not come from one
heroic change; it comes from stacking every win you can find, so **do not triage
toward only the fat stages and do not dismiss a small win because it is small**.
Twenty 0.5 ms wins *is* the target. Sweep the whole frame -- every stage in the
map below, including the ones that look already-optimal and the ones too small
to have been looked at before. The only ideas you throw away are the ones you
have *measured* not to work.

## Start here, do not re-derive

The benchmark queue is built, committed and passing acceptance
(`tools/bq/README.md`). **Read it before doing anything else** -- you do not run
benchmarks yourself, you enqueue them:

    python tools/bq/bq.py submitwait --label my-idea --frames 900 --reps 2 \
        --arm base=/abs/torirs-huntbase.exe \
        --arm v1=/abs/torirs-v1.exe --arm v2=/abs/torirs-v2.exe

A drainer must be running (`python tools/bq/bqd.py run`); check with
`bqd.py status`. **Batch aggressively**: a 12-arm job is ~13 min of box time,
twelve 2-arm jobs are ~54 min. Build variants in parallel locally -- that is
free -- and spend box time once.

**Baseline binary**: `torirs-huntbase.exe`, sha256 `716969438cc39b92...`,
qualified against the reference `torirs-dense.exe` (`64166bdf3c92f06b...`) at
+0.94%, inside the bimodality. Branch `perf/xp-frame-hunt` off
`perf/xp-35ms-baseline` @ `2d650ee7c`. Build with `scratchpad/build_xp.sh`.

**The map is already measured.** `docs/xpbench/map-eip-profile.txt`, from the
EIP sampler (a second thread suspends the render thread at ~1 kHz -- no
instrumentation, so it cannot misattribute one stage's cost to another the way
ablation does). Shares of render-thread time, scaled to the 35.2 ms baseline:

| stage | share | ms | note |
|---|---|---|---|
| `gouraud_tri_opaque_s4_asm` | 15.6% | 5.50 | hand-tuned asm, largely closed |
| `textri_opaque_lerp8_v3_asm` | 13.7% | 4.83 | hand-tuned asm, largely closed |
| **occlusion culling** (5 fns) | **13.0%** | **4.57** | `ground_tile_hidden` alone is 3.42 |
| `painter_paint_bucket` | 9.3% | 3.29 | |
| ntdll.dll | 7.9% | 2.77 | **suspect** -- see caveat |
| `ComputeProjectedFaceOrder` | 7.0% | 2.46 | per-face sort |
| projection (2 fns) | 6.2% | 2.17 | |
| `ToriRS_FrameNextCommand` | 5.0% | 1.75 | why is command decode 1.75 ms? |
| other C raster paths | 4.9% | 1.72 | |

Caveat you must carry: the profiled build ran **39.24 ms vs the 35.2 baseline**,
so the sampler costs ~4 ms and some of that ntdll share is plausibly
suspend/resume rather than real work. Confirm ntdll independently before
spending a build on it -- but if it *is* real, 2.77 ms in ntdll on a per-frame
path is almost certainly heap churn, and that is a pure-win target.

Read that table as a work list, not a ranking. The three fat non-kernel targets
-- occlusion 4.57, painter 3.29, face order 2.46 -- total **10.3 ms** and none
is a closed door, so they are the obvious place to start. But the bottom of the
table matters too: `FrameNextCommand` at 1.75 ms and "other C raster" at 1.72
are each larger than several wins you will need to hit 15.2, and the tail below
the top-40 cutoff is another ~2 ms that nobody has ever looked at. **Account for
the whole 35.2 ms.** If a stage is not in the table, find out what it costs
rather than assuming it is free.

## Closed doors: measured shut, do not reopen

pmuludq vs imul - whole-prologue divsd - whole-prologue reciprocal - packed
divps - the 8 KB reciprocal table - an SSE2 gouraud fill body - gouraud edge
divides via table - texture exact-block reciprocal refine - texture linear
blocks - SSE2 projection - the scanline raster family - NT stores in any span
kernel - the world3d painter.

If an idea sounds like one of these, **name which one and say why the new
evidence differs, before you spend a build on it.**

Also binding, read them: `raster-is-span-setup-bound`,
`gouraud-dominates-face-and-area`, `xp-perf-instrumentation-dominates-frame`,
`scene-small-csr-sorter-slower`, `v3-raster-refactor-slower-on-xp`,
`scanline-family-slower-than-default`, `d3d9-zbuffer-perf-fixes`,
`ablation-misattributes-latency`, `stderr-write-costs-6ms`,
`scene-scratch-8k-vertex-cap`.

## Use workflows. This is an explicit opt-in to multi-agent orchestration.

Run **several workflows in sequence**, and read each result before deciding the
next phase. Do not write one giant workflow.

1. **Deepen the map, exhaustively.** The EIP profile names functions, not
   reasons. Fan out readers over *every* row -- occlusion, painter, face order,
   both asm kernels, projection, `FrameNextCommand`, the C raster paths, the
   ntdll question, and the sub-1% tail. For each: what work is being done, how
   many times per frame, how much of it is redundant across frames or across
   faces, and what the per-item cost implies about where it is bound (issue,
   branch, cache, memory). Counts, not adjectives. End with a full 35.2 ms
   budget in which everything is accounted for.
2. **Ideate wide, on both axes.** Every lens, applied to every stage:
   *algorithmic* (asymptotics, better culling hierarchy, cheaper sort),
   *cross-frame* (temporal coherence, caching, incremental update),
   *data layout* (SoA, cache footprint, dedup, packing), *structural* (skip a
   stage, merge stages, reorder traversal), *kernel* (instruction selection,
   scheduling, unrolling, branch removal, addressing modes, P4-specific
   stalls -- partial-register, store-forwarding, denormals, trace-cache), and
   *build* (inlining, PGO, alignment, LTO behaviour, calling convention). Each
   agent checks the closed-doors list itself. Generate far more ideas than you
   can build; that is the point of the phase.
3. **Cost and rank.** Expected ms with the reasoning shown, plus build cost and
   correctness risk. **Keep the small ones** -- a 0.3 ms win that is cheap to
   build is worth having, and forty of those is the target. Rank by ms-per-hour
   of build effort, not by ms alone, and say which ones stack and which ones
   overlap (two ideas that both remove the same 2 ms are worth 2 ms, not 4).
4. **Build in parallel, benchmark in one batch.** Use worktree isolation for
   agents that edit; verify each arm's asm kernels with `nm` before it goes in
   the job.
5. **Adversarially verify every win.** An independent agent whose job is to
   *refute* it: wrong binary, arm order, bimodality, a stage skipped rather
   than sped up, an ablation flag that dropped the asm kernels, output that
   silently changed. A correctness regression that renders fewer faces will
   look exactly like a win.
6. **Loop until dry** -- two consecutive rounds with nothing new, or 15.2 ms.

## The measurement contract, non-negotiable

900 frames - `TORIRS_PERF` **unset**, not `=0` - `SDL_VIDEODRIVER=dummy` -
palindrome arm order - at least 2 reps - report best-of **and every run**. The
queue enforces all of this, which is why you use the queue.

**The box is bimodal by ~7.5%. A difference under ~8% is not a result until
re-run.** Say that plainly rather than banking it. For anything smaller, use the
ABBA harness (`TORIDRAW_FRAME_AB=1`) -- it alternates arms `A B B A` inside one
process, which cancels the drift that makes run-to-run comparison useless.

Never time a binary built with `TORIDRAW_ABLATE`, `TORIDRAW_SPAN_CENSUS` or
`TORIDRAW_SPAN_TRACE` in `TORIDRAW_PROBE_CFLAGS` -- they trip a makefile gate
that silently drops all four handrolled asm kernels. Clean build every time,
`rm -rf` the objdir, no exceptions.

## Code rules

CLAUDE.md at repo root is binding: `assert()` on a bad input parameter, never an
early return; one `assert()` per condition; an allocation failure is an assert,
not an `if`. No 64-bit arithmetic in toridraw kernels. A new kernel needs a C
reference twin plus a compare-mode test and keeps its scalar tail. When you
replace a clear with a maintained invariant, the verifier goes inside
`assert()` -- `OPT=1` defines `-DNDEBUG`, so it vanishes from the shipping lane.

Never bare `git stash` / `git stash pop` -- the stash stack is shared across
worktrees. Never `git prune`.

## Done

A branch off `perf/xp-35ms-baseline` measuring **<= 15.2 ms/frame** through the
queue; one commit per win, each carrying its measurement, binary sha256 and
exact build command; and a ledger of what was tried and rejected **with
numbers**, including the losers.

**If you fall short, say so plainly with the number you reached and what the
remaining ideas were. Do not round a 26 up to "close to target."** Given that
this is a 2.3x, falling short is a likely honest outcome and reporting it
accurately is worth more than a flattering summary.
