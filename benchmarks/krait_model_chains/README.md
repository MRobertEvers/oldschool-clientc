# Captured Krait model chains

Real consecutive GPU model calls from OSRS239 on the XT1060, captured without a Z-buffer. See [the harness guide](../../docs/krait_renderer.md).

| Corpus | Model calls | Cycles/call | Instructions/call |
|---|---:|---:|---:|
| lumbridge | 10,604 | 2,272.203 | 972.801 |
| ge-orbit | 41,988 | 4,021.555 | 2,100.724 |
| varrock-low | 6,760 | 5,074.911 | 3,039.922 |

Capture JSON files record the camera setup and raw corpus hash. Baseline JSON files include all hardware-counter samples and the executable hash. These measure the CPU model chain, not whole-app frame time.

Coverage:

- **lumbridge:** 10604 chains, 4 passes, 2527 distinct posed assets, 2380 visible, 38481 faces, 0 clipped, 28 pick hits, 1284 prioritized, 136 animated snapshots, 0 dynamic. Drawn priority-face counts: {0: 5432, 1: 7825, 2: 2480, 3: 668, 4: 100, 5: 136, 6: 992, 8: 76, 10: 324, 11: 184}. Handle-kind counts: {1: 6104, 5: 3492, 4: 1008}.
- **ge-orbit:** 41988 chains, 12 passes, 3726 distinct posed assets, 26864 visible, 336446 faces, 130 clipped, 41 pick hits, 12388 prioritized, 12 animated snapshots, 0 dynamic. Drawn priority-face counts: {0: 66901, 1: 112999, 2: 24595, 3: 8492, 4: 14827, 5: 11377, 6: 3632, 7: 1955, 8: 702}. Handle-kind counts: {1: 25566, 5: 13402, 4: 3020}.
- **varrock-low:** 6760 chains, 4 passes, 1493 distinct posed assets, 4156 visible, 90462 faces, 72 clipped, 28 pick hits, 1784 prioritized, 16 animated snapshots, 0 dynamic. Drawn priority-face counts: {0: 8052, 1: 18996, 2: 13774, 3: 3268, 4: 2736, 5: 64, 6: 364, 7: 936, 9: 128}. Handle-kind counts: {1: 3380, 5: 2284, 4: 1096}.

The corpora contain animated static geometry but no dynamic actors. Priorities missing from these captures are covered by the separate correctness suite, not by invented benchmark samples.

## Integrated renderer results

On the foreground XT1060 app, direct I32 order emission, cached acquired
prefixes and eight-command feed publication reduced combined draw/worker CPU
cycles by **10.68% at GE** and **5.28% at Varrock square ground**. Each comparison
uses twelve same-launch ABBA hardware-counter windows. Model commands/frame
are constant within each scene. Both arms share alignment and diagnostic code.
These are renderer userspace CPU counts, not FPS or GPU execution time.

See [GE complete A/B](experiments/app-foreground-all-ge-cycles.json),
[Varrock complete A/B](experiments/app-foreground-all-var-cycles.json), and
[the detailed plan](../../KRAIT_UNIFIED_KERNEL_PLAN.md). Earlier app records
without `foreground` in their name have an explicit keyguard limitation and
are provisional. Isolated `acquire`/`publish` probes are gate measurements,
not whole-app results.
