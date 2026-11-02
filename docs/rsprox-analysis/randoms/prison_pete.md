# Evil Bob / Prison Pete (ScapeRune prison) random event

Content: `OSRS-Content/osrs-content/server/scripts/macro_prison_pete/` (configs: constant, varp, npc;
scripts: `macro_prison_pete.rs2`, `macro_prison_pete_cheat.rs2`). Test: `test/randoms/prison_pete.lua`
(`::prisonpete` starts it). Nothing decides WHEN the event happens (same as Rick Turpentine / Quiz
Master: no scheduler in the tree; `~macro_bob_start` is the entry).

Sources: index 4462 (rev 241, owner, Ghosts Ahoy; ticks 3500..3661, wearing the bedsheet), index 4043
(rev 237, Leagues VI; 5000..5200). Index 554 (rev 227) and 1529/1694/1702/1738/3208 list npc 368 but
none has a Prison Pete window (554 has no `macro_evil_bob`/`prisonpete` name at all). Wiki:
Prison_Pete and Transcript:Prison_Pete (fetched `?action=raw`). Two recordings = two rolls, both with
three right keys; no wrong key, no leave.

## Timeline (4462, S = 3500)

| tick | R (recording) | ours | verdict |
|---|---|---|---|
| S | `macro_evil_bob_prison` 6754 added at player+(-1,+1), spawnangle south; follows (4043 same offset) | `npc_add` at (-1,+1), playerfollow | matched |
| S+1 [3501] | face player, say "Mew!", map_anim smokepuff 86 h124 on the ADD tile, sound_area 1930 r7, synth_sound 333 | same (`cat_attack` = 333) | matched |
| S+10/20/30 [3510/3520/3530] | type re-sent (transformation, level_change, enabled_ops); say (4462 Mew!/Meeeeow!, 4043 Meeeow!/Meeeeow!/Meeeeow!/Meeeow!/Meeeeow!) + synth 333; at S+30 re-send only (talking) | `npc_changetype` every 10, say random of the three spellings, synth; re-send only while talking | matched; spellings pooled (3 seen) |
| [3526] | op on Bob: page "Do you wanna meet my pet? His name's Pete.<br>Or has this amulet of humanspeak broken again?" chatquiz2 (forced break kept) | `~chatnpc_anim(^chat_quiz, "...Pete.\|Or has...")` | matched |
| [3528] | menu "Follow the scary cat?": "Yes, that seems like a good idea." / "No, leave the cat alone." | `~p_choice2_header` | matched |
| answer R (3538) | spotanim teleport_casting 111 h92, synth 201, menu closed; NO player anim in 4462 (4043 rev 237: human_castteleport 714 + sound_areas 2396/2384) | spotanim + `teleport_reverse` (201), no anim, no sound_area | matched to rev 241 |
| R+2 [3540] | Bob del; empty messagebox (script1508 "") | Bob `npc_del`, `~prison_blank_box` | matched |
| R+3 [3541] | map_anim smokepuff on Bob's tile + sound_area 1930 r7; fade_overlay (0,255,0,0,50); hpbar hidden + cc_deleteall | same | matched |
| R+6 [3544] | varbits 995 / 1549 / 3926 (421 / 21 / 6639; 4043 2420 / 22 / 3982), message " <br>Welcome to ScapeRune.", teleport to (2093,4467,0), seq -1, cam_reset, panels re-sent, minimap_toggle 0 | three varbit writes (random(4096) / 1..24 / random(8192)), same message, `p_telejump`, `anim(null)`, cam_reset, `~prison_sides`, `minimap_toggle(0)` | matched; one varp packet in R vs three varbit packets (Open) |
| A+1 [3545] | fade_overlay (0,0,0,255,50) | same | matched |
| A+4 [3548] | player faces Pete's tile, Pete faces player's; midi_song_v2 511 (0,60,60,0); messagebox closed, script1508 "0" | same | matched |
| A+5 [3549] | Pete pages (see below) | same | matched |
| "Okay." click [3560] | busy 0, seq -1, fade closed, song 511 again, `toplevel_sidebutton_switch` 3, side6 (magic) closed | `~macro_prison_release` | matched |
| lever op | L+1 face, [first pull] Pete's page at L+3, pull on its click: human_boxlever 798, loc_anim balloondrop_lever 450 (shape 10 rot 3), synth 2400; +2 `if_setmodel prisonpete:balloon1` + toplevel_mainmodal_open(-1,-1) + modal `prisonpete` 273; +2 closed, busy 0. Later pulls: anim at L+2 | same chain, `p_arrivedelay`/`p_delay` | matched (4462 x3, 4043 x3) |
| balloon op O (L+1) | busy+face; O+1 human_stamp 794; O+2 map_anim balloon_spot 524 (no height) on its tile + sound_area 2384 r10; O+3 human_pickupfloor 827, key `prisonpete_key` 6966 in pack, synth 2582 delay 30, balloon telejumps to a room tile, Pete's page | same; balloon `npc_tele` to a random walkable room tile | matched |
| key to Pete | pop page click -> server walks the player to Pete (`opnpc1` on him) -> "Ooh, thanks! I'll see if it's the right one..."; click: key removed, face the door (2097,4466); +1 human_pickpocket 881 delay 15; +3 synth 2402 + macro_transmitted +1 + page ("Hooray..." / on the third "You did it..." then player "Let's get out of here before that cat notices.") | `p_opnpc(1)` from the pop script; `[opnpc1,prisonpete_pete]` | matched; the walk-to-Pete tick is the engine's (Open) |
| pop before pull | Pete: "You're meant to pull the lever to find out which sort of animal to pop!" (chatang2), 4043 [5119]; after a right key the shape is empty again | shape var 0 after each key | matched |
| exit hallway [3643] | spam "You quickly escape the prison with Pete.", song 147 (0,0,0,0), jingle 150 (9,230 ms); +1 busy + fade out; +7 [3650] back | timer fires on x>=2096 with 3 keys; queue does the same chain | matched (area fitted to 2096,4466 in both recordings) |
| return T [3650] | macro_last 3->6 (7->6 in 4043), macro_transmitted 3->0, teleport to the tile Bob took the player from, Pete (368) added at (-2,+2) facing it, bedsheet back on (worn slot 0), panels, fade in | same | matched |
| T+3 [3653] | Pete emote_wave 863 + page "Thanks a lot for your help!<br>Here, have a present:" | same | matched |
| click [3657] | spam "Your reward is: 6 x Grimy toadflax." (noted), Pete map_anim smokepuff_large 188 + del, fade closed, player "Thanks! See you around!" | `~prison_reward`, same order | matched |
| click [3661] | magic tab back, region song | magic tab back; region song left to the engine | partly (Open) |

## Pages (index 4462 unless noted)

All verbatim, with their recorded moods; forced breaks (`|`) only where Jagex breaks early.
Bob: chatquiz2 (above). Pete: "So you've been captured too?" sad; player "Captured?" quiz; "Yes, Evil Bob
captured me and stuck me in this prison." sad; player "How do I get out of here?|I can't be held
captive by a cat!" angry; "Some of these balloon animals have keys in them, and if you pull the big
lever it tells you which shape animal contains the correct key, but I can never find it." sad3;
"You need to pull the lever to find out which shape animal contains the key, then pop that sort of
animal to get the key." sad3; "Bring me any keys you get and|I'll try them on the doors." sad2;
menu "Select an option": "Okay." / "But I want to leave now."; player "Okay.". "That's right, pull
it. Then remember what kind of animal shape you see. That's the animal that contains the right key."
happy3; "Great, now you've got a key!|Bring it to me so I can try it on the doors." happy2; "Ooh,
thanks! I'll see if it's the right one..." happy1; "Hooray, you got the right one! Now pull the lever
again and let's get the next portal unlocked!" happy2; "You did it, you got all the keys right!|Thank
you! You're my friend FOREVER!" happy2; "Let's get out of here before that cat notices." happy;
"Come on, we should leave before Evil Bob comes back!|You go first; I'll follow along." happy2 (4462
[3639], recorded on a click of Pete after the third key); "Thanks a lot for your help!|Here, have a
present:" happy2; "Thanks! See you around!" happy. The recorder's name never appears in a page.
(4462 [3524] "Would you sign this petition form, please?" is the Ghosts Ahoy dialogue Bob's click
interrupted: not this event.)

## The balloons and the shapes

* `prisonpete_multinpcs` (varb 1549, bits 12..16 of `varp531_macro_transmit`) is a permutation index:
  v = 1..24, and v-1 is the lexicographic index of the permutation of shapes 1234 over
  balloon1..balloon4 (cache multinpc lists; 25 and 0 are hidden). 4462 v=21 = (4,2,1,3), 4043 v=22 =
  (4,2,3,1). Verified against every pop in both recordings (6/6: the popped balloon's shape equals the
  shape the lever's model shows, 11028 = shape 1, 11034 = shape 4).
* The shape shown each pull is random per pull: 4462 showed shape 1 three times (balloon3), 4043 showed
  4, 1, 4. Ours: `random(4)+1` per pull (4 pulls in 2 recordings: no sign of a rule).
* `prisonpete_ignorebits_a/_b` (995 12 bits, 3926 15 bits) and the drill-demon / pirate-combination-lock
  views (1335.., 9585..) are the other random events' bits in the same varp: the arrival fills them
  with random values (R: 421/6639 and 2420/3982). Ours fills a with random(4096) and b with random(8192)
  (b's range UNVERIFIED: both samples are below 8192).
* `varp531_macro_transmit` was `scope=temp, transmit=no` in the generated block; the three lines moved
  to `macro_prison_pete.varp` (generated file edited, listed below).
* The doors/energy barrier are client multiloc state on macro_transmitted (1547, already transmit=yes):
  no loc packets in either recording.

## R / ours / verdict summary

| item | verdict |
|---|---|
| Bob arrival, reminders, says, synth, talk, menu | matched (leave time UNVERIFIED) |
| teleport in, panels, music, fades, bedsheet to the pack | matched; panel list partial |
| Pete intro pages and menu | matched |
| lever, shape interface, balloon pop, key, door verdict, three keys | matched |
| exit trigger, escape line, jingle, fade, return, Pete's present | matched |
| present table | wiki's Members table (lamp `macro_genilamp` UNVERIFIED which lamp); 4462 toadflax x6 and 4043 diamond x3 are on it |

## UNVERIFIED (no recording; source in brackets)

* Wrong key: Pete's "Aww, that was the wrong key! You must have popped the wrong sort of animal. Try the big lever again - it'll tell you which animal to pop." [wiki transcript], jingle `wrong_key_prison_pete` (149) [wiki: plays on a wrong key], mood sad, shape cleared.
* "But I want to leave now." -> "Oh. Well I guess you can leave but I'll still be stuck here." -> "Leave the event?" Yes/No -> "You leave the prison." and home without Pete [wiki].
* Talking to Pete again with no key (menu What is this place? / How do I get out of here?) [wiki]; holding a key and pulling/popping ("Hey, bring me that key you've got! I wanna get out of here!") [wiki]; using a non-key item ("No thanks, I don't want that just now.") [wiki]; "Don't mind me, I was just leaving..." [wiki].
* Another player's Bob (silent), Dismiss (op5 leaves), the No answer (talk just ends), Bob leaving unanswered (S+100 with a smoke puff), the lever outside the event ("Nothing interesting happens.").
* Shape models 2 and 3 (11031 / 11032): the two other unnamed cache models, order guessed.
* Full pack at the present: dropped at the feet.

## Open

1. Balloon movement: the generated default (`wander`, range 5) is left; the recordings show balloons stepping almost every tick over the whole room. Run `npc_movement_audit.py paths` on 4462/4043 vs a tick log.
2. The `p_opnpc` walk to Pete after the pop page: the recorded "Ooh, thanks!" tick (same tick as the click when adjacent, 3581; on arrival, 3608) was not diffed against ours.
3. Full wire audit (npc-info blocks per tick, `TORIRSSERVER_TRACE_OUT=2` packets, side-panel `if_setevents`, the `[bas]` ghost-disguise stance on bedsheet) not done: only the scriptrun pass below. The test does not wear the bedsheet, so the sheet in/out is untested.
4. Arrival writes three varbits (3 varp packets); the recording shows one varp. `wholewrite=deny` blocks a whole-varp write without an exemption.
5. Region song after the present ([3659] song 511 with no fade, [3661] song 355) is left to the engine.
6. Death inside, spell casting ("You didn't come here to do that!"), item drops in the exit, logout resetting progress, and the free-to-play table are wiki-only and unbuilt.
7. Leagues task message / notification (4043 only) is the seasonal lane's.

## Files touched outside the new directory

* `OSRS-Content/osrs-content/server/scripts/_explicit/configs/server_fields.generated.varp`: removed `scope=temp`, `transmit=no`, `protect=no` from `[varp531_macro_transmit]`.
* `OSRS-Content/osrs-content/server/scripts/_explicit/configs/server_fields.generated.npc`: removed `defaultmode=wander` and `wanderrange=5` from `[prisonpete_pete]` (now in `macro_prison_pete.npc`).
* `OSRS-Content/osrs-content/pack/varp.alloc`: `ss_allocate` added 9509..9512.

## Test

`test/randoms/prison_pete.lua`, private build and pack: `SCRIPTRUN PASS: 181 ticks in 482 ms`, `rows 37 pass 37 fail 0`.
