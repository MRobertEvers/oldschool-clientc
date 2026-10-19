# Skeletal Mystics (Chambers of Xeric) -- tick spec and content audit

Unit: guardians_mystics. Written 2026-10-18 for `docs/minigames/cox/COX_FINISH_PLAN.md`
phases 1-3. Content: the MYSTIC procs of `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/scripts/cox_minions.rs2`
(the shaman procs in the same file belong to the shamans unit), the mystic blocks of `configs/cox.npc` and `configs/cox.constant`.

## Sources (rank order: Jagex > recorded data > wiki > plugins > Near-Reality > ours)

| tag | source | file |
|---|---|---|
| J1 | Update: Chambers of Xeric Changes (2023-11-29): "A handy set of portals will now appear after all Mystics are defeated"; "from one end to the other, or from the middle of the room to either the start or the end" | `sources/newsposts/wiki_Update_Chambers_of_Xeric_Changes.wikitext:59,300` |
| J2 | Update: More Leagues IV Changes (2023-12-06): mystics "spawn dynamically based on the number of players who are actually in the Raid"; Christmas 2023: "now scale correctly when manually scaling" | `sources/newsposts/wiki_Update_More_Leagues_IV_Changes.wikitext:158`, `..._Christmas_2023...:86` |
| J3 | Mod Ash, Discord 9 Jul 2026: "Skeletal mystics - 25" (regen ticks) | `sources/wiki_Skeletal_Mystic.wikitext` |
| J4 | Project Rebalance (2024): mystics' heavy-ranged and crush defence reduced | `sources/newsposts/wiki_Update_Project_Rebalance_Combat_Changes.wikitext:134,147` |
| W1 | Skeletal Mystic infobox (rev 15315352): size 2, HP 160 (CM 240), att/str 140, def 187, magic 140, ranged 1, attack +85, str +50, magic att +40, magic str 38, def stab/slash 155, crush 75, magic 140, ranged light/standard 115, heavy 75, speed 4, max hit "Varies", attributes undead+xerician, aggressive "No"; article: "standard magic attack, and a damaging form of Vulnerability, along with melee"; "react to their target's overhead prayers ... If the mystic cannot reach its target using Protect from Magic, they would continue using magic, whereas if a mystic can reach its target who is praying against magic, it would attempt to melee them"; "attack range of 10 and cannot be safespotted" | `sources/wiki_Skeletal_Mystic.wikitext` |
| W2 | CoX/Strategies: Fire Blast or Vulnerability, melee punch; "Protect from Magic reduces the mystics' accuracy and damage by roughly 50%"; corner safespot "get up close to a mystic to prompt it to melee, then corner safespot ... drastically reduces the chance of a magic attack. If the mystic is using magic non-stop, it will no longer melee until prompted to do so again"; count table (1-2 -> 3, 3-5 -> 4, ... 27+ -> 12); drops; portals on the last kill | `sources/wiki_Chambers_of_Xeric_Strategies.wikitext:539-579` |
| W3 | Hit delay, Magic: `1 + floor((1 + distance) / 3)`, distance edge to edge | `sources/wiki_Hit_delay.wikitext:244-298` (fetched 2026-10-18, rev 15354744) |
| V1 | Synq 0:31:53: salve(ei), PfM; "trapping them on an obstacle. They'll get trapped on objects either east or west of them. To safe spot a mystic, make sure there are no obstacles between you. Doing this will force it into eventually trying to melee you"; "Stand diagonal of the mystics so you don't take any melee damage"; "Sometimes they'll start maging you again ... randomly decide to switch attack styles"; "use a second mystic as a safe spot"; "god spots ... rocks diagonal from the purple objects" | `synq_transcript.md:267-277` |
| V2 | Zulerah luring guide: "whenever you have a clear line of sight to the mystic they'll always try to melee you"; lure all three and corner-trap; "occasionally they will attack you with mage" | `sources/transcripts/yt_ztW5D_D5C6s.md` |
| V3 | Extile: "pseudo safe spot ... stuck diagonally ... they believe that they can reach you to melee you so they will skip attacks"; Gnomonkey: "they can choose to melee you 50/50 chance if you're close to them"; edb0ys: "only attempt to use melee if they have like a route to get to you"; Tubby: "fairly large aggro radius", "pretty accurate attacks even through prayer" | `sources/transcripts/yt_0a61znGm0pc.md`, `yt_wxGBi89vE_s.md`, `sources/transcript_edb0ys_cox.md`, `sources/transcript_tubby_solo_cox.md` |
| N1 | Near-Reality `SkeletalMystic.java`, `DarkAltarRoom.java`, `SkeletalMysticTeleportPortal.java`, `ScalingMechanics.getSkeletalMysticCount` | `~/Documents/git_repos/RSPS-NEAR-REALITY/.../chambersofxeric/` |
| C1 | Cache: npc 7604-7606 (size 2, op2 Attack); seq 5523 skeleton_update_mage_casting, 5485 skeleton_update_attack_weapon; spotanim 1322 skeleton_big_casting_red, 1321 skeleton_big_casting_purple, 130 fireblast_travel, 157 firewave_impact, 168 vulnerability_travel, 169 vulnerability_impact, 339 splash; loc 29741 raids_skeletalmystics_symbol (Mark of Power, 2x2), 49996-49998 portals (start "Travel-End", middle "Travel/Travel-End/Travel-Start", end "Travel-Start") | `configs/all.*.compack`, `configs/all.loc` |

## Geometry

Room plane 1 of the template (plugin room key y=164). Spawn tables per variant are N1's `spawnLocations` (6 tiles; ours cycles them one tile east for counts above 6), mark and portal tiles N1's (all decoded in `~cox_mystic_spawn_tile`, `~cox_mystic_mark_tile`, `~cox_mystic_portal_tile`). The room is a maze of rocks and pillars ("the mazelike room", J1) -- the obstacles ARE the safespots.

## Tick model

Npcs run before players. "T" = the mystic's decision tick; it reads raider tiles at the END of T-1.

| # | mechanic | decide / read | geometry | damage (solo, maxed) | prayer read | tell | source |
|---|---|---|---|---|---|---|---|
| 1 | Count | at spawn: party 1-2 -> 3, then +1 per 3 players, cap 12 (party = players actually in the raid) | -- | -- | -- | npcs | W2, J2 |
| 2 | Aggression | each tick an idle mystic takes the first raider within 15 tiles it can SEE (line of sight; line of walk while its style is melee) | radius 15 from the SW tile (OURS; "fairly large aggro radius") | -- | -- | the mystic turns and casts | N1 `setForceAggressive(true)`, V3; W1's "aggressive No" overruled by recorded play (V1-V3) |
| 3 | Cadence | 4 ticks | -- | -- | -- | -- | W1 |
| 4 | Style state | starts MAGIC. After every swing: 50% -> MELEE if a straight line of walk to the target exists, else MAGIC. MELEE and not beside the target: no swing; steps toward him (naive, one tile a tick -- an obstacle stops it) and asks again each tick; more than 8 ticks out of reach -> MAGIC | melee reach = orthogonally beside the 2x2 | -- | -- | a melee-style mystic walks at you instead of casting | N1 `attack()` reroll + `processNPC`, V1, V3 |
| 5 | Fire Blast (4 casts in 5) | cast T, hit lands T + 1 + floor((1+d)/3) | range 10 with approached LOS | accuracy roll (magic 140, +40), max 25 (+party/4) | PfM at the cast: damage halved | seq 5523 + graphic 1322 on the mystic at T; projectile 130 to the player landing on the hit tick; impact 157 or splash 339 | W1, W2, W3, N1 |
| 6 | Vulnerability (1 cast in 5) | as #5 | as #5 | as #5, and a landed cast drains Defence 4-8 | as #5 | seq 5523 + graphic 1321 (purple); projectile 168; impact 169 or splash 339 | W1, W2, N1 |
| 7 | Punch | T, lands T | beside the footprint | crush accuracy (att 140, +85), max 25 | PfM at T: halved | seq 5485 | N1, W1, W2 |
| 8 | Corner trap | a melee-style mystic whose naive step toward the target is blocked by an obstacle never arrives and never swings until the 8-tick give-up, or forever if he is still inside the reach box (a diagonal) | obstacle east/west of the mystic (V1) | 0 | -- | mystic standing still beside a rock, facing you, not casting | V1, V2, V3, N1 |
| 9 | Exit | Mark of Power (2x2) on the exit until the last mystic dies | mark tile | -- | -- | loc 29741 | N1, W2 |
| 10 | Portals | appear on the last kill; start <-> end, middle -> either | portal tiles | -- | -- | locs 49996-49998 appear | J1, W2, N1 (destinations) |
| 11 | Regen | +1 per drained stat every 25 ticks | -- | -- | -- | -- | J3 |
| 12 | Drops | big bones + 5-10 each golpar/buchu/noxifer seeds | at the corpse | -- | -- | ground items | W1, W2 |
| 13 | Defences | ranged (standard 115) and crush (75) are the weak sides; undead: salve(ei) | -- | -- | -- | -- | W1, J4, V1 |

### Rulings

- R1 Aggression: W1's infobox says "No"; every recorded guide (V1-V3) and N1 show them aggroing on sight. Recorded play outranks the infobox field.
- R2 Prayer: W2 "accuracy and damage by roughly 50%"; N1 halves damage only. Implemented as N1 (halved damage after a normal accuracy roll); with PfM's own effect on nothing else, the net is ~50%, matching "roughly 50%".
- R3 "Cannot be safespotted" (W1) vs corner trapping (W2, V1-V3): both true -- out of line of sight they keep casting (no LOS safespot), but a melee-aggroed one gets stuck. Implemented as #4/#8.
- R4 Magic hit delay: W3 (wiki) over N1's projectile arithmetic.
- R5 Vulnerability damage: W1/W2 "damaging form"; N1 never applies its damage roll. Wiki wins: it damages like the standard cast.
- R6 Max hits: W1 "Varies"; N1's 25 (+party/4 for fire) is the only number. Kept.
- R7 Portal destinations and the middle portal's menu: N1 only.

## Audit (mechanic / ours before 2026-10-18 / OSRS / verdict)

| mechanic | ours (before) | OSRS (spec) | verdict |
|---|---|---|---|
| Melee-style swing reach | fired from 10 tiles (attackrange is the magic reach): melee mystics punched across the room; no walk, so no corner trap existed | walk to melee distance first (N1), the walk is what traps (V1) | FIXED: `[ai_opplayer2]` steps toward and waits unless orthogonally beside (`~cox_mystic_adjacent`) |
| Accuracy | none: every cast/punch landed 0..max (probe: 3 mystics killed an unarmoured player in 9 ticks) | accuracy rolls | FIXED (`~npc_player_hit_roll` magic / crush) |
| Bonuses, undead | none stated; no `undead` param, so salve(ei) did nothing | W1 bonuses; undead | FIXED (cox.npc) |
| Magic hit timing | hit at duration/30 ticks (often before the projectile arrived) | W3 | FIXED (`~cox_mystic_magic_delay`; projectile fitted to land with it, from the 2x2's middle) |
| Vulnerability cast graphic | the player spell's 167 | 1321 skeleton_big_casting_purple (N1, C1) | FIXED |
| Splash vs impact | keyed on 0 damage | keyed on the accuracy roll (a hit for 0 is not a splash) | FIXED |
| Room-cleared count | `%varp6971` -- a PLAYER varp decremented by whichever raider got the kill | the room's living mystics (N1 `onFinish`) | FIXED (`~cox_mystic_living`); %varp6971 still written at spawn for the selftest |
| Portals | laid at spawn | appear after the last kill (J1) | FIXED |
| Portal ops | no handlers | travel start/end/middle (N1) | FIXED (`[oploc1..3]` handlers) |
| Drops | none (the death override skipped the default drop) | big bones + seeds (W1) | FIXED |
| Count table, HP 160, def 187, speed 4, range 10, regen 25, 1-in-5 vulnerability, defence drain 4-8, PfM halves | as spec | as spec | OK |
| Party size | `~cox_olm_party_size` returns 1 (root's stub) | players actually in the raid (J2) | OPEN (root) |
| Notice radius 15 | OURS | "fairly large" (V3) | OPEN (no number) |
| Melee reach box for the 8-tick give-up | huntall radius 2 from the SW tile | N1 `isOnRange(.., 1)` (a 2-tile box round the footprint) | approximate; unchanged |
| Idle wander | generated `defaultmode=wander`, range 5 | unknown | OPEN |
| CM stats/max hits | `%varp6818` (player varp, read only with a target bound) | x1.5 | OK for now |

## Probe evidence (2026-10-18, seed 5, `::coxgoto 9 0`, no armour, no prayer)

Before (`scratchpad/probe/base_m`): all three mystics cast on tick 14 together (proj 130, hit at 15: 21, 25, 8), 18, 22 (two vulnerabilities with graphic 167) -- 112 damage in 9 ticks, the player died. No melee was ever chosen in that window.
After (`::coxtest 9 0`, `scratchpad/probe/after_m`): no aborts; casts from ~10 tiles hit 4 ticks later (projectile 130 sent with a 120-cycle flight), splashes (339) on missed rolls, punches (5485) only from a mystic beside the player, a melee-style mystic walking one tile a tick toward him. Not observed in this probe: a full clear (portals) -- the unarmoured player died at tick 57.

## Probes the spec cannot settle

- P1 a melee-aggroed mystic behind a rock east/west of it: steps, stops, never swings (#8); and the 8-tick give-up back to magic.
- P2 a diagonal stand: the mystic stays melee and silent (Extile) -- check it does not flip to magic.
- P3 PfM halves both styles over 100 casts.
- P4 portals appear on the last death; Travel moves the player.

## RSProx (2026-10-18)

Recorded behaviour, read from RSProx 3282, 3285 and 3389 (9 mystics, 20 casts), is in
`docs/rsprox-analysis/cox/mystics.md` (rows RM-*). It **overrules** these rows of the tick model above:

- #2 aggression: within **7** of the south-west tile in sight (RM-W1), not 15. Unhunted, a mystic answers an attack 2 ticks after it lands (RM-W2).
- #5, #6 the casts' tells: the seq 5528, cast graphics at height 164, `curse_travel` (109) for Vulnerability, start 30, end 46 + 10 x gap, from the south-west tile, impacts 131 / 110 / 85 at height 124, the synths (RM-A1..A6). A splash sends no hitsplat.
- #6 the drain: none on a boosted Defence (RM-A7).
- #12 drops: on D+5, the line, seeds for 3000 ticks (RM-D2). The death seq comes with the killing hit (RM-D1).
- #9, #10: the mark at rotation 0 and replaced by `invisible_type8_nonblocking`; the portals at rotation 0, ccw middle (20, 18) and end (7, 18) (RM-D4).
- The ids go with the spawn slot (a, b, c); the wander range is at least 7 (RM-S1). In a fight a mystic never stepped (RM-S2).
- #4, #8 the melee walk and the corner trap are unrecorded (RM-M1). The walk is now the engine's pursuit (`npc_approach`), the line a rock stops.
