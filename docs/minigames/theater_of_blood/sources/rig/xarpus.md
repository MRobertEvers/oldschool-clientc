# Xarpus rig join (ToB, 2026-10-03, rig pass 1)

Table: xarpus.tsv (205 rows, written by build/rig_state/matthew-mbp-m4-raid-b1-rig-tob/xarpus_rig.py, which imports tools/gen_npc_combat.py load_catalog / seq_features / load_sound_pack read-only; catalog out/osrs239_anims).

## NPCs (16 records, 15 on the Xarpus rig)
- Normal 8338 static, 8339 feeding (size 3, ready+walk absorb), 8340 combat (size 5, ready+walk idle), 8341 xarpus_death (size 5, NO readyanim, walkanim death_b; model 35382).
- Entry (_story) 10766-10769 and Hard (_hard) 10770-10773: same anims per form. tobquest_xarpus 11187 = combat form.
- Pets: poh_verzik_pet_xarpus 10765, verzik_pet_xarpus 10874 ("Lil' Xarp", ready+walk pet_idle).
- tob_stranger_xarpus 11167: human_ready / human_walk_f on framemap 0 (3,905 seqs, shared); no seq name carries "xarpus", so no candidate (one placeholder row).
- Dump lines: docs/.../sources/cache_npc_xarpus.txt (static 1, feeding 21, combat 41, death 63, story 82+, hard 163+).

## The rig
One framemap, 1811, for ready, walk and every npc form (ready and walk never split). It holds 7 sequences: 8058 idle (52f, 4.0t), 8059 attack_ranged (21f, 2.0t), 8060 absorb (13f, 2.0t), 8061 fly_up (27f, 3.0t), 8062 death_a (10f, 1.0t), 8063 death_b (16f, 2.0t), 9033 pet_idle (52f, 4.0t). Rig is closed (not shared): the join is exact.
Off-rig by name (tier name): exhumed_start/loop/end 8064-8066 on framemap 1820, acid_splat_start/loop/end 8067-8069 on 1816; these are loc animations, not npc animations. Spotanims by name: 9 rows (acidpool_end_0..3, acidspit, acidsplash, exhumed_end, exhumed_energyorb, guano); only exhumed_end and acidpool_end_* use a Xarpus sequence (8066 2.0t, 8069 0.53t); acidspit/acidsplash/energyorb/guano borrow verzik_blood_projectile 8133, watersplash 465, book_spin 3354, myq3_vampire_teleport_projectile_anim 4730.
Candidates per npc: 7 on rig + 6 off-rig name = 13 (x15 npcs), plus 9 spotanim rows, plus the stranger row.

## Roles and tiers
- ready: 8058 idle (bound, combat forms), 8060 absorb (bound, static/feeding), 9033 pet_idle (bound, pets).
- attack: 8059 attack_ranged, rig+name; its frame sound 3290 is named tob_xarpus_attack_ranged_projectile_5 (rig+sound agrees). Only attack candidate; no special.
- spawn: none on the rig.
- death: 8062 death_a (1 tick, sound 4014 death_wingflap) and 8063 death_b (2 ticks, sound 3549 death_screech): rig+name; 8063 is also bound as walkanim of the xarpus_death records. Two death candidates; which plays on which body rests on the script comment (rs2:1629-1640) and our render, not on the rig.
- transition: 8061 fly_up stays tier rig, role unknown by the rule (name states a motion; frame sounds named wingback / forward_wings). The plugin (TobIDs.java:213) and script call it the phase 2 transformation: plugin claim only.
- defend: none on the rig.

## What stays unknown
Role of fly_up beyond the plugin; whether death_a or death_b is the live combat-form death (cache gives death_b only to the separate dead body); no defend, no spawn animation exists on the rig, so the client plays none.

## Ledger disagreements (npc_combat/, all source = generated)
No row names an animation off the rig or from another monster; every Xarpus ledger names death_a / attack_ranged / defend null.
- x/xarpus_death, xarpus_death_story, xarpus_death_hard: death_anim = tob_xarpus_death_a, but the record binds death_b (8063) as its only animation. Not off-rig; the dead body gets the combat form's death by default.
- static/feeding forms (8338/8339 and story/hard) carry attack_anim attack_ranged and a death_anim though the forms never attack or die in the script: harmless generated rows, rig+name.
- tob_stranger_xarpus: all "-" (shared rig): correct.

## Spec disagreements (encounters/xarpus.tsv, .av. rows)
- xarpus.av.p2_start.seq: 8061 on rig 1811, grade D [plugin]; role "transformation" is above the rig tier (rig, role unknown). Keep D; do not raise.
- xarpus.av.death.seq grade A: 8063 is on rig 1811, bound on xarpus_death, name states death: tier holds for the dead body. It does not cover the combat-form death 8062 (death_a), which the ledger plays and no .av. row names or measures.
- xarpus.av.exhumed_open.seq and xarpus.av.splat_land.seq, grade A: 8064 / 8067 are loc animations on framemaps 1820 / 1816, off every npc rig; grade A rests on the loc record's own anim (cache-bound), role "appear" by name only. Acceptable as cache-bound; not a rig fact.
- xarpus.av.death_pools.gfx grade A: spotanims acidpool_end_0..3 -> anim 8069; names state the role (name tier).
- xarpus.av.spit.seq (8059, D): the rig tier is rig+name and rig+sound, better than the grade shows.
- p1.idle_seq, p2.idle_seq (A, bound): hold.

## Unused candidates
Every rig+name sequence is used by a script (8059 rs2:1034, 8061 :699, 8063 :1660, 8062 via the ledger). Candidates with no .av. row: 8062 death_a and its sound 4014 death_wingflap; 9033 pet_idle (pets are outside the room); spotanim acidsplash 1556 / guano 1557 / energyorb 1550 not named by any .av. row (1550 is the heal orb row xarpus.av.exhumed_heal.proj, so used; 1557 guano and 1556 acidsplash: 1556 is the splat_land row, 1557 unused).

## Only a recording or a plugin constant can settle
Whether the live combat form plays 8062 or the dead body 8063 on the kill tick; fly_up's role; the order of the three wingflap sounds against ticks; the death_pools removal wave timing (M124).

## Closer audit (rig-tob closer, 2026-10-03)
(1) The only animation field on xarpus_death is walkanim=tob_xarpus_death_b (cache_npc_xarpus.txt:63-67), and x/xarpus_death.combat:15 says death_a: holds. Not a rig fault, because the script plays death_b on the changed type itself (tob_xarpus.rs2:1660). (2) xarpus.tsv:77 p2_start.seq is 8061 [plugin] D: holds. (3) 8058, 8061, 8062 and 8063 are all on 1811: holds. No strikes. 1557 guano is a spotanim, not a rig animation, so it is not counted as UNUSED.
