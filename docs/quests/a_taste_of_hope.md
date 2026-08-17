# A Taste of Hope modernization audit

Status: `audit-pending` — the native quest row, state carrier, world/cache
assets, journal dispatch, special-attack dispatch, and a 0-to-165 debug scaffold
exist. A legitimate player cannot start through the live Garth transform, the
first roof obstacle is also bound to the wrong transform, and every defining
quest system is absent or replaced by a state shortcut.

Audited: 2026-08-16

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the full quest, both Myreque bases,
shared Safalaan/Vertida ownership, the Serafina puzzle, both combat instances,
the Ivandis flail, the reward items, and downstream Myreque quests. It is an
implementation specification, not completion evidence.

## 1. Authoritative references

These revisions are pinned so implementation and review use a stable route,
dialogue, puzzle, combat, reward, replacement, and teleport contract.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [A Taste of Hope](https://oldschool.runescape.wiki/w/A_Taste_of_Hope?oldid=15285577) | 15285577, 2026-08-02 | Requirements, full route, cutscenes, combat, rewards, music, and downstream quests |
| [A Taste of Hope/Quick guide](https://oldschool.runescape.wiki/w/A_Taste_of_Hope/Quick_guide?oldid=15233795) | 15233795, 2026-06-15 | Ordered native checkpoints, item actions, puzzle recipe, flail assembly, and boss thresholds |
| [Transcript:A Taste of Hope](https://oldschool.runescape.wiki/w/Transcript%3AA_Taste_of_Hope?oldid=15255449) | 15255449, 2026-07-06 | Acceptance/refusal, re-talks, mercenaries, Myreque conversations, cutscenes, and completion dialogue |
| [Abomination](https://oldschool.runescape.wiki/w/Abomination?oldid=15206300) | 15206300, 2026-05-05 | Level-465 reveal, weakening to level 149, 200 HP, attacks, stat drain, and instance behavior |
| [Ranis Drakan](https://oldschool.runescape.wiki/w/Ranis_Drakan?oldid=15260156) | 15260156, 2026-07-10 | Level-233 stats, flail-only damage, 65%/25% add phases, blood attacks, enrage, and deathbank |
| [Ivandis flail](https://oldschool.runescape.wiki/w/Ivandis_flail?oldid=15284510) | 15284510, 2026-07-31 | Three-component creation, vampyre affinity, Bloom, autocast, Retainer, shops, and replacement |
| [Drakan's medallion](https://oldschool.runescape.wiki/w/Drakan%27s_medallion?oldid=15252650) | 15252650, 2026-07-04 | Teleport menu, unlock gates, Wilderness restriction, and replacement routes |
| [Tome of experience](https://oldschool.runescape.wiki/w/Tome_of_experience_%28A_Taste_of_Hope%29?oldid=15303388) | 15303388, 2026-08-16 | One non-bankable tome, three 2,500-XP chapters, level-35 gate, destruction, and reclaim |
| [Blood potion](https://oldschool.runescape.wiki/w/Blood_potion?oldid=15188847) | 15188847, 2026-04-22 | Second potion recipe and locked-door use |
| [Flaygian's notes](https://oldschool.runescape.wiki/w/Flaygian%27s_notes?oldid=15188834) | 15188834, 2026-04-22 | Readable clue, flail rationale, loss, and replacement behavior |
| [Item Retrieval Service](https://oldschool.runescape.wiki/w/Item_Retrieval_Service?oldid=15124878) | 15124878, 2026-02-10 | Ranis deathbank ownership and unsafe-death warning |

Transition aid only: the local Quest Helper checkout's
[`ATasteOfHope.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/atasteofhope/ATasteOfHope.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms every native
primary value from 0 through 160, the ten route zones, required gamevals, item
recipes, and NPC/loc alternates. It guides transitions and tests but does not
override the Wiki or cache.

`python3 tools/questhelper_extract.py atasteofhope --check` resolves every
referenced item, NPC, loc, and varbit and correctly identifies
`quest_tasteofhope`. This is provenance evidence, not playability evidence.

## 2. Native quest identity and player contract

The cache-native `quest_tasteofhope` row and pinned sources define this
contract:

| Field | Native value / expected behavior |
| --- | --- |
| Quest ID | 150 in the cache row; OSRS release-order number 138 |
| Type | Members' quest; Myreque series #4 |
| Difficulty / length | Experienced / medium |
| Release date | 24 May 2018 |
| Start | Talk to live Garth at Ver Sinhaza, by the Theatre of Blood |
| Prerequisite | Darkness of Hallowvale complete, including its transitive Myreque chain |
| Required levels | 48 Crafting, 45 Agility, 40 Attack, 40 Herblore, 38 Slayer; none boostable and all required at start |
| Recommended combat | 70 |
| Required kills | Abomination (149), Ranis Drakan (233), and four Vyrewatch (87) |
| Required items | 1,000 coins, knife or sickle, emerald, chisel, Rod of ivandis, level-2 enchant runes/tablet, and combat gear; vial of water and pestle are obtainable in the quest |
| Primary state | `%myq4`, cache varbit `myq4` on `myq4_main`, bits 0–8 |
| Native side state | `myq4_serafina_book`, `myq4_veliaf_rod`, and two-bit `myq4_xp_reward` on the same carrier |
| End state | `%myq4 = 165` |
| Quest points | 1 |
| Rewards | Ivandis flail, Drakan's medallion, and one three-chapter Tome of experience |
| XP reward | 2,500 XP per chapter, three chapters, any skill at level 35 or higher; the same skill may be selected repeatedly |
| Music | Welcome to the Theatre, Conspiracy, Bait, A Taste of Hope, and Vanescula at their native scene/location beats |
| Required for | A Night at the Theatre and Sins of the Father |

The dbrow already contains every skill, prerequisite, quest-point, start-NPC,
start-coordinate, and end-state fact. Content still must enforce them. Quest
list metadata is not an acceptance gate.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_tasteofhope/configs/tasteofhope.constant` | Named primary values and three coordinates | Values broadly mirror the native route, but several names mean “completed checkpoint” while the native value means “perform checkpoint”; no side-state constants exist |
| `server/scripts/quests/quest_tasteofhope/configs/tasteofhope.varp` | Configures cache carrier `myq4_main` as permanent/transmitted | Correct carrier ownership; native side bits are never used |
| `server/scripts/quests/quest_tasteofhope/scripts/tasteofhope.rs2` | Entire quest scaffold, journal, completion, shared NPC dispatch, and debug runner | 307 lines; explicit soft-skips replace the route, puzzle, crafting, combat, instances, and cutscenes |

The header openly defers Meiyerditch traversal, the Serafina matrix,
Abomination, flail crafting, Ranis, medallion UI, and Tome use. These are not
optional polish: together they are almost the entire quest.

### Mandatory shared and cross-directory surfaces

| Path | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dbrow dispatcher | Correctly calls `~tasteofhope_journal`, but the journal collapses dozens of meaningful substates |
| `server/scripts/quests/scripts/quest_cheat.rs2` | `::complete` adapter | Writes only 165; no side-state, world, reward, music, or downstream coherence is established |
| `server/scripts/quests/quest_darknessofhallowvale/scripts/{doh_castle,doh_meiyerditch}.rs2` | Earlier Safalaan and Vertida dialogue | A Taste owns the live triggers and delegates DoH's active range to procs; retain one canonical trigger per transformed NPC |
| `server/scripts/quests/quest_sinsofthefather/scripts/sinsofthefather.rs2` | Later Vertida dialogue and reward-era ownership | A Taste delegates the Sins flail range, but no A Taste completion prerequisite is enforced in Sins |
| `server/scripts/quests/quest_nightatthetheatre/` | Downstream quest | No `%myq4`/A Taste prerequisite enforcement was found |
| `server/scripts/areas/world/configs/{m56_50,m57_50,m56_150,m56_151}.spawn` | Harpert/guard, Garth/mercenaries/Kael, both bases, Serafina basement | Cache carriers are spawned; several live transformed click targets have no matching handler |
| `server/scripts/ladders_stairs/configs/ladders.loc` | Generic stair/ladder categories | Categorizes several quest locs, but categories do not implement stateful movement, steam, or instance safety |
| `server/scripts/doors/configs/doors.loc` | Generic Serafina door and Ral trapdoor stages | Generic toggling must not bypass the blood-potion lock or quest access rules |
| `server/scripts/skill_herblore/scripts/brew_potion.rs2` and `configs/brewing/brew.dbrow` | Generic potion dispatcher/data | Recognizes `myq4_blood_vial`, but no complete quest-specific two-recipe contract is connected |
| `server/scripts/skill_combat/scripts/player/specs/pvm_ivandis_flail.rs2` | Retainer special | Performs a normal crush hit; explicitly lacks target eligibility and 30-second retaliation suppression |
| `server/scripts/skill_combat/configs/{special_attack.obj,bas/*}` | Flail energy, weapon animations, and sounds | Baseline weapon data exists; vampyre affinity, Bloom, autocast, and full Retainer behavior are not proved |
| `server/scripts/interface_music/scripts/music.rs2` | Shared music unlock system | All five cache music rows exist, but quest content unlocks none of them |
| `configs/all.{dbrow,varp,varbit,obj,npc,loc,seq,spotanim}` | Native identity and assets | The intended state, transforms, items, actors, sequences, and locations are substantially present but unused |

### Cache-native content already available

The osrs239 cache includes much more than the script uses:

- the native primary and three side varbits on `myq4_main`;
- Garth/Harpert live transforms, three mercenaries, the bank guard, Safalaan,
  Vertida, Flaygian, Kael, Mekritus, Andiess, Vanstrom, Ranis, Vanescula,
  Verzik, four crowd/combat Vyrewatch, three Abomination forms, two Nylocas,
  steam, citizens, and cutscene variants;
- the complete roof loc set, red window, steam vents, Serafina bed/fountain/
  stairs/door/supply containers/chest, old-base crates/trapdoor/exit, Theatre
  crowd scenery, escape citizen, Ranis corpse, and deathbank chest;
- every potion intermediate, both notes, diary, chain, sickle intermediates,
  flail, medallion, and native one-object Tome;
- dedicated Myreque, vampyre, explosion, levitation, death, projectile, and
  scene sequences/spotanims; and
- native music dbrows for the quest scenes.

Modernization should drive these named assets through modern RuneScript,
instance, combat, UI, and lifecycle services. It should not replace them with
text summaries or numeric legacy openers.

## 4. Native state model and current reachability

Quest Helper and the native end state give this primary transition model:

| State | Required phase | Current implementation / defect |
| ---: | --- | --- |
| 0 / 5 / 10 | Garth introduction, choice, and opening cutscene | Only parent `myq4_garth` is handled; the spawned carrier transforms to live `myq4_garth_vis`. If invoked directly, it skips all three values to 15 with no requirements or choice |
| 15 | Enter north Myreque base and speak to Safalaan | Shared Safalaan dispatch advances directly to 20 with two paraphrased lines |
| 20 / 25 | Speak to mercenaries, then attempt rubble | No mercenary handler exists. The script binds `myq4_obstacle_rubble_01`, while the clickable transform is `myq4_obstacle_rubble_01_op` |
| 30 / 35 | Pay Harpert, then reattempt rubble | Only parent `myq4_harpert` is handled; live target is `myq4_harpert_vis`. Direct invocation charges no coins and jumps to 40 |
| 40 | Traverse roof and look through red window | Every obstacle is one shared text skip gated to values below 40; no `myq4_window` handler or cutscene exists. Returning to Safalaan can advance anyway |
| 45 / 50 | Report spying to Safalaan | Two short interactions write 45 then 55; no actual observation is recorded |
| 55 / 60 | Speak to Flaygian | Flaygian gives the wrong future notes item early and writes 65; full haemalchemy dialogue is absent |
| 65 / 70 | Speak to Safalaan and travel to Serafina | One line writes 75; route and discussion are absent |
| 75 | Enter Serafina's house and talk to Safalaan | Door/stairs teleport directly into the basement and write 80; no access/path correctness or conversation |
| 80 / 81 | Brew water-based potion and try door | Clicking any supply container adds later quest items and skips this experiment |
| 82 / 83 / 84 | Obtain blood and brew second potion | Never represented; no vial exchange, item-on-item recipes, consumption, or door use |
| 85 / 86 | Open chest, read/give old notes | Skipped; a crate handler adds notes and jumps directly to 90 |
| 90 | Return to base and trigger trap cutscene | Clicking an Abomination form stands in for the scene |
| 95 / 100 | Fight weakened Abomination | NPC Talk-to click writes 95 then 105 immediately; no combat or completion signal |
| 105 | Speak to injured Safalaan after the fight | A normal Safalaan click writes 110 and calls travel a soft-skip |
| 110 / 115 | Enter Old Man Ral's basement and regroup | Trapdoor teleports to the new base and writes 120 |
| 120 | Speak to Vertida, receive/read Flaygian's notes | Vertida gives no item and writes 125; the notes were incorrectly created at state 55 |
| 125 / 130 | Obtain components and assemble flail | Either crate adds a finished flail for free and writes 135; no items, levels, spellbook, runes, or recipe are checked |
| 135 | Speak to Safalaan/Kael, equip flail, accept fight | Kael silently adds a missing flail and writes 140; no ready choice or equipment check |
| 140 | Instanced Ranis battle | Talk-to on any Ranis form writes 145; no attack path, phases, adds, escape, death, or kill ownership |
| 145 | Speak to Kael | One line writes 150 |
| 150 / 155 / 160 | Return to Ral base and finish with Safalaan | First eligible Safalaan click runs completion immediately; later native dialogue states are skipped |
| 165 | Complete | Completion is committed before capacity-sensitive, incorrect reward delivery |

The normal route is blocked at state 0 by the live Garth transform. Even after
correcting that trigger, the rubble and Harpert transforms are misbound and the
red window has no trigger. Administrative invocation can make the state move,
but it proves only that assignments compile.

The debug `::tohrun` proc explicitly mutates every milestone, prints the
soft-skips, and calls completion. It is useful only as a constant smoke test;
it exercises no world click, requirements, item transaction, cutscene, combat,
instance, lifecycle, or reward selection.

## 5. Current versus required playable route

### Stage 1 — Garth, Safalaan, and the mercenaries

Required behavior:

- bind the canonical live Garth transform and preserve unrelated/postquest
  dialogue;
- enforce Darkness of Hallowvale and all five non-boostable skill gates before
  offering acceptance;
- implement Yes/No, re-talk, and the Vanstrom/Ranis entrance scene;
- route through the existing Meiyerditch/base access systems without teleport
  shortcuts;
- let Safalaan assign the investigation; and
- allow any of the three visible mercenaries to confirm that both vampyres are
  still inside and explain the window route.

Current code has no acceptance choice, prerequisites, skill checks,
mercenaries, or opening scene. The one Garth line writes 15 if a developer can
invoke the hidden parent definition.

### Stage 2 — Harpert and vampyric espionage

Required behavior:

- the first rubble attempt is blocked by the bank guard and writes the correct
  native checkpoint;
- Harpert offers the transcript choice and atomically removes exactly 1,000
  coins only on acceptance;
- the second attempt climbs to the roof;
- both timed steam vents inflict 15–20 damage and knock the player back only
  while active, with safe cancellation/death handling;
- jump/climb/down obstacles move to their exact coordinates and elevations;
- the red-window action runs the Verzik/Vanstrom/Ranis/Abomination scene; and
- the successful end restores health, prayer, and run energy and advances only
  after the post-scene dialogue completes.

Current code neither removes coins nor handles the live transforms. All five
obstacle names share one handler that changes a value and never moves the
player; the actual `myq4_window` is completely unhandled.

### Stage 3 — Flaygian and Serafina's house

Required behavior:

- Safalaan, Vertida, and Flaygian hold the full haemalchemy discussion in the
  correct 45–70 sequence;
- the normal world route and ladder/stairs semantics lead to Serafina's house;
- the optional bed search yields the old diary once and records
  `myq4_serafina_book`;
- Safalaan explains the locked room without giving away the puzzle;
- each supply loc gives only its own missing item with full-inventory and
  repeated-search handling;
- the fountain turns an empty vial into water;
- herb + water, crushed meat, and unfinished potion create the first potion
  with exact consumption; using it on the door deliberately fails;
- Safalaan exchanges/fills an empty vial with blood after the failed attempt;
- the blood recipe produces the second potion, whose use unlocks the door;
- the chest stages Open/Search correctly and yields readable old notes; and
- giving the notes to Safalaan removes them and writes the correct checkpoint.

The current six-loc stack makes every barrel/crate/chest equivalent, creates a
finished blood potion and old notes without ingredients, assigns 85 and 90 in
the same tick, and never uses the door. It also creates Flaygian's notes in the
wrong chapter.

### Stage 4 — the Abomination attack

Required behavior:

- returning to the northern base starts a private/restart-safe cutscene;
- the false notes are exposed and the Abomination kills Mekritus, Andiess, and
  Flaygian;
- Safalaan levitates/explodes, transforming the level-465 form to the level-149
  weakened form;
- the combat instance owns the player, Abomination, Vertida ally, exits,
  death/re-entry, and kill signal;
- the 200-HP boss uses melee/ranged attacks plus a 1–3 stat-drain projectile;
  Protect from Missiles blocks damage but not the drain;
- Vertida cannot die and can slowly finish the boss; and
- only authoritative NPC death permits the injured-Safalaan conversation and
  relocation to Old Man Ral's base.

The cached NPC animation data exposes a serious current gap: all three
Abomination forms have a death and defend animation, but generated combat data
has `attack_anim = null`. The modernization must author the real attack modes,
projectile/impact timing, and combat controller rather than rely on the generic
generator.

### Stage 5 — the legendary weapon

Required behavior:

- Ral's trapdoor and ladder obey access/state gates and return travel;
- Safalaan and Vertida perform the regroup conversation;
- Vertida gives Flaygian's notes capacity-safely, Read exposes the weapon clue,
  and loss/replacement routes work;
- speaking to Vertida sets `myq4_veliaf_rod`, enabling Veliaf to return a Rod
  of ivandis when appropriate;
- the two crates independently give one blessed silver sickle and one chain;
- emerald + sickle requires the emerald, chisel, and 48 Crafting;
- Lvl-2 Enchant consumes the correct runes on the standard spellbook, while the
  tablet alternative works through its normal spell/item system;
- the rod, chain, and enchanted sickle are consumed atomically to make exactly
  one flail; and
- Safalaan/Kael will not start the confrontation until the flail is equipped.

The finished flail is currently conjured by either crate, and Kael conjures a
second recovery copy when it is missing. There is no construction recipe,
notes Read handler, Rod recovery flag, or component loss policy.

### Stage 6 — Ranis Drakan and completion

Required behavior:

- Kael presents the ready/refuse branch and creates a private fight instance;
- a nearby `myq4_citizen_10_op` supports Escape and Quick-escape without
  falsely completing the fight;
- only the equipped Ivandis flail can damage Ranis, with the weapon's 20%
  vampyre damage bonus;
- the initial 400-HP phases use melee, magic, Blood Barrage healing, and the
  charged close-range explosion;
- at 65% and 25%, Ranis flies, becomes untargetable, spawns two level-87
  Vyrewatch, and throws blood bombs that can damage player and adds;
- after the second add wave, the enrage form attacks at speed 2 with accurate
  melee and max hit 6;
- death routes items to the cache-native chest and charges 20,000 coins for
  retrieval, with the standard unsafe-death warning;
- authoritative Ranis death produces the scene/corpse, returns the player,
  and unlocks Kael's post-fight dialogue; and
- Kael then Safalaan run every 145–160 conversation/cutscene beat, including
  Vanescula's reveal, before atomic completion at 165.

Current Talk-to triggers on five combat/transition forms bypass combat. There
is no Attack integration, instance, health threshold, Vyrewatch controller,
blood projectile, explosion, escape handler, corpse, deathbank handler, or
post-fight scene.

## 6. Item, weapon, UI, and lifecycle contracts

### Quest puzzle items and readable texts

Every quest item needs exact acquisition, Read/Use/Destroy, duplicate, loss,
replacement, inventory-capacity, and cleanup behavior. Currently only item
definitions exist for the diary, notes, potion intermediates, flail components,
and Tome; nearly all of their cache verbs have no RuneScript trigger.

Use atomic remove/add helpers for each transformation. A full inventory is not
always a failure when the operation consumes one source and returns one output;
calculate the net slots after consumption. Wrong item order, interruption,
repeated clicks, and reconnect must never consume inputs or advance state.

### Ivandis flail

The cache supplies the correct equipment stats and Retainer special ID. The
weapon still needs four separate contracts:

1. creation and post-quest purchase/replacement;
2. full damage against every vampyre tier plus the 20% bonus;
3. Bloom behavior inherited from the blessed sickle and standard-spell
   autocasting; and
4. Retainer target validation, below-50% check, messages, 10% energy, normal
   attack delay, and exactly 30 seconds without retaliation for juvenile/
   juvinate targets.

`pvm_ivandis_flail.rs2` explicitly falls back to a normal crush attack because
the current content API has neither a usable vampyre-subtype predicate nor a
retaliation-suppression primitive. Prove those gaps repository-wide and add the
smallest general combat capability; do not fake Retainer with movement freeze.

### Drakan's medallion

The cache currently exposes a modern Teleport submenu with Ver Sinhaza,
Darkmeyer, Slepe, and Castle Drakan. No handler was found. Implement by named
item/subaction through the shared teleport lifecycle:

- Ver Sinhaza is available after A Taste;
- Darkmeyer requires Sins of the Father;
- Slepe requires the medallion unlock from a Slepey tablet;
- Castle Drakan requires its current quest/shrine unlock;
- all destinations are unlimited but reject use above level 20 Wilderness and
  obey normal combat/teleblock/modal/cancellation rules; and
- Kael/Ivan/Ral/Perdu replacement ownership changes with later quest eras.

Do not reduce the item to a one-destination Rub script or hard-code a legacy
numeric menu.

### Tome of experience

The completion proc incorrectly loops three times and adds three Tome objects.
The native reward is one non-bankable object with three chapters. Use
`%myq4_xp_reward` as the durable chapter counter only after confirming the live
encoding for 0–3.

Read must open the standard modern XP-skill selector, show only eligible
skills, reject levels below 35 at confirmation time, allow cancellation, award
exactly 2,500 XP, and consume the book only after chapter three. Destruction and
later-quest reclaim must preserve already consumed chapters and cannot duplicate
XP.

### Modal and instance lifecycle

The two cutscenes, two combat instances, completion scroll, Tome selector,
medallion menu, deathbank confirmation, and any dialogue choice each need one
owner. Every mount must initialize state, arm events, and close on its own
button, logout, death, region change, superseding modal, or script cancellation.
Reconnect must resume from durable primary/side state, never from a temporary
scene local.

## 7. Reward and integration defects

`~toh_quest_complete` writes 165 before delivering anything. It then checks
only `inv`, not bank/owned/reclaim state, adds the flail and medallion
independently when a slot happens to exist, attempts to add three separate
Tomes, and always opens the completion scroll. With a full inventory the quest
is permanently complete with missing rewards; with banked rewards it can create
duplicates.

Completion must be a claim-safe transaction:

- preserve 150/155/160 until all mandatory final dialogue has run;
- reserve/compute net capacity or persist an explicit pending reward claim;
- commit 165, one quest point, one flail, one medallion, and one three-use Tome
  exactly once;
- clear or retain quest items according to pinned destroy/reclaim contracts;
- unlock music and world transforms idempotently; and
- make repeat click, disconnect, relog, and `::complete` policies explicit.

Downstream metadata lists A Night at the Theatre and Sins of the Father, but no
content-level `%myq4 >= 165` check was found in either quest. Both must reject
164 and accept 165 while retaining their other prerequisites. Later quests
also own medallion destinations/reclaims, Tome reclaim, Vertida dialogue, and
flail upgrade; those integrations need pairwise overlap tests rather than
first-trigger-wins assumptions.

## 8. Target modern architecture

The intended shape is content-owned policy over reusable engine services:

```text
cache-native durable state + side flags
                 |
       quest state controller/journal
                 |
     +-----------+------------+
     |           |            |
shared NPC   item recipe   cutscene/instance
dispatch     transactions  controllers
     |           |            |
     +------ combat/death -----+
                 |
     atomic rewards + integrations
```

Keep dialogue, exact stage policy, recipes, route coordinates, boss mechanics,
and rewards in RuneScript/config. Reuse modern named modal, cutscene, combat,
instance, teleport, music, item, and quest-completion services. Add C/VM work
only for a proven general gap such as retaliation suppression; do not add a
quest-specific native state machine.

## 9. Implementation sequence

### TOH-1 — correct identity, gates, transforms, and state semantics

- bind all live transforms (`myq4_garth_vis`, `myq4_harpert_vis`, rubble
  `_op`, mercenaries, window, and every base-era NPC) through canonical owners;
- enforce the native prerequisite and five non-boostable skills;
- name every primary and side value with native checkpoint semantics;
- replace the coarse journal with state/side/item-aware objectives; and
- capture live client values for 5/10, 25/35, 81–86, 95/100, 115, 130,
  155/160, and all three side varbits.

Exit evidence: a legitimate player can accept or refuse, every click target
resolves, no carrier/transform trigger is orphaned, and journals match all
native values.

### TOH-2 — implement opening, mercenaries, roof, and espionage

- build exact Garth/Safalaan/mercenary/Harpert dialogue and coin transaction;
- implement each roof move and elevation independently;
- implement authoritative steam timing, damage, knockback, and cancellation;
- build the red-window cutscene and resource restoration; and
- advance 40 only through the window's completed scene/dialogue.

Exit evidence: route traces at every coordinate/plane, 0/999/1000+ coin cases,
steam boundary tests, scene interruption/restart, and no Safalaan bypass.

### TOH-3 — implement Serafina route and potion puzzle

- connect normal hideout/Meiyerditch travel and Serafina access;
- implement optional diary and side bit;
- implement four distinct supply sources, fountain fill, both recipes, and the
  deliberate first-door failure;
- implement Safalaan blood-vial exchange and the blood-potion unlock; and
- implement staged chest, notes Read/Destroy/reclaim, and hand-in.

Exit evidence: every legal/illegal item order, all capacities, repeats,
destroy/loss/reconnect, wrong loc, door state, and 80–90 checkpoint trace.

### TOH-4 — implement the Abomination scene and combat instance

- build all actors, deaths, Safalaan reveal, weakening transform, music, and
  restart-safe scene checkpoints;
- author melee/ranged/stat-drain combat and Vertida ally behavior;
- own private instance entry, escape, death, reconnect, and cleanup; and
- advance only from authoritative boss death and completed post-fight dialogue.

Exit evidence: 465-to-149 transform, 200 HP, protection/stat-drain matrix,
Vertida-only kill, player kill, safe spot, death/re-entry, logout, and cleanup.

### TOH-5 — implement notes, Rod recovery, and flail construction

- move Flaygian's notes to Vertida and implement Read/replacement;
- wire `myq4_veliaf_rod` to Veliaf's canonical dialogue;
- implement independent sickle/chain crates and duplicate rules;
- implement emerald/chisel, Lvl-2 Enchant/tablet, and final three-component
  transactions; and
- implement flail affinity, Bloom, autocast, Retainer, shops, and replacement.

Exit evidence: level/spellbook/rune/tablet matrices, capacities, all use-on
directions, loss/reclaim, vampyre tiers, Retainer eligibility/timeout, and no
conjured finished item.

### TOH-6 — implement Ranis instance, deathbank, and finale

- build Kael readiness/equipment checks and instance admission;
- implement Ranis initial, 65%, 25%, add, flying, blood-bomb, explosion,
  Blood Barrage, and enrage controllers;
- implement escape citizen, deathbank chest/20,000 fee, unsafe-death policy,
  reconnect, and cleanup;
- build authoritative death/corpse/Kael/Safalaan/Vanescula finale; and
- preserve and journal native 145/150/155/160 checkpoints.

Exit evidence: threshold boundary tests, flail-only damage, four adds, bombs
damaging adds, phase attack timing/max hits, every exit/death/retrieval case,
and no Talk-to completion shortcut.

### TOH-7 — implement rewards, UI, music, and downstream contracts

- make completion and pending reward claim atomic/idempotent;
- implement one three-chapter Tome and durable XP selector;
- implement medallion teleports/unlocks/reclaims and Wilderness/lifecycle rules;
- unlock all five music tracks at their authoritative beats;
- enforce A Taste in both downstream quests and preserve shared NPC priority;
  and
- make cheat completion coherent without granting repeat rewards.

Exit evidence: capacities 0–28, banked/owned items, disconnect boundaries,
chapter counter/skills, all medallion eras, music, state164/165 downstream
tests, and double-cheat behavior.

### TOH-8 — remove scaffolding and verify end to end

- remove every `Soft-skip`, NPC Talk-to combat shortcut, direct finished-item
  grant, synthetic travel teleport, and harness-only progression assumption;
- run script/cache builds and quest-specific automated suites;
- run a real-client 0-to-165/postquest smoke with both death paths; and
- record exact commands, traces, packets/screenshots, and justified cosmetic
  deviations in this file before changing status.

## 10. Verification matrix

| Scenario | Required assertions |
| --- | --- |
| Start gates | DoH incomplete or any skill one level low cannot accept; boosts do not satisfy; No remains 0; Yes follows 5/10 then 15 |
| Live transforms | Garth, Harpert, all mercenaries, rubble `_op`, red window, base NPCs, and Kael resolve for every relevant primary value |
| Opening | Garth scene actors/order/re-talk work; Safalaan and any mercenary advance only at their exact checkpoints |
| Harpert | 0/999 coins refuse or fail safely; exactly 1,000 removed on confirmed acceptance; repeat cannot charge twice |
| Roof | Every obstacle reaches exact coordinate/plane; reverse/repeat/wrong-state safe; steam inactive succeeds and active deals 15–20 plus knockback |
| Window scene | Correct actors/dialogue/camera/music; completion restores HP/prayer/run; click/logout/reconnect cannot skip or duplicate |
| Shared NPCs | Safalaan/Vertida dispatch Darkness, A Taste, Sins, and defaults without starvation for every pairwise overlapping state |
| Serafina travel | Normal exit/path/stairs work; generic door logic cannot open locked room before blood potion |
| Diary | Bed gives one diary and side bit; Read/loss/replacement/capacity are correct and optional |
| Supplies | Each source gives only its own item; duplicates, full inventory, destroy/re-search, and wrong state are safe |
| First potion | Fountain, herb, vial, pestle, meat, and unfinished/finished items consume exactly; using potion on door fails and writes only intended state |
| Blood potion | Empty-vial exchange, second herb/meat recipe, door confirmation, unlock, repeats, and cancellation are atomic |
| Old notes | Closed/open chest stages, Read, hand-in, destroy/reclaim, and capacities preserve 85/86 correctly |
| Abomination scene | All three Myreque deaths, Safalaan levitation/explosion, 465-to-149 transform, music, and restart checkpoints match evidence |
| Abomination combat | 200 HP; melee/ranged; 1–3 stat drain; Protect from Missiles behavior; Vertida immortal/low damage; death/re-entry/cleanup exact |
| Old base | Trapdoor/ladder directions and access work at 110–160 and preserve unrelated generic door behavior |
| Flaygian notes | Vertida delivery, Read, Rod eligibility, destroy/reclaim, and full inventory cannot skip 120 |
| Components | Sickle and chain sources are independent; emerald/chisel and enchant requirements exact; every item-use direction and cancel path safe |
| Flail | Three inputs become one flail once; 40 Attack wield; vampyre tiers/full damage/20% bonus; Bloom/autocast; purchase/reclaim exact |
| Retainer | Only juvenile/juvinate below 50%; correct failure messages; 10% energy and delay; 30-second retaliation suppression; no freeze substitution |
| Ranis admission | Flail equipped and ready choice required; refuse/escape/re-entry do not advance; instance isolation works for simultaneous players |
| Ranis phases | 400 HP; initial melee/magic/Blood Barrage/explosion; exact 65% and 25% flying/add waves; four level-87 adds total; bomb targeting/damage exact |
| Ranis enrage | Second wave death triggers speed-2/max-6 melee form; protection and accuracy behavior match evidence; only authoritative death writes 145 |
| Ranis lifecycle | Escape/quick-escape, teleport/logout/disconnect/death/reconnect, ground items, instance teardown, and stale NPC clicks are safe |
| Deathbank | Chest only owns Ranis losses; 20,000 fee, capacity, repeat claim, unsafe subsequent death, and empty chest are exact |
| Finale | Corpse, Kael, Safalaan, Vanescula, music, cameras, and 145–160 re-talks all complete before 165 |
| Completion capacity | 0–28 used slots, banked rewards, repeat click, disconnect around commit, and relog yield exactly one claim set and 1 QP |
| Tome | One object, chapters 0–3, non-bankable, level 34/35 boundary, cancel/reconfirm, same skill thrice, exact 2,500 XP, crumble, destroy/reclaim |
| Medallion | Ver Sinhaza at 165; Darkmeyer/Slepe/Castle gates; level-20 Wilderness boundary; combat/teleblock/modal cancellation; every reclaim era |
| Music | Five tracks unlock at exact location/scene beats once and survive relog/cheat policy |
| Downstream | A Night and Sins reject 164 and accept 165 while all other prerequisites and shared-dialogue branches remain intact |
| Journal | Every primary value, potion substate, instance/death/reclaim state, missing item, and complete state names the correct next action |
| Cheat adapter | First complete establishes coherent 165/world/integration policy; second is a no-op and grants no rewards/XP/music twice |

Minimum repository checks after implementation:

```sh
python3 tools/questhelper_extract.py atasteofhope --check
make -C src mock230-scripts
mock230_pack --check-only
```

Also run quest-specific state, transform, item, cutscene, combat, instance,
deathbank, UI, music, shared-NPC, and downstream suites. Capture real-client
packets/screenshots for both cutscenes, both fights, deathbank, completion,
Tome selection, and each medallion menu state. Compilation cannot prove a
coin transaction, threshold controller, instance teardown, or atomic reward.

## 11. Definition of done

A Taste of Hope may be marked `verified-modern` only when:

- a legitimate Darkness-complete player meeting all five unboosted skill gates
  can play from live Garth state 0 through the full finale to 165 without a
  debug proc, state assignment shortcut, or text stand-in;
- every native primary and side value is named, durable, journaled, and written
  only by its authoritative world, item, scene, combat, or reward action;
- all live cache transforms are correctly bound and shared Safalaan/Vertida
  ownership preserves Darkness, A Taste, Sins, and default dialogue;
- mercenaries, Harpert's payment, the timed roof route, espionage cutscene,
  restoration, travel, and all re-talk/refusal branches match pinned evidence;
- both Serafina recipes, the deliberate failed potion, blood exchange, locked
  door, diary, chest, notes, destruction, replacement, and capacity paths are
  exact and bypass-proof;
- the Abomination scene and fight use authoritative actors, transforms,
  attacks, stat drain, ally, instance, death/re-entry, music, and kill signal;
- the old base, notes, Rod recovery, components, crafting/enchanting, full
  Ivandis combat affinity, Bloom, autocast, Retainer, shop, and reclaim
  contracts work without conjured finished items;
- Ranis is a private, deathbank-backed, flail-only multi-phase boss with exact
  thresholds, four adds, bombs, explosion, healing, enrage, escape, death,
  reconnect, corpse, and finale behavior;
- completion is atomic/idempotent and yields exactly 1 QP, one flail, one
  medallion, and one three-chapter Tome, with all selector/teleport/reclaim
  behavior and no full-inventory loss or duplicate value;
- all five music tracks, both downstream quest gates, cheat policy, and later
  Myreque-era shared ownership are durable and tested; and
- no active `Soft-skip`, Talk-to boss kill, direct final-item grant, synthetic
  travel teleport, numeric legacy UI opener, or harness-only assumption remains,
  with script/cache builds, automated suites, and real-client evidence recorded
  here.
