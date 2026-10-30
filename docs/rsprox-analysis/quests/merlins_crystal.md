# Merlin's Crystal — RSProx index 4433

Recorded 2026-10-30, rev 241.1, world 369, one session end to end (4,820 ticks), recorder
LoveTori (combat 39). It covers the start [498..513], every knight at stages 0, 1, 2/3 and
4, Arhein [934..1003], four crate rides [1155], [1251], [1356], [4386], the Keep, Sir Mordred
and Morgan Le Faye (stage 3: "No" [1455], "Ok then" [1597], the whole of it [1717]; stage 4
[4510]), the beehive [1912..1935], the candle maker four times, the Zamorak altar [2976], the
Lady of the Lake [3069..3105], the jeweller's beggar and Lady [3215..3454], four Thrantax
summons [3575], [4202], [4260], [4582], the crystal [4765] and the completion [4805]. The
journal is opened 39 times, at every stage.

Index 4434 is another player's ("Das Idle") view of the same Thrantax at [4202]: he killed it
twice while the recorder's word menu was up. It gives the death sound and the defend sound.

Before this port the quest was a LostCity port (2004): King Arthur's 2004 pages with no start
confirm, "RuneScape" for "Gielinor", gendered greetings ("Good morrow sirrah!"), the crate ride
as plain boxes with a `p_teleport` into the hold, Morgan added on a fixed tile with the
2004 menus, Mordred's "No" through an npc queue, the bees as a line and 2 damage, a beggar
who remembered "No", a door teleport for the jeweller's, the Thrantax smoke at chest height
with no sound, no despawn, the candle lit with an anim and a line, a 2004 journal, the
completion queued with an extra scroll line, and Mordred and Thrantax on human stances.

**Ours:** `quests/quest_arthur/` (scripts: `sir_mordred.rs2`, `quest_arthur_crate.rs2`,
`thrantax_altar.rs2`, `beggar.rs2`, `merlins_crystal.rs2`, `quest_arthur.rs2`,
`arthur_journal.rs2`; `configs/quest_arthur.npc`, `.constant`),
`areas/area_camelot/scripts/{king_arthur,sir_*}.rs2`, `areas/area_catherby/scripts/
{arhein,candle_maker}.rs2`, `quests/quest_currentaffairs/scripts/currentaffairs.rs2` (Arhein's
greeting), `areas/area_seers/scripts/bees.rs2`, `areas/area_taverly/scripts/lady_of_the_lake.rs2`,
`interface_questjournal/scripts/quest_journal.rs2` (`~quest_journal_rows`),
`skill_firemaking/scripts/firemaking.rs2` (the candle onto the tinderbox).

**Tests:** `test/quests/arthur.lua` (scriptrun) and `test/quests/_journals.lua`
(`journal.merlinscrystal.1..7`, `.4a`, `.4b`). Results in §12.

## 1. Every action in the recording

Quest rows are this port's (§2..§11). Generic rows were audited by one pass over the files
named (verdicts as written against the recording; the ones it changed are compiled and
ran in the quest test only where the test walks through them).

| tick | action | verdict |
|---|---|---|
| 2, 4820 | welcome screen, logout | engine |
| 15.., 241, 519.. | quest list → journal / overview (x39) | **this content** (§10) |
| 87, 3762 | world map close, spellbook filter | generic UI, bound |
| 98.., 232.., 2992 | Varrock, Camelot, Falador tablets | generic: audited 2026-10-29 (fe73b7bd3) |
| 140..165, 2154..2183, 3859.. | GE clerk, offers, collect | generic: audited 2026-10-29 (a72638397) |
| 173..221, 1863..1879, 3120, 3736.., 3905 | bankers, bank booths, Castle Wars chest, tabs, placeholders | generic, bound |
| 243, 3612, 4322 | Camelot courtyard gates | generic: **fixed** (`gates.rs2`): a walk-through, both leaves inviswall + open leaves on the far tiles for 2 ticks, synth 69 |
| 252.., 260.., 276.., 284.., 299.., 310.., 327.., 589.. | the eight knights | **this content** (§2) |
| 271 | kr_cam_doubledoorr | generic: matched (doubledoors.rs2) |
| 290, 367, 553.. | Camelot wooden stairs | generic: matched (maplink rows; face L+1, teleport L+2) |
| 317 | Merlin's workshop door | generic: matched ("The door won't open.") |
| 334 | kr_cam_bookcase | generic: **missing binding, added** (`bookcases.rs2`) |
| 374, 418 | kr_cam_poshdoor | generic: sound **fixed** to nicedoor_open (81) (`door_procs.rs2`) |
| 384.., 419.., 4727.. | Camelot ladders | generic: matched (face L+1, anim L+2, climb L+3) |
| 399, 400..407 | Knight Waves door, squire | generic: **added** (`kr_shared.rs2`): the squire's page and the King's Ransom mesbox; the door gives the mesbox before King's Ransom |
| 433, 437 | Camelot chest open / search | generic: **added** (`chests.rs2`) |
| 497.., 4803 | King Arthur | **this content** (§2, §11) |
| 541, 308, 365, 418, 1836, 2314 | doubledoorr_open, doubledoorl, altarv2, poshdoor_open, oak tree, Ourania chaos altar | clicked and superseded before arrival: nothing answered |
| 572.., 871, 1222, 1353 | stamina sips | generic: matched |
| 890 | The 'Wedge' | generic: **added** (`area_seers/scripts/wedge.rs2`): the greeting and menu; its answers need the diary (open) |
| 923, 1092 | man, woman | generic: matched (citizens.rs2) |
| 931 | Arhein Trade | generic: matched |
| 934.., 987.. | Arhein Talk-to | **this content** (§3) |
| 1007 | Shipwright Sophie | generic: **added** the greeting and the recorded 5-option menu (`boat_selection.rs2`); the boat answers open (no boat UI) |
| 1019..1070, 1038 | Trader Stan's crew, charter map | generic: **fixed** moods and the first menu (`ethicallyacquiredantiquities.rs2`); the map matched |
| 1110 | Hickton | generic: **fixed** (`hickton.rs2`): forced break gone, the cape option, the shop |
| 1134.., 1953.., 3650.., 4361.. | candle maker | **this content** (§6) |
| 1155.., 1251.., 1356.., 4386.. | Arhein's crate | **this content** (§4) |
| 1210, 1302 | Keep back door | generic: matched at [1302]; [1210] faced and was broken off by a tablet |
| 1295, 1299 | Keep crates | generic: **added** ("You search the crate but find nothing.", `crates.rs2`) |
| 1308.., 1312.., 4416.. | Keep stairs | quest's (`quest_arthur_keep_stairs.rs2`): landings matched (2769,3407,1; 2769,3401,2; 2769,3397,1; 2769,3403,0) |
| 1314, 1319 | Keep front door from outside | quest's: matched ("The door is securely locked. You will have to find another way in.") |
| 1310.., 1391 | bats, renegade knights | the Keep's mobs (3 and 5 damage on the recorder) |
| 1394.., 1797 | Protect from Melee | the recorder's |
| 1408.., 1496.., 4432.. | Sir Mordred, Morgan Le Faye | **this content** (§5) |
| 1563.., 1678, 1842, 3781 | prayer potions | generic: **fixed** "You drink some of your restore prayer potion." and spam dose lines (`prayer_potion.rs2`) |
| 1849 | Catherby house door | generic: matched (synth 81) |
| 1854.., 1912.., 1933, 3310.., 3373 | pickups (repellent, bucket, wax, bread) | generic: matched (pickup.rs2) |
| 1883 | leather gloves wear | generic, bound |
| 1898 | fence gate | generic: matched |
| 1908, 1911, 3214, 3349, 3390 | drops | generic: matched (drop.rs2) |
| 1916.., 1922, 1927 | the beehive | **this content** (§6) |
| 2187 | Ourania teleport tablet | generic: **added** the refusal mesbox before Lunar Diplomacy (`teleport_tablet.rs2`) |
| 2207, 3113, 3134 | ring of dueling worn, glory submenu | generic: audited 2026-10-29 |
| 2966 | Zamorak temple door | generic: matched |
| 2971 | Zamorak altar search | **this content** (§7) |
| 3033 | Taverley member gate | generic: **fixed** to the recorded swing (`gates.rs2`) |
| 3046 | farming tree patch check | generic: **fixed** the message type (spam) |
| 3069.., 3258.., 3449.. | the Lady of the Lake | **this content** (§8) |
| 3215.. (x12), 3242.. (x6), 3286.. | jeweller's door, ladder, laddertop | **this content** (§8) |
| 3218.., 3294.., 3321.. | the beggar | **this content** (§8) |
| 3491, 3555, 3556, 3559 | tinderbox / candle, Extinguish | **this content** (§9) |
| 3560, 4460, 4596 | Excalibur / rune scimitar wield | generic equip, bound |
| 3571, 4199, 4256, 4579 | bat bones on the symbol | **this content** (§9) |
| 3752, 3926 | staff of earth with autocast | generic: matched, except varp 4720 autocast_spell_obj never written (open) |
| 3789 | lobster | generic, bound |
| 4764 | Excalibur on the crystal | **this content** (§11) |
| 4808 | quest scroll close | **this content** (§11) |

TELEPORTS: every tablet landing is the 2026-10-29 audit's; Castle Wars and the glory are the
jewellery audit's; the Camelot stairs and ladders and the jeweller's ladder are same-tile
plane changes (matched); the four crate rides are §4; the Keep stairs are listed above.

## 2. King Arthur and the knights (stages 0..4)

Every page of `tools/rsprox/rsprox_pages.py` checked against our scripts mechanically
(text with `<br>` → space, and the mood from `if_setanim`): 103 knight pages.

| R | ours before | verdict |
|---|---|---|
| Kay "Good morrow!", Gawain "Good day to you!" (every stage; the recorder is male) | "Good morrow sirrah!", "Good day to you sir!" | **fixed** |
| Arthur "Welcome to my court. I am King Arthur of the Knights of the Round Table." [498] | "...I am King Arthur." | **fixed** |
| "So what are you doing in Gielinor?" / "...passing the time in Gielinor!" [501..503] | "RuneScape" | **fixed** |
| the start: quiz3 "Really? ... a good quest." + sad4 "Unfortunately, ... his tower." (one page each), then `Start the Merlin's Crystal quest?` Yes/No [510] | 4 pages, no confirm, stage before the player's page | **fixed**; the stage and "You've started a new quest: @q_mes@Merlin's Crystal</col>" go out with "I will see what I can do then." [512]. No: the wiki's two pages (moods unrecorded). recommended_combat 20: the warning mesbox below 20 (the recorder was 39) |
| Gawain: stage 2 with the menu after "She lives in her stronghold..." [687] | same | matched |
| Lancelot: stage 3 with one 4-line page "There are two ways in ... from Catherby." [813] | two pages | **fixed** |
| Pelleas at stage 4: "My best guess would be some sort of spell." [4668] | "using magic" | **fixed** |
| every other knight page (stages 0, 1, 2/3, 4) | — | matched, text and mood |
| forced `|` breaks in 7 knight lines | — | removed (the layout wraps where the client does) |

Stages 5..7 of the knights and the Grail branches are LostCity's (unrecorded).

## 3. Arhein [934..1003]

| R | ours before | verdict |
|---|---|---|
| first time: "Hello there!" / "Hello! Who are you?" / "The name's Arhein. ..." (all happy), `arhein_met` (varbit 18283, base varp 4956, transmit) with the third page | "Hello again!" always, no varbit | **fixed** (`ca_arhein_hello`) |
| menu: "What do you have for sale?|What's with the duck?|Where abouts do you make deliveries to?|I'd best be off." | same (Current Affairs) | matched |
| "Where abouts do you make deliveries to?" (quiz) → "Oh, various places... Port Sarim." (happy) | LostCity "Where do you deliver to?" flow | **fixed** |
| at stage 3: the player's "Do you ever deliver to the fort just down the coast?", "Yes, I do have orders..." (happy3), menu "Can you drop me off on the way down please?|Aren't you worried about supplying evil knights?|I see. Well, I'd best be off." | LostCity's 2-option menu and 2004 answers | **fixed**: "Sorry, but I don't think Sir Mordred would like that. ..." (confused3), "Evil? I know there are plenty of rumours, ..." (happy4), each closed by "Fair enough." |

Test: `talkToArhein-dialog`, `arhein.met`.

## 4. The crate ride (4 rides)

| tick | R | ours |
|---|---|---|
| O (L+1) | face, mesbox "The crate is empty. It's just about big enough to hide inside." | matched |
| | `Would you like to hide inside the crate?` Yes/No | matched |
| C (Yes) | mesbox with no continue "You climb inside the crate and wait.", `human_pickupfloor` delay 10, `fade_overlay` (948) 0,255,0,0,50, hp bar hidden | **fixed** (was a `p_teleport` and plain boxes) |
| C+2 | the lock (fov_clamp, cutscene_status, ops hidden, minimap 2, gravestone hide, areasounds override 25, draw distance), teleport into a fresh copy of the hold (template 2777,9849; raw 10881,8129 / 11841,8321 / 10305,8705 / 12609,13889), cam_shake x5, xp drops + sides closed, 6021, 5485, minimap_toggle 2 | **fixed** (`~arthur_crate_enter`, Misthalin Mystery's frame) |
| C+3 | fade in 0,0,0,255,50, the same box, clickable | **fixed** |
| | "And wait..." x2, the voices (Jagex's `<col=000080>`/`<col=800000>` lines), ..., "You feel the crate being put down." (17 boxes, LostCity had "You wait." first and `@dbl@`/`@dre@`) | **fixed** |
| | `Would you like to get back out of the crate?`; No: "You wait." "And wait..." "And wait..." and the question again [1279..1286] | matched |
| E (Yes) | fade 0,255,0,0,**15**, menu shut | **fixed** |
| E+1 | teleport to 2778,3401 (the jetty) | matched tile, **fixed** tick |
| E+2 | unlock, sides back, cam_unlock false + cam_reset, fade in 50, mesbox "You climb out of the crate." | **fixed** |
| its click | chat and `overlay_atmosphere` shut | **fixed** |

Our wire (scriptrun wirelog, one tick behind the tick log): Yes [166], lock [168], fade-in +
box [169], exit fade 15 [200], unlock [202]: the recorded offsets. The ride back from the Keep
(`merlincrate_empty2`) is unrecorded: LostCity's lines in the recorded frame.

## 5. Sir Mordred and Morgan Le Faye

`rsprox_npcinfo.py` per life: Mordred 3 lives, 3 steps (each toward the player), seqs
`fai_varrock_guard_attack` (6489) x49, `_defend` (6488) x81, `_death` (6490) x1, splats 12/16/43,
headbar 0..30; Morgan 4 lives, added on Mordred's tile facing the player, no seqs.

| R | ours before | verdict |
|---|---|---|
| Talk-to: one page "You DARE to invade MY stronghold?!?!<br>Have at thee knave!!!" (angry), he attacks on its click [1408..1411] | two pages | **fixed** |
| his swing `fai_varrock_guard_attack` + `2h_slash` (2503); block `fai_varrock_guard_defend` + `steel2` (25); death `fai_varrock_guard_death` + `human_death` (512) | `human_sword_slash`/`human_sword_def`/`human_death`, attack and death silent | **fixed** (`quest_arthur.npc`; generated lines deleted from `server_fields.generated.npc`, `combat_stats.generated.npc`, `npc_anims.generated.npc`) |
| stands (3 steps in 3 lives) | `wanderrange=5`: wandered the room | **fixed** (`wanderrange=0`) |
| the hit that empties him [1454]; on the NEXT tick Morgan added on his tile facing the player, smokepuff (86) at 124 there, Mordred's face reset, the player turned to his tile, her page [1455]; no death seq | Morgan on a fixed tile, 2 ticks after Mordred's death seq and death sound | **fixed**: `param=death_script,yes` (zero hitpoints runs `[ai_queue3]` on D+1 as the killer, no engine death). Ours [525] hit, [526] Morgan, no death seq |
| Mordred back to full (6 damage reads 25/30 [1608]) | `npc_statheal` | matched |
| menu "Tell me how to untrap Merlin and I might.|No. He deserves to die.|Ok then." | same | matched; the two player pages are **angry** (ours sad): **fixed** |
| "No": on its click "You kill Mordred.", his death seq, ops off, human_death, chat shut [1466]; both gone and smoke on her tile [1468]; he respawns 24 ticks later [1492] | `npc_queue(4)` | **fixed** (script death, `npc_setrespawn(24)`) |
| "Ok then": "OK then." then the box "Morgan Le Faye vanishes." with her removal and the smoke [1603] | same | matched |
| the explanation, 8 pages; stage 4 with the menu after the last [1760] | same | matched |
| menus: "So where can I find Excalibur?|OK I will do all that.|What are the magic words?" [1760]; after Excalibur "OK, I will go do all that.|What are the magic words?" [1784]; after the words "So where can I find Excalibur?|OK I will go do all that." [1775] | "OK I will go do all that." first; the second menu reversed | **fixed** |
| at stage 4 (4432..4545) the same fight, Morgan and full explanation, stage unchanged | same | matched |
| Mordred's hits on the recorder: 49 swings, all under Protect from Melee, all 0 | — | prayed: matches ours (protection blocks melee); his unprayed max is not in this recording (open) |

The renegade knights (36 lives, `fai_varrock_guard_attack` x3, one 5 on the recorder) and the
bats are the Keep's own mobs: not audited beyond that (open).

## 6. The beehive, the candle maker

| tick | R | ours before | verdict |
|---|---|---|---|
| [1916] no repellent, bucket held | O+1 `human_pickuptable`, `macro_swarm` added beside the player; O+2 it steps on to the player's south tile and stings (`swarm_attack` seq, 819 area r3 + synth 819 @10 + `swarm_hit` 821 @20), 2 damage, "Bees fly out of the hive and sting you!<br>Maybe you can find something to drive them away briefly."; gone O+5 | "Suddenly bees fly out..." + 2 damage | **fixed** |
| [1922] repellent on the hive | objbox (insect_repellent, zoom 400) "You pour insect repellent on the beehive.<br>You see bees leaving the hive.", `macro_swarm_nohunt` on the player's south tile for 3 ticks, 819 @10; repellent kept | mesbox | **fixed** |
| [1927] the wax | objbox (bucket_empty) "You try to get some wax from the beehive..."; on its click the bucket filled in its slot, `human_pickuptable`, 819 @10, objbox (bucket_wax) "You succeed in filling your bucket!<br>Meanwhile, ... wears off." | 3 mesboxes | **fixed** |
| candle maker x4 | every page and mood; "Yes please." is happy | neutral | **fixed** |
| [1998] the swap | the mesbox, then the wax for an unlit black candle on its click; asking again needs "Have you got any black candles?" again [3653] | same | matched |

Our wire: repellent [595] swarm on the south tile + 819 @10, freed [598]; wax [604] anim + 819.
The sting is not walked by the test (it uses the repellent first).

## 7. The Zamorak altar [2971..2977]

Mesbox at L+1 "You find a small inscription at the bottom of the altar. It reads: 'Snarthon
Candtrick Termanto'." (face `sizex=2`): text **fixed** (forced break removed); the words bit is
server-side (no var on the wire). Other stages ("An altar to the evil god Zamorak.") unverified.

## 8. The Lady of the Lake, the beggar, the jeweller's

| R | ours before | verdict |
|---|---|---|
| "Good day to you." | "Good day to you sir." | **fixed** |
| the menu has "I seek the sword Excalibur." from stage 4 while Excalibur is not held; the test pages | same | matched |
| door op from outside with the test started: the beggar added on the tile north of the door (the player's own), facing them, "Please, stranger... my family and I are starving..." (sad) | "Please kind sir..." | **fixed** |
| "No": his last line, then on its click he is gone, the player steps on to the door's tile, and in on the next tick; the next door op plays the whole plea again [3293] | the walk-through as a teleport; "No" remembered | **fixed** (only "Yes" without bread sets the remembered bit, [3314]) |
| the door never swings: `inviswall` in its slot + `inactiveposhdoor` (rot 1) on the tile inside for 2 ticks, `door_open` (62); from its own tile the player steps in, from inside on to its tile (12 recorded crossings) | teleports, no locs, no sound | **fixed** (`~jewellers_walk_through`) |
| the ladder: face L+1, `human_reachforladder` L+2, teleport L+3 with the Lady added and "Hello. I am here for Excalibur. Can I have it yet?" | same | matched |
| the bread: "You give the bread to the beggar." then the bread gone with "Thank you very much!", then him turned into her on his tile with the box, her two pages, Excalibur and her gone on the last click [3428..3441] | "You give bread to the beggar." | **fixed** |
| the beggar's Talk-to (remembered): "Have you got any bread for me yet?" and its two branches | same | matched |

## 9. The candle and the summoning

| tick | R | ours before | verdict |
|---|---|---|---|
| [3491], [4381] tinderbox dragged on the candle | the candle's slot turns lit; no line, anim or sound | anim, `tinderbox_strike`, "You light the black candle." | **fixed** (`[opheldu,unlit_black_candle]`) |
| [3559] candle dragged on the tinderbox | the same + `skill_light_candle` (3226) | as above | **fixed** (firemaking.rs2's branch) |
| [3556] Extinguish | spam "You extinguish the candle.", unlit in the slot | no binding | **added** (`[opheld1,lit_black_candle]`) |
| bat bones dropped on the symbol (S) | busy; a walk to two tiles west of the symbol (S, S+1 from the symbol, none from there); **S+3** Thrantax on the symbol, smokepuff at height **0**, `sound_area` smokepuff (1930) r10, the box (4/4: [3575], [4202], [4260], [4582]); the bones are never used up | smoke at 124, no sound, timing from the walk | **fixed**; ours [687] drop, [690] add + smoke 0 + 1930 r10 |
| the word menu | two orders recorded, rotations of one ring ([3581], [4585]: Snarthanto first; [4207], [4273]: Snarthtrick first) | one order not a rotation | **fixed**; the third rotation unrecorded |
| wrong words | the three pages, GRAAAAAARGH (the player turned to the symbol); on its click the candle gone, Thrantax steps to the player, swings 2 ticks later [3589..3591], [4302..4304] | same | matched; a Thrantax killed by another player while the menu was up is put back on the roar's page with no smoke [4295]: **added** |
| right words | GRAAAAAARGH, the boon, "I wish to free Merlin...", GRAAAAAARGH, stage 5 with "The deed is done..." [4592], Thrantax gone on its click | same | matched |
| Thrantax leaves when the summoner runs: both times on the tick their run step lands 17 tiles away ([3602], [4315]) | never | **added** (player soft timer, `>= 15` on the pre-step tile) |
| Thrantax combat (4434): attack 5540 + `ghost_attack`, defend 5541 + `ghost_hit`, death 5542 + `ghost_death`, 80 hp, swing every 4, death→del 4 ticks | anims/sounds already right (npc_anims.generated); death_delay 2 | **fixed** `death_delay=4` |

## 10. The journal (39 opens)

Every stage's rows are in the recording; they carry Jagex's own stray `<str>` tags mid-row
("free <str>Merlin", "<str><str>becoming") and stage 6 differs from 5 and 7 in two tags. The
2004 journal was re-wrapped text. Ours sends the recorded rows through a new
`~quest_journal_rows` (no re-wrap: our split's colour carry would have coloured a struck row
after a coloured one, stage 4's "<str>I have some bat bones with me"). Stage 4's four item rows
are each struck once the item is in hand (Excalibur worn counts, [4342]).

A headless probe (`::arthurjournal` at each state, scriptrun wirelog) against the recording:
stages 1, 2, 3, 4 (bat bones only, [1989]), 4 (everything, [3517]), 5, 6, 7: **8 of 8 match row
for row, tags included**. `test/quests/_journals.lua` carries all eight
(`journal.merlinscrystal.*`); like every journal row it needs the client lane. The one row no
recording shows: stage 4 without bat bones (LostCity's "Some bat bones.", unverified).

## 11. The crystal and the completion

| R | ours before | verdict |
|---|---|---|
| [4765]: both lines, stage 6, Merlin on the crystal's tile, the crystal deleted, `mine` (2656), his three pages, the box, him gone on its click | no sound | **fixed** |
| [4803..4805]: "I have freed Merlin from his crystal!" / Arthur's page; on its click stage 7, qp, the line, song 147 + jingle 153 (9955 ms), the scroll "6 Quest Points", "Excalibur", Excalibur at 78,128, chat shut | queued; scroll had "Honorary Knight of the Round Table" | **fixed** (direct, on the click); ours [804]/[805] match |

The wiki's failure lines for other items on the crystal are not used (ours keeps "... but you
fail to have any effect on it." and "... but it seems to be protected by a dark force.":
unverified).

## 12. Verification

- Scripts compile (`sscompile` into a private pack); private server pack (`cachepack
  --server-only`), private server `src/build_mc_opt/torirsserver`.
- `test/quests/arthur.lua` on scriptrun (`TORIRSSERVER_DEV=1`): stage 0 → 7, **158 rows
  pass**. The 6 failures:
  - 4 x `*.camelotTeleport.cast` / `varrockTeleport.cast`: the helper reads the tile before
    the landing; every `.landed` row passes (harness).
  - `goUpLadder1Camelot.castleDoorIn`, in 2 of 3 runs: `door_close: shape 18 is not a wall`
    from `[proc,open_double_door_right]` on `kr_cam_doubledoorr` (2758,3503), pressed a second
    time after its 500-tick revert (open, shared door code).
  - `quest.journal`: fails on every quest under scriptrun.
- `t.var.server` reads 0 for the `transmit=no` varps 6434/6436 (the tick log shows every
  write), so the test grades their bits by what they open.
- Wire checks above, section by section, from the run's tick log and wirelog.

## Open

- **Taverley's member gate swings** (RSProx 4433 [3033..3038]: both leaves deleted,
  metalgateopenl/r on the far tiles, `iron_door_open`, the player not moved).
  `[proc,member_gate_swing]` is written in `general_use/scripts/gates.rs2` but not bound: 58
  quest tests cross the gate with the driver's walk-through `cross_gate`, which fails on a
  swing.
- **Camelot's castle double door** aborts on a second press after its revert (above).
- Mordred's unprayed max and the renegade knights' and bats' anims/sounds (the recorder prayed
  through Mordred and never killed the mobs).
- Generic rows the audit left open: the Wedge's answers (no diary system), the shipwright's
  boat answers (no boat UI), varp 4720 `autocast_spell_obj` never written, the Ourania
  tablet's Baba Yaga condition, the squire after King's Ransom.
- Unrecorded and kept as LostCity's/the wiki's: the crate ride back, the knights at stages
  5..7, Arthur's "No." and "Thank you very much.", "I see. Well, I'd best be off.", the
  journal's stage 4 without bat bones, the Lady's 500-coin return, the crystal's failure lines,
  the third word order.
