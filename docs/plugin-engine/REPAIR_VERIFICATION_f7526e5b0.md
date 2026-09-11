# Plugin engine repair verification

Base `f7526e5b0`; audit `7cf1aeedc`; repair branch `repair/plugin-engine-f7526e5b0`.

[PR #85](https://github.com/MRobertEvers/oldschool-clientc/pull/85) targets `v3`. The original audit and working trees remain intact.

The [full visual matrix](full_visual_verification.json) records every captured attempt, exact binary and input provenance, inspected pixels, controls, API causes and limitations. The [visual gallery](FULL_VISUAL_VERIFICATION.md) contains durable selected 2x images. The [API failure analysis](API_FAILURE_ANALYSIS.md) explains the underlying contracts and the incorrect intermediate repair that pristine bytecode disproved.

## Current validation

All 22 requested test targets and 15 additional targets pass on the final compiled candidate. The native compiler/decompiler has 93 focused checks; all 33 complete affected pristine scripts also decompile and compile byte-for-byte. Seven native hook tests verify exact input refusal and normalization. The derived cache changes only the 33 fingerprint-matched damaged scripts and the independent ground-caption hook.

Full visual coverage uses frozen tested candidates. Every changed native lookup, sidebar input boundary and related composition path is repeated on H; unchanged detailed reproductions retain their original candidate hashes. Coverage is distinct from every historical attempt passing. The matrix explicitly separates current acceptance from unsuccessful probes, superseded repairs, native capability boundaries and ordinary world-label/HUD occlusion.

## Boundaries and retained evidence

Legacy CS1 keeps an enabled sidebar selected and does not implement CS2-only Activities features. Actual touch, focus commands and keyboard insets are tested; a dummy desktop backend does not demonstrate a physical software keyboard. The alleged baseline NaN case was not reproduced: malformed refresh values now remain finite and runtime zero is refused. Native root164 can overlap its special orb with an open inventory; release restores that baseline, and its strict pixel failure is retained. Native startup script1340 also fails in the original and repaired disabled-plugin controls. World labels can pass beneath fixed HUD text; the fixed UI-to-UI demo/readout collision is repaired.

The earlier 53 numbered repairs, one behavior closure and first-pass timing measurements remain inside `historical_first_pass` and each row’s original `live` record. They are not relabeled as measurements of the final renderer. Current per-ID additions are in `current_verification`; original full reproduction commands and negative controls remain unchanged.

## Per-ID disposition

| ID | Plugin | Current disposition |
|---:|---|---|
| 0 | client-settings | verified repair |
| 1 | client-settings | verified repair |
| 2 | client-settings | verified repair |
| 3 | feature-flags | verified repair |
| 4 | feature-flags | verified repair |
| 5 | feature-flags | verified repair |
| 6 | gameframe-layout | verified repair |
| 7 | gameframe-layout | verified repair |
| 8 | gameframe-layout | verified activation; legacy native boundary |
| 9 | gameframe-layout | verified repair |
| 10 | gameframe-layout | measured behavior |
| 11 | ground-items | verified repair |
| 12 | ground-items | verified repair |
| 13 | entity-highlighter | verified repair |
| 14 | entity-highlighter | verified repair |
| 15 | entity-highlighter | verified repair |
| 16 | entity-highlighter | verified repair |
| 17 | item-stats | verified repair |
| 18 | item-stats | refuted premise; touch behavior verified |
| 19 | widget-demo | verified repair |
| 20 | widget-demo | verified repair |
| 21 | widget-demo | verified repair |
| 22 | widget-demo | verified repair |
| 23 | loot-beam | verified repair |
| 24 | loot-beam | verified repair |
| 25 | loot-beam | verified repair |
| 26 | loot-beam | verified repair |
| 27 | loot-beam | measured behavior |
| 28 | loot-beam | verified repair |
| 29 | performance-display | verified repair |
| 30 | performance-display | narrowed; no current product effect |
| 31 | performance-display | verified repair |
| 32 | performance-display | verified repair |
| 33 | performance-display | verified validation; NaN allegation unproven |
| 34 | performance-display | verified repair |
| 35 | loot-tracker | verified repair |
| 36 | loot-tracker | verified repair |
| 37 | loot-tracker | verified repair |
| 38 | loot-tracker | verified repair |
| 39 | loot-tracker | verified repair |
| 40 | xp-tracker | verified repair |
| 41 | xp-tracker | verified repair |
| 42 | xp-tracker | verified repair |
| 43 | xp-tracker | verified repair |
| 44 | xp-tracker | verified repair |
| 45 | minimap-orbs | verified repair |
| 46 | minimap-orbs | verified repair |
| 47 | minimap-orbs | verified repair |
| 48 | minimap-orbs | verified repair |
| 49 | minimap-orbs | verified repair |
| 50 | xp-drop-orbs | verified repair |
| 51 | xp-drop-orbs | verified behavior |
| 52 | xp-drop-orbs | verified repair |
| 53 | xp-drop-orbs | verified repair |
| 54 | mobile-gameframe | verified repair |
| 55 | mobile-gameframe | verified repair |
| 56 | mobile-gameframe | verified repair |
| 57 | mobile-gameframe | verified repair |
| 58 | mobile-gameframe | verified repair |
| 59 | mobile-gameframe | verified lifetime behavior |
| 60 | mobile-gameframe | measured behavior |
| 61 | screenshot | verified repair |
| 62 | screenshot | verified repair |
| 63 | screenshot | verified repair |
| 64 | screenshot | verified repair |
| 65 | screenshot | verified repair |
| 66 | screenshot | verified repair |
| 67 | screenshot | verified repair |
| 68 | nxt-highlight | verified repair |
| 69 | nxt-highlight | verified repair |
| 70 | nxt-bird-nest | verified repair |
| 71 | nxt-bird-nest | verified repair |
| 72 | nxt-cannon-ammo | verified repair |
| 73 | nxt-cannon-ammo | verified repair |
| 74 | tile-indicator-c | verified repair |
| 75 | tile-indicator-c | verified repair |
| 76 | tile-indicator-c | verified repair |
| 77 | tile-indicator-c | verified repair |
| 78 | tile-indicator-lua | verified repair |
| 79 | tile-indicator-lua | measured behavior |
| D3 | gameframe-layout | verified full native chat pack |

## Artifacts and publication

Repair worktree: `/private/tmp/3draster-fix`. Baseline: `/private/tmp/3draster-critic`. Original capture evidence: `/private/tmp/critic-runs`. Recipes, raw reports and test logs: `/private/tmp/critic-fixes`.

The complete local HTML remains `/private/tmp/critic-artifact/report.html`. The requested existing public URL has not been updated: this session has neither an Artifact publisher nor a connected browser. No second public URL was created.
