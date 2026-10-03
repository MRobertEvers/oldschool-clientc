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
  Seam3: a cast never recomputed the combat varps (every spell after `::setlevel magic`
  splashed); fixed in OSRS-Content 93707f5d60 (seam3) (`[proc,pvm_spell_cast]` calls `~player_combat_stat`).
  Possibly related, not re-measured on the man: still open.
- Quest loop impact (for the PR, not a raid row): `ToriRSServer_CombatAddXp` now follows
  LostCity's `Player.addXp` (a drained stat stays drained through xp), as RAID_ORCHESTRATOR.md
  section 4 asks. The quest test `deserttreasure` moved green -> RED because the Ice Path
  chill (`chill.rs2`, ported from LostCity) now really drains: trolls 376 -> 466 ticks and
  magic below Fire Blast's 59 at Kamil. An A/B binary without the hunk ran 249/249. That
  test belongs to the quest loop; it needs a restore or `::setlevel magic` before Kamil
  when this branch reaches v3.
- Quest loop impact of the eat-delay port (for the PR, not a raid row): OSRS-Content
  7936c59bf9 (seam6 eat_delay_port, landed 2026-10-03 on the owner's decision) makes an eat
  a clock instead of a `p_delay`, so it no longer holds the incoming hits that the old food
  timing absorbed. Two quest tests moved green -> RED; the quest loop re-authors them with
  more food or prayer:
  - `troll`: first failing row 31 `player.died` at tick 465, inside killGeneral's
    `await_dead_engaged` (173 ticks into the wait, 2824,10077 level 2). The Troll general's
    hits killed the player after all 26 sharks were eaten (`opts.eat below = 90`), with the
    general at 1/30. Green before the port (killGeneral 204 ticks).
  - `regicide`: first failing row 205 `goKillGuardAtSecondForest-walk-toForests`, run from
    the Lumbridge respawn. The death came at the end of leg 4 (about tick 4663): the Tyras
    guard fight (`killGuard-dead`, 268 ticks) ate all 12 sharks (`eat shark below 35`,
    lowest 20/70, OUT OF shark), then `crossTripwire` snagged ("You have been poisoned!")
    and `leg.4.end` read shark x0 with the hp orb at 0; 'Oh dear, you are dead!' / 'You wake
    up in Lumbridge.' Green before the port (473/473, 38 hp at the end of leg 4).

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

- ToB, Maiden and Verzik (and Maiden's crabs and blood spawns): still the Normal npc ids in Entry and Hard; the `_story`/`_hard` records need their `[ai_timer]`/`[ai_queue]` bindings and the `::tobcrab*` debugprocs before they can be spawned. Maiden Entry Defence measured 200 (cache _story stat2 80). Client plugins keyed on id misread these modes. Maiden half FIXED in OSRS-Content d2134f89f5 (seam5): `~tob_maiden_mode_form` retypes her to her mode's record on her first tick, her crabs and blood spawns spawn their `_story`/`_hard` records, and every story/hard body has its triggers (Entry `::tobboss record=entry def=80 att/str/rng/mag 140`, s5m4_after2). Verzik half FIXED in OSRS-Content 26ba1bd604 (seam8): she and her adds spawn and retype within the mode's records (10830-10836 / 10847-10853, adds 10841-10846 / 10858-10863). The `::tobcrab*` debugprocs matched only the Normal records: FIXED in OSRS-Content 2cddff56d5 (seam6), they match every mode's body, crab and slug.
- ToB, Maiden: the dying_a retype lands at K+3, not blert's K+1, because `[ai_queue3]` runs at the engine's corpse stage (tob.rs2 / engine); the animation itself plays at K+1. FIXED in OSRS-Content 24198b54ed with the engine change (seam9: her living records state `death_delay=0` and `npc_death_step` runs `[ai_queue3]` on the death animation's tick; dying_a K+1, fade K+5, free K+9).
- ToB, Maiden: spec row `maiden.blood_spawn_step` (337 permille) is wrong as a move rate; restate it as "a free blood spawn steps one tile every tick, never two (1009 of 1021), excluding frozen/dying ticks" (grade B). RESTATED 2026-10-02: 997-1000 permille of pairs outside still runs of 5+ ticks (blert 1043 of 1043, no still run of 1-4). FIXED in OSRS-Content a44e3d97bf (seam4): a slug checks the step the stepper is about to take (`~tob_maiden_slug_can_step`) and redraws on the same tick, else steps to an open neighbour: 1000 permille, 5538 of 5538 free pairs, no still runs of 1-4 (s4m3_after2).
- ToB, Maiden: blert's non-trail splat is drawn 13 (+1 = 14) against our pool loc's 11 (`^tob_maiden_blood_splat_ticks`, also the damage window); unsourced which is wrong (verify_tob_timings named mismatch).
- ToB, Maiden Hard: the per-set scuff roll (3 %) still applies in Hard; 0 of 30 recorded Hard sets were scuffed (not significant at 3 %).
- ToB, Bloat: later walks one tick short (33 vs blert's minimum 34). FIXED in OSRS-Content 93707f5d60 (seam3): the clock and cap are armed at rise + walk_min/cap + ^tob_bloat_walk_first_step (blert walkTime = down - first step - 1): 14 attacked walks min 34, down-to-down min 68 (b3_after_entry).
- ToB, Bloat: one recorded later walk of 61 (raid 8e3ebf74, Bloat 27 %) lies outside 34..46, a speed-flip lockout extension below 40 %; the verifier names it rather than loosening.
- ToB, Bloat Entry: falling-flesh damage 30-50. FIXED in OSRS-Content 93707f5d60 (seam3): 20-25 [video][M62] (grade E, one narrator), ^tob_bloat_entry_hand_min/_max in tob_bloat.constant; 17 Entry hand hits 20-25 (b3_after_entry).
- ToB, Bloat: spec table bloat.tsv rows turn_rate / turn_rate_hard state blert's odds estimator; restate as the per-tick hazard 5.8 % +-0.5 / 13.9 % +-0.9 (RESTATED 2026-10-02: 4.8-6.8 / 12.1-15.7 %, `bloat_stats.txt` lines 38-39); entry_fly_max / entry_stomp_max ranges vs the implemented 4-8 / 20-40; add the first_walk measurement for the refit down chance.
- ToB, Nylocas: a small killed by a player despawned hp0+3 standing. FIXED in OSRS-Content 93707f5d60 (seam3): `death_delay=1` on the 18 small records (blert mechanics page): standing 2 x29. The walking residue (+3 / +4) is the engine row below.
- ToB, Nylocas (ENGINE): an npc that moved on or just before its killing tick gets an arrive delay (src/torirsserver/torirs_server_combat.c:2830-2836, `npc_death_step`'s TORIRSSERVER_DEATH_QUEUED arm: +1 if it moved on D-1, +2 on D), so a walking small despawns hp0+3 / +4 (measured 4 x93 cheat kills, 3 x5 player kills, seam3) where blert's table says +2 ("Walking: stop and turn anim same tick t+1, despawn t+2"). Proposed: an npc record field (e.g. `death_arrivedelay=no`, parsed beside death_delay at torirs_server_content.c:1733) honoured in that arm, set on the 18 small records. FIXED (seam9, torirs_server_combat.c `npc_death_step`: no arrive delay for a record stating `death_delay` under 2, the 18 smalls; bigs keep it).
- ToB, Nylocas: Vasilias' +2 vs +3 first-attack offset is modelled as blert's distribution (2 in 3 early); blert's split depends on the style pair (rng->mel always +2), not modelled.
- ToB, Nylocas: explosion damage lands on the animation tick; no source says animation or despawn.
- ToB, Nylocas: spec row nylocas.prince_min_life should be re-worded "no self-destruct". RESTATED 2026-10-02: `never` (text), grade C; all 12 recorded princes died at hp 0.
- ToB, Sotetseg: both mazes of one room were never driven (no cheat arms the 33.3 % threshold); the cycle phase is proved across rooms. The phase anchor (world tick vs spawn tick) is not knowable from the data; 3 of 22 raids differ by a tick.
- ToB, Sotetseg: the post-maze death-ball guard is "a due counter held one ball short"; if the first post-maze attack is a melee the death ball comes one magic attack later than a flag would put it. No source distinguishes the two.
- ToB, Sotetseg: the M29 cue half ("second head bob") stays open; Hard Mode's maze "divided among" its runners is not modelled.
- ToB, Sotetseg: defence floor clamped on his own tick; a second attack in the same player phase as the drain can still see below 100.
- ToB, Xarpus Entry: `tob_xarpus_combat_story`'s `[ai_queue3]` was the generated wiki drop script, so an Entry kill skipped `~tob_xarpus_died`. FIXED in OSRS-Content a44e3d97bf (seam4): `tools/wiki_droptable.py` `MINIGAME_DEATH_QUEUES` lets tob_xarpus.rs2 own the trigger; the regenerated wiki_xarpus.rs2 is `[proc,wiki_xarpus_drop]`, called before `~tob_xarpus_died(xarpus_death_story)`: 8063 plays 2 ticks, book only (x3_after2_entry).
- ToB, Xarpus: Hard duo budget/gap/heal unrecorded (regular fallbacks); Entry heal 6 at scales 2/3/5 by extension; Hard solo handoff blert 11 (one raid) vs 7. Four/five-man Hard figures are code only.
- ToB, Xarpus Hard: the gaze reads the hitter's tile when the hit LANDS; a ranged/magic hit in flight may differ from "attacked from". Exhumed spawning is not steered away from the ring.
- ToB, Verzik Entry: P3 reuses the P2 figure (400) where the cache's `verzik_phase3_story` is 600 (`^tob_verzik_p3_hp_entry` is defined; the one-pool peel `~tob_verzik_phase_hp` needs a P3 slice of its own). FIXED in OSRS-Content d2134f89f5 (seam5): `~tob_verzik_p3_pool` and `~tob_verzik_p3_restate` grow the pool by P3 - P2 on the fall tick: `::tobvz fall hp=600`, P3 `610 of 1300` (s5v_after).
- ToB, Verzik: the 8373 transition form. FIXED in OSRS-Content 26ba1bd604 (seam8 tob_verzik_forms_and_presentation): 8118 on the P2 body, the 8373-family form at +2 with 8119, the P3 id at +6.
- ToB, Verzik: P3 auto max 34 at the enrage is code only (33 sampled). The yellow pool graphic half FIXED in OSRS-Content 26ba1bd604 (seam8): 1595 is sent as back-to-back copies that cover the 14/20-tick charge and end on the blast (the mechanism is this engine's delivery; blert's lifetime is the observable).
- ToB, Verzik: inline literals in tob_verzik.rs2 (post-floor purple roll 50 [M18, fitted], Hard yellows 20, webs 32, transition 6, Athanatos flight 20 + 160, green ball 181) could become tob.constant entries.
- ToB, room tests: the tick log has no room-start row; a `room_start` ticklog row written by `~tob_room_start` would give room tests their anchor.

## From the ToB room-authoring pass matthew-mbp-m4-raid-b1-rooms-tob (2026-10-02)

- ToB, Sotetseg: the maze changetypes re-based his hitpoints (364 -> 2044 of 560). FIXED in OSRS-Content 93707f5d60 (seam3): `~tob_sote_retype` carries hp and pool at both retypes: Entry 371/371/371 and 184/184/184 over two mazes, Normal 1997 and 990 of 3000.
- ToB, Sotetseg: the shadow-realm path lit no tile. FIXED in OSRS-Content 93707f5d60 (seam3): lit from the runner's first realm tick (proc+4), when a scene exists for `loc_find`: 25-30 `loc_set` rows per maze.
- ToB, Sotetseg: off on 3 ended the maze 5 ticks later, not 1. FIXED in OSRS-Content 93707f5d60 (seam3): the despawn check scans both grids' players in the NPC phase (`~tob_sote_grid_occupied`), wiki Advanced ToB guide: 1 and 1 (115->116, 147->148).
- ToB, Sotetseg (coverage): cadence 2,3,5 and melee_hit_delay 0 were the default retaliation swing; first_attack 5; Entry melee and ball maxima Normal. FIXED in OSRS-Content 93707f5d60 (seam3): tob_retaliate.rs2 (56 gaps flat 5, melee +1 on 13 of 13), first attack 6 (blert), Entry 20 melee / 22 ball (wiki infobox). Still open from this row: melee_roll_adjacent 636 vs 483 (not re-measured) and death_ball_flight_distance_independent 5 vs 0 (s3close_sote_noblock). death_ball_hit_entry_solo FIXED in OSRS-Content a44e3d97bf (seam4): solo Entry deals a flat 15 (`~tob_sote_ball_flat`; wiki Sotetseg:105, Entry Mode:191, yt_B_gjVdmfOrY.md:93), 15/15/15 (s4_sote_ball_after).
- ToB, Verzik (second launch): tob.npc:2058 [tob_verzik_phase2_armourednylocas] hitpoints=180 in Entry mode, spec verzik.entry_athanatos_hp 30 (grade A, cache_npc_verzik.txt:700); the Athanatos heals Verzik so her P2 bar holds at 21/30. Athanatos FIXED in OSRS-Content a44e3d97bf (seam4): `~tob_verzik_athanatos_land` gives the Entry spawn pool 30, Defence 40, Magic 40 (cache record 10844, cache_npc_verzik.txt:699-701); killed in 4 hits, P3 at tick 128 (s4e_verzik_entry_c). Still open: Entry uses the Normal crab blast (58 of 63) and lightning 33-48 (41 measured in seam4; no source gives an Entry figure, seam5). The P3 pool 600 is FIXED in OSRS-Content d2134f89f5 (seam5).

## From seam3 (matthew-mbp-m4-raid-b1-seam3, 2026-10-03)

Fixed in OSRS-Content 93707f5d60 (seam3) (the rows the triage named; the room-pass rows above are replaced in place):

- ToB, every boss: the default `[ai_queue1,_]` retaliated for `retaliate=no` records (Sotetseg's stray 8138 landing +0). FIXED: tob_retaliate.rs2, a no-op binding per record (93), the record author's `retaliate=no`.
- Tree-wide, magic: a cast never set the magic damage type, so the Nylocas style check nulled every spell. FIXED: `%varp6295_damagetype = ^magic_style` around `~player_hit_npc_prepare` (player_magic.rs2), and the cast recomputes combat varps (LostCity changestat.rs2): 10 damaging hits, 6 magic nylocas killed from 12 Fire Bolts.
- Tree-wide, death: a raid hit in flight at a dead player landed after the respawn (Maiden 99 in Lumbridge). FIXED: `~raid_death_clear_hits` (death.rs2) clears the personal ToB/CoX/ToA hit queues by name, LostCity's combat_clearqueue pattern.
- ToB, Maiden: blackstorm `hit_player` rows had npc_slot -1; a lethal auto launched at a corpse or a departed raider landed. FIXED: the queue carries her uid and instance, `npc_finduid` before `p_overhit`, 0-hp and out-of-instance targets skipped at launch and landing.
- ToB, Nylocas Entry: explosions rolled 18/21. FIXED: 8 for small and big (wiki Entry Mode page "about 8"), tob_nylocas.constant.
- ToB, Verzik Entry: P1 bolt 137, P2 bomb 44, slam 45, stomp 82, P3 melee 63, P3 auto 33/34. FIXED: 60 / 16 / 16 / ~34 / 36 / 20 (wiki Entry infobox), tob_verzik.constant.
- ToB, Verzik, every mode: the P2 stomp dealt a flat 82. FIXED (behaviour change in Normal and Hard too): rolled 1..max, "up to 82 ... always a successful hit" (Strategies:909).
- ToB, Xarpus: poison 8-16 in every mode, stomp pairs up to 15. FIXED: poison capped at the infobox 11 (Normal/Hard), Entry halved rounding up and capped at 6; stomp pair capped at 9 per tick (wiki Strategies "up to 9 damage per tick"), Entry at most 5.
- ToB, supply chests: nothing placed `tob_midway_chest_closed`, every Open re-awarded the points, Entry had no bandages, stamina was unlimited. FIXED: placed at `~tob_room_cleared` (after Bloat at the wiki map pin, local 5,33; after Sotetseg at local 17,5), reward once per chest per player, Entry 10 bandages, one stamina per chest (wiki Chest (Theatre of Blood), Entry Mode, New Modes).

Found and left open:

- ENGINE, retaliation (root of the ToB fix): the default `[ai_queue1,_]` still fires for every `retaliate=no` npc outside ToB (40+ in cox.npc, about 40 in toa.npc, 3 in zulrah.npc). Fix at src/torirsserver/torirs_server_world.c:5487-5489 (the npc queue drain dispatching SS_TRIGGER_AI_QUEUE1): skip queue 1 when the record says `retaliate=no` and the trigger would resolve to the `_` default; or add an `nc_retaliate` reader. Then tob_retaliate.rs2 can go. Found by tob_shared_combat_scripts.
- Tree-wide, combat varps: `%varp6285_com_magicattack` and friends are never recomputed on a stat change (no `[changestat]` trigger; LostCity has `[changestat,_] gosub(player_combat_stat)`). Casts and swings are covered; anything else that reads com_* sees stale values after `::setlevel` or a level-up until the next equip.
- Tree-wide, magic damage type, other callers: powered staves (gear/powered_staff.rs2:506: trident, sanguinesti, shadow), specs/pvm_purging_staff.rs2:36, pvm_eye_of_ayak.rs2:56, pvm_wild_cave_accursed_charged.rs2, pvm_voidwaker.rs2, pvm_blessed_saradomin_sword.rs2, pvm_verzik_special_weapon.rs2 (Dawnbringer) call `~player_hit_npc_prepare` with the weapon's style: a powered staff is nulled by a Hagios.
- death.rs2: "Oh dear, you are dead!" prints twice per death (`~combat_death_message` and `~respawn_message`).
- ToB, Verzik P1: `tob_verzik_p1_land` (tob_verzik.rs2, queued with player_uid only) writes `hit_player` npc_slot -1 and sets lethal against a 0-hp target; same fix as Maiden's (carry her uid, `npc_finduid`, skip 0-hp at launch, check hp and instance at landing).
- ENGINE, tick log: the HIT_NPC row has no dealer column (ToriRSServer_TicklogHitNpc, torirs_server_ticklog.c, pushes e=0 f=0), so a dying Matomenos absorbed into Maiden cannot be told from a player's killing hit. Proposal: carry the dealer pid, or -2 for an npc_damage from the npc's own frame.
- ToB, Maiden Entry pools: 10 + 2c carried from Normal, grade E [M121]; `^tob_maiden_pool_entry_divisor` = 1 discloses it.
- ToB, Maiden: the blood throw still aims a pool at a dead raider's tile (harmless, unsourced).
- ToB, Nylocas: protection prayers against wave nylocas: `tob_nylo_swing` never checked prayer. FIXED in OSRS-Content 2cddff56d5 (seam6): the matching protection prayer blocks the swing to 0 (wiki Protection prayers; styles wiki Entry Mode:157), 0 of 52 matched swings landed (s6np_waves_after6). The SPEC GAP stays open: the table still has no `nylocas.prayer_reduction` row (proposed under seam6 below).
- ToB, Nylocas Entry: Vasilias measured 9 then 10 per form and 2 attacks per form (the Normal figures) against `nylocas.vasilias_switch_entry` 15 / `vasilias_attacks_entry` 3-4 (grade D, one blert raid): no Entry switch interval is implemented.
- ToB, Nylocas: [M93] is stale: wiki Strategies:746 pins the Normal 18/21 explosion maxima; tob.constant's note and nylocas.tsv `explosion_max` (E) can be promoted to D.
- ToB, Sotetseg: an eat on proc+2 holds the maze teleport (`p_delay`, food.rs2) to proc+5 against blert's +3 (72/72 + 6/6); the maze no longer ends under such a runner, but the late landing remains; how the game exempts the teleport is unsourced. The cause (the eat's `p_delay`) is gone since OSRS-Content 7936c59bf9 (eat-delay port); the teleport timing under an eat was not re-measured.
- ToB, Sotetseg Entry: `^tob_sote_melee_prayed_max_entry` = 10 is derived (20 halved); no source.
- ToB, Sotetseg: the second chest's tile (local 17,5, east flank) is unsourced ("by the exit").
- ToB, Sotetseg: tob.constant's `^tob_var_maze_seen` comment still describes the old one-tick latch; the check now scans tiles and the realm's slot 58 holds the runner's landing tick (tob.constant was frozen this pass).
- ToB, Sotetseg: the boss HUD bar read 17 % on the tick `::tobboss` said hp 0 of 560 (shot 081 of tob_sotetseg_seam3_noblock); not investigated.
- ToB, Xarpus: the poison buff formula is unpublished; the infobox cap 11 clips the [M70] reading (42 of 69 Normal hits read 11 at 100 % absorbed). tob_selftest.rs2 pins `~tob_xarpus_scale_poison(8,100)` = 16.
- ToB, Xarpus Entry P3: retaliation rolled 50-75 in every mode against the Entry infobox "38+ recoil". FIXED in OSRS-Content a44e3d97bf (seam4): Entry scales the base by 38/50 (`^tob_xarpus_retaliate_min_entry`, floor 38 [wiki], ceiling 57 [M123]); 53 = 38 x 1.4 measured (x3_after2_entry).
- ToB, Xarpus: proposed spec row `xarpus.p2.stomp_max_entry` 5 per tick ([wiki][M40], E; Strategies:838 + Entry Mode:20), measured 5; tob.constant's stomp comment (16 per tick, open) is stale. Re-measured in seam4 over 36 stomp ticks: Entry max 5, Normal max 9; no change needed.
- ToB, Verzik Entry, SPEC GAPS (Normal figures still rolled): P2 lightning 48, exploding nylocas 63/26/8, Athanatos landing 78, reds blood spell 45, P3 power blast 80, web snap 40 [M50], the enraged P3 auto (kept 20).
- ToB, Verzik: the wiki's Normal infobox says P2 47 / 50 (bounce) / ~80 (under) against `^tob_verzik_p2_bomb_max` 44 and `^tob_verzik_p2_slam_max` 45 (Strategies prose); for a spec pass.
- ToB, Verzik P3 melee hits every player at `npc_range <= 1`, i.e. also under her; the wiki says "everyone adjacent"; unverified.
- ToB, supply chests: `tob_bandages` has no Heal script (the wiki: heals 20, restores prayer and run energy, super combat/ranging/stamina effects); the chest stays visually closed (the cache's chest state is per player, varbits 6460/6461); points and the death count are instance registers, so a party shares one balance where the wiki has per-player points.
- Driver (attack_exact_copy): the another-copy case is FIXED by the raid-driver commit [seam:matthew-mbp-m4-raid-b1-seam4]: `QD.drive._press_row` asks `api_drive.menu_rect(x, y)` (`UIMinimenu_HitOption`) and dismisses a stale menu only when the press would SELECT a row offering its op on another element (s4stale_before2 5 `hit_npc` on A -> s4stale_after3 0; hauntedmine, childrenofthesun, thefeud tick-identical and green). The seam3 text follows. A press whose pixel lands inside a menu an earlier `covered` press left open SELECTS one of that menu's rows (uitree_interact.c `interact_minimenu`), so a retry can attack another copy while answering `covered` (s3ec_stale_before1: 5 `hit_npc` on copy A, 0 on the asked B). The fixer's fix (close any open menu in `QD.drive._press_row` before moving) was reverted by the seam3 closer: its tick moved hauntedmine (the Dayth fight lost: died at 633 with 23 sharks eaten, green 95/95 without it), childrenofthesun (its own retry loop consumed the mesbox a tick earlier) and thefeud (talkToAVillager off-viewport, 24 FAILs) from green to RED; each was green again with HEAD pointer.lua and nothing else changed. A fix must keep the old timing when the press pixel is outside the open menu (dismiss only when the pixel is inside its rectangle). Re-measured on the final tree: s3close_stale_menu, 5 `hit_npc` on A, 0 on B.
- Client: a `::tele` on a run's first tick can SIGSEGV in `app_wev_actor_root_fine` <- `app_logic_tick` (HEAD and seam binaries, ~/Library/Logs/DiagnosticReports/torirs_questtest-2026-10-03-002740.ips); waiting 5 ticks avoided it.
- Selftest: no C stanza pins the `npc_face` tick-log row (extend the Hans facesquare stanza, torirs_server_world_selftest.c ~15923).
- Tooling: `raid_coverage.parse_row` cuts a measured comma list to its first element, so a distribution row passes while later instances are out of tolerance; the sidecar vocabulary has one scope per row (no `normal+hard`, and `party` ignores the mode).


## From the ToB room pass, second launch: tob_maiden (2026-10-03)

- ToB, Maiden: spec `maiden.blood_spawn_step` measured 983 permille vs 997-1000. FIXED in OSRS-Content a44e3d97bf (seam4): 1000 permille (see "Still open after seam2" above).
- ToB, Maiden: spec `maiden.death_a_len` measured 4 vs 3 and `maiden.death_total` 9 vs 7. NOT a content bug (seam4 re-derivation): blert raids 147ff143, db36efc7, e2fbcc68 and a40d3d9c (Hard) all show 8364 at K+1, 8365 at K+5 and the despawn at K+9, as ours does (seam2: 13 of 13). The cache's 90 + 120 cycles are animation lengths and AdvancedRaidTracker's +7 counts from K+1. OPEN for the spec table: restate `death_a_len` as "dying_a held 4 ticks, first 8093 (K+1) to first 8094 (K+5)" and `death_total` as 9 (killing blow to npc_free), grade B. The dying_a npc id still retypes at K+3 (row in "Still open after seam2").
- ToB, Maiden: spec `maiden.blood_extra_splats` measured 1,2 vs 2. FIXED in OSRS-Content a44e3d97bf (seam4): a refused scatter is redrawn (`^tob_maiden_extra_draws` 25), then the 5x5 is walked from a random offset (`~tob_maiden_extra_tile`): 30 of 30 throws carried two (wiki Strategies:596, TobMistakeTracker MaidenMistakeDetector.java:33).
- ToB, Maiden: spec `maiden.trail_damage_entry`: a trail was 10+2c against the table's 2-5 (grade D). FIXED in OSRS-Content a44e3d97bf (seam4): `^tob_maiden_trail_damage_entry_min/max` 2/5 [wiki] Blood_spawn:48, a trail told from a pool by `~tob_maiden_pool_index`; 35 trail hits all 2-5 (s4m3_after2/3). Uniform shape disclosed. `maiden.freeze_full_bonus` unmeasured: no verb reads equipment bonuses or casts an ice spell (open). Reachable since seam5 (no content change): Ancients by `::setvar varb4070_spellbook 1`, curve `~tob_matomenos_freeze_chance` (tob_maiden.rs2), +152 gear 3 of 3 barrages hit and froze the crab (s5m4_freeze1).

## From seam4 (matthew-mbp-m4-raid-b1-seam4, 2026-10-03)

Found and left open:

- ToB, Entry Verzik and Entry Maiden fight as their NORMAL records: `::tobboss` reads Verzik's att/str/rng/mag 400 in P1/P2 and 400/400/300/300 in P3 against `verzik_phase1_story` 180/150/180/180 (cache_npc_verzik.txt:427-432), `phase2_story` 200/150/180/180 (:488-493), `phase3_story` 180/200/180/180 (:549-554); `tob_maiden_*_story` 140 throughout (cache_npc_maiden.txt:253-258). Needs a stat set at the spawn or retype (tob_verzik.rs2, tob_maiden.rs2). FIXED in OSRS-Content d2134f89f5 (seam5): Verzik by `~tob_verzik_entry_levels` at the stand-up, take-off, landing and P3 change (P1 180/150/180/180, P2 200/150/180/180, P3 180/200/180/180, s5v_after); Maiden as her `_story` record (def 80, 140s, s5m4_after2). Entry pillar 185 -> 200 (cache :634) in the same commit. Since seam8 (OSRS-Content 26ba1bd604) Verzik's Entry levels are her Entry records' own and `~tob_verzik_entry_levels` is retired.
- ToB, Normal records with no levels (engine default 1), spawned in every mode: `maiden_elemental` (cache 100/100/100/-/100/100, cache_npc_maiden.txt:201-206; Entry 30), `maiden_blood_slug` (cache stat2/stat6 0), `tob_verzik_phase2_bloodnylocas` (cache def 40 / mag 40, cache_npc_verzik.txt:365-367; Entry 30/30). FIXED in OSRS-Content d2134f89f5 (seam5): levels in tob.npc from the cache; the Entry red lowered to 30/30 by `~tob_verzik_add_red` (`::tobaddlevels`: `red hp=20 def=30 mag=30`). Also found and fixed: `tob_maiden_70/50/30` had no levels, so every Normal Maiden past 70 % fought at Defence 1 (s5m4_before2); now 350/200/350/350/350 (:41-106).
- ToB, Verzik Entry Athanatos: Defence and Magic lowered by `npc_statsub`, base stays 50; a stat restore over a long P2 could lift them back (not measured).
- ToB, spec table: `encounters/sotetseg.tsv` `attack_level` note ("ours uses the Normal record in every mode") is stale (::tobboss reads 180/250/350). `sotetseg.death_ball_hit_entry_solo` should add sources/wiki_Theatre_of_Blood_Entry_Mode.wikitext:191 and tighten its tolerance from `range` to `exact` (grade D stays).
- ToB, Sotetseg: gear reduction (`~gear_reduce_damage`) applies to the solo Entry flat 15; no source says whether it should.
- ToB, scale: an Entry Xarpus entered as the fourth raid of one session stood up at 1768 of 1768 (520 x 3.4); fresh, 520. `^tob_var_scale` appears to count earlier re-entries (s4e_readouts_a).
- ToB, Xarpus: killing blow -> retype is 3 ticks in every mode (engine default `death_delay=2` after the 1-tick 8062, cache_seq_xarpus.txt:7). A `death_delay=1` on `tob_xarpus_combat`/`_hard`/`_story` would make it K+2; no source states the K -> retype gap (AdvancedRaidTracker TobIDs.java:26 counts 3 from the 8063 tick).
- ToB, Xarpus: proposed spec row `xarpus.p3.retaliate_max_entry` 57 [M123] (E); THEATRE_OF_BLOOD_PLAN.md open table has the M123 row.
- ToB, Xarpus: the exit barrier `tob_arena_barrier` at 6434,107 lets the player out while Xarpus lives (x3_base_normal, 936 hp left; `~tob_barrier_step` runs once the room starts, tob_party.rs2:136). Needs a source for when it opens.
- ToB, Xarpus: Normal and Hard drop no book; the wiki's table ("The wild hunt", Always, unversioned, wiki_Xarpus.wikitext:269) covers every mode, but only `tob_xarpus_combat_story` has an npc_stats join for the generator.
- ToB, Xarpus: `xarpus_death_story` has no tob.npc overlay (`xarpus_death` has hitpoints 5000, nomove, huntmode=none, retaliate=no); a hit in flight on the collapse form could end it early.
- Tooling: `tools/port_droptables_check.py` bar 4 (`resolve_body`) follows `@label` and `gosub(proc)` but not `~proc`; tob_xarpus.rs2 carries a `no-death-drop:` waiver until it does.
- ToB, Maiden Normal trail damage is still 10+2c against wiki Blood_spawn:48's 5-13 (row `maiden.trail_damage`, D).
- Driver: a press on a stale menu's `Walk here`, `Examine` or `Cancel` row, the asked copy's own row, or its title bar still behaves as before seam4 (a walk, a swallowed press). hauntedmine, childrenofthesun and thefeud have green timelines with such presses (2, 3 and 9); dismissing them moved all three RED. Fixing it means re-timing those quest tests (quest-loop work).
- Room tests to re-author: tob_maiden lost its accidental pool rows to the changed roll stream (copy 60/1, coverage 32 of 39) and must stand in a pool and on a trail on purpose; the tob_xarpus attempt's exit must go through the gate and its `retaliate_uplift` row use the Entry base 38-57; tob_verzik stops at P2 cycle 4 on its own hp<25 bail and a nil Athanatos cast at its line 342; the tob_sotetseg attempt died in maze 2 on its own tornado step (38 + 36 + a rag of 17 from 84 hp).

## From seam5 (matthew-mbp-m4-raid-b1-seam5, 2026-10-03)

Fixed in OSRS-Content d2134f89f5 (seam5): the rows above marked so. Found and left open:

- Tree-wide, eating: an eat held every queued npc hit (food.rs2 `eat_food`/`eat_anglerfish` `p_delay(^eat_delay)`, `p_delay` in seven other consumption files and `p_delay(1)` in the potion files; a Sotetseg melee due +1 landed +3/+4, a young dark wizard's spell the same; LostCity consume.rs2:101-110 never `p_delay`s). FIXED in OSRS-Content 7936c59bf9 (seam6 eat_delay_port, landed 2026-10-03 on the owner's decision): the three consume clocks in player/scripts/consumption/consume_shared.rs2 (varps 7218-7220), no `p_delay` and no `p_stopaction` in any consume script. Sotetseg melee +1 x8 with an eat on the swing tick (landeat_sote_eat), wizard eaten +1 as plain (landeat_eat), conformance seam.eat_does_not_hold_queued_hit and seam.eat_delay_clocks. The ToA supply drinks are not covered (row in "From seam6").
- ToB, Maiden: `~tob_spawn_boss` (tob_raid.rs2) still adds the Normal `tob_maiden_100` in every mode, and its comment is stale; `~tob_maiden_mode_form` is idempotent, so spawning the mode record there would make it a no-op.
- ToB, Maiden: `::tobboss` knew only the 100 % bodies and `::tobcrab*` / `tob_dbg_slugs` only the Normal records. FIXED in OSRS-Content 2cddff56d5 (seam6): every Maiden body, crab and slug in every mode (s6m_after2: record=entry at 100/70/30).
- ToB, Maiden freeze curve: Ice Rush and Ice Blitz go through `~pvm_default_spell` -> `~pvm_spell_hit_roll` (player_magic.rs2:642), not the Matomenos curve, while the wiki says binding and grasping scale too; the curve reads equipment magic attack only (`~equip_get_bonuses`) where Jagex's line counts Void and prayers.
- ToB, Verzik: the barrier pool was P1 + 2 x P2 (Entry 1100 solo). FIXED in OSRS-Content 2cddff56d5 (seam6): P1 + P2 + P3 (1300 solo Entry, 6750 solo Normal), `~tob_verzik_p3_restate` retired (s6v_after).
- ToB, Verzik: P2 overkill carried into P3: FIXED in OSRS-Content 2cddff56d5 (seam6), each phase opens on its own fresh pool (`~tob_verzik_fresh_pool`; P3 600 of 600 after a -30 overkill, s6v_after). Still open: she stays attackable during the fall and for three ticks after the throne (the cache's 8373 transition form has no Attack op, cache_npc_verzik.txt:138-166); such hits are discarded at the landing but still show as hitsplats.
- ToB, Verzik: the Athanatos healed into P3 past the phase pool. FIXED in OSRS-Content 2cddff56d5 (seam6): every heal clamps at the phase's own pool (P2 heal stops at 400, P3 at 600, s6v_after).
- ToB, Verzik Entry P2 lightning: no source gives an Entry figure (verzik.scope.tsv:70 scopes `p2_zap_max` to normal; Entry Mode wiki :228; the 10-14 of transcript VU4WQ1ghn4E:13 is the 2024 Normal solo cap). Kept at the Normal 48.
- ToB, `::tobaddlevels` matched only the Normal records and its line passed 252 characters with five adds. FIXED in OSRS-Content 2cddff56d5 (seam6): every mode's records, at most four groups plus `shown=` (137 characters, s6m_after2).
- ENGINE, npc defaults: a record that states no Ranged reads 0 (g_npc_default.ranged unset, torirs_server_content.c:4616-4618), where a cache record without stat5 means 1 (the Matomenos read `att=1 str=1 rng=0`).
- Driver, cast at range: a named copy 8 tiles across the Lumbridge goblin field was pressed fast and the server never cast (no XP, no refusal line for 10 ticks; s5fp_conf_rows4..6); unexplained reach or line of sight.
- Driver, slow-path cast: its timeout detail still says `the cast never ran` when the Magic XP was paid but the 6-tick flight outlasted `ticks` (s5fp_gob_slow2); fixed only on the fast path to keep the quest path byte-identical.
- Room tests to re-author: tob_maiden must address Entry Maiden by her `_story` symbols (the committed file's `npc.nearest('tob_maiden_100')` is no_row in Entry); the tob_verzik attempt's rows that derive 1100 totals and its P3 rows (now out of food in P3 at 626, Verzik 535 of 1300); tob_nylocas is a tactics problem now (fast press: 0 presses of six ticks or more; out of food at click+448 with the cap lifted); the sampled-green tob_sotetseg dies after maze 1 on two tornado hits on consecutive ticks (295:43, 296:40, then 297:15), with or without the food fix.

## ToB room pass, fourth launch (tob_sotetseg review)

- ToB, Sotetseg: the maze proc runs one tick after the crossing splat (proc-1 in both mazes of the Entry solo run), so the last reading before the 2nd proc is 32.5 % against spec sotetseg.maze_trigger_hp 66.6,33.3 (grade C, tol range: blert sees 33.3 to 34.1, the crossing hit lands on the proc tick). Server: tob_sotetseg.rs2:809-834 (`~tob_sote_check_maze` runs after the splat is applied, tob_sotetseg.rs2:98-109). Evidence: raid_coverage tob_sotetseg, ledger row spec.sotetseg.maze_trigger_hp.

## ToB room pass, fourth launch (tob_verzik review)

- FIXED in OSRS-Content 2cddff56d5 (seam6): compared whole (left x 100 <= pool x pct), 120/600 enrages and 121/600 does not (s6v_after, s6v_after120); the reds had the same truncation (141/400) and now come at 140. Was: ToB, Verzik P3 enrage: `~tob_verzik_enraged` (tob_verzik.rs2:2338) compared an integer percent (`divide(multiply(left,100),pool) <= 20`), so 125 of 600 hitpoints (20.8 %) already enrages against spec verzik.p3_enrage_threshold (grade B, 20 percent at or below; blert sees tornadoes first at 13.7-19.9 %). Evidence: raid_coverage tob_verzik, ledger row spec.verzik.p3_enrage_threshold (tick 555).

## From seam6 (matthew-mbp-m4-raid-b1-seam6, 2026-10-03)

Fixed in OSRS-Content 2cddff56d5 (seam6): the rows above marked so. Found and left open:

- Tree-wide, eating: the eat-delay port (LostCity consume.rs2:96-110 as three clocks in a new player/scripts/consumption/consume_shared.rs2, varps 7218-7220; no `p_delay` and no `p_stopaction` in food.rs2, td_consumables.rs2, nightmarezone_potion.rs2 and 32 consumption files; an eat adds 3 to a running weapon delay per wiki Food/Fast foods). The seam6 closer held it back because the suite moved troll and regicide green -> RED. FIXED in OSRS-Content 7936c59bf9 (landed 2026-10-03 on the owner's decision, with the C selftest's full-health bite waiting 3 ticks): conformance 267/267, C selftest 11 (baseline), ::tobrun OK 56; suite 116 green + deserttreasure RED (baseline) + troll and regicide RED, accepted (row under "Quest loop impact of the eat-delay port").
- ToA, supplies: minigame_toa/scripts/toa_supplies.rs2 (the supply consumes, :43 to :258) `p_delay(1)` after every drink, so they hold queued hits too; the eat-delay port did not cover them.
- Tree-wide, eating: OSRS fast-food data the port leaves unported. The other combo foods (gnome battas, crunchies and bowls; crystal and corrupted paddlefish outside the Gauntlet; smelly kebab), the members' pie halves at 1 tick (wiki Template:Fast_foods_table says 1,1 for wild, summer and dragonfruit pies; LostCity's rule gives 1,2) and giant crab meat's 2-tick bites. Not on any raid path.
- ToB, Nylocas: spec rows the table lacks, for the next spec pass. `nylocas.prayer_reduction`: a wave nylocas hit under the matching protection prayer is 0 (D; wiki Protection_prayers + Protect_from_Melee; measured 0 of 52, s6np_waves_after6). `nylocas.vasilias_prayed_max`: Normal ranged/magic 1-17 under the matching prayer, melee 0 (D; wiki_Theatre_of_Blood_Strategies.wikitext:752).
- ToB, Nylocas: `^tob_vasilias_prayed_max` = 17 is applied in Entry (off-prayer 24) and Hard (off-prayer 105); only the Normal figure is sourced. Disclose as grade E rows `nylocas.vasilias_prayed_max_entry` / `_hard`.
- ToB, Nylocas: every nylocas and Vasilias hit is `add(1, random(max))` with no accuracy roll, so an off-prayer hit is never 0. No source in the tree states their accuracy.
- ToB, Nylocas: `combat_damage_player` (skill_combat/scripts/npc_combat_magic.rs2:261) applies `~gear_reduce_damage` with `^magic_style` for every caller, so a nylocas melee or ranged hit is reduced by the magic defence bonus.
- ToB, Nylocas: the prayer is read on the swing tick (as Vasilias and the tree's playerhit_n_* do); no source states the tick for wave nylocas specifically.
- ToB, Verzik: `~tob_verzik_p2_purple_due`'s first Athanatos cast is a 25 % roll per attack with no ceiling; with the changed P2 pool one run cast first at 18 against spec `p2_purple_first` 0-12 (s6copy_verzik).
- ToB, Verzik: tob.constant's comments at :2380-2384 and :3080-3087 still describe the shared P1 + 2 x P2 peel; the figures are right.
- Room tests to re-author: tob_verzik's `spec.verzik.entry_p2_hp_1p`, `entry_p3_hp_1p` and `reds_threshold` derive the pools from P1 + 2 x P2 and "the 400 floor" and must read `::tobboss phase_hp` (the fixer's copy ran 95/99 and ran out of food in P3 at tick 624, P3 now a full 600); tob_nylocas must pray during the waves (a praying copy survived to tick 482, 28 waves).

## From seam7 (matthew-mbp-m4-raid-b1-seam7, 2026-10-03)

Seam 7 was the presentation pass (OSRS-Content a006110486). Found by the fixers and left open:

- Tree-wide, sound: area sounds sent loops 0. FIXED in OSRS-Content 26ba1bd604 (seam8 sound_area_loops): sound.rs2 passes loops 1 (Kronos184 Message.java:175; rs_audio.c:124). About 104 direct `sound_synth(x, 0, y)` calls ported from LostCity are still silent (99 under quests/, prayer.rs2:395 prayer activation, gnome_gate.rs2:66, wizard_cromperty.rs2:119, poh_fairy_ring.rs2:281, poh_enter_leave.rs2:138; the prayer.rs2:63 comment says '0 loops, matching LC'). The 2004 lanes' loops-0 sounds are silent in our client too (rs_audio.c refuses 0 for every revision; Wave.java:146-149 plays it once).
- ENGINE, music: the region unlock wrote the music VARIABLE as a varp id. FIXED in the seam8 parent commit 0397a0b29 (seam8 ticklog_player_kinds_and_engine_rows): `ToriRSServer_MusicVariableVarp` (clientscript 7305's table); Ver Sinhaza unlocks bit 8 of varp 1681; C selftest stanza 'region music unlocks the musicmulti word'.
- ENGINE, npc sounds: `npc_sound_nearby` (torirs_server_combat.c) measures the 12-tile carry from the npc's south-west tile, not its footprint; a ranged player 9 tiles off Maiden's east edge hears none of her hit sounds (and a twisted bow on the far side of Bloat hears none of his: seam8 closer conformance run 1). Not changed in seam8: LostCity measures from npc_coord too (npc_death.rs2:18, sound.rs2:15); a footprint measure needs an OSRS source.
- ENGINE, server: a sub-0 pause button acted on the previous slot. FIXED in the seam8 parent commit 0397a0b29 (seam8): `sub >= 0` latches. tob_board.rs2:393-395 and :539-543 still carry the old 'SUB 0 (Back) DOES NOT REACH last_slot' comments (behaviour right; comments stale).
- CLIENT, cs2: a RUNCLIENTSCRIPT that GOSUBs a proc the client has not loaded stops at the GOSUB and never finishes when more RUNCLIENTSCRIPTs follow in the same flush (tob_partydetails' first push draws only its last row; TORIRS_CS2_TRACE_SCRIPT=2317). Path: src/game/task_cs2_run.c TASK_CS2_YIELD_SCRIPT.
- Driver: `t.chat.count` cannot answer a `p_countdialog` while a main modal is open (tob_partydetails Preferred Size/Level; likely the midway store's Buy-X), and `t.ui.invoke` has no comsubid, so a cc-created child (the store's stock slots) cannot be clicked.
- Driver: `QD.player.DEATH_LINE` is 'Oh dear, you are dead!'; a Theatre death now prints 'You have died. Death count: N.', so the death fence catches it only through the hitpoints-0 reading (state.lua).
- Engine/driver: no tick log kind for a player's spotanim or animation. FIXED in the seam8 parent commit 0397a0b29 (seam8): `player_anim`, `player_spotanim`, `loc_anim` and `npc_say` rows.
- ToB, content vars: varps 1740, 1746 and 3052 undeclared. FIXED in OSRS-Content 26ba1bd604 (seam8 tob_lobby_stranger_and_supply_chest): declared transmit=yes (1740/3052 temp, 1746 perm); 'Make party' and the store's points label read right.
- ToB, all rooms: bosses targeted a raider in the spectator cage. FIXED in OSRS-Content 26ba1bd604 (seam8, every room seam): every room's hunts skip `~tob_jailed`. Not exercised with one client: Hard Vasilias' two spreads, Sotetseg's ricochet and death-ball sharing skips, the Hard arena tornado.
- ToB, death: the inventory and equipment tabs are not closed in the cage (Near Reality closes them; the video shows them blank). The logout-during-a-fight half FIXED in OSRS-Content 26ba1bd604 (seam8 tob_hud_chat_lines_and_room_flow: a death, settled on return). A maze runner who dies in the realm is caged and the realm let go (seam8 tob_sotetseg_tornado_and_cage); what the real game does with the maze is unsourced. A logout during Sotetseg's maze (the realm branch of `~tob_on_reenter`) still folds the maze and restarts the room.
- ToB, death: Entry wipe bandages are 3 per raider (Entry Mode page :35) where the 2023 newspost says the number depends on what was used; grade D.
- ToB, lobby: `~tob_orator_place` (tob.rs2) is not called; one line in tob_raid.rs2 `~tob_build_room` after `~tob_spawn_boss` would place them (rooms 1-5). The Stranger's shop and the escape crystal FIXED in OSRS-Content 26ba1bd604 (seam8; tob_lobby.inv, `[opheld1,tob_teleport]`). Open: the Poll 83 logout-with-crystal rule (`~tob_escape_crystal_login`, written in tob.rs2, needs its call in `~tob_on_reenter`); A Night at the Theatre's Stranger talk must set `%varb15607_tobquest_stranger_vis` and offer the crystal and shroud options (nightatthetheatre.rs2); the crystal's animation/graphic, Configure/Auto-afk/Wear and the other activities are not implemented; the Hard chest death bands are an approximation (tob_lobby.constant `[M-open: hard chest death bands]`).
- ToB, lobby: `[oploc1,tob_scoreboard]` (tob_chest.rs2) still prints chat lines; it should call `~tob_board_scoreboard_open`. The lobby `tob_hud` box is not mounted in Ver Sinhaza, so varbit 6440 written on forming a party is not drawn, and `~tob_hud_close` resets it after a raid while the party persists. A party record whose members all log out stays in the 32-slot instance pool (no logout hook). The scoreboard keeps attempts, completions and deaths per account, not per mode or size; global stats are not kept.
- ToB, lobby: apply, withdraw, accept, reject, Unblock, kick, leave, the leader's re-order arrows, the friends filter and the members' call-in are implemented but not run (one client). `~tob_join_raid` still joins whatever raid carries the flag, not this party's.
- Tree-wide, retrieval: `~retrieval_room_for_all` (player/scripts/retrieval.rs2) passes its running count as `inv_itemspace`'s 4th argument, which the engine reads as a slot limit, so `~retrieval_reclaim_all` refuses 'Not enough space' when the service holds 2+ stacks. The ToB claim chest works around it; every other retrieval service (Nex excepted) still has it.
- Tree-wide, God Wars: `~gwd_in_bounds_coord` (godwars_chamber.rs2) translates the Nex chamber rectangle into whatever instance the player is in, so `~gwd_nex_on_death` claims deaths in other instanced activities (Inferno, Fight Caves, ToA, CoX) whose tile falls in that local box. death.rs2 now skips it for Theatre deaths only.
- ToB, vault: the common table, the per-player unique and the unread Entry/Hard shares. FIXED in OSRS-Content 26ba1bd604 (seam8 tob_rewards_and_completion): one team roll 10/91 | 10/77, the wiki's 29-row table, Entry 20 / Hard 115 / 130 in time, tertiaries, completions Normal/Hard only, CA thresholds and six new tasks. Open: [M22] the deaths/performance scaling, the Cabbage + Message deadweight, Lil' Zik as a follower, HM Grandmaster 385 (needs varp3057 declared and a Hard counter), the 27 mechanical/restriction CA tasks, the perfect bits set on a raid resumed at Verzik, the six-slot chest overflow.
- ToB, vault: the vault stairs `tob_treasureroom_stairsup` 32995 have no handler; which orb slot owns which side chest is not sourced (slots 1-4 follow the wiki pin order); Discard-all has no confirmation; 'You find: <unique>' is printed at roll time; unclaimed loot from a previous raid adds to the next (six slots; real rule unsourced); the vault reservation carries no resume flag (a logout forfeits the loot anyway). The jingle half FIXED in OSRS-Content 26ba1bd604 (seam8: every raider hears it once).
- ToB, vault: the war table (33011) stands inside the spectator enclosure and a raider cannot reach it; consistent with the wiki ("spectators can read the war table"), so it waits on spectating.
- ToB, Sotetseg: the arena floor turned plain at fight start. FIXED in OSRS-Content 26ba1bd604 (seam8 tob_hud_chat_lines_and_room_flow): `[queue,tob_room_settle]` syncs it at room entry.
- ToB, Sotetseg: the realm tornado moves only on ticks the runner moves (sote7_after3: stalled at 6430,153 from tick 215 to 258); the wiki says it follows the path. A mechanic row for a content seam.
- ToB, Sotetseg Hard Mode: the maze divided among its runners by orb order (wiki Sotetseg:102, Theatre_of_Blood_Hard_Mode:340) is not modelled; the cache's `tob_sotetseg_hard_lighttile_parent_1..4` (41750-41753) on varbit 12269 carry its presentation. A mechanic row for a content seam.
- ToB, Sotetseg: unplaced for want of a source: spotanim 1603 `tob_sotetseg_zap`, sound 3970 (`_impact_2`), seq 8141; loc 33036 `tob_sotetseg_triggeredtile` has one source (Near Reality).
- ToB, Verzik: the Athanatos heal and poison land on the timer tick while the globule is still flying (NylocasAthanatos.kt:95-102); no tick log row records an npc heal, so a fix cannot be proved (an `npc_heal` kind first). The zap half FIXED in OSRS-Content 26ba1bd604 (seam8: the hit lands with its ball, cast + flight/30). The heal is now readable (seam9 `npc_heal` tick log row: +10 at t244/249/254 after an Athanatos spawned at t239, `[proc,tob_verzik_athanatos_tick]`); the heal-timing fix is still open.
- ToB, Verzik: `verzik_powerblast_projanim_down` 1596 is a spotanim on each player at the blast where Near Reality sends it as a projectile from Verzik 9 ticks in (DotPassiveSpell.kt:53-58); one source, left.
- ToB, Verzik: the Hard Mode pillar debris still shows Bloat's 1570 (a commented stand-in); 1599, 3028, 3988, 4008 and seqs 8051/8054/8055 have no source. `^tob_verzik_throne_door_lx` (tob_verzik.constant) and `^tob_vault_trapdoor_lx` (tob_vault.constant) are the same 31; keep one.
- ToB, Xarpus: sounds 3231, 3944 and 3949 have only their cache names; sourcing them needs a recording's audio cross-correlated with `audioprobe --effect` (the corpus mp4s are video only). [M124]: which pool dissolves on which of the four ticks is approximated as uniform; it belongs in THEATRE_OF_BLOOD_PLAN.md's open table.
- ToB, Nylocas: seqs 7990/8001 (the mage and ranged nylocas' melee swings) and sounds 3982/3993/3946/3959 are unsourced; the likely use is a mage or ranged nylocas biting a pillar. The small's first death half ('turn') has no identified seq.
- ToB, Bloat: fly sound variants 1/4/6 (3544, 3951, 3983) have one source (Near Reality); `tob_bloat_cage_wall` 32956, `tob_dungeon_bloat_dead_merc` 32968 and `tob_bloat_chain_hook_boots` 32971 have none. The 3971 half FIXED in OSRS-Content 26ba1bd604 (seam8: Bloat's defend sound; the hand plays barbassault_splat 3308 per volley, grade E M163).
- ToB, Maiden: `tob_maiden_initial` 32972 and `tob_maiden_dead_remains` 32973 are unsourced for placement; `tob_maiden_blood_hit` 3989 has no binding.
- ENGINE, queues: a player queue armed from inside the same player's queue script with delay N runs N+1 ticks later (delay 1 measured 2); armed from an npc's turn it runs N later. Check against LostCity's Player.processQueues.
- GENERATOR drift: `tools/gen_npc_combat.py --write` would rewrite 516 ledger files and drop 420 blocks from npc_anims.generated.npc (hand blocks such as tob.npc shadow those npcs); regenerate deliberately with a review.
- Room tests: the uncommitted tob_xarpus attempt counts every pool `loc_set -1` after U as `pool_gone` for spec `xarpus.p2.splat_lifetime` with no upper tick bound; the pools now dissolve at K+4..K+7, so bound the row to ticks before the collapse.

## From the ToB presentation spec pass matthew-mbp-m4-raid-b1-spec-tob-av (2026-10-03)

Every `content_bugs` entry of the seven spec reports (build/spec_state/matthew-mbp-m4-raid-b1-spec-tob-av/<room>.spec.json), one line each. Entries the finder itself marked as an observation or "not a bug" are copied too and say so. Grades are the table row's after the closer's re-derivation.

- ToB, Maiden (ENGINE): her dying_a body is held 2 ticks (K+3..K+5) against blert's 4 (K+1..K+5, 13 of 13). Settled as engine in seam8: `[ai_queue3]` runs at the CORPSE stage K+1+death_delay (torirs_server_combat.c:2873 sets death_tick at ARRIVE; :2876-2921 CORPSE) and advance_npcs runs no timer or queue on an npc holding death_tick. Smallest change: ARRIVE falls through to CORPSE when death_delay == 0; then tob.npc death_delay=0 on the 12 living Maiden records and ^tob_maiden_death_a_ticks 3 -> 5 (check no other npc states death_delay=0 first). Row maiden.av.death.dying_a_form_ticks (B). FIXED in OSRS-Content 24198b54ed with the engine change (seam9; the C selftest checks dying_a 4 ticks from K+1 and the fade 4 from K+5).
- ToB, Maiden: 8093 sent twice per death. FIXED in OSRS-Content 26ba1bd604 (seam8 tob_maiden_death_and_cage): once, at K+1 (the engine's death_anim).
- ToB, Maiden, tob_maiden.rs2 ~tob_maiden_pool_land: loc 32984 is placed under every thrown pool. Evidence found in seam8 (video B_gjVdmfOrY 9:01 pools as splatter alone, 9:28 a spawn's trail of blobs; cache tob_blood_splat 300 cycles; blert MaidenDataTracker, TobMistakeTracker): the patch is ready (build/seam_state/matthew-mbp-m4-raid-b1-seam8/scratch_maiden/pool_loc_change.diff) and NOT landed, because it must land with torirs_server_world_selftest.c:32717-32760/:33053 counting pools by spotanim 1579, tob_maiden.lua's pool_life re-keyed, and tob_selftest.rs2's readouts. Edge: in Hard at period <= 7 a far pool could lose its last 1-3 ticks to the next throw's register reset. Row maiden.av.blood_throw.pool_loc (D). FIXED in OSRS-Content 24198b54ed (seam9: seam8's pool_loc_change.diff landed with its C selftest stanza, 'a thrown pool is graphic 1579 alone'). `test/raids/tob_maiden.lua`'s pool rows still key on 32984: the next maiden author re-keys them to map_spotanim 1579.
- ToB, Bloat: no defend sound, and 3971 played to a player a hand hits. FIXED in OSRS-Content 26ba1bd604 (seam8 tob_bloat_sounds_and_cage): tob.npc defend_sound tob_bloat_hit on the three records; the hand plays barbassault_splat 3308 per volley to every raider (Near-Reality PestilentBloat.kt:227-228,251; grade E M163).
- ToB, Bloat, tob_raid.rs2 ~tob_room_death_sound (observation, not a bug by any source): the dying cry 3965 arrives 4 ticks after npc_death, 3 after the 8085 start (the watchdog's delay); no source states its tick. Row bloat.av.death.sound (D). Found by bloat.spec.json.
- INSTRUMENT, tick log: no player_spotanim kind. FIXED in the seam8 parent commit 0397a0b29 (seam8).
- ToB, Bloat (observation, no source): the falling-flesh shadow among 1570-1573 is random in ours; the cache gives no rule. Found by bloat.spec.json.
- ToB, Nylocas (ENGINE), small killed by a player: measured with real hits in seam8 (scythe fast press, 87 kills): standing small anim +1 / despawn +2 (49 of 49), as blert; a small that moved on hp0-1 anim +2 / despawn +3 (10 of 10), the engine arrive-delay row above (torirs_server_combat.c:2830-2836). Proposed: a record field such as death_arrivedelay=no parsed beside death_delay (torirs_server_content.c:1733), set on the 18 small records. FIXED (seam9, the same `npc_death_step` change: 18 of 18 small deaths anim +1 / free +2 with real scythe hits, the walking big still +2).
- ToB, Nylocas, big killed by a player: not a content bug (seam8): the +3 was the ::kill path (it lands on the npc's own step tick). With real hits bigs read +1/+6 standing and +2/+7 walking, as blert.
- ToB, Nylocas, support npc 424/836. FIXED in OSRS-Content 26ba1bd604 (seam8 tob_nylocas_deaths_and_cage): tob.npc defend_anim/death_anim null on the three support records.
- ToB, Nylocas, support collapse: loc_anim 8074 on 32863 KEPT and sourced in seam8 by the cache's frame data: 8074 is the only other seq on 8073's frame group 10272 and starts on 8073's one frame 673185793 (configs/all.seq:181317-181348), 120 cycles = the 4-tick collapse. Still grade D (cache only).
- ToB, Nylocas: sound 4020 at death ticks: not a bug (seam8). It is the defend sound on every hit; a real-hit log has every 4020 row on a hit_npc tick and slot, and a ::kill log shows it on death ticks because the kill is the only hit.
- ToB, Nylocas: sound 3969 tob_pillar_collapse is sent by the Nylocas collapse but AV_INVENTORY lists it under verzik (frame sound of verzik_pillar_collapse); cache name only. Row nylocas.av.support_collapse.sound (D). Found by nylocas.spec.json.
- ToB, Sotetseg / tree-wide sound: 3994 and 4001 sent with loops 0. FIXED in OSRS-Content 26ba1bd604 (seam8 sound_area_loops): loops 1, heard (client trace: queued and played).
- ToB, Sotetseg: floor 33033 at the fight start. FIXED in OSRS-Content 26ba1bd604 (seam8): placed at room entry +2 ticks, barrier uncrossed.
- ToB, Sotetseg: the Entry/Hard tornado was the Normal record. FIXED in OSRS-Content 26ba1bd604 (seam8 tob_sotetseg_tornado_and_cage): 8389 / 10866 / 10869 by mode. Open: tob.npc has a [tob_sotetseg_creeper] block (defaultmode=none, huntrange 24, hitpoints 100) and no _story/_hard blocks; the spawn's npc_setmode(none) stands in.
- ToB, Sotetseg, tob_sotetseg.rs2 ("not modelled"): the Hard maze is not divided; locs 41750-41753 and varbit 12269 are never placed (wiki_Theatre_of_Blood_Hard_Mode.wikitext:340, cache multiloc). Row sotetseg.av.hard_maze.parent_locs (A). Found by sotetseg.spec.json.
- ToB, Sotetseg (observation, no source gives the tick): death-ball impact sound 3947 is sent at cast+15 while the splat shows at cast+16 (ticks 94/95, 298/299). Found by sotetseg.spec.json.
- ToB, Xarpus: the death screech 3549 played twice. FIXED in OSRS-Content 26ba1bd604 (seam8 tob_xarpus_death_sound_and_cage): only seq 8063's frame sound.
- ToB, Verzik: Normal ids in Entry and Hard. FIXED in OSRS-Content 26ba1bd604 (seam8 tob_verzik_forms_and_presentation): the mode's records end to end (configs/tob_verzik.npc), Entry levels the records' own. Webs and pillars keep the Normal ids (no Entry web stat4 in the cache).
- ToB, Verzik: 8373 never worn. FIXED in OSRS-Content 26ba1bd604 (seam8).
- ToB, Verzik: the P3 crab special rode no attack. FIXED in OSRS-Content 26ba1bd604 (seam8): 14406 over a regular attack with its projectile. Whether the live client shows 14406 or the auto's seq is still the open D row.
- ToB, Verzik: the green ball played no animation. FIXED in OSRS-Content 26ba1bd604 (seam8): a regular ranged/magic auto seq (8125/8124, chosen at random: no source says which) with 1598, no second projectile.
- ToB, Verzik: sound 4000 sent twice. FIXED in OSRS-Content 26ba1bd604 (seam8): only seq 8126's frame 59 (in Hard about 12 ticks before the blast, as the cache places it).
- ToB, Verzik: the yellow pool graphic ended before the blast. FIXED in OSRS-Content 26ba1bd604 (seam8): copies cover the charge (Normal/Entry 445->459, Hard 445->465); the tick log carries 3 or 4 map_spotanim rows per pool.
- ToB, Verzik: 8128 sent twice. FIXED in OSRS-Content 26ba1bd604 (seam8): once, death +1.
- ToB, Verzik, yellows blast: 1596 is a spotanim on each raider; Near Reality sends a projectile from Verzik (line 341 above; one source, left). Row verzik.av.p3_yellows.gfx_blast (D). Found by verzik.spec.json.
- ToB, Verzik (observation, not a defect claim): sound 3291 is sent once per pillar struck, six rows on one tick per volley (spec_verzik_pil ticks 48, 51, 54, 57); whether the real client plays six at once is unsourced. Row verzik.av.p1_pillar_hit.sound (D). Found by verzik.spec.json.
- ToB, rewards: the per-player 1/8.7 unique. FIXED in OSRS-Content 26ba1bd604 (seam8 tob_rewards_and_completion): one team roll 10/91 Normal, 10/77 Hard, none in Entry.
- ToB, rewards: the unique weights. FIXED in OSRS-Content 26ba1bd604 (seam8): 8,2,2,2,2,2,1/19 Normal, 7,2,2,2,2,2,1/18 Hard.
- ToB, rewards: the 4-row common table. FIXED in OSRS-Content 26ba1bd604 (seam8): the wiki's 29 rows over 30 slots, three rolls.
- ToB, rewards: Entry paid the whole table. FIXED in OSRS-Content 26ba1bd604 (seam8): 20 percent (an Entry chest read Magic seed x1, Vial of blood x10 noted, Runite ore x13 noted).
- ToB, rewards: Hard's bonus unread. FIXED in OSRS-Content 26ba1bd604 (seam8): 115, 130 inside the overall-time target (additive reading disclosed).
- ToB, rewards: no tertiaries. FIXED in OSRS-Content 26ba1bd604 (seam8): elite clue 2/6/7 of 50, Lil' Zik 1/650 | 1/500 (held in the chest or the pack: no follower), Holy kit 1/100, Sanguine kit 1/150, dust 1/275 in Hard inside the target. Open: the Cabbage + Message deadweight (needs the M22 individual points).
- ToB, combat achievements: speed thresholds and the Hard Speed-Runners. FIXED in OSRS-Content 26ba1bd604 (seam8): 2600/2000/1750/1700/1500/1600/1425 strict less-than on challenge time, duo = a team of 2; 382-384 on overall time.
- ToB, combat achievements: 19 of the 46 listed tasks are awardable since seam8 (were 13; new 381-384, 396, 397). Open, each needing a hook: 240, 241, 242, 250, 251, 252, 253, 372-380, 385 (a Hard counter), 386-395 (the ten Entry mechanical tasks).
- ToB, completions: Entry raids counted. FIXED in OSRS-Content 26ba1bd604 (seam8): Normal and Hard only (Entry 0 -> 0).
- ToB, HUD: 6448 in tens. FIXED in OSRS-Content 26ba1bd604 (seam8 tob_hud_chat_lines_and_room_flow): a permille, 6449 = 1000 (blert HpVarbitTrackedNpc.java:51-53, ATR TobIDs.java:31).
- ToB, HUD: 6440 not 2 inside the raid. FIXED in OSRS-Content 26ba1bd604 (seam8): 2 from the first arrival; 6447/6448/6449 0 in an unstarted room. The lobby box (0/1) is still not drawn (tob_hud not mounted in Ver Sinhaza).
- ToB, chat: the recorders' lines absent. FIXED in OSRS-Content 26ba1bd604 (seam8): the entry, call-in, wave and completion lines to blert's regexes; the unsourced lines removed. Unproven with one client: the call-in line to lobby members and the wave line reaching a second raider.
- ToB, lobby: the Stranger had no shop. FIXED in OSRS-Content 26ba1bd604 (seam8 tob_lobby_stranger_and_supply_chest): the shop (75,000), the crystal's Teleport, the shroud tiers (through a test affordance until the quest talk offers them). Open rows under the seam7 lobby line above (Poll 83 call site, the quest talk).
- ToB, logout during a fight. FIXED in OSRS-Content 26ba1bd604 (seam8): a death (team deaths, orb 30), the cage or the wipe on return; the hallway resume kept.
- ToB, death: a caged raider hit. FIXED in OSRS-Content 26ba1bd604 (seam8, every room).
- ToB, supply chest: the points label read 0. FIXED in OSRS-Content 26ba1bd604 (seam8: varp 1746 declared transmit).
- ToB, supply chest: Hard paid the Normal bands. FIXED in OSRS-Content 26ba1bd604 (seam8): the band less 4 (deathless 6..9, wiki Hard Mode:5); the death bands' offset is an approximation.
- ENGINE, music: the lobby region track never unlocked. FIXED in the seam8 parent commit 0397a0b29 (seam8).
- ToB, music: jingle 250 to the clearing raider only. FIXED in OSRS-Content 26ba1bd604 (seam8): every raider once.
- ToB, vault: the strategy table loc 33014 and the spectators' war table 33011 are not driven (war table unreachable by design). The two unlock lines for 'The Curtain Closes' are one since the seam8 music fix.

## From seam8 (matthew-mbp-m4-raid-b1-seam8, 2026-10-03)

Fixed in OSRS-Content 26ba1bd604 and the seam8 parent commit 0397a0b29: the rows above marked so. Found and left open:

- ToB, Xarpus: a raider in the spectator cage is not released when the room is won (`cleared=1, jailed=1` for 30+ polls after the kill, sx8x_after; the tick log holds the raider on 6421,95 to tick 215). Likely cause (unverified): on the watchdog path `[proc,tob_watch_room]` (tob_raid.rs2) ends with `~tob_room_cleared` and does not re-arm, so `~tob_spectate_tick` never runs again; Maiden clears from her own death queue and is not affected. FIXED in OSRS-Content 24198b54ed (seam9: every room; the cage has its own per-tick queue `[queue,tob_jail_watch]`, armed in `~tob_death_after`).
- ToB, Xarpus: a spit's landing is a normal player queue on its target (`queue*(tob_xarpus_land_splat)`), so it waits while the target is dying (113 -> 119 measured); the real projectile does not wait. Not changed: no source says which queue. FIXED in OSRS-Content 24198b54ed (seam9: spit and orb landings are Xarpus's own npc queues 4/5 carrying their due tick; spit + floor(end/30), orb throw + floor(end/30)).
- ToB, Xarpus: M71 xarpus.p2.spit_landing stays grade E at measured 3 (blert's 2 is its splat tick minus its own asserted spit clock); the flight is tob.constant `^tob_xarpus_spit_proj_length`/`_step`.
- ToB, HUD/room flow: `~tob_restore` after a boss heals only the clearing raider, in every mode; the Entry Mode page :22 says hitpoints and prayer are restored after each boss in Entry, which would be the whole party and Entry only.
- ToB, room flow: a non-clearing raider's watchdog chain runs on into the next room and `~tob_start_room` arms a second, so per-tick rules run twice for them in a party (SS_OP_QUEUE does not deduplicate).
- ToB, HUD: after a boss dies the bar stays at its last push until the next room's arrival clears it; precise timing (varbit 11866) is not offered (m:ss only).
- ToB, chat: 'The fight begins: <room>', 'You return to the party still inside the Theatre.' and 'The chamber stirs back to life around you.' have no source and are kept (tests read the first).
- ToB, tob.constant: `^tob_hud_scale_div` and `^tob_hud_val_max` are unreferenced and its comment 'The bar carries HITPOINTS IN TENS' is stale; `^tob_verzik_p*_defence_level_entry` now equals each Entry record's base (used by the absorb restore).
- ToB, tob_selftest.rs2: `::tobvz` 'form' and `::tobmelee` compare Normal Verzik types only (form 0 / no boss in Entry and Hard).
- ToB, Verzik: the Entry Matomenos reads att/str 100 from the generated block where the Normal record reads 1 (it never attacks; unsourced).
- ToB, lobby: the supply chest loc_added at 6405,97 is not listed by `t.world.loc_near` from the fight tile 6439,95 even after 20 ticks (driver or scene window; goto 6406,97 first).
- Content, flavour: Lumbridge cows never say Moo (`[ai_timer,cow]` in cows.rs2 is never armed: lumbridge.npc [cow] has no timer= and no `[ai_spawn,cow]` calls npc_settimer; chickens and sheep look the same), and Al Kharid warriors never call for help on a hit (their `[ai_queue1]` does not fire).
- Content evidence: two seam8 regression runs without `--no-publish` rewrote OSRS-Content selftest/quests/quest_cook/play and quest_druid/play (113 PNGs renamed, 2 ledgers); the closer's restore was refused by the session's permission check, so the working tree still differs from HEAD there (not committed).
- Room tests to re-author: test/raids/tob_maiden.lua names tob_maiden_100..._30 (Entry spawns the _story records since seam5; with the four symbols it runs 64/64) and its row 20 passes a string slot to `t.ticklog.rows`; test/raids/tob_verzik.lua addresses the Normal symbols and ids (the id-mapped copy reaches 67/69, out of food in P2); tob_sotetseg's solo runner dies to three tornado hits in maze 1 (eat before the walk-back).

## From seam9 (matthew-mbp-m4-raid-b1-seam9, 2026-10-03)

- Canifis, canafis_citizen.rs2:29-35: a Canifis citizen (canafis_man1) never turns into a werewolf when hit. No npc_retype row and npc_id 2613 unchanged after three melee hits or several landed wind strikes, although `[ai_queue2,_canafis_citizen]` calls `npc_changetype(~canafis_werewolf_type, 500)` (seam9 scratches s9retype1, s9retype2). Not a raid row; quest-loop content.
- ENGINE, torirs_server_world.c `npc_spawn`: the record is memset and `death_seq_tick` is never set to -1 (only the respawn reset in torirs_server_combat.c does). The phase-cleanup stamp (`death_seq_tick < 0`) therefore never runs for an npc in its first life from `npc_spawn` (every script-spawned npc, all of ToB), and `ToriRSServer_WorldNpcFree`'s "death animation seen in full" hold reads 0 and never holds. Found by the seam9 selftest stanza, which checks `death_seq_sent` instead. Not fixed.
- ToB, Xarpus: since seam9 a spit or orb still in flight when Xarpus dies is dropped (the engine's death step clears the npc queue). The original comment said no splat can land after him; no source states either way.
- ToB, Xarpus: since seam9 orbs land on throw + floor(end/30) (they mostly landed a tick later on the old player queue). No Xarpus-specific source states the orb impact tick; ENCOUNTER_TIMING.md 1.2/1.4 is the rule used.
- ToB, room tests: `tob_maiden.lua` (committed) names `tob_maiden_100` where Entry spawns `tob_maiden_100_story`, and `tob_verzik.lua` (committed) names `verzik_initial` where Entry is `_story`; both predate seam5. The WIP files are the authors'.
