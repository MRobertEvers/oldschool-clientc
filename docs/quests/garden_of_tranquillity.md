# Garden of Tranquillity modernization audit

Status: `audit-pending` — the native quest row, primary and secondary state,
all nine garden transforms, both source-statue/destination-plinth transforms,
trolley NPC variants, Falador cutscene actors, specialist NPCs, quest items,
journal dispatch, completion adapter, reward potion mechanic, and current
Varrock trellis are present. The authored route is nevertheless not
command-free completable: throwing the activated ring requires an item-use
packet while that same ring remains equipped. It also uses incompatible
primary-state meanings, malformed diplomacy choices, session-only crop
timers, one-seed garden planting, instant statue teleports, non-atomic item
grants, incomplete rewards, an ungated shortcut, and no White Tree fruit
lifecycle.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to Ellamaria and the list, the Wise Old
Man and Ring of charos activation, all six specialist routes, four shared
Farming patches, the White Tree cutting, the Edgeville well, Alain and shared
item crafting, all nine palace patches, both statue journeys, the two
cutscenes, completion and rewards, the trellis and fruit tree, downstream
prerequisites, shared NPC topics, journal/admin adapters, and recovery. It is
an implementation specification, not completion evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable route, dialogue, item, timing, reward, recovery, and unlock
contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Garden of Tranquillity](https://oldschool.runescape.wiki/w/Garden_of_Tranquillity?oldid=15293044) | 15293044, 2026-08-11 | Identity, requirements, complete route, crop timing/death, statue transport/failure, rewards, and downstream requirements |
| [Garden of Tranquillity/Quick guide](https://oldschool.runescape.wiki/w/Garden_of_Tranquillity/Quick_guide?oldid=15285765) | 15285765, 2026-08-02 | Ordered interactions, current diplomacy answers, planting quantities, trolley order, and finale choices |
| [Transcript:Garden of Tranquillity](https://oldschool.runescape.wiki/w/Transcript%3AGarden_of_Tranquillity?oldid=15263325) | 15263325, 2026-07-14 | Accept/refuse, list replacement, specialist topic menus, full-inventory behavior, lost-item recovery, trolley mechanics, cutscenes, and post-quest dialogue |
| [Ring of charos](https://oldschool.runescape.wiki/w/Ring_of_charos?oldid=15217181) | 15217181, 2026-05-26 | Permanent activation, loss/replacement rules, well recovery, fishing chance, and downstream charm uses |
| [White Tree patch](https://oldschool.runescape.wiki/w/White_Tree_patch?oldid=15262619) | 15262619, 2026-07-13 | Native growth/fruit lifecycle, four-fruit capacity, five-minute regrowth, and harvest XP |
| [White tree fruit](https://oldschool.runescape.wiki/w/White_tree_fruit?oldid=15184227) | 15184227, 2026-04-22 | Pick requirements, energy/health effect, compost use, and non-tradeable behavior |
| [Trolley (empty)](https://oldschool.runescape.wiki/w/Trolley_(empty)?oldid=14977270) | 14977270, 2025-08-31 | Trolley is a quest NPC, not merely an inventory-token teleport |
| [Trellis (Varrock)](https://oldschool.runescape.wiki/w/Trellis_(Varrock)?oldid=14892977) | 14892977, 2025-04-30 | Post-quest and level-35 Agility gates, denial dialogue, direction, sound, and zero XP |
| [Compost potion](https://oldschool.runescape.wiki/w/Compost_potion?oldid=15184321) | 15184321, 2026-04-22 | Four-dose reward and bucket/full-bin conversion behavior |
| [Swan Song](https://oldschool.runescape.wiki/w/Swan_Song?oldid=15288122) | 15288122, 2026-08-05 | Garden completion as a mandatory prerequisite and shared Wise Old Man interaction |
| [Varrock Diary](https://oldschool.runescape.wiki/w/Varrock_Diary?oldid=15293707) | 15293707, 2026-08-12 | Medium task requiring a White Tree fruit |

The sources define a members, intermediate, long quest released 30 August
2005. It requires completed Creature of Fenkenstrain and level 25 Farming,
non-boostable and required to start. It has no combat. Rewards are two quest
points, 5,000 Farming XP, permanent Ring of charos activation, one apple tree
seed, one acorn, five guam seeds, a four-dose compost potion, access to four
regrowing White Tree fruits, and the level-35 southern-trellis shortcut. It is
required for Swan Song, Defender of Varrock, and the Medium Varrock Diary.

Transition aid only: Quest Helper's
[`GardenOfTranquillity.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/gardenoftranquility/GardenOfTranquillity.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` (file last changed
2026-04-22) confirms primary phases 0/10/20/30/40/50, every specialist
threshold, 3- or 4-seed requirement, patch threshold, trolley state, route
waypoint, coordinate, requirement, and reward. `python3
tools/questhelper_extract.py gardenoftranquility --check` resolves every
referenced item, NPC, loc, and varbit. Its reported target dbrow is wrong: the
extractor mistakes the prerequisite `quest_creatureoffenkenstrain` for the
quest's own row. Its copied diplomacy choices are also stale at the sixth
question. The current Wiki and transcript remain authoritative.

## 2. Native quest identity and contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_gardenoftranquillity`; dbrow pack index 58, metadata ID 90 |
| Type / difficulty / length | Members quest / intermediate / long |
| Release / start | 30 August 2005 / `queen_ellamaria` at 3230,3478,0 |
| Prerequisite | `requirement_quests` stores dbrow pack index 19, which `all.dbrow.compack` resolves to `quest_creatureoffenkenstrain`; it is not metadata ID 19/Lost City |
| Skill | Farming 25, non-boostable and checked on start |
| Primary | `%garden_quest`, bits 0–5 of permanent `garden_varp_1`; canonical phases 0, 10, 20, 30, 40, 50; end state 60 |
| WOM/cutscene support | `%garden_cutscene_billybob` and `%garden_first_time_login`; their exact live semantics require trace before reuse or deletion |
| Specialists | Elstan 0–4, Lyra 0–3, Kragen 0–3, Dantaera 0–2, Althric 0–2, and Bernald 0–5 in named native varbits |
| Shared crop markers | Lyra patches 7/8 and Kragen patches 5/6 each have one-bit quest markers; two additional `garden_kragen_patch_5/6` bits remain semantically unproven |
| Ring | `%garden_ring_in_well_varbit`, 0 out / 1 in well; activation must remain permanent independently of possession |
| Palace patches | Eight 3-bit fields: weeds 0–2, weeded 3, seed 4, growing 5–6, full 7; White Tree is 4-bit: weeds 0–2, weeded 3, planted 4, growing 5–7, full 8, one through four fruit 9–12 |
| Statues | King and Saradomin fields: 0 source present, 1 in transit, 2 placed; the same fields drive source locs and destination plinths |
| Trolley | Native multinpc: 0 empty, 1 Saradomin loaded, 2 king loaded; push, pull, big-push, and place ops exist |
| Presentation assets | Billy and Bob guards, `PKMaster0036`, trolley animations, both statue transforms, `king_roald_cutscene`, Queen/Roald animations, every garden growth loc, and the modern trellis exist |
| End / rewards | State 60; 2 QP; 50,000 raw Farming XP; apple tree seed ×1, acorn ×1, guam seed ×5, compost potion(4) ×1; activated ring remains |

The local source note makes a category error: a `dbrow` reference is a pack
index, not the row's `id` column. `all.dbrow.compack` directly maps 19 to
Creature of Fenkenstrain and 86 to Lost City. The cache prerequisite is
correct; the comment alleging recurrent corruption must be removed.

The primary values are not free reconstruction space. Quest Helper maps 10,
20, and 30 to the successive Wise Old Man conversation/test phases, 40 to the
entire garden-making chapter, and 50 to King Roald. The transcript separately
defines first WOM approach, return with the ring, and test-retry states. Local
secondary flags carry collection, crops, planting, and statues; the primary
must remain the canonical chapter field.

## 3. Implementation surface

The direct root contains 1,588 lines in thirteen scripts/config files. The
real ownership surface also includes shared NPC, Farming, Herblore, general
item-use, ladder, prerequisite, journal, and administrative code.

| Quest-owned path | Present responsibility | Audit result |
| --- | --- | --- |
| `configs/gardenoftranquility.constant` | Source notes, primary aliases, patch/statue mappings, timers, rewards, coords | Rich cache inventory, but falsely calls the prerequisite corrupt, invents state 1 meanings, dismisses native fields without trace, and declares critical soft-skips acceptable |
| `configs/gardenoftranquility.varp` | Claims the two native packed carriers | Correct carrier ownership; must not be replaced by parallel progress vars |
| `scripts/garden_shared.rs2` | Ellamaria, aggregation, trolley grant, Roald, completion | No list/start confirmation, wrong primary writes, coarse hints, compressed finale, incomplete/non-atomic rewards |
| `scripts/garden_wom.rs2` | Diplomacy test and ring activation | Malformed menus, wrong answers, no 10/20/30 phases, unsafe worn-ring conversion, no permanent activation/recovery contract |
| `scripts/garden_elstan.rs2` | Falador marigold bargain and hooks | Uses real flower patch, but steals ordinary Elstan dialogue and can consume the marigold then lose seeds |
| `scripts/garden_lyra.rs2` / `garden_kragen.rs2` | Onion/cabbage bargains and bespoke patches | Replace shared Farming with invisible three-minute session timers; skip weeds, water, disease, death, harvest, recovery, and atomic seed grants |
| `scripts/garden_dantaera.rs2` | White Tree shoot item chain | Correct item family, but consumes no water, loses growth across logout/bank/drop, and offers no recutting after loss |
| `scripts/garden_althric.rs2` | Well, rose permission, seed taking, ring fishing | Required ring-disposal packet is impossible in normal play; recovery is non-atomic and uses an invented flat chance |
| `scripts/garden_bernald.rs2` | Vine cure, Alain, essence/shards/dust | Core recipe is present, but Alain's shared dialogue is permanently stolen, full inventory wrongly blocks 1→1 transforms, and seed grant/recovery is unsafe |
| `scripts/garden_finalgarden.rs2` | Nine palace patches and aggregate | Uses native transforms but skips raking/growth stages, consumes one instead of 3–4 seeds, drops compost buckets, and relies on session timers |
| `scripts/garden_statues.rs2` | Both source statues | Replaces both trolley journeys, Falador cutscene, failure/reset, and placement with player-visible `soft-skip:` text and immediate state 2 |
| `scripts/garden_journal.rs2` | Journal text | Correct modern API, but wrong spelling and primary phases; omits substeps, missing items, failures, and recovery |
| `scripts/garden_debug.rs2` | Reset/start/headless commands | Useful development scaffolding only; directly creates items/states and cannot prove gameplay |

Mandatory external owners:

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `quest_makingfriendswithmyarm/scripts/makingfriendswithmyarm.rs2` | Shared Wise Old Man dispatcher | Swan Song wins first, Garden second, then RFD/My Arm; first-match returns can hide simultaneously valid topics and Garden currently owns only local state 1 |
| `areas/varrock/scripts/king_roald.rs2` | Shared King Roald dispatcher | Garden state 50 wins before Dragon Slayer II, Defender of Varrock, Priest in Peril, and Shield of Arrav; compose topics instead of stealing them |
| `quest_fenkenstrain/scripts/fenkenstrain_finish.rs2` | Ring replacement | Always returns the unactivated ring, ignores the ring-in-well flag, and does not capacity-check; it can duplicate a well-held ring and breaks permanent activation |
| `skill_farming/scripts/farming_plant.rs2` / `farming_harvest.rs2` | Elstan marigold lifecycle | Correctly scopes callbacks to Falador flower patch 1; extend the same durable shared Farming model to Lyra/Kragen rather than bespoke quest growers |
| `general_use/scripts/hammer.rs2` / `skill_herblore/scripts/grind_ingredient.rs2` | Essence→shards→dust dispatch | Correct shared trigger seam; transaction and relevance rules belong in the Garden procs |
| `skill_farming/scripts/farming_craft.rs2` | Compost-potion use and persistent seedlings | Bucket conversion works in one direction and provides the durable date-based seedling pattern; full compost-bin conversion remains missing |
| `ladders_stairs` loc category/maplinks | Southern garden trellis | Generic `climb_unqualified` teleports both directions for every player, with no quest/35 Agility gate, Queen denial, or sound |
| `quest_swansong` / `quest_defenderofvarrock` | Downstream quest gates | Both explicitly soft-skip Garden because this port was considered pending; modernization must turn these into hard completion checks |
| quest journal / quest cheat | Dynamic row dispatch / admin completion | Both use the right row; cheat is the correct state-only, idempotent adapter and must not award route XP/items |

There is no legacy `if_openmain`/`if_openoverlay` or raw numeric entity ID in
the direct root. The old machinery is behavioral: incompatible state meaning,
session timers, bypassed shared Farming, impossible item-use assumptions,
first-match shared dialogue, instant transport, text in place of cutscenes,
and completion without an exactly-once settlement.

## 4. State and transition audit

### Canonical phases versus local meanings

| State | Canonical phase | Current behavior / defect |
| ---: | --- | --- |
| 0 | Requirements, Ellamaria exposition, explicit Start quest? Yes/No, capacity-safe list grant | Starts after abbreviated dialogue with no confirmation, base Farming check, list, title/presentation, or refusal; boosted 25 is accepted |
| 1 | Not a canonical Quest Helper chapter | Local acceptance write. Imported/native clients do not recognize it |
| 10 | First Wise Old Man phase | Local means test already passed and ring activated |
| 20 | Wise Old Man ring/test continuation | Local means all six specialist arcs are done and planting may begin |
| 30 | Wise Old Man retry/activation continuation | Never written or handled locally |
| 40 | Activated ring; collect/grow seeds, plant the garden, and move statues, all through secondary fields | Local enters only after all nine plants are fully grown, so almost the whole quest is reported under wrong chapters |
| 50 | Ellamaria has approved the finished garden; bring King Roald | Local reaches this when both statues are set, which is broadly the right finale phase, though Ellamaria's approval is collapsed into the write |
| 60 | Complete | Correct numeric end state; settlement and post-quest world effects are incomplete |

State 10 is a direct compatibility collision: a canonical save at its first
WOM visit looks like a local save whose ring was activated. State 20 likewise
means a WOM checkpoint canonically but “all seeds ready” locally. Migration
must inspect a version marker plus ring form, specialist fields, patch fields,
and inventory/bank ownership; changing constants alone will corrupt both kinds
of saves.

### Native secondary transitions

| Carrier | Canonical lifecycle | Current divergence |
| --- | --- | --- |
| Elstan | 0→1 bargain; 2 planted after request; 3 harvested; 4 marigold exchanged; replacements while required seeds are missing | Correct markers, but terminal 4 is written even if four seeds fail to fit |
| Lyra / Kragen | 0→1 bargain; patch marker on valid shared-Farming plant; NPC reaches 2 only when a living crop is fully grown; 3 only after full seed-set grant | Timers set 2 without crop state; full-inventory grants still write 3; no death/replant |
| Dantaera | 0→1 permission; 2 cutting taken; cutting may be replaced if every downstream form is lost | Value 2 permanently blocks recutting |
| Althric / well | 0→1 challenged; remove/destroy/throw activated ring; 2 permits seed taking; well flag persists until successful fishing | Local requires ring both selected for Use and still worn; clears well before capacity-safe ring grant |
| Bernald | 0→1 deal; 2 weak cure tried; 3 Alain consulted without ring; 4 strong cure works; 5 full four-seed grant | Writes 5 after a failed grant and never replaces missing seeds |
| Palace patches | Rake 0→1→2→3; plant exact 3/4 set at 4; durable 5→6→7 growth; White Tree through 8 | Planting accepts only 0/1, consumes one, jumps 4→7, and cannot resume after lost timers |
| Statues / trolley | source 0; load writes statue 1 and trolley 1/2; move NPC with collision; route transition; correct adjacent plinth writes statue 2 and returns item | Directly writes 0→2 and leaves trolley at 0; native in-transit states are unreachable |

## 5. Detailed lifecycle audit

### Start, list, and Ellamaria

The start needs the current non-boostable base Farming check and completed
Creature of Fenkenstrain before an explicit confirmation. Acceptance requires
one free slot, grants `garden_list`, commits primary 10 exactly once, and
leaves cancellation at 0. The list is not optional flavor: it is readable,
usable on patches/plinths, explains quantities/locations, and Ellamaria
replaces it if lost when capacity permits. The native item exists, but no
Garden script references it. Plinth inspection, plant-pot inspection, list
reading, use-on-Ellamaria responses, watering guidance, and active-stage topic
menus are all absent.

The local `stat(farming)` start check accepts a temporary boost, immediately
writes 1, and offers neither refusal nor a list. Later Ellamaria dialogue is a
single generic line until local state 40. Rebuild her as a topic router over
list replacement, statue trolley, planting guidance, incomplete-patch report,
Roald handoff, reward explanation, fruit permission, and unrelated future
topics. Capacity checks must happen before state commits or item removal.

### Wise Old Man and Ring of charos activation

Restore primary 10/20/30 around the transcript's first conversation, request
to bring the ring, test attempt/failure, and retry. The test has seven
questions: the first six require the current Wiki answers, while the seventh
may be answered any way. The sixth current answer is “Ask me nicely and I
might consider it”; the local/QH-derived monarchy answer is stale.

Every local `p_choice3` mistakenly places the scenario itself in selectable
slot 1 instead of presenting it as dialogue. Question 1 happens to accept the
actual correct answer in slot 3. Questions 2–5 test for slot 2, which is the
authored wrong answer; questions 6–7 test for the scenario text in slot 1.
Question 6 omits the current correct answer entirely, and question 7 wrongly
requires one answer. Use a separate question message followed by a real
three-choice menu, server-validate each response, and persist the retry phase.

Activation must reconcile every unactivated ring in inventory, worn equipment,
and bank as the live rule requires. It must atomically replace the selected
ring without needing an extra slot, preserve the item if interrupted, commit
primary 40 only after successful conversion, and record permanent activation
for future Fenkenstrain replacements. Local conversion deletes a worn ring;
with a full backpack it cannot add the activated form, yet still advances.

Shared Wise Old Man routing needs an explicit topic menu when Swan Song,
Garden, RFD, and My Arm are simultaneously relevant. Priority returns are not
the dialogue contract, especially after Garden correctly owns 10–30 instead of
local state 1.

### Elstan, Lyra, Kragen, and shared Farming

Elstan's marigold correctly uses the real Falador flower patch and only counts
a marigold planted after his request. Preserve all normal gardener choices:
protection, advice, shop, and exit. Protection must be refused for the requested
quest crop as the transcript specifies, without hiding unrelated valid crops.
The 20-minute marigold uses normal compost, watering, disease, cure, death,
harvest, XP, and persistent growth. Exchange one harvested marigold only when
four delphinium seeds can be delivered; at state 4, offer replacements whenever
the player lacks the required unplanted quantity across inventory/bank.

Lyra's onions and Kragen's cabbages are ordinary allotment crops in either of
their two local patches. They take about 40 minutes, may become diseased or
die, cannot be protected by the gardeners, require an empty flower patch, and
must be planted after the request. Local code instead consumes three seeds,
sets a one-bit marker, and starts a 300-tick player timer. It never rakes,
composts, waters, transforms, becomes diseased, dies, is cured, or survives a
logout/restart; it declares the crop grown after roughly three minutes even if
no crop exists.

Bring patches 5–8 under the shared Farming owner using the same durable
`date_minutes`/growth-arm model already used by other crops. Quest marker bits
record “planted after request”; the crop system remains authoritative for
stage, disease, death, and full growth. Either allotment in each pair satisfies
the request. Notify once when a qualifying crop becomes full. Lyra must grant
three pink plus three yellow orchid seeds atomically, requiring two slots only
when neither stack exists; Kragen grants four snowdrop seeds. Do not write NPC
state 3 on capacity failure. Both NPCs replace the exact missing unplanted seed
sets and retain their ordinary Farming menus.

### Dantaera and the White Tree cutting

Dantaera's permission, secateurs/magic-secateurs alternatives, filled plant
pot, trowel, watered cutting, and five-minute growth all belong in the normal
seedling lifecycle. Cutting must check capacity before setting value 2.
Subsequent cutting is denied while any shoot, potted shoot, watered shoot, or
sapling exists, but becomes available again after all forms are lost and the
palace tree is not planted. Dantaera has explicit loss dialogue.

Local watering changes only the shoot: it never decreases `watering_can_N` to
the next charge. Its one-shot timer is attached to the session and inventory;
logout loses it, and banking/dropping the watered shoot before it fires makes
the timer silently disarm forever. `garden_has_watered_shoot` can advance local
primary 20 while the shoot is still watered and not plantable. Reuse the
shared persistent seedling deadline and canonical watering-can transform,
reconcile inventory/bank forms on login, and require the actual sapling or an
already-planted palace tree before considering this leg ready.

### Brother Althric, rose seeds, and ring recovery

After Althric challenges the activated ring, the player may destroy it or use
it on the Edgeville well from inventory. The local handler requires
`last_useitem=ring_of_charos_unlocked` while also requiring that ring in
`worn`. An ordinary client cannot select an equipped ring as an item-use
source, so the command-free route deadlocks here. Accept the inventory item,
atomically remove it, then set the well and permission flags. The alternative
Destroy path must also unlock the roses and preserve permanent activation.

Each bush gives four seeds and refuses cleanly at full inventory. Missing
unplanted rose colours remain obtainable; “has seeds” means four of each, not
one. Local `garden_has_rose_seeds` accepts one and all palace handlers later
consume one.

The well accepts normal or fly fishing rods, not oily/barbarian rods. Fishing
success scales from 13% at Fishing 1 to 37% at 99; local flat one-in-three is
invented. On success, reserve capacity before clearing the well flag. Local
clears it first and can permanently lose the ring. While the flag says the
ring is in the well, Fenkenstrain must not yield another; after destruction or
later loss, Fenkenstrain must return the activated form because activation is
permanent. The current shared Fenkenstrain handler violates both rules and can
create an unactivated duplicate while the activated ring remains in the well.

### Bernald, Alain, and the strong cure

Preserve the two-cure sequence: accept while wearing the activated ring, use
one ordinary plant cure, speak to Bernald, talk to Alain with no ring equipped,
hammer one rune or pure essence, grind the shards, combine dust with the second
cure, speak to Bernald, use the strong cure, then collect four vine seeds.
Each transform is 1→1 or 2→1 and should work in a full inventory when the
consumed input frees the output slot. Local hammer/grind reject full inventory
unnecessarily; all transforms also need stale-packet revalidation.

Alain is the shared Taverley tree gardener. Local state 3+ permanently returns
the Garden recipe line before any ordinary gardener dialogue, including after
quest completion. Compose the quest topic with protection/advice/shop topics.
Bernald must grant all four seeds before state 5 and replace a missing
unplanted set thereafter. The local full-inventory path writes 5 without any
seeds and strands the quest.

### Ellamaria's nine patches

All non-orchid patches begin with three visible weed stages and must be raked
to value 3. The two orchid pots require one ordinary/super/ultracompost bucket
each and must return empty buckets; a bottomless bucket is not accepted. Then
consume exact sets atomically: four delphinium, four snowdrop, four vine, four
of each rose colour, three of each orchid colour, and one watered-grown White
Tree sapling. Wrong patch, insufficient quantity, no dibber/spade/rake,
watering, composting a fertile non-orchid patch, and digging an established
plant all have defined feedback.

Local plain-patch handlers permit only values 0–1, so they plant through weeds
but reject native weed value 2 or weeded value 3. They consume one seed, jump
to value 4, and a 150-tick session timer jumps to 7. White Tree jumps 4→8.
Orchid compost deletes the filled bucket without returning an empty bucket.
Modernize these as durable, restart-safe growth schedules that drive every
native stage. The palace plants take 10–15 minutes and cannot die; they need no
watering. Completion aggregation should derive from all nine full states, not
advance merely because seed-provider dialogue flags were written.

### Statue transport and Falador cutscene

Ellamaria gives/replaces one trolley capacity-safely. Using it on the correct
Lumbridge king or Falador Saradomin statue consumes/transforms the inventory
token into the private native trolley NPC, writes the statue to in-transit 1,
and writes trolley 2 or 1. Push moves one collision-valid tile away, Pull one
toward, Big-push up to four, and Place works only adjacent to the correct
plinth. Crossing the Lumbridge bridge or Falador north boundary moves the
private trolley to the north Varrock Palace route. Placement writes statue 2
and returns the inventory trolley.

The Falador load runs the Billy/Bob/PKMaster0036 distraction cutscene using the
native actors and `%garden_cutscene_billybob`, with camera, protected private
ownership, skip/cleanup, and reconnect boundaries. A current Wiki bug note is
not permission to omit the scene. Both journeys have a timed recapture path
(the current article describes five minutes; the transcript records a shorter
historic branch), teleport/random event/logout/stuck reset, trolley removal,
source restoration, and replacement from Ellamaria. Pin the target behavior
with a live current-client trace before encoding the exact deadline.

Local use-on-statue is not phase-gated, never consumes the inventory trolley,
never reaches statue/trolley in-transit states, runs no NPC, collision, route,
cutscene, deadline, reset, adjacency, or wrong-plinth rule, and immediately
writes state 2. Its player-facing message literally includes `soft-skip:`.
This is a missing defining mechanic, not a cosmetic deviation.

### Ellamaria approval, Roald, finale, and completion

When all nine plants are fully grown and both statues are placed, Ellamaria
enumerates incomplete patches if necessary and only then writes primary 50.
King Roald's shared menu must expose “Would you like to follow me for a
minute?”, the long non-ring argument, both charm steps, refusal, and unrelated
quest topics. On success run the native/private Garden Tour scene with Roald,
Ellamaria, player, camera, slap animation, dialogue, interruption cleanup, and
a resumable post-scene handoff to the Queen.

Local Roald routing always wins at state 50, compresses follow and the whole
scene into two messages, changes the story outcome to delight, and settles
inside the shared NPC script. It sets state 60 and Farming XP before checking
reward capacity; it grants only a compost potion when one slot is free and
then completes regardless. It omits the apple tree seed, acorn, and five guam
seeds. Interruption between state/XP/item/API calls can leave partial rewards.

Use an exactly-once settlement boundary or explicit reward ledger. Reserve or
stage four distinct reward slots as needed, grant one apple tree seed, one
acorn, five guam seeds, and compost potion(4), apply 5,000 Farming XP, state
60, two QP/count, jingle, icon, and scroll once. The activated ring is retained,
not duplicated. Repeated Roald/Queen packets, reconnect, scene resume, and
admin completion must not replay route rewards.

Post-quest Ellamaria needs the compost-potion explanation and garden/fruit
permission choices. Bucket compost conversion already works when the bucket is
the item-use target; full and big compost bins still need the shared potion
operation. This is an unlock regression adjacent to the quest reward.

### White Tree fruit, trellis, and downstream consumers

After completion, the White Tree progresses 8→9→10→11→12 at about one fruit
per five minutes and holds four. Pick-fruit capacity-checks, adds one
`garden_white_tree_fruit`, decrements the stage, grants 12 Farming XP, and
re-arms durable regrowth. The fruit restores 5–10% run energy and 3 Hitpoints
when eaten and can enter supercompost. The cache contains all loc stages and
the item's Eat op, but no script handles picking or consumption; the tree is
permanently left at fruitless value 8. Consequently the Medium Varrock Diary
task is impossible.

The modern southern trellis requires Garden state 60 and base/current level 35
Agility as appropriate to shared shortcut policy, works both directions,
plays its jump sound/animation, gives zero XP, and has Queen denial before
completion. It is currently imported into generic `climb_unqualified`, so
every player can teleport across it with no quest or skill check. The claimed
reward is therefore pre-unlocked and mechanically wrong.

Swan Song and Defender of Varrock both document Garden as a mandatory
prerequisite but intentionally do not enforce it. Replace those soft skips
with `%garden_quest >= 60` gates after migration is available. Audit all
activated Ring of charos consumers against permanent recovery, but do not make
Garden completion a condition for using the already-activated ring: activation
occurs at primary 40 and remains useful even before the quest ends.

### Journal, admin, and shared-topic behavior

The journal uses `~quest_journal`, but spells the title with one `l`, follows
local 1/10/20/40 meanings, and reports whole chapters instead of actionable
substates. Rebuild it from canonical primary plus specialist, crop, item,
well, garden, trolley, and settlement state. It must identify the next missing
seed/crop/patch/statue, loss recovery, full-inventory pending grants, ring in
well, interrupted trolley, incomplete growth, and Roald scene resume.

`::complete quest_gardenoftranquillity` correctly writes 60 once and lets the
common cheat layer own QP/count without XP/items. Preserve that separation.
The quest-specific `gardenoftranquilityrun` command creates items, writes
states, and calls the finale; it is a fixture helper, not Gate D evidence.

## 6. Migration and recovery

Introduce a one-time schema/migration marker outside the already packed native
bits. Never infer local versus canonical meaning from primary 10 or 20 alone.
Take a pre-migration snapshot and reconcile in this order:

| Existing evidence | Migration action |
| --- | --- |
| State 0 | Preserve; clear only impossible orphaned quest fields after separately auditing admin/test accounts |
| Local state 1 | Map to canonical 10; restore/offer the list without duplicating a carried/banked copy |
| State 10 with activated ring, specialist progress, or other local-only evidence | Map to 40; record permanent ring activation |
| State 10 without that evidence | Preserve as canonical WOM phase and resume dialogue |
| State 20 with all/most provider fields or palace readiness evidence | Map to 40; do not treat it as the canonical WOM continuation |
| State 20 without local provider evidence | Preserve as canonical WOM phase |
| State 30 | Preserve as canonical WOM retry/activation phase; local code never authored it |
| State 40 | Preserve as canonical making-garden chapter; derive exact next step from secondary state |
| State 50 | Preserve only when finished plants/statues or legacy finale evidence supports it; otherwise repair to 40 |
| State 60 | Preserve completion; separately reconcile QP/count and reward settlement from auditable history, never from current inventory absence |

Recovery rules must be permanent gameplay, not migration-only patches:

- Missing list: Ellamaria replaces one, capacity-safely.
- Specialist state says reward granted but the full unplanted seed set is
  absent from inventory/bank: the owning specialist replaces the exact set.
- Dantaera state 2 with no cutting form and palace tree below planted: permit
  another cutting.
- Ring in well: only the well can return it; capacity failure leaves the flag
  set. Ring destroyed/lost outside the well: Fenkenstrain returns activated
  form after first activation.
- One-shot local crop/patch states with no deadline: preserve already-full
  crops; for planted/growing states assign a documented migration deadline or
  make them immediately eligible once, then continue under durable growth.
- In-transit statue/trolley inconsistencies: reconcile to one private trolley
  when safe, otherwise restore the source, clear trolley state, and let
  Ellamaria replace the inventory trolley.
- Legacy state 60 has no trustworthy evidence that ordinary seed rewards were
  received, banked, traded, or consumed. Use an explicit one-time legacy claim
  ledger informed by account/audit history; do not grant repeatedly based on
  item absence. Admin-completed accounts must remain state-only.

For every item exchange, calculate required slots after consumed inputs and
existing stacks, revalidate at commit, add outputs successfully, then advance
state. For every growth system, persist an absolute deadline and re-arm on
login/region entry; `garden_first_time_login` should not be assigned a new
meaning until its original behavior is traced.

## 7. Modernization sequence

### Gate A — normalize contract and shared routing

1. Correct the prerequisite/comment and primary 0/10/20/30/40/50/60 meanings;
   add migration fixtures before changing live reads.
2. Inventory/trace the list, `%garden_cutscene_billybob`,
   `%garden_first_time_login`, extra Kragen fields, trolley region boundaries,
   current recapture deadline, and finale actors/cameras.
3. Build shared topic routers for Wise Old Man, King Roald, Elstan, Lyra,
   Kragen, Alain, Ellamaria, and Fenkenstrain.
4. Restore start confirmation, base-level requirement, list grant/read/use/
   replacement, and the canonical WOM phases/test/atomic ring activation.

### Gate B — shared Farming and item ownership

1. Extend shared allotment Farming to patches 5–8 with persistent growth,
   disease/death/cure, visual transforms, quest-after-request markers, and
   gardener protection refusal.
2. Make all six specialist grants atomic and recoverable; restore ordinary
   gardener topics and exact seed quantities.
3. Move the White Tree shoot into durable seedling growth, charged watering,
   recutting, bank/relogin reconciliation, and permanent ring recovery.
4. Correct strong-cure transactions and shared Alain/Fenkenstrain behavior.
5. Rebuild all nine palace patches with raking, exact quantities, returned
   buckets, native growth stages, durable deadlines, inspect/error ops, and
   aggregation.

### Gate C — trolley, scenes, completion, and unlocks

1. Implement private trolley NPC ownership, push/pull/big-push/place,
   collision, both region routes, source/plinth transforms, timer and
   teleport/logout/stuck recovery.
2. Build the Billy/Bob/PKMaster0036 and Garden Tour cutscenes with protected
   actors, camera lifecycle, skip/resume, and cleanup.
3. Implement Ellamaria's exact incomplete-garden handoff and compose Roald's
   charm/non-charm/shared topics.
4. Add guarded, capacity-safe, resumable, exactly-once completion with every
   reward and post-quest dialogue.
5. Implement White Tree fruit/regrowth/eating/compost use, the gated level-35
   trellis, and hard Swan Song/Defender of Varrock/Diary consumers.
6. Rebuild journal and admin assertions from canonical state and recovery.

### Gate D — regression and integration

1. Run migration over every ambiguous primary collision, missing reward,
   timer, seed, ring, patch, trolley, and settlement combination.
2. Reverify Creature of Fenkenstrain completion/replacement, shared Farming,
   every specialist's ordinary topics, Wise Old Man/Roald multi-quest topics,
   compost potion, shortcuts, Swan Song, Defender of Varrock, and Varrock
   Diary before/during/after Garden.
3. Run static pack/build, Quest Helper extraction, transition/property tests,
   private-actor two-player tests, and a real-client command-free 0→60 route.
4. Record captures for list/menu behavior, Farming transforms, both trolley
   routes, both cutscenes, completion scroll, fruit, shortcut denial/success,
   recovery, and post-quest dialogue before changing status.

## 8. Verification matrix

| Area | Required checks |
| --- | --- |
| Start | Creature incomplete/complete; Farming 24/25 and boosted 24; 0/1 free slot; accept/refuse/re-talk; list grant/read/use/lose/replace; exact state 10 |
| WOM phases | Canonical 10/20/30 entry/re-talk; ring inventory/worn/bank/duplicates/missing; all answer choices; sixth current answer; seventh any answer; fail/retry; interruption; atomic activation; exact state 40 |
| Shared WOM | Every Garden 0/10/20/30/40/60 × Swan Song/RFD/My Arm relevant state; topic composition; no branch theft |
| Elstan | Plant before/after request; every flower-patch state; water/compost/disease/cure/death; harvest; protection refusal; marigold exchange; 0/1 slot; seed loss/replacement; ordinary topics |
| Lyra/Kragen Farming | Either/both local allotments; weeds, compost, water, disease, cure, death/replant; empty/nonempty flower patch; plant-before-request; persistent 40-minute growth; logout/restart; full grants; exact 3+3/4 replacements |
| Dantaera | Normal/magic secateurs; no capacity; every cutting form; watering-can 1–8 transform; five-minute offline growth; bank/drop/logout; lose/recut; wrong item; palace already planted |
| Althric/well | Ring worn/inventory/destroyed; well transaction; permission; each rose set exact four; full inventory; missing colours; normal/fly/oily/barbarian rods; Fishing 1/99 probabilities; capacity failure; well/Fenkenstrain exclusivity |
| Bernald/Alain | Ring worn/removed; weak and strong cure order; rune/pure essence; full 1→1 transforms; wrong/stale item use; exact four seeds; loss/replacement; ordinary Alain gardener topics |
| Palace patches | Every weed state 0–3; rake/dibber/spade; exact 3/4 quantities; wrong patch; two compost types/pots; bucket returns; bottomless refusal; water/compost/spade denials; durable stages 4–7/8; cannot die; all planting orders |
| Trolley grant | No/one/full slot; existing item/private NPC/bank/ground; repeated request; list/trolley Ellamaria topics; two players |
| Lumbridge statue | Correct/wrong statue; load state 0→1; push/pull/big-push collision; bridge transition; Varrock path; wrong/far/correct plinth; deadline/teleport/logout/stuck reset; item return |
| Falador statue | Correct source; Billy/Bob/PKMaster0036 scene; camera skip/disconnect cleanup; load/route/boundary/place; recapture/reset; current banner collision; two players isolated |
| Ellamaria/Roald | Every incomplete patch/statue enumeration; state 40→50; ring/no ring; Roald refusal/non-charm/charm; simultaneous DS2/DoV/Priest/Arrav topics; Garden Tour skip/resume/logout |
| Completion | 0–4 free slots and existing reward stacks; repeated packet/queue/login; exact 5,000 XP, four item rewards, activated ring retention, 2 QP/count, state, jingle, icon, and scroll once |
| White Tree | State 8–12; five-minute offline increments; four-fruit cap; pick full inventory; stage decrement; 12 Farming XP; eat energy/HP bounds; supercompost input; Medium Diary trigger |
| Trellis | Quest 59/60; Agility 34/35; both directions; denial dialogue; animation/sound; zero XP; no generic bypass |
| Journal | Every primary × specialist/crop/item/well/patch/trolley/scene/settlement combination; loss and capacity recovery; exact double-l title; complete styling |
| Downstream/shared | Swan Song and Defender hard gates; activated-ring consumers before quest completion; Fenkenstrain before/after Great Brain Robbery location; compost bucket/bin; all specialist shops/protection/advice |
| Migration/admin | Local 0/1/10/20/40/50/60 and canonical 0/10/20/30/40/50/60 × secondary evidence; timers; rewards; ring/well; trolley; `::complete` twice without XP/item replay |

Required static evidence includes a clean RuneScript/config build,
duplicate-trigger and unresolved-symbol scans, no player-visible soft-skip or
session growth timer in the live path, canonical-state/migration fixtures,
every shared-topic binding, and a Quest Helper `--check` whose known dbrow
extractor bug is separately fixed or waived. Required runtime evidence is a
command-free fresh 0→60 playthrough after naturally completing Creature of
Fenkenstrain; all six provider routes with crop failures and offline waits;
every palace transform; both physical trolley routes and reset paths; both
cutscenes; loss/full/repeat/interruption cases; two players independently
moving statues and viewing scenes; exact completion; fruit and trellis use;
and regressions through shared Farming, Fenkenstrain, Wise Old Man, Roald,
Swan Song, Defender of Varrock, and Varrock Diary. A debug run, printed scene
summary, visible final loc, or successful compile is not route proof.

## 9. Definition of done

Garden of Tranquillity is modernized only when a player with completed
Creature of Fenkenstrain and unboosted Farming 25 can accept or refuse the
quest, receive/recover/use the list, complete the current diplomacy test, and
permanently activate/recover the Ring of charos through canonical states
0/10/20/30/40. They must be able to use ordinary persistent Farming to satisfy
Elstan, Lyra, and Kragen; recover every specialist seed and White Tree form;
complete Althric's well and Bernald/Alain's cure without impossible packets or
item loss; rake, compost, and plant exact quantities in every palace patch;
and wait through restart-safe native growth.

They must then physically move both private trolley NPCs through collision and
region routes, see the Falador distraction, recover from timeout/teleport/
logout, place each statue on the correct plinth, receive Ellamaria's approval,
charm Roald through a composed shared menu, and watch/resume the Garden Tour.
State 60, 5,000 Farming XP, apple seed, acorn, five guam seeds, compost
potion(4), two quest points/count, jingle, icon, and scroll must settle exactly
once. The White Tree must bear four regrowing usable fruits, the level-35
trellis must unlock only after completion, and Swan Song, Defender of Varrock,
and the Medium Varrock Diary must consume the real result. Every missing/full
inventory, loss, death, logout, repeated packet, ambiguous migrated save,
shared-topic, and two-player case must recover without debug writes,
duplicated rings/rewards, invisible crops, public trolley leakage, or stale
timers.
