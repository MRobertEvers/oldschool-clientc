# Death on the Isle modernization audit

Status: `audit-pending` — the native quest dbrow, both native state carriers,
cache-authored NPC/loc transforms, journal dispatch, cheat adapter, XP values,
and shared completion call exist. The gameplay implementation is a 525-line
single-file soft-skip scaffold, however, and cannot be completed through the
canonical world route. The house window and Agility shortcuts have no trigger
owners, villa and cellar transitions do not move or hold the player, actual
Pickpocket/Attack menu operations are unbound, every investigation collapses
to one click, both fights are skipped, rewards and the statue chain are absent,
and every post-completion talk to Stradius repeats XP, quest points, completed
count, and the completion scroll.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to the native state ladder, two packed carriers,
infiltration, held inventory, disguises, guest and suspect matrices, cellar and
theatre investigations, evidence items, two bounded fights, travel, item
recovery, completion, post-quest rewards, the Pendant of ates unlock, journal,
and debug adapters. It is an implementation specification, not verification
evidence.

## 1. Authoritative references

The article and quick guide define requirements, order, investigation,
confrontations, rewards, and post-quest unlocks. The transcript defines refusal
and re-talk branches, disguise rules, security-held items, pickpocket failures,
accusation choices, fight outcomes, and repeatable reward dialogue. Revisions
were resolved through the OSRS Wiki API on 2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Death on the Isle](https://oldschool.runescape.wiki/w/Death_on_the_Isle?oldid=15241076) | 15241076, 2026-06-27 | Identity, requirements, walkthrough, fights, rewards, and post-quest chain |
| [Death on the Isle/Quick guide](https://oldschool.runescape.wiki/w/Death_on_the_Isle/Quick_guide?oldid=14976503) | 14976503, 2025-08-29 | Exact action order, evidence, travel, and optional reward claims |
| [Transcript:Death on the Isle](https://oldschool.runescape.wiki/w/Transcript%3ADeath_on_the_Isle?oldid=15246180) | 15246180, 2026-07-01 | Requirements refusal, all dialogue, failures, fights, and post-quest claims |
| [Transcript:Death on the Isle/Journal](https://oldschool.runescape.wiki/w/Transcript%3ADeath_on_the_Isle/Journal?oldid=14814976) | 14814976, 2024-12-01 | Canonical journal stages |
| [Children of the Sun](https://oldschool.runescape.wiki/w/Children_of_the_Sun?oldid=15241067) | 15241067, 2026-06-27 | Direct quest prerequisite |
| [Villa Lucens](https://oldschool.runescape.wiki/w/Villa_Lucens?oldid=15211996) | 15211996, 2026-05-17 | Staff entry, villa floors, cellar, theatre, and reward locations |
| [Patzi](https://oldschool.runescape.wiki/w/Patzi?oldid=15197246) | 15197246, 2026-04-25 | Start, infiltration brief, introductions, and post-quest dialogue |
| [Head Butler](https://oldschool.runescape.wiki/w/Head_Butler?oldid=15197262) | 15197262, 2026-04-25 | Uniform gate, held inventory, mask/tray, exit, and reward recovery |
| [Stradius](https://oldschool.runescape.wiki/w/Stradius?oldid=15197263) | 15197263, 2026-04-25 | Interrogation, investigation gates, accusations, and completion |
| [Hutza](https://oldschool.runescape.wiki/w/Hutza?oldid=15197264) | 15197264, 2026-04-25 | Alternate guard interaction and completion |
| [Adala](https://oldschool.runescape.wiki/w/Adala?oldid=15200548) | 15200548, 2026-04-28 | Evidence, accusation, level-49 bounded fight, and confession |
| [Livius](https://oldschool.runescape.wiki/w/Livius?oldid=15197309) | 15197309, 2026-04-25 | Victim discovery and corpse evidence |
| [Costumer](https://oldschool.runescape.wiki/w/Costumer?oldid=15258631) | 15258631, 2026-07-09 | Theatre questioning and costume-needle claim/recovery |
| [Naiatli](https://oldschool.runescape.wiki/w/Naiatli?oldid=15197266) | 15197266, 2026-04-25 | Final accusation and staged confrontation |
| [Clodius](https://oldschool.runescape.wiki/w/Clodius?oldid=15197267) | 15197267, 2026-04-25 | Final-fight interruption |
| [Butler's uniform](https://oldschool.runescape.wiki/w/Butler%27s_uniform?oldid=14793631) | 14793631, 2024-11-05 | Two-piece disguise and acquisition |
| [Case file](https://oldschool.runescape.wiki/w/Case_file?oldid=15074993) | 15074993, 2025-12-01 | Investigation notes and Read behavior |
| [Drinking flask](https://oldschool.runescape.wiki/w/Drinking_flask?oldid=14764548) | 14764548, 2024-10-09 | Pavo evidence |
| [Threatening note](https://oldschool.runescape.wiki/w/Threatening_note?oldid=14764707) | 14764707, 2024-10-09 | Cozyac evidence |
| [Shipping contract](https://oldschool.runescape.wiki/w/Shipping_contract?oldid=14764677) | 14764677, 2024-10-09 | Xocotla evidence |
| [Wine labels](https://oldschool.runescape.wiki/w/Wine_labels?oldid=14764724) | 14764724, 2024-10-09 | Adala evidence |
| [Costume needle](https://oldschool.runescape.wiki/w/Costume_needle?oldid=15192736) | 15192736, 2026-04-22 | Threadless needle behavior and exclusions |
| [Butler's tray](https://oldschool.runescape.wiki/w/Butler%27s_tray?oldid=15192722) | 15192722, 2026-04-22 | Repeatable post-quest cosmetic claim |
| [Pendant of ates](https://oldschool.runescape.wiki/w/Pendant_of_ates?oldid=15284980) | 15284980, 2026-08-01 | Per-destination statue unlock and charge behavior |
| [Chest key](https://oldschool.runescape.wiki/w/Chest_key_%28Fancy_chest%29?oldid=15192725) | 15192725, 2026-04-22 | Post-quest Constantinius pickpocket |
| [Icon](https://oldschool.runescape.wiki/w/Icon_%28Aldarin%29?oldid=14976510) | 14976510, 2025-08-29 | Fountain search and North Aldarin statue activation |

Transition aid only: the local Quest Helper implementation at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/deathontheisle)
maps 23 primary states, 68 coordinates, nine items, 20 NPC forms, 18 locs,
and 38 side varbits. `python3 tools/questhelper_extract.py deathontheisle
--check` exits 0. It is a routing oracle, not evidence that server triggers,
combat, held inventory, transforms, recovery, or rewards work.

## 2. Canonical contract

Death on the Isle is a members, intermediate, medium quest released 25
September 2024. It starts by speaking to Patzi beside Villa Lucens on Aldarin.
Starting requires completed Children of the Sun, 34 Thieving, and 32 Agility;
neither skill is boostable. No items are required. Combat level 40 and two free
inventory slots are recommended. The two fights use no player equipment and
cannot kill the player.

A canonical run must:

1. accept Patzi's request or preserve the refusal/re-talk path;
2. enter the northern villa through its window while the Wandering Guard is
   not watching, take exactly one two-piece uniform, leave, and report back;
3. wear both pieces, have no follower, pass the Head Butler, surrender carried
   and worn items to secure storage, and enter with an assigned mask and tray;
4. talk to Patzi, identify Constantinius, Cozyac, Pavo, and Xocotla separately,
   then report all four introductions to Patzi;
5. enter the wine cellar, investigate the antique wine, discover Livius, and
   complete the guards' interrogation to receive a readable case file;
6. independently inspect the jug, small box, broken stool, wine storage,
   broken pottery, and Livius, and independently question all five relevant
   suspect groups before reporting the first investigation;
7. attempt to pickpocket all six suspects, obtain and inspect the four evidence
   items, and hand those items to Stradius or Hutza;
8. question every gathered suspect, choose accusations, accuse Adala, and
   resolve her level-49 bounded fight whether the player wins or loses;
9. receive Adala's confession, report it, traverse two 32-Agility loose-rock
   shortcuts, and meet the guards outside the theatre;
10. question the Costumer, inspect poison, stained costume, and secret
    bookshelf passage independently, then ask the four corresponding topics;
11. report the findings, choose Naiatli from the accusation tree, enter the
    theatre, and play the scripted prop-sword sequence against Naiatli and
    Clodius through its resumable final-fight substates; and
12. report to either guard exactly once for completion, recover held items, and
    leave Villa Lucens in a coherent post-quest world state.

Completion awards 2 quest points, 10,000 Thieving XP, 7,500 Agility XP, and
5,000 Crafting XP. Post-quest, the Costumer supplies a recoverable costume
needle; the Head Butler supplies the five animal masks and repeatable butler's
trays; and a Constantinius key, fancy chest, fountain icon, and statue sequence
unlocks North Aldarin on the Pendant of ates. These are playable rewards, not
text-only completion-scroll lines.

## 3. Native identity and persistence

| Field | Native value / expected behavior |
| --- | --- |
| Quest metadata ID / packed dbrow index | 195 / 3711 |
| Dbrow | `quest_deathontheisle` |
| Type / difficulty / length | Members; intermediate; medium |
| Release | 25 September 2024 |
| Start | Patzi, native NPC 14065, native packed start coordinate 23153529 |
| Direct prerequisite | `quest_childrenofthesun` (packed dbrow 3450) |
| Stat gates | Thieving 34; Agility 32; both required to start and unboostable |
| Primary carrier | Native permanent transmitted `doti_main` bits 0–6 (`%doti`) |
| Primary side state | Remaining `doti_main` bits 7–31 |
| Secondary carrier | Native permanent `doti_secondary` bits 0–28 |
| End state | 50 |
| Rewards | 2 QP; XP values 100000/75000/50000 tenths; post-quest items/unlock |

The dbrow is unusually complete and should be authoritative. It correctly
contains membership, difficulty, length, release, start NPC/coordinate, end
state, requirements, two quest points, all XP values, and the combat
recommendation. There is no reason to duplicate those facts in authored policy
or bypass them in Patzi's start handler.

Both state carriers are native cache fields. `doti_main` stores `%doti`, four
introduction bits, three evidence-pickpocket bits, five cellar clue bits, body
inspection, four first-interview bits, the Adala pickpocket/fight fields,
initial conversation, mask assignment, and `ates_piece_hunt`. `doti_secondary`
stores item handoff, Patzi and four evidence inspections, five accusation bits,
five follow-up question bits, backstage introduction, three theatre clues,
four Costumer topics, final-accusation introduction, the three-bit final fight,
and mask dialogue. Preserve these meanings; do not replace them with a parallel
authored state machine.

### 3.1 Primary ladder

| `%doti` | Canonical checkpoint | Current result |
| ---: | --- | --- |
| 0, 2 | Start conversation / accepted continuation | Both immediately offer the abbreviated choice; acceptance writes 4 and skips state 2 |
| 4, 6 | Enter guarded house and steal uniform | Window unbound; rack gives pieces without Guard or Thieving mechanics |
| 8 | Finish Patzi's uniform dialogue | Collapsed into any 4–9 talk with inventory pieces |
| 10, 12 | Equip uniform and enter staff door | Inventory-only prior check; worn/follower/held-item/travel mechanics absent |
| 14 | Talk to Patzi inside | One line writes 15 |
| 15 | Four guest introductions | Four independent bits exist; dialogue is one line each |
| 16 | Enter cellar | Handler writes 18 without moving |
| 18 | Investigate antique wine | Any clue click sets all cellar bits and does not model the spill |
| 19 | Discover fallen man | Skipped; Livius from state 18 writes 20 |
| 20, 21 | Guard interrogation and case file | One Stradius line writes 22; no Hutza, room, or case file |
| 22, 24 | First investigation | Individual evidence is collapsed; Stradius does not validate the full matrix |
| 26 | Pickpocket and inspect evidence | Never entered; Talk-to handlers grant/inspect four items automatically |
| 27 | Hand over evidence / ready to accuse | Items are not consumed; next Stradius talk writes 28 |
| 28, 30 | Question all suspects and accuse | Four one-line bits exist but are not a gate; Adala click ends the whole round |
| 32 | Adala confession | Stradius advances without requiring the post-fight conversation |
| 33 | Report upstairs | One line writes 34 |
| 34 | Reach theatre guards via shortcuts | Same Stradius NPC writes 36; both rock shortcuts are unbound |
| 36 | Guards' backstage briefing | Entrance or Costumer jumps directly to 40 |
| 38 | Theatre investigation | Never required; any one clue sets all three |
| 40 | Report and accuse Naiatli | One Stradius line writes 42; no accusation menu |
| 42, 45 | Scripted stage fight | Talk-to Naiatli or attack Clodius writes final fight 6 and state 49 |
| 49 | Final guard conversation | Calls unguarded completion |
| 50 | Complete | Still satisfies `>= 49`, so every Stradius talk calls completion again |

### 3.2 Side-state integrity

The current debug reset clears only `%doti`, the four introduction bits,
`doti_given_items`, and `doti_final_fight`. It leaves at least 33 native quest
bits stale, including clue, body, interview, pickpocket, evidence-inspection,
accusation, question, backstage, Costumer, and mask fields. `::dirun` performs
the same partial clear and deletes only six inventory item types. A reset or
second test run can therefore inherit solved evidence and presentation state.

The generic `::complete` arm is state-idempotent but writes only `%doti = 50`.
That is insufficient for transforms, secure-held inventory, rewards, statue
state, and post-quest dialogue. Define a normalization contract for cheat
completion and a separate exhaustive development reset. Test the full 64-bit
carrier image before and after both.

## 4. Implementation and ownership surface

The quest root has three files and 525 total lines: one constant file, one
carrier declaration, and one 485-line script. It contains no raw entity IDs,
legacy `if_openmain`/`if_openoverlay`, custom panel, timer, queue, NPC addition,
loc addition, instance, or real combat controller. Modern shared journal,
choice, and completion helpers are present; almost all gameplay they surround
is explicitly deferred or labelled soft-skip.

| Surface | Current responsibility | Audit result |
| --- | --- | --- |
| `deathontheisle.constant` | Native state aliases, XP, debug coordinate | Useful but omits many valid intermediate states |
| `deathontheisle.varp` | Re-declares native `doti_main` carrier | Carrier matches cache; `doti_secondary` is consumed through native config |
| `deathontheisle.rs2` | Entire quest, journal, completion, debug run | Skeleton; critical paths collapsed or unreachable |
| quest journal dispatcher | Routes dbrow to `deathontheisle_journal` | Correctly registered |
| quest cheat | Sets state 50 once | Registered; does not normalize side state/unlocks |
| native dbrow/varbits | Metadata and 50 named varbits across two varps | Strong source of truth; most fields are ignored or written too eagerly |
| native NPC transforms | State-based forms for cast, guards, body, and spectators | Rich cache surface exists; needs placement/visibility/live-state verification |
| native loc transforms | Wardrobe, clues, bookshelf, poison, costumes, passages | Cache assets exist; generated carrier report finds zero placements for the bookshelf side varbit |
| ladder/door configs | Generic stair and interrogation-door definitions | Configured but not integrated into a proven quest route |
| Pendant of ates script | Charge/check/uncharge and six menu destinations | Every destination, including North Aldarin, always refuses |
| leather and Hunter scripts | Normal needle crafting; costume needle for Hunter pouches | Costume needle is not a general threadless substitute |
| Aldarin transport | Antonia NPCs/planks exist in cache | No server trigger owns the 20-coin Sunset Coast ferry |

World entities and models are not the blocker by themselves. The cache has
Patzi, Adala forms, Constantinius, Cozyac, Pavo, Xocotla, the Head Butler,
Stradius, Hutza, the Costumer, Livius, Naiatli, Clodius, guards, spectators,
uniform variants, tray variants, masks, prop sword, case file, four evidence
items, costume needle, key, icon, all clue locs, both cellars, the theatre, and
the shortcuts. Gate A still requires a real-client trace of every transform
and placement because source search cannot prove packed-map reachability.

## 5. Start, infiltration, and held inventory

Patzi currently checks neither Children of the Sun nor base Thieving/Agility
levels. It offers only “Yes” and “Not now,” even when requirements fail, and
has no quest summary, detailed acceptance, rejection continuation, or useful
re-talk matrix. Read gates from the dbrow or use a shared dbrow-backed start
helper. Boosts must not satisfy either stat requirement.

The canonical 20-coin ferry from the Sunset Coast is not scripted. Its Antonia
NPCs and ship planks are cache-only. CKQ and other broader travel systems may
provide alternate access, but the advertised no-prerequisite paid route must
work before the quest can be called reachable. Treat ferry ownership as an
external dependency in this quest's smoke test.

`doti_house_window` exposes Enter but has no `oploc1` owner. This makes the
wardrobe inside inaccessible through the intended route. The wardrobe ignores
the Wandering Guard, animation, window position, and Thieving context. It adds
top and bottom independently: with one free slot it grants only the top and
leaves the player in an awkward partial state. It also binds the base wardrobe
name while native `_op`/`_noop` forms exist, so the transform ownership must be
resolved rather than guessed.

Implement the window as a bounded traversal whose guard line-of-sight check,
player lock, animation, movement, cancellation, and destination work in both
directions. Uniform acquisition must be atomic: require two free slots or use
a deliberate owner-scoped fallback, grant one pair only, and provide recovery
when either piece is missing at the appropriate states. The Guard must block
entry while watching without corrupting state.

The Head Butler presently advances on talk without checking worn slots, follower,
or location. He does not move the player, issue a mask/tray, set mask assignment,
store equipment/inventory, or enforce the “uniform and mask stay worn” rule.
Quest Helper confirms the native secure-area condition is
`holding_inventory_location = 5`; no server owner writes it here or elsewhere.

Build a modern held-inventory boundary around the staff entrance and every
exit. It must atomically escrow inventory and worn items, reject/handle a
follower, equip the villa variants, persist assignment, restore everything on
exit, and recover on logout, reconnect, death, teleport, forced removal, or
server restart. Never use a quest-local destructive clear. Prove full inventory,
bank placeholders, stack metadata, charges, worn equipment, and duplicate entry
cannot lose or duplicate items.

## 6. Villa and cellar investigation

Guest introductions correctly have four independent native bits, but each
current branch is a single line. Preserve independent completion and implement
canonical identity dialogue, re-talks, and state/location restrictions. Patzi
may advance only after all four are complete. NPC transforms must expose each
guest at the correct floor and stage.

The cellar entrance and villa exit are incorrectly bound to the same handler.
At state 16 it prints an entry message and writes 18 without movement; at other
states neither direction moves. Implement both directions with the native
coordinates, staff-held inventory boundary, lock/busy behavior, and correct
state gates. Do not let a state write stand in for transport.

At state 18, the antique wine should cause the spill/noise sequence and write
19. Livius should then cause the guards' arrival, bounded scene, transfer to
the interrogation room, state 20/21 dialogue, and case-file grant. Current code
lets any clue loc mark all five clue bits before the murder is established,
then lets Livius jump directly to 20. The case file is never granted and its
Read operation has no owner.

The first investigation is a matrix, not one Boolean:

| Evidence | Native side field | Required behavior |
| --- | --- | --- |
| Jug by cellar stairs | `doti_clue1` | Inspect independently |
| Broken pottery | `doti_clue2` | Inspect independently |
| Wine storage | `doti_clue3` | Inspect independently |
| Small box | `doti_clue4` | Inspect independently |
| Broken stool | `doti_clue5` | Inspect independently |
| Livius | `doti_bodycheck` | Inspect independently after discovery |
| Constantinius | `doti_investigated_constantinius` | Question independently |
| Cozyac | `doti_investigated_cozyac` | Question independently |
| Pavo | `doti_investigated_pavo` | Question independently |
| Xocotla | `doti_investigated_xocotla` | Question independently |
| Patzi and Adala | `doti_investigated_patzi` | Joint conversation independently |

Every loc/NPC must write only its own field after its dialogue/action succeeds.
Stradius or Hutza may advance to the pickpocket phase only when the whole
matrix is true. The journal and case file should derive their partial checklist
from these same fields. The generated loc-carrier report says the
`doti_bookshelf_clue` carrier has zero placements; validate the packed
bookshelf transforms separately before relying on generic synthesis.

## 7. Pickpocket evidence and accusation

The cache exposes Pickpocket as `opnpc3` on the six pickpocket forms. The quest
binds no `opnpc3` at all. Instead, `opnpc1` Talk-to on Cozyac, Pavo, Xocotla,
and Adala instantly sets both interview and pickpocket fields, tries to add the
item, and marks it inspected even when the add fails for lack of space. Patzi
and Constantinius pickpocket forms have no matching handler. This is both an
operation-wiring defect and a state-before-resource defect.

Implement a shared quest pickpocket attempt with the canonical failure text,
animation/stun policy resolved from cache, 34-Thieving assumption already
enforced at start, and per-target outcomes. Patzi and Constantinius yield no
evidence but still participate in the canonical sweep. The four evidence
owners yield exactly one recoverable item each:

| Target | Item | Acquisition field | Inspection field |
| --- | --- | --- | --- |
| Adala | Wine labels | `doti_pickpocket_adala` | `doti_investigated_labels` |
| Cozyac | Threatening note | `doti_pickpocket_cozyac` | `doti_investigated_letter` |
| Pavo | Drinking flask | `doti_pickpocket_pavo` | `doti_investigated_flask` |
| Xocotla | Shipping contract | `doti_pickpocket_xocotla` | `doti_investigated_contract` |

Grant success and acquisition state atomically. A full inventory must not mark
an absent item as acquired. Each item's Inspect operation must independently
write its inspection field. Define lost/destroyed replacement before handoff;
banked ownership must neither duplicate the item nor deadlock a secured quest
inventory. Stradius/Hutza must validate all four present and inspected, consume
all four in one transaction, then write `doti_given_items` and state 27.
Current code checks presence only, consumes none, and state 27 later sets the
handoff bit even without items.

The top-floor accusation round needs the five native questioned bits and five
accused bits, not the current four soft-skip lines. Preserve valid wrong
accusations and “No” continuations. Adala becomes accus-able only after the
required questioning flow. Her boss form exposes Attack as `opnpc2`, while the
quest binds only `opnpc1`; current talk jumps to state 32 without combat.

Implement Adala as a bounded, equipment-free, unlosable encounter. The level-49
form has native combat stats; stop the fight near defeat on either side and
select the matching transcript outcome. Protect spectators, NPC ownership,
multi-player isolation, target cleanup, logout, disconnect, death, and re-entry.
Only the completed outcome should expose post-fight Adala and state 32; her
confession must then be a separate successful conversation.

## 8. Theatre route, investigation, and final fight

Both native loose-rock shortcuts expose Navigate and have no RuneScript owner.
State 34 can nevertheless become 36 merely by talking to the villa Stradius.
Implement both level-32 Agility traversals with correct endpoints, animations,
locks, failure policy, and bidirectional/re-entry behavior. The theatre Stradius
transform, not an arbitrary same-name NPC, owns the backstage briefing.

The backstage entrance currently sets all three clue fields and state 40 on
entry without moving. Talking to the Costumer sets all four topic fields and
state 40. Clicking any one of the poison crate, bookshelf, or costume rack sets
all three clue fields. State 38 is never a meaningful checkpoint. Replace this
with the native sequence:

1. enter the theatre cellar and talk to the Costumer about the actors;
2. independently search poison crate, bookshelf passage, and stained costume;
3. independently ask about actors, poison, stained costume, and hidden passage;
4. leave by the stairs and report only when every required field is set.

Each transform must remain per-player through `doti_secondary`. Search only
the clicked loc, update its corresponding visual, and preserve re-talk text.
The secret bookshelf passage should connect the two cellars as the cache
intends; validate both sides, collision, and post-search transform placement.

The guard accusation at state 40 needs the transcript's multi-page suspect menu
and `doti_final_accusation_intro`. Only Naiatli advances. The final stage is a
scripted fight, not normal lethal combat: talk starts it, attacks advance
Naiatli, she runs, Clodius interrupts, Clodius is struck, Naiatli is struck in
later positions, and the confession follows. Quest Helper interprets
`doti_final_fight = 2` as the trap, values at least 3 as the post-trap sequence,
and values at least 6 as Naiatli downed.

Naiatli exposes Talk-to op 1 and Attack op 3; only Talk-to is bound. Clodius
exposes Attack op 1, but that one click currently marks the entire fight done.
Build a resumable per-player controller around the three-bit field. It must
validate target, phase, stage position, prop-sword presentation, exact bounded
damage, actor movement, spectator/world isolation, repeated clicks, logout,
disconnect, and re-entry. No normal death or item risk is allowed.

## 9. Completion, rewards, and downstream unlocks

`di_quest_complete` writes state 50, awards all three XP grants, and calls the
shared completion helper. It has no internal guard. Stradius dispatches it for
every `%doti >= 49`, including 50. Every post-quest Talk-to therefore repeats
10,000 Thieving XP, 7,500 Agility XP, 5,000 Crafting XP, 2 quest points,
`quests_completed_count`, and the completion scroll. This is a critical
economy/account-integrity exploit.

Make completion a single idempotent transaction. The procedure itself must
return immediately at state 50, not depend on its caller. Validate final-fight
and final-report state, restore secured items, clean temporary villa equipment,
commit the permanent state once, award XP/QP/count once, and mount the scroll
once. Repeated guard ops, duplicate queues, reconnect, and `::complete` twice
must be no-ops after the first commit. Either Hutza or Stradius should own the
canonical finish path.

The reward scroll presently advertises three rewards that do not exist as
playable systems:

- The Costumer never grants or replaces `costumeneedle`. Its only implemented
  use is Hunter pouch crafting. Standard leather, hard leather, dragonhide,
  snakeskin, fabric, and other needle recipes still accept only `needle` and
  often require thread. Add the costume needle to all canonical recipes except
  the Wiki-documented exclusions, without colliding duplicate `opheldu`
  dispatchers, and suppress thread consumption.
- The Head Butler never grants `mysterymask01_reward` through
  `mysterymask05_reward` or either reward tray. Canonical dialogue permits each
  mask and repeated tray claims. Use explicit inventory capacity/fallback and
  recovery policy; do not confuse villa-only disguise variants with rewards.
- The key/chest/fountain/statue chain has no handlers. `ates_piece_hunt` exists
  in unused bits 30–31 of `doti_main`, the fountain has native transforms, and
  key/icon objs exist. Implement no-key pickpocket, chest consumption/open,
  fountain search, icon use-on-statue, one-water-rune drop, and persistent
  unlock with loss/replacement and duplicate-action safety.

The Pendant of ates implementation explicitly refuses all six destinations.
North Aldarin must check the statue-unlock bit and teleport to a cache/live-
verified coordinate while consuming exactly one charge only on success. Keep
locked attempts and failed teleports charge-neutral. This quest cannot be
marked verified while its headline unlock always says “You haven't unlocked
this destination.”

## 10. Journal and recovery contract

The journal currently has six vague state ranges. It ignores every side bit,
whether the uniform is worn/held, exact introductions, the 11-part first
investigation, four evidence items and inspections, suspect questioning,
theatre clues/topics, final-fight phase, and post-quest reward chain. It can
claim “accuse the right suspect” before questioning anyone and “confront
Naiatli” after one backstage click.

Render the canonical journal transcript through `~quest_journal`, but derive
current todo/done lines from primary and side state plus required item
ownership. Recovery guidance must cover missing uniform pieces, secured-item
restoration, missing case file, four missing evidence items, interrupted Adala
fight, leaving either cellar, interrupted final fight, costume needle, masks,
tray, key, and icon. State alone must never claim possession of an item that a
failed `inv_add` did not deliver.

An explicit lifecycle test must exercise:

- rejection and re-talk at the start;
- requirements failing individually and boosts not satisfying them;
- Guard-blocked window entry and partial/full inventory uniform attempts;
- missing uniform, not-worn uniform, follower, secure entry/exit, relog, death,
  teleport, and restoration of inventory/worn metadata;
- every clue/interview in different orders and early guard reports;
- every pickpocket failure, full inventory, item loss, banked copy, inspection,
  and atomic handoff;
- every wrong accusation, both Adala fight outcomes, and interruption/re-entry;
- every theatre clue/topic order and bookshelf transform;
- every final-fight substate, invalid target, relog, and repeated attack;
- completion double-click/reconnect/re-talk and exact XP/QP/count deltas; and
- every post-quest item recovery plus locked/unlocked Pendant teleport charges.

## 11. Modernization work order

1. Preserve the native dbrow and two carriers; add named constants for every
   actual primary state and central predicates for each investigation matrix.
2. Add dbrow-backed start gates and full Patzi accept/refuse/re-talk dialogue.
3. Restore Aldarin ferry ownership, window/Guard traversal, atomic uniform
   acquisition, and recovery.
4. Implement the Head Butler's equipped disguise, follower, held-inventory,
   villa entry/exit, mask/tray, and crash-safe restoration controller.
5. Restore introductions, cellar transport, wine/body scene, guard
   interrogation, case file, and all independent first-investigation fields.
6. Bind actual `opnpc3` pickpockets and item Inspect operations; make evidence
   grants/recovery/handoff atomic.
7. Implement full suspect dialogue, accusation fields, Adala's `opnpc2` fight,
   both outcomes, and confession.
8. Implement both Agility shortcuts, theatre guards, cellar entry, three clue
   transforms, four Costumer topics, and bookshelf passage.
9. Implement the accusation menu and resumable Naiatli/Clodius stage controller.
10. Make completion internally idempotent and restore/clean all temporary state
    before the exactly-once shared reward call.
11. Implement costume needle recipes/recovery, masks/trays, key/chest/fountain/
    statue chain, and the North Aldarin Pendant destination.
12. Replace the journal and both debug adapters with exhaustive native-state
    behavior, then satisfy all Gate D tests and live-client captures.

Keep gameplay policy in RuneScript/config data. If secure held-inventory or
per-player staged actors reveal a genuinely missing general engine capability,
land that capability independently with generic tests before resuming the
quest; do not add quest-specific C routing.

## 12. Gate verdict and verification matrix

| Gate | Verdict | Blocking evidence |
| --- | --- | --- |
| A — discover | Fail | Packed placements/transforms, held inventory, travel, and external reward owners are not proven |
| B — modern machinery | Partial | Modern journal/choice/completion helpers exist; traversal, combat, persistence, and actor ownership machinery is absent rather than legacy |
| C — gameplay fidelity | Fail | Requirements, route, investigations, operations, fights, recovery, completion idempotence, and rewards are critically wrong or missing |
| D — verification | Fail | No organic end-to-end path or invariant suite; only a soft-skip `::dirun` exists |

Required Gate D evidence before `verified-modern`:

| Verification | Required proof |
| --- | --- |
| Static ownership audit | One owner for every NPC/loc/item operation, no unresolved names, duplicates, raw IDs, deferred/soft-skip markers, or reward text without behavior |
| Quest Helper check | `python3 tools/questhelper_extract.py deathontheisle --check` exits 0 (baseline already observed) |
| Compile/pack | `make -C src torirsserver-scripts` and `ToriRSServer_Pack --check-only` pass against the intended cache |
| State tests | Every primary state plus all 50 named native varbits, arbitrary clue orders, relog/reconnect, and reset normalization |
| Inventory tests | Uniform pair, secure escrow, case file, evidence, handoff, reward claims, destruction/recovery, full inventory, and banked duplicates |
| Combat tests | Both Adala outcomes and every Naiatli/Clodius phase, including interruption and invalid action |
| Completion tests | Exact XP/QP/count delta once; repeat Stradius/Hutza and `::complete` calls change nothing |
| Unlock tests | Costume needle canonical recipes, masks/tray recovery, key/icon chain, and successful charge-consuming North Aldarin teleport |
| Real-client smoke | Patzi through reward scroll and all three post-quest reward paths, with captures for held inventory, transforms, both fights, and completion |

Until that evidence exists, retain `audit-pending`. The current implementation
is useful only as a cache/state reconnaissance scaffold and must not be treated
as a playable quest.
