# Verzik Vitur (Normal Mode): what the server sends

Read from 28 recorded kills (see [README.md](README.md)) and compared with six scriptrun kills of our room (`test/raids/tob_verzik.lua`, trio). "Tick" offsets are server ticks; "cycles" are client cycles, 30 to a tick. Projectile heights are in our units (raw ÷ 4); spotanim heights are raw.

The **Status** column says what this pass did: **fixed** (our content now sends what is recorded), **open** (a difference that was found and not changed, with the reason).

## The fight's skeleton, tick by tick

Offsets from the death animation that ends each phase. Every row holds in all 28 kills unless a count says otherwise.

| Event | Recorded | Ours before | Status |
|---|---|---|---|
| P1 death anim `verzik_phase1_death` | +0 | +0 | |
| P1 → P2 transition npc appears | **+4**, a new npc `verzik_phase1_to2_transition`; no animation | +3, the P1 npc retyped, playing `verzik_phase2_spawn` | fixed (a new npc, +4, no anim) |
| `verzik_throne_npc` added | +4 | never | open: we draw the empty throne as a loc instead |
| Pillars collapse (`verzik_pillar_collapse`) | +4 | +3 | fixed |
| Transition says "You think you can defeat me?" | +5 | never | fixed |
| Pillars become rubble (`verzik_pillar_fade`) | +8, 4 after the collapse | 2 after the collapse | fixed |
| Rubble removed | 2 after the fade | 2 after the fade | |
| Becomes `verzik_phase2` | +17 | +16 | fixed |
| First P2 attack | +20 | +19 | fixed |
| P2 death anim `verzik_phase2_death` | +0 | +0 | |
| `verzik_phase2_to3_transition` appears, plays `verzik_phase3_spawn` | +2, a new npc | +2, a retype | fixed (a new npc) |
| `verzik_phase3` appears and says "Behold my true nature!" | +6, a new npc | +6, a retype, no line | fixed (a new npc, the line) |
| First P3 attack | +12 (27 of 27) | | |
| P3 death anim `verzik_phase3_death_a` + area sound 4008 (range 15) | +0 | +0, no sound | fixed |
| P3 **transforms** into `verzik_death_bat` (no animation) | +2 (21), +3/+4 (6) | +2, a new bat npc playing `verzik_phase3_death_b` | fixed (no anim); open: still a new npc, because the engine reaps the dying P3 npc |
| Bat removed; throne becomes `tob_dungeon_verzik_throne_transforming` | +8 (21 of 27), no `loc_anim` | throne at +2 with `loc_anim(verzik_throne_transform)`, bat at +8 | fixed (no `loc_anim`); open: the throne still swaps at +2 |
| Throne door `tob_dungeon_verzik_throne_door_opened` | +13 | +8 | open: `~tob_vault_trapdoor` keys its wait on the transforming throne already standing when the raid notices the death, so moving the swap to +8 let the vault place the door first and then overwrote it (a raider's click was lost in testing) |

## Starting the fight: the talk-to

The fight starts from Talk-to on Verzik. The opening line depends on the multinpc rung the player sees:

| Rung | Lines (all `chat_left`, `chathap2`) |
|---|---|
| `verzik_initial_base` (no completion yet) | "Now that was quite the show! I haven't been that entertained in a long time." → "Of course, you know I can't let you leave here alive. Time for your final performance..." |
| `verzik_initial_quickstart` | "Back again for more? I admire your perseverance. Shall we begin?" |

Then the menu **"Is your party ready to fight?"** with **"Yes, let's begin."** and **"No, don't start yet."**. On yes she says **"Oh I'm going to enjoy this..."** (head `verzik_phase1`, `chatlaugh1`), and in the same tick she becomes `verzik_phase1`.

Ours said "So, you wish to entertain me? Very well. Let us begin." and offered "Yes, begin the fight." / "Not yet.". **Fixed.**

## Phase 1

| Tell | Recorded | Ours before | Status |
|---|---|---|---|
| `verzik_phase1_projectile` 1580 to a player | start 20, angle 20, progress 0, height 100 → 25, **end 20 + 15 × tiles** | start 20, angle 21, progress 192, height 126 → 28, end a flat 110 | fixed |
| the same bolt at a pillar | height 100 → **37**, same timing | height → 52 | fixed |
| when the hit lands | ⌊end / 30⌋ ticks after the cast (95→3, 110→3, 125→4, 140→4, 155→5; 50 of 50) | a flat T+3 | fixed (follows the new end) |
| impact on the player `verzik_phase1_impact` | sent **at the cast**, delay = the bolt's end, height 0 | sent on landing, delay 0 | fixed |
| impact on a pillar `verzik_phase1_impactpillar` | sent at the cast, delay = end (110 / 185 / 275), **height 0** | sent on landing, height 208 | fixed |
| the pillar's lightning as DRAWN | 1582 is a lightning strike (model 35342 at resize 520, seq `verzik_lightning_impact`), x/z ±920, y up to 1738 | garbled: the client merges it into the pillar npc, which is `resizeh=1`, and pre-scales it by 128×128/resize to survive the npc's resize - x128, past the int16 vertex range | fixed (client, `app_entity_sync.c`: when that pre-scale cannot fit, the body takes its own resize and the graphic merges at its own scale; `ev_swing --bounds` prints a graphic's extents). That alone still drew nothing: the merge's bounds cylinder was measured before the resize (radius 32,524), which culled the element, and the pillar has no animation to re-measure it. Now re-measured after the resize (`app_entity_sync.c`); rendered with the client's npc + graphic hotkeys on npc 8379 against 3106 |
| attack sound 3887 (`godwars_saradomin_magic_impact` in our pack) | area, range 5, at each target, delay = end, sent at the cast | none | fixed |
| pillar hit sound | none beyond 3887 | `tob_pillar_hit` synth to the room | fixed (removed) |
| the hit sound when **she** is hit | `tob_verzik_human_defend` 3977 | `tob_verzik_human_hit` 4009 | fixed (`tob.npc`; the Hard and Entry ledgers are taken to match) |
| defend anim `verzik_phase1_defend` | sent at the attacker's cast, delay = their projectile's end (106 for the Dawnbringer at the usual range) | sent when the hit lands, delay 0 (engine combat) | open here: engine-wide, and another session is adding `npc_defend` for it (`docs/rsprox-analysis/combat/defend_timing.md`) |
| Dawnbringer special: defend delay | delay = the special's projectile end | the landing tick count (4) passed as cycles, so the block played at the cast | fixed (`pvm_verzik_special_weapon.rs2`) |
| "The spell strikes to the heart of Verzik Vitur's shield!" | on each Dawnbringer special hit | none | fixed |
| pillar loc `tob_dungeon_verzik_pillar` | rotation **0** | rotation 1 | fixed |

## Phase 2

Every P2 cast (urn bombs, blood, lightning, the Athanatos) is `verzik_phase2_attack_magic` plus an area sound **1514** (range 10) at her. P2's projectiles all start at **cycle 21**.

| Tell | Recorded | Ours before | Status |
|---|---|---|---|
| cast sound 1514 | area, range 10, every P2 cast; delay 10 on the melee | none | fixed |
| urn bomb `verzik_phase2_ranged` 1583 | start 21, angle 12, progress 128, height 70 → 0; **one end time for every bomb of a cast: 30 × ⌊2d/3⌋ for the raider she turns to** (180 of 182 casts where she turned) | start 20, angle 16, progress 64, height 140 → 31, end 56 + 8d per bomb | fixed (she turns to the nearest raider, which is who the recordings show her facing in ~90% of casts) |
| urn bomb landing | `verzik_ranged_impact` + area sound 1021 (range 4) at the tile | graphic only | fixed |
| blood spell `verzik_phase2_bloodproj` 1591 | start 21, angle 8, progress 128, height 70 → 24, **end 30 × ⌊2d/3⌋** (d = tiles) | start 20, angle 16, progress 64, 140 → 31, end 56 + 8d | fixed |
| blood spell cast sound | area 106, delay 21, range 7 | none | fixed |
| blood spell impact | `verzik_phase2_bloodimpact` **on the target player**, at the cast, delay = end | a map graphic at the tile, delay 80/88 | fixed |
| lightning `verzik_phase2_lightning` 1585, the throw | start 21, angle 12, progress 128, height 70 → 24, end 30 × ⌊2d/3⌋ | start 30, angle 0, progress 128, height 48 → 0, end 70 | fixed |
| lightning, each hop | sent when the previous leg lands, start 0, angle 32, progress 0, height 24 → 24 (to her: 24 → 65), end 30 × max(1, ⌊2d/3⌋) | all legs sent at the cast, start 30k + 60 | fixed (same arrival, sent from the cast with the arrival as its delay) |
| lightning cast sound | area 1715, delay 21, range 7 | none | fixed |
| lightning hitting a raider | area 1583 (range 8) on every leg it reaches; area 581 (range 7) with the shock | none | fixed |
| Athanatos throw `verzik_phase2_spawn_armouredtank_proj` 1586 | start 21, angle 0, progress 64, height 70 → 1, **end 90 × ⌊2d/3⌋**; it hatches when it lands (6/9/12/15 ticks) | start 20, angle 16, progress 64, 140 → 31, a flat 6 ticks | fixed |
| Athanatos throw sound | area 205, delay 21, range 7 | none | fixed |
| Athanatos landing | `verzik_phase2_spawning` + `tob_spider_tank_spawn` + area 360 (range 10) + area 684 (range 7) | graphic and anim only | fixed |
| Athanatos heal `verzik_phase2_bloodglobule` 1587 | start 0–5, angle 2, progress 0, height 5 → 70, end ≈ 20–24; a **heal splat** on her and area sound 167 (range 6) at the Athanatos | start 0, angle 7, height 28 → 32, end 30; no splat, no sound | fixed |
| poison globule 1588 | same shape as the heal | as the heal | fixed |
| `verzik_phase2_poisonaura` on the Athanatos | height **64** | height 92 | fixed |
| Matomenos (red) spawn | `verzik_phase2_heal` + `elemental_spawn` + area 684 (range 7, sometimes delay 10) | no sound | fixed |
| hitting any P2 nylocas | synth 4020 `tob_nylocas_hit` | none | fixed (`defend_sound`) |
| melee (stomp / slam) | `verzik_phase2_attack_melee` with **delay 10**; area 1514 (delay 10) and 1021 (delay 25, range 10); area 3201 (range 10) a tick later | delay 0, no sounds | fixed |
| stomp overhead | "There's nothing under there for you!" + game message "Verzik Vitur forces you out from beneath her." | "There's nothing for you there!" | fixed |
| exploding crabs `verzik_nylocas_*` | detonate: `top_spider_*_death_detonate` on the crab, gone 3 ticks later, **no map graphic**; killed: `top_spider_*_death` 2 ticks after the hit, gone 2 later | `top_spider_*_death` + map graphic `tob_nylocas_death_*_detonate` for both | fixed |

## Phase 3

| Tell | Recorded | Ours before | Status |
|---|---|---|---|
| mage auto `verzik_phase3_mageproj` 1594 | **start 53**, angle 20, progress 100, height 100 → 25, end min(53 + 15d, 179); hit ⌈end / 30⌉ | start 20, angle 16, progress 64, 112 → 31, end 56 + 8d; hit ⌊end / 30⌋ | fixed |
| mage impact | `verzik_phase1_impact` on the target at the cast, delay = end | none | fixed |
| ranged auto `verzik_phase3_rangeproj` 1593 | **start 62**, same shape, end min(62 + 15d, 179), hit ⌈end / 30⌉ | as the mage | fixed |
| crabs special | the crabs appear with an ordinary ranged/mage auto; **no `verzik_phase3_attack_summon`** | summon anim | fixed |
| green ball `verzik_acidbomb_projanim` 1598 | start **62**, end 242 (flight 180), angle 20, progress 100, height 100 → 30 | start 30, angle 16, progress 64, 112 → 31 | fixed |
| green ball impact `verzik_acidbomb_impact` | height **100** | 0 | fixed |
| webs `verzik_p3_web_proj` 1601 | start random **0–10**, angle 15, progress 0, height 87 → 0, **end 30 × ⌊d/2⌋** | start 0, angle 14, height 132 → 0, end a flat 120 | fixed |
| web loc `verzik_webspin_loc` | rotation random 0–3 | rotation 0 | fixed |
| caught in a web | "The sticky web grips your ankles." | "You're stuck in the web!" | fixed (the catch; the repeat on a blocked step still says "You're stuck in the web!", unrecorded) |
| a web's damage | "The webs grate against your skin as they snap against your ankles.", no graphic on the raider | "The web explodes and you are released." + `verzik_acidbomb_impact` | fixed (a harmless break keeps the old text and graphic, unrecorded) |
| yellows `verzik_powerblast_projanim_down` 1596 | start **261**, end 405, angle 50, progress 0, height 250 → 30 | start 0, angle 59, progress 255, 186 → 0 | fixed |
| yellows pool | `verzik_powerblast_safezone` on +0 and +2 | +0 and two delayed copies | open: not changed, too few recorded casts (2) |
| "Verzik Vitur is currently immune!" | on attacks 5-9 ticks after a P3 attack and after the yellows; most likely her web special | none | open: our room has no immunity to hang it on, so when she is immune is not established |
| green creeper reaches a raider | `verzik_p3_creeper_spot` height **0**, area 860 (range 6), a **heal splat** on her, "Verzik Vitur saps your health and powers up!" | height 92, no sound, no splat, no message | fixed |
| creeper despawn | no animation | `tob_shadow_projectile_despawn` | fixed |
| "fires a powerful projectile..." | ends `</col>` | no closing tag | fixed |

## The tornadoes

These were read from 83 tornado lives (`tob_verzik_creeper` 8386) in 10
sessions re-transcribed with every raider shown: 1833, 3299, 1764, 1779, 1837,
553, 582, 788, 3285 and 1852 (Hard). 1758 also has tornadoes but was not
re-transcribed. T is the touch tick.

| | Recorded | Ours before | Status |
|---|---|---|---|
| Spawn | one per raider with her "I'm not finished with you just yet!", 2-8 tiles from them | same | match |
| Walk | one tile a tick, diagonals included; the enrage's tornadoes step on the tick after they appear | same | match |
| Touch | it steps onto its raider's tile on T − 1. On T: a damage splat on the raider, spotanim 1602, `sound_area 860` (range 6) at the raider, "Verzik Vitur saps your health and powers up!" to the raider, a heal splat (6) on her, and the tornado is **deleted on T** | deleted on T + 1 | **fixed** |
| Heal | **3 × ⌊hitpoints / 2⌋** of the raider's hitpoints before the touch: 13 of 13 recorder touches (73 → 108, 133 → 198, 31 → 45, 16 → 24) | 3 × the damage | match |
| Damage splat | **⌈0.85 × ⌊hitpoints / 2⌋⌉**, 13 of 13 recorder touches (73 → 31, 133 → 57, 99 → 42) and every teammate's (from their heal) | ⌊hitpoints / 2⌋ | **open**: every session is Leagues V or Grid Master, so the 15% is most likely a seasonal damage reduction. The heal shows the server's own number is ⌊hp / 2⌋ |
| Respawn | the raider's next tornado appears on **T + 16**, 24 of 24 (Normal and Hard) | T + 16 | match |
| Respawn: first step | **one tick later** than the enrage's: its first step is on its add + 2, every respawn | add + 1 | **fixed** (`^tob_verzik_nvar_born`) |

`test/raids/_verzik_tornado.lua` forces it: `::tobvztornado 600` raises the
enrage's tornadoes in P3, and the raider stands still. Now: touch on 69,
deleted on 69, heal 147 (49 × 3), back on 85, first step 87, touched again on 93.

## Getting thrown

The room has three throws. The shared transcripts print only the recorder, who was never under a pillar or under her in P2. So these were read from the nine recordings re-transcribed with every player shown (`PLAYER_INFO_LOCAL_PLAYER_ONLY=false`, recipe in `tools/cox/rsprox_olm_party.py`), with `tools/rsprox_verzik_throws.py`. T is the tick the raider moves.

| Throw | Recorded | Ours before | Status |
|---|---|---|---|
| **P2 slam** (beside her) | her swing 8116 (delay 10) on **T−1**; on T: `human_troll_flyback` 1157, spotanim 245 (height 124), the hit, area sound 3201 (range 10, delay 0) on the tile left, and **one** exactmove (delay1 5, delay2 30) **3 tiles** (3,3 on a diagonal). 57 of 57 | everything on the swing tick (hit included), the 848 stun seq under the 1157, three 1-tile exactmoves (delay1 0), 3201 at her middle with delay 30, the heading from her south-west tile | fixed |
| P2 slam: heading | the eighth of the circle the raider stands in, seen from her middle, counted counterclockwise from east, each eighth holding its first edge. From her middle: (−2,−1) → W, (−1,−2) → SW, (2,−1) → SE, (2,1) → E, (1,2) → NE, (−1,2) → N, (−2,1) → NW. 59 of 59 bounces over 14 tiles | `~coord_direction2` from her SW tile | fixed (`~tob_verzik_bounce_dir`) |
| P2 slam: facing | the heading's cardinal: SW faces south, SE east, NE north, NW west | the heading | matches: the engine sends `direction & 3`, which is exactly that |
| P2 slam: stun | first own step on **T+6** (42 of 55; 7–11 for the rest, none sooner) | 5 ticks from the swing tick: first step T+4 | fixed (6 from T) |
| **P2 stomp** (under her) | the same as the slam, but **5 tiles in 60 cycles** (5,5 diagonally), same heading rule; "There's nothing under there for you!" on T−1. 2 of 2 (one Normal, one Hard) | 2 tiles ([NR]) | fixed |
| P2 stomp: stun | first own step on T+7 (1 sample) | 7 from the swing tick: T+6 | fixed (7 from T) |
| P2 slam damage | 0 (block splat) on most; up to 35 over 18 non-zero | rolled to 45 | open: 18 hits from Leagues/Grid Master worlds do not pin a max |
| **Pillar collapse** | on **T+2** after the collapse (8377 + 8052), whoever is within **2** of the centre (under the 5×5) plays `agilityarena_player_spikedback` **1114**. They are moved **one tile** (exactmove delay1 0, delay2 30) straight out along the longer axis; a corner raider goes to the side counterclockwise of the diagonal (NE → E, NW → N, SW → W, SE → S). The hit lands on T+3. **No graphic, no stun** (every thrown raider could move on T+3), no message. 34 of 34 thrown; 36 within 2 who stepped out by T+2 were not thrown; 6 standing 3 out were not touched | 1157 over 848, spotanim 245, a 5-tick stun, "The pillar collapses on top of you!", a diagonal for a corner, range 3, victims fixed on T | fixed |
| Pillar: facing | the raider's own (511, 2047, 1535: whatever they faced) | the heading | open: the engine cannot read a player's facing back |
| Pillar damage | 26–57 over 30 hits | 32–65 ([NR]) | open: seasonal worlds; two 26s sit under the floor |
| **P3 webs: walk to the middle** | she steps onto raiders on her way (20 steps): **nothing** happens to them, no seq, hit, push or stun. Across all of P3, 53 steps onto a raider did nothing | nothing | matches |
| **P3 webs: arrival** | on the tick the spin 8127 starts (the tick after she reaches the middle), every raider inside her 7×7 plays **1114** and is moved in one exactmove (delay1 0, 30 cycles) onto the **middle tile outside her nearest side**, 4 from her middle: (−1,−3) → (0,−4), (−3,2) → (−4,0), (3,2) → (4,0). No hit, graphic or stun. 3 of 3 inside, none of those 4 out | 1157, four 1-tile exactmoves directly away from her middle by `~coord_direction2` | fixed; a corner raider uses the pillar's rule (unrecorded) |
| P3 webs: facing | the raider's own (512, 1791) | the heading | open, as the pillar |

`test/raids/_verzik_p2_bounce.lua` (17 rows) forces a slam and a stomp: swing on T−1; graphic, hit and one move on T; first step on T+6 / T+7. `test/raids/_verzik_pillar_collapse.lua` (32 rows; 14 fail on the old content) asserts the pillar throw, including a NE and a SE corner and a raider 3 out who is left alone.

## Full inventory pass (2026-10-19)

Every packet kind in the room, over **38 fights in 14 sessions with every raider
shown**: npc and player seqs and graphics, map graphics, projectiles, area
sounds and synths, messages, npc speech, interfaces and their text, client
scripts, vars, locs, music, the camera and drops. Each recorded tell seen in 3
or more fights was checked against our tick logs (`tob_verzik` seeds 1-2,
`tob_verzik_hard` seed 1). The kinds a tick log does not carry (messages,
interfaces, client scripts) were checked against the content. The raiders'
own kit (weapons, spells, potions, relics) is left out.

| | Recorded | Ours before | Status |
|---|---|---|---|
| Entering: the camera | client script 143 `cam_forceangle(280, 1024)` right behind her card (37 of 38) | none | **fixed** (`~tob_title`) |
| An Athanatos bursting | with its poison globule, at the crab's tile: map `shilomist` at height 10, `sound_area smokepuff` (range 6) and `tol_homonculus_boo` (delay 20, range 8); 28 of 38 fights | the globule alone | **fixed** |
| P2 blood spell | rolls to hit. A miss sends `failedspell_impact` at height 96 in place of the blood impact, with the throw (16 of 52 throws even under Protect from Magic) | always the blood impact; no accuracy roll | **fixed** (`~npc_player_hit_roll(magic)` per raider in the splash; a miss deals and heals nothing) |
| A web on Aranea boots | the web lands and the raider is told, spam: "Your Aranea boots let you avoid the spider-based attack." | immune, silently | **fixed** |
| Hard P1 debris | `gg_falling_projectile` 200 → 0 over 120 cycles, angle 0, progress 0; with it `gargboss_exploding_projectile` and `sound_area 100_cauldron_explodes` (range 6), both delayed 90 | start height 250, no burst graphic, no sound | **fixed** |
| Hard P1 debris: the stun | `tob_bloat_stunned` at 124, synth `stunned`, spam "You've been stunned!"; no stun seq on the raider (their block plays) | `human_stunned` + `stunned_thieving` | **fixed** |
| Her death line | none: "Verzik Vitur has fallen." is in none of 32 kills | sent to the room | **fixed** (removed) |
| The completion lines | the wave line's challenge time with " (new personal best)" when it is one; the total time; then **"Your completed Theatre of Blood count is: <col=ff0000>N</col>."**, or "…: Hard Mode count is …" / "…: Entry Mode count is …", each mode its own count | wave and total, then "<col=ffd700>A new personal best!" (never recorded), no count | **fixed**: per-mode counts (`varp7543_tob_completions_hard`, `varp7542_tob_completions_entry`; Normal is the old combined count less Hard) |
| A death | "You have died. Death count: <col=ff0000>N</col>." to the raider, "<col=ff0000>Name</col> has died. Death count: <col=ff0000>N</col>." to the rest of the party. N is the **team's** deaths this raid | the raider's own count, uncoloured, nobody else told | **fixed** (every ToB room) |
| The total time's best | ". Personal best: m:ss" or " (new personal best)" on the total line | neither | **open**: needs a best overall time kept per mode; ours keeps the challenge best only |
| Untradeable drop | "<col=ef1020>Untradeable drop: Verzik vitur - patient record</col>" when her book falls | none | **open**: the loot-notification setting is not implemented anywhere in the tree |
| Pillar blockers | `invisible_type8_blocking` on each pillar's 3x3 while it stands, `_nonblocking` once it collapses (38 of 38) | the pillar npc blocks | open: invisible; matters only to client-side pathing |
| Combat achievement lines | "You have failed Perfect Verzik / Pop It: …" | | not copied here (the achievement system) |

`test/quests/_conformance.lua` reads the new death line; `test/raids/_vzbot.lua`
takes the wave line as her clear.

## Engine and tooling changes this needed

- **Area sounds are one `SOUND_AREA` packet now.** Jagex sends one `sound_area` with a range, heard with the client's distance falloff. Our server sent a `SYNTH_SOUND` to each player in range, at full volume. There is a new command, `sound_area(synth, delay, coord, range)` (opcode 11132), which queues a `SOUND_AREA` zone event. It is encoded with the rev-239 layout (`SoundAreaEncoder` v19, the same field list the client reads), and `[proc,sound_area]` calls it. A revision that cannot carry the packet still gets the per-player synths. The tick log still writes one `area` SOUND row per player in range. A scriptrun of the room sent 864 of them.
- **Music already matched.** The room's tracks go out as `midi_song_v2` with `fadeoutspeed=60, fadeindelay=60`, which is what our `midi_song` sends (`ToriRSServer_SendMidiSong`). The earlier "no fade" note came from our tick log, which records only the track. The recordings' songs with all-zero fades are the death jingle's track 147 and the resume after it, not room music.
- **The P3 bot reads the new flights.** `raid_solve_verzik_p3.lua` hard-coded the old web (+4) and ball (+7) landings. Webs now land at ⌊d/2⌋ ticks (`QD.VZP3.web_flight`) and the ball at +8. With the old numbers the bots took 50-100% more P3 damage, almost all of it from the creeper.
- **The relay plays the real dialogue** (`tob_relay.lua`).
- **The Verzik vault's collection log runs in each raider's own queue** (`tob_vault.rs2`). Run inside the one watchdog script that awards the party, it hit the 500,000-instruction cap in 4-6 of 6 seeds and aborted the rest of the award.
- **The nylocas and P1 defend sounds** went into the combat ledgers (`source = authored`) and into `npc_anims.generated.npc` by hand. `tools/gen_npc_combat.py --write` fails on this tree: it reads `general/configs/npc_default.npc`, which no longer exists.

## Checks

Six scriptrun seeds of `test/raids/tob_verzik.lua` (trio, Normal) pass with every change in, with no aborts. The run takes about 5 s:

```sh
./src/build_<you>_opt/torirsserver --scriptrun test/raids/tob_verzik.lua --bots 3 --name sa \
    --ticks 8000 --session $S/sa --fixture test/raids/fixtures/fresh_lumbridge.ini
python3 tools/rsprox_tells.py --anchor 8369-8375 ~/.rsprox/binary/archive/{3299,3285,788}.txt \
    --ours $S/*/ticklog.tsv
```

`::tobrun` asserts the new P1 flight (3 ticks at 6 tiles, 4 at 7) and the 17-tick throne-to-arena transit.
