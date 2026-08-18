# Jungle Potion modernization audit

Status: `audit-pending` — the native quest dbrow, 0–13 cache varp, Trufitus
offer and five-herb dialogue, all five gathering locs, generic cleaning data,
Pothole Dungeon travel, dynamic journal, completion scroll, cheat arm, POH
status adapter, and downstream prerequisite checks exist. The route is
organically completable, but it is not modern or current-OSRS faithful: snake
weed always succeeds instead of using its Herblore-scaled harvest roll, the
rogue's-purse wall never depletes, post-quest herb sales always pay one coin
instead of 1–4, hand-ins and completion write state before settlement, and the
shared Trufitus owner omits the Shilo Village branches that use the same NPC.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to the native state, Trufitus dialogue ownership,
five gathering sources, cleaning and freshness, Pothole Dungeon traversal,
hazards, item loss/recovery, journal, completion settlement, post-quest herb
sales, admin handling, and every downstream consumer. It is an implementation
specification, not verification evidence.

## 1. Authoritative references

Revisions were resolved through the OSRS Wiki API on 2026-08-17. The article,
quick guide, and transcript define the route and dialogue. The scenery/item
pages define gathering, depletion, cleaning, and later uses. Trufitus's pages
define his post-quest shop-like exchange and shared Shilo Village role.

| Reference | Pinned revision | Audit use |
| --- | --- | --- |
| [Article](https://oldschool.runescape.wiki/w/Jungle_Potion?oldid=15281195) | 15281195, 2026-07-29 | Identity, requirements, route, hazards, reward, unlocks |
| [Quick guide](https://oldschool.runescape.wiki/w/Jungle_Potion/Quick_guide?oldid=15029261) | 15029261, 2025-11-15 | Exact order, source restrictions, later-use extras |
| [Quest transcript](https://oldschool.runescape.wiki/w/Transcript%3AJungle_Potion?oldid=15263262) | 15263262, 2026-07-14 | Offer, clue re-asks, dirty/not-fresh branches, completion |
| [Trufitus](https://oldschool.runescape.wiki/w/Trufitus?oldid=15276265) | 15276265, 2026-07-26 | Shared actor, 1–4 coin exchange, Shilo role |
| [Trufitus dialogue](https://oldschool.runescape.wiki/w/Transcript%3ATrufitus?oldid=15032830) | 15032830, 2025-11-17 | Post-Jungle, herb-sale, and post-Shilo dialogue |
| [Druidic Ritual](https://oldschool.runescape.wiki/w/Druidic_Ritual?oldid=15240944) | 15240944, 2026-06-27 | Required quest and level-3 Herblore provenance |
| [Marshy jungle vine](https://oldschool.runescape.wiki/w/Marshy_jungle_vine?oldid=15254669) | 15254669, 2026-07-05 | Snake-weed roll and 60-second depletion |
| [Palm tree](https://oldschool.runescape.wiki/w/Palm_tree_%28Ardrigal%29?oldid=15202570) | 15202570, 2026-04-29 | Ardrigal source and depletion |
| [Scorched earth](https://oldschool.runescape.wiki/w/Scorched_earth?oldid=15202572) | 15202572, 2026-04-29 | Sito-foil source and depletion |
| [Volencia-moss rock](https://oldschool.runescape.wiki/w/Rock_%28volencia_moss%29?oldid=15202573) | 15202573, 2026-04-29 | Moss source and depletion |
| [Fungus-covered wall](https://oldschool.runescape.wiki/w/Fungus_covered_Cavern_wall?oldid=15200880) | 15200880, 2026-04-28 | Quest-only freshness and 100-tick depletion |
| [Pothole Dungeon](https://oldschool.runescape.wiki/w/Pothole_Dungeon?oldid=15018098) | 15018098, 2025-11-06 | Entrance, Jogres, levels, and traversal |
| [Grimy snake weed](https://oldschool.runescape.wiki/w/Grimy_snake_weed?oldid=15186652) | 15186652, 2026-04-22 | Cleaning level/XP, sources, later potion uses |
| [Grimy ardrigal](https://oldschool.runescape.wiki/w/Grimy_ardrigal?oldid=15186651) | 15186651, 2026-04-22 | Cleaning/source and 1–4 coin exchange |
| [Grimy sito foil](https://oldschool.runescape.wiki/w/Grimy_sito_foil?oldid=15186653) | 15186653, 2026-04-22 | Cleaning/source and post-quest duplicates |
| [Grimy volencia moss](https://oldschool.runescape.wiki/w/Grimy_volencia_moss?oldid=15186654) | 15186654, 2026-04-22 | Cleaning/source and Fairytale use |
| [Grimy rogue's purse](https://oldschool.runescape.wiki/w/Grimy_rogue%27s_purse?oldid=15186655) | 15186655, 2026-04-22 | Alternate sources and quest freshness restriction |
| [Rogue's purse](https://oldschool.runescape.wiki/w/Rogue%27s_purse?oldid=15183476) | 15183476, 2026-04-22 | Cleaning, Zogre use, drop/destroy behavior |
| [Shilo Village](https://oldschool.runescape.wiki/w/Shilo_Village?oldid=15292268) | 15292268, 2026-08-10 | Direct prerequisite and shared Trufitus handoff |
| [Shilo transcript](https://oldschool.runescape.wiki/w/Transcript%3AShilo_Village?oldid=15297971) | 15297971, 2026-08-13 | Wampum belt and all mid-quest Trufitus branches |
| [Tai Bwo Wannai Trio](https://oldschool.runescape.wiki/w/Tai_Bwo_Wannai_Trio?oldid=15265886) | 15265886, 2026-07-17 | Direct prerequisite and Timfraku continuation |
| [Zogre Flesh Eaters](https://oldschool.runescape.wiki/w/Zogre_Flesh_Eaters?oldid=15297479) | 15297479, 2026-08-13 | Direct prerequisite and snake/purse consumer |
| [My Arm's Big Adventure](https://oldschool.runescape.wiki/w/My_Arm%27s_Big_Adventure?oldid=15289163) | 15289163, 2026-08-06 | Cleanup-access prerequisite consumer |
| [Legends' Quest](https://oldschool.runescape.wiki/w/Legends%27_Quest?oldid=15293032) | 15293032, 2026-08-11 | Snake-weed/ardrigal bravery-potion consumer |
| [Fairytale I](https://oldschool.runescape.wiki/w/Fairytale_I_-_Growing_Pains?oldid=15292317) | 15292317, 2026-08-10 | Possible volencia-moss consumer |

Transition aid only: local Quest Helper commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/junglepotion)
maps states 0–11, all five actors/locs and item pairs, the cave zone, hazards,
requirements, and rewards. The quest file's last change is `e6ccb7c` from
2026-02-23. `python3 tools/questhelper_extract.py junglepotion --check` exits
0 and resolves every referenced gameval. Quest Helper is a transition aid, not
evidence that server rolls, loc transforms, transactions, shared dialogue, or
completion are correct.

## 2. Canonical contract

Jungle Potion is a members, novice, short quest. It starts by talking to
Trufitus Shakaya in Tai Bwo Wannai. Druidic Ritual must be complete. Level 3
Herblore is needed to clean the herbs and may use a visible boost, although
Druidic Ritual normally supplies that level. Combat is not required, but the
route passes level-46 Harpie Bug Swarms, poisonous tribesmen, and aggressive
level-53 Jogres; food, antipoison, and combat level 20 are recommendations.

The canonical route is strictly ordered:

1. accept the quest after the explicit quest-start confirmation;
2. search a ripe marshy jungle vine for grimy snake weed, clean it, and give
   the fresh herb to Trufitus;
3. search one of the special north-eastern palms for grimy ardrigal, clean it,
   and hand it in;
4. search scorched earth south of the village for grimy sito foil, clean it,
   and hand it in;
5. search the south-eastern mining rocks for grimy volencia moss, clean it,
   and hand it in;
6. search the rocks at the north-eastern coast, enter Pothole Dungeon, search
   a fungus-covered wall for grimy rogue's purse, clean it, climb out, and hand
   it in; and
7. receive one quest point and exactly 775 Herblore XP.

Each clue must be received before that herb's source works; all five cannot be
collected in advance. Extra copies can be harvested after a source is unlocked
and are intentionally useful. Monster-dropped snake weed or rogue's purse does
not satisfy the quest's “freshly picked” requirement. Dirty herbs are retained
and rejected with a clean-it-first branch.

Snake weed differs from the other ripe sources: every search performs a
Herblore-scaled roll (4% at level 1 and 86% at level 99, capped at 99), so a
successful harvest can take repeated searches. A harvested source becomes its
depleted counterpart and returns after roughly 100 ticks/60 seconds. This also
applies to the fungus-covered cavern wall, whose cache-authored depleted form
is `rogues_purse_cave_empty` (“Fungus pattern”).

All five grimy herbs require current Herblore 3 and each cleaning grants 2.5
Herblore XP. After completion, using any of the five clean herbs on Trufitus
consumes one and pays a random 1–4 coins. Grimy herbs are not consumed.

Completion directly gates Shilo Village, Tai Bwo Wannai Trio, Zogre Flesh
Eaters, and part of My Arm's Big Adventure, plus Karamja Diary tasks. It also
unlocks the herb sources needed by Legends' Quest, Zogre Flesh Eaters, and a
possible Fairytale I item request.

## 3. Native identity and state

| Field | Native value |
| --- | --- |
| Cache quest / dbrow | 40 / `quest_junglepotion` |
| Quest root | `quest_junglepotion`, 3 files, 367 lines |
| Shared dialogue owner | `areas/area_karamja/scripts/trufitus.rs2`, 270 lines |
| Start actor | native `trufitus`, world spawn `(2809,3086,0)` |
| Main carrier | cache varp 175 / `%junglepotion` |
| Start / end / post-talk | 0 / 12 / 13 |
| Quest points / XP | 1 / 775 Herblore |
| Metadata | members; novice; short; Karamja; combat 20 recommended |

The cache dbrow names Druidic Ritual as its requirement, end state 12, one
quest point, and 7,750 internal Herblore XP units. Preserve the dbrow and varp
as the identity and persistence sources; no authored parallel state is needed.

| State | Native name | Meaning and current owner |
| ---: | --- | --- |
| 0 | `not_started` | Trufitus's offer tree |
| 1 / 2 | `get` / `found_snake_weed` | Source not yet proven / fresh source searched |
| 3 / 4 | `get` / `found_ardrigal` | Second clue / fresh source searched |
| 5 / 6 | `get` / `found_sito_foil` | Third clue / fresh source searched |
| 7 / 8 | `get` / `found_volencia_moss` | Fourth clue / fresh source searched |
| 9 / 10 | `get` / `found_rogues_purse` | Cave clue / fresh wall searched |
| 11 | `found_all_herbs` | Final herb dialogue complete; reward queue pending/recoverable |
| 12 | `complete` | Cache end state; reward expected settled |
| 13 | `complete_after_spoken` | Complete plus post-quest Trufitus news delivered |

Quest Helper agrees with states 0–11 and treats state 11 as the final talk.
The native dbrow correctly ends at 12. State 13 is a valid post-completion
extension and every completion consumer must therefore test `>= 12`, never
equality.

## 4. Implementation and ownership surface

| Surface | Audit result |
| --- | --- |
| `quest_junglepotion_locs.rs2` | Five sources, cave travel, generic pickup, unguarded reward queue |
| `junglepotion_journal.rs2` | Dynamic dbrow journal for all native states; inventory-only recovery text |
| `quest_junglepotion.constant` | Correct 0–13 native state names and one-QP constant |
| `area_karamja/scripts/trufitus.rs2` | Start, hand-ins, clues, completion, post-Jungle talk/sales; explicitly defers shared quest arms |
| generic `identify.rs2` + dbrows | Correct shared cleaning trigger; level 3, 2.5 XP for every jungle herb |
| cache locs | All full/empty forms exist, including omitted `rogues_purse_cave_empty` |
| world spawns | Trufitus, Harpie Bug Swarms, tribesmen, and many Pothole Jogres exist |
| Jogre/tribesman drops | Alternate grimy snake-weed and rogue's-purse sources exist |
| quest list / POH adapters | Journal and 0/started/complete status are registered with `>= 12` |
| `quest_cheat.rs2` | Correct idempotent state/point shortcut to 12; intentionally grants no XP/items |
| downstream state reads | TBWT, My Arm, Shilo, and Zogre compare against `>= 12` or `< 12` correctly |

The implementation already uses symbolic gamevals, native state, modern chat
helpers, the dynamic journal, table-derived quest points, and the shared modern
completion scroll. Those are good foundations. Its old machinery is the
single monolithic shared-NPC owner, state-first item transactions, a bare
player queue with no receipt, incomplete cache-loc use, and an old queue entry
whose “no gaps found” conclusion was based on route shape rather than behavior.

The latest native implementation-path commit is `769a5d3` from 2026-08-12.
`docs/QUESTHELPER_CONTENT_PORT_QUEUE.md` still labels this exact tree
“audited-ok” and says all 12 journal states and rewards match. That conclusion
is contradicted by the current sources and pinned contract: at minimum it
misses the harvest roll, cave-wall depletion, random herb-sale price, explicit
start confirmation, transaction ordering, and shared Trufitus omissions.

## 5. Start, prerequisites, and dialogue

The long pre-quest conversation and its multiple refusal/farewell routes match
the transcript closely. The Druidic Ritual check is performed only after the
player accepts, and failed requirements leave state 0 unchanged. This is good.
The `stat(herblore)` check in generic cleaning uses the current level, so a
visible boost can satisfy level 3 as required.

The current start owner does not display the transcript's explicit “Start the
Jungle Potion quest?” Yes/No confirmation. It presents the narrative
“sounds difficult” / “challenge for me” choice and commits state 1 immediately.
Quest Helper likewise expects a final `Yes.` dialogue step after the narrative
choice. Modernize the flow through the repository's standard quest-start
confirmation, retaining every decline branch and committing state only after
requirements and final confirmation succeed.

Every active stage correctly offers “Of course!” and “Not yet, sorry, what's
the clue again?”, with stage-specific re-tell dialogue. Preserve those lines,
the no-item accusation, dirty-herb rejection, wrong-herb rejection, and
post-completion direction to Timfraku. Add fixtures for declining at every
nested offer point and re-talking without an item at every state.

## 6. Gathering, cleaning, and freshness

| Stage | Source / output | Current behavior | Required modernization |
| --- | --- | --- | --- |
| Snake weed | `snake_vine_full` → grimy snake weed | Every eligible search succeeds, then changes to empty for 100 ticks | Implement the capped current-Herblore success roll and failed-search loop; transform only on success |
| Ardrigal | `ardrigal_palm_full` → grimy ardrigal | Immediate success; empty for 100 ticks | Retain always-on-ripe success, add arrival/harvest sequencing, settle item before state |
| Sito foil | `sito_soil_full` → grimy sito foil | Immediate success; empty for 100 ticks | Same transaction/animation modernization |
| Volencia moss | `volencia_moss_rock_full` → grimy moss | Immediate success; empty for 100 ticks | Same transaction/animation modernization |
| Rogue's purse | `rogues_purse_cave_full` → grimy purse | Immediate success and passes `null`, so the wall never depletes | Change to `rogues_purse_cave_empty` for 100 ticks and verify all placed walls |

Each loc correctly rejects access before its clue and remains usable from its
unlock state onward. This prevents collecting future stages early while
allowing extra herbs for later quests. Keep that policy. A full inventory
currently grants nothing and leaves state/source unchanged, also correct.

The generic pickup proc writes `%junglepotion = found_*` before `inv_add`.
Reverse that order and check the add result: a failed delivery must not produce
freshness state. Apply the same rule to loc depletion. Repeated clicks during
the interaction must yield at most one item and one transform.

Freshness is represented only by the even-numbered “found” state, not by item
metadata. That correctly rejects a monster/drop-derived clean herb while the
state is still odd, and permits a replacement harvest after loss because the
source stays unlocked. It also means that once any valid source was searched,
any same-type clean object in inventory becomes indistinguishable. Preserve
the native state schema, but regression-test the live substitution behavior
before deciding whether this is canonical or requires a short-lived provenance
receipt. Do not invent permanent per-item freshness metadata without evidence.

Cleaning is already correctly centralized. All five grimy objects route to
`~attempt_clean_herb`; their dbrows require current level 3, output the correct
clean object, and award 25 internal units (2.5 XP). Do not duplicate cleaning
inside the quest. Test boosts below/at 3, members gating, the exact inventory
slot, rapid repeat, full/invalid slot, and all five output/message rows.

## 7. World route and hazards

The cache and Quest Helper agree on the five symbolic locs and route. The cave
entrance asks Yes/No, telejumps to `(2830,9520,0)`, and the handholds return to
`(2823,3119,0)`. Dialogue text matches the transcript. The implementation does
not run an arrival delay, movement/turning, or climb animation; verify the
current OSRS packet/capture before adding choreography, then use shared travel
helpers where the cache supplies them. Test collision, busy state, logout at
both ends, repeated input, and item drops on the cave stair tiles.

Hazards are world systems rather than quest-script damage:

- Harpie Bug Swarms level 46 are spawned along the ardrigal/cave route;
- poisonous tribesmen are spawned south and south-east of the village; and
- level-53 Jogres are densely spawned inside Pothole Dungeon and have live
  death/drop owners.

Verify aggression, poison, combat stats, bug-lantern rules, pathing, safe
tiles, death, gravestone placement, and return access in those shared owners.
The quest does not require killing a Jogre; the easy Karamja Diary kill is an
optional side effect and must not be turned into quest progress.

Loc depletion is shared world state in the current engine. Confirm this matches
the reference behavior with two players: one successful harvest should expose
the correct empty form to the intended audience, respawn at 100 ticks, and
never advance another player's quest state. Test every placed instance of each
full loc, not only Quest Helper's nearest coordinate.

## 8. Hand-ins, loss, journal, and recovery

Trufitus accepts only the clean expected herb. The dialogue path currently
writes the next state and then calls `inv_del`. Make each hand-in one checked,
atomic transaction:

1. require the exact expected state and clean object in the selected/verified
   inventory domain;
2. retain dirty, wrong, alternate-source, and out-of-order objects;
3. delete exactly one expected object successfully;
4. only then advance to the next odd state (or state 11); and
5. make duplicate packets/re-entry a no-op with stage-appropriate dialogue.

Loss is recoverable because every previously unlocked source remains active.
Test dropping, banking, death, gravestone expiry, logout, and relog with both
grimy and clean forms at all five even states. A banked or grounded herb must
not be silently consumed by Trufitus; the journal should tell the player to
retrieve/clean/replace it rather than claiming an impossible hand-in.

The dynamic journal covers 0–13 and correctly treats 12/13 as complete. Its
item-aware branches inspect inventory only. The state-2 snake branch has a
specific blank-objective bug: when the player holds grimy snake weed, neither
the clean branch nor `else if (grimy == 0)` runs, so it appends no active task.
Other herb states use an `else` and do show a recovery objective. Replace the
five copied blocks with one stage-aware helper that distinguishes held grimy,
held clean, stored/ground ownership when detectable, and lost/re-pick states.

Fix visible text inconsistencies while preserving intentional transcript
historicism: “Snakeweed” versus “Snake Weed”, “Rogues Purse”/“Rogue Purse”
versus cache “Rogue's purse”, and `Sito Foil,Volencia moss`. Journal prose is
not a reason to rename native symbolic objects.

## 9. Completion and post-quest services

After the final hand-in, state becomes 11, dialogue runs, and
`@trufitus_finish_quest` queues `junglepotion_quest_complete`. That queue
unconditionally writes 12, awards 775 XP, increments quest points/completed
count through `~quest_complete_rewards`, and paints the modern scroll. Talking
at state 11 queues it again. There is no guard or durable receipt in the queue.

State 11 is a useful recovery boundary: interruption after deleting the final
herb must resume the final dialogue/reward without another herb. Keep it, but
make settlement idempotent. At minimum the queued owner must require state 11
and refuse states 12+, and it must not expose state 12 before both XP and the
completion receipt are committed. Because `~quest_complete_rewards` itself has
no receipt, use the shared modernization solution rather than a quest-specific
point counter workaround. Test duplicate queues, dialogue interruption,
logout/reconnect before the queue fires, death, busy completion UI, and reward
scroll close/remount.

The cache row and current code agree on exactly one quest point and 775
Herblore XP. There is no item reward; `coins` is only the completion-scroll
model icon and must not be added as a quest reward.

Post-quest Talk-to at state 12 moves to state 13 after Trufitus's news and
points the player to Timfraku. At state 13 it repeats the dialogue without
rewriting state. This native extension is legitimate and downstream checks
already use `>= 12`.

Post-quest item-on-Trufitus correctly accepts the five clean herbs and rejects
the five grimy forms, but it always adds one coin. Replace the fixed quantity
with `1 + random(4)`, producing 1–4 inclusive, and interpolate the singular or
plural transcript message. Delete one herb only as part of the same checked
transaction. Repeated use, the last inventory slot, full inventory, invalid
items, logout, and all five herbs need tests.

## 10. Shared Trufitus and downstream consumers

`trufitus.rs2` owns the only Talk-to and item-on-Trufitus triggers. Its header
says “Zombie Queen / TBWT Trio arms deferred.” Current Wiki identity and
dialogue show no separate Tai Bwo Wannai Trio Trufitus branch beyond the
post-Jungle direction to Timfraku, which is present. The Shilo Village omission
is real and critical:

- Mosol Rei's current thin owner does not provide the full Wampum-belt start;
- using `mosol_wampum_belt` on Trufitus falls through Jungle Potion's generic
  post-quest item rejection;
- Trufitus cannot start Shilo Village or identify its plaque, scrolls, corpse,
  bone shard, beads, key clues, and post-quest states; and
- the Shilo journal itself says those Trufitus state writes should exist.

Modernize Trufitus as a composed shared actor dispatcher. Jungle Potion state
must select its own offer/active/post-Jungle dialogue without swallowing Shilo
quest items or later Shilo Talk-to states. Implement Shilo behavior in its own
quest owner/procs and delegate explicitly from the shared Karamja trigger. Add
cross-quest tests beginning at Jungle states 0, 1–11, 12, and 13 with Shilo
states before start, active, complete, and post-completion.

Downstream consumers found in the tree:

| Consumer | Current dependency | Audit requirement |
| --- | --- | --- |
| Tai Bwo Wannai Trio / Timfraku | `>= 12` exposes “Trufitus sent me” and advances TBWT | Preserve at both 12 and 13; verify refusal before 12 |
| Shilo Village / Mosol Rei | `< 12` blocks start | Gate is correct, but shared Trufitus continuation is absent |
| Zogre Flesh Eaters | `< 12` blocks requirements; later consumes snake/purse | Preserve access to extra sources and generic cleaning after completion |
| My Arm's Big Adventure | `< 12` fails requirement proc | Preserve both complete states and Cleanup-related access |
| Legends' Quest | journal/dialogue consumes clean snake weed and ardrigal | Preserve repeat harvesting and prevent Jungle freshness logic leaking into Legends |
| Fairytale I | may request clean volencia moss | Preserve repeat source and cleaning after completion |
| Shades of Mort'ton / Jogre / tribesman | alternate rogue/snake sources | Preserve drops, but do not let them satisfy an unproven Jungle stage |

The `%junglepotion` carrier is oddly declared under
`quest_zogreflesheaters/configs/zogreflesheaters.varp` even though it is native
cache varp 175 and owned by Jungle Potion. Move or document its declaration in
a neutral/native quest-state location without changing its packed identity,
and ensure pack order has a single declaration.

## 11. Modernization work packages

### JP-1 — state, start, and shared ownership

- keep dbrow `quest_junglepotion`, varp 175, states 0–13, and end state 12;
- add the explicit modern quest-start confirmation and preserve refusals;
- split Jungle-specific Trufitus procs from the shared actor dispatcher;
- compose the complete Shilo Village item/Talk-to arms without duplicate
  triggers; and
- relocate/document the varp declaration under its true owner.

### JP-2 — harvesting and travel

- implement the exact current-level, capped snake-weed success roll;
- make all five acquisitions checked item-first transactions;
- use every cache-authored depleted loc, including
  `rogues_purse_cave_empty`, for 100 ticks;
- add verified arrival/animation/busy semantics and retain repeated extra
  harvesting after unlock; and
- verify cave entry/exit, all source placements, multiplayer depletion, and
  hazards against a real client.

### JP-3 — cleaning, freshness, and hand-ins

- retain the generic dbtable-driven cleaning owner and test all five rows;
- define/test freshness provenance and alternate-source rejection;
- make each hand-in delete-before-state and replay-safe;
- cover full inventory, loss, bank/ground ownership, death, and relog; and
- consolidate journal generation and correct its blank/object-text defects.

### JP-4 — settlement and post-quest economy

- make state-11 completion resumable and exactly once;
- award 775 XP, one QP, completion count, scroll, and jingle through the shared
  receipt-aware lifecycle;
- retain state 13 as post-completion dialogue state;
- pay random 1–4 coins for each clean herb with an atomic exchange; and
- verify every downstream `>= 12` gate and later herb consumer.

### JP-5 — tests and evidence

- add a deterministic RuneScript transition harness for 0→13;
- add source-roll/depletion, cleaning, freshness, transaction, journal,
  completion replay, shared-Trufitus, and downstream-gate fixtures;
- run script pack/static checks and Quest Helper resolution;
- run two-player depletion and real-client start-to-scroll smoke captures; and
- replace the stale queue's “audited-ok” claim with the final evidence status.

## 12. Gate D verification matrix

| Scenario | Required proof |
| --- | --- |
| Start/refusal | Every offer choice, explicit confirmation, Druid gate, no write on decline/failure |
| State machine | Exact transitions 0→1→2→3→4→5→6→7→8→9→10→11→12→13 |
| Source gates | Future herbs fail; unlocked past herbs remain harvestable |
| Snake roll | Deterministic boundary rolls at boosted levels 1/3/98/99/100; cap at 99 |
| Source settlement | Full inventory, repeated packet, all five full→empty→full loc cycles |
| Multiplayer | Correct shared visibility/depletion; no other-player item/state mutation |
| Cleaning | Every grimy/clean pair, 3 requirement, 2.5 XP, boosts and rapid repeat |
| Freshness | Valid source accepted; Jogre/tribesman/Mort'ton variants rejected before proof |
| Loss/recovery | Dirty/clean drop, bank, death, logout/relog at every even state |
| Hand-ins | Wrong/dirty/order mismatch retained; one clean item deleted atomically |
| Cave/hazards | Entry refusal/accept, climb out, collision, Jogre/Harpie/poison behavior |
| Journal | Every state with grimy, clean, stored, and missing expected herb; 12/13 complete |
| Completion | interruption and resume at 11; duplicate queue/action grants XP/QP/count once |
| Post-quest sale | Five clean and five grimy objects; deterministic payouts 1,2,3,4; replay-safe |
| Shared Trufitus | Jungle × Shilo state/item cross-product; no swallowed Wampum/artifact arms |
| Consumers | TBWT, Shilo, Zogre, My Arm at 11/12/13; Legends/Fairytale herb access |
| Admin | `::complete quest_junglepotion` twice; first sets state/points, second is no-op |

Required commands after implementation:

```sh
python3 tools/questhelper_extract.py junglepotion --check
make -C src mock230-scripts
OSRS_CONTENT_ROOT="$PWD/OSRS-Content/osrs239-content" \
  ./src/mock230_pack --check-only
git diff --check
```

Static compilation is necessary but cannot prove harvest probability,
multiplayer loc ownership, transaction replay, downstream dialogue composition,
or one-time reward delivery. Record deterministic harness output plus live
client packets/screenshots for the real start, each source, cave travel, final
hand-in, completion scroll, state-13 talk, 1–4 coin exchange, and Shilo belt
handoff.

## 13. Exit criteria

Jungle Potion may move to `verified-modern` only when:

- the real Trufitus route completes organically from state 0 to state 12 and
  the follow-up reaches 13;
- all five sources, exact clue gates, snake roll, depleted locs, cleaning,
  freshness, loss/recovery, and hazards match the pinned contract;
- hand-ins, state-11 recovery, completion, and herb sales are atomic and
  replay-safe;
- one QP, 775 Herblore XP, completed count, jingle, journal, and completion
  scroll are correct exactly once;
- the shared Trufitus owner exposes every required Jungle and Shilo branch;
- TBWT, Shilo, Zogre, My Arm, Legends, Fairytale, drops, and post-quest herb
  economy remain functional at states 12 and 13;
- static, deterministic, multiplayer, reconnect/death, admin, and real-client
  verification evidence is recorded; and
- this record and the generated manifest contain no undisclosed critical
  simplification.

This audit intentionally makes no gameplay changes.
