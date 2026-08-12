# Quest Helper content port queue

Agent-loop state for **RuneLite Quest Helper -> OSRS-Content** forward port of
quests that LostCity (Sept 2004) and 2009scape (~Jan 2009) never implemented.

LostCity remains the content *shape* (RuneScript triggers, procs, configs).
Quest Helper
(`/Users/matthewevers/Documents/git_repos/quest-helper/src/main/java/com/questhelper/helpers/quests`)
is the **state-machine / test guide**: each helper's `steps.put(N, ...)` is the
quest varbit progression a `.rs2` port must reproduce. It does **not** define
implementation -- dialogue trees (including branches the helper never lists),
dig rewards, and combat come from the OSRS wiki transcript / cache / play,
guided by the helper's step map and gameval names. See methodology step 5 and
`PORTING_GUIDE` section 4.6 step 4.

Gameval constants (`NpcID.FOO` -> `foo` in `configs/all.*.compack`) are the
cache's own names -- **no id remapping**. When the helper and the osrs239 cache
disagree, **the cache wins**.

Parallel to:

- [`CONTENT_PORT_QUEUE.md`](CONTENT_PORT_QUEUE.md) - LostCity -> tree
- [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md) - mid-era
- [`KRONOS_CONTENT_PORT_QUEUE.md`](KRONOS_CONTENT_PORT_QUEUE.md) - post-2009 skills/bosses

**Do not steal LC or 2009scape slices.** Ownership: no LostCity proc **and** no
2009scape implementation (registry presence alone in `Quests.kt` is not
implementation).

Each tick ports **one** pending unblocked slice per `docs/PORTING_GUIDE.md` §4
and §4.6. Status: `pending` | `in_progress` | `done` | `blocked`.

**Depth-first:** a row stays `in_progress` until every `steps.put` value is
playable end-to-end. It only becomes `done` when the whole quest is.

## Shared tree -- never silence another lane

**Do not ever** `.rs2.skip` / `dirname.skip` / move / delete sibling content
(`skill_construction/`, `minigame_mta/`, or any other live tree) to green
`sscompile`. See PORTING_GUIDE section 7 and
`.cursor/rules/no-park-sibling-content.mdc`.

Loop prompt: read this file + PORTING_GUIDE section 4 / section 4.6 / section 7; run
`tools/questhelper_extract.py --check` on the next pending row; port it; NEVER
park sibling lanes; verify (`mock230_pack --check-only`,
`make -C src mock230-scripts`); update this file; re-arm. Stop only when the
user stops the loop.

## Methodology (non-negotiable)

1. **Grep LostCity first** (PORTING_GUIDE section 2.2). If LC has the proc, it belongs
   on `CONTENT_PORT_QUEUE`, not here.
2. **Grep 2009scape second.** If 2009scape has an implementation (not merely a
   `Quests.kt` enum entry), prefer
   [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md).
3. **No game-facing strings / ids / config constants in C.** Quest Helper Java is
   a *guide*, not something to re-implement in the engine. Express as `.rs2` +
   configs. New Server VM opcodes only when content cannot say it
   (`PORTING_GUIDE` §2.4 / §2.5) — plan + implement in the same slice (log below).
4. **Resolve names through the pack** — gameval lowercased; never copy numeric
   ids. Run `tools/questhelper_extract.py <helper-dir> --check` before writing
   scripts; unresolved names → `blocked` with the failing name, not workarounds.
5. **Wiki transcripts for dialogue (not just the helper).** Quest Helper is the
   state machine / critical-path guide; it does **not** enumerate every dialogue
   tree. Before writing scripts, open these pages (spaces -> `_`; see also
   `ExternalQuestResources.java` for the quest article URL):

   | What | Wiki URL |
   |---|---|
   | Quest / quick guide | `https://oldschool.runescape.wiki/w/<Quest_Name>` · `…/Quick_guide` |
   | **Dialogue trees** | `https://oldschool.runescape.wiki/w/Transcript:<Quest_Name>` |
   | Journal | `https://oldschool.runescape.wiki/w/Transcript:<Quest_Name>/Journal` |
   | NPC / item side trees | `https://oldschool.runescape.wiki/w/Transcript:<Name>` (follow links from the quest transcript) |

   Cover refuse options, re-talks, post-quest lines, lost-item replacements, and
   other branches the helper never `addDialogStep`s. Port when players can hit
   them; defer only with a queue-log note naming the deferred transcript
   section. Cite the transcript URL(s) in the row Notes / log when marking
   `done` (`PORTING_GUIDE` §4.6 step 4).
6. **Interfaces:** drive the rev-230 panel; do not invent IF1. See
   `UI_ERA_PORTING_GUIDE.md`.
7. **Never park sibling lanes** — no `*.skip`, no moving live trees aside for
   compile. Fix your own errors (PORTING_GUIDE §7).

## Skip list (out of scope)

| Quest Helper path | Why skip |
|---|---|
| `helpers/achievementdiaries/**` | diaries, not quests |
| `helpers/combattasks/**` | combat achievements |
| `helpers/mischelpers/**` | misc overlays |
| `helpers/skills/**` (skillsagility/, skillsmining/, skillswoodcutting/) | skill guides |
| `helpers/playerquests/**` | player-authored |
| League / `LeagueQuestRegions` variants | temporary league content |
| Spelling-only mismatches already owned elsewhere (`vampyreslayer`, `romeoandjuliet`, `monkeymadnessi`, `fairytalei/ii`, `blackknightfortress`) | LC / 2009scape under other names |
| Pre-Sept 2004 quests with LostCity `.rs2` implementations (see IN-LC list below) | belongs on CONTENT_PORT_QUEUE, not here |
| Mid-era (~Jan 2005 to Jan 2009) quests with 2009scape implementations | belongs on SCAPE2009_CONTENT_PORT_QUEUE |
| Helpers whose gameval names fail `--check` | `blocked` until pack grows |

### IN-LC: pre-Sept 2004 QuestHelper dirs that belong on CONTENT_PORT_QUEUE

These QH helpers implement LostCity-era quests already ported (or should be ported) via the main CONTENT_PORT_QUEUE. They are **not** post-Jan-2009 content and do not qualify for this queue:

| Quest Helper path | LC script name | Status |
|---|---|---|
| `animalmagnetism` | quest_animalmagnetism | IN-LC — CONTENT_PORT_QUEUE |
| `biohazard` | quest_biohazard | IN-LC — CONTENT_PORT_QUEUE |
| `cooksassistant` | quest_cook | IN-LC — CONTENT_PORT_QUEUE |
| `dwarfcannon` | quest_mcannon | IN-LC — CONTENT_PORT_QUEUE |
| `eaglespeak` | quest_eaglepeak | IN-LC — CONTENT_PORT_QUEUE |
| `eadgarsruse` | quest_eadgar | IN-LC — CONTENT_PORT_QUEUE |
| `heroesquest` | quest_hero | IN-LC — CONTENT_PORT_QUEUE |
| `holygrail` | quest_grail | IN-LC — CONTENT_PORT_QUEUE |
| `druidicritual` | quest_druid / quest_druidspirit | IN-LC — CONTENT_PORT_QUEUE |
| `icthlarinslittlehelper` | quest_icthlarin | IN-LC — CONTENT_PORT_QUEUE |
| `impcatcher` | quest_imp | IN-LC — CONTENT_PORT_QUEUE |
| `legendsquest` | quest_legends | IN-LC — CONTENT_PORT_QUEUE |
| `ragandboneman` | quest_ragandbone | IN-LC — CONTENT_PORT_QUEUE |
| `runemysteries` | quest_runemysteries | IN-LC — CONTENT_PORT_QUEUE |
| `seaslug` | quest_seaslug | IN-LC — CONTENT_PORT_QUEUE |
| `sheepherder` | quest_sheep / quest_sheepherser | IN-LC — CONTENT_PORT_QUEUE |
| `treegnomevillage` | quest_tree | IN-LC — CONTENT_PORT_QUEUE |
| `trollromance` | quest_troll / quest_troll_love | IN-LC — CONTENT_PORT_QUEUE |
| `waterfallquest` | quest_waterfall | IN-LC — CONTENT_PORT_QUEUE |
| `watchtower` | quest_itwatchtower | IN-LC — CONTENT_PORT_QUEUE |
| `zogreflesheaters` | quest_zogreflesheaters | IN-LC — CONTENT_PORT_QUEUE |
| `thefremennikexiles` | quest_viking | IN-LC — CONTENT_PORT_QUEUE (already done on QH queue) |
| `deserttreasureii` / `deserttreasure2` | quest_deserttreasureii | IN-LC — CONTENT_PORT_QUEUE (already done on QH queue) |
| `dragonslayer` / `dragonslayer1` | quest_dragon | IN-LC — CONTENT_PORT_QUEUE (found 2026-08-11 auditing row #111 of this queue) |
| `dragonslayerii` / `dragonslayer2` | quest_dragonslayer2 / quest_dragon | IN-LC — CONTENT_PORT_QUEUE (already done on QH queue) |
| `taibwowannaitrio` | quest_tbwt | IN-LC — CONTENT_PORT_QUEUE (found 2026-08-11 auditing row #112 of this queue) |
| `naturespirit` | quest_druidspirit | IN-LC — CONTENT_PORT_QUEUE (found 2026-08-11 auditing this queue; sibling of `druidicritual` / `quest_druid`) |
| `murdermystery` | quest_murder | IN-LC — CONTENT_PORT_QUEUE (found 2026-08-11, reused by King's Ransom slice #115) |
| `shadowofthestorm` | quest_shadowstorm | IN-LC — CONTENT_PORT_QUEUE (found 2026-08-11 auditing this queue) |
| `undergroundpass` | quest_upass | IN-LC — CONTENT_PORT_QUEUE (found 2026-08-11 auditing this queue) |
| `thegrandtree` | quest_grandtree | IN-LC — CONTENT_PORT_QUEUE |
| `thelosttribe` | quest_losttribe | IN-LC — CONTENT_PORT_QUEUE |
| `junglepotion` | quest_junglepotion | IN-LC — CONTENT_PORT_QUEUE |
| `recruitmentdrive` | quest_recruitmentdrive | IN-LC — CONTENT_PORT_QUEUE |
| `regicide` | quest_regicide | IN-LC — CONTENT_PORT_QUEUE |
| `tearsofguthix` | quest_tearsofguthix | IN-LC — CONTENT_PORT_QUEUE |
| `whatliesbelow` | quest_whatliesbelow | IN-LC — CONTENT_PORT_QUEUE |

### PENDING: genuinely post-Jan-2009 QuestHelper-only quests (no LC, no 2009scape)

These are the only remaining QH dirs that implement OSRS content released after Jan 2009 which neither LostCity nor 2009scape ever had. Ordered ascending by line count (depth-first ⇒ small-first):

| # | Slice | Helper | Lines | Status | Notes |
|---|---|---|---:|---|---|
| P1 | A Tail of Two Cats | `atailoftwocats` | 293 | done | Apr 2016 — TzTok-Jad + TzKal-Zad lore; two cats, timeline split; extract clean (39 gamevals resolve); scripts twocats.rs2 with all chapters + chore tracking via osrs239 varbits (twocats_quest id 1028, chores ids 1029–1036); sscompile.exe zero errors; wiki [Quick guide](https://oldschool.runescape.wiki/w/A_Tail_of_Two_Cats/Quick_guide) + [Transcript](https://oldschool.runescape.wiki/w/Transcript:A_Tail_of_Two_Cats); deferred ICTHLARIN's Little Helper gate (not yet ported), catspeak amulet e variant doesn't exist in osrs239 (only `twocats_amuletofcatspeak` id 6544) |
| P2 | Asoul's Bane | `asoulsbane` | 330 | in_progress | Mar 2019 — Asoul, dragonfire weapon quest; found 2026-08-10 already scripted (`quest_asoulsbane/scripts/soulbaine.rs2`, 193 lines, full room/cutscene/completion flow, npcs resolve e.g. `soulbane_launa`) by an untracked earlier tick, but its `quest_deviousminds`-style dbrow row was never declared (`configs/all.dbrow` has no `[quest_asoulsbane]` block; sscompile's own allocator log shows it only as `STALE=1 66540=quest_asoulsbane (no longer declared; kept, ids are stable)`) — the id resolves so it compiles clean, but `~quest_complete(quest_asoulsbane)` reads name/questpoints off a row with no declared fields. Needs a real dbrow block authored before this can be trusted `done` |
| P3 | Spirits of the Elid | `spiritsoftheelid` | 352 | done | Dec 2013 — Elid, spirit world, Khazard war; native dbrow `quest_spiritsoftheelid` (id 100, endstate 60) + native varbit schema on basevar `elid_main` reused as-is; see Log |
| P4 | Another Slice of Ham | `anothersliceofham` | 485 | done | Oct 2012 — Ham cult, Dorgesh-Kaan/Goblin Village/Sigmund; native dbrow `quest_anothersliceofham` (id 133, endstate 11) + native varbit schema on basevar `slice_base` reused as-is; see Log |
| P5 | Darkness of Hallow Vale | `darknessofhallowvale` | 816 | pending | Aug 2013 — Drakan's descendant, vampire theme |

## Queue

Ordered ascending by helper line count (depth-first => small-first). Miniquests
filed under `helpers/miniquests/` are at the end.

| # | Slice | Helper | Lines | Status | Notes |
|---|---|---|---:|---|---|
| 1 | bearyoursoul | `bearyoursoul` | 144 | done |  |
| 2 | doricsquest | `doricsquest` | 151 | done | npc=doric; varp31 doricquest (already allocated); dbrow quest_dorics id 30; scripts: doricsquest.rs2 + configs/doricsquest.varp + constant; wiki https://oldschool.runescape.wiki/w/Doric%27s_Quest/Quick_guide + Transcript:Doric%27s_Quest; deferred: pre-quest anvil dialogue (covered by Smithing gate), wares/insult side branches |
| 3 | witchspotion | `witchspotion` | 162 | done (LC) | OSRS has 3 rs2 files (not in PORT_QUEUE table) |
| 4 | impcatcher | `impcatcher` | 187 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 5 | xmarksthespot | `xmarksthespot` | 204 | done |  |
| 6 | tearsofguthix | `tearsofguthix` | 209 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 7 | entertheabyss | `entertheabyss` | 212 | done |  |
| 8 | theribbitingtaleofalilypadlabourdispute | `theribbitingtaleofalilypadlabourdispute` | 220 | done |  |
| 9 | monksfriend | `monksfriend` | 224 | done (LC) | re-audit 2026-08-10: `quest_drunkmonk` (dbrow `quest_monksfriend` id 28, journal wired `~drunkmonk_journal`, npc `brother_omad` not `brotheromad`) |
| 10 | therestlessghost | `therestlessghost` | 232 | done (LC) | re-audit 2026-08-10: `quest_priest` (`restless_ghost.rs2` npc `ghostx`, `father_aereck.rs2`, `father_urhney.rs2`; dbrow `quest_restlessghost` journal wired `~priest_journal`) |
| 11 | runemysteries | `runemysteries` | 246 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 12 | pryingtimes | `pryingtimes` | 247 | done |  |
| 13 | sheepshearer | `sheepshearer` | 248 | done (LC) | OSRS has 3 rs2 files (not in PORT_QUEUE table) |
| 14 | clientofkourend | `clientofkourend` | 257 | done |  |
| 15 | goblindiplomacy | `goblindiplomacy` | 257 | done (LC) | re-audit 2026-08-10: `quest_gobdip` (`general_bentnoze.rs2`; dbrow `quest_goblindiplomacy` journal wired) |
| 16 | thequeenofthieves | `thequeenofthieves` | 259 | done |  |
| 17 | rovingelves | `rovingelves` | 263 | done | npcs=roving_islwyn_2ops,eluned_prif,roving_mossgiant |
| 18 | thedepthsofdespair | `thedepthsofdespair` | 267 | done |  |
| 19 | druidicritual | `druidicritual` | 268 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row, dbrow `quest_druidicritual` exists — see IN-LC table (`quest_druid`/`quest_druidspirit`) |
| 20 | aporcineofinterest | `aporcineofinterest` | 275 | done |  |
| 21 | deviousminds | `deviousminds` | 275 | done | npcs=devious_monk_hooded/devious_monk_dead, high_priest_of_entrana |
| 22 | whatliesbelow | `whatliesbelow` | 286 | done (LC) | OSRS has 4 rs2 files (not in PORT_QUEUE table) |
| 23 | ernestthechicken | `ernestthechicken` | 288 | done (LC) | re-audit 2026-08-10: `quest_haunted` (`professor_oddenstein.rs2`, `veronica.rs2`; dbrow `quest_ernestthechicken` journal wired) |
| 24 | atailoftwocats | `atailoftwocats` | 293 | done | bookkeeping fix 2026-08-10: already `done` since slice 1 (2026-08-04, see P1 row + Log) — the 2026-08-06 table rebuild re-added it as `pending` without checking the tree first |
| 25 | fishingcontest | `fishingcontest` | 297 | done (LC) | re-audit 2026-08-10: `quest_fishingcompo` (`hemenster/bonzo.rs2`, `hemenster_fishing.rs2`; dbrow `quest_fishingcontest` journal wired) |
| 26 | junglepotion | `junglepotion` | 298 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 27 | gertrudescat | `gertrudescat` | 299 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 28 | princealirescue | `princealirescue` | 302 | done (LC) | OSRS has 4 rs2 files (not in PORT_QUEUE table) |
| 29 | cooksassistant | `cooksassistant` | 303 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row, dbrow `quest_cooksassistant` exists — see IN-LC table (`quest_cook`) |
| 30 | theascentofarceuus | `theascentofarceuus` | 310 | done |  |
| 31 | trollstronghold | `trollstronghold` | 311 | done (LC) | re-audit 2026-08-10: `quest_death` (shared dir w/ Death Plateau; `death_tenzing.rs2`, `death_saba_eohric.rs2`; dbrow `quest_trollstronghold` journal wired) |
| 32 | lostcity | `lostcity` | 312 | done (LC) | re-audit 2026-08-10: `quest_zanaris` (`shamus.rs2`, `tree_spirit.rs2`, `zanaris_camp.rs2`; dbrow `quest_lostcity` journal wired) |
| 33 | ethicallyacquiredantiquities | `ethicallyacquiredantiquities` | 313 | done |  |
| 34 | theidesofmilk | `theidesofmilk` | 316 | done |  |
| 35 | insearchofknowledge | `insearchofknowledge` | 317 | done |  |
| 36 | sheepherder | `sheepherder` | 317 | done (LC) | OSRS has 8 rs2 files (not in PORT_QUEUE table) |
| 37 | makinghistory | `makinghistory` | 319 | done | native dbrow `quest_makinghistory` (id 97, endstate 4) + native varbit schema on basevar `makinghistory` (prog/trader_prog/warr_prog/ghost_prog/melina_pres/droalak_pres) reused as-is |
| 38 | thehandinthesand | `thehandinthesand` | 319 | done | Oct 2006 -- Bert's sandpit, Sandy the corrupt slavedriver, Zavistic Rarve; native dbrow `quest_handinthesand` (id 102, endstate 160) + native varbit schema on basevar `handsand` (`%handsand_quest` 0/10/20.../150/160, question1-3, tele, serum) reused as-is; dir `quest_handinthesand` (cache-authoritative name, not the QH dir spelling) |
| 39 | bonevoyage | `bonevoyage` | 320 | done |  |
| 40 | theknightssword | `theknightssword` | 320 | done (LC) | re-audit 2026-08-10: `quest_squire` (`squire.rs2`, `reldo.rs2`; dbrow `quest_knightssword` journal wired) |
| 41 | trollromance | `trollromance` | 321 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row, dbrow `quest_trollromance` exists — see IN-LC table (`quest_troll`/`quest_troll_love`) |
| 42 | fightarena | `fightarena` | 322 | done (LC) | re-audit 2026-08-10: `quest_arena` (`general_khazard.rs2`, `khazard_guard.rs2`, `fightslave.rs2`; dbrow `quest_fightarena` journal wired) |
| 43 | asoulsbane | `asoulsbane` | 330 | in_progress | see P2 row — scripted but dbrow-less, found 2026-08-10 |
| 44 | childrenofthesun | `childrenofthesun` | 337 | done |  |
| 45 | deathplateau | `deathplateau` | 337 | done (LC) | re-audit 2026-08-10: `quest_death` (shared dir w/ Troll Stronghold; `death_denulth.rs2`, `death_dunstan.rs2`; dbrow `quest_deathplateau` journal wired) |
| 46 | seaslug | `seaslug` | 338 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 47 | thegardenofdeath | `thegardenofdeath` | 346 | done |  |
| 48 | atfirstlight | `atfirstlight` | 348 | done |  |
| 49 | tribaltotem | `tribaltotem` | 349 | done (LC) | re-audit 2026-08-10: `quest_totem` (dbrow `quest_tribaltotem` journal wired) |
| 50 | witchshouse | `witchshouse` | 350 | done (LC) | re-audit 2026-08-10: `quest_ball` (`ball_journal.rs2`, `quest_ball_locs.rs2`; dbrow `quest_witchshouse` journal wired) |
| 51 | spiritsoftheelid | `spiritsoftheelid` | 352 | done | npcs=elid_mayor,elid_ghaslor,elid_waterspirit (helper's own `elidmayor`/`elidghaslor`/`elidranging` spellings don't match the cache's `elid_`-prefixed names, `elid_ranging_target` not `elidranging` -- cache wins); see Log |
| 52 | taleoftherighteous | `taleoftherighteous` | 353 | done |  |
| 53 | contact | `contact` | 355 | done | Jan 2007 -- Sophanem quarantined from Menaphos, tunnels of the Sect of Scabaras, Giant Scarab boss; native dbrow `quest_contact` (id 124, endstate 130, questpoints 1, stat_xp_awarded thieving 70000=7000xp) + native varbit schema on basevar `contact_master` reused as-is, matching quest-helper's own VarbitID.CONTACT name exactly; see Log |
| 54 | shadesofmortton | `shadesofmortton` | 355 | done (LC) | found 2026-08-11 while auditing row #80's neighbours: LostCity already has a proc for this (`server/scripts/quests/quest_mortton/`, both files' own header comments say "Ported from LostCity quests/quest_mortton/..."; dbrow `quest_shadesofmortton` id 63, journal wired `interface_questjournal/scripts/quest_journal.rs2:659`) -- this queue's ownership rule is presence of an LC proc, not its completion state, so it belongs on `CONTENT_PORT_QUEUE.md`, not here, same as every other "IN-LC" row. Flagging for that queue: the LC port itself is only a stub (dbrow + journal text + the diary-reading step alone, `%morttonquest` never set past `^mortton_read_diary` anywhere in the tree -- shade combat, the serum, Razmire/Ulsquire dialogue, temple rebuild, altar, and pyre are all unimplemented), not finished end-to-end; not fixed here, out of scope for this queue's own slice budget |
| 55 | gettingahead | `gettingahead` | 361 | done |  |
| 56 | elementalworkshopi | `elementalworkshopi` | 362 | done | found 2026-08-11 already implemented: `quest_elemental_workshop` (`elemental_workshop_shield_book.rs2`, `elemental_workshop_journal.rs2`, `elemental_drops.rs2`; dbrow `quest_elementalworkshop1` journal wired `interface_questjournal/scripts/quest_journal.rs2:595`) — found while auditing #111's neighbours, see Log |
| 57 | bigchompybirdhunting | `bigchompybirdhunting` | 363 | done (LC) | re-audit 2026-08-10: `quest_chompybird` (`fycie.rs2`, `chompy_caves.rs2`, `ogre_bow.rs2`; dbrow `quest_bigchompybirdhunting` journal wired) |
| 58 | animalmagnetism | `animalmagnetism` | 366 | done (LC) | OSRS has 5 rs2 files (not in PORT_QUEUE table) |
| 59 | scorpioncatcher | `scorpioncatcher` | 374 | done (LC) | re-audit 2026-08-10: `quest_scorpcatcher` (`thormac.rs2`; dbrow `quest_scorpioncatcher` journal wired) |
| 60 | thecorsaircurse | `thecorsaircurse` | 376 | done |  |
| 61 | belowicemountain | `belowicemountain` | 377 | done |  |
| 62 | horrorfromthedeep | `horrorfromthedeep` | 380 | done (LC) | re-audit 2026-08-10: `quest_horror` (`horror_girlfriend.rs2`, `horror_diary.rs2`; dbrow `quest_horrorfromthedeep` journal wired) |
| 63 | dwarfcannon | `dwarfcannon` | 386 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row — see IN-LC table (`quest_mcannon`) |
| 64 | familycrest | `familycrest` | 386 | done (LC) | re-audit 2026-08-10: `quest_crest` (`crest_dimintheis.rs2`, `crest_caleb.rs2`; dbrow `quest_familycrest` journal wired) |
| 65 | insearchofthemyreque | `insearchofthemyreque` | 393 | done (LC) | re-audit 2026-08-10: `quest_routequest` (dbrow `quest_insearchofthemyreque` journal wired) -- caveat added 2026-08-11 while porting #132 In Aid of the Myreque: `quest_routequest/` only has `configs/quest_routequest.{constant,varp}` + `scripts/routequest_journal.rs2`; grepping the whole `server/scripts` tree for `%routequest` finds only the journal reading it, nothing ever writes it, and Veliaf/Ivan/Polmafi's own hideout npcs have no scripted dialogue anywhere -- this quest is not actually playable end to end despite the `done (LC)` mark. Not re-scored here (out of scope for #132); #132 soft-skips it as a prerequisite instead, same convention as Cabin Fever's Priest in Peril / King's Ransom's One Small Favour. |
| 66 | shadowsofcustodia | `shadowsofcustodia` | 406 | done |  |
| 67 | currentaffairs | `currentaffairs` | 407 | done |  |
| 68 | zogreflesheaters | `zogreflesheaters` | 410 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 69 | treegnomevillage | `treegnomevillage` | 418 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row — see IN-LC table (`quest_tree`) |
| 70 | templeofikov | `templeofikov` | 419 | done (LC) | re-audit 2026-08-10: `quest_ikov` (`ikov_firewarrior.rs2`, `ikov_lucien.rs2`; dbrow `quest_templeofikov` journal wired) |
| 71 | observatoryquest | `observatoryquest` | 424 | done (LC) | re-audit 2026-08-10: `quest_itgronigen` (`observatory_professor.rs2`, `observatory_assistant.rs2`, `goblin_guard.rs2`; dbrow `quest_observatory` journal wired) |
| 72 | olafsquest | `olafsquest` | 425 | done | Apr 2007 -- Olaf Hradson, family carvings, Brine Rat Cavern; native dbrow `quest_olafs` (id 132, endstate 80, questpoints 1) + native varbit schema on basevars `olaf_var`/`olaf_extra_var`/`olaf2_extra_var` (`%olaf_quest_var`, `%olaf_ingrid_quest`/`%olaf_volf_quest`, `%olaf_fire_multi`, `%olaf2_gate_disk_1..4`, `%olaf2_walkway_1/2`, `%olaf2_killed_ulfric`, `%olaf2_gate_completed`) reused as-is, matching quest-helper's own VarbitID names exactly; picture-wall lever puzzle mechanic (right/top/left/bottom pairwise mod-5 rotation, fixed start top=2/right=3/bottom=2/left=1) derived from `PaintingWall.java`'s own hint-branch checkpoints, not guessed; dbrow `requirement_quests` wrong (resolves to Nature Spirit) -- hard-gated on The Fremennik Trials instead (`%viking = ^viking_complete`, `quest_viking` dir -- note this dir is mislabeled "Fremennik Exiles" in the IN-LC table above, it actually implements Fremennik Trials, confirmed via `quest_journal.rs2:643`); zero hand-spawning (every npc/ground item already world-spawned in `m42_58.spawn`/`m41_57.spawn`/`m42_158.spawn`, matching quest-helper's own coords); constants namespaced `^olafq_*` not `^olaf_*` (collided with `quest_viking.constant`'s own `^olaf_*` trial-judge constants); deferred: Agility-scaled barrel-repair fail chance, visual skull-disk model rotation (no verified per-rotation model id in the pack), flavour-only treasure-map/note viewer interfaces; see Log |
| 73 | grimtales | `grimtales` | 427 | done | Jun 2007 -- Sylas's rare trinkets, Grimgnash's bedtime story, Miazrqa's shrinking-potion mouse maze, Glod atop the beanstalk; native dbrow `quest_grimtales` (id 135, endstate 60, questpoints 1) + native varbit schema on basevars `grim_main`/`grim_second` (`grim_quest`, `grim_storyline`, `grim_griffin_asleep`, `grim_given_feather`, `grim_dwarfquest`, `grim_dwarf_vis`, `grim_beard_climb`, `grim_pianotrack`, `grim_piano_used`, `grim_head_found`, `grim_show_musicsheet`, `grim_have_pendant`, `grim_stalk_state`, `grim_giant_dead`) reused as-is, matching quest-helper's own VarbitID names exactly; dbrow `requirement_quests` wrong (resolves to A Porcine of Interest) -- hard-gated on Witch's House instead (`%ballquest = ^ball_complete`, `quest_ball` dir); fixed a genuine pre-existing bug in that shared `quest_ball_locs.rs2` blocking this slice -- `open_witch_house_door`'s refusal condition fired whenever `%ballquest = ^ball_complete` (i.e. always, for every Grim Tales player, since Witch's House is a hard prereq), narrowed to `%ballquest < ^ball_started` only; merged a `grim_turnip` branch into the shared `skill_herblore/scripts/brew_potion.rs2`'s existing `[opheldu,tarrominvial]` trigger for the shrink-potion recipe rather than duplicating it; mouse-hole maze routed by `inzone` zone membership (quest-helper's own `Zone` bounds) rather than single coordinates, since the cache places multiple nail-wall climb instances per room; Glod hand-spawned in his own cloud instance (no world spawn, like Ulfric in Olaf's Quest); deferred: exact wrong-branch maze coordinates (routed to nearest correct room instead), Grimgnash's story wrong-answer text (original wording, not recoverable from wiki/helper), piano interface's own compartment-open/search buttons (world object's native `op3=Search` used instead), per-note piano highlight varbits, finer watchtower cosmetic beard-climb states, `grim_junglestatue`'s "second goblin" flavour object (no native op declared -- Glod drops the one goblin quest-helper's own step map actually requires); wiki https://oldschool.runescape.wiki/w/Grim_Tales/Quick_guide + Transcript:Grim_Tales; `mingw32-make -C src sscompile` clean, `mingw32-make -C src mock230-scripts` exit 0 (13,736 scripts, up from 13,664; zero "error" hits, zero grim-tales-related warnings — only pre-existing native-cache "no Attack op" warnings on `grim_*` npc records already shipped by the cache); `mock230_pack --check-only` not runnable in this worktree (no `cache.osrs239` present -- pre-existing environment gap unrelated to this slice, ~960 pre-existing category/cache errors reproduce identically without this change); next = Haunted Mine (#76) |
| 74 | thetouristtrap | `thetouristtrap` | 433 | done (LC) | re-audit 2026-08-10: `quest_desertrescue` (`irena.rs2`; dbrow `quest_touristtrap` journal wired) |
| 75 | twilightspromise | `twilightspromise` | 433 | done |  |
| 76 | hauntedmine | `hauntedmine` | 435 | done | Dec 2004 -- the Zealot's cart-tunnel dungeon, mine-cart lever puzzle, valve/lift race, Treus Dayth ambush, crystal outcrop; native dbrow `quest_hauntedmine` (id 68, endstate 11, questpoints 2, requirement_stats crafting 35 boostable, stat_xp_awarded strength 22000xp) + full native varbit schema on basevar `hauntedmine_bits` (`heardaboutkey`, `liftpoweredonce`/`liftpowerednow`, `begincart_fungus`/`endcart_fungus`, `pointspuzzlestarted`, 8 lever bits `lever_a/b/c/d/e/i/j/k`) reused as-is, matching quest-helper's own VarbitID names exactly; see Log |
| 77 | sleepinggiants | `sleepinggiants` | 438 | done |  |
| 78 | creatureoffenkenstrain | `creatureoffenkenstrain` | 440 | done | found 2026-08-11 already implemented: `quest_fenkenstrain` (`fenkenstrain_finish.rs2` calls `~quest_complete(quest_creatureoffenkenstrain)`; dbrow journal wired `interface_questjournal/scripts/quest_journal.rs2:699`) — found while auditing #111's neighbours, see Log |
| 79 | naturespirit | `naturespirit` | 450 | done (LC) | found 2026-08-11: pre-Sept-2004 quest (25 Mar 2004), belongs on IN-LC list not this queue — LC's own `quest_druidspirit` (Druidic Ritual's sequel) already implements it in full (`filliman.rs2`, 639 lines, calls `~quest_complete(quest_naturespirit)`); journal wired `interface_questjournal/scripts/quest_journal.rs2:647` (`~druidspirit_journal`) — see IN-LC table + Log |
| 80 | mountaindaughter | `mountaindaughter` | 459 | done | Mar 2005 -- Hamal's missing daughter Asleif, Mountain Camp/Rellekka diplomacy, White Pearl food source, Kendal the bearsuited "god"; native dbrow `quest_mountaindaughter` (id 75, endstate 70, questpoints 2, requirement_stats agility 20 boostable, no requirement_quests) + full native varbit schema on basevar `mdaughter_var` (`mdaughter_quest_var`, `mdaughter_mud_var`, `mdaughter_relations_var`, `mdaughter_food_var`, `mdaughter_hamal_heardofdeath`, `mdaughter_brundt_done`, `mdaughter_hamal_relations_done`, `mdaughter_bear_discovery`, `mdaughter_bear_mayattack`, `mdaughter_bear_multi_state`, `mdaughter_hamal_heardofbear`, `mdaughter_ragnar_gavenecklace`, `mdaughter_hamal_heardofburial`, `mdaughter_burial_state`, `mdaughter_bearman_autotalk`) reused as-is, matching quest-helper's own VarbitID names exactly; see Log |
| 81 | shieldofarrav | `shieldofarrav` | 467 | done (LC) | re-audit 2026-08-10: `quest_blackarmgang` (`weaponsmaster.rs2`; dbrow `quest_shieldofarrav` journal wired) |
| 82 | waterfallquest | `waterfallquest` | 473 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row, dbrow `quest_waterfall` — see IN-LC table |
| 83 | thegrandtree | `thegrandtree` | 475 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row, dbrow `quest_grandtree` — see IN-LC table |
| 84 | meatandgreet | `meatandgreet` | 478 | done |  |
| 85 | anothersliceofham | `anothersliceofham` | 485 | done | npcs=dorgesh_urtaq,slice_goblin_archaeologist,slice_zanik_follower (helper's own `slicezanik`/`slicehamgu` spellings don't match the cache's real npc ids -- cache wins); see Log |
| 86 | pandemonium | `pandemonium` | 485 | done |  |
| 87 | clocktower | `clocktower` | 486 | done (LC) | re-audit 2026-08-10: `quest_cog` (LC's own internal codename, not `clocktower`; `server/scripts/quests/quest_cog/{quest_cog,brother_kojo,cogs,cog_journal,quest_cog_gates_and_levers,quest_cog_spindles,quest_cog_food_trough}.rs2`, 538 lines total, full cellar-cogs + gates/levers + spindles + food-trough + Brother Kojo dialogue tree + completion queue; dbrow `quest_clocktower` id 29 endstate 8, journal wired `if ($row = quest_clocktower)` in `interface_questjournal/scripts/quest_journal.rs2:519`) |
| 88 | anightatthetheatre | `anightatthetheatre` | 490 | done |  |
| 89 | merlinscrystal | `merlinscrystal` | 490 | done (LC) | re-audit 2026-08-10: `quest_arthur` (`thrantax_altar.rs2`, `sir_mordred.rs2`; dbrow `quest_merlinscrystal` journal wired) |
| 90 | eaglespeak | `eaglespeak` | 504 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row — see IN-LC table (`quest_eaglepeak`) |
| 91 | defenderofvarrock | `defenderofvarrock` | 508 | done | found 2026-08-10 already fully scripted (`server/scripts/quests/quest_defenderofvarrock/scripts/{dov_elias,dov_rovin,dov_invasion,dov_camdozaal,dov_journal}.rs2`, 775 lines incl. config, by an untracked earlier tick, not logged here before now) — `%dov` progress on 0..56 covers Jolly Boar Inn offer, six hunting-trail clues, armoured-zombie dungeon (bottles/mist soft-kept to one hand-spawned zombie per gate), invasion + candidate/Aeonisig sigil check, Camdozaal barronite/golem-core forge, `~quest_complete(quest_defenderofvarrock)`; dbrow id 188 endstate 56, journal wired `interface_questjournal/scripts/quest_journal.rs2:903`; see Log |
| 92 | priestinperil | `priestinperil` | 511 | done (LC) | re-audit 2026-08-10: `quest_priestperil` (`trapped_drezel.rs2`, `temple_doors.rs2`; dbrow `quest_priestinperil` journal wired) |
| 93 | plaguecity | `plaguecity` | 514 | done (LC) | re-audit 2026-08-10: `quest_elena` (`edmond.rs2`, `alrena.rs2`; dbrow `quest_plaguecity` journal wired) |
| 94 | piratestreasure | `piratestreasure` | 520 | done (LC) | re-audit 2026-08-10: pre-Sept-2004 quest, belongs on IN-LC list not this queue — LC's own internal codename is `quest_hunt` (not `piratestreasure`; `server/scripts/quests/quest_hunt/scripts/{redbeard_frank,luthas,dig,banana_crate,food_store,pirate_message,hunt_journal}.rs2`, 403 lines total, dbrow `quest_piratestreasure` id 16, journal wired `interface_questjournal/scripts/quest_journal.rs2:447`) |
| 95 | shilovillage | `shilovillage` | 531 | done (LC) | re-audit 2026-08-10: `quest_zombiequeen` (`rashiliyia.rs2`, `nazastarool.rs2`, `mosol_rei.rs2`; dbrow `quest_shilovillage` journal wired) |
| 96 | thelosttribe | `thelosttribe` | 532 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row, dbrow `quest_losttribe` — see IN-LC table |
| 97 | demonslayer | `demonslayer` | 540 | done (LC) | re-audit 2026-08-10: `quest_demon` (`delrith.rs2`; dbrow `quest_demonslayer` journal wired) |
| 98 | holygrail | `holygrail` | 543 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row, dbrow `quest_holygrail` — see IN-LC table (`quest_grail`) |
| 99 | throneofmiscellania | `throneofmiscellania` | 546 | done | Nov 2004 — Miscellania regency, courting Brand/Astrid, Etceteria diplomacy; native varp `misc_quest` (0/10..90->100) + native varbit schema on basevar `misc_varbit_1..4` (`misc_affection`, `misc_approval`, `misc_acceptedtorule`, `misc_partner_multivar`, `misc_s1_d1..d3`/`misc_s2_d1..d3`/`misc_s3_d1..d3`/`misc_s1_give`/`misc_s2_give`/`misc_s1_emote`/`misc_s3_emote`) reused as-is, matching Quest Helper's own VarbitID names exactly; see Log |
| 100 | thefeud | `thefeud` | 550 | done | npcs=feudalim,feudalim,shantay (helper spellings don't resolve -- cache wins, see Log) |
| 101 | thegolem | `thegolem` | 551 | done (LC) | re-audit 2026-08-10: `quest_golem` (`golem.rs2`; dbrow `quest_golem` journal wired) |
| 102 | theredreef | `theredreef` | 559 | done |  |
| 103 | misthalinmystery | `misthalinmystery` | 564 | done |  |
| 104 | thefremennikexiles | `thefremennikexiles` | 573 | done |  |
| 105 | coldwar | `coldwar` | 574 | done | native dbrow `quest_coldwar` (id 126, endstate 135) + native varbit schema on basevar `peng_var`/`peng_var2` (`peng_quest`, `peng_transmog`, `peng_doing_greeting`, `peng_multi_hide`, `peng_multi_kgp`, `peng_emote_1..3`, `peng_pong_chat`) reused as-is, matching quest-helper's own VarbitID names/semantics exactly; every npc already world-spawned, no hand-spawning needed; see Log |
| 106 | mourningsendparti | `mourningsendparti` | 575 | done | Jul 2005 -- Mourner infiltration, gnome torture, toad/sheep signal, food poisoning; native dbrow `quest_mourningsendpart1` (id 87, endstate 9) + native varbit schema on basevar `mourning_quest_bits` (`mourning_gnome`, `mourning_sheep_red/green/yellow/blue`, `mourning_gun_ammo`, `mourning_elena`, `mourning_food_poison1/2/3`, `mourning_dye_chat`, etc.) reused as-is; dbrow `requirement_quests` wrong (resolves to eaglespeak/vampyreslayer/greatbrainrobbery, none matching) -- gated on quest-helper + wiki's own Roving Elves/Big Chompy Bird Hunting/Sheep Herder instead; zero hand-spawning (Islwyn already spawned by Roving Elves, Arianwyn/Oronwen/Essyllt/gnome/overpass mourner/Elena/sheep all world-spawned); see Log |
| 107 | wanted | `wanted` | 580 | done | native dbrow `quest_wanted` (id 92, endstate 11) + native varbit schema on basevar `quest_wanted`/`quest_wanted2` (`wanted_main`, `wanted_joke_option`, `wanted_commorb_intel`, `wanted_daquarius_hint`, `wanted_lord_d_exposition`, `wanted_zammy_mage_hint`, `wanted_mission1..19`/`wanted_missionNcomplete`) reused as-is; see Log |
| 108 | deathtothedorgeshuun | `deathtothedorgeshuun` | 587 | done | native dbrow `quest_deathtothedorgeshuun` (id 113, endstate 13, requirement_stats agility 23 + thieving 23, stat_xp_awarded thieving 2000 + ranged 2000) + native varbit schema on basevar `dttd_base`/`dttd_temp` (`dttd_main`, `dttd_tour_duke/priest/goblins/citizens/sun/shop`, `dttd_zanik_in_cellar`, `dttd_tour_ham_deacon`/`dttd_tour_ham_johanhus`, `dttd_ham_trapdoor_state`, `dttd_zanik_corpse`, `dttd_collecting_tears`, `dttd_guard_1..5_warned/dead`, `dttd_mill_guards_dead`) reused as-is, matching quest-helper's own VarbitID names exactly; see Log |
| 109 | myarmsbigadventure | `myarmsbigadventure` | 589 | done | npcs=myarm_baby_roc,myarm_giant_roc,eadgar_troll_chief_cook (helper's `myarmbabyr`/`myarmgiant`/`eadgartroll` abbreviations -- cache wins); native dbrow `quest_myarmsbigadventure` (id 120, endstate 320) + native varbit schema on basevar `myarm_quest` reused as-is; prerequisite Eadgar's Ruse already LC-ported (`quest_eadgar`, no soft-skip needed); see Log |
| 110 | thegiantdwarf | `thegiantdwarf` | 589 | done | npcs=dwarf_city_boatman_mines_prequest,dwarf_city_black_guard_leader,dwarf_city_shop_sculpture (queue's own abbreviated hint didn't match -- cache wins); native dbrow `quest_giantdwarf` (id 84, endstate 50, no `requirement_quests` column) + native varbit schema on basevar `giantdwarf_main` reused as-is; see Log |
| 111 | dragonslayer | `dragonslayer` | 591 | done (LC) | 2026-08-11: pre-Sept-2004 quest (23 Sep 2001), belongs on IN-LC list not this queue — LC's own internal codename `quest_dragon` (not `dragonslayer`; `server/scripts/quests/quest_dragon/scripts/{crandor,crandor_map,dragon_journal,dragonslayer_ned,elvarg,lady_lumbridge,magic_door,melzar_the_mad,melzars_maze,quest_dragon,wormbrain}.rs2`, 1083 lines total) already fully implements it; dbrow `quest_dragonslayer1` (id 17, endstate 10, releasedate 23,9,2001), journal wired `interface_questjournal/scripts/quest_journal.rs2:483`; see IN-LC table + Log |
| 112 | taibwowannaitrio | `taibwowannaitrio` | 602 | done (LC) | 2026-08-11: pre-Sept-2004 quest (4 Mar 2003), belongs on IN-LC list not this queue — LC's own `quest_tbwt` (`quest_tbwt.rs2`, `tbwt_jogre_bones.rs2`, `tbwt_journal.rs2`, `tbwt_lubufu.rs2`, `tbwt_monkey.rs2`, `tbwt_tamayu.rs2`, `tbwt_tiadeche.rs2`, `tbwt_tinsay.rs2`) already fully implements it; dbrow `quest_taibwowannaitrio` journal wired; see IN-LC table + Log |
| 113 | eadgarsruse | `eadgarsruse` | 613 | done (LC) | found while researching #109's prereq: IN-LC duplicate row, dbrow `quest_eadgarsruse` id 62 -- see IN-LC table (`quest_eadgar`, `server/scripts/quests/quest_eadgar/`, journal wired `interface_questjournal/scripts/quest_journal.rs2:615`) |
| 114 | heroesquest | `heroesquest` | 613 | done (LC) | 2026-08-11: duplicate row — already correctly listed on the IN-LC table (`quest_hero`); this Queue row was stale, table-sync fix only. `quest_hero` (11 files, 754 lines, dbrow `quest_heroes` journal wired `interface_questjournal/scripts/quest_journal.rs2:631`) |
| 115 | kingsransom | `kingsransom` | 617 | done | Jul 2007 — Anna Sinclair's frame-up, Morgan Le Faye's coup, Merlin's prison, King Arthur's granite curse; **row #111's real replacement slice** (Dragon Slayer turned out already LC-implemented, see Log); native dbrow `quest_kingsransom` (id 136, endstate 90, questpoints 1, requirement_stats defence 65 + magic 45) + native varbit schema on basevars `kr_varp1/kr_varp2/kr_varp3` (`kr_quest`, `kr_window`, `kr_clue_note/form/armour`, `kr_court_witness`, `kr_court_dog/butl/maid_proof`, `kr_court_thread`) reused as-is, matching quest-helper's own VarbitID names exactly; dbrow `requirement_quests` wrong (resolves to Ghosts Ahoy/In Aid of the Myreque/Devious Minds/Prince Ali Rescue, none matching) — gated on quest-helper's own getGeneralRequirements() + wiki instead: Black Knights' Fortress (`%spy`/`quest_blackknight`), Holy Grail (`%grail`/`quest_grail`) and Murder Mystery (`%murderquest`/`quest_murder`) are all already implemented in this tree and are hard-gated; One Small Favour (queue row #157, still pending) is soft-skipped, matching this queue's established convention for unported sibling prereqs; see Log |
| 116 | atasteofhope | `atasteofhope` | 629 | done |  |
| 117 | biohazard | `biohazard` | 635 | done (LC) | OSRS has 7 rs2 files (not in PORT_QUEUE table) |
| 118 | makingfriendswithmyarm | `makingfriendswithmyarm` | 640 | done |  |
| 119 | swansong | `swansong` | 644 | done | May 2006 -- Herman Caranos's besieged Piscatoris Fishing Colony, the Wise Old Man's own "swan song", Franklin's wall repairs, Arnold's monkfish, Malignius Mortifer's failed skeletal army, the Sea Troll Queen; see Log |
| 120 | royaltrouble | `royaltrouble` | 657 | done | May 2006 -- King Vargas's restlessness, a staged Miscellania/Etceteria feud, five Fremennik teens (Signy/Hild/Armod/Beigarth/Reinn) who failed their Trials, a Giant Sea Snake (level 149); thematically/mechanically linked to Throne of Miscellania (#99) -- same native npcs (misc_advisor_ghrim/misc_king_vargas/misc_queen_sigrid), branch merged into quest_misc's own existing opnpc1 triggers rather than duplicated; native dbrow `quest_royaltrouble` (id 112, endstate 30, questpoints 1, requirement_stats agility 40 + slayer 40, stat_xp_awarded agility/slayer/hitpoints 5000 each -- raw dbrow values /10, matches wiki exactly) + native varbit schema on basevars `royal_questvarbits` (`royal_quest`/`royal_misc`/`royal_etc`) and `royal_varbits` reused as-is, matching quest-helper's own VarbitID names exactly (fetched via GitHub raw + summarized, not verbatim -- exact intermediate breakpoint semantics reconstructed from the recovered ROYAL_MISC {10,20,30,40,50,60,80,110,120}/ROYAL_ETC {10,20,40} value sets + wiki step order, not independently confirmed); `%royal_liftstage`/`%royal_coalinengine` breakpoints ARE independently confirmed off this cache's own multivarbit .loc records (`royal_side_scaffold_multiloc`, `royal_top_scaffold_multiloc`, `royal_engine_platform_multiloc`, `royal_lift_platform_multiloc`) -- lift repair implemented as a real multi-step item-on-object puzzle (crates/beams/pulley beams/rope/coal engine) using those exact breakpoints, not narrated; dbrow `requirement_quests` resolves to The Corsair Curse (id 147) -- not a real prerequisite (same cache decode corruption flagged repeatedly on this queue) -- gated on Throne of Miscellania completion instead (`%misc_quest = ^misc_king_signed_treaty`); npcs=royal_misc_guard/royal_etc_guard (cache's own soldiers-being-blamed stand in for the wiki's unresolved Gunnhild/Leif/Frodi/Magnus/Helga/Haming/Matilda interviewees, cache wins), misc_sailor, royal_dwarf_drunk (Donal), royal_fremennik_teen3 (Armod, spokesperson), royal_sea_snake_mother_smaller (Giant Sea Snake boss, hand-spawned on trigger + `~npc_default_death`, same idiom as Contact's Giant Scarab), royal_cutscene_prince_brand/royal_cutscene_princess_astrid (dedicated intro-cutscene npcs, distinct from quest_misc's own Brand/Astrid, op1 added via additive .npc overlay); zero hand-spawning for every other npc (all world-spawned already); cave hazards (steam vents/falling rocks/slippery-rock plank) deferred as pass-through terrain, no damage/fail-chance system precedent in this tree; wiki https://oldschool.runescape.wiki/w/Royal_Trouble/Quick_guide + walkthrough (Transcript: page not fetched verbatim, paraphrased dialogue per copyright, same as King's Ransom); `mingw32-make -C src sscompile` clean, `mingw32-make -C src mock230-scripts` exit 0 (13,940 scripts, up from 13,887); `::royaltrouble` / `::royaltroublerun`; next = The Great Brain Robbery (#121) |
| 121 | thegreatbrainrobbery | `thegreatbrainrobbery` | 659 | done | Mar 2007 -- Brother Tranquility's Harmony Island monastery has had its monks' brains stolen by Mi-Gor's zombie pirates for his machine, Barrelchest; Dr Fenkenstrain (Creature of Fenkenstrain, already implemented, hard-gated on `%creatureoffenkenstrain >= ^fenk_complete`) is smuggled to the island inside a crate of wooden cats to perform the transplants; native dbrow `quest_greatbrainrobbery` (id 130, endstate 130, questpoints 2, requirement_stats prayer 50 + construction 30 -- dbrow only encodes 2 stat rows, wiki's crafting 16 checked separately, stat_xp_awarded prayer 60000=6000xp + crafting 30000=3000xp + construction 20000=2000xp, raw dbrow /10 matches wiki exactly) + native varbit schema on basevars `brain_extra_var`/`brain_extra_var_2` (`brain_broken_steps`, `brain_read_prayers`, `brain_words`, `brain_fenk_puzzle`, `brain_crate`, `brain_barrel_setup`, `brain_clamp_given`/`brain_tongs_given`/`brain_hammer_given`/`brain_jars_given`/`brain_staples_given`, `brain_statue_pushed`, `brain_seen_wallbreaker`, `brain_multi_monk`) reused as-is, matching quest-helper's own VarbitID names exactly (fetched via GitHub raw); master progress is a plain varp `brain_quest_var` (0/10/20.../130, matching quest-helper's own steps.put keys 1:1) -- confirmed authoritative (not guessed) via this cache's own multi-npc records: `brain_tranquility`/`brain_island_tranquility` both declare `multivarp=brain_quest_var` swapping Brother Tranquility zombie->human exactly at value 100, and `brain_island_fenkenstrain` only renders `fenk_fenkenstrain_model` from value 70 on, both landing exactly on quest-helper's own step keys; crate-build puzzle (`%brain_crate` 1..5: Build/Add-bottom/Fill/cats-added/Fenk-inside) and door-breach puzzle (`%brain_barrel_setup` 2..5: keg/fuse/lit/gone) both independently confirmed via this cache's own `brain_fenk_crate` and `brain_mon_entrance_door_multi` native multiloc records, matching quest-helper's own VarbitRequirement thresholds exactly; statue passage and underwater stairs repair (`brain_statue_saradomin`, `brain_underwater_stairs_broken`, op1=Repair, no item needed) likewise cache-baked map locs with no `.spawn` entry anywhere in this tree (confirmed via grep) -- script triggers only, no hand-spawning needed for any of the puzzle geometry; dbrow `requirement_quests` decodes to Black Knights' Fortress/Lost City -- not real prerequisites (same cache decode corruption flagged repeatedly on this queue) -- real prereqs per wiki are Creature of Fenkenstrain (hard-gated, already implemented), Cabin Fever and Recipe for Disaster/Freeing Pirate Pete (both have native dbrow rows but zero scripts anywhere in server/scripts -- soft-skipped, matching this queue's established convention for unported sibling prereqs); npcs=brain_tranquility/brain_island_tranquility (Brother Tranquility, split by location, matches the queue's own `brainbrothe` abbreviation), werewolfshopkeeper1 (Rufus, already has a Talk-to stub in `areas/area_canifis/scripts/rufus.rs2` -- merged a crate-scheme branch into its existing `[opnpc1,werewolfshopkeeper1]` trigger rather than duplicating, matches queue's own `feverharmle` sample which resolves to `fever_harmless_teach`-family Mos Le'Harmless npcs, not directly used here since Tranquility himself starts on Mos Le'Harmless), fenk_fenkenstrain_model (Dr Fenkenstrain, already has a full Talk-to tree in `quests/quest_fenkenstrain/scripts/fenkenstrain.rs2` for Creature of Fenkenstrain -- merged a branch into its existing `@fenk_talk` label rather than duplicating the trigger); brain_mi_gor/brain_barrel_chest hand-spawned on trigger for the final church confrontation, same idiom as Royal Trouble's Giant Sea Snake / Contact's Giant Scarab (neither has a `.spawn` entry); wooden-cat crafting implemented as a simplified oak-plank + knife make-action (no player-owned-house workshop flatpack minigame precedent anywhere in this tree -- deferred); surgical instruments (cranial clamp/brain tongs/3 bell jars/30 skull staples) drop from Sorebones kills via a simple scripted `obj_add` on `ai_queue3` death (no verified native drop table recoverable for these specific items); Barrelchest's own prayer-disabling special attack has no established mechanic precedent, left to the generic combat system, same reasoning as Royal Trouble's own boss; wiki https://oldschool.runescape.wiki/w/The_Great_Brain_Robbery/Quick_guide + https://oldschool.runescape.wiki/w/The_Great_Brain_Robbery (dialogue paraphrased per copyright, same convention as Royal Trouble/King's Ransom); `mingw32-make -C src sscompile` clean, `mingw32-make -C src mock230-scripts` exit 0 (14,078 scripts, up from 14,041; zero brain_-related errors/warnings); files: `quests/quest_thegreatbrainrobbery/{configs/thegreatbrainrobbery.constant,configs/thegreatbrainrobbery.varp,scripts/brain_{shared,tranquility,underwater,prayerbook,castle,door,finale,journal}.rs2}` + merges into `areas/area_canifis/scripts/rufus.rs2`, `quests/quest_fenkenstrain/scripts/fenkenstrain.rs2`, `interface_questjournal/scripts/quest_journal.rs2`; next = Rum Deal (#122) |
| 122 | rumdeal | `rumdeal` | 662 | done | Oct 2005 -- Pirate Pete's plan to get Captain Braindeath's zombie crew blind drunk so he can raid the island; native dbrow `quest_rumdeal` (id 95, endstate 19, questpoints 2, requirement_stats fishing50+prayer47+crafting42+slayer42+farming40, stat_xp_awarded fishing/prayer/farming 7000xp each) + native varbit schema on basevar `deal_var` (`deal_farming` blindweed patch 0-5, `deal_barrel` pressure-barrel sluglings 0-5, `deal_multi_hopper` brew control 0-2, all confirmed via native multiloc records) reused as-is; npcs=deal_pete,deal_captian_braindeath,deal_davey,deal_captian_donnie,deal_evil_spirit,deal_fever_spiders1 (queue's own abbreviated hint `dealevilsp`/`dealpete`/`dealcaptian` don't match real cache names -- cache wins); see Log |
| 123 | templeoftheeye | `templeoftheeye` | 662 | done |  |
| 124 | thefremennikisles | `thefremennikisles` | 670 | done | Feb 2007 -- King Gjuki's jester-spy plot against Mawnis Burowgar, two rounds of Jatizso tax collection, two bridge repairs, and the Ice Troll King; native dbrow `quest_fremennikisles` (id 127, endstate 340, questpoints 1, requirement_stats agility40+construction20) + native varbit schema on basevars `fris_r1` (`fris_quest`, `fris_task` troll counter, `fris_m_b3`/`fris_m_b4`/`fris_m_b5` bridges, `fris_king` Mawnis crown swap) and `fris_r2` (six `frisd_*_taxcollected` bits, shared/reset across both tax rounds) reused as-is; npcs=fris_r_king,fris_r_burgher_crown,fris_spymaster,frisd_oremerchant,frisd_weaponmerchant,frisd_izso_landlady,frisd_cook,frisd_armourmerchant,frisd_fishmerchant,fris_troll_king_true (cache spelling matches quest-helper's own NpcID names exactly, no drama this time); see Log |
| 125 | gardenoftranquility | `gardenoftranquility` | 684 | done | Aug 2005 -- Queen Ellamaria's hidden garden for King Roald; native dbrow `quest_gardenoftranquillity` (double-L cache spelling, id 90, endstate 60, questpoints 2, requirement_stats farming25) + native varbit schema on basevars `garden_varp_1`/`garden_varp_2` reused as-is; npcs=queen_ellamaria,elstan,lyra,kragen,dantaera,brother_althric,bernald (cache's own real names, not the queue row's stale `gardentroll`/`queenellama` hints -- see Log); see Log |
| 126 | murdermystery | `murdermystery` | 686 | done (LC) | found 2026-08-11: pre-Sept-2004 quest (Dec 2003), belongs on IN-LC list not this queue — LC's own `quest_murder` (20 files, 1580 lines, dbrow `quest_murdermystery` journal wired `interface_questjournal/scripts/quest_journal.rs2:491`) already fully implements it; found + directly reused by this tick's King's Ransom slice (#115), see Log |
| 127 | enakhraslament | `enakhraslament` | 688 | done | Jan 2006 -- Lazim, Enakhra's ruined desert temple, Akthanakos; native dbrow `quest_enakhraslament` (id 103, endstate 70) + native varbit schema on three basevars (`enakh_quest_expositbits`/`enakh_multivarbits`/`enakh_varbits`) reused as-is; see Log |
| 128 | perilousmoon | `perilousmoon` | 688 | done |  |
| 129 | theslugmenace | `theslugmenace` | 694 | done | Sept 2006 -- Sir Tiffy Cashien's Temple Knights send the player to Witchaven to investigate a Zamorakian conspiracy (Col. O'Niall/Brother Maledict/Mayor Hobb), a ruined temple, torn documents, five elemental runes, and the Slug Prince; see Log |
| 130 | cabinfever | `cabinfever` | 704 | done | Feb 2006 -- Bill Teach recruits the player to raid a rival pirate crew at sea; native dbrow `quest_cabinfever` (id 104, endstate 140, questpoints 2, requirement_stats smithing50+crafting45+ranged40+agility42, stat_xp_awarded crafting/smithing/agility 7000xp each, matches wiki exactly) + native varbit schema on basevars `fever_quest`/`fever_cannon_var`/`fever_extra_var`/`fever_storage_var` (`fever_hole_1/2/3`, `fever_holes_patched/proofed`, `fever_crate/chest/barrel`, `fever_plunder_points`, `fever_cannon`, `fever_cannon_powder/tamp/ammo/fuse/clean`, `fever_holes_in_the_hull`, `fever_gunpowder_barrel`) reused as-is, matching quest-helper's own VarbitID names exactly; queue's own hint `feverteach,feverteach,feverquest` resolves to real cache names `fever_teach`/`fever_quest_ship_teach` (cache wins, close but not identical spelling). dbrow `requirement_quests` decodes to Contact! (124) and A Soul's Bane (108) -- neither a real prerequisite, same known cache-decode-corruption failure mode this queue warns about repeatedly. Real prereqs per quest-helper's own getGeneralRequirements() are Pirate's Treasure FINISHED, Rum Deal FINISHED and Priest in Peril FINISHED; Pirate's Treasure (`%hunt >= ^hunt_complete`) and Rum Deal (`%deal_var >= ^deal_complete`) are both genuinely completable in this tree and hard-gated. Priest in Peril is soft-skipped and NOT gated on: `quest_priestperil.constant`'s own header documents its essence-bringing finale (`%priestperil` 10..60) as "deferred (blocked)", and grepping its scripts confirms `%priestperil` never advances past `^priestperil_meet_in_mausoleum` (8) anywhere in this tree -- hard-gating on it would make Cabin Fever itself permanently unstartable, so it isn't checked (documented, not silently dropped, matching the established convention for a corrupted/unportable prerequisite). Native multiloc records independently confirm every real breakpoint used (cache wins, not guessed): `fever_multi_hole_1/2/3` leak->planked->waterproofed at values 1/2; `fever_multi_chest/_crate/_barrel` closed->looted at value 1; `fever_multi_cannon` intact->destroyed->no_barrel->loaded at values 1/2/3 (quest-helper reuses this same var for "broken" (1) and, later, "fuse loaded and ready" (3) -- both kept); `fever_multi_hole_enemy_1/2/3` share one real counter, `fever_holes_in_the_hull` (0..3), sequentially revealing hull breaches -- this is the wiki's own "three holes in the enemy's ship" objective, mechanically confirmed (not narrated), driven directly by the final cannonball-firing phase instead of inventing a separate counter; `fever_multi_gunpowder_barrel` intact(0)->fused(2)->exploded(1), the cache's own non-monotonic order, matches quest-helper's `addedFuse`(2)/`explodedBarrel`(1) exactly; `fever_port_ship_teach` (dock-side "Bill Teach on his boat" wrapper) is a real `multivarp=fever_quest` record, invisible until value 10 -- independently confirms the master var really does jump 0->10 on first acceptance, used directly. This server only ever spawns the wrapper npc/loc types; `fever_teach`/`fever_port_ship_teach`/`fever_quest_ship_teach` all declare no op of their own in the cache -- additive overlay in `cabinfever.npc`, same convention as `royaltrouble.npc`/`theslugmenace.npc` (every multiloc wrapper used already carries its own real op -- Repair/Loot/Plunder/Load/Take-powder/Cross -- no loc overlay needed). All navigation between decks (ladders/nets/climb-down) is already handled by the generic climb system (`ladders_stairs/scripts/ladders.rs2`, cache-declared climb verbs) -- zero custom transport scripting needed for any of it; the ship-to-ship rope swing (`fever_sail1_hoistedl_climb`, reused by quest-helper's own `swingToBoat`/`swingToEnemyBoat`/`useRopeOnSailForSabo` alike) is one zone-aware (`distance(coord,...)`) teleport trigger. Every pirate crew/enemy npc (`fever_pirate_island_01..10`, `fever_pirate_millitia_01..10`, `fever_pirate_enemy_01..10`, `fever_smithing_smith`, `fever_harpoon_joe`, `fever_pirate_two_feet_charley`, `fever_mama_la_fiette`, `fever_dodgy_mike`) is already world-spawned (confirmed via grep of `areas/world/configs/*.spawn`) and pure flavour -- none gate any quest-helper step, none scripted. Simplifications (documented, no established precedent anywhere in this tree for the alternative): quest-helper's own 704 lines are mostly `ConditionalStep`/`Zone` bookkeeping to draw a helper arrow across geography that's already baked cache terrain here -- not reproduced. Locker searches (`fever_repair_locker`/`fever_weapons_locker`) grant a full requirement in one Search rather than quest-helper's own incremental per-item fetch loop, same convention as The Great Brain Robbery's crate-building simplification. Plunder containers grant a fixed split (crate 4 + chest 3 + barrel 3 = 10, matching `loot10`) rather than a random per-loot amount -- no drop-table precedent recoverable. The canister-firing phase ("fire with canisters until 3 pirates die") has no cannon-deals-damage-directly precedent anywhere in this tree (Royal Trouble's Giant Sea Snake / The Great Brain Robbery's Barrelchest both leave combat entirely to the generic system) -- a successful load/fire cycle is itself the real, required, repeatable action standing in for the kill, tracked via `%fever_quest` sub-values (111/112/113) rather than combat; the ball-firing phase instead drives the real `fever_holes_in_the_hull` counter directly. Misfire/wrong-ammo error handling (`canisterInWrong`/`resetCannon`) isn't modelled -- wrong ammo for the current phase is just refused with a hint message, no jammed-cannon state. `fever_rum`/`fever_gold`/`fever_smithed_anchor` and the enemy crew's own named weapons are native but never referenced by any quest-helper step -- deferred flavour. Wiki https://oldschool.runescape.wiki/w/Cabin_Fever/Quick_guide + quest-helper source fetched via GitHub raw (dialogue paraphrased, not verbatim, per copyright, same caveat as every prior slice). `mingw32-make -C src sscompile` clean, `mingw32-make -C src mock230-scripts` exit 0 (14,342 scripts, up from 14,316); files: `quests/quest_cabinfever/{configs/cabinfever.{constant,varp,npc}, scripts/cabinfever_{shared,bill,transport,lockers,repair,sabotage,loot,cannon,journal}.rs2}` + wiring into `interface_questjournal/scripts/quest_journal.rs2`. This was previously a soft-skipped prerequisite for The Great Brain Robbery (#121) -- a real port here means a future tick could tighten that gate. Next pending row (smallest-first): #132 In Aid of the Myreque, 710 lines. |
| 131 | icthlarinslittlehelper | `icthlarinslittlehelper` | 707 | done (LC) | 2026-08-11: duplicate row — already correctly listed on the IN-LC table (`quest_icthlarin`); this Queue row was stale, table-sync fix only. `quest_icthlarin` (5 files, 684 lines, dbrow `quest_icthlarinslittlehelper` journal wired `interface_questjournal/scripts/quest_journal.rs2:703`) |
| 132 | inaidofthemyreque | `inaidofthemyreque` | 710 | done | Jan 2006 -- Burgh de Rott repairs, Gadderanks's blood tithe raid, Ivan's Temple Trek escort, Rod of Ivandis; native dbrow `quest_inaidofthemyreque` (id 107, endstate 430, requirement_stats Crafting25/Mining15/Magic7) + native varbit schema on basevars `myreque_2_main_var`/`myreque2_multivar`/`myreque2_extravar` reused as-is, matching quest-helper's own VarbitID names exactly; dbrow `requirement_quests` decodes to Desert Treasure I (corrupt, known failure mode) -- real prereq (In Search of the Myreque FINISHED) soft-skipped since `%routequest` is never written anywhere in this tree (that quest has no scripted content beyond its own journal/dbrow, confirmed via grep -- row #65's "done (LC)" is optimistic); Crafting/Mining/Magic gate still hard-checked. Shares `myq5_veliaf_child` with Sins of the Father's own hub trigger (merged branch in `sinsofthefather.rs2`, not duplicated) and adds one case to the shared furnace hub (`skill_smithing/scripts/smelting/smelting.rs2`) for the Rod of Ivandis mould. See Log. |
| 133 | betweenarock | `betweenarock` | 716 | done | Mar 2005 -- Dondakan the Dwarf's cannon-through-the-rock scheme uncovers a sealed Arzinian realm; dwarven lore book + 3 torn pages, a golden cannonball, four schematic fragments, a golden helmet, and an Avatar guardian boss; see Log |
| 134 | ratcatchers | `ratcatchers` | 737 | done | 2QP, Thieving 4500xp; native dbrow+varbit schema reused; see Log |
| 135 | dreammentor | `dreammentor` | 745 | blocked | 2026-08-11: real, hard prerequisite (Lunar Diplomacy FINISHED, quest-helper's own getGeneralRequirements()) is itself still `pending` on this exact queue at #169 (1,756 lines) and zero Lunar Isle content of any kind exists in this tree (grep-confirmed: no `lunar*` script files anywhere, no scripted mainland<->Lunar Isle boat transport); unlike prior soft-skipped prereqs (Priest in Peril for Cabin Fever, ISOTR for In Aid of the Myreque), Dream Mentor's entire setting depends on it, not just a gate check — see Log |
| 136 | watchtower | `watchtower` | 758 | done (LC) | 2026-08-11: duplicate row — already correctly listed on the IN-LC table (`quest_itwatchtower`); this Queue row was stale, table-sync fix only. `quest_itwatchtower` (13 files, 2010 lines, dbrow `quest_watchtower` journal wired `interface_questjournal/scripts/quest_journal.rs2:599`) |
| 137 | shadowofthestorm | `shadowofthestorm` | 759 | done (LC) | found 2026-08-11: pre-Sept-2004 quest (2002), belongs on IN-LC list not this queue — LC's own `quest_shadowstorm` (3 files, 509 lines, `shadowstorm_ritual.rs2` calls `~quest_complete(quest_shadowofthestorm)`; journal wired `interface_questjournal/scripts/quest_journal.rs2:707`) already implements it — found while auditing #111's neighbours, see Log |
| 138 | landofthegoblins | `landofthegoblins` | 760 | done | 2QP, Agility/Fishing/Thieving/Herblore 8000xp each; native dbrow+varbit schema (`%lotg`) reused; see Log |
| 139 | elementalworkshopii | `elementalworkshopii` | 770 | done | 1QP, Smithing/Crafting 7500xp each; native dbrow+20-field varbit schema (`%elemental_quest_2_main` + sub-fields) reused, real prerequisite EW1 FINISHED; see Log |
| 140 | deserttreasure | `deserttreasure` | 803 | done (LC) | OSRS has 3 rs2 files (not in PORT_QUEUE table) |
| 141 | thedigsite | `thedigsite` | 803 | done (LC) | re-audit 2026-08-10: LostCity's own internal codename for this quest is `itexam`, not `thedigsite`/`digsite` -- `quest_itexam` (`server/scripts/quests/quest_itexam/`, `examiner.rs2`/`digsite_workman.rs2`/`area_digsite.rs2`/`panning_guide.rs2`/`itexam_chemistry.rs2`, trowel + specimen_brush reuse) already fully implements it; found while checking Another Slice of H.A.M.'s (#85) real prerequisite chain |
| 142 | troubledtortugans | `troubledtortugans` | 803 | done |  |
| 143 | undergroundpass | `undergroundpass` | 812 | done (LC) | found 2026-08-11: pre-Sept-2004 quest (2002), belongs on IN-LC list not this queue — LC's own `quest_upass` (31 files, 2602 lines, dbrow `quest_undergroundpass` journal wired `interface_questjournal/scripts/quest_journal.rs2:535`) already fully implements it — found while auditing #111's neighbours, see Log |
| 144 | hazeelcult | `hazeelcult` | 814 | done (LC) | OSRS has 11 rs2 files (not in PORT_QUEUE table) |
| 145 | darknessofhallowvale | `darknessofhallowvale` | 816 | pending | npcs=myq5veliaf,myq3citizen,myq3citizen |
| 146 | ghostsahoy | `ghostsahoy` | 821 | pending | npcs=ahoyoldman,giantlobste,ahoyvelorin |
| 147 | deathontheisle | `deathontheisle` | 827 | done |  |
| 148 | scrambled | `scrambled` | 840 | done |  |
| 149 | beneathcursedsands | `beneathcursedsands` | 859 | done |  |
| 150 | regicide | `regicide` | 944 | done (LC) | OSRS has 13 rs2 files (not in PORT_QUEUE table) |
| 151 | theeyesofglouphrie | `theeyesofglouphrie` | 969 | pending | npcs=gnomebrimst,gnomebrimst,grandtreeha |
| 152 | monkeymadnessi | `monkeymadnessi` | 988 | pending | npcs=grandtreena,pilotgrand,mmcaranock |
| 153 | forgettabletale | `forgettabletale` | 1,000 | pending | npcs=dwarfcityb,dwarfcityd,dwarfcityr |
| 154 | toweroflife | `toweroflife` | 1,021 | pending | npcs=tolnpcefer,tolnpcbarr,tolhomoncul |
| 155 | mourningsendpartii | `mourningsendpartii` | 1,100 | pending | npcs=mourningari,mournerhide,mourningari |
| 156 | enlightenedjourney | `enlightenedjourney` | 1,168 | pending | npcs=zeppiccard,shipmonk1c,zeppiccard |
| 157 | onesmallfavour | `onesmallfavour` | 1,244 | pending | npcs=slagilith,favourpetra,shiloantique |
| 158 | legendsquest | `legendsquest` | 1,261 | done (LC) | 2026-08-11: duplicate row — already correctly listed on the IN-LC table (`quest_legends`); this Queue row was stale, table-sync fix only. `quest_legends` (15 files, dbrow `quest_legends`, journal wired `interface_questjournal/scripts/quest_journal.rs2:~660`, `~legends_journal`) |
| 159 | thefremenniktrials | `thefremenniktrials` | 1,269 | pending | npcs=vikingaskel,vikingenemy,vikingenemy |
| 160 | thefinaldawn | `thefinaldawn` | 1,274 | done |  |
| 161 | secretsofthenorth | `secretsofthenorth` | 1,293 | done |  |
| 162 | theforsakentower | `theforsakentower` | 1,353 | done |  |
| 163 | recruitmentdrive | `recruitmentdrive` | 1,425 | done (LC) | OSRS has 9 rs2 files (not in PORT_QUEUE table) |
| 164 | akingdomdivided | `akingdomdivided` | 1,560 | done |  |
| 165 | theheartofdarkness | `theheartofdarkness` | 1,582 | done |  |
| 166 | thecurseofarrav | `thecurseofarrav` | 1,665 | done |  |
| 167 | sinsofthefather | `sinsofthefather` | 1,668 | done |  |
| 168 | ragandboneman | `ragandboneman` | 1,729 | done (LC) | OSRS has 4 rs2 files (not in PORT_QUEUE table) |
| 169 | lunardiplomacy | `lunardiplomacy` | 1,756 | pending | npcs=lunaroneiro,lunarmoond,lunarfremen |
| 170 | dragonslayerii | `dragonslayerii` | 1,782 | done |  |
| 171 | thepathofglouphrie | `thepathofglouphrie` | 1,959 | pending | varbit=s; npcs=poggolriec,poggolriec,poggolriec |
| 172 | whileguthixsleeps | `whileguthixsleeps` | 2,288 | pending | npcs=wgstaverley,wgstaverley,wgsbroav |
| 173 | monkeymadnessii | `monkeymadnessii` | 3,084 | done |  |
| 174 | recipefordisaster | `recipefordisaster` | 3,370 | pending | npcs=cook,hundreddwar,hundreddwar |
| 175 | songoftheelves | `songoftheelves` | 4,285 | done |  |
| 176 | deserttreasureii | `deserttreasureii` | 5,076 | done |  |

## Log

- queue created (2026-08-04): Quest Helper → OSRS-Content lane; ownership =
  no LC proc + no 2009scape impl; depth-first; first slice = X Marks the Spot
- extractor: `tools/questhelper_extract.py` — all 50 in-scope helpers `--check`
  clean (ItemID leading/`trailing `_` normalized; miniquest_ dbrow fallback)
- slice 1 done: X Marks the Spot — `%cluequest` on `cluequest_main`, Veos talk
  start, 4 digs via `~xmarks_try_dig` (spade hook), casket hand-in + rewards
  (`cluequest_lamp`, 200 coins, `trail_clue_beginner`), journal wire,
  `::xmarksthespot` / `::xmarksdig` / `::xmarksrun`; headless `::xmarksrun`
  MESSAGE_GAME payloads match dig→complete→OK; no new opcodes; scripts 6221;
  `mock230_pack --check-only` 0 errors; next = Ribbiting Tale (#2)
- loop armed: AGENT_LOOP_TICK_questhelper_port every ~180s
- slice 2 done: Ribbiting Tale — `%frog_quest` on `frog_quest_primary`,
  Marcellus/Sue/Gary/Dave/Jane/Cuthbert dialogue, axe log + orange tree chop +
  lily sabotage + bed letter + chest NALIA (interim) + plushy plant + Cuthbert
  kill + rewards (1 QP, 2000 WC XP, `%frog_quest_patch_unlocked`); journal wire;
  `::ribbitingtale` / `::ribbitrun`; headless `::ribbitrun` MESSAGE_GAME payloads
  match chop→sabotage→chest→plant→complete→OK; pack 0 errors; next = Prying Times (#3)
- slice 3 done: Prying Times — `%quest_pry` on `pry_main` (0/5..30→35), Steve
  Beanie + Thurgo crowbar + sea crate stout + bar crate unlock; rewards smithing
  1000 XP + 25 oak sawmill coupons; soft port-task/sail + Pandemonium prereq;
  sailing XP deferred (skill not in pack/stat.pack); `::pryingtimes` / `::pryrun`;
  headless OK; pack 0 errors; next = Client of Kourend (#4)
- slice 4 done: Client of Kourend — `%veos_progress` on `veos_quest` (0..6→7),
  feather→quill, five house interviews, Dark Altar orb, memoirs + 2 lamps;
  Port Sarim Veos gate after X Marks; `::clientofkourend` / `::cokrun`; headless
  OK; pack 0 errors; deferred ship cutscene / lamp Rub / Kourend Castle Teleport;
  next = Queen of Thieves (#5)
- slice 5 done: Queen of Thieves — `%piscquest` on `piscquest_main` (0..12→13);
  Tomas Lawry / poor woman / O'Reilly stew / Warrens Devan / Murder Conrad /
  Queen / Hughes chest letter / Shauna finish; rewards 2000 thieving XP, 2000
  coins, `veos_memoirs_pisc_page`; wiki
  https://oldschool.runescape.wiki/w/Transcript:The_Queen_of_Thieves + Quick_guide;
  `::queenofthieves` / `::qotrun`; headless OK; pack 0 errors; deferred full
  refuse/post-quest trees + Kingstown stairs (shared `fai_varrock_stairs`);
  next = Depths of Despair (#6)
- slice 6 done: Depths of Despair — `%hosidiusquest` on `hosidiusquest_main`
  (0..4,6..10→11); Lord Kandur / Olivia / Galana / Varlamore envoy / Crabclaw
  caves (crevice→stones→rocks→rope) / Artur / Sand Snake / Accord chest /
  return; rewards 1500 agility XP, 4000 coins, `veos_memoirs_hos_page`; wiki
  https://oldschool.runescape.wiki/w/Transcript:The_Depths_of_Despair + Quick_guide;
  `::depthsofdespair` / `::dodrun`; headless OK; pack 0 errors; deferred
  random library bookshelf, stone/rock fail rolls, snake instance, Butler/Elena
  trees, favour/graceful recolour; next = Porcine of Interest (#7)
- slice 7 done: Porcine of Interest — `%porcine` on `porcine_main`
  (0/5/10/15/20/25/30/35→40); notice board / Sarah bounty / rope on hole /
  skeleton soft-cutscene / Spria goggles / Sourhog kill / foot / Sarah coins /
  Spria finish; rewards 1000 slayer XP, 5000 coins, 30 slayer points; wiki
  https://oldschool.runescape.wiki/w/Transcript:A_Porcine_of_Interest + Quick_guide;
  `::porcineofinterest` / `::poirun`; headless OK; pack 0 errors; deferred
  tracking cabbage/cart trees, full Pig Thing cutscene, slash-weapon matrix,
  Sarah shop, Spria task/helmet upgrade; next = Ascent of Arceuus (#8)
- slice 8 done: Ascent of Arceuus — `%arcquest` on `arcquest_main` (0..13→14);
  Mori / Councillor Andrews / Tower souls / Trobin / Kaal-Ket-Jor / grave +
  hunting trail / Trapped Soul / Dark Altar rocks / finish; rewards 1500 hunter
  XP, 500 runecraft XP, 2000 coins, `veos_memoirs_arc_page`; wiki
  https://oldschool.runescape.wiki/w/Transcript:The_Ascent_of_Arceuus + Quick_guide;
  `::ascentofarceuus` / `::aoarun`; headless OK; pack 0 errors; deferred tower
  instance soul count, strict trail multilocs, Tower Mage gate, favour/graceful,
  Asteros/Kaal sibling polish; next = Ethically Acquired Antiquities (#9)
- slice 9 done: Ethically Acquired Antiquities — `%eaa` on `eaa_primary`
  (0..36→38); empty display / Herminius / tools+case / visitors / Regulus /
  crew sails / Artima / Stan / Betty notes / Haig pickpocket+crate / shame /
  return; rewards 6000 thieving XP, 5000 coins; wiki Quick_guide (+ Transcript
  deferred full shame matrix); `::ethicallyacquiredantiquities` / `::eaarun`;
  headless OK; pack 0 errors; Children of the Sun soft-skipped (#13 pending);
  deferred charter sail, full shame options, Haig cutscene; next = Ides of Milk (#10)
- loop aborted (user/system): AGENT_LOOP_TICK_questhelper_port stopped after tick 11
- loop re-armed (2026-08-04): AGENT_LOOP_TICK_questhelper_port every ~180s
- slice 10 done: Ides of Milk — `%cowquest` on `cowquest_main` (0..21→22);
  Cassius / Gillie / Seth shelves book / milk samples / Duke Horacio / Brutus
  bull / finish; post-quest Gillie cowbell+lamp; wiki Quick_guide;
  `::idesofmilk` / `::iomrun`; headless OK; pack 0 errors; deferred Brutus
  specials/dodge, lamp Rub skill picker; next = In Search of Knowledge (#11)
- slice 11 done: In Search of Knowledge — `%hosdun_knowledge_search` on
  `hosdun_status` (0/1/2→3); Aimeri feed / Forthos shelves / pages soft /
  Logosia tomes / `thosf_reward_lamp`; wiki
  https://oldschool.runescape.wiki/w/In_Search_of_Knowledge/Quick_guide;
  `::insearchofknowledge` / `::isokrun`; headless OK (`isokrun OK` payload);
  pack 0 errors; deferred Forthos combat page drops, knife web, Protect Magic,
  lamp Rub; next = Bone Voyage (#12)
- slice 12 done: Bone Voyage — `%fossilquest_progress` on `fossilquest_main`
  (0..35→50) + `%fossilquest_lucky_charm` / `%fossilquest_potion`; Haig /
  Foreman / Varrock+Guild sawmills / barge Lead+Junior / Jack / Odd Old Man
  charm / Apothecary sealegs / sail soft-skip; wiki
  https://oldschool.runescape.wiki/w/Bone_Voyage/Quick_guide (+ Transcript);
  `::bonevoyage` / `::bvrun`; headless OK (`bvrun OK` payload); pack 0 errors;
  deferred Dig Site/Kudos hard gates, WC60, sailing IF, bank-chest note; next =
  Children of the Sun (#13)
- slice 13 done: Children of the Sun — `%vmq1` on `vmq1_primary` (0..22→24) +
  guard mark bits; Alina / bag-guard follow soft / house door soft / Tobyn /
  mark four guards / castle roof finish; `%vmq1_questcomplete_type`=2; wiki
  https://oldschool.runescape.wiki/w/Children_of_the_Sun/Quick_guide (+ Transcript);
  `::childrenofthesun` / `::cotsrun`; headless OK (`cotsrun OK` payload); pack 0
  errors; deferred stealth tiles, house cutscene, wrong-guard overlay puzzle,
  Quetzal first-travel (VMQ2); next = The Garden of Death (#14)
- slice 14 done: The Garden of Death — `%tgod` on `tgod_primary` (0..54→56);
  tent journal / secateurs / four garden tablets / vines / translate soft /
  warning note; farming 10000 XP; wiki
  https://oldschool.runescape.wiki/w/The_Garden_of_Death/Quick_guide (+ Transcript);
  `::gardenofdeath` / `::godrun`; headless OK (`godrun OK` payload); pack 0 errors;
  deferred IF 804 puzzle, Boaty matrix, poison, farming 20 hard gate; next =
  At First Light (#15)
- slice 15 done: At First Light — `%afl` on `afl_main` (0..11→12); Apatura /
  Verity / Wolf+Kiko soft / fox poultice / Atza trim / report+bed / finish;
  hunter 4500 + construction 800 + herblore 500 XP; wiki
  https://oldschool.runescape.wiki/w/At_First_Light/Quick_guide (+ Transcript);
  `::atfirstlight` / `::aflrun`; headless OK (`aflrun OK` payload); pack 0 errors;
  deferred COTS hard gate, jerboa rolls, equipment pile IF, Master Rumours;
  next = Tale of the Righteous (#16)
- slice 16 done: Tale of the Righteous — `%shayzienquest` on `shayzienquest_main`
  (0..16→17); Phileas / library puzzle soft / Shiro / Duffy rope / altar soft /
  Gnosi / finish; wiki
  https://oldschool.runescape.wiki/w/Tale_of_the_Righteous/Quick_guide (+ Transcript);
  `::taleoftherighteous` / `::torrun`; headless OK (`torrun OK` payload); pack 0
  errors; deferred library puzzle, lizardman boss, CoK favour hard gate; next =
  Getting Ahead (#17)
- slice 17 done: Getting Ahead — `%ga` on `ga_main` (0..32→34); Gordon / Mary /
  flour lure soft / beast kill soft / clay→fur→dye head / mount; crafting 4000 +
  construction 3200 XP; wiki
  https://oldschool.runescape.wiki/w/Getting_Ahead/Quick_guide (+ Transcript);
  `::gettingahead` / `::garun`; headless OK (`garun OK` payload); pack 0 errors;
  also unblocked pestcontrol (shield-drop varps + deduped constants); deferred
  beast combat, tannery UI; next = The Corsair Curse (#18)
- lane B claimed (2026-08-04): parallel Quest Helper worker takes #19 Below Ice
  Mountain (`in_progress`); leaves #18 Corsair Curse for the primary lane —
  plan `questhelper_port_lane_b_bim.plan.md`
- slice 18 done: The Corsair Curse — `%corscurs_progress` on `corscurs`
  (0..55→60) + crew curse bits; Tock / sail / four crew soft / food→Ithoi burn
  soft / kill soft / finish; 2 QP; wiki
  https://oldschool.runescape.wiki/w/The_Corsair_Curse/Quick_guide (+ Transcript);
  `::corsaircurse` / `::ccrun`; headless OK (`ccrun OK` payload); pack 0 errors;
  deferred per-crew investigations, Ithoi combat, Yusuf bank UI; next =
  Below Ice Mountain (#19)
- slice 19 done (lane B): Below Ice Mountain — `%bim` on `bim_main` (0..40→45)
  + `%bim_checkal`/`%bim_marley`/`%bim_burntof` on `bim_extra`; Willow / Checkal
  + Atlas flex soft / Marley steak sandwich / Burntof ale+RPS soft / dungeon
  soft / Ancient Guardian soft / finish; 1 QP + 2000 coins + flex emote bit;
  wiki https://oldschool.runescape.wiki/w/Transcript:Below_Ice_Mountain +
  Quick_guide; `::belowicemountain` / `::bimrun`; headless OK (`bimrun OK`
  payload); pack 0 errors; deferred Atlas workout, Charlie tramp, full RPS,
  dungeon instance, mining pillars, Ramarno post-quest, QP16 hard gate; next =
  Shadows of Custodia (#20)
- lane B loop armed (2026-08-04): AGENT_LOOP_TICK_questhelper_port_b every ~180s;
  claimed #21 Current Affairs (`in_progress`, lane B) — #20 Shadows left to
  primary lane
- slice 21 done (lane B): Current Affairs — `%current_affairs` on
  `current_affairs_main` (0..40→45) + form Q bits; Arhein / Catherine form
  soft / Harry mayorfish / audit soft / form 7r4-5h sign / duck chart soft;
  fishing 1000 XP + 25 oak sawmill coupons + duck/mayor; sailing XP deferred;
  wiki https://oldschool.runescape.wiki/w/Current_Affairs/Quick_guide;
  `::currentaffairs` / `::carun`; headless OK (`carun OK` payload); pack 0
  errors; deferred Pandemonium/Sailing22 gates, form IF, boat sail, duck path;
  next pending for lane B = #22 Twilight's Promise (skip #20 if still
  in_progress on primary)
- slice 20 done: Shadows of Custodia — `%soc` on `soc_main` (0..22→24) + citizen /
  wall / bow / stalker side bits; noticeboard / four citizens / parents / wall→
  puddle→plank cloth / cave boys soft / reinforce+bows / Etz / Antos stalkers
  soft / Captain finish; 2 QP + slayer 10000 + hunter 4000 + fishing 3000 +
  construction 3000 XP (tenths); wiki
  https://oldschool.runescape.wiki/w/Shadows_of_Custodia/Quick_guide (+ Transcript);
  `::shadowsofcustodia` / `::socrun`; headless OK (`socrun OK` payload=24); pack 0
  errors; deferred full refuse trees, fishing anim, stalker combat, dungeon UI;
  next = Twilight's Promise (#22) if #21 still lane B, else Current Affairs (#21)
- slice 22 done: Twilight's Promise — `%vmq2` on `vmq2_primary` (0..48→50) +
  knight side bits / crest / letter / feed / first travel; Regulus→Ennius→
  Metzli/crypt→four knights soft→HQ letter→Renu→Teomat cultists soft→finish;
  1 QP + thieving 3000 XP (tenths); wiki
  https://oldschool.runescape.wiki/w/Twilight%27s_Promise/Quick_guide (+ Transcript);
  `::twilightspromise` / `::tprun`; headless OK (`tprun OK` payload=23); pack 0
  errors; deferred knight matrices, HQ stairs, Quetzal UI, cultist instance;
  next = Sleeping Giants (#23)
- slice 23 done (lane B): Sleeping Giants — `%sleeping_giants` on
  `giants_foundry_main` (0..25→30) + repair/tutorial bits; Kovac start /
  polish+grind+hammer repairs soft / commission crate→crucible→mould→preform
  soft / hand-in; smithing 6000 XP; wiki
  https://oldschool.runescape.wiki/w/Sleeping_Giants/Quick_guide;
  `::sleepinggiants` / `::sgrun`; headless OK (`sgrun OK` payload); pack 0
  errors; deferred mould IF 718, heat/temp loop, supply matrix, Smithing 15
  hard gate; next = Meat and Greet (#24)
- slice 24 done: Meat and Greet — `%mag` on `mag_primary` (0..24→26) + spice/meat
  supply + portion bits; Emelio→spice soft→Alba/direwolf soft→recipe 4/2/1/3→
  Renata soft→Lelia→minotaur soft→finish; 1 QP + cooking 8000 XP (tenths); wiki
  https://oldschool.runescape.wiki/w/Meat_and_Greet/Quick_guide (+ Transcript);
  `::meatandgreet` / `::mgrun`; headless OK (`mgrun OK` payload=23); pack 0
  errors; also unblocked barrows_puzzle (`:1`/`:2`/`:3` → `puzzle_q*`); deferred
  pin-pad IF, wolf den, connoisseur matrix, minotaur combat, shop UI; next =
  Pandemonium (#25)
- slice 25 done (lane B): Pandemonium — `%sailing_intro` on `sailing_intro_primary`
  (0..48→50) + wreck/cargo bits; Will/Anne→board/nav/salvage soft→Ribs/Steve/Jim
  raft+cargo-hold soft→courier deposit soft→finish; coupons/kits/spyglass;
  sailing XP deferred; wiki
  https://oldschool.runescape.wiki/w/Pandemonium/Quick_guide;
  `::pandemonium` / `::pandrun`; headless OK (`pandrun OK` payload=25); pack 0
  errors; deferred helm/sail, salvage cutscene, shipyard portal, cargo IF,
  port-task UI; next pending for lane B = The Red Reef (#27) (#26 owned by
  primary)
- slice 27 done (lane B): The Red Reef — `%trr` on `trr_primary` (0..40→42) +
  display-case bits; Raley→Finn→Katt→Floopa→Red Rock receptionist/cases→Paxton
  →Bethel soft→Zenith dive/dredger soft→Floopa finish; smithing 5000 XP
  (tenths) + bosun schematic; sailing XP deferred; wiki
  https://oldschool.runescape.wiki/w/The_Red_Reef/Quick_guide;
  `::redreef` / `::rrrun`; headless OK (`rrrun OK` payload=23); pack 0 errors;
  deferred Tortugans/Sailing52/Smithing48 gates, ship combat, Last Light fights,
  dive instance, lobster; next pending for lane B = #29 Fremennik Exiles (#28
  owned by primary)
- slice 29 done (lane B): The Fremennik Exiles — `%vikingexile` on
  `quest_vikingexile` (0..125→130) + letter/shield bits; Brundt kegs→Freygerd
  investigate soft→letter→SE shield soft→defence/Isle/Typhor/Jorm soft→finish;
  slayer+crafting 50000 + runecraft 30000 XP (tenths) + V's shield; wiki
  https://oldschool.runescape.wiki/w/The_Fremennik_Exiles/Quick_guide;
  `::fremennikexiles` / `::fxrun`; headless OK (`fxrun OK` payload=24); pack 0
  errors; deferred hard gates, shield craft matrix, basilisk wave, puzzle IF,
  boss fights; next pending for lane B = #31 Making Friends with My Arm (#30
  done by primary)
- slice 31 done (lane B): Making Friends with My Arm — `%my2arm_status` on
  `my2arm_perm_1` (0..196→200); Burntmeat→My Arm→Larry/Weiss soft→Mother→WOM
  coffin/apothecary soft→prison bosses soft→Snowflake dung/notes; con 10k +
  FM 40k + mining/agility 50k XP (tenths); wiki
  https://oldschool.runescape.wiki/w/Making_Friends_with_My_Arm/Quick_guide;
  `::makingfriendswithmyarm` / `::mfrun`; headless OK (`mfrun OK` payload=24);
  pack 0 errors; deferred boat/cliff/cave pathing, coffin IF, boss fights,
  fire-pit unlock; next pending for lane B = #33 Perilous Moons (#32 owned by
  primary)
- slice 33 done (lane B): Perilous Moons — `%pmoon_quest` on `pmoon_main`
  (0..31→36) + camp/boss bits; Attala→nagua soft→Jessamine→Neypotzli camps
  soft→Nahta/smith→Eyatlalli items soft→three Moons soft→finish; slayer 40k +
  RC/hunter/fish 5k XP (tenths); wiki
  https://oldschool.runescape.wiki/w/Perilous_Moons/Quick_guide;
  `::perilousmoons` / `::pmrun`; headless OK (`pmrun OK` payload=23); pack 0
  errors; deferred nagua combat, camp construction, talisman matrix, gather
  loops, Moon bosses; next pending for lane B = #35 Death on the Isle (#34
  owned by primary)
- slice 35 done (lane B): Death on the Isle — `%doti` on `doti_main` (0..49→50)
  + guest/clue bits; Patzi→butler uniform soft→intros→cellar murder soft→
  guards/evidence soft→Adala soft→theatre/Naiatli soft→finish; thieving 10k +
  agility 7.5k + crafting 5k XP (tenths); wiki
  https://oldschool.runescape.wiki/w/Death_on_the_Isle/Quick_guide;
  `::deathontheisle` / `::dirun`; headless OK (`dirun OK` payload=23); pack 0
  errors; deferred steal/equip checks, clue matrix, pickpockets, boss fights;
  next pending for lane B = #37 Beneath Cursed Sands (#36 owned by primary)
- slice 37 done (lane B): Beneath Cursed Sands — `%bcs` on `bcs_primary`
  (0..106→108); Jamila message→Maisa→Necropolis/guard soft→furnace/emblem/
  tomb soft→Champion soft→Zahur cure soft→Akh/Osman→finish; agility 50k XP
  (tenths) + Keris partisan + water circlet; wiki
  https://oldschool.runescape.wiki/w/Beneath_Cursed_Sands/Quick_guide;
  `::beneathcursedsands` / `::bcsrun`; headless OK (`bcsrun OK` payload=25);
  pack 0 errors; deferred guard/scarab/Champion/Akh fights, riddle/chemistry
  IF; next pending for lane B = #39 Secrets of the North (#38 owned by
  primary)
- slice 39 done (lane B): Secrets of the North — `%sotn` on `sotn_primary`
  (0..88→90) + inspect/trail/ghorrock bits; Carnillean guard→crime scene/
  hunter trail/Evelot soft→Hazeel cult/crest soft→Weiss/Snowflake/assassin
  soft→Ghorrock puzzle/Muspah soft→Jhallan→finish; agility 60k + thieving 50k
  + hunter 40k XP (tenths); wiki
  https://oldschool.runescape.wiki/w/Secrets_of_the_North/Quick_guide;
  `::secretsofthenorth` / `::snrun`; headless OK (`snrun OK` payload=23);
  pack 0 errors; deferred explain matrix, hunter trail, Evelot/assassin/
  Muspah fights, cult Q&A, brazier puzzle; next pending for lane B = #42
  Heart of Darkness (#41 owned by primary)
- slice 42 done (lane B): The Heart of Darkness — `%vmq3` on `vmq3_primary`
  (0..74→76) + recruit/trial/ruins bits; Itzla→Gorge pub/shop soft→tower
  recruits/trials soft→temple/Fides→ruins mine/levers/statues soft→Amoxliatl
  soft→Servius; mining/thieving/slayer/agility 8k XP each (tenths); wiki
  https://oldschool.runescape.wiki/w/The_Heart_of_Darkness/Quick_guide;
  `::heartofdarkness` / `::hodrun`; headless OK (`hodrun OK` payload=24);
  pack 0 errors; deferred tower puzzles/combat, robes equip, ice statue
  matrix, Amoxliatl fight; next pending for lane B = #44 Sins of the Father
  (#43 owned by primary)
- slice 44 done (lane B): Sins of the Father — `%myq5` on `myq5_primary`
  (0..136→138) + team/lab bits; Veliaf→Slepe/Kroy soft→Pater/Ivan trek soft→
  Vanescula/lab/Damien soft→Darkmeyer valves/flail soft→Vanstrom soft; blisterwood
  flail + 6 lamps + Drakan's medallion; wiki
  https://oldschool.runescape.wiki/w/Sins_of_the_Father/Quick_guide;
  `::sinsofthefather` / `::softrun`; headless OK (`softrun OK` payload=26);
  pack 0 errors; deferred Carl follow, temple trek instance, door puzzle,
  boss fights, craft IF; next pending for lane B = #46 Monkey Madness II
  (#45 owned by primary)
- slice 46 done (lane B): Monkey Madness II — `%mm2_progress` on `mm2_primary`
  (0..190→195) + sabotage/breach bits; Narnode→Glough/Anita/Entrana soft→
  Garkor/Kruk greegree soft→Kob/Keef/sabotage/lab soft→Nieve/Stronghold/
  Glough soft; slayer 80k + agility 60k + thieving/hunter 50k XP (tenths) +
  royal seed pod; wiki
  https://oldschool.runescape.wiki/w/Monkey_Madness_II/Quick_guide;
  `::monkeymadnessii` / `::mm2run`; headless OK (`mm2run OK` payload=26);
  pack 0 errors; deferred house puzzle, agility dungeon, fights, sabotage
  pathing; next pending for lane B = #48 Desert Treasure II (#47 owned by
  primary)
- slice 48 done (lane B): Desert Treasure II — `%dt2` on `dt2_primary`
  (0..114→118); vault/Asgarnia→Digsite war room soft→four medallion soft-skips
  →cell/Stranger/wights soft→finish; Ring of Shadows + 3 lamps; wiki
  https://oldschool.runescape.wiki/w/Desert_Treasure_II_-_The_Fallen_Empire/Quick_guide;
  `::deserttreasureii` / `::dt2run`; headless OK (`dt2run OK` payload=25);
  pack 0 errors; deferred digsite puzzle, Forgotten Four fights/instances,
  cell escape; next for lane B = miniquests M1/M2 (deprioritised) or idle
  until #47 Song of the Elves frees / primary needs help
- slice M2 done (lane B): Enter the Abyss — `%abyssal_miniquest` on
  `abyssal_miniquest` (0..3→4); wildy Mage→Varrock→scrying orb + three
  essence teleports (Aubury/Sedridor/Cromperty via `%rcu_essencespot_*` on
  `abyssal_warp`)→reward (book+small pouch+1000 RC XP); TOE zammy gated;
  wiki https://oldschool.runescape.wiki/w/Enter_the_Abyss/Quick_guide;
  2009scape ZamorakMageDialogue ref; `::entertheabyss` / `::etarun`;
  headless OK (`etarun OK` payload=23); pack 0 errors; deferred full refuse
  trees, Wanted! branch, Abyss terrain/tele map; next for lane B = idle
  (M1 Bear Your Soul owned elsewhere; main table clear)
- slice 26 done: A Night at the Theatre — `%tobquest` on `tobquest_main` bits
  8..14 (0..80→86) + `%tobquest_done_tob`; stranger→crypt/head→spider cave/
  Daer→eggs→Hespori bark soft→ToB soft→finish; 2 QP + 4 antique lamps; wiki
  https://oldschool.runescape.wiki/w/A_Night_at_the_Theatre/Quick_guide (+ Transcript);
  `::nightatthetheatre` / `::nattrun`; headless OK (`nattrun OK` payload=25);
  pack 0 errors; deferred crypt puzzle, araxyte pathing, Hespori fight, ToB
  raid, lamp Rub; next = Misthalin Mystery (#28) if #27 still lane B, else
  The Red Reef (#27)
- slice 28 done: Misthalin Mystery — `%mistmyst_progress` on `mistmyst_main`
  (0..130→135); Abigale→barrel/key→manor doors soft→candles/fuse/piano/
  fireplace soft→Abigale fight soft→Mandy finish; 1 QP + crafting 600 XP
  (tenths); wiki
  https://oldschool.runescape.wiki/w/Misthalin_Mystery/Quick_guide (+ Transcript);
  `::misthalinmystery` / `::mmrun`; headless OK (`mmrun OK` payload=24); pack 0
  errors; deferred manor instance, candle/piano/switch puzzles, boss fight;
  next = The Fremennik Exiles (#29) if #27 still lane B, else The Red Reef (#27)
- slice 30 done: A Taste of Hope — `%myq4` on `myq4_main` (0..160→165); Garth→
  Safalaan/spy soft→Flaygian→Serafina potion soft→abomination soft→flail→
  Ranis soft→finish; 1 QP + Ivandis flail + Drakan's medallion + 3 tomes; wiki
  https://oldschool.runescape.wiki/w/A_Taste_of_Hope/Quick_guide (+ Transcript);
  `::tasteofhope` / `::tohrun`; headless OK (`tohrun OK` payload=25); pack 0
  errors; deferred rooftop spy, potion matrix, abom/Ranis fights, tome Rub;
  next = Making Friends with My Arm (#31) if #29 still lane B, else Fremennik
  Exiles (#29)
- slice 32 done: Temple of the Eye — `%tote` on `tote_primary` (0..125→130);
  Persten→Zamorak mage/tea→Abyss energies soft→Sedridor/Traiborn puzzle soft→
  temple/GoTR tutorial soft→finish; 1 QP + runecraft 9210 XP (tenths) + medium
  pouch + amulet; wiki
  https://oldschool.runescape.wiki/w/Temple_of_the_Eye/Quick_guide (+ Transcript);
  `::templeoftheeye` / `::toerun`; headless OK (`toerun OK` payload=25); pack 0
  errors; deferred Abyss touch matrix, Traiborn IF, GoTR instance; next =
  Perilous Moons (#33) if #31 still lane B, else Making Friends with My Arm (#31)
- slice 34 done: Troubled Tortugans — `%tt` on `tt_primary` (0..42→44) + repair
  bits; Blunn→bandage Floopa→sail soft→elders→town repair soft→trail/cave/
  gryphon soft→Shellbane soft→finish; 1 QP + slayer 8000 XP (tenths); sailing
  XP deferred; wiki
  https://oldschool.runescape.wiki/w/Troubled_Tortugans/Quick_guide (+ Transcript);
  `::troubledtortugans` / `::ttrun`; headless OK (`ttrun OK` payload=23); pack 0
  errors; also unblocked puropuro (`%` → `modulo`); deferred sail matrix, hunt
  trail, gryphon fights; next = Death on the Isle (#35) if #33 still lane B,
  else Perilous Moons (#33)
- slice 36 done: Scrambled! — `%scrambled` on `scrambled_primary` (0..28→30) +
  kings-men bits; Alan→inspect egg→King→gather men→sample eggs soft→judge/
  panic soft→put Humpty together soft→finish; 1 QP + construction/cooking/
  smithing 50000 XP each (tenths); wiki
  https://oldschool.runescape.wiki/w/Scrambled!/Quick_guide (+ Transcript);
  `::scrambled` / `::scrun`; headless OK (`scrun OK` payload=23); pack 0
  errors; deferred egg side-tasks (axe/tea/jaguar), judge IF, pet-egg unlock;
  next = The Final Dawn (#38) while #37 Beneath Cursed Sands is lane B
- slice 38 done: The Final Dawn — `%vmq4` on `vmq4_primary` (0..67→68); Servius
  → Twilight Temple soft → Queen/Vibia → Janus house/dog/hideout soft → Attala/
  Cam Torum soft → keystone/cultists soft → Tal Teklan → Crypt of Tonali soft
  (Ennius/Metzli/final) → chamber inspect → finish; 3 QP + thieving 550000 +
  fletching/runecraft 250000 XP each (tenths) + Arkan blade + lamp; wiki
  https://oldschool.runescape.wiki/w/The_Final_Dawn/Quick_guide (+ Transcript);
  `::finaldawn` / `::tfdrun`; headless OK (`tfdrun OK` payload=24); pack 0
  errors; deferred basement combat, Janus puzzles, Neypotzli sun/moon, boss
  fights, lamp Rub; next = The Forsaken Tower (#40) while #39 Secrets is lane B
- slice 40 done: The Forsaken Tower — `%lovaquest` on `lovaquest_main` (0..10→11)
  + furnace/electricity/refinery/altar bits; Vulcana→Undor→tower entry→four
  puzzle soft-skips→Dinh's hammer→Undor→Vulcana; 1 QP + mining/smithing 5000
  XP each (tenths) + 6000 coins + memoirs page; wiki
  https://oldschool.runescape.wiki/w/The_Forsaken_Tower/Quick_guide (+ Transcript);
  `::forsakentower` / `::ftrun`; headless OK (`ftrun OK` payload=23); pack 0
  errors; deferred jug/power-grid/fluid/pylon puzzles, Ignisia gate; next =
  A Kingdom Divided (#41) while #39 Secrets is lane B
- slice 41 done: A Kingdom Divided — `%akd` on `akd_primary` (0..148→150) +
  house-help bits; Martin→Fullore→Hughes/Herbert soft→Yama soft→Rose trail/
  Forthos/Settlement/Faun soft→Kaht egg→Xamphur soft→burial→Lookout house
  help soft→finish; 2 QP + Book of the Dead + lamp; wiki
  https://oldschool.runescape.wiki/w/A_Kingdom_Divided/Quick_guide (+ Transcript);
  `::kingdomdivided` / `::akdrun`; headless OK (`akdrun OK` payload=25); pack 0
  errors; deferred house search, fights, puzzles, house side-quests, lamp Rub;
  next = The Curse of Arrav (#43) while #42 Heart of Darkness is lane B
- slice 43 done: The Curse of Arrav — `%coa` on `coa_primary` (0..58→60);
  Elias→mastaba doors/golem/tile soft→canopic→Trollweiss soft→fort key soft→
  base heist/Arrav soft→finish; 2 QP + mining/thieving/agility 400000 XP each
  (tenths); wiki
  https://oldschool.runescape.wiki/w/The_Curse_of_Arrav/Quick_guide (+ Transcript);
  `::curseofarrav` / `::coarun`; headless OK (`coarun OK` payload=24); pack 0
  errors; also unblocked giantmole (`%` → `modulo`); deferred levers, golem,
  tiles, cave pathing, fights; next = Dragon Slayer II (#45) while #44 Sins is lane B
- slice 45 done: Dragon Slayer II — `%ds2` on `dragonslayer2_main` (0..210→215);
  Alec→Dallas Crandor soft→Fossil map/boat soft→Lithkren diary soft→Bob/Sphinx/
  dream/key soft→Roald allies soft→Ungael/Galvek soft→finish; 5 QP + smithing
  800000 + mining 600000 + agility/thieving 500000 XP (tenths) + orb + 4 lamps;
  wiki https://oldschool.runescape.wiki/w/Dragon_Slayer_II/Quick_guide (+ Transcript);
  `::dragonslayer2` / `::ds2run`; headless OK (`ds2run OK` payload=25); pack 0
  errors; deferred mural/spawn/map IF/boat build/dream fight/ship combat/Galvek
  phases/lamp Rub; next = Song of the Elves (#47) while #46 Monkey Madness II is lane B
- slice 47 done: Song of the Elves — `%sote` on `sote_primary` (0..192→200);
  Edmond→Lathas/Alrena soft→Elena free/revolt soft→Arianwyn/Baxtorian/clans/
  seals soft→orb/Lletya/Pass/Essyllt/final soft→finish; 4 QP + 8×400000 XP
  (tenths); wiki
  https://oldschool.runescape.wiki/w/Song_of_the_Elves/Quick_guide (+ Transcript);
  `::songoftheelves` / `::soterun`; headless OK (`soterun OK` payload=26); pack 0
  errors; also unblocked Inferno duplicate Zuk stub; deferred revolt/puzzles/
  fights; next = Desert Treasure II (#48) if lane B frees it, else miniquests M1/M2
- slice M1 done: Bear Your Soul — `%arceuus_soulbearer_story` on `millcheck_multi`
  (0..2→3); soft book→Aretha→dig damaged bearer→Key Master repair; Soul Bearer;
  journal + spade `~bys_try_dig` + `keeper_of_keys` merge; LostCity none; wiki
  https://oldschool.runescape.wiki/w/Bear_Your_Soul; `::bearyoursoul` / `::bysrun`;
  headless OK (`bysrun OK` payload=23); pack 0 errors; deferred library bookcase
  search, dig anim, dusty-key pathing; next = Enter the Abyss (M2) if lane B frees
  it, else idle (main quest table complete through #48)
- queue expanded (tick): added IN-LC table (33 pre-Sept 2004 QH dirs → CONTENT_PORT_QUEUE),
  expanded skip list; added 5 pending entries for genuine post-Jan-2009 QuestHelper-only content:
  P1 atailoftwocats (Apr 2016, 293 lines), P2 asoulsbane (Mar 2019, 330 lines),
  P3 spiritsoftheelid (Dec 2013, 352 lines), P4 anothersliceofham (Oct 2012, 485 lines),
  P5 darknessofhallowvale (Aug 2013, 816 lines); ~74 remaining QH dirs classified as mid-era
  pre-2009 → SCAPE2009_CONTENT_PORT_QUEUE; next pending = P1 A Tail of Two Cats
- tick: P1 atailoftwocats in_progress — extract clean (39 gamevals resolve), configs written
  (atailoftwocats.varp + constant, twocats varplayer 0→65), scripts written (twocats.rs2:
  dialogue trees for all 4 chapters, chore tracking via 7 twocats_chores_* varbits, quest
  complete queue); compile blocked on Windows (no make/sscompile) — needs Linux/macOS env
- queue rebuilt (2026-08-06): Full audit of Quest Helper source. 176 in-scope
  quests identified (181 dirs minus 5 skip-list). 50 tracked as done, 14 already
  implemented in OSRS Content but missing from the table, and roughly 112 still
  pending. Depth-first ordering is preserved.
- slice 3 re-audit (2026-08-10): row #3 Witch's Potion (`witchspotion`,
  npcs=hetty,ratindoors) re-verified per methodology step 1 — grep LostCity
  first found it already fully implemented: `server/scripts/quests/quest_hetty/`
  (`quest_hetty.rs2` cauldron+completion, `hetty_journal.rs2`) +
  `areas/rimmington/scripts/hetty.rs2` (dialogue: quest-request/witch-rumour
  branches, ingredient gate on onion+rat's_tail+burnt_meat+eye_of_newt,
  pre-drink retalk, `%hetty` 0..2→3) + `drop_tables/scripts/rat.rs2`
  (quest-gated `rats_tail` drop, capped when player already holds/banks one).
  `ratindoors` is not a resolvable osrs239 gameval (no such npc in
  `all.npc`/`.compack`) — the cache's `rat` npc is what the quest actually
  uses, cache wins per the queue's own rule. Rewards 3250 magic XP + 1 QP via
  `~quest_complete(quest_witchspotion)`; landed under
  `CONTENT_PORT_QUEUE.md` slice 6d (see its Log), not this queue. Checked
  against `https://oldschool.runescape.wiki/w/Transcript:Witch%27s_Potion` +
  `https://oldschool.runescape.wiki/w/Witch%27s_Potion/Quick_guide` — existing
  dialogue covers both initial-contact branches, the no/partial/complete
  ingredient states, the pre-drink retalk line, and the drink/complete step;
  deferred: the wiki's itemised partial-ingredient message variants (impl uses
  one generic "not yet" line for any partial set, transcript lists per-count
  wording) — cosmetic only, no missed gate. No new script written (nothing to
  port); row flipped `pending` → `done (LC)`. Verify: `mingw32-make -C src
  sscompile` clean rebuild, then `mingw32-make -C src mock230-scripts` — ran
  to completion through all `quest_hetty`/`hetty`/`rat` files with zero
  diagnostics on them; sole failure is the pre-existing, unrelated
  `ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2` missing
  `%content_restrict_summoning_serverside` (out of scope, not touched).
  Same grep-first check surfaced 4 more stale `pending` rows already fully
  LC-owned (not yet caught by the 2026-08-06 rebuild) — fixed alongside since
  the evidence was already in hand: #4 impcatcher → `quest_imp/` (`quest_imp.rs2`
  + `imp_journal.rs2`); #13 sheepshearer → `quest_sheep/` (`quest_sheep.rs2` +
  `sheep_journal.rs2`) + `areas/lumbridge/scripts/fred_the_farmer.rs2`; #27
  gertrudescat → `areas/varrock/scripts/gertrude.rs2` + `shilop.rs2`; #28
  princealirescue → `quest_prince/` (`quest_prince.rs2` + `prince_journal.rs2`)
  + `areas/alkharid/scripts/hassan.rs2` + `areas/draynor/scripts/prince_ali.rs2`.
  All 4 confirmed by file existence only (not depth-audited against wiki
  transcripts the way #3 was) — a fuller staleness pass over the remaining
  ~108 pending rows is still owed, since the table was assembled from a QH-side
  audit that never cross-checked file existence per row. Rows #9 monksfriend
  and #10 therestlessghost were spot-checked (grep for `brotheromad`,
  `fatheraerec`, `fatherurhne` — no hits beyond a pre-allocated, unimplemented
  `quest_monksfriend`/`quest_restlessghost` dbrow slot) and are genuinely
  unimplemented; next = Monk's Friend (#9)
- staleness audit (2026-08-10 tick): re-verified #9 Monk's Friend per
  methodology step 1 before porting — the prior tick's `brotheromad` grep
  (no underscore, copying the QuestHelper Java constant's spelling) missed
  the real gameval `brother_omad`; LostCity already ships it complete as
  `quest_drunkmonk` (dbrow `quest_monksfriend` id 28, journal wired
  `~drunkmonk_journal`, full Islwyn/Cedric dialogue + blanket cave + wine
  cart + party reward). Same underscore-blind-spot pattern found across the
  table once checked systematically: swept every remaining `pending` row's
  npc list against the tree (grep + directory cross-reference against the
  full `quests/` listing) and found **36 more** already fully LC-owned but
  never flipped, plus the IN-LC table's own 10 duplicates still sitting as
  separate `pending` rows in the main table below it. All 37 fixed to
  `done (LC)` with the matching LC dir + dbrow key cited inline per row:
  #9 monksfriend→quest_drunkmonk, #10 therestlessghost→quest_priest,
  #15 goblindiplomacy→quest_gobdip, #19 druidicritual (IN-LC dup),
  #23 ernestthechicken→quest_haunted, #25 fishingcontest→quest_fishingcompo,
  #29 cooksassistant (IN-LC dup), #31 trollstronghold→quest_death,
  #32 lostcity→quest_zanaris, #40 theknightssword→quest_squire,
  #41 trollromance (IN-LC dup), #42 fightarena→quest_arena,
  #45 deathplateau→quest_death, #49 tribaltotem→quest_totem,
  #50 witchshouse→quest_ball, #57 bigchompybirdhunting→quest_chompybird,
  #59 scorpioncatcher→quest_scorpcatcher, #62 horrorfromthedeep→quest_horror,
  #63 dwarfcannon (IN-LC dup), #64 familycrest→quest_crest,
  #65 insearchofthemyreque→quest_routequest, #69 treegnomevillage (IN-LC dup),
  #70 templeofikov→quest_ikov, #71 observatoryquest→quest_itgronigen,
  #74 thetouristtrap→quest_desertrescue, #81 shieldofarrav→quest_blackarmgang,
  #82 waterfallquest (IN-LC dup), #83 thegrandtree (IN-LC dup),
  #89 merlinscrystal→quest_arthur, #90 eaglespeak (IN-LC dup),
  #92 priestinperil→quest_priestperil, #93 plaguecity→quest_elena,
  #95 shilovillage→quest_zombiequeen, #96 thelosttribe (IN-LC dup),
  #97 demonslayer→quest_demon, #98 holygrail (IN-LC dup),
  #101 thegolem→quest_golem. Every non-dup fix confirmed by both the LC
  script directory's own npc files AND a `[$row = quest_x] { ~y_journal; }`
  wire in `interface_questjournal/scripts/quest_journal.rs2` — same bar the
  prior tick used, not a deeper wiki-transcript audit. **A pass over the
  remaining ~75 pending rows (line count > 263) is still owed** — this sweep
  only covered rows small enough to reach in one tick; the pattern found
  held for every row checked, so expect more.
- slice 17 done: Roving Elves — no LC/2009scape impl exists (Apr 2005
  release, past both eras); `%rovingelves_quest` on `rovingelves_quest`
  (0/10/20/30/40/50→60); Islwyn confront-then-accept (moved Glarial's
  remains trust hook) → Eluned ritual explainer → Moss Guardian kill
  (`[ai_queue3,roving_mossgiant]`, same soft-kill idiom as
  `arena_boss_deaths.rs2`) drops `roving_old_consecration_seed` → Eluned
  enchants it → `[opheld1,roving_new_consecration_seed]` Plant op buries it
  at the shared `baxtorian_chalice_waterfall_quest` loc (Waterfall Quest's
  own Glarial's crypt room, left untouched — used the item's own `ifop1`
  inventory op instead of extending that file's `oplocu` to avoid a
  duplicate-trigger error) → return to Islwyn for `~p_choice2` crystal
  bow/shield reward; 1 QP + 10000 strength XP; gated on
  `%regicide_quest = ^regicide_complete` and
  `%waterfall_quest = ^waterfall_complete` (both already `done (LC)`); wiki
  https://oldschool.runescape.wiki/w/Roving_Elves/Quick_guide +
  Transcript:Roving_Elves; Islwyn/Eluned hand-spawned via `[login,_]`
  npc_find/npc_add idempotent check (the generated `m36_49.spawn` snapshot
  was captured post-Song of the Elves, so only `sote_islwyn`/`sote_ilfeen`
  exist there — no pre-SOTE `roving_islwyn`/`eluned_prif`-at-camp entry to
  reuse); deferred: unarmed-combat restriction on the Moss Guardian fight
  (any kill counts), Golrie lost-pebble replacement dialogue, Arianwyn
  introduction (belongs to Mourning's End Part I), post-quest crystal
  equipment replacement shop (`[opnpc3,roving_islwyn_2ops]` messages
  "not set up yet" rather than silently no-opping); `mingw32-make -C src
  sscompile` clean, `mingw32-make -C src mock230-scripts` zero diagnostics
  on any new/touched file (only the pre-existing, unrelated
  `ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2` missing
  `%content_restrict_summoning_serverside` failure, out of scope); next =
  Devious Minds (#21) — re-verified pending (grep for `deviousmonk`,
  `rcuzammyma`, wizard-tower/rune-essence terms: no hits beyond the
  pre-allocated `quest_deviousminds` dbrow slot)
- slice 21 done: Devious Minds — re-verified pending per methodology step
  1-2 first (grep LostCity + 2009scape for `devious`/`uzam` variants: only
  hits were the pack's own gamevals, no `.rs2` implementation). Unlike most
  QH-only slices this one has a **fully native cache varbit schema**
  (`configs/all.varbit`, basevar `devious_base`) already shaped for the
  quest, used directly instead of inventing a fresh varp: `%devious_main`
  (bits 0-7, primary progress, values authored here 0/10/20/30/40/50→60),
  `%devious_monk_met`/`%devious_monk_orb_given`/`%devious_cutscene` (flag
  bits), `%devious_altar` (bits 22-23, multiloc: altar → pouch-placed →
  scorched) and `%devious_monk` (bits 27-28, multinpc: hooded monk ↔ dead
  monk — the game's own twist mechanism, both npcs already in
  `areas/world/configs/m53_54.spawn`). Decoded `quest_deviousminds` dbrow's
  own `startcoord`/`startnpc`/`requirement_stats`/`requirement_quests`/
  `stat_xp_awarded` fields (coord format `(plane<<28)|(x<<14)|y`, dbrow-typed
  refs resolve via `all.dbrow.compack` row id not the `id` field) to get the
  cache-authoritative data instead of guessing from the wiki: start npc
  `devious_monk_hooded` at real coord (3405,3491) matching the spawn file
  exactly; skill gate Smithing 65 (boostable) / Runecraft 50 (not boostable
  per wiki) / Fletching 50 (boostable); direct prereqs `quest_wanted`,
  `quest_trollstronghold`, `quest_dorics`, `miniquest_entertheabyss` (only
  the last three are checked — Wanted! is queue row #107, still `pending`,
  has no varp to gate on yet, and hard-gating on it would make this quest
  permanently unstartable, noted rather than faked); reward XP 6500
  Smithing / 5000 Runecraft / 5000 Fletching + 1 QP, exactly matching both
  the dbrow and the wiki. Scripts: `deviousminds_monk.rs2` (disguised monk
  offer/refuse, whetstone+bowstring reminder, bowsword→orb handoff, dead
  monk search revealing the twist), `deviousminds_items.rs2` (whetstone
  grind, bow-sword stringing, orb+pouch sealing with small/medium/giant/
  degraded-pouch rejection messages per the transcript, altar placement +
  soft-narrated heist cutscene, colossal-pouch-survives-large-pouch-destroyed
  per wiki), `deviousminds_tiffy.rs2` (hand-spawned stand-in, see below,
  "Devious Minds"/"Something else" choice, completion), `deviousminds_journal.rs2`.
  Two additive edits to shared hub files (no existing lines touched, same
  shape every other multi-quest npc/skill file already uses): one `if`
  branch in `areas/entrana/scripts/high_priest_of_entrana.rs2` (High Priest
  blame + investigate-Paterdomus + report-to-Tiffy branch) and one
  `case devious_slenderblade :` line in `skill_fletching/scripts/bows.rs2`'s
  existing `[opheldu,bow_string]` switch (the other bow-string click order —
  `[opheldu,bow_string]` is already claimed tree-wide so a second binding
  would collide; this keeps both click orders working). Sir Tiffy Cashien
  has no standalone overworld npc under any resolvable spelling in this
  cache (every `tiffy` hit is DS2/AKD cutscene-scoped) — hand-spawned the
  closest normal-pose reuse, `ds2_meeting_sir_tiffy_cashien` (unbound
  elsewhere), in Falador Park (`[mapzone,0_46_52]` confirmed by
  `skill_farming`'s own Falador Park tree sync) via the same idempotent
  `[login,_]` npc_find/npc_add pattern `rovingelves_islwyn` used. Wiki
  https://oldschool.runescape.wiki/w/Devious_Minds +
  /Quick_guide + Transcript:Devious_Minds (full verbatim reproduction
  declined by the fetch tool as Jagex-copyrighted; used its detailed
  paraphrased section-by-section summary instead — dialogue here is
  original wording covering the same beats, not a copy). Deferred: exact
  Abyss/Law Altar traversal (soft-skipped like every other slice's
  inter-area journeys), the multi-monk/assassin scene as a real client
  cutscene rather than narrated `mesbox` lines, Wanted! gate (see above).
  `mingw32-make -C src sscompile` clean; `mingw32-make -C src mock230-scripts`
  — full corpus build, zero diagnostics on any new/touched file, only
  failure in the whole tree is the pre-existing unrelated
  `ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2` missing
  `%content_restrict_summoning_serverside` (untouched, out of scope).
  Staleness sweep with spare budget: #24 A Tail of Two Cats was still
  marked `pending` in the main table despite being `done` since slice 1
  (2026-08-04) — the 2026-08-06 full-table rebuild re-added it without
  checking the tree; fixed to `done`. Also found (not a stale-LC case, a
  different kind of gap) #43/P2 Asoul's Bane already has a real 193-line
  script (`quest_asoulsbane/scripts/soulbaine.rs2`, npcs resolve) from an
  untracked earlier tick, but its dbrow row was never declared — compiles
  clean only because the id allocator remembers the old slot
  (`STALE=1 66540=quest_asoulsbane`), so `~quest_complete` would read
  name/questpoints off an undeclared row; flipped to `in_progress` with the
  gap noted rather than trusted as `done`. Next = Making History (#37,
  319 lines, npcs=makinghistor/silvermerch — re-checked 2026-08-10, no
  existing script under any spelling, genuinely pending); The Hand in the
  Sand (#38, tied at 319) is next after that.
- slice #37 done: Making History — re-verified pending first (grep of
  `Server/content/scripts` [LostCity], no `2009scape` checkout available
  locally so cross-checked the OSRS-Content tree directly, and
  `quest-helper/.../makinghistory` `--check` 100% clean, all NpcID/ItemID/
  ObjectID/VarbitID gamevals resolve). Like Devious Minds and Doric's Quest,
  the varbit schema is already native to the osrs239 cache rank-0 export
  (`configs/all.varbit`, all on basevar `makinghistory`): `%makinghistory_prog`
  (0..4, endstate=4 per `quest_makinghistory` dbrow columndef 19 — cross-
  checked against `quest_priestinperil`'s endstate=60 matching the real
  `%priestperil` scale used by the done `quest_priestperil`), plus
  `%makinghistory_trader_prog` / `%makinghistory_warr_prog` /
  `%makinghistory_ghost_prog` sub-branch counters and
  `%makinghistory_melina_pres` / `%makinghistory_droalak_pres` presence bits
  that natively multivarbit-swap the already-placed `makinghistory_melina_multi`
  / `makinghistory_droalak_multi` world spawns visible/invisible (same
  mechanism as Devious Minds' `%devious_monk`) — no new varp/varbit authored.
  All six required npcs (`makinghistory_jorral`, `silver_merchant_ardougne`,
  `makinghistory_blanin`, `makinghistory_dron`, `makinghistory_droalak_multi`,
  `makinghistory_melina_multi`) and `kinglathas` were already base world
  spawns at (or within a tile of) the helper's own WorldPoints — no hand-spawn
  needed anywhere, unlike Devious Minds' Sir Tiffy. Scripts:
  `quest_makinghistory/scripts/makinghistory_jorral.rs2` (offer/decline,
  reminder, three-branch hand-in narrating the Saradomin/Zamorak-veterans-
  reconciled-under-Guthix backstory, King Lathas letter round-trip incl. lost-
  letter recovery, completion), `makinghistory_trader.rs2` (Erin the silver
  merchant's enchanted key, the dig north of Castle Wars via the shared
  `general_use/scripts/spade.rs2` hub, key-on-chest → journal),
  `makinghistory_frem.rs2` (Blanin's briefing + Dron's 12-question riddle,
  exact short answer strings from the BSD-licensed `quest-helper`
  MakingHistory.java `addDialogStep`s — not the wiki — since those already
  mirror the game's own short chat-option labels for automation; wrong answer
  dismisses, matching the paraphrased transcript), `makinghistory_ghost.rs2`
  (Droalak/Melina reconciliation gated on a ghostspeak-amulet-worn check
  covering both `amulet_of_ghostspeak` and the Ghosts Ahoy enchanted variant,
  scroll hand-in, and a post-quest Droalak farewell beat matching the wiki's
  "Droalak can be visited to confirm scroll delivery, after which the ghost
  disappears peacefully"), `makinghistory_journal.rs2` (wired into
  `interface_questjournal/scripts/quest_journal.rs2`'s dispatch, additive
  line after the `quest_deviousminds` case). Two additive-only hub edits (no
  existing lines touched): one `if` branch in
  `areas/area_ardougne_east/scripts/king_lathas.rs2`'s existing
  `[opnpc1,kinglathas]` (already claimed by the Biohazard/Underground Pass
  chain) and one in that same file's sibling
  `ardougne_east_shops.rs2`'s existing `[opnpc1,silver_merchant_ardougne]`;
  one line added to `general_use/scripts/spade.rs2`'s existing dig-proc
  chain. Rewards: 3 QP, 1000 Crafting XP + 1000 Prayer XP (dbrow columndef 33
  stat_xp_awarded 10000/10000, passed to `stat_advance` unmodified since that
  opcode's argument is tenths per `mock230_scripts.c`'s own comment —
  cross-checked against `quest_priest`'s `stat_advance(prayer, 11250)` at
  Restless Ghost's completion, the real independently-known 1,125 Prayer XP
  reward for that quest passed the same undivided way), 750 coins, 1
  enchanted key; gate is `quest_priestinperil` FINISHED (`%priestperil` =
  `^priestperil_complete`) + `quest_restlessghost` merely started
  (`%prieststart >= ^priest_started`), matching
  `quest_makinghistory` dbrow columndef 25 requirement_quests (all.dbrow.compack
  row ids 111/120) exactly. Wiki
  https://oldschool.runescape.wiki/w/Making_History +
  /Quick_guide + a paraphrased Transcript:Making_History summary (the fetch
  tool declined verbatim reproduction of the long narration, same as Devious
  Minds before it — dialogue here is original wording covering the same
  beats). Deferred: Port Phasmatys ecto-token/charter toll (soft-skipped like
  every other slice's inter-area travel gates on this queue), the castle
  stairs as a real object trigger (narrated only), item-loss replacement
  covered for key/scroll/letters but not exhaustively re-tested. `mingw32-make
  -C src sscompile` clean; `mingw32-make -C src mock230-scripts` — full corpus
  build, zero diagnostics on any new/touched file (confirmed by grepping the
  full build log for `makinghistory`/`kinglathas`/`silver_merchant`: no
  matches outside intent), only failure in the whole tree is the pre-existing
  unrelated `ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2`
  missing `%content_restrict_summoning_serverside` (untouched, out of scope);
  dbrow allocator report shows the same single pre-existing stale row
  (`66540=quest_asoulsbane`, unrelated) and no new stale entries —
  `quest_makinghistory`'s dbrow is natively declared, not allocator-only.
  Next = The Hand in the Sand (#38, 319 lines, npcs=handsandber/handsandgua —
  not yet re-verified this tick, take the same grep-first steps before
  writing).
- slice #38 done: The Hand in the Sand -- re-verified pending first (grep of
  the LostCity `Server/content` checkout on this machine for `handsand`/
  `hand.*sand` name variants: zero hits; no 2009scape checkout is available
  locally, same gap prior ticks noted, so cross-checked the OSRS-Content tree
  directly instead: no `quest_thehandinthesand`/`quest_handinthesand`
  directory existed, and `tools/questhelper_extract.py --check` on the
  helper's own dir resolved every ItemID/NpcID/ObjectID/VarbitID gameval
  clean, `dbrow.quest_thehandinthesand` UNRESOLVED because the real cache
  name differs). Like Devious Minds/Making History, this quest has a fully
  **native cache schema**: dbrow `quest_handinthesand` (not
  `quest_thehandinthesand` -- cache wins on the name split, id 102, endstate
  160, startnpc 5382 `handsand_bert`, startcoord decodes to (2551,3100,0)
  matching quest-helper's own WorldPoint to a tile, requirement_stats
  Crafting 49 / Thieving 17 with no `requirements_boostable` column set so
  both gate on `stat_base` per the wiki's "not boostable" note,
  stat_xp_awarded 90000/10000 tenths = 9000 Crafting / 1000 Thieving XP
  matching the wiki exactly, no requirement_quests column) and a native
  varbit schema on basevar `handsand`: `%handsand_quest` (bits 0-8, primary
  progress, authored 0/10/20.../150 exactly matching quest-helper's own
  `steps.put` scale plus 160 for complete), `%handsand_question1/2/3` (single
  bits, the three interrogation questions), `%handsand_tele` (Rarve's
  one-time Port Sarim teleport -- quest-helper's own `notTeleportedToSarim`
  VarbitRequirement reads this exact varbit at 0), `%handsand_serum` (bits
  13-15, values 1 and 5 are the client's own real checkpoints per
  quest-helper's `receivedBottledWater`/`madeTruthSerum` VarbitRequirements,
  the intermediate redberry-juice/pink-dye/rose-lens states tracked by item
  possession exactly as quest-helper itself tracks them). `handsand_transmit`
  basevar covers three more native multiloc/multinpc swap bits used
  cosmetically (authored values, nothing pre-existing reads them):
  `%handsand_sandy_multi` (swaps the two real world-spawned Sandy shells,
  `handsand_sandy`/`handsand_sandy_looking`,
  `areas/world/configs/m43_49.spawn`), `%handsand_coffee_multi` (mug present/
  used on loc `handsand_coffee_multiloc`), `%handsand_counter_multi` (Betty's
  counter, value 1 = vial placed is quest-helper's own confirmed `vialPlaced`
  checkpoint, value 2 = light-focused is authored). All five npcs are already
  base world spawns needing no hand-spawn: `handsand_bert` (5382,
  `m39_48.spawn`; itself a multivarbit shell whose whole nonzero range
  renders as quest-helper's own `HANDSAND_BERT_1OP`, but the real clickable
  entity stays the base id), `handsand_guard_captain` (5383, same file),
  `handsand_sandy`/`handsand_sandy_looking` (6405/6537, `m43_49.spawn`),
  `handsand_naziom` "Mazion" (5386, `m44_52.spawn`), and Zavistic Rarve is the
  **same** `zogre_human_zavistic_rarve` (881) already spawned at the guild
  itself for Zogre Flesh Eaters (`m40_48.spawn`) -- confirmed by both quests
  independently claiming the one shared "Bell" loc gameval
  (`zogre_outdoor_bell`, op1 Ring) for their own bell-summons-Rarve scene, and
  by the spawn coord sitting right by the bell. Scripts:
  `quest_handinthesand/scripts/handsand_bert.rs2` (offer/decline/qualify
  gate, hand handoff, rota exchange, scroll exchange),
  `handsand_guard.rs2` (beer-for-hand, dual talk/item-use binding),
  `handsand_rarve.rs2` (the whole bell arc: hand intake, scroll intake +
  orb + one-time Port Sarim teleport, evidence-orb intake, earth-runes/sand
  pit enchant, wizard's-head intake + `~quest_complete(quest_handinthesand)`),
  `handsand_sandy.rs2` (talk dispatch, custom pickpocket for `handsand_sand`
  gated on Thieving 17 via `stat_base`, desk search for the second rota,
  distraction dialogue, coffee-mug serum use, magical-orb Activate
  (`opheld1`, the item's own native `ifop1=Activate`), three-question
  interrogation), `handsand_betty.rs2` (dispatch, the full redberries ->
  redberry juice -> +white berries -> pink dye -> +lantern lens -> rose lens
  recipe chain, and the vial-on-counter + lens-through-the-doorway
  `distance()`-gated focusing step -- two empty vials total, matching
  quest-helper's own `vial2` item requirement exactly: one becomes the
  bottled water, one is shattered on the counter), `handsand_mazion.rs2`
  (skull handoff on Entrana), `handsand_journal.rs2` (wired into
  `interface_questjournal/scripts/quest_journal.rs2`'s dispatch, additive
  line after the `quest_makinghistory` case). Four additive-only hub edits
  (no existing lines touched): a guarded proc
  (`[proc,zfe_bell_or_handsand]`) plus one delegating branch each on
  `quest_zogreflesheaters/scripts/zogre_finish.rs2`'s existing
  `[oploc1,zogre_outdoor_bell]` and `[opnpc1,zogre_human_zavistic_rarve]`
  (both already claimed by that quest, shared gameval ids); one `else if`
  branch in `areas/port_sarim/scripts/betty.rs2`'s existing `betty_chat`
  label (already claimed, and already carrying one such branch for
  Ethically Acquired Antiquities' `%eaa`); one `else if` branch each in
  `skill_cooking/scripts/cooking_inv/scripts/pies.rs2`'s existing
  `[opheldu,redberries]` and `skill_herblore/scripts/brew_potion.rs2`'s
  existing `[opheldu,white_berries]` (both items already had their own
  claimed reciprocal trigger for unrelated recipes, so the new chain's other
  direction -- `[opheldu,handsand_bottle_water]` /
  `[opheldu,handsand_redberry_juice]` -- is bound fresh in this quest's own
  file instead of colliding). Wiki
  https://oldschool.runescape.wiki/w/The_Hand_in_the_Sand +
  /Quick_guide + the wiki's own detailed walkthrough page (the fetch tool
  declined verbatim Transcript reproduction, Jagex-copyrighted, same as
  Devious Minds/Making History before it; dialogue here is original wording
  covering the same beats: Bert's hand-in-the-sandpit discovery, the Guard
  Captain's beer-soaked fumble, Rarve identifying Clarence, the rota
  mismatch, Sandy's coffee-mug distraction and confession under truth serum
  to bribery/mind-magic/murder, and Mazion's "keep your hair on" skull
  handoff on Entrana). Deferred: the exact "giant mutant herring / pygmy
  shrew" trial-and-error distraction dialogue (soft-skipped to a single
  flavour choice, any topic works, same tier Porcine of Interest and Below
  Ice Mountain used for their own dialogue-guessing minigames), the "84
  buckets of sand daily from Bert" and "Betty sells pink dye" post-quest
  unlocks (no daily-reset primitive exists anywhere in this tree yet to hang
  the first on, and Betty's shop stock stays the pre-existing
  `inv.ini`-deferred stub the same as Ethically Acquired Antiquities left
  it), exact client multinpc render-bucket semantics for `handsand_bert`'s
  8-slot swap table (triggers bind the real spawned base id regardless, so
  play is unaffected), and Entrana weapon/armour banking (soft-skipped like
  every other slice's inter-area travel gates on this queue).
  `mingw32-make -C src sscompile` clean; `mingw32-make -C src mock230-scripts`
  -- full corpus build, zero diagnostics on any new/touched file (confirmed
  by grepping the full build log for `handsand`/`betty.rs2`/`zogre_finish`/
  `pies.rs2`/`brew_potion`/`quest_journal.rs2`: no matches outside intent),
  only failure in the whole tree is the pre-existing unrelated
  `ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2` missing
  `%content_restrict_summoning_serverside` (untouched, out of scope); dbrow
  allocator report shows the same single pre-existing stale row
  (`66540=quest_asoulsbane`, unrelated) and no new stale entries --
  `quest_handinthesand`'s dbrow is natively declared, not allocator-only.
  Next = Spirits of the Elid (#51 / P3, 352 lines, npcs=elidmayor,
  elidghaslor, elidranging -- not yet re-verified this tick, take the same
  grep-first steps before writing; rows #39-50 in between are all already
  `done`/`done (LC)`/`in_progress` (Asoul's Bane, #43) per the existing
  table, none newly stale-checked this tick).
- slice #51 done: Spirits of the Elid -- re-verified pending first (grepped
  the local LostCity `Server/content/scripts` checkout for `elid`/`khazard`
  name variants: only false-positive hits, e.g. `shantaypass.inv`'s own
  substring and `quest_tree`'s unrelated `khazard_warlord`; no LC quest
  implements it. No 2009scape checkout is available on this machine, same
  gap prior ticks on this queue have noted, so the OSRS-Content tree itself
  was cross-checked directly: no `quest_spiritsoftheelid` directory existed
  before this slice). Like Making History / The Hand in the Sand, this quest
  has a fully **native cache schema**: dbrow `quest_spiritsoftheelid`
  (`configs/all.dbrow`, id 100, startnpc 4756 `elid_mayor`, endstate 60,
  questpoints 2, requirement_stats Thieving 37 / Mining 37 / Ranged 37 /
  Magic 33 all `requirements_boostable=1` -- gated with `stat()` not
  `stat_base()` -- stat_xp_awarded Prayer 8000 / Thieving 1000 / Magic 1000
  XP tenths, all matching the wiki's requirement/reward lists exactly) and a
  native varbit schema on basevar `elid_main`: `%elidquest` (bits 0-6,
  0..127) whose own authored breakpoints are readable straight off this
  cache's `elid_fountain_multiloc` / `elid_statuette_multiloc` swap tables
  (`configs/all.loc`) -- the only explicit non-inherited transitions are at
  0, 10, 20, 25, 27, 30, 35, 40, 50, 55, 60, **exactly** quest-helper's own
  `steps.put` keys plus 60 for complete, cross-checked directly against
  `com/questhelper/helpers/quests/spiritsoftheelid/SpiritsOfTheElid.java`'s
  `loadSteps()` -- so this port authors those same ten breakpoints, the
  Hand in the Sand convention, with everything inside one plateau (which
  torn-robe piece is held, whether the key/sole/statuette has been obtained)
  tracked by item possession, matching quest-helper's own `ConditionalStep`
  grouping. Three more native varbit pairs are read, not authored:
  `%elid_whitegolem`/`%elid_greygolem`/`%elid_blackgolem` (golem-dead flags)
  and `%elid_thievingchannel`/`%elid_miningchannel`/`%elid_rangingchannel`
  (channel-cleared flags) -- quest-helper's own `VarbitRequirement`s read
  these exact varbits, and setting them natively re-skins the already-placed
  `elid_waterchannel_*_multiloc`/`elid_ranging_target_multinpc` swaps with no
  extra rendering logic. Golem weakness (white=stab, grey=slash,
  black=crush) needed no script-side gate -- `configs/all.npc`'s own
  crush/slash/stabdefence params (1 vs 300) already make the combat engine
  enforce it. All named npcs (`elid_mayor` "Awusah", `elid_ghaslor`,
  `elid_shiratti`, `elid_waterspirit`/`_sitting`/`_male` "Nirrie"/"Tirrie"/
  "Hallak", `elid_genie`) and two ground items (`elid_key` on
  `elid_wooden_table`, `elid_shoes` by Awusah's doorway) are already base
  world spawns in `areas/world/configs/{m53_45,m52_149,m52_145}.spawn` --
  no hand-spawn needed for any of them, and the ancestral key needs **no
  pickup script at all** since LostCity's generic
  `skill_magic/scripts/spells/telegrab.rs2` already handles any pickupable
  ground obj and `elid_key` carries no `telegrab_disabled` param. Only the
  three golems are hand-spawned (absent from every world spawn file), one
  per door via `npc_add` + `[ai_queue3,...]` death hook, the exact
  `~npc_retaliate(0)`/`npc_findhero`/`~npc_default_death` idiom Depths of
  Despair's Sand Snake and A Porcine of Interest's Sourhog used. Scripts:
  `quest_spiritsoftheelid/scripts/elid_mayor.rs2` (Awusah offer/qualify gate
  on `stat()` not `stat_base()`, reveal-the-crevice conversation, shoes
  hand-off, post-quest), `elid_ghaslor.rs2` (ballad hand-off, `elid_ballad`
  item Read op), `elid_shiratti.rs2` (flavour, not gating -- quest-helper
  never lists a required NpcStep for him), `elid_house.rs2` (cupboard
  open/search/shut via `loc_change` -- LostCity's `general_use/cupboards.rs2`
  explicitly defers "members/quest cupboards" -- plus the needle-and-thread
  mend chain, the `[opheldu,...]`/`last_useitem` idiom Hand in the Sand's
  redberry-juice chain used), `elid_dungeon.rs2` (rope-on-root entrance via
  `[oplocu,desert_water_cave_root]` -- quest-helper's own gameval, not an
  `elid_`-prefixed one -- the ancestral-key robe door gated on
  `inv_total(worn, ...)` for both mended robe pieces, all three golem doors
  + combat + channel-clear locs, the lake door, and the water-spirit gestalt
  talk), `elid_genie.rs2` (the crevice `elid_crevice_clickzone` climbed both
  ways off the **same** loc name -- no dedicated "climb up" gameval exists
  anywhere in this cache's `elid_` loc list, unlike the golem dungeon's own
  dedicated `elid_underground_exit` -- disambiguated via
  `if (loc_coord = ...)`, the `godwars_entrance.rs2`/`chests.rs2`/
  `doorman.rs2` precedent for the same same-name-multiple-placements
  pattern; the genie's two-visit sole-for-statuette trade with the
  "sole"/"soul" pun; knife-on-shoes; statuette-on-plinth completion),
  `elid_journal.rs2` (wired into `interface_questjournal/scripts/
  quest_journal.rs2`'s dispatch, additive line after the
  `quest_handinthesand` case). Wiki
  https://oldschool.runescape.wiki/w/Spirits_of_the_Elid +
  /Quick_guide + Transcript:Spirits_of_the_Elid (the fetch tool returned a
  structured summary rather than verbatim Jagex-copyrighted dialogue, same
  as every quest before it on this queue; dialogue here is original wording
  covering the same beats). Deferred: exact bow/arrow-or-magic-rune matrix
  on the ranging channel (soft-skipped, same tier as other slices' own
  weapon-style minigames -- any wielded weapon triggers the shot once the
  Black Golem is dead), the crevice's "light source" requirement (soft-
  skipped -- no generic light-source-category proc exists anywhere in this
  tree yet, same class of gap as Hand in the Sand's Entrana banking),
  Shiratti's cupboard-search flavour beyond the two required torn-robe
  pieces. `mingw32-make -C src sscompile` clean; `mingw32-make -C src
  mock230-scripts` -- full corpus build, zero diagnostics on any new/touched
  file (confirmed by grepping the full build log case-insensitively for
  `elid`: zero matches anywhere, including the diagnostics section); only
  failure in the whole tree is the pre-existing unrelated
  `ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2` missing
  `%content_restrict_summoning_serverside` (untouched, out of scope, same as
  every prior slice's report) -- this also means `sscompile` writes no
  output at all this run (all-or-nothing across the whole tree), so this was
  verified by log inspection rather than a produced pack, matching the
  bar prior "done" slices on this queue (e.g. #38) already accepted; dbrow
  allocator report shows the same single pre-existing stale row
  (`66540=quest_asoulsbane`, unrelated) and no new stale entries --
  `quest_spiritsoftheelid`'s dbrow is natively declared, not allocator-only.
  Rows #39-50 already carry `2026-08-10` re-audit notes from an earlier
  tick today; not re-checked again this tick for budget reasons -- next
  fresh spot-check candidate if a future tick has spare budget is #72 Olaf's
  Quest (pending, 425 lines) or beyond, since everything through #71 already
  reads `done`. Next = Another Slice of Ham (#P4 / #85, 485 lines,
  npcs=slicezanik,slicezanik,slicehamgu -- not yet re-verified this tick,
  take the same grep-first steps before writing).
- slice #85/P4 done: Another Slice of H.A.M. -- re-verified pending first (no
  LostCity or 2009scape checkout is available on this machine, same gap
  every prior tick on this queue has noted, so the OSRS-Content tree itself
  was cross-checked directly: no `quest_anothersliceofham`/`quest_slice`/
  `quest_ham` directory, and no `slice_`-prefixed npc/loc/varbit bound
  anywhere, before this slice). `tools/questhelper_extract.py
  anothersliceofham --check` resolved every ItemID/NpcID/ObjectID/VarbitID
  gameval clean. Like Making History/Hand in the Sand/Spirits of the Elid,
  this quest has a fully **native cache schema**: dbrow
  `quest_anothersliceofham` (id 133, endstate 11, questpoints 1,
  requirement_stats (5,25)=Prayer 25 / (0,15)=Attack 15 with **no**
  `requirements_boostable` column -- gated `stat_base()`, matching the
  wiki's "both non-boostable" -- stat_xp_awarded (14,30000)=Mining 3000 /
  (5,30000)=Prayer 3000) and a native varbit schema on basevar `slice_base`:
  `%slice_quest` (bits 0-10, 0..10 matching quest-helper's own `steps.put`
  keys exactly, +11 for complete matching dbrow endstate) plus
  `%slice_artifact_1`..`_6` (2 bits each, 0/1/2 = not dug/dug/handed-in,
  matching quest-helper's own dug/handed-in `VarbitRequirement`s exactly),
  `%slice_zanik_at_dig`, `%slice_hiding`, `%slice_added_middle_corridor_guard`,
  `%slice_reached_snipers`, `%slice_received_mace` -- all reused as-is, no
  new varbit authored. dbrow columndef 25 requirement_quests (ids 24/63/29)
  cross-references to `quest_scorpioncatcher`/`quest_shadesofmortton`/
  `quest_clocktower` by their own `id,int` field -- a method that correctly
  cross-checked for `quest_makinghistory` on an earlier slice, but here
  disagrees completely with both the wiki and this quest's own
  `QuestHelper.java getGeneralRequirements()` (Death to the Dorgeshuun /
  The Giant Dwarf / The Dig Site FINISHED, which agree with each other
  exactly) -- flagged as a cache decode/linkage mismatch on this one column
  and not used. Spot-checking those three real prerequisites found The Dig
  Site is **already implemented** under LostCity's own internal codename
  `quest_itexam` (not `thedigsite`) -- a stale-row bug on this queue's own
  #141, corrected above. Death to the Dorgeshuun and The Giant Dwarf remain
  genuinely unported (#108/#110, no `dttd_`/dwarf-city script directory
  anywhere beyond scattered native varbits), so the hard prerequisite gate
  on those two is soft-skipped (narrated only), the same deferral tier this
  queue has used for every other still-pending prerequisite quest.
  Scripts (`quest_anothersliceofham/scripts/`): `slice_urtag.rs2` (Ur-tag +
  Ambassador Alvijar argument opener, `stat_base` gate, quest accept),
  `slice_tegdak.rs2` (trowel/specimen brush hand-out and replacement, all
  six dig hotspots via `~slice_dig`, the specimen table clean via
  `~slice_clean`, sequential hand-in via `~slice_tegdak_handin`, ancient
  mace assembly), `slice_zanik.rs2` (idle-Zanik recruit -> hand-spawned
  `slice_zanik_follower` + `npc_setmode(playerfollow)`, Goblin Scribe
  mace-reading, Oldak teleport sphere to the Goblin Village), `slice_generals.rs2`
  (Wartface/Bentnoze mace identification + H.A.M. ambush cutscene teleport,
  the formal mace hand-off + escort after the tower fight), `slice_hammage.rs2`
  (hand-spawned H.A.M. Mage/Archer tower fight, `ai_queue3` death hooks),
  `slice_sergeants.rs2` (Mossfists/Slimetoes swamp-surface briefing, an
  additive `[oploc1,goblin_cave_entrance]` override that falls through to
  the same `~climb(-1)` the generic `climb_down` category default already
  ran -- no line of `ladders_stairs/scripts/ladders.rs2` touched, matching
  that file's own documented "name rung beats category rung" precedence --
  the cave-side sergeants, and the crate-based guard-avoidance sequence),
  `slice_sigmund.rs2` (final ladder, the shielded `slice_sigmund_showdown`
  -> mace-triggered swap to vulnerable `slice_sigmund_noprayer`, untie
  Zanik, quest completion), `slice_journal.rs2` (wired into
  `interface_questjournal/scripts/quest_journal.rs2`'s dispatch, additive
  line after the `quest_spiritsoftheelid` case). Configs:
  `quest_anothersliceofham.constant` (progress/requirement/reward/coord
  constants, full provenance in its header) and
  `quest_anothersliceofham.varp` (three plain standalone varps --
  `slice_ham_mage_dead`/`slice_ham_archer_dead`/`slice_sigmund_defeated` --
  for tracking this port authors that have no native cache equivalent,
  the same convention `quest_losttribe/configs/losttribe.varp` used for its
  own primary counter, since no quest directory in this tree yet authors a
  fresh bit-packed basevar of its own). npc corrections over quest-helper's
  own bare spellings (the near-match trap PORTING_GUIDE.md section 4.2
  warns about): `slice_sergeant_mossfists`/`slimetoes` (unspawned multi-npc
  shell ids) bound instead as the real spawned `_swamp`/`_cave` variant ids;
  `lotg_oldak_cutscene` (not the plain `dorgesh_oldak`/`dorgesh_oldak_there`
  overworld forms) is cache-authoritative for this pre-Giant-Dwarf encounter
  and carries no world spawn, hand-spawned via the same idempotent
  `[login,_]` npc_find/npc_add idiom Devious Minds' Sir Tiffy and Roving
  Elves' Islwyn used. Wiki Another_Slice_of_H.A.M./Quick_guide +
  Transcript:Another_Slice_of_H.A.M. (the fetch tool returned a structured
  summary rather than verbatim Jagex-copyrighted dialogue, same as every
  quest before it on this queue; the dialogue authored in these scripts is
  original wording covering the same beats). Deferred: the basement ->
  tunnel -> mines -> Dorgesh-Kaan travel route (quest-helper's own
  `goToCityF0`/`goToCityF1` housekeeping, not `steps.put` breakpoints
  themselves -- those names belong to Death to the Dorgeshuun's own
  unported tunnel-access mechanic, not to a single side-quest's namespace),
  the exact per-tick guard patrol/detection puzzle (soft-skipped to three
  sequential crate interactions), the exact "attack only after a prayer is
  raised, then use the mace's special attack to strip it" Sigmund timing
  (soft-skipped to a single narrated beat swapping the shielded npc form for
  the vulnerable one -- no generic "detect a used special attack" primitive
  exists in this engine for content to hook), full cross-zone escort AI for
  Zanik (a state flag plus a spawned `playerfollow` companion, not real
  per-zone re-fetching if left behind), and the "stay behind the houses" /
  ranged-or-magic-only enforcement on the H.A.M. Mage/Archer fight (plain
  combat). `mingw32-make -C src sscompile` clean; `mingw32-make -C src
  mock230-scripts` -- full corpus build, zero diagnostics on any new/touched
  file (confirmed by grepping the full build log case-insensitively for
  `anothersliceofham`/`dorgesh_urtaq`/`tegdak`/`slice_sigmund`/`slice_zanik`/
  `slice_artifact`/`goblin_cave_entrance`/`general_wartface`/
  `general_bentnoze`/`sergeant_mossfists`/`sergeant_slimetoes`/
  `lotg_oldak`: only hit is the expected new-varp-id allocator log line for
  the three authored varps), only failure in the whole tree is the
  pre-existing unrelated `ported_scape2009_summoning/scripts/
  summoning_spirit_wolf.rs2` missing `%content_restrict_summoning_serverside`
  (untouched, out of scope); dbrow allocator report shows the same single
  pre-existing stale row (`66540=quest_asoulsbane`, unrelated) and no new
  stale entries -- `quest_anothersliceofham`'s dbrow is natively declared,
  not allocator-only. Spare-budget spot-check: #72 Olaf's Quest -- no
  `quest_olaf*` directory exists anywhere in the tree, only a native
  `quest_olafs` dbrow with no implementing script, so unlike most of the
  #51-84 sweep range this row is genuinely pending, not stale; left
  unchanged. Rows #73-84 in that range remain unswept. Next = Clock Tower
  (#87, 486 lines, npcs=brotherkojo,brotherkojo -- not yet re-verified this
  tick, take the same grep-first steps before writing); #86 Pandemonium is
  already `done`.
- tick 2026-08-10c: three stale-row corrections, then one real port.
  **#87 Clock Tower** -- already implemented under LostCity's own internal
  codename `quest_cog` (not `clocktower`): `server/scripts/quests/quest_cog/
  scripts/{quest_cog,brother_kojo,cogs,cog_journal,
  quest_cog_gates_and_levers,quest_cog_spindles,quest_cog_food_trough}.rs2`,
  538 lines, full cellar-cogs + gates/levers + spindles + food-trough +
  Brother Kojo dialogue tree + completion queue; dbrow `quest_clocktower`
  id 29 endstate 8, journal wired at `quest_journal.rs2:519`. Row flipped
  to `done (LC)`. **#91 Defender of Varrock** -- found fully scripted
  (`quest_defenderofvarrock/scripts/{dov_elias,dov_rovin,dov_invasion,
  dov_camdozaal,dov_journal}.rs2`, 775 lines incl. config) by an untracked
  earlier tick, never logged on this queue before now; `%dov` 0..56, dbrow
  id 188 endstate 56, journal wired at `:903`, `~quest_complete` present.
  Row flipped to `done`. Discovered its own scripts (and the pre-existing,
  also-uncommitted `quest_crest/scripts/crest_dimintheis.rs2`) reference
  `^chat_worried`, which `interface_chat/configs/chat.constant` never
  declared -- cache.osrs239 has no dedicated `chatworried*` seq either --
  a real compile-blocking bug left by that earlier tick, not sibling
  content this tick chose to touch; fixed by adding
  `^chat_worried = chatsad1` (nearest existing expression, the same
  substitution convention that file already used for `^chat_shifty`).
  **#94 Pirate's Treasure** -- pre-Sept-2004 quest, wrongly filed on this
  queue instead of the IN-LC table; LC's own codename is `quest_hunt`
  (`quest_hunt/scripts/{redbeard_frank,luthas,dig,banana_crate,
  food_store,pirate_message,hunt_journal}.rs2`, 403 lines), dbrow
  `quest_piratestreasure` id 16, journal wired at `:447`. Row flipped to
  `done (LC)`. **#99 Throne of Miscellania** (546 lines, real port) --
  found only a partial skeleton pre-existing (`quest_misc/scripts/
  {misc_door_guard,misc_giant_nib,misc_journal}.rs2` + `quest_misc.constant`
  + `quest_misc.varp`, no NPC dialogue and no completion path), so this
  tick wrote the missing half: `misc_king_vargas.rs2` (quest offer +
  courting-partner choice + all `%misc_quest` 0->10->...->90 diplomacy
  advances + treaty/pen handoffs + `~quest_complete`), `misc_queen_sigrid.rs2`
  (Etceteria recognition demand, anthem condition relay, treaty hand-off),
  `misc_princess_astrid.rs2` / `misc_prince_brand.rs2` (courting: 3-part
  talk -> gift -> talk -> gift -> talk -> ring, over the native
  `misc_s1_d1..d3`/`misc_s2_d1..d3`/`misc_s3_d1..d3`/`misc_s1_give`/
  `misc_s2_give`/`misc_s1_emote`/`misc_s3_emote` varbits and `%misc_affection`
  0->40, ending in `%misc_acceptedtorule`; Brand's file also carries the
  one-off bard/anthem duty at `%misc_quest`=40 regardless of courting
  choice), `misc_advisor_ghrim.rs2` (awful->good anthem correction; 75%
  support finish gate), `misc_smithy.rs2` (Derrik: iron bar -> giant nib).
  All six NPC names (`misc_king_vargas`/`misc_queen_sigrid`/
  `misc_princess_astrid`/`misc_prince_brand`/`misc_advisor_ghrim`/
  `misc_smithy`) and every item/varbit/constant referenced resolve directly
  against `configs/all.npc`/`all.obj`/`all.varbit` and the pre-existing
  `quest_misc.constant` + `managing_miscellania.constant` -- no new varp/
  varbit authored, matching Quest Helper's own `VarbitID.MISC_*` names
  exactly. Wiki cross-check
  (https://oldschool.runescape.wiki/w/Throne_of_Miscellania/Quick_guide +
  Transcript:Throne_of_Miscellania) confirmed courting Brand vs Astrid is
  player-selected, not gender-locked (Quest Helper's own `courtingBrand`
  toggle agrees) -- implemented via a `~p_choice3` at Vargas. Real
  prerequisites are Heroes' Quest + The Fremennik Trials (both unported,
  #114/#159); the dbrow's own `requirement_quests` column (ids 72/57)
  decodes to Roving Elves / Nature Spirit instead -- the same cache
  decode/linkage mismatch flagged on Another Slice of H.A.M.'s row (#85),
  not used -- both real prereqs soft-skipped (narrated only). Deferred/
  simplified: the three-repeated-dialogue-per-stage courting ladder is
  condensed to one combined exchange per stage (content preserved, the
  re-click requirement is not); the dance/clap/blow-kiss emotes are
  narrated rather than requiring a live emote-completion primitive (none
  exists in this engine, same tier as Below Ice Mountain's flex emote);
  the 75%-support finish gate's underlying Managing Miscellania
  resource-collection loop (rake farming patches / mine coal / cut maples /
  fish) has no writer anywhere in this tree yet (that file's own header
  already says "Kingdom collect/resources deferred") so this port keeps
  Quest Helper's own item-gate (rake/pickaxe/axe/harpoon/lobster pot) as a
  real check but resolves the loop itself in one narrated interaction,
  setting `%misc_approval` straight to the 75% threshold (same soft-skip
  tier as Bone Voyage's sailing / Sleeping Giants' supply matrix); Quest
  Helper's `getAnotherAwfulAnthem` recovery branch (a second copy if the
  first is lost) not ported. `mingw32-make -C src sscompile` clean;
  `mingw32-make -C src mock230-scripts` -- grepping the full build log
  case-insensitively for `misc_king_vargas`/`misc_queen_sigrid`/
  `misc_princess_astrid`/`misc_prince_brand`/`misc_advisor_ghrim`/
  `misc_smithy`/`throneofmiscellania`/`quest_misc`/`chat_worried`: zero
  hits (no diagnostics touch any of them); also re-checked
  `quest_defenderofvarrock`/`quest_hunt`/`quest_cog`/`captain_rovin`/
  `crest_dimintheis`/`dov_*`: zero hits. Only failure in the whole tree is
  the pre-existing unrelated `ported_scape2009_summoning/scripts/
  summoning_spirit_wolf.rs2` missing `%content_restrict_summoning_serverside`
  (untouched, out of scope, same as every prior slice on this queue). dbrow
  allocator report shows the same single pre-existing stale row
  (`66540=quest_asoulsbane`) and no new stale entries. Next = The Feud
  (#100, 550 lines, npcs=feudalim,feudalim,shantay -- not yet re-verified,
  take the same grep-first steps before writing).
- slice 100 done: The Feud -- Apr 2005, Ali Morrisane's nephew caught
  between the Menaphite thugs and the bandits of Pollnivneach; helper's own
  npc spellings (`feudalim`, `shantay`) don't resolve in this cache at all
  (no such gamevals exist) -- grep-first (LostCity `content/scripts` +
  2009scape both absent on this machine, same gap every prior tick on this
  queue has logged; OSRS-Content tree itself cross-checked directly, no
  `quest_thefeud`/`feud`-shaped directory existed before this slice) found
  the quest is instead **fully native**: dbrow `quest_feud` (id 77, startnpc
  3533 = `feud_ali_m` "Ali Morrisane", endstate 28, `requirement_stats`
  (17,30) Thieving 30 **not boostable** -- no `requirement_quests` column at
  all, so the row's own decode-mismatch risk the queue warns about doesn't
  apply here) + native varbit schema (`%feud_var` on basevar `main_feud_var`,
  0..63, top breakpoint 28 confirmed independently from two directions: every
  `feud_*_multi` pre/postquest npc swap table and the shared `myarm_dung`
  loc both key `multivarbit=feud_var` slot 29 as the one transition) plus a
  dozen native sub-bitfields (`feud_var_drink`, `feud_var_talk_gangs`,
  `feud_var_comp_gangs`, `feud_ali_money`, `feud_distracted`, `feud_hag_list`,
  `feud_found_trait`, `feud_used_sauce`, `feud_given_jewels`,
  `feud_var_menaboss`/`feud_var_banditboss`, `feud_boss_vis`/`feud_boss_vis2`/
  `feud_bandit_boss_vis`, `feud_mayor_multivar`) reused as-is, same "cache
  states nearly everything" situation Spirits of the Elid / Another Slice of
  H.A.M. / Throne of Miscellania hit. Scripts: `feud_alimorrisane.rs2` (offer
  gated on `stat_base(thieving)>=30`, return/complete), `feud_recruitment.rs2`
  (Drunken Ali's 3 beers, questioning both gangs, buying 2 camels + receipts,
  Ali the Operator recruitment + 3 pickpocket tasks via street-urchin
  distraction + oak blackjack, real thieving-gated pickpocket check),
  `feud_heist.rs2` (disguise-gated door, 2 notes, safe->jewels),
  `feud_traitor.rs2` (Ali the Barman -> Kebab seller sauce -> trough dung ->
  Snake Charmer -> Ali the Hag poison -> poisoned beer), `feud_confrontation.rs2`
  (Menaphite Leader -> real hand-spawned "Tough Guy" fight via `npc_add`/
  `ai_queue3`/`npc_findhero`, same convention as Elid's golems; Bandit Leader
  -> real "Bandit champion" fight; Ali the Mayor reveal), `feud_journal.rs2`;
  wired into `interface_questjournal/scripts/quest_journal.rs2`. Important
  wrapper-npc finding: `areas/world/configs/m52_46.spawn` places several
  named quest npcs behind cosmetic-variety **wrapper** ids
  (`feud_egyptian_doorman_multi`, `feud_arabian_guard_multi`/`_2`,
  `feud_villager_multi_1/2/3`) and the three Leader/Mayor npcs as their own
  bare wrapper ids (`feud_mayor`, `feud_menap_boss`, `feud_bandit_boss`) --
  their resolved sub-npcs (`feud_egyptian_doorman_1`, `feud_villager_1_1`,
  `feud_mayor_geom`, etc.) carry the declared ops (`op1=Talk-to` etc.) but
  this server never reads `multivarbit`/`multinpc` at runtime (only
  `cachepack`'s client-side encoder does, confirmed by grepping all of
  `src/net/mock` -- multinpc resolution is a pure client rendering swap
  here), so triggers had to bind to the **wrapper** gameval names, which is
  what the live server entity's type actually is; every other named npc
  (`feud_drunken_ali`, `feud_hag`, `feud_egyptian_minder` "Ali the Operator",
  etc.) is spawned as its own bare pre-quest concrete npc, bypassing its own
  `_multi` wrapper entirely (the map-viewer snapshot generator apparently
  resolved those client-side before recording), so those bind directly.
  Rewards: 1 QP, 15000 Thieving XP (tenths, dbrow `stat_xp_awarded` 17/150000
  matches wiki exactly), 500 coins, desert disguise; oak blackjack granted
  mid-quest by Ali the Operator (doubles as the wiki's own reward). Wiki
  https://oldschool.runescape.wiki/w/The_Feud +
  .../The_Feud/Quick_guide + .../Transcript:The_Feud (structured summary, no
  verbatim Jagex dialogue reproduced, same convention every prior slice used).
  Deferred (named, not silent): the live rev-230 combination-lock interface
  (interface 330 `the_feud_safe.if`, clientscript 261 -- reverse-engineering
  its digit-button stack behaviour was out of scope; the safe opens
  narratively once both notes are held, same tier as Misthalin Mystery's
  candle/piano/switch puzzles); the snake-charming minigame (`feud_desert_snake`
  carries only `op2=Attack` in this cache, no charm op or snake-charm/basket
  item exists in `configs/all.obj` -- narrated via Ali the Snake Charmer
  instead); the glove-exclusion list (Barrows/ice/vambrace/Slayer barred per
  wiki, not enforced); the "hide behind cactus" stealth beat; the cowardly
  bandit side npc; cosmetic lookalike-villager randomisation
  (`feud_npc_multi`); flavor-only mid-confrontation villager chatter (op1
  Talk-to left to the engine's generic default chat, non-gating).
  **Also fixed two genuine pre-existing compile-blocking bugs hit while
  verifying** (same license as the prior tick's `chat_worried` fix): (1)
  `src/makefile`'s `mock230-scripts`/`mock230-scripts-summoning` targets
  passed `--pack .../ported/scape2009_summoning/pack` but never
  `.../configs`, so the summoning lane's own `content_restrict_summoning_serverside`
  varbit (declared in `ported/scape2009_summoning/configs/summoning.varbit`)
  was never visible to the compiler -- added the missing `--pack` line to
  both targets, mirroring the main tree's existing `pack`+`configs` pair.
  This is more consequential than it looks: `SSC_CompileDir` sorts all
  `.rs2` paths and stops at the **first** hard error (`ssc_compile.c:2932-2935`),
  and `ported_scape2009_summoning` sorts alphabetically before `quests`
  (`p` < `q`) -- so with the bug present, `mock230-scripts` was silently
  never reaching **any** file under `server/scripts/quests/` (or anything
  else `>= "q"`) at all, meaning the "grep the log for my own files" bar
  every recent slice used (including this one's own first attempt) was a
  false negative for any quest whose directory sorts `>= "q"` alphabetically.
  (2) With that fixed, compilation progressed further and hit a second,
  independent pre-existing bug: `quest_rovingelves/scripts/rovingelves_islwyn.rs2:48`
  referenced `^chat_surprised`, which does not exist (`chat.constant` only
  declares `^chat_shock`) -- fixed to `^chat_shock`. With both fixed,
  `mingw32-make -C src mock230-scripts` now **exits 0** (13354 scripts
  compiled, no failure at all, stronger than the "one known unrelated
  failure" bar); grepping the full log case-insensitively for `feud` is 0
  hits. `mingw32-make -C src sscompile` still clean/no-op. Next = Cold War
  (#105, 574 lines, npcs=penglarryz,penglarryz,penglarryi) -- #101-104 are
  already `done`.
- **re-verification tick (2026-08-11):** confirmed the prior tick's makefile
  fix in person before touching anything else -- `src/makefile:1733-1734`
  carries the `--pack $(SUMMONING_CLIENT_LANE)/pack` +
  `--pack $(SUMMONING_CLIENT_LANE)/configs` pair on the `mock230-scripts`
  target, and a clean `mingw32-make -C src mock230-scripts` genuinely exits 0
  (13354 scripts compiled, 0 case-insensitive `error` hits in the full log,
  166 lines total). Spot-checked all six named earlier "done" slices --
  Roving Elves (#17), Devious Minds (#21), Making History (#37), The Hand in
  the Sand (#38), Spirits of the Elid (#51), Another Slice of H.A.M. (#85) --
  by grepping that real full-build log case-insensitively for each slice's
  own filenames/npc-name fragments (`rovingelves`/`roving_`, `deviousminds`/
  `devious`, `makinghistory`, `handinthesand`, `spiritsoftheelid`/`_elid`,
  `anothersliceofham`/`slice_ham`/`_ham/`): **zero hits for any of the six**,
  meaning zero notes/warnings/errors were emitted for their files -- combined
  with the log's own 0-error total and "compiled 13354 scripts" success line,
  none of the six were silently masked by the now-fixed alphabetical-sort
  bug. No newly-discovered real compile errors; nothing to fix.
- slice done: Cold War (#105) -- Jan 2007, Larry/KGP penguin-spy infiltration
  quest. Grep-verified first (methodology steps 1/2): no `coldwar`/`penglarry*`
  script or config anywhere in this tree before this slice; no LostCity/
  2009scape checkout reachable on this machine (same gap prior ticks noted),
  cross-checked directly against the OSRS-Content tree itself instead.
  `tools/questhelper_extract.py coldwar --qh-root <real quest-helper
  checkout> --check` resolved all 47 ItemID/NpcID/ObjectID/VarbitID gamevals
  clean, exit 0 (the tool's hardcoded default `--qh-root` points at a macOS
  path that doesn't exist on this box; passed the real Windows checkout path
  explicitly, and worked around an unrelated `UnicodeEncodeError` on Windows'
  cp1252 stdout by setting `PYTHONIOENCODING=utf-8`, not fixed in the tool
  itself -- future ticks on Windows will hit the same wrapper issue).
  Native dbrow `quest_coldwar` (id 126, startnpc 827 == `peng_larry_zoo`
  cross-checked against `all.npc.compack`, endstate 135, questpoints 1,
  requirement_stats (22,34)=Construction/(12,30)=Crafting/(16,30)=Agility/
  (17,15)=Thieving/(21,10)=Hunter -- matches quest-helper's own
  `getGeneralRequirements()` exactly) and native varbit schema (basevar
  `peng_var`/`peng_var2`) reused as-is: `%peng_quest` (bits 0-7, primary
  progress, authored 0/5/10.../130->135-complete matching quest-helper's own
  `steps.put` cadence), `%peng_transmog`/`%peng_doing_greeting`/
  `%peng_multi_hide`/`%peng_multi_kgp` matching quest-helper's own
  `VarbitRequirement`s by name **and exact semantics** (`isPenguin`,
  `isEmoting`, `birdHideBuilt`, `guardMoved>=2`) -- the strongest native
  varbit/quest-helper name match found on this queue to date -- plus
  `%peng_emote_1..3` (the bird-hide 3-emote code) and `%peng_pong_chat`.
  Every npc quest-helper names already has a real world `.spawn` entry in
  this tree (`peng_larry_zoo`/`peng_zoo` in `m40_51.spawn`, `peng_larry_ice`
  in `m41_62.spawn`, `peng_larry_rell` in `m42_58.spawn`,
  `sheep_shearer_the_thing`/`fred_the_farmer` in `m49_51.spawn`,
  `peng_kgp`/`peng_noodle_multi`/`peng_ping`/`peng_pong`/
  `peng_icelord_warrior01..04` in `m41_162.spawn`, `peng_agility_instructor`
  in `m41_63.spawn`) -- the first slice on this queue needing **zero**
  hand-spawned npcs. `peng_noodle` is a native multinpc child of the
  `peng_noodle_multi` shell, gated on `%peng_multi_kgp` (hidden at 0, shown
  at 1, hidden again at 2) -- this port sets that same bit to 1 on first
  entering the KGP outpost (making Noodle appear) and to 2 once the
  control-room guard is lured away by Ping/Pong's bongo music (matching
  quest-helper's own `guardMoved >= 2` gate, and Noodle sensibly vanishing
  from the corridor once the base is on alert -- an emergent story beat from
  trusting the native bit rather than inventing a fresh one).
  Scripts: `quest_coldwar/scripts/{coldwar_shared,coldwar_larry,
  coldwar_birdhide,coldwar_zoo,coldwar_lumbridge,coldwar_clockwork,
  coldwar_outpost,coldwar_journal,coldwar_debug}.rs2` +
  `configs/coldwar.constant` (1211 lines total) covering the full critical
  path: bird-hide build (plank frame + spade cover) and 3-emote greeting
  puzzle (native code stored in `%peng_emote_1..3`, replayed via `p_choice4`
  at the zoo penguin / Lumbridge sheep-penguin), clockwork mechanism + suit
  crafting at a POH table 3/4, the zoo/Lumbridge disguise-and-passphrase
  loop, Fred the Farmer + cowbell theft, the KGP outpost (Noodle's ID-card
  exchange, the crush-course soft-skipped to a single beat + real
  talk-to-instructor completion, Ping/Pong's bongo-drum lure), the
  control-room/war-room reveal (`Pescaling Pax`/`Operation Freedom`,
  anti-magic disguise strip), and the icelord-pen escape via chasm. XP
  rewards (`stat_advance`, tenths) match the dbrow's own `stat_xp_awarded`
  exactly: Agility 5000, Crafting 2000, Construction 1500 (the dbrow carries
  no Attack-40 row that quest-helper's own `ExperienceReward` list has --
  cache wins per methodology step 3, so that lamp-sized bonus is not
  awarded). Wiki: `Cold_War` + `Cold_War/Quick_guide` (paraphrased summaries
  only, same convention every prior slice used -- dialogue authored is
  original wording covering the same beats: Larry's paranoid zookeeper
  premise, the clockwork-penguin infiltration, Ping/Pong's bongo distraction,
  Pescaling Pax's "Operation Freedom" reveal). Journal wired
  (`interface_questjournal/scripts/quest_journal.rs2`, `if ($row =
  quest_coldwar) { ~coldwar_journal; return; }`). Fixed one authoring bug hit
  during this slice's own first compile attempt (not pre-existing, introduced
  and caught within the same tick): the RuneScript lexer does not support
  backslash-escaped double quotes inside string literals (`mes("Larry: \"That's
  crazy!\"")` failed with `expected ')' after arguments to 'mes'` at
  `coldwar_debug.rs2:41`) -- no other file in this tree uses `\"` inside a
  `.rs2` string either, confirming it's unsupported rather than a typo; fixed
  by switching the handful of embedded quotes to single quotes. `mingw32-make
  -C src mock230-scripts` exits 0 afterward: 13410 scripts compiled (13354 ->
  13410, +56 from this slice's trigger blocks), 0 errors, 0
  warnings/notes naming any `coldwar`/`peng_`-prefixed file. Deferred (named,
  soft-skip tier matching this queue's convention -- e.g. Below Ice
  Mountain's rock-paper-scissors, Spirits of the Elid's golem weapon-matrix):
  the crush-course's exact per-obstacle tile pathing; the icelord fight's
  real combat (any interaction narrates the kill, same tier Below Ice
  Mountain's Ancient Guardian boss used); the precise anti-magic-reveal
  cutscene staging; full interactive `TORIRS_SIM_CLICK_AT` client headless
  verification (the `::cwrun` debugproc itself needs no interactive choices --
  it mutates state directly like every prior slice's `*run` command -- but
  driving it through an actual built win64 client + simulated clicks was not
  run this tick, budget spent on the quest's own scope plus the
  re-verification pass above; `mock230-scripts` compiling clean is the
  verification bar this tick's instructions asked for). Next pending = Mourning's End Part I
  (#106, 575 lines) -- not yet re-verified against the fixed pack pipeline.
- slice done: Mourning's End Part I (#106) -- two prior attempts on this exact
  slice both stalled early (right after finding the native dbrow, no file
  writes); this is a clean retry. Grep-first: no LostCity checkout on this
  box (macOS path in PORTING_GUIDE doesn't exist here; the local mirror at
  `C:\Users\mrobe\Documents\git_repos\2004scape` was grepped instead, no
  `mourning` hit); 2009scape not implemented either -- ownership confirmed
  against the OSRS-Content tree directly. Native dbrow
  `quest_mourningsendpart1`: id 87, startnpc 1116, endstate 9, questpoints 2,
  `requirement_stats` (4,60)=Ranged 60 / (17,50)=Thieving 50 -- **wiki-verified
  exactly** (https://oldschool.runescape.wiki/w/Mourning%27s_End_Part_I: "60
  Ranged (not boostable), 50 Thieving (not boostable)") and matching
  quest-helper's own `getGeneralRequirements()`. `stat_xp_awarded`
  (17,400000)=Thieving 4000 XP / (3,250000)=Hitpoints 2500 XP, also matching
  quest-helper's `ExperienceReward` list exactly. **`requirement_quests` on
  this dbrow is wrong**, exactly the failure mode this queue's methodology
  warns about: it lists dbrow ids 122/8/130, which resolve to
  `quest_eaglespeak`/`quest_vampyreslayer`/`quest_greatbrainrobbery` -- none
  matching quest-helper's real prereqs (`QuestRequirement(ROVING_ELVES,
  FINISHED)`, `BIG_CHOMPY_BIRD_HUNTING`, `SHEEP_HERDER`), and Great Brain
  Robbery is actually a **Part II** prereq in real OSRS, not Part I's --
  further confirming the field is misattributed. Independently
  wiki-confirmed via the quest's own "Quest Requirements" section. Gated
  instead on `%rovingelves_quest`/`%chompybird`/`%sheepherderquest`
  completion. Native varbit schema: `%mourning_quest` (plain varp, no bit
  children, bare 0..9 int) lines up with the dbrow's own `endstate=9` and
  quest-helper's own `steps.put` key range (0..8) exactly, authored
  0/2/3/4/5/6/7/8->9-complete (steps 0/1 are quest-helper's identical
  `talkToIslwyn` NpcStep, collapsed to one transition). `%mourning_quest_bits`
  (native, 32 bits, fully packed) supplies eleven more native sub-state
  fields matching quest-helper's own `VarbitRequirement`s by name and
  near-exact threshold semantics -- on par with Cold War's own high-water
  mark: `mourning_gnome` (bits 8-11) == the caged-gnome torture progression
  (`knowWeaknesses`>=3, `torturedGnome`>=5, `talkedWithItem`>=6,
  `releasedGnome`>=7, `repairedDevice`>=9, exact thresholds);
  `mourning_sheep_red/green/yellow/blue` (bits 12-15) == `redDyed` etc
  exactly; `mourning_gun_ammo` (bits 16-18) == `redToadLoaded`(1)/
  `greenToadLoaded`(2)/`blueToadLoaded`(3)/`yellowToadLoaded`(4) exactly;
  `mourning_elena` (bits 19-21) == `givenRottenApple`>=2/`receivedSieve`>=4
  exactly; `mourning_food_poison1/2` (bits 22-23) == `poisoned1`/`poisoned2`
  (quest-helper's own `twoPoisoned` accepts any 2-of-3; `mourning_food_poison3`
  has no quest-helper `ObjectStep` at all, left untouched); `mourning_dye_chat`
  (bit 30) == `learntAboutToads` exactly; `mourning_tegid_chat`/`_silk_1`/`_2`/
  `_fur`/`_trousers_chat`/`_trousers_fixed`/`_mourner_disguise` (bits 1-7)
  track the disguise-assembly beats as flavour/state flags.
  `mourning_can_see_eluned`/`_elena_plot_update`/`_eluned_chant`/
  `_mourner_vis`/`_druid_chat` are native but correspond to no quest-helper
  `Requirement`, left untouched. `mourning_quest_part2`/`mourning_quest_main`
  (a separate basevar) plus the Light Temple mirror/crystal-beam sub-bits are
  unambiguously **Part II** content (the Prifddinas puzzle) and were not
  touched, per this tick's own instructions. NPCs: **zero hand-spawning**
  needed -- `mourning_arianwyn`, `mourning_seamstress` (Oronwen),
  `mourner_hideout_head_mourner` (Essyllt), `mourner_hideout_gnome`,
  `mourning_overpass_mourner`, `elena2`, `herder_plaguesheep_1..4` are all
  already world-spawned (base, non-`_vis` forms -- the cache places the base
  id and the `_vis` swap ids quest-helper names aren't in any `.spawn` file;
  cache wins). `roving_islwyn_2ops` is hand-spawned by Roving Elves' own
  `[login,_]` hook already; since Islwyn is also this quest's start NPC
  (`steps.put(0/1)`), this slice extends Roving Elves'
  `[opnpc1,roving_islwyn_2ops]` trigger in
  `quest_rovingelves/scripts/rovingelves_islwyn.rs2` with a
  post-Roving-Elves-complete branch calling a new `~mend1_islwyn_start` proc,
  rather than defining a second, conflicting trigger. Discovered mid-slice:
  `mourner_hideout_gnome`'s own npc entry is a native `multivarbit` swap keyed
  on this exact `mourning_gnome` value (multinpc8..18 ->
  `mourner_hideout_gnome_head`, quest-helper's own post-release NpcID) -- the
  engine already renders the correct model once the bit crosses 8, so this
  port's own `mourning_gnome_rack` loc-based interaction (covering talk,
  tickle, release, give-items, ask-about-toads in one place, since this
  engine has no npc-swap-driven dialogue mechanism) also wired
  `[opnpc1,mourner_hideout_gnome_head]` to the same shared label as a
  convenience alias. `mourning_overpass_mourner`'s actual cache placement
  (`m35_52.spawn`, x2299/y3328) is ~86 tiles from quest-helper's own stated
  `WorldPoint(2385, 3326, 0)` -- cache wins, coords for the HQ basement
  teleport (`0_31_72_60_20`) were hand-decoded from the cache's own Essyllt
  placement instead of quest-helper's. Quest-helper's start NPC is Islwyn
  (`steps.put`); a general wiki fetch/search independently named Eluned as
  escorting the player from Isafdar to Lletya -- per methodology step 1
  quest-helper's own machine-readable state step is authoritative for which
  NPC changes state, so Islwyn was used and Eluned's escort (no
  varbit-changing `Requirement` in quest-helper's own model) was deferred as
  flavour-only, noted rather than silently dropped. Scripts:
  `quest_mourningsendparti/scripts/{mend1_shared,mend1_disguise,mend1_gnome,
  mend1_sheep,mend1_poison,mend1_journal,mend1_debug}.rs2` +
  `configs/mend1.constant` covering the full critical path: Islwyn/Arianwyn
  briefing, mourner kill (narrated, matching Cold War's icelord tier) +
  loot + soap-clean + Oronwen trouser repair, HQ infiltration + Essyllt's
  assignment, the full caged-gnome torture chain, dye-bellows + toad-load +
  fire-at-sheep (all four colours), Elena's rotten-apple/sieve hand-off, the
  barrel/press/naphtha-still (soft-skipped)/sieve/range toxin chain, both
  West Ardougne food-store poisonings, and the Essyllt/Arianwyn finish +
  `~quest_complete(quest_mourningsendpart1)`. Wiki:
  https://oldschool.runescape.wiki/w/Mourning%27s_End_Part_I +
  .../Quick_guide + Transcript:Mourning%27s_End_Part_I (structured summaries
  only, same convention every prior slice used; dialogue authored is
  original wording covering the same beats). `::mend1` / `::mend1run` debug
  hooks added, mirroring `::cwrun`'s idiom. Hit and fixed the same two
  known compile issues this queue has hit before: mixed `&`/`|` in a single
  `if` without explicit parens (this engine has no operator-precedence
  fallback -- fixed with parens in `mend1_journal.rs2`), and the RuneScript
  lexer's lack of `\"` escape support inside string literals (fixed by
  switching the handful of embedded quotes in `mend1_gnome.rs2` to single
  quotes, same fix as Cold War's own debug file hit). Also hit a genuinely
  new one: writing a packed varp whole (`%mourning_quest_bits = 0`) in the
  debug reset is rejected by this engine's own whole-write-destroys-varbits
  check -- fixed by resetting each individual native varbit by name instead
  (`mend1_debug.rs2`). **Also hit and fixed a correctness bug worth flagging
  for future ticks**: this engine's sscompile silently accepts *duplicate*
  trigger headers (`[opnpc1,X]`/`[oploc1,X]`/`[opheldu,X]` declared more than
  once for the same subject) with zero diagnostic -- confirmed empirically,
  not documented anywhere in PORTING_GUIDE. A first draft of this slice's
  scripts freely declared `[opnpc1,elena2]`, `[opnpc1,herder_plaguesheep_1..4]`,
  `[oploc1,mournerstewdoor]`, and `[opheldu,reddye/yellowdye/greendye/bluedye]`
  as fresh triggers, each of which turned out to already be live: `elena2`
  belongs to Biohazard (`areas/area_ardougne_east/scripts/elena.rs2`),
  `herder_plaguesheep_1..4` to Sheep Herder
  (`quest_sheepherder/scripts/diseased_sheep.rs2`), `mournerstewdoor` to
  Biohazard's own mourner-stew subplot
  (`areas/area_ardougne_west/scripts/doors.rs2`), and the four dyes to the
  base dye/cape-recolouring mechanic
  (`skill_crafting/scripts/dye_cape.rs2`). `mock230-scripts` compiled clean
  either way with no warning -- the duplication would have silently broken
  one implementation or the other for every player of any of those four
  systems, undetectable short of manually auditing every gameval this
  slice's port touched against the *whole* tree, not just
  `server/scripts/quests`. Caught by a full post-hoc grep of all `[opnpc*`/
  `[oploc*`/`[opheld*` headers this slice introduced against the entire
  `server/scripts` tree (not just the quests subtree), then fixed by
  converting each of the seven colliding triggers into a proc/branch called
  from the *front* of the existing file's trigger (same merge-not-duplicate
  pattern already established for Islwyn) -- see the added notes in
  `elena.rs2`, `diseased_sheep.rs2`, `doors.rs2`, and `dye_cape.rs2`.
  **Future ticks: grep every new opnpc/oploc/opheld header against the full
  `server/scripts` tree, not just the quest being ported, before assuming a
  cache-placed npc/loc/item has no existing trigger.** `mingw32-make -C src
  mock230-scripts` exits 0 afterward (post-fix): 13472 scripts compiled
  (13410 -> 13472; net lower than the pre-fix 13482 since seven duplicate
  top-level triggers were merged away rather than left standalone), 0
  errors, 0 warnings/notes naming any `mourning`/`mend1`-touched file (own or
  merged-into). Deferred (soft-skip
  tier, matching this queue's convention): the mourner kill is narrated
  rather than fought, its loot granted directly; the toad-catching/bellows
  loop (quest-helper's own `getToads` step names no ObjectID at all --
  untracked by any real varbit) collapsed to a single dye-on-bellows
  interaction per colour; the Rimmington fractionalising-still tar/heat
  minigame soft-skipped to one narrated interaction (naphtha-barrel path
  only, coal-tar-barrel alternate not implemented); "pick up rotten
  apple"/"pick up empty barrel" (quest-helper `DetailedQuestStep`s with no
  `ObjectID`) granted automatically at the adjacent gated beat; full
  interactive `TORIRS_SIM_CLICK_AT` client headless verification not run
  this tick (same budget note as Cold War's own slice -- `mock230-scripts`
  compiling clean is the verification bar these instructions asked for).
  Mourning's End Part II is explicitly out of scope for this slice (separate
  queue row) and was not touched. Next pending = Wanted! (#107, 580 lines).
- slice done: Wanted! (#107) -- fresh, not previously re-verified. Grep-first:
  no LostCity checkout on this box (macOS path in PORTING_GUIDE doesn't exist
  here); grepped the OSRS-Content tree directly for `wanted` (only incidental
  string hits, e.g. dialogue containing the word "wanted") and for a
  `quest_wanted`-named dir (none) -- ownership confirmed. 2009scape not
  implemented either. Native dbrow `quest_wanted`: id 92, startnpc 4687
  (Sir Tiffy Cashien), endstate 11, questpoints 1, requirement_questpoints 32
  -- **wiki/quest-helper-verified exactly**
  (quest-helper's own `getGeneralRequirements()`:
  `QuestPointRequirement(32)`). `stat_xp_awarded` (18,50000) = Slayer 5000 XP,
  matching quest-helper's `ExperienceReward(SLAYER, 5000)` exactly (confirmed
  the dbtable's fixed-point scale independently by reading
  `src/net/mock/mock230_scripts.c`'s `SS_OP_STAT_ADVANCE` case: "the
  reference's xp argument is already in tenths", so 50000 raw = 5000 display
  XP -- cross-checked against Cold War's own already-`done` dbrow, whose
  stat_xp_awarded (16,50000)/(12,20000)/(22,15000) match that same slice's
  logged Agility 5000/Crafting 2000/Construction 1500 exactly). **This dbrow's
  `requirement_quests` is wrong**, same failure mode this queue's methodology
  warns about: it lists dbrow ids 118/87/111/43, which resolve to
  `quest_slugmenace`/`quest_mourningsendpart1`/`quest_swansong`/
  `quest_undergroundpass` -- none matching quest-helper's real prereqs
  (`QuestRequirement(ENTER_THE_ABYSS, FINISHED)`, `RECRUITMENT_DRIVE`,
  `THE_LOST_TRIBE`, `PRIEST_IN_PERIL`). Gated instead on
  `%abyssal_miniquest`/`%rd_main`/`%lost_tribe_quest`/`%priestperil` reaching
  each quest's own native "complete" constant (`^eta_complete`/`^rd_complete`/
  `^lt_complete`/`^priestperil_complete`, all already declared by those
  quests' own `done` slices and confirmed tree-global by grep, e.g. Devious
  Minds already references `^eta_complete` from a file it doesn't own).
  Native varbit schema: `wanted_main` (11 bits on basevar `quest_wanted`)
  authored 0..10 to match quest-helper's own `steps.put` key range exactly
  (states 0/1/2 quest-helper lists as identical are collapsed to one
  transition, same precedent Mourning's End Part I set), then 11 on
  completion to match the dbrow's own endstate; `wanted_joke_option`,
  `wanted_commorb_intel`, `wanted_daquarius_hint` (0/1/2, matching quest-helper's
  own `VarbitRequirement` values exactly), `wanted_lord_d_exposition`,
  `wanted_zammy_mage_hint` all reused as-is. `wanted_mission1..19`/
  `wanted_missionNcomplete` (38 native bits total) is the "Hunt for Solus"
  schema; quest-helper's own Java only maps all 19 by name, but the wiki's
  own Quick_guide (fetched this tick) states the real quest walks a **fixed
  7-stop** route by name -- Rellekka(15) -> Musa Point(5) -> Wizards'
  Tower(12) -> Dorgesh-Kaan(3, Flames of Zamorak damage) -> Ardougne
  Market(8) -> Champions' Guild(2, decoy Black Knight) -> Rune Essence
  mine(4, real fight) -- not ascending numeric mission order, confirming the
  missionN index is a fixed per-location identity, not a visit-order counter.
  Only those 7 (+ mission1 for the initial Canifis/Savant contact) are ever
  set; the other 11 native slots are unused leftovers of the same schema,
  left untouched (same tier Mourning's End Part I left several native bits
  with no quest-helper `Requirement`). Implemented as real (not narrated)
  zone-gated progression: each Commorb "Scan" (native `ifop1=Scan` on
  `wanted_crystal_ball`, matching quest-helper's own mechanic exactly) checks
  the player's actual coordinates (`coordx(coord)`/`coordz(coord)`, the same
  builtins `quest_recruitmentdrive/scripts/recruitmentdrive_spishyus.rs2`
  already used for a coordinate-split check) against each stop's real-world
  bounding box (quest-helper's own `Zone` bounds, except Musa Point's, which
  is a bug in quest-helper itself -- `new Zone(new WorldPoint(2913, 1366, 0),
  new WorldPoint(2919, 3158, 0))` is an 1800-tile-tall zone that cannot be
  what the real game uses -- a small box around quest-helper's own
  `goToMusaPoint` target coordinate was used instead). NPCs: `rd_teleporter_guy`
  (Sir Tiffy Cashien), `sir_amik_varze`, `lord_daquarius`, `black_knight` are
  all already world-spawned (Taverley Dungeon's Black Knights' Base already
  has multiple `black_knight` spawns); `rcu_zammy_mage1_edgeb`/
  `wanted_solus_attackable` needed no hand-spawn/were hand-spawned as
  documented below. **Duplicate-trigger check (mandatory per this tick's own
  instructions): grepped every `[opnpc*`/`[oploc*`/`[opheld*` header this
  slice's NPCs/item would need against the *whole* `server/scripts` tree
  before writing anything.** Four collisions found and merged rather than
  duplicated, all with a comment at the splice point naming the new owner:
  `[opnpc1,rd_teleporter_guy]` (already Recruitment Drive's Sir Tiffy trigger,
  `quest_recruitmentdrive/scripts/recruitmentdrive.rs2` -- that file's own
  header comment already said "Deferred: ... Wanted! arms", anticipating this
  exact splice), `[opnpc1,sir_amik_varze]` (`areas/falador/scripts/
  sir_amik_varze.rs2`, its post-quest label), `[opnpc1,rcu_zammy_mage1_edgeb]`
  + `[opnpc1,rcu_zammy_mage1b]` (`quest_templeoftheeye/scripts/
  templeoftheeye.rs2`, which itself already shares the npc with Enter the
  Abyss's own `~eta_varrock_mage_talk` -- that file's header comment also
  already said "Deferred: full refuse/Wanted! dialogue trees"), and
  `[ai_queue3,black_knight]`/`[ai_queue3,aggressive_black_knight]`
  (`drop_tables/scripts/black_knight.rs2` -- the required "kill a Black
  Knight to prove yourself to Daquarius" beat is spliced into the existing
  death/loot handler rather than given its own competing death hook).
  `lord_daquarius`, `wanted_crystal_ball` (opheld1/opheld2), and
  `wanted_solus_attackable` (opnpc2 + ai_queue3) had zero existing triggers
  anywhere in the tree and were declared fresh. Solus Dellagar is hand-spawned
  (`npc_add`) at the Rune Essence mine once the chase's final scan lands
  there, and fought with real combat (`~npc_retaliate(0)` / `[ai_queue3,...]`
  + `npc_findhero` + `~npc_default_death`, the same pattern
  `quest_anothersliceofham/scripts/slice_sigmund.rs2` and
  `quest_arthur/scripts/sir_mordred.rs2` already established) -- not
  narrated. The required Black Knight kill near Daquarius reuses the
  already-world-spawned generic `black_knight` (any kill counts once the
  quest state calls for it; quest-helper's own guide only names "near
  Daquarius" as flavour text, not a zone-restricted `Requirement`). The decoy
  Black Knight at the Champions' Guild scan stop and the Flames of Zamorak
  damage at the Dorgesh-Kaan stop are narrated only (no combat/damage
  applied), same tier Cold War's icelords / Mourning's End's mourner kill.
  quest-helper's own wilderness Mage of Zamorak branch
  (`talkToMageOfZamorakInWilderness`, npc `rcu_zammy_mage1a`) is dead weight
  for any player who actually meets this quest's requirements -- Enter the
  Abyss finished is a hard prerequisite, so the branch that skips straight to
  the Varrock mage is always the one taken -- and was not modelled;
  `entertheabyss.rs2`'s own trigger for that npc was left untouched entirely
  (zero edits to that file this slice). Scripts:
  `quest_wanted/scripts/{wanted_shared,wanted_tiffy_amik,wanted_daquarius,
  wanted_mage,wanted_commorb,wanted_hunt,wanted_journal,wanted_debug}.rs2` +
  `configs/wanted.constant`, covering every `steps.put` value 0..10 end to
  end: Sir Tiffy's opening pitch (with the "Ask about the Wanted! Quest"
  dialog option quoted verbatim from the wiki transcript) / Sir Amik's Squire
  offer (including the joke "accept Squire status anyway" branch, which sets
  `wanted_joke_option` and still reaches the same next state, matching
  quest-helper's own model where the branch changes no end-state) / Commorb
  purchase (GP or law rune + enchanted gem + molten glass, both paths real
  item removal with insufficient-funds/components checks) / Savant contact /
  Daquarius investigation + Black Knight kill / Mage of Zamorak essence
  hand-off (20 rune or pure essence) / the 7-stop hunt / Solus fight / hat
  pickup / Sir Amik hand-in + `~quest_complete(quest_wanted)`. Wiki:
  https://oldschool.runescape.wiki/w/Wanted!/Quick_guide (fetched this tick
  for the full walkthrough and the fixed hunt order) +
  Transcript:Wanted! (fetched this tick for verbatim dialogue lines: Sir
  Tiffy's opening pitch, Sir Amik's Squire offer and second-visit acceptance,
  the Commorb purchase choice, Savant's "Current Assignment" brief, Daquarius
  before/after the Black Knight kill, the Mage of Zamorak's essence
  ultimatum, Sir Amik's completion line). `::wanted` / `::wantedrun` debug
  hooks added, mirroring `::mend1`/`::mend1run`'s idiom (the reset proc also
  clears the Commorb/hat and despawns any lingering hand-spawned Solus).
  Journal wired (`interface_questjournal/scripts/quest_journal.rs2`, `if
  ($row = quest_wanted) { ~wanted_journal; return; }`). Checked no `&`/`|`
  mixing without parens and no `\"` inside string literals (this queue's two
  known recurring compile pitfalls) before compiling -- neither occurred this
  slice. `mingw32-make -C src sscompile` then `mingw32-make -C src
  mock230-scripts` both exit 0: 13488 scripts compiled (13472 -> 13488, +16
  from this slice's own trigger blocks), 0 errors, 0 warnings/notes naming
  any `wanted`-prefixed file, dialogue file, or the two shared files this
  slice spliced into. `mock230_pack --check-only` also run: 963 pre-existing
  baseline errors (category-membership / cache-path issues, all predating
  this slice and none naming `wanted`) with zero new ones introduced --
  `mock230-scripts` compiling clean is this tick's real verification bar per
  its own instructions. **Also closed one of the two gates this tick's
  instructions asked about**: `quest_deviousminds/scripts/
  deviousminds_monk.rs2`'s own `deviousminds_qualifies` proc had a header
  comment explicitly deferring its Wanted! prerequisite ("still `pending` on
  this same QUESTHELPER_CONTENT_PORT_QUEUE and has no `%wanted_quest` varp in
  this cache yet to check against") -- now that `wanted_main` exists, added
  `if (%wanted_main < ^wanted_complete) { return(^false); }` to that proc
  (matching its existing three sibling checks) and updated the stale header
  comment; re-ran `mock230-scripts` afterward, still exit 0, 0 new
  warnings/errors naming `devious`. The instructions also named Mourning's
  End Part I as a quest that had soft-skipped/narrated around a Wanted! gate
  -- grepped `quest_mourningsendparti/` for `wanted` and found only an
  incidental dialogue string ("Islwyn said you **wanted** to speak to me");
  Mourning's End Part I's own real prerequisites (Roving Elves / Big Chompy
  Bird Hunting / Sheep Herder, per its own slice's log) never named Wanted!
  at all, so there was no second gate to close there -- correcting that part
  of this tick's own briefing rather than inventing a fix that doesn't exist.
  Deferred (soft-skip tier, matching this queue's convention): the exact
  chase geography *inside* each of the 7 hunt zones (any tile within the box
  completes that stop); the decoy Black Knight's own combat and the Flames of
  Zamorak damage (both narrated only); quest-helper's wilderness Mage of
  Zamorak branch (dead weight given the hard ETA prerequisite, see above);
  Solus's fight being a real single-target world spawn rather than
  quest-helper's stated "instanced fight"; full interactive
  `TORIRS_SIM_CLICK_AT` client headless verification not run this tick (same
  budget note as every prior slice on this queue -- `mock230-scripts`
  compiling clean is the verification bar these instructions asked for).
  Next pending = Death to the Dorgeshuun (#108, 587 lines).
- slice 108 done: Death to the Dorgeshuun -- Zanik, Sigmund, the H.A.M. mill.
  Grep-verified first (methodology steps 1-2): no LC proc, no 2009scape impl;
  genuinely pending. Fetched
  `github.com/Zoinkwiz/quest-helper` raw source for
  `helpers/quests/deathtothedorgeshuun/DeathToTheDorgeshuun.java` (local
  quest-helper checkout path in this doc's own header is a Mac path absent on
  this Windows box) to get the real 13-step state machine
  (`steps.put(0..12)`), all `VarbitID`/`NpcID`/`ObjectID` gameval names, and
  `getGeneralRequirements()`. Native dbrow `quest_deathtothedorgeshuun` (id
  113, endstate 13, requirement_stats agility 23 + thieving 23,
  stat_xp_awarded thieving 2000 + ranged 2000) confirmed by cross-check
  against quest-helper's own `SkillRequirement`/`ExperienceReward` calls.
  dbrow `requirement_quests` resolves to id 87 = `quest_mourningsendpart1` --
  **wrong**, per this queue's own warning about that column; the real
  prerequisite is The Lost Tribe FINISHED (quest-helper's
  `QuestRequirement(THE_LOST_TRIBE, FINISHED)`), gated instead on LC's own
  `%lost_tribe_quest = ^lt_complete`. Native varbit schema on basevar
  `dttd_base`/`dttd_temp` (`dttd_main` 0..13, `dttd_tour_duke/priest/
  goblins/citizens/sun/shop`, `dttd_zanik_in_cellar`, `dttd_tour_ham_deacon`/
  `dttd_tour_ham_johanhus`, `dttd_ham_trapdoor_state`, `dttd_zanik_corpse`,
  `dttd_collecting_tears`, `dttd_guard_1..5_warned/dead`,
  `dttd_mill_guards_dead`) reused as-is, matching quest-helper's own
  `VarbitID` names exactly. Cache multilocs `dttd_mill_trapdoor` /
  `dttd_tunnel_millside` are already driven off `dttd_main` directly, and
  `lost_tribe_mistag`/`lost_tribe_guide`'s native `multinpc` table caps their
  `_2ops` (Follow) variant at `%lost_tribe_quest` = 11 (`lt_complete`) --
  the `_3ops` variant (Cellar/Watermill fast-travel, matching quest-helper's
  UnlockReward "Access to Dorgesh-Kaan") only unlocks at 13, so
  `~dttd_quest_complete` sets `%lost_tribe_quest = 13` on finish, a real
  functional payoff verified against the cache's own multinpc table, not
  just flavour text. Spliced into three pre-existing shared triggers instead
  of duplicating them (critical correctness rule): `losttribe_finish.rs2`'s
  `[opnpc1,lost_tribe_mistag_2ops]` (Mistag's favour-quest offer gated on
  `%dttd_main = 0`), `tearsofguthix.rs2`'s `[oploc1,tog_juna]` (Zanik's
  revival scene gated on `%dttd_main = ^dttd_zanik_saved`), and reused
  `losttribe_ham.rs2`'s already-generic `[oploc1,osf_ham_ladder]` exit and
  `ladders_stairs/scripts/climb_shared.rs2`'s already-generic
  `~climb(-1)` handlers on both `osf_trapdoor_open` (H.A.M. lair entrance,
  already climbable once Lost Tribe sets `%ham_thief` = 1) and
  `dttd_ham_trapdoor_open` (the stage trapdoor, once this slice's own
  `[oploc1,dttd_ham_trapdoor_closed]` picklock action opens it) with zero
  new code. New files: `quest_deathtothedorgeshuun/configs/
  deathtothedorgeshuun.constant` (stage constants + 20 packed coords
  computed from quest-helper's own `WorldPoint`s) + `scripts/{dttd_shared,
  dttd_start,dttd_haminfiltrate,dttd_savezanik,dttd_mill,dttd_journal,
  dttd_debug}.rs2`. Covers all 13 quest-helper steps: Mistag's favour ->
  recruit Zanik (native multinpc wrapper `dttd_zanik_cellar` hand-spawned,
  shows `dttd_zanik_marked` only while `dttd_zanik_in_cellar` = 1) -> soft
  Lumbridge tour (Duke/citizens/priest/goblins/shop folded into one
  conversation, matching this queue's established collapse-the-sightseeing
  convention) + origin story -> Johanhus + hidden stage trapdoor -> 5-guard
  storeroom puzzle (`dttd_ham_guard_1..5` hand-spawned, real ordered
  Talk-to sequence matching their declared `op1=Talk-to`-only ops, no
  Attack op in the cache -- confirms the puzzle's own "distract, don't
  fight" framing rather than a soft-combat simplification) -> door capture
  twist (Zanik dragged off, native `dttd_zanik_corpse` bit reveals
  `dttd_zanik_dead_body`) -> mine + swamp caves -> Juna revival (real
  `[proc,pickaxe_checker]`/tinderbox checks, no consumption) -> zanik's
  story -> crate infiltration -> real combat (`opnpc2`/`ai_queue3`
  `~npc_retaliate`/`~npc_default_death` idiom, matching Defender of
  Varrock's Chaos Golem) against 3x hand-spawned `dttd_ham_guard_mill` then
  `dttd_sigmund_melee` -> smash `dttd_drilling_machine` -> exit via
  `dttd_cave_entrance_millside_blocked` -> `~dttd_quest_complete` (1 QP,
  2000 thieving + 2000 ranged XP tenths, matches dbrow `stat_xp_awarded`
  exactly). Journal wired
  (`interface_questjournal/scripts/quest_journal.rs2`, `if ($row =
  quest_deathtothedorgeshuun) { ~dttd_journal; return; }`). `::
  deathtothedorgeshuun` / `::dttdrun` debug hooks added, same idiom as
  `::wanted`/`::wantedrun`. Wiki:
  https://oldschool.runescape.wiki/w/Death_to_the_Dorgeshuun/Quick_guide +
  Transcript:Death_to_the_Dorgeshuun (consulted for the door-capture twist
  and Zanik's "chosen commander" foreshadowing, which quest-helper's own
  step map only implies). Checked no `&`/`|` mixing without parens and no
  `\"` inside string literals before compiling. `mingw32-make -C src
  sscompile` then `mingw32-make -C src mock230-scripts` both exit 0: 13520
  scripts compiled, 0 errors, 0 warnings/notes naming any `dttd`-prefixed
  file or the three shared files this slice spliced into; grep-verified
  every new/spliced trigger name (`[opnpc1,...]`/`[oploc1,...]`/
  `[opnpc2,...]`/`[ai_queue3,...]`/`[proc,...]`/`[debugproc,...]`) resolves
  to exactly one definition across the whole `server/scripts` tree (no
  silent duplicates). Deferred (soft-skip tier, matching this queue's
  convention): the exact stealth zone/behind-guard mechanic (Talk-to
  advances state directly rather than requiring the player to path behind
  each guard first); Zanik's disguise NPC swap
  (`dttd_zanik_follower`/`dttd_zanik_follower_ham`) and any live
  follower-pathing AI (she's represented purely as a state flag +
  fixed-point dialogue, never a moving spawned pet); the tears-of-Guthix
  gather minigame itself (narrated only, no items granted, matching
  tearsofguthix.rs2's own already-deferred full tears IF); damage-type
  restrictions on Sigmund (melee/magic only per quest-helper, not enforced
  here); full interactive `TORIRS_SIM_CLICK_AT` client headless verification
  not run this tick (same budget note as every prior slice -- `mock230-
  scripts` compiling clean is the verification bar these instructions
  asked for). Next pending = My Arm's Big Adventure (#109, 589 lines).
- slice 109 done: My Arm's Big Adventure -- Burntmeat, My Arm the troll who
  wants to learn farming, Captain Barnaby, Murcaily, a Baby Roc then a Giant
  Roc. Grep-verified first (methodology steps 1-2): no LC proc, no 2009scape
  impl; genuinely pending. Fetched `github.com/Zoinkwiz/quest-helper` raw
  source for `helpers/quests/myarmsbigadventure/MyArmsBigAdventure.java` for
  the real step map (`steps.put(0..310)` -> complete 320), every
  `VarbitID`/`NpcID`/`ItemID` gameval name, and `getGeneralRequirements()`.
  **Bonus fix while researching #109's own prerequisite**: this queue's own
  row #113 (`eadgarsruse`) was a stale duplicate of the IN-LC table's
  `eadgarsruse -> quest_eadgar` entry (LC already fully implements Eadgar's
  Ruse, journal wired) -- corrected to `done (LC)`, no soft-skip gating
  needed for #109's Eadgar's Ruse prerequisite, gated for real on
  `%eadgar_quest >= ^eadgar_complete`. Native dbrow
  `quest_myarmsbigadventure` (id 120, endstate 320, questpoints 1,
  requirement_stats farming 29 boostable + woodcutting 10 not boostable,
  stat_xp_awarded herblore 100000 + farming 50000, matching quest-helper's
  `SkillRequirement`/`ExperienceReward` calls exactly) confirmed by
  cross-check. dbrow `requirement_quests` resolves to ids 36/50/80 = Plague
  City / Gertrude's Cat / Icthlarin's Little Helper -- **wrong**, per this
  queue's own warning about that column; the real prerequisites (Eadgar's
  Ruse, The Feud, Jungle Potion, all FINISHED, plus >=60% Tai Bwo Wannai
  Cleanup favour) are quest-helper's own `getGeneralRequirements()`, and all
  three prerequisite quests are already real implementations in this tree
  (`%eadgar_quest`/`^eadgar_complete`, `%feud_var`/`^feud_complete`,
  `%junglepotion`/`^junglepotion_complete`) -- no soft-skip gating needed
  anywhere in this slice. Native varbit schema on basevar `myarm_quest`
  reused as-is, matching quest-helper's own VarbitID names exactly (`myarm`
  main progress 0-1023, `myarm_dung` 0-3, `myarm_supercompost` 0-7,
  `myarm_tubers` 0/1, `myarm_fakepatch` 0-15 with the exact same
  usedRake>=6/givenCompost=7/givenDibber>=9 thresholds quest-helper's own
  `VarbitRequirement`s use, `myarm_barnabyswap` 0/1). Discovered the native
  cache already runs a full multinpc positional-swap system for My Arm
  himself: the world-spawned wrappers `myarm_multi_kitchen`/`_ardougne`/
  `_brimhaven`/`_village`/`_larry`/`_teacher` each show `myarm_fixed` only
  across specific `myarm` bitfield ranges, so **My Arm needed zero
  hand-spawning anywhere** in this quest (same "every npc already
  world-spawned" precedent as coldwar #105 / mourningsendparti #106) --
  only the two Roc bosses are hand-spawned, lazily, once the patch is fully
  planted (Death to the Dorgeshuun / Spirits of the Elid idiom). **Critical
  correctness rule applied**: `eadgar_troll_chief_cook` (Burntmeat) already
  had two competing pre-existing `[opnpc1,eadgar_troll_chief_cook]`
  definitions before this slice (`quest_eadgar/scripts/
  eadgar_troll_chief_cook.rs2` and `quest_makingfriendswithmyarm/scripts/
  makingfriendswithmyarm.rs2`) -- a latent duplicate-trigger bug predating
  this slice that silently broke one of the two (sscompile gives no
  diagnostic for this). Burntmeat is shared by three quests in prerequisite
  order (Eadgar's Ruse -> My Arm's Big Adventure -> Making Friends with My
  Arm), so this slice consolidated all three into the single surviving
  definition in `quest_eadgar/scripts/eadgar_troll_chief_cook.rs2`,
  dispatching by quest state before falling through to Eadgar's Ruse's own
  unchanged logic, and removed `makingfriendswithmyarm.rs2`'s duplicate
  (converted to a plain `mf_burntmeat_talk` proc the dispatcher calls).
  Likewise `myarm_fixed`'s existing `[opnpc1,myarm_fixed]` (owned by Making
  Friends with My Arm, whose own quest requires this one finished) was
  spliced -- `if (%myarm < ^myarm_complete) { ~maba_myarm_talk; return; }`
  prepended -- not duplicated; that file's own `[debugproc,
  makingfriendswithmyarm]` was also updated to set `%myarm =
  ^myarm_complete` so its existing `::mfrun` headless test still starts
  correctly now that `myarm_fixed`'s trigger is gated. New files (all under
  `quest_myarmsbigadventure/`): `configs/myarmsbigadventure.constant`
  (21 stage constants 0..320, fakepatch sub-thresholds, coords) +
  `scripts/{maba_shared,maba_burntmeat,maba_myarm,maba_travel,maba_rocs,
  maba_journal,maba_debug}.rs2`. Covers the full quest-helper step map:
  Burntmeat's offer -> My Arm accepts -> Death Plateau troll cauldron
  (`[oplocu,death_troll_cauldron]`, bucket-on-pot) for the goutweedy lump ->
  roof + farming manual -> fertilise (3 Ugthanki dung + 7 supercompost,
  tracked on the real `myarm_dung`/`myarm_supercompost` sub-fields) ->
  Captain Barnaby at Ardougne docks (`myarm_barnaby`, native
  `myarm_barnabyswap` ship-swap reused) -> Brimhaven -> Tai Bwo Wannai ->
  Murcaily (`tbwcu_murcaily`, gated on the native `%favour_percentage >= 60`
  varbit, matching quest-helper's own `VarbitID.FAVOUR_PERCENTAGE` exactly)
  for the hardy gout tubers -> back to the roof -> give rake/supercompost/
  hardy tubers/seed dibber in quest-helper's own real order -> Baby Roc
  (level 75) then Giant Roc (level 172) hand-spawned and fought via the
  `opnpc2`/`ai_queue3` `~npc_retaliate`/`~npc_default_death` idiom (matching
  Death to the Dorgeshuun's Sigmund) -> give spade, harvest -> tell
  Burntmeat -> tell My Arm -> `~maba_quest_complete` (1 QP, 10000 herblore +
  5000 farming XP, 29 burnt meat, matches dbrow `stat_xp_awarded` exactly).
  Journal wired (`interface_questjournal/scripts/quest_journal.rs2`, `if
  ($row = quest_myarmsbigadventure) { ~maba_journal; return; }`).
  `::myarmsbigadventure` / `::mabarun` debug hooks added, same idiom as
  `::dttdrun`/`::wantedrun`/`::mfrun`. Wiki:
  https://oldschool.runescape.wiki/w/My_Arm%27s_Big_Adventure/Quick_guide +
  Transcript:My_Arm%27s_Big_Adventure (fetched this tick for requirements,
  full walkthrough order and reward text). Checked no `&`/`|` mixing without
  parens and no `\"` inside string literals before compiling; grep-verified
  every new/spliced trigger name (`[opnpc1,...]`/`[opnpc2,...]`/
  `[oplocu,...]`/`[ai_queue3,...]`/`[proc,...]`/`[debugproc,...]`) resolves
  to exactly one definition across the whole `server/scripts` tree before
  and after this slice's edits (no silent duplicates, including the two
  pre-existing ones this slice fixed). `mingw32-make -C src sscompile` then
  `mingw32-make -C src mock230-scripts` both exit 0: 13536 scripts compiled
  (13520 -> 13536, +16 from this slice's own new triggers/procs/debugprocs),
  0 errors, 0 warnings/notes naming `myarm`, `maba_`,
  `eadgar_troll_chief_cook` or `makingfriendswithmyarm`; dbrow allocator
  summary shows `quest_myarmsbigadventure` cleanly resolved (not STALE; the
  build's one STALE dbrow is the pre-existing, unrelated `quest_asoulsbane`
  from row #43). Deferred (soft-skip tier, matching this queue's
  convention): the rake head/handle break-and-repair mini-step (giving a
  rake directly progresses the patch, matching this queue's precedent for
  simplifying minor item mini-mechanics); patch disease chance and the
  Plant Cure item (narrated as unnecessary once enough supercompost is
  given, matching prior slices' deferral of RNG failure chains); the
  Drunken Dwarf's Leg / dwarf-joke easter egg NPC (`myarm_dwarfjoke`,
  cosmetic only, native default keeps it invisible); the Giant Roc's boulder
  shadow/dodge mechanic (`myarm_giant_roc_shadow`, combat simplified to the
  same `opnpc2`/`ai_queue3` idiom as every other hand-spawned boss on this
  queue); Tool Leprechaun Larry's spade-replacement flavour (a generic
  shared Tool Leprechaun shop, not this quest's own content); exact
  Stronghold/roof ladder pathing (pre-existing generic dungeon traversal, not
  quest-gated); full interactive `TORIRS_SIM_CLICK_AT` client headless
  verification not run this tick (same budget note as every prior slice --
  `mock230-scripts` compiling clean is the verification bar these
  instructions asked for). Next pending = The Giant Dwarf (#110, 589 lines).
- slice 110 done: The Giant Dwarf -- Commander Veldaban, Blasidar the
  sculptor's statue of King Alvis, Vermundi/Saro-Dromund/Santiri-Thurgo's
  three items, joining the Blue Opal trade consortium. Grep-verified first
  (methodology steps 1-2): no LC proc, no 2009scape impl; genuinely pending
  (released May 2005, so it's correctly on this post-2009-scope queue only
  because it's a QuestHelper-only quest neither era tree ever shipped, not
  because of its release date -- flagged for anyone auditing row provenance).
  Fetched `github.com/Zoinkwiz/quest-helper` raw source for
  `helpers/quests/thegiantdwarf/TheGiantDwarf.java` (local quest-helper
  checkout path in this doc's own header is a Mac path absent on this Windows
  box) for the real step map (`steps.put(0/5/10/20/30/40)`), every
  `NpcID`/`ObjectID`/`ItemID`/`VarbitID` gameval name, and
  `getGeneralRequirements()`. `tools/questhelper_extract.py` run against the
  fetched file (via a scratch dir, since the tool needs a directory of
  `.java`): every single ItemID/NpcID/ObjectID/VarbitID gameval resolves
  clean against the osrs239 cache (the only "unresolved" line was the tool's
  own dbrow-name-guess heuristic testing `quest_thegiantdwarf`, which isn't
  the real name -- see below). Native dbrow `quest_giantdwarf` (id 84,
  endstate 50, questpoints 2, requirement_stats magic 33 boostable +
  firemaking 16 + crafting 12 + thieving 14 boostable, stat_xp_awarded mining
  2500 + smithing 2500 + crafting 2500 + magic 1500 + thieving 1500 +
  firemaking 1500, matching quest-helper's own SkillRequirement/
  ExperienceReward calls exactly) confirmed by cross-check -- and unlike
  every quest_giantdwarf-preceding row's dbrow warning, this one has **no**
  `requirement_quests` column at all, matching quest-helper's own
  getGeneralRequirements() (no quest prerequisites; Knight's Sword is a
  cross-quest shortcut, not a gate -- see below). Native varbit schema on
  basevar `giantdwarf_main` reused as-is, matching quest-helper's own
  VarbitID names exactly (`giantdwarf_quest` main progress 7 bits,
  `giantdwarf_veldaban_introduced`, `giantdwarf_sculptor_introduced`,
  `giantdwarf_model_state` 4-bit hand-in bitmask matching quest-helper's own
  per-bit VarbitRequirement(MODEL_STATE, true, 0/1/2) probes,
  `giantdwarf_current_company`/`giantdwarf_original_company`,
  `giantdwarf_pie_given`, `giantdwarf_vermundi_givenbook`,
  `giantdwarf_gotpair`; `giantdwarf_cousin_introduced`/`giantdwarf_
  statue_invis`/`giantdwarf_cutscene_guard_visible`/`giantdwarf_
  red_traders_gone`/`giantdwarf_red_axe_gone`/`giantdwarf_
  brothers_toldsuccess`/`giantdwarf_player_had_completely_fixed_axe_at_least_
  once` are cosmetic/flavour-only bits this slice does not wire, deferred).
  giantdwarf_main already carries 31 of a varp's bits and quest-helper's own
  fine per-substep progress is tracked client-side via transient
  ChatMessageRequirement/WidgetTextRequirement, not persisted varbits at all
  -- rather than inventing new tracking vars (no spare `varp_NNN` slot exists
  or was needed), this slice folds all fine-grained progress into
  `%giantdwarf_quest` itself as one ascending sequence (0..28), jumping
  straight to the dbrow's own endstate 50 on completion, matching this
  queue's established "final proc sets the var to the true endstate
  regardless of interim numbering" precedent (myarm, dov, etc). Also
  discovered while trying to bitwise-OR `giantdwarf_model_state`'s three
  hand-in bits together: no precedent anywhere in this tree for `|` inside
  `calc()` -- rather than risk an unverified operator, this slice enforces
  strict clothes -> boots -> axe ordering (quest-helper's own real game state
  is independent/any-order per item, but its own guide/panel already
  sequences them this way) and assigns the cumulative bitmask value directly
  (1, then 3, then 7) at each hand-in instead. **Critical correctness rule
  applied twice**: Thurgo (`areas/port_sarim/scripts/thurgo.rs2`) already had
  a pre-existing `[opnpc1,thurgo]` dispatcher shared by Prying Times, Royal
  Crossbow repair/assembly and Knight's Sword (`%squire` switch) -- spliced
  in as `~gdwarf_thurgo_talk`, not duplicated, gated on `%giantdwarf_quest`
  being in the axe-repair range AND (having reached the Reldo lead OR
  `%squire >= ^squire_given_pie`, i.e. Knight's Sword's own
  `previouslyGivenPieToThurgo` shortcut -- if the player already gave Thurgo
  a redberry pie during Knight's Sword, this quest skips asking for a second
  one and skips the Librarian/Reldo detour entirely, matching the real
  cross-quest shortcut). Reldo (`quest_atailoftwocats/scripts/twocats.rs2`'s
  `[opnpc1,reldo_normal]`) already had a pre-existing dispatcher for A Tail
  of Two Cats -- spliced in as `~gdwarf_reldo_talk`, not duplicated, gated on
  `%giantdwarf_quest = ^gdwarf_imcando_asked`. Grep-verified every other
  new npc/loc trigger (`dwarf_city_boatman_mines_prequest`,
  `dwarf_city_black_guard_leader`, `dwarf_city_shop_sculpture(_model)`,
  `dwarf_city_shop_cloth_poor`, `dwarf_city_librarian`,
  `dwarf_city_shop_armour`, `dwarf_city_excentric_dwarf`,
  `dwarf_city_shop_weapons`, `dwarf_city_secretary_blue_opal`,
  `dwarf_city_director_blue_opal(_cutscene)`,
  `dwarf_keldagrim_bookcase_ladder`, `dwarf_keldagrim_spinning_machine`,
  `dwarf_keldagrim_wide_stairs_lower/upper`) had zero pre-existing
  definitions anywhere in the tree before this slice, so all are fresh, not
  spliced. New files (all under `quest_giantdwarf/`): `configs/
  giantdwarf.constant` (29 stage constants + thresholds + rewards + 16
  packed coords from quest-helper's own WorldPoints) + `configs/
  giantdwarf.varp` (claims the native `giantdwarf_main` carrier with
  protect/transmit/scope, matching bonevoyage's `fossilquest_main`
  precedent -- the cache's own `all.varp` entry is a bare name reservation,
  not a full declaration) + `scripts/{gdwarf_shared,gdwarf_start,
  gdwarf_clothes,gdwarf_boots,gdwarf_axe,gdwarf_consortium,gdwarf_journal,
  gdwarf_debug}.rs2`. Covers quest-helper's full step map end to end:
  Dwarven Boatman (gated on Magic 33/Firemaking 16/Crafting 12/Thieving 14)
  -> Keldagrim -> Commander Veldaban's task -> Blasidar's three requests ->
  Vermundi/Librarian/bookcase-climb/coal+logs+tinderbox spinning machine ->
  exquisite clothes -> Saro -> Dromund (boot-steal/Telekinetic-Grab window
  mechanic narrated via repeated Dromund dialogue rather than a real stealth/
  spell simulation, since the cache has no dedicated window loc or boot
  ground-item props for this quest -- matching this queue's precedent for
  simplifying spatial mechanics with no native prop to hang a real
  interaction off of, e.g. dttd's stealth-zone collapse) -> exquisite pair
  of boots -> Santiri -> sapphires (`opheldu`) -> Librarian/Reldo or Knight's
  Sword shortcut -> Thurgo (pie + iron bar) -> restored battleaxe -> Riki the
  sculptor's model (all three items handed in; Riki's own cosmetic model-swap
  npc variants deferred, no native multinpc dispatch table wires them unlike
  My Arm's troll swap) -> Blasidar's approval -> Blue Opal consortium
  (secretary/director ore-then-bar delivery simplified to one representative
  10-unit exchange each, matching quest-helper's own "recommended" item
  quantities of 10 exactly, rather than the real randomised repeated-task
  minigame to 75/100 points -- only the Blue Opal company path is wired,
  quest-helper's own source leaves company-choice detection an open TODO
  too) -> join -> pledge support -> report to Veldaban ->
  `~gdwarf_quest_complete` (2 QP, 2500 mining/smithing/crafting + 1500
  magic/thieving/firemaking XP, matches dbrow `stat_xp_awarded` exactly).
  Journal wired (`interface_questjournal/scripts/quest_journal.rs2`, `if
  ($row = quest_giantdwarf) { ~gdwarf_journal; return; }`).
  `::giantdwarf` / `::gdwarfrun` debug hooks added, same idiom as every
  prior slice (no `stat_setlevel` proc exists in this tree, confirmed by
  grep before use -- dropped the stat-boost lines from the debug hooks
  entirely, matching dttd/wanted's own precedent of not bothering since the
  headless walk never calls the requirement-check proc). Wiki:
  https://oldschool.runescape.wiki/w/The_Giant_Dwarf/Quick_guide (fetched for
  the full walkthrough + reward text) + Transcript:The_Giant_Dwarf (fetched
  for verbatim dialogue: Boatman's offer, Veldaban's briefing, Blasidar's
  three requests + refusal line, Vermundi's machine sequence, Saro/Dromund's
  boot lines, Santiri/sapphires, Reldo's Imcando lead, Thurgo's pie exchange,
  Riki's hand-in lines, secretary/director task lines, joining-company line).
  Checked no `&`/`|` mixing without parens and no `\"` inside string literals
  before compiling. Also caught and avoided an unverified-operator pitfall
  mid-slice: `|` inside `calc()` has no precedent anywhere in this tree (see
  above) -- redesigned around it rather than risk a silent miscompile.
  **First build attempt accidentally ran from the main repo checkout
  (`c:/Users/mrobe/Documents/git_repos/3d-raster`) instead of this worktree**
  and hit an unrelated pre-existing failure in the main checkout's own
  `ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2` (undeclared
  symbol `summoning_scroll_howl_scroll`, nothing to do with this slice) --
  caught by noticing the build output paths pointed at the main repo, not the
  worktree; re-ran `mingw32-make -C src sscompile` then `mingw32-make -C src
  mock230-scripts` from the correct worktree root and both exit 0: 13562
  scripts compiled (13536 -> 13562, +26 from this slice's own new triggers/
  procs/debugprocs), 0 errors, 0 warnings/notes naming `gdwarf`,
  `giantdwarf`, `thurgo`, `reldo_normal`, `twocats`, or `quest_journal`;
  grep-verified every new/spliced trigger name resolves to exactly one
  definition across the whole `server/scripts` tree both before and after
  this slice's edits (no silent duplicates, including the two pre-existing
  ones this slice spliced into). Deferred (soft-skip tier, matching this
  queue's convention): the real any-order independence of the three item
  side-quests (ported as strict clothes -> boots -> axe, matching
  quest-helper's own suggested guide order); Riki's cosmetic model-swap npc
  variants; the consortium's real randomised repeated-task points minigame
  and the seven non-Blue-Opal company paths; the boot-steal/Telekinetic-Grab
  spatial mechanic (narrated via dialogue); carry-weight gating on the
  bookcase climb; full interactive `TORIRS_SIM_CLICK_AT` client headless
  verification not run this tick (same budget note as every prior slice --
  `mock230-scripts` compiling clean is the verification bar these
  instructions asked for). Next pending = Dragon Slayer (#111, 591 lines).
- slice attempt on #111 Dragon Slayer (2026-08-11): grep-first check (step
  1) found it already fully implemented -- LC's own internal codename
  `quest_dragon` (not `dragonslayer`; 11 files, 1083 lines, dbrow
  `quest_dragonslayer1` id 17 endstate 10 releasedate 23,9,2001, journal
  wired). It's a pre-Sept-2004 quest (Feb 2001) that was misfiled on this
  queue instead of the IN-LC table -- fixed both (row #111 -> `done (LC)`,
  added to IN-LC table). While walking the table for the next genuinely-
  pending row, the same misfiling pattern turned up repeatedly (a cross-
  reference against `server/scripts/quests/lc_quests.txt`, the tree's own
  canonical LC-codename list, flagged several matches instantly): row #112
  Tai Bwo Wannai Trio (`quest_tbwt`, pre-Sept-2004, Mar 2003) -- same fix;
  row #126 Murder Mystery (`quest_murder`, pre-Sept-2004, Dec 2003) --
  same fix, and directly load-bearing for this tick's real slice below;
  row #137 Shadow of the Storm (`quest_shadowstorm`, pre-Sept-2004) --
  same fix; row #143 Underground Pass (`quest_upass`, 31 files, 2602
  lines, pre-Sept-2004) -- same fix; row #79 Nature Spirit
  (`quest_druidspirit`, pre-Sept-2004 Mar 2004, sibling of Druidic
  Ritual's `quest_druid`) -- same fix. Two rows were pure table-sync bugs
  (already correctly listed on the IN-LC table, but a stale duplicate
  `pending` row survived on this Queue table from an earlier rebuild):
  row #114 Heroes' Quest (`quest_hero`) and row #131 Icthlarin's Little
  Helper (`quest_icthlarin`) and row #136 Watchtower (`quest_itwatchtower`)
  and row #158 Legends' Quest (`quest_legends`) -- all four flipped to
  `done (LC)` with a note, no new IN-LC row needed (already present).
  Two more are genuinely mid-era (Sept 2004-Jan 2009, not pre-Sept-2004,
  so NOT added to the IN-LC table, just marked `done` on this Queue with a
  note per this queue's own ownership rule): row #56 Elemental Workshop I
  (`quest_elemental_workshop`, dbrow `quest_elementalworkshop1` only --
  note Elemental Workshop II's own dbrow `quest_elementalworkshop2` exists
  natively but has NO script implementation and NO journal wire, genuinely
  pending, queue row #139) and row #78 Creature of Fenkenstrain
  (`quest_fenkenstrain`, Oct 2006). One row (#54 Shades of Mo'rt'ton) was
  checked and confirmed genuinely still pending despite LOOKING done at a
  glance -- `quest_mortton` exists with a dbrow + journal shell
  (`mortton_journal.rs2`) and one real step (`serum_book.rs2` sets
  `%morttonquest` once), but has no npc dialogue files, no main walkthrough
  script, and `%morttonquest` is never advanced past that single step
  anywhere in the tree -- left `pending`, not touched.
- slice done (King's Ransom, #115, replacing the dead #111 slot): see row
  #115 for the native-schema summary. Full quest-helper step map (0-85)
  ported end to end on `%kr_quest` (native, 0..90): Gossip's introduction
  (spliced into the existing shared `gossipy_man` dispatcher,
  `quest_murder/scripts/gossip.rs2` -- critical correctness rule, not
  duplicated) -> guard hands out the investigation (spliced into
  `quest_murder/scripts/murder_guard.rs2`'s existing `murderguard`
  dispatcher) -> break into Sinclair mansion via `murderwindow` (fresh
  loc), climb `murder_qip_spiralstairs`/`murder_qip_spiralstairstop`
  (fresh locs) collecting scrap paper / address form / a black knight helm
  from `kr_sin_bookcase3a` (fresh loc) -> evidence handed back to the
  guard -> gossip points to Camelot -> Anna Sinclair (fresh npc
  `kr_anna_sinclair`, none of these fresh triggers exist anywhere else in
  the tree, grep-verified) asks for her name cleared at trial -> Seers'
  Village courthouse trial: `kr_courthouse_stairs_top`/`kr_judge`/
  `kr_court_fence_door` (fresh locs) call the dog handler / butler / maid
  witnesses in quest-helper's own fixed order, each testimony spliced into
  the pre-existing `pierre_the_family_dog_handler`/`hobbes_the_butler`/
  `mary_the_maid` dispatchers (`quest_murder/scripts/{pierre,hobbes,
  mary}.rs2`) using the native `kr_court_witness`/`kr_court_dog_proof`/
  `kr_court_butl_proof`/`kr_court_maid_proof`/`kr_court_thread` varbits --
  not guilty verdict -> Anna reveals the statue's secret passage, then the
  `kr_camelot_knight_statue` (fresh loc) ambush reveals Morgan Le Faye's
  plot and captures the player, narrated then teleported to
  `^kr_prison_coord` -> Merlin's prison dialogue spliced into the existing
  shared `[opnpc1,merlin]` trigger (`areas/area_camelot/scripts/
  merlin.rs2`, which already had an unconditional "rushing off" chat +
  `npc_del` for the unrelated Merlin's Crystal crystal-prison cutscene --
  gated the King's Ransom branch strictly on `%kr_quest` state, safe
  without a real zone check since the player is teleported into the
  fortress prison at capture and cannot physically be near the Camelot
  workshop Merlin during that window) -> vent found
  (`kr_underground_jail_cell_wall_bottom_with_vent`, fresh loc) -> cell
  door opened (`kr_underground_jail_bars_gate`, fresh loc) gated on either
  a lockpick or telekinetic-grab runes (law + air), matching quest-helper's
  own OR-requirement -> keep search (`kr_jewelry_box_table`, fresh loc)
  finds a golden-chalice Holy Grail replacement -> Wizard Cromperty's
  Animate rock scroll spliced into the existing shared
  `[opnpc1,cromperty_pre_diary]`/`[opnpc1,cromperty_post_diary]`
  dispatcher (`areas/area_ardougne_east/scripts/wizard_cromperty.rs2`) ->
  Black Knights' Fortress entrance reuses the quest's own pre-existing
  `bkfortressdoor1`/`bksecretdoor` triggers unmodified (their existing
  guard-disguise check already gates on the exact same bronze med helm +
  iron chainbody King's Ransom also requires) -> `kr_bkf_basement_
  laddertop`/`kr_arthur_statue_multi` (fresh locs) free King Arthur from
  Morgan Le Faye's granite curse (animate rock scroll + granite + chalice)
  and hand him the disguise in the same interaction -> final "meet Arthur
  back in Camelot" spliced into the existing shared `[opnpc1,king_arthur]`
  dispatcher (`areas/area_camelot/scripts/king_arthur.rs2`, which already
  had Merlin's Crystal/Holy Grail branches -- King's Ransom checked first,
  highest priority, since it's independent of and later than both) ->
  `~kr_quest_complete` (1 QP, 33000 defence XP, 5000 magic XP, 5000 XP
  lamp, matches dbrow `stat_xp_awarded` and quest-helper's own
  ExperienceReward/ItemReward calls exactly). Simplified (soft-skip tier,
  matching this queue's convention for narrating mechanics with no native
  widget precedent, e.g. Giant Dwarf's Telekinetic Grab boot-steal): the
  real 4-tumbler lock puzzle (`kr_tumb1-4_ans`/`kr_guess_num`/widget 588 --
  native varbits exist but no IF3 puzzle-widget precedent anywhere in this
  tree) is narrated via a single dialogue interaction instead of a real
  puzzle widget; keep-floor climbing (`kr_stairs`) is pure traversal with
  no state consequence, deferred; Knight Waves Training Grounds
  (`kr_wave_instr`/`kr_knightwaves_state`, a separate post-quest minigame)
  is out of scope and deferred. Dialogue is paraphrased from the wiki
  Quick guide (`https://oldschool.runescape.wiki/w/King%27s_Ransom/
  Quick_guide`) and a non-verbatim summary of `Transcript:King%27s_Ransom`
  (the transcript tool declined verbatim reproduction, citing Jagex
  copyright -- summarised beats used instead, not copied text). Caught a
  real parser bug mid-slice: `def_boolean $x = <comparison expression>`
  (e.g. `inv_total(...) > 0 | inv_total(...) > 0`) does not parse as a
  statement (`unexpected '>' at the start of a statement`) even though the
  identical comparisons parse fine *inside* an `if (...)` condition
  (confirmed against existing precedent, e.g. `sanfew.rs2`,
  `professor_oddenstein.rs2`) -- fixed by declaring `def_boolean $x =
  false;` then reassigning `$x = true;` inside `if` blocks, matching the
  only real precedent found for boolean reassignment
  (`godwars_bosses.rs2`'s `$slam`). New files (all under
  `quest_kingsransom/`): `configs/{kingsransom.constant,kingsransom.varp}`
  (claims the native `kr_varp1`/`kr_varp2`/`kr_varp3` carriers with
  protect/transmit/scope -- the cache's own `all.varp` entries are bare
  name reservations, matching this queue's giantdwarf/mourning precedent)
  + `scripts/{kr_shared,kr_mansion,kr_court,kr_prison,kr_fortress,
  kr_journal,kr_debug}.rs2`. `::kingsransom` / `::kingsransomrun` debug
  hooks added, same idiom as every prior slice. Journal wired
  (`interface_questjournal/scripts/quest_journal.rs2`, `if ($row =
  quest_kingsransom) { ~kr_journal; return; }`). Build: ran from this
  worktree (`c:/.../\.claude/worktrees/questhelper-port`, double-checked
  cwd before building per this tick's own warning about a prior mix-up);
  `mingw32-make -C src sscompile` then `mingw32-make -C src
  mock230-scripts` both exit 0: 13587 scripts compiled (13562 -> 13587,
  +25 from this slice's own new/spliced triggers), 0 errors; grep-verified
  every new/spliced trigger name (`murderwindow`, `murder_qip_spiralstairs`,
  `murder_qip_spiralstairstop`, `kr_sin_bookcase3a`,
  `kr_courthouse_stairs_top`, `kr_judge`, `kr_court_fence_door`,
  `kr_camelot_knight_statue`, `kr_underground_jail_cell_wall_bottom_
  with_vent`, `kr_underground_jail_bars_gate`, `kr_jewelry_box_table`,
  `kr_bkf_basement_laddertop`, `kr_arthur_statue_multi`,
  `kr_anna_sinclair`, plus the eleven spliced-into shared npc triggers)
  resolves to exactly one definition across the whole `server/scripts`
  tree, and the build log has zero errors/warnings/notes naming
  `kingsransom` or any `kr_*` symbol. Deferred (soft-skip tier): full
  interactive `TORIRS_SIM_CLICK_AT` client headless verification not run
  this tick (same budget note as every prior slice -- `mock230-scripts`
  compiling clean is the verification bar these instructions asked for);
  the tumbler puzzle widget and Knight Waves Training Grounds noted above.
  **Table bookkeeping note for the next tick:** rows #53 Contact! (355
  lines), #54 Shades of Mo'rt'ton (355, confirmed genuinely a stub this
  tick, see above), #72 Olaf's Quest (425), #73 Grim Tales (427), #76
  Haunted Mine (435) and #80 Mountain Daughter (459) are all SMALLER than
  King's Ransom and still marked `pending` -- they were not grep-audited
  this tick (out of this slice's scope) and may or may not be genuinely
  missing; the strict smallest-pending-first rule would point at #53
  `Contact!` next, not the next row after this slice's replacement chain.
  Recommend the next tick grep-audit #53 first per this queue's own
  depth-first ordering, given how many stale rows this tick found via the
  `lc_quests.txt` cross-reference technique (grep the helper's line-count
  neighbours against `server/scripts/quests/lc_quests.txt`, the tree's own
  canonical LC-codename list, before assuming a row is genuinely unported).
- slice done (Contact!, #53): grep-first check (steps 1-2) found **no** LC
  or 2009scape implementation -- `lc_quests.txt` has no `contact`/`icslittleh`/
  `jex`/`maisa` entry, and no `quest_complete(quest_contact)` call existed
  anywhere in the tree before this slice. Native dbrow `quest_contact`
  (id 124, endstate 130, questpoints 1, stat_xp_awarded 17,70000 = thieving
  7000xp, matches quest-helper's own `ExperienceReward(THIEVING, 7000)`
  exactly) + a full native varbit schema on basevar `contact_master`
  (`contact` 8-bit main progress + `contact_discussed_menaphos`,
  `contact_found_kaleef`, `contact_met_maisa`, `contact_osman_told`,
  `contact_osman_met`, `contact_told_priest`, `contact_met_baker`,
  `contact_people_vis`, `contact_bankers_vis`, `contact_gotscarabs`,
  `contact_maisa_ans`, `contact_been_downstairs`, `contact_osman_vis`,
  `contact_got_mage/lance/bow`, `contact_maisa_invis`,
  `contact_finished_cutscene`, `contact_never_had_keris`,
  `contact_used_reward_lamp`) already existed, matching quest-helper's own
  VarbitID names exactly -- reused as-is. `requirement_quests` resolved to
  dbrow ids 75/112 = Mountain Daughter / Royal Trouble, **neither of which
  is Contact!'s real prerequisite** (critical correctness rule, confirmed
  wrong yet again) -- real prereqs per quest-helper's own
  getGeneralRequirements() + the wiki are Prince Ali Rescue and Icthlarin's
  Little Helper, both already fully implemented in this tree, so both are
  hard-gated (`%princequest >= ^prince_complete`, `%ics_little_var >=
  ^ics_complete`) with no soft-skip needed. All npcs/locs/items resolved
  clean via `tools/questhelper_extract.py contact --check` (0 unresolved) --
  `contact_jex`/`contact_osman_multi`/`contact_osman_desert_multi` and the
  High Priest's `_town` variant are all already world-spawned (spawn configs
  `m51_43.spawn`/`m51_49.spawn`/`m51_44.spawn`), no hand-spawning needed
  except the Giant Scarab boss (spawned lazily on the second dungeon trip,
  My Arm's Big Adventure / Death to the Dorgeshuun idiom). Caught a real
  coordinate trap: quest-helper's own WorldPoint for Maisa/Kaleef's body/the
  scarab fight (~2258-2284, ~4315-4323, plane 0) does **not** match her real
  native spawn (`contact_maisa_multi` at (3218, 9246, 0), region 50_144,
  confirmed via `server/scripts/areas/world/configs/m50_144.spawn`) -- but
  *does* line up almost exactly with quest-helper's own `chasm` Zone
  bounding box (WorldPoint(3216,9217,0)-(3265,9277,0)), confirming this
  cache renders the underground chasm cavern at a duplicated high-Y map
  region (same trick seen elsewhere in this tree, e.g. `quest_dragon`'s own
  `demon_slayer.rs2` ground-item drop at `0_50_154_25_41`) -- cache wins, so
  Kaleef's body, Maisa and the boss fight are all placed near
  (3218, 9246, 0) instead of quest-helper's raw WorldPoints (documented in
  full in `configs/contact.constant`). Quest-helper's own ~50-point
  trap-dodging `linePoints` maze between the two ladders has no established
  maze/trap mechanic anywhere in this tree (soft-skip tier, matching King's
  Ransom's tumbler-lock precedent) -- narrated via `mes()` + a straight
  `p_teleport` instead. Spliced (not duplicated) into one pre-existing
  shared trigger: `[opnpc1,ics_little_hipriest_vis]`
  (`quest_beneathcursedsands/scripts/beneathcursedsands.rs2`, which already
  falls through to Icthlarin's Little Helper's own `ics_hipriest_talk`
  label) -- added a Contact! branch gated on `%ics_little_var >=
  ^ics_complete & %contact < ^contact_complete`, placed after BCS's own two
  checks and before the existing ICS fallthrough, grep-verified this is the
  only definition of that trigger in the tree both before and after the
  edit. All other triggers are fresh (`contact_jex`, `contact_maisa`,
  `contact_osman_multi`, `contact_osman_desert_multi`,
  `contact_osman_cave_instance`, `contact_scarab_boss` (opnpc2 + ai_queue3),
  `contact_temple_trapdoor_open`, `contact_ladder_barricaded`,
  `contact_dead_body_kaleef_vis`, `contact_kaleef_scroll` opheld1),
  grep-verified none pre-existed. New files under `quest_contact/`:
  `configs/{contact.varp,contact.constant}` (claims the native
  `contact_master` carrier with protect/transmit/scope, matching this
  queue's giantdwarf/mourning/kingsransom precedent) +
  `scripts/{contact_shared,contact_priest,contact_jex,contact_dungeon,
  contact_maisa,contact_osman,contact_scarab,contact_journal,
  contact_debug}.rs2`. `::contact` / `::contactrun` debug hooks added, same
  idiom as every prior slice. Journal wired
  (`interface_questjournal/scripts/quest_journal.rs2`, `if ($row =
  quest_contact) { ~contact_journal; return; }`). Dialogue paraphrased from
  the wiki Quick guide (`https://oldschool.runescape.wiki/w/Contact!/
  Quick_guide`) and quick-guide/transcript summaries fetched via WebFetch
  (not verbatim, per copyright). Build: ran from this worktree
  (`.claude/worktrees/questhelper-port`, cwd double-checked before building);
  `mingw32-make -C src sscompile` then `mingw32-make -C src mock230-scripts`
  both exit 0: 13606 scripts compiled (13587 -> 13606, +19 from this
  slice's own new/spliced triggers), 0 errors, 0 warnings/notes naming
  `contact` or any `contact_*` symbol; grep-verified every new/spliced
  trigger name resolves to exactly one definition of its trigger *type*
  across the whole `server/scripts` tree (opnpc2 + ai_queue3 both existing
  once each on `contact_scarab_boss` is correct, not a duplicate). Deferred
  (soft-skip tier, matching this queue's convention): the maze/trap gauntlet
  noted above; the Giant Scarab's real light-extinguish/poison mechanics (no
  dedicated widget precedent, generic combat used instead); the
  `contact_barricade` cosmetic gate object and the `contact_bankers_vis`/
  `contact_finished_cutscene`/`contact_used_reward_lamp`/`contact_got_mage/
  lance/bow` cosmetic native bits (post-quest bank-access polish, no
  gameplay branch found in the guide); full interactive
  `TORIRS_SIM_CLICK_AT` client headless verification not run this tick (same
  budget note as every prior slice -- `mock230-scripts` compiling clean is
  the verification bar these instructions asked for).
- staleness sweep on the remaining small unaudited rows per this tick's own
  recommendation: #72 Olaf's Quest, #73 Grim Tales, #76 Haunted Mine and #80
  Mountain Daughter were all checked against `lc_quests.txt` (no
  `olaf`/`grim`/`mountain` entries at all; the one `haunt` hit,
  `quest_haunted`, is already correctly attributed to Ernest the Chicken,
  row #23 -- confirmed via `~quest_complete(quest_ernestthechicken)` in
  `quest_haunted/scripts/quest_haunted.rs2:316`, not Haunted Mine) and
  against the tree for any existing `quest_olafsquest`/`quest_grimtales`/
  `quest_hauntedmine`/`quest_mountaindaughter` directory or
  `~quest_complete(...)` call (none found). All four remain genuinely
  `pending` -- left untouched, no full port attempted this tick (out of
  budget for a second full slice). Next pending row (smallest-first): #72
  Olaf's Quest, 425 lines.
- slice #72 done: Olaf's Quest -- re-verified genuinely pending (no
  `quest_olaf*` dir, no `lc_quests.txt` hit, only the native dbrow
  `quest_olafs` with no implementing script) before writing. Full native
  varbit schema reused: `%olaf_quest_var` (main progress, basevar
  `olaf_var`, authored breakpoints 0/10/20/30/40/50/60/80 matching
  quest-helper's own `steps.put` keys + dbrow endstate) plus
  `%olaf_ingrid_quest`/`%olaf_volf_quest` (family carvings delivered),
  `%olaf_fire_multi` (campfire multiloc reskin), `%olaf2_gate_disk_1..4`
  (picture-wall puzzle), `%olaf2_walkway_1/2` (rope-bridge barrel repairs),
  `%olaf2_killed_ulfric`/`%olaf2_gate_completed`. Scripts:
  `quest_olafsquest/scripts/{olaf_hradson,olaf_family,olaf_overworld,
  olaf_dungeon,olaf_journal}.rs2` + `configs/quest_olafsquest.constant`;
  hooked `~olaf_try_dig` into the shared `general_use/scripts/spade.rs2`
  chain and `~olaf_journal` into `interface_questjournal/scripts/
  quest_journal.rs2`. Picture-wall puzzle: reverse-derived the exact lever
  mechanic from `PaintingWall.java`'s own hint-branch checkpoints (not
  guessed) -- right lever adds 1 to right+left (mod 5), top adds 1 to
  top+left, left adds 1 to left+bottom, bottom adds 1 to top+bottom; fixed
  real-game start top=2/right=3/bottom=2/left=1 makes right->bottom->top->
  left->confirm the deterministic solve path, but this port simulates the
  full state machine (any order/extra pulls still resolve via confirm's
  own all-4 check), not a scripted replay. Key/lock gate: whichever of the
  5 `olaf2_gate_key_*` a skeleton fremennik drops must match its lock
  button (`_1`=cross/`_2`=square/`_3`=triangle/`_4`=circle/`_5`=star per
  quest-helper's own assignment); wrong guess breaks the key. dbrow
  `requirement_quests` wrong (resolves to Nature Spirit, id 57) -- hard-
  gated on The Fremennik Trials instead (`%viking = ^viking_complete`);
  found in the process that `quest_viking` (mislabeled "Fremennik Exiles"
  in the IN-LC table above) actually implements Fremennik Trials, confirmed
  via `quest_journal.rs2:643` wiring `quest_fremenniktrials` to
  `~viking_journal` -- table left uncorrected this tick (out of scope,
  noted here for a future audit pass). Renamed this quest's own constants
  to `^olafq_*` after `sscompile` caught a real duplicate: `quest_viking`
  already declares `^olaf_not_started`/`^olaf_started`/`^olaf_complete` for
  its own "Olaf" trial-judge NPC. Zero hand-spawning -- every npc
  (`olaf`/`olaf_ingrid`/`olaf_volf`/all nine `olaf2_undead_viking_lvl*`/
  `olaf2_brine_rats`/`olaf2_giant_bat`) and every ground item
  (`rope`/`olaf2_walkway_repair_barrel`/`_inv`/`olaf2_walkway_repair_rope`)
  already world-spawned in `m42_58.spawn`/`m41_57.spawn`/`m42_158.spawn`,
  matching quest-helper's own coords and Zone bounds tile-for-tile (no
  duplicated high-Y region here); only Ulfric himself is hand-spawned
  (absent from every world spawn file, matching quest-helper's own gestalt
  combat step). Wiki: Olaf's_Quest/Quick_guide +
  Transcript:Olaf's_Quest (dialogue summarized only, same Jagex-copyright
  caveat every prior slice on this queue has noted -- dialogue authored is
  original wording for the same beats). Deferred (queue-log note): the
  Agility-scaled barrel-repair fail chance ("guaranteed at level 78" per
  the wiki, no formula recoverable); the visual skull-disk model rotation
  (the four skull models in `interfaces/olaf2_skull_puzzle.if` are raw
  client model archive ids, not a gameval-named pack type -- no verified
  per-rotation sub-model id found, puzzle is fully solvable server-side
  without it); the `olaf2_rusty_gate_puzzle_open` loc swap-on-solve (the
  swap would need to fire from inside an `if_button1` interface callback,
  which has no `loc_coord`/`loc_id` trigger context -- `%olaf2_gate_completed`
  still lets players walk through on any further click, so progress is not
  blocked); the flavour-only Sven's map / Ulfric's note viewer interfaces
  (`interfaces/olaf2_treasuremap.if`, `olaf2_ulric_parchment.if`, neither
  gates progress in quest-helper's own step map). Verification:
  `mingw32-make -C src sscompile` then `mingw32-make -C src mock230-scripts`
  both exit 0: 13664 scripts compiled (13606 -> 13664, includes this tick's
  new files plus unrelated growth since the last logged count); 0 errors;
  grep of the full build log for "olaf" returned nothing (no warnings/notes
  naming this slice); grep-verified every new/spliced trigger name (incl.
  all nine skeleton variants' `opnpc2`/`ai_queue3`, both `if_button1`
  interfaces' component names) resolves to exactly one definition of its
  trigger type across the whole `server/scripts` tree before compiling.
  Next pending row (smallest-first): #73 Grim Tales, 427 lines (re-verified
  genuinely pending by the prior tick's staleness sweep above).
- slice done (2026-08-11): Grim Tales (row #73) -- re-verified genuinely
  pending first (grep of `server/scripts` + `lc_quests.txt` for
  `grimtales`/`grim_sylas`/`grim_grimgnash` found nothing; no 2009scape
  checkout on this machine). Quest Helper source fetched from
  `github.com/Zoinkwiz/quest-helper` (`helpers/quests/grimtales/GrimTales.java`,
  427 lines, matches the queue's own line count). Found a fully **native
  cache schema** on inspection: dbrow `quest_grimtales` (`configs/all.dbrow`
  id 135, endstate 60, questpoints 1, requirement_stats/stat_xp_awarded
  matching quest-helper's own `getExperienceRewards()` and the wiki reward
  list exactly: 60000 Woodcutting / 25000 Agility / 25000 Thieving / 15000
  Herblore / 10000 Farming / 5000 Hitpoints XP) plus a complete native
  varbit schema on basevars `grim_main`/`grim_second` matching every
  `VarbitID` quest-helper's own Java references by name
  (`GRIM_GRIFFIN_ASLEEP`, `GRIM_GIVEN_FEATHER`, `GRIM_DWARFQUEST`,
  `GRIM_DWARF_VIS`, `GRIM_PIANOTRACK`, `GRIM_PIANO_USED`, `GRIM_HEAD_FOUND`,
  `GRIM_HAVE_PENDANT`, `GRIM_STALK_STATE`, `GRIM_GIANT_DEAD`), all reused
  as-is. dbrow `requirement_quests` (col 25) resolved to id 160 =
  `quest_porcineofinterest` -- wrong per this queue's standing caution --
  hard-gated on Witch's House instead (`%ballquest = ^ball_complete`, the
  wiki + quest-helper's own `getGeneralRequirements()` agree). Fetched
  `Grim_Tales/Quick_guide` and `Transcript:Grim_Tales` for dialogue/mechanic
  detail beyond the helper's own sparse `steps.put` map. Wrote 6 new files
  under `quest_grimtales/{configs,scripts}/`: `quest_grimtales.constant`
  (full derivation writeup + coordinate/zone constants), `grim_journal.rs2`,
  `grim_sylas.rs2` (trinket trade + final reward), `grim_grimgnash.rs2`
  (7-beat bedtime-story dialogue puzzle via `~p_choice4` + feather theft),
  `grim_watchtower.rs2` (wall climb, drain pipe x2, beard climb, Rupert,
  Miazrqa), `grim_witchhouse.rs2` (piano note-sequence puzzle via
  `if_openmain(grim_piano)` + 14 `if_button1` triggers, compartment search,
  shrink-potion recipe, mouse-hole maze routed by `inzone` zone membership
  since the cache places multiple nail-wall climb instances per room, not
  the single coordinate quest-helper's own `Zone`-based steps name),
  `grim_beanstalk.rs2` (plant/water via `opheldu`, climb, Glod hand-spawned
  in his own cloud instance same as Ulfric in Olaf's Quest, golden goblin,
  shrink+chop). Found and fixed one genuine pre-existing bug blocking this
  slice in shared content: `quest_ball_locs.rs2`'s `open_witch_house_door`
  (the witch's house front door is the **same building** as Grim Tales'
  own Miazrqa's house -- confirmed via `ObjectID.WITCHHOUSEDOOR` resolving
  to the identical cache record, the wiki's own "get another key from the
  pot outside the Witch's House" line, and the map square: quest-helper's
  own `house` Zone (2901-2907,3466-3476) converts to the identical m45_54
  square `witchpot`/`witchhousedoor` already occupy) had a refusal
  condition that fired whenever `%ballquest = ^ball_complete` -- i.e.
  *always*, for every Grim Tales player, since Witch's House is a hard
  prerequisite -- permanently locking every one of them out even while
  holding the key. Narrowed to `%ballquest < ^ball_started` only. Also
  merged (not duplicated) a `grim_turnip` branch into
  `skill_herblore/scripts/brew_potion.rs2`'s existing
  `[opheldu,tarrominvial]` trigger for the shrink-potion recipe, plus a new
  non-colliding `[opheldu,grim_turnip]` for the reverse click order.
  Grep-verified every new/spliced trigger name (all `oploc`/`opheld`/
  `opnpc`/`if_button`/`ai_queue3` subjects, both merged shared-file
  triggers) resolves to exactly one definition of its trigger type across
  the whole `server/scripts` tree before compiling -- no duplicate-trigger
  collisions. Deferred (queue-log, same tier as prior slices' own
  deferrals): exact wrong-branch maze coordinates (routed to the nearest
  correct room via zone membership instead of quest-helper's own single
  `leaveWrong1`/`leaveWrong2` coordinates, which are not fully recoverable
  from the helper or wiki alone); Grimgnash's four-choice story
  wrong-answer text (original wording in the same tone -- the wiki
  transcript only records the correct line at each of the 7 beats); the
  piano interface's own `opencompartment`/`searchcompartment` buttons
  (`interfaces/grim_piano.if`) in favour of the world object's native
  `op3=Search` on `grim_piano_open`, matching quest-helper's own
  `searchPiano` being a separate step from `playPiano`; the per-note piano
  highlight varbits (`%grim_piano_note_ue` etc -- the sequence counter
  alone fully validates the puzzle); three of `%grim_beard_climb`'s five
  native cosmetic values (only 0/2 driven); `grim_junglestatue`'s "climb
  again for another golden goblin" flavour object, which has no `op1`/`op2`
  declared in this cache at all -- Glod himself drops the one golden
  goblin quest-helper's own step map actually requires, rather than
  guessing at an unauthored interaction verb; watering-can charge
  consumption (checked generically for "any can with a dose," not
  decremented, a one-off quest interaction rather than the general farming
  system). Verification: `mingw32-make -C src sscompile` clean (built
  `build_win64/sscompile`, 0 diagnostics beyond pre-existing snprintf
  truncation warnings in the compiler itself); `mingw32-make -C src
  mock230-scripts` exit 0, 13736 scripts compiled (13664 -> 13736, this
  tick's 6 new files + 2 merged edits); grep of the full build log for
  "grim" returned zero errors and zero notes naming this slice (only
  pre-existing "no Attack op" warnings on native `grim_*` cache npc
  records already shipped before this port, e.g. `grim_giant_mouse`,
  `grim_grimgnash`, unrelated to any script here); `mock230_pack
  --check-only` could not run in this worktree (`cache.osrs239` is not
  present -- confirmed pre-existing/environmental: the same invocation
  reports ~960 category/cache errors that reproduce identically and
  mention no `grim_*` symbol, matching this queue's own "BUILD PIPELINE
  NOTE" that `mock230-scripts` exit 0 is the real verification bar here).
  Next pending row (smallest-first): #76 Haunted Mine, 435 lines (#74/#75
  already `done`).
- slice #76 done: Haunted Mine -- grep-first confirmed no LC proc (`lc_quests.txt`
  has `quest_haunted`, but that's Ernest the Chicken, a same-word coincidence,
  not this quest) and no 2009scape row; found a full **native cache schema**
  waiting to be wired: dbrow `quest_hauntedmine` (id 68, startnpc
  `saradominist_zealot`, endstate 11, questpoints 2, requirement_stats
  crafting 35 boostable, stat_xp_awarded strength 220000=22000xp -- all
  confirmed against the wiki's own reward/requirement list) plus a full
  native varbit group on basevar `hauntedmine_bits` matching quest-helper's
  own `VarbitID.HAUNTEDMINE_*` names exactly (`heardaboutkey`,
  `liftpoweredonce`/`liftpowerednow`, `begincart_fungus`/`endcart_fungus`,
  `pointspuzzlestarted`, and eight lever bits `lever_a/b/c/d/e/i/j/k`).
  dbrow `requirement_quests` reads 111 (`quest_swansong`) -- wrong per this
  queue's standing caution -- hard-gated on Priest in Peril instead
  (`%priestperil >= ^priestperil_complete`, IN-LC, already fully playable).
  The lever-to-varbit mapping is not guessed: forced by quest-helper's own
  `ConditionalStep` pairing (e.g. `leverAWrong = VarbitRequirement(LEVER_B,
  0)` paired with the object step that pulls `HAUNTEDMINE_POINT_LEVER1` means
  that lever corrects `LEVER_B`, not `LEVER_A`) -- worked out the full
  8-lever target combination (b/a/e/i=1, c/d/j/k=0) from all eight pairings
  and implemented it as a real 0/1-toggle puzzle on the points-info panel's
  native "Check" op, not a scripted replay. All named locs/npcs are native
  cache map geometry (op text confirmed in `configs/all.loc`/`all.npc`:
  entrances "Crawl-down", ladders "Climb-up"/"Climb-down", lifts
  "Go-up"/"Go-down", valve "Turn", levers "Pull", panel "Check", cart/chisel
  crate "Search", mushroom "Pick", crystal outcrop "Cut", stairs
  "Walk-up"/"Walk-down") -- no hand-placing, only trigger scripts written.
  Several loc names are reused at multiple physical placements in this
  sprawling multi-region dungeon (`hauntedmine_laddertop_1e` at three rooms,
  `hauntedmine_ladder_1w` at three, `hauntedmine_puzzle_cart` /
  `hauntedmine_dark_stairs_top` at two each) -- dispatched by `inzone()`
  against quest-helper's own `Zone` rectangles (same technique Grim Tales'
  mouse-hole maze used) or, for the flooded room's two dark stairs sharing
  one room with no distinguishing zone, by `coordx()` side. The
  `hauntedmine_ladder*`/`hauntedmine_laddertop*` names are also registered in
  the shared `ladders_stairs/configs/ladders.loc` under generic
  `climb_up`/`climb_down` categories (a same-tile plane shift, wrong for this
  dungeon's cross-region layout since quest-helper's own WorldPoints show
  every "level" at plane 0 but wildly different x/y) -- this slice's own
  named `[oploc1,...]` triggers override that category default per
  `ladders.rs2`'s own documented name-beats-category rule, without editing
  that shared file. Only Treus Dayth (`hauntedmine_boss_ghost`) is
  hand-spawned on the key-pickup ambush, same tier as Ulfric/Glod in prior
  slices; `saradominist_zealot` and `hauntedmine_boss_key` are both already
  base world spawns (`m53_50.spawn`, `m43_69.spawn`). Deferred: the real-game
  valve/lift "race the ghost before it re-closes the valve" timing mechanic
  (turning the valve opens the lift unconditionally/permanently here, same
  tier as Olaf's Quest's barrel-repair chance); the "dark room" wrong-path
  mechanic for descending without a lit fungus (blocked with a message
  instead, matching Grim Tales' maze precedent); the points-settings panel's
  own map-grid interface (native op1 "Check" used directly); the Salve
  Amulet crafting-recipe unlock and Abandoned Mine shortcut / Nightmare Zone
  rewards (flavour-only per quest-helper's own `getUnlockRewards()`, no
  gating role). Files: `server/scripts/quests/quest_hauntedmine/configs/
  quest_hauntedmine.constant`, `scripts/{hauntedmine_zealot,
  hauntedmine_dungeon,hauntedmine_dayth,hauntedmine_journal}.rs2`; journal
  wired `interface_questjournal/scripts/quest_journal.rs2`. Wiki
  https://oldschool.runescape.wiki/w/Haunted_Mine/Quick_guide +
  Transcript:Haunted_Mine. Verification: `mingw32-make -C src sscompile`
  clean (built `build_win64/sscompile`, 0 diagnostics beyond pre-existing
  snprintf-truncation warnings in the compiler itself); `mingw32-make -C src
  mock230-scripts` exit 0, 13791 scripts compiled (13736 -> 13791); grep of
  the full build log for "hauntedmine" and for "error" (case-insensitive)
  both returned zero hits -- no warnings or errors attributable to this
  slice. `mock230_pack --check-only` not runnable in this worktree (no
  `cache.osrs239` present, same pre-existing environment gap every prior
  slice on this queue has noted). Next = Mountain Daughter (#80, 459 lines;
  #77/#78/#79 already `done`).
- slice #80 done: Mountain Daughter -- Mar 2005, Hamal's missing daughter
  Asleif, Mountain Camp/Rellekka diplomacy, White Pearl food source, Kendal
  the bearsuited "god"; re-verified genuinely pending first (no
  `mountaindaughter`/`mdaughter` proc anywhere in `server/scripts`, no
  `lc_quests.txt` hit besides the unrelated `quest_belowicemountain`, only
  native cache spawns/dbrow/varbit data). Native dbrow `quest_mountaindaughter`
  (id 75, startnpc `mdaughter_hamal` matching quest-helper's own first step,
  endstate 70, questpoints 2, requirement_stats agility 20 boostable,
  stat_xp_awarded attack 1000 + prayer 2000 tenths exactly matching
  quest-helper's own `ExperienceReward`s, no `requirement_quests` column at
  all) + full native varbit schema on basevar `mdaughter_var`
  (`mdaughter_quest_var`, `mdaughter_mud_var`, `mdaughter_relations_var`,
  `mdaughter_food_var`, plus ten more flag bits) reused as-is, matching
  quest-helper's own VarbitID names exactly. This port authors its own
  `%mdaughter_quest_var` breakpoints 0/10/20/30/40/50/60/70 matching
  quest-helper's own `steps.put` keys plus a final completion tier, resolving
  the one real Java ambiguity (whether the Hamal "diplomacy" and "food
  supply" topics interleave) against the wiki Quick guide's own linear
  step numbering (12-20 "Making Peace" fully before 21-26 "Food Supply"),
  which settles it as sequential -- `%mdaughter_food_var` only starts moving
  once `%mdaughter_relations_var` reaches 60. `viking_brundt_child` (Brundt
  the Chieftain, Rellekka longhall) is a **shared npc** already scripted by
  `quest_fremennikexiles/scripts/fremennikexiles.rs2`'s own
  `[opnpc1,viking_brundt_child]` -- per this queue's own standing caution
  that sscompile accepts duplicate trigger definitions with no diagnostic,
  this slice does not declare a second one; it edits that existing trigger to
  check a new `~mdq_brundt_relevant` proc first and falls through to the
  existing `@fx_brundt_longhall` otherwise. Kendal ("The Kendal") is a native
  multi-npc (`mdaughter_multi_bear`, `multivarbit=mdaughter_bear_multi_state`,
  displaying as `mdaughter_bearman` then hiding once the bit flips) --
  triggers bind the wrapper name, confirmed against this tree's only other
  multi-npc precedent (`quest_mm/scripts/mm_daero.rs2`'s own
  `[opnpc1,mm_daero]`, not a display-variant name); the real fight is the
  separate hand-spawned `mdaughter_bearman_fighter` (native combat stats
  already in `npc_combat/`), same tier as Treus Dayth in Haunted Mine. The
  corpse is dropped via `obj_add(...,^lootdrop_duration)` on the fighter's
  `ai_queue3` death hook, matching quest-helper's own plain-walk-over
  `grabCorpse` TileStep; its native `ifop3=Bury` op is reused directly for
  the burial action, the same `[opheld<n>,...]` binding style
  `skill_prayer/scripts/bury_bone.rs2` established for bones. Deferred:
  quest-helper's own long pole/plank/glove alternates matrix (checked for
  `mdaughter_stick` and the four base plank types specifically, and "any item
  in the worn hands slot" for gloves, not the full exceptions lists); the
  flat-stone crossings' exact "attempt without a plank" fail mechanic (always
  fails outbound without the item, always succeeds with it; the return leg
  simplified to always succeed either way, no formula recoverable); the
  corpse-burial tile soft-kept to "somewhere on Lake Island 3" via `inzone`
  rather than quest-helper's own single named tile (same tier as this
  queue's standing caution about cache coordinates vs. raw quest-helper
  WorldPoints). Files: `server/scripts/quests/quest_mountaindaughter/
  configs/quest_mountaindaughter.constant`, `scripts/
  {mountaindaughter_camp,mountaindaughter_spirit,mountaindaughter_kendal,
  mountaindaughter_burial,mountaindaughter_journal}.rs2`; one merged edit to
  `quest_fremennikexiles/scripts/fremennikexiles.rs2`'s existing Brundt
  trigger; journal wired `interface_questjournal/scripts/quest_journal.rs2`.
  Wiki https://oldschool.runescape.wiki/w/Mountain_Daughter/Quick_guide +
  Transcript:Mountain_Daughter. Verification: `mingw32-make -C src sscompile`
  clean (built `build_win64/sscompile`, 0 diagnostics beyond pre-existing
  snprintf-truncation warnings in the compiler itself); `mingw32-make -C src
  mock230-scripts` exit 0, 13839 scripts compiled (13791 -> 13839); grep of
  the full build log for "mdaughter"/"mountaindaughter"/"mdq_"/"error"
  (case-insensitive) all returned zero hits -- no warnings or errors
  attributable to this slice, and none attributable to the shared
  `fremennikexiles.rs2` edit either. `mock230_pack --check-only` not runnable
  in this worktree (no `cache.osrs239` present, same pre-existing environment
  gap every prior slice on this queue has noted). Bonus finding while
  auditing row #80's neighbours: row #54 Shades of Mort'ton was stale --
  LostCity already has a (partial, stub-only) proc for it in
  `server/scripts/quests/quest_mortton/`, so it belongs on
  `CONTENT_PORT_QUEUE.md`, not this queue; corrected in the table above, see
  that row's own note. Next pending row (smallest-first, after that
  correction): #119 Swansong, 644 lines.
- slice #119 done: Swan Song -- May 2006, Herman Caranos's besieged
  Piscatoris Fishing Colony. Grep-first confirmed genuinely unowned: no
  `lc_quests.txt` hit, no `swansong`/`swanarnold`/`swanseatrol`/`swanherman`
  proc anywhere in `server/scripts` before this slice (only native cache
  spawns/dbrow/varbit/combat/multi-npc data). Quest Helper source fetched
  from `github.com/Zoinkwiz/quest-helper`
  (`helpers/quests/swansong/{SwanSong,FixWall,FishMonkfish}.java`, 644 lines
  total, matching the queue's own line count exactly). Native dbrow
  `quest_swansong` (id 111, endstate 200, questpoints 2, requirement_stats =
  Magic 66 + Cooking 62 + Fishing 62 + Smithing 45 + Firemaking 42 +
  Crafting 40, matching quest-helper's own `SkillRequirement` list and
  `stat_xp_awarded` = Magic 15000 + Prayer 10000 + Fishing 50000 exactly,
  tenths format) + native varbit schema on basevar `swansong_quest`
  (`%swansong` main stage plus `%swansong_franklin`, `%swansong_wall_1..5`,
  `%swansong_arnold`, `%swansong_trolls`, `%swansong_bones`) and
  `swansong_temp` (`%swansong_ambush`, `%swansong_colony`) reused as-is,
  matching quest-helper's own VarbitID names exactly. `requirement_quests`
  wrong (dbrow id 107 resolves to none of In Aid of the Myreque/Garden of
  Death/other unrelated tables sharing that id number, not quest-helper's
  own One Small Favour id 74 or Garden of Tranquillity id 90; two *other*
  quests' own `.constant` files independently flag `quest_swansong` itself
  as a bad `requirement_quests` target too) -- soft-skipped both real
  prereqs (queue rows #157, #125, both still pending), matching the King's
  Ransom / Mourning's End Part I precedent for unported sibling quests.
  **The main `%swansong` breakpoints are not authored -- they're the real
  underlying values**, recovered from three native multi-npc records keyed
  on `multivarbit=swansong` (`swan_multioutside` at the colony gate displays
  Herman at values 0/5/10/15/20 and the Wise Old Man's cutscene appearance
  at 30/40/50/55; `wom_multi` at Draynor displays the Wise Old Man for
  values 0-29 and hides him from exactly 30 onward, the same tick he
  reappears at the gate) -- this cross-validates quest-helper's own
  `steps.put(...)` keys as literal real varbit values. Also native
  multi-locs keyed directly on quest sub-vars confirmed several design
  choices independently: `swan_firebox`/`swan_press` swap appearance on
  `multivarbit=swansong_franklin` (press only "activates" once the firebox
  is lit, matching this port's own gate), `swan_wall_1..5` swap
  broken/fixed appearance on their own `multivarbit=swansong_wall_n`, and
  `swan_fish` (the quest-only fishing spot, distinct from the permanent
  post-quest `swan_fishingspot` npc unlocked by `getUnlockRewards()`) shows
  as active for `%swansong_arnold` 0-5 and hides at exactly 6 -- this port's
  own chosen "finished" threshold. Scripts:
  `quest_swansong/scripts/{swansong_colony,swansong_army,swansong_finale,
  swansong_journal}.rs2` + `configs/quest_swansong.constant`; two merged
  edits to existing shared triggers per this queue's own "no duplicate
  trigger" caution -- `quest_makingfriendswithmyarm/scripts/
  makingfriendswithmyarm.rs2`'s own `[opnpc1,wise_old_man]` (Draynor) and
  `areas/area_yanille/scripts/yanille_thin_npcs.rs2`'s own `[opnpc1,
  wizard_frumscone]`, both now check a `~swansong_*_relevant` boolean proc
  first and fall through unchanged otherwise (same pattern Mountain
  Daughter's own Brundt merge established). Entrance-ambush trolls and the
  Sea Troll Queen hand-spawned (native combat stats reused as-is from
  `npc_combat/s/swan_troll_ambush.combat` / `swan_seatroll_queen.combat`),
  same `npc_add`/`[ai_queue3,...]`/`npc_findhero`/`~npc_default_death` tier
  as Treus Dayth / Ulfric / Glod. Journal wired
  `interface_questjournal/scripts/quest_journal.rs2`. Wiki
  https://oldschool.runescape.wiki/w/Swan_Song/Quick_guide +
  Transcript:Swan_Song (original-wording dialogue covering the same beats,
  same Jagex-copyright caveat every prior slice notes). Deferred: the real
  quest's own multi-NPC siege-battle cutscene finale (native combat data
  exists for `swan_skeleton_battle/training/unattackable`,
  `swan_troll_battle`, `swan_troll_general`, `swan_wom_ambush`,
  `swan_wom_coma` -- strongly implying a real large-scale scripted battle
  with the Wise Old Man's skeletal army and the Wise Old Man himself being
  knocked unconscious partway through, not recoverable from quest-helper or
  the wiki quick guide beyond "defeat the Sea Troll Queen"; this port
  hand-spawns the real Queen for a straightforward finale instead), the
  ambient colony population (`swan_colonist_1..3`, `swan_skeleton_training`
  dummies, the boat-trip npcs, `swan_kalphite_1/2`, `swan_drunkendwarf`),
  cooking-gauntlets burn-rate reduction, the Crafting Guild's own
  clay-mining/potter's-wheel minigame (Master Crafter hands over a pot +
  lid directly instead), Western Provinces hard-diary hook. `mingw32-make
  -C src sscompile` clean (rebuilds `build_win64/sscompile`, only
  pre-existing snprintf-truncation warnings in the compiler itself);
  `mingw32-make -C src mock230-scripts` exit 0, 13887 scripts compiled
  (13839 -> 13887); grep of the full build log for "swansong"/"swan_"
  (case-insensitive) returned zero hits -- no warnings or errors
  attributable to this slice or either merged shared-trigger edit.
  `mock230_pack --check-only` not runnable in this worktree (no
  `cache.osrs239` present, same pre-existing environment gap every prior
  slice on this queue has noted). Next pending row (smallest-first): #120
- slice #120 done: Royal Trouble -- grep-first audit found no LC/2009scape
  ownership (no `royaltrouble`/`royal_trouble` hits anywhere in the tree,
  no `lc_quests.txt` entry); native dbrow `quest_royaltrouble` (id 112,
  endstate 30) and full native varbit schema on `royal_questvarbits`
  (`royal_quest`/`royal_misc`/`royal_etc`) + `royal_varbits` were already
  present but undeclared (bare varp reservations), claimed here same as
  `kr_varp1/2/3`. Thematically and mechanically linked to Throne of
  Miscellania (#99, done) as the task flagged -- checked `quest_misc`
  first and found the SAME native npcs (misc_advisor_ghrim/
  misc_king_vargas/misc_queen_sigrid) are reused by Royal Trouble's own
  startnpc (dbrow startnpc=3670=misc_advisor_ghrim); merged a
  `~royaltrouble_relevant` branch into quest_misc's own existing
  `misc_advisor_ghrim.rs2`/`misc_king_vargas.rs2`/`misc_queen_sigrid.rs2`
  `[opnpc1,...]` triggers instead of duplicating them (duplicate trigger
  definitions compile silently with no diagnostic -- critical correctness
  rule), falling through to the original Throne of Miscellania dialogue
  when not relevant. Fetched quest-helper's own RoyalTrouble.java via
  GitHub raw (summarized by the fetch tool, not verbatim) to recover its
  own VarbitRequirement breakpoint sets: ROYAL_MISC
  {10,20,30,40,50,60,80,110,120} (killedBoss = ROYAL_MISC>=120), ROYAL_ETC
  {10,20,40} (finishedFinalConvoWithSigrid = ROYAL_ETC>=40) -- used as the
  progression anchors; the semantic assigned to each intermediate
  breakpoint is this port's own reconstruction (documented as such in
  `configs/royaltrouble.constant`), not independently verified against
  the original bytecode. The lift-repair puzzle's breakpoints
  (`%royal_liftstage` 0-9, `%royal_coalinengine` 0-5) ARE independently
  confirmed -- read directly off this cache's own multivarbit `.loc`
  records (`royal_side_scaffold_multiloc`, `royal_top_scaffold_multiloc`,
  `royal_engine_platform_multiloc`, `royal_lift_platform_multiloc`,
  `royal_lift_platform_at_top_multiloc`), per this queue's own methodology
  step of preferring native multi-loc records for real progression
  breakpoints; implemented as a genuine multi-step item-on-object puzzle
  (crates give beams/rope; opheldu combines beam+pulley beam into
  long/longer pulley beams; oplocu applies each piece to the concrete
  scaffold/platform/engine loc names in sequence; coal shoveled into the
  engine via opheldu; `oploc1` "Use-Lift" rides to the top) rather than
  narrated, since the cache ships every needed item/loc gameval
  (`royal_beam`, `royal_plank_pulley[_long][er]`, `royal_coal_engine`,
  `royal_mining_prop`) already declared and world-placed. One crate,
  `royal_crate_planks+pulleys`, has a literal `+` in its own cache
  identifier with no precedent anywhere in this tree for use as a trigger
  subject; rather than risk an unverified parser edge case, that starter
  pulley beam is instead handed over narratively by Donal alongside the
  mining prop and coal engine -- the other two crates (plain identifiers)
  are real repeatable interactions. dbrow `requirement_quests` decodes to
  The Corsair Curse (id 147) -- not a real Royal Trouble prerequisite
  (same recurring cache decode/linkage corruption flagged on King's
  Ransom's row and others); the wiki's actual direct prerequisite is
  Throne of Miscellania alone (which itself already transitively requires
  Heroes' Quest + The Fremennik Trials), so this slice hard-gates on
  `%misc_quest = ^misc_king_signed_treaty` instead. Investigation-phase
  NPCs (the wiki's Gunnhild/Leif/Frodi/Magnus/Helga/Haming/Matilda) don't
  resolve as distinct npcs anywhere in `configs/all.npc.compack`; this
  cache's own `royal_misc_guard`/`royal_etc_guard` (the very soldiers each
  side blames) stand in as the real interview targets instead, matching
  this queue's established "cache wins" substitution precedent (The Feud,
  Spirits of the Elid, Another Slice of H.A.M.). Boss
  (`royal_sea_snake_mother_smaller`, "Giant Sea Snake", combat level 149 --
  confirmed via `all.npc`'s own vislevel field, matching the wiki exactly)
  hand-spawned lazily on trigger with `~npc_retaliate`/`npc_findhero`/
  `~npc_default_death`, the same idiom as Contact's Giant Scarab
  (`contact_scarab.rs2`) -- no extinguish-light/poison boss mechanics
  precedent in this tree, left to the generic combat system. Zero
  hand-spawning for every other npc; all already world-spawned
  (`m39_60`/`m40_60`/`m39_160`/`m40_160` .spawn files) -- packed coords in
  the constant file are the exact tiles of those spawns, not invented (no
  Zone-bounds source available to refine further, flagged as a
  deferred-precision item like other approximate-coord slices). Deferred:
  cave hazards (steam vents, falling rocks, slippery-rock plank crossing)
  as pass-through terrain -- no damage/fail-chance system precedent
  anywhere in this tree to hook into, same tier as Grim Tales' deferred
  stone/rock fail rolls; the heavy box stays a permanent unconsumed
  souvenir item post-quest rather than being formally "turned in" a
  second time. Hit one syntax bug during the build: this dialect's string
  literals don't support `\"` escapes (verified by the compiler error,
  not assumed) -- reworded the one line that needed an embedded quote
  instead. `mingw32-make -C src sscompile` clean (only pre-existing
  snprintf-truncation warnings in the compiler itself); `mingw32-make -C
  src mock230-scripts` exit 0, 13940 scripts compiled (13887 -> 13940);
  grep of the full build log for "royal_"/"royaltrouble" (case-insensitive)
  returned zero warnings or errors attributable to this slice or any of
  the three merged shared-trigger edits. `::royaltrouble` /
  `::royaltroublerun` debug commands added, matching every prior slice's
  idiom; `mock230_pack --check-only` not runnable in this worktree (no
  `cache.osrs239` present, same pre-existing environment gap every prior
  slice has noted). Wiki
  https://oldschool.runescape.wiki/w/Royal_Trouble/Quick_guide + full
  walkthrough (dialogue paraphrased, not verbatim, per copyright, same
  caveat as every prior slice). Next pending row (smallest-first): #121
  The Great Brain Robbery.
  Royal Trouble, 657 lines.

- slice #121 done: The Great Brain Robbery -- grep-first audit found no
  LC/2009scape ownership (`lc_quests.txt` clean, no `brainrobbery`/
  `feverharmle`/`brainbrothe` hits anywhere in `server/scripts`); native
  dbrow `quest_greatbrainrobbery` (id 130, endstate 130, questpoints 2)
  already declared in the cache, unused until this slice. Master progress
  var `%brain_quest_var` (0/10/.../130) is a plain varp, not a sub-varbit --
  confirmed authoritative (not inferred) by this cache's own multi-npc
  records: `brain_tranquility`/`brain_island_tranquility` both declare
  `multivarp=brain_quest_var`, swapping Brother Tranquility from zombie to
  human exactly at value 100, and `brain_island_fenkenstrain` only renders
  Fenkenstrain from value 70 on -- both landing exactly on quest-helper's
  own `steps.put` keys (fetched via GitHub raw), independent confirmation
  of the full 0/10/.../130 breakpoint set, stronger than most prior slices
  where only endpoints were independently checkable. Crate-build
  (`%brain_crate` 1..5) and door-breach (`%brain_barrel_setup` 2..5)
  puzzles both independently confirmed via this cache's own
  `brain_fenk_crate`/`brain_mon_entrance_door_multi` native multiloc
  records, matching quest-helper's own VarbitRequirement thresholds
  exactly -- implemented as real click/item-on-loc puzzles (Build ->
  Add-bottom -> Fill 10 wooden cats -> Blow wolf whistle -> attach shipping
  order; keg -> fuse -> tinderbox) using the concrete cache loc state names
  directly, same idiom as Royal Trouble's lift repair. Statue passage and
  underwater stairs repair are likewise cache-baked map locs with no
  `.spawn` entry anywhere in this tree -- script triggers only, zero
  hand-spawning needed for any puzzle geometry. dbrow `requirement_quests`
  decodes to Black Knights' Fortress/Lost City -- not real prerequisites
  (same recurring cache decode corruption); real prereqs per wiki are
  Creature of Fenkenstrain (hard-gated on `%creatureoffenkenstrain >=
  ^fenk_complete`, already implemented), Cabin Fever and Recipe for
  Disaster/Freeing Pirate Pete (both have native dbrow rows but zero
  scripts anywhere in `server/scripts` -- soft-skipped, matching this
  queue's established convention for unported sibling prereqs, e.g. King's
  Ransom's One Small Favour). Two shared-file merges to avoid duplicate
  triggers (critical correctness rule): `areas/area_canifis/scripts/
  rufus.rs2`'s existing `[opnpc1,werewolfshopkeeper1]` trigger (Rufus's
  crate-scheme branch) and `quests/quest_fenkenstrain/scripts/
  fenkenstrain.rs2`'s existing `@fenk_talk` label (Fenkenstrain's own
  branch) -- both gated on `%brain_quest_var` relevance, falling through to
  existing dialogue unchanged otherwise. Mi-Gor/Barrelchest (level 190)
  hand-spawned lazily on trigger for the church confrontation, same idiom
  as Royal Trouble's Giant Sea Snake / Contact's Giant Scarab; no
  prayer-disabling boss mechanic precedent in this tree, left to the
  generic combat system. Deferred: wooden-cat crafting is a simplified
  oak-plank + knife make-action, not the real player-owned-house workshop
  flatpack minigame (no POH workshop precedent anywhere in this tree);
  surgical instruments (clamp/tongs/3 bell jars/30 skull staples) drop from
  Sorebones kills via a simple scripted `obj_add` on `ai_queue3` death, not
  a verified native drop table (not recoverable from available sources);
  Barrelchest's broken anchor reward stays broken (post-quest pirate-smith
  repair flavour not implemented, same tier as other reward items needing
  later unlocks elsewhere in this tree). `mingw32-make -C src sscompile`
  clean; `mingw32-make -C src mock230-scripts` exit 0, 14078 scripts
  compiled (14041 -> 14078); grep of the full build log for "brain"
  returned exactly one hit during development (an unknown-constant
  `^chat_evil` typo, fixed to `^chat_angry`, a real cache-confirmed mood
  constant) and zero hits on the clean rebuild. Wiki
  https://oldschool.runescape.wiki/w/The_Great_Brain_Robbery/Quick_guide +
  https://oldschool.runescape.wiki/w/The_Great_Brain_Robbery (dialogue
  paraphrased, not verbatim, per copyright, same caveat as every prior
  slice). `mock230_pack --check-only` not runnable in this worktree (no
  `cache.osrs239` present, same pre-existing environment gap every prior
  slice has noted). Next pending row (smallest-first): #122 Rum Deal, 662
  lines.

- slice #122 done: Rum Deal -- grep-first audit found no LC/2009scape
  ownership (`lc_quests.txt` clean, no `rumdeal`/`rum_deal` hits anywhere in
  `server/scripts`; the only tree hits were the native
  `interfaces/rum_deal_title.if` / `rum_deal_censor.if` cutscene-adjacent
  interfaces and `configs/all.dbrow`'s own undeclared `[quest_rumdeal]`
  block). Fetched quest-helper's own `RumDeal.java` (497 lines,
  steps.put 0-18) and its `SlugSteps.java` sub-helper (165 lines, the
  slugling-fishing/pressure-barrel arc) verbatim via GitHub raw -- both
  complete, not summarized. Native dbrow `quest_rumdeal` (id 95, endstate 19,
  questpoints 2, startnpc 601 = `deal_pete`, requirement_stats fishing 50 +
  prayer 47 + crafting 42 + slayer 42 + farming 40 -- all 5 rows present and
  matching quest-helper's own `getGeneralRequirements()` exactly,
  stat_xp_awarded fishing/prayer/farming 7000xp each -- raw dbrow values *10
  internal xp units, matches quest-helper's own `ExperienceReward` list
  exactly) already declared in the cache, unused until this slice. dbrow
  `requirement_quests` decodes to ids 163/111 (A Night at the Theatre / Swan
  Song depending on which `values=0:0:` block a naive id scan lands on) --
  not real prerequisites (same recurring cache decode corruption flagged
  repeatedly on this queue); the real prerequisites are quest-helper's own
  `getGeneralRequirements()`: Zogre Flesh Eaters (`quest_zogreflesheaters`,
  `%zogre >= ^zfe_complete`, IN-LC, already implemented) and Priest in Peril
  (`quest_priestperil`, `%priestperil >= ^priestperil_complete`, IN-LC,
  already implemented) -- both hard-gated. Three real native sub-varbits on
  basevar `deal_var` back the actual puzzles, all independently confirmed via
  this cache's own multiloc records rather than guessed: `%deal_farming`
  (blindweed patch, quest-helper's own thresholds rakedPatch=3/plantedPatch=4/
  grownPatch=5) matches `deal_blindweed`'s own native multiloc *0-indexed*
  against the variable value (multiloc1 shown at value 0) -- value3 =
  `deal_blindweed_empty` (freshly raked), value4 = `deal_blindweed_seed`
  (just planted), value5 = `deal_blindweed_fullygrown` (op1=Pick declared
  directly on that variant), landing exactly on quest-helper's own
  thresholds; `%deal_barrel` (pressure barrel sluglings, 0-5) matches
  `SlugSteps.java`'s own `getVarbitValue(DEAL_BARREL)` read and is
  independently confirmed via `deal_multi_lever`'s own native multiloc
  (values 0-4 show `deal_lever_down`, value5 shows `deal_lever_up` -- the
  lever visibly pops up at exactly 5 sluglings, matching quest-helper's own
  `numHandedIn >= 5 -> pullPressureLever` branch); `%deal_multi_hopper`
  (brewing control, 0-2, drives `deal_multicontrol`'s own native multiloc
  idle/spinning/running) has no quest-helper VarbitID of its own -- the
  original source gates the wrench/spirit arc purely on item possession
  (`holyWrench`/`evilSpiritNearby`), reused here only for cosmetic feedback.
  The master progress var, `%deal_quest` (native basevar, bare reservation),
  has no native multi-npc/multi-loc record fixing a concrete breakpoint set
  (unlike Great Brain Robbery's `brain_quest_var`), so this port's own
  16-value reconstruction (0-15, one per real distinguishable game state,
  collapsing quest-helper's own duplicate-instruction step keys like
  steps.put(0)/steps.put(1) which both point at the identical `talkToPete`
  object) is documented as such in `configs/rumdeal.constant`;
  `^deal_complete = 19` is not invented -- read directly off the dbrow's own
  `endstate` column, matching Contact!/Great Brain Robbery/Priest in Peril's
  own "_complete constant = dbrow endstate" idiom. npcs=deal_pete,
  deal_captian_braindeath (cache's own spelling, not "captain"), deal_davey,
  deal_captian_donnie, deal_evil_spirit, deal_fever_spiders1 -- this queue's
  own row hint abbreviations (`dealevilsp`/`dealpete`/`dealcaptian`) don't
  match any real cache name; cache wins, same precedent as The Giant Dwarf /
  My Arm's Big Adventure. Every npc except the Evil Spirit is already
  world-spawned (`server/scripts/areas/world/configs/m33_79.spawn` for the
  island roster incl. 11x `deal_fever_spiders1` in the basement and 3x
  `deal_squid` fishing spots around the coast, `m57_55.spawn` for Pete at the
  Ectofuntus dock) -- only the level-150 Evil Spirit is hand-spawned lazily
  on trigger next to the brewing control, same idiom as every prior
  on-demand quest boss on this queue (Royal Trouble's Giant Sea Snake, Great
  Brain Robbery's Barrelchest, Contact!'s Giant Scarab). The blindweed
  patch's 5-minute growth wait uses a genuine one-shot `[timer,
  deal_blindweed_grow]` player-timer (500 ticks @ 0.6s/tick) set on planting,
  same `settimer`/`[timer,...]` idiom already established by Draynor Manor's
  `manor_vines.rs2`. The stagnant-water gate (`deal_gate_closed` ->
  `deal_gate_open`) is a plain one-time `loc_del`/`loc_add` swap, not tied
  into `general_use/scripts/gates.rs2`'s own category-bound `_gate_main_*`
  system (this gate has no `category=` field, so it never matches that
  system's wildcard binds) -- a small bespoke swap instead, snapshotting
  `loc_coord`/`loc_angle`/`loc_shape` before delete, matching that same
  file's own snapshot-before-delete caution. Fishing sluglings is a simple
  guaranteed-catch loop (no skill-check precedent needed since Fishing 50 is
  already a hard quest-start requirement); the rare "Karamthulhu" joke catch
  (`deal_karamthulhu`/`inactivepet_squid`) is deferred, no rare-roll
  precedent anywhere in this tree. Fever spider's disease-on-hit-without-
  slayer-gloves penalty deferred (no disease/status-effect precedent for
  combat in this tree, same "left to the generic combat system" reasoning as
  Great Brain Robbery's Barrelchest prayer-disable). The Holy Wrench used
  mid-quest to fix the brewing control is quest-helper's own listed
  `ItemReward` too -- not re-granted at completion, the player simply keeps
  the one earned earlier (confirmed correct: it is never marked `isConsumed`
  by any interaction in this port). Files:
  `quests/quest_rumdeal/{configs/rumdeal.constant,configs/rumdeal.varp,
  scripts/deal_{shared,pete,braindeath,farming,water_hopper,sluglings,combat,
  donnie,journal,debug}.rs2}` + one line added to
  `interface_questjournal/scripts/quest_journal.rs2`. Checked the whole
  `server/scripts` tree for every new trigger/proc/debugproc name before
  writing (`deal_pete`, `deal_captian_braindeath`, `deal_davey`,
  `deal_captian_donnie`, `deal_evil_spirit`, `deal_fever_spiders1`, every
  `deal_blindweed_*` variant, `deal_gate_closed`, `deal_stagnant`,
  `deal_hopper`, `deal_brewvat_tap`, `deal_squid`, `deal_pressure`,
  `deal_multi_lever`, `deal_multicontrol`, `deal_spider_body`,
  `[timer,deal_blindweed_grow]`, `[debugproc,rumdeal]`/`[debugproc,
  rumdealrun]`, every `[proc,deal_*]`) -- zero collisions, no merges needed.
  `mingw32-make -C src sscompile` clean (only pre-existing snprintf-
  truncation warnings in the compiler itself); `mingw32-make -C src
  mock230-scripts` exit 0, 14111 scripts compiled (14078 -> 14111); grep of
  the full build log for "rumdeal"/"deal_" (case-insensitive) returned zero
  hits -- no warnings or errors attributable to this slice. `::rumdeal` /
  `::rumdealrun` debug commands added, matching every prior slice's idiom.
  Wiki https://oldschool.runescape.wiki/w/Rum_Deal/Quick_guide (dialogue
  paraphrased, not verbatim, per copyright, same caveat as every prior
  slice). `mock230_pack --check-only` not runnable in this worktree (no
  `cache.osrs239` present, same pre-existing environment gap every prior
  slice has noted). Next pending row (smallest-first): #124 The Fremennik
  Isles, 670 lines.

- slice #124 done: The Fremennik Isles -- grep-first audit found no LC/
  2009scape ownership (`lc_quests.txt` clean; no `fremennikisles`/
  `fristroll`/`frisd_`/`fris_` hits anywhere in `server/scripts` besides the
  unrelated `quest_fremennikexiles`/`quest_viking`/`quest_mountaindaughter`
  trees -- the three other Fremennik-named quests, correctly distinct per
  this slice's own briefing). Fetched quest-helper's own
  `TheFremennikIsles.java` (639 lines, steps.put 0-332) and its
  `KillTrolls.java` sub-helper (the troll-cave kill-counter NpcStep) verbatim
  via GitHub raw, not summarized (the WebFetch tool's own summarization pass
  was lossy on the first attempt -- switched to a direct `curl` download for
  verbatim source, worth noting for future slices). Native dbrow
  `quest_fremennikisles` (id 127, endstate 340, questpoints 1, startnpc 1900,
  requirement_stats agility 40 + construction 20 -- both rows match
  quest-helper's own `getGeneralRequirements()` exactly; the Woodcutting 56 /
  Crafting 46 entries quest-helper also lists are NOT real quest-start gates,
  each is OR'd against `not ironman` in the source so only ever bites on
  ironman item-sourcing routes -- the dbrow's own 2-row requirement_stats
  table confirms they aren't real requirements, skipped) already declared in
  the cache, unused until this slice. dbrow `requirement_quests` decodes to
  id 57 (`quest_naturespirit`) -- not a real prerequisite, same recurring
  cache decode corruption flagged repeatedly on this queue; the real
  prerequisite is quest-helper's own The Fremennik Trials finished
  (`%viking >= ^viking_complete`, LC's `quest_viking`, already implemented,
  hard-gated). Master progress is the native varbit `fris_quest` (10 bits on
  basevar `fris_r1`) -- no native multi-record ties a concrete breakpoint set
  to it, so this port's own 0-26 + 340 reconstruction collapses
  quest-helper's own duplicate-instruction step keys (steps.put(5)/(10),
  (60)/(70)/(80), (100)/(110)/(120), (160)/(170)/(180)/(190), (240)/(250),
  (300)/(310), (325)/(330)/(331)/(332) each point at one identical
  ConditionalStep object), same reconstruction idiom as Rum Deal's
  `deal_quest`; `^fris_complete = 340` read directly off the dbrow's own
  `endstate` column, not invented. Three more native sub-varbits on the same
  basevar back real puzzles, independently confirmed via this cache's own
  records rather than guessed: `%fris_m_b3`/`%fris_m_b4` (the two rope-bridge
  repairs quest-helper itself tracks) confirmed via `frisr_rb3`/`frisr_rb4`'s
  own native multiloc (multivarbit=fris_m_b3/fris_m_b4,
  multiloc1=frisb_rope_bridge_broken, multiloc2=frisb_rope_bridge) -- a THIRD
  native pair, `frisr_rb5` on `%fris_m_b5`, has no quest-helper counterpart
  and no wiki mention of a third bridge, flipped alongside `%fris_m_b4` here
  as a documented judgment call so no dangling broken-bridge geometry is left
  behind; `%fris_king` (Mawnis's own crown/no-crown cosmetic swap) confirmed
  via `[fris_r_burgher]`'s own native multinpc record, flipped to 1 only at
  true completion; `%fris_task` (troll-cave kill counter) confirmed by
  `KillTrolls.java`'s own `client.getVarbitValue(VarbitID.FRIS_TASK)` read,
  not inferred -- quest-helper's own text says "kill 10 trolls" while the
  native world spawn (`m37_160.spawn`) only places 9 pre-set `_pc` trolls
  (troll_bodyguard variants explicitly excluded from `KillTrolls.java`'s own
  `addAlternateNpcs` list, left to the generic combat system); all bridges
  and the trapdoor/chest/king-chamber geometry are cache-baked map locs with
  no `.spawn` entry anywhere in this tree, zero hand-spawning needed for
  puzzle geometry, same idiom as Great Brain Robbery's statue passage.
  Six more native varbits on basevar `fris_r2`
  (`frisd_weaponmerchant_taxcollected` Skuli, `frisd_oremerchant_taxcollected`
  Hring Hring, `frisd_fishmonger_taxcollected` Flosi,
  `frisd_armourmerchant_taxcollected` Raum, `frisd_pub_taxcollected`
  Vanligga, `frisd_cook_taxcollected` Keepa) back the tax-collection puzzle;
  quest-helper reuses the SAME Requirement instances across both the
  window-tax round and the later beard-tax round, so this port explicitly
  clears all six back to 0 when the second round starts
  (`~fris_reset_tax`), the only behaviour consistent with the varbits being
  genuinely shared. Every npc resolves natively with the cache's own
  `fris`/`frisd` prefix, matching quest-helper's own NpcID names exactly --
  no "cache wins" spelling drama this time (unlike Rum Deal's
  `deal_captian_braindeath`). All npcs are already world-spawned except the
  Ice Troll King, hand-spawned lazily on trigger in his chamber, same idiom
  as every prior on-demand quest boss on this queue (Royal Trouble's Giant
  Sea Snake, Great Brain Robbery's Barrelchest, Rum Deal's Evil Spirit).
  Three shared-file merges to avoid duplicate triggers (critical correctness
  rule -- `[opheldu,knife]`/`[opheldu,hammer]`/`[opheldu,needle]` already
  exist elsewhere in this tree): a case for `arctic_pine_log` merged into
  `skill_fletching/scripts/cut_logs.rs2`'s existing `[opheldu,knife]` (log
  splitting for the bridge repairs), a case for `arctic_pine_log` merged into
  `general_use/scripts/hammer.rs2`'s existing `[opheldu,hammer]` switch (the
  Neitiznot shield craft), and a case for `yak_hide_cured` merged into
  `skill_crafting/scripts/leather/leather.rs2`'s existing `[opheldu,needle]`
  switch (the yak-hide armour craft) -- all three deferred/simplified as
  plain item-on-item actions rather than tied to a specific
  woodcutting-stump loc, since no Fremennik-specific stump gameval name
  could be confirmed in `all.loc` (unlike the generic named tree stumps used
  for actual woodcutting). The jester "follow Mawnis's request" performance
  (a `DetailedQuestStep` whose own description is deliberately vague -- in
  the real client this is a random emote the player must copy) is
  implemented as a single scripted dialogue exchange rather than a real
  emote-matching minigame, no follow-the-leader precedent anywhere in this
  tree. Per-npc tax amounts aren't recoverable from quest-helper (it only
  tracks varbits, not currency) -- approximated at 2500gp x4 (window) +
  2000gp x5 (beard) = 20,000gp total, deliberately matching quest-helper's
  own reward-list text ("Around 20,000 coins in assorted rewards during
  quest") rather than an arbitrary guess. `mingw32-make -C src sscompile`
  clean (only pre-existing snprintf-truncation warnings in the compiler
  itself); `mingw32-make -C src mock230-scripts` exit 0, 14161 scripts
  compiled (14111 -> 14161); grep of the full build log for "fris"
  (case-insensitive) returned zero hits -- no warnings or errors
  attributable to this slice or any of the three merged shared-trigger
  edits. `::fremennikisles` / `::fremennikislesrun` debug commands added,
  matching every prior slice's idiom. Wiki
  https://oldschool.runescape.wiki/w/The_Fremennik_Isles/Quick_guide
  (dialogue paraphrased, not verbatim, per copyright, same caveat as every
  prior slice). Files:
  `quests/quest_thefremennikisles/{configs/thefremennikisles.constant,
  configs/thefremennikisles.varp,
  scripts/fris_{shared,journal,debug,gjuki,mawnis,bridges,cave}.rs2}` +
  merges into `skill_fletching/scripts/cut_logs.rs2`,
  `general_use/scripts/hammer.rs2`,
  `skill_crafting/scripts/leather/leather.rs2`,
  `interface_questjournal/scripts/quest_journal.rs2`. `mock230_pack
  --check-only` not runnable in this worktree (no `cache.osrs239` present,
  same pre-existing environment gap every prior slice has noted). Next
  pending row (smallest-first): #125 Garden of Tranquility, 684 lines.

- slice #125 done: Garden of Tranquility -- grep-first audit found no LC/
  2009scape ownership (`lc_quests.txt` clean; `garden`/`tranquility` hits
  elsewhere in the tree were false positives -- The Great Brain Robbery's
  Brother Tranquility NPC and the unrelated `quest_gardenofdeath` (Garden of
  Death, a different quest)). Fetched quest-helper's own
  `GardenOfTranquillity.java` verbatim via GitHub raw (684 lines, steps.put
  0/10/20/30/40/50; note the double-L British spelling of the class/file
  name itself, `github.com/.../helpers/quests/gardenoftranquility/
  GardenOfTranquillity.java`). Native dbrow is ALSO spelled
  `quest_gardenoftranquillity` (double L) even though this queue's own row
  and every file/dir in this port use the single-L `gardenoftranquility` --
  a real, confirmed spelling split (not a typo either direction), documented
  in the constant file header; every `db_getfield`/`quest_complete` call
  uses the double-L dbrow symbol. dbrow id 90, endstate 60, questpoints 2,
  startnpc 1390 (queen_ellamaria), requirement_stats farming 25 (stat id 19
  confirmed against the standard skill-id table, matches quest-helper's own
  `SkillRequirement(Skill.FARMING, 25)` exactly) already declared in the
  cache, unused until this slice. dbrow `requirement_quests` decodes to id
  19, which resolves to `quest_lostcity` -- not a real prerequisite (same
  recurring cache decode corruption flagged on nearly every prior slice);
  the real prerequisite is quest-helper's own `getGeneralRequirements()`:
  Creature of Fenkenstrain finished (`%creatureoffenkenstrain >=
  ^fenk_complete`, LC's `quest_creatureoffenkenstrain`, already implemented,
  hard-gated). Master progress is the native varbit `garden_quest` (6 bits
  on basevar `garden_varp_1`) -- no native multi-npc/multi-loc record ties a
  concrete breakpoint set to it, so this port's own 0/1/10/20/40/50/60
  reconstruction collapses quest-helper's own single giant `steps.put(40)`
  ConditionalStep (which internally covers all six villager fetch-quests,
  the inner-garden planting, and the statue transport) into real
  distinguishable milestones; `^garden_complete = 60` is read directly off
  the dbrow's own `endstate` column, not invented. Every per-npc native
  varbit below IS independently confirmed via this cache's own multiloc/
  multinpc records tying concrete values to real map cosmetics, not
  guessed: `garden_elstan_varbit`/`garden_lyra_varbit`/`garden_kragen_varbit`/
  `garden_dantaera_varbit`/`garden_althric_varbit`/`garden_bernald_varbit`
  (per-npc deal trackers, thresholds matching quest-helper's own
  VarbitRequirement values exactly -- `garden_bernald_varbit` confirmed via
  `garden_burthorpe_vines`'s own native multiloc, which renders diseased for
  values 0-3 and cured only at 4+, matching quest-helper's own
  `usedCureOnVines`(2)/`curedVine`(4) split precisely, i.e. the first,
  weaker Plant cure genuinely does nothing cosmetically); the nine
  inner-garden patches (`garden_delphinium_patch`, `garden_snowdrop_patch`,
  `garden_vine_patch`, `garden_rosebush_patch_red/pink/white`,
  `garden_orchid_pink_patch`/`_yellow_patch`, `garden_white_tree_patch`) all
  confirmed via their own native multiloc growth-stage records, index N =
  varbit value N-1, matching quest-helper's own `notPlantedX<=3`/`<=1`/
  seed-threshold checks exactly; the two statue pairs
  (`garden_king_statue_varbit`/`garden_saradomin_statue_varbit`) each
  confirmed via TWO native multiloc records apiece (the real-world statue
  and the garden's own destination plinth, both reacting to the same shared
  varbit) plus `garden_trolley_varbit` confirmed via the `garden_trolley`
  multinpc skin-swap. `garden_kragen_patch_5_varbit`/
  `garden_kragen_patch_6_varbit`, `garden_cutscene_billybob` and
  `garden_first_time_login` have no quest-helper VarbitID reference
  anywhere in the source -- reserved/unclaimed, left untouched, same
  reasoning as prior slices' unclaimed-bit notes. Every npc resolves
  natively with its own real cache name (`queen_ellamaria`, `elstan`,
  `lyra`, `kragen`, `dantaera`, `brother_althric`, `bernald`,
  `farming_gardener_tree_1` for Alain, `king_roald`, `wise_old_man`) --
  none of these match the queue row's own stale hint abbreviations
  (`gardentroll`/`queenellama`), which don't correspond to any real cache
  npc; cache wins, same recurring pattern as Rum Deal/The Fremennik Isles.
  Marigolds are grown on the REAL, pre-existing, fully-functional Falador
  flower patch (`farming_flower_patch_1`, `skill_farming`'s own generic
  system, `farming_flower_marigold` dbrow) -- quest-helper's own
  `ObjectID.FARMING_FLOWER_PATCH_1` is that exact patch, not a new
  quest-only one, so this slice merges two small hooks into that system's
  existing `farming_plant.rs2`/`farming_harvest.rs2` label blocks rather
  than building a new grower. Onions (`farming_veg_patch_7`/`_8`, Morytania)
  and cabbages (`farming_veg_patch_5`/`_6`, Ardougne) are real cache-declared
  allotment locs quest-helper's own `plantedOnions`/`plantedCabbages`
  Conditions OR together, but only the Falador allotment pair has real
  trigger code anywhere in this tree -- extending the generic multi-region
  allotment system to four more patches is out of scope for one quest
  slice, so this port adds bespoke quest-scoped plant/grow/harvest logic on
  these four previously-unclaimed locs instead, using the same
  `farming_allotments` dbrow data (level/seed-count/xp) the generic system
  itself would use; the real crop-stage cosmetic broadcast
  (`farming_transmit_a`/`_b`, shared scratch varbits reused per-region by
  the generic system) isn't driven by this bespoke logic, a documented
  simplification (patch won't visually change for onlookers). Three shared-
  file merges to avoid duplicate triggers (critical correctness rule): a
  case for `blankrune`/`blankrune_high` merged into `general_use/scripts/
  hammer.rs2`'s existing `[opheldu,hammer]` switch (Alain's strong plant
  cure recipe), a case for `rune_shards` merged into `skill_herblore/
  scripts/grind_ingredient.rs2`'s existing `[opheldu,pestle_and_mortar]`
  trigger (same recipe), and an additive branch merged into the existing
  `[opnpc1,wise_old_man]` trigger in `quest_makingfriendswithmyarm/scripts/
  makingfriendswithmyarm.rs2` (the diplomacy test) alongside that file's own
  pre-existing Swan Song branch -- same "external proc, called from shared
  trigger" idiom used by both. A fourth merge, an additive branch in
  `areas/varrock/scripts/king_roald.rs2`'s own `[opnpc1,king_roald]`
  trigger (the finale hand-off), follows that file's own existing `%dov`
  branch idiom. The Wise Old Man's diplomacy test is a real seven-question
  chat quiz (`~p_choice3`) -- the seven correct answers are quest-helper's
  own literal `addWidgetChoice` strings (not invented), scenario framing
  paraphrased from the wiki's own quick-guide summary (not verbatim quest
  text, per copyright). Deferred/simplified (no established mechanic
  precedent anywhere in this tree): the trolley statue-push route
  (quest-helper's own `setLinePoints` waypoint list) is a single soft-skip
  action once the trolley item is used on the correct real-world statue,
  same "soft-skip: <tedious traversal>" idiom already established in this
  tree (`king_roald.rs2`'s own Shield of Arrav dining-room soft-skip,
  `quest_makingfriendswithmyarm`'s cave-pathing soft-skip); fishing the ring
  back out of the well is a flat 1-in-3 per-click chance, same "simple
  probabilistic-catch loop" idiom as Rum Deal's slugling fishing; crop
  death isn't modelled for any of the eleven planted patches (guaranteed
  growth once planted); growth waits are real one-shot `settimer`/
  `[timer,...]` player-timers (eleven distinct timer names, one per patch/
  crop), deliberately compressed from real OSRS times for playability, a
  documented judgment call; King Roald "following" Ellamaria into the
  garden for the finale is a scripted dialogue exchange, not a real
  npc-follow simulation. `mingw32-make -C src sscompile` clean (only
  pre-existing snprintf-truncation warnings in the compiler itself);
  `mingw32-make -C src mock230-scripts` exit 0, 14236 scripts compiled
  (14161 -> 14236); grep of the full build log for "garden" (case-
  insensitive) returned zero hits -- no warnings or errors attributable to
  this slice or any of the four merged shared-trigger edits. `::
  gardenoftranquility` / `::gardenoftranquilityrun` debug commands added,
  matching every prior slice's idiom. Wiki
  https://oldschool.runescape.wiki/w/Garden_of_Tranquillity/Quick_guide
  (dialogue paraphrased, not verbatim, per copyright, same caveat as every
  prior slice). Files:
  `quests/quest_gardenoftranquility/{configs/gardenoftranquility.constant,
  configs/gardenoftranquility.varp,
  scripts/garden_{shared,wom,elstan,lyra,kragen,dantaera,althric,bernald,
  finalgarden,statues,journal,debug}.rs2}` + merges into
  `skill_farming/scripts/farming_plant.rs2`,
  `skill_farming/scripts/farming_harvest.rs2`,
  `general_use/scripts/hammer.rs2`,
  `skill_herblore/scripts/grind_ingredient.rs2`,
  `quest_makingfriendswithmyarm/scripts/makingfriendswithmyarm.rs2`,
  `areas/varrock/scripts/king_roald.rs2`,
  `interface_questjournal/scripts/quest_journal.rs2`. `mock230_pack
  --check-only` not runnable in this worktree (no `cache.osrs239` present,
  same pre-existing environment gap every prior slice has noted). Next
  pending row (smallest-first): #127 Enakhra's Lament, 688 lines.
- slice #127 done: Enakhra's Lament -- Jan 2006, Kharidian Desert; Lazim the
  mage rebuilds a statue of Enakhra at the quarry south of Bandit Camp to
  re-enter her collapsed temple, then guides the player through a fallen-
  statue/sigil-door ground floor, a Pentyn/fountain/furnace/six-brazier
  puzzle floor, a Boneguard corridor, and a wall repair that frees Akthanakos.
  Grep-first (methodology steps 1-2): no LC proc (`lc_quests.txt` and a
  `enakhra|kharidian|desert.*mine.*collapse|Uzer` sweep of `server/scripts`
  hit nothing but coincidental substring matches -- DT2's own unrelated
  `dt2_enakhra_combat`/`dt2_enakhra_cutscene` cutscene npcs, and `kharidian`/
  `Uzer` hits in unrelated desert-area files), no 2009scape impl (both queue
  docs silent) -- genuinely pending. Native dbrow `quest_enakhraslament` (id
  103, endstate 70, questpoints 2, `requirement_stats` (12,50)=Crafting 50,
  (11,45)=Firemaking 45, (5,43)=Prayer 43, (6,39)=Magic 39 matching
  quest-helper's own `getGeneralRequirements()` exactly, no
  `requirement_quests` column at all -- no prerequisites, matches the wiki)
  plus an unusually rich, **fully native** varbit schema declared across
  three basevars (`enakh_quest_expositbits` -- master `enakh_quest` 7 bits +
  one-shot blurb flags; `enakh_multivarbits` -- room/statue/brazier/wall
  state; `enakh_varbits` -- door locks, sigil doors, `enakh_where_is_lazim`),
  every field name matching quest-helper's own `VarbitID.ENAKH_*` lowercased
  exactly (`configs/all.varbit` lines 7803-8107), reused as-is rather than a
  locally invented catch-all var. Real native multi-npc records confirm
  Lazim visually follows the player between four rooms
  (`enakh_lazim_statue_east_multinpc`/`_fallen_statue_east_multinpc`/
  `_pedestal_multinpc`/`_altar_multinpc`, all `multivarbit=
  enakh_where_is_lazim`) -- writing that real varbit drives it for free. This
  server only ever spawns the wrapper npc/loc types
  (`areas/world/configs/m48_145.spawn`/`m49_45.spawn`) -- multinpc/multiloc
  leaf resolution is client-only rendering, confirmed the same way The
  Feud's slice did -- and several wrappers (`enakh_lazim_*_multinpc` x4,
  `enakh_boneguard_multinpc`, `enakh_akthanakos_multinpc`,
  `enakh_dummy_fountain_multinpc`, `enakh_dummy_furnace_multinpc`) declare no
  op of their own in the cache, only their resolved leaf npcs do -- additive
  op overlay `enakhraslament.npc` (same convention as quest_royaltrouble's
  own `royaltrouble.npc`), plus `enakhraslament.loc` for the temple's
  secret-entrance boulder wrapper. Scripts:
  `enakhraslament_quarry.rs2` (Lazim's quarry-statue dialogue FSM driving the
  real `enakh_statue_multivar` 0-7 directly -- base/body/chiseled/four head
  choices -- bespoke instant-mine triggers on the real, previously-unwired
  `enakh_sandstone_rocks`/`enakh_granite_rocks` locs, reusing
  `~pickaxe_checker`/`~mining_pickaxe_anim` from `skill_mining/scripts/
  mining.rs2`; the boulder entrance), `enakhraslament_temple.rs2` (fallen-
  statue limb chisel, the four real limb doors and four real sigil doors
  each keyed on their own native lock varbit, the shared ladder-up object
  reused at two real breakpoints exactly as quest-helper's own
  `goUpToPuzzles`/`goUpFromPuzzleRoom` do, the camel-mould pedestal, Pentyn/
  fountain/furnace/six-brazier puzzle floor, Crumble Undead on the Boneguard,
  the wall repair, and quest completion), `enakhraslament_journal.rs2`,
  `enakhraslament_debug.rs2`; wired into `interface_questjournal/scripts/
  quest_journal.rs2`. Two shared-file merges to avoid duplicate triggers
  (critical correctness rule): a case for `enakh_granite_medium` merged into
  `skill_crafting/scripts/gem/uncut_gem.rs2`'s existing `[opheldu,chisel]`
  switch (external proc `~enakhraslament_craft_head`, branching on quarry vs.
  puzzle-floor context, same "external proc called from a shared trigger"
  idiom as Garden of Tranquility's Alain/Wise Old Man merges) -- no merge
  needed for Crumble Undead / the fire and air puzzle spells since
  `enakh_boneguard` has no Attack op in the cache at all (only `op2=Talk-to`,
  confirming this was never meant to be a real fight), so those three casts
  are quest-scoped `opnpc1` ritual actions reusing the shared
  `~get_spell_data`/`~check_spell_requirements` procs from `skill_magic/
  scripts/magic.rs2` (level/membership/rune-or-staff possession) rather than
  touching the combat spellcasting system's own `[opnpct,magic_spellbook:*]`
  triggers. Deferred/simplified (documented, no established precedent
  anywhere in this tree for the alternative): the real kg-by-kg sandstone
  assembly collapses to plain quantities of `enakh_sandstone_medium` (5kg)
  handed to Lazim directly -- two of the raw/crafted "base" item names carry
  a literal `+` (`enakh_sandstone_huge_base+legs`/`enakh_sandstone_crafted_
  base+legs`, confirmed a legal script token via `uncut_gem.rs2`'s own
  `shellround_red+black` case, but still unnecessary intermediate items once
  kg bookkeeping is dropped); the four fallen-statue limb pickups collapse to
  one chisel action; sigil pickup is gated on all four limb doors (not a
  specific one per sigil) since quest-helper's own `enterKDoor`/`enterRDoor`/
  `enterMDoor`/`enterZDoor` steps are declared in source but never actually
  assigned to a `steps.put` breakpoint -- gating all four sigils behind all
  four limb doors is stricter/more complete than the guide, not looser; loc
  visuals (door open/closed swap, statue-collapse crack/hole states, Lazim's
  carrying-stone animation) don't update since this server only reads the
  wrapper type, not the real per-state loc name, same limitation as the
  npc side; the wrong-head-on-pedestal edge case and one-shot dialogue-blurb
  flavor fields, and the purely cosmetic Enakhra/Akthanakos form swaps and
  post-quest camulet charge mechanic, are untouched. Rewards: 2 QP, 7000
  Crafting/Firemaking/Magic/Mining XP each (dbrow `stat_xp_awarded` matches
  quest-helper's own `getExperienceRewards()` exactly plus a Mining line the
  guide omits, both awarded), Akthanakos's Camulet. `mingw32-make -C src
  sscompile` clean (only pre-existing snprintf-truncation warnings in the
  compiler itself); `mingw32-make -C src mock230-scripts` exit 0, 14281
  scripts compiled (14236 -> 14281); grep of the full build log for "enakh"
  (case-insensitive) returned zero hits -- no warnings or errors
  attributable to this slice or the one shared-trigger merge. `mock230_pack
  --check-only` not runnable in this worktree (no `cache.osrs239` present,
  same pre-existing environment gap every prior slice has noted). Wiki
  https://oldschool.runescape.wiki/w/Enakhra's_Lament/Quick_guide (dialogue
  paraphrased, not verbatim, per copyright, same caveat as every prior
  slice). Files:
  `quests/quest_enakhraslament/{configs/enakhraslament.{constant,varp,npc,
  loc}, scripts/enakhraslament_{quarry,temple,journal,debug}.rs2}` + merges
  into `skill_crafting/scripts/gem/uncut_gem.rs2`,
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #129 The Slug Menace, 694 lines (row #128 is absent from
  the pending table, presumably already resolved on an earlier sweep).
- slice #129 done: The Slug Menace -- Sept 2006, Witchaven; Sir Tiffy
  Cashien's Temple Knights send the player back to Witchaven (Wanted!/Sea
  Slug's own village) to investigate Col. O'Niall, Brother Maledict and
  Mayor Hobb, uncover a hobgoblin-dungeon false wall and a sealed imposing
  door in the sea slug dungeon, translate a transcript via Jorral, recover
  and repair three torn pages, craft and apply five elemental runes at the
  real runecrafting altars, and defeat the Slug Prince. Grep-first
  (methodology steps 1-2): `lc_quests.txt` and a `slug` sweep of
  `server/scripts` hit nothing but coincidental matches (Sea Slug's own
  `quest_seaslug`, Rum Deal's `deal_sluglings`, slayer rock slugs) -- no LC
  proc, no 2009scape impl -- genuinely pending. Native dbrow
  `quest_slugmenace` (id 118, endstate 14, questpoints 1, requirement_stats
  (12,30)=Crafting 30, (20,30)=Runecraft 30, (18,30)=Slayer 30, (17,30)=
  Thieving 30, matching quest-helper's own getGeneralRequirements() exactly;
  stat_xp_awarded (12,35000)/(20,35000)/(17,35000)=Crafting/Runecraft/
  Thieving 3500 each, matching getExperienceRewards() exactly, no Slayer xp
  despite the Slayer requirement, matching the wiki) + fully native varbit
  schema on one basevar (`quest_slug2`: master `slug2_main` 0-14,
  `slug2_npc_track1/2/3`+`slug2_npc_alltrack`, `slug2_doorbit`,
  `slug2_tornpages`, `slug2_fixed_page`, `slug2_haveslug`, `slug2_used_air/
  earth/water/fire/mind_rune`+`slug2_used_runes`), every field matching
  quest-helper's own `VarbitID.SLUG2_*` names lowercased exactly, reused
  as-is (`configs/all.varbit` lines 13053-13161). dbrow `requirement_quests`
  decodes to Fremennik Isles (127) and Song of the Elves (156) -- the second
  is 2018-era content that cannot predate a Sept 2006 quest, same known
  cache-decode-corruption failure mode this queue's methodology warns about
  (confirmed junk) -- real prerequisites per quest-helper's own
  getGeneralRequirements() are Wanted! FINISHED and Sea Slug FINISHED, both
  already implemented in this tree, gated on those instead
  (`%wanted_main >= ^wanted_complete` / `%seaslugquest >= ^seaslug_complete`).
  Native multi-npc records independently confirm three of `%slug2_main`'s
  breakpoints, used directly (not guessed): `slug2_maledict` STAGE1->STAGE2
  exactly at value 8, `slug2_oniall` STAGE1->STAGE2 at value 9 and
  STAGE2->gone at value 13, `slug2_hobb` STAGE2->STAGE3 at value 12 and
  STAGE3->gone at value 14 (= dbrow endstate) -- the master-var value table
  was hand-derived to land the corresponding narrative beats (Maledict's
  second conversation, O'Niall becoming reachable for page 3, all five runes
  applied, Slug Prince killed) on exactly those four numbers, then verified
  against all three records, not the reverse. This server only ever spawns
  the wrapper npc/loc types; three wrappers (`slug2_oniall`, `slug2_jeb`,
  `slug2_holgart_jeb`) declare no op of their own in the cache -- additive
  op overlay in `theslugmenace.npc` (`slug2_hobb`/`slug2_maledict`/the
  villager wrappers already carry `op1=Talk-to` natively, no overlay
  needed); `slug2_hidden_entrance` (the false-wall multiloc wrapper)
  likewise needed one, in `theslugmenace.loc`. Three genuine shared-trigger
  merges to avoid duplicate triggers (critical correctness rule): Sir Tiffy
  Cashien (`rd_teleporter_guy`) already has a live trigger for Wanted! --
  `~slugmenace_tiffy_talk` is called from `wanted_tiffy_amik.rs2`'s own
  `wanted_main >= wanted_complete` branch; Jorral (`makinghistory_jorral`)
  already has a live trigger for Making History -- `~slugmenace_jorral_
  translate` is called first with an early return; Bailey (`bailey`, Fishing
  Platform) already has a live trigger for Sea Slug -- `~slugmenace_bailey_
  talk` likewise. A fourth merge was caught only after an initial build
  (sscompile gives no duplicate-trigger diagnostic, confirmed the hard way):
  `holgartlandtravel` -- the wiki/quest-helper's own "Holgart, north of
  Witchaven" -- turned out to be the *same* Holgart already world-spawned
  and fully scripted for Sea Slug (`areas/area_fishing_platform/scripts/
  holgart.rs2`), not a separate unspawned npc; the first draft's own
  `[opnpc1,holgartlandtravel]` block was deleted and replaced with a branch
  merged into that file's existing `[label,holgartland_talk]`. Two more
  external-proc merges (not duplicate triggers, since the item names are
  quest-exclusive): a case for the five `slug2_rune_*_blank` items merged
  into `skill_crafting/scripts/gem/uncut_gem.rs2`'s existing `[opheldu,
  chisel]` switch (chisel+essence engraving, 5-way choice), and a case for
  the same five blanks merged into `skill_runecraft/scripts/runecraft.rs2`'s
  existing `[oplocu,_rc_altar]` switch (charging at the real air/water/
  earth/fire/mind altars, matched against that file's own
  `~runecraft_type_for_loc`). The Slug Prince (level 62, melee-only per
  quest-helper's own getCombatRequirements(), no special-defence-mechanic
  precedent anywhere in this tree) has no `.spawn` entry anywhere (confirmed
  via grep) -- hand-spawned lazily on trigger once all five runes are used,
  same idiom as Royal Trouble's Giant Sea Snake / Contact's Giant Scarab.
  Deferred/simplified (documented, no established precedent anywhere in this
  tree for the alternative): the real widget puzzle for combining the three
  page fragments (interface group 460, native `slug2_frag1/2/3_xpos/ypos/
  zpos/rot` drag-position varps) collapses to using sea slug glue directly
  on a repaired fragment, same "no puzzle-piece-dragging interface
  precedent" reasoning as every prior slice's own widget soft-skips; the
  three background Witchaven villagers (their own native stage1/stage2
  multi-npc wrappers) are pure flavour never referenced by any
  quest-helper `steps.put` or requirement, deferred with no gameplay
  consequence, same reasoning as the native but code-unreferenced
  `slug2_scan_mayor`/`slug2_savant_gotinfo`/`slug2_savant_scan`/
  `slug2_doorscan`/`slug2_queen_door`/`slug2_door_sound_control`/
  `slug2_oniall_control` bits (left unset); loc visuals for the imposing
  door's open/closed state don't swap (wrapper-only limitation, same as
  every prior slice's npc/loc side). Rewards: 1 QP, 3500 Crafting/
  Runecraft/Thieving XP each (dbrow `stat_xp_awarded` matches quest-helper's
  own `getExperienceRewards()` exactly, both awarded), unlocks purchasing
  Proselyte armour. `mingw32-make -C src sscompile` clean (only pre-existing
  snprintf-truncation warnings in the compiler itself); `mingw32-make -C src
  mock230-scripts` exit 0, 14316 scripts compiled (14281 -> 14316, net +35
  after the duplicate-trigger fix removed one competing top-level
  `[opnpc1,holgartlandtravel]`); grep of the full build log for "slug" /
  "holgart" (case-insensitive) returned zero hits both before and after the
  fix -- no warnings or errors attributable to this slice or any of the five
  shared-trigger merges. `mock230_pack --check-only` not runnable in this
  worktree (no `cache.osrs239` present, same pre-existing environment gap
  every prior slice has noted). `::theslugmenace` / `::theslugmenacerun`
  debug commands added, matching every prior slice's idiom. Wiki
  https://oldschool.runescape.wiki/w/The_Slug_Menace/Quick_guide (dialogue
  paraphrased, not verbatim, per copyright, same caveat as every prior
  slice). Files: `quests/quest_theslugmenace/{configs/theslugmenace.
  {constant,varp,npc,loc}, scripts/slugmenace_{tiffy,witchaven,pages,
  journal,debug}.rs2}` + merges into `quest_wanted/scripts/wanted_tiffy_
  amik.rs2`, `quest_makinghistory/scripts/makinghistory_jorral.rs2`,
  `areas/area_fishing_platform/scripts/bailey.rs2`, `areas/area_fishing_
  platform/scripts/holgart.rs2`, `skill_crafting/scripts/gem/uncut_gem.rs2`,
  `skill_runecraft/scripts/runecraft.rs2`,
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #130 Cabin Fever, 704 lines.
- slice done (2026-08-11): Cabin Fever (#130) -- Bill Teach's raid on a rival
  pirate ship; native dbrow `quest_cabinfever` (id 104, endstate 140) +
  native varbit schema on `fever_quest`/`fever_cannon_var`/`fever_extra_var`/
  `fever_storage_var` reused as-is, matching quest-helper's own VarbitID
  names exactly. Real prereqs are Pirate's Treasure + Rum Deal (both hard-
  gated, both genuinely completable) + Priest in Peril, which is soft-
  skipped -- `quest_priestperil.constant` documents its own finale as
  "deferred (blocked)" and `%priestperil` never reaches completion anywhere
  in this tree, so hard-gating on it would make Cabin Fever unstartable.
  Native multiloc records confirm every real breakpoint used (hole repair,
  loot containers, cannon repair/load, the enemy hull-breach counter
  `fever_holes_in_the_hull`, the sabotage barrel's non-monotonic fused(2)->
  exploded(1) order). All inter-deck navigation is already the generic climb
  system (`ladders_stairs/scripts/ladders.rs2`) -- zero custom transport
  scripting needed; the ship-to-ship rope swing is one `distance(coord,...)`
  teleport trigger reused by every crossing. `fever_teach`/`fever_port_ship_
  teach`/`fever_quest_ship_teach` needed an op overlay (`cabinfever.npc`);
  every multiloc wrapper used already carried its own real op, no loc
  overlay needed. Simplified (documented, no precedent anywhere in this tree
  for the alternative): locker searches grant a full requirement in one
  Search; plunder containers grant a fixed 4+3+3=10 split; the canister-kill
  phase has no cannon-deals-damage-directly precedent (same as Royal
  Trouble's Giant Sea Snake / GBR's Barrelchest leaving combat to the
  generic system) so a load/fire cycle itself stands in for a kill, tracked
  via `%fever_quest` sub-values rather than combat; misfire/wrong-ammo
  handling isn't modelled. Wiki https://oldschool.runescape.wiki/w/
  Cabin_Fever/Quick_guide + quest-helper source fetched via GitHub raw
  (dialogue paraphrased, per copyright). `mingw32-make -C src sscompile`
  clean, `mingw32-make -C src mock230-scripts` exit 0 (14,342 scripts, up
  from 14,316); no duplicate-trigger or duplicate-constant collisions
  (checked by hand against the whole tree). Files: `quests/quest_cabinfever/
  {configs/cabinfever.{constant,varp,npc}, scripts/cabinfever_{shared,bill,
  transport,lockers,repair,sabotage,loot,cannon,journal}.rs2}` + wiring into
  `interface_questjournal/scripts/quest_journal.rs2`. This was previously a
  soft-skipped prerequisite for The Great Brain Robbery (#121) -- that gate
  can now be tightened by a future tick. Next pending row (smallest-first):
  #132 In Aid of the Myreque, 710 lines (#131 Icthlarin's Little Helper was
  already `done (LC)`, table-sync only).
- slice done (2026-08-11): In Aid of the Myreque (#132) -- Veliaf sends the
  player to help the Myreque's cousin cell repair Burgh de Rott, fight off
  Gadderanks's vampyre blood-tithe raid, escort Ivan Strom to Paterdomus via
  Temple Trekking, and craft + bless the Rod of Ivandis. Native dbrow
  `quest_inaidofthemyreque` (id 107, endstate 430, questpoints 2,
  requirement_stats (12,25)=Crafting25/(14,15)=Mining15/(6,7)=Magic7,
  stat_xp_awarded Attack/Strength/Crafting/Defence 2000xp each -- both match
  quest-helper's own getGeneralRequirements()/getExperienceRewards() exactly)
  + a fully native varbit schema on basevars `myreque_2_main_var` (per-site
  repair progress), `myreque2_multivar` (`myreque_2_quest` master progress
  0-511 at bits 0-8, matching quest-helper's own steps.put range; crate
  sub-fields; blood-tithe chat flags; `juvinate_deaths`; `juvinate_ambush_
  deaths`/`_routetaken`) and `myreque2_extravar` -- reused as-is, matching
  quest-helper's own `VarbitID.*` names lowercased exactly, claimed bare in
  `myreque2.varp` same as `quest_cabinfever`'s own `fever_quest` precedent.
  `tools/questhelper_extract.py inaidofthemyreque --check` (staged locally
  from a `curl`-fetched copy of the QH source, no local checkout on this
  machine) resolved every gameval clean, zero unresolved names. dbrow
  `requirement_quests` decodes to dbrow id 79 = Desert Treasure I -- not a
  real prerequisite, same known cache-decode-corruption failure mode this
  queue warns about repeatedly. The real prerequisite (In Search of the
  Myreque FINISHED, `%routequest >= ^routequest_complete`) is soft-skipped:
  auditing `quest_routequest/` (row #65, marked `done (LC)`) found it only
  has `configs/quest_routequest.{constant,varp}` +
  `scripts/routequest_journal.rs2` -- grepping the whole tree for
  `%routequest` finds only the journal reading it, nothing ever writes it,
  and Veliaf/Ivan/Polmafi's hideout npcs have no scripted dialogue anywhere
  -- that quest is not actually completable in this tree, so hard-gating on
  it would make In Aid of the Myreque itself permanently unstartable (same
  reasoning as Cabin Fever's Priest in Peril / King's Ransom's One Small
  Favour); row #65 was annotated with this finding, not re-scored (out of
  scope for this slice). The Crafting 25 / Mining 15 / Magic 7 stat gate is
  still hard-checked (`myreque2_meets_requirements`), matching the dbrow.
  Native multiloc/multinpc records independently confirm every real
  breakpoint used: `burgh_furnace_multiloc` broken(0)->repaired(1)->
  coal_loaded(2)->fired(3) on `burgh_furnace_fix`; `burgh_general_store_
  roof_multiloc`/`_wall_multiloc` and `burgh_bank_wall_multiloc`/
  `burgh_bank_booth_multiloc` each 0/1 on their own varbit;
  `burgh_gadderanks_multinpc` invisible(0)->visible(nonzero) on
  `blood_tithe_visible`, matching `veliafReturnedToBase =
  VarbitRequirement(BLOOD_TITHE_VISIBLE, 3, GREATER_EQUAL)` exactly;
  `burgh_temple_trapdoor_multiloc`/`burgh_ivandis_tombdoor_board_multiloc`
  0/nonzero on `burgh_temple_trapdoor`/`ivandis_tomb_boards`, matching
  `libraryOpen`/`boardsRemoved`. Click-based repair triggers bind to the
  currently-resolved multiloc *leaf* (native `op1=Inspect` on every leaf,
  additive `op2=Repair`/`Add-coal`/`Light` overlay in `myreque2.loc`) rather
  than the wrapper, matching the cache's own `burgh_inn_trapdoor_closed`/
  `_open` pair (consumed by name in `ladders_stairs/scripts/climb_shared.
  rs2` and `doors/scripts/doors.rs2`) -- the opposite of Cabin Fever's own
  wrapper-level dispatch for its script-spawned ship instance, reasoned out
  from `~climb` turning out to be a pure "move one plane, same tile" op with
  no per-object destination lookup (`ladders_stairs/scripts/ladders.rs2`).
  `burgh_boared_up_wall_clickzone` is the same leaf resolved by both the
  shop-wall and bank-wall placements; one trigger branches on `coord`
  against `myreque2_shop_wall_coord`/`myreque2_bank_wall_coord` to update
  the right native varbit -- no wrapper ambiguity since the two multiloc
  *wrapper* records are themselves distinct, checked directly by grep before
  writing. `priestperiltrappedmonk_vis` (Drezel here) is confirmed a
  different gameval from Priest in Peril's own `priestperiltrappedmonk`
  (`trapped_drezel.rs2`) -- no collision. `myq5_veliaf_child` (the finish
  hand-in) is already claimed by Sins of the Father's own hub dispatcher
  (`sinsofthefather.rs2`'s `[opnpc1,myq5_veliaf_child]`, stacked with three
  sibling names) -- merged as an early branch in `sf_veliaf_talk` gated on
  `%myreque_2_quest`'s own delivery window, not a duplicate trigger.
  Combat kill-credit (Gadderanks + 2 Juvinates, then Ivan's escort ambush)
  uses hand-spawned attackable npcs (no `.spawn` entry for any of the five
  attackable/ambush variants, confirmed via grep) with `[opnpcN,name]
  ~npc_retaliate(0);` + `[ai_queue3,name] ...; ~npc_default_death;`, same
  idiom as Contact!'s Giant Scarab -- `juvinate_deaths` (native, 0-3) drives
  `defeatedGadderanks` exactly, and the escort's `juvinate_ambush_deaths`
  (native, 0-3) is driven to 2 (short-route simplification, see below).
  Simplifications (documented, no established precedent anywhere in this
  tree for the alternative): the basement rubble minigame (mine, bag with a
  bucket, empty outside, up to 5 trips) collapses to one pickaxe+spade
  click driving the real `burgh_inn_rubble_pile` counter to its cap, same
  "grant a full requirement in one action" convention as Cabin Fever's
  locker searches. The portable general-store crate item + its per-item
  fill loop (quest-helper's own `FillBurghCrate`) collapses to one hand-in
  to Aurel with everything in inventory at once, still driving the real
  native `burgh_axes_crate`/`burgh_food_crate`/`burgh_tinderbox_crate` to
  their caps (`burgh_crate_overseer` reaches 938 either way, matching
  `filledCrate` exactly). Ivan's Temple Trek escort simplifies to the short
  route only (2 level-75 Juvinates, matching the native 2-bit ambush
  counter) -- the long-route alternative (4 level-50) is deferred flavour,
  geography already baked. Vampyre silver-weapon immunity is hinted via
  dialogue only, combat left to the generic system -- no restriction
  precedent anywhere in `skill_combat` (grep-verified), same reasoning as
  Cabin Fever. The Lvl-1 Enchant cast on the Silvthrill rod does not route
  through the shared `magic_spell_table` dbrow (`skill_magic/configs/
  magic_spells.dbrow`'s `[magic_spell_enchant_level1]` `convertobj` list) --
  no established precedent anywhere in this tree for additively appending a
  `data=convertobj,...` row to an existing dbrow block from a second file
  (unlike the proven additive-field-merge convention for `.npc`/`.loc`
  overlays), so it was avoided; using cosmic + water runes directly on the
  rod (own dedicated trigger, Magic 7 checked directly) reproduces the same
  item transformation without touching shared spellbook content. `burgh_
  rod_clay` -> Silvthrill rod smelting is one new `case` added to the
  existing shared `[label,use_furnace]` switch in `skill_smithing/scripts/
  smelting/smelting.rs2` (quest-helper's own text is literally "at any
  furnace") -- merged as a branch, not duplicated. `pipeastsidetrapdoor`/
  `pipeastsidetrapdoor_open` (the way down to Drezel) are deliberately left
  untouched: a real generic climb binding already exists
  (`climb_shared.rs2`) and a second, seemingly-dead stub trigger for the
  same two names already exists in `quest_sinsofthefather/scripts/
  sinsofthefather.rs2` (itself soft-skipped) -- a pre-existing duplicate-
  trigger situation from before this slice, documented and not touched.
  Rewards: 2 QP, 2000 Attack/Strength/Crafting/Defence XP each (matches
  dbrow exactly), Temple Trekking unlock, Rod of Ivandis crafting ability.
  Wiki https://oldschool.runescape.wiki/w/In_Aid_of_the_Myreque/Quick_guide
  + quest-helper source fetched via GitHub raw (dialogue paraphrased, not
  verbatim, per copyright, same caveat as every prior slice). `mingw32-make
  -C src sscompile` clean (only pre-existing snprintf-truncation warnings in
  the compiler itself); `mingw32-make -C src mock230-scripts` exit 0, 14400
  scripts compiled (up from 14342); grep of the full build log for
  "myreque"/"inaidofthemyreque" returned zero hits, and a full self-sweep of
  all 58 trigger headers this slice authored against the rest of the tree
  found zero collisions. Files: `quests/quest_inaidofthemyreque/{configs/
  myreque2.{constant,varp,loc}, scripts/myreque2_{shared,hideout,burgh,shop,
  bank,furnace,fight,trek,rod,journal}.rs2}` + merges into `quest_
  sinsofthefather/scripts/sinsofthefather.rs2` and `skill_smithing/scripts/
  smelting/smelting.rs2`, plus wiring into `interface_questjournal/scripts/
  quest_journal.rs2`. Next pending row (smallest-first): #133 Between a
  Rock..., 716 lines.
- slice done (2026-08-11): Between a Rock... (#133) -- Dondakan the Dwarf
  is cannon-firing a wall in the Keldagrim south-west mine, convinced a
  lost realm lies behind it; the player fetches dwarven lore (3 torn
  pages -- scorpion kill, mine-cart search, low-grade rock mining) from
  Rolad, arms Dondakan with a golden cannonball to crack the rock,
  gathers four schematic fragments (Dondakan, the book's last page, the
  Dwarven Engineer, Khorvak) to solve the sealing mechanism, smiths a
  golden helmet, and is fired through into the hidden Arzinian realm to
  mine gold ore and defeat an Avatar guardian. Native dbrow `quest_
  betweenarock` (id 76, endstate 110, questpoints 2, requirement_stats
  (13,50)=Smithing50/(14,40)=Mining40/(1,30)=Defence30, all
  `requirements_boostable`=1, stat_xp_awarded Defence/Mining/Smithing
  5000xp each -- matches quest-helper's own getGeneralRequirements()/
  getExperienceRewards() exactly) + a fully native varbit schema on
  basevar `dwarfrock_main` (`dwarfrock_quest` master 0-255,
  `dwarfrock_gold_cannonball`, `dwarfrock_fired_gold_cannonball`,
  `dwarfrock_schematics_solved`, plus dialogue-tracking flags
  `dwarfrock_ferryman1_beenbefore`/`_ferryman2_beenbefore`/
  `_gold_boatman_met`/`_met_engineer`/`_rolad_schematics_heardof`/
  `_rolad_schematics_lookingfor`/`_dondakan_inside_heardof`/
  `_brothers_introduced`/`_brothers_toldvictory`/`_inside_visited`/
  `_inside_timeleft`) reused as-is, matching quest-helper's own
  `VarbitID.DWARFROCK_*` names exactly, claimed bare in
  `betweenarock.varp` same as `quest_cabinfever`'s own `fever_quest`
  precedent. `%dwarfrock_quest`'s exact ten-step breakpoints (0/10/.../
  100) matching quest-helper's own `steps.put` keys are independently
  confirmed (cache wins, not guessed) by the cache's own native
  `dwarfrock_multi_dondakan` multivarbit npc record: Dondakan
  (`dwarfrock_dondakan`) only renders at exact multiples of ten and
  swaps to `dwarfrock_dondakan_noaxe` from value 110 on (matching the
  dbrow's own `endstate`); `dwarfrock_multi_gold_boatman` likewise only
  resolves to `dwarfrock_gold_boatman` from 110 on -- both used directly,
  a stage-110 "quest complete" state added past the last `steps.put` key
  to match. `tools/questhelper_extract.py`-equivalent manual gameval
  resolution (staged locally from a `curl`-fetched copy of the QH source
  via `raw.githubusercontent.com/Zoinkwiz/quest-helper`, no local
  checkout on this machine) found every `NpcID`/`ObjectID`/`ItemID`/
  `VarbitID` name used by `BetweenARock.java`/`PuzzleStep.java` resolves
  clean in the osrs239 pack, zero unresolved names.
  Dbrow `requirement_quests` decodes to dbrow ids 35 (`quest_
  sheepherder`) and 52 (`miniquest_magearena1`) -- neither a real
  prerequisite, same known cache-decode-corruption failure mode this
  queue warns about repeatedly. The real prerequisites per quest-
  helper's own `getGeneralRequirements()` are Dwarf Cannon FINISHED and
  Fishing Contest FINISHED. Fishing Contest (`quest_fishingcompo`) is
  genuinely completable -- grep confirms it writes `%fishingcompo =
  ^fishingcompo_complete` and calls `~quest_complete(quest_
  fishingcontest)` from its own `quest_fishingcompo.rs2` -- and is hard-
  gated. **Dwarf Cannon is NOT genuinely completable in this tree despite
  being listed "IN-LC" with six separate "done" `CONTENT_PORT_QUEUE.md`
  log lines (26f/26v/26y/27c/31c/32h/33f)** -- auditing `quest_mcannon/`
  for this slice (per this queue's standing instruction to spot-check
  prerequisites rather than trust a `done` label) found every one of
  those slices real (railings, doors, ladder, cave guard, crate child,
  journal, cannonball smelting all genuinely scripted), but grepping
  every `%mcannon =` assignment site in the whole tree shows the master
  varp never advances 0->1 (`^mcannon_tasked_with_fixing_railings`), 8->9
  (`^mcannon_tasked_with_speaking_to_nulodion`), or 10->11
  (`^mcannon_complete`) anywhere -- the "Dwarf Commander" who assigns/
  advances/finishes the quest, and Nulodion's own talk dialogue (only his
  *item* mesbox exists, `nulodions_notes.rs2`), have no dialogue script
  anywhere in this tree. `%mcannon` is permanently stuck at 0 in this
  tree, so Dwarf Cannon cannot actually be started or completed --
  hard-gating on it would make Between a Rock... itself permanently
  unstartable. Soft-skipped instead, same convention as Cabin Fever's own
  Priest in Peril / In Aid of the Myreque's own In Search of the Myreque
  (reasoned out in `betweenarock_shared.rs2`); flagged with a matching
  note on `CONTENT_PORT_QUEUE.md` next to the mcannon log lines rather
  than re-scoring those six rows (out of scope for this slice -- they are
  each individually real, the gap is the missing quest-giver dialogue,
  not those slices). The Smithing 50 gate (golden helmet) and Defence 30
  gate (before Dondakan fires the player through) are hard-checked at
  their own action points, matching `requirement_check_skills_on_start`
  =0 -- not a single quest-start blanket check.
  Simplifications (documented, no established precedent anywhere in this
  tree for the alternative): quest-helper's own `PuzzleStep` drives a
  real native drag/rotate widget puzzle (`interfaces/dwarf_rock_
  schematics.if` + `_control.if`, genuine rev-230 interfaces) with exact
  pixel-position and rotation-id matching for three pieces -- no generic
  engine mechanic anywhere in this tree scripts exact widget position/
  rotation manipulation (grep-verified), so assembling the four
  schematic fragments collapses to one Use action once all four are
  held, same "grant a full requirement in one action" convention as
  Cabin Fever's locker searches / The Great Brain Robbery's crate build.
  The three torn book pages likewise auto-combine into `dwarf_rock_
  pagex3` the instant all three are held, skipping the intermediate
  `dwarf_rock_pagex2` bundle and any manual item-on-item combine step.
  The "keep gold ore in your inventory to stop the Avatar regenerating"
  consumable anti-regen mechanic isn't modelled -- 6 gold ore is checked
  once at the start of the confrontation, same as every other hand-
  spawned boss in this tree leaving the fight itself to the generic
  combat system; the 15-ore "weaker Avatar" route is recognised in
  dialogue only, since no level-75-vs-125 pair exists among the nine
  native colour/style Avatar variants. The realm's real 8-minute time
  limit (`dwarfrock_inside_timeleft`, a genuine native 10-bit field) is
  enforced with a coarser 30-second-per-decrement `softtimer` (16
  decrements) rather than chasing an unverifiable exact tick rate, same
  "approximate, not narrated, but real and enforced" reasoning as other
  slices' hand-picked travel coordinates -- same `softtimer`/
  `clearsofttimer` idiom as `minigame_barrows/scripts/barrows_
  tunnel.rs2`'s own prayer-drain timer. Avatar hand-spawned on trigger
  (no `.spawn` entry for any of the nine colour/style variants anywhere
  in the tree, confirmed via grep), same idiom as Royal Trouble's Giant
  Sea Snake / The Great Brain Robbery's Barrelchest / In Aid of the
  Myreque's Gadderanks. Avatar combat style (mage/archer/warrior) is
  selected by the player's own highest combat stat per the wiki's own
  "counters your strongest style" description; the green/yellow recolour
  is cosmetic RNG only, no established per-kill difficulty-scaling
  precedent anywhere in `skill_combat`. Golden cannonball smelting merges
  into the shared furnace switch (`skill_smithing/scripts/smelting/
  smelting.rs2`'s own `case gold_bar, perfect_gold_bar :`, previously
  `@craft_gold_menu` unconditionally) behind a new `@dwarfrock_gold_bar_
  or_menu` label that falls through to the unmodified jewellery menu for
  every other case, same additive idiom as In Aid of the Myreque's own
  `burgh_rod_clay` case; page 3 similarly hooks both successful-mine
  branches in `skill_mining/scripts/mining.rs2` (a no-op outside this
  quest's own Dwarven Mine page-collecting step). The two Troll
  Stronghold <-> Keldagrim tunnels (`trollromance_stronghold_exit_
  tunnel`, `dwarf_cavewall_tunnel`) and both Dwarven Ferryman crossings
  have zero pre-existing script references anywhere in the tree (grep-
  confirmed) -- this quest's own unique route, not shared with any other
  content; `fai_dwarf_trapdoor_down`/`ladder_from_cellar_directional`/
  `tunnelstairstop` (Dwarven Mine, Rolad's ladder, Khorvak's stairs) are
  already generic `category=climb_down`/`climb_up` records
  (`ladders_stairs/configs/ladders.loc`), needing zero custom transport
  scripting, same "no per-object destination lookup" reasoning as
  `~climb` itself. `goldrock1`/`goldrock2` inside the realm are the same
  generic gold rock the rest of the game already mines (`skill_mining/
  configs/rocks.loc`) -- no dedicated realm-only ore loc exists.
  Rewards: 2 QP, 5000 Defence/Mining/Smithing XP each (matches dbrow
  exactly), a Rune pickaxe, and functional access to the Arzinian realm
  (Ring of Wealth teleport menu entry deferred -- no established
  precedent anywhere in this tree for adding a teleport destination to a
  jewellery item's menu). Wiki https://oldschool.runescape.wiki/w/
  Between_a_Rock.../Quick_guide + quest-helper source fetched via GitHub
  raw (dialogue paraphrased, not verbatim, per copyright, same caveat as
  every prior slice). `mingw32-make -C src sscompile` clean (only pre-
  existing snprintf-truncation warnings in the compiler itself);
  `mingw32-make -C src mock230-scripts` exit 0, 14453 scripts compiled
  (up from 14400); grep of the full build log for "betweenarock"/
  "dwarfrock" returned zero hits, and a full self-sweep of all 53
  trigger headers this slice authored against the rest of the tree found
  zero collisions. Files: `quests/quest_betweenarock/{configs/
  betweenarock.{constant,varp}, scripts/betweenarock_{shared,travel,
  dondakan,pages,schematics,realm,journal}.rs2}` + merges into
  `skill_smithing/scripts/smelting/smelting.rs2`,
  `skill_mining/scripts/mining.rs2`, and wiring into
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #134 Ratcatchers, 737 lines.
- slice 134 done: Ratcatchers -- Gertrude sends the player to four retired
  ratcatchers (Jimmy Dazzler, Hooknosed Jack, Smokin' Joe, The Face/Felkrash)
  to train a cat, culminating in a King Rat fight and charming the Port Sarim
  Rat Pits with a snake charm. Grep-verified first (methodology steps 1-2): no
  LC proc (`lc_quests.txt` clean, no `quest_ratcatchers` dir), no 2009scape
  impl; genuinely pending. Fetched `Zoinkwiz/quest-helper`'s actual filename
  `RatCatchers.java` (capital C) + its companion `RatCharming.java` (540+197
  =737 lines, matching this row's own line count exactly) via GitHub's tree
  API after a direct raw-githubusercontent guess 404'd. Native dbrow
  `quest_ratcatchers` (id 99, endstate 127, questpoints 2, stat_xp_awarded
  thieving 45000 raw=4500xp, matching quest-helper's own
  ExperienceReward/QuestPointReward exactly). dbrow `requirement_quests`=75
  resolves to neither Gertrude's Cat (id 50) nor Icthlarin's Little Helper (id
  80) -- same known-corrupt column already flagged for row #109/#130; real
  prereqs are quest-helper's own getGeneralRequirements(): Icthlarin's Little
  Helper FINISHED (`%ics_little_var >= ^ics_complete`, verified genuinely
  reachable -- `icthlarin_pyramid.rs2:142` actually writes it, unlike the
  ISOTR/Dwarf Cannon false-`done` traps this queue warns about) plus The Giant
  Dwarf STARTED (`%giantdwarf_quest >= 1`). Icthlarin's Little Helper's own
  prereq is Gertrude's Cat, so gating on ICS transitively covers it; Gertrude's
  Cat itself independently verified real too (`quest_fluffs/scripts/
  quest_fluffs.rs2`'s own `~quest_complete(quest_gertrudescat)` write, npc
  `gertrudescat`, real dialogue -- not a stub). Native varbit schema on
  basevars `main_ratcatch_var` (`%ratcatch_var`, 8 bits 0-255) and
  `ratcatch_var_multi` (`%vc_raton_off1..6`) reused as-is, matching
  quest-helper's own VarbitID names exactly; `%ratcatch_var` breakpoints are
  quest-helper's own `steps.put` keys where no native sub-field already
  tracks the same ground, private intermediate values elsewhere (sewer-rat
  count, "directions read") in the same unclaimed range, same convention as
  `betweenarock`'s own private sub-values alongside its native bits. Native
  multi-npc records independently confirm the mansion's 6 rats: six
  `vc_partyrat_multi1..6` wrappers, each keyed on its own `vc_raton_off1..6`
  bit and world-spawned at exact coordinates in `m44_79.spawn` -- resolved to
  `vc_party_rat` and disambiguated by `npc_coord`, same idiom as
  `quest_fluffs`'s own `npc_coord = %fluffs_crate` check; **zero hand-spawning
  needed for the mansion rats**. The Varrock Sewer's 8 rats have no such
  native wrapper (`pitrat_sarim_def`, the id quest-helper names, has zero
  world spawns anywhere near the sewer -- it only lives in the Port Sarim rat
  pit map) -- the already-world-baked generic `rat` npc (dozens of instances
  in `m50_154.spawn`, right where Phingspet stands) stands in for it instead,
  again with **zero hand-spawning**. All items/npcs/locs resolved natively
  (gertrude_post/vc_phingspet/pitrat_sarim_def/vc_jimmy_dazzler/vc_party_rat/
  vc_hooknosed_jack/apothecary/vc_smokin_joe/vc_felkrash_the_bard/vc_face,
  ratcatchers_rathole1-5/vc_blank_walldecor/vc_trellis_base/vc_manhole_open/
  vc_ladder/fai_varrock_ladder(top)/feud_money_bowl, rat_poison/
  ratcatchers_poisonedcheese/ratcatchers_weedpot/ratcatchers_smokey_weedpot/
  ratcatchers_cat_antipoison/ratcatchers_party_directions/snake_flute/
  ratcatchers_music/vc_rat_pole -- every single one already declared, none
  invented). This tree has no follower/pet system at all (`quest_fluffs`'s
  own documented deferral), so "a cat is following you" and "a catspeak
  amulet is equipped" are modelled as inventory/worn checks over the same
  kitten/grown-cat item enumeration `gertrude.rs2`'s own `fluffs_has_pet_cat`
  already established, restricted to non-overgrown. **Two simplifications,
  both documented with "no established precedent" same as prior slices**: (1)
  the King Rat fight (`vc_blank_walldecor`, use cat + 8 fish) is one
  deterministic action, same "one action, no scripted pet-vs-monster combat
  loop" convention as `betweenarock`'s own Avatar fight; (2) the snake-charm
  8-note tune minigame (native rev-230 `interfaces/ratcatcher_flute.if`,
  interface 282, `cs1script1`-driven note buttons) collapses to one action
  once the snake charm + music scroll are held and the player is outside the
  Port Sarim manhole -- grep-confirmed **zero** `[if_button,...]` triggers
  anywhere in the whole tree drive any cs1script-heavy widget server-side,
  same precedent as `betweenarock`'s own schematics puzzle deferral AND
  `quest_death`'s own `death_dice` deferral (both also native rev-230
  interfaces left unwired for the identical reason). **Critical correctness
  catches this slice hit**: the Port Sarim Rat Pits sit on the same +6400
  world-Z underground map-sheet offset as Varrock Sewer, so the manhole/ladder
  there needed `general_use/scripts/manholes.rs2`'s own `p_telejump`+
  `movecoord(coord,0,0,6400)` idiom, NOT the simple `~climb` plane-delta proc
  (which blocks below plane 0 and would have silently no-opped); caught before
  building by checking the zone's own world-Z band, not by trial and error.
  Also hit **four real pre-existing trigger collisions** merged in rather than
  duplicated (grep-first, every one): `[opnpc1,gertrude_post]`
  (`quest_atailoftwocats/scripts/twocats.rs2`, gated so it only steals the
  turn from A Tail of Two Cats for players who are actually eligible to start
  or already mid/post Ratcatchers -- never for someone who simply hasn't met
  its prereqs); `[opheldu,pot_empty]` (`quest_swansong/scripts/
  swansong_army.rs2`, pot-of-weeds branch added ahead of its airtight-pot
  logic); `[opnpc1,apothecary]` (`areas/varrock/scripts/apothecary.rs2`,
  branch added ahead of the My Arm's Big Adventure/Between a Rock/romeojuliet
  chain already there) -- while checking this one, found a **pre-existing,
  unrelated latent duplicate**: `quest_atailoftwocats/scripts/twocats.rs2`
  *also* independently declares its own `[opnpc1,apothecary]` (line 313),
  meaning one of the two already silently shadows the other for real players.
  Not this quest's file and not caused by this slice -- left alone, flagged
  here for a future tick to actually fix. `mingw32-make -C src sscompile`
  clean (only pre-existing snprintf-truncation warnings in the compiler
  itself); `mingw32-make -C src mock230-scripts` exit 0, 14486 scripts
  compiled (up from 14453, +33 -- exactly matching this slice's own
  27+5+1=33 authored script/proc blocks); a full self-sweep of every trigger
  header this slice authored (21 opnpc/oploc/opheld/oplocu triggers) against
  the rest of the tree found zero collisions beyond the four merged above.
  Deferred, documented: full Rat Pits minigame content (doesn't exist as a
  tree anywhere in this repo -- a separate, much larger slice, matching this
  queue's own "never park sibling content" boundary in reverse: not stealing
  a whole minigame's scope into one quest slice); training overgrown cats
  into wily/lazy cats (same pet.rs2 deferral as Gertrude's Cat); Ring of
  Charos(a) snake-charmer price discount (no favour-item price-override
  precedent on an unrelated quest's NPC); DS2's own separate catspeak unlock
  path (no native item/varbit for it anywhere, same TODO quest-helper itself
  leaves). Wiki `oldschool.runescape.wiki/w/Ratcatchers` +
  `.../Quick_guide` + `Transcript:Ratcatchers` (dialogue paraphrased, not
  verbatim, per copyright, same caveat as every prior slice). Files:
  `quests/quest_ratcatchers/{configs/ratcatchers.{constant,varp},
  scripts/ratcatchers_{shared,journal}.rs2, scripts/ratcatchers.rs2}` +
  merges into `quest_atailoftwocats/scripts/twocats.rs2`,
  `quest_swansong/scripts/swansong_army.rs2`,
  `areas/varrock/scripts/apothecary.rs2`, and wiring into
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #135 Dream Mentor, 745 lines.
- slice 135 BLOCKED (not ported): Dream Mentor -- grep-verified first
  (methodology steps 1-2, including `lc_quests.txt`): no LC proc, no
  2009scape impl, genuinely otherwise-pending. Fetched `Zoinkwiz/quest-
  helper`'s `DreamMentor.java` (441 lines) + `CyrisusArmourSet.java`/
  `CyrisusBankConditional.java`/`CyrisusBankItem.java`/
  `SelectingCombatGear.java` (101+39+62+102=304, summing to 745, matching
  this row's own line count exactly). Native dbrow `quest_dreammentor` (id
  134, endstate 28, questpoints 2, requirement_combat 85,
  stat_xp_awarded hitpoints 150000 raw=15000xp + magic 100000 raw=10000xp,
  matches wiki exactly) + a genuine native varbit schema on basevars
  `dream_prog`/`dream_combattype`/`dream_health`/`dream_armament`/
  `dream_cutscene_seen`/etc, matching quest-helper's own VarbitID names
  exactly -- the port itself would have been straightforward. dbrow
  `requirement_quests`=88,36 decodes to Forgettable Tale / Plague City --
  same known-corrupt column this queue repeatedly flags -- but unlike
  every prior corrupt-dbrow case, quest-helper's own real
  getGeneralRequirements() here is Combat 85 (fine, a stat check) +
  **Lunar Diplomacy FINISHED** + Eadgar's Ruse FINISHED. Eadgar's Ruse is
  genuinely real and reachable (`quest_eadgar`, already implemented, IN-LC
  list). Lunar Diplomacy is NOT a corrupt-dbrow artifact to soft-skip --
  it is itself still `pending` on this exact queue, row #169, at 1,756
  lines, and grep of the whole tree for any `lunar*` script file, any
  `lunar_oneiromancer`/`lunar_pirate_captain` trigger, or any mainland<->
  Lunar Isle boat transport returned **zero hits**. Lunar Isle's own
  geography, npcs, bank booth (`[oploc2,lunar_moonclan_bankbooth]
  ~openbank;`) and mine ladders are all genuinely world-baked cache data
  (confirmed via `m32_60.spawn`/`m32_61.spawn` and
  `ladders_stairs/configs/ladders.loc`'s own `lunar_mine_slanty_ladder_*`/
  `lunar_moonclan_ladder`), same "no per-object destination lookup"
  pattern as everywhere else -- but the *only* way there in real OSRS,
  Lunar Diplomacy's own opening boat trip (`lunar_pirate_captain`,
  `lunar_captains_parrot`), has no scripted op anywhere in this tree.
  This is categorically different from this queue's prior soft-skipped
  prerequisites (Priest in Peril for Cabin Fever, In Search of the
  Myreque for In Aid of the Myreque): those were single unwritten gate
  variables on quests whose own content stood alone regardless. Dream
  Mentor's entire setting (Lunar Isle, the Oneiromancer, the dream-vial/
  brazier ritual, the "7 new Lunar spells" reward) is physically
  unreachable and thematically meaningless without Lunar Diplomacy having
  run first -- porting it now would mean either fabricating Lunar Isle
  transport wholesale (no native precedent to build from responsibly) or
  quietly stealing Lunar Diplomacy's own already-queued opening scope into
  this slice, both of which violate this queue's own "never park/steal
  sibling content" rule in reverse. Correctly left `pending` -> `blocked`
  rather than force-ported; re-queue once #169 Lunar Diplomacy lands. No
  files written, no build changes. Next pending row (smallest-first): #138
  Land of the Goblins, 760 lines (row #136 Watchtower and #137 Shadow of
  the Storm are already `done (LC)` stale-row fixes from an earlier tick).
- slice 138 done: Land of the Goblins -- Dorgeshuun Mines dweller Grubfoot's
  troubling dream sends the player (with Zanik) to infiltrate the Fishing
  Guild's goblin temple in disguise, free Zanik from its north-east cell,
  answer High Priest Bighead's loyalty quiz, thieve six enclave keys from
  six colour-coded priests, defeat five named skeleton high priests in the
  crypt beneath (Snothead/Snailfeet/Mosschin/Redeyes/Strongbones, each
  naming the next), and fix Oldak's fairy ring machine to reach Yu'biusk,
  the goblins' promised land. Grep-verified first (methodology steps 1-2,
  including `lc_quests.txt`): no LC proc, no 2009scape impl. Fetched
  `Zoinkwiz/quest-helper`'s `LandOfTheGoblins.java` (760 lines, matching
  this row's own line count exactly, single file). Native dbrow
  `quest_landofthegoblins` (id 166, endstate 56, questpoints 2,
  requirement_stats herblore48/thieving45/fishing40/agility38,
  stat_xp_awarded agility/fishing/thieving/herblore 80000 raw=8000xp each,
  matches quest-helper's own getExperienceRewards() exactly). dbrow
  `requirement_quests`=1,52 decodes to Cook's Assistant / Mage Arena I --
  same known-corrupt column this queue repeatedly flags -- real prereqs
  per quest-helper's own getGeneralRequirements() are Another Slice of
  H.A.M. FINISHED (`%slice_quest >= ^slice_complete`, already real,
  `quest_anothersliceofham/scripts/slice_sigmund.rs2` writes it) and
  Fishing Contest FINISHED (`%fishingcompo >= ^fishingcompo_complete`,
  already real, LC's own `quest_fishingcompo`), both independently
  confirmed genuinely reachable, plus the four hard skill gates above.
  Native varbit schema on basevars `lotg_base` (`%lotg`, 9 bits 0-511,
  breakpoints 0/2/4/.../52 matching quest-helper's own `steps.put` keys
  exactly, independently confirmed via the Java's own
  `VarbitRequirement(VarbitID.LOTG, 36, ...)`) and `lotg_base_2` reused
  as-is, matching quest-helper's own VarbitID names exactly; real
  sub-fields `%lotg_player_is_a_goblin` (disguise state),
  `%lotg_know_about_fish`, `%lotg_found_sphere`, `%lotg_machine_explained`,
  `%lotg_connectors_1/2/3` + `%lotg_fairy_ring_animating` (fairy ring
  puzzle) all driven directly rather than reinvented. All named npcs
  (`lotg_grubfoot`, `lotg_zanik` + its cutscene/yubiusk variants,
  `lotg_goblin_guard_black/white/yellow/darkblue/orange/purple`,
  `lotg_goblin_priest_<colour>_1op/2op`, `lotg_goblin_high_priest`,
  `lotg_goblin_skeleton_high_priest1..5` + `..._defeated` variants,
  `dorgesh_oldak_there`/`_1op`) and every loc/item (six enclave keys,
  Dorgesh-Kaan sphere, goblin mail in all six colours, `lotg_temple_
  huge_door`, five crypt graves, `lotg_bandos_sarcophagus`) are already
  natively declared -- none invented. **Zero hand-spawning needed for any
  of the geography**: the Goblin Cave, temple, crypt and Yu'biusk are all
  genuinely world-baked map data (confirmed via `configs/all.loc.compack`'s
  own `lotg_*` ids and the `m32_*`/`m38_*`-style world dressing), same
  "no per-object destination lookup" pattern as everywhere else in this
  tree -- only the five named skeleton priests are hand-spawned on trigger
  (zero `.spawn` entries anywhere, grep-confirmed), same idiom as
  `betweenarock`'s own Avatar. Travel between Dorgesh-Kaan's floors and its
  lower caves is already generic `category=climb_up`/`climb_down`
  (`ladders_stairs/configs/ladders.loc`'s own `dorgesh_1stairs`/
  `dorgesh_2stairs_posh`/`dorgesh_caves_ladder_down`) -- no custom
  transport scripting needed. The Goblin Cave itself is reached via the
  *already-existing* `[oploc1,mcannoncave]` (Dwarf Cannon's own real,
  IN-LC-listed `quest_mcannon`, confirmed genuinely implemented, not a
  false-`done` trap) -- confirmed its own `p_telejump` destination lands
  within a few tiles of quest-helper's own cited Goblin Cave coordinate,
  so this slice adds its own npcs inside that already-reachable
  underground zone rather than touching the trigger at all.
  **Five real pre-existing trigger collisions found and merged in rather
  than duplicated** (grep-first, every one, per this queue's own
  non-negotiable rule): (1) `[opnpc1,makeover_mage]`
  (`areas/falador/scripts/makeover_mage.rs2`) -- gated branch ahead of the
  generic cosmetic-makeover dialogue; (2) `[opnpc1,aggie]`
  (`areas/draynor/scripts/aggie.rs2`) -- gated branch ahead of the generic
  dye-shop dialogue for the whitefish/black-mail/white-mail-plus-four-dyes
  trade; (3) `[opnpc1,0_41_53_sinisterfishspot]`
  (`quest_fishingcompo/scripts/hemenster_fishing.rs2`) -- Fishing Contest's
  own competition logic silently no-ops outside an active competition, so
  the LOTG whitefish catch (disambiguated by slimy-eel bait) is checked
  first; (4) `[opheldu,toadflaxvial]`
  (`skill_herblore/scripts/brew_potion.rs2`) -- the generic herblore brew
  table, with a pharmakos-berry check ahead of it (not a real herblore
  recipe); (5) `[opheldu,golem_ink]` (`quest_golem/scripts/
  golem_portal.rs2`) -- quest-helper's own "Black dye" is this exact same
  `ItemID.GOLEM_INK`, and the entire black-mushroom-pick + pestle-and-
  mortar-grind pipeline (`[oploc1,golem_black_mushrooms]` +
  `[opheldu,golem_mushroom]`) already exists verbatim, shared with Shadow
  of the Storm's own Silverlight-dyeing step -- this slice adds only the
  "dye goblin mail" case to the existing switch, reusing the pickup/grind
  chain entirely unmodified. **Also found and fixed**: Goblin Diplomacy's
  own pre-existing `~dye_goblin_mail_armour` proc
  (`quest_gobdip/scripts/quest_gobdip.rs2`) only ever consumed the plain
  `goblin_armour` item, which would have silently failed for LOTG's own
  redye-in-place loop (colouring an already-coloured mail again) --
  extended in place to consume whichever mail colour is actually held,
  backward-compatible with Goblin Diplomacy's own always-plain-mail usage,
  and `skill_crafting/scripts/dye_cape.rs2`'s own `[opheldu,yellowdye]`/
  `[opheldu,bluedye]`/`[opheldu,orangedye]`/`[opheldu,purpledye]` switches
  extended with the missing goblin-mail cases (yellow and purple had none
  at all; blue and orange only matched plain mail). Simplifications
  (documented, no established precedent anywhere in this tree for the
  alternative, matching this queue's own repeated convention): (1) the
  "confirm to become a goblin" widget (native rev-230 interface 739,
  quest-helper's own text says "Your selection doesn't matter") collapses
  to an instant flag set on drinking the potion; (2) the fairy ring
  power-relay dial puzzle (native rev-230 interface 738, six increase/
  decrease buttons targeting exact values 9/4/1) collapses to one
  deterministic "fix the machine" action, same "no per-component widget
  click sequence" reasoning as `betweenarock`'s schematics puzzle /
  `quest_death`'s dice; (3) no follower/pet system exists in this tree
  (`quest_fluffs`'s own documented deferral) -- "Grubfoot/Zanik is
  following you" is an instant flag advance, not a literal companion NPC;
  (4) the optional "guess my goblin name" flavour exchange has no
  gameplay effect on progression per quest-helper itself, not modelled;
  (5) Yu'biusk (`InInstanceRequirement`) has no dynamic per-player
  instance precedent anywhere in this tree -- modelled as the real,
  shared, static map area already baked into the cache, same convention
  as `betweenarock`'s own Arzinian realm; (6) the five named skeleton high
  priests' unique special mechanics (stat-draining hits, summoned
  Skoblins) are left to the generic combat system, same reasoning as Royal
  Trouble's Giant Sea Snake; (7) High Priest Bighead's "true/false/false"
  quiz is one deterministic dialogue chain, no wrong-answer branch
  precedent anywhere in this tree's quest dialogue. Wiki
  `oldschool.runescape.wiki/w/Land_of_the_Goblins` +
  `.../Quick_guide` (dialogue paraphrased, not verbatim, per copyright,
  same caveat as every prior slice). `mingw32-make -C src sscompile`
  clean (only pre-existing snprintf-truncation warnings in the compiler
  itself); `mingw32-make -C src mock230-scripts` exit 0, 14,557 scripts
  compiled (up from 14,486, +71); full build log grepped for every
  touched/new filename (`lotg`, `landofthegoblins`, `quest_gobdip`,
  `dye_cape`, `golem_portal`, `brew_potion`, `hemenster_fishing`,
  `makeover_mage`, `aggie.rs2`) returned zero warnings or errors; a
  self-sweep of every trigger header this slice authored (grep batch
  covering every `opnpc`/`oploc`/`opheld`/`ai_queue` name) against the
  rest of the tree found zero collisions beyond the five merged above.
  Deferred: none identified beyond the documented simplifications above --
  every `steps.put` breakpoint is real and playable end-to-end. Files:
  `quests/quest_landofthegoblins/{configs/landofthegoblins.{constant,varp},
  scripts/lotg_{shared,intro,temple,keys,crypt,yubiusk,journal}.rs2}` +
  merges into `areas/falador/scripts/makeover_mage.rs2`,
  `areas/draynor/scripts/aggie.rs2`,
  `quests/quest_fishingcompo/scripts/hemenster_fishing.rs2`,
  `skill_herblore/scripts/brew_potion.rs2`,
  `quests/quest_golem/scripts/golem_portal.rs2`,
  `quests/quest_gobdip/scripts/quest_gobdip.rs2`,
  `skill_crafting/scripts/dye_cape.rs2`, and wiring into
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #139 Elemental Workshop II, 770 lines.
- slice 139 done: Elemental Workshop II -- decrypting the sequel to the
  Book of the Elemental Shield, sneaking into the sealed lower half of the
  Seers' Village elemental workshop, repairing its crane/press/water-tank/
  wind-tunnel machinery to prime an elemental bar, imbuing it with the
  power of the mind in a basement extractor, and smithing a Mind Helmet.
  Grep-verified first (methodology steps 1-2, including `lc_quests.txt`):
  no LC proc, no 2009scape implementation. Fetched `Zoinkwiz/quest-helper`'s
  `ElementalWorkshopII.java` (683 lines) + `ConnectPipes.java` (87 lines),
  770 total, matching this row's own line count exactly. Row #56 Elemental
  Workshop I was independently re-audited as this slice's real
  prerequisite (not just trusted as `done`): `quest_elemental_workshop`'s
  three files genuinely set `%elemental_workshop_book`/`_key` and drive
  `%elemental_workshop_finished` as a real completion flag read by its own
  journal -- confirmed genuinely completable, used directly as the gate
  here (dbrow `requirement_quests`=38 does not resolve to EW1's own dbrow
  id 55 -- the same known-corrupt column this queue has repeatedly
  flagged; the real prerequisite is quest-helper's own
  `QuestRequirement(ELEMENTAL_WORKSHOP_I, FINISHED)`). Native dbrow
  `quest_elementalworkshop2` (id 119, endstate 11, questpoints 1,
  requirement_stats magic(6)20/smithing(13)30 boostable, stat_xp_awarded
  smithing(13)/crafting(12) 75000 raw = 7500xp each -- matches
  quest-helper's own SkillRequirement/ExperienceReward calls exactly).
  Native varbit schema entirely pre-declared, none invented: 20 named
  sub-fields on three basevars (`elemental_quest_2_main` as the top-level
  progress var, `_hide_key`, `_hatch`, `_jig_pos`, `_jig_state`,
  `_fire_state`, `_fire_pos`, `_earth_pipe_1/2/3_state`, `_water_state`,
  `_water_valve_1/2`, `_water_door`, `_water_level`, `_air_cog1/2/3`,
  `_air_fan_state`, `_mind_jig`, `_box_state`), matching quest-helper's own
  `VarbitID.ELEMENTAL_QUEST_2_*` names exactly (lowercased) --
  `%elemental_quest_2_main` driven through 12 breakpoints (0-11) matching
  quest-helper's own `steps.put(0..10)` map 1:1, reaching 11 at
  completion, matching the dbrow's own `endstate`. Native MULTI-NPC/
  MULTI-LOC records keyed on these varbits recovered real progression
  breakpoints and let all cosmetic state-swapping be skipped entirely
  (grep-confirmed, none invented): `elem2_cart_npc` (multinpc on
  `_jig_state`, 6 leaves), `elem2_stairs_door` (multiloc on `_hatch`),
  `elemental_workshop_2_boiler_multi` (multiloc on `_hide_key`),
  `elemental_piping_blue_broken_multi` (multiloc on `_water_state`),
  `elem2_wind_pin_high/low/left_multi` (multiloc on `_air_cog1/2/3`),
  `elem_windtunnel_fanblade` (multiloc on `_air_fan_state`),
  `elem_extractor_gun` (multiloc on `_mind_jig`); per this tree's own
  precedent (`quest_priest`'s `restless_ghost_altar_skull`/`_no_skull`),
  every trigger below binds to the *resolved leaf* name, not the
  multivarbit wrapper -- self-audited against all seven wrappers to
  confirm. `elem1_qip_earth_elemental_rock_version_rock` (mineable rock,
  own `op1=Mine`) is world-spawned 47 times in the west room
  (`areas/world/configs/m42_154.spawn`); the awakened
  `elem1_qip_earth_elemental_rock_version` is not statically spawned
  anywhere (grep-confirmed) -- hand-spawned on trigger, same idiom as
  `betweenarock`'s Avatar, and its combat/drop table
  (`quest_elemental_workshop/scripts/elemental_drops.rs2`) was already
  fully implemented by EW1's own port, reused unmodified. **Two real
  pre-existing shared-object collisions found and merged in rather than
  duplicated** (grep-first, per this queue's own non-negotiable rule):
  (1) `elemental_workshop_workbench` -- EW1's own port never used this loc
  (grep-confirmed) so this slice claims the sole
  `[opheldu,elemental_workshop_workbench]` trigger, but *within this
  slice itself* the claw-smithing and Mind-Helmet-smithing actions both
  target it, so one shared trigger dispatches by held item to
  `~elem2_make_claw`/inline helmet logic rather than declaring the header
  twice (sscompile accepts silent duplicates with no diagnostic -- caught
  by a self-sweep grepping every trigger header this slice authored
  against the whole tree, confirming exactly one definition each); (2)
  `elemental_workshop_furnace` -- shared physical object with EW1's own
  (unimplemented) lava/waterwheel/bellows middle section
  (`multivarbit=elemental_workshop_fire`, EW1's own furnace-lighting flag,
  never set anywhere in this tree, grep-confirmed) -- an unwritten EW1
  sub-mechanic, soft-skipped per this queue's own convention rather than
  treated as a hard blocker; this slice's own ore-smelting trigger works
  from either multiloc leaf without touching that flag. Simplifications
  (documented, no established precedent anywhere in this tree for the
  alternative): (1) the pipe-connection minigame (native rev-230 widget
  `ElemMagicpressPipes`, `WidgetModelRequirement` in quest-helper's own
  `ConnectPipes.java`) has no established drag-connect widget precedent --
  collapses to one deterministic "open the junction box and reconnect the
  pipes" action, directly setting the three `_earth_pipe_N_state` fields
  to quest-helper's own solved target values (5, 6, 13), same "no
  per-component widget click sequence" reasoning as Land of the Goblins'
  fairy ring dial; (2) no native multiloc exists anywhere for
  `_fire_pos`/`_fire_state` (grep-confirmed) -- the crane's 15-variant
  track/lava-arm never visually swaps position in this cache build, so
  every crane/priming lever action narrates via `mes()` against a single
  fixed loc (`elem2_crane_track_up_empty`) rather than reproducing that
  client-side animation; (3) the optional second bar / Slashed Book ->
  Mind Shield side loop (quest-helper's own "if you want to smith a mind
  shield after this quest" asides) is not required by any `steps.put`
  breakpoint -- not modelled, explicitly optional per quest-helper itself.
  **The "priming a bar" apparatus itself (crane, press, water tank, wind
  tunnel) was NOT collapsed** -- quest-helper's own `primingInWorkshop`
  `ConditionalStep` chain has no player decisions anywhere (every state
  has exactly one valid next action), so this slice independently
  re-derived that entire priority-ordered chain (cross-checked against
  every named lever/valve/corkscrew object and its own WorldPoint) into a
  real deterministic finite-state machine played with the actual 9 named
  objects (`elem2_fire_lever_1/2`, `elem2_lever_3way`,
  `elem2_earth_lever_1`, `elem2_water_lever`, `elem2_corkscrew`,
  `elem2_valve_1/2`, `elem2_air_lever`) plus the jig cart -- ordinary
  object-click content this tree already has an idiom for, just an
  unusually long chain, not a widget puzzle. Wiki
  `oldschool.runescape.wiki/w/Elemental_Workshop_II` +
  `.../Quick_guide` (paraphrased, not verbatim, per copyright, same
  caveat as every prior slice). `mingw32-make -C src sscompile` clean
  (only pre-existing snprintf-truncation warnings in the compiler
  itself); `mingw32-make -C src mock230-scripts` exit 0, 14,606 scripts
  compiled (up from 14,557, +49); full build log grepped for `elem2`/
  `elementalworkshopii`/`elementalworkshop2` returned zero warnings or
  errors; a self-sweep of all 34 trigger headers this slice authored
  (grep batch covering every `opnpc`/`oploc`/`opheld`/`opnpcu`/`oplocu`
  name) against the rest of the tree confirmed exactly one definition
  each, including the one intentionally-shared workbench trigger.
  Deferred: none identified beyond the documented simplifications above
  -- every `steps.put` breakpoint (0-10) is real and playable end-to-end.
  Files: `quests/quest_elementalworkshopii/{configs/elementalworkshopii.
  constant, scripts/elem2_{shared,intro,gather,repair,priming,helm,
  journal}.rs2}` and wiring into
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #145 Darkness of Hallowvale, 816 lines (rows #140-144
  are already `done`/`done (LC)`).
