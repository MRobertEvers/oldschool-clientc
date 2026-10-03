# Content bugs the raid loop found and has not fixed

Found is not fixed: a defect with no row fails nothing. One line per finding: raid, room,
the constant or script line, what the source says, what we measured, who found it. A row
is removed only by the commit that fixes it, named here in its place.

## From seam1 (2026-10-02)

- ToA, Zebak (and Kephri, Akkha, Ba-Ba, the Wardens): the barrier `~toa_add_barrier`
  spawns for a room whose square has none is placed at entry + 2 x
  `~coord_direction2(entry, fight)`, and that direction can be diagonal (Zebak: NW, gate at
  6452,161 off the entry row); `~toa_pass_barrier` then steps two tiles from the PLAYER in
  the gate-minus-player direction, so the click starts the room (`::toastate` started=1,
  boss uid set) but the player ends on 6452,162 or 6454,161 and `walk_to` 6437,160 stalls
  at the barrier: the arena cannot be reached (runs rre_toacox1, rre_toacox3, rre_toacox4).
  Het's map-placed barrier crosses correctly (rre_het1: 6442,159, started=1). Source to
  settle it: Near Reality's `TOARaidArea.walkBarrier`. Files: `minigame_toa/scripts/toa_barrier.rs2`,
  `~toa_add_barrier`. Found by the raid_room_entry_verbs fixer.
- ToA, Wardens P1: `::toa 10` lands STARTED. The entry tile (31,56) is 10 tiles from the
  fight tile (32,46), inside `^toa_room_arena_radius` 12, so the proximity watchdog
  (`toa_raid.rs2` ~line 1129) starts the room on arrival and the `toa_wardens_barrier`
  click is moot. No source yet places the real entry tile; settle from NR, then move the
  tile or shrink the radius for that room. Found by the raid_room_entry_verbs fixer.
- CoX, every room: `::coxgoto` lands at the room CENTRE (the `::coxgoto_tekton`
  convention), so Tekton wakes on landing (`cox_tekton.rs2` wake-on-range) and hit the
  player 99 -> 65 before any click (rre_toacox3 shot 017). A room test that walks in from
  the door needs a door-tile landing derived from the serpentine path (`cox_layout.rs2`);
  no source places that tile yet. Found by the raid_room_entry_verbs fixer.
- ToB, Hard Mode door: `::tobmode 2` skips the door's `varp6826_tob_completions >= 1`
  requirement (`tob_party.rs2` `~tob_choose_mode`). Deliberate for a room-resume
  affordance; the whole-raid `tob_entry` test must use the real door
  (`tob_surface_raid_entrance` op 1 and the mode menu). Not a bug; a rule for the authors.
- Engine, ground items (client): OBJ_COUNT has no old count on the client path
  (`rs_gameproto_exec.c:538` drops `pkt->old_count`; `App_WorldObjStackSetCount` does not
  take it; the lookup matches id only where Client-TS matches id AND count == old_count),
  so a tile holding two piles of one stackable can retarget the wrong pile. Server side,
  `world_obj_add`'s merge loop merges a public add into ANY active pile of that id on the
  tile, a private one included (it never checks `receiver_pid`). OBJ_DEL carries pos+id
  only (`revpacket.h` PktObjDel, `torirs_server_encode.c:4317`) while rev 239's real
  OBJ_DEL carries the quantity, and `ToriRSServer_WorldGroundFind` takes the lowest slot,
  not the oldest. Invisible for non-stackable twins; wrong for two private piles of one
  stackable with different counts. Found by the client_ground_obj_merge fixer.
- Engine, retaliation: an ordinary npc hit by magic may never retaliate (a Lumbridge man:
  one swing then none, or none at all in the full harness; a cow: none). `DRIVER_NOTES.md`
  "An ordinary npc hit by magic may never retaliate". Found by the npc-state fixer.
- Quest loop impact (for the PR, not a raid row): `ToriRSServer_CombatAddXp` now follows
  LostCity's `Player.addXp` (a drained stat stays drained through xp), as RAID_ORCHESTRATOR.md
  section 4 asks. The quest test `deserttreasure` moved green -> RED because the Ice Path
  chill (`chill.rs2`, ported from LostCity) now really drains: trolls 376 -> 466 ticks and
  magic below Fire Blast's 59 at Kamil. An A/B binary without the hunk ran 249/249. That
  test belongs to the quest loop; it needs a restore or `::setlevel magic` before Kamil
  when this branch reaches v3.

## From the Theatre of Blood spec pass matthew-mbp-m4-raid-b1-spec-tob (2026-10-02)

One line each: raid, room, constant or script line, source (grade), our measurement, the report.
Grades are the spec table's; a D-source row is listed so it is not lost, not because it is settled.
Paths: `minigame_tob/` is `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_tob/`;
tables are `docs/minigames/theater_of_blood/encounters/<room>.tsv`.

- ToB, every Entry boss (Maiden, Bloat, Sotetseg, Xarpus, Verzik, Vasilias): `~tob_boss_hp` / `~tob_scale_hp` (tob_raid.rs2:518-522), `~tob_vasilias_scale_hp`, `~tob_verzik_scale_pick` multiply the cache unit by the party size | Jagex, Update:Theatre of Blood: Entry Mode Improvements (1 Mar 2023) lines 59-67 (A): "2 Players: +90%, 3 Players: +80%, 4 Players: +70%, 5 Players: +60% ... five players will have a hitpoint increase of 300% rather than 400%" = x1/1.9/2.7/3.4/4.0 | ours solo correct (Maiden 500, Bloat 320 measured), 2-5 players = x2..x5 by code read | maiden, bloat, nylocas, verzik reports.
- ToB, Maiden: `~tob_maiden_crab_hp` Entry = 16 x n | same Jagex post (A): 16/30/43/54/64 | ours 16 x party size (code read) | maiden.spec.json.
- ToB, Maiden: `npc_changetype` to tob_maiden_70/50/30 resets hitpoints to the record's 3500 base minus damage | blert 39 of 39 transmogs never raise her hitpoints (B, maiden_spec_pass_2026-10-02.txt) | ours solo Normal 1444 -> 2319 (+875), solo Entry 275 -> 3275 (+3000) at the retype (::tobcrabdrop + ::tobwhy) | maiden.spec.json (row maiden.transmog_hp_jump).
- ToB, Maiden Hard: `~tob_maiden_period_at` = max(5, 10 - min(c,5)) with `^tob_maiden_hard_speedup_leaks`=5, `^tob_maiden_hard_attack_ticks_min`=5 [M20] | blert 13 Hard rooms (B): period 10 - ceil(c/2) (9 at c=1,2 40/41; 8 at c=3,4 29/29; 7 at c=6 14/15; 6 at c=7,8 14/14) | ours solo Hard gaps 10, 10, 6 (c=4), then 5 from c=6 | maiden.spec.json.
- ToB, Maiden Hard: `^tob_maiden_crabs_max`=10 used at every party size by `~tob_maiden_crab_count` | blert 6 of 6 solo Hard spawns = 7 (B); Jagex New Modes (9 Jun 2021): "Removed the South 1 Nylocas spawn in raids with only 1 player" | ours solo Hard spawned 10 at the 70% drop | maiden.spec.json.
- ToB, Maiden: blood pool / trail damage cadence, `~tob_watch_room` re-arms with queue(...,1) | wiki Maiden "healing her every tick if not dodged"; AdvancedRaidTracker assessBloodForNextTick and TobMistakeTracker detectMistakes evaluate every tick (C) | ours hit_player on ticks 29,31,33..43: every SECOND tick, 5-6 hits per 11-tick pool instead of 11 | maiden.spec.json (row maiden.pool_hit_cadence).
- ToB, Maiden: blood spawn movement | blert 1043 of 3096 consecutive tick pairs moved (33.7%), never 2 tiles (B); Zenyte 1 in 3 | ours 44,137 moves in 57,029 slug-ticks = 77%, one tile every tick toward a waypoint | maiden.spec.json (row maiden.blood_spawn_step).
- ToB, Maiden: `~tob_maiden_pool_roll` (tob_maiden.rs2:1008) rolls 10% / 20% with no halving | Jagex, Eating in the Bank (9 Jan 2020) line 41 (A): "all raid team members successfully avoiding the blood splat attack now halves the chance of a blood spawn appearing" | ours: no halving (code read, not run) | maiden.spec.json.
- ToB, Maiden Entry: blood spawn hitpoints, `~tob_maiden_by_scale` 90/105/120 ignores the mode | cache maiden_blood_slug_story stat4=10 (A) | ours hp=90 in a solo Entry room (::tobwhy) | maiden.spec.json.
- ToB, Maiden: `^tob_maiden_death_a_ticks`=2 + death_b 4 = 6 | cache maiden_death_a 90 cycles (3 ticks) + maiden_death_b 120 (4) = 7; AdvancedRaidTracker MAIDEN_DEATH_ANIMATION_LENGTH=7 (A) | ours 6 (read from the constants, not run) | maiden.spec.json.
- ToB, Bloat: `^tob_bloat_turn_chance`=64 / `_hard`=32 [M17c] | blert 90 Regular / 45 Hard rooms (B): reversal on 6.7% (129 of 1927 eligible walking ticks) Regular, 17.1% (211 of 1234) Hard | ours 1.6% (533/33,663, 532 rooms), 3.3% (658/20,091, 278 rooms); tob_selftest.rs2:1118 (tob_st_bloat_walk) asserts the old guess and must change with it | bloat.spec.json.
- ToB, Bloat: `^tob_bloat_up_offset`=33 flips the phase on down+33 but the first step, fly and hand clocks run on down+34 | blert first step after the down = 33 in 111/111 Regular, 81/81 Hard (B) | ours 34 in 1065/1065 Normal, 555 Hard, 282 Entry | bloat.spec.json.
- ToB, Bloat: `^tob_bloat_down_lockout_after_turn`=5 | wiki "cannot go down for another 5 ticks"; blert min (down - reversal step) = 5 both modes (B) | ours min 4 (45 downs sit on it) | bloat.spec.json.
- ToB, Bloat: `~tob_bloat_get_up` sets the hand clock to rise + period | blert first drop ON the rise tick in 87 of 111 rises (B) | ours 6 ticks later (21 times) or 4 (13 times) | bloat.spec.json.
- ToB, Bloat Hard: hand clock restarts at each rise | blert drops every 4 or 6 continuously through downs (124 gaps spanning a down, all 4 or 6) (B) | ours 81 of 1307 hard gaps are 7-11 | bloat.spec.json.
- ToB, Bloat: `~tob_bloat_random_tile_packed` draws uniformly over the 16x16 area | blert 5,802,952 hands hit 220 tiles = 256 minus the 36 tank tiles (B) | ours 13.8% of 22,512 shadows on the tank | bloat.spec.json.
- ToB, Bloat Hard: `^tob_bloat_hp_hard_3/4/5` (1800/2100/2400) referenced by no script; `~tob_spawn_boss` scales the Normal table | cache stat4 2400, Jagex 75/87.5/100% (A) | ours solo Hard run begins after 587 and 643 damage = about 1500, not 1800 | bloat.spec.json.
- ToB, Bloat (the Sotetseg and Xarpus reports find the same for their rooms): `~tob_spawn_boss` adds the Normal npc type in every mode (tob_bloat 8359) | cache tob_bloat_story 10812 (stat4 320, def 10/5/10), tob_bloat_hard 10813 (stat4 2400) (A); client plugins key the mode by npc id | ours type 8359 in all 177 Hard and Entry downs | bloat.spec.json.
- ToB, Bloat: run / hands / hurry gates compare a FLOORED hitpoint percent with <= 60 / <= 90 / <= 40 | blert highest run-step health 59.72%, highest first-drop 89.89%, hurry boundary (39.60, 40.23] (B) | ours 60.94%, 90.9%, <= 40.99% | bloat.spec.json.
- ToB, Bloat Entry: fly and stomp damage reuse Normal | wiki infobox Entry max hit 8 (flies), 40 (stomp) (D) | ours 10-20 and 40-80 (118 flies, 80 of 80 stomps) | bloat.spec.json.
- ToB, Nylocas: `^tob_nylo_lifetime_ticks`=52 | blert natural spawn->despawn 52 small (183/183), 55 big (15/15) (B); blert guide "Animation begins on lifetime tick 52, despawn on 53" / "53, 56" | ours detonates and frees at +51 for both (120/120) | nylocas.spec.json.
- ToB, Nylocas: a naturally exploding big leaves no splits (`tob_nylo_detonate` npc_del) | blert 15/15 exploded bigs spawn 2 smalls on the despawn tick (B); guide "smalls spawn on despawn" | ours 0 splits | nylocas.spec.json.
- ToB, Nylocas: killed big despawn | blert hp0 -> despawn +6 standing / +7 walking (759 events) (B) | ours npc_death -> npc_free +3 / +5 (11 kills): splits 3 and 2 ticks early | nylocas.spec.json.
- ToB, Nylocas: flicker first style change | blert +5 ticks after spawn (740/740) (B) | ours +6..+8 | nylocas.spec.json.
- ToB, Nylocas: aggro incoming -> fighting swap | blert +10 west/east, +11 south (+9 east bigs) (B) | ours +6..+8 | nylocas.spec.json.
- ToB, Nylocas: Vasilias attack timing, `tob_vasilias_turn` sets swing = map_clock | Jagex (Tournament World ... ToB Tweaks:92, A): "Nylocas Vasilias will no longer be able to instantly attack players after switching combat styles"; blert 125/125 windows exactly 2 attacks at +(1..4) and +(5..8) | ours 3 attacks at +0,+4,+8 (34/34 Normal, 34 Entry, 36 Hard); Hard prince same pattern in blert (21/26) | nylocas.spec.json.
- ToB, Nylocas: split tiles | blert 620 of 772 parents leave exactly offsets (0,0)+(1,1) (B) | ours draws each split uniformly from six offsets (11 kills) | nylocas.spec.json.
- ToB, Nylocas Hard: magic split damage uncapped | Jagex New Modes:30 (A): "Capped the maximum damage the Nylocas Boss Mage split damage deals to 75" | ours bounce passes the full hit (<= 105), no cap (script read) | nylocas.spec.json.
- ToB, Nylocas: pillar bite damage | derived bracket mean 0.83-0.93 (D) | ours uniform 0..2, mean 1.03 (n=977) | nylocas.spec.json.
- ToB, Nylocas Hard: Prinkipas self-destruct `tob_prince_tick` age >= 52 | blert left-alone third demi-bosses lived 54-60 ticks (4/4) (D) | ours 52 | nylocas.spec.json.
- ToB, Nylocas: Vasilias landing tile | blert 3294,4247 (18/18) (B; RuneLite anchor convention for a 4x4 unconfirmed) | ours 3293,4246; the prince matches | nylocas.spec.json.
- ToB, Sotetseg: `[proc,tob_sote_tick]` arms clock + `^tob_sote_attack_ticks` (5) after the death ball | blert next attack 10 ticks later (19/19 Normal outside mazes, 10/10 Hard, 2/2 Entry) (B) | ours 5 (death ball 58, next 63; 113 -> 118) | sotetseg.spec.json.
- ToB, Sotetseg: `^tob_sote_magic_per_ball`=10 but `~tob_sote_attack` fires the death ball ON the 10th magic tick | blert 10 ordinary balls precede the death ball (20/23 Normal, 8/10 Hard) (B); wiki "After launching 10 projectiles ... large red ball" | ours 9 ordinary balls then the death tick | sotetseg.spec.json.
- ToB, Sotetseg: death-ball flight = 30 + 36 + 8 x distance cycles | blert 16 ticks after the death-ball attack (5/5) (B); Jagex Summer Finals:72 (A): "now consistent in terms of timing, regardless of your distance from the boss" | ours +9 ticks at 22 tiles, ~3 at 3 tiles | sotetseg.spec.json.
- ToB, Sotetseg: `^tob_sote_melee_hitsplat_delay`=1 but queue*(tob_sote_melee_impact,1) then queue*(combat_damage_player,0) lands at +2 | blert +1 (8/8) (B); wiki "the hitsplat is applied one tick after the attack" | ours +2 (41/41) | sotetseg.spec.json.
- ToB, Sotetseg: maze teleport and first move | blert teleport at proc+3 (72/72 team, 6/6 runner), nobody moved before proc+5 (0 of 114) (B); ART stallDuration=5; cache portal seq 160 cycles | ours teleports on the proc tick and moves at once | sotetseg.spec.json.
- ToB, Sotetseg: maze 4-tick despawn cycle restarted at each proc | blert re-activation tick mod 4 equal for both mazes in 19/22 raids (B); Horselord "0-3 ticks depending on the room cycle" | ours checks at proc+4n | sotetseg.spec.json.
- ToB, Sotetseg: maze chips and rag ride `~tob_watch_room`'s 2-tick queue | blert chips every 7 (29/29), rag on consecutive ticks (B); wiki "every 7 ticks", "every tick" | ours chips every 8 (16/16), rag every 2nd tick | sotetseg.spec.json.
- ToB, Sotetseg: no guard keeps the death ball off the first attack after a maze | Jagex New Modes:101 (A): "Sotetseg's big bomb should no longer happen immediately after the maze mechanic ends"; blert 0 of 44 | ours: unguarded (code read; needs a second player to reproduce) | sotetseg.spec.json.
- ToB, Sotetseg Entry/Hard: `~tob_spawn_boss` places tob_sotetseg_combat (8388) in every mode | cache _story stat2 150, _hard stat1 350 (A) | ours Entry def=200, Hard attack 250 | sotetseg.spec.json.
- ToB, Sotetseg: solo maze false clear, the runner in the realm makes `~tob_boss_alive` false (npc_find filters on plane) | not a source disagreement: room declared cleared 3 ticks after arrival ("The way onward is open" tick 24 for proc 21), runner hook stops, no chip | ours measured; ::tobrun and the C stanza arm the maze before boss_seen and cannot see it | sotetseg.spec.json.
- ToB, Sotetseg: defence floor not enforced | wiki floor 100 (D); Jagex Equipment Rebalance confirms a floor exists | ours none | sotetseg.spec.json.
- ToB, Xarpus: `tob_xarpus_open_health` (tob_xarpus.rs2:86) is overwritten | wiki Strategies:829 "starting with 75% of his health" (D after the closer's downgrade); blert P2-start ratio 79.7-86.0% in 16/17 | ours 3750/3750 (100%) at fight tick 1 solo Normal | xarpus.spec.json.
- ToB, Xarpus Hard: `~tob_xarpus_hard_ring_open` lays the 98-tile ring at the P2 stand-up | blert 98 splats at fight tick 3 in 7/7 Hard raids (B); wiki "At the beginning of the fight" | ours fight tick 51 solo Hard | xarpus.spec.json.
- ToB, Xarpus Hard: `tob_xarpus_exhumed_budget` falls back to the regular table except trio | blert Hard solo 9, 4-man 20, 5-man 24 (B) | ours 7, 15, 18 | xarpus.spec.json.
- ToB, Xarpus Hard: `tob_xarpus_spawn_ticks` returns 4 for every Hard party | blert Hard solo 12 (1 raid, 8 gaps) (B) | ours 4 | xarpus.spec.json.
- ToB, Xarpus Hard: heal per orb | blert solo 21 (B) | ours 20 (regular fallback) | xarpus.spec.json.
- ToB, Xarpus Hard: P1 handoff after the last despawn | blert 7 (8 of 11), 6 (2), 11 (solo) (B) | ours 9 | xarpus.spec.json.
- ToB, Xarpus Entry: heal per orb uses the regular table | blert 6 at scales 1 and 4 (2 raids, 50 exhumeds) (B) | ours 20 solo, 9 four-man (`tob_xarpus_heal_amount`, read) | xarpus.spec.json.
- ToB, Xarpus Entry: defence | cache tob_xarpus_*_story stat2=100 / stat5=50 / stat6=80 (A) | ours keeps 250 (Normal record; only Hard rewrites defence; script read) | xarpus.spec.json.
- ToB, Xarpus Hard P3: rotates every 8 ticks like Normal (`tob_xarpus_turn`) | wiki Xarpus:265, Hard Mode:354, blert XarpusDataTracker.java:135 (no turn events in Hard) (C): he faces the quadrant he was hit from | ours 8-tick rotation | xarpus.spec.json.
- ToB, Verzik P2/P3 pools: `^tob_verzik_p23_hp_3/4/5` = 2437/2843/3250 | cache stat4 3500 on verzik_phase2/3; wiki hitpoints2 = hitpoints3 = 3500; party_hits 3500/3062/2625; Jagex Summer Finals 75/87.5% (A) | ours after P1 hp = 4874 = 2 x 2437 | verzik.spec.json.
- ToB, Verzik Entry: `^tob_verzik_p1_hp_entry`=240, `^tob_verzik_p23_hp_entry`=320 (x party size), P3 reuses P2's pool | cache stat4 300 / 400 / 600 (cache_npc_verzik.txt:430, 491, 552) and the Jagex Entry scaling (A) | ours Entry solo P2 and P3 both 320 (::tobvz hp=640=2x320); ::tobrun's ~tob_st_verzik_pools pins 240 and 960 (a test pinning the bug: delete with the fix) | verzik.spec.json.
- ToB, Verzik Entry: `^tob_verzik_p1/p2/p3_defence_level` 20/200/150 in every mode | cache Entry stat2 10/120/120 (A) | ours Normal values (code read) | verzik.spec.json.
- ToB, Verzik Entry: reds get `^tob_verzik_reds_hp_3` (150), exploding nylocas `^tob_verzik_combat_nylo_hp` (11) | cache Entry stat4 20 and 3 (A) | ours 150 and 11 (code read) | verzik.spec.json.
- ToB, Verzik: `^tob_verzik_combat_nylo_hp`=11 [blert] in all modes | cache stat4 25 on verzik_nylocas_melee/ranged/magic (A; the wiki Ischyros page lists 8381 under 11, resolved by the ranking) | ours 11 | verzik.spec.json.
- ToB, Verzik P1: `^tob_verzik_p1_launch_delay`=3 ("anim on T, bolt on T+3"; ::tobrun asserts launch 21) | cache seq 8109 throw burst at tick 2.6-2.9 = T+3 (D) | our own tick log: wind-up 8109 on T, projectile 1580 on T+2 in 8/8 Normal, 6/6 Entry, 6/6 Hard; npc_queue(4,0,3) armed in the boss's own pass fires on the second following tick | verzik.spec.json.
- ToB, Verzik P2: Athanatos cadence (counter from 0, floor 16, then 25% per attack) | blert 62 recordings: first cast at attack 0 in 12 of 43 Normal, by the 5th in 35 of 43; 19-24 attacks between casts (B) | ours first purple on the 17th attack (23rd in another run) | verzik.spec.json.
- ToB, Verzik P2: Athanatos landing | blert 6 ticks after the cast (85 of 92) (B) | ours 4 (cast 157, npc 161) | verzik.spec.json.
- ToB, Verzik P2 -> P3: retype 1 tick after the pool is spent, no seq 8118, no 8373 form | blert P3 id 6 ticks after the phase event (62/62) (B) | ours id at +1, first attack id+12 (total 13 vs 12 inside +-1, but 6 transition ticks missing) | verzik.spec.json.
- ToB, Verzik P3 yellows: `^tob_verzik_yellow_charge_ticks`=10 | blert pools 14 ticks (23/23 Normal), 20 (15/17 Hard); OpenOSRS yellows=14 (B); Strategies:970 "7 ticks between the end of this special attack and Verzik's next auto-attack" | ours 10 (x3 in Hard), next auto 6 after the pool | verzik.spec.json.
- ToB, Verzik P2 reds: `~tob_verzik_summon_reds` always adds two | Jagex Project Rebalance:391 (A): "If only one player is left alive during Verzik's second phase, only one Nylocas Matomenos ... will spawn"; blert 10 of 118 summons single | ours 2 (code read) | verzik.spec.json.
- ToB, Verzik P3 green ball flight (D): ours 76 cycles (projectile 1598) | OpenOSRS isGreenBall = remainingCycles > 210 (VerzikHandler.java:425), a flight above 7 ticks | verzik.spec.json.
- ToB, Verzik P3 webs special length (D, M82): ours web anim 8127 to next auto = 29 ticks | blert 38-50 ticks (41 raids; blert re-arms +10) | verzik.spec.json.
- ToB, Verzik P3: `^tob_verzik_p3_auto_max`=34 before the enrage | Strategies:942/981 "up to 33", "increasing ... to 34" at the enrage; infobox 33 (D) | ours 34 | verzik.spec.json.
