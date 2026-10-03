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

- ToB, Maiden and Verzik (and Maiden's crabs and blood spawns): still the Normal npc ids in Entry and Hard; the `_story`/`_hard` records need their `[ai_timer]`/`[ai_queue]` bindings and the `::tobcrab*` debugprocs before they can be spawned. Maiden Entry Defence measured 200 (cache _story stat2 80). Client plugins keyed on id misread these modes. Maiden half FIXED in OSRS-Content d2134f89f5 (seam5): `~tob_maiden_mode_form` retypes her to her mode's record on her first tick, her crabs and blood spawns spawn their `_story`/`_hard` records, and every story/hard body has its triggers (Entry `::tobboss record=entry def=80 att/str/rng/mag 140`, s5m4_after2). Verzik still spawns the Normal ids; her Entry levels are set on them (below). The `::tobcrab*` debugprocs matched only the Normal records: FIXED in OSRS-Content 2cddff56d5 (seam6), they match every mode's body, crab and slug.
- ToB, Maiden: the dying_a retype lands at K+3, not blert's K+1, because `[ai_queue3]` runs at the engine's corpse stage (tob.rs2 / engine); the animation itself plays at K+1.
- ToB, Maiden: spec row `maiden.blood_spawn_step` (337 permille) is wrong as a move rate; restate it as "a free blood spawn steps one tile every tick, never two (1009 of 1021), excluding frozen/dying ticks" (grade B). RESTATED 2026-10-02: 997-1000 permille of pairs outside still runs of 5+ ticks (blert 1043 of 1043, no still run of 1-4). FIXED in OSRS-Content a44e3d97bf (seam4): a slug checks the step the stepper is about to take (`~tob_maiden_slug_can_step`) and redraws on the same tick, else steps to an open neighbour: 1000 permille, 5538 of 5538 free pairs, no still runs of 1-4 (s4m3_after2).
- ToB, Maiden: blert's non-trail splat is drawn 13 (+1 = 14) against our pool loc's 11 (`^tob_maiden_blood_splat_ticks`, also the damage window); unsourced which is wrong (verify_tob_timings named mismatch).
- ToB, Maiden Hard: the per-set scuff roll (3 %) still applies in Hard; 0 of 30 recorded Hard sets were scuffed (not significant at 3 %).
- ToB, Bloat: later walks one tick short (33 vs blert's minimum 34). FIXED in OSRS-Content 93707f5d60 (seam3): the clock and cap are armed at rise + walk_min/cap + ^tob_bloat_walk_first_step (blert walkTime = down - first step - 1): 14 attacked walks min 34, down-to-down min 68 (b3_after_entry).
- ToB, Bloat: one recorded later walk of 61 (raid 8e3ebf74, Bloat 27 %) lies outside 34..46, a speed-flip lockout extension below 40 %; the verifier names it rather than loosening.
- ToB, Bloat Entry: falling-flesh damage 30-50. FIXED in OSRS-Content 93707f5d60 (seam3): 20-25 [video][M62] (grade E, one narrator), ^tob_bloat_entry_hand_min/_max in tob_bloat.constant; 17 Entry hand hits 20-25 (b3_after_entry).
- ToB, Bloat: spec table bloat.tsv rows turn_rate / turn_rate_hard state blert's odds estimator; restate as the per-tick hazard 5.8 % +-0.5 / 13.9 % +-0.9 (RESTATED 2026-10-02: 4.8-6.8 / 12.1-15.7 %, `bloat_stats.txt` lines 38-39); entry_fly_max / entry_stomp_max ranges vs the implemented 4-8 / 20-40; add the first_walk measurement for the refit down chance.
- ToB, Nylocas: a small killed by a player despawned hp0+3 standing. FIXED in OSRS-Content 93707f5d60 (seam3): `death_delay=1` on the 18 small records (blert mechanics page): standing 2 x29. The walking residue (+3 / +4) is the engine row below.
- ToB, Nylocas (ENGINE): an npc that moved on or just before its killing tick gets an arrive delay (src/torirsserver/torirs_server_combat.c:2830-2836, `npc_death_step`'s TORIRSSERVER_DEATH_QUEUED arm: +1 if it moved on D-1, +2 on D), so a walking small despawns hp0+3 / +4 (measured 4 x93 cheat kills, 3 x5 player kills, seam3) where blert's table says +2 ("Walking: stop and turn anim same tick t+1, despawn t+2"). Proposed: an npc record field (e.g. `death_arrivedelay=no`, parsed beside death_delay at torirs_server_content.c:1733) honoured in that arm, set on the 18 small records.
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
- ToB, Verzik: the cache's 8373 transition form is not worn (tob.npc block and `~tob_boss_alive` entry exist since seam2; nothing changes into it).
- ToB, Verzik: P3 auto max 34 at the enrage is code only (33 sampled); yellow pool graphic 1595 is 183 cycles in our cache, so the pool vanishes on the client long before a 14/20-tick blast (blert's 14 is the live graphics object's lifetime; mechanism unsourced).
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
- ToB, Sotetseg: an eat on proc+2 holds the maze teleport (`p_delay`, food.rs2) to proc+5 against blert's +3 (72/72 + 6/6); the maze no longer ends under such a runner, but the late landing remains; how the game exempts the teleport is unsourced.
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

- ToB, Entry Verzik and Entry Maiden fight as their NORMAL records: `::tobboss` reads Verzik's att/str/rng/mag 400 in P1/P2 and 400/400/300/300 in P3 against `verzik_phase1_story` 180/150/180/180 (cache_npc_verzik.txt:427-432), `phase2_story` 200/150/180/180 (:488-493), `phase3_story` 180/200/180/180 (:549-554); `tob_maiden_*_story` 140 throughout (cache_npc_maiden.txt:253-258). Needs a stat set at the spawn or retype (tob_verzik.rs2, tob_maiden.rs2). FIXED in OSRS-Content d2134f89f5 (seam5): Verzik by `~tob_verzik_entry_levels` at the stand-up, take-off, landing and P3 change (P1 180/150/180/180, P2 200/150/180/180, P3 180/200/180/180, s5v_after); Maiden as her `_story` record (def 80, 140s, s5m4_after2). Entry pillar 185 -> 200 (cache :634) in the same commit.
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

- Tree-wide, eating: general/scripts/food.rs2 `eat_food` (:99) and `eat_anglerfish` (:59) call `p_delay(^eat_delay)` (2 ticks), so a delayed player runs no queue and every QUEUED npc hit is held: a Sotetseg melee due +1 lands +3 (eat on the swing tick) or +4 (the tick after), a young dark wizard's spell the same (seam5_sote_eat_before2, seam5_wizard_eat_head2). LostCity's consume.rs2:101-110 never calls `p_delay` (an `%eat_delay` map-clock varp plus `%action_delay`), and our gauntlet_craft.rs2:107-122 already does it that way. The engine and the raid queues match LostCity (torirs_server_scripts.c:938/:1537-1540; npc_combat_melee.rs2:47 is an ordinary queue). Proved in a scratch content copy: +1 x8 with an eat, arena 77/77 (seam5_sote_eat_foodproof2). Needs a new server-only varp and the food.constant comment; the same `p_delay(^eat_delay)` sits in seven player/scripts/consumption files and `p_delay(1)` in about 30 potion files (LostCity's potions set no delay). A whole-suite change for its own seam with an eat-heavy quest set. The seam3 row "an eat on proc+2 holds the maze teleport" is the same cause. Seam6 made and proved the port but did NOT land it: it moved troll and regicide green -> RED (see "From seam6" below).
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

- Tree-wide, eating: the eat-delay port (LostCity consume.rs2:96-110 as three clocks in a new player/scripts/consumption/consume_shared.rs2, varps 7218-7220; no `p_delay` and no `p_stopaction` in food.rs2, td_consumables.rs2, nightmarezone_potion.rs2 and 32 consumption files; an eat adds 3 to a running weapon delay per wiki Food/Fast foods) was proved by its fixer (Sotetseg melee +1 x8 with eats, seam6_sote_eat_fix; wizard +1; goblin gaps 4,4 and eaten 7,7) and passed both of its seam rows in the closer's conformance run. It was NOT landed: the closer's full suite moved `troll` (dies at the Troll generals with all 26 sharks eaten, general at 1/30; its `opts.eat below = 90` eats every 6 ticks, and each eat now costs 3 attack ticks) and `regicide` (leg 4 ends at 0 hp after the Tyras guard and the tripwire snag, against 38 hp committed) from green to RED. Both were green again without it (troll killGeneral 204 ticks = committed). It needs a quest-loop retune of those two tests, landed together with the port. Patch, new files and the two seam rows: build/seam_state/matthew-mbp-m4-raid-b1-seam6/close/ (eat_delay_port.content.patch, consume_shared.rs2, consume_delay.varp, conformance.eat_delay_port.closer.lua). The port also needs the C selftest's full-health bite to wait 3 ticks instead of on `active_script` (eat_delay/selftest_food_delay.patch).
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
