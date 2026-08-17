# Devious Minds modernization audit

Status: `audit-pending` — the current slice reaches a locally authored reward
screen, but it does not use the native state ladder and its required route is
not playable on a clean account. It is not `verified-modern`.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to one implementation unit. It is an audit
and implementation specification, not evidence that the quest is complete.

## 1. Authoritative references

The pinned revisions below make the target reproducible even when the live Wiki
changes. Cache metadata remains authoritative for IDs, state storage, transforms,
and client-facing completion. The Wiki defines player-visible behavior. Quest
Helper is a routing/state aid only; it does not override either source.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Devious Minds](https://oldschool.runescape.wiki/w/Devious_Minds?oldid=15292315) | 15292315, 2026-08-10 | Requirements, route, enemies, rewards, and unlocks |
| [Devious Minds/Quick guide](https://oldschool.runescape.wiki/w/Devious_Minds/Quick_guide?oldid=15261955) | 15261955, 2026-07-12 | Ordered actions, travel, pouch variants, and return route |
| [Transcript:Devious Minds](https://oldschool.runescape.wiki/w/Transcript%3ADevious_Minds?oldid=15263335) | 15263335, 2026-07-14 | Start warning, choices, re-talk, replacement, and post-heist dialogue |
| [Monk (Devious Minds)](https://oldschool.runescape.wiki/w/Monk_(Devious_Minds)?oldid=15013222) | 15013222, 2025-11-01 | Impostor dialogue and orb replacement ownership |
| [Dead Monk](https://oldschool.runescape.wiki/w/Dead_Monk?oldid=15110118) | 15110118, 2026-01-21 | Paterdomus investigation interaction |
| [Doric's Whetstone](https://oldschool.runescape.wiki/w/Doric%27s_Whetstone?oldid=14862516) | 14862516, 2025-03-14 | Grinding gate and animation surface |
| [Slender blade](https://oldschool.runescape.wiki/w/Slender_blade?oldid=15184947) | 15184947, 2026-04-22 | Intermediate item behavior |
| [Bow-sword](https://oldschool.runescape.wiki/w/Bow-sword?oldid=15184944) | 15184944, 2026-04-22 | Stringing XP, wield refusal, loss, and destruction |
| [Orb (Devious Minds)](https://oldschool.runescape.wiki/w/Orb_(Devious_Minds)?oldid=15184946) | 15184946, 2026-04-22 | Pouch combination, loss, and replacement |
| [Large pouch (Devious Minds)](https://oldschool.runescape.wiki/w/Large_pouch_(Devious_Minds)?oldid=15184945) | 15184945, 2026-04-22 | Filled-pouch operations and altar destruction |
| [Colossal pouch (Devious Minds)](https://oldschool.runescape.wiki/w/Colossal_pouch_(Devious_Minds)?oldid=15235223) | 15235223, 2026-06-19 | Filled-pouch operations and altar survival |
| [Bow string spool](https://oldschool.runescape.wiki/w/Bow_string_spool?oldid=15290537) | 15290537, 2026-08-08 | Current alternate string source |
| [Relic (Devious Minds)](https://oldschool.runescape.wiki/w/Relic_(Devious_Minds)?oldid=15198433) | 15198433, 2026-04-26 | Heist target and cutscene |
| [High Priest](https://oldschool.runescape.wiki/w/High_Priest?oldid=14308948) | 14308948, 2022-08-02 | Entrana investigation owner |
| [Sir Tiffy Cashien](https://oldschool.runescape.wiki/w/Sir_Tiffy_Cashien?oldid=15196234) | 15196234, 2026-04-25 | Shared Falador Park completion owner |
| [Abyss](https://oldschool.runescape.wiki/w/Abyss?oldid=15228428) | 15228428, 2026-06-07 | Intended smuggling route and traversal |
| [Law Altar](https://oldschool.runescape.wiki/w/Law_Altar?oldid=15019931) | 15019931, 2025-11-08 | Abyss destination and Entrana exit |
| [Entrana](https://oldschool.runescape.wiki/w/Entrana?oldid=15299867) | 15299867, 2026-08-14 | Prohibited-item policy and ordinary return journey |

Quest Helper is pinned locally at commit
`5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d`; its relevant source is
`helpers/quests/deviousminds/DeviousMinds.java`. Extraction resolves the native
0, 10, 20, 30, 40, 50, 60, 70 route and completion at 80. The duplicate 30/40
altar step is ambiguous routing evidence, not permission to invent a state
transition; capture the live client/server trace before implementing those two
commits.

## 2. Native quest identity and canonical contract

The revision-239 `quest_deviousminds` dbrow is the identity and metadata source
of truth.

| Field | Native value / expected behavior |
| --- | --- |
| Quest ID | 101 |
| Type | Members' quest |
| Difficulty / length | Experienced / short |
| Series / release | Mahjarrat, series entry 11; 19 December 2005 |
| Start | Hooded monk outside Paterdomus, around 3405–3406, 3491–3492 |
| End state | `%devious_main = 80` |
| Quest points | 1 |
| Required quests | Wanted!, Troll Stronghold, Doric's Quest, and the Enter the Abyss miniquest |
| Required skills | 65 Smithing and 50 Fletching, boostable at their actions; 50 Runecraft, not boostable, at the orb/pouch action |
| Start policy | Skills are warned about but not start-gated (`requirement_check_skills_on_start = 0`); direct quest prerequisites remain start requirements |
| Recommended combat | Current Wiki: 30; cache metadata: 50. Present 30 to players and retain the discrepancy as a source-version test |
| Required items | Mithril two-handed sword, bow string or bow string spool, and a non-degraded large or colossal pouch |
| Mandatory enemies | None. Abyssal creatures are conditional travel/acquisition hazards, not quest-owned kills |
| Rewards | 1 quest point; 6,500 Smithing, 5,000 Runecraft, and 5,000 Fletching XP |
| Crafting XP | An additional 50 Fletching XP when the slender blade is strung |
| Permanent dependency | Required for Secrets of the North |

The cache's coarse `requirements_boostable = 1` cannot express the mixed rule.
The implementation must use active Smithing/Fletching levels and base Runecraft
at the specific interactions, with table-driven tests for boosts and drains.

## 3. Expected playable route

1. The hooded monk checks all four completed quest prerequisites, lists any
   missing requirements, warns about insufficient skills without refusing the
   quest, and offers the transcript's accept/refuse choice.
2. At Doric's whetstone, using a mithril two-handed sword requires an active
   Smithing level of 65. After confirmation, the player and whetstone animate
   and the sword becomes one slender blade; this grants no Smithing XP.
3. Using a bow string or supported bow string spool on the blade requires an
   active Fletching level of 50, creates one bow-sword, and grants exactly 50
   Fletching XP. The quest bow-sword cannot be equipped.
4. The monk accepts exactly one bow-sword and supplies the orb. If the orb is
   lost before the altar event, his re-talk route replaces it without enabling
   duplicates.
5. Combining the orb with a non-degraded large or colossal pouch requires a
   base Runecraft level of 50. Empty/degraded pouch state and contained essence
   are handled deliberately; reverse use orders behave identically.
6. The intended route is Mage of Zamorak to the Abyss, through a functioning
   obstacle into the inner ring, through the Law rift, and out of the Law altar
   portal to Entrana. Current alternate teleports may remain if revision-239
   content intentionally supports them, but the ordinary ferry must enforce
   Entrana's equipment restrictions and cannot be the smuggling shortcut.
7. Using the protected orb on the church altar commits an owned cutscene: three
   monks carry the relic, the assassin teleports in with the bow-sword, kills
   them and steals it, the orb explodes, and the High Priest protects himself.
   Use the cache NPCs, sequences, spotanims, objects, and Assassin Attack jingle.
8. The High Priest sends the player to Paterdomus. The dead monk is searched;
   then the player returns to Entrana by the ordinary allowed route, reports the
   discovery, and is sent to Sir Tiffy Cashien.
9. The real shared Sir Tiffy in Falador Park completes the quest exactly once,
   grants the three reward XP amounts and one quest point, and establishes the
   native end state 80 used by the client and downstream quests.

The large pouch is destroyed by the altar explosion. The colossal pouch
survives. Exact behavior for a filled pouch, pouch degradation bookkeeping,
colossal-pouch death, and the native 30/40 state boundary must be live-traced
before code is authored; these are explicit evidence gaps, not soft-skip areas.

## 4. Implementation and ownership surface

Paths in this section are relative to
`OSRS-Content/osrs239-content/` unless stated otherwise.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_deviousminds/configs/quest_deviousminds.constant` | Authored stages, requirements, XP, and coordinates | Incorrectly claims no pre-existing state readers, compresses native completion from 80 to 60, and invents a duplicate Tiffy |
| `server/scripts/quests/quest_deviousminds/scripts/deviousminds_monk.rs2` | Start, bow-sword hand-in, orb, and dead monk | Hard-gates skills at start, omits recovery/ownership, and compresses dialogue and stages |
| `server/scripts/quests/quest_deviousminds/scripts/deviousminds_items.rs2` | Whetstone, stringing, orb/pouches, altar | Omits action skill gates, item lifecycle, required XP, travel, and the real cutscene |
| `server/scripts/quests/quest_deviousminds/scripts/deviousminds_tiffy.rs2` | Hand-spawned substitute NPC and completion | False owner: duplicates Sir Tiffy and writes 60 rather than native 80 |
| `server/scripts/quests/quest_deviousminds/scripts/deviousminds_journal.rs2` | Dynamic journal | Modern renderer is good; content describes only the compressed authored route |

### Shared owners and consumers

| Path | Relationship | Required modernization |
| --- | --- | --- |
| `server/scripts/areas/entrana/scripts/high_priest_of_entrana.rs2` | Shared High Priest for Desert Treasure I, Devious Minds, and Holy Grail | Replace order-dependent early returns with explicit topics/priority; carrying a DT pot must not hide a critical Devious report |
| `server/scripts/quests/quest_recruitmentdrive/scripts/recruitmentdrive.rs2` | Owns real `rd_teleporter_guy` Sir Tiffy at 2997,3373 | Route a Devious topic after preserving active Recruitment Drive/Wanted!/Slug Menace precedence |
| `server/scripts/quests/quest_wanted/scripts/wanted_tiffy_amik.rs2` | Shared Tiffy continuation | Test every overlapping state and topic; do not replace its owner |
| `server/scripts/quests/quest_theslugmenace/scripts/slugmenace_tiffy.rs2` | Shared Tiffy continuation | Preserve its reachable branches before and after Devious completion |
| `server/scripts/skill_fletching/scripts/bows.rs2` | Claims the generic bow-string use trigger | Add both supported click directions without colliding triggers; include spool behavior |
| `server/scripts/skill_runecraft/scripts/runecraft_pouch.rs2` | Pouch capacity, degradation, filling, emptying, checking, repair | Share pouch-family state rather than transforming an item while silently losing essence/accounting |
| `server/scripts/skill_runecraft/scripts/runecraft_abyss.rs2` | Abyss obstacles and rifts | Success currently awards XP/messages but does not move the player; implement traversal and verify every passage |
| `server/scripts/skill_runecraft/scripts/runecraft.rs2` | Law altar enter/exit data | Retain working Law rift/portal coordinates and cover them in the route test |
| `server/scripts/areas/port_sarim/scripts/monk_of_entrana.rs2`, `server/scripts/areas/entrana/scripts/monk_of_entrana.rs2`, and `server/scripts/general_use/scripts/gangplank.rs2` | Entrana ferry and gangplanks | Introduce one shared prohibited-item policy; allow the post-body return while rejecting weapons/armour and pre-heist shortcuts |
| `server/scripts/player/login.rs2` | Calls `~deviousminds_tiffy_login` | Remove this call with the duplicate spawn procedure |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dispatches the native dbrow | Keep this modern registration and expand the journal proc |
| `server/scripts/quests/scripts/quest_cheat.rs2` | Writes authored completion 60 | Move to native 80 and isolate debug state from reward claims |
| `server/scripts/quests/quest_secretsofthenorth/` | Canonical downstream consumer | Add/restore its prerequisite contract and test native Devious completion |
| `docs/bosses/quest_combat_manifest.json` | Combat contract entry | Correctly classifies conditional Abyss danger, but its evidence arrays and tests are empty |

No large-pouch or colossal-pouch acquisition path was found in drops or other
content. Leech/walker spawns exist, but the pouch drop/reward ownership needed
by a clean account does not. This shared Runecraft prerequisite must be restored
before the quest can pass Gate C; injecting a quest-only free pouch would hide
the missing system and change canonical behavior.

## 5. Persisted state and transforms

### Native carriers

`%devious_main` occupies bits 0–7 of permanent, transmitted `devious_base`.
Companion fields are already cache-native:

| Field | Bits | Intended responsibility | Current use |
| --- | ---: | --- | --- |
| `%devious_main` | 0–7 | Native primary progress, complete at 80 | Authored 0–60 ladder |
| `%devious_monk_met` | 8 | Initial monk conversation | Set once, not used for recovery/dialogue |
| `%devious_monk_orb_given` | 9 | Orb handover/recovery evidence | Set once, never read |
| `%devious_cutscene` | 10 | Heist cutscene viewed/committed | Set after four message boxes |
| `%devious_altar` | 22–23 | Altar → pouch → scorched transform | Writes 1 then 2 in the same interaction |
| `%devious_monk` | 27–28 | Hooded/dead monk multinpc transform | Correct cache transform, switched during message sequence |
| `%cave_horror_warning` | 29 | Unrelated shared setting | Must be preserved during migration/reset |

The altar carrier audit finds one declared/transmitted carrier, two loc variants,
one placement, four representable states, and active reads/writes. The monk
shells are both statically placed and the native multinpc controls visibility.
Never write or clear the whole `devious_base` varp: that would corrupt the
unrelated cave-horror warning.

### Native stage contract and trace gaps

| Native value | Quest Helper routing meaning | Migration requirement |
| ---: | --- | --- |
| 0 | Not started | Preserve |
| 10 | Make the complete bow-sword | Accepted/start state |
| 20 | Talk to the monk | Preserve the native intermediate rather than skipping it |
| 30 | Take protected orb to Entrana altar | Determine exact bow-sword/orb commit by live trace |
| 40 | Altar/cutscene follow-up | Determine exact cutscene commit by live trace |
| 50 | Go to the dead monk | Require High Priest direction before body investigation |
| 60 | Return to Entrana | Dead monk investigated |
| 70 | Talk to Sir Tiffy | High Priest report complete |
| 80 | Complete | Only permanent completion value |

Quest Helper's step labels do not establish the precise 20→30 or 30→40 write
moment. Record varbit changes from a current live playthrough, including logout
at the item exchange and every cutscene boundary, before freezing these writers.

### Compatibility mapping for existing accounts

The current slice may already have persisted the following non-native meanings:

| Legacy value | Legacy meaning | Provisional native target |
| ---: | --- | ---: |
| 0 | Not started | 0 |
| 10 | Accepted | 10 |
| 20 | Bow-sword exchanged/orb issued | Likely 30; confirm native exchange trace |
| 30 | Message-only heist complete | 50 after validating altar/cutscene flags |
| 40 | Dead monk searched | 60 |
| 50 | High Priest report | 70 |
| 60 | Local completion or debug cheat | 80 only when reward evidence proves an organic claim |

Implement migration as a one-time, idempotent compatibility adapter. Reconcile
primary state against `%devious_monk_orb_given`, `%devious_cutscene`, altar and
monk transforms, quest-item ownership across inventory/worn/bank/ground, and
available reward/QP/XP history. Legacy 60 is ambiguous because both gameplay
and `::complete` wrote it while only gameplay granted rewards. Do not blindly
grant rewards or mark every legacy 60 complete. Quarantine unresolved accounts
for explicit repair telemetry. Preserve unrelated bits and remove duplicate
quest items without destroying legitimate pouch contents.

## 6. Interaction and item lifecycle defects

### Start and dialogue

- The current `~deviousminds_qualifies` rejects all three insufficient skills
  before the offer. Canonical behavior warns about skills and still permits the
  start; only the four quest prerequisites are hard start gates.
- The rejection is a vague “not ready” line. The modern journal/start dialogue
  should show exact missing quest requirements, exact skill targets, and the
  mixed boostability rule.
- Dialogue is substantially abridged. Port all reachable accept, refuse,
  resume, item-present, item-missing, replacement, post-event, and post-quest
  branches from the pinned transcript.

### Whetstone and bow-sword

- The whetstone has no Smithing check at the operation and accepts any state
  at or after 10, including post-quest, allowing unlimited blades.
- It lacks confirmation, movement/locking, player and loc animations, sparks,
  sound, interruption handling, and a transactional item swap. Matching cache
  whetstone sequences and the `devious_grind` synth already exist.
- Stringing has no Fletching action gate, omits bow string spool, grants none of
  the required 50 Fletching XP, and permits unlimited bow-swords.
- The bow-sword exposes Wield in cache, and the generic equip handler equips it.
  Add the exact quest refusal before generic equip policy can run.

### Orb and pouches

- The monk's exchange incorrectly demands one free inventory slot even though
  deleting the bow-sword makes it slot-neutral.
- Orb loss has no replacement branch despite the transcript and the native
  `%devious_monk_orb_given` evidence field.
- Combining has no base-50 Runecraft check, uses an over-broad `>= 20` stage,
  and gives incomplete reverse-click coverage, especially degraded variants.
- Pouch essence count/type/degradation is neither checked nor migrated. The
  special large-pouch Empty path turns one item into two without a free-slot
  preflight; Fill is absent; Check/Empty are isolated from the shared pouch
  runtime.
- Large and colossal outcomes must be atomic. Never consume the orb/pouch until
  all preconditions and result slots are secured; rollback cleanly on logout or
  interruption.

### Destroy, drop, death, and duplicate policy

All Devious quest objects expose a cache Destroy option, but no exact handlers
exist. Generic op-5 handling drops them on the ground instead of showing a
confirmation and destroying them. The death runtime checks the server's
`destroy_death` overlay rather than raw cache `param_295`, and no Devious
overlay supplies it. Consequently the orb, bow-sword, and special pouch enter
ordinary keep/gravestone behavior instead of their canonical lost/reclaim
lifecycle.

Define one ownership predicate across inventory, worn equipment, bank, ground,
and any temporary container. It must prevent duplicate blades/bow-swords/orbs,
support the monk's legitimate replacement, preserve ordinary pouch ownership,
and cover drop, destroy, death, bank, full inventory, reconnect, and repeated
clicks. Live-trace the colossal-pouch death case before assigning its policy.

## 7. Travel and shared-system defects

### Abyss and pouch acquisition

Mage-of-Zamorak teleport and prayer drain exist after Enter the Abyss. The
skull/abyssal-bracelet policy is absent. More critically,
`~abyss_obstacle_attempt` performs the roll, message, and 25 XP award but never
moves a successful player through the obstacle. The Law rift can teleport to
the altar only if the unreachable inner ring is somehow reached.

Modernize the shared Abyss rather than adding a Devious teleport. Cover all
obstacle/tool categories, failure, success destination, animation, XP, combat
interruption, retry, skull/bracelet behavior, each rift, and Law altar exit. Add
canonical large-pouch acquisition and degradation/repair tests so a clean
account can meet the requirement.

### Entrana

The ferry and gangplank travel work, but the monks deliberately omit prohibited
equipment checks. A player can board with the bow-sword, weapons, armour, or
unprotected orb and bypass the intended Abyss route. Centralize the Entrana
admission predicate for every arrival route that should enforce it. Test normal
players, Lost City/Heroes' Quest interactions, the protected quest pouch,
modern teleport exceptions if retained, and the canonical post-dead-monk ferry
return.

### Altar and cutscene

The current altar consumes the item, changes both transforms, and substitutes
four message boxes for the complete heist. It has no scene ownership, actors,
movement, animations, spotanims, sound, jingle, camera, interruption handling,
or reconnect checkpoint. This is not a cosmetic omission: it collapses two
native stages and exposes the body before the High Priest directs the player.

The revision-239 cache contains the High Priest, monk-with-relic, assassin,
dead-monk actors; relic/orb/pouch props; altar transforms; relic carry/death,
assassin teleport, bow-sword/whetstone, and explosion sequences/spotanims; and
the `devious_grind` synth. Build the sequence with the shared cutscene runtime.
Commit item consumption, altar state, monk transform, cutscene flag, and primary
state at explicit idempotent checkpoints. Test logout before, during, and after
the explosion; concurrent players must not share or steal scene actors.

## 8. Investigation, completion, and downstream policy

The current post-heist state makes the dead monk immediately searchable, so the
first High Priest conversation is skippable. Restore the native order: altar
event, High Priest direction, dead monk, High Priest report, real Sir Tiffy.
The High Priest's present Desert Treasure pot branch runs first and can hide the
quest route; use an explicit shared-topic router and a priority matrix instead
of possession-based accidental precedence.

The cache already has the real, world-spawned Sir Tiffy Cashien:
`rd_teleporter_guy` at 2997,3373 in Falador Park. The Devious slice instead
spawns unrelated `ds2_meeting_sir_tiffy_cashien` near 3002,3368 on every login.
This produces two Tiffys, bypasses Recruitment Drive/Wanted!/Slug Menace topic
ownership, and causes Quest Helper's target NPC to be unable to finish Devious.
Delete the login spawn and bind the Devious topic to the real shared router.

The current reward amounts are correctly expressed in tenths for
`stat_advance`, but completion writes 60. The modern completion transaction
must write 80, grant exactly one quest point and the three XP rewards once, and
show the completion scroll once. Use a persistent claim ledger or equally
strong native evidence, and test repeated click, logout at each commit, debug
completion, full inventory, and legacy migration. The `coins` completion icon
is cosmetic only; there is no canonical item reward.

Secrets of the North is the canonical downstream requirement. Its local start
currently defers prerequisite gates, and a Devious value of 60 would fail the
native end-state contract anyway. Restore its prerequisite check against the
same `>= 80` completion predicate used by the quest list and shared metadata.

## 9. Journal, debug, and evidence policy

The quest is correctly registered with the dynamic modern journal. Expand it
to represent every native state and actionable recovery condition: missing
prerequisites, skill-at-action gates, blade/bow-sword/orb ownership, pouch type
and integrity, Abyss/Law route, High Priest sequencing, and the true completion
state. The journal must derive facts from authoritative state/ownership rather
than advance progress.

The generic cheat currently writes 60 without rewards or side transforms. A
modern debug path may set a coherent test fixture or call a clearly separated
state adapter, but it is not completion coverage and must not make migration
indistinguishable from organic reward claims. Delete stale source comments and
queue documentation that claim travel was intentionally soft-skipped or that
no real Sir Tiffy exists.

The combat manifest correctly says the quest has no mandatory quest-owned kill
and that Abyssal creatures are conditional. Its empty source/gameval/handler/
test fields mean the global combat checker cannot prove this quest's route.
Populate them with the shared Abyss evidence or explicitly record the verified
no-kill contract; never add a fake boss solely to satisfy a manifest.

## 10. Modernization work order

1. **Capture evidence.** Trace native 20/30/40 commits, filled/degraded pouch
   behavior, special-pouch death, alternate Entrana teleports, animations,
   sounds, jingle, scene checkpoints, and exact replacement dialogue.
2. **Freeze state and migration.** Name the native 0–80 ladder, implement the
   one-time legacy adapter and reward ambiguity quarantine, preserve all shared
   base-var bits, and add state transition/idempotency tests.
3. **Restore shared prerequisites.** Implement large-pouch acquisition, full
   Abyss traversal/skull behavior, Law route, and centralized Entrana admission.
4. **Modernize crafting and items.** Add action-scoped skill gates, confirmation
   and animation, 50 Fletching XP, spool support, equip refusal, atomic pouch
   conversion, ownership, destroy, death, and replacement lifecycle.
5. **Build the altar scene.** Use owned instances/actors and cache assets;
   checkpoint every irreversible mutation and support logout/re-entry.
6. **Fix shared NPC routing.** Restore High Priest sequence, remove duplicate
   Tiffy, and integrate Devious with Recruitment Drive, Wanted!, Slug Menace,
   Desert Treasure I, and Holy Grail topic precedence.
7. **Complete and unlock.** Commit native 80 and rewards once, update the
   journal/debug adapter, restore Secrets of the North prerequisite enforcement,
   and populate the combat evidence contract.
8. **Run the full matrix.** Exercise clean/legacy accounts, boosted/drained
   stats, both pouches, both string items, inventory/bank/ground/death/reconnect,
   all shared-NPC overlaps, every route boundary, and repeat every irreversible
   interaction.

Do not mark `verified-modern` until all shared prerequisites are in the normal
player path; resolving only the five quest-owned scripts is insufficient.

## 11. Verification matrix

| Area | Required automated/integration evidence |
| --- | --- |
| Metadata | Dbrow ID, prerequisites, QP, mixed boostability, action-gated skills, end state 80, journal registration |
| Start | Each missing prerequisite; every low skill; accept/refuse/re-talk; exact warning; combined missing requirements |
| Smithing | Level 64/65, boosts/drains, wrong item, confirmation cancel, animation/sound, interruption, repeat, post-quest |
| Fletching | Level 49/50, boosts/drains, both click directions, string and spool, exact 50 XP once, full inventory, duplicate prevention |
| Bow-sword/orb | Equip refusal, slot-neutral exchange, bank/ground ownership, lost orb replacement, destroy/drop/death, repeated clicks |
| Pouches | Base RC 49/50, active boost ignored, large/colossal, degraded/filled, check/fill/empty, reverse use, slot preflight, death/reconnect |
| Acquisition | Clean-account large-pouch source, drop eligibility, duplicate policy, degradation, repair, colossal alternative |
| Abyss | Teleport prerequisites, prayer/skull/bracelet, every obstacle fail/success/movement/XP, combat interruption, every rift, Law exit |
| Entrana | Every admission route, equipment policy, protected/unprotected orb, post-body ferry, Lost City/Heroes regressions |
| Cutscene | Correct actors/assets/order, sound/jingle, per-player ownership, item/pouch outcomes, logout at each checkpoint, concurrent players |
| Investigation | High Priest cannot be skipped; body visibility/search; first and second priest reports; shared DT/Holy Grail topic matrix |
| Sir Tiffy | Single real spawn; Recruitment Drive, Wanted!, Slug Menace, and Devious states/topics; Quest Helper target compatibility |
| Completion | Native 80, QP once, 6500/5000/5000 XP once, scroll once, repeated click, reconnect, cheat separation, migration ambiguity |
| Downstream | Quest list completion, Secrets of the North prerequisite, post-quest dialogue, item cleanup/reclaim |
| Legacy | Every old 0/10/20/30/40/50/60 value, side-field contradictions, duplicate items, unrelated cave-horror bit preservation |

## 12. Gate verdict

| Gate | Result | Evidence |
| --- | --- | --- |
| A — source coverage | **Fail** | Pinned references and cache state are now documented, but several native transitions and pouch/death details still require live traces |
| B — interaction correctness | **Fail** | Action skill gates, XP, animations, equip/destroy/death/recovery, dialogue, and idempotency are missing or incorrect |
| C — critical path | **Fail** | No clean-account pouch acquisition, Abyss obstacle movement, Entrana restriction, real altar cutscene, or correct shared Tiffy completion |
| D — permanence and release | **Fail** | Completion writes 60 instead of 80, legacy rewards are ambiguous, downstream gating is absent, and combat evidence is empty |

Release classification remains `audit-pending`. The quest can become
`verified-modern` only after implementation, static validation, replayable
integration evidence for this matrix, and a fresh audit with no critical-path
soft-skips or duplicate shared owners.
