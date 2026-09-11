# Darkness of Hallowvale modernization audit

Status: `audit-pending` — native metadata, permanent state, map objects, most
actors/items, the dynamic journal, cheat arm, and shared completion call exist.
The organic route is not playable: Drezel and King Roald are bound through the
wrong server identities, while Safalaan and Sarius are never made visible.
Most traversal, the mine, Vanstrom encounter, laboratory puzzle, and Tome are
messages or direct state writes. Several full-inventory paths permanently lose
required items, and both reward doors work before the quest starts. This is a
skeletal compatibility port, not a modern quest.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to the native ladder, shared Myreque actors,
Meiyerditch traversal, mine punishment, Castle Drakan survey, laboratory,
completion transaction, unlocks, recovery, journal, and debug adapters. It is
an implementation specification, not verification evidence.

## 1. Authoritative references

Revisions were resolved through the OSRS Wiki API on 2026-08-17. The article
and guide define mechanics; the transcript defines choices, re-talks,
inventory-full behavior, loss/replacement, and post-quest dialogue.

| Reference | Pinned revision | Audit use |
| --- | --- | --- |
| [Article](https://oldschool.runescape.wiki/w/Darkness_of_Hallowvale?oldid=15292356) | 15292356, 2026-08-10 | Identity, requirements, route, rewards, unlocks |
| [Quick guide](https://oldschool.runescape.wiki/w/Darkness_of_Hallowvale/Quick_guide?oldid=15238950) | 15238950, 2026-06-24 | Traversal, supplies, sketch order, combat, lab |
| [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADarkness_of_Hallowvale?oldid=15266228) | 15266228, 2026-07-17 | Dialogue, recovery, handoffs, completion |
| [In Aid of the Myreque](https://oldschool.runescape.wiki/w/In_Aid_of_the_Myreque?oldid=15302854) | 15302854, 2026-08-16 | Direct prerequisite |
| [In Search of the Myreque](https://oldschool.runescape.wiki/w/In_Search_of_the_Myreque?oldid=15292283) | 15292283, 2026-08-10 | Transitive prerequisite |
| [The Myreque](https://oldschool.runescape.wiki/w/The_Myreque?oldid=15303400) | 15303400, 2026-08-16 | Series/unlock context |
| [Veliaf Hurtz](https://oldschool.runescape.wiki/w/Veliaf_Hurtz?oldid=15264873) | 15264873, 2026-07-16 | Start, reports, completion, Tome replacement |
| [Old Man Ral](https://oldschool.runescape.wiki/w/Old_Man_Ral?oldid=15247611) | 15247611, 2026-07-02 | Phrase, route, papyrus replacement |
| [Vertida Sefalatis](https://oldschool.runescape.wiki/w/Vertida_Sefalatis?oldid=15253000) | 15253000, 2026-07-05 | Message and escort |
| [Drezel](https://oldschool.runescape.wiki/w/Drezel?oldid=15271643) | 15271643, 2026-07-22 | Bush investigation and rune grant |
| [King Roald](https://oldschool.runescape.wiki/w/King_Roald?oldid=15199212) | 15199212, 2026-04-28 | Shared palace actor |
| [Aeonisig Raispher](https://oldschool.runescape.wiki/w/Aeonisig_Raispher?oldid=15196327) | 15196327, 2026-04-25 | Optional Paterdomus teleport |
| [Safalaan Hallow](https://oldschool.runescape.wiki/w/Safalaan_Hallow?oldid=15286216) | 15286216, 2026-08-03 | Survey/lab briefing and book handoff |
| [Sarius Guile](https://oldschool.runescape.wiki/w/Sarius_Guile?oldid=15246324) | 15246324, 2026-07-01 | Rescue and fireplace clue |
| [Vanstrom Klause](https://oldschool.runescape.wiki/w/Vanstrom_Klause?oldid=15250331) | 15250331, 2026-07-03 | Five-attack survival encounter |
| [Vyrewatch](https://oldschool.runescape.wiki/w/Vyrewatch?oldid=15276444) | 15276444, 2026-07-27 | Harassment choices and punishment |
| [Daeyalt mine](https://oldschool.runescape.wiki/w/Daeyalt_mine?oldid=12629331) | 12629331, 2020-06-16 | Ore/cart/guard loop |
| [Meiyerditch](https://oldschool.runescape.wiki/w/Meiyerditch?oldid=15286217) | 15286217, 2026-08-03 | City topology |
| [Myreque Hideout](https://oldschool.runescape.wiki/w/Myreque_Hideout?oldid=15302194) | 15302194, 2026-08-15 | Sickle route destination |
| [Castle Drakan](https://oldschool.runescape.wiki/w/Castle_Drakan?oldid=15283343) | 15283343, 2026-07-30 | Survey and lab geography |
| [Castle sketches 1](https://oldschool.runescape.wiki/w/Castle_sketch_1?oldid=15221160), [2](https://oldschool.runescape.wiki/w/Castle_sketch_2?oldid=15221161), [3](https://oldschool.runescape.wiki/w/Castle_sketch_3?oldid=15221162) | 15221160–62, 2026-05-29 | Survey items/order |
| [Vertida message](https://oldschool.runescape.wiki/w/Message_%28Vertida%29?oldid=15185692) | 15185692, 2026-04-22 | First message recovery |
| [Fireplace message](https://oldschool.runescape.wiki/w/Message_%28fireplace%29?oldid=15185699) | 15185699, 2026-04-22 | Sarius message lifecycle |
| [Sealed message](https://oldschool.runescape.wiki/w/Sealed_message?oldid=15243106) | 15243106, 2026-06-30 | Final handoff |
| [Door key](https://oldschool.runescape.wiki/w/Door_key?oldid=15187159) | 15187159, 2026-04-22 | Pot search and latch |
| [Large ornate key](https://oldschool.runescape.wiki/w/Large_ornate_key?oldid=15185698) | 15185698, 2026-04-22 | Portrait/statue interaction |
| [Broken rune case](https://oldschool.runescape.wiki/w/Broken_rune_case?oldid=15173214) | 15173214, 2026-04-11 | One-time rune supply |
| [Haemalchemy volume 1](https://oldschool.runescape.wiki/w/Haemalchemy_volume_1?oldid=15242246) | 15242246, 2026-06-29 | Telekinetic Grab target |
| [Tome of experience](https://oldschool.runescape.wiki/w/Tome_of_experience_%28Darkness_of_Hallowvale%29?oldid=15185702) | 15185702, 2026-04-22 | Three uses, level gate, reclaim |
| [Temple Trekking](https://oldschool.runescape.wiki/w/Temple_Trekking?oldid=15296985) | 15296985, 2026-08-13 | Burgh de Rott Ramble unlock |
| [A Taste of Hope](https://oldschool.runescape.wiki/w/A_Taste_of_Hope?oldid=15303686) | 15303686, 2026-08-17 | Downstream prerequisite |

`Legacy of Seergaze` is excluded: it is not an Old School quest. Transition aid
only: local Quest Helper commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/darknessofhallowvale)
maps the ladder, route coordinates, items, actors, locs, and side varbits.
`python3 tools/questhelper_extract.py darknessofhallowvale --check` exits 0.
It does not prove server ownership, transactions, combat, or recovery.

## 2. Canonical contract

This is a members, experienced, long quest released 4 September 2006, Myreque
#3. It starts with Veliaf in the Burgh de Rott pub basement and directly
requires In Aid of the Myreque. Supplies are eight nails, two planks, a hammer,
and a knife or sickle; a knife is obtainable. The normal spellbook and
Telekinetic Grab runes are needed late, with runes obtainable in the lab. The
player survives five attacks from level-169 Vanstrom but does not kill him.

| Requirement | Canonical policy | Current defect |
| --- | --- | --- |
| 5 Construction | Boostable at start | `stat_base` rejects boosts |
| 20 Mining | Unboostable | Correct base-stat start policy |
| 22 Thieving | Boostable at start; latch unboostable | Start rejects boosts; no latch check |
| 26 Agility | Boostable at start; obstacles require unboostable 25 | Start rejects boosts; no obstacle checks |
| 32 Crafting | Unboostable | Correct base-stat policy |
| 33 Magic | Boostable | `stat_base` rejects boosts |
| 40 Strength | Unboostable | Correct base-stat policy |

Rewards are 2 QP, 7,000 Agility XP, 6,000 Thieving XP, 2,000 Construction XP,
membership of the Myreque, two Meiyerditch shortcut doors, and a three-chapter
Tome. Each chapter awards 2,000 XP to any skill at level 30+, and the same skill
may be chosen repeatedly. The non-bankable Tome transforms `(3)` to `(2)` to
`(1)` and then crumbles. Veliaf replaces it; after A Taste of Hope the hideout
chest also can. Completion is required for A Taste of Hope and unlocks Burgh de
Rott Ramble/reverse Temple Trekking.

## 3. Native identity and state

| Field | Native value |
| --- | --- |
| Cache quest / dbrow | 117 / `quest_darknessofhallowvale` |
| Start | native NPC 8277 at `(3494,9628,0)` |
| Direct prerequisite | `quest_inaidofthemyreque`, dbrow 77 |
| Primary state | `%myq3_main_quest`, bits 0–8 of permanent `myreque_3_main_var` |
| Visual state | permanent `myreque3_multivar` fields |
| End state / QP | 320 / 2 |
| XP tenths | Agility 70000; Thieving 60000; Construction 20000 |

The dbrow is correct: row 77 is In Aid of the Myreque, not The Feud. The
constant-file cache-corruption claim is false. Its claim that In Search has no
root/`quest_routequest` is stale; the inventory tracks that partial root.

### 3.1 State ladder

| State | Canonical phase | Current result |
| ---: | --- | --- |
| 0, 10, 20 | Offer; accepted; boat fixed | Start exists; repairs are usable prequest |
| 30, 40, 50, 52 | Sail, land, wall route, enter city | Most states collapse into a narrated teleport |
| 54, 60, 65 | Citizen and Ral | Dialogue compressed |
| 70, 80 | Sickle route and hideout | Route mostly messages; 80 retained |
| 90, 100 | Vertida message / return | 90 can be written without receiving message |
| 110, 120, 130 | Drezel and bushes | Drezel unreachable; cutscene omitted |
| 135, 140–160, 170 | Runes, King warning, return | Drezel teleports; actual King unreachable |
| 180, 190, 195 | Return and Vertida escort | Literal 191 replaces escort |
| 200, 210, 220 | Three survey positions | Global item-use shortcuts replace exact tiles |
| 230, 240, 250 | Vanstrom, Sarius, sketches complete | Literal 225 messages; Sarius hidden |
| 260, 270 | Sketch handoff, fireplace message | Order is circular and both flags written early |
| 280, 290, 300 | Lab entry and book | Literal 281; fake rune-pair acquisition |
| 310, 320 | Sealed message and completion | Both grants can fail after state advances |

Keep these native values: they are recovery and visual boundaries, not merely
Quest Helper arrows.

### 3.2 Side-state audit

The main carrier also contains mine punishment, sketches-given,
Sarius-message-given, electric-shock, ghetto-door, Tome chapters, Vyrewatch
hassle/conversation/chat/ignore fields, and rune-case-searched. Only a subset is
used; sketches/message are written prematurely, the rune-case bit grants no
runes, and Tome/encounter fields are unused.

The multivar contains boat/chute/water visuals, mine cart, wall floorboards,
hideout trapdoor, bush, Safalaan/Sarius visibility, tapestry, painting/key,
statue, table/trapdoor, ladder, and three push walls. Organic code never writes
Safalaan or Sarius visible. Hideout value 3 resolves both placed trapdoor
multilocs to `-1`, removing the entrance after first use. The mine cart never
resets between punishments.

## 4. Implementation and ownership surface

The quest root has 1,299 lines across one constant file and six scripts. It has
no raw IDs or legacy modal opens; modern choices and shared journal/completion
APIs exist. The old machinery is teleport-through-map policy, cache morphs used
as substitutes for gameplay, resolved-leaf bindings, and state-before-delivery
transactions.

| Surface | Audit result |
| --- | --- |
| `doh_burgh.rs2` | Veliaf route works; repairs ungated; completion unsafe |
| `doh_meiyerditch.rs2` | Traversal narrated; trapdoor/mine lifecycle wrong |
| `doh_urgent.rs2` | Drezel/King bindings miss world actors |
| `doh_castle.rs2` | Safalaan/Sarius hidden; sketches global; no combat |
| `doh_lab.rs2` | Click/presence shortcuts, no spell targeting, hardlocks |
| `doh_shared.rs2` | Correct XP/QP, unsafe reward/journal/debug lifecycle |
| `quest_sinsofthefather` | Owns real start Veliaf and delegates correctly |
| `quest_tasteofhope` | Owns Vertida/Safalaan delegation; Garth lacks DoH gate |
| `quest_inaidofthemyreque` | Start delegation works; Drezel leaf binding does not |
| `quest_defenderofvarrock` | Aeonisig delegation at state 170 works |
| generic door fallback | Reward and laboratory doors have no quest predicates |

The start spawn is `myq5_veliaf_burgh_hideout`; its sole owner delegates while
In Aid is complete and DoH is incomplete. Vertida's old-hideout spawn is
`sanguinesti_vertida_sefalatis`; A Taste of Hope delegates correctly.

Drezel's world spawn uses the Priest in Peril runtime wrapper, but the DoH arm
is bound to resolved leaf `priestperiltrappedmonk_vis`; the wrapper owner never
delegates. King Roald's palace spawn is `king_roald` at `(3222,3472,0)`, while
DoH binds unspawned `king_roald_cutscene`. Merge both branches into their one
real shared owners. Safalaan's two wrappers and Sarius depend on visibility
fields set only by `::dohrun`, making them organic hard stops.

## 5. Route and mechanic findings

### Start, boat, and wall

Apply the exact mixed boost policy, full accept/refuse/re-talk, and commit only
after acceptance. Each boat/chute repair consumes one plank and four nails with
a hammer, atomically and only in state 10. Current locs can consume supplies
and change permanent visuals before the quest. State 20 requires both repairs.

Restore the actual landing/floorboard route with movement, animations, planes,
collision/failure, and resume checks. Boarding currently teleports to a
plane-one wall, then one floorboard action narrates rocks, wall, passage, and
rubble before teleporting inside.

### Sickle-logo course and hideout

Retain the citizen choices and Ral's “Sage of Sanguinesti” phrase. Ral should
not give an early bronze knife; he later replaces missing papyrus. Support any
valid knife or sickle interaction, not only `bronze_knife`.

The roughly forty-action course is core gameplay. Use native placed locs and
coordinates for searches, squeezes, jumps, crawls, climbs, walls, and interior
transitions. Add real movement/animations, state/tile/plane checks, unboostable
25 Agility obstacle checks, unboostable 22 Thieving latch check, failures, and
relog recovery. Current code fabricates the pot key from the door, changes
table/ladder/wall varbits without traversal, and claims a knife action without
requiring it. Keep the hideout trapdoor in its open usable value.

### Vertida, Drezel, and Varrock

Vertida advances state 90 even when full inventory prevents the message and
does not replace it at state 90. Make every handoff ownership-safe. Restore the
bush shadowy-figure/werewolf cutscene with actor uniqueness, protection,
cleanup, logout/death/region recovery, and correct re-talk.

Drezel gives Varrock teleport runes; he does not teleport the player. Current
code directly teleports. Grant atomically, then use the real King dispatcher.
Retain Aeonisig's optional return teleport.

### Vyrewatch and mine

The mine shortcut unlocks when the hideout is found. Implement blood tithe
(six damage), invulnerable fight, Thieving distraction success/failure (six
damage), disguise where applicable, and mine punishment using the native
encounter fields. Current handlers bind walking types while world spawns are
flying, start only at 190, and expose a direct Talk-to mine shortcut.

In the mine, supply a valid pickaxe when space permits, mine actual daeyalt ore
into inventory, load and consume 15 ore, then exit past the northeast guard.
Current mining increments the cart directly, recognizes only six inventory
pickaxes, has no ore/capacity/animation/success, and never resets the cart.

### Survey, Vanstrom, and Sarius

Vertida must escort/route the player north and enable phase-correct Safalaan.
Safalaan grants charcoal plus three papyrus atomically; current code grants one
and advances on failure. Ral replaces missing papyrus based on papyrus plus
sketch ownership.

Each sketch occurs only at its exact tile in north/west/south order, requires
charcoal plus one papyrus, consumes one papyrus, retains charcoal, and grants
the exact sketch before advancing. Current charcoal/papyrus handlers work
anywhere, consume no papyrus, and advance even if full inventory loses output.

At the south point, spawn the attack-immune Vanstrom, count five attacks, and
handle Protect from Melee/prayer, death, escape, logout, ownership, cleanup,
and Sarius's rescue. Current code is three messages and literal state 225.

Give the three sketches to Safalaan first; only then obtain and show Sarius's
fireplace message. Current state 250 deletes all sketch copies and the
not-yet-obtainable message, sets both flags, then enables the fireplace at 260.
Consume exactly one of each sketch and separate the two handoffs.

### Mansion and laboratory

Canonical order:

1. use any knife on fireplace for Sarius's message;
2. knife the portrait and inspect/recover the ornate key;
3. show the message to Safalaan while retaining it;
4. slash the tapestry with a knife;
5. use the ornate key on the statue, consuming it;
6. traverse the real doors/stairs;
7. search the rune case once for law 1, fire 3, air 3, water 1, mind 1; and
8. cast Telekinetic Grab from the normal spellbook on Haemalchemy volume 1.

A silver sickle on the fireplace gets the wrong-shape response. The ornate key
on the inner door shocks for ten damage and uses `%myq3_electric_shock`.
Current code uses click/presence tests instead of use-on, changes painting state
before key delivery, does not consume the statue key, omits shock, and teleports
through topology. The rune case claims but grants no runes. Clicking it again
or using law rune on air rune substitutes for Telekinetic Grab, consumes runes,
and can advance to 300 without a book. Use the standard spell-target path and
atomic precondition/consumption/delivery order.

## 6. Completion, Tome, and downstream contract

Safalaan consumes the book, teaches the doors, and grants the sealed message.
Current code can consume/write 310 without granting it, then offers no
replacement. Completion must validate state/item, reserve the Tome or persist
reclaim, consume one message, grant exact XP/unlocks once, commit 320, and call
`~quest_complete_rewards` once. Current code commits 320 first and conditionally
adds the Tome, so full inventory permanently loses it.

Implement Read/Destroy on Tome `(3)/(2)/(1)`: modern skill choice, base level
30+, 2,000 XP, atomic decrement, repeated skill allowed, safe close/logout, and
Veliaf/chest replacement calculated from `%myq3_tome_xp` plus ownership.

The two `myq3_door_shortcut_reward` locs at `(3594,3219,0)` and
`(3625,3223,0)` use generic door fallback for everyone. Gate both directions
on state 320. Reconcile the placed but unhandled secret rock barricade at
`(3592,3211,0)` with the canonical topology. A Taste of Hope's Garth currently
starts without checking DoH; add the prerequisite in that owner. Verify the
Temple Trekking owner gates Burgh de Rott Ramble on 320.

## 7. Journal, debug, and recovery

Rebuild the journal from main and side state so it reflects the correct
sketches-first/message-second order and identifies lost messages, papyrus,
sketches, key, runes, book, sealed message, and postquest Tome recovery.
Implement transcript refusal, choice, re-talk, full-inventory, loss, and
postquest branches.

`::doh` omits mine punishment, electric shock, Tome, harassment/conversation,
chat, and ignore fields. It removes an ordinary bronze knife and only examines
inventory. Reset every quest-owned field/artifact without deleting unrelated
items. `::dohrun` manually sets actors that organic code never reveals and its
help text incorrectly says `::softrun`; it is not E2E evidence. `::complete`
sets only 320, leaving Tome/unlock persistence incoherent. Define and test a
deliberate idempotent cheat adapter.

## 8. Prioritized work packages

1. Correct stale metadata comments and mixed requirement policy; consolidate
   every shared NPC under the real spawn owner.
2. Restore atomic boat/chute repair, southern wall, sickle course, latch, and
   two-way hideout state.
3. Add loss-safe messages, bush cutscene, Drezel rune grant, and real King route.
4. Implement Vyrewatch encounters and a per-player resettable daeyalt mine.
5. Implement Vertida escort, Safalaan supplies/recovery, exact sketches,
   Vanstrom survival, and Sarius rescue.
6. Correct handoff order; implement real use-on mansion/lab topology, rune case,
   and standard Telekinetic Grab.
7. Make completion atomic; implement Tome/reclaim, reward/downstream gates,
   journal, and hermetic debug adapters.

No package should invent quest-specific C routing or replace symbolic cache
identities. Preserve native state values and side fields.

## 9. Gate D verification matrix

| Area | Required evidence |
| --- | --- |
| Static/build | One owner per entity; all symbols; `make -C src torirsserver-scripts`; `ToriRSServer_Pack --check-only` |
| Quest Helper | `python3 tools/questhelper_extract.py darknessofhallowvale --check` |
| Start | Prerequisite and every stat below/exact/boosted; accept/refuse; no prequest repair |
| Traversal | Both directions; wrong state/tile/plane; skill fails; relog at every interior |
| Shared actors | Cross-products for Veliaf, Vertida, Drezel, King, Aeonisig, Safalaan, Sarius |
| Items | Full/zero slots, duplicate op, exact-one removal, bank/ground/death loss and replacement |
| Mine | All choices, damage/death, pickaxes, inventory limits, 14/15 ore, repeated punishment/logout |
| Vanstrom | Five owned attacks, immunity, prayer, death/escape/logout, spawn cleanup, Sarius visibility |
| Laboratory | Both use directions, knife variants, sickle response, key recovery/shock, rune and spellbook checks |
| Completion | Exact XP/2 QP once; full inventory; retry/reconnect; scroll/jingle |
| Tome | Eligible/ineligible skill; same skill thrice; close/relog; transforms; both reclaim owners |
| Unlocks | Both doors denied before 320/allowed after; Ramble and A Taste of Hope gates |
| Debug/client | Hermetic reset; `::complete` twice; real-client start-through-postquest capture |

## 10. Exit criteria

Do not mark `verified-modern` until every critical route action is gameplay,
all actors are reachable through actual server owners, item lifecycles survive
full inventory/loss/relog/death, Vanstrom and the mine work, the Tome is usable
and recoverable, reward/downstream gates hold, and Gate D evidence is attached.

This audit intentionally makes no gameplay changes.
