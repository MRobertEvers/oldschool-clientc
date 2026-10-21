# The nexus between the fights: doors, the atrium, the navigator

The raid's spine: the Guardian's room (plane 0) opens two doors after the
kill; each later boss is left by following an Alliance Navigator back to the
atrium (the nexus again, plane 2) where the next pair of doors stands open.
Sessions in `RAID_PLAN.md`. Every raider in these recordings is someone else
(the recorder spectates), so what a raider's own client was sent on an op is
not recorded: the spectator's own transitions (same fades, same vars) are the
template, and the raiders' ticks come from when the others see them leave and
arrive.

## 1. After the Guardian (kill line K)

| tick | recorded | evidence |
|---|---|---|
| K | the dead form, `tfa_nexus_section1_door_west_slightly` (4113,5584 r3) and `..._east_slightly` (4141,5584 r1), exit 0b1 | built (`guardian_plan.md` §11) |
| K+3 | the two doors `..._slightly_opened` | built |
| K+15 | `armacus_selles_postguardian` added at (4127,5577) spawnangle north, `[facing] angle=1024 instant`, transparency 255 -> 0 over 30 cycles; runs north 2 tiles a tick | 4300 [862], 4301 [42], 4303 [326] (+15 3/3) |
| K+16 | four Guardians of Armadyl (`alliance_guardian_of_armadyl_1..6`, any four) at (4126,5574), (4126,5573), (4128,5574), (4128,5573), north, the same fade-in; run north 2 a tick | 3/3 |
| K+20 | Armacus stops at (4127,5586) (his last step a walk) and says "Pick a side and get to work!" | 4301 [47] |
| K+24 | a second four (4301 [51]) on the same tiles | 4301 |

The guardians run north 4 ticks and are gone with the room; their later path
is not recorded (the party had left). Ours: Armacus and the first four, the
second four not (cosmetic, one session).

## 2. The doors (Enter / Quick-enter)

`tfa_nexus_section{1,2,3}_door_{west,east}*`: op1 Enter, op2 Quick-enter
(cache). A raider walks to the door and faces it (`[facing] coord=(4113, 5584,
0), sizez=5`, 4301 [30]); the raiders vanished from the room 4..5 ticks after
the last of them arrived (4301 [43..52]). What Enter asks a party (a vote, a
wait) is not recorded: ours, solo, both ops take the player straight in.

| door | leads to |
|---|---|
| section 1 west (4113,5584) | Ezekiel (4300, 4301) |
| section 1 east (4141,5584) | Bork (4302, 4303, 4294) |
| section 2 west (4113,5597) | Moia: **not built** (no recording); the door says so |
| section 2 east (4141,5597) | Dreadnoth (4301, 4302, 4303) |
| section 3 west (4113,5610) | the Dagon'hai: **not built** |
| section 3 east (4141,5610) | Xarth (4301, 4302) |
| Ocellus door (4124,5621) | Ocellus, plane 1 (4301 [867..876]) |

## 3. Moving between rooms (the spectator's own packets: 4301 [49..54], [283..288], [559..567], [868..877])

| tick | recorded |
|---|---|
| O | `fade_overlay` (174) opened on `overlay_atmosphere` with its text "", script 948 (0,255,0,0,50) — 30 instead of 50 out of Dreadnoth and through the Ocellus door; `hpbar_hud:hp` hidden, script 2249 on `hpbar_hud:container` |
| O+1 | `minimap_state` 2, `busy` 1, `minimap_toggle 2` |
| O+2 | the teleport |
| O+3 | arrived; the new room's locs and npcs; `minimap_state` 0, script 948 (0,0,0,255,50), `minimap_toggle 0`, `hpbar_hud:hp` hidden again |
| O+5 | `busy` 0, the fade overlay closed, `tfa_hud` (974) re-opened on `overlay_atmosphere` with its two `if_setevents` |

The raiders' own: they disappear from the old room on O+3/O+4 of their op
(Vampyire faced the navigator [279], gone [283]; Fatalsystem [282], [285]) and
are added in the new room facing their arrival angle.

## 4. The atrium (the nexus on plane 2)

Each visit is a fresh instance of square (64,87) on plane 2, built with the
room's state for that stage. Arrival tiles: from a west room (4116,5586)
facing east (`angle=1536`); from an east room (4139,5587) / (4139,5600) /
(4136,5612) facing west (`angle=512`), the party spread over a few tiles.

| visit | locs (all shape 10) | npcs | evidence |
|---|---|---|---|
| A1, after stage 2 | `tfa_nexus_section_door_closed` (4141,5610 r1), (4113,5610 r3); `tfa_nexus_section2_door_east_slightly` (4141,5597 r1), `..._west_slightly` (4113,5597 r3), +1 tick both `..._slightly_opened`; `tfa_nexus_ocellus_door_closed` (4124,5621 r0); `tfa_in_raid_scoreboard_broken` (4119,5578 r2); `tfa_exit_large` (4124,5569 r0) | `eva_cashien_sword` (4127,5597) S; `armacus_selles` (4132,5579) N (4301) or (4120,5582) E (4302, 4303) | 4301 [286], 4302 [117], 4303 [705], [2400] |
| A2, after stage 3 | `tfa_nexus_section3_door_east_slightly_opened` (4141,5610 r1), `..._west_slightly_opened` (4113,5610 r3), `tfa_loot_wheelbarrow` (4130,5608 r0), the Ocellus door closed, the scoreboard | `eva_cashien` (4135,5603) W; `tfa_midraid_supplies_npc` (the Quartermaster) (4129,5609) S: "Stock available!" (+5), "Shop's open!"; `tfa_ivy_sophista_noop` (4127,5613) S: "Give them no quarter, pick a side!" (+12, then every 14) | 4301 [565..591], 4302 [423] |
| A3, after stage 4 | `tfa_nexus_ocellus_door_open` (4124,5621 r0), the scoreboard, the exit | `armacus_selles_postguardian` (4128,5619) S, `eva_cashien_sword` (4126,5619) S, `tfa_ivy_sophista` (4127,5618) S: "The situation is almost under control!" (+9), "Check the vault for danger and let's end this!" (+13) | 4301 [859..872] |

The Alliance's workers stand about each visit: temple knights, druids and
Guardians of Armadyl at fixed spots ((4123,5602), (4131,5598), (4124,5588),
(4132,5589), (4122,5580), (4126,5596)...), playing `human_herbing_grind_npc`
7319, `human_pickuptable` 832, `zalcano_runecraft` 8444,
`human_fletching_add_feather` 8481, `human_pickupfloor` 827 in turn; four more
walk in from the south door (4126/4128, 5573/5574) 3 ticks after arrival. Ours:
the named npcs as recorded, five workers at the 4301 spots cycling those five
seqs every 4..6 ticks (approximate: the order is not derived).

The Quartermaster's shop is not recorded (the spectator's ops get no answer,
4301 [576], [582]): ours, Talk-to says "Stock available!" and the shop is a gap.

## 5. Leaving a boss room: the Alliance Navigator

| tick | recorded | evidence |
|---|---|---|
| boss line +3 | `tfa_room_navigator` (Follow, Quick-follow) added beside the boss's spot, facing back toward the entry, fade-in over 30 cycles | Ezekiel (4067,5535) W 4301 [276]; Dreadnoth (4184,5663) E [549]; Xarth (4183,5535) E [847]; Bork (4184,5599) 4302 [105], 4303 [694], [2368] |
| op | Follow: the raider walks up, faces it, and leaves 3..4 ticks later (the transition of §3, into the atrium visit for the next stage) | 4301 [279..285] |
