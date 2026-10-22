# The Archive Guardian: spec from RSProx, and the implementation plan

Sources: fought solo by the recorder in index 4296 [361..840] (died) and index
4297 [97..259], [353..641], [817..1180] (died each time); spectated in a party
in index 4294 [545..1043] (wipe) and [1099..1431] (kill, 5 players). The wiki
(`sources/Archive_Guardian.wiki`, fetched 2026-10-20) has stats only. Every
rule below cites counts across those fights; tools in `tools/`
(`tfa_events.py` parser, `attack_clock.py`, `swoosh.py`, `my_hits.py`,
`tentacles.py`, `guardian_hp.py`, `fight_view.py`), outputs in
`guardian_actions.txt`, `fight_4297b.txt`, `room_collision.txt`.

## 1. The room and its entry

| fact | evidence |
|---|---|
| Lobby `tfa_lobby_raid_entry` (4123,5562) op1 with no party: chatmenu "You are not currently in a raiding party." / "Form or join a party.\|~~Spectate a specific party.~~\|~~Spectate a recent party.~~"; then `tfa_partylist` (977), `tfa_partydetails` (976) (script10094 rows, script10053/10060) | 4297 [327..341] |
| Op1 again with a party: L+1 "You enter the Archive...", `tfa_lobby` HUD closed, fade out; +1 minimap 2 `busy` 1; +3 teleport into the room (4126,5573), `cam_reset`, script10013; +4 the room's locs: 4 `tfa_nexus_section_door_closed` (4141,5597 r1; 4141,5584 r1; 4113,5597 r3; 4113,5584 r3), `tfa_exit_large` (4124,5569 r0); +5 minimap 0, `busy` 0, `areasounds_override`, music 147, fade in; +7 `tfa_hud` (974) opened (setevents 974:5, 974:1), script10042 names, `tfa_client_p0` 27, script10036 (0,1) each tick | 4297 [342..357], 4296, 4294 |
| +9..+17 more doors: `tfa_nexus_section_door_closed` (4141,5610 r1), (4113,5610 r3); `tfa_nexus_ocellus_door_closed` (4124,5621 r0) | 4297 [362], [370] |
| The room is square (64,87); open floor in `room_collision.txt` | `::tfamap` |

## 2. Spawn and the start

| fact | evidence |
|---|---|
| On entry: `tfa_guardian_start` ("Inert Guardian", op1 Touch) at (4125,5597) spawnangle south; four `tfa_guardian_battery_inactive` at (4139,5605), (4139,5592), (4114,5605), (4114,5592), spawnangle south | 5/5 |
| On entry, 41 `invisible_type8_blocking` (shape 10): the inert guardian's 5x5 and each battery's 2x2 | 4297 [353], [358] |
| While inert: spotanim `vfx_guardian_spawn_idle_spotanim01` (4207) every 3 ticks | 5/5 |
| Touch: chatmenu "Begin the challenge?" Yes./No.; on Yes "Challenge started: Archive Guardian.", `busy` 0, seq `spawn01` (14755) + spotanim `vfx_guardian_spawn_spotanim01` (4206), `tfa_exit_large` re-added with `opflags=0b0` | 4297 [125..131], [382..384]; every challenge in 4294/4296/4297 |
| Spawn+5: start -> `tfa_guardian` (level 984, ops 0b11111, seq `idle01` 14745, faces (4127,5572)); the four batteries -> `tfa_guardian_battery_active` (ops 0b11111, except the south-east one at (4139,5592): ops 0b00000 for the whole fight, never hit by a raider, destroyed only by the enrage's first overload); 25 `invisible_type8_nonblocking` on the 5x5; boss bar opened (`hpbar_hud_npc` 16659, hp 550, basehp 550, boss 1, colours 132/623/853, script `hp_hud_open`); music 887 (re-sent every tick of the fight) | 5/5 |
| The timer script10036(ticks since the start, 0) every tick | 4297 |

## 3. The attack clock

- One clock, 5 ticks: ranged, special and melee each take a slot (R->R +5 145/145, R->S 37/37, S->R 38/38).
- A swoosh does not take a slot; the next attack comes 3 ticks after it: next = max(last + 5, swoosh + 3) (W->R +3 28/28, W->S 5/5, W->M 1/1).
- First action after waking: a swoosh +1..3 (when the player is far) or a ranged +4..5.
- Special every 5th slot (4 ranged between, 26 of 31 pairs); after landing, the 3rd or 4th slot. **Approximate**: 5 pairs show 5 ranged between; no rule found.

## 4. Ranged attack

- Seq `range_attack01` (14751), npc spotanim `vfx_guardian_ranged_spotanim01` (4199) on the slot; projectile `vfx_guardian_ranged_projanim01` (4201) guardian -> player, starttime 48, endtime 116 at every distance (243/243), angle 127, progress 0, heights 0/0.
- Lands 4 ticks after the throw: player spotanim `vfx_guardian_ranged_hitanim01` (4200), `human_hit` synth delay 20.
- Damage: protect from missiles on: 0..10 (137 hits, max 10, even spread); off: max 41 seen (13, 20, 20, 31, 41). Rule: max 41, protect missiles takes 75% (max 10). Read at landing.

## 5. Swoosh (reposition)

- Before an attack slot, if the target's distance to the guardian's 5x5 (tile of the tick before) is >= 7: telejump to a box that puts the player exactly 3 tiles away (34/34), with seq `swoosh_attack01` (14750), exactmove (old sw tile, delay1 0, new sw tile, delay2 = 30 + 2 x Chebyshev travel (33/33), angle = travel direction) and face that angle.
- The box: the first of E, SE, S, SW (then W, NW, N, NE, unobserved) whose 25 tiles are all open (34/34 against the map): E = sw (px+3, pz-2), SE (px+3, pz-7), S (px-2, pz-7), SW (px-7, pz-7), W (px-7, pz-2), NW (px-7, pz+3), N (px-2, pz+3), NE (px+3, pz+3).

## 6. Special: mushrooms

Every special of every session with him (index 4294, 4296..4298, 4300, 4303..4306, 4308..4310, 4312..4316: 171 specials, 929 mushrooms, 469 bursts; the scratch tools `mush.py`, `popneg.py`, `mushlive.py` over the `--all-players` transcripts).

- Seq `special_attack01` (14753) + spotanim `vfx_guardian_special_attack_spotanim01` (4205); `tfa_guardian_mushroom_spawn` (62578) on two tiles a raider.
- **Where**: round the tile the raider stood on **the tick before** the special (10/10 solo specials where the raider moved that tick; the tick's own tile 1/10, two ticks before 6/10). **One on a tile beside it and one exactly three out** (every solo special; 225 of 225 party raiders who can be read apart; a tile two out 56 of 921, all from stacked raiders read as the nearest). Each is an even pick among that ring's open tiles: the eight beside 52..67 each (486), the 24 three out 9..25 each (379); the far one's way is independent of the near one's (angle bins 22..38, 28 each if independent). Never on a wall (0/929 against `room_collision.txt`, x origin 4108) nor on a live mushroom (1 of 1123 loc events); under his 5x5 while he flies, yes (59), and on another raider's tile (2). Packet order is the zones', not a rule.
- At most 39 alive at once (4303 [1835], 4312 [481]); ours holds 60 (one register each).
- +3: `tfa_guardian_mushroom_active` (62579) with `loc_anim nightmare_infection_spotanim_idle` (8631) (929/929). Mushrooms stay for the rest of the fight.
- **The burst**: the first tick T at or after active + 1 on which a raider stands within a tile of it bursts it on T + 1 (468 of 469; a raider beside it on the active tick itself does not). On the burst: `invisible_type8_nonblocking` + `tfa_guardian_mushroom_despawn` (62580) on the tile, `sound_area` 12175 delay 12 range 2 size 2 (423/425); +2 the tile's `invisible_type8_nonblocking` again (421/425).
- **The poison**: on burst + 1, every raider within a tile of it at the burst's end (poisoned with a fresh 10: within 1 at the burst 44/51 and 33/58 when still within 1 after; 2+ out 3/77, 6/241, the rest already poisoned) -- "You have been poisoned!", poison 50 (a 10 splat, type 65, then every 30 ticks), drowsy for 17 ticks (`runinvert_mode` 0 -> 3; running refused with "You're too drowsy to run!").
- Ours: the same, tick for tick in `tfa_guardian.lua` / `tfa_guardian_melee.lua` (1 + 3 round last tick's tile 15/15; every burst a tick after a raider within 1, the earliest active + 2). Before 2026-10-21 ours put both within 3 at random (a third at 2) and held 30.
- The tile of the tick before, the burst's "within a tile" and the poison's "within a tile at the burst's end" all read the raiders' tiles as the tick began from his timer (`~tfa_g_pre`, 2026-10-22): the room's tick runs in one raider's turn, after the raiders before them in the player list have moved, so in a party it read some raiders a tick late. `gv_check.py ours` over our three runs: 22/22 raiders one beside and one three out, 23/23 bursts on the rule.

## 7. Shield, batteries and the ground phase

- Flying (`tfa_guardian`): 550 hp is the shield (cache stat4 550; boss bar blue). Batteries `tfa_guardian_battery_active`: 180 hp, attackable, no defend sound.
- A battery (an "Inert Guardian", its flame) at 0: the NEXT tick seq `guthixian_battery_despawn` (15025) and ops 0b00000 (17/17, the enrage's overloads too); +2 -> `tfa_guardian_battery_destroyed` (headbar removed), 3 cam shakes (axes 0..2), synth 12184 to the killer; +3 the guardian takes 40% of the shield's max (220) capped at what is left (12/12), `sound_area` 12728 range 10 at its centre.
- Shield at 0: 3 ticks later (5/5 solo, whatever the attack clock), `idle_to_ground01` (14747) + `vfx_guardian_idletoground_spotanim01`, -> `tfa_guardian_unshielded` (level 984), `map_anim tfa_arena_sword_drop_explosion_instant` (4389) delay 20 at its centre, the 5x5 `invisible_type8_blocking`, boss bar 1100 green (25600/576/800).
- Grounded 35 ticks the first time in a fight, 6 more each landing after: 35 (24/24), 41 (8/8), 47 (2/2: 4294 [830]; [1183], spectated from mid-fight, landing at 54% so not the first); no attacks; tentacles. Every recording with him (index 4294 4296 4297 4300 4303..4306 4308..4310 4312 4314..4316; `tools/ground_time.py`). He lifts on the tentacles' 3-tick beat: the slot 3 ticks before his time has no wave, and he goes up 3 ticks after it (the last wave at +29 of 35, +35 of 41, +41 of 47). Then `ground_to_flying01` (14748) + `vfx_guardian_groundtoidle_spotanim01`, -> `tfa_guardian` with a full 550 shield, the 5x5 back to nonblocking, bar 550 blue; the first attack 5 ticks later.
- **His grounded stance is a stance override, not his type's**: both `tfa_guardian` and `tfa_guardian_unshielded` name the flying `idle01` as their ready anim, and the tick after each landing the npc info carries `[bas] ready=npc_archive_guardian_ground_idle01 (14746)` (the rev-241 BAS_CHANGE block, flag 0x400000; every landing of ten sessions at L+1); an empty `[bas]` (the type's stances again) rides with the lift (4297 [536], [1005]; the enrage's, 4294 [969]) and with the death's transformation (4294 [1431], 4300 [847], 4303 [311], [1849]); a raider who arrives while he is down gets it on his add (4300 [679]). Without it the client plays the landing and then idles his head in the air. Ours: `npc_basready` / `npc_basreset` (engine 2026-10-21), the client taking every unset stance from the type (deob Statics, flag 0x400000).
- Open: at the enrage's lift the recording transforms him back to `tfa_guardian` with the 550 bar (4294 [969]); ours stays unshielded (§9, approximate).
- Early lift: from the +11 slot, a slot where his real hp is under 34% of its max has no wave, he lifts 3 ticks later and enrages (§9): 23 lifts at +14, +17, +20, +23, +26, +29, +32, +41, +44, all on the beat; the enraged seq at lift +10 on 22 (4306 [2014]: the recorder stopped spectating at +9). Stopped at 33.4% (4315 [772]), 33.5% (4300 [559]), 33.8% (4304 [1093]); a wave at 34.6% (4294 [1224]); a timed lift at 34.3% with no enrage (4314 [899]). The +5 and +8 waves always come (6/6 below 34% at +8). An earlier reading of this (35 ticks fixed, the enrage at about 27% "at once") was from 4 solo first landings and two party lifts.
- Real hp (1100 solo, grounded only) carries over between ground phases.
- A blast while grounded: one sample only (4294 [1414], the party's enrage: a hit of 1). Ours takes 220 off the real health: **approximate**.

## 8. Vines (`archive_guardian_tentacle`, ground phase), audited 2026-10-22

Every vine of every session with him: 15 `--all-players --bas` transcripts
(4294, 4296, 4297, 4300, 4303..4306, 4308..4310, 4312, 4314..4316), 1164
lives, 464 waves, 106 raider-beats with a vine in reach;
`tools/gv_check.py rec` (parser `tools/gv_events.py`; collision
`room_collision.txt` plus his 5x5 while he is down).

- **When**: landing (`idle_to_ground01`) +5, then every 3 ticks (59/59 landings); the lift rules of §7.
- **Who**: one for each raider in the room, judged on the tile they stood on **as the tick began** (the end of the tick before): the vine faces that tile (1141 of 1143).
- **Where** (463 of 464 waves exact, every vine's tile and face; the 464th is 4300 [679], the recorder arriving mid-phase): A = that tile plus one step in the 8-way direction from his middle (his sw + 2) to it, rounded to the nearest of the eight; if A is blocked, the step the other way (toward him); then the raider's own tile; none free, no vine. Blocked: the map, his 5x5 (blocking while he is down), or a vine, live or added earlier in the same wave (0 vines on another vine's tile against about 70 expected if they did not block). 879 on A, 10 toward him (A in a wall, 4296 [534], 4304 [236], 4306 [807], [898], [1003]), 262 on the raider (A held by a vine: 234 a stacked raider's this wave, 16 a live one; 4 in a wall with the other side his 5x5); raiders stacked four deep get two (4303 [1371]). An even pick among the eight (ours before) would put 359 on the raider's tile.
- **On add**: spawn angle toward the raider's tile (south on it; 1143/1143), `[facing] coord=` that tile with `instant=true` (1143/1143), seq `tentacle_spawn01` (14771) delay 0, spotanim `vfx_guardian_tentacle_intro_spotanim01` (4208) height 0 delay 0, `sound_area` 12218 delay 0 range 4 no size on its tile (1141/1143, never without a vine). No other block in its life: no ops, bar or hit (never attacked).
- **Life**: `tentacle_attack01` (14768) at +3, +6, +9, `despawn01` (14769) at +12, gone at +13 (1140 of 1140 seen whole; the three cut short are the recorder leaving on a death, 4296 [820], 4305 [163]). None alive at his death (20/20 deaths).
- **The hit** (100 of 106 raider-beats; the rest: 5 dead raiders, not hit, and one run off its tile, 4294 [1200], hit): on the attack's own tick, every living raider exactly one tile from the vine as the tick began; on its tile is safe. One hit a raider a beat however many vines reach them (7/7 with two or three), and never a miss (101 hits, no zero or block splat).
- **The damage**: melee. 17..19 unprayed (15) or under Protect from Missiles (6); under Protect from Melee 10 (5/5 where the overheads are on the wire); 10 x22 and 11 x6 where they are not (4294, 4300, 4306: Fortunico 4 of 10 at 11, sycq 2 of 2; Fortunico and Rewz in the same gear, Rewz 10 x5). Read as 40% off the roll (17, 18 -> 10, 19 -> 11): that gives an 11 a third of the time, the recordings 6 of 33 (P = 0.04), the one tension left. A lethal hit shows what was left (2, 3, 15).
- Ours (`tfa_guardian.rs2` `~tfa_g_tentacle_spawn` / `_add` / `_strike`): the same rules; the raiders' tiles come from his timer (`[ai_timer]` on both fighting forms, `~tfa_g_snapshot`, the npc phase) because the room's tick runs in a raider's turn after the raiders before them have moved. Before 2026-10-22: an even pick of the nine (the raider's tile on a wall), no vine blocking, a coord face without `instant`, a third of its hits blocked, every vine in reach hitting, Protect from Melee ignored. Test: `test/guthixian_archive/tfa_guardian_vines.lua` (Protect from Melee; `gv_check.py ours`: 4/4 waves, 6/6 beats, 10 and 11); `tfa_guardian.lua`, `tfa_guardian_win.lua` 8/8 waves, 11/11 beats.

## 9. Enrage (party only: unverified solo)

- When the guardian's real hp is under 34% at a tentacle slot while grounded it lifts 3 ticks later (§7) and enrages: S, +5 `enraged_phase01` (14754) + `vfx_guardian_enraged_phase_spotanim01`, +10 a battery is overloaded (its remaining hp as one hit, destroyed as above), every raider hit by one roll of 42..49 split by the raiders in the room, x1.5 off Protect from Magic (63 blasts; `hit_causes_audit.md`), the guardian's shield takes the battery's blast; +5 S again (20-tick cycle). The enrage's lift is a lift: the flying form with the shield whole, broken by blasts and hits back to the grounded type in the air, no landing, the cycle going on (2026-10-22). Order: (4139,5592), (4139,5605), (4114,5592), (4114,5605).
- **Solo is unrecorded**: the per-player hit is taken as 64 / (players, minimum 2), rising 25% a cycle.

## 10. Melee

- **The slam** (`docs/rsprox-analysis/guthixian_archive/tools/tfa_mech.py`; every recorded `melee_attack01`, 14752, in index 4297, 4298, 4304, 4306, 4310, 4312, 4313, 4314): his attack falls to the slam when **any** raider stands under his 5x5 as it comes due (where they stood the tick before the swing: 24/24), and it lands the tick after on whoever is **still under him on the swing's tick**. Step under, then out on the swing's tick, and it hits nobody: index 4314, B0aty 10/10; 11 more in the other sessions, none hit; 4312 [303] back under at +1, not hit. Stay under and it lands: 4298 [187] 37, [367] 48, both through protect from missiles. The standard max from the cache (strength 240, bonus 0) is 25, so the slam is not the standard melee: ours 0..48 (two hits); protect from melee is unrecorded (taken to block it). "It's flying too high to hit using melee." for a player's melee on the flying guardian.
- **His head coming down** (the landing, `idle_to_ground01`): a raider one tile from his 5x5 (where they stood the tick before) is smashed for 24..28 on the landing's tick, no zeros (6/6: 4305 [134] 26, 24; 4306 [192] 24; 4314 [209] 27, [939] 28; 4315 [4293] 24); two tiles or more out, never (136/136); the one raider recorded under him was not hit (1/1). Ours the same.
- Ours: `test/guthixian_archive/tfa_guardian_melee.lua` (the dance, the stand, the landing): stepped out on the swing's tick 0 of 4 hit; under on it 10 of 10 hit the tick after; a tile off as he landed, 26.

## 11. The guardian's death

- Real hp 0: `death01` + spotanim `vfx_ocellus_null` height 200, hp bar 0; +7 -> `tfa_guardian_dead`, "Challenge complete: Archive Guardian. Duration: m:ss. Total: m:ss", batteries deleted, `tfa_nexus_section1_door_west_slightly` (4113,5584 r3) / `..._east_slightly` (4141,5584 r1), `tfa_exit_large` opflags 0b1, music 147, `tfa_client_p*` 27; +3 the doors `..._slightly_opened`; +8 the boss bar cleared (4294 [1424..1439]).
- (+15 Armacus and the Guardians of Armadyl arrive: the next stage, not implemented.)

## 12. A player's death (whole team dead)

- Hit to 0: D+1 `busy` 1, `tfa_client_p0` 1; D+3 `human_death`; D+7 "You have died. Total deaths: n.", prayers off, teleport to (4097,5569) (outside the room), exit opflags 0; D+10 boss bar cleared, "You failed to survive the Fractured Archive.", fade out, music 147, jingle 90; D+11 minimap 2, `busy` 1; D+13 back to the lobby (4130,5559) facing south, gravestone (1500 ticks), kept items as an ordinary unsafe death (4297 [628..641], 4/4).

## Audit: our scriptrun against 4297 and 4296 (2026-10-21)

Runs: `test/guthixian_archive/tfa_guardian.lua` (the batteries, then the
guardian, until the raid wipes) and `tfa_guardian_win.lua` (`::tfahp` cuts the
shield and the real health so the kill and its +7/+10/+15 happen in a short
run). Inventory: `tools/rsprox_tells.py --anchor 16657-16664 idx4297.txt
idx4296.txt --ours <both ticklogs>` (four recorded fights against two of ours,
per-fight averages; ours are shorter, so rates differ). Wire: `wirelog_tells.py`
over 4297 [125..262] anchored on "Challenge started" (R [131], O 96).

| tell | recording | ours | verdict |
|---|---|---|---|
| entry blocking locs | 41 `invisible_type8_blocking` (5x5 + 4x 2x2) | same, `~tfa_g_spawn`/`~tfa_g_battery_add` | fixed (was missing) |
| SE battery ops | `enabled_ops 0b00000` on activation, 5/5 wakes | `npc_setops(0)` on battery 0 at the wake | fixed |
| exit during the fight | re-added `opflags=0b0` at the challenge; `0b1` at the kill +7 | `loc_add_opflags` (new, opcode 11224), wire bytes `a8 00 c1 00 f5 2e` / `a8 01 ...` | fixed |
| ranged projectile | `[source] npc=tfa_guardian` (every shot) | `npc_projanim_pl` names the npc (`proj_source` matches) | fixed (was a bare tile) |
| ranged shot, field by field | 243/243: start 48, end 116, angle 127, progress 0, heights 0/0, from the 5x5's centre, homing on the player; the splat at throw+4 (95/102) with `human_hit` delay 20 | same on our wire (`MAP_PROJANIM_V2` layout checked against RSProx's 241 decoder; target index pid+1) | matched |
| ranged shot, as drawn (2026-10-21) | the wire bytes decode (rev-241 `MapProjAnimV2`) to the recorded shot exactly; the rocks ride the 82-frame skeletal seq of 4201 (14761), and the deob (239 `class50.method940`, 241 `class31.method651`) steps a projectile's seq only once cycle >= starttime, so frame 0 is the launch | our client bound the seq at the packet: launched at frame 48, the rocks showed at the mouth at once, then fell to the bind pose (specks) halfway | fixed in the client (`app_world_sync_positions` binds at launch); frames 0..68 over the flight, same on soft3d and opengl3-zbuffer |
| ranged impact | player spotanim `vfx_guardian_ranged_hitanim01` in **slot 2** (142/142) | was slot 0, which took the raider's own weapon graphic's slot | fixed |
| enrage spotanim, death's `vfx_ocellus_null` | slot 2 (8/8, 1/1) | was slot 0 | fixed |
| battery shakes | `cam_reset` the tick after the shakes, the blast tick (12/12) | was missing (the shakes never cleared) | fixed |
| grounded form's bar | healthbar 19 (224 rows); the flying form's 10 (517) | was 10 on both | fixed |
| range/special/swoosh/melee seqs and spotanims | per §3-§5 | present, same ids, heights, delays | matched |
| battery death | 0 at K; K+1 despawn seq, ops off; K+3 destroyed form + 3 shakes + synth 12184; K+4 `cam_reset`, the blast (220, or what is left: 110) and area 12728 r10 (17/17) | was one tick early (despawn on K) | fixed |
| land / lift / tentacles / mushrooms | §6-§8 | all present (`idle_to_ground01`, `ground_to_flying01`, tentacle spawn/attack/despawn + area 12218, mushroom spawn/active/despawn + area 12175 size 2) | matched (rates follow fight length) |
| defend sound | synth 12728 per hit on either form | same (npc config) | matched |
| idle01 at the wake | printed under `tfa_guardian_start` | printed under `tfa_guardian` | same tick, same packet: the parser attributes before/after the transformation |
| kill | death01 + `vfx_ocellus_null` 200; +7 dead form, doors, exit 0b1, line; +10 doors opened; +15 bar off | same | matched (4294 [1424..1446]) |
| wipe: spectate | K+7 [255]: line, sanity 100, prayers off, `tfa_client_spectate_mode` 1, teleport, `cam_reset`, side panels re-sent but 0 and 3, then sides 0,1,2,4,5,6,3 closed | same order | fixed (was missing) |
| wipe: lobby | [259]: spectate mode 0, `cam_reset`, every side panel re-sent | same | fixed |
| eating | "You eat the shark." + "It heals some health." (hurt only), both `type=spam`, synth `eat` 2393 | same | fixed in `food.rs2` (was "Shark", type 0, no heal line, no sound) |
| protect from missiles on | synth 2677 loops 1 | was loops 0 | fixed in `prayer.rs2` (every prayer-on sound) |
| R-only kit | dart 2696, whip 2720, sharp_eye 2685, potions, `tracking_*` varps, blowpipe varbit 5934, xp drops | our kit: bow of faerdhinen (crystal_bow2 1352) | the recorder's kit |
| music | 147 before, 887 during, every 3 ticks | same cadence; phase runs from the room's entry, the Touch tick is the player's | matched |

Still open, all engine-generic (not this content's to fake):

- `busy` (varbit 12393) around dialogue: set 1 a tick after a chatmenu opens and 0 on the resume (4297 [125..131], [329..332], [382..384]); a delay sets it on its own tick (4293 [35]). One rule is not yet derived; ours sets it only where content does.
- The chatmenu's options: Jagex `if_setevents 219:1 1..n`; ours `0..28` (`if_addresumebutton`, 101 call sites).
- The boss bar: Jagex has `hpbar_hud` open already and sends the vars and `hp_hud_open` (2376) on the wake tick, then 2376 every tick; ours opens 303 on the wake and feeds it the tick after, then on change. At the end Jagex clears 1683 at once; ours holds the bar ~6 ticks (the engine's boss hold).
- The side journal re-sent at the spectate/lobby: Jagex questlist 399, ours the journal's default 712.
- Player facing: Jagex `face_angle` after an op and `[facing] type=reset`; ours faces the entity.
- The gravestone line needs items to reach the gravestone; the test kit keeps all three.

## Implementation map

- `area_guthixian_archive/configs/tfa_guardian.npc`: server blocks for every form (timer, hp, modes, sounds: defend 12728 on both forms).
- `scripts/tfa_raid.rs2`: lobby entry (solo party), the room instance, HUD, exit, the deaths.
- `scripts/tfa_guardian.rs2`: the state machine in instance registers, `[ai_timer]` per form, `[ai_queue2]` for the shield and the batteries, the specials, tentacles, enrage.
- Engine: `p_runblock` (opcode 11223) for drowsy, `atan2_deg` for the swoosh yaw, `loc_add_opflags` (11224) for the exit.
- Test `test/guthixian_archive/tfa_guardian.lua`; audit with `wirelog_tells.py` and `rsprox_tells.py --ours` against 4297.

## Open probes

- What paces the special exactly (§3).
- The vine's 11 under Protect from Melee: 6 of 33 against a third (§8). The solo enrage (§9); the melee (§10).
- The slam's and the landing smash's "where they stood the tick before" (§10) still read `coord` in the room's tick: right solo, a tick late in a party for raiders earlier in the player list; `~tfa_g_pre` is there for them.
- Party scaling (4294: 5 players: shield 2520? batteries 400); only solo is built.

## NPC info audit (2026-10-21)

Every NPC_INFO block of every npc of the room, recording against our tick log, per life:
`tools/rsprox/npc_stream.py --match <type> --summary <transcript> <ticklog>` (counts per
life), `tools/rsprox/npc_timeline.py` (the recording's blocks) and
`tools/rsprox/ticklog_npc_timeline.py` (ours, with the file-only `npc_info` rows:
spawn angle, ops, level, transparency, exact move, timed bar). Facing is part of npc info
and is audited block by block (the entity face, the coord face and its `instant`, the angle,
the reset, the walk type).

| npc | block | recorded | ours now | verdict |
|---|---|---|---|---|
| Guardian | wake | coord face, then the target entity +1 (4297) | same | fixed |
| Guardian | swoosh | `angle=` the exact move's angle; the first after the wake faces the target from his middle +5 (4297 [208]) | same | fixed |
| Guardian | landing | `reset` 3 ticks before he lands; the entity again on the lift tick | same | fixed |
| Guardian | level | vislevel x party (1.06 / 1.12 / 1.19, +0.065 a raider), on add and every changetype | same | fixed |

## Damage audit (2026-10-21)

Every splat on every raider of the room, put down to its mechanic by
`tools/tfa_mech.py --room guardian` over the `--all-players` transcripts of index
4301..4308 (the party runs are spectated, so the raiders' prayers are not on the
wire: a mechanic's zeros are its prayed hits, its nonzero tail mostly the unprayed
ones). Each mechanic is matched by its own geometry: a projectile by its target and
landing tick, a tile hazard by the map anim or loc on the raider's tile, a swing by
the seq beside the raider.

| mechanic | recorded | ours before | ours now | verdict |
|---|---|---|---|---|
| ranged shot | an even 1..10 under protect from missiles (the recorder's own prayers, 4296..4298), 17..34 unprayed | 0..41, 75% taken | same | matched |
| mushroom poison | 10 decaying (10, 9, 8) | 50 | same | matched |
| vine (tentacle) | 17..19 unprayed or missiles (21), 10 under Protect from Melee (5/5), 10/11 where prayers are unrecorded (22/6); never a miss; one a beat | 17..19, a third blocked, one per vine | 17..19, melee 40% off, no miss, one a beat | fixed (§8) |
| poison 2..4 | single splats on a few raiders (4304) | none | none | **open** (source) |
| the slam | any raider under his 5x5 calls it; lands +1 on whoever stayed under (stepped out 0/21 hit, stayed 2/2: 37, 48) | the target only, at once, max 25 | any raider; +1 on those still under; 0..48 | fixed |
| his head landing | 24..28 on a raider a tile from his 5x5 (6/6), never at 2+ (136/136) | none | 24..28 on the ring | fixed |
