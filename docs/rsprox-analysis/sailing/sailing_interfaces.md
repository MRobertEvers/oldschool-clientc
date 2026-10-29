# Sailing interfaces and interactions vs RSProx index 4407

Recording: index 4407 (rev 241, 2026-10-29, the owner's Pandemonium playthrough, ~1887 ticks).
Transcripts: `~/.rsprox-index-cache/home-{default,bas,raw}/.rsprox/binary/archive/idx4407.txt`.
The default transcript has no world-entity info (boat motion), so this audit covers only the
server's content packets. Cites are `[T]`; O is the op tick, L is the player's last step.

Ours was checked on the wire with `test/raids/sailing_wire.lua` (scriptrun, 42/42 rows):
`TORIRS_SCRIPTRUN_WIRELOG` for interfaces, clientscripts, messages, synths and fades, and the tick
log for loc_anims and player seqs. The tick log's tick is the wire log's plus one. The test boards
a skiff (config 2, 2x5), not the recording's raft (config 1, 1x3). Where the two hulls differ, the
2x5 values were checked against the intro boat's ([473]..[826]) and the 1x3 values are copied from
the recording but not run.

Files:
- `OSRS-Content/osrs-content/server/scripts/sailing/scripts/boat_frames.rs2` (new): every frame below.
- `boat_sidepanel.rs2`, `boat_facilities.rs2`, `boat_facility_actions.rs2`, `boat_gangplank.rs2`,
  `boat_cargo.rs2`, `port_tasks.rs2`, `sailing_charting.rs2`.
- `player/scripts/equip.rs2`: the crate-discard hook.
- `src/torirsserver/torirs_server_world.c`: the move-mode hook, the single sidepanel mount, and
  zero-based facility varbits.

## 1. Boarding ([1310]..[1315] Board-previous; [1598]..[1606] Board)

| tick | recorded | ours | verdict |
|---|---|---|---|
| L+1 | op, `[facing] coord=` plank | engine face | matched |
| L+2 | `minimap_state` 2, `busy` 1, fade out: `if_settext fade_overlay:message ""`, `if_opensub overlay_atmosphere fade_overlay`, `948(0,255,0,0,15)`, `if_sethide hpbar_hud:hp`, `2249(hpbar_hud:container)`, `minimap_toggle 2` | `~sailing_frame_out` sends the same, in the same order | fixed (ours boarded on the op with no fade) |
| L+3 | `message_game spam "You board your boat."`, the boarded varbits, teleport, `synth 10754`, `8776(name,1,"",1)`, `915(0)`, `if_opensub side0 sailing_sidepanel`, the event list (§3), `603(-100,896,-100,896)` | `~sailing_boarded` sends the same; the varbits come from the engine sync | fixed (ours had "You walk up the gangplank and board the boat.", no synth, 8776, 915 or 603) |
| L+4 | `minimap_state` 0, `busy` 0, the sails' steady anims (`..._down_offset`, then `..._down`), fade in `948(0,0,0,255,15)`, `minimap_toggle 0` | same (`~sailing_sail_visual_on` + `~sailing_frame_in`) | fixed |
| L+5 | `if_closesub overlay_atmosphere` | same | fixed |

- **Board (op1) at a dock with one boat moored** boards straight away ([1598]); ours used to open
  the boat selection first. That selection is now used only when several boats are moored there
  (unverified: no recording has two).
- **Double mount:** the engine's own aboard-change hook mounted the sidepanel a second time on
  the boarding tick, which also re-sent every event. Fixed: the hook now runs only when content
  has not mounted the panel itself (varbit 19151).

## 2. Disembarking ([1535]..[1538], [1767]..[1770])

| tick | recorded | ours | verdict |
|---|---|---|---|
| O (from the deck, no walk) | `sailing_boat_1_port`, `last_dock`, `last_standard_dock`; fade out as in §1 | `~sailing_owned_set_port`, then `~sailing_frame_out` | fixed |
| O+1 | `spam "You dock the boat at Port Sarim and disembark."` / `"...at the Pandemonium..."`, varbits cleared, teleport, helm `loc_anim ..._inactive01`, `synth 10754`, `603(128,896,128,896)`, `hint_arrow reset`, `8778("",1,"",1)`, `if_opensub side0 combat_interface`, stone0 events, combat title/category | `~sailing_disembarked`: the line (the dock's `inline_name`, else `nice_name`), the inactive helm anim, the synth, 603, `hint_stop`, 8778, the combat tab | fixed (ours had "You walk down the gangplank." and nothing else) |
| O+2 | fade in | same | fixed |
| O+3 | closesub | same | fixed |

## 3. The sidepanel's events (every mount: [1313], [1604], [549]; re-armed at [1611], [1765])

| component | recorded | ours before | now |
|---|---|---|---|
| `switch_button` (937:1) | 0..12 OP1 | 0..11 | 0..12 |
| `facilities_content_clicklayer` (937:25) | 0..16 (raft) / 0..12 (intro boat), OP1-4 | 0..63 op_all | 0..(12 + 4 per installed facility hotspot), OP1-4 |
| `facilities_content_npc_targetlayer` (937:26) | same range, TGTNPC | none | same range, TGTNPC (4096) |
| `crew_assignation_clicklayer` (937:20) | same range, OP1 | 0..127 op_all | same range, OP1 |
| `crew_assignation_back_button` (937:29) | 0..11 OP1 | 0..0 | 0..11 |
| `crew_content_clicklayer` (937:10) | 0..1 TGTPLAYER | 0..127 op_all+TGTPLAYER | 0..1 TGTPLAYER |
| `crew_radio_buttons` (937:38) | 0..3 OP1 | none (the click did nothing) | 0..3; sub 2 sets `crew_player_view` 1 ([535]) |
| the three tab buttons | no events (the cache's own) | -1..-1 OP1 each | none |
| `915(0)` | only on boarding and on taking the helm | on every mount | boarding and helm only |

- **Tabs ([530], [531], [541], [545]):** `sailing_sidepanel_tabs` set to 1, 2, 1, 0. Matched already.
- **Switch ([546]):** sub 11 sets `visible` 0 and mounts `combat_interface`. Matched.
- **Switch back ([549], `combat_interface:switch_button`):** mounts the sidepanel with the
  recorded event list. Matched now that the list is fixed.

## 4. The helm ([1319], [1395], [1613]; intro boat [473], [598])

| tick | recorded | ours | verdict |
|---|---|---|---|
| O | `facility_lockedin` 3, `set_interaction_mode` x2, the player stepped onto the helm tile (a walk), `915(0)` | lockedin 3 (engine), `915(0)`, `p_teleport` onto the helm tile (goes out as a walk step) | fixed except `set_interaction_mode`: Open |
| on the helm tile (O+1 after a step, else O) | `[facing] coord=` 3 tiles forward, seq `human_sailing_alpha_helm_raft01_active01` (raft) / `small01` (2x5), helm `loc_anim ..._active01`, synth 10792 (raft) / 10807 (2x5) | same (`~sailing_helm_take_anims`) | fixed (ours sent "You take the helm. Click the water to steer." and nothing else) |
| O+1 | `player_at_helm` 1, `helm_status` 2, move mode 0 -> 4, `8778("",0,name,1)` | at_helm/status from the engine sync (on O, not O+1); 8778 on O+1 | 8778 fixed; varbit tick and mode 4: Open |
| every 10 ticks | seq `..._active01_loop` + helm `loc_anim ..._active01_loop` ([1329], [1339], ...) | `[timer,sailing_helm_loop]` every 10 | fixed (61 -> 71 -> 81 -> 91 on our tick log) |
| leave, O ([1532], [1757], [589]) | `reset_interaction_mode` + 2x set (walk), lockedin 0, `[facing]` own tile, seq -1, helm `..._inactive01`, synth 10793 (raft) / 10808 (2x5) | same, without the interaction-mode packets | fixed (ours sent "You step away from the helm.") |
| leave, O+1 | `player_at_helm` 0, `helm_status` 1, `8778("",0,"",1)` | 8778 on O+1 | fixed |
| leaving by working another facility ([1390]) | the leave frame on O, set sails furled (full_to_down + 10833) | `~sailing_facility_release_helm` plays the leave frame; set sails furled with `vessel_control 3` | fixed |

## 5. The sails: the sidepanel's three buttons (937:25 subs 0..2)

Every recorded press ([481]..[826] intro boat, [1405]..[1441] and [1754] raft), and our run ([64]..[87]):

| press | from -> to (move mode) | transition seq (cloth `_offset`, then mast) | synth | ours |
|---|---|---|---|---|
| sub 2 raise | furled -> half | `down_to_half` | 10832 | matched |
| sub 2 raise | half -> full | `half_to_full` | 10836 | matched |
| sub 2 raise | full | "The boat is already going as fast as it can." | none | fixed (ours raised the engine tier silently) |
| sub 2 raise | reverse -> furled | steady `down` | none | matched |
| sub 0 set/un-set | furled -> full | `down_to_full` | 11089 (2x5) / 10831 (raft) | fixed (ours set half when the engine's tier was 1) |
| sub 0 | full -> furled | `full_to_down` | 10833 | matched |
| sub 0 | reverse -> furled | steady `down` | none | matched |
| sub 1 lower | full -> half | `full_to_half` | 10834 | matched |
| sub 1 lower | half -> furled | `half_to_down` | 10835 | matched |
| sub 1 lower | furled -> reverse | steady `down` | none | matched |

- **Order:** the transition and its synth go out on the press tick; the steady seq
  (`_full`/`_half`/`_down`) goes out on the next tick ([481]/[482], [1325]/[1326]).
- **What was wrong:** the engine's move-mode hook sent the steady seq on the press tick, which
  replaced the transition on the client. Ours sent no transition seqs and no synths at all.
- **Fix:** the hook now calls `[proc,sailing_sail_mode_changed](old, new)`, which plays the change
  and queues the steady seq for the next tick. This covers every cause of a mode change: a press,
  leaving the helm, or an arrival ([1527]).
- **Set (op2) on the sails loc ([1398]):** furled -> full with the same frame.

## 6. Cargo hold ([1390]..[1394], [1761]..[1765], deposit [1609]..[1611])

| step | recorded | ours | verdict |
|---|---|---|---|
| open | `5204` hold inv, `5205` side whitelist, inv updates, `synth 10907`, `917(-1,-1)`, `if_opensub mainmodal 943` + `sidemodal 944`, events `943:10` 0..239 OP1-6,OP10; `944:1` 0..27 OP1-6,OP10,DEPTH1,DRAGTARGET; `943:18` 0..4 OP1; `capacity`; `steelborder 227(frame,"Cargo Hold: Dawn Raider")` | same; the name is read from varbits 19149/19150 (`sailing_boat_name_descriptor_options`/`noun_options`) | fixed (ours: no synth or 917, op_all events plus ten extra buttons, tools 0..31, title "Cargo hold") |
| from the helm | the helm's leave frame on O, then the hold on O+1 | same (`~sailing_cargo_loc`) | fixed |
| close ([1394]) | whitelist 0, both closesubs | `[if_close]` zeroes 5205 | fixed |
| withdraw a crate ([1764]) | `human_pickuptable`, the crate into the weapon slot, `synth 10903`, closes, `synth 10908`; `carrying_cargo` 1 on the next tick; no message | same | fixed (ours sent "You pick up the ...") |
| deposit-held ([1610]/[1611]) | on the face tick `human_pickuptable` and the hands emptied; next tick `carrying_cargo` 0, "You deposit some cargo into the cargo hold.", `synth 10905` | same | fixed (ours: "You load the ... into the cargo hold.") |

**Unverified:** the 944 opensub shows type 3 in our wire log against the recording's modal. That
type is the engine's `if_openmain_side`. The open, close and withdraw were run; deposit-held was
compiled only.

## 7. Ledger table ([1555]..[1589] take, [1577]..[1580] discard, [1778]..[1782] deliver)

| step | recorded | ours |
|---|---|---|
| take, L+2 | hands full: mesbox "You cannot pick up any cargo as your hands are full." | `~mesbox` with the line (was `mes` "You need both hands free...") |
| take, L+2 | `cargo_taken`, `last_cargo_taken` = slot+1, `carrying_cargo` 1, `human_pickuptable`, the crate into the weapon slot, `put_down` delay 15 | same; `last_cargo_taken` is now 1-based, and op2 reads it back -1 |
| take, L+3 | objectbox "You pick up a crate of ship parts." zoom 400 | `~objbox_scaled(crate, "You pick up a <lowercase name>.", 400)` (was `mes`) |
| discard (the worn slot's Remove) | objectbox "If you put the cargo down, it'll get lost, and you'll have to get a replacement.<br>Are you really sure you want to discard it?", menu "Yes, discard the cargo." / "No, keep holding the cargo."; Yes: take undone, `carrying` 0, red "Your cargo has been confiscated by the S.W.A.T. team.", `smokepuff_large` h124 d10, `put_down` + `smokepuff` | new `~port_task_discard_held`, hooked from `~unequip` for `sailing_cargo` objs (ours unequipped the crate into the backpack) |
| deliver, L+2 | `last_cargo_taken` 0, `cargo_delivered`, `human_pickuptable`, `put_down` delay 15 | same; the Pandemonium hand-off is the quest's `~pan_cargo_delivered` |

Compiled, not run. The test does not take a task.

## 8. Charting from the deck before the quest ([1369])

Recorded: one game line, "You don't have any reason to try to chart that.<br>You need to complete
the Pandemonium quest first." Ours charted it. Fixed in `[label,sailing_chart_generic]`; compiled,
not run.

## 9. Facilities tab empty on Will and Anne's boat (config 4)

Cause: cs2 9026, 8765 and 8817 read the sail, helm and hull options as
`db_getfield(varp5117, column, varbit)`, a zero-based index. The hotspot varbits are one-based
(those scripts subtract 1). The engine sent `vessel->facility[...]` (1) unchanged. For the
single-option intro boat that pointed past its only row, so the tab was empty; for the raft and
skiff it pointed at the second option. 4407 never writes 19154/19155/19168 for either hull,
because the stock parts are option 0. Fixed in the engine sync (`facility - 1`). Not photographed.

## Not audited here (scope or time)

- Trim (oploc1 on the sails: [550], [1483] x3) and the gust frames ("You feel a gust of wind.",
  `vfx_wind_sail_*` map anims, synth 10839/10841/10842). These are `~sailing_try_trim`/`boat_wind.rs2`.
- The trader crew man's opnpc4 -> `sailing_menu` (72) and its button ([1040], [1050]).
- The world map orb ([1362]): generic, not sailing.

## Open

1. **`set_interaction_mode` / `reset_interaction_mode`** (the helm's tile mode `heading` and the
   world entity's `walk`) go out on every helm take and leave. Our server has no encoder or script
   command for them. Our client does handle them (`App_InteractionMode*`), so adding them is
   engine work: a packet encoder plus a command. Steering would then need the heading-mode click
   packet handled server-side.
2. **Move mode 4:** Jagex shows 4 ("at rest") on boarding and on taking the helm while furled
   (0 -> 4), and 0 after any furl. The engine has no such state and shows 0. The sails play the
   same either way.
3. **Helm varbit ticks:** recorded `facility_lockedin` lands on O and `player_at_helm`/`helm_status`
   on O+1. The engine's sync writes all three on the same tick.
4. **The 1x3 raft's values** (helm 10792/10793, down_to_full 10831, raft01 seqs) are copied from
   the recording. Our run used a skiff, so they were not exercised. The skiff's and sloop's helm
   seqs and synths are assumed to be small01/10807 (no recording); the sloop's sail synths are
   assumed equal to the 2x5's.
5. **The stone0 `if_setevents -1..-1 OP1`** that Jagex sends with every side0 mount is not sent by
   ours.
6. **`combat_interface`'s mount** re-arms its own nine buttons on every open (`combat_tab.rs2`
   `[if_open]`); Jagex sends none of them.
7. **The 944 side panel's open type** (§6).
8. **The quest's own boarding** (Will and Anne's boat, `pan_boat_board`): its line is "You board
   the boat." ([325]). It should go through `~sailing_frame_out` / `~sailing_boarded` /
   `~sailing_sail_visual_on` on T+1 / `~sailing_frame_in`. That is the quest's file.
