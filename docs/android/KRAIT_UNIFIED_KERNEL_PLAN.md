# Krait Unified Kernel Plan

Date: 2026-09-05
Target: Motorola XT1060, Qualcomm Krait 300 / msm8960dt, ARMv7-A,
        two CPU cores, Adreno 320, the existing GLES2 painter renderer.

**Status: implementation and validation of this optimization pass are complete.
See [KRAIT_RENDERER_RESULTS.md](KRAIT_RENDERER_RESULTS.md) for the final combined
measurements and verification scope. The dated progress sections below retain
intermediate findings, including rejected candidates. No scene-painter
optimization, Z-buffer, or global material reordering was introduced.**

The design objective is an ordered GPU model pipeline with fewer intermediate
representations, fewer dependent metadata lookups, small hot kernels, and
controlled CPU-to-CPU handoffs. "Unified" describes ownership and dataflow. It
does not mean putting every model case into one enormous inlined function.

## 1. Scope and the ordering boundary

~~~text
                      OUT OF OPTIMIZATION SCOPE
                 +--------------------------------+
                 | Scene painter                  |
                 | tile / wall / spanning-object  |
                 | dependencies and model order   |
                 +----------------+---------------+
                                  |
                     ordered commands: A B C D ...
                                  |
==================== PRESERVED ORDER BOUNDARY ====================
                                  |
                      IN OPTIMIZATION SCOPE
                 +----------------v---------------+
                 | Command resolution / dispatch  |
                 +----------------+---------------+
                                  |
                 +----------------v---------------+
                 | Pose / bounds / project / pick |
                 +----------------+---------------+
                                  |
                 +----------------v---------------+
                 | Face cull / depth / priorities |
                 +----------------+---------------+
                                  |
                 +----------------v---------------+
                 | Residency / indices / actors   |
                 +----------------+---------------+
                                  |
                 +----------------v---------------+
                 | Upload / ordered GL submission |
                 +----------------+---------------+
                                  |
                 +----------------v---------------+
                 | Adreno vertex + fragment work  |
                 | depth test OFF, depth writes OFF|
                 +--------------------------------+
~~~

Independent preparation may overlap. Submission must still commit A, B, C, D
in the scene painter's order. Model-local face ordering has its own exact
contract; changing the processing schedule cannot change that order either.

The optimization covers the world GPU path first. UI model widgets and software
rasterization retain their required intermediate outputs. In particular, the
GLES2 UI code does read orthographic vertex arrays. A world-only optimization
must not silently remove those arrays from UI projection.

## 2. Evidence, confidence, and current measurements

### 2.1 Hardware model used for design

The local measurements in
[arch_fuzz/FINDINGS.md](../arch_fuzz/FINDINGS.md) are the starting evidence.
Its implementation and generated-kernel methodology were also reviewed.

~~~text
Observed / inferred property          Local result        Design consequence
-----------------------------------  ------------------  ----------------------------
Independent simple integer work      ~2.5-2.6 instr/cyc   expose independent operations
Dependent plain ADD chain            ~1.67 cyc/op        avoid serial cursor chains
Dependent shifted ADD                ~3 cyc/op           price address-generation chains
Integer multiply throughput          ~one / 2 cycles     don't move SIMD math to scalar
NEON Q integer ADD latency           ~3 cycles           interleave independent vectors
NEON Q integer MUL latency           ~4 cycles           separate producer and consumer
Tested NEON arithmetic throughput    ~one op/cycle       vector width, not instruction count
ARM <-> NEON lane round trip          ~7 cycles           cross domains in blocks
Branch miss penalty in probes        ~12.6-13.4 cycles   remove unpredictable hot decisions
Effective short branch history       ~8-9 branches       avoid relying on long toy patterns
Return-stack knee in probes          ~8 nested calls     avoid deep dispatch stacks
Effective reorder-window knee        ~72-96 instructions don't expect OOO to hide all DRAM
Independent DRAM chase scaling       ~4-5 useful misses  bound the prefetch/work window
~~~

The width/port descriptions are an effective scheduling model, not a verified
Qualcomm floorplan. The asymmetric-forwarding explanation fits several probes;
it does not prove the internal scheduler round-robins across particular ALUs.
Likewise, the branch-history and return-stack sizes are inferences from tested
patterns, not permission to assume a production branch always behaves that way.

In particular:

- The approximately seven-cycle round trip does not establish equal cost in
  each direction. Avoid per-face transfers without assuming a universal
  3.5-cycle cost for every transfer form.
- "One NEON pipe" is a useful resource-pressure model for the tested arithmetic.
  Interleaved loads, shuffles, conversions and scalar/VFP aliases still need
  their own instruction-level treatment.
- Predicted branches can overlap other work. Charging every branch its isolated
  back-to-back cost overestimates a real loop.
- Power-of-two indirect-call patterns in a microbenchmark are not a policy for
  organizing model types in a real renderer.
- Cycles are not universally immune to DVFS: CPU frequency changes the number
  of core cycles spent waiting for independently clocked memory and buses.
- The old research harness can fall back to estimated cycles. Those results
  are excluded from this project's performance evidence.
- The measured ARM move/zero idioms do not behave like x86 move elimination
  or dependency-breaking XOR. Price real moves and shuffles; use an independent
  immediate assignment when a scalar dependency must actually be broken.
- A zero dTLB miss event is not evidence of zero TLB misses. The local research
  explicitly identifies that event as unhelpful on this device.

### 2.2 Cache and memory model

~~~text
                          EACH KRAIT CORE
     +--------------------------------------------------------+
     | small instruction/data working sets                    |
     |                                                        |
     | L0-I: ~4 KiB        L0-D: ~4 KiB, direct mapped         |
     |                     ~3-cycle dependent hit              |
     |          |                         |                    |
     | L1-I: ~16 KiB       L1-D: ~16 KiB, 4-way               |
     |                     ~6-cycle dependent hit              |
     +----------+-------------------------+--------------------+
                |                         |
                +------------+------------+
                             |
                SHARED L2: ~512 KiB, ~32 cycles
                             |
                    DRAM: ~330-360 cycles
                  under the measured conditions

     Data line inferred: 64 B
     Instruction line inferred: 128 B
     L0/L1 data conflict period in the probes: 4 KiB
~~~

The pointer-chase results are dependency latencies, not the cost of every load
in a renderer. Multiple requests can overlap. A streaming copy has a different
cost from a dependent pointer chain of the same byte count.

The local streaming probes show substantial gains from prefetch for DRAM-sized
streams, but some cache-resident streams get worse with prefetch. Therefore:

1. Prefetch dependent metadata ahead of its eventual use.
2. Prefer model arrays only after the model survives an inexpensive bound test.
3. Keep the number of speculative lines bounded.
4. Price extra instructions, L0/L1 eviction and TLB reach.
5. Verify which physical effect the counters support.

No blanket "prefetch everything" or "pad every array" policy is planned.

### 2.3 Real-chain baseline

The captured workloads contain 59,352 model calls over 20 consecutive passes:

~~~text
Corpus             Calls    Visible   Near-clipped   Priority-bearing calls
-----------------  -------  --------  -------------  ----------------------
Lumbridge           10,604     2,380         0                 1,284
GE moving camera    41,988    26,864       130                12,388
Varrock low camera   6,760     4,156        72                 1,784
~~~

All twelve priorities occur across the captures, including priorities 10 and 11.
They also include full, shared and lent model handles, picking hits, and real
animated-geometry snapshots. There are currently no dynamic actors in these
offline captures.

Saved hardware medians, using each corpus's original baseline executable:

~~~text
Corpus             Cycles / chain    Instructions / chain
-----------------  ---------------  --------------------
Lumbridge              2,272.203             972.801
GE moving camera       4,021.555           2,100.724
Varrock low camera     5,074.911           3,039.922

Varrock additionally:
  branch misses / chain:          16.401
  L1 data-load misses / chain:    28.227
~~~

These are CPU model-chain measurements, not frame times or FPS. Full samples,
binary hashes, corpus hashes and median absolute deviations are in
[benchmarks/krait_model_chains](benchmarks/krait_model_chains/README.md).

For scale only: 16.4 branch misses times a roughly 13-cycle recovery is about
213 cycles, considerably smaller than a 5,075-cycle chain. Conversely, charging
all 28.2 L1 misses a 350-cycle DRAM penalty would exceed the chain's total.
Neither multiplication is an attribution model. Misses can hit L2, overlap
each other, and overlap branch recovery or arithmetic.

This is why removing instructions alone, eliminating all branches, or treating
every cache miss as DRAM is not a sound optimization strategy.

### 2.4 What the first fusion prototype actually showed

The first prototype makes projection produce packed vertices for the sorter.
It is in the isolated experimental checkout; it is not an accepted change in
the main checkout.

~~~text
GE chain, within each executable          OFF cycles    ON cycles     ON change
--------------------------------------  ------------  ------------  ----------
Prototype A                               4,383.285     4,245.650      -3.14%
Prototype B                               4,035.979     4,073.464      +0.93%
~~~

A and B are different executables, so their controls are not interchangeable.
A's apparent win is insufficient to show improvement over the unmodified
renderer. B does not support accepting that revision. Both candidates also
increased instructions relative to their own controls.

The stored results are
[A off](benchmarks/krait_model_chains/experiments/projection-pack-A-off.json),
[A on](benchmarks/krait_model_chains/experiments/projection-pack-A-on.json),
[B off](benchmarks/krait_model_chains/experiments/projection-pack-B-off.json),
and [B on](benchmarks/krait_model_chains/experiments/projection-pack-B-on.json).

The correction to the design is important: fewer logical passes are not enough.
The fused producer can lengthen live ranges, spill registers, enlarge code, or
move hot state into worse cache positions. The next iterations must address
the complete producer/consumer contract and inspect the generated assembly,
rather than simply attaching another output to the existing projection body.

## 3. The current CPU dataflow and its costs

~~~text
Ordered command
      |
      +-> resolve element -> model -> pose / batch metadata
      |
      +-> apply pose if needed
      |
      +-> fast bound reject -------------------------------> ordered rejection
      |
      +-> project vertices
      |       |
      |       +-> X32[]  Y32[]  Z32[]
      |       +-> orthographic XYZ for textured projection
      |       +-> projected bounds
      |
      +-> final bound reject ------------------------------> ordered rejection
      |
      +-> picking, when applicable
      |
      +-> reread X32[] / Y32[] / Z32[]
      |       |
      |       +-> packed XYZ16[] or XYZ32[]
      |
      +-> gather three corners per face
      +-> winding + depth
      +-> keys[], rejected lanes represented by sentinels
      +-> compact accepted keys
      +-> bitonic or stable depth radix sort
      +-> priority partition / flexible-priority insertion
      +-> int32 face order[]
      |
      +-> worker copies int32 order[] to result arena
      |       (dual-core lane only)
      |
      +-> draw resolves residency
      +-> reread face order and make U16 triangle indices
      +-> adjacent compatible sequence merging
      +-> upload and draw
~~~

Four distinct categories require different remedies:

~~~text
Cost class                     Example                        Primary remedy
-----------------------------  -----------------------------  -----------------------------
Dependent metadata latency     element -> model -> pose       compact descriptors + lookahead
Arithmetic dependency / issue  winding, depth, transforms     packed SIMD + independent chains
Intermediate memory traffic    projected arrays, order copies ownership + output-format changes
Dispatch / synchronization     callbacks, claim, publish      coarse decisions + bounded handoff
~~~

A faster face-sort leaf cannot eliminate the other three categories. Equally,
reducing command overhead does not justify changing the ordering algorithm.

## 4. Proposed unified dataflow

The target is a small family of kernels behind one GPU-model contract.

~~~text
     Immutable/versioned mesh descriptor + ordered instance command
                                   |
                    bounded descriptor lookahead
                                   |
                       cheap model bounds check
                         /                 \
                      reject              survive
                        |                    |
                 publish ordinal       choose ONE model path
                 without geometry       /       |        \
                                       /        |         \
                                  tile leaf  common Krait  rare/general
                                               path         fallback
                                                 |
                                  project into consumer-ready state
                                                 |
                                     picking from valid projected state
                                                 |
                                  SIMD winding/depth -> stable keys
                                                 |
                                 stable depth order + exact priorities
                                                 |
                              output directly to caller-owned order arena
                                      /                       \
                              static U16 IDs            general I32 order
                                      |                       |
                              retained index pack       actor/fallback bake
                                      \                       /
                                       ordered draw sequence
                                                 |
                                     contiguous uploads / GLES2
~~~

The preserved interfaces are explicit:

- Scene command ordinals are unchanged.
- The mathematical projection and winding rules are unchanged.
- The priority merge has the same threshold values and strict comparisons.
- Output ownership is explicit and survives until the consumer finishes.
- Unsupported models retain the existing general implementation.
- Model class decisions occur once per model, not once per face or vertex.
- Small terrain models keep small leaf functions.

## 5. First priority: remove order copies and reduce the GPU order width

This has a clearer resource budget than the initial projection-fusion attempt.

A valid face ID in the supported model range fits in U16. The renderer currently
uses I32 face-order arrays even though static resident draws ultimately consume
U16 vertex indices.

For a no-priority dual-core model with F accepted faces, ignoring sort-internal
key traffic other than its final read (priority partition traffic is separate):

~~~text
CURRENT
  sorted keys --read 4F--> emit I32 scratch --write 4F-->
  I32 scratch --read 4F--> worker I32 arena --write 4F-->
  worker arena--read 4F--> final U16 indices --write 6F-->

  Read: 12F bytes       Write: 14F bytes       Total: 26F bytes

PROPOSED STATIC PATH
  sorted keys --read 4F--> caller-owned U16 face arena --write 2F-->
  U16 arena   --read 2F--> final U16 indices          --write 6F-->

  Read:  6F bytes       Write:  8F bytes       Total: 14F bytes
~~~

That is a reduction in this portion of memory traffic, not a promised
46% renderer speedup. Priority-bearing models still need their ordering work,
and metadata, projection, cache misses and GPU costs remain.

Implementation approach:

1. Add a GPU-specific destination contract to the final order emitter.
2. Supply the destination arena before sorting; remove the temporary order copy.
3. Emit U16 face IDs directly for eligible static models.
4. Use a U16-input NEON index packer, selected once per model.
5. Keep the I32 API for software rasterization and consumers that need it.
6. Keep explicit fallback for dynamic actor baking until that consumer supports
   the new representation and has its own real-chain coverage.

Do not add an indirect callback per emitted face. Use a small set of compiled
emitters selected once at model entry.

Priority models must emit the final priority order into the destination.
Narrowing the pre-priority depth order and assuming it is final would be wrong.

Ownership and fallback rules:

~~~text
reserve enough output
        |
        v
sort / priority-resolve into reserved span
        |
        v
publish {ordinal, format, count, offset}
        |
        v
consumer reads immutable span
        |
        v
frame completion permits arena reuse
~~~

Reservation failure must choose the complete old path before publishing partial
output. One model cannot partly use a new arena and partly use stale scratch.

## 6. Projection and culling: share work without creating spills

### 6.1 Remove outputs the world GPU path does not consume

Textured CPU projection can produce orthographic XYZ arrays for software
texture mapping. World GPU submission uses baked model geometry/UV data.

A world-only projection path can omit those orthographic stores if the consumer
audit and image verification confirm that no world fallback reads them. The
GLES2 UI path does read them, so the request must be explicit and scoped to the
world painter's model stage. It must be reset after the call.

Near clipping and picking are separate requirements. Omitting a software
texture basis does not permit omitting data needed by picking.

### 6.2 Revisit projection-to-sort packing with a register budget

Current projection writes 12 bytes per vertex of screen XYZ. A subsequent
narrow-packing pass reads those 12 bytes and writes an 8-byte packed record.

~~~text
Current vertex-state traffic in these two stages:
    projection write 12V + packing read 12V + packed write 8V = 32V bytes

Naively fused:
    projection write 12V + packed write 8V                  = 20V bytes
~~~

The saving is 12V bytes of logical traffic. It can disappear if fusion produces
spill traffic. For example, one 16-byte spill and reload per four vertices costs
8 bytes per vertex; two such pairs exceed the logical saving.

Illustrative liveness budget, not a measured allocation diagram:

~~~text
                         EARLY PROJECTION       LATE PROJECTION       PACK
Separate stages          transform temporaries  XY bounds/outputs     XYZ + Z bounds
                         [---------- live ----------]                [-- live --]

Naively fused            transform temporaries + XY bounds
                                          + packed outputs + Z bounds
                         [---------------- peak live range ----------------]

ARMv7 NEON register file is shared by these values and constants.
Extra live accumulators can force stores/loads even when logical work is lower.
~~~

Before another fusion revision is accepted:

- inspect Q/D-register live ranges and callee-save traffic;
- count stack loads/stores in the affected loops;
- measure L1 data misses and, where validated, instruction-cache misses;
- keep flags outside the vertex loop;
- remove unused outputs before adding new ones;
- compare the whole producer/consumer chain, not the producer alone.

Possible refinement: derive the sort's depth span from accepted face keys
instead of carrying extra Z-bound accumulators through the most crowded part
of projection. This trades a compact key reduction against producer spills.
The reduction is only useful when the selected sort needs it; do not add it to
every tiny model indiscriminately.

### 6.3 Preserve the exact arithmetic

The depth divide is the existing fixed-point operation, not mathematical
integer division by three. For example, with the current multiplier/shift:

~~~text
  (3 * 21845) >> 16 = 0
   3 / 3           = 1
~~~

Replacing it with hardware SDIV would change ties, buckets and priority
thresholds. Hardware divide is only a candidate at sites where its semantics
match the existing operation exactly.

Packed XY coordinates may use low words without rebasing only under a proven
extent bound:

~~~text
  actual coordinate difference fits signed 16 bits
                       |
                       v
  subtracting low words modulo 65536 yields the same signed difference
                       |
                       v
  original winding sign is preserved
~~~

The existing extent guard, signed-Z guard, winding-width assumptions and
near-clipped fallback remain mandatory. No fast-math flag, altered reciprocal
refinement, FMA contraction, or changed scalar projection tail is accepted
without exact-output verification.

The existing overlapped K16 face tail is integer-exact. That does NOT prove an
overlapped SIMD projection tail matches the current scalar divide tail.

### 6.4 Bounds-first microbatches

The Lumbridge corpus rejects most model calls before output. Fetching face and
vertex arrays for every future command can evict useful data for geometry that
will never be processed.

A later static-model path will test bounded microbatches:

~~~text
  descriptor A B C D       cheap bounds A B C D
         |                         |
         +--> metadata prefetch    +--> reject B,D
                                          |
                                  arrays prefetch A,C
                                          |
                                   project/sort A,C
                                          |
                      commit A, rejected-B, C, rejected-D
~~~

This is preparation batching, not scene/model-order sorting. It can expose
independent bound calculations and dependent loads across several models.

Start with static, unposed commands inside one 3D pass. Flush the microbatch at
pass boundaries, scene events, animation ownership hazards, or unsupported
commands. Animated bounds cannot be evaluated before their required pose.

## 7. Stable sorting and priorities on Krait

### 7.1 Split the work by execution resource

~~~text
NEON domain                              Integer / memory domain
--------------------------------------   ---------------------------------
load packed corners                     output cursors / counts
winding arithmetic                      histogram accumulation
face-depth arithmetic                   prefix / range metadata
mask rejected lanes                     stable scatter
bulk key/order conversions              priority thresholds and band control
~~~

The goal is useful overlap and fewer crossings, not transferring every scalar
operation to NEON or every small SIMD operation back to ARM.

~~~text
Bad per-face synchronization:
  NEON face -> extract scalar -> branch/cursor -> rebuild vector -> next face
       <---------------- repeated dependency -------------------->

Target block handoff:
  NEON: [faces 0..7] [faces 8..15] [faces 16..23]
  ARM:               [consume 0..7] [consume 8..15]
                     bulk state crosses the boundary
~~~

The latter is a design target. Memory aliasing, store forwarding and the single
NEON pipeline may limit overlap; inspect and measure the actual schedule.

### 7.2 Choose the sort by accepted count and depth span

The current bitonic/radix crossover was previously evaluated with narrower
workloads. The real-chain harness now allows a better decision.

~~~text
accepted faces N, exact usable depth span D
                    |
      +-------------+------------------+
      |             |                  |
    N <= 2      short register run   enough keys / shallow span
      |             |                  |
 compare/swap   compact network     stable counting/radix
                                          |
                              wide span -> existing multi-pass fallback
~~~

A shallow counting pass has roughly O(N + D) work. A bitonic network has roughly
O(N log^2 N) compare/exchange work, performed largely on Krait's constrained
SIMD resource. Small networks can still win because histogram clearing and
prefix setup are not free.

The selector will be justified by the workload distribution and PMU counts.
It will not be a blind sweep of arbitrary thresholds. All ties must still
resolve to original face-index order.

Candidate pass fusions include accepted-key compaction with histogram work,
and the final stable scatter with a consumer-ready representation. They must
account for rejection handling: adding an unpredictable branch to each face can
cost more than the removed pass.

### 7.3 Priority handling is not a comparator shortcut

~~~text
First obtain depth order, with face-index ties
                     |
            partition by priority
                     |
      +--------------+----------------------+
      |                                     |
 fixed bands 0..9                    depth-ordered priority 10
      |                                     |
      |                              then priority 11
      |                                     |
      +----------- threshold insertion -----+
~~~

Exact output schedule:

~~~text
flexible faces with depth > average(1,2)
fixed bands 0,1,2
flexible faces with depth > average(3,4)
fixed bands 3,4
flexible faces with depth > average(6,8)
fixed bands 5,6,7,8,9
remaining flexible faces
~~~

The flexible stream is priority 10 followed by priority 11; it is not a
depth-merge of the two. Comparisons are strict greater-than. Equal depths,
empty groups, non-monotonic thresholds, uniform-priority models and the switch
from priority 10 to 11 must all retain reference behavior.

Do not remove transparent/hidden faces before the priority calculations merely
because their GPU triangles appear dispensable. A face that participates in
the reference depth/priority accumulation can affect another face's insertion
threshold even if its own pixels are invisible.

Priority storage is a locality problem, but the existing padding experiment
was a measured null. Do not repeat padding changes without evidence of conflicts
in the actual active bands. A compact per-model partition or direct final
emitter is a more substantial candidate, subject to cursor-dependency costs.

## 8. Dispatch, instruction-cache footprint, and branch prediction

### 8.1 Resolve stable facts once

At model/resource publication, maintain a versioned descriptor for facts such
as model kind, immutable face pointers, counts, tile eligibility and compatible
output format. Per-instance position, pose and clipping decisions remain live.

Invalidation must cover replacement, unsharing, in-place edits through the
write API, sequence changes, morphs and recycled element IDs. Existing
model_revision/pose bookkeeping is useful evidence, not a license to cache
untracked writes.

~~~text
Current repeated resolution:
  frame -> element -> kind -> model -> fields
  stage -> kind -> model -> fields
  sort  -> kind -> model -> fields
  draw  -> pose table -> track -> batch entry

Target:
  validated resource descriptor ----+
  ordered instance command ---------+--> one model classification
  pass camera / renderer state -----+--> specialized complete path
~~~

Do not replace validity checks with an unchecked pointer cache. Prefer one
verified lookup whose result is carried to its consumers.

### 8.2 Keep hot cases small

The renderer already has valuable two-face sorting and four-vertex projection
leaves. Preserve them. They should not pay the stack frame and callee-save
traffic of a large clipped, textured, priority-bearing model.

Use a small decision tree such as tile / common static / general fallback.
Do not group or reorder model commands by that classification.

For each specialized entry, inspect:

- actual instruction bytes and hot loop footprint;
- direct versus indirect calls in the inner path;
- stack-frame size and register saves;
- repeated constant loads and large shifted-address chains;
- whether the compiler merged supposedly separate cases back together.

The research's ~4 KiB and ~16 KiB instruction-cache knees are design budgets,
not guaranteed whole-renderer speedup factors. A giant unrolled network can
save loop branches and still lose by expanding the instruction working set.

Branchless code is appropriate when measured unpredictability justifies its
extra work. Rare clip/overflow/error handling should usually stay outside the
common body. A correctly predicted branch that skips substantial work is useful.

## 9. Memory latency and lookahead

The existing renderer already has staged prefetching. Extend its ownership and
layout coherently instead of adding independent prefetch ladders everywhere.

~~~text
Command distance ahead        Work whose address is becoming available
---------------------------   -------------------------------------------
+4                            element / descriptor node
+3                            model metadata and bounds
+2                            dependent pose/batch metadata
+1 or post-bound-survival      required vertex/face arrays
 current                      compute and consume
~~~

These distances are illustrative starting points, not fixed magic numbers.
A two-face tile and a large model provide very different amounts of lead work.
Pass markers terminate lookahead; element IDs after a world-view transition
must be resolved in the correct view.

Specific constraints:

- preserve retained static geometry instead of returning to 84-byte face gathers;
- keep transient outputs contiguous and avoid per-model allocation in steady state;
- provision arenas before the measured/model-processing interval;
- do not make CPU0 and CPU1 repeatedly write different words of one cache line;
- do not pad every result to a full line if that pushes the working set out of L2;
- preserve sparse-resource identity and generation validation;
- measure spills and scratch layout before changing cache-line alignment.

The current replay preserves whole-model sharing, but not every original
cross-model shared-face-pool allocation or original heap placement. It is a
strong arithmetic/ordering oracle and controlled workload, not an exact replica
of the app's entire pointer graph. Dispatch and allocation-layout claims need
the corresponding replay extension and real-app confirmation.

## 10. Two-core scheduling and publication

The current dual-core lane already translates commands on the draw thread and
feeds model work to a worker. It also lets the draw thread claim work. The plan
builds on that implementation; it does not assume the old duplicated frame-bus
walk still exists.

~~~text
DRAW CORE                     SHARED STATE                  MODEL CORE
---------                     ------------                  ----------
translate A,B,C  -----------> bounded command feed
                              ownership / ordinal -------> claim A
                                                          pose/project/sort A
read A result   <------------ publish A <------------------
pack/draw A                                                work on B
read B result   <------------ publish B <------------------
pack/draw B
...
join / lifetime boundary
~~~

The release/acquire protocol is a correctness requirement. ARMv7 memory
barriers cannot be removed because one replay happened to work.

The scheduling target is bounded outstanding **work**, not a large number of
queued model slots. A tile and a large actor do not have equal cost.
Coarser claims/publications are candidates only after proving:

1. the consumer can still advance in order;
2. exhaustion has a complete fallback;
3. there is no deadlock at pass close or frame end;
4. two cores do not mutate/read incompatible poses of the same model;
5. a result and its geometry remain alive through actor baking and GL staging.

Repeated use of the same mutable model with different poses is a particular
hazard: publishing face order is not sufficient if a later worker pose changes
the vertices the draw thread is about to bake. Preserve ownership, snapshot the
necessary geometry, or keep that case on the serial path.

Before tuning lead/claim size, extend the replay to include real animation
evaluation, actor baking, and a two-thread feed/consumer schedule. The current
single-thread chain reproduces result publication but not real contention.

Both CPU cores must actually be online when a dual-core test is claimed. CPU1
hot-unplug and a failed affinity request must not be silently interpreted as a
two-core measurement. Record counters for each thread in separate runs where
needed, and report both total work and critical-path/stall evidence.

## 11. Retained geometry, index packing, and GPU submission

~~~text
STATIC RESIDENT MODEL                  DYNAMIC / NONRESIDENT MODEL
---------------------                  ---------------------------
keep baked vertices on GPU             pose/bake or required gather
produce ordered indices                produce ordered vertex stream
          |                                         |
          +-------------------+---------------------+
                              |
                 append in original painter order
                              |
             merge only ADJACENT compatible items
                              |
                 contiguous staging and upload
                              |
                  ordered GLES2 draw calls
~~~

Preserve the resident ring's live-range protection: a placement cannot overwrite
vertices referenced by draws that have not consumed them. Preserve the U16
window bound, lap/wrap handling, compaction constraints and large-model fallback.

For U16 face IDs, eight IDs fit in one Q register. The packer can derive the
three vertex indices with narrow arithmetic where the proven U16 window bound
makes that exact. Invalid-face handling remains defined; narrowing is not a
substitute for validating eligibility.

Actor baking and the fallback gather need their own real workloads. Avoid
per-face library memcpy calls and schedule valid source-line prefetch only
when that path is sufficiently frequent and cache misses support the change.

GPU validation must preserve cutout/alpha behavior, texture animation, clipping,
UI composition and the exact submission order. Depth testing and depth writes
stay disabled. No global texture/material sort is permitted.

The installed backend is GLES2. Its programmable pipeline is vertex/fragment,
not a compute-shader sorting interface. Moving the whole sort to the GPU would
require a different design and potentially synchronization/readback; it is not
a free replacement for this CPU painter pipeline.
See the [GLES2 specification](https://registry.khronos.org/OpenGL/specs/es/2.0/es_full_spec_2.0.pdf).

KGSL exposes a gpubusy interface on the attached phone. Its presence has been
confirmed, but an idle read is not a rendering profile. Establish the driver
counter's semantics, interval and reset behavior before using it as evidence.
Do not assume the historical "about 2% busy" note describes the present workload.

## 12. Measurement ladder and promotion gates

~~~text
Known-good executable + frozen real corpus
                    |
                    v
       exact-output / lifetime verification
                    |
                    v
       generated-code and traffic inspection
                    |
                    v
       one hardware event, fixed real work
                    |
                    v
       control / candidate / control repetitions
                    |
                    v
       ALL representative real-chain corpora
                    |
                    v
       extended actor / dispatch / two-core replay
                    |
                    v
       real-app hardware counters + visual checks
                    |
                    v
       promote measured change; retain regression coverage
~~~

The promotion rules are:

- Unavailable or multiplexed counters fail; no wall-clock or inferred-cycle fallback.
- Use raw cycles as the primary CPU cost, with instructions, branch misses and
  cache misses as corroboration. Validate any newly added event first.
- Compare equal workload denominators and exact inputs.
- Include between-run control variation, not just within-run MAD.
- Compare against the saved original executable as well as an off-switch in
  the modified executable. Added code can make that modified control worse.
- Require an effect that clearly exceeds measured variation and justifies its
  complexity. A smaller instruction count alone is not a win.
- A component improvement is not automatically a chain improvement.
- A chain improvement is not automatically a frame/FPS improvement.
- Validate ordinary, clipped, large, priority-bearing and moving-camera cases;
  add dynamic actor coverage before changing that path.
- Keep a change out of the main renderer if its apparent gain depends on
  weakened ordering, omitted work, capture instrumentation, or a handicapped control.

Software clocks may control timeouts or waits; they are not profile evidence.
The [Simpleperf counter guidance](https://android.googlesource.com/platform/system/extras/+/android16-release/simpleperf/doc/executable_commands_reference.md)
also documents why multiplexed events need care. On this device the harness
uses one pinned hardware event at a time and checks enabled/running coverage.

## 13. Execution sequence and completion criteria

~~~text
Step  Status       Work                                  Required exit evidence
----  -----------  ------------------------------------  ------------------------------------
D0    COMPLETE     real-chain capture / replay           exact outputs + usable PMU baseline
U1    INTEGRATED   direct I32 destination                chain gains; U16 version rejected
U2    REVIEWED     projection consumer layout            fused repack rejected; exact fallback retained
U3    VALIDATED    stable sort / priority processing     compact4 accepted; sparse alternatives rejected
D1    VALIDATED    descriptor + coordinated prefetch     capture, invalidation suite, app PMU
A1    VALIDATED    prepared poses + unique vertex bake   real classic/skeletal/bake inputs; live parity
C1    INTEGRATED   cached acquires + 8-command release   concurrent tests + foreground app PMU
G1    VALIDATED    residency + shader/driver audit       KGSL hardware counters; exact GPU pixel parity
V1    VALIDATED    combined renderer changes             full-switch A/B, moving pixels, live transitions
~~~

U1 is prioritized ahead of more naive projection fusion because its ownership
and byte-traffic changes are easier to prove and it does not require carrying
additional vector accumulators through projection.

Each implementation experiment will record a short cost ledger:

~~~text
case / source revision / binary hash / corpus hash
work removed:      loads, stores, copies, branches, dispatches
work introduced:   guards, accumulators, spills, setup, lifetime management
predicted signal:  cycles + supporting counter(s)
correctness scope: paths exercised and fallback cases
observed result:   original/control/candidate counts and variation
decision:          accept, revise with a specific diagnosis, or discard
~~~

A null result is a reason to update the cost model or measurement coverage.
It is not a reason to launch a long sequence of unrelated flag sweeps.

Completion means accepted renderer changes are integrated, retain painter and
model priority behavior with no Z-buffer, improve the relevant full pipeline
under hardware-counter measurement, and survive real-device correctness checks.
A plan, a harness, or one faster microbenchmark does not satisfy that endpoint.

## 14. Code map

- [Model-chain harness and capture format](tools/perf/model_chain_replay.c)
- [Harness commands and detailed measurement contract](docs/krait_renderer.md)
- [Captured data and baseline counts](benchmarks/krait_model_chains/README.md)
- [Frame command translation](src/render/torirs_frame.c)
- [GLES2 world model dispatch and submission](src/platform/platform_renderer_gles2_core.c)
- [Resident painter path](src/platform/platform_renderer_gles2_painter.c)
- [Shared U16 index packer](src/platform/platform_renderer_gles2_indices.h)
- [GL-free model stage](src/platform/platform_renderer_gles2_dualcore_stage.c)
- [Two-core scheduling](src/platform/platform_renderer_gles2_dualcore.c)
- [Projection kernels](3rd/toridraw/impl/projection/projection.perspective.prepared.neon32.impl.h)
- [Projection dispatch](3rd/toridraw/impl/projection/projection.perspective.prepared.neon32.u.c)
- [A32 face culling and key sorting](3rd/toridraw/impl/facesort/facesort.bitonic_radix.small.neon32.u.c)
- [Depth radix and shared sort dispatch](3rd/toridraw/impl/facesort/facesort.bitonic_radix.small.dispatch.u.c)
- [Priority/reference ordering](3rd/toridraw/impl/facesort/facesort.bucket.small.scalar.u.c)
- [Model/pose invalidation](3rd/toridraw/toridraw_scene.c)

## 15. Implementation evidence and revised priorities (2026-09-05)

The first retained changes target intermediate storage and synchronization,
which are costs that the original real-chain replay either includes directly
or can isolate without inventing model geometry.

### Direct I32 output is retained; narrow output is not

The production GPU worker temporarily gives the existing sorter its final
arena destination. It restores the scene's owned scratch pointer immediately
after the call, before any return or capacity fallback. The algorithm still
emits the same I32 face IDs in the exact same priority/depth order.

~~~text
old: sort -> scratch order --read F*4 / write F*4--> worker arena -> index pack
new: sort ---------------------------------------> worker arena -> index pack

capacity guard: room >= scene.max_faces ? direct destination : scratch + copy
ownership:      borrow destination -> sort -> restore owned scratch pointer
publication:    append result -> release ready (unchanged)
~~~

Paired chain runs reduced cycles by about 3.7% in Lumbridge, 5.1% in GE and
5.4% in Varrock. The full arena-capacity guard deliberately retains the old
fallback near exhaustion. U16 output, priority outlining, and sparse-depth
variants were tested and rejected: instruction reductions alone did not
reliably reduce cycles. In-process ABBA caught sparse-sort regressions of
roughly 0.8-1.4% despite fewer instructions. Their higher D-cache misses are a
reason to preserve the existing compact sorter until the memory cost changes.

### Acquire a published prefix once

The local architecture research's barrier probes report approximately 52-81
cycles for a bare DMB, rising with pending stores. These are workload-sensitive
measurements, not a fixed instruction latency guarantee. The existing lane was
paying acquire barriers again for entries that an earlier acquire already covered.

~~~text
producer                         consumer
--------                         --------
write results/orders [0..N)
release-store ready=N  --------> relaxed-load ready=N
                                acquire fence
                                acquired_ready=N
                                use result 0
                                use result 1  -- no new acquire required
                                ...
                                use result N-1
write result N                   index N is outside acquired prefix:
release-store ready=N+1 -------> read publication and acquire again

joined frame boundary: reset frontier to zero before either thread runs
~~~

The same argument applies to the immutable command feed in the other direction.
Claims, release publication, wakeups, waiting, and model lifetimes remain intact.
An already-published draw-owned placeholder is recognized from its acquired
result. The change does not grant permission to read an unpublished result or
to retain a frontier across frames.

A producer-ahead probe uses actual replay results and captured commands and
calls the production acquisition functions. It measures **one result plus one
feed entry** per sample unit. GE medians were 247.310 -> 26.782 CPU cycles and
36.008 -> 22.012 instructions. This is isolated barrier overhead, not a claim
of an 89% renderer improvement. Actual overlap and claim ownership vary in the app.

The lane now uses `posix_memalign` to honor its existing 64-byte alignment.
`calloc` never guaranteed that extended alignment. Both diagnostic A/B arms
use the corrected allocation, so the measurements do not isolate its benefit.

Tests cover prefix growth, placeholders, unavailable tails, feed close/overflow,
frame reset, and 32,768 concurrently published command/result/order payloads.
The existing small/full-tier stage, ownership, exhaustion and concurrent-sort
checks also pass on the XT1060.

### Foreground app measurement gate

The compile-time `TORIRS_PIPELINE_PMU` diagnostic measures the draw thread's
RenderFrame and the worker's model pass using separate per-thread hardware
counters. It runs ABBA three times in one launch, with six settling frames
before each 90-frame window. No PMU calls exist in a normal build. Each read
requires full enabled/running coverage; no multiplexed estimate or timer fallback.
The two thread counts are summed per window before computing the median.

The first app runs showed lower combined cycles (GE 6.1%, Varrock 5.0%), but a
post-run screenshot exposed an uncontrolled keyguard. Those JSON files are
marked provisional and are not the final foreground acceptance evidence.
Foreground repeats and screenshots are required. Model command counts must
match across arms; worker-owned counts need not, because work stealing is active.
This measurement excludes scene-painter construction, kernel work and sleeping;
it is not FPS or GPU hardware time.

### Next bounded extension: eight-command feed publication

The command translator already writes a sequential immutable feed. An optional
batch of eight can amortize its release stores and SEV signals. The producer
must flush at END_3D, feed close/overflow, and before returning from translation
(including short lookahead windows), so the draw never waits on an unpublished
command. BEGIN_3D remains immediately published. No result batching or larger
model lead is included: those introduce different waiting/lifetime questions.

The production publication probe on real command payloads measured
213.627 -> 98.525 cycles per command (53.9% less), with 85.010 -> 78.886
instructions. This justifies app testing; it does not yet justify enabling
batching by default.

### Foreground acceptance results

The visible app repeats retain the direct-I32 and acquired-prefix changes:

| Static benchmark scene | Model commands/frame | Baseline combined cycles/frame | Candidate | Change |
|---|---:|---:|---:|---:|
| Grand Exchange | 4,332 | 20,875,817.617 | 19,833,404.839 | -4.99% |
| Varrock square ground | 3,033 | 26,411,422.856 | 25,507,721.033 | -3.42% |

Both use 12 alternating 90-frame windows on the same installed binary, with
six unmeasured settling frames per window. The screenshot check confirms
visible scene output. Counts include userspace GL driver work in RenderFrame,
but not GPU execution or scene-painter construction. These are CPU cycle
savings, not FPS improvements. Full raw samples are in
[GE foreground results](benchmarks/krait_model_chains/experiments/app-foreground-both-ge-cycles.json)
and [Varrock foreground results](benchmarks/krait_model_chains/experiments/app-foreground-both-var-cycles.json).
The layout alignment correction is common to both arms.

### Publication batching accepted

The eight-command feed extension reduced combined foreground renderer cycles
by 1.34% in GE and 1.65% in Varrock, with direct output and cached acquires
already enabled in both arms. These percentages are not added to earlier
results. A final `--target all` comparison measures the complete combination
against the three reference paths within one binary.

The implementation leaves `FeedPublish` immediate for callers that require
that contract. Only the translate-ahead loop uses deferred commit. Short
batches flush before returning to dispatch, at close and before overflow is
published. Tests explicitly exercise both a nonmultiple tail and overflow
before a batch fills. BEGIN_3D still publishes immediately. No result is
published early, and neither thread changes model or face order.

Rollback is independent per mechanism: `TORIDRAW_GPU_ORDER_DIRECT=0`,
`TORIRS_GLES2_ACQUIRE_CACHE=0`, `TORIRS_GLES2_FEED_BATCH=0`. Defaults apply to
ARM32 NEON; other builds retain the original paths unless opted in.

The standalone harness now records CPU affinity readback. The Motorola kernel
may report success while leaving both CPUs allowed. Hardware per-thread counts
remain usable, but older records must not be described as proven single-core
placement. This matches the caveat already found in the app's pinning research.

### Normal-build assembly check

The linked ARMv7 library confirms that an already-acquired result prefix
branches around `dmb ish`. Only a newly observed publication executes it:

~~~text
ldr  frontier
cmp  frontier, index
bhi  use_result             <-- acquired prefix: no shared ready load or DMB
ldr  ready
cmp  ready, index
bls  unavailable
dmb  ish                    <-- new prefix only
str  ready, frontier
use_result:
ldr  results
compute result address
return
~~~

The feed fast path similarly avoids its DMB. Its compiled command stride is
104 bytes on this ABI: eight committed commands represent 832 bytes of payload,
small enough to avoid creating a large additional working set merely to reduce
publication frequency. Both loops retain ordinary sequential loads/stores;
this optimization does not add ARM/NEON transfer latency or vector-register
pressure to the projection/sorting kernels.

### Final combined foreground comparison

The complete implementation was measured with `--target all`: direct I32 output,
acquired-prefix caching and eight-command feed publication change together.
The original computation/publication paths form arm A inside the same binary;
common alignment and diagnostic/selection guards are present in both arms.
This is not a comparison against a separately linked untouched executable.

| Scene | Baseline combined cycles/frame | All changes | Change | Baseline / candidate MAD |
|---|---:|---:|---:|---:|
| Grand Exchange | 20,734,490.011 | 18,519,431.272 | **-10.68%** | 37,219 / 114,143 |
| Varrock square ground | 25,441,963.722 | 24,098,797.844 | **-5.28%** | 320,988 / 100,502 |

Each scene uses six windows per arm, 90 frames per window, ABBA repeated three
times after warmup. Model commands per frame remain 4,332 and 3,033 respectively
through every arm. Both threads' cycle counts fall in these final comparisons.
The medians above are computed from combined per-window counts, not by adding
separately computed thread medians. They establish reduced measured CPU work;
no FPS or GPU-time claim follows from them.

Raw evidence:
[GE complete A/B](benchmarks/krait_model_chains/experiments/app-foreground-all-ge-cycles.json),
[Varrock complete A/B](benchmarks/krait_model_chains/experiments/app-foreground-all-var-cycles.json).

Animation evaluation/actor-bake replay, descriptor lifetime redesign and GPU
residency/driver work remain separate items in the full-pipeline plan. The
results above do not establish that every rendering subsystem is optimal.

The final GE hardware-instruction run also reduced combined instructions by
**2.05%** (11,145,938.739 -> 10,917,855.594 per rendered frame). Instruction work
moves between threads as claims change; summing both is essential. The larger
cycle reduction is consistent with removing expensive synchronization stalls,
not merely reducing retired instruction count. This is a supporting diagnosis,
not a derived count of stall cycles.

### Normal APK and final correctness checks

The main checkout's normal ARMv7 APK was built and installed after profiling.
Its library has no pipeline-PMU or chain-capture symbols. The original phone
launch settings were restored. APK/library hashes and verification coverage
are recorded in [integrated-build.json](benchmarks/krait_model_chains/experiments/integrated-build.json).

On-device checks pass for all 59,352 real chains, 1,869 sorter fixtures
(299,546 faces), 595 index-packing cases, both stage tiers, exhaustion,
ownership and concurrent prefix publication. Five host format/parity tests
also pass. Normal-APK Varrock readbacks at frame 120 show ordinary animation
phase variation at the fountain (680 changed pixels in one A/B pair, 530
between two baseline launches). A repeated optimized readback matches the
original reference exactly across all 550,656 pixels. The hashes and scope
are in [integrated-pixels.json](benchmarks/krait_model_chains/experiments/integrated-pixels.json).
This verifies this scene; it does not replace actor/UI-specific coverage.

## 16. Remaining-work profile after integration

A new foreground Varrock hardware profile of the normal optimized APK records
187,443 CPU-cycle samples and 112,479 L1D-read-miss samples, each with zero
reported loss. Events are recorded separately after 300 rendered warmup frames.
The whole-process denominator includes scene construction; that work remains
excluded from optimization.

| In-scope area | Cycle sample share | L1D miss sample share |
|---|---:|---:|
| General model face sorting, both threads | 17.75% | 20.10% |
| Projection wrapper, both threads | 7.08% | 4.99% |
| Common untextured projection leaf, both threads | 6.32% | 4.71% |
| FrameNextCommand translation | 4.83% | 3.76% |
| GLES2 dispatch | 3.59% | 3.94% |
| Painter model emission | 3.00% | 6.84% |
| memcpy leaf, caller not yet attributed | 3.41% | 14.21% |

The top address within general face sorting lies in stable scalar compaction
of rejected-key sentinels. That is a better next experiment than changing
metadata layouts without knowing the updated distribution. A four-key ARM
compaction block can expose independent loads and amortize the loop branch
without introducing vector transfers or changing stable order. It is being
checked in same-process real-chain A/B before promotion to the app.

Raw compressed hardware recordings, reports and metadata are under
[profiles](benchmarks/krait_model_chains/profiles). The reusable launcher is
[gles2_pipeline_sample.py](tools/perf/gles2_pipeline_sample.py). Instruction
addresses are sampling evidence with normal PMU skid; they are not exact
per-instruction stall measurements. The large memcpy miss share still needs
caller attribution before assigning it to uploads or resident-ring placement.

### Four-key compaction accepted

The sentinel filter now loads four source keys before issuing overlapping
output stores. Stable output order and sentinel handling are unchanged. On
Krait this exposes independent loads to the integer pipeline and reduces
loop-control instructions without adding ARM/NEON transfers.

| Real chain corpus | Cycles change | Instructions change |
|---|---:|---:|
| Lumbridge | -0.41% | -0.67% |
| GE orbit | -0.77% | -0.68% |
| Varrock low | -1.69% | -1.08% |

Foreground app ABBA, 180 frames/window, keeps the previous three optimizations
on in both arms. The additional change reduces combined renderer cycles by
1.22% at GE and 0.84% at Varrock. Varrock has more launch-window variation;
all three ABBA blocks favor the candidate. These percentages are incremental,
not an updated end-to-end total against the original renderer.

The change is integrated and the 1,869-fixture/299,546-face on-device suite
passes with it enabled. `TORIDRAW_SORT_COMPACT4=0` restores the original loop;
`ToriDraw_FaceSortSetCompact4` changes it between joined frames. The environment
is read during Init, keeping lazy initialization out of concurrent hot loops.

### Compiler target experiment remains under review

The NDK Clang supports `-mcpu=krait`, including ARM hardware divide. The explicit
`-mfpu=neon` still disables VFPv4/FMA, and captured projection/order outputs
remain exact. Disassembly must use `llvm-objdump --mcpu=krait`; otherwise valid
SDIV/UDIV instructions can appear undecoded. This does not alter the legacy
multiply/shift division-by-three operation used for face depth.

Separate executable runs suggested a benefit when the entire model-chain
pipeline uses the same target. Targeting the toridraw object alone barely
changed counts, so this is not evidence for blindly adding a library flag.
Cross-target inlining boundaries are part of the question.

A new same-process shared-library comparison loads two complete model pipelines,
validates both against captured output, and alternates hardware-counter windows.
Each library has private state and binds its internal toridraw symbols locally.
Its initial identical-library control shows identical instructions but 0.64%
cycle difference, quantifying remaining heap/code-placement bias. Compiler
results must exceed that control and then survive app validation before any
Krait-specific app build is promoted.

DWARF hardware-sample call chains were explicitly rejected by this device's
kernel; no software-timing substitute was used. Caller attribution for memcpy
remains open. Descriptor/bounds pipelining, real actor/animation chains, and GPU
submission validation are also still required; this progress does not close
the full objective.

The shared-library compiler A/B subsequently **rejected** blanket Krait codegen:
GE cycles increased 4.72%, while instructions changed only -0.09%. Its cycle
regression is larger than the identical-library control. The earlier small
standalone executable result therefore does not justify changing the app's
compiler target. Keep explicit, measured kernel changes and the generic ISA
build until a more specific code-generation improvement is demonstrated.

Next implementation work returns to static model metadata resolution. The
existing `gles2_rebuild_batch_pose_table` already resolves batch entries and
valid pages after commit, failed commit, unload and batch clear. It is a
promising place to build a compact primary-pose descriptor once instead of
repeating pose -> entry -> page lookups on every draw. A complete proposal
must preserve last-entry-wins mapping, generation/element identity, animated
pose fallback, page compaction updates, and resident-ring lifetime checks.
Capture actual placement call chains before benchmarking that redesign.

The fresh projection-wrapper samples also concentrate in camera-centre
multiplication, not predominantly in diagnostic increments. That evidence
supports a later bounds-first, cross-model scheduling experiment; it does
not justify assuming all wrapper cost is metadata or synchronization.

## 17. Retained-placement descriptor pipeline

The next prototype resolves primary static poses at resource rebuild, rather
than repeating pose-table -> pose-array -> batch-entry -> page-map -> page
reads at each draw. A 32-byte descriptor carries only validated indices and
values; it contains no pointer into a reallocatable array. The current ring
serial/lifetime check still runs when the model is emitted.

~~~text
resource commit / failed commit / clear / rebuild
                   |
                   v
      original retained pose table
                   |
                   +--> primary descriptor indexed by scene-element index
                          {raw-ID tag, batch, entry, page, offsets, span}

ordered visible draw --> primary/pose-zero + matching tag?
                              | yes               | no
                              v                   v
                       one descriptor       original full resolver
                              |                   |
                              +---------+---------+
                                        v
                             existing resident-ring check
                                        v
                               ordered index emission
~~~

Captured one actual foreground frame per scene, including every reached static
placement query and the actual pose tables, batch entries and page mappings.
The replay restores those metadata structures and calls the production resolver.
It does not invent geometry or replace the calls with randomized IDs.

| Capture | Queries | Graph elements | Descriptor bytes | Cycles/query A -> B | L1 read misses A -> B |
|---|---:|---:|---:|---:|---:|
| Varrock square ground | 1,940 | 42,335 | 1,354,720 | 254.510 -> 93.225 | 3.870 -> 1.834 |
| GE ground | 2,889 | 33,306 | 1,065,792 | 328.763 -> 92.754 | 3.924 -> 1.848 |

Instructions/query fall from about 128 to 63. Twelve same-process ABBA windows
measure each hardware event separately. These are lookup-chain measurements,
not renderer-wide gains. Heap addresses differ from capture, and geometry,
projection, uploads and GPU execution are excluded from this probe.

Correctness fixtures test primary hits, animated/secondary fallback, page
compaction, invalid pages, inactive batches, index-kind reuse, unmapped poses
and invalid encoded entries. All 41 checks pass on the XT1060. The candidate
remains in the experimental worktree until full-app A/B confirms net benefit.

### Coordinated prefetch, not lookup-only caching

The first app test of the lookup-only descriptor was not an improvement:
combined GE-ground cycles rose 0.56%. The draw got faster, but the worker got
slower. The integration still prefetched the original pointer chain and did
not prefetch the descriptor, so an isolated warm lookup was an insufficient
benchmark of this change.

Capture version 2 now records **every relevant prefetch call and every static
placement query in their original order**, including prefetches for models
that are subsequently culled. GE has 7,114 records / 2,889 placement queries;
Varrock has 4,974 records / 1,940 queries. The replay calls the same production
prefetch and resolution functions as the app.

The revised design uses a dense primary-only eligibility bitmap, about 4-6 KiB
for these worlds. It chooses which address to prefetch without first demanding
a cold descriptor line. Primary-only models prefetch their descriptor at +3
and skip the two dependent legacy prefetch stages; animated, secondary and
unmapped cases retain the original ladder. The lookup still validates the full
raw ID. The bitmap is only a prefetch hint and the descriptor-validity gate.

~~~text
                         +3                +2              +1          draw
primary-only bit = 1 --> descriptor PLD --> no old ladder --> no ladder --> descriptor
other                --> pose-node PLD --> pose-array PLD -> entry PLD -> old resolver
~~~

On the full captured metadata chains, GE cycles/query are 529.414 -> 241.837
and Varrock 527.911 -> 236.126, including all intervening prefetch calls. L1
read misses fall from about 5.2 to 3.6 per query. The bitmap variant executes
more instructions than the first coordinated version but takes fewer cycles:
removing dependent memory traffic is worth more than minimizing instruction
count on this workload.

The revised **foreground app** GE-ground test reduces combined renderer cycles
3.02%, with identical model-command counts. Both draw and worker counters fall.
Varrock app validation is still pending. Cache rebuild now clears only the
validity bitmap rather than redundantly clearing megabytes of descriptor data;
a slot is written before its validity bit is set. Mapping identity, pose shape,
page validity and current table extent are checked at rebuild.

### Placement pipeline accepted and integrated

The coordinated bitmap/descriptor pipeline reduces combined foreground renderer
cycles **3.02% at GE ground** and **3.88% at Varrock ground**, with all previous
optimizations enabled in both arms. Both scenes keep identical model-command
counts across arms. These are incremental CPU-work gains; they are not added
to earlier percentages or presented as FPS.

The implementation is now in the main checkout. ARM32 NEON enables it by
default; `TORIRS_GLES2_STATIC_PRIMARY=0` restores the reference lookup and
prefetch ladder. Fifty-one on-device correctness checks cover table shrink,
page movement/invalidation, clear, raw-kind reuse, primary-only eligibility,
secondary/animated fallback and invalid entries. Both captured full metadata
chains also verify in both modes.

Normal model unload intentionally owns only the nonbatch arena in the existing
backend; retained batch mappings remain immutable until batch rebuild/clear.
The new descriptor follows that same contract. `gles2_compact_static_pages` has
one caller, inside commit before the final pose-table/descriptor rebuild.
No resident-ring serial check, painter order or face-sort rule is removed.

A local test server has been started for the remaining live-actor work. The
service is task-owned `osrs239-net` on localhost:43596, with an XT1060-only ADB
reverse tunnel. Offline benchmark inputs remain insufficient for the animation
and dynamic-bake requirements; live capture and broader validation remain open.

## 18. Live actor and animation workload

A dedicated local test account now supplies real live-client rendering inputs.
The normal optimized APK's live hardware profile records 148,260 cycle samples
with zero loss. Classic animation transforms account for 12.26% of the sampled
process cycles across draw and model-worker threads; face baking plus the GLES2
bake wrapper account for another 6.85% (before separate colour helper samples).
UI layout/emission also appears in this broader workload. Scene painter work
remains excluded from optimization.

The existing `TORIDRAW_ANIM_SKIP_SAME` feature is off by default based on older
clock-based rs289 measurements. Its current comment overgeneralizes the tuple
cost: the comparison is reached for animated elements, not every static draw,
and pose-record bookkeeping happens even with reuse disabled. The live workload
therefore needs hardware-counter revalidation of that policy.

The new animation capture records 795 actual pose calls over 16 world passes,
with current and bind vertices, alpha state, bone memberships, selected classic
frames or skeletal palette data, post transforms, prior pose tuple, and exact
output vertices/bounds. Replay restores each captured input outside measured
windows and compares forced posing with reuse through the same resolved-element
implementation. This is not fabricated animation data. Records have independent
mutable model snapshots, so original heap sharing and the whole renderer are
not reproduced; app validation is still required.

Reuse has an ownership condition as well as a tuple condition. The current
worker/draw split must not expose a pose tuple before its geometry is complete,
or re-pose a model while another consumer is baking it. A prospective pipeline
will prepare changed poses on the draw thread before publishing immutable model
inputs to the worker, then avoid a second pose on either consumer path. Full
mutable models are element-owned; repeated commands within a render frame must
refer to the same frozen animation state. Those conditions must be verified
before promoting reuse, not inferred from a fast microbenchmark.

### Pose reuse: exact replay and live pipeline result

All 795 captured classic pose calls reproduce their vertices, alpha arrays
and bounds exactly in both forced and reuse modes. 449 requests have matching
prior poses. Hardware replay cycles fall 92,949.401 -> 49,244.349 per call,
instructions 81,550.009 -> 44,283.374, and L1 read misses 201.464 -> 102.791.
Input restoration is outside measured windows. No skeletal call is present in
this first corpus; skeletal capture/replay is implemented but not yet exercised
by authentic data.

The renderer prototype prepares requested poses in FrameNextCommand, using the
already-resolved element pointer, before the command is published. The worker
then projects/sorts read-only geometry; inline draw fallbacks also avoid a second
pose. Frame-end joins protect the model lifetime. World-only replay and ordinary
frame initialization clear the preparation flag, as does FrameEnd. This changes
command preparation, not scene-painter ordering.

A live same-launch ABBA test reports **24.66% fewer combined renderer cycles**:
18,118,157.789 -> 13,651,041.050 per frame. Draw cycles fall 10.65%, worker cycles
48.05%. A separate instruction run falls 22.86% combined. Live NPC motion produces
small command-count variation, unlike the frozen offline scenes; these are live
workload results, not exact identical-frame or FPS measurements. The mostly
static GE-ground regression run is effectively neutral (-0.47% cycles).

On-device stage tests now poison animation metadata after preparation and verify
that the prepared worker consumes unchanged geometry and produces the reference
projection/order. The unprepared control does re-pose, proving that the fixture
exercises the gate. Both scene tiers pass. Existing pose-invalidation tests also
pass with reuse off and on, including model replacement/write, sequence changes,
secondary-frame changes and pointer identity changes.

A live shadow-validation build is checking each reused/prepared pose against an
uncached evaluation on a private model copy. Runtime promotion is pending that
check. Probe builds now have a helper that forces every relevant translation
unit to rebuild when switching capture/PMU/normal modes; this prevents stale
capture instrumentation from contaminating hardware measurements.

Live shadow validation has now matched **56,000** actual cached/prepared poses
against uncached private-copy evaluations, including vertex arrays, alpha and
bounds, with no failure. Skeletal reuse also passes eight targeted correctness
checks (frame changes, palette identity, repeated frames and out-of-range
normalization). Authentic skeletal capture is still being added; it is not
represented by the first 795-call classic corpus.

The pose-reuse design also passes the existing invalidation suite in both
legacy modes, plus a prepared-worker handoff fixture in both scene tiers.
The mostly static regression run shows no material slowdown. The new GPU policy
will preserve an explicit `TORIDRAW_ANIM_SKIP_SAME=0` unless the GPU-specific
`TORIRS_GLES2_POSE_REUSE` override is supplied; generic non-GPU callers retain
their original policy.

A second capture uses the actual Callisto-cub asset through the client's debug
spawn path to seek skeletal coverage. This is a controlled test scenario, not
a claim that the pet was naturally present in the live town workload. The
capture still records real runtime inputs and outputs; its skeletal coverage
must be verified from the file before claiming it.

### Prepared-pose pipeline accepted and integrated

The main checkout now includes the resolved-element pose API, draw-thread
preparation before feed publication, read-only worker consumption, and inline
fallback reuse. ARM32 NEON enables the GPU policy by default. Set
`TORIRS_GLES2_POSE_REUSE=0` to restore the original placement of pose work;
an explicit legacy `TORIDRAW_ANIM_SKIP_SAME=0` is also respected unless the
GPU-specific override is supplied. Other renderer families retain their old
policy.

Authentic skeletal coverage is now verified: the controlled Callisto-cub
capture contains 912 actual pose calls, including 24 skeletal calls. Both
forced and reused evaluation match every captured vertex, alpha and bounds
result. Alongside the live classic corpus, that is 1,707 verified pose calls.
The normal APK has been rebuilt and installed; diagnostic capture, PMU and
shadow-verification code are compiled out.

Remaining work still includes actor vertex baking, projection/sort consumer
layout review, GPU counter/submission audit and broader final validation. The
24.66% live-scene cycle reduction is incremental to the earlier changes and
must not be added to their percentages.

## 19. Actor baking and GPU audit

The post-pose live profile contains 136,296 hardware-cycle samples with no loss.
Actor face baking plus the GLES2 wrapper accounts for 9.10% of sampled process
cycles, ahead of the remaining animation-transform work (3.89%).

The actor baker now optionally transforms each model vertex once into a reusable
12-byte XYZ scratch row, then gathers those coordinates in the original sorted
face order. The gate requires more face-corner transforms than model vertices;
small/low-visibility models retain the original path. The exact existing integer
pitch/yaw shifts, offsets and float conversion are reused. No new reciprocal,
rounding rule, face order, texture mapping or alpha policy is introduced.

A real capture contains 128 actor bakes / 50,375 ordered faces. Both paths match
all captured positions, packed colours, local UVs and texture identities. The
bake-kernel hardware replay drops cycles/model 92,387.624 -> 68,613.836 (-25.73%)
and instructions 82,538.264 -> 56,469.701. Scratch traffic increases L1 read
misses; the measured saving comes from removing repeated coordinate arithmetic,
not from assuming every byte reduction is a win. Texture-manager/GL work is
outside this replay and is covered by app testing.

Live app ABBA (180 frames/window) reduces combined renderer cycles **11.44%**
and instructions **8.83%**, incremental to pose preparation and the earlier
changes. Live NPC movement introduces small workload variation. Shadow checking
matches **8,000,000** live faces with no mismatch. Another 544 correctness cases
cover textured/untextured faces across pitch and yaw rotations. The cache is
integrated, defaults on ARM32 NEON, and can be disabled with
`TORIRS_GLES2_ACTOR_WORLD_CACHE=0`.

### Verified GPU-counter interface

Motorola's published ghost 5.1.1 source at commit
[9b58d85b](https://github.com/MotorolaMobilityLLC/kernel-msm/tree/9b58d85b1133aca8d33e7b94c8487484720bf4fd)
provides the KGSL UAPI and A3xx counter definitions. It is the matching device
family release source, not a claim that it is the exact installed f151976 build.
The device confirms the documented group sizes and query/read ABI.

`gpubusy` is **not used as a performance profile**: its published implementation
reads clock-on/elapsed accounting updated with `ktime_get()`. Instead the new
probe reads already-reserved SP_ALU_ACTIVE_CYCLES (group 10, countable 0x1d) and
SP_FS_FULL_ALU_INSTRUCTIONS (group 10, 0x0e). It never reserves or reprograms a
counter. Four 120-frame GE-ground windows produce approximately 1.736 billion
ALU-active cycles and 5.496 billion fragment ALU instructions each, with very
small variation. These counters are device-wide, not per-context GPU time or
occupancy. No FPS inference is made from them.

A shader experiment now replays the actual uploaded painter draw list into
identical offscreen targets. It compares all pixels and depth-disabled state,
then alternates hardware-counter windows over the same frozen geometry. The
candidate bypasses atlas sampling only for the reserved opaque-white tile used
by untextured faces. It is not promoted until the A/A control, exact-pixel check
and GPU A/B all pass.

### Shader optimization accepted

The white-tile branch passes exact same-frame offscreen pixel comparisons in
both GE-ground and Varrock-ground at 1196x720, with zero differing pixels. The
probe explicitly verifies depth test and depth writes are disabled. It replays
the actual uploaded world draw list, preserving every primitive's order.

| Frozen real draw list | SP ALU-active cycles | Fragment ALU instructions |
|---|---:|---:|
| GE ground | -11.59% | -27.21% |
| Varrock ground | -9.49% | -26.68% |

The identical-program A/A control differs by only 0.0016% in cycles and zero
in median fragment instructions. Counters are read without allocating or
reprogramming any GPU counter. These are GPU-work savings, not elapsed GPU time
or FPS. The source defines what the hardware events count; the probe does not
infer occupancy from them.

The optimized shader is selected automatically only for an Adreno 320 renderer
string. Other GPUs retain the reference unless explicitly opted in.
`TORIRS_GLES2_FAST_SHADER=0` is the rollback. Both plain and cutout variants
preserve their alpha behavior. The runtime change and the real-draw-list GPU
replay tool are integrated.

The GLES2 actor path also stops computing generic texture metadata that only
other backends consume, and avoids triangle-configuration writes for its ordered
painter frame stream. That stream is submitted as arrays; its triangle configs
are not read by painter emission. Position, colour, UV and alpha output stay
unchanged. The generic bake API retains its metadata for other backends.

## 20. Final combined measurement and completion audit

`--target complete` switches every accepted runtime optimization together:
direct order destination, acquired prefixes, feed batching, four-key compaction,
static descriptors/prefetch, prepared-pose reuse, actor world-coordinate cache
and the white-tile shader. The reference and candidate are in the same binary;
common alignment, refactoring and diagnostic guards remain in both arms. This
is not a separately linked untouched executable comparison.

The final live run reduces combined renderer CPU cycles **33.44%**
(20,233,556.247 -> 13,466,710.506 per frame), with draw -20.67% and worker -48.20%.
Live NPC movement causes small command-count variation. The frozen GE-ground
run reduces combined cycles **13.20%** with exactly 4,224 model commands/frame
in every arm. Each result uses twelve 180-frame ABBA windows after warmup.
The percentages are direct combined measurements, not sums of earlier wins.

The shader's GPU work is measured separately on immutable real draw lists,
with zero pixel differences and depth test/writes explicitly checked off.
CPU cycles and GPU shader counters use different clock domains and are not
combined into an FPS or occupancy estimate.

Reviewed but rejected candidates (projection repacking, narrow order output,
sparse depth sorting, priority outlining and blanket Krait compiler targeting)
are retained only as evidence in this plan/artifacts. They are not enabled in
the renderer. No claim of a mathematically globally optimal implementation is
made; the deliverable is the integrated, architecture-informed pipeline with
measured gains and preserved renderer behavior.

The final frozen Varrock-ground comparison reduces combined renderer cycles
**11.83%** with exactly 3,033 model commands/frame in every arm. Moving-camera
shader parity also passes at four distinct frames (300, 360, 420, 480), with
zero differing pixels at each checkpoint. The recorded ordered draw lists
contain 539, 230, 243 and 239 submissions respectively, so validation is not
limited to a static eleven-draw scene.

### Scene ordering reviewed and preserved

The unchanged scene traversal (`painters_bucket.u.c`) classifies the draw box,
queues visible tiles by Manhattan distance, and drains farthest first subject
to dependency gates. A tile waits for its lower level and required neighbours;
completion/revisits release blocked tiles. Ground includes bridge underpasses,
terrain and far-side features. Scenery waits for the ground across its whole
footprint, then uses farthest-corner chain order. Raised items, near decoration
and near walls complete the tile. World-entity descent emits BEGIN_WORLD / END_WORLD
around a nested painter with a transformed camera. This is why the optimized
consumer preserves command ordinals and never groups submissions globally by
material or model type. No file under src/painters was changed.

~~~text
farthest eligible tile
        |
        v
lower-level / neighbour gates -- blocked --> revisit when dependency completes
        |
        v
ground / bridge / far features
        |
        v
whole-footprint-ready scenery -- world entity --> nested painter in-place
        |
        v
raised items / near decoration / near walls
        |
        v
tile DONE --> release dependent neighbours
~~~

### Completion audit

| Requirement | Authoritative evidence | Verdict |
|---|---|---|
| Understand scene/model painter contracts | Scene traversal review above; exact depth/tie/priority-10/11 contract in §§1,6,7; source map §14 | Reviewed |
| Leave scene painter unchanged | No diff under src/painters | Preserved |
| GPU rendering without Z-buffer | Unchanged depth-disabled world setup; offscreen probe checks GL_DEPTH_TEST and GL_DEPTH_WRITEMASK before comparison | Verified |
| Optimize the full CPU/GPU renderer pipeline for this device | Integrated preparation, publication, sorting, metadata/prefetch, actor bake and Adreno shader changes; combined CPU and separate GPU hardware results | Delivered |
| Use real call chains for fast iteration | Model, placement/prefetch, classic/skeletal pose, actor bake and actual uploaded GPU draw-list captures/replays | Delivered |
| Hardware counters only for performance conclusions | perf_event_open/simpleperf CPU hardware events; KGSL existing SP counters; no timer fallback; gpubusy excluded after source audit | Verified |
| Critically use architecture research | Krait dependency, cache, barrier and register-budget reasoning; rejected blanket compiler/fusion/sparse-sort alternatives | Documented |
| Detailed ASCII plan | This file, with dataflow, ownership, stage scheduling and preservation diagrams | Delivered |
| Exact output and lifecycle validation | Final native/host suites; 56,000 pose and 8,000,000 face shadow comparisons; static/moving GPU pixel parity; live region transition to Varrock | Passed within documented scope |
| Normal device build and cleanup | Normal APK installed; diagnostic symbols absent; original env/args verified; task server, tunnel, manifests and test save removed | Complete |

Final verification scope and raw artifact links are in
[KRAIT_RENDERER_RESULTS.md](KRAIT_RENDERER_RESULTS.md). This does not assert a
universal optimum or exhaustive coverage of every possible game asset.
