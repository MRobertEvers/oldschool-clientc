# Animal Magnetism modernization audit

Status: `audit-pending` — a seven-file quest implementation, native quest row,
journal, completion call, puzzle shell, reward XP, and portions of every route
chapter exist. The legitimate route is nevertheless blocked: Malcolm has no
world spawn, and the first post-magnet write uses state 151 even though the
cache transform requires state 150. Earlier farm states and the chicken
cutscene are skipped, several quest-item transactions contradict the
transcript, and the post-quest device system is largely absent.

Audited: 2026-08-16

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the quest-owned scripts, Alice's farm,
the shared Old crone and Slayer Master dispatchers, the research-notes panel,
Ava's shop and device options, ammunition recovery, Dragon Slayer II, and the
Lumbridge & Draynor Medium Diary. It is an implementation specification, not
completion evidence.

## 1. Authoritative references

These revisions are pinned so implementation and review use stable
requirements, route, dialogue, replacement, puzzle, reward, and device
contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Animal Magnetism](https://oldschool.runescape.wiki/w/Animal_Magnetism?oldid=15292390) | 15292390, 2026-08-11 | Requirements, complete route, items, rewards, current Turael/Aya behavior, and downstream requirements |
| [Animal Magnetism/Quick guide](https://oldschool.runescape.wiki/w/Animal_Magnetism/Quick_guide?oldid=15126950) | 15126950, 2026-02-14 | Ordered interactions, repeated farm conversations, item actions, and travel checkpoints |
| [Transcript:Animal Magnetism](https://oldschool.runescape.wiki/w/Transcript%3AAnimal_Magnetism?oldid=15263367) | 15263367, 2026-07-14 | Accept/refuse choices, all re-talks, chicken cutscene, input failures, item loss/replacement, and post-stage dialogue |
| [Ava's device](https://oldschool.runescape.wiki/w/Ava%27s_device?oldid=15271380) | 15271380, 2026-07-21 | Device tiers, supported ammunition, save/drop/break rates, and interference policy |
| [Ava](https://oldschool.runescape.wiki/w/Ava?oldid=15153599) | 15153599, 2026-03-22 | Device purchase, 999-coin recovery, upgrades, DS2 role, and post-quest services |
| [Ava's accumulator](https://oldschool.runescape.wiki/w/Ava%27s_accumulator?oldid=15186234) | 15186234, 2026-04-22 | 50 Ranged gate, 75-steel-arrow upgrade, alternate payment, passive attraction, death, and exact 72/8/20 outcomes |
| [Ava's attractor](https://oldschool.runescape.wiki/w/Ava%27s_attractor?oldid=15270656) | 15270656, 2026-07-20 | Level-30 device behavior, replacement, passive attraction, and exact 60/20/20 outcomes |
| [Blessed axe](https://oldschool.runescape.wiki/w/Blessed_axe?oldid=15254568) | 15254568, 2026-07-05 | Mithril axe plus holy symbol recipe, ownership, use, destruction, and replacement expectations |
| [Crone-made amulet](https://oldschool.runescape.wiki/w/Crone-made_amulet?oldid=15183398) | 15183398, 2026-04-22 | Non-consumption of the ghostspeak amulet, delivery, destruction, and free replacement |
| [Malcolm](https://oldschool.runescape.wiki/w/Malcolm?oldid=15287765) | 15287765, 2026-08-05 | Farm location, amulet transform, chicken sale, and cutscene role |
| [Research notes](https://oldschool.runescape.wiki/w/Research_notes_%28Animal_Magnetism%29?oldid=15185530) | 15185530, 2026-04-22 | Nine-button puzzle, fixed solution, resulting item, and replacement source |

The current Wiki additionally identifies Animal Magnetism as required for
Dragon Slayer II and for the Lumbridge & Draynor Medium Diary. The medium task
is to purchase an upgraded device from Ava at 50 non-boostable Ranged using 75
steel arrows and either an attractor or 999 coins.

Transition aid only: the local Quest Helper checkout's
[`AnimalMagnetism.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/animalmagnetism/AnimalMagnetism.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` identifies the native
0–230 checkpoints, relevant zones, and Turael/Aya alternates. It guides
transition tests but does not override the Wiki, transcript, or osrs239 cache.

`python3 tools/questhelper_extract.py animalmagnetism --check` resolves every
named item, NPC, loc, varbit, and quest row. Quest Helper marks the holy symbol
as not consumed, but both the transcript's “hand over” language and the Wiki
product recipe define one holy symbol as a material. The authoritative sources
therefore support the current consumption of that specific input.

## 2. Native quest identity and player contract

The cache-native `quest_animalmagnetism` row and pinned sources define this
contract:

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 123; OSRS release-order number 116 |
| Type | Members' quest |
| Difficulty / length | Intermediate / medium |
| Release date | 12 December 2006 |
| Start | Talk to Ava on the ground floor of Draynor Manor's west wing |
| Prerequisites | The Restless Ghost, Ernest the Chicken, and Priest in Peril complete |
| Required levels | 18 Slayer, 19 Crafting, 30 Ranged, and 35 Woodcutting; all non-boostable and required to start |
| Required items | Mithril axe, five unnoted iron bars, ghostspeak amulet, 20 ecto-tokens, hammer, hard leather, holy symbol, and polished buttons |
| Required combat | None |
| Primary state | `%anma_main`, cache varbit `anma_main` on `anma_base_1`, bits 0–7 |
| Native side state | No quest-specific permanent side varbit found; puzzle button state is interface-session state |
| End state | 240 |
| Quest points | 1 |
| XP rewards | 1,000 Crafting, 1,000 Fletching, 1,000 Slayer, and 2,500 Woodcutting |
| Item reward | Ava's attractor below 50 base Ranged; Ava's accumulator at 50 or higher |
| Unlocks | Trade with Ava; device purchase, recovery, and upgrade services; device ammunition recovery |
| Required for | Dragon Slayer II and the Lumbridge & Draynor Medium Diary |

The dbrow already holds the start NPC/coordinate, prerequisite rows, four skill
requirements, four XP rewards, quest point, and end state. Content must enforce
those values; metadata alone does not make a playable quest.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_animalmagnetism/configs/anma.constant` | Primary-state names, requirements, rewards, and debug coordinates | Several native states are omitted; values 151–231 are deliberately shifted off the canonical tens based on an incorrect transform-index assumption |
| `server/scripts/quests/quest_animalmagnetism/configs/anma.varp` | Native carrier, Priest in Peril carrier, and temporary puzzle bits | `anma_base_1` and temporary ownership are sensible; configuring a prerequisite quest's whole carrier here is misplaced ownership |
| `server/scripts/quests/quest_animalmagnetism/scripts/anma.rs2` | Ava, start gate, main route, journal, completion, and debug runners | Broad route shell exists, but start checks current levels, completion commits state before reward delivery, and post-quest Ava is a placeholder |
| `server/scripts/quests/quest_animalmagnetism/scripts/anma_farm.rs2` | Malcolm, Alice, Old crone, amulet, and chicken purchase | Explicitly skips states 30–60 and the state-80/90 cutscene; consumes/recharges items contrary to transcript |
| `server/scripts/quests/quest_animalmagnetism/scripts/anma_witch.rs2` | Witch, selected iron, Rimmington mine, and magnet | Main recipe exists; loss replacement is incorrectly charged and facing north is forced rather than validated |
| `server/scripts/quests/quest_animalmagnetism/scripts/anma_trees.rs2` | Undead-tree attempts, Turael dialogue, blessed axe, and twigs | Route is unreachable after the wrong state write; Turael state ownership and alternate forms are incomplete |
| `server/scripts/quests/quest_animalmagnetism/scripts/anma_notes.rs2` | Research-notes panel, fixed solution, pattern, and container | Correct logical solution exists, but uses legacy `if_openmain` and does not drive the panel's authored random/complete layers |

These seven files total 891 lines. This is not an empty scaffold: meaningful
dialogue, item checks, a fixed puzzle solution, XP, and a completion call exist.
The audit result is still `audit-pending` because two hard reachability defects
and several knowingly skipped native phases prevent legitimate completion.

### Mandatory shared and cross-directory surfaces

| Path | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dynamic quest-list dispatcher | Calls `~anma_journal`; retain this modern dbrow route after expanding journal checkpoints |
| `server/scripts/quests/scripts/quest_cheat.rs2` | `::complete` adapter | Writes 240 only; useful for registry invariants, not proof of reward or post-quest services |
| `server/scripts/areas/world/configs/m48_52.spawn` | Ava, Witch, and Draynor undead-tree carriers | Correctly spawns `anma_assistant_multi`, `anma_witch_multi`, and the native tree carriers |
| `server/scripts/areas/world/configs/m56_55.spawn` | Alice | Spawns `farming_shopkeeper_4` at 3627,3526 |
| `server/scripts/areas/world/configs/m54_55.spawn` | Old crone | Spawns shared `ahoy_crone` at 3461,3558 |
| `server/scripts/areas/world/configs/m45_55.spawn` | Turael | Spawns base Turael; later WGS transforms and Aya require the same quest subject arbitration |
| Farm world spawns | Malcolm | No `anma_ghost_farmer_multi`, base Malcolm, or amulet Malcolm spawn exists in any world spawn file; only `::anmafarm` dynamically adds him |
| `server/scripts/quests/quest_ghostsahoy/` | Shared Old crone | Its hub is called before Animal Magnetism and can consume the click; both active quest subjects need explicit choice/arbitration |
| `server/scripts/skill_slayer/scripts/slayer_masters.rs2` | Shared Turael/Aya Talk-to | Handles only base Turael for Animal Magnetism and runs intro plus axe offer in one click; Aya and WGS Turael forms bypass the quest |
| `server/scripts/shop/draynor_manor/` | Ava's Odds and Ends | Shop data is present but op3 is bound to hidden carrier `anma_assistant_multi`, not clickable live Ava, and has no completion gate |
| `server/scripts/skill_combat/scripts/player/player_ranged.rs2` | Ava-device ammunition behavior | Implements nominal save percentages but wrong outcome composition, incomplete ammo types, and no metallic-torso interference |
| `server/scripts/quests/quest_dragonslayer2/` | Downstream prerequisite and Ava locator-orb branch | No Animal Magnetism prerequisite check exists; Ava's DS2 branch is deliberately allowed before her own quest gate |
| `server/scripts/interface_diaries/` | Lumbridge & Draynor Medium Diary | Generic counters/journal exist; no Ava device-upgrade task or Animal Magnetism gate was found |
| `interfaces/anma_rgb.{if,compack}` | Native research-notes interface 480 | Contains 59 IF1 components, including three complete models, six random models, nine pairs of buttons, layers, and close control |
| `configs/all.{dbrow,varp,varbit,npc,obj,seq,spotanim}` | Native identity and assets | Contains the quest row/state, transforms, quest objects, device stats, chicken cutscene actors, and dedicated animations/spotanims |

### Cache-native content already available

The cache contains more authored content than the route currently uses:

- Malcolm's base, amulet, cutscene, and sack variants;
- Alice, Cow31337Killer, undead cow, and undead chicken cutscene actors;
- the full `anma_main`-driven Malcolm and undead-tree transform tables;
- Ava/Witch carriers and live forms;
- the selected iron, bar magnet, blessed axe, undead twigs, both notes, pattern,
  container, polished buttons, attractor, and accumulator objects;
- the complete research-notes interface model/layer graph; and
- quest-specific animations, spotanims, and the “Here Chicky Chicky!” and
  “Chicken Grabbed” audio assets.

Modernization should connect these symbolic assets through normal world,
cutscene, UI, item, and reward machinery. The debug procs' temporary NPCs must
not remain the only way to see required actors.

## 4. Native state model and current reachability

The cache transform tables and Quest Helper produce this canonical primary
transition model. A transform component numbered `N + 1` is selected by varbit
value `N`; the local constants incorrectly treated the component number as the
state value after 150.

| State | Required phase | Current implementation / defect |
| ---: | --- | --- |
| 0 | Ava offer and acceptance/refusal | Offer exists with three choices, but requirements use current rather than base levels and failure is one generic message |
| 10 | Speak to Malcolm with ghostspeak equipped | Malcolm has no legitimate world spawn, so the route stops here |
| 20 | Speak to Alice | Implemented in shortened form |
| 30 | Return to Malcolm | Omitted |
| 40 | Return to Alice | Omitted |
| 50 | Return to Malcolm | Omitted |
| 60 | Return to Alice; learn about the Old crone | Omitted; local Alice jumps 20 directly to 70 |
| 70 | First Old crone conversation | Local code combines both conversations and immediately creates the amulet |
| 73 | Second Old crone conversation; mirror amulet | Omitted; local code consumes the ghostspeak amulet even though it must remain available/equipped |
| 76 | Give crone-made amulet to Malcolm | Basic item hand-in exists, but ghostspeak equipment is not required |
| 80 | Talk to transformed Malcolm | Omitted as a distinct conversation; purchase path opens immediately |
| 90 | Malcolm/Alice/chicken cutscene | Entire cutscene is absent despite native actors |
| 100 / 110 | Buy two chickens, then take them to Ava | Local purchase supports buying the missing quantity and writes 100; native 110 is not represented |
| 120 | Talk to Witch | Implemented in shortened form |
| 130 | Give five iron bars; receive selected iron | Implemented, with capacity/repetition cases needing atomic tests |
| 140 | Face north in Rimmington mine and hammer selected iron | Zone and hammer checks exist; direction is forcibly changed instead of checked |
| 150 | Give magnet to Ava, then first tree attempt | Ava deletes magnet and writes **151**, hiding every native tree carrier; route becomes blocked |
| 160 | Report the bounced axe / learn about Turael | Local equivalent is 161; hidden trees make it unreachable legitimately |
| 170 | Second Turael conversation and blessed-axe exchange | Local equivalent is 171; base Turael currently performs both conversations in the same click |
| 180 | Chop a tree until undead twigs are obtained | Local equivalent is 181; it uses an unverified 70% success roll and one fixed mithril animation |
| 190 | Give twigs to Ava | Local equivalent is 191 |
| 200 | Receive and translate research notes | Local uses 201 then 211, creating two non-native checkpoints |
| 210 | Give translated notes to Ava | Local uses 221 |
| 220 | Combine pattern, hard leather, and polished buttons | Local uses 231 |
| 230 | Give container to Ava | Local inserts 235 for the hand-in |
| 240 | Complete | End state matches the dbrow, but it is committed before XP, device, and completion-scroll delivery |

The transform error is deterministic. `nasty_tree` has
`multinpc151=nasty_tree_choppable`, which corresponds to state 150. State 151
selects `multinpc152=-1`. The pattern repeats at components 161, 171, and so on:
canonical states 160, 170, 180, 190, 200, 210, 220, 230, and 240 remain
choppable, while the locally shifted values select the immediately following
hidden entries.

Malcolm is an earlier independent blocker. The debug runner teleports to his
coordinate and executes `npc_add`, which can make a developer walkthrough look
viable while the production world remains empty. Alice, the Old crone, Witch,
Turael, Ava, and the trees already have real spawns; the debug runner also adds
several of them redundantly and can mask duplicate-actor defects.

## 5. Current versus required playable route

### Stage 1 — start requirements and Ava's offer

Required behavior:

- expose the offer only after all three prerequisite quests are complete;
- test base 18 Slayer, 19 Crafting, 30 Ranged, and 35 Woodcutting because none
  is boostable and all are start gates;
- show the transcript's requirement-specific refusal/journal information;
- support help, both refusal branches, re-talk, and normal Ava services without
  starting the quest accidentally; and
- write state 10 only after the accepted conversation completes.

`~anma_has_requirements` uses `stat(...)`, not `stat_base(...)`. Depending on
the VM's stat semantics, boosts can admit an ineligible player and drains can
reject an eligible one. The implementation should use the established
non-boostable requirement helper or explicit base-stat checks. Ava's DS2
locator-orb branch runs before this gate, while the local DS2 quest does not
require Animal Magnetism at all.

The current offer has a useful three-way modern `~p_choice3` and recognizable
dialogue, but the flirt choice simply ends instead of returning to the helping
decision. This branch and all re-talks should be reconciled line by line with
the pinned transcript.

### Stage 2 — Malcolm, Alice, and the crone-made amulet

Required behavior:

- spawn the native Malcolm carrier at 3618,3526 so his appearance follows
  `%anma_main` for every player;
- require a worn ghostspeak amulet to understand Malcolm and the Old crone;
- preserve Alice's Farming shop/advice subjects while adding the quest subject;
- run the full Malcolm → Alice → Malcolm → Alice → Malcolm → Alice exchange at
  states 10, 20, 30, 40, 50, and 60;
- run the Old crone's introduction at 70 and the actual mirroring at 73;
- require one free inventory slot but do **not** remove the ghostspeak amulet;
- replace a lost crone-made amulet for free using saved hair, again without
  consuming a new ghostspeak amulet; and
- give the amulet to Malcolm only through the explicit consent choice.

The local file advertises its 20→70 “bank-pass ping-pong” soft-skip. These are
native persistent states and transcript-defined re-talks, so the skip must be
removed rather than documented as acceptable compression.

The Old crone is shared with Ghosts Ahoy. Her current trigger invokes
`~ahoy_crone_hub` first and returns if it handled the click. If both quests are
active, Animal Magnetism may be starved. Use a subject menu or a single
canonical dispatcher that presents both relevant conversations. Do not create
another competing `[opnpc1,ahoy_crone]` trigger.

### Stage 3 — chicken cutscene and purchase

Required behavior:

- transform Malcolm to the crone-amulet form after the hand-in;
- make the next conversation run the Alice/Malcolm/Cow31337Killer/undead-cow/
  undead-chicken scene and its quest music;
- advance 80→90→100 only at the scene's actual checkpoints, with safe logout,
  region-change, and modal cancellation;
- sell quest chickens for exactly 10 ecto-tokens each, checking inventory and
  currency atomically;
- allow the player to decline, buy one or two, recover after partial purchase,
  and buy an optional extra undead chicken for 10 tokens; and
- require a worn ghostspeak amulet for Malcolm dialogue throughout.

The local purchase code correctly computes the missing number up to two and
checks tokens and free slots before deletion. It becomes available immediately
after the amulet hand-in, however, because states 80 and 90 and the cutscene are
collapsed. It stops all extra-chicken sales once the quest quantity is held.

Alice's local op1 also replaces her normal Talk-to with “nothing to trade”
messages outside a thin quest branch. Preserve her Farming subjects and let her
separate Trade op continue to open the real shop.

### Stage 4 — Witch, selected iron, and magnet

Required behavior:

- perform two Witch talks at 120/130 and remove exactly five unnoted iron bars
  only when selected iron can be delivered;
- if selected iron is lost, give a replacement for free as the transcript says
  “Lucky I made some replacements”;
- require a normal hammer and the canonical Rimmington mine zone;
- require the player already to face north, emitting the transcript's
  wrong-facing message without rotating them; and
- use the native animation/sound/spotanim timing and atomically replace the bar
  with the magnet.

The current Witch charges five new iron bars for a replacement. The hammer
action forcibly faces north with `facesquare`, so every orientation succeeds.
If RuneScript genuinely cannot query facing, add one general VM/content
capability with tests; do not retain a quest-specific automatic rotation.

### Stage 5 — undead trees, Turael/Aya, and blessed axe

Required behavior:

- keep the native state values so the cache exposes `nasty_tree_choppable`;
- let the first attempt use the required mithril axe and play the bounce scene;
- report to Ava before Turael becomes a dialogue subject;
- use two distinct Turael conversations at 160/170;
- support base Turael, WGS Turael variants, and Aya after While Guthix Sleeps;
- consume one inventory mithril axe and one holy symbol only on accepted,
  successful blessed-axe creation;
- provide loss/replacement dialogue at valid later states; and
- chop with the blessed axe until one undead-twig item is obtained, using the
  verified native success rule and animation for the actual held axe.

The local tree handler allows adamant, rune, dragon, and crystal axes for the
first attempt and checks normal axes only in the inventory, while it accepts a
blessed axe in inventory or worn. It always plays the mithril woodcutting
animation. The 30% failure probability is copied from 2009scape and remains
unverified against current OSRS behavior.

The shared Slayer trigger calls `~anma_turael_quest` and then
`~anma_turael_make_axe` during the same state-171 click. Thus the introduction
immediately flows into a crafting offer. Aya and the WGS Turael records route
only to ordinary Slayer dialogue even though the current Wiki explicitly makes
Aya the replacement when WGS was completed first.

### Stage 6 — research notes, pattern, and container

Required behavior:

- give/reclaim one research-notes item at state 190/200;
- mount interface 480 through the modern named parent slot, initialize every
  authored random/current/complete model layer, arm all nine server ops and the
  close operation, and re-arm after each mount;
- keep puzzle state session-local, with an intentional reset/reopen policy;
- solve only when buttons 1, 3, 4, 6, 7, and 8 are off and 2, 5, and 9 are on;
- replace research notes atomically with translated notes, show the complete
  representation, then advance to native 210;
- let Ava exchange translated notes for a pattern with full-inventory and
  duplicate/bank-safe handling;
- replace lost patterns while the player still needs a container;
- consume one pattern, one hard leather, and one polished-buttons item to make
  one container; and
- recover from a lost/banked container by offering the transcript's “spares”
  path rather than dead-ending at state 230.

The current fixed logical solution is correct. The UI is not modern: it calls
`if_openmain(anma_rgb)`, initializes all nine bits on every open, and only hides
the on/off button models. The native panel also contains six random model
layers and three complete layers that are never initialized or synchronized.
The solve immediately closes the panel, so the authored complete state is not
shown.

The recipe accepts either use direction through two handlers and checks all
three inputs. Add exact-stage gating, item-ownership checks across inventory/
bank where relevant, and transaction tests. Every quest item exposes a cache
Destroy option but no comprehensive destroy/reclaim handlers were found.

### Stage 7 — completion and immediate reward

Required behavior:

- establish and test the container-for-device one-slot transaction before
  writing 240;
- atomically grant the four XP awards once;
- choose attractor below 50 base Ranged and accumulator at 50 or higher;
- use the selected device as the completion-scroll icon and text;
- commit state only when all non-droppable rewards are delivered or record a
  durable pending-claim state;
- run `~quest_complete_rewards` exactly once; and
- make repeat clicks and `::complete` idempotent.

Current code deletes the container, writes 240, awards XP, and then calls
`inv_add`. Deleting the one-slot container frees the slot needed by the
one-slot device, so an ordinary full inventory is safe; this net-capacity
invariant should be explicit and tested rather than inferred. State is still
committed before XP, item, and completion-scroll delivery, leaving the
transaction ordered contrary to the modern lifecycle contract. The completion
scroll always uses `anma_50_reward` as its icon even when an attractor was
selected. The XP values are correct in the engine's tenths representation, and
the shared completion API correctly derives the one quest point from the
dbrow.

## 6. Post-quest devices and cross-system correctness

Completion is not the end of Animal Magnetism's contract. Its defining reward
is a permanent service and combat mechanic.

### Ava interaction and shops

After completion, Ava must support separate, arbitrated subjects for:

- normal post-quest dialogue;
- Ava's Odds and Ends, gated to quest completion;
- the cache op4 `Devices` service;
- replacement attractors/accumulators for the correct price;
- attractor → accumulator at 50 base Ranged using 75 steel arrows plus either
  the attractor or 999 coins;
- accumulator → assembler after Dragon Slayer II using Vorkath's head, 75
  mithril arrows, and either the accumulator or 4,999 coins;
- current later device/schematic integrations when their owning quests exist;
  and
- the DS2 locator-orb subject only after DS2's true prerequisite gate.

The generated odds-and-ends inventory itself contains feathers, feather packs,
arrows, and arrowheads, but its op3 handler targets the non-clickable carrier.
There is no `[opnpc4,anma_assistant]`, lost-device service, or upgrade service.
The present post-quest Talk-to merely says Ava is busy.

The medium Lumbridge & Draynor task must complete only on a successful upgraded
device purchase, never merely on quest completion, opening Ava's UI, receiving
the level-50 quest reward, or using `::complete`. The task must be idempotent
and use the shared diary counter exactly once.

### Ammunition recovery

The pinned device page defines mutually exclusive outcomes:

| Device | Automatically recovered | Dropped to ground | Broken |
| --- | ---: | ---: | ---: |
| Attractor | 60% | 20% | 20% |
| Accumulator / Ranging cape | 72% | 8% | 20% |
| Assembler | 80% | 0% | 20% |

The current routine first rolls automatic saving and then applies the generic
one-in-five break roll only to ammunition that failed to save. Its actual
outcomes are therefore:

| Device | Automatically recovered | Dropped to ground | Broken |
| --- | ---: | ---: | ---: |
| Attractor | 60% | 32% | 8% |
| Accumulator | 72% | 22.4% | 5.6% |
| Assembler | 80% | 16% | 4% |

Refactor this to one outcome roll with the authoritative three-way
distribution. The attractor currently accepts only `~ammo_is_arrow`; every
device must support recoverable arrows, bolts, darts, javelins, throwing
knives, throwing axes, and tok-tz-xil-ul, while excluding always-consumed ammo,
weapon charges, and other documented exceptions.

All device tiers must be disabled by interfering metallic torso equipment,
using a data-driven allow/deny classification shared by all firing paths.
Current code has no interference test. Passive metal-ammunition attraction at
the documented interval and the `Commune` interactions are also absent.

Delayed ground-drop ownership, map blockage, logout, death, target despawn, and
PvP/NPC target differences need regression tests after the outcome refactor.

### Downstream quest and diary gates

Dragon Slayer II must require `%anma_main >= 240` alongside its other native
prerequisites before its offer can be accepted. Ava's locator-orb conversation
must not be treated as a workaround for the missing start gate.

The generic diary implementation currently tracks tier counts but no
Animal-Magnetism-specific medium task was found. Implement the Ava upgrade task
at its transaction commit point, including 50 base Ranged and the two valid
payment variants.

## 7. Narrative, item, and lifecycle oversight matrix

| Area | Current oversight | Required invariant |
| --- | --- | --- |
| Start levels | Current-stat checks | Base levels; boosts/drains cannot alter eligibility |
| Malcolm | Debug-only spawn | One persistent carrier at the canonical coordinate |
| Ghost dialogue | No worn-amulet enforcement | Malcolm/Crone language follows equipped ghostspeak state |
| Farm relay | States 30–60 skipped | Every native talk and re-talk is reachable and persistent |
| Crone creation | Consumes ghostspeak | Ghostspeak retained; one free slot required |
| Crone replacement | Consumes another ghostspeak | Free replacement at the correct state |
| Shared Crone | Ghosts Ahoy gets first refusal | One dispatcher exposes every active subject |
| Chicken scene | Missing | Native actors/music run once with safe cleanup |
| Extra chicken | Cannot purchase | Optional purchase remains available for 10 tokens |
| Selected iron loss | Costs five new bars | Free transcript-defined replacement |
| Magnet direction | Forced north | Validate existing facing and fail without mutation |
| Tree visibility | Writes 151 | Use canonical 150/160/… values matching transforms |
| First tree attempt | Broad axe set | Match authoritative mithril-axe contract and equipment rules |
| Blessed chopping | Guessed probability | Verify exact retry rule and use correct animations |
| Turael | Two stages in one click | Separate intro and accepted exchange |
| Aya/WGS | No quest branch | Same quest service across all current transforms |
| Notes panel | Legacy main modal | Modern named mount, full layer init, close/reopen tests |
| Pattern/container | Partial replacement | Every destroy/loss/bank state remains recoverable |
| Completion | State before device grant | Atomic or durable pending claim; never lose device |
| Completion icon | Always accumulator | Icon matches actual reward tier |
| Ava shop | Bound to carrier | Bind live Ava and gate on 240 |
| Devices op4 | Unhandled | Complete purchase/recovery/upgrade service |
| Ammo outcomes | Nested probability | One authoritative mutually exclusive roll |
| Ammo classes | Attractor arrows only | All documented recoverable projectile classes |
| Metallic armour | Ignored | Data-driven interference classification |
| Diary | Generic counts only | Complete medium task on qualifying upgrade transaction |
| Dragon Slayer II | Missing prerequisite | Refuse start until Animal Magnetism is complete |
| Debug commands | Add duplicate/missing actors | Never count as route evidence; use only isolated test fixtures |

## 8. Modernization implementation plan

### Wave 1 — restore native state and world reachability

1. Replace the shifted post-150 constants with canonical 150, 160, 170, 180,
   190, 200, 210, 220, 230, and 240 meanings.
2. Add named constants for 30, 40, 50, 60, 73, 90, 110, and every other
   transcript-relevant native checkpoint.
3. Add Malcolm's native carrier to the canonical farm world spawn and remove
   redundant required-actor creation from ordinary debug walkthroughs.
4. Rebuild the journal around every native value and partial item condition.
5. Add a static invariant test comparing authored constants/writes with cache
   transform-visible states.

### Wave 2 — rebuild the farm chapter

1. Implement equipped-ghostspeak gating and the complete six-step relay.
2. Make the Old crone a shared subject dispatcher with Ghosts Ahoy.
3. Split 70/73 and make creation/replacement non-consuming and capacity-safe.
4. Implement the state-80/90 chicken cutscene with native actors, music,
   protected queues, cleanup, and reconnect behavior.
5. Finish chicken purchase, decline, partial quantity, extra purchase, and
   inventory/currency tests while preserving Alice's shop and advice.

### Wave 3 — magnet and tree chapter

1. Correct selected-iron replacement and make all Witch transactions atomic.
2. Add a general facing query if one is absent, then enforce north without
   rotating the player.
3. Drive native hammering animation/audio and exact zone boundaries.
4. Implement tree bounce/report/Turael as distinct canonical checkpoints.
5. Arbitrate base/WGS Turael and Aya through the shared Slayer Master trigger.
6. Verify the blessed-axe inputs, loss policy, chop chance, equipment search,
   animations, and twig ownership against pinned sources/client capture.

### Wave 4 — modern research-notes UI and item lifecycle

1. Decompile the interface-480 onload behavior and document each random,
   complete, button, layer, and close component.
2. Replace `if_openmain` with the repository's modern named subinterface mount,
   run required clientscript setup, and re-arm all server-handled ops.
3. Preserve the correct fixed solution while showing authored visual states and
   defining close/reopen reset behavior.
4. Add destroy/reclaim handlers and bank/duplicate policy for every quest item.
5. Make pattern, container, translated-notes, and final hand-ins atomic and
   retry-safe.

### Wave 5 — completion, devices, and downstream integrations

1. Make the net-one-slot container/device exchange explicit, order reward and
   state delivery safely, and make completion idempotent.
2. Use the selected tier for the reward object and completion icon.
3. Bind Ava's Trade and Devices ops to live Ava after completion; implement
   replacement, accumulator upgrade, assembler upgrade, and current owned
   integrations.
4. Refactor ammunition recovery to one three-way outcome roll, every supported
   ammo class, and metallic-torso interference.
5. Implement passive attraction and Commune where supported by current cache/
   engine policy.
6. Enforce Animal Magnetism in Dragon Slayer II and complete the diary task
   only on a qualifying upgraded-device transaction.

Do not add quest-specific C code for transforms, facing, UI, or devices. If a
general capability is missing, add the smallest reusable VM/service primitive,
prove it independently, and keep Animal Magnetism policy in RuneScript/config
data.

## 9. Verification contract

### Static and pack verification

- `python3 tools/questhelper_extract.py animalmagnetism --check`
- quest audit: no legacy `if_openmain`, unresolved symbolic names, duplicate
  shared-NPC triggers, debug-only required spawn, undisclosed soft-skip, or
  non-native state write;
- assert every `%anma_main` write is in the canonical state set;
- assert Malcolm and every required carrier have exactly one intended world
  spawn and correct transform ownership;
- assert journal, completion registry, dbrow end state, and cheat adapter agree;
- `make -C src mock230-scripts`; and
- `mock230_pack --check-only` against the intended cache.

### Automated route matrix

At minimum, test:

1. every prerequisite missing individually and all four base-level boundaries;
2. boosts below the threshold and drains above it;
3. accept, both refusals, re-talk, and full-inventory start behavior;
4. Malcolm availability without debug commands;
5. no/worn/inventory ghostspeak behavior at Malcolm and the Old crone;
6. each 10→20→30→40→50→60→70→73→76 transition and relog at every value;
7. crone-made amulet creation, free loss replacement, full inventory, duplicate,
   banked item, and Ghosts Ahoy simultaneous subject;
8. chicken cutscene completion, logout/region/death cancellation, replay guard,
   partial purchase, insufficient tokens, full inventory, and extra chicken;
9. Witch first/re-talk, four versus five bars, free selected-iron replacement,
   wrong zone, wrong facing, missing hammer, repeated use, and magnet hand-in;
10. tree visibility at every canonical state, bounce with each relevant axe
    location, Ava report, Turael and Aya variants, accepted/declined blessed axe,
    missing each input, loss replacement, failed/successful twig chops;
11. note receive/reclaim, open/close/reopen, every button, incorrect patterns,
    exact solution, duplicate packets, and full inventory;
12. every use-on direction for pattern/leather/buttons plus missing inputs,
    destroy/reclaim, banked item, and duplicate container;
13. completion at Ranged 49/50, full inventory, repeated click, logout between
    dialogue/reward, XP exactly once, correct device and icon, and `::complete`
    twice;
14. post-quest Trade/Devices availability, replacement prices, both accumulator
    payment paths, every failure branch, assembler gate, and simultaneous DS2
    dialogue; and
15. Dragon Slayer II refusal before 240 and acceptance after 240, plus diary
    completion only on the qualifying accumulator upgrade transaction.

### Statistical ammunition tests

Use deterministic rolls to cover boundary values rather than accepting a
flaky Monte Carlo test:

- attractor: 0–59 recover, 60–79 ground, 80–99 break;
- accumulator/cape: 0–71 recover, 72–79 ground, 80–99 break;
- assembler: 0–79 recover, no ground interval, 80–99 break;
- every supported ammo category and every excluded/always-consumed category;
- representative interfering and explicitly non-interfering torso equipment;
- blocked target square, delayed target despawn, logout, death, and ground-item
  ownership timing.

### Live-client evidence

Capture a real-client run from Ava's actual Talk-to through reward and
post-quest device service, with no state/debug command. Evidence must include:

- all farm NPC transforms and every relay state;
- the chicken cutscene, music, interruption, and reconnect behavior;
- wrong-facing and successful magnet creation;
- tree visibility/bounce, Turael and post-WGS Aya routes, and twig retries;
- research-notes mount, all visual layers, close/reopen, and completion state;
- inventory-full and lost-item recovery branches;
- attractor and accumulator reward tiers;
- live Ava Trade/Devices/replacement/upgrade interactions;
- representative ammunition recovery and metallic interference; and
- Dragon Slayer II and diary gates.

## 10. Exit criteria

Animal Magnetism can move from `audit-pending` to `verified-modern` only when:

- a new eligible player can complete the exact route through world spawns and
  menu operations without debug mutation;
- every persistent value matches the native cache state/transform contract;
- every skipped farm/cutscene phase and every material narrative re-talk is
  implemented;
- item creation, consumption, destruction, replacement, banking, inventory
  capacity, relog, and repeated action are safe at every stage;
- interface 480 is mounted and initialized through modern UI machinery;
- completion cannot commit without the correct device and cannot duplicate XP,
  item, quest point, or journal state;
- Ava's permanent shops, recovery, upgrade, Commune/passive behavior, and
  ammunition recovery match the pinned device contract;
- Dragon Slayer II and the medium diary integration are enforced at their true
  transaction/start boundaries; and
- static, pack, automated transition, deterministic probability, and live-client
  evidence is recorded.

The old queue's `audited-fixed` label is contradicted by production
reachability, native transform indexing, its own soft-skip comments, and the
missing defining reward services. It is historical evidence to inspect, not a
modernization verdict.
