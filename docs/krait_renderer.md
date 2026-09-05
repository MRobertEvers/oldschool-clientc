# Krait model-chain replay

~~~sh
make -f tools/perf/model_chain.mk -j3
python3 tools/perf/model_chain.py run \
  benchmarks/krait_model_chains/ge-orbit.chain.gz \
  --event cpu-cycles --event instructions \
  --out build/model-chain/ge-baseline.json
~~~

The driver uploads the small native executable each run and each corpus only
once, addressed by SHA-256. Rebuilds track the renderer's header dependencies.
No APK install, cache decode, scene construction, server, EGL or GL driver is
in a replay.

## The real call chain

The default **chain** mode calls the production GLES2DualCoreStage_BeginPass
once per captured pass, then GLES2DualCoreStage_ComputeModel for each model:
fast/bounds culling, vertex projection, applicable picking, face sorting and
priorities, order copy and result publication. Static models then use the
production gles2_painter_write_indices packer.

Calls retain capture order, including rejected and pick-only models. Repeated
geometry from the same source model is shared across records, preserving its
full/shared/lent/HD handle kind. Different posed geometry stays distinct.
Projection uses the captured camera, viewport, model position, bounds and
scratch capacity.

Index packing uses a model-relative base of zero. It does not simulate resident
ring placement, gather fallback, dynamic actor vertex baking, uploads or draws.
Geometry is captured **after posing**: animated snapshots are real, but
animation evaluation is not measured. The offline corpora contain no dynamic
actors. These limits matter when extrapolating to the complete app.

The optional **--mode sort** restores captured projected vertices and runs
sorting plus index packing. Do not subtract its counts from chain mode:
input-restoration costs and cache states differ.

## Correctness before counters

Every run first checks every chain using the GPU kernel and again using the
reference bucket sorter. It compares culling, projected vertices and bounds,
depth, near clipping, picking and exact face order. Static indices are checked
against the captured face indices. Verification and loading are outside the
measured windows.

The fixed-width little-endian file has no dereferenceable pointers and requires
a completion footer. Invalid indices/priorities, truncation, incompatible
capacities and incorrect expected outputs fail. Model tokens are opaque
identities used only for sharing geometry.

~~~sh
make -f tools/perf/model_chain.mk MODEL_CHAIN_TARGET=host -j3 \
  model-chain model-chain-tests
python3 tools/perf/test_model_chain.py

make -f tools/perf/model_chain.mk -j3 model-chain-tests
adb push build/model-chain/android/indices_test /data/local/tmp/
adb shell /data/local/tmp/indices_test
adb push build/model-chain/android/face_sort_test /data/local/tmp/
adb shell /data/local/tmp/face_sort_test
~~~

Index tests cover empty streams, SIMD boundaries, tails, high U16 bases and
invalid face IDs. The existing sorter suite exercises all twelve priorities,
ties, clipping and both sort lanes. Synthetic fixtures are correctness tests,
**not benchmark samples**.

Host and ARMv7 checks passed. An optional AddressSanitizer check was not counted
as a pass: this Mac's sanitizer runtime deadlocked while initializing shadow
memory, before entering the harness.

## Hardware measurements

Events: cpu-cycles, instructions, branch-misses, L1-dcache-load-misses.
Each gets a separate run. The process requests CPU0 affinity, records the effective mask, opens one pinned
per-thread perf_event_open counter, excludes kernel/hypervisor execution, and
rejects unavailable or multiplexed counters. There is no timer fallback,
inferred frequency or scaled estimate.

Each sample repeats the whole corpus to reach at least 30,000 calls. Ten
samples' worth of work warms the core/data before nine measured windows.
**--repetitions N** overrides this. JSON includes every raw count, denominator,
median, median absolute deviation, binary/corpus hashes and configuration.
Sort mode excludes rejected/pick-only calls from its denominator.

~~~sh
python3 tools/perf/model_chain.py run \
  benchmarks/krait_model_chains/varrock-low.chain.gz \
  --event branch-misses --event L1-dcache-load-misses \
  --out build/model-chain/varrock-memory.json

# Existing renderer control, same executable:
python3 tools/perf/model_chain.py run \
  benchmarks/krait_model_chains/ge-orbit.chain.gz \
  --env TORIDRAW_SORT_BITONIC_MAX=16 \
  --event cpu-cycles --event instructions \
  --out build/model-chain/ge-radix16.json
~~~

Compare the same corpus and mode, preferably control/candidate/control.
Cycles can change with CPU/memory frequency relationships and cache placement;
they are not universally immune to DVFS. Instruction counts and repeatability
provide cross-checks.

Android 5's shell protocol can report host success for remote failures. The
driver validates a remote exit marker, so missing inputs, failed executables
and PMU errors cannot silently succeed.

## Capture additional workloads

Build/install the diagnostic app once. Force the core rebuild when toggling the
macro: ordinary make dependencies do not track arbitrary command-line flags.

~~~sh
make -C src -j8 PLATFORM=android ANDROID_ABI=armeabi-v7a OPT=1 \
  TORIDRAW_PROBE_CFLAGS=-DTORIRS_MODEL_CHAIN_CAPTURE=1 \
  -W platform/platform_renderer_gles2_core.c all
./android/gradlew -p android -PtorirsAbi=armeabi-v7a installDebug

python3 tools/perf/model_chain.py capture \
  --scene grand-exchange-ground --passes 12 \
  --out build/model-chain/new-ge.chain.gz
~~~

Capture uses the existing benchmark manifest's scenes and camera paths.
**--camera x,y,z,pitch,yaw** overrides the path with a held camera. The driver
restores env.txt and extra_args.txt on exit and stops the capture app.

The hook is compiled out of normal builds. It runs the model stage an extra
time and writes snapshots: **capture is not a performance measurement**.
It requires single-threaded --gles2 with depth buffering disabled. For live
actor snapshots, enable TORIRS_MODEL_CHAIN_CAPTURE,
TORIRS_MODEL_CHAIN_FIRST_PASS and TORIRS_MODEL_CHAIN_PASSES with a live profile;
the offline driver selects the benchmark manifest.

Return to the normal app with:

~~~sh
make -C src -j8 PLATFORM=android ANDROID_ABI=armeabi-v7a OPT=1 \
  -W platform/platform_renderer_gles2_core.c all
./android/gradlew -p android -PtorirsAbi=armeabi-v7a installDebug
~~~

## Ordering and architectural review

Scene traversal is a tile/wall/spanning-object dependency walk with distance
buckets and LIFO ties. It is outside this harness and has not been changed.

Model sorting culls projected winding; near-clipped candidates bypass that 2D
rejection. It forms integer average-depth keys using the existing fixed-point
divide and model depth bias. Descending depth has ascending face-index ties.
NEON builds packed projected vertices, sorts short accepted runs with bitonic
networks, and uses stable depth radix passes above the crossover.

Fixed priorities 0–9 are traversed in band order, retaining depth order inside
each band. Priority 10's depth stream is followed by priority 11's stream.
Strict greater-than tests insert that combined stream before bands 0, 3 and 5,
against average depths of bands (1,2), (3,4), and (6,8), respectively.
Remaining flexible faces follow band 9. **Priorities 10 and 11 are not merged
with each other by depth.** Replay requires this exact order.

Local research reviewed:
[arch_fuzz/FINDINGS.md](../../arch_fuzz/FINDINGS.md) and its PMU implementation.
Measured cache/transfer costs motivate reducing projection-to-sort layout
conversion, intermediate key traffic and ARM/NEON synchronization across the
GPU model pipeline. Forwarding-network and predictor interpretations remain
hypotheses; isolated instruction kernels do not establish a renderer bottleneck.
The research harness's estimated-cycle fallback is inappropriate here.

Preliminary projected-only replay found an earlier radix crossover removing
about 14% of instructions but only about 3% of cycles. That motivates measuring
the whole dataflow; it is not evidence of a fully optimized renderer.
No speculative sorter or scene-painter optimization is retained in this change.

The one-event rule also follows
[Simpleperf's counter guidance](https://android.googlesource.com/platform/system/extras/+/android16-release/simpleperf/doc/executable_commands_reference.md).
Included baselines are CPU-chain measurements, not FPS claims.

## Acquired-prefix and publication probes

`--mode acquire` reuses actual prepared results and commands from the final
captured pass. It measures result plus feed acquisition with the producer
already ahead. Twelve ABBA windows compare original acquires with cached
publication prefixes in one process. At least 300,000 entry pairs run per
window. This is a protocol probe, not a simulation of real thread scheduling.
Do not extrapolate its percentage to the app.

## Full-app hardware-counter A/B

~~~sh
make -C src -j8 PLATFORM=android ANDROID_ABI=armeabi-v7a OPT=1 \
  TORIDRAW_PROBE_CFLAGS=-DTORIRS_PIPELINE_PMU=1 \
  -W platform/platform_renderer_gles2_dualcore.c all
./android/gradlew -p android -PtorirsAbi=armeabi-v7a installDebug
python3 tools/perf/gles2_pipeline_pmu.py --scene grand-exchange \
  --target both --event cpu-cycles --out build/model-chain/app-ge.json
~~~

Unlock the phone before launching. Check the saved post-run screenshot: a
lock screen or background activity is not a foreground renderer acceptance
run. The script restores env.txt and extra_args.txt even on failure.

The diagnostic counts the draw's RenderFrame and the worker's model pass
using one pinned, nonmultiplexed per-thread hardware event. It reports both
thread counts and their combined per-window total, plus model/worker/face
counts. Six settling frames precede each of twelve ABBA measurement windows.
The scene camera is static. Scene painter construction, kernel execution,
sleep and GPU execution are outside the measured scope. Work can move between
threads: judge the combined count, not one thread in isolation. This does not
measure FPS. The optional diagnostic compiles away completely in normal builds.

`--target direct` changes direct-I32 output alone; `--target acquire` changes
cached acquires with direct output enabled; `--target both` changes both; `--target feed` changes eight-command publication
with those two enabled; `--target all` compares all three with their reference paths.
Both arms use the corrected 64-byte lane allocation.

Runtime rollback controls, read at pass/frame boundaries:

- `TORIDRAW_GPU_ORDER_DIRECT=0`: original scratch-order copy.
- `TORIRS_GLES2_ACQUIRE_CACHE=0`: original per-entry acquire protocol.
- `TORIRS_GLES2_FEED_BATCH=0`: immediate publication of every translated command.

They default on for ARM32 NEON and remain opt-in on other targets.
The scene painter and all face priority/tie/clipping rules are preserved.

See [the unified plan](../KRAIT_UNIFIED_KERNEL_PLAN.md) for architecture rationale,
rejected prototypes, exact ordering invariants and measured integration results.

`--mode publish` measures the production reserve/commit/flush chain on real
captured command payloads. It compares immediate with eight-entry publication
in twelve ABBA windows. Command translation, worker overlap and GL dispatch
are excluded. It is the fast gate before an app `--target feed` experiment.

The vendor kernel can acknowledge affinity without honoring the requested
single-CPU mask. The replay records the effective mask; per-thread PMU counts
remain valid across migration, but cache behavior is not equivalent to a
strictly pinned run. Treat old captures lacking readback as requested affinity.

## Symbolized hardware sampling and compiled-pipeline comparisons

`gles2_pipeline_sample.py --out DIR --scene varrock-square-ground` samples
CPU cycles in the installed normal app after 300 rendered frames. It records
separate events, rejects sample loss and mismatched installed/local libraries,
saves symbol paths and restores launch settings. `--callgraph dwarf` is not
supported by the XT1060 kernel; ordinary hardware-PC samples work.

`--mode chain-ab` in model_chain.py alternates the four-key compaction switch
against its scalar reference inside one replay process.

For compiler experiments, build two Android-only pipeline libraries:

~~~sh
make -f tools/perf/model_chain.mk MC_OUT=/tmp/chain-generic \
  model-chain-library model-chain-compare
make -f tools/perf/model_chain.mk MC_OUT=/tmp/chain-candidate \
  MC_LIBRARY_CPU_FLAGS=-mcpu=krait model-chain-library
python3 tools/perf/model_chain_compare.py benchmarks/krait_model_chains/ge-orbit.chain.gz \
  --a /tmp/chain-generic/libmodel_chain.so --b /tmp/chain-candidate/libmodel_chain.so \
  --binary /tmp/chain-generic/model_chain_compare --out /tmp/chain-compare.json
~~~

First pass the same library as both A and B to measure heap/code-placement
bias. Each library owns its model data, verifies captured projections/order,
and binds internal symbols locally. The host counts whole real chains using
one pinned hardware event with full running coverage. This is a measurement
tool, not a recommendation to enable `-mcpu=krait`: the initial shared-library
comparison found that blanket change slower.

## Retained-placement pipeline

`placement_chain.py capture` records the actual retained pose table, batch
entries, page mappings, and the ordered prefetch/lookup calls in a rendered
frame. `placement_chain.py run` replays those same CPU functions with ABBA
hardware counters. Use `make -f tools/perf/model_chain.mk placement-chain
placement-tests` to build the ARM harness and invalidation checks.

The active descriptor path uses a 32-byte row and a small validity/eligibility
bitmap. It is refreshed on batch lifecycle changes, checks the full element
ID at lookup, and leaves resident-ring serial checks intact. Its coordinated
prefetch path matters: the lookup-only prototype did not improve the app.
`TORIRS_GLES2_STATIC_PRIMARY=0` restores the old lookup and prefetch ladder.

`build_android_renderer.py --probe MODE --install --serial T062809L3Z` rebuilds
all translation units containing the selected probe, so returning to `normal`
also removes capture/PMU code. It targets the device explicitly when several
phones are attached. Supported modes are listed by `--help`.

## Live animation replay

`anim_chain.py capture --manifest DEVICE_PATH --args-file PRIVATE_LOCAL_FILE
--out FILE.chain.gz` captures actual input/output pairs for element posing.
Use a single-threaded GPU argument file (the tool converts --gles2-dualcore to
--gles2), and a build made with `--probe animation`. The capture supports
classic, blended and skeletal input formats. `make -f tools/perf/model_chain.mk
animation-chain animation-tests` builds the replay and skeletal correctness
fixture. `anim_chain.py run FILE.chain.gz --out RESULTS.json` runs hardware
ABBA windows after exact reference verification.

Each measured window restores captured model/pose state before enabling the
counter. The replay measures pose evaluation, not animation asset loading,
original heap sharing, model construction or actor baking. Live app promotion
therefore remains a separate gate.

`gles2_pipeline_pmu.py --manifest DEVICE_PATH --args-file PRIVATE_LOCAL_FILE
--target pose --out RESULTS.json` tests prepared-pose ownership and reuse in
one live launch. NPC movement introduces small workload variation; inspect
model counts and both threads, not one thread's time. The GPU rollback is
`TORIRS_GLES2_POSE_REUSE=0`. The generic library's legacy reuse default remains
unchanged.
