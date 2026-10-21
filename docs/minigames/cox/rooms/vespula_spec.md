# Vespula — tick spec, rulings and content audit (2026-10-18)

Unit `vespula` (COX_FINISH_PLAN.md R). Content: `OSRS-Content/osrs-content/
server/scripts/minigames/minigame_cox/scripts/cox_vespula.rs2`, Vespula blocks in
`configs/cox.constant` / `cox.npc`. Solver plan: `vespula_solver.md`.

**RECORDED DATA NOW OUTRANKS sections 3-7.** Three RSProx recordings of Jagex's
server (820, 843, 3389; `docs/rsprox-analysis/cox/vespula.md`) were read after
this spec was written. Where a row below disagrees with them, section 8 has the
recorded value and the content follows section 8.

## 0. Sources (rank order, as the plan rules: Jagex > recorded > wiki > plugins > NR > ours)

| tag | source | file |
|---|---|---|
| **J19** | Jagex, *CoX, Kebos and Collection Log Changes*, 31 Jan 2019: "she does not start until a player enters her chamber"; portal regenerates less quickly | `sources/wiki_Vespula.wikitext` (Changes) |
| **J23** | Jagex, *Chambers of Xeric Changes*, 29 Nov 2023: grubs 100→**125** hp, blossom heals **35** (was 20), Vespula/soldier ranged & melee strength cut, soldier portal-healing reduced, guaranteed Overload(+) | `sources/newsposts/wiki_Update_Chambers_of_Xeric_Changes.wikitext:48-53,257-276` |
| **J20** | Jagex, Ferox Enclave / Vanguard Improvements: no Vespula melee after the portal dies | `newsposts/wiki_Update_Ferox_Enclave.wikitext:85` |
| **JA** | Mod Ash, Discord 9 Jul 2026: regen Vespula 10, portal 45, grubs 10000 (never), soldier flying 16, walking 10000 | wiki pages (CiteDiscord) |
| **C** | the osrs239 cache: npc 7530-7539 records (sizes, ops, grub `stat4=125`, readyanims), seq/spotanim/loc ids | `configs/all.{npc,seq,spotanim,loc}` (+ `.compack` id maps) |
| **M** | the template map squares m51_82 / m52_82 plane 2 (roots, pillars, abyss floor, inviswalls) | `maps/m51_82.jl2`, `m52_82.jl2` |
| **SQ** | Synq's CoX guide, Vespula chapter 1:25:34-1:38:14 | `synq_transcript.md:499-543` |
| **FL** | Fluffeh, *Vespula (No Enhance/Almost no Supplies)* 2022 | `sources/transcripts/yt_3HMsjFlJ7aA.md` |
| **AC** | The Academy, *VESPULA in 90 seconds* (2023-11-29) | `sources/transcripts/yt_ZALgpHA9mvw.md` |
| **G2** | two more guides (yt_1aFJyKKdjn8, yt_0a61znGm0pc, yt_38cqq0xFEYM) | `sources/transcripts/` |
| **W** | wiki: Vespula, Abyssal portal, Lux grub, Vespine soldier, Medivaemia blossom/root, Septic tendrils, Redemption, CoX/Strategies, CM Strategies, Talk:Vespula | `sources/wiki_*.wikitext` |
| **CA** | Redemption Enthusiast (Elite): "Kill the Abyssal Portal without forcing Vespula to land" | `sources/wiki_combat_achievements_cox.tsv:22`, `wiki_Redemption_Enthusiast.wikitext` |
| **NR** | Near-Reality `Vespula`, `AbyssalPortal`, `LuxGrub`, `VespineSoldier`, `VespulaRoom`, `SepticTendrils`, `MedivaemiaRootObject`, `LuxGrubNPC` | `sources/nr/vespula/*.java` |

No RuneLite / plugin-hub source carries a Vespula constant (searched `sources/runelite`,
`openosrs_coxhelper`, `cox_qol`, `de0`, `cox_analytics`); Synq names the "Chambers of
Xeric additions" prayer-enhance regen info box, which reads the player's prayer
regen, not Vespula. No Blert-like recording exists for CoX: there is **no recorded
tick data** for this room, which is why several rows below are calibrations (§4).

## 1. Ids (cache names; NR's numbers checked against them)

| what | id → name |
|---|---|
| npcs | 7530 `raids_vespula_flying` (5×5, op2 Attack), 7531 `_enraged` (readyanim = the sting loop), 7532 `_walking`; 7533 `raids_vespula_portal` (4×4, "Abyssal portal"); 7534-7537 `raids_vespula_caterpillar_{healthy,sickly,infected,dead}` (3×3, `stat4=125`, op1 **Feed** on sickly and infected only); 7538/7539 `raids_vespula_vespine_{flying,walking}` (3×3) |
| Vespula seqs | 7453 `vespula_flying`, 7454 `vespula_attack_melee_flying` (the grub sting; NR stingingAnimation), 7455 `vespula_attack_ranged_flying`, 7450 `vespula_attack_ranged` (grounded), 7457 `vespula_landing`, 7452 `vespula_takeoff`, 7459 `vespula_death_flying`, 7458 `vespula_death_walking` |
| grub / soldier seqs | 7466 `luxgrub_death`, 7468 `luxgrub_dead`, 7460 `vespine_hatch`, 7461 `vespine_walk`, 7462 `vespine_explode`; 7467 `luxgrub_vespine_hatch_spotanim` is the **spotanim's** seq (NR played it on the grub: wrong) |
| spotanims | 1364 `raids_vespula_poison` (her ranged projectile), 1366 `raids_vespula_portal_attack` (the drain), 1365 `raids_vespula_vespine_hatch`, 1367 `raids_vespula_vespine_healportal` (soldier → portal heal; NR never sends it), 1368 `raids_vespula_vespine_explode` |
| locs | 30072 `raids_vespula_portal` "Slimy pillars" 4×4 (seq 7499 active), 30073 `_portal_closed`, seq 7500 `raids_vespular_portal_closing`; 30024 `raids_vespula_tendrils_entrance` "Septic tendrils" op1 **Pass**; 30070 `raids_vespula_boil_blocking` / 30071 `_boil_burst` (blockwalk 0); 30068 `raids_vespula_herb` "Medivaemia root" op1 **Pick** / 30069 `_herb_empty` (2×2) |
| obj | 20892 `raids_vespula_herb` (Medivaemia blossom, not stackable) |

## 2. Geometry (template room-local, unturned; the room's door is the south edge)

NR's tables name multi-tile npcs by their **centre** and spawn at `transform(-2,-2)`
(Vespula) / `(-1,-1)` (grubs, soldiers). Below are south-west tiles.

| | ccw (m51_82 cell 0,1) | thru (m51_82 cell 1,1) | cw (m52_82 cell 0,1) |
|---|---|---|---|
| Vespula SW / enraged hover | (15,8) → x15-19 z8-12 | (10,10) → x10-14 z10-14 | (11,8) → x11-15 z8-12 |
| portal npc SW (4×4) | (17,20) | (22,11) | (11,20) |
| pillar loc | (17,21) a0 | (23,11) a1 | (11,21) a0 |
| portal hit tiles (the "cove") | (17,13) (18,13) | (15,13) (15,12) | (12,13) (13,13) |
| portal projectile start | (18,22) | (24,12) | (13,22) |
| grubs SW (3×3) | (22,8) (20,13) (12,5) (13,13) | (15,8) (7,10) (15,16) (7,16) | (15,13) (8,5) (8,13) (5,9) |
| soldier walk-to SW | (19,8) (20,10) (12,8) (13,10) | (12,8) (10,10) (12,16) (10,16) | (15,10) (8,8) (8,10) (8,9) |
| roots (2×2) | (2,4) (2,7) (2,21) (3,23) (4,2) (5,23) | (2,26) (4,28) (7,27) (12,26) (18,24) (19,26) | (24,15) (24,17) (25,3) (25,19) (27,9) (28,6) |
| tendrils (door) | (16,1) only | (15,1) (16,1), unrecorded (cw's) | (15,1) (16,1) |
| boils (exit) | (3,15) (3,16) west | (15,28) (16,28) north | (28,16) (28,15) east |

Consistency check that fixed the old tables: the hit tiles are the row/column
**edge-adjacent** to her hover, and the portal is exactly **7** tiles from them
(ccw: portal z20 − hit z13 = 7), matching the wiki ("The portal is seven tiles away
from the accessible area ... a weapon with an 8-tile range is highly recommended, as
otherwise players will be hit by Vespula's melee if she is enraged"): an 8-range
attacker stands one row further in, **under** her (stomp 2-8), a 7-range attacker
in the cove at her edge (sting 5-20, portal drain). With the old centre-as-SW tables
her footprint covered the cove.

ccw walk map from the template (`#` blocked terrain, `o` blocking loc incl. the
abyss floor which `blockrange=0`, `|` inviswall, `V` Vespula hover, `G` grubs, `x`
hit tiles, `h` roots, `T` tendrils, `B` boils; z up):

```
 20 ####...##########oo#############      portal npc x17-20 z20-23 (over the abyss)
 16 ###B....#########oo#############
 15 ###B....#####GGG#oo#GGG#########
 14 ####....#####GGG#||#GGG#########
 13 ####....###o#GGG#xx#GGG#########      the cove (7 from the portal)
 12 ####...........VVVVV..##########      8-range row = her top row
 10 ####...........VVVVV..GGG#######
  8 ####...........VVVVV..GGG#######
  7 ####...#####GGG#o....o##########
  2 ################......##########
  0 ###############TT###############
     0123456789012345678901234567890 1
```
(full renders for all three variants: `python3 -I /private/tmp/claude-501/-Users-matthewevers-Documents-git-repos-3draster/161e2e26-be26-45f2-96e7-4b10a354aafa/scratchpad/tools/vesp_map.py
ccw|thru|cw` -- a scratch tool reading `maps/*.jl2|jm2` + `configs/all.loc`; walls are
approximate, the engine's collision is the authority, probe P5.) The
safe tiles (§3 V14) on this map: x21 z8-12 and x13 z8-12 (two tiles off her
east/west edge, on her rows), z6 x15-19 (two below). "one tile after the lux
grub opposite of vespula" (AC) and "two tiles away from Vespula (halberd range)
... on that row or column" (W Strategies) both describe these.

## 3. The tick spec

Engine order: npcs act before players each tick; a hazard decided by an npc
timer on tick T reads raider tiles at the **end of T−1**. Player queues of delay 0
armed by an npc run in the same tick's player phase.

| # | mechanic | decide tick / cadence | positions read | geometry | damage (solo) | prayer | tell (client) | source |
|---|---|---|---|---|---|---|---|---|
| V1 | Wake | first tick a raider is in the room cell; then 2 inert ticks | end T−1 | room cell | – | – | roots flower (loc 30069→30068) | J19, NR initialDelay, NR load() |
| V2 | Stats | – | – | – | Vespula 200 hp, def 88, mage 88, rng 150 (magic def 70 flying / 190 enraged / 60 grounded, C); portal 250 hp def 176; soldier 100 hp; grub 125 | – | – | W infobox, C, J23 |
| V3 | Her ranged attack | every 3 ticks, only when not enraged and no grub to sting (or grounded) | target picked at release (end T−1) | nearest raider in the room not under / beside her footprint | max **14**, accuracy roll (rng 150+9)×64 vs ranged def; poison severity 96 (20) on 1 in 3 landed hits | Protect from Missiles, read **at landing**, halves (NR 0.5) | anim 7455 (flying) / 7450 (grounded), projectile 1364 from her centre, flight 40+5/tile cycles | W (max, speed, poison 20), SQ 1:29:03, FL (prot. missiles), NR attack() |
| V4 | (her NR splash) | – | – | NR also fires a 2nd projectile to a random tile ≤2 from the target, hitting that tile + 4 neighbours | – | – | – | NR only; **not adopted** (no other source; R4) |
| V5 | Landing | on every hit landing on her while flying/enraged | – | – | she can never drop below 1 hp | – | anim 7457, type 7530/7531→7532 | W ("chance to land each time she is attacked ... guaranteed ... 1 hitpoint"), Talk:Vespula, NR sendDeath; chance = **calibration 1 in 2** (P2) |
| V6 | Grounded | 33 ticks, then takeoff, full heal, flying | – | 5×5 walking form | – | – | anim 7452, type →7530 | W "~20 seconds", "restore her health", Talk:Vespula; NR 17 ticks rejected (R2) |
| V7 | Sting a grub | when not enraged, not grounded, lockout 0: pick the farthest healthy non-draining grub, walk to centre over it, sting on arrival; 4-tick lockout | – | her SW = grub SW −(1,1) | – | – | anim 7454 facing the grub; grub type 7534→7535 within a tick | NR getFarthestUnstingedGrub / stingDelay, SQ 1:26:40, W Lux grub |
| V8 | Grub drain | from the sting: −1 hp every tick until fed to full or 0 | – | – | 125 → 0 in **125 ticks** | – | grub health bar; type 7535 (<100%) → 7536 (≤50%) | J23 (125 hp, "more time to heal them"), W Lux grub ("a meter ... slowly depletes"), SQ ("3-4 [blossoms] per grub that was tagged"; "decent gear ... always defeat the portal before the soldier spawns"); rate = **calibration** (P1) |
| V9 | Feed | player op1 Feed / blossom on grub | – | grub 3×3 | +35 hp, clamp; full → stops draining, healthy | – | grub hp bar, type → 7534 | J23, cache op, SQ 1:27:47, NR LuxGrubNPC |
| V10 | Hatch | grub at 0 | – | soldier rises on its paired tile beside the grub (NR rises it on the grub and walks it out; here the dead grub's 3×3 blocks that walk, A23) | – | – | grub anim 7466 → type 7537; spotanim 1365; soldier anim 7460 | W, NR sendDeath/rise |
| V11 | Soldier | fights 5 ticks after the rise; heals portal & Vespula every 8 ticks while alive; at ≤20% drops (melee-only); explodes 33 ticks after dropping | heal: – ; explode: tiles ≤3 from its centre at that tick | 3×3 | crush melee max 14, speed 4, poison 15 (sev 71); heal 17 each per 8 ticks (calibration, P4); explosion 15-30 | Protect from Melee (engine generic: full) | 7538→7539; heal projectile 1367; explode anim 7462 + spotanim 1368 | W Vespine soldier, W Strategies ("explode after 20 seconds"), J23 ("reduced" healing), NR |
| V12 | Enrage | each flying tick: portal hit within the last **24** ticks → fly home; on arrival take the enraged form; leave it when 24 ticks pass without a hit | – | home = her spawn SW | – | – | type 7531 (readyanim = sting loop) | W ("until the portal is no longer attacked"), SQ 1:28:21 ("approximately 24 ticks"), FL; NR's 9 rejected for the enrage (R3) |
| V13 | Stomp | every enraged tick | end T−1 | raiders on her 25 tiles | 2-8 typeless | none | (position only) | W, AC ("every second we are underneath vespula we will take ... eight damage"), SQ ("only damage you up to eight") |
| V14 | Sting (enraged) | every 2 enraged ticks | end T−1 | **reach** = her edge ring minus its 4 diagonal corners; one raider in reach drawn, everyone in reach within 1 of them hit | 5-20 typeless | none | facing | W ("stinging anyone in melee range for 5-20 typeless"; "stand on that row or column ... any other tile ... jabbed"), NR stingDelay 2; **no poison** (SQ 1:29:03 outranks NR's 1/3) |
| V15 | Safe tiles | – | – | tiles ≥2 off her footprint (§2) | 0 | – | – | W Strategies, AC, G2 |
| V16 | Portal attackable | any time; range 7 from the cove, 8 from under her | – | – | – | – | – | W Abyssal portal |
| V17 | Portal regen | 1 per 45 ticks | – | – | – | – | – | JA |
| V18 | Portal drain | every 2 ticks while the portal was hit within the last 9 ticks | release: end T−1 on a hit tile; landing: the raider must **still** be on that tile 1 tick later | the 2 hit tiles | 3 prayer points; with 0 prayer, 3 typeless damage | – | projectile 1366 from the pillar to the tile | W Abyssal portal ("three points every two ticks ... three damage instead ... faint green projectile ... Redemption method players should not be hit"), NR hit tiles + landing check; NR's 1-2 damage rejected (R5) |
| V19 | Redemption | on any hit taken at ≤10% hp | – | – | heals ⌊prayer/4⌋ (24 at 99), prayer → 0 | – | spotanim | W Redemption (engine `~prayer_redemption_check`) |
| V20 | "Portal attacked" | stamp = tick of the last hit (0 splats count) | – | – | – | – | hitsplat on the portal | NR isUnderAttack; this tree: `[ai_queue2,raids_vespula_portal]` |
| V21 | Roots | Pick: one blossom; the root empties and flowers again 8 ticks later | – | 2×2 roots | – | – | loc 30068 ↔ 30069, anim 832 | W Medivaemia root, NR MedivaemiaRootObject |
| V22 | Tendrils | room load; Pass = 2-tile hop; leaving while the portal lives costs 15% max hp | – | z 1, per layout (RSProx: ccw one, cw two; rotations fixed, not turned) | 15 at 99 hp | – | loc 30024 | W Septic tendrils ("block players from entering ... passed through to start the encounter"), W Strategies ("damaging players if they attempt to leave"), NR SepticTendrils |
| V23 | Clear | portal at 0 | – | – | – | – | pillar seq 7500 → loc 30073, boils burst, tendrils gone, roots empty, grubs/soldiers removed, Vespula death anim; message "As the link to the abyss disintegrates, the abyssal creatures perish." | NR clear(), J20 (no melee after) |
| V24 | Drop | at clear, at Vespula | – | – | 2 Xeric's aid (+), 1 revitalisation (+), 1 prayer enhance (+), 1 overload (+) | – | ground items | W drops, J23 |
| V25 | Regen | Vespula 1/10 ticks, soldier (flying) 1/16, grubs never | – | – | – | – | – | JA |
| V26 | Style gates | at the attack click | – | – | flying: ranged/magic only (halberds from Aug 2025 **not** in rev 239, as godwars_bosses.rs2 rules); grounded: melee only | – | "too high ... melee" message | W, NR processCombat/applyHit |
| V27 | CA hook | – | – | – | Redemption Enthusiast: portal dead without a landing | – | – | CA (root's points/CA layer) |

## 4. Rulings (conflicts)

- **R1 grub hp / blossom**: 125 / 35 (J23 + cache `stat4=125`) over NR 30 / 20%; W
  Strategies' "heal them for 30" loses to J23.
- **R2 grounded time**: ~20 s = 33 ticks (W) over NR 17.
- **R3 enrage length**: 24 ticks after the last portal hit (SQ, FL) over NR 9; NR's 9
  is kept for the portal's own "under attack" drain gate (V18) where W is silent.
- **R4 her ranged splash** (NR only): not adopted.
- **R5 drain with no prayer**: 3 (W) over NR 1-2.
- **R6 landing**: W "Vespula" (chance at ≤20%, guaranteed at 1 hp) over W "Abyssal
  portal" ("23%") and NR (always at ≤23%). Chance per hit unpublished → P2.
- **R7 sting poison**: none (SQ: "The only time Vespula is able to poison the player
  is when it is directly auto attacking you") over NR's 1/3 on the enraged sting.
- **R8 sting removes no hp**: W Lux grub describes a draining meter, not a halving; NR's
  snap-to-50% / only-above-50% eligibility is NR's representation of a 30-hp grub.
  Eligible = healthy and not draining (a fed-to-full grub can be stung again, SQ).
- **R9 tendrils present from load** (W Septic tendrils, W Strategies) over NR (spawned at
  the first portal hit). Entering is free.
- **R10 soldier heal**: periodic while alive (W, SQ "the portal will start to heal")
  over W CM Strategies' "fully healing both" (older) and NR's full heal at the rise.
- **R11 dead grub returns** 16 ticks after its soldier is gone (W "shortly after it
  dies") over NR (16 ticks after hatching, soldier or not).
- **R12 halberd**: not applied (post-rev-239), as godwars_bosses.rs2 rules aviansies.

## 5. Audit (ours before this round → OSRS → verdict / fix)

| # | mechanic | ours (before) | OSRS | verdict |
|---|---|---|---|---|
| A1 | wake | populated at raid entry, stinging at once | waits for a raider in the room (J19) | **fixed**: dormant until a raider stands in the cell (V1) |
| A2 | tiles | NR centres used as SW tiles (Vespula +2,+2, grubs/soldiers +1,+1): her footprint sat on the hit tiles | §2 | **fixed** |
| A3 | grub hp / drain / feed | 30 hp, sting snaps to 50%, −1 per 16 ticks, feed 20%, no way to feed | 125, drain from sting, +35 a blossom | **fixed** (V8, V9); feed op1 + blossom-on-grub added |
| A4 | roots | no Pick handler; roots never flowered | Pick → blossom, regrow 8 | **fixed** (V21) |
| A5 | her ranged attack | none (retaliate=yes engine swing with stat band, no poison, no projectile) | V3 | **fixed**: scripted 3-tick ranged, projectile 1364, poison, prot. missiles at landing; engine swing disabled |
| A6 | landing | at ≤20% on her own timer poll; she could die at 0 hp | chance per hit, guaranteed at 1 hp, never dies | **fixed** (`[ai_queue2]` hook, hp floor 1) |
| A7 | grounded length / takeoff | 33 ticks, no heal on takeoff, no landing/takeoff anims | 33, full heal, anims | **fixed** |
| A8 | enrage trigger | hp-drop poll of the portal every 2 ticks (missed 0-damage hits) and a counter that ran 2 ticks per step (window 18 ticks) | every hit, 24 ticks | **fixed** (`[ai_queue2,raids_vespula_portal]` stamp, V12) |
| A9 | enrage move | changed form in place, wherever she was | flies to the portal front first | **fixed** |
| A10 | stomp / sting zones | anyone within 1 tile of her SW tile (not the 5×5) for both | under the 5×5 / edge ring w/o corners | **fixed** (V13, V14) |
| A11 | enraged sting poison | none (comment: "not ported") | none (R7) | correct by accident; now ruled |
| A12 | portal drain | tile-exact, but at release, damage 3 via `combat_damage_player` (magic-style gear reduction) | lands a tick later on the tile; 3 typeless | **fixed** (V18) |
| A13 | tendrils | spawned at first portal hit, no Pass handler (a raider could never leave) | at load, Pass hop, leave costs | **fixed** (V22) |
| A14 | boils | never spawned (clear tried to burst nothing) | spawned at load, burst at clear | **fixed** |
| A15 | soldiers | never fought (no hunt), no heal at all, explode at 17 ticks, poison none | fight, heal, 33 ticks, poison 15 | **fixed** (V11) |
| A16 | grub reset | 16 ticks after hatch | after its soldier is gone | **fixed** (R11) |
| A17 | clear | `%varp`/`mes` in the death hook, pillar looked for on the portal npc's tile (wrong tile), grubs/soldiers/Vespula left alive | V23 | **fixed** (room voice via `~cox_mes_at`, pillar loc tile per variant, everything removed, her death anim) |
| A18 | NR seq misuse | – | 7467 is a spotanim seq | used as spotanim 1365 |
| A19 | invented messages | "Vespula crashes to the ground.", "Vespula shrieks and dives." | no source | **removed** |
| A20 | drop | 2/1/1/1 at the portal | same items, at Vespula (NR drop) | moved |
| A21 | style gate | correct | – | kept |
| A22 | regen | Vespula 10, portal 45, soldier 16 | JA | kept |
| A23 | soldier rise | (new) risen on the grub's tile: the dead grub's 3×3 blocked every step; it never moved or fought | rises and fights | rises on its paired tile |
| A24 | soldier swings | refused: the engine treats the raid as single-way, and the raider's claim on the portal kept every soldier off him | CoX is multi-combat | `forcemulti=yes` on both soldier records (the DKs' precedent). **Every CoX npc that fights alongside another needs the same** (root / other units) |
| A25 | portal death | a heal (soldier pulse) landing in the engine's 3-tick death window reads as a scripted revive and cancels the death | the portal dies | soldiers never heal a portal at 0 |
| A26 | Vespula wandering | no `defaultmode` on her records: she wandered ±3 tiles while enraged, so the safe tiles were not safe | a fixed hover (the safe-tile method depends on it) | `defaultmode=none` on her three forms and the portal |
| A28 | **the portal's approach (ENGINE, fixed round 3)** | the server's collision read each loc's blockwalk AFTER rscache collapsed it for `breakroutefinding` (opcode 74): `raids_floor_abyss_*` (blockwalk=1) and `inviswall_serverside` carry it, so a raider walked the abyss to the portal (seed vespula0: walk 6444,109 -> 6444,113 reached in 2 ticks; `::coxblocked` showed the corridor open) | the client's route finder ignores such locs, the server enforces them ("The portal is seven tiles away from the accessible area", wiki) | rscache keeps `declared_blocks_walk/_projectiles`; `torirs_server_scene.c` `apply_loc_collision` stamps the declared pair. Client and scriptrun routes unchanged (they walk through, as OSRS's do) |
| A27 | her walk to alcove grubs | (new) a 5×5 cannot reach some alcove grubs (cw grub 3 at (5,9)): she oscillated between two tiles forever and never stung or shot | stings every grub | sting from where she stands after 8 ticks of walking (P3) |

Selftest rows that pin the superseded NR rules (`cox_selftest.rs2`, not this unit's
file): `cox_selftest_vespula_grub_hp_constant` (wants 30) and
`cox_selftest_vespula_sting_sets_max_half` (wants the 50% snap) now fail by design;
`_sting_boundary` still passes against the kept pure helper but tests a rule no
longer used. Their owner should delete those rows (CLAUDE.md: don't pin superseded
behaviour). The procs and constants they read are kept so the tree compiles.

## 6. Unsettled (each is a probe or a recording to find)

| P | question | current value | how to settle |
|---|---|---|---|
| P1 | grub drain rate after a sting | 1 hp/tick (125 ticks to hatch) | frame-step a video of a stung grub's bar (any solo redemption VOD with the HP bar visible); SQ's "3-4 blossoms per tagged grub" and "always kill the portal before a hatch" bound it to roughly 100-200 ticks |
| P2 | landing chance per hit at ≤20% | 1 in 2 | Talk:Vespula feedback only says "probability"; count hits-below-20% before landing in a "points method" VOD (SQ 1:36:20, FL) |
| P3 | can her 5×5 reach every grub (alcoves) | no (cw grub 3): she stings from where she stands after 8 ticks of walking (A27) | a VOD of her stinging an alcove grub: how close does she get |
| P4 | soldier heal amount after J23 | 17 per 8 ticks each (NR) | a VOD with soldiers alive and the portal HUD |
| P5 | the cove/under-her attack tiles vs the engine's range/LOS to the portal | per §2 | scriptrun: attack the portal from each candidate tile; record whether the swing fires |
| P6 | whether the enraged sting reach includes the diagonal corners | excluded | W "row or column" wording; a VOD standing on a corner |

## 7. Probe results (scriptrun, 2026-10-18, `::coxtest 2 0` = cw variant, turn 0)

Probe script: `/private/tmp/claude-501/-Users-matthewevers-Documents-git-repos-3draster/161e2e26-be26-45f2-96e7-4b10a354aafa/scratchpad/probe/vesp_probe.lua`
(runs vp1..vp9 beside it). No script aborts and no "no primary player bound" in any run.

| check | seen |
|---|---|
| wake | dormant until the raider passed the tendrils (t20); roots flowered t21 (6 × loc 30068) |
| tendrils | Pass in: 95→97, free; Pass out: 97→95 and a 14 hit (15% of 99) |
| stings | t26, 31, 36, 41 (4 grubs, 5 ticks apart = 4 lockout + 1); 7534→7535 the same tick as her seq 7454 |
| drain | sickly→infected (≤62 hp) 61 ticks after the sting; hatch 124 ticks after the sting (V8) |
| her ranged | every 3 ticks once all grubs were tagged; hits 0-14; poison 20 splats |
| enrage | portal hit → 7531 at her home; stings at the cove (13,13) 5-20 every 2 ticks; back to 7530 exactly 24 ticks after the last portal hit; the safe tile (17,10) took nothing while she was enraged |
| portal drain | standing on hit tile (12,13): prayer 99→93 in 6 ticks |
| landing | at ≤20% she landed within 1-3 hits; held at 1 hp; grounded 33 ticks (121→154), then 7530 |
| soldiers | hatch anim 7460 + grub 7466; heal projectile 1367 every 8 ticks; swung at the raider (12 hits from 7538, 2 from 7539) after A24; dropped to 7539 at ≤20%; exploded (seq 7462) 33 ticks after dropping; the dead grub came back 16 ticks after its soldier was gone |
| clear | portal 0 → boils burst, pillar → 30073, tendrils gone, roots emptied, every grub/soldier removed, Vespula's death anim then gone, the 4-item drop on the floor |
| client view | the grub meter, Vespula's takeoff heal and the soldier heals never reach the client's health bars (E1, `vespula_solver.md` §7): `npc_statheal` sends no bar |

## 8. Round 4 (2026-10-18): the recordings

From `docs/rsprox-analysis/cox/vespula.md` (RSProx 820, 843, 3389), read in
template tiles with the turned-room fixes (`tools/cox/rsprox_room.py`). Each row
replaces the spec row named.

| row | was | recorded, and now the content |
|---|---|---|
| V1 | the room wakes at once; roots flowered at the start | first move S+2; every unstung grub sickly at S+10 with no hp lost; roots flower S+10..S+15 (two at +10, one each at +11, +13, +14, +15) |
| V7 | the farthest grub; she centres over it; the drain armed the same tick | a random untagged grub; she stings from the nearest edge-adjacent 5x5 spot spanning its middle; the sting lands the next tick |
| V8 | 1 hp a tick from full, 125 ticks to the hatch | the grub drops to 56% (70 of 125, bar 17/30), then 1 hp a tick: the hatch 69 ticks after the sting. Its bar falls a drawn step at a time (~4.2 ticks a step) -- `npc_headbar_from`, since `npc_statheal` sends no bar on a loss |
| V10 | a soldier on a paired tile; the grub back 16 ticks after its soldier went | the death seq, then 2 ticks later the dead form and a soldier risen on the grub's own tile; the grub healthy again 37 ticks later, soldier or not |
| V11 | 17 to the portal and Vespula every 8 ticks, a projectile | the portal only: 3% of its max every 5 ticks from 9 ticks after the rise, graphic 1367 on itself, sound 808 |
| V12 | enraged on arrival home; held 24 ticks after the last hit | enraged the tick after an ATTACK on the portal (the swing, before any hit), in place; 20 ticks a cycle, renewed after one flying tick while the portal was attacked within 9 |
| V14 | everyone within a tile of the target, every 2 ticks | one raider on her reach, landing the tick after (sound 819); her own 3-tick clock: ready 3 ticks after a sting |
| V18 | every raider on the two hit tiles; lands 1 tick on, projectile end 30 | the portal's attack on its target (the first raider to hit it), every 2 ticks; projectile 1366 from the pillar, end 60, slope 2, heights 140/120 raw; lands 2 ticks on (sound 2885) |
| V23/V24 | everything at D+3, one stack of 2 aids at her | D+2 grubs/portal gone, death seqs, pillar anim; D+4 the message, boils burst, her journal at hover SW + (2,5) with the drop (2 aids as separate items); D+6 closed pillars. Portal: no defend or death anim |

Not in the recordings (unchanged): her ranged, her landing, a fed grub, a soldier's explosion.

**Root regrowth** (NR MedivaemiaRootObject: a picked root flowers again 8 ticks
later). A flowered root is a `loc_change` of the empty map loc, and a change's
revert only ever restores the map loc (the first pending target stands, as
LostCity's Loc lifecycle). The old pick's `loc_change(empty, 8)` therefore left
every picked root empty for good. The regrow is now the portal's `[ai_queue5]`,
8 ticks after the pick, carrying the root's index, so it ends with the room.

