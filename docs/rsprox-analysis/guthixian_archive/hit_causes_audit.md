# Every splat and heal, room by room (2026-10-22)

Every damage, block, poison, burn and heal splat on a raider, and every heal and
non-raider hit on a room npc, in the 28 TFA sessions on the index (4294..4321,
`--all-players --bas`: the raiders' overheads are on the wire when shown), each
given one cause by `tools/rsprox/rsprox_hit_causes.py` and diffed against our tick
logs (every `test/guthixian_archive/tfa_*.lua`, three seeds, private pack). The
per-room tables are reproduced with:

    python3 tools/rsprox/rsprox_hit_causes.py --npc '<room regex>' idx4294.txt..idx4321.txt \
        --ours <ticklogs> --by-prayer [--show '<key regex>']

A REC-ONLY cause is a missing mechanic or a stance our bots never take (decided
with a targeted test or the recording); OURS-ONLY is something Jagex never showed.
Most keys that differ only by the seq offset (`@k`) are one mechanic; a key with 1-2
splats is usually a coincidence of two causes on one tick.

The overhead is reliable only when shown: many raiders' appearance blocks never
carried one although their hits were all prayed (B0aty 27/27 at 0..10 in 4314), so
"no overhead" reads `none?`.

## Fixed

| room | mechanic | recorded | ours before | ours now |
|---|---|---|---|---|
| Ezekiel | star, second beat (S+5) | on the star or under his 3x3: 12/12; 0 elsewhere | none | the rule; `tfa_ezekiel_under.lua` |
| Ezekiel | melee under his 3x3 | 7/8 hit, 0..12 | none | the ring's roll |
| Guardian | enrage blast | one roll of 42..49 split by the raiders in the room, x1.5 off Protect from Magic: magic 21..23 (two), 14..15 (three), 10..11 (four), 8..9 (five); 21 vs 14 on one tick (4304 [274]) | 64 split by at least two, +25% a cycle, no prayer | the rule; the solo tests pray Magic for it |
| Guardian | the enrage's lift | to the flying form with the shield whole; blasts (220) and hits take it; empty, back to the grounded type in the air, no landing, the cycle going on (4294 [969..1039], [1350..1420], 4300, 4303) | stayed grounded, blasts on real health | the rule (`~tfa_g_enrage_break`) |
| Bork | the slams after a missed bomb | 14..50 off Protect from Melee, 18..33 under it (13 hits) | 14..48, prayer ignored | the rule |
| Xarth | burn (hitsplat 74) | +1 a fireball that hits, capped at 5 (980/983 splats 1..5; a fireball on a 5 left it 5, 4304 [2747]) | no cap (up to 15) | capped at 5 |
| Xarth | a corpse reaching him | absorbed: `absorb_corpse_fire` + a heal splat of all its health on him, `corpse_absorbed` slot 2 + seq + bar removed + no ops on it, deleted +2 (4/4) | deleted at once, healed hp-1, no splat | the rule |
| Xarth | a pillar's fall | a 100 splat and its bar full then empty on the destroyed npc (18/18) | none | the rule |
| Dreadnoth | the landing's crush heal | a heal splat (28..44 a raider) | healed, no splat | the splat |
| Dreadnoth | jellies under his landing | crushed: deleted (no death, no blood), he heals their health left, one splat after the raiders' (6/6 to the point: 28, 51, 3, 281, 75, 199) | not built | the rule |
| Ocellus | Untether's pools | 14..29 unprayed, Protect from Magic 430/434 zero | 20..30 | 14..29 |

## Matched (the main causes)

| room | causes |
|---|---|
| Guardian | ranged (prayer read at the throw: missiles 512 hits 0..14), vines, poison (decays 10, 9, 8 on ours too), battery overload (180..400 by party, ours 180), blasts on the shield |
| Ezekiel | melee dust, star S+3, swords' sweep, magic, volleys, sword burn |
| Bork | gong (melee 4..8 prayed), napalm 25..28, fire 2, burn 1..3 |
| Dreadnoth | slam shots (missiles 0 prayed), his melee through prayer, jellies (0 prayed), blood 7/8, landing |
| Moia | daggers (prayed 0), magic daggers, binder swings, coughs (20 on binders), binder full heals, her return heal (148 in ours, 195/215 recorded: party-dependent) |
| Xarth | ranged (prayed 0), fireballs (magic 0..11), explosions, AoE |
| Dagon'hai | ranged / magic (prayed 46/49, 45/49 zero), the cauldron's 20, shield heals 640..930 |
| Ocellus | volleys (prayed 0), cracks 3..5, strike 17..19, degen 6..7, slashes, line heals on raiders (ours now logged) |

## Raider kit (not content)

`kit:shock` (a `skeleton_killerwatt_electricshock` on the raider is always a 10: 109/109,
every room), `kit:rubybolt` (ruby bolts (e): the shooter pays the tick after the
`xbows_blood_sacrifice` on the npc), barrage and thrall splashes on npcs (no
projectile to the extra targets).

## Open

- **Smork shots** (852 recorded): they never fire in a solo fight (they shoot "every
  raider but Bork's target"); no party test of Bork exists.
- **Guardian ranged, unprayed**: 41 solo, 44..54 in four- and five-raider parties (6
  values): a party scaling of its max is likely, not fitted.
- **Dreadnoth landing, not under him, unprayed**: 15..38 fits all but 42 and 55.
- **Moia's return heal**: 195/215 recorded, 148 in our solo test.
- **Xarth's heal splats on his ranged/melee ticks** (18..114): unexplained.
- **Untested here**: Dagon'hai's Zamorak burn, Saradomin strikes, Bandos ring and
  enrage bolts (our solo test reaches none); Dreadnoth's jelly crush and the
  Guardian's post-shield blasts (no test stance produces them).

## The tick log, for this audit

- `hit_npc` rows carry the trigger>script that dealt them (`[ai_queue2,X]>[proc,..]`
  a raider's attack; `[queue,tfa_room_watchdog]>[proc,tfa_g_overload]` a room's);
  `npc_hitmark` splats are rows too (`hitmark ...`), so scripted damage is visible.
- `npc_heal` labels are trigger>script as well (a cheat reads `[debugproc,..]`).
- `heal_player` (new): a heal splat on a raider (`hitmark_bar` with a heal), its own
  kind so nothing summing `hit_player` damage counts it.
