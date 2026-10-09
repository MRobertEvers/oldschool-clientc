# SPEC: The Nylocas, Normal mode, trio (waves 1-31 + Nylocas Vasilias)

Abbreviations for citations:
- `N:` = OSRS-Content/osrs239-content/server/scripts/minigames/minigame_tob/scripts/tob_nylocas.rs2
- `B:` = .../scripts/tob_nylocas_boss.rs2
- `D:` = .../scripts/tob_damage.rs2
- `P:` = .../scripts/tob_party.rs2
- `RA:` = .../scripts/tob_raid.rs2
- `C:` = .../configs/tob.constant
- `NC:` = .../configs/tob_nylocas.constant
- `DB:` = .../configs/tob_nylo.dbrow (generated from blert_nylocas-waves.json)
- `W:` = docs/minigames/theater_of_blood/nylocas_waves.md
- `R:` = docs/minigames/theater_of_blood/sources/blert_api/reference/README.md (27 death-free Regular trio rooms, nylocas_normal_3.json)

The tick mapping is the one in solver_lessons.md rule 2, and every deadline below uses it. An npc decision on tick T reads player tiles at the END of T-1. My click after seeing tick d acts in d+1's player phase, so a plan's step k is my tile at the end of d+k.

There is a corollary that matters here. Any "read" a hazard makes in the npc phase of T sees prayer and gear as they stood after the player phase of T-1. That covers the wave swing's prayer, Vasilias' prayer, and a hit's style check. A prayer pressed after seeing d is therefore live for npc swings from d+2. This needs confirming once with lag.py (press to prayer bit).

---------------------------------------------------------------------------

## 1. ENTRY & START

**Entry (harness).** `t.raid.enter("tob","nylocas",{mode="normal"})`: the leader types `::tobmode`, then each member types `::tobjoinroom normal` (script/plugins/quest_driver/raid.lua:380-420). The party then waits at `t.party.barrier("entrance")` (test/raids/tob_nylocas_normal.lua:94-95). Two tiles to know:
- The arrival tile is local (31,49) (C:3026-3027).
- The entry tile is local (31,33) (C:2957-2958).

**Start.** `tob_arena_barrier` lies on the room's north wall at local z=31, crossed north-south (gate (31,31), C:3104-3105). Clicking it does the following:
- **Only the party leader** (orb slot 0) can start the room. It picks "Yes, begin the fight." (P:619-634), and that calls `~tob_start_room` (P:676-732).
- A member clicking before the start gets "You must wait for the party leader to start the fight." (P:621-624).
- After the start, any click just steps the player across (P:612-614, P:643-660). The landing tile is z-1, i.e. local (x,30) when crossing from the north.
- The fight tile is (31,30) (C:3270-3271).

`~tob_start_room` runs in this order:
1. It sets `started`, `room_start = map_clock` and `scale = party size` (P:677-683).
2. It prints "The fight begins: The Nylocas" (P:716).
3. It calls `~tob_nylo_begin` (P:732, N:177-189). That spawns the **four supports** (they do not exist before the start) and sets `next_wave = map_clock + 4` (C:1032). It also opens the HUD bar at full.

**Room tick 0** = the tick the leader crossed, which is the first tick the four `tob_nylocas_support` npcs are seen. Wave 1 is due on room tick 4 (N:167-176). Blert trios: 4 [4-8] (R:566).

**Origin.** The room is one map square. Local coords run 0..63, and `~tob_ncoord(0,0)` is the square's origin (N:892-896). From any perceived tile: `base = (x - x%64, z - z%64)`. Two cross-checks:
- The SW support npc stands at base+(25,18).
- Example instance in W:157: SW support (3289,4242), so base = (3264,4224).

Geometry, all room-local (C:1105-1159, W:145-160):

| thing | local tiles |
|---|---|
| arena box (players and nylos fight here) | x 26..37, z 19..30 (12x12) (C:1156-1159) |
| support npc SW anchors (size 3) | SW (25,18), NW (25,29), SE (36,18), NE (36,29) (C:1138-1145) |
| blocked 2x2 inside room | SW (26-27,19-20), NW (26-27,29-30), SE (36-37,19-20), NE (36-37,29-30) (W:162-165) |
| nylo walk target = support centre | anchor+(1,1) (N:1044-1047); a chewer stops at npc_range <= 2 of it (C:1458, N:973-978) |
| lane spawns (outside the box, floorless) | W (17,24) S-slot / (17,25) N-slot; S (31,9) W-slot / (32,9) E-slot; E (46,24) S / (46,25) N; E big (45,24) (C:1105-1120, N:382-397) |
| lane mouths (where a nylo enters the box) | W x=26, E x=37, S z=19 (N:780-806, N:871-890) |
| room centre | (31,24) (C:1450-1451) |
| Vasilias lands (SW tile, size 4) | (30,23), so she covers (30..33, 23..26) (C:1335-1336) |
| orator loc | (32,38) (tob.rs2:36) |

Lanes run outside the box. A nylo cannot leave a lane except through its mouth (C:1152-1155), and players never stand in a lane.

## 2. SYMBOLS

All names were verified in configs; npc ids are from sources/cache_npc_nylocas.txt.

**npc** (configs/all.npc, minigame_tob/configs/tob.npc:1514-2424):
- `tob_nylocas_incoming_melee` 8342, `_incoming_ranged` 8343, `_incoming_magic` 8344. These are small, pillar-bound (category 1238), size 1.
- `tob_nylocas_big_incoming_melee` 8345, `_ranged` 8346, `_magic` 8347. These are big (size 2).
- `tob_nylocas_fighting_melee` 8348, `_ranged` 8349, `_magic` 8350. These are aggro or retargeted (category 1239), size 1.
- `tob_nylocas_big_fighting_melee` 8351, `_ranged` 8352, `_magic` 8353, size 2.
- `tob_nylocas_support` 8358: size 3, `nomove`, `hitsplat=no`, `healthbar_standard_80`, base 330 (tob.npc:2135-2146).
- `nylocas_boss_spawning` 8354, `nylocas_boss_melee` 8355, `nylocas_boss_magic` 8356, `nylocas_boss_ranged` 8357, all size 4. Blert's extract.py swaps the 8356/8357 labels. Use the **names**, which B:542-569 maps.
- Names: Ischyros = melee (grey), Toxobolos = ranged (green), Hagios = magic (blue).
- Do NOT use the `_hard` / `_story` variants or `nylocas_miniboss_*` (Hard-mode Prinkipas, waves 10/20/30 only, N:284-300).

**seq** (configs/all.seq):
- `top_spider_melee_attack`, `top_spider_ranged_attack`, `top_spider_magic_attack`: the swing and the bite (N:1242-1249).
- `top_spider_{melee,ranged,magic}_death_detonate`: the natural explosion (N:1345-1352).
- `top_spider_melee_spawn_noloop`: Vasilias' drop-in (N:319-321).
- `nylocas_pillar_collapse`: a loc anim (N:1851-1854).

**spotanim** (configs/all.spotanim):
- Projectiles:
  - `tob_nylocas_rangedprojectile_size1`: small ranged.
  - `_sizemid`: big ranged.
  - `_size2`: Vasilias ranged.
  - `inferno_babysplitter_mage`: small magic.
  - `inferno_babysplitter_mage_big`: big magic.
  - `inferno_babysplitter_mage_biggest`: Vasilias magic.
  - Sources: N:1230-1240, B:286-289.
- Map graphics:
  - `tob_nylocas_shielded`: a wrong-style or nulled hit lands on the nylo's SW tile (D:316-320).
  - `tob_nylocas_death_{melee,ranged,magic}_standard`: a small killed (N:1376-1383).
  - `tob_nylocas_death_{style}_detonate`: a small exploded, drawn on its despawn tick (N:1385-1395).

**loc** (configs/all.loc):
- `tob_arena_barrier`.
- `tob_nylocas_support_pristine` 32862, `_collapsing` 32863, `_dead` 32864. The loc sits at anchor+(1,1), or at the anchor itself (N:1829-1849).

**varbit**: `varb6447_tob_client_waveprogress_type`, `varb6448_tob_client_waveprogress_val`, `varb6449_tob_client_waveprogress_max`.
- During the waves, VAL is the permille of the 4 supports' combined hp against 4x230 (N:56-90, tob_hud.rs2:220-226).
- After she lands, VAL is Vasilias' permille (B:115-121).
- Also `varb4070_spellbook` (1 = Ancients).

**varp**:
- `varp172_option_nodef`: auto-retaliate (1 = off). The harness turns it off (tob_nylocas_normal.lua:117-126).
- `varp6886_tob_boss_uid` names Vasilias once she lands (B:31-33).

**obj** (configs/all.obj):
- Weapons: `scythe_of_vitur`, `blade_of_saeldor`, `abyssal_whip`, `ghrazi_rapier`, `twisted_bow`, `toxic_blowpipe`, `magic_shortbow`, `rune_arrow`, `dragon_arrow`, `chinchompa_black`, `tumekens_shadow`, `sanguinesti_staff`, `eye_of_ayak`, `kodai_wand`.
- Supplies: `anglerfish`, `4dosepotionofsaradomin`, `4dose2restore`, `br_4dosepotionofsaradomin`, `br_4dose2restore`.
- Runes are `waterrune`, `chaosrune`, `deathrune`, `bloodrune`. NOT `water_rune`; the harness's `::give water_rune` must be an alias. Check it.

**messages**:
- "The chamber is full - the next wave is held back." (N:160-165)
- "The chamber shakes." (N:337, last nylo despawned)
- "Nylocas Vasilias drops into the chamber." (B:113)
- "Your attack has no effect on this Nylocas." (D:319)
- "A support collapses!" / "The roof gives way." (N:1815-1821)

## 3. HAZARDS (content, Normal, scale 3)

### 3.1 Wave clock (N:105-158, C:1031-1039, timing.rs2:191-206)
The room is a 4-tick cycle. Wave n+1 spawns on the first room tick that satisfies all three:
- room tick ≡ 0 mod 4;
- map_clock >= next_wave;
- the live count < cap(wave already out).

Spawning wave n sets `next_wave = map_clock + stall(n)` *before* it adds the npcs (N:191-212). A capped wave is retried 4 ticks later (N:146-150).

**Cap**: 12 while the last spawned wave is < 20, then 24 (C:1034-1039). Wave 20 is gated by 12 and wave 21 by 24. The count includes corpses until DESPAWN (N:560-618).

**Stalls** (DB, W:21-53) are 4,4,4,4,16,4,12,4,12,8, 8,8,8,8,8,4,12,8,12,16, 8,12,8,8,8,4,8,4,4,4,0. They sum to 232, so the uncapped floor is wave 31 at room tick 236 (C:1062-1063).

**Schedule** (W:23-53). There are 120 spawns: 43 big, 35 aggro, 53 flickers from wave 16 (C:1090-1093). Per-cell pillar targets are in W:92-122.

One cell is in conflict: wave 30, south, second spawn. DB says mel→rng→mel, but the game spawns 8344 magic 197/197 (W:186-204, registered C9). Our server runs DB, so it will spawn melee. Read the **npc id**, never the table, for style.

| wave | E | S | W |
|---|---|---|---|
| 1 | mel | mag | *rng |
| 2 | rng | *mel | mag |
| 3 | *mag | rng | mel |
| 4 | mel | MAG | rng |
| 5 | mag | mel | RNG |
| 6 | MEL | rng | mag |
| 7 | mel | mag+*RNG | - |
| 8 | rng | mel | *MAG |
| 9 | mag | - | *RNG+mel |
| 10 | *rng+RNG | rng+rng | rng+*rng |
| 11 | *mag+mag | mag+mag | *MAG |
| 12 | *mel+mel | MEL | mel+*mel |
| 13 | *MEL | rng+mel | rng+*mag |
| 14 | *RNG | mag+rng | mag+*mel |
| 15 | *mag+rng | MAG | mel+*rng |
| 16-31 | see W:38-53 (flickers a→b→c, bigs in caps) | | |

### 3.2 A nylo's life (N:755-827). Age 0 = its spawn tick S.
1. **S**: it is added as `incoming` style a (aggros too). It does not move on S (N:782-788).
2. **Lane walk**: it walks to its mouth one tile a tick on a straight line (N:871-890). It does nothing else in the lane: no chew, no swing.
3. **Flicker** (waves ≥16, packed chain a/b/c): `npc_changetype` to b on S+5, held 2 ticks, then c on S+7. This happens in the npc phase (N:919-960, C:1050-1052) and keeps damage taken (N:962-971). If b == a, the hold just starts.
4. **Aggro swap**: on its first turn with its SW tile inside the box, an aggro (table pillar = none) changes to the `fighting` type of its current style and does not step that turn.
   - Measured: +10 W/E, +11 S, +9 E-big (N:790-812).
5. **Pillar walker** (`incoming`): walks to its latched pillar's centre. When npc_range <= 2 it stops, faces, and every 3 ticks bites the support for 0/1/2 hp (weights 4:3:3, mean 0.9) (N:973-1042, C:1053, C:1254-1257).
   - Frozen nylos keep biting in Normal (N:988-997).
   - The bite seq is the style's attack seq. The support takes no hitsplat (hitsplat=no).
6. **Aggro fighter** (`fighting`), N:1062-1110:
   - Each tick it hunts players within 24 of its coord (C:1465) and picks the nearest by npc_range. A tie goes to the FIRST found (strict `<`, N:1074-1081).
   - Read: the end of T-1.
   - Reach: melee 1, ranged/magic 8 (C:1466). npc_range is footprint Chebyshev, so **diagonals count for melee**.
   - Out of reach: npc_walk to the target's tile. In reach: stop, then swing when `map_clock >= swing`, then `swing += 3`.
   - The swing clock is armed at S+3 (N:281) and only advances on a swing. So the first swing comes on the first tick in reach (any tick ≥ S+3), then every 3 ticks while in reach.
7. **Swing** (N:1112-1158):
   - Ranged/magic launch a projectile; melee has no projectile.
   - The hit is ROLLED at the swing: npc attack roll vs player defence roll (N:1178-1199). On a success it deals 0..max: small 17, big 24 (C:1428-1429).
   - **Prayer is read at the swing tick T** (npc phase): with the matching overhead the hit is 0 (D:192-215).
   - Lands after the projectile flight (`~tob_flight_ticks`); melee lands at T.
8. **Natural detonation**: at age 51 (small) or 52 (big), i.e. S+51 / S+52, wherever it stands (C:1045-1048, N:772-780).
   - The detonate seq plays on that tick.
   - It hunts players with npc_range <= **2 of its footprint**, reading the end of S+50 / S+51. Each takes **1..18 (small) / 1..21 (big)**, unprayable, queue delay 0 (N:1272-1322, C:1056-1057).
   - Despawn comes 1 tick (small) or 3 ticks (big) later, i.e. S+52 / S+55.
   - A big leaves its 2 splits on its despawn (N:1310-1322).
9. **Killed**:
   - A small despawns about t+2 and leaves a `_standard` death graphic (N:1525-1528, C note M8).
   - A big killed at tick t despawns at **t+6 standing / t+7 walking**, and its splits appear then (N:1530-1555, C:1049).
   - Corpses count toward the cap until despawn.

### 3.3 Splits (N:1545-1625)
A big leaves 2 smalls at its SW tile+(0,0) and SW+(1,1) (C:1278-1285).
- Each has a uniformly random style (`random(3)+1`) and a uniformly random pillar (`random(4)+1`), latched.
- They are never aggro and never flicker (styles packed 0).
- Both born on the despawn tick, so they detonate at birth+51.
- Blert trio: the split pillar is effectively random (parent's 28.6%, nearest 29.1%; nylo_split_pillars.csv, scale 3).

### 3.4 Wrong style and nulling (D:288-330)
The style check runs in the player's swing (`player_hit_npc_prepare`, called at the attack for melee, ranged and magic: player_ranged.rs2:141, player_magic.rs2:517). It compares the player's damage type (`%varp6295_damagetype`) against the nylo's **current type at that tick**. The npc phase of the same tick has already applied any flicker (N:913-917).
- Right style: damage lands.
- **Wrong style (wave nylo)**: 0 damage, plus `tob_nylocas_shielded` and "no effect" message. The player is **NULLED on that nylo for life**: even the right style never damages it again (D:293-310).
  - Normal mode has no reflect (reflect is Hard-mode or Vasilias only, D:318-324).
  - Nulling is per (party slot, nylo), stored on the nylo (C:1192, D:332-353).
- A spell always reads magic, whatever the weapon (player_magic.rs2:500-519). Powered staves (shadow, sang, ayak) read magic after the seam33 fix (raid_loop/CONTENT_BUGS.md:216).
- An AoE spell (Ice Burst/Barrage) or a chinchompa runs the check **per target**. Hitting a non-matching nylo in the splash nulls you on it.
- **Auto-retaliate must be off**: a retaliation swing with the wrong weapon nulls.

### 3.5 Supports (N:1701-1910)
- 230 hp in a trio, from `130 + 50*(5-3)` (C:1233-1238, N:1722-1745).
- **Collapse** at 0 hp (in its `[ai_queue3]`). Every non-jailed raider within 40, i.e. the **whole room, with no geometry and no prayer**, takes **1..50** (N:1756-1813, C:1258). The collapse also turns every chewer of that support into a `fighting` aggro (N:1873-1890).
- The loc goes pristine → collapsing (4 ticks of `nylocas_pillar_collapse`) → dead.
- **4th collapse**: every raider takes damage equal to its current hitpoints, i.e. a wipe (N:1892-1903).
- There is no other failure; time is not a failure (N:1694-1699).

### 3.6 Vasilias (B:27-330, C:1291-1400)
- **Spawn**. Let C = the room tick the last nylo DESPAWNED; `~tob_nylo_check_cleared` prints "The chamber shakes." She lands on the first room tick L ≥ C+16 with L ≡ 0 mod 4 (N:325-351, C:1312). Blert trio: L - C = 17 [16-19] (R:559).
- Landing form `nylocas_boss_spawning` at (30,23), drop-in seq, hp pool 1875 (C:1320, B:27-43).
- **Melee at M = L+2** (B:102-113, C:1383).
- **Colour clock**:
  - First switch at M+9, then every 10: M+19, M+29, ... (C:1291-1298, B:115-168).
  - Each switch goes to one of the OTHER two styles, uniformly random (B:462-484). So WHEN is known and WHICH is a coin.
  - The switch is `npc_changetype` in the npc phase of tick T, keeping hp (B:147-152).
- **Turn cancels**: on the turn tick every raider within 24 of her gets `p_stopaction` (B:204-240). Your attack order is dropped, so you must re-issue the attack.
- **Attacks**: `attackrate` 4.
  - Opening melee form: first attack at M+1 (78%) or M+4 (22%) (C:1386-1398).
  - After a turn T: first attack at T+2, or T+3 (24% into melee, 48% into ranged/magic) (C:1384-1400, B:170-183). The second comes 4 ticks after the first.
  - So **exactly 2 attacks per form**. Blert offsets: (2,6) 71, (3,7) 37, (1,5) 7, (4,8) 7 (blert_analysis_output.txt:15-18).
- **Targeting**: nearest raider by npc_range from her footprint. Ties are a **uniform random** pick (B:213-239). Read: the end of T-1.
- **Reach** is 8 in **every** style, melee included (C:1466, B:253-266). If nobody is within 8 she walks at the target; otherwise she stands.
- **Damage** (B:420-456), rolled at the swing, prayer read at the swing tick:
  - Off-prayer: hit roll, then 0..70.
  - Prayed melee: 0.
  - Prayed ranged/magic: hit roll, then 0..17 (C:1316-1319).
  - Ranged/magic land after the projectile flight; melee lands at T.
- **Wrong style on her**: no damage. The hit (capped at her current hp) is **reflected onto the attacker AND heals her**. She is never nulled (D:311-330). Blert trio heal from wrong style: 90 [0-284] (R:546).
- **Hard-mode** splash/bounce does not apply in Normal (B:303-307).

## 4. PERCEPTION (per hazard)

Verbs and fields are from src/torirsserver/torirs_server_scriptrun.c (SR) and the live client's src/plugin/torirs_plugin_drive_ui.c (CL).

**Npc rows** (`npcs(radius)`, SR:927, built in SR:785-925; live CL:2114) carry these fields:
- `slot`, `npc_id`, `base_npc_id`, `server_x/z`, `size`, `element_id`;
- `health_ratio/scale`, `hit_damage/hit_cycle` (the newest splat only);
- `anim_id`, `seq_id`/`seq_tick`, `spotanim_id`;
- `facing` (an npc slot, or pid+32768), `face_x/z`.

Rules for reading them:
- **Use `server_x/z`.** On the live lane `x/z` is the drawn tile.
- **Key every nylo on `slot`.** `slot` is the server's npc index, the same on every seat. `element_id` is per-client and not comparable across lanes.
- `npc_changetype` is surfaced: `npc_id` changes in place, with the same slot (scriptrun_core.c:740-764). The live lane matches.
- **View limit.** NPC_INFO starts at 15 tiles and grows by 1 a tick to 24 while fewer than 64 npcs are tracked (torirs_server.h:375-409, encode.c:6010-6038). The live client returns the nearest 64 rows. The room never holds more than 24 nylos, 4 supports and the boss, so the view should sit at 24.
  - The lane spawns are 14-15 tiles from the centre but up to 20 from the far side of the box. **A spawn can be invisible on its spawn tick to a raider standing on the opposite side if the view has not grown.** See the mitigation in 7.

| hazard | what the bot sees | when (end of tick) | verb / key |
|---|---|---|---|
| room start | 4 rows `tob_nylocas_support` appear; message "The fight begins: The Nylocas" | room tick 0 | `npcs`, `messages` |
| wave spawn | new slot whose tile is a lane spawn tile (sec 1), `npc_id` in 8342-8347 | S (if in view) | `npcs` |
| nylo style | **`npc_id`**: incoming 8342 mel / 8343 rng / 8344 mag; big 8345-8347; fighting 8348-8350; big fighting 8351-8353. Map with `symbol("npc", name)`, never by id. | every tick; a flicker shows b at S+5, c at S+7 | `npcs` |
| aggro | type becomes `tob_nylocas_*fighting*` at the mouth (S+9..11). It is predictable earlier from the wave table, keyed on (wave, lane, slot), but the table carries the C9 error, so predict and confirm. | swap tick | `npcs` |
| target pillar | not sent. The table gives it (W:92-122). For splits it is random; read it from `facing`/`face_x,z` once the split arrives, or from where it walks. | arrival | `npcs.facing` |
| bite | the chewer's `seq_id` is the style's attack seq on the bite tick; the support's `health_ratio` drops. **No hitsplat**: `hitsplat=no`, so `hit_damage` stays -1. | bite tick | `npcs` |
| support hp | each support row's `health_ratio/health_scale` (bar fill, not hp), plus `varbit(varb6448_tob_client_waveprogress_val)` = combined permille | each change | `npcs`, `varbit` |
| aggro swing | the fighter's `seq_id` = attack seq on T; ranged/magic also show a projectile (`spotanim_id` = the projectiles in sec 2) with `target` = my pid; my hp drops when it lands | T (too late to pray) | `npcs`, `projectiles`, `skill("hitpoints")` |
| detonation | the `*_death_detonate` seq on S+51 (small) or S+52 (big). **Too late to dodge** (it reads the end of the previous tick), so predict from the birth tick. | S+51/52 | `npcs` |
| despawn / splits | the slot vanishes; for a big, 2 new small slots appear at SW+(0,0)/(1,1) on the same tick. A small leaves `tob_nylocas_death_*_{standard,detonate}` on its tile. | despawn | `npcs`, `spotanims` |
| my wrong-style hit | map graphic `tob_nylocas_shielded` on the nylo's SW tile, and the message "Your attack has no effect on this Nylocas." That nylo is now **dead to me**: blacklist the (slot, birth) pair. | swing tick | `spotanims`, `messages` |
| cap stall | the message "The chamber is full - the next wave is held back."; the wave is late by 4n | due tick | `messages` |
| collapse | the support row disappears; the loc becomes `tob_nylocas_support_collapsing`, then `_dead`; message "A support collapses!" | collapse | `npcs`, `locs`, `messages` |
| cleanup end | the last nylo slot vanishes; message "The chamber shakes." | C | `npcs`, `messages` |
| boss land | `nylocas_boss_spawning` at local (30,23) on L, then `nylocas_boss_melee` on L+2 and message "...drops into the chamber." | L, L+2 | `npcs`, `messages` |
| boss colour | `npc_id` among 8355/8356/8357 (by name). The switch tick is **predictable** (M+9+10k); only the colour is a coin. | T | `npcs` |
| boss swing | `seq_id` = `top_spider_*_attack` on T, a projectile (size2 / mage_biggest) for ranged/magic, `facing` = the target's pid+32768 | T | `npcs`, `projectiles` |
| boss hp | `varb6448` permille of 1875, and the row's `health_ratio` | each hit | `varbit`, `npcs` |
| room clear | the boss slot vanishes; hp, prayer and spec restored; "...complete!" message | death | `npcs`, `messages`, `skill` |

**The bot cannot perceive:**
- a wave nylo's pillar before it arrives (it is in the table only);
- a split's pillar before it walks;
- who is nulled on which nylo, except its own message;
- the boss's next colour;
- a support's hitpoints in units: only the bar fill (the varbit permille × 920 / 1000 gives the combined hp to about 1 hp);
- the cap count. Count live slots of the 12 nylo types yourself, corpses included until the slot vanishes.

## 5. WHAT REAL TRIOS DO (Blert, 27 death-free Normal trio rooms, R:530-1680)

- **Room**, median [min-max] in room ticks:
  - wave 1 at 4 [4-8];
  - wave 31 at 260 [244-293] (natural 236, so about 6 stall cycles: on time to w13, w14 +4, w20-28 +8, w29 +16, w30-31 +24);
  - cleanup end 292 [277-341];
  - boss spawn 308 [296-357];
  - boss phase 95 [75-123], at 20.3 hp/tick;
  - **room 412 [372-472]**.
- **Leaks**: 0. **Deaths**: 0 (R:547, 551). No support collapse seen in 145 recorded rooms (NYLO_PILLARS.md).
- **Damage taken by the recording player**: mage 67 [24-120], melee 85 [19-122], range 58 [19-140] (R:548-550). Boss hits on a player median 0-4.
- **Roles by STYLE, not by lane** (wiki Strategies:711 "Trio: x1 mager, x1 melee, x1 ranger"). Share of each role's attacks in its own style:
  - mage 76% magic;
  - ranger 79% ranged;
  - melee 63% melee, plus 23% magic (a Sanguinesti on magic bigs).
- **Lanes by role** (share-weighted hits):
  - mage: E 17.5 > splits 14 > W 12 > S 10;
  - ranger: W 19.8 > splits 16.6 > S 14.9 > E 8.8;
  - melee: splits 17.5 > S 12.8 > E 9.9 > W 9.3.
  - Wave 1: mage → S mag 100%, ranger → W rng 100%, melee → E mel 93%.
- **Standing** (relative to boss tile (30,23)):
  - ranger at about (-4,+1)/(-3,+2): the WEST half, local about (26-27, 24-25);
  - mage at about (+7,+1)/(+7,+2): the EAST half, local about (37, 24-25), dropping south for south magic bigs;
  - melee roams south and east.
- **Weapons**:
  - mage: Eye of Ayak, plus barrage on the w11 E/S and w21 W doubles;
  - ranger: blowpipe / twisted bow, plus black chins on w10, w21 and w30/31 S;
  - melee: sulphur blades / scythe, plus a sang on magic bigs.
- **Team plan** (trio mdx:21-24): no stall before wave 28; let the old ones self-destruct at the 28/29 check.
- **Boss**: all three swap weapon and prayer with every colour. Each player lands 19-20 attacks. The first attack comes 3 ticks after spawn. The first weapon swap after a colour change comes 5-6 ticks later (R:1204-1212). The modal overhead is Protect from Melee. Second form: ranged 12/18, magic 6/18.

## 6. ROOM END and its races

- **End condition**: Vasilias' hp reaches 0. The watchdog (RA:1520-1540, `~tob_boss_alive` RA:1779-1800) sees her gone and calls `~tob_room_cleared` (RA:837-860).
  - This restores hp, prayer, run energy and spec, but not drained stats.
  - It prints "Wave 'The Nylocas' (Normal Mode) complete! Duration: ..." (RA:931-949).
  - The room ends when the party **walks out**; the barrier becomes a gate (P:612-614). The exit is local (39,51) (C:3069-3070).
- **Races**:
  1. **Turn vs swing on the same tick T.** Her retype and `p_stopaction` run in the npc phase of T, before any raider's swing on T. So a raider never swings on T. A press seen-after-T acts at T+1 against the NEW type. A wrong-style hit happens only if the bot attacks with the old weapon after seeing the turn, or if a gear press lands a tick after the attack press. **Emit wield before attack, in one tick.**
  2. **Flicker vs swing.** The style check reads the nylo's type at the swing tick (after that tick's npc phase). Swinging at a flicker on S+5 or S+6 (style b) or S+7 (c) with a's weapon nulls you.
  3. **Corpse cap.** A killed big holds a cap slot 6-7 ticks, and its splits arrive at despawn. A kill on the tick before a wave check does not free the slot.
  4. **Cleanup to boss.** C is the last DESPAWN, not the last death. A split born late (a big killed late) pushes C by up to 7.
  5. **Collapse damage** is room-wide and unprayable. With 1..50 per raider, a raider under 51 hp can die to any collapse. Eat above 50 whenever a support's health is low.
  6. **4th collapse = wipe.**

## 7. PROPOSED SOLVER SHAPE

Architecture as in P3: MEASURE, then CLOCK, then CONTEXT, then PLAN, then EMIT. Wake on `server_tick` and take the tick from `api_drive.tick()`, never from a wake count (solver_lessons.md rules 3, 26, 31).

### 7.1 MEASURE (facts with ticks)
- **origin** = (x − x%64, z − z%64) of my tile.
- **room tick** `r = tick − t0`, where t0 = the first tick the 4 supports were seen.
- **nylos**: slot → {type, style, big, aggro (fighting category), birth, chain}.
  - `birth` = the tick the slot was first seen on a lane spawn tile.
  - If first seen elsewhere: `first_seen − steps from its lane's spawn tile − 1`, since it never moves on S and then makes 1 step a tick.
  - A split's birth = the tick its parent's slot vanished.
  - `chain` comes from the table row matched by (wave, lane, tile, big). Use it only to *predict* the b and c switch ticks S+5 and S+7. Trust the live `npc_id`.
- **wave index**: count the wave-spawn events: the ticks on which ≥1 new slot appears on a lane spawn tile, with r ≡ 0 mod 4.
- **live count** for the cap.
- **support hp** per corner: `health_ratio`, plus the varbit sum.
- **nulled set** (mine): from the shielded graphic or message on the slot I last attacked.
- **boss**: L (first spawning row), M = L+2, switch ticks M+9+10k, current colour, and her attacks seen. A predicted attack window is {T+2, T+3, T+6, T+7} after a turn T, or {M+1, M+4, M+5, M+8} in the opening form.

### 7.2 Contexts
1. **PRESTART** (before the supports appear):
   - Kit check; auto-retaliate off; offensive prayer on.
   - Stand: leader at the barrier, members waiting at `party.barrier("started")`.
   - Then every seat crosses `tob_arena_barrier` (op 1) and walks to its station.
2. **WAVES** (r < cleanup end): the target-selection rule below.
   - Plan goal = the "attack tile" for my target: in reach of it, inside the box.
   - Constraints:
     - **forbid** the 4 blocked support 2x2s (they are already collision, so this is only belt-and-braces).
     - **zones** with `{lo=0, hi=2, require=false}` around every nylo whose detonation tick `birth+51` (small) or `birth+52` (big) falls in the horizon, `t0=t1=det−1` (the read tick), `size` = 1 or 2, `tier="damage"`, `cost=18` or `21`.
     - **zone** for melee aggros in my own view that target me: `{lo=0, hi=1, tier="damage", cost=9}` on swing-due ticks when they are not my style and nobody is killing them. This is cheaper to pray than to dodge, so keep it soft.
     - **edge**: the box x 26..37, z 19..30, margin 1, weight 0.3, to stay off the lane mouths.
     - **pull** to my role's station: ranger (27,25), mage (36,25), melee (31,21). These come from Blert's modal positions (R, 5 above); weight 0.2.
3. **CLEANUP** (wave 31 out, nylos remain): same as WAVES. Any style may be shared once nothing of mine remains: a raider with no own-style target helps by switching weapon. That is optional; Blert trios do it with a sang only.
4. **BOSS_DUE** (cleanup end C to L+2):
   - Eat or drink to full.
   - Pray Protect from Melee by L+1 (her opening is melee and her first attack is at M+1).
   - Wield the melee weapon if mine; walk to a tile 1-3 from her footprint (30..33, 23..26) on my role's side.
5. **BOSS**:
   - **Goal**: within my weapon's reach of her footprint (melee: gap 1; others: gap ≤ 7). Stay inside 8 of her so she never walks.
   - **zones**: none from her. She has no area attack in Normal and her reach is 8 regardless, so geometry cannot avoid her. Avoidance is prayer.
   - On each switch tick T (predicted, confirmed by `npc_id` at the end of T), emit in one tick, in this order: the protect prayer of her new colour, then the weapon of her new colour, then the attack on her slot.
   - Never press an attack with a weapon whose style ≠ her current `npc_id` style: that is a reflect and a heal (D:311-330).
   - On a predicted switch tick T, do not press an attack after seeing T-1 unless the swing lands on T-1's colour. Her `p_stopaction` on T cancels anyway, so this is only to avoid wasted swaps.

### 7.3 Target selection (deterministic; every raider computes all three assignments from the same view)
- Candidates: living nylo slots in the box or at a mouth whose **current** style == the role's style. Exclude any slot in *my* nulled set.
- For a flicker with age < 7 (style not settled), the style that counts is the style it will have on my earliest swing tick (d+1 at the soonest): b for ages 4-6, c from age 6. Never start a swing at a flicker whose predicted style on the swing tick differs from mine.
- Priority, lexicographic (ties by lower slot):
  1. aggro (`fighting`) within 8 of any raider;
  2. a chewer at a support whose `health_ratio` is lowest, if that support is < 40% (wiki "if you see a pillar getting low...");
  3. a split, or any nylo with `birth+51 − r ≤ 6` near a raider (blow-up risk);
  4. the newest wave spawn (smallest age), smalls before bigs, unless the big is aggro;
  5. otherwise the oldest.
- **Keep** the current target until it dies or becomes ineligible: no retargeting churn.
- Two raiders never share a role style, so no tie-break between raiders is needed.
- **Cross-help**: when a raider has had no own-style candidate for ≥ 3 ticks, it may take the oldest candidate of another style, at most one helper per slot. The helper is the lowest pid among idle raiders. It must carry and wield that style's weapon, and a magic help must be single-target. **Do not chin or barrage** in v1: the splash nulls you on off-style nylos (sec 3.4).

### 7.4 Non-movement channels
- **Prayer.** Waves: the offensive prayer of my style always; protect from the style of the most aggros within reach of me (melee counts only within 1), else Protect from Melee. Boss: the protect prayer of her colour from the switch tick on.
- **Gear.** The waves need one weapon per role. The boss needs all three styles per raider, and the swap is wield-then-attack in one tick (p3:907-913 pattern: a held op cancels the attack, so re-press it).
- **Eat** when hp < 51 and a support is below 25%, or when hp < 50 otherwise. 51 is the collapse max of 50. Drink a restore when prayer < 20.
- **Retaliate** stays off.

### 7.5 Kit (every item exists in configs/all.obj)

| seat | role | waves | boss green / blue / grey |
|---|---|---|---|
| p1 (leader) | ranger | `toxic_blowpipe` (2-tick) or `twisted_bow` + `dragon_arrow` | bow / `sanguinesti_staff` / `abyssal_whip` |
| p2 | melee | `blade_of_saeldor` or `ghrazi_rapier` (4-tick, single target; the scythe overkills 1x1s and its 3x3 sweep can null) | `magic_shortbow`+`rune_arrow` / `sanguinesti_staff` / `scythe_of_vitur` |
| p3 | mage | `tumekens_shadow` or `eye_of_ayak` or `sanguinesti_staff` (a powered staff reads magic, CONTENT_BUGS.md:216) | `magic_shortbow` / shadow / `abyssal_whip` |

Plus 99s via `::max*`, `4dosepotionofsaradomin` and `4dose2restore`, and `anglerfish` to fill.
- The harness gives `water_rune` and others, but the configs name them `waterrune`/`chaosrune`/`deathrune`/`bloodrune`. Verify, or drop Ancients entirely, since a powered staff covers magic.

### 7.6 Pass criteria (from the tick log, per seed; sweep ≥ 8 seeds)
- **Supports lost = 0**: no support `npc_del`/death row, no "A support collapses!".
- **Wrong-style events = 0**: no `tob_nylocas_shielded` graphic from a bot's swing, no "no effect" message, no reflect `hit_player` from Vasilias, no `npc_heal` on her.
- **Damage**:
  - per raider ≤ 120 in the room (Blert median 58-85, max 140);
  - natural detonations hitting a raider = 0;
  - aggro hits through the right prayer = 0;
  - Vasilias off-prayer hits = 0.
- **Room ticks**:
  - wave 31 spawned by r ≤ 293 (Blert max);
  - cleanup end ≤ 341;
  - room ≤ 472 (Blert max); target the median 412.
- **Deaths 0**; hp restored at the clear; the party exits.

## 8. OPEN QUESTIONS & RISKS

1. **Prayer press latency.** Is a protect prayer pressed after seeing d live for an npc swing on d+1 or only on d+2? This decides the boss's first-attack-in-form budget (T+2). Measure it with lag.py before relying on it.
2. **Style check tick.** Ranged and magic `player_hit_npc_prepare` runs at the swing (player_ranged.rs2:141, player_magic.rs2:517); confirm melee does the same. If any path checks at LANDING, then a flicker or a boss turn between the swing and the landing nulls or reflects.
3. **Wave 30 south slot 2** (C9): the game spawns magic, but our DB spawns `mel→rng→mel`. The solver must read `npc_id`. Fix DB or the generator separately.
4. **Hit-roll reading of `npc_range`.** Melee nylos and Vasilias' melee hit **diagonally** (Chebyshev `npc_range` ≤ 1). Vasilias' melee reaches **8**. Both are content facts here and may differ from OSRS.
5. **Nulling scope.** The content nulls a player permanently on a wave nylo after one wrong-style hit, AoE splash included. Is that OSRS? Content wins for the solver, but it forbids chins and barrage in v1.
6. **View radius.** Confirm the NPC_INFO view really sits at 24 inside the room. If a seat on the far side misses a spawn on S, the birth must come from the lane-distance estimate; this needs checking against the tick log.
7. **Aggro tie-break.** A wave aggro chooses the FIRST raider found among equally near ones (N:1074-1081), i.e. hunt order, probably pid order. Vasilias picks uniformly at random. Neither is perceivable before the swing; `facing` shows the choice on the swing tick.
8. **Cap counting.** Corpses count until despawn (small about t+2, big t+6/7). A bot's local count must match, or its predicted wave ticks drift. Verify against "The chamber is full" messages.
9. **Split pillar.** A split's pillar is random (N:1570-1576) and invisible until it walks. Treat splits by proximity to the weakest support.
10. **Supports are invisible before the start.** Compute the origin from my tile (64-aligned), not from the support npcs, in PRESTART.
11. **Runes naming** (`water_rune` vs `waterrune`) and whether `::maxmage` sets 30 Attack for `lava_battlestaff` (the harness note at tob_nylocas_normal.lua:113). Prefer the kit in 7.5, which needs no runes.
12. **Boss opening timing.** Blert says the first attack is about 3 ticks after the spawn *event*. In our content that event is the spawning form at L; she turns melee at L+2 and swings at M+1 = L+3, which agrees.
13. **Risk: the solver getting slow.** The Verzik P3 search is about 3-4 M expansions a run. Here 24 nylos means up to 24 zones (the cap is 48). Keep only the detonations within the horizon and the aggros within 3 tiles.
