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

Each tick ports one pending unblocked slice per `docs/PORTING_GUIDE.md` section 4
and section 4.6. Status: `pending` | `in_progress` | `done` | `blocked`.

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
   (PORTING_GUIDE section 2.4 / section 2.5) -- plan + implement in the same slice (log below).
4. **Resolve names through the pack** -- gameval lowercased; never copy numeric
   ids. Run `tools/questhelper_extract.py <helper-dir> --check` before writing
   scripts; unresolved names -> `blocked` with the failing name, not workarounds.
5. **Wiki transcripts for dialogue (not just the helper).** Quest Helper is the
   state machine / critical-path guide; it does **not** enumerate every dialogue
   tree. Before writing scripts, open these pages (spaces -> `_`; see also
   `ExternalQuestResources.java` for the quest article URL):

   | What | Wiki URL |
   |---|---|
   | Quest / quick guide | `https://oldschool.runescape.wiki/w/<Quest_Name>` and `.../Quick_guide` |
   | **Dialogue trees** | `https://oldschool.runescape.wiki/w/Transcript:<Quest_Name>` |
   | Journal | `https://oldschool.runescape.wiki/w/Transcript:<Quest_Name>/Journal` |
   | NPC / item side trees | `https://oldschool.runescape.wiki/w/Transcript:<Name>` (follow links from the quest transcript) |

   Cover refuse options, re-talks, post-quest lines, lost-item replacements, and
   other branches the helper never `addDialogStep`s. Port when players can hit
   them; defer only with a queue-log note naming the deferred transcript
   section. Cite the transcript URL(s) in the row Notes / log when marking
   `done` (PORTING_GUIDE section 4.6 step 4).
6. **Interfaces:** drive the rev-230 panel; do not invent IF1. See
   `UI_ERA_PORTING_GUIDE.md`.
7. **Never park sibling lanes** -- no `*.skip`, no moving live trees aside for
   compile. Fix your own errors (PORTING_GUIDE section 7).

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
| `dragonslayerii` / `dragonslayer2` | quest_dragonslayer2 / quest_dragon | IN-LC — CONTENT_PORT_QUEUE (already done on QH queue) |
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
| P2 | Asoul's Bane | `asoulsbane` | 330 | pending | Mar 2019 — Asoul, dragonfire weapon quest |
| P3 | Spirits of the Elid | `spiritsoftheelid` | 352 | pending | Dec 2013 — Elid, spirit world, Khazard war |
| P4 | Another Slice of Ham | `anothersliceofham` | 485 | pending | Oct 2012 — Ham cult, cooking-themed quest |
| P5 | Darkness of Hallow Vale | `darknessofhallowvale` | 816 | pending | Aug 2013 — Drakan's descendant, vampire theme |

## Queue

Ordered ascending by helper line count (depth-first => small-first). Miniquests
filed under `helpers/miniquests/` are at the end.

| # | Slice | Helper | Lines | Status | Notes |
|---|---|---|---:|---|---|
| 1 | bearyoursoul | `bearyoursoul` | 144 | done |  |
| 2 | doricsquest | `doricsquest` | 151 | pending | npcs=doric |
| 3 | witchspotion | `witchspotion` | 162 | pending | npcs=hetty,ratindoors |
| 4 | impcatcher | `impcatcher` | 187 | pending | npcs=wizardmizgo |
| 5 | xmarksthespot | `xmarksthespot` | 204 | done |  |
| 6 | tearsofguthix | `tearsofguthix` | 209 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 7 | entertheabyss | `entertheabyss` | 212 | done |  |
| 8 | theribbitingtaleofalilypadlabourdispute | `theribbitingtaleofalilypadlabourdispute` | 220 | done |  |
| 9 | monksfriend | `monksfriend` | 224 | pending | npcs=brotheromad,brotheromad,brotheromad |
| 10 | therestlessghost | `therestlessghost` | 232 | pending | npcs=ghostx,fatheraerec,fatherurhne |
| 11 | runemysteries | `runemysteries` | 246 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 12 | pryingtimes | `pryingtimes` | 247 | done |  |
| 13 | sheepshearer | `sheepshearer` | 248 | pending | npcs=fredthefar,sheepunsheer,sheepunsheer |
| 14 | clientofkourend | `clientofkourend` | 257 | done |  |
| 15 | goblindiplomacy | `goblindiplomacy` | 257 | pending | npcs=generalbent,generalbent,generalbent |
| 16 | thequeenofthieves | `thequeenofthieves` | 259 | done |  |
| 17 | rovingelves | `rovingelves` | 263 | pending | npcs=rovingislwy,rovingfemal,rovingmossg |
| 18 | thedepthsofdespair | `thedepthsofdespair` | 267 | done |  |
| 19 | druidicritual | `druidicritual` | 268 | pending | npcs=kaqemeex,sanfew |
| 20 | aporcineofinterest | `aporcineofinterest` | 275 | done |  |
| 21 | deviousminds | `deviousminds` | 275 | pending | npcs=deviousmonk,deviousmonk,rcuzammyma |
| 22 | whatliesbelow | `whatliesbelow` | 286 | done (LC) | OSRS has 4 rs2 files (not in PORT_QUEUE table) |
| 23 | ernestthechicken | `ernestthechicken` | 288 | pending | npcs=veronica,professorod,professorod |
| 24 | atailoftwocats | `atailoftwocats` | 293 | pending | OSRS has 1 rs2 files (not in PORT_QUEUE table) |
| 25 | fishingcontest | `fishingcontest` | 297 | pending | npcs=tunneldwarf,grandpajack,bonzo |
| 26 | junglepotion | `junglepotion` | 298 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 27 | gertrudescat | `gertrudescat` | 299 | pending | npcs=gertrudeque,shilop,wilough |
| 28 | princealirescue | `princealirescue` | 302 | pending | npcs=hassan,osman,ned |
| 29 | cooksassistant | `cooksassistant` | 303 | pending | npcs=generalshopk,generalassis,cook |
| 30 | theascentofarceuus | `theascentofarceuus` | 310 | done |  |
| 31 | trollstronghold | `trollstronghold` | 311 | pending | npcs=deathigcom,deathsherpa,trollchampi |
| 32 | lostcity | `lostcity` | 312 | pending | npcs=zanarislepre,treespirit,warrioradven |
| 33 | ethicallyacquiredantiquities | `ethicallyacquiredantiquities` | 313 | done |  |
| 34 | theidesofmilk | `theidesofmilk` | 316 | done |  |
| 35 | insearchofknowledge | `insearchofknowledge` | 317 | done |  |
| 36 | sheepherder | `sheepherder` | 317 | done (LC) | OSRS has 8 rs2 files (not in PORT_QUEUE table) |
| 37 | makinghistory | `makinghistory` | 319 | pending | npcs=makinghistor,silvermerch,makinghistor |
| 38 | thehandinthesand | `thehandinthesand` | 319 | pending | npcs=handsandber,handsandgua,handsandber |
| 39 | bonevoyage | `bonevoyage` | 320 | done |  |
| 40 | theknightssword | `theknightssword` | 320 | pending | npcs=sirvyvin,squire,reldonormal |
| 41 | trollromance | `trollromance` | 321 | pending | npcs=trollromance,trollromance,trollromance |
| 42 | fightarena | `fightarena` | 322 | pending | npcs=arenaogre,arenascorpi,arenabounce |
| 43 | asoulsbane | `asoulsbane` | 330 | pending | npcs=soulbanelau,soulbaneang,soulbaneang |
| 44 | childrenofthesun | `childrenofthesun` | 337 | done |  |
| 45 | deathplateau | `deathplateau` | 337 | pending | npcs=deathigcom,deathheadse,deathguard |
| 46 | seaslug | `seaslug` | 338 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 47 | thegardenofdeath | `thegardenofdeath` | 346 | done |  |
| 48 | atfirstlight | `atfirstlight` | 348 | done |  |
| 49 | tribaltotem | `tribaltotem` | 349 | pending | npcs=kangaimau,rpdtemploye,crompertypr |
| 50 | witchshouse | `witchshouse` | 350 | pending | npcs=shapeshifter,shapeshifter,shapeshifter |
| 51 | spiritsoftheelid | `spiritsoftheelid` | 352 | pending | npcs=elidmayor,elidghaslor,elidranging |
| 52 | taleoftherighteous | `taleoftherighteous` | 353 | done |  |
| 53 | contact | `contact` | 355 | pending | npcs=icslittleh,contactjex,contactmais |
| 54 | shadesofmortton | `shadesofmortton` | 355 | pending | npcs=razmirekeel,razmirekeel,razmirekeel |
| 55 | gettingahead | `gettingahead` | 361 | done |  |
| 56 | elementalworkshopi | `elementalworkshopi` | 362 | pending | npcs=elem1qipea,elem1qipea,elem1qipea |
| 57 | bigchompybirdhunting | `bigchompybirdhunting` | 363 | pending | npcs=chompybird,chompybirdd,rantz |
| 58 | animalmagnetism | `animalmagnetism` | 366 | done (LC) | OSRS has 5 rs2 files (not in PORT_QUEUE table) |
| 59 | scorpioncatcher | `scorpioncatcher` | 374 | pending | npcs=thormac,seer,jailer |
| 60 | thecorsaircurse | `thecorsaircurse` | 376 | done |  |
| 61 | belowicemountain | `belowicemountain` | 377 | done |  |
| 62 | horrorfromthedeep | `horrorfromthedeep` | 380 | pending | npcs=horrordagan,horrordagga,horrordagga |
| 63 | dwarfcannon | `dwarfcannon` | 386 | pending | npcs=lawgof2,nulodion |
| 64 | familycrest | `familycrest` | 386 | pending | npcs=dimintheis,calebfitzha,calebfitzha |
| 65 | insearchofthemyreque | `insearchofthemyreque` | 393 | pending | npcs=routevanstr,routecyreg,routecurpil |
| 66 | shadowsofcustodia | `shadowsofcustodia` | 406 | done |  |
| 67 | currentaffairs | `currentaffairs` | 407 | done |  |
| 68 | zogreflesheaters | `zogreflesheaters` | 410 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 69 | treegnomevillage | `treegnomevillage` | 418 | pending | npcs=khazardwarl,kingbolren,commandermo |
| 70 | templeofikov | `templeofikov` | 419 | pending | npcs=ikovfirewar,ikovlucien1,ikovlucien1 |
| 71 | observatoryquest | `observatoryquest` | 424 | pending | npcs=qipobsgobl,observatory,observatory |
| 72 | olafsquest | `olafsquest` | 425 | pending | npcs=olaf2ulfric,olaf,olafingrid |
| 73 | grimtales | `grimtales` | 427 | pending | npcs=grimsylas,grimgrimgna,grimsylas |
| 74 | thetouristtrap | `thetouristtrap` | 433 | pending | npcs=irena,desertmining,miningslave |
| 75 | twilightspromise | `twilightspromise` | 433 | done |  |
| 76 | hauntedmine | `hauntedmine` | 435 | pending | npcs=hauntedmine,hauntedmine,saradominist |
| 77 | sleepinggiants | `sleepinggiants` | 438 | done |  |
| 78 | creatureoffenkenstrain | `creatureoffenkenstrain` | 440 | pending | npcs=fenkfenkens,fenkgardene,fenkexperim |
| 79 | naturespirit | `naturespirit` | 450 | pending | npcs=fillimantar,fillimantar,ghastvis |
| 80 | mountaindaughter | `mountaindaughter` | 459 | pending | npcs=mdaughterbe,mdaughterha,mdaughterha |
| 81 | shieldofarrav | `shieldofarrav` | 467 | pending | npcs=weaponsmaste,reldonormal,reldonormal |
| 82 | waterfallquest | `waterfallquest` | 473 | pending | npcs=almerawater,hudonwaterf,golriewater |
| 83 | thegrandtree | `thegrandtree` | 475 | pending | npcs=grandtreena,grandtreebl,grandtreena |
| 84 | meatandgreet | `meatandgreet` | 478 | done |  |
| 85 | anothersliceofham | `anothersliceofham` | 485 | pending | npcs=slicezanik,slicezanik,slicehamgu |
| 86 | pandemonium | `pandemonium` | 485 | done |  |
| 87 | clocktower | `clocktower` | 486 | pending | npcs=brotherkojo,brotherkojo |
| 88 | anightatthetheatre | `anightatthetheatre` | 490 | done |  |
| 89 | merlinscrystal | `merlinscrystal` | 490 | pending | npcs=morganlefa,lakebeggar,thrantax |
| 90 | eaglespeak | `eaglespeak` | 504 | pending | npcs=eaglepeakzo,eaglepeakni,tailorp |
| 91 | defenderofvarrock | `defenderofvarrock` | 508 | pending | npcs=eliaswhite,eliaswhite,doveliaswh |
| 92 | priestinperil | `priestinperil` | 511 | pending | npcs=priestperilt,kingroald,priestperil |
| 93 | plaguecity | `plaguecity` | 514 | pending | npcs=edmond,alrena,jethickvis |
| 94 | piratestreasure | `piratestreasure` | 520 | pending | npcs=redbeardfra,pirateirate,pirateirate |
| 95 | shilovillage | `shilovillage` | 531 | pending | npcs=zqmainzombi,zqmainzombi,zqmainzombi |
| 96 | thelosttribe | `thelosttribe` | 532 | pending | npcs=losttribes,hans,bob |
| 97 | demonslayer | `demonslayer` | 540 | pending | npcs=delrith,delrithweak,aris |
| 98 | holygrail | `holygrail` | 543 | pending | npcs=merlin2,blackknight,kingarthur |
| 99 | throneofmiscellania | `throneofmiscellania` | 546 | pending | npcs=vikingsailo,miscflowerg,misckingva |
| 100 | thefeud | `thefeud` | 550 | pending | npcs=feudalim,feudalim,shantay |
| 101 | thegolem | `thegolem` | 551 | pending | npcs=golembroken,golembroken,golembroken |
| 102 | theredreef | `theredreef` | 559 | done |  |
| 103 | misthalinmystery | `misthalinmystery` | 564 | done |  |
| 104 | thefremennikexiles | `thefremennikexiles` | 573 | done |  |
| 105 | coldwar | `coldwar` | 574 | pending | npcs=penglarryz,penglarryz,penglarryi |
| 106 | mourningsendparti | `mourningsendparti` | 575 | pending | npcs=rovingislwy,mourningari,mourningove |
| 107 | wanted | `wanted` | 580 | pending | npcs=wantedsummo,rdteleporte,siramikvar |
| 108 | deathtothedorgeshuun | `deathtothedorgeshuun` | 587 | pending | npcs=dttdzanikf,dttdzanikf,dttdzanikf |
| 109 | myarmsbigadventure | `myarmsbigadventure` | 589 | pending | npcs=myarmbabyr,myarmgiant,eadgartroll |
| 110 | thegiantdwarf | `thegiantdwarf` | 589 | pending | npcs=dwarfcityb,dwarfcityb,dwarfcitys |
| 111 | dragonslayer | `dragonslayer` | 591 | pending | npcs=guildmaster,oziach,oracle |
| 112 | taibwowannaitrio | `taibwowannaitrio` | 602 | pending | npcs=tbwttimfrak,04347kar,tbwtlubufu |
| 113 | eadgarsruse | `eadgarsruse` | 613 | pending | npcs=sanfew,deathsherpa,trollprison |
| 114 | heroesquest | `heroesquest` | 613 | pending | npcs=achietties,gerrant,jailer |
| 115 | kingsransom | `kingsransom` | 617 | pending | npcs=gossipyman,murderguard,gossipyman |
| 116 | atasteofhope | `atasteofhope` | 629 | done |  |
| 117 | biohazard | `biohazard` | 635 | done (LC) | OSRS has 7 rs2 files (not in PORT_QUEUE table) |
| 118 | makingfriendswithmyarm | `makingfriendswithmyarm` | 640 | done |  |
| 119 | swansong | `swansong` | 644 | pending | npcs=swanarnold,swanseatrol,swanherman |
| 120 | royaltrouble | `royaltrouble` | 657 | pending | npcs=vikingsailo,miscadvisor,miscprinces |
| 121 | thegreatbrainrobbery | `thegreatbrainrobbery` | 659 | pending | npcs=feverharmle,brainbrothe,brainbrothe |
| 122 | rumdeal | `rumdeal` | 662 | pending | npcs=dealevilsp,dealpete,dealcaptian |
| 123 | templeoftheeye | `templeoftheeye` | 662 | done |  |
| 124 | thefremennikisles | `thefremennikisles` | 670 | pending | npcs=fristrollm,fristrollm,fristrollf |
| 125 | gardenoftranquility | `gardenoftranquility` | 684 | pending | npcs=gardentroll,queenellama,queenellama |
| 126 | murdermystery | `murdermystery` | 686 | pending | npcs=murderguard,gossipyman,poisonsales |
| 127 | enakhraslament | `enakhraslament` | 688 | pending | npcs=enakhlazim,enakhlazim,enakhlazim |
| 128 | perilousmoon | `perilousmoon` | 688 | done |  |
| 129 | theslugmenace | `theslugmenace` | 694 | pending | npcs=rdteleporte,slug2oniall,slug2maledi |
| 130 | cabinfever | `cabinfever` | 704 | pending | npcs=feverteach,feverteach,feverquest |
| 131 | icthlarinslittlehelper | `icthlarinslittlehelper` | 707 | pending | npcs=icslittlep,icslittler,icslittler |
| 132 | inaidofthemyreque | `inaidofthemyreque` | 710 | pending | npcs=routeveliaf,burghvilage,burghvilage |
| 133 | betweenarock | `betweenarock` | 716 | pending | npcs=dwarfrocka,dwarfrocka,dwarfrocka |
| 134 | ratcatchers | `ratcatchers` | 737 | pending | npcs=gertrudepos,vcphingspet,pitratsarim |
| 135 | dreammentor | `dreammentor` | 745 | pending | npcs=dreaminadeq,dreameverla,dreamuntouc |
| 136 | watchtower | `watchtower` | 758 | pending | npcs=madskavid,watchtowerw,watchtowerw |
| 137 | shadowofthestorm | `shadowofthestorm` | 759 | pending | npcs=agrithreen,agrithbadde,agrithdave |
| 138 | landofthegoblins | `landofthegoblins` | 760 | pending | npcs=lotggrubfoo,lotgzanikf,lotggoblin |
| 139 | elementalworkshopii | `elementalworkshopii` | 770 | pending | npcs=elem1qipea,elem1qipea,elem1qipea |
| 140 | deserttreasure | `deserttreasure` | 803 | done (LC) | OSRS has 3 rs2 files (not in PORT_QUEUE table) |
| 141 | thedigsite | `thedigsite` | 803 | pending | npcs=examiner,qipdigsite,qipdigsite |
| 142 | troubledtortugans | `troubledtortugans` | 803 | done |  |
| 143 | undergroundpass | `undergroundpass` | 812 | pending | npcs=kinglathasv,caveguidevi,caveguidevi |
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
| 158 | legendsquest | `legendsquest` | 1,261 | pending | npcs=gujuo,nezikchened,echnedzekin |
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
- queue rebuilt (2026-08-06): Full audit of Quest Helper source. 176 in-scope quests identified (181 dirs minus 5 skip-list). 50 tracked as done, 14 already implemented in OSRS Content but missing from table, ~112 pending porting. Depth-first ordering preserved.
