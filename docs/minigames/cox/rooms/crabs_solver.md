# Jewelled crabs: solver plan (phase 4)

Unit `crabs_thieving`, 2026-10-18. The tick spec and audit are
[`crabs_spec.md`](crabs_spec.md); this file is the plan for
`script/plugins/quest_driver/cox2_crabs.lua` (a `QD.T2` room, shaped like
`cox2_olm.lua`) and `test/raids/cox_crabs.lua`. No code until F3 (`::coxtest 5 <n>`)
is in and the content of the spec's audit is green on a probe.

## The strategy a strong solo plays

Sources: Synq 0:20:40-0:25:34, ThePerfectG's bad-crabs solve, the wiki Strategies,
The Academy. A solo has three crabs and one body, so the method is **park and
paint**:

1. **Read the layout.** Variant (CCW "good", THRU/CW "bad"/"rare" in the players'
   names -- which name is which is a probe, it depends on the turn) and turn give,
   in template terms, the focus tile, the orb's first heading and the four crystal
   tiles. Everything after is a route plan in that frame.
2. **Plan the bounce chain per crystal.** For each crystal, the crab tiles that turn
   the orb's path onto it: a crab on the orb's line, one tile before the turn point,
   turns it clockwise. A crystal reached with no bounce needs a white orb (the black
   crystal); one bounce with a painted crab sets the colour (yellow <- blue/magic,
   magenta <- green/ranged, cyan <- red/melee or smash). Order the crystals so each
   parked crab is reused (ThePerfectG: hammer two crabs in place, drag the third,
   re-hammer before the stun lapses).
3. **Lure.** A crab aggros at 3 tiles and follows (it chases, `givechase=no` means it
   does not path round walls far). Pull it so its tile lands on the planned bounce
   tile: the crab steps onto the tile beside me, so I stand one tile past the target
   on the crab's line. Stack two crabs by running back and forth through them
   (Synq 0:22:56).
4. **Park.** Smash (op3) with the pack hammer / DWH: 50-60 ticks of stun, red for 8.
5. **Paint at the right moment.** The colour lasts 8 ticks, so paint the bounce
   crab when the orb is <= 8 ticks from it (one style attack: magic for blue,
   ranged for green, melee for red; no attack delay on crabs, so switches are
   tick-to-tick). A white bounce needs the crab DIM: let the smash's red lapse (8
   ticks) before the orb arrives.
6. **Kill a bad orb.** If the orb took the wrong colour, recolour the next crab it
   will hit (a different colour destroys it) or step into it (25% of current HP --
   only at high HP, never below 8).
7. **Keep out of the line.** Never stand on the orb's next tile unless killing it;
   move diagonally round a crab to leave it in place (Synq).

Prayer: Protect from Melee whenever a crab is within 3 tiles and not stunned
(fairly accurate melee, 4 ticks).

## Perception (api_drive, client-side only)

| need | verb | status |
|---|---|---|
| crabs: tile, colour, stunned | `api_drive.npcs` rows: `server_x/z`, `npc_id` (7576 dim / 7577 red / 7578 green / 7579 blue), `spotanim_sent_id` = 245 and `spotanim_tick` (stun start; the stun timer is the plugin's: 50 ticks certain, then 0-10 unknown) | exists |
| orb: tile, colour, heading | `api_drive.npcs` rows `npc_id` 7580-7583; heading = the last tile step (first heading from the spec) | exists |
| crystals lit | `api_drive.locs` / `t.world.loc_near("raids_lasercrabs_smallcrystal_white")` | exists |
| big crystal gone | `t.world.loc_near("raids_lasercrabs_bigcrystal_5")` absent | exists |
| orb bursts | `api_drive.spotanims` (119/122/128 bolt, 157/160/163/166 wave) | exists |
| crab target (who it attacks) | `facing_entity` on the crab row | exists |
| the room's variant and turn | no packet says it; derive from the crystal and focus tiles seen (four crystal tiles fix variant + turn uniquely) | derived (no verb needed) |
| **missing** | none known. Engine work instead: a renewing freeze (A9) and the combat funnel's crab no-delay/stop rule (A15) -- without them the bot's re-smash and paint-then-switch timings differ from OSRS | engine |

## Mode machine

`ENTER -> SURVEY (variant, turn, crab tiles) -> for each crystal in plan order:
LURE(crab_i -> tile) -> PARK (smash) -> WAIT_ORB -> PAINT (at orb distance <= 7 ticks
to the bounce) -> CONFIRM (crystal white) | RECOVER (bad orb: kill it) -> DONE (big
crystal gone, walk to the exit Passage)`.

Re-smash guard: a parked crab whose 245 tick is >= 48 ticks old is re-smashed before
the next orb uses it.

## Kit

The CoX solo kit plus: a hammer in the pack (or DWH wielded), one ranged weapon and
a magic weapon (staff/trident/Tumeken's shadow -- with a magic bonus above -64),
melee main hand. Never drop equipment.

## Threat field

| hazard | window | damage | term |
|---|---|---|---|
| orb on my tile | each tick the orb's next tile is mine | ceil(0.25 x min(99, HP)) to me and anyone within 1 | forbid unless RECOVER chooses it |
| crab melee | crab within 1 tile and not stunned, every 4 ticks | unpublished max (~10 NR); 0 under Protect from Melee | expected HP |

## Probes (first run prints)

1. Each variant x turn: the four crystal tiles seen vs the spec's table; the orb's
   first heading.
2. Smash: 245 on the crab, stun length (ticks until it moves), red for 8.
3. A painted bounce: the orb's colour after it; a wrong-colour bounce destroys it.
4. Solve sequence ticks: crystal lit -> next orb; crabs gone +1; crystal gone +2.

## PASS

Room solved (all four small crystals white, the big crystal gone) and the player
through the exit Passage, alive, with **no orb hit taken** and fewer than 5 wasted
orbs after the first crystal (Playing with Lasers is "none": tracked, not required),
within 150 s of entering; 0 `abort` / `script error` lines; on 48 `::coxtest 5 n` seeds
(every variant and turn).

## Round 2 result (2026-10-18)

`script/plugins/quest_driver/cox2_crabs.lua` + `test/raids/cox_crabs.lua`, the
whole raid's kit (no hammer: the solver takes the room's spawn; Auto Retaliate
turned off through the combat tab). The plan above, as built:
- survey: the client's route to every template tile (96 a tick);
- the chain search: breadth-first, fewest NEW crabs, crabs already held are
  fixed bouncers (free), resumable (1200 nodes a tick), re-planned only when
  the held crabs no longer bring the orb home;
- park LAST chain crab first (the orb keeps its old line until the first one
  stands); lure = walk S -> Q through P only in a clear window, smash a crab
  SETTLED on P (there two ticks, me still and cardinal);
- fire: stand off the planned path (and its neighbours), paint the coloured
  chain crab 2-6 ticks before the orb's bounce at it, never with an op that
  walks me;
- hazards: the orb's path with all crabs, held crabs only, each free crab
  removed, a free crab stepped onto each of its next tiles; the same for the
  next orb from the sculpture at its earliest and latest birth.

48 seeds (`cr0..cr47`): 45 PASS, 3 FAIL, every one "1 orb hit" (all rooms
solved, exits open): a free crab stepping onto a newborn orb's first tile
beside me. Solve 230-880 ticks in the room, median ~310 (slower than a human:
re-smash does not renew a stun here).
