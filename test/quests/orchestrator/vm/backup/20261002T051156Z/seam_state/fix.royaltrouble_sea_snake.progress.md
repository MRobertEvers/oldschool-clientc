# fix.royaltrouble_sea_snake progress

## step 1 (research, done)
- Sources: docs/quests/royal_trouble.md:19-20 (149, 100 hp, melee close / ranged far, poison 9, Slayer 40 to damage);
  OSRS wiki Giant_Sea_Snake (fetched 2026-10-02): hp100 att170 str90 def160 mag1 rng130, all bonuses 0, max hit 14,
  Crush+Ranged, speed 4, poison 9, size 5, 20% earth weakness, npc id 1101. Cache configs/all.npc stat1..5 = 170/160/90/100/130 agrees.
  Wiki Royal_Trouble: fight is in an instance; Protect from Melee up close, ranged from distance.
- Pool: maps/m40_160.jm2 flags 2604-2622 x 10280+ blocked (f1); walkable shingle 2609-2621 x 10273-10279.
  maps/m40_160.jl2: pier_shadow_on_water (816) at 2614-2616,10280-10282 = the snake's water spot; royal_crate/barrel/shelves
  (the nest) at 2609-2610,10273-10274 and 2619-2620,10276-10279.
- Plan: snake SW at guide tile 2615,10280 (body 2615..2619 x 10280..10284 in the water); melee from 2618/2619,10279;
  ranged anywhere. Box drop on shore 2617,10278 (npc_coord is water, unreachable). Slayer 40 gate in [opnpc2].
  Stats block in configs/royaltrouble.npc; [ai_opplayer2] melee if npc_range<=1 else ranged.
## step 2 (edits done, pack compiles rc=0): royaltrouble.npc snake block; royal_kids_boss.rs2 slayer gate op+ap twin, ai_opplayer2 melee/ranged, box at ^royal_box_coord; constant snake 0_40_160_55_40 + ^royal_box_coord 0_40_160_57_38; royal_shared spawn only at misc 110. Scratch: build/seam_state/vm-b1-seam1/snake/snake.lua --name seam_royal_snake
