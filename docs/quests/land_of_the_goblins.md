# Land of the Goblins modernization audit

Status: `partial / blocked organic route` — the cache contains the complete
quest row, permanent varbits, actors, follower variants, goblin appearance
fields, temple and crypt scenery, both native interfaces, Yu'biusk terrain and
reward metadata. The server root is a 986-line compatibility slice. It cannot
be started organically because no required LOTG actor is world-spawned and the
script does not create Grubfoot. Even after fixture state is supplied, the
Goblin Cave staircase has no LOTG maplink and falls back to a same-tile plane
change instead of reaching the remote temple. Most remaining authored content
is represented by dialogue and state writes rather than follower movement,
transmogrification, guarded traversal, quiz choices, owned combat, interfaces,
cutscenes, or post-quest unlocks.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A-D to prerequisites, Grubfoot and Zanik ownership,
Dorgesh-Kaan travel, goblin transformation, temple infiltration, Zanik's
escape, High Priest Bighead's questions, the six keys, Hemenster whitefish,
Aggie's service, crypt combat, Oldak's machine, Yu'biusk, completion, recovery,
journal/admin behavior, and downstream consumers. It is an implementation
specification, not proof that the quest works.

## 1. Authoritative references

The OSRS Wiki was requested as the canonical gameplay authority. Direct Wiki
API/page access was blocked by robots during this audit. Search-indexed pages
were reviewed where available; the main article, quick guide and transcript
remain explicit live locators and must be revision-pinned immediately before
implementation. Quest Helper is transition evidence only and never overrides
the Wiki or rev-239 cache.

| Reference | Revision / status | Audit use |
| --- | --- | --- |
| [Land of the Goblins article](https://oldschool.runescape.wiki/w/Land_of_the_Goblins) | live locator; pin before coding | Identity, requirements, route, items, fights, rewards and unlocks |
| [Quick guide](https://oldschool.runescape.wiki/w/Land_of_the_Goblins/Quick_guide) | live locator; pin before coding | Ordering, travel, inventory, disguise, keys and recovery checkpoints |
| [Quest transcript](https://oldschool.runescape.wiki/w/Transcript%3ALand_of_the_Goblins) | live locator; pin before coding | Offer/refusal, dreams, quiz answers, re-asks, wrong answers, loss and post-quest dialogue |
| [Goblin mail](https://oldschool.runescape.wiki/w/Goblin_mail) | current search-index snapshot reviewed | Human/goblin equip behavior, recolours and infinite temple crate |
| [Potions](https://oldschool.runescape.wiki/w/Potions) | current search-index snapshot reviewed | Level 47 recipe, 55 Herblore XP, normal dose creation, cave/temple-only use and post-quest making |
| [Dye](https://oldschool.runescape.wiki/w/Dye?oldid=14854367) | oldid 14854367 | Required yellow/blue/orange/purple/black dyes and shared dye ownership |
| [Grave](https://oldschool.runescape.wiki/w/Grave_%28Land_of_the_Goblins%29?oldid=15013430) | oldid 15013430 | Priest order, crypt purpose and Hopespear's Will reuse |
| [Machine](https://oldschool.runescape.wiki/w/Machine_%28Land_of_the_Goblins%29?oldid=14825245) | oldid 14825245 | Oldak machine identity and Yu'biusk travel |
| [Goblin Temple altar](https://oldschool.runescape.wiki/w/Altar_%28Goblin_Temple%29?oldid=15012501) | oldid 15012501, search-index snapshot | Initial no-response behavior and later Prayer restoration |
| [Plain of mud sphere](https://oldschool.runescape.wiki/w/Plain_of_mud_sphere) | live locator; pin before coding | Oldak purchase, components, Break behavior and Goblin Cave destination |
| [Hopespear's Will](https://oldschool.runescape.wiki/w/Hopespear%27s_Will) | live locator; pin before coding | Post-quest crypt, goblin-potion and combat consumer |

Transition aid: local Quest Helper commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/landofthegoblins)
maps states 0 through 52, all six key/mail routes, interface 739 component 31,
interface 738 components 21-26 and 39, the five combat styles and every major
zone. Its quest path last changed in `8e5bb8a` on 2026-03-05.
`python3 tools/questhelper_extract.py landofthegoblins --check` does not exit
cleanly: every gameval resolves except
`NpcID._0_41_53_sinisterfishspot`. The actual symbolic NPC is already used by
Fishing Contest, so this is an extractor/alias normalization gap to fix and
then re-run, not evidence that the fishing spot is absent.

## 2. Canonical contract

Land of the Goblins is a members, experienced, medium quest, Dorgeshuun-series
entry 4, released on 9 February 2022. It starts with Grubfoot in the Dorgeshuun
Mines. Another Slice of H.A.M. and Fishing Contest must be complete. Base
Agility 38, Fishing 40, Thieving 45 and Herblore 48 are non-boostable start
requirements. A light source, no follower and enough room to unequip are route
requirements; the core inventory includes an unfinished toadflax potion,
goblin mail, yellow/blue/orange/purple dyes, vial, pestle and mortar, fishing
rod, raw slimy eel, coins and combat equipment.

The canonical route is:

1. accept Grubfoot's request, escort him through the mines to Dorgesh-Kaan,
   involve Mistag and the council, then meet Zanik in Oldak's lab;
2. watch the dream discussion, speak to Zanik again and travel with her to the
   Goblin Cave using the Dorgesh-Kaan sphere sequence;
3. meet Zanik in the cave, learn that the guard admits only goblins, consult
   the Makeover Mage, pick pharmakos berries and brew a Goblin potion;
4. return to the cave with no equipment worn, drink the potion only in an
   allowed zone, use interface 739 to select a goblin appearance/name, and
   physically pass the guard and staircase into the Goblin Temple;
5. make black dye, obtain the infinitely replaceable temple goblin mail, dye
   and wear it, pass the black guard, obtain a Dorgesh-Kaan sphere and use it
   to free Zanik from the cell;
6. leave the room and complete High Priest Bighead's full belief conversation,
   including wrong-answer feedback/retry and the Chosen Commander/Yu'biusk
   branches, to learn about the six enclave keys and crypt;
7. pickpocket the black/Huzamogaarb priest, ask Aggie about white dye, use a raw
   slimy eel as bait at the Fishing Contest spot to catch a whitefish, and have
   Aggie remove the black dye from the mail; players bring the four ordinary
   dyes rather than receiving them as an invented bundle;
8. wear white, yellow, blue, orange and purple mail in sequence, enter each
   enclave, pickpocket its priest, then use all six tribe keys to unlock the
   crypt; the unlocked entrance permits leaving to collect combat gear;
9. call and defeat Snothead (32, Melee), Snailfeet (56, Melee/Ranged), Mosschin
   (88, Melee/Magic), Redeyes (121, Melee/Magic plus Attack/Strength/Defence
   drain) and Strongbones (184, all styles, the same drains and level-29
   Skoblin summons), learning each next name from the defeated priest;
10. return to Oldak, traverse the southern Dorgesh-Kaan caves, operate native
    interface 738 until its connectors read 9/4/1, and complete the staged
    portal/cutscene trip to an owned Yu'biusk instance; and
11. approach the strange box to complete for two quest points, 8,000 XP each
    in Agility, Fishing, Thieving and Herblore, Goblin Temple access, fairy
    ring BLQ access to Yu'biusk, the ability to buy plain of mud spheres, and
    the ability to make Goblin potions after the quest.

The Goblin potion recipe itself uses Herblore 47 and awards 55 XP, while the
quest start requires 48. A normally finished potion has three doses. It may be
used only in the Goblin Cave and Goblin Temple. Leaving the allowed disguise
area must restore the player and reconcile worn goblin mail; the current guide
warns that teleporting with a full inventory while mail is worn destroys it.
Pin the current item/transcript revisions before encoding the exact warning,
destroy, vial-return and replacement wording.

## 3. Native identity, ownership and assets

| Field | Native value |
| --- | --- |
| Cache quest / dbrow | id 166 / `quest_landofthegoblins` |
| Root | `quest_landofthegoblins`, 9 files, 986 lines |
| Release / series | 9 February 2022 / Dorgeshuun #4 |
| Classification | members, experienced, medium |
| Start / end | `lotg_grubfoot` / state 56 |
| Quest points | 2 |
| Reward XP | four values of 80,000 internal tenths = 8,000 each |
| Latest audited content commit | `12a2ef4cf41313648eb58ce02e97030037a79df8`, 2026-08-17 |

The root owns the two permanent base varps, state constants, cave/temple,
key, crypt, Oldak/Yu'biusk, journal and completion handlers. Shared trigger
ownership must stay centralized:

- `areas/draynor/scripts/aggie.rs2` dispatches Aggie's LOTG branch;
- `areas/falador/scripts/makeover_mage.rs2` dispatches the Makeover Mage branch;
- `skill_herblore/scripts/brew_potion.rs2` owns unfinished-potion use routing;
- `quest_golem/scripts/golem_portal.rs2` owns black mushroom grinding/ink;
- `quest_gobdip` and `skill_crafting/scripts/dye_cape.rs2` own goblin-mail dyeing;
- `quest_fishingcompo` owns the Hemenster gate and fishing spot;
- `quest_mcannon` owns the Goblin Cave entrance;
- `quest_losttribe` owns the Lumbridge cellar, mine route and Dorgesh-Kaan
  geography; and
- POH journal, quest-status and fairy-ring adapters consume the main state.

Native varbits already cover much more than `%lotg`: Grubfoot-at-entrance,
Zanik-in-lab, fish knowledge, sphere discovery, black-dye knowledge, all three
relay connectors, crypt presence, Yu'biusk warning/return/sarcophagus state,
Zanik outside the temple, goblin transformation, fairy-ring animation, machine
explanation, goblin name halves, goblin type and goblin colour. Modernization
should make these the authoritative phase/display/recovery state instead of
adding replacements or overloading the main quest state.

Interfaces 739 and 738 are present. The cache also has Grubfoot and Zanik
follower variants, every guard/priest/boss/defeated actor, six keys, all mail
colours, one- through four-dose potion objects, pharmakos berries, whitefish,
the machine, graves, huge crypt door, Yu'biusk scenery and plain-of-mud sphere.

The dbrow's `requirement_quests` values 1 and 52 resolve to Cook's Assistant and
Mage Arena I. They conflict with the Wiki, Quest Helper and runtime comment.
Correct the cache metadata deliberately; do not preserve those IDs as a second
source of truth.

## 4. Placement and traversal blockers

There are no `lotg_*` actor entries in any generated world `.spawn` file, nor
an Oldak placement for the two variants used here. Despite a source comment
claiming the slice hand-spawns every named quest NPC, `~lotg_spawn_if_absent`
is called only for the five crypt bosses and their defeated forms. Grubfoot,
Zanik, their follower variants, both cave guards, all temple guards/priests,
High Priest Bighead and both Oldaks are absent. Their handlers are therefore
unreachable; a normal player cannot start the quest.

Build a player-phase matrix from native state before adding actors. Persistent
public actors should use generated world spawns/morphs. Escorts and scene-only
actors should be player-owned followers or bounded scene actors. Do not use a
radius-global `npc_find` to stand in for ownership, and never delete a shared
actor to phase one player.

The `lotg_goblin_staircase` has only `category=climb_down`. No source row for
its player tile `0_40_153_21_61` exists in `maplink.dbrow`. The generic climb
code therefore falls back to a same-tile one-plane move, while the temple is in
the remote `0_58_67` region. Add a cache-verified bidirectional maplink and
test both sides; the temple is otherwise unreachable even with actors fixed.

`cave_goblin_city_doorr` is claimed by the quest and hard-teleports one way to
`0_42_83_16_53`. It does not model the physical doorway, return route or
companion crossing. Move destination ownership into verified maplinks/door
services and regression-test The Lost Tribe and Another Slice of H.A.M.

## 5. Progress-state audit

| State | Canonical checkpoint | Current writer / defect | Required invariant |
| ---: | --- | --- | --- |
| 0 | not started | default; Grubfoot absent | Start actor exists; refusal/re-ask safe; requirements use base stats |
| 2 | Grubfoot recruited | dialogue writes immediately | Owned Grubfoot follower and route phase exist before commit |
| 4 | dream scene watched | first Zanik chat, no scene | Scene receipt owns actors/camera and resumes safely |
| 6 | talk to Zanik | second compressed chat | Transcript branch and actor/zone validated |
| 8 | ready to travel | third chat | Zanik follower/Oldak sphere setup exists |
| 10 | arrived at Goblin Cave | fourth lab chat writes without travel | Sphere scene places player and Zanik; cave re-entry works |
| 12 | guard/Makeover Mage lead | cave Zanik chat | Companion and guarded movement, not only dialogue |
| 14 | ingredients explained | guard chat, not Mage | Makeover Mage owns the correct checkpoint and re-asks |
| 16 | Goblin potion ready / transform phase | mixing writes state before drinking | Three-dose potion and XP settle first; drinking owns transform |
| 18 | entering temple | black-mail dyeing incorrectly writes it | Stair/guard traversal and goblin phase own state |
| 20 | inside temple | never written | Preserve native checkpoint and re-entry recovery |
| 22 | black mail obtained | black guard writes it | Crate/mail ownership and disguise validated before commit |
| 24 | sphere found | crate adds item without space check | Add/ownership succeeds before state; native sphere bit agrees |
| 26 | Zanik freed | chat does not consume/use sphere or move Zanik | Escape scene/item settlement and personal phase complete |
| 28 | left north-east room | never written | Guard/room-exit service owns it |
| 30 | belief discussion | skipped | Full quiz stage and wrong-answer retry |
| 32 | Yu'biusk/crypt discussion | skipped | Full transcript branch before key phase |
| 34 | collecting six keys | High Priest jumps 26 -> 34 | All prior stages reachable; item/bank/loss predicates drive substeps |
| 36 | crypt unlocked | first door click; keys retained; no entry | One unlock settlement, confirm entry, leave/re-enter support |
| 38 | Snothead defeated/name learned | defeated Snothead chat | Player-owned kill receipt and next-name knowledge agree |
| 40 | Snailfeet defeated/name learned | defeated Snailfeet chat | Same |
| 42 | Mosschin defeated/name learned | defeated Mosschin chat | Same |
| 44 | Redeyes defeated/name learned | defeated Redeyes chat | Same |
| 46 | Strongbones fight/name phase | defeated Strongbones chat writes it | Exact combat/Skoblins and Yu'biusk dialogue settle once |
| 48 | see Oldak | lab Oldak chat | Correct Oldak actor/zone and machine route |
| 50 | machine fixed | one loc click writes 9/4/1 | Interface validates player input and scene readiness |
| 52 | Yu'biusk finale | machine Oldak teleports to shared map | Owned instance/cutscene is complete and recoverable |
| 56 | complete | sarcophagus calls non-transactional finish | Exact rewards/unlocks settle once before permanent completion receipt |

Every transition needs one owner, actor/zone/session preconditions, replay-safe
item/value settlement and login/logout/death/teleport recovery. Existing saves
that skipped 18/20/28/30/32 or contain an out-of-zone permanent goblin flag
need an explicit migration/reconciliation table.

## 6. Requirements, start and companions

`~lotg_meets_prereqs` correctly names the two quest prerequisites but uses
`stat()` for all four skills. That admits temporary boosts even though the
requirements are non-boostable. Use base levels and make the journal/cache
metadata agree.

Grubfoot's current exchange is a two-option chat that writes state 2. It omits
the standard quest confirmation summary, Mistag/council responsibility,
follower creation, follow-distance recovery, light-source route context and
no-follower gate. The source dismisses followers because a generic pet system
is absent, but the cache contains exact Grubfoot/Zanik follower NPCs and native
phase fields. Treat escorted companions as quest-owned NPC sessions using the
modern actor/follow lifecycle, not pets and not instantaneous flags.

Four repeat Zanik conversations substitute for a dream cutscene and travel.
They validate neither location nor which Zanik appearance is active. Build a
resumable scene, phase the lab/cave/cell variants per player, and implement the
sphere teleport/follow sequence. Login or region change must restore the
appropriate static/follower actor without duplicating it or letting another
player's companion advance the quest.

## 7. Goblin potion and transformation

The potion pipeline currently deletes one unfinished toadflax potion and one
berry, adds a one-dose potion, gives no Herblore XP, and commits state 16 even
though it has not been drunk. It has no full-inventory transaction safety.
Integrate it into the shared Herblore service: validate level/quest checkpoint,
award 55 XP, produce the canonical normal-dose object, and commit only after
the object transfer succeeds. Preserve post-quest ability to make it.

All four drink handlers delete the entire selected potion. They do not reduce
4 -> 3 -> 2 -> 1 doses, return an empty vial, check an allowed zone, open
interface 739, change the avatar, record name/type/colour, restrict equipment,
or restore the human form. `%lotg_player_is_a_goblin` can remain permanently
set anywhere in the world. Implement transformation as one service that owns:

- cave/temple zone validation and no-equipped-item/no-follower checks;
- dose decrement and vial behavior through the standard potion contract;
- interface 739 mount, all selectable cosmetic values, confirm/cancel and
  remount after interruption;
- appearance/movement/equipment rules while transformed;
- guard and goblin-mail equip eligibility using the same predicate; and
- region exit, teleport, death, logout/reconnect and post-quest reversion,
  including the pinned full-inventory worn-mail behavior.

## 8. Temple, mail, Zanik and High Priest Bighead

The cave and enclave guards only talk. They do not walk the player past their
collision, synchronize a follower or prevent bypass. Build guarded traversal
services with exact source/destination tiles and disguise/mail predicates.

The armour crate calls `inv_add` without checking space and treats any
inventory mail as ownership; it ignores worn and banked quest mail. The shared
`~lotg_any_goblin_mail` also searches inventory only. Define a single ownership
domain for all seven colours across inventory, worn and bank, plus canonical
crate replacement behavior. Dyeing currently deletes dye and mail before an
unchecked add. Make every recolour atomic and regression-test Goblin Diplomacy,
cape dyeing and black ink because those global dispatchers were modified for
this quest.

The sphere crate similarly lacks space/recovery checks and ignores the native
`lotg_found_sphere` bit. Talking to Zanik merely tests possession and writes 26;
it neither uses the sphere nor runs an escape scene or phases Zanik out of the
cell. The scene must own sphere semantics, Zanik placement and room-exit state
28. Determine from the pinned transcript whether the sphere is consumed,
retained or supplied by Zanik before coding.

High Priest Bighead currently accepts even unworn inventory mail and bakes
three answers into an uninterruptible script, then jumps directly from 26 to
34. Implement the actual player choices, wrong feedback/retry, belief
follow-ups, Chosen Commander answer, Yu'biusk questions and crypt permission
across states 28/30/32. Appearance, worn-mail and room preconditions must be
checked server-side on every entry/re-entry.

## 9. Whitefish, Aggie, dyes and keys

Pickpocketing always succeeds after a current-stat check. It has no skill roll,
failure animation, stun/damage, NPC reaction or bank-aware duplicate check.
Use the shared Thieving attempt machinery with the canonical failure contract.
The start already guarantees base 45; do not turn every attempt into a second
boost-sensitive gate.

The Hemenster branch waits four ticks, consumes the eel and adds whitefish with
no space check, Fishing roll, XP, spot lifecycle or interruption handling. It
must merge with the existing Fishing Contest spot without stealing that
quest's interaction. Verify the current Fishing XP and catch/failure rate from
the pinned item/guide page; handle rod/eel loss, full inventory, movement and
spot changes.

Aggie's implementation invents an all-in-one transaction: it takes whitefish,
black mail and five coins, then grants white mail plus yellow, blue, orange and
purple dye. The guide/helper instead requires the player to bring those four
ordinary dyes; Aggie's LOTG service uses the whitefish to remove black dye.
It also sets fish knowledge before the explanatory dialogue completes. Rebuild
the exact transcript choices and atomically exchange only the canonical inputs
and output. Bank/lost-mail branches must not trap state 34.

All six key duplicate tests are inventory-only. Define canonical bank,
destroy/drop and priest-replacement behavior from freshly pinned key pages.
The huge door currently retains the keys, writes state 36 on the first click
and teleports only on the second. Pin whether keys are consumed, then make
unlocking one atomic action followed by the canonical enter confirmation while
retaining the ability to leave, gear up and re-enter.

## 10. Crypt and combat ownership

Each grave uses a radius-global `npc_find` and spawns a public NPC for 1,000
ticks. Death handlers create a public defeated husk without checking killer or
session. One player can see, fight, kill or talk to another player's priest and
receive state credit. Use a per-player combat instance or owner-visible NPCs,
record the kill before exposing its defeated dialogue, and clean up safely on
death, teleport, logout and reconnect.

All five enemies are delegated to generic combat. This omits Snailfeet's and
Mosschin's mixed styles, Redeyes' stat drains, and Strongbones' three styles,
drains and Skoblin summons. Encode the five encounters as data over a shared
owned-boss service, but retain exact NPC stats/animations/projectiles and each
failure/re-entry rule. Do not let a defeated-husk timeout become a deadlock;
main state plus the native crypt/name fields must recreate the correct phase.

Crypt entry also needs to reconcile goblin disguise/mail with combat equipment,
set `lotg_in_crypt`, prevent public cross-credit, and support the later
Hopespear's Will variant without merging that miniquest's weapon/armour/magic
restrictions into the main-quest fights.

## 11. Oldak, machine and Yu'biusk

Neither Oldak variant is placed. The source also assumes all Dorgesh-Kaan
stairs are correct without testing the full route. Verify each generic maplink
against the Quest Helper coordinates and AJQ alternative before claiming the
machine reachable.

Clicking the machine currently writes connector values 9/4/1 and state 50 in
one action. Interface 738 already exists and Quest Helper maps increment and
decrement buttons 21-26 plus confirm 39. Mount it through the modern subinterface
path, initialize from native connector varbits, arm/re-arm every server button,
enforce ranges, reject wrong settings, animate only the correct solution and
recover safely from close/logout/reopen.

Oldak then teleports the player to a shared static coordinate and state 52.
Quest Helper explicitly models Yu'biusk as an instance. Build a bounded
Oldak/Zanik portal cutscene and player-owned Yu'biusk session using native
warning, returned and sarcophagus fields. Re-entry after an interrupted scene,
leaving Yu'biusk, fairy-ring BLQ and the strange-box approach must all converge
on one recoverable state. Completion should be scene-owned, not an arbitrary
repeatable object click.

## 12. Completion and downstream unlocks

`~lotg_finish` writes state 56 first, advances all four skills, then invokes
the shared completion scroll. There is no settlement receipt or rollback
boundary. A repeat/resume at the wrong point can make permanent state disagree
with XP, quest points and unlocks. Settle the four 8,000-XP awards and two quest
points once through an idempotent completion transaction, then commit the
completion receipt/state and render the scroll from recorded results.

The scroll advertises altar and BLQ access but omits the plain-of-mud sphere
and post-quest potion-making unlocks. More importantly, most unlocks do not
exist:

- `lotg_teleport_artifact` has no Break handler and Oldak has no plain-of-mud
  sphere purchase/exchange; the indexed Wiki materials are two law runes and
  one molten glass, subject to confirmation on the pinned item page;
- the Goblin Temple altar has no server handler, despite the cache category and
  Wiki's quest/post-quest behavior;
- only the POH fairy-ring service recognizes BLQ; no verified world fairy-ring
  route to Yu'biusk was found; and
- Hopespear's Will and The Chosen Commander are not implemented, though both
  are future state/item/area consumers and the former revisits all five graves.

Create one post-quest unlock service consumed by potion brewing, Oldak's shop,
plain-of-mud sphere Break, temple altar, world/POH fairy rings and future
quests. The existing POH quest-status adapter correctly reports 0/in-progress/
complete and should remain a read-only consumer.

## 13. Journal, recovery and admin

The journal groups broad ranges and hides the missing native states. It says
the Makeover Mage will make the player a goblin, conflates brewing and
drinking, reports Zanik freed without checking her phase, and cannot direct
recovery for missing potion/mail/sphere/keys, human form, banked items or an
interrupted boss/interface/scene. Rebuild it from checkpoint plus ownership,
zone and session predicates, with an explicit branch for every supported
state.

There is no quest-local debug procedure. The generic quest cheat sets state 56
only, without XP, quest points, unlock receipts, disguise cleanup or side-field
reconciliation. Keep that clearly documented as a state fixture, not a proof
of completion. Add test-fixture-only setup/reset helpers that never award
persistent rewards and clean only owner-scoped actors/items/interfaces.

At login, reconcile at minimum: invalid boosted starts, follower/static actor
phase, out-of-zone goblin form, worn mail with no free space, missing sphere or
mail, the six-key phase, crypt boss/husk phase, open interface 738/739, an
abandoned Yu'biusk instance and completion settlement. Death and teleport need
explicit rules for each owned follower, boss and scene.

## 14. Modernization packages

Implement in dependency order; each package should be independently
reviewable and leave the content build green.

1. **Reference/schema package** — pin current article, guide, transcript and
   item/NPC revisions; fix cache quest prerequisites; inventory native morphs,
   interface scripts and all existing-save combinations.
2. **Placement/maplink package** — add correct persistent/phased actors,
   owner-scoped follower/scene actors, LOTG staircase round trip and verified
   Dorgesh-Kaan/Goblin Cave routes. This removes the organic-route blockers.
3. **State/recovery package** — assign every state and side varbit one owner,
   define item ownership domains, login/death/teleport reconciliation and save
   migration for skipped states/permanent goblin form.
4. **Start/companion package** — base-stat requirements, standard offer,
   Grubfoot escort, Mistag/council branch, Zanik dream scene and sphere travel.
5. **Transformation package** — canonical Herblore recipe/XP/doses, interface
   739, avatar/name selection, equipment/mail rules and zone-bound reversion.
6. **Temple package** — guarded traversal, stair maplink use, mail crate/dyes,
   Zanik sphere escape, personal phasing and full High Priest quiz states.
7. **Keys package** — shared Thieving attempts, Hemenster fishing, exact Aggie
   service, bank/loss replacement, mail redye loop and atomic crypt unlock.
8. **Crypt package** — owned encounters, all five combat contracts, Skoblins,
   defeated dialogue/name progression, leave/re-enter and Hopespear-safe hooks.
9. **Machine/Yu'biusk package** — verified cave route, interface 738, resumable
   Oldak/Zanik portal scene, owned Yu'biusk and strange-box completion scene.
10. **Completion/unlocks package** — idempotent XP/QP settlement, accurate
    scroll, Goblin potion, altar, BLQ, Oldak sphere and Break services.
11. **Journal/admin/test package** — predicate-derived journal, scoped fixtures,
    extractor alias fix, save migration and interruption/multiplayer tests.

## 15. Verification matrix (Gate D)

| Area | Required automated checks | Required integration checks |
| --- | --- | --- |
| Requirements/start | both prerequisite truth tables; four base-level boundaries; cache/runtime agreement | accept/decline/re-ask, light/no-follower and Grubfoot availability |
| Placement | actor visibility/morph table by state; no global duplicates | two players at different phases; restart/world-hop cleanup |
| Travel | every source/destination maplink including staircase round trip | full Lumbridge mines -> Dorgesh-Kaan -> cave -> temple route; Lost Tribe/Mcannon regressions |
| Followers/scenes | ownership, distance, region/logout reconciliation, scene receipts | Grubfoot and Zanik escort interruption; no cross-player advancement |
| Potion/form | level 47 recipe, 55 XP, dose/vial matrix, zone/equipment rules, interface 739 buttons | cancel/reopen, teleport with full inventory/worn mail, death/logout and post-quest reuse |
| Mail/dyes | inventory/worn/bank/lost/full-space matrix; atomic recolour | every guard room and colour; Goblin Diplomacy/Golem/cape-dye regressions |
| Zanik/quiz | sphere ownership and scene idempotence; all answer branches/states | cell escape, room exit, wrong answers/retry, 26/28/30/32 recovery |
| Fishing/Aggie | Fishing attempt/XP/interruption; exact transaction and space cases | Fishing Contest spot/gate regression; bank/lost whitefish/mail recovery |
| Keys/door | six key inventory/bank/loss cases; pickpocket success/failure/stun | all enclaves, redye order, unlock once, leave for gear and re-enter |
| Crypt | per-player spawn/kill credit, five style/effect rows, husk recreation | concurrent players, death/teleport/logout per fight, Strongbones summons |
| Machine | connector bounds, all six buttons, wrong/correct confirm, remount | interface close/logout/reopen and exact 9/4/1 animation |
| Yu'biusk | instance ownership, scene/state receipts, return fields | interruption at each cutscene boundary, BLQ/re-entry and strange-box approach |
| Completion | replay/full inventory/reconnect at every settlement boundary | exact 4x8,000 XP, 2 QP and one completion record/scroll |
| Unlocks | potion, altar, fairy ring and sphere access truth tables | world/POH BLQ, Oldak exchange, sphere Break/destination and pre-quest denial |
| Journal/admin | every native/legacy state and ownership permutation | truthful recovery hints; fixtures do not award or delete persistent value |

Run at minimum the content compiler/build, focused quest tests, multiplayer
actor/combat tests, shared Lost Tribe/Another Slice/Fishing Contest/Dwarf
Cannon/Goblin Diplomacy/Golem/Herblore/dye/fairy-ring regressions, and
`python3 tools/questhelper_extract.py landofthegoblins --check` after fixing its
fish-spot alias. Compiler success alone is not completion evidence.

## 16. Exit criteria

Land of the Goblins may be marked modern only when:

- the Wiki article, guide, transcript, potion/mail/whitefish/key/sphere, boss,
  altar and Hopespear revisions are freshly pinned and deviations recorded;
- Grubfoot is present and a normal account can traverse every real route from
  the quest offer to the strange box without fixture state;
- Grubfoot/Zanik, temple actors, crypt encounters and Yu'biusk are correctly
  owned/phased under concurrent players, logout, death and region changes;
- all native states 0-56 and relevant side varbits have reachable, truthful,
  recoverable owners, including formerly skipped 18/20/28/30/32;
- potion brewing/doses/XP, interface 739 transformation and human reversion
  match current OSRS, including worn-mail/full-inventory behavior;
- guarded movement, Zanik's escape, High Priest choices, fishing, Aggie, dyes,
  six keys and crypt unlock use real mechanics and atomic item transactions;
- the five bosses implement their exact styles, drains and summons without
  public cross-credit or dead defeated-NPC states;
- interface 738, portal scenes and owned Yu'biusk replace the one-click and
  shared-map shortcuts;
- completion settles two quest points and four 8,000-XP awards exactly once;
- Goblin potion making, altar, world/POH BLQ, Oldak purchase and plain-of-mud
  sphere Break are real, gated and tested downstream effects;
- the journal, cache, runtime, admin fixtures and POH consumers agree; and
- automated, restart and multiplayer integration evidence satisfies Gate D.

This audit intentionally makes no gameplay changes.
