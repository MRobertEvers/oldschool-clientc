# Grim Tales modernization audit

Status: `audit-pending` — the native quest row, both carrier varps, primary and
support fields, actors, scenery, quest items, journal dispatch, completion
adapter, and admin adapter exist. The intended route is not completable. The
first deterministic deadlock occurs when Sylas consumes the griffin feather
without setting `%grim_given_feather`, while the drain pipe requires that bit.
Even beyond that point, the wall never moves the player, freed Rupert has no
Talk-to handler and cannot issue his helmet, the piano accepts only eight of
nine notes, the canonical third mouse-hole climb has no transition, and the
shared cloud arena has neither a correct exit nor a private Glod encounter.
The implementation also replaces native primary-state semantics, omits every
boostable action check, substitutes unsafe inventory transactions, and leaves
Glod's mechanics, drops, recovery, and downstream unlocks incomplete.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to every primary/support state, dialogue,
skill check, puzzle, route transition, temporary form, encounter, item
transaction, cutscene, reward, recovery path, journal/admin adapter, shared
owner, and direct downstream consumer. It is an implementation specification,
not completion evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable route, dialogue, skill, puzzle, combat, item, recovery, reward, and
integration contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Grim Tales](https://oldschool.runescape.wiki/w/Grim_Tales?oldid=15292387) | 15292387, 2026-08-11 | Identity, requirements, complete route, supplies, Glod mechanics, rewards, and unlocks |
| [Grim Tales/Quick guide](https://oldschool.runescape.wiki/w/Grim_Tales/Quick_guide?oldid=14903800) | 14903800, 2025-05-19 | Ordered actions, exact piano and mouse routes, skill checks, and final sequence |
| [Transcript:Grim Tales](https://oldschool.runescape.wiki/w/Transcript%3AGrim_Tales?oldid=15263381) | 15263381, 2026-07-14 | Acceptance, wrong story answers, puzzle results, cutscenes, combat effects, retries, and full-inventory completion |
| [Grimgnash](https://oldschool.runescape.wiki/w/Grimgnash?oldid=15025501) | 15025501, 2025-11-12 | Story failure damage and pre-/post-route behavior |
| [Griffin feather](https://oldschool.runescape.wiki/w/Griffin_feather?oldid=15184997) | 15184997, 2026-04-22 | Nest source, ownership, Destroy text, and reacquisition |
| [Rupert the Beard](https://oldschool.runescape.wiki/w/Rupert_the_Beard?oldid=15041168) | 15041168, 2025-11-19 | Tower/freed forms, helmet issue, and 60,000-coin postquest sale |
| [Rupert's helmet](https://oldschool.runescape.wiki/w/Rupert%27s_helmet?oldid=15183196) | 15183196, 2026-04-22 | Freed-Rupert source, Destroy text, and replacement |
| [Miazrqa](https://oldschool.runescape.wiki/w/Miazrqa?oldid=15128577) | 15128577, 2026-02-17 | Pendant exchange and Rupert release responsibility |
| [Miazrqa's pendant](https://oldschool.runescape.wiki/w/Miazrqa%27s_pendant?oldid=15184999) | 15184999, 2026-04-22 | Mouse-hole source, Destroy text, and repeat acquisition |
| [Piano (Witch's House)](https://oldschool.runescape.wiki/w/Piano_%28Witch%27s_House%29?oldid=15263052) | 15263052, 2026-07-14 | Nine-note interface and compartment behavior |
| [Shrinking recipe](https://oldschool.runescape.wiki/w/Shrinking_recipe?oldid=15185003) | 15185003, 2026-04-22 | Compartment contents, recipe, read surface, and replacement |
| [To-do list](https://oldschool.runescape.wiki/w/To-do_list?oldid=15185004) | 15185004, 2026-04-22 | Compartment source, read surface, and replacement |
| [Shrunk ogleroot](https://oldschool.runescape.wiki/w/Shrunk_ogleroot?oldid=15185002) | 15185002, 2026-04-22 | Two initial roots, Experiment source, tradeability, and Eat behavior |
| [Experiment No.2](https://oldschool.runescape.wiki/w/Experiment_No.2?oldid=15199729) | 15199729, 2026-04-28 | Melee/ranged styles, thrown ogleroots, stats, respawn, and drops |
| [Shrink-me-quick](https://oldschool.runescape.wiki/w/Shrink-me-quick?oldid=15185001) | 15185001, 2026-04-22 | Level 52, 6 Herblore XP, exact drinking location, cat check, and postquest use |
| [Mouse hole](https://oldschool.runescape.wiki/w/Mouse_hole?oldid=15112972) | 15112972, 2026-01-25 | Dungeon topology, mice, grate/sewer connection, and repeat entry |
| [Magic beans](https://oldschool.runescape.wiki/w/Magic_beans?oldid=15184998) | 15184998, 2026-04-22 | Sylas source, Destroy text, replacement, and planting evidence conflict |
| [Glod](https://oldschool.runescape.wiki/w/Glod?oldid=15221322) | 15221322, 2026-05-29 | Private fight, exact stats, specials, resistances, guaranteed drops, and Nightmare Zone unlock |
| [Golden goblin](https://oldschool.runescape.wiki/w/Golden_goblin?oldid=15184996) | 15184996, 2026-04-22 | Glod drop, Destroy text, and cloud-area replacement |
| [Dwarven helmet](https://oldschool.runescape.wiki/w/Dwarven_helmet?oldid=15183706) | 15183706, 2026-04-22 | Reward, Defence/quest equip gates, Rupert/Perdu replacements, and diary use |
| [Falador Diary](https://oldschool.runescape.wiki/w/Falador_Diary?oldid=15295882) | 15295882, 2026-08-13 | Hard task for equipping the helmet in the Dwarven Mine |
| [Nightmare Zone](https://oldschool.runescape.wiki/w/Nightmare_Zone?oldid=15271187) | 15271187, 2026-07-21 | Postquest boss integration |
| [Perdu](https://oldschool.runescape.wiki/w/Perdu?oldid=15299884) | 15299884, 2026-08-14 | 115,000-coin helmet recovery service |

These sources define a members, master, medium quest released 4 June 2007.
Witch's House is the sole quest prerequisite. Farming 45, Herblore 52,
Thieving 58, Agility 59, and Woodcutting 71 are all boostable checks at their
respective actions and do not prevent starting the quest. Agility is checked
twice: on Rupert's beard and on the giant beanstalk. The mandatory route uses
two unfinished tarromin potions, the Witch's House key, a watering can, an axe,
combat supplies, and normally a seed dibber. Rewards are one quest point;
60,000 Woodcutting, 25,000 Agility, 25,000 Thieving, 15,000 Herblore, 10,000
Farming, and 5,000 Hitpoints XP; and a dwarven helmet.

Transition aid only: Quest Helper's
[`GrimTales.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/grimtales/GrimTales.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` (the file last
changed in `7ec39fd78484d294bd6846501080cb0d19e99eec` on 2026-06-01)
confirms primary states 0–4, 10, 12, 15, 16, 17, 19, 20, 30, 40, and 50;
the native substate thresholds; the nine piano notes; exact five-climb maze
route and two wrong-route exits; all actors, locs, zones, supplies, skills,
rewards, and Glod's level. Running
`python3 tools/questhelper_extract.py grimtales --check` at that commit resolves
all referenced gamevals and the quest row. Quest Helper cannot prove server
trigger reachability, exact state-write semantics, actor/instance ownership,
transactions, combat AI, recovery, or downstream consumers.

## 2. Native quest identity and state contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_grimtales`; quest metadata ID 135 |
| Type / difficulty / length | Members quest / master / medium |
| Release / location | 4 June 2007 / Taverley |
| Start | `grim_sylas` at the native Taverley quest marker |
| Primary state | `%grim_quest`, bits 0–7 of permanent/transmitted varp `grim_main` |
| Observed canonical route values | 0–4 start conversation; 10 collect/hand in feather and helmet; 12/15/16/17/19 Sylas item-exchange continuation states; 20 plant/water; 30 Glod/goblin; 40 shrink/chop; 50 final talk; 60 complete |
| End / quest points | State 60 / 1 QP |
| Requirement policy | Witch's House complete; five boostable action checks; cache correctly says not to check skills on start |
| XP | Native dbrow values exactly match the six current rewards, in tenths |
| Item reward | Dwarven helmet, with full-inventory deferral and postquest replacement services |
| Direct unlocks | Glod in Nightmare Zone; dwarven-helmet Hard Falador Diary task; Rupert and Perdu replacement services |

The cache row's `requirement_quests` entry resolves locally to A Porcine of
Interest, which conflicts with the authoritative Witch's House prerequisite.
The current runtime's explicit `%ballquest = ^ball_complete` gate is therefore
the correct authority until the quest metadata row itself is repaired. The row
also has `requirement_check_skills_on_start=0` and
`requirements_boostable=1`; adding five base-level checks to acceptance would
be a new defect.

Both native carriers contain a much richer contract than the current scripts
drive:

| Field | Native storage | Canonical responsibility / audit result |
| --- | --- | --- |
| `%grim_storyline` | `grim_main` bits 9–13 | Seven-stage story flow; used, but wrong outcomes are replaced |
| `%grim_dwarfquest` | bits 14–18 | 0/5/10/15/20/25 tower and pendant route; broadly used |
| `%grim_dwarfspoken` | bit 19 | Rupert conversation history; orphaned |
| `%grim_listentoreason` | bit 20 | Dialogue/cutscene history; orphaned |
| `%grim_pianotrack` | bits 21–26 | Note counter 0–8 before the ninth press; implementation completes at 8 |
| `%grim_piano_used` | bit 27 | Piano-open transform; used |
| `%grim_dwarfinformed` | bit 28 | Rupert information history; orphaned |
| `%grim_gramma_check` | bits 29–30 | Gramophone states; orphaned |
| `%grim_dwarf_vis` | bit 31 | Freed Rupert outside tower; used |
| seven piano-note bits | `grim_second` bits 0–6 | Per-key highlights; all orphaned |
| `%grim_musicsheet_found` | bit 7 | Music-stand entitlement; orphaned |
| `%grim_beard_climb` | bits 8–10 | Five visual/cutscene states; only 0 and 2 are used |
| `%grim_dwarf_vis_tower` | bit 11 | Tower Rupert transform; never written |
| `%grim_small` | bit 12 | Temporary mouse-sized form; written without lifecycle recovery |
| `%grim_logincheck` | bit 13 | Login/recovery support for the temporary form; orphaned |
| `%grim_stalk_state` | bits 14–17 | Mound/planted/grown/shrunk/stump multiloc; used, with an inferred final value |
| `%grim_giant_dead` | bit 18 | Player's Glod defeat; unsafe public-NPC write |
| `%grim_head_found` | bit 19 | Piano compartment searched; used with the wrong grant |
| `%grim_griffin_asleep` | bit 20 | Grimgnash transform; used without quest-stage ownership |
| `%grim_manhole_open` | bit 21 | Sewer access presentation; orphaned |
| `%grim_given_feather` | bit 22 | Feather hand-in; read but never written |
| `%grim_given_helmet` | bit 23 | Helmet hand-in; entirely ignored |
| `%grim_have_pendant` | bit 24 | Pendant source transform; written too early and never recoverable |
| `%grim_show_glyph` | bit 25 | Shrinking glyph transform; orphaned |
| `%grim_show_musicsheet` | bit 26 | Music-stand transform; misused by piano search |
| `%grim_stalk_cut` | `grim_main` bit 8 | Additional stalk-cut history; orphaned |

### Required migration

The port's claim that sparse Quest Helper keys can be collapsed is unsafe.
Primary quest values are persistent server/client/content contracts, not just
panel indexes. Current/native saves at 1–4 are treated as unstarted and can be
overwritten with 10; saves at 12/15/16/17/19 fall through to postquest thanks
dialogue while remaining stranded. Conversely, the port invented state 25 for
“beans planted,” while current Quest Helper keeps the route at 20 and reads
`%grim_stalk_state=1`.

| Existing value | Local meaning / behavior | Modernization action |
| ---: | --- | --- |
| 0–4 | Local treats all as pre-start because they are below 10 | Preserve exact current start-dialogue semantics; capture writes before implementing |
| 10 | Collect both items | Retain, reconcile `%grim_given_feather`, `%grim_given_helmet`, items, and subquests |
| 12/15/16/17/19 | Local has no branches and shows completion-like dialogue | Preserve each native transition; obtain live/deob traces for exact meanings |
| 20 | Both items exchanged, beans issued | Retain as plant/water phase; make beans recoverable |
| 25 | Port-only planted checkpoint | One-time map to 20 with `%grim_stalk_state=1`, after contradiction review |
| 30/40/50/60 | Broadly align with fight/delivered/final/complete | Reconcile encounter, stalk, item, and reward settlement before trusting them |

Use a deployment/version marker and a one-time migration before corrected
handlers become reachable. Do not infer exact 12–19 meanings only from their
numbers. Capture primary/support values after each Sylas dialogue on a current
reference server or equivalent deob, then map local histories using support
bits, owned items, stalk form, and settlement evidence. Never re-award historic
XP or a helmet merely because state 60 exists.

## 3. Implementation surface

The direct root has 1,122 lines across one constants file and six scripts. The
route also depends on Witch's House shared doors, generic maplinks, Herblore,
Farming equipment, combat/death/drop systems, dynamic-map ownership, diaries,
Nightmare Zone, equipment requirements, and lost-item services.

| Path / subsystem | Present responsibility | Audit result |
| --- | --- | --- |
| `quest_grimtales.constant` | Local aliases, requirements/rewards, zones, piano keys | Rich inventory, but falsely redefines native primary states and records several deferrals as acceptable |
| `grim_sylas.rs2` | Acceptance, item exchange, route hints, goblin hand-in, completion | Correct prerequisite/accept choice; feather bit never set, helmet bit ignored, beans can be granted without both items, capacity/recovery/settlement unsafe |
| `grim_grimgnash.rs2` | Story and feather pile | Route-shaped; no stage ownership, canonical failures/damage missing, final answer wrong, capacity/bank recovery unsafe |
| `grim_watchtower.rs2` | Wall, pipe, beard, Rupert window, Miazrqa | Wall does not cross, skill checks absent, cutscene transforms incomplete, visible Rupert has no handler, helmet cannot be obtained |
| `grim_witchhouse.rs2` | Ladders, piano, compartment, Experiment death, potion, mouse maze, pendant | Eighth-note completion, wrong loot, no Herblore check/XP, broad drink zone, canonical maze deadlock, unsafe pendant |
| `grim_beanstalk.rs2` | Plant/water, cloud entry, Glod death, goblin, shrink/chop | Unsupported state 25, no action checks, no can charge, public arena/NPC, no exit, no specials/drops, weak stage guards |
| `grim_journal.rs2` | Quest journal | Registered correctly but follows collapsed states and impossible support flags |
| `quest_ball_locs.rs2` | Shared Witch's House key/front door | Post-Witch's-House entry repair is valid; pot grant still needs ordinary capacity coverage |
| `brew_potion.rs2` | Shared two-way ogleroot/tarromin dispatch | Correct additive routing pattern, but calls a recipe with no level check, 6 XP, or knowledge policy |
| generic maplinks | House ladder and manhole travel | Ladder/manhole rows exist; cloud beanstalk top has no verified return maplink |
| cache actors/locs/items | All native presentation and operations | Assets are substantially complete; many operations and transform fields are orphaned |
| journal / quest cheat | Journal dispatch and state-only completion | Correct row routing and intentional state-only admin behavior; no reconciliation |
| Falador Diary / Nightmare Zone / lost-item services | Downstream rewards | No Grim Tales hooks found; NMZ itself is a two-boss stub |
| automated tests | Route and failure coverage | No Grim Tales tests found |

The cache's Glod and Experiment stats match the current Wiki. This is not a
numeric-ID or basic asset problem. “Old machinery” here is the use of static
teleports, broad zones, public NPC lookup, inferred state values, narrated
cutscenes, and unprotected delete/add/write sequences in place of owned modern
encounters, exact transitions, native substates, and recoverable settlement.

## 4. Primary route reachability

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| 0–4 | Explore Sylas topics, explicitly accept, arrive at collection state 10 | Explicit Yes/No and Witch's House check exist, but every native 1–4 value re-enters the offer and can be overwritten |
| 10: feather | Complete story, take feather, give it to Sylas, set native hand-in progress | Sylas deletes the feather but never writes `%grim_given_feather`; journal still asks for it and the pipe refuses access. This is the earliest deterministic deadlock. |
| 10: helmet | Cross wall, pipe twice, beard, Rupert, Miazrqa, piano/potions/maze, pendant, release Rupert, receive helmet, give it to Sylas | Wall does not move; beard has no Agility check; piano/maze fail; visible Rupert has no handler. Sylas can nevertheless grant beans from freed-Rupert bits without owning either requested item. |
| 12/15/16/17/19 | Native Sylas exchange continuation | No local branch; generic postquest wording strands imported/current saves |
| 20 | Plant and water while primary remains 20 and stalk substate advances 0→1→2 | Plant writes invented primary 25; neither action validates stage or Farming; watering is not a charge transaction |
| 30 | Agility-gated private cloud fight; Glod drops goblin and other guaranteed loot | Static shared teleport/public NPC, no follower gate, no specials, no proper drops, no return path; goblin is fabricated on a later climb |
| 40 | Apply second potion, then Woodcutting-gated chop | Weak stage checks allow early irreversible stalk transforms; no Woodcutting check or cutscenes |
| 50 | Final Sylas talk; require free slot; settle item/XP/QP exactly once | Item and all XP are granted before state 60 with no capacity or settlement guard; helmet can be lost and interruption can duplicate/partially settle |
| 60 | Postquest dialogue, replacements, diary/NMZ unlocks | Generic Sylas thanks only; Rupert sale, Perdu recovery, diary hook, and Glod unlock are absent |

State injection can hop over individual failures, but cannot establish route
correctness. The current queue entry's “done” label and historical compile
success prove only that symbols and scripts compiled, not that any legal player
can traverse the quest.

## 5. Detailed lifecycle audit

### Acceptance and Sylas item ledger

Keep the explicit Witch's House completion check and the actual accept/refuse
choice. Do not add skill checks there. Rebuild states 0–4 from captured native
dialogue transitions, including optional topics and interruption/re-talk
behavior, then enter 10 only at the canonical point.

At state 10, each item hand-in must be independently transactional. Feather
hand-in currently deletes the item and omits `%grim_given_feather=1`; helmet
hand-in deletes the item and omits `%grim_given_helmet=1`. The top-level router
then prioritizes `%grim_dwarfquest>=25 && %grim_dwarf_vis=1` over actual
ownership. `grim_sylas_have_helmet` conditionally deletes a helmet, never
requires the feather bit, unconditionally claims both items were delivered,
adds beans, and writes 20. A state-injected or inconsistent save can therefore
skip both requested items, while an ordinary save deadlocks after the feather.

Implement the captured 10→12/15/16/17/19→20 transitions and both native given
bits. Revalidate the expected state and item immediately before deletion.
Never conflate “Rupert is free” with “Rupert issued a helmet” or “Sylas received
it.” Beans require both hand-ins and a free slot, and must remain recoverable
from Sylas after destruction/loss while the stalk is still unplanted. Ownership
checks must distinguish inventory, bank, ground, issued history, consumed
history, and true loss.

### Grimgnash story and feather

The canonical first conversation offers multiple answers. Dangerous answers
and several later wrong story choices make Grimgnash hit 20% of current
Hitpoints rounded up; the damage can kill. Some answers return to the preceding
choice, while others end the conversation. After “Started to strangle the poor
gnome,” multiple remaining endings successfully put him to sleep. The current
script instead exposes the quest story even before acceptance, loops every
wrong answer harmlessly at the same beat, persists all successful beats, and
requires a fabricated combined “bones ... and a hole” final answer. It also
omits use-any-item-on-Grimgnash behavior.

Gate quest progress to the correct primary phase while retaining canonical
ordinary interaction outside it. Implement exact failure topology, damage,
interrupt/re-talk continuation, and all valid final endings. Feather pickup
must check capacity before committing, remain repeatable after true loss, and
not duplicate an item in the bank or a live ground item. The native sleep bit
drives the actor transform and should not be writable prequest.

### Watchtower, beard, Miazrqa, and Rupert

The crumbling east wall is the 58 Thieving action. Its handler prints success
but neither checks boosted Thieving nor crosses the collision boundary, so it
cannot perform the advertised route. Implement direction-aware movement,
animation/failure behavior, and tests from both sides. The pipe's requirement
that the feather has been given matches Quest Helper's ordered route; the
missing feather write, not the pipe, is the present fault.

The beard climb must check boosted Agility 59. Preserve the full five-state
`%grim_beard_climb` presentation and the tower/outside Rupert transforms across
both ascent and descent. Current direct teleports omit animations and do not
write `%grim_dwarf_vis_tower`, `%grim_dwarfspoken`, or related dialogue fields.

Pendant hand-in canonically releases Rupert through a cutscene, brings him
outside, and walks the player to a follow-up conversation where he issues
Rupert's helmet. The port splits the exchange over two Miazrqa talks, has no
release cutscene, and—most importantly—binds no Talk-to handler to
`grim_rupert_visible`. No legal route can receive the helmet. Add a shared
Rupert router for trapped, newly freed, quest-item replacement, active quest,
and postquest store states. Helmet issue must be capacity-safe, recoverable
after destruction, and recorded separately from Sylas hand-in. After completion
Rupert sells additional dwarven helmets for 60,000 coins; that transaction must
be stack-aware and must not charge on failed delivery.

### Witch's House, music sources, piano, and potion

The shared key/pot/front-door path correctly permits a Witch's House completer
to re-enter. Native maplinks also cover the house ladder and outside manhole.
Optional canonical discovery content is absent: the electrified cage/leather
gloves, music-stand sheet, read surfaces for the music sheet and to-do list,
gramophone, glyph, and their support fields. A player who already knows the
tune can skip some discovery, so these are fidelity/recovery omissions rather
than the first blocker, but the native operations should not remain inert.

The exact piano sequence is upper E-F-E-D-C, then lower A-E-G-A. The native
counter reaches 8 after lower G and expects the final lower A. The constant is
incorrectly set to 8, so the port opens the piano one press early; its expected
function also returns lower G for every position after 6. Implement all nine
presses, reset/highlight/audio behavior, and the interface's native Open/Search
components rather than requiring an undocumented world-object fallback.

The first compartment search must grant a shrinking recipe, a to-do list, and
two shrunk ogleroots—not a music sheet. The current code attempts two item adds,
writes the searched bit regardless of success, and grants no roots or to-do
list. Make the multi-item issue capacity-safe and restartable; do not consume
entitlement until all non-stackable items and the root stack are delivered.
Afterward the compartment reports no more roots; Experiment No.2 remains the
repeat source.

Adding an ogleroot to an unfinished tarromin potion requires boosted Herblore
52 and awards 6 XP. The shared two-way dispatch is structurally correct, but
the quest proc checks neither level nor XP and can be used before learning the
recipe. Preserve current OSRS's postquest ability to make the potion while
separating recipe knowledge from active-quest stage. Implement the ogleroot's
non-consuming failed Eat behavior and potion Empty/Destroy surfaces.

Drinking must work only in the southern mouse-hole room, not anywhere in the
whole house bounding box. Outside, it must remain unconsumed and show the
canonical refusal. A following cat blocks drinking; the follower/pet lifecycle
must be verified. Enter mouse form through an owned transition and use
`%grim_logincheck` or equivalent modern recovery so logout, reconnect,
teleport, death, and interruption cannot preserve a contradictory
`%grim_small` value or strand the player.

### Mouse-hole maze and pendant recovery

The canonical route is five exact interactions:

1. Climb up at 2282,5543,0.
2. Climb up at 2268,5520,1.
3. Climb up at 2270,5515,2.
4. Climb down at 2283,5530,3.
5. Use the climb-up loc at 2284,5542,2 (the guide describes the movement as
   down), then take the pendant.

The zone-only implementation handles rooms 1→2, 2→3, 4→5, and 5→6, but has no
up transition for room 3→4. The correct third nail therefore prints a generic
message and leaves the player in place. Its down handler instead sends every
room-3 down loc to a wrong branch. Because multiple instances of the same loc
type occur inside one broad room zone, room membership cannot distinguish the
correct nail from wrong nails. Route by exact loc coordinate/placement plus
direction, including wrong-route exits at 2277,5551,2 and 2268,5515,0. Test all
nails, not just the five highlighted by Quest Helper.

Pendant pickup currently attempts the add, immediately sets
`%grim_have_pendant=1`, clears mouse form, and teleports away. A full inventory
therefore destroys the only source entitlement permanently. Destroy text says
the player can find another pendant in the mouse hole, but the persistent bit
prevents that. Commit the source transform only after delivery, and define
drop/destruction/death/bank/live-ground ownership and re-entry rules. Preserve
repeat postquest access to the mouse hole and the sewer grate.

### Planting, watering, and stalk state

Planting requires boosted Farming 45 and the exact state/item ledger. The
current handler accepts beans at any primary phase, requires a dibber, consumes
the beans, writes stalk 1, and invents primary 25. Keep primary 20 during both
planting substates and use only `%grim_stalk_state` for the multiloc. The main
quest article and Quest Helper support bare-handed planting after the relevant
Barbarian Training unlock, while the pinned Magic beans page says a dibber is
still required and marks that claim as needing verification. Resolve this
specific conflict with a current live capture before coding; do not silently
choose either behavior.

Watering requires a can with at least one charge. The local whitelist accepts
ordinary cans 1–8, omits Gricoller's can, and never transforms/decrements the
ordinary can. Implement the shared watering-can charge transaction and verify
Gricoller's behavior against the current server. Validate stalk state 1 before
the cutscene/write so another loc-state contradiction cannot jump directly to
30. Reproduce the growth cutscene and its interruption/reconnect settlement.

### Private cloud encounter, Glod, drops, and exit

Climbing the grown stalk is the second boosted Agility 59 check and disallows a
follower/pet, including one held in inventory according to the current guide.
The port checks neither. It teleports every player to the same static template
coordinate, uses a radius-wide `npc_find` to suppress/spawn one public Glod,
and gives that NPC a short public lifetime. One player can block another's
spawn, attack another player's boss, receive ambiguous hero credit, or inherit
a damaged actor. This is not an instance despite source comments calling it
one.

Use the modern dynamic-map/owned-encounter machinery with explicit owner,
re-entry, logout, reconnect, death, teleport, and teardown contracts. Bind the
top beanstalk's Climb-down to the owning player's Taverley return. Today it has
only the generic `climb_down` category and no maplink row, so it falls one
plane at the cloud coordinates rather than returning to the earth mound.

Glod's cache stats are correct: level 138, 160 Hitpoints, Attack 115, Defence
110, Strength 120, and current defences. His route behavior is not. Implement
aggression; melee; fear/run and taunt/charge effects; the one-time early
Attack/Strength boost; prayer deactivation and drain of 2% current points plus
20; low-HP healing; poison/venom immunity; current 50% earth weakness; and
safe forced-movement boundaries. Tests must cover safespot distance and every
special under instance ownership.

On death Glod guarantees two big bones, an uncut sapphire, an uncut ruby, a
watering can(4), and—during this quest—the golden goblin, plus the documented
tertiary bones. The local AI writes only `%grim_giant_dead` and calls generic
death; it does not add the golden goblin or other guaranteed drops. Contrary to
its own comment, it fabricates the goblin directly into inventory only on a
later climb. Implement owner-private ground drops and canonical pickup. If the
player leaves or destroys the goblin, it must wait in/reappear through the
cloud-area recovery route without making Glod infinitely repeatable. Check
inventory, bank, ground, and issued/handed-in history to prevent duplication.

### Shrinking/chopping the stalk and completion

Only state 40 may accept the second shrink-me-quick. The local handler merely
refuses while a golden goblin is in inventory; it can transform the stalk at
other phases, consumes the potion, and writes stalk 3 without a stage check.
Then any player with an axe can instantly write the inferred stump value 4;
only the primary write is conditionally protected. An early use can therefore
permanently replace the stalk and deadlock the quest.

Require exact primary/stalk states, replay-safe item consumption, and the
canonical shrinking cutscene. Chopping requires boosted Woodcutting 71 and any
supported axe, with the full cutscene/animation and a protected transition to
50. Verify whether `%grim_stalk_cut`, stalk value 4, or both own the final
presentation instead of relying on an inference from the fifth multiloc child.

At final talk the transcript explicitly says Sylas refuses to settle while the
inventory is full and asks the player to return with space. The current code
attempts the helmet add, grants six XP awards, writes 60, and renders completion
without checking capacity. A failed add permanently loses the helmet. A crash
between any XP grant and the state write can partially settle and re-award on
retry.

Use the engine's protected quest-settlement pattern: revalidate state 50,
require one free slot, reserve/issue the helmet, record each reward or commit
the settlement atomically, award exact dbrow XP/QP once, write 60 once, then
render. Test interruption at every yield/write boundary. Admin completion
remains deliberately state-only and must not masquerade as a rewarded finish.

## 6. Recovery and downstream consumers

Every Destroy-bearing quest item advertises a replacement source. The current
implementation provides none reliably:

| Item / state | Required recovery | Current result |
| --- | --- | --- |
| Griffin feather | Retake from sleeping Grimgnash before hand-in | Bank not checked; given bit never set; duplicates or permanent confusion |
| Rupert's helmet | Ask freed Rupert again before hand-in | Freed Rupert has no handler |
| Pendant | Re-enter mouse hole and retake before hand-in | `%grim_have_pendant=1` permanently hides source |
| Recipe / to-do list | Recover from basement compartment as appropriate | To-do list never issued; searched bit blocks all retries |
| Two ogleroots | Initial compartment grant; repeat Experiment drops/throws | Initial grant absent; death root exists, ranged throw absent |
| Magic beans | Ask Sylas for another bag before planting | State-20 Sylas only repeats planting instructions |
| Golden goblin | Return to cloud area after Glod defeat | Local fabricates into inventory on climb; bank/ground ownership ignored |
| Dwarven helmet | Full-slot completion deferral; Rupert 60k; Perdu 115k | Completion can lose it; both replacement services absent |

The postquest surface also needs explicit integration work:

- Nightmare Zone currently runs a fixed two-wave Count Draynor/Elvarg stub and
  never checks Grim Tales or spawns either Glod variant.
- No Falador Diary hook records equipping the dwarven helmet in the Dwarven
  Mine.
- The cache enforces the helmet's Defence 50 requirement, but no local handler
  was found that also requires Grim Tales completion before equipping this
  tradeable item.
- Neither freed Rupert's 60,000-coin store nor Perdu's 115,000-coin recovery is
  implemented.
- Shrink-me-quick may canonically be made and used after completion; do not
  “fix” broad stage access by disabling that permanent content.

## 7. Modernization sequence

### Phase 0 — capture and migration safety

1. Capture native primary/support writes for Sylas states 0–4 and
   10/12/15/16/17/19/20, interruption points, both item orders, and full slots.
2. Capture story failure topology, all valid final endings, exact wall/beard
   movement, Rupert/Miazrqa cutscene states, music sources, can consumption,
   temporary-form recovery, stalk cut fields, and golden-goblin replacement.
3. Add a versioned migration for port-only state 25 and reconcile existing
   local saves. Quarantine ambiguous 1–4/12–19/reward histories for explicit
   repair rather than guessing.

### Phase 1 — native state and legal route

1. Restore native primary-state meanings and both given-item bits.
2. Repair feather hand-in, exact wall crossing, skill checks, full Rupert
   routing, and capacity-safe helmet/beans issue.
3. Implement the complete nine-note piano and correct compartment payload.
4. Replace zone-only mouse routing with exact placement transitions and safe
   pendant recovery.
5. Keep primary 20 while planting and use native stalk substates only.

### Phase 2 — modern owned mechanics

1. Add exact Herblore/Farming/Thieving/Agility/Woodcutting checks and rewards.
2. Implement temporary mouse-form lifecycle and every relevant follower/cat
   rule.
3. Replace the public cloud with a dynamic owned instance and verified return.
4. Implement Glod's specials, immunities/weakness, owner-private drops, and
   golden-goblin recovery.
5. Restore growth, release, shrinking, and chopping cutscenes through
   replay-safe transitions.

### Phase 3 — settlement and permanent integrations

1. Convert every item/state exchange to preflight → revalidate → consume →
   deliver → commit, with bank/ground/history-aware ownership.
2. Add protected one-time completion and canonical full-inventory deferral.
3. Implement Rupert/Perdu helmet services, quest equip gate, Falador Diary
   hook, and Nightmare Zone unlock.
4. Rewrite the journal for every native primary/support combination and add a
   read-only inconsistency report plus narrowly scoped repair tooling.

## 8. Required tests

### State and migration

- Snapshot every primary state 0–4, 10, 12, 15, 16, 17, 19, 20, 30, 40, 50,
  and 60 with all relevant support transforms.
- Migrate port-only 25 with consistent and contradictory stalk values.
- Load imported native values before and after the migration marker; prove no
  state is restarted, skipped, or presented as complete.
- Exercise both item orders, one item banked, item destroyed after issue, and
  reconnect after each Sylas yield.

### Route, skill, and puzzle

- For every skill action, test one level below, exact level, temporary boost,
  expired boost, and no accidental start-time gate.
- Test wall from both sides, both beard directions, two Agility checks, every
  Miazrqa/Rupert re-talk, and helmet full inventory/loss.
- Test every piano prefix, all wrong keys, close/reopen, ninth note, highlights,
  compartment capacity 0–4, and repeat search.
- Test all correct and incorrect nail placements, both wrong-route exits,
  logout/death/teleport in each mouse room, pendant full inventory, destruction,
  and repeat postquest entry.
- Test ordinary/Gricoller can behavior and resolve the dibber conflict with a
  captured expectation.

### Combat and instance ownership

- Run two simultaneous players: independent cloud maps, Glods, HP, specials,
  kill credit, drops, re-entry, and teardown.
- Cover follower refusal, login/reconnect, voluntary climb-down, teleport,
  death, and second-player interference.
- Force each Glod special, poison/venom, earth weakness, low-HP heal, prayer
  floor cases, forced movement at arena edges, and guaranteed/tertiary drops.
- Test Experiment melee/ranged switching, thrown roots, death roots, ordinary
  drops, and access during/after the quest.

### Transactions, completion, and consumers

- Fill the inventory for every source/exchange/reward; use surviving stacks to
  expose charge-without-delivery cases.
- Cover inventory/bank/ground/despawn/death/Destroy for all quest items and
  ensure neither duplication nor permanent loss.
- Interrupt completion before/after helmet, each XP award, state 60, QP, and
  renderer; prove exactly-once settlement.
- Verify Defence 50 plus quest completion on equip, the Dwarven Mine diary
  event once, Rupert and Perdu prices/capacity, and Glod's NMZ availability only
  after state 60.
- Prove journal and admin views for valid and contradictory states without
  silently mutating players.

## 9. Acceptance evidence

Do not mark Grim Tales modernized until one evidence bundle contains:

- pinned Wiki/Quest Helper inputs and the captured unresolved native writes;
- reviewed migration mapping and before/after save fixtures;
- clean script compile, cache/pack validation, and no duplicate trigger owners;
- deterministic unit tests for every state, skill, puzzle, transaction,
  temporary form, encounter special, drop, recovery, completion boundary, and
  downstream consumer;
- a clean-account end-to-end run without state/item injection;
- reconnect, death, teleport, full-inventory, item-loss, and two-player
  interference runs;
- journal/admin screenshots or traces for all canonical checkpoints; and
- explicit sign-off that Witch's House shared behavior, Herblore dispatch,
  generic maplinks, equipment, diaries, and Nightmare Zone did not regress.

## 10. Prioritized findings

### P0 — route, persistence, ownership, and reward integrity

1. Native primary states were redefined; 1–4 and 12/15/16/17/19 are mishandled,
   and port-only 25 needs migration.
2. Feather hand-in never sets `%grim_given_feather`, making the drain-pipe route
   deterministically unreachable.
3. Sylas ignores `%grim_given_helmet` and can issue beans without both items.
4. The wall does not cross collision and freed Rupert cannot issue the helmet.
5. The piano accepts eight notes and the canonical third maze climb is inert.
6. The pendant and piano entitlements can be permanently consumed by a full
   inventory.
7. Public static Glod ownership permits cross-player interference; the cloud
   return is broken and canonical drops do not exist.
8. Early shrink/chop interactions can irreversibly corrupt the stalk.
9. Completion can lose the reward and duplicate or partially award six XP
   grants.

### P1 — required mechanics and recovery

1. All five boostable action checks are absent, including both Agility checks.
2. Grimgnash wrong-answer damage/topology and valid endings are wrong.
3. The compartment payload, Herblore level/6 XP, and Experiment ranged root
   mechanic are missing.
4. Temporary mouse form has no login/death/teleport reconciliation.
5. Planting writes unsupported primary state 25; watering does not consume a
   charge and omits verified special-can handling.
6. Glod has none of his specials, immunities/weakness behavior, or private
   recovery ledger.
7. Every Destroy-advertised quest-item replacement path is absent or unsafe.
8. Rupert/Perdu, diary, quest equip gate, and Nightmare Zone integrations are
   absent.

### P2 — fidelity, presentation, and diagnostics

1. Release/growth/shrink/chop cutscenes and most native visual substates are
   collapsed to text/teleports.
2. Music stand, cage/gloves, gramophone, glyph, note highlights, and several
   read/item-use surfaces are inert or misassigned.
3. Journal guidance follows collapsed states and impossible flags.
4. No state audit/repair view or Grim Tales automated coverage exists.

## 11. Evidence still required before implementation

- Exact meanings/writes for primary states 1–4 and 12/15/16/17/19.
- Exact native story reset/damage behavior at every answer and interruption.
- Exact wall/beard failure animations and full five-state presentation writes.
- Current live answer to the seed-dibber/Barbarian Training conflict.
- Exact can/Gricoller charge behavior and follower/pet rules at both stalk and
  potion entry.
- Native ownership/drop persistence for Glod and golden-goblin reacquisition.
- Exact stalk completion ownership between `%grim_stalk_cut` and stalk value 4.
- Temporary mouse-form login/death/teleport recovery semantics.
- Current Rupert/Perdu transaction dialogue and integration hooks.

Until those captures exist, preserve the native fields and isolate the unknown
branches behind tests. Do not turn assumptions from the current port's long
comments into new persistent contracts.
