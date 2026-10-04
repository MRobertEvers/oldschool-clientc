# LEDGER_wiki: the wiki part of the Colosseum source corpus

Owner of this file: the wiki worker. Fetched 2026-10-03 by one worker, only host oldschool.runescape.wiki, through tools/toa_fetch_wiki.py (action=query for the revision, action=parse&oldid=REV for the wikitext, so each file is byte-identical to the ?oldid= permalink) wrapped in a throttle of one request per 1.2 s or slower (scratchpad slowfetch.py; the stock tool sleeps 0.8 s). The pin guard never overwrote a file; no .rev<revid> file was written. User-Agent of the tool: 3draster-toa-research/1.0. Search calls (api.php list=search, 1.2 s apart): namespace 112 with Fortis Colosseum, Colosseum, Sol Heredit, Varlamore, Dizana's quiver, Colosseum beta, Colosseum changes, Colosseum hotfix (build/logs/news_search.txt); namespace 0 and 828 title searches (build/logs/wiki_search.txt, build/logs/mod_search.txt). Fetch logs: build/logs/news_fetch1.txt to news_fetch3.txt, wiki_fetch1.txt, wiki_fetch2.txt. A redirect title resolves to its target and is pinned under the target (Sol Heredit/Strategies to Fortis Colosseum/Strategies#Sol Heredit; Colosseum Speed-Trialist to Colosseum Speed-Chaser, same page; I Was Here First to "I was here first!"; Sunfire bracers to Sunlit bracers). The wave table and the drop tables are NOT in a Module or Bucket: they are inline wikitext on Fortis Colosseum/Strategies (lines 873-905) and Rewards Chest (Fortis Colosseum) (DropsLineReward rows); the only Module pinned is Module:Tile markers/Colosseum.json (unnamed tile markers, region 7216 = map square m28_48). Nothing from the 2004 source was read or cited.

## Fetch rows

| fetch date | url | revision | file | what it is for |
|---|---|---|---|---|
| 2026-10-03 (rev stamp 2026-04-16) | https://oldschool.runescape.wiki/?oldid=15178360 (Glory) | rev 15178360 | docs/minigames/colosseum/sources/wiki/wiki_Glory.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-29) | https://oldschool.runescape.wiki/?oldid=15358549 (Fortis Colosseum) | rev 15358549 | docs/minigames/colosseum/sources/wiki/wiki_Fortis_Colosseum.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-08-16) | https://oldschool.runescape.wiki/?oldid=15303372 (Minimus) | rev 15303372 | docs/minigames/colosseum/sources/wiki/wiki_Minimus.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-08-25) | https://oldschool.runescape.wiki/?oldid=15319602 (Jaguar warrior) | rev 15319602 | docs/minigames/colosseum/sources/wiki/wiki_Jaguar_warrior.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-04-28) | https://oldschool.runescape.wiki/?oldid=15200480 (Serpent shaman) | rev 15200480 | docs/minigames/colosseum/sources/wiki/wiki_Serpent_shaman.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-27) | https://oldschool.runescape.wiki/?oldid=15357147 (Minotaur (Fortis Colosseum)) | rev 15357147 | docs/minigames/colosseum/sources/wiki/wiki_Minotaur_Fortis_Colosseum.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-07-14) | https://oldschool.runescape.wiki/?oldid=15263423 (Fremennik warband archer) | rev 15263423 | docs/minigames/colosseum/sources/wiki/wiki_Fremennik_warband_archer.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-04-28) | https://oldschool.runescape.wiki/?oldid=15200484 (Fremennik warband seer) | rev 15200484 | docs/minigames/colosseum/sources/wiki/wiki_Fremennik_warband_seer.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-08-14) | https://oldschool.runescape.wiki/?oldid=15299774 (Fremennik warband berserker) | rev 15299774 | docs/minigames/colosseum/sources/wiki/wiki_Fremennik_warband_berserker.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-04-28) | https://oldschool.runescape.wiki/?oldid=15200486 (Javelin Colossus) | rev 15200486 | docs/minigames/colosseum/sources/wiki/wiki_Javelin_Colossus.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-04-28) | https://oldschool.runescape.wiki/?oldid=15200487 (Manticore) | rev 15200487 | docs/minigames/colosseum/sources/wiki/wiki_Manticore.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-04-28) | https://oldschool.runescape.wiki/?oldid=15200488 (Shockwave Colossus) | rev 15200488 | docs/minigames/colosseum/sources/wiki/wiki_Shockwave_Colossus.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-30) | https://oldschool.runescape.wiki/?oldid=15358662 (Sol Heredit) | rev 15358662 | docs/minigames/colosseum/sources/wiki/wiki_Sol_Heredit.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-07-10) | https://oldschool.runescape.wiki/?oldid=15259696 (Gladiator (Fortis Colosseum)) | rev 15259696 | docs/minigames/colosseum/sources/wiki/wiki_Gladiator_Fortis_Colosseum.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-30) | https://oldschool.runescape.wiki/?oldid=15358918 (Fortis Colosseum/Strategies) | rev 15358918 | docs/minigames/colosseum/sources/wiki/wiki_Fortis_Colosseum_Strategies.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-04-25) | https://oldschool.runescape.wiki/?oldid=15197186 (Guard (Fortis Colosseum)) | rev 15197186 | docs/minigames/colosseum/sources/wiki/wiki_Guard_Fortis_Colosseum.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-17) | https://oldschool.runescape.wiki/?oldid=15349115 (Sol Heredit (Echo)) | rev 15349115 | docs/minigames/colosseum/sources/wiki/wiki_Sol_Heredit_Echo.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-04-01) | https://oldschool.runescape.wiki/?oldid=15163385 (Fortis Colosseum/Modifiers) | rev 15163385 | docs/minigames/colosseum/sources/wiki/wiki_Fortis_Colosseum_Modifiers.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-17) | https://oldschool.runescape.wiki/?oldid=15349128 (Sol Heredit (Deadman)) | rev 15349128 | docs/minigames/colosseum/sources/wiki/wiki_Sol_Heredit_Deadman.wikitext | wiki page |
| 2026-10-03 | https://oldschool.runescape.wiki/wiki/Glory_(Fortis_Colosseum) | MISSING | (none) | the title does not exist; the page is titled Glory |
| 2026-10-03 (rev stamp 2026-08-21) | https://oldschool.runescape.wiki/?oldid=15315069 (Civitas illa Fortis) | rev 15315069 | docs/minigames/colosseum/sources/wiki/wiki_Civitas_illa_Fortis.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-28) | https://oldschool.runescape.wiki/?oldid=15357677 (Dizana's quiver) | rev 15357677 | docs/minigames/colosseum/sources/wiki/wiki_Dizana_s_quiver.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-29) | https://oldschool.runescape.wiki/?oldid=15358486 (Tonalztics of Ralos) | rev 15358486 | docs/minigames/colosseum/sources/wiki/wiki_Tonalztics_of_Ralos.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-04-22) | https://oldschool.runescape.wiki/?oldid=15192256 (Echo crystal) | rev 15192256 | docs/minigames/colosseum/sources/wiki/wiki_Echo_crystal.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-08-25) | https://oldschool.runescape.wiki/?oldid=15320314 (Echo boots) | rev 15320314 | docs/minigames/colosseum/sources/wiki/wiki_Echo_boots.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-08-29) | https://oldschool.runescape.wiki/?oldid=15324618 (Sunfire rune) | rev 15324618 | docs/minigames/colosseum/sources/wiki/wiki_Sunfire_rune.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-08-25) | https://oldschool.runescape.wiki/?oldid=15320313 (Sunfire fanatic helm) | rev 15320313 | docs/minigames/colosseum/sources/wiki/wiki_Sunfire_fanatic_helm.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-08-25) | https://oldschool.runescape.wiki/?oldid=15320312 (Sunfire fanatic cuirass) | rev 15320312 | docs/minigames/colosseum/sources/wiki/wiki_Sunfire_fanatic_cuirass.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-08-25) | https://oldschool.runescape.wiki/?oldid=15320311 (Sunfire fanatic chausses) | rev 15320311 | docs/minigames/colosseum/sources/wiki/wiki_Sunfire_fanatic_chausses.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-21) | https://oldschool.runescape.wiki/?oldid=15352772 (Smol Heredit) | rev 15352772 | docs/minigames/colosseum/sources/wiki/wiki_Smol_Heredit.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-28) | https://oldschool.runescape.wiki/?oldid=15357236 (Sunfire splinters) | rev 15357236 | docs/minigames/colosseum/sources/wiki/wiki_Sunfire_splinters.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-07-09) | https://oldschool.runescape.wiki/?oldid=15258602 (Gloria) | rev 15258602 | docs/minigames/colosseum/sources/wiki/wiki_Gloria.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-28) | https://oldschool.runescape.wiki/?oldid=15357222 (Blessed Dizana's quiver) | rev 15357222 | docs/minigames/colosseum/sources/wiki/wiki_Blessed_Dizana_s_quiver.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-07-14) | https://oldschool.runescape.wiki/?oldid=15263463 (Ueman, Teoki of Ralos) | rev 15263463 | docs/minigames/colosseum/sources/wiki/wiki_Ueman_Teoki_of_Ralos.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-10-01) | https://oldschool.runescape.wiki/?oldid=15359242 (Colosseum scoreboard) | rev 15359242 | docs/minigames/colosseum/sources/wiki/wiki_Colosseum_scoreboard.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-08-24) | https://oldschool.runescape.wiki/?oldid=15317627 (Money making guide/Completing the Fortis Colosseum (Wave 12)) | rev 15317627 | docs/minigames/colosseum/sources/wiki/wiki_Money_making_guide_Completing_the_Fortis_Colosseum_Wave_12.wikitext | wiki page |
| 2026-10-03 (rev stamp 2024-10-23) | https://oldschool.runescape.wiki/?oldid=14785451 (Furball) | rev 14785451 | docs/minigames/colosseum/sources/wiki/wiki_Furball.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-12) | https://oldschool.runescape.wiki/?oldid=15341148 (Sunlight spear) | rev 15341148 | docs/minigames/colosseum/sources/wiki/wiki_Sunlight_spear.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-06-22) | https://oldschool.runescape.wiki/?oldid=15237683 (Sunlit bracers) | rev 15237683 | docs/minigames/colosseum/sources/wiki/wiki_Sunlit_bracers.wikitext | wiki page |
| 2026-10-03 (rev stamp 2024-10-23) | https://oldschool.runescape.wiki/?oldid=14785454 (Sportsmanship) | rev 14785454 | docs/minigames/colosseum/sources/wiki/wiki_Sportsmanship.wikitext | wiki page |
| 2026-10-03 (rev stamp 2025-12-07) | https://oldschool.runescape.wiki/?oldid=15080364 (Denied) | rev 15080364 | docs/minigames/colosseum/sources/wiki/wiki_Denied.wikitext | wiki page |
| 2026-10-03 (rev stamp 2024-10-23) | https://oldschool.runescape.wiki/?oldid=14785458 (Colosseum Grand Champion) | rev 14785458 | docs/minigames/colosseum/sources/wiki/wiki_Colosseum_Grand_Champion.wikitext | wiki page |
| 2026-10-03 (rev stamp 2025-09-22) | https://oldschool.runescape.wiki/?oldid=14990032 (I Brought Mine Too) | rev 14990032 | docs/minigames/colosseum/sources/wiki/wiki_I_Brought_Mine_Too.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-02-24) | https://oldschool.runescape.wiki/?oldid=15133851 (Slow Dancing in the Sand) | rev 15133851 | docs/minigames/colosseum/sources/wiki/wiki_Slow_Dancing_in_the_Sand.wikitext | wiki page |
| 2026-10-03 (rev stamp 2025-03-30) | https://oldschool.runescape.wiki/?oldid=14874258 (Showboating) | rev 14874258 | docs/minigames/colosseum/sources/wiki/wiki_Showboating.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-06-04) | https://oldschool.runescape.wiki/?oldid=15225319 (Perfect Footwork) | rev 15225319 | docs/minigames/colosseum/sources/wiki/wiki_Perfect_Footwork.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-08-27) | https://oldschool.runescape.wiki/?oldid=15322301 (Reinforcements) | rev 15322301 | docs/minigames/colosseum/sources/wiki/wiki_Reinforcements.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-04-20) | https://oldschool.runescape.wiki/?oldid=15181239 (One-off) | rev 15181239 | docs/minigames/colosseum/sources/wiki/wiki_One_off.wikitext | wiki page |
| 2026-10-03 (rev stamp 2024-10-22) | https://oldschool.runescape.wiki/?oldid=14784700 (I was here first!) | rev 14784700 | docs/minigames/colosseum/sources/wiki/wiki_I_was_here_first.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-08-12) | https://oldschool.runescape.wiki/?oldid=15293859 (Colosseum Speed-Chaser) | rev 15293859 | docs/minigames/colosseum/sources/wiki/wiki_Colosseum_Speed_Chaser.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-07-31) | https://oldschool.runescape.wiki/?oldid=15284456 (Combat Achievements/Bosses) | rev 15284456 | docs/minigames/colosseum/sources/wiki/wiki_Combat_Achievements_Bosses.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-07-12) | https://oldschool.runescape.wiki/?oldid=15261787 (Searing page) | rev 15261787 | docs/minigames/colosseum/sources/wiki/wiki_Searing_page.wikitext | wiki page |
| 2026-10-03 (rev stamp 2025-05-30) | https://oldschool.runescape.wiki/?oldid=14911113 (Sunfire fanatic armour) | rev 14911113 | docs/minigames/colosseum/sources/wiki/wiki_Sunfire_fanatic_armour.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-08-28) | https://oldschool.runescape.wiki/?oldid=15323005 (Jug of sunfire wine) | rev 15323005 | docs/minigames/colosseum/sources/wiki/wiki_Jug_of_sunfire_wine.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-12) | https://oldschool.runescape.wiki/?oldid=15341383 (Are You Not Entertained?) | rev 15341383 | docs/minigames/colosseum/sources/wiki/wiki_Are_You_Not_Entertained.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-05-15) | https://oldschool.runescape.wiki/?oldid=15211088 (Bee Swarm) | rev 15211088 | docs/minigames/colosseum/sources/wiki/wiki_Bee_Swarm.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-04-25) | https://oldschool.runescape.wiki/?oldid=15197107 (Seia, Teoki of Ranul) | rev 15197107 | docs/minigames/colosseum/sources/wiki/wiki_Seia_Teoki_of_Ranul.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-08-25) | https://oldschool.runescape.wiki/?oldid=15319018 (Oriana) | rev 15319018 | docs/minigames/colosseum/sources/wiki/wiki_Oriana.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-05-17) | https://oldschool.runescape.wiki/?oldid=15212293 (Fortis Salute) | rev 15212293 | docs/minigames/colosseum/sources/wiki/wiki_Fortis_Salute.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-09) | https://oldschool.runescape.wiki/?oldid=15337621 (Rewards Chest (Fortis Colosseum)) | rev 15337621 | docs/minigames/colosseum/sources/wiki/wiki_Rewards_Chest_Fortis_Colosseum.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-07-10) | https://oldschool.runescape.wiki/?oldid=15259848 (Healing totem) | rev 15259848 | docs/minigames/colosseum/sources/wiki/wiki_Healing_totem.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-03) | https://oldschool.runescape.wiki/?oldid=15329726 (Molten Sand) | rev 15329726 | docs/minigames/colosseum/sources/wiki/wiki_Molten_Sand.wikitext | wiki page |
| 2026-10-03 (rev stamp 2025-09-03) | https://oldschool.runescape.wiki/?oldid=14978884 (Module:Tile markers/Colosseum.json) | rev 14978884 | docs/minigames/colosseum/sources/wiki/wiki_Module_Tile_markers_Colosseum_json.wikitext | wiki page |
| 2026-10-03 (rev stamp 2026-09-23) | https://oldschool.runescape.wiki/?oldid=15353887 (Glorious Champion (Fortis Colosseum)) | rev 15353887 | docs/minigames/colosseum/sources/wiki/wiki_Glorious_Champion_Fortis_Colosseum.wikitext | wiki page |
| 2026-10-03 (rev stamp 2025-09-02) | https://oldschool.runescape.wiki/?oldid=14978337 (Update:A PoH Recode & More Fixes) | rev 14978337 | docs/minigames/colosseum/sources/newsposts/wiki_Update_A_PoH_Recode_More_Fixes.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-07-02) | https://oldschool.runescape.wiki/?oldid=14692012 (Update:Colosseum NPC Clickboxes & More) | rev 14692012 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Colosseum_NPC_Clickboxes_More.wikitext | newspost |
| 2026-10-03 (rev stamp 2020-03-16) | https://oldschool.runescape.wiki/?oldid=11840202 (Update:Continent Expansion) | rev 11840202 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Continent_Expansion.wikitext | newspost |
| 2026-10-03 (rev stamp 2026-02-03) | https://oldschool.runescape.wiki/?oldid=15119418 (Update:Deadman: Annihilation - Everything You Need To Know) | rev 15119418 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Deadman_Annihilation_Everything_You_Need_To_Know.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-07-15) | https://oldschool.runescape.wiki/?oldid=14700987 (Update:Deadman: Armageddon - First Look) | rev 14700987 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Deadman_Armageddon_First_Look.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-01-31) | https://oldschool.runescape.wiki/?oldid=14543026 (Update:Defender of Varrock, Varlamore Rewards & More) | rev 14543026 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Defender_of_Varrock_Varlamore_Rewards_More.wikitext | newspost |
| 2026-10-03 (rev stamp 2026-04-08) | https://oldschool.runescape.wiki/?oldid=15170657 (Update:Demonic Pacts - Overview) | rev 15170657 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Demonic_Pacts_Overview.wikitext | newspost |
| 2026-10-03 (rev stamp 2020-03-16) | https://oldschool.runescape.wiki/?oldid=11840436 (Update:Dev Blogs: Zeah & Achievement Diaries) | rev 11840436 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Dev_Blogs_Zeah_Achievement_Diaries.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-03-27) | https://oldschool.runescape.wiki/?oldid=14614006 (Update:Easter & Varlamore Updates) | rev 14614006 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Easter_Varlamore_Updates.wikitext | newspost |
| 2026-10-03 (rev stamp 2023-10-25) | https://oldschool.runescape.wiki/?oldid=14485094 (Update:Fortis Colosseum - First Look & Rewards) | rev 14485094 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Fortis_Colosseum_First_Look_Rewards.wikitext | newspost |
| 2026-10-03 (rev stamp 2026-05-08) | https://oldschool.runescape.wiki/?oldid=15208092 (Update:Further Leagues VI Changes) | rev 15208092 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Further_Leagues_VI_Changes.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-05-15) | https://oldschool.runescape.wiki/?oldid=14660915 (Update:Further Project Rebalance (Skilling) & Varlamore Changes) | rev 14660915 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Further_Project_Rebalance_Skilling_Varlamore_Changes.wikitext | newspost |
| 2026-10-03 (rev stamp 2025-03-08) | https://oldschool.runescape.wiki/?oldid=14859778 (Update:Game Jam: Charges QoL) | rev 14859778 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Game_Jam_Charges_QoL.wikitext | newspost |
| 2026-10-03 (rev stamp 2023-10-31) | https://oldschool.runescape.wiki/?oldid=14491552 (Update:Gielinor Gazette - October 2023) | rev 14491552 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Gielinor_Gazette_October_2023.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-10-25) | https://oldschool.runescape.wiki/?oldid=14786611 (Update:Gielinor Gazette - October 2024) | rev 14786611 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Gielinor_Gazette_October_2024.wikitext | newspost |
| 2026-10-03 (rev stamp 2025-10-11) | https://oldschool.runescape.wiki/?oldid=15002330 (Update:Interface Uplift Round 2) | rev 15002330 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Interface_Uplift_Round_2.wikitext | newspost |
| 2026-10-03 (rev stamp 2026-05-06) | https://oldschool.runescape.wiki/?oldid=15207358 (Update:Leagues VI: Demonic Pacts - Fixes & Issues) | rev 15207358 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Leagues_VI_Demonic_Pacts_Fixes_Issues.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-11-25) | https://oldschool.runescape.wiki/?oldid=14808166 (Update:Leagues V Teasers & FAQs - Releasing November 27th) | rev 14808166 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Leagues_V_Teasers_FAQs_Releasing_November_27th.wikitext | newspost |
| 2026-10-03 (rev stamp 2025-08-21) | https://oldschool.runescape.wiki/?oldid=14970813 (Update:More Doom Tweaks, Poll 84, & Summer Sweep Up Changes) | rev 14970813 | docs/minigames/colosseum/sources/newsposts/wiki_Update_More_Doom_Tweaks_Poll_84_Summer_Sweep_Up_Changes.wikitext | newspost |
| 2026-10-03 (rev stamp 2025-03-12) | https://oldschool.runescape.wiki/?oldid=14861545 (Update:Permanent Deadman: World 345 Improvements) | rev 14861545 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Permanent_Deadman_World_345_Improvements.wikitext | newspost |
| 2026-10-03 (rev stamp 2025-09-10) | https://oldschool.runescape.wiki/?oldid=14983410 (Update:Pet Insurance Rework & More) | rev 14983410 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Pet_Insurance_Rework_More.wikitext | newspost |
| 2026-10-03 (rev stamp 2023-11-03) | https://oldschool.runescape.wiki/?oldid=14492766 (Update:Poll Blog: Fortis Colosseum x Perilous Moons) | rev 14492766 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Poll_Blog_Fortis_Colosseum_x_Perilous_Moons.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-06-05) | https://oldschool.runescape.wiki/?oldid=14674214 (Update:Pride 2024) | rev 14674214 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Pride_2024.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-08-01) | https://oldschool.runescape.wiki/?oldid=14711204 (Update:QoL Improvements & Further Deadman: Armageddon Tweaks) | rev 14711204 | docs/minigames/colosseum/sources/newsposts/wiki_Update_QoL_Improvements_Further_Deadman_Armageddon_Tweaks.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-04-12) | https://oldschool.runescape.wiki/?oldid=14630395 (Update:Undead Pirates, Colosseum Changes & more!) | rev 14630395 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Undead_Pirates_Colosseum_Changes_more.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-06-05) | https://oldschool.runescape.wiki/?oldid=14674093 (Update:Undead Pirates Tweaks, Varlamore CAs & More) | rev 14674093 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Undead_Pirates_Tweaks_Varlamore_CAs_More.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-03-22) | https://oldschool.runescape.wiki/?oldid=14606906 (Update:Varlamore: Part One) | rev 14606906 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Varlamore_Part_One.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-03-11) | https://oldschool.runescape.wiki/?oldid=14553839 (Update:Varlamore: Part One - Overview) | rev 14553839 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Varlamore_Part_One_Overview.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-01-11) | https://oldschool.runescape.wiki/?oldid=14523443 (Update:Varlamore: Part One - Reward Changes) | rev 14523443 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Varlamore_Part_One_Reward_Changes.wikitext | newspost |
| 2026-10-03 (rev stamp 2023-08-20) | https://oldschool.runescape.wiki/?oldid=14454623 (Update:Varlamore: The Shining Kingdom) | rev 14454623 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Varlamore_The_Shining_Kingdom.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-04-04) | https://oldschool.runescape.wiki/?oldid=14623369 (Update:Varlamore Tweaks & Drop Rates) | rev 14623369 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Varlamore_Tweaks_Drop_Rates.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-04-24) | https://oldschool.runescape.wiki/?oldid=14643458 (Update:Varlamore Tweaks, GameJam V Commences & More!) | rev 14643458 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Varlamore_Tweaks_GameJam_V_Commences_More.wikitext | newspost |
| 2026-10-03 (rev stamp 2025-03-06) | https://oldschool.runescape.wiki/?oldid=14858681 (Update:Project Rebalance: Skilling & Poll 81 MTA Changes) | rev 14858681 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Project_Rebalance_Skilling_Poll_81_MTA_Changes.wikitext | newspost |
| 2026-10-03 (rev stamp 2024-09-25) | https://oldschool.runescape.wiki/?oldid=14751651 (Update:Varlamore: The Rising Darkness is OUT NOW) | rev 14751651 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Varlamore_The_Rising_Darkness_is_OUT_NOW.wikitext | newspost |
| 2026-10-03 (rev stamp 2025-02-05) | https://oldschool.runescape.wiki/?oldid=14847851 (Update:Royal Titans) | rev 14847851 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Royal_Titans.wikitext | newspost |
| 2026-10-03 (rev stamp 2025-04-30) | https://oldschool.runescape.wiki/?oldid=14892854 (Update:Poll 84: Batch II) | rev 14892854 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Poll_84_Batch_II.wikitext | newspost |
| 2026-10-03 (rev stamp 2025-07-16) | https://oldschool.runescape.wiki/?oldid=14938870 (Update:Summer Sweep Up: Combat) | rev 14938870 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Summer_Sweep_Up_Combat.wikitext | newspost |
| 2026-10-03 (rev stamp 2025-09-02) | https://oldschool.runescape.wiki/?oldid=14978336 (Update:Varlamore & Summer Sweep-Up Combat Tweaks) | rev 14978336 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Varlamore_Summer_Sweep_Up_Combat_Tweaks.wikitext | newspost |
| 2026-10-03 (rev stamp 2026-08-07) | https://oldschool.runescape.wiki/?oldid=15289827 (Update:Summer Sweep-Up Gear & PvM Changes) | rev 15289827 | docs/minigames/colosseum/sources/newsposts/wiki_Update_Summer_Sweep_Up_Gear_PvM_Changes.wikitext | newspost |

Also fetched and NOT pinned (kept in build/corpus_tmp/news_all/, 97 Update: pages from the namespace 112 searches; listed with their Colosseum mention counts in corpus.newsposts.md): mention only.

## Where the wiki and the newsposts disagree, or one page disagrees with another (settle from the source line)

- Unique roll offset: posts number it by the wave just completed, the chest page by the reward wave (corpus.newsposts.md, top). Our server rolls once per completed wave; the row is the one for the NEXT wave's reward.
- Shaman max hit: Fortis_Colosseum page 28 vs Fortis_Colosseum_Strategies 27 (no infobox figure).
- Sol Heredit max hit: Sol_Heredit infobox 44 (Typeless AOE) and 44 grapple vs Strategies 45/up to 45; the grapple parry window: Sol_Heredit says 4 ticks, Strategies says 3 ticks.
- Sol Heredit AoE shapes: Sol_Heredit page 6x6 and 7x7 hazards; Strategies 5x6 and 5x5 and 15x15 with safe lines at 9x9 and 11x11. Light-beam ball: up to 75 (Sol page) vs 70+ (Strategies).
- Javelin special: Fortis_Colosseum says every fifth attack; Strategies says after every four autos (same thing); the sky javelin lands 6 ticks later (Strategies); Javelin page says "a few ticks".
- Manticore max hits: Fortis_Colosseum 31 melee / 36 ranged / 31 magic; Strategies 34 per hit.
- Tonalztics hit range: shipped 0-75 percent (wiki page line 39) against the 0-50 percent in the 2023 posts; Division 12.5 percent and +50 percent accuracy since 22 July 2026 (Summer_Sweep_Up_Gear_PvM_Changes).
- Death fee: Fortis_Colosseum says 75 percent reduction up to 125,000 coins until 100 completed waves; the post (newsposts Varlamore_Part_One) says the fee reduction was 75 percent until 50 waves, to become 100 waves, and Easter_Varlamore_Updates confirms 'up to 100 completed waves'.
- Totem respawn: Modifiers page and Strategies say two minutes after destruction; the 10 April 2024 post set 1 minute to 2 minutes; a later Summer Sweep-Up post's one-minute totem line is Skotizo's, not the Colosseum's.

## What this part states (numbers), grouped by unit

Format: wiki file name minus the wiki_ prefix and .wikitext, then the line, then the line's text (cut). Newspost sentences are in corpus.newsposts.md and are not repeated here. Open the file at the line when the quote is cut.

### Wave table (fixed monsters and reinforcements)

- `Fortis_Colosseum_Strategies:882` *Wave 1: Fremennik Warband, [[Serpent shaman]]
- `Fortis_Colosseum_Strategies:883` **Reinforcement: [[Jaguar warrior]]
- `Fortis_Colosseum_Strategies:884` *Wave 2: Fremennik Warband, Serpent Shaman, [[Javelin Colossus]]
- `Fortis_Colosseum_Strategies:885` **Reinforcement: Jaguar Warrior
- `Fortis_Colosseum_Strategies:886` *Wave 3: Fremennik Warband, Serpent Shaman, 2x Javelin Colossus
- `Fortis_Colosseum_Strategies:887` **Reinforcement: Jaguar Warrior
- `Fortis_Colosseum_Strategies:888` *Wave 4: Fremennik Warband, Serpent Shaman, [[Manticore]]
- `Fortis_Colosseum_Strategies:889` **Reinforcements: Jaguar Warrior, Serpent Shaman
- `Fortis_Colosseum_Strategies:890` *Wave 5: Fremennik Warband, Serpent Shaman, Javelin Colossus, Manticore
- `Fortis_Colosseum_Strategies:891` **Reinforcements: Jaguar Warrior, Serpent Shaman
- `Fortis_Colosseum_Strategies:892` *Wave 6: Fremennik Warband, Serpent Shaman, 2x Javelin Colossus, Manticore
- `Fortis_Colosseum_Strategies:893` **Reinforcements: Jaguar Warrior, Serpent Shaman
- `Fortis_Colosseum_Strategies:894` *Wave 7: Fremennik Warband, Javelin Colossus, Manticore, [[Shockwave Colossus]]
- `Fortis_Colosseum_Strategies:895` **Reinforcement: [[Minotaur (Fortis Colosseum)|Minotaur]]
- `Fortis_Colosseum_Strategies:896` *Wave 8: Fremennik Warband, 2x Javelin Colossus, Manticore, Shockwave Colossus
- `Fortis_Colosseum_Strategies:897` **Reinforcement: Minotaur
- `Fortis_Colosseum_Strategies:898` *Wave 9: Fremennik Warband, Javelin Colossus, 2x Manticore
- `Fortis_Colosseum_Strategies:899` **Reinforcement: Minotaur
- `Fortis_Colosseum_Strategies:900` *Wave 10: Fremennik Warband, 2x Javelin Colossus, 2x Manticore
- `Fortis_Colosseum_Strategies:901` **Reinforcements: Minotaur, Serpent Shaman
- `Fortis_Colosseum_Strategies:902` *Wave 11: Fremennik Warband, Javelin Colossus, 2x Manticore, Shockwave Colossus
- `Fortis_Colosseum_Strategies:903` **Reinforcements: Minotaur, Serpent Shaman
- `Fortis_Colosseum_Strategies:904` *Wave 12: [[Sol Heredit]]
- `Fortis_Colosseum_Strategies:876` If the wave is not cleared within 40 seconds, the reinforcement NPCs will spawn. Realistically, only waves 1, 2, and 4 can be cleared without letting any reinforcements spawn. If reinforcements spawn in the same tick that the wave is considered cleared, they will be automatically killed off.
- `Fortis_Colosseum:35` Like the [[TzHaar Fight Cave]]s and [[Inferno]], players will face various enemies as they progress through the waves and their spawn locations are completely random. If the player does not complete a wave within 40 seconds, additional enemy reinforcements will arrive from either the north or south 
- `Fortis_Colosseum:37` As with the Fight Caves and Inferno, new enemies are introduced at specific waves. This occurs whenever there are two javelin colossi present, with such occurrences happening at waves 3, 6, 8 and 10; the first instance introduces a manticore, while the second instance introduces a shockwave colossi 
- `Fortis_Colosseum:39` There are 11 waves of regular enemies before facing the final boss.
- `Fortis_Colosseum_Strategies:1228` * It minimises the risk of being attacked by two NPCs on wave start. You have a 1 in 6 chance to get a double south spawn on waves 8, 10, and 11. '''On SW, you have nearly a 2 in 3 chance for double north!'''
- `Fortis_Colosseum_Strategies:1228` * It minimises the risk of being attacked by two NPCs on wave start. You have a 1 in 6 chance to get a double south spawn on waves 8, 10, and 11. '''On SW, you have nearly a 2 in 3 chance for double north!'''
- `Fremennik_warband_archer:40` '''Fremennik warband archers''' are enemies encountered in the [[Fortis Colosseum]]. They appear at the start of each wave (except wave 12 without Quartet), alongside a [[Fremennik warband seer]] and [[Fremennik warband berserker]]. Attacking them with melee will always result in a max hit. If attac
- `Minotaur_Fortis_Colosseum:41` '''Minotaur''' is a monster fought in the [[Fortis Colosseum]], using melee attacks that can hit up to 74. They begin to spawn in as reinforcements through waves 7–11, with a [[serpent shaman]] spawning alongside it on waves 10 and 11. Unlike the [[jaguar warrior]], minotaurs will immediately move u
- `Serpent_shaman:42` They appear at the start of the first six waves, and appear as reinforcements for waves 4, 5, 6, 10 and 11.
- `Manticore:45` Starting from wave 9, manticores will begin spawning in pairs. If one manticore selects an attack pattern and the other is within 15 tiles of it with line of sight, the other manticore will copy its attack pattern.

### Spawn tiles and arena

- `Fortis_Colosseum:184` On the 12th and final wave, Sol Heredit will leap down from his throne and into the arena to battle the player. At the same time, a group of unarmoured gladiators will close off the centre of the arena, limiting the amount of space to a 16x16 area (with four tiles jutting out from the corners). He i
- `Fortis_Colosseum:35` Like the [[TzHaar Fight Cave]]s and [[Inferno]], players will face various enemies as they progress through the waves and their spawn locations are completely random. If the player does not complete a wave within 40 seconds, additional enemy reinforcements will arrive from either the north or south 
- `Fortis_Colosseum_Modifiers:89` The paired Colossus spawns near the main Colossus, but not necessarily on one of the 12 default spawns.
- `Fortis_Colosseum_Strategies:1203` # Start by waiting on the start tile as shown. This prevents NPCs from spawning too close. Pray [[Protect from Magic]]  on waves 1, 4, 7, 8, and 11, and [[Protect from Missiles]] on all other waves.
- `Module_Tile_markers_Colosseum_json:3` "regionId": 7216,
- `Fortis_Colosseum:13` |map = {{Map|name=Fortis Colosseum|1824,3107|zoom=2}}
- `Sol_Heredit:58` |map = {{Map|name=Sol Heredit|1823,3123|mtype=pin}}
- `Fortis_Colosseum_Strategies:1414` [[Sol Heredit]] is the final boss of the Colosseum. When his wave starts, groups of gladiators will barricade the centre of the arena, limiting the player's movement against him to the area within the four pillars (roughly a 16x15 arena).

### Monster stats: Fremennik berserker

- `Fremennik_warband_berserker:16` |hitpoints = 48
- `Fremennik_warband_berserker:11` |max hit = 29
- `Fremennik_warband_berserker:15` |attack speed = 6
- `Fremennik_warband_berserker:8` |size = 1
- `Fremennik_warband_berserker:38` |id = 12816
- `Fremennik_warband_berserker:17` |att = 110
- `Fremennik_warband_berserker:18` |str = 110
- `Fremennik_warband_berserker:19` |def = 80
- `Fremennik_warband_berserker:20` |mage = 110
- `Fremennik_warband_berserker:21` |range = 110

### Monster stats: Fremennik archer

- `Fremennik_warband_archer:16` |hitpoints = 50
- `Fremennik_warband_archer:11` |max hit = 14
- `Fremennik_warband_archer:15` |attack speed = 6
- `Fremennik_warband_archer:8` |size = 1
- `Fremennik_warband_archer:38` |id = 12814
- `Fremennik_warband_archer:17` |att = 110
- `Fremennik_warband_archer:18` |str = 110
- `Fremennik_warband_archer:19` |def = 80
- `Fremennik_warband_archer:20` |mage = 110
- `Fremennik_warband_archer:21` |range = 110

### Monster stats: Fremennik seer

- `Fremennik_warband_seer:16` |hitpoints = 50
- `Fremennik_warband_seer:11` |max hit = 12
- `Fremennik_warband_seer:15` |attack speed = 6
- `Fremennik_warband_seer:8` |size = 1
- `Fremennik_warband_seer:38` |id = 12815
- `Fremennik_warband_seer:17` |att = 110
- `Fremennik_warband_seer:18` |str = 110
- `Fremennik_warband_seer:19` |def = 80
- `Fremennik_warband_seer:20` |mage = 110
- `Fremennik_warband_seer:21` |range = 110

### Monster stats: Serpent shaman

- `Serpent_shaman:16` |hitpoints = 125
- `Serpent_shaman:11` |max hit = 28
- `Serpent_shaman:15` |attack speed = 5
- `Serpent_shaman:8` |size = 1
- `Serpent_shaman:38` |id = 12811
- `Serpent_shaman:17` |att = 100
- `Serpent_shaman:18` |str = 90
- `Serpent_shaman:19` |def = 90
- `Serpent_shaman:20` |mage = 220
- `Serpent_shaman:21` |range = 160

### Monster stats: Jaguar warrior

- `Jaguar_warrior:17` |hitpoints = 125
- `Jaguar_warrior:12` |max hit = 47 (x3)
- `Jaguar_warrior:16` |attack speed = 5
- `Jaguar_warrior:9` |size = 2
- `Jaguar_warrior:39` |id = 12810
- `Jaguar_warrior:18` |att = 200
- `Jaguar_warrior:19` |str = 330
- `Jaguar_warrior:20` |def = 125
- `Jaguar_warrior:21` |mage = 100
- `Jaguar_warrior:22` |range = 160

### Monster stats: Javelin Colossus

- `Javelin_Colossus:16` |hitpoints = 220
- `Javelin_Colossus:11` |max hit = 48, 49 (Relentless I), 51 (Relentless II), 54 (Relentless III)
- `Javelin_Colossus:15` |attack speed = 5
- `Javelin_Colossus:8` |size = 3
- `Javelin_Colossus:38` |id = 12817
- `Javelin_Colossus:17` |att = 200
- `Javelin_Colossus:18` |str = 300
- `Javelin_Colossus:19` |def = 190
- `Javelin_Colossus:20` |mage = 225
- `Javelin_Colossus:21` |range = 360

### Monster stats: Manticore

- `Manticore:16` |hitpoints = 250
- `Manticore:11` |max hit = 31 ([[Melee]]), 36 ([[Ranged]]), 31 ([[Magic]])
- `Manticore:15` |attack speed = 10
- `Manticore:8` |size = 3
- `Manticore:37` |id = 12818
- `Manticore:17` |att = 300
- `Manticore:18` |str = 300
- `Manticore:19` |def = 250
- `Manticore:20` |mage = 300
- `Manticore:21` |range = 350

### Monster stats: Shockwave Colossus

- `Shockwave_Colossus:16` |hitpoints = 125
- `Shockwave_Colossus:11` |max hit = 56
- `Shockwave_Colossus:15` |attack speed = 5
- `Shockwave_Colossus:8` |size = 3
- `Shockwave_Colossus:37` |id = 12819
- `Shockwave_Colossus:17` |att = 120
- `Shockwave_Colossus:18` |str = 190
- `Shockwave_Colossus:19` |def = 150
- `Shockwave_Colossus:20` |mage = 350
- `Shockwave_Colossus:21` |range = 220

### Monster stats: Minotaur

- `Minotaur_Fortis_Colosseum:17` |hitpoints = 225
- `Minotaur_Fortis_Colosseum:11` |max hit = 74
- `Minotaur_Fortis_Colosseum:15` |attack speed = 5
- `Minotaur_Fortis_Colosseum:8` |size = 3
- `Minotaur_Fortis_Colosseum:39` |id = 12812,12813
- `Minotaur_Fortis_Colosseum:18` |att = 300
- `Minotaur_Fortis_Colosseum:19` |str = 360
- `Minotaur_Fortis_Colosseum:20` |def = 190
- `Minotaur_Fortis_Colosseum:21` |mage = 250
- `Minotaur_Fortis_Colosseum:22` |range = 120

### Monster stats: Serpent shaman

- `Fortis_Colosseum:95` |Can only attack with mage. Spawns primarily in the early waves. One spawns in with reinforcements on waves 4-6 and waves 10 and 11.
- `Serpent_shaman:40` '''Serpent shamans''' are enemies encountered in the [[Fortis Colosseum]], using high-accuracy [[Water Surge]] spells with a range of 10 tiles.
- `Fortis_Colosseum_Strategies:736` |27

### Monster stats: Jaguar warrior

- `Fortis_Colosseum_Strategies:768` Their claws will allow them to strike the player three times per attack. Each of these attacks makes their own independent roll on accuracy and damage like that of [[nail beast]]s. With a maximum hit of 47 and high melee stats, this means that the warrior can easily kill players, potentially dealing
- `Fortis_Colosseum:106` |Only appears with reinforcements on waves 1-6. Always attacks three times on a five tick cycle.

### Monster stats: Javelin Colossus

- `Fortis_Colosseum_Strategies:788` After every four auto-attacks, the colossus will launch a javelin into the air, which will proceed to land on the player's position 6 ticks afterwards. ''If they do not move out of the way'', they will take up to 40 [[typeless]] damage.
- `Fortis_Colosseum:117` |Uses ranged attacks. Every fifth attack is replaced with a javelin launched into the air that will land on whatever spot the player is currently standing, and this attack ignores prayer. If the Reentry modifier is active, the javelin special attack leaves behind a pool of [[molten sand]].
- `Javelin_Colossus:40` '''Javelin Colossi''' are enemies encountered in [[Fortis Colosseum]] who attack with ranged, throwing their javelins at the player with a range of 15 tiles. Every five attacks, they will launch a javelin high into the air, landing on the player's current position a few ticks afterwards. This will d
- `Fortis_Colosseum_Strategies:788` After every four auto-attacks, the colossus will launch a javelin into the air, which will proceed to land on the player's position 6 ticks afterwards. ''If they do not move out of the way'', they will take up to 40 [[typeless]] damage.
- `Fortis_Colosseum_Strategies:790` The '''Reentry 1''' modifier causes the special attack to leave behind a pool of [[Molten Sand|molten sand]] on the targeted tile, which will deal up to 15 damage every two ticks while standing on them. This is cleared at the end of the wave.

### Monster stats: Manticore

- `Manticore:41` Upon spotting a player in attack range, the Manticore will proceed to charge up a triple attack with all three combat styles. It will either use a range-magic or magic-range as the first two hits; the last hit is always melee. These attacks are launched one tick after each other (0.6 seconds) and la
- `Manticore:39` '''Manticores''' are monsters encountered in the [[Fortis Colosseum]], starting on wave 4. They have an attack range of 15 tiles.
- `Manticore:47` When a manticore attacks, any other manticore that is ready to attack (i.e., finished its full 10-tick charge-up) will have its attack delayed by 5 ticks. Manticores can overlap attacks on the player if one of them is not yet ready to attack when the other attacks.
- `Fortis_Colosseum_Strategies:812` [[Manticore]]s are another dangerous enemy in the Colosseum. Unlike other monsters, the Manticore will charge up a three-hit attack which takes 10 ticks to complete. Similarly to colossi, the manticore has an attack range of 15. The attack arrangement is dependent on spawn, and it will utilise a mag
- `Fortis_Colosseum_Strategies:818` The '''Mantimayhem 1''' modifier changes the Manticores damage output, causing 2 attacks per style for a total of 6, but not changing how you would handle prayer flicking them. These attacks roll separate accuracy checks and damage. While harmless if the player can flick their attacks, they will be 
- `Fortis_Colosseum:127` |31(Melee)

### Monster stats: Shockwave Colossus

- `Shockwave_Colossus:39` '''Shockwave Colossi''' are enemies encountered in [[Fortis Colosseum]] who attack with magic and have an attack range of 15 tiles.

### Monster stats: Minotaur

- `Fortis_Colosseum_Strategies:869` Similarly to the [[Yt-MejKot]] of the Fight Caves, Minotaurs will heal other enemies if they have been damaged. However, the minotaur will continuously heal them to '''full health''' if they are within 6 tiles of them and in line of sight. Their line of sight is based on their centre tile, so their 
- `Minotaur_Fortis_Colosseum:47` - has less than 75% of its total hitpoints
- `Minotaur_Fortis_Colosseum:49` - and is 7 or less tiles away

### Monster stats: Fremennik trio

- `Fremennik_warband_berserker:48` They are able to use routefinding, and as such will move around pillars to reach players. They attack on a fixed 6 tick cycle, only attacking when they are in melee distance and are not moving.
- `Fremennik_warband_archer:55` * Ranged level increased from 3 to 110.
- `Fremennik_warband_berserker:61` * The berserker's [[hitpoints]] has been reduced from 50 to 48.
- `Fortis_Colosseum_Strategies:674` If the '''Quartet''' handicap is active, a fourth member will spawn to the south, creating a diamond-like formation. This fourth member will attack on the same tick as its fellow.

### Monster ids (RuneLite list)

- `Fortis_Colosseum_Strategies:1583` ** 12810 Jaguar warrior
- `Fortis_Colosseum_Strategies:1584` ** 12811 Serpent shaman
- `Fortis_Colosseum_Strategies:1585` ** 12812 Minotaur
- `Fortis_Colosseum_Strategies:1586` ** 12813 Minotaur (with Red Flag active)
- `Fortis_Colosseum_Strategies:1587` ** 12814 Fremennik warband archer
- `Fortis_Colosseum_Strategies:1588` ** 12815 Fremennik warband seer
- `Fortis_Colosseum_Strategies:1589` ** 12816 Fremennik warband berserker
- `Fortis_Colosseum_Strategies:1590` ** 12817 Javelin Colossus
- `Fortis_Colosseum_Strategies:1591` ** 12818 Manticore
- `Fortis_Colosseum_Strategies:1592` ** 12819 Shockwave Colossus
- `Fortis_Colosseum_Strategies:1593` ** 12821 Sol Heredit
- `Fortis_Colosseum_Strategies:1594` ** 12823 Bees
- `Fortis_Colosseum_Strategies:1595` ** 12825 Healing totem
- `Fortis_Colosseum_Strategies:1596` ** 12826 Solarflare

### Modifiers (cache enum 5312 per the page comment)

- `Fortis_Colosseum_Modifiers:37` Bees!: Glory per wave 150 (line 41)
- `Fortis_Colosseum_Modifiers:44` Bees! (II): Glory per wave 300 (line 46)
- `Fortis_Colosseum_Modifiers:49` Bees! (III): Glory per wave 450 (line 51)
- `Fortis_Colosseum_Modifiers:54` Blasphemy: Glory per wave 100 (line 56)
- `Fortis_Colosseum_Modifiers:59` Blasphemy (II): Glory per wave 200 (line 61)
- `Fortis_Colosseum_Modifiers:64` Blasphemy (III): Glory per wave 300 (line 66)
- `Fortis_Colosseum_Modifiers:69` Doom: Glory per wave 200 (line 73)
- `Fortis_Colosseum_Modifiers:76` Doom (II): Glory per wave 400 (line 78)
- `Fortis_Colosseum_Modifiers:81` Doom (III): Glory per wave 600 (line 83)
- `Fortis_Colosseum_Modifiers:86` Dynamic Duo: Glory per wave 150 (line 92)
- `Fortis_Colosseum_Modifiers:95` Frailty: Glory per wave 200 (line 97)
- `Fortis_Colosseum_Modifiers:100` Frailty (II): Glory per wave 400 (line 102)
- `Fortis_Colosseum_Modifiers:105` Frailty (III): Glory per wave 600 (line 107)
- `Fortis_Colosseum_Modifiers:110` Mantimayhem: Glory per wave 150 (line 114)
- `Fortis_Colosseum_Modifiers:117` Mantimayhem (II): Glory per wave 300 (line 120)
- `Fortis_Colosseum_Modifiers:123` Mantimayhem (III): Glory per wave 450 (line 126)
- `Fortis_Colosseum_Modifiers:129` Myopia: Glory per wave 200 (line 131)
- `Fortis_Colosseum_Modifiers:134` Myopia (II): Glory per wave 400 (line 136)
- `Fortis_Colosseum_Modifiers:139` Myopia (III): Glory per wave 600 (line 141)
- `Fortis_Colosseum_Modifiers:144` Reentry: Glory per wave 150 (line 147)
- `Fortis_Colosseum_Modifiers:150` Reentry (II): Glory per wave 300 (line 156)
- `Fortis_Colosseum_Modifiers:159` Reentry (III): Glory per wave 450 (line 162)
- `Fortis_Colosseum_Modifiers:165` Red Flag: Glory per wave 250 (line 168)
- `Fortis_Colosseum_Modifiers:171` Relentless: Glory per wave 200 (line 173)
- `Fortis_Colosseum_Modifiers:176` Relentless (II): Glory per wave 400 (line 178)
- `Fortis_Colosseum_Modifiers:181` Relentless (III): Glory per wave 600 (line 183)
- `Fortis_Colosseum_Modifiers:186` Solarflare: Glory per wave 250 (line 190)
- `Fortis_Colosseum_Modifiers:193` Solarflare (II): Glory per wave 500 (line 195)
- `Fortis_Colosseum_Modifiers:198` Solarflare (III): Glory per wave 750 (line 200)
- `Fortis_Colosseum_Modifiers:203` Quartet: Glory per wave 100 (line 205)
- `Fortis_Colosseum_Modifiers:208` Totemic: Glory per wave 200 (line 214)
- `Fortis_Colosseum_Modifiers:217` Volatility: Glory per wave 100 (line 219)
- `Fortis_Colosseum_Modifiers:222` Volatility (II): Glory per wave 200 (line 224)
- `Fortis_Colosseum_Modifiers:227` Volatility (III): Glory per wave 300 (line 231)
- `Fortis_Colosseum_Modifiers:4` enum 5312 is an index of the structs containing the info
- `Fortis_Colosseum_Modifiers:38` |A [[Bee Swarm]] will drift around the arena, slowly converging on the player at a speed of 12 ticks. If beneath the player, it will deal up to 10 unblockable poison damage every tick, and inflict poison starting at 1 damage.
- `Fortis_Colosseum_Modifiers:55` |The player's prayer points are drained by 20% of damage taken from enemies.
- `Fortis_Colosseum_Modifiers:77` |The player is now killed upon gaining 10 stacks.
- `Fortis_Colosseum_Modifiers:82` |The player is now killed upon gaining 5 stacks.
- `Fortis_Colosseum_Modifiers:96` |The player's base [[Hitpoints]] are reduced by 10%, and overhealing is disabled.
- `Fortis_Colosseum_Modifiers:101` |Base Hitpoints are reduced by '''20%'''.
- `Fortis_Colosseum_Modifiers:106` |Base Hitpoints are reduced by '''40%'''.
- `Fortis_Colosseum_Modifiers:130` |The player's attack range is reduced by two tiles. Manually casted spells are unaffected.
- `Fortis_Colosseum_Modifiers:140` |Attack range is now reduced by '''six''' tiles. Manually casted spells are unaffected.
- `Fortis_Colosseum_Modifiers:172` |Enemy attacks will now bypass 33% of the player's Defence level, and have their max hit increased by 1.
- `Fortis_Colosseum_Modifiers:177` |Attacks now bypass '''66%''' of the player's Defence, and max hits are now increased by '''3'''.
- `Fortis_Colosseum_Modifiers:182` |Enemies will fully ignore [[Damage_per_second/Melee#Step_six:_Calculate_the_hit_chance|accuracy checks]], and max hits are now increased by '''6'''.
- `Fortis_Colosseum_Modifiers:187` |A damaging orb circles around the pillars, moving every 2 ticks, then stopping for 7 ticks when it reaches a corner.
- `Fortis_Colosseum_Modifiers:199` |The orb now moves every tick, stopping for 2 ticks when it reaches a corner. It will also now disable prayers if hit, alongside dealing even more damage.
- `Fortis_Colosseum_Modifiers:209` |When an enemy is reduced to 50% hitpoints or below, a [[healing totem]] will appear near them, and send healing projectiles to the target, healing them for a 30% their health every few ticks.
- `Fortis_Colosseum_Modifiers:211` They have 1 Hitpoint and will respawn two minutes after being destroyed, or after the enemy dies. Additionally, it will not heal the enemy if they are destroyed before their healing projectile reaches them.
- `Fortis_Colosseum_Modifiers:218` |Upon death, the enemy will explode one tile greater than their size. For example, a [[manticore]], whose size is 3x3 tiles, will explode in a 5x5 radius.
- `Fortis_Colosseum_Modifiers:91` Will not be given as an option after wave 11.

### Sol Heredit (wiki page)

- `Sol_Heredit:21` |hitpoints = 1500
- `Sol_Heredit:16` |max hit = 44 (Typeless AOE), 44 (Grapple), 15-25-35 (Triple Parry 1), 15-30-45 (Triple Parry 2)
- `Sol_Heredit:13` |size = 5
- `Sol_Heredit:44` |id = 12821
- `Sol_Heredit:92` Sol Heredit transitions phases at 90%, 75%, 50%, 25% and 10% HP respectively. Each phase starts with 6 beams of light randomly placed in a 9x9 area around the player, which will spawn [[molten sand]] after 2 ticks. Each new phase will spawn a crystal which will rotate around the edges of the arena w
- `Sol_Heredit:92` Sol Heredit transitions phases at 90%, 75%, 50%, 25% and 10% HP respectively. Each phase starts with 6 beams of light randomly placed in a 9x9 area around the player, which will spawn [[molten sand]] after 2 ticks. Each new phase will spawn a crystal which will rotate around the edges of the arena w
- `Sol_Heredit:92` Sol Heredit transitions phases at 90%, 75%, 50%, 25% and 10% HP respectively. Each phase starts with 6 beams of light randomly placed in a 9x9 area around the player, which will spawn [[molten sand]] after 2 ticks. Each new phase will spawn a crystal which will rotate around the edges of the arena w
- `Sol_Heredit:96` His grapple attack can only be performed below 75% HP; he will drop his shield and call out a body part, and the player will have 4 ticks to click on the item in the respective slot to parry the attack. If the item is not clicked on or an incorrect item is selected, the equipment will be torn from t
- `Sol_Heredit:100` * '''1st attack:''' 3 ticks after the start of his animation
- `Sol_Heredit:102` * '''3rd attack:''' 3 ticks after the second (delayed to 4 ticks if under 50%)
- `Sol_Heredit:92` Sol Heredit transitions phases at 90%, 75%, 50%, 25% and 10% HP respectively. Each phase starts with 6 beams of light randomly placed in a 9x9 area around the player, which will spawn [[molten sand]] after 2 ticks. Each new phase will spawn a crystal which will rotate around the edges of the arena w
- `Sol_Heredit:104` If the player prays too early against an incoming hit in the '''Triple Parry''', Sol will forcibly disable the prayer, preventing them from turning it on until the hit lands, resulting in an unavoidable hit. The damage from these hits are 15, 25 and 35 respectively, but from phase 4 onwards the seco
- `Sol_Heredit:106` Upon reaching 10% (150 hitpoints), Sol will become enraged, summoning a final batch of molten sands into the arena. Molten sands will continually target the arena with one tile targeted every 3 ticks (1.8 seconds) around the player in a 9x9 AoE, lasers will aim at the player every 4.2 ticks (7 secon
- `Sol_Heredit:106` Upon reaching 10% (150 hitpoints), Sol will become enraged, summoning a final batch of molten sands into the arena. Molten sands will continually target the arena with one tile targeted every 3 ticks (1.8 seconds) around the player in a 9x9 AoE, lasers will aim at the player every 4.2 ticks (7 secon
- `Sol_Heredit:82` Sol Heredit has 4 main AoE attacks, all of which will create a 6x6 dust hazard under the boss, making the attacks avoidable only by stepping out. (See [[Fortis Colosseum/Strategies#Sol Heredit|''Fortis Colosseum/Strategies § Sol Heredit'']] for a diagram)
- `Sol_Heredit:88` * '''Shield 1''': Sol Heredit will create a 7x7 hazard under him, with a 1 tile gap between the next line of hazards which will cover the entire arena. Dodged by moving 1 tile back.

### Sol Heredit (Strategies page)

- `Fortis_Colosseum_Strategies:1407` |45
- `Fortis_Colosseum_Strategies:1447` Sol Heredit's attack speed is based on the attack he previously used, with the spear stabs being a 7-tick attack and the shield slam a 6-tick attack. These attacks speed up by 1 tick each after he goes below 75% health and does his phase transition.
- `Fortis_Colosseum_Strategies:1433` Sol will be unable to move for 4 ticks starting on the tick he uses an AOE attack. Additionally, Sol must be next to the player at the start of a tick (before movement is calculated) to initiate an AOE attack. If the player walks away from Sol on a tick before Sol finishes his attack cooldown, they 
- `Fortis_Colosseum_Strategies:1436` *'''Spear 1:''' '''Sol Heredit begins the fight with this attack.''' 5x6 (under him+ in front) with two 4x1 lines towards the player's direction. Avoided by stepping back 1 tile from his centre tile, or his corner tiles.
- `Fortis_Colosseum_Strategies:1437` * '''Spear 2:''' 5x5 (under him) with three 4x1 lines towards the player's direction. Avoided by stepping back 1 tile diagonally on either side of his centre tile.
- `Fortis_Colosseum_Strategies:1438` *'''Shield 1:''' large 15x15 AoE with a safe line at 9x9. Avoided by stepping back 1 tile from his melee range.
- `Fortis_Colosseum_Strategies:1439` *'''Shield 2:''' large 15x15 AoE with a safe line at 11x11. Avoided by stepping back 2 tiles from his melee range.
- `Fortis_Colosseum_Strategies:1494` During the phase transition, 6 random tiles will be marked with a pillar of light, leaving behind a puddle of molten sand that will remain for the rest of the encounter. One of these will always target the player's tile.
- `Fortis_Colosseum_Strategies:1495` *Stepping on them will damage the player for ~6-8 damage (and increment Doom, if applicable) per tick.
- `Fortis_Colosseum_Strategies:1502` Sol Heredit will charge up an attack and strike 3 times. This attack must be prayed against with [[Protect from Melee]], or you will receive heavy damage. The attack requires you to activate your prayer tick-perfectly with the attack landing. The initial attack hits 3 ticks after the start of the ch
- `Fortis_Colosseum_Strategies:1504` *On phase 1-3, each attack will deal 15, 25, and 35 damage, respectively, if not properly prayed against. In phase 4 onwards, the second and third hits are increased to 30 and 45 respectively if not blocked.
- `Fortis_Colosseum_Strategies:1507` A crystal will spawn on the outside of the arena and move a random distance. Afterwards, it will stop and project a beam of light, which after a few seconds, will shoot a ball of light which can damage the player for 70+ damage.
- `Fortis_Colosseum_Strategies:1509` *If your position is targeted, you have 3 ticks to react and move out of the way.
- `Fortis_Colosseum_Strategies:1521` Phase 3 starts with the addition of another new Light Beam, 6 more Molten Sands, and the grapple special attack.
- `Fortis_Colosseum_Strategies:1427` Sol Heredit primarily attacks with dodgeable AOE attacks. They deal up to 45 [[Typeless|Typeless Melee]] damage which can be completely avoided if dodged correctly. Keeping your [[Protect from Melee]] prayer on should be '''avoided''', as it does not reduce his AOE damage, and having the prayer on d
- `Fortis_Colosseum_Strategies:1557` During Enrage Phase, Sol Heredit adds an initial 5 Molten Sands, and 1 extra Molten Sand every 1.8 seconds (3 game ticks), targeting a random unaffected tile in the arena. Like the phase transition sand, this sand is also in a 9 by 9 grid around the player, and can fail to spawn if Sol attempts to s
- `Fortis_Colosseum_Strategies:1559` Light beams '''fire much faster''' and more frequently, sending out an attack approximately every 7 seconds. '''You have only 2 ticks to react''' from the moment a beam targets your position. During the chaos of the fight, dodging the light beams must be of highest priority, as it will result in you
- `Fortis_Colosseum_Strategies:1552` No more Light Beams are added from this point, for a total of 4.
- `Fortis_Colosseum_Strategies:1569` *If '''Totemic''' is active, the totems will begin spawning when Sol Heredit reaches 50% of his health and will heal him for 75 hitpoints every 4.2 seconds if given the chance until destroyed. You can safely one-shot it with any manually casted spell; any weapon attack also works, but be careful of 
- `Fortis_Colosseum_Strategies:1530` ** ''I'LL CRUSH YOUR '''BODY'''!'' indicates that you must defend the gear in the platebody slot.
- `Fortis_Colosseum_Strategies:1531` ** ''I'LL BREAK YOUR '''BACK'''''<nowiki/>'''!''' indicates that you must defend the gear in the cape slot.
- `Fortis_Colosseum_Strategies:1532` ** ''I'LL TWIST YOUR '''HANDS''' OFF!'' indicates that you must defend the gear in the gloves slot.
- `Fortis_Colosseum_Strategies:1533` ** ''I'LL BREAK YOUR '''LEGS'''!'' indicates that you must defend the gear in the platelegs slot.
- `Fortis_Colosseum_Strategies:1534` ** ''I'LL CUT YOUR '''FEET''' OFF!'' indicates that you must defend the gear in the boots slot.
- `Fortis_Colosseum_Strategies:1528` *Countering the attack within 3 ticks will result in the chat message "''You successfully defend from Sol Heredit's grapple!",'' blocking the damage. Countering it on the last possible tick, however,  results in the chat message ''"You perfectly parry Sol Heredit's grapple!",'' which will also empow
- `Fortis_Colosseum:188` After falling below 90% of his health, he will begin to utilise special attacks, such as a three-hit melee combo that deals massive damage and disables overhead protection prayers, though it can be blocked on a perfect tick. He will also attempt to grapple the player, targeting a specific equipment 
- `Fortis_Colosseum:190` When Sol Heredit reaches ~150 hitpoints, he will enter an enrage phase, quickly engulfing the arena with beams of light until slain, making it important to kill him before too much of the arena becomes unusable. Once he is slain, the ground hazards subside and the reward chest appears.

### Glory

- `Glory:21` wave 1: completion 100, no-damage 100, modifier points 200, minimal cumulative 200
- `Glory:27` wave 2: completion 200, no-damage 200, modifier points 450, minimal cumulative 600
- `Glory:33` wave 3: completion 300, no-damage 300, modifier points 700, minimal cumulative 1,200
- `Glory:39` wave 4: completion 400, no-damage 400, modifier points 950, minimal cumulative 2,000
- `Glory:45` wave 5: completion 500, no-damage 500, modifier points 1,150, minimal cumulative 3,000
- `Glory:51` wave 6: completion 600, no-damage 600, modifier points 1,350, minimal cumulative 4,200
- `Glory:57` wave 7: completion 700, no-damage 700, modifier points 1,600, minimal cumulative 5,600
- `Glory:63` wave 8: completion 800, no-damage 800, modifier points 1,800, minimal cumulative 7,250
- `Glory:69` wave 9: completion 900, no-damage 900, modifier points 2,000, minimal cumulative 9,150
- `Glory:75` wave 10: completion 1,000, no-damage 1,000, modifier points 2,200, minimal cumulative 11,300
- `Glory:81` wave 11: completion 1,100, no-damage 1,100, modifier points 2,400, minimal cumulative 13,700 (row added by the corpus closer, 2026-10-03, read from `Glory.wikitext:81-86`)
- `Glory:87` wave 12: completion 1,200, no-damage 1,200, modifier points 2,600, minimal cumulative 16,350
- `Glory:112` time bonus wave 1: start 500 points, minus 1 per tick
- `Glory:116` time bonus wave 2: start 1,000 points, minus 2 per tick
- `Glory:120` time bonus wave 3: start 1,500 points, minus 3 per tick
- `Glory:124` time bonus wave 4: start 2,000 points, minus 4 per tick
- `Glory:128` time bonus wave 5: start 2,500 points, minus 5 per tick
- `Glory:132` time bonus wave 6: start 3,000 points, minus 6 per tick
- `Glory:136` time bonus wave 7: start 3,500 points, minus 7 per tick
- `Glory:140` time bonus wave 8: start 4,000 points, minus 8 per tick
- `Glory:144` time bonus wave 9: start 4,500 points, minus 9 per tick
- `Glory:148` time bonus wave 10: start 5,000 points, minus 10 per tick
- `Glory:152` time bonus wave 11: start 5,500 points, minus 11 per tick
- `Glory:156` time bonus wave 12: start 6,000 points, minus 12 per tick

- `Glory:21` |1
- `Glory:164` The theoretical maximum Glory would be 72,000 if all waves were completed on the first tick. Taking the fastest possible wave completions into account, the likely limit is somewhere between 60,000 - 65,000.
- `Glory:166` The minimum amount of Glory that one can theoretically complete the Colosseum with is 16,350.
- `Glory:46` |500
- `Glory:161` <ref name="points-lost">For example, if Wave 1 takes 30 seconds (50 ticks) the Player will receive a Time Bonus of 450 points. Note that it is impossible to receive an odd-numbered time bonus, and these will be rounded down to the next even number.</ref>
- `Glory:178` |Brawler
- `Glory:200` |Grand Champion
- `Glory:9` Glory is not attained cumulatively; instead, it works on a personal best system. For example, to unlock the bank chest inside the Colosseum, you must earn 2,000 points as your personal best of the whole run.
- `Glory:5` Every time the player completes a wave, they will earn Glory, in which the amount gained is dependent on the player's performance - damage taken, time spent, errors made, and types of handicaps selected. The player will retain Glory even if they die during a subsequent wave, but not if they teleport
- `Glory:101` <ref name="no-damage-bonus">Only awarded when the player takes no damage from NPCs (not including environmental damage such as Solarflare or molten sand, self-inflicted damage such as divine potions, or the javelin launch from the [[Javelin Colossus]]).</ref>

### Reward pool, cash-out, death

- `Rewards_Chest_Fortis_Colosseum:21` Upon the '''end''' of wave 3, the chest will begin to roll the unique drop table, with the chance increasing as the player reaches higher waves. Upon successfully rolling the unique table, the chest will then roll for following items:
- `Rewards_Chest_Fortis_Colosseum:23` Waves 4-6:
- `Rewards_Chest_Fortis_Colosseum:24` * 4/10 for an [[echo crystal]]; a successful roll for 1 will additionally roll a 1/10 chance of receiving either 2 or 3 crystals.
- `Rewards_Chest_Fortis_Colosseum:25` * 6/10 for a piece of the [[sunfire fanatic armour]]. There is a duplicate-avoidance system and the game will give preference to a piece not received; otherwise, all parts have an equal chance of rolling.
- `Rewards_Chest_Fortis_Colosseum:28` * 6/16 for an [[echo crystal]]; a successful roll for 1 will additionally roll a 1/10 chance of receiving either 2 or 3 crystals.
- `Rewards_Chest_Fortis_Colosseum:29` * 9/16 for a piece of the [[sunfire fanatic armour]]. There is a duplicate-avoidance system and the game will give preference to a piece not received; otherwise, all parts have an equal chance of rolling.
- `Rewards_Chest_Fortis_Colosseum:30` * 1/16 for the uncharged [[Tonalztics of Ralos]].
- `Rewards_Chest_Fortis_Colosseum:112` Lastly, completing wave 12 will guarantee [[Dizana's quiver]] from the rewards chest. Additionally, a flat 1/200 chance for the pet [[Smol Heredit]] is rolled. The quiver can later be given to [[Minimus]] for either 4,000 [[sunfire splinter]]s or another 1/200 chance at receiving Smol Heredit.
- `Rewards_Chest_Fortis_Colosseum:114` As wave 1 has a guaranteed drop of 80 sunfire splinters and, depending on the player's gear, can be cleared in about 25-60 seconds, repeatedly clearing the first wave makes for an ideal method of obtaining sunfire splinters for players struggling to consistently clear the higher waves. Those who can
- `Rewards_Chest_Fortis_Colosseum:117` On average, players can expect to receive 2014.6 [[Sunfire splinters]] per full completion of the Colosseum, or 6014.6 splinters if [[Dizana's quiver]] is traded in.
- `Rewards_Chest_Fortis_Colosseum:119` ===Wave 1===
- `Rewards_Chest_Fortis_Colosseum:125` ===Wave 2===
- `Rewards_Chest_Fortis_Colosseum:137` ===Wave 3===
- `Rewards_Chest_Fortis_Colosseum:157` ===Wave 4===
- `Rewards_Chest_Fortis_Colosseum:181` ===Wave 5===
- `Rewards_Chest_Fortis_Colosseum:198` ===Wave 6===
- `Rewards_Chest_Fortis_Colosseum:215` ===Wave 7===
- `Rewards_Chest_Fortis_Colosseum:239` ===Wave 8===
- `Rewards_Chest_Fortis_Colosseum:263` ===Wave 9===
- `Rewards_Chest_Fortis_Colosseum:285` ===Wave 10===
- `Rewards_Chest_Fortis_Colosseum:307` ===Wave 11===
- `Rewards_Chest_Fortis_Colosseum:327` ===Wave 12===
- `Fortis_Colosseum:177` If the player dies in the Colosseum they will respawn in the lobby area along with their [[grave]] rather than their normal [[respawn point]]. Players will have their reclamation fee reduced by 75% (up to {{Coins|125000}}) until they have completed 100 waves, after which the standard [[Death's Offic
- `Fortis_Colosseum:179` Alternatively, players can choose to end their Colosseum run and collect the loot they've earned to that point, with each wave adding an extra reward and the quality thereof increasing on later waves. This can only be done between waves: dying, logging out, or teleporting out will cause it to be for
- `Fortis_Colosseum:179` Alternatively, players can choose to end their Colosseum run and collect the loot they've earned to that point, with each wave adding an extra reward and the quality thereof increasing on later waves. This can only be done between waves: dying, logging out, or teleporting out will cause it to be for
- `Fortis_Colosseum:177` If the player dies in the Colosseum they will respawn in the lobby area along with their [[grave]] rather than their normal [[respawn point]]. Players will have their reclamation fee reduced by 75% (up to {{Coins|125000}}) until they have completed 100 waves, after which the standard [[Death's Offic
- `Fortis_Colosseum:28` Upon entering the Colosseum, players will arrive at a lobby area under the Colosseum itself, where they can enter the arena and begin the minigame by initially speaking to [[Minimus]], the Colosseum Master. A [[Bank_chest#Fortis_Colosseum|bank chest]] south of the arena's entrance can be accessed by

### Entry and surrounding systems

- `Fortis_Colosseum:21` As with all of Varlamore, the Fortis Colosseum requires completion of the quest [[Children of the Sun]].
- `Fortis_Colosseum:22` * Players can use the [[ring of dueling]] teleport to Fortis Colosseum. (requires 12,000 glory)
- `Minimus:21` '''Minimus''' is an extremely skilled [[Dwarf (race)|dwarven]] gladiator who serves as the Colosseum Master{{CiteNPC|npc=Minimus|quote=Eventually, the kingdom gave me the role of Colosseum Master. I still make sure to fit some fighting in, but now I have the honor of managing the fights themselves.}
- `Minimus:23` When attempting the Colosseum, if players do not pick a handicap for the next wave, Minimus will appear next to them so they can do so. Players can also give him excess unblessed [[Dizana's quiver]]s for either 4,000 [[sunfire splinters]] or an additional 1/200 chance at the [[Smol Heredit]] pet.<re
- `Smol_Heredit:43` '''Smol Heredit''' is a [[pet]] that can be obtained from completing the [[Fortis Colosseum]] after defeating [[Sol Heredit]]. Players have a 1/200 chance of obtaining him. Alternatively, players can exchange [[Dizana's quiver]] to [[Minimus]] for another 1/200 chance to obtain him. Unlike other bos
- `Dizana_s_quiver:49` '''Dizana's quiver''' is an item that requires level 75 [[Ranged]] to equip. It provides the [[best in slot]] ranged attack bonus for the cape slot, and is awarded for defeating [[Sol Heredit]] in the [[Fortis Colosseum]].
- `Dizana_s_quiver:53` Quivers can also be brought to [[Minimus]] to be exchanged for either 4,000 [[sunfire splinters]] or a 1/200 chance at receiving [[Smol Heredit]] (if not already in the player's possession), giving the item an indirect value of {{Coins|{{GEP|Sunfire splinters|4000}}}}. Players can configure the char
- `Tonalztics_of_Ralos:39` When charged, the weapon will [[Multi-hit weapons|hit twice]], with two independent damage rolls and an attack range of 7 tiles (9 on longrange). Uncharged, the weapon hits a target once for 0-75% of the player's [[maximum ranged hit]], with a slightly worse attack range of 6 tiles (8 on longrange).
- `Tonalztics_of_Ralos:76` The Tonalztics of Ralos has a [[special attack]], ''Division'', costing 50% special attack energy. ''Division'' increases accuracy by 50% and reduces the target's [[Defence]] level by 1/8 of the target's [[Magic]] level upon a [[successful hit]]. This effect occurs for each individual hit, meaning a
- `Sunfire_splinters:26` * Creating [[sunfire rune]]s with [[rune essence]] (and variants) alongside [[fire rune]]s (1 fire rune and 1 sunfire splinter are used per essence) and by extension making [[searing page]]s
- `Echo_boots:22` '''Echo boots''' are an enhanced pair of [[guardian boots]] which require level 75 [[Defence]] to wear. They are made by combining the boots with an [[echo crystal]], a rare drop from participating in the [[Fortis Colosseum]]. Compared to its predecessor, it gives an additional +2 [[prayer bonus]] a

### Music

- `Are_You_Not_Entertained:20` |unlockdetail = Unlocked in the [[Fortis Colosseum]]. It only unlocks upon starting the first wave, not by simply entering the colosseum.
- `Are_You_Not_Entertained:12` |duration = 05:29
- `Glorious_Champion_Fortis_Colosseum:13` |duration = 00:13
- `Fortis_Colosseum:197` {{Listen|filename=Glorious Champion (Fortis Colosseum).ogg|title= [[Glorious Champion (Fortis Colosseum)|Glorious Champion]]|desc=Ralos has smiled upon you...Music that plays when defeating Sol Heredit.}}

### Combat Achievements

- `Colosseum_Speed_Chaser:7` |description = Complete the Colosseum with a total time of 28:00 or less.
- `Fortis_Colosseum:257` {{Combat Achievements list|{{PAGENAME}}|mobname=the {{PAGENAME}}}}

### Cross-check against the cache (read-only, 2026-10-03)

The fourteen npc ids the Strategies page lists for the RuneLite highlighter (Fortis_Colosseum_Strategies:1583-1596) each exist in sources/cache_npc.txt with a colosseum_ name that fits: 12810 jaguar_warrior, 12811 standard_mager, 12812 minotaur, 12813 minotaur_routefind (the Red Flag variant), 12814 warbander_ranged, 12815 warbander_mage, 12816 warbander_melee, 12817 javelin_colossus, 12818 manticore, 12819 shockwave_colossus, 12821 sol_p1, 12823 modifier_bees, 12825 healing_totem, 12826 solar_flare. The wiki states no animation, projectile or graphic id anywhere; the one animation statement is Jaguar_warrior:25 (same attack animation as the dragon claws special, minus the uppercut). Not available from the wiki: attack animation ids, projectile ids, spawn tile coordinates per spawn (only the 12 default spawns and the A/B/start tiles as unnamed markers in Module_Tile_markers_Colosseum_json), a wave table with random versus fixed marked, reinforcement timing in ticks (40 seconds only).
