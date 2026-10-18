# Jewelled crabs (crystal crabs): tick spec and content audit

Unit `crabs_thieving`, 2026-10-18. Content: `cox_crabs.rs2` (whole file), the crab
records in `cox.npc`, the `^cox_crab_*` / `^cox_var_crab_crystals` constants in
`cox.constant`. Method: `docs/CONTENT_SOLVER_MANUAL.md` phases 1-3.

## Sources (ranked; the higher one wins a conflict)

| rank | id | source | file |
|---|---|---|---|
| Jagex | J1 | 29 Nov 2023 "Chambers of Xeric Changes": no splash above -64 magic bonus; no attack delay after hitting a crab; no auto re-attack after a colour change | `sources/newsposts/wiki_Update_Chambers_of_Xeric_Changes.wikitext:29-31,181-186` |
| Jagex | J2 | Mod Ash 4 Mar 2020 (quoted by the wiki): stun 50-60 ticks solo, 30-40 for 2-3, 20-30 for 4-5, 10-20 for 6+ | `sources/wiki_Jewelled_Crab.wikitext` |
| Jagex | J3 | Mod Ash 9 Jul 2026 (wiki): crabs regenerate stats 1 per tick | `sources/wiki_Jewelled_Crab.wikitext` |
| Jagex | J4 | 12 Dec 2023: Tumeken's shadow splashes at magic bonus -64 or lower; 16 Sep 2026: thralls recolour crabs (zombie red, skeleton green, ghost blue) | `sources/wiki_Jewelled_Crab.wikitext` (Changes) |
| plugin | P1 | RuneLite Crab Stun Timers: stun graphic **245**; party-size stun bases 50/30/20/10; a re-sent 245 restarts the timer | `sources/crab_stun_timers/CrabStunPlugin.java`, `TeamSize.java` |
| wiki | W1 | Jewelled Crab: colours (melee red, magic blue, ranged green), crystal/orb table, smash weapons, ids 7576-7579 | `sources/wiki_Jewelled_Crab.wikitext` |
| wiki | W2 | Strategies "Crab puzzle": one orb at a time, orb hit damage scales with current HP, aggressive low range ("stay within three tiles"), colour lasts "roughly five seconds", **a coloured orb hitting a crab of a different colour disappears**, three crabs needed | `sources/wiki_Chambers_of_Xeric_Strategies.wikitext:757-812` |
| wiki | W3 | Energy focus ids 7580-7583 | `sources/wiki_Energy_focus.wikitext` |
| wiki | W4 | main page Points: "attacking or being targeted by one of the crabs ... will reward some points upon each successful crystal colour change" | `sources/wiki_Chambers_of_Xeric.wikitext:545-546` |
| wiki | W5 | CA Playing with Lasers (no orb wasted after the first crystal lights) | `sources/wiki_Playing_with_Lasers.wikitext` |
| video | V1 | Synq 0:20:40-0:25:34 (crystal pairing, clockwise bounce, "standing two tiles away ... immediately aggro", de-aggro out of range, destroy a wrong orb by recolouring the next crab or running into it, three layouts good/bad/rare, crabs spawn on the template's west side) | `synq_transcript.md:220-236` |
| video | V2 | The Academy (bounce "to the right of the orb's perspective"; hammer in the inventory smashes), ThePerfectG bad-crabs solve (hammer, re-hammer), Steins;Gate | `sources/transcripts/yt_xnAytiI0Mzo.md`, `yt_Inv2k3a0XdU.md`, `yt_dh3inAB0dDw.md` |
| ref server | N1 | Near-Reality CrabPuzzleRoom / EnergyFocus / JewelledCrab / JewelledCrabPlugin (tiles, focus heading, orb loop, graphics, 1600 points, smash) | `sources/nr/*.java` (NR commit f577de7d) |
| cache | C1 | crab 7576 `horror_crab_ready/walk`, op2 Attack op3 Smash, no combat stats; orb 7580 `stoneglow`; small crystals 29758-29762 **blockwalk=1**; big crystal 29753-29757 2x2 blockwalk; spot 1326 `raids_lasercrabs_shattercrystal`, 245 `stunned_thieving`, seq 1313 `horror_crab_defend`, 401 `human_blunt_pound` | `OSRS-Content/osrs239-content/configs/all.{npc,loc,seq,spotanim}` |

## The tick spec

Solo-scaled. "Tick T" is the server tick; the npc phase runs before the player
phase, so anything an npc decides on T reads raider tiles at the end of T-1.

| # | mechanic | decide tick / cadence | geometry | numbers (solo) | tell the client sees | source |
|---|---|---|---|---|---|---|
| 1 | room load | build | 3 (solo) of 4 crab slots per variant, the sculpture (focus) tile, 4 small crystals, the 2x2 big crystal at the exit | crabs: CCW (22,10)(16,14)(17,21)(23,17); THRU (10,14)(15,9)(17,17)(22,12); CW (9,12)(7,18)(13,22)(16,17) (template tiles, turned with the room) | 3 dim crabs (npc 7576), big crystal loc 29753 | N1, W2 "three crabs" |
| 2 | first orb | load + 5 | at the focus: CCW (13,9), THRU (22,19), CW (18,14) | - | orb npc 7580 appears | N1 `schedule(sendFocus, 5)` |
| 3 | orb spawn lock | spawn tick | - | the orb does not move on its spawn tick | - | N1 `lock(1)` |
| 4 | orb heading | spawn | template heading E (CCW), S (THRU), W (CW), turned clockwise with the room | - | the orb's first step | N1 `(index + rotation) & 3` over {E,S,W,N} |
| 5 | orb step | every tick | one tile in its heading | - | orb npc walk | N1, W2 |
| 6 | bounce | the step's tick | a crab on the next tile turns the orb 90 deg clockwise; up to 10 tries a tick | - | orb turns | W2, V1 "always bounce clockwise", V2 "to the right of the orb's perspective", N1 |
| 7 | orb colour at a bounce | same | white orb + painted crab -> the crab's colour; any orb + dim crab -> unchanged; same colour -> unchanged; **coloured orb + crab of another colour -> orb destroyed** | - | npc changes 7580 -> 7581/7582/7583 | N1 (recolour), W2 + V1 (destroy) -- **ruled: W2 > N1** |
| 8 | double bounce | same | a second crab in the same tick ends the orb | - | bolt impact of the orb's colour on its tile (white/green: 119 windbolt_impact, red: 128, blue: 122), height 124 | N1 |
| 9 | wall | same | the next tile blocked and not a crystal: the orb ends | - | bolt impact | N1 |
| 10 | crystal | step onto it on T, judge on T+1 | the 4 small crystal tiles (template): CCW (19,12)(15,22)(18,18)(23,15); THRU (15,12)(24,16)(10,17)(9,12); CW (13,11)(9,10)(12,19)(12,23); positions 0..3 are black, yellow, magenta, cyan | black <- white orb, yellow <- blue, magenta <- green, cyan <- red | right colour: crystal loc -> 29762 white, bolt impact on it; big crystal steps 29753 -> 29757 | W1, W2, V1, N1 |
| 11 | crystal points | T+1 | - | 1600 per newly lit crystal (6400 a room) to the raider at the crab that bounced the orb | points overlay | N1 (amount), W4 (who) |
| 12 | orb hits a raider | every step (the tile it is on, and the tile it walks to) | the struck raider's tile; splash radius 1 around them | damage = ceil(min(99, struck HP) x 0.25) to everyone within 1 (1 at 1 HP: can kill) | wave impact of its colour on the raider (160 windwave, 157 firewave, 166 earthwave, 163 waterwave), height 124 | N1, W2, V1 |
| 13 | next orb | the tick an orb ends | at the focus | one orb at a time | new 7580 | W2, N1 |
| 14 | paint | the hit (prepare funnel) | any attack that lands | melee red, ranged green, magic blue; reverts to dim ~5 s (8 ticks) later | npc changes 7576 <-> 7577/7578/7579 | W1, W2, N1 (5000 ms) |
| 15 | splash | the hit | - | magic bonus -64 or lower always splashes; above it never | - | J1, J4 |
| 16 | no attack delay | after a crab hit | - | next swing possible the very next tick; a hit that CHANGES the colour stops the auto-attack | - | J1 |
| 17 | smash | op3 | adjacent | stun 50-60 ticks solo; paints red (8 ticks); retains aggression | player anim 401 (hammer) or the weapon's attack anim; crab anim 1313; crab spotanim 245 height 140 | J2, W1, W2, N1, P1 |
| 18 | smash weapons | op3 | - | wielded blunt (warhammers, DWH, elder maul, bludgeon, Torag's, granite maul/hammer...) or a hammer in the pack | "You don't have a suitable weapon equipped for hammering the crab." | W1, N1, V2 |
| 19 | re-smash | op3 on a stunned crab | - | restarts the stun | 245 re-sent | P1, N1 `forceFreezeDelay` |
| 20 | crab aggression | npc hunt | 3 tiles (aggro on approach; Synq says 2) | melee, 4 ticks; max hit unpublished (NR 10) | crab attack anim | W2 "within three tiles", N1 `setAggressionDistance(3)`, V1 |
| 21 | regen | every tick | - | 1 stat point / tick | - | J3 |
| 22 | solved | 4th crystal lit on T | - | crabs removed T+1, big crystal shatters T+2 | crabs vanish; spot 1326 on the big crystal, loc removed (exit open) | N1 transformCrystal |

## Rulings

- **Wrong-coloured bounce (row 7).** NR recolours the orb at every painted crab.
  The wiki Strategies says a coloured orb that meets a crab of another colour
  disappears, and Synq uses that to kill a bad orb on purpose. Wiki and video
  outrank a reference server: the orb is destroyed. A dim crab is "no colour",
  so it never destroys (the bad-crabs solves bounce a white orb off stunned
  crabs that have gone dim again).
- **Stun length (row 17).** NR rolls 46-53; Mod Ash's 50-60 wins.
- **Aggro range (row 20).** Wiki 3 / NR 3 / Synq "two tiles": the 3-tile hunt
  range stands (two tiles is inside it).
- **Who gets the crystal points (row 11).** NR pays the bounced crab's combat
  target. Ours pays a raider within 3 tiles of the last bounced crab, else any
  raider in the room: the same answer solo, close in a team.
- **Hit success (row 15).** The paint needs the hit to succeed (the shared
  combat funnel), so a crab now has defence 1 and every defence bonus -64:
  its defence roll is 0, so any attack roll above 0 lands (magic bonus above
  -64), and an attack roll of 0 (-64) never does -- the Jagex rule, from the
  ordinary accuracy formula. Residual: at a -63 magic bonus the land chance is
  A/(A+1), about 99%, not 100%.

## Audit (mechanic / ours before / OSRS / verdict)

| # | mechanic | ours (before 2026-10-18) | OSRS | verdict / fix |
|---|---|---|---|---|
| A1 | room state | crystal flags + stage in the building player's varps 7044-7048; the orb's timer bound "a raider in the room" first and `error`ed with none (the whole-raid probe's abort when the player teleported out) | the room's state; the orb cycles whoever is in the room | **fixed**: one instance register `^cox_var_crab_crystals` (bitmask), no player needed; the error is gone |
| A2 | points with no raider | n/a (aborted) | - | **fixed**: points go to a raider near the last bounced crab, else in the room, else nobody |
| A3 | crystal unreachable | the wall test came first, and the small crystals are blockwalk=1, so every orb that reached a crystal was ended as "hit a wall": **the room could never be solved** | the orb flies into the crystal | **fixed**: crystal test first, one-tile unclipped step (`npc_tele`) |
| A4 | first heading | aimed at the room's centre (no source) | E/S/W by variant, turned with the room | **fixed** (NR) |
| A5 | wrong-colour bounce | recoloured | destroyed | **fixed** (W2, V1) |
| A6 | lit crystal visual | small crystal never changed | turns white (29762) | **fixed** |
| A7 | bursts | no graphics at all | bolt impact on an ended orb, wave impact on a struck raider | **fixed** (NR ids); sounds (NR synth 219) not sent -- open |
| A8 | splash / miss | crab defence 100: melee/ranged/magic missed often, and a miss does not paint | no splash above -64 | **fixed** via crab defence stats (residual ~1% at -63) |
| A9 | re-smash | `npc_freeze` refuses a frozen crab and one inside its 5-tick immunity | re-smash restarts the stun | **engine work** (a forced, renewing freeze) |
| A10 | smash weapons | wielded hammer/DWH/elder maul only | any wielded blunt, or a hammer in the pack | **fixed** (category `weapon_blunt` / `weapon_blunt_heavy`, or `hammer` in inv) |
| A11 | smash tells | none | crab anim 1313, spotanim 245 (the plugin's key), player pound | **fixed**; the wielded weapon's own attack anim is not used (one pound anim) |
| A12 | solved sequence | crabs +1, big crystal changed +2 but **never removed** (exit stayed blocked); queued as player queues from an npc timer | crabs +1, shatter + removal +2 | **fixed** (the orb that lit the 4th crystal carries the two ticks) |
| A13 | big crystal stage | `loc_change(..., 6000)` (reverts after an hour) | permanent | **fixed** (`^max_32bit_int`) |
| A14 | hammer spawn | not placed | "there's a hammer spawn in this room" (V1) | **open**: no source holds its tile |
| A15 | no attack delay / stop on colour change | not implemented | J1 | **combat-funnel work** (`player_hit_npc_prepare.rs2` / `combat.rs2`, not this unit's files): reset `%varp5732_action_delay` after a crab hit; stop the auto-attack when the paint changed the colour |
| A16 | thralls recolour | no | J4 (16 Sep 2026) | open (thrall content) |
| A17 | crab max hit | engine from strength 140 | unpublished (NR max 10) | open |
| A18 | sounds | none | NR: orb 219, smash 556/2727, crab attack 718 | open (npc-context sound needs `~cox_npc_sound` synth names) |
| A19 | stun timing (orb not moving while a crab is stunned) | - | - | n/a |
| A20 | crab count by party | 3 solo, 4 from 2 players | "the more players ... the more crabs" (only 4 slots exist in NR) | kept |

| A21 | crabs take damage | the player's hits land real damage (ticklog `hit_npc` 4-11 on a crab; 5000 HP backstop + 1/tick regen) | 0 (NR `hit.setDamage(0)`; "cannot be killed by conventional combat") | **combat-funnel work**: `$prepared = 0` in the crab branch of `player_hit_npc_prepare.rs2` |
| A22 | paint revert while stunned | the revert was polled from the crab's `[ai_timer]` | 8 ticks after the last paint | **fixed**: `npc_changetype(colour, ^cox_crab_paint_ticks)` reverts to the spawn type in the npc phase; probe: a stunned crab repainted every 4 ticks by the player's melee went dim exactly 8 ticks after the last paint (ticklog `npc_retype` t57 -> t65) |

## Probe 1 results (2026-10-18, scriptrun, private script pack, `::coxtest 5 0`)

`crabs_probe.lua` (scratchpad): the room spawned 3 dim crabs, the four small
crystals at their tiles (black / yellow / cyan / magenta all found), the big
crystal; an orb at the focus moving one tile a tick on its heading and bursting
on the wall (spot 119 at height 124), the next orb sent; Smash put spotanim 245
on the crab, anim 1312/1313, crab held its tile 30+ ticks; the crab reverted 8
ticks after its last paint; crab melee on the player 0-15 (A17). The raider
teleported out with an orb live: 40 more ticks, **no `script error`, no abort**
(the 2026-10-18 bug). Not yet seen live: a crystal lighting and the solve
sequence (needs a lured bounce; the selftest
`cox_selftest_crab_crystal_idempotent_and_points` covers the state, now in a
scratch instance).

## Probes (facts the first run prints)

- the small-crystal locs exist on the four crystal tiles in every variant and
  turn (`t.world.loc_near("raids_lasercrabs_smallcrystal_<c>")`);
- the orb leaves the focus on the predicted heading and bounces as rows 6-9 say;
- a crystal lights on the tick after the orb steps on it; the big crystal is gone
  2 ticks after the fourth;
- no `script error` / `abort` with the raider out of the room.

## Round 2 (2026-10-18): content found by the solver

| # | mechanic | was | now | source |
|---|---|---|---|---|
| A23 | crabs follow a raider | `givechase=no`: a crab dropped its target after one step, so no crab could be led anywhere | `givechase=yes`, `maxrange=32` (cox.npc, all four forms) | Synq 0:22:56, ThePerfectG ("drag a third crab"), wiki ("luring these away is required"); NR RaidNPC maxDistance 32 |
| A14 | the hammer spawn | "not placed, no tile in any source" | it IS in the template: `raids_lasercrabs_hammer` (op Take) at template (15,1) CCW, (15,3) CW, by the entrance; `[oploc1,raids_lasercrabs_hammer]` gives a hammer to a raider with none | client loc probe; Synq, wiki |

Measured (scriptrun, `test/raids/cox_crabs.lua`, the whole raid's kit): a led crab
stops one tile behind me on a straight walk (the lure the solver uses); a
smashed crab holds 50-60 ticks and goes dim 8 ticks after its last paint even
while stunned; a free crab beside the orb's line steps into it and turns it.
Still open: A9 (a re-smash does not renew the stun -- the solver plans around
it), A15 (no attack delay / stop on colour change), A17, A18.
