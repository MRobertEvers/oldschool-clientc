# Muttadiles: tick spec, audit, fixes (2026-10-18; RSProx pass the same day)

Unit: muttadiles. Content: `OSRS-Content/.../minigame_cox/scripts/cox_muttadiles.rs2`,
the Muttadile blocks of `configs/cox.constant` and `configs/cox.npc`.
Plan: `COX_FINISH_PLAN.md`. Method: `docs/CONTENT_SOLVER_MANUAL.md`.
Solver plan: `muttadiles_solver.md` (next to this file).

## Sources (all under `docs/minigames/cox/sources/` unless a URL)

| tag | source | rank |
|---|---|---|
| **J23** | Jagex newspost *Chambers of Xeric Changes*, 29 Nov 2023 (`newsposts/wiki_Update_Chambers_of_Xeric_Changes.wikitext`, l.60-65 and l.307-330) | Jagex |
| **AshT** | Mod Ash, tree HP (11 Oct 2018), axe irrelevant (12 Dec 2018), chop odds (2 Nov 2018), chop damage (13 May 2024) -- quoted on `wiki_Meat_tree.wikitext` | Jagex |
| **AshS** | Mod Ash 19 Aug 2020: submerged "1/3 chance of hitting each person it finds" in line of sight -- quoted on `wiki_Muttadile.wikitext` | Jagex |
| **AshP** | Mod Ash 30 Nov 2023: the stomp "max 50% higher than the melee attack ... acts as melee - crush, I think" -- `wiki_Muttadile.wikitext` | Jagex |
| **AKD** | *A Kingdom Divided* patch: CM submerged damage now 20-80% of its old max (`newsposts/wiki_Update_A_Kingdom_Divided.wikitext:588`) | Jagex (CM only) |
| **SS25** | Summer Sweep Up: Combat, 25 Jun 2025: 40% earth weakness (wiki Changes) | Jagex |
| **WM** | wiki *Muttadile* (rev 15322287, identical to the live page on 2026-10-18) | wiki |
| **WT** | wiki *Meat tree* (identical to live 2026-10-18) | wiki |
| **WS** | wiki *Chambers of Xeric/Strategies*, Muttadiles section (l.585-612) | wiki |
| **WCM** | wiki *Chambers of Xeric/Challenge Mode/Strategies* l.414-426 and *Challenge Mode* l.63-75 | wiki |
| **WN** | wiki *Noxious tendrils* (`wiki_Noxious_tendrils.wikitext`, new): loc 29767, op Pass, six tiles (3279/3280/3311/3312/3343/3344, 5313) | wiki |
| **CA** | *Mutta-diet* (elite, id 307): kill both without either healing from the tree (`wiki_Mutta_diet.wikitext`) | wiki |
| **RL** | RuneLite gameval (`runelite/gameval_muttadile.txt`, new): seq 7417-7426 `DOHGADYLE_*`, spotanim 157/393/1291, npc 7561-7564 | recorded ids |
| **de0** | de0 CoxTimers (`de0/CoxTimersPlugin.java:163-181`): loc 29767 = tendrils spawned, 30013 = tree cut | plugin |
| **Synq** | Synq solo guide, Muttadiles 0:42:50-0:54:44 (`synq_transcript.md:313-357`) | video |
| **PG** | ThePerfectG, *FULL Solo Muttadile Guide* (`transcripts/yt_s1cQDeyBH2Y.md`) | video |
| **Tub / Ed / Ext / Gno / Pl / Ac** | Tubby (`transcript_tubby_solo_cox.md:96-106`), TheEdB0ys (`transcript_edb0ys_cox.md:150-162`, 2022), Extile, Gnomonkey, Plank2g, The Academy | video |
| **RX** | RSProx recordings of Jagex's server: 3285/3282/843 solo, 3389 CM, 820 a four-man (`docs/rsprox-analysis/cox/muttadiles.md`) | **recorded -- outranks every row below where it speaks** |
| **NR** | Near-Reality `SmallMuttadile`, `LargeMuttadile`, `MuttadileRoom`, `MeatTree`, `NoxiousTendrils`, combat json 7561/7562 (copied to `nr/muttadile/`, new); spawn tiles `nr/room_coords.txt:328-357` | reference server |

## Rulings on conflicts (higher source wins)

| question | sources | ruling |
|---|---|---|
| Heal per meal | **RX: 3 bites of 15%, consecutive ticks** -- ruling below superseded; WM "eating up to 50% of their health (100% total)"; WS "restoring up to 40%"; NR 2 bites x 10% (3 under 40%); Ac/Ext/Pl "heal up to 50%" / "heal up all of the way" | **50% a meal, 25% a bite, 2 bites** (WM, the specific page, plus WCM "each time consisting of two bites"). WS's 40% is the pre-2023 trigger figure. |
| Feed trigger | J23 "at 50% HP or below (up from 40%)"; WCM "around 40-50%"; NR random 1/3 a tick under 50% | **at <= 50%, at once** (J23) |
| Prayer vs ranged / magic | WS "only reducing"; Synq "partial protection"; NR `getRanged/MagicPrayerMultiplier = 0.33` (ceil) | **33% gets through** (NR is the only number; was 50 with no source) |
| Prayer vs melee bite | WS "protection prayers block the melee attacks"; Synq "100% protection" | **fully blocked** (was 50% through) |
| Stomp | AshP 1.5x melee max, "acts as melee - crush"; WS "in place of a melee attack", "raising its front legs"; Synq "can hit [through] the protect from melee"; PG "random which melee she does", "one tick to react" | **its own attack (seq 7424 slam), 50/50 against the bite, every player in melee reach, max 72 (CM 106), crush accuracy, NOT reduced by Protect from Melee, lands swing+1**. Was a headcount rule (72 only with two or more in reach) |
| Small: melee vs ranged in reach | WM "if within melee distance ... it will prioritise Melee"; PG "in melee distance he has a lesser chance to switch to range"; NR melee 3/4 in reach | ~~3/4~~ **RX: 9/10** (40 bites, 3 spits in reach) |
| Large: melee in reach | WM "Melee up close"; NR 1/2 | ~~1/2~~ **RX: 9/10** (11 of 11 melee) |
| Style lock | J23 "only swap ... after using the same style 3 times in a row, not counting Melee"; WS "minimum of three ... not guaranteed to swap"; Synq "start off by praying range" | **runs of >= 3 thrown attacks; at the end a coin decides (odds unpublished); first run ranged** (was 4-attack runs, random first style) |
| Prayer read point | Synq "damage is registered on the tick that the attack animation started"; PG tick-eat its projectiles; NR reads prayer on landing | **at the swing** (videos outrank NR on observed behaviour) |
| Max hits | WM infobox post-J23: small 28 melee / 30 ranged; large 48 melee, 72 stomp, 45 ranged, 23 magic; CM 41/44, 71/106/67/34; NR 47/30/25 (pre-2023) | **WM** (unchanged; COX_MECHANICS.md S8's "large 41/44" was the CM *small* column, corrected here) |
| Submerged cadence | infobox attack speed 4; NR every 6 ticks then a 1/4 gate and a target count | ~~every 4~~ **RX: every 6 ticks**, each seen player 1/3 (AshS; RX 9/31 solo, 56/160 party) |
| Submerged reach | WM "covers the entire chamber", AshS "everyone in line-of-sight" | **the room, line of sight** (was 16 tiles) |
| Give-up | WM "~15 or so seconds ... they will give up"; WS "will give up and no longer attempt to eat from it"; WS "will only eat one time and will not do so again" | **25 ticks without reaching the tree ends the meal and all later meals** |
| Damage while feeding | J23 (before: "all your damage is nullified even though the muttadile is just standing there"; after: damageable "even if they are frozen while attempting to reach the tree"); Ed (2022) "invincible until they are done eating" | ~~nullified~~ **RX: damage lands** walking to and eating at the tree (the post-2023 game); `~cox_mutta_prepare_hit` removed |
| Attack range | NR class `setAttackDistance(6)` (json 8) | **6** (was 10, no source) |
| Tree chop damage | J23 "(Woodcutting level / 2) - 2, plus or minus 5"; AshT/WT "/2, -7/+3" | same thing (99 -> 42..52); unchanged |

## The tick spec

Tick mapping (manual Part 2): an npc decision on tick T reads player tiles at
the end of T-1; a prayer pressed after seeing d is in force for swings at d+1.
"+n" below is the tick the hitsplat lands relative to the swing tick T,
measured in the 2026-10-18 probes (ticklog `npc_anim` vs `hit_player`).

### Small Muttadile (npc 7562, `raids_dogodile_junior`, 3x3)

| mechanic | decide | reads | geometry | damage (solo) | prayer | tell (client) | source |
|---|---|---|---|---|---|---|---|
| target | after every attack, and when it has none | raiders in its room | **a random raider** in its room (RX), no LoS needed | -- | -- | facing_entity on its npc row | WM aggressive; Vanguards' engage pattern |
| attack cadence | engine attack clock, 4 ticks | -- | attack range 6 | -- | -- | -- | WM infobox; NR |
| bite | T | my tile end T-1 | edge-adjacent to the 3x3 (not diagonal, not under); 9/10 of swings in reach (RX) | crush roll (att 150, +71) vs my crush def; 0..28 (CM 41); **+0** | Protect from Melee: 0 | seq 7420 `dohgadyle_bite` on T | WM, NR, PG, Synq |
| spit | T | my tile end T-1 | anywhere in range 6 / not bitten | ranged roll (150, +83) vs my range def; 0..30 (CM 44); **+(61+5d)//30** (RX), d = Chebyshev from its middle | Protect from Missiles: ceil(33%), read at T | seq 7421 `dohgadyle_bark` + projectile 1291 `lizardman_spit` (25->36, delay 5, progress 115) + `creeper_spit` | WM, RX |
| meal | the hit's tick, hp <= 125 (50% of 250) | own hp | walks to the tree (touching its 2x2) | heals 15% a bite (37 at 250), 3 bites on consecutive ticks once there; <= 3 meals; still damageable (RX) | -- | stops attacking; walks; seq 7420 at the tree; npc_heal rows / health bar rises | J23, WM, WCM, NR |
| give up | 25 ticks unable to touch the tree | -- | -- | no more meals ever | -- | resumes attacking | WM, WS |
| death | hp 0 | -- | -- | drops overload (+)(4), prayer enhance (+)(4), 1-3 stinkhorn, big bones | -- | seq 7426 `dohgadyle_death`; then the large one surfaces | WM drops |
| stats | -- | -- | -- | hp 250 x party scale; att/str 150, def 138, mag 1, rng 150; att+71 str+48 rng+83 rstr+56; def stab -5 slash 72 crush 50 mag 60 rng 0; 40% earth weakness | -- | -- | WM infobox, SS25 |

### Large Muttadile, submerged (npc 7561, `raids_dogodile_submerged`, 5x5)

| mechanic | decide | reads | geometry | damage | prayer | tell | source |
|---|---|---|---|---|---|---|---|
| sweep | every 6th tick of its timer (RX), from spawn, while the small one lives (and through the emergence) | every raider at end T-1 | its room, line of sight from the water (`huntall` checkvis) | each seen player independently 1/3; magic roll (250, +0) vs my magic def; 0..23 (CM 34; CM's 20-80% floor not done); **+(56+5d)//30** | Protect from Magic: ceil(33%) at T | projectile 393 `dragon_ranged_fire_attack` from its middle (62->36, delay 61, progress 128) and `firewave_cast_and_fire` (delay 10, range 15); no graphic on me | AshS, WM, RX |
| unattackable | -- | -- | -- | -- | -- | no Attack op (cache) | WM |

Measured: 23 throws in about 83 sweeps with one raider in sight (seed 3) = 0.28.

### Large Muttadile, surfaced (npc 7563, `raids_dogodile`, 5x5)

| mechanic | decide | reads | geometry | damage | prayer | tell | source |
|---|---|---|---|---|---|---|---|
| surfacing | E = the small one's deletion (D+3) + 0..3 | -- | walks 5 tiles out of the water, a tile a tick from E (our tile choice) | -- | -- | seq 7423 `dohgadyle_emerge` at E; npc retypes 7561 -> 7563 at E+5; no message | RX |
| target / cadence | as the small one | | attack range 6 | | | | |
| melee choice | T, in reach | my tile end T-1 | edge-adjacent to the 5x5; 9/10 of swings in reach (RX); then bite or stomp 1/2 each | | | | NR, PG |
| bite | T | | the target | crush (250, +88); 0..48 (CM 71); **+0** | Protect from Melee: 0 | seq 7420 | WM, Synq |
| stomp | T | every raider's tile end T-1 | every raider edge-adjacent | crush roll each; 0..72 (CM 106); **+1** | **none** (goes through Protect from Melee) | seq 7424 `dohgadyle_slam` ("raising its front legs") | AshP, WS, Synq, PG |
| ranged | T, not melee | | range 6 | ranged (250, +82); 0..45 (CM 67); **+(56+5d)//30** | Missiles: ceil(33%) at T | seq 7421 + projectile 1291 (46->36, delay 5, progress 250) + `creeper_spit` | WM, RX |
| magic | T, not melee | | range 6 | magic (250, +0); 0..23 (CM 34); **+(56+5d)//30** | Magic: ceil(33%) at T | seq 7422 `dohgadyle_lazor` + projectile 393 (62->36, delay 61, progress 128) + `firewave_cast_and_fire` | WM, RX |
| style lock | each thrown attack | | | first 3 ranged; after any run of 3, a coin for the next run | | the seq id (7421 vs 7422) | J23, WS, Synq |
| meal / give up | as the small one, own counters (3 meals) | | | heals 15% a bite | | | J23, WM |
| death | | | | drops overload, prayer enhance, Xeric's aid, revitalisation (all (+)(4)), big bones; room cleared (`~cox_mutta_cleared`) | | seq 7426 | WM drops |
| stats | | | | hp 250 x scale; att/str 250, def 220, mag 250, rng 250; att+88 str+55 rng+82 rstr+47 mag+0; def stab -5 slash 82 crush 60 mag 75 rng 0; 40% earth | | | WM infobox |

### Meat tree (npc 7564 on loc 30012 "Rocks"; chopped -> loc 30013 "Meat tree")

| mechanic | value | source |
|---|---|---|
| hp | max(100, 5 x party's total Woodcutting): solo 99 -> 495 | AshT |
| chop | op 1 Cut on the npc, any axe (type irrelevant); a roll every 5 ticks; "You hack away some of the meat." per landed chop, `tree_creak_1` | AshT, RX |
| success | 20% at 1 .. 78% at 99 (stat_random 50/200) | AshT |
| damage | (WC / 2) - 2 +/- 5: 42..52 at 99 | J23 |
| xp | 45 a success | WT |
| not interrupted by being hit | -- | Synq 0:45:01 |
| solo at 99 | ~11 successes, ~13 rolls, ~52 ticks (probe: first hit t56, dead t108) | measured |
| death | on the killing chop's tick: "You hack away the final pieces of meat.", room register bit, loc 30012 -> 30013, the npc gone (no splat, no animation); any meal in progress ends next tick; no further meals | WT, de0, RX |
| tell | the tree npc's health bar and hitsplats; loc 30013 appearing | de0 |

### Noxious tendrils (loc 29767 `raids_dogodile_tendrils_entrance`)

| mechanic | value | source |
|---|---|---|
| where | two tiles across the entrance (template lx 15/16, lz 1-2 per variant, `nr/room_coords.txt`) | WN, NR |
| when | spawned when a raider comes within 6 tiles of the entrance; the Muttadiles spawn at the same moment | NR, WCM ("Standing at the southern edge of Vasa's chamber will trigger the muttadiles to spawn") |
| op | Pass: a 2-tile hop through (Agility-free); the first pass is free, every later pass costs 22.5% of current hp; 15-tick lockout | WN, WS ("deal damage to players who try to leave"), NR |
| after the room | removed 2 ticks after the large one dies ("The tendril is about to collapse!") | NR |

**Implemented (round 2)**: two tendril locs at spawn; op Pass hops two tiles
straight through (`~forcemove`); going back out costs 22.5% of current hp
(NR's amount; the wiki says only that leaving hurts); removed 2 ticks after
the large one dies. Spawn timing differs from NR (laid at raid build, not on
approach): the room is entered through them either way.

## Audit (before this round -> after)

| mechanic | ours (before) | OSRS (spec) | verdict / now |
|---|---|---|---|
| any animation | none | bite/bark/lazor/slam/emerge | FIXED: every attack, meal bite and the surfacing animate |
| projectiles / graphics | none; damage `queue*(...,0)` on the swing | spit 1291, magic 393, impact 157, landing by flight | FIXED |
| accuracy | none: uniform 0..max every swing | npc attack roll vs player defence | FIXED (`~cox_mutta_roll`) |
| npc bonuses | absent (all 0) | infobox bonuses | FIXED (cox.npc) |
| earth weakness | absent | 40% (SS25) | FIXED |
| melee reach | `distance(sw tile, me) <= 1` | edge-adjacent to the 3x3/5x5 | FIXED (`~cox_mutta_in_melee`) |
| prayer vs melee | 50% through | 0 through | FIXED |
| prayer vs ranged/magic | 50% through (no source), read at swing | 33% (NR), at swing | FIXED |
| small melee odds | always melee in reach | 3/4 | FIXED |
| large melee | always melee in reach; stomp max only if 2+ in reach; stomp hit everyone through prayer-halving | 1/2 melee; bite or stomp; stomp through prayer, +1 | FIXED |
| style lock | runs of 4, random first style | runs of 3, ranged first | FIXED |
| submerged reach | 16 tiles, any room | its room, line of sight | FIXED |
| submerged accuracy / prayer | 0..23 flat, prayer halves | magic roll, 33% | FIXED |
| aggression | none: retaliate only; the large one never took a target unless hit | aggressive, targets a raider in its room | FIXED (`~cox_mutta_engage` every tick) |
| meal arrival | exact south-west tile ON the (blocking) tree: never reached, every meal ended by giving up | touching the tree | FIXED (`~cox_mutta_at_tree`); probe: 3 meals of 2 x 62 |
| meal bites | 2 bites on consecutive ticks, no anim | 4 ticks apart, seq 7420 | FIXED |
| meal keeps fighting | combat mode never dropped; a hit re-engaged it | stops fighting, ignores hits | FIXED (`npc_setmode(none)`, `[ai_queue1]` overrides) |
| give up | ended the meal only | no further meals | FIXED |
| small's meal on tree death | not ended | ended | FIXED (register bit read each tick) |
| tree locs | no loc; nothing on death | 30012 under, 30013 when chopped | FIXED |
| drops | none (`death_drop=null`) | small 4 items, large 5 | FIXED |
| room cleared | nothing | Guardians' recipe | ADDED `~cox_mutta_cleared($at)` |
| exit crystal | none | NR loc 30018 per variant, shattered on clear (anim 7506, sounds 1021/3821, gone +2) | FIXED round 2 (`~cox_mutta_room_done`); the Houndmaster's book (loc 30031) at her death |
| surfacing walk | `npc_walk` (collision on): she never left the water, 0 swings all fight; CW walked east | NR walks 5 tiles with collision off: WEST in CCW/THRU, SOUTH in CW | FIXED round 2 (a tile a tick, `npc_tele`; CW south) |
| chopping | `p_delay` between rolls: an eat/drink while chopping was dropped | OSRS woodcutting: an inventory op ends the chopping | FIXED round 2 (the woodcut action-clock cadence, re-op each tick) |
| damage while feeding | always damageable | RX: damageable | REVERTED 2026-10-18 (the nullifying was the pre-2023 game) |
| tendrils | absent | spec above | FIXED round 2 (`[oploc1,raids_dogodile_tendrils_entrance]`) |
| erratic movement | FIXED 2026-10-18 (RX: 1 attack in 7 owes one random step at +3) -- was: none (engine chase only) | "notorious for constantly walking towards and away"; NR: after a thrown attack 1/3 chance to close to 1-3 tiles, and a no-LoS re-path | OPEN (see below) |
| bronze axe spawn | absent | a bronze axe ground spawn in the room since 26 Jun 2024 (WM; Jagex patch notes "Added a handy Bronze Axe spawn to the muttadile room") | FIXED round 3: spawned beside the tendrils' landing tile (template (16, tendril row + 1); the tile is OURS, no source gives it) |
| CM submerged 20-80% | absent | AKD | OPEN (CM only) |
| surfacing timing | retype at D+3 | RX: emerge D+3..6, retype emerge+5 | FIXED 2026-10-18 |

## Probes (2026-10-18, private build `build_cox_mutta`, scriptrun seed 3: `::coxseed 3` + `::coxgoto 12 0`)

Probe scripts: `$SCRATCH/mutta_probe*.lua` (stand / chop / kill small / melee the large).
Every run: 0 `abort`, 0 `no primary player bound` in stdout.

- Small spit: anim t, splat t+2 (d 5-6); bite: anim and splat the same tick;
  spits under Protect from Missiles 2..10, off it up to 25.
- Submerged: projectile 393 + spotanim 157 together, splat +2; 0.28 of sweeps.
- Tree: 11 successes 42..52, 4-tick rolls, dead t108, loc 30013 at t111.
- Meals (tree up): heals of 62 at t120/124, t249/253, t445/449 -- three meals, then none.
- Surfacing: small dies t348, drops t350, emerge anim t350, retype t353, first attack t360.
- Large: stomps (7424) land +1 through Protect from Melee (22, 52); bites under it 0;
  thrown runs R R R | R R R | M M M | M M ...; magic splat +3.

## Still unsettled

1. (settled by RX 2026-10-18) **Erratic movement**: one step to a random
   neighbour, 3 ticks after 1 attack in 7.
2. (settled by RX) **Feeding immunity**: there is none.
3. **Style-coin odds** after a run of three (we use 1/2; Tub: "usually after
   three mage attacks, he will switch back to ranged" suggests magic -> ranged
   is likelier). RX runs R4 M4 R3 R3+ R2 / R7 R3 R2 do not settle it.
4. **Prayer fraction** 33% is NR's; RX under-prayer maxima (7 of 23, 7 of 30)
   fit it.
5. (fixed round 2) **Selftests that pinned the old behaviour** (`cox_selftest.rs2`, not this
   unit's): #106 `cox_selftest_mutta_style_lock_melee_exempt` asserts the lock
   reads 3 after the reroll from 1 (the old 4-attack run); under the fix it reads
   2 after one more call. #102 still passes (kinds 1 and 2 give 48/72).
6. The bronze axe's exact tile (ours); CM submerged floor; tendril spawn on approach (NR) vs at build (ours).
