# upass -- notes (parity3d pass, LostCity port checked leg by leg)

Source: LostCity quest_upass. Content: OSRS-Content/osrs239-content/server/scripts/quests/quest_upass/scripts.
Driven headless: scratch runs parity_upass (21 rows) and parity_upass_kalrag (8 rows); not a committed test.

- Koftik is six cache shells caveguide1..6 (multivarbit upass_koftik_*). Only the shell name binds triggers.
  caveguide2 stands at 2449,9716 (bridge), caveguide3 at 2479,9679 (grid). Goto 2451,9717 for the bridge:
  2450,9714 is rock and gives "I can't reach that!".
- The damp cloth has two sources: caveguide2 (koftik.rs2:79; after stage spoken_koftik, and again when
  the player has none) and the pile loc upass_gear at about 2452,9715 (koftik.rs2:143, p_delay 2, refuses when
  a cloth is held). The bridge burns it via [opheldu,damp_cloth] (upass_bridge.rs2:14).
- Randas' diary: caveguide2 choice "What does it say?" opens two ~mesbox pages (koftik.rs2:136). Wait for the
  page, do not drain with stop_at none before the p_delay lands.
- Kalrag (kalrag.rs2): owner-private npc via ~upass_spawn_kalrag (upass_encounters.rs2:315) at 2356,9911,
  78 hp, huntmode aggressive; it is a real 106-tick melee fight. On death: venom on the doll only with
  ibandoll in the backpack, then the blessed spiders within 5 tiles are set to opplayer2 (kalrag.rs2:30).
- Maze, grid and rockslide obstacles are trigger-complete (upass_obstacles.rs2, upass_grid.rs2); the
  slaves (upass_slave.rs2) and the unicorn gates (upass_unicorn.rs2) are the dialogue gates.
- Iban: lord_iban.rs2 uses the wiki bolt hazard; Iban's staff is granted in upass_tomb.rs2 on the doll throw.
  Completion is [queue,upass_quest_complete] in areas/area_ardougne_east/scripts/king_lathas.rs2:155.

Implemented differently from LostCity (OSRS wins):
- Named varbits replace LC's ibanmulti bitfield (%upass_venom_on_doll etc.).
- Books are ~mesbox pages, not ~book.
- Kamen's brew no longer hurts (wiki 25 Jul 2019); paladins do not respawn once their badge is held.
- The whereami exit teleports to 0_38_151_49_53 (2481,9717, "...and back to the cave entrance."), the SAME tile as LC
  koftik.rs2:131-145; the cave_exit_upass at 2496,9713 then leads to the surface (2436,3315).

The finale (seam37 upass_iban_lock_and_exit, LostCity decides):
- Iban's bolt hit (lord_iban.rs2 [ai_timer,iban]) lands in ONE tick: damage, throw to ^upass_iban_temple_entrance
  (2143,4648), stun anim/spotanim/sound -- no player_lock and no p_delay, as LC lord_iban.rs2:25-32. The old
  player_lock + p_delay(1) + player_unlock in the npc timer was dropped by the engine whenever the player's own script
  was suspended (the doll throw at the altar: 'dropping [ai_timer,iban], which suspended while [oplocu,cave_temple_altar]
  waits'), and the player stayed locked in the exit pocket for good. The throwback to the entrance is the port's wiki
  change (LC telejumps one tile east).
- Kalrag's death is LC's shape (kalrag.rs2:1-31): [ai_queue3,kalrag] only queues the PLAYER queue defeat_kalrag, which
  turns the blessed spiders within 5 of the player (opplayer2) and puts the blood on the doll. In the npc script the
  p_delay(3) lost the active npc and `npc_coord` aborted the spider hunt.
- The doll throw lands the player at 2482,9607 (upass_tomb.rs2, LC 0_38_150_50_7) in a closed pocket. LC's end Koftik is
  caveguide5 (npc 976, LC m38_150.jm2 "0 11 7" = 2443,9607); in the OSRS cache that Koftik is caveguide6 (4532,
  multivarbit upass_koftik_end; m38_150.spawn:8), while OSRS caveguide5 (upass_koftik_temple) stands at 2170,4727 level 1.
  So [oploc1,upass_last_out] (upass_tablets.rs2) finds caveguide6, and caveguide6's Talk-to (quest_regicide koftik.rs2,
  the only binding) opens @koftik_whereami while %upass = defeated_iban.
- Koftik stands behind the cave wall: Talk-to on him answers "I can't reach that!". The way out is the Cave beside him
  (upass_last_out at 2438,9607, op1 Enter) -- LC's own mechanism -- which plays the whereami dialogue with Koftik.
- Open: OSRS caveguide5 (the temple Koftik, 2170,4727 L1) still binds [opnpc1,caveguide5] @koftik_whereami at every
  stage (koftik.rs2:186); what that Koftik says in OSRS is unsourced here.

Orbs of light (parity3e):
- The four orbs are cache multilocs caveorb/caveorb2-4 showing caveorb_vis (op1 Take only) until the orb is destroyed.
  Stand: orb4 2416,9698 (past the 3rd plank), orb3 2385,9685, orb2 2386,9677; click_loc caveorb_vis at= those tiles.
- upass_orbs.rs2 [oploc1,caveorb_vis] gives caveorb2-4 (LC had them as ground objs). Orb1 is the logtrap rock at
  2382,9668: its caveorb_vis fires the LC trap (blown 5 west); take it by disarming upass_logtrap_trigger.
