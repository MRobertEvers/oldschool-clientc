# The solo raid relay: every layout, 128 seeds (2026-10-19)

Owner, 2026-10-19: "Write a solo chambers of xeric solver that is able to solve
all rooms and layouts across 128 seeds." Then, in the same session:

1. "You will need to implement olm prep meaning you will need to farm and make
   potions."
2. "Try to avoid 'maxed' sets and prefer budget gear."
3. "No runner should ever drop real gear. They should build the storage and
   store it in there."
4. "Check solo guides online for the appropriate setup."

The relay is `test/raids/cox_raid_solo.lua` (the prep relay,
`cox_raid_solo_prep.lua`, folded into it). GREEN per seed: the Ancient chest
looted, the raider never dead, no real item ever dropped. The target is 128 of
128 on scriptrun (`--name raid0..raid127`, the layout is the run name's draw).

## 1. Baseline (HEAD b6a8b603a, private build `build_cox_relay`)

`cox_raid_solo_prep`, raid1..raid8: **0/8**. Every room test passes on its own
(Vasa, Ice demon, Scavengers 8/8; Shamans 6/8 on its strict no-damage row), so
the relay fails on what a room is entered with and on the walker:

| seed | where | cause |
|---|---|---|
| raid1, raid3, raid8 | Vespula | script error: the solo method was retuned on 2026-10-18 to Tumeken's shadow + `::~maxmage`; the relay's crossbow has no `rate` (`cox2_vespula.lua` V.WEAPONS) |
| raid4 | Vasa | no overload on floor 1: crystal hits average 2.5 (room test 14), Vasa healed 418 (room test 51), never killed in 2,900 ticks |
| raid6 | Ice demon | arrived with 0 restore doses, prayer 49 |
| raid7 | Shamans | arrived with 0 restore doses, prayer 5, every stat 68 |
| raid5 | large scavenger room | walker: "exit edge nil" after the shortcut |
| raid2 | Olm | the prep brewed 1 overload of 2 (the golpar/noxifer seeds short); Olm entered at 72 hp, drained to 80; died with "none left" 5 times |

## 2. The setup (wiki Chambers_of_Xeric/Strategies, "Setups", Minimum column)

Read from `sources/wiki_Chambers_of_Xeric_Strategies.wikitext` lines 209-296,
re-checked online 2026-10-19. It is the budget column (the others are
Standard, Bowfa, DHCB and Max).

| slot | wiki Minimum | ours (obj) | note |
|---|---|---|---|
| head | Helm of Neitiznot | `fris_kingly_helm` | |
| cape | Fire cape | `tzhaar_cape_fire` | |
| neck | Amulet of fury | `enchanted_onyx_amulet` | |
| ammo | Ruby dragon bolts (e) | `dragon_bolts_enchanted_ruby` | |
| weapon | Abyssal tentacle | `abyssal_tentacle` | |
| body | Fighter torso | `barbassault_penance_fighter_torso` | |
| legs | Black d'hide chaps | `black_dragonhide_chaps` | |
| shield | Dragon defender | `dragon_parryingdagger` | |
| hands | Barrows gloves | `hundred_gauntlets_level_10` | |
| feet | Dragon boots | `dragon_boots` | |
| ring | Lightbearer | `lightbearer` | was the berserker ring |
| pack | Dragon crossbow | `xbows_crossbow_dragon` | |
| pack | Ava's accumulator | `anma_50_reward` | was Ava's assembler |
| pack | Trident of the Swamp | `toxic_tots_charged` | |
| pack | Occult necklace | `occult_necklace` | |
| pack | Black d'hide body | `black_dragonhide_body` | |
| pack | Toxic blowpipe | `toxic_blowpipe_loaded` | |
| pack | Mystic robe top, bottom | `mystic_robe_top`, `mystic_robe_bottom` | |
| pack | Bandos godsword | `bgs` | |
| pack | Anglerfish | `anglerfish` | |
| pack | Rune pouch | `bh_rune_pouch` | |
| pack | Imbued Saradomin cape | `ma2_saradomin_cape` | |
| pack | Rune pickaxe | `rune_pickaxe` | was the dragon pickaxe |
| pack | Salve amulet(ei) | `nzone_salve_amulet_e` | |
| pack | Dragon sword | `dragon_shortsword` | |
| pack | Lockpick | **`dragon_warhammer`** | DEVIATION (owner 2026-10-19): the storage build needs a hammer, and the DWH "can be used as the hammer for crabs" (wielded to smash, `cox_crabs.rs2`); scavengers drop a lockpick 2/18 |
| pack | Sanfew serum(4) | `sanfew_salve_4_dose` | was antidote++ |
| pack | 7 Saradomin brew(4) | `4dosepotionofsaradomin` x6 + **`4dosestamina`** | DEVIATION (owner 2026-10-19): "bring a stamina and save it for Olm" (wiki: "one stamina potion is highly recommended for Olm" solo); parked at the first unit, back in the Olm pack |
| pack | 2 Super restore(4) | `4dose2restore` x2 | was 3 |
| pack | Super combat potion(4) | -- | REMOVED (owner 2026-10-19): the slot for the scavengers' secondaries |
| pack | Ranging potion(4) | -- | REMOVED (owner 2026-10-19): the slot for the scavengers' secondaries |

28 in the pack. No max set anywhere (owner rule 2): the Vespula room's
Tumeken's shadow method is replaced by a crossbow/trident method on this kit.

The wiki's prep list (same page) says what the two boost potions are for:
"Tekton first: prepot melee", "Vanguards first: prepot all styles",
"Shamans present ... prepot range", "Tightrope first: prepot range",
"Muttadiles first: prepot melee/magic and ranged", "Vespula first ... prepot
mage". Vasa: "If Vasa is first ... drain health ... to about 10 HP" (the
overheal is Vengeance, not on this spellbook).

## 3. Facts the plan stands on

| fact | source |
|---|---|
| seeds come only from Guardians (3-5 each kind), Shamans and Mystics (5-10 each kind), and the weeds of floor 1's end and floor 2's start/end | wiki Strategies "Resources", "Farming" |
| herbs grow in 30 s (50 ticks); two patches a resource room | wiki "Farming"; `cox_resource.rs2` |
| (+) potions need Herblore 78, overload (+) 90; Farming 55 for noxifer | wiki "Herblore", "Farming" |
| bosses drop potions: Tekton 2 overload (+), 1 revit (+), 1 enhance (+); Vespula 2 aid, 1 revit, 1 overload, 1 enhance; Vasa 2 aid, 1 overload, 1 twisted; Vanguards one overload guaranteed | wiki Strategies |
| storage units: floor ends, resource rooms, the corrupted scavenger (thieving), the ice demon, outside Olm; tiny (unbuilt) private 25; each tier 2 mallignum planks + a hammer + Construction 30/60/90/99; private 30/60/90/120 | wiki "Storage Units"; supplies_spec.md section 4 |
| "outside items such as saradomin brews will stack if they are all of the same item, but raid items (e.g overloads) will not" | wiki "Storage Units" |
| ours: every outside item takes its own private slot (`cox_storage_put_private`, inv `stackall=no`) | content -- DIVERGENCE, section 5 C1 |

## 4. Workstreams

**K. The kit** (`QD.COX.KIT`, `cox2_raid.lua`): the section 2 table, one list
the relay and any room test can use. Solvers that name Ava's assembler take
whichever Ava's device the raider holds (`QD.COX.ava_name()`).

**N. Never drop a real item.** A real item is anything not made in the raid:
not `raids_*`, not a raid tool (`rake`, `spade`, `dibber`, the room's
`bronze_axe`, `hammer` from a scavenger is still the kit's hammer: never
dropped). One predicate, `QD.COX.is_raid_item(name)`, is the only gate any
drop passes. The drop sites found (2026-10-19):
- `cox2_tightrope.lua` ids.droppable (crossbow, rune pouch, BGS, sword,
  Ava's, d'hide body) to make a keystone slot;
- `cox2_supplies.lua` prep: Saradomin brews dropped to fit the gear in the
  store, and to free 12 slots; `rebalance` drops a potion when the store is
  full; `make_room` drops `vial_empty`;
- `cox2_muttadiles.lua` `QD.COX.spare` (a stamina, a restore, a heal).
The replacements: a store first (build it, section S), else drink the lowest
dose of a family in surplus (a vial-smashed raid potion frees its slot; an
outside one leaves a `vial_empty`, which is junk the raid made), else play
without the slot. Tightrope's keystone: the relay arrives with a free slot
(it is entered from storage or with `keep_free`).

**S. Build the storage.** The kit's dragon warhammer (`~cox_has_hammer` takes it carried), Construction 99 (a bring-along
level), planks from the scavengers (`SCAV_WANT.raids_plank`: 2 for small,
4 for medium). At the first storage unit reached with the planks:
`world_op` op5 (Build) until the tier stops rising. Medium (60 private)
holds every switch the prep parks plus the outside potions.

**P. Olm prep: farm and brew** (`cox2_supplies.lua`, in place): seeds from the
demi-bosses and the weeds, secondaries from the scavengers, both carried or
stored; the resource room farms both patches, brews, stores the potions, and
the floor-2 end restocks the Olm kit (2 overload (+), 4 enhance (+), 6 aid
(+), 4 revit (+), as `cox_olm_solo.lua`). Overloads are the critical path:
1 golpar seed per 3 golpar, 1 noxifer per overload (TheEdB0ys 1:04:26: each
overload is 3 golpar + 1 noxifer), so the rakes/loot keep both above the
plan's need; a boss overload (Tekton 2, Vasa 1, Vespula 1, Vanguards 1)
counts against it.

**F. Floor 1 on outside supplies.** Before each combat room with no overload
carried, the relay pre-pots for the room's style per the wiki's list (super
combat for Tekton/Vasa/Muttadiles/Vanguards melee, ranging for
Shamans/Tightrope/Mystics/Vanguards/Muttadiles ranged), at most once a room,
only when that style's stat is not already boosted. Supply families gain the
outside potions where a solver drinks a family (`QD.COX.SUPPLY.boost_melee`,
`boost_ranged`). Restores: the brew floor rule is unchanged.

**V. Vespula on the budget kit.** One owner (an agent), `cox2_vespula.lua`
and `test/raids/cox_vespula.lua` only: the crossbow/trident Redemption
method of section 11 with its root fallback, on the section 2 kit; 48 seeds;
the test's kit moves to it (no `::~maxmage`).

**W. The walker** (`cox2_raid.lua`): the large scavenger room's exit edge
after the shortcut (raid5).

## 5. Content items found

- **C1** private storage stacking of outside items (section 3). Fix when S
  alone does not give the prep room: count a distinct outside obj once.

## 6. Proof

1. One probe seed per failure class (raid4 Vasa, raid6 Ice demon, raid7
   Shamans, raid5 walker, raid2 Olm); read the whole stdout and the arrival
   rows; batch the fixes.
2. raid0..raid15; then raid0..raid127 once. Report: green count, deaths by
   room, the arrival state per room (`arrive.<i>.<room>` rows), any real
   item dropped (a ticklog/ledger scan for `drop` of a non-raid obj).
3. Every failure traced to its first wrong decision and written in section 7
   before the code changes. Two sweeps in a row that do not move toward green
   = stop and replan (plan-each-room-before-any-run).

## 7. Rounds

### Round 1 (2026-10-19)

Owner directions folded in: the dragon warhammer replaces the plain hammer
(storage build; wielded, it smashes the crabs); the crab room gets a fixed
solution per layout (unit below).

| sweep | change | result (raid1..16) | what it showed |
|---|---|---|---|
| base8 | HEAD prep relay | 0/8 | section 1 |
| r1 | wiki Minimum kit, drop guards | 0/16 | 9 runs aborted on the guard: Crabs dropped the kit's hammer, Ice demon dropped the mystic robe bottom to make three slots |
| r2 | brought-in snapshot (a room's hammer/tinderbox may go back down, the kit's never); Ice demon parks its unused switches in its own storage unit | 0/16 | every room failure entered with **0 free slots**: the Minimum is a full 28, the room tests carry 27; the blowpipe is two-handed, so its switch was refused (Vasa: 64% zero hits; Mystics: 230/263 refused swings; Thieving: no slot for a grub, 4,000 ticks idle) |
| r3 | `cox_make_free(1)` before each solver room: eat the anglerfish (it overheals), else a 1-dose of a family that is due | 0/16 | prayer is floor 1's binding budget: Guardians ~70 points, Tekton ~66, Crabs ~45 (Protect from Melee held 425 ticks), Shamans ~38; boost prayers held whole rooms (Piety 480 ticks at Tekton, Rigour + Piety 330 each at Vasa) |
| r4 | boost prayers gated on the prayer pool (`QD.COX.prayer_pool`, 330) | 0/16 | `make_free` found nothing to consume by room 4 (`free.4.vasa 0`): OSRS's answer is decanting, which our content had only for attack/strength/defence/prayer |
| -- | decanting for Saradomin brew, super restore, super combat, ranging, sanfew (`decant_potion.rs2`, the 3-dose super restore's branch in `brew_potion.rs2`); `make_free` decants first | (next sweep) | |

**Vespula on the budget kit** (unit, `vespula_solver.md` section 13): the
dragon crossbow (3.0 a tick on the portal; the trident 0.4-1.2, never kills)
on Synq's Redemption loop with his root fallback as the normal path: 35/48.
Open: the thru layout 2/10 (an enraged sting stepping off the attack tile;
stopping short of the feed tile), one death on run energy (no stamina in the
Minimum), one soldier a tick before the kill, three lapsed enrages.

### Round 2 (2026-10-19)

| sweep | change | result (raid1..16) | what it showed |
|---|---|---|---|
| r5 | decanting + `make_free` decants first | 0/16 | `make_free` still failed 52 of 87 rooms: the scavengers' loot filled the slot just made |
| r6 | loot never takes the last slot (`QD.SUP.RESERVE`); raid stacks stashed at every unit passed (`QD.SUP.stash`); the walker crosses a cleared shortcut (Rubble / Rotten stump are crossed, not walked) | 0/16 | raid5 past the large scavenger room; Vasa 5, Muttadiles 5 |
| budget1 | `_cox_room_budget.lua`: every room from a full start on the wiki kit, 12 seeds | Tekton, Vasa, Muttadiles, Guardians, Shamans, Ice demon, Tightrope, Thieving 12/12; Mystics 10/12; **Vanguards 0/12** | the relay's Vasa/Muttadiles losses are attrition, not the kit; per room costs below |
| r7 | boss potions looted first, a potion makes its own room | 0/16 | Muttadiles gone from the failures; raid5 reached Olm |
| r8 | prep: judge with what storage holds, park the gear, then take the stacks back (buchu first); Tekton's finder takes the relay's look ahead; the walker clears an uncleared obstacle (an iron axe farmed from the scavengers for the sapling) | 0/16 | 17 of 18 preps brewed nothing: the rake never taken (its rack past the scene from the doorway), seeds lost to the reserve |
| r9 | the pack plan (`QD.SUP.parkable`: the BGS, a done room's only switch, the boost potions once an overload is carried); the prep steps in for the tool rack | 0/16 | weeds 12/16 ok; arrivals at 6 hp / 0 prayer |
| r10 | `cox_top_up` before each fight (80 hp, 30 prayer) | 0/16 | Vanguards 4, Mystics 4, Vespula 3, then one each; Olm met with a full pack (now `keep_free = 1`) |

Fresh-start room costs on the wiki kit (budget1, means of 12):

| room | pass | ticks | prayer pts | heal doses | restore doses |
|---|---|---|---|---|---|
| Tekton | 12/12 | 688 | 63 | 0 | 3.3 |
| Vasa | 12/12 | 1405 | 19 | 21.9 | 7.3 |
| Vanguards | 0/12 | 1032 | 60 | 28.0 | 4.9 |
| Muttadiles | 12/12 | 273 | 7 | 11.2 | 4.9 |
| Guardians | 12/12 | 187 | 62 | 0 | 0.3 |
| Mystics | 10/12 | 406 | 26 | 16.6 | 7.6 |
| Shamans | 12/12 | 246 | 54 | 0 | 0.2 |
| Ice demon | 12/12 | 980 | 63 | 0.5 | 1.6 |
| Tightrope | 12/12 | 347 | 49 | 0 | 0 |
| Thieving | 12/12 | 200 | 0 | 0 | 0 |

The kit holds 28 heal and 12 restore doses for the whole of floor 1: Vasa,
Vanguards and Mystics each spend most of it. Those rooms need the budget
methods the guides use (Mystics' corner pseudo-safespot; Vanguards' weak-style
damage), not more supplies.

**Crabs per layout** (unit, `crabs_solver.md` "Round 3"): a fixed plan per
variant (`QD.COXCR.PLAN`), the DWH wielded for every smash, Protect from Melee
only while a free crab is close: 45/48 (the old search 44/48 on the same
kit); thru 24 prayer points a room (was 35), cw 34 (was 43); ccw still the
slow one (median 786 ticks, 90 points: every crystal needs all three crabs).

**Vanguards on the budget kit** (unit, `vanguards_solver.md` round 4): 24/24
harness and 24/24 room test (+12/12 more harness seeds), median 622 ticks, 7
heal / 5 restore doses (was: 0/12, every heal gone). Causes: the target re-picked
every tick on a 1-hp lead (35-106 switches a run, each drawing another
vanguard); no stat restores (the brews drained every combat stat to 1); the
prayer followed the last swinger, not the soonest hit. Their 1 hp / 100 ticks
regeneration is now counted.

| sweep | change | result | what it showed |
|---|---|---|---|
| r11 | Vanguards budget solver | 0/16 | Mystics 4, Vespula 3, Crabs 2 (ccw turn 3 from a full start: the bot stood beside a free dim crab for 1,860 swings, 399 damage, and timed out on its second crystal), Vasa 2, Vanguards 2 (arrivals with 7-8 heal and 0 restore doses), one each Muttadiles, Ice demon, Olm |

Queue (one unit at a time): Mystics (running), Crabs ccw turn 3, Vasa's kill
speed without an overload, Vespula thru; the floor-1 budget after them.

### Round 3 (2026-10-19): farm when possible

The owner: "Cox you can farm and grow and make potions. Do that when
possible." A floor's resource room is passed before its seeds come (the
demi-bosses, the floor end's weeds), so its prep had nothing to plant. Every
join between rooms works both ways (a Passage teleports to whichever side I
am not on; a tendril wall hops either way, 15% hp back), so the relay walks
BACK: `farm_trip` in `cox_raid_solo.lua` keeps the floor's path and its
resource room, and after any room -- and at the floor end after the weeds --
when `cox_farm_due` finds something to grow and brew from what is carried or
stored, it walks back (`cox_walk_path`, `QD.COX.step_back`: tendrils, the
room before's Passage from its far side, an open edge), preps (plant both
patches, brew, store), and walks on to where it was. At most two trips a floor.

| sweep | change | result | what it showed |
|---|---|---|---|
| r12 | farm trips | 0/16 | 2 trips taken, both stuck on the first Passage back (checked the room 3 ticks after the op; the server's walk to the doorway takes longer) |
| r13 | `through_passage` waits for the teleport | 0/16 | trips work both ways (raid10: back 2 rooms, planted 2, brewed 5, on again; raid15: back 3). The brews were few: only stinkhorn among the secondaries -- the scavengers' loot is capped by the slot reserve |

**Mystics** (unit, `mystics_solver.md` round 4): one held trap tile per
target (the walk stalls at P, my tile inside P's 2-tile box, never on its
side), a lure tile first while it is on magic, the trapped one stays the
target, Protect from Magic and Rigour off while it sits stuck: 22/24 harness,
22/24 room test (was 10/12); best seeds 2-4 heal doses, median still 12 heal
and all 8 restores. Open: rooms where all three spawn close and wake together;
spawns with no trap or lure tile; punches before a trap is found.

Content item C2: in the Vespula room npc type 7531 (seq 7459) is "shown its
death animation by a script and is still alive a tick later (200 hp)" -- a
CONTENT CONTRACT line in every raid holding Vespula (raid5, raid6 r13). The
room still passes.

| sweep | change | result | what it showed |
|---|---|---|---|
| r14 | an outside potion may be lent for a slot (owner's ruling) | 0/16 | raid15: the iron axe for the sapling refused by the slot reserve; Olm met at 54 hp / 5 prayer |
| r15 | the axe hunt may use the reserve; a top-up before Olm (95 hp, 85 prayer; restores before enhances); loot and scavengers take endarkened juice first | **2/16** (raid13, raid16: Olm dead, chest looted, never dead; raid16 farmed on a trip back) | raid15 died in the head phase; the rest are room fights: Crabs ccw t3 2, Vespula 3, Mystics 2, Vasa 2, Muttadiles 2, Ice demon 1, Vanguards 1 |

### Round 4 (2026-10-19): 48-seed baselines

Units: Vespula thru (45/48 room test, 23/24 relay harness; engine issues: a
feed on a 3x3 grub walks to the side's middle tile; an in-range ranged attack
whose timer is not ready keeps walking its route), Crabs ccw (48/48 + 48/48
wandered, 0 orb hits: the table check had compared crabs to their spawns,
which the relay's crabs had left; stuns now honest about the 5-tick immunity).

Owner directions: "you have to bring a stamina and save it for Olm" (one brew
swapped for it, parked at the first unit, back in the Olm pack); "Vespula
requires a prayer [enhance] ... If the relay sees that it can't get [one]
before vespula, it should scout a different raid" (`cox_scout`: the visible
path from the start room, both floors; Reload when Vespula comes before a
Tekton / Vanguards / Muttadiles or a seeds-scavengers-resource chain).

Content: C3 the start room's steps are `raids_exit_steps_multi` on
varb5423_raids_client_isleader -> `raids_exit_steps_reload` ("Climb",
"Reload") for a leader; neither op had a handler and both fell to the generic
climb onto floor 2's plane. Now Climb = the Leave/Stay choice, Reload (before
the raid starts) = a new layout (`cox.rs2`).

| sweep (48 seeds) | change | pass | top failures |
|---|---|---|---|
| r20 | clean baseline, stamina kit | 8/48 | Vespula 13, Vanguards 5, Mystics 5, Olm 4 |
| r21 | the prep stashes the Olm-only items first; a tool only past the reserve; decant by cell | 6/48 | zero-slot entries 73 -> 12; farm trips back through a done tendril room failed 8 times |
| r22 | done tendril rooms walked; a failed trip walks back; Vespula entered at full prayer | 8/48 | Vespula 12 |
| r23 | scouting + Reload | 6/48 | 11 reloads; Vespula 13 |
| r24 | Vespula's Redemption loop skips the overload pre-pot | 8/48 | Vespula 9 (13/22 pass), Vanguards 9, Mystics 6, Olm 5 |

Vespula in the relay: carrying an enhance did not separate wins from losses
(fail 25 with / 13 without; pass 5 with / 28 without); the overload pre-pot
did -- every losing room had drunk one. Vanguards' relay losses are supply:
7 of 9 arrived with no restore dose (it needs restores against the brews'
drain). The floor's total supply is now the binding constraint.

| r25 | the kit without the super combat and ranging potion (owner's choice: r24's scavenger rooms stopped for room 88 times in 48 raids; one kill drops three stacks) | **14/48** | brews 253 -> 541 (runs brewing 27 -> 36), scavenger takes 56 -> 119, Olm losses 5 -> 0. Left: Vespula 9, Vasa 3, Vanguards 3, Mystics 3, Shamans 2, Muttadiles 2, descents 2. Vespula's losses cluster by layout/turn (thru t3 3/3, cw t2 2/2) -> a unit. (The private binary was rebuilt before this sweep: another session's new opcode made the old one refuse the shared pack.) |

## 8. Synq's solo method against the relay (2026-10-19)

The owner: "Instead of thrashing, again check synqs solo guide for how to solo
chamber's of xeric." From `synq_transcript.md` 0:00:41-0:14:39 (Reqs, Gear,
Plugins, Scouting, General Info) and 1:49:02 (Olm):

| Synq | the relay today | gap |
|---|---|---|
| **Scout** (0:05:50): "you are randomly generating a dungeon. This can be done over and over to generate a dungeon specifically for you. You can right click the stairs to do this"; the instance map "only loads part of the raid with you as the center" -- move away from the start and reload the client to load more | Reload only when Vespula is seen before an enhance source; the visible part only | scouting is his main tool |
| Pick the raid (0:06:56-0:08:05): easiest combat rooms "the Muttadiles, shamans, skeletal mystics, and guardians"; Vanguards "arguably the hardest room ... it's fine to skip on them"; "Five room raids are usually the best ... four combats and one puzzle, or three combats and two puzzles"; tightrope and crabs the best puzzles | every layout taken | the relay's worst rooms (Vespula, Vanguards, Vasa) are the ones he advises scouting around |
| What drops what (0:06:56): Muttadiles 2 overloads, 2 enhances, 1 brew, 1 restore; Tekton 2 overloads, 1 enhance, 1 restore; Vasa 1 overload, 2 brews; Vespula 1 overload, 1 enhance, 2 brews, 1 restore; Vanguards 1-3 overloads, 1 enhance, 4 brews, 2 restores; shamans/mystics/guardians seeds | the loot takes potions first | the drop table is how he plans supplies -- "no prep solo raids" ride on it |
| **Bank chest before the start** (0:08:21): "Once you found the raid you want to do, you need to decide on what items to bring": crush for Tekton, stab for Vasa, "an extra one to two restores" + an antipoison for Vespula's Redemption, a ZGS optional for Muttadiles, the best pickaxe for Guardians, the salve (ei) for mystics, an antipoison for shamans, a lockpick for thieving, demonbane/fire for the ice demon | one fixed kit for every layout | the start room's `raids_bank_chest_lobby_working` is open until Start raid |
| Private chest early (0:09:28): "bank items you don't need until the end of the raid ... helpful for picking up any extra supplies" (mage gear but the crabs staff, a spare stamina, a weapon an overload replaces) | the pack plan parks the BGS, the stamina, a done room's switch | close; his parks what the SCOUTED rooms will not use |
| Floor ends (0:10:34): a chest, an energy pool, weeds to rake "if you didn't happen to get any" seeds | done | -- |
| Overload + prayer enhance as a pair; brew just before the overload's 15 s re-boost (0:12:44-0:13:53) | rooms drink on their own rules | room-level |
| Olm (1:50:09): "you're probably not going to complete your first solo raid deathless" | -- | -- |

What follows from it, in order of size:
1. **Scout for the raid**, as he does, not take every layout. This changes
   the goal's meaning ("all rooms and layouts") -- the owner's call.
2. **Pack the kit per scouted layout** from the start room's bank chest:
   only the switches the seen rooms use, the rest of the 28 as supplies
   (Vespula's extra restores, an antipoison only with shamans/Vespula).
3. Plan supplies from the drop table of the scouted combat rooms, farming
   only when the drops will not carry Olm.

## 9. Plan: scout lightly, pack per layout (owner's choice, 2026-10-19)

The owner chose "scout lightly": reload only away from the relay's worst
cases, and pack the kit per layout (Synq 0:05:50, 0:08:21).

1. **Scout** (`QD.COX.scout_verdict`, from `cox_scout`'s visible path):
   reload when (a) Vespula comes before an enhance source on the path (the
   owner's rule, section 7 round 4), or (b) Vespula or the Vanguards are seen
   on floor 1 -- they meet the bare kit before any boss drop or prep (r25:
   Vespula 9 losses, Vanguards 3; Vanguards' relay losses arrive with no
   restore). At most 20 reloads, then the raid is played as dealt.
2. **Pack** from the start room's bank chest before Start raid
   (`raids_bank_chest_lobby_working`; setup `::bankgive`s the spares): with
   Vespula on the scouted path, two more super restores (Synq: "an extra one
   to two restores to use the redemption method"). Items for unseen rooms
   stay in the kit -- the scout sees two or three rooms a floor.
3. Measure on 48 seeds; record reloads per raid and the per-room failures.

### Rounds 5-6 (2026-10-19): room units on relay arrivals, no cheats

Owner: "Don't stop until it works." "You should not use cheats during the
relay" (verified: every cheat in 48 runs is a setup bring-along; none in run).
"Mystics should be solved with pray mage and rigour and the blowpipe and
should avoid tanking multiple mystics at a time."

Units (each reproduced on the relay's own seeds):
- Vespula (sec. 15-16): the losses were run energy (rest outside the
  tendrils to 95%), blocks counted as swings, half root trips, a brew every
  cycle, the Redemption threshold: relay 12/19 -> 17/19 on its seeds; open: a
  feed on a 3x3 grub walks to the side's middle (engine T7).
- Muttadiles (round 5): Ranged drained by aid sips with no restore; the
  small one fought from range (now bitten on its edge under Protect from
  Melee): 14/18 -> 16/18.
- Mystics (round 5, the owner's method): Rapid never pressed after the
  blowpipe went on; the target flipped between two awake mystics; walks woke
  others (wake masks): 12/21 -> 14/21; open: cluster spawns.

Shared fixes: the boost gate counts a RUNNING enhance (seen as a dose
leaving the pack); the top-up heals over 50 hp only with a restore left; a
reserve (`cox_reserve`) keeps a scouted Vespula's restores from the rooms
before her, armed by the start-room scout or the floor-2 start's own look;
sweeps read a private pack snapshot (`TORIRSSERVER_SCRIPTS`), since other
sessions' pack rebuilds made the binary refuse mid-sweep.

| sweep (48) | pass | top failures |
|---|---|---|
| r27 | 10 | Vespula 7, Mystics 5, Vasa 4, Muttadiles 4 |
| r29 | 12 | Mystics 9, Vespula 6, Vasa 4 |
| r30 | 12 | Mystics 7, Vespula 6 (none packed: all floor 2) |
| r31 | 13 | Mystics 7, Vespula 5, Vasa 4, Vanguards 3 |

### Round 7 (2026-10-19): scout for a completable raid (owner's call)

The owner's direction: stop solving every room in the relay; scout for a
raid it can complete. `QD.COX.completable` enumerates every layout (26
patterns x 2 combat rings x way x start x puzzle start) consistent with the
rooms the start room shows on both floors, and accepts the raid when at least
90% of those completions use only the reliable rooms (Tekton, Guardians,
Shamans, Muttadiles, Tightrope, Thieving, Crabs, Ice demon). Otherwise the
start room's exit steps Reload it (cap 1500). First probe (r33s, 8 seeds): 1
pass. Every accepted raid was then lost in a "reliable" room, and each loss
had its own cause:

- Tekton (r33s raid1/5): the tentacle missed 4 of 5 swings (424 of 525), and
  his anvil heals (1431) matched the damage (1493) over the 4000 ticks. Now:
  a dragon warhammer special whenever there is 500 energy (the first that
  makes contact always lands, wiki). There is no cap: his drained Defence
  regenerates a level per 15 ticks (`cox_tekton_common_tick`), and the
  lightbearer refills 500 in ~125. Harness 12/12; most kills by ~400 ticks
  (was ~2360).
- Floor-start weeds (r34s raid1/2/8): no storage unit in a floor's start
  room, so the rake filled the pack and the step timed out. Now
  `cox_make_free` first, and `no_room` (skip) when the pack is still short.
- Guardians (r35s raid4): in that turn the room's middle is a wall pocket.
  The route to it ended where the player stood, and the statues were a loop
  round the west wall away, past the plan's ten straight steps (4000 ticks
  on one tile). Now `enter` walks the client's route to a statue's own
  tile. Harness 12/12.
- Ice demon (r35s raid1): 21 Xeric's aid doses with no stat restore drained
  Ranged; 1082 of 1120 blowpipe hits missed. The super restore sat in
  storage: the rebalance quota had no free slot past seven aids. Now the
  quota swaps out a potion of a family over its share, and `make_slots`
  stores the restores after the heals (`SLOT_ORDER`).

r36s (same 8 seeds): 7/8, the one loss in Olm's phase 3 (930 taken).

## 10. Status: called good (owner, 2026-10-19)

The owner closed the relay work here: the solver's purpose was to find
content issues, not to reach 128/128. Everything is UNCOMMITTED in the
owner's folder.

**Where it stands.** r37 (48 seeds, completable-raid scout, today's kit),
41 finished when it was called: 32 pass, 7 fail. Of the 7, Olm 5 (raid2, 9,
11, 31, 35; raid2 died in phase 3 with 930 taken), Muttadiles 1 (raid29),
`raid.out.10` 1 (raid25). The best full sweep before scouting was 14/48 (r25).
The room harness (`test/raids/_cox_room_budget.lua`, kit = relay kit) passes
Tekton 12/12, Guardians 12/12, Ice demon 11/12. Unit results from rounds 5-6:
Crabs 48/48, Vanguards 24/24, Vespula 47/48, Vasa 24/24.

**Content issues the relay found (fixed in OSRS-Content):**
- Decanting did not cover the potions a CoX solo carries: families 11-15
  (Saradomin brew, super restore, super combat, ranging, sanfew) added to
  `decant_potion.rs2`; a super restore used on a super restore decants
  (`brew_potion.rs2`, the 3dose2restore branch).
- The start room's exit steps were a generic climb: `raids_exit_steps_reload`
  now has Climb and Reload (`cox.rs2`), as the leader sees them.

**Open, not content:**
- Vespula T7 (engine): feeding a 3x3 grub walks to the side's middle tile.
- Olm is the relay's main loss on the budget kit; not investigated.
- Mystics: cluster spawns (two awake at once) are still lost in the relay
  (14/21 relay rooms in round 5).

**Running it again.** Private binary `src/build_cox_relay_opt`; every sweep
reads a pack snapshot through `TORIRSSERVER_SCRIPTS=<snapshot>`, rebuilt
whenever another session changes CoX content (the binary refuses a stale
pack). Scratch tools (sweep.sh, cost.py) lived in session 86977d1e's
scratchpad and will need rewriting.
