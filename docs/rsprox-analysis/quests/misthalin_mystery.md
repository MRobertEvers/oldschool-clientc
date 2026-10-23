# Misthalin Mystery, from RSProx

Written 2026-10-23. The quest's content (`OSRS-Content/osrs-content/server/scripts/quests/
quest_misthalinmystery/`: `misthalinmystery.rs2`, `mist_cutscenes.rs2`, `mist_showdown.rs2`) was
rebuilt from one recording:

| session | rev | world | what it shows |
|---|---|---|---|
| index 4336 (local, 2026-10-23) | 241.1 | w337, normal | the whole quest: Abigale [434] to the scroll [2039] |

It is the only session on the index that enters the island square (`search mapsquare 6475`: 4336,
and 3156 passing through). Every rule below that rests on one occurrence says so. Branches the
recording does not reach (refusing the quest, the three other answers to the Killer, full packs,
re-talks, leaving the showdown mid-fight, the boat home) keep the wiki transcript's lines and are
marked `[wiki]` / `[unverified]` in the scripts.

Before this rebuild the quest was a wiki/Quest Helper port: invented camera tiles, five message
boxes the recording never sends ("The killer appears from a closet and stabs Tayten", "You stab
Abigale", ...), swing-open doors, notes as mesboxes, the explosion on a door click with a made-up
line, the cutscenes on the static map, every mood neutral.

Tools: `tools/rsprox/rsprox_cutscene.py` (drafts each scene; made to read rev-241 `id=NAME (N)`
transcripts for this), `tools/rsprox/rsprox_interact.py --op` (op timing), a per-tick viewer.

## The stage ladder (`%varb3468_mistmyst_progress`)

| value | tick | written by |
|---|---|---|
| 5 | 450 | "Hewey bought me enough time..." clicked, with "You've started a new quest: @q_mes@Misthalin Mystery</col>" |
| 10 | 453 | "The boat we escaped on..." clicked |
| 15 | 532 | the south garden (area trigger), before any click on the barrel |
| 20 | 605 | inside Sid's scene, as the killer walks off |
| 25 | 639 | the bucket on the barrel |
| 30 | 676 | the manor key in the front door |
| 35 | 770 | inside Tayten's scene |
| 40 | 811 | note 1 read (before the scroll is filled) |
| 45 | 828 | the knife on the painting |
| 50 | 864 | the ruby key in the ruby door |
| 55 | 950 | the fourth candle |
| 60 | 962 | the fuse lit |
| 65 | 979 | the explosion |
| 70 | 1089 | inside Lacey's scene, as the note goes through the fence |
| 75 | 1121 | note 2 read |
| 80 | 1197 | D-E-A-D |
| 85 | 1256 | the emerald key in the emerald door |
| 90 | 1360 | inside Mandy's scene |
| 95 | 1369 | note 3 read |
| 100 | 1443 | the knife on the fireplace |
| 105 | 1490 | S-D-Z-E-O-R |
| 110 | 1549 | two ticks after the sapphire key |
| 111 | 1588 | the confrontation's end |
| 115 | 1771 | the third knife thrown back |
| 120 | 1876 | the reveal's last page clicked |
| 125 | 1937 | the stab's page clicked |
| 130 | 1993 | the rescue's last page clicked |
| 135 | 2039 | Mandy's last page clicked: `mistmyst_xpreward` 1, qp, Crafting 600 xp, three uncut gems, the scroll (icon `mistmyst_cutscene_knife`; "1 Quest Point", "600 Crafting XP", "An Uncut Ruby", "An Uncut Emerald", "An Uncut Sapphire") |

## The cutscenes

Every scene is played in a **private copy of the island square**: the raw transcript moves the
player from 1615,4828 to 11855,8155 at [544]; the default transcript prints it translated back as
a one-tile move. The static npcs are `[del]`eted and the cast `[add]`ed with high indices. Jagex
makes a new copy for every scene and every re-entry of the sapphire room ([544], [735], [1035],
[1265], [1551], [1775], [1912], [1943]). Ours: `~mist_copy_enter(dx, dz)` (map_instance_from_square
into `%varp6600_map_instance_handle`, the old copy freed after the move).

The frame is identical in all seven (and in the canoe ride, index 4286):

| tick | packets |
|---|---|
| T | fade out (`fade_overlay` 948 `0,255,0,0,50`, hp bar hidden, `cc_deleteall`) |
| T+2 | `fov_clamp` 1, `cutscene_status` 1, npc/loc/obj ops hidden, `minimap_state` 2, `gravestone_tli_hide` 1, `option_acceptaid` 0, `option_areasounds_override` 25, `areasounds_override` 1, `draw_distance_max_override` 1; the move into the copy; the cast; 5 zero `cam_shake`; xp drops and sides 0-6, 11, 12 closed; `settings_set_draw_distance`; `highlighting_off`; `minimap_toggle 2` |
| T+3 | camera placed at rate 232/232, fade in `0,0,0,255,50` |
| E | fade out, the last page's chat closed |
| E+2 | everything back, the move out, `cam_unlock false`, `cam_reset`, sides re-opened, `highlighting_on`, `cam_reset`, fade in, `minimap_toggle 0` |
| E+4 | the fade overlay closed |

| scene | window | trigger | copy entry (template) | first camera eye / look (height, rate) | out to |
|---|---|---|---|---|---|
| Sid | 542..615 | area: the barrel's yard (fade at T, lock T+2) | 1615,4827 | 1614,4826 h300 / 1616,4831 h275, 232; then 10/15 and 20/20 pans | 1615,4827 |
| Tayten | 727..790 | the pink door at 30 (face O, fade O+1) | 1635,4837, a step to 4838 | 1634,4841 h1250 / 1637,4838 h250 | 1635,4838 |
| Lacey | 1033..1111 | area: the north garden | the player's tile, then 1632,4849 | 1625,4847 h375 / 1632,4850 h375 | 1632,4849 |
| Mandy | 1261..1366 | the kitchen door at 85 (face O, fade O+1) | 1630,4842 | 1634,4848 h1400 / 1627,4840 h250; 10/10 pans | 1630,4842 |
| confrontation | 1544..1592 | the sapphire key at 105 (message O, fade O+2) | 1627,4829 (Jagex bounces the player through a second copy at 1623,4831 first) | 1619,4824 h1250 / 1623,4830 h300 | stays: the showdown |
| reveal | 1771..1880 | the third hit (fade on it, lock H+2, new copy H+4) | 1623,4830 | 1619,4824 h1250 / 1623,4830 h300; 5/5 pans | stays: Abigale's fight |
| rescue | 1939..1997 | the sapphire door from inside at 125 (face+fade O) | 1627,4829 | 1620,4826 h1300 / 1626,4833 h150 | 1628,4829 |

The scene bodies (cast, walks, faces, seqs, sounds, props, pages with moods) are transcribed in
`mist_cutscenes.rs2` with the tick on each step. No box the wiki transcript writes ("The killer
appears from a closet...", "Abigale stabs Hewey.", "Mandy uses the Bandos godsword's special
attack...") is sent: the scene shows it. One page the transcript lacks is sent: Abigale's
"Hahahaha!" [1817].

## Area triggers (fitted to this session)

No op line precedes Sid's scene, Lacey's scene, the explosion, or stage 15. Each fires on the first
tick the player's tile, **read before that tick's movement**, is in its area: the fade is one tick
after the step that entered it. Ours: `[mapzone,0_25_75]` arms `[timer,mist_island]`, which tests
rectangles (`misthalinmystery.constant`).

| trigger | in | out (recorded) |
|---|---|---|
| 15 | 1623,4817 [532] | 1625,4817 [531] |
| Sid | 1615,4827 [542] | 1616,4825 [541] |
| Lacey | 1638,4847 [1033] | 1639,4846 [1032]; 1634,4842 inside the manor at 65 [1017] |
| explosion | out of the candle room: 1643,4834 [976] | 1643,4832 [975] |

The zone-crossing model (`[zone,..]`, one tick after entering an 8x8) fits Sid and 15 but not Lacey
(the player stood in the same zone at [1017], already at 65, with no scene). The barrel's own op
at [543] still fires (its face is on the wire) and is swallowed: ours returns at 10/15.

## Interactions

| interaction | recorded shape | ticks |
|---|---|---|
| bucket / knife / tinderbox | O: `human_pickupfloor`/`_pickuptable` + synth `pick`; O+1: the line + the item | [627..628], [682..683], [876..877] |
| barrel key / piano key / fireplace key | O: anim 832 + `pick` + item + objbox | [655], [1209], [1495] |
| painting slash / search | face O, action O+2 (`human_knife_chop` / 832) | [826..828], [834..836] |
| bucket on barrel, knife on painting | the box, then "Nothing interesting happens." after its click | [641], [830] |
| locked door | `locked` (2402) + mesbox, on O | 9 of 9 |
| wrong key | `locked` + "The key doesn't fit." | [847], [852], [1219] |
| key in door | O+1: line, key gone, stage, `unlock` (2419; front door `ds_big_door_unlock` 2972), a step onto the door's tile; O+2: through | [676..677], [864..865], [1256..1257] |
| walk-through door | `inviswall` in the door's slot + `*_inactive` leaf on the far tile for 2 ticks, `creakydoor_open` (56), the player stepped across | front [677], [2021]; ruby [865], [868], [871], [1249]; emerald [1257], [1381] |
| candle | O: varbits + mesbox, no anim | 4 of 4 |
| fuse | O: "You attempt to light the fuse..." + `human_createfire`; O+3: `[sequence] -1` + the page | [920..923], [959..962] |
| climb the wall | O: teleport across + `human_walk_crumbledwall` delay 30 + exactmove 30..100 + `climb_wall` delay 30; held 2 | [991], [1242], [1395], [1430] |
| note | take: objbox, no anim; read: stage, then `letterscroll` (interface 70) filled line by line with `pagepos` counting to 100, opened by `toplevel_mainmodal_open` | [796], [811], [1121], [1369] |
| boat | face L+1, fade L+2, `mes_spam` + teleport to 1637,4802 L+4, fade in L+5, overlay closed L+7 | [469..475] |
| finale | the knife leaves with "You decide it's wise to leave the murder weapon behind." (carried [1908], worn [1998]) | |
| wield the knife | `combat_weapon_category` 17 + `[bas] ready=elid_choc_ice_seller_ready (2911), walk=walkback=hween16_knife_walk (7327), run=human_toadcannon_running (2322)` (turn, walkleft/right `human_*`) + synth 2238; only in the `--bas` transcript. Ours: `quest_misthalinmystery/configs/misthalinmystery.obj` (`test/quests/_mm_knife_stance.lua`) | [1924] |
| crate / crates (Search) | arrival tick: "You search the crate but find nothing.", no anim (`general_use/scripts/crates.rs2`) | [898], [900], [902] |
| wardrobe (Open / Search) | arrive A, A+1 face, A+2 `human_openbigcupboard` + `wardrobe_open` (96), A+3 `draynor_wardrobe_open`; Search: "The wardrobe is empty." on the op tick; the leaf reverts 499 ticks on (`general_use/scripts/wardrobes.rs2`; Close not recorded) | [1502..1508], [1511..1516], [2006], [2014] |

## The piano and the gem panel

Piano (7 tries, [1139]..[1197]): each key `mes` "You play a(n) X." + its `grim_piano_*` synth. A bit
follows the previous one in D-E-A-D wherever in the four it falls: a D sets d1 (or d2 once a is
set), an E needs d1, an A needs e. Every fourth key ends the try: dead=15 opens it ("There is a
clicking sound as the compartment on the piano unlocks."); anything else "...the lock on the
piano's compartment resets." + "I don't think that's right.. Perhaps the clue the killer left
behind could help?" (two dots, as sent), the bits cleared after. Opening clears the bits; the
close button clears the attempt count.

Gem panel ([1456]..[1490]): `interface_select1` per flip and the gem's model in `selected_N`
(sapphire 32345, diamond 32333, zenyte 32349, emerald 32334, onyx 32342, ruby 2586, the buttons'
own); the same follow-the-previous rule (onyx at fourth with the emerald unset sets nothing,
[1462]); "...the lock on the panel resets." + "I don't think that's the right order.. ...".

## The showdown ([1590]..[1771])

The killer is `mistmyst_invisible_npc` (40 hp) at 1623,4829. From 10 ticks after the lock lifts,
every 19 ticks (9 of 9 cycles): S a `dark_spec_spot` (height 1) on a wardrobe, never the last one
(8 of 8 changes); S+14 the wardrobe opens, `throwingknife`, a `steel_tknife_travel` (start 41,
angle 15, progress 11, heights 163/146 raw) to the mirror when it is in the wardrobe's row/column
(end 46 + 5/tile, 6 of 6) else to the far wall (end 41 + 5 per tile past the first, 2 of 2); off
the mirror a second knife at start 82: back into the wardrobe (end 122) when the mirror was last
pushed towards it (3 of 3), else to 1619,4832 (6 of 6; end 172 off its side 4 of 4, 122 off its
back 1); S+15 the killer moves to that wardrobe; S+18 the wardrobe shuts, on a hit "Ow!" +
`female_hit_2` (range 10); S+19 the hit (10, headbar 22/15/8 of 30) and the next shadow. The third
hit starts the reveal on the same tick.

Pushes (24): on the op tick the player is placed on the mirror's tile with an exactmove (0..29,
facing the push) and `agility_pyramid_block_push_1`, the mirror npc moves a tile (a walked
teleport), `mistmyst_mirror_unblocker` on its old tile and `mistmyst_mirror_blocker` on its new,
`pyramid_block` (range 4) on the old tile; the sequence cleared the next tick. The mirror stays in
1622..1624 x 4828..4831: 5 of 5 refused pushes say "The mirror can't go any further in that
direction." The first blocker is laid five tiles south of the mirror ([1552] 1622,4823) and lifted
by the first push ([1605]).

## Wire audit

The quest test (`test/quests/misthalinmystery.lua`) against the client with the embedded
server, its tick log (`build/quest_gate/misthalinmystery/ticklog.tsv` through
`tools/rsprox/ticklog_tells.py`) diffed scene by scene against this session's timeline. A
dialogue scene waits on whoever reads it, and the recorder reads at another pace than the
driver, so each event is placed by (continue clicks since the cast spawned, ticks since the
last click): `[resume_pausebutton]` in the transcript, `input` packet 81 in the tick log. Seqs,
synths, cast adds/removes and type changes are compared; the island's residents leaving and
coming back as the player enters and leaves a copy are left out (both sides do it).

| scene | recorded events | matched | the rest |
|---|---|---|---|
| Sid's demise | 13 | 13 | |
| Tayten's demise | 16 | 15 | synth 3424 is the recorder's stamina running out ("Your stamina enhancement has expired.") |
| Lacey's demise | 11 | 11 | |
| Mandy's demise | 22 | 22 | |
| the reveal | 20 | 20 | the shadow's removal 4 ticks before the killer lands is ours too (outside the window the diff reads) |
| the rescue | 22 | 22 | |
| the showdown | the cycle | yes | a throw every 19 ticks, "Ow!" at throw+4, the hit at throw+5; third hit -> shadow gone +4, killer +8 ([1771] -> [1775], [1779]) |

Outside the scenes (player-paced, so compared by presence, not tick): every synth the
recording plays has an answer except the recorder's own actions (2401 drinking, 2739 dropping
and 2238 wielding the knife) and the locked-door synth 2402, which our run never asked for (the
test does not try a locked door; `~mist_locked` plays it). The piano's fourteen keys map to the
same fourteen notes (3692..3705). Every loc, npc and ground-item op the recorder used has a
handler; the crate and wardrobe ones were added from this audit.

The audit found one client bug: an exact move left the player's grid tile on the start tile
(`World_PlayerPathSetHead` set `route[0]` but not the tile readers fall back to when the route
is empty), so after the wall climb the client still said 1647,4829 and routed the next click
from there. Fixed in `src/world/world.c`, test `test_rev239_exact_move_moves_the_tile`.

Found after (2026-10-23, the owner's run): the piano's keys stayed lit. Each label's
onmouseover sets its key's lit model through clientscript 67 (`if_setmodel`), the onmouseleave
the key's own. The lit model was not resident, so the host handed it to the app's lazy store;
the key's own model was resident and bound directly, and the store re-bound the lit one on the
next tree generation. The server sends nothing here (no packet between key clicks but the
`mes` and the synth). Fixed in the client: a direct bind drops the stored request
(`RS_CS2Host.widget_model_direct`); `test/quests/_mm_piano_hover.lua` sweeps all fourteen keys
(14 stuck before, 0 after).

The first audit also missed the killer's knife's stance (above, "wield the knife"): the
default transcript carries no player `[bas]`.

## Open

- `option_acceptaid` (4180) goes 1 -> 0 at every lock and back at every unlock; not sent (it is a
  player setting; restoring it needs the old value kept).
- The killer's hitsplat is recorded as type 17 (damage on another); ours sends the generic
  `hitsplat_damage` and the server picks the viewer's variant.
- The confrontation's bounce through a second copy ([1551]..[1553]) is not reproduced.
- `busy` (12393) is engine-generic and not set by this content.
- Music changes at the island's edges (147 then 76) are the area music system, not content.
