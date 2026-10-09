# SPEC: The Maiden of Sugadinti, Normal mode, trio (solver from scratch)

Abbreviations used in citations:
- M = OSRS-Content/osrs239-content/server/scripts/minigames/minigame_tob/scripts/tob_maiden.rs2
- K = .../minigame_tob/configs/tob.constant
- KM = .../configs/tob_maiden.constant
- NPC = .../configs/tob.npc
- RAID = .../scripts/tob_raid.rs2
- PARTY = .../scripts/tob_party.rs2
- TOB = .../scripts/tob.rs2
- TIM = .../scripts/tob_timing.rs2
- PM = server/scripts/skill_combat/scripts/player/player_magic.rs2
- CS = skill_combat/combat_stats.rs2
- SPL = skill_combat/configs/magic/magic_combat_spells.dbrow
- SR = src/torirsserver/torirs_server_scriptrun.c
- SC = src/scriptrun/scriptrun_core.c
- TL = build/quest_gate/tob_maiden/ticklog.tsv. This is an older Entry-mode solo run. It is used only for engine timing, which does not depend on the mode.
- REF / TC / SCR / TSV / W / WM / WP = the Blert reference README, maiden_trio_crabs README, maiden_normal_3.script.json, encounters/maiden.tsv, and the three wiki pages (the full paths are in section 5).

Tick mapping, from solver_lessons.md rules 2 and 19 and M:15-19:
- Her timer runs in the npc phase of tick T and reads player tiles at the end of T-1.
- A move click made after seeing tick d moves the player during d+1.
- Player queues run before that player moves. Blood damage and the blackstorm landing are both player queues.
- Throughout, `tile_end(t)` means the tile at the end of tick t, and plan step k is `tile_end(d+k)`.

All tiles below are LOCAL to the room origin O. The local tables are in K:448-500 and K:335-374.

---------------------------------------------------------------------------
## 1. ENTRY & START

**Getting in**
- `t.raid.enter("tob","maiden",{mode="normal"})` (raid.lua:440, 556-626). The leader runs `::tobmode 1 <normal>`. Each member runs `::tobjoinroom`, and a joiner lands on the leader's tile.
- Everyone lands at the entrance, local (52,30) (K:2953-2954, RAID:81). That is the corridor side of `tob_arena_barrier`, which runs x49, z29..32 (K:2947, K:3100-3101).
- The boss stands unstarted as `tob_maiden_100` (TOB raid.lua:63). The test checks this with `t.npc.nearest("tob_maiden_100",30)` (tob_maiden_normal.lua:108).

**Start event**
- The leader clicks `tob_arena_barrier` op1. That runs `~tob_start_room` (PARTY:676):
  - `^tob_var_started=1`
  - `^tob_var_clock = S + 9`, where S is the crossing tick (PARTY:682; TIM:217-219; K:178)
  - `^tob_var_scale = party size`, then `~tob_rescale_boss` (PARTY:684-699)
  - a message: "The fight begins: The Maiden of Sugadinti" (PARTY:720). This goes to the crosser only.
- Members then click the barrier themselves (tob_maiden_normal.lua:135). A started room refuses new joins (RAID:333) but not a barrier crossing.
- The first attack is on S+9, then every 10 ticks (K:167, M:415-419). Blert has it on 9 [9-10] (REF:353).

**Perceiving S**
- The leader has the message.
- Everyone can anchor her clock on the first `maiden_attack_special` or `maiden_attack_blood` seq seen on an npc row: that is tick A1 = S+9, and every attack after is A1 + 10n. Her period is never changed in Normal (M:472-475).
- Until A1 is seen, assume A1 = (first tick a barrier crosser is inside) + 9. Per lessons rule 4, that guess may only be used to keep raiders safe.

**Room origin**
- O = (boss.server_x - 26, boss.server_z - 28). Her south-west tile is local (26,28) (K:448-450).
- This holds for every body form (100/70/50/30, dying), because she has `moverestrict=nomove` (NPC:168).
- Verified: TL row 2 spawns her at packed 105283676, which is (6426,92), so O = (6400,64).
- Cross-check: the tob_maiden_normal.lua member stations (6441,94) map to local (41,30).
- **Open:** confirm that the npc row's x is her SW tile for a size-6 npc (SR:928-950 uses `grid_position`).

**Floor and geometry**

| Feature | Local tiles | Source |
|---|---|---|
| Arena | x 23..51, z 18..42 (29 x 25) | K:637-640 |
| Her body | x 26..31, z 28..33. Plain floor, not walk-blocked (K:455-472). Players can't stand on it in practice; the pools exclude it. | K:448-450 |
| Her centre, used for blackstorm targeting | (29,31) | M:174-176 |
| Staircase / spectator cage | (49,28), 3x6 | K:641-642 |
| Exit passage after the room | (40,6) / (41,6) | K:3065-3066 |
| Orator loc | (56,31) | TOB:43-45 |

---------------------------------------------------------------------------
## 2. SYMBOLS

Every name below was checked against the configs. `api_drive.symbol` kinds are npc, obj, loc, seq, spotanim, varp, varbit (SR:540-547).

**npc** (configs/all.npc)

| Name | Line | Notes |
|---|---|---|
| tob_maiden_100 | 908382 | id 8360 |
| tob_maiden_70 | 908488 | |
| tob_maiden_50 | 908594 | |
| tob_maiden_30 | 908700 | |
| tob_maiden_dying_a | 908806 | |
| tob_maiden_dying_b | 908911 | |
| maiden_elemental | 909016 | the Nylocas Matomenos crab, size 2 (K:454) |
| maiden_blood_slug | 909133 | the blood spawn |

Her body changes type at 70/50/30% (M:498-521), so key on any of the four forms, or use `base_npc_id`.

**seq** (configs/all.seq)

| Name | Line | Meaning |
|---|---|---|
| maiden_attack_special | 316528 | 8092, the blackstorm |
| maiden_attack_blood | 316492 | 8091, the blood throw |
| maiden_death_a | 316581 | |
| maiden_death_b | | |
| elemental_spawn | 316784 | 8098, plays when a crab spawns |
| elemental_death | 316735 | 8097 |
| elemental_idle / elemental_walk | | |

**spotanim** (configs/all.spotanim)

| Name | Line | Meaning |
|---|---|---|
| maiden_shadow_proj | 22629 | 1577, the blackstorm projectile |
| maiden_blood_proj | 22642 | 1578, the blood projectile |
| maiden_lingering_blood | 22655 | 1579, the pool's map graphic |
| ice_barrage_travel | 5235 | 368 |
| ice_barrage_impact | 5248 | 369 |

**loc** (configs/all.loc)

| Name | Line | Meaning |
|---|---|---|
| tob_maiden_blood | 2547391 | 32984, the blood-spawn TRAIL (grounddecor). Pools place no loc (M:1273-1295). |
| tob_arena_barrier | 2530314 | |

**varbit** (configs/all.varbit)
- `varb4116_prayer_protectfrommagic` (:24699)
- `varb4070_spellbook` (:24423). Set to 1 for Ancients, as the test kit does.

**obj**: see the kit in section 7.

---------------------------------------------------------------------------
## 3. HAZARDS

**Her clock**
- She attacks on A1 = S+9, then every 10 ticks (K:167, 178; M:415-419).
- Each attack is EITHER a blackstorm OR a blood throw (M:584-590).
- Blood eligibility: no blood on the two attacks after a throw (K:184, M:596-599). After that, each attack rolls 1 in 3 (K:197, M:610). Blert trio: 3 [1-6] throws against 12 [10-16] storms per room (REF:350-351).

### H1. Blackstorm

**Decided** on attack tick T, in the npc phase. It reads `tile_end(T-1)`.

**Target:** the living, un-caged raider with the smallest `distance(centre(29,31), tile)` (Chebyshev). A tie goes to the LOWER orb index, which is the party order with the leader at 0 (M:632-670, M:236-244). Hunt range is 24 (K:626).

**Damage:**
- `random(0 .. floor((365 + 35c)/10))`, where c is the number of leaks (M:37-39, M:818-829; K:253-254). At c=0 that is 0..36.
- It always "hits"; there is no accuracy roll (M:804-817).
- Protect from Magic halves it (K:285). The prayer is read at LAUNCH, tick T, in the target's context (M:826).
- It is an overhit. The lethal verdict is taken at launch, so it can't be tick-eaten (M:705-715).

**Drain:** 50% chance (K:255). The amount is (dmg+1)/5, taken from the stat backing the target's highest attack bonus at launch (M:716-725, M:868-895). For a ranger that is Ranged. A melee weapon worn at T drains Attack and Strength instead (that is the "bow flick").

**Landing:** `queue*` with delay floor(170/30) = 5, so it lands on T+5. Measured in TL: 8092 on 51 and hit_player on 56; 71→76; 81→86; 101→106.

**Prayer timing:** Protect from Magic must be on at the npc phase of T. Turn it on by a click seen on T-2 at the latest, which leaves one tick of margin (see the open question in section 8). Its state at landing does not matter.

### H2. Blood throw (pools)

**Decided** on attack tick B. Each raider's pool tile is `tile_end(B-1)` (M:984-1006). Seven pool slots exist, so a trio uses 3 + 2 = 5 (K:3357, M:983).

**One pool per player tile.** It is skipped if that tile:
- is her body (M:1196-1212),
- is map-blocked,
- already carries a trail loc (M:1184), or
- was already claimed by this throw (M:1217-1229).

Two raiders on one tile share one pool.

**Extras:** two more pools, at random tiles in a 5x5 centred on the FURTHEST raider (by `npc_range`). They land exactly 1 tick after that raider's pool, because their flight is +25 cycles (M:1015-1066; K:529). They are always two unless no tile in the 5x5 can take one.

**Landing:**
- land = floor((50 + 15d) / 30), where d = `npc_range` from her footprint (M:1100-1101; K:527-528). The extras use floor((50 + 15d + 25) / 30).
- TL: B=61, end 170 → spotanim on 66; extras end 195 → 67. Both match.
- Do not compute d yourself; read it off the packet (section 4).

**Damage window:** a tick T with T-B in [land, land+10] (11 ticks; K:209; M:2123-2154).

**Damage read:**
- The per-raider watchdog runs as a player queue every tick (RAID:1500-1513, 1717-1720). On tick T it reads `tile_end(T-1)`.
- So the pool tile is FORBIDDEN at the ends of ticks B+land-1 through B+land+9.

**Damage per tick:** 10 + 2c (K:329-330, M:44-45). It also:
- drains prayer by dmg/2 (M:2104), and
- HEALS her by the same amount (M:2105, M:1660-1669, clamped to her scaled maximum).

**Dodge slack:** the pool tile is your own `tile_end(B-1)`, and land ≥ 2 (d ≤ 2 gives 65-80 cycles). A click seen on B moves you during B+1. So land-1 ≥ 1 tick of slack always holds.

**Blood-spawn roll:** made when each pool EXPIRES, at T-B ≥ land+11 (M:1260-1262, M:1306-1329).

| Pool | Chance | Source |
|---|---|---|
| Nobody stood in it | 10% | K:235 |
| Someone stood in it | 20% | K:236 |
| No pool of the throw was stood in at all | 5% | M:1349-1356 |

Caps: per throw, 1 + (number of pools stood in) (K:243, M:1313-1316); total, 8 alive (K:238).

### H3. Blood spawn (`maiden_blood_slug`)

| Property | Value | Source |
|---|---|---|
| HP (trio) | 90 | K:675 |
| Attacks | none | NPC:1349-1363 |
| Retaliates | no | NPC:1349-1363 |
| Movement | `blockwalk=none`, `moverestrict=passthru` | NPC:1349-1363 |

**Each tick, in its timer (npc phase):**
1. It lays a trail loc `tob_maiden_blood` on its CURRENT tile, which is `tile_end(T-1)`, unless that tile is blocked or her body. The trail lasts 30 ticks; laying it again resets the 30 (M:1710-1713; K:223, K:211-222).
2. It then walks one tile a tick toward a destination within 10 tiles (K:227). It redraws on arrival, on a stall, or 1 tick in 3 (K:228; M:1776-1814). It never pauses (KM:57-62).

**Trail damage (Normal):** uniform 5..13 per tick (KM:42-45; M:2156-2163). It also drains prayer by dmg/2 and heals her (M:2083-2108).
- A trail laid on T damages, on T itself, a raider whose `tile_end(T-1)` is the slug's `tile_end(T-1)`.
- So never end a tick on a slug's tile.
- The slug's next tile is unknown: it is one of 8 neighbours, usually continuing its last step.

**Trail perception:** the bot sees the trail as a loc add on tick T. It is live through T+29.

**Kill rule:** kill slugs (90 HP, Defence 0, NPC:1352). A trail is permanent danger for 30 ticks.

### H4. Nylocas Matomenos crabs

**Spawn**
- When her hp% ≤ 70 / 50 / 30 of her SCALED maximum (2625 in a trio, K:645), checked every tick in her timer (M:486-522). One hit can cross two thresholds, and then both waves spawn.
- Count: 2 × scale = 6 crabs (M:56-72; K:233-234). They spawn on a uniformly random 6 of the 10 points (M:270-345).
- 3% of waves are "scuffed": every crab is shifted to its alternate tile (K:386, M:279-286).
- Each crab has 75 hp as its pool (K:667, M:333-334). Stats: Defence 100, Magic 100 (NPC:1250-1262). It never attacks (NPC:1263-1270) and does not retaliate. It is passthru.
- Message: "The Maiden calls for aid!" (M:572).

**Spawn points** (local SW anchor; K:335-374)

| Point | Tile | Scuffed tile |
|---|---|---|
| N1 | (37,40) | (38,41) |
| N2 | (41,40) | (42,41) |
| N3 | (45,40) | (46,41) |
| N4i | (49,38) | (50,39) |
| N4o | (49,40) | (50,41) |
| S1 | (37,20) | (38,19) |
| S2 | (41,20) | (42,19) |
| S3 | (45,20) | (46,19) |
| S4i | (49,22) | (50,21) |
| S4o | (49,20) | (50,19) |

**Walk**
- No step on the spawn tick C (K:97-102, M:1460-1463).
- From C+1, one tile a tick toward her SE tile (31,28) (M:1508-1509; K:482-483). The stepper is greedy, diagonal first.
- The agent's sources say "straight at her"; the content and its cited Blert streams say the SE tile (M:1495-1507). Content wins.

**Leak**
- Checked in the crab's timer: its SW anchor at `tile_end(T-1)` is inside x 24..32, z 26..34 (K:485-500; M:1524-1534).
- Then she heals 2 × the crab's current HP, and c += 1 (M:1551-1580). c permanently raises the storm max (+3.5) and the pool damage (+2/tick).

**Derived steps to the leak rectangle and leak tick** (greedy walk to (31,28)):

| Points | Steps | Leak tick | Last safe freeze cast |
|---|---|---|---|
| N1, S1 | 6 | C+7 | C+5 |
| N2, S2 | 9 | C+10 | C+8 |
| N3, S3 | 13 | C+14 | C+12 |
| N4i, N4o, S4i, S4o | 17 | C+18 | C+16 |

These are derived, not measured; verify on the tick log. Blert's first observed leak is 6-9 ticks after the spawn (TC:21). The wiki says the 3s and 4s clump at +11 and +16 (W:528).

**Freeze (Ice Barrage)**
- The freeze is applied on the CAST tick, in the player phase (PM:113-171, PM:579-583). It lasts 32 ticks (SPL:678).
- Rooms are instanced, which makes them multiway (CS:776-786). The barrage therefore hits a 3x3 radius 1 around the primary's anchor (PM:147-165). A secondary re-freezes only if it is not already frozen (PM:161).
- Accuracy on a crab, magic only: chance = (lvl_now × (bonus+64) − lvl_base × 64) / (lvl_base × 140). That is bonus/140 unboosted, and 100% at +140 (M:1956-1995; K:244; called from CS:824-836).
- A crab frozen on cast tick F stays at `tile_end(F)`. It moved during F's npc phase before the cast. So a crab is stopped short only if the cast lands on or before the "last safe freeze cast" column above.
- A crab frozen INSIDE the rectangle still leaks; the arrival test ignores freezes (M:1464).
- After 32 ticks it resumes walking (its `npc_walk` is re-issued every tick, M:1468).
- Barrage max hit is 30 (SPL:674).

### H5. Heals, as a cost of time, not damage
- Pools and trails heal her by the damage they deal.
- Leaks heal her by 2 × the crab's HP.
- Blert trio: healed 223.5 [0-821] per room (REF:357).

### H6. End of the room
See section 6.

**Not hazards in content** (wiki claims with no content behind them):
- Blood spawns throw nothing. WP's "blood spawn projectiles" has no content counterpart.
- Hard-mode ramp and attack speed-up are not in Normal (M:452-478, 820-822).

---------------------------------------------------------------------------
## 4. PERCEPTION

**Fixed facts about the drive API**
- `api_drive.tick()` is the server tick.
- Wake on `server_tick`, per lessons 3 and 31.
- Npc and player rows: use `server_x` / `server_z` (lesson 26).

| Hazard | Packet | Verb and field | Seen on |
|---|---|---|---|
| Her attack kind | npc seq | `npcs()` row `seq_id` == maiden_attack_special / _blood, `seq_tick` | T (TL: npc_anim 8092/8091 on T) |
| Storm target | MAP_PROJANIM 1577 | `projectiles()`: spotanim_id 1577, dst = target's `tile_end(T-1)`, target = player, `cycles_left` | T (TL row 26). The dst names who was picked. Lands T+5 (queue, not the projectile). |
| Pool tiles | MAP_PROJANIM 1578 ×(3+2) | `projectiles()`: dst_x/dst_z = pool tile (SC:1180-1207); end_delay → land = floor(end/30) via `cycles_left` + launch | B. land is exactly floor(end_delay/30) (TL: 170→+5, 195→+6). |
| Pool landing | MAP_ANIM 1579 | `spotanims()` x,z | B+land, the first damage tick. **Too late to dodge**: use the projectile. Scriptrun retires a map anim after 40 ticks (SR:1131-1134), so pool life must be the bot's own B+land..B+land+10 bookkeeping. |
| Trails | LOC_ADD_CHANGE / LOC_DEL tob_maiden_blood (grounddecor) | `locs()` (SC:1117-1130) | The tick it is laid. Expiry = LOC_DEL (the server's revert), or assume laid+30. **Check** that scriptrun's locs list keeps grounddecor shape 22. |
| Slugs | npc add | `npcs()` npc_id maiden_blood_slug | Tile each tick. Destination not perceivable. |
| Crabs | npc add + seq elemental_spawn | `npcs()` npc_id maiden_elemental | C (spawn). Steps seen C+1... Frozen state not sent; infer from npc row `spotanim_sent_id` == ice_barrage_impact or from no step. HP via `health_ratio/health_scale` after the first hit. |
| Leak | crab death seq elemental_death on a crab inside the rectangle that was not killed by us; her `health_ratio` rising | `npcs()` | Tick of the leak. Count c yourself (no varp exposes `^tob_var_leaks`). |
| Her HP / thresholds | npc headbar; boss HUD bar | `npcs()` `health_ratio/health_scale`; the type change to tob_maiden_70/50/30 | Each tick |
| My HP / prayer / stats drain | stat | `skill("hitpoints")`, `skill("prayer")`, `skill("ranged")` | Each tick |
| Prayer on | varbit | `varbit(symbol("varbit","varb4116_prayer_protectfrommagic"))` | Each tick |
| Teammates | player info | `players()` server_x/z, pid | Each tick |
| Messages | game message | `messages()` | "The fight begins…", "The Maiden calls for aid!" (M:572), the wave-end line |

**Gaps** (things the bot cannot perceive)
- **c, the leak count.** The bot must infer it: from her heal of 2× crab HP plus `elemental_death` on an unhurt crab inside the rectangle, or from a storm max above 36.
- **The blood-spawn odds.**
- **The roll outcome before the anim.** Blood vs storm is known only on T, from the seq. The blood eligibility count IS deducible from history.
- **Freeze state.** There is no flag.
- **A slug's destination.**

---------------------------------------------------------------------------
## 5. WHAT REAL TRIOS DO

Source paths:
- REF = docs/minigames/theater_of_blood/sources/blert_api/reference/README.md (24 death-free Regular trio rooms)
- TC = .../blert_api/maiden_trio_crabs/README.md (26 rooms)
- SCR = .../reference/maiden_normal_3.script.json
- TSV = docs/minigames/theater_of_blood/encounters/maiden.tsv
- W = sources/wiki_Theatre_of_Blood_Strategies.wikitext
- WM = sources/wiki_The_Maiden_of_Sugadinti.wikitext
- WP = sources/wiki_Perfect_Maiden.wikitext

**Roles**
- Every room is dps1 + dps2 + one freezer (REF:346; TC:27; W:611).
- DPS weapons: scythe in 25/25/19 of 26 rooms, blowpipe 6/7/4 (TC:32-33).
- Freezer: Accursed sceptre barrage about 80% (TC:28-30), and a twisted bow on her.

**Positions** (offsets from her SW tile; local = (26,28) + offset)
- DPS on her NE corner at (5,6), (4,6) or (6,5), local (31,34)/(30,34)/(32,33), gap 1 (REF:517-524, 396, 421). They are the closest, so they tank the storms.
- Freezer about 10 out east, at (15,0), local (41,28) (REF:525, 528), for 100% and 70%. At 50% the distance is 7 [1-10]. At 30% the freezer is in melee at (6,0) (REF:526, 450).
- DPS Protect from Magic is right on about 50% of attacks (REF:411, 436). The freezer is unprayed in 20/24 rooms.

**Crabs**
- The freezer pre-aims; the first barrage hit lands at +1 (TC:30, REF:495).
- First freezer hit by lane: S1 +1, S2 +3.5, N2 +6, N3/S3 +11, the 4s +16 (SCR seg 70).
- N1 is left to the DPS (first hit +4).
- The priority is the 3s/4s clump (W:651; WM:129).
- Leaks are normal: median 5 per room [0-13] (REF:362). By wave: 1 / 2 / 2.
- Killed before reaching her: 5, 4 and 0 of 6 (TC:18).

**Room length**: median 157.5 ticks [132-204] (REF:379).

| Phase | Start tick | Length |
|---|---|---|
| 100→70 | — | 42 |
| 70% | 42 | 30 |
| 50% | 69.5 | 40 |
| 30% | 107 | 48 |

(REF:366-378)

**Damage**: HP lost per room on the recorder (REF:359-361). Storm on the recorder: median 12.5, max 62. Blood: median 0. Zero deaths (REF:354-358).

| Role | Median | Range |
|---|---|---|
| dps1 | 60.5 | 24–106 |
| dps2 | 102 | 92–133 |
| freezer | 65 | 18–142 |

**Specs**: hammer first, BGS (W:632-633).

**Perfect Maiden** (WP:7, 14):
- no trail damage,
- no damage taken off-prayer, and
- no crab heals her.

---------------------------------------------------------------------------
## 6. ROOM END

**Killing blow K**
- Her `[ai_queue3]` runs at K+1 because `death_delay=0` (NPC:176; TOB:476-548). It:
  - deletes every live crab and slug (M:2007-2046),
  - deletes every trail loc and clears the pool registers (M:2055-2073, M:2015-2022),
  - retypes her to `tob_maiden_dying_a` and heals her to base (TOB:504, 518).
- `maiden_death_a` is sent at K+1 by the engine.
- K+5: `tob_maiden_dying_b` plus `maiden_death_b` (TOB:548, 559-562; K:440-441).
- K+9: `~tob_room_cleared` (TOB:573-591). It:
  - sends the wave/split message,
  - restores the party,
  - sets `^tob_var_cleared=1`,
  - places the chest (RAID:843-856),
  - deletes her.

**End races**
- A blackstorm already in flight lands after her death unless the target has left the instance, died, or been caged (M:897-917). A storm launched on K-4..K still lands.
- Pools stop counting: the registers are cleared at K+1, and the sweep reads the registers.
- A crab arriving on K is left dying, not deleted (M:2029-2036).

**Pass marker**: her body types gone, or `tob_maiden_dying_b` seen, or the room-cleared message. Then the exit is (40,6).

---------------------------------------------------------------------------
## 7. PROPOSED SOLVER SHAPE

This follows Verzik P3: MEASURE → CLOCK → CONTEXT → `api_drive.plan` → EMIT, every server tick.

All three run the same rule on the same view. Roles are fixed by seat:
- seat 0 (leader) = TANK
- seat 1 = FREEZER
- seat 2 = DPS2

### Clock
- A1 = first attack seq. Attack ticks are A1 + 10n.
- `blood_cd` comes from the history of seen throws: no blood possible on the 2 attacks after one.
- Crab waves are keyed on their spawn tick C.

### Contexts (the planner horizon H = 8-10)

**C0 PRE.** The leader crosses the barrier; the others cross and walk to their stations. Pre-boost and set prayers. Station tiles (local):
- TANK: NE face, gap 1, at (32,33)/(31,34). This is the closest to centre (29,31) by a margin, so it is always her storm target. Blert's DPS tile.
- DPS2: gap 1 at (32,32) or (30,34). It must be FARTHER from (29,31) than the tank, or tie with a higher orb index. The tank is orb 0 and wins all ties, so both may stand at Chebyshev 3.
- FREEZER: (41,28)-(41,30), about 10 east, central between the N and S rows. It reaches both rows within range 10.

**C1 STEADY**, between waves:
- TANK: goal = her footprint, side N/E.
- DPS2: goal = her footprint, plus a zone that costs gap < tank's.
- FREEZER: a pull to its station.
- All three: forbid the live pool tiles, trail tiles and slug tiles below.

**C2 BLOOD (B seen).**
- Forbid each pool tile from the projectile dsts for ticks [B+land-1, B+land+9]. Use tier DAMAGE, cost 10+2c per tick, with the extras at land+1.
- Every raider stands on its own pool tile at B. The plan must step off by the end of B+land-1, and must not step onto a teammate's pool or an extra.
- The cheapest dodge is one tile along her face, staying gap 1. The tank must stay the closest after the dodge, or the next storm moves.

**C3 CRABS (wave at C).**
- FREEZER: goal = a cast tile within 10 of the clump target.
  - Cast schedule: S1/N1 by C+5, the 2s by C+8, the 3s/4s clump by C+12..16.
  - Choose the primary to maximise the crabs in its 3x3 at cast.
  - Re-cast on the clump before frozen+32.
- TANK and DPS2: kill the unfrozen singles nearest her first (N1/S1 lead the leak clock). Then the frozen clump, then her.
- A plan zone forbids standing within 1 of a crab's predicted tile (crabs pass through but body-block attacks?).
- Constraint for all three: the attack press on a crab moves you (lesson 8). Use the "walk then attack" plan (lesson 18).

**C4 SLUGS.** Each slug becomes zones with `lo=0`, `hi=k`, t0=t1=now+k, k = 0..2, on its tile. Its tile at now+k is unknown within k. Use tier DAMAGE, cost 9/tick. Target slugs with DPS2 when no crab is alive.

**C5 END.** After K, stop attacking. If a storm is in flight, keep the prayer; then walk to the exit (40,6) or wait for the next room.

### Plan constraints (collision_map.h:890-1025)
- **forbid[]**: pool tiles over their windows, and trail tiles over [laid, laid+29] (end-tick windows shifted by -1). Max 256: with 8 slugs × 30 ticks the trail set can reach about 240 tiles. Keep only tiles within the horizon's reach (a Chebyshev of H×2 from me).
- **zones[]**:
  - her body (26..31, 28..33), `require=0`, lo=0 hi=0, LETHAL-ish (cannot stand).
  - DPS2's "not closer than the tank" band.
  - the slugs' uncertainty disks.
- **pulls[]**: the freezer's station; the tank's NE corner.
- **goal**: her footprint (26,28,size 6), side 1 (N) or 2 (E), `under_ok=0`.
- **chasers**: none. Crabs walk to her, not to you.
- **edge**: the arena box x 23..51, z 18..42, margin 1.

### Non-movement channels, in order: prayer → food → gear → spec → attack
- **Prayer:** TANK and DPS2 keep Protect from Magic plus Rigour. The tank never drops Protect from Magic, because it read at T and the target is predictable. The freezer keeps Protect from Magic if prayer allows, plus Augury when casting. Sip a restore at prayer < 25.
- **Food:** eat at HP ≤ 60. The worst case is a storm of 36+3.5c halved to 18+ plus a pool tick; the overhit's lethal verdict is taken at T against current HP, so eat by T-1. Brew at ≤ 45.
- **Spec:** dragon_warhammer (exists in all.obj) on her at the start, by the tank. Then attack.
- **Freezer:** Ice Barrage (spellbook varbit 1) on crabs only. Twisted bow on her between waves.

### Kit
Every name was checked as an obj in configs/all.obj:
- **Tank / DPS2:** twisted_bow, dragon_arrow, masori_mask, masori_body, masori_chaps, avas_assembler, dragon_warhammer (tank), toxic_blowpipe (optional), anglerfish ×20, br_4dosepotionofsaradomin ×2, br_4dose2restore ×3, br_4doserangerspotion. A scythe_of_vitur exists if melee is wanted. Ranged keeps the raiders off the crab paths and is what the harness supports.
- **Freezer:** ancestral_hat, ancestral_robe_top, ancestral_robe_bottom, kodai_wand, arcane, eternal_boots, magus_ring. That totals +140 per the test's own sum (tob_maiden_normal.lua:29-32), which gives a 100% freeze.
  - Runes: waterrune, deathrune, bloodrune (the all.obj names; the test types water_rune etc., so check that `::give` aliases them).
  - anglerfish, br_4dose2restore. sanguinesti_staff exists as an alternative.

### Pass criteria, from the tick log (`hit_player` / `npc_heal` / leak rows), over 16 seeds (`--name`)
- **(a)** All three alive; the room ends (dying_b seen); no script aborts.
- **(b)** Room ticks from S to K ≤ about 200. The Blert median is 157.5, max 204.
- **(c)** Zero pool or trail damage on any raider. That is a Perfect-Maiden property, and Blert's median blood damage is 0.
- **(d)** Every storm on a raider prayed: hit ≤ 18 + 2c. Zero unprayed storms.
- **(e)** Leaks ≤ 5 per room (the Blert median), stretch 0. Read from `[proc,tob_maiden_leak_heal]` npc_heal rows (M:1671-1674).
- **(f)** HP lost per raider ≤ 106 (the Blert dps1 max). Report storm, pool and trail separately.

---------------------------------------------------------------------------
## 8. OPEN QUESTIONS & RISKS

1. **Prayer timing.** Is a Protect from Magic toggle clicked after seeing d active at the npc phase of d+1? This depends on whether the prayer is applied in client input or in the player's turn. Measure with lag.py's "press to prayer bit". If it is the player's turn, the prayer must be on by d+2.
2. **Threshold detection lag.** Player hits on her land as npc queues ([ai_queue3] → npc_damage) in the npc phase. If her timer runs before her queue in that phase, a crossing is seen a tick late. Read the hit_npc vs npc_spawn rows.
3. **Crab walk.** The content says she walks to her SE tile (M:1508). Derived leak ticks are C+7/10/14/18. Verify per point on a scriptrun log (lesson 28). The agent's Blert summary disagrees on the goal tile; content wins, but measure.
4. **Barrage 3x3 for size-2 crabs.** Does `npc_findallany(centre,1)` match by anchor or by footprint? This decides clump-freeze counts.
5. **Freeze perception.** No flag is sent. Is `spotanim_sent_id` (369) on the npc row reliable on both lanes?
6. **Trails in scriptrun.** Confirm that `locs()` lists the grounddecor trail adds, and that LOC_DEL arrives on the server's 30-tick revert. TL shows 1159 loc_set rows, so the server does send them.
7. **Pools on members.** The test header says only the barrier-crosser takes pool damage (tob_maiden_normal.lua:21-23, "CONTENT_BUGS seam19"). RAID:1486-1513 now arms every raider (`~tob_arm_party_watchdogs`). Treat pools as hurting all three, and check the tick log.
8. **First-attack anchor.** TL marks "room start" at 43 and the first 8092 at 51, which is not 43+9. Which tick does "mark" record? Anchor on the seen anim, not the barrier.
9. **The storm target is fully predictable**, so the tank can absorb every storm prayed. But if the tank must dodge a pool along her face, it must stay strictly closest. One dodge to Chebyshev 4 hands the storm to DPS2. That is safe if DPS2 also prays, so keep Protect from Magic on everyone.
10. **Forbid cap of 256** with long slug lives; a scuffed spawn shifts crabs +1/+1 (3% of waves); a two-threshold hit spawns 12 crabs at once.
11. **Heal clamp.** A heal is clamped to her scaled maximum (M:1660-1669), so a leak at near-full HP is cheaper than it looks. Low priority.
12. **Harness kit runes** are named water_rune / blood_rune / death_rune, but all.obj has waterrune, bloodrune, deathrune. Check the `::give` alias.
