# The phone's gameframe (toplevel_osm) against the Android recordings

Sessions: **index 4397** (Android 241.4, client type 7, a brand-new account:
Tutorial Island, root `toplevel_osm` from the first packet) and **index 4398**
(the same account later: welcome screen on `toplevel_display`, Play at [36],
world map open at [44] and close at [73]). Desktop controls: index 4289
(new account), 4339 (welcome screen).

Ours: the client run headless as a phone (`TORIRS_CLIENTTYPE=7
TORIRS_ON_MOBILE=1`, embedded server, `TORIRSSERVER_TRACE_OUT=2`), driven
through the agent API: a new account, and a past-tutorial save through
Play, the world map and its close.

## What was missing

The hotkey column (interface 892 `osm_hotkeys`) and the popout box under it
never drew. Three causes, all on the server:

1. Nothing mounted 892. `gameframe.enum`'s `[toplevel_osm]` block went from
   `popout` to `tli_listener`.
2. Its slot values were never sent or saved: varps 4504..4507 were
   `scope=temp, transmit=no` and nothing wrote them, so the builder
   (clientscript 7410) hid all five slots.
3. `popout_panel_mobile_enabled` (varbit 13981, varp 3417, also `temp`,
   unsent) was 0, and `~script5357` refuses a phone its popout strip without it.

## Root mounts

| # | recorded | ours before | now | verdict |
|---|---|---|---|---|
| 1 | 4397 [0]: `if_opensub toplevel_osm:hotkeys (601:40) <- osm_hotkeys (892)` between popout and tli_listener | absent | same place | fixed (`gameframe.enum`) |
| 2 | 4398 [36] Play: after popout's `IF_MOVESUB`, `if_opensub 601:40 <- 892`, then tli_listener's move | absent | same place | fixed (`ToriRSServer_GameframeSwitchTop`) |
| 3 | 4398 [44], [73]: the world map's two switches send no close and no second open for 601:40 | n/a | none sent; the mount stays in the server's table and the client's | fixed, both sides |
| 4 | every switch: orbs re-opened straight after `xp_drops`' move (4339 [5]; 4398 [36], [44], [73]) | after all 34 moves | after `xp_drops` | fixed |
| 5 | 4397 [0], 4398 [0]: `if_setevents popout:osm_buttons 0..4 [OP1]`, phone only | absent | sent from `~chrome_popout_login` | fixed; a tick late, see Open |

Interface packets of the three switch ticks, ours against 4398, packet for
packet (IF_OPENTOP, IF_MOVESUB, IF_OPENSUB, IF_CLOSESUB): Play 38/38 (the
recorder's 39th is their Lumbridge Guide hint box), world map open 38/38,
close 38/38.

## Values

| what | recorded | now |
|---|---|---|
| column, new account (4289 [0] desktop, 4397 [0]) | 19, 1, 25, 24, 3 | same |
| column slot 0 on leaving the island (4289 [1361], 4397 [976]) | 19 -> 22 | same (`~tutorial_finish`) |
| loadouts 1..3 | 22 23 8 9 21 / 24 23 8 9 1 / 6 1 15 9 24 | same |
| varp 3417 (4289, 4397, 4398 [0]) | long press 300, popout mobile 1, cannon 1 / 10 / 1 | same |
| `settings_mobile_minimenu_icon_enabled` on graduation | 0 -> 1 | same |

All twenty hotkey varbits are the account's, not the device's: 635 cached
sessions carry them, desktop included. A save from before they were kept (all
four words zero) is dealt the defaults once at login.

## The client

A mount a root switch does not move is kept and reopens when its root returns
(`App_RootMountsDetach` / `App_RootMountsReturn`). The reference keys a mount
by component and its open-top touches none of them (rev 239
`class415.method9502`); ours dropped them, so with the server matching row 3
the column would have vanished after the first look at the world map.

## Presses

No cached session presses a hotkey slot (0 `if_button` on `osm_hotkeys`), so
this part is built from the cache and the native client, not from a recording.

A slot is `osm_hotkeys:hotkeyN` with clickmask 6: the client SENDS ops 1 and 2,
and the cache's handler (clientscript `osm_hotkey_op`, 7423) only changes the
client's own copy of each var. Three things were missing:

| what | before | now |
|---|---|---|
| the server's answer to a press | nothing bound: Run lit up and the player kept walking; a toggle was gone at the next varp or login | `[if_button1/2,osm_hotkeys:hotkeyN]` does 7423's arm on the server: the run orb's, quick-prayers' and the special attack's own handlers, and the same write for each setting (`player/scripts/osm_hotkeys.rs2`). Their words are kept and sent |
| the accordion | Swap-to Loadout reverted at relog | ops 3..5 copy the loadout into the column, op 6 is the lock |
| `minimenu_setblockmode` / `setaddmode` / `setprioritymode` | unrouted no-ops | kept per property and applied to every built menu, as native `Minimenu::CanAddOp` does (osclient 216): a row goes when a property it has is blocked or none is addable. Tile marking is `setblockmode(2, 0)` + `(4, 0)`, so the tap's only row is "Mark tile" |
| the popout launcher (XP tracker, loot, hiscores) | a phone's buttons are `popout:osm_buttons`, button at sub 2n and icon at 2n + 1; only the desktop `popout:buttons` was bound, so they did nothing | `[if_button1,popout:osm_buttons]` opens panel n for sub 2n (`interface_chrome/scripts/chrome_panels.rs2`) |
| client ops on a phone | offered only with the Shift KEY held | also under the simulated shift the cache holds for a phone (`setsimulatedshiftactive`, native `Minimenu::IsShiftDown`); a client op is a tap's default when nothing else is |

Checked on the running client as a phone: Run changes the pace (3.7 -> 1.8
tiles/s); in tile mode a tap marks the tile and does not walk, and leaving the
mode gives Walk here back; with tagging on a long press on an npc offers "Tag"
and "Tag-All" while a tap still talks; Swap-to Loadout 2, tap-to-drop, tagging
and "show tagged" are all there after a relog. A desktop's menus are unchanged.

Inferred, not recorded: the server runs clientscript 7579 (installs the npc
"Tag" ops) when tagging is toggled and 6685 when "show tagged NPCs" is. Both
take the new value and are called by nothing in the cache, like the other
server-run `*_handle_event` scripts.

## Open

- **`pvp_icons` (90) on a phone.** Mounted in the burst after `hpbar_hud` in
  both Android sessions and in none of 234 rev 241/242 desktop ones, as
  `if_sethide icon_container false`, the open, `if_sethide wildernesslevel
  true`, `if_sethide icon_container true`, `pvp_icons_layout (385)(true)`; the
  same five packets recur mid-session (4397 [65], [663]). What triggers the
  repeats is not established by two sessions. Not built.
- **Customise (the accordion's op 2) and the Hotkeys settings page.** The
  client does nothing on Customise; Jagex's server opens the page. Not
  recorded, not built, so a slot still cannot be reassigned.
- **A tagged npc draws nothing at the recorded defaults.** The outline and tile
  styles (varbits 14170, 14171) are 0 in every login burst and the Settings
  page that turns them on is not server-backed here.
- **The Settings page writes only the client's copy.** A hotkey's write sends
  its whole word, so another setting on the same word changed in Settings that
  session goes back to the server's value (varps 3416, 4503, 4660).
- **Hotkey 20** (the hitpoints orb's cure) and the special attack's second op
  have no handler on this server to stand for.
- **Minimenu properties not applied:** visual feedback (0), interface (3) and
  cancel (8). Visual rides every entity op in the native table, so blocking on
  it would empty the world; the other two would take buttons and Cancel away.
- **A brand-new account's values arrive a tick after the root** (`[login,_]`
  runs on tick 1), where Jagex has them in the burst. A returning account's
  are in the burst.
- **Burst order shared with every layout**, desktop too: Jagex opens the tabs
  as side1..6, 9, 8, 10..13, 7, the orbs a second time, then side0; ours opens
  side0..13 in order and the orbs once. `xp_drops` is recorded in every burst
  and ours mounts it only when enabled. Varps go out before `IF_OPENTOP` in the
  recording and after it in ours.
