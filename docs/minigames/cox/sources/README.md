# Chambers of Xeric — source corpus index (spec pass, 2026-10-02)

Everything under this folder is evidence, not specification. Ranking (docs/minigames/theater_of_blood/COMMUNITY_SOURCES.md):
first-party Jagex statement > the game's own cache > code a competitive player relies on > a dataset built from
recordings > a written guide > a video > a forum post. Every fetch (date, url, revision or commit, file) is
recorded in [`../SOURCES.md`](../SOURCES.md). Spec workers quote these files on disk, never a live page.

## Contents

| Path | What | Rank |
|---|---|---|
| `newsposts/` | 137 `Update:` pages (Jagex newsposts as kept by the wiki), raw wikitext, `manifest.tsv` with revids. Numbers extracted into `build/spec_state/matthew-mbp-m4-raid-b1-spec-cox/corpus.newsposts.md` | first-party |
| `wiki_*.wikitext`, `manifest.tsv` | 79 wiki pages (raid, /Strategies, CM, every boss and minion, CA tasks, Ancient chest), pinned revisions | written guide (infobox numbers are cited upstream) |
| `wiki_combat_achievements_cox.tsv` | the 40 CA tasks whose `monster` is Chambers of Xeric or its CM | wiki |
| `dps_calc_monsters_cox.json` | weirdgloop osrs-dps-calc `monsters.json` rows for 25 CoX-related npc rows (speed in ticks, size, hp, stats) at commit 89c3e25b3 | dataset (base stats, unscaled) |
| `blert_repo/` | Blert repo at 7c7750cf0: no CoX guide, CoX recorder is "Coming Soon" | n/a |
| `openosrs_coxhelper/`, `cox_qol/`, `cox_analytics/`, `cox_assistant/`, `tekton_reset_tracker/`, `harrisun_mystic_ui/`, `lizardman_shaman_minion_alert/`, `crab_stun_timers/`, `de0/`, `de0_pluginhub_48a8c0a/` | RuneLite plugin code, see [`PLUGIN_HUB_README.md`](PLUGIN_HUB_README.md) | code players rely on |
| `nr/room_coords.txt` | Near Reality room coordinates (held before this pass) | reference server |
| `wdr/` | We Do Raids public pages; no CoX mechanics (see its README) | n/a |
| `transcripts/` | 45 YouTube auto-caption transcripts (this pass); plus `transcript_edb0ys_cox.md`, `transcript_tubby_solo_cox.md` here and `../synq_transcript.md` | video |

## Video guides (machine transcripts)

Downloaded 2026-10-02 with `yt-dlp --skip-download --write-auto-subs --sub-langs en-orig --sub-format vtt --write-info-json`
(6 s apart; a first attempt that also asked for the translated `en` track hit HTTP 429 and was abandoned), converted by
`tools/raid_gate/vtt_to_md.py`. Auto-captions mangle numbers and npc names; cross-check before use.

**Overlay and timestamp column.** Nobody has watched these videos for this table, and no frame has been counted.
The notes say only what the title, description, chapter list or transcript state. "Overlay not frame-checked" means the
video mentions a plugin or tile indicator and the spec worker must run `tools/raid_gate/frame_count.py` before trusting any
tick count read off it. Every short per-room video is wholly about that room, so its attack timestamps are the
transcript's own `[mm:ss]` paragraphs; the long videos carry chapter lists, reproduced below for Olm and the whole-raid rows.
The only video found whose title names a tick counter is `iQd1ku7CP80`, and it has no captions.

Already held before this pass (kept, not re-fetched): Synq `klhBxOH8reQ` (3:54:55, 99 chapters, `../synq_transcript.md`),
TheEdB0ys `uM2VZicSSZM` (1:35:22, `transcript_edb0ys_cox.md`), Tubby `595L4MEheXc` (51:49, `transcript_tubby_solo_cox.md`).

| Room | File | Channel | Video | Length | Uploaded | Notes |
|---|---|---|---|---|---|---|
| Olm | `yt_49C4mqEzUAg.md` | Kaoz OSRS | [The Only Solo Olm Guide You'll Need In OSRS (3:0, 4:1)](https://www.youtube.com/watch?v=49C4mqEzUAg) | 23:59 | 2024-12-23 | description mentions plugin; not frame-checked. Chapters: 0:00 Intro; 1:24 Quests, Levels, Gear; 2:48 Plugins, Getting There; 3:26 Olm "Phases" & Auto Attacks; 5:12 Special Attacks & More; 6:41 1. Supplies; 7:20 2. Ground Markers; 7:48 3. Head Movement; 8:14 4. Melee Hand Setup; 8:35 5. 3:0 Mage Hand; 9:39 6. Mage Hand Splashing; 10:43 7. Melee Hand; 11:31 8. Automatic 4:1; 12:46 9. 3:1 Once, Then 4:1; 14:00 10. Special Attack 4:1; 15:31 11. Auto Into Special 4:1; 16:25 12. Fixing & ALL Examples; 17:36 13. Flame Wall Skipping; 19:02 14. Phase Transitions; 19:40 15. Phase 3 Setup For 4:1; 20:43 16. P3 Head Running; 22:23 17. Loot & End; 23:01 Sponsors & Outro |
| Olm | `yt_cRsOeK9U8U0.md` | SoupRS | [How to 4:1 at Olm (OSRS Raids)](https://www.youtube.com/watch?v=cRsOeK9U8U0) | 15:23 | 2019-10-20 | chapters give the Olm attack cycle: Auto Attacks, Crystal Burst, Lightning, Acid Flame Wall. Chapters: 0:00 Intro; 5:12 Mage Hand; 5:39 Mei Chen; 6:22 Second Auto Attack; 7:01 Empty Event; 8:07 Auto Attacks; 9:12 Crystal Burst; 10:01 Lightning; 11:34 Acid Flame Wall; 13:26 Outro |
| Olm | (none, no captions) | king kuang | [Solo Olm With Tick Counter and True Tile](https://www.youtube.com/watch?v=iQd1ku7CP80) | 10:13 | 2021-11-18 | title names a tick counter and true-tile overlay (the only video found that advertises one); NO captions |
| Olm | `yt_JqIWfP335bE.md` | Molgoatkirby | [Solo CoX Olm Counting Guide (never get teleports)](https://www.youtube.com/watch?v=JqIWfP335bE) | 23:08 | 2021-05-31 | counting guide (Olm head/hand cycle by counted ticks, 'Killing on 7/8/9/10'); overlay not frame-checked. Chapters: 0:00 Intro; 0:37 Overview; 2:46 Clenching; 7:36 Ghost Clinching; 11:33 Example Kill; 13:51 Special Attacks; 15:09 How I start; 16:04 Transition; 16:33 Tips; 18:42 Killing on 7; 19:03 Killing on 8; 19:53 Killing on 9; 20:12 Killing on 10; 20:28 Attacking behind Olm; 21:37 Final tip |
| Olm | `yt_sACYyrEdD7w.md` | The Academy | Premier OSRS Guides | [The Great Olm Team Guide - Beginner Tips and Skipping Special Attacks](https://www.youtube.com/watch?v=sACYyrEdD7w) | 34:00 | 2020-03-18 | 33 chapters; Olm special attacks. Chapters: 0:00 Intro; 0:54 Olm Overview; 1:08 Info; 3:16 Room Layout; 5:09 Attacks; 5:30 "Auto" Attacks; 5:45 Basic Attacks; 6:15 Prayer Orbs; 6:55 Phase-Specific Attacks; 10:57 Special Attacks; 11:17 Crystal Burst; 11:40 Lightning; 12:06 Teleport; 13:36 AutoHeal; 14:05 Attack Cycle; 14:47 Roles; 15:11 Runner; 17:36 Mager; 18:02 Meleer; 18:24 Flow of the Fight; 19:47 Roles; 19:53 Runner; 20:32 Mager; 21:10 Meleer; 22:10 Beginning Phases; 23:09 Final Phase; 24:30 Head Phase; 26:05 Skipping Specials; 28:30 Mage Set; 29:30 Melee Set; 31:26 Skipping Tip; 32:10 Tips; 33:24 Outro |
| Olm | `yt_QmfylzVqKm8.md` | WildMudkip | [Solo Olm 4:1 Guide (and more tips)](https://www.youtube.com/watch?v=QmfylzVqKm8) | 19:30 | 2023-08-23 | chapter 'Tile indicators Runelite plugin' 5:11 and 'Attack on the correct tick' 5:31; overlay not frame-checked. Chapters: 0:00 Intro; 0:56 Team raids; 2:17 Team raids 3:1 melee skipping; 3:39 Team raids 4:1 melee skipping; 5:11 Tile indicators Runelite plugin; 5:31 Attack on the correct tick; 7:37 Olm's attack cycle; 8:58 Specials while getting into 4:1; 10:15 Getting into 4:1 summary; 10:34 Hitting zeros; 11:15 Phase specific autos; 11:38 Falling crystals; 12:11 Acid trail; 12:33 Flame wall; 16:04 1:0; 17:54 Final Tips (vile vigour, replay buffer) |
| Olm | `yt_K2PYHVMoQP8.md` | AsukaYen OSRS | [Olm Attack Patterns - OSRS Quick Tips in 3 Minutes or Less](https://www.youtube.com/watch?v=K2PYHVMoQP8) | 2:46 | 2020-06-10 | 3-minute attack-pattern summary; 5 chapters. Chapters: 0:00 <Untitled Chapter 1>; 1:10 LIGHTNING; 1:15 EMPTY EVENT; 1:19 TELEPORTS; 1:22 NORMAL ATTACK 1 |
| Olm | `yt_iiNQAiEfibk.md` | Noodely | [The Easiest Way To Learn SOLO Olm (DPS Progression Ladder)](https://www.youtube.com/watch?v=iiNQAiEfibk) | 29:53 | 2026-09-01 | chapter 'Plugins' (see chapters); overlay not frame-checked. Chapters: 0:00 Intro; 0:46 DPS Ladder; 1:08 Plugins; 2:49 Gear; 5:28 Starting Olm; 7:01 2-0 Mage Hand; 9:51 1-0 Melee Hand; 12:00 Transition; 12:25 2-0 & 3-0 Mage Hand; 14:18 3-1 Melee Hand; 17:20 Final transition; 17:59 4-1 Melee Hand; 23:03 3-0 Mage Hand; 24:32 Starting Head Phase; 25:08 1-0 Head Phase; 26:41 4-1 Head Phase |
| Tekton | `yt_m49FdYfuMeY.md` | Theoatrix OSRS | [Tekton Boss Guide (Raids 1: Chambers of Xeric)](https://www.youtube.com/watch?v=m49FdYfuMeY) | 5:30 | 2019-05-10 |  |
| Tekton | `yt_WOuFM3txi0w.md` | The Academy | Premier OSRS Guides | [TEKTON - In 90 Seconds or Less](https://www.youtube.com/watch?v=WOuFM3txi0w) | 1:33 | 2023-10-30 |  |
| Tekton | `yt_Pki7GmiUE9s.md` | RuneScythed | [Solo Tekton Flinching Guide - OSRS 2023 (Solo Raids)](https://www.youtube.com/watch?v=Pki7GmiUE9s) | 2:50 | 2023-03-17 | description mentions true tile + plugin; not frame-checked |
| Tekton | (none, no captions) | Edward OSRS | [OSRS CM Tekton with Osmumten's Fang | 5 Tick walk + Vengeance](https://www.youtube.com/watch?v=BbryLzdknkA) | 2:19 | 2022-09-11 | title: 5-tick Tekton walk with Fang + Vengeance; NO captions |
| Tekton | (none, no captions) | Edward OSRS | [OSRS 4 tick CM Tekton Walk Example - Bludgeon/Inquisitor's Mace](https://www.youtube.com/watch?v=h4d4ge6tIa0) | 1:24 | 2022-03-15 | title: 4-tick CM Tekton walk; NO captions |
| Tekton | `yt_iFDVgx_HPGk.md` | BoosterOSRS | [Solo CM Tekton - Max Eff Guides OSRS](https://www.youtube.com/watch?v=iFDVgx_HPGk) | 6:02 | 2025-05-02 | CM Tekton, 11 'tick' mentions in the transcript |
| Crabs | `yt_xnAytiI0Mzo.md` | The Academy | Premier OSRS Guides | [JEWELLED CRABS - In 90 Seconds or Less](https://www.youtube.com/watch?v=xnAytiI0Mzo) | 1:31 | 2023-11-01 |  |
| Crabs | `yt_dh3inAB0dDw.md` | Steins;Gate | [Short and Simple CoX Crabs Guide OSRS 2024](https://www.youtube.com/watch?v=dh3inAB0dDw) | 2:02 | 2024-06-24 |  |
| Crabs | `yt_Inv2k3a0XdU.md` | ThePerfectG | [Bad Crabs? Clean It Up! Instant Solve Guide](https://www.youtube.com/watch?v=Inv2k3a0XdU) | 1:18 | 2021-04-26 |  |
| Ice demon | `yt_f4cH7_HSpC4.md` | The Academy | Premier OSRS Guides | [ICE DEMON - In 90 Seconds or Less](https://www.youtube.com/watch?v=f4cH7_HSpC4) | 1:31 | 2023-11-02 |  |
| Ice demon | `yt_aU4DDfFig0c.md` | Plank2g | [Beginner's Guide to Ice Demon | How to Ice Demon | Old School RuneScape | OSRS](https://www.youtube.com/watch?v=aU4DDfFig0c) | 2:37 | 2020-05-21 |  |
| Ice demon | `yt_DQaQCq4prz8.md` | Steins;Gate | [Short and Simple Ice Demon Guide OSRS 2024](https://www.youtube.com/watch?v=DQaQCq4prz8) | 2:46 | 2024-06-24 |  |
| Shamans | `yt_M6s4k0oUj7g.md` | The Academy | Premier OSRS Guides | [SHAMANS - In 90 Seconds or Less](https://www.youtube.com/watch?v=M6s4k0oUj7g) | 1:17 | 2023-11-03 |  |
| Shamans | `yt_e7x4q0j3Rzg.md` | TheEdB0ys | [OSRS Lizard Shaman Guide | How to fight Lizarman Shamans OSRS](https://www.youtube.com/watch?v=e7x4q0j3Rzg) | 14:24 | 2019-10-30 |  |
| Shamans | (none, no captions) | Klonzyon | [Shamans in Chambers of Xeric](https://www.youtube.com/watch?v=IVvIwrqAsH8) | 1:44 | 2022-08-19 | description mentions a counter; NO captions |
| Vanguards | `yt_EmK-LHnJqPE.md` | The Academy | Premier OSRS Guides | [VANGUARDS - In 90 Seconds or Less](https://www.youtube.com/watch?v=EmK-LHnJqPE) | 1:28 | 2023-11-21 |  |
| Vanguards | `yt_cQ2yX7Y-Epg.md` | Steins;Gate | [Short and Simple Vanguards Guide OSRS 2024](https://www.youtube.com/watch?v=cQ2yX7Y-Epg) | 2:57 | 2024-06-24 |  |
| Thieving | `yt_7NXG1Y2NsLs.md` | The Academy | Premier OSRS Guides | [THIEVING - In 90 Seconds or Less](https://www.youtube.com/watch?v=7NXG1Y2NsLs) | 1:03 | 2023-11-29 |  |
| Thieving | `yt_-oPCM_OxZpI.md` | Steins;Gate | [Short and Simple CoX Thieving Room Guide OSRS 2024](https://www.youtube.com/watch?v=-oPCM_OxZpI) | 1:33 | 2024-06-24 |  |
| Thieving | `yt_k4YqkvdOuGY.md` | FIFO Clan | [Cox learner guides: Thieving｜FIFO CLAN](https://www.youtube.com/watch?v=k4YqkvdOuGY) | 3:22 | 2021-02-03 |  |
| Vespula | `yt_ZALgpHA9mvw.md` | The Academy | Premier OSRS Guides | [VESPULA - In 90 Seconds or Less](https://www.youtube.com/watch?v=ZALgpHA9mvw) | 1:34 | 2023-11-29 |  |
| Vespula | `yt_eLg6BOZyZmU.md` | RuneScythed | [Solo Vespula Guide - OSRS 2023 (Solo Raids)](https://www.youtube.com/watch?v=eLg6BOZyZmU) | 3:19 | 2023-04-05 |  |
| Vespula | `yt_3HMsjFlJ7aA.md` | Fluffeh | [Vespula (No Enhance/Almost no Supplies)](https://www.youtube.com/watch?v=3HMsjFlJ7aA) | 2:22 | 2022-08-03 |  |
| Tightrope | `yt_erDL_d2QcTc.md` | The Academy | Premier OSRS Guides | [TIGHTROPE - In 90 Seconds or Less](https://www.youtube.com/watch?v=erDL_d2QcTc) | 1:03 | 2023-12-16 |  |
| Tightrope | `yt_2M2bzzc3X90.md` | Plank2g | [Tightrope: A Beginner's Guide | How to Tightrope | OSRS](https://www.youtube.com/watch?v=2M2bzzc3X90) | 1:13 | 2020-07-08 |  |
| Tightrope | `yt_aSXVrgu5RF8.md` | Nang | [Chambers of Xeric Tightrope Skip: Quick Guide](https://www.youtube.com/watch?v=aSXVrgu5RF8) | 2:07 | 2022-07-15 |  |
| Guardians | `yt_bg0mFA8jHIU.md` | The Academy | Premier OSRS Guides | [GUARDIANS - In 90 Seconds or Less](https://www.youtube.com/watch?v=bg0mFA8jHIU) | 1:08 | 2023-12-16 |  |
| Guardians | `yt_e4tup60QPXM.md` | Theoatrix OSRS | [Puzzle/Demi-Boss Rooms in Raids 1 (OSRS) - Chambers of Xeric Guide](https://www.youtube.com/watch?v=e4tup60QPXM) | 10:03 | 2019-05-21 |  |
| Guardians | (none, no captions) | Klonzyon | [Flinching Guardians in Chambers of Xeric](https://www.youtube.com/watch?v=_CUPrtw-nyU) | 28 | 2022-08-19 | title/description: true-tile flinching; NO captions |
| Vasa | `yt_e_B2m6fIbJE.md` | The Academy | Premier OSRS Guides | [VASA NISTIRIO - In 90 Seconds or Less](https://www.youtube.com/watch?v=e_B2m6fIbJE) | 1:44 | 2023-12-17 |  |
| Vasa | `yt_1__PixCCuSY.md` | Plank2g | [How to Vasa Nistirio: Beginner's Edition | Old School RuneScape | OSRS](https://www.youtube.com/watch?v=1__PixCCuSY) | 1:38 | 2020-05-08 |  |
| Vasa | `yt_7qqnFxFVZ54.md` | Charles | [OSRS Raid Guides - Vasa Nistirio](https://www.youtube.com/watch?v=7qqnFxFVZ54) | 2:01 | 2017-02-13 |  |
| Vasa | (none, no captions) | Trueleaduh | [OSRS Raids Vasa Nistirio Solo](https://www.youtube.com/watch?v=SuqdDYqHjAo) | 5:53 | 2017-02-27 | NO captions |
| Mystics | `yt_B89-ZKRiKAI.md` | The Academy | Premier OSRS Guides | [MYSTICS - In 90 Seconds or Less](https://www.youtube.com/watch?v=B89-ZKRiKAI) | 36 | 2023-12-25 |  |
| Mystics | `yt_ztW5D_D5C6s.md` | Zulerah | [Skeletal Mystics Luring Guide - OSRS](https://www.youtube.com/watch?v=ztW5D_D5C6s) | 3:37 | 2019-04-10 |  |
| Mystics | `yt_sYDBteWvl1I.md` | Steins;Gate | [Short and Simple CoX Mystics Guide OSRS 2024](https://www.youtube.com/watch?v=sYDBteWvl1I) | 1:07 | 2024-06-29 |  |
| Muttadiles | `yt_DQjmjN4QKDw.md` | The Academy | Premier OSRS Guides | [MUTTADILE - In 90 Seconds or Less](https://www.youtube.com/watch?v=DQjmjN4QKDw) | 1:13 | 2023-12-25 |  |
| Muttadiles | `yt_s1cQDeyBH2Y.md` | ThePerfectG | [FULL Solo Muttadile Guide! Best Up To FIVE People](https://www.youtube.com/watch?v=s1cQDeyBH2Y) | 5:17 | 2021-04-04 | chapter 'How to tick eat (Big mutta)' |
| Muttadiles | `yt_MLXx0W_1YSk.md` | Plank2g | [Muttadile Guide | How to Muttadile |  Old School RuneScape | OSRS](https://www.youtube.com/watch?v=MLXx0W_1YSk) | 1:37 | 2020-08-03 |  |
| Muttadiles | (none, no captions) | Klonzyon | [Muttadiles using Freezes in Chambers of Xeric](https://www.youtube.com/watch?v=XjhYjGLXPms) | 2:16 | 2022-08-19 | freezes on Muttadiles; NO captions |
| Whole raid / CM | `yt_0a61znGm0pc.md` | Extile | [OSRS Chambers of Xeric Guide For Idiots](https://www.youtube.com/watch?v=0a61znGm0pc) | 20:00 | 2024-02-10 | 20 chapters, one per room. Chapters: 0:00 Intro; 0:28 Getting here and scouting; 1:00 Gear; 1:17 Tekton; 2:12 Muttadile; 2:47 Mystics; 3:16 Tightrope; 3:52 Theiving; 4:15 Guardians; 4:42 Vasa; 6:00 Vanguards; 6:37 Vespula; 7:25 Crabs; 8:28 Ice Demon; 9:24 Shamans; 10:00 Prep; 12:32 Olm Room Layout; 13:47 Olm Attacks; 16:53 Real time walk through; 19:30 Outtro and thanks |
| Whole raid / CM | `yt_wxGBi89vE_s.md` | Gnomonkey | [Your First Budget Solo CoX Guide (OSRS)](https://www.youtube.com/watch?v=wxGBi89vE_s) | 1:10:39 | 2024-10-27 | 15 chapters incl. Olm chapters at 18 and 50 min; the plugin list in the description. Chapters: 0:00 <Untitled Chapter 1>; 6:52 Muttadile; 8:54 Tekton; 10:40 Crabs; 12:39 Mystics; 15:06 Thieving; 18:58 Olm; 40:46 Bofa Raid; 42:48 Vesp; 43:36 Crabs; 45:48 Shamans; 46:30 Vasa; 48:00 Ice Demon; 50:55 Olm; 1:06:01 Tightrope Skip |
| Whole raid / CM | `yt_1aFJyKKdjn8.md` | Beleti | [The ONLY Solo CM GUIDE You'll EVER Need | OSRS](https://www.youtube.com/watch?v=1aFJyKKdjn8) | 50:59 | 2026-02-12 | 27 chapters, every CM room; Olm chapters 35 to 44 min. Chapters: 0:00 Introduction; 0:32 Gear and inventory setup; 2:08 Spells and special weapons; 3:22 Room specific items; 6:16 Pre-potting and preparation; 7:26 Tecton fight mechanics; 9:23 Crab room strategy; 10:36 Scavenging and storage; 11:58 Ice Demon room; 14:04 Shaman room techniques; 15:55 Prep and potion making; 18:27 Vanguards room tactics; 21:01 Thieving room and grubs; 22:27 Vespula fight strategy; 24:08 Tightrope room fight; 26:30 Guardians room mechanics; 27:13 Vasa Nistirio encounter; 29:47 Skeletal Mystics room; 31:41 Muttadile room fight; 33:58 Preparation for Olm; 35:56 Olm head phase mechanics; 37:20 Olm mage phase strategy; 38:32 Olm melee phase tactics; 40:20 Handling Olm special attacks; 42:58 Olm final phase tactics; 44:56 Advanced Olm methods; 50:57 Conclusion |
| Whole raid / CM | `yt_38cqq0xFEYM.md` | PecanBread11 | [OSRS: Solo CM advanced guide](https://www.youtube.com/watch?v=38cqq0xFEYM) | 39:14 | 2025-08-18 | 18 chapters, every CM room; 91 'tick' mentions in the transcript (densest tick-talk of the set). Chapters: 0:00 Intro; 1:10 Gear setup; 8:14 Banking properly; 11:48 Avoiding stat tick downs; 14:54 Tekton; 17:04 Crabs; 18:29 Ice demon; 22:21 Shamans; 23:40 Vanguards; 27:03 Thieving; 29:32 Vespula; 30:40 Tightrope; 31:55 Guardians; 32:51 Vasa; 33:57 Mystics; 34:18 Muttadiles; 35:50 Olm; 38:46 Outro |
| Whole raid / CM | (none, no captions) | Woox | [Efficient Raids Solo Guide (Chambers of Xeric)](https://www.youtube.com/watch?v=ox1leZ5Z44E) | 41:42 | 2017-02-20 | 2017 Woox solo guide; NO captions. Chapters: 0:00 <Untitled Chapter 1>; 0:26 Recommended stats; 13:51 Estimated Kill Time; 19:06 Max hits; 34:41 Completion time and points; 40:45 1293m |

### Per-room coverage (two guide videos by different players, minimum; with captions)

| Room | Players with captions |
|---|---|
| Olm | Kaoz, SoupRS, Molgoatkirby, The Academy, WildMudkip, AsukaYen, Noodely (+ Synq, Tubby, TheEdB0ys held) |
| Tekton | Theoatrix, The Academy, RuneScythed, BoosterOSRS |
| Crabs | The Academy, Steins;Gate, ThePerfectG |
| Ice demon | The Academy, Plank2g, Steins;Gate |
| Shamans | The Academy, TheEdB0ys (generic Lizardman shaman guide, not CoX-specific; checks the same npc) |
| Vanguards | The Academy, Steins;Gate (only two videos found with captions) |
| Thieving | The Academy, Steins;Gate, FIFO Clan |
| Vespula | The Academy, RuneScythed, Fluffeh |
| Tightrope | The Academy, Plank2g, Nang |
| Guardians | The Academy, Theoatrix (puzzle/demi-boss rooms) |
| Vasa | The Academy, Plank2g, Charles |
| Mystics | The Academy, Zulerah, Steins;Gate |
| Muttadiles | The Academy, ThePerfectG, Plank2g |

Short "90 Seconds or Less" and "Short and Simple" videos are summaries; the strongest tick-talk is in WildMudkip,
Molgoatkirby, Noodely, PecanBread11, Beleti and Gnomonkey (Olm, CM, whole raid).
