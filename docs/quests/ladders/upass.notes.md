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
- caveguide5 exit teleports to 0_38_151_49_53, not the LC surface tile (open).
