# Krait renderer results

Target: Motorola XT1060, Krait 300, Adreno 320, Android 5.1. GPU painter mode;
no depth test or depth writes. Scene-painter ordering was not changed.

## Final combined CPU result

All accepted runtime changes were switched together in one installed binary,
using twelve ABBA windows of 180 rendered frames and per-thread hardware
counters. Draw and worker counts are summed per window before taking medians.

| Workload | Reference cycles/frame | Optimized cycles/frame | Reduction |
|---|---:|---:|---:|
| Live local test account | 20,233,556 | 13,466,711 | **33.44%** |
| GE ground, fixed camera | 29,403,834 | 25,523,400 | **13.20%** |
| Varrock ground, fixed camera | 26,343,633 | 23,228,491 | **11.83%** |

The static scenes have identical model-command counts in every arm. Live NPC
motion causes small workload variation. These are userspace renderer CPU
cycles, excluding scene construction, kernel execution and sleeping. Common
alignment/refactoring/diagnostic guards remain in both arms. No FPS claim is
made, and the percentages are not sums of individual experiments.

## GPU result

The actual uploaded painter draw list was replayed into equal offscreen targets.
Reference and optimized shaders produced identical pixels. Existing kernel-owned
KGSL shader counters were read without reserving or reprogramming counters.

| Draw list | Shader ALU-active cycle reduction | Fragment ALU instruction reduction |
|---|---:|---:|
| GE ground | **11.59%** | **27.21%** |
| Varrock ground | **9.49%** | **26.68%** |

The identical-program control differs by 0.0016% in cycles and zero in median
fragment instructions. These counters are device-wide, not per-context elapsed
time or occupancy. Four additional moving-camera checkpoints have zero pixel
differences, including draw lists with 539, 230, 243 and 239 submissions.

## Implemented pipeline

- Sort directly into the published I32 order arena, with a complete capacity fallback.
- Reuse acquired immutable prefixes and publish command feeds in batches of eight.
- Compact four face keys per ARM block while preserving stable face order.
- Resolve retained primary-pose descriptors at resource rebuild; coordinate their prefetch with a small eligibility bitmap.
- Prepare changed poses on the owning thread before publishing model inputs; reuse matching poses and keep worker geometry read-only.
- Transform each eligible actor model's vertices once, then emit faces in painter order.
- Skip the white atlas lookup for untextured faces on Adreno 320; retain plain/cutout alpha behavior.

Rejected projection, narrow-order, sparse-sort and blanket compiler-target
experiments are documented in the plan. They are not enabled.

## Verification

- 59,352 captured model chains: projection, bounds, picking, clipping, order and indices.
- 1,869 sorting fixtures / 299,546 faces; 595 index-packing cases.
- 12,088 real placement/prefetch records; 51 invalidation/fallback checks.
- 1,707 real pose calls, including 24 skeletal calls; 56,000 live private-copy pose comparisons.
- 128 real actor bakes / 50,375 faces; 544 textured/untextured rotation cases; 8,000,000 live face comparisons.
- Prepared-worker handoff, concurrent publication, exhaustion and ownership checks in both scene tiers.
- Exact shader pixels and explicit depth-disabled checks on static and moving real draw lists.

## Reproduction and evidence

See [the detailed ASCII plan](KRAIT_UNIFIED_KERNEL_PLAN.md),
[tooling instructions](docs/krait_renderer.md), and
[raw measurements](benchmarks/krait_model_chains/experiments).

Normal build:

```sh
python3 tools/perf/build_android_renderer.py --probe normal --install --serial T062809L3Z
```

Rollback controls: `TORIDRAW_GPU_ORDER_DIRECT=0`,
`TORIRS_GLES2_ACQUIRE_CACHE=0`, `TORIRS_GLES2_FEED_BATCH=0`,
`TORIDRAW_SORT_COMPACT4=0`, `TORIRS_GLES2_STATIC_PRIMARY=0`,
`TORIRS_GLES2_POSE_REUSE=0`, `TORIRS_GLES2_ACTOR_WORLD_CACHE=0`,
`TORIRS_GLES2_FAST_SHADER=0`.

This is a measured optimization pass, not a proof of a global performance
optimum. The source and tool artifacts remain available for further workloads.

The final normal APK also survived a live region transition to Varrock with
actor/UI rendering and no reported GL errors. Temporary server/account/tunnel
setup was removed and the original phone launch files were verified restored.
The validation scope is the captured workloads and stated correctness cases,
not exhaustive coverage of every game asset.
