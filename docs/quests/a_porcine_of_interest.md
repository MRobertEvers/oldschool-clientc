# A Porcine of Interest modernization audit

Status: `audit-pending` — native quest state and shared completion are present,
but the normal gameplay route cannot reach the quest Sourhog or completion.

Audited: 2026-08-16

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to this quest and its Slayer integration.
It is an implementation specification, not completion evidence.

## 1. Authoritative references

These revisions are pinned so later implementation and review use the same
route, dialogue, combat, item, reward, and downstream Slayer requirements.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [A Porcine of Interest](https://oldschool.runescape.wiki/w/A_Porcine_of_Interest?oldid=15272507) | 15272507, 2026-07-22 | Requirements, full route, fight advice, rewards, music, and change history |
| [A Porcine of Interest/Quick guide](https://oldschool.runescape.wiki/w/A_Porcine_of_Interest/Quick_guide?oldid=14828170) | 14828170, 2024-12-28 | Ordered interactions, equipment, tracking trail, fight, foot, and finish |
| [Transcript:A Porcine of Interest](https://oldschool.runescape.wiki/w/Transcript:A_Porcine_of_Interest?oldid=15107528) | 15107528, 2026-01-17 | Choices, re-talks, cutscene lines, full-inventory branches, and post-quest task offer |
| [Sourhog](https://oldschool.runescape.wiki/w/Sourhog?oldid=15275486) | 15275486, 2026-07-25 | Quest and ordinary variants, attacks, protection, weaknesses, poison immunity, and kill credit |
| [Reinforced goggles](https://oldschool.runescape.wiki/w/Reinforced_goggles?oldid=15212469) | 15212469, 2026-05-17 | Protection, initial/replacement pairs, 100-coin post-quest purchase, and Slayer helmet use |
| [Spria](https://oldschool.runescape.wiki/w/Spria?oldid=15279957) | 15279957, 2026-07-29 | Unlock, task list, Sourhog exclusivity, shop/reward operations, and task policy |
| [Sourhog foot](https://oldschool.runescape.wiki/w/Sourhog_foot?oldid=15189868) | 15189868, 2026-04-22 | Cutting tools, destroy warning, and replacement behavior |
| [Safety in Numbers](https://oldschool.runescape.wiki/w/Safety_in_Numbers?oldid=15253420) | 15253420, 2026-07-05 | Music unlock location and track identity |

The source comments name Quest Helper's `aporcineofinterest`, but no Quest
Helper checkout or extracted fixture is present in this workspace. It can be
added as a transition/test aid; the pinned Wiki remains authoritative.

## 2. Native quest identity and player contract

The native `quest_porcineofinterest` dbrow supplies the core metadata:

| Field | Native value / expected behavior |
| --- | --- |
| Quest ID | 160 |
| Type | Members' quest |
| Difficulty / length | Novice / very short |
| Release date | 10 September 2020 |
| Start | Read the notice board behind Fortunato's Wine Shop in Draynor Village |
| End state | `%porcine = 40` |
| Quest points | 1 |
| Prerequisites / required levels | None |
| Required items | One rope; a knife or an allowed slash weapon for the foot, with a bronze scimitar available in the cave |
| Recommended combat | 20; this is advice, not a requirement |
| Mandatory enemy | The level-37 quest Sourhog |
| Direct rewards | 1 quest point, 5,000 coins, 1,000 Slayer XP, and 30 Slayer reward points |
| Unlocks | Sourhog Cave, Sourhog tasks from Spria, and Spria as a Slayer master |
| Equipment effects | Existing Slayer helmets gain reinforced-goggle protection; future helmets require reinforced goggles as a component |
| Replacement equipment | Reinforced goggles cost 100 coins from Slayer masters after completion |
| Music | `Safety in Numbers`, unlocked in Sourhog Cave |

The dbrow's Slayer XP is `10000` because `stat_advance` accepts tenths of an XP;
the quest constant and completion code preserve that representation correctly.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_porcineofinterest/configs/porcineofinterest.constant` | Names the eight primary milestones, rewards, and coordinates | Primary values and direct reward quantities align with native data |
| `server/scripts/quests/quest_porcineofinterest/configs/porcineofinterest.varp` | Redeclares native `porcine_main` | Correct permanent, transmitted carrier; native side-bit ownership is not documented here |
| `server/scripts/quests/quest_porcineofinterest/scripts/porcineofinterest.rs2` | Sarah/Spria dialogue, journal, completion, and debug walk | Uses modern choices and shared completion, but abridges dialogue and omits several unlocks |
| `server/scripts/quests/quest_porcineofinterest/scripts/porcineofinterest_locs.rs2` | Notice board, hole, skeleton, corpse, quest boss death, and soft kill | Tracking/blockage operations and boss creation are absent; cutscene and fight are soft-skipped |

### Mandatory shared and cross-directory files

| Path | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `server/scripts/shop/south_falador_farm/scripts/sarahs_farming_shop.rs2` | Owns Sarah's Farming Shop operation | Quest Talk-to safely delegates its non-quest branch to the real shop |
| `server/scripts/skill_slayer/scripts/slayer_masters.rs2` | Activates Spria and owns Talk-to/Assignment/Trade/Rewards | Completion gate is correct; Spria is incorrectly allowed to replace an existing task |
| `server/scripts/skill_slayer/scripts/slayer_assign.rs2` and generated task dbrows | Assignment selection and category data | Sourhogs exist as task/category data; verify only Spria assigns them in the current cache rules |
| `server/scripts/skill_slayer/scripts/slayer_helm.rs2` | Crafts Slayer helmets and exposes the common worn-helmet predicate | Recipe neither requires reinforced goggles after the quest nor supplies Sourhog protection/disassembly behavior |
| `server/scripts/shop/slayer_equipment/configs/slayer_equipment_shop.inv` | Shared Slayer Equipment stock | Explicitly omits reinforced goggles as “not shop-sold,” contrary to the current 100-coin stock |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dispatches native quest dbrow | Correct modern dispatch; quest-owned text skips tracking and recovery detail |
| `server/scripts/quests/scripts/quest_cheat.rs2` | Idempotently sets completion state | Keep as an end-state adapter, not evidence that gameplay is completable |
| `server/scripts/areas/world/configs/m47_51.spawn` | Spawns Sarah and the transforming sheepdog | Sarah and dog are present; Rosie's quest interaction/state is unimplemented |
| `server/scripts/areas/world/configs/m49_151.spawn` | Populates the Sourhog Cave | Spawns eight ordinary `sourhog` NPCs but never `porcine_sourhog_second` |
| `server/scripts/ladders_stairs/configs/ladders.loc` | Categorizes cave ropes for generic climbing | Both rope locs are categorized; verify the shared exit destination and quest-specific entry ownership in-client |

The generated spawn files must remain generated. A quest-owned encounter should
not be hand-added to `m49_151.spawn`; it needs player/encounter ownership so one
player cannot kill, duplicate, or strand another player's quest boss.

### Cache-native content already available

The osrs239 cache exposes more of the intended quest than the scripts use:

- permanent `%porcine` plus `porcine_footcut`, `porcine_inspected_cart`,
  `porcine_need_rope`, `porcine_rosie`, `porcine_stop_warning`,
  `porcine_ads_neverslain`, `porcine_ads_offeredtask`, and
  `porcine_ads_acceptedtask`;
- state-transforming hole, cabbage, potatoes, carrot, broken trees, damaged
  cart, skeleton, dead-Sourhog, sheepdog/Rosie, and related cave locs/NPCs;
- the `porcine_cave_blockage` Climb-over operation and quest-specific
  `porcine_sourhog_second` NPC;
- reinforced goggles, Sourhog foot, skeleton note/rope, and the bronze
  scimitar available near the skeleton; and
- `synth_aporcineofinterest` plus the `Safety in Numbers` music dbrow.

Only `porcine_footcut` is written by the quest. The other seven native side
bits are unused, even though their names correspond directly to missing cart,
rope, Rosie, warning, and post-completion task-offer behavior.

## 4. Persisted state model and reachability

`%porcine` is permanent and transmitted through `porcine_main`. The eight
primary states align with cache transforms, but several actions do not enforce
the milestone implied by their state.

| Value | Symbol | Intended milestone | Current normal writer / defect |
| ---: | --- | --- | --- |
| 0 | `poi_not_started` | Notice not accepted | Default/reset; Yes/No notice board is implemented correctly |
| 5 | `poi_sarah` | Accepted notice; speak to Sarah | Notice-board Yes branch |
| 10 | `poi_rope` | Sarah interviewed; investigate cart and follow produce/tree trail | Sarah dialogue writes 10, but every visible tracking loc lacks a trigger |
| 15 | `poi_cave` | Rope secured to the hole; investigate cave | Using a rope on the hole consumes one and writes 15; cart/trail can be ignored |
| 20 | `poi_spria` | Pig Thing encounter complete; wake and speak to Spria | Skeleton text replaces the entire cutscene, writes 20, and teleports directly to Spria |
| 25 | `poi_kill` | Goggles received; return and kill quest Sourhog | Spria safely grants goggles, but mere inventory possession permits entry; no boss is created and blockage has no trigger |
| 30 | `poi_foot` | Quest Sourhog defeated; collect its foot | Only `porcine_sourhog_second` death or debug soft-kill writes 30; that NPC has no normal spawn |
| 35 | `poi_finish` | Foot given to Sarah; return to Spria | Sarah consumes one foot and grants 5,000 stackable coins |
| 40 | `poi_complete` | Completion and Slayer unlocks | Spria sets state, XP, points, and shared completion; reachable only after state 30 is injected/soft-killed |

The eight ordinary Sourhogs in the cave use ordinary death/drop behavior and do
not advance the quest. The scripted quest-boss death callback therefore cannot
run during normal play. `::poirun` masks the break by calling
`~poi_soft_kill_sourhog` and assigning the otherwise unreachable state 30.

## 5. Current versus required playable route

### Stage 1 — notice board, Sarah, and tracking

Required behavior:

1. Checking the notice board presents the bounty and an acceptance choice;
   refusal leaves state 0 and acceptance sends the player to Sarah.
2. Sarah's transcript supports questions about the attack, appearance, tips,
   location, Rosie, and the non-quest shop conversation.
3. At the crossroads the player investigates the damaged cart first, then
   follows the visible cabbages, carrots, potatoes, and broken trees to the
   hole. Trail-prop interactions give contextual evidence without inventing an
   unsupported requirement to click every breadcrumb in a strict order.
4. The cart/trail gives state-aware re-click feedback and the hole explains the
   need for rope. A rope must be used on the hole, consumed once, and remain
   visibly tied.

Current notice acceptance, Sarah/shop delegation, and rope consumption are
reasonable modern foundations. Sarah's investigation is collapsed into a few
fixed lines; every cache-visible tracking prop has an `Investigate` option but
no RuneScript trigger. The player can walk directly to the hole and attach a
rope without inspecting the cart or trail. `porcine_inspected_cart` and
`porcine_need_rope` are unused.

### Stage 2 — skeleton and Pig Thing cutscene

Required behavior:

1. Climbing down unlocks `Safety in Numbers`; the shared climb-up rope returns
   the player to the correct surface tile.
2. The pile of rope may be investigated and the skeleton supplies its note and
   ominous discovery dialogue.
3. Investigating the skeleton starts the complete Pig Thing sequence: the
   creature appears behind the player, spits acid, reacts to gnome goggles when
   applicable, knocks the player out, and leaves before Spria rescues them.
4. The cutscene owns player input and transient NPCs, resumes or concludes
   safely on logout, and cannot be disrupted by unrelated follower/pet clicks.

Current skeleton interaction emits a few lines, says the player blacked out,
writes state 20, and teleports to Spria. It has no staged NPC, camera, acid
attack, animations, conditional goggles line, Spria rescue, or interruption
contract. The pile of rope has no quest trigger, and no music-unlock script was
found for `Safety in Numbers`.

### Stage 3 — Spria, cave re-entry, and quest Sourhog

Required behavior:

1. Spria explains Sourhogs, their acidic saliva, protective goggles, and the
   monster's weaknesses. A full inventory leaves the player at the claim state;
   lost goggles can be replaced without duplication.
2. The player must equip reinforced goggles before entering the dangerous
   section. Carrying them in inventory is insufficient.
3. Climbing over the blockage shows the native Yes/No danger warning and starts
   or enters an owned quest-Sourhog encounter.
4. The level-37 quest Sourhog uses both crush/melee and acidic ranged attacks.
   Without correct protection the saliva has its current high-damage and
   Attack/Defence-drain behavior; reinforced goggles and qualifying reinforced
   Slayer helmets protect the player.
5. Stab/ranged weakness, slash/magic resistance, poison immunity, aggression,
   and recoil kill-credit behavior match the pinned monster reference.
6. Death, logout, leaving, repeated blockage clicks, and another player nearby
   cannot duplicate the boss or award another player's kill.

The initial and lost-goggle grants are capacity-safe and mostly duplicate-safe.
However, cave entry accepts goggles in inventory despite saying they must be
worn. `porcine_cave_blockage` has no handler, `porcine_stop_warning` is unused,
and no normal spawn or `npc_add` exists for `porcine_sourhog_second`. If that
NPC is externally injected, it falls back to general combat and a death
callback; no quest-specific acid/protection/stat-drain, resistance, poison, or
encounter-ownership implementation was found.

### Stage 4 — foot, Sarah, and completion

Required behavior:

1. The corpse yields one Sourhog foot when cut with a knife or allowed slash
   weapon. The abyssal whip, abyssal tentacle, noxious halberd, and dragon claws
   are excluded; the cave's bronze scimitar is a valid fallback.
2. Full inventory does not alter corpse state. If the foot is destroyed or
   otherwise lost, the player can return to the corpse and retrieve another.
   Holding the foot blocks unnecessary re-entry until it is delivered.
3. Sarah consumes exactly one foot, grants 5,000 coins, and gives the hooded-
   woman/Spria dialogue before directing the player back to Spria.
4. Spria awards one quest point, 1,000 Slayer XP, and 30 Slayer points once;
   activates the cave, Spria, and Sourhog task unlocks; and makes every existing
   Slayer helmet provide reinforced-goggle protection as required.
5. If the player has no current Slayer assignment, Spria offers one immediately
   after completion, with the native offered/accepted bits preserving the
   transcript branches across interruption.

Current foot cutting accepts only a knife. It correctly refuses full inventory
and duplicates, but setting `porcine_footcut = 1` permanently transforms the
corpse; destroying the foot leaves no replacement route. Sarah's hand-in is
safe for stackable coins but omits the hooded-woman exchange. Completion grants
the direct numeric rewards through modern shared infrastructure, but does not
upgrade existing Slayer helmets or offer a task. All three native
`porcine_ads_*` bits are unused.

## 6. Downstream Slayer contract

Quest completion is not done when the reward scroll closes. The following
post-quest behavior belongs in the same acceptance boundary:

- the transforming Spria NPC exposes Talk-to, Assignment, Trade, and Rewards
  only at `%porcine >= 40`;
- Spria gives no per-task Slayer points, shares the current 40-point block list
  with Turael/Aya, and has Turael's task list plus Sourhogs;
- following the 8 May 2024 change, Turael no longer assigns Sourhogs;
- Spria cannot perform Turael's free task-replacement/task-streak reset service;
- the Slayer Equipment shop sells reinforced goggles for 100 coins after the
  quest, while pre-completion access remains through Spria's quest grant; and
- reinforced goggles and reinforced Slayer helmets protect against ordinary
  Sourhogs as well as the quest variant.

The completion gate and active Spria operations exist. Shared block-list/cost
comments appear aligned with the current Wiki. The free-reset helper, however,
returns true for both Turael and Spria, then deliberately avoids resetting the
streak for Spria. Current references say Spria cannot change the task at all,
so both allowing the replacement and preserving the streak are wrong. The
equipment shop also omits the 100-coin goggles entirely.

## 7. Gap and oversight register

| Priority | Area | Current defect | Required correction |
| --- | --- | --- | --- |
| P0 | End-to-end reachability | No normal spawn/creation exists for `porcine_sourhog_second`; ordinary cave Sourhogs do not advance state. | Create a player/encounter-owned quest boss behind the blockage and prove a no-debug start-to-finish run. |
| P0 | Blockage | The cache loc has Climb-over but no trigger; the native warning bit is unused. | Implement state/equipment checks, Yes/No warning, movement/entry, idempotent encounter creation, and return/re-entry behavior. |
| P0 | Combat/protection | Quest boss has no Sourhog acid attack, protection, drain, resistance, poison-immunity, or kill-credit policy. | Implement/share authoritative Sourhog combat mechanics and test goggles, reinforced helmets, recoil, poison, and weapon styles. |
| P1 | Tracking | Cart, produce, and broken-tree locs are visible but inoperable; the hole bypasses even the damaged-cart investigation. | Gate discovery on the cart with native side state and implement contextual trail interactions/re-click/journal feedback without requiring an unsupported strict breadcrumb order. |
| P1 | Cutscene | The entire Pig Thing/Spria rescue sequence is replaced with text and teleport. | Build a restart-safe, input-owned modern cutscene using native NPCs, cameras, animations, and conditional gnome-goggles dialogue. |
| P1 | Equipment enforcement | Cave entry accepts goggles in inventory although the dialogue and Wiki require them equipped. | Query effective worn protection only, including reinforced Slayer-helmet variants. |
| P1 | Foot tools/recovery | Only `knife` works; allowed slash weapons do not. Once `porcine_footcut` is set, a destroyed/lost foot is unrecoverable. | Use the shared weapon attack-style/profile API plus explicit exclusions; restore a replaceable corpse state when no foot is held/banked as appropriate. |
| P1 | Helmet reward | Existing Slayer helmets receive no Sourhog protection, and post-quest helmet assembly does not require reinforced goggles. | Derive effective protection centrally from completion plus any supported Slayer-helmet variant; require/consume goggles for future post-quest assembly and return the component on disassembly. Do not invent duplicate helmet item variants. |
| P1 | Goggles shop | Shared shop explicitly omits the current 100-coin post-quest stock. | Add conditional post-quest stock/purchase behavior without exposing it early; correct the misleading comment. |
| P1 | Completion task offer | Spria never offers an immediate task and all three native advertisement bits are unused. | Implement transcript-accurate no-task/has-task offer branches and persist offer/accept state idempotently. |
| P1 | Spria task policy | Shared helper lets Spria replace another master's task without wiping the streak. | Restrict free replacement to Turael/Aya; Spria must describe the existing assignment instead. |
| P1 | Dialogue | Sarah/Spria branches, re-talks, monster explanation, Rosie, hooded woman, and post-quest lines are abridged. | Reconcile the pinned transcript and make every branch reachable under its real predicate. |
| P1 | Journal | State 10 tells the player to find the hole, skipping the required cart/trail; recovery and blockage objectives are absent. | Render primary plus side-state objectives, including missing rope/goggles/foot and pending reward/task offer. |
| P1 | Music | No quest script unlocks `Safety in Numbers` on cave entry. | Connect cave entry to the shared music-unlock service and prove persistence/idempotence. |
| P1 | State ownership | Seven cache-native Porcine side bits are unused while content is collapsed. | Confirm semantics and make native fields canonical where they represent client-visible/permanent behavior. |
| P1 | Completion atomicity | State 40 is written before XP, points, and shared quest completion execute. | Verify RuneScript action atomicity or retain a durable claim checkpoint so interruption cannot lose or duplicate any reward. |
| P1 | Test validity | `::poirun` directly assigns every state and calls a soft-kill. | Test real operations and authoritative encounter callbacks; keep direct state assignment only for isolated cheat-adapter tests. |
| P2 | Presentation | Exact animations, spot effects, camera timing, chase staging, examine text, and the katana easter egg are absent/unaudited. | Reconcile after the critical route works; document only genuinely cosmetic deviations. |

## 8. Modern-engine assessment

Parts to retain:

- native permanent `%porcine` and cache-native Porcine side bits;
- symbolic cache names rather than raw NPC, loc, object, interface, or map IDs;
- modern `~p_choice2` acceptance and Sarah conversation choices;
- cache-driven loc/NPC transforms;
- dbrow-based journal dispatch and shared `~quest_complete_rewards`; and
- the existing cross-file ownership boundary for Sarah's shop and Spria's
  general Slayer operations.

No `if_openmain` or `if_openoverlay` exists in the quest root. The principal
problem is therefore incomplete gameplay built around modern lifecycle pieces,
not an old modal interface that needs a mechanical rewrite. The target should
remain content-first:

```text
native quest + side state
          |
          v
ordered tracking and restart-safe cutscene
          |
          v
owned Sourhog encounter + shared Sourhog mechanics
          |
          v
atomic quest completion
          |
          +--> helmet/goggles protection and equipment stock
          +--> Spria assignment/task-policy integration
```

Do not add a quest-specific engine opcode to compensate for missing content.
Use existing movement, choice, cutscene, combat, inventory, NPC ownership, music,
shop, and Slayer services. If an owned encounter or equipment-component query
is genuinely unavailable after repository-wide proof, add the smallest reusable
engine capability with general tests, then keep Porcine policy in RuneScript.

## 9. Implementation sequence

### POI-1 — formalize state and ownership contracts

- Add the quest and every mandatory external file above to the generated
  implementation manifest.
- Confirm every primary-state and side-bit transform, the exact climb-up/down
  destinations, music-unlock service, weapon-profile query, Slayer helmet
  variants, and encounter ownership API.
- Define one authoritative Sourhog protection/mechanics service usable by the
  quest boss and ordinary Sourhogs.

Acceptance: every native field and cross-file behavior has one owner; no state
or debug action substitutes for a missing interaction.

### POI-2 — implement tracking and cave discovery

- Complete Sarah's transcript choices and re-talks, including Rosie/shop paths.
- Implement the damaged-cart gate, contextual produce/tree trail, rope-needed
  state, re-click feedback, journal updates, and atomic rope consumption.
- Verify both cave ropes and unlock `Safety in Numbers` on first cave entry.

Acceptance: state 10 cannot advance to 15 until the cart has been investigated;
trail props guide without imposing an invented order, and repeated-use/relog
cases remain coherent.

### POI-3 — implement the Pig Thing cutscene

- Stage the Pig Thing, player, skeleton, acid attack, Spria rescue, cameras,
  animations, conditional gnome-goggle line, blackout, and wake-up dialogue.
- Own input/followers/transient entities and define logout/disconnect cleanup or
  a stable resume checkpoint.

Acceptance: normal skeleton interaction reaches state 20 through the full
sequence once, cannot be disrupted into a stale state, and leaves no NPC leak.

### POI-4 — implement the quest Sourhog encounter

- Require effective worn goggles/helmet protection and implement the blockage
  warning plus owned encounter start/re-entry/cleanup.
- Implement Sourhog crush and ranged acid attacks, unprotected effects,
  protection, aggression, affinities, poison immunity, and kill credit.
- Advance state 25 to 30 only from the authoritative quest-boss death.

Acceptance: one player's real level-37 Sourhog kill advances only that player;
death/logout/leave/repeated clicks and nearby players cannot duplicate or steal
the encounter; ordinary Sourhogs never satisfy the quest kill.

### POI-5 — implement item and completion fidelity

- Support the knife and allowed slash-weapon matrix with exact exclusions.
- Make foot acquisition, destruction, loss, replacement, full inventory, and
  Sarah consumption atomic and transcript-accurate.
- Complete Sarah/Spria dialogue, direct rewards, the completion-gated helmet
  protection/recipe contract, immediate task offer, and durable completion
  ordering.

Acceptance: all item/reward cases lose and duplicate nothing; one real route
reaches state 40 and repeat completion calls cannot award anything twice.

### POI-6 — complete downstream Slayer integration and verification

- Add 100-coin post-quest reinforced goggles to the correct shared shop path.
- Ensure only Spria assigns Sourhogs, Spria awards zero points per task, current
  block sharing/cost is correct, and Spria cannot perform Turael replacement.
- Remove active soft-skip/deferred markers, replace `::poirun` with real-trigger
  orchestration or retire it, compile/pack, and record real-client smoke.

Acceptance: quest and ordinary Sourhog protection work through goggles and all
reinforced helmet variants; Spria/shop/task behavior matches the pinned current
references; all modernization Gates A–D pass.

## 10. Verification matrix

| Scenario | Required assertions |
| --- | --- |
| Notice | No remains at 0; Yes reaches 5; subsequent checks do not restart or duplicate dialogue state |
| Sarah | All investigation/Rosie/shop/re-talk branches are reachable; accepting the hunt reaches 10 exactly once |
| Tracking | The cart is required before the hole; cabbage/carrot/potatoes/trees give correct contextual and repeated feedback without an unsupported mandatory order |
| Rope | Wrong item fails; no rope fails; one rope is consumed once; repeated use does not consume more; loc transform and journal agree |
| Cave travel/music | Climb-down/up destinations are correct at every eligible state; `Safety in Numbers` unlocks once and persists |
| Cutscene | Normal and gnome-goggle lines differ correctly; input/pet activity cannot break staging; logout/reconnect cleans or resumes safely |
| Goggles grant | Zero-slot case retains claim; initial and replacement grants do not duplicate against inventory, worn, or bank copies |
| Protection gate | Inventory-only goggles fail; worn goggles pass; every reinforced helmet variant passes; unreinforced headgear fails |
| Blockage/ownership | No/Yes warning paths, repeated clicks, concurrent players, death, leave, logout, and re-entry produce at most one owned boss |
| Sourhog combat | Melee/ranged acid attacks, protected/unprotected effects, affinities, poison immunity, aggression, and recoil/player kill credit match references |
| Quest kill | Only authoritative `porcine_sourhog_second` death at state 25 writes 30; ordinary Sourhog/debug/another player's kill cannot |
| Foot tools | Knife and representative valid slash profiles work; every named exclusion fails; cave bronze scimitar works |
| Foot lifecycle | Full inventory does not cut; duplicate is refused; Destroy warns; no-foot corpse permits replacement; exactly one foot reaches Sarah |
| Sarah reward | One foot is consumed and exactly 5,000 coins are granted; hooded-woman dialogue/re-talks are correct; repeated click cannot duplicate coins |
| Completion | Exactly 1,000 Slayer XP, 30 points, and one quest point are awarded; interruption/repeat calls lose or duplicate nothing |
| Helmets | Every supported existing normal/imbued/variant helmet derives protection after completion without item mutation; future assembly/disassembly requires/returns goggles correctly |
| Task offer | No-task completion offers once; accept assigns once; decline/relog/has-task branches use native advertisement bits coherently |
| Post-quest shop | Reinforced goggles cost 100 coins after completion and are unavailable through that stock before completion |
| Spria policy | Active only after 40; assigns Sourhogs and correct shared tasks; grants zero task points; shares correct blocks; never replaces a current task |
| Cheat adapter | First `::complete quest_porcineofinterest` reaches 40 and accounts for one quest point; second invocation is a no-op |

Minimum repository checks after implementation:

```sh
tools/questhelper_extract.py aporcineofinterest --check
make -C src mock230-scripts
mock230_pack --check-only
```

The Quest Helper command is conditional on adding the missing helper source or
fixture. Also record automated quest-state/item/combat/Slayer tests and real-
client packet/screenshot evidence; script compilation cannot prove encounter,
cutscene, equipment-protection, or shop behavior.

## 11. Definition of done

A Porcine of Interest may be marked `verified-modern` only when:

- the real notice-board start reaches state 40 without direct state assignment
  or a soft-kill;
- Sarah, tracking, rope, cave travel, Pig Thing cutscene, Spria, blockage,
  quest Sourhog, foot, and final conversations match the pinned route and
  transcript, including refusal, re-talk, loss, full-inventory, and relog paths;
- the quest Sourhog is an owned, fully mechanical encounter and protection is
  based on equipped goggles or a reinforced Slayer helmet;
- direct rewards, completion-gated helmet protection/assembly, the immediate
  task offer, Sourhog task unlock, Spria policy, and 100-coin replacement
  goggles are exact, durable, and idempotent;
- native state/cache assets and shared systems are used coherently, with no
  raw-ID, generated-spawn edit, or quest-specific engine shortcut;
- no active critical soft-skip, deferred route marker, legacy panel open, or
  direct-state debug runner remains; and
- script compilation, cache packing, automated state/item/cutscene/combat/
  Slayer coverage, real-client smoke evidence, and idempotent cheat evidence
  are recorded in this file.
