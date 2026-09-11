# Contact! modernization audit

Status: `audit-pending` — the native quest row, packed progress varbit, most
principal actors, item configs, journal dispatch, requirement procedure, and a
recognisable 0–130 story outline exist. The production route is not completable.
The first dungeon shortcut teleports to a duplicate high-Y chasm containing
Maisa but not Kaleef's searchable body, so the parchment required to advance
from state 40 cannot be obtained. If that state is bypassed, the Giant Scarab
is a shared world spawn and its death writes state 100, while the native cave
Osman only becomes visible at state 110 and is not spawned anywhere. Completion
also grants the wrong lamp item. The labyrinth, hazards, two major cutscenes,
and instanced multi-style boss encounter are prose substitutions or absent.
This is a broken legacy outline, not a modern quest implementation.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to the full route, native primary and side-state,
shared High Priest and Osman ownership, both dungeon maps, light and trap
mechanics, parchment and Keris lifecycle, the Giant Scarab instance, rewards,
shops/bank/music/Nightmare Zone unlocks, the Beneath Cursed Sands prerequisite,
journal, debug adapters, and reconnect/death behavior. It is an implementation
specification, not verification evidence.

## 1. Authoritative references

The Wiki article and guide define mechanics, requirements, rewards, and the
current route. The transcript defines dialogue, cutscenes, re-talks, and item
recovery. Revisions were resolved through the OSRS Wiki API on 2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Contact!](https://oldschool.runescape.wiki/w/Contact%21?oldid=15292391) | 15292391, 2026-08-11 | Identity, requirements, full route, encounter, rewards, and unlocks |
| [Contact!/Quick guide](https://oldschool.runescape.wiki/w/Contact%21/Quick_guide?oldid=15233716) | 15233716, 2026-06-14 | Required items, exact route, answers, labyrinth, fight, and completion |
| [Transcript:Contact!](https://oldschool.runescape.wiki/w/Transcript%3AContact%21?oldid=15263370) | 15263370, 2026-07-14 | Start/refusal/re-talks, Coenus scene, persuasion, cutscenes, and finale |
| [High Priest](https://oldschool.runescape.wiki/w/High_Priest?oldid=14985480) | 14985480, 2025-09-13 | Start/completion actor and shared quest ownership |
| [Coenus](https://oldschool.runescape.wiki/w/Coenus?oldid=15239638) | 15239638, 2026-06-25 | Menaphos arrow cutscene at the start |
| [Jex](https://oldschool.runescape.wiki/w/Jex?oldid=14768495) | 14768495, 2024-10-13 | Cellar access and dungeon warning |
| [Kaleef](https://oldschool.runescape.wiki/w/Kaleef?oldid=15282209) | 15282209, 2026-07-30 | Body/search and Keris ownership |
| [Parchment](https://oldschool.runescape.wiki/w/Parchment?oldid=15185408) | 15185408, 2026-04-22 | Read, destroy, and non-replacement lifecycle |
| [Maisa](https://oldschool.runescape.wiki/w/Maisa?oldid=15196404) | 15196404, 2026-04-25 | Identity questions, chasm scenes, and sequel reuse |
| [Osman](https://oldschool.runescape.wiki/w/Osman?oldid=15279899) | 15279899, 2026-07-29 | Persuasion, desert/chasm scenes, and Keris recovery |
| [Sophanem Dungeon](https://oldschool.runescape.wiki/w/Sophanem_Dungeon?oldid=15257158) | 15257158, 2026-07-08 | Two-level topology, traps, light behavior, and creatures |
| [Giant Scarab](https://oldschool.runescape.wiki/w/Giant_Scarab?oldid=15199518) | 15199518, 2026-04-28 | Level 191 instanced boss, attacks, poison, summons, and cannon restriction |
| [Scarab mage](https://oldschool.runescape.wiki/w/Scarab_mage?oldid=15281960) | 15281960, 2026-07-29 | Magic minion behavior |
| [Locust rider](https://oldschool.runescape.wiki/w/Locust_rider?oldid=15281959) | 15281959, 2026-07-29 | Melee/ranged minion behavior |
| [Scarab swarm](https://oldschool.runescape.wiki/w/Scarab_swarm?oldid=15236838) | 15236838, 2026-06-21 | Sand-pit and dungeon hazard behavior |
| [Keris](https://oldschool.runescape.wiki/w/Keris?oldid=15254686) | 15254686, 2026-07-05 | Requirements, special damage, poison variants, loss, and replacement |
| [Combat lamp](https://oldschool.runescape.wiki/w/Combat_lamp?oldid=15185818) | 15185818, 2026-04-22 | One lamp, two 7,000-XP wishes, banking, destruction, and reclaim |
| [Sophanem](https://oldschool.runescape.wiki/w/Sophanem?oldid=15280165) | 15280165, 2026-07-29 | Post-quest shop access and city world state |
| [Sophanem bank](https://oldschool.runescape.wiki/w/Sophanem_bank?oldid=15211878) | 15211878, 2026-05-17 | Completion-gated bank access |
| [Beneath Cursed Sands](https://oldschool.runescape.wiki/w/Beneath_Cursed_Sands?oldid=15297310) | 15297310, 2026-08-13 | Downstream prerequisite and reused Sophanem cast |
| [Nightmare Zone](https://oldschool.runescape.wiki/w/Nightmare_Zone?oldid=15271187) | 15271187, 2026-07-22 | Giant Scarab post-quest unlock |

The current contract is a members, experienced, short quest released 10
January 2007 and the third Desert-series quest. It starts with the High Priest
in Sophanem. Direct prerequisites are Prince Ali Rescue and Icthlarin's Little
Helper; Gertrude's Cat is therefore an indirect prerequisite. There are no
required skill levels. The Wiki recommends 70 combat, 50 Agility, 50 Thieving,
40 or 43 Prayer, antipoison, food, and prayer restoration. A light source and
tinderbox are required unless the chosen light source cannot be extinguished.

Completion awards 1 quest point, 7,000 Thieving XP, and one bankable Combat
lamp with two choices of 7,000 XP in Attack, Strength, Defence, Hitpoints,
Ranged, or Magic. The player obtains Kaleef's Keris during the boss sequence.
Completion unlocks the Sophanem bank and shops, the music tracks Back to Life
and The Spymaster, and the Giant Scarab in Nightmare Zone. Contact! is required
to start Beneath Cursed Sands.

Transition aid only: the local Quest Helper checkout's Contact! implementation
at commit [`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/contact)
confirms the native 0/10/20/30/40/50/60/70/80/90/100/110/120 state ladder,
zones, objects, NPCs, one progress varbit, and route coordinates.
`python3 tools/questhelper_extract.py contact --check` exits 0 and resolves the
quest row, four item symbols, seven NPC symbols, four loc symbols, one varbit,
and guide coordinates. Quest Helper cannot prove server travel, map placement,
instance ownership, combat deaths, reward transactions, or unlocks.

## 2. Native quest identity and state contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 124 |
| Type / series | Members quest; Desert series #3 |
| Difficulty / length | Cache 2 / 1; Wiki experienced / short |
| Start | High Priest of Icthlarin (`ics_little_hipriest_vis`) in Sophanem |
| Direct prerequisites | Prince Ali Rescue; Icthlarin's Little Helper |
| Indirect prerequisite | Gertrude's Cat |
| Required skills | None |
| Recommended | 70 combat; 50 Agility; 50 Thieving |
| Required equipment | Light source and tinderbox, unless the source is inextinguishable |
| Primary state | `%contact`, bits 0–7 of transmitted `%contact_master` |
| Quest points | 1 |
| Completion XP | 7,000 Thieving; two 7,000-XP combat wishes from one lamp |
| Item reward | Keris, acquired after the Giant Scarab |
| Permanent unlocks | Sophanem bank/shops, two music tracks, Giant Scarab in Nightmare Zone |
| Downstream | Beneath Cursed Sands |
| End state | 130 |

The dbrow's `requirement_quests` values resolve to Mountain Daughter and Royal
Trouble, not Contact!'s actual prerequisites. The production start procedure
does explicitly check Prince Ali Rescue and Icthlarin's Little Helper, so the
live start gate is better than its metadata. Modernization must correct the row
too: quest-list presentation, recommendation/dependency tooling, `::complete`,
and future generated invariants must not retain false prerequisites.

### Primary state inventory

| State | Canonical/native phase | Current implementation |
| ---: | --- | --- |
| 0 | Not started; High Priest introduction | Requirements checked; multiple route discussions and the Coenus scene are compressed away |
| 10 | Start dialogue / Menaphos attempt | Constant absent; never read or written |
| 20 | Resume/finish introductory sequence | Constant absent; never read or written |
| 30 | Speak to Jex | Written immediately on accepting the quest |
| 40 | Enter and investigate the dungeon | Jex writes this and prematurely enables the market visibility bit |
| 50 | Read Kaleef's parchment; find Maisa | Handler exists, but body and Maisa are on different maps in the production route |
| 60 | Convince Osman in Al Kharid | Two persisted questions lead here |
| 70 | Meet Osman north of Sophanem | Palace persuasion writes this |
| 80 | Return to the dungeon / encounter setup | Desert Osman writes this; the global boss is spawned on ladder use |
| 90 | Giant Scarab encounter phase | Constant absent; never read or written |
| 100 | Giant Scarab encounter/death sequence | Global NPC death writes this directly |
| 110 | Speak to Osman in the chasm | Constant absent; native cave Osman is visible only here and has no spawn |
| 120 | Return to High Priest | Cave handler intends to write this, but is unreachable |
| 130 | Complete | Shared completion writes state before awarding XP/items/quest points |

States 10, 20, 90, and 110 are not arbitrary padding. The High Priest owns the
first three native thresholds, Quest Helper groups 80/90/100 into the encounter,
and `contact_osman_cave_instance` resolves from its native multi-NPC only at
state 110. The current 80→100→120 shortcut is incompatible with that cache
contract even before accounting for the absent cave spawn.

### Native side-state inventory

| Varbit | Native shape | Current use and mismatch |
| --- | --- | --- |
| `%contact_people_vis` | 1 bit | Set by Jex at state 40; also makes Sophanem market actors/guards visible, unlocking completion content early |
| `%contact_bankers_vis` | 1 bit | Never written; no coherent completion-time bank world-state transition |
| `%contact_gotscarabs` | 1 bit | Never used; encounter ownership/state intent unimplemented |
| `%contact_maisa_ans` | 2 bits | Persists the first identity answer; wrong choices print the correct answer anyway |
| `%contact_been_downstairs` | 1 bit | Set only by the first shortcut; no gameplay behavior depends on it |
| `%contact_osman_vis` | 2 bits | Never used despite multiple Osman stages |
| `%contact_got_mage` / `%contact_got_lance` / `%contact_got_bow` | 1 bit each | Never used; the multi-style encounter/minion tracking is absent |
| `%contact_maisa_invis` | 1 bit | Never used; the static duplicate-map Maisa is not retired for later scenes |
| `%contact_finished_cutscene` | 1 bit | Never used; intro and encounter cutscene resume/cleanup are absent |
| `%contact_never_had_keris` | 1 bit | Written by unreachable cave Osman, never consumed by a recovery service |
| `%contact_used_reward_lamp` | 2 bits | Never used; the correct two-wish lamp lifecycle is absent |
| `%contact_discussed_menaphos` | 1 bit | Set on acceptance but not used to resume the compressed introduction |
| `%contact_found_kaleef` | 1 bit | Blocks replacement permanently even if parchment is lost before reading |
| `%contact_met_maisa` | 1 bit | Duplicates progress state and is only written |
| `%contact_osman_told` / `%contact_osman_met` | 1 bit each | Written, but primary state drives dialogue/visibility |
| `%contact_told_priest` | 1 bit | Written, not read |
| `%contact_met_baker` | 1 bit | Never used; market dialogue/unlock detail is absent |

Bit 31 of the same carrier is `%met_zahur` and belongs to other content. It
must remain untouched by Contact! reset, completion, and migration code.

## 3. Implementation surface

The quest root contains 555 lines in two configs and nine scripts.

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `configs/contact.constant` | Authored states, rewards, and three coordinates | Omits four native states and contains the false premise that a duplicate high-Y chasm supersedes the actual map |
| `configs/contact.varp` | Transmitted permanent `contact_master` carrier | Appropriate native carrier declaration |
| `scripts/contact_priest.rs2` | Start, reminders, finale, post-quest | Requirements correct; introduction compressed; shared dispatcher fallthrough and post-complete routing defective |
| `scripts/contact_jex.rs2` | Jex and temple trapdoor | Warns about light/traps but enforces neither; sets market visibility far too early |
| `scripts/contact_dungeon.rs2` | First ladder, Kaleef body, parchment | Replaces dungeon with teleport; wrong map; destructive read and unrecoverable loss state |
| `scripts/contact_maisa.rs2` | Identity questions | Basic persisted flow exists; wrong answers echo correct text and actor is on disconnected map |
| `scripts/contact_osman.rs2` | Palace, desert, and cave Osman | Persuasion cannot be exited; cutscene absent; cave state and spawn incompatible |
| `scripts/contact_scarab.rs2` | Boss spawn, retaliate operation, death | Shared global NPC/drop; no instance, minions, poison/light mechanics, or cleanup |
| `scripts/contact_shared.rs2` | Requirement and completion procedures | Correct start prerequisites; wrong lamp, partial inventory grant, and non-idempotent commit order |
| `scripts/contact_journal.rs2` | State journal | Broad summary; cannot represent missing native states or item/lifecycle recovery |
| `scripts/contact_debug.rs2` | Reset and `::contactrun` | State-forcing success path hides all blockers and incompletely resets native side-state |

Mandatory cross-directory surfaces include:

| Surface | Relationship / modernization requirement |
| --- | --- |
| `configs/all.dbrow` | Correct false prerequisite rows while preserving ID 124, end 130, 1 QP, recommendations, and 7,000 Thieving XP |
| `configs/all.varbit` | Preserve the native 32-bit layout and use every gameplay-relevant bit intentionally |
| `configs/all.obj` | Parchment, four Keris variants, and the correct `contact_lantern` resolve with native operations |
| `configs/all.npc` | High Priest, Jex, three Osman forms, Maisa, boss, and minions; cave Osman is state-110 gated |
| `configs/all.loc` | Trapdoor, ladders, Kaleef body, traps, shops, bank, and dungeon transforms |
| `areas/world/configs/m51_43.spawn` | High Priest, Jex, market multis, guards, and Sophanem surface actors |
| `areas/world/configs/m43_80.spawn` | Bank-cellar scenery and actors |
| `areas/world/configs/m35_67.spawn` | The placed searchable Kaleef body in the real low-Y chasm |
| `areas/world/configs/m50_144.spawn` | Static high-Y Maisa and encounter creatures used by the broken shortcut |
| `quest_beneathcursedsands` | Owns the shared High Priest operation and must enforce Contact! at its real start |
| `quest_icthlarinslittlehelper` | Fallthrough owner for ordinary High Priest dialogue |
| market/shop scripts | Post-quest Sophanem merchant access must not be exposed at state 40 |
| bank scripts/locs | Bank access must be completion-gated and proven from both sides of the entrance |
| combat/poison/light systems | Giant Scarab/Keris effects and dungeon light extinction must use general modern systems |
| Nightmare Zone and music unlocks | Completion must expose Giant Scarab and the two reward tracks |
| `quests/scripts/questpoints.rs2` | Shared modern completion scroll, points, count, and jingle lifecycle |
| `quests/scripts/quest_cheat.rs2` | End-state adapter only; must set coherent permanent unlock state and be idempotent |

No production handler exists for `contact_lantern`. No production reference to
`contact_keris` implements its Scabarite/Kalphite damage effect or replacement
service; only generic weapon animation/sound config and this quest's boss/drop
references were found. No separate Contact! implementation supplies the
labyrinth, instance, minions, Nightmare Zone unlock, or music transitions.

## 4. Reachability and canonical route audit

| Phase | Canonical behavior | Current behavior / consequence |
| --- | --- | --- |
| High Priest start | Discuss Menaphos approaches, attempt negotiation, Coenus fires arrows, resume dialogue and accept/refuse | One two-choice dialogue and one message jump 0→30; states 10/20 and the arrow scene are absent |
| Jex / supplies | Learn of tunnels; prepare light/tinderbox; a nearby guard can sell a tinderbox and unlit torch | Jex only warns; guard supply dialogue and actual equipment enforcement are absent |
| Temple and bank | Open cellar, enter bank, descend into the real dungeon | Trapdoor teleports to bank; next ladder teleports straight to a different chasm map |
| First labyrinth | Traverse two levels; avoid floor/wall traps, crushers, pits, bad ladders, scarabs, darkness, and monsters | One message claims all traps were disarmed. No movement, failure, damage, light extinction, tinderbox use, poison, or enemies |
| Kaleef and parchment | Search body, read retained parchment, optionally destroy it; no replacement after destruction | Searchable body is only in the low-Y chasm while player is sent high-Y. Reading deletes it. Dropping/destroying before reading leaves `%contact_found_kaleef=1` and state 40 forever. **Hard blocker.** |
| Maisa questions | Find nearby Maisa and answer Prince Ali questions, with valid wrong/unknown dialogue | Maisa is in the high-Y duplicate where the body is absent. Wrong choices print “Draynor Village” or “Leela” regardless of selection; “I don't know” is absent |
| Palace Osman | Introduce the quest, choose the compelling political argument, or leave/give up and retry | Four-choice loop repeats until the correct answer, with no exit branch |
| Desert Osman | Meet outside Sophanem; Osman knocks out Jex and enters in a staged scene | Two-choice dialogue plus a message writes state 80; Jex, movement, animation, visibility, and resume state are absent |
| Second labyrinth | Re-enter the same dangerous route and reach a private Osman/Maisa scene | Same teleport bypass; no scene or instance setup |
| Giant Scarab | Fight level 191 boss in an instance; manage melee/ranged attacks, poison, extinguished light, and spawning mage/melee/ranged minions; no cannon | One global fixed-coordinate NPC is added for 1,000 ticks. It uses generic combat only, with no encounter rules, isolation, re-entry, or lifecycle cleanup |
| Boss death / Keris | Remaining minions die; Osman appears; Keris is available on the ground and can later be claimed if missed | NPC death writes the nearby hero's var and creates a shared fixed-coordinate world drop. Another player can share credit or take the dagger. State goes to 100, not native Osman state 110 |
| Chasm Osman | Speak at state 110, conclude the plan, recover a missed Keris, and return to priest | NPC multi appears only at 110, current code expects 100, and no NPC spawn/add exists. **Hard blocker.** |
| Completion | High Priest completes once, awards all rewards, and exposes permanent unlocks | Correct Thieving XP call exists, but wrong item/quantity is granted; completion state commits first and unlock bits/services are not established |

The first blocker is deterministic without a client capture. The exact
`contact_ladder_barricaded` handler teleports to `(3218, 9246)`. World/map
audit finds Maisa there, but `contact_dead_body_kaleef_vis` is placed once in
the real low-Y chasm near `(2284, 4315)`. The body interaction cannot occur on
the route's destination. Moving only the body would not fix the implementation:
the second journey, dungeon topology, instancing, and native state ownership
would still be wrong.

## 5. Dungeon, encounter, and lifecycle oversights

### 5.1 The labyrinth is gameplay, not scenery

The canonical dungeon is a repeatable, dangerous traversal. Floor and wall
traps deal percentage-based damage; crushers deal damage; sand pits can spawn
scarab swarms; scarab traps can extinguish ordinary light and drop the player
to the lower floor; incorrect ladders lead to dangerous lower areas; darkness
and hostile creatures remain relevant. The second trip is not permission to
skip those systems.

Modernization must bind every required loc operation to native placed scenery,
use shared light/extinguish and poison behavior, validate tinderbox recovery,
implement bidirectional ladders and failure destinations, and test each hazard.
Dungeon traversal must remain possible after relog and after inventory changes.
An inextinguishable light source must follow current shared light policy rather
than a Contact!-specific exception.

### 5.2 Parchment ownership is internally contradictory

The Wiki contract intentionally allows the parchment to become permanently
unobtainable after the player reads and destroys it, because reading already
advanced the quest. The current script instead destroys it during Read. Worse,
the search sets `%contact_found_kaleef` before reading; a drop or generic
Destroy at state 40 prevents another copy while leaving progress unchanged.

Make body search a transaction, preserve the item on Read, advance exactly
once, and permit a replacement only while it has not been successfully read.
Define duplicate prevention across inventory, bank, worn/container storage, and
ground ownership. Destroy confirmation should explain the irreversible result
only after state 50.

### 5.3 The boss must be a player-owned instance

`npc_find` around one global coordinate is not encounter ownership. A nearby
player can suppress a spawn, join the fight, receive the hero-based state
write, or collect the globally added Keris. A 1,000-tick lifetime is not a
recovery policy. Logout, death, region change, and re-entry have no cleanup or
reset contract.

Build the encounter with the repository's current player-instance and queue
ownership rules. The instance must stage Osman/Maisa, transition through native
states 80/90/100, spawn and cycle the appropriate mage and locust-rider styles,
remove remaining summons on boss death, enforce cannon restrictions, apply the
boss's special poison/light behavior, and advance only the owning player to
110. Queue subjects and protected contexts must be explicit. On logout, death,
or abandonment, remove owned NPCs/locs and make re-entry deterministic.

### 5.4 Keris needs combat and recovery services

The cache supplies ordinary and three poisoned Keris variants, but ordinary
weapon stats alone do not implement the weapon. The current OSRS behavior
requires 50 Attack, gives a damage bonus against Kalphites and Scabarites, and
has a 1-in-51 chance to deal triple damage against those targets. It can be
poisoned and participates in a hard Desert Diary task.

Implement the effect as shared combat classification/weapon behavior, not in
the Contact! dialogue. Boss-ground ownership must prevent theft. If the player
does not take it, Osman must provide the intended later claim. Lost/destroyed
replacement belongs to Osman before Beneath Cursed Sands and Selim afterwards,
with the current OSRS fees; Perdu's separate fee/service must stay coherent.
Use `%contact_never_had_keris` only if it represents a verified native branch.

## 6. Dialogue, shared actors, and downstream integration

### 6.1 High Priest dispatch can fall through into the wrong quest

Beneath Cursed Sands owns `[opnpc1,ics_little_hipriest_vis]` and calls
`@contact_priest_talk` while Icthlarin's Little Helper is complete and Contact!
is not. It then unconditionally calls `@ics_hipriest_talk`. Most Contact!
branches return, but the acceptance path ends after writing state 30 without a
`return`, so control can continue into Icthlarin's Little Helper dialogue.

The dispatcher also calls Contact! only while `%contact < 130`. Therefore the
post-completion branch inside `contact_priest_talk` is unreachable through the
real Talk-to operation; completed players fall to the older quest's dialogue.
Define one explicit shared-actor router with exactly one owner per state and a
return after every delegation. Preserve Beneath Cursed Sands priority when it
is actually active, then Contact! post-quest/reclaim services, then
Icthlarin's Little Helper fallback.

### 6.2 Beneath Cursed Sands does not enforce Contact!

The downstream script starts from Jamila at `%bcs = 0` with a two-choice menu.
It checks no quest or skill requirements; its header explicitly defers hard
gates. Contact! is a required quest, so a player can currently start Beneath
Cursed Sands without completing it. Add the prerequisite at the real start
transaction and to any alternate start path. Test shared High Priest, Maisa,
Osman, and market actors at every overlap state so one quest cannot steal or
strand the other's operation.

### 6.3 Dialogue branches need transcript parity

Restore the staged High Priest/Coenus sequence with cancel/relog-safe state
10/20 transitions, Jex and supply-guard branches, Maisa's actual selected wrong
answers and “I don't know” option, Osman's give-up path, desert and chasm
cutscene re-talks, post-quest High Priest dialogue, and recovery conversations.
Choice menus must never trap the player in an unbounded loop and every state
change must occur after its required dialogue/action succeeds.

## 7. Rewards and permanent unlocks

### 7.1 The implementation grants the wrong lamp

The correct cache item already exists: `contact_lantern`, named “Combat lamp,”
with Rub and Destroy operations. OSRS grants one of these, and it provides two
separate 7,000-XP wishes. The script instead grants up to two
`thosf_reward_lamp` items, describes them as a Combat lamp, and uses that wrong
item on the reward scroll. No Rub/redeem handler was found for either the
intended Contact! item or the generic item in this route.

Implement one bankable `contact_lantern` with two uses recorded by the native
two-bit `%contact_used_reward_lamp`. Each Rub must show the modern combat-skill
selection, validate the selection, award 70,000 engine XP units to exactly one
eligible combat stat, advance usage atomically, and consume the item only after
the second successful award. Cancel, repeated operations, logout, and double
resume must not award XP. If lost before all XP is claimed, the High Priest
must provide the Wiki-defined reclaim without duplicating completed wishes.

### 7.2 Completion is not atomic or idempotent

`~contact_quest_complete` first writes 130, then awards Thieving XP, conditionally
adds zero, one, or two wrong lamps based on free slots, and finally calls the
shared completion procedure. Full inventory therefore silently loses all or
part of the reward while progress is committed. Direct or debug re-entry can
repeat XP, items, quest points, and completed-count work because the procedure
has no guard.

Stage all preconditions before committing. The reward must go to inventory or
a documented bank/reclaim path, never vanish. Commit state, 7,000 Thieving XP,
one lamp entitlement, quest point/count, unlock flags, music, and Nightmare Zone
eligibility exactly once. The reward scroll must name the Keris, the singular
two-use lamp, bank/shops, music, and Nightmare Zone accurately.

### 7.3 Shop and bank state is early, missing, or unproved

`%contact_people_vis=1` is written when Jex first opens the cellar. Native NPC
multis use that bit for Sophanem merchants and guards, so current production
exposes completion-reward actors at state 40. Conversely, completion never
writes `%contact_bankers_vis`, and no Contact!-owned production write was found
for it. Bank booths use shared bank behavior if reached, but the entrance and
world-state contract is not coherently completion-gated.

Move permanent visibility/access to the one-time completion transaction or to
a proven native cutscene threshold. Audit every surface merchant, stall, guard,
banker, booth, ladder, and door before and after completion. `::complete` must
establish the same permanent world state as organic completion without granting
inventory rewards twice.

## 8. Journal, debug, and recovery audit

The journal recognizes only the authored 0/30/40/50/60/70/80/100/120/130
ladder. It cannot guide interrupted start cutscenes, the real 80/90/100
encounter sequence, or state 110. It claims the player can search nearby after
the wrong-map teleport, cannot distinguish a lost unread parchment, and offers
no lamp/Keris recovery. Modernize it against the native state table and item
entitlements, including reconnect and inventory-full states.

`::contactrun` writes every progress flag directly, adds a Keris without a
fight, skips both maps/cutscenes, and invokes completion. It proves only that
assignments and a procedure call compile. It is not evidence that any required
NPC, loc, item, combat, or reward operation is reachable.

`~contact_debug_reset` clears only a subset of the carrier. It leaves banker,
scarab, Osman visibility, three combat-style, Maisa visibility, cutscene, lamp
usage, and baker bits behind. It deletes every Keris from inventory without
distinguishing legitimate post-quest ownership and does not delete/reconcile
the wrong or correct lamp. Replace it with a generated/test-only state adapter
that preserves unrelated bit 31, clears owned instance state safely, and is not
used as gameplay evidence.

## 9. Prioritized defect ledger

| Priority | Defect | Player impact | Required closure evidence |
| --- | --- | --- | --- |
| P0 | Wrong-map first ladder: Maisa high-Y, Kaleef body low-Y | Organic route blocks at state 40 | Real-client traversal reaches both actors on one canonical route |
| P0 | Cave Osman expects 100, native multi requires 110, and actor is never spawned | Route blocks after boss | Owned instance advances to 110 and exposes Osman after death/relog rules |
| P0 | Global boss, hero-based credit, and shared Keris ground drop | Cross-player credit/theft and unsafe lifecycle | Two-player isolation, death, logout, re-entry, and ownership tests |
| P0 | Wrong lamp item/quantity with no redemption or reclaim | 14,000 combat XP reward unavailable | Two-use selection, cancel, double-op, loss/reclaim, and inventory-full tests |
| P1 | Entire two-level labyrinth and hazards replaced by teleport prose | Core gameplay absent | Loc-by-loc traversal/failure/light/poison tests |
| P1 | Parchment deletes on read and can strand state if lost first | Permanent state-40 lock | Read/retain, pre-read loss replacement, post-read destroy tests |
| P1 | Native 10/20/90/110 states and both staged cutscenes absent | Cache multis/resume behavior inconsistent | Transition and reconnect coverage at every state |
| P1 | Shared High Priest falls through; Contact! post-complete branch unreachable | Wrong quest dialogue and no recovery service | Actor routing matrix across ICS/Contact!/BCS states |
| P1 | Beneath Cursed Sands start omits Contact! prerequisite | Downstream quest can start illegally | Start refusal/acceptance tests with prerequisite boundaries |
| P1 | Shops enabled at Jex; bank completion flag never written | Rewards available early or missing | Pre/post completion actor/door/booth world-state tests |
| P1 | Keris special damage and recovery services absent | Reward weapon incomplete; loss permanent | Combat probability/classification and replacement tests |
| P1 | Completion commits before rewards and is replayable | Lost or duplicated XP/items/QP | Atomic inventory-full and double-completion tests |
| P2 | Maisa wrong responses echo correct answers; Osman loop has no exit | Dialogue inaccurate or trapping | Transcript branch tests |
| P2 | Guard supply dialogue, post-quest dialogue, music, and NMZ hooks absent | Missing convenience/narrative/unlock behavior | Operation and entitlement assertions |
| P2 | Journal/debug adapters omit native side-state | Misleading recovery and false test confidence | State/item journal snapshots; complete/reset invariants |

## 10. Modernization work packages

1. **Correct native contract and actor routing.** Add states 10/20/90/110,
   correct dbrow prerequisites, preserve packed bits, and replace shared High
   Priest fallthrough with explicit ownership. Add the Beneath Cursed Sands
   start gate.
2. **Restore introduction and dialogue.** Implement Coenus and desert/chasm
   cutscenes with current player/NPC queues, transcript branches, resume flags,
   re-talks, and supply-guard service.
3. **Restore dungeon topology.** Bind native trapdoor, bank, ladders, traps,
   lower floor, chasm, darkness/light, damage, poison, swarms, and failure
   paths. Remove both coordinate shortcuts.
4. **Repair item lifecycle.** Make parchment acquisition/read/destruction
   transactional; implement Keris ground ownership, special combat behavior,
   and Osman/Selim/Perdu recovery policy.
5. **Build the encounter instance.** Stage Osman/Maisa, boss, all minion styles,
   poison/light mechanics, cannon restriction, native state transitions,
   cutscene, cleanup, death, logout, reconnect, and deterministic re-entry.
6. **Make completion atomic.** Grant 7,000 Thieving XP and one two-use Combat
   lamp exactly once; implement lamp UI/state/reclaim; unlock city services,
   tracks, Nightmare Zone, and accurate reward text.
7. **Modernize journal and test adapters.** Cover every native state and
   recovery condition; make `::complete` idempotent and coherent; remove debug
   state forcing from acceptance evidence.
8. **Verify Gates A–D.** Run static/config/pack checks, transition tests,
   multi-player isolation, lifecycle cases, and real-client route captures.

RuneScript/config should own quest policy. Add C only if a general reusable
instance, light, combat-classification, or modal capability is genuinely absent;
do not add Contact!-specific engine routing.

## 11. Verification matrix

| Area | Required cases |
| --- | --- |
| Requirements | Both direct prerequisites independently missing; both present; dbrow/dependency tooling agrees |
| Start | Accept, refuse, re-talk at 0/10/20, Coenus cutscene cancel/relog/resume, shared priest ownership |
| Jex/supplies | Prepare branch, guard purchase with insufficient/full inventory, trapdoor before/after permission |
| Dungeon | Every trap success/failure, both floors, wrong ladders, damage floor, light extinction/re-light, inextinguishable source, poison, death, relog |
| Parchment | Full inventory, duplicate prevention, pre-read loss and replacement, read retains, repeated read, post-read destroy |
| Maisa/Osman | Every answer and give-up branch, persisted first answer, re-talks, desert scene interruption/recovery |
| Instance | Two simultaneous players, all boss/minion styles, poison despite protection behavior, light effects, no cannon, boss/minion death order, ownership |
| Encounter lifecycle | Logout before/during/after boss, player death, region leave, timer expiry, reconnect, deterministic re-entry, cleanup |
| Keris | Owned drop, missed claim, loss/destruction, Osman/Selim/Perdu boundaries, poison variants, 50 Attack, target classification, special proc |
| Lamp | Full inventory, bankability, two different/same skill wishes, cancel, double click, logout between wishes, loss/reclaim before/after each use |
| Completion | Exact XP/QP/items/text, repeated High Priest operation, double queue resume, `::complete` twice, no duplicate rewards |
| Unlocks | Every shop/guard/bank path before and after completion, music tracks, Nightmare Zone eligibility |
| Downstream | Beneath Cursed Sands cannot start before 130 and can start after; shared actors route correctly in every overlap |
| Journal | Snapshot at 0/10/20/30/40/50/60/70/80/90/100/110/120/130 plus lost-item recovery states |

Required commands/evidence before `verified-modern`:

```text
python3 tools/questhelper_extract.py contact --check
make -C src torirsserver-scripts
ToriRSServer_Pack --check-only <intended-cache arguments>
quest-specific static/state/ownership tests
real-client start-to-reward smoke with route, instance, and lamp captures
two-player encounter isolation smoke
```

The Quest Helper check already passes for the untouched audit baseline. No
script build, pack check, automated transition test, or real-client smoke was
run for this documentation-only audit, and none may be inferred from
`::contactrun`.

## 12. Exit criteria

Contact! may move from `audit-pending` to `verified-modern` only when:

- the real High Priest start reaches both Kaleef and Maisa through the native
  dungeon without debug commands or coordinate shortcuts;
- states 10/20/90/110 have tested ownership and resume behavior;
- the boss is player-isolated and its full encounter, death, cleanup, Keris,
  logout, death, and re-entry contracts pass;
- parchment, Keris, and Combat lamp loss/replacement transactions cannot strand
  progress or duplicate value;
- completion awards exactly 1 QP, 7,000 Thieving XP, and two 7,000 combat-XP
  wishes from one `contact_lantern`, once;
- shops, bank, music, Nightmare Zone, and Beneath Cursed Sands gates agree with
  completion state;
- shared High Priest/Maisa/Osman/market operations have one explicit owner for
  every Icthlarin's Little Helper, Contact!, and Beneath Cursed Sands overlap;
- journal, organic completion, and `::complete` agree at state 130 and repeated
  completion is a no-op; and
- Gate D static, pack, automated, multi-player, and real-client evidence is
  recorded here with any remaining non-critical deviation precisely bounded.
