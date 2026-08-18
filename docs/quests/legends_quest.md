# Legends' Quest modernization audit

Status: `partial / blocked organic route` — the cache contains the native quest
row, permanent state and bitfields, every principal object and actor, the full
surface and dungeon geography, journal dispatch, completion metadata, guild
shops, and post-quest feature rows. The 26-file, 3,006-line quest root also has
substantial recent work on Nezikchened's three encounters and the Yommi growth
sequence. It is nevertheless impossible to play from the real start to
completion. The Legends' Guards have no talk handler and the outer gate refuses
an unstarted player, so Radimus cannot be reached. Dense-jungle cutting, the
Shaman Cave entrance, lockpick/Mining/Strength trials, rune wall, real gem
puzzle, magic gate, winch, Viyeldi trigger, crystal furnace, dragon eye and
heart recess are absent or claimed by incorrect generic fallbacks. Both
canonical Viyeldi branches therefore dead-end. If states are injected around
those blockers, the reward loop grants five 30,000 XP selections rather than
four.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A-D to requirements, guild admission, map-making, jungle
traversal, Gujuo ownership, both caves, every skill trial and puzzle, item
lifecycle, all three Nezikchened fights, the Viyeldi choice, Yommi growth,
totem replacement, hand-in, training rewards, recovery, journal/admin behavior
and downstream unlocks. This is an implementation specification, not evidence
that the quest works; this audit intentionally makes no gameplay changes.

## 1. Authoritative references

The OSRS Wiki was requested as the gameplay authority. Direct Wiki page/API
access was blocked by robots during this audit, but search-indexed article
content was reviewed and the implementation already records recent pinned
revisions. Those pinned revisions are the reproducible baseline below. Re-pin
them immediately before coding because Wiki mechanics remain authoritative over
the port and Quest Helper.

| Reference | Revision / status | Audit use |
| --- | --- | --- |
| [Legends' Quest article](https://oldschool.runescape.wiki/w/Legends%27_Quest?oldid=15293032) | oldid 15293032 | Requirements, full route, recovery, rewards and unlocks |
| [Quick guide](https://oldschool.runescape.wiki/w/Legends%27_Quest/Quick_guide?oldid=15231427) | oldid 15231427 | State order, item/skill gates, both Viyeldi paths and repeat trips |
| [Quest transcript](https://oldschool.runescape.wiki/w/Transcript%3ALegends%27_Quest?oldid=15263273) | oldid 15263273 | Acceptance, choices, failure, replacement and post-quest dialogue |
| [Nezikchened](https://oldschool.runescape.wiki/w/Nezikchened?oldid=15242487) | oldid 15242487 | Three encounters, prayer drain, weakening tools and reset behavior |
| [Ranalph Devere](https://oldschool.runescape.wiki/w/Ranalph_Devere?oldid=15215908) | oldid 15215908 | Combat identity and heart-crystal drop |
| [Irvig Senay](https://oldschool.runescape.wiki/w/Irvig_Senay?oldid=15215870) | oldid 15215870 | Combat identity and heart-crystal drop |
| [San Tojalon](https://oldschool.runescape.wiki/w/San_Tojalon?oldid=15276414) | oldid 15276414 | Combat identity and heart-crystal drop |
| [Holy water](https://oldschool.runescape.wiki/w/Holy_water?oldid=15290188) | oldid 15290188 | Binding-book vial enchantment, bowl decanting and ranged weapon behavior |
| [Climbing rope](https://oldschool.runescape.wiki/w/Climbing_rope_%28Viyeldi_caves%29?oldid=14638714) | oldid 14638714 | Winch persistence and return traversal |
| [Dragon sq shield](https://oldschool.runescape.wiki/w/Dragon_sq_shield?oldid=15090955) | oldid 15090955 | Smithing recipe versus Legends-gated wield requirement |
| [Quest experience rewards](https://oldschool.runescape.wiki/w/Quest_experience_rewards) | current search-index snapshot reviewed | Exactly four 30,000 XP choices |

Transition aid: local Quest Helper commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/legendsquest)
maps states 0-20, 22, 25, 30-35, 40, 45, 50, 55, 60, 65 and 70 to
the relevant zones, actors, items and actions. Its quest path last changed in
`354ccc5` on 2025-10-02. It is transition/test evidence only.
`python3 tools/questhelper_extract.py legendsquest --check` exits zero and all
referenced gamevals resolve, but it reports the quest dbrow as
`quest_familycrest`. That association is false. Fix the extractor's descriptor
selection, then require both the correct `quest_legends` association and clean
gameval resolution before considering this check authoritative.

## 2. Canonical contract

Legends' Quest is a members, master, long quest released on 20 August 2003. It
starts through the Legends' Guards outside the guild. The player needs 107
quest points and completion of Family Crest, Heroes' Quest, Shilo Village,
Underground Pass and Waterfall Quest. The ten skills are checked where used,
not at acceptance, and are boostable: Agility 50, Crafting 50, Herblore 45,
Magic 56, Mining 52, Prayer 42, Smithing 50, Strength 50, Thieving 50 and
Woodcutting 50.

The canonical route is:

1. pass the guards, accept Radimus's assignment, obtain his notes, enter the
   Kharazi Jungle using an axe and machete, and separately map its west, middle
   and east sections with papyrus and charcoal at Crafting 50;
2. obtain a bullroarer from a jungle forester, summon Gujuo, agree to rescue
   Ungadulu, locate and enter the mossy-rock Shaman Cave at Agility 50;
3. investigate Ungadulu and pass the cave's Agility crevice, Thieving lock,
   three Mining boulders and Strength gate;
4. read the marked wall and place Soul, Mind, Earth, Law and Law runes in that
   order, then place opal, jade, red topaz, sapphire, emerald, ruby and diamond
   on their correct carved rocks to reveal the binding book;
5. get Gujuo's vessel sketch, smith a gold bowl from two gold bars at an anvil
   with a failure chance, have Gujuo bless it at Prayer 42, cut a hollow reed,
   and siphon pure water from the unreachable pool;
6. use the water on the fire wall and the binding book on Ungadulu, defeat the
   first Nezikchened, receive Yommi seeds, germinate them with pure water, find
   the pool polluted, consult Gujuo and brew/drink a bravery potion;
7. cast any Charge Orb spell on the magic gate, tie a rope to the winch, descend
   into the Viyeldi caves, defeat the three ancient warriors for crystal
   sections, fuse and activate the heart crystal, place it in the recess, and
   reach the blocked sacred-water source;
8. accept Echned Zekin's dark dagger and choose either the short path—summon
   Viyeldi from the blue hat and stab him—or the long path—return the dagger to
   Ungadulu for the Holy Force card; expose and defeat the second Nezikchened,
   move the boulder and refill the bowl with sacred water;
9. plant, water, grow, fell, trim and carve a Yommi tree before it rots, take
   the totem, return it to the corrupted pole, defeat the short-path rematches
   with San, Irvig and Ranalph where applicable, defeat the final Nezikchened,
   replace the pole and receive a gilded totem from Gujuo; and
10. give Radimus the completed map and gilded totem, enter the guild, claim
    exactly four freely chosen 30,000 XP sessions, and talk once more to finish.

Completion awards four quest points and 120,000 total selectable XP. Each
30,000 award may target Attack, Defence, Strength, Hitpoints, Prayer, Magic,
Woodcutting, Crafting, Smithing, Herblore, Agility or Thieving, and the same
skill may be selected repeatedly. Unlocks include the Legends' Guild and cape,
dragon square shield wielding, skills-necklace/combat-bracelet charging at the
guild and Fountain of Rune, normal Kharazi access, the level-79 vine shortcut,
replacement/toggleable dark daggers, post-quest bravery-potion making and the
Legends-gated rare-drop behavior. Modern OSRS consumers such as achievement
diaries, later quests and Nightmare Zone must be inventoried and tested rather
than inferred from the 2003 reward scroll.

## 3. Native identity, state and assets

| Field | Native value |
| --- | --- |
| Cache quest / dbrow | id 51 / `quest_legends` |
| Implementation root | `quest_legends`, 26 files, 3,006 lines |
| Classification | members, master, long |
| Start / end | start NPC 10712 (`legends_guild_guard1` in Quest Helper) / state 75 |
| Release | 20 August 2003 |
| Requirements | 107 QP; ten boostable, route-time skills |
| Quest points | 4 |
| Recommended combat | 65 |
| State carriers | permanent `%legendsquest`, permanent `%legends_bits` |
| Attempt/ownership fields | two temporary Nezikchened vars; six player-UID Yommi fields |
| Latest audited root commit | `4498b4e69b81cd817d63b2d31a0081d1095fbce2`, 2026-08-17 |

The root consists of eight config files and eighteen scripts. It owns Radimus,
the foresters, bullroarer/Gujuo, Ungadulu, Book of Binding, the invented gem
shortcut, barrels, three warriors, Viyeldi/Echned, boulder, Nezikchened, the
Yommi/totem sequence, guild gates/doors and a 459-line journal.

The cache already provides the correct native objects for the missing route:
the jungle vegetation, mossy entrance, crevices, lockpick gates, three mining
boulders, strength gate, marked wall, one coordinate-disambiguated
`lg_gemplacerock`, magic gate/open form, barrels, winch forms, climbing rope,
rocky ledges, Viyeldi hat, crystal furnace, dragon-eye rock, heart and recess
forms, shimmering fields, sacred-water source and both totem-pole forms. The
implementation's claim that separate gem rocks were unavailable is therefore
not a sound reason for its all-at-once shortcut.

World spawns include Radimus in his hut and guild, both Legends' Guards, a
static `ungadulu_good`, multiple public San/Irvig/Ranalph warriors, and the
item-looking `viyeldihat`. Gujuo, Echned, Viyeldi and the three Nezikchened
encounters are meant to be conditional or player-owned. Build an explicit
state/zone/owner phase matrix before changing spawns; do not delete or transform
a shared actor to phase one player.

The quest dbrow correctly says skills are not checked on start and are
boostable. Its five `requirement_quests` numeric rows are corrupt: 72, 48, 133,
154 and 158 currently resolve to Roving Elves, Murder Mystery, Another Slice of
H.A.M., X Marks the Spot and Sins of the Father. Runtime Radimus code names the
correct five quests. Repair the cache row rather than teaching consumers to
ignore it.

## 4. Progress-state audit

The source labels its constants `todo: confirm these`; Quest Helper broadly
confirms them but exposes missing phases. Treat item, actor, zone and encounter
receipts as part of each invariant instead of using the main integer alone.

| State | Canonical checkpoint | Current writer / defect | Required invariant |
| ---: | --- | --- | --- |
| 0 | not started | default; guards have no handler and gate blocks entry | Eligible guard dialogue admits player; accept/refuse is replay-safe |
| 1 | notes received / started | Radimus writes after add/drop | Confirmation, notes ownership and start scene settle before commit |
| 2 | all three map sections complete | one anywhere-in-jungle shortcut consumes one set | Three native bits, Crafting checks/failures and completed map agree |
| 3 | bullroarer received | forester dialogue | Replacement predicate covers inventory, bank and private ground |
| 4 | Gujuo summoned | bullroarer interaction | Player-owned Gujuo exists; no other player's actor suppresses spawn |
| 5 | rescue accepted | Gujuo dialogue | Correct transcript branch and cave lead are known |
| 6 | entrance found | fire-wall interaction, not mossy entrance | Agility entrance traversal owns this checkpoint |
| 7 | Ungadulu investigated | dialogue | Correct phased actor and cavern questions persist |
| 8-10 | vessel/water preparation | state 9 has no constant; Gujuo shortcut writes 10 | Sketch, smithing, Prayer blessing, reed and bowl form drive progress |
| 11 | first Nezikchened summoned | binding-book use | Owned encounter receipt and prayer-drain attempt state exist |
| 12 | first Nezikchened defeated | death queue | Kill, Ungadulu phase and retry/full-health behavior agree |
| 13 | all pure bowls consumed; seeds germinated | seed use | OSRS all-bowls rule, inventory/bank settlement and seeds are atomic |
| 14-15 | polluted pool observed; Gujuo consulted | constants/journal, no complete writer route | Hollow-reed attempt observes pollution before Gujuo advances |
| 16 | bravery drunk and lower cave entered | no route writer | magic gate, rope bit, bravery bit and descent all reconcile |
| 17 | three sections fused | no furnace handler | three owned kill/drop receipts and heart item settle once |
| 18 | activated heart placed in recess | no dragon-eye/recess handlers | activated form, loc phase and barrier traversal agree |
| 19 | blocked source/Echned encountered | boulder script | Correct zone/actor ownership and repeat approach behavior |
| 20 | dark dagger received | Echned adds without space/recovery protection | dagger ownership succeeds before state; replacement is defined |
| 21 | Viyeldi decision phase | no constant/writer | Explicit unresolved choice/hat/dialogue receipt |
| 22 | second Nezikchened defeated | death queue | short/long path tool, kill and boulder retry agree |
| 23-24 | source-cleansing/boulder phases | absent | Preserve any native/QH-observed intermediates during live capture |
| 25 | sacred water collected | direct source fill | moved boulder, correct bowl, dose and water-source rules agree |
| 30 | Yommi totem collected | timed global loc sequence | Player-owned plant lifecycle settles item before checkpoint |
| 31-34 | final encounter subphases | QH observes them; source only writes 32 and uses bits | Persist hero/demon phase and safe retry without shared-state leakage |
| 35 | final Nezikchened defeated | death queue | Owned kill receipt survives logout/death and pole is actionable |
| 40 | good totem replaced | global loc transform for 500 ticks | Permanent player phase; no shared pole mutation |
| 45 | gilded totem received | Gujuo add/private drop then commits | Recoverable ownership before commit; Gujuo can replace loss |
| 50 | map and totem returned | Radimus deletes both then writes | Exact owned items and hand-in receipt settle atomically |
| 55/60/65/70 | four training choices claimed | each call grants 30,000, then adds five | Durable per-award receipt prevents cancel/crash duplication |
| 75 | complete | state 70 calls a fifth training session before completion | Final extra talk grants no XP; shared completion settles exactly once |

Permanent `%legends_bits` already has the three map bits; cavern dialogue;
Soul/Mind/Earth/two-Law ordering; seven gem placements; ten bowl doses; tied
rope; bravery consumed; three crystal pieces; killed Viyeldi; given dagger;
and three final-warrior kills. Bits 4-6 are deliberately reused between early
Ungadulu dialogue and the final warriors. Keep a migration table around that
reuse and never clear those bits until the earlier meaning is obsolete.

## 5. Start, guild and jungle traversal

Both guards are correctly spawned outside the guild, but there is no
`[opnpc1,legends_guild_guard1/2]`. At state 0 the outer gate lets an exiting
player through but refuses entrance and says a guard approaches. Radimus's hut
is north of that gate. The only organic start actor is therefore unreachable.
Implement the transcript-accurate guard eligibility/admission exchange and
ensure the gate walk is actor/session controlled, collision-safe and reversible.

Radimus checks the correct five runtime quest vars and 107 QP. It correctly
does not reject the ten route-time skills. The start is still a two-choice
paraphrase rather than the standard quest-start confirmation and summary. It
places notes on the public ground if full, commits after that placement, and
charges 30 coins for replacements. Pin the transcript for exact confirmation,
price, cupboard machete, table supplies, replacement and inventory-full rules;
no cupboard/table handlers were found.

No quest or shared handler was found for cutting the dense Kharazi vegetation.
This is the next hard blocker after start. Author the correct axe/machete,
Woodcutting, failure, vegetation-transform, movement and re-entry behavior in
the shared jungle-cutting service, preserving Tai Bwo Wannai/Jungle Potion
ownership where appropriate.

The map shortcut ignores the existing west/middle/east zones and bits. Using
either papyrus or charcoal on the notes anywhere inside the broad jungle zone
consumes one of each and immediately creates a completed map. Restore the three
separate Crafting-50 observations, current-stat boost behavior, paper/charcoal
failure and break rules, and loss/replacement behavior. Do not advance state 2
until all three bits and the completed-map ownership are reconciled.

Forester dialogue is shared with One Small Favour and must retain branch
precedence. The Legends branch grants/replaces the bullroarer, but replacement
checks and full-inventory behavior need bank/private-ground tests. Swinging it
uses a radius-global `npc_find`; the initially spawned Gujuo is not explicitly
owner-bound. Make summoned Gujuo player-owned, bounded to the summoner and
eligible zone, and safe under two simultaneous players.

## 6. Shaman Cave, runes and gems

The mossy `lgshamancaverock1` has no quest handler, nor do the corresponding
exit, bookcase/crevice and marked-wall route. Add the 50 Agility squeeze with
the correct boost/failure/damage behavior and bidirectional recovery. The
bottom lockpick gate is currently captured by
`general_use/scripts/door_locked_fallback.rs2` and always says it is locked.
It must use the 50 Thieving check, lockpick requirements/break chance and exact
open traversal. The three `mine_test_boulder*` objects have no 52 Mining
handler. The strength gate falls into generic double-door behavior and opens
for free, bypassing the 50 Strength trial. Give each obstacle one explicit
quest owner and remove only its conflicting fallback claim.

The native route reads/searches `lgancientwalldoor`, then consumes the SMELL
runes in order. A wrong rune resets the attempt when the cave is re-entered.
The current `legends_gem_shrine` instead accepts all five runes and all seven
gems at the shimmering barrier in one unchecked transaction, deletes all
twelve objects before confirming output placement, and directly gives the
binding book. That bypasses every rune bit and gem-row bit.

Implement each rune-on-wall and gem-on-rock action against the native bits and
the rock's verified coordinate. Correct gems are consumed; an incorrect gem is
not. All seven correct placements trigger the light show and private binding
book appearance. If the book is lost, gems must be repeated but SMELL runes are
not. Item removal, scene completion and book ownership must be one replay-safe
settlement. The Book of Binding's current read action is a series of message
boxes rather than the native book interface; recover that interface if present
and test vial enchanting at current Magic 56 and Prayer 42 with the exact
five-point costs and slot-preserving conversion.

## 7. Bowl, pure water and first battle

Gujuo currently deletes one gold bar and grants an already blessed bowl when
base Smithing is 50. It never gives `goldbowlpic`, never uses an anvil or second
bar/failure roll, and never checks Prayer 42. This is an invented soft-skip.
Restore the sketch; any-anvil, hammer and two-bar Smithing-50 attempt; failure
consumption (players commonly bring two to six bars); the unblessed bowl; and
Gujuo's current-Prayer-42 blessing attempt with five Prayer drained on failure.
The shared post-quest gold-smithing menu is not a substitute for this quest
recipe and currently refuses gold smithing until completion.

The surface pool currently fills a blessed bowl directly. Canonical access
requires cutting a hollow reed from `tall_reeds` with a machete and using that
reed on the out-of-reach pool. No reed handler exists. Add reed acquisition,
pure/polluted pool forms, bowl dose semantics and the chance for pure water to
evaporate on leaving the jungle. Germinating seeds intentionally empties all
pure-water bowls; verify the exact inventory and bank scope against a current
live capture before retaining the port's bank-wide deletion.

One use of water currently removes the entire bowl contents, changes one fire
segment and treats the barrier as solved. Restore the multi-segment fire
timing/re-ignition and movement puzzle. Ungadulu is a static public good-form
spawn even before the quest, while fight setup deletes/transforms actors around
the player. Replace that with a per-player bad/good phase. The recent first
Nezikchened code has useful modern foundations—owner assignment, pinned stats,
prayer drain, Holy water effects and last-hit handling—but it needs adversarial
two-player, timeout, logout, death and re-entry tests before reuse.

The shared Herblore table owns ardrigal + snake weed -> bravery potion and is
the right service boundary. Keep the route-time 45 Herblore check boostable,
make drinking set the permanent bravery bit, and allow post-quest remaking.
States 14 and 15 must be driven by trying a hollow reed on the now-polluted pool
and then asking Gujuo, not by an injected state.

## 8. Lower cave, crystals and Viyeldi choice

Most of this chapter is absent despite complete cache assets:

- `lgmagictrialgateclosed` behaves as an ordinary door, so no Charge Orb spell,
  unpowered orb or runes are consumed and Magic 56 is bypassed;
- there is no rope-on-winch action, tied-rope persistence, bravery gate or
  verified winch descent; generic `Climb-down` has no discovered maplink;
- rocky-ledge fall/recovery, damage and safe return traversal are unverified;
- public San/Irvig/Ranalph spawns can award crystal pieces through death
  handlers, but their `inv_add` paths have no full-inventory/private-ground
  settlement and need kill ownership review;
- `furnace_legendsquest`, `dragons_eye_rock` and `heart_recess_empty` have no
  handlers, making states 17 and 18 unreachable; and
- the static `viyeldihat` has no take/talk trigger and Viyeldi is not summoned,
  so the short branch cannot be selected organically.

Implement the magic gate through the shared spell-on-loc service, preserving
the selected Charge Orb spell's exact runes, orb conversion and retry. Rope and
bravery are one-time access receipts: later trips require another Charge Orb
cast but not another rope or potion. Create bidirectional maplinks and test
failed grip/fall damage, returning by the hanging climbing rope and login on
both sides.

The crystal chapter must award only the relevant section after a player-owned
eligible kill, replace a lost section without farming contradictory state, fuse
all three at the furnace, activate the heart at the dragon's eye, fill the
recess and phase the shimmering field. Full inventory, ground expiry, death,
logout and shared-spawn kills all need explicit outcomes.

Echned currently gives the dark dagger without checking whether `inv_add`
succeeds. His transformation path is partly implemented but not safely reached.
The Viyeldi script explicitly declares the canonical long route out of scope.
No dark-dagger-on-Ungadulu route was found, even though Holy Force handlers and
journal text exist. Implement both equal branches:

- short: hat interaction summons an owned Viyeldi; the in-dialogue dagger use
  kills him, records the choice, and using the dagger on Echned exposes a
  stronger second Nezikchened; and
- long: using the dagger on Ungadulu consumes it and grants Holy Force; using
  the card near Echned exposes/weakens Nezikchened without killing Viyeldi.

The existing second-fight dagger/Holy Force and prayer-drain code is a candidate
to retain after precise fight and reset tests. Dagger/card acquisition must be
slot-safe and recoverable. After victory, moving the boulder and filling from
the sacred source must own states 22-25; a direct bowl-on-source conversion
must not bypass the encounter.

## 9. Yommi tree and final encounter

The Yommi implementation models planting, watering, timed growth/rot, chopping,
trimming, carving and totem collection and correctly uses current Herblore 45
and Woodcutting 50 checks. It records the planter UID for each of six fixed
plots. However, each `loc_change` is still world-shared: one player's tree is
visible to and physically blocks other players even though the UID rejects
their actions. Move the sequence to player-private loc phases/instances, or a
general per-player loc service, and recover owned state on logout/re-entry.

The current timers run long `world_delay` sequences and rotten-tree cleanup
adds logs without robust free-space settlement. Verify every growth duration,
water dose, axe tier, chop roll, XP, rot condition and magic-log side effect
against the pinned article/live captures. Test all six plots simultaneously,
two players on one plot, full inventory at every product, teleport/death and
server restart. Do not clear bits 4-6 for final-warrior reuse until the totem is
owned and early dialogue no longer consumes them.

Using the Yommi totem on the evil pole deletes/adds a public loc for 500 ticks.
That globally changes another player's world. Gujuo spawning also performs a
radius-global duplicate check before assigning an owner. Make the pole and
Gujuo phase player-private. Preserve the short-path sequence of San, Irvig and
Ranalph before Nezikchened and the long-path direct demon encounter. States
31-34 observed by Quest Helper should become durable subphase receipts rather
than compressing everything into state 32 plus overloaded bits.

Final enemies are now owner-bound, which is the right direction. Prove target
ownership, kill credit, timeout/full-health reset, prayer drain, holy-water
interaction, logout/death and simultaneous-player isolation. Gujuo currently
commits state 45 after granting or privately dropping the gilded totem; if that
ground object expires he has no replacement branch. Use an item-ownership
receipt and a transcript-accurate loss recovery path.

## 10. Hand-in, rewards and completion

Radimus accepts the completed map from inventory and the gilded totem from
inventory or a broad ground-object total. If the latter path is taken he calls
`obj_del` without a selected, verified owned object. Require both exact quest
items in a supported ownership domain, settle their deletion and hand-in
receipt atomically, then write state 50. State 50 correctly opens the guild
main doors before final quest completion so the player can train inside.

The severe reward defect is deterministic:

| Starting state | Current action | XP choices so far | Resulting state |
| ---: | --- | ---: | ---: |
| 50 | `radimus_training` | 1 | 55 |
| 55 | `radimus_training` | 2 | 60 |
| 60 | `radimus_training` | 3 | 65 |
| 65 | `radimus_training` | 4 | 70 |
| 70 | `radimus_final_training` calls the training proc again | **5** | 75 |

The actual award is therefore 150,000 XP although the completion scroll claims
120,000. State 70 must be the no-XP final conversation. Each of states
55/60/65/70 should be committed by an idempotent reward receipt in the same
protected settlement as its corresponding 30,000 XP choice. Cancellation before
a choice grants nothing; disconnect/crash after XP cannot grant it twice. After
four receipts, the extra talk calls `~quest_complete_rewards` once, awards the
four table-derived QP and writes the durable complete state.

The completion scroll currently names 120,000 XP, guild/cape, dragon square
shield and jungle access but omits jewellery charging, vine shortcut,
dark-dagger replacement/toggle and rare-table effects. Keep the UI concise but
make the actual unlock contract explicit and tested. Completion must reconcile
legacy saves at 50-70 without removing earned XP or awarding a fifth choice;
that likely needs an account migration decision because the main state alone
cannot prove whether older characters already received duplicated XP.

## 11. Journal, admin and recovery

The journal is extensive and already dispatched from the dynamic quest list.
It describes route states that have no writers, including polluted water,
lower-cave traversal, crystals, both dagger branches and final subphases. It
also uses base skill values in its requirement display although the skills are
boostable when used. Keep base levels for display/preparation, but do not turn
them into a start gate. Reconcile state 9, 21, 23, 24 and 31-34 with native
captures before changing constants, and add lost-item/recovery messaging for
every relevant checkpoint.

The POH quest-status adapter and journal dispatch correctly treat state 75 as
complete. Generic `::complete` sets `%legendsquest` directly to 75. It grants no
training XP, does not settle the four choices or items, and does not exercise
consumer unlocks. Retain it only as an explicit administrative state override,
or replace it with a fixture that separately declares whether rewards should be
settled. Add local debug commands for safe checkpoint setup, owned actor/loc
inspection, each Viyeldi choice and reward-receipt inspection; never use them as
end-to-end evidence.

At minimum, define replacement/recovery for notes and completed map,
bullroarer, binding book, sketch, every bowl/reed/vial form, seeds/germinated
seeds, bravery potion, rope, crystal pieces/heart forms, dark/glowing dagger,
Holy Force, Yommi totem and gilded totem. Predicates must distinguish inventory,
bank, equipment, player-private ground, active actor/loc phase and permanently
settled consumption. State must never advance after a failed add, and a ground
drop that expires must never strand the player.

## 12. Downstream consumer audit

| Consumer / unlock | Current evidence | Required work |
| --- | --- | --- |
| Legends' Guild outer/main access | outer gate allows state >0; main doors allow state >=50 | Add guard start; verify collision, both directions and pre/post-hand-in phases |
| Guild shops / cape | Fionella and Siegfried shop openers exist; cape stock is present | Gate actor access through valid guild phase and test buy/wear behavior |
| Dragon square shield | Smithing assembly correctly checks members, Smithing 60 and hammer without requiring Legends; no completion gate was found when wielding | Keep smithing ungated, require Legends plus Defence 60 on wield, and test both halves/shop access |
| Jewellery charging | Recharge Dragonstone says the quest is absent and upgrades bracelets/necklaces unconditionally; all items jump to six charges | Add Legends gate where canonical, implement +4 capped arithmetic, and audit guild/Fountain interaction ownership |
| Kharazi vine | cache feature advertises Agility 79 and quest row 85; teleport row exists; no runtime handler was found | Resolve row 85, add both-direction traversal and require completion/current Agility 79 |
| Dark dagger | no Radimus replacement or investigate/toggle branch after completion | Implement transcript/item-form behavior without duplicating dagger ownership |
| Bravery potion | shared Herblore recipe exists | Verify post-quest remaking and permanent access bit |
| Rare-drop table | mountain-troll helper checks completion; a shared random-jewel rung remains commented out while `~megararetable` is reachable elsewhere | Reconstruct every canonical caller and gate only the intended Legends rare-table rung |
| Holy water | shared ranged code recognizes Nezikchened and its quest temp var | Verify general demon behavior, charge/ammo settlement and quest encounter reset |
| Later quests/diaries/Nightmare Zone | no complete consumer set demonstrated by this root | Inventory RFD/DS2, Karamja diary, NMZ and every database quest prerequisite before Gate D |

Do not duplicate guild, smithing, magic, drop-table or agility policy inside the
quest root. Add narrow state adapters to their owning services and regression
test non-Legends callers.

## 13. Modernization work packages

Implement in dependency order; do not begin combat polish while the route is
still unreachable.

1. **Metadata and evidence:** re-pin Wiki pages, fix the five dbrow prerequisites
   and Quest Helper extractor association, capture native state/varbit behavior,
   and generate the complete trigger/consumer manifest.
2. **Start and surface traversal:** implement guards, start confirmation,
   cupboard/table recovery, dense-jungle cutting, three-part mapping, forester
   replacement and player-owned Gujuo.
3. **Shaman Cave obstacles:** add entrance/exit, crevice, lockpick gate, mining
   boulders, strength gate, marked wall and rune ordering, with boost/failure
   tests at every boundary.
4. **Binding book and water:** restore per-rock gems/light show/book recovery,
   native book UI, exact vial enchantment, sketch/anvil bowl creation, Prayer
   blessing, hollow reed, evaporation/pollution and fire-wall timing.
5. **First encounter:** phase Ungadulu per player and harden the recent owned
   Nezikchened implementation under timeout/death/logout/two-player tests.
6. **Lower-cave traversal:** implement bravery/magic gate, rope/winch/maplinks,
   ledges, warrior kill ownership, crystal fusion/activation/recess and barrier.
7. **Both moral branches:** implement Viyeldi hat/dialogue, dagger-on-Viyeldi,
   dagger-on-Ungadulu, Holy Force, Echned transformation, second fight, boulder
   and sacred water, including all loss/retry paths.
8. **Yommi and finale:** convert plot/pole/Gujuo phases to player-private state,
   verify the timed tree lifecycle, persist states 31-34 and harden final combat.
9. **Atomic completion:** repair hand-in, four reward receipts, state-70 final
   talk and completion lifecycle; design a legacy 50-70 migration policy.
10. **Consumers and verification:** correct guild/cape, shield, jewellery,
    shortcut, dagger, potion, rare-drop and modern downstream consumers, then
    run the full Gate D matrix with two real clients.

## 14. Gate D verification matrix

| Area | Required proof |
| --- | --- |
| Requirements | Every correct/missing prerequisite; 106/107 QP; each skill below/at/boosted; no skill start gate |
| Start | eligible/ineligible guards; accept/refuse/re-ask; full inventory; lost notes; both gate directions |
| Jungle | each dense-jungle side; axe/machete failures; three map zones/orders; paper/charcoal loss; two players |
| Gujuo | valid/invalid bullroarer zones; concurrent summons; logout/despawn; replacement and dialogue branches |
| Shaman Cave | every obstacle at below/at/boosted skill; broken lockpick; failure damage; both directions/re-entry |
| Rune/gem puzzle | correct and every wrong order; cave reset; every wrong rock; light show; book loss/repeat; full inventory |
| Bowl/water/fire | 2-6 bar smith failures; Prayer failure/drain; reed; evaporation; pollution; all-bowls germination; fire timing |
| First demon | all styles/prayers; Holy water; prayer drain; timeout/full-health reset; death/logout; second player present |
| Lower caves | four Charge Orb spells; rune/orb consumption; repeat casts; rope/potion one-time state; every fall/return |
| Crystals | each warrior/drop/full inventory; duplicates/loss; furnace; eye; recess; barrier and re-entry |
| Choice/second demon | short and long paths; Viyeldi spared/killed; dagger/card loss; Echned answers; timeout/death/logout |
| Yommi | all six plots; all growth/rot timings; axes; boost expiry; inventory full; teleport/logout/restart; two players one plot |
| Finale | both path-specific enemy orders; every 31-35 retry; pole isolation; gilded-totem loss/replacement |
| Hand-in/rewards | missing either item; bank/ground variants; cancellation at every choice; same skill x4; crash after XP; no fifth XP |
| Completion | one completion scroll/jingle; exactly 4 QP and 120,000 XP; replay and `::complete` policy |
| Unlocks | guild/cape, shield make/wield, jewellery +4/max, vine 79, dagger toggle, potion, rare table, diaries/later quests/NMZ |
| Regression | Jungle Potion, One Small Favour, generic doors, all Charge Orb spells, Smithing, Fountain of Rune, drop tables |

No state may be declared verified from an admin-injected checkpoint. Record
server assertions, inventory/stat deltas, actor/loc ownership and paired-client
captures for every multiplayer-sensitive scene.

## 15. Exit criteria

Legends' Quest can leave `partial / blocked organic route` only when:

- the corrected dbrow, Wiki revisions, transition manifest and downstream
  consumer list are checked in and the extractor resolves `quest_legends`;
- a normal eligible player can start at the guards, complete every physical
  route and puzzle, choose either Viyeldi path and recover every mandatory item;
- all summoned actors, changing trees, fire walls and totem phases are isolated
  correctly between simultaneous players;
- every boostable skill, failure, timeout, death, logout, teleport and
  inventory-full boundary has a deterministic recovery outcome;
- Radimus grants exactly four 30,000 XP choices, the fifth conversation grants
  none, and shared completion awards exactly four QP once;
- every named unlock and post-quest consumer is implemented at its owning
  service and regression-tested; and
- clean builds, static checks, scripted state/inventory tests and two-client
  end-to-end captures pass both the short and long paths from state 0 to 75.
