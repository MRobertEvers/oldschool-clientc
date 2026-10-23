# X Marks the Spot, from RSProx

Recording: index 4353 (uploaded 2026-10-23, rev 241.1, world 337, normal world,
"X Marks the Spot"), 463 ticks, one recorder (LoveTori, 8 -> 9 QP). The whole
quest: start in the Sheared Ram, four digs, the orb destroyed and handed back,
the casket destroyed and dug again, the hand-in at Port Sarim, the lamp, and two
talks with Veos afterwards. Index 1139 (Leagues VI) is the only other session
with the quest's varbits: an auto-complete, used here for the login values and
the completion's varbits only.

Ours: `OSRS-Content/osrs-content/server/scripts/quests/quest_xmarksthespot/`
(`xmarksthespot.rs2`, `.constant`, `.varp`, `.npc`), `player/newplayer.rs2`
(login placement), `quests/scripts/quest_cheat.rs2`, the digspade line in
`general_use/scripts/spade.rs2` (written by the Corsair Curse session from index
4352; this recording agrees: digspade 1470 on every dig, find or not).
Test: `test/quests/xmarksthespot.lua`. `test/quests/clientofkourend.lua` talks to
the Piscarilius Veos as `veos_vis` now (the plain variant both recordings show),
and passes 100 of 101 on scriptrun (the journal row, as below).

Cites are `[tick]` in index 4353 unless stated.

## The stage ladder (`%varb8063_cluequest`)

| value | written | ours |
|---|---|---|
| 1 | [124] on the "Start the X Marks the Spot quest?" Yes, before the start line | same |
| 2 | [125] with the "Take this scroll" page and `inv_add clue1` | same |
| 3 | [159] Bob's dig | same |
| 4 | [197] the castle dig | same |
| 5 | [334] the Draynor dig | same |
| 6 | [365] the pig-pen dig, with `veos_lumbridge_vis` 2 -> 0 | same |
| 7 | [425] "You give Veos the Ancient Casket." with `inv_del` | same (was never written) |
| 8 | [431] with `cluequest_clue_reward` (8258) = 1, `cluequest_coin_reward` (8065) = 1 | same |

`cluequest_lamp_reward` (8066) = 1 when the lamp is rubbed ([452]).

Placement varbits (multinpc shells, `tools/data/multinpc_shells.csv`): at login
`veos_pisc_vis` 2, `veos_sarim_vis` 3, `veos_lumbridge_vis` 2 (index 4353 [0] and
index 1139 [0], the same). Only `veos_lumbridge_vis` ever moves, to 0 at the
casket dig ([365]; index 1139's auto-complete does the same). Before: ours hid
the Lumbridge Veos when he gave the scroll, never set Piscarilius at login, and
swapped both Sarim and Piscarilius to the amulet variants at completion.

## Pages

`tools/rsprox/rsprox_pages.py` over the session. Every recorded page is now ours
with its text and mood (`IF_SETANIM` from `TORIRSSERVER_TRACE_OUT=2`, decoded:
every page the test reaches matches seq for seq; the recording has 35).

- Start ([113..127]): "Hello there." `chathap1`; option 2 "I'm looking for a
  quest." `chatquiz1`; then neutral pages, among them "I am here on a bit of a
  hunt..." (ours said "I'm here ... Kourend, I came") and "Alas ... Maybe you
  could help with this." (ours lacked the last sentence). The start menu
  follows the Alas page directly: `Start the X Marks the Spot quest?` Yes./No.
  Ours had two invented menus ("Can I help?", "Sounds good, what should I do?")
  and their pages: removed.
- The recorder walked away on "Awesome. Anything else I should know?" ([127]),
  so the spade advice, the "Who are you?" branch, and "No." are not recorded:
  kept from the wiki transcript, marked `[wiki]`.
- The lost orb ([294..300], the Lumbridge Veos): the item goes out WITH "I came
  across this orb..." (ours added it after the page).
- The casket ([365..369]): objbox, then "Hmm... Must have been the wind."
  `chatcon1` (ours "Hmmmm..."), "Anyway, this must be..." `chathap3`.
- The hand-in ([420..434]): 11 pages and the box. After the reward, ONE page:
  "Anyway, I'd best be getting back to Great Kourend. You're welcome to come
  along. In fact, as a thank you for your help, I'm willing to take you to and
  from Great Kourend any time you like." `chathap4`. Ours had the wiki's two
  older pages and a player reply; the recorder rubbed the lamp on that page
  ([435]), so whether a reply follows is unrecorded, and none is sent.
- After the quest, Client of Kourend not started ([439..461], two talks):
  "Hello Veos." / "Hello there. What can I do for you?" / `Can you take me to
  Great Kourend?|Tell me more about Great Kourend.|Nothing.`; "Tell me more" is
  one page and the chat closes on its continue ([461]). Ours sent Client of
  Kourend's three-way gate. New label `xmarks_veos_sarim_after`; its options 1
  and 3 are not recorded (option 1 sails as the gate did).

## Digs

The spade (`spade.rs2`): `p_arrivedelay`, then the anim, digspade and the answer
on one tick: C+1 after a step on the click tick or the one before ([131/132],
[141/142], [151/152], [158/159], [192/193], [196/197]), C when standing
([189], [334]). Each find on that tick: the stage, the old item replaced in its
slot, then an objbox (zoom 400), never a game line:

| dig | tile | box | ours before |
|---|---|---|---|
| Bob [159] | 3229,3209 (3232,3209 [152] and 3233,3209 [155] not) | clue2, "You dig up a Treasure Scroll." | 3230,3209 (Quest Helper), `mes` |
| castle [197] | 3203,3212 (3204,3211 [189], 3204,3212 [193] not) | clue3, "You dig up a Mysterious Orb." | same tile, `mes` |
| Draynor [334] | 3108,3262, the orb's distance 0 | **clue2** (23068), "You dig up a Treasure Scroll." while giving clue4 (23070): Jagex's box, copied | 3109,3264 (Quest Helper: wrong), `mes` |
| pig pen [365] | 3077,3260; the re-dig [390] at 3078,3260 | casket + the whisper text | 3078,3259 |

The pen accepts at least two tiles; ours takes the 3x3 around the pen's middle
3077,3260 (unverified extent). The re-dig after the casket was destroyed writes
no varbit ([390]).

## The orb (`xmarks_feel_orb`)

23 feels, [202..332] (the recorder double-clicked on two ticks). The temperature is the Chebyshev distance to 3108,3262
(every recorded distance fits it exactly; the dig is that tile). Each feel, on
the click's tick: `cluequest_prev_distance` (8064) when it changed, the varp
`cluequest_prev_coord` (3697, transmitted) always, the line, a synth, and
`cluehelper_hotcold_update (6473)(here, last tile or null, distance, last
distance or 0)`.

| band | recorded distances | synth | line |
|---|---|---|---|
| cold | 90, 95 | `history_key_cold` 1198 | The orb is cold[, ...] |
| warm | 62..87 | `history_key_warm` 1202 | The orb is warm[, ...] |
| very hot | 11 | `history_key_hot` 1200 | The orb is very hot[, ...] |
| incredibly hot | 5 | `history_key_burning` 1197 | The orb is incredibly hot[, ...] |
| shaking | 0, 2 | `history_key_burning` 1197 | The orb is visibly shaking and burns to the touch. This must be the spot. |

The first feel ([202]) has no comparison; later ones add ", and warmer than
last time." / ", but colder than last time." / ", and the same temperature as
last time." by the distance against the last feel (12 warmer, 2 colder, 6 same).
The shaking line never has one. The cut points between the bands are not
recorded: ours are shaking <= 2, incredibly hot <= 9, very hot <= 29,
warm <= 89, and cold beyond (nothing past 95 was felt, so no colder band is
built). Before: invented "The orb feels ..." lines, six bands centred on the
wrong tile, no sound, no state.

## Items

- Clue reads ([130], [136], [338]): `trail_cluetext` (203) with Jagex's own
  `<br>`s, `toplevel_mainmodal_open (2524)(-1, -1)`, the mount. The cipher page
  is "The cipher reveals<br>where to dig next:<br><br>ESBZOPS QJH QFO" (ours had
  the cipher alone). The map (clue 2) was never read.
- Destroy, iop 5 (if_buttonx op=7): the orb [264..270] and the casket
  [379..382]: `confirmdestroy` with `confirmdestroy_init (814)(obj, 1, "Are you
  sure you want to destroy this item?", note)`; Yes: the slot empties and synth
  `destroy_object` 2381. Notes: the orb "Maybe you can get another from Veos in
  Lumbridge.", the casket "You'll need to dig it up again.", the scrolls the
  orb's (wiki Treasure_scroll). Ours had no destroy: op 5 dropped them.
- The lamp ([435..452]): the cache's `xpreward` (240), not the twocats picker:
  `if2` (262) = -1, 2524, the mount, the pause button over the skill children,
  title "Choose the stat you wish to be advanced!". Its confirm
  (`xpreward_confirm.cs2`) resumes on child (enum_681 index - 1): [451] sub 7 =
  Agility. The grant on the next tick: the reward bit, the lamp out, synth
  `found_gem` 2655, the picker closed, and the box "<col=0000ff>Your wish has
  been granted!</col><br>You have been awarded 300 Agility experience!". The
  close button (240:28, [438]) closes it.

## Rewards ([431])

The scroll: casket model; "1 Quest Point", "An Antique Lamp", "200 Coins",
"Ability to receive Clue Scroll", "Boxes", "A Beginner Clue Scroll Box" (ours
now wraps to the same six lines). Items: the lamp, 200 coins, and the **beginner
scroll box** `league_clue_box_beginner` (24361), not a clue scroll (ours gave
`trail_clue_beginner`). Music: `midi_song_v2` 147 with a zero envelope and
jingle 154.

## Npc info

Veos (`npc_block_inventory.py`): the only block kind on either side is
`[facing]`. Spawns: Lumbridge 3228,3242 facing south ([104], [287]), Port Sarim
3054,3245 facing **north** ([412]); ours had Sarim south (`configs/xmarksthespot.npc`
now says north). Facing: he turns to the player once, on the page after the
first ([114], [295], [421]), and at the end resets and turns back to angle 0
([128..129], [302..303]). Ours re-faces the player's tile and entity on every
page (19 and 26 faces against 6 and 2): the shared chat procs, as the
Stronghold of Security audit found. Open (shared).

## Wire audit

Ours: `test/quests/xmarksthespot.lua` on scriptrun with private packs
(2026-10-23): 62 of 63 rows pass; the one failure is `quest.journal`, which
fails the same way on Sheep Shearer under scriptrun (the questlist rows are
built by a clientscript scriptrun does not run). Tick log rows and
`TORIRSSERVER_TRACE_OUT=2` packets.

| R (4353) | ours | verdict |
|---|---|---|
| every page: text, speaker, mood (35, incl. the post-quest talk) | same, on every page the test reaches | matched |
| start line on the Yes, stage 1 then 2 | same ticks | matched |
| dig: `human_dig` + digspade 1470 + stage + item + objbox, one tick | same (122, 132, 176, 239) | matched |
| dig timing C+1 after a step, C standing | 175 -> 176 after a walk; 122 on a standing click | matched |
| orb: 8064, 3697, line, synth, 6473 args | cold 1198 [135] (95, null, 0), incredibly hot 1197 [163] (8, last tile, 95) | matched; the clientscript's arg types go out as `i`, Jagex's `coord` (engine) |
| destroy: 814, Yes -> `destroy_object` 2381 | same ([95], [111]) | matched; events 0..28, Jagex 0..1 (`if_addresumebutton`, engine) |
| clue read: 2524 + mount, text with `<br>` | same | matched |
| hand-in: stage 7 with the box | 260 | matched |
| reward: varbits, qp, line, items, song 147, jingle 154, scroll lines | same | matched; jingle length 6000 vs 5869 ms (engine) |
| final page AFTER the scroll is closed ([433] close, [434] page) | the page on the reward tick, the scroll queued after the dialogue | open: shared `questscroll.rs2` queues the mount (its header explains why) |
| lamp: 262 = -1, 2524, mount, resume, grant next tick, 2655, picker closed, box | same | matched; events 0..28, Jagex 0..23 (engine) |
| Veos spawn angles | south / north | matched |
| Veos facing, once per talk then reset + angle 0 | every page | open (shared chat procs) |
| player `[facing] coord=` Veos each page | engine | engine-generic |
| level-up box and its sounds after the lamp ([454]) | engine | engine-generic |

## Open

- The final page should wait for the scroll to close (shared `questscroll.rs2`).
- Veos's facing per page (shared chat procs).
- `if_addresumebutton` arms 0..28 where Jagex arms 0..23 (xpreward) or 0..1
  (confirmdestroy).
- Clientscript coord arguments are typed `i` on our wire.
- The orb's band edges are ours (no feel between 12 and 61, nor beyond 95).
- Scroll boxes (2026-10-23, `trail/scripts/trail_scrollbox.rs2`, test
  `test/quests/_scrollbox.lua`): Open, the cap, and box drops after this quest,
  from eleven recorded opens (indexes 1025, 3907, 3909) and the refusal and
  cap lines (indexes 231, 3910, 938). Not built: picking up boxes past the cap
  ("Some of your Scroll Boxes were destroyed..."), scrolls held in the bank
  counting against the cap, and the wiki's "only if you don't already have
  one" on this quest's box.
- Unrecorded and kept from the wiki: "Who are you?", the spade advice pages,
  "No." at the start menu, the mid-quest hint menu, the casket's Open, options
  1 and 3 of the post-quest menu, and the full-backpack lines.
