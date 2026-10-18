# Verzik Vitur (Normal Mode): what the server sends

Read from 28 recorded kills (see [README.md](README.md)) and compared with six scriptrun kills of our room (`test/raids/tob_verzik.lua`, trio). "Tick" offsets are server ticks; "cycles" are client cycles, 30 to a tick. Projectile heights are in our units (raw ÷ 4); spotanim heights are raw.

The **Status** column says what this pass did: **fixed** (our content now sends what is recorded), **open** (a difference that was found and not changed, with the reason).

## The fight's skeleton, tick by tick

Offsets from the death animation that ends each phase. Every row holds in all 28 kills unless a count says otherwise.

| Event | Recorded | Ours before | Status |
|---|---|---|---|
| P1 death anim `verzik_phase1_death` | +0 | +0 | |
| P1 → P2 transition npc appears | **+4**, a new npc `verzik_phase1_to2_transition`; no animation | +3, the P1 npc retyped, playing `verzik_phase2_spawn` | fixed (timing, no anim; still a retype) |
| `verzik_throne_npc` added | +4 | never | open: we draw the empty throne as a loc instead |
| Pillars collapse (`verzik_pillar_collapse`) | +4 | +3 | fixed |
| Transition says "You think you can defeat me?" | +5 | never | fixed |
| Pillars become rubble (`verzik_pillar_fade`) | +8, 4 after the collapse | 2 after the collapse | fixed |
| Rubble removed | 2 after the fade | 2 after the fade | |
| Becomes `verzik_phase2` | +17 | +16 | fixed |
| First P2 attack | +20 | +19 | fixed |
| P2 death anim `verzik_phase2_death` | +0 | +0 | |
| `verzik_phase2_to3_transition` appears, plays `verzik_phase3_spawn` | +2 | +2 | |
| `verzik_phase3` appears and says "Behold my true nature!" | +6 | +6, no line | fixed |
| First P3 attack | +12 (27 of 27) | | |
| P3 death anim `verzik_phase3_death_a` + area sound 4008 (range 15) | +0 | +0, no sound | fixed |
| P3 **transforms** into `verzik_death_bat` (no animation) | +2 (21), +3/+4 (6) | +2, a new bat npc playing `verzik_phase3_death_b` | fixed |
| Throne becomes `tob_dungeon_verzik_throne_transforming` | +8 (21 of 27), no `loc_anim` | +2, with `loc_anim(verzik_throne_transform)` | fixed |

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
| attack sound 3887 (`godwars_saradomin_magic_impact` in our pack) | area, range 5, at each target, delay = end, sent at the cast | none | fixed |
| pillar hit sound | none beyond 3887 | `tob_pillar_hit` synth to the room | fixed (removed) |
| the hit sound when **she** is hit | `tob_verzik_human_defend` 3977 | `tob_verzik_human_hit` 4009 | fixed (`tob.npc`) |
| defend anim `verzik_phase1_defend` | sent at the attacker's cast, delay = their projectile's end (106 for the Dawnbringer at the usual range) | sent when the hit lands, delay 0 (engine combat) | open: engine-wide combat timing, not Verzik's |
| Dawnbringer special: defend delay | delay = the special's projectile end | the landing tick count (4) passed as cycles, so the block played at the cast | fixed (`pvm_verzik_special_weapon.rs2`) |
| "The spell strikes to the heart of Verzik Vitur's shield!" | on each Dawnbringer special hit | none | fixed |
| pillar loc `tob_dungeon_verzik_pillar` | rotation **0** | rotation 1 | fixed |

## Phase 2

Every P2 cast (urn bombs, blood, lightning, the Athanatos) is `verzik_phase2_attack_magic` plus an area sound **1514** (range 10) at her. P2's projectiles all start at **cycle 21**.

| Tell | Recorded | Ours before | Status |
|---|---|---|---|
| cast sound 1514 | area, range 10, every P2 cast; delay 10 on the melee | none | fixed |
| urn bomb `verzik_phase2_ranged` 1583 | start 21, angle 12, progress 128, height 70 → 0; one end time shared by every bomb of a cast | start 20, angle 16, progress 64, height 140 → 31 | fixed (shape); end time **open**: no rule fits more than ~75% of casts, so ours is kept |
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
| caught in a web | "The sticky web grips your ankles." | "You're stuck in the web!" | fixed |
| a web's damage | "The webs grate against your skin as they snap against your ankles." | "The web explodes and you are released." | fixed |
| yellows `verzik_powerblast_projanim_down` 1596 | start **261**, end 405, angle 50, progress 0, height 250 → 30 | start 0, angle 59, progress 255, 186 → 0 | fixed |
| yellows pool | `verzik_powerblast_safezone` on +0 and +2 | +0 and two delayed copies | open: not changed, too few recorded casts (2) |
| hitting her during the yellows | "Verzik Vitur is currently immune!" | none | fixed |
| green creeper reaches a raider | `verzik_p3_creeper_spot` height **0**, area 860 (range 6), a **heal splat** on her, "Verzik Vitur saps your health and powers up!" | height 92, no sound, no splat, no message | fixed |
| creeper despawn | no animation | `tob_shadow_projectile_despawn` | fixed |
| "fires a powerful projectile..." | ends `</col>` | no closing tag | fixed |

## Also differs, not changed

- **Music fades.** The server sends `midi_song_v2` with `fadeoutspeed=60, fadeindelay=60` on the room tracks (566 / 572). Ours sends the track with no fade; that is the engine's music packet.
- **Area sounds as packets.** Jagex sends one `sound_area` with a range; our server sends `SYNTH_SOUND` to each player in range (`~sound_area`). The same sounds are heard, without distance falloff.
- **Separate transition npcs.** Jagex despawns P1 and adds `verzik_phase1_to2_transition` (likewise P2 → P3 → `verzik_phase3`). Ours retypes one npc through all forms. The timing and what is drawn now match; the client sees one npc index instead of three.
- **P2 urn bomb end time.** One end per cast (60–180), shared by every bomb. The nearest, farthest and first target each explain about 75% of 392 casts, so ours (56 + 8 × distance) is kept.
