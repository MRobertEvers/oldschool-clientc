# autozuk: jeremiah855/AUTOZUK (Inferno wave solver, single-file `index.html`)

Pinned 2026-10-03 by the waves loop corpus worker (`corpus.code`). Entry in `../LEDGER_code.md`; every constant below is a row of `../CODE_CONSTANTS.md` (the row id is in the first column).

* **Repository**: https://github.com/jeremiah855/AUTOZUK
* **Commit**: 346dece30982a2c5ee60b702721d91ba17b4b8da (committed 2026-07-14)
* **Licence**: MIT, Copyright (c) 2026 jeremiah855 (`LICENSE`).
* **What it is for**: solves a prayer rotation / safe tile for a scouted wave.
* **Files copied** (byte for byte, licence header intact): `index.html` (220 KB): the `sim-core` script (lines ~366-1000: arena, spawn tiles, MOB_DEFS, hit-tick tables, line of sight, movement, dig, blob scan, revive, pillar collapse) and the worker. The UI and gear sections are in the same file; read the sim-core.
* **What it credits as its own sources**: Takes weapon and equipment data from the weirdgloop osrs-dps-calc `equipment.json` (index.html:1421). States no source for its monster numbers; its dig constants (-38 / -50, 10 %), 10 % revive, flicker and blob scan are identical to the trainer's, so it is built on the trainer's model. The calibrated pieces are `MONSTER_PROJECTILE_HIT_TICKS` (index.html:446-452, comment "calibrated projectile-origin tables"), the monster max-hit table `INFERNO_NPCS` (:1485-1495) and the spawn-order `infernoscouter index digit`. TJS cites https://detuks.com/blog/autozuk-inferno-wave-solver (not GitHub, not fetched) as the write-up.

## Constants this source states (name, value, file and line)

The value column is the whole row of `../CODE_CONSTANTS.md`; a source often states only part of it (the notes below and the row's last column say which part).

| row | quantity | value | file:line (relative to this directory) |
|---|---|---|---|
| C002 | arena: playable grid | 29 x 30 tiles | `index.html:369` |
| C013 | spawn: candidate spawn tiles (region-local SW tile of the footprint) | (18,41) (39,41) (20,35) (40,34) (33,29) (22,23) (40,21) (18,18) (32,18); world = region + (2240, 5312) [derived] | `index.html:371` |
| C015 | spawn: nibbler spawn block | 3x3 block region x 25..27, y 33..35; n tiles taken from a shuffle of 9 | `index.html:634` |
| C016 | spawn: monsters spawn on tick 15 of the wave (counted since login) | 15 | `index.html:2799` |
| C020 | pillar: hitpoints | 255 | `index.html:574` |
| C021 | pillar: size and positions | 3x3; south (21,37), west (11,23), north (28,21) in the trainer frame = region SW tiles (27,23) (17,37) (34,39) | `index.html:372` |
| C023 | pillar: nibbler damage to a pillar | floor(random*5) = 0..4 per hit, one hit per attack speed (4) | `index.html:900` |
| C024 | pillar: collapse effects | AUTOZUK: nibblers die, other monsters next to the pillar take floor(hp/2), the player next to it takes floor(current hp/2) | `index.html:770`, `index.html:1174` |
| C025 | pillar: nibbler target rule | all of a wave's nibblers target one randomly chosen surviving pillar and ignore the player until none is left | `index.html:629` |
| C026 | Jal-Nib: hitpoints / level / defence | 10 / 32 / 15 | `index.html:1486` |
| C027 | Jal-Nib: attack speed / range / size / style | 4 / 1 / 1 / crush | `index.html:381` |
| C028 | Jal-Nib: max hit vs player | 4 (damage 0..4) | `index.html:1486` |
| C031 | Jal-MejRah: max hit; run-energy drain per hit | 19; run energy -300 (trainer stat units) | `index.html:1487` |
| C033 | Jal-Ak: attack cycle | 6 ticks (trainer: attackSpeed 3 doubled by the scan: scan, then attack 3 ticks later) | `index.html:379` |
| C034 | Jal-Ak: scan rule (when it reads the prayer) | scans when it gains line of sight, or when its cooldown is <= 0 and no scan is held; the attack follows 3 ticks later | `index.html:909` |
| C036 | Jal-Ak: max hit (magic/ranged) | 29 | `index.html:1488` |
| C037 | Jal-Ak: melee when the player is adjacent | crush, 50% of attacks while within melee distance (diagonals count) | `index.html:514` |
| C038 | Jal-Ak: bloblets on death | 3: range (+1,-1), melee (0,0), mage (+2,-2) from the blob SW tile; 15 hp, level 70; first attack after a cooldown of 4 | `index.html:936` |
| C039 | Jal-AkRek (bloblets): hp / max hit / speed / range | 15 / 18 / 4 / melee 1, mage 15, range 15 (level 70) | `index.html:1489` |
| C041 | Jal-ImKot: max hit | 49 (slash) | `index.html:1492` |
| C042 | Jal-ImKot: dig trigger | no line of sight and attackDelay <= -38 with 10% per tick, or <= -50 | `index.html:871` |
| C044 | Jal-ImKot: dig landing tile | first free of: (player.x-3, player.y+3), under the player, (x-3, y), (x, y+3), else (x-1, y+1) (trainer frame) | `index.html:1267` |
| C046 | Jal-Xil: attack range | trainer/AUTOZUK 15 tiles; kotori Type.RANGER range 98 | `index.html:377` |
| C047 | Jal-Xil: max hit (ranged / crush when adjacent) | 46 / 19 | `index.html:1493` |
| C048 | Jal-Xil: projectile hit delay | trainer: SDK ranged formula floor((3+d)/6)+1 then +2 (reduceDelay -2); AUTOZUK calibrated table by distance from the 3x3 centre: d1-5 3, d6-9 4, d10-12 5, d13+ 6 (hit tick, attack tick = 1) | `index.html:450` |
| C050 | Jal-Zek: max hit (magic / stab when adjacent) | 70 / 52 | `index.html:1494` |
| C051 | Jal-Zek: flicker (visual tell) | 1 tick before the attack | `index.html:903` |
| C052 | Jal-Zek: resurrection chance per attack opportunity | 10% | `index.html:904` |
| C053 | Jal-Zek: revive: hp, once, who, which waves | returns at floor(maxhp/2); each corpse once; nibblers and bloblets excluded; none on wave 69 | `index.html:962` |
| C054 | Jal-Zek: ticks after a revive | mager acts again after 8; the revived monster's first attack after attackSpeed (trainer) or attackSpeed+1 (AUTOZUK) | `index.html:907` |
| C086 | line of sight: algorithm and masks | Bresenham-style, 16.16 fixed point; masks NORTH 0x400, EAST 0x1000, SOUTH 0x4000, WEST 0x10000, FULL 0x20000; an NPC's LoS is tested from the player to the closest footprint tile; range 1 = melee adjacency | `index.html:499` |
| C087 | movement: NPC step rule | one step a tick toward the player by sign(dx), sign(dy); if the destination footprint overlaps the player keep y (corner safespot); under the player, a random cardinal sidestep; no move while attackDelay > attackSpeed (after a dig); frozen or stunned block movement | `index.html:874` |
| C090 | combat: NPC max hit and defence roll | max = floor((effective * (bonus + 64) + 320) / 640), effective = level + 9; defence roll (def + 9) * (bonus + 64) | `index.html:1618` |
