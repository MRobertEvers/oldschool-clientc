# Client of Kourend and the Lovakengj Minecart Network (RSProx index 4415)

Session 4415 (rev 241.1, world 369, 2026-10-29) plays Client of Kourend start to
finish, then rides the Lovakengj Minecart Network from every station. Transcripts:
`rsprox_index.py get 4415` (default and `--bas`).

The same recorder's later sessions use the quest's rewards and the network again:
4416 (The Queen of Thieves) and 4418 (The Depths of Despair) add memoir pages, use
Reminisce and ride 7 more carts; 4418 breaks a Kourend Castle teleport tablet; 4419
wields the memoirs. They are sections 8 and 9 and the 4416/4418 rows of section 7.

Content:
- `quests/quest_clientofkourend/scripts/clientofkourend.rs2`: rebuilt from this recording.
- `quests/quest_clientofkourend/scripts/veos_ferry.rs2`: the crossing and the first arrival.
- `quests/quest_xmarksthespot/scripts/xmarksthespot.rs2`: the Port Sarim talk.
- `transport_lovakengj_minecart/`: the network. This replaces the old wiki placeholder,
  which had 6 of 12 stations, a chat menu, no frame and no fare frame.
- `general/scripts/interface_menu.rs2`: `~menu_new_pick`, interface 947.
- `areas/area_arceuus/scripts/trossa.rs2`.
- `interface_questjournal/scripts/quest_journal.rs2`: the journal title colour.
- `general/scripts/enchanted_jewellry/kharedst_memoirs.rs2`: the reward book, rebuilt
  from 4416 and 4418 (section 8).
- `skill_magic/configs/magic_spells.dbrow`: the Kourend Castle Teleport landing
  (section 9). `skill_magic/scripts/spells/teleport.rs2`: the teleport guards split
  out as `~player_teleport_blocked`, for the memoirs' own cast.

Tests:
- `test/quests/clientofkourend.lua`
- `test/minecarts/lovakengj_rides.lua`
- `test/quests/xmarksthespot.lua` (its post-quest Veos check).
- `test/magic/kharedst_memoirs.lua`: Inspect, the empty entry, both page adds,
  Reminisce from the menu and from the right-click entry, Check, the castle tablet.

## 1. Action inventory (`rsprox_actions.py`), a verdict per row

| tick | action | verdict |
|---|---|---|
| 2 | welcome_screen:play | engine (login) |
| 16, 21, 33, 51, 320 | quest list / overview / journal open+close | journal: section 6 |
| 85, 90..106 | Falador bank booth, bank deposit/withdraw (helm, 3 tablets, staminas) | generic: banks (`interface_bank`), not re-diffed |
| 111 | Varrock teleport tablet Break | generic: `teleport_tablet.rs2` (diffed for TGV, index 4394) |
| 148, 494, 731, 1211 | stamina potion drinks | generic: potions. OPEN: `stamina_duration` (varbit 24) is never transmitted (varp_transmit_audit) |
| 155..173 | GE clerk, GE offer for feathers, collect | generic: `interface_grandexchange`, not re-diffed. OPEN: the `ge_*` vars it records are transmit=no |
| 199..225 | banker, glory equip, glory Draynor teleport (worn op4) | generic: `bankers.rs2`, `amulet_of_glory.rs2` |
| 273 | Veos (Port Sarim) Talk-to | **this content**, section 2 |
| 325 | feather on enchanted scroll | **this content** |
| 330, 1051, 1075, 1581 | Veos (Piscarilius) Talk-to | **this content** |
| 370/371, 503/506, 622/626, 782/783, 950/952 | five general stores, Trade and Talk-to | Talk-to is **this content**; Trade is the shops (`shop/<city>/`), bound |
| 413/424 | world map open/close | engine UI |
| 497 | archeuus_stairs_lower | generic: maplink row exists (`arceuus_stairs_maplinks.dbrow`) |
| 498, 616, 777, 944, 948 | store doors (Arceuus double, Lovakengj, Shayzien, Hosidius) | generic doors (`doubledoors.loc`/`doors.loc`). The test passes each with `pass_door` |
| 1071 | veos_orb Destroy | **this content**: `[opheld5,veos_orb]`, was dropped by the default drop |
| 1242..1371, 1526 | 13 minecart rides, one cart loc | **this content**, section 7 |
| 1396 | Trossa (arceuus_fairyring_curator) Talk-to | had NO binding. Added, recorded pages (`trossa.rs2`); checked in the minecart test |
| 1451 | veos_orb Activate | **this content** |
| 1605 | quest scroll close | engine UI |
| 1608, 1622 | veos_lamp Rub | **this content** (lamp picker) |
| 1640 | logout | engine |

## 2. Teleports

| tick | cause | landing, R | ours | verdict |
|---|---|---|---|---|
| 115 | Varrock tablet | 3213,3424 | generic | not this content |
| 228 | glory Draynor | 3105,3251 | generic | not this content |
| 289 | Veos sail, Sarim to Piscarilius | 1824,3691 | 1824,3691 | fixed: `^cok_veos_piscarilius` was 1824,3690 |
| 1252..1535 | 13 minecart rides | the 12 landings | all 12 equal (test) | matched |

## 3. The crossing (Veos, [286..292], S = the click that ends "Certainly!...")

| tick | R | ours (`TORIRSSERVER_TRACE_OUT`) | verdict |
|---|---|---|---|
| S | fade out (948 0,255,0,0,50), song 147 (all 0), jingle `quick_sail`, chat closed | same | matched |
| S+3 | "You board the ship and sail to Port Piscarilius.", `veos_amulet` 0->1, `veos_sarim_vis` 3->1, `veos_pisc_vis` 2->1, teleport | same | matched |
| S+4 | fade in | same | matched |
| S+6 | overlay closed, Veos's first page (head 2147) | same | matched |
| [315] | after "See you soon.": `zeah_playerhasvisited` 1, `veos_sarim_vis` 1->2 | same (test checks both) | matched |

A first arrival (`zeah_playerhasvisited` 0) gets the arrival talk. The job is offered
inside "Could you tell me more about Kourend?". Our old tree offered it from a
"Have you got any quests for me?" row instead.

## 4. Stages (veos_progress, 5619)

| value | R tick, the click it goes out with | ours |
|---|---|---|
| 1 | [304] "Sounds interesting!", with the start line and the scroll | same |
| house bits 5620..5624 | each on the click of the store owner's rhyme/answer page, with the box | same |
| 2 | [962] Horace's answer page, the fifth house, in a messagebox | same |
| 3 | [1063] Veos's "Well that's just the thing..." | same (was written before the take) |
| 4 | [1065] "My client will be most pleased.", with the orb and no box | same (an objbox before was invented) |
| 5 | [1453] two ticks after Activate | same |
| 6 | [1592] "...on ab--" | same |
| 7 | [1600] "Knowledge can be a curse..." | same (was after the last page) |
| 8, 9 | [1617], [1625] each lamp rubbed | same (the picker never counted) |

Every quest varbit is transmitted. `varp_transmit_audit.py` lists none of them.

## 5. Pages

`tools/quest_gate`'s scriptrun trace against the transcript, in order. The match is
on box kind, chathead animation, and text length (bodies are capped at 48 bytes in
the trace).

- **141 of 141 pages match.**
- The only differences are the player's name: LoveTori is 8 characters, cok_p1 is 7.

Changes from the old tree:
- The Sarim menu.
- Arrival.
- The 5-row Piscarilius menu.
- "Goodbye." rows.
- 4-row menus for Regath, Munty and Horace, with "Who are you?".
- Every mood.
- Leenz's verse breaks.
- Regath's stray spaces and his "- runic, magical and religious -".
- "knowledge of life".
- "They mentioned".
- The double object box (interface 11, scroll and quill) for each note.
- The whisper in a messagebox with the recorded break.
- The Client's pages with `<col=000080>` on every row.
- The last page sent without a wait and closed under the scroll.
- Scroll rows "2 Antique Lamps|Kourend Castle Teleport|Kharedst's Memoirs" with the quill model.
- The lamp picker from X Marks the Spot, with the recorded title, child 7, found_gem and the box.

The orb ([1451..1456]):
- Click: `wanted_crystal_ball_scan` + spotanim at 92 + `wanted_scan`.
- Click+2: the stage; the game message with `<br>`; the orb becomes `broken_glass` in its slot; "Argh!"; `glass_break`; the objbox; the messagebox.

UNVERIFIED (wiki Transcript:Client_of_Kourend / Transcript:Veos / the store owners'
transcripts, moods unknown): "Have you got any quests" start, No to the start, the
scroll/orb replacement when lost, "What am I meant to be doing", no-tools refusals,
"provides" topics, "Who are you?", arrival "Goodbye.", Sarim "Can you take me to
Great Kourend?", full-pack lines.

## 6. Journal

Recorded at three states. Our rows were read by the real client in the run.py lane;
all three pass row for row as plain text. On the wire, every row's byte length is
equal to the recording's.

| R | state | ours |
|---|---|---|
| [318] | stage 1, no quill | 5/5 rows |
| [345] | stage 1, quill, no house yet | 13/13 rows. The blank row 17 is `<col=000080>` against R's `""`: the engine split carries the colour into an empty row. It draws nothing |
| [1086] | stage 4 | 13/13 rows |

The title is now `<col=7f0000>Name</col>` for every quest. That was recorded in 4378,
4381, 4404, 4412 and 4415; ours sent `<col=800000>Name`.

Stages 2, 3, 5, 6 and complete are UNVERIFIED (no recording opens them). A done house
drops from the list (UNVERIFIED).

## 7. Lovakengj Minecart Network ([1242..1371], [1526..1535])

The menu is interface 947 `menu_new`:
- `toplevel_mainmodal_open(-1,-1)`.
- Clientscript 9142 ("Minecart rides: 20 coins", the 12 stations + "(Cancel)", 1, -1, 0, 0).
- The rows are armed. Jagex arms 0..512; `if_addresumebutton` arms 0..28.

The same at every station. It opens on the op, or on arrival when walked (L, [1246]).

The ride, A = the answer (13/13 recorded rides; ours on the trace, ride 1):

| tick | R | ours |
|---|---|---|
| A | coins -20, `chatdefault_restoreinput` (2158), fade out, mainmodal closed | same |
| A+1 | `minimap_state` 2, `minimap_toggle` 2 | same |
| A+2 | synth `forget_cart_move` (1112) delay 5 | same |
| A+3 | teleport to the landing | same, 12/12 landings |
| A+4 | `minimap_state` 0, busy 0, fade in, `minimap_toggle` 0 | same |
| A+6 | overlay closed | same (queued, the player is free from A+4) |

- 4416 and 4418 ride 7 more (Travel, `opnpc3`): Kingstown to Piscarilius (4416 [998]);
  Kingstown to Hosidius West, Hosidius West to Hosidius South, Hosidius South to
  Arceuus and back, Hosidius South to Kourend Woodland (4418 [97..541]). All 7 land
  on the tiles above and teleport at A+3 (A+4 four times in 19, each a click the proxy
  filed a tick early: the fade comes a tick after it, 4415 [1287..1288]). No conductor
  turns to the player on Travel, in any of the three sessions; ours sends no face.
- Your own station ([1332] Hordal, [1364] Ferain) gets one page from the conductor:
  chatcon1 "You're already here, human.", with the npc facing you. No coins are taken.
  Ours matches (test).
- The cart loc ([1282], 1648,3930) opens the same menu on its tick. It is
  answered as the nearest station.
- Conductors are 12 types. None steps in 15 lives. Each is added at a fixed spawnangle
  (S, S, S, N, E, E, E, W, W, E, S, E). They are now `defaultmode=none` + `facing=`
  in `lovakengj_minecart.npc`; the generated block had six of them wandering 5
  tiles, all facing south. The keys were removed from `server_fields.generated.npc`.
- Five conductors (Ferain, Elnes, Hordal, Buneir, Hatna) were never spawned.
  `lovakengj_minecart.spawn` adds them at the recorded tiles.

UNVERIFIED (wiki Lovakengj_Minecart_Network, Transcript:Loinur):
- Talk-to.
- Travel without 20 coins.
- Free rides after The Forsaken Tower, and that menu's title.
- The 100,000-coin refund.
- "Already here" from a cart loc.

## 8. Kharedst's memoirs (4416, 4418; the quest's reward)

Our tree had a wiki-era script (2026-08) on six private varps (6995..7000,
`transmit=no`) with invented messages, no menu and no page Inspect. Jagex keeps it
all on varp 1566's varbits, perm and transmitted: `hosidius_page`..`arceuus_page`
(6030..6034) and `veos_memoir_charges` (6035). The quest hands the book over with
none of either (4415).

| interaction | R | ours | verdict |
|---|---|---|---|
| Reminisce, plain click (iop1) | C: busy, `chatmenu` "What would you like to remember?", 5 rows: `'<title>' - <house>` per page in, `<col=ff0000>The first entry is missing.<col=ff0000>` (second, third, ...) per page out (4416 [1207] [1216], 4418 [901] [905]) | same (test reads the rows) | fixed |
| a missing entry | on the answer: "There is nothing here to remember.", menu closed (4418 [903]) | same | fixed |
| an entry, A = the answer | A: `arceuus_necromancy_anim` (3865), `arceuus_teleport_spotanim` (1296, no height), `sound_area` 200 range 5, menu closed. A+3: memories - 1, "Kharedst's Memoirs now has <col=800080>N</col> memories remaining.", teleport, seq -1 (4416 [1218..1221], 4418 [907..910]) | same, on the trace (A, A+1, A+2, A+3) | fixed |
| Reminisce, right-click entry (`if_subop`) | the same cast with no menu (the Book of the Dead, 362 [3967], same five places) | same (`last_subop` 1..5) | fixed; memoirs' own unrecorded |
| landings | Lancalliums 1713,3610 / 1714,3611 / 1714,3612; Flute 1802,3748 / 1803,3749 / 1804,3748; Hearsay 1478,3575; Jubilation 1543,3762; Disposition 1680,3747 (4416, 4418, 362, 3133, 3910) | the wiki pins, within 2 (was 3) | matched |
| a page on the book, either order | C: the page's varbit 1, memories + 20 (0 -> 20, 18 -> 38), the page gone, objbox of the page "You add an entry to Kharedst's Memoirs." (4416 [1201], 4418 [1041]) | same | fixed |
| a page's Inspect (its iop1) | player page, chatneu2, "This looks like one of the pages to Kharedst's Memoirs. Maybe I should try adding it to the book." (4418 [1032..1033]) | same | fixed (was unbound) |
| Wield (iop2) | the equip (4419 [725], synth 2238) | engine equip, wearpos 5 | generic |
| quest completion | The Depths of Despair (4418) and The Queen of Thieves (4416) complete without setting their page's varbit; it goes out with the add | ours set it at completion in Depths of Despair and Ascent of Arceuus: removed | fixed |

UNVERIFIED (no recording):
- Check: the Book of the Dead's recorded box (362 [37], "On the inside of the cover a
  message is displayed in dark ink. It reads: <col=800080>33/250 memories
  remain.<col=800080>"), with the memoirs' limit, 20 a page (wiki).
- Reminisce with no memories left, and past level 20 Wilderness (wording ours).
- A page added twice ("Nothing interesting happens.").
- Read (iop3): not bound. No recording reads the memoirs; the Book of the Dead's book
  text (362 [494], interface 392, 30+ pages) is a later book with another title.
- The Old Memorial's rune top-up and the secret page (`veos_memoir_secret`, 6036).

`charges_selftest.rs2`'s two memoir procs now read the varbits. `pack/varp.alloc`
retires 6995..7000.

## 9. Kourend Castle Teleport (the quest's spell)

| R | ours | verdict |
|---|---|---|
| 10 casts and tablet breaks land in 1639..1643 x 3671..3675: index 354 [3166] [3226] [3477], 362 [3057], 415 [2870], 786 [1575], 1016 [3410], 3907 [813], 3909 [3752], 4418 [36] (tablet) | `tele_coord` was 1631,3673 (wiki), 10 tiles west. Now 1641,3673, radius 2; the test's tablet lands 1639,3672 | fixed |

The tablet's own shape is the generic one (`teleport_tablet.rs2`, checked for TGV).

## Open

- Veos's possession anim/spotanim ([1592]) is `npc_anim_specific` / `npc_spotanim_specific`,
  sent to the recorder alone. The engine has no such packets, so ours goes out in npc
  info (public). It plays only `human_killerwatt_electricshock`, not the
  `urn_search_bit` Jagex sends first in the same tick.
- Generic, seen in this session and not fixed here:
  - `stamina_duration` and the GE vars are never transmitted.
  - The client lane's `confirmdestroy` and `xpreward` presses do nothing. They fail
    identically in X Marks the Spot's test. Both pass headless, so this is a
    client/driver issue, not content.
- The journal's blank row carries a colour tag (above).
- The memoirs: Read, Check's wording, the Old Memorial, the secret page (section 8).
- The Book of the Dead keeps its memories in an item var; Jagex spends
  `veos_memoir_charges` for it too (362 [6762]). That is A Kingdom Divided's content,
  not changed here.
- The conductors' Talk-to is still unrecorded (4415, 4416, 4418, 1418 only Travel).
- On scriptrun the journal rows (`journal.*`, `quest.journal`) fail: the headless
  lane has no quest list to open. They pass on the client lane, as before.
