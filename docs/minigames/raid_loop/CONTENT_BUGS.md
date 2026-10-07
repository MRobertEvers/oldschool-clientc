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
- Tree-wide, magic damage type, other callers: powered staves (gear/powered_staff.rs2:506: trident, sanguinesti, shadow), specs/pvm_purging_staff.rs2:36, pvm_eye_of_ayak.rs2:56, pvm_wild_cave_accursed_charged.rs2, pvm_voidwaker.rs2, pvm_blessed_saradomin_sword.rs2, pvm_verzik_special_weapon.rs2 (Dawnbringer) call `~player_hit_npc_prepare` with the weapon's style: a powered staff is nulled by a Hagios. FIXED for the powered staves and three specials (raid seam33 powered_staff_damage_type): one shared entry `[proc,player_hit_npc_prepare_magic]` (player_hit_npc_prepare.rs2; the set-call-restore of the cast fix) called by the powered-staff auto (every powered staff: "a category of magic weapons that possess a built-in magic spell", wiki Powered staff :8), the Dawnbringer special ("[[Magic damage]] gear bonuses ... do not affect the damage inflicted by the Dawnbringer's special attack", wiki Dawnbringer :58), the purging staff special ("uses the best [[demonbane spell]]", wiki Purging staff :62), and the eye of Ayak special, which also read the weapon's row for everything else and now rolls magic accuracy x2 against magic defence for 130% of the staff's own max hit with Magic XP ("double the accuracy and a 30% higher maximum hit", wiki Eye of Ayak :82; :40 "powered staff"). Measured: a trident on a fresh Hagios 4 of 4 hits 0 before, 7 and 4 (killed) after; Dawnbringer and Ayak specials 0 before, 11 (killed) after; a whip still 0 (psd33_red / psd33_green); in the Entry room 3 of 3 magic nylocas hit by a trident, whip 6 of 6 rows 0 (psd33_room). Six kept Entry rooms byte-identical, _play_nylocas 5 of 5. Still open, files outside this seam: pvm_voidwaker.rs2:44 is MAGIC damage ("deals guaranteed Magic damage", wiki Voidwaker :55) -> `~player_hit_npc_prepare_magic`; pvm_wild_cave_accursed_charged.rs2:32-41 (accursed sceptre, a powered staff) rolls the weapon's row and 150% of the MELEE max hit -> magic accuracy x1.5, 150% of `~powered_staff_maxhit`, the magic entry ("a 50% increase to max hit and accuracy", wiki Accursed sceptre :80); pvm_blessed_saradomin_sword.rs2 is UNSOURCED for a style check: "a [[Magic]]-based attack ([[Magical melee]])" (wiki Saradomin's blessed sword :59) and magical melee "roll[s] for accuracy against your magic defensive bonuses but [is] fully protected against by the [[Protect from Melee]] prayer" (wiki Magical melee :2): no source says which colour of nylocas it hits, left as melee. player_magic.rs2:480/:508 can call the shared entry instead of the inline copy (behaviour-identical).
- death.rs2: "Oh dear, you are dead!" prints twice per death (`~combat_death_message` and `~respawn_message`).
- ToB, Verzik P1: `tob_verzik_p1_land` (tob_verzik.rs2, queued with player_uid only) writes `hit_player` npc_slot -1 and sets lethal against a 0-hp target; same fix as Maiden's (carry her uid, `npc_finduid`, skip 0-hp at launch, check hp and instance at landing).
- ENGINE, tick log: the HIT_NPC row has no dealer column (ToriRSServer_TicklogHitNpc, torirs_server_ticklog.c, pushes e=0 f=0), so a dying Matomenos absorbed into Maiden cannot be told from a player's killing hit. Proposal: carry the dealer pid, or -2 for an npc_damage from the npc's own frame.
- ToB, Maiden Entry pools: 10 + 2c carried from Normal, grade E [M121]; `^tob_maiden_pool_entry_divisor` = 1 discloses it.
- ToB, Maiden: the blood throw still aims a pool at a dead raider's tile (harmless, unsourced).
- ToB, Nylocas: protection prayers against wave nylocas: `tob_nylo_swing` never checked prayer. FIXED in OSRS-Content 2cddff56d5 (seam6): the matching protection prayer blocks the swing to 0 (wiki Protection prayers; styles wiki Entry Mode:157), 0 of 52 matched swings landed (s6np_waves_after6). The SPEC GAP stays open: the table still has no `nylocas.prayer_reduction` row (proposed under seam6 below).
- ToB, Nylocas Entry: Vasilias measured 9 then 10 per form and 2 attacks per form (the Normal figures) against `nylocas.vasilias_switch_entry` 15 / `vasilias_attacks_entry` 3-4 (grade D, one blert raid): no Entry switch interval is implemented. FIXED in OSRS-Content 0bc66408fc (seam10): windows 14 then 15, 3-4 attacks each (s10ny_vas4, 23 windows; seam.vasilias_entry_window_and_reflect).
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
- (**Heal FIXED seam13**, prayer restore still open: see "From seam13") ToB, supply chests: `tob_bandages` has no Heal script (the wiki: heals 20, restores prayer and run energy, super combat/ranging/stamina effects); the chest stays visually closed (the cache's chest state is per player, varbits 6460/6461); points and the death count are instance registers, so a party shares one balance where the wiki has per-player points.
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
  **FIXED 2026-10-07 content_bugs (engine):** `npc_spawn` sets `npc->death_seq_tick = -1` after its memset, the
  respawn reset's own value (torirs_server_combat.c "A new life: its death has not been shown yet"). Built and
  measured in a private worktree (scratchpad/eng, binaries torirs_cb / torirsserver_fix vs _base): server selftest
  18 failures before and after, the same lines; the svcplayentry Entry raid's 773 npc_free and every npc_death row
  are identical on both binaries (md5 of the tick/kind/type rows equal), and a Deviant-spectre probe with
  TORIRSSERVER_ANIM_LOST=1 prints no hold on either. So no ToB timing moves; the stamp and the hold are now armed for
  a first life as the code intends.

- ToB, Xarpus: since seam9 a spit or orb still in flight when Xarpus dies is dropped (the engine's death step clears the npc queue). The original comment said no splat can land after him; no source states either way.
- ToB, Xarpus: since seam9 orbs land on throw + floor(end/30) (they mostly landed a tick later on the old player queue). No Xarpus-specific source states the orb impact tick; ENCOUNTER_TIMING.md 1.2/1.4 is the rule used.
- ToB, room tests: `tob_maiden.lua` (committed) names `tob_maiden_100` where Entry spawns `tob_maiden_100_story`, and `tob_verzik.lua` (committed) names `verzik_initial` where Entry is `_story`; both predate seam5. The WIP files are the authors'.

## From seam10 (matthew-mbp-m4-raid-b1-seam10, 2026-10-03)

Fixed in OSRS-Content 0bc66408fc and the seam10 parent commit:

- ToB, Nylocas Entry: a support collapse rolled 1-50 in every mode (`~tob_nylo_pillar_fell`); the Entry Mode page :179 says "30+ damage". FIXED: Entry rolls 30..^tob_nylo_pillar_collapse_max 50 (measured 46, 41, 31, 36; was 3, 15, 27). The 50 ceiling is the Normal sentence (Strategies:726), so the Entry spread stays [M95].
- ToB, Nylocas: a big nylocas's detonation hunted 2 tiles from its south-west anchor (`huntall(npc_coord, 2)`), so a raider 2 tiles off its north or east edge was missed and one 2 off the south-west reached at 3 (Entry Mode page :176 "within two tiles"). FIXED: the hunt is widened by the body and `npc_range(coord) <= 2` decides (bigs at footprint 2 now hurt 6 and 8; a small at 3 misses).
- ToB, Nylocas: Vasilias NULLED a raider for good after one wrong-style hit (tob_damage.rs2, the waves' rule), so one misclick left a solo boss nobody could finish. Every source gives her only a reflect and a heal (wiki_Nylocas_Vasilias:82, Strategies:754, Jagex DSF:52). FIXED: she is never nulled; the reflect and heal stay (tick 105 heal 9 = reflect 9, 29 damaging hits after the probe, room completed).
- ToB, Verzik P2: a poisoned hit on the Athanatos sent her a 1588 and up to 70 on EVERY hit while it lived on and healed her every 5 ticks (vz10_glob_poison: seven hits, 25-60 each). wiki_Nylocas_Athanatos:54 "will instead burst"; Strategies:923 "To kill the Athanatos, it has to be hit with poison". FIXED: the poisoned hit kills it after its globule leaves (1590, 1588 and npc_death on one tick, no 1587 after).
- ToB, Verzik P3: `~tob_verzik_pool_near` drew a yellow pool within +-2 tiles of the raider with no walk test, so a raider near the north edge got a pool on the wall row z=99 (run 10: 6423,99 for a raider on 6423,98: 1596 + 1600 + the hit). The pools are there to be stood on (Entry Mode page :251; Strategies:968). FIXED: a `map_blocked` test (the Maiden pools' test); a blocked draw takes the next open tile of the 5x5 from the drawn offset, so no extra rolls are spent (four pools from the north-west corner, all reachable, all 1597).
- ToB, Xarpus: `~tob_xarpus_on_acid` charged standing damage on the 3x3 AROUND every puddle. The sources charge it on the puddle's own tile (Entry Mode page :207, wiki_Xarpus:257, Near-Reality Xarpus.kt processMechanics), and the 3x3 is the one-off landing hit; the melee step-back of Strategies:844 only works that way. Before the fix the wiki strategy died 26 ticks into phase 2 (60 of 119 damage from the melee tile beside a stepped-back puddle). FIXED: `~tob_xarpus_on_acid` = `~tob_xarpus_poisoned($at)` (the wiki strategy kills him under four names).
- ToB, tob.constant: `^tob_vasilias_switch_ticks_entry` = 10 was dead once Entry read `^tob_vasilias_entry_window_ticks` (tob_nylocas.constant). FIXED: removed (seam10 closer).

Found and left open:

- ToB, Nylocas Entry: the story-mode recoil cut ("Reduced maximum recoil damage from hitting the nylocas boss on the wrong style", New Modes newspost :38) has no figure; Entry reflects the full rolled hit capped at her hitpoints ([M97], grade E).
- ToB, Nylocas Hard: the Prinkipas is still nulled on a wrong-style hit like a wave nylocas; it changes colour too, but no source names its wrong-style rule.
- ToB, Nylocas Entry: Vasilias' prayed hits reach 17 (the Normal figure, Strategies:752) under the right protection prayer in magic and ranged forms; no Entry prayed figure exists.
- ToB, Xarpus: crossing a puddle in the middle of a 2-tile run step costs nothing (the ground sweep sees only the end-of-tick tile); the wiki says running over it hurts, and no source says how.
- ToB, Verzik P2: the poisoned hit's 1588 and its damage land on the same tick (picture only), and the Athanatos heal lands on its timer tick before the 1587 arrives (both already listed above, unchanged).
- ToB, Verzik room: `verzik.av.room.death_cage` (24 placements) cannot be counted from the client: `t.world.hazard_at` finds 32717 on 10 of the 24 map tiles and other wall locs on the rest. Needs a verb returning every copy of a loc, or the row measured from the map. Not content.
- Gear: `::give scythe_of_vitur` reports "has run out of charges and reverted to its uncharged form" early in the Xarpus room, yet it keeps landing three hitsplats a swing to the kill (the uncharged scythe still attacking may be a content gap for the item owner).
- Combat interface: `combat_interface:special_attack` pressed while idle leaves varp301 0 and the energy unchanged; pressed while engaged it arms and fires inside one tick. Nobody has found why; the minimap orb is the reliable path.
- Room tests to re-author (the authors', read-only to the seam pass): test/raids/tob_bloat.lua still carries the faulty Defence probe (the replacement block is in build/seam_state/matthew-mbp-m4-raid-b1-seam10/bloat_scratch/tob_bloat_copy.lua) and `spec.bloat.fly_first` reads 3 against 1 in the committed file today; tob_xarpus.lua's planner must mark only the puddle's own tile; tob_verzik.lua must bring poisoned ammo or a charged serpentine helm for the globule row; tob_nylocas.lua must keep supports standing (each Entry collapse is now 30-50).

## From the ToB rig pass matthew-mbp-m4-raid-b1-rig-tob (2026-10-03)

Source: docs/minigames/theater_of_blood/RIG_AUDIT.md, which gives the rig join per room under sources/rig/. All of these are open.

STRUCK (seam11): the premise below is wrong. The server loads every `*.generated.npc` first and every authored .npc second (torirs_server_content.c `load_npc_generated_config`, then `load_npc_authored_config`; cachepack ranks generated 1, authored 2), so an authored value always wins; torirs_server_content.c:2030-2041 is the per-header seed, not the file order. The Athanatos null held before any edit. Original text: ~~The server lays every `[gameval]` block over the same record in directory order (src/torirsserver/torirs_server_content.c:2030-2041). So npc_anims.generated.npc is applied after tob.npc, and an attack_anim set there reaches the game. Fix each row in the ledger and regenerate, or state the field in tob.npc and make sure the generated block no longer restates it.~~

Ledger faults, where an npc's attack is not an attack:
- **FIXED seam11** (tob_rig_ledger_faults: authored attack_anim null in tob.npc / tob_verzik.npc, all modes). ToB, Maiden: maiden_blood_slug (+_story, _hard) attack_anim is projectile_muspah_attack_ranged_01 (9944), named for another monster. The blood spawn's rig 1814 offers no attack, so it should be null (RIG_AUDIT L1).
- **FIXED seam11** (tob_rig_ledger_faults: authored attack_anim null in tob.npc / tob_verzik.npc, all modes). ToB, Maiden: maiden_elemental (+_story, _hard) attack_anim is elemental_spawn, the spawn sequence. It should be null (L2).
- **FIXED seam11** (tob_rig_ledger_faults: authored attack_anim null in tob.npc / tob_verzik.npc, all modes). ToB, Bloat: tob_bloat (+_story, _hard) attack_anim is tob_bloat_sleep 8082, the 33-tick down. Any generic attack path plays the whole down. It should be null (L3).
- **FIXED seam11** (tob_rig_ledger_faults: authored attack_anim null in tob.npc / tob_verzik.npc, all modes). ToB, Sotetseg: tob_sotetseg_creeper (+_story, _hard) attack_anim is tob_shadow_projectile_spawn, the spawn sequence. It should be null (L4).
- **FIXED seam11** (tob_rig_ledger_faults: authored attack_anim null in tob.npc / tob_verzik.npc, all modes). ToB, Verzik: tob_verzik_phase2_bloodnylocas (+_story, _hard) attack_anim is elemental_spawn. It should be null (L5).
- **FIXED seam11** (tob_rig_ledger_faults: authored attack_anim null in tob.npc / tob_verzik.npc, all modes). ToB, Verzik: tob_verzik_creeper (+_story, _hard) attack_anim is tob_shadow_projectile_spawn. It should be null (L6).
- **NOT A FAULT, seam11** (the authored null held in all three modes before any edit; nc_param, not npc_param, reads the cache table, see the seam11 engine row). ToB, Verzik: tob_verzik_phase2_armourednylocas, all three modes, attack_anim is tob_spider_tank_spawn. For the Normal record the ledger and tob.npc:2484 already say null, but the stale npc_anims.generated.npc:44992 overlays them. The _story and _hard ledgers still say spawn. Regenerate, and author _story and _hard null (L7).
- **FIXED seam11** (authored attack_anim and death_anim null, all three bats). ToB, Verzik: verzik_death_bat (+_story, _hard) attack_anim is verzik_phase3_death_b, the bat's only sequence (its ready animation). It should be null (L8).

Script fault:
- **FIXED seam11** (tob_sotetseg_maze_proc_and_creeper: no seq of his at the proc; row sotetseg.av.maze.boss_seq is 0, grade C). ToB, Sotetseg: tob_sotetseg.rs2:1020 plays `npc_anim(tob_sotetseg_shadow_portal)` on the boss. Seq 8142 is on framemap 1830, the exit loc 33037's animation, and the boss rig 1822 cannot play it. The spec row sotetseg.av.maze.boss_seq (encounters/sotetseg.tsv:79) claims it as his. Drop the call, or move it to the loc. No source names his own maze animation (RIG_AUDIT S1).

Unused rig animations that our content never plays. Each one is named for this exact npc. A recording or plugin constant should confirm the timing before playing it:
- **FIXED seam11** (~tob_maiden_spawn_set plays 8098; row maiden.av.crab_spawn.seq). ToB, Maiden: the Matomenos never plays elemental_spawn 8098 when it appears. encounters/maiden.tsv:100 says "no source binds them", and the Matomenos rig 1821 is that source (U1).
- **FIXED seam11** (9004 on spawn, 9005 then npc_del a tick later via ~tob_sote_tornado_leave; rows sotetseg.av.tornado.spawn_seq/despawn_seq). ToB, Sotetseg: the creeper never plays tob_shadow_projectile_spawn 9004 when it appears, nor _despawn 9005 when it leaves. It is removed with npc_del (tob_sotetseg.rs2:1594-1684), so its ledger death_anim never fires either (U7, U8).
- **FIXED seam11** (~tob_verzik_add_red plays 8098; row verzik.av.reds.spawn_seq). ToB, Verzik: the P2 Matomenos (8385) never plays elemental_spawn 8098 when it appears (U9).
- **FIXED seam11** (9004 on spawn, 9005 on the touch, removed next tick; row verzik.av.tornado.seqs). ToB, Verzik: the creeper (8386) never plays 9004 or 9005 (U10).
- ToB, Nylocas and Verzik: the ranged and magic nylocas never play their own melee swings, top_spider_ranged_meleeattack 8001 and top_spider_magic_meleeattack 7990. No source yet says when they would: perhaps against a pillar or an adjacent raider (U3, U4, U11).

## From seam11 (matthew-mbp-m4-raid-b1-seam11, 2026-10-03)

The rig pass rows above marked FIXED seam11 are closed. Open:

- ENGINE: `nc_param(type, param)` reads only the cache's param table (ToriRSServer_NpcParam), never the .npc overlay that `npc_param` reads first (torirs_server_ops_npc.c:266). `::tobnpcanim`'s cache_attack= reads `set` for the Athanatos, whose authored value is null. Any script that uses nc_param for an overlay param gets the wrong answer, e.g. quest_cog_food_trough.rs2:51 `nc_param(npc_type, death_anim)`.
- GENERATOR HAZARD: npc_anims.generated.npc, combat_stats.generated.npc and npc_combat/*.combat still name the eight seqs (harmless in game: authored wins). `gen_npc_combat.py --write` skips every npc with an authored block, so a regeneration would DROP the generated death_anim of tob_bloat, the Matomenos (8097), the blood spawn (8103) and others. Restate those in tob.npc, or run `--fix-authored`, before any `--write`. 54 slots change on the next write (build/seam_state/matthew-mbp-m4-raid-b1-seam11/rigledger/decide_diff.txt).
- **FIXED seam12** (centred spawn and steps, one column inward on rows touching column 0 or 13; moverestrict=passthru on tob_sotetseg_creeper/_story/_hard; sote12_before 0 npc_tile rows from start 13 and 12, sote12_after_a/b followed and hit from 13, 12, 1, 7). ToB, Sotetseg: the size-3 tornado is placed with its SW corner on the path tile, so its body sits north-east of the path, and it walks with player occupancy respected. It never moved when the path started at column 12 or 13 (sote11_copy maze 1: spawn 237 -> despawn 291, 0 npc_tile rows; sote11_a maze 2: spawn 229 -> free 260). Near-Reality centres it (RedStorm.transformCoord: tile -1,-1) and walks it with collision off (addWalkSteps(..., false)). Fix: centre spawn and steps on the path tile and give the creeper records moverestrict=passthru (tob.npc).
- **FIXED seam12** (settled by recordings: in the Blert pull, 62 raids, raiders praying Protect from Magic lost 0 hitpoints in 370 of 370 casts, so the hit is blocked as the wiki prose says; her heal off a lone prayed target was 11 or less in 73 of 78 clean readings, so the heal counts half the roll as Mod Ash's arithmetic says. Ours now counts floor(roll/2) for a prayed raider: vz12_kill_a max 8, vz12_kill_b max 8, close12_vz max 11. Spec row verzik.p2_heal_spell_fraction grade A -> B with the pull as its source.) ToB, Verzik: the blood spell under Protect from Magic deals 0 (`$dealt = 0`) and heals on the ROLL (tob_verzik.rs2 [proc,tob_verzik_blood_spell], ~2193; measured 8 prayed casts raw 0, heals +2..+17). Mod Ash (wiki_Theatre_of_Blood_Strategies.wikitext:930, grade A) says "up to 45 (halved by prayer) ... heals her for half of the total ... if they prayed, that'd be ~22, healing her for 11"; the wiki prose on the same line ("Protect from Magic will block the attack, but will still heal her for half the damage she would have dealt") is what the content follows. A source-ranking call for the next Verzik seam.
- ToB, Verzik: the enrage tornado's contact is now the tile beside the raider (npc_range 1; the engine keeps an npc off a player's tile). No source says whether the real tornado shares the tile; if it walks through players (moverestrict=passthru) contact would land one tick later. Needs an M number in the plan's open table.
- ToB, Bloat: the committed test/raids/tob_bloat.lua fails tech.protect_from_missiles on the current tree (125 fly hits under Protect from Missiles, largest 7 > 6, against the committed evidence's 57 hits, max 6). A private HEAD content root with no seam11 edit gives the same result, so the cause predates seam11. Look at the fly roll or the prayer timing.

## From seam12 (matthew-mbp-m4-raid-b1-seam12, 2026-10-03)

The two seam11 rows marked FIXED seam12 above are closed. Also closed:

- **FIXED seam12** ToB, Verzik: after a tornado heal lifted her back above 20 percent she attacked every 7 ticks again (`~tob_verzik_enraged` recomputed the threshold from her hitpoints; DRIVER_NOTES, rooms-tob eighth launch). The enrage now latches on bit 70. Sources: blert VerzikDataTracker.java:798-808 (one-way `enraged`), OpenOSRS VerzikHandler.java:415-417 (one-way `tornados`), and the Blert pull: 20 of 62 rooms went back above 20 percent after the enrage and every plain-attack gap there is 5 (84 of 84). Measured after: vz12_kill_a 9 and vz12_kill_b 12 gaps begun above 20 percent, all 5; close12_vz 9, all 5. Spec row verzik.p3_enrage_latch (C).
- **FIXED seam12** ToB, Verzik: P3 autos read the protection prayer on the attack tick, so a switch made on sight was a tick late. Now read when the projectile lands (`[queue,tob_verzik_p3_auto_land]`). Source: transcripts/yt_oGPT3sZMnd8.md:51 "You need prayer up as the projectile hits your character". Before: 5 of 11 switched autos over the Entry halved cap 10 (vz11_kill7/7b); after: 0 (vz12_kill_a max 8, vz12_kill_b max 8, close12_vz max 9). Spec row verzik.p3_prayer_read (D).
- **FIXED seam12** ToB, Verzik: the green ball's pose was 8124 or 8125 at random. Now 8125 always (OpenOSRS VerzikHandler.java:456-477 reads isGreenBall only under ANIMATION_ID_P3_RANGE). Spec row verzik.av.p3_ball.seq 8124,8125 -> 8125.

Open:

- ToB, Sotetseg: one column short of Near-Reality's centre on a maze row whose path touches column 0 or 13. Near-Reality walks the storm with no collision (`addWalkSteps(..., false)`); this engine's npc walk always checks walls, so the body sits one column inward there. The path tile is still under the body and the hunt still looks at it. An engine verb for a wall-ignoring npc walk would close it.
- ToB, Sotetseg: the arena (overworld) tornado is unmeasured since the centring: it chases the arena team, which a solo run cannot have. Same procs as the realm one.
- ToB, Verzik: a P3 auto now splats one tick after its prayer read (the landing queue queues combat_damage_player 0), so hit_player for an auto is about T+4 rather than T+3. verzik.p3_proj_flight is grade E [M80]; no source states the cycles.
- ToB, Verzik: the blood-spell heal lands on the cast tick and reads the prayer at the cast; Blert's hitpoint stream shows the heal at T+2 (health-bar latency unknown). Worth an [Mn] if measured.
- ToB, Verzik: tob.constant's comment on `^tob_verzik_p2_heal_spell_max` still describes the Mod Ash reading (comment only; the behaviour is in tob_verzik.rs2).

## From seam13 (matthew-mbp-m4-raid-b1-seam13, 2026-10-04)

- **FIXED seam13** ToB, Entry bandages: `tob_bandages` showed `ifop1=Heal` with nothing bound to
  it, so every press said "Nothing interesting happens." `[opheld1,tob_bandages]` in
  tob_spectate.rs2 now heals 20, boosts Attack/Strength/Defence by floor(L*15/100)+4, Ranged by
  floor(L/10)+4 and Magic by 4, cures poison and applies a stamina dose, as food. Source: the
  wiki item page Bandages (Theatre of Blood), pinned at
  build/seam_state/matthew-mbp-m4-raid-b1-seam13/s13b/wiki_Bandages_Theatre_of_Blood.wikitext
  (lines 23-33). Conformance row seam.tob_entry_bandages_heal: 99 to 117, 112 and 103, stamina 1.
- **FIXED seam13** ToB, Verzik P2: the tree did not model insulated boots, so a solo raider took
  the full lightning roll (42, 43 and 34 in s13b_full_a). `slayer_boots` worn now cut a zap that
  does not pass through her to 60 percent. Source: wiki Insulated boots :26, pinned in the same
  dir. The Entry page :94 calls them "a mandatory requirement if doing it in solo".
- Open, ToB, bandages: the prayer restore is not modelled. The item page says only "a variable
  amount ... more research is needed", so it gives no figure to use. [M] row.
- Open, ToB, bandages: the item page says Magic gets a static +4 (the code uses this). The Entry
  Mode page says Magic scales like a ranging potion. The two wiki pages disagree.
- Open, ToB, Verzik: tob.constant's comment on `^tob_verzik_p2_zap_max` still says "25 with
  insulated boots". The item page says 40 percent off, and tob_verzik.rs2 applies 60/100 as a
  literal. The comment is stale, and the literal could become a named constant. The P2 lightning
  max of 48 is Normal's figure, because no source gives one for Entry.
- Open, ToB, Nylocas (suspected, NOT measured): the Entry Mode page :164 says "Frozen nylocas
  cannot attack the pillars until unfrozen". tob_nylocas.rs2 has no freeze reference, so an Ice
  Burst would not stop a chewer. This is what keeps the Ancients strategy from working.
- Open, ToB, supply chests: an Entry chest hands over min(10, free slots) and then answers "The
  chest is empty." (tob_chest.rs2:159). No source says whether a raider may come back for the
  rest after freeing slots.
  **CLOSED 2026-10-07 content_bugs (content already does it):** tob_chest.rs2 `[oploc1,tob_midway_chest_closed]` keeps
  `left = ^tob_entry_bandages - taken` per player (`%varp6854/6855_tob_supply_*`), so a raider who took 6 of 10 for
  want of slots gets the other 4 on the next Open; "The chest is empty." only once all 10 are taken.

## From seam14 (matthew-mbp-m4-raid-b1-seam14, 2026-10-04)

- **FIXED seam14** ToB, Nylocas: a wave nylocas's swing dealt add(1, random(max)), so every swing
  landed for 1..max whatever the raider wore; Vasilias did the same (16 of 16 prayed magic and
  ranged swings landed). `~tob_nylo_hit_roll` in tob_nylocas.rs2 now rolls the npc's attack roll
  against the player's defence roll (stab, ranged or magic by colour), and only a roll that beats
  it deals 0..max; `~tob_vasilias_damage` uses it. Sources: LostCity npc_combat_melee.rs2:27-28
  ("randominc($attack_roll) > randominc($defence_roll)" then "randominc($maxhit)"); the cache
  records (cache_npc_nylocas.txt: Entry small 80/80/1 +505, big 100/100/6 +400, Vasilias Entry
  160/140/20 +380) and the wiki infoboxes. Room damage for a driven Entry solo fell from 545-969
  to 361-441 (scratch) and 279-392 (relay first half). Spec rows nylocas.swing_miss_entry and
  nylocas.vasilias_prayed_miss_entry (C).
- **FIXED seam14** ToB, Nylocas Entry: frozen nylocas went on chewing the supports. Entry Mode
  page :166: "Frozen nylocas cannot attack the pillars until unfrozen, even if they are in melee
  range". `~tob_nylo_pillar_tick` now returns while npc_frozen > 0, in Entry only. Spec row
  nylocas.frozen_bites_entry (D); conformance seam.nylocas_frozen_no_bite_entry.
- Open, ToB, Nylocas Normal/Hard freeze: Strategies:719 says only "Frozen nylocas will usually
  stop attacking a pillar for a short time", which is not a rule, so Normal and Hard frozen
  nylocas still bite.
- Open, ToB, Vasilias Entry: her prayed magic/ranged max is still the Normal 17 (no Entry
  figure exists); the accuracy roll now gates it.
- Open, ToB, supply chests (tob_chest.rs2:122-169): an Entry chest gives min(10, free slots) and
  then says "The chest is empty." The item page ("The chest can only contain a maximum of 10
  bandages, so uncollected bandages are deleted") and Entry Mode :7 ("Leftover bandages in the
  supply chest do not carry over from each boss") say the leftovers stay until the next chest.
  Patch proposed at build/seam_state/matthew-mbp-m4-raid-b1-seam14/verz/chest_proposal/ (not in
  the seam's files, not applied, not compiled).
  **CLOSED 2026-10-07 content_bugs:** the leftovers already stay in the same chest for that raider (row above); they
  are not carried to the second chest (each chest has its own `%varp685x_tob_supply_*`), as the pages say.
- Open, ToB, bandages: no prayer restore (tob_spectate.rs2). Entry Mode :7/:35 say the bandage
  acts as a prayer potion; :151 says it "slightly restores Prayer"; the item page says "more
  research is needed". A prayer-potion restore would be grade E at best: the owner decides. The
  Magic boost is SETTLED: the 9 June 2021 hotfix "Reduced the amount that the story mode bandage
  boosts your magic level by" supports the item page's static +4, which the code uses.
- Open, ToB, Verzik Entry figures: the P2 lightning (48; 28 with boots), blood spell (45), crab
  explosion, Athanatos landing, yellow blast and web snap all roll the Normal values, because no
  source gives an Entry figure. The Entry page :228 says the lightning "can largely be ignored in
  a solo raid" with insulated boots, yet it dealt 110-146 per P2. The tornado heal (Normal 3x,
  Entry "a percentage", :256) has no Entry figure either.
- Open, ToB, Verzik: her pillars spawn as the Normal records 8379/8377/8378, not Entry
  10840/10838/10839, in every run (npc_retype rows); hp is set to 200 by script. Presentation only.
  Re-checked 2026-10-07 content_bugs, NOT CHANGED: all.npc `verzik_story_pillar_npc` and `verzik_pillar_npc` are
  byte-identical apart from stat4 (md5 of the remaining fields equal; the same for the collapsing forms), and the
  script sets the Entry 200; the only observable difference is the npc id a client sees. Swapping the record by
  mode means per-mode `[ai_queue1,...]` triggers and five `npc_find` sites in tob_verzik.rs2: left OPEN (presentation
  only, low value).
- Closed as stale, ToB, Verzik: tob.constant's "25 with insulated boots" comment is the
  Strategies page's figure, not stale; the 60 percent is now ^tob_verzik_p2_zap_boots_pct with
  both quotes (behaviour unchanged).

## From seam15 (matthew-mbp-m4-raid-b1-seam15, 2026-10-04)

- **FIXED seam15** Potions, super restore: `[proc,super_restore_effect]` (prayer_potion.rs2)
  healed Hitpoints 8 + 25% of base, 32 a dose at 99, through the super restore, its br_ supply
  copy (br_potion.rs2:74) and the Castlewars brew. Wiki [Super restore] oldid 15183989 line 53:
  "restores all player stats that have been lowered, including Prayer, but not Hitpoints". The
  Hitpoints line is gone. Conformance seam.super_restore_no_hitpoints.
- **FIXED seam15** ToB, Verzik: her tornado outlived her and touched a raider 17 ticks after
  her npc_death (rooms-tob launch 12, tick 841). Now no contact while she lies dying, every
  tornado fades on the bat's tick, and no respawn after her last hitpoint (blert
  VerzikDataTracker.java:446-456; Near Reality PurpleTornado.kt:34-37,54). Spec rows
  verzik.p3_tornado_end (D), verzik.p3_inflight_after_death (B). Conformance
  seam.verzik_tornado_gone_with_her.
- **FIXED seam15** ToB, supply chests: an Entry chest now keeps what a full backpack could not
  take, until ten are taken (Entry Mode:7; Bandages (Theatre of Blood)). Conformance
  seam.tob_entry_chest_keeps_leftovers.
- **FIXED seam15 (engine)** `::give dragon_dagger_p++` gave dragon_dagger_p:
  cheat_id_from_name (torirs_server_world.c) underscored the argument before the exact gameval
  lookup. Conformance seam.give_takes_the_exact_symbol.
- Not a bug, sourced: Verzik's in-flight auto still lands after her npc_death (15 of 62 Blert
  P3 rooms, 3-6 ticks after her last launch). Near Reality's blockIncomingHits(15) is not OSRS.
- Open, potions, Sanfew serum: restores only Attack, Strength, Defence, Ranged and Magic at
  4 + 30% (restore_potion.rs2:79-83, and the br copy at br_potion.rs2:93-97). Wiki [Sanfew
  serum] oldid 15236706 line 53: "restores 4 + 30% of the player's base level (rounded down) per
  dose in all skills except Hitpoints". Both copies should change together.
  **FIXED 2026-10-07 content_bugs (OSRS-Content 3bd2334365):** `~sanfew_serum_restore` (prayer_potion.rs2), the
  super restore's list at 4 + 30%, called by both copies; the page is pinned at sources/wiki_Sanfew_serum.wikitext:53.
  Probe cb_potions1: agility 59 -> 92, prayer 39 -> 72.
- Open, potions, Super restore mix (barbarian_mix.rs2 `[proc,brutal_mix_restore_all]`):
  restores only the five combat stats, not Prayer or the other skills. Wiki [Super restore]
  line 61: the mix is a super restore with caviar, healing 6 Hitpoints a dose (the 6 is there).
  **FIXED 2026-10-07 content_bugs (3bd2334365):** `[proc,brutal_mix_restore_all]` is `~super_restore_effect` (Prayer and
  every other skill but Hitpoints; the unsourced poison immunity is gone). wiki_Super_restore.wikitext:61. Probe
  cb_potions1: prayer 0 -> 34.
- Open, potions: a super restore drunk with a Prayer cape or ring of the gods (i) worn, or a
  holy wrench carried, should restore Prayer by 8 + 27% (wiki [Super restore] line 57). Not
  implemented.
  **FIXED 2026-10-07 content_bugs (3bd2334365):** `~prayer_restore_pct` (prayer_potion.rs2) adds 2 to the Prayer percent
  of the prayer potion, the super restore (and through it the br_ copy, the Castlewars brew and the restore mix)
  and the brutal prayer mix when a ring of the gods (i) or a Prayer / max cape is worn, or a holy wrench / Prayer or
  max cape is carried: wiki_Prayer_potion.wikitext:90 (fetched by name; the longer list) and wiki_Super_restore
  .wikitext:57. Probe cb_potions1: super restore with a wrench, prayer 0 -> 34 (8 + floor(99 x 27/100)).
- Open, ToB, configs/tob.constant: `^tob_verzik_nvar_expire` (register 2) is documented as
  "crab / web: the tick it dies"; seam15's tornado fade also keeps the tick its despawn began
  there. Suggested comment: "crab / web: the tick it dies; tornado: the tick its despawn began".
  Documentation only.
  **FIXED 2026-10-07 content_bugs (3bd2334365):** tob.constant:2620 now says so.

## From seam16 (matthew-mbp-m4-raid-b1-seam16, 2026-10-04)

- **FIXED seam16** ToB, Bloat: the stomp hit a raider out of Bloat's sight. `~tob_bloat_stomp`
  was `huntall` over 6 tiles with no sight test; s15k1 ticks 438 and 510 stomped a raider
  standing behind the tank for 26 and 33. Entry Mode :134 "This can be avoided by moving out
  of his line of sight" and :145 "Run away and out of his line of sight to avoid it" (grade C
  with transcripts yt_B_gjVdmfOrY.md:69 and yt_KF9y2GYTJ-A.md:125). The stomp now also asks
  `~tob_bloat_sees`, the flies' test. The 6-tile reach stays [M65]. Conformance
  seam.bloat_stomp_needs_sight.
- **FIXED seam16** ToB, Bloat: a falling-flesh tile rolled twice in one volley was drawn twice
  and landed twice (s15k1 tick 681: 22 + 22; 15 of 32 volleys repeated a tile).
  blert_api/bloat_events.csv (B): 16 hand graphics in 740 of 1 050 recorded drops, 15 in 271,
  14 in 39. `~tob_bloat_hand_repeat` skips the repeat where it is drawn and where it lands;
  every shadow is still rolled, so the room's roll stream does not move. Spec bloat.hand_tiles
  is now 14-16 range.
- **FIXED seam16** ToB, Verzik P3: her ranged, magic and melee attacks never missed. Each dealt
  `add(1, random(max))` with no roll: 28 of 29 landed in s15k1, and the HEAD scratch landed
  16 of 16 autos and 17 of 17 melee unprayed. Sources: cache verzik_phase3_story
  (cache_npc_verzik.txt:537, Ranged and Magic 180, +20 ranged and magic attack, A);
  Strategies:942 "As her attacks are very accurate"; Entry Mode :243 "damage is calculated
  upon impact" and :245 "a crush based melee attack"; LostCity npc_combat_melee.rs2:27-28 for
  the roll itself (C). The autos are now rolled at the landing, the melee against crush.
  Spec verzik.p3_auto_miss_entry (C). Conformance seam.verzik_p3_attacks_roll_accuracy.
- Not a bug, sourced: Maiden's blackstorm has no accuracy roll. Strategies:590 "The attack
  always lands as a [[successful hit]] and deals damage equal to 36.5 + (3.5 * c) ... This can
  be halved by activating [[Protect from Magic]]". A prayed Entry hit above 9 is the leak term
  c, not a failed prayer (61 of 61 hits matched at their launch-tick c). Entry Mode :111 says
  only "Magic; preventable with [[Prayer]]"; that is grade D against grade D, so the halving
  stays. Conformance seam.maiden_blackstorm_always_lands_entry; spec maiden.auto_land_rate_entry
  and maiden.auto_prayed_entry (D).
- Open (grade D, one source), potions, Saradomin brew: `sara_brew.rs2:30`
  `stat_drain(defence, 2, 10)` lowers Defence by 10% + 2 per dose. Wiki Saradomin_brew :56
  says a dose "raises [[Hitpoints]] by 15% + 2 and [[Defence]] by 20% + 2 of their base
  levels", and :89 gives "floor((Defence Level) * 1/5) + 2" (pinned at
  build/seam_state/matthew-mbp-m4-raid-b1-seam16/trj/wiki_Saradomin_brew.wikitext; copy it to
  sources/ with a manifest row). Every brew a raider drinks makes each accuracy-rolled hit
  land more often. Engine-wide: the quest suite must be re-run after the fix. Found by the
  tob_relay_wiki_kit fixer.
  **Already FIXED (seam36, checked 2026-10-07):** sara_brew.rs2:52 `stat_boost(defence, 2, 20)`.
- Open, ToB, Xarpus [M70]: the poison buff counts an exhumed as fully absorbed when one orb
  reaches him. s15k1 leaked 1-4 orbs from 6 of 7 exhumeds, giving +85% and Entry poison hits of
  4-6. Mod Kieren's "buffed by a percentage based upon how many exhumed absorbs you missed"
  (wiki_Xarpus.wikitext:257) may count orbs (16 of 56 = 29% here). No source settles it; left E.
- Open, ToB, Verzik P2: her stomp on a raider under her and the urnbomb still deal
  `add(1, random(max))` with no roll. The bomb is a tile AoE ("avoided by standing one tile
  away", Entry Mode :226), so no roll is plausible there; the stomp has no source either way.
- Open, ToB, Verzik green ball in Entry: 74 (75% of the Hitpoints level, the Normal figure).
  The Entry page gives no figure ("be prepared to take unavoidable damage", :250).
- Open, ToB, configs/tob.constant line citations in the spec tables have drifted (bloat.tsv
  cited :896 for `^tob_bloat_stomp_range`, which was at :949 before seam16 and is at :960
  after it). seam16 re-pointed that one row. Its maiden comments add 11 lines to
  tob.constant between lines 240 and 285, so every other spec citation past line 240 is off by
  5-11 more lines; they were not re-pointed. Documentation only.

## From seam18 (matthew-mbp-m4-raid-b1-seam18, 2026-10-04)

- **FIXED seam18** Potions, Saradomin brew: `[label,consume_effect_sara_brew]`
  (player/scripts/consumption/sara_brew.rs2) ran `stat_drain(defence, 2, 10)`, so every dose
  LOWERED Defence 2 + 10% (99 -> 88) and every accuracy-rolled hit on a brewing raider landed
  more often. Now `stat_boost(defence, 2, 20)`. Wiki [Saradomin brew] revid 15322175
  (2026-08-27), pinned at docs/minigames/theater_of_blood/sources/wiki_Saradomin_brew.wikitext
  with a manifest row: line 56 "Each dose of a Saradomin brew temporarily lowers Strength,
  Attack, Magic, and Ranged by 10% + 2 of their current levels, and raises Hitpoints by 15% + 2
  and Defence by 20% + 2 of their base levels, all rounded down. This can boost the player's
  Hitpoints and Defence above their base level by up to the amount restored."; line 89 "Defence
  boost is calculated with: floor((Defence Level) * 1/5) + 2"; table line 109 "95–99 || +21".
  The engine's `stat_boost` step is `constant + base * percent / 100` and its result
  `max(min(current + d, base + d), current)` (torirs_server_scripts.c, SS_OP_STAT_BOOST), so
  (2, 20) is the wiki's formula of the base level, rounded down, and the "up to the amount
  restored" cap: a second dose at 120 leaves 120. **Grade C at best**: one wiki item page,
  whose prose (:56), formula (:89) and table (:109) agree with each other and with our server;
  no recorder or plugin source. Measured: Defence 99 -> 120, Strength 99 -> 88, Hitpoints
  99 -> 115 (the wiki's table: +16 at 94-99), read the next tick (run sbd18_green); RED on the
  HEAD pack: Defence 99 -> 88 (sbd18_red). Conformance seam.brew_raises_defence and
  seam.brew_defence_no_stack. The other brews do not route through this label and carry no
  copy of it: god_brew.rs2 (Zamorak, ancient, forgotten, Armadyl brews) and barbarian_mix.rs2
  (Zamorak mix, ancient mix) keep their own Defence drains (their headers cite their own wiki
  pages; those pages are not pinned and were not re-checked here); castlewars_brew.rs2 boosts
  Defence 5 + 15% (super combat's arm, wiki_Castlewars_brew.wikitext); no Blighted or Sanfew
  path in the consumption directory touches a brew.
- Open, potions, Saradomin brew br_ supply copy: br_potion.rs2:76-83 (`br_*dosepotionofsaradomin`,
  the Tombs of Amascut supply brew; the file's header says it mirrors the ordinary families
  "constant for constant") still runs `stat_drain(defence, 2, 10)`. The same one-line fix,
  `stat_boost(defence, 2, 20)` in place of that drain, was outside seam18's file list.
  **Already FIXED (seam36, checked 2026-10-07):** br_potion.rs2:85 `stat_boost(defence, 2, 20)`.
- Open, potions, Saradomin brew drains, of the base or the current level: line 56 and line 114
  ("Attack/Strength/Ranged/Magic drain is calculated with: floor((Current Stat Level) * 1/10) +
  2") say the drain is of the current level; `stat_drain(x, 2, 10)` steps by the base level.
  The same page's line 58 ("At skill levels 90 and higher, a super restore will only fully
  restore lost stats from two doses of Saradomin brew") holds only for a base-level step: of
  the current level, three doses at 99 drain 11 + 10 + 9 = 30, which a super restore's 32
  covers. The two agree on the first dose from an undrained stat. The page disagrees with
  itself, so the drain was left as it was; a second source (a plugin's brew calculator, a
  Jagex statement, or a measured three-dose drain) would settle it.

## From seam17 (matthew-mbp-m4-raid-b1-seam17, 2026-10-04)

- **FIXED seam19** (below) ToB, every room, tob_hud.rs2 `[proc,tob_hud_orbs]` and tob_raid.rs2:1572-1573
  `[proc,tob_arm_watchdog]` (`queue(tob_room_watchdog, 0, 0)`, reached from tob_party.rs2:697
  `~tob_start_room`): a raider's copies of the party orb varbits (6442..6446,
  `varb644N_tob_client_pK`) are written only when `~tob_hud_orbs` runs FOR THAT RAIDER, on
  its own arrival (`[queue,tob_room_settle]`) and in the fight watchdog, which is queued only
  on the raider who crossed the barrier. So a member never sees the orbs of raiders who
  arrived after it. Measured in the three-client smoke (test/raids/_party_smoke.lua, closer
  run on the final tree), Normal, Maiden's fight running: the leader reads
  `p0=27 p1=27 p2=27`, member p2 reads `p0=27 p1=27 p2=0` (`p2:raid.orbs_member_view_observed`).
  Expected: every raider's HUD shows every party member's orb (raidwide.tsv
  `raidwide.hud.orb_full`, "party orb varbit (6442..6446) for a raider at full hitpoints",
  tob.constant:3429). The real game's refresh cadence for those orbs is not pinned: find a
  source before choosing a fix (every raider's watchdog, or a broadcast from `~tob_hud_orbs`).
  Found by seam17 party_run_and_verbs. The smoke grades the leader's three orbs and each
  raider's own orb until it is fixed.

## From seam19 (matthew-mbp-m4-raid-b1-seam19, 2026-10-04)

- **FIXED seam19** ToB, every room: a raider's orb column showed only the raiders who arrived
  before it, and nobody saw a member's hitpoints move in a fight (seam17's row above). Source:
  the orbs are the whole party's hitpoints on every raider's screen, one order for the team
  (wiki Theatre of Blood/Strategies, sources/wiki_Theatre_of_Blood_Strategies.wikitext:578,
  "the order of the health orbs that are present on the top left of the player's screen, from
  top to bottom", with one orb order per scale for the whole team), drawn by the cache from
  each client's varbits 6442..6446 (raidwide.tsv `raidwide.hud.orb_full`, A). The refresh
  cadence is still not pinned, so the fix keeps the one this tree already had and widens who
  it reaches: `~tob_hud_orbs` now also runs `~tob_hud_orbs_party`, which, for a party of two
  or more, makes every other raider who is in this raid (`%varp5893_tob_active` 1 and the same
  handle) publish their own slot and then rewrite their six varbits, so the starter's per-tick
  watchdog and every arrival refresh the whole party. A party of one returns before any
  `p_finduid` (one-client ledgers byte-identical: cooks_assistant, druid; the six Entry rooms'
  counts unchanged). Not chosen: arming every raider's watchdog, which would also count every
  raider into `^tob_var_boss_misses` each tick (the boss-death rule). Measured (scratch
  bring_along/scratch_tobjoin.lua, run s19join_after3, Normal Bloat, three raiders): at the
  entrance every raider reads p0=27 p1=27 p2=27; with the fight running and only the leader
  hit by the flies, all three read p0=10 p1=27 p2=27 (before the fix the members read p0=27,
  stale, while the leader read p0=6). `_party_smoke` 99/99: the members'
  `raid.orbs_member_view_observed` reads "p0=27 p1=27 p2=27 (all full)" (before: p2=0).
- **FIXED seam19** ToB, every room: a raider who joined a party already inside
  (`~tob_join_raid`, the door's second entrant) had the orb varbits and no orb column on screen.
  The Theatre HUD is mounted by `~tob_title_card` (tob_title.rs2, whose header lists "a joiner
  stepping into a party through `~tob_join_raid`" among the entries it serves) and by
  `~tob_hud_open` at the barrier, for the raider who crosses it; every other way into a room
  (the build, `~tob_carry_party`'s "Everyone gets the card", the resume) gives the card, the
  join did not. `~tob_join_raid` now gives the room's card after `~tob_room_arrive`. Measured
  in s19join_after3: the three raiders' screens at Bloat's entrance each show three full orbs,
  each with its own orb marked (before: the members' screens had none).
- FIXED seam22 (see "From seam22" below). Was Open, ToB, every room: the fight watchdog (`[queue,tob_room_watchdog]`, the per-player,
  per-tick hook) is armed only for the raider who crosses an unstarted barrier
  (`~tob_start_room`), on a resume and on a re-entry; a teammate who crosses a started barrier is
  only stepped (`[oploc1,tob_arena_barrier]`, `~tob_barrier_step`), and `~tob_carry_party`
  arms nobody. So the per-player rules that hook carries run for ONE raider of a party:
  Maiden's blood (`~tob_maiden_blood_sweep`, "will do damage to the player every tick"),
  Sotetseg's maze tick (`~tob_sote_player_tick`), the cleared room's exit walk. tob_raid.rs2's
  own comment says it "is re-armed every tick for every player who crossed the barrier". Not
  fixed here: arming it per raider also multiplies `^tob_var_boss_misses` (one instance register
  counted into by every watchdog) and so shortens the two-consecutive-tick death confirmation
  to one tick for three raiders; the fix has to split the per-player rules from the per-room
  ones. Found by seam19 tob_party_room_bring_along (code read, not yet measured in a party
  room); a Normal Maiden party test will show it as a member standing in blood untouched.
- Note, debug: there are two member joins. `::tobjoinroom [0|1|2]` (tob.rs2, seam19) is the
  party room test's: reset, the door's join and `~tob_party_join`, kit, the room line, and the
  door's and the join's refusals. `::tobjoin` (tob_selftest.rs2:2755) is the C selftest's
  two-raider harness (torirs_server_world_selftest.c:34043, parses "tobjoin <handle>"): Normal
  only, arguments ignored, no "already inside" refusal (typed twice it joins the party twice).
  The seam19 triage asked for the new one under `::tobjoin`; that name was taken in a file this
  seam does not own. Folding them is: delete the selftest's, rename `::tobjoinroom` to
  `::tobjoin` and have it also print "tobjoin <handle>" for the C harness.
- Open, ToB, every room: the room's music reaches only the raider who built the room or
  started the fight. `~tob_music_room` runs in `~tob_build_room` and `~tob_music_fight` in
  `~tob_start_room`, each a per-player `~tob_music_play` on the active raider; `~tob_join_raid`
  and `~tob_carry_party` play nothing. Measured in seam19's `_party_smoke` tick log: the leader's
  `music 0 570` at tick 98 on entry, no music row for the two members who joined at tick 111.
  Found by seam19 tob_party_room_bring_along (tick log read; not fixed: the music pages'
  "upon entering" sentences were not re-read for a party).

## Owner rulings (2026-10-04)

- Tree-wide, protection prayers: the DEFAULT is that an npc attack reads the target's protection
  prayer on the attack's animation tick, when the projectile is SENT ("Jad prayer protection is
  checked on the animation. In fact MOST things in OSRS ARE. It is the EXCEPTION that damage is
  calculated on it."). A landing-tick read needs a pinned source naming that npc. Never grade a
  send-tick read as a defect; never move a read to the landing tick without the source.
- ToB, Verzik P3: RULED the exception, and SOURCED (grade B) the same day from Blert: of the P3 autos aimed at the recorder in the Normal streams, the 28 magic autos thrown with Protect from Magic OFF and lit by the landing (T+3) hit at most 16 (mean 8.2), the prayed profile, against max 33 (mean 20.6) when off throughout, and the 2 thrown with it ON and dropped before the landing hit 20 and 18 (sources/blert_api/spec_pass_verzik/seam20_p3_prayer_read.txt). "You are correct about p3 Verzik on hit" (owner,
  2026-10-04): her P3 ranged and magic autos read the prayer when the projectile hits
  (`[queue,tob_verzik_p3_auto_land]`, seam12; Entry Mode :243 "damage is calculated upon impact",
  transcripts/yt_oGPT3sZMnd8.md:51). The seam12 conformance row, verzik.tsv `p3_prayer_read` and
  the room test's landing-tick technique stand. Sotetseg's ball and the P2 urn bombs are NOT
  covered by this ruling: seam20 (SEAM_TRIAGE_2026-10-04h.md) sources them or moves them.

## From seam20 (matthew-mbp-m4-raid-b1-seam20, 2026-10-04): when a protection prayer is read

The owner's default (above) is the rule this pass applied: a landing-tick read stays only
where a pinned source names that npc. Both reads the triage named have one, so NEITHER MOVED;
no behaviour changed (comments, spec rows and two conformance rows only), and no room test
measures anything different. The Verzik P3 ruling row above is present and unchanged.

- Sotetseg's ball, `[queue,tob_sote_impact]`: SOURCED AT THE LANDING (spec row
  `sotetseg.ball_prayer_read_tick`, grade B). No wiki or Strategies line gives the tick
  (wiki_Sotetseg.wikitext:92 and Strategies :787-:821 name the block, the split "upon
  striking a player" and the 3-second lock only). The recorder does: blert's stage-13
  streams (build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_sote_raw, 21 Normal and
  Hard rooms) carry the PRIMARY raider's `prayerSet` and `hitpoints` on every tick
  (sources/blert_repo/common__event.ts:429,436; Protect from Magic is bit 16, 65536, the
  most common lit value in blert_sote_summary.txt MISC). Of 116 ordinary balls
  (NPC_ATTACK 8, TOB_SOTE_BALL) aimed at the recorder:
  - class A, Magic OFF on the throw tick and ON within T+1..T+4: 30 balls (15 switched on
    at T+4). 23 of the 30 lost no hitpoints at all through T+12, and 26 kept the Magic
    bit lit from the switch through T+7. A throw-tick read makes every one of the 30 an
    unprayed roll of up to 50 followed by the lock; 23 clean outcomes cannot come from it.
    The 7 drops are 14-45 at T+5/T+6 (four at distance 2: his melee), T+9 and T+12.
    Many are deliberate flicks: on at T+4, off again at T+8 (`-----MMMM-----`).
  - class C, Magic off throughout: 9 balls; the unprayed hits land at T+7 (37, 7, 4 at
    distance 4-8), which is our own flight at the barrier (end_cycle 232 = 7 ticks).
  - class B, Magic ON on the throw tick and OFF within T+1..T+4: 9 balls, mixed and small:
    m11_s3_921e8 T88 lost 45 at T+7 and m11_s5_41852 T222 lost 5 at T+7 (a throw read
    blocks both); m11_s3_71c56 T51 and m11_s3_921e8 T128 lost nothing (a 0 roll, a maze
    null, or a counter-example: not resolvable from the stream); the rest are back on
    before the landing or the recorder died.
  A guide says it in words: "Put on your prayer earlier than you think. The timing for
  these does feel super weird" (transcripts/yt_4i4lv-srJkw.md:93). The ricochets keep the
  same landing read by the wiki's "similar projectiles" (:92); no stream separates them.
  Script and output, pinned by the seam20 closer (who re-ran it: byte-identical):
  sources/blert_api/spec_pass_sotetseg/scratch/seam20_prayer_read.py and
  sources/blert_api/spec_pass_sotetseg/seam20_prayer_read.txt. OURS, measured: conformance row
  `seam.tob_sotetseg_ball_prayer_read_at_landing` (run s20_sote_c): off at throw 16 and on
  before landing 23, blocked to 0 (hitsplat 26); on at throw 26 and off before landing 33,
  unprayed 10, and the next press refused (the lock).
- Verzik P2's urnbomb, `[queue,tob_verzik_urnbomb_land]`: SOURCED AT THE LANDING (spec row
  `verzik.p2_bomb_prayer_read_tick`, grade D: one source family, the wiki). "Praying
  Protect from Missiles when the urnbombs land halves the damage taken from these attacks"
  (wiki_Verzik_Vitur.wikitext:394); of the blood spell, "Protect from Magic negates all
  damage from the blood spells, and is calculated during her attack animation unlike the
  urnbombs" (:397); "The damage is calculated on impact. As such, it is possible to save a
  few prayer points by not activating Protect from Missiles" (Strategies :907). So the
  tile bomb is NOT a send-tick read: the wiki names it as the exception, in the same
  sentence that names the blood spell as the default. The bomb is thrown at each raider's
  locked tile, queued on that raider, and judged at the landing against that raider still
  standing on it (`verzik.p2_bomb_judged_tile`, wording updated). OURS, measured:
  conformance row `seam.verzik_p2_urnbomb_prayer_read_at_landing` (run s20_bomb_a, Entry,
  max 16): off at the throw and on before the landing, raw 7,5,4,7,4,1 (all within the
  halved 8); on at the throw and off before the landing, raw 2,11,8,1,13,12.
- Verzik P3's autos, `[queue,tob_verzik_p3_auto_land]`: RULED (owner, 2026-10-04), the
  landing read stands; one comment line above the queue records the ruling.

What the class A/B split does NOT settle: the exact tick inside the flight. Class A
puts the read at T+4 or later (15 balls switched on at T+4, and 10 of them lost nothing at
all); three class B balls whose prayer was off from T+2 or T+4 lost nothing, which a 0 roll
explains but a read near T+3 would too. "Landing" is the reading every ball agrees with; the tick within +-1 of it is open
with M100 (sotetseg.ball_flight_by_distance).

Rooms (seam20 closer): both new spec rows are in every mode's scope (sidecar `all`; the
Sotetseg row's quantity names "Normal and Hard" only for blert's rooms, which the name
heuristic would have read as a Hard row). Neither room test measures them yet, so the raid
coverage gate reads tob_sotetseg 83 of 84 and tob_verzik 145 of 146; both ledgers are
otherwise unchanged and fully PASS. tob_sotetseg and tob_verzik must be re-authored to
measure them (SEAM_LEDGER.md).

THE AUDIT: every protection-prayer read in minigame_tob/scripts (grep
`prayer_is_on|check_protect_prayer|tob_nylo_prayed_against`, every mode: Entry, Normal and
Hard share these procs), with the tick it is read on.

| Read | Where | Tick |
|---|---|---|
| Maiden's blackstorm auto | `~tob_maiden_hit_damage` tob_maiden.rs2:815, called at :708 | the launch (send); the verdict rides to `[queue,tob_maiden_land_auto]` |
| Bloat's flies (Missiles, 25 %) | `~tob_bloat_fly_damage` tob_bloat.rs2:487, called at :431 | the throw (send); the roll rides in the queue |
| Nylocas wave swing / shot | `~tob_nylo_wave_damage` tob_damage.rs2:199 (via :180-188), called at tob_nylocas.rs2:1156 | the swing or launch (send) |
| Vasilias (Nylocas boss) | `~tob_vasilias_damage` tob_nylocas_boss.rs2:371, called at :247 and :294 | the swing or launch (send) |
| Sotetseg melee (Melee, halved) | `[queue,tob_sote_melee_impact]` tob_sotetseg.rs2:316 | the swing tick (queued at delay 0 from the swing: the same tick's player phase; the splat is +1) |
| Sotetseg ball and ricochets | `[queue,tob_sote_impact]` tob_sotetseg.rs2:383 | THE LANDING, sourced (above) |
| Verzik P1 auto (Magic, halved) | tob_verzik.rs2:771 | the launch (send); the hit rides to `[queue,tob_verzik_p1_land]` |
| Verzik P2 urnbomb (Missiles, halved) | `[queue,tob_verzik_urnbomb_land]` tob_verzik.rs2:1614 | THE LANDING, sourced (above) |
| Verzik P2 blood spell (Magic) | `[proc,tob_verzik_blood_spell]` tob_verzik.rs2:2243 | the cast (send) |
| Verzik P3 ranged / magic auto | `[queue,tob_verzik_p3_auto_land]` tob_verzik.rs2:3050 | THE LANDING, ruled (owner) |

No prayer read at all (nothing to time): Maiden's blood pools and crabs, Bloat's stomp and
falling flesh, Xarpus (spit, acid, stomp, `tob_xarpus_delayed_poison`), Sotetseg's death
ball (`[queue,tob_sote_ball_impact]`), the maze rag and tornado, Verzik P2's body slam and
stomp and Athanatos, the Hard acid, and P3's green ball (`[queue,tob_verzik_ball_land]`),
webs (`[queue,tob_verzik_web_land]`) and melee. Only the three landing reads above sit in
a landing queue, each sourced or ruled.

## From seam22 (matthew-mbp-m4-raid-b1-seam22, 2026-10-05): what the first Normal trio pass found

Seam tob_normal_trio_findings (SEAM_TRIAGE_2026-10-05a.md). Four findings; two moved code
(Sotetseg's arena rules in a party, Xarpus's chain in a party), two are settled without a
change (Verzik P1 at three, the Nylocas aggro count), and the prayer drain is the source's.
Scratches, Blert harvests and analyses: build/seam_state/matthew-mbp-m4-raid-b1-seam22/trio/ (gitignored);
the scripts, their outputs and the scratches are pinned at
docs/minigames/theater_of_blood/sources/blert_api/spec_pass_seam22/ ("trio/<file>" below names a
file there; the raw Xarpus harvest is re-fetched by its fetch_blert_xarpus.py).
Every "before" below is the same scratch under the same run name on a pack compiled off-tree
from the content repo's HEAD copy of the changed files (trio/mk_before_pack.sh, loaded with
TORIRSSERVER_SCRIPTS; the shared tree was never edited to prove it).

- **FIXED seam22** ToB, every room (seam19's Open row): every raider in the fight is judged by
  the room's per-player, per-tick rules, not only the raider who crossed the barrier. Source:
  "The players in the arena must also follow the same path ... Standing on a wrong tile will
  damage the player" (wiki_Sotetseg.wikitext:97); "Once someone steps on the fourth row, a
  tornado will spawn ... This tornado will not appear for the maze runner (unless they are the
  only player in the encounter)" (wiki_Theatre_of_Blood_Strategies.wikitext:803); Maiden's
  blood "will do damage to the player every tick" (tob_raid.rs2's own note). tob_raid.rs2:
  `~tob_watch_room` now calls `~tob_arm_party_watchdogs` (every other raider of the party who is
  in this raid, the `~tob_hud_orbs_party` shape; a party of one returns before any
  `p_finduid`), `~tob_arm_watchdog` arms only when `getqueue(tob_room_watchdog) = 0` (one chain
  per raider; a raider who crossed an earlier room's barrier and this one's had two), and
  `^tob_var_boss_misses` holds the TICK of the first miss rather than a count, so three
  watchdogs confirm a boss death on the same second tick one does (a count would have cleared a
  room on one transient miss). tob_sotetseg.rs2: the player half no longer stamps
  `^tob_var_maze_seen` with the tick (nothing read the stamp; it made every raider after the
  first on a grid return at the landing guard, so one raider a tick was judged), and an arena
  tornado despawns only when nobody else in its world is past the third row (a trio walks the
  path in file; read as "anybody on rows one to three despawns it", the raider behind would
  despawn it every tick and the raider ahead respawn it at the start). Measured, scratch
  trio/s22_sote_trio.lua (Normal, three raiders, `::tobmazearm`; the leader is the runner, p3
  walks to the mirrored path start, p2 three rows up the start column): run s22_sote_trio_after2
  `arena.tornado_spawn` "1 tornado spawn(s): type 8389 tick 39 at 6420,85", p2 ragged every tick
  on its off-path tile (21, 20, 18, 17, 16, 7), p3 hit by the tornado for 35 on the start tile,
  one spawn only; the same scratch on the HEAD pack (s22_sote_trio_before): 0 spawns, 0 rag
  explosions, no hit on either member. One-client and Entry identical (six Entry rooms, tick log
  and ledger cmp-equal on both packs under one run name). In a party the arena's floor now
  comes back on the maze's completion tick (an arena raider's hook restores it; before, the
  runner's did, a tick later, on the way out of the realm): Normal Sotetseg copy, tick logs
  equal but for the 210 floor rows at 240 instead of 241 and 472 instead of 473 (Near-Reality
  `completeMaze` sets the light floor on that tick). Stale comments left in files this seam does
  not own: tob_hud.rs2:250-260 and tob_spectate.rs2:295-297 still say only the starter's
  watchdog runs and that `^tob_var_boss_misses` is a count.
- **Settled seam22, no change** ToB, Sotetseg, the prayer drain (99 points in about 165 ticks on
  a prayed raider, the Normal author's run 2): it is the source's drain, not the room's. Nothing
  in tob_sotetseg.rs2 drains prayer (its only prayer effect is the unprayed ball's 5-tick
  protection lock). The author prayed Protect from Magic (drain effect 12) and an offensive
  prayer (Rigour/Piety/Augury, 24): "the total across all activated prayers is added to an
  internal prayer drain counter each game tick ... Players have a base prayer drain resistance of
  60, increased by 2 for every point of prayer bonus" (wiki Prayer, ?action=raw, :459-:463; the
  table :301-:304 Protect from Magic 12, :405-:409 Rigour 24; LostCity
  scripts/skill_prayer/scripts/prayer.rs2:164-:173 the same counter and `60 + equipment bonus *
  2`). 36 a tick at bonus 0 is one point per 1.67 ticks, 99 points in 165 ticks. Measured,
  trio/s22_prayer_drain.lua: 99 -> 63 in 60 ticks, 0 after 167. The Entry room prays Protect from
  Magic alone (12: a point per 5 ticks). Recipe, not content: restores, or flick the offensive
  prayer.
- **FIXED seam22** ToB, Xarpus phase 2, a party: a spit's splat chains to the NEXT RAIDERS IN ORB
  ORDER, not by Near-Reality's 50/50 bounce-or-splash coin. Source: "The first player will
  splatter to the next player in sequence, while the remaining players will splatter to the next
  two players in sequence. In the event that all players are on the same tile, or only one
  person is present in the arena, the poison will splatter a random uncovered tile"
  (wiki_Theatre_of_Blood_Strategies.wikitext:836, :838). Blert, freshly harvested (Normal stage-14
  streams: 13 trio, 6 duo, 4 four-raider, 5 solo rooms, trio/blert_xarpus, trio/fetch_blert_xarpus.py)
  agrees on WHERE: of the splats thrown from a landing (TOB_XARPUS_SPLAT source BOUNCE with
  bounceFrom = the landing, XarpusDataTracker.java), 115 of 117 in the trio rooms and 40 of 40 in
  the four-raider rooms fell on a tile a raider stood on (trio/an_blert_xarpus3.py); the coin's
  splash branch throws at random UNCOVERED tiles - at least half of every landing's orbs, and
  blert records every one of those - so the coin is refuted at party scale. The duo rooms (17 on
  a raider, 65 random) fit the sentence too: a duo's second chain walks back onto the target and
  goes to a random tile. Blert cannot COUNT the chains: it drops a splat that lands on a tile
  already holding one, and the next raider in orb order is the next spit's target, whose puddle
  is usually under them before the chain arrives (364 of 475 predicted trio chain tiles already
  splatted, trio/an_blert_xarpus2.py); so the count stays the wiki's (grade D). tob_xarpus.rs2:
  the spit passes "first spit of the phase" in its landing's one-bit field; `~tob_xarpus_land_splat`
  in a party throws 1 (first) or 2 orbs, each by `~tob_xarpus_chain_to` at the next raider after
  the target in orb order (a random uncovered tile when the walk is back on the target, the raider
  is caged or gone, or stands on the landing tile), and a chained orb does not chain again;
  `~tob_xarpus_bounce_splat` is gone. SOLO KEEPS THE COIN'S SPLASH (1-2 random uncovered tiles):
  both readings send a solo chain to random tiles (:838) and the count is not settled - blert's
  five Normal solo rooms (never masked: uncovered tiles) give 0, 1, 2 or 3 orbs within 12 ticks of
  a new landing (8, 43, 25, 18; trio/an_blert_xarpus6.py), which neither "1 then 2" nor "1 or 2"
  fits cleanly; Open below. Measured, scratch trio/s22_xarpus_trio.lua (Normal, three raiders,
  `::god 1`, stations stepping a square): `spec.xarpus.p2.chain_count` PASS "1,2,2,2,2,2,2,2,2,2,2,2,2",
  all 25 chained orbs on a raider's tile (trio/an_ours_xarpus.py); the HEAD pack under the same
  run name: FAIL "1,1,2,2,1,1,1,2,1,1,1,1,2", 10 of the 17 orbs from a spit's tile on random tiles.
  Spec row `xarpus.p2.chain_count` re-worded with both quotes and the blert finding (still D,
  value 1,2). tob_xarpus_normal must be re-authored: its dodge recipe was fitted to the coin (the
  copy under the same run name survives to 593 on the HEAD pack with chain_count 1,2,4, and the
  leader now dies to the stomp and acid at about tick 203 of the new pack); the Entry room is
  unchanged (solo path, tick log and ledger cmp-equal; tob_xarpus under its own id 120/120 PASS,
  FULL 63 rows).
- Open, ToB, Xarpus, solo: how many orbs a solo landing throws (Near-Reality 1-2, the wiki's
  sentence read for one player 1 then 2, blert 0-3 per new landing). Kept as Near-Reality's until
  a source separates them; a per-spit count over blert's solo rooms that also accounts for spits
  landing on old puddles (82 spits, 19 new landings, 70 chained in one room) would.
- **Settled seam22, no change** ToB, Verzik P1 at party 3 (all three Normal authors' raiders died
  by about tick 120): our P1 matches every source checked, so the failure is the recipe. Geometry:
  on the P1 auto ticks of 10 Normal trio rooms (blert stage-15 streams, the spec pass's harvest,
  trio/an_blert_verzik_p1.py) the raiders stood on room-local (26,29) 90 times and (30,34) 26
  times - our hide tile 6426,93 and our melee tile 6430,98 exactly (pillars from Near-Reality's
  `VerzikViturRoom.onLoad`, already aligned to the cache's death cages; tob.constant 3350-3384).
  Damage: the recorded raider's prayed drops across an auto are 0 (hidden) or 1-57, inside our
  prayed bolt's 0-68 ((1 + random(137)) / 2; "Having Protect from Magic active before the
  projectile is launched will reduce the damage by 50%, or 68 damage", Strategies :877). Cadence 14
  (verzik.p1_cadence, B). The shield is 1500 at three (A). What the trios do that the authors did
  not: P1 lasts 58-116 ticks (median about 70, 3-7 autos), and about 10 Dawnbringer specials per
  room (104 DAWN_SPEC attacks in the 10 rooms, trio/an_blert_verzik_p1_attacks.py) passed between
  raiders by dropping it ("requiring players to drop the Dawnbringer for the next player (in orb
  order) to use", Strategies :875; one Dawnbringer per raid, tob_xarpus.rs2), scythes between
  autos and the team behind the south-west pillar on the auto ("hide behind one pillar together",
  :883). The recipe is in DRIVER_NOTES.md ("Verzik P1 with three raiders", seam22).
- **Settled seam22, no change** ToB, Nylocas, `spawn_aggro` 34 of 35: our count is right and
  the test's is the wrong measurement. "Some wave-spawned Nylocas attack players instead of
  pillars. These are called aggros. As with all spawns, aggros are fixed across encounters"
  (blert_guides/tob_nylocas_mechanics_page.tsx:176-:178); our wave table holds exactly 35 aggro
  rows of 120 (configs/tob_nylo.dbrow `aggro,1`), and every one spawns. An aggro walks in as
  `incoming` and swaps to `fighting` at the box's edge, 9-11 ticks after it spawns (aggro_swap, B,
  380 blert swaps; tob_nylocas.rs2 `~tob_nylo_is_aggro`): one killed in its lane before the edge
  never swaps, in the game as here, and is still one of the 35. The Normal attempt counts swaps
  (the wave-10 west aggro was splashed by an Ice Burst before the edge). Count the spawns the
  table names (nylocas_waves.md `*` rows: wave, lane, size) or add the table's aggros killed in
  their lane to the swaps; `spawn_aggro` re-worded to say so.

## From seam30 play_tob_sotetseg (matthew-mbp-m4-raid-b1-seam30, 2026-10-05)

- ToB, Sotetseg Entry solo: a solo raider only ever sees the red ball. The Entry page's
  solo strategy says the room throws both: "It launches two types of small projectiles,
  either red or grey. The '''red''' one can be completely blocked with '''Protect from
  Magic''', while the '''grey''' one can be completely blocked with '''Protect from
  Missiles'''" (sources/wiki_Theatre_of_Blood_Entry_Mode.wikitext:191). Ours: the main
  ball is always `tob_sotetseg_maging` 1606 (tob_sotetseg.rs2 [proc,tob_sote_cast]), and
  the grey 1607 exists only as a SPLIT, which `[proc,tob_sote_split]` sends to another
  targetable player (`uid ! $victim`), so a party of one never sees it. Measured: six
  runs of test/raids/_play_sotetseg.lua (so30a and the five survey2 names): 79 projectile
  rows of 1606, 6 of 1604, 0 of 1607. Open: which projectile a solo raider's main ball is (no recorder
  row for solo Entry here); the play prays Missiles for a 1607 already, so a fix changes
  no play.
- ToB, Sotetseg Entry: the melee through Protect from Melee hits 1..10
  (`^tob_sote_melee_prayed_max_entry = 10`, tob_sotetseg.constant:26, "[derived] 20
  halved, the 45 -> 22 rule; no Entry source"). Not a disagreement, an UNSOURCED number
  that is the whole of a played Entry room's damage now: 54-70 of 70-97 taken per
  _play_sotetseg run, and every one of raid_report's "prayer" MISTAKES on it (10-13 per
  run, "took N ... through protectfrommelee"). raid_report counts a hit through the
  right prayer as a mistake; for this boss that is the content's rule (W:11 "up to 45
  damage (22 if prayed against)"), not a play error.
- ToB, Nylocas Entry (from seam30 play_tob_nylocas, NOT checked against an Entry source):
  Vasilias' prayed max through a magic or ranged protection is `^tob_vasilias_prayed_max
  = 17` (tob_nylocas.constant:1306) in every mode, while her Entry off-prayer max is 24.
  W:752's "17 if prayed against" is the Normal sentence. If Entry scales it down, this is
  the number that makes the red _play_nylocas names run out of supplies at Vasilias.
  Open: find an Entry source before filing it as a bug.

## From seam31 vasilias_entry_prayed_max (matthew-mbp-m4-raid-b1-seam31, 2026-10-06)

- ToB, Nylocas Entry: Vasilias' prayed max is UNSOURCED for Entry (settles the seam30 row
  above: no Entry source exists here; nothing changed). Ours: `[proc,tob_vasilias_damage]`
  (tob_nylocas_boss.rs2:383) returns `randominc(^tob_vasilias_prayed_max)` = 1..17
  (tob.constant:1306, not tob_nylocas.constant) for a magic or ranged swing through the
  matching protection in EVERY mode, while the off-prayer max is per mode, 24 / 70 / 105
  (tob.constant:1303-1305). Sources searched, none states an Entry prayed figure:
  the Vasilias infobox has per-version off-prayer max hits only ("|max hit1 = 24",
  "|max hit2 = 70", "|max hit3 = 105", wiki_Nylocas_Vasilias.wikitext:23-25) and its
  Entry section (:88-94) gives no number; the Entry Mode page's Vasilias section
  (wiki_Theatre_of_Blood_Entry_Mode.wikitext:184) gives none; its one damage line is
  generic, "the bosses have reduced stats and deal ~50% less damage than in normal mode"
  (:20), and her own infobox contradicts a flat 50% (24 of 70 is 66% less), so it fixes no
  number; the 17 is the Normal sentence, "can hit up to 70 off-prayer, and 17 if prayed
  against (except melee, which is fully protected)" (wiki_Theatre_of_Blood_Strategies
  .wikitext:752; 70 = max hit2); the 2021 New Modes post's Story Nylocas changes
  (newsposts/wiki_Update_Theatre_of_Blood_New_Modes.wikitext:32-37) name none; the cache
  record nylocas_boss_magic_story (cache_npc_nylocas.txt:754) carries no max-hit param;
  the one Entry blert stream (b093b327, scale 4, 429 events, build/spec_state/
  matthew-mbp-m4-raid-b1-spec-tob/blert_nylo_raw) has player rows with prayerSet and
  equipment only, no hitpoints or hitsplats, so it cannot measure one. Hard's 17 is the
  same open question (row "applied in Entry ... and Hard" above). Measured (ny31_final
  survey, the 5-of-5 green tree, raid_report): Vasilias took 42 of 177 (_play_nylocas),
  42 of 136 (sva), 85 of 202 (svb), 28 of 282 from her magic form (svc), 42 of 164 (svd);
  largest 17 magic, 12 ranged. It no longer reds the room. To settle: an Entry
  hitsplat recording (a player's own damage-taken log in Entry), or an owner ruling on a
  derived figure (17 x 24/70 = 6, or 17 halved = 8 by E:20), filed as grade E
  `nylocas.vasilias_prayed_max_entry`. Found by the seam30 Nylocas fixer; checked by the
  seam31 vasilias_entry_prayed_max fixer.

## From seam31's room seams (matthew-mbp-m4-raid-b1-seam31, 2026-10-06; filed by the closer)

- ENGINE: a Saradomin brew's overheal does not hold. svaplaynyloc t532-538 (Nylocas
  survey, 2026-10-06): the consume row reads hitpoints 99 -> 115, and the raider row reads
  99 on the same tick and every tick after, with no hit landing; 30 doses went that way in
  one run. The wiki's Saradomin brew heals over the base (to base + 16) and the boost
  decays one point a minute. This looks like the stat snap-back RAID_ORCHESTRATOR.md
  section 4 already names (torirs_server_combat.c), here on a BOOSTED stat. The bandage's
  own boost (tob_spectate.rs2 [opheld1,tob_bandages], stat_boost on the combat stats) is
  probably cancelled the same way; not measured. The Nylocas plan caps a brew at the base
  (raid_play_tob_nylocas.lua _play_nylocas_supplies); the library's _play_supplies still
  counts the overheal. Found by the seam31 play_tob_nylocas_green fixer.
- CLIENT: the client's npc row for the Verzik tornado (10846) never follows its walk.
  api_drive.npcs x,z (WorldEntity grid_position; DriveUi_Npcs, torirs_plugin_drive_ui.c
  ~582) stays on the spawn tile for its whole life (vz31d: 6431,91 for nine ticks) while
  the server's npc_tile rows walk it one tile a tick (tob_verzik.rs2
  ~tob_verzik_tornado_tick, npc_walk). Other walkers (crabs, Matomenos) are read moving.
  Not investigated. The Verzik plan dead-reckons the tornado from its spawn tile and
  believes the row whenever it moves. Visual check for later: does the tornado's model
  move on screen? Found by the seam31 play_tob_verzik_green fixer.
- SPEC: our server summons the Matomenos in Verzik P2's 7th attack slot after six
  attacks (a 36-tick cycle in every survey log), while `verzik.reds_attacks_between`
  reads "P2 attacks between Matomenos summons 7" (Blert, grade B). Either Blert counts the
  summon as one of the seven, or the content is one attack short. A row for the next spec
  pass; the plan holds for both readings. Found by the seam31 play_tob_verzik_green fixer.

## From seam32's Normal trio plans (matthew-mbp-m4-raid-b1-seam32, 2026-10-06; filed by the closer)

- CONTENT: the `br_` Saradomin brew DRAINS Defence. br_potion.rs2:76-80
  (`[br_4dosepotionofsaradomin ...]`: `stat_drain(defence, 2, 10)` beside the Attack,
  Strength, Magic and Ranged drains). wiki_Saradomin_brew.wikitext:56: "raises Hitpoints by
  15% + 2 and Defence by 20% + 2", and the owner's ruling "the Saradomin brew raises
  Defence". sara_brew.rs2 was fixed in seam18; this copy, which the party kits and the ToB
  chest hand out, was not. Every brew dose in the Maiden and Bloat trios lowers the
  raider's Defence. Not edited (no seam row named it). Found by the seam32
  play_tob_bloat_normal fixer.
- CONTENT: no salve amulet bonus against undead anywhere (no `salve` in skill_combat/*.rs2
  or src/torirsserver/*.c). W:670: "Being undead, the salve amulet is very powerful
  against Bloat ... make sure to equip the salve amulet". The Bloat kit wears
  amulet_of_rancour (`::maxmelee`). Found by the seam32 play_tob_bloat_normal fixer.
- CONTENT: a swing due ON Vasilias' turn tick lands and is judged against the new form.
  wiki_Theatre_of_Blood_Strategies.wikitext:752: "The player will stop attacking when
  Vasilias changes forms"; tob_nylocas_boss.rs2 `tob_vasilias_act` calls p_stopaction
  ("cancels every player's attack if she has just turned"), and npcs act before players
  (ENCOUNTER_TIMING 1.1). Yet s32ny2 has player_anim and npc_heal on the npc_retype tick
  (t562 whip heal 4; t572 bow heal 1+3; t632 bow heal 5+3; 34 heals, 342 hitpoints). The
  plan no longer swings on the turn tick, which hides it. Found by the seam32
  play_tob_nylocas_normal fixer.
- CONTENT: a frozen nylocas stops biting a pillar only in Entry.
  wiki_Theatre_of_Blood_Strategies.wikitext:719: "Frozen nylocas will usually stop
  attacking a pillar for a short time"; tob_nylocas.rs2:985-996 stops the bite in Entry
  only, so in Normal a freeze buys nothing (grade D reading of "usually"). The trio plan
  does not rely on freezes. Found by the seam32 play_tob_nylocas_normal fixer.
- DRIVER: Ice Rush presses answer `refused` about a third of the time for a helper seat
  (s32ny7 p2: 15 of 41; spell.lua `QD.player._select_row_is_held`), each a lost tick and a
  3-tick block of that copy. Found by the seam32 play_tob_nylocas_normal fixer.
- DRIVER: the Maiden rangers' add Attack on a walking Matomenos answers `timeout` on most
  presses (every leader's play.fight detail) though the hits land. Not read yet. Found by
  the seam32 play_tob_maiden_normal fixer.

## From seam33 chinchompa_multi_target (matthew-mbp-m4-raid-b1-seam33, 2026-10-06)

- FIXED (seam33, content; commit named by the closer): chinchompas hit one npc.
  player_ranged.rs2:17 said "Chinchompa multi-target ... (#18) still deferred": every
  throw was one ordinary ranged hit. The trio sources' Maiden ranger chins the clumps
  (wiki_Theatre_of_Blood_Strategies.wikitext:284 "Ranger should bring 15-25 black
  chinchompas ... to hit groups of Nylocas Matomenos"; :639-641 "barrage a few times before
  letting the ranger chin the clump"). Now `[proc,player_chinchompa_splash]`
  (player_ranged.rs2) hits the 3x3 around the target on the primary's landing tick, the
  primary's accuracy roll deciding every secondary, one damage roll per npc through
  `~player_hit_npc_prepare`, up to 11 targets (12 for the black chinchompa), and only in a
  multi-way area (maps/multiway.csv or a map instance, the engine's own rule). Sources:
  wiki_Chinchompa_weapon, wiki_Red_chinchompa:24, wiki_Black_chinchompa:25/:64,
  wiki_Multicombat_area:68/:87. Measured: build/quest_gate/chin33_red2 (multi zone, one row
  per throw: 21[ws1488:3] 25[ws1488:0]) -> chin33_green (21[ws1487:11 ws1488:1 ws1489:11],
  the copy four tiles east never hit; Lumbridge single-way: primary only) and chin33_room
  (ToB Nylocas instance, 20[ws1079:10 ws1080:6 ws1081:11]). Found by the seam32
  play_tob_maiden_normal fixer.
- Still open, CONTENT: chinchompa fuse accuracy by distance (wiki_Chinchompa_weapon table:
  short fuse 100/75/50 percent at 0-3/4-6/7+ tiles, medium 75/100/75, long 50/75/100,
  "applied to the accuracy/defence roll when the attack is cast") and the "heavy" ranged
  defence for the primary's roll are not modelled: `~player_npc_hit_roll`
  (combat_stats.rs2:710) takes no modifier. Not in seam33's files.
- Still open, CONTENT: an npc whose record says `forcemulti=yes`, standing outside both
  maps/multiway.csv and a map instance, is treated as single-way by the chinchompa splash:
  the record key is the engine's (`ToriRSServer_CombatMultiway`) and content cannot read it.
- Still open, CONTENT: `~pvm_barrage_spell` (player_magic.rs2:68-71) still hits the 3x3 in
  single-way areas; its comment says multiway "is not modelled", which stopped being true
  when `map_multiway` was hosted. Same wiki line as above (Multicombat area:68).

## From seam33's Normal trio plans (matthew-mbp-m4-raid-b1-seam33, 2026-10-06; filed by the closer)

- Open, CONTENT: the Scythe of Vitur's 1x3 arc on 1x1 targets is not implemented.
  scythe_of_vitur.rs2:38 says "Only (1) is implemented" (no NPC_FINDALLZONE dispatch), so a
  scythe multi-hits only a 2x2 big. The trio guide's "Scythe the east melee doubles"
  (tob_nylocas_trio_content.mdx:415, :232-239) cannot be played. Found by
  play_tob_nylocas_normal_green; the Normal trio Nylocas meleer is the bottleneck (66-77
  swings in about 390 wave ticks, 34-44 of 206 copies never killed).
- Open, unsourced: the swift blade (the trio meleer's 3-tick weapon, trio guide :386)
  carries no attack or strength bonus params in all.obj, only attackrate 3. No source page
  was fetched, so its stats were not checked; the plan does not use it.
- Re-read, ENGINE suspect (the seam32 row above, "Vasilias turn-tick swing", stands): "The
  player will stop attacking when Vasilias changes forms"
  (wiki_Theatre_of_Blood_Strategies.wikitext:752), and npc turns resolve before player
  turns (ENCOUNTER_TIMING 1.1), so a swing due on the turn tick must not land.
  tob_nylocas_boss.rs2:166-189 calls p_stopaction in that hunt, so the content means it; a
  swing still landing on the npc_retype tick (seam32 s32ny2) points at the engine's turn
  order or at p_stopaction not clearing an interaction already due that tick. The Normal
  plan masks it (npc_heal on her 0 on all seven seam33 names).

## From seam36 tob_rows_nylocas_verzik (matthew-mbp-m4-raid-b1-seam36, 2026-10-06)

- FIXED (content): Verzik P2 summoned the Matomenos one attack early and the first set off
  the attack clock. Source, the 60 pinned Blert streams re-read by red spawn tick
  (build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_verzik/1[012]_*.json): 7 P2
  attacks between two spawns in 84 of 84 cycles, spawn -> first attack 12 (84/84), seventh
  attack -> next spawn 8 (84/84), spawn -> spawn 44 (84/84); the first set 4 ticks after the
  attack before it in 62 of 62 rooms (Entry 2, Normal 43, Hard 17). Blert's plugin says the
  same, VerzikDataTracker.java:70-71 `P2_ATTACKS_PER_REDS = 7` and :721-722 "Last auto before
  the next reds phase". Ours summoned on the seventh slot after SIX attacks (36-tick cycle,
  survey svaverzik: RED307, 319..339, RED343) and checked the 35% every tick. Now
  `[proc,tob_verzik_reds_due]` is read on the attack slot only and the seventh attack pushes
  the next slot to `^tob_verzik_p2_reds_after_last = 8` (tob_verzik.rs2 ~tob_verzik_p2_tick,
  tob.constant). Measured: seed_survey _play_verzik 5 of 5 green, every name RED -> 7
  attacks 4 apart -> RED 8 later (svaplayverzi RED215, 227..251, RED259, 271..295, RED303).
  Changed kept row: tob_verzik `spec.verzik.reds_attacks_between` 7 PASS -> 8 FAIL; it counts
  "casts and the summon, which takes the seventh attack's place", the old reading; the
  re-author counts casts between spawns (7). verzik.tsv: row reworded, new row
  `verzik.reds_last_attack_to_summon` 8 (grade B).
- OPEN, ENGINE (not in this seam's files; one line): `p_stopaction` stops the wrong player.
  torirs_server_scripts.c:13461 `case SS_OP_P_STOPACTION: ToriRSServer_CombatStopPlayer(srv);`
  stops `srv->active_player` (torirs_server_combat.c:951-953), while `p_finduid` binds only
  the script's active player (scripts.c:6956-6985, `SSVM_SetActive`), which is the handler's
  own `player` (scripts.c:5175). So Vasilias' turn cancel (tob_nylocas_boss.rs2:187-190, the
  wiki's "The player will stop attacking when Vasilias changes forms",
  wiki_Theatre_of_Blood_Strategies.wikitext:752) stops one stale player once per raider.
  Evidence, build/quest_gate/s32ny2 ticklog, tick 562 in serial order: 15615 npc_retype 1079
  8355 -> 8357, 15616 input p0 [apnpc2,nylocas_boss_ranged], 15617 player_anim p0 1658,
  15619 hit_player p0 4, 15620 npc_heal 4: the interaction survived the stop. A solo room
  hides it (the stale player is the only one). Fix: `assert(player);
  ToriRSServer_CombatStopPlayerAt(player);`. This settles the seam32/seam33 rows "a swing due
  ON Vasilias' turn tick": the content is right, the engine op is not. A hit already in
  flight at the turn (rolled before it) is a separate question no source answers (the Blert
  streams carry no hitsplats); not changed.
- SETTLED, no change: a frozen nylocas and a pillar. The mechanic page: "Freeze ... restricts
  movement of affected players and NPCs, rendering them immobile for a period of time without
  disabling other actions" (wiki Freeze, fetched 2026-10-06, build/seam_state/
  matthew-mbp-m4-raid-b1-seam36/sources36/wiki_Freeze.wikitext:4). So "a frozen npc cannot
  attack" is false in general; Entry's own sentence is an Entry exception ("Frozen nylocas
  cannot attack the pillars until unfrozen, even if they are in melee range",
  wiki_Theatre_of_Blood_Entry_Mode.wikitext:166), and Normal's "Frozen nylocas will usually
  stop attacking a pillar for a short time" (Strategies:719) is the Freeze rule seen from
  play (a frozen walker cannot reach the support; one already biting goes on). The content
  (tob_nylocas.rs2 ~tob_nylo_pillar_tick) is exactly that. The live Nylocas, Ice spells and
  Theatre of Blood pages carry no frozen-nylocas sentence.
- SETTLED, no change: Vasilias' reflect is 100 percent ("now reflects and heals 100% of the
  damage done with the incorrect style instead of 50%", wiki_Nylocas_Vasilias.wikitext:131;
  newsposts Deadman Summer Finals:52); tob_damage.rs2:293-301 reflects and heals the rolled
  damage (clamped to her hitpoints). The survey FAIL "largest reflect 7 > half 9 of max hit
  19" is the kept check's sample bound (tob_nylocas.lua:1797: one reflect over half the max
  rules out 50 percent), not the rule: the re-author keeps pressing the wrong style until one
  reflect exceeds half its press max.
- SETTLED, no change: the urnbomb reads Protect from Missiles at LANDING already
  (tob_verzik.rs2 `[queue,tob_verzik_urnbomb_land]` rolls and calls `~check_protect_prayer`
  inside the landing queue). Decisive server evidence: build/quest_gate/s22e_verzik "rev thrown
  334 landing 337 flipped 334 (before true, after false) hit 15", the seam36 tob_verzik run
  "rev thrown 292 ... hit 10": over the prayed ceiling 8 under a prayer that was on at the
  throw, impossible under a throw read. The sva/svc/svd FAILs had 1-2 reverse trials that
  all rolled <= 8 (each 1 in 2). Re-author: a trial is decisive only when it is a reverse
  trial (on at throw, off at landing) that lands over floor(max/2); keep trialling until one
  does (a forward trial can never prove landing).
- OPEN, unsourced: Entry Vasilias' prayed max (17 = Normal's). The one Entry Nylocas stream
  (blert_nylo_raw/*.m10.s4.json) has 0 of 300 player rows with hitpoints; nothing new.
- SETTLED server side, CLIENT note: the Verzik tornado (10846) is walked by the server as an
  npc and its npc_tile rows move one tile a tick (svaverzik slot 1080: 624 6431,91, 625
  6430,91 ... 630 6425,86, 632 6427,84). The client row that stays on the spawn tile is the
  client lane's (DriveUi_Npcs / WorldEntity grid_position).

## From seam36 tob_rows_bloat_sotetseg_brews (matthew-mbp-m4-raid-b1-seam36, 2026-10-06)

- CLOSED, NOT A CONTENT BUG: Bloat's flies through Protect from Missiles (the survey's
  `tech.protect_from_missiles` "123 fly hits ... the largest was 7"). Source:
  wiki_Pestilent_Bloat.wikitext:90 "[[Protect from Missiles]] reduces damage from the flies
  by 25%." tob_bloat.rs2 `~tob_bloat_fly_damage` already keeps 75 % (`^tob_bloat_flies_prayer_pct`)
  of the Entry 4..8 roll, read off the target at launch: 3..6. Re-read of
  build/seed_survey_2026-10-05/svdbloat/ticklog.tsv: the fly-sized hit_player rows from tick 153
  (the first press) on are 3 x64, 4 x23, 5 x33, 6 x22, max 6; the only 7s are t87 and t89,
  before any press. The kept test's window is wrong, not the content: tob_bloat.lua :185
  `shield_at[now] = (phase == "walk" and prayer_now > 0)` counts a walking tick with prayer
  POINTS as shielded, so pre-press flies count (note for the re-author: read the lit set,
  `t.prayer.read`, or the raider row's `prayers` bit). Spec: bloat.entry_fly_prayer 3-6 (D).
- CLOSED as a disclosed approximation (grade E, M165): Sotetseg Entry's melee through Protect
  from Melee 1..10. Normal is halved, not blocked: wiki_Theatre_of_Blood_Strategies.wikitext:787
  "The melee attack consists of a lunge with its horns that can deal up to 45 damage (22 if
  prayed against)". Entry has only the unprayed max (wiki_Sotetseg.wikitext:23 "max hit1 = 20
  ([[Melee]])") and the Entry page's advice "praying Protect from Melee"
  (wiki_Theatre_of_Blood_Entry_Mode.wikitext:191); no Entry recorder row exists (blert: one
  Entry raid, no melee on the recorder). `^tob_sote_melee_prayed_max_entry = 10` stays;
  spec sotetseg.melee_max_prayed_entry (E, [M165]); the plan's open table needs its M165 row.
  raid_report.py counting a hit through the right prayer as a "prayer" MISTAKE is the tool's
  rule, not the play's error, for this boss.
  Re-checked 2026-10-07 content_bugs: STILL the disclosed approximation, blocked on a source. The Entry page
  gives only "an inaccurate but high-hitting melee attack" and the advice to pray Protect from Melee
  (wiki_Theatre_of_Blood_Entry_Mode.wikitext:191); the Entry infobox only the unprayed 20 (wiki_Sotetseg
  .wikitext:23); Blert's one Entry raid has no melee on the recorder. Missing: any Entry recording of a melee
  hit through Protect from Melee (a Blert Entry stream with Sotetseg melee on the recorder, or a video).
- FIXED (content): the `br_` Saradomin brew drained Defence. `br_` is the cache's Last Man
  Standing supply family ("battle royale": all.obj also holds br_bloody_key, br_token); the
  ToB chest (tob.rs2 :855) and the ToA bundles hand it out. br_potion.rs2 now
  `stat_boost(defence, 2, 20)` like sara_brew.rs2 (wiki_Saradomin_brew.wikitext:56 "raises
  Hitpoints by 15% + 2 and Defence by 20% + 2 of their base levels"). Measured
  (build/quest_gate/s36brewA): defence 99 -> 120 on one dose. The only other Saradomin brew
  path is sara_brew.rs2 (castlewars_brew.rs2 is a different potion).
- FIXED (engine): a brew's overheal was cancelled by the next hit. Every hit on a player ran
  ToriRSServer_CombatSyncHitpoints (torirs_server_combat.c), which clamped hitpoints to the
  base; a 0 splat did it too (svaplaynylocC: consume 89 -> 105 at t274, raider 105 at t275, a 0
  hit_player at t276, 99). Sources: wiki_Saradomin_brew.wikitext:68 "This heal is a boost to the
  Hitpoints skill and does allow for [[overhealing]]"; wiki_Hitpoints.wikitext:95 "Boosted
  hitpoints levels above a player's maximum Hitpoints level decay at a rate of one per minute,
  identically to other [[temporary boost]]s." The sync no longer clamps (255 cap; the bar's
  denominator becomes the current hitpoints while overhealed, so the bar reads full); a LOST
  hitpoints level still clamps, in the xp path. Tick log (s36brewA): t12 consume
  br_4dosepotionofsaradomin 99 -> 115; raider 115 at t13..t16; a 15 nightshade at t16 -> 100.
  Before (s36brewBefore, pre-edit binary): the same bite read 99. The bandage's combat boost
  (tob_spectate.rs2) holds through a hit (attack 106, defence 120 after a 15 bite).
- OPEN (content, not this seam's file): the overheal never DECAYS. stat_restore.rs2
  `[timer,stat_restore]` skips `^stat_hitpoints` (LostCity's stat_boost_restore does too: 2004
  had no hitpoints boost), so a held overheal stays until a hit. The page's rule (Hitpoints :95
  above) is one line there: for hitpoints, `if (stat > stat_base) stat_sub(hitpoints, 1, 0)`.
  Also seen, not investigated: in the scratch run hitpoints 84/85 under base did not regenerate
  in 240 ticks on either binary (health_regen.rs2 timer).

## From seam36 combat_rows_tree_wide (matthew-mbp-m4-raid-b1-seam36, 2026-10-06)

- FIXED (seam36, content; commit named by the closer), DAMAGE half: no salve amulet bonus
  against undead (the seam32 row above). wiki_Salve_amuleti.wikitext "Comparison": Salve
  amulet 16.67% melee; (e) 20% melee; (i) 16.67% melee and ranged, 15% magic; (ei) 20% all
  three, "to accuracy and damage"; :111 "do not stack with the black mask and slayer
  helmet's on-task bonuses; wearing both will only apply the bonuses of the salve amulet".
  New gear/salve_amulet.rs2 `~salve_or_black_mask_scale_target` (salve when it applies to
  that style against an `undead`-param npc, else the mask) replaces the mask call in
  `~player_hit_npc_prepare`. Pestilent Bloat already carries param=undead,1
  (npc/configs/combat_stats.generated.npc; wiki_Pestilent_Bloat.wikitext:20). Measured,
  kourend_spectre stand-in, Lumbridge, ::maxmelee + whip: build/quest_gate/s36c_salve_red2
  (salve(ei): 16 landed, max 34 = M) -> s36c_salve_green2 (`fury.hits` M=36 max 36, 0 above;
  `salve_ei.hits` M=34 (::salvemax) max 40 = floor(34 x 6/5), 2 rows above M).
- Still open, CONTENT (one line, not in seam36's files): the salve's ACCURACY half.
  combat_stats.rs2:642 `[proc,player_attack_roll]` still calls
  `~slayer_black_mask_scale_target($roll, $style)`; replacing it with
  `~salve_or_black_mask_scale_target($roll, $style)` applies the page's accuracy column
  (the closer's snippet). Until then a salve raises the max, not the hit chance.
- FIXED (seam36, content), compiled only, no driver run: a barrage/burst hit the 3x3 in a
  single-way area (the seam33 row). wiki_Multicombat_area.wikitext:68 "they can only hit
  multiple opponents while in a multi-combat area"; wiki_Ice_Barrage.wikitext:23/:29.
  `~pvm_barrage_spell` (player_magic.rs2) now sweeps the 3x3 only when `map_multiway` or
  `map_instance_find` says so (the chinchompa splash's own test; every ToB room is an
  instance, so no ToB plan changes). gear_selftest.rs2 `~gear_selftest_barrage` asserts 1
  target in a single-way area, 2..9 in a multi-way one.
- FIXED (seam36, content), compiled only, no driver run: chinchompa fuse accuracy (the
  seam33 row). wiki_Chinchompa_weapon.wikitext:21-40 (short 100/75/50, medium 75/100/75,
  long 50/75/100 at 0-3/4-6/7+ squares; Mod Ash "applied to the accuracy/defence roll when
  the attack is cast"). player_ranged.rs2 `~player_chinchompa_hit_roll` scales the ranged
  attack roll by the stance's percentage at Chebyshev distance player->npc coord (a big
  npc's south-west tile: an approximation the page does not settle). The seam33 Normal
  Maiden ranger chins from short range on short fuse (100%), so its plan should not move;
  not re-surveyed in seam36.
- Still open, CONTENT: the chinchompa primary's 'heavy' ranged defence
  (wiki_Chinchompa_weapon.wikitext:19). No npc in this tree carries a light/standard/heavy
  split of `rangedefence`; needs the param first.
- Left, not ToB: `forcemulti` npcs (only dks.npc and tormented_demons.npc carry it) outside
  multiway.csv and an instance read as single-way to the chinchompa splash and now to the
  barrage too; the key is the engine's (`ToriRSServer_CombatMultiway`).
- Left, time: the Scythe of Vitur 1x1 arc (seam33 row; gear/scythe_of_vitur.rs2:38) and the
  swift blade's missing bonuses (no source fetched) are unchanged by seam36.

## From the seam36 closer (matthew-mbp-m4-raid-b1-seam36, 2026-10-06)

- FIXED (closer, content, one line): the salve's ACCURACY half above.
  combat_stats.rs2 `[proc,player_attack_roll]` now calls
  `~salve_or_black_mask_scale_target($roll, $style)`: wiki_Salve_amuleti.wikitext:20
  "Increases melee, ranged and magic damage & accuracy", the Comparison table's Accuracy
  columns, and :111 "do not stack with the black mask". With no salve against an undead
  target it returns the black mask call unchanged (cooks_assistant and druid byte-identical,
  the quest suite at its baseline). The accuracy effect itself was not measured by a run.
- Still OPEN, ENGINE: `p_stopaction` stops `srv->active_player`, not the bound player
  (torirs_server_scripts.c:13461; the seam36 nylocas row above). Not applied by the closer:
  it changes every content caller of the command, and the party Nylocas run that proves it
  was not in this pass.
- Still OPEN, CONTENT: a held overheal never decays (stat_restore.rs2 skips hitpoints;
  wiki_Hitpoints.wikitext:95 "decay at a rate of one per minute").
- Test row, not content: tob_nylocas `spec.nylocas.vasilias_spawn_delay` reads 41 on the
  closer's own-name run. Tick log: a big died at 613 and split; its smalls freed at 665; she
  spawned at 682, 17 ticks later, inside the spec's 16-19. The row takes the last WAVE
  nylocas free (641) and skips the split smalls (DRIVER_NOTES.md, closer seam36).

## From seam38 stopaction_and_stat_rows (matthew-mbp-m4-raid-b1-seam38, 2026-10-06)

- FIXED, ENGINE: `p_stopaction` stopped `srv->active_player`, not the script's bound
  player. Source: LostCity Engine-TS PlayerOps.ts:431-433 `[ScriptOpcode.P_STOPACTION]:
  checkedHandler(ProtectedActivePlayer, state => { state.activePlayer.stopAction(); })`.
  Content caller: tob_nylocas_boss.rs2 `[proc,tob_vasilias_act]` `huntnext` loop,
  `if (p_finduid(uid) = true) { p_stopaction; }` for every raider on her turn. Now
  `assert(player); ToriRSServer_CombatStopPlayerAt(player);` (torirs_server_scripts.c).
  The same active-vs-bound audit of every `p_*` case in `ToriRSServer_ScriptCommand`,
  fixed in the same file: `p_opnpc` (shadowed the bound player with `srv->active_player`;
  its AFK stop and interaction clear now act on the bound player, and
  `WorldInteractionSet`/`WorldWalkToApproach` run with the world pointed at it),
  `p_opnpct` (cleared/set `srv->active_player`'s interaction but wrote the spell and
  cleared the steps of the bound one), `p_walk` (`WorldWalkTo(srv)` walked
  `srv->active_player` while the same-tile branch read the bound player), `p_logout`,
  `p_countdialog`. The rebinding is a save/`ToriRSServer_WorldSetActive`/restore pair
  (`script_world_bind_player`/`script_world_unbind_player`), the shape `p_overhit` uses; in
  a player's own script the two are one player and it is a no-op. Correct already:
  `p_overhit` (uid-addressed), `p_teleport`/`p_telejump`/`p_delay`/`p_stun`/`p_aprange`/
  `p_arrivedelay`/`p_locmerge`/`p_exactmove` (read the bound `player`).
  Measured: Normal trio `_play_nylocas --party 3` own name (s38nylo3): her 37 retypes
  (t490..849), 0 player_anim rows on a retype tick, every raider row on those ticks reads
  `tgt -1`; play.no_reflect PASS (0 heal rows). The SAME run on the pre-fix C
  (s38nylo3pre, QUEST_BINARY from a HEAD worktree) also reads 0 and `tgt -1` on all 27
  retypes: the play plan's turn holds keep every raider off her on the turn, so this plan
  cannot show the bug; the evidence of the bug stays seam36's s32ny2 tick 562 (serials
  15615-15620, p0 swinging after the retype). Entry solo survey `_play_nylocas` 5 of 5.
  Server selftest 11 failures, the same 11 rows as HEAD.
- OPEN, ENGINE (not this seam's file): `src/torirsserver/torirs_server_ops_player.c`
  `p_oploc`/`p_opobj`/`p_opplayer` call `ToriRSServer_WorldClearPendingAction(srv)`,
  `ToriRSServer_WorldInteractionClear(srv)`, `WorldInteractionSet(srv, ...)` and
  `WorldWalkToApproach(srv, ...)`, which act on `srv->active_player`, and `p_oploc`
  resolves the loc transform against `srv->active_player`; the same rebind applies. No
  content caller from an npc script is known.
- FIXED, CONTENT: a held overheal never decayed. Source: wiki_Hitpoints.wikitext:95
  "Boosted hitpoints levels above a player's maximum Hitpoints level decay at a rate of one
  per minute, identically to other temporary boosts. This timer can be restarted through
  activating the Rapid Heal prayer or through logging out" -- the two places that rearm
  `[timer,health_regen]` (prayer.rs2 `~health_regen_rearm`, login.rs2
  `~health_regen_login`), so the decay lives on that timer, not on stat_restore:
  health_regen.rs2 now subtracts one when `stat(hitpoints) > stat_base(hitpoints)` and heals
  one otherwise, and the interval is the flat 100 while overhealed (the page's rate; Rapid
  Heal / the cape / the regen bracelet are regeneration rates, :56-58). Measured, scratch
  s38_hp1 (fresh_lumbridge) raider rows: consume t254 `[opheld1,br_4dosepotionofsaradomin]`
  99 -> 115; hp 115 at t255; 114 at t301 (the timer already running, not restarted by the
  drink); 113 at t401, exactly 100 later. Ledger overheal.drink 115, overheal.minute 114,
  overheal.two_minutes 113 PASS.
- SETTLED, no change: "hitpoints of 84/85 under the base did not regenerate in 240 ticks"
  (seam36, fresh_lumbridge scratch). Not reproduced: s38_hp1 `::drain hitpoints 15 0` at t6
  -> 84; raider rows 85 at t101, 86 at t201 (wiki Hitpoints:56 "1 Hitpoint per minute").
  The timer is armed at login (login.rs2:40 `~health_regen_login`) and survived the
  fixture start. Seam36's scratch had an npc biting (15 at t16) and a bandage; its reading
  is not repeated here, so the row is closed on this measurement, not explained.
- SPEC: M165 (Sotetseg Entry prayed melee max 10, grade E) is now in
  THEATRE_OF_BLOOD_PLAN.md's open table and `^tob_sote_melee_prayed_max_entry` carries
  `[derived][M165]` (tob_sotetseg.constant).

## From seam38 scythe_arc_and_salve_accuracy (matthew-mbp-m4-raid-b1-seam38, 2026-10-06)

- FIXED (seam38, content; commit named by the closer): the Scythe of Vitur's 1x3 arc on 1x1
  targets (the seam33 row; scythe_of_vitur.rs2:38 "Only (1) is implemented").
  wiki_Scythe_of_vitur.wikitext:81 "their attack range is increased to a 1x3 arc radius in
  front of the player, which can allow them to hit three 1x1 targets in front of them";
  :83 "accuracy and strength rolls are rolled independently on each target, though each hit
  will deal 50% less damage, (rounded down), than the preceding hit"; :85 "47-23-11";
  wiki_Multicombat_area.wikitext:95 "Cleave: Hit many targets and/or larger targets multiple
  times in a player-facing cone" with :68 "they can only hit multiple opponents while in a
  multi-combat area". Tiles from Near-Reality ScytheOfViturCombat.java:139-159 (target tile,
  left and right across the facing; an npc counted by its south-west tile) and :101 (1.0 /
  0.5 / 0.25). New `[proc,scythe_of_vitur_arc]` / `[proc,scythe_of_vitur_arc_hit]`, called
  from `~scythe_of_vitur_swing` only when the primary is size 1; `npc_findallany` (dispatched
  now, torirs_server_scripts.c) replaces the missing NPC_FINDALLZONE. The large-target hits
  (size 2: two, size 3+: three, floor(M/2), floor(M/4)) were already the page's rule and are
  unchanged. Proved, build/quest_gate/s38arc_after2 (6 of 6): single-way Lumbridge, three
  goblins in a row, one hit_npc row on the middle copy (tick 11); multi-way Barbarian Village
  (multiway.csv 0_48_53) three rows on three slots in one tick (tick 24: 5, 5, 4; tick 37:
  5, 5, 1); a lone goblin one row; cow (size 2) two rows; black_demon_strongholdcave_1 (size 3)
  three rows on 8 of 9 swing ticks (the ninth killed it), per-position max 12 / 8 / 25 in log
  order hit2, hit3, primary. Bloat (size 5, party 3 survey): 3 rows a scythe swing (tick 111:
  2, 2, 33).
- Stated, not sourced: which arc side takes the 50% hit. The wiki is silent and NR leaves it
  to a HashSet's iteration order; this takes the left one (across the facing) first.
- Left, small: NR gives a LARGE npc standing on an arc side tile the swing's remaining hits;
  the wiki's "three 1x1 targets" does not cover it; here every arc secondary takes one hit.
- No ToB room changes from the arc: every scythe primary in the kept Bloat, Sotetseg, Xarpus
  and Verzik rooms is size 2 or more (Bloat 5, Sotetseg 5, Xarpus 5, Verzik forms, Athanatos
  3, Matomenos 2); the arc fired 0 times in _play_smoke/_play_sotetseg/_play_xarpus/
  _play_verzik/_play_bloat x3 and the kept tob_verzik scratch (no tick with a 1x1 primary and a
  same-tick secondary). The Normal trio Nylocas "Scythe the east melee doubles" plan is what
  it unblocks.
- PROVED (seam38), the salve's ACCURACY half applied by the seam36 closer
  (combat_stats.rs2:646): build/quest_gate/s38salve1, kourend_spectre (undead), ::maxmelee,
  attack 1, whip: 75 landed of 381 swings (19.7 per 100) without, 90 of 317 (28.4 per 100)
  with salve(ei); ratio 1.44, 95% interval about 1.10..1.89, containing the page's 6/5. One
  salve application on the attack roll (grep: only combat_stats.rs2:646).
- Closed as a row, CONTENT: the chinchompa 'heavy' ranged defence
  (wiki_Chinchompa_weapon.wikitext:7 "|type = heavy"). Every ToB npc page in sources/ gives
  dlight = dstandard = dheavy (Pestilent_Bloat 800/800/800; Sotetseg 120/150/150 per form;
  Xarpus 100/160; Verzik 10-250 per form; every Nylocas and the Maiden and Blood spawn 0), so
  a heavy split changes no ToB roll; the tree's single `rangedefence` equals the heavy value
  there. Still open tree-wide (an npc whose heavy differs needs the param first).

## From seam41 tob_room_clear_restore (matthew-mbp-m4-camera-b1-seam41, 2026-10-06)

- FIXED, CONTENT: the room-clear restore reached one raider. Source, quoted: "When each boss
  is killed, the [[hitpoints]], [[prayer points]], [[run energy]] and [[special attack]]
  energy of all team members are restored to full (drained stats will '''not''' be
  restored)." (wiki_Theatre_of_Blood_Strategies.wikitext:537; the live page, fetched by name
  2026-10-06, carries the same line at :545). Entry's own page says the same of its subset:
  "In Entry Mode, your Hitpoints and Prayer are replenished after defeating each boss"
  (wiki_Theatre_of_Blood_Entry_Mode.wikitext:22), and a guide video: "Once you defeat her,
  all of your stats are fully restored automatically" (transcripts/yt_M1t2qWMbzEs.md:37).
  The seam39 relay's reading "Normal should restore nobody, Entry everybody" has no line
  behind it: the Strategies page is the Normal page and says all four, for all members.
  `~tob_room_cleared` (tob_raid.rs2) ran `~tob_restore` on whichever raider's watchdog
  noticed the corpse (the Maiden's: her hero), so the members walked on with what they had.
  Now `~tob_restore_party`: the noticer, then every orb-slot uid once (p_finduid, still in
  this raid by active flag and handle), as `~tob_room_cleared_announce` walks them. The cage
  path (`~tob_spectate_tick`) and the vault (`~tob_vault_award`) were already per raider.
  - BEFORE (seam39 relay, old content, build/quest_gate/svbplaynorma): Maiden npc_death tick
    582; tick 591 pid 0 hp 58->99 prayer 92->99; pid 1 prayer 84 and pid 2 hp 71 prayer 83
    unchanged.
  - AFTER (r41spec2p3, `_play_bloat` copy, --party 3, members' special energy set to 0 before
    the fight): Bloat npc_death 327; tick 330 p1 hp 96 pr 82 spec 0, p2 hp 97 pr 82 spec 0;
    tick 331 p0, p1, p2 all hp 99, prayer 99, spec 1000. A brew's boost above base is kept
    (r41bloatp3 tick 357: p1/p2 hp 120 stay 120, prayer 80 -> 99), which is "drained stats
    will not be restored" read the other way: the restore heals up to base, never down.
    Run energy is the same proc's `healenergy(10000)`; the raider row has no run column.
  - Rows changed: none in `seed_survey.py _play_entry --names 3` (3 of 3, 96 rows, 0 of 97
    ledger rows differ from the pre-fix ledgers: a solo raider was always the noticer, so its
    supplies_left row does not move). `_play_bloat --party 3`: 5 of 5 green; in every name
    the members' prayer goes 69-82 -> 99 on the leader's tick (death + 4). The Normal relay
    (_play_normal) was not re-run: its members now reach Bloat restored, which is the
    supply budget the relay's open issue asked about.
- FIXED, CONTENT (behaviour not yet driven): the supply chest's points were the TEAM's.
  Source: "These supplies can be purchased with points, which are earned based on the
  individual performance of each player: Above average: 10-13 points, Average: 8-11 points,
  Below average: 6-9 points. The points earned can carry over to the second chest within
  the Theatre." (wiki_Theatre_of_Blood_Strategies.wikitext:568-574); "the amount of points a
  player is given can range from 6 to 13" (wiki_Chest__Theatre_of_Blood_.wikitext:20). Ours
  kept the balance in one instance register (`^tob_var_points`), so in a trio each raider's
  first Open paid into one pool any of them could spend, and banded on the team's death
  count (`^tob_var_deaths_room`), which the first Open zeroed (the second raider always drew
  the deathless band). Now: the balance is the player's own `%varp6844_tob_points` (tob.varp
  "one running balance for the raid", cleared at raid start and now also for a joiner in
  `~tob_join_raid`, with `%varp6840_tob_died_in`); the band reads the player's own death bits
  for the two rooms the chest covers (`~tob_chest_own_deaths`). OPEN: a party run that opens
  the chest. Scratch r41chest.lua (three runs, r41chestp3 / r41chest2p3 / r41chest3p3) found
  the chest but never opened it: from the arena it is not framed, and after the west-barrier
  walk the loc was out of the client's entity pool. The next try takes `_play_entry.lua`'s
  POST.bloat + take_chest whole in a Normal party and reads `::tobstores points=` per raider
  (expected: each 6-13, not a sum). The "below average" band is now unreached (one raider's
  own deaths are 0, 1 or 2 = the onion); what "below average" measures is unpublished (M22).
  **CLOSED 2026-10-07 content_bugs: driven in a party, each balance is the raider's own.** Probe
  build/seam_state/content_bugs/probes/cb_chest_party.lua (the HEAD _play_bloat fight, then every seat opens
  the chest and reads `::tobstores`), run build/quest_gate/cb_chest3 (Normal trio, Bloat killed tick 273):
  p1 13, p2 12, p3 10 points, each inside the wiki's 6..13 ("the amount of points a player is given can range
  from 6 to 13", wiki_Chest__Theatre_of_Blood_.wikitext:20), none a sum. Found on the way (cb_chest1/2, p2/p3
  read 0): the chest (local 5,33) stands one tile from Bloat's exit (5,31)-(5,32), and `~tob_exit_walked`
  (tob_raid.rs2, `^tob_exit_reach = 1`) moves the WHOLE party on when any raider steps within a tile of it; a
  raider who walks to the chest's south-east side leaves the room for the two who have not opened it yet. Filed
  below as its own OPEN row (needs a source for who the passage moves).
- CHECKED, no change: where the Normal chest stands. "It is accessible twice per raid - once
  after killing the Pestilent Bloat, and once after killing Sotetseg" (Chest page :20; the
  main page :61 "After Bloat and Sotetseg, players have an opportunity to buy supplies ...
  using points earned within the raid at a supply chest"). `~tob_chest_place` places it on
  those two clears only; Normal is a points store (8 items, enum 1952/1953), Entry gives 10
  bandages. The relay fixer's "Nothing refills before Bloat" is the game's own budget.

## From seam41 ops_player_bound_and_kept_rows (matthew-mbp-m4-camera-b1-seam41, 2026-10-06)

- FIXED, ENGINE: `p_oploc`, `p_opobj`, `p_opplayer` re-issued the interaction on
  `srv->active_player` (whoever the phase last selected), not the script's bound player
  (seam38's OPEN row above). Source: Engine-TS `src/engine/script/handlers/PlayerOps.ts:389-403`
  `state.activePlayer.stopAction(); ... state.activePlayer.setInteraction(Interaction.SCRIPT,
  state.activeLoc, ServerTriggerType.APLOC1 + type)`, the same `state.activePlayer` at :998-1014
  (P_OPOBJ) and :1017-1028 (P_OPPLAYER, target `state._activePlayer2`), where
  `ScriptState.ts:214` `activePlayer = this.intOperand === 0 ? this._activePlayer :
  this._activePlayer2` is the bound player. `torirs_server_ops_player.c` now reads
  `SSVM_Active(state, SSVM_ENT_PLAYER)`, binds it to the world around the op
  (`ops_player_bind_world` / `_unbind_world`, the shape of seam38's
  `script_world_bind_player`; it also binds that player's scene window, where the loc handle
  resolves), resolves the multiloc transform against that player, and compares
  `p_opplayer`'s target with the bound actor. Proof on a stand-in (no content caller exists):
  a scratch debugproc `::s41oploc` (compiled only into a scratch pack via
  TORIRSSERVER_SCRIPTS, never into the tree) queues a script on the leader that
  `p_finduid`-binds the other raider and calls `p_oploc(1)` on poordoor 3226,3223. Party of
  2: BEFORE (HEAD binary, s41oplocB) the LEADER walked 3222,3218 -> 3226,3222 and opened the
  door (ticklog `input` t8 pid 0 `[oploc1,poordoor]`), the member stayed at 3220,3214;
  AFTER (s41oplocA) the MEMBER walked 3220,3214 -> 3226,3222 and opened it (`input` t10
  pid 1 `[oploc1,poordoor]`, loc_set t10 1535 -> 1536), the leader stayed. Server selftest
  11 failures before and after, the same 11 rows (farming rake and firemaking, the
  `p_oploc`/`p_opobj` self re-issues, pass).
- OPEN, ENGINE (same file, same shape, no known raid caller): `remote_view_start` /
  `remote_view_end` still act on `srv->active_player`
  (torirs_server_ops_player.c, `SS_OP_REMOTE_VIEW_START`/`_END`); a remote view started from
  a script bound to another player would open on the wrong one.
  **FIXED 2026-10-07 content_bugs (engine, torirs_server_ops_player.c):** both now act on
  `SSVM_Active(state, SSVM_ENT_PLAYER)` (asserted; the opcodes require the protected active player,
  ss_meta_test.c:296-298), the source being the same Engine-TS rule as p_oploc above
  (ScriptState.ts:214 `activePlayer` is the bound one). The view is player-scoped
  (`ToriRSServer_WorldRemoteViewStart(player, ...)` touches only that player), so no world bind. Every
  content caller (poh_portal_nexus.rs2, poh_portal_chamber_functions.rs2) runs in the viewer's own script,
  where the two pointers are one player: behaviour-identical there; no stand-in proof run.
- FIXED, KEPT TEST ROW: tob_bloat.lua `tech.protect_from_missiles` (and the
  `spec.bloat.fly_prayer` reading built on the same window) counted a walking tick as
  shielded when prayer POINTS were above 0, so flies before the first press counted as
  prayed. Source: wiki_Pestilent_Bloat.wikitext:90 "[[Protect from Missiles]] reduces damage
  from the flies by 25%." The window now reads the lit set (`t.prayer.read()`,
  `set.protectfrommissiles`). Kept run (own name): green 94 of 94, "93 fly hits ... first
  lit at tick 131, the largest was 6"; spec.bloat.fly_prayer 3-6 over 93 hits.
- FIXED, KEPT TEST ROW: tob_verzik.lua `spec.verzik.reds_attacks_between` counted the casts
  plus the summon. Source: Blert VerzikDataTracker.java:71 `P2_ATTACKS_PER_REDS = 7`, reset
  at the spawn (:846 `verzikAttacksUntilSpecial = P2_ATTACKS_PER_REDS`) and counted down per
  auto to "Last auto before the next reds phase" (:718-722): seven autos BETWEEN spawns. The
  row now counts casts strictly between two summons. HEAD file on this tree (s41verzikold):
  "measured 8 count ... 7 casts and the summon" FAIL; kept run (own name): "measured 7
  count ... after the summon on tick 369 and before the next summon on tick 413" PASS, 254 of
  254 rows.
- OPEN, TEST VARIANCE (not this seam's rows; the kept room tests are not reproducible run to
  run on this tree): four tob_bloat runs killed Bloat on ticks 389, 570, 277 and never (the
  HEAD file's s41bloatold, 900 loop turns); s41bloatnew2 red on `tech.eat_before_stomp`
  (hitpoints 38 on the tick before a stomp). tob_verzik's own-name run is 254 of 254 but
  gate.py finds two shots with one MD5 (213 `spec.verzik.av.p3_yellows.seq`, 215
  `...gfx_blast`); a second run of the same file (s41verziknew2) had 39 gate findings
  (p1_cap, p3 melee predicate, tornado pct ...).
- OPEN, CONTENT or MEASUREMENT (found, not this seam's file): `spec.verzik.reds_threshold`
  read the summon at 22.0 percent of her P2 pool (s41verzikold, tick 438) and at 43.2 percent
  (s41verziknew2, tick 461), against Blert's 35. Since seam36 the 35 percent test runs on the
  attack slot only (tob_verzik.rs2 `[proc,tob_verzik_reds_due]`); a summon above 35 percent
  is not explained by that, so either the `::tobboss` phase_hp read or the trigger is wrong.
  **CLOSED 2026-10-07 content_bugs, the trigger is the source's (no content change).** The summon is
  `~tob_verzik_reds_due` on an attack slot: the first slot where `hp * 100 <= pool * 35` of P2's own
  pool (tob_verzik.rs2 `[proc,tob_verzik_left_at_or_below]`, `^tob_verzik_p2_reds_pct = 35` [wiki]),
  "the first set on the slot after she crosses 35% (4 ticks after the attack before it in 62 of 62
  recorded rooms)" (Blert, the code's own citation). A read under 35 is a big hit between slots; the
  kept test now reads both sides of the last hit: build/quest_gate/cb_vz1 (tob_verzik, HEAD test, own
  run) PASS "41.2 percent just before it ... 28.5 percent" at the summon on tick 387 (one 51 hit
  crossed 35). The 43.2 percent of s41verziknew2 is not reproduced and is above the line the code
  tests on every slot; it was read before the test subtracted the absorb window's heals (seam41).
## From seam40 play_tob_maiden_follows_blert (matthew-mbp-m4-raid-b1-seam40, 2026-10-06)

### Maiden blackstorm: the tie-break hands one raider every storm (Blert says the dps share them)

Content: `tob_maiden.rs2 [proc,tob_maiden_blackstorm]` targets "the closest player to her centre by Chebyshev distance. In the case of a tie, higher orb order takes the hit" [mc]. The centre is (+3,+3) from her SW tile, so EVERY tile on her north face (z+6) and her east face (x+6) is 3 from it. Two scythe raiders on her north-east corner therefore always tie, and orb order gives the same raider every storm. Seam40 runs m40a-m40k, with both seats on her edge: the leader took 19-26 of 19-26 storms and died twice.

Blert (`docs/minigames/theater_of_blood/sources/blert_api/reference/maiden_normal_3.json`, 24 Normal trio rooms) measures who she targets within each room. role.dps1.boss_targeted_pct is 38 [5.6-58.8] and role.dps2 is 41.45 [18.8-64.3]: the two dps share the storms in every room. If ties were broken by a fixed orb order, most rooms would read near 0 / 100. role.freezer is 19.95 [6.2-50], with the freezer at dist_boss 10 in phases 100/70.

Open: what breaks the tie in the real game? Candidates are random, last-attacker, and a different centre. No source has been found. The plan works around it: seat 1 steps one tile off on every other attack ("THE SHARED TANK"). Grade: the [mc] sentence is D; Blert's split contradicts a deterministic orb tie-break (B).

**CLOSED 2026-10-07 (settled by owner_tob_normal, progress.md "STORM TILES, streams", 20:15): no content bug.** Over all 293 recorded storms (24 rooms, stormtiles.py) the target is the raider nearest her centre (SW +3,+3) by Chebyshev distance with the orb-order tie: 293 of 293; a north/east tie-break also 100 % (it never decides one), centre SW+2.5 97.3 %. The share split above is where the raiders STAND (in all 24 rooms the freezer is orb 0, the leader, and takes the ties from her east edge in her 30 form), not a random tie. `~tob_maiden_blackstorm` already does exactly this (`~tob_maiden_centre` = SW + size/2, `distance`, orb tie).

### Maiden Normal trio: our room runs about 1.7x the real length, and specials are not the whole gap

Measured with the seam40 plan, which follows the reference's weapons, stands and cadence. Five names: room 259 / 262 / 305 / 261 (one leader died). The reference is 157.5 [132-204].

Swing rates match. Phase 100 attack counts are inside the reference: role.dps*.phase.100.attacks_boss 8-9 against 7.5 [6-15]. Our scythe swings land at a 5-tick cadence.

Damage per swing is close in phase 100. A scythe swing on her in phase 100 averages 36.7 (42 three-hit swings). Isolated real scythe swings in the Blert cached streams average 39.2 (n=12). In the crab phases ours falls to 29-35 a swing, while the real per-boss-attack average is 44.5-49.5 (Blert crab phases, 27 rooms). The real teams use a special in many crab phases: ZCB in dps*|70 for 8-10 rooms, CLAW and CHALLY at 30, DINHS at 50. The plan uses none after the opener.

Heals: ours 568-1128, the reference 223.5 [0-821]. N1 alone heals 150 in most 70 percent waves; the reference's phase.70.boss_heal is 1 [0-226].

Not a content claim yet: no single number here is shown wrong. Candidates to measure next:
- the scythe's accuracy against her (zero share 19-23 % after 1-3 successful hammers, where about 11 % is expected at Defence 69-140);
- the specials the real teams use.

## From seam40 play_tob_nylocas_follows_blert (matthew-mbp-m4-raid-b1-seam40, 2026-10-06)

### Nylocas: wave stall from wave 11 that Blert trios never see (seam40, UNSOURCED rule)

Evidence: reference nylocas_normal_3.json (27 Normal trio rooms) and ny40/ny_waves.py vs ny_ourwaves.py.
Blert trios have 14 copies alive right after a spawn and no stalled wave before w28; wave 31 at 260 [244-293].
Our content stalls a wave whenever 12 are alive: w12 +11, w17 +31, w20 +47, w31 +67 ticks (w31 299-335 over 5 names),
with the same waves 1-10 timing as Blert. Either the cap value, what it counts (e.g. bigs as one, eggs/splits),
or when it is tested differs from the game. No source pinned here: check the OSRS wiki Nylocas page
(The Nylocas, "wave" / "cap") before changing minigame_tob's spawn loop.

### Nylocas: attacks per killed small 1.34-1.40 vs Blert 1.13 (71% one attack)

Greys are the gap (death age 23 vs 16; 15.8 popped per room vs 9.9; big greys popped 4.6 vs 0.3).
Trace ny40j: a whip on a chewing grey hit 0 four times in a row (hitsplat 26) t170-183. Check the small
nylocas' defence/accuracy against the wiki stat block and whether a copy attacking a pillar blocks hits.

### Vasilias: 12.5 hp per tick vs Blert 20.3 (24.7 a swing vs ~34) with Blert's weapons on her forms.

### Mage level 112 in Blert = Imbued heart; no heart in content (the plan drinks a magic potion, 103).

### Bloat stomp reach is anchored on the SOUTH-WEST tile (tob_bloat.rs2:823 huntall(npc_coord, ^tob_bloat_stomp_range = 6))
Status: OPEN (seam42 closer). Not applied: the plan's in_stomp (raid_play_tob_bloat.lua) reads the same south-west anchor, so the content and the plan must change together, with a run; the radius is still the closer's guess from Blert data.
**CLOSED 2026-10-07 content_bugs: FIXED by seam48** (row "Bloat stomp reach: centre-based (landed, seam48)" below). HEAD tob_bloat.rs2:831 `huntall(movecoord(npc_coord, 2, 0, 2), ^tob_bloat_stomp_range, 0)` with `^tob_bloat_stomp_range = 5` (tob.constant:973): footprint + 3 on every side, the Blert data (footprint+2 23/27, +3 4/10, >=4 0/10); raid_play_tob_bloat.lua:434 `in_stomp` reads the same centre (`P.stomp_centre`). No wiki or cache number exists (wiki_Pestilent_Bloat.wikitext:92 "it will stomp the surrounding area"; Strategies:675 none), so Blert stays the source. Nothing changed here.
Source: Blert, 27 recorded Normal trio downs (build/blert/bloat, seam42 reader bloat/blert_bloat_reference.py):
a raider at Chebyshev distance <=2 from Bloat's 5x5 footprint at T+28 took the stomp 23 of 27 times, at 3 4 of 10,
at >=4 0 of 10, on every side.  Ours reaches footprint distance 6 on the south and west faces and only 2 on the
north and east faces (radius 6 round the south-west corner).  The tob.constant comment says [M65] "no source gives
a number".  Proposed (the closer decides the radius; the data puts it at footprint+2..3):
    huntall(movecoord(npc_coord, 2, 0, 2), 5, 0);   // centre tile, footprint + 3
or radius 4 (footprint + 2).  The plan's in_stomp (raid_play_tob_bloat.lua) reads stomp_range from the
south-west tile like the content; change both together.  UNPROVED by a run in this pass (no content edit made).

### From raid seam42 play_tob_xarpus_follows_blert -- Xarpus Normal trio phase 3 is short (UNSOURCED, Open)
Status: OPEN (seam42 closer): unsourced; measure first, as the entry says.

Evidence: Blert reference/xarpus_normal_3.json (13 death-free Regular trio rooms):
outcome.phase.phase2.ticks (the screech to his death) 51 [45-70]; role.melee*.phase.phase2.attacks_boss
9-10 [8-12] per raider, mostly scythe (weapons.melee*|phase2.SCYTHE 8-9). Ours (seed_survey.py
_play_xarpus --party 3 --names 5, seam42 final): 36-44 ticks on 7-9 swings a raider, 0 retaliations.
Fewer swings and a shorter phase means each swing takes more of him in ours, or he has less left at
the screech (ours screeches on the first grid slot after the bar reads <= 25%: plan screech_pct 27.5
with the margin; X xarpus.p3.screech_pct 22.5 for Entry, W:851 "~25%"). Not fixed: no source in hand
says which (his Normal hit points at the screech, or the scythe's three hits on a 5x5 in phase 3);
measure the screech hit points from the npc row against Blert's npc hitpoints (event 8 at the
xarpusPhase 2 tick) before changing anything.
**CLOSED 2026-10-07 content_bugs as content (measured, no change).** (1) The screech point: Blert
reference/xarpus_normal_3.json (13 rooms) output.phase.phase2.boss_pct_per_tick 0.517 x ticks 51 = 26.4 %
of his pool dealt from the screech to his death (19.71 hp a tick x 51 = 1005 of 3750, with a median 94 hp
healed inside it), against "Xarpus screeches once at 25% of his health" (wiki_Xarpus.wikitext:258) and
"Upon reaching ~25% of his health" (Strategies:851); ours screeches at the first slot under 25 %: the same
point within the heal. (2) His P3 stats are the wiki infobox's: tob.npc `[tob_xarpus_combat]` defence 250,
ranged 100, magic 220, hitpoints 5000 scaled (wiki_Xarpus.wikitext:122-145, id 8340, dstab/dslash/dcrush 0);
the melee formula and the scythe's three hits on a 5x5 were settled in seam52. (3) What is left is the
PLAY: ours ~23 hp a tick (937 in 36-44 ticks) against Blert's 19.7, because the real teams lose swings to
the quadrant dance ("He will rotate every 8 ticks ... 1/2 from a 5-tick weapon", Strategies:853; "will
immediately look at the direction from which he was attacked", wiki_Xarpus.wikitext:265) and ours took 0
retaliations. STILL OPEN on a source: what heals him a median 94 (25-139) inside Blert's P3 window; no
page names a P3 heal (an exhumed still absorbing at the screech is the likely reading; unmeasured).
## From seam46 nylocas_waves_and_vasilias_rows (matthew-mbp-m4-raid-b1-seam46, 2026-10-06)

Settles the four seam40 Nylocas rows above against their sources. None is a content defect in
minigame_tob: every number the rows suspected matches its source, and no content file changed.

### Nylocas wave stall from wave 11 -- CLOSED, content matches the source (no change)

The cap, what it counts and when it is tested are all sourced and our content has all three:
- value: "Before wave 20, the cap is 12 nylocas; afterwards, the cap is doubled to 24."
  (wiki_Theatre_of_Blood_Strategies_Nylocas.wikitext:3); blert `waveCap()` is
  `currentWave < CAP_INCREASE_WAVE ? 12 : 24` (Hard 15) (blert_plugin/NylocasDataTracker.java:88-92).
  No source scales it with the party. Ours: tob.constant `^tob_nylo_cap_early` 12, `_hard` 15, `_late` 24,
  `^tob_nylo_cap_increase_wave` 20.
- count: blert `roomNyloCount()` is `nylosInRoom.size()` (+3 for a Prinkipas), a nylo leaving the map only
  on its DESPAWN (NylocasDataTracker.java:84-86, :215): splits and bigs count one each, a corpse counts until
  it despawns. Ours: `~tob_nylo_count` (tob_nylocas.rs2), the same.
- test: on the tick the wave is due, `roomNyloCount() >= waveCap()` is a stall (NylocasDataTracker.java:123);
  ours `~tob_nylo_count >= ~tob_nylo_cap(...)` (tob_nylocas.rs2 `[proc,tob_nylo_wave_tick]`), retried every
  4-tick cycle.
Blert's "14 alive right after a spawn" is a room under 12 on the due tick plus the 2-4 the wave adds, so the
same cap. Our stalls from w11 are the room holding 12 on the due tick because copies die slower (the grey
row below and the plan's weapon switching), not a different rule. Spec rows nylocas.cap_pre20 / cap_post20 /
cap_pre20_hard / cap_increase_gate already carry it (grade B/C).

### Small greys 1.34-1.40 attacks per kill -- CLOSED as content; the zeros are NULLING (sourced) after a plan misclick

- Stat block matches: wiki Nylocas Ischyros `def1 = 1`, `dstab..dheavy = 0`, `hitpoints1 = 11`, `scaledhp = Yes`
  (wiki_Nylocas_Ischyros.wikitext:29-75); cache 8342 has no stat2 and no defence params
  (cache_npc_nylocas.txt:1-20); tob.npc `[tob_nylocas_incoming_melee]` defence=1; trio hp 8 / 9 / 11
  (tob.constant `^tob_nylo_small_hp_3/4/5`). Big: wiki def2 20, cache stat2=20, tob.npc 20.
- A pillar-chewer is hittable: nothing in tob_nylocas.rs2 or tob_damage.rs2 blocks a hit on a copy that is
  biting a support.
- The four zeros in ny40j are NULLING. build/quest_gate/ny40j/ticklog.tsv: raider 2 `tgt 1096` holding 12926
  (toxic blowpipe), player_anim 5061 at t154 -> `hit_npc 1096 8342 0 26` at t157 (ranged on an Ischyros); from
  t157 raider 2 holds 4151 (whip), anims 1658 at t170/174/178/182 -> `hit_npc 1096 8342 0 26` at
  t171/175/179/183. The rule is the wiki's: "Each spider is immune to damage outside of their combat style ...
  If the nylocas is attacked with a wrong style, the player that attacked them can no longer damage them."
  (wiki_Theatre_of_Blood_Strategies.wikitext:724), implemented in tob_damage.rs2 `~tob_nylo_nulled_here`.
  OPEN, DRIVER (not content): the trio plan fires its previous weapon at a newly chosen target before the
  colour's weapon is equipped, which nulls that copy for the seat for life. The plan must equip the colour's
  weapon BEFORE the attack click on a new target (raid driver / _play_nylocas, not minigame_tob).

### Vasilias 12.5 hp per tick vs Blert 20.3 -- CLOSED as content (stats match); per-swing gap OPEN (player side)

- wiki Nylocas Vasilias `def = 50`, `dstab..dheavy = 0`, `hitpoints2 = 2500`, `scaledhp = Yes`
  (wiki_Nylocas_Vasilias.wikitext:31-68); cache 8355 stat2=50, stab/slash/crush/magicdefence 0
  (cache_npc_nylocas.txt:306-336); tob.npc `[nylocas_boss_melee/_magic/_ranged]` defence=50; tob.constant
  `^tob_vasilias_hp_3/4/5` 1875 / 2187 / 2500. Nothing to change.
- ny40j: 175 hits on her, 34 at 0 (19%), spread evenly over the 10-tick window (dt0 3/13, dt3 8/40, dt9 6/19),
  so wrong-style reflects at the turn are not the gap. With def 50 and no bonuses a 118-attack scythe should
  almost never miss: the gap is on the player side (boosts -- see the heart row -- prayer, the scythe's three
  hits on a size-4, or accuracy), not in her content. OPEN with that evidence; no source settles it here.
- **CLOSED 2026-10-07 content_bugs (player side checked, matches).** Her block: wiki_Nylocas_Vasilias.wikitext:18
  `size = 4`, :41 `def = 50`, defence bonuses 0; all.npc nylocas_boss_melee/_magic/_ranged size=4 stat2=50. The
  scythe on a 3x3-or-larger target takes three independently rolled hits at 100/50/25 % (wiki_Scythe_of_vitur
  :81-85; gear/scythe_of_vitur.rs2 header, size 3+ three hits, unchanged since seam38). The player formula is the
  wiki's to the term (seam52 "Settled: the melee formula is not short"; its 'NOT THE FORMULA' line puts her melee-form
  expectation at 41-42 a swing). The 21-25 a swing is the room (swings into the wrong form, reflected, across a form
  change): a DRIVER/plan question, not a content row.

### Mage level 112 in Blert -- the heart EXISTS in content (correction of the seam40 row)

skill_slayer/scripts/imbued_heart.rs2: `[opheld1,imbued_heart]` -> `stat_boost(magic, 1, 10)` (99 -> 109) and
`[opheld1,saturated_heart]` -> `stat_boost(magic, 4, 10)` (99 -> 112). Blert's 112 is the SATURATED heart
exactly. OPEN, DRIVER (plan): give the mage seat a saturated heart and invigorate at the door instead of the
magic potion (103). Not a content row.

### Seen by seam45 (Verzik Normal trio, melee) -- rows to triage, nothing edited

- DRIVER/CLIENT (C, src/plugin): `api_drive.npcs` rows for Verzik's P3 tornadoes (8386) mostly stay on the
  spawn tile while the server walks them (s45 e8 enrage log: rows "6431,91;6432,91;6432,90" for 10+ ticks
  while the tick log walked T81 6431,91 -> 6431,98); some rows do move. The plan cannot dodge what it
  cannot see; it dodges only rows seen moving (raid_play_tob_verzik.lua QD.RAID_PLAY_VERZIK_DODGE note).
  Same finding as s31 vz31d, still open.
- CONTENT, unsourced either way: a P2/P3 nylocas blasts on EVERY ending, killed included
  (tob_verzik.rs2 ~tob_verzik_crab_blast, after Near Reality's onFinish); the wiki sentence quoted there
  is "explode if they reach their target". A melee raider can never kill one without taking up to 63.
- CONTENT (spec V verzik.p3_auto_max): a PRAYED P3 auto still deals up to 16-17 to every raider; the
  melee trio takes 8-12 eats in P3 against Blert's 0-6. If OSRS blocks it fully, hp lost per raider
  (232-566 here, Blert 69-259) is mostly this and P2's urnbomb/zap.
- CONTENT: the P3 tornado reaches a raider on her east edge (round her body, 12+ tiles) about every 28
  ticks per raider (respawn 16, tob.constant ^tob_verzik_p3_tornado_respawn); W:981 calls a touch "the
  off chance". A ball landing with a touch (74 + 50% of current) is lethal at any hitpoints (e18 svb).

## Seen by seam48 (member_swings_seen)

### Crystal halberd special: the large-target second hit is not implemented (pvm_dragon_halberd.rs2, sa_kind 19)
special_attack.obj gives crystal_halberd sa_kind 19 (300 energy), the dragon halberd's Sweep. The script's own header
says it implements only "a single hit with the unconditional +10% damage boost"; the second hit "if used against
'large' monsters (anything larger than 1x1)" (wiki Dragon halberd, quoted there) is NOT reproduced because the
engine has no npc_size opcode. Bloat is 5x5: the real trios' down-1 special (17 of 19 Blert Normal trios, seam42)
would do about half its damage here. Needs an npc_size opcode, then the second hit at 25% reduced accuracy.
### Bloat stomp reach: centre-based (landed, seam48)
tob_bloat.rs2 huntall(movecoord(npc_coord, 2, 0, 2), ^tob_bloat_stomp_range = 5): "it will stomp the surrounding
area" (wiki_Pestilent_Bloat.wikitext:92) + Blert's 27 downs (footprint+2: 23/27, +3: 4/10, >=4: 0/10). [M65] grade D.
The plan moved with it in the same commit (centre distance, leave margin kept at -2, the leave locked once
started): tech.leave_before_stomp PASS 3 of 3, 0 stomp hits (closer survey 2).

## From seam52 melee_damage_per_swing (matthew-mbp-m4-raid-b1-seam52, 2026-10-06)

Settled: the melee formula is not short. `combat_stats.rs2` and the scythe split follow the
wiki to the term: `~combat_effective_stat` scales the CURRENT (boosted) level by the prayer
(Piety 123, `~check_strength_prayer`) before `+ style + 8` (wiki_Maximum_melee_hit step one),
`~combat_maxhit` is `(effective * (bonus + 64) + 320) / 640` (step two, the 0.5 is the 320),
the defence roll is `(level + 9) * (bonus + 64)`, the hit test is `randominc(A) > randominc(D)`,
and `scythe_of_vitur.rs2` rolls hits 2 and 3 on their own accuracy at `scale(50)` and
`scale(25)` of the max (wiki_Scythe_of_vitur :83 "each hit will deal 50% less damage,
(rounded down), than the preceding hit"). Bloat, Verzik P2/P3 and Vasilias stats in
all.npc equal the wiki's. With `::maxmelee` (torva body/legs, rancour) the wiki expectation
on Bloat is 38.0 per swing (Reap) -- exactly the 36-39 seam51 measured. The gap to the
reference was the KIT and the reference's own number:

- KIT (fixed by a test affordance, `cheat_max_gear.rs2` `::tobkit` / `::tobkitsalve`): the
  recorded raiders (Blert equipmentDeltas, build/blert/{bloat,verzik,sotetseg}) wear radiant
  oathplate body and legs (Bloat 79/90, Verzik 66/81, Sotetseg 81/87; torva 0), and at
  Bloat a salve amulet(ei) (74/90; Bloat is undead). Wiki expectation with that kit on
  Bloat: 42.6 (Reap) / 43.8 (Chop); Verzik P2 29.3 (against 28.0 for `::maxmelee`).
  Measured on this content: seam52_melee_probe (solo Normal Bloat, `::tobkitsalve`, Reap):
  24 swings in the downs, 1005 damage, **41.9 per swing**, 7 of 72 rows zero, maxes seen
  50/26/13 within 55/27/13.
- THE REFERENCE'S "~49": the down-1 damage divided by ALL attacks, of which about 4.7 per
  room are crystal-halberd specials. Regressed per kind over 29 recorded rooms (Bloat HP
  drop in down 1 against attack counts): scythe **46** per swing, halberd special 72.5,
  claw special 50. The scythe's 46 is inside the noise of the wiki's 42.6-43.8.
- OPEN (not my file): `skill_combat/configs/combat.dbrow:72`, `weapon_scythe_table` slot 1
  ("Chop") is `^stab_style`; the wiki's Module:CombatStyles (sources/wiki_Module_CombatStyles.lua
  :547-549) has `'Chop', 'Slash', Aggressive`, and wiki_Scythe_of_vitur :41 "it does not
  have a Stab combat style". Fix: `data=damagetype,^slash_style` on line 72. Until it lands
  a raider must NOT pick style slot 1 with a scythe (stab 70 against Bloat's 40 stab
  defence); Reap (slot 0, the default) costs 1 max hit (50 against 51) and nothing else.
- OPEN (shared funnel, EV -1.2 per swing on an undead): the salve multiplies the ROLLED
  damage (`player_hit_npc_prepare.rs2` `~salve_or_black_mask_scale_target($prepared)`),
  where the wiki multiplies the MAX HIT ("Step three", wiki_Maximum_melee_hit :86-87 and
  the Salve amulet (e) row :164) and rolls under it. Same maximum; the distribution is
  lumpy and the mean slightly low (Bloat with `::tobkitsalve`: 41.5 against 42.6). Moving
  it touches every caller of `~player_hit_npc_prepare` (ranged, magic, every special) --
  left for a seam that owns them. **FIXED 2026-10-07 content_bugs (OSRS-Content b8118fbdfd)**:
  `[proc,player_maxhit_vs_npc]` (gear/salve_amulet.rs2) scales `%varp6287_com_maxhit` by the
  salve (else the black mask / slayer helmet) after the two target-bound recomputes a swing
  rolls from (`[label,player_combat_start]`, `~player_melee_swing`); every melee and ranged
  roll site and every special reads that varp, specials multiply after it (the page's order).
  `~player_hit_npc_prepare` keeps the factor for MAGIC only (a spell's or powered staff's own
  maximum). Source: wiki_Maximum_melee_hit.wikitext:86-87 "Max Hit = floor(floor(Base Damage)
  x Special Bonus)", table :161-166 "Salve amulet (undead) 7/6 ... Salve amulet (e) 1.2".
  Proof (build/quest_gate/cb_staff_salve1, salve(e), whip, Deviant spectre): 7 of 47 landed
  hits are 5 mod 6 (5, 11, 17, 23); floor(r*6/5) is never 5 mod 6, so roll scaling could not
  land one. Disclosed: a multi-target swing's secondaries take the primary's maximum.
- NOT THE FORMULA: Vasilias's 21 per swing against Blert's 34 (seam51 nylocas) -- the wiki
  expectation in its melee form is 41-42 per swing with either kit; the shortfall is the
  room (swings into the wrong form, reflected, or across a form change), not the swing.
  Verzik P2's 43% zero splats: the formula gives 39% (`::maxmelee`) / 36% (`::tobkit`) per
  row; the rest are the heal window's zeros (`tob_damage.rs2` `~tob_prepare_player_hit`).

## 2026-10-06 seam55 (orchestrator, owner: "This needs to be fixed. Fix it now"): the Maiden's blackstorm landed exactly its max

`tob_maiden.rs2 ~tob_maiden_hit_damage` returned `36.5 + 3.5c` (halved by Entry and by Protect from Magic)
on every storm. The Strategies page's "deals damage equal to 36.5 + 3.5c" (wiki_Theatre_of_Blood_Strategies
.wikitext:590) is the MAX: the Maiden's infobox lists max hit 36 (18 in Entry; wiki_The_Maiden_of_Sugadinti
.wikitext:39-44) and the 112 recorded Normal trio storms on the Blert raiders have a median of 12.5 and a
range of 0 to 62 (blert_api/reference/maiden_normal_3.json boss.hit_on_recorder.maiden_auto). Fixed: the
damage is `random(max + 1)`, still with no accuracy roll ("always lands as a successful hit") and the over-hit
kept. Effect before the fix: a trio lost 170-390 hitpoints at Maiden against the recorded 60-106, ate its pack
empty and reached Bloat with nothing (seam53 relay, seam54 Maiden). maiden.tsv auto_damage_base reworded.

## 2026-10-06 seam55 (orchestrator, from the Nylocas fixer's combat-tab probe): powered staves show the melee staff's styles

The combat tab shows Bash / Pound / - / Focus for the Eye of Ayak (and so for every powered staff: there is no
`weapon_powered_staff_table` in skill_combat/configs/combat.dbrow, only `weapon_staff_table`, and all.obj gives
the Ayak and the Sanguinesti staff the same category 1 / param_1564 3). OSRS: a powered staff's styles are
Accurate / Accurate / Longrange (wiki_Eye_of_Ayak.wikitext:71 "combatstyle = Powered Staff") and every style
casts its built-in spell. The damage TYPE was already made magic for powered staves (seam36 content row), so the
seat still casts; the LABELS and the Longrange option are wrong, and `t.ui.style("Accurate")` cannot find a
button on these weapons (the relay picks "Pound" for the Ayak seat for now, test/raids/_play_normal.lua). OPEN:
add the powered-staff table and map the powered staves to it in combat_stats.rs2, then the harnesses pick
"Accurate".

**FIXED 2026-10-07 content_bugs (OSRS-Content b8118fbdfd).** Every `~powered_staff_is` weapon now writes
varbit 357 = 24 (`^weapon_type_powered_staff`, the cache's DBTable 78 row `combat_interface_staff_selfpowering`:
"Accurate / Accurate / Longrange") and rolls `weapon_powered_staff_table` (combat.dbrow: magic, magic, magic;
`^style_magic_powered_accurate` x2, `^style_magic_powered_longrange`). Sources: wiki_Module_CombatStyles.lua:485-502
'Powered Staff'; "combatstyle = Powered Staff" on Eye_of_Ayak:71, Sanguinesti_staff:67, Tumeken_s_shadow:66,
Trident_of_the_Seas:80, Trident_of_the_Swamp:69, Warped_sceptre:64, Thammaron_s_sceptre:67, Accursed_sceptre:64,
Bone_staff:49, Dawnbringer:45 (pages fetched into sources/ by name). The styles' invisible bonuses came with it:
"+3 if using Accurate on powered staves, or +1 if using Longrange ... +8" (wiki_Damage_per_second_Magic.wikitext:20-24),
Longrange "+3 invisible bonus to their Defence level" (wiki_Combat_Options.wikitext:46); and a SPELL now gets no
style bonus ("autocasting does not give invisible bonuses", :49): the flat +1 every cast carried
(LostCity_Content2 player_combat_stat.rs2:73) is gone, so every spell's accuracy roll is one effective level lower
(about 1 percent). Probe build/quest_gate/cb_staff_salve1: varbit 357 = 24; `t.ui.style("Accurate")` slot 0,
`"Longrange"` slot 3, `"Pound"` no_style. HARNESS CHANGE: a seat that picks "Pound" on a powered staff must
pick "Accurate" (told the Nylocas and Maiden owners). Not changed: Longrange's XP split (the powered staff
still gives 2 Magic XP a damage on every style; wiki_Powered_staff.wikitext:8 and :132 say only that Longrange
gives Defence XP, no split).

## 2026-10-06 owner_tob_normal: the Matomenos walked to the nearest tile of Maiden, not her south-east tile

`tob_maiden.rs2 [proc,tob_maiden_crab_goal]` clamped the crab's tile into her 6x6 (the nearest tile), so a crab
from the north row walked along her TOP row to her north-east corner (ours: a north 4 at (23,10) on (16,5) at +6,
then west along z+5 to (6,5)). The recorded rooms walk every crab to her SOUTH-EAST tile: in the 26 Blert
Regular trio Maiden streams (build/blert_maiden, NPC_UPDATE per tick, tiles from her SW tile) the north 4 at
(23,10) walks (22,9) (21,8) ... (14,1) (13,0) and then west along her bottom row to (7,0); the north 3 at (19,12)
reaches (8,1) at +11 beside the south 3 at (8,0) -- "3s and 4s are the nylocas which spawn on the eastern side of
the arena, and will clump together 11 ticks and 16 ticks after spawning" (wiki_Theatre_of_Blood_Strategies
.wikitext:520), "frozen on top of each other in front of Maiden" (:637); Mc's "the crabs will walk towards
Maiden's southeast tile" was the rule the clamp replaced. FIXED: the goal is her south-east tile again (the
nearest-tile clamp procs stay for `::tobrun`'s law check). Pack compiled.

## 2026-10-06 owner_tob_normal: a Matomenos took its first step on the tick it spawned

Ours stood one tile in on its spawn tick (pos 5 spawned at (19,12) read (18,11) on the spawn tick); every
recorded crab is still on its spawn tile on its NPC_SPAWN tick and one tile in a tick later (Blert pos 5:
+1 (18,11), +2 (17,10) ... +11 (8,1)), so ours reached every tile -- and her -- a tick early. FIXED:
`^tob_var_maiden_crab_clock` (tob.constant, slot 9, unused in her room) holds the map_clock of the last
70/50/30 spawn and `~tob_maiden_crab_tick` takes no step on it. Arrival ticks now match Blert's leaks (pos 0/1
at +6, 2/3 at +9, 5 at +13, 6-9 at +17). Pack compiled.

## 2026-10-06 owner_tob_normal: the engine walked a ranged/magic weapon to its BARE cache reach, not the content's

`torirs_server_combat.c player_weapon_attackrange` read the obj's `weapon_attackrange` param, while content's
`[apnpc2,_]` fires from `~player_attackrange` (skill_combat/combat.rs2), which adds two on Longrange ("Attack
range is increased by +2 tiles up to a maximum of 10 tiles", wiki_Combat_Options.wikitext:37) -- so the
approach walked a Longrange bow, or the Eye of Ayak on its Longrange slot, two tiles closer than the trigger
fired from. FIXED in the engine: the reach is `[proc,player_attackrange]`'s answer (the bare param only when
no content proc answers). And in content, the powered-staff Longrange check read `%varp43_com_mode = 3` (the
tab's fourth button, Focus) but `~player_combat_stat` clamps the mode to the style row's count on the first
swing (combat_stats.rs2:382: the melee staff row has three entries, Bash / Pound / Focus), so from the second
swing it read 2 and the reach fell back to 6. Now `>= 2`. The Eye of Ayak stays at its own reach: 6
(wiki_Eye_of_Ayak.wikitext:70 "attackrange = 6", :78 "{{CombatStyles|Powered Staff|speed=3|attackrange=6}}"),
8 on Longrange. PROBE build/seam_state/owner_tob_normal/probes/_probe_range.lua (Ayak on Focus, Attack on
Maiden from 8 off her edge): the old binary swung once from 8 and walked to 6 (`moved true, now 6437`), the
owner binary swings from 8 and stays (`moved false, 6439`; verbose "at-range ready? range=8 in_range=1"
on every ask).

## 2026-10-06 owner_tob_normal: a spell on a target out of reach -- checked, no divergence

A combat spell pressed on an npc beyond its ten tiles walks the caster into reach and casts there: PROBE
probes/_probe_cast.lua, Ice Barrage on Maiden pressed from 13 tiles off her edge, the barrage seen two ticks
later from 9 tiles off. A walk the player clicks afterwards replaces the interaction here as any click does
(the engine's OPNPC/OPLOC/MOVE handlers clear the target first, torirs_server_combat.c), so a plan that steps
off a splat re-casts after the step; nothing changed.

## 2026-10-06 owner_tob_normal: FIXED the seam52 OPEN row -- the scythe's "Chop" was stab

`skill_combat/configs/combat.dbrow` `weapon_scythe_table` slot 1 was `^stab_style`; the wiki's Module:CombatStyles
(sources/wiki_Module_CombatStyles.lua :547-549) has `'Chop', 'Slash', Aggressive` and wiki_Scythe_of_vitur :41 "it does not
have a Stab combat style". Now `^slash_style`. The Maiden scythe seats swing on Chop (+3 Strength levels at a target drained
to 0 Defence). Pack compiled.

## 2026-10-06 owner_nylocas: a powered staff's built-in spell took no equipment magic damage -- FIXED

wiki_Tumeken_s_shadow.wikitext:108 "Note that any magic damage bonuses are applied after the base max hit is
calculated" (the powered staff's own max-hit page), :76 the shadow's passive multiplies "magic damage ... from
the player's worn equipment" by three, "capped at a total of 100%". The spellbook's casts added
%varp6222_com_magicdamage (player_magic.rs2); the powered staves' attack (powered_staff.rs2
[label,player_powered_staff_attack]) never did, so an Eye of Ayak in an occult necklace hit for the bare
floor(Magic/3) - 6. Fixed: `[proc,powered_staff_magicdamage]` applies the worn bonus (tenths of a percent), x3
capped at 100% for Tumeken's shadow. Not handled: the shadow's x4 inside the Tombs of Amascut. Also noted, not
changed: all.obj gives the occult necklace magicdamage 50 (5% by the cache unit combat_stats.rs2 states) where
the wiki's occult is +10%.

### owner_tob_normal: a frozen npc was frozen again (engine npc_freeze), no immunity -- FIXED (C)
`npc_freeze` kept the longer of the two freezes, so a barrage on a Matomenos already frozen renewed it to the
full 32 ticks (svaplaymaide 724b841a9: crab 1080 hit at +1 and again at +11 stood still 50 ticks), and a crab
could be frozen again the tick it thawed. wiki_Freeze.wikitext:7 "followed by a short immunity to the Status
after which the target can be frozen again"; :9 "The immunity window for most freezes (with the exception of
Grasp spells) is 5 ticks". Fixed in torirs_server_scripts.c SS_OP_NPC_FREEZE: nothing lands while
`frozen_ticks` or the new `freeze_immune_ticks` (TORIRSSERVER_FREEZE_IMMUNITY_TICKS 5, set when the freeze runs
out, torirs_server_world.c) is above zero; a respawn clears both. Not handled: Grasp spells' 2-tick immunity
(they share the 5). The barrage's damage still lands on a frozen crab (player_magic.rs2 is unchanged).
Checked, no divergence: the freeze CHANCE (wiki_Nylocas_Matomenos.wikitext:168, scales with magic attack, 100%
at +140) is the content's accuracy roll -- `~pvm_freeze_effect` runs only on a successful
`~player_npc_hit_roll(^magic_style)`.

## 2026-10-06 (orchestrator, owner's rule): only the party leader starts a room; the barrier started it once per click

`tob_party.rs2 [oploc1,tob_arena_barrier]` asked every clicker "Yes, begin the fight." and started the room on
each answer; the question suspends the script, so three seats answering on one tick (the lockstep door,
raid_play.lua 41f763640) built the Nylocas room three times (twelve supports at room tick 3). Owner, 2026-10-06:
"Only the party leader can start a room. The non leaders can only pass the gate once the room is started."
Fixed: a member (orb slot != 0) clicking an unstarted barrier is told to wait and does not move; the leader's
answer re-reads `^tob_var_started` and a started room is only stepped through; a started room is a gate for
everyone (unchanged). The library's cross_together must now have the leader answer and the members press the
barrier only after the room has started.

### owner_tob_normal: Dinh's bulwark special hit one target -- FIXED (content d259d21c54)
pvm_dinhs_bulwark.rs2 hit its primary twice and nothing else. wiki_Dinhs_bulwark.wikitext:72 "Shield Bash, which
hits up to 10 enemies (in both PvM and PvP) in a 11x11 area around the player (thus up to five tiles away from the
player) with 20% increased accuracy"; :74 the 5% drain on every monster hit, not on the primary's doubled hit; :135
"the actual range is 11 x 11". Now: npc_findallany(coord, 5) after the primary's two hits, up to 9 more, each its
own roll and drain; multiway or an instance only, as the barrage's splash.

### owner_tob_normal: a Normal blood-spawn trail hit 10 + 2c (36-42 a tick) -- FIXED (content 35366f400e)
tob_maiden.rs2 `~tob_maiden_blood_damage` gave a Normal trail tile the splat rule `10 + 2c`; with 13-16 leaks a
trail hit 36-42 a tick, and the trio runs (sm91: sva, svd, svf) died on it three or four times in ten ticks.
wiki_Blood_spawn.wikitext:48 "In entry mode the damage is 2 to 5, and in normal mode the damage is 5 to 13."
Now a Normal trail rolls 5..13 uniform (`^tob_maiden_trail_damage_normal_min/_max`, grade D, the Entry row's shape);
pools are unchanged (10 + 2c); Hard keeps the splat rule (no source; The_Maiden_of_Sugadinti:144 only says Hard
splatters stay). Eight names after: 0 deaths (from 3), trail hits 5-13.

### owner_tob_normal: the zaryte crossbow's Evoke was a plain doubled-accuracy hit -- FIXED (content ade9c689b0)
pvm_zaryte_xbow.rs2 said the bolt guarantee was "moot -- no enchanted-bolt system exists", but enchanted_bolts.rs2
has one. wiki_Zaryte_crossbow.wikitext:108 "guarantees the special effect of any enchanted bolts used, provided that
the player lands a successful hit"; :54/:86 the passive: Ruby bolts (e) "22% of the opponent's current hitpoints with
a cap of 110" (20% / 100 without, wiki_Ruby_bolts_e.wikitext:52). Now Evoke calls `~bolt_enchant_on_hit_evoke`:
ruby always forfeits on a hit, 22% / 110 with the crossbow worn (autos too); other bolts keep their ordinary roll under
Evoke (not implemented, disclosed). Measured: the freezer's opener Evoke on Maiden hit 110.

## 2026-10-07 (owner-approved): Vasilias gave every tied target to the first player found

`tob_nylocas_boss.rs2 ~tob_vasilias_act` picked the nearest player and kept the first one found on a tie, so
with the trio at one distance (the usual case: the 27 reference rooms put all three a median 3 tiles from her)
the party leader took every attack -- svdplaynyloc's mage 17 of her 20. Blert's 27 death-free Normal trio rooms
give her 492 attacks to the three roles about equally on every form (melee 35/35/30%, magic 36/33/32%, ranged
39/35/27%), the target the strict nearest in only 260 of them. Fixed: the nearest, and among players as near a
uniform pick (a running count of the tied, each replacing the choice with odds 1 in that count). Approved by
the owner 2026-10-07. Note: the content is right that her hits pass a matching protection prayer up to 17
(wiki_Theatre_of_Blood_Strategies.wikitext, Nylocas Vasilias: "up to 70 off-prayer, and 17 if prayed against
(except melee, which is fully protected)").

## 2026-10-07 (owner's rule, every room): only the party leader starts a room -- the doors and Verzik too

Owner, 2026-10-07: "Only the leader CAN start the room. That should be every room." The arena barrier had the
rule (0b6dffc89); the other two start paths did not: `tob_party.rs2 ~tob_door_enter` (the Xarpus arena door,
the Verzik entrance door, the Nylocas walkway landings, the spectator barrier) and `tob_verzik.rs2`
`~tob_verzik_talk` / `~tob_verzik_quickstart` (talking to Verzik). Fixed: a shared `~tob_is_party_leader`
(orb slot 0); a member is told "You must wait for the party leader to start the fight." and nothing starts;
the leader's answer re-reads `^tob_var_started` after the suspending question/dialogue, so a room never starts
twice. The debug `::tobgo` is unchanged (a command, not a player path).

## 2026-10-07 (owner): nothing but the red barrier and talking to Verzik starts an encounter

Owner, 2026-10-07: "When I say start the room, I mean the red barrier and talking to Verzik to start the
encounter" and "Nothing else should start the encounter." `tob_party.rs2 ~tob_door_enter` (the Xarpus arena
door, the Verzik entrance door, the Nylocas walkway landings, the spectator barrier) asked "Yes, begin the
fight." and called `~tob_start_room` -- a divergence (doors are doors). Fixed: those doors never start an
encounter; the leader gate added there in afd14f6018 is reverted with it. The encounter starts only at the red
barrier ([oploc1,tob_arena_barrier], the party leader) and at Verzik (~tob_verzik_talk / ~tob_verzik_quickstart,
the party leader). No harness or relay step used those doors to start a room (checked: they use the barrier,
Verzik, and the exit passages).

## 2026-10-07 content_bugs: walking near a cleared room's exit moves the whole party

- OPEN, CONTENT (found by the cb_chest party probe; no source in hand). `~tob_exit_walked` (tob_raid.rs2,
  asked from the per-player room watchdog while the room is cleared) calls `~tob_advance_room` when ANY raider
  stands within `^tob_exit_reach` (1) of the room's exit, and the advance moves every raider (the "KNOWN GAP,
  PARTY" note in `~tob_advance_room`). After Bloat the supply chest (local 5,33, the wiki map pin) is one tile
  from the exit (5,31)-(5,32): cb_chest2, p2 stepped to local 6,32 beside the chest on tick 313 and on tick 314
  all three were in the Nylocas room, p2 and p3 with 0 points and the chest never opened. Needed: a source for
  what the passage does to the raiders who have not walked through (OSRS: each raider walks it on their own, as
  far as any page shows; none here says so in a line). The instance holds one room at a time, so a per-raider
  passage is an architecture change, not a constant.


## 2026-10-07 (owner-approved): two-hit melee weapons rolled one hit

The melee swing (`skill_combat/combat_stats.rs2`) rolled ONE hit for every weapon but the scythe, so sulphur
blades, glacial temotli, earthbound tecpatl and the dual macuahuitl were weaker one-hit weapons. The recorded
Normal trio meleers clear the Nylocas greys mostly with the blades (Blert: 771 attacks in 27 rooms;
owner_nylocas progress Step 18). Sources: wiki_Multi-hit_weapons.wikitext:25/32/39 "two independently rolled
hits per attack, hitting twice on the same game tick ... the combined max hit ... dividing it by two"; :54-55
and wiki_Dual_macuahuitl.wikitext:25 the macuahuitl "spaced one game tick apart ... The first hit takes half,
rounded down, and the second hit takes the remainder ... the second check will only proceed if the first
succeeds". Fixed: `gear/multihit_melee.rs2` (~melee_multihit_kind / _first_max / _second) wired into the melee
swing; the first hit rolls against half the max (rounded down), the second against the remainder, through the
same per-target funnel and XP; same tick for the three, a tick later and only after a landed first hit for the
macuahuitl. Not modelled: the Blood moon set's early-attack effect; Torag's hammers (not in this cache's obj
symbols under that name; same rule when added).

### owner_tob_normal: the Void Knight set had no set effect -- FIXED (content bc52eba22f)
combat_stats.rs2 ~player_combat_stat summed the set's item stats and nothing else. wiki_Void_Knight_equipment.wikitext:19-30
(melee +10% accuracy and damage; ranged +10%, elite +12.5% damage; mage +45% accuracy) and wiki_Maximum_ranged_hit.wikitext:19/:23
(the modifier multiplies the effective level after the +8). Now ~void_set_worn reads helm / top / robe / gloves (every (l), (or)
variant; not broken ones) and scales the effective levels. Not done: the elite mage helm's +5% magic damage. Found because every
reference ToB raider bows and pipes in elite ranged void (Blert equipmentDeltas, 24 Maiden rooms).
