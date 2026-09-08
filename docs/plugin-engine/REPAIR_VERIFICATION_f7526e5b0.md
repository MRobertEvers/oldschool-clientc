# Plugin engine repair verification

Base: `f7526e5b0`. Audit: `7cf1aeedc`. Repair branch: `repair/plugin-engine-f7526e5b0`.

The repair worktree is `/private/tmp/3draster-fix`; the evidence baseline remains `/private/tmp/3draster-critic`. No changes were pushed to master or v3.

## Results

53 numbered entries have verified repairs. 1 additional numbered entry has a closed observation gap through real gameplay. The separately recorded Classic Fixed chat-clipping defect (D3) is also verified repaired.

24 entries retain live verification gaps. ID30 was narrowed to no current product effect. ID68 has corrected stroke-width semantics, but the original coarse convex silhouette and occlusion limitation remain.

All 22 requested test targets and four additional targets pass. Reproduction captures retain their actual binary hashes and candidate commits; affected cases were repeated when live verification exposed further defects. Earlier attempts are preserved rather than silently replaced.

The exclusive performance comparison measured native 3.22 ms, Classic Fixed 3.65 ms, Modern Resizable 3.71 ms and Stone Drawer 4.38 ms, all at 50 fps; the original plugin-frame cases were about 31 ms. These are controlled local captures, not a cross-device performance guarantee.

Full original commands and controls were recovered from the underlying behavior reports because 61 register commands had been truncated at 400 characters and 42 controls at 300. The machine-readable companion retains those full originals and exact per-ID results.

## Material changes

- Screenshot lifecycle survives root remounts; corner controls avoid native chrome, valid twelve-character names decode correctly, pressed opacity resets, and bounded notices retain every filename character.
- Classic Fixed preserves the full native chat pack; Stone Drawer restores map/wiki and chat controls, rejects malformed housing, and places its decorations using valid sibling anchors.
- Anchor ordering avoids repeated whole-tree scans. XP globe actions augment native menus, real pointer absence reaches plugins, and tooltip placement clears the native caption.
- Settings preserve unavailable plugins, validate declared values, keep staged edits while reflecting external writes, and preserve boolean spellings and integer expressions during Save and schema reload.
- Tracker values are linear in quantity, sessions are scoped to their owner, lists paginate, scrolled custom content retains its true origin, and legacy loot inference handles arrivals before or after NPC removal.

## Evidence and publication

The full live register is [repair_verification_f7526e5b0.json](repair_verification_f7526e5b0.json). Captures remain under `/private/tmp/critic-runs`; test, mutation, recipe and review artifacts remain under `/private/tmp/critic-fixes`.

The existing report was rebuilt at `/private/tmp/critic-artifact/report.html`. The requested published URL remains unchanged and was not updated because this session has neither an Artifact publisher nor a connected browser. A later Artifact-enabled session must supply the existing URL explicitly when publishing; the same local file path alone can create a new URL in another conversation.

The original manifest is untouched. A repair-only copy changes its `revconfig_cache` path from an older worktree to this repair adapter, which adds the semantic mobile chat-background role. Content, cache and server-script inputs remain pinned; the JSON includes hashes and the exact substitution.

## Per-ID disposition

| ID | Plugin | Result |
|---:|---|---|
| 0 | client-settings | verified repair |
| 1 | client-settings | verified repair |
| 2 | client-settings | live verification gap |
| 3 | feature-flags | verified repair |
| 4 | feature-flags | verified repair |
| 5 | feature-flags | verified repair |
| 6 | gameframe-layout | verified repair |
| 7 | gameframe-layout | live verification gap |
| 8 | gameframe-layout | live verification gap |
| 9 | gameframe-layout | live verification gap |
| 10 | gameframe-layout | live verification gap |
| 11 | ground-items | verified repair |
| 12 | ground-items | live verification gap |
| 13 | entity-highlighter | verified repair |
| 14 | entity-highlighter | verified repair |
| 15 | entity-highlighter | live verification gap |
| 16 | entity-highlighter | verified repair |
| 17 | item-stats | verified repair |
| 18 | item-stats | live verification gap |
| 19 | widget-demo | verified repair |
| 20 | widget-demo | verified repair |
| 21 | widget-demo | verified repair |
| 22 | widget-demo | verified repair |
| 23 | loot-beam | verified repair |
| 24 | loot-beam | verified repair |
| 25 | loot-beam | verified repair |
| 26 | loot-beam | live verification gap |
| 27 | loot-beam | live verification gap |
| 28 | loot-beam | live verification gap |
| 29 | performance-display | verified repair |
| 30 | performance-display | narrowed; no current product effect |
| 31 | performance-display | verified repair |
| 32 | performance-display | verified repair |
| 33 | performance-display | live verification gap |
| 34 | performance-display | live verification gap |
| 35 | loot-tracker | verified repair |
| 36 | loot-tracker | verified repair |
| 37 | loot-tracker | verified repair |
| 38 | loot-tracker | verified repair |
| 39 | loot-tracker | live verification gap |
| 40 | xp-tracker | verified repair |
| 41 | xp-tracker | verified repair |
| 42 | xp-tracker | verified repair |
| 43 | xp-tracker | verified repair |
| 44 | xp-tracker | live verification gap |
| 45 | minimap-orbs | verified repair |
| 46 | minimap-orbs | verified repair |
| 47 | minimap-orbs | verified repair |
| 48 | minimap-orbs | verified repair |
| 49 | minimap-orbs | live verification gap |
| 50 | xp-drop-orbs | verified repair |
| 51 | xp-drop-orbs | verified behavior |
| 52 | xp-drop-orbs | live verification gap |
| 53 | xp-drop-orbs | verified repair |
| 54 | mobile-gameframe | live verification gap |
| 55 | mobile-gameframe | verified repair |
| 56 | mobile-gameframe | verified repair |
| 57 | mobile-gameframe | verified repair |
| 58 | mobile-gameframe | verified repair |
| 59 | mobile-gameframe | live verification gap |
| 60 | mobile-gameframe | live verification gap |
| 61 | screenshot | verified repair |
| 62 | screenshot | verified repair |
| 63 | screenshot | verified repair |
| 64 | screenshot | verified repair |
| 65 | screenshot | verified repair |
| 66 | screenshot | verified repair |
| 67 | screenshot | live verification gap |
| 68 | nxt-highlight | partial; original visual limitation remains |
| 69 | nxt-highlight | live verification gap |
| 70 | nxt-bird-nest | verified repair |
| 71 | nxt-bird-nest | verified repair |
| 72 | nxt-cannon-ammo | verified repair |
| 73 | nxt-cannon-ammo | verified repair |
| 74 | tile-indicator-c | verified repair |
| 75 | tile-indicator-c | verified repair |
| 76 | tile-indicator-c | live verification gap |
| 77 | tile-indicator-c | verified repair |
| 78 | tile-indicator-lua | verified repair |
| 79 | tile-indicator-lua | live verification gap |
| D3 | gameframe-layout | verified full chat pack and authored rail geometry |

The status table deliberately does not turn unit-only repairs, missing causal stimuli, or expected harness-oracle failures into live acceptance. Read each JSON row for its conclusion, controls, attempts, adaptations, and inspected frames.
