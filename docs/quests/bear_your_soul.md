# Bear Your Soul modernization audit

Status: `audit-pending` — the native 0–3 state carrier, Aretha spawn, shared
spade hook, Cerberus-lobby cave entrances, Key Master dialogue trigger,
journal, cheat adapter, and modern miniquest completion call exist. The
legitimate route is blocked at state 0 because no Arceuus Library search or
`Soul journey` Read handler acquires the book or writes state 1. It is blocked
again at state 2 because the Key Master has no production spawn. The current
dig proc ignores location and can award a damaged soul bearer almost anywhere
in the world, while the completed Soul bearer has none of its advertised
charging, ensouled-head banking, checking, uncharging, or recovery behavior.

Audited: 2026-08-16

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the miniquest root, Arceuus Library,
Aretha and the church crypt, the shared spade dispatcher, Taverley Dungeon and
Cerberus' lobby, Key Master services, charged-item storage, banking, ensouled
heads, journal, and completion lifecycle. It is an implementation
specification, not completion evidence.

## 1. Authoritative references

These stable OSRS Wiki revisions define the route, dialogue, item lifecycle,
reward, and post-miniquest service.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Bear Your Soul](https://oldschool.runescape.wiki/w/Bear_Your_Soul?oldid=15299967) | 15299967, 2026-08-14 | Identity, route, access alternatives, loss recovery, and reward |
| [Bear Your Soul/Quick guide](https://oldschool.runescape.wiki/w/Bear_Your_Soul/Quick_guide?oldid=15292363) | 15292363, 2026-08-10 | Ordered book, Aretha, crypt, travel, and repair actions |
| [Transcript:Bear Your Soul](https://oldschool.runescape.wiki/w/Transcript%3ABear_Your_Soul?oldid=15263179) | 15263179, 2026-07-14 | Start message, acceptance/refusal, re-talk, Key Master repair, and finale |
| [Soul journey](https://oldschool.runescape.wiki/w/Soul_journey?oldid=15187538) | 15187538, 2026-04-22 | Library rotation, item restrictions, first-read start, and customer relationship |
| [Transcript:Soul journey](https://oldschool.runescape.wiki/w/Transcript%3ASoul_journey?oldid=14330499) | 14330499, 2022-10-01 | Complete 18-page book contents for the book interface |
| [Damaged soul bearer](https://oldschool.runescape.wiki/w/Damaged_soul_bearer?oldid=15187539) | 15187539, 2026-04-22 | Crypt acquisition, Check/Destroy contract, and during-miniquest replacement |
| [Soul bearer](https://oldschool.runescape.wiki/w/Soul_bearer?oldid=15187540) | 15187540, 2026-04-22 | Fill, Check, Uncharge, charges, bank transfer, UIM restriction, and post-completion reclaim |
| [Key Master](https://oldschool.runescape.wiki/w/Key_Master?oldid=14919364) | 14919364, 2025-06-14 | NPC location, Cerberus relationship, Talk-to/Listen operations, and clue relationship |
| [Transcript:Aretha](https://oldschool.runescape.wiki/w/Transcript%3AAretha?oldid=15023536) | 15023536, 2025-11-12 | Shared soul-lore subjects, quest offer, refusal, post-completion dialogue, and medium clue branch |
| [Transcript:Key Master](https://oldschool.runescape.wiki/w/Transcript%3AKey_Master?oldid=15294595) | 15294595, 2026-08-12 | Shared backstory/Slayer subjects, repair sequence, and master clue branch |

The sources identify Bear Your Soul as a short, intermediate, members-only
miniquest in the Great Kourend series, released 19 May 2016. It has no quest,
skill, quest-point, or combat requirement and awards no quest points or XP. The
only reward is a working Soul bearer.

Transition aid only: the local Quest Helper checkout's
[`BearYourSoul.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/bearyoursoul/BearYourSoul.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms the 0–2
active checkpoints, church dig coordinate, Taverley/Key Master zones, cave
entrance, NPCs, items, and alternative access route. It guides transition
tests but does not override the Wiki, transcript, or osrs239 cache.

`python3 tools/questhelper_extract.py bearyoursoul --check` resolves all named
symbols, coordinates, and `miniquest_bearyoursoul`.

## 2. Native miniquest identity and player contract

The cache-native `miniquest_bearyoursoul` row and pinned sources define this
contract:

| Field | Native value / expected behavior |
| --- | --- |
| Cache miniquest ID | 138 |
| Type | Members' miniquest |
| Difficulty / length | Intermediate / short |
| Series | Great Kourend |
| Release date | 19 May 2016 |
| Start | Find and read `Soul journey` in the Arceuus Library, then accept Aretha's offer at the Soul Altar |
| Prerequisites | None |
| Required levels | None |
| Required item | Spade, retained |
| Route access | Dusty key, a supported boostable 70/80 Agility Taverley shortcut, or a Key master teleport |
| Combat | None; reaching the Key Master does not require 91 Slayer or a Cerberus task |
| Primary state | `%arceuus_soulbearer_story`, cache varbit on `millcheck_multi`, bits 30–31, values 0–3 |
| End state | 3 |
| Quest points / XP | 0 / none |
| Reward | One functional Soul bearer |
| Permanent effects | Soul bearer loss recovery from the Arceuus church crypt and access to its charge/banking utility |

The dbrow correctly records a 0-quest-point miniquest, release date, end state,
and no direct or indirect prerequisites. Its start coordinate is present but no
start-NPC field is populated; that is compatible with the book-first start and
must not be "fixed" by treating Aretha's spawn as the only start trigger.

The cache also contains `arceuus_soulbearer_oldstory`, an older one-bit field
on another carrier. No runtime reference was found. Modernization must confirm
save migration/history before removing or writing it; the current two-bit
`arceuus_soulbearer_story` is the authoritative live state.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Miniquest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_bearyoursoul/configs/bearyoursoul.constant` | Four states and Aretha/dig coordinates | State and coordinates match cache/Quest Helper; the dig coordinate is never consulted |
| `server/scripts/quests/quest_bearyoursoul/configs/bearyoursoul.varp` | Restates the native `millcheck_multi` carrier for the overlay | Correct carrier; duplicate base definition is intentional overlay structure, not a new progress varp |
| `server/scripts/quests/quest_bearyoursoul/scripts/bearyoursoul.rs2` | Completion, journal, dig proc, Aretha, and debug route | A 98-line route sketch with no production start, global dig exploit, compressed dialogue, incomplete item ownership, and a debug-only full walk |

The root totals 116 lines across three files. Its comment explicitly defers
the library search and dusty-key path, while `::bysrun` directly writes every
state and calls completion. That debug path proves symbol linkage only; it
does not prove any legitimate world interaction.

### Mandatory shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dynamic miniquest journal dispatcher | Correctly calls `~bearyoursoul_journal`; journal lacks item/recovery detail |
| `server/scripts/quests/scripts/quest_cheat.rs2` | `::complete` adapter | Idempotently writes state 3 but cannot prove item reward, charges, or recovery |
| `server/scripts/areas/world/configs/m28_60.spawn` | Aretha | Correct production spawn at 1814,3851 |
| Arceuus Library bookcases and NPCs | `Soul journey` acquisition and help | Cache has the bookcases, four customers, Logosia, and Biblia; world NPC spawns exist, but no random-book search/customer/Biblia service was found |
| `server/scripts/general_use/scripts/spade.rs2` | Shared Dig operation | Calls `~bys_try_dig` after one generic animation, but the miniquest proc has no region predicate |
| Arceuus church `cryptstairsup/down` | Route into the crypt | Both cache locs are categorized into shared climb handling; verify authored landings and access on all planes rather than adding quest teleports |
| `server/scripts/areas/taverly/dungeon/` | Dusty-key route | Velrak can supply the key and the deep-dungeon gate accepts it; access shortcuts and the complete route still need live verification |
| Key master teleport | Alternative route | Cache item exists, but no production Break/Teleport handler was found |
| `server/scripts/minigames/minigame_cerberus/scripts/cerberus.rs2` | Cave entries and Key Master | Cave entrances reach the lobby and own Key Master's op1; Key Master is unspawned, repair is a soft-skip, ordinary dialogue is one line, and Listen is absent |
| Cerberus lobby world ownership | Key Master carrier | No `.spawn` or `npc_add` for `keeper_of_keys` was found at the canonical 1310,1251 coordinate |
| Charged-item framework | Soul bearer charges | Cache charge dbrow and 11-bit quantity varbit exist; no Soul bearer-specific use of the reusable per-item charge APIs exists |
| Ensouled-head items, inventory, and bank | Reward's Fill operation | No Soul bearer/ensouled-head interaction or atomic multi-item bank transfer exists |
| Treasure Trails | Aretha and Key Master shared subjects | Cache clue targets exist for Aretha (medium) and Key Master (master); their name-specific op1 handlers must not preclude future/current clue dispatch |

### Cache-native assets already available

The cache contains the essential symbolic content:

- `arceuus_library_soulbearerbook` with Read and the complete display item;
- native library progress fields including `zeah_library_fetchbook`,
  `zeah_library_bookhunter`, and shuffle state;
- Aretha, Key Master, library customers, Biblia, bookcases, church stairs,
  Taverley gates, cave entrances, and Cerberus lobby scenery;
- damaged and repaired Soul bearer objects, with Check/Destroy on the damaged
  form and Fill/Check/Uncharge on the repaired form;
- `charges_soul_bearer` and `%charges_soul_bearer_quantity`, supporting the
  cache's 1,000-charge contract; and
- the modern shared quest journal/completion and reusable item-var charge
  services.

Modernization should connect these assets through data-driven library,
shared-spade, maplink/climb, NPC subject, charged-item, and banking services.
It should not replace the library with a fixed debug bookcase, make the Key
Master a per-click temporary NPC, or store reward charges in a parallel quest
varp.

## 4. Native state model and current reachability

The cache, Wiki route, and Quest Helper agree on the following state sequence:

| State | Required phase | Current implementation / defect |
| ---: | --- | --- |
| 0 | Search the rotating Arceuus Library collection, obtain `Soul journey`, read it, and receive the artefact message | **Blocked:** there is no bookcase acquisition or item Read handler and no production write to state 1 |
| 1 | Speak to Aretha, choose the book subject, then accept or refuse her offer | Aretha is spawned, but one short exchange automatically advances to 2; all shared subjects and Yes/No choice are absent |
| 2 | Enter church crypt, dig with spade, bring damaged bearer to Key Master, and have it repaired | Dig is globally reachable without a location check; Key Master is not spawned; debug state/item injection is the only full path |
| 3 | Complete; use, charge, uncharge, and recover the Soul bearer | Completion scroll exists, but every reward operation and post-completion crypt reclaim is absent |

State 2 deliberately carries both "not yet dug" and "damaged bearer held".
The item is the native substate. Modernization does not need another permanent
quest bit, but it does need an authoritative inventory/bank/ground ownership
policy and a crypt-only recovery transaction.

### Deterministic state-0 blocker

`arceuus_library_soulbearerbook` resolves in the cache and advertises Read, but
its symbolic name occurs nowhere under `server/scripts` outside comments and
the audit search. None of the Arceuus Library bookcase names has a production
search handler, and Biblia/customer NPC services are absent. Consequently a
normal player cannot obtain the book and no live action writes
`%arceuus_soulbearer_story=1`.

The correct repair is a reusable Arceuus Library implementation. The Wiki
states that only eligible shelves contain books, locations rotate every 80–100
minutes, Sam/Professor Gracklebone/Villia can request this title, and Biblia
can narrow its location. First complete reading displays the artefact message
and begins the miniquest. The book cannot be banked and disappears when
dropped, preventing duplicate customer turn-ins. Preserve those shared rules
instead of granting the book directly from a quest NPC.

### Deterministic global-dig exploit

`^bys_dig_coord` is declared but never read. General spade handling calls
`~bys_try_dig` after several other quest digs regardless of the player's
coordinate. At state 2, the proc checks only inventory space and absence of a
damaged bearer. A player can therefore dig one up anywhere that an earlier
spade proc does not consume the click. With a full inventory it silently
returns false and falls into unrelated/default spade behavior.

Restrict the branch to the authored interior of the Arceuus church crypt—not
merely distance from one sample tile—then perform animation, delay, message,
and item delivery atomically. During state 2 it yields the damaged form. After
state 3, the documented loss-recovery dig yields a usable replacement only
when the player does not already own one under the chosen inventory/bank policy.

### Deterministic Key Master blocker

The cache NPC `keeper_of_keys` has Talk-to and Listen, and the Cerberus script
binds Talk-to. Repository-wide searches found no persistent spawn or scripted
addition for the type. The cave entrance teleports to the correct lobby map,
but the player arrives in an empty service area and cannot invoke the repair
branch. Add one canonical persistent Key Master at 1310,1251 and retain his
shared Cerberus, Listen, clue, ordinary-dialogue, and miniquest subjects through
one dispatcher.

## 5. Current versus required playable route

| Chapter | Current route | Modernized route contract |
| --- | --- | --- |
| Library search | Absent; debug runner says the book was read | Rotating eligible shelves, capacity-safe book acquisition, Biblia clues, customer coexistence, full 18-page Read interface, and state-1 commit only after the required read completion/message |
| Aretha offer | One click auto-advances | Shared soul-lore/quest/clue menu; `I've been reading your book...`; explicit Yes/No; refusal remains state 1; acceptance alone writes state 2 |
| Church travel | Relies on generic stairs without evidence | Verify both exterior/interior stairs and authored landings on every relevant plane; do not use quest-only coordinate shortcuts |
| Crypt dig | Any world tile at state 2 | Spade retained; exact crypt area; dig animation/timing; full-inventory message; one damaged item; safe Destroy/re-dig |
| Taverley access | Dusty-key gate exists; Cerberus cave reaches lobby | Preserve dusty-key acquisition, supported boostable shortcuts, and functional Key master teleport; no Slayer/task gate merely to reach the NPC |
| Key Master repair | Missing NPC; if injected, one soft-skip line completes | Spawn canonical NPC; shared subject arbitration; complete repair transcript/animation; consume exactly one damaged bearer and grant one repaired bearer atomically |
| Completion | State 3 is written before cleanup/grant | Idempotent repair/reward commit with state, item, 0 QP/XP, scroll, and reconnect behavior exactly once |
| Soul bearer utility | All advertised item ops are unbound | Charge to 1,000 with one blood+one soul rune per charge; Fill sends eligible heads to bank for one charge each; Check and Uncharge work; UIM rejects Fill; bank configuration is supported |
| Loss recovery | Dig is disabled after state 3 | Crypt-only documented reclaim, duplicate-safe across inventory/bank/ground, followed by fully functional item behavior |

## 6. Narrative, state, and lifecycle oversight matrix

| Priority | Oversight | Evidence / consequence | Required correction |
| --- | --- | --- | --- |
| Blocker | No library acquisition/read route | Book has cache Read op but no server trigger; no production state-1 write exists | Implement shared rotating Arceuus Library and full book Read start transaction |
| Blocker | Key Master is not spawned | Only cache definition and op1 handler exist; no `.spawn`/`npc_add` | Add canonical persistent carrier and verify lobby loading |
| Critical | Dig works almost anywhere | Declared dig coordinate is unused; shared spade calls proc globally | Gate on the complete church-crypt area and test every boundary tile |
| Critical | Reward has no behavior | No Soul bearer item trigger or ensouled-head transfer exists | Implement Fill, Check, charge/configure, Uncharge, and charge drain on reusable item-var services |
| Critical | Post-completion loss recovery is absent | Dig proc accepts only state 2; Wiki explicitly permits another from crypt | Add state-3 duplicate-safe reclaim with exact ownership policy |
| High | Aretha omits acceptance/refusal | State 1 auto-writes 2; transcript has Yes and refusal | Implement choice and preserve state on refusal/interruption |
| High | Aretha name handler suppresses shared subjects | Only quest synopsis exists despite soul-lore, postquest, and medium-clue branches | Use one subject dispatcher with exact stage-aware menu |
| High | Key Master name handler suppresses shared subjects | Ordinary history is one line; Listen and master clue are absent | Integrate miniquest repair with Cerberus/task, Listen, clue, and ordinary dialogue |
| High | Completion is state-first | Writes 3 before deleting/granting and trusts inventory-only checks | Use an interruption-safe, idempotent reward claim/commit |
| High | Damaged-item ownership is inventory-only | Bank/ground state can duplicate; full inventory silently falls through | Define inventory/bank/ground ownership, explicit feedback, and atomic delivery |
| High | Damaged Check/Destroy are unbound | Cache advertises both; transcript/item page defines re-dig recovery | Implement informative Check and Destroy confirmation/reclaim path |
| High | Soul journey item rules are absent | Read, rotating acquisition, disappearing Drop, and bank prohibition are not implemented | Treat it as a shared library book with exact read/drop/bank/customer lifecycle |
| High | Key master teleport is unimplemented | Cache item exists but no Break/Teleport trigger was found | Implement shared teleport-scroll destination and consumption policy |
| Medium | Taverley alternatives are not proven | Dusty gate works, but 70/80 Agility alternatives were not found in this audit | Verify or implement both boostable shortcuts without imposing them as requirements |
| Medium | Church stairs have only generic evidence | Cache categories call shared climb; no live-client route proof exists | Validate both directions and add maplinks only where authored landings differ |
| Medium | Repair presentation is a soft-skip | One line replaces the repair sequence and visual | Restore Key Master dialogue, repair action/animation, and final response choice |
| Medium | Journal collapses state 2 | Cannot distinguish dig, travel, missing item, Key Master, or recovery | Derive guidance from state, location, and authoritative item ownership |
| Medium | Debug runner is false evidence | Directly writes 0→1→2, invents damaged item, and calls completion | Keep only a clearly scoped reset/helper; acceptance tests must invoke production handlers |
| Low | Post-completion Aretha dialogue is wrong | Current “The souls rest” does not match her authored thanks/respect exchange | Implement the pinned post-miniquest subject |

### Item and reward lifecycle contract

| Item / state | Acquisition and use | Loss, capacity, and duplication policy |
| --- | --- | --- |
| `Soul journey` | Found in rotating Arceuus Library shelves; Read all required content; first qualifying read writes state 1 | Full inventory refuses with clear feedback; Drop makes it disappear; it cannot be banked; library/customer reacquisition remains available without resetting story |
| Spade | Brought by player and used through shared held-item Dig | Retained; absent/wrong item cannot trigger; every other shared dig must retain priority at its own valid location |
| Damaged soul bearer | State-2 crypt dig produces one | Check explains damage; Destroy confirms it can be re-dug; inventory/bank/ground ownership prevents duplicates; full inventory leaves state unchanged with explicit feedback |
| Dusty key | Obtained from Velrak or existing player storage; opens deep Taverley gate | Retained by the route; keyring behavior, if supported, must be accepted consistently |
| Key master teleport | Alternative direct access to the Key Master | Consumed only after a successful legal teleport; blocked teleport leaves item intact |
| Soul bearer | Key Master repairs one damaged bearer into the reward; post-completion crypt dig reclaims a lost one | Unique under inventory/bank/ground policy; Drop/reclaim is safe; reward completion cannot leave state 3 without an obtainable item |
| Soul bearer charges | One blood rune plus one soul rune per charge, maximum 1,000 | Charge amount cannot exceed materials/cap; Uncharge follows the authoritative rune-return contract; transfer/bank preserves per-item charge data |
| Ensouled heads | Fill moves eligible held heads to bank, consuming one charge per head | Transfer only what bank capacity accepts, debit exactly that count atomically, reject at zero charges, and reject Ultimate Ironmen as documented |

## 7. Modernization implementation plan

### Wave 1 — restore the real start and shared library service

1. Inventory every eligible Arceuus Library shelf, book title, customer,
   Biblia hint, and native library varbit before defining the data model.
2. Implement the 80–100-minute shared book-location shuffle and deterministic
   test control without hard-coding a quest-only shelf.
3. Bind every eligible shelf through one data-driven Search service with
   capacity checks, player ownership, concurrent-player behavior, and customer
   request coexistence.
4. Implement Biblia's narrowing dialogue and preserve Logosia/In Search of
   Knowledge plus Sam, Professor Gracklebone, and Villia subjects.
5. Render the complete pinned `Soul journey` transcript through the shared
   modern book interface; on qualifying first read, show the artefact message
   and atomically write state 1.
6. Enforce disappearing Drop, no-bank, no-drop-trick, reacquisition, and
   customer hand-in behavior for the book.
7. Expand state-0/1 journal text to distinguish finding, owning, and reading.

### Wave 2 — rebuild Aretha, crypt travel, and digging

1. Replace Aretha's name-only synopsis with a shared stage-aware subject menu
   covering soul lore, her purpose, quest offer/re-talk/postquest, exit, and
   Treasure Trails.
2. Implement the pinned offer dialogue, Yes/No choice, refusal loop, and
   interruption-safe state-2 acceptance commit.
3. Verify both church stair pairs and their exact landings; add shared maplink
   rows only where the ±1-plane fallback is wrong.
4. Define the complete church-crypt dig zone in data and gate
   `~bys_try_dig` before any item mutation.
5. Make the shared spade action animate/delay once, return a precise consumed
   result, handle full inventory, and coexist with every earlier/later dig.
6. Implement damaged-bearer Check, Destroy, drop/ground/bank ownership, and
   state-2 re-dig without duplicates.
7. Add post-completion crypt reclaim according to the pinned usable-item
   contract and exact ownership checks.

### Wave 3 — restore the Key Master route and repair transaction

1. Verify dusty-key acquisition/gate, keyring behavior, Taverley entry/exit,
   the boostable 70/80 Agility alternatives, and cave entrance round-trip.
2. Implement Key master teleport through the shared teleport-scroll service,
   including restrictions, destination, animation, and consumption timing.
3. Add one persistent Key Master spawn at the cache-authoritative lobby
   coordinate and verify uniqueness across reload/reconnect.
4. Build one Key Master dispatcher for ordinary backstory, Slayer/task and
   Cerberus gates, Listen, clue, miniquest repair, and post-repair dialogue.
5. Reproduce the repair transcript/visual and atomically exchange exactly one
   damaged item for one Soul bearer.
6. Make completion state, zero points/XP, reward item, modern scroll, and
   repeated/reconnect behavior idempotent and exactly once.
7. Update the state-2 journal from item/location context: dig, travel, enter
   cave, or speak to Key Master.

### Wave 4 — implement the complete Soul bearer reward

1. Store charges with the reusable per-item charge framework using the native
   charge dbrow/quantity field; cap at 1,000.
2. Implement amount selection and bank Configure-Charges with one blood rune
   plus one soul rune per successful charge and exact cap/material clamping.
3. Bind Fill to the complete authoritative ensouled-head set and atomically
   move only bankable heads while draining exactly one charge per transferred
   head.
4. Implement zero-charge, no-head, bank-full/partial-capacity, duplicate
   packet, noted-item, and Ultimate Ironman refusals.
5. Implement Check and Uncharge, preserving/refunding exact charge state under
   the pinned policy and across inventory/bank transfers.
6. Implement the confirmation preference introduced for repeated Fill use,
   using native state when available rather than a quest-specific toggle.
7. Add charged-item and bank round-trip self-tests, then regression-test
   reanimation so banking a head does not consume or reanimate it accidentally.

Do not add quest-specific C code for library shuffling, book panels, spade
regions, maplinks, teleport scrolls, NPC subject arbitration, item-var charges,
or bank transfers. If a general capability is missing, add the smallest
reusable engine/service primitive, prove it independently, and keep Bear Your
Soul policy in RuneScript/config data.

## 8. Verification contract

### Static and pack verification

- `python3 tools/questhelper_extract.py bearyoursoul --check`;
- assert all primary state writes are 0–3 and `arceuus_soulbearer_oldstory` is
  either migrated/documented or intentionally untouched;
- assert at least one production path—not debug—writes state 1 and exactly one
  acceptance transaction writes state 2;
- assert `~bys_try_dig` has an explicit church-crypt area predicate before any
  add/delete and a distinct post-completion reclaim path;
- assert Key Master has one canonical spawn and one shared subject dispatcher;
- assert Soul journey, damaged bearer, Soul bearer, and Key master teleport
  have handlers for every advertised operation;
- assert no `Soft`, deferred library/dusty-key marker, global dig, unbound item
  op, unchecked add/state pair, or direct debug completion remains
  undisclosed;
- assert dbrow, journal, cheat adapter, end state, reward icon/text, charge row,
  and completion call agree;
- `make -C src mock230-scripts`; and
- `mock230_pack --check-only` against the intended cache.

### Automated route matrix

At minimum, test:

1. every eligible/ineligible library shelf across two controlled shuffles,
   shuffle boundary 80/100 minutes, concurrent players, empty/full inventory,
   repeated Search, and all customer assignments;
2. Biblia hints on each floor/section, no active request, Soul journey request,
   customer request coexistence, and Logosia/In Search of Knowledge dispatch;
3. all 18 book pages, close/reopen, first-read message, early close, repeated
   read at states 0–3, Drop disappearance, bank rejection, customer hand-in,
   and reacquisition;
4. Aretha before read, at state 1, refusal, acceptance, interruption, repeated
   packets, soul-lore subjects, state-2 re-talk before/after dig, postquest,
   and medium-clue arbitration;
5. both church stair directions/planes, exact landings, blocked movement,
   simultaneous players, and route availability at states 0–3;
6. spade outside every crypt boundary, on every valid crypt tile, absent/wrong
   item, full inventory, existing damaged item in inventory/bank/ground,
   Destroy/drop/re-dig, relog, and duplicate packets;
7. Velrak/dusty-key route, locked/unlocked gate from both sides, keyring if
   supported, exact 70/80 Agility boundaries with boosts/drains, and no
   accidental Slayer requirement;
8. Key master teleport success, blocked destination, animation/interruption,
   exact one-item consumption, and cave entrance/exit round-trip;
9. Key Master spawn uniqueness, ordinary subjects, task/no-task Cerberus
   branches, Listen, master clue, no damaged item, valid repair, full
   inventory, and repeated/reconnect repair packets;
10. completion interrupted before/after exchange, state, scroll, and registry
    updates; repeated Key Master clicks; exact 0 QP/XP; one usable reward;
11. post-completion reclaim with item in inventory/bank/ground, no item, full
    inventory, repeated digs, Drop, relog, and duplicate prevention;
12. charging with zero/partial/exact/excess blood and soul runes, amount input,
    999/1,000 charge boundaries, duplicate packets, bank configuration, and
    inventory/bank charge preservation;
13. Fill with every eligible ensouled head, mixed types/stacks, no heads, zero/
    partial/exact charges, full/partially-full bank, noted or invalid objects,
    confirmation preference, duplicate packets, and exact debit/transfer;
14. Ultimate Ironman rejection with no item/charge mutation; and
15. Check/Uncharge at 0/1/1,000 charges, exact rune return under the pinned
    policy, full inventory, bank round-trip, loss/reclaim, and reanimation
    regression.

### Live-client evidence

Capture a real-client run from a clean state through reward use and loss
recovery without state/debug commands. Evidence must include:

- a live library shuffle, shelf searches, Biblia hint, item acquisition, all
  book pages, first-read message, Drop/bank restrictions, and journal change;
- Aretha's soul subjects, refusal then acceptance, church stair round-trip,
  outside-crypt dig rejection, valid dig animation, full-inventory response,
  damaged-item Check/Destroy/recovery, and journal branches;
- the dusty-key and one alternative Taverley route, Key master teleport, cave
  entry/exit, persistent Key Master, ordinary/Listen/quest subjects, repair
  visual, and interruption/reconnect boundary;
- exact completion with no QP/XP, one usable Soul bearer, and repeated
  interactions causing no duplicate reward;
- charging, bank Configure-Charges, Check, partial and full-bank Fill across
  multiple head types, exact charge drain, Uncharge, and UIM refusal; and
- Drop/loss followed by crypt reclaim, with the replacement preserving the
  same complete utility contract.

Only after static checks, automated matrices, pack validation, and live-client
evidence pass may this record change from `audit-pending` to `modernized`.
