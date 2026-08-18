# King's Ransom modernization audit

Status: `partial / blocked organic route` — the cache quest row, permanent
varps, world geometry, phased actors, evidence objects, trial fields, prison
lock state, Grail puzzle assets, Arthur morphs, reward lamp, and Knight Waves
roster all exist. The server script is a 673-line compatibility slice that
bypasses most of those assets. It can be driven to completion by the debug
walk, but a normal player cannot complete the authored court sequence: the
stairs never move the player into the court map and that map contains only the
jury spawn. Later traversal is also represented by state writes rather than
movement, the prison and Grail puzzles are narrated away, state 80 is skipped,
the lamp is the wrong object and has no redemption handler, and Knight Waves
does not exist as playable content.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A-D to native state ownership, the Sinclair investigation,
Anna's trial, the Camelot ambush, prison escape, Keep Le Faye, Cromperty,
Black Knights' Fortress, Arthur's rescue, completion settlement, Knight Waves,
prayer unlocks, journals, recovery, admin handling, and downstream consumers.
It is an implementation specification, not proof that the quest works.

## 1. Authoritative references

The OSRS Wiki was requested as the canonical gameplay authority. Direct Wiki
API access was unavailable from this workspace during the audit, so the article
revision below is the latest search-indexed snapshot that could be resolved on
2026-08-17; the live guide and transcript links remain explicit review inputs
and must be revision-pinned before implementation lands. Do not silently treat
the older cache quest-row prerequisite IDs as canonical.

| Reference | Revision / status | Audit use |
| --- | --- | --- |
| [King's Ransom article](https://oldschool.runescape.wiki/w/King%27s_Ransom?oldid=15138021) | oldid 15138021, search-index snapshot | Identity, requirements, route, items, trial, prison, Grail, fortress, rewards, consumers |
| [Quick guide](https://oldschool.runescape.wiki/w/King%27s_Ransom/Quick_guide) | live locator; pin before coding | Exact ordering, inventory and travel checkpoints |
| [Quest transcript](https://oldschool.runescape.wiki/w/Transcript%3AKing%27s_Ransom) | live locator; pin before coding | Offer, re-asks, six witnesses, trial failure, capture, prison and completion dialogue |
| [Knight Waves](https://oldschool.runescape.wiki/w/Knight_Waves_Training_Grounds) | live canonical page; search-index snapshot reviewed | Entry, eight waves, restrictions, safe death, persistence, XP, prayer and respawn unlocks |
| [Knight Waves transcript](https://oldschool.runescape.wiki/w/Transcript%3AKnight_Waves) | live locator; pin before coding | Squire instructions, exit/re-entry and completion dialogue |
| [Table puzzle](https://oldschool.runescape.wiki/w/Table_%28King%27s_Ransom%29) | live locator; pin before coding | Interface 390 riddle, correct container and trap behavior |
| [Antique lamp](https://oldschool.runescape.wiki/w/Antique_lamp_%28King%27s_Ransom%29) | live locator; pin before coding | 5,000 XP, level-50 gate, destroy/reclaim policy |
| [Animate rock scroll](https://oldschool.runescape.wiki/w/Animate_rock_scroll) | live locator; pin before coding | Shared One Small Favour object, ownership and replacement rules |

Transition aid only: local Quest Helper commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/kingsransom)
maps states 0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70,
75, 80 and 85, all required objects and actors, the court, prison and fortress
zones, and the native tumbler fields. Its quest path last changed in `241eaec`
on 2025-08-27. `python3 tools/questhelper_extract.py kingsransom --check`
exits 0 and resolves every referenced gameval. Its reward declaration uses
`thosf_reward_lamp` with an explicit placeholder comment; the native cache has
the exact `kr_reward_lamp`, so that helper entry must not be copied into server
content.

## 2. Canonical contract

King's Ransom is a members, experienced, medium quest and Camelot-series entry
3. It starts with Gossip outside Sinclair Mansion. Black Knights' Fortress,
Holy Grail, Murder Mystery and One Small Favour must be complete. Base Defence
65 is non-boostable and required to start. Base Magic 45 is non-boostable, but
the article marks only Defence as a start requirement; the implementation must
verify the precise Magic checkpoint against a freshly pinned guide/transcript
instead of automatically rejecting the opening conversation.

The quest has no required enemy kill. Its canonical flow is:

1. accept Gossip's investigation through the standard quest-start flow;
2. speak to the mansion guard, enter through the east window, physically take
   scrap paper and an address form, search the western library bookcase for the
   undersized black knight helm, leave and hand all three objects to the guard;
3. exhaust Gossip's relevant history topics, then speak to imprisoned Anna;
4. agree to defend Anna, receive the criminal's thread, question servants if
   desired, enter the court and rebut the dagger, poison, thread and presence
   claims without adding incriminating testimony;
5. after a not-guilty verdict, learn about the Camelot statue, enter it and be
   captured by Morgan Le Faye and the Sinclairs;
6. speak to Merlin, find the vent, have the knights form a human pyramid, use
   Telekinetic Grab on the hair-fixing guard or obtain the required supplies
   from prisoners, then solve the four-tumbler hair-clip puzzle and open the
   cell;
7. traverse Keep Le Faye, solve the container riddle on the upper table, and
   obtain the real Holy Grail from the round purple container; wrong choices
   trigger their documented damage/stat-drain/teleport recovery path;
8. speak to Wizard Cromperty for the animate rock scroll, bring any granite,
   the Grail, full black armour, a bronze med helm and an iron chainbody to the
   Black Knights' Fortress, follow its actual route, and free Arthur;
9. speak to Arthur in the basement and give him the bronze/iron disguise, then
   meet him in Camelot for one quest point, 33,000 Defence XP, 5,000 Magic XP,
   one 5,000-XP lamp usable on a skill at level 50 or above, and access to
   Knight Waves; and
10. separately complete Knight Waves to receive 20,000 XP each in Attack,
    Strength, Defence and Hitpoints, unlock Chivalry and Piety subject to their
    Prayer/Defence levels, and unlock the Camelot respawn option.

Knight Waves consists of Sir Bedivere (110), Pelleas (112), Tristram (115),
Palomedes (118), Lucan (120), Gawain (122), Kay (124) and Lancelot (127). Only
Melee is allowed; Prayer, Magic and Ranged are disabled. Knights drain specified
combat stats. The room is a safe-death activity. Leaving through the door
resets to wave one, while death or teleport preserves the relevant retry wave;
logout/resume behavior must match the freshly pinned current page.

## 3. Native identity, ownership and assets

| Field | Native value |
| --- | --- |
| Cache quest / dbrow | id 136 / `quest_kingsransom` |
| Root | `quest_kingsransom`, 9 files, 673 lines |
| Release / series | 24 July 2007 / Camelot #3 |
| Classification | members, experienced, medium |
| Start / end | `gossipy_man` / state 90 |
| Quest points | 1 |
| Reward XP | 330,000 and 50,000 internal tenths = 33,000 Defence / 5,000 Magic |
| Latest audited content commit | `7214485c4283818e974e3e4a943e0d491e4ceb5a`, 2026-08-17 |

The quest root owns three varps, state constants, mansion/court/prison/fortress
handlers, journal, completion and debug code. Shared actor dispatch remains in
the existing owners and must stay single-owned:

- `quest_murder`: Gossip, mansion guard, Pierre, Hobbes and Mary;
- `area_camelot`: Merlin and King Arthur;
- `area_ardougne_east`: Wizard Cromperty;
- `quest_blackknight`: the fortress entrance and secret-wall traversal; and
- generated world spawns: Sinclair exterior/interior, courthouse, prison,
  Keep Le Faye and Black Knights' Fortress basement actors.

The cache already provides exact quest objects `kr_clue_note`, `kr_clue_form`,
`kr_clue_armour`, `murderthreadg`, `kr_hairclip`, `holy_grail`,
`favour_animate_rock` and `kr_reward_lamp`; the four tumbler answer/current
position fields, current tumbler and guess number; court proof/counter/witness
fields; interface 588 lock-puzzle assets; interface 390 container assets;
Arthur and witness morphs; `kr_wave_instr`, `kr_knightwaves_state`, and all
eight `kr_knight*` NPC definitions. Modernization should connect those native
assets, not invent parallel flags or substitute generic objects.

The cache `requirement_quests` values resolve to unrelated quest rows 73, 107,
101 and 10. This conflicts with the Wiki, Quest Helper and even the root source
comment. Treat it as bad cache metadata requiring a deliberate data correction,
not as authority for runtime gating.

## 4. Progress-state audit

| State | Canonical meaning | Current writer / defect | Required invariant |
| ---: | --- | --- | --- |
| 0 | not started | default | Offer is repeatable; no quest items exist solely from opening dialogue |
| 5 | speak to guard | Gossip writes 5 | Guard must write 10; current guard leaves state at 5 |
| 10 | collect evidence | constant and helper only | Currently unreachable; journal wrongly falls through to complete |
| 15 | evidence handed in | guard tests bits, not items | Atomically remove/validate all three real items, then write 15 |
| 20 | Gossip history complete | one compressed re-talk writes 20 | Preserve required topic progression/re-asks |
| 25 | accepted Anna's case | Anna writes state but gives no thread | Grant exact thread before commit; full-inventory branch must retry |
| 30 | trial active | stairs write state without travel | Enter an owned court session; support witnesses, counters, recess/failure/retry |
| 35 | Anna acquitted | fixed judge script writes 35 | Only a correct evidence balance may acquit |
| 40 | enter statue / captured | Anna writes 40, deletes herself | Phase Anna per player; statue owns capture cutscene and prison placement |
| 42 | local invented ambush step | statue writes 42 | Reconcile with canonical 40 -> 45 state map; do not strand old saves |
| 45 | talk to Merlin | Merlin writes 45 from 42 | Correct zone/actor and capture recovery must be enforced |
| 50 | vent found / escape setup | vent writes 50 | Human-pyramid scene and guard/hair-clip acquisition belong here |
| 55 | free knights / tumbler phase | absent constant and writers | Add native state; journal currently mislabels it complete |
| 60 | cell opened | one door click writes 60 | Require real hair clip and solved interface; preserve retry state |
| 65 | Grail obtained | table may write 65 with no item | Award only after correct puzzle settlement |
| 70 | animate scroll obtained | Cromperty may write 70 with no item | Respect inventory/bank ownership, One Small Favour sharing and recovery |
| 75 | reached Arthur | ladder writes state without moving | Real traversal and full-black disguise checks must precede it |
| 80 | Arthur restored | defined but never written | Restoration consumes intended inputs, morphs Arthur, then awaits dialogue |
| 85 | Arthur has disguise | statue jumps 75 -> 85 | Arthur basement dialogue owns bronze/iron handoff |
| 90 | complete | Camelot Arthur calls non-idempotent reward proc | Settle exactly once, then expose Knight Waves access |

Every transition needs a single owner, a zone/actor precondition, a replay-safe
receipt where it transfers value, and explicit login/logout/death/teleport
recovery. Existing saves at 42 and the currently skipped states need a migration
matrix before constants or morph tables change.

## 5. Requirements and quest start

Current `~kr_meets_requirements` checks Black Knights' Fortress, Holy Grail,
Murder Mystery, Defence 65 and Magic 45. It omits One Small Favour even though
that quest is now implemented and completes at state 285. The comments claiming
it is unported are stale. Both the runtime gate and journal omit it.

The gate also applies Magic 45 before the quest starts, while the current Wiki
snapshot explicitly marks Defence—not Magic—as required to start. Pin the
current transcript/guide and split `can_start` from `can_continue` if Magic is
only checked later. Use base stats for both non-boostable checks.

Gossip currently writes state 5 after a shortened dialogue choice, without the
standard quest-start confirmation/summary. Modernize it through the shared
quest-offer helper, retain decline/re-ask branches, and commit state only after
the accepted conversation completes. Fix the cache quest prerequisite rows so
journal/tooling and runtime no longer disagree.

## 6. Sinclair Mansion and evidence

The current mansion is a flag simulation:

- smashing `murderwindow` changes `%kr_window` but never walks or teleports the
  player through the window;
- the window sets the scrap-paper bit without adding `kr_clue_note`;
- the stairs change `%kr_window` without changing plane/tile and also set the
  address-form bit automatically;
- the bookcase sets a bit but never adds `kr_clue_armour`;
- the real `kr_clue_form` ground spawn at `(2739,3581,1)` is ignored; and
- the guard checks only three bits, ignores the selected evidence option,
  removes no items and never writes state 10.

Implement the cache-authored window morph and real traversal. Ground items must
be phase/ownership-safe and pickup handlers must keep the corresponding bit and
object consistent. The bookcase should perform a full-inventory check and add
the exact helm. Read and destroy operations need canonical text and replacement
rules. The guard transaction must validate the three inventory objects,
consume the canonical set as appropriate, advance 10 -> 15 only after success,
and be safe against duplicate submission or a disconnect mid-dialogue.

## 7. Gossip, Anna and the trial

The post-evidence Gossip conversation compresses the investigation history into
one unconditional step. Anna incorrectly says David alone framed her, advances
without supplying `murderthreadg`, and later calls `npc_del`, which risks
deleting a shared world actor instead of applying a player-specific morph.
Canonical dialogue and per-player phasing should come from the pinned transcript.

The current trial is not a trial system:

- the courthouse stairs do not instance or teleport the player;
- the court map `m28_66` spawns only `kr_jury_dummy`; judge, prosecutor, guards
  and the `kr_court_witness` morph are not created;
- `kr_judge` is handled as a loc while the cache also supplies the judge NPC;
- only dog handler, butler and maid hooks exist, and the judge forces that
  fixed order rather than offering all six witnesses;
- setting `%kr_court_witness` cannot make an unspawned witness appear;
- the criminal's thread is never required, shown or consumed;
- the native good/bad evidence counters are unused, so wrong testimony cannot
  produce a guilty verdict or retry; and
- the recess gate, camera, prosecution case, gavel, jury and balloon behavior
  are absent.

Build this as an owned court session using the modern instance/cutscene pattern.
Spawn every participant, centralize witness selection, and let each shared
servant dispatcher route into one court service only when both the session and
witness morph match. Encode testimony as data: question, proof rebutted, good or
bad counter delta, and follow-up dialogue. The verdict service owns cleanup and
state 35; recess, guilty verdict, logout and disconnect restore the courthouse
state without leaking actors or counters into another player's trial.

## 8. Capture, prison and escape puzzle

The statue currently narrates an ambush, teleports directly to the prison and
adds state 42. There is no cutscene session, actor staging or safe recovery if
the sequence aborts. Create a bounded scene with a completion/fallback receipt,
then place the player at the canonical cell checkpoint. Entering Camelot or the
Sinclair recovery routes after leaving must recapture the player where the Wiki
specifies without duplicating setup.

The prison escape presently accepts either a normal `lockpick` or merely owning
one air and one law rune. It never casts Telekinetic Grab, consumes runes,
targets `kr_keep_guard_hair`, awards `kr_hairclip`, initializes or opens
interface 588, validates the four tumblers, writes state 55, opens/transforms
the bars, or performs the human-pyramid sequence. Prisoner supply dialogue is
also absent.

Restore the native flow:

1. Merlin dialogue and the vent trigger establish the escape plan;
2. the pyramid cutscene opens the vent route while keeping the cell coherent;
3. a real spell-on-NPC path validates Magic 45, spellbook/runes/line of sight,
   consumes the cast resources and transfers the hair clip; the prisoner route
   provides the canonical alternative supplies;
4. using the clip on the gate initializes randomized native answer fields and
   interface 588;
5. button handlers update only the native current-position/guess fields;
6. a correct solution writes 55/60 and transforms/opens the gate; and
7. logout, death, teleport and re-entry restore a solvable state without
   reroll abuse or lost clip deadlocks.

## 9. Keep Le Faye, Grail and Cromperty

At state 60 the jewelry table currently narrates finding the correct box,
conditionally adds `holy_grail`, then writes 65 even with a full inventory. It
does not open interface 390 or implement the riddle, incorrect containers,
five-damage trap, random stat drain, teleport, or recapture shortcut.

Implement real Keep Le Faye ladder/stair traversal and guard geography. The
table owns a puzzle session backed by the existing interface. A correct round
purple selection checks ownership and inventory space, adds exactly one Grail,
then commits 65. Wrong selections execute the canonical trap and leave the
quest recoverable. Because `holy_grail` is shared with the completed Holy Grail
quest and one is world-spawned elsewhere, ownership checks must distinguish the
King's Ransom checkpoint without deleting unrelated copies.

Cromperty uses the same `favour_animate_rock` object as One Small Favour. The
current hook adds it only if space exists but writes 70 regardless; a banked
copy is not considered. Centralize ownership across inventory/bank and both
quests. Only commit after the item is possessed or the canonical already-owned
branch completes. Define loss/replacement policy and ensure neither quest can
duplicate or invalidate the other's live item.

## 10. Fortress and Arthur rescue

The source comment says the shared `bkfortressdoor1` bronze-med/iron-chain gate
is all King's Ransom needs. That is false. Those two pieces are used for the
fortress's existing outer disguise and later given to Arthur, but King's Ransom
also requires full black armour for its deeper route. The current quest never
checks black full helm, platebody and platelegs/plateskirt (including the exact
canonical trimmed variants), while `bksecretdoor` is completely ungated.

The basement ladder only writes state and does not move the player. Clicking
Arthur's statue accepts the scroll, Grail and any granite plus bronze/iron,
consumes only bronze/iron, skips restoration state 80 and writes 85. It never
consumes or reconciles the spell inputs, performs the restoration morph, waits
for Arthur dialogue, or applies the correct `Free` interaction contract.

Modernize the cross-quest fortress route without regressing Black Knights'
Fortress. The shared entrance service should preserve that quest's bronze/iron
behavior and add a King's Ransom-specific deeper checkpoint for full black
armour. Route every ladder/door through real maplinks. Arthur's `Free` handler
validates the exact input domain and current location, performs a resumable
restoration scene, writes 80 only after Arthur exists, and leaves item handoff
to Arthur's basement dialogue. That dialogue consumes the bronze/iron disguise,
writes 85 and phases the basement/Camelot Arthur morphs correctly.

## 11. Completion and reward settlement

`~kr_quest_complete` currently writes 90 first, grants both XP awards, tries to
add `thosf_reward_lamp` only when one slot is free, then displays completion.
This has no idempotence receipt: a repeated call can duplicate XP and quest
points, while a full inventory permanently loses the lamp. `thosf_reward_lamp`
is a copied placeholder. The exact cache object is `kr_reward_lamp` (object
11679), but neither object has a Rub/redeem handler anywhere in server scripts.

Replace completion with one replay-safe settlement:

- verify state 85 and the Camelot Arthur actor/zone;
- reserve or defer the exact `kr_reward_lamp` according to its canonical
  bank/destroy/reclaim policy;
- grant 33,000 Defence and 5,000 Magic XP once;
- grant one quest point once through the shared completion service;
- commit state 90 and a settlement receipt atomically; and
- show the reward scroll from recorded results, not as the authority that pays.

The lamp's Rub flow must open the shared skill-choice interface, allow only a
base level of 50 or above, award exactly 5,000 XP once, consume the exact lamp
only after a valid choice, and handle close/logout/retry safely. Verify the
freshly pinned item page before deciding whether destroyed lamps can be
reclaimed; do not infer this from other antique lamps.

## 12. Knight Waves, prayers and respawn

Knight Waves is wholly absent despite the completion dialogue advertising it.
Only native cache state/NPC definitions exist. There is no entrance/squire
handler, owned room, wave spawner, stat drain, combat-style restriction, safe
death, exit/reset, resume, completion settlement or Camelot respawn service.

The prayer system is also over-permissive. Its Chivalry and Piety rows encode
only Prayer levels 60 and 70. `~prayer_checks` tests the Prayer level and points
but never `kr_knightwaves_state` or Defence 65/70, so every member reaching the
Prayer level can currently use both prayers without the quest, Knight Waves or
Defence requirement. Quick-prayer selection delegates to the same incomplete
availability model.

Implement Knight Waves as a post-quest activity with a dedicated lifecycle,
not as more main-quest states. The room service must:

- require state 90 at entry and show canonical squire instructions once;
- enforce Melee-only attacks and reject Prayer/Magic/Ranged server-side;
- spawn one canonical knight at a time and apply its exact stat-drain effects;
- record wave defeat separately from the live NPC/session;
- make death safe and preserve the canonical retry wave;
- distinguish door exit/reset from teleport, logout and death persistence;
- settle four 20,000-XP awards once, then mark activity complete; and
- enable Camelot respawn selection through the central respawn service.

Prayer availability must combine the activity-complete flag with base Defence
and base Prayer requirements for both normal and quick-prayer paths. Diaries
requiring Piety or Knight Waves must query the same canonical completion/unlock
service. The current tree has no implemented Kandarin/Morytania diary consumer;
record those as downstream work rather than claiming they pass.

## 13. Journal, phasing, recovery and admin

The journal omits One Small Favour, treats both levels as start gates, and has
no branches for canonical states 10 or 55. Either state therefore falls into
the final `else` and displays `QUEST COMPLETE!`. It describes evidence as given
although objects never existed, treats lockpick/runes as the escape items, and
contains state 80 text that the gameplay path cannot reach. Rebuild it from
canonical checkpoint predicates and item/recovery state, with explicit branches
for every supported/migrated value.

NPC/loc phasing should be audited as a single matrix. Cache morphs already key
many mansion actors, Arthur, Merlin and the statue to `kr_quest`; server logic
must not use `npc_del` to emulate personal phasing. Login must reconcile
abandoned court/cutscene instances, prison placement, open puzzle interfaces,
missing critical objects and Arthur's two locations. Death and teleport need
explicit behavior at every instanced checkpoint.

`::kingsransomrun` is not a test: it directly writes every state, manufactures
objects, skips all interfaces/scenes and calls the live reward proc. Its reset
deletes every `holy_grail` and `favour_animate_rock` in inventory, including
items owned by Holy Grail or One Small Favour, and does not reset the native
tumbler, court good/bad, wave or login fields. Replace it with test-fixture-only
setup that never awards persistent XP/QP and resets only scoped fixture assets.

The generic quest cheat correctly avoids duplicate state writes, but it sets
only 90. That intentionally does not simulate reward, Knight Waves, prayer or
item ownership. Document that contract and keep downstream unlock checks from
assuming a cheated main state also completed Knight Waves.

POH quest status (`0 / in progress / complete`) and quest-journal dispatch are
correct consumers of the main state. Preserve their single-owner adapters.

## 14. Modernization packages

Implement in dependency order; each package should be independently reviewable
and keep the server build green.

1. **Reference and schema package** — pin current article/guide/transcript,
   Knight Waves, puzzle and item revisions; correct cache prerequisite rows;
   inventory all native interfaces/morphs/animations; define old-save migration.
2. **State and transaction package** — add the missing 55 constant, remove or
   migrate 42, define transition owners, item ownership domains, settlement
   receipts, and login/death/teleport recovery services.
3. **Start and investigation package** — correct prerequisite checkpoints,
   standard offer flow, guard 5 -> 10, real mansion traversal/items/read/destroy,
   atomic evidence hand-in and truthful journal branches.
4. **Anna and court package** — criminal thread transfer, optional servant
   questioning, owned court session, all six witnesses/questions, good/bad
   evidence, recess/failure/retry, verdict and cleanup.
5. **Capture and prison package** — resumable ambush scene, prison recovery,
   Merlin/pyramid sequence, prisoner supplies, real Telegrab, hair clip,
   interface 588 tumbler puzzle and gate transform.
6. **Keep and Grail package** — actual travel, interface 390 riddle/traps,
   shared Grail ownership and recapture/re-entry recovery.
7. **Cromperty and fortress package** — shared animate-scroll ownership,
   full-black route checks, maplinks, Arthur restoration state 80 and disguise
   handoff state 85.
8. **Completion package** — idempotent XP/QP/lamp settlement, exact lamp choice
   interface, full-inventory and destroy/reclaim behavior, post-quest dialogue.
9. **Knight Waves package** — activity lifecycle, eight combat waves,
   restrictions, safe death/persistence, XP settlement, prayer and respawn
   unlocks, diary-facing service.
10. **Journal/admin/test package** — checkpoint-derived journal, scoped fixtures,
    migration tests, restart/rollback tests, and removal of stale simplification
    comments/debug assertions.

## 15. Verification matrix (Gate D)

| Area | Required automated checks | Required integration checks |
| --- | --- | --- |
| Requirements | every prerequisite and both base-stat boundaries; Defence start vs Magic checkpoint | accept/decline/re-ask and cache/journal agreement |
| Mansion | window/stairs maplinks, spawn ownership, pickup/read/destroy/reclaim, full inventory | collect in any allowed order, leave/re-enter, hand in exactly once |
| Anna/thread | space/no-space, inventory/bank/lost object, repeat dialogue | state 20 -> 25 and pre-trial recovery |
| Court | six witnesses, every question delta, correct and bad verdict thresholds | enter/recess/retry/logout/disconnect; no actor leaks across players |
| Capture | scene receipt and fallback placement | statue/Camelot/Sinclair recapture routes after interruption |
| Prison | spell/rune/target checks, alternative supplies, puzzle generation/validation | interface close/logout, wrong guesses, correct gate morph, state 55/60 |
| Grail | every container result, trap effects, item-space and ownership cases | all Keep floors, teleport trap, recapture and successful retry |
| Scroll | One Small Favour and King's Ransom inventory/bank permutations | loss/replacement without duplication or cross-quest deletion |
| Fortress | bronze/iron and full-black variant matrix, all maplinks | Black Knights' Fortress regression plus King's Ransom basement route |
| Arthur | missing input branches, scene idempotence, state 75/80/85 morphs | restoration interruption, basement handoff, Camelot appearance |
| Completion | full inventory, replay, reconnect at each settlement boundary | exact XP/QP/lamp once and reward scroll correctness |
| Lamp | every skill below/at/above 50, cancel/reopen, consume-after-award | destroy/reclaim behavior from pinned reference |
| Knight Waves | entry gate, eight NPC/stat-drain rows, style blocks, wave persistence | safe death, door reset, teleport/logout resume, one-time four-XP settlement |
| Unlocks | Chivalry/Piety Prayer+Defence+activity truth table; quick prayers | Camelot respawn and diary consumer behavior |
| Recovery/admin | every supported and legacy state, item and instance reconciliation | restart at 5/10/25/30/40/42/50/55/60/65/70/75/80/85/90 |

Run at minimum the content compiler/build, focused quest tests, shared
Murder Mystery/Black Knights' Fortress/Holy Grail/One Small Favour regression
tests, prayer and quick-prayer tests, combat safe-death tests, and
`python3 tools/questhelper_extract.py kingsransom --check`. Compiler success
alone is not completion evidence.

## 16. Exit criteria

King's Ransom may be marked modern only when:

- Wiki article, quick guide, transcript, Knight Waves, puzzle and lamp revisions
  are freshly pinned and all intentional deviations recorded;
- the organic route reaches every canonical state with real movement, objects,
  actors, interfaces and cutscenes rather than debug writes or narration;
- the court supports correct, incorrect, recess and retry outcomes;
- the prison and Grail puzzles use their native state/interfaces and recover
  across close, logout, death and teleport;
- shared Grail/animate-scroll/fortress ownership does not regress prerequisite
  quests or destroy unrelated items;
- Arthur uses distinct restoration and disguise handoff states 80 and 85;
- completion and the exact reward lamp settle once under full inventory,
  replay and reconnect conditions;
- Knight Waves is playable, safe, persistent according to current OSRS, and
  awards its XP/prayer/respawn unlocks once;
- Chivalry and Piety are unavailable until Knight Waves and their base Prayer
  and Defence requirements are all satisfied in normal and quick-prayer paths;
- journal, POH, admin and downstream consumers agree with the same state; and
- automated and multiplayer integration evidence satisfies Gate D.

This audit intentionally makes no gameplay changes.
