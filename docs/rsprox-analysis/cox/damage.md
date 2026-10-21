# Party scaling and failure damage (2026-10-21)

## Sources

- **osrs-dps-calc** (the OSRS Wiki's DPS calculator): `src/lib/scaling/ChambersOfXeric.ts` at
  commit `e8b1611ec2b9` (2025-03-24), its id lists in `src/lib/constants.ts` at `9d484476e946`.
- **RSProx** (the `rsprox` skill): every session with CoX's scaling varbits (`search varbit
  12285`): index 819 (rsprox.net 820, a party scaled to 53-54), 842 (843), 3265 (3282), 3268
  (3285), 3371 (3389, Challenge Mode) -- all Grid Master -- and two normal-world sessions, index
  2512 (rsprox.net 2528, rev 229, a Vespula room) and 4284 (the owner's, rev 241, 4 splats).
- **Splats** by `tools/rsprox/rsprox_player_hits.py` (rsprox-content skill §13), inside each raid
  (varbit 5424 set), tagged with the room by the last CoX npc seen.

## Scaling

`cox_scaling.rs2` matches the calculator term for term: the group levels (55 + 44 hp / 99, the
offence and defence curves), hitpoints (`hits += hits x floor(n/2)`), Challenge Mode (+50%,
Tekton's 20/35, the Glowing crystal exempt), the clamps after Challenge Mode, the singles
(scavenger beasts, vespine soldiers), Olm (`f = min(n-1, 50) - 3 floor(min(n, 50) / 8)`, the
mage claw's magic halved) and the Guardians (151 + party Mining / n). Every id list agrees.

On the wire, the boss HP HUD (varp 1683 `hpbar_hud_npc`, varbit 6100 `hpbar_hud_basehp`) against
the formula from each raid's own inputs (9540 scaled party, 12285 highest combat): **36 of 36**
(Olm's head and claws, Tekton, Vasa, the Vanguards, the Muttadiles, Vespula's portal; solo at
combat 117-126, and 53-54: head 13,600, claws 10,200), and **7 of 7** Challenge Mode readings at
exactly +50%. `olm.md` had the 54-party pools as 10,202 / 13,602: corrected.

Fixed: two attack rolls ignored the scaling. Olm's autos and the Ice demon rolled constants
(the solo level's 32116 and 25536); both now roll the npc's own scaled Magic / Ranged level
(`~cox_olm_attack_roll`, `cox_icedemon_land`), unchanged at solo with 99 Hitpoints.
`cox_olm_chamber_shot`, `cox_olm_mechanics`, `cox_icedemon` pass.

Open: **Challenge Mode cannot be started.** The party board records it
(`cox_party_reg_challenge`, varbit 6385) but nothing copies it to the raid's
`%varp7493_cox_challenge_mode`; the CM layout and rooms come with it.

## Failure damage

| Room | Mechanic | Recorded | Ours |
|---|---|---|---|
| Olm | falling crystal, own tile / 1 tile | 14-22 / 9-14 (`olm_crystal_explode` d0 14, d1 13) | 15-22 / 9-14 (olm.md) |
| Olm | crystal bomb, teleport, burst, burn | as olm.md | as olm.md |
| Olm | acid | poison splats 2-6 | 3-6 |
| Tekton | anvil sparks (`wild_falloff_meteor_blast`) | 13-16 (5) | 10-20 (wiki) |
| Guardians | falling boulder (`castlewars_catapult_splash`) | 1-35 on the tile (26), 4-31 one off (8) | 35 (from 820) |
| Ice demon | blitz (`ice_blitz_impact`), through Protect from Magic | 1-17 (11) | magic 25, prayer-scaled |
| Vasa | teleport explosion (`raids_vasanistirio_magic_impact`) | solo: 95 -> 18, 118 -> 21, 99 -> 19 (dealt 77, 97, 80) | the teleported raiders' (HP - 5) |
| Vasa | boulder (`raids_vasanistirio_range_impact`) | 0-31 at a 54 party | 18, scaled |

Every recording of these rooms is a **Grid Master** world, and Grid Master reduces the damage
its recorder takes: the recorder's own Overload (+) lands **9** five times two ticks apart (all
sessions; the wiki: "hits of 10"). So a failure number fitted on these recordings may be ~10%
low. The Vasa explosion fits (HP - 5) x 0.86 exactly, and Olm's teleport fit of 4.3 a tile is
5 x 0.86. The reduction is not one plain factor (Olm's burn lands 5, not 4), and no
normal-world recording reaches Olm, Vasa, Tekton or the Guardians to settle it. Until one does,
those values stay as fitted, marked here.

The flat 9s in every room are that Overload, not a room's mechanic.
