# Ratcatchers, from RSProx

Rebuilt 2026-11-22 from the owner's recording of the whole quest, RSProx index 4650 (rev 242.1,
10,521 ticks, recorder LoveTori, stage 0 to 127 in one session; also `--raw` and `--bas`
transcripts). Before this the quest was a Quest Helper + wiki reconstruction: invented stages
(6..14 for the sewer count, 22, 41, 62, ...), invented pages ("Gertrude sent me -- I'm here to
help with the rats."), the cat's catches as a mesbox, no mansion guards (they wandered on the
generated default), no warehouse copy, no smoke scene, no Pied Piper, and an invented journal.
Every page, stage, message, frame and route here is from 4650 unless marked UNVERIFIED. The flute
(the snake charm's interface) was already rebuilt from index 4448 and is kept (below).

Files (OSRS-Content `quests/quest_ratcatchers/`): `ratcatchers.rs2` (every talk: Gertrude, the
sisters, Jimmy, Jack and Pox, the Apothecary, Joe, The Face, Felkrash, Badir), `ratcatchers_mansion.rs2`
(the Directions, the gate, the trellis, the guards' watch and catch, the party rats),
`ratcatchers_cat.rs2` (what a cat's catch counts for, the praise, the tally, the rat pole),
`ratcatchers_scenes.rs2` (the warehouse copy, the King Rat duel, the Keldagrim smoke, the Pied
Piper), `ratcatchers_flute.rs2`, `ratcatchers_journal.rs2`, `configs/ratcatchers.npc` (every
npc's movement), `.constant`, `.varp`, `.spawn`. Shared hooks: `quests/quest_fluffs/scripts/kitten_pet.rs2`
(Chase Vermin rewritten from the recording; op 4 bound for grown and overgrown cats),
`areas/varrock/scripts/gertrude.rs2`, `apothecary.rs2`, `skill_woodcutting/scripts/woodcut.rs2`,
`skill_cooking/scripts/gnome_cooking/gnome_cooking.rs2`; the generated npc blocks lose the 63
lines `ratcatchers.npc` now states. Tool: `tools/rsprox/npc_routes.py` (new).

Test: `test/quests/ratcatchers.lua`, one leg per recorded section, each entered at its stage and
place (`::setvar`, `::tele`): 75/75, 77/77, 76/76 on scriptrun under three run names (the counts
move with how many chases and feeds a roll needs). `_ratcatch_flute.lua` passes. 
`_ratcatchers_refusals.lua` (index 4489/4492, before the quest) passes 40/40: its player now has
combat stats, since the sewer rats beside Trotters attack a combat-3 player and the hit shut his page.

No die-off after the poisoned holes: the recording has only the player's "Those rats should soon die
off." The two warehouse rats that die ([7037], [7056]) are Pox's kills (`cat_attack`, `mouse_death`).

## Stages (varbit 1404 `ratcatch_var` on varp 607, perm, transmitted)

| value | written [tick] | with |
|---|---|---|
| 5 | [324] | "Thank you Gertrude, I'll be sure to have a look..." and "You've started a new quest" |
| 10 | [514] | the sisters: "I suppose so. How will you know that I've completed your task?" |
| 15 | [742] | the eighth rat (the rat-o-meter 1423 to 8, then 0, on one tick) |
| 20 | [775] | "Jimmy Dazzler hangs out in Ardougne." |
| 25 | [947] | "Off I go then..." |
| 30 | [4090] | the first climb up the trellis |
| 35 | [5662] | the sixth party rat |
| 40 | [6155] | down the trellis top, out to Ardougne |
| 45 | [6276] | "He hangs around in a rather unfashionable part of Varrock..." |
| 50 | [6595] | "Report back to me when you're done." |
| 55 | [7036] | the fourth rat hole (ratcatch_ratholes 1405 = 15) |
| 57, 60 | [7158], [7162] | "Can I help? I feel somewhat responsible..." / "That's kind of you" |
| 65, 70 | [7398], [7400] | the Apothecary's ingredients page / the mixing mesbox |
| 75, 80 | [7481], [7509] | "You hand the vial..." / "Don't worry - we're on the case." |
| 85 | [7759] | "The rat is dead!" |
| 90 | [7861] | "Say, if you want to know more about the ratcatching business..." |
| 95 | [8075] | "Just take a simple pot..." |
| 100 | [8311] | the ambush scene, two ticks into it |
| 105 | [8361] | "So where are the Keldagrim rat pits then?" |
| 110 | [8707] | "So why do you want all the rats wiped out at once?" |
| 115 | [8939] | "They charm snakes over in Pollnivneach..." |
| 120 | [9929] | "It's not that difficult. Here, take these." |
| 125 | [10331] | Play, the right tune, in Port Sarim |
| 127 | [10506] | the click after Felkrash's last page |

Other quest vars, all on transmitted varps: rat-o-meter 1423, vc_raton_off1..6 (1424..1429, the six
party rats, each hides its multinpc for the player), ratcatch_guards_caught 14605 (1 for the four
ticks of a catch), ratcatch_ratholes 1405 and its four bits, withdrawfrom_kingrat 1403 (set on
the wall's first warning), catknowsdrill 1410, cathas_expressed 1422. New server-only varps:
9844 the mansion checkpoint, 9845 the cat's lifetime rats, 9846 the Directions' reading tile.

## How the npcs move (`configs/ratcatchers.npc`, fitted with `tools/rsprox/npc_routes.py`)

The ten mansion guards each walk one fixed loop; the left and right halves are mirror images about
x = 2847.5 (x' = 5695 - x). Routes in our patrol units (a stand of g ticks is pause g - 2):

| guard | period | route | on it (ticks seen) |
|---|---|---|---|
| left_front / right_front | 80 | 2840,5074 (14) - 2840,5067 (14) - 2827,5067 (8) - back; mirrored to 2855 / 2868 | 405/434, 320/352: every miss a catch |
| left_mid / right_mid | 40 | x 2840 (2855), 5076 (13) <-> 5084 (13) | 966/995, 384/395 |
| left_back / right_back | 80 | a loop of 2824..2828 x 5094..5100, three stands (18, 14, 18) | 1251/1252, 205/206 (the mirror, fitted on the left) |
| left_fullback / right_fullback | 200 | 2829,5105 .. 2844,5107 and down the west lawn to 2824,5097 | 1238/1444, 283/336 |
| left_inside / right_inside (plane 1) | 240 | six doorway stands of 16..19 ticks | 1298/1302, 1279/1288 |
| chiefguard, staticguard | - | stand at the front door | 19/19 views still |

The fullback's leg from 2825,5097 back to 2829,5102 is a recorded telejump every time (7/7): the
hedge at 2828..2829 x 5095..5100 stands in its straight line and Jagex's patrol jumps after 19
ticks. Ours walks round it in five steps; its stand there is four ticks shorter so the loop keeps
200 ticks. Checked in-game on our tick log: the guards keep their lines (left/right mid on x 2840 /
2855 over 5077..5084, the front pair on 5068..5074, the back and inside loops on theirs).

**The catch** (41 recorded; the wiki: "proximity and line of sight", "backs turned is not enough"):
31 of the 34 attributed catches are on the first tick the player is within 6 tiles (Chebyshev) of
that guard; the other three were first seen beyond it. Ours: every tick in the grounds during the
task, any guard within 6 with a line of sight (`npc_find(.., ^vis_lineofsight)`). The frame, as
all 41 are sent: T the caught varbit, `busy`, the player's `curse_casting`, the guard's "Halt
thief!", his face to the player and `curse_impact`, the curse flying player -> guard, sounds 127 and
126; T+2 `smokepuff_large`; T+3 "You've been spotted and teleported a small distance away." and the
black; T+4 the checkpoint, the black lifted, `busy` 0; T+6 the overlay shut. The checkpoint is the
last reached: the gate 2848,5064, north of the trellis 2846,5111 (after a climb), inside at the foot
of the ladder 2860,5089 (on the house's ground floor). Wire-diffed (below).

The wanderers' radius is the farthest recorded tile from the spawn row: The Face, Bellemorde,
Felkrash, Jimmy, Jack, Joe, the sisters 2; Pox 1; Bones, Ali M and Councillor Halgrive 3; Trotters 4
from his observed spawn; the other party guests 5 (the default). Jimmy was generated
`wanderrange=0` and walks (20 steps in 4 views). Sir Ceril Carnillean had no spawn row anywhere:
added in the middle of his recorded box, radius 3. The King Rat and the warehouse Pox are content's
(spawned into the copy and scripted).

## The sections

| section | recorded | ours |
|---|---|---|
| Gertrude [280..347] | "Hello again." -> offer -> start confirm (No branch [302] and Yes [315]) -> her menu "Do you have any more kittens? / I'll be off" | verbatim; stage 5 + the line with the page after "I don't know which is harder"; her menu reuses her kitten offer |
| The sisters [471..790] | Phingspet on the right page (chat_right with her head), Grimesquit left; the task, the progress talk with the count, the hand-back with the rat pole, the post talk | verbatim; the count is 8 minus the meter |
| Chase Vermin (21 orders) | C order + face; C+1 "Meeeeeoooowww!" cat_attack; C+2 rat "Eek!", rat_hit, the cat's exact move onto the rat at 15 cycles a tile with cat_pounce at that delay; C+3 rat gone / "The rat manages to get away!"; C+7 the praise and kittens_mew | rewritten to that shape (kitten_pet.rs2), op 4 bound; tick log matches (pounce 45 for 3 tiles, 60 for 4) |
| The praise | sewer and warehouse "Hey well done puss, you got it!", the party "Well done, Puss, you got it!" (6/6); the tenth rat "Well done puss! 10 horrible rodents caught!" + cat_rollingover | the same; the tally is a new server varp |
| The rat pole | wielded, every catch offers "Do you want to add a rat to the pole?"; Yes swaps the worn pole up a stage (banner stance from the obj) | the same (`vc_rat_pole` .. `_6`) |
| Jimmy [910..6438] | the map and the spell (his human_castheal with the box); the lost map; the hand-back (stage 45, the manual); his four-row menu and the shared pits talk | verbatim; the pits talk is one proc for Jimmy, Jack and Joe (only the benefits differ) |
| The Directions (24 reads) | refused outside Ardougne's four squares 40..41 x 51..52 (the recorder probed every edge: 2559/2560, 2687/2688, 3263/3264, 3391/3392); inside: black on the answer, the gate C+3, lifted C+4, shut C+6; inside the grounds a page of its own; after stage 40 "I don't want to go there again." | the same; the gate's railing and the trellis top return the player to the reading tile |
| The mansion | trellis up (reach L+2, plane 1 L+3), six party rats, the ladder inside, out by the trellis top at 35; trees refused ("If you cut down this tree the guards will hear you...") | the same |
| Jack [6565..7913] | the task, the poison menu, Pox sick, the anti-poison, the King Rat, his menu | verbatim; Jack's recorded pre-quest refusal (index 4489) kept |
| The warehouse | a fresh copy of square 51,52 at each climb while the task is on: seven rats and Pox (stage 50), the King Rat alone (80); four holes (cheese placed O, scuttling O+2, bit and page O+5) | the same; Pox fights the rats as ambience |
| The King Rat [7577..7764] | the warning, the choice, the cat through the wall, "Be careful in there, cat!", Hisss!/Pfssst!, "Go get him, kitty!"; a round every 4 ticks (rat swing, cat splat, cat swing, rat splat), hits 0 or 1 (cat 10/30 swings, rat 8/30), King 10 hp, cat 6; food through the wall fills the cat; "The rat is dead!", the jingle 123, the purr | the same; the cat's withdraw on low hp is UNVERIFIED (never needed) |
| The Apothecary [7373..7425] | "Talk about something else." -> five rows with "Talk about the Ratcatchers Quest." | the row added to his menu; the old pre-greeting branch removed |
| Joe and the smoke [8052..8395] | his talk; the lit pot on the hole: a lock in a copy, smoke, the rats out of the far hole and back; with the cat out the cat speaks up; the signal (catspeak) and the ambush (stage 100) | the same scene frame for both, copy of square 45,159 |
| Port Sarim [8609..8939], [10091..10504] | The Face (three tricks, the third tells), Felkrash's task, Bellemorde | verbatim |
| Badir [9609..9984] | a coin in the bowl opens the menu; the price is always one coin more than the player carries (19,783 carried -> "19,784", 4/4); walk away slowly -> 100 coins; the beer taken | the same, The Feud's half kept |
| The flute's Play | the right tune at 120 in Port Sarim: no playback, stage 125, walk-with-flute, the scene; in the rat pits "I don't think I should play that tune here."; after 125 "I don't want to play that tune again..." | the same |
| The Pied Piper [10331..10391] | three rats beside the player C+4..C+6, line + black C+7, lock + copy + jingle 147 C+10, camera C+11/C+15/C+24, eight queued rats hop to the pier's end and drown one every 4 ticks, line + black C+56, out C+60 | the same |
| The end [10494..10506] | Felkrash's five pages, 127 on the last click, scroll "4,500 Thieving XP / Access to the Rat Pits", the full pole at 78,128 | the same |

## The journal

Opened at 18 stages (5, 10, 15, 30, 35, 40, 45, 55, 60, 80, 85, 90, 95, 105, 110, 115, 120, 125):
each is copied row for row as sent (`ratcatchers_journal.rs2`, the stage-10 count live). Stages 20,
25, 50, 57, 65, 70, 75, 100 and the completed journal are UNVERIFIED: the stage before, its current
rows struck.

## Wire audit (scriptrun `TORIRS_SCRIPTRUN_WIRELOG`, `wirelog_tells.py`)

| frame | result |
|---|---|
| The catch [2125..2133] | every line matched after two fixes (the caught varbit a tick early; `busy` held to the overlay's close, Jagex drops it with the teleport). Left: Jagex re-sends the area's song (491) on the teleport (engine music) |
| The Directions [1071..1081] | matched after the chat box shut with the black; left: the music unlock a tick late, the menu's `if_setevents` range (the shared chat proc) |
| The Pied Piper [10331..10395] | matched after: the scene a tick later from Play, the four HUD panels Jagex re-opens in the lock and the unlock, the silence song 147, the player's two faces, the closing line two ticks late. Left: `last_quest_id` re-sends, music_len resent unchanged |
| Chase Vermin | tick log: cat_attack C+1, pounce + rat_hit C+2 (15 cycles a tile), kittens_mew C+7 |

## The flute (index 4448, rev 241, a player who had not started the quest)

Our version was a guess: press the eight notes in order, each press counted in
`ratcatch_music_len`, a line for a wrong note. The interface is a page of music: a note is written
to each of eight pages and Play sounds what was written.

`vc_note_current` (1411) is the fingering under the player's fingers. Bits 0..5 are the six holes
(`vc_fingers_current` 1412 and `vc_note_current_finger1..5` are views of the same bits) and bit 6
is the raised octave (`vc_note_current_hi` 1413, `finger6`).

| press | recorded | ours |
|---|---|---|
| the charm's inventory op | [1876], [1885], [1999]: page 0, the fingers and all eight `vc_noteN` cleared; `rc_flute_pageno` "Page 1 of 8"; `rc_flute_prev_layer` hidden, `rc_flute_next_layer` shown; the main modal. Opens at any quest stage. Pressed while open [1888]: shut, then the same | same (was refused outside one quest stage, and needed the music scroll) |
| a note | the fingering and its sound on one tick: D 63 / 1836, E 62 / 1837, F# 60 / 1838, G 56 / 1841, A 48 / 1831, B 32 / 1832, C 24 / 1822, C# 1 / 1821 | same |
| Raise octave | bit 6 set, before the note [2003] or after it [2017]; no sound, no line | same (ours printed two lines of its own) |
| a note with the octave raised | 64 more, and the raised sound: D 127 / 1824 [2004], E 126 / 1825 [2022], G 120 / 1829 [2049] | same |
| C with the octave raised | [1984]: 88 kept, "I just can't hit that high note.", the low C sounds | same |
| Next page | [1945], [1985]: the page's fingering written to its `vc_noteN`, `vc_note_number` up, the fingers take the next page's note, "Page N of 8"; the back arrow shown from page 2, the forward arrow hidden on page 8 | same |
| Previous page | [1941], [2008]: the same going back; the back arrow hidden on page 1 | same |
| a press on the tick after a page turn | refused, a `varp_sync` and nothing else: [1935], [1946], [1950], [1954], [1965], [1979], [2046], [2081] | same: the turn ends in a one-tick delay and the engine refuses a button from a delayed player |
| Play | [1882], [1989], [2029], [2057], [2096]: the shown page's note written; `ratcatch_music_len` = the shown page's number; `ratcatcher_walk_with_flute` (2903); the modal shut; pages 1 to the shown one sound a note a tick from the press's own tick; the animation ends the tick after the last note; a click in between is dropped [1901]. No line | same |
| playback of a raised C (88) or C# (65) | a silent beat: [1995], [2099], [2100] | same |

Raised sounds heard only in playback: F# 124 / 1826 [2031], A 112 / 1819 [2058], [2102],
B 96 / 1820 [2060], [2101].

Checked against our tick log of `_ratcatch_flute.lua`: one note, the animation and 1838 on the
press's tick and the animation's end a tick later; three raised notes, 1824, 1825, 1826 on three
ticks and the end on the fourth, as [2029]..[2032].

**The written notes were never sent.** `vc_note1..4` sit on varp 610 and `vc_note5..8` on 611,
both `transmit=no` in the generated block. Jagex sends them (every turn and Play shows them, and
Quest Helper reads them on the client). They are `transmit=yes, scope=temp` in `ratcatchers.varp`
now.

**The tune** is not recorded. It is Quest Helper RatCharming's stored notes: 63, 56, 62, 60, 127,
32, 1, 48 (D, G, E, F#, raised D, B, C#, A), and the quest's check reads those eight varbits and
the length 8 after the last note.

## Two faults in the test host this found

- **An unsent varp read 0.** `t.var.server` read the client's record of a varp and fell back to
  the server's own copy only when the client could not address the id. The client's array has
  grown past the content-allocated ids, so the King Rat fight's flag (varp 7877, `transmit=no`)
  read 0 through the whole fight. `api_drive.var_content` now answers whether the varp reaches the
  client, in both hosts, and the driver reads the server's copy when it does not: the rule the
  varbit path already had.
- **The headless host could not press an IF1 button.** It sent the numbered IF_BUTTONX for every
  press and refused op 0. Jagex's client and ours send the op-less IF_BUTTON for a static IF1
  button, and content binds it as `[if_button,<com>]`. Op 0 sends IF_BUTTON now, for a component
  the cache says is an IF1 button with a row.

With the fight's variables read from the server the test feeds the cat for the first time (the
cat's hurt read 0 before, so no fish was ever used). A fish pressed on the tick the fight is won
answers "Nothing interesting happens."; the test notes it instead of failing the press.

## Open

- The fullback guards' telejump legs (ours walks round the hedge) and the timing of Jagex's
  stuck jump; a guard that catches resumes its loop on our engine's own phase.
- The catch rule is "within 6 with a line of sight" on our map; the recording's few long stands
  close to a guard without a catch (2472, 2528, 5859) are assumed to be walls or hedges between.
  A recording that probes it (stand still in a guard's path) would settle facing.
- The King Rat: the cat's withdraw on low health, "Go get that nasty rat, don't back down!" and
  losing the fight are UNVERIFIED (the recorded cat never fell below half).
- Chase Vermin's odds outside the sewer task (wiki rates), a kitten's chase, and milestone lines
  other than the tenth rat are UNVERIFIED.
- The smoke scene's failed rats don't run back into the hole (they vanish with the unlock).
- The rat pits themselves (Ardougne, Varrock, Keldagrim, Port Sarim: the gamblers, the pit fights,
  the mousetraps, the viewing walls and their cameras [8773..8826]) are not part of this rebuild.
- The journal at stages 20, 25, 50, 57, 65, 70, 75, 100 and 127 (never opened).
- Gertrude at stages after 5, Badir with a Ring of Charos, the overgrown cat's wily training: no
  recording.
