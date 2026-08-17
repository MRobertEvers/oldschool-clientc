# Biohazard modernization audit

Status: `audit-pending` — the cache-native quest row, primary state ladder,
errand-boy bitfield, public actors, journal, most dialogue, item route, Varrock
inspection, completion, West Ardougne gate, Combat Training Camp, and
Underground Pass dependency exist. The legitimate route is nevertheless
hard-blocked at the first distraction because the watchtower handler binds its
multivar wrapper rather than the visible interactive leaf. The mourner cauldron
has the same defect later. The prerequisite row names Rag and Bone Man I instead
of Plague City, the canonical gas-mask and medical-gown equipment rules are not
enforced, quest items can leak or be lost, Asyff's free priest outfit is absent,
and the advertised West Ardougne teleport is usable before completion.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to Elena, Jerico, Omart, Kilron, the
watchtower distraction, West Ardougne, Mourner Headquarters, the Chemist,
Hops/Chancy/Da Vinci, Varrock's east gate, Asyff, Julie, Guidor, King Lathas,
the Combat Training Camp, the West Ardougne teleport, the journal, recovery,
completion, and every shared-quest dispatcher. It is an implementation
specification, not completion evidence.

## 1. Authoritative references

These stable OSRS Wiki revisions define the current route, dialogue, item
lifecycle, travel restrictions, reward, and permanent unlocks.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Biohazard](https://oldschool.runescape.wiki/w/Biohazard?oldid=15256425) | 15256425, 2026-07-07 | Identity, requirements, ordered route, transport rules, rewards, and post-2019 item behavior |
| [Biohazard/Quick guide](https://oldschool.runescape.wiki/w/Biohazard/Quick_guide?oldid=15256426) | 15256426, 2026-07-07 | Exact preparation, dialogue choices, handoffs, equipment, and recovery route |
| [Transcript:Biohazard](https://oldschool.runescape.wiki/w/Transcript%3ABiohazard?oldid=15263261) | 15263261, 2026-07-14 | Start/refusal, first/repeat dialogue, equipment rejection, item loss/capacity, finale, and ordinary subjects |
| [Plague sample](https://oldschool.runescape.wiki/w/Plague_sample?oldid=15185815) | 15185815, 2026-04-22 | Duplicate cleanup, replacement, border inspection, and modern teleport behavior |
| [Guidor](https://oldschool.runescape.wiki/w/Guidor?oldid=14806539) | 14806539, 2024-11-23 | Priest disguise, diagnosis, item consumption, and shared post-quest dialogue |
| [Asyff](https://oldschool.runescape.wiki/w/Asyff?oldid=15006217) | 15006217, 2025-10-16 | One-time free priest outfit, normal shop, and overlapping quest subjects |
| [Mourner Headquarters](https://oldschool.runescape.wiki/w/Mourner_Headquarters?oldid=15302196) | 15302196, 2026-08-15 | Gown-only entry, sick mourner/key, locked gate, and exact upstairs crate |
| [Medical gown](https://oldschool.runescape.wiki/w/Medical_gown?oldid=15182973) | 15182973, 2026-04-22 | Cupboard source, required disguise, and replacement |
| [Key (Biohazard)](https://oldschool.runescape.wiki/w/Key_(Biohazard)?oldid=15238132) | 15238132, 2026-06-23 | Mourner drop, headquarters gate, and quest-only ownership |
| [Touch paper](https://oldschool.runescape.wiki/w/Touch_paper?oldid=15185077) | 15185077, 2026-04-22 | Chemist grant/replacement and Guidor cleanup |
| [Combat Training Camp](https://oldschool.runescape.wiki/w/Combat_Training_Camp?oldid=15302179) | 15302179, 2026-08-15 | Completion gate, six 50-XP dummies, and permanent access |
| [West Ardougne Teleport](https://oldschool.runescape.wiki/w/West_Ardougne_Teleport?oldid=14961024) | 14961024, 2025-08-07 | Biohazard completion gate for spell/tablet and destination contract |

The sources identify Biohazard as a novice, short members quest released 23
October 2002 and the second quest in the Elf series. Plague City is its only
quest prerequisite. A gas mask is required; a level 13 Mourner is fought. The
recommended combat level in the local cache row is 10.

Transition aid only: the local Quest Helper checkout's
[`Biohazard.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/biohazard/Biohazard.java)
and
[`GiveIngredientsToHelpersStep.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/biohazard/GiveIngredientsToHelpersStep.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirm states
0/1/2/3/4/5/6/7/10/12/14/15/16, all route coordinates, the three errand-boy
assignments, the free-outfit bit, and both unlocks. They guide transition tests
but do not override the Wiki, transcript, or osrs239 cache.

`python3 tools/questhelper_extract.py biohazard --check` resolves every Quest
Helper item, NPC, loc, coordinate, varbit, and `quest_biohazard` row.

### Reference-era rule

The current Chemist script preserves obsolete pre-2019 behavior in which the
player can announce the plague sample and have it confiscated. Current OSRS no
longer destroys the sample or vials when teleporting and the Chemist's quest
subject does not confiscate the sample. Modernization must follow the pinned
current references, not preserve an old-engine hazard merely because the port
contains it.

## 2. Native quest identity and player contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 39 |
| Type | Members quest |
| Difficulty / length | Novice / short |
| Series | Elf series, part 2 |
| Release date | 23 October 2002 |
| Start | Elena in East Ardougne, approximately 2592,3336,0 |
| Prerequisite | Plague City complete |
| Required item | Gas mask; ordinary route later supplies or offers the other quest items |
| Combat | One level 13 Mourner |
| Primary state | `%biohazard`, clean permanent varp 68, states 0–16 |
| Side state | `%bioerrand` plus four native Biohazard varbits on `elenaquest_extra_bits` |
| Quest points | 3 |
| Experience | 1,250 Thieving XP; native value 12,500 tenths |
| Unlocks | Combat Training Camp, free West Ardougne gate passage, West Ardougne Arceuus teleport and tablet, and Underground Pass start |
| End state | 16 |

The cache `requirement_quests` value is corrupt. It points to row 109,
`quest_ragandboneman1`; Plague City's row is 36. The Elena transform becomes
visible after she returns home in Plague City, but visibility is not a canonical
completion gate and must not substitute for the corrected dbrow plus an
explicit pre-mutation requirement check.

### Native primary ladder

| State | Constant | Required phase |
| ---: | --- | --- |
| 0 | `biohazard_not_started` | Ordinary Elena dialogue; explicit accept/refuse after Plague City |
| 1 | `biohazard_started` | Ask Jerico how to cross the wall |
| 2 | `biohazard_spoken_jerico` | Obtain feed/cage and feed the watchtower |
| 3 | `biohazard_used_birdfeed` | Release the pigeons in the authored zone |
| 4 | `biohazard_released_pigeons` | Wear gas mask and cross with Omart |
| 5 | `biohazard_climbed_ladder` | Poison the mourner stew |
| 6 | `biohazard_poisoned_stew` | Obtain gown, defeat the sick Mourner, unlock headquarters |
| 7 | `biohazard_found_distillator` | Return Elena's distillator |
| 10 | `biohazard_given_distillator` | Visit Chemist and obtain touch paper |
| 12 | `biohazard_spoken_chemist` | Smuggle the three vials, disguise, and consult Guidor |
| 14 | `biohazard_found_secret` | Report the diagnosis to Elena |
| 15 | `biohazard_reported_elena` | Confront King Lathas and accept the continuing mission |
| 16 | `biohazard_complete` | Permanent rewards and Underground Pass availability |

States 8, 9, 11, and 13 are intentionally absent from the native ladder. Do
not renumber the quest to make it visually contiguous.

### Side-state inventory

`%bioerrand` is a permanent bitfield. Bits 1–3 mean Hops/Chancy/Da Vinci
received the correct vial, bits 4–6 record that each helper received a vial,
and bits 7–9 record a wrong vial. The state must be reset coherently by test
adapters and may only clear a correct entitlement after a successful retrieval.

| Varbit | Bit | Required ownership |
| --- | ---: | --- |
| `%biohazard_met_omart` | 8 | First introduction versus repeat wall-crossing dialogue |
| `%biohazard_met_julie` | 9 | Guidor's wife first/repeat disguise conversation |
| `%biohazard_free_clothes` | 10 | Asyff's one-time free priest outfit |
| `%biohazard_postquest_chat` | 11 | Native one-time post-quest dialogue; owner/event requires live-client confirmation |

All four varbits exist in the cache and none is read or written by production
code. Use the first three for the evidenced subjects above. Do not assign the
fourth based on its name alone; capture the live transition and actor before
making it part of completion verification.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_biohazard/configs/quest_biohazard.constant` | Errand bits and wall destinations | Constants exist; primary constants are misleadingly owned by Combat Training Camp |
| `configs/quest_biohazard.varp` | Permanent `%bioerrand` carrier | Present, but has no completion/cheat reset policy |
| `scripts/biohazard_journal.rs2` | Dynamic journal | Covers the main ladder but cannot expose broken interaction leaves, equipment, exact item recovery, or side bits |
| `scripts/jerico.rs2` | Jerico route and guidance | Broad phase progression exists; dialogue is abbreviated |
| `scripts/quest_biohazard_locs.rs2` | Cupboards, distraction, wall travel, cauldron, HQ gate/crate | Both key multivar interactions bind wrappers; gas-mask rule and post-distillator route limits are absent; grants are capacity-unsafe |
| `scripts/chemist.rs2` | Chemist quest branch and touch paper | Uses stale confiscation dialogue and unsafe grant ordering |
| `scripts/errand_boys.rs2` | Rimmington handoffs and Varrock retrieval | Correct assignment matrix exists; failed/full-inventory retrieval can destroy entitlement; Chancy's wrong-vial coins are absent |
| `scripts/guidors_wife.rs2` | Priest-disguise door | Worn-set check exists; native first/repeat bit is unused |
| `scripts/guidor.rs2` | Sample validation and diagnosis | Core check exists; duplicate/bank cleanup is incomplete and mutation is not transactional |

The quest root totals 952 lines across nine files. Its comments describe a
direct LostCity port and several deliberately dropped effects. Port provenance
is useful discovery evidence, not proof that old machinery matches current
OSRS behavior.

### Mandatory shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `configs/all.dbrow` | Quest metadata | ID/reward/end state are sound; requirement row is corrupt |
| `areas/area_combat_training/configs/combat_training.{constant,varp}` | `%biohazard` and primary constants | Correct values, wrong ownership boundary; move or clearly centralize without duplicating symbols |
| `areas/area_ardougne_east/scripts/elena.rs2` | Start, distillator exchange/recovery, Guidor report | No explicit prerequisite gate; four-item handoff advances with partial grants and uses an incorrect free-space test |
| `areas/area_ardougne_east/scripts/king_lathas.rs2` | Finale, reward, Underground Pass and other quest dispatch | Finale exists but completion queue is unguarded; Dragon Slayer II, Making History, SOTE, Regicide, and Underground Pass priority needs matrix tests |
| `areas/area_ardougne_west/scripts/mourner.rs2` | Sick Mourner combat/key | Death callback grants a key without quest-state ownership; full inventory can lose the drop |
| `areas/area_ardougne_west/scripts/doors.rs2` | West gate and HQ disguise rules | Completion gate exists; gown must remain equipped while infiltrating |
| `areas/varrock/scripts/east_gate.rs2` | Chemical inspection | Entry confiscation exists; exit warning/confirmation is absent and nearby-NPC lookup can make enforcement bypassable |
| `quests/quest_eaglepeak/scripts/asyff.rs2` | Fancy-clothes shop and Eagle's Peak | No Biohazard subject or free priest outfit despite the native bit and current reference |
| `skill_magic/scripts/spells/arceuus_teleport.rs2` | West Ardougne spell | Direct dispatch has no Biohazard completion gate |
| `skill_magic/scripts/spells/teleport_tablet.rs2` | West Ardougne tablet | Direct dispatch has no Biohazard completion gate |
| `areas/area_combat_training/scripts/combat_training_camp.rs2` | Reward gate and six Attack dummies | Access is gated at 16 and six 50-XP dummy hits are modeled; retain and regression-test |
| `quests/quest_upass/scripts/upass_entrance.rs2` | Downstream dependency | Correctly blocks before Biohazard 16 |
| `interface_questjournal/scripts/quest_journal.rs2` | Journal dispatcher | Correctly dispatches to `~biohazard_journal` |
| `quests/scripts/quest_cheat.rs2` | Completion/test adapter | No Biohazard arm exists |
| Map spawns and multivar loc/NPC configs | Elena, Jerico, Omart/Kilron, mourners, Chemist/helpers, Guidor/Julie, Asyff, King | All critical public actors exist; interactive leaves and exact crate identity must be validated, not inferred from wrapper names |

Biohazard also shares Elena with Mourning's End Part I, Asyff with Eagle's
Peak, the Chemist with Regicide/ordinary subjects, King Lathas with Underground
Pass/Regicide/Making History/Dragon Slayer II/Song of the Elves, and West
Ardougne with Plague City and later Elf-series quests. Every shared NPC needs a
single deterministic subject dispatcher; quest modernization must not add a
second conflicting trigger.

## 4. Native reachability and first blockers

The `biowatchtower` cache loc is a multivar wrapper keyed by `%biohazard`. At
state 2 the player sees and clicks `biowatchtower_op`, but the production
`oploc1` and `oplocu` handlers bind `biowatchtower`. Interaction packets name
the visible leaf, as does Quest Helper. Bird feed therefore cannot be applied
and state 2 cannot reach 3 through the legitimate route.

The same defect recurs at state 5: `mournercauldron` resolves to
`mournercauldron_op`, while production binds `oplocu,mournercauldron`.
Modernization must bind the visible leaves, retain the native transforms, and
prove both examine/investigate and item-on-loc packets in a live server test.

| State | Current implementation / defect |
| ---: | --- |
| 0 | Elena is visible through Plague City's side bit, but neither dialogue nor metadata proves Plague City completion before writing 1 |
| 1 | Jerico advances to 2 and supplies direction; cupboard and map-ground cage sources exist |
| 2 | Hard-blocked: watchtower feed handler is attached to the wrapper, not `biowatchtower_op` |
| 3 | Pigeon zone/use flow consumes full cage, returns empty cage, and writes 4; authored projectile/sound staging is dropped |
| 4 | Omart crosses without a worn gas mask and never uses the native first/repeat bit |
| 5 | Hard-blocked independently: cauldron handler is attached to the wrapper, not `mournercauldron_op` |
| 6 | Gown/key/gate path exists, but the Mourner can leak the key outside this phase and disguise removal is not policed |
| 7 | Crate may be any matching loc name and can grant out of phase; Elena advances before an unsafe four-item grant |
| 10 | Chemist follows obsolete confiscation behavior; replacement remains possible but touch paper lacks a safe full-inventory outcome |
| 12 | Assignment matrix is present; retrieval can clear entitlement even if the vial was not added; Asyff free set is absent |
| 14 | Guidor's diagnosis reports to Elena; exact duplicate/bank cleanup is wrong |
| 15 | King Lathas presents the full explanation and explicit continue/refuse choice; completion queue lacks an internal state guard |
| 16 | West gate, Combat Training Camp, and Underground Pass work; spell/tablet are not gated and thus leak before 16 |

## 5. Current versus required playable route

| Chapter | Current route | Modernized route contract |
| --- | --- | --- |
| Start | Elena can start whenever her carrier is visible | Correct dbrow to Plague City row 36, explicitly test completion, preserve ordinary Elena/Mourning's End subjects, and write 1 only after Yes |
| Jerico/distraction | Cupboard/cage work; wrapper binding blocks feed | Bind the visible tower leaf, preserve item recovery/bank uniqueness, play authored reaction staging, validate zone, then commit 2→3→4 |
| Wall crossing | Omart merely recommends a mask and offers repeat crossings indefinitely | Require worn gas mask, reject/guide without it, use `met_omart`, prevent removal while the infection zone requires it, and stop Omart re-entry after the distillator route closes |
| Stew/disguise | Wrapper blocks cauldron; gown is checked only at the HQ door | Bind visible cauldron leaf; capacity-safe gown replacement; require the gown throughout HQ infiltration and eject/reject on removal |
| Mourner/key | Any kill of the shared sick Mourner can grant the key | Arm the hostile encounter only in state 6 with worn gown, credit the killer/owner, grant inventory-or-ground, and keep replacement possible without leaking quest items |
| Headquarters | Named crate grants a distillator with broad conditions | Resolve the exact third-from-left crate/coordinate, require state 6/7, advance only with a successful inventory-or-ground outcome, and keep the gate key reusable |
| Elena analysis | Deletes distillator, writes 10, then conditionally adds four items with `freespace > 1` | Require the net three free slots or use an atomic authored fallback; commit state and all items together; provide exact, duplicate-aware recovery |
| Chemist | Wrong dialogue can confiscate the sample | Use the current quest subject, never confiscate for dialogue/teleport, grant or ground touch paper safely, and advance only after a recoverable outcome |
| Rimmington handoff | Correct/wrong matrix mostly exists | Preserve correct assignments; remove authored bank copies at handoff; wrong Chancy handoff returns 10 coins with inventory-or-ground fallback; never set bits without consuming exactly the offered vial |
| Varrock collection | Correct entitlement clears even if inventory add fails | Capacity-check first, grant then clear bits atomically, preserve retry and Elena recovery, and allow order-independent collection |
| Border gate | Carried chemicals are confiscated on guarded entry; exit is silent | Make inspection authoritative at the crossing and add the current exit warning/Yes-No route while vials are held |
| Asyff/Julie | Shop can sell the outfit; no free set; Julie only checks worn items | Add a deterministic Biohazard subject and one-time, two-slot-safe free priest set; set `free_clothes` only after grant; use `met_julie` for first/repeat dialogue |
| Guidor | Checks sample/vials/paper then deletes one inventory copy of each | Validate the full set without mutation, run diagnosis, apply the current all-copy inventory/bank cleanup policy atomically, then write 14 |
| Finale | Full exposition calls an unguarded completion queue | Recheck state 15 inside the queue, write 16 exactly once with 1,250 Thieving XP/3 QP, preserve refusal/re-talk, and establish all permanent unlocks |
| Rewards | Gates/camp work; teleport spell and tablet work for everyone | Gate spell and tablet on 16 with correct failure text, retain West gate/camp, verify six dummy XP awards, and prove Underground Pass becomes available |

## 6. Item lifecycle and transaction rules

| Item family | Required behavior |
| --- | --- |
| Bird feed / pigeon cage | One usable feed source with bank-aware replacement; cage is a map item; releasing pigeons returns the empty cage without requiring an extra slot |
| Rotten apple / medical gown | Apple is consumed only by the active cauldron interaction; gown is replaceable while needed and enforced as worn inside HQ |
| Mourner key | Quest-phase, credited Mourner drop; reusable on the gate; inventory-or-ground and recoverable if lost |
| Distillator | Exact crate only; one active copy; Elena consumes it only as part of a successful analysis transaction |
| Sample and three vials | Elena grant/recovery is atomic; teleporting does not destroy them; Varrock guards confiscate carried chemicals on prohibited entry, not sample/paper |
| Errand-boy chemicals | Correct helper owns the chemical until successful Varrock retrieval; bank copies follow the pinned item cleanup rule; a full inventory never clears entitlement |
| Wrong vial | Is lost as authored; Chancy additionally returns 10 coins with ground fallback |
| Touch paper | Chemist supplies/replaces it; full inventory yields a recoverable result; Guidor consumes all applicable copies after diagnosis |
| Priest outfit | Asyff gives the one-time free set if two slots/outcome are available; normal shop remains usable; Julie checks both pieces worn |

All item/state transitions should use the modern check → reserve → mutate →
commit pattern. Never write the next state or clear an entitlement before every
required delete/add/ground outcome has succeeded. Tests must cover inventory
0/1/2/3/4 free slots, bank duplicates, dropped items, death, reconnect, repeated
packets, and two players performing the same public-NPC phase concurrently.

## 7. Oversight register

### P0 — release blockers and integrity

1. **The canonical route is hard-blocked at state 2.** The watchtower handlers
   bind `biowatchtower`, but the clickable transform is `biowatchtower_op`.
2. **A second deterministic blocker exists at state 5.** The cauldron handler
   binds `mournercauldron`, but the clickable transform is
   `mournercauldron_op`.
3. **Eligibility metadata is false.** The quest requires Rag and Bone Man I in
   the cache and has no explicit Plague City completion gate.
4. **The reward teleport leaks globally.** Both spell and tablet dispatch
   without testing `%biohazard >= 16`.
5. **The Mourner key leaks outside the quest phase.** The death callback is not
   state-, disguise-, encounter-, or owner-gated.
6. **Quest transactions can strand players.** Elena advances with partial item
   grants and errand-boy retrieval clears entitlement after a failed `inv_add`.
7. **Completion is not idempotent.** The queued reward has no internal state
   guard and can duplicate XP/reward handling if invoked twice.

### P1 — fidelity, recovery, and shared systems

1. Worn gas mask is not required at Omart and cannot be enforced after crossing.
2. Medical gown is not enforced after HQ entry; a player can remove the disguise
   inside.
3. Omart continues offering wall entry after the distillator stage, contrary to
   the route's post-crossing closure.
4. Chemist dialogue/confiscation is obsolete current-OSRS behavior.
5. Asyff never offers the native one-time free priest outfit.
6. Julie/Omart first/repeat varbits and the post-quest varbit are unused.
7. Chemical/sample/touch-paper bank and duplicate cleanup is incomplete.
8. Chancy omits the 10-coin wrong-vial return.
9. Varrock gate lacks the exit warning and relies on a nearby guard lookup for
   entry enforcement.
10. The exact third-from-left headquarters crate is not proven; a name-only
    handler may bind more than the authored source.
11. Full-inventory gown, key, distillator, touch-paper, priest-set, and vial
    outcomes are unsafe or silent.
12. King Lathas and Elena/Asyff/Chemist shared dispatchers lack a complete
    cross-quest priority regression matrix.

### P2 — presentation and maintainability

1. Primary constants/varp are owned under Combat Training Camp while the quest
   root owns only errand state, obscuring the state boundary.
2. Port headers still normalize dropped projectiles, sound, and cutscene staging.
3. Dialogue is abbreviated at Jerico, Omart/Kilron, mourners, helpers, and
   recovery branches; first/repeat distinctions are flattened.
4. The journal reports broad phases but not actionable recovery/equipment
   conditions.
5. There is no coherent Biohazard `::complete`/state test adapter.
6. `%biohazard_postquest_chat` has no proven owner; it needs live evidence and a
   named test before use.

## 8. Modernization packages

### Package 0 — state, metadata, and shared dispatch

- correct `requirement_quests` to Plague City row 36 and add the explicit start
  guard before any dialogue mutation;
- establish a single quest-owned definition for primary and side state without
  changing cache IDs or native bit positions;
- document `%bioerrand` and all four native side-varbit ownership rules;
- define deterministic subject priority for Elena, Chemist, Asyff, Julie, and
  King Lathas; and
- add a test adapter that can set every primary phase plus coherent side/items,
  while completion mode establishes exactly the permanent unlock state.

Exit evidence: cache compile, symbol ownership check, prerequisite tests for
Plague City 35/in-progress/complete, and a shared-NPC dispatch matrix.

### Package 1 — start, distraction, and wall travel

- modernize Elena/Jerico dialogue and explicit start acceptance;
- bind `biowatchtower_op` for investigate and feed, validate item/zone, and
  restore observable mourner/pigeon staging;
- make feed/cage acquisition and recovery capacity-safe; and
- implement first/repeat Omart state, worn gas-mask rejection, in-zone equipment
  enforcement, authored east/west crossings, and post-distillator closure.

Exit evidence: states 0→5 from a clean eligible save, every refusal/re-talk,
wrong item/zone, no mask/mask removed, reconnect, and repeated packet.

### Package 2 — Mourner Headquarters

- bind `mournercauldron_op` and commit the rotten-apple transaction only at 5;
- implement gown recovery and continuous disguise enforcement;
- owner/state-gate the level 13 Mourner encounter and key drop;
- identify and bind the exact authored crate, with safe distillator recovery;
  and
- preserve shared doors and later Mourning's End behavior.

Exit evidence: 5→7 with 0/1 free slots, wrong mourner, competing players,
death/logout, gown removal, key loss, wrong crate, and repeat search tests.

### Package 3 — Elena, Chemist, and quest-item service

- replace partial grants with atomic capacity-aware analysis/recovery;
- modernize the Chemist to the current quest subject, safe touch-paper grant,
  and no confiscation/teleport breakage; and
- centralize exact sample/vial/paper duplicate and bank-cleanup rules so Elena,
  helpers, gate guards, and Guidor cannot disagree.

Exit evidence: exhaustive free-slot/bank/drop/death/reconnect matrix through
7→10→12 and current teleport behavior tests.

### Package 4 — smuggling, disguise, and diagnosis

- transact all six correct/wrong Rimmington branches and all Varrock retrievals;
- add Chancy's 10 coins and authoritative east-gate entry/exit behavior;
- integrate Biohazard into Asyff's existing subject menu, granting the one-time
  priest set safely while preserving shop and Eagle's Peak;
- use Julie's native first/repeat bit and enforce both worn pieces; and
- make Guidor's diagnosis and cleanup atomic before state 14.

Exit evidence: all 3×3 helper assignments, every collection order, full
inventory, duplicate/bank copies, gate direction, outfit already owned/free
grant/shop, Eagle's Peak overlap, and Guidor missing-item matrix.

### Package 5 — finale and permanent rewards

- preserve the full Elena/King exposition and explicit refusal/re-talk;
- make completion state-guarded and once-only;
- gate the West Ardougne spell and tablet at 16;
- verify West gate, Combat Training Camp, six 50-XP dummy awards, and Underground
  Pass start; and
- resolve/capture the native post-quest-chat transition without inventing it.

Exit evidence: completion duplication/race tests; exact 3 QP and 1,250 Thieving
XP; spell/tablet at 15 versus 16; gate/camp/upass at 15 versus 16; post-quest
Elena/King/shared-quest matrices.

## 9. Verification matrix

### Static gates

- compile configs and RuneScript with no duplicate triggers or unresolved
  wrapper/leaf symbols;
- rerun `python3 tools/questhelper_extract.py biohazard --check`;
- prove every critical public actor and exact interactive leaf is placed;
- scan all writes to `%biohazard`, `%bioerrand`, and the four native side bits;
- scan every producer/consumer/remover of feed, cage, apple, gown, key,
  distillator, sample, vials, paper, and priest outfit; and
- prove spell/tablet, West gate, camp, and Underground Pass share the same
  completion predicate.

### Automated route gates

1. Clean route 0→16 with Plague City complete and no pre-owned optional items.
2. Refusal and re-talk at Elena, Omart, crossings, Chemist, helpers, Julie,
   Guidor, Elena report, and King Lathas.
3. Save/reconnect at every primary state and after each `%bioerrand` bit pattern.
4. Inventory 0–4 free slots plus bank/drop/death recovery at every grant/handoff.
5. Wrong item, wrong loc leaf, wrong zone, wrong crossing direction, and repeated
   packet at every transition.
6. Gas-mask/gown/priest outfit missing, partially worn, removed after entry, and
   recovered.
7. All nine helper assignments and all six Varrock outcomes, including Chancy's
   coins.
8. Two-player Mourner/key and shared-NPC conversations; only the correct player
   may receive state/item credit.
9. Overlap matrix for Mourning's End I, Eagle's Peak, Regicide, Underground
   Pass, Making History, Dragon Slayer II, and Song of the Elves.
10. Completion invoked twice and after reconnect; XP, QP, and count change once.
11. Every reward surface at state 15 and 16, including spell and tablet.

### Live evidence required before `verified`

- packet/trigger capture for `biowatchtower_op` and `mournercauldron_op`;
- exact Mourner Headquarters crate ID/coordinate and key ground fallback;
- current equipment-removal response for gas mask and medical gown;
- current Asyff free-outfit item/capacity sequence;
- exact duplicate/bank cleanup at correct helper handoff and Guidor diagnosis;
- Varrock east-gate exit warning and entry-confiscation directionality; and
- actor/event/state transition for `%biohazard_postquest_chat`.

## 10. Completion criteria

Biohazard may move from `audit-pending` to `verified` only when Packages 0–5
are implemented, the legitimate clean route reaches 16 without cheats, both
visible-leaf blockers are proven fixed, canonical Plague City eligibility and
all disguise rules are enforced, every item transaction is recoverable and
duplicate-safe, shared NPCs preserve every other quest subject, completion is
once-only, and all four advertised permanent unlocks reject state 15 and work
at state 16. Documentation or the presence of a reward line is not evidence
that the quest or unlock is operational.
