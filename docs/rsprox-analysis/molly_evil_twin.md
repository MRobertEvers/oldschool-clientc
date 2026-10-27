# The Evil twin random event (Molly) vs RSProx

Recordings (all four end in a catch):

| index | rev | Molly | notes |
|---|---|---|---|
| 4394 | 241.1 | two_red | [1000]..[1125], full; "I want to leave." clicked away, briefing redone |
| 1101 | 237.1 | one_red | [2000]..[2138]; door clicked mid-briefing -> "Wait!"; also another player's unanswered Pete [32198]..[32228] |
| 1102 | 237.1 | four_blue | [8000]..[8121]; Pete followed the player through a teleport |
| 213 | 227.1 | - | pages only [23512]..[23681]; door clicked on "Good luck!" -> "Wait!"; door back twice |

Content: `server/scripts/macro_evil_twin/` (scripts, `.constant`, `.npc`, `.varp`).
Trigger: `::molly` (`macro_evil_twin_cheat.rs2`). Test: `test/randoms/molly.lua`
(scriptrun, `--fixture test/quests/fixtures/fresh_lumbridge.ini`; passes on seeds
molly0/1/2: three_blue, five_green, one_blue).

Tiles are offsets from the template square m29_80 (1856,5120); every copy is a
whole-square copy (4394 raw 11078,9873 = template 1862,5137).

## Recorded rules (counts across sessions)

- **Pete**: added 1..3 tiles from the player facing south; S+1 smoke (`smokepuff`
  86 on his add tile) + `sound_area` 1930 range 7 + "Message for you, <name>!"
  (3/3); a reminder at S+10 and S+20 with his type re-sent (1101, 1102); S+28
  `teleport_other_casting` + `postie_pete_tele`, S+30 gone (1101 [32226]..[32228]).
  He follows the player; through a teleport he is re-added on the player's tile
  a tick later (1102 [8012]..[8013]).
- **Accept**: on the answer click the music fades (song 147, fade-out 20) and
  Pete's type is re-sent [1010]; on the next click the teleport-other pair
  (player 1816 + 342 h92, Pete 3949 + 343) and synths 199, 200 [1011]; C+3 the
  player in the copy at (6,17), Molly at (4,15) facing south, Head to Head (612)
  [1014]; C+4 the claw and mark at (14,12), the side panels shut, minimap off [1015].
- **Briefing**: pages verbatim from 4394/1101/213 (every recorded Molly page is in
  the script). "Briefed" is set on the "Good luck!" click: 213 clicked the door
  on that page's tick and got "Wait! Do you know what you're doing here?",
  1101 mid-briefing the same; 4394 clicked through and went straight in (3/3).
- **Door**: arrive A, step onto the door's tile A+1, A+2 inviswall in the door's
  slot + `grab_evil_twin_poor_door_open` on (9,17) rot 3 + `door_open` + the step
  across; restored 2 ticks later. Entering the pen deletes Molly and adds the
  five suspects of her colour, each number on its own fixed tile (11,9) (13,8)
  (15,11) (10,6) (17,4) (3/3 identical).
- **Claw**: one tile per press, several presses in one tick each act ([1085]);
  `grab_evil_twin_reaching_control` (4004), mark + claw lifted and re-added,
  `twin_move_crane` 2273, camera to the new mark. Up = south, right = west.
- **Grab (G)**: "You caught the Evil twin!", 666 + 4001 on the suspect, claw gone,
  panel shut, camera to the cage, `twin_lower_crane` 2272. G+5 claw form +
  "You're putting me in prison?!" + seq -1; carried a tile a tick from G+6 to the
  cage (10,4), arriving A; A+2 drop (667 + 4003, type back, `twin_crane_drop`
  2271); A+3 claw home, camera pans; A+8 the other four in `smokepuff_large`
  (188) + synth 1930 each, the twin `emote_angry` facing (14,9), Molly back;
  A+12 "Congratulations! You caught the evil twin." + Molly's two pages (3/3:
  4394 A=[1099], 1101 A=[2103], 1102 A=[8084]).
- **Reward**: on the "Fantastic!" click: macro_last = 13, home, song 147 zero
  envelope + jingle 101 `evil_twin_caught` (4,008 ms), panels back; on the next
  click minimap on, "Your reward is: <n> x <gem>.", the noted gems, objbox.
  Recorded 3 rubies, 3 sapphires, 4 emeralds.

## Wire audit (ours = scriptrun tick log, seed molly0, 2026-10-27)

| section | recorded (4394) | ours | verdict |
|---|---|---|---|
| Pete arrival | add S, smoke + 1930 + say S+1 | add t5, all on t6 | matched |
| accept | music fade + type re-sent [1010]; tele-other pair + 199/200 [1011] | t10; t11 | matched (fade added 2026-10-27) |
| into the copy | teleport + Molly + 612 at C+3 [1014]; claw/mark C+4 [1015] | t14 (612 region); t15 | matched |
| door in | A [1064], door tile A+1, open + cross + suspects A+2 [1066], restore +2 | 48, 49, 50, 52 | matched |
| claw | 4004 + mark/claw + 2273 per press | every press t97..t109 | matched |
| grab/carry | G; G+5 transform + say; walk from G+6; drop A+2; smoke + Molly A+8 | G=110; 115; 116..120; 122; 128 | matched |
| door out | loc change on the step onto the door tile [1118] | t141 | matched |
| reward | macro_last 13, 147 + jingle 101 [1123] | t188: varp2606 = 13<<19, 147, 101 (4008) | matched (147 added 2026-10-27) |
| minimap_state | 2 at [1015], 0 at [1124] | t15, t189 | matched |

## Unverified (no recording; wiki)

- A wrong suspect, an empty grab, two misses and Molly's fail pages and the
  teleport to a random wiki location; "Tell her to get stuffed."; Dismiss;
  another player's Pete ("Sorry, I don't have any messages for you."); "No
  thanks."; leaving via "I want to leave." (4394 clicked Molly instead of
  continuing). Grab over the cage: the wiki's trivia message.
- The gem table (2..4, each gem 1/4) is the wiki's; three rolls recorded.
- Pete's "Maybe later." (wiki) is not sent: the one recorded unanswered Pete
  (1101 [32226]) left without it.

## Open

- Nothing schedules the event: the wiki's rule (60..120 minutes after the last,
  checked every 5 minutes, no instances/wilderness/busy) is not built; `::molly`
  is the only way in. A scheduler would bring Pete to automated test lanes too,
  so it is a decision for the owner.
- The HUD (side panels shut/reopened, the panel's side modal, cameras) is not in
  the tick log; it was read from the script against the transcript, not diffed
  on the wire.
