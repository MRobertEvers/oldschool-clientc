# Getting Ahead modernization audit

Status: `audit-pending` — the native quest row, primary state, support bits,
actors, scenery, items, animations, journal dispatch, completion adapter, and
admin adapter are present. The live route is not completable: the flour-on-gate
script is bound to a multiloc shell while interaction dispatch uses its resolved
child, and the quest's exact staircase handler blocks access to the upstairs
supplies at state 4. Later checkpoints contain a fake boss kill, unreachable
wall Build handler, and a state-30 Gordon bypass. Several loose state guards
would also let retained quest items rewind a completed save and award repeat XP
and quest points after the first completion.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to Gordon, Mary, the Sergeant, both gate
leaves, the flour trail and overnight scene, the Headless Beast's private lair,
combat and drops, every local supply source, the three fake-head forms, the
mounted-head hotspot, completion and coin recovery, Mary's tannery, Nightmare
Zone eligibility, journal/admin adapters, migration, and recovery. It is an
implementation specification, not completion evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable route, dialogue, combat, item, reward, recovery, and unlock contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Getting Ahead](https://oldschool.runescape.wiki/w/Getting_Ahead?oldid=15241058) | 15241058, 2026-06-27 | Identity, non-boostable start requirements, supplies, route, combat, rewards, tannery, and Nightmare Zone unlock |
| [Getting Ahead/Quick guide](https://oldschool.runescape.wiki/w/Getting_Ahead/Quick_guide?oldid=14512617) | 14512617, 2023-12-01 | Exact ordered state transitions and confirmation prompts |
| [Transcript:Getting Ahead](https://oldschool.runescape.wiki/w/Transcript%3AGetting_Ahead?oldid=14516193) | 14516193, 2023-12-14 | Accept/refuse, re-talks, optional item uses, fake-head hand-ins, final scene, and completion dialogue |
| [Gordon](https://oldschool.runescape.wiki/w/Gordon?oldid=14769400) | 14769400, 2024-10-13 | First/repeat greeting and post-quest 3,000-coin recovery |
| [Mary](https://oldschool.runescape.wiki/w/Mary_%28Getting_Ahead%29?oldid=15196663) | 15196663, 2026-04-25 | Pre/post NPC forms, one-time post-quest dialogue, Trade unlock, products, and prices |
| [Headless Beast](https://oldschool.runescape.wiki/w/Headless_Beast?oldid=15200212) | 15200212, 2026-04-28 | Stats, immunity, melee and falling-rock attacks, safespot, drops, and Nightmare Zone availability |
| [Headless Beast's Lair](https://oldschool.runescape.wiki/w/Headless_Beast%27s_Lair?oldid=14898117) | 14898117, 2025-05-10 | Private area, entrance/exit, skeleton, bones, cowhides, and post-kill world change |
| [Gordon and Mary's farm](https://oldschool.runescape.wiki/w/Gordon_and_Mary%27s_farm?oldid=15229525) | 15229525, 2026-06-08 | Farm layout and upstairs flour source |
| [Flour trail](https://oldschool.runescape.wiki/w/Flour_%28Getting_Ahead%29?oldid=15201849) | 15201849, 2026-04-29 | Eight per-player trail positions and visibility contract |
| [Shelves](https://oldschool.runescape.wiki/w/Shelves_%28Getting_Ahead%29?oldid=15202355) | 15202355, 2026-04-29 | Search menu and red/yellow dye choice |
| [Rocks](https://oldschool.runescape.wiki/w/Rocks_%28Getting_Ahead%29?oldid=15139918) | 15139918, 2026-03-01 | One bronze-pickaxe source and depleted transform |
| [Skeleton](https://oldschool.runescape.wiki/w/Skeleton_%28Getting_Ahead%29?oldid=14898121) | 14898121, 2025-05-10 | Search source for Neilan's journal |
| [Neilan's journal](https://oldschool.runescape.wiki/w/Neilan%27s_journal?oldid=15282396) | 15282396, 2026-07-30 | Read text, Drop behavior, skeleton source, and POH bookcase recovery |
| [Clay head](https://oldschool.runescape.wiki/w/Clay_head?oldid=15189933) | 15189933, 2026-04-22 | Knife/soft-clay recipe, singleton/bank cleanup, death, loss, and mounted cleanup |
| [Fur head](https://oldschool.runescape.wiki/w/Fur_head?oldid=15189934) | 15189934, 2026-04-22 | Three valid furs, needle/thread gate, special invalid uses, singleton and loss behavior |
| [Bloody head](https://oldschool.runescape.wiki/w/Bloody_head?oldid=15189935) | 15189935, 2026-04-22 | Red-dye recipe, singleton/bank cleanup, death, loss, and final use |
| [Mounted Head Space](https://oldschool.runescape.wiki/w/Mounted_Head_Space_%28Getting_Ahead%29?oldid=13991396) | 13991396, 2021-01-08 | State-30 Build hotspot, level/tools/materials, and zero-XP build |
| [Sergeant](https://oldschool.runescape.wiki/w/Sergeant_%28Kebos_Lowlands%29?oldid=14770144) | 14770144, 2024-10-13 | Optional quest dialogue and shared A Kingdom Divided ownership |
| [Tanner](https://oldschool.runescape.wiki/w/Tanner?oldid=15291340) | 15291340, 2026-08-09 | Current standard tanning matrix and Mary's completion gate |

The sources define a members, intermediate, short quest released 25 November
2020. Starting requires unboosted level 30 Crafting and level 26 Construction;
the cache and Wiki recommend combat level 45. The required items are all
obtainable locally: one of bear fur, grey wolf fur, or the item named `Fur`;
soft clay or a usable pickaxe; hammer; saw; two ordinary planks; at least six
nails of any grade; knife; red dye; pot of flour; and needle plus thread, with a
costume needle replacing both needle and thread under the current policy.

The mandatory kill is the level-82 Headless Beast. Rewards are one quest point,
3,000 coins, 4,000 Crafting XP, 3,200 Construction XP, Mary's standard-price
tannery, and Headless Beast eligibility in Nightmare Zone. If a full inventory
prevents the coin grant, Gordon holds exactly one 3,000-coin reward for later.

Transition aid only: Quest Helper's
[`GettingAhead.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/gettingahead/GettingAhead.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` (the file itself last
changed in `63eaf383c95d940a7865c2b84cf85ec14941c955` on 2025-04-22)
confirms every even primary state from 0 through 32, the private-lair bounds,
the canonical coordinates, supplies, recipes, alternates, 3,000 coins, XP, QP,
and tannery reward. `python3 tools/questhelper_extract.py gettingahead --check`
resolves its dbrow, 18 item symbols, three NPCs, 11 locs, 26 coordinates, and
all 17 state entries. It cannot prove RuneScript trigger reachability,
instancing, combat, transactions, recovery, or downstream unlocks.

## 2. Native quest identity and contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_gettingahead`; dbrow pack index 61, quest metadata ID 161 |
| Type / difficulty / length | Members quest / intermediate / short |
| Release / location | 25 November 2020 / Kebos Lowlands |
| Start | `ga_gordon`; cache marker 1247,3685,0; world spawn and helper target 1248,3686,0 |
| Primary state | `%ga`, bits 0–5 of permanent/transmitted native varp `ga_main` (varp 2851) |
| Canonical values | 0 not started; 2 Mary; 4 gate; 6/8/10/12 lure/lair phases; 14 Gordon after kill; 16 Mary; 18 make clay; 20 show clay; 22 add fur; 24 show fur; 26 add dye; 28 show bloody; 30 build; 32 final Gordon; 34 complete |
| Native support bits | `%ga_reward` bit 6; `%ga_journal` bit 7; `%ga_mary_dialogue` bit 8; `%ga_gordon_met` bit 9 |
| End / quest points | State 34 / 1 QP |
| Requirements | Crafting 30 and Construction 26; both non-boostable and required before acceptance |
| Recommended combat | 45 |
| XP | 40,000 raw Crafting units = 4,000 XP; 32,000 raw Construction units = 3,200 XP |
| Item reward | 3,000 coins, with a post-completion claim if inventory capacity prevented the first grant |
| Unlocks | Mary's standard-price tannery and Headless Beast in Nightmare Zone |

The authored aliases agree with the native primary and Quest Helper and must
not be renumbered or replaced. States 8, 10, and 12 are currently unreachable,
but native gate/trail multilocs visibly use that band; modernization must
restore their intended lure/cutscene sequence rather than collapsing them.

All four native support bits are currently ignored. Their cache relationships
and canonical behavior provide the appropriate owners for:

- Gordon's first-versus-repeat standard greeting (`ga_gordon_met`);
- Neilan journal acquisition/presentation state (`ga_journal`), subject to
  verification of exact polarity and bookcase recovery interaction;
- Gordon's outstanding-versus-paid 3,000-coin reward (`ga_reward`), again with
  polarity confirmed from a live capture or deob before migration; and
- Mary's Talk-only versus Talk/Trade multinpc (`ga_mary_dialogue`). The Wiki
  says the service form appears after the first post-quest conversation, not
  merely when state 34 is written.

Do not add parallel variables until these bits are decoded and tested. Existing
saves need a one-time reconciliation of completion, coin ownership, head items,
mounted loc state, Mary form, journal state, and any invalid odd primary value.

## 3. Implementation surface

The direct root has 321 lines across one constants file, one primary-varp
declaration, and one script. The route depends on cache transforms, two world
spawn files, shared combat/instance/item systems, tannery and Nightmare Zone.

| Path / subsystem | Present responsibility | Audit result |
| --- | --- | --- |
| `quests/quest_gettingahead/configs/gettingahead.constant` | Primary aliases, coordinates, XP constants | State aliases and XP scale are correct; cave coordinate is the beast's centre, not an entry tile |
| `quests/quest_gettingahead/configs/gettingahead.varp` | Native `ga_main` declaration | Correct permanent/transmitted owner |
| `quests/quest_gettingahead/scripts/gettingahead.rs2` | Entire dialogue, route, fake combat, fake-head crafting, wall, stairs, journal, completion, debug | Route-shaped but fundamentally incomplete, unsafe, and partially unreachable |
| `configs/all.dbrow` | Native metadata | Correct identity, start, state 34, QP, skill requirements, combat recommendation, and XP |
| `configs/all.varbit` | Primary plus four native support bits | Primary used; every support bit orphaned |
| `configs/all.npc` and generated NPC animations | Gordon, Mary shell/two forms, Sergeant, Beast and NMZ variants | Assets and Beast stats/attack/defend/death animations exist; Mary Trade form is never selected |
| `configs/all.loc` | Gate wrappers/children, trail, cave exits, skeleton, wall wrapper/children, shelves, workbench, pickaxe rocks, flour barrel, stairs | Nearly the full route exists in cache; shell-bound triggers and missing handlers leave it inert |
| `configs/all.obj` | Three head forms/detail models and journal | Inventory items exist; recipes, singleton policy, Destroy text, reading, death, and cleanup are incomplete |
| `areas/world/configs/m19_57.spawn` | Gordon, Mary, Sergeant, local tools/dyes and upstairs needle/thread | Core actors and several supplies exist; canonical interactive sources remain unwired |
| `areas/world/configs/m18_57.spawn` | Two ordinary planks west of the cave | Both canonical planks are present |
| quest journal dispatcher | Calls `~gettingahead_journal` | Correct row registration; content is too coarse |
| `quests/scripts/quest_cheat.rs2` | State-only admin completion | Correct end state and repeat no-op; intentionally does not grant rewards or reconcile support bits |
| `quests/scripts/questpoints.rs2` | QP/count/scroll/jingle | Modern API is used, but it is not intrinsically idempotent and callers must guard it |
| `areas/alkharid/scripts/tanner.rs2` | Shared tanning transactions | Reusable core exists; Mary has no delegates and her swamp snakeskin price differs from the generic standard branch |
| `minigames/minigame_nightmarezone` | Dream selection, bosses, points | Current stub ignores quest unlock selection and hardcodes only Count Draynor then Elvarg |
| map-instance helpers / combat / death / drops | Private maps, real HP combat, death queues, loot | General capability exists; Getting Ahead uses none of it |

No direct script uses raw numeric entity IDs or legacy quest-list/completion
interfaces. The obsolete machinery is behavioral: base-shell triggers are
written as though interactions dispatch on the map placement ID, travel is
implemented with static teleports, the boss is a click-to-skip dialogue, item
recipes are attached to unrelated scenery, and progression uses broad numeric
guards rather than exact transactional transitions.

## 4. State and transition audit

| State | Canonical phase | Current behavior / defect |
| ---: | --- | --- |
| 0 | Standard Gordon dialogue; check both base skills; explicit accept/refuse | No skill checks or full introduction; a bare Yes/No starts and writes 2. `%ga_gordon_met` is ignored. |
| 2 | Talk to Mary about tracking | One line writes 4; no full/repeat dialogue |
| 4 | Talk to Gordon about cattle; collect flour; use it on southern gate | Gordon has only generic progress. Stairs silently block upstairs. `oplocu` binds the gate shell, but dispatch uses `ga_fencegate_l_normal`, so the route deadlocks. Right leaf is unbound. |
| 6 | Overnight/lure sequence begins | Local gate would jump directly here, consume flour without returning the pot, and print “Soft-skip.” |
| 8/10/12 | Flour gate/trail and lair approach phases | Never written. Native gate and eight flour-trail placements expect this band. |
| 14 | Beast is dead; return to Gordon | No real boss can reach it. Clicking a nonexistent/static Beast would directly write 14 and may invent bear fur. |
| 16 | Talk to Mary about a fake | One line writes 18 and incorrectly directs the player to a workbench |
| 18 | Knife on soft clay; confirm | Workbench Search instead accepts soft clay or even hard clay, requires no knife/level/confirmation, and writes 20 |
| 20 | Show clay head to Gordon | Talking with head writes 22; most transcript and item-use presentation absent |
| 22 | Add valid fur with needle/thread; confirm | Exact `ga_clayhead` item-use works in either click orientation via the four-rung engine, but accepts only bear fur, requires no needle/thread, and writes 24 under a broad `>=22` guard |
| 24 | Show fur head | Talking with head writes 26; dialogue collapsed |
| 26 | Add red dye; confirm | Recipe omits confirmation/presentation and uses broad `>=26`; writes 28 |
| 28 | Show bloody head | Talking with head writes 30; dialogue collapsed |
| 30 | Build on resolved hotspot with level, tools, head, planks, nails | Local handler binds only the wall shell, while dispatch uses `ga_mounted_head_hotspot`; legitimate Build is unreachable. Gordon incorrectly completes from state 30. |
| 32 | Mounted; final Gordon talk and scene | Gordon also completes here, but no final cutscene or Mary participation exists |
| 34 | Complete; reward/service/unlock/recovery | XP/QP scroll occurs on the normal call, but 3,000 coins are never granted, tannery and NMZ are locked, and loose earlier handlers can rewind this state |

The earliest live blocker is state 4. Even if a tester injects later state and
items, state 30 still has no reachable Build trigger; the erroneous Gordon arm
is the only nominal completion route. `::garun` hides every defect by assigning
all states and head items directly, never exercising skills, supplies, travel,
combat, transforms, construction, capacity, recovery, or unlocks.

## 5. Detailed lifecycle audit

### Start, ordinary dialogue, and requirements

Gordon's cache start actor and world spawn are correct. The local start does
not check base Crafting 30 or Construction 26, although both are non-boostable
and required to start. It omits the ordinary farm introduction, the refusal
for insufficient skills, the quest-start prompt, the promised compensation,
and the repeat greeting represented by `%ga_gordon_met`. Only explicit
acceptance may write state 2; cancellation, refusal, skill failure, and ordinary
conversation must leave state 0 unchanged.

Mary's state-2 conversation is reduced to one instruction. Gordon's required
state-4 cattle/gate clue is absent, as are all stage re-talks. The spawned
`ga_sergeant` has a Talk-to op but no handler anywhere, so the optional exchange
about the soldiers' night shift is dead. That NPC is also used by A Kingdom
Divided; route its topics compositionally rather than giving this quest a
blanket handler.

### Local supply sources and farmhouse travel

The cache and world data provide nearly every local supply, but present scripts
do not implement the canonical acquisition contract:

- `kebos_spiralstairs_bottom/top` are exact quest handlers. At every state other
  than 30–32 they return silently, overriding normal stair travel precisely
  when pot/flour, needle, and thread are needed. Their allowed teleports also
  land at 1244,3687 upstairs and Gordon's outdoor coordinate downstairs rather
  than the corresponding stair landings.
- `ga_flourbarrel` offers Take-from but has no handler. The upstairs pot-empty
  spawn therefore cannot be filled locally. The route must support the current
  barrel/pot interaction and avoid duplicate supplies.
- the kitchen knife identified by the canonical route/helper is not present in
  the generated `m19_57.spawn` inventory and no local Take-knife trigger exists.
  Resolve the map loc/ground-item source against the cache and make its current
  form testable; do not silently require an externally supplied knife.
- `ga_shelves` offers Search but has no script. Red and yellow dye appear as
  overlapping generated ground objects at the same tile, which does not
  implement the current choose-a-dye search dialogue or its repeat policy.
- `ga_workbench` canonically supplies 20 iron nails. The quest steals its Search
  op for clay sculpting, so nails cannot be obtained there at all.
- `ga_rocks_pickaxe` offers Take-pickaxe and has a no-pickaxe counterpart, but
  neither its item grant nor its per-player transform is implemented.
- the hammer, saw, two planks, upstairs needle/thread, nearby clay rocks, sink,
  bucket/pot sources, and west-side bears exist. Verify ordinary pickup respawn
  and generic sink/mining behavior rather than duplicating those owners.

The stairs should be restored as ordinary bidirectional travel for all quest
states. Quest-specific instructions belong in the journal/dialogue, not in an
exact loc override that disables the building.

### Flour gate, overnight scene, and trail

Both gate placements are native multiloc shells. At state 4 the left shell
resolves to `ga_fencegate_l_normal`; interaction dispatch deliberately keeps
the scene entity as the base but selects the trigger type and category from the
resolved child. `[oplocu,ga_fencegate_l]` can therefore never receive the click.
Bind the correct resolved children or a verified shared category route for both
leaves, and retain normal Open behavior.

The latent body is unsafe even after that binding is repaired. It accepts flour
for every `%ga >= 4`, including state 34, so a completed player could consume
flour and rewind to state 6. It deletes the filled pot without implementing the
verified empty-container result, jumps over 8/10/12, and supplies no animation,
player line, overnight transition, gate visual, or eight-location trail.

Implement one exact state-4 transaction: revalidate the selected pot of flour,
reserve/transform it, ask any required confirmation, run the protected
overnight scene, and advance through the native lure band so gate and trail
multilocs render correctly per player. Cancellation, duplicate packets,
disconnects, and a full inventory for any returned container must have a
defined resume result. States below 4 and above the lure band must never consume
flour or change progress.

### Cave entry, private instance, exit, and recovery

The current Enter accepts every state at or above 6, including completion, has
no Yes/No warning, and teleports directly to 1191,10021—the Beast's centre in
the template square. It creates no instance and spawns nothing. The ordinary
exit returns to Gordon at 1248,3686 rather than the outside entrance near
1212,3647. Native `ga_cave_out_instance` exposes both Escape and Quick-escape,
but neither op is handled.

Create a player-owned instance from the lair template using the existing map
instance API, enter at the correct inside threshold, spawn/state the Beast and
quest scenery once, and make both exit modes return to the real exterior. The
instance owner must define:

- entry at each lure/kill/post-kill state and rejection outside the quest;
- Beast alive/dead reconciliation on re-entry;
- death, teleport, logout, reconnect, region change, and manual escape cleanup;
- pool exhaustion without charging or advancing;
- isolation between two players at different states;
- cowhides visible before the kill and absent afterward; and
- skeleton/journal, bones, planks/clay context, and any static item respawns.

Never save a coordinate inside an instance that may be freed. Logout cleanup
must release the reservation and place the player at a deterministic safe
exterior/recovery coordinate.

### Headless Beast combat, special, safespot, and drops

`ga_beast` already has size 3, level 82, 100 HP, attack 60, defence 42,
strength 86, magic 42, melee attack/defend sequences, and a death sequence.
`bear_headless_beast_stomp` plus normal/faster/fastest
`ga_beast_rock_fall` spot effects also exist. The general combat/death engines
can own HP, targeting, kill credit, and delayed drops.

Local content instead registers op 1, which the NPC does not expose, and op 2
Attack as a direct click script. It prints a soft-skip, optionally creates
ordinary bear fur, and writes 14 without combat. No Beast is spawned in the
world/template spawn configs. Canonically the Beast is aggressive, attacks
with melee at speed four for up to 10, periodically slams the ground, and drops
a rock on the player's marked tile for 12–16 damage unless they move. It is
immune to poison, venom, freezes, cannon, and thralls. The pond forms a real
safespot where the special is not used.

Replace the direct Attack trigger with real NPC combat and a quest-owned AI
special using tile snapshot, telegraph, delayed spot effect, movement check,
and damage. Collision and size must make the pond safespot emerge from the map,
not an unconditional no-damage message. On valid player-attributed death, run
the canonical player line, write state 14 exactly once, remove/transform the
pre-kill cowhides, and use a drop handler for guaranteed big bones plus long
bone (1/400) and curved bone (1/5,012.5). The Beast never supplies the fur used
for the fake head.

Test melee/ranged/magic/halberd, safespot, prayer, movement at every special
boundary, simultaneous lethal hits, player death, leave/re-entry, and kill
credit. Nightmare Zone variants must share verified combat mechanics without
quest-state writes or quest drops.

### Skeleton and Neilan's journal

The cache skeleton has Search and `ga_journal` has Read, but neither trigger
exists. Searching during the quest should grant one journal with capacity and
duplicate checks; reading must render the pinned text; Drop/loss and POH
bookcase recovery must follow the shared book system. The optional journal can
be shown to Gordon at state 14 for the transcript branch without consumption.

Decode `%ga_journal` before using it. The bit must not make a full-inventory
search claim success, prevent legitimate recovery after Drop, or duplicate a
banked/bookcase copy. The journal is optional and may never gate state 14.

### Clay, fur, and bloody head transactions

Canonical clay sculpting is an inventory recipe: knife plus soft clay at state
18, followed by Yes/No and the knife/clay emote. The workbench is unrelated.
The local handler even accepts hard clay, removes it before an ineffective
free-space test (the deletion itself creates a slot), requires no knife or
confirmation, and monopolizes the nail source.

The fur recipe correctly benefits from the modern four-rung item-use resolver,
so one exact `ga_clayhead` binding can support either click orientation. Its
body is incomplete: only `fur` (Bear fur) is accepted, while `grey_wolf_fur`
and `werewolve_fur` (display name `Fur`) are valid; no ordinary/costume needle
or thread contract is checked; no confirmation or emote occurs; and the broad
stage predicate is unsafe. Establish the current costume-needle/thread
consumption policy from a live capture, then implement it atomically.

The red-dye recipe similarly needs exact state, Yes/No, presentation, and an
atomic one-for-one transform. Preserve the special rejections for vial of
blood, blood pint, raw bear meat, raw beef, and raw chicken from the pinned
article/transcript rather than consuming them.

All three heads share lifecycle invariants:

- only the recipe appropriate to the current exact state may advance progress;
- the player may hold only one of the current form;
- crafting a new form while the same form is banked destroys the banked copy;
- Destroy/death removes the item but leaves a recoverable quest state;
- loss at states 20, 22, 24, 26, 28, or 30 must allow reconstruction from soft
  clay without rewinding the primary;
- mounting removes every inventory/bank head variant and prevents all further
  crafting; and
- dialogue detail-model objects (`ga_*head_large`) are presentation assets,
  never valid inventory ingredients or a substitute for the real bloody head.

Current code violates the last five rules. In particular, a retained/banked
clay head used after state 34 would write 24, and a retained fur head plus dye
would write 28. Once the player follows the shortened route again, Gordon calls
the unguarded completion proc and grants another XP/QP/count award. Exact-state
guards and completion cleanup are mandatory economic/state-integrity fixes.

### Showing Gordon the heads and optional items

At states 14, 20, 24, and 28 Gordon must accept both Talk-to with the expected
head in inventory and using that head on him. The full failed clay/fur and
successful bloody-head conversations drive the next state only after the
dialogue completes. A missing head gives the correct re-talk without changing
state. Mary has parallel progress comments at every phase.

At state 14, using Neilan's journal, big bones, an ensouled bear head, or the
Mountain Daughter bearhead on Gordon produces distinct refusal dialogue and
does not consume or advance. The local script implements none of these use-on
branches. Keep optional interactions exact to state 14 so they cannot intercept
ordinary uses or post-quest dialogue.

### Mounted-head Build transaction

At primary 30 the `ga_mounted_head` shell resolves to
`ga_mounted_head_hotspot`; at 32 it resolves to the visible mounted head. As
with the gate, local `[oploc1,ga_mounted_head]` and its use-on sibling cannot
receive the resolved hotspot's Build click. Bind `ga_mounted_head_hotspot`
directly and leave the visible child non-interactive except for Examine.

The Build action requires current eligibility for level 26 Construction under
the verified drained-level policy, a normal or Imcando hammer, a current valid
saw/Amy's saw policy, the real bloody head, two `woodplank`, and six nails of
any cache grade. Hammer/saw are retained; head, planks, and successfully used
nails are consumed; no Construction XP is awarded here. Implement the current
nail-bending chance and ask Yes/No before commitment. Report the cache-authored
missing-material list without partial deletion.

One protected transaction must revalidate all inputs, perform build
animation/sounds, consume exact materials, purge every head variant from
inventory and bank, and only then write state 32. Duplicate Build packets,
disconnect, logout, full inventory, mixed nail grades, bent nails, and tool
alternates must never produce a mounted loc without the matching state or
charge twice.

Delete the current Gordon condition that treats state 30 as final. Only state
32 may enter the completion dialogue.

### Completion, rewards, coin recovery, and replay safety

`~ga_quest_complete` writes 34, grants the two correct XP amounts, and calls
the modern completion API. It does not add 3,000 coins, does not use
`%ga_reward`, omits coins from the reward text, and merely uses `coins` as the
scroll model. It also has no internal exact-state/idempotence guard.

The modern settlement must be callable only after the mounted state and must
award each of XP, QP, completed count, scroll, world state, and unlock markers
exactly once. Add 3,000 coins when either an existing coin stack or one free
slot can receive them. Otherwise preserve one outstanding claim in the native
reward bit; Gordon's post-quest dialogue grants it exactly once when capacity
becomes available. Re-talk, double-click, reconnect, injected stale head items,
and support-bit migration must not duplicate or lose any part of settlement.

The final conversation also needs the Gordon/mounted-head/Mary cutscene before
the reward scroll. Use player-owned or private actors; never move/delete the
public Mary or Gordon spawn for other players. Complete the protected scene or
resume at a defined settlement boundary after interruption.

### Mary's tannery and Nightmare Zone

The Mary shell transforms through `%ga_mary_dialogue`: pre-service
`ga_mary_1op` has Talk-to, while `ga_mary_2op` has Talk-to and Trade. Current
content binds Talk-to on all forms but never sets the bit and has no op-3 or
use-on delegate. After state 34, Mary's first conversation must deliver the
post-quest thanks, enable the service form, and offer the tannery; later talks
offer tan-or-exit, while Trade opens it directly.

Reuse the shared tannery transaction rather than cloning its interface logic,
but parameterize prices/products correctly for Mary. In particular, current
Wiki prices are 1/3 coins for soft/hard leather, 15 for ordinary snake hide, 20
for swamp snake hide, and 20 for each standard dragonhide. The existing generic
branch charges 15 for either snakeskin source, so blind delegation would still
be wrong. Test all hides, notes, quantities, mixed sources, insufficient coins,
capacity, cancellation, and pre-completion denial.

The cache supplies normal/hard `nzone_ga_beast` forms, but Nightmare Zone's
current script is a two-wave stub with Count Draynor and Elvarg regardless of
quest completions. Register Getting Ahead in the eventual quest-derived boss
eligibility/selectable-rumble owner and share the verified Beast AI/stat policy.
Do not add a quest-local wave hack. Tests must prove state 32 does not unlock,
state 34 does, admin completion reconciles eligibility, and NMZ kills never
touch `%ga`, quest loot, or lair world state.

### Journal, admin completion, and migration

The journal uses the modern dbrow dispatch and renderer, but collapses 17
states into five vague lines. It omits both start skills, required items, the
state-4 Gordon clue, upstairs/local sources, flour trail, private lair, loss
recovery, each head hand-in, Build materials, outstanding coins, and the
post-quest tannery conversation. Render exact current-state guidance and use
inventory/bank/support-bit conditions only where they cannot hide recovery.

The admin adapter correctly writes 34 once and awards nothing. Preserve that
contract, then run the same non-reward reconciliation used at login so Mary,
the mounted loc, NMZ eligibility, and obviously invalid quest-head remnants do
not contradict a completed primary. Never infer that `::complete` should grant
3,000 coins or XP.

Migration needs explicit cases for primary values 0–34, especially odd/corrupt
values; complete saves with orphan head items; state 30 saves that previously
completed by talking to Gordon; state 34 with unknown coin bit; state 34 with
Mary still pre-service; saves inside the static lair coordinate; and any save
whose XP/QP history cannot be reconstructed. Ambiguous reward history must not
be guessed into a duplicate economic grant.

## 6. Modernization work packages

### Package 1 — seal state corruption and restore basic route ownership

- Restrict every state-changing handler to its exact canonical state.
- Remove Gordon's state-30 completion arm and add an internal state-32 guard to
  settlement.
- Bind both resolved gate children and the resolved mounted-head hotspot.
- Restore ordinary stair travel at every state and correct both landings.
- Implement Gordon/Mary/Sergeant dialogue routing and start skill gates with
  the four native support bits.
- Add a migration/login reconciliation before exposing repaired handlers.

### Package 2 — implement supplies, lure, and private lair

- Wire the flour barrel, knife source, dye shelves, nail workbench, pickaxe
  rocks/depleted transform, skeleton, journal Read, and documented recovery.
- Implement the state-4 gate transaction, empty-pot policy, protected overnight
  scene, and native 6/8/10/12 gate/trail transforms.
- Allocate/copy/free a private lair, correct entry/exit coordinates, spawn
  state, two exit ops, reconnect/death/logout policy, and two-player isolation.

### Package 3 — replace fake combat with the real encounter

- Spawn the quest Beast and route Attack to general combat.
- Add the stomp/marked-tile falling-rock AI, immunity rules, pond safespot,
  kill credit, death line, state-14 transition, cowhide cleanup, and canonical
  drop table.
- Factor combat behavior for reuse by Nightmare Zone without quest side
  effects.

### Package 4 — rebuild fake-head and construction transactions

- Move clay sculpting from the workbench to knife-on-soft-clay.
- Support all three furs, ordinary/costume needle rules, thread policy,
  confirmations, animations, invalid uses, and exact one-for-one transforms.
- Implement singleton/bank cleanup, Destroy/death/loss recovery, and final
  purge for every head form.
- Add Gordon/Mary hand-ins/use-ons and optional evidence branches.
- Build from `ga_mounted_head_hotspot` with level, alternate tools, two planks,
  any six nails, bending, animation, atomic consumption, and no build XP.

### Package 5 — settle once and deliver both unlocks

- Run the final private cutscene and guarded completion transaction.
- Grant/recover exactly 3,000 coins through the native reward bit; include the
  coins on the modern scroll alongside correct XP, QP, and unlock lines.
- Enable Mary's post-dialogue multinpc and correctly parameterized shared
  tannery.
- Register quest-derived Headless Beast eligibility in the modernized NMZ boss
  selector.
- Expand journal/admin/login reconciliation and close all downstream tests.

## 7. Verification matrix

Automated transition coverage must include at least:

| Scenario | Required assertion |
| --- | --- |
| Start skills | Base 29/30 Crafting crossed with base 25/26 Construction and boosted values; only both qualifying bases may reach the accept prompt |
| Start choice/re-talk | No, cancellation, insufficient skills, first/repeat ordinary greeting, and Yes; only final Yes writes 2 |
| Mary/Gordon/Sergeant | Every state-specific Talk-to and optional Sergeant topic; no shared A Kingdom Divided interception |
| Farm stairs | Both directions at 0, 4, 22, 30, 32, and 34; correct landings and no silent override |
| Supply sources | Flour/pot, knife, dye choice, 20 nails, one pickaxe/depleted rocks, hammer, saw, planks, needle/thread, bucket/sink/clay; capacity and repeat policy |
| Gate dispatch | Left/right leaf and both item-use directions against resolved normal children; exact state 4 only |
| Lure scene | Confirmation/cancel, returned container, forced 6/8/10/12 phases, eight trail locs, interruption/reconnect, no post-complete rewind |
| Instance | Pool available/exhausted, two players, entry/exit/quick-exit, death, logout, teleport, reconnect, alive/dead re-entry, no saved freed coordinate |
| Beast normal combat | Stats, size/collision, max hits, speed, aggression, immunity, prayer, all attack styles, pond safespot, special telegraph/dodge/hit |
| Beast death/drop | One state-14 write and line; guaranteed big bones; seeded long/curved rolls; no invented fur; pre-kill cowhides disappear |
| Journal | Full inventory, inventory/bank copy, Search, Read, Drop, bookcase recovery, show Gordon, optional/no progression gate |
| Clay recipe | State 18, both use directions, hard clay rejection, missing knife, Yes/No, animation, full inventory, duplicate packets |
| Fur recipe | All three furs, ordinary/costume needle, thread policy, each missing component, Yes/No, animation, both directions |
| Dye recipe | Red dye both directions, Yes/No, special blood/meat rejections, no other dye acceptance |
| Head lifecycle | Lose/Destroy/death/bank/recreate at every 20–30 checkpoint; bank duplicate purge; no state regression at 32/34 |
| Gordon hand-ins | Talk and use-on for each real head plus journal, big bones, ensouled bear head, and bearhead; exact state/no consumption |
| Mounted Build | Resolved hotspot reachability; base/drained level; each tool alternate; 2 planks; mixed nail grades/bending; full material matrix; duplicate/interruption |
| Completion | State 30 never completes; state 32 completes once; scene ownership; XP/QP/count/scroll exactly once after double-click/relog |
| Coins | Existing coin stack, one free slot, full inventory/no stack, later Gordon claim, repeated claim, ambiguous migrated bit |
| Mary tannery | First postquest talk changes form; Talk and Trade; every hide/product/price, quantity, coins, capacity, notes, cancellation; pre-34 denied |
| Nightmare Zone | 32 locked, 34 eligible/selectable, normal/hard forms, shared AI, no `%ga` writes or quest drops, admin completion reconciliation |
| Admin/migration | First `::complete quest_gettingahead` writes 34, second no-op, neither grants rewards; all enumerated legacy/corrupt cases converge safely |

Gate D commands and evidence:

1. `python3 tools/questhelper_extract.py gettingahead --check`.
2. Quest-specific static audit: exact state writers, resolved-child trigger
   bindings, support-bit consumers, symbolic resolution, instance ownership,
   completion/journal/cheat registration, and no undisclosed `soft-skip` or
   `Deferred` marker.
3. `make -C src mock230-scripts` and `mock230_pack --check-only` against the
   intended cache.
4. Automated route, transaction, combat, migration, two-player, tannery, and
   NMZ tests covering the matrix above.
5. Two-client headless smoke from Gordon through both gate leaves, private
   Beast combat, all three heads, real Build, reward scroll, coin recovery,
   Mary Trade, and a selectable NMZ Beast.
6. Packet/screenshots for quest-start menu, overnight trail, falling-rock
   special, private-instance isolation, each crafting confirmation, wall Build,
   final scene, completion scroll, and tanning interface.

## 8. Prioritized findings

| Priority | Finding | Player impact |
| --- | --- | --- |
| P0 | Flour handler binds `ga_fencegate_l` shell while dispatch uses resolved `ga_fencegate_l_normal`; right leaf is absent | Required state-4 action has no reachable trigger |
| P0 | Exact staircase overrides return silently outside 30–32 | Canonical flour/needle/thread collection and ordinary farmhouse travel are blocked |
| P0 | No Beast spawn/instance/combat exists; Attack is a click-to-skip state write | Mandatory boss, danger, special, safespot, death, and kill credit are absent |
| P0 | Wall handler binds shell, not resolved `ga_mounted_head_hotspot` | Required Build action is unreachable |
| P0 | Gordon completes directly from state 30 | Players/injected saves bypass all level, tool, plank, nail, and head construction checks |
| P0 | Gate/head recipes use broad lower-bound guards and can write earlier states after 34 | Retained quest items can rewind completion and replay XP, QP, completed count, and scroll |
| P0 | Completion never grants 3,000 coins and has no full-inventory recovery | Entire item reward is missing |
| P1 | Crafting 30 / Construction 26 are not checked before quest start | Non-qualifying players enter a route they should not start |
| P1 | Workbench replaces knife crafting and removes the canonical 20-nail source | Wrong recipe/tool/material behavior and a missing required local supply |
| P1 | Three head forms lack singleton, bank, Destroy/death, loss recovery, and mounted cleanup contracts | Deadlocks, duplicates, stale items, and state-rewind inputs persist |
| P1 | Static cave teleports target the Beast centre and Gordon; instance exit ops are absent | Incorrect geography, no player isolation, and unsafe session recovery |
| P1 | Mary's native postquest form/Trade op and shared tannery delegates are never enabled | Advertised tannery reward is unusable |
| P1 | Nightmare Zone ignores Getting Ahead and contains only two hardcoded bosses | Advertised Headless Beast unlock is unusable |
| P1 | Flour barrel, shelves, pickaxe rocks, skeleton/journal, Sergeant, and optional Gordon item uses are unwired | Local supplies, lore, and current transcript branches are missing |
| P1 | Beast skip invents bear fur and omits guaranteed big bones/tertiary bones | Quest supplies and loot are materially wrong |
| P2 | Dialogue/journal collapse 17 states into a few generic lines | Requirements, clues, re-talks, recovery, and current narrative are absent |
| P2 | Debug `garun` fabricates all state/items and is treated as a route test | False completion confidence; it proves only variable assignment |

## 9. Current evidence and acceptance boundary

Completed during this audit:

- traced every `%ga` read/write and confirmed all four native support bits have
  no live consumer;
- decoded the native dbrow, start marker/actor, primary, state 34, requirements,
  QP, recommended combat, and XP;
- resolved the direct NPC, loc, obj, sequence, spot-animation, map, spawn, and
  item symbols against the osrs239 cache;
- proved gate and mounted-head shell bindings disagree with the engine's
  resolved-child loc dispatch;
- traced stair interception, cave teleports, workbench ownership, every head
  transaction, completion ordering, journal, cheat, shared quest-point proc,
  tannery, and Nightmare Zone consumers;
- compared every canonical route phase, supply, optional interaction, reward,
  and loss/recovery rule with the pinned Wiki revisions; and
- ran the pinned Quest Helper extractor successfully.

Not yet performed: no gameplay implementation was changed, no compile/pack
claim is made, no transition/combat/instance/tannery/NMZ test exists, and no
real-client smoke or capture has been recorded. `verified-modern` requires all
P0/P1 findings, all five work packages, and the complete verification matrix to
pass. Until then, Getting Ahead remains `audit-pending`; the normal route stops
at state 4 and the debug route is not acceptance evidence.
