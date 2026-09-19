# Icthlarin's Little Helper modernization audit

Status: `audit-pending` — the cache-native quest row, 0-26 primary state,
support varbits, principal NPCs, items, maps, start, riddle, preparation
hand-ins, two combat shells, reward values, journal dispatch, and several
downstream reads exist. The quest is not completable through ordinary gameplay
in the current worktree: after the High Priest advances the player to state 7,
the western pyramid door rejects every state at or above 5, so the canopic-jar
room can only be reached with the debug command. The implementation also
replaces all four memories, both tile puzzles, and the ceremony with soft-skip
messages; uses timed public-map NPCs without player ownership or recovery; and
does not provide a coherent cat, jar, symbol, or catspeak-amulet lifecycle.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A-D to acceptance, hypnosis and all four
memories, Sophanem access, both pyramid traversals, the Sphinx and High Priest,
jar selection and restoration, preparation, the ceremony, both encounters,
death/logout recovery, completion settlement, journals/admin adapters, and
every direct consumer found. It is an implementation specification, not
evidence that the quest has been modernized.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable requirements, route, transcript, combat, item-lifecycle, reward,
and integration contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Icthlarin's Little Helper](https://oldschool.runescape.wiki/w/Icthlarin%27s_Little_Helper?oldid=15292330) | 15292330, 2026-08-10 | Identity, requirements, full route, rewards, and unlocks |
| [Icthlarin's Little Helper/Quick guide](https://oldschool.runescape.wiki/w/Icthlarin%27s_Little_Helper/Quick_guide?oldid=15109545) | 15109545, 2026-01-20 | Ordered actions, items, re-entry, and combat preparation |
| [Transcript:Icthlarin's Little Helper](https://oldschool.runescape.wiki/w/Transcript%3AIcthlarin%27s_Little_Helper?oldid=15285199) | 15285199, 2026-08-01 | Offer/refusal, memories, riddle, re-talks, ceremony, and finale |
| [Klenter's Pyramid](https://oldschool.runescape.wiki/w/Klenter%27s_Pyramid?oldid=14774215) | 14774215, 2024-10-13 | Pyramid topology, traps, pit, tile puzzles, and chambers |
| [Sophanem](https://oldschool.runescape.wiki/w/Sophanem?oldid=15280165) | 15280165, 2026-07-29 | City access, hostile phase, gate, and postquest integration |
| [Wanderer](https://oldschool.runescape.wiki/w/Wanderer?oldid=15252969) | 15252969, 2026-07-04 | Start checks, hypnosis, jar choice, and postquest identity |
| [Sphinx](https://oldschool.runescape.wiki/w/Sphinx?oldid=15254285) | 15254285, 2026-07-05 | Cat gate, riddle, token, cat loss, and amulet replacement |
| [High Priest (Sophanem)](https://oldschool.runescape.wiki/w/High_Priest_%28Sophanem%29?oldid=14985480) | 14985480, 2025-09-13 | Token handoff, assignments, ceremony, and completion |
| [Apparition](https://oldschool.runescape.wiki/w/Apparition?oldid=15215809) | 15215809, 2026-05-23 | Four forms, styles, stats, weakness, drops, and retry |
| [Possessed Priest](https://oldschool.runescape.wiki/w/Possessed_Priest?oldid=15199218) | 15199218, 2026-04-28 | Stats, four spells, despawn, drops, and re-entry retry |
| [Canopic jar](https://oldschool.runescape.wiki/w/Canopic_jar?oldid=15185287) | 15185287, 2026-04-22 | Four jars, temporary banking, destruction, deduplication, and recovery |
| [Sphinx's token](https://oldschool.runescape.wiki/w/Sphinx%27s_token?oldid=15185439) | 15185439, 2026-04-22 | Issue, handoff, loss, and replacement |
| [Holy symbol (Icthlarin's Little Helper)](https://oldschool.runescape.wiki/w/Holy_symbol_%28Icthlarin%27s_Little_Helper%29?oldid=15185437) | 15185437, 2026-04-22 | Carpenter issue and replacement lifecycle |
| [Unholy symbol (Icthlarin's Little Helper)](https://oldschool.runescape.wiki/w/Unholy_symbol_%28Icthlarin%27s_Little_Helper%29?oldid=15185438) | 15185438, 2026-04-22 | Temporary third-memory state and restoration |
| [Linen (Icthlarin's Little Helper)](https://oldschool.runescape.wiki/w/Linen_%28Icthlarin%27s_Little_Helper%29?oldid=15185873) | 15185873, 2026-04-22 | Raetul gate, price, and handoff |
| [Bucket of saltwater](https://oldschool.runescape.wiki/w/Bucket_of_saltwater?oldid=15185684) | 15185684, 2026-04-22 | Lake collection and suntrap transaction |
| [Bucket of sap](https://oldschool.runescape.wiki/w/Bucket_of_sap?oldid=15246706) | 15246706, 2026-07-01 | Tree allowlist, tool, bucket, and shared quest use |
| [Pile of salt](https://oldschool.runescape.wiki/w/Pile_of_salt?oldid=15261488) | 15261488, 2026-07-11 | Suntrap output and embalmer handoff |
| [Bag of salt](https://oldschool.runescape.wiki/w/Bag_of_salt?oldid=15183631) | 15183631, 2026-04-22 | Alternate embalmer input |
| [Embalming manual](https://oldschool.runescape.wiki/w/Embalming_manual?oldid=15282364) | 15282364, 2026-07-30 | Ground spawn, reading, and bookcase recovery |
| [Catspeak amulet](https://oldschool.runescape.wiki/w/Catspeak_amulet?oldid=15182936) | 15182936, 2026-04-22 | Reward, Sphinx replacement, equip gate, and later consumers |
| [Cat](https://oldschool.runescape.wiki/w/Cat?oldid=15213220) | 15213220, 2026-05-19 | Accepted follower variants, death/loss behavior, and postquest dialogue |
| [Magic carpet](https://oldschool.runescape.wiki/w/Magic_carpet?oldid=15276502) | 15276502, 2026-07-27 | Sophanem/Menaphos route unlocks |
| [Pyramid Plunder](https://oldschool.runescape.wiki/w/Pyramid_Plunder?oldid=15235004) | 15235004, 2026-06-18 | Started-quest/Sophanem access consumer |
| [A Tail of Two Cats](https://oldschool.runescape.wiki/w/A_Tail_of_Two_Cats?oldid=15302023) | 15302023, 2026-08-15 | Direct sequel prerequisite and catspeak consumer |
| [Contact!](https://oldschool.runescape.wiki/w/Contact%21?oldid=15292391) | 15292391, 2026-08-11 | Direct later prerequisite and shared High Priest |
| [Ratcatchers](https://oldschool.runescape.wiki/w/Ratcatchers?oldid=15292483) | 15292483, 2026-08-11 | Direct later prerequisite and catspeak consumer |
| [Rogue Trader](https://oldschool.runescape.wiki/w/Rogue_Trader?oldid=15297810) | 15297810, 2026-08-13 | Started-quest Sophanem clothing task |
| [Desert Diary](https://oldschool.runescape.wiki/w/Desert_Diary?oldid=15280543) | 15280543, 2026-07-29 | Started-quest Sophanem tasks and area integrations |
| [Falador Diary](https://oldschool.runescape.wiki/w/Falador_Diary?oldid=15295882) | 15295882, 2026-08-13 | Indirect dependency through Ratcatchers |

These sources define a members, intermediate, medium quest released 26 April
2005. Gertrude's Cat must be complete. The player needs an ordinary, overgrown,
or hell kitten/cat, a tinderbox, a full waterskin, a willow log, salt or the
bucket-and-suntrap route, a bucket of sap, and linen or 30 coins. Combat level
50 and Agility 35 are recommendations, not start requirements; the pit has an
Agility-dependent failure chance and consumes 20% run energy.

Current mechanics that must survive modernization include:

- four memory sequences during which teleporting is blocked, logout returns
  the player outside the pyramid, and death loses the accompanying cat;
- Wanderer checks of combat levels and equipment bonuses, followed by a
  weighted choice among the Het, Apmeken, and Scabaras jars for new starts;
- a hostile first Sophanem phase, Klenter and spectre presentation, pyramid
  crushers/mummies/scarabs, a fallible pit, and a 25-tile puzzle in which an
  action flips a 3-by-3 neighborhood;
- a confirmed wrong Sphinx answer taking the actual follower, the answer 9
  issuing a replaceable token, and the High Priest authorizing city access;
- the jar-selected Apparition, its melee or magic attack style, full prayer
  protection, 40% Air elemental weakness, safe death/despawn/re-entry retry,
  and exact jar restoration after a second puzzle traversal;
- resumable salt, sap, linen, and willow-log arms; the Embalmer-before-Raetul
  dependency; and loss-aware symbol replacement by the Carpenter;
- the temporary unholy symbol only inside the third memory, restoration of the
  holy symbol, the ceremony cutscene, and a player-private Possessed Priest
  with four magic spells and safe timed retry;
- jar-dependent Possessed Priest potion drops: defence for Het, attack for
  Apmeken, agility for Scabaras, and magic for the legacy Crondis route;
- the fourth memory in which Icthlarin breaks the hypnosis, followed by an
  outside-city completion conversation; and
- two quest points, 4,500 Thieving XP, 4,000 Agility XP, 4,000 Woodcutting XP,
  a catspeak amulet, Sophanem access, and magic-carpet routes, with replacement
  and postquest cat-dialogue behavior.

Transition aid only: Quest Helper's
[`IcthlarinsLittleHelper.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/icthlarinslittlehelper/IcthlarinsLittleHelper.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms primary
states 0-26, the native jar selector, preparation and presentation bits, all
principal world points, required items, encounters, and reward values. The
file last changed in `241eaec29b19243bda7e88e99d5c16568c0776a6` on
2025-08-27. Running
`python3 tools/questhelper_extract.py icthlarinslittlehelper --check` resolves
every named world point and game value. Quest Helper cannot prove server
writes, actor ownership, transaction atomicity, current transcripts, recovery,
or multiplayer isolation.

## 2. Native quest identity and state contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_icthlarinslittlehelper`; quest metadata ID 80 |
| Implementation root | `server/scripts/quests/quest_icthlarin` |
| Type / difficulty / length | Members quest / intermediate / medium |
| Release / series | 26 April 2005 / Kharidian series, second entry |
| Start | Wanderer west of the Agility Pyramid |
| Primary state | `%ics_little_var`, bits 0-4 of native permanent/transmitted `main_ics_var` (varp 445) |
| Authored range | 0 not started through 26 complete |
| Quest prerequisite | `quest_gertrudescat`, complete |
| Start skill policy | No hard skill requirements; Agility 35 and combat 50 recommended |
| End / quest points | State 26 / 2 QP |
| Direct XP | Thieving 45,000 tenths; Agility 40,000 tenths; Woodcutting 40,000 tenths |

The dbrow correctly records membership, difficulty, length, series, start NPC,
end state, two quest points, Gertrude's Cat, recommended combat and Agility,
and all three XP rewards. Those values should remain data-driven. The current
constants name 0, 1, 2, 3, 5-8, 11, 12, 14-19, and 24-26, but omit states 4,
9, 10, 13, and 20-23 even though Quest Helper and the live contract use them.
Do not collapse those intermediate states merely because the current port
soft-skips their scenes.

The primary varp also carries one state-of-mind bit, a suntrap bit, and 25 tile
bits. `ics_little_multi` carries the jar selector, preparation milestones,
four pot presentation bits, priest presentation, tile count, scarabs, entrance,
Sphinx, cat-loss, Wanderer, pit, and sarcophagus support. The newer
`ics_little_multi_extra` carries Sphinx/cat/token/lore and visibility support.
Most are never written by the current scripts. The three preparation bits are
packed inside the native embalmer field; migration must preserve the exact
cache layout rather than replace it with new parallel booleans.

### Required state capture and migration

Capture varps 443, 445, and 446, inventory/bank/equipment/follower domains,
active map/instance identity, owned actors, loc transformations, pending
cutscene state, and dialogue after every canonical action. Include fresh,
full-inventory, wrong-answer, boosted, interrupted, death, logout, reconnect,
duplicate-item, banked-item, and two-player cases.

| Existing shape | Risk | Migration rule |
| --- | --- | --- |
| State 0/1 without completed Gertrude's Cat | Debug or illegal acceptance | Preserve for compatibility, block new acceptance, and report the illegal history |
| State 1 after choosing the explicit refusal | Current refusal writes progress | Treat as accepted legacy progress; never consume supplies or replay rewards, but fix new dialogue |
| State 2 with a fixed liver jar | Current always-Het implementation | Preserve the evidenced jar; use weighted native selection only for new starts |
| State 2-4 outside a memory | Logout/current soft skip | Resume at the canonical outside-pyramid boundary without advancing |
| State 5/6 without cat or token | Legitimate loss or current inventory proxy | Sphinx replacement/retry must inspect real follower and item domains |
| State 7 outside the jar chamber | Ordinary current hard stop | Reopen canonical first-puzzle traversal; never require the debug command |
| State 8-10 with no Apparition | Timed despawn, death, logout, or current public spawn | Re-enter a fresh owner-private encounter of the selected form |
| State 11-13 with multiple/different jars | Current pot overwrite/duplicate path | Reconcile against captured selector, remove only proven invalid duplicates, and restore the selected jar safely |
| State 14 outside the pyramid | Normal post-jar handoff | Preserve restored-jar evidence and route to the High Priest |
| State 15 with partial preparation bits | Normal resumable work | Retain each paid material independently and never consume it twice |
| State 16+ missing holy symbol | Loss or interrupted third memory | Carpenter replacement remains available until the authoritative handoff |
| State 17/18 holding unholy symbol | Current reversed transformation or interrupted memory | Capture provenance; restore the holy symbol at the canonical memory exit without duplicating either |
| State 19-23 with no Possessed Priest | Timed despawn/logout/death/current public spawn | Re-enter a full-health player-private retry and retain completed ceremony setup |
| State 24/25 trapped in east room | Current one-way door | Restore canonical exit/fourth-memory boundary, never infer completion |
| State 26 with missing XP, QP, or amulet | Current non-atomic settlement | Repair only independently receipted missing components; state 26 alone is not proof of payment |
| State 26 with no catspeak amulet | Legitimate loss | Sphinx replacement with an accompanying cat, not a second quest reward |

Admin fixtures must separate primary-state positioning, clean encounter setup,
and fully receipted completion. A state-only cheat is never evidence that a
player legitimately completed item consumption, combat, XP, or quest points.

## 3. Implementation surface

The direct quest root contains seven files and 809 lines at OSRS-Content commit
`61cebc61d08bd9d86d568992f4aaade32b0bdaa7`. The audit describes that exact
tree and does not change gameplay code.

| Path / subsystem | Present responsibility | Audit result |
| --- | --- | --- |
| `icthlarin.constant` | Sparse state names, jar values, rewards, coordinates | Correct reward values; missing intermediate state names; approximate/soft-route coordinates |
| `icthlarin.varp` | Registers native permanent carriers | Appropriate carriers, but comments are incomplete and the scripts leave most support bits unused |
| `icthlarin.rs2` | Wanderer, hypnosis, rock entrance, journal, start fixture | Inventory-cat proxy, no prerequisite/accept boundary, fixed jar, no memories, and no recovery hooks |
| `icthlarin_pyramid.rs2` | First door, pit, west door, Sphinx, town High Priest, completion | Contains the state-7 hard stop, skips puzzles/traps, incomplete token/cat lifecycle, and non-atomic completion |
| `icthlarin_jar.rs2` | Four pot handlers, Apparitions, jar return, fixture | Any pot can replace the selector; public timed spawn; no retry/ownership; return route is skipped |
| `icthlarin_embalm.rs2` | Embalmer, Raetul, saltwater/suntrap, sap, Carpenter | Broad resumable skeleton; gating, allowlist, capacity, and symbol recovery defects remain |
| `icthlarin_ceremony.rs2` | East door, sarcophagus, Possessed Priest, inside priest | Reverses symbol lifecycle, skips memories/ceremony, public timed spawn, no retry, and traps late states |
| Shared Sphinx dispatcher | Routes Dragon Slayer II, A Tail of Two Cats, then this quest | Cross-owner behavior exists but needs an explicit priority/eligibility matrix and regression tests |
| Shared High Priest dispatcher | Routes Beneath Cursed Sands, Contact!, then this quest | Cross-owner behavior exists but needs an explicit priority/eligibility matrix and regression tests |
| `fluffs_has_pet_cat` | Cat eligibility proxy | Counts inventory, bank, and worn objects; cannot represent the required accompanying follower |
| Sophanem map/loc content | Entrance rock, pyramid maps, manual spawn, NPCs | Assets largely exist; exit-hole, hostile phase, trap, and route handlers are incomplete or absent |
| Quest list / POH status | Journal and completion display | Journal dispatch and generated POH adapter exist |
| Quest cheat | Admin completion | Sets state 26 only; grants no XP, QP, item, or settlement receipt |
| Quest combat manifest/checker | Encounter inventory | Row remains `audit-pending` and has no source, handler, loot, recovery, or isolation assertions |
| Content port queues | Historical completion claims | Both queues overstate completeness; one incorrectly endorses the fixed Het jar and must be reconciled |

The cache contains the major items, NPCs, locs, maps, varbits, and quest row.
This is principally a state-machine, route, ownership, transaction, and
integration problem rather than a missing-symbol problem.

## 4. Route reachability and narrative fidelity

| Segment | Current implementation | Required behavior / defect |
| --- | --- | --- |
| Eligibility | `fluffs_has_pet_cat` is checked, not Gertrude's Cat completion | Require the dbrow prerequisite and a qualifying active follower; do not accept a banked cat |
| Offer | Choosing “Sorry” after hearing the request writes state 1 | Preserve a real `Start quest?` Yes/No commit boundary; refusal must leave state 0 |
| Supplies | Deletes tinderbox and full waterskin | Use an atomic transaction; retain exact current dialogue and handle interruption |
| Hypnosis | Always grants the liver/Het jar and teleports | Perform equipment/combat observations, weighted current jar selection, fade/memory presentation, and write selector+item atomically |
| First memory | Absent | Present Klenter/spectre/hostile-city sequence with teleport, logout, and death policy |
| Pyramid entry | Door advances 2 to 3 and teleports | Require the cat at the canonical boundary and bind the player to the correct private/session route |
| Hazards | Pit always succeeds; crushers only animate; scarabs never spawn | Implement run-energy cost, Agility roll/failure/damage, crushers, mummies, scarabs, and safe movement arbitration |
| First tile puzzle | Two messages immediately advance 3/4 to 5 | Drive all 25 native tile bits and 3-by-3 flips; validate completion server-side |
| Sphinx | Core riddle/confirmation exists | Use the actual follower; track cat loss; support all qualifying variants, token capacity, and replacement |
| High Priest token | Deletes token and advances to 7 | Preserve handoff bit and city permission; recover a lost token instead of hardlocking state 6 |
| Jar-room entry | West door rejects every state >=5 | **P0 hard blocker:** state 7 ordinary play cannot reach the jar; route both required traversals |
| Second memory/Apparition | Spawn occurs only after clicking any pot | Bind the selected jar/form, show the memory, and start an owner-private recoverable encounter |
| Jar return | A later “Take” on the pot deletes the jar and jumps to 14 | Require selected jar at its exact spot after the second puzzle; preserve drop/use presentation and dedupe policy |
| Leave after jar | West door remains closed at 14 | Provide ladder/door/rock exit and city re-entry routes for every legal state |
| Preparation | Separate arms broadly work | Add canonical dialogue, Raetul gate, pointy-tree support, manual reading, atomic capacity checks, and recovery |
| Third memory | East door teleports; using holy symbol changes it to unholy | Canonically lend the unholy presentation only inside the memory, hide it in the sarcophagus, then restore the holy symbol |
| Ceremony | One message and a timed public spawn | Play the ritual/Amascut sequence and create the owner-private Possessed Priest only after the scene |
| Fourth memory | One soft-skip line says Icthlarin appeared | Present Icthlarin breaking the hypnosis, restore world/cat state, and route the player outside |
| Final return | State 25 can remain trapped by the east door | Make the ordinary exit reachable and complete only through the town High Priest |

The open rock currently teleports to the same approximate east-city coordinate
and says the player squeezed into Sophanem. No handler was found for the exit
crack, and no state-of-mind, ghost, spectre, or hostile-city behavior is wired.
Those are route responsibilities, not cosmetic extras: they determine whether
logout, death, and re-entry resume at a legal boundary.

## 5. Item and transaction contracts

| Item / operation | Current behavior | Modern contract |
| --- | --- | --- |
| Cat | Inventory/bank/worn object predicate; wrong answer deletes only inventory variants | Use follower ownership and variant-aware custody. Wrong answer removes the confirmed follower; death-in-memory follows current loss rules; ordinary banked cats do not satisfy “with you” |
| Tinderbox + waterskin | Sequential deletes | Preflight exact inputs, commit both once, then write hypnosis state/jar receipt; interruption cannot pay only one item |
| Canopic jar | Always starts liver; any pot overwrites selector and can create another jar | Select once, bind item/form/spot, support temporary banking and missing-item restoration, remove invalid duplicates across proven domains, and never change selector from an unrelated pot |
| Sphinx token | One grant and one delete; no loss recovery | Replace only while eligible and accompanied by a cat; domain-aware single-copy rule and atomic High Priest handoff |
| Saltwater -> salt | Deletes one bucket, adds empty bucket and salt without capacity preflight | Treat as a replacement-plus-output transaction; validate exact capacity and stage/presentation bits |
| Sap | Only `evergreen` and `evergreen_large` | Add the current pointy regular tree allowlist, require knife plus empty bucket, share behavior safely with Eyes of Glouphrie, and prevent duplicates by domain |
| Linen | Available before meeting Embalmer; rejects a full pack even if spent coins free a slot | Gate on `%ics_metembalmer`; compute post-delete capacity; delete exactly 30 coins and add one linen atomically |
| Willow log / holy symbol | Log is deleted before checking output space; replacement ends after state 16 | Preflight capacity or place a recoverable output; allow one symbol replacement until authoritative handoff; inspect all item domains |
| Holy/unholy symbols | Holy is permanently converted to unholy | Model the temporary memory transformation and ensure the player leaves with the canonical holy symbol until it is handed over |
| Embalming manual | Static ground spawn exists but no read handler was found | Wire Read and canonical text; preserve ground/bookcase acquisition and loss recovery |
| Catspeak amulet | Completion blindly adds one after one-slot check | Receipt-aware first grant, domain-aware no-duplicate settlement, Sphinx loss replacement with cat follower, and compatibility with upgraded amulet consumers |

Every irreversible operation should follow `preflight -> lock -> consume ->
produce/write receipt -> commit`. Item checks must state their intended domain:
inventory-only for immediate use, worn/follower for participation, or
inventory+bank+equipment for duplicate and replacement policy.

## 6. Encounter contracts

### Apparition

The four NPC definitions and cache records are present, but the script creates
the chosen actor with `npc_add(..., 200)` at a shared coordinate and listens to
the NPC type's global death queue. It records neither player UID nor actor UID,
allocates no private map, verifies no killer, and has no death/logout/despawn
re-entry handler. A second player can therefore kill or consume another
player's objective. After the timer expires, states 8-10 only say “Defeat the
Apparition first” and never recreate it, permanently stranding ordinary play.

Modernization must bind the actor to player, selected jar, memory instance,
and attempt ID; route every damage/death/despawn/logout event through that
ownership record; and clear it idempotently. The selected form determines
melee or magic behavior. Current prayer protection, stats, attack speed,
Air weakness, bones drop, death retention, cat-loss policy, and full-health
retry need executable assertions rather than a generic default-death call.

### Possessed Priest

The ceremony uses the same unsafe pattern with `npc_add(..., 300)` on the
public east-room coordinate and a global type death queue. It supplies no
unique magic AI, four-spell selection, max hits, range, despawn presentation,
full-health retry, or player ownership. If the actor expires or the player
logs out/dies at states 19-23, re-entering the door only teleports to the room;
it never respawns the priest. Cross-credit and permanent-progress failure are
both possible.

The replacement encounter needs a private attempt and explicit actor handle,
current level-91/90-HP combat data, four magic spells (maximum hits 1, 2, 4,
and 6), attack speed 4, range 5, prayer interaction, timeout, death/logout
cleanup, and idempotent re-entry. On a legitimate kill it drops bones and the
jar-selected potion, with the current coin chance if confirmed by capture.
Only the owning player's valid kill can advance 19-23 to 24.

### Required combat test matrix

- each of the four Apparition records: selected jar, attack style, stats,
  attack speed, protection prayer, Air weakness, bones, owner-only credit;
- all Possessed Priest spells and max hits, distance/range, protection prayer,
  timeout, potion selector, bones/coin policy, and owner-only credit;
- leave arena, death, logout, reconnect, server restart, timer expiry, double
  click, simultaneous players, spectator kill, and stale actor cleanup;
- no progress from the wrong actor, wrong jar, wrong instance, old attempt ID,
  unrelated NPC of the same type, or admin fixture without an encounter receipt.

## 7. Completion, reward, and recovery audit

The town High Priest currently checks one free slot, adds the amulet, grants
all three numerically correct XP awards, writes state 26, then calls the shared
quest-completion helper for QP/count/scroll presentation. This sequence is not
atomic or idempotent. Interruption before state 26 may duplicate the amulet or
XP; interruption after state 26 but before the helper may omit QP/count; and
state alone provides no way to distinguish either case.

Use a quest-specific settlement receipt with independent bits for:

1. valid state-25 finale and holy-symbol handoff;
2. catspeak-amulet first grant or recoverable delivery;
3. Thieving, Agility, and Woodcutting XP grants;
4. two quest points and global completed-quest count;
5. Sophanem/world access and carpet unlock presentation; and
6. completion scroll and postquest dialogue availability.

Lock settlement, preflight item delivery, write each receipt with its effect,
and set state 26 only at the defined commit boundary. Re-entry repairs only a
receipted missing component; it never replays already granted XP/QP. The admin
completion command must either use that same settlement path or clearly create
a state-only diagnostic fixture under a different command.

## 8. Journal, admin, provenance, and tests

The journal is registered and covers broad milestones, but it collapses the
unnamed native states, does not expose partial preparation or item recovery,
and describes skipped routes as if they existed. Generate journal text from
named authoritative states plus support bits and explicit recovery conditions.
Unknown or impossible combinations should produce a diagnostic-safe entry,
not silently fall into “helping the priests.”

The `::icthlarin` fixture only establishes a start demonstration. Separate
fixtures are required for clean eligibility, every memory boundary, each tile
puzzle, every jar/Apparition pairing, partial preparation combinations, symbol
recovery, Possessed Priest retry, pre-completion, and fully settled completion.
Fixtures must allocate their own actor/instance state and clean it up.

Historical status documents are not authorities. In particular,
`QUESTHELPER_CONTENT_PORT_QUEUE.md` labels the quest audited/fixed and treats a
fixed Het jar as correct, contradicting both the current Wiki contract and the
ordinary state-7 hard stop. `SCAPE2009_CONTENT_PORT_QUEUE.md` records the soft
puzzles/cutscene while marking the slice done. Update those claims when the
implementation lands; this dossier is the current evidence record.

No Icthlarin-specific executable route or transaction suite exists. The combat
manifest supplies only a name/summary row and cannot prove actor ownership,
loot, forms, retry, or state writes. Add tests before changing status from
`audit-pending`.

## 9. Downstream consumer and shared-owner audit

| Consumer / owner | Current implementation | Required contract / finding |
| --- | --- | --- |
| Contact! | Explicitly checks Icthlarin state >=26 plus Prince Ali Rescue; shares High Priest | Direct prerequisite is present; regression-test dispatcher priority and completed/started combinations |
| Ratcatchers | Checks Icthlarin completion and Giant Dwarf start; recognizes base/upgraded catspeak items | Primary prerequisite exists, but follower/item semantics remain incomplete |
| A Tail of Two Cats | Shared Sphinx route and upgraded-amulet expectations | Its start omits the Icthlarin prerequisite and requires an upgraded amulet directly; retain as a separate consumer defect |
| Dragon Slayer II | Owns the shared Sphinx dispatcher and later catspeak behavior | Document ordered dispatch and test overlapping quest states; do not let later content steal Icthlarin token/replacement dialogue |
| Beneath Cursed Sands | Owns the shared High Priest dispatcher | Document ordered dispatch and test overlapping BCS/Contact/Icthlarin states |
| Magic carpets | Cache multinpcs/content appear to gate Sophanem/Menaphos around state 2 | Current Wiki describes completion unlocks; this cache-vs-Wiki contradiction requires live capture before changing the threshold |
| Pyramid Plunder | Requires Sophanem access / Icthlarin started and Thieving 21 | A state-2 access gate is plausible; keep distinct from completion-only rewards and test rock/gate entry |
| Rogue Trader | Wiki clothing task requires Icthlarin started | No task implementation was found; only unrelated cheap-flight state is read |
| Desert Diary | Easy/Elite Sophanem tasks require Icthlarin started | Generic completion counters exist, but no task model or direct Icthlarin requirement was found |
| Falador Diary | Indirectly depends on a partial Ratcatchers route | No direct Icthlarin read is expected; diary task implementation remains absent |
| Wintertodt troublecat | Recognizes worn base/upgraded catspeak amulets | Preserve both item families and centralize the catspeak capability predicate |
| Embalmer postquest shop | Shared NPC has an unconditional third operation | Verify the Contact!/shop gate independently; shared-NPC modernization must not expose later content early |
| Postquest cat conversation | No replay/interpretation handler found | Add the current one-time/repeat dialogue semantics using the actual follower and catspeak capability |

Shared NPCs need one documented dispatcher per operation. Its selection must
be based on explicit quest eligibility and priority, with a matrix covering
every overlapping state, rather than whichever quest file happens to own the
trigger.

## 10. Modernization order

### P0 — make the quest safe and completable

1. Name and test all native states/support fields; capture ambiguous current
   behavior before migrating existing records.
2. Implement Gertrude's Cat plus active-follower eligibility and a real
   acceptance boundary; make hypnosis and jar selection transactional.
3. Restore the complete first-memory/pyramid route, 25-tile puzzle, hazards,
   pit policy, exit/re-entry, and especially state-7 jar-room reachability.
4. Make token and selected-jar lifecycle loss-aware and domain-aware; prevent
   pot-based selector overwrite and duplicate creation.
5. Replace both timed public spawns with player-owned, retryable encounter
   attempts and validated death credit.
6. Restore the second traversal, exact jar placement, third memory, correct
   symbol lifecycle, ceremony, fourth memory, and every legal exit route.
7. Replace completion with receipted, idempotent settlement.

### P1 — restore current fidelity and integrations

1. Port current transcript branches, memory cutscenes, Klenter/spectre/city
   presentation, puzzle feedback, and ceremony presentation.
2. Complete preparation gating, tree allowlists, inventory-capacity math,
   manual reading/recovery, and symbol replacement.
3. Implement current Apparition and Possessed Priest AI, weakness, prayer,
   drops, timers, and recovery.
4. Implement catspeak-amulet replacement and postquest cat dialogue.
5. Resolve carpet thresholds by live capture and repair direct consumers and
   shared Sphinx/High Priest dispatchers.
6. Expand journal, admin fixtures, POH status verification, and provenance
   docs from the authoritative state model.

### P2 — hardening and maintenance

1. Add property/fuzz tests for puzzle flips, transactions, duplicate domains,
   state migrations, and settlement interruption.
2. Add multiplayer stress tests for both encounters and shared NPC dispatch.
3. Add telemetry for impossible support combinations, orphaned actors,
   repeated recovery, legacy fixed-Het histories, and settlement repairs.
4. Replace approximate coordinates and cache/Wiki assumptions with captured
   route fixtures and golden presentation snapshots.

## 11. Verification matrix

| Area | Required automated evidence |
| --- | --- |
| Eligibility/offer | Gertrude's Cat complete/incomplete; every accepted cat variant; banked-only cat rejection; Yes/No; no state write on refusal |
| Hypnosis | all combat/equipment observation branches; weighted jar distribution; single atomic supply payment; teleport/logout/death policy |
| Pyramid route | every state 2-25 entry/exit; crushers, mummies, scarabs; pit energy/success/failure; both 25-tile puzzle traversals |
| Sphinx/token | answer 9; every wrong-answer confirmation branch; actual follower loss; full inventory; lost token replacement; overlap dispatch |
| Jar/Apparition | all selected forms and exact spots; drop/bank/recovery/dedupe; private ownership; death/logout/despawn/retry; wrong-player kill |
| Preparation | every partial-order permutation; bag/pile salt; bucket capacity; all trees; linen exact coins/full pack; willow full pack; manual read/recover |
| Ceremony/priest | temporary symbol transitions; sarcophagus; cutscene interruption; four spells; potion selector; timeout/death/logout/re-entry |
| Completion | every interruption point; full inventory; banked/existing amulet; XP/QP/count exactly once; state and receipt consistency |
| Consumers | Contact!, Ratcatchers, A Tail, DS2, BCS, carpets, Pyramid Plunder, Rogue Trader, diaries, Wintertodt, postquest cat dialogue |
| Admin/journal | every native/unknown state, partial bits, missing-item recovery, fixture cleanup, state-only versus fully settled commands |

Minimum manual smoke should run two players concurrently from hypnosis through
both encounters; kill, leave, log out, and reconnect at every memory/actor
boundary; repeat with full inventories and banked/lost quest items; then verify
each downstream unlock on the completed account and its absence on started and
not-started controls.

## 12. Definition of done

Icthlarin's Little Helper may move from `audit-pending` to `modernized` only
when:

- ordinary gameplay completes the canonical route without any debug command,
  soft-skip message, unreachable chamber, or unintended teleport;
- all native states and support fields have named semantics, migration rules,
  and current-server evidence for ambiguous values;
- the four memories, two tile-puzzle traversals, traps, pit, Sophanem access,
  cat policy, and every loss/re-entry boundary match the pinned contract;
- jar, token, preparation materials, symbols, manual, cat, and catspeak amulet
  have atomic, domain-aware, duplicate-safe acquisition and recovery;
- both encounters are player-private, source-accurate, retryable, and immune to
  cross-credit, stale actors, death, logout, and timeout;
- completion and all three XP rewards, QP, count, amulet, and unlocks are
  independently receipted and exactly-once;
- shared Sphinx/High Priest dispatch and every direct/indirect consumer pass an
  explicit overlap matrix;
- route, transaction, combat, recovery, settlement, journal, admin, and
  multiplayer tests pass; and
- the combat manifest and historical port queues cite executable evidence
  rather than the present soft implementation.
