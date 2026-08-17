# Fishing Contest modernization audit

Status: `audit-pending` — the native quest row, 0–5 primary state, dedicated
`garlicpipe` carrier and five quest/minnow varbits, all four competition spots,
quest items, dwarf/contest actors, journal, completion API, tunnel maplinks,
Kylie Minnow assets, and several downstream prerequisite checks exist. The
nominal contest route can reach a completion call, but it is not a modern or
safe implementation: two permanent server-only variables replace native
state, garlic teleports the shared Sinister Stranger for every player, the
White Wolf Tunnel is open before the quest, and the implementation retains an
old three-fish/repay contest loop. Start requirements, current dialogue and
item transactions, player isolation, reward settlement, Land of the Goblins,
Recipe for Disaster, and the advertised minnow unlock are also incomplete.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to Austri and Vestri, both tunnel
entrances, the pass and Hemenster gate, Grandpa Jack and the supplies, Bonzo's
contest, all competitors and spots, the garlic/Stranger scene, trophy return,
rewards and recovery, the journal, and every downstream unlock. It is an
implementation specification, not completion evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable route, dialogue, item, reward, and integration contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Fishing Contest](https://oldschool.runescape.wiki/w/Fishing_Contest?oldid=15302643) | 15302643, 2026-08-16 | Identity, requirements, route, one-carp hand-in, rewards, and minnow unlock |
| [Fishing Contest/Quick guide](https://oldschool.runescape.wiki/w/Fishing_Contest/Quick_guide?oldid=15001007) | 15001007, 2025-10-09 | Ordered interactions and alternate travel |
| [Transcript:Fishing Contest](https://oldschool.runescape.wiki/w/Transcript%3AFishing_Contest?oldid=15289741) | 15289741, 2026-08-07 | Requirement/start confirmation, all competitors, payment, quit, hand-in, recovery, and completion dialogue |
| [Fishing pass](https://oldschool.runescape.wiki/w/Fishing_pass?oldid=15239594) | 15239594, 2026-06-25 | Gate presentation, loss replacement, Drop behavior, and Land of the Goblins exception |
| [Fishing trophy](https://oldschool.runescape.wiki/w/Fishing_trophy?oldid=15185693) | 15185693, 2026-04-22 | Bonzo replacement, canonical drop-trick duplicates, hand-in, and post-quest cleanup |
| [Red vine worm](https://oldschool.runescape.wiki/w/Red_vine_worm?oldid=15290182) | 15290182, 2026-08-08 | McGrubor's Wood source, stackability, and cross-quest ownership |
| [Raw giant carp](https://oldschool.runescape.wiki/w/Raw_giant_carp?oldid=15290042) | 15290042, 2026-08-07 | Level/tool/bait, zero catch XP, one-fish route, all-copy hand-in, and drop trick |
| [Giant carp](https://oldschool.runescape.wiki/w/Giant_carp?oldid=15290043) | 15290043, 2026-08-07 | Zero-XP cooking conversion and cooked-item lifecycle |
| [Wall Pipe](https://oldschool.runescape.wiki/w/Wall_Pipe?oldid=14428083) | 14428083, 2023-06-20 | Three placements, garlic use, Search, arbitrary-item, and unusual distance behavior |
| [Transcript:Grandpa Jack](https://oldschool.runescape.wiki/w/Transcript%3AGrandpa_Jack?oldid=15221333) | 15221333, 2026-05-29 | Five-coin rod purchase, capacity/coin refusal, fishing help, and ordinary story topics |
| [Transcript:Morris](https://oldschool.runescape.wiki/w/Transcript%3AMorris?oldid=15030314) | 15030314, 2025-11-16 | Talk, pass/no-pass gate, and Hemenster whitefish exception |
| [White Wolf Tunnel](https://oldschool.runescape.wiki/w/White_Wolf_Tunnel?oldid=15211869) | 15211869, 2026-05-17 | Completion-gated passage and shared underground services |
| [Kylie Minnow](https://oldschool.runescape.wiki/w/Kylie_Minnow?oldid=15110373) | 15110373, 2026-01-21 | Permanent platform access, base Fishing 82, outfits, and 40:1 shark exchange |
| [Minnow](https://oldschool.runescape.wiki/w/Minnow?oldid=15227341) | 15227341, 2026-06-06 | Catch quantities/XP, moving spots, flying-fish loss, platform rules, and trade |
| [Recipe for Disaster/Freeing the Mountain Dwarf](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_the_Mountain_Dwarf?oldid=15292336) | 15292336, 2026-08-10 | Direct Fishing Contest prerequisite and Rohak/tunnel consumer |
| [Between a Rock...](https://oldschool.runescape.wiki/w/Between_a_Rock...?oldid=15292292) | 15292292, 2026-08-10 | Direct quest prerequisite |
| [Forgettable Tale...](https://oldschool.runescape.wiki/w/Forgettable_Tale...?oldid=15301583) | 15301583, 2026-08-14 | Direct quest prerequisite |
| [Land of the Goblins](https://oldschool.runescape.wiki/w/Land_of_the_Goblins?oldid=15292373) | 15292373, 2026-08-10 | Direct prerequisite, gate exception, and shared whitefish spot |

The sources identify a members, novice, very-short quest released 28 May 2002.
It has no prerequisite quest and requires base, non-boostable level 10 Fishing.
Required supplies are a spade, five coins, garlic, a fishing rod or pearl
fishing rod, and one red vine worm; ten coins are needed if Jack supplies the
rod. An oily fishing rod does not work. Rewards are one quest point, 2,437
Fishing XP, access to the White Wolf Mountain passage, and eligibility to earn
permanent minnow-platform access after also showing Kylie a full regular or
spirit angler outfit at base Fishing 82.

The current source set has two non-authoritative route disagreements which
must not become server mechanics. The main article suggests collecting at
least two worms, its required-items block and the raw-carp page require one,
and the older quick guide says three. Exactly one worm is consumed for the one
required carp. The quick guide also says to wait for the contest to end, while
the current article, transcript, and raw-carp page direct the player to talk to
Bonzo with one carp. Implement and test the latter server contract; additional
worms/carp remain optional and support documented drop tricks.

Transition aid only: Quest Helper's
[`FishingContest.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/fishingcontest/FishingContest.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms primary states
0–4, actors, coordinates, items, base level 10, one worm/carp, both rewards,
and native `fishingcompo_stranger` as the garlic/moved observation. `python3
tools/questhelper_extract.py fishingcontest --check` resolves the dbrow, all
items, locs, and that varbit, but reports
`NpcID._0_41_53_sinisterfishspot` unresolved. The asset does exist locally as
`0_41_53_sinisterfishspot` (NPC 4080); this is a leading-underscore/name-
normalization gap in the extractor, not evidence that the spot is absent.

## 2. Native quest identity and contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_fishingcontest`; dbrow pack index 52, quest metadata ID 27 |
| Type / difficulty / length | Members quest / novice / very short |
| Release / location | 28 May 2002 / Kandarin |
| Start | Either Austri (`tunnel_dwarf`) or Vestri (`tunnel_dwarf1`); native coordinate 2876,3483,0 |
| Requirement | Base Fishing 10, non-boostable; no prerequisite quest |
| Primary state | `%fishingcompo`, permanent transmitted varp 11 |
| Canonical values | 0 not started; 1 accepted; 2 assigned willow spot; 3 assigned pipe spot; 4 won/trophy phase; 5 complete |
| Secondary carrier | Native permanent varp 12 `garlicpipe` |
| Quest varbits | `fishingcompo_garlicpipe` bit 0; `fishingcompo_paid` bit 2; `fishingcompo_passed` bit 3; `fishingcompo_stranger` bit 4 |
| Related native field | `minnow_access`, bits 5–6 of `garlicpipe`; values 0/1 use locked Kylie, 2 uses trade-enabled Kylie, 3 hides her |
| Native actor transform | Parent `sinister_stranger` reads `fishingcompo_stranger`, selects two visible leaves then hidden; world parent spawns at 2637,3440,0 |
| End state / quest points | 5 / 1 |
| Reward | 2,437 Fishing XP (native raw tenths 24,370); White Wolf Tunnel; minnow-access prerequisite |

The primary values already agree with cache, Wiki, and Quest Helper and must
not be renumbered. The secondary design does not. This port declares permanent,
untransmitted `%hemenster_comp_stage` and `%hemenster_pipe_stashed`, copied
from its old LostCity/RSC-shaped source, while revision 239 already dedicates
native garlic, fee-paid, gate-passed, and Stranger facts to this quest. That is
exactly the parallel authored progress state prohibited by the modernization
plan. Recover the live meaning and reset rules of each native bit, migrate the
legacy fields, then remove their authority. Bit 1 is unnamed and must not be
appropriated. Bits 5–6 belong to the separate persistent minnow service and
must be preserved during any carrier migration.

The Stranger wrapper establishes per-player phase presentation but does not,
by itself, relocate the single world spawn. Moving Vlad from the pipes must be
a player-owned actor/scene operation or an equivalent private-NPC transform
whose position, visibility, dialogue, and cleanup are isolated. Neither a
global teleport nor simply setting a visual leaf on the original tile fulfils
the contract.

## 3. Implementation surface

The direct root contains 399 lines across seven files. Six mandatory shared
area owners add 440 lines before tunnel, completion, journal, consumer, and
minnow subsystems are counted.

| Quest-owned path | Present responsibility | Audit result |
| --- | --- | --- |
| `configs/quest_fishingcompo.constant` | Primary and invented competition-stage values | Primary values are correct; the four-stage, three-catch competition model is old machinery |
| `configs/quest_fishingcompo.varp` | Re-declares native primary | Correct permanent/transmitted carrier |
| `configs/quest_fishingcompo_comp.varp` | Invented fee/fish-count and garlic state | Duplicates native cache facts and is the source of incompatible persistence |
| `scripts/fishingcompo_journal.rs2` | Dynamic journal for states 0–5 | Correct dispatcher/API shape; content and completed styling are stale |
| `scripts/hemenster_fishing.rs2` | Four competition spots, equipment, catch loop, LOTG merge | Route-shaped but retains RSC three-catch timing, exact normal-rod checks, global-actor assumptions, and incomplete hand-over cleanup |
| `scripts/quest_fishingcompo_gate.rs2` | Garlic pipe, Stranger movement, gate, quit path | Contains the critical public NPC teleport, generic pipe behavior, no native bits, teleport-only gate, and missing LOTG exception |
| `scripts/quest_fishingcompo.rs2` | Delayed completion settlement | Unguarded state/XP/QP/count/scroll delivery; stale comments still say core content is deferred |

Mandatory shared/cache surfaces:

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `areas/area_white_wolf_mountain/scripts/mountain_dwarf.rs2` | Both start/recovery/completion actors | Missing base-level and speedrun checks, current start confirmation, capacity-safe pass grants, exact re-talks, full trophy conversation, and post-quest trophy use |
| `areas/area_seers/scripts/hemenster/{bonzo,grandpa_jack,sinister_stranger,fishermen}.rs2` | Contest transaction and shared characters | Bonzo omits native Pay; Jack omits sale/help and most hints; Dave/Joshua/Morris are placeholders; Stranger text and actor lifecycle are stale |
| `areas/area_seers/scripts/mcgrubors_wood.rs2` | Eight vine locs and worm acquisition | Functional and stack-aware, but misspells “amongst”; shared guard-dog/railing behavior needs route verification |
| `configs/all.dbrow`, `all.varp`, `all.varbit` | Metadata and state schema | Correct row, rewards, primary and `garlicpipe` fields exist; current quest ignores every native secondary bit |
| `configs/all.npc`, world spawns, fishing configs | Competitors and four named spots | All actors/spots are present at the expected Hemenster coordinates; the shared Stranger parent makes global teleport unsafe |
| shared ladder/maplink system | Four surface entries and four underground exits | Destinations are correct, but generic `_climb_down` calls `~maplink_try` without a quest predicate, making the reward tunnel public at state 0 |
| shared cooking table | Raw-to-cooked giant carp lifecycle | Both objects exist, but no `cooking_generic` row or dedicated handler exists, so the documented zero-XP conversion is unavailable |
| shared quest completion API | XP, QP/count, jingle, scroll | Modern presentation exists, but callers must make it exactly-once; current queue does not |
| quest journal dispatcher / quest cheat | UI and admin adapters | Row 27 dispatches correctly; `::complete` writes state 5 and awards QP/count once but intentionally does not grant route rewards |
| `quest_betweenarock` / `quest_forgettabletale` / `quest_landofthegoblins` | Direct prerequisite consumers | Each explicitly checks `%fishingcompo >= 5`; preserve and test these predicates |
| `quest_recipefordisaster/scripts/recipefordisaster_dwarf.rs2` | Mountain Dwarf subquest and Rohak | Native dbrow lists Fishing Contest, but local start checks only the RFD introduction; the subquest is startable without this quest |
| Kylie/minnow cache, spawns, and fishing configs | Advertised post-quest unlock | Locked/unlocked Kylie, access varbit, rowboats, four spots, and minnows exist; no Talk/Trade/boat/catch/movement/flying-fish implementation exists |

No legacy IF1 quest panel or raw numeric entity reference remains in the
direct root. The dominant defects are older state ownership, public-world
mutation, generic transport bypass, abbreviated transactions/dialogue, and
missing reward consumers—not an obsolete quest-specific interface.

## 4. State and transition audit

| State | Canonical phase | Current behavior / defect |
| ---: | --- | --- |
| 0 | Start at either dwarf; require base Fishing 10; Speedrunning unavailable; explicit Yes/No confirmation | Either dwarf can start below level 10. The old “Fortunately I'm alright...” choice replaces current confirmation, “Do you have a brother?” is absent, and a full inventory writes state 1 before an unchecked pass add. Garlic can also be consumed at the pipe before the quest. |
| 1 | Hold/recover pass, collect supplies, enter through Morris, pay Bonzo once | Pass recovery checks inventory and bank but has unchecked adds. Morris Talk is a stub. Bonzo lacks op3 Pay. Fee state is stored in `%hemenster_comp_stage`, and quitting resets it to unpaid despite the old source explicitly noting OSRS retains paid state. |
| 2 | Paid and assigned willow spot; move Vlad with garlic | Current primary reaches 2, but garlic is in a parallel varp and moves the public NPC. Native garlic/paid/passed/Stranger facts remain zero. If another player already moved Vlad, a garlic-before-payment player can stay at state 1 because `npc_find` fails. |
| 3 | Garlic moved Vlad; fish at the pipe spot; bring one giant carp to Bonzo | Correct spot and one-carp catch are possible, but only the ordinary rod works, current stat rather than base stat is checked, every catch increments an old counter, and three catches auto-end the contest. Current Bonzo skips the explicit carp option/wait. |
| 4 | Winner holds or can replace trophy; return it to either dwarf | Bonzo replaces an absent trophy with no capacity check and inventory-only ownership, which supports the documented drop trick accidentally. The dwarf wrongly offers a replacement pass when the missing object is the trophy and truncates the current completion conversation. |
| 5 | Complete; tunnel open; downstream prerequisites true; Kylie eligibility available | Completion is queued without an entry guard. The tunnel was already open at state 0, Kylie has no implementation, RFD Mountain Dwarf ignores the prerequisite, and LOTG gate access can fail after a lost pass. |

Primary state alone is not enough to reconstruct an active attempt. Modern
transitions should pair it with the named native fields, with explicit
invariants such as “states 2/3 imply paid,” “state 3 implies garlic and moved
Stranger,” and “state 4 implies contest settled.” The exact live reset policy
for paid/passed after a regular-fish loss, quit, death, or logout should be
captured before code is changed; the old port's own comment already proves its
repayment behavior was a deliberate RSC substitution, not verified OSRS.

## 5. Detailed lifecycle audit

### Start, tunnel denial, and fishing pass

The current transcript places the non-boostable requirement check and an
explicit “Start the Fishing Contest quest? Yes/No” after “Well, let's be
friends!” It also has a Speedrunning-world refusal. The local script instead
continues into the old two-choice fishing boast and performs no requirement
check. Gate on `stat_base(fishing) >= 10`, preserve every non-starting dialogue
loop, and write state only after the player confirms and a pass slot is
reserved. A declined or ineligible start must not mutate either carrier.

Initial and replacement pass delivery must be one acknowledged transaction.
The pass is shown, not consumed, whenever Morris admits the player; a truly
lost pass is replaced by either dwarf before completion and never after it.
The current inventory-plus-bank absence check is a reasonable duplicate guard,
but it does not account for a dropped copy and both `inv_add` calls are
unchecked. Capture live ground/bank behavior, then test full inventory,
existing pass in inventory/bank/on ground, repeated Talk, packet replay, and
both dwarves. At state 4, an absent trophy routes to Bonzo, not to spare-pass
dialogue.

Clicking either surface stair before completion should call the corresponding
dwarf denial/progress dialogue. The local quest omitted those name handlers;
the generic climb category finds a maplink and immediately teleports to the
tunnel. Add quest-aware name ownership above the category: surface descent
requires state 5, while underground stairs must always permit escape even for
an inconsistent/admin/migrated save. Keep all four verified maplink endpoints
and test both tiles at each entrance, both directions, missing dwarf spawn,
collision, and two players with different states.

### Supplies, McGrubor's Wood, and Grandpa Jack

The Seers' Village garlic spawn at 2714,3478 and the McGrubor's Wood route are
present. The vine handler correctly retains the spade, allows repeated
stackable worms, permits a full inventory when a worm stack exists, and refuses
when it cannot create a new stack. Correct “amoungst,” verify all eight vine
members, the loose railing, locked gate/Forester, aggressive guard dogs,
repeated digging, and the fact that worms are shared with Wanted! and A Fairy
Tale I. Completion must not delete spare worms.

Grandpa Jack's quest hints stop after the carp and worm clue. The current
transcript continues through the guard-dog warning, the player's description
of Vlad, and Jack's garlic/Seers-or-Ardougne advice. More importantly, his
normal menu always includes buying a regular rod for five coins and a fishing
tutorial. Neither exists locally. Implement the full shared topic menu and
capacity-safe purchase: refuse if the player already has an ordinary rod,
handle insufficient coins, offer buy/decline, reserve a slot, then exchange
exactly five coins for one rod. Preserve his non-quest diamond story and the
quest hint only while relevant.

Fishing at Hemenster must accept `fishing_rod` and `fishingrod_pearl` and must
reject `oily_fishing_rod`. The local exact ordinary-rod check rejects the
documented pearl variant. Use a named equipment predicate or table rather than
duplicating tool rules in several branches. Base level 10 was established at
start; a temporary stat drain should not reinterpret a non-boostable base
requirement as failure. Add the missing raw-giant-carp to giant-carp cooking
recipe: boostable Cooking 10, zero XP, failure to the trout-style burnt fish,
ordinary and special-range success chances, and no burn at the documented
level thresholds.

### Morris, Hemenster gate, and Land of the Goblins

Morris Talk currently says he is not interested. Restore his standard purpose
dialogue and the pass-present continuation. Gate clicks should show the pass,
keep it, open both leaves through an interruptible gate transaction, cross the
player, and restore collision. The current handler only teleports and, if
Morris is absent, silently lets a pass holder bypass the actor transaction.
Use native `fishingcompo_passed` according to captured live semantics rather
than leaving it permanently zero.

The gate is also a Land of the Goblins integration point. The pass page and
Morris transcript allow a completed champion who has spoken to Aggie about
white dye to request a Hemenster whitefish and enter without a pass. Local LOTG
sets `%lotg_know_about_fish` and correctly merges slimy-eel use into the same
Sinister fishing spot, but the gate never checks that fact. A completed player
who lost the now-irreplaceable pass is therefore blocked. Compose the LOTG
choice into the sole Morris/gate owner, preserve Fishing Contest admission,
and verify both quests in every overlap. LOTG's whitefish branch currently
checks only an ordinary rod; align it with the live tool contract separately
without allowing slimy eel to trigger a competition catch.

Leaving during an active contest must finish Bonzo's current two-choice
dialogue and safely cross only on “another day.” The local affirmative branch
omits “Ok, I'll see you again,” clears the fee, and never resets or privately
restores Vlad. Recover live paid/garlic/assignment persistence from the native
bits and a runtime trace. Cancel must leave the player and attempt unchanged;
logout/death/teleport must not strand a private actor or leak an open gate.

### Garlic pipe and player-isolated Stranger

All three wall pipes share the `garlicpipe` name. The main guide says to use
the third pipe at 2638,3446, while the scenery page says garlic works in one of
the three. Verify live coordinate acceptance before narrowing the handler; do
not infer it solely from Quest Helper highlighting the eastern pipe.

Search and arbitrary-item behavior is not generic. Adjacent Search produces
the player's sewage line; two squares gives “I can't reach that”; three to
eight gives the ordinary no-find response; farther interactions approach to
eight first. Adjacent arbitrary items produce the “no point stuffing this up
a dirty pipe” line, while farther uses are generic. Current Search and every
non-garlic use call the same default helper. Reproduce the cache-era range
contract with explicit approach and cancellation tests.

Garlic use needs phase, ownership, and repeat guards. Current code consumes
garlic at any quest state, waits two ticks without revalidation, writes only
the invented boolean, and consumes further garlic even after the pipe is
already stashed. A successful active-quest transaction should consume exactly
one garlic, set native garlic state, and, when paid, run Vlad's move once and
set the Stranger/assignment facts at the defined commit boundary. Before
payment it should persist the smell and defer the scene until Bonzo assigns
spots. Wrong state, repeated use, missing item after interruption, or a stale
packet must not consume another item.

`~move_hemenster_pipe` is the central isolation failure. It finds the public
parent within eight tiles and calls `npc_tele(2631,3435)`. The move is shared
by all players, has no reset, and takes Vlad outside the later eight-tile find
radius. The first player changes the world for everyone; a second player who
stashed garlic before paying can stay at primary state 1 because Bonzo returns
after the failed find without assigning any spot. Winning, quitting, logout,
death, and completion never restore the NPC.

Replace this with a player-owned competition scene/private actor. An incomplete
observer must see Vlad at the pipes; the garlic player must see him move and
must be blocked from his new spot; another active player must retain their own
phase. Dialogue and `npc_find` must resolve the player's private actor, and
cleanup must handle quit, loss, win, logout, death, region change, and server
restart. Reset the public world spawn during deployment because a running
legacy world may already have teleported it.

### Bonzo, competitors, catch, and contest settlement

Bonzo's native NPC has Talk-to and Pay. Only Talk is bound locally. Add op3 so
it pays/starts through the same idempotent transaction, reports “already paid”
during a competition, and reports no need after winning. Talk must retain the
entry menu and no-money branch. Deduct exactly five coins once, write native
paid state, assign spots, and never charge again because of a quit/reconnect
unless live evidence explicitly establishes a reset.

Big Dave and Joshua are not hostile one-line blockers. Each has a three-option
current menu about their status, swapping spots, and tips that points to
Grandpa Jack. Morris also has full dialogue, and Vlad's current accent/text
differs substantially from the local older spelling. Restore these shared NPC
topics and have clicking Dave's or Joshua's occupied spot invoke the same
dialogue only during an active contest. Outside the contest, the spot should
follow verified no-op behavior rather than opening unrelated quest text.

The actual catch is one zero-XP giant carp from the pipe-side
`0_41_53_sinisterfishspot`, using one red worm and either accepted rod. Ordinary
bait/other assigned spots can produce losing fish, but the modern server does
not need the invented three-catch counter. Current code increments stage on
every fish and auto-calls Bonzo at stage 4, inherited from its RSC source. It
also chooses red worm ahead of ordinary bait, only deletes up to three
sardines/carp, never removes caught shrimp on loss, and can delete unrelated
pre-existing sardines. Model the attempt's eligible catch explicitly enough to
hand over the live-defined set without an arbitrary cap or collateral items.

After a giant carp, Bonzo must offer “I have this big fish. Is it enough to
win?” and “I think I might still be able to find a bigger fish.” The former
runs the wait/handover presentation, takes every carried raw giant carp as the
item page states, awards one trophy, and commits state 4 once. The latter does
nothing. A regular-fish hand-in declares Vlad the winner and leaves a retryable
quest with the live fee/garlic policy. Test dropped/banked/cooked carp, multiple
carp, documented ground-item retention, full inventory, repeat packets, and
interruption before and after the state/trophy commit. Catching must be disabled
only after the authoritative win.

### Trophy return, reward settlement, and recovery

Bonzo replaces a truly lost trophy at state 4. The item page explicitly allows
multiple trophies through the drop trick, so an inventory-only absence check
may be intentional for ground copies; bank behavior still needs a live trace.
The local unchecked add can falsely claim delivery with a full inventory. Make
replacement capacity-safe while preserving verified duplicates. After quest
completion, using any extra trophy on either dwarf must remove it; local
dwarves have no use-item handler and leave all extras behind.

The completion conversation is heavily truncated. Restore the trophy's shine,
trust, tunnel explanation, optional beer, player response, and then completion.
Trophy consumption creates a free slot, but it does not make the remaining
reward sequence idempotent. Current code deletes one trophy, queues a delay-zero
completion before the last local line, then unconditionally writes state 5,
adds 24,370 raw Fishing XP, adds QP/completed count, and mounts a scroll. A
direct/repeated queue duplicates XP/QP/count; cancellation can separate item,
state, dialogue, and reward. The scroll also uses `coins` as its rotating icon
despite no coin reward and prints “2437” rather than “2,437.”

Create one guarded, resumable settlement. Validate state 4 and trophy, consume
exactly one, finish the conversation, award 2,437 Fishing XP and one QP/count,
commit state 5 once, then present the correct trophy-oriented completion scroll
and novice jingle. A disconnect at every boundary must resume without demanding
a consumed trophy or replaying a reward. Extra carried trophies follow the
canonical use-on-dwarf cleanup rather than blanket deletion.

### Tunnel, minnows, and prerequisite consumers

The White Wolf Tunnel reward is a present but unauthorized transport. Both
surface loc names are category `climb_down`; both underground names are
`climb_up`; eight maplink rows provide the correct west/east destinations.
Because the shared category handler calls `~maplink_try` first, any account can
descend at state 0. Gate only surface entry and always preserve exit. This also
prevents pre-quest access to Rohak and other tunnel content through the reward
entrance.

Between a Rock..., Forgettable Tale..., and Land of the Goblins correctly
reject `%fishingcompo < 5`. Recipe for Disaster - Mountain Dwarf does not: its
native subquest row names Fishing Contest, but the local frozen-dwarf handler
checks only `%recipefordisaster >= intro` and advances `%rfd_dwarf` from zero.
Add the missing prerequisite to the shared RFD requirement/start transaction
and test native/admin/migrated completion. Do not rely on the currently public
tunnel as an accidental prerequisite.

The minnow reward is absent despite unusually complete cache/world assets.
`minnow_access` shares the native `garlicpipe` carrier, Kylie has locked and
trade-enabled leaves, both Kylies and four spots are spawned, and both rowboats
exist. No server script binds Kylie, either boat, or the spots. Implement the
whole advertised consumer: base Fishing 82, completed Fishing Contest, full
regular or spirit angler outfit shown once for permanent access, locked/unlocked
dialogue, safe travel both ways, small-net catches of 10–14 minnows and 26.1 XP,
25-tick clockwise spot moves, flying-fish loss, platform exclusion from the
guild boost, and 40 minnows for one noted raw shark. The 2026 permanent-Deadman
100,000-coin entry rule is world-mode-specific and must be composed rather than
charged globally. This work can live in the Fishing/minnow subsystem, but
Fishing Contest cannot be `verified-modern` while its stated reward is inert.

### Journal and administrative adapters

The journal is correctly dispatched by dbrow and covers all six primary
values. It does not use native garlic/paid/passed facts, cannot describe a
quit/lost catch/trophy recovery accurately, calls the winning fish plural
“Giant Carp,” and renders the entire completed narrative with `journal_todo`
instead of completed styling. Rebuild it from primary plus native secondary
facts, with one carp, pass/trophy recovery, and standard completion colors.
Do not expose an invented fish counter after that variable is retired.

`::complete quest_fishingcontest` correctly uses the shared administrative
adapter: it writes state 5, awards the row's QP/count, and is a no-op when
repeated; it intentionally does not grant XP or play the route. Post-login
tunnel and downstream predicates must derive safely from state 5, while minnow
access remains a separate earned fact. Never call the unguarded gameplay reward
queue from the cheat, and never treat the cheat as route evidence.

## 6. Migration and recovery

The native primary meanings are stable, but deployment must translate legacy
secondary saves and repair public world state deliberately:

1. Preserve `%fishingcompo` values 0–5. Reject out-of-range values with
   telemetry; do not renumber imported or live saves.
2. Preserve all existing bits 5–6 of `garlicpipe` exactly because they are
   permanent minnow access, not scratch space for the quest migration. Preserve
   unknown bit 1.
3. Translate `%hemenster_pipe_stashed=true` to native garlic state. For primary
   3, also establish the verified moved-Stranger/assignment facts. Never clear
   a native fact already set by an imported save.
4. Translate `%hemenster_comp_stage >= 1` and primary states 2/3 to paid state.
   Treat old fish counts 2–4 only as migration evidence: preserve carried/raw
   catches and resume at Bonzo or a valid spot; do not require three fish or
   auto-settle merely because the old counter was 4.
5. Capture and encode live quit/loss rules before mapping active state-1/2/3
   combinations. When evidence is ambiguous, preserve the paid fee and give a
   retryable assignment instead of charging or deleting items silently.
6. Reset the public `sinister_stranger` world spawn to its map coordinate on
   deployment and remove orphaned legacy movement. Rebuild each active player's
   private actor from native state on login/region entry.
7. Reconcile pass, raw carp, and trophy copies across inventory, bank, and
   ground ownership without deleting documented drop-trick collectibles.
   Extra post-quest trophies are cleaned only through the canonical dwarf use.
8. State-5 players may have lost or duplicated XP/QP/count through the queued,
   unguarded settlement. Aggregate totals cannot attribute this quest safely;
   use a migration ledger, telemetry, or support reconciliation rather than
   blindly replaying every reward.
9. Derive tunnel admission and all prerequisite predicates from state 5 after
   login. Preserve already-started Between a Rock, Forgettable Tale, LOTG, or
   RFD progress rather than rolling it back because their historical gate was
   missing or inconsistent.
10. Preserve legitimate `minnow_access=2` saves even though this tree currently
    lacks the service. State-5 players at 0/1 have completed only the quest
    prerequisite and must still show Kylie the outfit at base level 82.

## 7. Modernization sequence

### Gate A — native state, start, and transports

1. Lock primary 0–5 and every `garlicpipe` bit in transition/migration tests;
   retire legacy secondary authority only after a measured mapping exists.
2. Restore both dwarves' current requirement, speedrun, confirmation, refusal,
   pass-delivery/recovery, progress, brother, and stair-denial branches.
3. Add completion-gated surface tunnel handlers above generic climb maplinks,
   with unconditional safe underground exits.
4. Restore Morris Talk, native gate/pass state, physical gate traversal, and
   the LOTG whitefish exception.

### Gate B — supplies and isolated contest setup

1. Complete Jack's hints, rod sale, fishing help, capacity checks, and shared
   topics; accept ordinary and pearl rods and add giant-carp cooking.
2. Implement all wall-pipe range/use behavior and a phase-safe, idempotent
   garlic transaction using native facts.
3. Replace public Vlad teleport with a private, resumable competition actor
   and deterministic cleanup/rebuild boundaries.
4. Bind Bonzo Pay and Talk to one fee/assignment transaction and restore Dave,
   Joshua, Morris, and Stranger dialogue without duplicate triggers.

### Gate C — catch, hand-in, and exactly-once completion

1. Replace the old three-catch stage with the current one-carp path while
   preserving verified losing-fish and voluntary-delay branches.
2. Make Bonzo hand-in/replacement all-copy-aware, capacity-safe, drop-trick-
   compatible, and exactly once under repeated packets/interruption.
3. Restore the full dwarf return conversation and one guarded trophy/XP/state/
   QP/count/scroll/jingle settlement transaction.
4. Rebuild the journal and migration/login reconciliation from native facts.

### Gate D — reward and downstream integration

1. Implement permanent Kylie access, rowboat travel, minnow spots/catches,
   flying fish, platform rules, and shark exchange on the native access field.
2. Add Fishing Contest to the RFD Mountain Dwarf start predicate; reverify the
   three existing completion consumers and LOTG shared spot/gate.
3. Run migration for legacy secondary fields, public Vlad position, ambiguous
   rewards, and valid item duplicates without resetting primary progress.
4. Run fresh, migrated, loss/recovery, isolation, transport, minnow, and all
   downstream scenarios through real interactions.

## 8. Verification matrix

| Area | Required checks |
| --- | --- |
| Start | Both dwarves; Fishing base 9/10 with boosted/drained current values; normal/speedrun worlds; Yes/No; all side loops; full inventory; exact one state/pass commit |
| Pass | Carried/banked/dropped/lost/duplicate; full inventory; both dwarves; states 1–4; no replacement after 5; repeated packet |
| Tunnel | Both surface names and two placed tiles each at states 0–5; both underground exits; direct/maplink/category actions; missing dwarf; collision; two players |
| Supplies | Garlic spawn; all eight vines; spade missing/retained; new/existing worm stack at full inventory; guard dogs, gate, Forester, railing; cross-quest worms |
| Grandpa Jack | Quest hint before/during/after; story/help; normal rod already held; 0/4/5 coins; buy/decline; full inventory; pearl/oily rod interactions |
| Morris/gate | Talk and click; pass/no pass; state/native passed combinations; both leaves/sides; gate animation/collision; missing actor; repeated click; two players |
| LOTG gate/spot | FC states 4/5 × LOTG knowledge 0/1 × pass present/absent; whitefish choice; slimy eel/red worm/ordinary bait; no branch stealing |
| Pipe | All three coordinates; Search/use at distances 0–9; garlic/wrong item; wrong state; pre/post payment; repeated/stale use; cancel during delay; exact one consume |
| Stranger | Garlic before/after payment; original/new spot dialogue; quit/loss/win/logout/death/region/restart cleanup; two competitors and one observer with independent actors |
| Bonzo payment | Talk and Pay; 0/4/5 coins; before/during/after contest; repeated op; quit/re-enter; exact one fee and native state commit |
| Competitors | Full Dave/Joshua/Vlad menus; occupied-spot clicks; before/during/after quest; current gender text; no unrelated spot dialogue |
| Catch | Base/current Fishing combinations; ordinary/pearl/oily/no rod; worm/bait; correct/wrong spot; full inventory with one/multiple bait; zero XP; no three-catch auto-end |
| Bonzo hand-in | One/multiple/dropped/banked/cooked carp; regular fish and no carried catch; delay choice; all carried carp removed only on win; one trophy/state commit |
| Trophy | First/replacement copy; inventory/bank/ground/full; canonical drop trick; both dwarves; extra trophy use after completion; no accidental blanket deletion |
| Completion | State 4/5 entry guard; missing/duplicate trophy; interrupt every dialogue/queue boundary; exact 2,437 XP/1 QP/count once; trophy icon, jingle, scroll; repeated Talk/login |
| Migration | Every primary state × legacy stage 0–4 × garlic boolean; every native bit; active catches/items; public Vlad displaced; minnow bits preserved; ambiguous state-5 rewards |
| Journal/admin | Every primary/native combination; loss/recovery text; standard completed style; `::complete` twice; login-derived tunnel/prerequisites without XP replay |
| Minnow access | Quest 4/5 × base Fishing 81/82 × boosted current × regular/spirit/mixed outfit; permanent unlock; both boats; world-mode fee; relog/death |
| Minnow gameplay | Four 25-tick rotations; quantities by level; 26.1 XP; small net/no net; flying-fish 1/10 and 16–26 loss; no guild boost; two players; 40:1 noted shark trade |
| Downstream | Between a Rock, Forgettable Tale, LOTG, and RFD Mountain Dwarf before/after state 5; already-started migrations; shared Rohak/tunnel and whitefish behavior |

Required static evidence includes a clean RuneScript/config build, duplicate-
trigger and unresolved-symbol scans, native/legacy transition fixtures, maplink
predicate tests, no public NPC movement, no unexpected numeric IDs, and the
Quest Helper check with its known leading-underscore normalization exception.
Required runtime evidence is a command-free fresh 0→5 playthrough from both
dwarves; alternate garlic/payment orders; loss, quit, item recovery and drop
tricks; two simultaneous contest players plus an observer; interruption at
every actor and settlement boundary; pre/post-completion tunnel attempts; full
minnow unlock/gameplay; and all four downstream starts. A debug state, visible
trophy, completion scroll, generic maplink teleport, or successful compile is
not route proof.

## 9. Definition of done

Fishing Contest is modernized only when a base-level-10 player can confirm the
quest at either dwarf, receive/recover one pass, obtain or buy all supplies,
enter through a correct Morris/gate transaction, pay Bonzo once, privately move
Vlad with one garlic, catch and hand over one giant carp with either valid rod,
recover a lost trophy, and complete the full dwarf conversation. Trophy,
2,437 Fishing XP, one quest point/count, state 5, jingle, scroll, and all native
secondary facts must settle exactly once under repeated actions and every
interruption, without public NPC movement, stale attempt state, or item loss.
The tunnel must be denied before completion and safe in both directions after;
Kylie/minnows and all four prerequisite consumers must enforce the completed
quest correctly; LOTG must retain its passless gate and shared whitefish path.
Every legacy save, full-inventory case, item loss/duplicate, logout, death,
concurrent player, journal, and admin adapter must recover without debug writes,
repayment inherited from RSC, duplicated rewards, or regressions to shared
Fishing, Cooking, Hemenster, White Wolf Tunnel, and downstream content.
