# Vanguards: tick spec and content audit

Unit `vanguards`, 2026-10-18. Content: `cox_vanguards.rs2` (whole file), the
Vanguards block of `cox.npc`, the `^cox_vanguard_*` / `^cox_var_vanguard_*` /
`^cox_npcvar_vanguard_*` constants in `cox.constant`. Method:
`docs/CONTENT_SOLVER_MANUAL.md` phases 1-3. Solver plan: `vanguards_solver.md`.

## Sources (ranked; the higher one wins a conflict)

| rank | id | source | file |
|---|---|---|---|
| Jagex | J1 | Mod Ash, 14 Sep 2021 (quoted on the wiki): shells go down to rotate after "20 - 36 ticks, I think" of fighting | `sources/wiki_Vanguard_2026-10-18.wikitext` (Mechanics) |
| Jagex | J2 | 21 May 2020 "Vanguard Improvements": defence lowered / hp raised, melee one's magic and melee defences swapped, **aggro range reduced**, one spawn shifted, at least one overload | `sources/newsposts/wiki_Update_Vanguard_Improvements.wikitext` |
| Jagex | J3 | 28 May 2020: the health-gap visual cue; reset gap 40% for teams of four or fewer (was 33%) | wiki Changes |
| Jagex | J4 | 16 Jun 2021 "A Kingdom Divided": "Vanguards can be damaged during their transition phase" (Poll 75 Q27: "you will trigger a reset while they are transitioning, and the pending damage will reset them"); 23 Jun 2021 "can no longer become immortal after reaching 0 HP" | `sources/wiki_Chambers_of_Xeric.wikitext:1016`, `sources/newsposts/wiki_Update_Poll_75_Game_Improvements_Blog.wikitext:246`, wiki Changes |
| Jagex | J5 | 29 Nov 2023: drops standardised (melee 2 aid + elder, ranged 1 aid + 2 revit + twisted, magic 1 aid + prayer enhance + kodai), overload rule unchanged | `sources/wiki_Chambers_of_Xeric.wikitext:961-965` |
| Jagex | J6 | 18 Jun 2020: a toggleable boss HUD with each Vanguard's healthbar | `sources/wiki_Chambers_of_Xeric.wikitext:1060` |
| plugin | P1 | de0 CoX Vanguards (Plugin Hub `dey0/pluginhub-plugins` 48a8c0a): one NPC object per style kept across shells; hp tracked from hitsplats; **+1 hp each time a vanguard turns into the walking form**; HEAL splat = all back to full; safe damage `base*4/10 - 1`; solo base `180 * cb / 126` (CM x1.5); melee wander box radius 9 round the dig tile, recorded once | `sources/de0/CoxVanguardsPlugin.java`, `CoxVanguardsHighlight.java`, `CoxVanguardsConfig.java`, `CoxVanguardsDatabox.java` |
| wiki | W1 | Vanguard: infobox (levels, bonuses, max hits 22/18/22, ids, size 3, 4-tick), Mechanics (burst when a player nears a hole; 3 hits per attack, AoE for ranged/magic; prayer reduces, never nullifies; 40%/33.3% gap -> shuffle + heal; swirl tell; "cannot be attacked while in their shell, but damage dealt just before they shuffle will still count"; lose aggression on shuffle; fatal hit in the shell -> 1 hp), drops, Changes | `sources/wiki_Vanguard_2026-10-18.wikitext` (refetched; the 2026-08-27 copy `wiki_Vanguard.wikitext` lacks several Changes rows) |
| wiki | W2 | Strategies "Vanguards": prayers "significantly reduce the accuracy and damage"; dark spiral on the most-health, light on the least; "If the difference goes past the threshold, all three Vanguards will shuffle and fully heal"; shuffle clockwise or counterclockwise, untargetable while shuffling, 3-6 damage to players in their path "every few ticks", re-emerge once all three are in position; ranged one won't move while you stand under it; solo: chase the highest, stay at the edges | `sources/wiki_Chambers_of_Xeric_Strategies.wikitext:445-491` |
| wiki | W3 | CA tasks: Perfectly Balanced (no reset), Together We'll Fall (all dead within 10 s of the first) | `sources/wiki_combat_achievements_cox.tsv:16-17` |
| video | V1 | Synq 1:38:49-1:45:29: spawn style random; keep within 40%; white tornado = too much damage dealt to it, darker = not enough; follow the one you need as it retreats; trample; ranged one won't move if you stand under it; magic one moves after >1 tick under it; melee one can be safespotted (leash from its spawn); 4-tick vanguards vs 5-tick weapons; cap max hit (gear, prayers) | `synq_transcript.md:546-572` |
| ref server | N1 | Near-Reality `npc/Vanguard.java`, `room/VanguardRoom.java`: positions (centre tiles), wake `< 7`, rise 2-step, aggression 3, ranged can't walk outside a shuffle, AoE tile picker, projectiles/graphics/sounds, prayer x0.33, fall/rise animations, shuffle 1 + 10 ticks, gap check at the shuffle only (33% flat), death rule, drops, crystal 30017, book loc 30026 | `~/Documents/git_repos/RSPS-NEAR-REALITY/near-reality-server-main/core/.../chambersofxeric/` |
| cache | C1 | npc 7525 dormant, 7526 walking (op1 Attack), 7527 melee / 7528 ranged / 7529 magic (op2 Attack), size 3; seq ids from `configs/all.seq.compack`; spotanims 56/57 swirls, 1331 magic, 1332/1333/1334 three rock sizes | `OSRS-Content/osrs239-content/configs/` |

## The tick spec

Solo, normal mode, one maxed raider (base hp 180). "Tick T" is the server
tick; the npc phase runs before the player phase, so a decision an npc makes on
T reads raider tiles and hitpoints as they were at the END of T-1.

| # | mechanic | decide tick | reads | geometry | numbers (solo) | tell the client sees | source |
|---|---|---|---|---|---|---|---|
| V1 | room load | build | - | three 3x3 holes; SW tiles (template, turned with the room): CCW (20,16)(12,21)(11,11); THRU (20,17)(12,21)(12,11); CW (18,12)(12,21)(8,12) | - | dormant npc 7525 (readyanim `vanguard_dormant` 7427) | N1 positions are CENTRES (`moveLocation(-1,-1)`) |
| V2 | wake | the first tick a raider is within 6 of any dormant one | raider tiles end of T-1 | Chebyshev from the dormant SW tile (NR: Euclidean `< 7`) | - | - | N1, W1 ("gets near one of the holes") |
| V3 | rise | wake tick L: each hole becomes a RANDOM style (3! permutations) and plays `vanguard_spawn` 7428; L+1: each plays its opening transition (melee 7438, ranged 7443, magic 7433); L+2: fights (aggressive) | - | - | - | npc type change 7525 -> 7527/8/9; seqs | N1 launch, V1 "spawn point of each vanguard is completely random" |
| V4 | aggression | every tick a vanguard has no target | raider tiles end of T-1 | huntrange 3 from the npc; plus retaliation when hit | - | the vanguard faces / attacks | N1 `setAggressionDistance(3)`, J2 |
| V5 | melee swing | every 4 ticks while the target is in melee reach | target at the swing | melee reach of a 3x3 | 3 independent hits, each: accuracy roll (150 atk, +20) vs the raider's defence (crush, see U3), then 0..18; all land on the swing tick | `vanguard_attack_melee` 7441, sound 3308, three splats same tick | W1 infobox, N1 (three `delayHit(0)`) |
| V6 | ranged volley | every 4 ticks, target within 10 and in sight | who stands on each tile AT THE SWING | 3 tiles: the target's + 2 random distinct walkable tiles within 3 of it, never under the vanguard | each hit: accuracy (150 rng, +40) then 0..22; lands with its rock (30 + 30 cycles: 2 ticks) | `vanguard_attack_ranged` 7446; rocks 1332/1333/1334 to each tile; impact 305 + sound 360 at landing | W1, W2 ("hit by two or even all three"), N1 |
| V7 | magic volley | as V6 | as V6 | as V6 | each: accuracy (150 mag, +40) then 0..22; lands with its orb (15 + 75 cycles: 3 ticks) | `vanguard_attack_magic` 7436, sound 3530; projectile 1331 to each tile; impact 659 + sound 156 at landing | W1, N1 |
| V8 | protection prayer | melee: the swing tick; ranged/magic: the LANDING tick | the raider's overhead then | - | the right overhead leaves 33% of the rolled hit (floor); never 0 unless the roll was | splat | W1 ("will not nullify ... reduce it by a significant amount"), N1 0.33 |
| V9 | movement | engine chase | - | melee: chases, leashed 9 from its dig tile (where it rose, kept through every shuffle); ranged: never moves outside a shuffle (stand under it and it cannot hit you); magic: steps out from under a raider, else holds | - | npc walks | P1 wander box, V1, W2, N1 `addWalkStep` |
| V10 | shuffle timer | 20-36 ticks (uniform) after each emergence | - | - | - | - | J1 |
| V11 | gap reset | any active tick on which (most hp - least hp) >= floor(0.4 x base) among the LIVING vanguards (33.3% for 5+) | hitpoints at the end of T-1 (our poll; OSRS presumably the hit's own tick, see U4) | - | 72 at base 180 | every living vanguard gets a heal splat for what it was missing, then V13 starts the same tick | W1, W2, J3, P1 (integer form), N1 (heal splat) |
| V12 | gap tell | every 2 ticks while the gap >= 30% of base (OUR GUESS for "close", see U5) | as V11 | on the npc | - | swirl 57 (dark) on the most-health, 56 (light) on the least; `raids_boss_swirl_1_fast` 2 ticks long | W1, W2, V1, C1 |
| V13 | shells close | S (timer or reset) | - | - | target dropped, no new target | each plays its closing transition (melee 7442, ranged 7447, magic 7437), still the combat type (damageable) | N1, J4, W1 ("lose aggression") |
| V14 | walk | S+1: each becomes 7526, gains 1 hp (no splat, capped), and walks to the next hole, all the same way round (a coin per shuffle) | - | the three holes as a ring | - | type change -> 7526 (readyanim 7430, walk 7429); npc walks | N1, W2, P1 (+1) |
| V15 | trample | every tick of the walk | raider tiles | any raider inside a walking vanguard's 3x3 | 3-6 (wiki; NR 1-5) | splat | W2, N1 |
| V16 | re-emerge | the tick all walking vanguards stand on their holes | - | - | - | type back to ITS OWN style + opening transition; aggressive at once | W2, N1, P1 (style kept) |
| V17 | killing blow | the death queue | hitpoints | - | outside the active stage: survive at 1; active and all three alive and the other two's max >= the reset gap from 0: survive at 1 (V11 resets next tick); otherwise dies. Once one is dead the other two may die in any order | death anim 7432 | W1, J4, N1 sendDeath |
| V18 | drops | on each death | - | at the vanguard's centre | melee 2 Xeric's aid (+) + elder (+); ranged 1 aid + 2 revitalisation (+) + twisted (+); magic 1 aid + prayer enhance (+) + kodai (+); overload (+) 1/3 each, at least one per room | ground items | J5, W1 drops |
| V19 | room cleared | the last death | - | crystal 2x2, template SW ccw (4,15), thru (15,27), cw (22,16), turned with the room | - | exit crystal (loc 30017 `raids_blockage_orange`, placed at spawn) plays `raids_crystal_smashing` 7506 + sounds 1021/3821 and is removed 2 ticks later (round 2); book loc 30026 not placed | N1 VanguardRoom/Raid.shatterCrystal |

## Conflicts and rulings

| # | conflict | ruling |
|---|---|---|
| R1 | gap check: NR only at the shuffle (and only then heals); wiki: past the threshold they "shuffle and fully heal" at once | **wiki** (and Poll 75's "pending damage will reset them"): checked every active tick; a reset heals AND starts a shuffle |
| R2 | gap size: NR 33% flat; wiki/J3 40% (<= 4) / 33.3% (5+) | **Jagex/wiki**; integer form from P1 (`floor(base * 4 / 10)` resets) |
| R3 | styles at a shuffle: our old code re-dealt them each shuffle ("reassigned their styles"); P1, V1 and NR keep each vanguard's style | **P1/NR/V1**: a vanguard keeps its style (and hp) for life; only its hole changes |
| R4 | trample: wiki 3-6 "every few ticks"; NR 1-5 every tick | wiki damage, NR cadence (wiki's cadence is unquantified, U6) |
| R5 | shuffle length: NR 10 ticks fixed; wiki "once all three are in position" | **wiki**: arrival ends it (a 24-tick cap guards a blocked path only) |
| R6 | damage in the shell: NR zeroes it; wiki/J4: damage dealt before or during the transition counts; fatal -> 1 hp | **Jagex/wiki** |
| R7 | stats: NR json is the pre-May-2020 band (def 210, hp 150); wiki/dps-calc the current | **wiki** |
| R8 | +1 hp per shell: P1 only (no other source mentions it) | **P1** (recorded behaviour players' counters rely on); silent heal |
| R9 | drops: wiki Strategies says magic also drops a revitalisation; main page and the Nov 2023 note do not | **J5** |
| R10 | first shuffle: NR 10 s (~17 ticks) after the rise, then 20 s; J1 20-36 | **J1** for every shuffle |
| R11 | ranged projectile: NR one spotanim (1332) for all three rocks; the cache has three sizes | ours: one of each per volley (cache reading, U7) |

## Audit (before 2026-10-18 -> after)

| mechanic | ours before | OSRS | verdict | now |
|---|---|---|---|---|
| hole tiles | NR centre tiles used as SW tiles: every vanguard 1 tile NE of its hole | V1 | WRONG | fixed (-1,-1) |
| defence / attack bonuses | none stated: every bonus 0, so no combat triangle at all | V5-V7 infobox bonuses | WRONG | stated in `cox.npc` (+ damagetype, anims, xp 27.5/7.5/5%) |
| defend / attack anims | generated: all three used the MAGIC defend/attack seq | per style | WRONG | per style in `cox.npc` |
| aggression | every active tick each vanguard `npc_attackplayer`ed the nearest raider within 32: the whole room aggroed at once (baseline probe: the magic one hit from across the room from its first tick) | huntrange 3 + retaliation | WRONG | engine aggression, huntrange 3, `npc_sethuntmode` on at emergence / off at the shell (the engine never re-seeds huntmode on a type change) |
| movement | melee maxrange 32; ranged givechase=no only (maxrange 7 generated); combat forms generated `defaultmode=wander`, wanderrange 5 | V9 | WRONG | melee maxrange 9, ranged `moverestrict=nomove` maxrange 32, all `defaultmode=none` wanderrange 0 |
| accuracy | none: every hit `random(max+1)` | accuracy roll vs defence | WRONG | rolled per hit |
| prayer | read at the swing for all three tiles, from the TARGET's overhead | melee at the swing; projectiles at landing, the struck raider's | WRONG (multi) | read at landing per raider (`[queue,cox_vanguard_land]`) |
| AoE tiles | could land under the vanguard (only its SW tile excluded); projectiles from the SW corner; one rock gfx; ~map_projectile generic timing (magic landed 2 ticks after the swing) | V6/V7 | WRONG | footprint excluded, centre source, NR timings (magic 3 ticks, ranged 2), three rock sizes |
| melee sound | none | 3308 | MISSING | added |
| shuffle: styles | re-dealt every shuffle (an npc kept its hp but changed style) | kept | WRONG | kept (`^cox_npcvar_vanguard_style`) |
| shuffle: who moves | only npcs found exactly ON a hole tile were shelled/walked; a chasing melee or a moved magic one kept fighting | all living | WRONG | all living, by uid |
| shuffle: countdown | decremented only by the MELEE vanguard's timer: once it died the room never shuffled again | room clock | WRONG | once a tick for the room (`^cox_var_vanguard_clock`) |
| shuffle: closing | instant type change, no animation, target kept | 1-tick closing transition, target dropped | WRONG | V13 |
| shuffle: +1 hp | none | P1 | MISSING | added |
| shuffle: opening | no animation | opening transition | MISSING | added |
| trample | SW tile only, every tick, 3-6 | 3x3, 3-6 | WRONG | 3x3 |
| gap reset | healed (no splat, a red "wounds close" chat line no source has, `vanguard_heal` anim) and kept fighting; no shuffle | heal splats + immediate shuffle | WRONG | V11 |
| gap tell | none | swirls 56/57 | MISSING | added (trigger point guessed, U5) |
| rise | walking form + spawn anim, 2 ticks, then style | style at once + spawn anim, opening anim, fight | WRONG | V3 |
| wake range | 7 | 6 (NR `< 7`) | off by one | 6 |
| death in shell | survive at 1 (melee/ranged/magic only; a fatal hit landing on the WALKING form had no handler: default death, no drops) | survive at 1 | WRONG | `[ai_queue3,raids_vanguard_walking]` added |
| death spread rule | per TYPE lookups (broke once styles moved) | V17 | partly | by uid |
| drops | none (death_drop null, no script drop) | V18 | MISSING | added |
| npc timers with no player | no aborts found in the baseline | - | OK | still none (probe) |
| clicking the shell | nothing | NR message | MISSING | "The Vanguard is protected by its shell." |

## Unsettled (written down, not guessed silently)

| # | question | what we do | how to settle |
|---|---|---|---|
| U1 | hp scaling with combat level / party (`180 * cb / 126`, P1) | 180 (one maxed raider) | `cox_scaling.rs2` (foundation) |
| U2 | can a raider keep attacking the walking form (player-side combat loop)? | yes today (E2): engine work | E2 |
| U3 | the melee vanguard's damage type (stab/slash/crush all +20) | crush | a recorded hit-chance sample; low impact (equal bonuses) |
| U4 | gap read point: OSRS likely on the hit; ours polls at the next npc phase (1 tick later) | poll | recorded footage of a reset (splat tick vs heal tick) |
| U5 | "close to the threshold" for the swirls | 30% of base (75% of the 40% gap) | footage with the plugin's hp count visible |
| U6 | trample cadence ("every few ticks") | every tick in the footprint | footage |
| U7 | which rock size goes where | 1332 target, 1333/1334 extras | cache viewer on footage |
| U8 | projectile hit decided at the swing (NR) vs on landing; Synq's "step back under too soon -> two projectiles" | at the swing (NR) | footage of a dodged volley |
| U9 | accuracy cut by protection prayer ("reduce the accuracy and damage") | damage only (x0.33) | none available; NR models damage only |
| U10 | attack range of the casters | 10 | none found |
| U11 | the boss HUD (J6) with three bars, exit crystal, book, CA tasks | not implemented (HUD and room exits are foundation work) | F2 / HUD owner |

## Probes (scriptrun, 2026-10-18)

Server `src/build_cox_vanguards_opt/torirsserver`, packs via
`tools/cox/locked_packs.py`, probe `::maxstats`, `::coxtest 11 0`,
`::coxgoto 11 0|1` (room centre), then `::coxvgdmg <style> <n>` (a damage
splat on one vanguard; a probe aid in `cox_vanguards.rs2`) and godsword attacks.
Seed 7 (`--name 7`). Baseline (old content, `::coxseed 5`): the magic vanguard
hit the raider from across the room from its first active tick, every 4 ticks,
no accuracy roll; nothing else happened in 30 ticks.

| # | probe | result after the fixes |
|---|---|---|
| P1 | wake + rise | wake T14: 7525 -> 7528/7527/7529 + 7428; T15 opening anims 7443/7438/7433; fighting from T16. OK |
| P2 | aggression | standing at the room centre (no vanguard within 3) for 90 ticks: no swing at all; attacking the ranged one drew only it (swings every 4: anim T115 -> splat T117; magic anim T127 -> splat T130). OK |
| P3 | shuffle | timer shuffle T42 (26 ticks after the rise): closing anims T42, 7526 T43, walk 10 ticks, back to the SAME style per npc with opening anims T53; trample 3-6 on the raider in a walker's path (T46-48: 6, 5, 6); +1 hp on each later shell (T140: 6 -> 7). OK |
| P4 | gap reset | 80 on the ranged one at T57 -> heal splat 80 and closing anims T58, walking T59. OK (1-tick read lag, U4) |
| P5 | deaths, drops | ranged: aid, 2 revitalisation, twisted (+ overload on one seed's roll); magic: aid, prayer enhance, kodai; melee (last): 2 aid, elder, and the guaranteed overload when none had dropped. A fatal 7 on the magic one in its shell left it at 1. No `abort`, no `no primary player bound` in any run |

## Engine gaps found (not content; for the root session)

| # | gap | effect | wanted |
|---|---|---|---|
| E1 | `[ai_queue3]` runs after the death animation (`npc_death_arrive` plays `death_anim` first) | a killing blow that the rules turn into "left at 1 hp" (V17) plays `vanguard_death` 7432 for ~2 ticks before the vanguard carries on: a false death tell | a pre-death hook (NR `sendDeath` decides before any animation), or a per-type "script decides death" flag |
| E2 | the player's attack loop keeps hitting an npc after it changes into a type with no op2 Attack | a raider who was attacking when the shell closed keeps landing hits on the walking form (probe: a godsword hit 1 tick after the retype). The wiki allows hits launched before the move; later ones should stop ("They cannot be targeted while shuffling") | player combat drops a target whose current type offers no Attack op. ROUND 2: done in content -- `~cox_vanguard_prepare_hit` (cox_combat.rs2 dispatch): a swing prepared against the walking form deals 0, says "The Vanguard is protected by its shell." and `p_stopaction`s; hits prepared before the shell still land |
