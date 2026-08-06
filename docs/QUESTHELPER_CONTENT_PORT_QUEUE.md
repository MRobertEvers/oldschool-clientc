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
   | Quest / quick guide | `https://oldschool.runescape.wiki/w/<Quest_Name>` · `.../Quick_guide` |
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
| `helpers/skills/**` | skill guides |
| `helpers/playerquests/**` | player-authored |
| League / `LeagueQuestRegions` variants | temporary league content |
| Spelling-only mismatches already owned elsewhere (`vampyreslayer`, `romeoandjuliet`, `monkeymadnessi`, `fairytalei/ii`, `blackknightfortress`) | LC / 2009scape under other names |
| Helpers whose gameval names fail `--check` | `blocked` until pack grows |

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

- queue rebuilt (2026-08-06): Full audit of Quest Helper source. 176 in-scope quests identified (181 dirs minus 5 skip-list). 50 tracked as done, 14 already implemented in OSRS Content but missing from table, ~112 pending porting. Depth-first ordering preserved.
