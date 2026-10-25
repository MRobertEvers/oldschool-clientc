# Demon Slayer, from RSProx

Written 2026-10-25. The quest was a LostCity port (2004 text, "Gypsy Aris", no crystal ball, no
vision, no wardrobe, no case, no copies of the circle, a mesbox for the summoning). It is now
rebuilt from one playthrough:

| session | rev | world | what it shows |
|---|---|---|---|
| index 4375 (the owner, 2026-10-25, "Demon Slayer") | 241.1 | w631, normal | the whole quest: Aris and Wally's vision [32..185], Sir Prysin [214..300], Captain Rovin [328..387], the drain [425..443], the sewer key [532..547], Traiborn [629..725], a key destroyed and washed down again [812..1086], Silverlight [1120..1155], the summoning [1216..1245], out of the copy and back in [1250..1264], the fight and the banishing [1270..1326] |

Branches it does not take use the OSRS wiki (`Transcript:Demon_Slayer`, `Transcript:Aris`,
`Transcript:Sir_Prysin`, `Transcript:Captain_Rovin`, `Transcript:Wizard_Traiborn`, the three
`Silverlight_key_*` pages), with `^chat_*` base moods, marked `[wiki]` in the scripts.

Files: `quests/quest_demon/{scripts/demon_slayer.rs2, scripts/demon_cutscenes.rs2 (new),
scripts/delrith.rs2, configs/quest_demon.constant, configs/quest_demon.npc}`,
`areas/varrock/scripts/{aris,sir_prysin,captain_rovin}.rs2`, `areas/draynor/scripts/traiborn.rs2`,
`npc/scripts/dark_wizard.rs2` + `npc/configs/dark_wizard.npc` (the circle's wizards),
`quests/scripts/questpoints.rs2` (the jingle), engine `npc_setpassthru` (below);
test `test/quests/demon.lua`; `docs/quests/CUTSCENES.tsv` row `demon`.

## The stage ladder (`%varb2561_demonslayer_main`) and the side varbits

| var | 4375 tick | written on |
|---|---|---|
| main 1 | [145] | K5+6 of Wally's vision: with "You've started a new quest: @q_mes@Demon Slayer</col>", the black lifted and Aris's "Delrith will come forth..." |
| delrith_incantation_1..5 | [115] | the continue on Wally's "Now, what was that incantation again?" (a draw per word until unused: _3 and _5 written twice) |
| main 2 | [282] | with Prysin's key menu, after "I gave the other to the wizard Traiborn." |
| delrith_drain_key 0->1 | [434], [995] | the bucket poured with no rusty key owned (2->1 after the destroy) |
| delrith_drain_key 1->2 | [540], [1086] | the rusty key taken |
| delrith_silverlight_case 1 | [1140] | P+15 of the case scene (the case's multiloc empties) |
| delrith_seen_summoning_cutscene 1 / 0 | [1245] / [1317] | the summoning's last continue / W+19 of the banishing (the table's multiloc: broken while it is 1) |
| main 3 | [1319] | the continue on "...back into the dark dimension from which he came." |

The bones count and Traiborn's start are server-only (no var moves at [699], [711]): ours keeps
`varp7818_demon_bones_given` / `varp7821_demon_traiborn_started`.

## Pages and menus

Our run (scriptrun, `build/demon_private/run6/wire.log`) against the 173 recorded pages: every page
the run reaches is sent with the recorded text and the recorded chathead seq, **128 of 128**; the
45 unsent are branches the route does not take (Aris's "How am I meant...", Prysin's first two
answers, the wizard's directions, Traiborn's "too bad" and second talk, the bones in two lots,
the key check with two keys, the second pickup) and the incantation, which is rolled per player.
One page wrapped differently and now carries Jagex's hard break ([58] "...just south<br>of this
city.").

Menus: 38 of the 42 recorded menus are sent identically; the 4 unsent sit on those same untaken
branches. Shapes worth knowing:
- Aris's topic menu ([63], [70], [82], [150], [160], [172], [182]): the first offers three topics
  before the vision and four after it; every later one is all of them less the topic just heard.
- Rovin's reasons ([354], [363], [367], [371]): after Aris or the city the other two, as Jagex
  spells them in the two-line menus ("...destroy the city" and "...give me the key" with no stop);
  after Sir Prysin's name, why he had a key plus the other two again.
- Traiborn's quest topic ([633], [641], [672], [694]): "Talk about Demon Slayer. / Talk about
  something else." after his greeting while the keys are wanted.
- The incantation: five menus "Select a word" ([1274..1295]); each word goes over the player's
  head and on a page with no continue, the next menu three ticks later; the last word ends "!".

## Aris's crystal ball ([38..46])

| R | packets | ours |
|---|---|---|
| [38] the click on "Okay, here you go." | Aris: spotanim 779 start, seq 4614 attaining, face the ball (3203,3425); loc_anim 4609 on the ball; the coin; synth ds_crystal_ball_start (2976) | matched (tick 20) |
| [39], [40], [41] each click | spotanim 780 middle; sound_area ds_crystal_ball_anim (2971) r5 on the player's tile | matched (21..23) |
| [42] | spotanim 781 end, seq 4615 returning, loc_anim 4610, synth ds_crystal_ball_end (2975) | matched (24) |
| [46] | her face back on the player ("It's Delrith!") | the page's own face |

## Wally's vision ([86..145]): a copy of the circle, the player as Wally

S = the continue on "So how did Wally kill Delrith?". Ours: S = 41, ticks below.

| R | packets | ours |
|---|---|---|
| S [87] | the lock (fov_clamp, cutscene_status, ops hidden, minimap_state 2, ...), a blank unclickable box, xp drops and sides shut, highlighting_off, fade 948(0,255,0,0,50), minimap_toggle 2 | matched |
| S+4 [91] | into the copy (raw 11273,15627 = the circle's 3225,3371) | 45 (copy 25,43) |
| S+5 [92] | appearance `npc=qip_ds_wally` (the --bas transcript), Wally the Hero unlocked (musicmulti_22 bit 15, "@mes_hl_red@" line), song 196 at 0/60/60/0, cam_moveto (27,41) 300 232/232, cam_lookat (29,39) 250, shakes (1,0,10,10) (3,0,90,1), rumbling, fade in, Aris narrates | matched (46) |
| K1+1 [103] | teleport to (25,35), two lookats (27,39) 200 2/10 and 100 1/10, shake 3 off, rumbling, "Die, foul demon!" with no continue | matched (48) |
| [104], [105] | a run by (26,37) to (27,39) | matched (49, 50) |
| K1+5 [107] | face (27,41), seq 4603 qip_ds_wally_cutscene, synth ds_wally_sword (2989, delay 10) | matched (52) |
| K1+9 [111] | cam_moveto (27,41) 100 2/10, shakes (1,0,10,5) (3,0,2,50), rumbling, "Now, what was that incantation again?" | matched (56) |
| K2 [115] | the words rolled and said | matched |
| K3 [128] / K3+4 [132] | fade out, chat shut / the fade overlay shut, the glint (778) and seq 4604, shakes off, rumbling, the camera at (25,35) 500, equip_silverlight, song 147 zero, jingle 152 | matched (63) |
| K5+1 [140], K5+3 [142] | a blank box and the black; song 196 zero | matched (68) |
| K5+5 [144] | the unlock, appearance back, seq -1, out to the tile the vision started on, cam_unlock false, cam_reset, sides, minimap 0 | matched (70) |
| K5+6 [145] | stage 1, the line, fade in, the region's track | the track comes at K5+5 with the move (one tick early) |

The fade overlay stays mounted through the talk after the vision; Jagex shuts it when the talk ends
([185], the player walked away from the menu). Ours shuts it on "Okay, thanks..." only; a talk
walked out of leaves it mounted (it is clear, nothing shows).

## Sir Prysin and the case ([1120..1155])

P = the continue on "Excellent! Now I can give you Silverlight." Ours: P = 505.

| R | packets | ours |
|---|---|---|
| P+1 [1126] | the chat shut; he walks south four tiles [1126..1129], his face reset with the first step, **onto the player's tile** | matched (506..509) |
| P+6 [1131], P+7 [1132] | he faces the case (3204,3468); the player walks up behind him, a tile a tick | matched (511, 512) |
| P+7 [1132] | seq 4597 demon_silverlight_open delay 19, synth cupboard_open (58) delay 19 | matched |
| P+8 [1133] | the player faces the case | matched |
| P+12 [1137] | loc_anim 4596 on the case (shape 4, rotation 3), synth casket_open (50) | matched (517) |
| P+15 [1140] | case varbit 1; transformation sir_prysin_silverlight + level 0 + enabled_ops, seq 4598 | matched (520) |
| P+18 [1143], P+19 [1144] | presenting_start + his face on the player; presenting_middle | matched |
| P+20 [1145] | the three keys out, Silverlight in, the glint + receive seq, back to sir_prysin + presenting_sword_end, equip_silverlight, the objbox | matched (525) |
| [1152..1155] | he walks home through the player as he says "Now go kill that demon!" | three steps and a fourth; Jagex's last tile is a telejump ([1155]) |

Our npcs are stopped by players (the owner's CoX/ToA findings); Jagex walks Prysin through the
player both ways (2 of 2). New engine command `npc_setpassthru(boolean)`: the active npc's own steps
ignore entity occupancy (as `moverestrict=passthru` does for a type) while its type keeps its own
`moverestrict=indoors`; cleared by false and at a respawn. Prysin sets it for the walk to the case
and clears it five ticks after the walk home.

## Captain Rovin ([328..387])

Every page and menu as recorded; the key is an objbox "Captain Rovin hands you a key." on the click of
"Okay, I'll give you the key..." ([385]). After the key, the wiki's "Yes, you said before..." with
"Check your pockets" / the bank line; a destroyed key means the whole talk again (the wiki).

## The drain and the sewer key

| R | packets | ours |
|---|---|---|
| search, key still there ([856..863]) | L+1: human_pickupfloor, "That must be the key Sir Prysin dropped." (chathap1), "I don't seem to be able to reach it..." (chatshifty3) | matched |
| search, washed down ([440..443]) | human_pickupfloor, the line "Nothing interesting seems to have been dropped down here today." (a game message) | coded |
| the bucket ([432..436], [994..997]) | "You pour the liquid down the drain.", drain varbit 1, human_pickupfloor, the bucket emptied, ds_drain (2982), ds_key_fall (2983), "OK, I think I've washed the key down into the sewer. I'd better go down and get it!" (chathap2) | matched |
| the key ([540], [1086]) | L+1: drain varbit 2, the key, pick2 (2582), the objbox "You pick up an old rusty key."; no seq | matched |
| the pipe ([547], [934]) | "I can't reach that!" (engine) | engine |

"The key is there" is the quest's keys step with no rusty key owned and none waiting in the sewer:
at [856] the varbit was still 2 from the first pickup, the key destroyed at [831], and the pages came.

## Destroying a key ([812..832])

The cache's confirmdestroy (interface 584, `confirmdestroy_init` 814 with the obj, 1, "Are you sure
you want to destroy this item?" and the key's note); Yes ([831]) empties the slot with
destroy_object (2381) and shuts the box a tick later. The drain's varbit is not touched.

## Traiborn ([629..725])

| R | packets | ours |
|---|---|---|
| bones used on him ([699], [711]) | a messagebox "You give Traiborn N set(s) of bones."; the bones leave on its continue; "That's all of them." / "I still need N more." / "Ok, I'll keep looking." | coded; the gate gives 25 at once ([711]'s path) |
| R [714] (the continue on "Hurrah!") | his seq 4602 + spotanim 777, ds_bone_spell (2973), the verse | matched (451) |
| W [715] | his face on 3112,3161; qip_ds_wizards_key_wardrobe_magic there (rotation 2), ds_cupboard_fall (2979), the chat shut | matched (452) |
| W+2 [717] | human_openchest, chest_open (52) | matched |
| W+4 [719] | loc_anim 4600 qip_ds_wardrobe_disappear, the key, ds_cupboard_dissappear (2978), the objbox | matched |
| W+5 [720] | his face back on the player | matched |
| W+14 [729] | the wardrobe gone | matched (temp loc_add, 14 ticks) |

The spinach roll is an objbox with clock_tick_1 (3120, 3 loops) as sent ([653]).

## The summoning ([1215..1245])

T = the tick the player is first read inside the circle (reached 3225,3375 at [1215], T = [1216]).
Ours: reached 549, T = 550.

The circle is fitted to every recorded tile: inside 3225,3375, 3223,3367, 3222,3372; outside
3223,3377, 3221,3366, 3220,3374, 3220,3365, 3218,3365. West edge x 3222 and north edge z 3375 on
those; the east (3233) and south (3361) edges are not recorded.

| R | packets | ours |
|---|---|---|
| T [1216] | no fade: the lock, Delrith unlocked (bit 18), into a copy at the same tile, the three young wizards and Denath added chanting (seq 4617) each facing the table, the hud taken, song 195, minimap_toggle 2 | matched |
| T+1 [1217] | the player faces the table, cam_moveto (24,48) 475, lookat (27,41) 300, a pan to (31,48) at 1/1, Denath's page | matched |
| [1221] | all four say "Arise, Delrith!"; the page "Dark wizards" | matched |
| D [1223] | the box "The wizards cast an evil spell..." with no continue, loc qip_ds_stone_table_explosion, summon_npc (188), cam lookat 100 / moveto (27,37) 500, ds_table_explosion (2988) | matched (570) |
| D+1 [1224] | loc_anim 4622 | matched |
| D+2 [1225] | four rocks (782) from the table's tiles to the corner stones, 0->30, heights 0; cam_shake (0,15,0,0) | matched |
| D+3 [1226] | four rock_blast map anims (783); shake off | matched |
| D+6 [1229] | Delrith added on the table with seq 4623, loc stone_table_summoning, ds_break_table (2974, delay 10), ds_delrith_appear (2980), cam lookat 400 1/1 | matched (576) |
| D+11 [1234] | loc stone_table_destroyed | matched |
| D+12, D+13 | he steps south twice, his face reset with the first | matched (582, 583) |
| D+14 [1237] | the wizards stop and face him; he faces Denath; "Ha ha ha!..." | matched |
| [1239], [1241], [1243] | the faces as recorded; Denath walks off east and is gone six ticks later [1249] | matched (five tiles, then gone) |
| E [1245] | seen = 1, the unlock, a level 7 wizard's water strike at the player, Delrith's face reset | matched; the cast seq is `human_caststrike` where Jagex sends the rev 234+ `_walkmerge` form (the shared spell table) |

Out of the copy: off the circle's edge is out, onto the same tile in the world, the copy dropped
([1249] -> [1250]; ours 593 -> 594). Back in with the summoning seen, a new copy with the fight's
cast ([1263] -> [1264]; ours 611 -> 612): Delrith at (27,39), wizards at (26,43), (29,43), (26,40),
song 195.

## The fight and the banishing ([1264..1326])

| R | packets | ours |
|---|---|---|
| [1270], [1271] | the swing, demon_block; the splat 7 (he has 7 hitpoints), black_demon_hit (399) | the gate's three swings (0, 4, 3) |
| K+1 [1272] | transformation delrith_weakened + level_change 1 + enabled_ops, his face reset, ds_portal_open (2985), "Now what was that incantation again?" (chatang1) | matched (604; `param=death_script,yes`) |
| each word | menu, the word over the head and on a page with no continue, next menu three ticks on | matched |
| W+3 [1301] | seq 4624 banished, the box "Delrith is sucked into the vortex..." with no continue, ds_delrith_banished (2981, delay 10) | matched (626) |
| W+19 [1317] | seen = 0, "...back into the dark dimension from which he came.", Delrith gone | matched (642) |
| K [1319] | stage 3, out to 3212,3373, fade (15 cycles) | matched |
| K+2 [1321] | "You scurry away from the enraged dark wizards having defeated the demon.", fade in | matched |
| K+3 [1322] | qp, the line, the glint + receive seq, equip_silverlight, song 147 + quest_complete_1 (152), the scroll (Silverlight, "3 Quest Points") | glint at K+3; song + jingle at K+4 with the scroll (the shared scroll proc mounts a tick later) |

## The circle's wizards

The four `qip_ds_young_dark_wizard` types (the world's circle and the copies) were on the generated
`~npc_generic_magicattack`: a punch with no projectile, from one tile. Jagex's cast from range: the
level 7s water strike and confuse ([1245], [1249]: waterstrike_casting / confuse_casting 92, the
`_walkmerge` cast seqs), the level 20s earth strike ([1264], [1319]). They now use
dark_wizard.rs2's spells, with attackrange 5; the four generated bindings and their generated
`attackrange,1` are deleted (an authored binding makes `gen_npc_stats.py` skip them).

## Engine-generic and the recorder's own

The recorder's teleport tablets, glory and necklace, bank trips, stamina potions, the sink's
`human_pickuptable_walkmerge` (ours `human_pickuptable`), door and stair handling, and the earth
strike that rode into the fight copy with the player ([1264], the splat of 1 at [1267]).
`varp_transmit_audit` over 4375 lists only engine trackers.

## Tests

- `test/quests/demon.lua` (scriptrun, `fresh_lumbridge`, a private pack): **132 of 133 rows pass**;
  `quest.journal` fails, as on every quest under scriptrun (no clientscripts build the quest list).
  The route is the recording's: the vision through "Wally doesn't sound like a very heroic name.",
  the drain searched then washed, bones used on Traiborn, the summoning, out of the copy at the
  west edge and back in, the banishing. It keeps a tick log with a mark per leg.
- The camera rows (`cutscene.wallyVision`, `cutscene.summoning`) run on the live client lane only;
  scriptrun has no camera serial, so there they are notes. The camera lines were checked on the
  scriptrun wire log instead: every one in both scenes carries the recorded tile, height and rate.

## Open

- Delrith's own attack: he never swings in the recording (dead in one hit); ours lets him
  retaliate with the generated demon_attack and sound 397 [unverified].
- The wrong incantation ([wiki]): "The vortex collapses. That was the wrong incantation." and
  Delrith whole again. Unrecorded.
- After the banishing the square's own track starts with the move out (K); Jagex sends nothing
  until the jingle. Holding it would bring song 195 back after the jingle (our jingle-end resume).
- The region track after the vision comes a tick early (K5+5 with the move, Jagex K5+6).
- The quest scroll and its jingle a tick after the glint (the shared scroll proc).
- The fade overlay after the vision is shut only by "Okay, thanks..." (Jagex: when the talk ends).
- Prysin's last tile home is a telejump in the recording, a walk here.
- The circle's east and south edges, Rovin's and Traiborn's lost-key talks, Aris's stage 1/2
  talks, Prysin's post-quest price: the wiki's, unrecorded.
- A logout inside a circle copy: the generic backstop (linger 0) puts the player on the respawn
  tile; Jagex's behaviour is unrecorded.
