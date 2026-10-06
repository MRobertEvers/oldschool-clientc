#!/bin/sh
# regenerate ours_measurements.txt from build/quest_gate/spec_sotetseg_*/ticklog.tsv + ledger.tsv (needs the runs to exist)
W=${W:?}; E=$W/docs/minigames/theater_of_blood/sources/blert_api/spec_pass_sotetseg; D=$W/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob; Q=$W/build/quest_gate
echo "# Sotetseg spec pass: OUR server measured, 2026-10-02. Driver: python3 tools/quest_gate/run.py --script <lua> --name <name> --no-build --no-publish (binary src/torirs_questtest, content pack current). Scripts in scratch/."
echo "# God mode was on in every run except spec_sotetseg_10, so hit_player damage reads 0 where god mode absorbed it; timing rows are unaffected. Ticklog columns: serial tick kind a b c d e f label."
echo
echo "## SOLO_NORMAL_CADENCE (spec_sotetseg_1: normal, solo, player at the fight tile 22 tiles from him, ::tobgo at tick 8)"
python3 $D/scratch/ours_an.py $Q/spec_sotetseg_1/ticklog.tsv
python3 $D/scratch/an.py $Q/spec_sotetseg_1/ticklog.tsv | awk '$1>=53 && $1<=70'
echo "(tick 58: npc_anim 8139 twice, projectile 1606 (start 20, end 232) and projectile 1604 (start 30, end 242): the death ball goes out WITH the tenth ordinary ball; the next attack is tick 63 (+5); impact of the 1604 is tick 67 = +9 at 22 tiles, the ordinary ball +8)"
echo
echo "## ADJACENT_ROLL (spec_sotetseg_7: ::tobwarp 15 39, the tile south of him, 420 ticks)"
python3 $D/scratch/ours_an.py $Q/spec_sotetseg_7/ticklog.tsv
echo
echo "## MAZE_PROC (spec_sotetseg_4: ::tobgo then ::tobmazearm in the same tick, so boss_seen is still 0)"
python3 $D/scratch/an.py $Q/spec_sotetseg_4/ticklog.tsv | sed -n 1,8p
awk -F'\t' '$3=="player_tile" && $2>=8 && $2<=20 {print "player_tile tick "$2": "$5","$6",plane "$7}' $Q/spec_sotetseg_4/ticklog.tsv | awk '{k=$4" "$5; if(k!=p){print}; p=k}'
echo "(proc = tick 9: npc_retype 8388->8387 and npc_anim 8142 on the same tick the runner is at 6427,151 plane 3 (arrival +0); maze left at 17; his attack attempt is at 18 (+1) with no target in the arena (solo) and it consumes the clock, so the next visible attack is 23 = 18+5)"
echo
echo "## MAZE_CYCLE (spec_sotetseg_c3, c4, c7, c8: ::tobmazeout issued after k ticks; proc 9; despawn checks 13, 17, 21)"
for k in 3 4 7 8; do echo "k=$k: mark tick $(awk -F'\t' '$3=="mark"{print $2}' $Q/spec_sotetseg_c$k/ticklog.tsv); re-activation tick $(awk -F'\t' '$3=="npc_retype" && $5=="8387" && $6=="8388"{print $2}' $Q/spec_sotetseg_c$k/ticklog.tsv)"; done
echo
echo "## RUNNER_TICK_PERIOD (spec_sotetseg_8: runner steps north off the path; spec_sotetseg_9: proc on an even tick, standing on the path; spec_sotetseg_10: no god mode, 150 ticks)"
echo "8: hit_player ticks: $(awk -F'\t' '$3=="hit_player"{printf $2" "}' $Q/spec_sotetseg_8/ticklog.tsv)"
echo "8: tornado npc_spawn tick: $(awk -F'\t' '$3=="npc_spawn" && $5=="8389"{print $2}' $Q/spec_sotetseg_8/ticklog.tsv) (step 4, onto row 3 = the fourth row, resolved at tick 20); npc_free tick $(awk -F'\t' '$3=="npc_free" && $5=="8389"{print $2}' $Q/spec_sotetseg_8/ticklog.tsv)"
echo "8: tornado npc_tile rows (tick:x,z): $(awk -F'\t' '$3=="npc_tile" && $8=="8389"{printf $2":"$5","$6" "}' $Q/spec_sotetseg_8/ticklog.tsv | cut -c1-900)"
echo "9: hit_player ticks: $(awk -F'\t' '$3=="hit_player"{printf $2" "}' $Q/spec_sotetseg_9/ticklog.tsv)  (proc tick 10; chip gaps 8)"
echo "10: chip hits tick:damage $(awk -F'\t' '$3=="hit_player"{printf $2":"$6" "}' $Q/spec_sotetseg_10/ticklog.tsv)"
echo
echo "## SOLO_RUNNER_FALSE_CLEAR (spec_sotetseg_3: ::tobgo, 12 ticks, ::tobmazearm, ::tobmazestate each tick; 'The way onward is open' at tick 24 = the room declared cleared)"
cut -f2,6 $Q/spec_sotetseg_3/ledger.tsv | cut -c1-300 | sed -n 5,14p
echo
echo "## PRAYER_BLOCK (spec_sotetseg_5: a ball hit at tick 21 re-blocks every 5 ticks, never lapses; spec_sotetseg_6: ::tobmazearm right after the hit at 21 stops the next hit; drive tick of each attempt, refused until tick 25 processed at 26)"
cut -f2,6 $Q/spec_sotetseg_6/ledger.tsv | cut -c1-140 | sed -n 3,9p
echo
echo "## HITPOINTS (spec_sotetseg_hp_entry, spec_sotetseg_hp_hard, spec_sotetseg_2 for normal; ::tobwhy; solo = scale 1)"
cut -f6 $Q/spec_sotetseg_hp_entry/ledger.tsv | sed -n 4,6p | cut -c1-200
cut -f6 $Q/spec_sotetseg_hp_hard/ledger.tsv | sed -n 4,6p | cut -c1-200
cut -f6 $Q/spec_sotetseg_2/ledger.tsv | sed -n 4,5p | cut -c1-200
echo
echo "## MAZE_GENERATOR (spec_sotetseg_11: ::tobmaze, ::tobmazerate)"
cut -f6 $Q/spec_sotetseg_11/ledger.tsv | sed -n 4,5p | cut -c1-400
echo
echo "## DAMAGE_ROLLS (spec_sotetseg_dmg_off: adjacent, no god mode, ::setlevel hitpoints 99 every tick, 105 ticks; the prayer run is identical because the first ball hit blocks the melee prayer, so Protect from Melee halving was NOT measured)"
python3 $E/scratch/dmg_an.py $W
echo
echo "## TOBRUN (spec_sotetseg_run: ::tobrun in a Sotetseg room)"
cut -f6 $Q/spec_sotetseg_run/ledger.tsv | sed -n 4p | cut -c1-60
echo "(its Sotetseg checks are ~tob_st_sote_clock, _seed_pack, _tornado_damage, _ball_ring, _research; they pin the 5-tick period and the TENTH magic attack as the ball tick, which the recorder contradicts, see sotetseg.magic_per_ball)"
