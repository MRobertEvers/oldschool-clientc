# Guardians: recorded behaviour

Read from submissions 820 (a trio and a four-man raid) and 3282 (solo, normal), all on Grid Master worlds; see [README.md](README.md). Tool: `tools/cox/rsprox_guardians.py`. The room's full spec is `docs/minigames/cox/rooms/guardians_spec.md`; its source D1 is this data.

## Damage

| Attack | Recorded | Notes |
|---|---|---|
| Stomp (falling rock) | 47 stomps spread evenly from 1 to 35, with 35 in both the trio and the four-man | Max **35**, not scaled by party. Solo stomps hit 4, 9, 13. Near-Reality's 15 was low. |
| Melee swing, Protect from Melee on | 14 against a trio's max of 22; 19 against a four-man's 24 | At least **79%** gets through; we keep 80%. The wiki only says it "hits through"; Tubby says the prayer "reduces it by a bit". |
| Knockback at the passage | 15 and 7 (solo, both statues standing); 5 and 12 (one standing) | One hit **per statue still standing**, inside its melee roll (0–20). No stomp follows. |

The melee max comes from the standard NPC formula, ⌊0.5 + (strength + 9) × (strength bonus + 64) / 640⌋, with the wiki talk page's about +8% strength per extra member. That gives exactly the infobox's 20 solo, 22 in a trio and 24 in a four-man.

## Tells

| Event | Recorded |
|---|---|
| Stomp | On the swing tick, the rock projectile `motherlode_rockfall` (645, start 15, end 30) and `castlewars_catapult_splash` (305) as a map graphic on the target tile, delayed 30 cycles. The hit shows the next tick. |
| Statue sequences | Both 1203 (`dragon_halberd_special_attack`) and 4278 (`emote_stampfeet`) are seen with a rock thrown, so the sequence alone does not tell a stomp from a swing. The rock does. |
| Knockback | Each standing statue plays `human_scythe_sweep` (440). The raider plays `eadgar_knockout` (1258) and moves one tile (an exactmove: delay1 0, delay2 10). |

## Fixed in our content (2026-10-18)

- `^cox_guardian_stomp_maxhit` 15 → **35**.
- `^cox_guardian_melee_prayer_pct` 50 → **80**.
- The knockback is rewritten as above: player 1258, one sweep (440) and one hit per standing statue, no stomp.
- The stomp's splash is sent with the rock, delayed 30 cycles, instead of `catapultimpact` when it lands.
- Separately, from the wiki's Mod Ash quote: a dragon warhammer or elder maul special drains a Guardian on the hit it rolled, before the zeroing (selftest #179).
