# kotori_inferno: OreoCupcakes/kotori-plugins, `inferno` plugin (RuneLite client plugin, maintained 2026)

Pinned 2026-10-03 by the waves loop corpus worker (`corpus.code`). Entry in `../LEDGER_code.md`; every constant below is a row of `../CODE_CONSTANTS.md` (the row id is in the first column).

* **Repository**: https://github.com/OreoCupcakes/kotori-plugins
* **Commit**: 9ea4866e0fe1fb96ab07fcce3211f151441d4053 (committed 2026-10-01; the default branch HEAD on the fetch date)
* **Licence**: BSD 2-Clause, Copyright (c) 2023 Kotori (`LICENSE`); the files keep their upstream headers (Devin French 2017, Jacky 2019, Kyleeld 2019): the plugin is a descendant of the RuneLite / OpenOSRS Inferno plugin.
* **What it is for**: the Inferno prayer helper; ticks until each npc attacks; the Zuk shield predicted position; the set timer infobox.
* **Files copied** (byte for byte, licence header intact): `inferno/src/main/java/com/theplug/kotori/inferno/` InfernoNPC.java (attack-tick table: `Type` enum with ticksAfterAnimation, range, priority; animation-driven attack cycle), InfernoPlugin.java (animation id constants, wave number, safespot map and line-of-sight cache, Zuk shield tracking and prediction, set-timer HP thresholds), InfernoWaveMappings.java (wave table 1-69 by monster level), InfernoSpawnTimerInfobox.java (set timer 210 s / 105 s), InfernoBlobDeathSpot.java, displaymodes/InfernoZukShieldDisplayMode.java. Not copied: overlays, config, panel code (presentation), the trivial test launcher.
* **What it credits as its own sources**: Nothing is cited. The constants are those of the OpenOSRS plugin it descends from (compare `openosrs_inferno/`): the two files carry the identical `Type` table. A client observer: it times attacks from the animation it sees, not from server code.

## Constants this source states (name, value, file and line)

The value column is the whole row of `../CODE_CONSTANTS.md`; a source often states only part of it (the notes below and the row's last column say which part).

| row | quantity | value | file:line (relative to this directory) |
|---|---|---|---|
| C001 | arena: region id | 9043 (Inferno interior) | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:79` |
| C006 | ids: npc ids JalTok-Jad / healer (final-wave variant) | 7700 / 7701 (7704 / 7705) | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:400` |
| C007 | ids: npc ids TzKal-Zuk / shield (Ancestral Glyph) / Jal-MejJak / pillar placeholder / dying pillar | 7706 / 7707 (INFERNO_MOVING_SAFESPOT) / 7708 / 7709 / 7710 | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:330` |
| C009 | anim: animation ids the plugins key on | Nib attack 7574 (death 7576); bat attack 7578 (stand 7577); blob range 7581 melee 7582 magic 7583 (death 7584); melee 7597 (burrow 7600); ranger melee 7604 range 7605; mager mage 7610 melee 7612 (revive 7611); Jad mage 7592 range 7593; Zuk 7566 | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:173`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:174`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:176`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:182`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:184`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:186`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:441`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoBlobDeathSpot.java:12`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:288`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:293` |
| C010 | wave table: waves 1-66: monsters per wave (nibbler, bat, blob, melee, ranger, mager) | w1 3,1,0,0,0,0 ... w66 3,0,0,0,0,2; nibblers 3 except 6 on waves 3, 8, 17, 34 | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoWaveMappings.java:51` |
| C011 | wave table: waves 67 / 68 / 69 | 67: 1 JalTok-Jad; 68: 3 JalTok-Jad; 69: TzKal-Zuk | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoWaveMappings.java:117`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoWaveMappings.java:118`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoWaveMappings.java:119` |
| C012 | wave table: wave announcement chat line | game message "Wave: <n>" | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:488` |
| C025 | pillar: nibbler target rule | all of a wave's nibblers target one randomly chosen surviving pillar and ignore the player until none is left | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:303` |
| C027 | Jal-Nib: attack speed / range / size / style | 4 / 1 / 1 / crush | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:393` |
| C030 | Jal-MejRah: attack speed / range / style | 3 / 4 / ranged | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:394` |
| C032 | Jal-Ak: hitpoints / level / size / range | 40 / 165 / 3 / 15 | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:395` |
| C033 | Jal-Ak: attack cycle | 6 ticks (trainer: attackSpeed 3 doubled by the scan: scan, then attack 3 ticks later) | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:395` |
| C034 | Jal-Ak: scan rule (when it reads the prayer) | scans when it gains line of sight, or when its cooldown is <= 0 and no scan is held; the attack follows 3 ticks later | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:308` |
| C035 | Jal-Ak: style from the overhead read at the scan | Protect from Magic -> ranged; Protect from Missiles -> magic; no overhead (or melee) -> 50/50 magic or ranged | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:313` |
| C038 | Jal-Ak: bloblets on death | 3: range (+1,-1), melee (0,0), mage (+2,-2) from the blob SW tile; 15 hp, level 70; first attack after a cooldown of 4 | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoBlobDeathSpot.java:11` |
| C040 | Jal-ImKot: hitpoints / level / size / speed / range | 75 / 240 / 4 / 4 / 1 | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:396` |
| C043 | Jal-ImKot: dig and resurface timing | 6 ticks frozen digging; on resurfacing attackDelay 6 and frozen 2; observers use 12 ticks from the burrow animation to the next attack | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:288` |
| C045 | Jal-Xil: hitpoints / level / size / speed | 125 / 370 / 3 / 4 | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:397` |
| C046 | Jal-Xil: attack range | trainer/AUTOZUK 15 tiles; kotori Type.RANGER range 98 | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:397` |
| C049 | Jal-Zek: hitpoints / level / size / speed | 220 / 490 / 4 / 4 | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:398` |
| C054 | Jal-Zek: ticks after a revive | mager acts again after 8; the revived monster's first attack after attackSpeed (trainer) or attackSpeed+1 (AUTOZUK) | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:293` |
| C056 | JalTok-Jad: hitpoints / level / size | 350 / 900 / 5 | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoWaveMappings.java:131` |
| C057 | JalTok-Jad: attack speed | 8 on wave 67 and the Zuk-wave Jad; 9 on wave 68; kotori: 8 after the animation (6 with its sixTickJad option) | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:240` |
| C058 | JalTok-Jad: prayer must be up N ticks after the attack animation starts | 3 (JAD_PROJECTILE_DELAY = 3; kotori ticksAfterAnimation 3) | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:399`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:205` |
| C061 | JalTok-Jad: animations the plugin uses to tell the style | mage 7592, range 7593 (melee 7590 in TJS) | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:184`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:185` |
| C062 | Jad healers: spawn threshold | below 50% of hitpoints (175 of 350), once | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:400` |
| C064 | Yt-HurKot: hp / level / speed / size | 90 / 141 / 4 / 1 | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:400` |
| C069 | TzKal-Zuk: hitpoints / level / size | 1200 / 1400 / 7 | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoWaveMappings.java:119` |
| C070 | TzKal-Zuk: attack speed | 10; 7 when enraged (below 240 hp) | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:233`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:228` |
| C071 | TzKal-Zuk: first attack | trainer: attackDelay 14 and stun 8 at spawn; kotori: 12 ticks once the shield has reached its corner ("TODO: Could be 10 or 11. Test!") | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:513` |
| C076 | Zuk set timer: pause and resume | pauses once when Zuk is below 600 hp; Jad spawns and the timer resumes below 480 hp with +175 ticks (105 s) | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:995`, `inferno/src/main/java/com/theplug/kotori/inferno/InfernoSpawnTimerInfobox.java:38` |
| C079 | Zuk healers: Jal-MejJak threshold, count, tiles | below 240 hp: 4 Jal-MejJak at (16,9) (20,9) (30,9) (34,9), spawn delay 2 | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:377` |
| C081 | Ancestral Glyph (shield): hitpoints / size / speed / ends | 600 / 5 / 1 tile per tick / reverses when x < 11 or x > 35 (so it reaches x 10 and x 36 in the trainer frame), pausing at each end | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:845` |
| C082 | Ancestral Glyph (shield): pause at each end | trainer freeze(5) on reaching an end (x < 11 or x > 35); kotori ticksLeftInCorner 4 | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:761` |
| C086 | line of sight: algorithm and masks | Bresenham-style, 16.16 fixed point; masks NORTH 0x400, EAST 0x1000, SOUTH 0x4000, WEST 0x10000, FULL 0x20000; an NPC's LoS is tested from the player to the closest footprint tile; range 1 = melee adjacency | `inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:107` |

## Notes

* The plugin states no hitpoints; levels only as the wave table's monster key (`InfernoWaveMappings.java:125-141`), and attack ticks / ranges in `InfernoNPC.java:393-402`.
* `InfernoSpawnTimerInfobox.java:39-40` SPAWN_DURATION_WARNING 120 s and SPAWN_DURATION_DANGER 30 s are display-colour thresholds, not mechanics.
* `InfernoNPC.java:240-247` `sixTickJad` is a user option: with it the plugin assumes a 6-tick Jad cycle (the magic animation is longer than 6 ticks); the default is the 8-tick cycle.
* `InfernoPlugin.java:511-513` the plugin's own comment `// TODO: Could be 10 or 11. Test!` on the 12-tick first Zuk attack after the shield reaches its corner.
