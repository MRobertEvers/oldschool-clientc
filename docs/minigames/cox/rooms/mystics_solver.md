# Skeletal Mystics -- solver plan (phase 4)

Target shape: `script/plugins/quest_driver/cox2_olm.lua` (a `QD.T2.register` room) and
`test/raids/cox_mystics.lua`, entered with `::coxtest 9 <n>`. Spec: `mystics_spec.md`.
No code this round.

## The strategy a strong solo human plays

From V1 (Synq 0:31:53), W2 (CoX/Strategies), V2 (Zulerah's luring guide), V3 (Extile, Gnomonkey, edb0ys, Tubby):

1. Gear: ranged with the **salve amulet (ei)** (they are undead; W2 "A salve amulet(ei) will help"), **Protect from Magic** for the whole room, Rigour/Eagle Eye. Weapon: toxic blowpipe (fast, low defence is not their case but it is the WDR minimum's ranged weapon) or the dragon crossbow with ruby bolts (e) at range; Synq: "the bofa and blowpipe are also commonly used".
2. **One mystic at a time.** Their aggro is by sight within a large radius: approach so only one sees you (Synq "Sometimes you can isolate the first mystic without having to aggro the others"; Tubby "try to attack one mystic without aggroing the other two ... fairly large aggro radius").
3. **Corner trap** where the layout allows: come close in the open so the mystic switches to melee (a straight walk to you exists; 50% after each swing), then put an obstacle between you so its naive step is blocked east or west of it (Synq "They'll get trapped on objects either east or west of them"); stand diagonal to it, never orthogonally beside it (Synq "Stand diagonal of the mystics so you don't take any melee damage"). A trapped melee mystic does not swing (Extile). It flips back to magic eventually (8 ticks out of its reach box, or after a cast): re-prompt it. "Losing DPS while luring them isn't worth it" (Synq) -- keep shooting.
4. Without a trap: shoot from range under PfM; the cast is halved and only one mystic shoots.
5. On the last kill the portals appear; take the end portal (Travel-End from the start/middle portal) or walk out.

## Perception (api_drive, both lanes)

| fact | verb | notes |
|---|---|---|
| mystics (7604-7606), tiles, HP | `api_drive.npcs(0)`: `npc_id`, `server_x/z` (2x2 SW), `health_ratio` | |
| who is aggroed on me | npcs row `face_x/face_z` + first-seen casts/walks | there is no target field on the row |
| a cast and its kind | npc `seq_id` 5523 + its graphic (`spotanim_sent_id` 1322 fire / 1321 vulnerability) | landing: projectile 130/168 `cycles_left` |
| incoming magic | `api_drive.projectiles(0)`: 130 / 168 with `target` = me, `cycles_left` | lands on the hitsplat tick |
| a punch | npc `seq_id` 5485 | lands that tick |
| melee style | inferred: a mystic that walks toward me instead of casting | the style is not sent; infer from movement + no cast for >= 4 ticks |
| obstacles | `tiles_nearest`, `route`, collision via `aplan` | for the corner-trap tile search |
| exit / portals | `api_drive.locs`: 29741 Mark of Power gone, 49996-49998 appear | |

Missing / engine work:

- **E2 line of sight / line of walk on both lanes**: the trap and the "isolate one mystic" both need "can mystic M see me from tile X" (LOS, its aggro) and "is there a straight line of walk" (its melee reroll). `server_los` exists on the live plugin only; scriptrun has no such bind. Needs one C binding registered on both lanes: `api_drive.los(x1,z1,x2,z2,level,size,kind)` with kind = sight | walk.
- **E3 npc naive step**: `api_drive.npc_step(slot, toward_x, toward_z)` -> the tile a size-N npc's naive one-tile step lands on (or itself when blocked), the same `findNaivePath` the server mover uses. The corner-trap search is "a tile T, diagonal to the mystic, where its step toward T is blocked"; the bot must ask the server's own rule rather than reimplement it in Lua.
- E1 (npc phase order, see `guardians_solver.md`) affects the punch's flinch timing only marginally here.

## Threat field

| term | window | where | E[dmg] / max | prayer |
|---|---|---|---|---|
| fire blast | cast T (seen), lands T + 1 + floor((1+d)/3) | anywhere in its LOS within 10 | acc x 12.5 -> halved under PfM / 25 (12 PfM) | PfM at the cast |
| vulnerability | as fire, 1 in 5; a landed one drains 4-8 Defence | as fire | as fire | PfM |
| punch | swing T when orthogonally beside its 2x2 at end T-1 | its 4-neighbourhood | acc x 12.5 / 25 (12 PfM) | PfM (melee) at the swing -- the bot keeps PfMagic, so punches are avoided by position, not prayer |
| extra aggro | a second mystic that sees me | its LOS within 15 | doubles the field | -- |

## Mode machine

`ENTER` (prayer PfM, Rigour, salve on, weapon) -> `PICK` (the nearest mystic that can be engaged while at most one other can see me; score tiles by how many mystics see them via E2) -> `LURE` (step into its open line until it walks at me = melee) -> `TRAP` (move to a diagonal tile behind an obstacle per E3; hold) or `KITE` (no trap tile: shoot from max range out of the others' sight) -> `SHOOT` (every weapon cycle, from the trap or kite tile) -> next `PICK` -> `EXIT` (portal or walk). `RESCUE`: HP threshold -> step out of every LOS, Xeric's aid (+); Defence drained by vulnerability -> revitalisation (+) only if the drain is large.

## Kit (start from `test/raids/cox_olm_solo.lua`)

The Olm kit already holds what this room needs: **salve amulet (ei)** (in the We Do Raids minimum), toxic blowpipe with dragon darts, dragon crossbow + ruby dragon bolts (e), black d'hide body/chaps, CoX (+) potions. Wear the salve instead of the fury for this room (one swap). No new items.

## Probes

- P1 per mystic per tick: style inferred vs actual (cast / walk / punch rows), and whether it is trapped (tile unchanged while not adjacent).
- P2 cast landing tick vs predicted `1 + floor((1+d)/3)`.
- P3 damage under PfM vs without (halving).
- P4 how many mystics see the bot each tick (aggro discipline).
- P5 portals appear on the last death; Travel moves the bot.

## PASS

All mystics dead and the player alive on 48 seeds, with **0 punch damage** (a strong human never stands beside one), damage taken per mystic reported (the field's expected cost is the yardstick), and no `abort` / `no primary player bound` in stdout. First probe (2026-10-18, no armour, no prayer, `scratchpad/probe/after_m`): casts at 31-33 landed 4 ticks later at range, accuracy rolls splashed several, punches only when beside one (5485 at 39/43/47), a melee-style mystic walked one tile a tick at the player; the unarmoured player died at tick 57 to three mystics at once -- the case the strategy avoids.

## Round 2 (2026-10-18): built and swept

`script/plugins/quest_driver/cox2_mystics.lua` (QD.T2 room `cox_mystics`) and
`test/raids/cox_mystics.lua`. **48/48** (`--name my0..my47`): every mystic dead,
the Mark of Power seen gone and the portals up, the exit Passage taken, 0 punch
damage (0 punches in 48 runs); damage taken 157-369 (median 244, the overload's
50 included).

As built (it differs from the plan above where the probes said so):
- Watchers (`B.watcher`, the planner's line-of-sight rule) price every tile a
  mystic sees: an awake one 0.8 a tick (max 3), a sleeping one 1.5 (waking it);
  the 2x2's side is forbidden, two tiles when that mystic stepped last tick (a
  melee mystic walks in and punches on the next read).
- THE CLICK: from out of the blowpipe's reach (5) and more than three tiles from
  every mystic, an Attack on the target (`intents.world_op`), latched until I
  swing or a mystic comes within three tiles; then the plan shoots. The plan
  alone found no shot in the maze for hundreds of ticks; toggling the click tick
  by tick shot 3 times in 300 ticks.
- The target: a mystic within three tiles first, then the awake one with the
  fewest hitpoints. Going past a close one for another walked me beside it.
- Xeric's aid drains Ranged: a revitalisation (+) when Ranged is 5+ under its
  base, or under it with the overload's re-boost more than 8 ticks off (the
  first probe landed 15 of 90 swings without it).
- No corner trap and no new drive verb: E2 (line of sight on scriptrun) was not
  needed -- the watchers are the C planner's own sight test.
