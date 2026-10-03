# Content bugs the raid loop found, and the commits that fixed them

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

The spec pass's 65 ToB rows. Seam pass 2 (`matthew-mbp-m4-raid-b1-seam2`) fixed most of
them; each fixed row is replaced here by the commit that fixed it and what the server
measures now (run directories under `build/quest_gate/`, named in SEAM_LEDGER.md). What
is still open is listed in full under "Still open". Paths: `minigame_tob/` is
`OSRS-Content/osrs239-content/server/scripts/minigames/minigame_tob/`; tables are
`docs/minigames/theater_of_blood/encounters/<room>.tsv`.

### Fixed by seam2

- ToB, every Entry boss and Entry npc: hitpoints multiplied by the party size. FIXED in OSRS-Content ebe115ad09 (seam2): ~tob_entry_scale stacks x1/1.9/2.7/3.4/4.0 (Jagex Entry Mode Improvements) for every boss, Maiden crabs (16/30/43/54/64), blood spawns, Vasilias, Xarpus and Verzik (`~tob_verzik_scale_pick`, pinned 300 -> 1020 at four); `::tobscale` reads Maiden 500/950/1350/1700/2000.
- ToB, Maiden: Entry crab 16 x n. FIXED in OSRS-Content ebe115ad09 (seam2): stacked, measured hp=16 solo.
- ToB, Maiden: the 70/50/30 transmog raised her hitpoints. FIXED in OSRS-Content ebe115ad09 (seam2): `~tob_maiden_transmog` carries hp and pool: 1444 -> 1444 solo Normal, 275 -> 275 Entry.
- ToB, Maiden Hard: period max(5, 10 - min(c,5)). FIXED in OSRS-Content ebe115ad09 (seam2): 10 - ceil(c/2) (blert): measured c1 9, c2 9, c3 8, c4 8, c6 7, c7 6, c8 6, c>=10 5; c=5 and c>=9 stay E [M20].
- ToB, Maiden Hard: 10 crabs solo. FIXED in OSRS-Content ebe115ad09 (seam2): fixed sets: seven solo on blert's tiles (N1, N2, S1 empty), ten at 2-5.
- ToB, Maiden: pool damage every second tick. FIXED in OSRS-Content ebe115ad09 (seam2): the room watchdog is a 1-tick queue: hit_player 28, 29, ... 33 consecutive.
- ToB, Maiden: blood spawn movement. FIXED in OSRS-Content ebe115ad09 (seam2): differently from the row: blert's slug lives show a free slug stepping EVERY tick (1009 of 1021), so the 33.7 % was a mixture of freezes, the dying room and stuck slugs; ours 0.968 of 11,478 slug-ticks moved, 0 two-tile steps. Spec row `maiden.blood_spawn_step` needs restating (see Still open).
- ToB, Maiden: no halving of the blood-spawn chance. FIXED in OSRS-Content ebe115ad09 (seam2): 20 stood / 10 unstood / 5 all dodged (`~tob_maiden_bloodspawn_chance_at`, ::tobrun pin); not driven with two players.
- ToB, Maiden Entry: blood spawn hp 90. FIXED in OSRS-Content ebe115ad09 (seam2): 10 stacked (cache), measured hp=10.
- ToB, Maiden: death 2 + 4 ticks. FIXED in OSRS-Content ebe115ad09 (seam2): `^tob_maiden_death_a_ticks` 3: 8093 K+1, 8364 K+3, 8365 K+5, npc_free K+9 (blert K+9), s2close_maiden_hard.
- ToB, Bloat: turn 64/32. FIXED in OSRS-Content ebe115ad09 (seam2): 17/7 (blert per-tick hazard 1 in 17 / 1 in 7): ours 0.0615 and 0.1526 per at-risk tick.
- ToB, Bloat: first step on down+34. FIXED in OSRS-Content ebe115ad09 (seam2): the rise tick is a walking tick: D+33 in 161/161 N, 159/159 H, 111/111 E.
- ToB, Bloat: lockout min 4. FIXED in OSRS-Content ebe115ad09 (seam2): the turn is rolled before the step: min 5.
- ToB, Bloat: first hand 4-6 after the rise. FIXED in OSRS-Content ebe115ad09 (seam2): free-running hand clock: first volley ON the rise tick 116/116.
- ToB, Bloat Hard: 7-11 hand gaps across a down. FIXED in OSRS-Content ebe115ad09 (seam2): all 4 or 6.
- ToB, Bloat: hands on the tank. FIXED in OSRS-Content ebe115ad09 (seam2): redrawn off the 6x6 tank: 0 of 36,000 shadows, 220 tiles.
- ToB, Bloat Hard: Hard hp table unread. FIXED in OSRS-Content ebe115ad09 (seam2): `~tob_boss_hp` reads it: 1800/1800/1800/2100/2400.
- ToB, Bloat/Sotetseg/Xarpus: the Normal npc type in every mode. FIXED in OSRS-Content ebe115ad09 (seam2): the mode's cache record with its levels in tob.npc (Bloat 10812/10813, Sotetseg 10865/10868, Xarpus _story/_hard); Maiden and Verzik stay Normal ids (Still open).
- ToB, Bloat: floored <= gates. FIXED in OSRS-Content ebe115ad09 (seam2): strictly below, unfloored: run max 59.47 %, first drop max 89.69 %, hurry 39.93 / 40.00.
- ToB, Bloat Entry: Normal fly and stomp damage. FIXED in OSRS-Content ebe115ad09 (seam2): flies 4-8, stomp 20-40 [wiki][M62] (minimum an approximation).
- ToB, Nylocas: lifetime 51 for both sizes. FIXED in OSRS-Content ebe115ad09 (seam2): animation on lifetime tick 52/53, despawn 52/55 after spawn (151/151, 41/41).
- ToB, Nylocas: no splits from a natural big explosion. FIXED in OSRS-Content ebe115ad09 (seam2): two smalls on the despawn tick at (0,0)+(1,1) (39/41).
- ToB, Nylocas: killed big despawn +3/+5. FIXED in OSRS-Content ebe115ad09 (seam2): +6 standing / +7 walking, splits on that tick.
- ToB, Nylocas: flicker +6..+8. FIXED in OSRS-Content ebe115ad09 (seam2): +5 in every lane and size.
- ToB, Nylocas: aggro swap +6..+8. FIXED in OSRS-Content ebe115ad09 (seam2): on entering the arena box: +10 west/east, +11 south, +9 east big.
- ToB, Nylocas: Vasilias three attacks per form. FIXED in OSRS-Content ebe115ad09 (seam2): two per form, +2/+3 then +4 (17/17 each mode); spawning form held 2 ticks.
- ToB, Nylocas: split tiles uniform over six. FIXED in OSRS-Content ebe115ad09 (seam2): (0,0) then (1,1).
- ToB, Nylocas Hard: bounce uncapped. FIXED in OSRS-Content ebe115ad09 (seam2): capped at 75 (code + ::tobrun pin; needs a second player to drive).
- ToB, Nylocas: bite mean 1.03. FIXED in OSRS-Content ebe115ad09 (seam2): 0/1/2 weighted 4:3:3, mean 0.90 [M7, D]: 0.899 over 2768 bites.
- ToB, Nylocas Hard: Prinkipas self-destruct at 52. CLOSED as "no self-destruct" in ebe115ad09: the 54-60 were kill times (all 12 recorded princes end at 0 hp); removed, alive 78-83 ticks until killed.
- ToB, Nylocas: Vasilias landing 3293,4246. FIXED in OSRS-Content ebe115ad09 (seam2): 3294,4247 (blert's coordinates are SW tiles), measured in all three modes.
- ToB, Sotetseg: +5 after the death ball. FIXED in OSRS-Content ebe115ad09 (seam2): +10.
- ToB, Sotetseg: death ball on the 10th magic tick. FIXED in OSRS-Content ebe115ad09 (seam2): ten ordinary balls, then the death ball instead of the 11th.
- ToB, Sotetseg: distance-dependent death-ball flight. FIXED in OSRS-Content ebe115ad09 (seam2): 15-tick flight, splat +16 at 3 and 22 tiles.
- ToB, Sotetseg: melee +2. FIXED in OSRS-Content ebe115ad09 (seam2): +1 (17/17).
- ToB, Sotetseg: maze teleport on the proc tick. FIXED in OSRS-Content ebe115ad09 (seam2): stun 5 + teleport at proc+3, first move proc+5.
- ToB, Sotetseg: maze cycle at proc+4n. FIXED in OSRS-Content ebe115ad09 (seam2): checks on world ticks that are multiples of 4.
- ToB, Sotetseg: chips every 8, rag every 2nd tick. FIXED in OSRS-Content ebe115ad09 (seam2): every 7 and every tick (the 1-tick watchdog).
- ToB, Sotetseg: death ball possible first after a maze. FIXED in OSRS-Content ebe115ad09 (seam2): held one ball short: the first post-maze attack is ordinary.
- ToB, Sotetseg Entry/Hard: Normal record. FIXED in OSRS-Content ebe115ad09 (seam2): the mode's records (Entry def 150, Hard attack 350).
- ToB, Sotetseg: solo maze false clear. FIXED in OSRS-Content ebe115ad09 (seam2): the boss is found from the room's fight tile: no "way onward" in 60 maze ticks, chips every 7.
- ToB, Sotetseg: no defence floor. FIXED in OSRS-Content ebe115ad09 (seam2): floor 100 on his own tick [wiki, D]: drain to 50 reads 100 a tick later.
- ToB, Xarpus: opening at 100 %. FIXED in OSRS-Content ebe115ad09 (seam2): 75 % of his pool [wiki, D]: 2812/3750, 3375/4500, 390/520.
- ToB, Xarpus Hard: ring at the stand-up. FIXED in OSRS-Content ebe115ad09 (seam2): 98 pools on fight tick 3, live acid in phase 1.
- ToB, Xarpus Hard: regular exhumed budget. FIXED in OSRS-Content ebe115ad09 (seam2): 9 / 16 / 20 / 24 (solo measured; four and five by code).
- ToB, Xarpus Hard: solo rise gap 4. FIXED in OSRS-Content ebe115ad09 (seam2): 12 (8 of 8).
- ToB, Xarpus Hard: heal 20. FIXED in OSRS-Content ebe115ad09 (seam2): 21 solo.
- ToB, Xarpus Hard: handoff 9. FIXED in OSRS-Content ebe115ad09 (seam2): 7.
- ToB, Xarpus Entry: heal 20/9. FIXED in OSRS-Content ebe115ad09 (seam2): 6 at every scale.
- ToB, Xarpus Entry: Defence 250. FIXED in OSRS-Content ebe115ad09 (seam2): the _story record (def 100), and the Normal record's own levels (it fought at Defence 1).
- ToB, Xarpus Hard P3: timed rotation. FIXED in OSRS-Content ebe115ad09 (seam2): faces the quadrant of the last landed hit.
- ToB, Verzik: P2/P3 pool 2437/2843/3250. FIXED in OSRS-Content ebe115ad09 (seam2): 2625/3062/3500 (cache 3500).
- ToB, Verzik Entry: units 240/320 multiplied by the party. FIXED in OSRS-Content ebe115ad09 (seam2): 300/400 stacked; P3 still reuses the P2 figure (Still open).
- ToB, Verzik Entry: Normal Defence. FIXED in OSRS-Content ebe115ad09 (seam2): 10/120/120 per phase (`~tob_verzik_defence_level`), measured with `::tobboss`.
- ToB, Verzik Entry: reds 150, exploding nylocas 11. FIXED in OSRS-Content ebe115ad09 (seam2): reds 20 (measured), crabs 3 stacked (code read; set as the crab's pool).
- ToB, Verzik: exploding nylocas 11. FIXED in OSRS-Content ebe115ad09 (seam2): 25 (cache stat4, tob.npc and the constant).
- ToB, Verzik P1: bolt on T+2. FIXED in OSRS-Content ebe115ad09 (seam2): T+3 (4/4).
- ToB, Verzik P2: first Athanatos on attack 17. FIXED in OSRS-Content ebe115ad09 (seam2): a 25 % roll from attack 0, then 20+ attacks (gaps 21, 22, 21, 20, 20, 20).
- ToB, Verzik P2: Athanatos landing +4. FIXED in OSRS-Content ebe115ad09 (seam2): +6 (7/7).
- ToB, Verzik P2 -> P3: one tick, no 8118. FIXED in OSRS-Content ebe115ad09 (seam2): six ticks: 8118 at E, 8374 at E+6, first attack E+12; the 8373 form is not worn (Still open).
- ToB, Verzik P3 yellows 10 then +6. FIXED in OSRS-Content ebe115ad09 (seam2): 14 (Hard 20), next auto +7.
- ToB, Verzik P2: always two reds. FIXED in OSRS-Content ebe115ad09 (seam2): one when one raider is left (measured solo).
- ToB, Verzik P3: green ball 76 cycles. FIXED in OSRS-Content ebe115ad09 (seam2): 221 (> 210) [oosrs, D].
- ToB, Verzik P3: webs special 29. FIXED in OSRS-Content ebe115ad09 (seam2): 42 to the next auto [M82, D].
- ToB, Verzik P3: auto max 34 throughout. FIXED in OSRS-Content ebe115ad09 (seam2): 33 until the enrage, 34 from it [wiki, D].

### Found and fixed in seam2 with no spec-pass row

- ToB, Maiden Hard solo: the Ramp (focus) damage applied solo. FIXED in OSRS-Content ebe115ad09 (seam2): off in a solo raid (Jagex New Modes hotfix 9 Jun 2021, A).
- ToB, Maiden: crab and blood-spawn hitpoints set under the 5-man base. FIXED in OSRS-Content ebe115ad09 (seam2): set as the pool (blert: a trio crab 75/75).
- ToB, Maiden: her death deleted a crab that was already dying (selftest SILENT DEATH, exposed by the transmog fix). FIXED in OSRS-Content ebe115ad09 (seam2): `~tob_maiden_delete_one` leaves 0-hp npcs to the engine.
- ToB, Bloat: down chance 21 was a pooled fit under the 1/64 turn guess. FIXED in OSRS-Content ebe115ad09 (seam2): refitted 25 Regular / 28 Hard against blert's first walks (p 0.09 / 0.75, 200 rooms each) [rec][M17b].
- ToB, Nylocas: a nylocas killed during its explosion animation was deleted at 0 hp (and a big split twice). FIXED in OSRS-Content ebe115ad09 (seam2): the death path owns it.
- ToB, Xarpus: the barrier's changetype into the feeding form reset his pool to the record's base. FIXED in OSRS-Content ebe115ad09 (seam2): carried in `~tob_wake_boss` and the stand-up.
- ToB, Verzik Hard: `~tob_verzik_absorb_reds` aborted (iterator lost), so no second red set came. FIXED in OSRS-Content ebe115ad09 (seam2): detonated after the sweep.
- ToB, Verzik: her stand-up changetype re-based her to the record's 9000, so the solo P1 shield was 3750 (HEAD 3626) against 1500. FIXED in OSRS-Content ebe115ad09 (seam2): `~tob_verzik_retype` carries hp and pool at all four changes: skip1 spent 3750 -> 1500.
- ToB, Verzik: reds were the record's 200 at every scale (a heal cannot move a spawn off its base), and read the build-time scale. FIXED in OSRS-Content ebe115ad09 (seam2): sized as their pool at the party's scale: solo Normal 150, Entry 20.

### Still open after seam2

- ToB, Maiden and Verzik (and Maiden's crabs and blood spawns): still the Normal npc ids in Entry and Hard; the `_story`/`_hard` records need their `[ai_timer]`/`[ai_queue]` bindings and the `::tobcrab*` debugprocs before they can be spawned. Maiden Entry Defence measured 200 (cache _story stat2 80). Client plugins keyed on id misread these modes.
- ToB, Maiden: the dying_a retype lands at K+3, not blert's K+1, because `[ai_queue3]` runs at the engine's corpse stage (tob.rs2 / engine); the animation itself plays at K+1.
- ToB, Maiden: spec row `maiden.blood_spawn_step` (337 permille) is wrong as a move rate; restate it as "a free blood spawn steps one tile every tick, never two (1009 of 1021), excluding frozen/dying ticks" (grade B). RESTATED 2026-10-02: 997-1000 permille of pairs outside still runs of 5+ ticks (blert 1043 of 1043, no still run of 1-4). Ours reads 968 (seam2's 1-4 tick stalls in the east entrance pocket), so a room test asserting the row fails until those stalls go.
- ToB, Maiden: blert's non-trail splat is drawn 13 (+1 = 14) against our pool loc's 11 (`^tob_maiden_blood_splat_ticks`, also the damage window); unsourced which is wrong (verify_tob_timings named mismatch).
- ToB, Maiden Hard: the per-set scuff roll (3 %) still applies in Hard; 0 of 30 recorded Hard sets were scuffed (not significant at 3 %).
- ToB, Bloat: later walks one tick short. With blert's walkTime (down - first step - 1) ours reads 33 in 8 of 50 attacked walks (bf_fix6_atk_normal) against blert's minimum 34, i.e. down-to-down 67 vs 68; the down clock armed in `~tob_bloat_get_up` must land the next down >= 68 after the last one.
- ToB, Bloat: one recorded later walk of 61 (raid 8e3ebf74, Bloat 27 %) lies outside 34..46, a speed-flip lockout extension below 40 %; the verifier names it rather than loosening.
- ToB, Bloat Entry: falling-flesh damage still 30-50, unsourced (M62).
- ToB, Bloat: spec table bloat.tsv rows turn_rate / turn_rate_hard state blert's odds estimator; restate as the per-tick hazard 5.8 % +-0.5 / 13.9 % +-0.9 (RESTATED 2026-10-02: 4.8-6.8 / 12.1-15.7 %, `bloat_stats.txt` lines 38-39); entry_fly_max / entry_stomp_max ranges vs the implemented 4-8 / 20-40; add the first_walk measurement for the refit down chance.
- ToB, Nylocas: a small killed by a player despawns hp0+3 standing / +4..+5 walking against blert's +2 (2751 events): needs `death_delay=1` on the 18 small records in tob.npc plus the engine row below.
- ToB, Nylocas (ENGINE): an npc that moved on its killing tick gets a 2-tick arrive delay (`npc_death_step` QUEUED / SS_OP_NPC_ARRIVEDELAY), so a walking big's death animation plays at t+3 where blert's table says t+2.
- ToB, Nylocas: Vasilias' +2 vs +3 first-attack offset is modelled as blert's distribution (2 in 3 early); blert's split depends on the style pair (rng->mel always +2), not modelled.
- ToB, Nylocas: explosion damage lands on the animation tick; no source says animation or despawn.
- ToB, Nylocas: spec row nylocas.prince_min_life should be re-worded "no self-destruct". RESTATED 2026-10-02: `never` (text), grade C; all 12 recorded princes died at hp 0.
- ToB, Sotetseg: both mazes of one room were never driven (no cheat arms the 33.3 % threshold); the cycle phase is proved across rooms. The phase anchor (world tick vs spawn tick) is not knowable from the data; 3 of 22 raids differ by a tick.
- ToB, Sotetseg: the post-maze death-ball guard is "a due counter held one ball short"; if the first post-maze attack is a melee the death ball comes one magic attack later than a flag would put it. No source distinguishes the two.
- ToB, Sotetseg: the M29 cue half ("second head bob") stays open; Hard Mode's maze "divided among" its runners is not modelled.
- ToB, Sotetseg: defence floor clamped on his own tick; a second attack in the same player phase as the drain can still see below 100.
- ToB, Xarpus Entry: `tob_xarpus_combat_story`'s `[ai_queue3]` is the generated wiki drop script, so an Entry kill skips `~tob_xarpus_died` (no 8063 collapse on `xarpus_death_story`).
- ToB, Xarpus: Hard duo budget/gap/heal unrecorded (regular fallbacks); Entry heal 6 at scales 2/3/5 by extension; Hard solo handoff blert 11 (one raid) vs 7. Four/five-man Hard figures are code only.
- ToB, Xarpus Hard: the gaze reads the hitter's tile when the hit LANDS; a ranged/magic hit in flight may differ from "attacked from". Exhumed spawning is not steered away from the ring.
- ToB, Verzik Entry: P3 reuses the P2 figure (400) where the cache's `verzik_phase3_story` is 600 (`^tob_verzik_p3_hp_entry` is defined; the one-pool peel `~tob_verzik_phase_hp` needs a P3 slice of its own).
- ToB, Verzik: the cache's 8373 transition form is not worn (tob.npc block and `~tob_boss_alive` entry exist since seam2; nothing changes into it).
- ToB, Verzik: P3 auto max 34 at the enrage is code only (33 sampled); yellow pool graphic 1595 is 183 cycles in our cache, so the pool vanishes on the client long before a 14/20-tick blast (blert's 14 is the live graphics object's lifetime; mechanism unsourced).
- ToB, Verzik: inline literals in tob_verzik.rs2 (post-floor purple roll 50 [M18, fitted], Hard yellows 20, webs 32, transition 6, Athanatos flight 20 + 160, green ball 181) could become tob.constant entries.
- ToB, room tests: the tick log has no room-start row; a `room_start` ticklog row written by `~tob_room_start` would give room tests their anchor.

## From the ToB room-authoring pass matthew-mbp-m4-raid-b1-rooms-tob (2026-10-02)

- ToB, Sotetseg (tob_sotetseg.rs2:948, spec.sotetseg.hp_entry_per_player, grade A): `~tob_sote_end_maze` `npc_changetype` hands the combat form full hitpoints: 364 before the first maze, 2044 after it (Entry pool 560), so the fight cannot be finished and the second maze never fires.
- ToB, Sotetseg (tob_sotetseg.rs2:821, technique.maze_path_lit): `~tob_sote_light_path` lights no tile (`~tob_sote_set_tile` 735 loc_find misses, 0 loc_set rows), so the shadow-realm path is never visible.
- ToB, Sotetseg (tob_sotetseg.rs2:905, spec.sotetseg.maze_off_on_3): a step-off ends the maze 5 ticks later on cycle tick 3 where the spec says 1.
- ToB, Sotetseg (coverage, unfixed): cadence 2,3,5 vs 5; first_attack_entry 5 vs 7; melee_roll_adjacent 636 vs 483; melee_hit_delay 0,1 vs 1; death_ball_hit_entry_solo 6-14 vs 15; ball_max_entry 3 vs 22.
