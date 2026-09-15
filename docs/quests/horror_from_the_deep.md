# Horror from the Deep modernization audit

Status: `audit-pending` — the cache-native quest row, primary/support varbits,
start dialogue, bridge materials, Gunnjorn key source, lamp materials, three
optional texts, six wall offerings, both Dagannoth stat blocks, a shared
completion call, journal dispatch, and the Recipe for Disaster prerequisite
read are present. The quest is not completable through ordinary gameplay in
the current worktree: the front door never enters the quest lighthouse
instance, the repaired-light transition never joins the correct map variant,
and the available iron-ladder routes do not lead an in-progress player to the
quest cavern. Debug travel can reach authored combat, but the start also
bypasses Alfred Grimhand's Barcrawl and completion omits two of three XP
rewards. The god-book reward remains an isolated three-item grant rather than
the current prayer-book system.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A-D to acceptance, the bridge and key arms,
lighthouse instancing and stairs, optional books, lamp repair, strange-wall
offerings, both encounters, death/logout recovery, completion settlement, the
rusty casket and god-book lifecycle, journals/admin adapters, and every direct
consumer found. It is an implementation specification, not evidence that the
quest has been modernized.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable requirements, route, transcript, combat, item-lifecycle, reward,
and integration contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Horror from the Deep](https://oldschool.runescape.wiki/w/Horror_from_the_Deep?oldid=15294310) | 15294310, 2026-08-12 | Identity, requirements, route, rewards, and direct consumers |
| [Horror from the Deep/Quick guide](https://oldschool.runescape.wiki/w/Horror_from_the_Deep/Quick_guide?oldid=15080801) | 15080801, 2025-12-07 | Ordered actions, exact items, travel, and combat preparation |
| [Transcript:Horror from the Deep](https://oldschool.runescape.wiki/w/Transcript%3AHorror_from_the_Deep?oldid=15263295) | 15263295, 2026-07-14 | Offer/refusal, re-talks, interaction messages, fight transitions, and finale |
| [Transcript:Jossik](https://oldschool.runescape.wiki/w/Transcript%3AJossik?oldid=15284276) | 15284276, 2026-07-31 | Casket possession branches, confirmation, shop, and prayer-book dialogue |
| [Transcript:Larrissa](https://oldschool.runescape.wiki/w/Transcript%3ALarrissa?oldid=15284279) | 15284279, 2026-07-31 | Quest and current postquest dialogue |
| [Lighthouse](https://oldschool.runescape.wiki/w/Lighthouse?oldid=15240550) | 15240550, 2026-06-27 | Instance/world topology, floors, basement, and postquest access |
| [Larrissa](https://oldschool.runescape.wiki/w/Larrissa?oldid=15059715) | 15059715, 2025-11-23 | Start identity and current Sailing exchange |
| [Gunnjorn](https://oldschool.runescape.wiki/w/Gunnjorn?oldid=15096379) | 15096379, 2025-12-28 | Key source and agility-course location |
| [Jossik](https://oldschool.runescape.wiki/w/Jossik?oldid=15196239) | 15196239, 2026-04-25 | Store gate and salvaged-prayerbooks behavior |
| [Lighthouse key](https://oldschool.runescape.wiki/w/Lighthouse_key?oldid=15183873) | 15183873, 2026-04-22 | Issue, loss/replacement, unlock, and one-time-use policy |
| [Manual](https://oldschool.runescape.wiki/w/Manual?oldid=15282347) | 15282347, 2026-07-30 | Optional Lightomatic text identity |
| [Diary (Horror from the Deep)](https://oldschool.runescape.wiki/w/Diary_%28Horror_from_the_Deep%29?oldid=15282335) | 15282335, 2026-07-30 | Optional ancient diary identity and storage |
| [Journal (Horror from the Deep)](https://oldschool.runescape.wiki/w/Journal_%28Horror_from_the_Deep%29?oldid=15282333) | 15282333, 2026-07-30 | Optional Jossik journal identity and storage |
| [Dagannoth (Horror from the Deep)](https://oldschool.runescape.wiki/w/Dagannoth_%28Horror_from_the_Deep%29?oldid=15274475) | 15274475, 2026-07-25 | Juvenile stats, regeneration, weakness, despawn, and retry |
| [Dagannoth mother](https://oldschool.runescape.wiki/w/Dagannoth_mother?oldid=15199457) | 15199457, 2026-04-28 | Mother stats, attacks, colours, weaknesses, drops, and NMZ unlock |
| [Rusty casket](https://oldschool.runescape.wiki/w/Rusty_casket?oldid=15254124) | 15254124, 2026-07-05 | Drop, no-pickup/Telegrab rules, banked branch, and Jossik recovery |
| [God book](https://oldschool.runescape.wiki/w/God_book?oldid=15296216) | 15296216, 2026-08-13 | Six-book family, page completion, retrieval, and book actions |
| [Holy book](https://oldschool.runescape.wiki/w/Holy_book?oldid=15300898) | 15300898, 2026-08-14 | Saradomin damaged/completed lifecycle |
| [Unholy book](https://oldschool.runescape.wiki/w/Unholy_book?oldid=15300896) | 15300896, 2026-08-14 | Zamorak damaged/completed lifecycle |
| [Book of balance](https://oldschool.runescape.wiki/w/Book_of_balance?oldid=15294284) | 15294284, 2026-08-12 | Guthix damaged/completed lifecycle |
| [The Lighthouse Store](https://oldschool.runescape.wiki/w/The_Lighthouse_Store?oldid=14770840) | 14770840, 2024-10-13 | Completion gate and stock owner |
| [Alfred Grimhand's Barcrawl](https://oldschool.runescape.wiki/w/Alfred_Grimhand%27s_Barcrawl?oldid=15292247) | 15292247, 2026-08-10 | Mandatory completed-at-start prerequisite |
| [Recipe for Disaster/Defeating the Culinaromancer](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Defeating_the_Culinaromancer?oldid=15292344) | 15292344, 2026-08-10 | Direct later prerequisite |
| [Rag and Bone Man II](https://oldschool.runescape.wiki/w/Rag_and_Bone_Man_II?oldid=15292349) | 15292349, 2026-08-10 | Alternative direct later prerequisite |
| [Fremennik Diary](https://oldschool.runescape.wiki/w/Fremennik_Diary?oldid=15267932) | 15267932, 2026-07-20 | Medium-tier quest requirement |
| [Nightmare Zone](https://oldschool.runescape.wiki/w/Nightmare_Zone?oldid=15271187) | 15271187, 2026-07-21 | Postquest Mother unlock |
| [Book about lighthouses](https://oldschool.runescape.wiki/w/Book_about_lighthouses?oldid=15121672) | 15121672, 2026-02-06 | Current postquest Sailing input |
| [Armadylean paint](https://oldschool.runescape.wiki/w/Armadylean_paint?oldid=15229420) | 15229420, 2026-06-08 | Current repeatable Sailing output |

These sources define a members, intermediate, short quest released 17 November
2004. Alfred Grimhand's Barcrawl must be complete before acceptance. Agility
35 is boostable, not required to start, and is needed to use the obstacle pipe
to Gunnjorn. Completion awards two quest points and exactly 4,662.5 XP in each
of Magic, Strength, and Ranged. It unlocks the Lighthouse store, the Dagannoth
caves, one initial Saradomin/Zamorak/Guthix damaged book, all six prayer-book
families through Jossik, and the Dagannoth Mother in Nightmare Zone.

Current mechanics that must survive modernization include:

- the standard `Start the Horror from the Deep quest?` Yes/No boundary after
  Barcrawl eligibility is established;
- one plank and 30 steel nails per bridge side, a retained hammer, an Agility
  failure chance while the walkway is incomplete, and failure-free crossing
  after both sides are fixed;
- a boostable Agility-35 pipe, full-inventory key refusal, lost-key
  replacement, and one-time lighthouse unlock;
- a private lighthouse instance with Larrissa inside, explicit ladder blocks
  before her briefing and before lamp repair, and correct movement among the
  instance, exterior top floor, quest basement, and postquest caves;
- independently selectable optional manual, diary, and journal entries from
  the bookcase;
- one swamp tar and molten glass consumed, plus a retained tinderbox use;
- confirmation before each irreversible wall offering, exactly one basic
  elemental rune, one valid sword/longsword excluding rusty and prop swords,
  and one arrow excluding ogre and training arrows;
- Jossik dialogue between the juvenile and Mother, the six fixed colour
  changes with their overhead/message feedback, and safe retry after the
  juvenile's ten-minute despawn;
- the casket-in-inventory, dropped/recovered, full-inventory, and banked
  dialogue branches, followed by a two-stage god choice; and
- a modern salvaged-prayerbooks interface that can unlock every other book for
  5,000 coins, reclaim completed/incomplete/missing books, allow multiple
  completed copies, and allow only one incomplete copy per family.

Transition aid only: Quest Helper's
[`HorrorFromTheDeep.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/horrorfromthedeep/HorrorFromTheDeep.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms primary
states 0-5, both bridge bits, key bit, three lamp bits, six offering bits, the
strange-door bit, instance/exterior floor zones, quest basement/cavern zones,
required items, Barcrawl, boostable Agility, combat forms, and all three XP
rewards. The file last changed in
`241eaec29b19243bda7e88e99d5c16568c0776a6` on 2025-08-27. Running
`python3 tools/questhelper_extract.py horrorfromthedeep --check` resolves every
named item, NPC, loc, varbit, dbrow, and world point. Quest Helper cannot prove
server writes, instance allocation, ownership, transactions, reward
settlement, current dialogue, or multiplayer cleanup.

The local LostCity `quest_horror` source was inspected only to identify the
lineage and missing routing responsibilities of this port. It is not an OSRS
authority and must not override the pinned current sources or osrs239 cache.

## 2. Native quest identity and state contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_horrorfromthedeep`; quest metadata ID 65 |
| Implementation root | `server/scripts/quests/quest_horror` |
| Type / difficulty / length | Members quest / intermediate / short |
| Release | 17 November 2004 |
| Start | `horror_girlfriend_prequest` (Larrissa) |
| Primary state | `%horrorquest`, bits 0-10 of native permanent/transmitted varp `deephorror` (varp 351) |
| Authored values | 0 not started; 1 started; 2 entered lighthouse; 3 fixing lighthouse; 4 repaired lighthouse; 5 defeated juvenile; 10 complete |
| Unresolved values | 6-9 are not named, read, or written in this repository; capture must establish whether they are reserved, legacy, or reachable |
| End / quest points | State 10 / 2 QP |
| Skill policy | Agility 35, boostable, deferred until the pipe rather than checked at start |
| Quest prerequisite | `miniquest_barcrawl`, state 2, completed before acceptance |
| Recommended combat | 50 |
| Direct XP | Magic 46,625 tenths; Strength 46,625 tenths; Ranged 46,625 tenths |

The dbrow correctly records membership, difficulty, length, start, end state,
two quest points, Barcrawl as a direct prerequisite, boostable Agility 35 with
`requirement_check_skills_on_start=0`, combat recommendation 50, and all three
XP awards. These values should remain data-driven.

The native `deephorror` carrier also exposes the following support bits:

| Bit | Current symbol | Intended responsibility / audit result |
| --- | --- | --- |
| 11 | `%horrordoor` | Quest Helper treats this as the strange-wall door-unlocked condition. Current content instead sets it while unlocking the lighthouse front door and never writes it when all six offerings are present. Capture must settle this contradiction before migration. |
| 12-13 | `%horrorbridgeleft`, `%horrorbridgeright` | Per-side repair state; appropriate native carriers |
| 14 | `%horroragilitykey` | Historical key-acquired milestone; item ownership still requires inventory/bank checks |
| 15 | `%horrorlighthouseentrance` | Lighthouse entrance/unlock support; current code sets it only after also misusing `%horrordoor` |
| 16-21 | `%horrorfire`, `%horrorwater`, `%horrorearth`, `%horrorair`, `%horrorsword`, `%horrorarrow` | Six permanent wall offerings |
| 22-24 | `%horrortar`, `%horrorglass`, `%horrorlight` | Three lamp repairs |

Only one cache loc transformation uses the primary state: `horror_ladder_top2`
selects the prequest ladder for states 0-9 and the postquest ladder for state
10. No authored quest script currently gives those transformed records their
canonical destinations.

Three server-owned variables were added in the dirty worktree:

| Variable | Scope | Audit result |
| --- | --- | --- |
| `%horror_boss_active` | temporary | Useful session latch, but not an actor UID or instance handle; cleanup scans public coordinates instead |
| `%horror_magic_element` | temporary | Synchronous elemental-spell latch around the shared player-hit funnel |
| `%horror_reward_book` | permanent | Encodes only one of the original three choices; insufficient for six unlocks, page state, incomplete/completed copies, casket provenance, and settlement |

No current cache-named god-book support carrier was found in this tree. The old
`godbook_multi` naming is explicitly deferred elsewhere and must not be
resurrected by guessing. Capture current client/server vars first; if the cache
has no suitable carrier, add a versioned quest-owned prayer-book record rather
than overloading `%horror_reward_book`.

### Required state capture and migration

Capture varp 351, any hidden prayer-book variables, every item domain, the
active map/instance handle, NPC ownership, relevant loc transformations, and
dialogue after each canonical action. Exercise states 0-10, including unknown
6-9, with fresh, boosted, full-inventory, interrupted, death, logout,
reconnect, duplicate-item, banked-casket, and two-player cases.

| Existing shape | Risk | Migration rule |
| --- | --- | --- |
| State 0 with bridge/lamp/offering bits | Debug or stale support state | Do not infer acceptance; quarantine impossible support combinations with telemetry |
| State 1 without completed Barcrawl | Current start bypass | Preserve state for compatibility, but require Barcrawl for new acceptance and flag the legacy-illegal history |
| State 1 with `%horroragilitykey=1` but no key | Normal lost key or current stale bit | Gunnjorn replacement must be item-domain aware; do not clear progress |
| State 2/3 outside any lighthouse instance | Current ordinary route | Re-enter through a newly allocated canonical instance; do not advance just from coordinates |
| State 3/4 with partial lamp bits | Normal interrupted repair | Resume from native bits and never consume the same material twice |
| State 4 with `%horrordoor=1` but incomplete offerings | Current front-door misuse | Capture provenance; do not silently treat the strange wall as solved |
| State 4 with all offerings but `%horrordoor=0` | Current authored wall completion | Derive the wall-ready presentation only after native semantics are confirmed; retain all six paid offerings |
| State 5 with no actor | Normal retry after logout/leave/despawn | Jossik must replay the Mother transition safely, without rolling state back |
| State 6-9 | Unknown native/legacy meaning | Never coerce to state 5 or 10 without current-server capture |
| State 10 missing Strength/Ranged XP or QP | Current state-first completion can produce this | Repair only independently evidenced missing settlement components; state 10 alone cannot prove what was paid |
| State 10 with `%horror_reward_book=0` and banked casket | Legitimate pre-choice state | Jossik must ask for the stored casket, not manufacture a recovered one |
| State 10 with `%horror_reward_book=1..3` | Current simplified reward | Migrate the chosen original book to the modern unlock record without granting pages or other books |
| State 10 with completed books but no authored choice | Existing item history/import | Reconcile per family by proven ownership/page state; do not delete or duplicate valuable books |

Admin fixtures must distinguish “set primary dialogue state,” “create a clean
encounter fixture,” and “fully settle completion.” Unknown states and partial
settlements need explicit fixtures; a state-only `::complete` is not evidence
of a real player history.

## 3. Implementation surface

The direct root contains ten files and 1,321 lines in the current worktree.
Several are new or modified but uncommitted, so this audit describes the shared
working tree exactly as inspected and does not claim those changes as a stable
baseline.

| Path / subsystem | Present responsibility | Audit result |
| --- | --- | --- |
| `quest_horror.constant` | States, timings, route coordinates | Correctly names current timings, but the encounter/interactions hard-code the same values instead of using these constants |
| `quest_horror.varp` | Boss latch, magic element, selected book | Native primary/support state is used elsewhere; selected-book model is incomplete |
| `quest_horror.npc` | 4 juvenile and 9 Mother forms | Current stats, defence, Earth weakness, XP modifier, and null ordinary drops are broadly correct |
| `horror_girlfriend.rs2` | Larrissa start/re-talks | Transcript is broad; no Barcrawl check or standard quest-offer boundary; no instance allocation |
| `horror_interactions.rs2` | Bridge, front door, books, lamp, wall, casket pickup | Major routing, transaction, selection, allowlist, and native-bit defects |
| `horror_encounter.rs2` | Jossik, two owner-private fights, cleanup, completion | Combat skeleton exists; narrative transition, owner lookup, settlement, XP, and presentation are incomplete |
| `horror_jossik.rs2` | Initial damaged-book choice and replacement | Only three direct grants; banked-casket and modern salvaged-prayerbook behavior are wrong/missing |
| `horror_diary.rs2` | Read three lighthouse texts | Abridged `mesbox` stubs instead of the cache-authored modern book presentation |
| `horror_journal.rs2` | Quest journal | Registered, but prerequisite, partial lamp, offerings, recovery, and post-completion book state are incomplete |
| `basalt_rocks.rs2` | Lighthouse approach | Broad obstacle route exists; not quest state, but remains part of reachability smoke |
| Barbarian Outpost `gunnjorn.rs2` | Key issue/replacement | Inventory/bank/full checks exist; dialogue is abridged and depends on a missing Barcrawl system |
| Agility `maplink_agility.rs2` | Gunnjorn pipe | Correct route and level 35, but checks base level, contradicting the boostable requirement |
| Shared ladders/maplinks | Lighthouse stairs and iron ladders | Spiral floor-skip support exists; quest-specific cross-map ladder routes are absent/wrong |
| World spawns m39_56/m38_71/m39_72 | Larrissa and both Jossiks | Static actors exist on separate map variants; no private instance lifecycle binds them coherently |
| Shared hit funnel in `area_rs2012_tormented_demons` | Mother damage filtering | Functionally central but owned by an unrelated 2012 encounter file; unsuitable modular ownership |
| Player Magic | Elemental spell latch | Covers ordinary damaging spells through the shared funnel; needs a full attack-path matrix |
| Player death/logout | Encounter cleanup hooks | Hooks are registered, but cleanup is coordinate/type scanning rather than actor/instance ownership |
| Lighthouse shop | Jossik Trade | Stock exists; well Jossik's op3 has no completion guard, enabling premature trade if he is reached |
| Quest list | Journal dispatch | `quest_horrorfromthedeep` correctly routes to `~horror_journal` |
| Quest cheat | Admin completion | No Horror from the Deep arm despite the new completion call |
| POH quest status | Quest status display | No generated Horror from the Deep adapter was found |
| Quest combat manifest/checker | Text-presence assertions | Records the quest but incorrectly declares the route complete and institutionalizes Magic-only completion |
| Recipe for Disaster finale | Later prerequisite | Explicit `%horrorquest >= 10` read exists |
| Rag and Bone Man II | Later prerequisite | Cache dbrow exists, but no implementation root/journal/completion path exists |
| Fremennik Diary | Medium-tier prerequisite | Generic count UI exists; no task list or Horror-specific requirement consumer exists |
| Nightmare Zone | Mother unlock | Stub contains only Count Draynor and Elvarg; no Mother unlock/wave |
| Prayer/god-book content | Pages, completion, actions, retrieval | Cache items exist; no page-combination or complete-book behavior exists in active content |
| Sailing Larrissa exchange | Book about lighthouses -> Armadylean paint | Both cache items exist; no exchange handler was found |

The osrs239 cache contains the principal quest item, NPC, loc, animation,
map, varbit, and dbrow identities. This is not a missing-symbol problem. It is
primarily a route/lifecycle/ownership/reward implementation problem.

## 4. Route reachability and fidelity

### Acceptance and prerequisite

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Eligibility | Barcrawl complete; Agility is not checked at start | Larrissa never reads Barcrawl and starts for any player |
| Upstream reachability | Barcrawl must itself be completable | Native `barcrawl`/`barcrawl_progress` carriers and dbrow exist, but bartender arms and the miniquest are deferred; normal completion is absent |
| Offer | Standard Start? Yes/No after explanatory dialogue | Bespoke “Okay, I'll help!” choice writes state 1; modern offer boundary is absent |
| Refusal | No state mutation | Broadly correct |
| Journal before start | Show Barcrawl and deferred boostable Agility requirement | Lists Agility/combat advice but omits Barcrawl |

The prerequisite gap has two layers: Horror accepts players who do not qualify,
and the qualifying miniquest itself is not playable. Modernization must add the
local acceptance gate but cannot mark the end-to-end prerequisite test passing
until Barcrawl is independently modernized.

### Bridge and Gunnjorn

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Repair each side | Confirm one regular plank, hammer, 30 steel nails; consume plank/nails atomically | Counts are correct; items are deleted before a two-tick delay and the bit is written afterward, so interruption can destroy materials without repair |
| Repair feedback | Distinct first-half and completed-walkway messages | Uses one generic message for both sides |
| Incomplete crossing | Jump with Agility success/failure and slight damage | Crossing is refused unless the clicked side is already repaired; then it raw-telejumps with no animation, XP, roll, or damage |
| Complete crossing | Failure-free balance across the walkway | Raw axis-derived telejump broadly crosses, without canonical presentation |
| Pipe | Boostable Agility 35 | Generic shortcut uses `stat_base(agility)`, making it unboostable |
| Key issue | Exact transcript; checked add; no duplicate while owned/stored | Inventory/bank/full-space logic is broad; add return is unchecked and dialogue is heavily abridged |
| Key replacement | Reissue after genuine loss | Inventory/bank checks support this; no provenance or ground/death-storage policy is tested |

The bridge support bits are appropriately per player even though the visual
loc does not change. The repair should reserve/commit both consumed inputs and
the chosen side bit as one action. A disconnect, death, competing action, or
duplicate packet during the hammer sequence must result in either all effects
or none.

### Lighthouse entry and instance topology

This is the current hard route blocker.

Canonical topology has four distinct responsibilities:

1. unlock the exterior door once, consuming/retiring the key as current
   capture confirms;
2. enter a private lighthouse copy at m38_71 with Larrissa inside;
3. after lamp repair, preserve the deliberate exterior/instance floor routing
   while permitting the quest iron ladder to m39_72; and
4. after completion, use the ordinary exterior lighthouse and m39_156
   Dagannoth caves.

Current `oploc1,horror_lighthouse_doorway` only toggles support bits and calls
`~horror_walk_through_wall`, which moves across the exterior doorway on m39_56.
It never allocates or enters a map instance and never teleports to the native
m38_71 template. Consequently the static inside Larrissa at 2445,4599 is not
part of the player's route.

The exterior copy lets a player climb to the well Jossik and the alternate top
floor. Lamp repair only writes state 4 and prints a message; it does not perform
the cross-map transition needed by the cache topology. On the ground floor,
the shared maplink for `horror_ladder_top` at 0_39_56_14_60 goes directly to
0_39_156_22_10, the postquest cave. The transformed
`horror_ladder_top2_prequest` has only the generic unqualified-ladder category,
no quest script or correct maplink, so it falls back to a one-plane climb rather
than m39_72. No other ordinary trigger enters the quest cavern.

Additional entry defects:

- `%horrordoor` is written as a front-door bit despite Quest Helper treating it
  as the strange-door bit;
- the key is not consumed or otherwise retired and remains as an obsolete
  quest item;
- state 2 can be written by Larrissa before actual entry, so state is not proof
  of instance presence;
- the pre-brief and pre-repair iron-ladder blocks from the transcript are not
  bound to the actual ladder; and
- the two Larrissas and both Jossiks are static map actors rather than actors
  associated with a per-player instance/session.

Modernize this with the existing `map_instance_from_square`,
`map_instance_coord`, `%map_instance_handle`, and centralized release patterns.
The instance owner, entry/exit coordinate, state, and teardown destination
must be explicit. Death, logout, reconnect, front-door exit, and completion
must free exactly the owner's instance and never strand a saved coordinate in
an instance block.

### Optional books and lamp

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Search bookcase | Menu for manual, diary, journal, or all three | Forces all three with no menu |
| Single selection | Needs one free slot; gives selected item | Absent |
| All selection | Needs three slots | Present only as the forced path |
| Partial ownership | Grant only the requested/missing text under current policy | Always demands three free slots even if only one item is missing; checks inventory only |
| Read | Full cache-era/current text in a proper book presentation | Four abridged `mesbox` pages per item; no modern mounted book UI |
| Lamp | Tar/glass consumed, tinderbox retained, any valid ordering with exact messages | Broad mechanic exists; state 3 is also advanced by taking optional books, and no instance/topology transition occurs on completion |
| Ladder | Block before inside briefing and before complete repair | Missing from the reachable shared ladder route |

The lamp writes are individually recoverable through native bits, but they are
not implemented as checked inventory transactions. The completion check should
own the one state-4 transition and topology update. Optional book pickup must
not be used as proof that the mandatory repair phase began.

### Strange wall and cavern entry

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Inspect wall | Native six-icon panel reflects each offered slot | Replaced by a chat message; no modern interface/presentation |
| Offer item | Warn that it will be lost, then Yes/No | Consumes immediately with no confirmation |
| Rune policy | One air/water/earth/fire rune | Broadly correct |
| Sword policy | Sword or longsword; reject rusty and prop swords | Category predicate accepts `weapon_2h_sword`; no explicit current allowlist test |
| Arrow policy | Any arrow except ogre/training | Predicate explicitly accepts `ammo_ogre_arrow` and `ammo_training_arrow` |
| Commit | Delete one item and write one bit atomically | Writes the bit before unchecked `inv_del`; a failed/interrupted delete can retain the item while permanently solving the slot |
| Ready state | Sound/presentation and front-side gated door open | Prints messages; never writes the disputed `%horrordoor` bit |
| Crossing | Open the correct far half, side-gated; descend to cavern | `Study` directly telejumps into m39_72; generic far-door handlers have no quest/item gate; ladder staging is omitted |

Because the ordinary basement is already unreachable, the direct wall
telejump does not repair overall reachability. Once the map route is restored,
the current generic `horror_far_*_door` self-stage bindings must be replaced by
quest-owned, side-aware gates so an adjacent/open interaction cannot bypass
the six paid offerings.

## 5. Item lifecycle, recovery, and transaction audit

| Item/state | Current lifecycle | Required modernization |
| --- | --- | --- |
| Planks/nails | Deleted before delayed bit write | Reserve then atomically commit one plank, 30 nails, and one side bit |
| Hammer/tinderbox | Retained | Keep retained; accept the intended hammer collection and test substitutions |
| Lighthouse key | Reissued using inventory/bank checks, retained on unlock | Track unlocked entrance on the correct native bit, consume/retire key per capture, and preserve loss replacement only before unlock |
| Manual/diary/journal | Forced three-item grant; inventory-only duplicate logic | Restore choice menu, per-choice space checks, exact text UI, and current domain/replacement policy |
| Lamp inputs | Deleted then support bit written; no shared commit | Checked one-step mutation with idempotent repeat messages |
| Wall offerings | Support bit written before unchecked delete | Confirmation followed by atomic delete+bit commit; reject invalid classes explicitly |
| Rusty casket | Direct inventory grant when space, implicit Jossik recovery otherwise | Preserve provenance: carried, deliberately dropped/recovered, or stored; never infer dropped from inventory absence alone |
| Ground casket | `opobj3` blocks pickup | Add `telegrab_disabled` or equivalent; generic Telekinetic Grab currently bypasses the no-pickup rule because the cache item lacks that param |
| Initial damaged book | `%horror_reward_book` written before unchecked add, then casket deleted | Preflight slot, atomically consume/recover casket and commit chosen unlock plus checked item grant |
| Replacement book | Checks only chosen original family in inventory/bank/worn | Replace with family-aware modern retrieval across proven page/unlock state and relevant item domains |
| Other five books | Not available | One-time 5,000-coin unlock each through current salvaged-prayerbooks UI |
| God pages | Cache items only; no active combination scripts | Implement all six four-page progressions, checked item transforms, duplicates, and recovery |
| Completed books | Cache items only; no preach/blessing lifecycle found | Implement current actions/special-energy policy and every integration that owns those actions |
| Sailing book/paint | Both cache items, no handler | Larrissa gives unlimited Armadylean paint while the Book about lighthouses is carried, with checked inventory behavior |

Inventory, bank, worn, ground/private ground, gravestone/death storage, POH
bookcase where applicable, and any current reclaim storage must be treated as
different domains. “Not in inventory” is never sufficient proof of loss for a
unique or paid reward.

## 6. Encounter audit

### Juvenile Dagannoth

The four emergence/combat records broadly match current OSRS:

- 120 Hitpoints; 78 Attack and Strength; 81 Defence; 1 Magic; 50 Ranged;
- speed four, max melee hit nine, zero style defences;
- 35% Earth elemental weakness;
- one Hitpoint regeneration every 20 ticks;
- a ten-minute/1,000-tick lifetime; and
- null ordinary death drops.

The code also uses `npc_setowner`, makes logout/death/zone-leave retryable, and
can restore the phase from state 4. Those are useful modern pieces.

Defects remain:

- Jossik's opening account is reduced to two lines rather than the full
  transcript/camera emergence sequence;
- juvenile death immediately spawns the Mother and prints a message, skipping
  the required player/Jossik dialogue between fights;
- state 5 is written before that omitted narrative transition;
- retry/credit depends on `npc_findhero`, which this runtime documents as a
  constant-one stub rather than real per-player damage attribution; and
- no reachable ordinary route enters the arena, so static combat presence does
  not make the quest playable.

Recoil-only juvenile defeat is a documented valid route and must have an
explicit test. Direct-damage, recoil, simultaneous death, despawn, safespot,
leave/re-enter, logout, and two-player cases must all converge on one owner and
one retryable state transition.

### Dagannoth Mother

The nine authored forms broadly match current combat data:

- 120 Hitpoints; 78 Attack and Strength; 81 Defence; 1 Magic; 50 Ranged;
- speed four, melee max hit nine, and an independently rolled double ranged hit
  up to 12 each;
- +150 stab/slash/crush defence, +50 Magic/Ranged defence, +5% combat XP, and
  35% Earth weakness;
- fixed 30-tick (18-second) Air -> Water -> Melee -> Earth -> Fire -> Ranged
  cycle; and
- matching Air/Water/Earth/Fire elemental spells, melee, or ranged as the only
  direct damaging style for each colour.

The style filter is injected into the global player-to-NPC preparation funnel
hosted by `area_rs2012_tormented_demons`, while ordinary spell casts latch their
element in `player_magic.rs2`. This is a layering defect: quest combat policy
should be registered through a neutral encounter/damage-extension owner, not
hidden in an unrelated 2012 boss directory. Verify every normal swing, spell,
powered staff, special attack, multi-hit, recoil/Retribution, poison/venom,
thrall, delayed hit, and simultaneous colour-change boundary. Invalid attacks
must deal and award zero damage/XP without consuming encounter state
incorrectly.

Presentation is materially incomplete. Each colour change must send its
documented overhead cry and “The Dagannoth changes to ...” feedback. Emergence,
camera, movement, attack-style selection under protection prayers, and final
Jossik dialogue are reduced or absent.

### Ownership and cleanup

`npc_setowner` is the right intent, but `%horror_boss_active` stores only a
boolean. `~horror_clear_actor` performs thirteen public `npc_find` scans by
coordinate/type and deletes the first match. It does not call the available
owner-scoped `npc_findowned` machinery or validate a stored UID/instance. In a
shared arena, one player's leave/logout can therefore target another player's
actor depending on search visibility semantics.

Modernization should prefer a private map instance for the entire quest
lighthouse/cavern. Store the encounter actor UID or find by owner within that
instance, validate `map_instance_find(npc_coord)`, and make teardown idempotent.
The cleanup matrix must cover timeout, front/back ladder, door exit, arbitrary
teleport, region leave, death, logout, disconnect, server restart/reconnect,
and completion. Delete only the owning player's actors and free only the
owning instance.

## 7. Completion, casket, and prayer-book reward

### Completion settlement

The Mother death path currently:

1. clears timers and the boss latch;
2. creates private bones;
3. grants the casket directly if one slot is free or says Jossik recovered it;
4. writes `%horrorquest = 10`;
5. raw-telejumps to the exterior; and
6. queues Magic XP plus `~quest_complete_rewards`.

This is not atomic or idempotent. A disconnect or script failure after state 10
but before the queued reward can permanently lose XP/QP/count/scroll. Re-entry
cannot safely replay because the shared completion proc itself always adds
points and completed-count state.

The direct reward defect is exact and severe: only
`stat_advance(magic, 46625)` exists. Strength and Ranged 46,625-tenths awards
are absent, and the reward-scroll text advertises only Magic. The dbrow already
contains all three correct values, so completion and the static contract should
derive/assert all three rather than copying an incomplete string.

The canonical post-kill dialogue is also omitted. Quest completion should
teleport/present through the correct instance exit, show the player and Jossik
exchange, publish the unique “survived” completion wording if the modern scroll
supports it, and then leave the casket choice pending upstairs.

Add settlement markers or a versioned receipt for:

- primary state;
- Magic, Strength, and Ranged XP independently;
- quest points and completed-count publication;
- casket carried/recovered provenance;
- completion scroll/jingle publication where replay policy matters; and
- instance/actor teardown.

The settlement must be safe under duplicate Mother death queues, repeated
resume, disconnect between each boundary, and migration of existing state-10
accounts.

### Rusty casket choice

Current Jossik behavior collapses all missing-inventory cases into “I found the
casket.” This incorrectly bypasses a casket deliberately stored in the bank.
Canonical behavior distinguishes:

- casket carried: inspect it;
- casket dropped/unrecoverable or lost because the kill inventory was full:
  Jossik recovered it;
- full inventory: ask the player to make one space; and
- casket stored: ask the player to retrieve it.

The choice itself is one direct three-option menu. Canonical dialogue asks what
the casket says, challenges the first answer, permits a different second
answer, includes “I wish it were one of the other gods,” and commits only the
second confirmed Saradomin/Zamorak/Guthix choice.

### Salvaged prayerbooks

The postquest `Rewards` option is not a modern prayer-book system. It only
replaces the initially chosen original book for free if neither its unfinished
nor completed form is in inventory, bank, or worn slots. It cannot:

- show the cache-authored salvaged-prayerbooks interface;
- unlock the other original books or Armadyl/Bandos/Zaros for 5,000 coins;
- track all four pages for each family;
- transform an incomplete book into a completed one;
- show completed, incomplete, and missing state;
- permit multiple completed copies while limiting incomplete copies;
- distinguish a stored/ground/death-held copy; or
- support the books' current read/preach/blessing behavior.

This is part of the durable Horror from the Deep reward contract, not an
optional unrelated feature. The quest cannot be marked `verified-modern` while
its principal item reward is unusable.

## 8. Journals, admin, tests, and downstream consumers

### Journal and quest-list behavior

The dbrow dispatch is present and uses `~quest_journal`, which is the correct
modern list/journal architecture. Content defects remain:

- the not-started journal omits Alfred Grimhand's Barcrawl;
- it does not explain that Agility is boostable and deferred to the pipe;
- key progress trusts `%horroragilitykey` even when the item was lost/stored;
- states above 1 mark bridge/key work complete from primary state even if
  support state is inconsistent;
- the lamp section can omit the remaining glass/tar work once `%horrorlight`
  is true;
- state 4 does not track each of six offerings;
- state 5 cannot distinguish Mother active, absent/retryable, or narrative
  transition; and
- state 10 does not expose pending casket/book choice or book recovery.

Unknown states 6-9 need an explicit safe journal branch after capture, not an
empty or misleading page.

### Admin and inventory registries

The new shared completion call means the plan's generated inventory would now
discover this root as a completable unit, but the current master plan still
lists it among partial roots. That classification mismatch is useful evidence:
a completion call is present, but the route is still blocked. Regenerate the
inventory only after its discovery rules can report both “has completion call”
and “ordinary reachability unverified/blocked”; never equate the two.

`quest_cheat.rs2` has no `quest_horrorfromthedeep` arm, contradicting its claim
that every completion call is represented. The POH quest-status generator also
has no adapter. Add both from the captured state contract and test the admin
command twice. The first full-settlement fixture must establish correct state,
QP, and completion count; the second must be a no-op. A separate dialogue-state
fixture should not award rewards.

### Existing automated evidence

`tools/check_quest_combat_contract.py` is a text-presence checker, not a route
or transition test. Its Horror assertions currently require the hard-coded
quest coordinates, Magic-only XP, direct casket grant, and simplified
three-book behavior. The generated combat manifest then says the “complete
route” exists. These are false-positive contracts that must be replaced, not
treated as verification. On the 2026-08-17 audit worktree, the repository-wide
checker exits nonzero first on the unrelated Nature Spirit requirement that the
grotto respawn an empty pouch; consequently it cannot currently provide a green
baseline for Horror even before its Horror assertions are corrected.

No Horror-specific executable transition, instance, inventory transaction,
reward settlement, casket, prayer-book, or multiplayer test was found. The
generic owner-visibility self-test does not prove that this quest's public
cleanup scans select the correct actor.

### Downstream consumers

| Consumer | Canonical dependency/effect | Current result |
| --- | --- | --- |
| Recipe for Disaster finale | Requires Horror completion | Explicit state-10 gate exists; this is the only direct consumer currently wired |
| Rag and Bone Man II | Requires Horror or Fremennik Trials | Cache row only; no playable quest implementation/consumer |
| Fremennik Medium Diary | Requires Horror completion | Generic diary counter UI only; no task/prerequisite model |
| Nightmare Zone | Mother after completion, runes supplied | Two-boss stub; Mother absent |
| Lighthouse store | Available only after completion | Well Jossik op3 opens it unconditionally; can be reached prematurely via the broken exterior topology |
| Dagannoth caves | Postquest basement/cave access | Shared exterior ladder routes there even before completion once the lighthouse is entered, while the quest cavern is unreachable |
| Slayer | Dagannoths/task semantics after quest where current policy applies | No Horror-specific unlock publication/read was found; audit against current Slayer assignment policy |
| God books | Initial choice, six paid unlocks, pages, reclaim, actions | Only simplified initial-three selection/replacement exists |
| Sailing/Larrissa | Repeatable Book about lighthouses -> Armadylean paint | Cache assets exist; interaction absent |

Downstream completion reads should depend on the canonical settled completion
state, while reward systems should depend on their own idempotent unlock
records. Do not make state 10 alone imply that a prayer book was chosen or that
all six were unlocked.

## 9. Prioritized defects

### P0 — blocks completion or corrupts permanent rewards

1. **The quest cavern is unreachable through ordinary gameplay.** No private
   lighthouse entry, repaired-light map transition, or correct prequest iron
   ladder route exists.
2. **Barcrawl acceptance is not enforced, and Barcrawl itself is not
   playable.** The quest starts from state 0 for an ineligible player.
3. **Completion omits 4,662.5 Strength and 4,662.5 Ranged XP.** Only Magic is
   awarded/advertised.
4. **Completion is state-first and non-idempotent.** Interruption can produce
   state 10 without XP/QP/count, while replay can duplicate shared rewards.
5. **Wall payments are non-transactional and accept invalid ammunition.** Bits
   are written before delete; ogre/training arrows and 2h swords are accepted.
6. **Bridge payments can be lost on interruption.** Items are deleted before
   the delayed repair state commits.
7. **The god-book reward cannot be completed or used.** All six page systems
   and the modern retrieval/unlock UI are absent.

### P1 — major fidelity, ownership, recovery, or unlock defect

1. Front-door and strange-wall support-bit semantics conflict; the key remains
   after unlock.
2. The boostable Agility requirement is implemented as a base-level check.
3. Bookcase selection is replaced by a forced three-item grant and an incorrect
   three-slot rule.
4. Pre-brief/pre-repair ladder blocks, wall confirmation, side-aware far door,
   and instance exit/re-entry behaviors are absent.
5. Juvenile-to-Mother and Mother-to-completion dialogue/camera sequences are
   skipped; Mother colour feedback is absent.
6. Encounter cleanup scans public coordinates instead of owner UID/instance;
   `npc_findhero` is not real multiplayer credit.
7. Banked casket is treated as dropped/recovered, and dropped caskets remain
   Telegrab-able.
8. The Lighthouse Store and postquest caves can be exposed before completion
   through the current exterior topology.
9. Nightmare Zone, Fremennik Diary, Rag and Bone Man II, and Sailing consumers
   are absent/stubbed.

### P2 — presentation, maintainability, and verification debt

1. Optional texts use abridged mesboxes rather than a modern book panel.
2. Dialogue is abbreviated throughout Gunnjorn, Jossik, encounters, and book
   selection.
3. Quest-specific hit policy is coupled into an unrelated Tormented Demons
   file.
4. Named timing/coordinate constants are unused while scripts hard-code their
   values, and the checker enforces both copies.
5. Journal partial-state/recovery guidance is incomplete.
6. Unique “survived” completion wording and exact sound/camera/animation
   choreography are unverified.
7. Manifest/checker status overstates implementation and tests syntax presence
   instead of behavior.

## 10. Modernization design and implementation order

### Step 1 — capture and lock the state contract

Capture current OSRS varp/varbits and prayer-book state for every canonical
action. Resolve `%horrordoor` versus `%horrorlighthouseentrance`, primary values
3 and 6-9, key consumption, instance templates/landings, wall door state,
casket provenance, and all six book unlock/page carriers. Record packet and
screenshot evidence for the wall/book/reward interfaces.

Add an executable state adapter that exposes named predicates without inventing
new primary values. Add migration telemetry before changing existing accounts.

### Step 2 — restore acceptance, bridge, pipe, and key transactions

Implement Barcrawl-complete eligibility and the standard offer helper. Keep
Agility deferred and make the pipe use current/boosted level. Convert both
bridge repairs to checked atomic mutations, restore incomplete-crossing
failure and complete-crossing presentation, then make Gunnjorn's initial and
replacement grants checked and transcript-complete.

Barcrawl remains a separate modernization dependency; add a blocked end-to-end
fixture until that miniquest is playable, plus a direct prepared-state fixture
for Horror development.

### Step 3 — make the lighthouse a real owner instance

Use the m38_71 template with the engine's map-instance lifecycle. Bind Larrissa
and all relevant loc operations inside the owner instance. Implement exact
front door, instance exit, spiral stair, repaired-light map transition, quest
iron ladder, return ladder, postquest exterior ladder, and cave destinations.
Centralize death/logout/reconnect/leave release and safe fallback coordinates.

Do not keep raw permanent-map telejumps as substitutes. Prove two simultaneous
players see and mutate only their own instance state.

### Step 4 — restore books, lamp, wall, and route presentation

Implement the four-option bookcase and proper modern book UI for all three
texts. Make lamp materials atomic and drive topology/state from one repair
completion proc. Decompile/mount the cache-authored strange-wall panel if
present, re-arm every server op on mount, and reflect all six native bits.

Replace category guesses with a tested canonical sword/longsword and arrow
allowlist. Every offering gets a confirmation and atomic item+bit commit. Bind
both door halves and ladders with side/state gates.

### Step 5 — isolate and complete both encounters

Move the style hook behind a neutral combat-extension dispatcher or other
shared owner. Store encounter ownership explicitly and run both fights inside
the player's instance. Restore emergence, Jossik transition, colour feedback,
prayer-sensitive attacks where current behavior requires it, and post-kill
dialogue. Verify every damage source and cleanup route.

### Step 6 — make completion a settlement

Preflight/commit casket provenance, all three XP awards, state 10, QP/count,
instance exit/cleanup, and scroll publication with idempotent receipts. The
dbrow is the reward-number authority. Add migration repair for independently
proven missing Strength/Ranged rewards without replaying Magic or QP.

### Step 7 — implement the complete prayer-book subsystem

Build the initial two-stage casket choice, banked/dropped/full branches, all six
5,000-coin unlocks, all 24 page slots, incomplete-to-complete transforms,
reclaim rules, duplicate policies, and current book actions. Mount and verify
the salvaged-prayerbooks interface using named components/client scripts rather
than a hand-painted substitute.

### Step 8 — finish adapters and consumers

Correct the journal, add quest-cheat and POH status adapters, regenerate the
quest inventory/combat manifest, and update the static checker to reject the
known defects rather than require them. Wire the store, postquest caves, NMZ,
Fremennik Diary, Rag and Bone Man II, Slayer policy, and Sailing exchange in
their owning systems.

## 11. Required verification matrix

### Static and pack checks

- every quest/cache symbol resolves and no raw entity/interface IDs are added;
- completion, journal, cheat, POH status, and generated manifest sets agree;
- no legacy `if_openmain`/`if_openoverlay` remains for the wall, books, or
  salvaged-prayerbooks panel;
- no hard-coded duplicate of a named route/timing constant remains;
- every irreversible item action has a checked transaction and idempotent
  support-state assertion;
- `python3 tools/questhelper_extract.py horrorfromthedeep --check` passes;
- quest-specific transition tests, `make -C src torirsserver-scripts`, and
  `ToriRSServer_Pack --check-only` pass against the intended cache.

### Acceptance and route tests

- Barcrawl incomplete/complete and state-0 refusal/acceptance;
- Agility 34, boosted 34->35, base 35, and boost expiry at the pipe;
- both bridge orders, exact materials, missing hammer/nails/plank, interruption,
  duplicate packet, incomplete failure/damage, and completed crossing;
- key initial/full/lost/inventory/bank/ground and one-time door unlock;
- two simultaneous private lighthouse instances;
- every stair/ladder/door direction at states 0-5 and 10;
- leave, logout, reconnect, death, and completion from every instance floor.

### Puzzle and item tests

- each bookcase choice with 0/1/2/3 free slots and partial ownership;
- exact book page navigation/remount/close behavior;
- all six lamp input orders, repeated items, and interruption boundaries;
- every valid basic rune, canonical sword/longsword, and valid arrow;
- explicit rejection of rusty/prop/2h/non-sword weapons, ogre/training arrows,
  bolts, and unrelated items;
- Yes/No for every offering, full stack consumes exactly one, and reconnect
  after every partial combination;
- far-door side gates and no bypass via generic open/use-on/remote interaction.

### Encounter tests

- juvenile stats, Earth weakness, regen cadence, safespot, ten-minute despawn,
  direct and recoil-only kills;
- required Jossik transition before Mother spawn;
- all six Mother colours at exact 30-tick cadence with overhead/message;
- matching/nonmatching ordinary spell tiers, powered staves, melee/ranged,
  specials, multi-hits, recoil/Retribution, poison/venom, thralls, and colour
  changes on projectile boundaries;
- Mother melee/ranged selection, double independent ranged rolls, protection
  prayers, max hits, defence/XP modifier, bones and casket;
- timeout, ladder/door exit, teleport, death, logout, disconnect, simultaneous
  death, duplicate death queue, and two-player isolation.

### Completion and reward tests

- exactly 4,662.5 Magic, Strength, and Ranged XP; two QP; one completed-count
  increment; one scroll/jingle;
- disconnect/restart after each settlement boundary and duplicate resume;
- casket carried, kill inventory full, deliberately dropped, Telegrab attempt,
  banked, full at Jossik, and lost/recovered;
- every first/second god answer permutation and “other gods” loop;
- all six paid unlocks with exact coins, insufficient coins, exact-coins/full
  inventory, duplicate purchase, and interrupted purchase;
- all 24 pages, wrong pages, repeated pages, completion transforms, loss,
  reclaim, one incomplete versus multiple completed copies, and book actions;
- store/caves/NMZ/Slayer/diary/Rag and Bone Man II/Sailing gates before and
  after settled completion;
- `::complete quest_horrorfromthedeep` twice and separate state-only fixtures.

### Real-client smoke

Run from the real Larrissa trigger through Barcrawl gating, both bridge sides,
boosted pipe, key, private instance, all floors, bookcase, lamp, wall panel,
both fights, completion scroll, casket choice, prayer-book UI, store, and caves.
Capture two-player instance/actor visibility, every modern modal, colour-change
feedback, full-inventory recovery, death/logout/reconnect, and postquest
Larrissa exchange.

## 12. Definition of done

Horror from the Deep may be marked `verified-modern` only when:

- the real prerequisite and start trigger reach one refusal-safe acceptance;
- every mandatory action reaches state 10 without debug travel;
- the lighthouse and encounters are owner-isolated and teardown-safe;
- all consumed items, casket states, XP, QP, and book unlocks are atomic and
  idempotent;
- the complete current six-book reward ecosystem is usable and recoverable;
- every direct consumer reads the correct settled state/unlock;
- journal/admin/POH/manifest registries agree;
- automated transition, transaction, instance, combat, recovery, and consumer
  tests pass; and
- a real-client two-player smoke records the modern UI and complete route.

Until then, retain `audit-pending`. The newly authored completion call changes
inventory discovery but does not make this a completable quest: its ordinary
route remains blocked before the cavern, and its permanent rewards are
incorrect.
