# Full plugin visual verification

All **19 shipped feature plugins** were visually reviewed on **OSRS239 and rs289lc**, along with the Lua runtime and four public API examples. **351 captured scenarios were inspected at 2x**. Three additional attempts stopped before capture; all failures, corrected stimuli and native limitations remain in the [complete machine-readable record](full_visual_verification.json).

These lossless WebP images preserve the exact reviewed 2x pixels; open an image to inspect it at full size. They are representative evidence, not substitutes for the complete reproduction/control register. Per-image hashes, source candidates and run IDs are in [visual_evidence.json](visual_evidence.json).

The final compiled candidate is `ad0f09ad349451a856080d45e49dcdfb0ee36dc4`, binary SHA256 `557628b0e971e5d53a6c18a501c7f5325161bd4a7d7d39e25688063fa1cecac9`. All 37 integrated targets pass. The repaired native pipeline has 93 focused compiler/decompiler checks, 33 full pristine-script byte-exact round trips, and seven hook/compatibility tests.

F/G images retain their exact provenance for unchanged implementations. Twenty-one explicitly selected H regressions repeat canonical root/child lookup, cannon updates, native footprints, ground controls, whole-sidebar movement, native release and shared composition. The [acceptance selection](full_visual_verification.json) separates those checks from superseded F/G failures.

## Shipped feature plugins

| Plugin | OSRS239 | rs289lc |
|---|---|---|
| client-settings | [![client-settings osrs239](visual_evidence/da4fa39f97852a54.webp)](visual_evidence/da4fa39f97852a54.webp) | [![client-settings rs289lc](visual_evidence/3d5a2dfc2ce63bde.webp)](visual_evidence/3d5a2dfc2ce63bde.webp) |
| feature-flags | [![feature-flags osrs239](visual_evidence/a7e14bef805e3b35.webp)](visual_evidence/a7e14bef805e3b35.webp) | [![feature-flags rs289lc](visual_evidence/e63b020929cdd869.webp)](visual_evidence/e63b020929cdd869.webp) |
| gameframe-layout | [![gameframe-layout osrs239](visual_evidence/c6e662f67ffa7976.webp)](visual_evidence/c6e662f67ffa7976.webp) | [![gameframe-layout rs289lc](visual_evidence/4508b40a080c910a.webp)](visual_evidence/4508b40a080c910a.webp) |
| mobile-gameframe | [![mobile-gameframe osrs239](visual_evidence/ca3d1b805bcfbf13.webp)](visual_evidence/ca3d1b805bcfbf13.webp) | [![mobile-gameframe rs289lc](visual_evidence/eabdb4fd3b11d06f.webp)](visual_evidence/eabdb4fd3b11d06f.webp) |
| minimap-orbs | [![minimap-orbs osrs239](visual_evidence/c6e662f67ffa7976.webp)](visual_evidence/c6e662f67ffa7976.webp) | [![minimap-orbs rs289lc](visual_evidence/4508b40a080c910a.webp)](visual_evidence/4508b40a080c910a.webp) |
| loot-tracker | [![loot-tracker osrs239](visual_evidence/c6e662f67ffa7976.webp)](visual_evidence/c6e662f67ffa7976.webp) | [![loot-tracker rs289lc](visual_evidence/4508b40a080c910a.webp)](visual_evidence/4508b40a080c910a.webp) |
| nxt-highlight | [![nxt-highlight osrs239](visual_evidence/f045592edb0f0562.webp)](visual_evidence/f045592edb0f0562.webp) | [![nxt-highlight rs289lc](visual_evidence/e93f4549f8daeae2.webp)](visual_evidence/e93f4549f8daeae2.webp) |
| nxt-bird-nest | [![nxt-bird-nest osrs239](visual_evidence/38df12661e2afd18.webp)](visual_evidence/38df12661e2afd18.webp) | [![nxt-bird-nest rs289lc](visual_evidence/68cd68f085259d0b.webp)](visual_evidence/68cd68f085259d0b.webp) |
| nxt-cannon-ammo | [![nxt-cannon-ammo osrs239](visual_evidence/020266f294221a23.webp)](visual_evidence/020266f294221a23.webp) | [![nxt-cannon-ammo rs289lc](visual_evidence/854268de42270d4e.webp)](visual_evidence/854268de42270d4e.webp) |
| tile-indicator-c | [![tile-indicator-c osrs239](visual_evidence/59eeea82f84527a2.webp)](visual_evidence/59eeea82f84527a2.webp) | [![tile-indicator-c rs289lc](visual_evidence/d6c737b98b008b10.webp)](visual_evidence/d6c737b98b008b10.webp) |
| tile-indicator-lua | [![tile-indicator-lua osrs239](visual_evidence/2e57a114ea7c0d03.webp)](visual_evidence/2e57a114ea7c0d03.webp) | [![tile-indicator-lua rs289lc](visual_evidence/bd2dc9a20b2922f0.webp)](visual_evidence/bd2dc9a20b2922f0.webp) |
| entity-highlighter | [![entity-highlighter osrs239](visual_evidence/b3b409d7414ec5df.webp)](visual_evidence/b3b409d7414ec5df.webp) | [![entity-highlighter rs289lc](visual_evidence/ba1a2c5a0782a4cd.webp)](visual_evidence/ba1a2c5a0782a4cd.webp) |
| performance-display | [![performance-display osrs239](visual_evidence/0e7b7b0ccfa3a863.webp)](visual_evidence/0e7b7b0ccfa3a863.webp) | [![performance-display rs289lc](visual_evidence/5fc7efa026b014b7.webp)](visual_evidence/5fc7efa026b014b7.webp) |
| loot-beam | [![loot-beam osrs239](visual_evidence/ddeac8b9da86d318.webp)](visual_evidence/ddeac8b9da86d318.webp) | [![loot-beam rs289lc](visual_evidence/aa4c48afde6828a2.webp)](visual_evidence/aa4c48afde6828a2.webp) |
| item-stats | [![item-stats osrs239](visual_evidence/95b0d21d77df1aec.webp)](visual_evidence/95b0d21d77df1aec.webp) | [![item-stats rs289lc](visual_evidence/68ba6dedf3e4cafe.webp)](visual_evidence/68ba6dedf3e4cafe.webp) |
| xp-tracker | [![xp-tracker osrs239](visual_evidence/97c0343d8ce8da0d.webp)](visual_evidence/97c0343d8ce8da0d.webp) | [![xp-tracker rs289lc](visual_evidence/0d6ecd1b53d311c9.webp)](visual_evidence/0d6ecd1b53d311c9.webp) |
| xp-drop-orbs | [![xp-drop-orbs osrs239](visual_evidence/7d0b401f3e37ca4a.webp)](visual_evidence/7d0b401f3e37ca4a.webp) | [![xp-drop-orbs rs289lc](visual_evidence/0d6ecd1b53d311c9.webp)](visual_evidence/0d6ecd1b53d311c9.webp) |
| ground-items | [![ground-items osrs239](visual_evidence/bc42383fb8de4031.webp)](visual_evidence/bc42383fb8de4031.webp) | [![ground-items rs289lc](visual_evidence/7d830fef60a7f11e.webp)](visual_evidence/7d830fef60a7f11e.webp) |
| screenshot | [![screenshot osrs239](visual_evidence/68d6d02a7a34cdfc.webp)](visual_evidence/68d6d02a7a34cdfc.webp) | [![screenshot rs289lc](visual_evidence/465bfca661eab9a4.webp)](visual_evidence/465bfca661eab9a4.webp) |

## Public API examples

| Example | OSRS239 | rs289lc |
|---|---|---|
| widget-demo | [![widget-demo osrs239](visual_evidence/ddeac8b9da86d318.webp)](visual_evidence/ddeac8b9da86d318.webp) | [![widget-demo rs289lc](visual_evidence/1e914e6fd4db9664.webp)](visual_evidence/1e914e6fd4db9664.webp) |
| widgetprobe | [![widgetprobe osrs239](visual_evidence/5fe9dcd96fb8f067.webp)](visual_evidence/5fe9dcd96fb8f067.webp) | [![widgetprobe rs289lc](visual_evidence/d3729eee91f40f32.webp)](visual_evidence/d3729eee91f40f32.webp) |
| paneldemo | [![paneldemo osrs239](visual_evidence/29c43c2252396197.webp)](visual_evidence/29c43c2252396197.webp) | [![paneldemo rs289lc](visual_evidence/f9b4f7cfcf0ebc92.webp)](visual_evidence/f9b4f7cfcf0ebc92.webp) |
| windemo | [![windemo osrs239](visual_evidence/d8791997dfb62c10.webp)](visual_evidence/d8791997dfb62c10.webp) | [![windemo rs289lc](visual_evidence/03e84a71c3f67807.webp)](visual_evidence/03e84a71c3f67807.webp) |

## Additional causal controls

- [beam-shipped-old-duplicate-f-osrs239](visual_evidence/229d3d9f6246ced6.webp)
- [beam-shipped-low-f-rs289lc](visual_evidence/bf6d55ef7781d989.webp)
- [beam-shipped-medium-f-rs289lc](visual_evidence/94b244135c94b2e2.webp)
- [beam-shipped-exact-low-f-rs289lc](visual_evidence/363f856d1733fb7d.webp)
- [beam-shipped-low-f-osrs239-enabled](visual_evidence/e5512ea416ad0e87.webp)
- [beam-shipped-medium-f-osrs239-enabled](visual_evidence/be5147ff7758c9fa.webp)
- [beam-shipped-high-f-osrs239-enabled](visual_evidence/ca97573a57453e96.webp)
- [beam-shipped-insane-f-osrs239-enabled](visual_evidence/23b881b00bb777f1.webp)
- [beam-shipped-exact-low-f-osrs239-enabled](visual_evidence/8246883de7298038.webp)
- [beam-shipped-high-f-rs289lc-arrows](visual_evidence/1170cbb6a5b674ee.webp)
- [beam-shipped-insane-f-rs289lc-arrows](visual_evidence/889f9803882f7887.webp)
- [beam-shipped-old-duplicate-f-rs289lc-arrows](visual_evidence/74a33fcf2a7e584b.webp)
- [minimap-native-producer-active-f-osrs239](visual_evidence/a191aa4ff2459a7c.webp)
- [minimap-native-producer-off-toggle-f-osrs239](visual_evidence/35a504640e653d74.webp)
- [minimenu-overflow-original-osrs239](visual_evidence/1602cfac65a39a61.webp)
- [minimenu-overflow-scrolled-osrs239](visual_evidence/67488a651b7100ff.webp)
- [minimenu-overflow-select-osrs239](visual_evidence/6ad12905ae2cd92b.webp)
- [h-sidebar-osrs239-open](visual_evidence/86fa1398197af1ec.webp)
- [h-sidebar-osrs239-collapse](visual_evidence/7db3de22b0c75a71.webp)
- [cannon-empty-osrs239-h](visual_evidence/2b190147ea1d7e17.webp)
- [cannon-loaded-control-osrs239-h](visual_evidence/1896280673a0501e.webp)
- [native-npc-footprint-live-1-osrs239-h](visual_evidence/40be60afe730614e.webp)
- [native-npc-footprint-live-0-osrs239-h](visual_evidence/885fc1ff18dfcfa3.webp)
- [Screenshot press feedback](visual_evidence/b67f08b5d9e3ee48.webp)

## Findings and limits

The [API failure analysis](API_FAILURE_ANALYSIS.md) explains why the implementations failed, including the incorrect intermediate opcode repair. The [UI verification detail](UI_API_VERIFICATION.md) covers native controls and containment. The [80-item register](REPAIR_VERIFICATION_f7526e5b0.md) preserves original reproductions and per-ID dispositions.

The legacy client explicitly lacks CS2-only Activities features and retains an enabled sidebar selection. Physical keyboard appearance was not demonstrated by the dummy desktop backend; actual touch, input focus, typing and keyboard insets were exercised. Native root164 inventory/special-orb overlap and startup script1340 remain baseline limitations. World labels can pass beneath fixed HUD text; fixed UI-to-UI example overlap was repaired. Original NaN speculation remains unproven, while invalid refresh values are now refused or safely clamped.

The existing public Artifact URL has not been updated because no publisher or connected browser is available. The full local report remains `/private/tmp/critic-artifact/report.html`; no second public URL was created.
