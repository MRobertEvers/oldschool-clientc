# The Great Olm: recorded behaviour

Read from the normal solo kills 3282, 843 and 3285, plus the trio and four-man raids in 820. 3389 is Challenge Mode and is excluded from damage. All are on Grid Master worlds; see [README.md](README.md).

In the Olm chamber the claws sit at x 3220 (west wall) or 3238 (east wall). On the west wall the head is at z 5738, the left claw at 5743 and the right at 5733; on the east wall the claws swap. The status column is our content's state on 2026-10-18.

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
