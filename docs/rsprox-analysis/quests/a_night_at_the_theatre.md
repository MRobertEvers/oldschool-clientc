# A Night at the Theatre, from RSProx

Written 2026-10-20. The quest's content (`quest_nightatthetheatre/scripts/*.rs2`,
`minigame_tob/scripts/tob_natt.rs2`) was rebuilt from two recordings:

| session | rev | world | what it shows |
|---|---|---|---|
| index 1679 = rsprox.net 1695 | 226.1 | w367, normal | the whole quest, [9603] first talk .. [11201]; the recorder had beaten the Theatre and took the **skip** |
| index 3 = rsprox.net 101 | 227.1 | w367, Leagues V | the whole quest again, with the Theatre of Blood **played** in Entry Mode solo, [22597] .. [27545] |

Every page's text and chathead expression in the content was checked against the
recorded pages (`tools/rsprox/rsprox_pages.py`): 317 of 317 match. Branches no
recording reaches (re-talks, lost items, full packs, refusals, Daer Krand's own
talk, the in-Theatre lines) are the wiki transcripts' (Transcript:A Night at the
Theatre rev 15245915, Transcript:Mysterious Stranger, Transcript:Daer Krand) and
are marked `[wiki]` in the scripts; the few lines with no source at all are
marked `[unverified]`.

The cutscenes were drafted with `tools/rsprox/rsprox_cutscene.py` (new: a
transcript window -> RuneScript with every tile instance-relative, every camera
packet, actor add/walk/face/seq/spotanim/say, loc change, sound, fade and page as
sent), then read against the transcript.

## The varbit ladder

`%varb12276_tobquest` moves in steps of two, written on the tick of the action
that earns it:

| value | tick (1679) | event |
|---|---|---|
| 2 | 9616 | "Yes." to "Start the A Night at the Theatre quest?" |
| 4 | 9628 | "...So where do I come in?" |
| 6 | 9657 | "Now, I must remain here..." |
| 8 | 9664 | "Oh... Oh god." (talk over) |
| 10 | 9683 | into the crypt (instance) |
| 12 | 9733 | "You unlock the gate." (the key is used up) |
| 14 | 9749 | the head in the pack |
| 16 | 9791 | the head given |
| 18 | 9871 | memory 1 (Maiden) ends |
| 20 | 9886 | "...north of Darkmeyer. I'll see if I can find it." |
| 22 | 9914 | into the spider cave (instance; `tobquest_in_instanced_cave` = 1 at 9909) |
| 24 | 9947 | the skeleton's sticky note |
| 26 | 9950 | the note read |
| 28 | 10168 | Daer Krand's acid (`tobquest_given_acid` = 1) |
| 30 | 10224 | the acid melts the sac, the eggs taken |
| 32 | 10262 | the eggs given |
| 34 | 10326 | memory 2 (Bloat, athanatos) ends |
| 36 | 10367 | memory 3 (Vasilias) ends |
| 38 | 10384 | "Perfect. I will await your return." |
| 40 | 10513 | the Nature Spirit names the island |
| 42 | 10599 | the first Chop wakes the hespori (instance) |
| 44 | 10759 | the hespori dead, the instance given back |
| 46 | 10768 | the bark chopped |
| 48 | 10798 | the bark given |
| 50 | 10832 | memory 4 (Sotetseg) ends |
| 52 | 10899 | memory 5 (Xarpus, Queen Nylocas) ends |
| 54 | 10947 | "Okay, I think we can make this work." |
| 56..80 | 10957 | the skip: every step in one tick |
| 82 | 11049 | memory 6 (the doctor) ends |
| 84 | 11106 | the Mysterious Master's last line |
| 86 | 11112 | the scroll, four `tobquest_lamp`, `quest_complete_2` |

The Theatre, played (index 3): 56 on the door's teleport [25677]; per room the
start (barrier) and the wave line: Maiden 58 [25703] / 60 [25754], Bloat 62 / 64,
Nylocas 66 / 68, Sotetseg 70 / 72, Xarpus 74 / 76, Verzik 78 [27022] / 80 [27274];
`tobquest_done_tob` = 1 on the next talk [27338]. The cache's multilocs read the
same ladder: the coffin's Search shows at 10..14, the skeleton's at 22..26, the
egg sac Open at 22..28 and Search (re-take) at 30, the hespori Chop (ready) at
40..42 and (dead) at 44..46 (`multilocN` is value N-1).

## The memories (frame common to all seven)

T = the click on "Prepare yourself!" (the Stranger's red-eyed head, 10877):
fade to black (clientscript 948 `0,200,0,0,50`) with an empty chatbox; T+2 the
copy of the room (`tob_template_*`), fov_clamp 3, cutscene_status 1, minimap 2,
gravestone hidden, ops hidden, side panels closed, the player in the copy, the
cast added; T+3 the memory's music; T+4 the first camera; T+5 a red veil
(`0,0,5570560,170,50`). End E: veil to black (`5570560,170,0,0,50`), the step
written; E+2 everything back, the player on 3672,3223 facing west, camera reset;
E+4 the black lifts (`0,0,0,200,50`) to Ver Sinhaza's music (556).

| memory | window (1679) | room | music | notes |
|---|---|---|---|---|
| 1 Maiden | 9799..9877 | m49_69 | 570 | vyrewatch walks in 7 west; Verzik added, Ranis walks 15 west under a 2/1 glide |
| 2 Bloat + athanatos | 10273..10331 | m51_69 | 578 | upper floor a tick, then ground; the Bloat glides his loop through the pages (npc timer), then is removed |
| 3 Vasilias | 10335..10390 | m51_66 | 580 | `top_spider_melee_spawn_noloop` as he rises |
| 4 Sotetseg | 10803..10838 | m51_67 | 584 | |
| 5 Xarpus, Queen | 10841..10905 | m49_68 L1 | 567 | the baby queen bites (`npc_araxyte01_attack`) and dies |
| 6 the doctor | 10990..11062 | m49_67 | 702 | `verzik_phase2_death` delay 15, a red flash (948 to 11141120), the Queen and 13 nylocas; the doctor's end heard, not seen |
| 7 the Master | 11073..11112 | m51_92 L1 | 147 | the Stranger in memory form 4 tiles north; the blank page uses `chatchant1` |

## The crypt, the cave, the island

- Crypt (9667..9767): staircase question "It could be dangerous down there. Are
  you sure you want to enter?"; four quest vyrewatch (female_2 3671,9638,
  male_1 3675,9647, male_2 3683,9639, female_1 3687,9645); the kill with the
  Ivandis flail (`myq3_vampire_grounded_attack` every 4, death
  `myq3_vampire_grounded_death`) drops vampyre dust and the key
  ("<col=ef1020>Untradeable drop: Crypt key</col>"); the gate becomes an
  `inviswall` with its open leaf north for two ticks; the coffin's sawing
  (`apple_splat` at 0/20/30/50 under a black fade).
- Cave (9905..9989, 10188..10240): the cave mouth's question; sixteen `araxyte_baby`
  placed on entry, passive; the sticky note on `note` 291 lines 1..11; Daer Krand
  in the Sisterhood Sanctuary (acid menu hidden while the acid is held [10172]);
  the acid on the sac swaps the acid's slot for the eggs, and the cave's araxytes
  ambush the thief (venom at [10227]).
- Island (10578..10768): two `exactmove` hops on the stepping stone (30..45
  cycles, `human_spot_jump` and `jump` at 15); the fight in a copy of m54_52; four
  open flowers (`hespori_healer_dead_to_alive`), immune hespori ("The hespori is
  currently immune to your attacks."), magic (`hespori_attack_special`, proj
  1965, impact 1966, end 115+5d cycles) and ranged (`hespori_attack_ranged`, two
  1964 at 30..55+5d and 55..80+5d) every 6 ticks, 9 magic / 15 ranged recorded;
  the flowers reopen at 66% [10657] and 33% [10712]; the vines (proj 1967, impact
  1968, five "loosen" lines then free); death `hespori_death`, the copy given back
  [10759]; the chop (5 ticks) gives the bark.

## Wiki-sourced numbers ([wiki], unverified by a recording)

Vyrewatch level 105 (90 hp, max 10, speed 4); the quest hespori (360 hp, def 120,
mage 126, range 150, magic max 14 / 3 prayed, ranged 8+8 / 2+2 prayed, vines 40+
after 10 ticks); flowers 10 hp; araxyte level 96 (60 hp, max 13, speed 4, venom);
the lamp's level-50 floor; the in-Theatre Stranger lines; Daer Krand's own talk.

## Getting there, and the grotto

- Ver Sinhaza has no walk from the mainland. The test travels the way a player
  who finished A Taste of Hope does: Drakan's medallion, Teleport > Ver
  Sinhaza. Recorded (index 225 [6077]..[6080]): `human_castteleport` and
  `teleport_casting` (92) on the press, the teleport to 3649,3230,0 three
  ticks later (`general/scripts/enchanted_jewellry/drakans_medallion.rs2`).
  The cache gives the medallion `ifop1=Wear`, `ifop2=Teleport` and four
  `subaction=2,N` entries. Only Ver Sinhaza is recorded, so Darkmeyer, Slepe
  and Castle Drakan are refused, not guessed.
- A submenu entry goes out as IF_SUBOP, which is IF_BUTTONX plus the entry
  0-based (the deob's send writes `var6 - 1`). Index 4286 [427] pins the
  base: a ring of dueling's Rub `subop=1` landed at Castle Wars, its
  `subaction=4,2`. The server now stores `last_subop` 1-based, as every
  content reader (giantsoul, xeric talisman, ring of shadows, ...) compares
  it. Before, no client sent IF_SUBOP at all, and the server kept the wire
  byte, which is one lower than every reader expects.
- The grotto (index 1679 [10500]): "You prepare to enter the Druid's grotto.",
  the teleport to 3442,9734,1 and, on the same tick, the Nature Spirit (944)
  added at 3441,9738,1. A player past Nature Spirit had no spirit to talk to
  before; the grotto door adds it (`quest_druidspirit.rs2`).


## The wire audit (2026-10-20)

Our side is the quest test's tick log (`t.ticklog.start()`, one mark per leg;
`tools/rsprox/ticklog_tells.py`), the recording's is
`tools/rsprox/rsprox_tells.py --timeline` over the same section. Every row of
both was given a verdict. "Kit" = the recorder's own gear or actions.

### The crypt (1679 [9664]..[9770]; index 3 [23741]..)

| recorded | ours before | verdict |
|---|---|---|
| `myq3_vampire_grounded_attack` every 4 ticks; death seq two ticks after the last swing; key + dust two ticks after that | same | match |
| `vyrewatch_vampire_attack` (888) with each swing (6/6), `vyrewatch_vampire_hit` (883) the tick a blow lands (6/6), `vyrewatch_vampire_death` (889) | nothing: every vyrewatch's sounds were `null` in the generated npc block | **fixed** (`nightatthetheatre.npc`) |
| no vyrewatch defend seq on a tick it is struck and not swinging (0/4; index 3 0/3) | `myq3_vampire_grounded_defend_male` on every hit | **fixed** (`defend_anim,null`) |
| `pick2` on the key; `unlock` + `grate_open` and the gate's leaf for 2 ticks | same | match |
| coffin: `apple_splat` at 0/20/30/50 **and `barbassault_splat` at 70** | the four splats only | **fixed** |
| `sword_hit2_*` / `leather_hit_*` (delay 20) on the recorder's blocks | `human_block_1` / `human_hit_*` | engine-wide: the player's hit sound follows their worn armour in both recordings; ours never does. **Open**, not quest content |
| spotanim 1542 (`sanguinesti_staff_heal`) on the vyrewatch once | none | kit: 1 of 13 flail hits across both recordings, none in index 3 |
| `human_castteleport` + `teleport_all` r5 at [9764] | - | kit (the recorder's teleport out) |

### The cave (1679 [9890]..[10262]; index 3 [24334]..)

| recorded | ours before | verdict |
|---|---|---|
| `human_longcrawl` + `climbing_loop` x4 delay 4, in and out | same | match |
| `pick2` at the skeleton | same | match |
| one message, "...egg sac.<br>You take some strange spider eggs..." | two `mes` lines | **fixed** |
| `liquid` and **`lore_inferno` delay 20** on the theft | `liquid` only | **fixed** |
| the cave's 16 `araxyte_baby` (no Attack op, no levels) stay put; **16 new fighters** -- 10 `araxyte_normal` (level 96), 6 `araxyte_large` (level 146, size 2) -- added on a fixed schedule (+0 x3, +1 x4, +2 x4, +4, +5, +7, +11, +13), each with `top_spider_melee_spawn_noloop` and a face 3 tiles out, the same type per slot in both recordings within 0..2 tiles | four `araxyte_baby` hatched and every baby set on the player | **fixed** (natt_cave.rs2 `natt_cave_ambush_step`) |
| `insect_attack` (537) on every bite | nothing | **fixed** (the fighters' `attack_sound`) |
| `clock_tick_1`, `tol_homonculus_crying` | - | engine (stamina expiry warnings) |
| `vampire_suck` r6 at 3635,3383; `smokepuff` beside another player | - | world / another player |

### The memories (1679 [9799]..[11112])

| recorded | ours before | verdict |
|---|---|---|
| every fade: 948, `cc_deleteall` (2249) on hpbar_hud:container and hpbar_hud:hp hidden, on one tick (41/41) | 948 only | **fixed** (`~natt_fade`) |
| `invisible_type8_nonblocking` under the boss with the copy: Maiden 26,28; Bloat 31,29; Nylocas 26,16 and 37,16 (rot 1); Xarpus 35,45 L1 | missing | **fixed** |
| the other locs (chain hook, barriers, Xarpus door, six pillars) | same | match |
| memory 5: `insect_attack` / `insect_death`; memory 6: `mud_pie_impact`, the doctor's end at 0/10/20/30/40/50 | same | match |
| clientscripts 4517/4518/4716/4717/4722/5485/3145/2498/5224 on the cast tick | - | engine refreshes on a teleport, not the memory's |

### The island (1679 [10575]..[10770]; index 3 [24661]..)

| recorded | ours before | verdict |
|---|---|---|
| `human_spot_jump` + `jump` delay 15, twice | same | match |
| the rise: four flowers `hespori_healer_dead_to_alive`, `hespori_spawn` seq and sound | same | match |
| **a swing every 6 ticks from 3 after the rise** (26 in 1679, 11 in index 3) | **one swing, then nothing**: the engine clears an AP mode before firing `[ai_applayer2]` and ours never re-selected it | **fixed** |
| magic until the player first strikes the hespori, then ranged with ~1 magic in 9 (both recordings) | 50/50 | **fixed**, inferred from 2 recordings |
| projectile heights: magic and vines 85 -> 39, ranged 24 -> 14 (v1 packets: our units) | 21 -> 10 and 6 -> 4 (divided by 4 by a wrong rule in the skill) | **fixed**; the skill corrected |
| ranged `hespori_range` at 20 and 45; magic `hespori_magic_cast_fire`, `hespori_magic_hit` at the landing | same | match |
| vines: **`hespori_entangle`** on the cast, **`hespori_vines`** with the impact, **`hespori_vine_no_move`** on each tug (5/5) | the magic's cast sound; no landing or tug sound | **fixed** |
| `hespori_defend` on every hit taken; flowers die and reopen twice (12 deaths) | same | match |
| `hespori_death_1` with the death seq | nothing | **fixed** (`death_sound`) |
| the bark: **`snap`** with "You chop some bark" | nothing | **fixed** |
| projectile 337 (`banshee_scream`) player -> hespori, spotanim 1542, `light_creature_down` | - | kit (none in index 3) |

### Getting there

| recorded | ours before | verdict |
|---|---|---|
| any cast teleport: `teleport_all` (200) as an area sound on the cast tick, **range 10 from a spellbook, 5 from an item** (14 sessions) | silent | **fixed** in `~player_teleport_normal` (items, 5) and the new `~player_teleport_spell` (spellbooks, 10); ancient/Arceuus given 10 unverified |

### The Stranger in the Theatre (index 3 [25677]..)

| recorded | ours before | verdict |
|---|---|---|
| appears at the room's tile (Maiden 55,23; Bloat 46,39; Nylocas 31,45; Sotetseg 23,13; Xarpus 35,25 L1; Verzik 28,12) | same | match |
| wanders within 2 tiles (four rooms); never moves in Verzik's | wanders 5 everywhere | **fixed in config** (`wanderrange=2`, Verzik `defaultmode=none`), not yet seen on our wire (the Theatre leg is blocked, below); Xarpus's reads 22, a translation artefact, unverified |

### Verified on our wire

Run 19 (2026-10-20, the private packs of every fix above): the crypt, cave,
grotto and island legs pass, and their tick log shows each fixed row as
recorded -- the vyrewatch's three sounds and no defend seq; the ambush's
sixteen adds at +0/+1/+2/+4/+5/+7/+11/+13 with their spawn seq and
`insect_attack` on every bite; the hespori swinging every six ticks from three
after the rise (magic first), its projectiles at 85 -> 39 / 24 -> 14 with
distance-scaled ends, the vines' three sounds, `hespori_death_1`, `snap`.
The vines' rate was then halved (one recorded event; unverified) and land
four ticks after the cast as recorded.

Not re-run on the latest packs: the Theatre and the finish. They passed before
(runs 16 and 17); on 2026-10-20 16:50 another session's in-progress rev-241
migration rewrote the quest driver's var names (`varp6568_tob_active`,
`varp6969_damagestyle`, ...), which a rev-239 run cannot resolve, and the
Maiden's solver has failed on every run since, with any packs.

### Open

- The player's own hit/block sound follows their worn armour in both
  recordings (`sword_hit2_*`, `leather_hit_*`); ours always plays
  `human_block_1` / `human_hit_*`. Engine-wide, every npc fight.
- The hespori's style rule (magic until first struck, then ~1 in 9) and the
  vines' rate are inferred from two recordings.
