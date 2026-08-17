# Below Ice Mountain modernization audit

Status: `audit-pending` — the native quest dbrow, primary and recruit state
carriers, world transforms, crew spawns, item recipe, journal arm, cheat arm,
and modern completion call exist. The legitimate route is blocked at the
excavation entrance: the script writes state 35 without creating or entering
the private dungeon, and neither the Ancient Guardian nor its four pillars has
a reachable production instance. Completion then writes 45 even though the
native dbrow and post-quest transforms require 120. The claimed access reward
teleports the player to another surface tile beside the entrance rather than
to the Ruins of Camdozaal.

Audited: 2026-08-16

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the quest root, Willow's crew and every
world transform, Charlie the Tramp, the Blue Moon cook, the Rising Sun shop,
shared item-use and food handling, emotes, music, private maps, combat and
Mining alternatives, Camdozaal travel and Ramarno, post-quest Nardah dialogue,
the journal, the cheat adapter, and the completion lifecycle. It is an
implementation specification, not completion evidence.

## 1. Authoritative references

These stable OSRS Wiki revisions define the route, dialogue, puzzle, rewards,
and permanent world changes.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Below Ice Mountain](https://oldschool.runescape.wiki/w/Below_Ice_Mountain?oldid=15240942) | 15240942, 2026-06-27 | Identity, requirements, complete route, combat alternative, rewards, and downstream quests |
| [Below Ice Mountain/Quick guide](https://oldschool.runescape.wiki/w/Below_Ice_Mountain/Quick_guide?oldid=15124188) | 15124188, 2026-02-09 | Ordered actions, items, valid drinks, recruit substates, entrance, Guardian, and pillars |
| [Transcript:Below Ice Mountain](https://oldschool.runescape.wiki/w/Transcript%3ABelow_Ice_Mountain?oldid=15263419) | 15263419, 2026-07-14 | Acceptance/refusal, re-talks, recruit dialogue, drink reactions, RPS, cutscenes, Ramarno, and post-quest dialogue |
| [Ruins of Camdozaal](https://oldschool.runescape.wiki/w/Ruins_of_Camdozaal?oldid=15241730) | 15241730, 2026-06-28 | Permanent destination, entrance/exit relationship, forge, mining, fishing, golems, vault, and bank context |
| [Ramarno](https://oldschool.runescape.wiki/w/Ramarno?oldid=15239734) | 15239734, 2026-06-25 | Finale introduction, workshop relocation, shared services, and later-quest relationship |
| [Flex](https://oldschool.runescape.wiki/w/Flex?oldid=14874299) | 14874299, 2025-03-30 | Unlock timing and actual emote requirement for Checkal |
| [Steak sandwich](https://oldschool.runescape.wiki/w/Steak_sandwich?oldid=15190178) | 15190178, 2026-04-22 | Recipe unlock, use-on ingredients, edibility, and six-Hitpoint restore |
| [Ancient Guardian](https://oldschool.runescape.wiki/w/Ancient_Guardian?oldid=15200249) | 15200249, 2026-04-28 | Level-25 boss and non-combat pillar alternative |
| [Barbarian Workout](https://oldschool.runescape.wiki/w/Barbarian_Workout?oldid=15253411) | 15253411, 2026-07-05 | Track unlocked during Atlas's training montage |
| [The Ruins of Camdozaal](https://oldschool.runescape.wiki/w/The_Ruins_of_Camdozaal?oldid=15303115) | 15303115, 2026-08-16 | Track unlocked when the ruins are revealed |

The sources identify Below Ice Mountain as quest #148, a short, novice,
free-to-play quest released 14 April 2021. It has no series and no required
skill or combat level. Starting it requires at least 16 quest points. Combat is
optional: the player may fight the level-25 Ancient Guardian, or use a pickaxe
and a boostable Mining level of 10 to mine all four structural pillars while
the Guardian attacks.

Transition aid only: the local Quest Helper checkout's
[`BelowIceMountain.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/belowicemountain/BelowIceMountain.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms primary
states 0, 5, 7, 10, 15, 20, 25, 30, 35, and 40; recruit substates;
coordinates; the real Flex action; four pillars; items; and both Guardian
solutions. It guides transition tests but does not override the Wiki,
transcript, or osrs239 cache.

`python3 tools/questhelper_extract.py belowicemountain --check` resolves every
named item, NPC, loc, coordinate, varbit, and `quest_belowicemountain`.

## 2. Native quest identity and player contract

The cache-native `quest_belowicemountain` row and pinned sources define this
contract:

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 162 |
| Type | Free-to-play quest |
| Difficulty / length | Novice / short |
| Series | None |
| Release date | 14 April 2021 |
| Start | Willow south of Ice Mountain at 3003,3435,0 |
| Start requirement | 16 quest points; no prerequisite quests or required levels |
| Required items | Cooked meat, bread, knife, and one valid drink; items can be gathered during the quest |
| Valid drinks | Beer, Asgarnian ale, Dwarven stout, or Wizard's mind bomb; Asgoldian ale is not accepted |
| Recommended | Combat level 15 with gear/food, or a pickaxe and boostable Mining 10 |
| Primary state | `%bim`, cache varbit on `bim_main`, bits 0–6, values 0–127 |
| Recruit states | `%bim_marley` bits 7–12, `%bim_checkal` bits 13–18, and `%bim_burntof` bits 19–24 on `bim_main` |
| Lifecycle side state | `%bim_claimed_reward` bit 25 and `%camdozaal_ramarno_intro` bits 26–27 on `bim_main`; `%bim_workout_counter` bits 0–15 on `bim_extra` |
| End state | **120**, not 45 |
| Quest points | 1 |
| Item reward | 2,000 coins |
| Unlocks | Ruins of Camdozaal access, Flex emote, steak sandwich recipe, `Barbarian Workout`, and `The Ruins of Camdozaal` music |
| Post-quest | Mandatory Ramarno introduction/relocation; crew visible in Nardah to members; required for Defender of Varrock and Desert Treasure II |

The dbrow correctly records the start NPC/coordinate, F2P status, one quest
point, 16-QP gate, recommended Mining 10 and combat 15, release date, and end
state 120. The quest has no prerequisite quest rows. Its nonzero prerequisite
count metadata must not be misread as actual prerequisite dbrows.

### Cache state disproves the authored completion value

The quest constants describe `%bim` as ending at bit 5 and set
`^bim_complete=45`. Both claims are false:

- `all.varbit` defines `%bim` on bits 0–6, allowing state 120;
- the quest dbrow's `endstate` is 120;
- Willow's, Marley's, Checkal's, and Burntof's Nardah carriers expose their
  live NPC only at `multinpc121`, which corresponds to `%bim=120`;
- primary-state entrance and NPC transforms reserve values after 40 for the
  authored finale and do not treat 45 as the permanent state; and
- `%bim_claimed_reward` and `%camdozaal_ramarno_intro` are native lifecycle
  fields that the current implementation never uses.

State 45 is therefore an intermediate finale value, not a defensible local
exception. A save at 45 can satisfy the current script's own comparison while
remaining incomplete to the cache, hiding the Nardah crew and disagreeing
with the quest list. The debug runner's `bimrun OK` is consequently false
evidence.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_belowicemountain/configs/belowicemountain.constant` | Primary/recruit breakpoints, rewards, and coordinates | End state and bit-width comments are wrong; state 25 and finale range 45–119 are missing; dungeon coordinate is a surface tile |
| `server/scripts/quests/quest_belowicemountain/configs/belowicemountain.varp` | Overlay declarations for `bim_main` and `bim_extra` | Carriers are native, but the comment wrongly says recruit bits live on `bim_extra`; all three actually live on `bim_main` |
| `server/scripts/quests/quest_belowicemountain/scripts/belowicemountain.rs2` | Entire start, recruit, recipe, barmaid, entrance, Guardian, completion, journal, and debug route | A 521-line monolith with explicit soft-skips for the defining montage, RPS game, instance, pillars, finale, and post-quest scene |

The root totals 570 lines across three files. Its header explicitly defers the
Atlas workout/counter, Charlie branch, full RPS matrix, dungeon instance, four
pillars, Ramarno, QP gate, and complete Rising Sun service. `::bimrun` writes
every state directly, invents and deletes the sandwich, skips all gameplay,
and completes at the wrong value. It proves only that selected symbols link.

### Mandatory shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dynamic quest journal dispatcher | Correctly calls `~belowicemountain_journal`; the journal omits most native states and every recruit substate |
| `server/scripts/quests/scripts/quest_cheat.rs2` | `::complete` adapter | Uses the wrong `^bim_complete=45` and leaves native unlock/lifecycle bits untouched |
| `server/scripts/areas/world/configs/m46_53.spawn`, `m48_53.spawn`, `m48_54.spawn`, `m46_52.spawn`, `m50_53.spawn`, and `m46_54.spawn` | Willow, Atlas, Checkal, Marley, Burntof, barmaids, cook, and entrance crew | Production carriers exist and their native transforms broadly match primary/recruit progress |
| `server/scripts/areas/varrock/scripts/tramp.rs2` | Charlie the Tramp | Existing op1 is a full Shield of Arrav/general dispatcher; Below Ice Mountain has no additive Marley referral branch |
| `server/scripts/skill_cooking/scripts/cooking_inv/scripts/cooked_meat.rs2` and the quest's bread trigger | Sandwich use-on directions | The cooked-meat branch is deliberately merged to avoid a duplicate trigger; preserve that ownership and add exact animation/capacity behavior |
| `server/scripts/general/scripts/food.rs2` and food data | Steak sandwich Eat | Already category-driven and restores the native six Hitpoints; do not replace it with a quest-only Eat handler |
| `server/scripts/areas/falador/scripts/barmaid.rs2` and Recipe for Disaster dwarf dialogue | Rising Sun shared NPCs | Quest handler correctly delegates RFD first and ordinary sales outside its window, but during the errand it hides two valid shop drinks and changes the normal menu/price path |
| `server/scripts/interface_emote/scripts/emote.rs2` | Flex display and execution | Animates Flex for everyone and has no unlock policy or nearby-Checkal completion hook, despite native `%emote_flex` existing |
| `server/scripts/interface_music/scripts/music.rs2` | Track lock/read policy | Correctly reads music bits, but no script writes `musicmulti_21` bit 2 for `Barbarian Workout` or bit 0 for `The Ruins of Camdozaal` |
| Private-map/instance services | Excavation dungeon | Modern owner-scoped map instances exist elsewhere in the tree; Below Ice Mountain never uses them |
| Combat, death, and Mining/pickaxe services | Guardian or four-pillar solution | No real combat/death branch, pickaxe validation, per-pillar state, mining action, or reset/re-entry lifecycle exists |
| `server/scripts/areas/world/configs/m46_90.spawn` and adjacent Camdozaal squares | Permanent ruins | The native Ramarno carriers and much of the scenery/world population exist; entry and exit are unbound in this quest |
| `server/scripts/quests/quest_defenderofvarrock/scripts/dov_camdozaal.rs2` | Ramarno and sacred forge | Hand-spawns a base Ramarno despite native transformed carriers and owns his op1 for DOV/DT2; BIM introduction must be merged through one shared dispatcher |
| `server/scripts/quests/quest_defenderofvarrock/scripts/dov_rovin.rs2` | Later Camdozaal travel | Comments assume BIM already teleports into the ruins, but the current coordinate is outside; the downstream leg is therefore blocked |
| `server/scripts/quests/quest_deserttreasureii/` | Downstream prerequisite and Ramarno/Whisperer route | Current DT2 implementation explicitly defers hard quest gates and shares Ramarno through DOV; modernization must preserve dispatch while DT2 separately fixes prerequisites |
| `server/scripts/areas/world/configs/m53_45.spawn` | Members' post-quest Nardah crew | All four carriers exist but remain hidden because they require `%bim=120` |

### Cache-native assets already available

The cache contains substantially more than the script uses:

- entrance, inside, cutscene, and post-quest forms of Willow and the crew;
- Atlas, Tina, Ramarno, Ancient Guardian active/inactive/cutscene forms, and
  dedicated cutscene crew variants;
- the blocked entrance, door, debris, rubble, supply props, tripwire, Willow's
  bag, exit, four structural pillars and their mined/cutscene forms;
- Camdozaal workshop, forge, bank, doors, vault, tracks, mining/fishing, and
  golem scenery;
- `bim_pushups`, `bim_pushups_stand`, `bim_human_ready`, and
  `bim_sandwich_make` sequences;
- native `%emote_flex`, music rows/bits, workout counter, claimed-reward flag,
  and Ramarno intro state; and
- primary/recruit-driven multi-NPC and multi-loc tables for every public-world
  relocation.

Modernization should connect these assets through reusable cutscene,
owner-scoped instance, emote-unlock, music-unlock, combat, mining, travel, and
NPC-subject services. It should not replace the dungeon with surface messages,
spawn a global shared boss, or invent parallel progress fields.

## 4. Native state model and current reachability

### Primary state

| State / range | Required phase | Current implementation / defect |
| ---: | --- | --- |
| 0, 5, 7 | Willow's pre-acceptance conversation, including requirement check and refusal/re-talk checkpoints | Only exact 0 offers the quest; 5 and 7 fall into a generic directions menu and cannot resume the authored start |
| 10 | Recruit the crew, beginning with Checkal guidance while recruit varbits carry independent progress | All three NPC handlers are reachable, but journal says only Checkal and major interactions are compressed |
| 15 | Finish Marley and Burntof after/alongside Checkal according to remaining recruit states | Broadly reachable; journal ignores side-state details and implies both remain even when one is complete |
| 20 | Whole crew assembled; talk to Willow at the blocked entrance | Willow asks Yes/No but immediately jumps to 35 and leaves the player outside |
| 25 | Entrance/excavation sequence checkpoint | Never written or handled separately |
| 30 | Inside/re-entry checkpoint after the crew's excavation and betrayal sequence | Constant exists, but no production path writes it and entrance never teleports |
| 35 | Ancient Guardian encounter; fight it or mine four pillars | No boss spawn/instance; name-bound Attack immediately wins if an NPC is injected; any one pillar click immediately wins if a loc is injected |
| 40 | Rummage/finale and completion cutscene | Surface entrance offers a soft-skip and calls completion; bag, doors, coins presentation, ruins reveal, and Ramarno are absent |
| 45–119 | Native finale/finalization range reserved by transforms | Incorrectly collapsed to “complete” at 45 |
| 120 | Permanent completion, open Camdozaal, Nardah crew, post-quest journal | Never written by production or cheat paths |

The primary transform topology independently supports the table: Willow's
start carrier is visible through 14, her entrance carrier through 29, her
inside carrier at 30–34, the Guardian phase begins at 35, and the Nardah crew
appears only at 120. Exact meanings for every reserved finale value must be
derived from the cache cutscene and transcript before implementation; they
must not be guessed from Quest Helper's active-step keys.

### Recruit substates

| Recruit | Native substates | Required behavior | Current defect |
| --- | --- | --- | --- |
| Checkal | 0; 5/10 need Atlas; 15/20 have Flex; 40 recruited | Full Atlas workout montage, increment/reset native workout counter, unlock Flex and `Barbarian Workout`, then perform the actual Flex near Checkal | One message replaces training; no counter/music/emote bit; a dialogue choice named “Flex” replaces the emote |
| Marley | 0; 5 need recipe; 10 recipe known; 35 fed; 40 recruited | Optional Charlie referral, cook dialogue, make sandwich, give it, then speak again to send Marley | Charlie absent; giving the sandwich jumps directly 10→40, so the authored 35 re-talk branch is unreachable |
| Burntof | 0; 5 needs drink; 10/15 drink given/RPS; 40 recruited | Distinct response for each valid drink, then best-of-three RPS whose outcome reflects Burntof's drunken play even though player choices do not matter | One generic drink line and one player choice always write 40 |

Recruit side bits intentionally remain useful after completion: Marley at 40
is the persistent steak-sandwich recipe unlock. Do not clear or replace them
when the primary state reaches 120.

### Deterministic entrance blocker

At state 20 the transformed crew is present at the outside entrance. Talking
to Willow writes state 35 after one message. No cutscene is mounted, no private
map is allocated, no player is teleported, and no Guardian is spawned. Clicking
the now-open entrance at state 35 only prints “Soft-skip: instance” and returns.
Repository-wide spawn searches find no production `bim_golem_boss`, so the
name-bound instant-win handler cannot be reached legitimately. This is the
first hard blocker on an otherwise interactable route.

The post-quest branch is also not travel. `^bim_dungeon_coord` decodes to
2996,3494,0, another surface coordinate beside the entrance. A completed
player clicking the door is moved a few tiles outside and told they entered
Camdozaal. The permanent ruins are around 2951,5779,0 on map square `m46_90`.
Both the public entrance and `bim_exit` need exact two-way authored landings.

## 5. Current versus required playable route

| Chapter | Current route | Modernized route contract |
| --- | --- | --- |
| Start | Exact state 0 gets short Yes/No; no QP check | Enforce 16 QP before acceptance, retain exact refusal, and resume 0/5/7 dialogue without starting below the threshold |
| Crew overview | Willow gives short directions | Preserve transcript subjects, re-talk hints, independent recruit progress, and correct world transforms |
| Checkal / Atlas | One Atlas message, then a fake dialogue Flex | Full player-owned workout montage using cache sequences/counter; unlock Flex and track at the authored moment; actual emote near Checkal completes recruitment |
| Marley referral | Willow mentions Charlie, but Charlie has no quest subject | Add optional Charlie branch to his existing shared dispatcher while allowing direct Marley contact |
| Steak sandwich | Cook unlocks recipe; knife use consumes ingredients; Marley jumps directly to recruited | Preserve both merged use-on paths, play sandwich animation, allow creation in a full inventory because two inputs are consumed, write 35 on feeding, and require the state-35 follow-up |
| Burntof drink | Accepts the correct four item IDs with one generic reaction | Reject invalid ale, retain one valid drink only after confirmed hand-in, and reproduce drink-specific transcript reactions |
| RPS | One choice always wins | Present all rounds of best-of-three RPS with interruption/re-talk safety and Burntof's authored drunken outcomes |
| Rising Sun | Quest window exposes only Asgarnian ale and a separate price | Keep RFD priority and the complete shared ale shop; make quest hints/additional dialogue stage-aware without suppressing valid purchases |
| Entrance excavation | One message writes 35 | Player-owned cutscene with dynamite, rubble clearing, trap disarm, Willow's betrayal, Guardian awakening, crew escape, and state 20/25/30 checkpoints |
| Guardian | Injected NPC click immediately wins | Real level-25 combat through combat/death callbacks, instance re-entry, death/reset, and exactly-once transition on actual defeat |
| Pillars | One injected pillar click at Mining 10 immediately wins | Require boostable current Mining 10 and a valid pickaxe; mine four distinct pillars with animation/timing while Guardian attacks; persist/reset instance-local progress correctly |
| Finale | Surface door soft-skips directly to reward | Rummage Willow's bag, open ruins, show complete finale, award exact rewards, reveal track, introduce Ramarno, and commit native state 120 safely |
| Camdozaal access | Teleports to surface entrance | Exact entrance/exit maplinks to permanent ruins, gated at 120, with music and downstream quest integration |
| Post-quest | Generic Willow line; Nardah carriers hidden; Ramarno absent | Mandatory post-scroll Ramarno conversation and relocation, shared later-quest dialogue, plus all four Nardah crew subjects at state 120 |

## 6. Oversight and lifecycle matrix

| Priority | Oversight | Evidence / consequence | Required correction |
| --- | --- | --- | --- |
| Blocker | No dungeon instance or entry | State 20 jumps to 35 on the surface; re-entry is message-only | Allocate an owner-scoped instance from the authored template, teleport safely, and support re-entry/reconnect |
| Blocker | Guardian and pillars are unreachable | No production boss spawn and no travel to the baked locs | Spawn instance-owned actors/loc state and bind real success callbacks |
| Critical | Wrong final state 45 | Dbrow and post-quest transforms require 120 | Model the finale range and commit 120 only after the authoritative completion transaction |
| Critical | Camdozaal reward does not transport | “Dungeon” coordinate is beside the surface entrance | Bind exact public entrance and ruins exit landings; test both directions and blocked movement |
| Critical | QP requirement is not enforced | Header admits the 16-QP gate is soft | Refuse start below 16 without mutating any state; permit at exactly 16 |
| Critical | Combat is an interaction soft-skip | Attack op writes 40 without damage/death | Use the standard combat controller and boss-death callback; handle player death/logout |
| Critical | Four-pillar solution is one click | Either normal or mined loc writes 40; no pickaxe check | Track four unique instance-local pillars and require the complete Mining action |
| Critical | Flex is globally usable | Cache defines `%emote_flex`, but UI server policy ignores it | Add reusable emote-unlock policy; set the native bit during Atlas training |
| High | Checkal requires a fake dialogue action | Quest Helper and Wiki require the actual Flex emote | Let the emote service notify nearby stage-valid Checkal and advance exactly once |
| High | Both quest music tracks remain locked | Music player reads native bits; quest writes neither | Unlock `musicmulti_21` bit 2 during training and bit 0 at the ruins reveal through a shared writer |
| High | Marley state 35 is unreachable | Sandwich hand-in writes 40 directly | Write 35 after consumption and 40 only after the follow-up conversation |
| High | Full inventory wrongly blocks sandwich creation | Two ingredients are deleted before one output is added | Capacity-check the net slot delta, then delete/add atomically with animation |
| High | Charlie route is missing | Transcript/Wiki include an optional referral; shared Charlie op1 already exists | Merge a BIM subject without replacing Shield of Arrav, charity, or alley dialogue |
| High | Burntof's challenge is absent | One choice replaces best-of-three and all drink reactions | Implement a resumable round state/presentation and exact accepted-drink branches |
| High | Entrance/finale cutscenes are absent | Cache supplies dedicated actors, props, sequences, and transform ranges | Build protected player-owned cutscenes with skip/logout cleanup and idempotent state commits |
| High | Ramarno introduction is absent | Native two-bit intro field and two carriers are unused | Implement 0→1→2 introduction/relocation and merge DOV/DT2 subjects |
| High | Reward lifecycle ignores claimed bit | `%bim_claimed_reward` is unused; state is written before coin/completion call | Reconcile intended semantics and make state, 2,000 coins, QP/count, scroll, unlocks, and reconnect exactly once |
| High | Cheat completion produces a corrupt save | Writes 45 and no Flex/music/Ramarno lifecycle state | Set canonical permanent state/unlocks without rewards duplication; second invocation must be a no-op |
| High | State 5/7 start variants cannot resume | Willow handler offers only at exact 0 | Implement every native pre-acceptance checkpoint |
| High | Nardah post-quest content is invisible | Carriers select real crew only at primary value 120 | Finish at 120 and implement stage/member-aware post-quest subjects |
| High | Downstream code assumes nonexistent access | Defender of Varrock comments rely on BIM entry; DT2 defers all hard gates | Establish shared Camdozaal travel/Ramarno ownership and regression-test later quests |
| Medium | Rising Sun quest branch suppresses shared shop | Only one ale appears during Burntof's errand | Use one stage-aware dispatcher preserving RFD and the full current shop matrix/prices |
| Medium | Journal collapses native and recruit state | 5/7, 20/25/30, side bits, items, and real final state are absent | Derive journal text from primary plus recruit/item/instance context |
| Medium | No sandwich make animation | Cache has `bim_sandwich_make` but script mutates instantly | Play the authored animation and commit the exchange at the correct tick |
| Medium | Debug runner is false evidence | It directly mutates every state and reports 45 as success | Limit helpers to reset/position or drive production triggers; assert final state 120 |
| Low | Post-completion crew dialogue is generic/absent | Surface Willow line does not cover Nardah transcript | Implement pinned individual post-quest conversations without altering progress |

### Item, reward, and unlock lifecycle

| Item / unlock | Required lifecycle | Current result / modernization rule |
| --- | --- | --- |
| Cooked meat | One sandwich ingredient; can be picked up in the Barbarian Long Hall | Existing generic item/food remains shared; sandwich use must consume exactly one |
| Bread | One sandwich ingredient | Preserve its merged Recipe for Disaster soggy-bread branch and default behavior |
| Knife | Tool used on bread or cooked meat; retained | Validate the actual used/target direction and never consume it |
| Steak sandwich | Recipe available after the cook teaches it and permanently thereafter; heals 6 if eaten; Marley consumes one | Native food row already heals 6; recipe must be remakeable, animation-backed, and net-capacity safe |
| Valid drink | Burntof consumes exactly one of beer, Asgarnian ale, Dwarven stout, or Wizard's mind bomb | Preserve player choice and item-specific response; never consume Asgoldian ale or an item on refusal/interruption |
| Pickaxe | Any supported pickaxe for the non-combat route; retained | Use the shared pickaxe capability/equipment policy, not an arbitrary item list in quest dialogue |
| 2,000 coins | Granted once at completion | Stack/capacity and reconnect-safe reward transaction using `%bim_claimed_reward` as supported by native semantics |
| Flex | Unlock during Atlas's montage, before quest completion | Set `%emote_flex`; server refuses locked use but permits it immediately for the Checkal step |
| Steak sandwich recipe | Unlock represented by Marley's persistent side state | Keep `%bim_marley>=10`; do not introduce a duplicate reward bit |
| `Barbarian Workout` | Unlock during Atlas's montage | Set native music row bit exactly once and update the music interface state |
| `The Ruins of Camdozaal` | Unlock at the ruins reveal/end | Set native music row bit exactly once when the player legitimately reaches the reveal |
| Ruins access | Public entrance/exit available after state 120 | Gate exact maplinks by canonical completion, not by state 45 or a surface teleport |

## 7. Modernization implementation plan

### Wave 1 — correct schema, start, and shared unlock services

1. Correct the `%bim` width/end-state constants, add named 25 and finale
   checkpoints only after cache/cutscene verification, and document all native
   side varbits on their actual carriers.
2. Add static assertions that the dbrow, journal, cheat adapter, transforms,
   completion proc, and tests all agree on end state 120.
3. Implement Willow's complete state 0/5/7 offer/refusal/re-talk tree and check
   `%qp>=16` before the first progress mutation.
4. Add a reusable emote-unlock predicate/writer keyed to native emote varbits;
   keep cache-drawn locked cells visible but reject execution with the normal
   message.
5. Let successful stage-valid emote execution publish a reusable nearby-NPC
   hook so actual Flex, not a dialogue choice, completes Checkal.
6. Add a reusable music-row unlock proc that reads the row's variable/bit
   metadata, writes the correct carrier, notifies the player, and is idempotent.
7. Expand the journal for 0/5/7, all recruit substates, item possession, and
   the exact 20/25/30 route.

### Wave 2 — rebuild all three recruit stories

1. Implement Atlas's protected workout montage with native actors/sequences,
   `%bim_workout_counter`, skip/interruption handling, and deterministic tests.
2. At the authored training beat, atomically set `%emote_flex`, unlock
   `music_bim_training`, and advance Checkal to the have-Flex substate.
3. Complete Checkal only when Flex is actually performed in the valid nearby
   context; ignore unrelated Flex use and duplicate packets.
4. Merge Charlie's optional Marley referral into the existing `tramppg` op1
   subject tree, retaining every Shield of Arrav/general branch and allowing
   direct travel to Marley.
5. Reproduce cook recipe/re-talk dialogue, preserve both safe merged item-use
   directions, play `bim_sandwich_make`, and perform a net-capacity-safe atomic
   ingredient exchange.
6. On Marley hand-in, consume exactly one sandwich and write 35; only his next
   conversation writes 40 and moves his carrier to the entrance.
7. Implement all valid/invalid drink responses and consume one chosen drink
   only after the dialogue commits.
8. Implement best-of-three RPS as a resumable player-owned sequence with all
   choices and Burntof's drunken outcome presentation; advance to 40 once.
9. Refactor the Rising Sun op1 into one shared dispatcher: RFD priority,
   Burntof quest subject, and the complete ordinary ale shop remain available
   without trigger replacement.

### Wave 3 — entrance cutscene and private dungeon

1. Identify and verify the exact cache template square, entrance/re-entry
   offsets, exit, four pillar coordinates, boss coordinate, and collision.
2. Allocate one player-owned map instance through the existing modern instance
   service; define logout, death, abandonment, reconnect, and release policy.
3. Build the complete entrance cutscene with dedicated Willow/crew actors,
   dynamite/debris, rubble, tripwire/trap, betrayal, Guardian awakening, and
   crew escape.
4. Commit states 20→25→30 only at interruption-safe narrative boundaries;
   rebuild visible loc/NPC state correctly after reconnect.
5. Spawn exactly one instance-owned level-25 Ancient Guardian and connect its
   Attack op to normal combat/retaliation/death rather than direct state writes.
6. Spawn/transform four distinct structural pillars. Require boostable current
   Mining 10 and a supported pickaxe, animate and delay each action, and keep
   the Guardian attacking during the attempt.
7. Complete the encounter once on either actual Guardian death or all four
   pillars mined; cancel the losing path safely and never let one pillar or one
   click finish it.
8. Support surface and in-instance re-entry at 30/35, including death, logout,
   full worlds, duplicate clicks, and stale instance handles.

### Wave 4 — finale, rewards, Camdozaal, and downstream integration

1. Implement Willow's bag investigation, doors, Guardian aftermath, ruins
   reveal, and the complete protected finale across the cache-reserved state
   range.
2. Reconcile `%bim_claimed_reward` with an idempotent completion transaction:
   exact 2,000 coins, one quest point/completed-count update, reward scroll,
   permanent recipe/unlocks, and canonical state 120 across interruption and
   reconnect.
3. Unlock `music_camdozaal_ruins` at its authored reveal and verify both music
   tracks become manually playable while unrelated tracks remain locked.
4. Bind the surface `bim_entrance` and ruins `bim_exit` as exact two-way,
   state-120-gated travel with movement/modal checks and no coordinate loop.
5. Implement the automatic post-scroll Ramarno introduction with native
   `%camdozaal_ramarno_intro` 0→1→2 transforms, then leave him at his
   workshop in the proper ordinary state.
6. Replace Defender of Varrock's unconditional base-NPC hand-spawn with the
   native carrier/shared Ramarno dispatcher; preserve BIM introduction, DOV
   forge, DT2 Whisperer, ordinary services, and future clue subjects in a
   deterministic priority order.
7. Implement each Nardah crew member's state-120 post-quest dialogue and verify
   members/F2P world visibility policy without weakening the F2P quest itself.
8. Correct the cheat adapter to prepare a coherent post-quest save, including
   canonical state and native unlocks, while leaving Ramarno intro in the
   intended first-visit state and awarding no production reward twice.
9. Regression-test Defender of Varrock's forge trip and DT2's shared Ramarno
   dispatch; record their separate prerequisite-gate defects in their own
   audits rather than hiding them inside BIM.

Do not add quest-specific C for QP checks, dialogue, item recipes, emote/music
bits, cutscenes, instances, combat, Mining, maplinks, or NPC dispatch. If a
general capability is absent, add the smallest reusable engine/service
primitive, prove it independently, and keep Below Ice Mountain policy in
RuneScript/config data.

## 8. Verification contract

### Static and pack verification

- `python3 tools/questhelper_extract.py belowicemountain --check`;
- assert `%bim` is bits 0–6, every permanent completion comparison uses 120,
  and no production/cheat path calls 45 complete;
- assert all active primary states 0/5/7/10/15/20/25/30/35/40 and the finale
  range have an explicit handler/journal policy;
- assert recruit writes use only the native substate sets and Marley has a
  reachable 35→40 transition;
- assert the 16-QP test occurs before every start mutation;
- assert `%emote_flex`, `music_bim_training`, `music_camdozaal_ruins`,
  `%bim_claimed_reward`, `%camdozaal_ramarno_intro`, and
  `%bim_workout_counter` each have a production owner and tests;
- assert one owner-scoped instance route, one boss-death completion callback,
  four unique pillar identities, a pickaxe predicate, and exact two-way public
  maplinks exist;
- assert Charlie, bread/cooked-meat, Rising Sun/RFD, Ramarno/DOV/DT2, food,
  emote, and music shared triggers have one owner/dispatcher each;
- assert no active `Soft-skip`, deferred quest-critical marker, injected-click
  instant win, wrong surface coordinate, unbound advertised operation, or
  direct debug completion remains undisclosed;
- assert dbrow, state 120, quest points, 2,000 coins, reward text/icons,
  journal, cheat arm, transforms, and completion call agree;
- `make -C src mock230-scripts`; and
- `mock230_pack --check-only` against the intended cache.

### Automated route matrix

At minimum, test:

1. start at 0/5/7 with 0, 15, exactly 16, and more than 16 QP; refusal,
   interruption, repeated clicks, and no mutation below the gate;
2. every Willow directions/re-talk branch with all combinations of three
   recruit substates and correct transformed carriers;
3. Atlas before assignment, accept/refuse training, every montage boundary,
   skip/logout/reconnect, workout counter reset, Flex/music unlock timing, and
   duplicate completion packets;
4. Flex locked before training, usable after training, performed far away,
   beside wrong NPC, beside Checkal in wrong/right state, perform/loop op, and
   exactly-once Checkal recruitment;
5. Charlie with every Shield of Arrav gang state, money/alley branches,
   BIM inactive/active/already-referred states, and direct Marley bypass;
6. cook and recipe before/after teaching; knife on bread and cooked meat;
   reversed invalid directions; missing ingredient/tool; one/full inventory;
   existing sandwich; animation interruption; repeated packets; and permanent
   recipe after completion;
7. eating the sandwich at low/full health, exact six-Hitpoint heal, making a
   replacement, Marley hand-in with 10→35, re-talk 35→40, full inventory,
   and interruption without item loss;
8. each of four valid drinks, Asgoldian ale and unrelated invalid items,
   inventory ordering, hand-in refusal/interruption, and item-specific lines;
9. all RPS choices/round combinations, best-of-three display, close/re-talk,
   logout/reconnect, and a single 40 commit;
10. every Rising Sun barmaid with ordinary shop, Burntof subject, RFD dwarf
    priority, all sale items/prices, zero/exact coins, full inventory, and no
    duplicate trigger shadowing;
11. entrance cutscene from states 20/25/30 with accept/refuse, every protected
    boundary, skip/logout, simultaneous players, actor/loc cleanup, and correct
    transforms;
12. instance allocation failure, two concurrent players, re-entry, logout,
    reconnect, death, stale handle, surface exit, and owner isolation;
13. Guardian aggro/combat/death with melee/ranged/magic, player death, kill
    credit, duplicate death queues, and no click-to-win path;
14. Mining at 9/10 and boosted/drained boundaries, every supported inventory/
    equipped pickaxe, no pickaxe, each pillar order, repeat mined pillar,
    interruption, Guardian attacks, death/re-entry, and exactly four required;
15. race between Guardian death and fourth pillar, proving exactly one state-40
    transition and consistent encounter cleanup;
16. Willow's bag, finale actors/doors/music, early close/logout/reconnect at
    every reserved state, and no state-45 pseudo-completion;
17. completion with empty/full inventory and existing/no coin stack,
    interruption before/after each commit, duplicate queues, relog, exact 2,000
    coins, one QP/count increment, one scroll, claimed bit, and final state 120;
18. music lock before each authored beat, unlock at the exact beat, manual
    playback, playlist behavior, relog, duplicate unlock, and unrelated bits;
19. surface entrance and ruins exit at states 0/40/45/119/120, exact landings,
    blocked movement/modal, repeated clicks, logout, and no surface loop;
20. Ramarno intro 0→1→2, automatic post-scroll start, interruption and
    revisit, carrier relocation, ordinary/DOV/DT2 priority, and no duplicate
    hand-spawn;
21. all four Nardah carriers/dialogues at 45 versus 120, member/F2P access
    policy, repeated conversations, and no state mutation; and
22. `::complete quest_belowicemountain` twice, coherent state/unlocks/travel,
    no reward duplication, plus Defender of Varrock forge and DT2 Ramarno
    dispatch regressions.

### Live-client evidence

Capture a real-client run from a clean sub-16-QP check through post-quest
Camdozaal without state/debug commands. Evidence must include:

- QP refusal, acceptance at 16, 0/5/7 re-talk behavior, journal changes, and
  every crew world transform;
- complete Atlas montage, track notice, locked-to-unlocked Flex behavior, and
  actual Flex beside Checkal;
- Charlie's optional referral coexisting with Shield of Arrav, the cook,
  sandwich animation/full-inventory case, six-HP Eat behavior, and Marley's
  separate feed/re-talk states;
- all valid/invalid Burntof drinks, complete best-of-three RPS, shared Rising
  Sun shop, and RFD arbitration;
- full entrance cutscene, two simultaneous private instances, Guardian combat,
  all four pillars under attack, death/re-entry, and encounter cleanup;
- bag/finale, both music unlocks, exact reward transaction, reward scroll,
  state-120 world transforms, and no duplication after reconnect;
- exact surface-to-Camdozaal and exit round-trip, automatic Ramarno introduction
  and workshop relocation, Nardah crew, and a later revisit; and
- Defender of Varrock's forge route plus DT2's shared Ramarno subject continuing
  to dispatch correctly.

Only after static checks, automated matrices, pack validation, and live-client
evidence pass may this record change from `audit-pending` to
`verified-modern`.
