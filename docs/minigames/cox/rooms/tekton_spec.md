# Tekton: the tick-level spec and the content audit (2026-10-18)

Content: `OSRS-Content/osrs-content/server/scripts/minigames/minigame_cox/scripts/cox_tekton.rs2`,
Tekton blocks in `configs/cox.constant` (`^cox_tekton_*`, `^cox_npcvar_tekton_*`,
`^cox_var_tekton_down`) and `configs/cox.npc` (the six `raids_tekton_*` records).
Solver plan: [`tekton_solver.md`](tekton_solver.md).

## Sources, in the order they outrank each other

| key | source | file |
|---|---|---|
| **J** | Jagex: update log entries quoted on the wiki (31 Jan 2019 defence -25% and "less sensitive to return to the anvil"; 29 Nov 2023 first DWH spec guaranteed, "no longer resets his Defence when healing at the anvil"; 25 Jun 2025 20% water weakness); Mod Ash (7 Sep 2023 ranged immune / magic ÷5; 9 Jul 2026 regen 1 per 15 ticks) | `sources/wiki_Tekton_2026-10-18.wikitext` |
| **W** | OSRS Wiki: Tekton (infobox, Mechanics), CoX/Strategies (Tekton), CoX/Challenge Mode/Strategies (Tekton), CA pages Anvil No More, Blind Spot | `sources/wiki_Tekton_2026-10-18.wikitext`, `wiki_Chambers_of_Xeric_Strategies.wikitext`, `wiki_Chambers_of_Xeric_Challenge_Mode_Strategies.wikitext`, `wiki_Anvil_No_More.wikitext`, `wiki_Blind_Spot.wikitext`, `wiki_Talk_Tekton_2026-10-18.wikitext` |
| **G** | guides filmed with tile markers / metronomes: **SQ** Synq's 2026 solo guide 1:09:31-1:25:34; **TH** Theoatrix 2019; **AC** The Academy 2023; **RS** RuneScythed 2023 flinch guide | `synq_transcript.md`, `sources/transcripts/yt_m49FdYfuMeY.md`, `yt_WOuFM3txi0w.md`, `yt_Pki7GmiUE9s.md` |
| **P** | plugin constants: OpenOSRS coxhelper (`tektonAttackTicks` 27 / 47, auto anim ids), RuneLite gameval ids | `sources/openosrs_coxhelper/CoxPlugin.java`, `sources/runelite_AnimationID_tekton.txt` |
| **NR** | Near-Reality (Zenyte, Kris 2017-2018): `npc/Tekton.java`, `combat/tekton/Tekton{Engage,Combat,Retreat,Smith}Script.java`, `room/TektonRoom.java`, `plugins/object/TektonFire.java` | decoded tiles via `tools/cox_nr_locs.py` |
| **C** | this cache (osrs239): records, seqs, spotanims, the template map | `configs/all.npc/.seq/.spotanim/.loc`, `maps/m51_82.jl2`, `m52_82.jl2` |

**RECORDED DATA NOW OUTRANKS ALL OF THE ABOVE** (2026-10-18, §8): RSProx
sessions 820, 3285, 843 and 3389 (`docs/rsprox-analysis/cox/tekton.md`). Where
§3 and §8 disagree, §8 is what the content does.

## 1. Ids (all resolved in this cache)

| what | name (id) |
|---|---|
| npcs | `raids_tekton_waiting` 7540, `_walking_standard` 7541, `_fighting_standard` 7542, `_walking_enraged` 7543, `_fighting_enraged` 7544, `_hammering` 7545 (P, C). The client sees the id change on every transform. |
| seqs (orange / red) | hammer loop `tekton_spawn` 7473 (the hammering/waiting ready anim); rise off the anvil `tekton_avil_transition_to_idle` 7474 / `_enraged` 7487; lean to the anvil `tekton_idle_transition_to_anvil` 7475; enter stance `tekton_idle_transition_to_ready` 7478 / 7488; leave stance `tekton_ready_transition_to_idle` 7479 / 7489; **swings: stab 7482 / 7493, slash 7483 / 7494, crush 7484 / 7492**; ready 7480 / 7490; death 7495 (P: RuneLite gameval; NR uses the same numbers) |
| spotanims | spark projectile `wild_falloff_meteor_flying` 660; impact `wild_falloff_meteor_blast` 659 (NR `Projectile(660,100,5,0,25,60,64,5)`, `Graphics(659)`; names C) |
| locs | `raids_tekton_anvil` 6x4 blockwalk (C); `raids_tekton_walkblocker` 4x4 (NR 30023: under him in his stance); `raids_tekton_fire` 2x2 op1 Cross (NR 30021: the entrance fire); `raids_tekton_book` (NR 30022: dropped where he dies, his journal); `raids_lava_smoke3` 2x2 non-blocking (the smoke vents, C); `raids_blockage_orange` (NR 30017: the exit crystal; placed and shattered by `cox_tekton.rs2`) |

## 2. Geometry (template room-local; the room is turned by the layout and every tile goes through `~cox_room_at*`)

| | CCW | THRU | CW | source |
|---|---|---|---|---|
| anvil SW, angle | (14,22) 0 → 6x4 | (7,13) 3 → 4x6 | (12,22) 0 → 6x4 | C (jl2 lines `1 14 54: 29867 10`, `1 39 45: 29867 10 3`) |
| **Tekton's work tile** (his SW at the anvil) | (15,18), faces N | (11,14), faces W | (13,18), faces N | NR anvilTiles; agrees with C (centred on the working side) |
| smoke vent tiles (NR) | (22,9) (11,8) (11,21) (22,21) (23,16) | (22,8) (11,8) (12,21) (23,20) | (20,9) (12,7) (8,11) (9,21) (21,20) | NR; each inside a C `raids_lava_smoke3` 2x2 |
| spark origins (lava) | (9,8) (9,22) (16,25) (25,23) (24,9) | (24,9) (25,23) (21,22) (7,15) (9,8) (9,21) | (22,9) (6,8) (7,22) (15,24) (22,23) | NR sparkTiles |
| entrance fire / safe tile | (15,1) / (16,0) | same | same | NR |
| exit crystal | (6,14) | (14,24) | (24,16) | NR (F2's) |

## 3. The mechanics, tick by tick

Convention (manual Part 2): `T` is a server tick; npcs run before players, so a
decision on `T` reads where raiders **ended `T-1`**. A tell "seen at T" means
the client draws it after tick `T`'s packets.

| # | mechanic | decide tick / read tick | geometry | damage / numbers (solo, max) | tell the client sees | sources & ruling |
|---|---|---|---|---|---|---|
| T1 | **dormant at the anvil** | — | at his work tile, facing the anvil | not attackable (no op) | id 7540, hammer loop 7473 | W "seen working with his magnetic anvil"; NR spawns at anvilTiles |
| T2 | **wake** | the first `T` where a raider ended `T-1` within **4** tiles (Chebyshev) of his 4x4 footprint | box `[x-4, x+7] × [z-4, z+7]` | — | rise seq 7474, id → 7541; the entrance fire appears | SQ "Standing on the fourth tile away from Tekton in any direction will lure it off the anvil" vs NR box of 3: **SQ wins (G > NR)**. Probe P1 |
| T3 | **windup** (the swing) | every 3rd tick of the session clock (T6): reads raiders at end `T-1` | faces N/E/S/W of his middle toward the raider he picked (T5) | style uniform of stab / slash / crush | swing seq (7482/7483/7484, red 7493/7494/7492) + a face-coordinate turn, both on `T` | W "Stab, Slash, Crush", attack speed 3; NR `Utils.random(2)`, `stage % 3 == 0` |
| T4 | **strike** | `T+1`, reading raiders at **end of `T`** | the row outside the faced edge **and** the column outside his right-hand edge (facing S → W, W → N, N → E, E → S); 4 tiles each, no corners | accuracy (390+9)(64+64) vs the player's style defence roll; max **52** orange / **59** red (cache str bonus 20/30); Protect from Melee halves (T13) | the hitsplat on `T+1`. **No tell between the windup and the read**: the raider's end-of-`T` tile is fixed before the client draws `T`'s windup, so a dodge is decided off the 3-tick cadence, not the animation | W "attacking both the area in front of him and to his right simultaneously"; W/CM "a small delay before they hit… his attacks can cleave clockwise"; W/Str "it is possible for the animation to not appear while using this method, so a metronome is recommended"; TH "his actual hit comes a little bit after he starts his animation"; RS/SQ flinch (adjacent on the scan tick, away by the next). NR strikes one stage later too, but only its SLASH hits the right column (and with the PREVIOUS swing's facing): **W wins: every swing hits front + right, current facing** |
| T5 | **scan** | with every windup, at end `T-1` | the 16 edge tiles; **the 4 diagonal corner tiles do not count** | nobody on an edge → no swing; he leaves for the anvil (T7) | no swing seq on a cadence tick; leave-stance seq 7479 | W "if Tekton is unable to scan anyone in melee range on his next attack, he will stop and return"; SQ "checks if a player is on any of the tiles next to him, not including the corner tiles… every three ticks… one tick before his attack happens"; AC "Standing in the highlighted red tiles that are diagonal will cause Tekton to reset prematurely". NR lets a MOVING player on a corner count: **G wins** |
| T5b | which raider | at the windup | keeps the side he faces while anyone is on it, else a random raider on an edge | — | the face turn | W "If multiple players are surrounding Tekton, he will randomly change direction"; NR keeps a target still in reach. Solo: always the one raider |
| T6 | **session length** | a tick budget from his first swing: at a swing slot whose count ≥ budget he leaves instead of swinging | — | **first session 30 ticks = 10 swings; every later session 45 = 15 swings** | the stance-leave seq on a cadence tick | NR `stage >= (enraged ? 45 : 30)`; P "Phase Over" counter 27 from the first swing, 47 after an anvil (same split, ±1 swing); SQ "Tekton will always go back to the anvil after a set amount of time… This doesn't necessarily mean that you messed up"; W/Str "After 10-14 attacks". **Ruling:** the two tick-level sources agree on a fixed budget, first short, later long; the wiki's range is a prose summary they fall in (15 sits one above it). Probe P2 |
| T7 | **leaving for the anvil** | the swing slot of T5/T6 | walks to his work tile; **attackable all the way**; not solid | trample: raider under his footprint while he walks, **1-3 a tick** | seq 7479/7489, id → 7541/7543, walk | W/Str "He can still be attacked as he makes his way back"; W/Str "A player who remains under Tekton while he returns to his anvil will take rapid stomp damage"; NR 1-3 per walking tick |
| T8 | **enrage** | he rises enraged from every repair; red for **18 ticks from the moment he re-enters his stance**, then orange for the rest of the session | — | red: max 59, def bonuses stab 280 / slash 290 / crush 180, str bonus 30 (cache params); **defence LEVEL unchanged at 205** | id 7543/7544 (red) → 7541/7542 | W "enter an enraged form… max hit and defence bonuses increase… only last briefly"; W/Str "wear off around ~5 or so attacks"; TH "red aura… only lasts 15 seconds"; SQ "every phase after the first… red enraged form", "spec… when Tekton changes from its red form to its orange form"; NR `setEnrageTime(18)` at contact. **Ruling: NR's 18 ticks (6 swings) fits W's "~5 or so"; TH's 15 s (25 ticks) is a rough recollection.** NR also multiplies the defence LEVEL by 1.3 while red: **rejected** (W infobox def2 = 205; the cache's red records raise the bonuses) |
| T9 | **contact** | the engine's melee reach to his target (an edge tile, not a corner) | he stops; becomes solid (walkblocker under him); first swing next tick | — | enter-stance seq 7478/7488 on the contact tick; id → 7542/7544 next tick with the first swing | W "Tekton will walk to a player until he makes contact… stop moving and enter a combat stance. Tekton will turn into a solid NPC"; NR TektonEngageScript |
| T9b | **walking out** | rise `R`; target picked `R+2` (a random raider in the room, not behind the fire); he follows that raider | — | — | rise seq; walk with face-entity on the target | NR engage stages 0/2. SQ "lure Tekton as far away from the anvil as possible" — he follows wherever the raider goes |
| T10 | **the repair** | arrival tick `A` (his SW on the work tile) | at the anvil | **not attackable** (op1 "Attack" answers "Tekton's anvil protects him from your attacks."); heals **4 (1.33%) per volley**, 5 volleys = 20; no stat restored | lean seq 7475, id → 7545, faces the anvil | NR applyHit/processCombat; TH "you can't attack him"; AC "we can no longer attack him"; W "roughly 1.11 to 1.33%… every few ticks" + the page's note "heals 4 in normal max solo"; J 29 Nov 2023 "no longer resets his Defence"; SQ "will not regain any defense levels". W/Anvil No More says "restoring… drained stats": **J wins** |
| T11 | **sparks** | volleys at `A+3, A+7, A+11, A+15, A+19`; each throw reads its raider's tile at end `T-1`; lands `round((60 + 5·d)/30)` ticks later (d = Chebyshev from its lava tile), reading raiders at the end of the tick before the landing | 3x3 on the captured tile; hits anyone in it, "even if from another player" | **10-20** typeless each; **2 sparks per raider** below 3 raiders (1 each at 3-4); up to 4 raiders; raiders behind the fire are not targeted | projectile 660 from lava to the tile on the throw tick; blast 659 on the landing | W "sparks will fly from the lava… all players getting targeted by two sparks… 3x3 AoE"; W/Str "10-20 damage… avoided by simply running two spaces away"; W/CM "after five sets of sparks, he will resume combat"; TH "land exactly where you're standing every 2.4 seconds"; SQ "click the next tile the moment the meteors land"; W/Blind Spot "they may occasionally desync and hit on different ticks due to travel distance"; NR every 4 ticks, 6 volleys in a 23-tick smith. **Ruling: 5 volleys (W) at NR's 4-tick spacing inside NR's 23-tick repair; he rises at `A+23`.** Probe P3 |
| T12 | **smoke vents** | every 3rd server tick (`map_clock % 3 = 0`), whole fight incl. dormant | 3x3 round each vent tile (§2) | **1-3** typeless | the vents (`raids_lava_smoke3`) are always drawn; the hitbox overhangs the 2x2 vent on two sides | W/Blind Spot "pillars of smoke… erratic hitboxes and can deal 1-3 damage when standing on them"; SQ "four smoke vents at each corner of the room… 2 by 2 radius"; NR 3x3 every 3 ticks. **Ruling: NR's 3x3 (W says the hitbox does not match the vent), damage W = NR** |
| T13 | **Protect from Melee** | read at the strike (`T+1`) | — | **halves** his hit | overhead | NR `getMeleePrayerMultiplier() = 0.5`; SQ "tank the hit off prayer [for vengeance]… acceptable to venge on prayer as well" (a prayed hit still lands); W/Str "His attacks can deal heavy damage if not protected against". No source gives a number but NR. Probe P4 |
| T14 | trample | while walking (T7, T9b) | his 4x4 footprint | 1-3 a tick | — | W/Str, W/CM, TH, NR |
| T15 | **entrance fire** | lit on the wake; out on his death | `raids_tekton_fire` at (15,1) | crossing while he lives: 22.5% of current hp | the fire loc | NR TektonRoom/TektonFire; W/Blind Spot "Entering through Tekton's fire"; W trivia "cook raw food on the fire blocking the entrance" |
| T16 | ranged immune | at the hit | — | 0 | — | J Mod Ash 7 Sep 2023 |
| T17 | magic | at the hit | — | ÷5 (80% reduced); water spells +20% (weakness) | — | J Mod Ash 7 Sep 2023; J 25 Jun 2025 |
| T18 | **death** | his death trigger | — | drops at his middle: 5 stinkhorn, 2 overload (+), prayer enhance (+), revitalisation (+); journal loc; fire out; exit crystal (F2) | death seq 7495, the book, the items | W Drops; NR drop()/sendDeath |
| T19 | DWH / elder maul / BGS | — | — | first DWH/maul spec that makes contact always hits; a missed DWH/maul spec still drains 5%, a missed BGS 10 levels | — | J 29 Nov 2023, 13/6 Apr 2017; W Mechanics |
| T20 | stats (solo, max) | — | size 4 | HP 300; att/str 390; def 205; magic 205; att bonus +64; def bonuses 155/165/105 (red 280/290/180); str bonus 20 (red 30); regen 1 per 15 ticks; immune poison/venom/binding | — | W infobox; dps-calc monsters.json; J Mod Ash 9 Jul 2026 |

## 4. Audit (2026-10-18) — the content as found, against §3

| # | mechanic | ours (before) | OSRS (§3) | verdict |
|---|---|---|---|---|
| A1 | damage to Tekton | `[ai_queue2,raids_tekton_fighting_*]` held a re-target handler; `[ai_queue2]` is the engine's damage hook, so **every player hit on his fighting forms did nothing** | hits land | **FIXED** (handlers deleted) |
| A2 | combat driver | engine npc combat (`npc_attackplayer` every tick, `retaliate=yes`): he chased raiders, tracked them with FACE_ENTITY, swung on contact | fixed stance, own 3-tick clock, no chase (T3-T5, T9) | **FIXED**: his own timer; engine combat only for the walk out; every form `retaliate=no`, `defaultmode=none` |
| A3 | strike timing / read | damage applied on the windup tick against end-`T-1` tiles → flinching and corner-running impossible | strike `T+1` reading end of `T` (T4) | **FIXED** (`npc_queue` 5 → `[ai_queue5]`) |
| A4 | accuracy | `random(max+1)` on everyone in the wedge, no roll | attack roll vs defence roll (T4) | **FIXED** (`~npc_melee_attack_roll`, `stab/slash/crushattack = 64` added to the records) |
| A5 | swing styles / seqs | always `tekton_hammer_crush`, crush only | stab/slash/crush uniform, red seqs when enraged (T3) | **FIXED** |
| A6 | facing quadrant | centre `x+2`, `abs(dz) >= abs(dx)` → a raider on his east edge's bottom row read as "south" | the edge he is on | **FIXED** (exact edge test, `~cox_tekton_side_of`) |
| A7 | scan | a 2-tile box INCLUDING corners and a tile of slack | the 16 edge tiles, no corners (T5) | **FIXED** |
| A8 | session length | rolled 10-14 swings (normal) / 4-6 (enraged, read as "before the anvil") | budget 30 / 45 ticks (T6); the ~5 is how long the RED lasts (T8) | **FIXED**; old procs kept only for `cox_selftest` #135/#136 |
| A9 | enrage | red for the whole session; enraged `defence=246` (that is the CM level) | red 18 ticks from contact then orange; level 205 (T8) | **FIXED** |
| A10 | anvil position | walked to the ANVIL's SW tile (on top of a blockwalk loc) and spawned 4 south of it (CCW off by 1, THRU on the wrong side) | his work tile (§2) | **FIXED** |
| A11 | repair heal | 4 hp **every tick** (~80-90 a repair) | 4 per volley, 20 a repair (T10) | **FIXED** |
| A12 | repair length | 5 volleys every 4 ticks from the first tick | volleys `A+3..A+19`, rise `A+23` (T11) | **FIXED** |
| A13 | attackable at the anvil | yes ("hitting him here is the whole point") | no (T10) | **FIXED** for the click (`[opnpc1/apnpc1,raids_tekton_hammering]`) and magic (no op2); `~cox_tekton_prepare_hit` zeroes it once wired (O1) |
| A14 | sparks: tell | none (no projectile, no graphic) | projectile 660 + blast 659 (T11) | **FIXED** |
| A15 | sparks: mechanism | `queue*(cox_tekton_spark_resolve, 2)(coord, 0)`: a PLAYER queue with a coord argument (`queue*` carries only ints) on whichever raider the hunt last bound; flat 2-tick landing | npc queue, distance-based flight | **FIXED** (`npc_queue` 6 → `[ai_queue6]` on every form) |
| A16 | wake | any raider within 16 of his SW tile (the whole room) | 4 from the footprint (T2) | **FIXED** |
| A17 | rise / walk out | walked straight at `npc_findhero` (any player, no hunt) | rise seq, target at `R+2`, engine pursuit (T9b) | **FIXED** |
| A18 | solid in stance | no | walkblocker (T9) | **FIXED** |
| A19 | trample | none | 1-3 a tick under him while walking (T14) | **FIXED** |
| A20 | entrance fire | none | lit on wake, burn on crossing, out on death (T15) | **FIXED** |
| A21 | death | `[ai_queue3]` → `mes` + `%varp` points readout (player-scoped) and nothing else | journal, drops, fire out, room cleared (T18) | **FIXED** (`^cox_var_tekton_down`, `~cox_tekton_cleared(coord)` for F2) |
| A22 | regen | fighting forms only | every active form; none restored at the anvil (T10, T20) | **FIXED** |
| A23 | smoke | `queue*(combat_damage_player)` (a magic-style player queue a tick late, gear-reduced) | typeless 1-3 on the tick (T12) | **FIXED** (`~cox_tekton_hurt`) |
| A24 | ranged / magic | full damage | 0 / ÷5 (T16, T17) | **FIXED** (round 2): `~cox_tekton_prepare_hit` through `cox_combat.rs2` |
| A27 | exit crystal | none | `raids_blockage_orange` before the Passage until he dies, shattered (seq 7506, sounds 1021/3821, gone +2) | **FIXED** (round 2) |
| A25 | DWH/maul first hit, miss drains | not implemented | T19 | **OPEN (O2)** |
| A26 | stats, weakness, attack speed, hp | 300 / 390 / 205 / water 20% / 3 | same | correct |

## 5. Open — not fixed this round

- **O1 (DONE round 2, wired in cox_combat.rs2)** ranged immunity, magic ÷5 and the anvil's zero for an attack ALREADY
  in flight: `~cox_tekton_prepare_hit($damage, $style)` and
  `~cox_tekton_is_type(npc)` exist in `cox_tekton.rs2`; they need this branch
  in `skill_combat/scripts/player/player_hit_npc_prepare.rs2` (root/shared):
  `} else if (~cox_tekton_is_type(npc_type) = true) { $prepared = ~cox_tekton_prepare_hit($prepared, $style);`
- **O2** the DWH / elder maul guaranteed first hit and the miss drains (5% /
  10 levels for BGS): need a hook in `player_special_attack.rs2` (shared).
- **O3 (DONE round 2)** the exit crystal (`raids_blockage_orange`, NR 30017) is
  laid at spawn and shattered at death by `cox_tekton.rs2`.
- **O4** Tekton's journal: the book loc is laid; reading it (varbit 16191) and
  "no longer dropped after reading" are not wired.
- **O5** the onyx (1/450 solo) is not rolled (rewards owner).
- **O6 (DONE round 2)** `cox_selftest` #135/#136 now pin the session budget
  and the scan's edge geometry; the superseded procs and constants are gone.

## 6. Probes the first solver run must print (facts §3 could not settle)

| | question | how |
|---|---|---|
| P1 | does a raider 4 tiles out wake him and 5 not? | stand at 5 for 10 ticks, then 4 |
| P2 | swings per session: 10 first, 15 later | count swing seqs between stance-enter and stance-leave in the tick log |
| P3 | spark landing tick vs the drawn projectile | `spotanims`/`projectiles` rows vs `hit_player` rows |
| P4 | Protect from Melee halves (no source above NR gives a number) | max hit on prayer over 200 swings ≤ 29 |
| P5 | a raider on an edge at end `T-1`, two tiles away by end `T`, is never hit | the flinch replay |

## 7. Round 2 (2026-10-18): what the solver measured

48-seed sweep of `test/raids/cox_tekton.lua` (`::coxtest 0 N`): 46 PASS, 2
fail before the room (`::coxtest 0 46/47` exceeds the script instruction
budget in its layout search, cox.rs2). Every passing seed: Tekton dead, the
exit crystal shattered, **0 damage from any Tekton form** (swings, sparks,
vents, trample), every session exactly **10 swings first and 15 after**
(P2 confirmed, 286 sessions), no strike ever landing on an edge tile at its
read (P5 confirmed), mean 585 ticks. Strike = windup + 1 tick (P3/P4 via the
monitor: no clock mismatch). The engine-order change (E1, npc combat after
timers and queue) needed no change here: contact is keyed on map_clock and
the strike's `npc_queue` delay 2 still lands on the windup's next tick.

## 8. Round 3 (2026-10-18): the recordings

Read in full in [`docs/rsprox-analysis/cox/tekton.md`](../../../rsprox-analysis/cox/tekton.md) (tool `tools/cox/rsprox_tekton.py`). What changed in §3, all now in `cox_tekton.rs2`:

| row | was | recorded (now the content) |
|---|---|---|
| T2 | rise on the first tick a raider is seen within 4 | rise a tick after that (on the ring at the end of W, rise on W+2) |
| T3/T4 | every swing strikes front + right with the NEW facing, a tick later | a swing strikes with the facing HELD into it (the face sent with a swing aims the next); stab/crush: the front row, on T+1; slash: the front row and his right column, on T+2 |
| T6 | 30 / 45 ticks from the first swing (10 / 15 swings) | from contact: swings C+3..C+27 (9), after a repair C+2..C+47 (16); he swings on the last slot and turns to walk on that tick, the first step 2 ticks later |
| T8 | red for 18 ticks | red to C+17 |
| T9 | form change a tick after contact | stance seq (delay 30) and the fighting form on C; he stops on C (the route is cut) |
| T9b | walking form at the rise | walking form, first step and the entrance fire at R+2 |
| T10 | lean on arrival; 4/300 a volley; rise A+23 | lean the tick after (delay 15); heal base/100+1 every 3 ticks from A+3; rise on that cadence, A+21 at the latest (early rises unexplained) |
| T11 | 5 volleys from A+3 | a volley every 4 ticks on the room clock while he is the hammering form (through R+1); start cycle 0..30, slope 20..25, heights 0, sounds 155/156; CCW lava tiles (7,11) (20,5) (26,20) (12,26) |
| T18 | all on the death queue, crystal gone +2 | death seq on the killing tick; journal + drop at SW+(1,1) with the party message at D+3; fire out and the crystal's shatter on the room clock's next turn, the crystal gone 4 ticks later |

Still open: Protect from Melee's factor (820 is scaled for 54, every hit was prayed), what ends a repair early, the THRU/CW lava tiles, the journal's rotation.
