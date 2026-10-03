# The Inferno: downloaded sources and their ranking

Everything the Inferno spec pass cites, pulled down on **3 October 2026** so that a later edit upstream cannot
silently move the acceptance target. This is the corpus index: it lists what is pinned, where, at which revision or
commit, and what each source kind can and cannot be trusted for. It states **no verdict**: where two sources disagree,
section 9 lists the rows and the spec workers settle them from the source lines, quoting them.

Paths are relative to the worktree root. The part ledgers (one per source kind) carry the full fetch record and each
part's "what this part states" list: [LEDGER_wiki](sources/LEDGER_wiki.md), [LEDGER_blert](sources/LEDGER_blert.md),
[LEDGER_code](sources/LEDGER_code.md), [LEDGER_guides](sources/LEDGER_guides.md), [LEDGER_cache](sources/LEDGER_cache.md).
The four corpus reports are `build/spec_state/matthew-mbp-m4-waves-b1-spec-inferno/corpus.{wiki,blert,code,guides}.json`;
the cache part has no report of its own (the inventory agent's commit c992f27f1 landed the dumps and the ledger).

## 0. The ranking (one paragraph)

The ranking is `docs/minigames/theater_of_blood/COMMUNITY_SOURCES.md`'s, applied to the Inferno: a **Jagex newspost** is
grade A (Jagex statements; the pinned copies are the OSRS Wiki's `Update:` mirror, and a quote that carries a grade
opens the post's original link on line 1); the **rev-239 cache** states ids, hitpoints, animations and frame counts as
data; **Blert's recordings** are grade B only where the recorder *observes* an event (spawn, attack animation, hit,
despawn) and never where the plugin or server *asserts* a table; **plugin code** that observes the live client and
**simulators** are grade C when two independent lineages agree (section 5 gives the four lineages: a shared lineage is
one source); the **reference servers** (Kronos, LostCity) are shape and id sources, never a balance number; the
**pinned wiki** is a secondary source that quotes Jagex (Mod Ash tweets second-hand) and is the spec's starting point;
**guides and video transcripts** are grade D (a narrator's number) and a video alone never promotes a grade.
Nothing in `sources/` is authored by this project except the cache dumps (mechanical, `tools/waves_gate/cache_dump.py`),
`blert/SAMPLE_SUMMARY.md` and `CODE_CONSTANTS.md` (derived, with file:line into the copies), and the READMEs.

## 1. Jagex newsposts (grade A; fetched 2026-10-03)

Source: the OSRS Wiki's `Update:` pages (namespace 112), which mirror Jagex's posts. 190 `Update:` titles were found by
a namespace-112 search, 193 screened, **71 pinned** in `docs/minigames/inferno/sources/newsposts/` with
`newsposts/manifest.tsv` (title, revision, revision date, file; every fetch is also in
[LEDGER_wiki](sources/LEDGER_wiki.md)). About 120 screened posts mention an Inferno word only in passing and are not
pinned. The Inferno is a 2017 release: the release post (2017-06-01) gives **no mechanics numbers**. The 2016 dev blogs
(40+ waves; 63 + 22 waves) are superseded proposals and are history, not spec.

Every grade A sentence the worker found follows, with file and line (the worker's full list is
`build/spec_state/matthew-mbp-m4-waves-b1-spec-inferno/corpus.newsposts.md`; a mirror is a copy, so a row that carries a
grade opens the original link on line 1 of its file). "Cited tweets" are Jagex statements quoted on pinned wiki pages
and were **not fetched**: they are second-hand.

### Entry, the sacrificed cape, access rules

| Date | File:line | Quote |
|---|---|---|
| 2017-06-01 | `docs/minigames/inferno/sources/newsposts/wiki_Update_The_Inferno.wikitext`:69 | "Beyond sacrificing a Fire Cape, there are '''no requirements''' to attempt The Inferno." |
| 2017-06-01 | `docs/minigames/inferno/sources/newsposts/wiki_Update_The_Inferno.wikitext`:21,23 | "If you wish to enter the city of Mor Ul Rek, present your Fire Cape to a city guard." / "You will not lose your Fire Cape while doing this." (the city, not the Inferno) |
| 2017-03-17 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Dev_Blog_Mor_Ul_Rek_The_Inferno.wikitext`:80 | "You must make a '''one-time sacrifice''' of a Fire Cape in order to convince the TzHaar you're worthy." |
| 2017-05-03 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Progress_Report_The_Inferno.wikitext`:105 | "...having to show off a Fire cape (or Max fire cape) to be able to access the city, and in fact you must sacrifice one completely to access The Inferno." |
| 2016-01-20 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Dev_Blog_The_Inferno_v2.wikitext`:25 (SUPERSEDED as a proposal; the one-time fee survived) | "will be made to sacrifice a fire cape in order to be granted access ... (the payment of a fire cape is a one-time fee)." |
| 2017-06-08 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Pest_Control_Open_Weekend.wikitext`:106 | "Items dropped in the Inferno now stay on the ground for 30 minutes before despawning." |
| 2017-06-08 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Pest_Control_Open_Weekend.wikitext`:107 | "Teleports are no longer permitted inside the Inferno; there's no real need to teleport out of there, and players were triggering it by accident." |
| 2018-03-15 | `docs/minigames/inferno/sources/newsposts/wiki_Update_QoL_Deadman_and_the_Falador_Party_Room.wikitext`:58 | "You can no longer log out at the start of a wave during the Inferno." |
| 2018-03-15 | `docs/minigames/inferno/sources/newsposts/wiki_Update_QoL_Deadman_and_the_Falador_Party_Room.wikitext`:59 | "Jal-Zek can no longer move when resurrecting another monster within the Inferno." |
| 2018-03-15 | `docs/minigames/inferno/sources/newsposts/wiki_Update_QoL_Deadman_and_the_Falador_Party_Room.wikitext`:56 | "Jal-ImKot can no longer deal damage during its dig animation in the Inferno." |
| 2018-03-15 | `docs/minigames/inferno/sources/newsposts/wiki_Update_QoL_Deadman_and_the_Falador_Party_Room.wikitext`:57 | "The Ancestral Glyph used to hide from TzKal-Zuk in the Inferno now remembers which way it will begin travelling, and will no longer change direction when logging back in." |
| 2017-06-22 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Mining_Guild_Expansion.wikitext`:93,95 | "Players can now interact with scenery in the Inferno (dead clicks have now been removed from the northern edge of the arena)" / "Jal-Zek is now able to perform its attack animation correctly even while taking damage" |
| 2019-01-31 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Chambers_of_Xeric_Kebos_and_Collection_Log_Changes.wikitext`:88 | "The only exception is the Inferno which will be disabled on these worlds." |
| 2019-08-15 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Small_Game_Updates_and_Betas.wikitext`:87 | "The Inferno itself is still being adjusted to handle the line-of-sight changes, since we would prefer the update not to affect how players fight Zuk in there." |
| 2021-01-26 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Equipment_Rebalancing_Changes.wikitext`:20 | "You'll be able to play any content within the main game, including the Inferno." (beta worlds) |
| 2021-10-06 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Group_Ironman.wikitext`:249 | "In PvM, there are safe deaths in places like the Chambers of Xeric, the Fight Caves, the Inferno and Pest Control." |
| 2021-11-26 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Group_Ironman_Post_Launch_Improvements.wikitext`:14 | "it's impossible to use teleport spells to escape the Inferno" |
| 2023-09-20 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Poll_80_Shooting_Stars_Changes.wikitext`:92 | "Infernal Cape repair cost has been increased from 75,000 to 225,000 GP" |
| 2022-06-17 / 2019-10-30 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Third_Party_Client_Guidelines.wikitext`:13; `docs/minigames/inferno/sources/newsposts/wiki_Update_Another_Message_About_Unofficial_Clients.wikitext`:25 | features that aid fights are prohibited "(this includes all Raids sub-bosses, Slayer bosses, Demi-bosses, and wave-based minigames, including the Fight Caves and Inferno)" (bears on the plugin sources: an Inferno helper is a third-party-client question) |

### Wave structure, spawns, pillars

| Date | File:line | Quote |
|---|---|---|
| 2017-05-03 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Progress_Report_The_Inferno.wikitext`:66-68 | "one thing I managed to tie into the core system, was purely randomised spawns. A player shouldn't be able to determine where monsters are going to be spawned, but at the same time, these locations are saved per run! This means they cannot logout and in to get more desired spawn locations for the monsters." |
| 2017-05-03 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Progress_Report_The_Inferno.wikitext`:56 | "the wave structure is vital for so much of the content" (no number) |
| 2016-01-20 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Dev_Blog_The_Inferno_v2.wikitext`:29 (SUPERSEDED proposal) | "you will be faced with '''40+ waves''' of '''6 previously unseen TzHaar monsters'''" (the shipped game has 69 waves and 6 new monster kinds before Jad; the wiki table is the spec, `wiki/wiki_Inferno.wikitext`:163-275) |
| 2016-01-19 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Dev_Blog_The_Inferno.wikitext`:7,9 (SUPERSEDED, Jad 2 inside the Fight Cave) | "Upon completing 63 waves of the Fight Cave ... you will be faced with '''22 additional waves'''" |
| 2023-03-15 | `docs/minigames/inferno/sources/newsposts/wiki_Update_The_Last_of_Poll_78.wikitext`:76 | speedrun categories: "Inferno categories (Standard, Melee, No-pillar)" |

### Nibblers (Jal-Nib)

| Date | File:line | Quote |
|---|---|---|
| 2025-08-20 | `docs/minigames/inferno/sources/newsposts/wiki_Update_More_Doom_Tweaks_Poll_84_Summer_Sweep_Up_Changes.wikitext`:41,117 | "Added 40% Water weakness to Nibblers and Inferno Bats." |
| 2025-05-30 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Summer_Sweep_Up_Combat_Loot.wikitext`:42 | water weaknesses "Chief among these are all TzHaar creatures (including TzTok-Jad and TzKal-Zuk)" (no percentage in the post; the 40% is on each monster page's Changes box) |

### The bat (Jal-MejRah)

| Date | File:line | Quote |
|---|---|---|
| 2021-04-22 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Poll_75_Game_Improvements_Blog.wikitext`:273,277 | "Inferno Bats are frustrating, due to them randomly draining stats, even when prayed against correctly ... they will still be able to drain run energy" / poll "Should Inferno Bats stop draining player stats when prayed against correctly" |
| 2021-06-30 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Phosani_s_Nightmare.wikitext`:326 | "Bats in the Inferno no longer drain combat stats if you are praying Protect from Missiles." |
| 2025-04-16 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Easter_Event.wikitext`:25 | "The Run Energy drain effect of the Jal-MejRah can now be blocked by Prayer. This was already the case for their stat-stealing effect." |
| 2025-04-09 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Farming_Autocast_QoL_Improvements.wikitext`:104 "Ever been drained by that pesky bat in Inferno" (mention only) | |

### Ranger and the healers (Jal-Xil, Jal-MejJak)

| Date | File:line | Quote |
|---|---|---|
| 2021-05-28 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Equipment_Rebalance_Ranged_Meta_Proposal.wikitext`:1081 | "we're going to proceed with reducing the Hitpoints of both Jal-Xil and Jal-MejJak throughout the Inferno by 5 each." |
| 2021-06-30 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Phosani_s_Nightmare.wikitext`:292 | "HP from the the healers at Zuk and the rangers within the inferno have been reduced by 5." |
| 2021-05-28 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Equipment_Rebalance_Ranged_Meta_Proposal.wikitext`:1040-1075 | the player's blowpipe against them (112 Ranged, Rigour, dragon darts): Jal-Xil max hit 30 -> 29, accuracy 93.73% -> 92.22%, DPS 11.716 -> 11.144, time to kill 11.095 -> 11.666; Jal-MejJak accuracy 90.09% -> 87.72%, DPS 11.262 -> 10.599, time to kill 7.104 -> 7.548 (these are the PLAYER's numbers vs the monsters, before the 5 hp cut) |

### Mager (Jal-Zek)

| Date | File:line | Quote |
|---|---|---|
| 2017-06-22 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Mining_Guild_Expansion.wikitext`:95 | "Jal-Zek is now able to perform its attack animation correctly even while taking damage" |
| 2018-03-15 | `docs/minigames/inferno/sources/newsposts/wiki_Update_QoL_Deadman_and_the_Falador_Party_Room.wikitext`:59 | "Jal-Zek can no longer move when resurrecting another monster within the Inferno." |

(The revive chance 1/10 is a Jagex tweet cited on the wiki page, listed under "cited tweets" below; no newspost states it.)

### Meleer (Jal-ImKot)

| Date | File:line | Quote |
|---|---|---|
| 2018-03-15 | `docs/minigames/inferno/sources/newsposts/wiki_Update_QoL_Deadman_and_the_Falador_Party_Room.wikitext`:56 | "Jal-ImKot can no longer deal damage during its dig animation in the Inferno." |

### Jad (JalTok-Jad, Yt-HurKot)

No newspost states a Jad number for the Inferno. Mention-only or Fight Cave: `docs/minigames/inferno/sources/newsposts/wiki_Update_XP_Drops_Jad_Pet_Slayer.wikitext` (TzTok-Jad), `docs/minigames/inferno/sources/newsposts/wiki_Update_Land_of_the_Goblins.wikitext`:(Six Jad challenge),
`docs/minigames/inferno/sources/newsposts/wiki_Update_TzHaar_Ket_Rak_s_Challenges.wikitext` (Six Jad challenge). The wiki cites Mod Ash tweets for the healer heal (see below).
`docs/minigames/inferno/sources/newsposts/wiki_Update_Combat_Achievements_Poll_Blog.wikitext`:381 states the lore: "a number of eggs from TzTok-Jad were hatched inside the Inferno. From these eggs came the JalTok-Jads."

### TzKal-Zuk and the glyph

| Date | File:line | Quote |
|---|---|---|
| 2021-01-13 | `docs/minigames/inferno/sources/newsposts/wiki_Update_God_Wars_Instancing_and_Soul_Wars_Improvements.wikitext`:82 | "Zuk's shield no longer vanishes right as he dies." |
| 2024-07-24 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Deadman_Armageddon_While_Guthix_Sleeps_Tweaks.wikitext`:65 | "The 'Attack' option on Zuk is once again visible during the introductory cut scene." (so there is an intro cutscene during which Zuk is attackable in the game's data) |
| 2026-02-04 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Deadman_Annihilation_Tweaks_Shellbane_Gryphon_CAs_More.wikitext`:80 | "Zuk's lava base is visible once again" |
| 2026-04-01 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Remaining_Getting_Around_Changes.wikitext`:53 | "The shield no longer clips into the arena at Inferno during the final stage." |
| 2021-12-01 | `docs/minigames/inferno/sources/newsposts/wiki_Update_PJ_Timer_Beta_Chat_Changes.wikitext`:138 | "We've lowered the HiScores' TzKal-Zuk Kill Count Requirement from 2 to 1." |
| 2022-01-19 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Leagues_III_Shattered_Relics_Launch.wikitext`:326 | "Dying at the same moment as killing TzKal-Zuk now correctly subtracts 1 KC from a task." |
| 2021-04-20 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Equipment_Rebalance_Ranged_Meta.wikitext`:208 | "TzKal-Zuk is intentionally placed out of range for the Blowpipe" (closer 2026-10-03: the quote is on line 208; line 28 says "a creature that the Blowpipe cannot reach (such as TzKal-Zuk)") |

### Rewards, the pet, Slayer

| Date | File:line | Quote |
|---|---|---|
| 2017-08-03 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Inferno_slayer_task_Tournament_worlds_and_Poll_56.wikitext`:15 | "this task will be solely for those who have already killed TzKal-Zuk." |
| 2017-08-03 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Inferno_slayer_task_Tournament_worlds_and_Poll_56.wikitext`:19 | "Completion of this task will yield 100,000 Slayer xp from killing TzKal-Zuk, and it offers an increased chance of the Jal-nib-rek pet." |
| 2017-08-03 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Inferno_slayer_task_Tournament_worlds_and_Poll_56.wikitext`:21 | "Dying during the task or leaving the Inferno prior to completion will see the task wiped, as with the TzTok-Jad task." |
| 2017-06-08 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Pest_Control_Open_Weekend.wikitext`:92-98 | first twelve to earn the Infernal cape, "No one has yet received the pet! Remember you can gamble that precious Infernal cape for an extra chance." |
| 2017-03-17 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Dev_Blog_Mor_Ul_Rek_The_Inferno.wikitext`:17 | "The prayer bonus of the Infernal Cape now matches that of the Fire Cape." |
| 2018-02-22 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Old_School_RuneScape_s_5th_Birthday.wikitext`:81 | "A metamorphosis option has been added to the Jal-Nib-Rek pet, allowing it to transform into the TzRek-Zuk." |
| 2022-10-21 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Combat_Achievements_Expansion_Rewards.wikitext`:136,150 | Master tier: "Slayer tasks to kill TzTok-Jad or TzKal-Zuk are increased to 2 kills per task"; Grandmaster "3 kills per task" (the shipped values are on `wiki/wiki_Combat_Achievements_Master.wikitext`:61 and `wiki_Combat_Achievements_Grandmaster.wikitext`:50) |
| 2021-07-21 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Combat_Achievements.wikitext`:266,279 | "When you are assigned a TzTok-Jad or TzKal-Zuk Slayer task you will have to kill two of either" / "three of either" |
| 2025-06-25 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Summer_Sweep_Up_Combat.wikitext`:140 | "You can now show your Zuk Slayer Helmet to TzHaar-Ket-Keh to benefit from an equipped Zuk Helm within the Inferno and Fight Caves. ... you won't have an improved chance at the pet." |
| 2025-09-24 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Interface_Uplift.wikitext`:35 | "Fixed the Slayer cape perk so players can now get back-to-back Zuk tasks as intended." |
| 2025-02-12 | `docs/minigames/inferno/sources/newsposts/wiki_Update_Royal_Titans_Feedback_12th_Birthday_Celebrations.wikitext`:53 | "You can no longer accidentally get repeat Jad or Zuk tasks by cancelling them through teleport." |

### Cited tweets (Jagex statements quoted on the pinned wiki pages; the tweets themselves were not fetched)

| Date | File:line | Quote |
|---|---|---|
| 2023-03-24 | `wiki/wiki_Jal_Zek.wikitext`:48 (Mod Ash) | "1/10 of it choosing that spec rather than other attacks." |
| 2020-12-30 | `wiki/wiki_Yt_HurKot.wikitext`:70 (Mod Ash) | "Its other heal code appears to do 15-24 every 4 ticks." (Inferno healer) |
| 2020-05-06 | `wiki/wiki_Yt_HurKot.wikitext`:70 (Mod Ash) | "It's a flat +5 (per NPC), running every 4 ticks." (Fight Caves healer) |
| 2023-09-27 | `wiki/wiki_TzKal_Zuk.wikitext`:17 (Mod Ash) | "Your description of 'Zuk doing 1 damage roll, with the maxhit being the avg of it's mage+range maxhit' is the correct interpretation." |
| 2022-07-10 | `wiki/wiki_TzKal_Zuk.wikitext`:76 (Mod Ash) | accuracy "rolls the average accuracy against the average defence" of the player's effective Ranged and Magic defence |

## 2. The rev-239 cache (data; dumped 2026-10-03)

Pinned by the inventory agent (commit c992f27f1) as mechanical dumps of the unpacked text form in OSRS-Content at commit
`c70422bdf6` (branch `matthew-mbp-m4-waves-b1`; revision 239, `osrs239-content/meta.ini`); the binary cache is not read.
The command that reproduces every file byte for byte, the selection rules and the negative results are in
[LEDGER_cache](sources/LEDGER_cache.md). Nothing here is a Jagex statement about behaviour: it is data (hitpoints,
animations, frame lengths, ids, placements) and it decides ids and frame counts that no other source states.

| File | Records |
|---|---|
| `docs/minigames/inferno/sources/cache_npc.txt` | 30 npcs |
| `docs/minigames/inferno/sources/cache_seq.txt` | 96 sequences (frame count, run-length frame lengths, ticks, frame sounds) |
| `docs/minigames/inferno/sources/cache_spotanim.txt` | 25 spotanims |
| `docs/minigames/inferno/sources/cache_locs.txt` | 106 locs, each with up to 12 map placements cited to the `.jl2` line |
| `docs/minigames/inferno/sources/cache_vars.txt` | 5 varps, 26 varbits |
| `docs/minigames/inferno/sources/cache_objs.txt` | 22 objs |
| `docs/minigames/inferno/sources/cache_sounds.txt` | 37 sounds |
| `docs/minigames/inferno/sources/cache_interfaces.txt` | 3 interfaces, 7 clientscripts |
| `docs/minigames/inferno/sources/cache_enums_dbrows.txt` | 8 dbrows, 19 structs, 0 enums |
| `docs/minigames/inferno/sources/cache_music.txt` | 2 music tracks, 0 jingles |

The audiovisual inventory built on the dumps is `docs/minigames/inferno/AV_INVENTORY.md` and `AV_INVENTORY.tsv` (386
rows: each asset against where our content uses it). Negative results kept in the ledger: no selected sequence carries a
frame sound (agrees with `docs/INFERNO_SOUNDS.md` section 4); the map places no rocky-support locs (30353-30355); no
interface exists for a wave counter, timer, entry warning or practice mode (`inferno_hp_hud` 596 only); no enum or
jingle matches the name rule.

## 3. Blert recordings (grade B only for OBSERVED events; fetched 2026-10-03)

**Read `docs/minigames/inferno/sources/blert/PROVENANCE.md` before using any row.** Blert's recorder emits events it
*observes* (an npc spawning, an attack animation, a hit, a despawn) and tables it *asserts* (the plugin's npc
hitpoints, the server's spawn tiles and WAVES table). Only observed events can carry grade B. In particular
`NPC_DEATH` is a despawn, not the hitpoint-zero tick (`docs/minigames/inferno/sources/blert/plugin/src/main/java/io/blert/core/DataTracker.java:772-780`); tick 0 of a wave
is the recorder tick of the chat message "Wave: N", not a server tick, and spawn time is clamped to tick 0; the
wave length is the per-wave record's `ticks`, the wave's start message to its end message as the client stamped them (closer 2026-10-03, correcting "a lower bound (last observed event)": `tools/waves_gate/verify_blert.py:455-457`; the pinned `blert/blert/challenge-harder/src/processing/inferno.rs:346` stores `ticks: events.duration()`, and the full clone at 7c7750cf, not pinned, sets the merged timeline's last tick from the client's stage-end update, `challenge-harder/src/merging/client_events.rs:287`; a WIPED last wave ends where the tracker was torn down). The recorder has **no event** for the Zuk shield, the Zuk healers,
pillar damage, the mager's revive target or projectiles.

| What | Pinned at | Fetched |
|---|---|---|
| `blert-io/plugin` (MIT) | commit 0efb39800b9e1f73e01c0116b962ccc93f0f8f6f, `docs/minigames/inferno/sources/blert/plugin/` | 2026-10-03 |
| `blert-io/blert` (README says MIT; no LICENSE file at this commit), 9 files | commit 7c7750cf01b23e5d223623d7c34c5ef10ca327fd, `docs/minigames/inferno/sources/blert/blert/` | 2026-10-03 |
| `blert-io/protos` | commit 450112dffc33e1e51b7ec201e1c8a45bf2f903c9, `docs/minigames/inferno/sources/blert/protos/` | 2026-10-03 |
| blert.io public API, 1,262 requests (the rows of `FETCH_LOG.tsv`; closer recount 2026-10-03) at one per 3 s (hard constant in `tools/waves_gate/verify_blert.py`), 16:32Z-17:33Z and 19:38Z-19:45Z | live API (no revision); `docs/minigames/inferno/sources/blert_api/FETCH_LOG.tsv` has date, status, bytes, url, file of every request | 2026-10-03 |
| Per-wave event streams `GET /api/v1/challenges/inferno/<uuid>/events?stage=<199+wave>` and per-wave records | `docs/minigames/inferno/sources/blert_api/wave_records.tsv` (1,269 rows); `observed_npc_events.tsv` (2.9 MB, over the 2 MB rule: on disk beside it, **not in git**, rewritten offline from the cache by `verify_blert.py export`) | 2026-10-03 |
| Derived distributions (observed events only) | `docs/minigames/inferno/sources/blert/SAMPLE_SUMMARY.md` | 2026-10-03 |

The sample is 20 challenges, 1,235 wave streams: 18 planned (12 completed, 6 failed at wave 31 or later; all cached in
full) plus two newer challenges from a live listing that moved on, `c8d56ab9` (waves 1-32) and `dd5b1591` (waves 1-28),
**left partial on purpose** (finishing would exceed the 150-request budget). Waves 28-32 therefore have n = 18 or 19.
One cache file is committed (`docs/minigames/inferno/sources/blert_api/c8d56ab9-fc21-4fb2-88cd-8c68ad254fd3.json`, 1.35 MB).
`7dca5546-ecfb-41bf-b946-30855bc175e6.json` (2,086,242 bytes: under the tool's 2 MiB line, over the loop's 2 MB rule) is on
disk in `sources/blert_api/` and **not in git** (closer 2026-10-03); the other 19 (5 to 9 MB each, 106 MB) stay in
`build/corpus_tmp/blert_api/` and are not in git. `sources/blert_api/.gitignore` names the two left out. A rerun re-lists live challenges but never refetches a
cached wave. See `docs/minigames/inferno/sources/blert/README.md` and `PROVENANCE.md`.

## 4. Plugin code, observers of the live client (grade C when two lineages agree)

Fetched from GitHub only, 2026-10-03, sequentially. Copies are byte for byte with their licence header, so the line
numbers are upstream's; every constant row in `docs/minigames/inferno/sources/CODE_CONSTANTS.md` (92 rows, C001-C092) cites
file:line into them. Each source directory has a README naming repo, commit, files and licence. Lineages: **A** OpenOSRS
(`openosrs_inferno/`, `kotori_inferno/`: one lineage, kotori is a descendant of the 2020 plugin); **C** observers
(`inferno_scouter/`, `inferno_2d_map/`, `infernostats/`: ids, region, grid, spawn tiles); **D** set timers
(`settimer/`, `inferno_timer_maxswa/`, kotori's spawn-timer infobox: one community origin).
Jagex bars third-party features that predict attacks in wave minigames including the Inferno
(`docs/minigames/inferno/sources/newsposts/wiki_Update_Third_Party_Client_Guidelines.wikitext:13`): the plugins are
read as evidence of what the client shows, not as endorsed tooling.

| Date | Source | Commit / revision | What was taken |
|---|---|---|---|
| 2026-10-03 | https://api.github.com/repos/runelite/plugin-hub/git/trees/master:plugins | tree of master (commit 9f14e7daebee0e8b03c4d522eccba92476bc4766 at the fetch) | the 2,838 plugin names (`build/corpus_tmp/hub_names.txt`) |
| 2026-10-03 | https://api.github.com/repos/runelite/plugin-hub/contents/plugins/<name> for inferno-2d-map, inferno-autosplitter, inferno-blob-audio, inferno-scouter, inferno-splits-logger, inferno-stats, inferno-tracker, inferno-wave-splits, infernalfc, set-timer, tzhaar-hp-tracker, fight-cave-helper, fight-cave-waves, npc-attack-tick-timer | same | the `repository=` and `commit=` pin of each |
| 2026-10-03 | https://api.github.com/repos/runelite/runelite/contents/runelite-client/src/main/java/net/runelite/client/plugins | master | listing |
| 2026-10-03 | https://api.github.com/search/code?q=InfernoNPC filename:InfernoNPC.java and InfernoPlugin.java (two requests, 3 s apart) | n/a | 22 repositories carrying copies of the OpenOSRS Inferno plugin (OreoCupcakes/kotori-plugins, lucid-plugins/SideloadPlugins, karankurbur/OpenOSRSPlugins, Dirro/osrs-plugins, jky-dev/b2slite, Dabalon/MeteorLite, several Kronos client trees, ...) |
| 2026-10-03 | https://api.github.com/search/repositories (queries: InfernoTrainer, inferno runelite plugin, zuk runelite, openosrs plugins, zuk osrs, zuksharp, inferno osrs simulator, tzkal-zuk, jal-zek resurrect, osrs inferno solver; 3 s apart) | n/a | candidate repositories |
| 2026-10-03 | https://github.com/OreoCupcakes/kotori-plugins (clone --depth 1) | 9ea4866e0fe1fb96ab07fcce3211f151441d4053 | `inferno/` |
| 2026-10-03 | https://github.com/karankurbur/OpenOSRSPlugins (clone --depth 1) | 904639cfdeaef33e724c872118e005e8967eadfd | `inferno/` |
| 2026-10-03 | https://github.com/Dirro/osrs-plugins (clone --depth 1) | b13d786e205b02f6e0a1f5eb6069405fa6277e41 | `inferno/` |
| 2026-10-03 | https://github.com/open-osrs/plugins (clone --depth 1) | 9e680b5e220e33bc74a368d1f735fee49a22ca06 | none: the repository no longer carries an `inferno` plugin |
| 2026-10-03 | https://github.com/lucid-plugins/SideloadPlugins (clone --depth 1) | 027df8dcc643a5091e772b2de5f0dafd32df2fe8 | `src/main/java/com/lucidplugins/inferno/` |
| 2026-10-03 | https://github.com/OldSchoolSDK/InfernoTrainer (clone --depth 1) | 06fc103f70f1fa228678ca79910a8d3bb0798a7d | `src/content/inferno/js/**`, `test/simulations/ZukLineOfSight.test.ts` |
| 2026-10-03 | https://github.com/OldSchoolSDK/osrs-sdk (clone, then `--unshallow` for the file history) | 04fdaee3d155238e54cf16c1ac259f6c2b210078 (version bump to 0.1.4: 9b80ab4) | `src/sdk/**` |
| 2026-10-03 | https://github.com/jeremiah855/AUTOZUK (clone --depth 1) | 346dece30982a2c5ee60b702721d91ba17b4b8da | `index.html` |
| 2026-10-03 | https://github.com/propagating/ZukPrayer (clone --depth 1) | 1743f1d806f62ade70748549d3299d83096376e9 | README, `LoginTicks.java`, `ZukPrayerPlugin.java` read |
| 2026-10-03 | https://api.github.com/repos/propagating/zuksharp | n/a | HTTP 404 |
| 2026-10-03 | https://github.com/jeremiah855/inferno-scouter (clone --depth 1) | ff025377ea34d01c30d3bace220b9b14e359cfcb (= hub pin) | `InfernoScouterPlugin.java`, README |
| 2026-10-03 | https://github.com/TheRealGuru/inferno-2d-map (clone, then fetch of the hub pin) | 463b31a2989d49d5b310f39fe7f3d01c70999db4 (= hub pin; repo head 52503f8) | `Inferno2dMapPlugin.java`, `Coordinate.java` |
| 2026-10-03 | https://github.com/InfernoStats/InfernoStats (clone --depth 1) | 0efd7441d0a33486bbf2f13fd54cea846f426ee8 (= hub pin) | `InfernoStatsPlugin.java`, `model/InfernoNpc.java`, `controller/ChatHandler.java` |
| 2026-10-03 | https://github.com/InfernoStats/SetTimer (clone --depth 1) | 45a47eb32763087006459e43e865975fdace68fe (= hub pin) | `SetTimer.java`, `SetTimerPlugin.java` |
| 2026-10-03 | https://github.com/maxswa/inferno-timer (clone --depth 1) | e36b1ca99f2522d74c37694ffa9c2ff6e85fc02a | `src/App.tsx`, `src/utils.ts` read |
| 2026-10-03 | https://github.com/bradyp30/Zuk-Timer (clone --depth 1) | 1dec8d9d8f357b7ed0ab6035c2fa3745d6b1eec1 | `timer.js` read, not copied (no licence) |
| 2026-10-03 | https://github.com/davidsaad-git/triple-jad-sim (clone --depth 1) | b6c8d95386bec11de3f7acddc5cf714b1b4ff904 | `docs/INFERNO_DATA.md`, `docs/RESEARCH.md`, README, NOTICE.md |
| 2026-10-03 | https://github.com/jamiegyoung/runemarkers (clone --depth 1) | e7e618564bfc29a166db78efad56f20ff98ee1ac | `entities/inferno.json` |
| 2026-10-03 | https://github.com/Deagsly/Blobs, evaan/InfernoTracker, molgoatkirby/InfernoAutoSplitter, usa-usa-usa-usa/inferno-wave-splits (clone --depth 1; commits 257d7026, 85c8f5ca, f7c351b9, 5e026535) | n/a | file lists and sizes only |
| 2026-10-03 | https://raw.githubusercontent.com/runelite/runelite/d8e7d1e5f34e2899eda3d7cf4cd9661ae2206f22/runelite-api/src/main/java/net/runelite/api/gameval/NpcID.java (blob sha 05827b4a5e6d8acb7454dfb19d4070c0f298ad57, 1,300,343 bytes) | d8e7d1e5 (master 2026-09-30) | the Inferno rows |

The table above also holds the simulator rows (section 5). Hub plugins listed but not opened (disabled, splits or
trackers, no mechanics): infernalfc, inferno-splits-logger, npc-attack-tick-timer, tzhaar-hp-tracker,
inferno-autosplitter, inferno-tracker, inferno-wave-splits; the hub's pins are copied to
`docs/minigames/inferno/sources/plugin_hub_manifest_inferno.txt`. RuneLite core has no Inferno plugin. Numeric npc ids for
the gameval names the plugins use are in `docs/minigames/inferno/sources/runelite_gameval_NpcID_inferno_excerpt.txt`
(runelite commit d8e7d1e5f34e2899eda3d7cf4cd9661ae2206f22, 2026-10-03): npcs 7691-7710, pillar locs 30353-30355, region 9043.
Also pinned: `docs/minigames/inferno/sources/runemarkers_inferno/` (the WeDoRaids Zuk tile set, runemarkers e7e61856, one
community-guide origin) and `docs/minigames/inferno/sources/zukprayer/` (README only, no licence: the "wave spawns on
tick 15" claim, one lineage with AUTOZUK).

## 5. Simulators (lineage B; grade C only where an independent lineage agrees)

`docs/minigames/inferno/sources/infernotrainer/` (InfernoTrainer, GPL-3.0, commit 06fc103f70f1fa228678ca79910a8d3bb0798a7d)
on the engine `docs/minigames/inferno/sources/osrs_sdk/` (osrs-sdk, commit 04fdaee3d155238e54cf16c1ac259f6c2b210078), and
`docs/minigames/inferno/sources/autozuk/` (AUTOZUK, MIT, commit 346dece30982a2c5ee60b702721d91ba17b4b8da), which is built on
the trainer (identical dig constants -38/-50, 10% revive, 6/6 dig, spawn list) and adds its own "calibrated" projectile
tables. `docs/minigames/inferno/sources/triple_jad_sim/` (commit b6c8d95386bec11de3f7acddc5cf714b1b4ff904) is a
**compilation** of the wiki, the OpenOSRS plugin, InfernoStats, inferno-scouter, runemarkers and the trainer: a map of claims
and open questions, never a source for a row. A simulator is built from observed data and is not Jagex's code: none of
its constants is a grade-A statement. Fetch dates and commits are the section 4 table (all 2026-10-03).

## 6. Reference servers (shape and ids only)

Kronos and LostCity were **not fetched** on purpose. The ids, shapes and opening waits our tree took from Kronos are
listed from the content's own comments in `docs/minigames/inferno/sources/LEDGER_code.md` section 3. Kronos's 125 vs 130
(Jal-Xil), 75 vs 80 (Jal-MejJak) and 251 vs 148 (Zuk max) were already overridden by wiki figures, so no server balance
number appears in `CODE_CONSTANTS.md`. Our own content (not a source) is in
`OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno/` (`configs/inferno.constant`, `scripts/inferno*.rs2`).

## 7. The pinned OSRS Wiki (secondary; fetched 2026-10-03)

Fetched only from oldschool.runescape.wiki, sequentially, at the project's User-Agent via `tools/toa_fetch_wiki.py`
(pacing 0.8 s; the worker's own scripts 1.2-1.5 s). 40 wiki pages pinned as raw wikitext at the revision below (the
`?oldid=` link renders exactly the text in `sources/`); the same list is `docs/minigames/inferno/sources/wiki/manifest.tsv`.
`docs/minigames/inferno/sources/wiki/TECHNIQUES.md` has 46 technique and system rows with file:line (blob scan and flick,
blob + meleer cycle, ranger + mager off-tick, healer tagging, triple Jad stagger, the Zuk shield walk). The wiki quotes
Mod Ash tweets second-hand (Jal-Zek revive 1/10, Yt-HurKot heal 15-24 per 4 ticks, Zuk single damage roll): the tweets
were not fetched; the archive urls are on the pinned pages.

| File | Page | Revision | Revision date |
|---|---|---:|---|
| `docs/minigames/inferno/sources/wiki/wiki_Yt_HurKot.wikitext` | [Yt-HurKot](https://oldschool.runescape.wiki/w/Yt-HurKot?oldid=15316944) | 15316944 | 2026-08-23 |
| `docs/minigames/inferno/sources/wiki/wiki_Inferno.wikitext` | [Inferno](https://oldschool.runescape.wiki/w/Inferno?oldid=15337520) | 15337520 | 2026-09-09 |
| `docs/minigames/inferno/sources/wiki/wiki_Infernal_cape.wikitext` | [Infernal cape](https://oldschool.runescape.wiki/w/Infernal_cape?oldid=15359317) | 15359317 | 2026-10-01 |
| `docs/minigames/inferno/sources/wiki/wiki_Jal_Nib_Rek.wikitext` | [Jal-Nib-Rek](https://oldschool.runescape.wiki/w/Jal-Nib-Rek?oldid=15352769) | 15352769 | 2026-09-21 |
| `docs/minigames/inferno/sources/wiki/wiki_TzKal_Zuk.wikitext` | [TzKal-Zuk](https://oldschool.runescape.wiki/w/TzKal-Zuk?oldid=15316929) | 15316929 | 2026-08-23 |
| `docs/minigames/inferno/sources/wiki/wiki_Jal_Ak.wikitext` | [Jal-Ak](https://oldschool.runescape.wiki/w/Jal-Ak?oldid=15316947) | 15316947 | 2026-08-23 |
| `docs/minigames/inferno/sources/wiki/wiki_Jal_AkRek_Xil.wikitext` | [Jal-AkRek-Xil](https://oldschool.runescape.wiki/w/Jal-AkRek-Xil?oldid=15316950) | 15316950 | 2026-08-23 |
| `docs/minigames/inferno/sources/wiki/wiki_Jal_MejRah.wikitext` | [Jal-MejRah](https://oldschool.runescape.wiki/w/Jal-MejRah?oldid=15316946) | 15316946 | 2026-08-23 |
| `docs/minigames/inferno/sources/wiki/wiki_Jal_Nib.wikitext` | [Jal-Nib](https://oldschool.runescape.wiki/w/Jal-Nib?oldid=15316945) | 15316945 | 2026-08-23 |
| `docs/minigames/inferno/sources/wiki/wiki_Jal_AkRek_Mej.wikitext` | [Jal-AkRek-Mej](https://oldschool.runescape.wiki/w/Jal-AkRek-Mej?oldid=15316949) | 15316949 | 2026-08-23 |
| `docs/minigames/inferno/sources/wiki/wiki_Jal_ImKot.wikitext` | [Jal-ImKot](https://oldschool.runescape.wiki/w/Jal-ImKot?oldid=15358435) | 15358435 | 2026-09-29 |
| `docs/minigames/inferno/sources/wiki/wiki_Jal_Zek.wikitext` | [Jal-Zek](https://oldschool.runescape.wiki/w/Jal-Zek?oldid=15316955) | 15316955 | 2026-08-23 |
| `docs/minigames/inferno/sources/wiki/wiki_Jal_Xil.wikitext` | [Jal-Xil](https://oldschool.runescape.wiki/w/Jal-Xil?oldid=15316952) | 15316952 | 2026-08-23 |
| `docs/minigames/inferno/sources/wiki/wiki_JalTok_Jad.wikitext` | [JalTok-Jad](https://oldschool.runescape.wiki/w/JalTok-Jad?oldid=15316953) | 15316953 | 2026-08-23 |
| `docs/minigames/inferno/sources/wiki/wiki_Jal_AkRek_Ket.wikitext` | [Jal-AkRek-Ket](https://oldschool.runescape.wiki/w/Jal-AkRek-Ket?oldid=15316948) | 15316948 | 2026-08-23 |
| `docs/minigames/inferno/sources/wiki/wiki_Jal_MejJak.wikitext` | [Jal-MejJak](https://oldschool.runescape.wiki/w/Jal-MejJak?oldid=15316954) | 15316954 | 2026-08-23 |
| `docs/minigames/inferno/sources/wiki/wiki_Inferno_Strategies.wikitext` | [Inferno/Strategies](https://oldschool.runescape.wiki/w/Inferno%2FStrategies?oldid=15350480) | 15350480 | 2026-09-18 |
| `docs/minigames/inferno/sources/wiki/wiki_Rocky_support.wikitext` | [Rocky support](https://oldschool.runescape.wiki/w/Rocky_support?oldid=14552065) | 14552065 | 2024-03-03 |
| `docs/minigames/inferno/sources/wiki/wiki_Ancestral_Glyph.wikitext` | [Ancestral Glyph](https://oldschool.runescape.wiki/w/Ancestral_Glyph?oldid=15350504) | 15350504 | 2026-09-18 |
| `docs/minigames/inferno/sources/wiki/wiki_Tokkul.wikitext` | [Tokkul](https://oldschool.runescape.wiki/w/Tokkul?oldid=15183440) | 15183440 | 2026-04-22 |
| `docs/minigames/inferno/sources/wiki/wiki_Mor_Ul_Rek.wikitext` | [Mor Ul Rek](https://oldschool.runescape.wiki/w/Mor_Ul_Rek?oldid=15337990) | 15337990 | 2026-09-09 |
| `docs/minigames/inferno/sources/wiki/wiki_TzHaar_Ket_Keh.wikitext` | [TzHaar-Ket-Keh](https://oldschool.runescape.wiki/w/TzHaar-Ket-Keh?oldid=15298692) | 15298692 | 2026-08-14 |
| `docs/minigames/inferno/sources/wiki/wiki_Inferno_music_track.wikitext` | [Inferno (music track)](https://oldschool.runescape.wiki/w/Inferno_(music_track)?oldid=15340435) | 15340435 | 2026-09-11 |
| `docs/minigames/inferno/sources/wiki/wiki_Combat_Achievements_Master.wikitext` | [Combat Achievements/Master](https://oldschool.runescape.wiki/w/Combat_Achievements%2FMaster?oldid=15329081) | 15329081 | 2026-09-02 |
| `docs/minigames/inferno/sources/wiki/wiki_Combat_Achievements_Grandmaster.wikitext` | [Combat Achievements/Grandmaster](https://oldschool.runescape.wiki/w/Combat_Achievements%2FGrandmaster?oldid=15321195) | 15321195 | 2026-08-26 |
| `docs/minigames/inferno/sources/wiki/wiki_Tzkal_slayer_helmet.wikitext` | [Tzkal slayer helmet](https://oldschool.runescape.wiki/w/Tzkal_slayer_helmet?oldid=15320325) | 15320325 | 2026-08-25 |
| `docs/minigames/inferno/sources/wiki/wiki_Nibblers_Begone.wikitext` | [Nibblers, Begone!](https://oldschool.runescape.wiki/w/Nibblers,_Begone%21?oldid=14771391) | 14771391 | 2024-10-13 |
| `docs/minigames/inferno/sources/wiki/wiki_Jad_What_Are_You_Doing_Here.wikitext` | [Jad? What Are You Doing Here?](https://oldschool.runescape.wiki/w/Jad%3F_What_Are_You_Doing_Here%3F?oldid=14828785) | 14828785 | 2024-12-29 |
| `docs/minigames/inferno/sources/wiki/wiki_Nibbler_Chaser.wikitext` | [Nibbler Chaser](https://oldschool.runescape.wiki/w/Nibbler_Chaser?oldid=15299777) | 15299777 | 2026-08-14 |
| `docs/minigames/inferno/sources/wiki/wiki_Inferno_Grandmaster.wikitext` | [Inferno Grandmaster](https://oldschool.runescape.wiki/w/Inferno_Grandmaster?oldid=14771468) | 14771468 | 2024-10-13 |
| `docs/minigames/inferno/sources/wiki/wiki_Facing_Jad_Head_on_II.wikitext` | [Facing Jad Head-on II](https://oldschool.runescape.wiki/w/Facing_Jad_Head-on_II?oldid=15307320) | 15307320 | 2026-08-19 |
| `docs/minigames/inferno/sources/wiki/wiki_Playing_with_Jads.wikitext` | [Playing with Jads](https://oldschool.runescape.wiki/w/Playing_with_Jads?oldid=14771505) | 14771505 | 2024-10-13 |
| `docs/minigames/inferno/sources/wiki/wiki_No_Luck_Required.wikitext` | [No Luck Required](https://oldschool.runescape.wiki/w/No_Luck_Required?oldid=14862964) | 14862964 | 2025-03-16 |
| `docs/minigames/inferno/sources/wiki/wiki_Wasn_t_Even_Close.wikitext` | [Wasn't Even Close](https://oldschool.runescape.wiki/w/Wasn't_Even_Close?oldid=14936348) | 14936348 | 2025-07-12 |
| `docs/minigames/inferno/sources/wiki/wiki_Budget_Setup.wikitext` | [Budget Setup](https://oldschool.runescape.wiki/w/Budget_Setup?oldid=15319472) | 15319472 | 2026-08-25 |
| `docs/minigames/inferno/sources/wiki/wiki_Half_Way_There.wikitext` | [Half-Way There](https://oldschool.runescape.wiki/w/Half-Way_There?oldid=14771568) | 14771568 | 2024-10-13 |
| `docs/minigames/inferno/sources/wiki/wiki_Inferno_Speed_Runner.wikitext` | [Inferno Speed-Runner](https://oldschool.runescape.wiki/w/Inferno_Speed-Runner?oldid=14771571) | 14771571 | 2024-10-13 |
| `docs/minigames/inferno/sources/wiki/wiki_The_Floor_Is_Lava.wikitext` | [The Floor Is Lava](https://oldschool.runescape.wiki/w/The_Floor_Is_Lava?oldid=15319473) | 15319473 | 2026-08-25 |
| `docs/minigames/inferno/sources/wiki/wiki_Slayer_task_TzHaar.wikitext` | [Slayer task/TzHaar](https://oldschool.runescape.wiki/w/Slayer_task%2FTzHaar?oldid=15316995) | 15316995 | 2026-08-23 |
| `docs/minigames/inferno/sources/wiki/wiki_Module_Tile_markers_Inferno_Zuk_Safespots_json.wikitext` | [Module:Tile markers/Inferno Zuk Safespots.json](https://oldschool.runescape.wiki/w/Module:Tile_markers%2FInferno_Zuk_Safespots.json?oldid=15350387) | 15350387 | 2026-09-18 |

## 8. Guides and videos (grade D; fetched 2026-10-03)

21 YouTube auto-caption transcripts, fetched with `yt-dlp --skip-download --write-auto-subs --sub-langs en-orig`, one video
at a time, converted by `tools/waves_gate/vtt_to_md.py`; the table, frame-count ranges and the overlay column are in
`docs/minigames/inferno/sources/transcripts/README.md`, every quoted statement (with video id and [mm:ss]) in
`docs/minigames/inferno/sources/transcripts/STATED.md`. A machine transcript is the lowest-ranked evidence; auto-captions
mangle names (Zuk "Zuck", Jad "Chad", mager "major", blob "block"). A narrator's number is grade D on its own and two
guides agreeing are one kind of source. A video is never promoted by itself: watch it only to read a frame against a
source line. Four full-run videos (H-Iup_IFUVc, GVnFERtla0E, r3s4rbTd4QU, -w3WGOZ_Yeo, 1.5-2.5 hours each) were read only
by keyword search; a spec worker mining wave-by-wave solves should read them. The raw `.vtt` and `.info.json` are in `build/corpus_tmp/yt/` (not in git).

| Video | Uploaded | File | For |
|---|---|---|---|
| https://www.youtube.com/watch?v=HnUr5zcF4fU | upload 2019-03-31 | docs/minigames/inferno/sources/transcripts/yt_HnUr5zcF4fU.md | xzact part 1: spawns, bat, blob, melee, ranger, mager, stacks |
| https://www.youtube.com/watch?v=uaoSaUT4SZc | upload 2019-06-04 | docs/minigames/inferno/sources/transcripts/yt_uaoSaUT4SZc.md | xzact part 2: Jad, triples, Zuk |
| https://www.youtube.com/watch?v=qnw3kGlpiuQ | upload 2021-05-21 | docs/minigames/inferno/sources/transcripts/yt_qnw3kGlpiuQ.md | Gnomonkey: stats of every monster, Zuk, pet |
| https://www.youtube.com/watch?v=6trKOSUr4EM | upload 2024-10-03 | docs/minigames/inferno/sources/transcripts/yt_6trKOSUr4EM.md | Gnomonkey 2024: pillars 255, dig, respawn |
| https://www.youtube.com/watch?v=H-Iup_IFUVc | upload 2025-02-26 | docs/minigames/inferno/sources/transcripts/yt_H-Iup_IFUVc.md | Gnomonkey 2025: full run (not mined line by line) |
| https://www.youtube.com/watch?v=bybdjfCgcG4 | upload 2026-03-02 | docs/minigames/inferno/sources/transcripts/yt_bybdjfCgcG4.md | Kamahkaze: ticks, blob, melee, Zuk |
| https://www.youtube.com/watch?v=BbJCzcMcMbE | upload 2025-10-06 | docs/minigames/inferno/sources/transcripts/yt_BbJCzcMcMbE.md | Kaoz: entry, monsters, Jad, Zuk |
| https://www.youtube.com/watch?v=oKVmC7Rb1BY | upload 2025-07-25 | docs/minigames/inferno/sources/transcripts/yt_oKVmC7Rb1BY.md | Rob: 1-tick alternate, melee safespots |
| https://www.youtube.com/watch?v=7gb8lDjX3hQ | upload 2025-05-05 | docs/minigames/inferno/sources/transcripts/yt_7gb8lDjX3hQ.md | Rob: full run, Zuk |
| https://www.youtube.com/watch?v=GVnFERtla0E | upload 2026-08-05 | docs/minigames/inferno/sources/transcripts/yt_GVnFERtla0E.md | Rob: mage-tank run (not mined line by line) |
| https://www.youtube.com/watch?v=EMqBmTq6Rbo | upload 2024-04-07 | docs/minigames/inferno/sources/transcripts/yt_EMqBmTq6Rbo.md | VideoGameBot part 1: blob, flinch, melee |
| https://www.youtube.com/watch?v=yT-YtEZ7tsU | upload 2024-04-11 | docs/minigames/inferno/sources/transcripts/yt_yT-YtEZ7tsU.md | VideoGameBot part 3: triples, Zuk |
| https://www.youtube.com/watch?v=E08It1hMHeg | upload 2023-06-03 | docs/minigames/inferno/sources/transcripts/yt_E08It1hMHeg.md | aatykon: wave by wave |
| https://www.youtube.com/watch?v=c47wPpvPVJs | upload 2021-03-17 | docs/minigames/inferno/sources/transcripts/yt_c47wPpvPVJs.md | aatykon: blob/melee dig reset |
| https://www.youtube.com/watch?v=Q46vyoHfF4Q | upload 2023-01-05 | docs/minigames/inferno/sources/transcripts/yt_Q46vyoHfF4Q.md | Nairy: monsters, hp, max hit |
| https://www.youtube.com/watch?v=r3s4rbTd4QU | upload 2026-05-13 | docs/minigames/inferno/sources/transcripts/yt_r3s4rbTd4QU.md | dearlola1: full run (not mined line by line) |
| https://www.youtube.com/watch?v=-w3WGOZ_Yeo | upload 2025-08-30 | docs/minigames/inferno/sources/transcripts/yt_-w3WGOZ_Yeo.md | Reynold: mage-tank run (not mined line by line) |
| https://www.youtube.com/watch?v=XMYWmidc6hA | upload 2025-04-04 | docs/minigames/inferno/sources/transcripts/yt_XMYWmidc6hA.md | triple Jad cadence |
| https://www.youtube.com/watch?v=2D4Zrp5iN3Y | upload 2026-07-30 | docs/minigames/inferno/sources/transcripts/yt_2D4Zrp5iN3Y.md | RS Mina: blob cycle |
| https://www.youtube.com/watch?v=nppNxCucrY0 | upload 2023-02-14 | docs/minigames/inferno/sources/transcripts/yt_nppNxCucrY0.md | flick vocabulary |
| https://www.youtube.com/watch?v=uP3DU21K3xE | upload 2026-04-23 | docs/minigames/inferno/sources/transcripts/yt_uP3DU21K3xE.md | Zuk healers safe spot |

## 9. Unavailable

Every source no worker could get, and why. None is filled in from memory.

| Source | Why |
|---|---|
| oldschool.runescape.wiki `TzKal-Zuk/Strategies` | no such page (MISSING in `docs/minigames/inferno/sources/wiki/manifest.tsv`); the Zuk strategy is the TzKal-Zuk section of `Inferno/Strategies`, pinned |
| A Tokkul-by-wave table | no pinned wiki page or newspost states one; only the 16,440 maximum (doubled with the elite Karamja diary) in `docs/minigames/inferno/sources/wiki/wiki_Inferno.wikitext:298` |
| Mod Ash tweets cited by the wiki (Jal-Zek revive 1/10, Yt-HurKot heal 15-24 per 4 ticks, Zuk single damage roll) and the "Cited tweets" of section 1 | not fetched; quoted second-hand from the wiki, archive urls on the pinned pages |
| Combat Achievement task rows from the bucket | the tier pages are bucket-rendered and list no rows; the twelve task pages (monster = TzKal-Zuk) are pinned instead |
| Fight Cave and TzHaar-Ket-Rak's Challenges wiki pages | outside the brief, not pinned (their newsposts are pinned); `docs/minigames/FIGHT_CAVES.md` exists |
| Any pinned wiki or newspost line for: the revived monster's first-attack delay, the shield's speed in tiles per tick, the spawn tile list, Zuk's first attack tick, the entry cutscene's camera and length, sound or graphic ids | no pinned source states them (LEDGER_wiki.md "Unavailable or not stated") |
| Practice mode (a source for its rules) | no pinned wiki page or newspost states a practice mode's rules (the "Inferno practice" newspost search, 14 hits, pinned none that do); the content has a fee file only and the cache has no interface for it (LEDGER_cache.md "Negative results") |
| Blert: Zuk shield, healer, pillar-damage, revive-target and projectile data | the recorder has no event for them (`docs/minigames/inferno/sources/blert/PROVENANCE.md` section 1) |
| Blert: `INFERNO_WAVE_START` stream event and the stage-end tick | dropped / not served by the API; wave start only via the per-wave record |
| Blert: complete streams for challenges `c8d56ab9` (waves 1-32) and `dd5b1591` (waves 1-28) | left partial on purpose, 150-request budget |
| Blert website pages and any host other than blert.io's API and GitHub | not fetched |
| `github.com/propagating/zuksharp` | HTTP 404 (named in the ZukPrayer README; not public under that name) |
| detuks.com AUTOZUK write-up and the AUTOZUK YouTube video | not GitHub; not fetched by the code worker; triple_jad_sim cites the write-up for "monsters spawn on tick 15" |
| ZukPrayer source and bradyp30/Zuk-Timer code | no licence file: quoted (README / timer.js lines), not copied |
| Hub plugins infernalfc, inferno-splits-logger, npc-attack-tick-timer, tzhaar-hp-tracker, inferno-autosplitter, inferno-tracker, inferno-wave-splits | not opened (disabled, splits or trackers, no mechanics) |
| Kronos and LostCity server code | not fetched on purpose (LEDGER_code.md section 3) |
| RuneLite `WorldArea.hasLineOfSightTo` (what kotori calls) | not pinned; the client's own line-of-sight implementation |
| Videos `UpOw3xB9Bjo` (BTSRS, Inferno: Blob Mechanics Explained), `zTQdupqm-lM` (Hug my cat, 2 Tick flick guide), `a_0ze3AS7Ak` (Edward OSRS, same-ticked mager/ranger and melee) | no auto-subtitles for en-orig / en |
| A frame-by-frame reading of any video | none pinned; a video is not a source for a number (grade D, section 8) |
| Binary cache (`cache.osrs239`) | not read; the dumps quote the unpacked text form |

## 10. Where the sources disagree

One row per disagreement seen across the four "what this part states" lists and `CODE_CONSTANTS.md`. **No verdict**:
each source's value is quoted with file and line, and the spec workers settle the row from the lines. Paths are under
`docs/minigames/inferno/sources/` unless they begin `OSRS-Content/`. "C0nn" = the row of `CODE_CONSTANTS.md`.
"Tree" = our content, `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno/configs/inferno.constant`; it came from a private-server
port and is not a source.

| # | Quantity | Source values (file:line) |
|---|---|---|
| D01 | Pillar collapse damage after wave 66 | wiki Inferno: half the player's current hp within one tile (`wiki/wiki_Inferno.wikitext:264`); wiki Inferno/Strategies: flat 49 within one tile (`wiki/wiki_Inferno_Strategies.wikitext:514`); AUTOZUK: floor(hp/2) (`autozuk/index.html:770,1174`, C025) |
| D02 | Nibbler damage to a pillar | trainer and AUTOZUK 0..4 per hit, one hit per 4 ticks (`infernotrainer/src/content/inferno/js/mobs/JalNib.ts:12`, `autozuk/index.html:900`, C023); wiki max hit 4 (`wiki/wiki_Jal_Nib.wikitext:12`); triple_jad_sim "2-4" (`triple_jad_sim/`, via C023) |
| D03 | Yt-HurKot heal on Jad | wiki 15-24 every 4 ticks (`wiki/wiki_Yt_HurKot.wikitext:70`; Mod Ash tweet 2020-12-30, second-hand); trainer random 0..19 (`infernotrainer/src/content/inferno/js/mobs/YtHurKot.ts:16`, C065); Tree quotes the wiki (`configs/inferno.constant:364`) |
| D04 | Jal-MejJak heal on Zuk | wiki 15-24 every three ticks (`wiki/wiki_Jal_MejJak.wikitext:44`); trainer random 0..24 (`infernotrainer/src/content/inferno/js/mobs/JalMejJak.ts:22`, C080) |
| D05 | Zuk maximum hit | wiki 148, a single roll up to the average of mage max 128 and ranged max 169 (`wiki/wiki_TzKal_Zuk.wikitext:17`); trainer 251 (`infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:223`, C072); Kronos 251 (LEDGER_code.md section 3: already overridden by the wiki figure) |
| D06 | Zuk's first attack tick | trainer attackDelay 14 plus stun 8 at spawn (`infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:72,231`); kotori 12 once the shield reaches its corner, marked "TODO: Could be 10 or 11. Test!" (`kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:513`, C071); Blert has Zuk's attack gap (10 or 7) but its first-attack n is not listed (`blert/SAMPLE_SUMMARY.md` line 2744) |
| D07 | First Zuk add set (ranger + mager) | trainer 72 ticks (`infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:66-177`, C075-C079); bradyp30 Zuk-Timer 51 s from the cutscene (`LEDGER_code.md` section 1; file not copied); Tree 60 (`configs/inferno.constant:312`); wiki "on a repeating 3:30 minute timer" with a pause at 600 and +1:45 (`wiki/wiki_Inferno.wikitext:287-291`) and "approximately 3 minutes and 30 seconds after the previous set" (`wiki/wiki_Inferno_Strategies.wikitext:712`) |
| D08 | Glyph pause at each end | trainer freeze(5) (`infernotrainer/src/content/inferno/js/ZukShield.ts:149`); kotori 4 (`kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:761`), OpenOSRS 4 (`openosrs_inferno/inferno/src/main/java/net/runelite/client/plugins/inferno/InfernoPlugin.java:759`); wiki "about 5 ticks" (`wiki/wiki_Ancestral_Glyph.wikitext:34-36`); Tree 4 (`configs/inferno.constant:257`) (C082) |
| D09 | Melee dig trigger | wiki: digs 50 ticks after wave start, then every 40-60 ticks, none in the first 30 (`wiki/wiki_Jal_ImKot.wikitext:47-49`); trainer and AUTOZUK: no line of sight and attackDelay <= -38 (10% per tick) or <= -50 (`infernotrainer/src/content/inferno/js/mobs/JalImKot.ts:113`, `autozuk/index.html:871`, C042); guide Gnomonkey "20 s" (`transcripts/STATED.md`, qnw3kGlpiuQ [0:14:47]) against 30 s / 50 ticks (bybdjfCgcG4 [0:18:36], 6trKOSUr4EM [0:12:43], BbJCzcMcMbE [0:13:17]) |
| D10 | Melee delay after resurfacing | wiki six ticks (`wiki/wiki_Jal_ImKot.wikitext:47`); trainer 6 frozen then attackDelay 6 (`infernotrainer/src/content/inferno/js/mobs/JalImKot.ts:113-175`); kotori 12 ticks from burrow animation 7600 (`kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:288`, C043); guide "three or four ticks" (`transcripts/STATED.md`, EMqBmTq6Rbo [0:59:28]) |
| D11 | Blob: ticks from reading the prayer to the attack | trainer 3 (`infernotrainer/src/content/inferno/js/mobs/JalAk.ts:139-153`, C034); kotori 3 or 4 depending on whether it came out of a safespot (`kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:308-320`, C035); wiki "reads 3 ticks before each 6-tick attack" (`wiki/wiki_Jal_Ak.wikitext:49,55`); guides disagree between a two-tick and a three-tick gap (`transcripts/STATED.md`); Blert records attacks, not the scan (modal blob gap 6, `blert/SAMPLE_SUMMARY.md` line 2744) |
| D12 | Blob style when no overhead prayer is up | 50/50 in the trainer only (`infernotrainer/src/content/inferno/js/mobs/JalAk.ts:139-153`, C034); wiki: uses the opposite style (`wiki/wiki_Jal_Ak.wikitext:49`) |
| D13 | Ranger projectile hit delay | trainer floor((3+d)/6)+1+2 (`infernotrainer/src/content/inferno/js/mobs/JalXil.ts:56`, `osrs_sdk/src/sdk/weapons/RangedWeapon.ts:25`); AUTOZUK table d1-5: 3, d6-9: 4, d10-12: 5, d13+: 6 (`autozuk/index.html:450`) (C048) |
| D14 | Ranger attack range | trainer and AUTOZUK 15 tiles (`infernotrainer/src/content/inferno/js/mobs/JalXil.ts:105`, `autozuk/index.html:377`); kotori 98 (`kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:397`) (C046) |
| D15 | Mager revive: which corpse | trainer random (`infernotrainer/src/content/inferno/js/mobs/JalZek.ts:204`, `infernotrainer/src/content/inferno/js/InfernoMobDeathStore.ts:9`); AUTOZUK oldest first (`autozuk/index.html:962`); wiki says only "defeated monsters ... once ... half health" (`wiki/wiki_Jal_Zek.wikitext:48-50`) (C053) |
| D16 | Revived monster's first attack | trainer attackSpeed (`infernotrainer/src/content/inferno/js/mobs/JalZek.ts:207`); AUTOZUK attackSpeed + 1 (`autozuk/index.html:907`); mager acts again after 8 in trainer, AUTOZUK and kotori (`kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:293`) (C054) |
| D17 | Jad attack gap | wiki speed 8, 9 on wave 68 (`wiki/wiki_JalTok_Jad.wikitext:46-52`); trainer 8 or 9 (`infernotrainer/src/content/inferno/js/InfernoRegion.ts:450-478`, `infernotrainer/src/content/inferno/js/mobs/JalTokJad.ts:33`); kotori 8 only (`kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:221`); Blert observed jad ranged gap 9 in 356 of 426, gap 8 in 58, mage gap 9 in 371 of 438 (`blert/SAMPLE_SUMMARY.md` line 2744; the sample is not split by wave 67 / 68, and Zuk's own Jad gap is 8, n=141) |
| D18 | Jad healer count and trigger | wiki wave 67 one Jad + 5 healers, wave 68 three Jads + 3 healers each (`wiki/wiki_JalTok_Jad.wikitext:46-52`; healers at or below 50% hp, `wiki/wiki_Yt_HurKot.wikitext:70`); trainer 5 / 3 (`infernotrainer/src/content/inferno/js/InfernoRegion.ts:450-478`); kotori 3 (`kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:205`) |
| D19 | Wave-complete delay before the next wave | trainer 9 ticks after the last death, "its own choice" (`infernotrainer/src/content/inferno/js/InfernoRegion.ts:687`, C018); Tree 8 (`configs/inferno.constant:11`); Blert server `WAVE_INTERVAL_TICKS = 6` (`blert/blert/challenge-harder/src/processing/inferno.rs:26`, asserted) and the timer begins 10 ticks before wave 1 (`blert/plugin/src/main/java/io/blert/challenges/inferno/InfernoChallenge.java:60`) |
| D20 | Spawn tiles (nine) | scouter, region 9043: (18,41) (39,41) (20,35) (40,34) (33,29) (22,23) (40,21) (18,18) (32,18) (`inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:1091`); trainer and AUTOZUK repeat them after a frame transform (verified by script, `CODE_CONSTANTS.md` header); Tree `spawn_5` (21,27) vs scouter (22,23) (`configs/inferno.constant:46-63`); Blert server assumes a different list in world coordinates (`blert/blert/challenge-harder/src/processing/inferno.rs:28-47`, asserted); Blert observed spawn tiles per wave (`blert/SAMPLE_SUMMARY.md` line 77 onward) |
| D21 | Spawn-to-tile assignment rule | trainer: shuffle, then mager, ranger, melee, blob, bat (`infernotrainer/src/content/inferno/js/InfernoWaves.ts:26-80`); wiki: random per wave, saved per run, no tile list (`newsposts/wiki_Update_Progress_Report_The_Inferno.wikitext:66-68`); Blert's spawn index is observed per wave (`blert/SAMPLE_SUMMARY.md` line 2993 onward); "spawn on tick 15 since login" is one AUTOZUK / ZukPrayer lineage (`zukprayer/UPSTREAM_README.md:5`) |
| D22 | Retaliation delay | trainer and SDK floor(speed/2)+1 (`osrs_sdk/src/sdk/Unit.ts:363`, `infernotrainer/src/content/inferno/js/mobs/JalTokJad.ts:229`); triple_jad_sim says the wiki is ceil(speed/2)+1 (C089; the wiki line is not located) |
| D23 | Pillar tiles and ids | wiki tiles (2257,5349) (2274,5351) (2267,5335), scenery 30284-30287 (`wiki/wiki_Rocky_support.wikitext:22-30`); Blert plugin the same tiles (`blert/plugin/src/main/java/io/blert/challenges/inferno/Pillar.java:43-45`); gameval locs 30353-30355 (`runelite_gameval_NpcID_inferno_excerpt.txt`; the cache places no rocky-support locs, `cache_locs.txt`, LEDGER_cache.md "Negative results") |
| D24 | Mager maximum hit | wiki 70 magic, 52 melee (`wiki/wiki_Jal_Zek.wikitext:11`); guides 70 (qnw3kGlpiuQ) or 71 (Q46vyoHfF4Q) (`transcripts/STATED.md`) |
| D25 | Bloblet hit points | wiki 15 (`wiki/wiki_Jal_AkRek_Ket.wikitext:19`); Blert plugin table 15 (`blert/plugin/src/main/java/io/blert/challenges/inferno/InfernoNpc.java:33-76`, asserted); guides 15 (Gnomonkey) or 20 (Nairy) (`transcripts/STATED.md`) |
| D26 | Jal-Xil and Jal-MejJak hit points | wiki 125 and 75 after the 2021-06-30 cut of 5 (`wiki/wiki_Jal_Xil.wikitext:59`, `newsposts/wiki_Update_Equipment_Rebalance_Ranged_Meta_Proposal.wikitext:1081`); Blert plugin ranger 125 (`blert/plugin/src/main/java/io/blert/challenges/inferno/InfernoNpc.java:33-76`); Kronos 130 and 80 (LEDGER_code.md section 3: overridden) |
| D27 | Pet drop rate | wiki 1/100, 1/75 on a Slayer task (`wiki/wiki_Inferno.wikitext:300`); guide Gnomonkey 1/100, 1/50 on task, 1/100 with the cape sacrificed (`transcripts/STATED.md`, qnw3kGlpiuQ [0:44:44]) |
| D28 | Number of waves and the wave table | wiki Inferno and Strategies: 68 rows, identical (`wiki/wiki_Inferno.wikitext:177-275`), wave 69 is Zuk; kotori, OpenOSRS and trainer identical tables, 69 waves (C010, C011); Blert server `WAVES: [&[u32]; 69]` (`blert/blert/challenge-harder/src/processing/inferno.rs:47`) and the plugin advances while `wave < 69` (`blert/plugin/src/main/java/io/blert/challenges/inferno/InfernoChallenge.java:143`); the 2016 dev blogs say 40+ (`newsposts/wiki_Update_Dev_Blog_The_Inferno_v2.wikitext:29`, superseded). Blert observed tick-0 sets agree with the wiki on waves 1-27 checked (`blert/SAMPLE_SUMMARY.md` line 3) |
| D29 | Nibbler counts per wave | wiki: 210 nibblers over waves 1-68, six alone on waves 3, 8, 17, 34 (`wiki/wiki_Inferno.wikitext:177-275`); Blert observed tick-0 nibbler counts (`blert/SAMPLE_SUMMARY.md` line 3; the worker notes the recorder saw them spawn on tick 0, to be settled against the wiki per wave) |
| D30 | Combat-stat drain vs the bat | wiki: no stat drain when Protect from Missiles (2021-06-30), run drain blockable by prayer since 2025-04-16 (`wiki/wiki_Jal_MejRah.wikitext:49-53`, `newsposts/wiki_Update_Easter_Event.wikitext:25`); guide Gnomonkey: run drain 3 per hit, stat drain 1, not if praying range (`transcripts/STATED.md`, qnw3kGlpiuQ [0:12:03]-[0:12:37]) |

## 11. Units (proposed final cut)

The cut follows `docs/WAVES_ORCHESTRATOR.md` section 7 and what the corpus actually covers. Source tags: **NP** newsposts,
**cache** the dumps, **BL** Blert (observed events only), **code** plugins and simulators (`CODE_CONSTANTS.md` rows),
**wiki**, **guides**. A unit whose best evidence is code + wiki cannot reach grade B: the unit's spec row says so.

| Unit id | What it measures; sources |
|---|---|
| `inferno_wave_table` | Waves 1-69: which monsters each wave carries, the nine spawn tiles, the rule assigning them, acting order, the six-nibbler in-between waves, wave-complete delay and the wave-start tick. wiki table `wiki/wiki_Inferno.wikitext:177-275`, code C010/C011 (three identical tables), BL tick-0 sets and spawn tiles (grade B), NP `Progress_Report_The_Inferno:66-68`; D19-D21, D28-D29 |
| `inferno_entry_and_cape` | Mor Ul Rek entry, the one-time sacrifice of a Fire Cape, no teleports, floor items 30 minutes, entry cutscene. NP (grade A rows in section 1), wiki, cache (`inferno_entrance` 30352, interfaces); cutscene camera and length have no source (section 9) |
| `inferno_nibblers_and_pillars` | Jal-Nib hp, 4-tick attack, targeting the pillars; three pillars at 255 hp, scenery and tiles, collapse damage and radius after wave 66. wiki, cache npc, BL observed nibbler/bat spawns (not pillar damage: no event), code C020-C025; D01-D02, D23 |
| `inferno_bat` | Jal-MejRah: 3-tick cadence, range, max hit 19, run-energy drain and stat drain, 2021 and 2025 changes. NP (grade A), wiki, BL bat gap 3 (n=1872), guides; D30 |
| `inferno_blob_and_splits` | Jal-Ak: 6-tick cycle, the prayer scan, opposite-style rule, the three splits and their first attack, double-blob waves. wiki, BL gaps 6 and 4 and first-attack-after-spawn 3 (grade B), code C034/C035, guides; D11-D12, D25 |
| `inferno_melee` | Jal-ImKot: max 49, 4-tick cadence, dig trigger, six-tick resurfacing delay, no damage while digging (NP 2018-03-15). NP, wiki, BL meleer gap 4 and dig animation 7600, code C042/C043, guides; D09-D10 |
| `inferno_ranger` | Jal-Xil: 4-tick cadence, range, 46 max, projectile flight and hit delay, damage taken at animation start. wiki, BL ranger gap 4 (projectile events decide D13), code C046/C048; D13-D14, D26 |
| `inferno_mager_resurrection` | Jal-Zek: 4-tick cadence, max 70/52, the revive chance, once per monster, half hp, nibblers and bloblets excluded, 8-tick wait, no move while reviving (NP 2018-03-15), none on wave 69. NP, wiki, BL mager gap 4 and resurrect animation 7611 (the revive target has no event), code C052-C054; D15-D16, D24 |
| `inferno_mixed_late_waves` | Waves 35-66 composites (ranger + mager, double mager, blob + melee stacks), line of sight round the pillars, the pillar safespots and the stack. wiki wave table and TECHNIQUES.md, BL per-wave sets and wave records, code (LOS), guides |
| `inferno_single_jad` | Wave 67: one JalTok-Jad, attack tells and ticks to hit per style, speed 8, the five Yt-HurKot healers at 50% hp and their heal. wiki, BL jad gaps and healer gap 4 (grade B), code C056-C065, guides, NP; D03, D17-D18 |
| `inferno_triple_jad` | Wave 68: three Jads, speed 9, the stagger offsets (3 ticks), three healers each, the 30 s CA window. wiki, BL (wave 68 events), code (stun offsets [1,4,7]), guides; D17-D18 |
| `inferno_zuk_glyph_shield` | The Ancestral Glyph: 600 hp, 5 wide, path, speed, pause at the ends, the safespot tiles and the direction fixed per run (NP 2018-03-15). wiki, code (C081-C091), runemarkers; **no Blert data** (no event): ceiling grade C; D08 |
| `inferno_zuk_fight` | TzKal-Zuk: 1200 hp, max hit, 10-tick cadence and 7 when enraged, first attack, flight, what blocks it. wiki, BL Zuk gap 10/7 (observed), code C070-C072, guides; D05-D06 |
| `inferno_zuk_sets_and_healers` | The timed set (ranger + mager every 3:30, pause at 600 with +1:45), the Jad at 480 hp, four Jal-MejJak at 240 with their heal and sparks, the enrage, wave 69 spawn timing. wiki, BL set-monster first attacks (zuk_mager 6, zuk_ranger 8, zuk_jad 6-7), code C075-C080, guides; D04, D07 |
| `inferno_pause_and_logout` | Logging out between waves, no logout at a wave start (NP 2018-03-15), spawns saved per run, the tick-15 claim, teleports barred. NP (grade A), wiki `wiki/wiki_Inferno_Strategies.wikitext:680`, code (ZukPrayer, one lineage); D21 |
| `inferno_death_and_failure_reward` | Death in the Inferno (safe death per NP), the Tokkul for a failed run by wave, the 16,440 maximum and the elite-diary doubling. NP, wiki `wiki/wiki_Inferno.wikitext:298`; the by-wave table is unavailable (section 9) |
| `inferno_completion_reward_and_pet` | The infernal cape, its repair cost (NP 2023-09-20), the Jal-Nib-Rek pet roll, cape exchange 1/100, Slayer 100,000 xp (NP 2017-08-03), the broadcast, the twelve Combat Achievements (ids 341-352). NP, wiki, guides; D27 |
| `inferno_practice_mode` | The practice mode the content has a fee for. **No corpus source states its rules** (section 9); the unit is held until a source is found, or cut to a content-only check |
| `inferno_presentation_av` | Each monster's spawn, attack and death sequences, graphics and projectiles, sounds, music, the wave-complete message, the entrance. cache dumps (`cache_seq.txt`, `cache_spotanim.txt`, `cache_sounds.txt`, `cache_music.txt`), `docs/minigames/inferno/AV_INVENTORY.tsv`, `docs/INFERNO_SOUNDS.md` (a reconstruction; the cache has no frame sound), `docs/BOSS_ASSETS.md`; the video frames are judged by crop, never by claim |

Techniques (blob flick, off-tick ranger + mager, corner trapping, healer tagging, shield walk) are rows inside the unit that
owns the monster, sourced from `docs/minigames/inferno/sources/wiki/TECHNIQUES.md`, not units of their own.
