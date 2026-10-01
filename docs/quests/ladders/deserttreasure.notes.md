# deserttreasure -- relay notes (parity3d, wiki source, driven leg by leg)

Source: OSRS Wiki + Quest Helper (LostCity has no Desert Treasure I). Content:
quest_deserttreasure/scripts/deserttreasure.rs2. Drivers: one per leg, all green.

Setup: `::setvar deserttreasure 10` skips the intro. Levels 99 combat, magic 60+ (pillars), hp food.
Single-way combat: `::passive <npc_symbol>` on every aggressive npc nearby, or "I'm already under attack".

- L1 Indiana 3178,3042 (asgarnia base row in deserttreasure.spawn); Terry 3359,3334 (primer refusal in
  archaeological_expert.rs2); Eblis at the mirrors 3214,2954 (stages 0..10).
- Smoke: Draynor trapdoor 3118,3244; Ruantun 3112,9690. Dungeon fumes: softtimer dt_smoke_hazard
  (deserttreasure.rs2:901), 20 dmg/20 ticks, never kills. 4 torches, Fareed via warm key.
- Shadow: Rasolo 2535,3431; bandit chest 3169,2970 (retry lockpick); Damis lair 2739,5091 (two forms).
- Blood: Malak 3496-3497,3478-3479; vampire trapdoors (trap1/2 handlers in the quest file).
- Ice: child 2836,3740 (feed any cake/pizza/chocolate, one item consumed, then "Yes"). Gate
  icegate_left 2838,3739 needs subquest>=2. East side is cold (x2823-2875, z3716-3830): softtimer
  dt_ice_cold every 10 ticks drains stats 1, run energy, special attack, 1 hp.
  Cave entrance 2868,3718 refuses under 5 kills (`%fd_icewarrior_trollskilled`, trollrescue_icetroll_melee1..7),
  cave lands 2874,3720, path winds east then north; Kamil 2863,3757 (fire spells; freezes).
  Ledge 2837,3804 needs death_spikedboots WORN (allowed only in the ice region, death_locs.rs2).
  Small gate icegate_right_small 2853,3810 lvl1 -> bridge lvl2. Ice blocks 2825,3807 / 2825,3811 lvl2:
  cast fire_blast (melee op is "Smash-ice", the driver only presses Attack rows). Both freed -> subquest 4.
  Child gives the diamond only with a free slot (subquest 5, ice_stage 100).
  The blocks are spawned as their BASE symbols troll_block_1/2 (m44_59.spawn, multivarbit
  fd_icewarrior_dadfree/mumfree): Talk-to runs [opnpc1,troll_block_N], which hands the block form to
  the ice mesbox and the freed form to the parent's label (trap 19). Before seam33 only
  [opnpc1,fd_troll_mum/dad] existed, so the freed parent answered nothing ("map_flag: no dialogue").
  A smashed block leaves the pool and the parent RESPAWNS in its place ~20 ticks later
  (`t.npc.await_present fd_troll_mum 25 150`). A fire blast can kill a block inside the cast's
  settle (`hp no bar -> gone`), so read dadfree/mumfree, not `await_dead_engaged`, as the proof.
  talkToTrolls (either parent, subquest 4 on the bridge): 13 pages, father/mother by name, then
  p_teleport to 2830,3740 with subquest still 4 (Quest Helper DesertTreasure.java:156-157).
  talkToChildTrollAfterFreeing (troll child, subquest 4): 6 npc pages (child, father, father,
  mother, child, child), the diamond, 1 player page. The parents stand beside the child
  (troll_frozen_1/2, visible at subquest 4-5). Dialogue source:
  https://oldschool.runescape.wiki/w/Transcript:Desert_Treasure_I (Troll child/father/mother).
- Pillars (oblix1..4 at 3221/3245, 2909/2887): use each diamond, magic 50. All four -> stage 13.
  Exterior ladder desert_laddertop 3233,2897 sealed until then.
- Pyramid: ladders are generic maplinks. Floor 1 lands 2913,4954 lvl3; down at 2909,4964; floor 2 down
  at 2846,4973 (ONLY reachable from 2845,4973); floor 3 down at 2784,4941; floor 4 lands 3233,9293.
  Hazards softtimer dt_pyramid_hazard (deserttreasure.rs2:1714): trap drops to 3233,2890 (floors 1-3
  only, agility/thieving dodge), scarab swarm, mummy. Odds are this pack's choice, not the wiki's.
  Temple door pair 3233/3234,9324 carries the player across; Azzanadra 3232,9317 -> stage 15.
- ::deserttreasure debugproc jumps to Azzanadra (skips the floors).

Differences from the guide: no ice-path slipping; hazards and cold do not survive logout; the
temple door has no swing animation; placing diamonds writes 13 directly (native 11/12 collapsed).

Where the guide's items come from (seam34 parity3f_carry_overs; every route below was
driven in build/quest_gate/p3f_dt_items3, 24/24, driver
build/seam_state/seam34/p3f_scratch/dt_items.lua). None needs a ::give except the RAW input
named in brackets, which the guide has you bring:
- Spiked boots: ::complete quest_deathplateau (the row is quest_deathplateau, NOT
  quest_death), Tenzing (death_sherpa 2820,3556) "Can I buy some Climbing boots?" 12 gp,
  then Dunstan (death_smithy 2919,3574) "Can you put some spikes on my Climbing boots?" /
  "Yes, but I still want them." [iron bar].
- Gas mask: after Plague City, Alrena's cupboard (alrenascupboardshut op1 then
  alrenascupboardopen op1, 2574,3334) -> "You find a protective mask."
- Garlic powder: pestle_and_mortar on garlic (either order) -> fd_crushed_garlic
  [garlic, pestle; garlic is also stock3 of the spice shop].
- Spice: spice_merchant_ardougne (2658,3296) op3 shop `spicestall`, stock1 spicespot
  (230 gp). Stealing from the stall works too, but the owner/guards catch you in sight.
- Cake: steal from cakethiefstall op2 (2655,3311, Thieving 5; 13/20 rolls are a cake),
  or bake one.
- Silver bar: silver_ore on fai_falador_furnace (2602,3310, East Ardougne), Smithing 20
  ("You need a Smithing level of 20 to smelt a silver bar." below it) [silver ore].
  The Ardougne SILVER shop (silver_merchant_ardougne op3) opens EMPTY in the client: its
  inv is content-declared (pack/inv.alloc), "cell 3 of shopmain:items is not mounted".
- Ice gloves: the Ice Queen's drop (drop_tables/ice_queen.rs2); hero.lua pickUpIceGloves
  drives it live.
- Eblis: use each ingredient on him; plain OR noted forms count, and the %fd_* counters
  add up across several uses (deserttreasure.rs2 dt_eblis_take), so 12 magic logs + 6
  steel bars + 6 molten glass can arrive over several trips with no bank noting. Bank
  withdraw-as-note exists (bank.rs2 bankmain:note) but no quest test drives the bank yet.
- Rewards (orchestrator note): Desert Treasure I gives quest points, Magic XP and Ancient
  Magicks; the ring of visibility comes from Rasolo mid-quest. There is no Ancient signet.
