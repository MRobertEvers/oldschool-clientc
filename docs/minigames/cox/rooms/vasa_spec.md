# Vasa Nistirio -- tick spec and content audit (2026-10-18)

Unit: vasa. Content: `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/scripts/cox_vasa.rs2`,
the Vasa block of `configs/cox.constant` (`^cox_vasa_*`, `^cox_npcvar_vasa_*`) and the four
`raids_vasanistirio_*` records of `configs/cox.npc`. Solver plan: `vasa_solver.md` (this folder).

## Sources

Rank (COX_FINISH_PLAN): Jagex > recorded data > video/wiki > plugin constants > Near-Reality > our code.
No recorded per-tick data exists for CoX (Blert's CoX recorder is "Coming Soon",
`sources/blert_repo/README.md`), so the tick table rests on Near-Reality, refined by everything above it.

| id | source | file | what it settles |
|---|---|---|---|
| J1 | Jagex, *Chambers of Xeric Changes* (29 Nov 2023) | `sources/newsposts/wiki_Update_Chambers_of_Xeric_Changes.wikitext:282-296` | ranged defence 60 -> 40; heal only if the crystal is not broken ("all the healing will be discarded"); blue arrow splats while storing, purple heal splat on failure; Overload (+) drop |
| J2 | Mod Ash, Twitter 19 May 2020 (quoted on the wiki) | `sources/wiki_Vasa_Nistirio.wikitext` Mechanics | "Every 2 ticks, it heals 1% HP and (10% + 1) Defence." |
| J3 | Mod Ash, Discord 9 Jul 2026 (quoted) | same, and `wiki_Glowing_crystal.wikitext` | regen: Vasa 1 per 10 ticks, crystal 1 per 9 |
| J4 | Jagex, *A Kingdom Divided* | `sources/newsposts/wiki_Update_A_Kingdom_Divided.wikitext:586` | CM crystals lose the +50% (CM only) |
| W1 | wiki *Vasa Nistirio* (rev 15322286) | `sources/wiki_Vasa_Nistirio.wikitext` | infobox stats; the special; boulders within 8, 3x3; stomp up to 8/tick; 67-tick timer; drops |
| W2 | wiki *Glowing crystal* | `sources/wiki_Glowing_crystal.wikitext` | 120 hp, def 100, stab -5 / slash 180 / crush 180, ranged-immune, 1/3 magic; "different crystal ... unless the special attack was just used" |
| W3 | wiki *CoX/Strategies*, *CoX/Challenge Mode/Strategies* | `sources/wiki_Chambers_of_Xeric_Strategies.wikitext`, `..._Challenge_Mode_Strategies.wikitext:375-400` | boulders "land at the player's position", 3x3; pray melee under him; teammates halve the stunned player's damage |
| W4 | wiki *Magical fire (Chambers of Xeric)* (fetched 2026-10-18, `?action=raw`) | `sources/wiki_Magical_fire_Chambers_of_Xeric.wikitext` | loc 30019, op Cross, damages who passes |
| W5 | wiki *Nistirio's manifesto* (fetched 2026-10-18) | `sources/wiki_Nistirios_manifesto.wikitext` | the book drop (not implemented) |
| W6 | wiki CA *Stop Drop and Roll* | `sources/wiki_Stop_Drop_and_Roll.wikitext`, `wiki_combat_achievements_cox.tsv` | "Kill Vasa before he performs his teleport attack for the second time" |
| V1 | Synq, CoX guide, Vasa chapter 0:55:16-1:09:16 | `synq_transcript.md:360-415` | damage read at the end of the teleport animation; eat after; 3x3 boulders dodged by 2 tiles; attacks every 3 ticks; 67 total crystal ticks; same-crystal rule; crystal broken on the leave tick still heals; the CM skip |
| V2 | Tubby, solo CoX | `sources/transcript_tubby_solo_cox.md:58-66` | PfMissiles reduces but hits through; venge reflects 86 off an anglerfish-boosted special |
| V3 | edb0ys, CoX guide | `sources/transcript_edb0ys_cox.md:170-188` | teleported "even if you're on the other side of the flames"; PfMagic joiners take nothing; timer only counts on a crystal and resets at the special |
| V4 | Gnomonkey budget solo; Academy; Plank2g; Charles; Extile; Beleti; PecanBread | `sources/transcripts/yt_*.md` (README.md:77-91) | pray melee under him -> less damage; 4 tbow hits before the first crystal; skip tech |
| R1 | RuneLite `HitsplatID` (commit 4f3ff3e2ad) | `~/Documents/git_repos/runelite/runelite-api/src/main/java/net/runelite/api/HitsplatID.java:37-38` | HEAL = 6 (purple), CYAN_UP = 11 (the blue arrow) |
| N1 | Near-Reality `VasaNistirio.java`, `VasaNistirioRoom.java`, `Crystal.java`, `VasaNistirioMagicalFire.java`, `RaidNPC.java` | `~/Documents/git_repos/RSPS-NEAR-REALITY/near-reality-server-main/core/src/main/java/com/zenyte/game/content/chambersofxeric/` | the tick table, positions, ids, max hit 18, PfMissiles x0.33, fire 22.5% |
| C1 | our cache (`configs/all.npc/.seq/.spotanim/.loc`) | OSRS-Content | names and lengths: `vasa_spawn` 150 cy, `vasa_stun_spawn` 120, `vasa_attack` 60, `vasa_heal_spawn`/`vasa_healed` 55, `teleport_rune_human` 80; spotanims 1327-1330 `raids_vasanistirio_{magic,range}_{travel,impact}`, 80 `stunned`, 1296 `arceuus_teleport_spotanim`; crystal locs 4x4 |

NR's animation ids (rise 7408, swirl 7409, explode 7410) map by role and length onto the cache's names:
`vasa_spawn` (5 ticks, the rise to T0+5), `vasa_stun_spawn` (4 ticks, to T0+10), `vasa_attack` (2 ticks).

## Tick conventions

`T` is the server tick of an npc decision; npcs run before players, so a decision on T reads raider tiles
at the end of T-1 (manual Part 2). Vasa's whole fight is one `[ai_timer]` at interval 1 on Vasa. Boulders
in flight are kept on Vasa and resolved by that timer on their landing tick L, so they read raider tiles
at the end of L-1. Solo, max combat: every party term of the scaling is 1, so the wiki's numbers are the
solo numbers.

## The spec

| row | mechanic | decided | reads | geometry | damage / numbers (solo) | prayer | tell (client) | source |
|---|---|---|---|---|---|---|---|---|
| V1 | Vasa, three forms | -- | -- | 5x5; spawn SW (14,13)/(13,13)/(13,14) CCW/THRU/CW | 300 hp, def 175, mage 230, range 230, att/str 1 | -- | npc 7565 "Rocks" -> 7566 -> 7567 | W1, N1 |
| V2 | positions | -- | -- | crystals' SW (4x4) at the four corners (CCW (6,5),(6,23),(23,23),(23,5)); fire 2x2 at (15,1) | -- | -- | locs `crystal_off` in all four corners | N1 (`crystalLocations`, `fireLocations`) |
| V3 | Vasa defence | every hit | -- | -- | stab +170, slash +190, crush +40, magic +400, ranged +40 light/standard, +30 heavy; ranged att +100; xp +7.5% | -- | -- | W1 (post J1) |
| V4 | crystal defence | every hit | -- | 4x4 | 120 hp, def 100, magic 100; stab -5, slash/crush +180; ranged 0 damage; magic x1/3; xp +2.5% | -- | npc 7568 "Glowing crystal" | W2 |
| V5 | wake | each tick while dormant | end of T-1 | a raider within 7 (Chebyshev) of the room centre = his middle | -- | -- | `vasa_spawn` on the rocks | N1 stage -1; W1 "upon approaching" |
| V6 | entrance fire | T0 (wake) | -- | loc 30019 at (15,1), blocks | Cross: 22.5% of current hp, carried 2 tiles | -- | loc appears | W1, W4, N1 |
| V7 | rise | T0 | -- | -- | invulnerable (no Attack op on 7565; NR zeroes stage 1-3) | -- | T0 `vasa_spawn`, T0+5 `vasa_stun_spawn`, T0+10 `vasa_attack`, T0+12 -> 7566 | N1 stage 0 |
| V8 | GROUP | T0+13 (re-special: arrival at centre + 1) | end of T-1 | every raider in the room, or within 1 of the fire | two groups at random: half, rounded up, NEAR; the rest FAR (solo: near) (RV-S2) | -- | `arceuus_necromancy_anim` + `arceuus_teleport_spotanim` on each target | W1 ("divide them into equal amounts"); N1 `sendExplosionAttack`; V3 |
| V9 | TELEPORT | G+3 (T0+16) | hp at end of T-1 | NEAR: a random tile 3-4 (Chebyshev) from his middle, next to him; FAR: 8-11 out, by the walls (RV-S2) | NEAR: bank += max(0, hp - 5), **not capped at 99**, stunned to the explosion; FAR: nothing | NEAR: all prayers off and refused to the explosion; FAR: free | `vasa_stun_spawn`; `stunned_thieving` on each NEAR | W1; N1 `teleportTargets`; V1 0:56:23; V2 (86 reflected) |
| V10 | projectiles | G+6 (T0+19) | -- | 8 tiles at (-1,2,5)^2 from his SW minus the middle | -- | -- | `vasa_attack`; 8x `raids_vasanistirio_magic_travel`, 150 cycles | N1 `sendProjectiles` |
| V11 | EXPLOSION | G+11 (T0+24) | end of T-1 | everyone within 4 of his middle (the NEAR group, and FAR raiders who ran in) | bank / shares to each; a runner under Protect from Magic takes 0 (RV-S4) | -- | 8x `raids_vasanistirio_magic_impact`; splat | W1; N1 `sendExplosions`; V3 |
| V12 | invulnerable in the special | G .. G+11 | -- | -- | hits land as 0 | -- | blue 0 | N1 `applyHit` |
| V13 | crystal pick | G+13 (T0+26) / pause end / skip | -- | without replacement since the last special; all four used -> any but the current | -- | -- | `crystal_off` -> `crystal_on` (glow); npc 7568 appears on it | V1 0:58:36, 0:59:08; W2 |
| V14 | walk to it | from the pick | -- | a fixed route of straight legs (`~cox_vasa_leg`) to a 5x5 spot sharing an edge with the crystal; 14-15 tiles at 1 tile a tick; another face stays open | -- | -- | `vasa_walk` | N1 `calcFollow`; V1 "follow a set path ... always an opening"; our walk maps (below) |
| V15 | crystal timer | each tick on a crystal | -- | -- | 67 ticks shared by all crystals between two specials; leaves on the 68th | -- | -- | W1; V1 1:06:51; V3 0:47:04; N1 |
| V16 | siphon | every 2 ticks on it | -- | -- | stores 1% max hp (3), restores (10%+1) defence now | -- | cyan up-arrow splat (hitsplat 11) "3" | J1, J2, R1 |
| V17 | special resets | at G | -- | -- | timer = 67, crystal pool = all four | -- | -- | V1 0:59:41; V3 |
| V18 | timer out | 68th siphon tick | -- | -- | heals the stored amount (a crystal broken on that tick still pays) | -- | purple heal splat; `vasa_healed`; crystal dark; walks to centre; special the tick after arrival | J1; V1 1:07:56; N1 stage 6 |
| V19 | crystal broken | tick after its death | -- | -- | store discarded | -- | `vasa_healed`; walk next tick; at centre faces south (tick 1), next crystal (tick 3) | J1; N1 stages 5/7; V3 "pauses" |
| V20 | invulnerability swap | 3rd siphon tick | -- | -- | Vasa -> 7567 (0 damage), the crystal becomes hittable; before that the crystal takes 0 | -- | `vasa_heal_spawn` at arrival, form 7567 | N1 `++delay >= 3`, `cannotHit`; V1 1:00:14 |
| V21 | boulder | every 3 ticks while walking (first on the first moving tick) | target tile = end of T-1; hit = end of L-1, L = T+2 | every raider 1..8 tiles from his footprint; 3x3 around the tile | accuracy (230+9)(100+64) vs ranged def; 0..18 | PfMissiles x0.33, read at L | `vasa_attack`; `raids_vasanistirio_range_travel` to the tile (60 cy); `range_impact` at L | W1 (8 tiles, standard ranged), W3 (3x3 at the player's position), V1 (3 ticks, 2 tiles), N1 (18, 0.33, +2) |
| V22 | stomp | every tick from the first group | end of T-1 | under his 5x5 | 0..8 | PfMelee stops it **[amount unsettled, P5]** | his footprint | W1, W3; V4 Gnomonkey; N1 1-5 |
| V23 | regen | every 10 ticks (crystal 9) | -- | -- | 1 level of each drained stat incl. hp | -- | -- | J3 |
| V24 | death | -- | -- | -- | fire removed; live crystal dark and gone | -- | `vasa_death` | N1 `sendDeath` |
| V25 | drop | death | -- | at his middle | Overload (+) 1, Xeric's aid (+) 2, Twisted (+) 2, Endarkened juice 5 | -- | ground items | W1 drops; J1 |
| V26 | skip | at G with no target | -- | -- | no special; he walks to a crystal at once and is hittable | -- | no teleport | V1 1:08:07; V4 PecanBread |
| V28 | exit crystals | death | -- | two 1x1 `raids_blockage_purple_small` (NR 30016) at CCW (4,15)/(4,16), THRU (15,27)/(16,27), CW (27,16)/(27,15) | shatter: `raids_crystal_smashing`, sounds 1021/3821, gone 2 ticks later | -- | loc anim | N1 `blockingCrystalLocations`, `Raid.shatterCrystal` |
| V27 | Stop Drop and Roll | -- | -- | -- | kill before the 2nd teleport | -- | -- | W6 |

### Rulings

- **Damage cap (V9).** NR caps each raider's hp at 99 and each splat at 99. Tubby reflects 86 with
  vengeance off an anglerfish special (75% of ~115): no cap. Video > NR. Not capped.
- **PfMagic joiners (V11).** NR halves; the wiki ("they will not take any damage") and edb0ys say 0. Wiki > NR.
  CM/Strategies' "50% of the stunned players' health" is the share the stunned player keeps -- consistent.
- **Boulder cadence (V21).** NR fires every 4 ticks; the wiki infobox (speed 3) and Synq ("attacks every three
  ticks") say 3. 3.
- **Same crystal (V13).** NR forbids the current crystal even after a special; Synq and W2 allow it after a
  special. Without replacement since the special (Synq: "can't go to the same crystal twice before its next
  explosion").
- **Heal (V16/V18).** Mod Ash's 2020 per-2-tick heal was live; J1 (2023) stores it and pays only on a timeout.
  The defence part of J2 stays live (J1 speaks of healing only) -- **unsettled** (P6).
- **"Underneath" vs "next to" (V9).** The Academy says "directly underneath"; the wiki and NR say next to him. Next to.
- **Skip (V26).** NR still runs the special with nobody in it (and stays invulnerable); Synq's skip "damages
  Vasa earlier", which only holds if there is no special. No special.
- **Stomp (V22).** NR rolls typeless 1-5 ignoring prayer; the wiki says up to 8 and to pray melee. 0..8,
  stopped by Protect from Melee (how much it stops is not stated anywhere).

## Audit (before this round -> now)

| mechanic | ours before (2026-08-20) | OSRS | verdict / fix |
|---|---|---|---|
| state | 15 player varps read in every `[ai_timer]` | -- | **broken**: every timer aborted on its first line ("no primary player bound"); Vasa never woke. Moved to npc vars on Vasa (`^cox_npcvar_vasa_*`) |
| wake | `npc_findhero` (no range) | within 7 of centre | fixed (V5) |
| rise | changetype and special on the wake tick | 13 ticks, 3 anims | fixed (V7) |
| special timing | group -> 2 -> teleport -> 7 -> explosion -> 9 walk | 3 / 8 / 10 (NR) | fixed (V8-V11) |
| teleport rings | 2-8 / 10-24 around a SW-based centre two tiles off | NEAR 3-4 / FAR 8-11 around his middle, half each way (RV-S2: untranslated RSProx; NR's 3-4 near is right, its 11-14 far is not) | fixed (V9); `vasa.teleport_ring` judges it |
| prayers in the special | off, not blocked | off and refused | protection refused 8 ticks (`~prayer_block_protection`); other prayers can still be clicked -- **gap**, needs a block-all in `prayer.rs2` |
| HP bank | `min(99,hp)-5`, split, then `min(99,...)` | uncapped | fixed (V9) |
| PfMagic joiner | halved | 0 | fixed (V11) |
| projectiles / explosion gfx | none / per-player gfx | 8 projectiles to a ring, 8 impacts | fixed (V10/V11) |
| invulnerability (special, siphon) | none: every form took full damage | 0 | fixed (`[ai_queue2]`, V12/V20) |
| crystal locs | never placed; no glow | four dull corners; the picked one glows | fixed (V2/V13) |
| crystal hittable | always | only while he siphons | fixed (`[ai_queue2,raids_vasanistirio_crystal]`) |
| crystal timer | a fresh 67 at every crystal | 67 shared until the next special | fixed (V15/V17) |
| siphon store | 3 per 2 ticks, no splat, no defence | 3 per 2 ticks with the cyan splat, defence restored | fixed (V16) |
| heal on timeout | statheal only | + purple heal splat | fixed (V18) |
| walk targets | centre = SW at (16,16) (2 tiles off); crystal = SW on the crystal tile (overlapping it); one straight `npc_walk` (stalls on the rocks) | spawn tile; a set path to a spot beside the crystal | fixed (V14): routes per variant from `::coxvasamap` |
| pause / re-special | 3-tick pause; special on arrival | pause 3; special arrival+1 | fixed |
| boulders | every 4 ticks, only the targeted player, 0..25 flat, no accuracy | every 3, whole 3x3, 0..18 with accuracy | fixed (V21) |
| stomp | anyone within 1 of his SW tile | under the 5x5 | fixed (V22) |
| npc stats | no defence bonuses at all (magic +400 missing), walking/healing forms wander | V3 | fixed in `cox.npc` |
| crystal | death plays `human_death` | goes dark | `death_anim,null` |
| fire Cross | no handler: no way out once lit | 22.5% hp, crosses | added (V6) |
| drops | none | V25 | added |
| party scaling | none (party is always 1 in this tree) | wiki scaledhp | not done (solo only) |
| sounds | none | NR has 8 | not done (NR ids only, unverified) |
| Nistirio's manifesto | none | once per account | not done |
| CM | none | x1.5, crystals not (J4) | not done |
| XP on a 0 hit | the funnel gives XP before `[ai_queue2]` zeroes it | 0 xp (NR) | **gap** (shared funnel) |

## Stand tiles and routes (V14)

`::coxvasamap` (cox_vasa.rs2) prints the room's walk map in template coordinates; 41 layouts gave one map per
variant whatever the turn. A 5x5 BFS from his spawn to the nearest spot sharing an edge with each crystal, split
where the route turns, gives (template SW tiles; the last is where he siphons):

| variant | spawn | crystal 0 (low,low) | 1 (low x, high z) | 2 (high,high) | 3 (high x, low z) |
|---|---|---|---|---|---|
| CCW | (14,13) | (13,13) (12,14) (5,14) **(5,9)** | (14,23) **(10,23)** | (23,13) **(23,18)** | (14,12) (13,11) (13,3) **(18,3)** |
| THRU | (13,13) | (12,13) (11,14) (3,14) **(3,9)** | (13,14) (14,15) (14,22) **(9,22)** | (23,13) **(23,17)** | (13,4) **(18,4)** |
| CW | (13,14) | (4,14) **(4,9)** | (13,15) (14,16) (14,24) **(9,24)** | (14,14) (15,13) (22,13) **(22,18)** | (13,4) **(17,4)** |

Every route is 14-15 tiles, which settles P4: at a tile a tick he reaches the first crystal ~16 ticks after the
explosion, the "four [twisted bow] hits ... before the first crystal" of Synq 1:01:34 (hits at +0, +5, +10, +15).
The way back is the same legs reversed.

## Probe results (2026-10-18, scriptrun, `::coxtest 10 0` then `::coxgoto`; seed vp1)

Run dirs under the session scratchpad `vasa/runs/p2`, `p7`. No `abort`, no "no primary player bound".

| event | expected | measured |
|---|---|---|
| wake T0 | raider within 7 | T0 = 21, `vasa_spawn` |
| anims | T0+5, +10, form +12 | 26, 31, 33 |
| group / teleport / projectiles / explosion | +13 / +16 / +19 / +24 | 34 / 37 / 40 / 45 |
| explosion damage | hp - 5 at end of T0+15 | 94 at 99 hp; 50 at 55 hp (second special) |
| prayer block after teleport | 8 ticks | `prayer_protect_blocked` = 45 at 37 |
| crystal lit + walk | T0+26 | 47 (loc 29774 + npc 7568), arrives 61 (14 tiles) |
| boulders | every 3 from the first moving tick, land +2 | thrown 48, 51, 54, ...; hits on 50, 53, ...; max seen 17 (cap 18) |
| healing form / crystal hittable | arrival + 2 | 63 / first crystal hit 72 |
| shared 67 | crystal 1 broken after 53 siphon ticks -> crystal 2 lasts 14 | crystal 1 62..114 broken (dark 115); crystal 2 arrives 147, leaves 161 |
| broken -> pause -> next | walk next tick, face on arrival+1, next crystal +3 | 115 leave, 131 face, 133 next crystal |
| timeout -> special | heal, walk, group arrival+1 | 161 leave (heal), 176 face, teleport 180, explosion 188 |
| stomp | 0..8 per tick under him; 0 under PfMelee | 0..8; all 0 with the prayer on |
| no raider in the room at a group | no special, straight to a crystal | seen after the probe player died (p6) |

## Probes (facts the first runs must print)

- **P1** the wake tick and T0+13/16/24/26 events in the tick log (`hit_player` at T0+24 = hp-at-T0+15 - 5).
- **P2** `npc_changetype` keeps the 1-tick timer (Vasa keeps acting after T0+12 and after each form swap).
- **P3** settled against our map (above); still wants one frame-counted video per variant to confirm OSRS's stand tiles.
- **P4** settled: the routes are 14-15 tiles, matching Synq's four tbow hits before the first crystal.
- **P5** stomp under Protect from Melee (full stop vs reduction).
- **P6** defence restore live vs stored after J1.
- **P7** whether he throws during the 3-tick pause at the centre (NR: no; nothing else says).

## Round 2 content changes (2026-10-18)

- The exit crystals (V28), placed at spawn, shattered on his death (seen in every sweep seed).
- `~cox_vasa_prepare_hit` via `cox_combat.rs2` (`~cox_prepares_hit` / `~cox_prepare_player_hit`, one line each):
  a hit on him while he prepares the special or siphons is 0 when it is prepared, so no XP either (NR
  getXpModifier), with NR's "preparing a powerful spell" line in the special.
- `cox_selftest.rs2`: the old #108-#118 (the player-varp state machine) deleted; #108 routes reach their
  crystals in every variant, #109 the footprint distance, #115 the stomp hitbox, #117 the crystal's damage.
