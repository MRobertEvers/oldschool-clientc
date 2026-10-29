# Client of Kourend and the Lovakengj Minecart Network (RSProx index 4415)

Session 4415 (rev 241.1, world 369, 2026-10-29) plays Client of Kourend start to
finish, then rides the Lovakengj Minecart Network from every station. Transcripts:
`rsprox_index.py get 4415` (default and `--bas`).

Content:
- `quests/quest_clientofkourend/scripts/clientofkourend.rs2`: rebuilt from this recording.
- `quests/quest_clientofkourend/scripts/veos_ferry.rs2`: the crossing and the first arrival.
- `quests/quest_xmarksthespot/scripts/xmarksthespot.rs2`: the Port Sarim talk.
- `transport_lovakengj_minecart/`: the network. This replaces the old wiki placeholder,
  which had 6 of 12 stations, a chat menu, no frame and no fare frame.
- `general/scripts/interface_menu.rs2`: `~menu_new_pick`, interface 947.
- `areas/area_arceuus/scripts/trossa.rs2`.
- `interface_questjournal/scripts/quest_journal.rs2`: the journal title colour.

Tests:
- `test/quests/clientofkourend.lua`
- `test/minecarts/lovakengj_rides.lua`
- `test/quests/xmarksthespot.lua` (its post-quest Veos check).

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
