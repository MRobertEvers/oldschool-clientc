# Creature of Fenkenstrain modernization audit

Status: `audit-pending` — the native quest row, castle and dungeon maps, most
actors/objects/items, modern dialogue choices, a journal, an organic completion
call, the correct headline reward, and downstream quest stubs exist. The
canonical route does not work. The implementation advances a parallel packed
`%creatureoffenkenstrain` varbit while native morphs, Quest Helper, and
`::complete` use `%fenk_quest` varp 399. It then reverses the grave types: the
Mausoleum's ornate graves cannot yield the torso, arms, or legs, while arbitrary
Haunted Woods graves can. Cavern, mausoleum, shed, furnace, conductor, and tower
mechanics are deliberately bypassed or left to ungated generic doors. A player
can force completion through undocumented shortcuts, but not by following the
current quick guide. This is a legacy soft implementation, not a modern quest.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to both progress carriers, the native `%fenk_flags`
layout, sign and interview, Roavar/brain ownership, bookcases and amulets,
Gardener Ghost following, every grave, Experiment Cave and key, body-part and
tool recovery, shed and brush construction, furnace crafting, conductor
world-state, tower route, Ring of Charos completion/reclaim, journal/debug
adapters, and all permanent/downstream unlocks. It is an implementation
specification, not verification evidence.

## 1. Authoritative references

The Wiki article and quick guide define requirements, route, mechanics, rewards,
and unlocks. The transcript defines sign confirmation, interviews, re-talks,
item ownership, grave-specific responses, key behavior, failure dialogue, and
post-quest state. Revisions were resolved through the OSRS Wiki API on
2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Creature of Fenkenstrain](https://oldschool.runescape.wiki/w/Creature_of_Fenkenstrain?oldid=15292324) | 15292324, 2026-08-10 | Identity, requirements, full route, rewards, and dependencies |
| [Creature of Fenkenstrain/Quick guide](https://oldschool.runescape.wiki/w/Creature_of_Fenkenstrain/Quick_guide?oldid=15266785) | 15266785, 2026-07-18 | Exact route order, objects, items, dialogue answers, and coordinates |
| [Transcript:Creature of Fenkenstrain](https://oldschool.runescape.wiki/w/Transcript%3ACreature_of_Fenkenstrain?oldid=15263305) | 15263305, 2026-07-14 | Start confirmation, actor branches, graves, item hand-ins, finale, and post-quest dialogue |
| [Dr Fenkenstrain](https://oldschool.runescape.wiki/w/Dr_Fenkenstrain?oldid=15207388) | 15207388, 2026-05-06 | Shared actor states, pickpocket, post-quest move, and ring recovery |
| [Gardener Ghost](https://oldschool.runescape.wiki/w/Gardener_Ghost?oldid=15242419) | 15242419, 2026-06-29 | Ghostspeak, head directions/following, history, and shed key |
| [Roavar](https://oldschool.runescape.wiki/w/Roavar?oldid=14992283) | 14992283, 2025-09-25 | Pickled-brain purchase ownership and Canifis inn dialogue |
| [Lord Rologarth](https://oldschool.runescape.wiki/w/Lord_Rologarth?oldid=14888002) | 14888002, 2025-04-20 | Creature identity, tower dialogue, and permanent castle state |
| [Experiment cave](https://oldschool.runescape.wiki/w/Experiment_cave?oldid=15234162) | 15234162, 2026-06-16 | Dungeon topology, permanent partial unlock, gate, chest, and monsters |
| [Experiment](https://oldschool.runescape.wiki/w/Experiment?oldid=15199186) | 15199186, 2026-04-28 | Level variants, combat, key source, and post-quest behavior |
| [Grave (Haunted Woods)](https://oldschool.runescape.wiki/w/Grave_%28Haunted_Woods%29?oldid=14684724) | 14684724, 2024-06-20 | Ed Lestwit's exact grave and empty results for other wooden graves |
| [Grave (Creature of Fenkenstrain)](https://oldschool.runescape.wiki/w/Grave_%28Creature_of_Fenkenstrain%29?oldid=15202637) | 15202637, 2026-04-29 | Mausoleum west/middle/east torso/arms/legs mapping |
| [Pickled brain](https://oldschool.runescape.wiki/w/Pickled_brain?oldid=15183774) | 15183774, 2026-04-22 | Brain acquisition and head combination |
| [Cavern key](https://oldschool.runescape.wiki/w/Cavern_key?oldid=15184609) | 15184609, 2026-04-22 | Level-51 drop, gate use, attack restriction, and post-quest chest source |
| [Shed key](https://oldschool.runescape.wiki/w/Shed_key?oldid=15183775) | 15183775, 2026-04-22 | Gardener source and one-time shed unlock |
| [Tower key](https://oldschool.runescape.wiki/w/Tower_key?oldid=15184612) | 15184612, 2026-04-22 | Doctor source and one-time tower unlock |
| [Garden brush](https://oldschool.runescape.wiki/w/Garden_brush?oldid=15183660) | 15183660, 2026-04-22 | Shed/cupboard source and extension recipe |
| [Garden cane](https://oldschool.runescape.wiki/w/Garden_cane?oldid=15184611) | 15184611, 2026-04-22 | Three-cane collection and bronze-wire assembly |
| [Conductor mould](https://oldschool.runescape.wiki/w/Conductor_mould?oldid=15184048) | 15184048, 2026-04-22 | Correct fireplace, furnace use, Crafting gate, and loss behavior |
| [Conductor](https://oldschool.runescape.wiki/w/Conductor?oldid=15184610) | 15184610, 2026-04-22 | Silver lightning-rod crafting and repair use |
| [Ring of charos](https://oldschool.runescape.wiki/w/Ring_of_charos?oldid=15217181) | 15217181, 2026-05-26 | Reward effects, death/loss, bank-aware recovery, and later doctor relocation |
| [Werewolf Agility Course](https://oldschool.runescape.wiki/w/Werewolf_Agility_Course?oldid=15239940) | 15239940, 2026-06-26 | Completion reward access contract |
| [Garden of Tranquillity](https://oldschool.runescape.wiki/w/Garden_of_Tranquillity?oldid=15293044) | 15293044, 2026-08-11 | Full completion dependency and ring activation |
| [The Great Brain Robbery](https://oldschool.runescape.wiki/w/The_Great_Brain_Robbery?oldid=15301585) | 15301585, 2026-08-14 | Full completion dependency and Fenkenstrain relocation |
| [Rag and Bone Man II](https://oldschool.runescape.wiki/w/Rag_and_Bone_Man_II?oldid=15292349) | 15292349, 2026-08-10 | Partial-completion dependency and Experiment bone |
| [Priest in Peril](https://oldschool.runescape.wiki/w/Priest_in_Peril?oldid=15292273) | 15292273, 2026-08-10 | Required completion and Morytania access |
| [The Restless Ghost](https://oldschool.runescape.wiki/w/The_Restless_Ghost?oldid=15268042) | 15268042, 2026-07-20 | Partial-completion/ghostspeak prerequisite |
| [Letter (Creature of Fenkenstrain)](https://oldschool.runescape.wiki/w/Letter_%28Creature_of_Fenkenstrain%29?oldid=15187118) | 15187118, 2026-04-22 | Optional clock lore and replacement behavior |

The current contract is a members, intermediate, medium quest released on 31
January 2005. It requires completion of Priest in Peril and partial completion
of The Restless Ghost. Level 20 Crafting and 25 Thieving are boostable and are
not required merely to start; they are checked at the actions that use them.
The player must be able to defeat one level-51 Experiment and pass level-72
Experiments.

Required supplies are a ghostspeak amulet, silver bar, three unnoted bronze
wires, ordinary needle, five thread, spade, and either 50 coins or level 33
Magic with one law and air rune for the pickled brain. Completion awards 2
quest points, 1,000 Thieving XP, and the Ring of Charos. The ring permits
access to the Werewolf Agility Course and is later activated during Garden of
Tranquillity. Full completion gates Garden of Tranquillity and The Great Brain
Robbery; opening the Experiment Cave is enough for the relevant Rag and Bone
Man II content.

Transition aid only: the local Quest Helper implementation at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/creatureoffenkenstrain)
maps native varplayer 399 through route states 0–6 and resolves 30 item symbols,
four NPCs, twelve locs, nine side-state varbits, and all principal coordinates.
`python3 tools/questhelper_extract.py creatureoffenkenstrain --check` exits 0.
Quest Helper is a transition aid, not proof of server collision, doors, combat
drops, inventory ownership, world-state visibility, or completion atomicity.

## 2. Native identity and progress contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 71 |
| Type / difficulty / length | Members; intermediate; medium |
| Start | `fenk_signpost` in central Canifis; Doctor can also begin the interview |
| Prerequisite quests | Priest in Peril complete; The Restless Ghost partially complete |
| Skills | 20 Crafting and 25 Thieving, both boostable and checked at use, not start |
| Recommended combat | 30 |
| Primary state | `%fenk_quest`, native permanent transmitted varp 399 |
| Side state | `%fenk_flags`, native permanent transmitted varp 400 |
| Quest points | 2 |
| Completion XP | 1,000 Thieving (`10000` tenths) |
| Item reward | Ring of Charos |
| Permanent unlocks | Experiment Cave, Werewolf Agility Course via ring, post-quest castle actors |
| Downstream | Garden of Tranquillity; The Great Brain Robbery; partial Rag and Bone Man II |
| End state | 9 |

The dbrow correctly declares the two skills, boostability, no start-time skill
check, 2 quest points, 1,000 Thieving XP, state 9, release metadata, and start
loc. Its quest requirement rows cannot by themselves express “partial Restless
Ghost”; runtime and journal presentation must preserve that exception without
claiming full completion is required.

### 2.1 Primary state split is the first blocking defect

The implementation uses `%creatureoffenkenstrain`, bits 2–5 of the unrelated
`%scorpcatcher_secondary` carrier, as its main story state. Native varp 399 is
also declared as `%fenk_quest`, but organic code leaves it at 0 until the
conductor is repaired and then writes shifted values. Native `fenk_fenkenstrain`,
`fenk_creature`, `fenk_creature_released`, `fenk_fenkenstrain_in_tower`, and
`fenk_table_multi` all morph from `%fenk_quest`. Quest Helper reads varplayer
399. The `::complete` adapter and POH crest logic also read/write `%fenk_quest`.
The journal, Roavar, Garden of Tranquillity, and The Great Brain Robbery instead
read `%creatureoffenkenstrain`.

That is not harmless duplicated bookkeeping. `tools/loc_var_audit.py --var
fenk_quest` finds the native ritual table morph at one placed castle loc; the
corpse appears at value 3. Current code reaches its authored “lightning” phase
without writing `%fenk_quest=3`, so the sewn creature is never displayed.
`::complete` writes only `%fenk_quest=9`, leaving the journal and both downstream
quest gates at not-started. Organic completion writes both, concealing the split
only at the final action.

### 2.2 Expected state ladder versus current writes

| Native `%fenk_quest` | Canonical phase / Quest Helper step | Current parallel state and native write |
| ---: | --- | --- |
| 0 | Not accepted; obtain brain; sign/interview available | `%creatureoffenkenstrain` 0→1 for sign, then 2 for hire; native remains 0 |
| 1 | Hired; collect head, brain, torso, arms, and legs | Parallel 2; native remains 0 |
| 2 | Body parts delivered; give needle and five thread | Parallel 3; native remains 0 |
| 3 | Body sewn; make and repair conductor; corpse table visible | Parallel 4; native remains 0 |
| 4 | Conductor repaired; return to Doctor | Parallel 5 and native **5**, skipping 1–4 |
| 5 | Tower key received; speak to Rologarth | Parallel 6 and native **6** |
| 6 | Rologarth revealed truth; pickpocket Doctor | Parallel 7 and native **7** |
| 9 | Complete; Doctor moves, Rologarth released | Both carriers written 9 organically; cheat writes native only |

Values 7 and 8 are not route steps in Quest Helper; value 9 is the native
dbrow end state. Modernization must make `%fenk_quest` the sole primary state,
migrate or reconcile existing divergent saves, update every downstream reader,
and reserve `%creatureoffenkenstrain` only if an independently proven cache
consumer requires it. A migration must handle every pair of values, not merely
copy the larger number.

### 2.3 Native side-state inventory

| `%fenk_flags` field | Native shape | Current use / mismatch |
| --- | --- | --- |
| `%fenk_arms`, `%fenk_legs`, `%fenk_torso`, `%fenk_head` | Four 1-bit flags | Set on excavation, then both flag and item are required at hand-in; dropping an item can make it unobtainable |
| `%fenk_needle` | 1 bit | Set after deleting the needle; exact return/consumption behavior needs live confirmation |
| `%fenk_threads_given` | 3 bits | Correctly supports 0–5 partial thread hand-in |
| `%fenk_coffin` | 1 bit | Set after consuming star amulet; used to allow repeated teleport entry |
| `%fenk_spoken_to_gardener` | 1 bit | Used as a hard head-dig prerequisite although current route does not require speaking first |
| `%fenk_gardener_directions` | 1 bit | Never written; native following/direction state absent |
| `%fenk_conductor_repaired` | 1 bit | Written, but no loc transform or visual replacement reads it |
| `%fenk_wound_clock` | 1 bit | Never used; optional letter scene absent |
| `%fenk_counter` | 5 bits | Never used; investigate native dialogue/action counter ownership |
| `%fenk_digresult` | 3 bits | Never used; grave identity/result behavior replaced by loc-type shortcuts |
| `%fenk_unlocked_tower` | 1 bit | Set on Doctor dialogue; door still uses key/inventory and a teleport shortcut |
| `%fenk_unlocked_cavern` | 1 bit | Set on soft cave entry; not connected to a real keyed route or partial dependency |
| `%fenk_unlocked_shed` | 1 bit | Never written; generic door opens for everyone |
| `%fenk_read_signpost` | 1 bit | Written with a parallel primary state; no Yes/No start confirmation |

Preserve newer/unrelated bits sharing the carrier, including bridge-state
fields, during reset and migration. Debug reset currently clears only a subset
of the native fields and is not a safe migration model.

## 3. Implementation surface

The quest root contains 814 lines in two configs and four scripts.

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `configs/fenkenstrain.constant` | Shifted states, requirements, XP, and soft coordinates | State model conflicts with native varp; every route coordinate is labelled soft |
| `configs/fenkenstrain.varp` | `%fenk_quest`, `%fenk_flags`, and secondary carrier declarations | Native carriers exist, but the quest is driven from the wrong packed varbit |
| `scripts/fenkenstrain.rs2` | Requirements, sign, Doctor, journal, debug adapters | Modern choices exist; start semantics, state ownership, dialogue, journal, and debug coherence fail |
| `scripts/fenkenstrain_parts.rs2` | Bookcases, amulets, memorial, cave key, gardener, graves, brain, body hand-in | Loc-type routing is reversed; dungeon/key/recovery behavior is bypassed |
| `scripts/fenkenstrain_lightning.rs2` | Needle/thread, cupboard, brush, mould, conductor, repair | Shed key, canes/wire, Crafting check, furnace UI/XP, and conductor world-state are absent |
| `scripts/fenkenstrain_finish.rs2` | Tower door, creature, pickpocket completion and ring reclaim | Wrong-plane teleport/dynamic NPC; non-atomic completion; storage-blind reclaim |

Mandatory cross-directory surfaces include:

| Surface | Relationship / modernization requirement |
| --- | --- |
| `configs/all.dbrow` | Preserve ID 71, end 9, skill/boost policy, 2 QP, 1,000 XP, and partial prerequisite exception |
| `configs/all.varp` / `all.varbit` | Native varp 399, `%fenk_flags` layout, and the conflicting packed varbit |
| `configs/all.npc` | Doctor, tower Doctor, Rologarth forms, Gardener multis, and three Experiment variants |
| `configs/all.loc` | Sign, bookcases, memorials, graves, keyed doors, chest, cupboard, cane pile, fireplace, table, conductor, clock, stairs/ladders |
| `configs/all.obj` | Brain, amulets, keys, body parts, brush/canes, mould/conductor, letter/journal, and Ring of Charos |
| `areas/world/configs/m54_54.spawn` | World pickled-brain spawn at `(3492, 3474)` |
| `areas/world/configs/m55_55.spawn` | Native castle Doctor/Gardener/Rologarth placements and state-driven tower actors |
| `areas/world/configs/m54_155.spawn`, `m55_155.spawn` | Full shared Experiment Cave population already placed |
| `areas/area_canifis/scripts/werewolfinnkeeper.rs2` | Roavar dialogue currently exposes brain purchase only after the shifted “hired” state |
| `drop_tables/scripts/wiki_experiment.rs2` | Level-25 variants currently drop cavern keys without quest/key checks |
| generic doors | Mausoleum and shed doors open without quest-specific keys; tower closed form is exact-bound by quest |
| generic ladders/stairs maplinks | Castle and cave topology should be used instead of direct teleports |
| Crafting furnace/jewellery system | Must own silver conductor option, level check, bar use, and 50 Crafting XP |
| `quest_gardenoftranquility` | Full prerequisite and Ring of Charos activation; currently reads parallel state |
| `quest_thegreatbrainrobbery` | Full prerequisite, shared Doctor routing/move, and later ring recovery; currently reads parallel state |
| Rag and Bone Man II / Experiment drops | Partial cave unlock and optional Experiment bone |
| Werewolf Agility Course | Reward access implementation was not found in focused production search |
| `interface_questjournal` | Correctly dispatches the dbrow to `~fenkenstrain_journal`, which reads the wrong carrier |
| `quests/scripts/questpoints.rs2` | Modern completion scroll/QP/count/jingle lifecycle |
| `quests/scripts/quest_cheat.rs2` | Correctly targets native varp but leaves authored consumers incomplete |
| POH crest logic | Correctly checks native `%fenk_quest`; preserve it during unification |

No legacy IF1 panel open or raw numeric entity ID occurs in the quest root.
The current problems are not missing modern chatmenu capability; they are
state ownership, world interaction, transaction, and deliberately simplified
gameplay.

## 4. Start, interview, and requirements

| Canonical behavior | Current behavior / consequence |
| --- | --- |
| Reading sign offers `Start the quest?` Yes/No, then records the notice | The notice opens immediately with no confirmation; only players passing a custom aggregate check get shifted state 1 |
| Doctor can conduct the interview directly, including when sign reports unmet skill requirements | State-0 Doctor refuses the interview and orders the player back to the sign; a player below/without a temporary boost cannot start |
| Crafting 20 and Thieving 25 are boostable action requirements, not start gates | `~fenk_has_requirements` checks current boosted stats before sign start; Crafting is never checked at brush/conductor work; Thieving is checked at pickpocket |
| Priest in Peril must be complete; only partial Restless Ghost is required | Runtime thresholds broadly express this, but journal says both quests are required and metadata cannot express partial Restless Ghost by itself |
| Wrong interview answer ends at the appropriate question; No says the full refusal | Current script asks the second question even after a wrong first answer and only rejects after both; refusal text is reduced to “No.” |
| Accepted state is native 1 | Current writes packed value 2 and leaves native state 0 |

The sign is the canonical start loc, but it is not permission to convert
boostable mid-quest requirements into a start gate. Use the native quest-start
confirmation and native state 1 only after the successful interview. Requirement
UI should explain unmet action requirements while still permitting the route
where the current game does.

The in-progress Doctor menu omits transcript branches asking why he wants the
creature and whether it will replace the player. It adds “I have some body
parts” as a top-level choice but only accepts all four at once, whereas the
transcript acknowledges each present part before concluding when the set is
complete. Preserve no-part and partial-part re-talks without deleting anything
until the intended hand-in policy is established.

## 5. Brain, bookcases, gardener, and grave ownership

### 5.1 Pickled brain purchase is not attached to the ground item

The native brain is spawned on Roavar's table before the quest. Canonically,
attempting to pick it up starts Roavar's 50-coin purchase dialogue; Telekinetic
Grab is the alternative. Trying again while owning one is refused. The current
ground item has no quest-specific pickup interception, so generic pickup takes
it for free. Roavar's optional Talk-to purchase appears only from shifted
“hired” state 2, too late for the recommended pre-start collection, and can sell
duplicates. It deletes coins before an explicit free-space check.

Implement ownership on both manual pickup and Telekinetic Grab, handle coins,
capacity, duplicate inventory/bank/ground ownership, and make the ordinary
Roavar conversation coexist with later Great Brain Robbery/Canifis content.

### 5.2 Bookcases ignore room identity

The west upstairs bookcase's “Joy of Gravedigging” supplies the marble amulet;
the east bookcase's “Handy Maggot Avoidance Techniques” supplies the obsidian
amulet. Each also contains its own non-quest book choices. Current code binds
all `fenk_bookcase` locs to one four-choice menu and allows either amulet from
any placement. It has no explicit inventory-full handling and no location- or
bookcase-specific compartment state.

Use active loc coordinate/identity, preserve the separate menus, and define
recovery while the amulet is still needed. Combining pieces is correctly
implemented as bidirectional item-on-item and consumes one of each, but recovery
after placing the star must not create post-unlock duplicate amulets.

### 5.3 Gardener following and grave results are replaced by global shortcuts

The transcript offers Gardener history, head directions, name, later shed-key
and conductor-mould help. With a ghostspeak amulet, asking about the head can
make him follow for ten minutes and give directional overhead text until Ed
Lestwit's exact grave is reached. Native `%fenk_gardener_directions` exists for
this flow. Current dialogue has one fixed head explanation, requires a
ghostspeak/humanspeak amulet, sets `%fenk_spoken_to_gardener`, and never follows.
It has no shed-key or mould branches.

More seriously, the excavation logic confuses loc types:

- `fenk_grave_poor` is the wooden Haunted Woods family. After cave unlock,
  current code gives torso, then arms, then legs from any such grave.
- `fenk_grave` is the ornate Mausoleum family. Current code never gives the
  three body parts from it; it falls through to the generic head check.
- before cave unlock, any grave of either type can give Ed's head after one
  Gardener conversation; the authored `^fenk_ed_grave` coordinate is unused;
- every Read action prints “Here lies someone” rather than the named transcript
  inscriptions; and
- west/middle/east Mausoleum identity is ignored.

Thus a player following the quick guide reaches the Mausoleum and cannot obtain
the required parts. An informed player can return to arbitrary Haunted Woods
graves and receive them in a fixed sequence. This is a deterministic canonical
route blocker.

Flags are set when each item is dug up, yet hand-in later requires the physical
item too. If a head, torso, arms, or legs is dropped or lost, the set flag blocks
another excavation and the quest can become permanently stuck. Modernization
must give exact-coordinate results, define whether each bit means excavated or
delivered, and support current-game recovery without enabling duplicates across
inventory, bank, ground ownership, death storage, and hand-in.

## 6. Experiment Cave, combat, key, and Mausoleum

Placing the star amulet currently consumes it and sets `%fenk_coffin`, which is
a useful persistent unlock seed. Pushing the memorial then directly teleports
to `(3496, 9960)`, prints “(Soft)”, and adds a temporary level-51 Experiment at
the landing. The real dungeon maps and populations are already present. The
canonical player should enter through the opened memorial, traverse the cave,
kill the designated level-51 wolf-woman, unlock the north-west gate with the
cavern key, search the chest/exit, climb the ladder, and reach the Mausoleum.

Current key behavior conflicts in three places:

1. The quest's `fenk_experiment_1` death handler puts a key directly into the
   killer's inventory rather than producing the expected owned ground drop.
2. The Wiki-generated handlers for `fenk_experiment_2` and `_3` always create a
   cavern key, so level-25 variants and post-quest kills farm keys too.
3. `fenk_mausoleum_door` is registered as an ordinary generic door with no key
   check. The key is never consumed/read by a production gate.

The canonical restriction that another level-51 wolf-woman cannot be attacked
while carrying a key or after completion is absent. The post-quest chest key
source is absent even though `fenk_chest_closed/open` configs exist. The partial
cave unlock is not exposed coherently to Rag and Bone Man II because native
`%fenk_quest` remains 0 during the entire body-part phase.

Restore the real route, per-player memorial/unlock presentation, designated
combat/drop ownership, gate and chest lifecycle, bidirectional ladder/maplinks,
and post-quest access. Test death, relog, key loss, stolen/expired ground drops,
and simultaneous players. Do not spawn a duplicate Experiment when the map
already supplies the encounter.

## 7. Sewing, shed, brush, furnace, and lightning

Needle/thread tracking is the strongest existing subflow. Thread may be handed
in partially and `%fenk_threads_given` records 0–5. Once both requirements are
met, dialogue advances to the lightning task. It still writes the wrong primary
carrier and never makes the ritual-table corpse appear. Verify from live OSRS
whether the ordinary needle is returned or consumed; Quest Helper marks it
non-consumed while the transcript says Fenkenstrain uses it.

The rest of this phase bypasses its mechanics:

| Canonical action | Current implementation / defect |
| --- | --- |
| Ask Gardener for shed key; key drops to owned ground if inventory full | No dialogue branch, no key grant, `%fenk_unlocked_shed` never changes |
| Locked shed requires key once, then remains unlocked | Generic door opens for every player without key |
| Search cupboard for brush | Search works only after manually opening a temporary world-shared loc; basic duplicate check only |
| Take three separate canes | Clicking the pile never gives `fenk_cane`; it directly upgrades a carried brush |
| Use each cane on brush with one bronze wire; enforce boostable Crafting 20 | No bronze wire check or consumption, no item-on-item operation, no Crafting check |
| Only correct upstairs west fireplace yields mould | Exact loc-name handler is not coordinate-specific and omits other-fireplace results |
| Use furnace UI with mould and silver bar to craft conductor | Using mould directly on silver bar works anywhere; furnace and modern Crafting interface are skipped |
| Award 50 Crafting XP for conductor | No action XP is awarded |
| Repair roof conductor and show lightning/world change | Item is deleted and flags/states change after one click; no animation sequence; broken loc remains because no transform reads the repaired bit |

The conductor mould is retained after crafting and may continue making rods
after the quest, but cannot be reacquired after completion if lost. Implement
that lifecycle through the shared furnace system rather than a quest-local
item-on-item shortcut. The exact furnace option must re-arm correctly after
interface remount, apply current boosted Crafting at the action, consume one
silver bar, award 50 Crafting XP, retain the mould, and handle capacity/repeats.

`tools/loc_var_audit.py --var fenk_conductor_repaired` finds no loc transform.
Use a player-visible modern state mechanism supported by the map/cache, with a
relog-safe repaired result and no global cross-player lightning leakage.

The optional clock/letter path is wholly absent: `%fenk_wound_clock` is never
written and no `fenk_clock`, `fenk_letter`, or `fenk_journal` production handler
was found. This does not block completion but is a transcript and lore gap.

## 8. Tower, Rologarth, completion, and reward lifecycle

After conductor repair, the current Doctor dialogue gives/replaces the tower
key and sets both a side unlock and shifted primary states. Capacity is not
checked before state advances, although later re-talk can retry the key. The
canonical route is upstairs, unlock metal door once, then climb the northern
ladder to the already placed Rologarth on plane 2.

Instead, the exact tower-door handler teleports directly to
`^fenk_tower_stand = 0_55_55_28_48`, a plane-0 exterior coordinate, and adds a
temporary Rologarth there for 500 ticks. Native world data already places
`fenk_creature` at `(3547, 3555, 2)`. The dynamic duplicate is shared world
state, has timeout/re-entry races, and bypasses the door/ladder topology. It
also makes source comments calling this “Soft” part of the active route.

Rologarth's first conversation has the core reveal, but compresses drunken
dialogue and the full historical explanation. The second conversation omits
the ring's werewolf-disguise power. The transcript's intermediate Doctor
confrontation (“So have you destroyed it?”) is absent; state 6 goes straight to
pickpocket completion.

Completion currently performs:

1. pickpocket animation and delay;
2. unconditional `inv_add(ring_of_charos)` without a capacity/ownership guard;
3. writes both end states;
4. grants 1,000 Thieving XP; and
5. invokes the shared completion helper for quest points/count/jingle/scroll.

State and XP precede the shared lifecycle and there is no state recheck after
the delay. Two overlapping pickpockets or an abort after the permanent write
can duplicate reward/XP or leave the quest complete without shared QP/count.
Full inventory behavior is undefined. Make this one guarded, idempotent
transaction and reject/retry safely before any mutation.

Post-quest pickpocket recovery correctly recognises both normal and activated
ring variants in inventory/worn containers, but ignores the bank, death storage,
ground ownership, Garden of Tranquillity's well state, and later Great Brain
Robbery location. Banking a ring can therefore allow another one. Use a shared
unique-item ownership/reclaim check that follows Fenkenstrain to Harmony Island
and respects the activated version.

Focused search found no Werewolf Agility Course access implementation despite
the ring being its headline permanent unlock. Completion text alone does not
deliver that reward. Implement and prove the course entrance/trainer route with
the ring equipped, including activated ring compatibility.

## 9. Journal, debug, and cross-quest consistency

The dynamic quest-list dispatcher correctly calls `~fenkenstrain_journal`; no
legacy per-quest IF1 component is used. The journal reads the parallel carrier,
shows full Restless Ghost as a requirement rather than partial completion,
states Crafting/Thieving as start requirements, cannot represent side-state
progress or lost-item recovery, and treats any value at least 9 as complete.
Rebuild it over native state and relevant side flags with exact invalid-state
diagnostics.

Debug helpers also expose the split:

- `::fenkenstrain` resets only selected side bits and sets Restless Ghost fully
  complete, not the minimum partial state;
- `::fenkparts` and `::fenklightning` force the parallel state while leaving
  native morph state at 0;
- `::fenkfinish` forces shifted native/parallel value 6, sets
  `%fenk_threads_given=1` rather than 5, and teleports to the wrong plane; and
- `::complete quest_creatureoffenkenstrain` correctly sets native 9 but leaves
  the journal, Garden of Tranquillity, and The Great Brain Robbery blocked.

After state unification, each debug helper must build a coherent snapshot and
the generic completion adapter must be idempotent. Organic and cheat completion
must produce the same permanent quest/unlock visibility, differing only in
narrative/action rewards as defined by shared debug policy.

Garden of Tranquillity and The Great Brain Robbery currently gate on the
parallel varbit and must move to native `%fenk_quest`. Great Brain Robbery's
shared Doctor conversation is already routed through the one quest-owned
Talk-to trigger, which is the correct no-duplicate-trigger pattern. Preserve
that ownership while adding correct actor-location and ring-reclaim behavior.

## 10. Defect ledger

| Priority | Defect | Player impact | Required proof |
| --- | --- | --- | --- |
| P0 | Primary state split/shift between packed varbit and native varp 399 | Native morphs, journal, cheat, downstream gates, and partial consumers disagree | Migration matrix plus every native 0–6/9 transition, relog, journal, morph, cheat, and dependency test |
| P0 | Grave loc types and coordinates are reversed/global | Canonical Mausoleum graves yield no body parts; wrong woods graves do | Exact Ed and three Mausoleum coordinate tests; every other grave returns its own inscription/empty result |
| P0 | Body flags block replacement after dropped/lost items | Quest can become permanently stuck before hand-in | Drop/death/bank/ground/relogin recovery for every body part without duplicates |
| P0 | Cavern, key gate, shed, and tower routes are bypassed by teleports/generic doors | Core traversal and key gameplay is absent; wrong-plane duplicate creature | End-to-end real-map route with locked/unlocked states and no dynamic duplicate actors |
| P0 | Completion is non-atomic and lacks capacity/overlap guards | Ring/XP can be lost or duplicated; complete state can precede QP/count | Full-inventory, double-click, failure-injection, logout/reconnect, and repeated completion tests |
| P1 | Start helper turns boostable action skills into hard start requirements | Valid players cannot start directly or train/boost during quest | Start below skill levels; fail/pass only at Crafting/pickpocket actions with current boosts |
| P1 | Brain ground pickup bypasses Roavar purchase and duplicate ownership | Brain is free; canonical coin/Telegrab choice and full-inventory behavior fail | Manual pickup, buy, decline, no-coins, Telegrab, duplicate, bank, and capacity matrix |
| P1 | Experiment variants/drop handlers all grant cavern keys; gate ignores key | Wrong monsters farm keys, including post-quest | Designated level-51 combat/drop and post-quest chest/attack restrictions |
| P1 | Gardener follow/key/help graph is absent | Transcript directions and required shed key cannot occur | Ghostspeak/no-ghostspeak, ten-minute follow, region exit, grave arrival, shed/mould branches |
| P1 | Brush construction ignores canes, bronze wire, and Crafting | Required items/skill can be bypassed entirely | Bidirectional item-use at 19/20 boosted levels; one cane/wire per stage; full/lost item cases |
| P1 | Conductor is made anywhere and awards no 50 Crafting XP | Furnace gameplay/interface and action reward absent | Modern furnace menu, remount, silver/mould quantities, 50 XP, repeat/loss behavior |
| P1 | Repaired conductor has no visual state | World still shows broken conductor after lightning | Per-player state and relog/client capture before/after repair |
| P1 | Werewolf Agility Course unlock not found | Headline permanent reward is unavailable | Pre/post completion and both ring variants at course entrance/trainer |
| P1 | Ring reclaim checks only inventory/worn | Banked/death/well rings can be duplicated; later relocation may break reclaim | All storage/loss states and castle/Harmony Island actor locations |
| P2 | Interview, Doctor, Gardener, Rologarth, and post-quest dialogue compressed | Reachable narrative branches are missing or ordered incorrectly | Transcript choice/re-talk matrix at every state |
| P2 | Bookcases/fireplace are not placement-specific | Wrong scenery gives quest items | All castle bookcase/fireplace coordinates and duplicate/recovery states |
| P2 | Clock, letter, and journal lore absent | Optional canonical lore cannot be played | Wind/repeat/read/drop/reclaim tests |
| P2 | Journal/debug snapshots are inaccurate | Visible state and developer verification lie about route | Golden journal and coherent debug/`::complete` tests |

## 11. Modernization work packages

### WP1 — native state authority and migration

- Adopt `%fenk_quest` 0–6/9 as the sole primary route carrier.
- Map every read/write and native NPC/loc morph; correct side-state meanings.
- Migrate all reachable divergent packed/native save pairs without overwriting
  unrelated packed carrier bits.
- Update journal, Roavar, Garden of Tranquillity, The Great Brain Robbery, Rag
  and Bone Man II partial checks, debug helpers, and `::complete` invariants.

### WP2 — start, actor dialogue, and item ownership

- Restore sign Yes/No start prompt and direct Doctor interview.
- Enforce Priest in Peril/full and Restless Ghost/partial while keeping skills
  boostable at their actual actions.
- Implement full Doctor/Gardener/Rologarth branch and re-talk matrix.
- Attach Roavar payment/Telegrab policy to the ground brain and centralize
  unique quest-item ownership/recovery.

### WP3 — bookcases, graves, cave, and keys

- Route bookcases and fireplaces by exact native placement.
- Implement Gardener following/directions and exact named grave results.
- Restore memorial opening, real Experiment Cave traversal, designated
  level-51 fight/drop, keyed gate, chest, ladder, and Mausoleum graves.
- Define recoverable, duplicate-safe lifecycle for amulets, keys, head, brain,
  torso, arms, legs, and partial cave access.

### WP4 — sewing, shed, Crafting, and conductor world-state

- Preserve partial needle/thread hand-in and native ritual-table morph.
- Add shed key dialogue, one-time door unlock, cupboard recovery, three canes,
  three bronze wires, and boostable Crafting checks.
- Add the conductor to the modern furnace interface, including 50 Crafting XP
  and retained mould.
- Implement lightning animation/cutscene and relog-safe player-visible conductor
  state.

### WP5 — tower, atomic completion, and permanent rewards

- Use the real upper-floor door/ladder route and native placed Rologarth.
- Add final Doctor confrontation and complete transcript/post-quest routing.
- Commit ring, state, XP, QP/count, jingle, and completion scroll exactly once.
- Implement bank/death/well-aware ring recovery at all Doctor locations.
- Restore and test Werewolf Agility Course access plus downstream quest gates.

### WP6 — optional lore and Gate D evidence

- Implement clock winding, letter, journal/read/recovery behavior.
- Add state, trigger, item-lifecycle, multi-player, combat, and failure tests.
- Compile/pack osrs239 and run a real-client end-to-end smoke with captures.

## 12. Verification matrix

| Scenario | Expected result |
| --- | --- |
| Fresh sign Yes/No and direct Doctor | No leaves native 0; valid interview writes native 1; skills do not block start |
| Priest/Restless Ghost combinations | PiP must be complete; documented partial Restless Ghost threshold passes |
| Interview answer matrix | Wrong first answer ends before question two; correct two answers hire; refusal text/state match transcript |
| Brain manual pickup / purchase / Telegrab | Exactly one owned brain, correct 50-coin policy, decline/no-money/capacity behavior |
| West/east bookcases | Only intended books/items appear; piece/star loss and post-placement duplicates are coherent |
| Gardener without/with ghostspeak | Incomprehensible response or full choices/follow/key/mould guidance as appropriate |
| Every Haunted Woods grave | Only Ed Lestwit's exact grave gives head; all inscriptions/results correct |
| Memorial and cave | Star persists unlock, real dungeon route opens, relog/re-entry works |
| Experiment variants | Only designated level-51 target gives one owned key at correct state; attack restrictions and Experiment bone coexist |
| Mausoleum gate/chest/ladder | Locked before key, unlocks once, post-quest chest source works, topology is bidirectional |
| Three Mausoleum graves | West torso, middle arms, east legs; duplicates/loss/recovery correct |
| Every body-item subset | Doctor acknowledges/consumes intended parts and cannot strand or duplicate them |
| Needle plus 0–5 thread | Partial hand-in persists, correct needle lifecycle, state 3 and corpse table appear once |
| Shed key/cupboard | Locked before key, owned-ground fallback when full, permanent unlock, one brush lifecycle |
| Brush at 0–3 canes/wires and Crafting 19/20 | One cane and wire consumed per valid stage; boost policy exact; no bypass |
| Four fireplaces | Only upstairs west produces/replaces mould under allowed states |
| Furnace conductor | Correct menu/remount, one silver bar, retained mould, 50 Crafting XP, boosted level check |
| Conductor repair | Correct roof route, item consumption, lightning, native state 4, visual state, relog safety |
| Doctor/tower/Rologarth | Key and one-time door unlock, real plane-2 ladder route, no temporary duplicate NPC |
| Two simultaneous final pickpockets / full inventory | One ring, 1,000 XP, 2 QP, one count/jingle/scroll; safe retry with capacity |
| Ring loss/reclaim/storage matrix | One correct ring variant across inventory, worn, bank, ground, death, well, castle, and Harmony Island |
| Werewolf Agility Course before/after | Access denied before reward; normal/activated ring grants intended post-quest access |
| Downstream prerequisites | Garden/Brain require native 9; Rag partial unlock occurs at the documented native threshold |
| Journal at 0–6/9 and invalid | Correct requirements, side progress, recovery guidance, completion, and diagnostic fallback |
| Every debug helper and `::complete` twice | Coherent native/morph/side/unlock state; second completion is a no-op |
| Two players in castle/cave | No shared teleports, actors, drops, conductor visuals, or temporary loc leakage |

Required static/build commands after implementation:

```sh
python3 tools/questhelper_extract.py creatureoffenkenstrain --check
python3 tools/loc_var_audit.py --tree OSRS-Content/osrs239-content --var fenk_quest
python3 tools/loc_var_audit.py --tree OSRS-Content/osrs239-content --var fenk_conductor_repaired
make -C src mock230-scripts
src/build/mock230_pack --check-only
git diff --check
```

Also run trigger-uniqueness and symbolic-reference audits for every Doctor and
Rologarth form, Gardener multi, three Experiment variants, sign, bookcases,
graves, memorial, chest, all three keyed doors, cupboard, cane pile, fireplace,
furnace option, conductor, clock, and all item-use directions. Gate D additionally
requires a real-client smoke from sign/interview through the canonical cave,
Mausoleum, furnace, lightning, tower, completion scroll, ring recovery, and
Werewolf Agility Course, with packets/screenshots for every modern choice or
interface touched.

## 13. Exit criteria

Creature of Fenkenstrain may become `verified-modern` only when:

1. native `%fenk_quest` is authoritative at 0–6/9, every divergent old save has
   a tested migration, and all native morphs/journal/debug/downstream readers
   agree;
2. sign/direct interview, prerequisites, boostable action checks, every
   transcript branch, and all re-talks behave correctly;
3. brain, amulets, head, body parts, keys, tools, mould, conductor, letter, and
   ring have duplicate-safe full-inventory/loss/death/relog recovery;
4. the canonical real-map cave, designated Experiment fight, key gate/chest,
   Mausoleum graves, shed, furnace, conductor, and tower routes are playable
   without soft teleports, ungated generic doors, or temporary duplicate NPCs;
5. completion atomically awards exactly 2 quest points, 1,000 Thieving XP, one
   Ring of Charos, one completed count, jingle, and modern reward scroll;
6. the Werewolf Agility Course, Garden of Tranquillity, The Great Brain Robbery,
   Rag and Bone Man II partial access, POH crest, post-quest castle state, and
   later ring recovery all pass; and
7. static audits, Quest Helper extraction, osrs239 compile/pack, automated
   transition/lifecycle/multi-player tests, and a captured real-client smoke
   all pass with evidence recorded.

Until those conditions hold, the inventory status remains `audit-pending`.
