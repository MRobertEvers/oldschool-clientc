# deserttreasure -- relay notes (wiki source; LostCity has none)

Setup: `::setvar deserttreasure 10` skips the intro (stage 10 = mirrors ready). Prereqs by ::complete
(digsite, priestinperil, waterfall, plaguecity) + setvar ikov/desertrescue/troll_quest (^dt_*_complete).
Single-way combat: `::passive <npc>` on aggressive npcs nearby, or "I'm already under attack".

FIGHTS (stat blocks: configs/deserttreasure.npc, wiki + cache; they were 10 hp before). Real bosses now:
- Fareed 130 hp, hits to 37: needs 99 combat, rune armour, ~20 sharks, ice gloves (else disarmed).
  Cast water_blast (weak to water); press Attack once first, a bare cast says "already under attack".
  ~220 ticks. His diamond drops AT THE KILL: click_obj fd_diamond_fire op 3 right there.
- Damis form 1 90 hp, form 2 200 hp (3-tick, drains prayer): ~500 ticks with an abyssal whip; bring 30+
  sharks (worn gear frees slots, ::give more mid-fight). Ring of visibility worn at 2739,5089.
  Dark diamond drops at the second kill, pickup op 3.
- Dessous 200 hp (silver weapon +10%): pour the blessed pot from 3567,3402 (tile WEST of the tomb, he
  spawns on it, step to 3567,3403), ~620 ticks. Stage 3 is written only if he is still alive at death.
- Kamil 130 hp at 2863,3757: fire_blast, ~170 ticks; dt_ice_stage 2 + fd_icewarrior_subquest 2 first.
- Bosses live ^dt_boss_lifespan (3000) ticks; at 500 they used to vanish mid-fight uncredited.
- Rune platebody needs ::complete quest_dragonslayer1.

Smoke: Draynor trapdoor 3118,3244; well 3310,2964 (rope maplink back up). Light 4 torches (Firemaking 50,
each burns 250 ticks), chest 3248,9362 needs Thieving 53 and a FREE slot (retry; failed picks are rolls).
Fareed's gate fd_fw_metalgateclosed_r 3303,9376 eats the warm key.
Shadow: Rasolo 2535,3431; bandit chest 3169,2970 (retry lockpick).
Blood: Malak 3496,3479; vampire trapdoors in the quest file.
Ice: child 2836,3740 (any cake/pizza/chocolate, one consumed); gate 2838,3739; east side is cold
(softtimer dt_ice_cold drains stats); cave 2868,3718 needs 5 troll kills; ledge 2837,3804 needs spiked
boots WORN; blocks 2825,3807 / 2825,3811 lvl2 need fire_blast (melee op is Smash-ice).
Pillars: use each diamond, Magic 50; all four -> stage 13. Ladder desert_laddertop 3233,2897.
Pyramid: ladders are maplinks; floor 2 down at 2846,4973 only from 2845,4973. Hazards softtimer
dt_pyramid_hazard (odds are the pack's). Temple door 3233/3234,9324. Azzanadra 3232,9317.

Differs from the guide: no ice-path slipping; hazards/cold do not survive logout; diamonds on pillars
write 13 directly; ::deserttreasure debugproc jumps to Azzanadra. Item sources: see QUEST_SUITE_KIT.
