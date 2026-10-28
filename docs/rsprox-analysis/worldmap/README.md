# World map: open, refresh, close

What Jagex sends when a player opens the world map from the minimap orb, keeps it open, and closes it, on desktop and on the Android client, all at revision 241. The server side is `src/torirsserver/torirs_server_worldmap.c`. The root switch the phone needs is `ToriRSServer_GameframeSwitchTop` in `torirs_server_encode.c`.

## Sessions

| session | client | what it shows |
|---|---|---|
| index 4286, 4339, 4342, 4345, 4349, 4352 | desktop 241 | orb op 2 → open; `worldmap:close` op 1 → close |
| index 4367 | desktop 241 | the same open, from the quest overview ("Show on Map") |
| index 4398 | Android 241.4 (client type 7) | orb op 2 at [44], `worldmap:close` op 1 at [73] |

The index's RSProx cannot transcribe index 4398. It decodes the Android client with the desktop 241 protocol and stops at packet 2 (`CAM_TARGET_V3: Unknown type: 18`). Our own osrsm241 tables frame the whole recording: 1429 server packets and 125 client packets over 81 ticks, with 0 decode failures (`make -C src rsprox-multirev-check RECORDINGS=…`). The interface packets below were decoded from `MULTIREV_HEX=<PROT>` dumps against the osrsm241 layouts in `3rd/rsprot/packets/`. The client packets come from the same framing walk, done by a scratch dumper.

## Desktop (7/7 opens)

Click tick T is `IF_BUTTONX orbs:worldmap (160:55) op 2`. In the same tick, the server sends:

1. `RUNCLIENTSCRIPT worldmap_transmitdata (1749)` `(player tile, null, null)`. All three args are sent every time: varc 188 you-are-here, varc 1078 clue, varc 401 gravestone.
2. `IF_OPENSUB <live top>:floater ← worldmap (595)`, overlay.
3. `IF_SETEVENTS_V2 worldmap:toggles (595:21) 0..4 OP1`.

While the map is open, 1749 is sent again **every 3 ticks counted from the open, only when the tile changed**:

- 4286: 437 → 440
- 4339: 100 → 103 → 106 → 109
- 4349: 505 → 508 → 511
- 4342 and 4345: the player stood still and nothing was sent

Close is `IF_BUTTONX worldmap:close (595:38) op 1`, answered in the same tick by `IF_CLOSESUB <live top>:floater`. Nothing else is sent.

## Android (index 4398)

The phone does not float the map over `toplevel_osm` (601). For as long as the map is open, the root becomes `toplevel_display` (165).

**Open, [44]**, after `IF_BUTTONX orbs_osm:worldmap (897:50) op 2`:

| # | packet |
|---|---|
| 1 | `RUNCLIENTSCRIPT keyboard_hide (2239)` |
| 2 | `IF_OPENTOP toplevel_display (165)` |
| 3 | 34 × `IF_MOVESUB toplevel_osm:<role> → toplevel_display:<role>` |
| 4 | `IF_OPENSUB toplevel_display:orbs ← orbs_osm`, `IF_SETEVENTS orbs_osm:triggers 0..0 script_trigger`, `4717`/`4722` (map_clock) — interleaved after the `xp_drops` move |
| 5 | `1749 (tile, -1, -1)` |
| 6 | `IF_OPENSUB toplevel_display:displaymodal ← floater_blankmodal (594)`, **type 0** (modal) |
| 7 | `RUNCLIENTSCRIPT 5354` (popout layout) |
| 8 | `IF_OPENSUB toplevel_display:floater ← worldmap (595)`, overlay |
| 9 | `IF_SETEVENTS_V2 worldmap:toggles 0..4 OP1` |

`varp 1021 toplevel_temp` is re-sent at its current value (0x02000000) in the same tick. The same value went out at login, when the session also started on `toplevel_display` for the welcome screen.

**Close, [73]**, after `IF_BUTTONX worldmap:close (595:38) op 1`:

1. `IF_CLOSESUB toplevel_display:floater`, then `IF_CLOSESUB toplevel_display:displaymodal`
2. `RUNCLIENTSCRIPT settings_client_mode (3998) (1)`
3. `IF_OPENTOP toplevel_osm (601)`, then the 34 moves back, interleaved with 14 `IF_SETEVENTS` on `toplevel_osm:stone0..13`
4. orbs remount, triggers armed, `4717`/`4722`
5. `5937` (buff bar), `3998 (1)`, `5937`, `5354`

`varp 1055 chat_filter_assist` is re-sent at its current value. The same value went out at every switch to `toplevel_osm` (ticks 0, 36 and 73).

**The move list is fixed.** Every switch in the session moves the same 34 roles in the same order, whether each slot is occupied or not. That holds for display → osm at the welcome screen's close [36], osm → display [44], and display → osm [73]:

`chat_container mainmodal maincrm overlay_atmosphere overlay_hud sidemodal side0..side13 sidecrm pvp_icons pm_container orbs xp_drops zeah floater buff_bar stat_boosts_hud helper_content hpbar_hud popout tli_listener ui_highlights`

None of the three switches sends an `IF_RESYNC_V2`.

## What the server does now

| behaviour | before | now |
|---|---|---|
| phone orb (897:50) | unmatched, so the map never opened | opens |
| 1749 args | `(tile)` | `(tile, -1, -1)` |
| `worldmap:toggles` | never armed | armed 0..4 op 1 on open |
| marker refresh | every tick the tile changed | every 3 ticks from the open, when changed |
| phone open/close | floater inside `toplevel_osm`, so chat and HUD drew over the map | root switch to `toplevel_display`: 34 moves, `floater_blankmodal` modal, the recorded script calls, and back on close |
| quest overview "Show on Map" | its own three packets; no phone switch, and `worldmap_open` never set, so its close did nothing | `worldmap_open` command: the same path as the orb |

## The same rules outside the map

The phone's switches turned out to be the general shape of every root switch, so they are now the server's:

- **Every switch moves, it does not reopen.** `ToriRSServer_GameframeOpentop` with a root already up is `ToriRSServer_GameframeSwitchTop`: IF_OPENTOP, then the 34 IF_MOVESUBs by role name, and no IF_RESYNC_V2. Only a session's first root is built from gameframe.enum. This covers the Fixed/Resizable change (index 1835, rev 227, same shape: moves, stones, orbs, then `2358(0)` and `5937`) and the welcome screen's Play. The chatbox reset and settings-side re-init that `~gameframe_apply_mode` used to send were repairs for the old remount, so they are gone.
- **Orbs.** After every switch the orbs are opened again in the slot they were moved to, and `[if_open,orbs]` / `[if_open,orbs_osm]` arm `triggers` and run 4717/4722 with `map_clock` (every orbs IF_OPENSUB in 4339, 4398 and 1835 is followed by both). `map_clock` (varp 3079) was already sent every tick by `player_clock_vars`.
- **Stones.** `[if_open,<root>]` now fires on every root, and arms the 14 side stones with one table for every gameframe (stretch 4342, pre_eoc 4339, osm 4398): 0 1 3 4 12 13 op 1; 5 6 9 10 ops 1-2; 8 11 ops 1-4; 2 7 ops 1-6. Tutorial Island is excluded, since it hands the stones out itself.
- **Phone varps.** `toplevel_temp` (1021) is re-sent at its current value when `toplevel_display` comes up, and `chat_filter_assist` (1055) when `toplevel_osm` does.
- **Welcome screen.** A real client's login opens on `toplevel_display` with `welcome_screen` in displaymodal (enum `[toplevel_display]`, no orbs; the engine mounts the device's orbs). Play (`[if_button1,welcome_screen:play]`) closes it and switches to the saved layout in the recorded order. Logins that skip it:
  - still on Tutorial Island (index 4289, tutorial 1, gets no welcome screen; index 4288, tutorial 1000, does)
  - reconnects
  - sessionless bots and selftests
  - `TORIRS_CONTENT_TEST` (quest and raid gates)
  - `TORIRSSERVER_SKIP_WELCOME`

## Still different

- The welcome screen's two `motw_setmodel` (233) calls follow its IF_OPENSUB here; Jagex sends them just before the IF_OPENTOP.
- Jagex interleaves the stones' IF_SETEVENTS between the moves and the orbs remount after `xp_drops`; here they follow all the moves.
- A phone's login also gets `chat_filter_assist` at [0] in 4398; here it is sent when `toplevel_osm` comes up.
