# Tormented Demons: what Jagex's server sends

Old School's Tormented Demon (2024, While Guthix Sleeps), read from RSProx recordings. The method and caveats are the same as for [the Inferno](../inferno/README.md). Content is in `OSRS-Content/osrs239-content/server/scripts/bosses/boss_tormented_demons/`.

## Sources

| Submission | Rev | World | What it holds |
|---|---|---|---|
| 3312 | 234 | Grid Master, with names | 236 kills with a bow at the Ancient Guthix Temple |
| 1756 | 227 | Leagues V | about 300 kills with a bow; 300 lives reach the defenceless switch |
| 552 | 227 | Leagues V | 45 engagements; fire bombs thrown at other players, and a third demon's single bombs |
| 1696 | 226 | normal | While Guthix Sleeps' quest demons (13601/13602): three fire bombs at the recorder |
| 3308 | 234 | Grid Master | 14 engagements |

The wiki's text for the mechanics is archived in `sources/` (fetched 2026-10-18). All recorded players used a bow or a powered staff, so rules about melee attackers (e.g. the shield bonus by weapon speed) are the wiki's alone.

Tools:
- `tools/rsprox/rsprox_td.py` splits every demon into lives and measures each rule below.
- `tools/rsprox/rsprox_td_shield.py` lines the shield graphics and prayer writes up with the recorder's own attack ticks.
- `tools/rsprox/rsprox_attack_tells.py --npc tormented_demon --attack luc2_undead_demon_firey_balls luc2_undead_demon_spare_ribs luc2_undead_demon_melee` gives the tells of each attack.
- `tools/rsprox/rsprox_tells.py --timeline` gives the ordered stream.

## How the server shows state

The demon is **one npc type for its whole life**. Everything that changes is per-npc extended info:

| State | Recorded |
|---|---|
| Overhead prayer | `headicon_customisation`: slot 0, sprite group 440, index 0 melee / 1 ranged / 2 magic |
| Defenceless (no flames) | `body_customisation` with one model: 55475 on 13599/13601, 55474 on 13600/13602. It comes with `luc2_undead_accuracy_debuff` in spotanim **slot 5** (254 of 300). |
| Graphics | Numbered spotanim slots: attack graphics and `entangle_casting` in slot 2; the shield struck, drop, restore and summon in slot 4; the debuff in slot 5; the death graphic in slot 0 |
| Overhead bar | `healthbar_standard_60` (2,556 of 2,556 headbars) |

## Combat

E is the player's first attack on a fresh demon.

| Event | Recorded |
|---|---|
| Engage (E) | The tick after the player's first attack, the shield drops: `luc2_undead_demon_explosion_fire` + `luc2_undead_demon_explosion_fire_spot` (slot 4), with synths 9224 `wgs_td_explosion_fire_power_up_scream_03`, 9210 `…explosion_fire_start_01` (delay 3), 9215 `…explosion_explode_01` (delay 36) and 9259 `…explosion_high_fire_01` (delay 39). |
| The shield, per attack | Decided at each of the recorder's attacks on the demon, and only then; the demon sends no shield graphic of its own. Shield up: `luc2_undead_demon_shield_spot` (slot 4) on the attack. Shield down: no graphic, the one unshielded attack. The attack after it: `luc2_undead_demon_shield_restore_spot`, and the shield is up. 1756's engages: `−1:shield X +1:shield +3:NONE +5:restore` (242 of 280); the first attack from E + 2 is the unshielded one, an attack on E + 1 still meets the shield. After a bomb the next attack is the unshielded one (1696: 1933, then the restore on 1937). (`rsprox_td_shield.py`, "shield") |
| Not a loop | `shield_spot` looked like a two-tick loop of the demon's (7,105 two-tick gaps) only because the Leagues and Grid Master recorders attack every 2 ticks. 1696's 4-tick bow and staff get it every 4 ticks, on their attacks. |
| Prayer on engage | The first write after E is always the attacker's style. When the spawn prayer was different it comes early: E + 1 (120), E + 3 (85) or E + 5 (24) in 3312, and E + 3 to E + 15 in 1756. When the demon already prayed that style, nothing is written until the damage rule. |
| Prayer switch | On the player's attack: 940 of 941 icon writes in 1756 are on one of the recorder's attack ticks. The icon is rewritten to the last style that damaged it once 150 has been dealt since the last write, counted at the launch with that attack's own damage included (580 of 639 writes; the rest are the hit-to-attack matching). The same icon is re-sent too. |
| Stall after a switch | A write at W holds the next attack to W + 5 at the earliest: gaps 6 / 8 / 10 for a write at +1 / +3 / +5 into a cycle (232 / 146 / 36 in 3312). |
| Attack cadence | First attack at E + 6, then every 6 ticks (1,172 of 1,784 gaps) unless stalled |
| Style choice | **Per segment, not per swing.** On each explosion (the engage, a bomb) the demon rolls a style and swings it until the next: 538 single-demon lives, one with two styles. After the engage: melee 271, fireball 140, rib shards 127 (½, ¼, ¼) at every gap from 1 to 9. A rib or fireball demon throws even at an edge-adjacent player (1756, life 4378 at 1245: eight rib swings at gap 1). (`rsprox_td.py`, "segment style") |
| Melee closes | A melee demon steps toward the player on **E + 1** (239 of 263; the other 24 were in reach); a fireball or rib demon never walks (264 of 267) |
| Player's prayer read | **At the launch.** Prayer turned on during the swing tick T: 17 of 17 blocked. Turned on after T (on by T + 3): 21 of 27 hit, the rest misses. Turned off after T: still blocked (1696). (`rsprox_td.py`, "prayer read") |
| Melee | `luc2_undead_demon_melee` + `luc2_undead_demon_melee_spot` (slot 2); synth 9219 `wgs_td_melee_attack_impact_01`; the hit lands on the swing |
| Magic | `luc2_undead_demon_firey_balls`; `luc2_undead_demon_fireball_proj` start **30**, end **118** (fixed, not by distance), peak 0, arc 160, height 86 → 30; `luc2_undead_demon_fireball_impact` on the player, delayed 118, height 124; synth 9229 `wgs_td_mage_attack_impact_02` delayed 118; the player's block delayed **98** (131 of 135) |
| Ranged | `luc2_undead_demon_spare_ribs` + `luc2_rib_bone_shard_spotanim` (slot 2); `luc2_rib_bone_shard_projectile` start **60**, end **118**, peak 10, arc 200, height 100 → 30; synth 9253 `wgs_td_demon_ranged_spare_ribs_inital_movement_01` delayed 118; the player's block delayed **108** (111 of 112) |
| Projectile hits | The player's splat is on **T + 4** (135 of 135 fireballs, 112 of 112 rib shards) |
| Defenceless | Exactly 30 ticks after the last explosion: E + 30 (300 of 300 in 1756), then B + 30 after each bomb (1943, 2004, 2064 in 1696) |
| Defenceless ends | On the player's first attack after a fire bomb (1696: a cast on 1933, 1982, 2041), with `body_customisation` reset |

## Bind and fire bomb (1696, 552)

The bomb belongs to the **player**. Every demon fighting them throws on the same tick B, whether or not its own swing is due: 1913, 1974, 2034 in 1696 (60–61 apart), and three demons at once on 14748 in 552. Each demon's next swing is B + 6 (14748 → 14754).

- **Every demon:** `luc2_undead_demon_explosion_fire` + `_explosion_fire_spot` (slot 4).
- **One demon binds:** `entangle_casting` (177, slot 2, height 240) on it.
  - Its two bombs are the player's tile and one beside it.
  - Every other demon throws two bombs at tiles beside the player.
  - All tiles are distinct (1913, 1974, 2034, 14671, 14748).
- **The player:**
  - the message "The demon's spell binds you!";
  - `entangle_impact` (179) at height 124;
  - the projectile `entangle_travel` (178) from the binder's middle: 46 → 76, peak 16, arc 64, height 45 → 0.
  - They eat on B + 2 (1915) and walk again from B + 3.
- **Each bomb tile:**
  - `luc2_undead_demon_fireball_large_proj` (2855) from the demon's middle: 45 → 115, peak 30, arc 128, height 86 → 0;
  - map graphics `gargboss_debris_shadow_90` (1446) delayed 30 and `luc2_undead_demon_fireball_large_impact` (2856) delayed 115.
- **Sounds, once:**
  - synths 9224 (delayed 30), 9210 (delay 3), 9215 (delay 36), 9259 (delay 39), 7099 `dt2_vardorvis_entangled_01` and 154 `paralyze`;
  - 154 again on B + 1 (1914, 1975, 2035);
  - 9240 `wgs_td_explosion_ground_attack_player_impact_01` on B + 4, whether or not the player was caught (2038).
- **Damage:** a player standing on a bomb tile is hit on B + 4 (36 on 1917, 23 on 1978).

**The third demon** (552, 22779): in place of each swing (every 6 ticks), it plays `explosion_fire` and throws one bomb at the player's tile, with no bind and no slot-4 graphic. Synths 9221 `…power_up_shorter_quieter_scream_02` (delay 33), 9335 `…explosion_charge_no_flames_01` (delay 3) and 9215 (delay 36); 9240 on +4.

## Death and respawn

| Event | Recorded (536 of 536) |
|---|---|
| D (the killing splat's tick, 231 of 236) | `luc2_undead_demon_death` + `luc2_undead_demon_death_spot` (slot 0), an empty `headicon_customisation`, ops hidden |
| D + 6 | npc removed |
| D + 9 | `invisible_tormented_demon_*` (13603–13606) added at the spawn, ops 0b11111, both customisations empty |
| D + 10 | `luc2_undead_demon_summon` + `wgs_undead_demon_summon_spotanim` (slot 4), transformed into the demon with a headicon, ops 0b00000, level 450. Area sounds (range 7) on its tile: 9333 `wgs_td_fire_shield_restore_floor_burn_longer_02` delay 4, 9332 `…fire_shield_restore_03` delay 128, 9336 `…explosion_charge_no_flames_02` delay 136. |
| D + 18 | ops 0b00010 (Attack) — 8 ticks after the summon (235 of 236) |

The recordings respawn 9 ticks after death, while the wiki's infobox says 21. Both recording worlds (Grid Master, Leagues V) are boosted, so we keep the wiki's 21 and the recorded order within it.

Recorded spawn variants: variant 2 at 4071,4426, 4079,4430, 4123,4381, 4083,4373 and 4088,4367; variant 1 at 4117,4385 and 4088,4379.

## Implemented (2026-10-18)

**Engine (server and client)**, rev-239 NPC extended info that our server never sent and our client discarded:
- `spotanim_npc_slot(spotanim, height, delay, slot)` — the NPC spotanim block is a list of (slot, graphic).
  - The client keeps one graphic per slot (`WorldEntity_NPC.spotanims[8]`), steps each on its own clock, and merges every playing slot onto the body.
  - `spotanim_npc` is slot 0.
- `npc_headicon(slot, group, index)` — headicon customisation. The client's overhead reads the override before the type's icon.
- `npc_bodymodel(model)` — body customisation with one model (−1 resets). The client remounts the body when it changes and loads missing models first.
- Respawn returns an npc to its spawn type (LostCity `Npc.resetEntity(respawn)`: `this.type = this.baseType`). Before this, an npc respawned as whatever type it died as.
- Tick-log rows `npc_headicon` and `npc_body`; `npc_spotanim` carries the slot in column f.

**Content:**
- **One type:**
  - The prayer is `npc_headicon`.
  - The three `tormented_demon_protect_*` records are retired; they stay in the config only because the baked client cache lists them.
- **Spawns and respawn:**
  - World spawns are the invisible twins.
  - The spawn trigger queues the summon a tick after the add, with its sounds and the hidden menu, and the menu returns 8 ticks later.
  - `death_delay=5`, so the demon is removed at D + 6.
  - `healthbar_standard_60`.
- **Engage:**
  - The demon takes the attacker (`npc_attackplayer`) so the retaliation flinch doesn't re-seed its clock.
  - The first swing is at E + 6.
- **The demon's timer:**
  - It runs every tick: the engage's explosion, the shared fire bomb, a melee segment closing, and defenceless at 30 ticks.
- **The shield and the prayer, on the player's attack (2026-10-19):** see "The shield" below.
- **Attacks:**
  - Each swing is the engine's attack clock (`[ai_opplayer2]`), and a prayer swap holds it with `npc_attackdelay`.
  - Melee, magic and the new ranged rib attack all use the recorded parameters, sounds, player blocks and hit ticks.
- **The bomb** follows the recorded rules: shared by every demon, one binder, distinct tiles, a 3-tick freeze rather than `player_lock`, and the B + 4 detonation. Third and later demons throw single bombs.
- **Defenceless:** at the last explosion + 30, shown with the no-flames body and the slot-5 debuff, and ended by the first attack after a bomb.
- **Death:** the death graphic and the cleared icon on the killing splat's tick.

## Verified (2026-10-18)

`test/bosses/tormented_demon_tells.lua` fights the world demon at 4123,4381 with a bow, kills it with `::tdhp 0`, and runs `::tdtest`. Read from its tick log:

- **Engage:**
  - E: the drop and all four synths.
  - E + 1: the switch to ranged, which the headless client decoded as headicon 440/1.
  - (Superseded 2026-10-19, "The shield": the two-tick shield loop this run checked was not Jagex's.)
- **Attacks:**
  - First swing at E + 6, then every 6.
  - Ribs and fireballs with the recorded projectiles, synths and player blocks (108 / 98).
  - The player's splat on T + 4.
- **Defenceless:**
  - E + 30: body 55474 (the headless client decoded it) and the slot-5 debuff.
  - It ends on the first attack after the bomb, and comes again at B + 30.
- **The bomb:**
  - B = E + 60, with the bind, entangle, both tiles with shadow and impact, and the sounds.
  - 154 on B + 1; 9240 and the hit on B + 4; the next swing at B + 6.
- **Death and respawn:**
  - Death sequence, graphic and cleared icon on the killing splat's tick; removed at D + 6.
  - The respawn is added invisible (13604) and summoned a tick later, with all three area sounds.
- `::tdtest` passes.

## Segment styles and the approach (2026-10-19)

**Content:**
- `~td_roll_segment_style` rolls the segment on the engage (`random(4)`: two faces melee, one magic, one ranged), kept in `^td_var_style`.
- Every demon that throws the bound bomb changes to either other style, even odds (wiki: "will always change its combat style after this attack"; the 6 recorded lives with bombs all change).
- The timer walks a melee segment's demon beside the player (`npc_approach`) on each tick it is not edge-on, from the tick after the explosion.
- The swing is the segment's style. A melee segment out of reach at its swing turns to magic or ranged and keeps it (wiki).

**Lanes:** the live client's `api_drive.npcs` row gains `headicon_group`, `headicon_index` and `body_model`, which scriptrun already answered.

**Solver:** [solver.md](solver.md) is the plan; `test/bosses/tormented_demon.lua` plays it.

## The shield (2026-10-19)

The shield "never dropped" on our client: the demon's timer re-sent `shield_spot` every two ticks for the whole fight, which hid the drop a tick after it played. Now, from the recordings (`rsprox_td_shield.py`):

- **Per attack, at the launch** (`~td_prepare_player_hit`): shield up, `shield_spot` and the 20% reduction; down, no graphic and the unshielded attack; spent, the restore graphic and the shield up again.
- **The engage:** the player's first attack A meets the shield (struck); the explosion is E = A + 1, played by the demon's timer; the shield counts as down from E + 2. Every fight clock (first swing E + 6, defenceless E + 30, bomb E + 60) runs from E as before.
- **A bomb** drops it on B.
- **The prayer** is checked on the player's attack, after that attack's damage is counted: the engage switch on the attack after the engaging one, then every 150.

From the wiki alone (no recording shows them):

- **The unshielded attack** (wiki Strategies, "Shield"): X² − 16 bonus damage, ÷3 on prayer, for crush melee, crossbows and ballistae, and spells not from a powered staff; X is the weapon's speed for its style (Rapid a tick less), a spell's the cast's 5 or a slower worn weapon's. With X ≥ 5 the player's next attack comes 4 / 3 / 2 ticks later against 1 / 2 / 3 demons (a player queue that runs after the attack's own script). Demonbane spells pierce the shield (`%varp7061_magic_spell_demonbane`).
- **The first swing through prayer:** the demon's first magic or ranged swing, thrown while the shield is not back up, goes through a matching protection prayer at 25%.
- **Two demons:** one melees, the other throws. A demon engaging beside a melee partner throws; beside a thrower it melees; on a bomb the two trade.

Probe (scriptrun, Dharok's greataxe on crush, a demon praying melee): struck on A, the explosion on A + 1, the unshielded attack on E + 6 hits 11 (33 ÷ 3), the next attack 4 ticks later with the restore. The solver's 48 seeds stay GREEN (138–285 ticks).

## Open

- **The engage switch:** ours lands on the attack after the engaging one. Jagex's first write comes 0–7 attacks after the first damaging hit (1756), and the recordings don't show what decides it.
- **Hits through a held prayer:** the three 1696 hits once read as going through a held prayer were other demons' hits (a rib shard, a bomb) on the same tick; nothing recorded shows the wiki's first-swing rule.
- **Player spotanim slots:** the fireball impact is in the player's slot 2 on Jagex's wire. Our server sends players one slot (0), so the player's own graphics share it.
- **Body customisation colours and textures** are parsed and not applied. The demon's override is a model alone.
