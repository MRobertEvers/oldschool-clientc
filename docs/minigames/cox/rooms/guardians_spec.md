# Guardians (Chambers of Xeric) -- tick spec and content audit

Unit: guardians_mystics. Written 2026-10-18 for `docs/minigames/cox/COX_FINISH_PLAN.md`
phases 1-3. Content: `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/scripts/cox_guardians.rs2`,
the guardian blocks of `configs/cox.npc` and `configs/cox.constant`.

## Sources (rank order: Jagex > recorded data > wiki > plugins > Near-Reality > ours)

| tag | source | file |
|---|---|---|
| J1 | Mod Ash, 15 Jun 2018 (quoted on the wiki): "The damage modifier is ((50 + Mining level + pickaxes's level req) / 150). That's applied to whatever damage you rolled" | `sources/wiki_Guardian_Chambers_of_Xeric.wikitext` |
| J2 | Mod Ash, 29 Jul 2019: capped "so that no new pick will be better than dragon ones" | same |
| J3 | Mod Ash, 13 Apr 2018: DWH special "should apply its special effect before the roll gets changed to zero" | same |
| J4 | Mod Ash, Discord 9 Jul 2026: "Stone guardians - 8" (regen ticks) | same |
| J5 | Update: Developer Blog: PvM QoL Week (2020-03-16): Elysian reduction for "attacks that hit through Protection Prayers", list includes "Stone Guardians in the Chambers of Xeric" | `sources/newsposts/wiki_Update_Developer_Blog_PvM_QoL_Week.wikitext:49` |
| J6 | Update: A Kingdom Divided: "A Pickaxe spawn has been added to all of the Stone Guardians room" (Poll 75: "an Iron pickaxe near the entrance") | `sources/newsposts/wiki_Update_A_Kingdom_Divided.wikitext:590`, `..._Poll_75_...:214` |
| W1 | Guardian infobox (rev 15315357): 2 statues, size blank (cache: 2), att/str 140, def 100, magic/ranged 1, str bonus +20, def stab +80 slash +180 crush -10 magic 0 ranged 0, max hit 20 (CM 33), slash, speed 4, HP 250 (maxed solo), aggressive Yes, immune thralls, xpbonus 7.5 | same |
| W2 | Guardian article: "attack with Melee and can occasionally stomp the ground, dealing moderate to heavy damage in a 3x3 area"; flinch "attacking the guardian and immediately running two spaces away"; HP formula `H = 151 x (1 + floor(T/2)) + floor(avgMining) x T`; "Attempting to enter the passage ... knocking the player back for moderate damage" | same |
| W3 | CoX/Strategies: "rock fall ... 3x3 area on their target's current position when the attack was performed"; "run two tiles away ... then walk back one tile"; PfM optional; iron pickaxe spawn; seeds 3-5 each; set the pickaxe to crush | `sources/wiki_Chambers_of_Xeric_Strategies.wikitext:493-512` |
| W4 | CA Dancing with Statues (elite): kill credit without falling-rock damage; "3x3 AoE centered on the tile it would fall on, so run two spaces away" | `sources/wiki_Dancing_with_Statues.wikitext` |
| W5 | Flinching: "When an opponent gets a hitsplat on itself, it starts a retaliation delay which equals half of its attack speed (rounded down)" | `sources/wiki_Flinching.wikitext` (fetched 2026-10-18, rev 15259611) |
| W6 | Pickaxe: every pickaxe attacks every 5 ticks, crush secondary | `sources/wiki_Pickaxe.wikitext` (rev 15358358) |
| V1 | Synq 0:30:39: best pickaxe on Smash; "stand three tiles away ... Attack any guardian and step three tiles away right after"; "Protecting melee does help"; one-click method once one is dead | `synq_transcript.md:258-265` |
| V2 | Tubby 18:38: "melee attack which can hit through prayer, but ... protect from melee reduces it by a bit"; four-tile marked tile, ctrl-click walk | `sources/transcript_tubby_solo_cox.md` |
| V3 | Academy (bg0mFA8jHIU), Theoatrix (e4tup60QPXM), Extile (0a61znGm0pc), Beleti (1aFJyKKdjn8) | `sources/transcripts/` |
| N1 | Near-Reality `RaidGuardianNPC.java`, `GuardiansRoom.java` (ids, structure, tiles, numbers no higher source has) | `~/Documents/git_repos/RSPS-NEAR-REALITY/.../chambersofxeric/` |
| C1 | Cache: npc 7569/7570 (size 2, op2 Attack, readyanim human_staffready_statue), 7571/7572 dead (readyanim human_dead, no ops); seq 1203 dragon_halberd_special_attack, 4278 emote_stampfeet; spotanim 645 motherlode_rockfall, 71 catapultimpact; loc 41754 raids_stoneguardians_pickaxe (Take), 26209 invisible_type8_blocking | `configs/all.*.compack`, `maps/m51_82.jl2`, `maps/m52_82.jl2` |

There is no recorded (Blert-style) data for CoX; the plugins under `sources/` have nothing on this room.

## Geometry (template tiles, unturned; the room turns with the layout)

| variant | left statue (SW of 2x2) | right statue | push tiles (gap) | blocked tiles (exit side) | Passage | iron pickaxe loc |
|---|---|---|---|---|---|---|
| CCW | 12,17 | 12,21 | 12,19 / 12,20 | 11,19 / 11,20 | west wall, x 9-10 | 20,18 |
| THRU | 16,23 | 12,23 | 14,23 / 15,23 | 14,25 / 15,25 | 14,26 | 9,20 |
| CW | 16,20 | 16,24 | 16,23 / 16,22 | 18,23 / 18,22 | 19,22 | 6,22 |

From N1 `guardianSpawnTiles` / `preventedTiles` (decoded by tools/cox_nr_locs.py into `~cox_guardian_spawn_tile` / `~cox_guardian_blocked_tile`), passages and the pickaxe from the template maps (C1). The statues stand either side of a two-tile gap; the exit is on the far side of the gap.

## Tick model

Npcs run before players. "T" is the server tick of the statue's decision; it reads raider tiles at the END of T-1.

| # | mechanic | decide tick / read tick | geometry | damage (solo, maxed) | prayer read | tell (what the client is sent) | source |
|---|---|---|---|---|---|---|---|
| 1 | Attack cadence | 4 ticks between swings (attack clock) | -- | -- | -- | -- | W1, monsters.json |
| 2 | Swing condition | T, if the target is ORTHOGONALLY beside the 2x2 footprint at end T-1 and the clock is ready; the statue never moves, keeps its target while he is away, and swings the tick he is back beside it if the clock allows | 2x2 footprint, cardinal adjacency (engine reach for attackrange 1) | -- | -- | -- | N1 (`combatAttack` returns 0 out of reach; `freeze`), engine |
| 3 | Style roll | T: a player within melee distance -> 50/50 melee or stomp; nobody within melee distance -> stomp | footprint distance <= 1 | -- | -- | -- | N1 `attack()` |
| 4 | Melee ("cleave") | T; hit lands T | target only | slash, accuracy roll (att 140, bonus 0), max 20 | PfM at T: halved (factor OURS, see R3) | npc seq 1203 at T | W1, N1, J5, V1, V2 |
| 5 | Stomp (falling rock) | swing T fixes the tile = target's tile at end T-1; lands T+1 (npc phase) and hits every raider within 1 tile (Chebyshev) of it at end of T | 3x3 centred on the tile | typeless 0-15, no accuracy | none (hits through prayer) | npc seq 4278 at T; projectile 645 to the tile at T (lands at T+1); map graphic 71 on the tile at T+1 | W2, W3, W4, N1 `stomp()`, J5 |
| 6 | Dodge window | a raider who is >= 2 tiles from the stomp tile at the end of T is clear; one who moved only 1 tile is still in the 3x3 | -- | -- | -- | as #5 | W3, W4 |
| 7 | Flinch | a hit landing on the statue at npc phase H (a player melee swing on tick P lands at H = P+1) delays its next swing to H+2 (half of 4), at most once per 4 ticks | -- | -- | -- | the statue's hitsplat | W5, N1 `flinch()` |
| 8 | Partner aggro | a hit on either statue: the idle partner takes the attacker, first swing no sooner than 2 ticks later | -- | as #4/#5 | -- | -- | N1 `autoRetaliate` / `_autoRetaliate` |
| 9 | Aggression | each tick, an idle statue takes a raider who is beside it | footprint distance <= 1 | as #4/#5 | -- | -- | W1 (aggressive Yes), N1 `checkAggressivity` |
| 10 | Exit blocked | from spawn until both are dead: invisible blocking locs on the two blocked tiles | blocked tiles | -- | -- | (none; locs are invisible) | N1 `loadRoom`/`finish` |
| 11 | Knockback | every 3rd tick (statue tick counter), raiders on or orthogonally next to a blocked tile (read end of previous tick); resolves next tick if he has not moved: put on the push tile, 15-30 typeless, +40 points; the nearer statue swings (seq 1203) and targets him (full delay); the other STOMPS the other push tile (#5, lands a tick later, almost certainly on him) and targets him | blocked tiles, Euclidean <= 1 | 15-30 + a stomp 0-15 | none | statue seqs 1203 + 4278, rock projectile, graphic 71 | W2, wiki CoX points, N1 `process`/`pushBack` |
| 12 | Player damage | every hit on a statue: x (50 + Mining + pick req)/150 applied to the rolled hit, req capped at 61 (crystal = dragon); no wielded pickaxe -> 0 | -- | dragon @ 99: x1.4 | -- | -- | J1, J2, W2 |
| 13 | Defence | crush -10 vs slash +180: Smash | -- | -- | -- | -- | W1, W3, V1 |
| 14 | HP | 250 each solo (151 x (1 + floor(1/2)) + 99 x 1) | -- | -- | -- | -- | W1, W2 |
| 15 | Regen | +1 to every drained stat incl. hitpoints every 8 ticks | -- | -- | -- | health bar creeps back | J4 |
| 16 | Death | the statue is replaced by its demolished form (7571/7572), which stays; 3-5 each of buchu/golpar/noxifer seeds on a push tile; the second death removes the exit blockers | -- | -- | -- | npc death seq, then the lying statue | W1/W3 drops, wiki trivia, N1 `sendDeath` |
| 17 | Iron pickaxe | "Take" on the template loc gives an iron pickaxe; the loc stays | 20,18 / 9,20 / 6,22 | -- | -- | loc 41754 | J6, W3 |

### Rulings

- R1 Pickaxe damage: J1/J2 (Jagex) over Theoatrix's "mining changes your accuracy" (V3) and NR's crystal +0.5 (N1).
- R2 "Both must die together / regen if one is left": no source says so. Synq plays on with one dead ("once you've defeated one of the guardians, you can use this one click method") -- the statues are independent; only the 8-tick regen applies. Nothing implemented.
- R3 Protect from Melee vs the cleave: J5 says it hits through, V1/V2 say the prayer helps; nobody gives a number. 50% damage is OURS (the raid's partial-prayer convention, NR Tekton/mystic 0.5). Probe row P2 below.
- R4 Stomp max: W2 "moderate to heavy", W1 max hit 20, N1 `getMaxHit(15)`. 15 kept (the only number); flagged.
- R5 Knockback period 3 ticks and 40 points: N1 only (wiki: points awarded, cap formula unknown).
- R7 Regen includes hitpoints (2026-10-18 review): J4's list is Jagex's per-npc regen timer and includes "Vasa's crystal - 9" and "Vespula's grubs - 10000 (i.e. never)", which have no combat stats to drain. So the timer moves hitpoints too, as the RS2 engine's npc regen does for every stat. +1 hp per 8 ticks is right.
- R8 Unsourced numbers, searched again 2026-10-18 (wiki Guardian, CoX/Strategies, Challenge Mode pages): no public figure for the stomp max (R4: N1's 15), the knockback damage (R5: N1's 15-30), or the Protect from Melee factor (R3: OURS 50%). They stay flagged until a recorded source exists.
- R6 Stomp on a DIAGONAL target (N1 attacks a target at a footprint corner, always as a stomp): the engine never swings at a diagonal; not modelled. Probe row P3.

## Audit (mechanic / ours before 2026-10-18 / OSRS / verdict)

| mechanic | ours (before) | OSRS (spec) | verdict |
|---|---|---|---|
| Statues stand still | `defaultmode=wander`, `wanderrange=5` from the generated record: the right statue walked about (probe npc_tile rows) | frozen (N1) | FIXED: `moverestrict=nomove`, `defaultmode=none` (cox.npc) |
| Defence/attack bonuses | none stated: every style met 0 | stab 80 / slash 180 / crush -10 / magic 0 / ranged 0, str +20 (W1) | FIXED (cox.npc) |
| Magic/Ranged level, xp bonus | 0 / x1.0 | 1 / +7.5% (W1) | FIXED |
| Stomp resolution | a PLAYER queue whose `npc_uid` aborted ("NPC_UID requires an active entity") -- no stomp ever hit | lands next tick on the statue's queue | FIXED: `npc_queue(4, offset, 1)` -> `[ai_queue4]` -> `~cox_guardian_stomp_land` |
| Stomp tell | animation only | rock projectile 645 + graphic 71 at the tile (N1) | FIXED |
| Melee accuracy | always landed 0..20 | accuracy roll (N1 `melee()`) | FIXED |
| Melee vs PfM | ignored | through prayer, reduced (J5, V1, V2) | FIXED with an OURS factor (R3) |
| "Anyone adjacent" for the style roll | SW-tile radius 1: a player on the north/east side was never "adjacent", always stomped | footprint distance | FIXED |
| Partner aggro | none | both statues take the attacker (N1) | FIXED (`~cox_guardian_partner_retaliate`) |
| Aggression | retaliate only | aggressive (W1, N1) | FIXED (adjacent raiders only; a statue reaches nobody else) |
| Exit gate | knockback only, polled every 3 ticks: a running raider could slip past | blocking locs + knockback (N1) | FIXED (`invisible_type8_blocking` on the blocked tiles, removed on the second death) |
| Knockback poll | left statue only: killing the left first switched it off | while either stands | FIXED (right polls once left is down) |
| Knockback trigger shape | Chebyshev <= 1 (diagonals) | Euclidean <= 1 (N1) | FIXED |
| Knockback follow-up | two animations, no target, wrong way round (farther statue swung) | nearer swings and targets, other stomps the other push tile and targets (N1) | FIXED |
| Knockback poll radius | huntall from the room's corner, radius 32 (neighbouring rooms) | the room | FIXED (room centre, radius 16) |
| Death | register only; npc vanished; no drops | demolished statue stays; seeds (W1, W3, N1) | FIXED (`~cox_guardian_fall`, cox.npc dead records) |
| Iron pickaxe | loc present, Take did nothing | gives an iron pickaxe (J6, W3) | FIXED (`[oploc1,raids_stoneguardians_pickaxe]`) |
| HP formula, damage formula, regen 8, attack speed 4, flinch | as spec | as spec | OK (unchanged) |
| Points multiplier on hits | `~cox_on_damage` (root's) | NR scales points by the damage modifier | NOT CHECKED (root's points file) |
| DWH/elder maul special before the zeroing (J3) | the drain was gated on the hit that got through, so a Guardian (which zeroes non-pickaxe hits) was never drained | drain applies on the rolled hit | FIXED 2026-10-18: `~cox_spec_drain_lands` (cox_guardians.rs2), called by pvm_dragon_warhammer.rs2 and pvm_elder_maul.rs2; selftest #179 |
| Push "slide" | `p_telejump` snap | ForceMovement slide | OPEN (engine has no ForceMovement) |
| Statue facing | engine faces the target | N1 re-faces a fixed tile on each attack | OPEN (cosmetic) |
| Defend animation | generated `human_unarmedblock` (424) on every hit | unknown | OPEN (unsourced either way) |

## Probe evidence (2026-10-18, seed 5, `::coxgoto 1 0`, dragon pickaxe, no armour)

Before the fix (`scratchpad/probe/base_g`): right statue npc_tile moved 6517,81 -> 6519,78 -> back; stomp `4278` at ticks 37, 42, 62, ... with no hit_player; every stomp aborted at `cox_guardians.rs2:233 NPC_UID`; melee hits 12-18 every 5 ticks (each 2 ticks after the player's hit: the flinch).
After (`::coxtest 1 0`, scratchpad probes `after_g`, `g2`, `g5`): no aborts; statues never move; stomp = seq 4278 + projectile 645 on the swing tick, graphic 71 and the 3x3 hit on the next tick; cleave rolls accuracy (0s seen); standing in the gap: knockback 29 then the other statue's rock 13 a tick later, every 3 ticks; both statues killed (god mode): dead forms 7571/7572 spawned on the tick of death, 3-5 of each seed on the push tiles, the blocked tiles walkable after the second death. Not observed: the iron pickaxe Take (the oploc went out; the probe had no inventory read). Found: the engine swings an npc before it drains its queue (see guardians_solver.md E1), so a flinch lands a tick late.

## Probes the spec cannot settle (rows the first solver run prints)

- P1 stomp landing vs a raider who left on tick T (expect clear at 2 tiles, hit at 1).
- P2 cleave damage with PfM on/off over 100 swings (expect half; the factor is OURS).
- P3 a raider standing at a footprint corner: N1 stomps him, ours never swings.
- P4 knockback when standing in the gap: 15-30 + the other statue's stomp on the next tick.
