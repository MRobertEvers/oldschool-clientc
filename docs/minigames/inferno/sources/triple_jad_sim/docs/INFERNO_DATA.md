# Inferno reference data

Numbers and behaviour collected 2026-09-04 from the OSRS wiki, the OpenOSRS inferno plugin (BSD-2), InfernoStats and inferno-scouter (BSD-2), runemarkers (MIT), and — as a behavioural reference only, no code copied — OldSchoolSDK/InfernoTrainer and osrs-sdk (GPL-3.0, "IT"/"SDK" below). Anything marked IT is that trainer's model, not confirmed in-game.

Sources: wiki pages Inferno, Inferno/Strategies, TzKal-Zuk, each monster page, Damage_per_second (Melee/Ranged/Magic), Maximum_ranged_hit, Maximum_magic_hit, Twisted_bow, Zaryte_crossbow, Toxic_blowpipe, Hit_delay, Attack_range, Attack_speed, Prayer, Prayer_flicking, Game_tick; https://github.com/JourneyDeprecated/OpenOSRS (inferno plugin); https://github.com/InfernoStats/InfernoStats; https://github.com/jeremiah855/inferno-scouter; https://github.com/jamiegyoung/runemarkers (entities/inferno.json); https://github.com/OldSchoolSDK/InfernoTrainer; https://github.com/OldSchoolSDK/osrs-sdk; https://detuks.com/blog/autozuk-inferno-wave-solver.

## 1. Arena and coordinate frames

- Region 9043, world base (2240, 5312). Region coords below: x east, y north, 0..63.
- Scout grid (InfernoStats / scouter): `x = regionX - 17`, `y = 46 - regionY`.
- IT local: `x = regionX - 6`, `y = 60 - regionY` (y grows south). IT region size 51 x 57, blockers at x=10, x=40, y=13, y=44.
- Playable area: region x 17..46, y 17..46 (30 x 30).

Spawn tiles (NPC south-west corner), 9 slots in scouter reading order (y descending, then x ascending):

| slot | region | scout | IT | note |
| --- | --- | --- | --- | --- |
| 1 | 18,41 | 1,5 | 12,19 | NW |
| 2 | 39,41 | 22,5 | 33,19 | NE |
| 3 | 20,35 | 3,11 | 14,25 | W |
| 4 | 40,34 | 23,12 | 34,26 | E |
| 5 | 33,29 | 16,17 | 27,31 | centre |
| 6 | 22,23 | 5,23 | 16,37 | SW |
| 7 | 40,21 | 23,25 | 34,39 | SE |
| 8 | 18,18 | 1,28 | 12,42 | far SW |
| 9 | 32,18 | 15,28 | 26,42 | S |

- Nibbler spawn block: region x 25..27, y 33..35 (3x3 centre block).
- Pillars (3x3, "Rocky support", object ids 30353/30354/30355, NPC ids 7709 and 7710 dying, 255 HP, 4 visual states per 25%): SOUTH centre region (28,24); WEST centre (18,38); NORTH centre (35,40). Remaining pillars auto-collapse at the start of wave 67; collapse damages adjacent players and monsters (wiki gives both "49" and "half current HP").
- Player start (IT local): waves 1-66 (28,17); wave 67 (18,25); wave 68 (25,27); wave 69 (25,15).
- Runemarkers Zuk tiles (region, y=46 row): safe grey x 20, 26, 36, 42; red x 22, 23, 24, 38, 39, 40; unsafe middle (31,45); other grey (33,43), (32,29), (26,34).

## 2. Waves

Columns: nibbler, bat (Jal-MejRah), blob (Jal-Ak), meleer (Jal-ImKot), ranger (Jal-Xil), mager (Jal-Zek). Identical across the wiki, IT and the OpenOSRS plugin.

```
w1  [3,1,0,0,0,0]  w2  [3,2,0,0,0,0]  w3  [6,0,0,0,0,0]  w4  [3,0,1,0,0,0]  w5  [3,1,1,0,0,0]
w6  [3,2,1,0,0,0]  w7  [3,0,2,0,0,0]  w8  [6,0,0,0,0,0]  w9  [3,0,0,1,0,0]  w10 [3,1,0,1,0,0]
w11 [3,2,0,1,0,0]  w12 [3,0,1,1,0,0]  w13 [3,1,1,1,0,0]  w14 [3,2,1,1,0,0]  w15 [3,0,2,1,0,0]
w16 [3,0,0,2,0,0]  w17 [6,0,0,0,0,0]  w18 [3,0,0,0,1,0]  w19 [3,1,0,0,1,0]  w20 [3,2,0,0,1,0]
w21 [3,0,1,0,1,0]  w22 [3,1,1,0,1,0]  w23 [3,2,1,0,1,0]  w24 [3,0,2,0,1,0]  w25 [3,0,0,1,1,0]
w26 [3,1,0,1,1,0]  w27 [3,2,0,1,1,0]  w28 [3,0,1,1,1,0]  w29 [3,1,1,1,1,0]  w30 [3,2,1,1,1,0]
w31 [3,0,2,1,1,0]  w32 [3,0,0,2,1,0]  w33 [3,0,0,0,2,0]  w34 [6,0,0,0,0,0]  w35 [3,0,0,0,0,1]
w36 [3,1,0,0,0,1]  w37 [3,2,0,0,0,1]  w38 [3,0,1,0,0,1]  w39 [3,1,1,0,0,1]  w40 [3,2,1,0,0,1]
w41 [3,0,2,0,0,1]  w42 [3,0,0,1,0,1]  w43 [3,1,0,1,0,1]  w44 [3,2,0,1,0,1]  w45 [3,0,1,1,0,1]
w46 [3,1,1,1,0,1]  w47 [3,2,1,1,0,1]  w48 [3,0,2,1,0,1]  w49 [3,0,0,2,0,1]  w50 [3,0,0,0,1,1]
w51 [3,1,0,0,1,1]  w52 [3,2,0,0,1,1]  w53 [3,0,1,0,1,1]  w54 [3,1,1,0,1,1]  w55 [3,2,1,0,1,1]
w56 [3,0,2,0,1,1]  w57 [3,0,0,1,1,1]  w58 [3,1,0,1,1,1]  w59 [3,2,0,1,1,1]  w60 [3,0,1,1,1,1]
w61 [3,1,1,1,1,1]  w62 [3,2,1,1,1,1]  w63 [3,0,2,1,1,1]  w64 [3,0,0,2,1,1]  w65 [3,0,0,0,2,1]
w66 [3,0,0,0,0,2]
w67: 1 JalTok-Jad (attack speed 8, 5 healers at <=50% HP)
w68: 3 JalTok-Jad (speed 9, attacks 3 ticks apart, 3 healers each), triangle around centre
w69: TzKal-Zuk + shield; sets of Jal-Xil + Jal-Zek; Jad at <480 HP; 4 Jal-MejJak at <240 HP
```

Spawn rules:
- Monsters spawn on tick 15 of every wave (detuks). Positions are random per wave; there is no relation between waves.
- IT model: Fisher-Yates shuffle of the 9 tiles, then assign in order mager(s), ranger(s), meleer(s), blob(s), bat(s). Nibblers: shuffle the 9-tile centre block, take n. All nibblers of a wave target one randomly chosen surviving pillar.
- Mager revives near the centre of the arena / south side of the north pillar. IT: first free tile scanning x 26..32, y 24..36 (IT local), fallback (21,22).
- Next wave starts 9 ticks after the last monster dies (IT; allows bloblets). Logging out pauses between waves.
- Spawn stun: on spawn all monster actions are frozen 1 tick (Zuk 8, Jad configurable) (IT).

## 3. Monsters

All: 40% water weakness, immune to poison and venom (Jal-Xil / AkRek-Xil: poison 0%, venom 100%). NPC max hit formula `eff = level + 9; max = floor((eff * (bonus + 64) + 320) / 640)` reproduces every listed value.

| name | id | cb | hp | att | str | def | mag | rng | size | style | speed | max | range | anims |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Jal-Nib | 7691 | 32 | 10 | 1 | 1 | 15 | 15 | 1 | 1 | crush | 4 | 4 | 1 | 7574 |
| Jal-MejRah | 7692 | 85 | 25 | 0 | 0 | 55 | 120 | 120 | 2 | ranged | 3 | 19 | 4 | attack 7578, stand 7577 |
| Jal-Ak | 7693 | 165 | 40 | 160 | 160 | 95 | 160 | 160 | 3 | magic / ranged / crush if adjacent | 6 | 29 | 15 | range 7581, melee 7582, magic 7583 |
| Jal-AkRek-Ket | 7696 | 70 | 15 | 120 | 120 | 95 | 1 | 1 | 1 | crush | 4 | 18 | 1 | |
| Jal-AkRek-Xil | 7695 | 70 | 15 | 1 | 1 | 95 | 1 | 120 | 1 | ranged | 4 | 18 | 15 | |
| Jal-AkRek-Mej | 7694 | 70 | 15 | 1 | 1 | 95 | 120 | 1 | 1 | magic | 4 | 18 | 15 | |
| Jal-ImKot | 7697 | 240 | 75 | 210 | 290 | 120 | 120 | 220 | 4 | slash, no diagonal | 4 | 49 | 1 | attack 7597, burrow 7600 |
| Jal-Xil | 7698 / 7702 | 370 | 125 | 140 | 180 | 60 | 90 | 250 | 3 | ranged / crush if adjacent | 4 | 46 / 19 melee | unlimited | melee 7604, range 7605 |
| Jal-Zek | 7699 / 7703 | 490 | 220 | 370 | 510 | 260 | 300 | 510 | 4 | magic / stab if adjacent | 4 | 70 / 52 melee | unlimited | mage 7610, melee 7612, revive 7611 |
| JalTok-Jad | 7700 / 7704 | 900 | 350 | 750 | 1020 | 480 | 510 | 1020 | 5 | ranged / magic / stab if adjacent | 8 (w67, w69), 9 (w68), melee 4 | 113 | | melee 7590, mage 7592, range 7593 |
| Yt-HurKot | 7701 / 7705 | 141 | 90 | 165 | 125 | 100 | 150 | 150 | 1 | crush | 4 | 18 | 1 | |
| TzKal-Zuk | 7706 | 1400 | 1200 | 350 | 600 | 260 | 150 | 400 | 7 | typeless single projectile | 10, enraged 7 | 148 (mage 128, range 169 components) | | attack 7566 |
| Ancestral Glyph | 7707 | | 600 | | | | | | 5 wide (IT) | | | | |
| Jal-MejJak | 7708 | 250 | 75 | 1 | 1 | 100 | 1 | 1 | 1 | ground AoE | 3 | 10 | | 2858 |

Offensive bonuses: Jal-MejRah ranged att 30 / str 30; Jal-Ak str 45, mbns 45, rngbns 45; AkRek 25 in their style; Jal-ImKot str 40; Jal-Xil ranged att 40 / str 50; Jal-Zek mbns 80; Jad magic att 100, mbns 75, ranged att 80; Zuk str 200, magic att 550, mbns 450, ranged att 550, rngbns 200.
Defence bonuses: Jal-Nib -20 all; Jal-MejRah melee 30, magic -20, range 45; Jal-Ak 25 all; AkRek 25 in own style; Jal-ImKot melee 65, magic 30, range 50; Jal-Xil / Jal-Zek / Jad 0; Yt-HurKot melee 0, magic 100, range 100; Zuk stab/slash/crush 0, magic 350, ranged 100.

Behaviour:
- Jal-Nib: spawns in the centre block, targets a random intact pillar, 2-4 damage to pillars every 4 ticks, 100% accuracy vs player; ignores the player until all pillars are gone.
- Jal-MejRah: range 4; each hit drains 3 run energy and may drain all combat stats (not Prayer) by 1, boosting its own. Blocked by Protect from Missiles. Plugin cue: attack animation + LOS + range (defence animation bug).
- Jal-Ak (blob): checks the player's overhead when in range or 3 ticks after its last attack, attacks with the opposite style 3 ticks later (6-tick cycle); no overhead: 50/50 (IT). Melee (crush) if adjacent incl. diagonal. Can hit through LOS once scanned (IT). On death spawns 3 bloblets: Xil at (+1,-1), Ket at same SW tile, Mej at (+2,-2), each with 4-tick cooldown (IT); bloblets attack on spawn tick unless they must move.
- Jal-ImKot: melee only, cannot hit diagonally. Digs 50 ticks after wave start then every 40-60 ticks, not if attacked within the last 15 ticks; 6-tick delay before attacking after resurfacing (wiki). IT: dig when no LOS and attackDelay <= -50 (or <= -38 with 10%/tick); dig animation 6 ticks frozen; lands preferring NW of player (x-3, y+3), then under, then W, then N; after landing attackDelay 6, frozen 2. Plugin: burrow anim 7600 then next attack in 12 ticks.
- Jal-Xil: ranged, melee (crush) when adjacent incl. diagonal (IT 50%). Projectile leaves 2 ticks after the animation; hit delay floor((3+d)/6)+1 plus 2 (IT).
- Jal-Zek: magic, melee (stab) when adjacent incl. diagonal. 1/10 chance per attack opportunity to revive a dead monster instead of attacking (each revivable once, at half HP; not nibblers/bloblets; not on wave 69); resumes 8 ticks after; the revived mob's first attack is 8 ticks after revive, same tick as the mager.
- Pathing/LOS: monsters larger than 1x1 align their SW corner with the player when they cannot attack; a monster with LOS and in range stops moving; corner safespot rule: player N/S of corner, monster E/W. Barrages hit relative to the SW tile. IT: step SW corner toward the player on both axes, else x-only, else y-only; random sidestep when the player is under the mob; LOS = the RuneLite/Woox bresenham mask algorithm.
- Retaliation: an NPC retaliates ceil(speed/2)+1 ticks after your hit initiates combat (IT: floor(speed/2)+1; Jad 2).

## 4. Jad waves

- w67: 1 Jad at IT (23,27), 5 healers. w68: 3 Jads at IT (18,24), (28,24), (23,35), spawn stuns randomised among 1/4/7 ticks (rotation direction 50/50), 3 healers each, attacks 3 ticks apart giving a 9-tick cycle.
- Healers (Yt-HurKot) spawn at <=50% (175 HP), heal 15-24 every 4 ticks at melee distance, stop when attacked, die with Jad. IT placement: random offsets within +-5 x, y -5..+9 around Jad.
- Cues: magic = rears on hind legs and breathes fire, sound at animation start; ranged = brief rear then ground slam, boulder from the ceiling, sound when you should already be praying; melee (adjacent only) = head bash, no warning. Plugin: prayer must be on 3 ticks after the animation starts, next attack 8 ticks later without an animation reset. IT: 50/50 range/magic each attack, melee 50% if adjacent.

## 5. Zuk (wave 69)

- 1200 HP; attack speed 10, 7 when enraged (<240 HP). One projectile per attack rolling 0..148 (average of magic max 128 and ranged max 169); accuracy is the average of Zuk's ranged and magic accuracy vs the average of the player's ranged and magic defence; ignores protection prayers; HP is checked when the hit lands (no tick-eating).
- Shield (Ancestral Glyph, 600 HP vs minions, blocks Zuk indefinitely) moves east-west between the wall ends and reverses, pausing at the ends. IT: 1 tile/tick, x 11..35 (IT local), 5-wide, 5-tick pause at each end, 1-tick initial freeze, start (23,13), direction random. Wiki: direction is fixed per run since 2018; resetting the wave changes the timing. If destroyed the player is fully exposed and minions retarget the player 2 ticks later (IT).
- IT geometry: Zuk SW (22,8) size 7, walls at x=21 and x=29 (y 0..8), player start (25,15). Zuk hits the shield unless the player is outside the shield's x span or y > 16, then hits the player. Initial attack delay 14, spawn stun 8; projectile 4 ticks, visible after 2.
- Sets: 1 Jal-Xil + 1 Jal-Zek spawn behind the player and attack the shield until attacked. First set after the shield's first full rotation (IT timer 72 ticks then 350). Period ~3:30 = 350 ticks. Timer pauses when Zuk <600 HP and 175 ticks are added once; resumes when Jad spawns at <480. IT positions: mager (20,21) delay 7, ranger (29,21) delay 9. No revives on 69.
- Jad at <480 HP, 3 healers, speed 8, IT position (24,25), delay 7, targets the shield first.
- 4 Jal-MejJak at <240 HP on the lava (IT x 16, 20, 30, 34 at y=9): heal Zuk 15-24 every 3 ticks each until tagged; then throw 3 lava sparks per volley every 3 ticks into the player box (3x3 splash, 5-10 unprayable damage); untargetable for 2 ticks after the first tag (IT).
- Two rock outcrops give 6 tiles (3 per side) with no LOS to Zuk; attacking from them drags you to the middle.
- Safespots: four consistent safespots pre-enrage because the 10-tick attack and the shield cycle align. IT markers at y=14: green x 14, 20, 30, 36; red 16, 17, 18, 32, 33, 34.
- On Zuk's death all remaining minions die.
- Weapon ranges: tbow / bowfa 10, ACB / ZCB 8, other crossbows 7, blowpipe 5, longrange +2 (max 10).

## 6. Combat formulas

- Effective level: `floor(floor((lvl + boost) * prayer) + style + 8)`, then x1.1 void (x1.125 elite ranged str, magic void x1.45). Style: accurate +3 (att / ranged), aggressive +3 str, controlled +1, defensive +3 def; NPCs +1 (+9 total). Powered staff accurate +3, longrange +1 magic.
- Prayer multipliers: melee att 1.05 / 1.10 / 1.15, Chivalry 1.15, Piety 1.20; str 1.05 / 1.10 / 1.15, Chivalry 1.18, Piety 1.23; ranged att 1.05 / 1.10 / 1.15, Deadeye 1.18, Rigour 1.20; ranged str Rigour 1.23; magic acc 1.05 / 1.10 / 1.15, Vigour 1.18, Augury 1.25; magic damage +1 / 2 / 3 / 4%; defence Rigour / Augury / Piety 1.25, Chivalry 1.20, Steel Skin 1.15.
- Max hit melee: `floor((eff_str * (str_bonus + 64) + 320) / 640)` then gear (slayer 7/6, salve 7/6 or 1.2, not stacking). Ranged: `floor(floor(0.5 + eff_rstr * (rstr_bonus + 64) / 640) * gear)`; slayer (i) 1.15, salve (i) 7/6, (ei) 1.2; tbow multiplier stacks multiplicatively. Magic: `floor(base * (1 + additive % incl. magic damage, void 5%, prayer))` then x1.15 slayer (i), floor after each step; Ice Barrage 30, Blood Barrage 29, Blitz 26 / 25.
- Attack roll: `floor(eff_att * (bonus + 64) * gear)`; tbow accuracy multiplier applied to the roll. NPC defence roll `(def + 9) * (style_def_bonus + 64)`; magic `(9 + npc_magic) * (magic_def + 64)`. Player defence roll `eff_def * (bonus + 64)`; player magic defence uses 70% magic level + 30% defence (verify).
- Hit chance: `A > D ? 1 - (D + 2) / (2 (A + 1)) : A / (2 (D + 1))`. Damage uniform 0..max, capped by HP; magic min 1 on success.
- Twisted bow: `M = min(max(target magic lvl, target magic acc bonus), 250)`; `acc% = 140 + (10 * 3M/10 - 10) / 100 - ((3M/10 - 100)^2) / 100` clamp 0..140; `dmg% = 250 + (10 * 3M/10 - 14) / 100 - ((3M/10 - 140)^2) / 100` clamp 0..250. Speed 5 rapid / 6, range 10.
- Zaryte crossbow: 6 / 5 rapid, range 8 (10 longrange); spec 75%: 2x accuracy, guaranteed bolt proc, bolt effects +10%: ruby 22% of current HP cap 110, diamond 1.26x, onyx 1.32x, dragonstone 1.45.
- Toxic blowpipe: rapid 2 ticks vs NPCs, accurate / longrange 3; range 5 / 7; +30 ranged att, +20 str plus dart str; venom 25% per shot; spec 50%: +100% acc, +50% dmg, heals floor(dmg/2).
- Other speeds: bowfa 5 / 4 rapid, range 10; ACB / ZCB / rune xbow 6 / 5, range 8 (rcb 7); spells 5, range 10; tridents / sang 4, range 7 (+2); shadow 5, range 8; scythe 5; rapier 4.
- Slayer helm (i): 7/6 melee, 1.15 ranged and magic; TzKal slayer helm is cosmetic.
- Prayer drain: counter += drain per tick; when counter > `60 + 2 * prayer_bonus` lose 1 point (protect prayers 12/tick, Rigour / Augury / Piety 24/tick). Redemption heals floor(prayer/4) at <=10% HP.

## 7. Ticks and ordering

- Tick 600 ms. Player hit delays: melee 0; bow / xbow `1 + floor((3 + d) / 6)`; blowpipe / thrown `1 + floor(d / 6)`; spells and powered staves `1 + floor((1 + d) / 3)`; shadow +1. NPCs are processed before players each tick so hits on NPCs land one tick later.
- Plugin ticksAfterAnimation (animation start to hit / cycle): Nib 4, Bat 3, Blob 6, Meleer 4, Ranger 4, Mager 4, Jad 3 then 8 / 9 cycle, Jad healer 4, Zuk 10 / 7. Ranges: Bat 4, Blob 15, Meleer 1, Ranger / Mager 98.
- SDK tick order: prayer pretick, entities (pillars), mobs (timers, movement incl. incoming projectiles, attacks), new mobs, delayed actions, region projectiles, players (timers, movement, incoming attacks, attack, food, regen), mid-tick, dead removal. Prayer toggles apply at the next tick start; not drained on the activation tick, so a 1-tick flick has zero drain.
- Player attack cooldown = weapon speed after each attack (global; switching weapons keeps the remaining delay). Eating adds 3 ticks to the attack delay; food, potion and karambwan each delay 3 ticks with combo order food, potion, karambwan.

## 8. InfernoTrainer replay and known gaps

- RNG is Math.random; "replay" serialises the shuffled 9-tile list into the URL (`?wave=N&spawns=[{x,y}...]` in IT local coords); custom spawns `?wave=0|1|74&mager=[[x,y]]&ranger=...` in scout coords. Scouter codes: 9 letters (o empty, Y bat, B blob, R ranger, X melee, M mager, optional rank digit) + pillar HPs.
- Open accuracy issues: Zuk max hit 148 vs 251, healer sparks too strong, far-SW ranger LOS, set timer reset, slayer helm ranged bonus. IT vs wiki: Zuk def 234 vs 260, Yt-HurKot magic / range def 130 vs 100, Jal-ImKot range def 5 vs 50.

## 9. Unresolved (verify in game)

- Exact shield speed and end-pause; exact set / Jad / MejJak spawn tiles on 69; pillar collapse damage; blob style with no overhead; Zuk first attack tick; player magic defence formula; NPC projectile ids.
