# infernotrainer: OldSchoolSDK/InfernoTrainer (infernotrainer.com, Supalosa and contributors)

Pinned 2026-10-03 by the waves loop corpus worker (`corpus.code`). Entry in `../LEDGER_code.md`; every constant below is a row of `../CODE_CONSTANTS.md` (the row id is in the first column).

* **Repository**: https://github.com/OldSchoolSDK/InfernoTrainer
* **Commit**: 06fc103f70f1fa228678ca79910a8d3bb0798a7d (committed 2026-07-17). A clone of the same repository already sits in the owner's home directory (`~/Documents/git_repos/InfernoTrainer`); it was not used.
* **Licence**: GPL-3.0 (`LICENSE`, copied). package.json says ISC; the LICENSE file is GPL-3.0.
* **What it is for**: practising Inferno waves 1-69 in a browser.
* **Files copied** (byte for byte, licence header intact): `src/content/inferno/js/` InfernoWaves.ts (wave table, the nine spawn tiles, nibbler spawn block, spawn assignment), InfernoRegion.ts (arena, pillars, wave 67/68/69 setup, player starts, wave-complete timer), InfernoPillar.ts, ZukShield.ts, InfernoMobDeathStore.ts, InfernoHealerSpark.ts, Wall.ts, `mobs/` JalNib, JalMejRah, JalAk, JalAkRekKet/Mej/Xil, JalImKot, JalXil, JalZek, JalTokJad, YtHurKot, JalMejJak, TzKalZuk (stats, attack speeds, ranges, sizes, scan/dig/revive/heal rules); `test/simulations/ZukLineOfSight.test.ts`; UPSTREAM_README.md (the upstream README, renamed) and `sidebar.html` (what it credits). Not copied: loadouts, settings, models, sounds, rendering, JalTokJadAnim.
* **What it credits as its own sources**: README: built "from my interest in OSRS's Inferno" as a clean re-implementation of the engine; replays use the Inferno Stats plugin's spawns (https://github.com/InfernoStats). sidebar.html links the wiki Inferno page as "Mob Explanations". No other source is named; there is no per-number citation. Its own code carries hedges ("cheat way for now. pillar should AOE", "Blobs attack on a 6 tick cycle, but these mechanics are odd"). The npm dependency `osrs-sdk` 0.1.4 is the engine (see `osrs_sdk/`).

## Constants this source states (name, value, file and line)

The value column is the whole row of `../CODE_CONSTANTS.md`; a source often states only part of it (the notes below and the row's last column say which part).

| row | quantity | value | file:line (relative to this directory) |
|---|---|---|---|
| C002 | arena: playable grid | 29 x 30 tiles | `src/content/inferno/js/InfernoRegion.ts:370` |
| C010 | wave table: waves 1-66: monsters per wave (nibbler, bat, blob, melee, ranger, mager) | w1 3,1,0,0,0,0 ... w66 3,0,0,0,0,2; nibblers 3 except 6 on waves 3, 8, 17, 34 | `src/content/inferno/js/InfernoWaves.ts:154` |
| C011 | wave table: waves 67 / 68 / 69 | 67: 1 JalTok-Jad; 68: 3 JalTok-Jad; 69: TzKal-Zuk | `src/content/inferno/js/InfernoRegion.ts:442` |
| C013 | spawn: candidate spawn tiles (region-local SW tile of the footprint) | (18,41) (39,41) (20,35) (40,34) (33,29) (22,23) (40,21) (18,18) (32,18); world = region + (2240, 5312) [derived] | `src/content/inferno/js/InfernoWaves.ts:15` |
| C014 | spawn: rule assigning monsters to tiles | Fisher-Yates shuffle of the 9 tiles per wave; assigned in order mager(s), ranger(s), melee(s), blob(s), bat(s); nibblers separately | `src/content/inferno/js/InfernoWaves.ts:26`, `src/content/inferno/js/InfernoWaves.ts:57` |
| C015 | spawn: nibbler spawn block | 3x3 block region x 25..27, y 33..35; n tiles taken from a shuffle of 9 | `src/content/inferno/js/InfernoWaves.ts:127` |
| C017 | spawn: spawn stun (ticks before a new monster acts) | 1 for nib, bat, blob, melee, ranger, mager, Yt-HurKot; Zuk 8; Jal-MejJak 1; Jad = constructor option (1, or 1/4/7 on wave 68) | `src/content/inferno/js/mobs/JalNib.ts:37`, `src/content/inferno/js/mobs/TzKalZuk.ts:231`, `src/content/inferno/js/mobs/JalMejJak.ts:91`, `src/content/inferno/js/mobs/JalTokJad.ts:130` |
| C018 | spawn: next wave starts N ticks after the last monster dies | 9 ("1 extra tick to allow for bloblets") | `src/content/inferno/js/InfernoRegion.ts:687` |
| C019 | player start: player start tile (trainer frame) | waves 1-66 (28,17); wave 67 (18,25); wave 68 (25,27); wave 69 (25,15) | `src/content/inferno/js/InfernoRegion.ts:413`, `src/content/inferno/js/InfernoRegion.ts:446`, `src/content/inferno/js/InfernoRegion.ts:457`, `src/content/inferno/js/InfernoRegion.ts:485` |
| C020 | pillar: hitpoints | 255 | `src/content/inferno/js/InfernoPillar.ts:32` |
| C021 | pillar: size and positions | 3x3; south (21,37), west (11,23), north (28,21) in the trainer frame = region SW tiles (27,23) (17,37) (34,39) | `src/content/inferno/js/InfernoPillar.ts:209` |
| C022 | pillar: pillars present | waves 1-66 only (none on 67-69) | `src/content/inferno/js/InfernoRegion.ts:360` |
| C023 | pillar: nibbler damage to a pillar | floor(random*5) = 0..4 per hit, one hit per attack speed (4) | `src/content/inferno/js/mobs/JalNib.ts:12` |
| C024 | pillar: collapse effects | AUTOZUK: nibblers die, other monsters next to the pillar take floor(hp/2), the player next to it takes floor(current hp/2) | `src/content/inferno/js/InfernoPillar.ts:200` |
| C025 | pillar: nibbler target rule | all of a wave's nibblers target one randomly chosen surviving pillar and ignore the player until none is left | `src/content/inferno/js/InfernoWaves.ts:144` |
| C026 | Jal-Nib: hitpoints / level / defence | 10 / 32 / 15 | `src/content/inferno/js/mobs/JalNib.ts:52`, `src/content/inferno/js/mobs/JalNib.ts:33`, `src/content/inferno/js/mobs/JalNib.ts:49` |
| C027 | Jal-Nib: attack speed / range / size / style | 4 / 1 / 1 / crush | `src/content/inferno/js/mobs/JalNib.ts:89` |
| C028 | Jal-Nib: max hit vs player | 4 (damage 0..4) | `src/content/inferno/js/mobs/JalNib.ts:12` |
| C029 | Jal-MejRah: hitpoints / level / size | 25 / 85 / 2 | `src/content/inferno/js/mobs/JalMejRah.ts:47`, `src/content/inferno/js/mobs/JalMejRah.ts:25`, `src/content/inferno/js/mobs/JalMejRah.ts:87` |
| C030 | Jal-MejRah: attack speed / range / style | 3 / 4 / ranged | `src/content/inferno/js/mobs/JalMejRah.ts:79`, `src/content/inferno/js/mobs/JalMejRah.ts:83` |
| C031 | Jal-MejRah: max hit; run-energy drain per hit | 19; run energy -300 (trainer stat units) | `src/content/inferno/js/mobs/JalMejRah.ts:15` |
| C032 | Jal-Ak: hitpoints / level / size / range | 40 / 165 / 3 / 15 | `src/content/inferno/js/mobs/JalAk.ts:50`, `src/content/inferno/js/mobs/JalAk.ts:22`, `src/content/inferno/js/mobs/JalAk.ts:93`, `src/content/inferno/js/mobs/JalAk.ts:89` |
| C033 | Jal-Ak: attack cycle | 6 ticks (trainer: attackSpeed 3 doubled by the scan: scan, then attack 3 ticks later) | `src/content/inferno/js/mobs/JalAk.ts:81` |
| C034 | Jal-Ak: scan rule (when it reads the prayer) | scans when it gains line of sight, or when its cooldown is <= 0 and no scan is held; the attack follows 3 ticks later | `src/content/inferno/js/mobs/JalAk.ts:139` |
| C035 | Jal-Ak: style from the overhead read at the scan | Protect from Magic -> ranged; Protect from Missiles -> magic; no overhead (or melee) -> 50/50 magic or ranged | `src/content/inferno/js/mobs/JalAk.ts:116`, `src/content/inferno/js/mobs/JalAk.ts:114` |
| C036 | Jal-Ak: max hit (magic/ranged) | 29 | `src/content/inferno/js/mobs/JalAk.ts:124` |
| C038 | Jal-Ak: bloblets on death | 3: range (+1,-1), melee (0,0), mage (+2,-2) from the blob SW tile; 15 hp, level 70; first attack after a cooldown of 4 | `src/content/inferno/js/mobs/JalAk.ts:158`, `src/content/inferno/js/mobs/JalAkRekMej.ts:44` |
| C039 | Jal-AkRek (bloblets): hp / max hit / speed / range | 15 / 18 / 4 / melee 1, mage 15, range 15 (level 70) | `src/content/inferno/js/mobs/JalAkRekKet.ts:45`, `src/content/inferno/js/mobs/JalAkRekMej.ts:81` |
| C040 | Jal-ImKot: hitpoints / level / size / speed / range | 75 / 240 / 4 / 4 / 1 | `src/content/inferno/js/mobs/JalImKot.ts:46`, `src/content/inferno/js/mobs/JalImKot.ts:22`, `src/content/inferno/js/mobs/JalImKot.ts:83`, `src/content/inferno/js/mobs/JalImKot.ts:95` |
| C041 | Jal-ImKot: max hit | 49 (slash) | `src/content/inferno/js/mobs/JalImKot.ts:42` |
| C042 | Jal-ImKot: dig trigger | no line of sight and attackDelay <= -38 with 10% per tick, or <= -50 | `src/content/inferno/js/mobs/JalImKot.ts:113` |
| C043 | Jal-ImKot: dig and resurface timing | 6 ticks frozen digging; on resurfacing attackDelay 6 and frozen 2; observers use 12 ticks from the burrow animation to the next attack | `src/content/inferno/js/mobs/JalImKot.ts:129`, `src/content/inferno/js/mobs/JalImKot.ts:175` |
| C044 | Jal-ImKot: dig landing tile | first free of: (player.x-3, player.y+3), under the player, (x-3, y), (x, y+3), else (x-1, y+1) (trainer frame) | `src/content/inferno/js/mobs/JalImKot.ts:114` |
| C045 | Jal-Xil: hitpoints / level / size / speed | 125 / 370 / 3 / 4 | `src/content/inferno/js/mobs/JalXil.ts:68`, `src/content/inferno/js/mobs/JalXil.ts:33`, `src/content/inferno/js/mobs/JalXil.ts:109` |
| C046 | Jal-Xil: attack range | trainer/AUTOZUK 15 tiles; kotori Type.RANGER range 98 | `src/content/inferno/js/mobs/JalXil.ts:105` |
| C048 | Jal-Xil: projectile hit delay | trainer: SDK ranged formula floor((3+d)/6)+1 then +2 (reduceDelay -2); AUTOZUK calibrated table by distance from the 3x3 centre: d1-5 3, d6-9 4, d10-12 5, d13+ 6 (hit tick, attack tick = 1) | `src/content/inferno/js/mobs/JalXil.ts:56` |
| C049 | Jal-Zek: hitpoints / level / size / speed | 220 / 490 / 4 / 4 | `src/content/inferno/js/mobs/JalZek.ts:86`, `src/content/inferno/js/mobs/JalZek.ts:50`, `src/content/inferno/js/mobs/JalZek.ts:119`, `src/content/inferno/js/mobs/JalZek.ts:127` |
| C050 | Jal-Zek: max hit (magic / stab when adjacent) | 70 / 52 | `src/content/inferno/js/mobs/JalZek.ts:147` |
| C051 | Jal-Zek: flicker (visual tell) | 1 tick before the attack | `src/content/inferno/js/mobs/JalZek.ts:37` |
| C052 | Jal-Zek: resurrection chance per attack opportunity | 10% | `src/content/inferno/js/mobs/JalZek.ts:199` |
| C053 | Jal-Zek: revive: hp, once, who, which waves | returns at floor(maxhp/2); each corpse once; nibblers and bloblets excluded; none on wave 69 | `src/content/inferno/js/mobs/JalZek.ts:204`, `src/content/inferno/js/InfernoMobDeathStore.ts:9`, `src/content/inferno/js/mobs/JalZek.ts:64` |
| C054 | Jal-Zek: ticks after a revive | mager acts again after 8; the revived monster's first attack after attackSpeed (trainer) or attackSpeed+1 (AUTOZUK) | `src/content/inferno/js/mobs/JalZek.ts:217`, `src/content/inferno/js/mobs/JalZek.ts:207` |
| C055 | Jal-Zek: revive tile | first free tile scanning x 26..32, y 24..36 (trainer frame), fallback (21,22) | `src/content/inferno/js/mobs/JalZek.ts:159`, `src/content/inferno/js/mobs/JalZek.ts:169` |
| C056 | JalTok-Jad: hitpoints / level / size | 350 / 900 / 5 | `src/content/inferno/js/mobs/JalTokJad.ts:157`, `src/content/inferno/js/mobs/JalTokJad.ts:141`, `src/content/inferno/js/mobs/JalTokJad.ts:238` |
| C057 | JalTok-Jad: attack speed | 8 on wave 67 and the Zuk-wave Jad; 9 on wave 68; kotori: 8 after the animation (6 with its sixTickJad option) | `src/content/inferno/js/InfernoRegion.ts:450`, `src/content/inferno/js/InfernoRegion.ts:464`, `src/content/inferno/js/mobs/TzKalZuk.ts:154` |
| C058 | JalTok-Jad: prayer must be up N ticks after the attack animation starts | 3 (JAD_PROJECTILE_DELAY = 3; kotori ticksAfterAnimation 3) | `src/content/inferno/js/mobs/JalTokJad.ts:33` |
| C059 | JalTok-Jad: style choice | 50% ranged / 50% magic; melee (stab) 50% of attacks while adjacent | `src/content/inferno/js/mobs/JalTokJad.ts:262` |
| C060 | JalTok-Jad: max hit (magic) | 113 | `src/content/inferno/js/mobs/JalTokJad.ts:274` |
| C062 | Jad healers: spawn threshold | below 50% of hitpoints (175 of 350), once | `src/content/inferno/js/mobs/JalTokJad.ts:170` |
| C063 | Jad healers: count | 5 on wave 67; 3 per Jad on wave 68; 3 on the Zuk-wave Jad | `src/content/inferno/js/InfernoRegion.ts:450`, `src/content/inferno/js/InfernoRegion.ts:464`, `src/content/inferno/js/mobs/TzKalZuk.ts:156` |
| C064 | Yt-HurKot: hp / level / speed / size | 90 / 141 / 4 / 1 | `src/content/inferno/js/mobs/YtHurKot.ts:64`, `src/content/inferno/js/mobs/YtHurKot.ts:46`, `src/content/inferno/js/mobs/YtHurKot.ts:96`, `src/content/inferno/js/mobs/YtHurKot.ts:108` |
| C065 | Yt-HurKot: heal per attack on Jad | trainer: random 0..19 (damage = -floor(random*20)) | `src/content/inferno/js/mobs/YtHurKot.ts:16` |
| C066 | Yt-HurKot: placement when spawned | random offset around Jad: waves 67/68 x -5..+5, y -5..+9 less size; Zuk wave x 0..5, y -(0..3) less size; retried while the tile holds a monster | `src/content/inferno/js/mobs/JalTokJad.ts:185`, `src/content/inferno/js/mobs/JalTokJad.ts:182` |
| C067 | Jad wave 67: positions (trainer frame) | Jad (23,27), player (18,25) | `src/content/inferno/js/InfernoRegion.ts:449` |
| C068 | Jad wave 68: positions and stun offsets | Jads (18,24) (28,24) (23,35); stun shuffle of [1,4,7]; attack speed 9 | `src/content/inferno/js/InfernoRegion.ts:459`, `src/content/inferno/js/InfernoRegion.ts:463`, `src/content/inferno/js/InfernoRegion.ts:477` |
| C069 | TzKal-Zuk: hitpoints / level / size | 1200 / 1400 / 7 | `src/content/inferno/js/mobs/TzKalZuk.ts:245`, `src/content/inferno/js/mobs/TzKalZuk.ts:215`, `src/content/inferno/js/mobs/TzKalZuk.ts:292` |
| C070 | TzKal-Zuk: attack speed | 10; 7 when enraged (below 240 hp) | `src/content/inferno/js/mobs/TzKalZuk.ts:277` |
| C071 | TzKal-Zuk: first attack | trainer: attackDelay 14 and stun 8 at spawn; kotori: 12 ticks once the shield has reached its corner ("TODO: Could be 10 or 11. Test!") | `src/content/inferno/js/mobs/TzKalZuk.ts:72`, `src/content/inferno/js/mobs/TzKalZuk.ts:231` |
| C072 | TzKal-Zuk: max hit | trainer 251 (magicMaxHit) | `src/content/inferno/js/mobs/TzKalZuk.ts:223` |
| C073 | TzKal-Zuk: projectile | typeless; setDelay 4, visualDelayTicks 2; ignores protection prayers (isBlockable false) | `src/content/inferno/js/mobs/TzKalZuk.ts:44`, `src/content/inferno/js/mobs/TzKalZuk.ts:31` |
| C074 | TzKal-Zuk: target choice | hits the shield unless the player is left of the shield's x, 5 or more tiles right of it, or north of y 16 (trainer frame y > 16): then the player | `src/content/inferno/js/mobs/TzKalZuk.ts:201`, `src/content/inferno/js/mobs/TzKalZuk.ts:204` |
| C075 | Zuk set timer: first set and period | 72 ticks after spawn, then every 350 ticks | `src/content/inferno/js/mobs/TzKalZuk.ts:66`, `src/content/inferno/js/mobs/TzKalZuk.ts:125` |
| C076 | Zuk set timer: pause and resume | pauses once when Zuk is below 600 hp; Jad spawns and the timer resumes below 480 hp with +175 ticks (105 s) | `src/content/inferno/js/mobs/TzKalZuk.ts:140`, `src/content/inferno/js/mobs/TzKalZuk.ts:145`, `src/content/inferno/js/mobs/TzKalZuk.ts:146` |
| C077 | Zuk set contents: mager + ranger per set | 1 Jal-Zek at (20,21) after a 7-tick spawn delay and 1 Jal-Xil at (29,21) after 9, both aggro on the shield (trainer frame) | `src/content/inferno/js/mobs/TzKalZuk.ts:127`, `src/content/inferno/js/mobs/TzKalZuk.ts:129` |
| C078 | Zuk Jad: spawn threshold and kit | below 480 hp: 1 Jad at (24,25), speed 8, stun 1, 3 healers, spawn delay 7, aggro on the shield | `src/content/inferno/js/mobs/TzKalZuk.ts:149`, `src/content/inferno/js/mobs/TzKalZuk.ts:157` |
| C079 | Zuk healers: Jal-MejJak threshold, count, tiles | below 240 hp: 4 Jal-MejJak at (16,9) (20,9) (30,9) (34,9), spawn delay 2 | `src/content/inferno/js/mobs/TzKalZuk.ts:165`, `src/content/inferno/js/mobs/TzKalZuk.ts:168` |
| C080 | Jal-MejJak: hp / level / speed / heal / spark | 75 / 250 / 3 / heals Zuk random 0..24 / 3 sparks per volley, 5..10 each, landing after 2 ticks | `src/content/inferno/js/mobs/JalMejJak.ts:121`, `src/content/inferno/js/mobs/JalMejJak.ts:100`, `src/content/inferno/js/mobs/JalMejJak.ts:158`, `src/content/inferno/js/mobs/JalMejJak.ts:22`, `src/content/inferno/js/InfernoHealerSpark.ts:20`, `src/content/inferno/js/mobs/JalMejJak.ts:73` |
| C081 | Ancestral Glyph (shield): hitpoints / size / speed / ends | 600 / 5 / 1 tile per tick / reverses when x < 11 or x > 35 (so it reaches x 10 and x 36 in the trainer frame), pausing at each end | `src/content/inferno/js/ZukShield.ts:49`, `src/content/inferno/js/ZukShield.ts:168`, `src/content/inferno/js/ZukShield.ts:144`, `src/content/inferno/js/ZukShield.ts:148`, `src/content/inferno/js/ZukShield.ts:152` |
| C082 | Ancestral Glyph (shield): pause at each end | trainer freeze(5) on reaching an end (x < 11 or x > 35); kotori ticksLeftInCorner 4 | `src/content/inferno/js/ZukShield.ts:149` |
| C083 | Ancestral Glyph (shield): start and direction | spawn (23,13), frozen 1 tick, direction random per run | `src/content/inferno/js/ZukShield.ts:38`, `src/content/inferno/js/ZukShield.ts:35`, `src/content/inferno/js/InfernoRegion.ts:489` |
| C084 | Zuk arena: geometry (trainer frame) | Zuk SW (22,8) size 7; walls at x=21 and x=29 for y 0..8; player start (25,15); tile markers at y=14, safe x 14/20/30/36, unsafe 16-18 and 32-34 | `src/content/inferno/js/InfernoRegion.ts:492`, `src/content/inferno/js/InfernoRegion.ts:494`, `src/content/inferno/js/InfernoRegion.ts:513`, `test/simulations/ZukLineOfSight.test.ts:55` |
| C085 | Jal-Zek: no revival on wave 69 | revive disabled when wave >= 69 | `src/content/inferno/js/mobs/JalZek.ts:64` |
| C089 | combat: retaliation delay | a retaliating NPC waits floor(speed/2)+1 ticks (Jad flinchDelay 2) | `src/content/inferno/js/mobs/JalTokJad.ts:229` |
| C091 | Zuk safespots: pre-enrage safe and danger tiles on the north row (region 9043 coordinates) | safe (grey) region (20,46) (26,46) (36,46) (42,46); danger (red) (22,46) (23,46) (24,46) (38,46) (39,46) (40,46); labelled UNSAFE (31,45); other grey markers (33,43) (32,29) (26,34) | `src/content/inferno/js/InfernoRegion.ts:513` |
