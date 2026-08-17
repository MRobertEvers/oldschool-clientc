# Druidic Ritual modernization audit

Status: `audit-pending` — the local quest has the correct native 0–4 ladder,
unique symbolic triggers, all four meat conversions, Sanfew's all-items check,
a dynamic journal, the shared completion scroll, and an accessible Taverley
Dungeon route. It is playable in outline but is not verified modern. Its start
and Herblore lesson preserve obsolete LostCity dialogue, completion occurs
before the lesson instead of after it, the completion queue has no
duplicate-delivery guard,
item hand-offs are not transactional, the two suits of armour mutate shared
world state, the reward scroll shows coins that are not a reward, and most
Herblore training paths work before the quest that is supposed to unlock them.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to the current offer and recommended-combat warning,
Kaqemeex and Sanfew shared dialogue, the four raw and enchanted meats, dungeon
access, the prison-door encounter, cauldron transactions, completion ordering,
the Herblore unlock boundary, downstream quests and diaries, persistence,
migration, journal, debug tools, and verification. It is an implementation
specification, not evidence that the quest is complete.

## 1. Authoritative references

The current article and quick guide define the route, items, avoidable combat,
rewards, and unlock. The transcript is decisive for the standardized quest
offer and current Herblore tutorial; both differ from the old dialogue in the
tree. Revisions were resolved through the OSRS Wiki API on 2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Druidic Ritual](https://oldschool.runescape.wiki/w/Druidic_Ritual?oldid=15240944) | 15240944, 2026-06-27 | Identity, requirements, route, dungeon danger, rewards, and unlock |
| [Druidic Ritual/Quick guide](https://oldschool.runescape.wiki/w/Druidic_Ritual/Quick_guide?oldid=14458277) | 14458277, 2023-08-27 | Exact short critical path and conversation choices |
| [Transcript:Druidic Ritual](https://oldschool.runescape.wiki/w/Transcript%3ADruidic_Ritual?oldid=15263231) | 15263231, 2026-07-14 | Current offer, refusal, re-talks, hand-in, lesson, and completion order |
| [Kaqemeex](https://oldschool.runescape.wiki/w/Kaqemeex?oldid=14924100) | 14924100, 2025-06-22 | Start, completion owner, and post-quest dialogue |
| [Sanfew](https://oldschool.runescape.wiki/w/Sanfew?oldid=15221877) | 15221877, 2026-05-30 | Assignment, hand-in, shared-quest routing, and later work |
| [Cauldron of Thunder](https://oldschool.runescape.wiki/w/Cauldron_of_Thunder?oldid=15009191) | 15009191, 2025-10-24 | Location, four one-for-one products, and nearby danger |
| [Suit of armour](https://oldschool.runescape.wiki/w/Suit_of_armour?oldid=15283635) | 15283635, 2026-07-31 | Triggered aggression, target ownership, stats, and combat behavior |
| [Taverley Dungeon](https://oldschool.runescape.wiki/w/Taverley_Dungeon?oldid=15259518) | 15259518, 2026-07-10 | Entrance, route, dungeon entities, and return path |
| [Enchanted chicken](https://oldschool.runescape.wiki/w/Enchanted_chicken?oldid=15185654) | 15185654, 2026-04-22 | Representative enchanted-meat ownership and creation limits |
| [Herblore](https://oldschool.runescape.wiki/w/Herblore?oldid=15294542) | 15294542, 2026-08-12 | Completion gate for training and boosts, skill methods, and level 3 result |
| [Attack potion](https://oldschool.runescape.wiki/w/Attack_potion?oldid=15247702) | 15247702, 2026-07-02 | The lesson's level-3 example recipe |
| [Chichilihui rosé](https://oldschool.runescape.wiki/w/Chichilihui_ros%C3%A9?oldid=15192726) | 15192726, 2026-04-22 | Documented pre-completion boost exception |
| [Jungle Potion](https://oldschool.runescape.wiki/w/Jungle_Potion?oldid=15281195) | 15281195, 2026-07-29 | Immediate downstream quest and Herblore requirement |
| [Recruitment Drive](https://oldschool.runescape.wiki/w/Recruitment_Drive?oldid=15292308) | 15292308, 2026-08-10 | Direct prerequisite and debug-owner collision |
| [Achievement Diary/All achievements](https://oldschool.runescape.wiki/w/Achievement_Diary/All_achievements?oldid=15263582) | 15263582, 2026-07-14 | Potion-making diary consumers of the unlock |

Transition aid only: Quest Helper at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/druidicritual)
observes states 0, 1, 2, and 3; the four raw/enchanted item alternatives;
Sanfew's upstairs room; dungeon, cauldron-room, ladder, stair and prison-door
locations; and the three exact rewards. `python3 tools/questhelper_extract.py
druidicritual --check` resolves the expected dbrow and every referenced
gameval. Quest Helper is a state/test oracle, not server behavior evidence.

## 2. Canonical contract

Druidic Ritual is a members-only, novice, very short quest released 27 February
2002. It starts with Kaqemeex at the stone circle north of Taverley. It has no
quest or skill requirement. Combat level 10 is recommended, and the current
offer warns players below that level; it does not block them.

A canonical run must:

1. preserve Kaqemeex's optional lore branches, then display the modern
   `Start the Druidic Ritual quest?` Yes/No offer and low-combat warning;
2. send the player to Sanfew upstairs in Taverley's Herblore shop, where he
   requests raw bear meat, raw rat meat, raw beef, and raw chicken;
3. allow the four raw meats to be acquired or brought in advance, including
   ordinary trade/bank ownership, and direct the player to Taverley Dungeon;
4. let the player reach the prison door, animate two level-19 suits of armour
   on attempted entry, target only the triggering player, and preserve the
   canonical option to avoid combat by repeatedly opening the door;
5. transform each meat independently and in any order at the Cauldron of
   Thunder, allowing multiple enchanted copies only while the quest is at the
   appropriate stage;
6. make Sanfew accept exactly one of each enchanted meat atomically, preserve
   reminder and incomplete-item dialogue, then direct the player back to
   Kaqemeex;
7. play Kaqemeex's current in-game lesson about vials, cleaning herbs, grinding
   ingredients, levels, and the guam/eye-of-newt Attack potion before the
   completion moment; and
8. complete once with 4 quest points, 250 Herblore XP, and a real permanent
   ability to train and boost Herblore.

The suits are avoidable, so defeating either is not a completion condition and
must not provide required quest loot. Nearby skeletons and the suits remain a
risk to low-level players, but no combat level is a hard requirement.

Completion leaves the player at 250 Herblore XP, level 3 from a fresh account.
The unlock governs cleaning herbs, making unfinished and finished potions,
herb-tar and barbarian training, Mastering Mixology, Herblore-producing
activities, and ordinary Herblore boosts. The current Wiki documents
Chichilihui rosé as a special pre-completion exception that may reach level 2;
exceptions must be explicit rather than consequences of a missing global gate.

## 3. Native identity and persistence

| Field | Native value / expected behavior |
| --- | --- |
| Quest metadata ID | 18 |
| Dbrow | `quest_druidicritual` |
| Type / difficulty / length | Members; novice; very short |
| Release | 27 February 2002 |
| Start | `kaqemeex` (NPC 5045), coordinate 47795612 |
| Progress carrier | `%druidquest`, native varp 80, clean permanent integer |
| Start / end | 0 / 4 |
| Recommended combat | 10, warning only |
| Rewards | 4 QP; 2500 raw Herblore XP (250 displayed XP) |

The local `druidquest` declaration correctly reuses native varp 80. It has no
packed neighboring fields and no quest-specific side varbits. Whole writes are
therefore valid for this carrier, but only its owning quest lifecycle and
explicitly isolated debug fixtures should perform them.

### 3.1 Primary ladder

| `%druidquest` | Canonical checkpoint | Current local result |
| ---: | --- | --- |
| 0 | Not started / Kaqemeex offer | Lore branches exist; offer and warning are stale |
| 1 | Accepted / speak to Sanfew | Correct owner, room, re-talk, and next step |
| 2 | Collect and enchant four meats | Correct broad stage; item state is inventory-derived |
| 3 | Sanfew accepted meats / return to Kaqemeex | Correct endpoint; hand-in is not atomic |
| 4 | Complete / Herblore unlocked | Correct end value; lesson/reward order and unlock consumers are wrong |

This is one of the few audited quests whose locally emitted primary values
match the native/current ladder exactly. Modernization must not invent finer
persistent states merely to track four independent items; their concrete
ownership is the state until Sanfew consumes the complete set.

### 3.2 Save migration and reward reconciliation

No primary-state remap is required. Preserve 0–4 and reject/log out-of-range
values. Add an authored completion-transaction version or ledger only if no
native field can represent it; do not overload `druidquest` with transient
lesson or scroll state.

Legacy completion can leave several ambiguous cases:

- state 4 is written immediately before XP and quest points are awarded, so a
  process failure can expose completion without all rewards;
- duplicate delivery of the zero-delay completion queue would award XP, quest points, and
  completed-count more than once;
- `::complete quest_druidicritual` and eight Recruitment Drive debug commands
  write state 4 without the reward path; and
- the unlocked actions are currently usable at states 0–3, so item/XP history
  cannot prove that a player learned Herblore canonically.

Recompute quest points and completed count from authoritative quest end states
rather than trying to subtract suspected duplicates. A completed account below
250 total Herblore XP can be raised safely to the 250-XP floor. At or above that
floor, old data cannot prove whether the fixed reward was granted; do not add
another 250 without a durable provenance signal. Record the repaired reward
ledger once, make migration idempotent, preserve higher legitimate XP, and do
not delete raw or surplus enchanted meat.

States 0–2 may own any mixture of raw and enchanted meats in inventory, bank,
or private ground storage. State 3 proves Sanfew accepted one complete set but
does not prove that surplus copies should disappear. State 4 must prevent new
enchanting but may preserve collectible surplus enchanted meat already owned.

## 4. Current implementation surface

The direct quest root contains four files and 159 lines. The playable primary
route spans 539 lines across seven owned/shared files before counting the
Herblore consumers exposed by its reward.

| File | Lines | Current responsibility |
| --- | ---: | --- |
| `quest_druid/configs/quest_druid.constant` | 10 | Native 0–4 constants and stale hand-authored QP constant |
| `quest_druid/configs/quest_druid.varp` | 8 | Native varp-80 declaration |
| `quest_druid/scripts/druid_journal.rs2` | 95 | Dynamic journal for the four active stages and completion |
| `quest_druid/scripts/quest_druid.rs2` | 46 | Completion queues and four cauldron conversions |
| `area_taverly/scripts/kaqemeex.rs2` | 136 | Start, lore, completion owner, obsolete lesson, and post-quest dialogue |
| `area_taverly/scripts/sanfew.rs2` | 194 | Assignment/hand-in plus One Small Favour and Eadgar's Ruse routing |
| `areas/taverly/dungeon/scripts/prison_doors.rs2` | 50 | Two suit transforms/spawns and prison-door traversal |

Gate A also owns or consumes:

- generic ladder and staircase maplinks into Taverley Dungeon and Sanfew's
  upstairs room;
- static `kaqemeex`, `sanfew`, both suit locs, prison doors, cauldron, dungeon
  skeletons, and raw-meat source NPCs/drop tables;
- shared quest completion, quest-point/count, scroll, and jingle scripts;
- Herblore cleaning, brewing, herb tar, barbarian mixes, Mastering Mixology,
  Degrime, Herbiboar XP, boosts, skillcape, and quest-specific recipes;
- Jungle Potion, Recruitment Drive, One Small Favour, Eadgar's Ruse and other
  quests whose requirements depend directly or indirectly on this completion;
- potion-making achievement diary event owners; and
- the global quest journal and quest-cheat registries plus Recruitment Drive's
  prerequisite-forcing debug commands.

The five core interaction headers—Kaqemeex, Sanfew, cauldron, and both prison
door forms—are unique in the current tree. The route has no raw numeric entity
IDs and no legacy `if_openmain`/`if_openoverlay`; it already uses symbolic
gamevals, modern `~p_choice*`, the dynamic journal, and the shared completion
scroll. Modernization should preserve those good foundations.

The older queue records in `QUESTHELPER_CONTENT_PORT_QUEUE.md` label the quest
`audited-ok` and claim an exact Wiki match. Current evidence contradicts that
label: the transcript changed, the completion/lesson order was not compared,
the coin model and transaction boundaries were missed, and the advertised
Herblore unlock was not followed into its skill consumers.

## 5. Start and dialogue audit

Kaqemeex's three opening topics and the major accept/refuse route are present,
but they reproduce an older LostCity conversation. The current transcript
places a standardized `Start the Druidic Ritual quest?` Yes/No prompt after the
lore and displays a warning when combat is below the recommended level 10. The
local script instead offers `Ok / not interesting / what's in it for me`, has
an obsolete reward-negotiation detour, and never shows the warning.

Restore the current transcript while preserving all optional lore and clean
return paths. The combat warning must be advisory; selecting Yes at combat 3
is valid. State 1 must be written only after final acceptance. Repeated talk at
states 1 and 2 should continue to Sanfew, while post-completion talk should
offer current fundamentals again without replaying rewards.

Sanfew's Druidic Ritual arms closely match the current assignment, location,
reminder, incomplete, and successful hand-in branches. His shared trigger
checks two One Small Favour states before Druidic Ritual, then later routes
post-quest dialogue into Eadgar's Ruse. These priorities are harmless for
valid saves because the later quests require Druidic Ritual, but malformed or
debug-prepared combinations can shadow the active ritual. Replace implicit
source-order assumptions with one documented shared-NPC dispatcher and tests
for every simultaneously reachable state.

## 6. Meat acquisition, ownership, and cauldron

All four raw meats resolve and have real acquisition paths in current drop
tables; players may also trade or bank them. All four enchanted forms resolve
as members-only, untradeable quest items. The canonical route allows raw items
to be brought before starting, conversion in any order, partial trips, and
multiple enchanted copies while state 2 is active.

The local cauldron correctly restricts conversion to exactly state 2 and maps
each raw item one-for-one to its enchanted form. It does not track artificial
per-meat bits, and it stops conversion after Sanfew's hand-in. Those choices
should remain.

Each branch currently prints success, deletes the raw item, and adds the
enchanted item without checking either inventory mutation. Replace this with a
shared atomic conversion:

1. validate player, stage, source slot/type/quantity, cauldron identity, range,
   and not-busy state;
2. exploit the source slot that will be freed rather than demanding an extra
   inventory slot;
3. replace exactly one item in that slot or rollback on failure;
4. play the canonical message only after success; and
5. reject repeated/stale packets without duplication or loss.

Audit Drop, private-ground ownership, bank, death, alchemy, cooking, and trade
behavior for each enchanted form. Canonical recovery is simply to acquire and
enchant another raw item while state 2; no NPC replacement or permanent
per-item checkpoint is needed. Public ownership must not allow another player
to hand in someone else's untradeable item.

## 7. Dungeon access and suits of armour

Quest Helper resolves the surface ladder, Sanfew stairs, both prison doors,
and the cauldron against revision 239. The generic travel route and map content
exist; this quest does not need a teleport shortcut or private dungeon.

The prison-door implementation is unsafe in a shared world. It finds either
static suit loc at a fixed coordinate, globally deletes that loc for 500
cycles, hand-spawns a global NPC for 500 cycles, and returns. A second player
can consume the other suit, see a missing prop, inherit aggression, or enter a
door whose intended response was triggered by someone else. Radius/location
existence is not player ownership.

Preserve the canonical interaction: each of the two suits animates once in
response to that player's attempts, is aggressive only to the triggering
player, and never makes killing mandatory. Repeated door clicks must eventually
walk the player through even while one or both suits live. Use per-player loc
transforms and owned NPCs where supported, or an explicit actor registry with
owner/generation/type checks, relog cleanup, death cleanup, respawn, and
cross-player tests. Do not globally delete static scenery.

The NPC's native combat identity is level 19, 29 HP, slash, five-tick attack,
and max hit 3. The local overlay broadly matches its combat stats but assigns a
`bones` death drop even though the current canonical page exposes no reward
drop. Verify this with a live kill and make the quest suit zero-loot if
confirmed. Combat credit must never advance the quest or substitute for door
entry.

## 8. Sanfew hand-in

Sanfew correctly demands all four enchanted types at once and does not consume
a partial set. On success, however, four unchecked `inv_del` operations occur
after several dialogue lines and before state 3. A changed slot, repeated
resume, disconnect, or unexpected delete failure can remove only part of the
set while still advancing.

Implement one inventory transaction that locks the four concrete slots,
revalidates type/quantity after dialogue, removes exactly one of each, commits
state 3, and rolls back on any failure. A repeated click after commit must route
to the state-3/post-hand-in dialogue and consume nothing. Surplus enchanted
copies remain. Test all 16 present/missing combinations, duplicate quantities,
bank-only ownership, inventory rearrangement during dialogue, reconnect, and
double submission.

## 9. Completion, lesson, and reward transaction

Current OSRS has Kaqemeex deliver the entire Herblore lesson and then completes
the quest. The local `druid_completion` queues `druid_quest_complete`, which
immediately writes state 4, grants XP and calls the completion scroll, then
queues `druid_fundamentals`. This reverses the canonical order. Because the
second queue depends on `npc_uid`, an NPC lookup failure can skip the lesson;
because the scroll mounts asynchronously, the chat and modal can also obscure
or interrupt one another.

The lesson itself is obsolete. It tells the player to consult the Council's
website twice and to `identify` herbs. The current transcript keeps the guide
in-game, says herbs must be cleaned, says some ingredients may need grinding,
and removes those external-site lines. Update the words and preserve the
current guam leaf + eye of newt Attack-potion example.

Finish the lesson under a protected player-owned continuation, then execute a
single idempotent completion transaction. It must durably record the 250 XP
award and completion entitlement, move to state 4, let shared quest metadata
award exactly 4 QP and one completed-count increment, play the novice jingle,
and mount the scroll once. Repeated clicks, duplicate queues, relog at each
line, or missing Kaqemeex must resume safely without duplicating rewards.

The scroll currently receives `coins` as its rotating model even though the
quest awards no coins. The shared API always renders its model argument. Trace
the current reward scroll and use its canonical cache model or explicitly hide
the model slot; never display a fabricated coin reward.

## 10. Herblore unlock audit

The largest gameplay defect is outside the quest root. Druidic Ritual's reward
text says Herblore is unlocked, but the generic clean and brew entry points
only check `map_members`. A member at state 0 can clean grimy herbs, make
unfinished and finished potions, and gain Herblore XP if their level was raised
by any other path. Herb tar and Mastering Mixology also lack the quest gate;
Degrime and Herbiboar award Herblore XP without checking completion. The
skillcape boost has no explicit gate. In contrast, Wintertodt potion-making and
the POH Greenman's ale path already check `%druidquest`, proving the policy is
expected but inconsistently applied.

Create one shared `~herblore_unlocked` predicate and apply it at the public
boundary of every training or boost action, before item/rune deletion, XP,
animation, or output. At minimum audit:

| Consumer | Current gate | Required behavior |
| --- | --- | --- |
| Manual grimy-herb cleaning | Members only | Refuse before state 4 without changing item or XP |
| Unfinished/finished potions | Members + level | Add state-4 gate before recipe or consumption |
| Herb tar | No quest gate | Require unlock plus real level/XP recipe contract |
| Barbarian mixes | Generic brew path | Require Druidic Ritual and the Barbarian-training owner |
| Mastering Mixology | Level only | Require state 4 before paste handling or minigame entry |
| Degrime | Spell requirements + herb level | Require state 4 before runes, conversion, or half XP |
| Herbiboar harvest XP | Herblore level only | Require state 4 before harvesting/Herblore XP |
| Wintertodt potion | Explicit state-4 gate | Retain and route through shared predicate |
| POH Greenman's ale | Explicit state-4 gate | Retain consumption and refusal semantics |
| Ordinary/mature ales, pies, stews, cape | Missing or partial handlers | Implement current boost rules and state-4 gate |
| Chichilihui rosé | Content incomplete | Preserve the documented pre-completion level-2 exception |

Do not put this policy inside `stat_advance` globally: fixed quest rewards and
other legitimate XP awards may target Herblore without being training actions.
The guard belongs at user-action/activity boundaries, with named exceptions.
Refusal must not consume ingredients or ordinary boost items unless the pinned
canonical behavior says consumption still occurs.

The unlock also needs client-facing consistency. The skill guide may remain
viewable, but actionable recipes, minigames, and boosts must not appear usable
and then succeed through crafted packets. Add server-side guards regardless of
client filtering.

## 11. Downstream quests, diaries, and shared owners

Direct start consumers found in the tree include Jungle Potion, Recruitment
Drive, One Small Favour, and Eadgar's Ruse/Sanfew. Normal Jungle Potion and
Recruitment Drive dialogue explicitly check state 4; One Small Favour records
the requirement, and later Herblore-level quests rely on the skill being
untrainable before completion. Re-test every quest requiring any Herblore level
after the central gate lands; a raw stat raised through debug or a lamp must not
silently replace a required Druidic Ritual completion where current OSRS still
requires the unlock.

The current achievement-diary reference includes potion-making tasks in
Kourend & Kebos, Desert, Kandarin, Morytania, Karamja, and Varrock. No
quest-specific diary event owner was found through the Druidic/Herblore state
search. Each existing recipe must emit its canonical event only after a valid
state-4 craft, in the exact region and from the required starting materials;
pre-quest rejected attempts must never count.

Sanfew is a shared owner for Druidic Ritual, One Small Favour, and Eadgar's
Ruse. Preserve explicit precedence and all post-quest dialogue without letting
a later quest consume ritual items or hide state-2 reminders. Kaqemeex should
remain the sole completion/re-teaching owner.

## 12. Journal and debug tooling

The dynamic journal is wired to `quest_druidicritual` and broadly correct at
states 0–4. It does not distinguish which raw/enchanted meats are already
owned, banked, or still required. Its state-2 formatting also concatenates
several color/text fragments around `in|the Cauldron`, making spacing brittle.
Rebuild the stage-2 section from an ownership matrix so each of four ingredients
is complete, raw-but-needs-enchanting, or missing, and name the real next route.

`::complete quest_druidicritual` currently writes only state 4. It grants no
XP, quest points, completed count, tutorial ledger, or reward reconciliation.
Eight Recruitment Drive `debugproc`s also force both Black Knights' Fortress
and Druidic Ritual complete as a side effect. Debug preparation must call a
shared idempotent prerequisite fixture or use isolated test-save setup; it must
not silently manufacture a partially completed permanent quest on a normal
save. Running the completion adapter twice must be a no-op the second time.

Remove the stale `^druid_questpoints = 4` constant if it has no consumer. The
dbrow is authoritative for quest points, and parallel hand-maintained reward
metadata invites drift.

## 13. Modernization sequence

1. Pin the current transcript/cache evidence, capture the canonical completion
   scroll model and suit behavior, and add a machine-readable state/item/owner
   contract.
2. Add the idempotent reward ledger and migration/reconciliation tests without
   changing the correct native 0–4 meanings.
3. Update Kaqemeex's offer, low-combat warning, current lesson, re-talks, and
   protected lesson-before-completion continuation.
4. Make each cauldron conversion and Sanfew's four-item hand-in atomic, with
   storage-aware ownership and repeated-packet tests.
5. Replace global suit-loc deletion/spawns with per-player transforms and owned
   combat while retaining spam-click avoidance and ordinary dungeon travel.
6. Make completion exactly once, remove the coin model, reconcile QP/count/XP,
   and update the journal and debug adapter.
7. Centralize the Herblore unlock predicate across all current training and
   boost entry points, preserve documented exceptions, and wire downstream
   quest/diary events.
8. Run Gate D, attach real-client captures, and change status only when both the
   short quest route and the permanent skill boundary pass.

No quest-specific C shortcut is justified. The route, transactions, owned NPC
spawns, dialogue, quest completion, and Herblore gates are expressible in the
current RuneScript/config machinery. If atomic multi-item inventory commit is
not available, add one general tested inventory transaction capability rather
than four quest-specific opcodes.

## 14. Verification matrix

### Static and pack checks

- Run `python3 tools/questhelper_extract.py druidicritual --check`.
- Resolve dbrow 18, varp 80, Kaqemeex, Sanfew, four raw and four enchanted
  meats, cauldron, both doors, suit NPC/locs, ladder, stairs, Attack-potion
  ingredients, completion interface, and novice jingle.
- Fail on duplicate core triggers, raw IDs, legacy modal opens, direct
  quest-point constants, global suit deletion/spawns, unchecked quest-item
  transactions, coin reward model, completion-before-lesson, or a public
  Herblore training/boost entry point without the shared unlock predicate.
- Run `make -C src mock230-scripts` and the revision-239 pack check.

### State, dialogue, and item tests

- Exercise 0→1→2→3→4 from the real Kaqemeex spawn, including all lore choices,
  Yes/No, low-combat warning, every re-talk, current lesson line, and post-quest
  fundamentals replay.
- Verify combat levels below, at, and above 10 all may accept after the warning
  policy.
- Test every raw/enchanted combination, all 24 conversion orders, duplicate
  meats, pre-start possession, bank/private-ground ownership, Drop/death,
  conversion outside state 2, full inventory, slot movement, repeated packets,
  reconnect, and surplus items after completion.
- Test all 16 Sanfew present/missing sets plus dialogue-time slot mutation,
  repeated submission, interruption after each transaction phase, and rollback.
- Test two players opening the prison door concurrently: each sees two correct
  transforms, owns only their attackers, can spam through, cannot affect the
  other's locs/NPCs, and receives no quest progress or unintended loot from a
  kill.

### Completion, migration, and unlock tests

- Interrupt/relog after every lesson line and every durable completion step;
  resume at the correct point with exactly 250 XP, 4 QP, one completion-count
  increment, one jingle, and one scroll.
- Deliver duplicate queues and repeat Kaqemeex/`::complete`; all later attempts
  must be no-ops. Verify migration for states 0–4, Herblore XP below/equal/above
  250, suspected duplicate QP, surplus items, and all debug-produced states.
- Before state 4, attempt manual cleaning, every brew stage, herb tar,
  barbarian mix, Mixology, Degrime, Herbiboar, Wintertodt, and every implemented
  boost. Verify no protected input, rune, XP, or output changes. Test the
  Chichilihui exception separately.
- After state 4, test the same successful/failing level, inventory, recipe, and
  activity contracts; ensure no double guard changes normal timing or XP.
- Start every direct downstream quest with states 3 and 4, test malformed high
  Herblore at state 0, and verify all implemented potion diary events reject
  pre-quest actions and accept only exact post-quest crafts.

### Real-client evidence

Capture the modern offer and combat warning; Sanfew assignment/reminder;
surface and dungeon travel; both suit transforms and avoidance; four cauldron
products; partial and complete hand-in; current Herblore lesson; quest-complete
scroll with no fake coins; level-3/250-XP result; journal at each state;
pre-quest clean/brew/boost rejection; post-quest Attack potion; downstream
quest gate; concurrent-player prison door; relog recovery; and idempotent debug
completion.

`verified-modern` requires both the visible 0–4 route and the permanent
Herblore boundary to pass. The obsolete offer/lesson, unsafe completion queue,
shared suits, transaction gaps, false reward model, and missing skill gates are
critical; they cannot be waived as cosmetic deviations.
