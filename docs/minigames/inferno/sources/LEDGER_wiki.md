# LEDGER_wiki: the wiki part of the Inferno source corpus

Pass: matthew-mbp-m4-waves-b1-spec-inferno, worker "wiki". Fetched 2026-10-03 (Saturday) between 10:56 and 11:05 CDT.
Host: oldschool.runescape.wiki only, one worker, sequential, never parallel. Pacing: `tools/toa_fetch_wiki.py` sleeps 0.8 s per page and 0.8 s per batch
(about one request per second); my own search and screening scripts slept 1.2 to 1.5 s between requests. User-Agent `3draster-toa-research/1.0 (mrobertevers@gmail.com)`
(the one the tool carries). The pin guard in the tool kept every existing file; no `.rev<id>` file was written (no collision: `ls` shows none).
Nothing was fetched from Blert, GitHub or YouTube in this part; the Mod Ash tweets quoted by the wiki were NOT fetched (their archive urls are on the pinned pages).

## How the sources were found (every search, 2026-10-03)

1. Newspost search, namespace 112, `srlimit=50`, queries: Inferno (50 hits), TzKal-Zuk (37), Infernal cape (50), Jal-Nib-Rek (3), Mor Ul Rek (17), Jad (50), Inferno practice (14).
   Extra queries to cover what those missed: "The Inferno" (50), Zuk (50), Jal-Xil (3), "fire cape" Inferno (18), "Inferno" pillars (10), "Fight Caves" Inferno (29),
   TzHaar Inferno (24), "Infernal cape" unrestricted (1), Inferno "Ancestral Glyph" (1), Jal-Zek mager (0), Jal-Ak blob (0), Jal-ImKot (2). 190 distinct `Update:` titles.
   Scripts: `build/corpus_tmp/search.py`, `search2.py`; result lists `search_results.txt`, `search2_results.txt` (not in git).
2. Screening read: the raw wikitext of all 193 candidate posts (the 190 plus the dated updates named in the Changes boxes of the monster pages) was read in batches of 15 titles
   (`action=query&prop=revisions&rvprop=content`) into `build/corpus_tmp/upd/` (not in git), then scanned for inferno, zuk, jad, jal-, fire cape, infernal. 71 posts that state an
   Inferno fact or a number were pinned through the tool (the second fetch, below); the rest mention the name in passing and are not pinned (listed under "Not pinned").
3. The Inferno page's History and Changes sections were read for the dated updates (`wiki_Inferno.wikitext`:322-395): The Inferno (2017-06-01), Pest Control & Open Weekend (2017-06-08),
   QoL, Deadman and the Falador Party Room (2018-03-15), Phosani's Nightmare (2021-06-30). The monster pages' Changes boxes added: More Doom Tweaks, Poll 84 (2025-08-20),
   Summer Sweep Up: Combat (2025-06-25), Easter Event (2025-04-16), Deadman: Armageddon (2024-07-24), Leagues III (2022-01-19), PJ Timer Beta (2021-12-01),
   God Wars Instancing (2021-01-13), the 5th Birthday (2018-02-22), Poll 80 (cape repair), Interface Uplift. All pinned.
4. Combat achievements: `insource:/monster = TzKal-Zuk/` (12 hits, all twelve are Inferno tasks); searches for `monster = Inferno`, `The Inferno`, `JalTok-Jad` returned 0.
5. Module and Bucket data: namespace 828 search "Inferno" and "Zuk" found only `Module:Tile markers/Inferno Zuk Safespots.json` as Inferno data (pinned). The wave table, drop table
   and monster rows are in the pages' own wikitext (no Module carries them). The Combat Achievements list is bucket-rendered (the pages themselves are the pin).
6. Where a title did not exist: `TzKal-Zuk/Strategies` (MISSING). Searched "Inferno slayer", "Inferno speed", "Inferno music", "Tokkul Inferno wave"; found `Inferno/Strategies#TzKal-Zuk`,
   `Slayer task/TzHaar`, `Tzkal slayer helmet` (all pinned) and the twelve CA task pages. Redirects: none of the requested titles redirected (the resolved title equals the request in every manifest row).

## Fetch table (one row per pin; all fetched 2026-10-03)

| Date | URL (permalink) | Revision | File | What it is for |
|---|---|---|---|---|
| 2026-10-03 | https://oldschool.runescape.wiki/w/TzKal-Zuk/Strategies | MISSING (no such page) | none | searched instead: the Zuk strategy is the TzKal-Zuk section of Inferno/Strategies |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Yt-HurKot?oldid=15316944 | rev 15316944 (rev date 2026-08-23) | `docs/minigames/inferno/sources/wiki/wiki_Yt_HurKot.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Inferno?oldid=15337520 | rev 15337520 (rev date 2026-09-09) | `docs/minigames/inferno/sources/wiki/wiki_Inferno.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Infernal_cape?oldid=15359317 | rev 15359317 (rev date 2026-10-01) | `docs/minigames/inferno/sources/wiki/wiki_Infernal_cape.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Jal-Nib-Rek?oldid=15352769 | rev 15352769 (rev date 2026-09-21) | `docs/minigames/inferno/sources/wiki/wiki_Jal_Nib_Rek.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/TzKal-Zuk?oldid=15316929 | rev 15316929 (rev date 2026-08-23) | `docs/minigames/inferno/sources/wiki/wiki_TzKal_Zuk.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Jal-Ak?oldid=15316947 | rev 15316947 (rev date 2026-08-23) | `docs/minigames/inferno/sources/wiki/wiki_Jal_Ak.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Jal-AkRek-Xil?oldid=15316950 | rev 15316950 (rev date 2026-08-23) | `docs/minigames/inferno/sources/wiki/wiki_Jal_AkRek_Xil.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Jal-MejRah?oldid=15316946 | rev 15316946 (rev date 2026-08-23) | `docs/minigames/inferno/sources/wiki/wiki_Jal_MejRah.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Jal-Nib?oldid=15316945 | rev 15316945 (rev date 2026-08-23) | `docs/minigames/inferno/sources/wiki/wiki_Jal_Nib.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Jal-AkRek-Mej?oldid=15316949 | rev 15316949 (rev date 2026-08-23) | `docs/minigames/inferno/sources/wiki/wiki_Jal_AkRek_Mej.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Jal-ImKot?oldid=15358435 | rev 15358435 (rev date 2026-09-29) | `docs/minigames/inferno/sources/wiki/wiki_Jal_ImKot.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Jal-Zek?oldid=15316955 | rev 15316955 (rev date 2026-08-23) | `docs/minigames/inferno/sources/wiki/wiki_Jal_Zek.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Jal-Xil?oldid=15316952 | rev 15316952 (rev date 2026-08-23) | `docs/minigames/inferno/sources/wiki/wiki_Jal_Xil.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/JalTok-Jad?oldid=15316953 | rev 15316953 (rev date 2026-08-23) | `docs/minigames/inferno/sources/wiki/wiki_JalTok_Jad.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Jal-AkRek-Ket?oldid=15316948 | rev 15316948 (rev date 2026-08-23) | `docs/minigames/inferno/sources/wiki/wiki_Jal_AkRek_Ket.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Jal-MejJak?oldid=15316954 | rev 15316954 (rev date 2026-08-23) | `docs/minigames/inferno/sources/wiki/wiki_Jal_MejJak.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Inferno/Strategies?oldid=15350480 | rev 15350480 (rev date 2026-09-18) | `docs/minigames/inferno/sources/wiki/wiki_Inferno_Strategies.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Rocky_support?oldid=14552065 | rev 14552065 (rev date 2024-03-03) | `docs/minigames/inferno/sources/wiki/wiki_Rocky_support.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Ancestral_Glyph?oldid=15350504 | rev 15350504 (rev date 2026-09-18) | `docs/minigames/inferno/sources/wiki/wiki_Ancestral_Glyph.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Tokkul?oldid=15183440 | rev 15183440 (rev date 2026-04-22) | `docs/minigames/inferno/sources/wiki/wiki_Tokkul.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Mor_Ul_Rek?oldid=15337990 | rev 15337990 (rev date 2026-09-09) | `docs/minigames/inferno/sources/wiki/wiki_Mor_Ul_Rek.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/TzHaar-Ket-Keh?oldid=15298692 | rev 15298692 (rev date 2026-08-14) | `docs/minigames/inferno/sources/wiki/wiki_TzHaar_Ket_Keh.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Inferno_(music_track)?oldid=15340435 | rev 15340435 (rev date 2026-09-11) | `docs/minigames/inferno/sources/wiki/wiki_Inferno_music_track.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Combat_Achievements/Master?oldid=15329081 | rev 15329081 (rev date 2026-09-02) | `docs/minigames/inferno/sources/wiki/wiki_Combat_Achievements_Master.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Combat_Achievements/Grandmaster?oldid=15321195 | rev 15321195 (rev date 2026-08-26) | `docs/minigames/inferno/sources/wiki/wiki_Combat_Achievements_Grandmaster.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Tzkal_slayer_helmet?oldid=15320325 | rev 15320325 (rev date 2026-08-25) | `docs/minigames/inferno/sources/wiki/wiki_Tzkal_slayer_helmet.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Nibblers,_Begone!?oldid=14771391 | rev 14771391 (rev date 2024-10-13) | `docs/minigames/inferno/sources/wiki/wiki_Nibblers_Begone.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Jad?_What_Are_You_Doing_Here??oldid=14828785 | rev 14828785 (rev date 2024-12-29) | `docs/minigames/inferno/sources/wiki/wiki_Jad_What_Are_You_Doing_Here.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Nibbler_Chaser?oldid=15299777 | rev 15299777 (rev date 2026-08-14) | `docs/minigames/inferno/sources/wiki/wiki_Nibbler_Chaser.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Inferno_Grandmaster?oldid=14771468 | rev 14771468 (rev date 2024-10-13) | `docs/minigames/inferno/sources/wiki/wiki_Inferno_Grandmaster.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Facing_Jad_Head-on_II?oldid=15307320 | rev 15307320 (rev date 2026-08-19) | `docs/minigames/inferno/sources/wiki/wiki_Facing_Jad_Head_on_II.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Playing_with_Jads?oldid=14771505 | rev 14771505 (rev date 2024-10-13) | `docs/minigames/inferno/sources/wiki/wiki_Playing_with_Jads.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/No_Luck_Required?oldid=14862964 | rev 14862964 (rev date 2025-03-16) | `docs/minigames/inferno/sources/wiki/wiki_No_Luck_Required.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Wasn%27t_Even_Close?oldid=14936348 | rev 14936348 (rev date 2025-07-12) | `docs/minigames/inferno/sources/wiki/wiki_Wasn_t_Even_Close.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Budget_Setup?oldid=15319472 | rev 15319472 (rev date 2026-08-25) | `docs/minigames/inferno/sources/wiki/wiki_Budget_Setup.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Half-Way_There?oldid=14771568 | rev 14771568 (rev date 2024-10-13) | `docs/minigames/inferno/sources/wiki/wiki_Half_Way_There.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Inferno_Speed-Runner?oldid=14771571 | rev 14771571 (rev date 2024-10-13) | `docs/minigames/inferno/sources/wiki/wiki_Inferno_Speed_Runner.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/The_Floor_Is_Lava?oldid=15319473 | rev 15319473 (rev date 2026-08-25) | `docs/minigames/inferno/sources/wiki/wiki_The_Floor_Is_Lava.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Slayer_task/TzHaar?oldid=15316995 | rev 15316995 (rev date 2026-08-23) | `docs/minigames/inferno/sources/wiki/wiki_Slayer_task_TzHaar.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Module:Tile_markers/Inferno_Zuk_Safespots.json?oldid=15350387 | rev 15350387 (rev date 2026-09-18) | `docs/minigames/inferno/sources/wiki/wiki_Module_Tile_markers_Inferno_Zuk_Safespots_json.wikitext` | wiki page, raw wikitext (api parse by oldid) |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:XP_Drops,_Jad_Pet_%26_Slayer?oldid=11842908 | rev 11842908 (rev date 2020-03-16) | `docs/minigames/inferno/sources/newsposts/wiki_Update_XP_Drops_Jad_Pet_Slayer.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Devblog_38_-_Zeah,_Deadman_Mode_and_Jad_2?oldid=13981521 | rev 13981521 (rev date 2020-12-22) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Devblog_38_Zeah_Deadman_Mode_and_Jad_2.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Dev_Blog:_The_Inferno_v2?oldid=13981520 | rev 13981520 (rev date 2020-12-22) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Dev_Blog_The_Inferno_v2.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:March_-_The_Month_Ahead_(2017)?oldid=11842748 | rev 11842748 (rev date 2020-03-16) | `docs/minigames/inferno/sources/newsposts/wiki_Update_March_The_Month_Ahead_2017.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Dev_Blog:_Brimstone_%26_The_Inferno?oldid=11842663 | rev 11842663 (rev date 2020-03-16) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Dev_Blog_Brimstone_The_Inferno.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Dev_Blog:_Mor_Ul_Rek_%26_The_Inferno?oldid=11840804 | rev 11840804 (rev date 2020-03-16) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Dev_Blog_Mor_Ul_Rek_The_Inferno.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Infernal_Cape_Design_Update?oldid=14741667 | rev 14741667 (rev date 2024-09-08) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Infernal_Cape_Design_Update.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Progress_Report:_The_Inferno?oldid=11840454 | rev 11840454 (rev date 2020-03-16) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Progress_Report_The_Inferno.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:World_Map_%26_Balancing_Changes?oldid=11842902 | rev 11842902 (rev date 2020-03-16) | `docs/minigames/inferno/sources/newsposts/wiki_Update_World_Map_Balancing_Changes.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Network_maintenance?oldid=11842773 | rev 11842773 (rev date 2020-03-16) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Network_maintenance.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:The_Inferno?oldid=14785131 | rev 14785131 (rev date 2024-10-22) | `docs/minigames/inferno/sources/newsposts/wiki_Update_The_Inferno.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Pest_Control_%26_Open_Weekend?oldid=7955970 | rev 7955970 (rev date 2018-11-13) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Pest_Control_Open_Weekend.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Inferno_slayer_task,_Tournament_worlds,_and_Poll_56!?oldid=14858652 | rev 14858652 (rev date 2025-03-06) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Inferno_slayer_task_Tournament_worlds_and_Poll_56.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:QoL,_Deadman_and_the_Falador_Party_Room?oldid=11840823 | rev 11840823 (rev date 2020-03-16) | `docs/minigames/inferno/sources/newsposts/wiki_Update_QoL_Deadman_and_the_Falador_Party_Room.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Small_Game_Updates_and_Betas?oldid=14202231 | rev 14202231 (rev date 2021-11-06) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Small_Game_Updates_and_Betas.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Combat_Achievements_Poll_Blog?oldid=13960821 | rev 13960821 (rev date 2020-11-27) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Combat_Achievements_Poll_Blog.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Dev_Blog:_The_Inferno?oldid=13981519 | rev 13981519 (rev date 2020-12-22) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Dev_Blog_The_Inferno.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:TzHaar-Ket-Rak%27s_Challenges?oldid=14024533 | rev 14024533 (rev date 2021-03-03) | `docs/minigames/inferno/sources/newsposts/wiki_Update_TzHaar_Ket_Rak_s_Challenges.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Combat_Achievements?oldid=14204222 | rev 14204222 (rev date 2021-11-14) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Combat_Achievements.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Land_of_the_Goblins?oldid=14243982 | rev 14243982 (rev date 2022-02-09) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Land_of_the_Goblins.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Mining_Guild_Expansion?oldid=11842766 | rev 11842766 (rev date 2020-03-16) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Mining_Guild_Expansion.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Old_School_RuneScape%27s_5th_Birthday?oldid=11840448 | rev 11840448 (rev date 2020-03-16) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Old_School_RuneScape_s_5th_Birthday.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:God_Wars_Instancing_and_Soul_Wars_Improvements?oldid=14000857 | rev 14000857 (rev date 2021-01-20) | `docs/minigames/inferno/sources/newsposts/wiki_Update_God_Wars_Instancing_and_Soul_Wars_Improvements.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Equipment_Rebalancing_Changes?oldid=14004216 | rev 14004216 (rev date 2021-01-26) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Equipment_Rebalancing_Changes.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Shooting_Stars?oldid=14033120 | rev 14033120 (rev date 2021-03-18) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Shooting_Stars.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Equipment_Rebalance:_Ranged_Meta?oldid=14056723 | rev 14056723 (rev date 2021-04-20) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Equipment_Rebalance_Ranged_Meta.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Poll_75_Game_Improvements_Blog?oldid=14363427 | rev 14363427 (rev date 2023-01-17) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Poll_75_Game_Improvements_Blog.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Equipment_Rebalance:_Ranged_Meta_Proposal?oldid=14104727 | rev 14104727 (rev date 2021-06-29) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Equipment_Rebalance_Ranged_Meta_Proposal.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Phosani%27s_Nightmare?oldid=14200664 | rev 14200664 (rev date 2021-11-01) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Phosani_s_Nightmare.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:PJ_Timer_Beta_%26_Chat_Changes?oldid=14213423 | rev 14213423 (rev date 2021-12-01) | `docs/minigames/inferno/sources/newsposts/wiki_Update_PJ_Timer_Beta_Chat_Changes.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Leagues_III_-_Shattered_Relics_Launch?oldid=14253596 | rev 14253596 (rev date 2022-03-15) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Leagues_III_Shattered_Relics_Launch.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Poll_76_Game_Improvements_Blog?oldid=14246413 | rev 14246413 (rev date 2022-02-14) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Poll_76_Game_Improvements_Blog.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Poll_76_Improvements?oldid=14354393 | rev 14354393 (rev date 2022-12-22) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Poll_76_Improvements.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Combat_Achievements_Expansion:_Rewards?oldid=14340295 | rev 14340295 (rev date 2022-11-02) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Combat_Achievements_Expansion_Rewards.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Poll_80_%26_Shooting_Stars_Changes?oldid=14858682 | rev 14858682 (rev date 2025-03-06) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Poll_80_Shooting_Stars_Changes.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Deadman:_Armageddon_%26_While_Guthix_Sleeps_Tweaks?oldid=14714601 | rev 14714601 (rev date 2024-08-07) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Deadman_Armageddon_While_Guthix_Sleeps_Tweaks.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Summer_Sweep-Up:_Combat_%26_Loot?oldid=14929959 | rev 14929959 (rev date 2025-06-30) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Summer_Sweep_Up_Combat_Loot.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Summer_Sweep_Up:_Combat?oldid=14938870 | rev 14938870 (rev date 2025-07-16) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Summer_Sweep_Up_Combat.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Getting_Around?oldid=15104173 | rev 15104173 (rev date 2026-01-12) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Getting_Around.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Remaining_Getting_Around_Changes?oldid=15163005 | rev 15163005 (rev date 2026-04-01) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Remaining_Getting_Around_Changes.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:RuneFest:_Old_School_Reveals?oldid=11086767 | rev 11086767 (rev date 2019-12-14) | `docs/minigames/inferno/sources/newsposts/wiki_Update_RuneFest_Old_School_Reveals.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Dev_Blog:_Slayer_Expansion_v2?oldid=13981833 | rev 13981833 (rev date 2020-12-22) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Dev_Blog_Slayer_Expansion_v2.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Dev_Blog:_Slayer_Expansion?oldid=13981894 | rev 13981894 (rev date 2020-12-23) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Dev_Blog_Slayer_Expansion.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Spellbook_Rework,_Wise_Old_Man_and,_Increased_Zoom?oldid=11842847 | rev 11842847 (rev date 2020-03-16) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Spellbook_Rework_Wise_Old_Man_and_Increased_Zoom.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Chambers_of_Xeric,_Kebos,_and_Collection_Log_Changes?oldid=14858684 | rev 14858684 (rev date 2025-03-06) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Chambers_of_Xeric_Kebos_and_Collection_Log_Changes.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Another_Message_About_Unofficial_Clients?oldid=14249821 | rev 14249821 (rev date 2022-02-25) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Another_Message_About_Unofficial_Clients.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Tombs_of_Amascut:_Rewards_Beta?oldid=14145505 | rev 14145505 (rev date 2021-08-12) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Tombs_of_Amascut_Rewards_Beta.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Group_Ironman?oldid=14917791 | rev 14917791 (rev date 2025-06-10) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Group_Ironman.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Group_Ironman:_Post_Launch_Improvements?oldid=14217873 | rev 14217873 (rev date 2021-12-17) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Group_Ironman_Post_Launch_Improvements.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Group_Ironman_Improvements?oldid=14250680 | rev 14250680 (rev date 2022-03-02) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Group_Ironman_Improvements.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Third_Party_Client_Guidelines?oldid=15262613 | rev 15262613 (rev date 2026-07-13) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Third_Party_Client_Guidelines.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:The_Last_of_Poll_78?oldid=14402353 | rev 14402353 (rev date 2023-04-24) | `docs/minigames/inferno/sources/newsposts/wiki_Update_The_Last_of_Poll_78.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Farming_%26_Autocast_QoL_Improvements?oldid=14885371 | rev 14885371 (rev date 2025-04-16) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Farming_Autocast_QoL_Improvements.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Easter_Event?oldid=14886928 | rev 14886928 (rev date 2025-04-17) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Easter_Event.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Summer_Sweep-Up_Blog_Update:_Combat_%26_Loot?oldid=14915621 | rev 14915621 (rev date 2025-06-05) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Summer_Sweep_Up_Blog_Update_Combat_Loot.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:More_Doom_Tweaks,_Poll_84,_%26_Summer_Sweep_Up_Changes?oldid=14970813 | rev 14970813 (rev date 2025-08-21) | `docs/minigames/inferno/sources/newsposts/wiki_Update_More_Doom_Tweaks_Poll_84_Summer_Sweep_Up_Changes.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Interface_Uplift?oldid=14997941 | rev 14997941 (rev date 2025-10-02) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Interface_Uplift.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Gathering_QoL,_Trouver_Changes_%26_more!?oldid=15210138 | rev 15210138 (rev date 2026-05-13) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Gathering_QoL_Trouver_Changes_more.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Bank_Tags,_Trouver_System_Rework_%26_More!?oldid=15237567 | rev 15237567 (rev date 2026-06-22) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Bank_Tags_Trouver_System_Rework_More.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Summer_Sweep_Up_-_Agility_%26_Chambers_of_Xeric_Changes?oldid=15303824 | rev 15303824 (rev date 2026-08-17) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Summer_Sweep_Up_Agility_Chambers_of_Xeric_Changes.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Volcanic_Mine_Tweaks_%26_Make_All_Preview?oldid=11842895 | rev 11842895 (rev date 2020-03-16) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Volcanic_Mine_Tweaks_Make_All_Preview.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Boss_Slayer_Master_Blog?oldid=14793524 | rev 14793524 (rev date 2024-11-05) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Boss_Slayer_Master_Blog.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Q%26A_Summary_-_13/07/23?oldid=14437669 | rev 14437669 (rev date 2023-07-21) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Q_A_Summary_13_07_23.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Fortis_Colosseum_-_First_Look_%26_Rewards?oldid=14485094 | rev 14485094 (rev date 2023-10-25) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Fortis_Colosseum_First_Look_Rewards.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Herblore_Activity_-_Varlamore:_Part_Two?oldid=14752112 | rev 14752112 (rev date 2024-09-25) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Herblore_Activity_Varlamore_Part_Two.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Leagues_V:_Raging_Echoes_-_Issues_%26_Fixes?oldid=14816589 | rev 14816589 (rev date 2024-12-03) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Leagues_V_Raging_Echoes_Issues_Fixes.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Royal_Titans_Feedback_%26_12th_Birthday_Celebrations?oldid=14850806 | rev 14850806 (rev date 2025-02-12) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Royal_Titans_Feedback_12th_Birthday_Celebrations.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Summer_Sweep_Up_Slayer_%26_More?oldid=14976248 | rev 14976248 (rev date 2025-08-29) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Summer_Sweep_Up_Slayer_More.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Doom_Combat_Achievements?oldid=14980498 | rev 14980498 (rev date 2025-09-05) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Doom_Combat_Achievements.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Poll_85_-_Bridges,_Boots,_Ropes_%26_Roots?oldid=14988437 | rev 14988437 (rev date 2025-09-18) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Poll_85_Bridges_Boots_Ropes_Roots.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |
| 2026-10-03 | https://oldschool.runescape.wiki/w/Update:Deadman:_Annihilation_Tweaks,_Shellbane_Gryphon_CAs_%26_More!?oldid=15121071 | rev 15121071 (rev date 2026-02-05) | `docs/minigames/inferno/sources/newsposts/wiki_Update_Deadman_Annihilation_Tweaks_Shellbane_Gryphon_CAs_More.wikitext` | Jagex newspost mirrored on the wiki (namespace 112), raw wikitext |

Total: 40 wiki pins + 1 MISSING, 71 newspost pins. Each file is byte-identical to the `?oldid=` rendering (the tool re-reads by oldid). `manifest.tsv` in each directory has the same rows.
Licence: the OSRS Wiki text is CC BY-NC-SA 3.0; the pinned files are raw wikitext copies for research, with the wiki's templates intact. No licence header is added to wikitext (it is not a source file);
this ledger is the attribution: source oldschool.runescape.wiki, revisions as above.

## Not pinned

About 120 of the 193 screened posts mention an Inferno word only in passing (merchandise, Deadman, Leagues, Community Showcase, Gielinor Gazette, Q&A summaries, LMS loadouts and so on)
or concern the Fight Caves only. They are not pinned and not quoted: mention only. Examples with the reason: Update:Deadman: Armageddon - First Look (Zuk points), Update:Community Spotlight: Molgoatkirby
(interview), Update:Leagues IV pages (league tasks), Update:Group Ironman Blog (duplicate of the pinned Group Ironman), Update:Tombs of Amascut Nex Rewards Beta (Zuk ball cannot be dodged: a comparison), Update:Chambers of Xeric Changes.
Pinned but mention-only for the Inferno: see the last block of the newspost section below.
Not pinned because outside the brief: TzHaar-Ket-Rak's Challenges page, Fire cape, TzTok-Jad, Fight Caves (the six Jad challenge is a separate unit; the news post about it is pinned).

## Unavailable or not stated by any pinned source

- `TzKal-Zuk/Strategies`: no such page (MISSING). The Zuk strategy is inside `Inferno/Strategies`.
- A Tokkul-by-wave table: no pinned page states one (only the 16,440 maximum, doubled by the elite Karamja diary). `Tokkul` and `Inferno` say "increasing amounts based on the wave".
- The first-attack delay of a revived monster, the mager revive's choice rule beyond "1/10", the shield's speed in tiles per tick and its first direction rule, the spawn tile list and the spawn rule
  (only "random per wave, saved per run"), Zuk's first attack tick after the cutscene, the cutscene's camera and length, the sounds and graphics ids: no wiki or newspost line states them
  (cache, Blert and plugins decide).
- The wave 69 timing of the first mager and ranger set is "after the shield has done a complete rotation" (W:Inferno_Strategies:711): no tick count.
- The Combat Achievement tier pages do not list their rows (bucket data): the twelve task pages stand in; a thirteenth Inferno task not marked `monster = TzKal-Zuk` would be missed.
- The Mod Ash tweets were not fetched; they are quoted second-hand from the wiki and carry the wiki's date. Grade them A only after the tweet is read (archive urls are on the pages).

## Disagreements between pinned sources (not settled here; quoted so the spec pass settles them)

1. Pillar collapse after wave 66: `W:Inferno:264` "deal half the player's current hp if the player is within one tile" against `W:Inferno_Strategies:514` "do 49 damage if the player is within one tile".
2. The enrage threshold wording: `W:Inferno:285` "lowered below 240", `W:Inferno:291` "below 240 Hitpoints", `W:Inferno_Strategies:761` "reaching 240 or less hitpoints", `W:TzKal_Zuk:81` "At 240 hitpoints", `W:Jal_MejJak:44` "at 20% health" (20% of 1200 = 240): "below" against "at or below" 240; a recorder or the cache decides which tick.
3. Zuk attack cadence: `W:TzKal_Zuk:21-22` and `W:Inferno_Strategies:761` say 10 then 7; `W:TzKal_Zuk:81` says "3 ticks faster" (10 to 7 agrees); `W:Inferno:285` says only "attacks speed up considerably".
4. Jad stagger on wave 68: `W:Inferno_Strategies:684` "the second and third ones will attack 3 ticks afterwards, resulting in a 9 tick cycle"; `W:JalTok_Jad:51` "the other two will have a slight delay before engaging the player so that all three can be flicked at once".
5. The poll date: `W:Inferno:371` "Mor Ul Rek and The Inferno were then polled ... on 24 March 2017" against `NP:Progress_Report_The_Inferno:21` "on 30th March, we polled the Inferno and it passed ... 84%". Not a mechanic; noted.
6. Inferno wave count: `NP:Dev_Blog_The_Inferno_v2:29` "40+ waves" (a proposal) and `NP:Q_A_Summary_13_07_23` "60 waves" (a loose remark) against 69 on the Inferno page: the table is the spec.
7. Jal-Zek is listed with "Stab" as its melee style on `W:Jal_Zek:14` and `W:JalTok_Jad:15` (Jad "Stab") but "Melee (Slash)" for Jal-ImKot and "Melee" generally on `W:Inferno` (the table: Jal-Zek "Magic, Melee"); damage-type labels are the cache's to settle.
8. Wave 1-66 starting prayer advice: `W:Inferno_Strategies:842` (range 1-33, mage 35-66) is a guide heuristic, not a mechanic.

## What this part states: every sentence or constant that gives a number

### A. From Jagex newsposts (grade A)

Notation: `NP:<name>` = `docs/minigames/inferno/sources/newsposts/wiki_Update_<name>.wikitext`. The date is the
`{{Update|date=...}}` of the post (line 1 of the file). Grade A = a Jagex statement. Posts that state a design
proposal later superseded are marked SUPERSEDED: they are history, not spec.

### Entry, the sacrificed cape, access rules

| Date | File:line | Quote |
|---|---|---|
| 2017-06-01 | NP:The_Inferno:69 | "Beyond sacrificing a Fire Cape, there are '''no requirements''' to attempt The Inferno." |
| 2017-06-01 | NP:The_Inferno:21,23 | "If you wish to enter the city of Mor Ul Rek, present your Fire Cape to a city guard." / "You will not lose your Fire Cape while doing this." (the city, not the Inferno) |
| 2017-03-17 | NP:Dev_Blog_Mor_Ul_Rek_The_Inferno:80 | "You must make a '''one-time sacrifice''' of a Fire Cape in order to convince the TzHaar you're worthy." |
| 2017-05-03 | NP:Progress_Report_The_Inferno:105 | "...having to show off a Fire cape (or Max fire cape) to be able to access the city, and in fact you must sacrifice one completely to access The Inferno." |
| 2016-01-20 | NP:Dev_Blog_The_Inferno_v2:25 (SUPERSEDED as a proposal; the one-time fee survived) | "will be made to sacrifice a fire cape in order to be granted access ... (the payment of a fire cape is a one-time fee)." |
| 2017-06-08 | NP:Pest_Control_Open_Weekend:106 | "Items dropped in the Inferno now stay on the ground for 30 minutes before despawning." |
| 2017-06-08 | NP:Pest_Control_Open_Weekend:107 | "Teleports are no longer permitted inside the Inferno; there's no real need to teleport out of there, and players were triggering it by accident." |
| 2018-03-15 | NP:QoL_Deadman_and_the_Falador_Party_Room:58 | "You can no longer log out at the start of a wave during the Inferno." |
| 2018-03-15 | NP:QoL_Deadman_and_the_Falador_Party_Room:59 | "Jal-Zek can no longer move when resurrecting another monster within the Inferno." |
| 2018-03-15 | NP:QoL_Deadman_and_the_Falador_Party_Room:56 | "Jal-ImKot can no longer deal damage during its dig animation in the Inferno." |
| 2018-03-15 | NP:QoL_Deadman_and_the_Falador_Party_Room:57 | "The Ancestral Glyph used to hide from TzKal-Zuk in the Inferno now remembers which way it will begin travelling, and will no longer change direction when logging back in." |
| 2017-06-22 | NP:Mining_Guild_Expansion:93,95 | "Players can now interact with scenery in the Inferno (dead clicks have now been removed from the northern edge of the arena)" / "Jal-Zek is now able to perform its attack animation correctly even while taking damage" |
| 2019-01-31 | NP:Chambers_of_Xeric_Kebos_and_Collection_Log_Changes:88 | "The only exception is the Inferno which will be disabled on these worlds." |
| 2019-08-15 | NP:Small_Game_Updates_and_Betas:87 | "The Inferno itself is still being adjusted to handle the line-of-sight changes, since we would prefer the update not to affect how players fight Zuk in there." |
| 2021-01-26 | NP:Equipment_Rebalancing_Changes:20 | "You'll be able to play any content within the main game, including the Inferno." (beta worlds) |
| 2021-10-06 | NP:Group_Ironman:249 | "In PvM, there are safe deaths in places like the Chambers of Xeric, the Fight Caves, the Inferno and Pest Control." |
| 2021-11-26 | NP:Group_Ironman_Post_Launch_Improvements:14 | "it's impossible to use teleport spells to escape the Inferno" |
| 2023-09-20 | NP:Poll_80_Shooting_Stars_Changes:92 | "Infernal Cape repair cost has been increased from 75,000 to 225,000 GP" |
| 2022-06-17 / 2019-10-30 | NP:Third_Party_Client_Guidelines:13; NP:Another_Message_About_Unofficial_Clients:25 | features that aid fights are prohibited "(this includes all Raids sub-bosses, Slayer bosses, Demi-bosses, and wave-based minigames, including the Fight Caves and Inferno)" (bears on the plugin sources: an Inferno helper is a third-party-client question) |

### Wave structure, spawns, pillars

| Date | File:line | Quote |
|---|---|---|
| 2017-05-03 | NP:Progress_Report_The_Inferno:66-68 | "one thing I managed to tie into the core system, was purely randomised spawns. A player shouldn't be able to determine where monsters are going to be spawned, but at the same time, these locations are saved per run! This means they cannot logout and in to get more desired spawn locations for the monsters." |
| 2017-05-03 | NP:Progress_Report_The_Inferno:56 | "the wave structure is vital for so much of the content" (no number) |
| 2016-01-20 | NP:Dev_Blog_The_Inferno_v2:29 (SUPERSEDED proposal) | "you will be faced with '''40+ waves''' of '''6 previously unseen TzHaar monsters'''" (the shipped game has 69 waves and 6 new monster kinds before Jad; the wiki table is the spec, `wiki/wiki_Inferno.wikitext`:163-275) |
| 2016-01-19 | NP:Dev_Blog_The_Inferno:7,9 (SUPERSEDED, Jad 2 inside the Fight Cave) | "Upon completing 63 waves of the Fight Cave ... you will be faced with '''22 additional waves'''" |
| 2023-03-15 | NP:The_Last_of_Poll_78:76 | speedrun categories: "Inferno categories (Standard, Melee, No-pillar)" |

### Nibblers (Jal-Nib)

| Date | File:line | Quote |
|---|---|---|
| 2025-08-20 | NP:More_Doom_Tweaks_Poll_84_Summer_Sweep_Up_Changes:41,117 | "Added 40% Water weakness to Nibblers and Inferno Bats." |
| 2025-05-30 | NP:Summer_Sweep_Up_Combat_Loot:42 | water weaknesses "Chief among these are all TzHaar creatures (including TzTok-Jad and TzKal-Zuk)" (no percentage in the post; the 40% is on each monster page's Changes box) |

### The bat (Jal-MejRah)

| Date | File:line | Quote |
|---|---|---|
| 2021-04-22 | NP:Poll_75_Game_Improvements_Blog:273,277 | "Inferno Bats are frustrating, due to them randomly draining stats, even when prayed against correctly ... they will still be able to drain run energy" / poll "Should Inferno Bats stop draining player stats when prayed against correctly" |
| 2021-06-30 | NP:Phosani_s_Nightmare:326 | "Bats in the Inferno no longer drain combat stats if you are praying Protect from Missiles." |
| 2025-04-16 | NP:Easter_Event:25 | "The Run Energy drain effect of the Jal-MejRah can now be blocked by Prayer. This was already the case for their stat-stealing effect." |
| 2025-04-09 | NP:Farming_Autocast_QoL_Improvements:104 "Ever been drained by that pesky bat in Inferno" (mention only) | |

### Ranger and the healers (Jal-Xil, Jal-MejJak)

| Date | File:line | Quote |
|---|---|---|
| 2021-05-28 | NP:Equipment_Rebalance_Ranged_Meta_Proposal:1081 | "we're going to proceed with reducing the Hitpoints of both Jal-Xil and Jal-MejJak throughout the Inferno by 5 each." |
| 2021-06-30 | NP:Phosani_s_Nightmare:292 | "HP from the the healers at Zuk and the rangers within the inferno have been reduced by 5." |
| 2021-05-28 | NP:Equipment_Rebalance_Ranged_Meta_Proposal:1040-1075 | the player's blowpipe against them (112 Ranged, Rigour, dragon darts): Jal-Xil max hit 30 -> 29, accuracy 93.73% -> 92.22%, DPS 11.716 -> 11.144, time to kill 11.095 -> 11.666; Jal-MejJak accuracy 90.09% -> 87.72%, DPS 11.262 -> 10.599, time to kill 7.104 -> 7.548 (these are the PLAYER's numbers vs the monsters, before the 5 hp cut) |

### Mager (Jal-Zek)

| Date | File:line | Quote |
|---|---|---|
| 2017-06-22 | NP:Mining_Guild_Expansion:95 | "Jal-Zek is now able to perform its attack animation correctly even while taking damage" |
| 2018-03-15 | NP:QoL_Deadman_and_the_Falador_Party_Room:59 | "Jal-Zek can no longer move when resurrecting another monster within the Inferno." |

(The revive chance 1/10 is a Jagex tweet cited on the wiki page, listed under "cited tweets" below; no newspost states it.)

### Meleer (Jal-ImKot)

| Date | File:line | Quote |
|---|---|---|
| 2018-03-15 | NP:QoL_Deadman_and_the_Falador_Party_Room:56 | "Jal-ImKot can no longer deal damage during its dig animation in the Inferno." |

### Jad (JalTok-Jad, Yt-HurKot)

No newspost states a Jad number for the Inferno. Mention-only or Fight Cave: NP:XP_Drops_Jad_Pet_Slayer (TzTok-Jad), NP:Land_of_the_Goblins:(Six Jad challenge),
NP:TzHaar_Ket_Rak_s_Challenges (Six Jad challenge). The wiki cites Mod Ash tweets for the healer heal (see below).
NP:Combat_Achievements_Poll_Blog:381 states the lore: "a number of eggs from TzTok-Jad were hatched inside the Inferno. From these eggs came the JalTok-Jads."

### TzKal-Zuk and the glyph

| Date | File:line | Quote |
|---|---|---|
| 2021-01-13 | NP:God_Wars_Instancing_and_Soul_Wars_Improvements:82 | "Zuk's shield no longer vanishes right as he dies." |
| 2024-07-24 | NP:Deadman_Armageddon_While_Guthix_Sleeps_Tweaks:65 | "The 'Attack' option on Zuk is once again visible during the introductory cut scene." (so there is an intro cutscene during which Zuk is attackable in the game's data) |
| 2026-02-04 | NP:Deadman_Annihilation_Tweaks_Shellbane_Gryphon_CAs_More:80 | "Zuk's lava base is visible once again" |
| 2026-04-01 | NP:Remaining_Getting_Around_Changes:53 | "The shield no longer clips into the arena at Inferno during the final stage." |
| 2021-12-01 | NP:PJ_Timer_Beta_Chat_Changes:138 | "We've lowered the HiScores' TzKal-Zuk Kill Count Requirement from 2 to 1." |
| 2022-01-19 | NP:Leagues_III_Shattered_Relics_Launch:326 | "Dying at the same moment as killing TzKal-Zuk now correctly subtracts 1 KC from a task." |
| 2021-04-20 | NP:Equipment_Rebalance_Ranged_Meta:208 | "TzKal-Zuk is intentionally placed out of range for the Blowpipe" (closer 2026-10-03: the quote is on line 208; line 28 says "a creature that the Blowpipe cannot reach (such as TzKal-Zuk)") |

### Rewards, the pet, Slayer

| Date | File:line | Quote |
|---|---|---|
| 2017-08-03 | NP:Inferno_slayer_task_Tournament_worlds_and_Poll_56:15 | "this task will be solely for those who have already killed TzKal-Zuk." |
| 2017-08-03 | NP:Inferno_slayer_task_Tournament_worlds_and_Poll_56:19 | "Completion of this task will yield 100,000 Slayer xp from killing TzKal-Zuk, and it offers an increased chance of the Jal-nib-rek pet." |
| 2017-08-03 | NP:Inferno_slayer_task_Tournament_worlds_and_Poll_56:21 | "Dying during the task or leaving the Inferno prior to completion will see the task wiped, as with the TzTok-Jad task." |
| 2017-06-08 | NP:Pest_Control_Open_Weekend:92-98 | first twelve to earn the Infernal cape, "No one has yet received the pet! Remember you can gamble that precious Infernal cape for an extra chance." |
| 2017-03-17 | NP:Dev_Blog_Mor_Ul_Rek_The_Inferno:17 | "The prayer bonus of the Infernal Cape now matches that of the Fire Cape." |
| 2018-02-22 | NP:Old_School_RuneScape_s_5th_Birthday:81 | "A metamorphosis option has been added to the Jal-Nib-Rek pet, allowing it to transform into the TzRek-Zuk." |
| 2022-10-21 | NP:Combat_Achievements_Expansion_Rewards:136,150 | Master tier: "Slayer tasks to kill TzTok-Jad or TzKal-Zuk are increased to 2 kills per task"; Grandmaster "3 kills per task" (the shipped values are on `wiki/wiki_Combat_Achievements_Master.wikitext`:61 and `wiki_Combat_Achievements_Grandmaster.wikitext`:50) |
| 2021-07-21 | NP:Combat_Achievements:266,279 | "When you are assigned a TzTok-Jad or TzKal-Zuk Slayer task you will have to kill two of either" / "three of either" |
| 2025-06-25 | NP:Summer_Sweep_Up_Combat:140 | "You can now show your Zuk Slayer Helmet to TzHaar-Ket-Keh to benefit from an equipped Zuk Helm within the Inferno and Fight Caves. ... you won't have an improved chance at the pet." |
| 2025-09-24 | NP:Interface_Uplift:35 | "Fixed the Slayer cape perk so players can now get back-to-back Zuk tasks as intended." |
| 2025-02-12 | NP:Royal_Titans_Feedback_12th_Birthday_Celebrations:53 | "You can no longer accidentally get repeat Jad or Zuk tasks by cancelling them through teleport." |

### Cited tweets (Jagex statements quoted on the pinned wiki pages; the tweets themselves were not fetched)

| Date | File:line | Quote |
|---|---|---|
| 2023-03-24 | `wiki/wiki_Jal_Zek.wikitext`:48 (Mod Ash) | "1/10 of it choosing that spec rather than other attacks." |
| 2020-12-30 | `wiki/wiki_Yt_HurKot.wikitext`:70 (Mod Ash) | "Its other heal code appears to do 15-24 every 4 ticks." (Inferno healer) |
| 2020-05-06 | `wiki/wiki_Yt_HurKot.wikitext`:70 (Mod Ash) | "It's a flat +5 (per NPC), running every 4 ticks." (Fight Caves healer) |
| 2023-09-27 | `wiki/wiki_TzKal_Zuk.wikitext`:17 (Mod Ash) | "Your description of 'Zuk doing 1 damage roll, with the maxhit being the avg of it's mage+range maxhit' is the correct interpretation." |
| 2022-07-10 | `wiki/wiki_TzKal_Zuk.wikitext`:76 (Mod Ash) | accuracy "rolls the average accuracy against the average defence" of the player's effective Ranged and Magic defence |

### Mention only (pinned, read, no number about the Inferno; not quoted)

The_Inferno release post carries no mechanics numbers beyond the quote above. Network_maintenance (1 June 2017: launch maintenance), March_The_Month_Ahead_2017
(release plan), Dev_Blog_Brimstone_The_Inferno and Dev_Blog_Mor_Ul_Rek_The_Inferno (location, thieving and fishing numbers of Mor Ul Rek: level 90 Thieving,
level 80 Fishing, 35,000 xp per hour, obsidian armour level 60 Defence: not Inferno mechanics), Infernal_Cape_Design_Update (2017-04-24, art only),
Devblog_38 (2016-01-11, Jad 2 first pitch), RuneFest_Old_School_Reveals (2015), Dev_Blog_Slayer_Expansion and _v2 (Fight Cave pet), XP_Drops_Jad_Pet_Slayer (Fight Cave),
TzHaar_Ket_Rak_s_Challenges, Land_of_the_Goblins, Shooting_Stars (Jad challenge special restore), Poll_76_Game_Improvements_Blog:314 and Poll_76_Improvements:24
(Fight Caves draw distance "similar to the Inferno": the Inferno already shows npcs beyond the usual draw distance),
Equipment_Rebalance_Ranged_Meta (crystal bow and blowpipe numbers; Inferno only as a worry), World_Map_Balancing_Changes:185 (tournament worlds before release),
Bank_Tags_Trouver_System_Rework_More, Gathering_QoL_Trouver_Changes_more (Infernal cape repair and locking), Boss_Slayer_Master_Blog, Doom_Combat_Achievements,
Fortis_Colosseum_First_Look_Rewards (the third wave-based minigame), Spellbook_Rework (slayer task wording), Group_Ironman_Improvements, Herblore_Activity_Varlamore_Part_Two,
Leagues_V_Raging_Echoes_Issues_Fixes, Volcanic_Mine_Tweaks_Make_All_Preview (Jal-Nib-Rek chathead), Summer_Sweep_Up_Agility_Chambers_of_Xeric_Changes
(the Jal-Nib-Rek rename note is on the pet page), Summer_Sweep_Up_Slayer_More, Summer_Sweep_Up_Blog_Update_Combat_Loot, Poll_85, Q_A_Summary_13_07_23 (a developer says
"you have to go through all 60 waves again": a loose figure, the wiki has 69), Combat_Achievements_Poll_Blog (tier text; the Inferno tasks are on the task pages).


### B. From the pinned wiki pages (grade D alone, C when two pages agree)

Notation: `W:<name>` = `docs/minigames/inferno/sources/wiki/wiki_<name>.wikitext`. Everything here is the wiki (a written guide,
tier D alone, C when two independent pages agree, higher only where the quote is a Jagex tweet or newspost cited by the wiki). The infobox
values are the wiki's reading of the cache; the cache dump owner (the inventory agent) decides them. Hitpoints, defence and magic of the
table in `W:Inferno`:33-161 are the post-2021 values (the 5-hp cut is recorded in the Changes boxes).

### Entry, system, rewards

| Quote (file:line) | What it fixes |
|---|---|
| "The player must survive 69 increasingly challenging waves, ultimately killing the level 1400 TzKal-Zuk" (W:Inferno:21) | 69 waves; Zuk combat level 1400 |
| "players must give TzHaar-Ket-Keh a fire cape. This is a one-time fee" (W:Inferno:23); "He will require a sacrifice of one fire cape" (W:TzHaar_Ket_Keh:16) | entry fee |
| "Restocking between waves is not possible and other players cannot enter, although the minigame can be paused by attempting to log out between waves. Purple sweets also cannot be used in the Inferno." (W:Inferno:23) | solo, pause, no purple sweets |
| "Tokkul is also given, with increasing amounts based on the wave they were defeated on (16,440 maximum; this is doubled if the player has completed the elite tier of the Karamja Diary)." (W:Inferno:298); W:TzKal_Zuk:91 "16,440 Tokkul (doubled ...)" | maximum token reward; the per-wave table is NOT on any pinned page (see unavailable) |
| "a 1/100 chance of receiving a Jal-Nib-Rek pet ... increased to 1/75 if the player was on a Slayer task ... give TzHaar-Ket-Keh any unwanted infernal capes for a second chance ..., having also a 1/100 chance." (W:Inferno:300); W:TzKal_Zuk:87 (`rarity=1/100`, `altrarity=1/75`); W:TzKal_Zuk:95 "~1/50 ... (~1/43 on Slayer task)"; W:Jal_Nib_Rek:64,68-70 | pet roll, cape gamble, pet messages |
| "If the player already has the pet, capes cannot be exchanged." (W:Infernal_cape:71) | cape exchange rule |
| "101,890 Slayer experience" (W:TzKal_Zuk:55); "~125,000" in a successful attempt (W:Slayer_task_TzHaar:24) | Slayer xp |
| Infernal cape bonuses: `|str = 8`, `|prayer = 2`, `|dmagic = 12`, `|drange = 12`, `|amagic = 1`, `|arange = 1` (W:Infernal_cape:92-102); repair 225,000 coins (W:Infernal_cape:77) | reward item (the cache dump decides the full row) |
| "Any remaining pillars by the end of wave 66 are automatically destroyed by the start of wave 67." (W:Inferno:165) | pillars gone before the Jads |
| "You can no longer log out at the start of a wave" (W:Inferno:387); "Players can no longer teleport out" and "Items dropped in the Inferno now stay on the ground for 30 minutes" (W:Inferno:392-393) | dated rules (all also in newsposts) |
| "Mor Ul Rek Inferno music": track `Inferno` unlocked in the Inferno (W:Inferno_music_track:22); "Unlocking this music track requires the player to show a fire cape to a TzHaar-Ket" (W:Inferno_music_track:24); tempo 96 bpm, D minor (W:Inferno_music_track:47) | music |

### The wave table (waves 1-68; 69 is Zuk)

`W:Inferno`:177-275 and `W:Inferno_Strategies`:440-519 hold the same 68 rows (checked by script 2026-10-03: equal). Totals over waves 1-68: 210 Jal-Nib,
48 Jal-MejRah, 40 Jal-Ak, 36 Jal-ImKot, 34 Jal-Xil, 33 Jal-Zek, 4 JalTok-Jad (W:Inferno_Strategies:520 states the same running totals). Six nibblers alone on waves
3, 8, 17, 34 (W:Inferno_Strategies:550); first Jal-Ak wave 4 (W:Inferno:183), first Jal-ImKot wave 9 (:189), first Jal-Xil wave 18 (:204), first Jal-Zek wave 35 (:227);
double-blob waves "7, 15, 24, 31, 41, 48, 56, 63" (W:Jal_Ak:55); mager "on every wave from 35 to 66, as well as on wave 69" (W:Inferno_Strategies:653);
wave 67 = 1 Jad, five healers (W:Inferno:270-271); wave 68 = 3 Jad, three healers each, "The Jads attack three ticks apart" (W:Inferno:272-273);
wave 69 = Zuk (W:Inferno:281).

### Pillars (Rocky support)

| Quote | File:line |
|---|---|
| "There are a total of three" ; "Each support starts with 255 health." ; "Jal-Nibs, which appear from waves 1 to 66" ; "The support will change in appearance and examine for every 25% of its health lost" ; "The supports automatically collapse after wave 66 has been cleared" | W:Rocky_support:28,30 |
| scenery ids 30284, 30285, 30286, 30287 (100%, 75%, 50%, 25%) | W:Rocky_support:23-26 |
| pillar tiles (map pins) (2257,5349), (2274,5351), (2267,5335) | W:Rocky_support:22 |
| "Each pillar has 255 hit points." | W:Inferno:165 |
| "Nibblers will ... dealing 2-4 damage every 2.4 seconds" | W:Inferno_Strategies:552 |
| collapse damage: "half the player's current hp if the player is within one tile" (W:Inferno:264) vs "do 49 damage if the player is within one tile" (W:Inferno_Strategies:514): DISAGREEMENT, unresolved | |
| "If they manage to destroy one, it cannot be used for safespotting and will damage players and monsters who are standing on adjacent tiles upon collapse." | W:Inferno:56 |

### Monsters: the infobox figures (wiki reading of the cache)

| Monster (npc id) | Level / size | Hitpoints | Att / Str / Def / Mage / Range | Attack speed | Max hit | Style | File:line |
|---|---|---|---|---|---|---|---|
| Jal-Nib (7691) | 32 / 1 | 10 | 1 / 1 / 15 / 15 / 1 | 4 | 4 | crush | W:Jal_Nib:8-25,46 |
| Jal-MejRah (7692) | 85 / 2 | 25 | 0 / 0 / 55 / 120 / 120 | 3 | 19 | ranged | W:Jal_MejRah:7-24,45 |
| Jal-Ak (7693) | 165 / 3 | 40 | 160 / 160 / 95 / 160 / 160 | 6 | 29 | ranged, magic, crush | W:Jal_Ak:7-24,45 |
| Jal-AkRek-Mej (7694) | 70 / 1 | 15 | 1 / 1 / 95 / 120 / 1 | 4 | 18 | magic | W:Jal_AkRek_Mej:7-24,44 |
| Jal-AkRek-Xil (7695) | 70 / 1 | 15 | 1 / 1 / 95 / 1 / 120 | 4 | 18 | ranged | W:Jal_AkRek_Xil:7-24,42 |
| Jal-AkRek-Ket (7696) | 70 / 1 | 15 | 120 / 120 / 95 / 1 / 1 | 4 | 18 | crush | W:Jal_AkRek_Ket:7-24,44 |
| Jal-ImKot (7697) | 240 / 4 | 75 | 210 / 290 / 120 / 120 / 220 | 4 | 49 | slash | W:Jal_ImKot:7-24,43 |
| Jal-Xil (7698, 7702) | 370 / 3 | 125 | 140 / 180 / 60 / 90 / 250 | 4 | 46 ranged, 19 melee | ranged, crush | W:Jal_Xil:7-24,43 |
| Jal-Zek (7699, 7703) | 490 / 4 | 220 | 370 / 510 / 260 / 300 / 510 | 4 | 70 magic, 52 melee | magic, stab | W:Jal_Zek:7-24,43 |
| JalTok-Jad (7700, 7704, 10623) | 900 / 5 | 350 | 750 / 1020 / 480 / 510 / 1020 | 8 (9 on wave 68) | 113 | ranged, magic, stab | W:JalTok_Jad:8-25,44 |
| Yt-HurKot Inferno (7701, 7705, 10624) | 141 / 1 | 90 | 165 / 125 / 100 / 150 / 150 | 4 | 18 | crush | W:Yt_HurKot:12-38,64 |
| TzKal-Zuk (7706) | 1400 / 7 | 1200 | 350 / 600 / 260 / 150 / 400 | 10 normal, 7 enraged | 148 | typeless magic and ranged | W:TzKal_Zuk:13-31,50 |
| Jal-MejJak (7708) | 250 / 1 | 75 | 1 / 1 / 100 / 1 / 1 | 3 | 10 | area of effect | W:Jal_MejJak:7-24,42 |
| Ancestral Glyph npc 7707, scenery 30338, tile (2270,5363) | | | | | | | W:Ancestral_Glyph:15,30,14 |

Other figures on the same pages:

- Jal-Nib "attacks have 100% accuracy no matter the player's defensive gear" (W:Jal_Nib:52). Every monster above has a 40% Water elemental weakness since 2025 (Changes boxes, e.g. W:Jal_Nib:57-59, W:JalTok_Jad:61-63, W:TzKal_Zuk:104-106).
- Jal-MejRah: "They drain 3 run energy per hit." (W:Jal_MejRah:49); "at only 4 squares" attack range (:53); stat drain by 1 blocked by Protect from Missiles (W:Inferno:68; W:Jal_MejRah:51,71); run drain blocked since 2025-04-16 (:65).
- Jal-Ak: "attacks every 6 ticks; 3 ticks before each attack, it will detect what protection prayer the player is using" (W:Jal_Ak:49); "If the player is not using any protection prayer, Jal-Ak's attack will be of a random style." (:49); the splits "attack on the same tick (unless they have to move to reach you)" (W:Inferno_Strategies:594); Mej and Xil "attack distance of 15 tiles" (W:Jal_AkRek_Mej:46; W:Jal_AkRek_Xil:44); "detects ... as soon as it is in range or 3 ticks after its last attack, and attacks with the opposite style after another three ticks" (W:Inferno:80).
- Jal-ImKot: dig "50 ticks after the start of a wave, and every 40-60 ticks thereafter ... cannot be reset within the first 30 ticks" (W:Jal_ImKot:49); "six game tick delay before the Jal-ImKot attacks after resurfacing" (:47); Inferno page says "after 30 seconds" (W:Inferno:92): 50 ticks = 30 s for the first dig, while the repeat of 40-60 ticks is 24-36 s, so the Inferno page states the first dig only).
- Jal-Zek: "1/10 chance" of choosing the revive (W:Jal_Zek:48, Mod Ash 2023-03-24); revived at half health, once each, "near the centre-east of the arena" (:48); "resume attacking 8 ticks after it uses its special ability" (:50); revives "slain monsters in the current round" (W:Inferno:116); nibblers and small blobs cannot be revived (:48); melee on the diagonal (:52).
- JalTok-Jad: "can attack with melee every 4 ticks, as opposed to its normal 8 tick attack speed" (W:JalTok_Jad:46); five healers at wave 67, three at 68 and 69 (:50-52); wave 68 Jads "9 ticks instead of the normal 8" (:51); wave 69 Jad "when TzKal-Zuk reaches 480 or less hitpoints", initially targets the glyph, healers "always spawn north of it" (:52).
- Yt-HurKot: spawn at or under 50% (W:Yt_HurKot:70); Inferno heal "15-24 hitpoints" every 4 ticks (:70, Mod Ash 2020-12-30); "Inferno healers will restore their Jad's health when in melee distance of them" (:77); "Inferno healers do not respawn" (:79).
- TzKal-Zuk: "Zuk's attack speed will increase from 10 (6.0 seconds) to 7 (4.2 seconds)" (W:Inferno_Strategies:761); max hit 148 = average of magic max 128 and ranged max 169, melee 251 never used (W:TzKal_Zuk:17 note); one damage roll between 0 and that average (:17, :76); hybrid accuracy roll (:76); sets "on a repeating 3:30 minute timer" (W:Inferno:287; closer 2026-10-03: the ledger had quoted "every 3:30 minute", which the line does not say); paused "between 600 and 480 Hitpoints" and "a one-time addition of 1:45 minutes" (W:Inferno:287); Jad "below 480" (:287); four Jal-MejJak "When TzKal-Zuk is below 240 Hitpoints" (:291); Jal-MejJak heal "15-24 hitpoints every three ticks" (W:Jal_MejJak:44), blasts "5-10 damage" (:44); "attacking 3 ticks (1.8s) faster" at enrage (W:TzKal_Zuk:81); "Any remaining minions when TzKal-Zuk is killed will automatically die with it." (:83).
- Ancestral Glyph: "5x3 area of the shield ... extends 3 to the first 3 rows along the northern wall" (W:Ancestral_Glyph:36); "600 hitpoints against other monsters" (:36); "moving on the east-west axis from one end to the other repeatedly" (:34); "pausing for approximately 5 ticks once it hits the edge" (W:Inferno_Strategies:722); the pre-enrage safespot tiles are on W:Module_Tile_markers_Inferno_Zuk_Safespots_json:4-67 (see TECHNIQUES.md row 33).
- Zuk arena: six no-line-of-sight tiles (W:Inferno:283).
- Zuk line of sight / weapon ranges: twisted bow and bowfa 10, Tumeken's shadow 8 accurate / 10 longrange, Armadyl and zaryte crossbow 8 rapid / 10 longrange, other crossbows 7 / 9, blowpipe 5 / 7 (W:Inferno_Strategies:735-739).

### Combat Achievements naming the Inferno (all `monster = TzKal-Zuk`)

| Task (id, tier) | Description (file:line) |
|---|---|
| Half-Way There (341, Elite) | "Kill a Jal-Zek within the Inferno." W:Half_Way_There:7 |
| Inferno Grandmaster (342, Grandmaster) | "Complete the Inferno 5 times." W:Inferno_Grandmaster:7 |
| The Floor Is Lava (343, Grandmaster) | "Kill Tzkal-Zuk without letting Jal-ImKot dig during any wave in the Inferno." W:The_Floor_Is_Lava:7 |
| Playing with Jads (344, Grandmaster) | "Complete wave 68 of the Inferno within 30 seconds of the first JalTok-Jad dying." W:Playing_with_Jads:7 |
| No Luck Required (345, Grandmaster) | "...without being hit by him and not taking any damage from a JalTok-Jad." W:No_Luck_Required:7,11 |
| Nibblers, Begone! (346, Master) | "Kill Tzkal-Zuk without letting a pillar fall before wave 67." W:Nibblers_Begone:7 |
| Wasn't Even Close (347, Grandmaster) | "...without letting your hitpoints fall below 50 during any wave in the Inferno." W:Wasn_t_Even_Close:7 |
| Budget Setup (348, Grandmaster) | "Kill Tzkal-Zuk without equipping a Twisted Bow within the Inferno." W:Budget_Setup:7,11 |
| Nibbler Chaser (349, Grandmaster) | "Kill Tzkal-Zuk without using any magic spells during any wave in the Inferno." W:Nibbler_Chaser:7,11 |
| Facing Jad Head-on II (350, Grandmaster) | "Kill Tzkal-Zuk without equipping any range or mage weapons before wave 69." W:Facing_Jad_Head_on_II:7,11 |
| Jad? What Are You Doing Here? (351, Grandmaster) | "Kill Tzkal-Zuk without killing the JalTok-Jad which spawns during wave 69." W:Jad_What_Are_You_Doing_Here:7,11 |
| Inferno Speed-Runner (352, Grandmaster) | "Complete the Inferno in less than 65 minutes." W:Inferno_Speed_Runner:7,11 |

Note: the tier pages `W:Combat_Achievements_Master` and `_Grandmaster` carry only reward text (Slayer 2 and 3 kills per task, W:Combat_Achievements_Master:61, W:Combat_Achievements_Grandmaster:50, the Zuk helm show to Ket-Keh :56); the task rows are bucket-rendered, so the twelve task pages are the pin. The task ids 341-352 are the wiki's (`|id =` in each page); the cache enum is the inventory agent's.


### C. Technique rows

`docs/minigames/inferno/sources/wiki/TECHNIQUES.md` has 46 rows, each with the sentence, the mechanic and the file:line (S = Inferno/Strategies, I = Inferno, and so on).
