# A Soul's Bane — RSProx index 4430

Recording: index 4430 (rev 241.1, w369, 2026-10-29, "A Soul's Bane"). The recorder farms
trees and banks first ([0]..[860]), then plays the quest from start to completion
([863]..[3625]). It is the only recording of the quest (`search varbit 2011`: 4430 alone).
Transcripts: default, `--raw` and `--bas` (`~/.rsprox-index-cache/home-*/…/idx4430.txt`).

The quest was built from the OSRS wiki and Quest Helper (2026-10-03, b54/b70) with the rooms
as the public map squares, a kill-counting rage room, no scenes and wiki moods. Everything
below is from the recording; lines no recording reaches are marked *unverified* in the
scripts.

Files: `quests/quest_soulsbane/scripts/soulsbane.rs2` (Launa, rope, re-entry, journal),
`soulsbane_scenes.rs2` (copies, the frame, eight scenes), `soulsbane_anger.rs2`,
`_fear.rs2`, `_confu.rs2`, `_hope.rs2`, `_tolna.rs2`; hooks in
`skill_combat/combat_stats.rs2` (rage/confusion/hopelessness swing) and
`player/logout.rs2` (`~soulsbane_on_logout`). Test `test/quests/soulsbane.lua`.

## 1. Action inventory (`rsprox_actions.py`), a verdict per row

| first tick | action | verdict |
|---|---|---|
| [8] … [805] | Varrock / Falador / Lumbridge teleport tablets (op=2) | generic: `teleport_tablet.rs2`, not re-audited here |
| [17]..[19], [134] | drop grandtree items | generic drop (Grand Tree's own items; 3draster-3c's quest) |
| [28]..[87], [621]..[800] | bank booths, bank tabs, withdraw/deposit, GE clerk + offers | generic bank / GE (GE: 3draster-55's port) |
| [40]..[44] | stats → skill guide | generic interface |
| [109]..[567] | farming tree patches, gardeners, saplings, spirit tree | generic farming (4430's pre-quest half; Farming has its own recordings 4419/4422) |
| [140], [519], [800] | stamina potions | generic drink |
| [195], [273] | quest list → journal overview | generic journal |
| [863] | `fai_varrock_member_gater` | generic double gate |
| [876], [902], [926] | Launa op1 | **this content**, §3 |
| [919] | quest journal close | journal (§9) |
| [923] | rope on `soulbane_falloff1` | **this content**, §4 |
| [931] | `soulbane_falloff2_rope_multi` op1 | **this content**, §4 |
| [970]..[1286] | rack x4, sword/spear wield | **this content**, §5 |
| [978]..[1391] | Attack anger unicorn / bear; prayers; lobster | **this content** §5; prayers/eating generic |
| [1033] | wield rune scimitar | recorder's kit (and the "I can't deal any damage..." test) |
| [1302], [1621], [1708], [2609] | prayer restore doses | generic drink |
| [1439] | `soul_bane_awall_void_exit` | **this content**, §6 |
| [1486]..[1659] | fear holes 1..6 | **this content**, §6 |
| [1500] | Attack fear reaper x53 | **this content**, §6 (fight) |
| [1797], [2280] | kitten: use raw sardine, Interact | generic pet (3draster-55's port) |
| [1869], [1908] | `soulbane_fwall_exit_multi` | **this content**, §6/§7 |
| [1960]..[2288] | Attack confusion beasts (real, fake1..4) | **this content**, §7 |
| [1970] | wield Kharedst's memoirs | recorder's kit |
| [2292] | Smite (refused, level 52) | generic prayer |
| [2534], [2563] | `soulbane_door_multi6` | **this content**, §7/§8 |
| [2607]..[3270] | Attack hopeless creatures | **this content**, §8 |
| [3293] | `soul_bane_hwall_void_exit` | **this content**, §9 |
| [3360]..[3505] | Attack Tolna's heads; quick prayers | **this content**, §10; prayers generic |
| [3586] | Tolna (human) op1 | **this content**, §10 |
| [3609] | surface Tolna op1 | **this content**, §11 |
| [3625] | quest scroll close | §11 |
| [3636] | logout | — |

## 2. Teleports

| tick | from → to | cause | ours |
|---|---|---|---|
| [13]..[805] | tablets, spirit tree | generic | generic |
| [938] | rift → copy (3015,5244) | the rope, scene | `~sb_scene_rage` (lock T+3 after the fade) |
| [962] | scene copy → room copy (3015,5244) | last click of the scene | `~sb_scene_to_room` |
| [1416] | room → Varrock copy (3256,3399) | the rage page's click | `~sb_scene_memory` |
| [1432] | → new rage copy (3028,5230) | end of the memory | the same |
| [1448] / [1478] | → fear scene / fear room (3054,5244) | rage exit | `~sb_scene_fear` |
| [1873] / [1897] | → dig-site copy (3310,3430) / new fear copy (3050,5221) | fear page | `~sb_scene_hole` |
| [1913] / [1954] | → confusion scene / room (3060,5210) | fear exit | `~sb_scene_confusion` |
| [2539] / [2557] | → new confusion copy (3057,5199) twice | confusion page | `~sb_scene_scared` |
| [2570] / [2601] | → hopelessness scene / room (3085,5210) | real door | `~sb_scene_hopeless` |
| [3279] | → bridge room copy (3021,5210) | fifth creature, white fade | `[queue,soulsbane_hope_bridge]` |
| [3302] / [3352] | → Tolna scene / room (2971,5212,1) | bridge-room exit | `~sb_scene_tolna` |
| [3597] | → surface (3309,3452), "You've returned to the entrance." | the reunion's last click, no fade | `[opnpc1,soulbane_tolna]` |

Every room and scene is a NEW private copy (raw tiles 10k–13k; template squares 47_81, 48_81,
46_81 level 1, 50_53, 51_53). Ours: `~sb_copy_enter` / `~sb_t` (each copy one whole square:
a template tile's offset in its square is its offset in the copy).

## 3. Launa and the start ([876]..[907])

Every page verbatim with its mood (`rsprox_pages.py`): hap1, sad1, quiz1, sad4+sad1 split,
... The rev-241 start replaces the wiki's two-option menu: the last of her long page, then
`Start the A Soul's Bane quest?` Yes./No. ([892]); "Yes." → the player's "Would you like
me to go down…" (quiz2), Launa's "You would do that?…" (shock1), and the stage (1) with
"You've started a new quest: @q_mes@A Soul's Bane</col>" on the NEXT page ([896]). Stage 1
talk ([903]..[907]): quiz1/sad1/quiz1/**ang2**/neu1. Fixed: moods, the confirm, the stage
tick; deleted the wiki's "Surely by now they'd both be dead?" branch (not a rev-241 option).

## 4. The rope and the climb ([923]..[965])

| R | ours |
|---|---|
| rope on falloff: L+1 face, rope gone, `riftrope_pres`=1, synth `bane_attach_rope` (1774), **no line** | same; deleted "You tie the rope…" |
| climb: op at L+1, [933] face + `cam_moveto/lookat` rate 100, [934] `climb_down_soul` (3838), [935] fade, [938] lock + copy | `~sb_scene_rage` (was `human_reachforladder` + a plain teleport) |
| [938] song `wrath_and_ruin` (565) + unlock line; [939] silent track + jingle `angry_a_soul_s_bane` (180), camera jump 232 | same |
| [940] Tolna's voice in a mesbox with no continue, the same box with "Click here to continue" at [945]; [947] the second line + synth `bane_fire_loop` x2 | `~sb_mesbox_nocontinue` then `~mesbox`, every camera pan on its tick |
| [959] blank + fade; [962] the room's copy; [963] unlock, stage 2; [965] the anger bar overlay (315), all ten segments hidden | `~sb_scene_to_room` + `~soulsbane_anger_room_start` |

## 5. The room of rage ([962]..[1436])

| rule | R | ours |
|---|---|---|
| rack | op → weapon back (varbit 0) → menu "Which weapon would you like to take?" The Sword/The Spear/The Mace/The Battleaxe/**None** → item + varbit + synth `equip_sword` | same (was: "Nothing", weapon back after the choice) |
| the bar | `anger_damagedealt` += each counted hit ON THE SWING TICK; 10 `if_sethide` per swing, segment N at N×150 (136/184, 283/318, 435/456, 595/618, 720/758, 891/901 either side) | `~soulsbane_anger_add_damage` / `~soulsbane_anger_bar` (was: 8 KILLS) |
| damage | matching anger weapon: 1..49 at a max of 4-5 (49, 12, 43, 31, 1, 48, 16 — never ×10 multiples) | `dealt*10 - random(10)`: a roll over ten times the max |
| wrong weapon | rune scimitar on the bear: "I can't deal any damage..." + block 0 ([1034]) | same |
| XP | none from the hits; 40 Attack at the end ([1406] attack exp 0→40) | same |
| monsters | unicorn (3014,5228), bear (3025,5234), rat, goblin; wander; a new one ~60 ticks after a death ([1006]→[1068], [1068]→[1130]) | `npc_add` in the copy + `npc_setrespawn(60)` |
| the special ([1392]) | say "Arrrgghhh!", spotanim `anger_special_spotanim`, seq `anger_special_play`, bar closed, synth `bane_summon_dragon`; [1401] nine `anger_special_projectile` (6 at floor tiles, 3 at monsters; endtime 125..200 step 5, heights 172/124 raw = 43/31); each monster takes its hitpoints in one splat as it lands; [1406] stage 3 + flame + XP; [1411] page chatshock2 | `[queue,soulsbane_anger_special]` |
| memory ([1413]..[1436]) | Varrock copy, young Launa + boy, six pages each with the line overhead too (Tolna's overhead says "any longer", the page "any more"), boy walks off, "Tolna?", "TOLNA !!!", `watchedcutscene`=1, back in a new rage copy at (3028,5230) | `~sb_scene_memory` |

## 6. The room of fear ([1445]..[1913])

| rule | R | ours |
|---|---|---|
| entry | rage exit: fade [1445], lock [1448] (the anger weapon goes back to the rack), song `fear_and_loathing` (602), jingle `fear_a_soul_s_bane` (182), two timed boxes, [1478] room copy, [1479] stage 4 + page "Hmmm, no light sources…" (neu2, "This can't **be just** a simple dungeon") | `~sb_scene_fear` (the page's word order fixed) |
| holes | `enemydoor` 0..5 = `soul_bane_fwall_void`, `_void2`..`_void6` (0 at [1486], 2 at [1563], 5 at [1659], 1 at [1736], 3 at [1819]) | same |
| a look | L+1 face; L+2 minimap 2 + black `948(0,0,0,0,0)`; +2 song re-sent; +3 a cry; +6 the reaper (if that hole) + overlay `soulbane_waaa` + synth `grim_reaper_appear`; +8 back | `[label,soulsbane_fear_look]` (was: an anim and an immediate spawn) |
| reaper tile | fixed per hole: (3065,5243), (3067,5228), (3063,5221), (3052,5221), —, (3048,5239) | `~soulsbane_fear_reaper_tile` (hole 4 *unverified*) |
| next hole | never the current and never `enemydoor_old` (2027, written from the 2nd kill) | same |
| kill lines | "I don't like looking in these holes but I must!", "I don't feel quite so afraid now.", "These holes aren't that spooky.", "This isn't so scary after all!" on the death tick | same |
| fifth | stage 5, `exitlit` 1, `watchedcutscene` 0; page +3; the boy and the hole: dig-site copy, `soul_bane_hole` loc, the boy walks, `tolna_hole` + loc anim `hole_squeeze` + synth `bane_opening_close`; back at (3050,5221) | `[queue,soulsbane_fear_done]` + `~sb_scene_hole` |

## 7. The room of confusion ([1910]..[2561])

| rule | R | ours |
|---|---|---|
| entry | fear exit (exitlit → 0), song `method_of_madness` (600), jingle `confused_a_soul_s_bane` (181), the room's creepers in the scene copy, [1954] stage 6, no page | `~sb_scene_confusion` |
| illusion | every swing: its own `hitcountN` +1, a 0 block; 3rd "Does this creature really exist?" (once); 8th "That monster wasn't even real!", `confdeath`, gone 2 ticks later | `~soulsbane_confu_fake_hit` (was: the doubt line on every hit) |
| illusions attack | `confrange` + real damage and poison on the player while fighting a fake ([1980]..[1988]) | config (open: the ranged attack, §Open) |
| real death | a false door's `doorNpres`, every count 0, the illusions deleted, "Those other monsters were just hallucinations!", a new five at once on random tiles (x 3050..3065, z 5189..5205) | `~soulsbane_confu_real_dead` |
| fifth | door5 + door6open, `watchedcutscene` 0, stage 7, no line; page +4 "Aha! The exit is now clear!…" (neu2); the boy scared (`tolna_scared`, pages scared2 / shock1) in a new copy, back at (3057,5199) | `[queue,soulsbane_confu_done]` + `~sb_scene_scared` |
| the door | `human_dwarf_recieve`, `soul_bane_con_door6_open1` on its tile, synth `bane_door_open`, fade | `[oploc1,soulbane_door_multi6]` |

## 8. The room of hopelessness ([2567]..[3282])

| rule | R | ours |
|---|---|---|
| entry | song `no_way_out` (594), jingle `hopeless_a_soul_s_bane` (183), [2604] stage 8 + five `monst3` at (3077,5203), (3088,5201), (3076,5197), (3085,5199), (3096,5199), jingle 266 | `~sb_scene_hopeless` / `~soulsbane_hope_room_start` |
| forms | `transformation` monst3→monst2→monst1 of the SAME npc ([2636], [2674], [2737]) | `npc_changetype` + full heal in `[ai_queue3]` (was: death + a new npc) |
| lifeforce | `hope_monmes` counts the player's swings 0..8; "These monsters seem to draw my very lifeforce away!" as it leaves 0 | `~soulsbane_hope_player_hit` (combat_stats hook) |
| kill lines | "Is there any end to killing these monsters?", "This is hard work!", "I've almost killed them all now." (a full stop), "Maybe if I kill one more?" | same |
| fifth | stage 9 + bridge; white fade (16777215) + synth `whiteout`; [3279] a copy of the bridge room (3021,5210); [3280] the page as the white lifts | `[queue,soulsbane_hope_bridge]` |
| creatures' attack | magic: `sound_area hopeless_suck` at the npc, `soulbane_hope_travel` (634, heights 96/124 raw), impact spotanim 635 + the player's block seq on endtime (100 + 5×tiles) | config (open) |

## 9. Brana and Tolna ([3293]..[3356])

Bridge-room exit: fade, lock [3302] in a copy of 46_81 level 1 at (2971,5212,1); Brana and the
three heads; song 565 + jingle `a_monster_a_soul_s_bane` (184). Each Brana page is his
overhead line and the page (shock1 ×4, neu1 after), the next a tick after the click; Tolna's
voice in plain mesboxes; after "...tried so hard to find you!" the player walks to
(2979,5212) under "Lies!" / "Ah, we have a visitor!", says "Your father isn't lying." aloud and
on a page; "Hah!…" then the fight copy: stage 10, `final_seencut` 1, Brana behind at
(2969,5210), multiway on. Fixed: "Sorry **but** your mother and I..." (no comma), "...tried
so hard…" (the wiki dropped the dots), the pages' overheads, the walk, the room copy.

## 10. The heads, Tolna and the reunion ([3354]..[3597])

Heads launch `soulbane_tolna_travel` (640) from their centre (endtime = 95 + 5×tiles: 175/16,
150/11, 145/10), launch spotanim 639 h210, impact 641 + block seq on endtime,
`sound_area creeper_spit` at the head, `fireblast_hit` at the target; melee `neckattack`
adjacent; poison. They never move (config now `defaultmode=none`). The last head plays
`neckdeathfade` + synth `bane_tolna_final_death`, stage 11 on its death; four ticks later
Tolna (2984,5211) wakes (`wake_up`). His talk (moods: quiz1/neu1/quiz2/neu2, Brana's
pages from afar) ends with stage 12, both pres varbits, "You've returned to the entrance.",
the player at (3309,3452) with no fade, multiway off.

## 11. The hand-in ([3609]..[3625])

Moods all neu (neu4+neu1 splits); "At least **whatever** it is" (the wiki's "what ever"). On
the last click: stage 13, the room varbits cleared (`fear_enemydoor`, `confu_door1..5pres`,
both tallies, `tol1..3dead`), `warning_pres` 1, qp, the line, 500 coins, the scroll titled
"You have completed A Soul's Bane!" with the anger sword, "1 Quest Point / Access to Tolna's
Rift / 500 Defence XP / 500 Hitpoints XP / 500 Coins". Deleted the invented "Your backpack
is full…" refusal (coins drop on the floor instead, *unverified*).

## 12. The journal

Opened at stage 1 ([917]) and stage 12 ([3606]): copied row for row — current format, every
finished step a struck paragraph, the current one navy with maroon names. Stages 2..11 strike
the paragraphs stage 12 shows; their current lines and the completed journal are
*unverified*.

## 13. Npc config

Sounds from the wire (`attack_sound`/`defend_sound`/`death_sound`; the generated block's
`null`s deleted): unicorn 876/878/877, bear 297/299/298, rat –/705/704, goblin –/464/463,
reaper 1778/1780/1779, creepers 381/383/382, hopeless –/494/493, heads 1782/1785/1783.
Heads: `defaultmode=none`, `wanderrange=0` (4430: never a step).

## Verification

Scriptrun (`torirsserver --scriptrun test/quests/soulsbane.lua --fixture fresh_lumbridge.ini`,
private script and server packs): **138 of 139 rows**, stage 0 → 13 through every scene; the
one FAIL is `quest.journal`, which fails on every quest under scriptrun (no clientscripts).

Wire audit, our tick log against `rsprox_tells.py 4430 --from 931 --to 3625` by kind and name:
- matched: every scene's jingle (180, 178, 182, 181, 182, 183, 184, 266) and song (565, 602,
  600, 594); `climb_down_soul`; the special's spotanim 630, seq 3804, synth 1781 and nine
  `anger_special_projectile`s (endtime 125..200); `soul_bane_hole` + `hole_squeeze` + synth
  1777; the door's `soul_bane_con_door6_open1` + synth 1775; synths 1774 (rope), 1776 (fire),
  2398 (reaper), 2425 (whiteout), 1773 (the last head); the hopeless creatures' 5 → 1 → 2 → 3
  transformations; the monsters' defend/death sounds; `hopeless_death` 5 times (was 15: the
  form changes played it, fixed); the illusions' `fake_creeper_death` (380, added).
- `varp_transmit_audit.py`: no recorded quest var we fail to send (2027, 2041, 2560 sit on
  the transmitted varp710/712).

## Open

- **Fights not audited per §13/§13b** (the owner rule: one Opus agent per encounter). What
  4430 shows and ours does not do yet: the confusion beasts' AND illusions' ranged attack
  (`confrange` + synth 381) and poison; the hopeless creatures' magic (`hopeless_suck` 495
  area + `soulbane_hope_travel`, endtime 100 + 5×tiles, impact 635) and `lessattack` + 492;
  the heads' projectile (`creeper_spit` 385, launch 639, travel 640, impact 641,
  `fireblast_hit` 156) beside `neckattack` + `bane_tolna_spit` (1784 area, 141 times) and
  poison; accuracy and max hits. Our attacks are still the config's melee.
- The last head plays its config `bane_tolna_death` as well as `bane_tolna_final_death`
  (Jagex: only the latter).
- Engine: an inventory change made in the tick the inventory side panel is closed never
  reaches our client (the rage weapon at the fear lock); ours clears it three ticks early, on
  the fade.
- The completion's jingle is the shared proc's 153; 4430 plays 152 (`quest_complete_1`).
- Rat and goblin attack sounds: none heard in 4430.
- Re-entry mid-quest (rope after stage 2), logout inside a copy, the false doors, the rope
  up, the small voids, Brana's own op, Launa at stages 2..11: *unverified* (no recording).
- Journal rows for stages 1 and 12 belong in `test/quests/_journals.lua` (another session was
  editing it during this port).
- No live-client run yet: the shared script pack's contract gate fails on other sessions'
  edits (The Grand Tree's `check-quest-combat-contract`, a sailing chain in
  `check-npc-script-player-suspend`), so `run.py` cannot rebuild it; the camera rows
  (`t.cutscene.await`) need the live client.
