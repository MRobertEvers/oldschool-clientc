# CoX rooms against RSProx: facings, bars, overlay, locs, sounds, despawns

`tools/cox/rsprox_room_audit.py <room>` compares one room's recordings (820,
843, 3282, 3285, 3389, the archive's only CoX room sessions) with our runs of
the room's own test. It covers six things:
- the angle each npc is added at;
- its overhead bars;
- the boss HP overlay;
- every loc's tile, shape and rotation;
- the sounds;
- how long dropped objs last.

`test/raids/_cox_room_probe.lua` tells it which variant and turn a run's
raid n holds. The tick log carries what it needs: the spawn row's angle, the
obj row's duration, and an `npc_headbar` row.

## What every room shares

| | Recorded | Ours before |
|---|---|---|
| Scripted locs' rotation | **Fixed** instance rotation, the same in every turn of a room. Exit crystals and fires are 0, Vespula's and Muttadiles' tendrils 0/1, Vasa's exit pair 0/1, Mystics' portals 0, crabs' big crystal 0. Vasa's corner crystals are the exception and turn with the room (template: SW 1, NW 2, NE 0, SE 3). | turned with the room: **fixed** (`~cox_room_loc_fixed`) |
| A loc taken away at a clear | **Replaced** by `invisible_type8_nonblocking`, shape 10, rotation 0, on its tile. This covers crystals, fires, tendrils and Vasa's exits. Guardians' and the thieving beast's `invisible_type8_blocking` go to the nonblocking twin. The crabs' 2x2 crystal leaves four of them. | deleted: **fixed** (`~cox_loc_vacate`) |
| Exit crystal shatter | `100_cauldron_explodes` and, 5 cycles on, `kr_smash_window`, area sounds at the crystal, range 10. | Muttadiles sent synths to each raider; Vasa used range 16 with no delay: **fixed** |
| Overhead bars | Sent with a hit, never for a heal (archive-wide: only "headbar npcs" such as Nylocas pillars and Akkha's move a bar without a splat). Ids: `standard_30` for every CoX npc except the Ice Demon (19, waiting 7), the thieving beast (7, its hunger, every tick), Olm (`standard_120`, dying 8). | Tekton, Vasa, Vespula and the big Muttadile had 60/80; `npc_statheal` sent a bar: **fixed** |
| HP overlay | Server-written for **bosses** only (the boss bit is always set). Shown from the first attack, through every form, while other npcs take the hits, until the boss is removed or turns into a non-boss form, then 8 more ticks. Bosses: Tekton (not waiting), Vasa (not dormant, not his crystals), the vanguards (melee/ranged/magic), both Muttadiles, Vespula's portal (pinned from the pass), Olm. | the combat target only, no boss bit: **fixed** (engine: npc param `hpbar_hud_boss`, sticky boss, 8-tick hold) |
| Hit and death sounds | The hit sound goes to the dealer only. Recorded: Guardians `poh_stonehit`; crabs `rock_crab_hit`; tightrope mage `godwars_spirit_hit/death`, ranger `blade2`/`human_death`; scavengers `goblin_hit/death`. Deaths: Tekton `tzhaar_mej_death`, vanguards `tzhaar_ket_death`, Vasa `avatar_warrior_death`, Muttadiles `crocodile_death`. | missing: **fixed** (`cox.npc`) |
| Drops | Every room's vials and seeds 3000 ticks (`despawn=2999`), each vial its own item; bones 200. | Vanguards and Guardians 200; Tekton's and Vasa's vials stacked: **fixed** |
| Prayer off | `cancel_prayer` (1,798 times in the archive); `prayer_off` is the refusal sound. | `prayer_off` on every toggle-off: **fixed** |

## Per room

| Room | Recorded | Fixed |
|---|---|---|
| Tekton | Faces template north (west in thru) at his add. Crystal (7,14) / (14,23) / (22,16). The book sits on his tile + (1,1), at a random rotation. | Facing (within a degree); crystal tiles; book rotation; the two overloads. |
| Vasa | Footsteps (`godwars_avatar_footsteps`, range 12, delays 0 and 9) every walking tick. `lunar_energy_transfer` every other tick while siphoning. `paralyze` with "ignore your puny attack". His book on his middle tile at removal. | All of these. |
| Vanguards | Book on the crystal's instance tile + (1,1), placed as the crystal goes. | Book placed; drop duration. |
| Muttadiles | Tendrils (15,1)/(16,1) in thru as well as cw. Meat tree rotation 0. Bronze axe 18000 ticks. | All of these. |
| Tightrope | `tightrope` synth on every step of a crossing (loops 2, then 1 on the last). | Per-step creak. |
| Thieving | Beast bar `healthbar_7` every tick, 100 falling as it eats. | Hunger bar. |

## Open

- **Vasa thru spawn tile:** recorded (13,14) (820, 3389); ours (13,13). Moving it made 4 of 8 thru seeds take a stomp; the baseline is not yet measured.
- **Crabs:** the crabs pinch adjacent raiders (`rock_crab_attack`, a 0 splat, `human_block_1`). A stun comes with `kalthite_egg_crack` and `stunned` (area, delays 20 and 30) and `stunned_thieving`. The orb has its own sounds (`anma_axe_bounce`, `place_crystal`, `tl_place_fractured`, `whiteout`, `windbolt_hit`). None is modelled; it is a mechanics audit of its own.
- **Player block sound:** a 0 splat on the player carries an armour-dependent synth (`leather_hit_*`, `human_block_1`, …). This is engine-wide and not yet done.
- **Vespula's turns:** her turns on stomp ticks (843 t+7, 3389 t+10/11) fit no target rule found. The per-sting turn is done.
- **Spawn angles of npcs first seen after moving:** mystics, the tightrope pair, crabs and dormant vanguards are first seen mid-motion, so their add angles are not spawn rules.
- **Left to other sessions:** Ice Demon (waiting facing per variant, snow → invisible), Shamans and Mystics.
