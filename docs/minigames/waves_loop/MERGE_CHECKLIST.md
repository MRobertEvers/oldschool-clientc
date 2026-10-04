# Merge checklist -- the waves loop

Every var id, alloc row or other numbered allocation the waves branch adds, so that
whoever reaches `v3` second can see the collision before the pack does
(`docs/WAVES_ORCHESTRATOR.md` section 10 lesson 11: names carry the id). One row per id.
Check each id against `v3`'s `OSRS-Content/osrs239-content/pack/varp.alloc` (and the
matching alloc file for other kinds) and the raid branch's before merging; renumber
the name and every reference together.

**Seam pass 3 closer (2026-10-03):** the three ids below are allocated only inside the held patch
`patches/matthew-mbp-m4-waves-b1-seam3.eat_delay_port.content.patch`; the branch's `varp.alloc`
does not carry them yet. Keep the rows so whoever lands the patch checks them again.

**Seam pass 4 `eat_delay_land` (2026-10-04): landed.** The patch is applied; `OSRS-Content/osrs239-content/pack/varp.alloc`
now carries `7223=varp7223_consume_combo_delay`, `7224=varp7224_consume_food_delay`,
`7225=varp7225_consume_potion_delay` (its last three lines at the landing; checked free against the
branch's alloc, which ends at 7222 before them). No other id was allocated. The Gauntlet's own
`varp6682_gauntlet_eat_delay` / `varp6681_gauntlet_combo_tick` are no longer read by its eat (it uses
these three); they are still declared and reset by `gauntlet.rs2`, so no id was freed.

| Kind | Id | Name | File that declares it | Added by | Collides with |
|---|---|---|---|---|---|
| varp | 7223 | `varp7223_consume_combo_delay` | `server/scripts/player/configs/consumption/consume_delay.varp` | waves seam pass 3 `eat_delay_port` | the raid branch's content `7936c59bf9` declares the same timer as `varp7218_consume_combo_delay`; `v3` already has 7218 = `varp7218_ft_jugs` |
| varp | 7224 | `varp7224_consume_food_delay` | same | waves seam pass 3 `eat_delay_port` | raid `varp7219_consume_food_delay`; `v3` 7219 = `varp7219_ft_fluid_seed` |
| varp | 7225 | `varp7225_consume_potion_delay` | same | waves seam pass 3 `eat_delay_port` | raid `varp7220_consume_potion_delay`; `v3` 7220 = `varp7220_bv_voy_bearing` |
| varp | 7226 | `varp7226_prayer_drain_fresh` | `server/scripts/skill_prayer/configs/prayer_drain.varp` | waves seam pass 4 `prayer_land` | none at allocation: the OSRS-Content `origin/v3` alloc ends at 7222 and the raid branch's at 7220 (2026-10-04) |

**Seam pass 4 `prayer_land` (2026-10-04):** one temp varp, 7226, the per-prayer "lit since the last drain" mask (wiki Prayer:528); allocated by `tools/ss_allocate.py` (the line is the alloc's last).

When the raid branch merges after this one, its three `consume_*` varps must take this
branch's ids (or new ones) and its `consume_shared.rs2` / `consume_delay.varp` /
selftest stanza be renamed with them: the two ports are the same code under two numbers.
**Seam pass 5 `inferno_monsters_file` (2026-10-04):** five npc_var slots for the Inferno monsters, written high so the
`varn` allocator (which counts up from 0) does not reach them.

| Kind | Id | Name | File that declares it | Added by | Collides with |
|---|---|---|---|---|---|
| npc_var slot | 59 | `^inferno_var_nib_slot` | `MI/configs/inferno.constant` (monsters block) | seam pass 5 `inferno_monsters_file` | not a pack id: a raw index into `ToriRSServerNpc.script_vars` (64 slots). It shares the array with `pack/varn.alloc` ids (0-12 today; 5 and 6 are skill_combat's `npc_action_delay` / `npc_start_coord`). Check the varn allocator has not reached 59 on `v3` |
| npc_var slot | 60 | `^inferno_var_last_swing` | same | same | as 59 |
| npc_var slot | 61 | `^inferno_var_pillar` | same | same | as 59 |
| npc_var slot | 62 | `^inferno_var_dig_at` | same | same | as 59 |
| npc_var slot | 63 | `^inferno_var_spawn_tick` | same | same | as 59 (63 is the last slot, TORIRSSERVER_NPC_VAR_MAX 64) |
