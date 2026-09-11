# A Kingdom Divided modernization audit

Status: `audit-pending` — a shared completion path exists, but critical gameplay
is represented by explicit soft-skips and the quest is not `verified-modern`.

Audited: 2026-08-16

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to one implementation unit. It is an audit
and implementation specification, not evidence that the quest is complete.

## 1. Authoritative references

The pinned revisions below make the intended behavior reproducible even if the
live Wiki changes during implementation.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [A Kingdom Divided](https://oldschool.runescape.wiki/w/A_Kingdom_Divided?oldid=15300076) | 15300076, 2026-08-14 | Requirements, enemies, rewards, unlocks, and high-level route |
| [A Kingdom Divided/Quick guide](https://oldschool.runescape.wiki/w/A_Kingdom_Divided/Quick_guide?oldid=15131940) | 15131940, 2026-02-21 | Ordered interactions, items, puzzles, combat, and travel |
| [Transcript:A Kingdom Divided](https://oldschool.runescape.wiki/w/Transcript:A_Kingdom_Divided?oldid=15263399) | 15263399, 2026-07-14 | Accept/refuse, re-talk, alternate, lost-item, and post-quest dialogue branches |
| [Book of the dead](https://oldschool.runescape.wiki/w/Book_of_the_dead?oldid=15299827) | 15299827, 2026-08-14 | Upgrade, charges, teleports, destruction, and replacement behavior |

Quest Helper's `akingdomdivided` identifier is mentioned by the current source,
but no Quest Helper checkout or extracted fixture is present in this workspace.
It may be added as a state/test aid; it must not override the pinned Wiki.

## 2. Native quest identity and contract

The native `quest_kingdomdivided` dbrow is the metadata source of truth:

| Field | Native value / expected behavior |
| --- | --- |
| Quest ID | 164 |
| Type | Members' quest |
| Difficulty / length | Experienced / long |
| Series | Great Kourend, displayed as quest 4 |
| Release date | 16 July 2021 |
| Start | Martin Holt at the entrance to Kourend Castle |
| End state | `%akd = 150` |
| Quest points | 2 |
| Recommended combat | 70 |
| Required skills | 54 Agility, 52 Thieving, 52 Woodcutting, 50 Herblore, 42 Mining, 38 Crafting, and 35 Magic; the Wiki marks all as non-boostable and required to start |
| Direct quest requirements | The Depths of Despair, The Queen of Thieves, The Ascent of Arceuus, The Forsaken Tower, and Tale of the Righteous |
| Indirect requirements | Client of Kourend and X Marks the Spot through the direct prerequisites |
| Required supplies | Normal spellbook and runes for Fire Bolt or better, an axe, a 3- or 4-dose defence potion, volcanic sulphur, molten glass, and a dark essence block (or the tools to obtain one) |
| Mandatory enemies | Judge of Yama (level 168), two assassins (level 132), a lizardman brute (level 75), Xamphur (level 239), and a barbarian warlord (level 91) |
| Rewards | 2 quest points; Book of the dead; two lamps granting 10,000 XP each in a skill of at least level 40; 24 Arceuus spells; Kourend Castle respawn access through Asteros; and access to fight Yama |

The dbrow already contains the seven stat requirements, five direct quest rows,
two quest points, start NPC/coordinate, and end state. Modernization should read
or share those values rather than duplicate a second policy table in the quest.

## 3. Implementation surface

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_kingdomdivided/configs/kingdomdivided.constant` | Names 41 primary progress values from 0 through 150 and debug/reward constants | State names broadly track the guide; `^akd_lamp_count = 1` contradicts the two-lamp reward |
| `server/scripts/quests/quest_kingdomdivided/configs/kingdomdivided.varp` | Declares the `akd_primary` carrier | Uses a native cache carrier, but duplicates a carrier already represented by cache config and does not document ownership of `akd_secondary` |
| `server/scripts/quests/quest_kingdomdivided/scripts/kingdomdivided.rs2` | Journal, dialogue, interactions, completion, and debug walk | Only 375 lines for a long quest; contains 20 active `Soft-skip` messages and no faithful critical path |

Paths in this section are relative to
`OSRS-Content/osrs239-content/` unless stated otherwise.

### Shared and cross-directory consumers

| Path | Relationship | Required follow-up |
| --- | --- | --- |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dispatches `quest_kingdomdivided` to `~kingdomdivided_journal` | Keep the modern dbrow dispatch; expand state-aware journal text |
| `server/scripts/quests/scripts/quest_cheat.rs2` | `::complete` sets `%akd` to 150 idempotently | Keep as a state adapter; test it twice and do not treat it as gameplay coverage |
| `server/scripts/skill_magic/scripts/spellbook_switch.rs2` | Blocks choosing the Arceuus spellbook until `%akd >= 150` | Completion gate is correct; verify the entire 24-spell unlock surface |
| `server/scripts/skill_magic/scripts/spells/spellbook_swap.rs2` | Applies the same gate to Spellbook Swap | Completion gate is correct; retain shared policy |
| `server/scripts/general/scripts/enchanted_jewellry/book_of_the_dead.rs2` | Implements equip, charge check, five teleports, and charge drain | Add quest upgrade/grant integration, starting charge policy, Old Memorial recharge, destroy, and replacement lifecycle |
| `server/scripts/areas/world/configs/*.spawn` | Supplies many AKD NPCs and props in Kourend and underground maps | Audit every stage's visibility/transforms; static presence alone does not prove correct quest phasing |
| `server/scripts/doors/configs/doors.loc` and shared door fallback | Defines Rose's basement, prison, lookout, and Hughes door/gate behavior | Add state/item restrictions where the guide requires them |
| `server/scripts/ladders_stairs/` | Supplies shared climbing for quest entrances/exits | Verify direction, arrival square, stage gating, and instance ownership |

`quest_secretsofthenorth/scripts/secretsofthenorth.rs2` also binds an operation to
`akd_settlement_ruins_assassin`. That is a separate quest's use of a shared cache
NPC name and must not be mistaken for this quest's missing assassin encounter.

## 4. Persisted state model

The primary progress varbit `%akd` occupies bits 0–8 of native `akd_primary`.
The cache also supplies detailed side-state varbits on `akd_primary` and
`akd_secondary`, including Hughes clues, Tomas's lie, Martin's memoir, Asteros,
lamp claim state, five statue positions, five recruited flags, and five
per-house helped fields. Most are currently unused.

| Value | Symbol | Intended milestone | Current reachable writer |
| ---: | --- | --- | --- |
| 0 | `not_started` | Quest not started | Reset/debug only |
| 2 | `martin` | Accepted Martin's request | No normal writer; skipped at start |
| 4 | `fullore` | Sent to Commander Fullore | First Martin click, without acceptance or requirement checks |
| 8 | `outside` | Meet Fullore outside Hughes's house | Fullore dialogue |
| 10 | `search` | Search Hughes's house | Immediately written with 14 by Fullore |
| 14 | `herbert` | Follow evidence to Herbert | Immediately written with 10 by Fullore |
| 18 | `yama` | Enter Judge of Yama encounter | One Herbert click |
| 20 | `fullore_yama` | Judge defeated; report back | Talking to the combat NPC instantly wins |
| 24 | `fullore2` | Yama report | Immediately written with 26 by Fullore |
| 26 | `martin2` | Resume Martin/Rose investigation | Immediately written with 24 by Fullore |
| 30 | `diary` | Obtain/read Rose's diary | Martin soft-skip |
| 34 | `martin3` | Show diary to Martin | Immediately written with 38 by Martin |
| 38 | `note` | Follow diary note | Immediately written with 34 by Martin |
| 40 | `martin4` | Continue to Forthos | Martin dialogue |
| 42 | `forthos` | Reach Forthos Ruin | Martin dialogue |
| 46 | `panel` | Solve Forthos panel | Immediately written with 50 by Martin |
| 50 | `note2` | Obtain next note | Immediately written with 46 by Martin |
| 52 | `martin5` | Continue investigation | Martin dialogue |
| 54 | `settlement` | Reach Settlement Ruins | Martin dialogue |
| 60 | `ice` | Survive assassins; investigate ice | Martin soft-skip |
| 64 | `note3` | Melt ice and obtain clue | Clicking the ice NPC; no spell/use-on validation |
| 66 | `martin6` | Return to Martin | Martin dialogue |
| 68 | `faun` | Investigate the Legless Faun | Martin dialogue |
| 70 | `statues` | Solve statue puzzle | Debug runner only |
| 76 | `note4` | Obtain statue clue | Debug runner only |
| 78 | `crates` | Search Rose's basement | Debug runner only |
| 80 | `cutscene` | Basement reveal/cutscene | Debug runner only |
| 84 | `kaht` | Speak to Kaht B'alam | Kaht can advance from 80 to 84, but no normal script reaches 80 |
| 88 | `egg` | Obtain lizardman egg | Egg loc; advances even with a full inventory |
| 90 | `kaht2` | Return egg to Kaht | Immediately written with 92 and 96 by Kaht |
| 92 | `door` | Obtain key/reach laboratory gate | Immediately written with 90 and 96 by Kaht |
| 96 | `xamphur` | Enter Xamphur encounter | Kaht or soft-skipped lab gate |
| 102 | `xamphur_cs` | Defeat Xamphur/cutscene | Talking to the combat NPC; immediately followed by 104 |
| 104 | `table` | Search Xamphur's table | Immediately written with 102, then redundantly used as table precondition |
| 108 | `burial` | Return for burial | Table click or Fullore; Fullore also writes 110 |
| 110 | `houses` | Recruit the five house leaders | Fullore dialogue |
| 120 | `lookout` | Leaders assembled at Lookout | Any one of five leaders or Fullore |
| 124 | `help` | Begin the five house favours | Fullore also marks every house helped and writes 134 |
| 134 | `helped` | All five independent favours complete | Fullore or any one leader marks all five fields complete |
| 140 | `hosidius_final` | Return to Lookout/final meetings | Fullore soft-skip |
| 142 | `finish_talk` | Begin council conclusion | Fullore dialogue |
| 146 | `last_cs` | Final council/coronation cutscene | Immediately written with 148 by Fullore |
| 148 | `finish` | Ready to claim rewards | Fullore dialogue |
| 150 | `complete` | Permanent completion | `~akd_quest_complete` |

The unused `~akd_houses_helped` procedure correctly tests all five helped
fields, but no call site invokes it. The active flow instead writes all five
fields at once. The five native recruited bits are also unused.

## 5. Expected playable route

This is the minimum critical route to implement from the pinned Wiki. The
transcript must then supply all reachable dialogue variants around it.

### Chapter 1 — the disgraced councillor

1. Martin checks all five quest prerequisites and seven non-boostable skills,
   explains the risk, and offers an explicit accept/refuse choice.
2. Fullore starts the castle sequence and Hughes investigation. The player
   searches the house, gathers the receipt and related evidence, and questions
   Tomas Lawry, Fuggy, and Cabin Boy Herbert.
3. The player enters the Chasm of Fire and actually fights the Judge of Yama,
   including the fire-wave mechanic, failure/death, re-entry, and post-fight
   dialogue.
4. Fullore receives the report; Martin directs the player into the Arceuus
   Library. The player pickpockets Istoria for the key and obtains/reads Rose's
   diary and note.

### Chapter 2 — the last princess

1. At Forthos Ruin the player searches the stone piles, derives their unique
   code, cuts the vines with an axe, and operates the panel correctly.
2. At the Settlement Ruins the two-assassin encounter runs as combat, with
   safe retry/re-entry behavior.
3. A Fire Bolt-or-better spell is used on the ice; a plain NPC click must not
   substitute for the spell or consume progress.
4. At the Legless Faun the player solves the panel and arranges all five
   statues in the required order, using the native statue varbits.
5. The player follows the note to Rose's shack, searches the bed/crates, opens
   the trapdoor, and sees the basement reveal/cutscene.

### Chapter 3 — the mysterious mage

1. Kaht B'alam asks for an egg. Taking one spawns the lizardman brute encounter;
   inventory-full and repeated pickup paths remain safe.
2. Kaht removes exactly one valid egg and gives/authorizes the laboratory key.
   The gate checks that state instead of teleporting the plot forward.
3. The laboratory entry cutscene runs in the correct map/instance context.
4. Xamphur is a real boss encounter with hand mechanics, corruption/failure,
   death, retry, cleanup, and the post-fight cutscene.
5. The table evidence is collected and Fullore runs the return/burial sequence.

### Chapter 4 — the five houses

1. Recruit Trobin Arceuus, Vulcana Lovakengj, Kandur Hosidius, Shauna
   Piscarilius, and Shiro Shayzien separately. Set only that leader's native
   recruited bit and preserve re-talk behavior.
2. Run the first Xeric's Lookout meetings/cutscenes and enable the five favours.
3. Complete and persist each independent favour:

   - Arceuus: implement its complete Wiki route and return dialogue; confirm the
     intended helper/NPC and state details against the pinned transcript during
     implementation.
   - Lovakengj: work with the Tasakaal; combine volcanic sulphur and the defence
     potion into the shielding potion and resolve the Doors of Dinh sequence.
   - Hosidius: defeat the barbarian warlord and complete Phileas Rimor's route.
   - Piscarilius: work with Mori, molten glass, and dark essence to make the
     nullifier and complete the Chasm dialogue.
   - Shayzien: visit Martin in prison, work with Jorra, survive the assassin,
     obtain the declaration, and recover the Shayzien journal from the Vinery
     barrel/chest route where required.

4. Call a shared all-five predicate only after each task is genuinely complete;
   no single NPC may complete another house's work.

### Chapter 5 — council and rewards

1. Run the return-to-Lookout talks, accords, council, and coronation cutscenes
   with correct temporary NPC ownership and logout/re-entry recovery.
2. Fullore atomically upgrades Kharedst's memoirs to the Book of the dead,
   preserving charges, and grants exactly two quest-specific 10,000-XP lamps.
3. The completion lifecycle grants two quest points exactly once, displays the
   modern completion scroll, and activates all permanent unlocks.
4. Fullore supports post-quest replacement/reclaim for missing rewards; Asteros
   handles the Kourend Castle respawn unlock; Yama and all 24 Arceuus spells use
   the same permanent completion policy.

## 6. Gap and oversight register

| Priority | Area | Current defect | Required correction |
| --- | --- | --- | --- |
| P0 | Start contract | The first Martin click advances 0 directly to 4. There is no prerequisite, non-boostable skill, or required-item readiness handling and no accept/refuse choice; state 2 is unreachable normally. | Use native quest metadata/shared requirement helpers, modern `~p_choice*` dialogue, and separate not-started, offered, refused, accepted, and re-talk behavior. |
| P0 | Critical gameplay | Twenty active messages explicitly soft-skip combat, puzzles, searches, travel, cutscenes, and all five house favours. Several guide states are reachable only through `::akdrun`. | Replace every skip with the real operation and ensure every progress write is owned by successful completion of that operation. |
| P0 | Rewards | Completion advertises two lamps but `^akd_lamp_count` is 1 and the script grants generic `thosf_reward_lamp`; native `akd_lamp` is unused and has no Rub handler. | Grant two `akd_lamp` items and implement a 10,000-XP picker restricted to skills at level 40+, with atomic claim flags and duplicate protection. |
| P0 | Reward loss | `%akd` becomes 150 before free-space checks. With insufficient space, the book or lamp is silently omitted and Fullore has no reclaim path. | Preflight capacity or provide a durable pending-claim state. Completion and each reward claim must be idempotent and reclaimable after inventory space is freed. |
| P0 | Book upgrade | A new Book of the dead is simply added. Kharedst's memoirs is not upgraded, charges are not preserved/initialized, Old Memorial recharge is absent, and destruction/replacement is incomplete. | Integrate the item-var charge API with an atomic identity upgrade, starting/max charges, Old Memorial recharge, destroy confirmation, and replacement from Fullore without losing retained charges. |
| P0 | Egg atomicity | The egg loc advances to 88 even if inventory is full. Kaht deletes every egg rather than exactly one and advances through key, gate, and lab states in one talk. | Refuse progress when no egg can be granted; prevent/handle duplicates; remove one egg only after the exchange succeeds; grant/key the gate separately. |
| P1 | Combat | Judge of Yama and Xamphur are defeated by talking to their combat NPCs. Assassins, lizardman brute, and barbarian warlord are absent. | Implement encounters using current combat, instance, queue, ownership, death, retry, and cleanup machinery. Progress only on the authoritative kill/encounter result. |
| P1 | Puzzle/action validation | Forthos, statues, Rose's searches, and laboratory entry are absent. Ice progresses through `opnpc1` without a fire spell. | Bind the cache-authored loc/NPC/use-on operations, validate required tools/spells/items, persist native puzzle fields, and implement reset/retry rules. |
| P1 | Five-house state | Fullore or any one leader marks all five houses helped. Recruited bits are unused, helped fields are flattened to 1, and the all-five proc is dead. | Give each house its own transition range and journal branch, use recruited/helped native fields, and derive `%akd = 134` only from the all-five predicate. |
| P1 | Dialogue | The script has essentially no player choices and only placeholder one-line re-talk/post-quest messages. Transcript branches, refusal, alternates, loss recovery, and NPC-specific responses are missing. | Implement reachable pinned-transcript branches with modern chat menus and state/item-sensitive re-talks. |
| P1 | Journal | Forty-one primary values collapse into five vague todo buckets. It cannot tell the player which evidence, item, leader, or favour remains. | Render journal objectives from detailed primary and side state, including all five independent house tasks and pending reward claims. |
| P1 | Cutscenes/world state | Burial, Lookout, council, coronation, and earlier reveals are text skips. Static spawns exist without a demonstrated visibility lifecycle. | Use modern cutscene/instance and transform machinery; verify temporary entities, phasing, logout, region change, and cleanup. |
| P1 | Permanent unlocks | Arceuus spellbook selection is completion-gated, but no Asteros respawn handling was found and access to fight Yama is not demonstrated. | Inventory and test all 24 spell consumers, Kourend respawn selection, Yama access, and post-quest world transforms. |
| P1 | Debug/test validity | `::akdrun` writes the milestone constants directly and calls completion. It proves only that 150 can be stored. | Retain a reset helper if useful, but make automated tests invoke real triggers and assert state, inventory, NPC/loc cleanup, rewards, and relog recovery. |
| P2 | Presentation | Music transitions, jingles, animations, exact camera work, and cosmetic NPC movement have not been audited. | Reconcile cache assets and the transcript after the critical route works; document only genuinely cosmetic deviations. |

## 7. Modern-engine assessment

Parts worth retaining:

- native `%akd`, `akd_primary`, and `akd_secondary` state carriers;
- symbolic NPC, loc, object, dbrow, and coordinate names rather than raw IDs;
- the shared `~quest_complete_rewards` completion scroll and quest-point path;
- dbrow-based dynamic quest-journal dispatch; and
- the item-var based Book of the dead teleport charge implementation.

No `if_openmain` or `if_openoverlay` call is present in this quest root, so the
central defect is not an IF1 panel left behind here. The implementation uses
modern lifecycle pieces around a skeletal gameplay script. Modernization means
connecting cache-native state and content to real gameplay, not replacing these
pieces with a second quest framework or adding quest-specific C shortcuts.

Before adding authored variables, audit the native cache fields first. In
particular, the Hughes evidence bits, Tomas lie, memoir, Asteros, lamp reward,
statue positions, recruited bits, and per-house helped fields already model much
of the required branching.

## 8. Implementation sequence

### AKD-1 — make the contract and state model explicit

- Add this quest to the generated manifest with all files above and pinned
  revisions.
- Map the five native quest requirement rows to canonical dbrows and reuse the
  shared start-condition renderer/checker.
- Produce a transition fixture covering all primary values and relevant native
  side fields; identify values that are cutscene checkpoints versus stable
  resume points.
- Remove consecutive writes that make an intermediate state unobservable.

Acceptance: every persisted field has one documented owner, start cannot bypass
requirements, refusal leaves state 0, and relog at each stable state resumes the
same objective.

### AKD-2 — implement investigation and Judge of Yama

- Implement acceptance, castle/Hughes cutscene, evidence searches, Tomas/Fuggy/
  Herbert dialogue, the Chasm route, and the full Judge encounter.
- Use native clue fields and implement evidence-specific re-talks.
- Add death, logout, arena re-entry, repeated-op, and post-kill tests.

Acceptance: states 0–26 are reachable only through the Wiki actions and no
debug state assignment is needed.

### AKD-3 — implement Rose's trail and puzzles

- Implement library key pickpocket, diary/note lifecycle, Forthos code/vines/
  panel, assassins, spell-on-ice validation, statue puzzle, shack searches, and
  basement reveal.
- Add replacement/read behavior for every quest note or diary that can be lost.

Acceptance: states 30–80 and all statue/clue side fields survive relog and have
correct journal objectives; incorrect puzzle actions cannot advance progress.

### AKD-4 — implement Kaht and Xamphur

- Implement the egg/lizardman encounter and atomic egg-for-key exchange.
- Implement gated laboratory entry, cutscene, full Xamphur fight, table evidence,
  failure/retry, and cleanup.

Acceptance: states 84–108 require real actions; full inventory, duplicate egg,
death, logout, and repeated gate/boss operations cannot skip or duplicate state.

### AKD-5 — implement recruitment and five independent favours

- Recruit each leader and persist only their corresponding field.
- Run the Lookout sequence, then implement the Arceuus, Lovakengj, Hosidius,
  Piscarilius, and Shayzien routes independently.
- Replace direct all-fields writes with a tested `~akd_houses_helped` gate.

Acceptance: any completion order works, the journal names unfinished houses,
each subroute resumes after relog, and state 134 is impossible before all five
tasks complete.

### AKD-6 — finish council, rewards, and permanent effects

- Implement final meetings/cutscenes and safe resume checkpoints.
- Implement the memoirs-to-book upgrade and charge lifecycle.
- Implement two `akd_lamp` skill pickers and durable pending/reclaim state.
- Audit the 24 Arceuus spell gates, Asteros respawn, Yama access, and post-quest
  dialogue/world state.

Acceptance: completion is atomic and idempotent; a full inventory cannot lose a
reward; lamps give exactly 20,000 total selectable XP under the level-40 rule;
all unlocks activate together at permanent completion.

### AKD-7 — verification and removal of scaffolding

- Replace `::akdrun`'s direct state walk with test orchestration over real
  triggers or clearly label/remove it once automated coverage exists.
- Remove every active `Soft-skip`/`Deferred` marker and stale one-line placeholder.
- Complete static packing, transition tests, and real-client smoke evidence.

Acceptance: all Gates A–D in the governing plan pass and the manifest status can
change from `audit-pending` to `verified-modern`.

## 9. Verification matrix

| Scenario | Required assertions |
| --- | --- |
| Start eligibility | Each missing quest/skill blocks start; boosted levels do not satisfy the seven skills; refusal and later acceptance are safe |
| Investigation | Evidence cannot be skipped; duplicate searches/dialogues do not duplicate items or state |
| Judge encounter | Waves and damage work; kill advances once; death/logout/re-entry restore a valid encounter state |
| Forthos/statues | Wrong inputs do not advance; partial puzzle state persists/resets according to OSRS; axe and spell requirements are enforced |
| Quest-item lifecycle | Diary, notes, egg, key, shielding potion, nullifier, declaration, journal, and book all handle full inventory, duplicates, loss, and replacement |
| Xamphur | Hands/corruption/failure work; kill and post-fight cutscene advance once; cleanup leaves no private NPCs/locs |
| Five houses | All 120 valid completion orders converge; no one house writes another's field; journal reports the exact remainder |
| Cutscenes | Logout, death, region change, and reconnect at every checkpoint do not strand the player or leak NPCs |
| Completion | Full inventory defers claims safely; book charges survive upgrade/replacement; exactly two valid lamps and two quest points are awarded once |
| Unlocks | 24 spells, Arceuus spellbook switching, Kourend respawn, Yama access, and post-quest world/dialogue state share the permanent gate |
| Cheat adapter | First `::complete quest_kingdomdivided` reaches 150 and accounts for points; second invocation is a no-op |

Minimum repository checks after implementation:

```sh
tools/questhelper_extract.py akingdomdivided --check
make -C src torirsserver-scripts
ToriRSServer_Pack --check-only
```

The Quest Helper check is conditional on adding the missing helper source or
fixture. Record the exact cache path, test commands, client packets/screenshots,
and results in this document rather than replacing evidence with a `done` label.

## 10. Definition of done

A Kingdom Divided may be marked `verified-modern` only when:

- every Wiki critical action above is playable from the real Martin Holt start;
- all prerequisite, dialogue, item, combat, puzzle, cutscene, death, relog,
  inventory, duplicate-action, and replacement cases pass;
- native state is used coherently and every transition has one successful-gameplay
  owner;
- rewards and permanent unlocks are exact, atomic, reclaimable, and idempotent;
- the journal is accurate throughout all five house branches;
- no active soft-skip, raw-ID workaround, legacy panel open, or quest-specific
  engine shortcut remains; and
- script compilation, cache packing, automated transition coverage, real-client
  smoke coverage, and the idempotent completion-cheat check are recorded here.
