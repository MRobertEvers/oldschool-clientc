Ethically Acquired Antiquities - author notes (2026-10-03, wiki-sourced; LostCity has none)

Spawns: nobody in the Fortis half stood anywhere. Now in quest_ethicallyacquiredantiquities/configs/eaa.spawn:
Herminius 1712,3163; Regulus 1701,3143; Artima 1766,3101; two citizens, two academics, two tourists spread round 1709-1716,3161-3168.
The Fortis Cothon crew are crew_man3/woman3 at 1742-1744,3136 (not man1 as the guide says). Port Sarim crew are also man3/woman3 (3039,3193 / 3042,3192) beside Stan 3039,3192. All six crew models and Stan are bound.

Visitors: the informant is random per player, a citizen or a tourist, never an academic. The roll is kept in varb11199_eaa_shame (1 citizen, 2 tourist) until stage 28. Talk to all three kinds until stage 8.
Stage 4 needs the tools (varb11195) BEFORE the second inspect of the case.

Doors/barriers: Varrock Museum storeroom door vm_store_room_door (3266,3456) is locked until you hold the storeroom key (from pickpocketing the curator, op3 Pickpocket at stage 24). The single crate eaa_large_crate (3266,3458) is searched from 3266,3459; the decoy eaa_large_crates (3267,3458) from 3267,3457. The case shows the diadem only after completion (varb11207).
Port Sarim: reach it by charter (3000 coins; crew menu "Yes, I would like to charter a ship.") or any travel.

Dialogue gates: crew favour is "Have you seen a man with a case?" then the favour option (needs 1 free slot for the sails). Artima: "I was hoping for some help." then "Go on then.". Crew at Port Sarim, stage 16: "Have you seen a grey-haired man with a case?" (stage 18). Betty needs 1 free slot for the notes. Haig stage 22: "I'm looking for Xerna's Diadem."; stage 28: "I found Xerna's Diadem...".

Shame game: five options per round (two raise, three lower), 20 per good pick, 10 per bad pick, 10 picks to reach 100%; failing resets and "I found Xerna's Diadem..." starts it again. Step sizes and turn count are ours, the wiki gives only the option lists.
Interface 881 opens in toplevel_osrs_stretch:overlay_hud only.

Different from the guide: Artima has no shop (Trade unbound, 'What do you have?' omitted); Betty's wares are the existing stub. The confession cutscene (2 scenes) is not authored: the lines are spoken as plain dialogue (ethicallyacquiredantiquities.rs2, eaa_haig_confront).
Driver: use a SHORT --name (eaa_parity): a long name truncates and the Character Creator modal blocks the inventory tab.
