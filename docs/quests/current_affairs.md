# Current Affairs modernization audit

Status: `audit-pending` — the native quest metadata, progress carrier, form
answer fields, Catherby office locs, items, modern dialogue helpers, journal
dispatch, cheat arm, and shared completion call exist. The organic route is not
playable: Councillor Catherine has no world spawn and no script creates her, so
the player stops immediately after accepting the quest. Most remaining
mechanics are explicit soft-skips: the script does not enforce prerequisites,
replaces the eight-answer form and matching audit with fixed values, bypasses
the Sailing/current-following sequence, omits Sailing XP, and can complete with
missing rewards. This is a skeletal compatibility port, not a modern quest.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to the native state ladder, shared Catherby actors,
form and audit persistence, mayor-election item lifecycle, Sailing ownership,
current duck route, completion transaction, post-quest tools, journal, and
debug adapters. It is an implementation specification, not verification
evidence.

## 1. Authoritative references

The Wiki article and quick guide define requirements, route, mechanics,
rewards, and unlocks. The transcript defines choices, refusal/re-talk branches,
inventory-full behavior, lost-item recovery, the repeated-answer audit, and
post-quest dialogue. Revisions were resolved through the OSRS Wiki API on
2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Current Affairs](https://oldschool.runescape.wiki/w/Current_Affairs?oldid=15286651) | 15286651, 2026-08-03 | Identity, requirements, route, rewards, and permanent benefits |
| [Current Affairs/Quick guide](https://oldschool.runescape.wiki/w/Current_Affairs/Quick_guide?oldid=15204331) | 15204331, 2026-05-01 | Exact route order, inventory, boat, ripple, and duck destination |
| [Transcript:Current Affairs](https://oldschool.runescape.wiki/w/Transcript%3ACurrent_Affairs?oldid=15263411) | 15263411, 2026-07-14 | Start, all eight questions, audit retries, loss/reclaim, completion, and post-quest dialogue |
| [Arhein](https://oldschool.runescape.wiki/w/Arhein?oldid=15021471) | 15021471, 2025-11-09 | Shared NPC baseline shop/delivery dialogue and quest role |
| [Councillor Catherine](https://oldschool.runescape.wiki/w/Councillor_Catherine?oldid=15139016) | 15139016, 2026-03-01 | Office actor, form/audit ownership, and post-quest dialogue |
| [Harry](https://oldschool.runescape.wiki/w/Harry?oldid=15051747) | 15051747, 2025-11-21 | Shared shop actor and election-kit replacement behavior |
| [Form cr-4p](https://oldschool.runescape.wiki/w/Form_cr-4p?oldid=15119515) | 15119515, 2026-02-03 | Fill-in operation, eight stored answers, and destruction/replacement |
| [Form 7r4-5h](https://oldschool.runescape.wiki/w/Form_7r4-5h?oldid=15119516) | 15119516, 2026-02-03 | Unsigned/signed forms and mayor use-on interaction |
| [Mayoral fishbowl](https://oldschool.runescape.wiki/w/Mayoral_fishbowl?oldid=15119517) | 15119517, 2026-02-03 | Purchase, break-on-drop, and Harry replacement |
| [Mayor of Catherby](https://oldschool.runescape.wiki/w/Mayor_of_Catherby?oldid=15040522) | 15040522, 2025-11-19 | Chain, loss/replacement, consult/feed, wearable, and POH behavior |
| [Current duck](https://oldschool.runescape.wiki/w/Current_duck?oldid=15197383) | 15197383, 2026-04-25 | Quest route, current-tracking tool, loss, and cargo-hold recovery |
| [Sea charting](https://oldschool.runescape.wiki/w/Sea_charting?oldid=15294037) | 15294037, 2026-08-12 | Current completion and persistent charting contract |
| [Sailing](https://oldschool.runescape.wiki/w/Sailing?oldid=15304296) | 15304296, 2026-08-17 | Skill and player-boat integration |
| [Pandemonium](https://oldschool.runescape.wiki/w/Pandemonium?oldid=15282707) | 15282707, 2026-07-30 | Required prerequisite and initial Sailing/boat unlock |
| [Sawmill coupon (oak plank)](https://oldschool.runescape.wiki/w/Sawmill_coupon_%28oak_plank%29?oldid=15194272) | 15194272, 2026-04-22 | Correct 25-coupon reward variant and stack behavior |
| [Catherby](https://oldschool.runescape.wiki/w/Catherby?oldid=15282771) | 15282771, 2026-07-30 | Office, fish shop, docks, and bay geography |

The current contract is a members, novice, short quest released on 19 November
2025 and the third quest released with Sailing. It starts with Arhein at the
northern Catherby docks. Starting requires completion of Pandemonium, level 22
Sailing, and level 10 Fishing. The Wiki explicitly marks Sailing unboostable;
its Fishing boostability marker is unresolved (`?`) in the pinned revision.
Quest Helper treats both as base-level requirements, but that is only a
transition aid. Live-client or another first-party trace must settle Fishing
boostability before Gate D.

The player needs charcoal, which is available from Catherine's cabinet, and 50
coins. A player-owned boat docked at Catherby is required for the last phase.
There is no combat. Completion awards 1 quest point, 1,400 Sailing XP, 1,000
Fishing XP, the current duck, the Mayor of Catherby, 25 oak-plank sawmill
coupons, and the completed Catherby Bay current chart.

Transition aid only: the local Quest Helper implementation at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/currentaffairs)
maps all nine pre-completion states, six route coordinates, eight form-answer
varbits, the boat/current state, eight items, five NPCs, and two locs.
`python3 tools/questhelper_extract.py currentaffairs --check` exits 0. Quest
Helper is not proof of server spawns, ownership, boat movement, inventory
transactions, or reward correctness. Its reward symbol resolves to the
wood-plank `sawmill_coupon`; the Wiki and cache metadata require
`sawmill_coupon_oak`, which the current server script correctly names.

## 2. Native identity and progress contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 208 |
| Type / difficulty / length | Members; novice; short |
| Release | 19 November 2025 |
| Start | Arhein (`arhein`, native NPC 3200) at `(2803, 3430, 0)` |
| Prerequisite | Pandemonium complete (`quest_pandemonium`, dbrow 7103) |
| Skills | 22 Sailing; 10 Fishing; both are start requirements |
| Primary state | `%current_affairs`, bits 0–6 of permanent transmitted `%current_affairs_main` |
| Form state | `%current_affairs_form`, permanent transmitted carrier for eight answers and eight correctness flags |
| Mayor counter | `%current_affairs_mayor_count`, permanent transmitted varp |
| Quest points | 1 |
| Completion XP | 1,400 Sailing (`14000` tenths); 1,000 Fishing (`10000` tenths) |
| Item rewards | Current duck; Mayor of Catherby; 25 oak-plank sawmill coupons |
| Permanent unlock | Catherby Bay current chart and reusable current duck sea-charting tool |
| End state | 45 |

The dbrow correctly supplies name, membership, difficulty, length, Catherby
location, release date, start NPC/coordinate, state 45, quest point, prerequisite,
skills, and XP. `pack/stat.pack` declares Sailing at protocol stat index 23, so
the source comment claiming Sailing is unavailable is stale. The omitted 1,400
Sailing XP can and must use `stat_advance(sailing, 14000)`.

### 2.1 Expected state ladder versus current behavior

| `%current_affairs` | Canonical phase | Current implementation |
| ---: | --- | --- |
| 0 | Not started; Arhein baseline menu plus quest offer and requirement/start confirmation | Quest branch takes over Arhein's entire conversation; no hard requirements; abbreviated Yes/No with no clear prompt |
| 5 | Speak to Councillor Catherine | Arhein writes 5, but Catherine is not spawned, making the organic route impossible |
| 10 | Fill all eight questions on form cr-4p and return it | Cabinet/form bindings exist; all answers are silently set to option 1 |
| 15 | Return to Arhein and learn the Mayor has died | One short exchange advances directly to 20 |
| 20 | Buy election kit, catch a mayor, and show it to Arhein | Core item exchange exists, but loss/capacity checks and aquarium scope are wrong |
| 25 | Show chained Mayor to Catherine and pass the answer-matching audit | No chained state is modelled; audit is replaced by messages and advances immediately |
| 30 | Receive form 7r4-5h, have Mayor sign it, return it | Both use-on directions and form replacement exist; dialogue/capacity transition is incomplete |
| 35 | Tell Arhein the by-law changed; receive current duck | Item and state advance exist only if one slot is free |
| 40 | Board own boat, deploy duck on the Catherby ripple, follow and collect it, return | Clicking the inventory item anywhere sets chart completion; no boat, ripple, moving duck, follow, or ownership |
| 45 | Complete and retain the rewards/unlocks | Shared completion UI is called, but the state is committed before a non-atomic, incomplete reward grant |

The numerical ladder matches the native dbrow and Quest Helper. No state
migration is needed. Modernization must preserve those values while replacing
the behavior within each state; adding extra narrative milestones should use
the native side fields or proven unused ladder values only after checking every
cache consumer.

### 2.2 Side-state inventory

| Field | Native shape | Current use / required contract |
| --- | --- | --- |
| `%arhein_met` | bit 7 of `%current_affairs_main` | Never read or written; determine whether it preserves baseline/intro dialogue |
| `%current_affairs_form_dialogue` | bit 8 | Unused; likely owns form UI/dialogue progress or interruption recovery |
| `%current_affairs_form_given` | bit 9 | Used only as a delayed hand-in flag; name and behavior do not match cleanly |
| `%current_affairs_harry_chat` | bit 10 | Unused; likely owns first/repeat Harry dialogue |
| `%current_affairs_kit_purchased` | bit 11 | Tracks payment and allows replacements; current replacement capacity is wrong |
| `%current_affairs_audit_start` | bit 12 | Set immediately before the soft-skipped audit; should support interruption/retry |
| `%current_affairs_form_2_given` | bit 13 | Written on one replacement path but never read |
| `%current_affairs_form_q1..q8` | eight 2-bit values | Current code writes `1` to all; must preserve each selected answer index 1–3 |
| `%current_affairs_form_q1_correct..q8_correct` | eight 1-bit flags | Entirely unused; should track matched questions so a failed audit repeats only incorrect ones |
| `%current_affairs_mayor_count` | full permanent varp | Entirely unused; should advance replacement mayor numbering without duplication |
| `%sailing_charting_current_duck_catherby_bay_complete` | bit 28 of Sailing chart carrier 0 | Written by shortcuts and completion; must be owned by the real charting action and remain idempotent |
| `%sailing_boarded_boat` | native Sailing varbit | Not checked; must participate in a general player-boat ownership predicate |

`current_affairs_duck` is a native multinpc driven by the primary state. It
renders its visible duck child for values 0–35 and disappears from value 36
onwards. The base is not present in any spawn roster, so the intended dockside
duck is absent at every state. The moving and stopped current-duck NPC types
also exist in cache, but production code never creates the moving form.

## 3. Implementation surface

The quest root contains 528 lines in two config files and one script. Its
critical external surface is larger than the root.

| Path / surface | Present responsibility | Audit result |
| --- | --- | --- |
| `quest_currentaffairs/configs/currentaffairs.constant` | Native state aliases, XP/count, and route coordinates | Ladder and coordinates match; stale comment says Sailing stat is absent |
| `quest_currentaffairs/configs/currentaffairs.varp` | Three native permanent carriers | Correct carriers; mayor counter and many bits are not implemented |
| `quest_currentaffairs/scripts/currentaffairs.rs2` | Journal, all quest actors/locs/items, completion, and debug | One-file skeleton with declared critical soft-skips |
| `configs/all.dbrow` | Quest ID 208 and metadata | Correct native identity, prerequisite, rewards, and end state |
| `configs/all.varbit` / `all.varp` | Main state, form fields, mayor count, Sailing/boat fields | Rich native persistence exists; much is ignored |
| `configs/all.npc` | Catherine, dock duck morph, and moving/stopped sea duck | Types exist; required world/dynamic spawns do not |
| `configs/all.loc` and `maps/m44_53.jl2` | Council desk/cabinet and both multi-tile aquariums | Cabinet at `(2827,3453)` and aquarium geometry are placed |
| `configs/all.obj` | Forms, fishbowl, Mayor, duck, and oak coupons | Correct symbolic items and menu ops exist; handlers are incomplete |
| `areas/world/configs/m43_53.spawn` | Arhein and Catherby dock actors | Arhein exists; dock duck does not |
| `areas/world/configs/m44_53.spawn` | Harry and east Catherby actors | Harry exists; Catherine does not |
| `areas/area_catherby/scripts/arhein.rs2` | Shared Arhein Talk-to and One Small Favour routing | Delegates unconditionally to Current Affairs, leaving baseline trade/delivery dialogue dead |
| `areas/area_catherby/scripts/harry.rs2` | Shared Harry Talk-to/shop routing | Delegates only during states 20–24, preserving ordinary behavior outside the window |
| `interface_questjournal/scripts/quest_journal.rs2` | Dynamic dbrow journal dispatch | Correctly calls `~currentaffairs_journal` |
| `quests/scripts/quest_cheat.rs2` | `::complete` state adapter | Sets only state 45; does not establish chart unlock or reward ownership |
| `quests/scripts/questpoints.rs2` | Shared completion scroll, QP/count, and jingle | Correct API is called organically, but quest-side transaction is unsafe |
| `quest_pandemonium` / Sailing state | Prerequisite, player boat, and Sailing introduction | Pandemonium itself contains boat/cargo/Sailing soft-skips; no general production Sailing or sea-charting subsystem was found |

No raw numeric entity IDs or legacy `if_openmain`/`if_openoverlay` calls occur
in the quest root. Modern chat choices and the shared journal/completion APIs
are present. The dominant problems are reachability, missing gameplay,
cross-owner dispatch, persistent state, and reward atomicity rather than IF1
UI syntax.

## 4. Start, requirements, and shared Arhein routing

The quest cannot be considered started merely because state 5 can be written.
The start path must first check Pandemonium state 50 and the two skill
requirements using the engine's base/current-stat policy that matches live
OSRS. It must present the transcript's requirement refusal, quest summary, and
explicit accept/decline path. No state write occurs on refusal or if a start
precondition changes while the dialogue is open.

Current code has no prerequisite or skill check at all. Any member can accept.
It compresses the introduction to three lines, then calls `~p_choice2("Yes.",
...)` without a meaningful question. The journal's not-started branch likewise
does not display requirements from the dbrow.

Arhein is shared by baseline Catherby commerce, One Small Favour, and Current
Affairs. The area handler gives two One Small Favour states absolute priority,
then always jumps to `@ca_arhein_talk` and returns. That label always handles
every Current Affairs state, including state 0 and its default case, so the
baseline “What do you have for sale?”, delivery questions, and ship dialogue
below the return are unreachable. The source comment acknowledges the dead
code. This is a live area regression, not harmless quest-local compression.

Modernization needs one owner for `[opnpc1,arhein]` and an explicit combined
menu. Relevant quest options should appear alongside “What do you have for
sale?”, delivery questions, and exit. One Small Favour and Current Affairs can
be active simultaneously; precedence must not silently hide either. Test the
state cross-product for both quests, including Arhein's `opnpc3` trade action.

## 5. Councillor, form cr-4p, and audit

### 5.1 Missing actor and office reachability

`current_affairs_councillor` resolves to native NPC 14952, but it is absent
from `m44_53.spawn` and all other `.spawn` files. There is no `npc_add` for her.
NPC positions are server state rather than cache map data, so her config alone
does not put her in the world. After Arhein writes state 5, the only normal
transition is the unreachable `[opnpc1,current_affairs_councillor]` trigger.
Add the authoritative spawn at `(2825,3454,0)` through the generated spawn
source/workflow, not a hand edit that `gen_spawns.py` will overwrite.

The office desk and cabinet are native placed locs. The cabinet handler gives
one charcoal only while the quest is in the form phase and does not consume it,
matching the route's reusable charcoal. Its ownership check is inventory-only;
confirm live behavior for a charcoal already banked or on the ground rather
than inventing a global uniqueness rule.

### 5.2 Eight-answer form contract

Form cr-4p must ask these eight three-option questions in this fixed order and
store the chosen index in `%current_affairs_form_q1..q8`:

| # | Question | Options in order |
| ---: | --- | --- |
| 1 | Main reason for making port at Catherby? | Pleasure; Trade; Piracy |
| 2 | How many hulls does the ship have? | 0; 1; 2+ |
| 3 | Insured against cargo spillage and theft? | Cargo spillage; Theft; Both |
| 4 | First mate has first aid training? | No; Yes; Partial |
| 5 | Second mate has second aid training? | Partial; No; Yes |
| 6 | Plague symptoms in the last two weeks? | Yes; Partial; No |
| 7 | Sailing experience? | Less than a month; Less than a year; A year or more |
| 8 | Home port? | Varrock; Falador; Edgeville |

Any answer combination is valid. The player needs charcoal in inventory, but
charcoal is not consumed. The item can use sequential modern `chatmenu`
choices; a dedicated cache-authored panel is not required unless live capture
proves one. Interruption, closing a choice, relogging, and destroying/reclaiming
the form must preserve completed answers and resume safely.

Current `~ca_fill_form_soft` sets every answer to 1 in one tick and announces a
soft-skip. It does not present questions, let the player choose, or use the
correctness fields. This removes the quest's central memory mechanic. The
hand-in is also awkwardly split: Catherine consumes a filled form and sets
`form_given`, then a second conversation moves state 10 to 15. Align the state
and re-talk behavior to the transcript without using a flag whose name suggests
the opposite ownership.

### 5.3 Audit retry contract

After Arhein adds the mayoral chain, Catherine asks the same eight questions in
the same order. Each response is compared with the saved form value. On a
perfect pass, she gives form 7r4-5h if one slot is available. If any response
is wrong, she reports the mismatch and repeats only the incorrectly answered
questions until all eight match. The native `q*_correct` flags can persist this
retry set across interruptions. Do not alter the original `q*` values during
the audit.

Current Catherine code sets `audit_start`, prints two soft-skip messages, and
writes state 30 in the same conversation. A subsequent state-25 conversation
also writes 30 solely because `audit_start=1`. No answer is asked or compared,
and no unsigned form is granted on either audit-success path; every player must
talk again in state 30 to receive it. It does not model Catherine rejecting an
unchained mayor. Canonically, a full inventory pauses before form delivery. The
modern path should commit state 30 only when the unsigned form has been granted
or an ownership-safe equivalent is recorded.

## 6. Election kit, Mayor, and item recovery

Harry's shared dispatch is correctly limited to states 20–24, so his normal
fishing-shop dialogue remains reachable elsewhere. The quest branch must offer
the mayoral election kit for 50 coins, require exactly two destination slots
when neither kit item is owned, remove coins only after all conditions pass,
and grant the tiny net plus mayoral fishbowl atomically.

Current initial purchase broadly does this, but subsequent replacement always
requires two free slots even if only one component is missing. Replacement
must calculate the number of missing items. It also needs storage-aware or
otherwise canonical ownership rules so a player cannot manufacture duplicate
nets/bowls through bank, death, or logout transitions.

The Mayoral fishbowl breaks when dropped. Catching the Mayor consumes the empty
bowl but retains the tiny net; dropping the Mayor breaks the bowl and kills the
fish. Before Arhein chains the Mayor, Harry is the replacement path and the
player must catch another. After Arhein has established a stock of spares,
Arhein supplies replacement mayors and increments `%current_affairs_mayor_count`
without charging another 50 coins. The current script gives replacement mayors
directly from Arhein in states 25 and 30 but never increments or speaks the
counter, and it does not distinguish the chain on the item or in persistent
state.

The aquarium handler is declared as global `[oploc1,aquarium]`. Cache map data
currently places that symbol only in `m44_53.jl2`, as eight model pieces forming
the two fish-shop aquariums, but exact coordinate/shape validation is still
needed: every clickable piece must route correctly and future same-symbol
placements must not become quest sources. Scope the action to the two Catherby
aquarium footprints or a dedicated category.

Current catching code incorrectly requires a free slot before replacing the
fishbowl with the Mayor. That exchange is slot-neutral, so a full inventory
with the net and bowl should succeed. The Mayor is retained during Arhein and
Catherine interactions and signs form 7r4-5h when either item is used on the
other. The current two-direction use-on implementation correctly preserves the
Mayor and replaces the unsigned form in-place; add interruption/repeat guards
and the transcript text.

The reward Mayor remains a functional novelty item after the quest. Native
menu ops declare Consult, Feed, and Destroy, and the item is wearable. The
Wiki also documents fish food consumption and POH placement. The quest root
has no Consult or Feed handlers and no focused production search found its POH
integration. These are permanent reward contracts, not optional dialogue
polish. Ownership/reclaim must account for inventory, worn slot, bank, POH, and
death according to live behavior before Arhein hands out another.

## 7. Sailing, current duck, and sea charting

The canonical final sequence is spatial and boat-owned:

1. The player tells Arhein the by-law changed and receives the current duck.
2. The player boards their own boat docked at Catherby.
3. They sail east to the ripple just west of the Obelisk of Water, around
   `(2835,3418,0)`.
4. While the boat is on the ripple, Track-current releases the inventory duck.
5. A moving `sailing_charting_current_duck_moving` follows the authored current
   toward shore; the player follows by boat.
6. At the shore near Holgart, around `(2802,3322,0)`, it becomes the stopped
   collectible form.
7. Only the owning player/boat may collect it. Collection returns the item and
   commits the Catherby Bay chart bit exactly once.
8. The player returns to Arhein and selects the explicit “I've charted the
   currents!” option.

Current `[opheld1,sailing_charting_current_duck]` accepts a click from any tile
on land or sea and immediately sets the completion bit without removing the
duck. The stopped-NPC handler likewise accepts any matching NPC globally and
does not prove the player launched or owns it. The moving form is never spawned,
queued, pathed, or transformed. No boat/ripple check, route, cleanup, logout,
death, region-change, duplicate-click, or competing-player ownership exists.

The cache exposes the item, both NPC forms, chart bit, board state, and many
other Sailing fields, but the production tree has no general Sailing/sea
charting subsystem. Pandemonium directly toggles `%sailing_boarded_boat` and
also declares its boat gameplay soft-skipped. Current Affairs therefore
depends on shared platform work: authoritative boat instances and movement,
deck/player ownership, cargo hold storage, water/ripple coordinates, charting
deployment, player-owned NPC queues, and reconnect cleanup. Implement this as
general Sailing machinery used by charting tools; do not add another
quest-specific teleport or unconditional varbit write.

Before completion, a lost/dismissed duck is reclaimed from Arhein with one free
slot. During and after charting, canonical recovery may also involve the
player's cargo hold. The current code supports only inventory absence at
Arhein, is bank/cargo blind, and can duplicate the tool. After completion the
duck is a permanent reusable current-tracking tool, so its Track-current,
Inspect, Dismiss, storage, and reclaim operations must work outside state 40.
The current handler instead only prints a generic message post-quest.

The static dockside `current_affairs_duck` is also missing from the spawn
roster. Add it through the spawn generation source at its verified Arhein-area
coordinate and let the native `%current_affairs` multinpc remove it after state
35. Do not dynamically add a permanent shared-world NPC per player.

## 8. Completion, rewards, and permanent unlocks

`~ca_quest_complete` currently performs these operations in the unsafe order:

1. writes state 45;
2. sets the Catherby chart bit;
3. grants 1,000 Fishing XP;
4. prints that Sailing XP was skipped;
5. conditionally adds duck and Mayor only when a slot is free;
6. unconditionally attempts to add 25 oak coupons; and
7. invokes the shared completion UI/QP/count lifecycle.

There is no entry guard. Reward capacity is not calculated before mutation,
and state is committed before the grants. Missing duck or Mayor is silently
omitted. Coupons are stackable but still require one slot if the player owns no
stack. Because the end state is written first, an organic retry cannot recover
an omitted item; because the procedure itself has no guard, direct/debug or
future alternate invocation can duplicate XP and coupons. The chart bit is set
both by the shortcut and again at completion, masking whether gameplay actually
occurred.

Canonical completion has a special Mayor rule: if the player lost the Mayor,
Arhein offers a spare; with no free slot the conversation ends and quest
completion waits. The counter advances and Arhein names the new mayor. A
modern completion transaction must:

- require state 40 and a legitimately completed/collected current route;
- resolve duck and Mayor ownership across their canonical stores;
- pause before any irreversible mutation if the missing Mayor or coupon stack
  cannot fit;
- grant 1,400 Sailing and 1,000 Fishing XP exactly once;
- grant/retain exactly one current duck and one Mayor under the canonical
  ownership policy, plus 25 `sawmill_coupon_oak`;
- preserve the chart bit produced by real collection rather than fabricating
  it at the reward screen;
- call `~quest_complete_rewards(quest_currentaffairs, ...)` once; and
- write state 45 at the transaction's commit point so retries are harmless.

The shared completion call correctly derives 1 quest point and completion
metadata from the dbrow. The display string says the correct rewards even when
the code has not granted them, so the scroll is currently not evidence.

The `::complete` adapter writes only `%current_affairs=45`. That is enough for
the generic completed count/QP path but leaves the Catherby chart bit false and
does not define permanent item reclaim state. Establish a documented adapter
policy: the cheat should set every permanent unlock required by completed-state
consumers without awarding normal inventory/XP rewards, and running it twice
must be a no-op.

## 9. Dialogue, journal, and debug lifecycle

All quest dialogue is compressed to functional hints. Missing transcript
coverage includes Arhein's complete introduction and normal menu, requirement
refusals, Catherine's bureaucracy and re-talks, the history of 25 fish mayors,
Harry's full purchase/recovery branches, the unchained-Mayor rejection, every
form/audit line, duck instructions, inventory-full completion pause, numbered
Mayor replacement, and post-quest options. Use the modern `~p_choice*`/
`last_slot` dialogue machinery and retain the humor; no legacy IF1 router is
needed.

The journal has only four broad in-progress sentences. It does not report
requirements before start, missing charcoal/form, partially answered form,
pending filing re-talk, election-kit components, caught/unchained Mayor, audit
retry set, unsigned/signed form, missing duck, boat/ripple target, or collected
chart status. Modernize it from the native state and side fields so reconnects
tell the player what can actually be done. State 45 must agree with the dbrow
and permanent unlocks.

`::currentaffairs` resets only the main state, three flags, and chart bit. It
leaves all eight answers, eight correctness flags, `arhein_met`,
`form_dialogue`, `harry_chat`, `form_2_given`, and `mayor_count` stale. It also
does not remove quest items. `::carun` clears more inventory/state, then
manually writes every phase and invokes the unsafe completion procedure; it
does not exercise requirements, Catherine reachability, choices, aquarium
geometry, replacement, boat ownership, NPC pathing, or full inventory. It can
overflow inventory and still report success based only on state/coupon count.

Debug reset/run tools must be explicitly non-production, storage-aware, and
complete enough to avoid stale permanent bits. The headless test must begin at
the real Arhein trigger and use real Catherine/Harry/loc/boat interactions.
A shortcut that writes the answer matrix or chart bit is useful only as a
separately named fixture setup and cannot serve as Gate D evidence.

## 10. Defect ledger

| Priority | Defect | Player impact | Required resolution |
| --- | --- | --- | --- |
| P0 | Councillor Catherine has no server spawn | Organic route stops immediately at state 5 | Add/verify generated spawn at `(2825,3454,0)` and interaction reachability |
| P0 | Pandemonium/Sailing/Fishing start gates absent | Ineligible players can start and complete | Enforce exact quest/base-stat requirements and refusal/start confirmation |
| P0 | Form and audit are hardcoded soft-skips | Central eight-answer memory mechanic is absent | Persist selections, compare all answers, retry only mismatches, survive interruption |
| P0 | Sailing/current route is an anywhere-click shortcut | No boat, ripple, follow, collection, or ownership gameplay | Build/use shared Sailing and sea-charting machinery |
| P0 | Sailing XP is omitted despite native stat support | Reward is short by 1,400 Sailing XP | Grant `stat_advance(sailing, 14000)` once |
| P0 | Completion commits before capacity/rewards | Items may vanish; retries may duplicate partial rewards | Preflight and atomically commit chart/reward/completion lifecycle |
| P1 | Arhein delegation makes baseline commerce dialogue unreachable | Shared NPC loses shop/delivery/ship conversation | Merge all eligible options under one owner and test quest-state cross-product |
| P1 | Dockside Current Affairs duck is not spawned | Intro world state is visually absent | Add verified generated spawn using native multinpc visibility |
| P1 | Mayor chain/count/replacement lifecycle is incomplete | Catherine accepts no proven chain; replacement history never advances | Model chain milestone and idempotent `%current_affairs_mayor_count` updates |
| P1 | Election-kit recovery demands two slots for one missing item | Valid recovery is blocked | Count missing components and transact atomically |
| P1 | Aquarium catch incorrectly requires a free slot | Full-inventory slot-neutral catch is blocked | Account for bowl consumption before capacity decision |
| P1 | Duck/Mayor ownership is inventory-only | Bank, worn, cargo, POH, death, and reconnect can lose/duplicate rewards | Add canonical cross-container ownership and reclaim policy |
| P1 | Current duck and Mayor permanent item ops are absent | Reward tools/novelty behavior do not work after completion | Implement Track/Inspect/Dismiss and Consult/Feed/POH/reclaim integrations |
| P1 | `::complete` leaves permanent chart state incoherent | Cheat-completed saves differ from organic completion | Define and test completion adapter unlock invariants |
| P2 | Dialogue and journal are heavily abbreviated | Narrative and recovery guidance are missing | Port transcript branches and state-specific journal detail |
| P2 | Debug reset/run leave stale fields and bypass every critical system | Automated success is a false positive | Clear all owned state and replace shortcut evidence with real-route tests |
| P2 | Stale source comments claim Sailing is absent | Future work is misdirected | Remove after Sailing XP and route use are implemented |

## 11. Modernization work packages

### WP1 — Restore reachability and native prerequisites

- Add Catherine and the dock duck through the authoritative spawn data/generator
  workflow, then verify exact coordinates, facing, collision, and menu ops.
- Add Pandemonium state 50 and level 22 Sailing/10 Fishing start checks.
- Resolve Fishing boostability from live behavior; record the evidence.
- Restore explicit accept/refuse/re-talk dialogue without writing state early.
- Expand the not-started journal from native dbrow requirements.

### WP2 — Unify shared Catherby actor routing

- Make the area script the sole owner of Arhein Talk-to.
- Compose Current Affairs, One Small Favour, shop, ship/delivery, and exit
  choices according to eligibility rather than unconditional delegation.
- Preserve Harry's narrow quest dispatch and full normal shop behavior.
- Add a matrix test for both quests' simultaneous states and all menu ops.

### WP3 — Implement form persistence and audit

- Implement all eight fixed-order form choices with answer values 1–3.
- Resume safely after menu close, busy interruption, logout, or reconnect.
- Preserve answers across form destruction/reclaim as canonical behavior
  requires; do not silently overwrite them with option 1.
- Compare audit responses, set/clear `q*_correct`, and repeat only incorrect
  questions until all match.
- Couple state/form ownership so inventory-full delivery never strands a save.

### WP4 — Complete election and Mayor lifecycle

- Make kit purchase/replacement capacity-aware and duplicate-safe.
- Scope aquarium interaction to the two Catherby fixtures and make bowl-to-Mayor
  exchange slot-neutral.
- Implement fishbowl/Mayor destruction, pre-chain recatch, post-chain Arhein
  replacement, mayoral chain milestone, and numbered mayor counter.
- Complete both form-signing directions, interrupted/repeated-use guards, and
  unsigned/signed form reclaim.
- Integrate Consult, Feed, worn/bank/death ownership, POH placement, and
  post-quest replacement as verified by live behavior.

### WP5 — Deliver shared Sailing and current charting

- First modernize the Pandemonium boat prerequisite sufficiently that an owned
  boat can be docked, boarded, moved, and persisted at Catherby.
- Implement a reusable charting-tool API around boat/ripple validation,
  player/boat ownership, dynamic NPC queues, path stages, collection, cleanup,
  cargo recovery, and idempotent completion bits.
- Author the Catherby ripple-to-Holgart current path with the native moving and
  stopped duck types.
- Make launch consume/transfer item ownership and collection restore it exactly
  once through logout, death, region change, and competing-player attempts.
- Keep the post-quest duck usable for other sea-charting locations.

### WP6 — Make completion atomic and permanent state coherent

- Preflight missing Mayor and coupon-stack capacity before any XP/state write.
- Grant both XP rewards and exact items once, then call the shared lifecycle.
- Separate real chart collection from completion; never award route credit from
  an arbitrary item click or completion fallback.
- Make organic completion, reconnect resume, duplicate dialogue, and
  `::complete` converge on documented permanent unlock invariants.
- Expand the journal and post-quest actor/item dialogue.

### WP7 — Harden tooling and regression coverage

- Reset every quest-owned side field and safely remove/reset transient owned
  entities/items in debug setup.
- Replace `::carun`'s writes with a real start-to-finish harness; keep targeted
  fixture helpers clearly separate.
- Add static checks for missing required spawns, global exact loc bindings,
  unresolved soft-skip/deferred markers, and missing Sailing reward calls.
- Capture live-client evidence for choices, dynamic duck movement, reward
  scroll, and post-quest item operations.

## 12. Verification matrix

| Scenario | Required evidence |
| --- | --- |
| Discovery/static | Root, external actors, journal, cheat, dbrow, carriers, maps, spawns, items, and completion appear in the maintained manifest; no undisclosed `soft-skip`/`deferred` remains |
| Spawn/reachability | Catherine exists at `(2825,3454,0)` and dock duck renders for native states 0–35; both disappear/change only as authored |
| Start refusal | Missing Pandemonium, Sailing, or Fishing produces correct refusal and state remains 0 |
| Start acceptance | Eligible player accepts through Arhein's explicit menu; state becomes 5 once; baseline shop/ship options remain available |
| Shared quests | Every relevant Current Affairs × One Small Favour state exposes all valid Arhein choices with no duplicate trigger |
| Form matrix | Each of 3 choices for each question persists in the correct two-bit field; partial close/relog resumes without corruption |
| Form loss | Destroy/reclaim before and after filling never duplicates or strands the form; charcoal is retained |
| Audit success | Matching all saved answers grants one unsigned form and advances safely |
| Audit failure | One or several wrong answers cause only those questions to repeat; relog preserves the retry set |
| Audit full inventory | No form/state loss; clearing one slot resumes delivery exactly once |
| Kit purchase | Insufficient coins and 0/1 free slots do not charge; valid purchase charges 50 and grants both items once |
| Kit loss/recovery | Losing either or both components requires only the missing capacity and cannot duplicate banked/owned copies |
| Aquarium | Only Catherby fixtures work; full-inventory bowl replacement succeeds; repeat click cannot create a second Mayor |
| Mayor lifecycle | Pre-chain loss returns to Harry/recatch; post-chain loss returns to Arhein; counter increments once; Catherine rejects unchained Mayor |
| Form signing | Both use-on directions replace unsigned with signed form once and retain Mayor; loss/reclaim works at every state |
| Boat launch | Land, wrong boat, wrong owner, and wrong sea tile reject without consuming duck or setting chart bit |
| Duck route | Correct ripple launches one owned moving duck, follows authored path, becomes collectible at Holgart, and sets chart only on valid collection |
| Duck interruption | Logout, death, region change, cargo transfer, full inventory, and duplicate/foreign collection recover without loss or duplication |
| Completion full inventory | Missing Mayor/coupon slot pauses before XP, QP, state, or partial item grant |
| Completion exactness | One completion grants 1,400 Sailing XP, 1,000 Fishing XP, one QP, one duck/Mayor under ownership policy, and 25 oak coupons |
| Repeat completion | Re-talk, duplicate packet, reconnect, and second `::complete` add no XP, items, points, or counter increments |
| Permanent rewards | Duck Track/Inspect/Dismiss/reclaim and Mayor Consult/Feed/wear/POH/reclaim match the pinned sources |
| Journal | Every state, partial form, missing item, audit retry, signed form, lost duck, and charted state gives actionable accurate text |
| Build/pack | `make -C src mock230-scripts` and `mock230_pack --check-only` pass against the intended cache |
| Real client | Capture start choices, form menus, audit retry, kit/aquarium, chained Mayor, signed form, owned-boat route, moving duck, reward scroll, and post-quest ops |

## 13. Exit criteria

Current Affairs may be marked `verified-modern` only when:

- a fresh eligible account can complete the canonical route from Arhein to
  state 45 without a debug command or manual variable write;
- Catherine and both duck world forms have correct spawn/ownership lifecycles;
- prerequisite and unresolved Fishing boost policy are evidenced and enforced;
- all eight player-selected answers persist and the audit retry behavior is
  correct across interruption;
- every form, kit item, Mayor, and duck loss/full-inventory path is recoverable
  and duplicate-safe;
- the player must use an owned boat, the Catherby ripple, and the complete
  duck-follow route to earn the chart bit;
- completion is atomic and grants the exact Wiki/dbrow XP, points, items, and
  permanent chart state once;
- Arhein, Harry, One Small Favour, ordinary Catherby commerce, and post-quest
  reward functions remain compatible;
- journal and `::complete` states converge on the documented native contract;
  and
- all Gate D static, compile, pack, automated, reconnect/death, and real-client
  evidence is attached to this record.

Until then, retain `audit-pending`. The absent Catherine spawn alone prevents
organic completion; the form/audit and Sailing shortcuts independently fail
the critical-path standard even after reachability is restored.
