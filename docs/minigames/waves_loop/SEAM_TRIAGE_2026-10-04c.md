# Waves seam pass 6 triage (2026-10-04): Jad, the triple Jad, Zuk, the glyph and the final-wave adds

Pass: `matthew-mbp-m4-waves-b1-seam6`. Written by the waves orchestrator from the spec
pass's batch C (tables `mixed_late_waves`, `single_jad`, `triple_jad`, `zuk_glyph_shield`,
`zuk_fight`, `zuk_sets_and_healers`, `presentation_av` under
`docs/minigames/inferno/encounters/`; rows JAD-*, WZ-*, ZUK-*, GLYPH-*, INF-AV-004..008 in
`docs/minigames/waves_loop/CONTENT_BUGS.md`). Four seams, one fixer at a time, in this
order. It runs after seam pass 5 has landed, so the pillars are locs, the wave file spawns
from Blert's tiles, and the reward proc exists in `inferno.rs2`.

The method is seam pass 5's (`SEAM_TRIAGE_2026-10-04b.md`, "How a content seam works
here"): the table is the target; re-read the source line before touching the script; fix
grade A to C rows and D rows whose source is the wiki; never change a grade E row; prove by
PLAYING with real attacks, prayer and food, quoting tick-log distributions before and
after; presentation only from a source; write the fixing commit into each row's `fixed in`.
Several batch C rows were measured under `::god` or `::zukhp`: re-measure each by a fight
where a fight can reach it before you call it fixed. `MI` is
`OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno`.

## content: inferno_jad_file

Units: single_jad, triple_jad, mixed_late_waves (the Jad rows); zuk_sets_and_healers
(the Zuk-wave Jad shares the code).

Files: `MI/scripts/inferno_jad.rs2`, `MI/scripts/inferno_ai.rs2 (the Jad's attack proc only; the other monsters' procs landed in seam pass 5 and are not yours)`, `MI/configs/inferno.constant (the Jad and healer blocks only)`

Summary. Tables: `single_jad.tsv`, `triple_jad.tsv`. Rows: JAD-PRAYER-READ is NOT a defect (owner, 2026-10-04: "Jad prayer protection is
checked on the animation. In fact MOST things in OSRS ARE. It is the EXCEPTION that damage
is calculated on [landing]."): our server reading the protection prayer on the tick the
Jad swings is the OSRS rule, and the batch C scratch that died with a reactive prayer was
playing the wrong technique, not finding a bug. Leave the read where it is. Pin the wiki's
Jad and TzTok-Jad lines on when the prayer is checked and quote them in the table's row; if
a wiki line reads differently from the owner's rule, do not change the server: write the
quote in your report for the orchestrator to bring to the owner. JAD-OPEN (first attack 1 tick after spawn against Blert's observed offset: the row gives
the distribution), JAD-TRIPLE-STAGGER (the three Jads' first swings at 1, 3 and 6 ticks:
the table says what the stagger should be), JAD-HEALER-STRAND (killing the Jad with a
healer alive leaves the wave stuck: the healers must die or despawn with the Jad as the
wiki states), JAD-HEALER-PLACE (the healers' spawn block against the 70 recorded healer
tiles), JAD-DEATH-CLIP (the Jad is freed 3 ticks after the killing blow, cutting its death
clip: free it when the clip ends), JAD-SOUND-TELL and the table's presentation rows
(sequences and sounds per attack from `RIG_ANIMATIONS.md` and Blert's ids; `docs/
INFERNO_SOUNDS.md` for which sound rows a source states), INF-AV-007 (Jad's attacks on
the glyph play no impact sound or graphic: the document gives 163 and the impact
spotanim at layer k). Prove on wave 67 (one Jad: the prayer up on the swing tick as the technique row,
kill it with its healers) and wave 68 (three Jads: the stagger and the prayer order from the tick log).

Evidence. `CONTENT_BUGS.md` JAD-*, INF-AV-007; `single_jad.tsv`, `triple_jad.tsv`.

## content: inferno_glyph_file

Units: zuk_glyph_shield.

Files: `MI/scripts/inferno_glyph.rs2`, `MI/configs/inferno.constant (the glyph block only)`

Summary. Table: `zuk_glyph_shield.tsv`. Rows: WZ-ROW (the glyph's row), WZ-START (the
player's wave 69 start tile), WZ-DWELL (4 ticks on an end tile; the table gives the
source's dwell and the full period), WZ-DIR (always starts west: the table says whether
the direction is fixed or rolled), WZ-ALIGN (the glyph's x at each Zuk shot and the first
shot one tick after the glyph's first arrival), WZ-WINDOW (the absorbed window is 8 tiles
and asymmetric by direction: the wiki states the safe tiles relative to the glyph; fix to
the stated window), GLYPH-DEATH-CLIP (freed 2 ticks into a 4-tick death clip). The shield
walk is the technique: prove by following the glyph for three full periods with
`t.player.step_tick`, no Zuk shot landing, from the tick log.

Evidence. `CONTENT_BUGS.md` WZ-*, GLYPH-DEATH-CLIP; `zuk_glyph_shield.tsv`.

## content: inferno_zuk_file

Units: zuk_fight, zuk_sets_and_healers, completion_reward_and_pet (the kill calls the
reward proc seam pass 5 wrote in `inferno.rs2`).

Files: `MI/scripts/inferno_zuk.rs2`, `MI/configs/inferno.constant (the Zuk block only)`

Summary. Tables: `zuk_fight.tsv`, `zuk_sets_and_healers.tsv`. Rows: Zuk's cadence and the
enrage (ZUK-ENRAGE-GAP: the shot straddling the crossing to 240 waits the slow gap, so the
enrage starts one shot late: the wiki states when the faster cadence begins), ZUK-MIN-HIT
(an unblocked shot rolls 1..148 and never 0: the table's max hit and whether 0 is
possible), the shield's hitpoints and what damages it, the timed sets (ZUK-SET-TILE,
ZUK-SET-OPEN: the set's Jal-Xil and Jal-Zek attack 1 tick after spawning; the table gives
the source's offset and the set timer), the Jad at its threshold (ZUK-JAD-OPEN), the
healers at theirs (`inferno_adds.rs2` is the next seam's: call its procs, do not edit
them), ZUK-DEATH-CLIP (Zuk freed 3 ticks into a 5-tick death clip), the kill: write the
Zuk kill count, the cape, the pet roll and the broadcast through seam pass 5's reward proc
(INF-AV-008, REWARD-*). Blert has no event for the shield, so shield rows stay at their
grade; do not promote them. Zuk's hit chance and flight are open rows M60 and M61: measure
them by fighting (a long unprayed stand behind no glyph is a death; measure from behind the
glyph's edges and from the recorded hits) and report the distributions, and fix only what a
graded row says. Prove: wave 69 entered at `::inferno 69`, the glyph followed, Zuk's
cadence and the first set measured by playing; the kill itself with the content's own path
if a fought kill is out of reach, saying so plainly.

Evidence. `CONTENT_BUGS.md` ZUK-*, INF-AV-008, REWARD-*; `zuk_fight.tsv`,
`zuk_sets_and_healers.tsv`.

## content: inferno_adds_file

Units: zuk_sets_and_healers, mixed_late_waves (the final-wave ranger and mager), presentation_av.

Files: `MI/scripts/inferno_adds.rs2`, `MI/configs/inferno.npc (the Jal-MejJak and final-wave add blocks only)`

Summary. Tables: `zuk_sets_and_healers.tsv`, `presentation_av.tsv`. Rows: ZUK-HEAL-GAP and
ZUK-HEAL-FIRST (Jal-MejJak heals every 4 ticks starting 1 tick after spawn; measured under
`::god`: re-measure by fighting at the healer threshold), ZUK-MEJJAK-GAP (a provoked
Jal-MejJak volleys at 4 then 5), INF-AV-004 (its flinch is its walk animation 2863; the
rig's defend is 2869), INF-AV-005 (its heal beam and lava ball use the Fire Blast
projectile 130 where `docs/BOSS_ASSETS.md` and the rig name 660), INF-AV-006 (no spawn
animation; 2864 is on its rig), RIG-9 (its death is a "go down" transition; the rig's death
is 2866), the final-wave ranger's and mager's attack rows the tables grade, and every
presentation row of `presentation_av.tsv` whose event is in this file (sequence, graphic,
projectile and sound per event; `docs/INFERNO_SOUNDS.md`: a row the document DERIVES stays
as it is, a row a source STATES is placed). Prove by fighting through the healer phase:
the heals, the volley and every animation in the tick log, and a picture of each.

Evidence. `CONTENT_BUGS.md` ZUK-HEAL-*, ZUK-MEJJAK-GAP, INF-AV-004..006, RIG-9;
`zuk_sets_and_healers.tsv`, `presentation_av.tsv`.
