# Vespula: recorded behaviour

Read from three submissions on Grid Master worlds (see [README.md](README.md)):

| Submission | Raid | Room | What it holds |
|---|---|---|---|
| 820 | four-man (`raids_client_partysize` 4, `_scaled` 54) | CCW, turned a quarter | the whole fight, 130 ticks: a sting, a hatch, a soldier, 59 drains, 41 enraged stings, the clear |
| 843 | solo | CCW, the same turn | the start, the first enrage, two drains, the clear |
| 3389 | solo, Challenge Mode | CW, unturned | the start, one drain, the clear (on a shorter schedule) |

Tool: `tools/cox/rsprox_vespula.py` (sections layout, vespula, grubs, drain, clear, timeline), on top of `tools/cox/rsprox_room.py`. Its output over these three is [vespula.out](vespula.out). The content is `cox_vespula.rs2`; the spec is `docs/minigames/cox/rooms/vespula_spec.md`, whose §8 lists what this file changed.

**Status key:** **fixed** = changed to match the recording; **match** = ours already did it; **open** = not settled by these recordings.

## Reading the coordinates (do this first)

The transcriber maps every instance tile back to the template tile it was copied from, one 8x8 chunk at a time. In a room turned an **odd** number of quarter turns it rotates each tile's offset inside its chunk the wrong way. That **mirrors the offset on both axes** (`l -> 7 - l`):

- two template-adjacent tiles across a chunk edge print 15 apart (the boils at (3,15) and (3,16) print as z 8 and 23);
- the raider's 2-tile run prints as a 14-tile jump at a chunk edge.

`rsprox_room.py` detects the mirror from the local raider's runs and undoes it.

Two more rules:

- **NPC tiles.** A size-N npc's printed tile is its instance south-west tile mapped back. That is another corner of its template footprint: the template **north-west** corner in 820/843's room. The room's turn comes from the npcs' step labels (instance directions) against their corrected tile deltas. When no npc takes a labelled step, it comes from the portal's known template tile.
- **Unturned rooms.** These come out right as printed (3389).

After correcting, every tile matches the template tables of `cox_vespula.rs2` **exactly**:

- Vespula: (15,8) CCW, (11,8) CW.
- Portal: (17,20), (11,20).
- Grubs: (12,5), (22,8), (20,13), (13,13) CCW; (8,13), (15,13), (8,5), (5,9) CW.
- Boils: (3,15), (3,16).
- Tendrils: anchored at (16,1).
- Roots: (2,5), (2,8), (2,22), (3,24), (4,3), (5,24). These are the NE corners of the 2x2s at (2,4), (2,7), (2,21), (3,23), (4,2) and (5,23).

## The start

S is the tick the room's npcs appear.

| | Recorded | Ours before |
|---|---|---|
| First move | S+2 (3 of 3). She flies a tile a tick (the transcript calls each step a teleport). | match |
| Unstung grubs | Every unstung grub turns **sickly at S+10** (3 of 3), with no hp lost (no bar sent). 820's regrown grub did the same, 10 ticks after it came back. | sickly only below full hp: **fixed** |
| Roots | Flower from S+10 to S+15: two at +10, one each at +11, +13, +14 and +15 (820). | all at the start: **fixed** |
| Grub facing | Each grub is added facing her hover along the axis between them, and keeps that angle through every form: above her south (0), below her north (1024), beside her toward her. cw unturned (3389): (8,13) 0, (15,13) 0, (8,5) 1024, (5,9) 1536. ccw (820, 843, a quarter turn; instance angles 512, 0, 1536, 1536): (12,5) north, (22,8) west, (20,13) south, (13,13) south. | all faced 0 in the instance whatever the turn: **fixed** (`~cox_vespula_grub_face_tile`, through the room transform; a live client reads these exact instance angles in a turned and an unturned room) |
| Grub bars | No bar on a healthy grub. From the sting, `headbar id=0` (healthbar_standard_30, shown 300 cycles) on every fill step, so it stays drawn through the drain. | **match** (`npc_headbar_from`; a live client draws it from the sting to the hatch) |

## Her stings on the grubs

| | Recorded | Ours before |
|---|---|---|
| Which grub | Not the farthest: two of three first picks were not. A random unstung grub fits all three. | farthest: **fixed** |
| Where she stings from | The nearest 5x5 tile **edge-adjacent** to the grub's 3x3 with the grub's middle column or row inside her span: (17,8) for the grub at (20,13), (13,8) for (12,5), CW (8,8) for (5,9). 3 of 3. | centred over the grub: **fixed** |
| When | The tick after she arrives. Seq 7454 with delay 15. | match |
| What it does | The **next** tick: the grub drops to **56%** (bar 17/30) in the infected form, with sound 570 (delay 15) at its middle. | drain armed, no drop, same tick: **fixed** |
| The drain | 1 hp a tick (the bar falls a step every ~4.2 ticks). It hatches **69 ticks after the sting**. | 1 a tick from full (125 ticks): **fixed**. No bar at all on the client, the sting's drop included: **fixed** (`npc_headbar_from`) |
| The hatch | The grub's death seq, then 2 ticks later the dead form, with a soldier risen **on the grub's own tile** (hatch seq and graphic). | soldier on a paired tile: **fixed** |
| Regrowth | 37 ticks after the dead form, the grub is healthy again, **with its soldier still alive**. | 16 ticks after the soldier is gone: **fixed** |

## The enrage

| | Recorded | Ours before |
|---|---|---|
| Trigger | The tick **after a raider attacks the portal**, before any hit lands (843: attack 884, enraged 885; 3389: 4339 → 4340). | after the hit landed, then flew home first: **fixed** |
| Where | **In place**. She then flies home enraged, a tile a tick. | changed form only on arrival home: **fixed** |
| Length | 20 ticks a cycle. Then one tick flying, and enraged again at once while the portal is under attack (820: five cycles, gaps of 21). | 24 ticks after the last hit: **fixed** (renewal reads the 9-tick "under attack" window) |
| Movement while enraged | None once home: 820 holds (15,8) for over 120 ticks, 843 likewise. The "flying back and forth, jabbing" is her enraged form's own ready animation, `vespula_attack_melee_flying` (the cache: 20 frames x 3 cycles, a 2-tick loop). A sting adds `face_angle` toward its target and the same seq. | **match** (a live client poses her enraged form with that loop) |
| Stomp | 2–8 every tick to anyone under her (3 of 3 rooms). | match |
| Sting | **One** raider on her reach, landing the tick after (sound 819 at their tile). A raider beside the target is not hit. On her own 3-tick clock: ready 3 ticks after a sting, the first tick someone is in reach; the beat runs on across the flying and re-enrage ticks (24 → 28, 46 → 49). | everyone within a tile of the target, every 2 ticks: **fixed** |

## The portal's drain

| | Recorded | Ours before |
|---|---|---|
| What it is | The portal's own **attack on its target**: the first raider to hit it. One raider took every drain in 820. | every raider on the two cove tiles: **fixed** |
| When | Every 2 ticks, from the tick after its first hit lands (843, 3389). | every 2 ticks while hit within 9 |
| The projectile | 1366, from the pillar (18,22) to the tile the target ended the last tick on: start 0, end 60, slope 2, progress 128, heights 140 → 120 raw. | end 30, heights 10/10: **fixed** |
| Landing | 2 ticks after the throw; sound 2885 (range 3) on the tile. | 1 tick: **fixed** |
| Amount | not measurable here: one drain on the local raider, with Rigour draining too | 3 prayer (wiki), **open** |

## Soldiers

| | Recorded (820) | Ours before |
|---|---|---|
| Rise | On its grub's tile; flies 3 ticks later. | paired tile, fought 5 ticks later: **fixed** |
| Attack | Seq 7454, every 4 ticks. | match (engine, attack rate 4) |
| Heal | The **portal only**, 3% of its max (211 of 7,000) every 5 ticks from 9 ticks after the rise, with spotanim 1367 (height 200) on itself and sound 808. Vespula took no heal. | 17 to the portal and Vespula every 8 ticks, a projectile: **fixed** |

## The clear

D is the portal's killing blow (its ops hidden).

| | Recorded (843, 820) | Ours before |
|---|---|---|
| D+2 | The grubs and the portal gone; her death seq (7459) and every soldier's; sound 2744 (range 10) at her; the pillars' closing anim (7500); the roots emptied. | all at D+3, the drop on her: **fixed** (portal `death_delay=1`) |
| D+4 | Her and the soldiers gone; the message `<col=ef20ff>As the link to the Abyss disintegrates, the abyssal creatures perish.</col>`; the boils burst, each with graphic 240 (height 5), and sound 1457; her journal (loc 30025) on the tile **above her middle**, her hover SW + (2,5) (CCW (17,13), CW (13,13)), with graphic 240, sound 1457 and the drop on it. | **fixed** |
| The drop | 2 Xeric's aid (+), each its own item, and one each of revitalisation (+), prayer enhance (+) and overload (+). | one stack of 2: **fixed** |
| D+6 | The closed pillars. | **fixed** |

3389 (Challenge Mode) did D+4's work at D+2. Ours follows normal mode.

The portal plays no defend or death animation (3 of 3). Ours played the engine's human block and death: **fixed**.

## Not seen

These are not in any of the three recordings, so ours is unchanged:

- her ranged attack and her landing (she was enraged throughout);
- a fed grub;
- a soldier's explosion.
