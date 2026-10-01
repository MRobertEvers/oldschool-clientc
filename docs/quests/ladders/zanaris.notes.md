# zanaris (Lost City) -- notes (parity3d pass, LostCity checked leg by leg)

Source: LostCity quest_zanaris. Content: OSRS-Content/osrs239-content/server/scripts/quests/quest_zanaris/scripts/leprechaun_tree.rs2.
Driven headless: scratch runs parity_zanaris (62 rows), parity_zanaris_z4 / _z (zombie drops), parity_zanaris_ent (Entrana ladder). Not committed tests.

- Zanaris is map square m38_69 here. LostCity 225 used m50_149 with the same local coords, so a coord
  copied from LostCity needs its square changed. The door lands at 0_38_69_20_56 (2452,4472).
- The leprechaun tree has no stage gate. Chop it at any stage: Shamus appears 10 tiles east, a second
  chop while he is out is his scold (chat_angry, not a <p,angry> tag).
- The Dramen tree at spoken_shamus needs Woodcutting 36, then the spirit is summoned and retaliates
  (~npc_retaliate(0)); it has no huntmode and drops nothing (death_drop null), as in LostCity.
  spirit_defeated -> the next chop cuts a branch (stage 4). Branch needs a free slot.
- Knife on branch needs Crafting 31; stage staff_made (5) is written there, not at the door.
- Door orientation: $entering false is the OUTSIDE (x 3201). Wielding the staff there teleports.
  The player_teleport proc ends in if_close, so completion is a queue that runs after it.
- Shed door from inside or unwielded: plain walk-through, stays in the swamp.
- 239 splits LostCity's one zombie_entranan into five cache variants; all share one drop label
  (drop_tables/scripts/zombie.rs2). Roll is 8/128 nothing; a single empty kill is not a bug, two in a
  row on one variant is worth a re-run.
- Entrana ladder at 2820,3374 needs cave_monk within 2 tiles; the choice sends you to 2822,9774.
- The Zanaris exit ladder (zanarisladderout 2410 / 2 5488) is in no map of this cache, so it is
  unreachable in the client. The attendant coordinate in ladder_fairy.rs2 is fixed for when a loc lands.
- doorman.rs2 / zanarismarketdoor still use LostCity 225 coords (0_50_149_*) and no doorman spawns.
