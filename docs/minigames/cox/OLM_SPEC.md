# The Great Olm: the tick-level spec the content implements (2026-10-13)

Content: `OSRS-Content/.../minigame_cox/scripts/cox_olm.rs2`, constants in
`configs/cox_olm.constant`. Baseline: Near-Reality `greatolm/` (structure and
every cache id); refined by the sources below where they state a number.

Sources, in the order they outrank each other (ROOM_AGENT.md precedence):
- **M** Mod Ash, quoted on the wiki: style switch 1/5 (27 Jul 2020); phase attack
  1/3, 1/2 in the final phase (6 Sep 2021); phase-attack memory (23 Aug 2022);
  clench threshold 1/20 of base health (26 Aug 2020).
- **W** OSRS Wiki: Great_Olm, Chambers_of_Xeric/Strategies (Great Olm section),
  Perfect_Olm_(Solo), the CoX update log (29 Nov 2023 solo portal within 10;
  life siphon 10-attack cooldown). Raw text: `sources/wiki_Great_Olm_2026-10-13.wikitext`.
- **P** the OpenOSRS coxhelper tracker (live-client data): animation ids, 45-tick
  clench, first head action 4 ticks after the rise in P1 and 8 later.
- **G** solo guides (`sources/olm_4to1_research.md`, ~20 videos, two tile-marker
  exports in `sources/olm_tiles/`): the 4:1 loop and its tiles.
  Cited by name where one guide states a number: **SQ** Synq, **TG** the team
  guide (yt sACYyrEdD7w), **AY** AsukaYen.
- **NR** Near-Reality greatolm/ (not authoritative; its view boxes reproduce
  every wiki safespot and every guide tile, so they are used as the model).

## 1. Ids (resolved through the server, `sources/olm_cache_ids.txt`)

| what | name (id) |
|---|---|
| npcs | olm_head (7554), olm_hand_left (7555, MELEE hand), olm_hand_right (7553, MAGE hand), spawning forms 7551/7552/7550, olm_firewall_npc (7558) |
| locs (the visible body) | olm_head_spawning 29880 → olm_head 29881 → olm_head_cave 29882; olm_hand_left_spawning 29883 → olm_hand_left 29884 → _cave 29885; right 29886/29887/29888; olm_acid_pool 30032; olm_crystal_attack_small 30033 / _large 30034; olm_crystal_bomb 29766 |
| head seqs (plain / glowing) | spawn 7335/7383; idle front 7336/7374, right 7337/7376, left 7338/7375; turn right 7339/7381, right→mid 7340/7382, left 7341/7377, left→mid 7342/7378, left→right 7343/7379, right→left 7344/7380; attack front 7345/7371, right 7346/7373, left 7347/7372; death 7348 |
| left claw seqs | spawn 7354, idle 7355 (clenched 7365), signs: crystal 7356/7366, heal 7357, lightning 7358/7368, swirl 7359/7369; clench 7360, clenched 7361, unclench 7362, death 7370 |
| right claw seqs | spawn 7350, idle 7351, death 7352 |
| projectiles / graphics | magic auto 1339, ranged auto 1340; spheres 1345/1343/1341 (impacts 1346/1344/1342); acid 1354; deep burn 1349; fire wall 1347/1348; crystal bomb 1352 (explode 40); falling rock 1357, crystal explode 1358, shadow 1446, shatter 1338; lightning 1356; portal 1359; siphon spit 1355, siphon glow 1363; red aura 246 |

Two Near-Reality ids were wrong against the cache's own names and are not used:
its falling crystal flies the bomb's projectile (1352) and its bomb the falling
rock (1357); its acid projectile is Vespula's (1364).

## 2. Geometry (chamber-local: template m50_89 less (16, 8))

- Chamber x 11..21, z 26..44. Head loc (4|22, 34), claw locs z 39 / 29 on each wall
  (west angle 3, east angle 1). The left (melee) claw is NORTH on the west wall
  and SOUTH on the east. NPCs: head (6|23, 34), claws x 7|22 at the claw rows.
- **Views** (NR): SOUTH z 26..36, MIDDLE z 31..41, NORTH z 36..44 (whole chamber
  width). Turn target: count players in MIDDLE, in z < 36, in z > 36; a hand hit
  for damage since the last step adds one to its side; MIDDLE wins every tie;
  the west wall breaks N/S ties north, the east south.
- The guides' west tiles in this frame: mid skip (12,35), thumb (12,39), ring
  finger (12,42); east column x = 21.

## 3. The clock

- Rise: the head's spawn anim, 10 ticks after the barrier (P1) or 24 after the
  fall; the claws and the risen locs 4 ticks later. First action: rise + 4 (P1),
  rise + 8 (later) [P]. Then every 4 ticks.
- Rotation [W]: auto, empty, auto, crystal burst, auto, empty, auto, lightning,
  auto, empty, auto, teleport; the penultimate phase adds auto, empty, auto,
  left-claw protection [NR].
- A step (NR GreatOlm.attack): if nobody is in the facing view, or the step is
  not an auto and no auto is owed: an auto marks itself owed; the head turns to
  the most-players view if that differs (a special still fires if somebody is
  left in the old view); else an empty step does nothing. Otherwise the owed
  auto (if any) goes first, then the step's own auto or special. A special only
  fires with the left claw up, unclenched and not regenerating.
- The read: every step reads the tiles players ENDED the previous tick on (the
  npc phase runs first).

## 4. Attacks (numbers; [source])

| attack | rule |
|---|---|
| auto | at everyone in view; projectile (65,15), 30 + 18 + 5/tile cycles; lands at floor(cycles/30) ticks; max 27 P1-P2, 28 P3, 29 final [W]; accuracy (250+9)(60+64) vs the player's defence roll; the matching protect prayer READ AT LANDING takes 80% [W 75-80%, NR 0.2]; 1/5 style switch after attacking [M] |
| sphere | 1 in 20 autos, never twice running [NR]; any protect overhead on at launch is turned off and prayer halved [W]; lands +4 [NR]: half current hp unless its prayer is on then [W] |
| phase attack | replaces an auto 1/3 (1/2 final) [M], not twice in one 4-step cycle [NR], then two plain autos first [G]; picks among the attacks not used this phase [M]: the power's two in P1-P2, all six in P3 and the final stand |
| acid spray | 7-12 pools in view, each where its spit lands; pool 16 ticks; 3-6 + poison each tick stood on [W] |
| acid drip | one player in view; for 21 ticks a pool on every tile they walk over (a running tick pools the tile stepped through too) [TG, SQ]; standing on a pool hurts; pools poison from 2 [W] |
| deep burn | one player in view; 6 hits of 5 every 8 ticks [SQ], all combat stats -2 each, `olm_burnwithme_pl_spot` on each [W, P]; spreads to adjacent players [W] (not implemented: party size 1) |
| fire wall | rows z±1 of one player in view, 10 tiles from the far wall; lands +1 (judged on the cast tick); a player on a wall row leaps (8-10) outward or into the gap at random [W]; the row between burns 50-65 8 ticks after the walls land ("in 5 seconds") [W]; fails without both rows open [W]; doused by water/ice spells, Humidify beside a flame, a watering can or a gourd vial [W, SQ] |
| falling crystals | one player in view, 11 crystals, one a tick (from the tick after the cry) on the tile they ended the previous tick on, each landing 2 ticks after its aim (60-cycle projectile) on whoever ended the tick before there, 15-20 [W; cadence and flight: Zenyte FallingCrystal] |
| crystal bombs | 2 (3 at 8+) at tiles ≥3 apart, placed +3, blow +9; 7x7: 60 on it, 45 at 1, 30 at 2, 15 at 3 [owner, SQ] |
| crystal burst | a seedling under every player; the burst and its shatter graphic 3 ticks later (reads end of +2) [AY, P] shoves and hits 10-40 [NR] |
| lightning | 4 bolts (5 at 6+), distinct columns x 12..20, each from either end at random ("they can spawn all in one side" [SQ]); the first shows on the cast tick [P], then one tile a tick, 17 tiles; on your tile: 15-25, protect prayers off and blocked 5 ticks, bound 8 [NR, W] |
| teleport (solo) | a tile within 10 [W patch]; shown +1,+3,+5,+7; at +9 (reads end of +8): 0 on it, else 5 a tile (max 50) and moved there [W] |
| protection (P3) | the left claw heals from damage for 16 ticks, "until the next special" [TG, NR; W "two of Olm's attacks" = the two autos] |
| life siphon (final) | 1 in 16 autos, 10-attack cooldown [NR, W patch]; two tiles in view, shine +3, at +8 everyone off them takes 1-18 and Olm heals 5x [W] |
| transition / final crystals | up to 4 in the air, one at a player every 4 ticks; shadow `gargboss_debris_shadow_120` [P], lands +4 (reads end of +3): 16-25 under, 10-16 beside [W] |

## 5. Hands, phases

- Solo HP: hands 600, head 800 (600+300f / 800+400f) [W DPS calc]. Phases: 3 hand
  phases + the final stand on the last one's wall [W]; one more per 8 players.
- Damage: off-style keeps 34% (66% mitigation) [W]; no melee on the head or the
  mage hand ("I can't reach that!") [NR].
- Clench (P1-P2, mage hand up): judged at EVERY even step on the heaviest hit
  launched at the left claw since the previous even step (one hit >= 1/20 of
  base, not a killing blow) [M, MG]: that step is a clenched null (the even list
  does not advance) and the claw is invulnerable for 45 ticks [P]; generic
  specials stop while clenched. The mage hand's death: on Olm's next 2-tick poll
  the claw unclenches and that tick counts as clenched (the ghost clench: an
  even step on it is a clenched null) [MG].
- The head heals to full every 5th tick until the final stand [NR].
- P3: the first hand down regenerates at full after 50 ticks ("roughly 30
  seconds" [W]; NR 34) unless the other falls first; both down → final stand.
- Side switch: crystals fall 33 ticks from the last hand's death, the head sinks,
  rises on the other wall 24 ticks later with a new power, never the last [NR].

## 6. Rulings and open points

- Prayer on autos is read at landing (NR, the shape of Verzik's), not at the
  release: no source settles it for Olm; flights are 1-3 ticks. PROBE.
- P3 regeneration 50 ticks (wiki) over NR's 34.
- The fire wall's flames block movement (`blockwalk=all`).
- Dousing: water/ice spells (`cox_olm_douse`), the watering can and the gourd
  vial (`cox_olm_douse.rs2`), Humidify beside a flame (one tile; the reach is not
  published). Tests: cox_olm_douse, cox_olm_douse_op, cox_olm_douse_water.
- Teleport pairs for parties are not implemented: every player gets a tile.
  GAP (solo is exact).
- "Scuffed" Olm (G: some effects a tick late at random) is not modelled.
- The penultimate's head-glow on the hands and the regen bar's hitbar are not
  sent. GAP (visual only).

## 7. Found by the solver (content checks, `olm_mech` / `olm_step`)

| when | finding | state |
|---|---|---|
| ol1h t1069 | later phases acted at rise + 9: `cox_olm_rise_done` armed the action clock one higher than the countdown needs | FIXED (`sub($first, ^cox_olm_rise_ticks)`); `clock` check 0/478 since |
| ol1e t185 | an acid-drip pool replaces a crystal seedling on the same tile (both layer-10 locs); the burst still comes at first sight + 3, and its cleanup deletes the pool early | OPEN: does OSRS let a pool and a seedling share a tile? |
| ol1l t855 | fire-wall flames (npc 7558) did not block movement: a trapped player walked out | FIXED: `blockwalk=all`; a water or ice spell on a flame douses that segment (`cox_olm_douse`, routed from `player_magic.rs2`; test `test/raids/cox_olm_douse.lua`). Humidify and the watering can: not yet. The burn lands 10 ticks after the walls (the wiki: "in 5 seconds"): NR's number, kept |
| ol1u t1661 | at every phase's end the dying LEFT CLAW was retyped into `olm_head_spawning`: `npc_findallany(tile, olm_head, 8)` takes no type (olm_head was read as a distance) and the last npc in range was the claw. A second "head" stood in the chamber; the final stand's head target was the ghost | FIXED: `npc_findall(tile, olm_head, 8, 0)` in `switch_side` and `final_stand`; the same untyped search in `cox_guardians.rs2` (set hp) and `cox_points.rs2` (points cap) fixed alike; check `one_head` |
| ol1f-ol1l | portal swap (first sight + 8, min(50, 5 a tile), teleport onto it), seedling burst (first sight + 3), rotation (579 actions) | match the spec |
| ol10 t692 | falling crystals: both queues were delay 0 -- all eleven fell in one tick, each judged against the tile it had just been aimed at (unmissable; 68 hp in a tick) | FIXED: one a tick, landing the next (`cox_olm_falling_tick`) |
| the end | the Olm's death left the way out shut and the chest empty: no exit crystal change, `cox_reward_roll` never called, the chest op a message, the roll only varps | FIXED: the blocking crystal (`raids_blockage_green`, chamber (16,45)) smashes and is gone at + 2; the reward crystal (17,47) is the Ancient chest (30028); every raider rolls into `raids_rewards` (inv 581) with the wiki's common table (quantity = points x max / 131,070, herbs as seeds 1 in 3, the ancient tablet replacing a roll), uniques, clue, pet, kit, dust and the dark journal; the chest opens the cache's reward interface (Take, Take-all, Bank-all, Discard). Loot beams: not yet |

| crops review | every head projectile's hit was queued under its flight (autos at flight/30 - 1: a tick for a 2.6-tick flight; the sphere's 150-cycle flight against a 3-tick hit; deep burn floor(flight/30)): the splat, and the prayer read, came before the projectile | FIXED: hit = round(flight / 30); the sphere's flight is its 4-tick landing |
| crops review | acid-spray pools were laid at once, under spits still in the air | FIXED: laid on the head's own queue (`[ai_queue6,olm_head]`) as each spit lands; the solver prices a spit's tile from its landing |
| points | the raid's points were never shown: varp6743 untransmitted, varp4609 / varbit 5431 (the raids_overlay's) never written, the overlay never mounted | FIXED in the server (`~cox_points_show`, `~cox_hud_mount`, varp4609 / 1431 / 1432 transmitted); the solver checks points = 5 x damage dealt (`points`). OPEN: the mounted overlay draws nothing (its content script's gate) |
| ol20 t1633-1653 | falling crystals: the next aim was queued FROM the queue drain at delay 1, which fires one or two ticks later by the slot it lands in -- 1, 1, 1, then every other tick: 22 ticks of crystals for eleven, the 30-cycle projectile a tick ahead of its hit | FIXED: all thirteen entries queued at the cry (entry n lands n - 2's crystal, then aims its own; tiles in a ring of three registers, ^cox_olm_v_falling0): one a tick, landing +2, the projectile 60 cycles. (The engine quirk -- a queue from inside a drain is slot-order dependent -- is C, not touched: torirs_server_scripts.c's queue drain) |
| ol1 t457 (strategy pass) | the ghost clench with the melee hand clenched: `~cox_olm_hand_down` set the clench to run out in (poll - now) ticks, but the claw's death queue runs before the head's timer, so it ran out -- and the unclench played, and the hand could be hit -- a tick before the poll (unclench 457, the clenched null 458) | FIXED: the poll tick itself ends a running clench and plays the unclench (`~cox_olm_fight_tick`, `v_ghost_anim` in both cases); check `ghost` 0/193 |
| ol5 t542, ol41 t1028 | a killing blow clenched the dying claw: the hit was recorded uncapped, and Olm learns of the death only from the death queue three ticks on, so the even step between clenched a claw at 0 hp | FIXED: a hit that kills does not count toward the clench (`~cox_olm_prepare_hit`); check `clench` 0/18447 |
| crops review | OPEN, unverified on screen: the falling-crystal target's red aura (sp_attackglow_red 246 is sent each tick); the left claw's special signs (framing); flame styling |
