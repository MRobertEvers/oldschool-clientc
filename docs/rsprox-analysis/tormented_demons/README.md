# Tormented Demons: what Jagex's server sends

Old School's Tormented Demon (2024, While Guthix Sleeps), read from RSProx recordings. The method and caveats are the same as for [the Inferno](../inferno/README.md). Content is in `OSRS-Content/osrs239-content/server/scripts/bosses/boss_tormented_demons/`.

## Sources

| Submission | Rev | World | What it holds |
|---|---|---|---|
| 3312 | 234 | Grid Master, with names | 236 kills with a bow at the Ancient Guthix Temple |
| 1756 | 227 | Leagues V | about 300 kills with a bow; 300 lives reach the defenceless switch |
| 552 | 227 | Leagues V | 45 engagements; fire bombs thrown at other players |
| 1696 | 226 | normal | While Guthix Sleeps' quest demons (13601/13602): three fire bombs at the recorder |
| 3308 | 234 | Grid Master | 14 engagements |

The wiki's text for the mechanics is archived in `sources/` (fetched 2026-10-18). All recorded players used a bow, so rules about melee and magic attackers (e.g. the shield-bonus by weapon speed) are the wiki's alone.

Tools:
- `tools/rsprox/rsprox_td.py` splits every demon into lives and measures each rule below.
- `tools/rsprox/rsprox_tells.py --npc tormented` gives the inventory.

## How the server shows state

| State | Recorded | Ours |
|---|---|---|
| Overhead prayer | The `headicon_customisation` extended info: slot 0, sprite group 440, index 0 melee / 1 ranged / 2 magic. One npc type (13599/13600) throughout. | A type swap to `tormented_demon_protect_*`. The headicon looks the same, but this is a different mechanism (see "Engine" below). |
| Defenceless (no flames) | `body_customisation` with model 55474 or 55475 (one per variant), exactly **30 ticks after the shield first drops** (300 of 300 in 1756). Reset on respawn and on the fire bomb. | not shown |
| Shield visual | `luc2_undead_demon_shield_spot` in spotanim **slot 4** every 2 ticks (7,105 of 7,105 gaps), from the tick before the first engagement for as long as the fight lasts. The drop/restore graphics take the same slot on their ticks. | one `shield_spot` on engagement; our engine has one spotanim slot |

## Combat

| Event | Recorded | Ours before |
|---|---|---|
| Engage (the player's first attack) | `luc2_undead_demon_explosion_fire` + `luc2_undead_demon_explosion_fire_spot` (slot 4) on the attack tick. Synths 9224 `wgs_td_explosion_fire_power_up_scream_03`, 9210 `…explosion_fire_start_01` delay 3, 9215 `…explosion_explode_01` delay 36 and 9259 `…explosion_high_fire_01` delay 39. **This is the shield dropping**, not a bomb. | `shield_spot` |
| Shield back up | `luc2_undead_demon_shield_restore_spot` (slot 4) on the player's **next attack tick** (533 of 534; E+5 with a 5-tick bow) | restore on the next hit, prepared |
| Prayer on engage | Switches to the attacker's style 1–3 ticks after the drop, before any hit | a random starting prayer, kept |
| Prayer switch | Each time about 150 damage has accumulated, the head icon is rewritten to the last style it was hit by (the same style is re-sent too) | after 150, switch to the last style |
| Attack cadence | 6 ticks (1,172 of 1,784 gaps); 8 and 10 after a prayer swap's 6-tick pause | 6 |
| Melee | `luc2_undead_demon_melee` + `luc2_undead_demon_melee_spot`; synth 9219 `wgs_td_melee_attack_impact_01` | anim + spot |
| Magic | `luc2_undead_demon_firey_balls`; `luc2_undead_demon_fireball_proj` start **30**, end **118** (fixed, not by distance), peak 0, arc 160, height 86 → 30; `luc2_undead_demon_fireball_impact` on the player delayed 118 (height 124); synth 9229 `wgs_td_mage_attack_impact_02` delayed 118 | 30 + 32 + 5d, height 40, peak 16, impact at once, no sound |
| Ranged | `luc2_undead_demon_spare_ribs` + `luc2_rib_bone_shard_spotanim` on the demon; `luc2_rib_bone_shard_projectile` start **60**, end **118**, peak 10, arc 200, height 100 → 30; synth 9253 `wgs_td_demon_ranged_spare_ribs_inital_movement_01` delayed 118 | **missing**: ranged threw the fireball |
| Style choice | Melee when it can reach (1,179); otherwise magic (573) or ranged (540) at random | melee/magic by engagement order |
| Hits on the player | Both projectiles land on cycle 118, so 3 ticks after the attack | flight-based |

## Bind and fire bomb (1696; 552 for the projectile)

The bomb comes every 60–61 ticks: 1913, 1974 and 2034 in 1696. On the bomb tick B:

- **The demon:** `luc2_undead_demon_explosion_fire` + `_explosion_fire_spot` (slot 4), the same as the shield drop. The demon that binds also gets `entangle_casting` (177) at height 240 (slot 2).
- **The player:**
  - the message "The demon's spell binds you!";
  - `entangle_impact` (179) at height 124;
  - the projectile `entangle_travel` (178) from the demon: 46 → 76, peak 16, arc 64, height 45 → 0.
  - They walk again from B+3.
- **Each bomb tile:**
  - map graphic `gargboss_debris_shadow_90` (1446) delayed 30;
  - `luc2_undead_demon_fireball_large_proj` (2855) from the demon: 45 → 115, peak 30, arc 128, height 86 → 0;
  - map graphic `luc2_undead_demon_fireball_large_impact` (2856) delayed 115.
  - Two tiles per demon: the player's, and one adjacent.
- **Sounds:** synths 9224 (delayed 30), 9210 delay 3, 9215 delay 36, 9259 delay 39, 7099 `dt2_vardorvis_entangled_01` and 154 `paralyze`.

## Death and respawn

| Event | Recorded (536 of 536) |
|---|---|
| D | `luc2_undead_demon_death` + `luc2_undead_demon_death_spot` |
| D+6 | npc removed |
| D+9 | `invisible_tormented_demon_*` added at the spawn |
| D+10 | `luc2_undead_demon_summon` + `wgs_undead_demon_summon_spotanim`, transformed to the visible demon. Area sounds (range 7): 9333 `wgs_td_fire_shield_restore_floor_burn_longer_02` delay 4, 9332 `…fire_shield_restore_03` delay 128, 9336 `…explosion_charge_no_flames_02` delay 136 |

The recordings respawn 10 ticks after death, while the wiki's infobox says 21. Both recording worlds (Grid Master, Leagues V) are boosted, so we keep the wiki's 21 and the recorded order within it.

## Engine work this needs for an exact match

The 239 client parses `headicon_customisation` and `body_customisation` but discards them (`src/net/rev/osrs239/osrs239_entity_info.c`). Our server sends neither, and sends one npc spotanim per tick with no slots. Until those exist:
- the prayer stays a type swap;
- defenceless is a type swap to a record with the no-flames model;
- the shield loop shares the one spotanim slot, and an attack graphic wins its tick.
