# Chambers of Xeric — downloaded sources (fetch ledger)

Everything the CoX plan, the research log and the per-encounter specs cite, pulled down so that an edit upstream cannot
silently move the acceptance target. Shape follows `docs/minigames/tombs_of_amascut/SOURCES.md` and the Theatre of Blood
sources README. The index of what each folder is worth is [`sources/README.md`](sources/README.md); the plugin code is
described in [`sources/PLUGIN_HUB_README.md`](sources/PLUGIN_HUB_README.md).

**Fetch date for every row below: 2026-10-02**, unless a row says otherwise. Wiki and Blert requests: one per second or
slower, sequential, `User-Agent: 3draster-cox-research/1.0 (mrobertevers@gmail.com)` (tools/toa_fetch_wiki.py sends
`3draster-toa-research/1.0 (mrobertevers@gmail.com)`). Nothing here is authored by this project except the two derived
TSVs named below. A pinned file is never replaced: re-fetches add a `.revNNN` file beside it.

Rules carried from the task: no provenance tag was loosened, no `[Mn]` removed; where no source settles a number the plan
keeps its `[Mn]`.

## 0. What was already held before this pass (kept)

| Path | What |
|---|---|
| `sources/de0/` | dey0 plugin files: ChestData, CoxThievingPlugin, CoxTimersPlugin, CoxUtil, CoxVanguardsPlugin, PreciseTimersSetting. Identical to dey0/pluginhub-plugins@48a8c0a1ecbc40541e2e83d9f8793bf5640a63f5 except CoxThievingPlugin.java, which differs (older copy; the pinned-commit file is `sources/de0_pluginhub_48a8c0a/CoxThievingPlugin.java`) |
| `sources/nr/room_coords.txt` | Near Reality room coordinates (from the RSPS-NEAR-REALITY tree; not re-fetched) |
| `sources/transcript_edb0ys_cox.md` | TheEdB0ys, `uM2VZicSSZM`, 1:35:22 |
| `sources/transcript_tubby_solo_cox.md` | Tubby, `595L4MEheXc`, 51:49 |
| `synq_transcript.md` | Synq, `klhBxOH8reQ`, 3:54:55 |

## 1. Update: newsposts (namespace 112), first-party, pinned raw

Found by `list=search&srnamespace=112` over 24 queries (the raid, every room and boss, Olm, Xeric, Kebos, Raids, Olmlet,
Overload, crabs, scavengers; ~320 candidate titles), then every candidate's wikitext was read and filtered for CoX terms;
137 posts that mention the raid or a room in substance were pinned with `tools/toa_fetch_wiki.py` into `sources/newsposts/`
(`manifest.tsv` there: title, revid, revision date, file). Posts that only name the raid in passing (Guardians of the Rift,
Christmas events, Olm as a pet) were excluded unless they carry CoX text. The query list and candidate/hit tables are in
`build/corpus_tmp/cox_news_candidates.json` and `cox_news_hits.json` (not committed). Posts that state a number are listed
with quotes in `build/spec_state/matthew-mbp-m4-raid-b1-spec-cox/corpus.newsposts.md`. The runescape.com originals return
HTTP 403 to non-browser clients and were not tried.

Finding: **no newspost states an npc attack speed, tick cadence, projectile flight or Olm phase length.** The only cadence
number is "Olm's healing pool attack now has a 'cooldown' of 10 attacks" (29 Nov 2023).

| File | Page | Revision | Revision date |
|---|---|---:|---|
| `wiki_Update_Chambers_of_Xeric.wikitext` | [Update:Chambers of Xeric](https://oldschool.runescape.wiki/w/Update:Chambers_of_Xeric?oldid=11842607) | 11842607 | 2020-03-16 |
| `wiki_Update_Chambers_of_Xeric_Tweaks.wikitext` | [Update:Chambers of Xeric Tweaks](https://oldschool.runescape.wiki/w/Update:Chambers_of_Xeric_Tweaks?oldid=11840085) | 11840085 | 2020-03-16 |
| `wiki_Update_Chambers_of_Xeric_Challenge_Mode.wikitext` | [Update:Chambers of Xeric: Challenge Mode](https://oldschool.runescape.wiki/w/Update:Chambers_of_Xeric:_Challenge_Mode?oldid=11839833) | 11839833 | 2020-03-16 |
| `wiki_Update_Chambers_of_Xeric_Challenge_Mode_Is_Here.wikitext` | [Update:Chambers of Xeric: Challenge Mode Is Here!](https://oldschool.runescape.wiki/w/Update:Chambers_of_Xeric:_Challenge_Mode_Is_Here!?oldid=11839804) | 11839804 | 2020-03-16 |
| `wiki_Update_A_Taste_of_Hope.wikitext` | [Update:A Taste of Hope](https://oldschool.runescape.wiki/w/Update:A_Taste_of_Hope?oldid=11842579) | 11842579 | 2020-03-16 |
| `wiki_Update_Chambers_of_Xeric_Revisited.wikitext` | [Update:Chambers of Xeric Revisited](https://oldschool.runescape.wiki/w/Update:Chambers_of_Xeric_Revisited?oldid=11842611) | 11842611 | 2020-03-16 |
| `wiki_Update_Chambers_of_Xeric_Kebos_and_Collection_Log_Changes.wikitext` | [Update:Chambers of Xeric, Kebos, and Collection Log Changes](https://oldschool.runescape.wiki/w/Update:Chambers_of_Xeric,_Kebos,_and_Collection_Log_Changes?oldid=14858684) | 14858684 | 2025-03-06 |
| `wiki_Update_Boss_Heads_and_Chambers_of_Xeric.wikitext` | [Update:Boss Heads and Chambers of Xeric](https://oldschool.runescape.wiki/w/Update:Boss_Heads_and_Chambers_of_Xeric?oldid=11842599) | 11842599 | 2020-03-16 |
| `wiki_Update_Bounty_Hunter_Feedback_Changes.wikitext` | [Update:Bounty Hunter Feedback Changes](https://oldschool.runescape.wiki/w/Update:Bounty_Hunter_Feedback_Changes?oldid=12386491) | 12386491 | 2020-05-16 |
| `wiki_Update_Chambers_of_Xeric_Improvements.wikitext` | [Update:Chambers of Xeric Improvements](https://oldschool.runescape.wiki/w/Update:Chambers_of_Xeric_Improvements?oldid=12485287) | 12485287 | 2020-05-28 |
| `wiki_Update_A_Kingdom_Divided_Arceuus_Spellbook_Rework_Poll_Blog.wikitext` | [Update:A Kingdom Divided & Arceuus Spellbook Rework Poll Blog](https://oldschool.runescape.wiki/w/Update:A_Kingdom_Divided_&_Arceuus_Spellbook_Rework_Poll_Blog?oldid=14858673) | 14858673 | 2025-03-06 |
| `wiki_Update_A_Night_At_The_Theatre.wikitext` | [Update:A Night At The Theatre](https://oldschool.runescape.wiki/w/Update:A_Night_At_The_Theatre?oldid=14065714) | 14065714 | 2021-04-30 |
| `wiki_Update_A_Kingdom_Divided.wikitext` | [Update:A Kingdom Divided](https://oldschool.runescape.wiki/w/Update:A_Kingdom_Divided?oldid=15180065) | 15180065 | 2026-04-18 |
| `wiki_Update_A_Mysterious_Quest_Boss_Rewards.wikitext` | [Update:A Mysterious Quest: Boss Rewards](https://oldschool.runescape.wiki/w/Update:A_Mysterious_Quest:_Boss_Rewards?oldid=14352390) | 14352390 | 2022-12-13 |
| `wiki_Update_Chambers_of_Xeric_Changes_Path_of_Glouphrie_More.wikitext` | [Update:Chambers of Xeric Changes, Path of Glouphrie & More](https://oldschool.runescape.wiki/w/Update:Chambers_of_Xeric_Changes,_Path_of_Glouphrie_&_More?oldid=14454622) | 14454622 | 2023-08-20 |
| `wiki_Update_Chambers_of_Xeric_Scouting_Scaling.wikitext` | [Update:Chambers of Xeric: Scouting & Scaling](https://oldschool.runescape.wiki/w/Update:Chambers_of_Xeric:_Scouting_&_Scaling?oldid=14474288) | 14474288 | 2023-10-03 |
| `wiki_Update_Chambers_of_Xeric_Potential_Tweaks_Changes.wikitext` | [Update:Chambers of Xeric: Potential Tweaks & Changes](https://oldschool.runescape.wiki/w/Update:Chambers_of_Xeric:_Potential_Tweaks_&_Changes?oldid=14476491) | 14476491 | 2023-10-10 |
| `wiki_Update_Chambers_of_Xeric_Changes.wikitext` | [Update:Chambers of Xeric Changes](https://oldschool.runescape.wiki/w/Update:Chambers_of_Xeric_Changes?oldid=14511996) | 14511996 | 2023-11-29 |
| `wiki_Update_Christmas_2023.wikitext` | [Update:Christmas 2023](https://oldschool.runescape.wiki/w/Update:Christmas_2023?oldid=14515308) | 14515308 | 2023-12-12 |
| `wiki_Update_Bank_QoL_Mini_Menu_Improvements.wikitext` | [Update:Bank QoL & Mini Menu Improvements](https://oldschool.runescape.wiki/w/Update:Bank_QoL_&_Mini_Menu_Improvements?oldid=14841084) | 14841084 | 2025-01-22 |
| `wiki_Update_Dev_Blog_Raids_Rewards.wikitext` | [Update:Dev Blog: Raids Rewards](https://oldschool.runescape.wiki/w/Update:Dev_Blog:_Raids_Rewards?oldid=13981560) | 13981560 | 2020-12-22 |
| `wiki_Update_Dev_Blog_Raids_Rewards_Update.wikitext` | [Update:Dev Blog: Raids Rewards Update](https://oldschool.runescape.wiki/w/Update:Dev_Blog:_Raids_Rewards_Update?oldid=13981561) | 13981561 | 2020-12-22 |
| `wiki_Update_December_The_Month_Ahead_2016.wikitext` | [Update:December - The Month Ahead (2016)](https://oldschool.runescape.wiki/w/Update:December_-_The_Month_Ahead_(2016)?oldid=14285991) | 14285991 | 2022-05-15 |
| `wiki_Update_Developer_Blog_PvM_QoL_Week.wikitext` | [Update:Developer Blog: PvM QoL Week](https://oldschool.runescape.wiki/w/Update:Developer_Blog:_PvM_QoL_Week?oldid=11840018) | 11840018 | 2020-03-16 |
| `wiki_Update_Easter_2017_Holiday_Event.wikitext` | [Update:Easter 2017 Holiday Event](https://oldschool.runescape.wiki/w/Update:Easter_2017_Holiday_Event?oldid=11840204) | 11840204 | 2020-03-16 |
| `wiki_Update_Dev_Blog_Content_Poll_54.wikitext` | [Update:Dev Blog: Content Poll 54](https://oldschool.runescape.wiki/w/Update:Dev_Blog:_Content_Poll_54?oldid=11842666) | 11842666 | 2020-03-16 |
| `wiki_Update_Eating_in_the_Bank.wikitext` | [Update:Eating in the Bank](https://oldschool.runescape.wiki/w/Update:Eating_in_the_Bank?oldid=11292930) | 11292930 | 2020-01-09 |
| `wiki_Update_Darkmeyer.wikitext` | [Update:Darkmeyer](https://oldschool.runescape.wiki/w/Update:Darkmeyer?oldid=14204054) | 14204054 | 2021-11-14 |
| `wiki_Update_Ferox_Enclave.wikitext` | [Update:Ferox Enclave](https://oldschool.runescape.wiki/w/Update:Ferox_Enclave?oldid=12875902) | 12875902 | 2020-07-16 |
| `wiki_Update_Combat_Achievements_Poll_Blog.wikitext` | [Update:Combat Achievements Poll Blog](https://oldschool.runescape.wiki/w/Update:Combat_Achievements_Poll_Blog?oldid=13960821) | 13960821 | 2020-11-27 |
| `wiki_Update_Equipment_Rebalancing_Updated.wikitext` | [Update:Equipment Rebalancing Updated](https://oldschool.runescape.wiki/w/Update:Equipment_Rebalancing_Updated?oldid=14253453) | 14253453 | 2022-03-14 |
| `wiki_Update_Equipment_Rebalance_Ranged_Meta.wikitext` | [Update:Equipment Rebalance: Ranged Meta](https://oldschool.runescape.wiki/w/Update:Equipment_Rebalance:_Ranged_Meta?oldid=14056723) | 14056723 | 2021-04-20 |
| `wiki_Update_Deadman_Reborn_and_QoL_Changes.wikitext` | [Update:Deadman Reborn and QoL Changes](https://oldschool.runescape.wiki/w/Update:Deadman_Reborn_and_QoL_Changes?oldid=14180221) | 14180221 | 2021-09-11 |
| `wiki_Update_Easter_2022.wikitext` | [Update:Easter 2022](https://oldschool.runescape.wiki/w/Update:Easter_2022?oldid=14295218) | 14295218 | 2022-06-15 |
| `wiki_Update_Desert_Treasure_II_Rewards_Beta.wikitext` | [Update:Desert Treasure II Rewards Beta](https://oldschool.runescape.wiki/w/Update:Desert_Treasure_II_Rewards_Beta?oldid=14399007) | 14399007 | 2023-04-15 |
| `wiki_Update_Desert_Treasure_II_The_Fallen_Empire.wikitext` | [Update:Desert Treasure II - The Fallen Empire](https://oldschool.runescape.wiki/w/Update:Desert_Treasure_II_-_The_Fallen_Empire?oldid=14450256) | 14450256 | 2023-08-07 |
| `wiki_Update_Defender_of_Varrock_Varlamore_Rewards_More.wikitext` | [Update:Defender of Varrock, Varlamore Rewards & More](https://oldschool.runescape.wiki/w/Update:Defender_of_Varrock,_Varlamore_Rewards_&_More?oldid=14543026) | 14543026 | 2024-01-31 |
| `wiki_Update_Deadman_Armageddon_First_Look.wikitext` | [Update:Deadman: Armageddon - First Look](https://oldschool.runescape.wiki/w/Update:Deadman:_Armageddon_-_First_Look?oldid=14700987) | 14700987 | 2024-07-15 |
| `wiki_Update_Colosseum_NPC_Clickboxes_More.wikitext` | [Update:Colosseum NPC Clickboxes & More](https://oldschool.runescape.wiki/w/Update:Colosseum_NPC_Clickboxes_&_More?oldid=14692012) | 14692012 | 2024-07-02 |
| `wiki_Update_Deadman_Annihilation_Everything_You_Need_To_Know.wikitext` | [Update:Deadman: Annihilation - Everything You Need To Know](https://oldschool.runescape.wiki/w/Update:Deadman:_Annihilation_-_Everything_You_Need_To_Know?oldid=15119418) | 15119418 | 2026-02-03 |
| `wiki_Update_Load_Test_Tournament_World_QOL.wikitext` | [Update:Load Test, Tournament World & QOL](https://oldschool.runescape.wiki/w/Update:Load_Test,_Tournament_World_&_QOL?oldid=11842747) | 11842747 | 2020-03-16 |
| `wiki_Update_Lizardman_Shaman_Improvements.wikitext` | [Update:Lizardman Shaman Improvements](https://oldschool.runescape.wiki/w/Update:Lizardman_Shaman_Improvements?oldid=14858643) | 14858643 | 2025-03-06 |
| `wiki_Update_Grotesque_Guardians.wikitext` | [Update:Grotesque Guardians](https://oldschool.runescape.wiki/w/Update:Grotesque_Guardians?oldid=11840088) | 11840088 | 2020-03-16 |
| `wiki_Update_Fixes_to_Slepe_and_A_Taste_of_Hope.wikitext` | [Update:Fixes to Slepe and A Taste of Hope](https://oldschool.runescape.wiki/w/Update:Fixes_to_Slepe_and_A_Taste_of_Hope?oldid=14858695) | 14858695 | 2025-03-06 |
| `wiki_Update_Game_Improvements_and_Nightmare_Beta_Changes.wikitext` | [Update:Game Improvements and Nightmare Beta Changes](https://oldschool.runescape.wiki/w/Update:Game_Improvements_and_Nightmare_Beta_Changes?oldid=10999849) | 10999849 | 2019-12-05 |
| `wiki_Update_Gielinor_Gazette_September_2020.wikitext` | [Update:Gielinor Gazette: September 2020](https://oldschool.runescape.wiki/w/Update:Gielinor_Gazette:_September_2020?oldid=13468367) | 13468367 | 2020-09-27 |
| `wiki_Update_Group_Ironman_Blog.wikitext` | [Update:Group Ironman Blog](https://oldschool.runescape.wiki/w/Update:Group_Ironman_Blog?oldid=14181506) | 14181506 | 2021-09-14 |
| `wiki_Update_Group_Ironman.wikitext` | [Update:Group Ironman](https://oldschool.runescape.wiki/w/Update:Group_Ironman?oldid=14917791) | 14917791 | 2025-06-10 |
| `wiki_Update_Grand_Exchange_Tax_Item_Sink.wikitext` | [Update:Grand Exchange Tax & Item Sink](https://oldschool.runescape.wiki/w/Update:Grand_Exchange_Tax_&_Item_Sink?oldid=14302352) | 14302352 | 2022-07-12 |
| `wiki_Update_Gielinor_Gazette_June_2022.wikitext` | [Update:Gielinor Gazette - June 2022](https://oldschool.runescape.wiki/w/Update:Gielinor_Gazette_-_June_2022?oldid=15112404) | 15112404 | 2026-01-25 |
| `wiki_Update_Game_Jam_IV_Overview_October_2023.wikitext` | [Update:Game Jam IV Overview - October 2023](https://oldschool.runescape.wiki/w/Update:Game_Jam_IV_Overview_-_October_2023?oldid=14645089) | 14645089 | 2024-04-26 |
| `wiki_Update_Leagues_IV_Trailblazer_Reloaded.wikitext` | [Update:Leagues IV - Trailblazer Reloaded](https://oldschool.runescape.wiki/w/Update:Leagues_IV_-_Trailblazer_Reloaded?oldid=14508371) | 14508371 | 2023-11-20 |
| `wiki_Update_Leagues_IV_Post_Launch.wikitext` | [Update:Leagues IV - Post-Launch](https://oldschool.runescape.wiki/w/Update:Leagues_IV_-_Post-Launch?oldid=14509962) | 14509962 | 2023-11-23 |
| `wiki_Update_Leagues_V_Teasers_FAQs_Releasing_November_27th.wikitext` | [Update:Leagues V Teasers & FAQs - Releasing November 27th](https://oldschool.runescape.wiki/w/Update:Leagues_V_Teasers_&_FAQs_-_Releasing_November_27th?oldid=14808166) | 14808166 | 2024-11-25 |
| `wiki_Update_Leagues_V_Raging_Echoes_OUT_NOW.wikitext` | [Update:Leagues V: Raging Echoes OUT NOW!](https://oldschool.runescape.wiki/w/Update:Leagues_V:_Raging_Echoes_OUT_NOW!?oldid=14911776) | 14911776 | 2025-05-30 |
| `wiki_Update_Interface_Uplift_Round_2.wikitext` | [Update:Interface Uplift Round 2](https://oldschool.runescape.wiki/w/Update:Interface_Uplift_Round_2?oldid=15002330) | 15002330 | 2025-10-11 |
| `wiki_Update_Get_Ready_For_Leagues_VI_Demonic_Pacts_April_15th.wikitext` | [Update:Get Ready For Leagues VI: Demonic Pacts - April 15th](https://oldschool.runescape.wiki/w/Update:Get_Ready_For_Leagues_VI:_Demonic_Pacts_-_April_15th?oldid=15175892) | 15175892 | 2026-04-14 |
| `wiki_Update_Leagues_VI_Demonic_Pacts_Launches_Today.wikitext` | [Update:Leagues VI: Demonic Pacts - Launches Today!](https://oldschool.runescape.wiki/w/Update:Leagues_VI:_Demonic_Pacts_-_Launches_Today!?oldid=15181922) | 15181922 | 2026-04-21 |
| `wiki_Update_Leagues_VI_Demonic_Pacts_Fixes_Issues.wikitext` | [Update:Leagues VI: Demonic Pacts - Fixes & Issues](https://oldschool.runescape.wiki/w/Update:Leagues_VI:_Demonic_Pacts_-_Fixes_&_Issues?oldid=15207358) | 15207358 | 2026-05-06 |
| `wiki_Update_Leagues_VI_Changes.wikitext` | [Update:Leagues VI Changes](https://oldschool.runescape.wiki/w/Update:Leagues_VI_Changes?oldid=15195615) | 15195615 | 2026-04-24 |
| `wiki_Update_Party_Pete_s_Birthday_Bash.wikitext` | [Update:Party Pete's Birthday Bash](https://oldschool.runescape.wiki/w/Update:Party_Pete's_Birthday_Bash?oldid=11842784) | 11842784 | 2020-03-16 |
| `wiki_Update_OSRS_Reveals_The_Kebos_Lowlands.wikitext` | [Update:OSRS Reveals: The Kebos Lowlands](https://oldschool.runescape.wiki/w/Update:OSRS_Reveals:_The_Kebos_Lowlands?oldid=14226009) | 14226009 | 2022-01-08 |
| `wiki_Update_Old_School_RuneScape_Twisted_League.wikitext` | [Update:Old School RuneScape Twisted League](https://oldschool.runescape.wiki/w/Update:Old_School_RuneScape_Twisted_League?oldid=11840059) | 11840059 | 2020-03-16 |
| `wiki_Update_Poll_70_and_Bounty_Hunter_Changes.wikitext` | [Update:Poll 70 and Bounty Hunter Changes](https://oldschool.runescape.wiki/w/Update:Poll_70_and_Bounty_Hunter_Changes?oldid=12237846) | 12237846 | 2020-04-30 |
| `wiki_Update_Poll_71_Game_Improvements_Blog.wikitext` | [Update:Poll 71 Game Improvements Blog](https://oldschool.runescape.wiki/w/Update:Poll_71_Game_Improvements_Blog?oldid=12277421) | 12277421 | 2020-05-05 |
| `wiki_Update_Poll_71_and_Darkmeyer_Updates.wikitext` | [Update:Poll 71 and Darkmeyer Updates](https://oldschool.runescape.wiki/w/Update:Poll_71_and_Darkmeyer_Updates?oldid=12653102) | 12653102 | 2020-06-18 |
| `wiki_Update_Poll_73_Game_Improvements_Blog.wikitext` | [Update:Poll 73 Game Improvements Blog](https://oldschool.runescape.wiki/w/Update:Poll_73_Game_Improvements_Blog?oldid=13437495) | 13437495 | 2020-09-23 |
| `wiki_Update_Poll_74_Game_Improvements_Blog.wikitext` | [Update:Poll 74 Game Improvements Blog](https://oldschool.runescape.wiki/w/Update:Poll_74_Game_Improvements_Blog?oldid=13999132) | 13999132 | 2021-01-18 |
| `wiki_Update_Poll_75_Game_Improvements_Blog.wikitext` | [Update:Poll 75 Game Improvements Blog](https://oldschool.runescape.wiki/w/Update:Poll_75_Game_Improvements_Blog?oldid=14363427) | 14363427 | 2023-01-17 |
| `wiki_Update_New_Client_Features_Milestone_1.wikitext` | [Update:New Client Features: Milestone 1](https://oldschool.runescape.wiki/w/Update:New_Client_Features:_Milestone_1?oldid=14101212) | 14101212 | 2021-06-23 |
| `wiki_Update_Points_Based_Combat_Achievements.wikitext` | [Update:Points-Based Combat Achievements](https://oldschool.runescape.wiki/w/Update:Points-Based_Combat_Achievements?oldid=14405278) | 14405278 | 2023-05-03 |
| `wiki_Update_Poll_80_Wilderness_Tombs_of_Amascut_More.wikitext` | [Update:Poll 80: Wilderness, Tombs of Amascut & More!](https://oldschool.runescape.wiki/w/Update:Poll_80:_Wilderness,_Tombs_of_Amascut_&_More!?oldid=14436990) | 14436990 | 2023-07-18 |
| `wiki_Update_More_Leagues_IV_Changes.wikitext` | [Update:More Leagues IV Changes](https://oldschool.runescape.wiki/w/Update:More_Leagues_IV_Changes?oldid=14513899) | 14513899 | 2023-12-06 |
| `wiki_Update_Poll_81_Mage_Training_Arena.wikitext` | [Update:Poll 81: Mage Training Arena](https://oldschool.runescape.wiki/w/Update:Poll_81:_Mage_Training_Arena?oldid=14555068) | 14555068 | 2024-03-15 |
| `wiki_Update_OSRS_Mobile_6th_Anniversary_Celebration.wikitext` | [Update:OSRS Mobile - 6th Anniversary Celebration](https://oldschool.runescape.wiki/w/Update:OSRS_Mobile_-_6th_Anniversary_Celebration?oldid=14911802) | 14911802 | 2025-05-30 |
| `wiki_Update_More_on_Leagues_V_Raging_Echoes.wikitext` | [Update:More on Leagues V: Raging Echoes](https://oldschool.runescape.wiki/w/Update:More_on_Leagues_V:_Raging_Echoes?oldid=14911796) | 14911796 | 2025-05-30 |
| `wiki_Update_New_Delve_Boss_Rewards_Varlamore_The_Final_Dawn.wikitext` | [Update:New Delve Boss Rewards - Varlamore: The Final Dawn](https://oldschool.runescape.wiki/w/Update:New_Delve_Boss_Rewards_-_Varlamore:_The_Final_Dawn?oldid=14873197) | 14873197 | 2025-03-28 |
| `wiki_Update_Permanent_Deadman_World_345_Improvements.wikitext` | [Update:Permanent Deadman: World 345 Improvements](https://oldschool.runescape.wiki/w/Update:Permanent_Deadman:_World_345_Improvements?oldid=14861545) | 14861545 | 2025-03-12 |
| `wiki_Update_More_Poll_84_Yama_Prep.wikitext` | [Update:More Poll 84 & Yama Prep](https://oldschool.runescape.wiki/w/Update:More_Poll_84_&_Yama_Prep?oldid=14896480) | 14896480 | 2025-05-07 |
| `wiki_Update_Poll_84_AFK_Timers_More.wikitext` | [Update:Poll 84, AFK Timers & More](https://oldschool.runescape.wiki/w/Update:Poll_84,_AFK_Timers_&_More?oldid=14915246) | 14915246 | 2025-06-04 |
| `wiki_Update_Prayers_Scrolls_QoL_a_Quest.wikitext` | [Update:Prayers Scrolls, QoL & a Quest](https://oldschool.runescape.wiki/w/Update:Prayers_Scrolls,_QoL_&_a_Quest?oldid=11842791) | 11842791 | 2020-03-16 |
| `wiki_Update_QoL_Week_1_PvM_Poll_Blog.wikitext` | [Update:QoL Week 1: PvM Poll Blog](https://oldschool.runescape.wiki/w/Update:QoL_Week_1:_PvM_Poll_Blog?oldid=11839788) | 11839788 | 2020-03-16 |
| `wiki_Update_PvM_QoL_updates_Wilderness_Rejuvenation_improvements.wikitext` | [Update:PvM QoL updates & Wilderness Rejuvenation improvements](https://oldschool.runescape.wiki/w/Update:PvM_QoL_updates_&_Wilderness_Rejuvenation_improvements?oldid=14858697) | 14858697 | 2025-03-06 |
| `wiki_Update_Poll_Blog_The_Kebos_Lowlands.wikitext` | [Update:Poll Blog: The Kebos Lowlands](https://oldschool.runescape.wiki/w/Update:Poll_Blog:_The_Kebos_Lowlands?oldid=11839763) | 11839763 | 2020-03-16 |
| `wiki_Update_QoL_and_CoX_Changes.wikitext` | [Update:QoL and CoX Changes](https://oldschool.runescape.wiki/w/Update:QoL_and_CoX_Changes?oldid=11842804) | 11842804 | 2020-03-16 |
| `wiki_Update_QoL_and_W45_Changes.wikitext` | [Update:QoL and W45 Changes](https://oldschool.runescape.wiki/w/Update:QoL_and_W45_Changes?oldid=11840307) | 11840307 | 2020-03-16 |
| `wiki_Update_Q_A_Summary_06_01_22.wikitext` | [Update:Q&A Summary 06/01/22](https://oldschool.runescape.wiki/w/Update:Q&A_Summary_06/01/22?oldid=14443718) | 14443718 | 2023-07-29 |
| `wiki_Update_Q_A_Summary_03_03_2022.wikitext` | [Update:Q&A Summary 03/03/2022](https://oldschool.runescape.wiki/w/Update:Q&A_Summary_03/03/2022?oldid=14253614) | 14253614 | 2022-03-15 |
| `wiki_Update_Q_A_Summary_10_03_2022.wikitext` | [Update:Q&A Summary 10/03/2022](https://oldschool.runescape.wiki/w/Update:Q&A_Summary_10/03/2022?oldid=14253617) | 14253617 | 2022-03-15 |
| `wiki_Update_Q_A_Summary_05_05_2022.wikitext` | [Update:Q&A Summary 05/05/2022](https://oldschool.runescape.wiki/w/Update:Q&A_Summary_05/05/2022?oldid=14284368) | 14284368 | 2022-05-11 |
| `wiki_Update_Pride_2022_Pet_Reclaim_Changes_New_Mobile_Client.wikitext` | [Update:Pride 2022, Pet Reclaim Changes & New Mobile Client!](https://oldschool.runescape.wiki/w/Update:Pride_2022,_Pet_Reclaim_Changes_&_New_Mobile_Client!?oldid=14296611) | 14296611 | 2022-06-22 |
| `wiki_Update_Q_A_Summary_07_07_2022.wikitext` | [Update:Q&A Summary 07/07/2022](https://oldschool.runescape.wiki/w/Update:Q&A_Summary_07/07/2022?oldid=14302634) | 14302634 | 2022-07-13 |
| `wiki_Update_Q_A_Summary_06_10_2022.wikitext` | [Update:Q&A Summary 06/10/2022](https://oldschool.runescape.wiki/w/Update:Q&A_Summary_06/10/2022?oldid=14334465) | 14334465 | 2022-10-13 |
| `wiki_Update_Q_A_Summary_16_02_23.wikitext` | [Update:Q&A Summary 16/02/23](https://oldschool.runescape.wiki/w/Update:Q&A_Summary_16/02/23?oldid=14380772) | 14380772 | 2023-02-23 |
| `wiki_Update_Project_Rebalance_NPC_Defence_Changes.wikitext` | [Update:Project Rebalance - NPC Defence Changes](https://oldschool.runescape.wiki/w/Update:Project_Rebalance_-_NPC_Defence_Changes?oldid=14630576) | 14630576 | 2024-04-12 |
| `wiki_Update_Project_Rebalance_Item_Combat_Adjustments.wikitext` | [Update:Project Rebalance - Item & Combat Adjustments](https://oldschool.runescape.wiki/w/Update:Project_Rebalance_-_Item_&_Combat_Adjustments?oldid=14643588) | 14643588 | 2024-04-24 |
| `wiki_Update_Project_Rebalance_Combat_Changes.wikitext` | [Update:Project Rebalance: Combat Changes](https://oldschool.runescape.wiki/w/Update:Project_Rebalance:_Combat_Changes?oldid=14674117) | 14674117 | 2024-06-05 |
| `wiki_Update_Poll_84_Stackable_Clues_More.wikitext` | [Update:Poll 84: Stackable Clues & More](https://oldschool.runescape.wiki/w/Update:Poll_84:_Stackable_Clues_&_More?oldid=14879903) | 14879903 | 2025-04-09 |
| `wiki_Update_Poll_84_Batch_II.wikitext` | [Update:Poll 84: Batch II](https://oldschool.runescape.wiki/w/Update:Poll_84:_Batch_II?oldid=14892854) | 14892854 | 2025-04-30 |
| `wiki_Update_QoL_Tweaks_Fixes.wikitext` | [Update:QoL Tweaks & Fixes](https://oldschool.runescape.wiki/w/Update:QoL_Tweaks_&_Fixes?oldid=15101815) | 15101815 | 2026-01-08 |
| `wiki_Update_Raids_Improvements.wikitext` | [Update:Raids Improvements](https://oldschool.runescape.wiki/w/Update:Raids_Improvements?oldid=11842812) | 11842812 | 2020-03-16 |
| `wiki_Update_Raids_Tweaks_QoL.wikitext` | [Update:Raids Tweaks & QoL](https://oldschool.runescape.wiki/w/Update:Raids_Tweaks_&_QoL?oldid=11842813) | 11842813 | 2020-03-16 |
| `wiki_Update_Silver_Jewellery_Tournament_World_QoL.wikitext` | [Update:Silver Jewellery, Tournament World & QoL!](https://oldschool.runescape.wiki/w/Update:Silver_Jewellery,_Tournament_World_&_QoL!?oldid=11842831) | 11842831 | 2020-03-16 |
| `wiki_Update_Quality_of_Life_Improvements.wikitext` | [Update:Quality of Life Improvements](https://oldschool.runescape.wiki/w/Update:Quality_of_Life_Improvements?oldid=11840212) | 11840212 | 2020-03-16 |
| `wiki_Update_Resizeable_Chat_Toggle_and_iOS_Beta_End.wikitext` | [Update:Resizeable Chat Toggle and iOS Beta End](https://oldschool.runescape.wiki/w/Update:Resizeable_Chat_Toggle_and_iOS_Beta_End?oldid=14858702) | 14858702 | 2025-03-06 |
| `wiki_Update_Small_Changes_Tweaks.wikitext` | [Update:Small Changes & Tweaks](https://oldschool.runescape.wiki/w/Update:Small_Changes_&_Tweaks?oldid=14117617) | 14117617 | 2021-07-07 |
| `wiki_Update_Ruinous_Powers_Beta_v2.wikitext` | [Update:Ruinous Powers Beta v2](https://oldschool.runescape.wiki/w/Update:Ruinous_Powers_Beta_v2?oldid=14417456) | 14417456 | 2023-06-14 |
| `wiki_Update_Sailing_Winter_Summit_Update.wikitext` | [Update:Sailing - Winter Summit Update](https://oldschool.runescape.wiki/w/Update:Sailing_-_Winter_Summit_Update?oldid=14536557) | 14536557 | 2024-01-20 |
| `wiki_Update_Run_Energy_Changes_Open_Beta_2.wikitext` | [Update:Run Energy Changes - Open Beta 2](https://oldschool.runescape.wiki/w/Update:Run_Energy_Changes_-_Open_Beta_2?oldid=14786235) | 14786235 | 2024-10-24 |
| `wiki_Update_Run_Energy_Changes.wikitext` | [Update:Run Energy Changes](https://oldschool.runescape.wiki/w/Update:Run_Energy_Changes?oldid=14833541) | 14833541 | 2025-01-08 |
| `wiki_Update_Royal_Titans_Feedback_12th_Birthday_Celebrations.wikitext` | [Update:Royal Titans Feedback & 12th Birthday Celebrations](https://oldschool.runescape.wiki/w/Update:Royal_Titans_Feedback_&_12th_Birthday_Celebrations?oldid=14850806) | 14850806 | 2025-02-12 |
| `wiki_Update_Summer_Sweep_Up_Combat_Loot.wikitext` | [Update:Summer Sweep-Up: Combat & Loot](https://oldschool.runescape.wiki/w/Update:Summer_Sweep-Up:_Combat_&_Loot?oldid=14929959) | 14929959 | 2025-06-30 |
| `wiki_Update_Summer_Sweep_Up_Combat.wikitext` | [Update:Summer Sweep Up: Combat](https://oldschool.runescape.wiki/w/Update:Summer_Sweep_Up:_Combat?oldid=14938870) | 14938870 | 2025-07-16 |
| `wiki_Update_The_Fractured_Archive_Rewards_Primer.wikitext` | [Update:The Fractured Archive - Rewards Primer](https://oldschool.runescape.wiki/w/Update:The_Fractured_Archive_-_Rewards_Primer?oldid=15143177) | 15143177 | 2026-03-05 |
| `wiki_Update_Summer_Sweep_Up_2026.wikitext` | [Update:Summer Sweep-Up 2026](https://oldschool.runescape.wiki/w/Update:Summer_Sweep-Up_2026?oldid=15272295) | 15272295 | 2026-07-22 |
| `wiki_Update_The_Fractured_Archive_First_Rewards_Proposal.wikitext` | [Update:The Fractured Archive - First Rewards Proposal](https://oldschool.runescape.wiki/w/Update:The_Fractured_Archive_-_First_Rewards_Proposal?oldid=15255983) | 15255983 | 2026-07-07 |
| `wiki_Update_Summer_Sweep_Up_Gear_PvM_Changes.wikitext` | [Update:Summer Sweep-Up Gear & PvM Changes](https://oldschool.runescape.wiki/w/Update:Summer_Sweep-Up_Gear_&_PvM_Changes?oldid=15289827) | 15289827 | 2026-08-07 |
| `wiki_Update_Summer_Sweep_Up_Agility_Chambers_of_Xeric_Changes.wikitext` | [Update:Summer Sweep Up - Agility & Chambers of Xeric Changes](https://oldschool.runescape.wiki/w/Update:Summer_Sweep_Up_-_Agility_&_Chambers_of_Xeric_Changes?oldid=15303824) | 15303824 | 2026-08-17 |
| `wiki_Update_Summer_Sweep_Up_Miscellaneous.wikitext` | [Update:Summer Sweep Up Miscellaneous](https://oldschool.runescape.wiki/w/Update:Summer_Sweep_Up_Miscellaneous?oldid=15330534) | 15330534 | 2026-09-04 |
| `wiki_Update_Summer_Sweep_Up_Pet_Improvements_More.wikitext` | [Update:Summer Sweep-Up Pet Improvements & More](https://oldschool.runescape.wiki/w/Update:Summer_Sweep-Up_Pet_Improvements_&_More?oldid=15345210) | 15345210 | 2026-09-16 |
| `wiki_Update_Upcoming_Changes_and_Fake_XP_Drop_Improvements.wikitext` | [Update:Upcoming Changes and Fake XP Drop Improvements](https://oldschool.runescape.wiki/w/Update:Upcoming_Changes_and_Fake_XP_Drop_Improvements?oldid=11839900) | 11839900 | 2020-03-16 |
| `wiki_Update_The_Theatre_of_Blood.wikitext` | [Update:The Theatre of Blood](https://oldschool.runescape.wiki/w/Update:The_Theatre_of_Blood?oldid=11839993) | 11839993 | 2020-03-16 |
| `wiki_Update_The_Kebos_Lowlands.wikitext` | [Update:The Kebos Lowlands](https://oldschool.runescape.wiki/w/Update:The_Kebos_Lowlands?oldid=14858680) | 14858680 | 2025-03-06 |
| `wiki_Update_Updated_Quest_Panel_Sandstone_Grinder_and_QoL.wikitext` | [Update:Updated Quest Panel, Sandstone Grinder and QoL](https://oldschool.runescape.wiki/w/Update:Updated_Quest_Panel,_Sandstone_Grinder_and_QoL?oldid=11840841) | 11840841 | 2020-03-16 |
| `wiki_Update_Warding_Design_Blog.wikitext` | [Update:Warding Design Blog](https://oldschool.runescape.wiki/w/Update:Warding_Design_Blog?oldid=11839956) | 11839956 | 2020-03-16 |
| `wiki_Update_The_Twisted_League.wikitext` | [Update:The Twisted League](https://oldschool.runescape.wiki/w/Update:The_Twisted_League?oldid=14204051) | 14204051 | 2021-11-14 |
| `wiki_Update_Twisted_League_Stats_and_Changes.wikitext` | [Update:Twisted League Stats and Changes](https://oldschool.runescape.wiki/w/Update:Twisted_League_Stats_and_Changes?oldid=11842897) | 11842897 | 2020-03-16 |
| `wiki_Update_Twisted_League_Reward_Shop_and_Game_Improvements.wikitext` | [Update:Twisted League Reward Shop and Game Improvements](https://oldschool.runescape.wiki/w/Update:Twisted_League_Reward_Shop_and_Game_Improvements?oldid=14307914) | 14307914 | 2022-07-29 |
| `wiki_Update_Vanguard_Improvements.wikitext` | [Update:Vanguard Improvements](https://oldschool.runescape.wiki/w/Update:Vanguard_Improvements?oldid=12425473) | 12425473 | 2020-05-21 |
| `wiki_Update_The_Nightmare_and_Chambers_of_Xeric_QoL.wikitext` | [Update:The Nightmare and Chambers of Xeric QoL](https://oldschool.runescape.wiki/w/Update:The_Nightmare_and_Chambers_of_Xeric_QoL?oldid=13860266) | 13860266 | 2020-11-11 |
| `wiki_Update_Theatre_of_Blood_New_Modes.wikitext` | [Update:Theatre of Blood: New Modes](https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood:_New_Modes?oldid=14858659) | 14858659 | 2025-03-06 |
| `wiki_Update_Tombs_of_Amascut_Nex_Rewards_Beta.wikitext` | [Update:Tombs of Amascut & Nex Rewards Beta](https://oldschool.runescape.wiki/w/Update:Tombs_of_Amascut_&_Nex_Rewards_Beta?oldid=14253448) | 14253448 | 2022-03-14 |
| `wiki_Update_Tombs_of_Amascut_Raid_Rewards_V2.wikitext` | [Update:Tombs of Amascut: Raid Rewards V2](https://oldschool.runescape.wiki/w/Update:Tombs_of_Amascut:_Raid_Rewards_V2?oldid=14253482) | 14253482 | 2022-03-14 |
| `wiki_Update_Tombs_of_Amascut_Q_A_Summary_05_09_2022.wikitext` | [Update:Tombs of Amascut Q&A Summary 05/09/2022](https://oldschool.runescape.wiki/w/Update:Tombs_of_Amascut_Q&A_Summary_05/09/2022?oldid=14321864) | 14321864 | 2022-09-07 |
| `wiki_Update_Varlamore_Activities_Kourend_Changes.wikitext` | [Update:Varlamore Activities & Kourend Changes](https://oldschool.runescape.wiki/w/Update:Varlamore_Activities_&_Kourend_Changes?oldid=14497625) | 14497625 | 2023-11-13 |
| `wiki_Update_While_Guthix_Sleeps.wikitext` | [Update:While Guthix Sleeps](https://oldschool.runescape.wiki/w/Update:While_Guthix_Sleeps?oldid=14858254) | 14858254 | 2025-03-05 |
| `wiki_Update_Varlamore_Summer_Sweep_Up_Combat_Tweaks.wikitext` | [Update:Varlamore & Summer Sweep-Up Combat Tweaks](https://oldschool.runescape.wiki/w/Update:Varlamore_&_Summer_Sweep-Up_Combat_Tweaks?oldid=14978336) | 14978336 | 2025-09-02 |

## 2. OSRS Wiki pages, pinned revisions (`sources/wiki_*.wikitext`, `sources/manifest.tsv`)

79 titles resolved, raw wikitext at the revision shown (`?oldid=` renders exactly the file). `tools/fetch_cox_wiki.sh` lists
the page set; its curl run is not what produced these files (that script fetches unpinned `action=raw` and uses a different
contact address). Missing (recorded `MISSING` in the manifest, not retried): `Great Olm (head)`, `Crystal (Chambers of
Xeric)`. `Great Olm (Left claw)` and `Great Olm (Right claw)` were requested and resolved onto the `Great Olm` page (one file).
Also probed: the `Guide:` namespace (3002) holds **no Chambers of Xeric guide** (search for "Chambers of Xeric", "CoX",
"Olm", "Tekton", "Vanguards", "raids" returned only unrelated guides; `Guide:Advanced Theatre of Blood` is ToB). Wiki
update history is the `{{Update}}` list inside the raid page, pinned in `wiki_Chambers_of_Xeric.wikitext`.

| File | Page | Revision | Revision date |
|---|---|---:|---|
| `wiki_Chambers_of_Xeric.wikitext` | [Chambers of Xeric](https://oldschool.runescape.wiki/w/Chambers_of_Xeric?oldid=15356945) | 15356945 | 2026-09-27 |
| `wiki_Great_Olm.wikitext` | [Great Olm](https://oldschool.runescape.wiki/w/Great_Olm?oldid=15352695) | 15352695 | 2026-09-21 |
| `wiki_Tekton.wikitext` | [Tekton](https://oldschool.runescape.wiki/w/Tekton?oldid=15322293) | 15322293 | 2026-08-27 |
| `wiki_Vasa_Nistirio.wikitext` | [Vasa Nistirio](https://oldschool.runescape.wiki/w/Vasa_Nistirio?oldid=15322286) | 15322286 | 2026-08-27 |
| `wiki_Vespula.wikitext` | [Vespula](https://oldschool.runescape.wiki/w/Vespula?oldid=15322290) | 15322290 | 2026-08-27 |
| `wiki_Vanguard.wikitext` | [Vanguard](https://oldschool.runescape.wiki/w/Vanguard?oldid=15322275) | 15322275 | 2026-08-27 |
| `wiki_Lux_grub.wikitext` | [Lux grub](https://oldschool.runescape.wiki/w/Lux_grub?oldid=15315333) | 15315333 | 2026-08-21 |
| `wiki_Vespine_soldier.wikitext` | [Vespine soldier](https://oldschool.runescape.wiki/w/Vespine_soldier?oldid=15315334) | 15315334 | 2026-08-21 |
| `wiki_Abyssal_portal.wikitext` | [Abyssal portal](https://oldschool.runescape.wiki/w/Abyssal_portal?oldid=15315336) | 15315336 | 2026-08-21 |
| `wiki_Chambers_of_Xeric_Strategies.wikitext` | [Chambers of Xeric/Strategies](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Strategies?oldid=15357585) | 15357585 | 2026-09-28 |
| `wiki_Chambers_of_Xeric_Challenge_Mode.wikitext` | [Chambers of Xeric/Challenge Mode](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Challenge_Mode?oldid=15360234) | 15360234 | 2026-10-02 |
| `wiki_Chambers_of_Xeric_Potions.wikitext` | [Chambers of Xeric/Potions](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Potions?oldid=14121903) | 14121903 | 2021-07-15 |
| `wiki_Chambers_of_Xeric_Food.wikitext` | [Chambers of Xeric/Food](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Food?oldid=15359174) | 15359174 | 2026-09-30 |
| `wiki_Glowing_crystal.wikitext` | [Glowing crystal](https://oldschool.runescape.wiki/w/Glowing_crystal?oldid=15315326) | 15315326 | 2026-08-21 |
| `wiki_Chambers_of_Xeric_Challenge_Mode_Strategies.wikitext` | [Chambers of Xeric/Challenge Mode/Strategies](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Challenge_Mode/Strategies?oldid=15356548) | 15356548 | 2026-09-26 |
| `wiki_Perfect_Olm_Solo.wikitext` | [Perfect Olm (Solo)](https://oldschool.runescape.wiki/w/Perfect_Olm_(Solo)?oldid=15226565) | 15226565 | 2026-06-05 |
| `wiki_Perfect_Olm_Trio.wikitext` | [Perfect Olm (Trio)](https://oldschool.runescape.wiki/w/Perfect_Olm_(Trio)?oldid=15330896) | 15330896 | 2026-09-04 |
| `wiki_Olmlet.wikitext` | [Olmlet](https://oldschool.runescape.wiki/w/Olmlet?oldid=15352752) | 15352752 | 2026-09-21 |
| `wiki_Ice_demon.wikitext` | [Ice demon](https://oldschool.runescape.wiki/w/Ice_demon?oldid=15343102) | 15343102 | 2026-09-14 |
| `wiki_Muttadile.wikitext` | [Muttadile](https://oldschool.runescape.wiki/w/Muttadile?oldid=15322287) | 15322287 | 2026-08-27 |
| `wiki_Scavenger_beast.wikitext` | [Scavenger beast](https://oldschool.runescape.wiki/w/Scavenger_beast?oldid=15285671) | 15285671 | 2026-08-02 |
| `wiki_Corrupted_scavenger.wikitext` | [Corrupted scavenger](https://oldschool.runescape.wiki/w/Corrupted_scavenger?oldid=15353369) | 15353369 | 2026-09-22 |
| `wiki_Overload_Chambers_of_Xeric.wikitext` | [Overload (Chambers of Xeric)](https://oldschool.runescape.wiki/w/Overload_(Chambers_of_Xeric)?oldid=15188136) | 15188136 | 2026-04-22 |
| `wiki_Cavern_grubs.wikitext` | [Cavern grubs](https://oldschool.runescape.wiki/w/Cavern_grubs?oldid=15188140) | 15188140 | 2026-04-22 |
| `wiki_Medivaemia_blossom.wikitext` | [Medivaemia blossom](https://oldschool.runescape.wiki/w/Medivaemia_blossom?oldid=15188141) | 15188141 | 2026-04-22 |
| `wiki_Keystone_crystal.wikitext` | [Keystone crystal](https://oldschool.runescape.wiki/w/Keystone_crystal?oldid=15188162) | 15188162 | 2026-04-22 |
| `wiki_Jewelled_Crab.wikitext` | [Jewelled Crab](https://oldschool.runescape.wiki/w/Jewelled_Crab?oldid=15346042) | 15346042 | 2026-09-16 |
| `wiki_Guardian_Chambers_of_Xeric.wikitext` | [Guardian (Chambers of Xeric)](https://oldschool.runescape.wiki/w/Guardian_(Chambers_of_Xeric)?oldid=15315357) | 15315357 | 2026-08-21 |
| `wiki_Meat_tree.wikitext` | [Meat tree](https://oldschool.runescape.wiki/w/Meat_tree?oldid=15201212) | 15201212 | 2026-04-29 |
| `wiki_Skeletal_Mystic.wikitext` | [Skeletal Mystic](https://oldschool.runescape.wiki/w/Skeletal_Mystic?oldid=15315352) | 15315352 | 2026-08-21 |
| `wiki_Deathly_ranger.wikitext` | [Deathly ranger](https://oldschool.runescape.wiki/w/Deathly_ranger?oldid=15199986) | 15199986 | 2026-04-28 |
| `wiki_Deathly_mage.wikitext` | [Deathly mage](https://oldschool.runescape.wiki/w/Deathly_mage?oldid=15199987) | 15199987 | 2026-04-28 |
| `wiki_Stone_Guardian.wikitext` | [Stone Guardian](https://oldschool.runescape.wiki/w/Stone_Guardian?oldid=15215980) | 15215980 | 2026-05-24 |
| `wiki_Lizardman_shaman_Chambers_of_Xeric.wikitext` | [Lizardman shaman (Chambers of Xeric)](https://oldschool.runescape.wiki/w/Lizardman_shaman_(Chambers_of_Xeric)?oldid=15321659) | 15321659 | 2026-08-27 |
| `wiki_Ancient_chest.wikitext` | [Ancient chest](https://oldschool.runescape.wiki/w/Ancient_chest?oldid=15344244) | 15344244 | 2026-09-15 |
| `wiki_Tightrope_Chambers_of_Xeric.wikitext` | [Tightrope (Chambers of Xeric)](https://oldschool.runescape.wiki/w/Tightrope_(Chambers_of_Xeric)?oldid=15202805) | 15202805 | 2026-04-29 |
| `wiki_Talk_Chambers_of_Xeric.wikitext` | [Talk:Chambers of Xeric](https://oldschool.runescape.wiki/w/Talk:Chambers_of_Xeric?oldid=15226512) | 15226512 | 2026-06-05 |
| `wiki_Undying_Raid_Team.wikitext` | [Undying Raid Team](https://oldschool.runescape.wiki/w/Undying_Raid_Team?oldid=14771429) | 14771429 | 2024-10-13 |
| `wiki_A_Not_So_Special_Lizard.wikitext` | [A Not So Special Lizard](https://oldschool.runescape.wiki/w/A_Not_So_Special_Lizard?oldid=15229963) | 15229963 | 2026-06-09 |
| `wiki_Redemption_Enthusiast.wikitext` | [Redemption Enthusiast](https://oldschool.runescape.wiki/w/Redemption_Enthusiast?oldid=14983862) | 14983862 | 2025-09-10 |
| `wiki_Blind_Spot.wikitext` | [Blind Spot](https://oldschool.runescape.wiki/w/Blind_Spot?oldid=15015567) | 15015567 | 2025-11-05 |
| `wiki_Putting_It_Olm_on_the_Line.wikitext` | [Putting It Olm on the Line](https://oldschool.runescape.wiki/w/Putting_It_Olm_on_the_Line?oldid=14957791) | 14957791 | 2025-08-03 |
| `wiki_Anvil_No_More.wikitext` | [Anvil No More](https://oldschool.runescape.wiki/w/Anvil_No_More?oldid=14983866) | 14983866 | 2025-09-10 |
| `wiki_Kill_It_with_Fire.wikitext` | [Kill It with Fire](https://oldschool.runescape.wiki/w/Kill_It_with_Fire?oldid=14983858) | 14983858 | 2025-09-10 |
| `wiki_Playing_with_Lasers.wikitext` | [Playing with Lasers](https://oldschool.runescape.wiki/w/Playing_with_Lasers?oldid=14771518) | 14771518 | 2024-10-13 |
| `wiki_Perfectly_Balanced.wikitext` | [Perfectly Balanced](https://oldschool.runescape.wiki/w/Perfectly_Balanced?oldid=14983857) | 14983857 | 2025-09-10 |
| `wiki_Together_We_ll_Fall.wikitext` | [Together We'll Fall](https://oldschool.runescape.wiki/w/Together_We'll_Fall?oldid=14983856) | 14983856 | 2025-09-10 |
| `wiki_Undying_Raider.wikitext` | [Undying Raider](https://oldschool.runescape.wiki/w/Undying_Raider?oldid=15322703) | 15322703 | 2026-08-28 |
| `wiki_No_Time_for_Death.wikitext` | [No Time for Death](https://oldschool.runescape.wiki/w/No_Time_for_Death?oldid=14796854) | 14796854 | 2024-11-11 |
| `wiki_Dancing_with_Statues.wikitext` | [Dancing with Statues](https://oldschool.runescape.wiki/w/Dancing_with_Statues?oldid=15098327) | 15098327 | 2026-01-01 |
| `wiki_Cryo_No_More.wikitext` | [Cryo No More](https://oldschool.runescape.wiki/w/Cryo_No_More?oldid=14983861) | 14983861 | 2025-09-10 |
| `wiki_Mutta_diet.wikitext` | [Mutta-diet](https://oldschool.runescape.wiki/w/Mutta-diet?oldid=14983854) | 14983854 | 2025-09-10 |
| `wiki_Shayzien_Specialist.wikitext` | [Shayzien Specialist](https://oldschool.runescape.wiki/w/Shayzien_Specialist?oldid=15275518) | 15275518 | 2026-07-26 |
| `wiki_Stop_Drop_and_Roll.wikitext` | [Stop Drop and Roll](https://oldschool.runescape.wiki/w/Stop_Drop_and_Roll?oldid=15303318) | 15303318 | 2026-08-16 |
| `wiki_Blizzard_Dodger.wikitext` | [Blizzard Dodger](https://oldschool.runescape.wiki/w/Blizzard_Dodger?oldid=14983855) | 14983855 | 2025-09-10 |
| `wiki_Chambers_of_Xeric_Load_Test.wikitext` | [Chambers of Xeric Load Test](https://oldschool.runescape.wiki/w/Chambers_of_Xeric_Load_Test?oldid=15058597) | 15058597 | 2025-11-23 |
| `wiki_Chambers_of_Xeric_Veteran.wikitext` | [Chambers of Xeric Veteran](https://oldschool.runescape.wiki/w/Chambers_of_Xeric_Veteran?oldid=14771354) | 14771354 | 2024-10-13 |
| `wiki_Dust_Seeker.wikitext` | [Dust Seeker](https://oldschool.runescape.wiki/w/Dust_Seeker?oldid=14771362) | 14771362 | 2024-10-13 |
| `wiki_Chambers_of_Xeric_CM_Solo_Speed_Runner.wikitext` | [Chambers of Xeric: CM (Solo) Speed-Runner](https://oldschool.runescape.wiki/w/Chambers_of_Xeric:_CM_(Solo)_Speed-Runner?oldid=15360567) | 15360567 | 2026-10-02 |
| `wiki_Chambers_of_Xeric_Master.wikitext` | [Chambers of Xeric Master](https://oldschool.runescape.wiki/w/Chambers_of_Xeric_Master?oldid=15171344) | 15171344 | 2026-04-09 |
| `wiki_Chambers_of_Xeric_CM_Grandmaster.wikitext` | [Chambers of Xeric: CM Grandmaster](https://oldschool.runescape.wiki/w/Chambers_of_Xeric:_CM_Grandmaster?oldid=14784736) | 14784736 | 2024-10-22 |
| `wiki_Chambers_of_Xeric_CM_Solo_Speed_Chaser.wikitext` | [Chambers of Xeric: CM (Solo) Speed-Chaser](https://oldschool.runescape.wiki/w/Chambers_of_Xeric:_CM_(Solo)_Speed-Chaser?oldid=15293814) | 15293814 | 2026-08-12 |
| `wiki_Immortal_Raid_Team.wikitext` | [Immortal Raid Team](https://oldschool.runescape.wiki/w/Immortal_Raid_Team?oldid=14771438) | 14771438 | 2024-10-13 |
| `wiki_Chambers_of_Xeric_CM_Trio_Speed_Runner.wikitext` | [Chambers of Xeric: CM (Trio) Speed-Runner](https://oldschool.runescape.wiki/w/Chambers_of_Xeric:_CM_(Trio)_Speed-Runner?oldid=14784735) | 14784735 | 2024-10-22 |
| `wiki_Chambers_of_Xeric_CM_5_Scale_Speed_Runner.wikitext` | [Chambers of Xeric: CM (5-Scale) Speed-Runner](https://oldschool.runescape.wiki/w/Chambers_of_Xeric:_CM_(5-Scale)_Speed-Runner?oldid=15275665) | 15275665 | 2026-07-26 |
| `wiki_Chambers_of_Xeric_Trio_Speed_Chaser.wikitext` | [Chambers of Xeric (Trio) Speed-Chaser](https://oldschool.runescape.wiki/w/Chambers_of_Xeric_(Trio)_Speed-Chaser?oldid=14771461) | 14771461 | 2024-10-13 |
| `wiki_Chambers_of_Xeric_CM_Trio_Speed_Chaser.wikitext` | [Chambers of Xeric: CM (Trio) Speed-Chaser](https://oldschool.runescape.wiki/w/Chambers_of_Xeric:_CM_(Trio)_Speed-Chaser?oldid=14784734) | 14784734 | 2024-10-22 |
| `wiki_Chambers_of_Xeric_CM_5_Scale_Speed_Chaser.wikitext` | [Chambers of Xeric: CM (5-Scale) Speed-Chaser](https://oldschool.runescape.wiki/w/Chambers_of_Xeric:_CM_(5-Scale)_Speed-Chaser?oldid=15275660) | 15275660 | 2026-07-26 |
| `wiki_Chambers_of_Xeric_Solo_Speed_Runner.wikitext` | [Chambers of Xeric (Solo) Speed-Runner](https://oldschool.runescape.wiki/w/Chambers_of_Xeric_(Solo)_Speed-Runner?oldid=15149381) | 15149381 | 2026-03-15 |
| `wiki_Chambers_of_Xeric_Trio_Speed_Runner.wikitext` | [Chambers of Xeric (Trio) Speed-Runner](https://oldschool.runescape.wiki/w/Chambers_of_Xeric_(Trio)_Speed-Runner?oldid=14981253) | 14981253 | 2025-09-06 |
| `wiki_Immortal_Raider.wikitext` | [Immortal Raider](https://oldschool.runescape.wiki/w/Immortal_Raider?oldid=14771499) | 14771499 | 2024-10-13 |
| `wiki_Chambers_of_Xeric_CM_Master.wikitext` | [Chambers of Xeric: CM Master](https://oldschool.runescape.wiki/w/Chambers_of_Xeric:_CM_Master?oldid=14784737) | 14784737 | 2024-10-22 |
| `wiki_Chambers_of_Xeric_Grandmaster.wikitext` | [Chambers of Xeric Grandmaster](https://oldschool.runescape.wiki/w/Chambers_of_Xeric_Grandmaster?oldid=15171345) | 15171345 | 2026-04-09 |
| `wiki_Chambers_of_Xeric_5_Scale_Speed_Runner.wikitext` | [Chambers of Xeric (5-Scale) Speed-Runner](https://oldschool.runescape.wiki/w/Chambers_of_Xeric_(5-Scale)_Speed-Runner?oldid=15275664) | 15275664 | 2026-07-26 |
| `wiki_Chambers_of_Xeric_Solo_Speed_Chaser.wikitext` | [Chambers of Xeric (Solo) Speed-Chaser](https://oldschool.runescape.wiki/w/Chambers_of_Xeric_(Solo)_Speed-Chaser?oldid=15360561) | 15360561 | 2026-10-02 |
| `wiki_Chambers_of_Xeric_5_Scale_Speed_Chaser.wikitext` | [Chambers of Xeric (5-Scale) Speed-Chaser](https://oldschool.runescape.wiki/w/Chambers_of_Xeric_(5-Scale)_Speed-Chaser?oldid=15275659) | 15275659 | 2026-07-26 |
| `wiki_Combat_Achievements_Elite.wikitext` | [Combat Achievements/Elite](https://oldschool.runescape.wiki/w/Combat_Achievements/Elite?oldid=15321189) | 15321189 | 2026-08-26 |
| `wiki_Combat_Achievements_Master.wikitext` | [Combat Achievements/Master](https://oldschool.runescape.wiki/w/Combat_Achievements/Master?oldid=15329081) | 15329081 | 2026-09-02 |
| `wiki_Combat_Achievements_Grandmaster.wikitext` | [Combat Achievements/Grandmaster](https://oldschool.runescape.wiki/w/Combat_Achievements/Grandmaster?oldid=15321195) | 15321195 | 2026-08-26 |

`wiki_combat_achievements_cox.tsv` — derived here from the pinned CA pages: every page whose `monster =` is `Chambers of
Xeric` or `Chambers of Xeric: Challenge Mode` (40 tasks), reduced to `id / file / tier / monster / type / description`. The
set is the wiki's own answer to "which tasks belong to this raid" (search `insource:/monster = Chambers of Xeric/`, 40 hits).
The three tier index pages (`Combat Achievements/Elite`, `/Master`, `/Grandmaster`) are pinned too.

## 3. Blert (<https://github.com/blert-io/blert>)

- Shallow clone 2026-10-02, commit `7c7750cf01b23e5d223623d7c34c5ef10ca327fd` (committed 2026-10-01). Scratch clone:
  `build/corpus_tmp/blert/` (not committed). Kept under `sources/blert_repo/`: `web_raids_cox_page.tsx` ("Coming Soon!"),
  `common_challenge_stage_enum_excerpt.ts` (13 CoX stage names), README.
- Guides: `web/app/guides/` holds only `blert/` and `tob/` (bloat, how-to-hmt, nylocas, plugins). **No CoX guide.**
- Recorder probe, 2026-10-02: `GET https://blert.io/api/v1/challenges?type=2&limit=1` returned HTTP 200 with body `[]`.
  Control, same minute: `?type=1&limit=1` (ToB) returned a challenge row. Blert still does not record Chambers of Xeric.

## 4. RuneLite plugin code

See [`sources/PLUGIN_HUB_README.md`](sources/PLUGIN_HUB_README.md) for repo, commit and files. Hub manifests read from
`https://raw.githubusercontent.com/runelite/plugin-hub/master/plugins/<name>` (2026-10-02); the hub tree listing was read via
the GitHub API (2834 plugins). OpenOSRS commit `99587b920b7029fa9b6cf0652f28f729371fcf5a`.

**COX_PLAN.md section 10's path `sources/runelite/CoxPlugin.java` was not real** (no such directory existed). The file it
describes is OpenOSRS `coxhelper/CoxPlugin.java`, now `sources/openosrs_coxhelper/CoxPlugin.java`. The plan was not edited.

## 5. weirdgloop osrs-dps-calc `monsters.json`

`https://raw.githubusercontent.com/weirdgloop/osrs-dps-calc/89c3e25b344aea90d0189746e4b5f73dde0f0383/cdn/json/monsters.json`
(main at 2026-09-02T18:18:56Z; the whole file is 2860 rows). `sources/dps_calc_monsters_cox.json` keeps the 25 rows whose name is a
CoX npc: Abyssal portal (speed 2, size 4, hp 250), Deathly mage / Deathly ranger (4, 1, 120), Glowing crystal (4, 4, 120),
Great Olm head (4, 5, 800) and Left/Right claw (speed -1, size 5, hp 600), Ice demon (3, 2, 140), Lizardman shaman Standard
(4, 3, 150) and Lizardman Temple (4, 2, 150), Muttadile Large (4, 5, 250) / Small (4, 3, 250), Scavenger beast (4, 2, 30),
Skeletal Mystic (4, 2, 160), Stone Guardian Magic/Melee/Ranged (5, 1, 62), Tekton Normal and Enraged (3, 4, 300), Vanguard
Magic/Melee/Ranged (4, 3, 180), Vasa Nistirio (3, 5, 300), Vespine soldier (4, 3, 100), Vespula (3, 5, 200). These are base,
unscaled values; the file has no row for the Jewelled Crab, the CoX Guardian, Lux grub or the Meat tree, and I did not map
"Stone Guardian" or the two Lizardman shaman rows to the raid's own npcs: that mapping is for the spec worker (the speeds in
this table are what `tools/fetch_cox_wiki.sh` says the wiki infoboxes agree with).

## 6. We Do Raids

<https://wedoraids.com>: sitemap read 2026-10-02 (14 urls, same as the ToB pass). Six pages that mention CoX copied byte-for-byte
from `docs/minigames/theater_of_blood/sources/wdr/` (fetched there the same day) into `sources/wdr/`. The public site holds no
CoX mechanics; the guides live in the Discord (`#room-resources`, `#olm-resources`), which is not reachable: UNAVAILABLE.

## 7. YouTube guide transcripts

Search: `yt-dlp --flat-playlist "ytsearch8:<room> guide ..."`, 16 queries, 2026-10-02 (3 s apart; the raw result list is
`build/corpus_tmp/cox_yt/search.txt`, not committed). 53 candidates chosen (at least two players per room where they exist);
45 have captions and are in `sources/transcripts/yt_<id>.md`; table with channel, title, length, date and overlay/chapter
notes is in `sources/README.md`. Without captions (listed as unavailable): `iQd1ku7CP80`, `BbryLzdknkA`, `h4d4ge6tIa0`,
`IVvIwrqAsH8`, `_CUPrtw-nyU`, `SuqdDYqHjAo`, `XjhYjGLXPms`, `ox1leZ5Z44E`.

## 8. Reference servers (read where already in the tree; nothing new fetched)

- **Near Reality** (`~/Documents/git_repos/RSPS-NEAR-REALITY`, Zenyte-derived): `sources/nr/room_coords.txt`; its CoX scripts are
  catalogued in `COX_NEARREALITY_PORT_PLAN.md` (e.g. `TektonCombatScript`, a hard-coded 3-tick period); not authoritative.
- **Zenyte**: quoted in the research log `COX_MECHANICS.md` (e.g. the Tekton combat-stance block ids 30017/30021/30022/30023);
  no Zenyte file is held in `sources/`.
- **Rank**: below the cache, below plugins that had to match the real server; use only to cross-check.

## 9. Not obtainable

- We Do Raids Discord role guides, `#room-resources`, `#olm-resources`, mentor material: Discord not reachable.
- Blert: no CoX recorder or guide exists (see 3). No Blert CoX dataset can exist to download.
- Hub plugins named `cox-helper`, `olm-related`: not on the hub (see `PLUGIN_HUB_README.md`).
- 8 YouTube videos without captions (see 7). No frame was extracted from any video in this pass (corpus-only launch).
- runescape.com original newsposts: not tried (HTTP 403 to non-browser clients per COMMUNITY_SOURCES.md).
- The wiki pages `Great Olm (head)` and `Crystal (Chambers of Xeric)` do not exist.
