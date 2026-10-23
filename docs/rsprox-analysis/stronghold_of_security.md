# Stronghold of Security, and Count Check's teleport, from RSProx

Content: `OSRS-Content/osrs-content/server/scripts/areas/area_stronghold/`
(`stronghold.rs2`, `stronghold_npcs.rs2`, `stronghold_questions.rs2`, the
generated question table). Test: `test/quests/strongholdofsecurity.lua`.

## Sessions

| index | rev | what | use |
|---|---|---|---|
| 4346 | 241.1 | the owner's: Count Check (PIN fail, the teleport), every floor end to end, every reward, the plaque, Solztun, the rope out | primary |
| 3258 | 234.1 | Grid Master: Count Check's teleport, all four floors, every reward | counts, rewards, menu ceiling |
| 1707, 1828 | 227.1 | Leagues V: the first floor's gates | greeting, no questions |

Grid Master and Leagues accounts were never asked a question (0 of 45 second
gates in 3258); the owner's account was asked on 20 of 24. Whatever account
state decides that is not on the wire; this server asks every account.

Timing is from the op tick O (the face) unless noted; L is the player's last
step (the skill's timing model).

## Count Check (npc 7414, Lumbridge graveyard)

4346 [178..211]:

- O: the intro, `chathap3`: "Ahahahaha! I am Count Check, the renowned security expert. Would you like me to check your account to see if it is secure?"
- The menu: `Check my account, Count Check!|Can you give me any advice?|Where can I learn more about security?|Who are you?|I'll see you another time.`
- "Check my account" (`chathap1`), no Bank PIN: game line "To set a Bank PIN, talk to a banker and <col=ef1020>ask to check your PIN settings</col>.", then `chatsad2` "You do not have a Bank PIN, so you fail my checks! Talk to a banker to set a PIN before we meet again."; the next click closes it.
- "Where can I learn more about security?" (`chatquiz1`), never visited: `chatquiz3` "The Stronghold of Security, in Barbarian Village. I see you have not visited it. Would you like to? I can send you straight there - but only once." then the menu `Teleport to the Stronghold of Security?` `Yes|No`.
- Yes, on the pick tick S: `human_castteleport` 714, `teleport_casting` 111 at 92, `sound_area teleport_all` (200) range 5 on the player's tile, the menu closed. S+3: the teleport to 3081,3421, `sos_teleported_by_count` (5371) = 1, `cam_reset`, `[sequence] id=-1`. That is `~player_teleport_normal` (3258 [361..364] the same).

Unrecorded branches (advice, Who are you?, the PIN pass and its lamp, the
visited answer, No) are the wiki transcript's (Transcript:Count_Check,
2026-10-23), moods neutral. His lightning on "such fun it is to count" is not
sent: no recording names its spotanim.

## The hole (sos_dung_ent_open)

4346 [801..806]: L+1 face; L+2 the teleport to 1859,5243 and the mesbox "You
squeeze through the hole and find a ladder a few feet down leading into the
Stronghold of Security." on one tick. Standing beside it ([213], [404]): both
on the click.

## Gates

Shape, 4346 48/48 passes and 3258 45/45:

| tick | packets |
|---|---|
| L+1 | `[facing] coord=` the gate |
| L+2 | either a question page, or `[sequence] sos_security_door_drag` (4282) + `synth_sound sos_through_door` (2858) |
| +1 | `[teleport]` one tile, onto the gate's tile from outside or off it from inside; `[facing] type=reset` |
| +2 | `[sequence] sos_security_door_appear` (4283) |

No exactmove and no loc change in any pass. The first pass of all (the first
visit's first gate, 1859,5238) plays the appear with the teleport and the
player's page a tick later: `chatshock4` "Oh my! I just got sucked through that
door... what a weird feeling! Still, I guess I should expect it as these
evidently aren't your average kind of doors.... they talk and look creepy!"
(4346 [236..238], 3258 [375..377]).

The first gate greets until the player is through it: two pages, "Greetings
Adventurer. This place is kept safe by the spirits within the doors. As you
pass through you will be asked questions about security. Hopefully you will
learn much from us." and "Please pass through and begin your adventure, beware
of the various monsters that dwell within." (4346 [221], walked away, greeted
again [234..235]; 3258 [373..374]; 1707; 1828.)

Every gate page is the floor's door npc (`sos_door_war` "Gate of War",
`sos_door_fam` "Rickety Door", `sos_door_pest` "Oozing Barrier",
`sos_door_death` "Portal of Death") with chathead seq `door_chathead` (4281),
46 of 46.

Questions, 4346:

- Only the second gate of a pair: 0 of 24 first gates asked.
- 20 of 24 second gates asked (skipped: 1867/1870, 1874,5207/5204, 1890,5208/5211, 2156,5286/5289).
- A question page, the menu `Select an option` with the options in a fixed order per question (the two giveaway questions differ, both recorded twice), the door's response to the pick, the pass on that page's click.
- Two wrong answers: "How do I set a bank PIN?" -> "Wrong! ..." -> through ([499..503]); "What is an example of a good bank PIN?" -> "This is a bad idea ..." -> a new question on the click ([614..615]), answered right, through.
- Clicking the gate again with a page up starts over with a new question ([242], [940]).

Model: the door asks unless `random(5) = 0`, and after a wrong answer rolls
again (5 skips in 26 rolls). Fitted on one account; label it so.

The question text: 17 strings in the wiki's table differ from what Jagex sent
("huts", "varified", "managment", "WoW!", "provides you with a bond",
"it is fake", ...). `docs/areas/stronghold/sources/rsprox_4346_overrides.tsv`
carries the recorded strings with their ticks and the generator applies them.
One recorded question is on no wiki revision: "Who is it okay to share my
account with?" (`My friends.|Relatives.|Nobody.`, 4346 [1138]); only its right
answer's response was recorded, so it is not in the pool (named gap).

## Portals

4346 [409..412], [674..678], [1001..1004]; 3258: L+1 face; L+2 "You enter the
portal to be whisked through to the treasure room." (spam) and the teleport,
no anim. Refused (combat 8, floor unclaimed, [223..227]): L+2 "You are not of
sufficient experience to take the shortcut through this level." The combat
shortcut levels are the wiki's (no session above them).

## Ladders and ropes

| loc | recorded | shape |
|---|---|---|
| `sos_war_ladd_up` (treasure room) | 4346 [398..402], [818..822] | L+1 face; L+2 `human_reachforladder`; L+3 "You climb up the ladder which seems to twist and wind in all directions." (spam) + teleport to 3081,3421 |
| `sos_*_ladd_down` | 4346 [418..427], [836..843], [853..859], [1009..1014]; 3258 [474..480] | L+1 face; L+2 varbit `cws_warning_4` (3854) +1, clientscript 2524 (-1,-1), `cws_warning_4` (579) as the main modal; on `warn1` "Yes": `human_reachforladder`, the modal closed; next tick "You climb down the ladder to the next level." (spam) + teleport |
| `sos_*_rope_up` | 4346 [663..671], [993..998], [1219..1225]; 3258 [885..891] | L+1 face; L+2 `human_reachforladder`; L+3 "You shin up the rope, squeeze through a passage then climb a ladder."; L+4 the climb-up line + teleport (Famine and Pestilence to the floor's start, Death to the surface) |

The warning's count went 0..5 over 4346; login values of 3, 6 and 7 appear in
other sessions and 7 is "Don't ask me this again" (clientscript
`cws_graphic_swap`). The toggle's layer was never shown in a recording.

Every destination matched the shortest-path maplink rows already in the tree.

## Rewards

| reward | recorded | O | on the continue |
|---|---|---|---|
| Gift of Peace `sos_war_chest` | 4346 [375..383]; 3258 [468..472] | synth `sos_creaks` (1247), mesbox "The box hinges creak and appear to be forming audible words...." | `sos_emote_flap`, 2,000 coins, jingle 157, mesbox "...congratulations adventurer, you have been deemed worthy of this reward. You have also unlocked the Flap emote!" |
| Grain of Plenty `sos_fam_sack` | 4346 [653..661]; 3258 [600..608] | `sos_creaks`, "The grain shifts in the sack, sighing audible words...." | `sos_emote_doh`, 3,000, jingle 179, "... the Slap Head emote!" |
| Box of Health `sos_pest_chest` | 4346 [981..990]; 3258 [703..711] | `sos_creaks`, the box line | `sos_emote_idea`, the restore and "You feel refreshed and renewed.", 5,000, jingle 177, "... the Idea emote!" |
| Cradle of Life `sos_death_pram` | 4346 [1147..1163]; 3258 [797..820] | synth `sos_choir` (1246), mesbox "As your hand touches the cradle, you hear a voice in your head of a million dead adventurers..." | mesbox "... welcome adventurer... you have learnt the value of securing your account, and may claim your prize..."; `objectbox_double` sos_boots + sos_boots2 at 400 "You may claim these boots. They protect your feet equally, but they look very different."; `objectbox_double` sos_boots2 + sos_boots3 "You can return here for more boots at any time, or to get replacements if you lose them."; `skillmulti` mode 23 "Select the boots you want." Fancy/Fighting/Fancier, ceiling 18 / 11 (the free slots); the pick: `sos_emote_stamp`, the boots, jingle 158, `objectbox` at 400 "You claim your prize: Fighting boots<br>You have unlocked the 'Stamp Foot' emote." |

The op fires at L+1 with no arrivedelay (all four, both sessions). Only the
Box of Health restores: the Cradle left prayer at 28 of 43 ([1162]), and the
earlier "every reward restores Hitpoints and Prayer" was this tree's plan doc,
not the wiki. A reopened reward is not recorded anywhere.

## The plaque and Solztun

`sos_thankplayers` (4346 [1166..1175]): at L+1, messagescroll2 ms1..ms12 as
recorded, clientscript 2524, the main modal; its close button closes it.

Solztun (`sos_barb_spirit`, 4346 [1179..1216]): chathead seq `chatneu_idle` on
every page, and `<col=000080>` at the head of every wrapped row, so his pages
are written pre-broken. The intro, the menu `Who are you?|Why are you here?|Bye!`,
each branch verbatim with the player's moods; `sos_brother_found` (5639) = 1
on the player's "Litara" page. The wiki's ghostspeak / Restless Ghost variants
of the player's first line and the "speaking again" answer are unrecorded.
The sceptre imbue is not implemented (it needs a skull sceptre and an
authenticator; no recording).

## Named gaps

- The `busy` varbit (12393) Jagex raises over every held step: this engine
  does not drive it anywhere.
- The Varrock diary task on the first climb down (4346 [427]): no diary here.
- Area music unlocks: the engine's music system, not this content.
- The skip rule's account condition (Grid Master / Leagues never asked).
- "Who is it okay to share my account with?": two of three responses unknown.
- Count Check's lightning; the cws "Don't ask me this again" layer.
- Jingle lengths (`midi_jingle` length field): ours 14667 / 9257 / 8000 /
  10400 ms against Jagex's 16000 / 9080 / 8000 / 9180 for 157 / 179 / 177 / 158.
  The field comes from our own MIDI decode (`tools/gen_jingle_lengths.py`),
  engine-wide; the client ignores it.
- Npc facing across a conversation: Jagex sends Count Check one
  `[facing] player=` for the whole talk (4346 [180]) and Solztun one
  ([1181]); the shared `~chatnpc` proc sends a coord face then the entity face
  on every page (8 rows for Count Check's 4 pages). Shared chat-proc
  behaviour, not this content.
- The player's `[facing] type=reset` with each one-tile teleport is the
  engine's teleport, not checked packet by packet (the trace was run at level 1).

## Wire audit

Ours: `test/quests/strongholdofsecurity.lua`, scriptrun, 2026-10-23, 281/281
PASS; tick log `build/quest_gate/strongholdofsecurity/ticklog.tsv`, packets
with `TORIRSSERVER_TRACE_OUT=1`. Shapes from both sides by
`build/sos_private/tools/sos_shapes.py` (recording vs tick log, L = the last
walking step).

### Count Check

| R (4346) | ours | verdict |
|---|---|---|
| intro `chathap`, menu of 5 | same text, `^chat_happy`, same menu | matched |
| PIN fail: game line, then `chatsad2` page, closes | same | matched |
| offer `chatquiz3`, menu `Teleport to the Stronghold of Security?` Yes/No | same | matched |
| pick S: 714 + 111@92 + sound 200 r5 | tick 20: all three | matched |
| S+3 teleport 3081,3421, `[sequence] -1`, varbit 5371=1 | tick 23 | matched |
| npc `[facing] player=` once | coord + entity per page | gap (shared proc) |

### The hole, ladders, ropes, portals

| R | ours | verdict |
|---|---|---|
| hole walked: teleport + mesbox L+2; standing: on C | standing (37, 1293): on C | matched |
| ladder up: reach L+2, line + teleport L+3 | 35/36 | matched |
| portal: line + teleport L+2, no anim | 31 | matched |
| portal refused: line L+2 | the line, test 2 runs | matched |
| down: L+2 varp 1045, 2524, opensub; Yes: reach + closesub; next tick line + teleport | 332..333, 671..672, 972..973; packets VARP_LARGE, RUNCLIENTSCRIPT, IF_OPENSUB on one tick, IF_CLOSESUB, then MESSAGE_GAME + the move | matched |
| count 0..5 across climbs, persisted | 1, 2, 3 in the run; save holds `1045 = 3072` (3) | matched |
| rope: reach L+2, rope line L+3, ladder line + teleport L+4 | 1290 reach, 1292 teleport | matched |

### Gates

| R | ours | verdict |
|---|---|---|
| silent pass: drag L+2 + synth 2858; teleport +1; appear +2 | every silent pass: drag L+2, teleport +1, appear +2 (77/79, 102/104, ...) | matched |
| first pass: appear with the teleport, player page a tick later | 45 drag, 46 appear (with the teleport), the page | matched |
| question pass: drag on the response's click | drag on the click (59, 163, ...) | matched |
| greeting two pages until through | both pages; varp7663 bit 32 set | matched |
| first leaves never ask (0/24) | 0 of 23 first leaves asked (run 2); 21 questions on the second leaves | matched |
| wrong answer: through or asked again | run 2: "Wrong! ..." then a second question on the click, answered right, through (the [614..626] branch) | matched (the pass-through branch not exercised) |
| 46 door pages `door_chathead` | `^sos_door_chathead` on every page | matched |
| page line breaks | 19 of 19 recorded pages that appear on both sides identical | matched |
| no exactmove, no loc change | none | matched |

### Rewards

| R | ours | verdict |
|---|---|---|
| O: `sos_creaks` + first mesbox (L+1, no arrivedelay) | 322, 661, 964 | matched |
| continue: emote varbit, coins, jingle, second mesbox | 323 / 662 / 965 | matched (jingle length: gap) |
| Box of Health: restore + "You feel refreshed and renewed." | both; all stats at base | matched |
| no restore on 1, 2, 4 | none sent | matched |
| Cradle O: `sos_choir`, two mesboxes, two double boxes at 400, skillmulti mode 23 (3 boots, ceiling = free slots), pick: emote, boots, jingle 158, prize box at 400 | 1233 choir, every page, cells a..c, 1239 jingle | matched |
| reopened chest / second Cradle claim | not recorded; ours a game line / the box without the emote line | unverified |

### Plaque and Solztun

| R | ours | verdict |
|---|---|---|
| plaque: ms1..ms12, 2524, main modal at L+1; close button | same; the test opens and closes it | matched |
| Solztun: every page verbatim, `chatneu_idle`, per-row `<col=000080>`, player moods | same; varbit 5639 = 1 on the Litara page | matched |
| npc `[facing] player=` once | coord + entity per page | gap (shared proc) |

### Monsters

The floors' monsters are cache spawns fought by the engine's combat; this
work did not change them and did not audit their npc info or damage (the
recording's 11 splats are all `swing` / `none` from floor monsters). Not
claimed as audited.
