# Quest Helper content port queue

Agent-loop state for **RuneLite Quest Helper → OSRS-Content** forward port of
quests that LostCity (Sept 2004) and 2009scape (~Jan 2009) never implemented.

LostCity remains the content *shape* (RuneScript triggers, procs, configs).
Quest Helper
(`/Users/matthewevers/Documents/git_repos/quest-helper/src/main/java/com/questhelper`)
is the **state-machine / test guide**: each helper's `steps.put(N, …)` is the
quest varbit progression a `.rs2` port must reproduce. It does **not** define
implementation — dialogue trees, dig rewards, and combat come from wiki /
cache / play, guided by the helper's step map and gameval names.

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

Loop prompt: read this file + PORTING_GUIDE §4 / §4.6; run
`tools/questhelper_extract.py --check` on the next pending row; port it; verify
(`mock230_pack --check-only`, `make -C src mock230-scripts`); update this file;
re-arm. Stop only when the user stops the loop.

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
5. **Interfaces:** drive the rev-230 panel; do not invent IF1. See
   `UI_ERA_PORTING_GUIDE.md`.

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
| 2 | Ribbiting Tale of a Lily Pad Labour Dispute | `theribbitingtaleofalilypadlabourdispute` | 220 | pending | |
| 3 | Prying Times | `pryingtimes` | 247 | pending | |
| 4 | Client of Kourend | `clientofkourend` | 257 | pending | needs #1 |
| 5 | The Queen of Thieves | `thequeenofthieves` | 259 | pending | needs #1 |
| 6 | The Depths of Despair | `thedepthsofdespair` | 267 | pending | needs #1 |
| 7 | A Porcine of Interest | `aporcineofinterest` | 275 | pending | |
| 8 | The Ascent of Arceuus | `theascentofarceuus` | 310 | pending | needs #1 |
| 9 | Ethically Acquired Antiquities | `ethicallyacquiredantiquities` | 313 | pending | |
| 10 | The Ides of Milk | `theidesofmilk` | 316 | pending | |
| 11 | In Search of Knowledge | `insearchofknowledge` | 317 | pending | miniquest-ish |
| 12 | Bone Voyage | `bonevoyage` | 320 | pending | |
| 13 | Children of the Sun | `childrenofthesun` | 337 | pending | |
| 14 | The Garden of Death | `thegardenofdeath` | 346 | pending | |
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
