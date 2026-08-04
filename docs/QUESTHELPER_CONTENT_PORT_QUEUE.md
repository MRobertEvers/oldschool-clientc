# Quest Helper content port queue

Agent-loop state for **RuneLite Quest Helper → OSRS-Content** forward port of
quests that LostCity (Sept 2004) and 2009scape (~Jan 2009) never implemented.

LostCity remains the content *shape* (RuneScript triggers, procs, configs).
Quest Helper
(`/Users/matthewevers/Documents/git_repos/quest-helper/src/main/java/com/questhelper`)
is the **state-machine / test guide**: each helper's `steps.put(N, …)` is the
quest varbit progression a `.rs2` port must reproduce. It does **not** define
implementation — dialogue trees (including branches the helper never lists),
dig rewards, and combat come from the OSRS wiki transcript / cache / play,
guided by the helper's step map and gameval names. See methodology step 5 and
`PORTING_GUIDE` §4.6 step 4.

Gameval constants (`NpcID.FOO` → `foo` in `configs/all.*.compack`) are the
cache's own names — **no id remapping**. When the helper and the osrs239 cache
disagree, **the cache wins**.

Parallel to:

- [`CONTENT_PORT_QUEUE.md`](CONTENT_PORT_QUEUE.md) — LostCity → tree
- [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md) — mid-era
- [`KRONOS_CONTENT_PORT_QUEUE.md`](KRONOS_CONTENT_PORT_QUEUE.md) — post-2009 skills/bosses

**Do not steal LC or 2009scape slices.** Ownership: no LostCity proc **and** no
2009scape implementation (registry presence alone in `Quests.kt` is not
implementation).

Each tick ports **one** pending unblocked slice per `docs/PORTING_GUIDE.md` §4
and §4.6. Status: `pending` | `in_progress` | `done` | `blocked`.

**Depth-first:** a row stays `in_progress` until every `steps.put` value is
playable end-to-end. It only becomes `done` when the whole quest is.

## Shared tree — never silence another lane

**Do not ever** `.rs2.skip` / `dirname.skip` / move / delete sibling content
(`skill_construction/`, `minigame_mta/`, or any other live tree) to green
`sscompile`. See PORTING_GUIDE §7 and
`.cursor/rules/no-park-sibling-content.mdc`.

Loop prompt: read this file + PORTING_GUIDE §4 / §4.6 / §7; run
`tools/questhelper_extract.py --check` on the next pending row; port it; NEVER
park sibling lanes; verify (`mock230_pack --check-only`,
`make -C src mock230-scripts`); update this file; re-arm. Stop only when the
user stops the loop.

## Methodology (non-negotiable)

1. **Grep LostCity first** (`PORTING_GUIDE` §2.2). If LC has the proc, it belongs
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
   tree. Before writing scripts, open these pages (spaces → `_`; see also
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
| `helpers/skills/**` | skill guides |
| `helpers/playerquests/**` | player-authored |
| League / `LeagueQuestRegions` variants | temporary league content |
| Spelling-only mismatches already owned elsewhere (`vampyreslayer`, `romeoandjuliet`, `monkeymadnessi`, `fairytalei/ii`, `blackknightfortress`) | LC / 2009scape under other names |
| Helpers whose gameval names fail `--check` | `blocked` until pack grows |

## Queue

Ordered ascending by helper line count (depth-first ⇒ small-first). Miniquests
filed under `helpers/quests/` are at the end.

| # | Slice | Helper | Lines | Status | Notes |
|---|---|---|---:|---|---|
| 0 | Queue tracker | — | — | done | This file + PORTING_GUIDE §4.6 + `tools/questhelper_extract.py` |
| 1 | X Marks the Spot | `xmarksthespot` | 204 | done | `%cluequest` / `quest_xmarksthespot`; Veos + 4 digs + casket; `::xmarksrun` OK; scripts 6221; pack 0 errors |
| 2 | Ribbiting Tale of a Lily Pad Labour Dispute | `theribbitingtaleofalilypadlabourdispute` | 220 | done | `%frog_quest` / `quest_ribbitingtale`; Marcellus↔frogs, chop/sabotage/chest NALIA/plushy/Cuthbert; `::ribbitrun` OK; scripts 6168; pack 0 errors; deferred iface 809 dial + hop-off cutscene |
| 3 | Prying Times | `pryingtimes` | 247 | done | `%quest_pry` / `quest_pryingtimes`; Steve+Thurgo crowbar+stout+crate; `::pryrun` OK; soft port-task/sail; sailing XP deferred; pack 0 errors |
| 4 | Client of Kourend | `clientofkourend` | 257 | done | `%veos_progress`; houses+quill+orb; `::cokrun` OK; wiki [Transcript](https://oldschool.runescape.wiki/w/Transcript:Client_of_Kourend); deferred ship/lamp Rub/tele |
| 5 | The Queen of Thieves | `thequeenofthieves` | 259 | done | `%piscquest`; Lawry→stew→Devan→Conrad→Queen→chest→Shauna; `::qotrun` OK; wiki [Transcript](https://oldschool.runescape.wiki/w/Transcript:The_Queen_of_Thieves); deferred full refuse trees / shared stairs climb |
| 6 | The Depths of Despair | `thedepthsofdespair` | 267 | done | `%hosidiusquest`; Kandur→Olivia→Galana→envoy→caves→Artur→snake→chest; `::dodrun` OK; wiki [Transcript](https://oldschool.runescape.wiki/w/Transcript:The_Depths_of_Despair); deferred bookshelf RNG / fail rolls / instance / favour UI |
| 7 | A Porcine of Interest | `aporcineofinterest` | 275 | done | `%porcine`; notice→Sarah→rope→cave→Spria→Sourhog→foot→Sarah→Spria; `::poirun` OK; wiki [Transcript](https://oldschool.runescape.wiki/w/Transcript:A_Porcine_of_Interest); deferred tracking/cutscene/slash matrix |
| 8 | The Ascent of Arceuus | `theascentofarceuus` | 310 | done | `%arcquest`; Mori→Andrews→tower souls→Kaal→grave trail→Trapped Soul→rocks→Trobin; `::aoarun` OK; wiki [Transcript](https://oldschool.runescape.wiki/w/Transcript:The_Ascent_of_Arceuus); deferred instance/trail strictness |
| 9 | Ethically Acquired Antiquities | `ethicallyacquiredantiquities` | 313 | done | `%eaa`; museum→crew→Betty→Haig shame→Herminius; `::eaarun` OK; wiki [Quick guide](https://oldschool.runescape.wiki/w/Ethically_Acquired_Antiquities/Quick_guide); COTS soft-skip; deferred charter/shame matrix |
| 10 | The Ides of Milk | `theidesofmilk` | 316 | done | `%cowquest`; Cassius→Gillie/Seth→milk→Duke→Brutus→finish; `::iomrun` OK; wiki [Quick guide](https://oldschool.runescape.wiki/w/The_Ides_of_Milk/Quick_guide); deferred Brutus specials / lamp Rub |
| 11 | In Search of Knowledge | `insearchofknowledge` | 317 | done | `%hosdun_knowledge_search`; Aimeri→tomes→Logosia; `::isokrun` OK; pack 0 errors; deferred combat pages/web/lamp Rub |
| 12 | Bone Voyage | `bonevoyage` | 320 | done | `%fossilquest_progress`→50; Haig→foreman→sawmills→navigators→sail soft; `::bvrun` OK; pack 0 errors |
| 13 | Children of the Sun | `childrenofthesun` | 337 | done | `%vmq1`→24; Alina→follow soft→door→Tobyn→mark→roof; `::cotsrun` OK; pack 0 errors |
| 14 | The Garden of Death | `thegardenofdeath` | 346 | in_progress | `%tgod` endstate 56 |
| 15 | At First Light | `atfirstlight` | 348 | pending | |
| 16 | Tale of the Righteous | `taleoftherighteous` | 353 | pending | needs #1 |
| 17 | Getting Ahead | `gettingahead` | 361 | pending | |
| 18 | The Corsair Curse | `thecorsaircurse` | 376 | pending | |
| 19 | Below Ice Mountain | `belowicemountain` | 377 | pending | |
| 20 | Shadows of Custodia | `shadowsofcustodia` | 406 | pending | |
| 21 | Current Affairs | `currentaffairs` | 407 | pending | |
| 22 | Twilight's Promise | `twilightspromise` | 433 | pending | |
| 23 | Sleeping Giants | `sleepinggiants` | 438 | pending | |
| 24 | Meat and Greet | `meatandgreet` | 478 | pending | |
| 25 | Pandemonium | `pandemonium` | 485 | pending | |
| 26 | A Night at the Theatre | `anightatthetheatre` | 490 | pending | |
| 27 | The Red Reef | `theredreef` | 559 | pending | |
| 28 | Misthalin Mystery | `misthalinmystery` | 564 | pending | |
| 29 | The Fremennik Exiles | `thefremennikexiles` | 573 | pending | |
| 30 | A Taste of Hope | `atasteofhope` | 629 | pending | |
| 31 | Making Friends with My Arm | `makingfriendswithmyarm` | 640 | pending | |
| 32 | Temple of the Eye | `templeoftheeye` | 662 | pending | |
| 33 | Perilous Moons | `perilousmoon` | 688 | pending | |
| 34 | Troubled Tortugans | `troubledtortugans` | 803 | pending | |
| 35 | Death on the Isle | `deathontheisle` | 827 | pending | |
| 36 | Scrambled! | `scrambled` | 840 | pending | |
| 37 | Beneath Cursed Sands | `beneathcursedsands` | 859 | pending | |
| 38 | The Final Dawn | `thefinaldawn` | 1274 | pending | |
| 39 | Secrets of the North | `secretsofthenorth` | 1293 | pending | |
| 40 | The Forsaken Tower | `theforsakentower` | 1353 | pending | needs #1 |
| 41 | A Kingdom Divided | `akingdomdivided` | 1560 | pending | |
| 42 | The Heart of Darkness | `theheartofdarkness` | 1582 | pending | |
| 43 | The Curse of Arrav | `thecurseofarrav` | 1665 | pending | |
| 44 | Sins of the Father | `sinsofthefather` | 1668 | pending | |
| 45 | Dragon Slayer II | `dragonslayerii` | 1782 | pending | |
| 46 | Monkey Madness II | `monkeymadnessii` | 3084 | pending | |
| 47 | Song of the Elves | `songoftheelves` | 4285 | pending | |
| 48 | Desert Treasure II | `deserttreasureii` | 5076 | pending | |
| M1 | Bear Your Soul | `bearyoursoul` | 144 | pending | miniquest; deprioritised |
| M2 | Enter the Abyss | `entertheabyss` | 212 | pending | miniquest; deprioritised |

## Opcode gap log

Record new Server VM opcodes **before** inventing C content hooks. Format:
`slice | opcode | why content needs it | status`.

| Slice | Opcode / surface | Why | Status |
|---|---|---|---|
| 1 | (none) | Dig/talk/inv/varbit already expressible | confirmed — no new opcode |
| 2 | (none) | Chest letter-dial is client iface 809; interim `mes`+letter gate | deferred UI polish, no new opcode |
| 3 | `sailing` skill / port-task | Prying Times needs Sailing XP+level + PortTaskStep cargo | deferred — soft-skip delivery/sail; smithing XP awarded |

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
