# UITree redraw A/B suite

This directory contains the deterministic C fixture used to verify UITree
redraw retention. The standard-library-only runner is
[`tools/uitree_redraw_suite.py`](../../tools/uitree_redraw_suite.py). It keeps
three questions separate:

1. What did the production baseline actually draw?
2. What should a full, uncached walk draw?
3. Does candidate production retention draw that exact result with less work?

## Run it

Run from a clean, committed candidate checkout:

```sh
python3 tools/uitree_redraw_suite.py --profile quick
python3 tools/uitree_redraw_suite.py --profile full
```

A complete run also executes and archives `test-uitree`,
`test-client-trigger`, `test-cs2-frame-settle`, and an optimized native-client
link before the A/B lanes.

`quick` uses 256 visual frames, 4,000 timed operations per benchmark scenario,
six three-process trials, and 5,000 deterministic bootstrap resamples. `full`
uses 2,048 visual frames, 30,000 operations, 18 trials, and 20,000 resamples.
Override those values with `--visual-frames`, `--bench-frames`, `--trials`, and
`--bootstrap-samples`. Benchmark trial counts must be a multiple of six so
each execution order is represented equally.

Artifacts default to a new timestamped directory under
`build/uitree-redraw/`, which is ignored by Git. Use `--out` to select another
new directory. A full run can contain many large BMP files, so do not place it
in a tracked source directory.

The runner resolves `origin/v3` by default, creates a temporary detached
worktree at that exact commit, builds isolated baseline and candidate
executables, runs the suite, and safely removes the temporary worktree.
Artifacts are preserved on command failures and failed gates. Use
`--keep-baseline-worktree` to preserve the detached checkout for diagnosis.

Final evidence requires both source worktrees to be clean. A dirty candidate
is refused by default because its reported commit cannot reproduce its
binary. `--allow-dirty` is a development escape hatch; runs using it are
explicitly marked non-reproducible in the manifest, summary, and report.

An existing baseline worktree can be supplied explicitly:

```sh
python3 tools/uitree_redraw_suite.py \
  --baseline-root /path/to/v3-worktree \
  --baseline-ref origin/v3
```

Its `HEAD` must match the resolved baseline ref and its worktree must be clean.
`--allow-baseline-mismatch` makes an intentional revision mismatch explicit,
but marks the resulting run as development evidence.

Externally built harnesses are supported for diagnosis:

```sh
python3 tools/uitree_redraw_suite.py --skip-build \
  --baseline-harness /path/to/baseline/uitree_redraw_harness \
  --candidate-harness /path/to/candidate/uitree_redraw_harness
```

Such a run is marked non-reproducible because the runner did not build the
recorded binaries. `--skip-native`, `--skip-visual`, `--skip-chrome`, and
`--skip-bench` are diagnostic shortcuts. Each skipped lane creates a failed
completeness gate, sets `evidence_complete` to false, and makes the runner exit
nonzero; a partial run cannot be mistaken for final parity-and-performance
evidence.

## Build and harness contract

The runner invokes this Makefile once per source tree:

```text
make -C test/uitree_redraw \
  SOURCE_ROOT=<baseline-or-candidate-root> \
  BUILD_DIR=<isolated-absolute-build-dir> \
  CANDIDATE=0|1
```

The executable is `BUILD_DIR/uitree_redraw_harness` and accepts:

```text
uitree_redraw_harness \
  --mode visual|bench \
  --out <new-directory> \
  --retention 0|1 \
  --frames <positive-count> \
  --seed <uint32>
```

`--retention 0` forces a full emit walk. `--retention 1` exercises the
production retention behavior compiled from that executable's source tree,
for both baseline and candidate. Correctness comes from separate processes
and separately built source revisions; no retained execution is accepted as
its own oracle.

Visual mode writes one row per trace frame using this schema:

```text
frame,scenario,checkpoint,pixel_hash,emit_hash,emit_count,full_walks,retained_frames
```

Benchmark mode writes exactly one row for each individual scenario and one
aggregate row:

```text
scenario,elapsed_ns,operations,ns_per_op,full_walks,retained_frames,emit_nodes
```

The required scenarios are `steady`, `camera`, `overlay_position`, `scroll`,
`hover`, `content`, `topology`, and `aggregate`. Each individual scenario is
warmed outside its timed region and receives fresh deterministic fixture
state.

## Four-way visual oracle

The same trace and seed run in four independent processes:

1. Baseline with retention `0`: independent baseline full walk.
2. Baseline with retention `1`: actual production behavior before the change.
3. Candidate with retention `0`: forced-full correctness oracle.
4. Candidate with retention `1`: optimized production behavior under test.

The authoritative exact-parity gates are:

```text
baseline full == candidate forced
candidate retained == candidate forced
```

For every frame, `frame`, `scenario`, `checkpoint`, `pixel_hash`, `emit_hash`,
and `emit_count` are compared exactly. For every checkpoint, all four BMP sets
must contain the same filename. BMPs are decoded to top-to-bottom RGB, ignoring
alpha, and dimensions and every RGB byte must match for each exact gate.

Coverage is also part of the contract. The runner checks every row against the
fixture's complete repeating 24-step trace, including hover, two-axis scroll,
content, transparency, topology, host-camera input, five projection states,
shape rotation, host input, reachability, and volatile overlay counts. A run
shorter than one complete cycle, a renamed scenario, or a dropped checkpoint
fails even if the remaining images happen to match.

The production baseline is evidence of the known bug, not the oracle. Its
retained output may differ from the forced oracle only on trace rows named
`host-camera` or `host-input`; frame identity may never differ. Any baseline
difference in another scenario fails the suite, and at least one permitted
baseline-retained difference is required so the test proves it exercised the
stale-host-input defect rather than silently passing a vacuous trace.

Structural gates independently require both production builds to retain some
frames and require both forced modes to perform only full walks. Cumulative
`full_walks` and `retained_frames` totals are read from the final trace row.

Every image comparison writes a deterministic 32-bit diff BMP. Exact images
take a fast path and produce a black diff; mismatches preserve a bounding box,
different-pixel count, maximum channel delta, and a brightened visual diff.
Open `gallery.html` for the side-by-side view:

```text
baseline retained (before) | candidate forced (oracle) | candidate retained (after) | after diff
```

The repository's real Soft3D debug-overlay visual test is also compiled and
run against both revisions through the isolated `chrome-visual` target:

```sh
make -C test/uitree_redraw chrome-visual \
  SOURCE_ROOT=<baseline-or-candidate-root> \
  BUILD_DIR=<isolated-build-dir> \
  CANDIDATE=0|1 \
  CHROME_OUT_DIR=<isolated-output-dir>
```

All `CHROME_OUT_DIR/build/debug_overlay_*.bmp` images are copied into the
artifact tree and compared as decoded RGB with zero tolerance. This exercises
the real display-list translator, baked fonts, and software rasterizer.

## Three-way performance evidence

Each trial starts three independent benchmark processes with one shared seed:

- `baseline-retained`: actual baseline production retention.
- `candidate-retained`: candidate production retention.
- `candidate-forced`: candidate with full walks forced as a control.

The six permutations of those variants are executed once per six-trial block.
This balances each variant across first, middle, and last positions and avoids
systematically crediting one executable for thermal or order effects. Processes
are never benchmarked concurrently. Trials, not frames within a process, are
the independent samples.

For each comparison the runner calculates paired per-trial `ns_per_op` ratios,
their median, and a deterministic paired-bootstrap 95% confidence interval.
The production baseline/candidate gates are deliberately split by validity:

- `steady` is primary and passes only when the CI upper bound is below `1.0`.
- `correct-aggregate` is primary. It recomputes elapsed time per operation from
  `steady`, `overlay_position`, `scroll`, `hover`, `content`, and `topology`,
  excluding `camera`, and requires its CI upper bound below `1.0`.
- `overlay_position`, `scroll`, `hover`, `content`, and `topology` are
  regression guards. A guard passes when its CI upper bound is at most
  `1 + tolerance`, or its median absolute increase is no greater than the
  practical floor.
- `camera` is informational because baseline production is intentionally stale
  for host-camera movement while the corrected candidate rebuilds. Comparing
  those timings as if they performed equal work would be misleading.
- The harness-provided `aggregate` is also informational because it includes
  that invalid camera comparison. It is retained in raw evidence but is not the
  primary aggregate gate.

The defaults are a 3% guard tolerance and a 100 ns/op practical floor; change
them with `--regression-tolerance` and `--practical-floor-ns`. An interval
overlapping `1.0` is not evidence that a primary case is faster.

Candidate retained `steady` is additionally required to beat candidate forced
full walks, proving that retention is effective within one committed
implementation. Candidate retained versus candidate forced `camera` is a guard
on the cost of the correctness fix, not a baseline speedup claim. Counter gates
run for every trial rather than over medians: each individual scenario must
report the requested operation count; aggregate values must exactly sum their
components; `full_walks + retained_frames` must equal operations; production
aggregates must exercise retention; and every forced record must contain only
full walks. Equal-work scenarios also require exact emitted-node totals between
candidate forced/retained and between baseline/candidate production. This
prevents one malformed trial or a mismatched denominator from hiding inside a
timing ratio.

## Reproducibility and artifacts

The report records both commit and tree IDs, source status, recursive submodule
state, runner/harness source hashes, executable hashes, toolchain and host
identity, selected compiler/linker environment variables, every command, all
parameters, and the balanced order schedule. A clean run includes a concrete
checkout command and runner invocation. Any development override is listed as
a reproducibility warning.

Every run emits:

```text
manifest.json                 source/build/host identity and reproduction command
summary.json / summary.csv    gates, raw values, ratios, confidence intervals
report.md                     readable correctness/performance report
junit.xml                     one CI testcase per gate
SHA256SUMS                    integrity hash of every other artifact
gallery.html                  visual before/oracle/after/diff gallery
logs/                         native checks plus complete build/execution logs
visual/harness/               four visual executions and frames.csv files
visual/chrome/                copied Soft3D baseline/candidate images and diffs
perf/raw/                     three metrics files per independent trial
```

Upload large generated evidence as CI artifacts; do not commit it as source
goldens.

## Optional future full-client replay lane

The deterministic fixture is appropriate for PR CI but does not replace a
cache-backed full-client integration run. A future manual or nightly lane can
record one canonical client command trace with `TORIRS_CMD_RECORD`, then replay
the same `.trscmd` on baseline and candidate with `TORIRS_CMD_REPLAY`.

That lane should preserve the same separation of evidence:

- Use independent baseline-full, baseline-production, candidate-forced, and
  candidate-production executions; do not use a retained tree as its own
  correctness reference.
- Compare normalized RGB at fixed checkpoints with zero tolerance.
- Run screenshot capture only in correctness executions, never timed ones.
- Use identical absolute cache/manifest inputs and isolated copies of
  `TORIRSSERVER_SAVES`.
- Preserve the trace, screenshots, raw timing windows, logs, build identity,
  and cache hashes.
- Fail clearly when the external cache fixture is unavailable instead of
  reporting the deterministic fixture as full-client coverage.
