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

## More tells (2026-10-19, untranslated 820, 3282, 3389; `tools/rsprox_tells.py --anchor 7569-7572 --ours`)

| Event | Recorded |
|---|---|
| Block | `human_spear_block` (430), 554 of 554 in 820, on the raider's swing tick; the splat and `poh_stonehit` (978) on the next (solo 33 of 33). A block on a statue's swing tick replaces its 1203/4278: rocks thrown under 430. |
| The rock | `motherlode_rockfall` (645), 111 of 111: start 15, end 30, angle 0, progress 0, raw heights 900 -> 25, **from a random one of the target tile's eight neighbours** (each seen, 6-20 times), not from the statue. |
| Who it hits | nearly every rock's target stands beside the throwing statue (gap 1); the statue does not reach a raider who stepped off before it swung. |
| Facing | both statues added at one angle, the room's turn of their template facing, toward the side the raiders walk in from (3282 and 820's first room 512, 3389 and 820's second 1536); the dead forms keep it. |
| Death | D: `human_death` (836) and `poh_stone_shatter` (977, area, range 6). D+2: the statue becomes its dead form (npc transformation), the seeds (3-5 each, despawn 2999) and "The Stone Guardian slain by <name> dropped seeds for all." |
| Swing cycle | solo: 5 ticks between swings (3282 x4, 3389 x2) with the raider beside it, hitting every 2 ticks (a Grid Master 2-tick attacker); four-man: 4 (67) or 5 (51). Not settled: Near-Reality's flinch (a hit puts the next swing at +2) would give 3282's 41 -> 44, recorded 41 -> 46. |

## Fixed in our content (2026-10-19)

- The block seq `human_spear_block` (was the engine's `human_unarmedblock`) and the death sound `poh_stone_shatter`
  (area, range 6), on both statues (`cox.npc`).
- Both statues, and the dead forms, face away from the exit through the room's turn (`~cox_guardian_face_coord`): CCW
  east, THRU south, CW west. They were the engine's south in every room. Four scriptrun layouts face W, N, S and E,
  each away from its exit tiles.
- The rock drops from a random neighbour of its tile at 225 -> 6 (raw 900 -> 24), progress 0; it was thrown from the
  statue at 255 -> 10, progress 64. `test/raids/_guardian_stomp_probe.lua` (layout 0: 14 rocks, every source a
  neighbour, 15..30).
- Not changed: ours adds the dead form as a new npc where the game transforms the statue (same tile, now the same
  angle); the swing cycle above.

## Fixed in our content (2026-10-18)

- `^cox_guardian_stomp_maxhit` 15 → **35**.
- `^cox_guardian_melee_prayer_pct` 50 → **80**.
- The knockback is rewritten as above: player 1258, one sweep (440) and one hit per standing statue, no stomp.
- The stomp's splash is sent with the rock, delayed 30 cycles, instead of `catapultimpact` when it lands.
- Separately, from the wiki's Mod Ash quote: a dragon warhammer or elder maul special drains a Guardian on the hit it rolled, before the zeroing (selftest #179).
