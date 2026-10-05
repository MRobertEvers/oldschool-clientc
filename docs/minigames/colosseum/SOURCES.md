# Fortis Colosseum -- downloaded sources

Everything the waves loop's Colosseum spec pass will cite, pulled down on
**3 October 2026** (the Blert sample's requests are stamped 4 October 2026 UTC,
which is the same evening in local time). Index of the corpus pass
`matthew-mbp-m4-waves-b1-spec-colosseum`; the four part reports are
`build/spec_state/matthew-mbp-m4-waves-b1-spec-colosseum/corpus.{wiki,blert,code,guides}.json`
and their ledgers are `sources/LEDGER_<part>.md` (all paths below are relative to
`docs/minigames/colosseum/` unless they start with `tools/` or `build/`).

Nothing in `sources/` is authored by this project except the cache extracts and
the rig tables, which are mechanical dumps of `OSRS-Content/osrs239-content/`, and
the id maps, which are tables of file:line quotes. Treat all of it as evidence,
not as specification. Where two sources disagree, section 10 says so and gives no
verdict: the spec workers settle each from the source line. Where no source
settles a number, the plan raises an `[Mn]` task instead of inventing one. The
2004 source (LostCity) is not a source in this loop (owner ruling 2026-10-03):
neither wave minigame existed in 2004, and nothing in this folder was read from it.

## 0. The ranking

Grades are the standard's (WAVES_ORCHESTRATOR.md section 3): A is Jagex or the
cache, B is Blert's distribution with our server's inside it, C is two independent
non-recorder sources, D is one source, E a disclosed approximation. First-party data
ranks first: the Jagex newsposts (grade A for the numbers they state) and the
rev-239 cache (ids, stats, animation lengths, the modifier table in enum 5312).
Next come the live-game recordings, the Blert sample (grade B, but only its
OBSERVED rows; what the plugin asserts is `[plugin]` and carries no grade; read
`sources/blert/PROVENANCE.md` first). Then the pinned wiki (the only place the wave
table, the drop tables and the Glory rules are written down; one volunteer source,
so grade D alone and C once an independent source agrees), the RuneLite plugins and
simulators (grade D, and two that share a schema are one source), reference servers
(none fetched), and last the guides and videos (grade D, machine transcripts nobody
has watched). A number that rests on one D source is raised as an `[Mn]`; a video
promotes nothing on its own.

What the corpus lacks, and where each gap sits: the cache has no wave table and no
reward table (inventory: `LEDGER_cache.md`); the wiki has no animation,
projectile or graphic ids and no spawn tiles; Blert records no hit, projectile,
damage, prayer, reward roll, pet or Minimus walk-in; no pinned code states a spawn
tile or the reinforcement tick; no sampled guide states the reward pool or the
Glory rules.

---

## 1. Jagex newsposts (grade A) -- pinned from the wiki's `Update:` pages

Fetched **2026-10-03** by one worker from `oldschool.runescape.wiki` only (the host
is one worker's), raw wikitext pinned by revision with `tools/toa_fetch_wiki.py`
behind a one-request-per-1.2-second throttle (the stock tool sleeps 0.8 s). A
file is byte-identical to its `?oldid=` permalink. Search calls (`list=search`,
namespace 112, 1.2 s apart) and fetch logs are in `LEDGER_wiki.md`. 39 posts are
pinned in `sources/newsposts/` (`manifest.tsv` has 39 rows; the wiki part's JSON
says 38, the directory has 39 files: trust the directory); 25 of them carry a
quoted number, the rest are mention-only. No Colosseum beta posts exist: the
pre-release posts are the Poll Blog and First Look, which are proposals.

Rules for reading the quotes below (from the part's own header): the April 2024
drop-table posts number the roll by the wave **just completed**, the chest page by
the wave whose **reward** it is (same nine rows, shifted by one); the
`Varlamore_Tweaks_Drop_Rates` table is the superseded rate; the First Look and the
Poll Blog are proposals, quoted only to date a number. A quote below is
`KEY:LINE`, where KEY is the file name minus `wiki_Update_` and `.wikitext`, in
`sources/newsposts/`. Every quote is the whole source line, cut at 380 characters.

### 1.1 Pinned posts

| File | Page | Revision | Revision stamp |
|---|---|---:|---|
| `newsposts/wiki_Update_A_PoH_Recode_More_Fixes.wikitext` | [Update:A PoH Recode & More Fixes](https://oldschool.runescape.wiki/?oldid=14978337) | 14978337 | 2025-09-02 |
| `newsposts/wiki_Update_Colosseum_NPC_Clickboxes_More.wikitext` | [Update:Colosseum NPC Clickboxes & More](https://oldschool.runescape.wiki/?oldid=14692012) | 14692012 | 2024-07-02 |
| `newsposts/wiki_Update_Continent_Expansion.wikitext` | [Update:Continent Expansion](https://oldschool.runescape.wiki/?oldid=11840202) | 11840202 | 2020-03-16 |
| `newsposts/wiki_Update_Deadman_Annihilation_Everything_You_Need_To_Know.wikitext` | [Update:Deadman: Annihilation - Everything You Need To Know](https://oldschool.runescape.wiki/?oldid=15119418) | 15119418 | 2026-02-03 |
| `newsposts/wiki_Update_Deadman_Armageddon_First_Look.wikitext` | [Update:Deadman: Armageddon - First Look](https://oldschool.runescape.wiki/?oldid=14700987) | 14700987 | 2024-07-15 |
| `newsposts/wiki_Update_Defender_of_Varrock_Varlamore_Rewards_More.wikitext` | [Update:Defender of Varrock, Varlamore Rewards & More](https://oldschool.runescape.wiki/?oldid=14543026) | 14543026 | 2024-01-31 |
| `newsposts/wiki_Update_Demonic_Pacts_Overview.wikitext` | [Update:Demonic Pacts - Overview](https://oldschool.runescape.wiki/?oldid=15170657) | 15170657 | 2026-04-08 |
| `newsposts/wiki_Update_Dev_Blogs_Zeah_Achievement_Diaries.wikitext` | [Update:Dev Blogs: Zeah & Achievement Diaries](https://oldschool.runescape.wiki/?oldid=11840436) | 11840436 | 2020-03-16 |
| `newsposts/wiki_Update_Easter_Varlamore_Updates.wikitext` | [Update:Easter & Varlamore Updates](https://oldschool.runescape.wiki/?oldid=14614006) | 14614006 | 2024-03-27 |
| `newsposts/wiki_Update_Fortis_Colosseum_First_Look_Rewards.wikitext` | [Update:Fortis Colosseum - First Look & Rewards](https://oldschool.runescape.wiki/?oldid=14485094) | 14485094 | 2023-10-25 |
| `newsposts/wiki_Update_Further_Leagues_VI_Changes.wikitext` | [Update:Further Leagues VI Changes](https://oldschool.runescape.wiki/?oldid=15208092) | 15208092 | 2026-05-08 |
| `newsposts/wiki_Update_Further_Project_Rebalance_Skilling_Varlamore_Changes.wikitext` | [Update:Further Project Rebalance (Skilling) & Varlamore Changes](https://oldschool.runescape.wiki/?oldid=14660915) | 14660915 | 2024-05-15 |
| `newsposts/wiki_Update_Game_Jam_Charges_QoL.wikitext` | [Update:Game Jam: Charges QoL](https://oldschool.runescape.wiki/?oldid=14859778) | 14859778 | 2025-03-08 |
| `newsposts/wiki_Update_Gielinor_Gazette_October_2023.wikitext` | [Update:Gielinor Gazette - October 2023](https://oldschool.runescape.wiki/?oldid=14491552) | 14491552 | 2023-10-31 |
| `newsposts/wiki_Update_Gielinor_Gazette_October_2024.wikitext` | [Update:Gielinor Gazette - October 2024](https://oldschool.runescape.wiki/?oldid=14786611) | 14786611 | 2024-10-25 |
| `newsposts/wiki_Update_Interface_Uplift_Round_2.wikitext` | [Update:Interface Uplift Round 2](https://oldschool.runescape.wiki/?oldid=15002330) | 15002330 | 2025-10-11 |
| `newsposts/wiki_Update_Leagues_VI_Demonic_Pacts_Fixes_Issues.wikitext` | [Update:Leagues VI: Demonic Pacts - Fixes & Issues](https://oldschool.runescape.wiki/?oldid=15207358) | 15207358 | 2026-05-06 |
| `newsposts/wiki_Update_Leagues_V_Teasers_FAQs_Releasing_November_27th.wikitext` | [Update:Leagues V Teasers & FAQs - Releasing November 27th](https://oldschool.runescape.wiki/?oldid=14808166) | 14808166 | 2024-11-25 |
| `newsposts/wiki_Update_More_Doom_Tweaks_Poll_84_Summer_Sweep_Up_Changes.wikitext` | [Update:More Doom Tweaks, Poll 84, & Summer Sweep Up Changes](https://oldschool.runescape.wiki/?oldid=14970813) | 14970813 | 2025-08-21 |
| `newsposts/wiki_Update_Permanent_Deadman_World_345_Improvements.wikitext` | [Update:Permanent Deadman: World 345 Improvements](https://oldschool.runescape.wiki/?oldid=14861545) | 14861545 | 2025-03-12 |
| `newsposts/wiki_Update_Pet_Insurance_Rework_More.wikitext` | [Update:Pet Insurance Rework & More](https://oldschool.runescape.wiki/?oldid=14983410) | 14983410 | 2025-09-10 |
| `newsposts/wiki_Update_Poll_Blog_Fortis_Colosseum_x_Perilous_Moons.wikitext` | [Update:Poll Blog: Fortis Colosseum x Perilous Moons](https://oldschool.runescape.wiki/?oldid=14492766) | 14492766 | 2023-11-03 |
| `newsposts/wiki_Update_Pride_2024.wikitext` | [Update:Pride 2024](https://oldschool.runescape.wiki/?oldid=14674214) | 14674214 | 2024-06-05 |
| `newsposts/wiki_Update_QoL_Improvements_Further_Deadman_Armageddon_Tweaks.wikitext` | [Update:QoL Improvements & Further Deadman: Armageddon Tweaks](https://oldschool.runescape.wiki/?oldid=14711204) | 14711204 | 2024-08-01 |
| `newsposts/wiki_Update_Undead_Pirates_Colosseum_Changes_more.wikitext` | [Update:Undead Pirates, Colosseum Changes & more!](https://oldschool.runescape.wiki/?oldid=14630395) | 14630395 | 2024-04-12 |
| `newsposts/wiki_Update_Undead_Pirates_Tweaks_Varlamore_CAs_More.wikitext` | [Update:Undead Pirates Tweaks, Varlamore CAs & More](https://oldschool.runescape.wiki/?oldid=14674093) | 14674093 | 2024-06-05 |
| `newsposts/wiki_Update_Varlamore_Part_One.wikitext` | [Update:Varlamore: Part One](https://oldschool.runescape.wiki/?oldid=14606906) | 14606906 | 2024-03-22 |
| `newsposts/wiki_Update_Varlamore_Part_One_Overview.wikitext` | [Update:Varlamore: Part One - Overview](https://oldschool.runescape.wiki/?oldid=14553839) | 14553839 | 2024-03-11 |
| `newsposts/wiki_Update_Varlamore_Part_One_Reward_Changes.wikitext` | [Update:Varlamore: Part One - Reward Changes](https://oldschool.runescape.wiki/?oldid=14523443) | 14523443 | 2024-01-11 |
| `newsposts/wiki_Update_Varlamore_The_Shining_Kingdom.wikitext` | [Update:Varlamore: The Shining Kingdom](https://oldschool.runescape.wiki/?oldid=14454623) | 14454623 | 2023-08-20 |
| `newsposts/wiki_Update_Varlamore_Tweaks_Drop_Rates.wikitext` | [Update:Varlamore Tweaks & Drop Rates](https://oldschool.runescape.wiki/?oldid=14623369) | 14623369 | 2024-04-04 |
| `newsposts/wiki_Update_Varlamore_Tweaks_GameJam_V_Commences_More.wikitext` | [Update:Varlamore Tweaks, GameJam V Commences & More!](https://oldschool.runescape.wiki/?oldid=14643458) | 14643458 | 2024-04-24 |
| `newsposts/wiki_Update_Project_Rebalance_Skilling_Poll_81_MTA_Changes.wikitext` | [Update:Project Rebalance: Skilling & Poll 81 MTA Changes](https://oldschool.runescape.wiki/?oldid=14858681) | 14858681 | 2025-03-06 |
| `newsposts/wiki_Update_Varlamore_The_Rising_Darkness_is_OUT_NOW.wikitext` | [Update:Varlamore: The Rising Darkness is OUT NOW](https://oldschool.runescape.wiki/?oldid=14751651) | 14751651 | 2024-09-25 |
| `newsposts/wiki_Update_Royal_Titans.wikitext` | [Update:Royal Titans](https://oldschool.runescape.wiki/?oldid=14847851) | 14847851 | 2025-02-05 |
| `newsposts/wiki_Update_Poll_84_Batch_II.wikitext` | [Update:Poll 84: Batch II](https://oldschool.runescape.wiki/?oldid=14892854) | 14892854 | 2025-04-30 |
| `newsposts/wiki_Update_Summer_Sweep_Up_Combat.wikitext` | [Update:Summer Sweep Up: Combat](https://oldschool.runescape.wiki/?oldid=14938870) | 14938870 | 2025-07-16 |
| `newsposts/wiki_Update_Varlamore_Summer_Sweep_Up_Combat_Tweaks.wikitext` | [Update:Varlamore & Summer Sweep-Up Combat Tweaks](https://oldschool.runescape.wiki/?oldid=14978336) | 14978336 | 2025-09-02 |
| `newsposts/wiki_Update_Summer_Sweep_Up_Gear_PvM_Changes.wikitext` | [Update:Summer Sweep-Up Gear & PvM Changes](https://oldschool.runescape.wiki/?oldid=15289827) | 15289827 | 2026-08-07 |

### 1.2 Every grade A sentence, with file and line

Searched (namespace 112, srlimit 50, 2026-10-03): Fortis Colosseum 39 hits, Colosseum 55, Sol Heredit 14, Varlamore 125 (first 50 returned), Dizana's quiver 21, Colosseum beta 8, Colosseum changes 45, Colosseum hotfix 6; results in build/logs/news_search.txt. No post is a Colosseum beta: the pre-release posts are the poll and the First Look.

#### Modifier: Bees

- `Undead_Pirates_Colosseum_Changes_more:74` * Increased time between movements from 7 ticks (4.2s) to 12 ticks (7.2s).
- `Undead_Pirates_Colosseum_Changes_more:75` * Increased respawn delay from 18 ticks (~11s) to 50 ticks (30s).

#### Modifier: Doom

- `Undead_Pirates_Colosseum_Changes_more:80` ** Doom stacks reset to 0 at the end of each wave.
- `Undead_Pirates_Colosseum_Changes_more:81` ** Death is guaranteed at 15, 10, or 5 stacks of Doom depending on the active modifier tier.
- `Undead_Pirates_Colosseum_Changes_more:82` ** Doom stacks will only be applied by Colosseum sources. Instances of self-dealt damage like the Soulreaper Axe or Divine Potions will no longer add Doom stacks.
- `Varlamore_Tweaks_Drop_Rates:19` * There will now be three tiers of Doom. Each tier will allow you to take up to 15, 10 or 5 Stacks of Doom (respectively) before meeting your end.
- `Undead_Pirates_Colosseum_Changes_more:88` ''The Doom Scorpion''

#### Modifier: Doom (release fix)

- `Easter_Varlamore_Updates:119` Just a small fix to the description of Doom, which now correctly states that the effect applies on ''any ''damage taken, not just damage taken ‘off-Prayer’.

#### Modifier: Blasphemy

- `Undead_Pirates_Colosseum_Changes_more:86` * Similar to the Doom change, self-dealt damage will no longer result in additional Prayer Point drain if Blasphemy is active.

#### Modifier: Mantimayhem (Manticore)

- `Undead_Pirates_Colosseum_Changes_more:95` ** Tier 1 adds an additional projectile per orb, meaning you'll get hit twice as many times for each attack, making hits off-Prayer more punishing.
- `Undead_Pirates_Colosseum_Changes_more:96` ** Tier 2 adds a Venom effect to any damage received from the Manticore's attacks.
- `Undead_Pirates_Colosseum_Changes_more:97` ** Tier 3 removes the 'forced' Melee orb in the final slot of the Manticore's attack sequence. Effectively, this makes the order of attacks totally random.
- `Varlamore_Tweaks_GameJam_V_Commences_More:36` * The 'Mantimayhem' modifier will no longer be offered as an option going into Wave 12, since it doesn't do anything during the Sol Heredit fight. We ''could ''add a Manticore into the mix here, but we're not sure it would be a welcome addition...

#### Modifier: Myopia

- `Undead_Pirates_Colosseum_Changes_more:101` * Myopia now affects autocast spells. Manually-cast spells remain unaffected.

#### Modifier: Relentless

- `Undead_Pirates_Colosseum_Changes_more:105` * Tiers 1 and 2 now cause enemy attacks to ignore 33% and 66% of your Defence, respectively. Tier 3 is unchanged and ignores all Defence.
- `Undead_Pirates_Colosseum_Changes_more:106` * Bonus 'minimum hits' from this modifier remain unchanged at 1, 3 and 6 per tier.

#### Modifier: Totemic

- `Undead_Pirates_Colosseum_Changes_more:110` * Increased respawn timer from 1 minute to 2 minutes after a totem has been destroyed.
- `Undead_Pirates_Colosseum_Changes_more:111` * Reduced healing from 40% to 30% of the targeted NPCs health.
- `Royal_Titans:42` * Colosseum Totems once again respect the player's attack delay.

#### Monster: Fremennik warband (ranged/melee)

- `Varlamore_Tweaks_Drop_Rates:103` * The Ranged Warbanders have been given a little buff, so their ranged level more accurately reflects the rest of their stats. Watch out though, as this means they will hit a little harder now!
- `Varlamore_Tweaks_Drop_Rates:104` * We've reduced the Warband Melee's maxhit to give you a little more of a fighting chance up against those fellas.
- `Pride_2024:17` * Reduced the health of Fremennik warbanders in the Fortis Colosseum from 50 to 48.
- `Pride_2024:123` Over to the Fortis Colosseum now, where the melee Fremennik warbander has been taking ''two ''hits to kill instead of one – a noticeable difficulty increase. To offset this, we’re reduced his HP from 50 to 48 so you can now one-shot him in peace.

#### Monster: Serpent shaman

- `Undead_Pirates_Colosseum_Changes_more:208` * The Serpent Shaman inside the Colosseum has had some animations updated so that their spells better reflect their powers.
- `Undead_Pirates_Tweaks_Varlamore_CAs_More:49` * The Serpent Shaman's Water Surge now looks even more impressive!

#### Monster: Manticore

- `Undead_Pirates_Tweaks_Varlamore_CAs_More:48` * Updated the Manticore’s examine text.
- `Undead_Pirates_Tweaks_Varlamore_CAs_More:198` | Complete Wave 4 without taking avoidable damage from a Manticore.

#### Monster: Minotaur

- `Undead_Pirates_Tweaks_Varlamore_CAs_More:203` | Complete Wave 7 without the Minotaur ever healing other enemies.

#### Monster: Jaguar warrior

- `Undead_Pirates_Tweaks_Varlamore_CAs_More:208` | Kill a Jaguar Warrior using a Claw-type weapon special attack.

#### Monster: Sol Heredit

- `Varlamore_Part_One:31` * Extended the range of Sol Heredit's Spear Strike and Shield Slam attacks.
- `Varlamore_Tweaks_GameJam_V_Commences_More:37` * Sol Heredit will no longer damage players with his 'grab' attack after he runs out of hitpoints. Apologies to anybody who's missed out on juicy rewards as a result of this nasty bug.
- `Undead_Pirates_Tweaks_Varlamore_CAs_More:248` | Defeat Sol Heredit without taking any damage from his Spear, Shield, Grapple or Triple Attack.
- `Undead_Pirates_Tweaks_Varlamore_CAs_More:223` | Defeat Sol Heredit after Fortis Saluting to the North, East, South and West of the arena while he's below 10% Hitpoints.
- `Undead_Pirates_Tweaks_Varlamore_CAs_More:253` | Defeat Sol Heredit with the "Bees! II", "Quartet", and "Solarflare II" modifiers active.
- `Undead_Pirates_Tweaks_Varlamore_CAs_More:228` | Complete Wave 11 with either "Red Flag", "Dynamic Duo", or "Doom II" active.
- `Further_Leagues_VI_Changes:30` * The minion summoned from the Minion relic will now attack Sol Heredit if the player engages him in combat.

#### Monster: Sol Heredit (wave 12)

- `Varlamore_Tweaks_GameJam_V_Commences_More:36` * The 'Mantimayhem' modifier will no longer be offered as an option going into Wave 12, since it doesn't do anything during the Sol Heredit fight. We ''could ''add a Manticore into the mix here, but we're not sure it would be a welcome addition...

#### Waves and arena

- `Varlamore_The_Shining_Kingdom:94` While our previous offerings have had loads of waves, this time we want to deliver a shorter experience with a focus on replayability. We want the Fortis Colosseum to feel less like a ‘one and done’ milestone and more like something you’re excited to improve at over time. Some randomising of enemy spawn locations and even slight variations on the enemies you’ll encounter ensure
- `Fortis_Colosseum_First_Look_Rewards:98` The Fortis Colosseum is Old School’s third wave-based minigame, following in the fiery footsteps of the Fight Caves and the Inferno.
- `Fortis_Colosseum_First_Look_Rewards:130` Those of you who can’t get enough of the Colosseum can choose to extend their run by running back every single wave with ''all ''the modifiers accumulated so far… plus a few new ones. This is sure to push your Glory to new heights… if you make it out alive.
- `Varlamore_Part_One_Overview:58` We want the Fortis Colosseum to feel less like a ‘one and done’ milestone and more like something you’re excited to improve at over time. Some randomising of enemy spawn locations and even slight variations on the enemies you’ll encounter ensure that there’s plenty of variety… but there’s something else that keeps the crowds screaming for more. &nbsp;
- `Undead_Pirates_Tweaks_Varlamore_CAs_More:46` * Colosseum acid pools no longer deal damage between waves.
- `Undead_Pirates_Colosseum_Changes_more:215` * Special Attack Energy no longer regenerates between Waves in the Colosseum.
- `Undead_Pirates_Colosseum_Changes_more:213` * Bees inside the Colosseum are now more visible. Buzzin’!
- `Further_Project_Rebalance_Skilling_Varlamore_Changes:39` * Prevented certain tiles in the Fortis Colosseum from blocking players who tried to walk on them.
- `Varlamore_Tweaks_Drop_Rates:106` * We've upped the Colosseum security so you'll no longer find NPCs wandering the arena if they aren't supposed to be there.
- `Colosseum_NPC_Clickboxes_More:14` * Improved clickboxes at the Colosseum. All Colosseum NPCs are included, as well as their shadows!
- `Gielinor_Gazette_October_2024:72` ''Speed tasks are the most difficult ones to test. Usually by the time we release CAs, a couple of weeks have passed and we've had some data to base our speed times on, but it’s not an exact science. For example, the Fortis Colosseum had an average completion time of around 30 minutes when we released its Combat Achievements, so 28 and 24 minutes felt like reasonable targets. T
- `Permanent_Deadman_World_345_Improvements:51` ** Players can no longer enter the Colosseum or create a private instance at the Hueycoatl while skulled.
- `Pride_2024:123` Over to the Fortis Colosseum now, where the melee Fremennik warbander has been taking ''two ''hits to kill instead of one – a noticeable difficulty increase. To offset this, we’re reduced his HP from 50 to 48 so you can now one-shot him in peace.

#### Glory

- `Fortis_Colosseum_First_Look_Rewards:120` You’ll earn Glory every time you complete a wave. How much you get depends on how much damage you took, how quickly you cleared the wave, and the number of errors you made – effectively, it’s a reflection of how well you performed.
- `Fortis_Colosseum_First_Look_Rewards:122` This isn’t a currency in the traditional sense – you can’t farm it by repeating low-level waves over and over again. It’s based solely on your performance, and the better you perform, the more Glory you’ll accrue.
- `Varlamore_Tweaks_Drop_Rates:105` * Those taking on the Colosseum will now receive the correct number of Glory for stacked modifiers.
- `Varlamore_Tweaks_Drop_Rates:201` Once you've completed Wave 6 and for each wave afterwards, the Unique Drop Table is unlocked. If you roll on this table you do not roll on the Normal Drop Table for that wave. Glory will NEVER affect your odds. The base for rolling on the Unique Drop Table is 1/220. This number can be modified based on your current wave +1, which equals to:

#### Reward pool and cash-out

- `Varlamore_Tweaks_Drop_Rates:201` Once you've completed Wave 6 and for each wave afterwards, the Unique Drop Table is unlocked. If you roll on this table you do not roll on the Normal Drop Table for that wave. Glory will NEVER affect your odds. The base for rolling on the Unique Drop Table is 1/220. This number can be modified based on your current wave +1, which equals to:
- `Varlamore_Tweaks_Drop_Rates:202` * Wave 6 - 1/108
- `Varlamore_Tweaks_Drop_Rates:203` * Wave 7 - 1/92
- `Varlamore_Tweaks_Drop_Rates:204` * Wave 8 - 1/76
- `Varlamore_Tweaks_Drop_Rates:205` * Wave 9 - 1/60
- `Varlamore_Tweaks_Drop_Rates:206` * Wave 10 - 1/44
- `Varlamore_Tweaks_Drop_Rates:207` * Wave 11 - 1/28
- `Varlamore_Tweaks_Drop_Rates:210` * Echo Crystal - 6/10*
- `Varlamore_Tweaks_Drop_Rates:211` * Sunfire Fanatic Armour Piece - 3/10**
- `Varlamore_Tweaks_Drop_Rates:212` * Tonalztics of Ralos - 1/10
- `Varlamore_Tweaks_Drop_Rates:215` * Smol Heredit - 1/200***
- `Varlamore_Tweaks_Drop_Rates:216` * Dizana's Quiver - Only acquired upon a completion of the Colosseum.****
- `Varlamore_Tweaks_Drop_Rates:218` <nowiki>* Rolling an Echo Crystal will also have a 1/10 chance of receiving either 2 or 3 Echo Crystals instead.</nowiki>
- `Varlamore_Tweaks_Drop_Rates:220` <nowiki>** Obtaining these will favour pieces you haven't already received, otherwise it is random.</nowiki>
- `Varlamore_Tweaks_Drop_Rates:222` <nowiki>*** The pet can only be rolled on completion of Wave 11 and its odds are not affected by any other factors.</nowiki>
- `Varlamore_Tweaks_Drop_Rates:224` <nowiki>**** You can also gamble Quivers for a 1/200 chance to receive Smol Heredit by talking to Minimus.</nowiki>
- `Varlamore_Tweaks_Drop_Rates:108` * You can now trade the Dizana's Quiver for a total of 4,000 Sunfire Splinters by talking to Minimus.
- `Varlamore_Tweaks_Drop_Rates:109` * We have slightly reduced the number of Sunfire Splinters obtainable in Wave 1, and increased the number gathered from waves 9-12. So while you'll receive 20% less Splinters on your first wave, if you make it through those later rounds, you'll see a lot more!
- `Undead_Pirates_Colosseum_Changes_more:116` Check out [https://secure.runescape.com/m=news/a=97/varlamore-tweaks--drop-rates?oldschool=1 last week’s newspost] to learn about how drops in the Colosseum work. Or, just remember the most important bit: from Wave 3 onwards, the Colosseum rolls to see whether you’re offered regular loot or a shiny unique. As the Wave count increases, so do your odds!
- `Varlamore_Part_One:38` * Increased the lenience of the reduced Colosseum death fees. Currently, your fee is reduced by 75% until you've completed 50 waves, we're looking to up this to 100 waves for now. We don't want to go overboard because gold sinks are valuable to us and we feel progression will get easier as more players produce educational content to aid on conquering the Colosseum. Outside of t
- `Easter_Varlamore_Updates:64` * The reduction for Colosseum Death Costs will now apply up to 100 completed waves.
- `Pet_Insurance_Rework_More:34` * Valuable loot from the Colosseum now sends a clan chat broadcast when claimed.

#### Reward pool and cash-out (REVISED rates, Apr 2024)

- `Undead_Pirates_Colosseum_Changes_more:134` (table lines 134-169, wave old->new chance) w3 -->1/124; w4 -->1/110; w5 -->1/96; w6 1/108->1/82; w7 1/92->1/68; w8 1/76->1/54; w9 1/60->1/40; w10 1/44->1/26; w11 1/28->1/12; w12 -->-
- `Undead_Pirates_Colosseum_Changes_more:177` * Sunfire Armour Piece: adjusted from 3/10 to 9/16.
- `Undead_Pirates_Colosseum_Changes_more:178` * Echo Crystal: adjusted from 6/10 to 6/16.
- `Undead_Pirates_Colosseum_Changes_more:179` * Tonalztics of Ralos: adjusted from 1/10 to 1/16. Now only obtainable from Wave 7 onwards.

#### Items: Tonalztics of Ralos / Glaive

- `Undead_Pirates_Colosseum_Changes_more:185` * Tonalztics of Ralos' Special Attack's Defence reduction is no longer capped at 50% of the target's Defence.
- `Undead_Pirates_Colosseum_Changes_more:186` * Increased the Tonalztics of Ralos' High Alchemy value to 360,000 GP.
- `Fortis_Colosseum_First_Look_Rewards:144` Uncharged, you'll throw it out, hit your enemy for between 0 - 50% of your maximum hit and have the glaive return to your hand. Infinite ammo sounds neat, but only 50% damage? What gives?
- `Fortis_Colosseum_First_Look_Rewards:148` As fun as the thematics are, we've got a cherry on top to make sure that your time spent obtaining one of these bad boys is worthwhile: its special attack. For 50% Energy, successful hits will drain the target's Defence level by 10% of their ''Magic ''level. This means that when charged, you'll drain either 0%, 10% or 20% of your target's Magic level from their Defence, dependi
- `Summer_Sweep_Up_Gear_PvM_Changes:93` ** Special attack buffed to reduce opponent's Defence by 12.5% of its Magic level per hit, up from 10%.
- `Summer_Sweep_Up_Gear_PvM_Changes:94` ** Special attack buffed to grant +50% increased accuracy.
- `Summer_Sweep_Up_Combat:136` * The Tonalztics of Ralos has had its range increased by 1. On Rapid, you can hit 6 tiles away. On Longrange, you can hit 8 tiles away.

#### Items: Echo crystal / boots

- `Undead_Pirates_Colosseum_Changes_more:187` * Echo Crystals now add 6,000 charges to Echo Boots, up from 2,500.
- `Undead_Pirates_Colosseum_Changes_more:188` * Echo Boots are now able to store charges up to a maximum of 60,000, up from 10,000.
- `Undead_Pirates_Colosseum_Changes_more:192` * Increased Echo Crystals' High Alchemy value to 90,000 GP.
- `Defender_of_Varrock_Varlamore_Rewards_More:34` Echo Crystals are an upgrade item that will give ''something'' powerful area-of-effect recoil damage in a 3 x 3 square around the wearer. The upgraded item will require charging, and when you’re out of Echo Crystals, you’ll have to head back to the Colosseum for more. Following your feedback, we’ve also given the Echo Crystals a +2 Prayer bonus.
- `Project_Rebalance_Skilling_Poll_81_MTA_Changes:123` * Echo Boots now require 75 Defence.
- `Poll_84_Batch_II:49` * Echo boots can now be reverted to Guardian boots on a F2P world, in case their owner wishes to sell them.

#### Items: Sunfire splinters / runes

- `Varlamore_Part_One:37` * Adjusted the Runecraft XP offered by crafting Sunfire runes. Effectively rather than these scaling with your Runecraft level up to 13.5 XP per Essence used, they now offer a flat 9 XP per Essence used at all Runecraft levels.
- `Fortis_Colosseum_First_Look_Rewards:185` Just like Lava Runes, Sunfire Runes will count as Fire Runes when casting spells – but whenever you consume one, that spell will gain a 10% minimum hit. As a brief note, we anticipate that hovering over a Fire spell with any Sunfire Runes in your inventory would clearly show that you'd be using Sunfire runes as opposed to boring old Fire Runes!
- `Interface_Uplift_Round_2:160` As such, we're removing Sunfire Splinters from Doom's loot table with today's game update.

#### Items: Sunfire fanatic armour

- `Fortis_Colosseum_First_Look_Rewards:171` Seriously though, Proselyte has been the go-to Prayer bonus gear since 2006, and spilling blood to sustain an enigmatic sun god in an ancient Colosseum feels like a fitting spot to earn some gear that brings you a little closer to the divine. Each piece of the tradeable Sunfire Fanatic set requires 60 Prayer and 40 Defence, and will be dropped individually with each piece boast

#### Items: Dizana's quiver

- `Fortis_Colosseum_First_Look_Rewards:198` For your trouble, you’ll get +10 Ranged accuracy and +1 Ranged strength over Ava’s Assembler – and let’s face it, it looks ''much ''nicer than a rucksack full of magnets and undead animal parts.
- `Fortis_Colosseum_First_Look_Rewards:202` So, it ''looks'' cooler and it’s a good bit stronger – but come on. We can’t seriously expect you to start picking your own arrows up off the floor. Bring your quiver and your favourite of Ava’s devices along to her workshop, and she’ll apply the same ammo-saving effect to your Cape - that's 80% pickup rate for an Assembler or 72% for an Accumulator.
- `Undead_Pirates_Tweaks_Varlamore_CAs_More:47` * When using Dizana's Quiver alongside a Twisted Bow, the correct Ranged Strength stats will be used when firing Dragon Arrows.

#### Combat Achievements

- `Undead_Pirates_Tweaks_Varlamore_CAs_More:116` You’ve had plenty of time to get to grips with the new combat challenges Varlamore offers – so we’ve thrown a few complications into the mix! You may now take on 12 new Combat Achievements for Perilous Moons, and 13 for the Colosseum, ranging from Medium to Grand Master.
- `Undead_Pirates_Tweaks_Varlamore_CAs_More:233` | Complete the Colosseum with a total time of 30:00 or less.
- `Undead_Pirates_Tweaks_Varlamore_CAs_More:258` | Complete the Colosseum with a total time of 24:00 or less.
- `Undead_Pirates_Tweaks_Varlamore_CAs_More:238` | Defeat Sol Heredit 10 Times.
- `Undead_Pirates_Tweaks_Varlamore_CAs_More:243` | Defeat Sol Heredit without running.
- `Gielinor_Gazette_October_2024:72` ''Speed tasks are the most difficult ones to test. Usually by the time we release CAs, a couple of weeks have passed and we've had some data to base our speed times on, but it’s not an exact science. For example, the Fortis Colosseum had an average completion time of around 30 minutes when we released its Combat Achievements, so 28 and 24 minutes felt like reasonable targets. T
- `Demonic_Pacts_Overview:78` * Complete Wave 12 of Fortis Colosseum (200 Points)
- `Deadman_Armageddon_First_Look:93` * Fortis Colosseum - 100 Points per Sol Heredit kill.


---

## 2. rev-239 cache extracts (authoritative for ids and stats)

Dumped **2026-10-03** by the inventory agent with
`tools/waves_gate/cache_dump.py --game colosseum` from the unpacked text cache at
`OSRS-Content/osrs239-content/` (revision 239, OSRS-Content at `c70422bdf6`); the
command and the per-file selection rules are in `sources/LEDGER_cache.md`, and
re-running it reproduces every file byte for byte. The binary cache was not read.
The cache has no wave table, no reward table and no 5-tick cadence; it does have
the modifier table (`enum_5312`, in `cache_enums_dbrows.txt`).

| File | Contents |
|---|---|
| `sources/cache_npc.txt` | 45 npc records (ids 12767-12829 range: the 14 wave npcs 12810-12826, Sol phases, Minimus 12808, pets, props) |
| `sources/cache_seq.txt`, `cache_spotanim.txt` | the animation and graphic ids the rig pass reads |
| `sources/cache_objs.txt`, `cache_locs.txt`, `cache_map.txt` | items, locs, and the two map squares m28_48 (region 7216, arena) and m28_148 (region 7316, lobby) |
| `sources/cache_enums_dbrows.txt` | enums and db rows including the modifier table (enum 5312; the wiki ledger lists 34 tier rows) |
| `sources/cache_vars.txt`, `cache_interfaces.txt`, `cache_music.txt`, `cache_sounds.txt` | varps/varbits/varcs, interfaces, music tracks, sound effects |
| `RIG_ANIMATIONS.md`, `sources/rig/*.md` and `*.tsv` | every animation on each monster's rig with evidence tiers (`name`, `rig`, `rig+name`); the `unknown`/candidate roles are what the code and Blert id tables promote |
| `AV_INVENTORY.md`, `AV_INVENTORY.tsv` | the 1,419-row inventory of audio, visual and id facts |

There is no separate cache part report: the cache is first-party input to every
other part. Cross-checks against it are noted in each section (the 14 npc ids the
wiki lists, Sol's stats, the berserker's 48 hitpoints, the animation lengths).

## 3. Blert recordings (grade B observed / `[plugin]` asserted)

Read **`sources/blert/PROVENANCE.md`** before citing anything from this section. A
Blert row is OBSERVED when the plugin copies something the client saw (an npc
spawn tile, an animation id that changed, a despawn, a chat line, the game's own
split timer) and ASSERTED when the plugin infers it (the second and third
manticore orb, a grapple hit as "no defend within 4 ticks", every hitpoint
constant, the tick-0 offset of wave 12). Observed rows can carry grade B and are
what promote a rig candidate; asserted rows are `[plugin]` and promote nothing.
Blert has no event for a hit, a projectile, damage, a prayer, the reward roll, the
pet or the Minimus walk-in, and `NPC_DEATH` is a despawn, not a hitpoint-zero.

| What | Where | Pinned at | Fetched |
|---|---|---|---|
| plugin (MIT, 25 files) | `sources/blert/plugin/` | blert-io/plugin `0efb39800b9e1f73e01c0116b962ccc93f0f8f6f` (2026-10-01) | 2026-10-04 (UTC) clone |
| blert server and web (7 files) | `sources/blert/blert/` | blert-io/blert `7c7750cf01b23e5d223623d7c34c5ef10ca327fd` (2026-10-01); `proto/` is an empty submodule dir | clone on disk since 2026-10-03, re-read 2026-10-04 |
| event schema (5 files) | `sources/blert/protos/` | blert-io/protos `450112dffc33e1e51b7ec201e1c8a45bf2f903c9` (2026-09-30) | 2026-10-03 |
| the sample: 18 challenges (12 completed, 6 failed late), 208 wave streams | `sources/blert_api/<uuid>.json` x18 (24 MB in all, each 1.3-1.7 MB, none over 2 MB), `FETCH_LOG.tsv`, `observed_npc_events.tsv`, `wave_records.tsv` | blert.io API at the time, no version header | 2026-10-04T00:18Z-00:30Z; 230 requests, all HTTP 200, 3.0 s apart by `tools/waves_gate/verify_blert.py --game colosseum` |
| derived tables | `sources/blert/SAMPLE_SUMMARY.md` (tick-0 set per wave, spawn tiles, attack gaps, reinforcement ticks, wave length, Sol and modifier events, handicap options), `sources/blert/ID_TABLE.md` (every animation, spotanim and graphic id the plugin names, mapped to the monster and the attack with plugin file:line, confirming rig rows in `sources/rig/*.tsv`), `sources/blert/README.md` | derived | 2026-10-04 |

One stray request (2026-10-04T00:32:32Z, an Inferno type-5 listing, HTTP 400) came
from the part's own regression check; its row was removed from the Inferno
`FETCH_LOG` and is logged in `LEDGER_blert.md` section 1. The pinned plugin's
MIT licence is in `sources/blert/plugin/LICENSE`; the 24 MB API sample is inside
the repository (each file under 2 MB), nothing over 2 MB is committed.

## 4. Plugin code (grade D; one observer each)

Pinned **2026-10-03**; the table is `sources/SIMULATORS.md` (repos, commits, licences,
action taken); every constant is a quote with file:line in
`sources/CODE_CONSTANTS.md` (A wave table, B arena and modifiers, C animation and
graphic ids, D Sol, E line-of-sight constants, F cross-source summary), and
`sources/code_id_map.tsv` has 85 rows mapping every id a plugin names to its rig
rows. Names resolve to ids through `sources/runelite_gameval/colosseum_names.tsv`
(runelite/runelite `04b96b7a`, BSD-2).

| Directory | Source | Commit | Licence | What it states |
|---|---|---|---|---|
| `sources/fortis_colosseum/` | LlemonDuck/fortis-colosseum (hub plugin, "FORT") | `e8b26968` | BSD-2 | the only wave table in code (`WaveSpawns.java:34-97`), modifier bit and level varbits, orb order, region ids |
| `sources/colosseum_waves/` | willediger/Colosseum-Waves ("WAVES") | `94376dd6` | BSD-2 | a wave recorder; npc-type schema shared with FORT and SUPA |
| `sources/combat_logger/` | SuperNerdEric/combat-logger ("LOGGER") | `a5c6f304` | BSD-2 | the Colosseum attack-animation id lists (`id_lists_excerpt.txt`), `ColosseumHelper.java` |
| `sources/inf_colo_additions/` | bopsec/bop-plugins hub plugin inf-colo-additions | `6ce8337b` | BSD-2 | arena world bounds, region ids (`excerpt.txt`, an excerpt only) |
| `sources/sol_heredit_trainer/` | OldSchoolSDK/InfernoTrainer branch merge-sims ("SOLSIM", colosim.com Sol Heredit Trainer) | `cc557916` | GPL-3.0 | Sol Heredit and his hazards; GPL-3: keep it out of any file we ship |
| (quoted, not copied) | Supalosa/osrs-colosseum ("SUPA", los.colosim.com) | `5b1734f0` (2026-08-24) | NONE | per-monster size, range, cooldown, manticore charge and orb order, first-attack delay |
| (read, not tabled) | chsami/Microbot-Hub colosseumprayer | `4cb81986` | BSD-2 | a bot author's heuristics (`TILE_RANGE_RANGED_MAGING = 18`, javelin on even ticks): grade E, not used |

`inf_colo_additions/excerpt.txt` is twelve lines, each prefixed with its line number in the upstream file; a cite such as `excerpt.txt:53-56` names those prefixes, as does `combat_logger/id_lists_excerpt.txt`.

FORT, WAVES and SUPA share one npc-type schema (1-6, reinforcement shaman 7) and
FORT and WAVES emit links to SUPA: they are not three independent sources. Clones
live in `build/corpus_tmp/` (not in git); no file was copied from a repository
that has no licence.

## 5. Simulators

Sol Heredit only, in `sources/sol_heredit_trainer/` (SOLSIM above, GPL-3, a
player-tested engine fork). Its stats match cache npc 12821 exactly (1500
hitpoints, 350/200/400/350/300, size 5); its combat level 1200 does not (cache
1563). Every number about Sol's attack timing, hit sizes, laser and sand cadence
is a simulator author's belief (grade D) until the wiki, Blert or a video agrees.
The wave monsters have no simulator except SUPA's unlicensed line-of-sight tool.
Rows: `sources/CODE_CONSTANTS.md` section D and E; the repo table is
`sources/SIMULATORS.md`.

## 6. Reference servers

None. The brief said not to fetch a Colosseum reference server, and none was.
Nothing in this corpus is a server's behaviour; see section 9.

## 7. The pinned wiki (grade D alone, C with an independent second source)

Fetched **2026-10-03** by one worker from `oldschool.runescape.wiki` only, through
`tools/toa_fetch_wiki.py` (User-Agent `3draster-toa-research/1.0`) behind a
1.2-second throttle: `action=query` for the revision, then `action=parse&oldid=REV`
for the wikitext. The pin guard never overwrote a file. 63 pages are in
`sources/wiki/` with `manifest.tsv` (title, revision, revision stamp, file); a
redirect resolves to its target and is pinned under the target (Sol
Heredit/Strategies -> Fortis Colosseum/Strategies#Sol Heredit; Colosseum
Speed-Trialist -> Colosseum Speed-Chaser; I Was Here First -> "I was here first!";
Sunfire bracers -> Sunlit bracers). The wave table and the drop tables are inline
wikitext, not a Module or Bucket: wave table
`sources/wiki/wiki_Fortis_Colosseum_Strategies.wikitext:873-905`, drop tables
`sources/wiki/wiki_Rewards_Chest_Fortis_Colosseum.wikitext:119-340`. The one
Module pinned is `Module:Tile markers/Colosseum.json` (unnamed tile markers,
region 7216). `sources/wiki/TECHNIQUES.md` is derived: 27 named techniques with
the sentence and the mechanic line. The 14 npc ids the Strategies page lists for
the RuneLite highlighter (lines 1583-1596, ids 12810-12826) all match the names in
`sources/cache_npc.txt`. The row-by-row quote list (wave table, spawn tiles,
monster stats, modifiers, Sol, Glory, rewards, items, combat achievements) is in
`sources/LEDGER_wiki.md` under "What this part states (numbers), grouped by unit".

### 7.1 Pinned wiki pages

| File | Page | Revision | Revision stamp |
|---|---|---:|---|
| `wiki/wiki_Glory.wikitext` | [Glory](https://oldschool.runescape.wiki/?oldid=15178360) | 15178360 | 2026-04-16 |
| `wiki/wiki_Fortis_Colosseum.wikitext` | [Fortis Colosseum](https://oldschool.runescape.wiki/?oldid=15358549) | 15358549 | 2026-09-29 |
| `wiki/wiki_Minimus.wikitext` | [Minimus](https://oldschool.runescape.wiki/?oldid=15303372) | 15303372 | 2026-08-16 |
| `wiki/wiki_Jaguar_warrior.wikitext` | [Jaguar warrior](https://oldschool.runescape.wiki/?oldid=15319602) | 15319602 | 2026-08-25 |
| `wiki/wiki_Serpent_shaman.wikitext` | [Serpent shaman](https://oldschool.runescape.wiki/?oldid=15200480) | 15200480 | 2026-04-28 |
| `wiki/wiki_Minotaur_Fortis_Colosseum.wikitext` | [Minotaur (Fortis Colosseum)](https://oldschool.runescape.wiki/?oldid=15357147) | 15357147 | 2026-09-27 |
| `wiki/wiki_Fremennik_warband_archer.wikitext` | [Fremennik warband archer](https://oldschool.runescape.wiki/?oldid=15263423) | 15263423 | 2026-07-14 |
| `wiki/wiki_Fremennik_warband_seer.wikitext` | [Fremennik warband seer](https://oldschool.runescape.wiki/?oldid=15200484) | 15200484 | 2026-04-28 |
| `wiki/wiki_Fremennik_warband_berserker.wikitext` | [Fremennik warband berserker](https://oldschool.runescape.wiki/?oldid=15299774) | 15299774 | 2026-08-14 |
| `wiki/wiki_Javelin_Colossus.wikitext` | [Javelin Colossus](https://oldschool.runescape.wiki/?oldid=15200486) | 15200486 | 2026-04-28 |
| `wiki/wiki_Manticore.wikitext` | [Manticore](https://oldschool.runescape.wiki/?oldid=15200487) | 15200487 | 2026-04-28 |
| `wiki/wiki_Shockwave_Colossus.wikitext` | [Shockwave Colossus](https://oldschool.runescape.wiki/?oldid=15200488) | 15200488 | 2026-04-28 |
| `wiki/wiki_Sol_Heredit.wikitext` | [Sol Heredit](https://oldschool.runescape.wiki/?oldid=15358662) | 15358662 | 2026-09-30 |
| `wiki/wiki_Gladiator_Fortis_Colosseum.wikitext` | [Gladiator (Fortis Colosseum)](https://oldschool.runescape.wiki/?oldid=15259696) | 15259696 | 2026-07-10 |
| `wiki/wiki_Fortis_Colosseum_Strategies.wikitext` | [Fortis Colosseum/Strategies](https://oldschool.runescape.wiki/?oldid=15358918) | 15358918 | 2026-09-30 |
| `wiki/wiki_Guard_Fortis_Colosseum.wikitext` | [Guard (Fortis Colosseum)](https://oldschool.runescape.wiki/?oldid=15197186) | 15197186 | 2026-04-25 |
| `wiki/wiki_Sol_Heredit_Echo.wikitext` | [Sol Heredit (Echo)](https://oldschool.runescape.wiki/?oldid=15349115) | 15349115 | 2026-09-17 |
| `wiki/wiki_Fortis_Colosseum_Modifiers.wikitext` | [Fortis Colosseum/Modifiers](https://oldschool.runescape.wiki/?oldid=15163385) | 15163385 | 2026-04-01 |
| `wiki/wiki_Sol_Heredit_Deadman.wikitext` | [Sol Heredit (Deadman)](https://oldschool.runescape.wiki/?oldid=15349128) | 15349128 | 2026-09-17 |
| `wiki/wiki_Civitas_illa_Fortis.wikitext` | [Civitas illa Fortis](https://oldschool.runescape.wiki/?oldid=15315069) | 15315069 | 2026-08-21 |
| `wiki/wiki_Dizana_s_quiver.wikitext` | [Dizana's quiver](https://oldschool.runescape.wiki/?oldid=15357677) | 15357677 | 2026-09-28 |
| `wiki/wiki_Tonalztics_of_Ralos.wikitext` | [Tonalztics of Ralos](https://oldschool.runescape.wiki/?oldid=15358486) | 15358486 | 2026-09-29 |
| `wiki/wiki_Echo_crystal.wikitext` | [Echo crystal](https://oldschool.runescape.wiki/?oldid=15192256) | 15192256 | 2026-04-22 |
| `wiki/wiki_Echo_boots.wikitext` | [Echo boots](https://oldschool.runescape.wiki/?oldid=15320314) | 15320314 | 2026-08-25 |
| `wiki/wiki_Sunfire_rune.wikitext` | [Sunfire rune](https://oldschool.runescape.wiki/?oldid=15324618) | 15324618 | 2026-08-29 |
| `wiki/wiki_Sunfire_fanatic_helm.wikitext` | [Sunfire fanatic helm](https://oldschool.runescape.wiki/?oldid=15320313) | 15320313 | 2026-08-25 |
| `wiki/wiki_Sunfire_fanatic_cuirass.wikitext` | [Sunfire fanatic cuirass](https://oldschool.runescape.wiki/?oldid=15320312) | 15320312 | 2026-08-25 |
| `wiki/wiki_Sunfire_fanatic_chausses.wikitext` | [Sunfire fanatic chausses](https://oldschool.runescape.wiki/?oldid=15320311) | 15320311 | 2026-08-25 |
| `wiki/wiki_Smol_Heredit.wikitext` | [Smol Heredit](https://oldschool.runescape.wiki/?oldid=15352772) | 15352772 | 2026-09-21 |
| `wiki/wiki_Sunfire_splinters.wikitext` | [Sunfire splinters](https://oldschool.runescape.wiki/?oldid=15357236) | 15357236 | 2026-09-28 |
| `wiki/wiki_Gloria.wikitext` | [Gloria](https://oldschool.runescape.wiki/?oldid=15258602) | 15258602 | 2026-07-09 |
| `wiki/wiki_Blessed_Dizana_s_quiver.wikitext` | [Blessed Dizana's quiver](https://oldschool.runescape.wiki/?oldid=15357222) | 15357222 | 2026-09-28 |
| `wiki/wiki_Ueman_Teoki_of_Ralos.wikitext` | [Ueman, Teoki of Ralos](https://oldschool.runescape.wiki/?oldid=15263463) | 15263463 | 2026-07-14 |
| `wiki/wiki_Colosseum_scoreboard.wikitext` | [Colosseum scoreboard](https://oldschool.runescape.wiki/?oldid=15359242) | 15359242 | 2026-10-01 |
| `wiki/wiki_Money_making_guide_Completing_the_Fortis_Colosseum_Wave_12.wikitext` | [Money making guide/Completing the Fortis Colosseum (Wave 12)](https://oldschool.runescape.wiki/?oldid=15317627) | 15317627 | 2026-08-24 |
| `wiki/wiki_Furball.wikitext` | [Furball](https://oldschool.runescape.wiki/?oldid=14785451) | 14785451 | 2024-10-23 |
| `wiki/wiki_Sunlight_spear.wikitext` | [Sunlight spear](https://oldschool.runescape.wiki/?oldid=15341148) | 15341148 | 2026-09-12 |
| `wiki/wiki_Sunlit_bracers.wikitext` | [Sunlit bracers](https://oldschool.runescape.wiki/?oldid=15237683) | 15237683 | 2026-06-22 |
| `wiki/wiki_Sportsmanship.wikitext` | [Sportsmanship](https://oldschool.runescape.wiki/?oldid=14785454) | 14785454 | 2024-10-23 |
| `wiki/wiki_Denied.wikitext` | [Denied](https://oldschool.runescape.wiki/?oldid=15080364) | 15080364 | 2025-12-07 |
| `wiki/wiki_Colosseum_Grand_Champion.wikitext` | [Colosseum Grand Champion](https://oldschool.runescape.wiki/?oldid=14785458) | 14785458 | 2024-10-23 |
| `wiki/wiki_I_Brought_Mine_Too.wikitext` | [I Brought Mine Too](https://oldschool.runescape.wiki/?oldid=14990032) | 14990032 | 2025-09-22 |
| `wiki/wiki_Slow_Dancing_in_the_Sand.wikitext` | [Slow Dancing in the Sand](https://oldschool.runescape.wiki/?oldid=15133851) | 15133851 | 2026-02-24 |
| `wiki/wiki_Showboating.wikitext` | [Showboating](https://oldschool.runescape.wiki/?oldid=14874258) | 14874258 | 2025-03-30 |
| `wiki/wiki_Perfect_Footwork.wikitext` | [Perfect Footwork](https://oldschool.runescape.wiki/?oldid=15225319) | 15225319 | 2026-06-04 |
| `wiki/wiki_Reinforcements.wikitext` | [Reinforcements](https://oldschool.runescape.wiki/?oldid=15322301) | 15322301 | 2026-08-27 |
| `wiki/wiki_One_off.wikitext` | [One-off](https://oldschool.runescape.wiki/?oldid=15181239) | 15181239 | 2026-04-20 |
| `wiki/wiki_I_was_here_first.wikitext` | [I was here first!](https://oldschool.runescape.wiki/?oldid=14784700) | 14784700 | 2024-10-22 |
| `wiki/wiki_Colosseum_Speed_Chaser.wikitext` | [Colosseum Speed-Chaser](https://oldschool.runescape.wiki/?oldid=15293859) | 15293859 | 2026-08-12 |
| `wiki/wiki_Combat_Achievements_Bosses.wikitext` | [Combat Achievements/Bosses](https://oldschool.runescape.wiki/?oldid=15284456) | 15284456 | 2026-07-31 |
| `wiki/wiki_Searing_page.wikitext` | [Searing page](https://oldschool.runescape.wiki/?oldid=15261787) | 15261787 | 2026-07-12 |
| `wiki/wiki_Sunfire_fanatic_armour.wikitext` | [Sunfire fanatic armour](https://oldschool.runescape.wiki/?oldid=14911113) | 14911113 | 2025-05-30 |
| `wiki/wiki_Jug_of_sunfire_wine.wikitext` | [Jug of sunfire wine](https://oldschool.runescape.wiki/?oldid=15323005) | 15323005 | 2026-08-28 |
| `wiki/wiki_Are_You_Not_Entertained.wikitext` | [Are You Not Entertained?](https://oldschool.runescape.wiki/?oldid=15341383) | 15341383 | 2026-09-12 |
| `wiki/wiki_Bee_Swarm.wikitext` | [Bee Swarm](https://oldschool.runescape.wiki/?oldid=15211088) | 15211088 | 2026-05-15 |
| `wiki/wiki_Seia_Teoki_of_Ranul.wikitext` | [Seia, Teoki of Ranul](https://oldschool.runescape.wiki/?oldid=15197107) | 15197107 | 2026-04-25 |
| `wiki/wiki_Oriana.wikitext` | [Oriana](https://oldschool.runescape.wiki/?oldid=15319018) | 15319018 | 2026-08-25 |
| `wiki/wiki_Fortis_Salute.wikitext` | [Fortis Salute](https://oldschool.runescape.wiki/?oldid=15212293) | 15212293 | 2026-05-17 |
| `wiki/wiki_Rewards_Chest_Fortis_Colosseum.wikitext` | [Rewards Chest (Fortis Colosseum)](https://oldschool.runescape.wiki/?oldid=15337621) | 15337621 | 2026-09-09 |
| `wiki/wiki_Healing_totem.wikitext` | [Healing totem](https://oldschool.runescape.wiki/?oldid=15259848) | 15259848 | 2026-07-10 |
| `wiki/wiki_Molten_Sand.wikitext` | [Molten Sand](https://oldschool.runescape.wiki/?oldid=15329726) | 15329726 | 2026-09-03 |
| `wiki/wiki_Module_Tile_markers_Colosseum_json.wikitext` | [Module:Tile markers/Colosseum.json](https://oldschool.runescape.wiki/?oldid=14978884) | 14978884 | 2025-09-03 |
| `wiki/wiki_Glorious_Champion_Fortis_Colosseum.wikitext` | [Glorious Champion (Fortis Colosseum)](https://oldschool.runescape.wiki/?oldid=15353887) | 15353887 | 2026-09-23 |

## 8. Guides and videos (grade D, lowest rank)

Fetched **2026-10-03** from youtube.com only, one download at a time through
`/Users/matthewevers/.local/bin/yt-dlp` (`--write-auto-subs --sub-langs en-orig`;
the translated `en` track rate-limits with HTTP 429, so downloads ran 20-30 s
apart). 26 videos produced captions and are in `sources/transcripts/yt_<id>.md`
(converted by `tools/waves_gate/vtt_to_md.py --game "Fortis Colosseum"`; the header
line is the converter's, generated from `--game`); raw `.vtt` and `.info.json`
stay in `build/corpus_tmp/yt/` (not in git). `sources/transcripts/README.md` is
the video table (channel, length, upload date, what the title and description
say about an overlay or counter, the units the captions discuss);
`sources/transcripts/STATED.md` is every number a narrator states, with video id
and `[mm:ss]`, plus "Where two guides disagree". **Nobody has watched these
videos**: the `[mm:ss]` ranges are keyword-density pointers, no description names a
tick counter, and a video promotes no grade on its own. The best frame-count
candidate is Sun fish `M7sIf6mx4vw` (68 min, every unit, Sol from 54:00). Search
queries (12 `ytsearch8:` strings, 69 distinct ids, several unrelated) are logged
in `LEDGER_guides.md`. The guides give no reward, Glory or wave-table numbers.

## 9. Unavailable

Every source no worker could get, and why. None of these is filled in from memory.

| Source | Why |
|---|---|
| A Colosseum reference server (any engine) | not fetched by instruction; no server behaviour is cited |
| 2004 source (LostCity) | not a source in this loop (owner ruling 2026-10-03); neither minigame existed in 2004 |
| Colosseum beta newsposts | none exist; the pre-release posts are the Poll Blog and First Look (proposals) |
| Wiki title `Glory (Fortis Colosseum)` | no such title; the page is `Glory`, pinned (the manifest row for it is a MISSING stub, with no file) |
| Wiki `Sol Heredit/Strategies`, `Colosseum Speed-Trialist` | redirects, pinned under their targets |
| Animation, projectile and graphic ids, per-spawn tile coordinates on the wiki | not on the wiki; the ids come from the cache, Blert and the plugin code |
| `Module:Collection log/data.json`; per-CA task pages beyond the 13 named | not pinned |
| Blert hit, projectile, damage and prayer events; reward roll; pet; Minimus walk-in | the plugin records none of them (`blert/PROVENANCE.md` section 1) |
| Whether `Wave: 12` or the Sol jump message starts wave 12 | open: Sol's tick 0 rests on an asserted -1 offset (`blert/PROVENANCE.md:23`) |
| Blert `COLOSSEUM_TOTEM_HEAL` events | 0 in 208 wave streams; not evidence of no heal (the event needs a cycle-0 sighting from the totem's spawn tile, `blert/PROVENANCE.md`) |
| `blert/proto` submodule | empty in the blert clone; the schema is taken from the protos repo at `450112d` |
| Hub plugins for manticore orb order or Sol timers | none exist in plugin-hub `fc877fc0` beyond `fortis-colosseum` and `inf-colo-additions` |
| Spawn tiles and the reinforcement tick in code | no pinned code states them (`CODE_CONSTANTS.md` section F.4); Blert observes them, SAMPLE_SUMMARY |
| Pillar object ids; Reentry molten pool ids | names gone from runelite master; resolve from `sources/cache_locs.txt` |
| Guide captions: no `en` captions for hOMVTeauUHg, Si1RqVxUvI8, _PzEVzfdQG8, -CA4em2msA4, t1qDqa_SXpk; no caption file for 6WKX_cxgOQg, lgJieqwWgYg | YouTube produced none |
| Reward pool, cash-out, Glory rules, spawn-tile wave table, Sol attack animation ids in any guide | not covered by any sampled guide |
| NickClark787/DesktopScape, danbisdev stacksolver | not cloned: DPS maths and a stack solver, no wave mechanics |
| markd315/colo-invo | read only, not fetched into sources; a reward worker may read `views/index.html` for the reward list |
| Cache dumps for the reward table or wave table | the cache has neither (`LEDGER_cache.md`) |
| Video frame counts | nothing has been watched; the video method (RAID_ORCHESTRATOR.md section 3) is for the spec workers |

---

## 10. Where the sources disagree

One row per disagreement across the four "what this part states" lists and
`sources/CODE_CONSTANTS.md`. Each cell is a value with file:line (paths under
`docs/minigames/colosseum/`). **No verdict**: the spec workers settle each from the
source lines, with a quote. `W:` = `sources/wiki/wiki_<page>.wikitext`; `N:` =
`sources/newsposts/wiki_Update_<key>.wikitext`; `B:` = `sources/blert/`;
`CC:` = `sources/CODE_CONSTANTS.md`; `ST:` = `sources/transcripts/STATED.md`;
`LW:` = `sources/LEDGER_wiki.md`.

| Quantity | Values by source |
|---|---|
| Reinforcement time | wiki: 40 seconds (`W:Fortis_Colosseum:35`, `W:Fortis_Colosseum_Strategies:876`); Blert: tick 66 of the wave in every wave 2-11 (`B:SAMPLE_SUMMARY.md:874-897`); guides: 40 s in five (`ST:118`), 45 s in Lilratbags (`ST:12`, `ST:118`); no code states it (`CC:F.4`) |
| Unique roll offset | posts number the roll by the wave just completed: wave 3 = 1/124 ... wave 11 = 1/12 (`N:Undead_Pirates_Colosseum_Changes_more`; reading rules at the top of section 1, quotes under "Reward pool and cash-out" in 1.2); chest page by reward wave: wave 4 ... 12 (`W:Rewards_Chest_Fortis_Colosseum:35-80`). Same nine rows shifted by one |
| Unique roll rates | 10 April 2024 weights 9/16, 6/16, 1/16 (N: `Undead_Pirates_Colosseum_Changes_more`) against the superseded `Varlamore_Tweaks_Drop_Rates` table (wave 6 = 1/108 ... wave 11 = 1/28, base 1/220) |
| Wave table, tick-0 set | wiki (`W:Fortis_Colosseum_Strategies:882-904`), FORT (`fortis_colosseum/WaveSpawns.java:34-97`, `CC:A`) and Blert's tick-0 sets (`B:SAMPLE_SUMMARY.md:3-26`) read the same for all 12 waves; Blert shows a 2nd Fremennik archer/seer in 1 run per wave and a 2nd shockwave in 2 runs of wave 11 (`B:SAMPLE_SUMMARY.md:9-26`); the wiki says Quartet adds one, Dynamic Duo a second shockwave (`CC:A` modifier row); Blert's own `WAVES` table is not a wave table (`B:PROVENANCE.md`) |
| Wave 1 reinforcement | wiki lists a jaguar (`W:Fortis_Colosseum_Strategies:883`); FORT lists a jaguar for waves 1-6 (`CC:A`); Blert shows no reinforcement in wave 1 (median 34 ticks, shorter than 66) (`B:SAMPLE_SUMMARY.md:874-897`, wave lengths `:952`) |
| Where new monsters appear | wiki: a manticore at wave 4, second manticore at wave 9, shockwave at wave 7 (`W:Fortis_Colosseum_Strategies:888,894,898`) but its intro sentence says new enemies come "whenever there are two javelin colossi present ... waves 3, 6, 8 and 10" (`W:Fortis_Colosseum:37`) |
| Spawn locations | "completely random" (`W:Fortis_Colosseum:35`); Blert: reinforcements on (1823-1825, 3120) (`B:SAMPLE_SUMMARY.md:28-`, `LEDGER_blert.md`); the wiki says the reinforcements arrive from the north or the south; tile table is in players' shared LoS links, not in code (`CC:F.4`) |
| Attack cadence | wiki attack speed: 6 Fremennik, 5 shaman/jaguar/javelin/shockwave/minotaur, 10 manticore (`LW:182-286`); Blert gaps 6 Fremennik, 5 the rest, 10 manticore (`B:SAMPLE_SUMMARY.md:831-851`); SUPA cooldown 5 and manticore 10 (`CC:E`); the cache animation lengths are 3.0/2.1/3.33/2.0 ticks, the cadence is not in the cache (`CC:E`) |
| Javelin special | "every fifth attack" (`W:Javelin_Colossus:40`, `W:Fortis_Colosseum`); "after every four autos" (`W:Fortis_Colosseum_Strategies:788`); guides five in two, four in Lilratbags (`ST:117`); sky javelin lands 6 ticks later (`W:Fortis_Colosseum_Strategies:788`) against "a few ticks" (`W:Javelin_Colossus:40`); Blert pointer (closer, no verdict): `blert_api/observed_npc_events.tsv` holds 2241 `javelin_auto` and 450 `javelin_toss` attack rows in 18 runs (gap rows `B:SAMPLE_SUMMARY.md:841-842`); counting the autos between consecutive tosses of one javelin (same run, wave, room_id) is the observed test of four against five |
| Shaman max hit | 28 (`W:Fortis_Colosseum`, `LW:119`), 27 (`W:Fortis_Colosseum_Strategies`), 56 magic (`ST:64`) |
| Manticore max hit | 31 melee / 36 ranged / 31 magic (`W:Fortis_Colosseum`) against 34 per hit (`W:Fortis_Colosseum_Strategies`) (`LW:121-122`) |
| Manticore burst timing | wiki: 10-tick charge, other manticores delayed 5 (`W:Manticore:47`); guides: 3 orbs one tick apart then recharge over seven ticks (`ST:53-54`); SUPA charge 10, post-fire delay 5 (`CC:E`); Blert: burst starts every 10 ticks, the 2nd and 3rd orb are asserted by the plugin (`B:SAMPLE_SUMMARY.md:899`, `B:PROVENANCE.md`) |
| Manticore orb order | "r" = range, mage, melee and "m" = mage, range, melee (FORT `ManticoreOrbOrder.java:20-30`, SUPA `constants.ts:43-49`; not independent, `CC:E`); Blert observed orders mage-range-melee (558) and range-mage-melee (406), melee last (`B:SAMPLE_SUMMARY.md:899-910`); Mantimayhem III removes the forced melee (`N:Undead_Pirates_Colosseum_Changes_more:97`) |
| Manticore pair copy | wiki: from wave 9 pairs copy a pattern within 15 tiles and line of sight (`W:Manticore:45`); guides: two manticores take turns every five ticks (`ST:57`) |
| Berserker hitpoints | cache stat4 = 48 (`sources/cache_npc.txt` npc 12816); wiki 48 (`W:Fremennik_warband_berserker:16`, post "reduced from 50 to 48"); Blert plugin constant 50 (`B:plugin/src/main/java/io/blert/challenges/colosseum/ColosseumNpc.java:37-40`) |
| Sol combat level | SOLSIM 1200 (`sol_heredit_trainer/js/mobs/SolHeredit.ts:184`) against cache vislevel 1563 (`CC:D`) |
| Sol max hit | wiki infobox 44 typeless AoE and 44 grapple (`W:Sol_Heredit`), Strategies 45 / up to 45 (`W:Fortis_Colosseum_Strategies`) (`LW:119`); SOLSIM 70 (`SolHeredit.ts:281-289`); a guide: three hits "for a total of 70 damage" (`ST:86`); SOLSIM triple hits 15/25/35 (short) and 15/30/45 (long) (`SolHeredit.ts:609-623`) |
| Sol grapple window | wiki Sol page: 4 ticks to click the slot (`W:Sol_Heredit:96`); Strategies: 3 ticks (`W:Fortis_Colosseum_Strategies:1528`); guides: 4 ticks in two, 5 in MarlGames (`ST:119`); Blert: a hit is no defend within 4 ticks, event tick = announce + 5, parry seen at +4 (`B:SAMPLE_SUMMARY.md:1175-1187`, asserted, `B:PROVENANCE.md`); SOLSIM grapple damage at +4 (`SolHeredit.ts:626-668`) |
| Sol triple spacing | wiki: 3 ticks after the second, delayed to 4 under 50% (`W:Sol_Heredit:100-102`); SOLSIM short +2/+5/+8, long +2/+5/+9 (`SolHeredit.ts:670-690`); guides: "second tick of each three-tick cycle" and "three ticks after he starts charging" (`ST:120`) |
| Sol attack delays | wiki: spear 7, shield 6, each 1 less below 75% (`W:Fortis_Colosseum_Strategies:1447`); SOLSIM spear 7/6, shield 6/5, triple 12/11, long 12, grapple 7 (`SolHeredit.ts:408-690`); Blert: thrust gaps 5-7, 12-14 (`B:SAMPLE_SUMMARY.md:831-851`, an animation set, not a hit) |
| Sol hazard shapes | Sol page 6x6 and 7x7 (`W:Sol_Heredit`); Strategies 5x6, 5x5 and 15x15 with safe lines at 9x9 and 11x11 (`W:Fortis_Colosseum_Strategies`) (`LW:120`); SOLSIM shield ring rect x-7..x+12, y-12..y+7 (`SolHeredit.ts:421-430`) |
| Sol laser | beam ball up to 75 (Sol page) against 70+ (Strategies) (`LW:120`); SOLSIM 60-79, every 25-35 ticks, every 12 in the last phase (`SolHeredit.ts:145-147`); wiki: every 7 seconds in enrage (`W:Sol_Heredit:106`); the warning is 2 ticks (`W:Fortis_Colosseum_Strategies:1559`) or 3 (`:1509`), Blert scan to shot 4 in 59 and 3 in 11 (`B:SAMPLE_SUMMARY.md:1175-1187`) |
| Sol phases | 90/75/50/25/10% (`W:Sol_Heredit:92`, `ST:79-90`); SOLSIM 1500/1350/1125/750/375/150 hitpoints (`SolHeredit.ts:121-128`): same values, one source (D) |
| Sol first attack | SOLSIM the spear, hedged by its author (`SolHeredit.ts:157`: "first attack is always a spear?"); Blert: Sol first seen on tick 6 in 12 of 12 wave-12 streams, and his first attack animation after the spawn is the thrust in 12 of 12, 5 ticks after the spawn in 10 and 6 in 2 (`B:SAMPLE_SUMMARY.md:862`, tick 0 asserted) |
| Sol animation names | Blert plugin: 10883 thrust, 10884 break (grapple), 10885 slam, 10887 combo (`ColosseumNpc.java:48-54`); SOLSIM: 10883 spear, 10884 grapple, 10885 shield, 10886 long triple, 10887 short triple (`SolHeredit.ts:58-62`); 10882 named by nobody (`CC:F`) |
| Healing totem under Totemic | heal 30% of the target's hitpoints, spawns at 50% (`W:Fortis_Colosseum_Modifiers:209`) against 40% -> 30% in the post (section 1.2, "Modifier: Totemic"); respawn 2 minutes (`W:Fortis_Colosseum_Modifiers`, `W:Fortis_Colosseum_Strategies`, `ST:64`) against the 10 April 2024 post's 1 minute to 2 minutes (`LW:124`); Blert saw 10 totems spawn and no heal event in 208 waves (`B:SAMPLE_SUMMARY.md:1175-1187`), which does not show that no heal happened: the recorder sees a heal only from a projectile sighted on cycle 0 leaving the totem's spawn tile (`B:PROVENANCE.md`, TOTEM_HEAL row; plugin `WaveDataTracker.java:232`, `:243`) |
| Death fee | wiki: 75 percent reduction up to 125,000 coins until 100 completed waves (`W:Fortis_Colosseum`) against a post saying 50 waves, to become 100 (`N:Varlamore_Part_One_Reward_Changes`) and `N:Easter_Varlamore_Updates` saying "up to 100" (`LW:123`) |
| Tonalztics | shipped 0-75 percent hits (`W:Tonalztics_of_Ralos:39`) against 0-50 percent in the 2023 posts (First Look, Poll Blog); Division 12.5 percent and +50 percent accuracy since 22 July 2026 (`N:Summer_Sweep_Up_Gear_PvM_Changes`) (`LW:122`) |
| Wave length | wiki: no number; Blert medians 34, 59, 123, 104, 167, 189, 164, 190, 184, 256, 237, 223 ticks for waves 1-12 (`B:SAMPLE_SUMMARY.md:952`); the Minimus intermission is not counted |
| Jaguar warrior and its mager | FORT: reinforcement only in waves 1-6 (`CC:A`); wiki lists a jaguar as a reinforcement in waves 1-6 (`W:Fortis_Colosseum_Strategies:883-893`); the cache has `colosseum_jaguar_warrior` 12810 and `colosseum_standard_mager` 12811, which Blert and the wiki name the serpent shaman, so no separate "jaguar mager" is stated by any source and no sampled guide gives a separate mager number (`ST:15`, `B:ID_TABLE.md`) |
| Minotaur ids | Blert: 12812 and 12813 both play 10843, 5 events on 12813 (`B:SAMPLE_SUMMARY.md:831-851`); cache: 12813 is `minotaur_routefind` (`sources/cache_npc.txt`); code: Red Flag minotaur is 12813 (`CC:`, `LEDGER_code.md`) |
| Minimus npc id | cache npc 12808 `COLOSSEUM_MASTER` in the current gameval; old name MINIMUS_12808 (`LEDGER_code.md`); the wiki's Minimus page is `W:Minimus` |
| Region and arena bounds | region 7216 and 7316 in three code sources and cache m28_48 / m28_148 (agree, `CC:B`); arena world box x1808-1840, y3090-3123 (`inf_colo_additions/excerpt.txt:53-56`) against SOLSIM's 15-tile local span (`Constants.ts:2-5`): different quantities, flagged for the spec worker |
| Wave-12 Fremennik | wiki: no Fremennik on wave 12 without Quartet (`W:Fremennik_warband_archer:40`); FORT: 1 Fremennik with Sol under Quartet (`WaveSpawns.java:42-45`); Blert: a Fremennik seer spawned on tick 9 of wave 12 in 1 run (`B:SAMPLE_SUMMARY.md:874-897`), Quartet in 1 run |
| Handicap id | Blert: id = handicap + 30 x level (`blert/blert/challenge-harder/src/processing/colosseum.rs:27`); cache enum 5312 and FORT `Modifier.java:19-32` bit ids 0-13: the two are to be diffed by the spec worker (`CC:B`) |

---

## 11. Units

The proposed final cut for the loop to measure. It starts from
`docs/WAVES_ORCHESTRATOR.md` section 8 and keeps only what the corpus covers; the
sources in each line are the ones that state something about the unit (A = a
newspost, C = cache, BL = Blert, W = wiki, CODE = plugin or simulator, G = guides).
A unit with no `BL` or `CODE` source is a wiki-and-cache unit and will carry more
`[Mn]` tasks.

| Unit id | What it measures, and which sources cover it |
|---|---|
| `wave_table` | the 12 waves' tick-0 monster sets, fixed and random parts (Quartet's extra Fremennik, Dynamic Duo's second shockwave). BL tick-0 sets (18 runs), W wave table `Strategies:873-905`, CODE FORT `WaveSpawns.java:34-97`; G none |
| `wave_reinforcements` | which monsters arrive per wave, at what tick (66 in Blert, "40 s" in the wiki and guides), from which gate, and the same-tick-as-clear kill rule. BL spawn ticks and tiles, W `Strategies:876`, G `ST:118`; no code source |
| `arena_and_spawn_tiles` | the arena map square, region ids, pillar locs, the 12 default spawn tiles and the reinforcement tiles, line-of-sight blockers. C `cache_map.txt`/`cache_locs.txt`, BL spawn tiles `SAMPLE_SUMMARY.md:28-`, CODE SUPA constants and `inf_colo_additions/excerpt.txt`, W tile-marker module |
| `fremennik_trio` | archer, seer and berserker: stats, 6-tick cadence, styles, the melee max-hit rule, animation ids 10850/10853/10856. C, A (berserker 50 to 48), BL, W, CODE LOGGER; rig `fremennik_trio.tsv` |
| `javelin_colossus` | auto and sky-javelin toss every fifth attack (four vs five open), 15-tile range, the landing delay, animation ids 10892/10893. C, BL gaps, W, CODE, G |
| `jaguar_warrior` | the reinforcement melee monster, 5-tick cadence, animation 10847; the corpus names no separate "mager" (the serpent shaman is the cache's `standard_mager`), so the brief's "jaguar warrior and its mager" folds into this unit and `serpent_shaman`. C, BL, W, CODE, G |
| `serpent_shaman` | magic only, 10-tile range, 5-tick cadence, the heal to other monsters below 75 percent, animation 10859. C, BL, W, CODE SUPA, G |
| `manticore` | the 10-tick charge, three orbs, the 2 spotanim orb ids (2681/2683/2685), the orb order and Mantimayhem, the pair copy rule, the 5-tick delay between two manticores, animation 10869. C, A (Mantimayhem posts), BL burst starts and orders (orbs 2 and 3 asserted), W, CODE FORT and SUPA, G |
| `shockwave_colossus` | the clap, 5-tick cadence, size 3, the animation 10903 (the rig row is already attack1 `rig+name`; LOGGER and Blert confirm it, `blert/ID_TABLE.md:15`), the second shockwave under Dynamic Duo. C, BL, W, CODE LOGGER |
| `minotaur` | melee up to 74, 5-tick cadence, heal range 7 and the 75 percent heal rule, ids 12812 and 12813, animation 10843, the post-spawn rush. C, BL, W, CODE SUPA, G |
| `modifier_system` | the choice between waves (3 options, handicap id), each modifier, its levels, stacking, Glory points and the numbers the posts fix (bees 7 to 12 ticks, doom 15/10/5, relentless 33/66, totemic 40 to 30 percent, Mantimayhem not offered at wave 12). A newsposts, C `enum_5312`, BL handicap options `SAMPLE_SUMMARY.md:1160`, W Modifiers page, CODE FORT `Modifier.java`, G |
| `sol_heredit_attacks` | every attack (spear, shield, triple short and long, grapple), its animation (10883-10887), its tell, its tick to hit and its safe tiles; the first attack; the attack pool. C, BL animation sets (the hit is not recorded), W Sol and Strategies, CODE SOLSIM and LOGGER, G Sun fish `M7sIf6mx4vw`; hit ticks need frame counts |
| `sol_heredit_phases` | thresholds at 90/75/50/25/10 percent, the transition pools and crystals, the lasers and their cadence, the shrinking arena, the enrage, the Totemic totem. C, BL dust, laser and pool events, W, CODE SOLSIM `SolHeredit.ts:121-128`, G |
| `colosseum_entry_and_minimus` | the lobby (region 7316), the entrance and fee, Minimus (npc 12808) and his dialogue, the wave counter and the between-wave choices. C `cache_map.txt`/npc/interfaces, W `Minimus`, `Colosseum scoreboard`; BL none; G none |
| `reward_pool_and_cash_out` | the per-wave reward tables, the always-drops (wave 1 80 splinters), the unique roll and its offset, the leave-or-continue choice. A newsposts (10 April 2024 rates), W `Rewards_Chest_Fortis_Colosseum.wikitext:119-340`; no cache table, no Blert event, no guide |
| `glory` | Glory points per wave, completion, no-damage, modifier points, the time bonus, titles at 2,000-20,000. W `Glory.wikitext`, A, C interfaces; BL none |
| `reward_items` | the uniques and their numbers: Tonalztics of Ralos (charges, 0-75 percent), Echo boots, Sunfire fanatic armour, Sunfire splinters and runes, Sunlit bracers, Echo crystal. A, W, C `cache_objs.txt` |
| `quiver_and_pet` | Dizana's quiver (the wave-12 always-drop and the Blessed version) and the pet (Smol Heredit, 1/200 at wave 12). W, A, C `cache_npc.txt` npc 12767 |
| `death_and_fee` | what is kept on death in the Colosseum, the death fee and the 100-wave reduction. A (Varlamore posts), W `Fortis_Colosseum`; open: 50 vs 100 waves |
| `colosseum_music` | the Colosseum tracks and their unlock rows. C `cache_music.txt`, W music pages |
| `colosseum_combat_achievements` | the Colosseum CA tasks (13 named pages pinned; the wiki list is partial). A newsposts (CA lists), W CA pages, C `cache_enums_dbrows.txt` |
| `wave_presentation` | the attack, spawn, death and transition sequences, graphics, projectiles and sounds per monster, every one from a cited binding. C `cache_seq.txt`/`cache_spotanim.txt`/`cache_sounds.txt`, `RIG_ANIMATIONS.md`, `sources/rig/*.tsv`, BL `ID_TABLE.md`, CODE `code_id_map.tsv`; spotanim 2698 (Sol sunfire beam) is not in the rig tables yet |

Not proposed as a unit, with the reason: a reference-server comparison (no server
fetched); a per-wave hit-damage table (no source records a hit, only the wiki's
maxima); a video-frame unit (nothing has been watched; frame counts are the spec
workers' to take). Sol's tell timing (hit tick after the animation) and every
manticore orb after the first rest on assertions and guides, so they open `[Mn]` rows
in the unit that carries them.

---

## 12. The closer's audit (2026-10-03)

Done by the corpus closer (Opus) before the commit; the scripts were throwaway checks in
the closer's scratchpad, the results are these.

| Part | Opened | Result |
|---|---|---|
| Newsposts | every `KEY:LINE` quote in section 1.2 (109) and in the part's list (109), machine-matched against `sources/newsposts/` at the named line; the revised-rate summary row read by hand (`Undead_Pirates_Colosseum_Changes_more:130-169`) | all found at their line; none removed. Two exact duplicate quotes inside one heading removed (Shining Kingdom:94 under "Waves and arena", Drop_Rates:201 under "Reward pool") |
| Wiki | every quote in `LEDGER_wiki.md` (285) machine-matched at its line; 8 summary rows (Glory, Modifiers) read by hand | all found; the Glory wave 11 row (`Glory:81`) was missing from the ledger and is added |
| Blert | 16 cited statements (`ColosseumNpc.java:32-55`, `Manticore.java`, `WaveDataTracker.java` 46, 58-64, 77-84, 300-310, 437-438, `ColosseumChallenge.java:52-60`, `Handicap.java:30-43`, `colosseum.rs:27-29, 58-81`, `spawn-index.ts:30-41`, `SAMPLE_SUMMARY.md` 3-26, 831-851, 874-898, 952-966) | all match |
| Blert provenance | 9 events re-read in the plugin (NPC_ATTACK, the manticore inner orbs, the grapple HIT, wave 12's -1 offset, HANDICAP_CHOICE, TOTEM_HEAL, DOOM, NPC_SPAWN / NPC_DEATH with `getTick`, the SOL_DUST labels) | every OBSERVED / ASSERTED call is right; two caveats added to `blert/PROVENANCE.md` (a missing orb style drops one orb event; zero TOTEM_HEAL events is not zero heals) |
| Code | 12 cited statements (`WaveSpawns.java:40-95`, `ManticoreOrbType.java:22-31` with the gameval names, `ColosseumWavesPlugin.java:216`, `ColosseumStateTracker.java:33-34`, `excerpt.txt:53-56`, `id_lists_excerpt.txt`, `SolHeredit.ts` 56-63, 121-128, 145-147, 157, 184, 208-213, SUPA `constants.ts` 20-30, 43-49, 60-61, 66, 69) | all present. Corrected: the 10903 row (the shockwave rig row was already `attack1`, so the plugin CONFIRMS it, it does not promote it); `SolHeredit.ts:157` is hedged by its own comment ("first attack is always a spear?") |
| Guides | all 119 quoted strings in `transcripts/STATED.md` matched against the named narrator's transcript; 4 read by hand at their `[mm:ss]` | 118 verbatim; the 119th ("jaguar mager") is the worker's own phrase, not a quote |
| `tools/waves_gate/verify_blert.py` | the throttle and the cache | `MIN_INTERVAL_SECONDS = 3.0` (+0.05 s) is a module constant, no flag or environment variable sets it, and a lock file spaces requests across processes; a cached wave or overview is never fetched again (`fetch_challenge`, `fetch_overview`). Run offline, `--game colosseum summary` reproduces `blert/SAMPLE_SUMMARY.md` byte for byte and `--game inferno summary` the Inferno's below line 1; no request was made (both FETCH_LOGs and the lock file unchanged). `sample` re-lists the newest runs, so re-running it fetches a NEW sample: the pinned one is the 18 files in `blert_api/` |

Left open (not fixed here): spotanim 2698 (Sol's sunfire pool) is in no `sources/rig/*.tsv`
(the rig pass owns those tables); the cache, inventory and rig files are committed by
their own passes.
