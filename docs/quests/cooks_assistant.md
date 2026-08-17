# Cook's Assistant modernization audit

Status: `audit-pending` — the native quest row, native progress varp, Cook
dispatcher, dynamic journal, ingredient hand-in, Cooking XP, shared completion
scroll, quest-point award, and post-quest dialogue all exist. The quest can be
completed with pre-obtained ingredients, or with an undocumented ordinary-cow
milk workaround. Its canonical new-player collection route is not intact:
Cold War owns the dairy cow's `Milk` operation and returns "Nothing interesting
happens" outside its cowbell stage. The permanent range reward is also exposed
before completion, while a production self-test shadows item-on-range cooking.
The final hand-in deletes all three ingredients before a non-persistent player
queue commits progress and rewards. This is a small, recognisable legacy port,
not yet a verified modern quest.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to the native three-state quest, all ingredient sources,
the shared Cook operation, completion transaction, Cook-o-matic access, Recipe
for Disaster hand-off, diary dependency, journal, speedrunning metadata, debug
adapter, and reconnect behavior. It is an implementation specification, not
verification evidence.

## 1. Authoritative references

The Wiki article and quick guide define mechanics, requirements, rewards, and
the collection route. The transcript defines initial choices, refusal, help,
re-talk, hand-in, and the finale. Revisions were resolved through the OSRS Wiki
API on 2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Cook's Assistant](https://oldschool.runescape.wiki/w/Cook%27s_Assistant?oldid=15240921) | 15240921, 2026-06-27 | Identity, requirements, route, rewards, and downstream requirements |
| [Cook's Assistant/Quick guide](https://oldschool.runescape.wiki/w/Cook%27s_Assistant/Quick_guide?oldid=15238952) | 15238952, 2026-06-24 | Required containers, collection locations, and completion route |
| [Transcript:Cook's Assistant](https://oldschool.runescape.wiki/w/Transcript%3ACook%27s_Assistant?oldid=15263168) | 15263168, 2026-07-14 | Start choices, refusal, ingredient help, re-talks, hand-in, and finale |
| [Cook](https://oldschool.runescape.wiki/w/Cook_%28Lumbridge%29?oldid=15303539) | 15303539, 2026-08-16 | Shared actor dialogue and cross-quest ownership |
| [Cooking range (Lumbridge Castle)](https://oldschool.runescape.wiki/w/Cooking_range_%28Lumbridge_Castle%29?oldid=15116937) | 15116937, 2026-01-30 | Completion-only access and reduced burn chance |
| [Dairy cow](https://oldschool.runescape.wiki/w/Dairy_cow?oldid=15302655) | 15302655, 2026-08-16 | Canonical Milk operation and dairy-cow behavior |
| [Cow](https://oldschool.runescape.wiki/w/Cow?oldid=15254984) | 15254984, 2026-07-06 | Distinguishes ordinary cows from the intended dairy-cow source |
| [Bucket of milk](https://oldschool.runescape.wiki/w/Bucket_of_milk?oldid=15281482) | 15281482, 2026-07-29 | Milk sourcing and container lifecycle |
| [Egg](https://oldschool.runescape.wiki/w/Egg?oldid=15203418) | 15203418, 2026-04-29 | Egg sourcing |
| [Pot of flour](https://oldschool.runescape.wiki/w/Pot_of_flour?oldid=15286977) | 15286977, 2026-08-04 | Flour sourcing and container lifecycle |
| [Mill Lane Mill](https://oldschool.runescape.wiki/w/Mill_Lane_Mill?oldid=15264815) | 15264815, 2026-07-16 | Hopper, controls, flour bin, and 30-pot capacity |
| [Gillie Groats](https://oldschool.runescape.wiki/w/Gillie_Groats?oldid=15246817) | 15246817, 2026-07-01 | Optional milk directions |
| [Millie Miller](https://oldschool.runescape.wiki/w/Millie_Miller?oldid=15258541) | 15258541, 2026-07-09 | Optional flour directions |
| [Recipe for Disaster/Another Cook's Quest](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Another_Cook%27s_Quest?oldid=15302151) | 15302151, 2026-08-15 | Completion prerequisite and unboostable level 10 Cooking gate |
| [Lumbridge & Draynor Diary](https://oldschool.runescape.wiki/w/Lumbridge_%26_Draynor_Diary?oldid=15295884) | 15295884, 2026-08-13 | Easy task requiring bread baked on the Cook-o-matic |
| [The Lost Tribe](https://oldschool.runescape.wiki/w/The_Lost_Tribe?oldid=15292326) | 15292326, 2026-08-10 | Competing Cook dialogue during the cellar incident |
| [Death to the Dorgeshuun](https://oldschool.runescape.wiki/w/Death_to_the_Dorgeshuun?oldid=15292353) | 15292353, 2026-08-10 | Later Zanik/Cook shared-actor interaction |
| [Quest Speedrunning](https://oldschool.runescape.wiki/w/Quest_Speedrunning?oldid=15286399) | 15286399, 2026-08-03 | Native speedrun-row intent |

The current contract is a free-to-play, novice, very short quest released on 4
January 2001. It has no quest or skill prerequisites. The player needs an egg,
a bucket of milk, and a pot of flour; an empty bucket and pot are additionally
needed when gathering the latter two from scratch. Completion awards 1 quest
point and 300 Cooking XP, and permanently permits use of the Cook-o-matic 25,
whose burn chance is better than an ordinary range for supported foods.

Cook's Assistant is required for Recipe for Disaster's introductory subquest.
That subquest also requires level 10 Cooking, which the current Wiki marks as
unboostable. The Easy Lumbridge & Draynor Diary asks the player to bake bread
on the Cook-o-matic, making correct completion gating a downstream contract,
not merely reward text.

Transition aid only: the local Quest Helper checkout's Cook's Assistant helper
at commit [`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/cooksassistant)
groups native states 0 and 1 into the quest route and resolves six item symbols,
three NPC symbols, eight loc symbols, and `%mill_flour`. Its dairy-cow target is
the `fat_cow` loc at `(3172, 3317)`. `python3
tools/questhelper_extract.py cooksassistant --check` exits 0. Quest Helper cannot
prove server trigger precedence, persistence, reward atomicity, range access,
or dialogue ownership.

## 2. Native quest identity and state contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 1 |
| Type | Free-to-play quest; starter quest |
| Difficulty / length | 0 / 0; novice / very short |
| Start | Cook (`cook`, native NPC 4626) in Lumbridge Castle kitchen |
| Prerequisites | None |
| Required items | Egg, bucket of milk, pot of flour |
| Collection containers | Empty bucket and empty pot when self-gathering |
| Primary state | `%cookquest`, native permanent transmitted varp 29 |
| Quest points | 1 |
| Completion XP | 300 Cooking (`3000` tenths) |
| Permanent unlock | Completion-only Cook-o-matic access and lower burn chance |
| Downstream | Recipe for Disaster intro; Easy Lumbridge & Draynor Diary task |
| End state | 2 |
| Speedrun metadata | Native speedrun dbrow 3436 and best-time varp exist |

The cache row correctly supplies display name, release date, start NPC and
coordinate, quest points, end state, Cooking XP, starter flag, and speedrun row.
There is no reason to introduce an authored parallel quest-progress carrier.
The existing native `%cookquest` varp is sufficient.

### Primary state inventory

| State | Canonical phase | Current implementation |
| ---: | --- | --- |
| 0 | Not started; full top-level conversation, help request, accept/refuse | Cook forces the "What's wrong?" path; accept writes 1; refusal remains 0 |
| 1 | Accepted; gather, ask for help, report/hand in ingredients | Inventory checks and all-items hand-in exist; detailed help and canonical partial acknowledgement do not |
| 2 | Complete; range access, post-quest and dependent quest dialogue | Shared completion reaches 2, but range is not gated and dependent RFD uses a boostable stat check |

Only state 2 has an authored constant. Active scripts compare raw `0` and `1`.
Modernization should name all three states while preserving their native values,
so lifecycle checks and tests express intent without changing save data.

The transcript does not require persistent per-ingredient turn-in bits: the
Cook recognises which ingredients are currently carried, but does not consume
them until all three are presented. Inventory-based tracking is therefore
appropriate. It still needs explicit behavior for banked/dropped items, repeated
talks, and the immediate all-items-on-accept route.

## 3. Implementation surface

The quest root contains 336 lines in two configs and two scripts.

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `configs/quest_cook.constant` | Complete state and quest-point constant | Native values correct; start/in-progress constants absent |
| `configs/quest_cook.varp` | Native `%cookquest` declaration | Correct permanent, transmitted, unpacked varp |
| `scripts/quest_cook.rs2` | Cook dispatcher, start, re-talk, hand-in, completion queue, post-quest routing | Recognisable route; dialogue compressed; completion non-atomic; stale engine comments |
| `scripts/cook_journal.rs2` | Three-state dynamic journal | Modern dispatch path; wrong title and grammar; inventory-only presentation |

Mandatory cross-directory surfaces include:

| Surface | Relationship / modernization requirement |
| --- | --- |
| `configs/all.dbrow` | Preserve native ID 1, metadata, state 2, 1 QP, 300 Cooking XP, and speedrun row |
| `configs/all.loc` | `fat_cow`, hopper/controls/bin, and `cooksquestrange` native operations and morphs |
| `configs/all.obj` | Empty/filled bucket, egg, grain, empty pot, and pot of flour lifecycle |
| `areas/world/configs/m49_51.spawn` | Canonical dairy cow and nearby egg spawns |
| `areas/world/configs/m50_50.spawn` | Castle kitchen Cook, range, and empty-pot source |
| `general_use/scripts/windmills.rs2` | Shared grain-to-flour mechanic for Mill Lane Mill and other windmills |
| `npc/scripts/cow_milking.rs2` | Noncanonical ordinary-cow workaround |
| `quest_coldwar/scripts/coldwar_lumbridge.rs2` | Exact `fat_cow` operation currently intercepting Milk as cowbell theft |
| `quest_idesofmilk` Gillie script | Owns Gillie's Talk-to and omits Cook's Assistant help outside that quest |
| Millie Miller world spawn | Native actor is present, but no Talk-to handler was found |
| `skill_cooking/scripts/cooking.rs2` | Modern range cooking and Cook-o-matic success chance, currently without quest gate |
| `selftest_useon.rs2` | Exact production range use-on binding shadows generic cooking |
| `interface_questjournal/scripts/quest_journal.rs2` | Dynamic dbrow dispatch correctly reaches `~cook_journal` |
| `quests/scripts/questpoints.rs2` | Shared scroll, QP/count, and jingle lifecycle |
| `quests/scripts/quest_cheat.rs2` | State-2 adapter exists; must remain idempotent and establish the gated unlock |
| `quest_recipefordisaster` | Cook actor reuse and level-10 Cooking start gate |
| `quest_losttribe` / Lumbridge Bob routing | Competing cellar-incident conversation and witness flow |
| POH quest-hall landscape logic | Correctly treats Cook's Assistant as one Lumbridge painting requirement |
| diary and speedrunning systems | Native/downstream metadata exists, but no Cook-specific production implementation was found |

There is one `[opnpc1,cook]` declaration, which is the right ownership model:
the handler routes Lost Tribe, Cook's Assistant, and Recipe for Disaster instead
of relying on duplicate triggers whose compile order can replace one another.
Modernization should retain one dispatcher and make its priority matrix explicit.

## 4. Canonical route and dialogue audit

| Phase | Canonical behavior | Current behavior / consequence |
| --- | --- | --- |
| Initial greeting | Four choices: ask what is wrong, ask for a cake, comment on unhappiness, or compliment the hat; side branches rejoin appropriately | The script immediately selects "What's wrong?" and omits three branches and their holiday/pirate/cake/hat dialogue |
| Offer | Cook explains the birthday problem; player accepts or refuses | Core two-choice accept/refuse and state write are present, though text is compressed |
| Accept while ready | If all three ingredients are already carried, the Cook recognises them and the same conversation proceeds to hand-in | State becomes 1 and dialogue ends; player must Talk-to again |
| Ingredient help | Cook gives a repeatable item menu with flour, milk, and egg directions; advice responds to empty pot/bucket ownership | Only a one-line list is given; all sourcing/help branches are absent |
| Re-talk with none | Player may get right on it or ask to be reminded how | One fixed reminder; no choice or detailed directions |
| Re-talk with some | Cook acknowledges what is present and lists what remains, without consuming partial ingredients | Inventory-dependent list exists, but canonical acknowledgement/hand-over phrasing and branch details are compressed |
| Re-talk with all | Cook takes one egg, milk, and flour and concludes the crisis | Core check and deletion exist |
| Finale | Cook thanks the player; conversation covers Duke Horacio's party before completion | The finale stops at "I am saved, thank you!" and immediately schedules rewards |
| Complete | Ordinary post-quest conversation, Cook-o-matic explanation, and later quest routing | Four-choice legacy small talk exists; RFD routes exist; completion-gated range permission is not enforced |

The initial side branches are not cosmetic dead text: they are reachable
transcript choices and establish normal actor behavior before acceptance.
Implement them with current `~p_choice*` helpers and `last_slot`; no legacy IF1
choice machinery is necessary.

The all-items-at-start path must not require an extra click. Acceptance can
write state 1, then inspect the inventory and fall directly into the shared
hand-in procedure. That procedure must have one authoritative item/reward
transaction regardless of whether it is reached from initial dialogue or a
later re-talk.

The Lost Tribe check currently precedes all Cook's Assistant states and returns
while its cellar-incident condition is active. A routing test must prove that
this temporary red-herring dialogue does not strand a player who is starting or
finishing Cook's Assistant and that Bob remains the actual witness path. The
Cook's other later uses, including Recipe for Disaster's Pirate Pete and Sir
Amik Varze branches, must retain their intended priority after state 2.

## 5. Ingredient acquisition audit

### 5.1 Milk: canonical action is intercepted

The native dairy cow is a loc named `fat_cow`, placed at `(3172, 3317)` near
the Lumbridge chicken farm. Its first operation is `Milk`; its second is
`Steal-cowbell`. The only exact first-operation binding is in Cold War:
`[oploc1,fat_cow]` performs cowbell theft during a narrow Cold War state and
otherwise prints "Nothing interesting happens." Consequently the Wiki and
Quest Helper route cannot produce Cook's Assistant milk.

The generic milking script binds use-empty-bucket to the attackable NPCs `cow`,
`cow2`, and `cow_beef`, swaps the bucket immediately, and prints "You milk the
cow." That is an undocumented, noncanonical workaround. It omits the native
dairy-cow loc, `cow3`, movement/animation timing, and the dairy-cow interaction
contract. Its existence means the quest is not globally impossible, but does
not make the intended starter route valid.

Modernization must centralize the dairy cow's operations by operation number or
an explicit dispatcher:

1. `Milk` requires an empty bucket, handles full inventory without item loss,
   performs the expected interaction, and produces one bucket of milk.
2. `Steal-cowbell` delegates to Cold War and enforces that quest's stage.
3. Use-bucket-on-dairy-cow and menu-op behavior agree with the native config.
4. Ordinary cows do not silently become the documented quest route unless the
   current game explicitly supports them.

Gillie Groats and Millie Miller are optional guidance, not hard progress gates.
Nevertheless, the transcript/current route uses them to teach collection.
Gillie's Talk-to is currently swallowed by Ides of Milk and otherwise says only
"Mind the cows!"; Millie is spawned but has no discovered Talk-to handler. Add
shared-actor routing without redeclaring duplicate NPC triggers.

### 5.2 Flour: mechanically narrow and detached from native state

The shared windmill script supports using grain on the hopper, operating the
control, and taking flour with an empty pot. It uses two authored permanent
booleans, `%hopper_full` and `%mill_flour_ready`, across every windmill. It does
not use native `%mill_flour`, which Quest Helper and the cache expose as a count,
or `%mill_showflour`, which drives the `millbase` multiloc.

This creates four observable defects:

- the flour-bin visual never changes because no production write to
  `%mill_showflour` was found;
- one boolean entitlement cannot represent Mill Lane Mill's capacity of up to
  30 pots of flour;
- loading at one windmill and operating or withdrawing at another can share the
  same player-global state; and
- grinding another grain while flour is ready cannot increment a count.

The single-pot Cook's Assistant route may still work because handlers bind the
base symbolic loc and consult the authored boolean even when the visible morph
does not change. That must be confirmed in a client smoke; it is not evidence
that the shared mechanic is correct. Modernize the windmill once using the
native count/visual contract and mill identity or a well-defined equivalent,
then regression-test every affected windmill rather than adding quest-specific
flour.

### 5.3 Egg and containers

World spawn audit finds ordinary egg ground spawns near the Lumbridge farms,
including the canonical chicken-coop area. An empty bucket is spawned in the
castle cellar and an empty pot in the castle kitchen; store alternatives are
also part of the canonical route. These are generic, tradeable objects and do
not need quest-specific ownership bits.

Tests must still cover picking up/buying each container with a full inventory,
dropped or banked ingredients, carrying duplicates, and completing with exactly
one of each. The hand-in must remove one egg, one bucket of milk, and one pot of
flour while leaving duplicates untouched.

## 6. Completion, rewards, and range access

### 6.1 The current completion has a loss window

The hand-in deletes all three ingredients, then schedules
`queue(cooks_quest_complete, 0, 0)`. On the next tick that queue writes state 2,
grants 300 Cooking XP, and invokes `~quest_complete_rewards`. Player normal
queues are runtime state cleared during logout cleanup and are not persisted in
the player save. A disconnect, queue cancellation, or conflicting queue in the
one-tick gap can therefore leave the player at state 1 with all ingredients
gone and no XP, quest point, completion count, or reward scroll.

The queued handler also has no `state = 1` guard. It writes the end state before
the XP and shared lifecycle calls, so an abort after the first write produces a
partially completed permanent save; a duplicate delivery can grant XP twice.
The shared completion helper already owns its own modern reward-panel timing,
so a separate unprotected quest queue is not a sound UI boundary.

Replace this with one guarded, idempotent transaction:

1. Validate state 1 and all three required quantities.
2. Reserve/remove exactly one of each without yielding or closing the player
   session between removal and the permanent commit.
3. Commit state, XP, quest points/completed count, and unlock state exactly once
   through the shared lifecycle.
4. Mount the completion panel through the shared helper after the committed
   dialogue boundary.
5. Make repeated clicks, duplicate resumes, logout/reconnect, and `::complete`
   harmless.

If the script VM cannot express rollback around the shared lifecycle, introduce
a general quest-completion transaction capability rather than a Cook-specific C
shortcut. Test failure injection at each mutation boundary.

### 6.2 Cook-o-matic reward is both early and shadowed

The modern Cooking script recognises `cooksquestrange`, opens the generic
cooking menu on its oven category, and applies the cache's
`successchance_cookomatic` values. No `%cookquest` gate exists in that path.
Because the range is physically accessible in the kitchen, a state-0 or state-1
player can click it and receive the completion reward's lower burn chance. The
Wiki contract says the Cook stops non-completers from using it.

Separately, `server/scripts/selftest_useon.rs2` declares the exact production
trigger `[oplocu,cooksquestrange]`. It writes `%mock_quest_progress` and does not
decline to the generic oven-category handler. Exact-name trigger specificity
therefore shadows item-on-range cooking for every player, before and after the
quest. The quest source comment claiming `oplocu` is undispatched is stale; this
self-test exists specifically to prove that dispatch, and the modern Cooking
system consumes it.

Gate both direct `Cook` and item-on-loc entry paths before invoking generic
Cooking. At states 0 and 1, the Cook should stop the action with canonical
dialogue; at state 2, both paths must delegate normally and apply the special
chance only for supported foods. Move the self-test to a dedicated fixture loc
or make it decline without intercepting production behavior. Test ordinary and
Cook-o-matic burn tables on both sides of completion.

The quest source also says no MIDI jingle exists. The shared completion helper
now implements the jingle policy, so that comment must be removed or corrected
as part of modernization. Stale porting comments are dangerous here because
they currently justify two live defects.

## 7. Journal, debug, speedrunning, and downstream contracts

The journal correctly opens through the dynamic quest list by dbrow and uses
`~quest_journal`; no per-quest IF1 list component remains. Its title is
incorrectly rendered as "The Cook's Quest" rather than "Cook's Assistant", and
the active text says "Duke of Lumbridges'". State 1 marks ingredients from live
inventory, which is suitable only while the Cook consumes nothing partially.
Unknown values above 1 are treated as complete; use exact native state handling
and a safe diagnostic fallback.

The `::complete quest_cooksassistant` arm sets state 2 and relies on the shared
cheat lifecycle for quest points/count. It intentionally does not reproduce XP
or narrative rewards. Once range access is correctly state-gated, the cheat's
state adapter will establish that unlock. Verify it twice and prove the second
invocation changes nothing.

The cache supplies a Cook's Assistant speedrun row and a permanent best-time
varp, but focused production search found no Cook-specific server use of either.
Record speedrun setup, start, timing, reset, and completion behavior as an
unverified cross-system gap; do not invent quest-local timing if the shared
speedrunning system is itself absent.

The completed Cook dispatcher starts Recipe for Disaster when
`stat(cooking) >= 10`. `stat` is the boostable current value, while the pinned
Wiki says level 10 is unboostable. Use `stat_base(cooking)` and correct any
contradictory RFD metadata after live/cache confirmation. Preserve Pirate Pete
and Sir Amik Varze routing. Focused search did not find the Easy diary's bread
task or Death to the Dorgeshuun's Zanik/Cook visit; record these as downstream
integration gaps pending their own quest/diary audits, rather than silently
expanding Cook's Assistant state.

## 8. Defect ledger

| Priority | Defect | Player impact | Required proof |
| --- | --- | --- | --- |
| P0 | `fat_cow` Milk op is owned by Cold War cowbell logic | Canonical self-gather route cannot obtain milk; starter teaching route fails | State-0 player obtains milk from the native dairy cow; Cold War cowbell op still works independently |
| P0 | Ingredient deletion precedes a non-persistent, unguarded completion queue | Disconnect/cancel can destroy required items without completion; duplicate delivery can duplicate XP | Failure-injection and reconnect tests at every hand-in boundary |
| P0 | Cook-o-matic is usable before completion and item-on-range is shadowed by a self-test | Reward is granted early; normal item-on-range cooking is broken after completion | Pre-completion denial plus post-completion click/use-item cooking and burn-table tests |
| P1 | Windmills use global authored booleans instead of native count/visual state | Empty visual, cross-mill state leakage, one-pot cap versus 30 | Multi-player/multi-mill, relog, visual morph, and 0/1/30-pot tests |
| P1 | Start, help, re-talk, and finale transcript branches are compressed/absent | Reachable narrative and tutorial guidance missing; ready-on-accept requires extra interaction | Transcript branch matrix from state 0 and state 1 with every item subset |
| P1 | RFD intro uses boostable `stat(cooking)` | Boosted level can bypass an unboostable downstream requirement | Base 9 + boost rejected; base 10 accepted |
| P1 | Shared Cook priority is implicit | Lost Tribe/RFD conditions can swallow another valid Cook conversation | Routing matrix across Cook, Lost Tribe, and all three RFD states |
| P2 | Gillie help is swallowed and Millie has no discovered Talk-to | Optional canonical collection guidance is unavailable | Actor dialogue tests outside/inside owning quests |
| P2 | Journal has wrong title/grammar and loose unknown-state handling | Visible fidelity and diagnostic errors | Golden journal output for 0, every state-1 item subset, 2, and invalid state |
| P2 | Stale source comments claim modern use-on/jingle machinery is absent | Future work is directed around capabilities that now exist | Comment/source audit after implementation |
| P2 | Speedrun, Easy diary, and Zanik/Cook integrations are not found | Native/downstream features may be absent | Cross-system audits and live proof before claiming support |

`P0` here denotes lifecycle or canonical-route defects that must be closed before
the quest can be called modern. The availability of trade/pre-obtained items and
the ordinary-cow workaround must not downgrade the broken official route.

## 9. Modernization work packages

### WP1 — native state, dialogue, and actor routing

- Add symbolic constants for states 0, 1, and 2 without changing varp values.
- Rebuild the transcript branch graph with modern chatmenu helpers.
- Share one guarded hand-in entry from ready-on-accept and state-1 re-talk.
- Define and test the Cook dispatcher priority for Lost Tribe, this quest,
  normal post-quest talk, and each RFD branch.
- Correct the journal title, grammar, item subsets, and invalid-state behavior.

### WP2 — shared ingredient systems

- Split native dairy-cow Milk and Steal-cowbell operations without duplicate
  trigger ownership; add full-inventory and interaction behavior.
- Add Gillie and Millie shared dialogue routing where current transcript calls
  for it.
- Replace windmill booleans with the native count/visual model or a proven
  equivalent with explicit per-mill ownership and 30-pot capacity.
- Verify egg, bucket, pot, store, ground-spawn, bank, and duplicate-item paths.

### WP3 — atomic completion and range unlock

- Make the item removal, state transition, XP, QP/count, and completion-scroll
  lifecycle guarded and idempotent without a logout loss window.
- Gate both Cook and item-on-range paths by native state 2.
- Remove the exact production self-test interception and retain equivalent
  coverage on a fixture.
- Correct stale engine comments and prove the jingle/shared panel lifecycle.

### WP4 — downstream integrations

- Enforce RFD's unboostable base Cooking requirement and reconcile metadata.
- Verify the Easy diary bread task receives only legitimate post-quest range
  use.
- Audit speedrun start/reset/timer behavior and later shared Cook dialogue.
- Keep POH quest-hall landscape eligibility working from state 2.

### WP5 — automated and live verification

- Add transition/invariant tests and a deterministic hand-in failure harness.
- Compile and pack against the intended osrs239 cache.
- Run a real-client route from the Cook through all three self-gathered
  ingredients, completion scroll, range cooking, and post-quest dialogue.
- Capture the modern chatmenu/completion packets and the flour-bin morph.

## 10. Verification matrix

| Scenario | Expected result |
| --- | --- |
| Fresh state, every initial choice | All four transcript branches work; refusal remains 0; acceptance becomes 1 |
| Accept while carrying all ingredients | Same conversation consumes one each and completes exactly once |
| State 1 with each of eight ingredient subsets | Correct acknowledgement/help/reminder; no partial item loss |
| Milk via `fat_cow` with/without bucket and full inventory | Canonical result or accurate explanation; no loss; Cold War op remains separate |
| Flour from Mill Lane Mill | Grain, hopper, control, visible bin, and up to 30 pots survive relog and do not leak to another mill/player |
| Egg/container collection and stores | Canonical sources work with capacity and purchase checks |
| Logout at every hand-in boundary | Player resumes coherently; never loses ingredients without completion |
| Repeated Cook clicks / duplicate completion resume | One state transition, 300 XP, 1 QP, one completion count, one scroll |
| Range before state 2 | Both direct Cook and item-on-range are denied by Cook dialogue |
| Range after state 2 | Both entry paths cook normally; supported foods use Cook-o-matic chance |
| RFD at base/current 9/10 combinations | Only base level 10 or higher passes |
| Lost Tribe and RFD routing matrix | Exactly one intended conversation owns every combination |
| Journal 0/1/2 and invalid | Correct title, wording, item subset, completion, and safe fallback |
| `::complete quest_cooksassistant` twice | First establishes coherent end state/QP/count/unlock; second is a no-op |
| Speedrun setup/reset/finish | Native row and best-time state work without contaminating normal saves |

Required static/build commands after implementation:

```sh
python3 tools/questhelper_extract.py cooksassistant --check
make -C src mock230-scripts
src/build/mock230_pack --check-only
git diff --check
```

Also run trigger-uniqueness and symbolic-reference audits for `cook`, `fat_cow`,
`millbase`, hopper/controls, Gillie, Millie, and `cooksquestrange`. Static
success is not enough: Gate D requires a real-client smoke and captured evidence
for the start menus, canonical ingredient route, completion UI, and both range
entry paths.

## 11. Exit criteria

Cook's Assistant may become `verified-modern` only when:

1. a fresh player can accept or refuse through every transcript branch and
   self-gather all three ingredients using the canonical world interactions;
2. all item subsets, help/re-talk paths, full inventory, loss, bank, duplicate,
   relog, and reconnect cases are coherent;
3. hand-in and rewards are atomic and idempotent, with exactly 300 Cooking XP,
   1 quest point, one completed-count increment, the jingle, and the modern
   completion scroll;
4. both Cook-o-matic entry paths are blocked before state 2 and fully functional
   with the reduced burn chance afterward;
5. Lost Tribe, RFD, POH, diary, and speedrun integrations have explicit passing
   evidence;
6. the journal and post-quest dialogue match the native state and current
   transcript; and
7. the static checks, osrs239 compile/pack, automated transition/lifecycle
   suite, and real-client smoke all pass with commands and captures recorded.

Until those conditions hold, the inventory status remains `audit-pending`.
