# Ratcatchers: what the recordings show, and the test brought back to passing

Written 2026-11-21. No recording plays Ratcatchers end to end (the index has no session that
writes its stage past the start). Four parts of what `test/quests/ratcatchers.lua` walks through
are recorded in other sessions, and those decided this work:

| part | recorded in | ours |
|---|---|---|
| the snake charm's flute, interface `ratcatcher_flute` (282) | index 4448 [1876]..[2103]: twelve openings, 104 presses, every button | rebuilt, below |
| reading the Ardougne teleport scroll | index 4360 [1181] | already matched; the test's rows were wrong |
| Shantay's Buy-pass and the doorway's warning | index 4448 [1186]..[1216], 4621 [504], 4629 [3903] | already matched; the test's rows were wrong |
| the Shantay carpet's Travel menu | index 4629 [5781] | already matched (Desert Treasure I) |

The test went from 222 of 251 rows to every row passing on four rolls (257 to 259 rows: the count
moves with how many fish the cat is fed). The new `test/quests/_ratcatch_flute.lua` replays the
recorder's own flute sessions: 15 of 15 (three rolls).

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

- What follows the right tune at the manhole (the line, the stage) is ours: no recording, and the
  wiki was not read for it. A video clip does show it
  (`~/Documents/osrs_cutscenes/ratcatchers/`, the row in `docs/quests/CUTSCENES.tsv`, not ported):
  "Rats start appearing out of nowhere, seemingly drawn to your music." with a fade to black, about
  27 seconds on the Port Sarim docks with the side panel and minimap hidden, then "Each rat in turn
  throws itself into the water and drowns." with a second fade and the player back by the manhole.
  Ours prints one line of its own and has no scene.
- Lowering the octave again, leaving page 8 backwards, a blank page in playback, and C# pressed
  with the octave already raised are not recorded; each is written the nearest recorded way and
  says so in the script.
- The headless host does not model `if_sethide`: a hidden arrow's press is still sent. The tests
  check that the page does not move instead.
- `test/quests/_conformance.lua`'s row for the op-less press was updated to the new first note
  (fingers 63) and not run: that file stops at its own plan check (233 verbs against 227 declared).
- The rest of Ratcatchers (every page, stage and scene) is unaudited: nothing records it.
