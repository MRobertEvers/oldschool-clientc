# The Great Olm: recorded behaviour

Read from the normal solo kills 3282, 843 and 3285, plus the trio and four-man raids in 820. 3389 is Challenge Mode and is excluded from damage. All are on Grid Master worlds; see [README.md](README.md).

In the Olm chamber the claws sit at x 3220 (west wall) or 3238 (east wall). On the west wall the head is at z 5738, the left claw at 5743 and the right at 5733; on the east wall the claws swap. The status column is our content's state on 2026-10-18. What changes with more than one raider is under [Parties](#parties).

## Damage

| Attack | Recorded | Ours | Status |
|---|---|---|---|
| Auto (magic or ranged), right prayer | 5 | max 27 at 20% | matches |
| Burn With Me | 5 a hit | 5 | matches |
| Crystal burst | 30 seen; shunts the raider one tile (`agilityarena_player_spikedback` 1114), "The crystal beneath your feet grows rapidly and shunts you to the side." | 10–40 | in range |
| Falling crystal on the raider's tile | 15–22 (n=11 solo; 14–22 in the party raids) | was 16–25 / 15–20 | **fixed: 15–22** |
| Falling crystal one tile away | 9–14 (n=10 solo, 44 in all) | was 10–16 | **fixed: 9–14** |
| Crystal bomb | exact 13 a ring: 26 at two tiles, 13 at three (21 of 21) | was 60/45/30/15 | **fixed: 52/39/26/13**, nothing past 3 |
| Teleport | 1 + ⌊4.3 × tiles⌋ (Chebyshev): 5, 9, 13, 18, 22, 26, 56 for 1–6 and 13 tiles; no cap. The hit shows the tick **after** the move. | was 5 a tile, max 50, same tick | **fixed** |
| Solo teleport destination | any chamber tile (13 tiles seen); the wiki says "a random tile anywhere in the chamber" | was within 10 | **fixed** |
| Sphere landing | 6 ticks after the cast, every time (5 cases) | was 4 | **fixed: 6** |

Not seen yet: an unprayed auto, a lightning hit (every bolt was dodged), more than one wrong-prayer sphere hit (34), and the acid pool's own hit (the 1–6 ticks are poison splats, id 65).

## Projectiles

Times are client cycles (30 a tick): `start → end`. "Peak" is the projectile's slope, which rsprox calls `angle`; "progress" is the field of the same name. Raw heights are shown with our byte (÷4) in brackets.

| Projectile | Start → end | Peak | Progress | Height |
|---|---|---|---|---|
| `olm_firebreath_travel` (1339), magic auto | 32 → 46 + 5 × tiles | 5 | 0 | head → 160 (40) |
| `olm_generic_range_proj` (1340), ranged auto | 32 → 46 + 5 × tiles | 5 | 0 | head → 160 (40) |
| `olm_weak_{melee,range,mage}_proj` (1345/1343/1341), spheres | 32 → **180** whatever the distance | 5 | 0 | head → 160 |
| `olm_acid_spit` (1354) at a tile | 32 → 46 + 10 × tiles | 5 | 0 | head → 0 |
| `olm_acid_spit` (1354) smothering the raider | 32 → 46 + 5 × tiles | 0 | 0 | head → 160 |
| `olm_burnwithme_travel` (1349) | 32 → 46 + 10 × tiles | 0 | 0 | head → 160 |
| `olm_healme_spit` (1355), siphon | 32 → 46 + 10 × tiles | 5 | 0 | head → 0 |
| `olm_firewall_travel` (1347), head to the wall's start | 32 → 60 | 5 | 0 | head → 0 |
| `olm_firewall_travel_2` (1348), the wall spreading | 60 → 90 | ⌊50 × (10 − tiles) / 9⌋: 50, 44, 38, 33, 27, 22, 16, 11, 5 for tiles 1–9 | 0 | 0 → 0 |
| `olm_crystalrock_falling` (1357) | 0 → 120 | 10 | **64** | 1000 (250) → 0 |
| `olm_crystal_bomb_travel` (1352), bombs and the crystal power's crystal | 0 → 60 | 10 | **64** | 1000 (250) → 0 |

- **Head height** is 340 (85) facing the middle and 355–360 (89) facing a side.
- **Falling crystals and the crystal power's crystal** come down from a random one of the 8 tiles around their target; every direction is seen.

## Graphics

| Graphic | Where / when |
|---|---|
| `olm_shockwave` (1356) | the lightning, a map graphic per tile |
| `olm_crystal_explode` (1358), height 20 | a falling crystal landing |
| `gargboss_debris_shadow_120` (1447) | map graphic under a falling crystal |
| `olm_crystalwave_shatter` (1353), height 10 | crystal burst |
| `raids_olm_central_shattercrystal` (1338) | crystal power landing |
| `olm_crystal_bomb_explode` (40), height 40 | a bomb going off |
| `olm_healme_spotanim` (1363) | each siphon pool, **sent with the spit**, delayed to its landing (delay = the spit's end) |
| `olm_playerswap_0` (1359) | on the raider every 2 ticks before a teleport, and as a map graphic on the destination |
| `olm_burnwithme_pl_spot` (1351), height 110 | on a burning raider |
| `olm_weak_*_impact` (1342/1344/1346), height 64 | a sphere landing |
| `sp_attackglow_red` (246) | on the crystal power's target, the tick after its crystal is thrown |

## Sounds

All are area sounds, `loops=1`. The id is followed by our name from `pack/4_soundeffects.pack`.

| Id | Name | Delay | Range | Where | Event |
|---|---|---|---|---|---|
| 3887 | `godwars_saradomin_magic_impact` | 0 | 3 | each `olm_shockwave` tile | lightning (1,033 of 1,033) |
| 1021 | `100_cauldron_explodes` | 0 | 4 | the impact tile | falling crystal lands |
| 3834 | `godwars_avatar_footsteps` | 0 | 15 | the target tile | falling crystal thrown |
| 3749 | `dragonslayer_dragonball` | 12 | 13 | the head's face | magic auto |
| 1784 | `bane_tolna_spit` | 12 (10 for spheres) | 13–15 | the head's face | ranged auto, acid, spheres |
| 585 | `dragonbreath` | 0 | 13 | the head | Burn With Me |
| 3750 | `dragonslayer_dragonbreath` | 12 | 13 | the head's face | flame wall |
| 1551 | `enak_barrier` | 0 | 15 | the left claw | every left-claw cast |
| 3821 | `kr_smash_window` | 0 | 2–3 | the tile | crystal shatter, crystal power landing |
| 324 | `brawler_cry` | 12 | 15 | the target | crystal power: "chosen you as its target" |
| 3405 | `fi_trollking_roar` | 15 (rise), 0 (death) | 15 | the head | rising; head death |
| 200 | `teleport_all` | 0 | 3 | the raider | teleport |

## Messages (exact)

| Text | When |
|---|---|
| `The Great Olm rises with the power of <col=00ff00>acid</col>.` / `<col=ff0000>flame</col>` / `<col=ff00ff>crystal</col>` | with the spawning locs at each rise (crystal was `006000` in ours: fixed) |
| `The Great Olm's left claw clenches to protect itself temporarily.` | clench; **no colour tag** (ours had `ef1020`: fixed) |
| `The Great Olm is giving its all. This is its final stand.` | final stand; no colour tag (fixed) |
| `As the Great Olm collapses, the crystal blocking your exit has been shattered.` | 2 ticks after the final `olm_head_death_front` |
| `The Great Olm had no one to pair you with! The magical power will enact soon...` | solo teleport cast (ours said "The Olm has…": fixed) |
| `As you had no pairing... you are taken to a random spot.` | solo teleport |
| `The teleport attack has no effect!` | teleport that does not move you (ours: "had no effect on you.": fixed) |
| `<col=ff0000>The Great Olm has chosen you as its target - watch out!</col>` | crystal power |
| `The falling crystal shatters into you.` | the crystal power's crystal landing on you (added) |
| `The crystal beneath your feet grows rapidly and shunts you to the side.` | crystal burst |
| `<col=ff0000>The Great Olm has smothered you in acid. It starts to drip off slowly.` | acid drip |
| `The Great Olm infects you with a burning overwhelming power.` | Burn With Me lands (added); the raider also says `Burn with me!` overhead |
| `You feel the deep burning inside dissipate.` | burn ends |
| `The Great Olm prepares to absorb energy from anyone unprotected...` / `Your health is sapped!` | siphon cast / siphon hit (added) |
| `<col=ff3045>…sphere of aggression…</col>`, `<col=229628>…accuracy and dexterity…</col>`, `<col=3366ff>…magical power…</col>` + ` Your prayers have been sapped.` | spheres (our colours were `ff0000`/`00ff00`/`0000ff`: fixed) |
| `<col=ff0000>You've been electrocuted to the spot!</col>`, `You've been injured and can't use protection prayers!` | lightning |
| `The Great Olm sounds a cry...` | |
| `The Great Olm is currently protecting its left claw! You cannot see a way to attack it successfully.` | |

**Never sent:** `The Great Olm retreats into the depths, shaking the chamber.` It appears in no recorded kill, phase changes included, so ours no longer sends it.

## Lifecycle animations

Times are ticks, measured over all three normal solo kills.

| Step | Recorded |
|---|---|
| Rise | `olm_*_spawning` locs + the "rises with the power of" message at T. Spawn animations at T+1 (sometimes T+2): `olm_head_spawn`, or `olm_head_spawn_enraged` in the last phase, `olm_hand_left_spawn`, `olm_hand_right_spawn`. Risen locs (`olm_head`, `olm_hand_*`) 5 ticks after the animations. |
| First attack | Phase 1: on, or 1 tick after, the risen swap. Later phases: 3–4 ticks after it. |
| A claw dies | `olm_hand_*_death`, then its loc becomes `olm_hand_*_cave` 2 ticks later. |
| The last claw dies (phases 1–2) | `olm_head_death_front` 3–4 ticks after the claw's death. The head loc becomes `olm_head_cave` 2 ticks after that. The next rise's spawning locs come 34 ticks after the head's death. |
| Right claw dies, left alive | Left claw plays `olm_hand_left_gripping_spawn1` 3–4 ticks later. If it was clenched: `olm_hand_left_unstunned` at +2, then `gripping_spawn2` at +4. From then the left claw idles in `olm_hand_left_gripping_idle` and casts with `olm_hand_left_gripping_attack_{stun,earthshock,earthquake}`. |
| Left claw dies first | No right-claw counterpart; there is no gripping set for the right claw. |
| Clench | `olm_hand_left_stunned` with the message, `olm_hand_left_stunned_idle` 2 ticks later, `olm_hand_left_unstunned` about 44 ticks on. |
| Left-claw casts | `olm_hand_left_cast_{stun,earthshock,earthquake,heal}`, every 16 ticks in a phase, in that order. |
| Final death | `olm_head_death_front`, then the collapse message 2 ticks later. |

**Turning:** each turn animation goes from the same stance to the same stance in ours as in Jagex's. Non-enraged: `turn_left`/`turn_right` front→side, `*_reverse` side→front, `turn_left_right`/`turn_right_left` side→side. Enraged: `rotate_*` in the same pattern. Ours matched before this pass.

## Lifecycle in ours (2026-10-18, fixed and re-measured)

| Step | Before | Now (`cox_olm_solo` seeds olm1, olm3) |
|---|---|---|
| Spawning locs → spawn animations | +10 | +1 |
| Spawning locs → risen locs | +15 | +6 |
| Risen → first head action, phase 1 | +4 | +1 |
| Risen → first head action, later phases | +3 | +3 (a turn when the raider is out of view, then the attack) |
| "rises with the power of" message | at the risen swap | with the spawning locs |
| Last claw's death → head `death_front` | same tick | +3 |
| Head death → head cave | +3, with all three caves re-sent | +2, the head's only |
| Head death → next spawning locs | 23 (+10 wait) | +34 |
| Right claw dies, left alive | nothing | `gripping_spawn1` +3, or `gripping_spawn2` +4 from a clench; gripping casts and `gripping_idle` after |
| Claw idle after a cast, `stunned_idle` after a clench | never sent | +2 |

The claws' cave loc 2 ticks after their death, the clench (stunned to unstunned in 45 ticks; Jagex's is 44) and the turn mapping already matched. The constants are `^cox_olm_rise_wait_first` (9), `^cox_olm_rise_wait_after_fall` (11), `^cox_olm_first_act_p1` (6), `^cox_olm_fall_after_claw` (3), `^cox_olm_head_cave_after` (2), `^cox_olm_grip_after` (3), `^cox_olm_grip_after_clench` (4) and `^cox_olm_claw_idle_after` (2), with the scheduler in `~cox_olm_lifecycle_tick`.

The fight's init now clears only Olm's own register slots (0–79). It used to clear 96, which ran into the raid layout's registers 112–122.

## Parties

Read from 820's four-man fight (t4759–t9387), re-transcribed with every raider shown. The archive's transcripts print the recorder alone (`PLAYER_INFO_LOCAL_PLAYER_ONLY`); `tools/cox/rsprox_olm_party.py` says how to re-transcribe into a private RSProx home, and runs every check below. Rules are judged on the tile each raider **started** the tick on: Olm acts before the players move.

**This fight scaled as a 54-player party.** Grid Master added 50 to the party's 4 (`raids_client_partysize_scaled` = 54). Hitpoints and phase count follow the scaled size: claws 10,200 each, head 13,600 (`hpbar_hud_basehp`; osrs-dps-calc's 600 + 300f and 800 + 400f with f = 50 − 3 × ⌊50 / 8⌋ = 32; this line used to say 10,202 / 13,602), and 9 hand phases (3 + ⌊54 / 8⌋). Everything else below follows the real 4: 6 bolts, not 31. Ours scales by `cox_scaling.rs2` (osrs-dps-calc's formula), which gives both recorded sizes exactly (1: 800 / 600; 54: 13,600 / 10,200).

### The head

| Rule | Recorded (820) | Ours before | Now |
|---|---|---|---|
| Autos | at every raider in the faced view, nobody else: 409 of 410 | the same | unchanged |
| Turn target | the view with the most raiders, counting each **view** (the boxes overlap: the separating row, z 36, is in all three); middle wins a tie, the wall decides north/south | counted south/north **of** the separating row, so a raider on it counted only for the middle | **by view**: 500 of 505 null/special steps (the old rule: 462); solo 98 of 103 (85) |
| Hand steer | a claw whose hit landed in the **6 ticks** before the step adds one to its side; a heal splat (the penultimate protection) is not a hit | a hit launched since the last step (4 ticks) | **6 ticks, by the landing** (`~cox_olm_hand_landed` via `~cox_on_damage`) |
| Empty faced view | the head turns and the step is lost (3 of 3) | the same | unchanged |

The five turn decisions the rule misses: t4770 (one raider in the chamber, a claw hit on the step's own tick), t4842, and three tied S/N cases (t5026, t9050, t9154) that go against the wall's tie-break.

### Specials

| Special | Recorded | Ours before | Now |
|---|---|---|---|
| Crystal burst | a seedling under every raider (76 of 82 casts), **never under a raider on an acid pool** (8 of the 13 left out stood on one; 5 unexplained) | every raider | skips an acid-pool tile |
| Lightning | **4 bolts solo, 6 in the four-man, one more in the penultimate phase** (5 / 7, every cast); each in its own column among **ten** (x 12–21), from either end. It follows the party, not who is in the chamber (t4794: one raider inside, 6 bolts) | 4 always (5 at 6+), nine columns | 4 + party / 2, +1 penultimate, ten columns |
| Teleport, solo | colour on the raider at +0 +2 +4 +6 +8, on the tile at +2 … +8, the move at **+8**, the hit at +9 (8 casts) | +1 … +7, move +9 | as recorded |
| Teleport, party | the raiders in the chamber in **random pairs** (a new pairing every cast), a colour a pair (`olm_playerswap_0`, `_1`; four exist); "You have been paired with `<col=ff0000>`name`</col>`! The magical power will enact soon..."; the colour on both at +0 … +8; the pair **swap tiles** at +10 (31 casts) or +9 (18; nothing seen separates them), `telescroll_teleport` on both; each hit the tick after by the solo formula on the distance between them (13 each for 3 tiles). On one tile: "The teleport attack has no effect!". An odd raider out is not picked (the one 3-raider cast; edb0ys's guide says so) | every raider got a solo random tile | as recorded, the swap at +10 |
| Spheres | each colour goes out with a chance of ½ (at least one), each at a **different** raider in view: with 3 in view, 1/2/3 spheres 9/8/3 times (expected 8.6/8.6/2.9) | one at every raider in view | as recorded |

### Phase attacks

| Attack | Recorded | Ours before | Now |
|---|---|---|---|
| Acid drip, Burn With Me, fire wall, crystal power | one **random** raider in the faced view (the crystal power's target was in view on all 16 casts) | the first raider hunted | random in view |
| Burn With Me spreading | on each burn hit, the burn passes to every raider **within one tile** (same tile included) who is not burning (`olm_burnwithme_travel_pl`, 0 → 30 cycles, slope 20, height 120 both ends); the new burn is **5 hits**, the first 8 ticks on, with no message. A burn that ends on that hit leaves its raider open to catch it again that tick (t6606). 23 spreads | never spread | as recorded |
| Crystal power | 11 crystals **one every 2 ticks** (160 of 160 gaps, solo too), the first 1 or 2 ticks after the cast (8 / 8), each landing 2 ticks after its throw | one a tick | as recorded, the start 1 or 2 at random |
| Acid spray | 7–10 pools solo (n=7), 7–15 in the four-man (n=25), not tied to the raiders in view | 7–12 | 7–12 + random(party) |
| Crystal bombs | none thrown in 820 | 2 (3 at 8+) | unchanged |

### The crystal showers (transition and final stand)

A beat of 4 ticks: a **volley** at random tiles, one in each of the three views solo and two in each in the four-man, then 2 ticks later **one crystal on a random raider's tile**. A transition's beats run from 6 to 34 ticks after the head's death (8 volleys and 7 aimed: 31 crystals solo, 55 in the four-man). The final stand's run from its first tick until the head dies. Two kills of three started with the volley; 843 started with the aimed crystal. Ours threw up to 4 random tiles a tick. Now it follows the beat, (party + 1) / 2 a view (one solo, two for a four-man). Each crystal lands on the head's own npc queue (`ai_queue7` the burst, `ai_queue8` the hit), so a party's volleys are not capped by registers.

### Proved on our server

- `test/raids/cox_olm_party.lua` (`--bots 3`), 10 of 10 seeds:
  - the head turns south for two raiders on the separating row and one at z 30, on both walls;
  - every auto goes at exactly the raiders in the faced view;
  - 5 bolts in distinct columns;
  - a seedling under each raider;
  - one pair of three swaps at +10 and takes 1 + ⌊4.3d⌋ at +11; on one tile, no swap and no hit;
  - the burn spreads to the raider beside its target and never to the one four tiles off.
- `test/raids/cox_olm_solo.lua`: 5 of 5 seeds green. Its own model checks (autos, specials, the clock) show the same misses on HEAD content as on this change. They come from the lifecycle retiming earlier on 2026-10-18 (`^cox_olm_first_act_p1` 6 against the solver's `FIRST_P1` 9), not from this pass.

## Index 4392 (rev 241, normal world, 2026-10-26)

The first normal-world, rev-241 Olm kill (solo; the recorder kills the mage hand first, so the left claw's
gripping casts appear). Where it disagrees with the Grid Master rows above, it wins. Tools: `tools/rsprox_tells.py
--anchor 7550-7555,7558 <idx4392> --ours <ticklog>`; `tools/cox/rsprox_olm_party.py <idx4392> turns`; the steer and
scuffed replays below (scratch scripts, logic summarised).

### The head's steer (fixed)

| Rule | 4392 (155 null/special turns) | rsprox.net 3282 / 3285 / 843 |
|---|---|---|
| a damaging hitsplat LANDED in the 6 ticks before the step (was ours) | 142 | 32/33, 53/57, 13/13 |
| a raider's attack on a claw that went out for damage > 0, in the 8 ticks before the step | **155** | 33/33, 54/57, 13/13 |

The 13 misses of the old rule: a trident cast landing on the step's own tick [4246] [4278] [5060]; a cast still in
the air [4350] [5470] [5830]; an attack 7-8 back [4678] [5342]; the rest a launch inside the window whose splat
landed after. A 0 or a splash never steers (4190, 5172, 5196, 4566, 5398 all have one, and do not turn). The window's
edges: 7 ticks 153, 9 153, the step's own tick counted 151, splashes counted 151. Ours now counts the attack in
`~cox_olm_prepare_hit` (`~cox_olm_hand_attacked`, `^cox_olm_steer_ticks = 8`); the solver (`CO.launched_in`) too.

### The scuffed Olm (fixed)

Every recording runs in stretches: the head's idle stance 2 ticks after a turn or an attack (normal) or 3 (scuffed,
Synq 2:44:51). 4392: +3 [4186-4856], +2 [4860-5410], +3 [5414-6034], +2 [6038-6314] (416 turns and attacks);
rsprox.net 820 flips 24 times (stretches 98-320 ticks), 3282/3285 too. In a scuffed stretch every delayed event is one
tick later: the fire wall (6 of 6), the bombs (12 of 12), the shower's crystals (110 of 110 bursts at +4, against 311 of
312 at +3 normal). What flips it is not found (not the wall clock: best fixed threshold 243/416). Ours:
`^cox_olm_v_scuffed`, a stretch of 20-620 ticks (`^cox_olm_scuffed_min`, `_spread`).

### Fire wall, bombs, crystals, lightning (fixed)

| Tell | Recorded (normal; scuffed +1) | Was |
|---|---|---|
| fire wall | 20 `invisible_type8_blocking` locs on the flame tiles and the rows judged at C+1; `olm_firewall_npc` at C+3; both gone and the row burnt at C+10 (6 of 6) | flame npcs only, C+1 to C+9 |
| crystal bomb throw | `olm_crystalrock_falling` from the face, start 32, end 46 + 10/tile (12 of 12, distances 3-10), angle 5, progress 0, the head's shot height | `olm_crystal_bomb_travel`, 60 cycles, height 250 (also rsprox.net 3285 [16550]: the old table conflated the two) |
| bomb landing / blast | each at C + end/30; all of a throw at C+10: `olm_crystal_bomb_explode` height 40 + `100_cauldron_explodes` range 10 | placed C+4, blast C+9 (no height, silent), hit C+10 |
| crystal power landing | `olm_crystalwave_shatter` height 10 + `kr_smash_window` range 3 ([5438]-[5458], [6085]) | `raids_olm_central_shattercrystal` (the burst's) |
| shower | `cam_shake` axes 0, 1, 2 at random 2-5 every 2 ticks (212 of 212); `cam_reset` with the rising forms and the last death | no camera |
| lightning hit [5123] | `human_killerwatt_electricshock`, `paralyze` (154), "`@mes_hl_red@You've been injured and can't use protection prayers!</col>`", bound 4 | `human_stunned`, silent, plain line, 8 |
| acid spray | `barbassault_splat` range 3 as each spit lands (94); drip pools silent (152/152) | silent |
| deep burn | `salamander_scorch` synth every burn (43/43) | silent |
| solo teleport | `teleport_all` range 3 on the tile left ([4828], [5140]) | silent |
| life siphon | `crumble_hit` range 10 at the head ([6058], [6226]) | silent |
| head / claws | no defend or death sequence (the generated block's `human_unarmedblock` / `human_death` went out on ~470 hits) | fixed in cox.npc |
| swamp trident | impact 92 (100/100), splash 124 (59/59) | 124 / 92 |

Every bolt step's `olm_shockwave` + `godwars_saradomin_magic_impact` matched already; our tick log logs an area sound
only for a raider in range (the wire sends it to the zone), so 20 vs 442 there is the log. A special still fires on a
step the head turns away from a raider in its old view (5/36 casts in 4392; 45/248 in 820) -- unchanged.

### Open

- The scuffed flip's trigger. The solver's `olm_step` check: 2/390 mismatches on ol1 after the steer change (its
  orphan-hitsplat launch is dated a tick before the splat, wrong for a trident cast).
- Rate rows (the recorder's kit and position): head attack variants per view, acid spit sound delays 10/12.
