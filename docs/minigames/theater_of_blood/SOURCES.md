# Theatre of Blood — source ledger

Every external fetch for the ToB corpus, with its date, url, revision or commit and the file it landed in. The older
fetches (17 August 2026) are described in [`sources/README.md`](sources/README.md) sections 1-5 with their revisions and
are NOT repeated here; this file records them by row and adds everything fetched on and after 2026-10-02.
Never re-fetch a pinned revision: add a new file (`...rev<id>.wikitext`) instead of replacing one.
Shape follows `docs/minigames/tombs_of_amascut/SOURCES.md`. Ranking of evidence: `COMMUNITY_SOURCES.md` (first-party Jagex > cache >
plugin/recorder code > recording dataset > written guide > video > forum).

Etiquette on every fetch below: one request at a time, 0.8-3 s apart, User-Agent `3draster-tob-research/1.0 (mrobertevers@gmail.com)`.

## 1. Fetch ledger

| Date | Source | What | Where |
|---|---|---|---|
| 2026-08-17 | oldschool.runescape.wiki | ToB wiki pages, pinned (README section 1) | `sources/wiki_*.wikitext` |
| 2026-08-17 | blert-io/plugin, blert API, OpenOSRS, tobqol, tobutilities, TobMistakeTracker, V-TOB, devqhp | plugin code and recordings (README section 3) | `sources/blert_plugin`, `blert_api`, `openosrs_theatre`, ... |
| 2026-08-17 | YouTube | 19 machine transcripts (README section 4) | `sources/transcripts/` |
| 2026-10-02 | wiki api.php `list=search`, namespace 112 (Update), 25 queries (raid, each boss, each reward, modes) | 319 candidate `Update:` titles, content-filtered for ToB keywords, 77 kept | `build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/newsposts.*` |
| 2026-10-02 | wiki `action=parse&oldid=` via `tools/toa_fetch_wiki.py` | the 77 newsposts (section 2) | `sources/newsposts/` |
| 2026-10-02 | wiki `list=search insource:/monster = .../` for the raid, its modes and 6 bosses; `list=allpages` prefix sweeps | 50 Combat Achievement titles; 48 pages fetched (section 3) | `sources/wiki_*.wikitext`, `sources/manifest.tsv`, `sources/wiki_combat_achievements_tob.tsv` |
| 2026-10-02 | wiki `Guide:` namespace (3002) prefix probe | only `Guide:Advanced Theatre of Blood` exists; already held, rev 15222073 | `sources/wiki_Guide_Advanced_Theatre_of_Blood.wikitext` |
| 2026-10-02 | github.com/blert-io/blert, shallow clone | commit `7c7750cf01b23e5d223623d7c34c5ef10ca327fd` (2026-10-01): 18 ToB guide pages, npc ids/definitions, analysis rules, attack registry | `sources/blert_repo/`, `sources/blert_guides/tob_*` |
| 2026-10-02 | https://blert.io/api/v1/challenges?type=1&limit=1 | non-empty (a live ToB challenge, `mode` 11, `scale` 3, started 2026-10-03T00:03Z): Blert still records ToB | this file |
| 2026-10-02 | same, `type=2` (CoX) and `type=3` (ToA) | `[]` both: Blert does not record CoX or ToA | this file |
| 2026-10-02 | same, `type=4` | non-empty (Colosseum, stage 102): recorded | this file |
| 2026-10-02 | https://wedoraids.com sitemap and 12 pages | public site has no mechanics; guides are Discord-only | `sources/wdr/` |
| 2026-10-02 | github.com/runelite/plugin-hub tree (GitHub trees API) + 31 plugin descriptors raw | hub names and pinned commits | `sources/PLUGIN_HUB_README.md` |
| 2026-10-02 | 14 plugin repos, shallow clones | mechanics files only (section 5) | `sources/advancedraidtracker`, `party_hits`, `nylo_death_indicators`, `nylo_stats`, `xarpus_exhumed_counter`, `theatreofbloodstats` |
| 2026-10-02 | YouTube via yt-dlp 2026.08.19 (`ytsearch8:` x 10 queries; subs and info json for 30 videos) | 30 more transcripts, 4 hit HTTP 429 and were retried after a pause | `sources/transcripts/yt_*.md`, README sections 4a |
| 2026-10-02 | https://blert.io/api/v1/challenges?limit=20&type=1&stage=ge15&mode={10,11,12}&scale=eq{1..5}&status=eq1, then /api/v1/raids/tob/{uuid}/events?stage=15 for 62 completed raids that reached Verzik (entry 2, normal 43, hard 17), one request at a time, 3 s apart, User-Agent `3draster-tob-research/1.0 (mrobertevers@gmail.com)` | Verzik spec pass: phase openings, reds, enrage, yellows, crabs, purple, pillars | `sources/blert_api/verzik_spec_pass_2026-10-02.txt` (reductions and the uuid list), `sources/blert_api/spec_pass_verzik/` (fetchers and reducers); raw streams not committed |
| 2026-10-02 | https://blert.io/api/v1/trends/bloat-downs?mode={10,11,12}&downNumber=eq{1,2} (5 requests, 3 s apart, UA `3draster-tob-research/1.0 (mrobertevers@gmail.com)`; bloat spec pass) | Entry (mode 10) has 0 recorded downs; Regular first walks 108 397; Hard first walks 17 758, second walks 17 238 | `sources/blert_api/trend_bloat_downs_mode_*.json` |
| 2026-10-02 | https://blert.io/api/v1/challenges?type=1&mode={10,11,12}&scale=eq{1..5}&status=eq1, then /raids/tob/{uuid}/events?stage=11 (bloat spec pass; one request every 3 s, UA `3draster-tob-research/1.0 (mrobertevers@gmail.com)`, never parallel) | 90 Regular rooms (scales 3-5) and 45 Hard rooms (scales 3-5) of per-tick Bloat events; Entry (mode 10): listings empty for scales 1, 2, 3, 5 and one scale-4 raid whose stage-11 stream answers 404, so Blert holds no Entry Bloat recording | raw streams stay out of git (`build/spec_state/<pass>/blert_bloat_raw/`); reduction in `sources/blert_api/bloat_rooms.csv`, `bloat_stats.txt`, `analyze_bloat.py` |
| 2026-10-03 | https://blert.io/api/v1 (`challenges?mode=10, 11, 12`, `raids/tob/<uuid>/events?stage=14`) | 18 Xarpus streams (entry 2, hard 7, regular 9), 3 s per request, UA with mrobertevers@gmail.com; summarised, raw not kept | `sources/xarpus_blert_2026-10-03.tsv` |
| 2026-10-02 | OSRS-Content `configs/all.seq` (read only) | Xarpus seq frame durations | `sources/cache_seq_xarpus.txt` |
| 2026-10-03 | https://blert.io/api/v1/challenges?type=1&mode=11 (scale eq3/eq4/eq5, status eq1) + raids/tob/<uuid>/events?stage=12, 1 request per 3 s, UA `3draster-tob-research/1.0 (mrobertevers@gmail.com)` | 14 Regular Nylocas streams (nylocas spec pass) | `build/spec_state/.../blert_nylo_raw/` (not committed), analysis in `sources/blert_api/spec_pass_nylocas/` |
| 2026-10-03 | same, mode=12 scale eq4 | 4 Hard Nylocas streams | same |
| 2026-10-03 | same, mode=10 limit 5 | 4 Entry raids listed (Blert now holds a few); one has a stage-12 stream (b093b327-de5b-462f-b155-3dc73d80e293, scale 4, Vasilias only); 664c1f8b answers 404 on stage 12 | same |
| 2026-10-03 | YouTube 9MPHZy4sjmM (Help Me RNG, Entry Mode) via tools/raid_gate/frame_count.py | full video downloaded to `build/frames/9MPHZy4sjmM/` for an Entry boss colour-switch count; NOT counted (no rows in videos.tsv) | `build/frames/` (not committed) |
| 2026-10-02 | https://blert.io/api/v1/challenges?type=1&mode=12 (scales 1-5), `mode=11` (scales 3-5), `mode=10` (scales 1-5), `&status=eq1`, then `/api/v1/raids/tob/{uuid}/events?stage=10` | Maiden spec pass: 13 Maiden event streams (Hard scales 1-5 x2-3 raids, Regular scales 3-5); Entry has one solo and one four-player completed raid, and both event streams answer HTTP 404; one request at a time, 3 s apart, `User-Agent: 3draster-tob-research/1.0 (mrobertevers@gmail.com)` | `sources/blert_api/maiden_modes_rooms.csv`, `maiden_modes_attacks.csv`, `maiden_modes_trails.csv`, `maiden_spec_pass_2026-10-02.txt`; extractor `maiden_modes_extract.py`, analyses `maiden_spec_analyses.py` (raw streams not committed) |
| 2026-10-02 | YouTube B_gjVdmfOrY (Entry Mode ToB guide), HLS via `tools/raid_gate/frame_count.py` | 8:12-9:30 downloaded and cut (60 fps, 1920x1080) and dumped at 10 fps for a frame count of the Entry Maiden's cadence; no tick overlay and the Maiden is ~60 px, so no attack onset was countable and NO row went to `videos.tsv` | `build/frames/B_gjVdmfOrY/` (not in git) |
| 2026-10-02 | https://blert.io/api/v1/challenges?type=1&mode={10,11,12}&scale=eq{1,3,4,5}&status=eq1&stage=ge13 (9 listings), the same with mode=10 and no scale (1), then /api/v1/raids/tob/<uuid>/events?stage=13; 3 s apart, UA `3draster-tob-research/1.0 (mrobertevers@gmail.com)` | 22 Sotetseg streams: Normal 14 (scale 3 x4, 4 x4, 5 x6), Hard 7, Entry 1 (the other listed Entry raid answers 404 for stage 13) | derived outputs only: `sources/blert_api/spec_pass_sotetseg/`; the raw streams (about 20 MB) stay in `build/spec_state/`, not committed |
| 2026-10-02 | OSRS-Content `configs/all.seq` (read only) | Sotetseg seq frame durations | `sources/blert_api/spec_pass_sotetseg/cache_seq_lengths.txt` |
| 2026-10-02 | YouTube o-zdMHT_rsc (Taran, *How not to tickeat at Sotetseg*, 23 s, 60 fps) via tools/raid_gate/frame_count.py | downloaded to `build/frames/o-zdMHT_rsc/`; one launch-to-hit clip only, no ten-instance count was possible, so no `videos.tsv` row | not committed |

Two pinned pages were overwritten by mistake during the 2026-10-02 fetch (redirect titles `Nylocas King` and `Theatre of Blood/Story Mode` resolved to `Nylocas Vasilias` and `Theatre of Blood/Entry Mode`) and were restored byte-for-byte from git; the newer revisions were kept beside them as `wiki_Nylocas_Vasilias.rev15315531.wikitext` (2026-08-21) and `wiki_Theatre_of_Blood_Entry_Mode.rev15333040.wikitext` (2026-09-07). The pinned 17 August revisions stay the citation targets.

## 2. Newsposts (grade-A candidates), pinned 2026-10-02

Raw wikitext, titles are `Update:<name>`; `sources/newsposts/manifest.tsv` carries the same rows. Number-bearing sentences, verbatim with file and line, are indexed in `build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/corpus.newsposts.md`.

| File | Title | Revision | As of |
|---|---|---:|---|
| `newsposts/wiki_Update_A_Night_At_The_Theatre.wikitext` | [A Night At The Theatre](https://oldschool.runescape.wiki/w/Update:A_Night_At_The_Theatre?oldid=14065714) | 14065714 | 2021-04-30 |
| `newsposts/wiki_Update_A_Night_At_The_Theatre_Rework.wikitext` | [A Night At The Theatre Rework](https://oldschool.runescape.wiki/w/Update:A_Night_At_The_Theatre_Rework?oldid=14130136) | 14130136 | 2021-07-29 |
| `newsposts/wiki_Update_A_Taste_of_Hope.wikitext` | [A Taste of Hope](https://oldschool.runescape.wiki/w/Update:A_Taste_of_Hope?oldid=11842579) | 11842579 | 2020-03-16 |
| `newsposts/wiki_Update_Araxxor.wikitext` | [Araxxor](https://oldschool.runescape.wiki/w/Update:Araxxor?oldid=14736773) | 14736773 | 2024-08-31 |
| `newsposts/wiki_Update_Arceuus_Spellbook_Beta_and_A_Kingdom_Divided_Preparation.wikitext` | [Arceuus Spellbook Beta and A Kingdom Divided Preparation](https://oldschool.runescape.wiki/w/Update:Arceuus_Spellbook_Beta_and_A_Kingdom_Divided_Preparation?oldid=14509845) | 14509845 | 2023-11-23 |
| `newsposts/wiki_Update_Automated_Plank_Make_and_In_Game_Clock.wikitext` | [Automated Plank Make and In Game Clock](https://oldschool.runescape.wiki/w/Update:Automated_Plank_Make_and_In_Game_Clock?oldid=11842591) | 11842591 | 2020-03-16 |
| `newsposts/wiki_Update_Christmas_2024.wikitext` | [Christmas 2024](https://oldschool.runescape.wiki/w/Update:Christmas_2024?oldid=14821375) | 14821375 | 2024-12-11 |
| `newsposts/wiki_Update_Collection_Log_Improvements_and_The_Last_of_Poll_74.wikitext` | [Collection Log Improvements and The Last of Poll 74](https://oldschool.runescape.wiki/w/Update:Collection_Log_Improvements_and_The_Last_of_Poll_74?oldid=14285384) | 14285384 | 2022-05-12 |
| `newsposts/wiki_Update_Combat_Achievements.wikitext` | [Combat Achievements](https://oldschool.runescape.wiki/w/Update:Combat_Achievements?oldid=14204222) | 14204222 | 2021-11-14 |
| `newsposts/wiki_Update_Combat_Achievements_Poll_Blog.wikitext` | [Combat Achievements Poll Blog](https://oldschool.runescape.wiki/w/Update:Combat_Achievements_Poll_Blog?oldid=13960821) | 13960821 | 2020-11-27 |
| `newsposts/wiki_Update_Deadman_Reborn_and_QoL_Changes.wikitext` | [Deadman Reborn and QoL Changes](https://oldschool.runescape.wiki/w/Update:Deadman_Reborn_and_QoL_Changes?oldid=14180221) | 14180221 | 2021-09-11 |
| `newsposts/wiki_Update_Deadman_Armageddon_While_Guthix_Sleeps_Tweaks.wikitext` | [Deadman: Armageddon & While Guthix Sleeps Tweaks](https://oldschool.runescape.wiki/w/Update:Deadman:_Armageddon_&_While_Guthix_Sleeps_Tweaks?oldid=14714601) | 14714601 | 2024-08-07 |
| `newsposts/wiki_Update_Dev_Blog_Theatre_of_Blood.wikitext` | [Dev Blog: Theatre of Blood](https://oldschool.runescape.wiki/w/Update:Dev_Blog:_Theatre_of_Blood?oldid=11839870) | 11839870 | 2020-03-16 |
| `newsposts/wiki_Update_Eating_in_the_Bank.wikitext` | [Eating in the Bank](https://oldschool.runescape.wiki/w/Update:Eating_in_the_Bank?oldid=11292930) | 11292930 | 2020-01-09 |
| `newsposts/wiki_Update_Equipment_Rebalance_Tier_Changes_Poll_76.wikitext` | [Equipment Rebalance Tier Changes & Poll 76](https://oldschool.runescape.wiki/w/Update:Equipment_Rebalance_Tier_Changes_&_Poll_76?oldid=14858692) | 14858692 | 2025-03-06 |
| `newsposts/wiki_Update_Equipment_Rebalance_Ranged_Meta.wikitext` | [Equipment Rebalance: Ranged Meta](https://oldschool.runescape.wiki/w/Update:Equipment_Rebalance:_Ranged_Meta?oldid=14056723) | 14056723 | 2021-04-20 |
| `newsposts/wiki_Update_Farming_Improvements_and_Rebalancing_Existing_Content.wikitext` | [Farming Improvements and Rebalancing Existing Content](https://oldschool.runescape.wiki/w/Update:Farming_Improvements_and_Rebalancing_Existing_Content?oldid=11842698) | 11842698 | 2020-03-16 |
| `newsposts/wiki_Update_Fixes_to_Slepe_and_A_Taste_of_Hope.wikitext` | [Fixes to Slepe and A Taste of Hope](https://oldschool.runescape.wiki/w/Update:Fixes_to_Slepe_and_A_Taste_of_Hope?oldid=14858695) | 14858695 | 2025-03-06 |
| `newsposts/wiki_Update_Game_Improvements_Poll_Blog.wikitext` | [Game Improvements Poll Blog](https://oldschool.runescape.wiki/w/Update:Game_Improvements_Poll_Blog?oldid=11840442) | 11840442 | 2020-03-16 |
| `newsposts/wiki_Update_Game_Jam_Charges_QoL.wikitext` | [Game Jam: Charges QoL](https://oldschool.runescape.wiki/w/Update:Game_Jam:_Charges_QoL?oldid=14859778) | 14859778 | 2025-03-08 |
| `newsposts/wiki_Update_Halloween_Fixes_Miscellaneous_Changes.wikitext` | [Halloween Fixes & Miscellaneous Changes](https://oldschool.runescape.wiki/w/Update:Halloween_Fixes_&_Miscellaneous_Changes?oldid=14850167) | 14850167 | 2025-02-10 |
| `newsposts/wiki_Update_Interface_Uplift.wikitext` | [Interface Uplift](https://oldschool.runescape.wiki/w/Update:Interface_Uplift?oldid=14997941) | 14997941 | 2025-10-02 |
| `newsposts/wiki_Update_Interface_Uplift_Round_2.wikitext` | [Interface Uplift Round 2](https://oldschool.runescape.wiki/w/Update:Interface_Uplift_Round_2?oldid=15002330) | 15002330 | 2025-10-11 |
| `newsposts/wiki_Update_Leagues_II_Trailblazer_An_Inside_Look.wikitext` | [Leagues II - Trailblazer: An Inside Look](https://oldschool.runescape.wiki/w/Update:Leagues_II_-_Trailblazer:_An_Inside_Look?oldid=13923907) | 13923907 | 2020-11-20 |
| `newsposts/wiki_Update_Making_Friends_With_My_Arm_Deadman_Autumn_Finals_and_Full_Mobile_Launch.wikitext` | [Making Friends With My Arm, Deadman Autumn Finals and Full Mobile Launch](https://oldschool.runescape.wiki/w/Update:Making_Friends_With_My_Arm,_Deadman_Autumn_Finals_and_Full_Mobile_Launch?oldid=11842753) | 11842753 | 2020-03-16 |
| `newsposts/wiki_Update_Minigame_Tweaks_Skilling_Adjustments_more.wikitext` | [Minigame Tweaks, Skilling Adjustments & more!](https://oldschool.runescape.wiki/w/Update:Minigame_Tweaks,_Skilling_Adjustments_&_more!?oldid=14667197) | 14667197 | 2024-05-23 |
| `newsposts/wiki_Update_New_Client_Features_Milestone_1.wikitext` | [New Client Features: Milestone 1](https://oldschool.runescape.wiki/w/Update:New_Client_Features:_Milestone_1?oldid=14101212) | 14101212 | 2021-06-23 |
| `newsposts/wiki_Update_New_Client_Features_Milestone_2.wikitext` | [New Client Features: Milestone 2](https://oldschool.runescape.wiki/w/Update:New_Client_Features:_Milestone_2?oldid=14138185) | 14138185 | 2021-08-04 |
| `newsposts/wiki_Update_New_Renderer_Beta_Pause.wikitext` | [New Renderer Beta Pause](https://oldschool.runescape.wiki/w/Update:New_Renderer_Beta_Pause?oldid=15012055) | 15012055 | 2025-10-30 |
| `newsposts/wiki_Update_PJ_Timer_Beta_Chat_Changes.wikitext` | [PJ Timer Beta & Chat Changes](https://oldschool.runescape.wiki/w/Update:PJ_Timer_Beta_&_Chat_Changes?oldid=14213423) | 14213423 | 2021-12-01 |
| `newsposts/wiki_Update_Pet_Insurance_Rework_More.wikitext` | [Pet Insurance Rework & More](https://oldschool.runescape.wiki/w/Update:Pet_Insurance_Rework_&_More?oldid=14983410) | 14983410 | 2025-09-10 |
| `newsposts/wiki_Update_Phosani_s_Nightmare_and_HiScores.wikitext` | [Phosani's Nightmare and HiScores](https://oldschool.runescape.wiki/w/Update:Phosani's_Nightmare_and_HiScores?oldid=11816262) | 11816262 | 2020-03-12 |
| `newsposts/wiki_Update_Phosani_s_Nightmare_Poll_Blog.wikitext` | [Phosani's Nightmare: Poll Blog](https://oldschool.runescape.wiki/w/Update:Phosani's_Nightmare:_Poll_Blog?oldid=14107203) | 14107203 | 2021-07-03 |
| `newsposts/wiki_Update_Poll_71_and_Darkmeyer_Updates.wikitext` | [Poll 71 and Darkmeyer Updates](https://oldschool.runescape.wiki/w/Update:Poll_71_and_Darkmeyer_Updates?oldid=12653102) | 12653102 | 2020-06-18 |
| `newsposts/wiki_Update_Poll_74_Game_Improvements_Blog.wikitext` | [Poll 74 Game Improvements Blog](https://oldschool.runescape.wiki/w/Update:Poll_74_Game_Improvements_Blog?oldid=13999132) | 13999132 | 2021-01-18 |
| `newsposts/wiki_Update_Poll_75_Game_Improvements_Blog.wikitext` | [Poll 75 Game Improvements Blog](https://oldschool.runescape.wiki/w/Update:Poll_75_Game_Improvements_Blog?oldid=14363427) | 14363427 | 2023-01-17 |
| `newsposts/wiki_Update_Poll_78_Near_Miss_Skilling_Improvements.wikitext` | [Poll 78: Near-Miss & Skilling Improvements](https://oldschool.runescape.wiki/w/Update:Poll_78:_Near-Miss_&_Skilling_Improvements?oldid=14364081) | 14364081 | 2023-01-19 |
| `newsposts/wiki_Update_Poll_79_Point_based_Combat_Achievements_more.wikitext` | [Poll 79: Point-based Combat Achievements & more!](https://oldschool.runescape.wiki/w/Update:Poll_79:_Point-based_Combat_Achievements_&_more!?oldid=14399464) | 14399464 | 2023-04-17 |
| `newsposts/wiki_Update_Poll_80_Shooting_Stars_Changes.wikitext` | [Poll 80 & Shooting Stars Changes](https://oldschool.runescape.wiki/w/Update:Poll_80_&_Shooting_Stars_Changes?oldid=14858682) | 14858682 | 2025-03-06 |
| `newsposts/wiki_Update_Poll_83_QoL_Changes.wikitext` | [Poll 83 QoL Changes](https://oldschool.runescape.wiki/w/Update:Poll_83_QoL_Changes?oldid=14871023) | 14871023 | 2025-03-26 |
| `newsposts/wiki_Update_Project_Rebalance_Item_Combat_Adjustments.wikitext` | [Project Rebalance - Item & Combat Adjustments](https://oldschool.runescape.wiki/w/Update:Project_Rebalance_-_Item_&_Combat_Adjustments?oldid=14643588) | 14643588 | 2024-04-24 |
| `newsposts/wiki_Update_Project_Rebalance_Overview_Scythe_Fang.wikitext` | [Project Rebalance: Overview, Scythe & Fang](https://oldschool.runescape.wiki/w/Update:Project_Rebalance:_Overview,_Scythe_&_Fang?oldid=14514115) | 14514115 | 2023-12-07 |
| `newsposts/wiki_Update_Q_A_Summary_13_07_23.wikitext` | [Q&A Summary - 13/07/23](https://oldschool.runescape.wiki/w/Update:Q&A_Summary_-_13/07/23?oldid=14437669) | 14437669 | 2023-07-21 |
| `newsposts/wiki_Update_Q_A_Summary_07_07_2022.wikitext` | [Q&A Summary 07/07/2022](https://oldschool.runescape.wiki/w/Update:Q&A_Summary_07/07/2022?oldid=14302634) | 14302634 | 2022-07-13 |
| `newsposts/wiki_Update_Q_A_Summary_19_1_2023.wikitext` | [Q&A Summary 19/1/2023](https://oldschool.runescape.wiki/w/Update:Q&A_Summary_19/1/2023?oldid=14366275) | 14366275 | 2023-01-25 |
| `newsposts/wiki_Update_Q_A_Summary_31_03_2022.wikitext` | [Q&A Summary 31/03/2022](https://oldschool.runescape.wiki/w/Update:Q&A_Summary_31/03/2022?oldid=14748125) | 14748125 | 2024-09-22 |
| `newsposts/wiki_Update_QoL_Deadman_and_the_Falador_Party_Room.wikitext` | [QoL, Deadman and the Falador Party Room](https://oldschool.runescape.wiki/w/Update:QoL,_Deadman_and_the_Falador_Party_Room?oldid=11840823) | 11840823 | 2020-03-16 |
| `newsposts/wiki_Update_Remaining_Getting_Around_Changes.wikitext` | [Remaining Getting Around Changes](https://oldschool.runescape.wiki/w/Update:Remaining_Getting_Around_Changes?oldid=15163005) | 15163005 | 2026-04-01 |
| `newsposts/wiki_Update_Revenant_Cave_Rewards_and_Troll_Quest_Announcement.wikitext` | [Revenant Cave Rewards and Troll Quest Announcement](https://oldschool.runescape.wiki/w/Update:Revenant_Cave_Rewards_and_Troll_Quest_Announcement?oldid=14285386) | 14285386 | 2022-05-12 |
| `newsposts/wiki_Update_Revenant_Cave_Rewards_Revisited_Content_Poll_Theatre_of_Blood.wikitext` | [Revenant Cave Rewards: Revisited, Content Poll & Theatre of Blood](https://oldschool.runescape.wiki/w/Update:Revenant_Cave_Rewards:_Revisited,_Content_Poll_&_Theatre_of_Blood?oldid=11839871) | 11839871 | 2020-03-16 |
| `newsposts/wiki_Update_Round_Table_Summary_30_06_2022.wikitext` | [Round Table Summary 30/06/2022](https://oldschool.runescape.wiki/w/Update:Round_Table_Summary_30/06/2022?oldid=14300706) | 14300706 | 2022-07-07 |
| `newsposts/wiki_Update_Sailing_Technical_Pre_Release.wikitext` | [Sailing Technical Pre-Release](https://oldschool.runescape.wiki/w/Update:Sailing_Technical_Pre-Release?oldid=15069278) | 15069278 | 2025-11-27 |
| `newsposts/wiki_Update_Scythe_Fang_Updates.wikitext` | [Scythe & Fang Updates](https://oldschool.runescape.wiki/w/Update:Scythe_&_Fang_Updates?oldid=14535581) | 14535581 | 2024-01-17 |
| `newsposts/wiki_Update_Small_Changes_Tweaks.wikitext` | [Small Changes & Tweaks](https://oldschool.runescape.wiki/w/Update:Small_Changes_&_Tweaks?oldid=14117617) | 14117617 | 2021-07-07 |
| `newsposts/wiki_Update_Smithing_and_Silver_Crafting_Interfaces.wikitext` | [Smithing and Silver Crafting Interfaces](https://oldschool.runescape.wiki/w/Update:Smithing_and_Silver_Crafting_Interfaces?oldid=10082161) | 10082161 | 2019-08-09 |
| `newsposts/wiki_Update_Stronghold_of_Security_iOS_Closed_Beta_Launch_and_Ghostly_Robes.wikitext` | [Stronghold of Security, iOS Closed Beta Launch and Ghostly Robes](https://oldschool.runescape.wiki/w/Update:Stronghold_of_Security,_iOS_Closed_Beta_Launch_and_Ghostly_Robes?oldid=14044106) | 14044106 | 2021-03-29 |
| `newsposts/wiki_Update_Summer_Special_2018.wikitext` | [Summer Special 2018](https://oldschool.runescape.wiki/w/Update:Summer_Special_2018?oldid=11840217) | 11840217 | 2020-03-16 |
| `newsposts/wiki_Update_Summer_Sweep_Up_Agility_Chambers_of_Xeric_Changes.wikitext` | [Summer Sweep Up - Agility & Chambers of Xeric Changes](https://oldschool.runescape.wiki/w/Update:Summer_Sweep_Up_-_Agility_&_Chambers_of_Xeric_Changes?oldid=15303824) | 15303824 | 2026-08-17 |
| `newsposts/wiki_Update_Summer_Sweep_Up_Combat.wikitext` | [Summer Sweep Up: Combat](https://oldschool.runescape.wiki/w/Update:Summer_Sweep_Up:_Combat?oldid=14938870) | 14938870 | 2025-07-16 |
| `newsposts/wiki_Update_Summer_Sweep_Up_2026.wikitext` | [Summer Sweep-Up 2026](https://oldschool.runescape.wiki/w/Update:Summer_Sweep-Up_2026?oldid=15272295) | 15272295 | 2026-07-22 |
| `newsposts/wiki_Update_Summer_Sweep_Up_Gear_PvM_Changes.wikitext` | [Summer Sweep-Up Gear & PvM Changes](https://oldschool.runescape.wiki/w/Update:Summer_Sweep-Up_Gear_&_PvM_Changes?oldid=15289827) | 15289827 | 2026-08-07 |
| `newsposts/wiki_Update_Teleport_Options_Tidying_Up.wikitext` | [Teleport Options & Tidying Up](https://oldschool.runescape.wiki/w/Update:Teleport_Options_&_Tidying_Up?oldid=11842862) | 11842862 | 2020-03-16 |
| `newsposts/wiki_Update_The_Blood_Moon_Rises_Tweaks_Fixes.wikitext` | [The Blood Moon Rises Tweaks & Fixes](https://oldschool.runescape.wiki/w/Update:The_Blood_Moon_Rises_Tweaks_&_Fixes?oldid=15256817) | 15256817 | 2026-07-08 |
| `newsposts/wiki_Update_The_Garden_of_Death_More.wikitext` | [The Garden of Death & More](https://oldschool.runescape.wiki/w/Update:The_Garden_of_Death_&_More?oldid=14399190) | 14399190 | 2023-04-15 |
| `newsposts/wiki_Update_The_Theatre_of_Blood.wikitext` | [The Theatre of Blood](https://oldschool.runescape.wiki/w/Update:The_Theatre_of_Blood?oldid=11839993) | 11839993 | 2020-03-16 |
| `newsposts/wiki_Update_Theatre_of_Blood_Changes_Deadman_Summer_Finals.wikitext` | [Theatre of Blood Changes & Deadman Summer Finals](https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood_Changes_&_Deadman_Summer_Finals?oldid=14858688) | 14858688 | 2025-03-06 |
| `newsposts/wiki_Update_Theatre_of_Blood_Rewards_Tournament_World_Feedback_Tweaks.wikitext` | [Theatre of Blood Rewards: Tournament World Feedback Tweaks](https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood_Rewards:_Tournament_World_Feedback_Tweaks?oldid=11840312) | 11840312 | 2020-03-16 |
| `newsposts/wiki_Update_Theatre_of_Blood_Entry_Mode_Improvements.wikitext` | [Theatre of Blood: Entry Mode Improvements](https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood:_Entry_Mode_Improvements?oldid=14388131) | 14388131 | 2023-03-14 |
| `newsposts/wiki_Update_Theatre_of_Blood_Entry_Mode_Poll_Blog.wikitext` | [Theatre of Blood: Entry Mode Poll Blog](https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood:_Entry_Mode_Poll_Blog?oldid=11842876) | 11842876 | 2020-03-16 |
| `newsposts/wiki_Update_Theatre_of_Blood_Feedback_Tweaks.wikitext` | [Theatre of Blood: Feedback Tweaks](https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood:_Feedback_Tweaks?oldid=11842874) | 11842874 | 2020-03-16 |
| `newsposts/wiki_Update_Theatre_of_Blood_New_Modes.wikitext` | [Theatre of Blood: New Modes](https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood:_New_Modes?oldid=14858659) | 14858659 | 2025-03-06 |
| `newsposts/wiki_Update_Theatre_of_Blood_Progress_Update.wikitext` | [Theatre of Blood: Progress Update](https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood:_Progress_Update?oldid=14858675) | 14858675 | 2025-03-06 |
| `newsposts/wiki_Update_Tombs_of_Amascut_Q_A_Summary_05_09_2022.wikitext` | [Tombs of Amascut Q&A Summary 05/09/2022](https://oldschool.runescape.wiki/w/Update:Tombs_of_Amascut_Q&A_Summary_05/09/2022?oldid=14321864) | 14321864 | 2022-09-07 |
| `newsposts/wiki_Update_Tournament_World_Mobile_Enhancements_and_Theatre_of_Blood_Tweaks.wikitext` | [Tournament World, Mobile Enhancements and Theatre of Blood Tweaks](https://oldschool.runescape.wiki/w/Update:Tournament_World,_Mobile_Enhancements_and_Theatre_of_Blood_Tweaks?oldid=14285389) | 14285389 | 2022-05-12 |
| `newsposts/wiki_Update_Tournament_Worlds_Theatre_of_Blood_rewards.wikitext` | [Tournament Worlds: Theatre of Blood rewards](https://oldschool.runescape.wiki/w/Update:Tournament_Worlds:_Theatre_of_Blood_rewards?oldid=11842888) | 11842888 | 2020-03-16 |
| `newsposts/wiki_Update_TzHaar_Ket_Rak_s_Challenges.wikitext` | [TzHaar-Ket-Rak's Challenges](https://oldschool.runescape.wiki/w/Update:TzHaar-Ket-Rak's_Challenges?oldid=14024533) | 14024533 | 2021-03-03 |
| `newsposts/wiki_Update_Wyrmscraig_Is_Out_Today.wikitext` | [Wyrmscraig Is Out Today!](https://oldschool.runescape.wiki/w/Update:Wyrmscraig_Is_Out_Today!?oldid=15284457) | 15284457 | 2026-07-31 |

Note: `As of` is the revision timestamp (many Update pages were last touched in a 2025-03-06 wiki-wide import).

## 3. Wiki pages added 2026-10-02

Pinned rows from `sources/manifest.tsv` (Combat Achievements, mode page, odds and ends). Held-since-August pages keep their README section 1 pins.

| File | Page | Revision | As of |
|---|---|---:|---|
| `wiki_Anticoagulants.wikitext` | [Anticoagulants](https://oldschool.runescape.wiki/w/Anticoagulants?oldid=15114215) | 15114215 | 2026-01-27 |
| `wiki_Can_You_Dance.wikitext` | [Can You Dance?](https://oldschool.runescape.wiki/w/Can_You_Dance??oldid=14771434) | 14771434 | 2024-10-13 |
| `wiki_Nylocas_On_the_Rocks.wikitext` | [Nylocas, On the Rocks](https://oldschool.runescape.wiki/w/Nylocas,_On_the_Rocks?oldid=14838738) | 14838738 | 2025-01-18 |
| `wiki_Harder_Mode_III.wikitext` | [Harder Mode III](https://oldschool.runescape.wiki/w/Harder_Mode_III?oldid=15299264) | 15299264 | 2026-08-14 |
| `wiki_Back_in_My_Day.wikitext` | [Back in My Day...](https://oldschool.runescape.wiki/w/Back_in_My_Day...?oldid=15299969) | 15299969 | 2026-08-14 |
| `wiki_No_Pillar.wikitext` | [No-Pillar](https://oldschool.runescape.wiki/w/No-Pillar?oldid=14771483) | 14771483 | 2024-10-13 |
| `wiki_Pack_Like_a_Yak.wikitext` | [Pack Like a Yak](https://oldschool.runescape.wiki/w/Pack_Like_a_Yak?oldid=14976476) | 14976476 | 2025-08-29 |
| `wiki_Hard_Mode_Completed_It.wikitext` | [Hard Mode? Completed It](https://oldschool.runescape.wiki/w/Hard_Mode?_Completed_It?oldid=14771520) | 14771520 | 2024-10-13 |
| `wiki_Harder_Mode_II.wikitext` | [Harder Mode II](https://oldschool.runescape.wiki/w/Harder_Mode_II?oldid=14771523) | 14771523 | 2024-10-13 |
| `wiki_Just_To_Be_Safe.wikitext` | [Just To Be Safe](https://oldschool.runescape.wiki/w/Just_To_Be_Safe?oldid=15160167) | 15160167 | 2026-03-28 |
| `wiki_A_Timely_Snack.wikitext` | [A Timely Snack](https://oldschool.runescape.wiki/w/A_Timely_Snack?oldid=15097963) | 15097963 | 2025-12-31 |
| `wiki_Morytania_Only.wikitext` | [Morytania Only](https://oldschool.runescape.wiki/w/Morytania_Only?oldid=15355214) | 15355214 | 2026-09-24 |
| `wiki_Attack_Step_Wait.wikitext` | [Attack, Step, Wait](https://oldschool.runescape.wiki/w/Attack,_Step,_Wait?oldid=14771538) | 14771538 | 2024-10-13 |
| `wiki_Chally_Time.wikitext` | [Chally Time](https://oldschool.runescape.wiki/w/Chally_Time?oldid=15136869) | 15136869 | 2026-02-26 |
| `wiki_Don_t_Look_at_Me.wikitext` | [Don't Look at Me!](https://oldschool.runescape.wiki/w/Don't_Look_at_Me!?oldid=15141287) | 15141287 | 2026-03-02 |
| `wiki_Nylo_Sniper.wikitext` | [Nylo Sniper](https://oldschool.runescape.wiki/w/Nylo_Sniper?oldid=15355289) | 15355289 | 2026-09-24 |
| `wiki_Harder_Mode_I.wikitext` | [Harder Mode I](https://oldschool.runescape.wiki/w/Harder_Mode_I?oldid=14833807) | 14833807 | 2025-01-09 |
| `wiki_Appropriate_Tools.wikitext` | [Appropriate Tools](https://oldschool.runescape.wiki/w/Appropriate_Tools?oldid=14771582) | 14771582 | 2024-10-13 |
| `wiki_Pass_It_On.wikitext` | [Pass It On](https://oldschool.runescape.wiki/w/Pass_It_On?oldid=15211460) | 15211460 | 2026-05-16 |
| `wiki_Can_t_Drain_This.wikitext` | [Can't Drain This](https://oldschool.runescape.wiki/w/Can't_Drain_This?oldid=15339732) | 15339732 | 2026-09-10 |
| `wiki_Theatre_4_Scale_Speed_Chaser.wikitext` | [Theatre (4-Scale) Speed-Chaser](https://oldschool.runescape.wiki/w/Theatre_(4-Scale)_Speed-Chaser?oldid=15275663) | 15275663 | 2026-07-26 |
| `wiki_Theatre_Trio_Speed_Chaser.wikitext` | [Theatre (Trio) Speed-Chaser](https://oldschool.runescape.wiki/w/Theatre_(Trio)_Speed-Chaser?oldid=15166525) | 15166525 | 2026-04-06 |
| `wiki_Royal_Affairs.wikitext` | [Royal Affairs](https://oldschool.runescape.wiki/w/Royal_Affairs?oldid=15123807) | 15123807 | 2026-02-08 |
| `wiki_Theatre_5_Scale_Speed_Chaser.wikitext` | [Theatre (5-Scale) Speed-Chaser](https://oldschool.runescape.wiki/w/Theatre_(5-Scale)_Speed-Chaser?oldid=15275662) | 15275662 | 2026-07-26 |
| `wiki_Pop_It.wikitext` | [Pop It](https://oldschool.runescape.wiki/w/Pop_It?oldid=14939861) | 14939861 | 2025-07-18 |
| `wiki_Theatre_Duo_Speed_Runner.wikitext` | [Theatre (Duo) Speed-Runner](https://oldschool.runescape.wiki/w/Theatre_(Duo)_Speed-Runner?oldid=15166528) | 15166528 | 2026-04-06 |
| `wiki_They_Won_t_Expect_This.wikitext` | [They Won't Expect This](https://oldschool.runescape.wiki/w/They_Won't_Expect_This?oldid=15344217) | 15344217 | 2026-09-15 |
| `wiki_Theatre_5_Scale_Speed_Runner.wikitext` | [Theatre (5-Scale) Speed-Runner](https://oldschool.runescape.wiki/w/Theatre_(5-Scale)_Speed-Runner?oldid=15275667) | 15275667 | 2026-07-26 |
| `wiki_Theatre_4_Scale_Speed_Runner.wikitext` | [Theatre (4-Scale) Speed-Runner](https://oldschool.runescape.wiki/w/Theatre_(4-Scale)_Speed-Runner?oldid=15275668) | 15275668 | 2026-07-26 |
| `wiki_Stop_Right_There.wikitext` | [Stop Right There!](https://oldschool.runescape.wiki/w/Stop_Right_There!?oldid=14957417) | 14957417 | 2025-08-03 |
| `wiki_Theatre_of_Blood_SM_Speed_Chaser.wikitext` | [Theatre of Blood: SM Speed-Chaser](https://oldschool.runescape.wiki/w/Theatre_of_Blood:_SM_Speed-Chaser?oldid=15107779) | 15107779 | 2026-01-17 |
| `wiki_Theatre_HM_4_Scale_Speed_Runner.wikitext` | [Theatre: HM (4-Scale) Speed-Runner](https://oldschool.runescape.wiki/w/Theatre:_HM_(4-Scale)_Speed-Runner?oldid=15275669) | 15275669 | 2026-07-26 |
| `wiki_Theatre_Trio_Speed_Runner.wikitext` | [Theatre (Trio) Speed-Runner](https://oldschool.runescape.wiki/w/Theatre_(Trio)_Speed-Runner?oldid=15166527) | 15166527 | 2026-04-06 |
| `wiki_Personal_Space.wikitext` | [Personal Space](https://oldschool.runescape.wiki/w/Personal_Space?oldid=14822535) | 14822535 | 2024-12-13 |
| `wiki_Theatre_HM_5_Scale_Speed_Runner.wikitext` | [Theatre: HM (5-Scale) Speed-Runner](https://oldschool.runescape.wiki/w/Theatre:_HM_(5-Scale)_Speed-Runner?oldid=15275670) | 15275670 | 2026-07-26 |
| `wiki_Theatre_HM_Trio_Speed_Runner.wikitext` | [Theatre: HM (Trio) Speed-Runner](https://oldschool.runescape.wiki/w/Theatre:_HM_(Trio)_Speed-Runner?oldid=14784727) | 14784727 | 2024-10-22 |
| `wiki_Theatre_of_Blood_HM_Grandmaster.wikitext` | [Theatre of Blood: HM Grandmaster](https://oldschool.runescape.wiki/w/Theatre_of_Blood:_HM_Grandmaster?oldid=14784722) | 14784722 | 2024-10-22 |
| `wiki_Two_Down.wikitext` | [Two-Down](https://oldschool.runescape.wiki/w/Two-Down?oldid=14771548) | 14771548 | 2024-10-13 |
| `wiki_Theatre_of_Blood_SM_Adept.wikitext` | [Theatre of Blood: SM Adept](https://oldschool.runescape.wiki/w/Theatre_of_Blood:_SM_Adept?oldid=14790739) | 14790739 | 2024-10-29 |
| `wiki_Team_Work_Makes_the_Dream_Work.wikitext` | [Team Work Makes the Dream Work](https://oldschool.runescape.wiki/w/Team_Work_Makes_the_Dream_Work?oldid=14771573) | 14771573 | 2024-10-13 |
| `wiki_Captain_Tobias.wikitext` | [Captain Tobias](https://oldschool.runescape.wiki/w/Captain_Tobias?oldid=15315296) | 15315296 | 2026-08-21 |
| `wiki_Nylocas_Vasilias.rev15315531.wikitext` | [Nylocas Vasilias](https://oldschool.runescape.wiki/w/Nylocas_Vasilias?oldid=15315531) | 15315531 | 2026-08-21 |
| `wiki_Verzik_Vitur_Patient_Record.wikitext` | [Verzik Vitur - Patient Record](https://oldschool.runescape.wiki/w/Verzik_Vitur_-_Patient_Record?oldid=15312455) | 15312455 | 2026-08-20 |
| `wiki_Monumental_chest.wikitext` | [Monumental chest](https://oldschool.runescape.wiki/w/Monumental_chest?oldid=15354643) | 15354643 | 2026-09-23 |
| `wiki_Theatre_of_Blood_Entry_Mode.rev15333040.wikitext` | [Theatre of Blood/Entry Mode](https://oldschool.runescape.wiki/w/Theatre_of_Blood/Entry_Mode?oldid=15333040) | 15333040 | 2026-09-07 |
| `wiki_Nylocas_A_Night_at_the_Theatre.wikitext` | [Nylocas (A Night at the Theatre)](https://oldschool.runescape.wiki/w/Nylocas_(A_Night_at_the_Theatre)?oldid=15196906) | 15196906 | 2026-04-25 |
| `wiki_Verzik_s_Defeat.wikitext` | [Verzik's Defeat](https://oldschool.runescape.wiki/w/Verzik's_Defeat?oldid=15353877) | 15353877 | 2026-09-23 |
| `wiki_Theatre_of_Blood_Rewards_Testing.wikitext` | [Theatre of Blood Rewards Testing](https://oldschool.runescape.wiki/w/Theatre_of_Blood_Rewards_Testing?oldid=15058594) | 15058594 | 2025-11-23 |

`Theatre of Blood/Strategies` has one subpage only (`/Nylocas`); there is no `/Strategies/<room>` for the other rooms (`allpages` prefix `Theatre of Blood/` returned Entry Mode, Hard Mode, Records, Story Mode, Strategies, Strategies/Nylocas). The wiki's boss pages carry the strategy text for the other rooms.

## 4. Blert

* Clone: https://github.com/blert-io/blert, commit `7c7750cf01b23e5d223623d7c34c5ef10ca327fd`, committer date 2026-10-01. Contents in `sources/blert_repo/README.md`.
* Guides in the repo for ToB: Bloat (humid), Nylocas (mechanics, trio, 4s ranger/mage/melee/melee-freeze), HMT how-to, plugins. No Maiden, Sotetseg, Xarpus or Verzik guide exists upstream at this commit.
* The plugin copy in `sources/blert_plugin/` is from 17 August (blert-io/plugin); the hub now pins `0c27d7294db0853caa5295b6771f211e353a8e36`. Not re-fetched.
* API probe, 2026-10-02: `type=1` ToB returns live data; `type=2` CoX and `type=3` ToA return `[]`.

## 5. RuneLite plugin hub

See `sources/PLUGIN_HUB_README.md` for repo, hub commit, cloned commit and files per plugin. Plugins whose clone HEAD is newer than the hub pin: AdvancedRaidTracker (3cbedc99 vs d66a7075), nylo-stats (5412bad8 vs 8ffba1e2), nyloer (c619122c vs 9b400ec7; not copied).

## 6. We Do Raids

`sources/wdr/` (12 pages, fetched 2026-10-02). No mechanics: "Guides for most bosses can be found on the discord in the channels #room-resources, #olm-resources and the various TOB channels." The Discord is not reachable here.

## 7. Video transcripts

Held list and the 2026-10-02 additions are in `sources/README.md` sections 4 and 4a. Converter: `tools/raid_gate/vtt_to_md.py`. Frame counting of sections: `tools/raid_gate/frame_count.py`.

## 8. Reference servers (not fetched; already in the tree or the research log)

Not authoritative; every use is cross-checked against cache or wiki (`docs/RAID_ORCHESTRATOR.md` section 3, step 5).

| Server | Where | Used for |
|---|---|---|
| Zenyte (`com.zenyte.game.content.theatreofbloodv`-family package; 98 ToB files) | `/Users/matthewevers/Documents/git_repos/ZenyteLikeServer` (owner's tree, outside this worktree; quotes in `docs/TOB_RESEARCH.md`, mirrors Skryllzz/SSLCode, matthewl99/Server-RSPS, Rims-Naps/Zyrox-Server) | ids and shapes; its flat pillar damage and slope were wrong (TOB_RESEARCH M7, M-rows around line 926-1060) |
| RSPS-NEAR-REALITY (`[nr]`) | `docs/minigames/cox/sources/nr`, `docs/minigames/tombs_of_amascut/sources` section 3; there is no `nr` copy for ToB | Zenyte-derived content; ToB coverage is not assessed here |
| Kronos | mentioned in the research log only | none for ToB |
No new server code was fetched in this pass.

## 9. Not used, or unreachable

* Discord (WDR, GM30, Max Eff Moneys, Flash's Hideout): not reachable.
* runescape.com original newsposts: HTTP 403 to non-browser clients (COMMUNITY_SOURCES.md); the wiki's `Update:` copies stand in.
* Fandom mirrors, RSPS wikis, aggregator blogs: see README section 5.
