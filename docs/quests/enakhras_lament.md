# Enakhra's Lament modernization audit

Status: `audit-pending` — the native quest row, all three permanent quest
varps, most cache-authored transforms, quarry mining, a quest journal, reward
XP/QP calls, and broad route scaffolding exist. The quest is nevertheless
impossible through ordinary play. Completing the statue does not perform the
canonical fall into the temple; the replacement boulder neither exposes its
closed-state action nor transports the player; exact ladder handlers suppress
valid maplinks without moving; limb and sigil doors never change or permit
passage; the magic barrier never moves the player; and the final Boneguard is
not spawned. The implementation also uses the wrong primary states, replaces
real spell casts with clicks, collapses the statue and puzzle transactions,
omits every cutscene, has several permanent item-loss traps, completes without
a deliverable Camulet, and leaves the Camulet, camel mask, diary task, and
post-quest lifecycle unwritten.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the direct quest root, revision-239
state/cache assets and placements, shared Mining/Crafting/combat-magic/maplink
owners, quest-list journal and reward helpers, loss/recovery, the Camulet, the
Desert Diary, and the Desert Treasure II prerequisite. It is an implementation
specification, not completion evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable route, dialogue, item, reward, and downstream contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Enakhra's Lament](https://oldschool.runescape.wiki/w/Enakhra%27s_Lament?oldid=15292316) | 15292316, 2026-08-10 | Quest identity, requirements, full route, puzzles, finale, rewards, and downstream requirements |
| [Enakhra's Lament/Quick guide](https://oldschool.runescape.wiki/w/Enakhra%27s_Lament/Quick_guide?oldid=15276622) | 15276622, 2026-07-27 | Ordered interactions, inventory, temple traversal, spell targets, and wall repair |
| [Transcript:Enakhra's Lament](https://oldschool.runescape.wiki/w/Transcript%3AEnakhra%27s_Lament?oldid=15284168) | 15284168, 2026-07-31 | Acceptance/refusal, individual limb choices and replacement, sigil/cutscene dialogue, Boneguard attacks, full-inventory retry, and post-quest Lazim services |
| [Camulet](https://oldschool.runescape.wiki/w/Camulet?oldid=15276620) | 15276620, 2026-07-27 | Four shared charges, two teleport destinations, dung recharge, replacement, camel speech, and permanent one-million-coin upgrade |
| [Fallen statue](https://oldschool.runescape.wiki/w/Fallen_statue?oldid=15201697) | 15201697, 2026-04-29 | Repeated chisel/pickaxe limb extraction and item lifecycle |
| [Brazier](https://oldschool.runescape.wiki/w/Brazier_%28Enakhra%27s_Lament%29?oldid=14849704) | 14849704, 2025-02-09 | Six coordinate-specific fuels and the separate tinderbox-lighting action |
| [Rubble](https://oldschool.runescape.wiki/w/Rubble_%28Enakhra%27s_Lament%29?oldid=15088110) | 15088110, 2025-12-16 | Three-depletion 5 kg sandstone source beside the final wall |
| [Boneguard](https://oldschool.runescape.wiki/w/Boneguard?oldid=15266956) and [Crumble Undead](https://oldschool.runescape.wiki/w/Crumble_Undead?oldid=15298965) | 15266956 / 15298965, 2026-07-18 / 2026-08-14 | First guard's Talk/attack/cast-to-hit behavior and real combat-spell contract |
| [Secret entrance](https://oldschool.runescape.wiki/w/Secret_entrance?oldid=15276630) and [Magic barrier](https://oldschool.runescape.wiki/w/Magic_barrier?oldid=14952090) | 15276630 / 14952090, 2026-07-27 / 2025-07-27 | Re-entry unlocks and pass-through traversal |
| [Sandstone (32kg)](https://oldschool.runescape.wiki/w/Sandstone_%2832kg%29?oldid=15184461), [Sandstone (20kg)](https://oldschool.runescape.wiki/w/Sandstone_%2820kg%29?oldid=15184460), and [Stone head](https://oldschool.runescape.wiki/w/Stone_head?oldid=15275733) | 15184461 / 15184460 / 15275733, 2026-04-22 / 2026-04-22 / 2026-07-26 | Exact-weight delivery, intermediate carving, placement, head choice, and wrong-head recovery |

The sources identify Enakhra's Lament as quest number 97, released 23 January
2006. It is an experienced, medium, members' quest with no quest prerequisite.
It requires 50 Crafting (not boostable), 45 Firemaking (boostable, and needed
only to identify brazier fuel), 43 Prayer (not boostable), 39 Magic
(boostable), and the standard spellbook. These are not all hard start gates;
they authorize later interactions. The reward is two quest points, 7,000 XP in
Crafting, Mining, Firemaking, and Magic, a Camulet, and the ability to make a
camel mask. Completion is required for the Medium Desert Diary and Desert
Treasure II - The Fallen Empire.

Transition aid only: the local Quest Helper checkout's
[`EnakhrasLament.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/enakhraslament/EnakhrasLament.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms primary
states 0/10/20/30/40/50/60, actor/object coordinates, all native substate
predicates, requirements, and all four 7,000-XP rewards. It guides state and
placement tests but does not override the Wiki, transcript, or cache.

`python3 tools/questhelper_extract.py enakhraslament --check` resolves the
quest row and every relevant NPC, loc, and varbit. Its unresolved
`enakh_sandstone_crafted_base_legs` and `enakh_sandstone_huge_base_legs` are
extractor spelling aliases for cache tokens containing a literal `+`; four
modern topped-potato names are absent from revision 239. Those are compatibility
facts, not evidence that the route is playable.

## 2. Native quest identity and contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest row | `quest_enakhraslament`; dbrow pack index 40, quest metadata ID 103 |
| Type / difficulty / length | Members / experienced / medium |
| Release / series | 23 January 2006 / Mahjarrat #4 |
| Start | Speak to Lazim at 3191,2926,0 and explicitly accept or refuse |
| Quest prerequisite | None |
| Skills | 50 Crafting, 45 Firemaking, 43 Prayer, 39 Magic; later-action checks rather than hard start checks |
| Mining supplies | Exactly 52 kg mixed sandstone plus two 5 kg granite; 35/45 boostable Mining only when self-mining |
| Primary state | `%enakh_quest`, bits 0–6 of permanent varp `enakh_quest_expositbits` |
| Other state | Cache-native fields on permanent `enakh_quest_expositbits`, `enakh_multivarbits`, and `enakh_varbits`; native `%enakh_rubble_limit` for the three-use rubble visual |
| Canonical primary values | 0 offer; 10 statue; 20 bottom floor/pedestal; 30 four puzzles; 40 first Boneguard; 50 second Boneguard; 60 wall/finale; 70 complete |
| End state / quest points | 70 / 2 |
| XP reward | 7,000 Crafting, Mining, Firemaking, and Magic (`70000` raw tenths each) |
| Item reward | Camulet, initially four charges |
| Unlocks | Camel/Ugthanki speech, temple teleport and recharge/upgrade/replacement, camel-mask moulding |
| Downstream | Camulet teleport for Medium Desert Diary; quest prerequisite for Desert Treasure II |

The dbrow's `requirement_check_skills_on_start=0` agrees with the current Wiki:
acceptance should not reject the player merely for lacking a later-action
level. Its single `requirements_boostable=1` is too coarse to encode the mixed
policy, so interaction code must enforce each actual contract. The direct
constant's claim that Quest Helper omits Mining XP is false; its reward list
contains all four skills.

No new permanent quest varp is justified. The cache already exposes primary
progress, dialogue one-shots, statue/head states, individual limbs, four limb
locks, four sigil doors, four puzzle results, six braziers, Lazim location,
pedestal/head state, two Boneguard encounter flags, hits dodged, final wall,
Enakhra/Akthanakos forms, Camulet charge, and all secret-entrance boulders.
Modernization should define named predicates over those fields and remove the
invented primary values 45 and 55.

## 3. Implementation surface

The direct root contains 931 lines across eight files.

| Quest-owned path | Present responsibility | Audit result |
| --- | --- | --- |
| `configs/enakhraslament.constant` | State, quantities, XP, and a long port rationale | Native metadata is useful; states/quantities are wrong and several comments are disproved by the Wiki, helper, cache, or implementation |
| `configs/enakhraslament.varp` | Declares the three cache-native carrier varps | Correct ownership; no parallel quest state is needed |
| `configs/enakhraslament.npc` | Adds wrapper options/animations | Lazim wrappers become clickable, but first Boneguard is moved from native op2 to op1 and the absent final Boneguard is not created |
| `configs/enakhraslament.loc` | Adds boulder and door options/`next_loc_stage` | Comments promise `loc_change`, but no script calls it; closed boulder leaf still lacks an option and doors remain blocking |
| `scripts/enakhraslament_quarry.rs2` | Lazim, statue, head crafting, east entrance | Auto-accepts, collapses all weight/item steps, accepts the wrong head, skips native 20, and never performs the fall or boulder transport |
| `scripts/enakhraslament_temple.rs2` | Limbs, doors, sigils, pedestal, puzzles, guards, wall, completion | Broad scaffold but wrong state machine, multiple hard blockers, fake spell casts, absent actors/cutscenes/recovery, and unsafe completion |
| `scripts/enakhraslament_journal.rs2` | Dynamic primary milestone journal | Correctly dispatched, but it describes the simplified/wrong states and cannot diagnose independent or lost-item substates |
| `scripts/enakhraslament_debug.rs2` | Reset/arm/synthetic run commands | Writes substates and calls completion directly; useful for isolated setup only, never route proof |

Mandatory shared/cache surfaces:

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `skill_mining/scripts/mining.rs2` and `configs/mine.dbrow` | Quarry sandstone/granite | Real 35/45 Mining gates, weighted tiers, depletion, and XP exist. The quest must accept all sandstone weights, not only 5 kg |
| `skill_crafting/scripts/gem/uncut_gem.rs2` | Shared chisel dispatcher | Correct place to dispatch 5 kg granite, but the quest proc must enforce phase, chosen head, 50 Crafting, timing, and recovery |
| `ladders_stairs/configs/maplink.dbrow` | Temple ladders | Exact rows exist for central and pillar ladders. Quest-specific exact triggers currently preempt them without calling `~maplink_try` |
| `configs/all.loc` and map placements | Doors, boulders, statue, pedestal, puzzles, wall, rubble | Required variants/transforms exist, including depleted rubble and barrier. Standalone doors require an authored unlock-and-transient-pass policy |
| `areas/world/configs/m49_45.spawn` | Quarry actors | Lazim is correctly spawned at 3191,2926,0 |
| `areas/world/configs/m48_145.spawn` | Temple actors | Puzzle actors and first Boneguard wrapper exist. `enakh_akthanakos_boneguard` does not; only a default-hidden freed-Akthanakos wrapper occupies 3105,9297,1 |
| `skill_combat/scripts/player/spells/crumble_undead.rs2` | Real cast path | Consumes runes, rolls accuracy, and plays combat spell effects, but rejects the quest guard as a normal unattackable NPC; add a quest-special success adapter instead of bypassing the cast |
| `quests/scripts/questpoints.rs2` | Completion UI/QP/count | Correct shared reward presentation; cannot make the preceding item/state transaction atomic |
| `interface_questjournal/scripts/quest_journal.rs2` | Quest-list dispatch | Correctly calls `~enakhraslament_journal` for the native row |
| `quests/scripts/quest_cheat.rs2` | `::complete` adapter | Idempotently writes 70 only. It does not prove XP, item, forms, charge, or downstream integration |
| `interface_diaries/` | Desert Diary | Area/tier counters exist, but repository search finds no Camulet-teleport task hook |
| Desert Treasure II | Downstream quest | Its native prerequisite contract names dbrow pack index 40, but no local gameplay prerequisite consumer currently verifies Enakhra's Lament end state |

## 4. Primary state and current reachability

| Canonical state | Canonical phase | Current state / defect |
| ---: | --- | --- |
| 0 | Lazim offer and explicit Yes/No | Dialogue hardcodes player “Yes”, gives no warning/choice, and immediately writes 10 |
| 10 | Deliver 32 kg, carve/place base; deliver 20 kg, carve/place body; detail statue; choose/carve/place head | Direct dialogue consumes 7×5 kg (35 kg) and 4×5 kg, skips both raw-block and placement items, performs no Crafting check, and accepts whichever head item is found while displaying the saved choice |
| 20 | Fall into temple; talk to Lazim; retrieve head/limbs; traverse outer rooms; enter centre; mould/place camel head | Local code never uses 20. Finishing the head writes 30 and redirects the player to a boulder that cannot transport them |
| 30 | Complete four independent elemental puzzles | Local 30 means only “statue complete”. Cache consumers therefore see a puzzle-state value before the player has entered the temple |
| 40 | Pass barrier, climb, Talk/attack, and land a real Crumble Undead hit | Local 40 means “ground floor done”. Exact ladder handling does not move the player |
| 50 | Descend, survive the second Boneguard's attacks with Protect from Melee, accept wall task | Local 50 means “puzzle floor done”. Barrier/ladder handling does not move, and the first guard is completed by clicking it |
| 60 | Take three nearby rocks, alternate sandstone/chisel on wall, finish dialogue/cutscene/reward | Local 55 means first guard done and local 60 means wall done. The final Boneguard actor and rubble handler are absent |
| 70 | Complete, with Camulet delivered and post-quest services available | XP/QP values are right, but state 70 is written before a Camulet can be delivered; recovery, charges, teleports, camel speech, mask, and upgrade are absent |

The mismatch is systemic, not cosmetic. `%enakh_quest` drives cache NPC/loc
transforms: state 50 turns the first Boneguard into its pile, and other scenery
expects the native breakpoints. Invented states 45/55 and shifted values can
produce actors and collision inconsistent with server dialogue.

### Independent hard blockers

1. Canonically, placing the chosen head collapses the statue and puts the
   player/Lazim inside the temple. Current code only writes state 30.
2. The east boulder is a re-entry object, not a substitute for the fall. Its
   default `enakh_secret_boulder_shut` leaf has no option, `%enakh_boulder_e_multivar`
   is never opened, and the exact handler has no teleport.
3. If manually teleported to the entrance room, the ordinary ladder down works
   through its maplink. Later exact handlers on `enakh_temple_ladderup` do not:
   they suppress valid coordinate-specific maplinks and only print text/write
   state.
4. All eight limb/sigil item-use handlers write bits but never swing/change a
   door or walk the player through. The configured `next_loc_stage` parameters
   are dead, so collision keeps every door closed.
5. `enakh_magic_wall` has explicit Pass-through geometry but no runtime
   maplink. Its handler prints that the player stepped through while leaving
   them on the same side.
6. `enakh_temple_pillar_ladder_top` has valid maplinks, but its exact handler
   again prints without moving.
7. No `enakh_akthanakos_boneguard` is spawned or created. The placed
   `enakh_akthanakos_multinpc` resolves to `-1` until the freed-form bit is set,
   so the final encounter cannot be clicked.
8. The nearby rubble has native 3/2/1/empty leaves but no Take-rock handler.
   Even mutation past the absent NPC requires imported quarry sandstone rather
   than the canonical three local rocks.

An end-state write, direct teleport, or debug proc can hop over these failures;
none is an end-to-end quest test.

## 5. Detailed lifecycle audit

### Offer, statue, and first temple entry

The real start offers “Of course!” and a refusal path. Because skills are
later-action requirements, the offer should explain the danger/Prayer need but
not invent a hard all-skills start gate. Current dialogue is only four lines,
is irreversible on first Talk, and incorrectly describes the statue as already
being Enakhra.

Lazim must accept mixed 1/2/5/10 kg sandstone incrementally until exactly 32
kg, return a native 32 kg block, let the player chisel it into a base, and
require placement on the flat ground. Repeat for exactly 20 kg/body. Current
code consumes 35 kg in one batch for the base and 20 kg in one batch for the
body, with the chisel merely present, and directly changes scenery. It neither
sets `%enakh_lazim_statue_body_blurb` nor provides the intermediate items and
their destroy/replacement lifecycle.

The player then chisels the placed statue, discusses a head, carves the saved
choice from one 5 kg granite, and places it. A mismatched head costs granite
but is rejected and the choice can be changed. Current Talk performs the
chisel action, and its hand-in deletes any one of four heads while setting the
visual from `%enakh_choose_statue_head`; a wrong head is silently accepted.
Enforce nonboostable 50 Crafting at the actual carving actions, preserve the
second 5 kg granite for the pedestal, and verify animation/delay/no-extra-XP
against live behavior before coding.

Head placement must run the collapse/push sequence, set the fallen-statue and
Lazim-location fields, advance 10→20, and place the player at the bottom-floor
arrival. Cancellation/disconnect must leave a recoverable state with the east
secret entrance available. Do not route initial completion through a static
boulder click.

### Fallen statue, outer rooms, sigils, and secret exits

After the reveal dialogue, chisel or pickaxe on the fallen statue presents one
available limb at a time. The native aggregate is 63: fallen state 3 plus all
four taken bits. Current one-click grant demands four slots and sets only the
four taken bits, yielding aggregate 60. It also has no head-return conversation
and no lost-limb recovery. Dropping any limb before its door permanently
bricks the current route because the source refuses all further extraction.

Restore individual choice, one-slot capacity checks, both tools, Lazim's
original-head return, and inventory/bank/door-aware replacement for every
unconsumed limb and sigil. Do not duplicate an item merely because it is in the
bank. The canonical destroy text explicitly points limb recovery to Lazim.

Physical room access is the authorization for each pedestal. Current sigil
pickup incorrectly requires all four limb-lock bits before even the first M
sigil. The outer route may be explored anticlockwise for correctly ordered
story scenes, but only one matching inner sigil door is required to reach the
centre; the other three are optional permanent unlocks. Current ladder instead
requires all four inner doors and does not consume any sigil, contradicting
the Wiki and transcript.

Each limb/sigil insertion must consume exactly one item, set its native bit,
play the associated headache/cutscene when applicable, and execute a real door
pass. Because the loc pairs are standalone rather than player-varbit multilocs,
use the progress bit as player authorization and a transient door swing/walk
on each passage; do not leave a shared-world door globally open for one
player. Add Open behavior to the closed leaves for already-unlocked players.
Climbing an outer sand pile must set the corresponding N/E/S/W boulder bit so
that exterior re-entry is genuinely unlocked.

All five historical scenes are absent: temple completion, the Avarrockian
attack, Akthanakos's attempted persuasion, failed skeleton weapon, and
Enakhra's betrayal/freezing of Akthanakos. They are progression content, not
decorative comments; cancellation/relogin needs an idempotent resume/seen
policy using native one-shot fields.

### Pedestal and four elemental puzzles

The original chosen head is first tried in the pedestal and found too small;
Lazim then explains the mould. Soft clay creates the positive mould, the second
5 kg granite becomes Akthanakos's camel head, and placement advances 20→30 and
plays the betrayal scene. Current code lets soft clay work immediately, never
returns/tries the original head, ignores wrong-head/seen fields, and jumps
40→45. Preserve capacity before irreversible deletion and allow a lost mould
or puzzle head to be rebuilt from clay/granite at the proper phase.

The four rooms are independent. State 30 advances to 40 only when blood, ice,
smoke, and shadow are all complete. Current braziers cannot even start until
the other three are done, and state advancement is tied solely to the forced
last coal action.

- Blood: Pentyn accepts a whole bread, whole pie/cake/pizza, or supported
  topped baked potato. Current hardcoded list omits the revision-239 butter and
  cheese potatoes and does not scale with the cache's whole-food categories.
- Ice: cast a real fire Bolt-or-stronger standard spell on the frozen-fountain
  NPC. Current Melt click checks only Fire Bolt requirements and consumes no
  runes, awards no spell XP, and plays no cast.
- Smoke: cast a real air Bolt-or-stronger spell on the furnace NPC. Current
  Clear click repeats the same fake-cast defect and rejects valid higher spells.
- Shadow: place the coordinate-specific logs/oak/willow/maple/candle/coal, then
  use a tinderbox to light the fuel. Investigate requires the effective
  Firemaking knowledge check; insertion itself permits guess-and-check.
  Current use-item handlers instantly light, ignore tinderbox and darkness,
  force an invented sequence, and only accept an unlit candle.

Implement spell-target hooks through the modern magic event path so level,
spellbook, runes/staves, delay, animation/projectile, XP, and target-specific
success all remain owned by Magic. Implement darkness damage and lit-candle
protection as an area effect. Keep the six existing brazier varbits and compute
the shadow/primary aggregates after every successful sub-action.

After all four globes are lit, the barrier's exact handler must authorize and
perform the two-tile pass-through. The central/top ladder handlers should
validate the canonical phase and delegate to their existing coordinate-specific
maplinks, not replace travel with messages.

### Boneguards, wall, and finale

The first Boneguard is Talk-to in native op2, warns the player, and attacks if
they linger. Crumble Undead must be selected and cast; the spell consumes
runes and succeeds only on a landed hit, so splashes require another cast.
Current overlay moves Talk-to to op1, while current quest code treats that
ordinary click as an automatic successful spell. The shared Crumble proc is a
real spell but currently requires an ordinary attackable NPC. Add a narrowly
scoped quest-special branch/callback that retains its rune, timing, animation,
accuracy, and splash behavior and only writes state 50 on hit. Transform the
guard to its pile, spawn five big bones, and play the freed-spirit dialogue.

The stone ladder must really move 2→1. In the final room, create/show the
Akthanakos Boneguard at the cache/QH coordinate while state 50 is active; the
freed wrapper is not a substitute. First contact without protection permits
the canonical attack and writes seen state. With Protect from Melee active,
the guard attacks several times, native hits-dodged/seen fields advance, and
the player may accept or refuse the wall task. Current code refuses all contact
until prayer is on and then instantly reveals Akthanakos's identity.

The nearby rubble must give one 5 kg sandstone per Take-rock, at most three,
advancing `%enakh_rubble_limit` and its 3/2/1/empty visuals. The wall's existing
alternate sandstone/chisel transaction and three-stage multivar are the
strongest part of the direct script and should be retained, with interruption,
duplicate-click, and inventory tests. Completion cannot require stone imported
from the quarry.

After the third trim, talk to the still-present Boneguard. Transform it into
Akthanakos, deliver the Camulet, play the Enakhra confrontation, wall
destruction, skeletal forms, and northern departure, then commit reward state.
Current code omits all forms/scenes and writes 70 before XP/UI/item delivery.
Its completion proc has no idempotence guard, so a direct repeated call can
duplicate XP/QP presentation. If inventory is full it completes anyway, sets a
promise flag, and permanently loses the reward because the post-completion
Akthanakos branch returns before retrying.

Completion should be one guarded transaction: verify state 60 and wall/help
substates; reserve/deliver the Camulet (or pause before completion with the
transcript's retry); run the finale; award each XP/QP exactly once; write 70;
set final forms; and show the reward UI. Relogging at every cutscene boundary
must either resume or settle to one coherent side, never replay awards.

### Post-quest item and downstream lifecycle

The Camulet is currently only an inert item definition plus a completion grant.
No repository script handles Check-charge, Rub, worn Check/Teleport/Temple/
Surface, camel speech, dung recharge, shared charge drain, or Lazim services.
Implement the native `%enakh_camulet_charge` contract with four charges shared
across duplicate amulets, the inside-temple destination, the hard-Desert-Diary
surface destination, no-charge feedback, and no dung consumption when already
full. Lazim must replace a missing Camulet with inventory/bank/equipment-aware
ownership and offer the permanent one-million-coin unlimited upgrade. Loss at
quest completion must use the same safe recovery path.

Using soft clay on the installed pedestal head after the quest must create the
camel mask. This advertised reward is absent. Wearing the Camulet must enable
the supported camel/Ugthanki conversations; audit My Arm's Big Adventure and
dung acquisition when wiring that shared behavior.

The Medium Desert Diary task is specifically a Camulet teleport. The diary
framework has counters but no task hook, so the teleport success path must
complete the exact Desert-medium task once. Desert Treasure II's prerequisite
resolver must compare the native Enakhra end state 70; do not duplicate an
invented boolean.

## 6. Migration and modernization sequence

### Gate A — restore canonical state and migration first

1. Replace constants with 0/10/20/30/40/50/60/70 and named substate
   predicates. Remove 45/55 and correct stale comments/reward assertions.
2. Add a one-time migration for existing local saves:
   local 0→0, 10→10, 30/40→20, 45→30, 50→40, 55→50, 60→60, 70→70. Refine
   30/40 from pedestal/puzzle/guard substates rather than player position.
3. Repair aggregates while migrating: if all four limb bits are set, set
   fallen-statue state 3 so the aggregate is 63; retain individual locks,
   sigils, puzzles, braziers, wall stages, and chosen head.
4. For migrated state-70 players, expose Lazim replacement rather than
   silently minting duplicates. Initialize a recovered Camulet to the correct
   charge policy and preserve any existing upgrade/charge field.

### Gate B — make the route physically playable

1. Restore acceptance and exact 32/20 kg/intermediate-item statue flow.
2. Implement collapse/fall and all four persistent secret-entrance unlocks.
3. Wire exact ladder handlers to existing maplinks and author barrier/boulder
   cross-map movement with correct source/destination checks.
4. Implement player-authorized transient door swings and consumption for all
   limbs/sigils; allow one inner door to reach the centre.
5. Spawn/control the final Boneguard separately from freed Akthanakos and add
   the three-use rubble interaction.

### Gate C — restore mechanics, narrative, and recovery

1. Add individual limb selection, original-head and lost-item recovery, all
   five historical scenes, and pedestal wrong-head/mould behavior.
2. Make all four puzzle rooms independent; integrate real elemental spell
   targeting, two-stage brazier lighting, Firemaking inspection, darkness, and
   complete supported food categories.
3. Integrate real Crumble Undead hit/splash, both Boneguard attack sequences,
   Protect from Melee, wall task acceptance, forms, and the full ending scene.
4. Harden every consume/create operation for capacity, duplicates, banking,
   cancellation, death, teleport, disconnect, and relog.

### Gate D — completion and consumers

1. Make reward settlement guarded, Camulet-safe, and idempotent.
2. Implement Camulet charge/teleport/recharge/upgrade/replacement/camel-speech
   behavior and camel-mask creation.
3. Wire the Desert-medium teleport task and validate Desert Treasure II's
   native quest prerequisite.
4. Rewrite the journal around canonical primary and independent substate,
   including exact lost-item and full-inventory recovery advice.
5. Keep debug commands as state setup, then add assertions rather than using
   synthetic completion as acceptance evidence.

## 7. Verification matrix

| Area | Required checks |
| --- | --- |
| Discovery/start | Native row; start icon/NPC; offer/refusal/re-offer; no false hard skill gate; journal colour/state |
| Statue weights | Mixed 1/2/5/10 kg deliveries across multiple conversations; exact 32/20 totals; over/under behavior; both intermediate blocks; destroy/replacement; 50 Crafting policy |
| Head/fall | All four choices; wrong head; granite loss/retry; full inventory; collapse destination; logout at sequence boundaries; east re-entry |
| Bottom floor | Chisel and pickaxe; each limb order; one free slot; drop/destroy/bank/reclaim; aggregate 63; four outer doors; each optional inner route; exact sigil consumption; cutscenes |
| Secret entrances | Every inner sand pile unlocks matching exterior boulder; enter/exit/re-enter on all four; pre-unlock closed state |
| Pedestal | Original head too small; Lazim advice; clay/mould/granite/head loss and recovery; wrong head; betrayal scene; optional camel mask |
| Four puzzles | Any room order; every whole supported food; partial/burnt rejection; valid bolt/blast/wave/surge and invalid spell/spellbook; rune/staff/XP; splash where applicable; each fuel coordinate; tinderbox second step; Firemaking inspection; darkness/candle |
| Barrier/travel | Four-globe aggregate; both pass directions; central/top ladders and pillar ladders use real destinations; cancellation leaves coherent state |
| Boneguards | Talk warning/attack; real Crumble rune drain/XP/animation; splash retry; on-hit transform/bones; final actor visibility; unprotected hit; prayer drain/attacks; accept/refuse/re-offer |
| Rubble/wall | Exactly three local 5 kg rocks; depletion visuals; full inventory; sandstone/chisel alternation; interruption/relog at every wall/trim stage |
| Completion | Full inventory pause/retry; Camulet delivered before commit; forms/cutscene; exact four XP awards and 2 QP once; repeated Talk/call/relog/death cannot duplicate |
| Camulet | Initial four charges; inventory/worn teleports; shared duplicate drain; zero-charge behavior; dung recharge/full refusal; lost/banked/equipped replacement; million-coin upgrade; hard-diary surface target; camel speech |
| Downstream | Camel mask; Medium Desert Diary task only on successful teleport; Desert Treasure II rejects state 60 and accepts 70 |
| Migration | Every old primary value and mixed substate; no downgrade of 70; no lost/duplicated limb, sigil, XP, QP, or Camulet |

Required static checks include a clean RuneScript/config build, duplicate-trigger
scan, unresolved symbol scan, door/maplink ownership audit, and
`python3 tools/questhelper_extract.py enakhraslament --check` with only the
documented cache-name/version differences. Required runtime evidence is a fresh
0→70 playthrough without commands or direct teleports, a second playthrough in
a different room/puzzle order, all recovery/full-inventory branches, and
post-quest/consumer tests.

## 8. Definition of done

Enakhra's Lament is modernized only when a fresh player can explicitly accept,
build the statue with exact-weight native items, fall into and freely re-enter
the temple, traverse real doors/ladders/barrier, see the historical scenes,
solve the four independent puzzles through real skill/spell systems, defeat
both Boneguard mechanics, repair the wall from local rubble, receive rewards
exactly once, and use/recover/recharge/upgrade the Camulet. All native state,
cache transforms, journal advice, migration, camel mask, Desert Diary task, and
Desert Treasure II prerequisite must agree under normal play, interruption,
loss, full inventory, banking, death, relog, and repeated interaction. Debug
state writes, compilation alone, and a reward scroll alone do not satisfy that
contract.
