# RuneLite plugin code for Chambers of Xeric — what is pinned and why

Fetched 2026-10-02. Each plugin hub plugin is a one-file manifest in
`runelite/plugin-hub/plugins/<name>` naming a repo and a commit; every clone below is a
shallow fetch of exactly that commit, and ONLY the files that encode mechanics were copied
(configs, panels, overlays and tests were left behind). Rank: code a competitive player
relies on, below Jagex statements and the cache. A plugin observes the client; it is
evidence of what the server did, not a specification of it.

| Folder | Hub plugin | Repo | Commit | Files kept | What it encodes |
|---|---|---|---|---|---|
| `cox_qol/` | cox-qol (Buchu's "CoX Additions") | MoreBuchus/buchus-plugins | 64ee848dda5e3b443e739ada61af04e3d7d29c51 | CoxAdditionsPlugin, CoxAdditionsVarbits, overlay/{CoxHPOverlay, OlmHpPanelOverlay, OlmPhasePanel, OlmSideOverlay, VanguardInfoBox, InstanceTimerOverlay} | Olm phase / hand tracking, Olm and Vanguard HP, instance timer, varbits |
| `cox_analytics/` | cox-analytics | DangItOSRS/cox-analytics | 60c3d373ef8956d60b549dc90bd2a37e7f843cb9 | CoxAnalyticsPlugin, CoxAnalyticsVarbits | room splits, points varbits |
| `cox_assistant/` | cox-assistant (package `coxmegascale`) | caasssh/cox-assistant | 3e1667bbd56e9e3dcf3da1ab82d9b13333151870 | calc/{RaidMath, DefenceTracker, CoxDefenceProfiles, SpecPlanner}, detect/{RaidRoom, RaidRoomDetector} | scale-dependent hitpoints/defence math, Mystics spec planning, room detection |
| `tekton_reset_tracker/` | tekton-reset-tracker | brodloy/tekton-reset-tracker | a1541c37c133addff3f32e33bad131409e2a08fa | TektonResetTrackerPlugin | Tekton reset / anvil phase timing |
| `harrisun_mystic_ui/` | harrisun-mystic-ui | opaf-osrs/harrisun-mystic-ui | 05ec0fbc6d29cd9f206b5fdb0b6673a9968f5040 | MysticHudPlugin | Skeletal Mystic attack/shield timing |
| `lizardman_shaman_minion_alert/` | lizardman-shaman-minion-alert | baloooouu/lizardman-shaman-minion-alert | 86e111c356f30bed4191fed36ef6c29f6533e9d3 | LizardmanShamanMinionAlertPlugin | shaman spawn/minion rule |
| `crab_stun_timers/` | crab-stun-timers | AnkouOSRS/crab-stun-timers | 84d0186027384e25c0af53684841d4bf699dc731 | CrabStun, CrabStunPlugin, TeamSize | Jewelled/Scavenger crab stun durations by team size (generic crab plugin; check it applies to CoX) |
| `openosrs_coxhelper/` | (not on the hub) OpenOSRS `coxhelper` | JourneyDeprecated/OpenOSRS | 99587b920b7029fa9b6cf0652f28f729371fcf5a, `runelite-client/src/main/java/net/runelite/client/plugins/coxhelper/` | CoxPlugin, CoxOverlay, CoxInfoBox, CoxConfig, NPCContainer, PrayAgainst, Victim | Olm action clock, Tekton/guardian/Vasa attack timers, `crippleTimer = 45` |
| `de0/` | cox-additions (already held before this pass) | dey0/pluginhub-plugins | 48a8c0a1ecbc40541e2e83d9f8793bf5640a63f5 | held copies identical to this commit except CoxThievingPlugin.java | Vanguard timers, thieving, precise timers |
| `de0_pluginhub_48a8c0a/` | cox-additions | dey0/pluginhub-plugins | 48a8c0a1ecbc40541e2e83d9f8793bf5640a63f5 | CoxThievingPlugin.java | the pinned-commit version that differs from `de0/CoxThievingPlugin.java` (the earlier copy is older; neither replaced) |

## The path `docs/minigames/cox/sources/runelite/CoxPlugin.java`

COX_PLAN.md section 10 links `sources/runelite/CoxPlugin.java` and claims it is "in
tree". It was **not**: no `sources/runelite/` directory existed. The file the plan
describes (olm action clock, `crippleTimer = 45`) is OpenOSRS's `coxhelper/CoxPlugin.java`,
now pinned at `sources/openosrs_coxhelper/CoxPlugin.java`. The plan's link is a dangling
path and was left unedited (that doc is not this pass's to change); spec workers must
quote the openosrs_coxhelper copy.

## Probed and not found

- Hub plugins named `cox-helper` and `olm-related`: **no such hub plugin** (hub tree
  listing, 2834 entries, 2026-10-02; names matching cox/xeric/olm/raid/tekton/vasa/
  vanguard/muttadile/crab were read). `cox-helper` exists only as the OpenOSRS `coxhelper`
  package above.
- AdvancedRaidTracker (capslock13, d66a7075d6) has **no CoX code** (no match for olm,
  tekton, xeric, vasa in its sources); ToB/ToA only.
- Plugins read for names only and not cloned (UI, loot, scouting, party, storage, bank:
  cox-bank-viewer, cox-clipboard, cox-drink, cox-light-colors, cox-mega-scale,
  cox-scouter-external, cox-scouting-qol, cox-special-loot-hider, cox-storage-planner,
  coxscavcalculator, crab-scouter, low-detail-chambers, no-trolls-in-raids, raid-*).
  `raid-damage-tracker` (osrschak-commits, f4d0472) and `crab-solver` were read as
  hub manifests only.
- `raid-speed-run-tracker` is `disabled=true` in the hub.
