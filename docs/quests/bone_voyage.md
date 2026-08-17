# Bone Voyage modernization audit

Status: `audit-pending` — the native quest row, complete 0/5/10/11/15/20/21/
22/23/25/30/35/50 state vocabulary, persistent charm and potion side states,
quest journal, shared-NPC dispatch, required quest items, barge maps, voyage
interface, completion call, cheat arm, and several Fossil Island activity gates
exist. The normal implementation is not faithful or complete: it ignores The
Dig Site and 100 Kudos at start, binds the Woodcutting Guild hand-in to the
Prifddinas sawmill NPC instead of the placed Hosidius operator, has no lost
proposal/agreement recovery, omits barge disembarkation, replaces the sailing
activity with a message, arrives at the wrong island coordinate, omits the
Fossil Island note book, and leaves every post-quest barge travel option
unhandled.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the start requirements, Curator Haig
Halen, both sawmills and the Woodcutting Guild gate fallback, proposal and
agreement recovery, barge guards, all crew members, Jack Seagull and the Rusty
Anchor interruption, the Odd Old Man, the Apothecary, both quest-item side
states, the voyage interface and retry loop, arrival, note book reward,
post-quest transport, Museum Camp utilities, digsite-pendant unlock, journal,
completion, cheat behavior, and Dragon Slayer II prerequisite contract. It is
an implementation specification, not completion evidence.

## 1. Authoritative references

These stable OSRS Wiki revisions define the current quest route, dialogue,
item recovery, voyage behavior, reward, and permanent Fossil Island contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Bone Voyage](https://oldschool.runescape.wiki/w/Bone_Voyage?oldid=15262285) | 15262285, 2026-07-13 | Identity, start requirements, route, voyage, rewards, and Dragon Slayer II dependency |
| [Bone Voyage/Quick guide](https://oldschool.runescape.wiki/w/Bone_Voyage/Quick_guide?oldid=15089626) | 15089626, 2025-12-18 | Exact route order, guild fallback, ingredients, voyage controls, and post-quest recommendation |
| [Transcript:Bone Voyage](https://oldschool.runescape.wiki/w/Transcript%3ABone_Voyage?oldid=15248086) | 15248086, 2026-07-02 | Eligibility refusal, accept/refuse, re-talk, document replacement, crew dialogue, item replacement, failure/retry, and arrival |
| [Kudos](https://oldschool.runescape.wiki/w/Kudos?oldid=15105676) | 15105676, 2026-01-15 | The 100-Kudos access threshold and relationship to Fossil Island |
| [Canal barge](https://oldschool.runescape.wiki/w/Canal_barge?oldid=15211692) | 15211692, 2026-05-16 | Post-quest Quick-Travel and digsite-pendant route |
| [Barge guard](https://oldschool.runescape.wiki/w/Barge_guard?oldid=15040225) | 15040225, 2025-11-19 | Pre-quest Embark, post-quest Embark/Quick-Travel, and aboard Disembark options |
| [Sawmill proposal](https://oldschool.runescape.wiki/w/Sawmill_proposal?oldid=15188442) | 15188442, 2026-04-22 | Proposal identity, delivery, and replacement ownership |
| [Sawmill agreement](https://oldschool.runescape.wiki/w/Sawmill_agreement?oldid=15188443) | 15188443, 2026-04-22 | Signed-document identity, return, and replacement ownership |
| [Bone charm](https://oldschool.runescape.wiki/w/Bone_charm?oldid=15188444) | 15188444, 2026-04-22 | Charm acquisition, hand-in, and loss recovery |
| [Potion of sealegs](https://oldschool.runescape.wiki/w/Potion_of_sealegs?oldid=15188445) | 15188445, 2026-04-22 | Ingredient exchange, hand-in, and loss recovery |
| [Fossil island note book](https://oldschool.runescape.wiki/w/Fossil_island_note_book?oldid=15282247) | 15282247, 2026-07-30 | Completion grant, Read behavior, activity notes, and replacement sources |
| [Fossil Island](https://oldschool.runescape.wiki/w/Fossil_Island?oldid=15229241) | 15229241, 2026-06-08 | Island access, Museum Camp travel, pendant binding, and mushtrees |
| [Museum Camp](https://oldschool.runescape.wiki/w/Museum_Camp?oldid=15302184) | 15302184, 2026-08-15 | Arrival area and buildable utilities, including the level-21 bank chest |
| [Digsite pendant](https://oldschool.runescape.wiki/w/Digsite_pendant?oldid=15302156) | 15302156, 2026-08-15 | Post-quest House on the Hill destination unlock and charge behavior |
| [Strange machine](https://oldschool.runescape.wiki/w/Strange_machine?oldid=14707611) | 14707611, 2024-07-26 | Pendant-binding interaction at the House on the Hill |
| [Fossil island bank chest](https://oldschool.runescape.wiki/w/Fossil_island_bank_chest?oldid=14338262) | 14338262, 2022-10-27 | 21 Construction and the 2 oak planks, iron bar, 5 nails, and hammer transaction |

The sources were resolved through the OSRS Wiki API on 2026-08-17. They
identify Bone Voyage as quest #132, an intermediate, short, members quest
released 7 September 2017. Starting requires completion of The Dig Site and at
least 100 Kudos. Required consumables are two vodkas and one unfinished
marrentill potion. The reward is 1 quest point, access to Fossil Island, and a
Fossil Island note book.

Transition aid only: the local Quest Helper checkout's
[`BoneVoyage.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/bonevoyage/BoneVoyage.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms the native
0/5/10/11/15/20/21/22/23/25/30/35 ladder, all route coordinates, the three
side-state tests for each crew item, the 60 Woodcutting conditional route, the
two voyage zones, required items, 1 quest point, and Fossil Island unlock. It
is a transition-test aid and does not override the Wiki, transcript, or cache.

`python3 tools/questhelper_extract.py bonevoyage --check` resolves the quest
dbrow, all four quest items, all ten referenced NPC symbols, the eastern guild
gate, both side-state varbits, Kudos, and all coordinates. That symbol check
does not establish world reachability: Quest Helper asks for
`prif_sawmill_operator` at 1620,3499, while this repository actually spawns
`poh_sawmill_opp` at 1623,3500 and spawns `prif_sawmill_operator` only in
Prifddinas at 3315,6116.

## 2. Native quest identity and player contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 143 |
| Type | Members quest |
| Difficulty / length | Cache values 1 / 1; Wiki intermediate / short |
| Release date | 7 September 2017 |
| Start | Curator Haig Halen, Varrock Museum, 3257,3448,0 |
| Start requirements | The Dig Site complete and at least 100 Kudos; neither is boostable or consumable |
| Required items | 2 vodka and 1 marrentill potion (unf); one free slot for each recoverable document/item exchange outcome |
| Primary state | `%fossilquest_progress`, bits 0–8 of transmitted permanent carrier `fossilquest_main` |
| Side state | `%fossilquest_lucky_charm`, bits 9–10; `%fossilquest_potion`, bits 11–12 of the same permanent carrier |
| Voyage state | `%fossilquest_sailing_progress`, bits 0–6, and `%fossilquest_sailing_arrowspeed`, bits 7–30 of transmitted temporary carrier `fossilquest_temp` |
| Quest points | 1 |
| Item reward | Fossil Island note book |
| Unlocks | Fossil Island, repeat barge travel, pendant binding at the House on the Hill, Museum Camp utilities, island systems, and a Dragon Slayer II prerequisite |
| End state | 50 |

The quest dbrow correctly names The Dig Site through dbrow pack ID 29 and has
the correct identity, membership, release date, start NPC, quest-point reward,
and end state. It has no field for the 100-Kudos threshold. The runtime start
must therefore check both `~itexam_get_progress >= ^itexam_complete` and
`%vm_kudos >= 100`; metadata alone cannot enforce eligibility.

### Primary state inventory

| State | Local constant | Canonical phase |
| ---: | --- | --- |
| 0 | `bv_not_started` | Ordinary Curator menu; check The Dig Site and 100 Kudos before offering acceptance |
| 5 | `bv_foreman` | Talk to the Digsite barge foreman |
| 10 | `bv_sawmill` | Obtain proposal from the Varrock Lumber Yard operator |
| 11 | `bv_guild` | Have Hosidius operator sign it, or use Berry/eastern-gate fallback without 60 Woodcutting |
| 15 | `bv_return_agree` | Return signed agreement to the Varrock operator |
| 20 | `bv_foreman2` | Return to the foreman after redwood shipment |
| 21 | `bv_lead` | Embark and learn the voyage history/curse from Lead Navigator |
| 22 | `bv_jack` | Ask Jack Seagull about cursed voyages; Ahab proposes sealegs potion |
| 23 | `bv_lead2` | Return and offer both remedies to the navigators |
| 25 | `bv_items` | Obtain and independently hand in charm and potion |
| 30 | `bv_sail` | Ready to begin the voyage |
| 35 | `bv_sail2` | Voyage failed/aborted; retry from the docked crew |
| 50 | `bv_complete` | Award note book and 1 QP, finish aboard the island barge, then allow landing and repeat travel |

No production script writes state 35. The two temporary voyage varbits are
also never read or written, so neither native retry state nor the cached
progress/bearing interface has a server owner.

### Side-state inventory

| Varbit | Values | Required ownership |
| --- | --- | --- |
| `%fossilquest_lucky_charm` | 0 none; 1 obtained; 2 given | Odd Old Man grants/replaces at 25; Junior consumes exactly one; replacement stops after hand-in |
| `%fossilquest_potion` | 0 not discussed; 1 ingredients requested; 2 obtained; 3 given | Apothecary performs/repeats the exchange at 25; Lead consumes exactly one; retry needs no new player item |
| `%fossilquest_sailing_progress` | 0–127 | Per-attempt distance/progress, reset on start/abort/failure/completion |
| `%fossilquest_sailing_arrowspeed` | packed 24-bit field | Per-attempt bearing, rudder, wind, and/or speed presentation as read by the native interface; exact packing requires CS2 decompilation |

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_bonevoyage/configs/bonevoyage.constant` | Primary/side states and route coordinates | Correct state vocabulary; destination constant is mislabeled and points to 3757,3875 near the House on the Hill, not Museum Camp or the arrival barge |
| `server/scripts/quests/quest_bonevoyage/configs/bonevoyage.varp` | Permanent and temporary carrier overlays | Both carriers are clean/transmitted; voyage carrier is declared `scope=temp`, but no runtime initializes or consumes it |
| `server/scripts/quests/quest_bonevoyage/scripts/bonevoyage.rs2` | Start, journal, repair route, crew/items, soft voyage, completion, debug walk | 371-line single-file implementation; broad state ladder exists, but critical mechanics, recovery, reward, arrival, and post-quest transport do not |

The quest root totals 421 lines across three files. Its header explicitly marks
the real prerequisite gates, sailing interface, bank-chest note, and full
dialogue trees as deferred. Those disclosures are useful audit evidence, not
evidence that a playable substitute is acceptable.

### Mandatory shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `configs/all.dbrow` | `quest_bonevoyage` | Correct The Dig Site dbrow and end state; cannot encode/check Kudos by itself |
| `configs/all.varbit` | Primary, item, voyage, Kudos, and Museum Camp utility state | Native layouts exist; voyage and camp utility states have no production owners in the audited surface |
| `configs/all.obj` | Proposal, agreement, charm, potion, note book, ingredients | All items resolve; note book is never produced or read, and quest Destroy/loss behavior is not specialized |
| `configs/all.npc` | Quest/post-quest multi-NPCs and menu ops | Correct transforms expose pre/post menus, but post-quest and aboard leaf handlers are absent; guild handler points at the wrong NPC family |
| `configs/all.loc` and interface pack | Barge controls, voyage locs, `fossilquest_sailing`/overlay, camp utility multis | Assets exist but are not wired; bank chest remains `fossil_bank_chest_notbuilt` because `%fossil_bank` is never written |
| `areas/world/configs/m52_53.spawn` | Digsite foreman, guards, crew, workers, and docked barge | Required actors are placed; aboard guard's Disembark has no handler |
| `areas/world/configs/m25_54.spawn` | Hosidius Woodcutting Guild operator | Places `poh_sawmill_opp`, not the bound `prif_sawmill_operator`; canonical qualified-player dialogue fails |
| `areas/world/configs/m28_74.spawn` | Voyage-map Junior Navigator | Actor exists, but no code enters/owns the voyage map or binds voyage controls |
| `areas/world/configs/m58_59.spawn` | Island barge crew and Museum Camp actors | Crew transforms to unhandled post-quest leaves; separate camp shop and field actors exist |
| `areas/varrock/scripts/curator.rs2` | Shared Haig dispatcher | Bone Voyage in-progress takes priority after Defender of Varrock and `%eaa`; start is offered through ordinary news menu, but requirements are skipped |
| `skill_woodcutting/scripts/woodcutting_guild.rs2` | Eastern-gate Berry fallback and level-60 entry | Calls `@bv_guild_gate` before the level check whenever state 11 and proposal are present; this lets any player use the fallback, including qualified players |
| `quests/quest_ragandboneman/scripts/ragandboneman.rs2` | Shared Odd Old Man | Bone Voyage state 25 preempts Rag and Bone Man until the charm phase advances; overlap dialogue is abbreviated |
| `areas/varrock/scripts/apothecary.rs2` | Shared Apothecary | Bone Voyage is below A Tail of Two Cats, One Small Favour, and Making Friends with My Arm, but above Ratcatchers and Romeo & Juliet; collision matrix is untested |
| `interface_questjournal/scripts/quest_journal.rs2` | Journal dispatch | Correctly routes the dbrow to `~bonevoyage_journal`; text is broad and does not reflect partial item state, failure, or landing |
| `quests/scripts/quest_cheat.rs2` | Generic `::complete` adapter | Writes only progress 50; leaves charm, potion, voyage temp state, quest items, notebook, and arrival/post-quest world contract incoherent |
| `skill_hunter/scripts/birdhouse.rs2`, `herbiboar.rs2`, `drift_net_fishing.rs2` | Downstream island activities | Correctly reject players below state 50; most other island systems have no equivalent explicit gate in production scripts |
| `shop/museum_camp/scripts/fossil_island_general_store.rs2` | Museum Camp store | Shop exists, but it neither owns note-book replacement nor checks Bone Voyage |
| `quest_dragonslayer2` and its dbrow | Direct downstream quest | Native requirement list includes Bone Voyage; DS2 later assumes meaningful access to Fossil Island and its map locations |

### Cache-native assets already available

- primary, charm, potion, voyage, Kudos, Museum Camp utility, and other Fossil
  Island varbits, plus the quest row and correct end state;
- proposal, agreement, charm, potion, note book, ingredient, construction, and
  teleport item configs;
- pre/post Digsite guard leaves, aboard guard, quest/post Lead and Junior
  Navigator leaves, John, David, Ahab, Jack, and barge worker/foreman configs;
- the full docked barge, separate voyage-map barge, controls, rudder, sail
  controls, cargo, crew spawn, and `fossilquest_sailing` interfaces;
- the Museum Camp, bank/bench/well multis, House on the Hill strange machines,
  mushtrees, island crew, and activity/world maps; and
- the shared modern quest journal and completion service.

Modernization should connect those assets through current RuneScript
transactions, protected movement/instances, named interface mounts, packet
handlers, retry cleanup, and shared transport services. It should not invent a
second state machine or retain the narrated voyage as an accepted path.

## 4. Native reachability and first blocker

The route can reach state 11 from the real Curator, foreman, and Varrock
sawmill triggers. The first canonical route defect is the guild signing step:

1. The state-11 journal and Wiki send a qualified player to the Hosidius
   sawmill operator at approximately 1620,3499.
2. The placed NPC there is `poh_sawmill_opp`. Its one global Talk-to handler
   knows states 10 and 15, but at state 11 falls through to "Want some planks?"
3. `@bv_guild_sawmill_talk` is instead bound to
   `prif_sawmill_operator`, whose only spawn is in Prifddinas.
4. Opening the eastern guild gate while holding the proposal does call the
   Berry fallback and advances to 15 before the level-60 check. This is a
   non-obvious workaround for qualified players and the intended route for
   players without 60 Woodcutting, not a valid replacement for the operator.

The fallback means the quest is conditionally completable by a player who
knows to ignore the qualified route. It is still a deterministic failure of
the canonical action and of Quest Helper's level-60 branch.

| State | Current reachability / defect |
| ---: | --- |
| 0 | Curator offers and starts the quest without checking The Dig Site or 100 Kudos; refusal exists but the transcript eligibility branches do not |
| 5 | Foreman reliably advances to 10 through abbreviated dialogue |
| 10 | Any `poh_sawmill_opp` can grant the proposal; free-space failure correctly leaves state 10 |
| 11 | Hosidius operator cannot sign; eastern gate workaround advances; a lost proposal cannot be reclaimed from Varrock and permanently blocks the route |
| 15 | Varrock operator consumes agreement and advances; a lost agreement cannot be reclaimed from Berry/Hosidius and permanently blocks the route |
| 20 | Foreman advances to 21; barge appearance/workers are not state-reviewed |
| 21 | Port guard boards the player; Lead advances to 22; no aboard Disembark handler exists |
| 22 | Jack advances to 23, but Ahab's interruption and choice-dependent shared pub dialogue are absent |
| 23 | Lead advances to 25; no detailed remedy comparison or independent crew requests |
| 25 | Odd Old Man and Apothecary can grant/recover items; Junior must receive charm before Lead accepts potion; full-inventory replacement feedback is incomplete |
| 30 | Lead prints a soft-skip message, teleports directly from the docked barge to 3757,3875, and completes; the voyage map/interface never runs |
| 35 | Unreachable in production; no abort/failure/retry loop owns it |
| 50 | Note book is absent; arrival conversation is absent; island and Digsite post-quest crew/guard leaves have no Talk-to/Travel/Embark/Quick-Travel handlers |

The current destination `0_58_60_45_35` is 3757,3875, north of the Museum
Camp near the House on the Hill. The named Museum Camp destination in the
repository is 3730,3821, while the arrival barge crew is at 3723,3784–3785 on
plane 1. The direct teleport therefore skips both the voyage arrival and
Junior Navigator's landing handoff and does not even land in the place named
by `^bv_fossil_camp_coord`.

## 5. Current versus required playable route

| Chapter | Current route | Modernized route contract |
| --- | --- | --- |
| Start | Curator asks one compressed question and writes 5 after Yes | In the shared news subject, check The Dig Site and 100 Kudos, reproduce distinct refusal messages, show explicit start confirmation, and write 5 only after acceptance |
| Foreman | Two short linear conversations | Preserve full first/re-talk and post-shipment dialogue; state commits after the relevant conversation, not on generic Talk-to |
| Proposal | Grants one with a free-slot check, then state 11 | Bind only the Varrock instance/coordinate or dispatch by location; recover a genuinely lost proposal at state 11; never duplicate banked/owned copies |
| Guild agreement | Wrong NPC binding; gate silently substitutes a worker | Bind Hosidius operator for qualified players and Berry/eastern gate only for unqualified players; preserve proposal presence/capacity and agreement replacement dialogue |
| Agreement return | Consumes one and writes 20 | Validate exact Varrock operator and exact agreement; lost agreement remains recoverable at state 15 until successfully returned |
| Boarding | Port guard's Talk-to teleports aboard | Add Embark op, protected movement, aboard Disembark, reboarding at every relevant state, and state-correct guard/crew dialogue |
| Curse history | Lead and Jack compress the entire story | Restore optional nine-barge history, Jack/Longbow Ben/Ahab menu integration, both remedies, re-talks, and no unintended shared-pub subject loss |
| Charm | Grant/replace and Junior consume exist | Retain capacity-safe grant, explicit full-inventory response, deterministic loss recovery only before hand-in, independent order, and exact side-state transition |
| Potion | Two-step ingredients and replacement exist | Preserve two vodkas + unfinished marrentill potion, transact atomically, give clear capacity feedback, support deterministic replacement only before hand-in, and independent order |
| Voyage | One message followed by direct teleport/completion | Move to private voyage map, mount and arm native overlay/main IF, own controls/ticks/bearing/progress, support abort and timeout/failure, return safely at 35, and complete only at successful arrival |
| Arrival/reward | Writes 50, calls completion with `coins`, and teleports to wrong coordinate | Commit 50 and 1 QP once, grant note book capacity-safely, use correct reward-scroll content/icon, finish aboard island barge, then let Junior take player to Museum Camp |
| Permanent access | Menu ops exist in cache but no handlers | Implement Digsite Quick-Travel/Embark, aboard Disembark, island crew Travel, pendant binding, note-book replacement, and all completion-gated island services |
| Museum Camp | Bank/bench/well cache multis exist without scripts | Implement each Construction level/material/tool transaction, per-player varbit transform, XP, repeat behavior, and note-book activity entry; level-21 bank chest is the minimum quest-adjacent acceptance case |

## 6. Item and state transactions

### Eligibility and acceptance

The start dispatcher must keep Bone Voyage inside Haig's shared menu without
preempting Defender of Varrock, The Golem pickpocket, Shield of Arrav, or The
Dig Site certificate operations. When the player selects interesting news:

1. If The Dig Site is incomplete, use the transcript's missing-qualification
   route and remain at 0.
2. If The Dig Site is complete but Kudos is below 100, say the player is not
   ready, show the requirements message, and remain at 0.
3. If both pass, offer `Start the Bone Voyage quest? Yes/No` and write state 5
   only on Yes.
4. Repeated packets and dialogue resumes must not start twice or bypass the
   current shared-NPC priority decision.

### Sawmill document lifecycle

The document exchange must be loss-tolerant and location-specific:

1. At state 10, the Varrock operator needs one free outcome slot, gives exactly
   one proposal, then commits 11. No Hosidius or other `poh_sawmill_opp` spawn
   may perform this branch.
2. At state 11 with no proposal anywhere in supported player ownership, the
   Varrock operator replaces it. With an existing copy, he gives directions.
3. At state 11, a qualified player hands the proposal to the Hosidius operator;
   an unqualified player attempting the eastern gate hands it to Berry. The
   replacement is slot-neutral because one unstackable document is exchanged,
   but validation must still precede consumption.
4. The signer consumes exactly one proposal, produces exactly one agreement,
   then commits 15. Duplicate packets cannot create multiple agreements.
5. At state 15 with no agreement, the same signer used in step 3 replaces it.
   The gate fallback must remain accessible to the unqualified route.
6. The Varrock operator consumes exactly one agreement, commits 20, and never
   repeats the redwood shipment transition.

The current post-delete free-space checks are not normally lossy because
removing an unstackable document frees its replacement slot. They are still
the wrong transaction shape and conceal the more serious missing replacement
branches.

### Charm and potion lifecycle

- The Odd Old Man grants one charm at side state 0 only when the outcome can be
  delivered, then writes charm state 1. At state 1 with no owned charm he
  replaces it; at state 2 he only provides post-hand-in dialogue.
- The Apothecary first writes potion state 1 after listing the recipe. The next
  interaction validates one `marrentillvial` and two `vodka`, performs a single
  atomic 3-for-1 exchange, then writes potion state 2. Removing the ingredients
  currently frees enough slots, so the late capacity check is not practically
  lossy, but it must be normalized with shared transaction helpers.
- At potion state 2 with no owned potion, the Apothecary replaces it without
  charging ingredients again. At state 3 he only discusses the expedition.
- Lead and Junior accept their own items independently at quest state 25. Each
  consumes one item and commits only its own side state. Setting state 30 must
  require both side states at their handed-in values, regardless of order.
- Death, dropping/Destroy, logout, teleporting off the barge, and repeated
  packets must preserve a recoverable, non-duplicating state.

### Voyage attempt lifecycle

The cached `fossilquest_sailing` (interface 604),
`fossilquest_sailing_overlay` (603), voyage map, controls loc, and two temporary
varbits should be treated as one attempt-scoped service:

1. Starting from state 30 or retry state 35 confirms readiness, moves the player
   into an isolated voyage map, initializes temporary values, and mounts both
   native interfaces.
2. Decompile each interface's onload/transmit scripts before assigning packed
   fields. Bind every left/right/rudder/sail/abort server op and reject packets
   when the player is not in the owned attempt.
3. A protected tick updates wind/bearing, rudder effect, sail speed, progress,
   and the interface. Going too far off course or timing out returns the player
   to the Digsite barge, clears temporary/UI/instance state, and commits 35.
4. Abort confirmation performs the same cleanup without consuming another
   charm/potion and leaves the crew ready to retry.
5. Success moves the player to the island arrival barge, clears all temporary
   state, grants the note book or uses the verified full-inventory behavior,
   commits completion once, and presents the reward scroll.
6. Logout, death, region loss, interface close, duplicate controls, and server
   restart must resolve to a deterministic retry, never state 50 or a stranded
   private instance.

### Completion and permanent unlock transaction

`~bv_quest_complete` currently writes 50 before invoking the completion
service, grants no note book, and supplies `coins` as the display item even
though Bone Voyage has no coin reward. Modern completion must:

1. require a committed successful voyage and state 30/35 attempt ownership;
2. arrange delivery of one `fossil_note_book` without losing completion to a
   full inventory, using captured live behavior for drop/bank/deferred delivery;
3. commit side state, progress 50, 1 QP, completion count, note book, and reward
   presentation exactly once;
4. leave the player on the island barge for Junior's authored arrival dialogue
   and optional Museum Camp travel;
5. enable all pre/post multi-NPC leaves and transport ops immediately; and
6. make `::complete quest_bonevoyage` establish the same permanent unlock
   state and clean all attempt-owned temporary state without fabricating
   inventory items that would duplicate on later recovery.

The note book's post-quest recovery sources—Fossil Collector, Fossil Island
General Store shop keeper, and POH bookcase—also need shared ownership checks
and Read support. The note book is not just reward-scroll text; island utility,
mushtree, rowboat, fossil, and other discoveries write entries into it.

## 7. Shared NPC, world, and unlock oversights

### Shared-dispatch collision matrix

Before implementation, add transition tests for at least these simultaneous
states:

| Shared actor | Bone Voyage state | Other state to preserve | Expected routing |
| --- | ---: | --- | --- |
| Curator Haig Halen | 0 / 5–49 / 50 | Defender of Varrock, `%eaa`, Shield of Arrav, Dig Site certificates, The Golem pickpocket | Explicit subject/menu selection; no quest action made unreachable |
| `poh_sawmill_opp` | 10 / 11 / 15 | Ordinary planks/shop and every placed sawmill instance | Coordinate/region-specific Bone Voyage branch; ordinary service remains available through a menu |
| Woodcutting Guild gate/Berry | 11 | below/at/above 60 Woodcutting | Only unqualified players use Berry signing; qualified players enter and use operator; both recover agreement |
| Odd Old Man | 25 | Rag and Bone Man I collecting/turn-in | Bone Voyage charm subject plus an explicit route to the active bone quest |
| Apothecary | 25 | A Tail of Two Cats, One Small Favour, Making Friends with My Arm, Ratcatchers, Romeo & Juliet | Stable subject menu; no priority-only starvation |
| Jack Seagull/Ahab/Longbow Ben | 22 | ordinary pub and Pirate's Treasure-style subjects | Preserve every menu option and Ahab's interruption exactly once |

### Post-quest transport

The cache exposes the correct menus but no production script binds them:

- `fossilquest_barge_guard_port_postquest`: Talk-to, Embark, Quick-Travel;
- `fossilquest_barge_guard_barge`: Talk-to, Disembark;
- `fossilquest_lead_navigator_postquest`: Talk-to, Travel; and
- `fossilquest_jr_navigator_postquest`: Talk-to, Travel.

Implement one shared, completion-gated transport contract with protected
movement and correct destinations. Embark should board the appropriate local
barge; Quick-Travel should go directly between Digsite and Museum Camp/arrival;
Disembark must return to the side from which the player boarded; island Travel
must not send players into the obsolete quest voyage. Test every menu op, not
only Talk-to.

### Museum Camp and island access

Three Hunter activities explicitly gate on state 50, but the island is a
public world area and many other systems do not reference Bone Voyage. The
authoritative boundary should be access and transport plus any content-specific
requirement—not scattered assumptions that coordinates are unreachable.

At minimum modernization must verify:

- Museum Camp bank, cleaning bench, well, cooking pot, spinning wheel, loom,
  and other utility builds against levels, materials, nails, hammer, XP, and
  their `fossil_perm_transmit` bits;
- Fossil Island note-book activity entries and replacement;
- strange-machine pendant binding and unbinding after quest completion;
- magic mushtree discovery and destination persistence;
- rowboats, underwater entry, volcanic mine, drift-net fishing, bird houses,
  herbiboar, ammonite crabs, shops, and fossil-cleaning access; and
- Dragon Slayer II's Museum Camp and island map route when Bone Voyage was
  normally completed or completed through the cheat adapter.

The level-21 bank chest—two oak planks, one iron bar, five nails, and a
hammer—is an explicit post-quest acceptance test because both the Wiki and
Quest Helper call it out, and the native `fossil_bank` multi already exists.

## 8. Findings by priority

### P0 — critical path, reward, and unlock correctness

1. Enforce The Dig Site and 100 Kudos before state 0→5.
2. Bind the state-11 signing dialogue to the actual Hosidius operator and keep
   Berry's eastern-gate fallback for players without 60 Woodcutting.
3. Add deterministic proposal and agreement recovery at states 11 and 15.
4. Implement aboard Disembark so the required Port Sarim, Odd Old Man, and
   Apothecary trips do not depend on teleporting.
5. Replace the state-30 message/teleport with the native voyage, including
   abort, failure, state 35 retry, logout/death cleanup, and success.
6. Complete at the island barge, grant the Fossil Island note book, and use the
   correct reward presentation. Do not teleport to 3757,3875 as "camp."
7. Bind all post-quest Embark/Quick-Travel/Disembark/Travel ops so the headline
   Fossil Island access reward actually persists.
8. Make completion and `::complete` idempotent and coherent across primary,
   side, temporary, reward-item, and world-access state.

### P1 — full route and recoverability

1. Restore full Curator eligibility/accept/refuse/re-talk dialogue.
2. Restore foreman, both sawmills, Berry, Lead/Junior, Jack/Ahab/Longbow Ben,
   Odd Old Man, Apothecary, crew, arrival, and post-quest dialogue branches.
3. Make charm and potion hand-ins independent and atomic; add explicit
   full-inventory and genuine-loss feedback.
4. Scope shared sawmill logic by world instance/coordinate and preserve normal
   plank/shop options at every unrelated operator.
5. Implement note-book Read/recovery and minimum Museum Camp build contracts.
6. Rewrite the journal for eligibility, document possession/loss, separate
   charm/potion states, voyage/retry, successful arrival, and completion.
7. Test every shared-NPC overlap and every non-Talk-to menu op.

### P2 — fidelity, presentation, and maintainability

1. Use the native barge history, crew conversations, potion pass-out scene,
   camera/audio, voyage interface, progress feedback, and arrival presentation.
2. Connect barge construction/workers and relevant loc/NPC transforms to the
   native state where the cache supports them.
3. Centralize recoverable quest-item ownership and Fossil Island transport
   predicates rather than duplicating inventory/state tests.
4. Add quest speedrunning integration only if/when a native Bone Voyage
   speedrun row and live behavior are established; do not invent metadata.
5. Remove all `Soft:`/`Soft-skip:`/`Deferred:` claims only after their real
   paths and failure tests land.

## 9. Modernization work packages

### Package 0 — fixtures, state model, and shared dispatch

- Add an executable transition fixture for states 0, 5, 10, 11, 15, 20, 21,
  22, 23, 25 with all side-state combinations, 30, 35, and 50.
- Encode The Dig Site/Kudos eligibility and define atomic proposal, agreement,
  charm, potion, note-book, and completion transactions.
- Split the single quest file into lifecycle/journal, museum/barge repair,
  crew/remedies, voyage, and post-quest transport modules while retaining one
  owner for each state transition.
- Add explicit shared-NPC dispatcher tests before changing dialogue priority.

### Package 1 — start and barge repair

- Modernize Haig's start/refusal/re-talk path and exact requirements.
- Correct Varrock-versus-Hosidius sawmill dispatch and bind Berry/gate behavior.
- Implement proposal/agreement replacement, capacity, duplicate packet,
  drop/Destroy, bank ownership, relog, and ordinary sawmill service tests.
- Verify foreman and construction-worker presentation for every repair state.

### Package 2 — boarding, curse, and remedies

- Implement protected Embark/Disembark from both guard menu ops.
- Restore Lead's optional history, Jack/Ahab sequence, return conversation, and
  independent Lead/Junior remedy requests.
- Make Odd Old Man and Apothecary shared menus composable with every overlapping
  quest, then implement full item loss/capacity/replacement contracts.
- Cover leaving/reboarding, giving either item first, logout, death, and repeat
  packets.

### Package 3 — voyage and completion

- Decompile and document interface 603/604 onload and transmit contracts.
- Implement a private voyage instance, control ops, wind/bearing/progress tick,
  sail speed, abort confirmation, fail/timeout, state-35 retry, and cleanup.
- Capture current live values/timing before finalizing the packed temporary
  field rather than guessing from its width.
- Implement successful arrival, note-book delivery, exact completion/jingle,
  reward scroll, Junior landing conversation, and once-only state/QP commit.

### Package 4 — permanent Fossil Island contract

- Implement every guard/crew post-quest transport op in both directions.
- Implement note-book Read/recovery and discovery entries.
- Implement Museum Camp utility builds, starting with the bank chest, using
  native multis and per-player persistent bits.
- Verify pendant binding/unbinding, mushtrees, rowboats, shops, island activity
  gates, and Dragon Slayer II integration.
- Make the cheat adapter set the same permanent unlocks and clear temporary
  voyage state, then test it twice.

Package 4 is part of Bone Voyage acceptance because access to Fossil Island is
the quest's only headline unlock. A reward scroll that says "access" while
repeat transport and the island's foundational services are unimplemented is
not a complete quest.

## 10. Verification plan

### Static and build checks

- `rg` must find no production `Soft-skip` or known critical `Deferred` marker
  in the Bone Voyage ownership surface.
- Resolve every NPC, loc, obj, varbit, interface, animation, sound, map, and
  dbrow symbol against the intended osrs239 cache.
- Assert that the Hosidius coordinate's placed NPC is one of the handler's
  bound types and that Prifddinas cannot advance Bone Voyage.
- Assert exactly one journal arm, one cheat arm, one completion call, and one
  writer for each primary/side transition.
- Run `python3 tools/questhelper_extract.py bonevoyage --check`.
- Run `make -C src mock230-scripts` and the intended-cache
  `mock230_pack --check-only` target after implementation.

### Automated transition tests

1. Exercise all four prerequisite combinations: reject when either or both are
   missing and accept only The Dig Site plus 100 Kudos; repeat/decline stay at
   0.
2. Exercise states 5→10→11→15→20 through real actors for qualified and
   unqualified guild paths.
3. Lose/destroy/bank proposal and agreement at every owning state; recover one
   without duplication; full inventory and repeated packets do not skip.
4. Embark and disembark at 21, 23, and 25; teleport/relog/death aboard returns
   to a valid state and location.
5. Obtain charm/potion in either order; test missing ingredients, exact
   consumption, full inventory, loss/replacement, both hand-in orders, and
   repeated hand-ins.
6. Start voyage at 30, exercise every control and speed, intentionally abort,
   fail into 35, relog/death/interface-close, retry, and then succeed.
7. Complete with full and non-full inventories; award 1 QP and exactly one note
   book; duplicate success/completion packets are no-ops.
8. Exercise every post-quest guard/crew menu op in both directions and after
   relog/restart.
9. Build bank chest with below/exact level, missing hammer, every missing
   material, weak nails if breakage applies, full/repeated packets, and relog;
   verify `%fossil_bank`, model, bank access, XP, and note-book entry.
10. Bind/unbind a digsite pendant, discover mushtrees, use representative
    island activities, and begin Dragon Slayer II from normal and cheat-made
    completion state.
11. Test Curator, sawmill, Odd Old Man, Apothecary, and pub overlaps with every
    active shared quest state in the matrix above.

### Live-client acceptance

Run one fresh-account-equivalent route from Haig through the reward scroll and
Museum Camp, once below and once at/above 60 Woodcutting. Capture:

- both prerequisite refusals and explicit quest acceptance;
- proposal/agreement grant, signing, loss recovery, and both guild routes;
- Embark/Disembark and all shared dialogue subjects;
- charm/potion acquisition, replacement, and both hand-ins;
- voyage interface layout, control packets, timing, failure, abort, retry,
  success, and cleanup;
- note-book reward, arrival conversation, Museum Camp landing, reward scroll,
  and completion journal;
- repeat barge travel in both directions, pendant binding, and note-book
  replacement; and
- the complete bank-chest build and use flow.

Finally execute `::complete quest_bonevoyage` twice on a clean player. The
first invocation must create a coherent completed/access state without leaving
voyage UI, temp values, or obsolete quest items; the second must be a no-op.

## 11. Audit evidence and disposition

Evidence collected for this record:

- full quest-root and cross-directory `rg` inventory;
- quest dbrow and pack-ID resolution (`29=quest_digsite`);
- native varbit bit-range inspection;
- NPC multi-config, menu-op, and world-spawn reconciliation;
- voyage interface/loc and Museum Camp utility asset inventory;
- OSRS Wiki API revision pinning and targeted article/transcript extraction;
- local Quest Helper source review at the pinned commit; and
- successful `python3 tools/questhelper_extract.py bonevoyage --check` symbol
  resolution, with the world-placement caveat documented above.

No gameplay code was changed and no compile, pack, automated transition, or
live-client run was claimed. Bone Voyage remains `audit-pending` until all P0
items, the relevant P1 recovery/shared-dispatch items, and Gates A–D pass with
recorded evidence.
