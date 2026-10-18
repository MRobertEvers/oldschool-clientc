# The loot room

Read from 8 completions (14 and 1852), region 57,80. Full inventory:
[loot.out](loot.out).

**Ours:** `minigame_toa/scripts/toa_rewards.rs2` (not yet compared).

| Tick (from arrival) | Recorded |
|---|---|
| +0..5 | The P3 death npcs (`toa_warden_p3_death_tumeken`) and `toa_amascut_p3_enrage` are removed; synth `toa_wardens_amascut_despawn_03` (6051). `toa_teleport_crystal_continue_wardens` (45138) rot 0 and obj `toa_book_wardens` (27310) on the floor. Room song `song_731`. Area `toa_loot_purple_flame_loop_06` (5936) loops, re-sent while there (the purple-chest flame) |
| Chest opened (C) | the sarcophagus `toa_vault_sarcophagus_open` (44934) rot 2; area `toa_loot_chest_open_01` (5934) |
| C+1 | loc_anim `toa_osmumten_chest_reveal` (9505); areas `toa_loot_chest_slide_04` (5939) and `toa_loot_chest_turn_02` (5930) |
| C+8 | loc_anim `toa_osmumten_chest_open` (9506); loc `toa_vault_loot01_voucher` (55807) rot 2 (the item shown); area `toa_loot_orb_03` (5942) |
| C+15 | area `toa_loot_inv_01` (5925); the item's own loc_anim cycles, e.g. `fx_cycle_sword` (9512), `fx_cycle_ring` (9508), `fx_cycle_staff` (9514) |
| Taking | synth `chest_open` (52) and `found_gem` (2655) |

Other raiders' chests: `toa_vault_chest_open` / `_open02` / `_open03`
(44787–44789) on their tiles.

Messages: "<col=ef1020><name></col> found something special: <col=ef1020><item></col>",
"You leave the Tombs of Amascut.". Supplies left on the floor:
`toa_supply_heal_3/4`, `toa_supply_prayer_4`.
