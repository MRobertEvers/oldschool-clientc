# ToriRSEd status (branch v3-maped, after phase 5)

The design is `docs/MAPED_DESIGN.html`; the per-phase ownership brief is
`docs/MAPED_IMPLEMENTATION.md`. This page says what is built against each
design section, what is not, every open issue the four phases left (with the
file and line to start from), and how to run the editor. Read it first when
picking the editor up again.

Commits: phase 0 `86bc224de` (design + brief), 0b `f2998b6af` (picker and New
square in the brief), 1 `a0eeeb676`, 2 `ffdc6be36`, 3 `aa8798eae`, 4 `ceabbc9c2`,
5 (this commit: the owner's follow-ups after the first live-window run on
2026-10-30 -- dock layout, server catalog panel, record copy, spawn duplicate,
brush footprint, spawn ghosts, History panel, slider skin).

The plan for the editor as a game editor, measured against the editors
people know, is docs/MAPED_ROADMAP.md.

## Escape clears every selection (2026-11-08)

Owner: "Escape should clear the spawn selection. Why does spawn and tile
selections use a separate selection and not respect escape? They should."
The editor keeps three selections because they are three different things
with three owners: the loc selection is `ui->tools.locs` (document refs,
shared with every window through the selection state); the tile is the
latched `ui->tools.tile_*` plus the panel's `sel_kind` TERRAIN (the subject
of Apply, the Spawns list's square, the Place player ghost); the Spawns
selection is the Spawns panel's own rows (`SpawnsPanel.selection`, one
square's rows by name and tile, since the list is one square's and a row
has no id). The active player's mark is a fourth, the App's
(`g_app_player_preview.selected`). What was wrong is that Escape's Deselect
(edpanel_hotkeys.c) knew only the first: it cleared the locs and the
rectangle and left the rest -- the spawn selection and the tile, selected
from the world or the list, had no key that let them go. Now Deselect clears
all of them (`Editor_Spawns_ClearSelection`, `tile_valid`, and the App drops
the player's mark before running it), `panel_sync_primary` lets a TERRAIN
subject go to NONE when the tile went, and the Place-tool ghost drop waits
until the spawn selection is empty too. editor_panels_test
`test_spawn_escape`. docs/MAPED_HOTKEYS.md "Deselect / cancel".

## Lanes: the switch made legible (2026-11-08)

Owner: "It is not clear how to switch lanes... I seem to be stuck in a
scratch lane, and it is not clear how to create a scratch lane from the
fresh cache." Nothing was broken: a Switch (and Scratch lane's switch)
writes `build/lanes/lane.ini`, which every launch then obeys over the
manifest's `lane=`, and the running editor stays where it is. What was
missing was the panel saying so in the user's terms -- it spoke of "the
next boot" and a file name, never of quitting and launching again, never of
which lane the launches were pinned to or why, and Enter on a lane row did
nothing. So (edpanel_publish.c, edpanel_statusbar.c, the lanes wire):

- **The boot line**, a label under Lane / Base in the Lanes panel, always
  says what the launches do and what to do about it:
  - a switch waiting: "Next launch opens: working. Quit and launch the
    editor again to switch; this session stays on scratch-261108a."
  - pinned by an earlier Switch: "Every launch opens: scratch-261108a (set
    by Switch; the manifest asks for working)." -- the way back is a Switch
    to the named lane.
  - nothing staged: "Every launch opens: working (the manifest's lane).
    Switch or Scratch lane changes that."
  The server tells the panel which lane the boot ASKED for before lane.ini
  had its say: `FACT_LANE_INFO` gains a second trailing string `asked`
  after `next_boot` (`ToriRSMapEd_SetLaneAsked`, called by the daemon's
  main and `ToriRSMapEd_EmbedStartLane`; `struct EditorUiLane` gains
  `pinned` and `asked`; `torirsmapedctl lane` prints both).
- **The buttons say what they do**: "Switch at relaunch" (was "Switch...";
  tip: the picked lane opens at the next launch, quit and launch again),
  "Scratch lane" (tip: a clean mirror of main as it is now, opens at the next
  launch), "Lane from main" (was "New lane"; the button column is narrow,
  a longer label is cut). The lane list's row for
  the staged lane carries " | next launch".
- **Enter on a lane row switches**: the first activation picks the row, the
  second (Enter again, a double-click) is its Switch, as the Scenes list's
  second activation enters the scene. The old handler only refreshed.
- **Every sentence names the action**: "Switched: working opens from the
  next launch on. Quit and launch the editor again to go there; this
  session stays on scratch-261108a"; "Scratch lane scratch-261108b made
  from main; it opens from the next launch on. Quit and launch the editor
  again to go there"; the chip's tip "Quit and launch again to switch to
  working". The word "boot" is gone from the panel.
- Tests: editor_scenes_ops_test (the boot line both ways, the row marker,
  Enter twice switching to main and back, `pinned` / `asked` after the
  restart). A live re-point of a running server is still not built (the
  6.5 open item stands): the switch is honest about being a relaunch.

## Spawn preview (2026-11-08)

Spawns panel, "Show spawns in the world" (Shift+F6, off by default): every
spawn of the open squares stands in the scene. Npcs are held on the first
frame of their ready pose (`WorldEntityFacet_Animation.held`), ground items
sit on their tiles, suppressed rows are translucent, and the play account
stands where the server's account list says, in its own gear. The View tab
filters npcs, ground items, the active player, generated / edited /
suppressed rows, and turns tile markers and the wander range on and off.

A Select click on a model or marker selects it (Shift adds, Ctrl toggles, Alt
removes); the minimenu's "Select spawn" / "Select player" works under any
tool. The entities carry `editor_preview`, so they offer no game ops and an
offline world click does not walk the player. Table:
map_editor_ghost.h `MapEditorSpawnPreview`; landing: app_map_editor.c; rows and
filters: edpanel_spawns.c. `TORIRS_EDITOR_SPAWN_PREVIEW=1` ticks the box for a
headless photograph.

Rows from the `.spawn` files quests and skills keep under their own `configs/`
come from a read-only TEXT document, `spawns:m<x>_<z>`, which the server builds
on open by walking `server/scripts` (skipping `areas/world` and `areas/edited`).
They are listed marked `~`, have a cyan marker and a Quest and skill rows
toggle; an edit suppresses them and adds an edited row, as for generated rows.
The server cuts it from a snapshot of every scripted `.spawn` file, walked
on a worker thread when the server opens and again, in the background, once
the snapshot is 30 s old and a square asks (editor_host_local.c
`scripted_spawns`); a change to such a file shows after the square is
reopened, once the next walk has landed. Until 2026-11-08 every square open
walked the 58k entries of server/scripts on the UI thread (2-3 s a time). The toggles persist as `spawn_view` in
`editor_prefs.ini`. The Place player ghost and the active player also work on
the web lane.

## How to run it

### Quick start (2026-11-02)

**Desktop, one command:**

    ./launch run osrs242-mapeditor

builds the client and the live lane's `torirsserver` and `js5_server` (the
profile's `build=`), and opens the editor window with its embedded editor
server on the **working lane**: `build/lanes/working/content`, a
copy-on-write mirror of `OSRS-Content/osrs-content` made on the first run
(the manifest's `[editor:boot] lane=working`; see "Editor lanes" below).
Every save lands in the lane, never in the tree; **File > Publish...**
(Ctrl+Shift+U) writes the lane's changes into the tree, another lane or a
new one. **RUN** (Ctrl+Return, or the Live panel's RUN) saves every dirty
document, builds the packs and the client cache into the lane's live dir
(`build/lanes/working/live`; `build/live/default` for a `lane=main`
session), then reloads the lane's running server -- or, when the lane is down, launches its game server and js5_server
on what it just built -- and starts the lane's play client (a second window,
logged in as `maped43620`), or reloads the one it already started.

A RUN shows a **progress bar** while it runs: at the foot of the main window
and in the Live panel, with the step (save, build, reload, play client),
what the build is on, and the time left estimated from the last RUN that
finished on this lane (`build/live/default/run_times.ini`). The first RUN
into a new lane has no estimate and says so. `torirsmapedctl run` prints the
same rows. How long a RUN takes, measured 2026-11-02 (see "RUN speed"):

| RUN                                  | before   | now      |
|--------------------------------------|----------|----------|
| warm lane, server up (edit + reload) | ~5 s     | ~3 s     |
| warm lane, server down (launch too)  | ~7 s     | ~5 s     |
| brand-new lane (first RUN ever)      | ~90 s    | ~25 s    |

**Web, one command:**

    ./launch run osrs242-mapeditor-web      # then open http://localhost:8088/

starts the torirsmaped daemon (the editor server: content tree, lock, BUILD,
RUN, lane `build/live/default`), a js5_server for the page's cache and
io_server, which serves the page (from `build-web/`, which the launcher's
`make -C src web` refreshes) and proxies `/maped/` to the daemon. The bare
URL is the whole URL: io_server sends `/` to the editor's command line
(`--page-query`). The page is a client of the native daemon, always -- a page
has no filesystem, so there is no in-page editor server any more (owner,
2026-11-02); a page whose io_server reaches no daemon says so in one line
with the command above and stops. In the tab: the editor and its dock; Pop
out opens a tab per panel on the same daemon; RUN builds, reloads or
launches the lane, and opens the play tab (the lane's own io_server on
43622, logged in as `maped43620` with the fixture save -- no hand-typed `ws=`
or `js5_url=`: the lane publishes the whole URL). Why not the daemon's
WebSocket: see "The web editor's link to the daemon" below.

**What RUN never does:** launch or reload anything after a failed step. A
failed packs or client-cache build stops the RUN with the failing tool's own
line in the RUN message ("the packs build failed: sscompile:
quests/.../wanted.varp:4: ... ; nothing was launched or reloaded") and its
last lines repeated at the end of the Live panel's log; Launch and Play
refuse a lane whose last build failed or that was never built ("the lane has
not been built (no <live>/servpack/pack.manifest): press RUN"). A launched
server that exits before it answers shows the last lines of its log in the
panel, and its last line in the status ("exited (status 1) before it
answered: torirsserver: no server pack at ...").

**Where the logs are:** `build/live/default/logs/{server,js5,io,play}.log`
(the lane's processes); `./launch logs osrs242-mapeditor-web [torirsmaped|
io_server|js5_server]` (the web run's services); the editor's Console panel
and the Live panel's log (build lines, RUN outcomes, a dead server's log
tail).

**The lock message.** One editor server writes a content tree at a time:
`<content_dir>/.editor-session.lock` names its holder (`pid`, `since`,
`cmd`). A second editor on the same tree opens READ-ONLY and says who holds
it -- in the Console at boot, in the Inspector's first row, and in RUN's stop:

    read-only: /.../OSRS-Content/osrs-content is held by pid 43893
    (torirs --manifest manifests/manifest_osrs242_mapeditor.ini, since 16:22)
    -- close it, or run this window against the daemon

Close that process, or point this window at the daemon that holds the tree
(`server=tcp`, `osrs242-mapeditor-tcp`). A lock whose pid no longer runs (a
crash, a kill -9) is moved aside and taken at the next start, with one
console line ("held a stale lock (pid N ... no longer runs); removed it").

**Launcher options for a private run** (how the checks below were made, and
how to run a private build against a scratch tree): `--binary <client>` runs
that client instead of building one; `--set SECTION.KEY=VALUE` sets one
profile key for the run (`override:editor:boot.content_dir=<mirror>`,
`service:torirsmaped.binary=<private daemon>`, `service:torirsmaped.live_dir=
<scratch lane>`, `service:io_server.root=<private web build>`); the
generated manifest is then `build/manifests/<profile>.set.ini`, never the
profile's own.

All commands from the repository root. `OSRS-Content` is the content tree the
editor edits; in this worktree it is a symlink into the owner's checkout and is
read only by rule, so editing sessions that SAVE need a writable tree (a real
checkout, or the mirror `src/maped/test/live_e2e/mirror.py` makes).

**Native, one window, embedded editor server** (the default):

    make -C src
    src/torirs --manifest manifests/manifest_osrs242_mapeditor.ini

or through the launcher, which builds the client and prepares the lane first:

    ./launch run osrs242-mapeditor          # one window, embedded ToriRSMapEd
    ./launch run osrs242-mapeditor-tcp      # the window attached to a shared daemon (started for you, port 43610)
    ./launch run osrs242-mapeditor-view     # a viewer window of that session (start -tcp first)

(`profiles/osrs242-mapeditor*.ini`; `./launch list` names the osrs239 twins.
The preparation runs the content tree's contract checks and refuses a tree
that fails them; `TORIRS_SKIP_CHECKS=1 ./launch run ...` boots on the tree as
it stands.)

The manifest's `[editor:boot] server=embed` runs ToriRSMapEd inside the
process over `content_dir`. The panel set (menu bar, tool rail, terrain,
palettes, square picker, New, locs, spawns, inspector, catalog, **Preview**,
console, builders, interface and CS2 tools, Live) is in the same window; the
View menu opens the hidden ones. **Preview** (module "preview", View >
Preview, Ctrl+Shift+P; src/editor/panels/edpanel_preview.c) is the model view
of the Catalog's pick as a panel of its own: its well fills the panel, so a
bigger panel shows a bigger model; it opens beside the Catalog when the
Catalog opens or a record is picked (unless you closed it this session), and
a popped-out Preview follows the pick the main window makes. The 3D view is itself a panel, **World**: View > World
hides or shows it, its title bar moves it and its grip resizes it like any
other panel.

**Panel furniture: Close, Pop out, edges.** Every View-menu panel, World
included, carries two buttons at the right end of its title bar: the X
closes it (exactly View > <panel> off; the View menu row then reads
"<panel>  hidden" in its right column, and the row or the panel's hotkey
brings it back) and the arrow beside it **pops it out** into a window of its
own. Every edge and corner of a panel's border resizes it (the grip in the
bottom-right corner stays as the visible handle); a moved edge snaps to the
grid, and a resized panel keeps its box in the saved layout like a dragged
one. A docked panel that closes or pops out gives its space back: the column
closes up, and with World gone the panels spread over as many columns as fit.

**Pop out** (`Editor_UiPanelPopOut(ui, "<module>")`, `Editor_UiWorldPopOut`)
opens a second process of the same binary and manifest -- the launch
service's `view` verb, `--editor-view [--editor-panel <module>] --maped
tcp://127.0.0.1:<port> --maped-group <Client> --camera <pose>` -- joined to
this window's Client, so documents, selection, tool, level and every state
relay stay mirrored. Every popped-out window runs the full command-panel
session in solo layout (`Editor_UiSetSolo`): it hears and publishes the
Client's shared state like the main window -- STATE_TOOL, STATE_EDIT_LEVEL,
STATE_SELECTION, STATE_CATALOG_PICK and **STATE_TOOLS** (key 7: brush
radius/falloff/step/strength, underlay, overlay, overlay none/shape/rotation,
flags, place shape/rotation, view level/solo; `ui_tools_pack` in
src/editor/editor_ui.c publishes whenever a window's tools moved, a window's
boot publishes nothing, and its own echo -- or a fact ordered before it --
never undoes a newer change), and the boot STATE_SYNC hands a late window
the Client's state as it stands. World pops out as the World panel alone
(`--editor-panel world`), looking exactly where the panel looked: its
clicks run the Client's tool with the Client's brush and palette, its title
names them ("World - Raise - level 1"), and the viewer's thin toolbar sits
over it (Open panels turns it into a full window). Any other module pops
out as that panel alone, filling the window (no menu bar, no world drawn;
the world is still loaded around the camera's square so App-bound panels
answer). Open in new view (the picker) still opens a plain viewer, which
does not edit. The
main window hides the panel; View > <panel> shows it here again (pop in);
the other window runs until it is closed. One console line each way.
Several panels may be out at once, one process each: each is a full client
(cache, renderer, an EDIT world of the open squares) -- roughly the main
window's footprint again per window. It works under BOTH server modes: with
`server=tcp` the window dials the daemon (World may use the daemon's own
SPAWN_VIEW); with the default `server=embed` the first pop-out opens a
loopback listener on the embedded ToriRSMapEd -- 127.0.0.1, an ephemeral
port, the daemon's own accept/pump code (src/torirsmaped/torirs_maped_net.c)
polled from the client's frame loop (`EditorView_AppFrame`) -- and the
window dials that. On the web Pop out opens a tab per panel
(`torirsSpawnEditorView(camera, group, maped, panel)`, `&panel=<module>` in
the tab URL); it needs a page booted against a daemon (`maped=`), since one
tab cannot reach the ToriRSMapEd embedded in another.

**Environment the editor client reads** (beside the manifest):

- `TORIRS_EDITOR_LAYOUT` -- where the dock layout is saved (below). Unset:
  `editor_layout.ini` in the working directory (IndexedDB on the web);
  empty: no layout is read or written (what every headless harness sets);
  else that path. Same rule as `TORIRS_PREFS` for `preferences.ini`.
- `TORIRS_EDITOR_HOTKEYS` -- where the rebound hotkeys are saved
  (`editor_hotkeys.ini`, docs/MAPED_HOTKEYS.md "Action ids"): the same rule
  -- unset: `editor_hotkeys.ini` in the working directory (IndexedDB on the
  web); empty: none read or written (set it empty in headless runs, as the
  layout); else that path. Read at boot through the task queue, written when a
  binding changes, flushed at shutdown with the layout. Only non-default
  bindings are in it.
- `TORIRS_SIM_HOTKEY="<frame>,<chord>[,hold=<frames>][;<frame>,<chord>...]"`
  (src/main.c) -- press a chord at a main-loop frame, for a photograph:
  `<chord>` is `[ctrl+][shift+][alt+]<name>` with LibToriRS_KeyCodeName's
  names in any case (`z`, `f1`, `lbracket`, `pageup`, `escape`; `enter`,
  `page_up` accepted). The key goes down at `<frame>` and up at
  `<frame>+hold` (default 2), modifiers held throughout; IsKeyDown is true on
  the press frame only, IsKeyHeld through the span; entries may overlap. It
  writes both the platform key arrays and, for keys the cache keymap has, the
  OSRS arrays. A malformed entry exits 2 with a `sim_hotkey: bad entry` line;
  every press logs one `sim_hotkey:` line, OPT builds included.
- `TORIRS_HOTKEY_DEBUG=1` -- prints `hotkey_edges: frame N down=... up=...
  held=...` on every frame with a key edge: what the editor's dispatcher
  (src/app/app_editor_hotkeys.c) sees.
- `TORIRS_PREFS`, `TORIRS_PLUGINS=0` (photographs and checks run with plugins
  off), `TORIRS_PRESENT_BMP` + `TORIRS_PRESENT_BMP_FRAME` (write one frame as a
  BMP: the photograph recipe, never a desktop screenshot),
  `TORIRS_EDITOR_HOVER` (park the hover, src/app/app_map_editor.c),
  `TORIRS_EDITOR_LEVEL=<level>[,<x>,<z>[,<yaw>,<pitch>,<zoom>]]` (pin a
  level once at boot, optionally with the free camera on absolute tile x,z of
  that level; same file),
  `TORIRSMAPED_LIVE_DIR` (the live lane, below; unset = `build/live/default`
  under the repo, so RUN works from the default profile; empty = no lane).
- `TORIRS_EDITOR_POPOUT=<module>[,<module>...][@<frame>]` -- for a
  photograph: at main-window editor frame `<frame>` (default 300) pop each
  named panel out, as its title-bar button does
  (src/app/app_editor_view.c). `TORIRS_EDITOR_RESIZE=<module>,<l|r|t|b>,<dx>,<dy>[@<frame>]`
  presses that panel's border on that edge, moves it by (dx, dy) and lets
  go (default frame 200). A popped-out window inherits its leader's
  environment, TORIRS_PRESENT_BMP included: `TORIRS_EDITOR_VIEW_BMP=<dir>[@<frame>]`
  makes each write `<dir>/view_<module>.bmp` at its own frame (default 400)
  instead.
- `TORIRS_MAPED_STATE_DEBUG=1` -- one `maped state: Client <n> sets|heard
  key <k> [<count>] <values...>` line per shared-state fact a window sets or
  hears (src/editor/editor.c): run the main window and a popped-out one with
  it to watch the relay in both logs (a popped-out window's log is
  `build/launch/views/view<n>.log` under the main window's working
  directory). Keys: 1 selection, 2 tool, 3 pinned level, 4 catalog pick,
  5 view square, 7 shared tools, 10.. cameras, 39 view self. A popped-out
  window never inherits `TORIRS_SIM_HOTKEY` (the leader's simulated keys).
- `TORIRS_EDITOR_OPEN_CMD` -- the Triggers panel's and the catalog's "Open
  in editor" command, `{path}` (the absolute path, shell-quoted) and
  `{line}` placeholders, e.g. `code --goto {path}:{line}` or
  `subl {path}:{line}`. Unset: `code --goto {path}:{line}` when `code` is on
  PATH, else `open {path}` (macOS) / `xdg-open {path}`. Run through
  `/bin/sh -c '<cmd> &'` (src/editor/editor_host_local.c
  `Editor_HostLocalOpenInEditor`), never waited on; the command line is
  logged to the console. An environment variable rather than a manifest key
  (`[editor:boot] open_cmd=`) because src/bootmanifest is not the editor's.
- `TORIRS_EDITOR_PREVIEW=<kind>:<name>[@<frame>][;<kind>:<name>@<frame>...]`
  -- for a photograph: at main-window editor frame `<frame>` (default 60)
  the Catalog picks record `<name>` of `<kind>` (`loc`, `npc`, `obj`) as a
  click on its row does (published to the Client; the Catalog shows it found
  by name, its row selected), e.g. `loc:oaktree@250;npc:hans@700`. A
  popped-out window ignores it (src/editor/panels/edpanel_preview.c). With
  `TORIRS_EDITOR_VIEW_BMP` set a popped-out window also moves an inherited
  `TORIRS_EXIT_BMP` to `<dir>/view_<module>_exit.bmp`, so it no longer
  overwrites the leader's exit photograph.
- `TORIRS_EDITOR_CATALOG=<kind>:<find>[@<frame>][,every=<frames>][,pick=<row>@<frame>...][,next=<frame>][,edit=<frame>]`
  -- for a measurement: at main-window editor frame `<frame>` (default 60)
  open the Catalog on `<kind>` and type `<find>` one character every
  `<frames>` frames (default 8), click result row `<row>` at each pick
  frame, Next page / Edit record... at theirs. `TORIRS_CATALOG_STATS=1`
  prints a line per catalog round trip (ms) and per frame
  (src/editor/panels/edpanel_catalog.c; "Catalog performance" below).
- `TORIRS_EDITOR_TRIGGERS=<subject>[,file][,record][,add=<kind>[:<op
  text>]][,check]` -- for a photograph: once the layout ran, dock the
  Triggers panel over the right half of the world on record `<subject>`
  (with `file`, `<subject>` is a path under server/scripts and the panel
  lists that file); `record` opens the Record panel beside it, `add=` runs
  Add trigger, `check` runs Check (src/editor/panels/edpanel_triggers.c).
- `TORIRS_EDITOR_ACCOUNTS=show[,tab=<n>][,account=<name>]` -- for a
  photograph: once the layout ran, dock the Accounts panel over the column
  under Terrain, open tab `<n>` (0 Position, 1 Stats, 2 Vars, 3 Quests, 4
  Items) and select `<account>` once the lane's list arrived
  (src/editor/accounts/accounts_panel.c).

**The plugin rail is the client settings.** An editor window -- the main one
and every popped-out panel or view -- keeps the platform's rail, but it
carries one stone, "Client settings", and that stone opens the client
settings page directly: the roster (Manage Plugins), the page's Back and
every other way to the roster land on that page, and no plugin page is
offered beside the editor's dock (`app_plugin_editor_page`,
src/plugin/torirs_plugin_panel.u.c; owner, 2026-11-02).
`TORIRS_PLUGIN_RAIL_DEBUG=1` prints the stones a window offers.

**The saved layout.** Every panel's box, whether it is open, whether the user
placed it, and View > Snap to grid persist across launches in
`editor_layout.ini` (format and policy: src/editor/editor_layout.h). It is
read at boot through the App's task queue (the default layout waits for it,
so nothing flashes at its default place first) and written a few frames
after any change settles -- a drag or resize released, a View toggle, Reset
layout, Snap to grid, a window resize -- and once after boot. Both trips are
`ToriRS_IO_QueueFileRead`/`QueueFileWrite` items (src/editor/
task_editor_layout_io.c): on the desktop platform_x_io.c reads/writes the
file; on the web platform_web_io.js hands the same items to
`host.readClientFile`/`host.writeClientFile` (src/web/torirs_hostio.js),
which keep it in IndexedDB. A file made for the same surface and scale puts
every panel back exactly; on another surface only the panels the user placed
keep their boxes (pulled back onto the surface if it shrank) and the rest are
laid out for the new surface. View > Reset layout returns to the default (not
to the file) and saves that. To move a panel by hand, set `user_placed = 1`
and its `x/y/w/h` (chrome pixels at scale 1) in the file.

**Native, against the daemon** (several windows on one tree):

    make -C src torirsmaped torirsmapedctl
    src/build_opt/torirsmaped 43610 --content-dir OSRS-Content/osrs-content --repo-root . \
        [--view-binary src/torirs --view-manifest manifests/manifest_osrs242_mapeditor.ini] \
        [--live-dir <dir>]

then boot the client with `server=tcp` in `[editor:boot]` (host/port default
localhost:43610). `torirsmapedctl` drives the same daemon headless: `status`,
`squares`, `open`, `doc`, `new`, `select`, `undo`/`redo`, `save`, and since
phase 4 `text-replace`, `tile-height`, `live-attach`, `live-detach`,
`live-cheat`, `build packs|client`, `run`, `play [seconds]` (start the lane's
play client, launching the lane when it is down, and wait until the server
lists it), `play-restart [seconds]`, `play-stop`, and for spawns and records:

    torirsmapedctl spawn-edit [gen:]<kind> <name> <x> <z> <level> [count] to <kind> <name> <x> <z> <level> [count]
    torirsmapedctl spawn-move [gen:]<kind> <name> <x> <z> <level> [count] to <x> <z> [level]
    torirsmapedctl cfg-set <type>:<name>[@<overlay>] <key[#n][@line]> <value|->

A spawn row is `<kind> <name> <x> <z> <level> [count]`: kind `npc`/`obj`
(`-npc`/`-obj` for a suppression row), the name a pack symbol or `#<id>`,
ABSOLUTE tiles, a count for an obj only. Same square and kind is one
SPAWN_EDIT in place; anything else a delete and an add; one undo step either
way. `gen:` edits a row of the GENERATED file (areas/world): it is suppressed
and the new row added to the edited file. `cfg-set` is one CFG_FIELD on a
record block (`-` removes the field).

The named areas and square collections (the AREAS document,
`<content_dir>/editor/areas.txt`):

    torirsmapedctl areas
    torirsmapedctl area-set <name> <level|all> <x0> <z0> <x1> <z1> [<x0> <z0> <x1> <z1>]... [colour <0..7|->] [note <text|->] [rename <new name>]
    torirsmapedctl area-remove <name>
    torirsmapedctl collection-set <name> [<square key>...] [note <text|->] [rename <new name>]
    torirsmapedctl collection-remove <name>

`area-set` upserts by name (ABSOLUTE tiles, inclusive, up to 16
rectangles; quote a name with a space); an existing area keeps its colour and
note unless given, `rename` renames it in the same step. `collection-set`
keys are the picker's (`m50_50`, `ported/<lane>/maps/m16_101`); keys replace
the list, none keeps it. Each verb is one undo step, unsaved until `save`.
`areas` prints the records. `TORIRS_EDITOR_AREAS=dock,select=<area>,goto`
(and `tall`, `tab=collections`, `collection=<name>`) docks the Areas panel, selects,
moves the camera and filters the picker for a photograph
(src/editor/panels/edpanel_areas.c).

CFG keys: `<type>:<name>` is the `[name]` block of `configs/all.<type>`;
`<type>:<name>@<overlay>` is that block in a SERVER OVERLAY, `<overlay>` a
path under `server/scripts` ending `.<type>`
(`npc:giantspider1@areas/lumbridge/configs/lumbridge.npc`,
`npc:giantspider1@_explicit/configs/server_fields.generated.npc`). Either is
block-scoped (the 10 MB generated overlay is never loaded whole) and saved
back over its block with every other byte kept. `fields:<type>.ini` is the
read-only TEXT document of a type's field register.

**Viewer window** (a second camera on the same session):

    src/torirs --manifest manifests/manifest_osrs242_mapeditor.ini --editor-view \
        --maped tcp://127.0.0.1:43610 --maped-group <client id> --camera x,z,level,yaw,pitch,zoom

or "Open in new view" in the picker (the daemon spawns it when started with
`--view-binary/--view-manifest`, else the client's launch service does).

**Web** (the Quick start's `./launch run osrs242-mapeditor-web`, by hand):

    make -C src servers torirsmaped && make -C src web
    src/build_opt/torirsmaped 43610 --content-dir OSRS-Content/osrs-content --repo-root . --live-dir build/live/default &
    src/build/io_server --manifest manifests/manifest_osrs242_mapeditor.ini --root build-web \
        --maped-port 43610 --page-query 'args=--manifest,manifests/manifest_osrs242_mapeditor.ini&js5_port=43595'
    # plus a js5_server on cache.osrs241, port 43595; then open http://localhost:8088/

**The web editor's link to the daemon (2026-11-02).** A web boot of an
editor manifest attaches to the native daemon whatever its `server=` says
(src/web/torirs_host.js `editorDaemonBoot`: GET `/maped/ping` first, then
`--maped tcp://127.0.0.1:<[editor:boot] port>`); the in-page embedded server
is unreachable on the web (`Editor_HostOpenMapEdEmbed` refuses under
`__EMSCRIPTEN__` with the command to run). The bytes go through io_server:
`POST /maped/open|io|close`, `GET /maped/ping` (src/ioserver/io_maped_proxy.c;
off unless `--maped-port N`, loopback and that port only), over SYNCHRONOUS
XMLHttpRequests from `editor_host_remote.c` (`maped_web_io`). Not the
WebSocket the daemon also speaks: the editor's client half is request/reply
(`await_reply` waits for its answer inside the C call), and a page built
without ASYNCIFY or threads receives a WebSocket's bytes only after it
returns to the event loop -- which a waiting C call never does, so every
open/load timed out (the earlier `?maped=ws://` view tabs were
compile-checked only; they could not have worked). A sync XHR is the one
call a page can block in; the proxy keeps it on the page's own origin (no
second port, no CORS). Every window costs one ~1 ms request a frame while
idle. Pop-out tabs (`?editor_view=1&maped=tcp://127.0.0.1:43610&panel=...`)
use the same proxy. The wasm still links the embedded server's code
(torirs_maped*.o, 187 KB of wasm objects, about 3% of the 6.3 MB torirs.wasm; it is unreachable on the
web, not compiled out -- open issue).

A web client PLAYING on a live lane (a RUN goes live in it, row 16): serve the
page with io_server (`--root` the web build, `--boot-root` a tree whose
manifest's `[net:boot]` names the lane's game port and whose `dir=` resolves to
a dat2 cache, e.g. the lane's), and open
`/?args=--manifest,manifests/manifest_osrs242.ini,--connect,127.0.0.1:<game>,--user,<name>,--pass,<letters+digits>&ws=ws://127.0.0.1:<game>/&js5_url=ws://127.0.0.1:<js5>/&idb=<fresh name>`
(add `&env=TORIRS_CACHE_INVALIDATE_DEBUG%3D1&env=TORIRS_LOC_CFG%3D<id>` to
see the reload). The lane's server builds its world, and answers the daemon's
control link, only after that first login.

A LAUNCHED lane serves that page itself when it has an io_server
(`TORIRSMAPED_LIVE_IO_BIN` and `TORIRSMAPED_LIVE_IO_MANIFEST` set -- the
latter now only switches io_server on; there is no preopen): it starts
`io_server --root <io_root> --port <io_port> --boot-root <live>/play/web`,
the tree holding `manifests/manifest_<rev>.ini` (the play manifest, `dir=../cache`,
the lane's game port), `cache` (a link to the lane's cache: io_server opens only a
cache inside its boot root) and `revconfig` (a link to the repository's), and
FACT_LIVE_PLAY carries the play URL
(`ToriRSMapEd_LaneWebPlayUrl`: `http://127.0.0.1:<io>/?args=--manifest,manifests/manifest_<rev>.ini,--connect,127.0.0.1:<game>,--user,maped<game>,--pass,lane<game>run&ws=ws://127.0.0.1:<game>/&js5_url=ws://127.0.0.1:<js5>/&idb=maped<game>`;
`torirsmapedctl play` prints it as `web play:`). **Checked (2026-11-02):** that
URL, opened in headless Chromium against a launched lane, logged in as the
lane's account (server log `maped43633 logged in`, the link attached) and drew
both RUN spikes, with the build-web/ of 2026-10-22. **The web editor's RUN:**
its Live panel sends LIVE_PLAY browser once (src/editor/live/live_panel.c), so
the daemon never starts a native client for it, and after a RUN whose account
is not on the server it opens the play URL in a tab (`window.open`; a
blocked popup is logged); its Play does the same. That half is
compile-checked (emcc -fsyntax-only of live_panel.c, live_client.c and
torirs_maped.c) but was not run in a browser: build-web/ was not rebuilt
(it is a shared output). A lane with no io_server answers "no play tab: the
lane serves no web page".

**The launched live lane** (edit, RUN, the live server and clients update):

    torirsmaped ... --content-dir <writable tree> --repo-root . --live-dir <live dir>

`--live-dir` (or `$TORIRSMAPED_LIVE_DIR`; the embedded editor defaults it to
`<repo>/build/live/default` when unset, and an empty value means no lane) binds the native BuildRunner and
LiveLink (the repo root, content dir and live dir made absolute first: the
builds run `make -C <repo>/src`, and a manifest's relative `repo_root=..`
once built the lane's client cache from a base cache that was not there --
a 29-byte master index, and a play client that logged in and crashed).
Everything a build writes lands under the live dir:
`scripts/` (base script pack), `scripts_lanes/`, `servpack/`, `cache/` (the
client cache the lane's js5_server serves), `map_archives.txt`. The lane's
launch settings default from `ToriRSMapEd_LaneDefaults`
(src/torirsmaped/torirs_maped_build.c) and can be overridden with
`TORIRSMAPED_LIVE_SERVER_BIN`, `_JS5_BIN`, `_IO_BIN`, `_IO_MANIFEST`,
`_IO_ROOT`, `_BASE_CACHE`, `_GAME_PORT`, `_JS5_PORT`, `_IO_PORT`
(src/torirsmaped/torirs_maped_live.c `live_lane_from_env`). The Live panel
(View menu) attaches or launches, runs BUILD and RUN, follows a player's
varps, sends cheats and teleports to the selection. The pack build allocates
ids into the content tree's `pack/<ns>.alloc` (that is what allocation is), so
the content dir of a lane must be writable. A launched lane's server keeps its
accounts and saves in `<live>/saves` (`TORIRSSERVER_SAVES`), skips the welcome
screen (`TORIRSSERVER_SKIP_WELCOME=1`), and every launched process writes
`<live>/logs/{server,js5,io,play}.log`.

**The play client** (RUN opens a game client, or reloads the one it opened):
each lane has one, started and tracked by the editor server like the lane's
servers (src/torirsmaped/torirs_maped_live.h "The play client"), so a RUN
from any window, the Live panel's **Play** / **Stop** and
`torirsmapedctl play | play-restart [seconds] | play-stop` reach the same one.
It is `<repo>/src/torirs` (`TORIRSMAPED_LIVE_PLAY_BIN`; "" = none) on
`<live>/play/manifest.ini`, written at each start from
`manifests/manifest_<rev>.ini` (`_PLAY_MANIFEST`) with the cache dir
`<live>/play/cache` (filled over the lane's JS5), `[net:boot]` the lane's game
port and every relative path made absolute, run as `--js5 --js5-host
127.0.0.1 --js5-port <js5> --js5-fallback-port 0 --user maped<game port>
--pass lane<game port>run` (`_PLAY_USER`, `_PLAY_PASS`), its first save
`test/quests/fixtures/fresh_lumbridge.ini` copied to
`<live>/saves/<account>.ini` (`_PLAY_FIXTURE`) so it does not start on
Tutorial Island. It inherits the editor's environment minus the capture and
editor keys (`TORIRS_SIM_HOTKEY`, `TORIRS_EXIT_BMP`, `TORIRS_MAX_FRAMES`,
`TORIRS_BMP_SERIES`, `TORIRS_PRESENT_BMP*`, `TORIRS_SCREENSHOT*`,
`TORIRS_EDITOR_*`, `TORIRSMAPED_*`) with `TORIRS_PREFS=<live>/play/
preferences.ini`; `TORIRSMAPED_PLAY_ENV_<NAME>=<value>` reaches it as
`NAME=<value>` (headless: `TORIRSMAPED_PLAY_ENV_SDL_VIDEODRIVER=dummy`). RUN,
after the reload: no play client -> it is started; one on the server's player
list -> "reloaded in the running client (<account> at x,z, no relog)" (it took
CACHE_INVALIDATE and the REBUILD); exited or logged out -> started again;
still logging in -> left alone. A lane that is DOWN when RUN reaches the
reload is launched on what the RUN just built (no reload owed) and the client
started; its login builds the main torirsserver's world, which is when the
link attaches. A lane that cannot launch (no server binary) keeps the old
failure plus "Launch the lane first". The Live panel's play line reads `play:
not started | starting <account> | logged in as <account> at m50_50
(3222,3218) | <account> logged out | exited -- <why>`; Play starts it (or
restarts a dead or logged-out one, launching the lane when it is down), Stop
stops it; Detach of a launched lane stops it with the servers.
`TORIRS_EDITOR_LIVE=show` opens the Live panel with the editor (photographs).

The whole loop, scripted, is `make -C src maped-live-e2e`
(src/maped/test/live_e2e/run.sh, below), and with the real server and the
real play client `make -C src maped-live-e2e-play` (run_play.sh, below).

## Built, by design section

| § | section | built | not built |
|---|---|---|---|
| 1 | What exists, and the verdict | ToriRSMapEd kept as the authority; documents generalised | — |
| 2 | Architecture | authority + mirrors + facts + group state; EditorHost, BuildRunner and LiveLink vtables; embed, tcp and ws transports; the editor modules name no `struct App`: the App-bound panels (catalog, inspector, areas) ask the client through `struct EditorUiAppServices` (src/editor/editor_ui.h, `user_data` + nullable function pointers like `Builder_Services`), filled by `app_map_editor_ui_services` (src/app/app_map_editor.c) before they register, and the viewer's App glue is a unit of the App layer (src/app/app_editor_view.c) | browser host binding for the content tree (IndexedDB / io_server `/boot/`) |
| 3 | Documents and commands | kind-keyed documents (square, spawn, cfg incl. server-overlay blocks `<type>:<name>@<overlay>`, iface, text incl. `fields:<type>.ini`, and AREAS: kind 5, key `areas`, `editor/areas.txt`, below); Editor_Cmd per brief §2 with TILE, LOC, SPAWN, SPAWN_SUPPRESS, CFG_FIELD, CFG_BLOCK, IF_FIELD, IF_COMPONENT, TEXT_SPLICE, and SPAWN_EDIT (kind 9: one row rewritten in place, wire `before row, after row, string before_text`, a row being `u32 is_obj, u32 suppress, string name, u32 x, u32 z, u32 level, u32 count`); v2 wire encoding; undo stack grows on demand | DOC_DELETE; DOC_CLOSE unloading; SAVE_AS (reserved, UNSUPPORTED) |
| 4 | The editing World | WorldBuilderOps with STATIC (pure refactor) and EDIT (resident build maps, placement handles, tile-local flush); `[world:boot] layout=`; per-tile derive | local flush gaps (occluders, minimap icons, LINK_BELOW rebakes); ghost under EDIT still uses runtime loc changes |
| 5 | Windows, views, cameras | viewer windows as processes (`--editor-view`), STATE_CAMERA publish/follow, SPAWN_VIEW, web viewer tab; **popped-out panels** (`--editor-panel <module>`: one panel filling a window of the session, `Editor_UiSetSolo`), the launch verb's `panel=` key, a new view adopting its Client's pinned level from a boot STATE_SYNC (`EditorView_ExpectSync`); the embedded ToriRSMapEd's on-demand loopback listener (src/torirsmaped/torirs_maped_net.{h,c}, shared with the daemon's reactor), so Open in new view and Pop out work under `server=embed`; tests: views_embed_test (the listener end to end with a TCP client joining the embed's Client, the replayed level cap, the relay both ways; the panel spawn body and launch argv), editor_view_test.js (`panel=`) | layer toggles not read by the painter; see open issues (World, views) |
| 6 | Server state and protocol | protocol v2, HELLO capabilities, frame/state-key reservations, group state relay | — |
| 7 | Chrome | TREE, VLIST, TOOLBAR, SLIDER (settings-bar track 2852/2855/2857, gem knob 2858, label column <= 30%), RADIO, MODAL, TOOLTIP, CODE, GRAPH, splitter docking; grid snap on drag/resize release (`ToriRSChrome_SetSnap`, 8 px at 1x, 16 at 2x, edge snap within one cell); 28 baked skin slots; DOM rendering; hollow panels (`ToriRSChrome_PanelSetHollow`: title bar, rails and grip drawn and hit, the body neither painted nor hit, `ToriRSChrome_PanelBodyRect`); the dock layout persisted per device in `editor_layout.ini` (`TORIRS_EDITOR_LAYOUT`; codec src/editor/editor_layout.c, IO tasks src/editor/task_editor_layout_io.c, debounce `app_map_editor_layout_tick`; desktop file / web IndexedDB through host.readClientFile/writeClientFile) | DOM multi-panel dock layout; a drag still in progress at quit is not saved |
| 7b | World panel | the 3D view is the registry module "world" (editor_ui.c `ui_world_register`, first chrome panel so it draws under the rest): titled, resizable, hollow; in the View menu; the default layout docks it over exactly the region the old world rect was; `Editor_UiWorldRect` is its body; the App cuts the world widget to it through the reference letterbox (`App_MapEditorWorldLetterbox` answers UITREE_HOST_GET_WORLD_LETTERBOX: the 3D pass draws in the body, the rest of the widget box is the black band) and restates `world_emit_desc` (`App_MapEditorWorldView`), so the paint, picks, hover, brush footprint, ghosts and selection marks all follow it; hidden World = no world drawn or picked; part of the saved layout; viewer windows (panels hidden) keep the whole frame; **Pop out / Pop in** (title-bar arrow, `Editor_UiWorldPopOut`; View > World pops it back in) under embed (loopback listener) and tcp, the main window's columns taking the freed space; **Close box** and **edge resize** on World and every other View-menu panel (chrome `poppable`, `closable`, `ToriRSChrome_PanelEdgeAt`); tests: editor_panels_test `test_popout` (button press, camera and pinned level handed to the App, hide, View row "popped out", pop in, refusal kept with its reason, the column closing up, Close box + View row "hidden", left-edge and corner resize with the grid, World's top edge, a solo window); photographed 2026-11-02 under the default embed profile | the box origin of the world widget is taken from last frame's desc (the letterbox request carries only its size); the panel has no maximise / dock-to-edge gesture |
| 8 | Asset catalog | server-side index of packs, allocs, compacks, config blocks, overlays, interfaces, cs2/rs2 headers, spawns, maps; CATALOG_QUERY/RECORD (a record's fields in the pack's merge order: all.<type>, `.generated.` overlays, authored overlays, path order within a rank); mtime refresh; the Catalog panel queries it (every kind, settled find, paged list, record view, Edit record..., Copy to new; edpanel_catalog_model.c). **Preview panel** (2026-11-02, owner: "the previewer in the catalog should be its own panel connected to the catalog panel"; module "preview", src/editor/panels/edpanel_preview.c, registered by the Catalog, right after it in the View menu and the dock order): the Catalog lost its model well and Reset view (it is that much shorter) and the Preview holds them -- a subject row, the MODELVIEW filling the body (`stretch`, so the dock and a resize give the well every spare pixel; `ToriRSChrome_ModelViewSetBox`), Reset view. Subject (`Editor_Preview_Subject`): the pick (a multiloc's chosen rung, `Editor_Catalog_PreviewId`), else while nothing is picked the primary selected loc. The App renders it at the well's size (src/app/app_map_editor.c `app_map_editor_preview_update`): one 640x512 sprite for every well (`EDITOR_PREVIEW_SPRITE_W/H`; GL3 reuses an atlas tile only at the same size, so per-size sprites would fill the 2D atlas within a few resizes) drawn centred and clipped (`ToriRSChrome_ModelViewSetSprite`), the projection scaled to the well (`EditorPreviewCamera_ProjectionScale`: the reference 120x96 framing, bigger; 384 at the reference, so the calibrated fit no longer cuts the top off trees and npcs) and the model's vertical middle on the view's; a new subject gets the default pose and a fresh fit; a loc or npc config the window never loaded is asked for once (a popped-out window's world need not hold it), and missing models are asked for once per subject, not every frame. Orbit keys as before while the well has the focus (`app_modelview_focused` is any focused MODELVIEW, which is now the Preview's; the editor hotkeys stay gated). **Opening**: when the Catalog opens or a record is picked while the Preview is closed, it opens against the Catalog's left side, top-aligned (`Editor_UiPanelOpenBeside`), unless the user closed it in this session (in-process memory, never the layout file; a relayout with no room, Tab, a pop-out or the editor closing is not a user close). **Relay**: the pick is now heard as well as published (STATE_CATALOG_PICK, `catalog_on_state`, with the shared tools' echo rule; the name asked of the catalog on the next tick), so a popped-out Preview -- or Catalog -- follows the main window's picks; the eyedropper (Shift+I, `Editor_Catalog_Adopt`) publishes too. Tests: editor_panels_test `test_preview` (registry order, furniture, View row and chord, the Catalog has no model view, auto-open beside the Catalog, the selected-loc subject, a picked row, Reset view, the well growing 200 px with the panel and nothing scrolling, a peer window solo on "preview" hearing the pick and naming it, the echo rule, Pop out / pop in, the Close box holding through a pick and a Catalog reopen, a layout file without "preview" loading as the one with it does), editor_ghost_preview_test (projection scale), editor_hotkeys_test (`view.preview`). Photographed 2026-11-02 at 1280x800: the Catalog with oaktree picked beside the Preview; a popped-out Preview showing hans after the main window picked him **Catalog performance** (2026-11-02, owner: "Selecting and searching is very slow"): no request stats the tree any more -- a request answers from the index and, past the 2 s interval, starts a freshness sweep the service pass advances 2 ms a slice; the index warm-builds in 6 ms slices from the server's first service pass; the last query's hits are held (a page flip tests nothing, typing on tests only the held hits); a record view re-reads only its own changed files. Numbers, recipe and test: open issues "Catalog performance" | a Catalog opened in the first ~15 s waits for the rest of the warm build; a sweep that finds a change merges in one ~80-100 ms step; see open issues (Catalog performance, Preview panel) |
| 9a | Active level | menu bar Level row after View: `Auto 0 1 2 3 Above`, the pressed button is `tools.edit_level`, Above (pinned only) draws every level; keys PageUp/PageDown step and clamp 0..3 (from Auto, from the plane under the pointer), Ctrl+0..3 pin, the same chord again = Auto; World panel title "World - level N"; a pin caps the view at itself (`vis_level` follows, levels above and roofs not drawn), Auto draws every level as before; `edp_level_*` in src/editor/panels/edpanel_common.c; synced as STATE_EDIT_LEVEL (another window's pin moves the cap too, menu bar tick); with a level pinned the hover, every tool's click (select included), the brush footprint, ghosts, rectangle and inspector use the PINNED plane's tile under the pointer (`app_map_editor_pin_hover` re-picks against that level's height field when the pick met another plane; `Editor_PanelHoverTile`); the free camera's eye shifts by the height between planes on a pin; an empty upper level takes paint, heights, locs and spawns (flushed locally) | see open issues (World, views) |
| 9 | Terrain tools | brushes with radius/falloff/step, palettes with shape thumbnails, flags, level radio, rectangle select/copy/paste/fill/clear; brush footprint under the pointer (`Editor_Terrain_BrushFootprint`, same weights as the stroke, falloff shown as wash intensity, follows a drag; >121 tiles as nested washes) | — |
| 9b | Square picker and New square | atlas (storages, world-map areas incl. Unmapped instance regions, colour, spawns, flags), picker GRAPH with area tabs, zoom, hover, search, open/shift-open/right-click; New modal (coordinates, storage maps/ or a lane with lane creation, template empty/copy/flat) -> DOC_CREATE. **World map under the grid, zoomed like 595** (2026-11-02, reworked the same day after the owner's notes): the page's area drawn as the CS2 world map draws it, by the client's own renderer (game/rs_worldmap_render.c `RS_WorldMapRender_RegionPixels`: a region baked into a caller buffer with the icons its locs carry, same loads/waits/request table as `RegionSprite`; `SetFrameBudget`; a used-up geography request is forgotten so the next bake of that region asks again at once). game/rs_worldmap_overview.{h,c} keeps every region at one pixel per tile for the process (~16 KB each, never evicted) plus a pool of close-up bakes at the zoom's own 2..8 px a tile (`RS_WorldMapOverview_BakeScale` = ceil(px/64), 595's range; 96 slots, 64 MB, least recently used out, four 8 px regions a poll), and cuts pictures of a zoom's GLOBAL pixel space (struct RS_WorldMapOverviewTile: square column m starts at floor(m*64*zoom/64), rows from the world's north edge) through each area's compositemap records -- box-filtered below one pixel a tile, the close-up bake pixel for pixel above it. **Zoom**: continuous (`Editor_Picker.zoom`, logical px a square) from the whole page in the well (`Editor_PickerZoomMin`; 4 px for a small page, never below 0.25) to one square filling it (at least 256, `Editor_PickerZoomMax`); the wheel is x1.25 a notch about the cursor (the map point under it stays put); left-drag pans (past 3 px it is a pan, never a click); a still click selects and opens (shift adds); a double-click opens in place; with the well focused (a press focuses it) `+`/`=` and `-`/`_` zoom about its middle and the arrows pan an eighth of it (new `TORIRS_CHROME_EVENT_GRAPH_KEY`; Escape releases); Fit frames the page's squares edge to edge; axis labels thin to 1/2/5/10/20/50/100 so neighbours never touch; the grid and the map share the global pixel space so they agree to the pixel at every zoom. **Tiles** (editor/editor_worldmap.{h,c}): the well is drawn as 128x128 logical-px pictures, each rendered once per zoom, area and page into one of 80 scene sprites (`UITREE_SCENE_EDITOR_PICKER_MAP_BASE` 0x4C000000 + slot, least recently drawn reused) -- every picture the same size. THAT was the owner's "blanks out when I zoom in": the old single picture changed size on every zoom, and the GL3 renderer packs sprites into its shared 2048-square 2D atlas and reuses a sprite's place only for the same size, so after a few zooms the inserts failed and the map silently stopped drawing (src/platform/platform_sdl2_renderer_gl3.c `gl3_sprite_upload_rgba`); pictures are logical px (stretched by the chrome scale) for the same reason. A pan renders only the tiles it brings in; tiles render nearest the middle first, 3M source tiles of work a tick (at least one); until a zoom's tiles are in, the last complete zoom is drawn scaled under them (the transition), so a zoom never flashes black and never stalls a frame. Regions still loading: polled every tick, tiles drawn meanwhile redrawn when the bake serial moves (at most every 10 ticks). **Over the map**: the map element icons (the bakes' loc icons and the compositemap's own list, the latter first so labels survive a crowded view) through `app_mapfunction_scene_id` (config and sprite loaded on demand, like 595), fading in from 40 to 64 px a square, and area labels by text size (large from 6, medium 16, small 40, a grey-to-white fade); outlines: the selection (yellow, the whole cell; a plain click replaces it, shift adds), unsaved (accent), created (green), and the squares in THIS window's world (`EDITOR_PICKER_OPEN_HERE` = loaded and inside the window's own view frame, a cyan 2 px inset) -- the owner's "small selection squares that persist" were the old white open-set outline of every square the mirror had ever opened; the server's open flag no longer draws one, and the view frames are drawn under the selection so a frame never hides it. Squares the page does not show are dimmed; a shown square no area maps keeps its own colour; black elsewhere; a faint grid from 8 px a square. **Full map** page (beside the surface in the Area dropdown, `EDITOR_PICKER_TAB_FULL`): every square of the atlas at its true coordinate, every world-map area composed onto that one grid (worldmap_area("*") = `EDITOR_UI_MAP_AREA_ALL`; `RS_WorldMapOverview_PollAll` / `RenderTileAll` / `IconsAll`, the main area first, where two areas place a region the first keeps it), so dungeons, caves and instance regions show their imagery where their squares are; same search, open, New square and selection. Services on `EditorUiAppServices`: `worldmap_area`, `worldmap_poll` (with the zoom), `worldmap_render_tile`, `image_publish`, `worldmap_icons` (src/app/app_map_editor.c, not filled under `__EMSCRIPTEN__`). A click on an EMPTY square opens New square prefilled with it. No services (headless harness, web): the picker as before; Unmapped page: no map. Timing (osrs241, this Mac, private OPT build): cold full surface 0.73-0.80 s first ask to last bake (about 0.4 s of it one frame reading the 1632 regions' tiles); Lumbridge at 595's 100% (256 px a square) 80 ms; the Full map's whole grid ~1 s cold. `TORIRS_WORLDMAP_STATS=1` prints each poll's regions to come and elapsed ms (stderr). `TORIRS_EDITOR_PICKER=<area>[,fit][,zoom=<px>][,at=<x>:<z>][,click=<x>:<z>]...[,new]` (applied 20 ticks in, after the layout; the short `<area>,<zoom>,<x>,<z>` still works) sets a page, zoom, centre and clicks for photographs. Tests: editor_worldmap_test (31: whole region and chunk records, box filter, 48 px a square drawn exactly over its square, 512 px sharp blocks, close-up bake pixel for pixel, the map's edges equal the picker's at real zooms, composition incl. a shown square's own colour), editor_panels_test `test_worldmap` (fake App: area asked once then after the retry, the poll is the visible squares, every picture 128x128 in a slot below 80, quiet ticks reuse pictures, stale tiles redrawn when regions land, slop vs drag-pan vs click, click A then B leaves one selection mark at B drawn over the frame, the open-here mark gone when the world moves, shift-click then double-click opens in place, `+`/`-`/Left on the focused well, the zoom transition keeps the map under the cursor then leaves no stand-in, 48 px a square has pictures with map in them, icon centred on its tile and opaque at 64, fading at 52, gone at 20 with the large label kept, empty-square click -> New square, Full map extent = every atlas square and m27_83 drawn at 27,83, Unmapped and no-services fall back) and `test_picker` (wheel x1.25 about the cursor, zoom range ends, Fit framing, label spacing). Photographed 2026-11-02 at 1x with the Squares panel enlarged: whole surface Fit, Lumbridge at 256 px a square with icons and labels, one square filling the well, the owner's 48 px view at 49,43, two successive plain clicks, the Full map whole and its underground band (z 140-159) at 40 px **Trackpad gestures** (2026-11-02): the world map takes a pinch and a two-finger scroll. A pinch zooms continuously by its exact ratio about its centre (a 2x spread is 2x, no 1.25 steps); a two-finger scroll pans in the platform's own direction (natural scrolling kept) and never zooms; ctrl+wheel (Cmd on a Mac) zooms continuously by 1.25^(fraction of a notch); a mouse wheel still steps x1.25 about the cursor, and the keys still work. The input layer carries a per-frame pinch (`pinch_scale`/`pinch_x`/`pinch_y`/`pinch_from_wheel`) and the wheel's exact deltas (`scroll_dx`/`scroll_dy`/`scroll_px_per_notch`/`scroll_trackpad`/`scroll_flipped`) as `TORIRS_CMD_INPUT_PINCH` (29) and `TORIRS_CMD_INPUT_SCROLL` (30). Sources: macOS through an AppKit monitor (`platform_macos_gesture.m`: magnify, plus hasPreciseScrollingDeltas scrolls in points, because sdl2-compat does not forward SDL3's pinch); the web through a page listener (ctrl+wheel as exp(-deltaY/100), Safari gesturechange); SDL multi-finger gestures measured from the fingers' spread; fractional SDL and Win32 wheel deltas. The chrome routes them in `ToriRSChrome_WheelFrame` (new `TORIRS_CHROME_EVENT_GRAPH_PINCH` / `GRAPH_SCROLL`); only a GRAPH well takes a pinch or a precise scroll, everything else and the game get the integer wheel they always got, an unclaimed desktop pinch is dropped. Sims: `TORIRS_SIM_PINCH="f,scale,x,y"`, `TORIRS_SIM_SCROLL="f,dx,dy[,trackpad]"`. Tests: editor_panels_test `test_picker_gestures`, sdl_gesture_test (18). **Maximise**: a double-click on any editor panel's title (400 ms / 4 px on the chrome clock) fills everything below the menu bar, drawn over every other panel; another restores the box; a title drag or edge resize past 4 px restores first; Reset layout unmaximises; the layout file records `maximised = 1` with x/y/w/h as the restore box (editor_panels_test `test_maximise`). | picker Delete only clears the square (no DOC_DELETE); a NEW square has no `pack/5_maps.pack` line, so BUILD client cache cannot import it yet; see open issues (Square picker world map) |
| 10 | Placement tools | loc multi-select, nudge (cross-border as one undo), rotate, reshape, duplicate; spawn place/delete/suppress, spawn multi-select and Duplicate (Ctrl+D); translucent npc/obj placement ghost with footprint (`map_editor_ghost.c` spawn ghost, Place click writes the spawn row via `Editor_Locs_PlaceSpawn`); Move tool outlines every selected loc's destination; **spawn edit form** (Spawns > Edit tab: kind, name -- pack symbol, `#<id>`, or a display name the catalog resolves, refused when unknown -- abs X/Z, level, count, Suppressed; Apply = one undo step "edit spawn"/"move spawn" via `Editor_SpawnEditPlan`: SPAWN_EDIT in place, delete+add across a square or kind, suppress+add for a generated row; a multi-selection takes each changed field, X/Z/Level as an offset); **spawn move** typed or "Move: click a tile" (the next world click, `editor_panel.c`), the destination footprint and wander square previewed under the pointer; **wander preview** ("Show wander range", default on: each selected npc's wanderrange square filled and maxrange leash outlined on its level, also around the npc spawn ghost; values from the Record panel when it holds the npc, else the catalog's merged record, else 5/7; `MapEditorWander_Rects`, drawn in `app_overlay.c`); orphan `-name` rows listed and editable; ctl `spawn-edit`/`spawn-move` | arrow nudges commit per press (no preview); the move preview is footprint + wander square, not a translucent npc model; a delete+add edit's undo re-adds the row at its section's end |
| 11 | Loc interaction builder | `.interaction` model, loc emitter (oploc/oplocu/aploc), step templates, sscompile Check | Check runs synchronously on the UI thread |
| 12 | NPC interaction and dialogue builder | npc emitter, dialogue graph to nested if/else, multiloc/multinpc shell editor; **Record panel** (Catalog > Edit record..., Spawns > Edit npc record...): one loc/npc/obj record over every file that states it -- `configs/all.<type>` and each server overlay the catalog lists, as block-scoped CFG documents -- every field resolved to the layer that wins it (rank 0/1/2, path order, `param=<name>,..` per name), plus every key of `fields/<type>.ini` the record states nowhere; Set field writes where the field is stated or "override in <layer>" (opening the block there with CFG_BLOCK if needed), Remove field shows the layer beneath; one undo step "edit record"; `editor_cfg.h` Editor_CfgRecord; ctl `cfg-set` | (6.6) STATE_VAR_PREVIEW is honoured by every multiloc / multinpc resolve (VarPManager_SetPreview); the dialogue graph's head model in the chat-box preview is unbound |
| 13 | Varp and varbit editor | editor_vars bit map, overlap check, naming; editor_alloc mirrors ss_allocate.py | allocator and vars not wired to the protocol or a panel; constants; live ::getvar/::setvar |
| 14 | CS2 and the interface editor | `.if`/`.compack` codec (982 interfaces byte exact), component tree, inspector, hooks, CS2 run/trace/watch, CODE editor with `cs2 compile`, App mount/pick/refill | IF1 type 8 not mountable; IF_FIELD values over 190 bytes read only; cs2 compile synchronous |
| 15 | Save, save as new, export, pack | atomic text saves, FACT_SAVED; SAVE_AS of a loc/npc/obj record (fresh id in pack/<type>.alloc, .client line, every field; one undo step; catalog and shell editor "Copy to new...", `torirsmapedctl cfg-copy`); BUILD packs (`make torirsserver-packs` into the live dir); BUILD client cache (`cachepack pack --incremental`, only changed archives written, reference table bumped, plus saved squares by archive) | Save as new for squares (DOC_CREATE copy covers it), interfaces and builder subjects; export/import lane; incremental repack of interfaces, CS2 and other assets in BUILD client cache; scripts lint and cs2 compile as BUILD kinds |
| 16 | RUN and the live lane | RUN = SAVE_ALL -> build (packs and client cache at once since 2026-11-02, `TORIRSMAPED_BUILD_RUN`; see "RUN speed") -> CTL_RELOAD -> play client -> FACT_RUN_DONE/LIVE_STATUS, with FACT_RUN_PROGRESS rows (a bar in the Live panel and at the foot of the main window), stop at first failure, pending set kept on a failed reload; control link (0xED) with gate ticket; script hot reload (shadow table, swap at the tick boundary, old table freed when no frame holds it); sequence/healthbar/struct config groups live; square reload with one REBUILD per covering player and reload=1 on REBUILD_REGION; edited-lane spawn reload; CACHE_INVALIDATE (our opcode 237) sent at 239/241 before the REBUILD; Live panel; launch and attach; scriptrun `--control-port` **Config groups go live (phase 9, 2026-11-01):** the editor snapshots `servpack/pack.manifest` before the packs build and diffs its `archive` lines after it (`ToriRSMapEd_BuildServPackDiff`), sending one reload row `(255, group)` per changed group beside the script pack and client rows (build log `servpack: N archive(s) changed in M group(s)`). The server (`ToriRSServer_ReloadConfigKind`, `ToriRSServer_ContentReload{Locs,Npcs,Objs}`) checks the whole kind against the open pack first and keeps the running table on refusal; loc replaces the info tables, scene records, defs, categories and op overlays and owes every logged-in player one REBUILD; npc re-points every live `npc->def` by type in one bounded pass while the old array is held; obj replaces the info table and requirement rows; seq, healthbar and struct as before. A RESTART_NEEDED answer finishes the RUN with restart rows instead of failing every later RUN. **Client:** CACHE_INVALIDATE on 2/6, 2/9, 2/10 evicts the decoded loc/npc/obj records (retired, never freed under a holder) and the dat2 group cache, then the provider's archive hook (registered in main.c, native) drops the platform's decompressed copy and re-primes JS5 -- 255/255, 255/<index>, the rewritten group (`Js5ClientInvalidateArchive`; js5_server re-checks its cache on every master request); the owed REBUILD is forced past the same-zone early-out so placed scenery re-decodes. Proven: e2e RUN (c) loc op3 and (d) npc op4 pressed by the never-relogged bot (16 s / 13 s); a real client logged into a real torirsserver lane re-decoded blueflower's op3 as the new text after one RUN, no relog (`TORIRS_CACHE_INVALIDATE_DEBUG=1`, `TORIRS_LOC_CFG=<id>`). **Web client (2026-11-01):** main.c registers the same hook on the emscripten build (`PlatformWeb_IO_ArchiveInvalidateHook`, platform_web_api.c); `PlatformWeb_IO_InvalidateArchive` (platform_web_io.js) releases the decoded-archive LRU entry, drops the resident container and the index's reference-table memo (owned decode retired, never freed) and moves the table's generation so a read already out keeps nothing; the host's `invalidateArchive` (torirs_hostio.js -> torirs_js5.js) parks the index's outstanding requests (socket reopened when one was on the wire), fetches 255/255, 255/<index> when the stored table no longer matches the master, re-validates every IndexedDB-stored group of the index by its container CRC against the new table (rscache decode, `ToriRS_WebApi_ReferenceTableArchiveCrc`) and overwrites only the rewritten ones; reads of the index wait meanwhile; re-primes are serialised. Proven on a real torirsserver lane in Chromium: one RUN, no relog, the console showed `io 2/6: 1 decompressed copy dropped`, `js5 fetched 255/255`, `255/2`, the 8 groups the client build rewrote, then `loc_cfg 1187 decoded: name='Flower' ops [] [] [Smell] [] []` (`?env=TORIRS_CACHE_INVALIDATE_DEBUG%3D1&env=TORIRS_LOC_CFG%3D1187`). Tests: web_io_invalidate_test 5, js5_invalidate_test 7 (`make -C src test-web-cache-invalidate`, in test-web-channel). **Play client (2026-11-02):** RUN = ... reload -> RUN_STEP_PLAY (6, numbered after DONE) -> DONE: the lane's play client (torirs_maped_live.h "The play client"; How to run it) is started when there is none, left alone when the server lists its account ("reloaded in the running client (<account> at x,z, no relog)"), started again when it exited or logged out; a lane that is down at the reload is LAUNCHED on what the RUN built and the client started (no reload owed), a lane that cannot launch says "Launch the lane first". The editor server owns the process like the lane's servers (a `ToriRSMapEdPlayLauncher` seam: native posix_spawn + waitpid; the test binds a fake), so RUN from any window, the Live panel's play line + **Play** / **Stop**, and `torirsmapedctl play | play-restart | play-stop` drive one client, headless or not; LIVE_PLAY (59) / FACT_LIVE_PLAY (179). **Control link:** CTL_PLAYERS (7) -> CTL_FACT_PLAYERS (134): logins since boot (the pool slots' login generations summed), active count, and per player account, display name, slot, login generation, tile; sent once after an accepted HELLO (which is how the editor knows to ask: an older server closes the link on an unknown frame), then polled with the status (every second while the client logs in). The lane's launched server now keeps its saves in `<live>/saves`, skips the welcome screen, and logs to `<live>/logs/`. **Client:** CACHE_INVALIDATE on index 5 (a map square) now evicts too (`CacheProvider_InvalidateArchive`: the split group, the IO layer's copies through the archive hook -- JS5 re-primes and refetches the square -- and the provider's decoded squares); before, a real client rebuilt the scene from the square it already held and a terrain edit waited for the next login (the scriptrun bot, having no cache provider, never showed it). Proven by `make -C src maped-live-e2e-play` (below) and photographed (a RUN from the editor window launched the lane and started the client; a second RUN raised a second spike in the same, never-relogged client). Tests: live_editor_test 146 (play section: launcher on a fake client binary -- manifest, fixture save, environment -- and the state machine: lane down, online, logged out, died, Play/Stop, detach, a server without CTL_PLAYERS, browser), live_panel_test 34, cache_provider_invalidate_test 25. | the play client's position does not reach STATE_LIVE_FOCUS (viewers do not lock to it); Play cannot raise an existing window (it reports "already logged in"); Restart offer; stale list (always 0) |
| 16b | Accounts and the Place player tool (2026-11-02) | **Accounts panel** (module "accounts", View > Accounts, starts closed, pops out, every window shares the same facts; src/editor/accounts/accounts_panel.c over the model in accounts_client.c): the lane's accounts are the saves under `<live_dir>/saves/*.ini`, listed by the editor server (only it knows the live dir; windows never touch a file). Per account a dropdown + status row ("online at 3222,3218 level 0: edits are cheats; values from the server's checkpoint" / "offline: edits write the save"), Refresh, **Use for RUN / Play** (the lane's play account at runtime, remembered in `<live_dir>/accounts.ini` [lane] so a daemon restart keeps it; FACT_LIVE_PLAY relays it to every window), Duplicate, Delete (offline only), Name + Password (+ Fixture) + New account (the save from the fixture, default the lane's play fixture; the password is claimed at the first login -- the server's rule -- and remembered in accounts.ini [passwords]); tabs **Position** (x z level + Move, To selection, the Place player tool), **Stats** (25 skills with level and xp; Set level, Set XP, Max all, Reset all), **Vars** (Find = a settled catalog query over varps and varbits by name or id; the save's varps with their catalog names; a varbit's base varp and bits from the catalog; Value + Set), **Quests** (every `quest` dbrow with [x]/[ ]/[?]; Complete = `::complete`, Mark complete = `::questmark`, Mark all complete; offline these are QUEUED per account and flushed 3 s after its login; the queue is shown), **Items** (Backpack / Worn / Bank slot lists; Slot + Obj by id or name or From Catalog pick + Count; Set, Remove, Clear backpack), and a Log of the server's answers (+ the Live log's lines while an online cheat is answered). **Every edit applies at once** (no Apply/Revert): the server chooses the path from CTL_FACT_PLAYERS -- ONLINE = a cheat on the control link (`setlevel`, `xp` (grows only), `maxstats`, `setvar <name>`, `item <id> <n>` (+ `wield`), `tele <coord>`, `complete`, `questmark`, `clearinv`) followed by `::checkpoint <leg>` whose file `saves/checkpoints/<leg>.ini` is read back as the view's SNAPSHOT (flag; "values from the server's checkpoint"); OFFLINE = the save edited in place with a line-preserving codec (comments and unknown keys kept; a varbit spliced into its base varp's bits; stats as `<boosted> <xp_tenths>`). **Daemon module** src/torirsmaped/torirs_maped_accounts.{h,c} (compiled into torirs_maped.c after the live half): requests 65..73 (ACCOUNTS_LIST 65, ACCOUNT_VIEW 66, CREATE 67, DELETE 68, DUPLICATE 69, USE 70, SET 71, PLACE 72, ACCOUNTS_QUESTS 73; range 65..80), facts 191..194 (FACT_ACCOUNTS 191, FACT_ACCOUNT 192, FACT_ACCOUNT_STATUS 193, FACT_QUESTS 194; range 191..200); one SET key vocabulary for the panel and the ctl (`player.pos`, `stats.<stat>[.xp]`, `stats.all max|reset`, `varps.<id|name>`, `varbits.<id|name>`, `vars.<name>`, `inv|worn|bank.<slot> <obj> [n] | -`, `inv.clear`, `quests.<dbrow> complete|mark`, `quests.all mark`, `cheat <line>`, `refresh`); the quest index = the `::complete` arms of quests/scripts/quest_cheat.rs2 (var + `^constant`, the constants read from every `.constant` under server/scripts) joined to the `quest` rows of configs/all.dbrow; the service pass (every pump + the daemon's idle loop) restores the play account, watches logins (queue flush), reads landed checkpoints (4 s timeout -> "wrote no checkpoint"). **Place player tool** (EDITOR_TOOL_PLACE_PLAYER, rail icon, hotkey P, `tool.place_player`): a world click sends ACCOUNT_PLACE for the lane's play account; a magenta tile marker follows the hover; the console says "online: ::tele ..." or "offline: wrote saves/...". The arrows and every other key stay the camera's / the table's. **ctl**: `accounts`, `account-view <name>`, `account-new <name> <password> [fixture]`, `account-dup <name> <new> <password>`, `account-delete <name>`, `account-use <name>`, `account-set <name> <section.key> <value...>`, `account-place <name> <x> <z> <level>`, `quests`. `TORIRS_EDITOR_ACCOUNTS=show[,tab=<n>][,account=<name>][,container=<n>][,loadout=<name>]`. **Account builder (2026-11-07)**: only the Account picker and its status line sit above the tabs, which are now **Account** (lane line, Refresh / Use / Duplicate / Delete, the position and Place player tool, New account), **Items**, **Loadouts**, Stats, Vars, **Quests**, **Log**. Items is a container picker over one GRAPH well drawn as the game draws it: Equipment = interfaces/wornitems.if's layout (slot plate sprite 170, the links 172/173, the empty slots' silhouettes 156..166), Backpack = the gameframe's 4 x 7, Bank and every `[container.<inv>]` of the save (or any inv opened by name, Inv + Open inv; the save gets the section on the first Set) = paged 8 x 7 grids sized from configs/all.inv; every item is its own icon with its stack printed as the game prints it (new EditorUiAppServices.obj_icon / cache_sprite, native only: the web and the headless harness draw the ids); a press selects a slot and fills Slot / Obj / Count; Set / Remove / Clear container (worn.clear and bank.clear offline; `container.<inv>.<slot|clear>` offline only). **Loadouts** are the lane's, `[loadout.<name>]` in accounts.ini (worn + backpack): the list, the selected one's gear drawn, Save current gear (LOADOUT_SAVE 75), Apply to account (SET `loadout <name>`: offline the save's worn and backpack replaced; online `::clearinv`, `::item`+`::wield` per worn item, `::clearinv`, `::item` per backpack item -- a worn slot the loadout leaves empty keeps its item), Delete loadout (LOADOUT_DELETE 76); FACT_LOADOUTS 195 to every window. **Quests**: the counts, Find + Show (All / Not complete / Complete / Unknown), each row with its progress, the selected row's var and value, Reset (SET `quests.<dbrow> reset`: the var to 0; not a proc arm), Complete shown / Mark shown (SET `quests.list complete|mark <dbrow>...`, batched). FACT_ACCOUNT ends with the LOOK (gender, the 12 kits resolved as the game server resolves them -- the save's `kit<wearpos>`, else player/configs/appearance.enum, else -1 -- and the 5 colours) and the save's other containers (inv, name, size, items); ACCOUNTS_INVS 77 -> FACT_INVS 196 lists configs/all.inv. **Place player's ghost**: while the tool is armed the PLAY account stands translucent on the hovered tile in its own body and gear (app_map_editor.c: Editor_Accounts_PlayLook, the slots composed in the server's order -- a worn item's wearpos 2/3 blank their positions, an unclaimed position wears its kit -- configs, idk table and models loaded on demand, then a scene player of its own; down when the tool, the look, the level or the world changes). Photographed 2026-11-07 (osrs242 cache, a scratch lane): the worn layout, backpack, bank, an other inv, a loadout, the quests tab, and the ghost in rune armour and a cape of legends on Lumbridge courtyard. Tests: accounts_test (172: codec byte for byte, quest arms, every SET key offline, queue, use/dup/delete, the look and its defaults, containers by name with their size, the inv list, loadouts saved / listed / applied / replaced / deleted, quest reset and batches, the ONLINE path against the mock control peer incl. the flush, a landed checkpoint, an online loadout and a container refused), maped_daemon_test.sh (11), editor_panels_accounts_test (70: the layout above the tabs, the worn layout's and the backpack's slot keys, a pressed slot, Set / Remove / Open inv / Clear, a loadout end to end, the quest counts, filter, detail, Reset and Complete shown, the play account's look; the default play account's first save made by Place player, workbench 7.1), `make -C src maped-live-e2e-accounts` (the real server + play client: 26 ok, 0 failed). | varcs: no server op or cheat writes a varc for a player and a save holds none, so the panel has no Varcs tab (said here rather than faked); online, one slot cannot be emptied and the bank and other containers cannot be edited (no cheat; the Items tab says so); a loadout applied online cannot empty a worn slot it leaves empty; online xp only grows (`::xp` adds); `::setvar` needs the catalog's name (the daemon refuses while the catalog is still indexing); the view online is the last checkpoint, refreshed after each edit, not live-subscribed; a server attached by address keeps its own saves dir (the module reads the lane's); `::checkpoint` is refused in a dialogue or a fight (the Log says so and the view falls back to the save); accounts.ini holds passwords in plain text (a dev lane); the quest states of the four proc-arm quests are [?] |
| 17 | The web | wasm editor client, viewer tab boot, capabilities gate the menus | browser BuildRunner/LiveLink (answer UNSUPPORTED); content tree host binding |
| 3c | Areas and Collections | **AREAS document** (kind 5, key `areas`, `<content_dir>/editor/areas.txt`: in the content tree so the team shares and reviews it, outside every pack input so naming an area never changes a build; header comment in src/editor/editor_areas.h): one record per line, `area <name> \| level <0..3\|all> \| rects <x0> <z0> <x1> <z1>[, ...] [\| colour <0..7>] [\| note <text>]` and `collection <name> \| squares <key> ... [\| note <text>]`, names unique per kind; line-preserving codec (src/editor/editor_areas.c: comments, blank lines, CRLF, no final newline byte-identical; the first record brings a 3-line header comment, the last removed takes it back, an emptied document's save removes the file); opened by every session at `Editor_OpenHost`, so every window mirrors it; held as text, applied by parse -> edit -> emit; **commands** AREA_SET 10, AREA_REMOVE 11, COLLECTION_SET 12, COLLECTION_REMOVE 13 (`u.area`: `int line`, the record line before and after, each <= 511 bytes; wire `i32 line, string before, string after`; a SET with a before replaces by name, a rename when the names differ; a REMOVE's undo re-inserts at its old line; refused when the bytes do not match or a name is taken), History rows from the codec ("Mark area Lumbridge castle", "Rename area A to B", "Set level of area A to 1", "Add rect to area A", "New collection C", "Add 1 square to collection C", "Delete area A"); **Areas panel** (module "areas", View menu, dock order after "squares", admitted when the column has room): Areas tab (list name/level/rects, rect status, Name, New from rect -- the Rect tool's rectangle on its level, "no rectangle" said in the panel --, Add rect (to the selected), Rename, Level + Set level, Delete, Go to -- the free camera to the area's centre, opening its square first when it is not loaded --, Show outlines (on), Hide others) and Collections tab (list name/squares, current square = the picker's focus else the latched tile's square, From open (squares), From picker (the shift-click set), Add / Remove current, Rename, Delete, Open all -- each as a document, into the world one load at a time, a confirm past 16); the Rect panel's "Mark area from rect"; the Squares picker's **Collection** dropdown (`all` + every collection: only its squares, on any Area page, Fit frames them; follows edits; off when the collection goes); **world overlay** (app_overlay.c `app_overlay_build_editor_areas` from `Editor_Areas_Overlay`: each rect washed in its colour on its level, outlined along the ground, the name in the overlay's TEXT at the NW corner of the first rect, the selected area brighter; through `world_emit_desc`, so it follows the World panel); ctl `areas`, `area-set`, `area-remove`, `collection-set`, `collection-remove`; tests editor_areas_test, editor_panels_test `test_areas`, maped_embed_test, maped_daemon_test.sh (9) | see open issues (Areas and collections) |
| 3d | Catalog palette | **PALETTES document** (kind 6, key `palettes`, `<content_dir>/editor/palettes.txt`, beside areas.txt and outside every pack input; header comment in src/editor/editor_palette.h): `palette <name> [\| note <text>]` headers and `palette <name> \| slot <1..10> \| <loc\|npc\|obj>:<record> [\| shape <0..22> rot <0..3>] [\| note <text>]` entries; keys unique (header per palette, entry per slot); line-preserving codec (src/editor/editor_palette.c), opened by every window at `Editor_OpenHost`; **commands** PALETTE_SET 14 / PALETTE_REMOVE 15 (`u.area`, wire `i32 line, string before, string after`, refused on a byte mismatch or a taken key; rename / delete / swap are one stroke via `Editor_PalettePlan*`), History rows from the codec ("Add oaktree to palette Lumbridge slot 3", "Rename palette A to B", "Swap slots 1 and 2 of palette P"); **Catalog palette panel** (module "catalog_palette", View menu, starts closed, docks after Preview; src/editor/panels/edpanel_catalog_palette.c): palette dropdown, Name + New palette / Rename / Delete, a 2x5 strip of numbered slots each a thumbnail rendered by the App (src/app/app_catalog_palette.c, the Preview's camera, one per frame, cached per record in scene sprites 0x4C001000+n; the record name on the web or while it renders), the pick's slot outlined, hover tooltip; click = the catalog pick (published: a popped-out palette picks for the main window) + Place armed with a loc slot's shape and rotation (an npc/obj pick under Place is the spawn ghost); right-click Remove / Set pose from Place tool / Move to slot 1..10 (held = swap); Add current pick into first free or a chosen slot (a loc with the Place pose); the Catalog's "Add to palette" button and the Preview's menu row; hotkeys Shift+1..9 / Shift+0 slots, Ctrl+Shift+A add, Alt+] / Alt+[ step (docs/MAPED_HOTKEYS.md); `TORIRS_EDITOR_CATALOG_PALETTE=<palette>[,slot=<n>]`; ctl `palettes`, `palette-set <palette> <slot|0> <kind>:<record> [shape <s> rot <r>]` / `palette-set <palette> rename <name>`, `palette-remove <palette> [<slot>]`; tests editor_palette_test, editor_panels_palette_test, maped_daemon_test.sh (10) | thumbnails duplicate the Preview's model building (one helper wanted); no thumbnails on the web; the shown palette is per window; a loc slot holds one pose, not multiloc rungs; slots cannot resolve without the catalog capability |
| 3b | History panel | `Editor_History` (editor_cmd.c) rebuilt from the FACT_CMD stream + own undo/redo; rows newest first with names ("Raise 21 tiles m50_50 L0", "New square m1_1 (copy of m50_50)"), undone prefix, current marker, filter by key, click-to-jump; rebuilds only on serial change | a late-joining window lists only steps since it connected; another window's long drag may show as several rows until an undo re-cuts them |
| 5b | Default layout | `Editor_UiLayout` (editor_ui.c) at boot/resize/scale/Reset: menu bar full width, tool rail wraps to 2+ columns, one right column 22% (200..400 px at 1x; order terrain, palette, locs, spawns, rect, inspector, squares, areas, catalog, preview, history), panels admitted while >= 160 px, spare height shared (the remainder to the column's first stretching panel: the picker's well before the Preview's), console strip under the world when <= 1/4 height, the World panel docked over what is left; a saved layout (`editor_layout.ini`) applied before the first pass; panels that do not fit are hidden (View menu); dragged panels keep their place until View > Reset layout; View > Snap to grid | tested 1280x800..2560x1440 at scale 1 and 2 headless + photographed at 1280/1530/1920 |
| 7c | Hotkeys | docs/MAPED_HOTKEYS.md is the user's reference. **One table** (src/editor/editor_hotkeys.{h,c}): 100 actions (File, Edit, Tools, Brush, Level, Camera, View -- one per View-menu panel module, the unbound ones included), each with an ini id, group, name, doc row, context mask (G, W, L, R, S, N = no loc; L excludes S and N) and kind (edge, repeat after 300 ms at 20 Hz, held); two chords per action at most; chord parse/format (`ctrl+shift+z` <-> "Ctrl+Shift+Z", "Cmd" on Apple); `editor_hotkeys.ini` codec (non-default bindings only, `<action> =` unbound, a bad line skipped and reported); conflicts per context; `Editor_HotkeysResolve` over a plain snapshot (edges, held keys, live contexts) -> fired actions, active held actions, CLAIMED keys; the later binding wins a clash. **One dispatcher** (src/app/app_editor_hotkeys.c, called first in the frame, before the developer overlay): snapshot from LibToriRS_Input, gates (editor open, no text field / model view / menu drop / minimenu), Hotkeys-panel capture, resolve, dispatch through `Editor_HotkeyRun` (src/editor/panels/edpanel_hotkeys.c: the one entry point the File / Edit / View rows also call) or the App-bound actions (camera flight and turn read in app_camera.c, eyedropper I / Shift+I, Ctrl+A select the hovered square's locs on the edit level, Return apply to selection, Ctrl+V paste at the hovered tile, Home / Ctrl+Home framing through the Areas panel's camera move, Escape ending a drag, PageUp/Down from the plane under the pointer); a fired chord sets `input_frame_consumed`, marks `hotkey_consumed` (re-marked after app_ui_hotkeys clears it) and logs `editor: <action> (<chord>)`; claimed chords are skipped by `app_debug_key_down/held` and the world debug keys. Previous tool (`EditorUi.previous_tool`), Tab hide/show every panel but World (`Editor_UiToggleAllPanels`, remembers what was open), brush radius/step/strength steppers that move the Terrain sliders (`edp_brush_step`), Ctrl+4 Auto, Ctrl+5 Above, Shift flies 4x. **Hints** from the table, re-labelled on every rebind (`hotkeys.serial`): menu rows `Name\tChord` drawn right-aligned by the chrome (menu-mode dropdown rows and TREE rows split at a tab), tool rail / Level / Locs / Spawns / Terrain slider tips end with the chord; the old `[D]/[R]/[S]/[Del]` literals are gone. **Hotkeys panel** (module "hotkeys", View, F1): search, capture (Esc cancels, Backspace unbinds, a lone modifier only for Fly faster), clashes red on both rows, Reset to defaults. **Persistence** `editor_hotkeys.ini` / `TORIRS_EDITOR_HOTKEYS` through task_editor_hotkeys_io.c (the layout's IO pattern). Tests: editor_hotkeys_test (854 checks, incl. every action's id and row in the doc, and `view.preview` on Ctrl+Shift+P), editor_panels_test `test_hotkeys`. Photographed 2026-10-31 (F1 panel, `3` arming Raise, F12 hiding World, Ctrl+Z undoing a `torirsmapedctl tile-height` edit with the console line) | see open issues (Hotkeys) |
| 7d | Triggers panel | **Triggers panel** (module "triggers", View > Triggers, Ctrl+E; src/editor/panels/edpanel_triggers.c): helps with script triggers, the scripts are edited in the user's own editor. Show: *Record / selection* (the Record panel's record, else the selected loc, else the Spawns panel's npc / obj, else the catalog's pick; a name looked up once per id), *Name* (Subject field), *File* (every header of one file under server/scripts: its open document, else the disk, else the catalog). Rows are the catalog's TRIGGER contributions (`trigger`, `subject`, `file`, `line`; `subject=<name>` then an exact match, since the filter also matches `<name>_*`), the subject's edited script read from this window's document when it is open ("(unsaved)"). **Open in editor** on a row, on a Problems row and on a catalog TRIGGER row (Catalog's "Open in editor"): `Editor_HostLocalOpenInEditor` (editor_host_local.c) runs `TORIRS_EDITOR_OPEN_CMD` (editor_open_cmd.h: `{path}` shell-quoted, `{line}`) or the default (`code --goto`, else open / xdg-open) through `/bin/sh -c '... &'`, the command in the console; a document of that file with unsaved edits is saved first; web / Windows answer UNSUPPORTED and the panel shows the path to copy. **Add trigger...**: a kind from the type's list (editor_script.c: oploc1..5/u, aploc1..5/u; opnpc/apnpc; opobj/apobj + opheld1..5/u -- the builders' names), Op text; when the kind answers a menu entry the record lacks (`op<N>`, `ifop<N>` for opheld; "", `default`, `null` count as none) the field is written through the Record panel's `Editor_CfgRecordFieldCommands` (the layer it would choose) and the template (`mes("Nothing interesting happens.");`, `~displaymessage(^dm_default);` for a use-on kind) is appended to `server/scripts/edited/<s>/scripts/<s>.rs2` (opened empty when absent; a header comment starts a new file) as a TEXT_SPLICE from the panel -- one stroke, one History row "Add [oploc3,crate] (op3=Search)"; refused: a kind of another type, a trigger the tree already declares (sscompile refuses duplicates), a builder-generated script, a missing menu text, an edited script that changed on disk. **Refresh** asks the catalog again (it re-stats changed files at most every 2 s) and reads every shown file from disk; a document open here whose bytes differ from the disk (and that has no unsaved edits) is "edited outside" -> **Reload from disk** splices it to the disk's text (`Builder_TextSplices`, one undo step "Reload <file> from disk"). **Check** (Ctrl+Shift+B, File > Check scripts) = sscompile over a stage of the scripts ON DISK (`Builder_SscStage`/`Run`, its own work dir `src/build_opt/tmp/triggers_check`, native only, synchronous; unsaved script documents are not in it and the console says so) -> Problems (`file:line: message`) with Open problem in editor. **Run** (Ctrl+Return, File > Run) = the Live panel's RUN (`Editor_Live_Run`; disabled while `Editor_Live_Busy`). Record panel: "Triggers..." opens the panel following that record. Tests: editor_script_test (pure: parsing, kinds and fields, every template, the add plan, the open command's quoting and defaults, diag lines; plus every template of every kind compiled by sscompile against a stage), editor_panels_test `test_triggers` (subject and file rows, Open in editor through a recording command, Add trigger with op3=Search as one undo step, duplicate / wrong-type refusals, edited outside -> Reload from disk, Problems rows, the hotkeys), editor_hotkeys_test (the three chords and the doc) | see open issues (Triggers panel) |
| 18–19 | Build order, risks | phases 0–5 done in order | see open issues |

## The live lane end to end (phase 4)

`make -C src maped-live-e2e` runs `src/maped/test/live_e2e/run.sh`. It writes
only under `src/build_opt/tmp/live/` and reads OSRS-Content:

1. `mirror.py` makes `content/`, a writable mirror of
   `OSRS-Content/osrs-content` (real directories of per-file symlinks for the
   trees a save or a build writes into; `pack/` copied because ss_allocate
   rewrites ledgers in place; `server/scripts/build*`, `server/pack` and every
   `*.lock` left out; large asset trees linked as directories). A save renames
   over the symlink, so the edited file becomes a real file in the mirror.
2. `lane/` gets copies of the tree's three packs (`server/pack` ->
   `servpack/`, `server/scripts/build` -> `scripts/`,
   `server/scripts/build_summoning_curses` -> `scripts_lanes/`) and of
   `cache.osrs241` (-> `cache/`).
3. torirsmaped serves the mirror with `--live-dir lane/`. The lane's game
   server is `server.sh`: `torirsserver --scriptrun bot.lua --tick-ms 200
   --control-port <game port>` on the lane's packs and cache, so the bot is the
   live player. js5_server is the real one on `lane/cache`.
4. `torirsmapedctl live-attach launch` starts both and attaches; the bot logs
   in and reads Hans's first line.
5. (a) `text-replace rs2:areas/lumbridge/scripts/hans.rs2` through the
   authority, `run`: the RUN saves, builds the packs into the lane, builds the
   client cache, reloads. The bot, never relogged, reads the new line on a
   later Talk-to.
6. (b) `tile-height 50 50 0 22 18 60` on the square the bot stands in, `run`:
   the bot's wire log shows exactly one REBUILD after the reload, and the
   CACHE_INVALIDATE before it.
7. `live-detach` stops the launched processes; OSRS-Content's hans.rs2,
   m50_50.jm2 and `pack/*.alloc` are checked byte-identical to before.

Last run (2026-10-30, integrate-4): **0 failed**. RUN (a) took 52 s (the
first packs build of the mirror plus a 20-type incremental client pack), RUN
(b) 25 s. The bot read "Hello. What are you doing here?" at tick 11, the
RUN (a) reload swapped 1 script (the previous table was freed at once: no
instance on it) and sent one CACHE_INVALIDATE for the 8 client archives the
client build wrote, and the bot read "Hello. What are you doing here,
editor?" at tick 270 on its 37th Talk-to with no login in between. RUN (b)
reloaded m50_50; the bot's wire log holds REBUILD_NORMAL_V2 at tick 0 (login)
and exactly one more at tick 397, after two CACHE_INVALIDATEs in the same
tick. RUN (b) also swapped 7 scripts: the mirror's per-file symlinks follow
OSRS-Content, which other sessions were editing meanwhile.

What run.sh does NOT cover: a real game client (the scriptrun bot decodes
with the client's parser but has no cache provider; run_play.sh below is the
real-client lane); io_server (no io manifest in the lane); the Live panel
driving the RUN (torirsmapedctl drives the same frames; the panel is covered
by test-live-panel against the python mock peer); a gated server.

**The real server and the real play client:** `make -C src
maped-live-e2e-play` runs `src/maped/test/live_e2e/run_play.sh` (writes only
under `src/<obj>/tmp/live_play/`, removed when it passes unless
`LIVE_E2E_KEEP=1`; the client is the `$(PLATFORM_TARGET)` the make links,
headless through `TORIRSMAPED_PLAY_ENV_SDL_VIDEODRIVER=dummy`): the same
mirror and lane copies, the lane's server the real torirsserver,
`TORIRSMAPED_LIVE_PLAY_BIN` the client. (A) tile spike, `run` with the lane
DOWN: launched on what it built, play client started, `play` waits until the
server lists it (one login); (B) a second spike, `run`: "reloaded in the
running client (maped43633 at 3222,3218, no relog)", `squares_reloaded 1`,
the client's log shows `cache invalidate: index 5, 1 archive(s): 1 dropped`
and `js5 fetched 5/12850`, the server's login count stays 1 and the same pid
runs; (C) the client is killed, `run`: "restarted the play client (it had
exited, status -9)", it logs in again (login 2); `live-detach` stops server,
js5_server and client; OSRS-Content checked untouched. `LIVE_E2E_SHOTS=<dir>`
gives the client a film strip; `LIVE_E2E_MIRROR_HOOK=<command>` may replace a
file in the MIRROR when another session's half-written content fails the
packs build (2026-11-02: `quests/quest_wanted/configs/wanted.varp`'s new
varp9514 lacked `clientcode`/`wholewrite`/`wholeread`). Last run
(2026-11-02, private OPT build): **23 ok, 0 failed**; RUN (A) 41 s (first
packs build of the mirror), login 5 s later from an empty play cache, RUN (B)
21 s. Photographed the same day from the editor window (Ctrl+Return under
`TORIRS_SIM_HOTKEY`, `TORIRS_EDITOR_LIVE=show`): the Live panel after the RUN
that launched the lane ("RUN done: the lane was not up: launched it on the new
packs and cache; play client started as maped43633 on the launched lane",
"play: logged in as maped43633 at m50_50 (3222,3218)"), the client's own frame
with that RUN's spike, and after a second RUN (ctl) the second spike in the
same client, the server's login count still 1.

## Locs: delete, undo, redo (2026-11-02)

The owner's report: "delete and the undo redo buffer are buggy when adding or
removing locs". Every add / remove / undo / redo sequence is now a test
through the embedded authority, the mirror's echo and the client's EDIT flush,
checked on the document (the loc list and the `.jl2` Save would write) and on
the world (the scenery on the tile, element ids, and a STATIC rebake of the
same documents after every sequence): `loc_undo_phase` in
src/engine/world_builder/test/edit_world_test.c (sequences 1-11), the
document half in src/editor/test/editor_cmd_test.c. What it found, by side:

- **Document** (src/editor/editor_doc.c, editor_cmd.c `apply_loc`). An undone
  delete was appended to the end of the square's list, so the `.jl2` saved
  after delete + undo differed from the file (a Clear tile undo also reversed
  the tile's order). Edits now insert in cache order (loc id, level, x, z,
  shape, rotation -- every `.jl2` in the content tree is in it) with
  `Editor_SquareLocInsert`; the parser keeps file order. A removal names its
  placement exactly (`Editor_SquareLocFindExact`) and is REFUSED when it is
  not there -- accepted, a delete of a loc another window had already deleted
  was recorded and its undo added a duplicate. (level, x, z, shape) names one
  placement (no content square holds two): an add onto a taken key is refused.
- **Command** (src/editor/panels/edpanel_ops.c). Place onto a tile+shape already
  placed added a second loc there (what the ghost previewed was a replace), so
  its undo, a delete or a select by key hit the OLD loc. Place now sends a
  replace (before = the occupant) and its undo restores the occupant;
  move / rotate / reshape / duplicate leave a loc whose landing key is held by
  an unselected loc where it is. Delete of a selection that is all "(gone)"
  says so in the console.
- **Apply** (src/app/app_map_editor.c `app_edit_sync_square`). The world diff
  walked shadow and document in step, which read every loc after a mid-list
  insertion as gone and back: the undo re-placed the rest of the square and
  renumbered its elements. It now matches locs by value, removes first, then
  places each new loc in map order (`WorldEdit_LocPlaceBefore`) -- EDIT's
  shade replay and a tile's last wall follow map order, so a loc filed at the
  end lit its neighbours differently from a fresh bake. The hover ghost's
  displaced occupant goes back in its place the same way
  (`WorldEdit_PlacementNext`).
- **Scene** (3rd/toridraw/toridraw_scene.c `td_scene_free_element_id`). A
  removed element whose id carries its kind (every scenery and terrain
  element, since 5fa22160b) was reset but never released: each delete, undo
  or redo leaked a slot until a pool clear, and ~570 delete/undo cycles in one
  session ran the scene out of its 65536 (the next add asserted; OPT crashed in
  ToriDraw_SceneElementSetModel). Released by index now. An npc despawn leaked
  the same way between region loads.
- **History** (src/editor/editor_cmd.c, src/editor/editor.c). Undo a place,
  then place the same loc again: the window read its own new edit as a Redo of
  the undone row, so the redo tail the authority had dropped stayed in the
  panel. `Editor_HistoryOwnEdit` marks the echo of this window's own edit. A
  same-tile change of loc or shape reads "Replace" (not "Turn").
- **Tool** (src/editor/editor_panel.c `panel_loc_ref_pointed`). The Delete and
  Rotate tools and the K accelerator acted on the first loc filed on the
  terrain tile under the pointer: clicking a tree deleted the flower patch
  under its corner and the tree stayed. They act on the front-most model the
  frame hit-tested under the pointer (ground decor only when nothing stands
  over it), the tile's first loc when no model is there.

Photographs (TORIRS_SIM_HOTKEY `x` arms Delete, TORIRS_SIM_CLICK_AT on the
tree at 390,350 of m50_50's default view, `ctrl+z`, `ctrl+y`; mirror +
scratch manifest, TORIRS_EXIT_BMP): before / after delete / after undo /
after redo -- the tree goes, comes back pixel-identical, goes again; the only
other pixels that move are the fishing spots' animation.

Not done: torirsmapedctl has no loc verbs (loc-add / loc-remove); a hand-made
`.jl2` out of cache order gets a deterministic but different position for an
undone delete; a DOC_CREATE template holding two locs on one key would trip
`server_command`'s assert (no content square does).

**Nudge and shapes (2026-11-02).** A move landing on a tile+shape held by an
unselected loc swaps: the holder goes to the tile the move leaves (a run of
selected pieces pushes it to the run's tail), one undo step; copy and reshape
onto a taken key stay and say so. The panel consumes its own STATE_SELECTION
echoes (the STATE_TOOLS rule): applying them collapsed a multi-selection and a
late one stranded a nudged selection. A loc's placeable shapes are its
config's model rows (decor 5..8 draw 4, 11 draws 10; no shape list = 10/11):
the Place pose dropdown, the Locs panel's shape strip (thumbnails rendered
per loc, a button per shape), Reshape / Shift+Q and every place / swap /
reshape the panels send stay within them, and the pose snaps to the pick's
first shape; the authority still accepts any shape. Fit walls to neighbours
(G, on by default; "Fit selection to neighbours") picks 0 / 2 / 9 and the
rotation from adjacent walls of the same loc, and the ghost shows it. Arrows
nudge only under Select / Move (context M); under any other tool they are
the camera's (N). Place keys: Space / Return place (P), Q / Shift+Q turn and
reshape the ghost, E swaps to the catalog pick, Escape drops the ghost.
Tests: edit_world_test sequences 12-13, editor_hotkeys_test. Open: no shape
strip in the Catalog or Preview panels; the strip's images are not
clickable (the buttons under them are); the Fit toggle is per window.

## Editor lanes (2026-11-02)

Owner: "Edits should go to a dedicated 'working' lane and then there should
be a publish menu that publishes to a target lane or a new lane."

A **lane** is a content tree to edit plus the live dir its RUN builds into
(torirsmaped/torirs_maped_lanes.h). `main` is the real tree
(`OSRS-Content/osrs-content`) and `build/live/default`; a named lane is
`build/lanes/<name>/{lane.ini, content/, live/}`. A lane's content is a
copy-on-write view of its base, in one of two layouts:

- **Sparse** (every lane `ToriRSMapEdLanes_Create` makes since 2026-11-10;
  lane.ini `layout = sparse`, `sparse_base = <tree>`, and deliberately no
  `base =` line, so an older editor refuses the lane instead of writing
  through its links). A directory of the lane is ONE LINK to the base's
  (junction or symlink) until something is about to be written inside it;
  `ToriRSMapEdLanes_MakeWritable(lane, rel)` then makes every ancestor of
  `rel` real, top down, EXPLODING a linked one: a real directory holding one
  link per base entry (sub-directories as directory links). Real from
  creation: the root, `server/` and `server/scripts/` (the base's pack
  outputs stay out, the tools' outputs land in the lane), `maps/` (exploded:
  the first thing a map editor writes) and `pack/` (copied, with its
  `ancestor/` seed). The explode is crash-safe (`<parent>/.<dir>.explode.tmp`
  built complete -- a dot-name no build tool reads, ending in `.tmp`, which
  no lane walk reads -- then the link removed, then the rename; a rename that
  fails puts the link back; a leftover with the directory absent is
  finished by the next MakeWritable or Follow, one beside the directory is
  removed by Follow or rebuilt by the next MakeWritable) and never deletes
  through a link. Explodes of one lane take turns across processes
  (`<lane>/explode.lock`, so an editor publishing into a lane another editor
  is saving into cannot rename a half-built directory into place). An
  absent ancestor the base HAS is filled from the base, never made empty
  (an empty one would read as its base files deleted by the lane). Each
  directory made real after creation has its time in `<lane>/exploded.ini`:
  everything the base had in it then was linked, so Changes and Follow take
  the later of that and the lane's `created` -- a file the base gained after
  `created`, shown through a link and then deleted by the lane, is the
  lane's deletion and Follow keeps it deleted. Temporaries (`*.tmp`) in the
  base are never linked into a lane, and every writer creates its temporary
  exclusively after removing any name left there, so a link at a temporary's
  name is never written through. A server opened on a lane's `content/` as
  a plain tree (`content_dir` / `--content` without `lane=`) finds its lane
  (`ToriRSMapEdLanes_OfContent`) and guards its saves all the same.
  **The hazard**: a write that reaches a directory
  that is still a link lands in the base tree, so every writer into a lane
  calls MakeWritable first (publish into a lane, follow and discard do; the
  editor server's saves through the WRITE GUARD below).
  Follow links a directory new in the base as one link; a linked directory
  follows on its own, so Follow's counts are lower than a mirror's for the
  same base. Discard leaves exploded directories exploded (a real
  directory of links reads as the link it replaced).
  **Every tool that reads a lane must walk INTO directory links** (in a
  mirror they were real directories, so a walker that stopped at links lost
  nothing; in a sparse lane it loses most of the tree, silently). Audited
  2026-11-10: sscompile, cachepack, the game server's loaders and pack
  check, the catalog, the editor host, accounts and alloc all type an entry
  by `d_type` and `stat()` the rest (DT_LNK and MinGW's typeless dirent
  included), so they follow; `tools/ss_allocate.py` did not on macOS/Linux
  (`os.walk` without `followlinks`) and now walks through links, and the
  server's stale-pack warning (`scripts_scan_newer`) used `lstat` on POSIX
  and now uses `stat`. The tools' in-tree writes reachable from BUILD, RUN
  and Check land only in `pack/` (ss_allocate's ledgers) and
  `server/scripts/build/` (its stamp), both under directories kept real.
- **Legacy** (`base = <tree>`, lanes made before; `ToriRSMapEdLanes_Mirror`
  still builds one): a full MIRROR (mirror.py's rules, ported): real
  directories, one link per file, directory links for the pure-asset trees
  (models, sprites, ...), a copy of `pack/`. Unchanged; MakeWritable works
  on it too (an asset tree it explodes is then walked like any other).

Both: no pack outputs and no locks; a save renames a temporary over the
link, so the edited file is a real file in the lane and the base is
untouched; the lane's **changes** are its real files that differ from the
base (modified / added) and the base's files it no longer has (deleted).
The walks go only into the lane's real directories, so a sparse lane's
scan costs what it changed; Changes, Follow, Publish and Discard give a
sparse lane the same answers as a mirror of the same content (lanes_test
runs both side by side). No conversion of existing lanes yet.

- **Serving a lane**: `torirsmaped ... --lane <name>` (or
  `$TORIRSMAPED_LANE`), the manifest's `[editor:boot] lane=<name>` for the
  embedded server (`ToriRSMapEd_OpenLane`, `ToriRSMapEd_EmbedStartLane`,
  `Editor_HostOpenMapEdEmbedLane`; bootmanifest `editor_lane`), the launcher's
  `[service:torirsmaped] lane=` (else the manifest's). The lane is made on
  first use; its live dir is bound for BUILD, RUN and the servers. `main`
  (or no lane) is the shape before: the tree itself. Both editor manifests
  say `lane=working`.
- **Write guard** (2026-11-10): `ToriRSMapEd_OpenLane` sets the server's
  local host's guard (`Editor_HostLocalSetWriteGuard`, editor_host.h) to
  MakeWritable on the lane. editor_host_local.c asks it with the
  content-relative path before every mkdir (`make_parents`, the spawn
  file's directories), every save's temporary and rename (`write_file_atomic`:
  squares, spawns, every document kind, an interface's `.compack`) and every
  delete; a refusal fails the save (IO_ERROR, the sentence in the log). Then,
  right before the temporary is opened or the file removed, it checks that
  no directory between the root and the target is a link: an assert, and in
  the shipping build (NDEBUG) a refused write -- a missed guard never writes
  into the base. A delete of a path that is not there at all explodes
  nothing. Main or no lane: no guard, as before. Everything else the server
  writes goes under the lane's live dir (builds, RUN's run_times and
  map list, CHECK's puts, accounts, play saves and logs) or is RUN's scratch
  squares, which go through the same host. The session lock sits at the
  content root, always real. Not guarded: the Triggers, Quest and Shop panels
  build their paths from the manifest's `editor_content_dir` -- the BASE
  tree, not the lane -- so Open in editor, Show in Explorer and Copy path
  name the base's file, which the owner then edits in the base, as before
  sparse lanes (and the Triggers panel's "changed outside" compares the
  lane's document with the base's file).
- **Wire** (requests 81..90, facts 201..210, torirs_maped_lanes.h):
  LANE_INFO / LIST / CHANGES / PUBLISH (target or new name, flags reset |
  force) / DISCARD / CREATE; FACT_LANE_PUBLISHED is a broadcast (every
  window's panel refreshes). Handlers in torirs_maped_lanes_wire.c; the host
  keeps the facts for the panel (`Editor_HostMapEdLanesSend/Take`).
- **Publish**: every dirty document is saved first (the same save-all),
  then the changes are written into the target's content -- the main tree
  (refused while another editor's lock holds it, unless forced), another
  lane, or a new lane mirrored from main first. When the target is the
  lane's base, the published files become links again (the working lane is
  clean) unless "keep". **Discard** drops the server's documents and its
  history, re-links every change (removes additions), and every window
  reopens the squares it shows (`FACT_LANE_PUBLISHED reload=1`).
- **Publish panel** (module "publish", File > Publish..., Ctrl+Shift+U;
  src/editor/panels/edpanel_publish.c): the lane and its base, the change
  counts and list (M/A/D path), a target dropdown (every other lane, "New
  lane..." with a name box), "Keep the changes here", Publish, Discard
  changes, New lane, Refresh; refreshes when it opens and after a save. A
  `lane=main` session's panel says there is nothing to publish and how to
  start on a lane. On demand like the Live panel: hidden at boot, flies out
  beside the column when opened (`TORIRS_EDITOR_PUBLISH=show` opens it with
  the editor, for photographs); editor_panels_test `test_publish`.
- **ctl**: `lane`, `lanes`, `changes`, `publish <target> [keep] [force]`,
  `publish-new <name>`, `discard`, `lane-create <name>`.
- Tests: lanes_test (190 on 2026-11-10, with and without
  `TORIRSMAPED_LANES_LISTINGS=0`: names, the mirror's rules, create / lookup
  / list, the change scan, publish to a new lane / another lane / main with
  reset, the lock refusal and force, discard; the sparse skeleton and
  lane.ini, MakeWritable on a deep path, the same edits and base moves on a
  sparse lane and a legacy mirror compared row by row and by the tree each
  shows, publish into a sparse target, explode crash recovery, a legacy
  lane's asset tree exploded), lanes_host_test (`test-lanes-host`: the
  real local host on a sparse lane with the guard -- a square, a spawn, an
  overlay record, a new quest directory, a ledger, editor/areas.txt saved
  and a script deleted inside linked directories, the base byte for byte
  unchanged, the change scan exactly those; a refusing guard and a guard
  that does nothing both fail the save), maped_daemon_test.sh section 12 (a
  daemon on `--lane work`: an edit saved through it never touches the tree,
  publish-new, publish main resets the lane, discard), editor_hotkeys_test.

Open: the Publish panel has no per-file pick (all or nothing); a lane's base
is always the main tree (no lane of a lane). (Superseded: Follow brings a
lane up to its base; a sparse lane's linked directories show the base's
present without it.) The script panels open the base's files, not the
lane's (Write guard, above): a script edited in the owner's editor is
edited in the base whatever the lane.

## RUN speed (2026-11-02)

Owner: "run takes an extremely long time before it opens the live client ...
needs to be faster. Profile and fix." The first RUN of the day into
`build/live/default` ran 17:52 -> 17:53:43 on his Mac. Profiled with the
lane's own logs and a timed `torirsmapedctl run` sequence on a mirror
(`timeline.sh`: lane down, attached, detached again; every ctl line
timestamped), plus `time make ...` of each half and `sample` of cachepack:

- A warm RUN was ~5 s: make start-up and the object verifier ~0.5 s per
  make x 2, id allocation 0.4 s, script packs 1.2 s, server pack check
  0.15 s, client pack 1.6 s + the map squares, reload < 1 s, then the play
  step. The game server boots to "listening" in 2.1 s; the play client logs
  in ~3 s after it starts.
- A BRAND-NEW lane (the default lane is new since 2026-11-02) paid two FULL
  repacks back to back: the server pack 20.7 s (`types 18.97s`, 121917
  archives) then the client pack 20.3 s (20 types, 195393 records), then
  the launch and the client -- the owner's 90 s. cachepack's hot path is
  its per-line key lookup in the merge (`key_candidates`, strcmp), spread
  over ~6 M lines; no single quadratic, left alone.
- A RUN within ~a minute of the play client's login finds the client gone
  from the server and restarts it (another boot and login): the JS5
  background fill is still running and the CACHE_INVALIDATE drops the game
  session (the e2e settles 75 s for this reason). Open, below.

Fixes (src/maped/live-editor.mk, torirsmaped/torirs_maped_build.{h,c}):

1. **One build, both halves at once.** RUN's build is `TORIRSMAPED_BUILD_RUN`
   = `make -C src maped-run-build ...`: the cachepack tool once, serially,
   then `make -j2 torirsserver-packs maped-client-cache`. RUN's steps are
   save -> build (`RUN_STEP_BUILD`, 7) -> reload -> play client -> done; the
   manual Build packs / Build client cache buttons keep their own jobs and
   steps. Cold: 41 s -> 21 s. A failed half is named from make's error
   lines ("the client cache build failed: ...").
2. **A new lane is seeded**, not repacked: `maped-lane-seed` copies the
   tree's own `server/pack`, `server/scripts/build` and
   `server/scripts/build_summoning_curses` (incremental state included) into
   a lane that has no `pack.manifest` yet, and the build brings them up to
   date; skipped while the tree's pack is mid-write (`.pack.lock`). Nothing
   is written under the tree. The client cache has no seed (its base is
   `cache.osrs241`, the configs still pack): the brand-new lane's first RUN
   is now one 20 s client pack.
3. **`OBJVERIFY_SKIP=1`** on RUN's makes: the object verifier hashes every
   object's prerequisites at every make start-up (~0.5 s) for a build that
   compiles none of them.
4. **Progress** (`FACT_RUN_PROGRESS`, 180): step, index of 4, whole-RUN and
   step percent, elapsed, remaining, detail (the last unit the build
   reported, the play client's state). The estimate weighs the steps by
   the last finished RUN's durations and the build by its unit count
   (`<live>/run_times.ini`, written at every RUN that finishes); without a
   history the step's own clock moves the bar asymptotically and the time
   left reads unknown. Four rows a second at most. Shown by the Live panel
   (a bar under the run line) and by a small RUN window at the foot of the
   main window (`live_panel.c`; never in the View menu, the dock or the
   layout file; a popped-out window keeps the panel's bar only), and
   printed by `torirsmapedctl run` per step and per 5 %.

Measured after, same recipes: the timed sequence's RUNs 29 s / 5.3 s /
4.2 s became 19.6 s / 3.5 s / 2.4 s (lane built elsewhere so every unit
recompiles; attached and warm; lane down and warm; the client logs in ~3 s
after a launch); maped-live-e2e-play's RUN (A) 39 s -> 22 s. Photographed
2026-11-02: the RUN window at the foot of the main window and the Live
panel's bar, both reading "RUN 21%: build 2/4: packs and client cache (4 s,
~16 s left)" four seconds into a RUN.

Tests: live_editor_test (the RUN build's command, the one job, both
failure halves named, progress rows in order), maped-live-e2e-play (RUN (A)
and (B) timings printed), maped_daemon_test.sh.

### The warm build again (2026-11-07)

Owner: "I want that warm build to be extremely fast." The last RUN on
`build/lanes/working` spent 5.1 s of 6.1 s in the build step with nothing to
rebuild. Profiled on a copy of that lane (its content is the mirror: one
symlink per file, ~102,000), with `sample`, a recipe-line timestamp `SHELL`,
and a stat-counting dyld interposer. The cost was the filesystem, not the
tools' work: a no-change build made ~480,000 `stat()` calls, ~185,000 of them
on a path the same process had already stat'd. Every walker types entries by
`d_type` and stats only what it cannot type, but every mirror entry is a
`DT_LNK`, so every entry is a stat through its link (~3 us; one pass over
`server/` is ~175 ms). Fixes:

1. **One make.** `make -j2 maped-run-build` runs both halves as its own
   targets, ordered after `maped-run-tools` (the seed, cachepack once). It was
   a `$(MAKE) -j2` under it, and parsing src/makefile twice plus a cachepack
   check per half was ~0.7 s. make's error lines still name the half.
2. **The CS2 link check only when the client scripts were imported**
   (`cp_assets_selects`): the one-square `--assets=maps` import linked every
   script in the table, 0.4 s for an archive no script reads.
3. **The square goes in with the config pack**: `--incremental` takes
   `--assets=<kinds> --archive-list F` (a bare `--assets` is still refused),
   one cachepack run instead of a second that loaded the names and opened the
   cache again.
4. **The base identity check where the base is copied** (the first build): a
   later incremental pack never reads the base, and the check is a crc of its
   whole 252 MB dat2.
5. **One stat per entry per process.** `RSCache_EntryKinds`
   (rscache_serverpack.h) remembers each symlinked entry's directory-or-not
   for cachepack's content walk and the server-pack input list it shares;
   sscompile's build does the same for its source walk, its inputs check and
   the symbol loaders (`SSC_Symbols.entry_is_dir`, ssc_build.c). The symbol
   table's three loader walks were 1 s of a one-script edit.
6. **ss_allocate's stamp types entries without following links**: a link is
   listed by its name (with `@` since 2026-11-10) and never stat'd. (This
   said `os.walk` never enters a symlinked directory; since sparse lanes the
   walk enters every directory link, and the directories it reached through
   one are stamped by the path it reached them at.)

Measured interleaved against HEAD's toolchain on two copies of the lane
(medians; outputs compared: both script.dat byte-identical, server and client
packs "0 archive(s) changed" each way, `cachepack verify` exact for loc and
the 2,950 squares after a config commit and a square in one run):

| warm RUN build                       | before | now    |
|--------------------------------------|--------|--------|
| nothing changed (one saved square)   | 2.24 s | 0.97 s |
| one `.rs2` edited                    | 3.35 s | 1.43 s |

A changed cachepack or sscompile binary changes the incremental state's key:
the first RUN after this lands repacks the lane once (~25 s).

Left: four processes each resolve the mirror's links once (both script packs,
the server pack check, the client pack), and src/makefile's parse (~0.25 s,
its `$(shell)` probes included) is the floor of any make-driven RUN.

## Accounts (2026-11-02)

The lane's player saves, managed from the editor (row 16b; the module header
src/torirsmaped/torirs_maped_accounts.h has the wire and the SET key
vocabulary). What to know when using it:

- **Where the accounts are.** `<live_dir>/saves/<name>.ini`, the directory the
  lane's launched torirsserver keeps its players in (TORIRSSERVER_SAVES). The
  daemon needs a lane (`--live-dir`, or `$TORIRSMAPED_LIVE_DIR`; unset =
  `build/live/default`); without one every verb answers "no lane is bound".
  The module's own file is `<live_dir>/accounts.ini`: `[lane] play_user /
  play_pass` (what Use for RUN / Play set), `[passwords]` (accounts made here;
  plain text, this is a dev lane), `[queue] <name>.<n> = <cheat>`.
- **Online or offline is the server's call**, from the attached lane's player
  list. Online the edit is a cheat for that player on the control link and
  the view is refreshed through `::checkpoint <leg>` (legs 90001.. are this
  module's; the quest harness counts from 1); the checkpoint carries every
  non-zero varp, so the Vars tab shows the live state. Offline the save is
  edited in place. The status fact says which ("online: ::setlevel cooking
  60 for acctb" / "offline: wrote saves/acctb.ini (cooking level 60)") and the
  editor console repeats it.
- **Quests** have no offline form: `::complete` / `::questmark` are queued and
  sent 3 s after the account's next login (the login's scene settles first);
  the panel's Quests tab and `account-view` list the queue. A quest's state
  is read from the save (or the checkpoint) through its `::complete` arm's
  var and constant; the four quests whose arm goes through a proc (Clock
  Tower, The Dig Site, Plague City, Vampyre Slayer) read `[?]`.
- **Passwords.** The server claims a password at an account's FIRST login
  (torirs_server_auth.c), so New account writes the save and removes any
  stale `saves/accounts/<name>.ini`; Use for RUN / Play needs a password the
  editor knows (made here, or the lane's default `maped<port>`).
- **End to end** (`make -C src maped-live-e2e-accounts`, writes only under
  `src/<obj>/tmp/live_accounts`): the real torirsserver and js5_server on
  the lane's packs and cache, the real play client; an account made and
  edited offline (attack 40) is the play account; after `play` the checkpoint
  shows attack 40 and an online `stats.cooking 60`; `account-place` is
  `::tele` and the server's player list moves; a quest mark online is
  `::questmark` and the snapshot reads complete; after `play-stop` an offline
  strength 33 and a QUEUED Sheep Shearer mark both reach the character at the
  next login (two logins on the server). Last run 2026-11-02: 26 ok, 0 failed
  (LIVE_E2E_OBJ=build_agent_acct_opt, server binaries from build_opt).

## Ergonomics (2026-11-03)

The owner's direction: a fully fledged game editor, ergonomic the way Unity,
Unreal, Godot, Tiled and Hammer are. Four strands landed, each a hotkey
action in the one table (docs/MAPED_HOTKEYS.md has the user's reference):

**Merged with workbench 6.x (2026-11-06).** This work and the workbench
branch (the sections below) were built side by side from phase 14; where
both made the same thing, the merge kept the workbench's and carried this
work's extras into it, so parts of the text below describe the pre-merge
shape:

- **One command palette**: the workbench's (`view.command_palette`,
  **Ctrl+K**, module "cmdpalette", edpanel_palette_cmd.c). edpanel_command.c,
  `view.command` and Ctrl+P are gone. Carried over: Go to tile...
  (`Editor_CmdPalette_OpenWith`), absolute tiles with an optional level as
  `EDITOR_CMDPAL_KIND_TILE` rows (the camera now, or the open with
  `ui->open_goto_*`; both numbers under 256 stay a square for the picker's
  Find), the Recent group first (`EDITOR_CMDPAL_RECENT`), and the App-bound
  actions handed over through `Editor_CmdPalette_TakePendingAction` instead
  of refused. The records come from the palette's own CATALOG_QUERY, so
  `Editor_Catalog_Find` is gone.
- **One status bar**: the workbench's (always on, no View row) with this
  work's fields as one more run, the "info" track after the hover: the
  selection, the tool and a brush's radius, the edit level, the unsaved
  count, live / RUN, fps under `TORIRS_EDITOR_FPS=1`. The lane field is the
  workbench's lane chip (`Editor_Lanes_ChipText`); `Editor_Publish_LaneName`
  and the boot-time LANE_INFO ask are gone.
- **Contexts**: K and Q kept bits 8 and 9; D and U moved to 10 and 11
  (`EDITOR_HK_CTX_COUNT` 12); `Editor_HotkeyContextFacts::camera_unity`
  picks one, so the status bar's hint resolves under the same scheme.
- **Compact dock and panel groups**: a group's frame is one slot that is
  never collapsed (its strip is the tabs); only ungrouped panels collapse to
  their title bars. The world's 60 % cap applies to the console's group strip
  as to the plain console (`ui_strip_capped`). The collapsed height is
  measured at the chrome's quarter scale, as `dbg_build_window` builds it.
  A workspace snapshot carries no collapsed wish (the window's top section
  keeps it).
- **Catalog**: the warm build still runs on its worker thread. A
  `TORIRSMAPED_CATALOG_PROGRESS` query while the thread builds gets an empty
  page flagged PARTIAL with the progress the thread publishes (never a read
  racing it); the sliced build (web, win32, `TORIRS_CATALOG_WARM_THREAD=0`)
  answers from the partial index as the workbench made it. The query's
  `TORIRSMAPED_CATALOG_NOWAIT` is gone (its value 2 is PROGRESS); the atlas's
  NOWAIT is `TORIRSMAPED_CATALOG_SQUARES_NOWAIT` (2) in CATALOG_SQUARES'
  flags, sent only to a server with `TORIRSMAPED_CAP_CATALOG_SQUARES_NOWAIT`.
- **Lanes**: the workbench's Win32 link emulation (junctions, hard links)
  replaced this work's refusal to make lanes on Windows. The change scan
  stays on its thread; its result also fills the lane count cache, keyed by
  the write counters at the scan's start.
- **Double-click** in the World (Select tool): on a loc it is the
  workbench's Select same; on a tile with no loc, this work's recentre.

- **Command palette** (`view.command`, **Ctrl+P**, View > Command palette;
  module "command", src/editor/panels/edpanel_command.c). A chrome MODAL with
  one box and twelve rows: every edge / repeat action of the hotkey table
  (name, group, id; the chord after a tab; Enter runs it -- the App-bound
  ones, the camera and the eyedroppers, land in the App's dispatcher on the
  next frame through `Editor_Command_TakePendingAction`), "Open <panel>" for
  every View-menu panel, squares by name or coordinates through the picker's
  own parser over the atlas it holds (`Editor_PickerSearch`; "m50" lists the
  atlas's m50_* squares; an empty cell offers New square there), absolute
  tiles ("3222,3218": the camera goes there at once when the square is in
  the scene, else the square is opened with the tile as the camera's landing
  -- `ui->open_goto_*` -> `Editor_Panel.sq_open_goto_*` ->
  `app_map_editor_open_pending_square`), catalog records by kind
  ("loc:oak", "npc:hans", "obj:knife": `Editor_Catalog_Find` sets the
  Catalog's Kind and Find and drives its model whether or not the panel is
  open, the held page's rows are the hits, Enter is
  `Editor_Catalog_PickRecord`), and the recent squares (first, with the box
  empty). Matching: prefix > word-start > substring > subsequence. Up / Down
  and Escape reach it through `Editor_Command_Feed` from the App's hotkey
  frame (the box has the keyboard, so the gates are shut, exactly as the
  Hotkeys panel's capture is fed); Enter is the box's own activation. The
  palette closes before it runs the hit. Nothing scans the content tree per
  keystroke: the catalog query settles and asks the server once per text.
  Ctrl+Shift+P stays the Preview panel's (editor_hotkeys_test pins it).
- **Go to tile...** (`view.goto_tile`, **Ctrl+Shift+G**; Ctrl+G stays Snap):
  the palette prefilled with the hovered tile's coordinates (the App), else
  the latched tile's or the selection's (the panel side).
- **Recent squares** (File > Recent squares, the last ten, newest first;
  src/editor/editor_recent.{h,c}, src/editor/task_editor_recent_io.{h,c}).
  Noted by `Editor_UiOpenSquare` (every open path ends there), remembered
  per user in `editor_recent.ini` beside `editor_layout.ini` under the same
  path rule (`TORIRS_EDITOR_RECENT`; read at boot and written on change by
  app_editor_hotkeys.c's second io machine, flushed at shutdown; never in
  the content tree). The same file holds the camera scheme (`[camera]
  scheme = default|unity`). The menu bar's File rows grow by a header and
  one row per square; a row opens it as the picker's Open does (the save
  guard first). edpanel_menubar.c `menubar_open_recent`.
- **Camera schemes** (`camera.scheme`, unbound, the Hotkeys panel's row or
  the palette's "Camera scheme"; remembered as above). The default scheme is
  untouched. The **Unity** scheme (src/app/app_camera.c
  `app_camera_unity_*`): right-drag looks (yaw / pitch, pitch clamped 64..512;
  a right CLICK still opens the minimenu, a drag past 4 px hides it),
  middle-drag pans the ground plane (faster the higher the eye), the wheel
  zooms toward the tile under the cursor (a sixth of the distance per notch,
  never under two tiles), Alt+left-drag orbits the selection's first loc
  (else the latched tile, else the hovered tile) at the eye's distance, and
  **F frames** the selection (`camera.frame_key`, context **U**; Fly down
  keeps F under the default scheme, context **D** -- two new exclusive
  contexts; a caller stating neither is on D, so every older test and the
  resolver's callers are unchanged). Home / Ctrl+Home frame under both
  schemes. The presses are latched by `app_camera_unity_gate` from the hotkey
  frame, BEFORE the editor's world click, which then sees
  `input_frame_consumed` and never paints or selects under an orbit or a
  pan; the drags move the camera in `app_world_camera_mouse` and end in
  `app_camera_keep_over_scene` (the clamp). Its state is the subsystem's
  file-static (struct App may not grow). **Double-click** a tile in the
  World panel (Select tool) recentres the camera on it at the distance it
  has (`app_map_editor_camera_to_tile(..., zoom 0)`: the pose of the eye's
  own view ray, `EditorView_PoseFromEye`), under either scheme.
- **Selection**: `edit.select_kind` **Ctrl+Shift+L** selects every loc of
  the catalog's picked loc id (else of the selection's first loc) over the
  open squares on the pinned level (else the selection's); `edit.invert`
  **Ctrl+I** makes the open squares' unselected locs on that level the
  selection. Both App-free in edpanel_hotkeys.c, both capped by the 256-loc
  selection (the console says how many were left out). **Hide selected /
  Show all was skipped**: the world's scenery entities carry no per-loc
  visibility flag (src/world/entity_scenery.h), so an overlay-only hide
  would be a remove-and-restore through the edit layout per loc -- not
  cheap, and not "overlay-only".
- Fixed on the way: the Hotkeys panel's group headers were in the doc's
  order (File, Edit, Place, Palette, Tools, ...) while the table is grouped
  File, Edit, Place, Tools, Brush, Level, Camera, Palette, View; the
  Register asserts the two agree, and the asserts are compiled out in the
  OPT build, so the headers were mislabelled from "Palette" on (the Tools
  rows sat under a "Palette" header). k_groups now follows the table.

Tests: editor_hotkeys_test `test_ergonomics` (the chords, F under D / U,
D and U exclusive and clash-free, the recent ring and its codec: 1095
checks); editor_panels_test `test_command_palette` (the module, View row,
open / focus, 'undo' / 'ssqn' / 'open hotk' hits with the chosen row yellow,
Up / Down / Escape, Enter runs and closes, m50_51 opens, "m50" lists, a tile
carries the landing, an empty cell offers New, recents in File and in the
palette, the scheme toggle, Select kind / Invert over m50_50, and
"loc:crate" / "npc:gua" picking through the catalog harness: 2711 checks, 0
failed). `make -C src check-pt-switch check-tree-walks` pass.
`check-app-boundary` fails at HEAD already (struct App has 380 top-level
fields against the baseline's 378; src/app.h is untouched here -- someone
committed two fields without raising the ceiling); editor_hotkeys_test's doc
check fails on `view.compact`, the layout strand's row, whose doc lines were
not in yet when this ran.

Photographs (1280x800, TORIRS_HIDPI=0, against a mirror of the tree on a
scratch lane; the scratchpad's `ergo/`): `palette_undo.bmp` (Ctrl+P, "undo"
typed: the Edit: Undo row first, yellow, Ctrl+Z after the tab),
`palette_tile.bmp` ("3222,3218": Go to tile 3222,3218 L0 / m50_50),
`palette_goto.bmp` (Ctrl+Shift+G over a parked hover: the box prefilled
"3222,3218"), `file_recent.bmp` (the File menu open with Recent squares),
`palette_record.bmp` ("loc:oak": the catalog's page as Pick loc rows),
`palette_tile_run.bmp` ("3286,3420" then Enter: the palette gone, m51_53
opened and the camera over the tile -- TORIRS_SIM_TYPE's `k84` is the Enter
the box sees; a TORIRS_SIM_HOTKEY `return` writes the key arrays only and
never reaches a text input).

Open:

- The Unity scheme's right-drag look and the minimenu: the menu opens on
  the right PRESS (app_frame.c, the interact pass) and is hidden once the
  drag passes 4 px; a very short drag therefore flashes the menu for a
  frame or two. Opening the menu on release instead is the interact pass's
  (the performance strand owns app_frame.c).
- The palette's record hits wait one settle frame plus a server round trip
  ("asking the catalog..."); Enter during the wait keeps the box. Typing a
  record query also moves the Catalog panel's Kind and Find (by design: the
  Catalog shows what was asked), which a user who had a different kind
  there will notice.
- In the first seconds of a boot the server's square index is still
  building and the picker has no atlas: the palette then parses
  coordinates itself (`cmd_hits_coords_blind`: "m50_50", "50 50" a square,
  "3222,3218" a tile, both under 256 a square) and the row says "(atlas
  still building)"; an open of a square that is not there says so in the
  console. The first photograph of "3222,3218" was taken in that window and
  read "nothing matches" before this fallback.
- The palette lists squares by NAME only for an "m<digits>" prefix (the
  atlas is thousands of squares; "s" alone would list the world) and area
  names are not offered (the picker's search handles them; the palette does
  not -- a "Show area <name>" hit would need a non-const picker).
- An App-bound action run from the palette (the camera, an eyedropper,
  Paste at the hovered tile) runs on the NEXT frame, with the pointer still
  where it was over the palette: the hover-dependent ones say "point at the
  tile" in the console.
- Alt+left-drag orbit: the pivot's ground is read on the edit level (Auto =
  the loc's / tile's own level); the orbit distance is the eye's distance at
  the press, so an eye far from the pivot orbits a wide circle -- frame it
  first (Home / F).
- No photograph of a drag (the headless sim has clicks and chords, no
  drags): the Unity scheme is proven by editor_hotkeys_test's context checks
  and by reading app_camera.c; a live run is the owner's.

## Performance (2026-11-03)

Owner: "It should be fast and efficient." Measured with the editor's own
frame clocks, headless, on a mirror of the real tree (the recipe below):
`TORIRS_EDITOR_FRAME_STATS=1` prints, at exit, the frames sampled (from the
first drawn world frame; `TORIRS_EDITOR_FRAME_STATS_FROM=<n>` starts later,
to isolate a scenario) and the p50 / p95 / max / mean in ms of each phase:
the whole frame (App_RunOnce + App_Render), the editor UI tick (chrome tick,
overlay and editor panel ticks; the chrome build inside it, on its own row),
the hover pick finish + ghost / preview / palette updates, the async pump
(IO and the embedded editor server), logic + movers, the edit layout's
remesh flush, the game tree's interact pass, the render (world + 2D) and
the world's DRAW_MODEL commands inside it; counters (chrome rebuilds, pick
frames, preview rasters); and the startup marks "world drawn" and "catalog
ready" as seconds from process start with the resident set at each
(src/perf/frame_stats.h, header-only; the clocks are one branch when off;
never TORIRS_PERF). The sites: app/app_frame.c, app/app_render.c,
app/app_debug_ui.c, app/app_map_editor.c, platform/platform_renderer_soft3d.c,
torirsmaped/torirs_maped_catalog.c.

Recipe (private no-LTO OPT build so `sample` has stacks; a mirror so the
real tree is never opened by a lock-taking editor; the lane under a scratch
repo root; headless at 1280x800 on soft3d):

    make -C src -j12 OPT=1 PROFILE=1 OPT_RELEASE_CFLAGS=-DNDEBUG OPT_RELEASE_LDFLAGS= \
      PLATFORM_OBJ_BASE=build_agent_perf PLATFORM_TARGET=build_agent_perf_opt_prof/torirs \
      build_agent_perf_opt_prof/torirs
    python3 src/maped/test/live_e2e/mirror.py OSRS-Content/osrs-content <scratch>/content
    # manifest: manifests/manifest_osrs242_mapeditor.ini with absolute dir=,
    # content_dir=<scratch>/content, repo_root=<scratch>/repo, revconfig_cache=,
    # and `window=1280x800` under [ui:boot]
    TORIRS_HIDPI=0 TORIRS_PREFS="" TORIRS_EDITOR_LAYOUT="" TORIRS_PLUGINS=0 SDL_VIDEODRIVER=dummy \
    TORIRS_EDITOR_FRAME_STATS=1 TORIRS_MAX_FRAMES=600 [scenario envs] \
      ./src/build_agent_perf_opt_prof/torirs --manifest <scratch>/manifest.ini
    # scenarios: TORIRS_SIM_HOTKEY="100,w,hold=200" (pan); TORIRS_EDITOR_CATALOG="loc:oak@200";
    # TORIRS_EDITOR_PICKER="Full map,fit" (the Squares panel is in the default
    # layout now); TORIRS_SIM_HOTKEY="500,3" TORIRS_SIM_DRAG="550,300,300,700,500,1,1,300"
    # TORIRS_EDITOR_FRAME_STATS_FROM=540 (a Raise brush drag over the World panel);
    # TORIRS_EDITOR_POPOUT="preview@150"; TORIRS_SIM_CLICK_AT="150,500,350" (mouse parked
    # over the world: the hover pick every frame). `ps -o rss` sampled every 0.25 s
    # beside it; `sample <pid> 8` for the hot functions.

The numbers (this Mac, the mirror; frame = p50 / p95 ms; the loop is paced at
20 ms so a frame under that is idle time given back):

| scenario | before | after | what changed |
|---|---|---|---|
| idle, default panels | frame 8.4 / 9.6 ms; the async pump 6.2 ms every frame for the catalog's warm build (6 ms slices for the ~25 s it took at that pace; never finished within 1200 frames here) | 2.4 / 2.7 ms; pump 0.02 ms; catalog ready 8.4 s after start | the warm build runs on a worker thread (`cat_warm_thread_*`; `TORIRS_CATALOG_WARM_THREAD=0` keeps the slices) |
| boot with the Square picker in the layout (the default since 2026-11-03) | world drawn 8.3-8.7 s after start: the picker's atlas request at frame 2 ran the whole build synchronously; the same 6.8-7.0 s stall for a Catalog opened at frame 200 or F4 at frame 100 | world drawn 0.12 s; the panel says "the catalog index is still building" and asks again each tick; the Catalog's count row says "the catalog index is building..." and re-asks every 10 ticks | `TORIRSMAPED_CATALOG_NOWAIT` on the two polling requests (query, squares) answers `EDITOR_HOST_BUSY` while the thread builds; every other catalog request waits as before (a click's record view, the Triggers/Spawns/Accounts lookups) |
| Catalog open at frame 200 with "oak" | one 6.76 s frame | frame 2.5 / 4.0 ms, the query answers at frame ~409 | as above |
| Square picker, Full map (8,370 regions) | ps rss peak 1355 MB, exit 1181 MB; assets group peak 773 MB; the pump stalled 400 ms (every region's tiles asked for at once and held until baked) | peak 877 MB, exit 713 MB; assets peak 268 MB; pump max 137 ms; the whole map in 914 ms (was 1073) | `OVERVIEW_REGIONS_IN_FLIGHT` 256: a poll stops asking for more regions once that many are loading or waiting (`TORIRS_WORLDMAP_INFLIGHT=<n>`, 0 = no cap) |
| catalog index resident | editor group 292 MB live after the build (peak 420): pools 79 MB used of 123, contribs with a third of slack | 218 MB live (peak 346) | `cat_source_trim`: a parsed source gives back its pool and contrib growth slack (`TORIRS_CATALOG_TRACE=1` prints the breakdown) |
| camera pan (W held 200 frames) | 7.5 / 9.3 ms | 1.0 / 2.8 ms | the pump (above); the pan itself was never the cost |
| popped-out Preview attached | 8.7 / 10.0 ms | 2.5 / 2.9 ms | the pump (above) |
| Raise brush drag, 300 frames | 3.7 / 5.1 ms (after the build); remesh flush p50 0.001 ms, max 1.8 ms | same | nothing: the edit layout's per-tile remesh is already cheap |
| startup | world drawn 0.17-0.20 s, rss 183 MB (0.13 s with the thread); catalog ready never within 24 s (slices) | catalog ready 8.0-8.4 s on the mirror (scan 4.6 s + parse 3.7 s + merge 0.1 s of IO-bound work; the real tree's scan is faster), rss 450 MB at that moment; exit rss 192-206 MB | |
| random hitches in use (2026-11-08; the owner: camera moves, placing the player, selecting a loc) | every 15 s a 3.4-5.9 s frame: the status bar's lane chip asked LANE_CHANGES COUNT_ONLY and the embedded server walked the lane's 100k-file mirror on the UI thread, because the count cache expired on every ACCOUNTS_LIST (a read the Accounts panel and the spawn preview send every few seconds counted as a lane write); every square open a 2-3 s frame: the `spawns:m<x>_<z>` document walked server/scripts (58k entries) synchronously | no frame over 30 ms after boot in 1500 idle frames; the chip's ask answers from the cache or starts the scan thread and the landing broadcasts FACT_LANE_COUNT; the square open answers from the spawn snapshot | note_lane_writes counts only writing account requests; every lane walk goes through lanes_scan_want (one scan thread, a queue of lane names, torirs_maped_lanes_wire.c); the Publish panel takes a broadcast count into its list row by name and re-asks after 4 periods if an answer never came; `scripted_spawns` snapshot (editor_host_local.c). Found with `TORIRS_EDITOR_FRAME_STATS_SLOW=<ms>` (one line per slow loop iteration with every phase) and `TORIRS_MAPED_PUMP_TRACE=<ms>` (every request and service step of the embedded server over that long, DOC_OPEN with its key); `TORIRSMAPED_LANES_DEBUG=1` says why a count wants a walk, `TORIRS_EDITOR_HOST_TRACE=1` times the spawn walk |
| every Save (2026-11-10, Windows, the real tree in a sparse lane) | one frame of ~0.26 s even when nothing was written, ~0.41 s when one square was: after every save the server ran `ToriRSMapEdCatalog_RefreshMaps` (11,862 stats of maps/ and the lanes' maps/, 190-200 ms here, mingw's readdir has no d_type), a full merge when anything changed, and set `invalid` so the next query swept all ~42,000 sources again (1.8 s of stats on the service pass) | a save that wrote nothing: no frame over 50 ms; a one-tile height edit saved: 190-215 ms, of which the catalog is 1 stat + 9 ms parse + 160-181 ms merge | the server notes the files each save wrote or removed (`saved_doc_note`, `Editor_HostDocPath`, an interface's sibling .compack) and `catalog_saved` hands them to `ToriRSMapEdCatalog_RefreshPaths` (one stat each, re-parsed whatever the stat says, then one merge; no `invalid`, a sweep in flight runs on). RefreshMaps is left for Discard. What is left is the full `cat_merge` of ~408,000 records (130-180 ms; a save of `configs/all.loc` adds a 90 ms parse); a per-source merge would remove it. `TORIRS_CATALOG_TRACE=1` prints one `refresh paths:` line per save |

Hot functions (`sample`, 8 s, idle with the mouse over the world): 83% of
the main thread sleeps in the pacer; of the working samples 826/938 are
the render -- `toridraw_textri_flat_trans_lerp8_v3_presorted_run_xrgb8888_asm`
172, `toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm` 144,
`ToriDraw_ProjectWithVTable` 72, `toridraw_compute_projected_face_order_small_general`
59, `scene_occluders_point_hidden_impl` 51, the SDL blit 48,
`ToriDraw2D_FillRect` 45, `bucket_paint_world` 44 -- i.e. the software
rasteriser drawing the World panel (1.4 ms of the 2.4 ms frame), then the
2D chrome raster (~0.5 ms). During the warm build (the boot sample): 1697 of
1788 samples under `cat_job_step`, of which 840 + 432 in `read` and 233 in
`open` -- the parse is the file reads, not the parsing.

Measured and left alone, with why:

- The chrome build: 4-7 rebuilds in 600 idle frames, 59 in a 360-frame
  brush drag, 0.004 ms mean -- it is retained already; a per-panel display
  list cache would save nothing measurable.
- The editor UI tick: 0.08 ms p50 with every default panel ticking. Ticking
  only visible panels would save microseconds.
- The hover pick: render 2.16 ms with the mouse parked over the world
  (651 pick frames) against 2.18 without -- the per-model hit test is in
  the noise; a pick cache keyed on mouse + camera + geometry is not worth
  its staleness risk.
- The Preview's 640x512 raster: only on a pose or subject change already
  (`app_map_editor_preview_update` latches both); an orbit re-rasters per
  key, which is the feature.
- The world draw (1.4 ms, the rasteriser) and the 2D raster (0.5 ms) are
  the frame now; nothing in the editor's own code is above 0.1 ms p50.
- The Full map's remaining assets peak (268 MB) is the loc, underlay and
  overlay types every region's floors ask the provider for; they stay
  resident by the provider's design. The one-pixel bakes (16 KB x regions
  seen, 134 MB for the Full map) are still never freed.
- The max frame of ~90 ms in every run is the boot's world load landing in
  the async pump (frame 6), before the first drawn frame.
- A request that is not NOWAIT during the warm build still waits for the
  rest of it (now a thread that has run flat out since boot): a Triggers,
  Spawns or Accounts lookup in the first ~8 s. A NOWAIT on those needs a
  retry in each panel.
- NOT mine, seen on the last runs of the day (14:54 tree): a lanes request
  at frame 2 (`handle_lanes` -> `changes_walk_base`, torirs_maped_lanes.c,
  under another strand's edit) stats and lstats every file of the base
  tree synchronously inside the embed pump: the world drew 3.3-7.0 s after
  start again (0.12 s without it). The same shape as the catalog's warm
  build was: it wants a thread or slices, and a BUSY answer meanwhile.

## Screens and portability (2026-11-03)

Owner's direction: "a fully fledged game editor. It should work on many
machines and screen sizes." This strand: small screens, the layout engine,
a status bar, the Windows and web builds, the window's box across runs.

**Compact panels** (editor_ui.c `Editor_UiLayout`, the dock column). Below
`EDP_COMPACT_H` x `EDP_COMPACT_W` = 800 rows or 1280 columns of LOGICAL
pixels (device px / chrome scale: 1366x768, 1280x720, 1024x600, 960x600 at
1x; a 1440x900 MacBook at 2x is 2880x1800 and is NOT compact) the column
holds EVERY default member -- Terrain, Palette, Inspector, Squares, Areas,
Catalog, Preview, History -- each either expanded or **collapsed to its title
bar**. The chrome grew a collapsed panel state for it
(`ToriRSChrome_PanelSetCollapsed` / `PanelIsCollapsed` /
`PanelCollapsedHeight`: the header block and the bottom border, no rows, no
tab strip, no scroll bar, no grip; the title still drags, closes, pops out)
and a title single-click (`ToriRSChrome_TakeTitleClick`: a press and release
on a title that moved the panel no further than the double-click slop; the
first click of a double-click reports here too, a drag that moved it does
not; a clicked panel is not snapped). Collapse over the chrome's tab strip
because the tab strip is per PANEL and stamps that panel's own widgets:
docking the modules as tabs of one panel would mean re-parenting every
module's widget list, activations and GRAPH events into a shared panel (the
chrome has no such move), or rewriting every module to build into one;
collapsing keeps each module's panel, dispatch, saved box and View row
exactly as they are, and the layout engine only decides heights.
Rule: every member gets its title bar first (a title that does not even fit
is hidden, last first); then, most recently expanded first (`expanded[]` is
the user's wish, all 1 at boot, `expand_serial[]` the order), each panel
takes its minimum (`EDP_DOCK_MIN_H`) while the column still holds it, so
expanding one collapses the least recently expanded when there is no room;
spare height is shared among the expanded ones (the stretcher, the picker's
well, takes the remainder). A title click on a collapsed docked panel
expands it; on an expanded one collapses it; a double-click still maximises
(a collapsed panel is expanded first; Restore puts it back in the column).
A collapsed panel dragged out of the column expands on its next title click.
**Setting**: View > "Compact panels: on/off (auto)" and **Ctrl+Shift+C**
(`EDITOR_HK_VIEW_COMPACT`) toggle it to the opposite of what is in effect
(`Editor_UiSetCompact`: -1 automatic, 0 never, 1 always); Reset layout
returns to automatic. The layout file records `compact = -1/0/1` under
`[layout]` and `collapsed = 0/1` per panel (the wish comes back on the same
surface). The tool rail wraps into up to 8 columns rather than running off
the bottom; a menu list wider than what is right of its title is pulled
back inside the surface (`dbg_dropdown_rect`), so the View menu never clips
at narrow widths. **The world keeps at least `EDP_WORLD_MIN_PERCENT` (60%)
of the surface's area at every size from 960x600 logical up**: the console
strip under the world is shortened (scrolling) to what keeps it, and dropped
below half a dock panel (it then waits in the View menu) -- at 1280x800 1x
the strip is 96 px (was 149), at 1366x768 88 px, at 1024x600 none. Below
960x600 logical the 200 px dock floor and the rail alone take more.

**Status bar** (module "statusbar", editor/panels/edpanel_statusbar.c): a
MENUBAR-style one-line strip along the bottom of the main window, a layout
member like the menu bar (the dock column, the world and the console end
above it; the maximise area too); View > "Status bar" is its one show/hide
row and the layout file keeps it. Fields: the hovered tile `tile x,z Ln
m<mx>_<mz>` (app_services.hover_tile), the selection `sel: 3 locs | 2 spawns
| rect N tiles | tile | none`, the tool (a brush with its radius), `level
Auto | level 1 +above`, `lane <name>  N unsaved` (the Publish panel's lane
fact -- `Editor_Publish_LaneName`; the panel now asks the lane facts once at
boot whatever shows -- and `Editor_DocUnsavedCount`), `live <state>  RUN
<progress line | done | stopped>` (`Editor_LivePanelModel`), and `N fps`
only with `TORIRS_EDITOR_FPS=1` (frames per chrome second). Every field is
rebuilt only when its value changes, and `ToriRSChrome_SetText` is a no-op
on the same text, so a still frame touches nothing. The ergonomics strand's
status needs land here; there is no second bar.

**Window box across runs**: the layout file also records `window_x/y/w/h`
(window points; `Editor_UiSetWindowBox`, told every frame from
`PlatformWindow_WindowBox`, written when the layout settles). main.c reads
the file synchronously before the window exists
(`main_editor_layout_window`, editor boots only, `TORIRS_EDITOR_LAYOUT=""`
reads nothing) and sizes the window to it over the manifest's `window=`
(an explicit `--window` still wins), then `PlatformWindow_SetWindowPosition`
puts it back where it was once it exists (SDL, Win32 GDI; Android has no
desktop).

**Windows (mingw)**: `make -C src PLATFORM=win64 CC=x86_64-w64-mingw32-gcc
OPT=1 PLATFORM_OBJ_BASE=build_agent_win64
PLATFORM_TARGET=build_agent_win64/torirs_win64.exe
build_agent_win64/torirs_win64.exe` links the whole client with the embedded
editor server (PE32+ x86-64, 10.5 MB). What failed and was guarded:
`M_PI` in app/app_cam_arc.h (MinGW's math.h hides it: a guarded define);
`mkdir(path, mode)` in torirsmaped/torirs_maped_accounts.c (`_mkdir` on
Windows, as torirs_server_savefile.c does); and torirsmaped/torirs_maped_lanes.c,
the lane module the client links: `mkdir`, `realpath` (`_fullpath`),
`lstat`/`S_ISLNK` (stat; nothing is a link), `kill(pid, 0)`
(`OpenProcess`) go through one shim block, and the two `symlink` sites
(a lane's mirror, a relink on Publish/Discard) are
`TORIRSMAPED_LANES_SYMLINKS 0` on Windows: Mirror, Create and the relinks
refuse with `why` ("a lane mirror needs symlinks, which this Windows build
has no support for (edit the main tree: [editor:boot] lane=main)") -- never
a mirror of copies that would silently diverge. Lookup, List, lane.ini and
the main lane work. The daemon (posix_spawn, the live link) stays POSIX
behind the existing `TORIRSMAPED_JOBS_NATIVE`. Not run on a Windows machine.

**Web**: `make -C src PLATFORM=web OPT=1 PLATFORM_OBJ_BASE=build_agent_web
<repo>/build-web/torirs.js` built the page (the `web` target's `servers`
prerequisite would link into src/build, so the page target alone); the
native chrome draws the editor there, so compact panels and the status bar
are the same code. Photographed through DevTools in headless Google Chrome
(scratchpad web/shoot.py: navigate, wait, set the device metrics, capture)
against a js5_server (`--max-pending 65535`), the torirsmaped daemon on a
mirror and src/build/io_server with `--maped-port`: at a 1024x600 and a
1366x768 browser window the page's canvas (768 px wide in the harness
layout; "full canvas" takes the window) showed the editor attached to the
daemon ("editor server = the native torirsmaped daemon"), the compact dock
(Terrain and Palette expanded, the rest as title bars), the rail in two
columns and the status bar. The page assets in build-web/ were stale
(torirs_host.js from Oct 22 never asked the daemon): the `web` target's
asset copy was repeated by hand; a page built by `make -C src web` gets it.

**Tests**: editor_panels_test `test_layout` boots 1280x800, 1530x1006,
1920x1080, 2560x1440, 1366x768, 1280x720, 1024x600 and 960x600 at 1x and
1280x800, 1530x1006, 1920x1080, 2560x1440 and 2880x1800 at 2x; every size:
no panel covers the world, all on the surface, no overlaps, the world keeps
60% (from 960x600 logical), the status bar spans the bottom and everything
ends above it, the rail ends above the status bar, compact below 1280x800
logical with every member in the column and a collapsed one exactly its
collapsed height; at 1024x600 a title click expands a collapsed panel
(collapsing another when there is no room) and collapses an expanded one,
the file states `compact`/`collapsed`; at 1920x1080 the View row and
`EDITOR_HK_VIEW_COMPACT` force it on and off and Reset returns to automatic;
`test_layout_codec` round-trips compact, the window box and a collapsed
panel; `test_maximise` finds the menu bar by module (the status bar is a
MENUBAR-style strip too) and expects the area above the status bar.
editor_panels_test: 2660 checks, 0 failed (was 1641; 2711 with the
ergonomics strand's tests beside them). `make test-editor test-uitree`:
every suite green (uitree "All UITree tests passed"); editor_hotkeys_test
needs the `view.compact` rows in docs/MAPED_HOTKEYS.md (added). Under
load (the web photograph's Chrome beside it) editor_panels_test once lost
its catalog-index wait and segfaulted after it; alone it passes.
`check-pt-switch` and `check-tree-walks` PASS; `check-app-boundary` is red
on `struct App` 380 fields vs the 378 baseline -- not this strand's (app.h
untouched here).

**Photographs** (headless, SDL dummy, TORIRS_EXIT_BMP at frame 400, the
mirror + a scratch manifest; looked at, crops at 2x): native 960x600,
1024x600 (compact: Terrain and Palette expanded, Inspector / Squares / Areas /
Catalog / Preview / History as title bars, the rail in two columns, the
status bar reading `tile -  sel: none  Select  level Auto  lane working
live detached`), 1280x720, 1366x768 (Terrain, Palette, Inspector, Areas
expanded; the console strip 88 px), 2880x1800 at chrome scale 2 (not
compact; the column Terrain, Palette, Inspector, Areas, Catalog). Paths in
the strand's report.

**Open**: the Windows binary has not been run on Windows; a lane on
Windows needs a mirror without symlinks (junctions or copies with a
manifest) before `lane=working` works there; the DOM executor still has no
dock layout (the web draws the native chrome).

## Workbench step 6.0 (date 2026-11-04)

The first step of the workbench design's build order (section 16, "6.0
Chrome: keycaps, tooltips, groups, status bar"; section 15 "Hotkeys at the
action"). Three slices merged into v3-workbench (chrome `b249c631`, status
bar `2333f02a`, panel groups `c866edb6`), then the editor wired onto the
chrome's new API.

What landed:

- **Keycaps, badges, tooltip chords** (src/ui). A BUTTON wears a chord as a
  keycap right-aligned in its plate, Small face, caption elided beside it
  (`ToriRSChrome_ButtonSetChord`); an ICONBUTTON wears a badge in its icon
  box's corner (`ToriRSChrome_IconButtonSetChord`); a tooltip ends with a
  keycap (`ToriRSChrome_SetTooltipChord`, falling back to the chord a button
  wears). `TORIRS_CHROME_CHORD_MAX` 24. Executor stream: new
  `TORIRS_CHROME_CMD_WIDGET_CHORD = 28`, WIDGET_TOOLTIP carries the tooltip
  chord in `text`; the DOM executor (plugin_chrome runtime, modern and IE8
  CSS) draws the same `<kbd>` keycap and badge.
- **R2: no silent truncation.** The chrome keeps a 16-entry ring of cut
  strings (`ToriRSChrome_TakeTruncation`, `ToriRSChrome_TakeTruncationsDropped`,
  `ToriRSChrome_TruncationFieldName`); `Editor_UiTick` drains it every frame
  into the console, one line per record ("chrome: History text cut to 191 of
  300 bytes (widget N): ..."), plus one line for any the ring dropped.
- **Chords at the action** (src/editor/panels, `edp_button_chord`,
  `edp_iconbutton_chord`, `edp_tooltip_chord`; `Editor_HotkeysJoin` spells a
  keycap or a compact pair). Each tool on the rail wears its chord as a badge
  and its tooltip says what the click does; Save / New square / Squares say
  theirs in the tooltip only. Keycaps on Locs (the four nudges, Rotate,
  Reshape, Duplicate, Delete, Swap), Spawns (Duplicate, Delete / suppress),
  Rectangle (Copy, Paste, Fill, Clear, Mark area), History (Undo, Redo),
  Catalog (Add to palette) and Catalog palette (Add current pick). The
  Terrain sliders read "Radius  [ ]", "Step  Ctrl+[ ]", "Strength  Shift+[ ]".
  The tooltip names every chord when there are two ("Del, Backspace"). All of
  them relabel on the frame after a rebind (`ui->hotkeys.serial`).
- **Status bar** (module "statusbar", src/editor/panels/edpanel_statusbar.c):
  a 20 px strip across the foot, a MENUBAR-style panel of LABELs: the armed
  tool's live hint rows from `Editor_HotkeysHint` ("[Q] rotate"), the hover
  readout ("3222,3218 L1 | m50_50"), the status sentence (`Editor_UiStatus`,
  shown 6 s, kept in the console; `Editor_UiStatusText`), and "read-only" from
  FACT_HELLO. `Editor_HotkeyContextsFrom` derives the contexts. The dock,
  the console strip and a maximised panel stop above it. Its chords stay text
  ("[Q]"): the chrome's keycap is a BUTTON / ICONBUTTON / tooltip drawing and
  has no LABEL form.
- **Panel groups** (editor_ui.c): `Editor_UiRegisterGroup`,
  `Editor_UiJoinGroup`, `Editor_UiLeaveGroup`, `Editor_UiResetGroups`,
  `Editor_UiFindGroup`, `Editor_UiGroupOf`, `Editor_UiPanelInStrip`,
  `Editor_UiGroupShown`, `Editor_UiGroupShow`. Three default groups:
  right-top (Catalog, Locs, Spawns, Inspector), right-mid (Terrain, Palette,
  Catalog palette), bottom (Console, History). A group is one WINDOW frame
  whose only row is a TABSTRIP ("Catalog (F8)"); the shown member's window
  lays out under it, the others hide. One dock place per group; frame drag,
  member resize, Close / Pop out on the shown tab, maximise, Hide/show
  panels, solo windows and View > Reset groups all handle groups. The layout
  file gains `[group <name>]` sections; older files still apply.
- Fix: edpanel_hotkeys.c `k_groups` now follows the action enum's order
  (Palette after Camera); before, any registry boot with the Hotkeys panel
  asserted (OPT=0) or read past the array and crashed.

Review fixes (same date):

- A Spawns selection behind a tab of its group is open, as the menubar
  already said: `Editor_Spawns_SelectionCount` (and the wander rects) count
  it while Spawns is in its strip, so Del, Ctrl+D, the S context and the
  hint strip keep acting on it with Catalog in front (test-editor-groups
  `test_spawns_behind_tab`).
- The dispatcher derives its contexts with `Editor_HotkeyContextsFrom`
  (app_editor_hotkeys.c `app_hk_snapshot`) and leaves the mask on the
  EditorUi (`hotkey_contexts`); the status bar's hint reads it, so W-gated
  rows (the Rect tool's Ctrl+V) show over void, sky and a pinned level
  before its hover resolves, exactly when the key acts.
- `Editor_HotkeysJoin` compacts a pair under its shared modifiers only when
  both hold the same ones: Ctrl+Shift+[ beside Ctrl+] is "Ctrl+Shift+[
  Ctrl+]", not "Ctrl+Shift+[ ]" (which named the unbound Ctrl+Shift+]).
- No silent cut in the chrome: a BUTTON whose caption Build elided says it
  whole on hover (`caption_cut`; the tooltip leads with it, "Redo: Redo the
  step undone last", or is the caption when there is no tip;
  `ToriRSChrome_TooltipText`); Copy and Fill have tips. A keycap or rail
  badge whose chord is wider than its box shows the key it ends on
  ("...+1"), never a fragment such as "Ctrl+S". The DOM's chord-less
  button ellipses its caption like the canvas (modern and IE8 CSS
  `text-overflow`), the badge ellipses, and the IE8 page sizes the keycap's
  room from the key (`fitLegacyKeycap`) instead of a fixed 40px.
- Live's RUN and Triggers' Check and Run wear their chords as keycaps; the
  Triggers tips lost their "[chord]" text (Check's was 64 bytes and cut).
  Two Live tips over 63 bytes (RUN, Play) are shorter, so a boot cuts none.
- `ui_group_remove` and `Editor_UiPanelInStrip` assert the group and the
  slot before reading them.

Tests (2026-11-04, this container, objects in build_wb60*; after the
review fixes): test-chrome-widgets 340 checks, 0 failed, dom ok (and the
legacy page's keycap padding); test-editor-hotkeys 1696, 0 failed;
test-editor-groups 2068, 0 failed; **test-editor-chords** (new,
content-free: every wired button wears and draws its keycap, its drawn
caption is whole or said whole by its tooltip, the rail badges and an
over-wide one fitted, the slider labels, a rebind / unbind / reset
relabelling them, a 300-byte label logged once, a full ring) 501, 0
failed; the last three also at OPT=0 with asserts. Each new check was seen
to fail on the code before its fix, in a scratch worktree. test-editor-dropdown 36, test-editor-cmd 62, test-editor-areas
136, test-editor-panels-palette 50 and -accounts 48 (with `ulimit -s
unlimited`), all 0 failed; the web channel's node tests pass but
web/test/chrome_layout_test.js, which opens `src/Makefile` and so fails on
a case-sensitive file system (the file is `src/makefile`; not from this
change);
check-tree-walks and check-pt-switch PASS. test-editor-panels compiles and
links but needs the OSRS-Content squares; its content-free tests (layout,
codec, saved layout, maximise, world panel, popout, shared tools, open relay,
hotkeys, status bar) ran from a scratch copy against an empty content dir:
1263 checks, 0 failed.

Open:

- The panel-button keycap check lives in test-editor-chords (content-free),
  not in editor_panels_test, which this container cannot run.
- Spawns' "Move: click a tile" and "Place at latched tile" wear no keycap:
  no table action runs them (place.here is the loc Place tool's).
- The status bar shows no lane name or change count (FACT_HELLO carries no
  lane name; the Publish panel keeps its lane state private), no lane / RUN
  chips, no hovered loc or "fitted"; nothing calls `Editor_UiStatus` yet.
- Group tabs say their chord as text in the title ("Catalog (F8)"), not as
  the design's keycap: the TABSTRIP has no keycap drawing. A compressed tab
  (four right-top tabs fill 303 of the 322 px strip at 1530x1006, so a
  longer rebound chord compresses them) clips its title at the tab's edge
  without an ellipsis.
- At 1x a keycap leaves little of the 104 px plate: Redo beside Ctrl+Shift+Z
  draws "Re...", Mark area from rect "M...". Hovering says the caption
  whole; widening the plate or moving the key out of it is the design's
  call. The DOM button's tooltip (`title`) does not lead with a caption its
  CSS ellipsed: the runtime cannot tell that it did.
- Groups: dragging a tab out of a strip or a title bar onto one is not wired
  (`Editor_UiLeaveGroup` / `JoinGroup` exist; a LeaveGroup is not saved);
  Shift+Pop out of a whole group; the red attention count on tabs. Group
  titles are "A / B / C" (the baked font has no middle dot).
- The Small face has no ⌃ ⇧ ⌥ glyphs yet, so chords use the word spelling;
  a chord cut at 23 bytes can split a UTF-8 symbol, as labels can.
- Panel titles and dropdown / structured option labels still truncate
  without a record: they are not widget fields, so the ring does not see them.
- The DOM button keeps its red art, so its keycap sits a few pixels from the
  canvas one; the IE8 keycap and badge CSS (and `fitLegacyKeycap`'s
  measured padding, estimated when the key is not laid out yet) is unchecked
  in MSHTML.
- `make -C src torirsmaped` fails to link here (pow / floor: its recipe links
  no `-lm`); the same objects link with `-lm` added. Not from this change.

## Workbench step 6.0b, part 2: skin and font tiers (date 2026-11-05)

The skin and the fonts baked at 1x, 2x and 3x, reproducibly (design section 2
"R1 in full", section 16 step 6.0b). The whole of it is in
docs/CHROME_SCALE.md; which tier the native draw path picks at which scale is
the scale-core slice's.

- **One class table** (src/engine/torirs_chrome_skin_class.h, read by the C and
  the tools): 48 GLYPH slots (tool icons -- sixteen of them 15x15 world-map
  discs drawn by `spritebake --disc` on the crosshair's plate, 2026-11-07 --,
  marks and boxes, scroll and tree
  arrows, Close / Popout / Dock and their Over states, Run / Stop / Save /
  Build, Warning, Error, SplitterGrip, SliderKnob, SearchGlass) and 21
  TEXTURED (bodies, frame, button plates, scroll track and grips, slider
  tracks, PluginIcon), with the axes each tiles along.
- **tools/chrome_skin_tiers.py** derives src/engine/torirs_chrome_skin_tiers.{c,h}
  and res/plugin_chrome/skin@2x, skin@3x from the 1x bake, no cache: GLYPH
  through a mask route (alpha mask, two inks by k-means, scale2x / scale3x;
  at most three colours), TEXTURED through xBR (integer, alpha-aware,
  wrapping tiled axes). Byte-identical on a rerun; the recipe is in the
  generated header. `ToriRSChromeSkin_SpriteTier(slot, tier)` and
  `ToriRSChromeSkin_ClassOf(slot)` in ui/torirs_chrome_skin.h; tier 1 is the
  bake itself, a slot the tiers do not cover falls back to 1x.
- **Font tier hook**: `ToriRSChromeFont_<Face><N>x_TUNED` in the metrics header
  (0 everywhere; fontbake writes it from a new `--tuned` recipe argument) and
  `ToriRSChromeFont_Tier(face, tier)` (engine/torirs_chrome_font_tier.h), which
  uitree_scene_bridge.c now picks its fonts through. **tools/chrome_font_tiers.py**
  checks every 2x and 3x font tier is the 1x masks multiplied and writes the
  hand-tuning queue (glyphs whose scale2x / scale3x form differs, 1-pixel
  counters first: p12's a, e, s are there) into docs/CHROME_SCALE.md. No glyph
  is hand-drawn.
- **make -C src chrome-skin** now runs both tools with --check and
  test-chrome-skin-tiers, needs no cache, and fails on a stale tier, PNG or
  queue. The cache re-bake it used to be is **chrome-skin-rebake**, which then
  rewrites the tiers.
- **Web**: the theme's new `skinTiers` (web/torirs_chrome.js says 3; `make
  web` stages both tier directories) lets the runtime draw a packaged slot
  from the smallest tier covering its CSS size times devicePixelRatio, sized
  back to the authored size; modern.css carries `image-set()` rules under
  `.tpc-skin-tiers`. A host that says nothing gets 1x, as before. The
  runtime's SKIN_FILES gained SliderTrackLeft / Right.

Tests (2026-11-05, this container, build_opt): test-chrome-skin-tiers 922
checks passed; chrome-skin passes on a clean tree; test-chrome-widgets 340
checks, 0 failed, dom ok (with the new tier-selection block); runtime,
legacy and compat node tests ok; test-uitree, test-debug-overlay-visual,
test-chrome-surface-sdl, test-chrome-browser-exec, test-chrome-web-skin,
test-sdl-chrome-geometry, test-sdl-gesture, test-sdl-pointer-map pass;
`make -C src all` builds the client (torirsmaped fails to link for a
missing -lm, which this change does not touch). Each new check was seen to
fail on a mutated scratch copy (16 mutations: stale 1x pixel, edited PNG,
reclassified slot, a fourth colour, a wrong size, a class hole, a frayed
texture, a wrong font tier, a non-derived font mask, a stale queue, a tuned
1x face, a DPR-blind runtime, a wrong size table, tiers without a host).

Open:

- The native draw path does not read the tiers yet (scale-core slice); 1x,
  2x and 3x draw exactly as before.
- GLYPH slots with more than two inks flatten in the mask route (the cache's
  colour discs -- ToolSelect's yellow crosshair goes into the plate at 2x --
  SliderKnob, SaveButton): the skin's hand-drawing queue, or candidates to
  reclassify. The drawn tool discs (2026-11-07) are made for the route: their
  ring-ink strokes carry the shape and the one accent colour is allowed to go.
- Only the web host stages and announces the tiers; the macOS WKWebView,
  WebView2 and MSHTML hosts stage skin/ alone and stay at 1x. The runtime
  picks the tier when it paints, not again when devicePixelRatio changes.
- web/test/chrome_layout_test.js still opens `src/Makefile` (case).

## Workbench step 6.0b, part 3: scale selection and the two window features (date 2026-11-05)

Design section 2 "R1 in full" (the Controls row), section 3 "Every panel pops
out" (the Dock button, lighter panel windows), section 16 step 6.0b. Built on
v3-workbench `64310269`; the chrome model's rational scale
(`ToriRSChrome_SetScaleQ`) is another slice's, so this one codes against it
behind a shim and builds alone.

What landed:

- **Scale from the display.** `main_dynamic_chrome_scale` (main.c) returns
  the display's device pixel ratio in quarter units, snapped to the design's
  steps 4, 5, 6, 7, 8, 10, 12 (1x .. 3x; a tie goes to the larger step) and
  never under 8 on a touch device (`touch_ui` or `touch_camera`). The ratio is
  the new `PlatformWindow_ChromeDensityPercent`: the drawable-to-window ratio
  (devicePixelRatio on the web), or, where the backend has one coordinate
  space, Windows' per-monitor DPI under SDL (`SDL_GetDisplayDPI` on the
  "windows" driver only: X11's DPI comes from the monitor's millimetres and
  is not trusted). `PlatformWindow_DisplayDensityPercent`, which the game's
  own scaling reads, is unchanged. The ladder no longer steps up by 500
  points of window height: a tall window on a 1x display used to get 2x
  chrome under `chrome_scale=dynamic`, and now gets 1x unless View > Chrome
  scale says otherwise. A manifest that never sets `chrome_scale` keeps the
  integer display density exactly as before.
- **Pins take decimals.** `TORIRS_CHROME_SCALE` and `[ui:boot] chrome_scale=`
  read "2" or "1.5" (`ToriRSChromeScaleQ_Parse`, src/ui/torirs_chrome_scale_q.h,
  header-only, shared by main.c, the boot manifest and the tests); the
  manifest field and `AppConfig.chrome_scale` are now quarter units (-1
  dynamic, 0 unset). Precedence: env, View > Chrome scale (when not Auto),
  manifest, display.
- **The shim** (editor_ui.c): `Editor_ChromeSetScaleQ`, `Editor_ChromeScaleQ`,
  `Editor_ChromeScaleInteger`, `Editor_UiPx`. Under `TORIRS_CHROME_SCALEQ`
  (the integrator defines it when the rational model is linked) they call
  `ToriRSChrome_SetScaleQ` / `ToriRSChrome_ScaleQ` / `ToriRSChrome_Px`, and
  the integer handed to `App_SetChromeScale` is the tier at or above the
  scale (so the two setters never undo each other); without it a quarter value
  rounds to `(q + 2) / 4`. `edp_px` goes through `Editor_UiPx`;
  `ui_follow_scale` and the layout file's capture / apply / restore convert
  in quarters (exactly the old arithmetic at the integer scales).
- **View > Chrome scale** (Auto, 100 %, 125 %, 150 %, 175 %, 200 %, 250 %,
  300 %) and **View > Text size** (Normal, Large): radio rows between the
  panel rows and Hide / show panels (Reset layout and Snap to grid stay the
  last two), the chosen one "on" in its right-hand column, Auto naming what
  the display gives ("Chrome scale: Auto (150 %)"). Each is a hotkey action,
  unbound (`view.scale_auto`, `view.scale_100` .. `view.scale_300`,
  `view.text_normal`, `view.text_large`; docs/MAPED_HOTKEYS.md). Large is the
  chrome theme's body-face setting (`font_row` = Menu) plus the smallest
  additive theme change, **`ToriRSChromeTheme.row_h`** (0 = the grid's 18;
  Large 22), read by `DBG_ROW_H`; the editor re-asserts it every frame, so a
  theme installed later wears it. Both re-run the layout.
- **editor_prefs.ini** (src/editor/editor_prefs.{c,h},
  task_editor_prefs_io.{c,h}; `TORIRS_EDITOR_PREFS`, empty = off): `[view]
  chrome_scale = auto|100..300`, `text_size = normal|large`, read at boot and
  written on each change (`EditorUi.prefs_serial`), through the IO queue like
  the layout file. Device-local, never on a Client state key; a popped-out
  window reads the same file at its boot, which is how it inherits the
  leader's values.
- **Dock button.** `ToriRSChrome_PanelSetDockable` / `ToriRSChrome_TakeDock`
  / `ToriRSChrome_PanelDockRect` (left of Pop out, else of Close, else at the
  right end; the baked DockButton / DockButtonOver slots, a flat box with a
  foot bar without skin). The solo layout wears it (where the main window's
  Close and Pop out sit); pressing it is `Editor_UiDockBack`: it publishes
  the new **`TORIRSMAPED_STATE_DOCK_PANEL = 9`** (registry index, serial;
  documented in torirs_maped.h's key registry), sets `dock_exit`, and the
  window ends (`EditorView_RequestExit`, main.c's frame loop; `window.close()`
  on the web). The window that shows the panel set (not solo, editor open)
  shows that panel exactly as View > panel does, leaving an open one open;
  its pop-in bookkeeping and console line follow as for any pop-in. No
  session: a console line, the window stays.
- **Panel-only windows load no world.** `--editor-panel <module>` for a
  module other than world / preview (`Editor_UiModuleNeedsWorld`) sets
  `EditorView_AppHoldsNoWorld`: the boot's default load (app_boot.c, one
  added condition), the first-sight load (world_load_attempted) and the
  camera's load (view_apply_or_load) stand down; the console and the editor
  console say what was skipped. World and Preview windows are unchanged.

Tests (2026-11-05, this container, objects in src/build_opt of the private
worktree):

- **test-editor-workbench** (new, content-free; maped/workbench.mk) 270
  checks, 0 failed: the draw-list golden (every primitive of a widget scene in
  both themes and of the whole registry at 1530x1006, at 1x, 2x and 3x,
  digested and compared with the values recorded at 64310269 before any draw
  path change; committed first, alone), the snap table, the touch floor, the
  decimal pins, the shim both ways, the View rows (order, "on", relayout at
  2x, Large's 22 px rows and back to 18, a fresh boot still the golden), the
  prefs codec round trip for every combination plus a hand-edited file, and
  Dock across two sessions of one embedded ToriRSMapEd (the popped window's
  button where it belongs, the press, the relay, the leader showing Catalog,
  the serial, an open panel left open, the session-less refusal).
- test-editor-hotkeys 1746 (the doc rows of the ten new actions included),
  test-editor-groups 2068, test-editor-chords 501, test-chrome-widgets 340 and
  the DOM test, test-editor-dropdown 36, test-editor-cmd 62,
  test-editor-areas 136, test-editor-panels-palette 50, -accounts 48 (with
  `ulimit -s unlimited`), test-editor-views-web 9 cases: all 0 failed.
  bootmanifest_test PASS with a new decimal fixture (linked against
  build_opt's rscache objects: 3rd/rscache/build is not built here).
  check-tree-walks and check-pt-switch PASS. The content-free subset of
  editor_panels_test (layout, world panel, popout, shared tools, open relay,
  layout codec, saved layout, maximise, hotkeys, status bar) from a scratch
  copy against an empty content dir: 1273 checks, 0 failed. The client
  (`torirs`) links; `torirsmaped` does not link here (undefined pow / floor in
  torirs_maped_accounts.c under LTO, no -lm on its line; not from this
  change).
- Each new check was seen to fail on a mutant before being kept green, the
  mutants applied temporarily in this private worktree (no other build reads
  it) and restored from git: the golden (row height 18 -> 19 fails every
  digest), Large rows (DBG_ROW_H ignoring row_h), the solo Dock, the leader's
  show (a toggle closes an open panel), the snap tie, the prefs comment cut,
  the "on" mark, a missing doc row, and the boot manifest's decimal parse.
- Not run: test-editor-panels and test-editor-views in full (they read the
  OSRS-Content squares, not checked out here); no photograph (no cache).

Open:

- The rational scale itself (glyph and skin tiers, hinted outlines) is the
  chrome slice's; until `TORIRS_CHROME_SCALEQ` is defined, 125 % draws at 1x
  and 150 % / 175 % at 2x. The bridge fonts (`App_SetChromeScale`'s
  `UITreeSceneBridge_SetChromeScale`) still follow the integer tier.
- The layout file's `scale` key stays an integer (the tier), so a layout saved
  at 150 % restores as "same scale" at 200 % on the integer chrome.
- Docking does not carry the popped window's scroll position back: the
  leader's panel was hidden, not torn down, so it comes back where it was in
  the leader, not where the popped window was scrolled.
- Text size is the editor's chrome only; the plugin window (the DOM executor)
  has no theme command, and neither Dock nor Text size crosses the executor
  seam -- the editor's title bars and rows are drawn by the in-canvas chrome on
  every lane, the web included.
- A touchscreen laptop under SDL is not counted as touch (no platform call
  for it yet): only `touch_ui` / `touch_camera` raise the 2x floor.
## Workbench step 6.1, the consumers (date 2026-11-05)

The second half of build-order step 6.1 ("Gesture frames"; design section
14, "Mouse, trackpad, touch"): everything that CONSUMES the bus's
PAN / PINCH / TWIST. The producers (SDL2, Win32, the web; TORIRS_SIM_GESTURE)
are the other slice. Until the two are joined, src/input/torirs_gesture.h is
the contract (struct ToriRS_GestureFrame: kind, x, y, dx, dy, fingers,
scale_q16, radians_q16, phase; `ToriRS_InputGestures`,
`ToriRS_InputLastWasTouch`) and src/input/torirs_gesture_stub.c answers "no
frames, never a touch", so the client behaves as before; the integrator
deletes the stub and points the two functions at the decoded list.

What landed:

- **Chrome routing** (src/ui/uitree_debug_overlay_gesture.u.c, a third
  unity fragment; `ToriRSChrome_Gesture`, `struct ToriRSChromeGesture`):
  over a GRAPH a two-finger PAN is GRAPH_SCROLL (the content follows the
  fingers) and a PINCH goes through `ToriRSChrome_Pinch` about its point;
  over a MODELVIEW a PAN is the new `TORIRS_CHROME_EVENT_VIEW_ORBIT` (dx yaw,
  dy pitch, 1/256 logical px) and a PINCH `TORIRS_CHROME_EVENT_VIEW_ZOOM`, a
  TWIST (a roll) nothing; over a VLIST / CODE two fingers scroll by PIXELS
  (whole rows taken, the part row kept in `ToriRSChromeWidget::pan_rem`), a
  TEXTAREA scrolls its lines the same way when they overflow, anything else
  on a panel (TREE rows, a SLIDER) scrolls the panel; a PINCH or TWIST
  anywhere else, and a one-finger PAN, is taken and ignored. Returns whether
  the chrome took it; over no panel it is the World's. No change to the
  draw path.
- **Executor and DOM** (one look): `TORIRS_CHROME_INTENT_GESTURE` (17, the
  new KIND_MAX; `ToriRSChrome_GestureWidget` applies it by the same rules,
  numbers in `text`: "dx dy fingers scale_q16 radians_q16 phase"). The DOM
  runtime posts it from a GRAPH well's and a MODELVIEW's touch pairs (pan,
  pinch, twist), a MODELVIEW's one-finger drag (orbit), ctrl+wheel (a 1.25 a
  notch pinch, as the canvas) and Safari's gesturechange; a tap is left to
  the browser's click. runtime.js / runtime-ie8.js regenerated with tsc
  6.0.2; web/torirs_chrome.js fences on GESTURE; modern.css gives the model
  well `touch-action: none` as the graph already had.
- **World** (src/editor/editor_world_gesture.{h,c}, pure; glue
  `app_map_editor_gestures` in app_map_editor.c, called from app_frame.c
  before the editor's tick): the frames are offered to the chrome first
  (`Editor_UiGesture`), the rest to the free camera. Two-finger PAN moves
  the eye by the ground-plane difference under the fingers (the ground point
  stays under them; yaw, pitch and height kept); PINCH moves the eye along
  the ray of the ground point under the pinch by the ratio (clamped 64 ..
  12800 fine units from it); TWIST turns yaw about the ground under the
  viewport's centre, the scene turning with the fingers. All through
  `ToriRS_WorldUnprojectPlane`, the projection's own inverse; the ground is
  the pinned level's height sampled where the ray meets it. Free camera and
  perspective only (the follow cam's anchor is the player's).
- **One finger is the tool's**: while the editor panel is open main.c hands
  the touch layer no world viewport (`App_EditorOwnsTouch`), so a one-finger
  drag is a LEFT drag (the marquee, a brush stroke), never the middle-button
  camera synthesis. With the editor closed the game's touch input is
  untouched.
- **Picker and Preview**: the picker already zoomed on GRAPH_PINCH and
  panned on GRAPH_SCROLL; the frame path now reaches both. The Preview takes
  VIEW_ORBIT (the middle drag's gearing: 4 yaw, 2 pitch a logical px, a q8
  remainder so a slow finger still turns it) and VIEW_ZOOM through the new
  `preview_orbit` / `preview_zoom` services (`EditorPreviewCamera_ZoomBy`,
  continuous). Its tooltip says "or drag to orbit, pinch to zoom".
- **Gesture spelling** (section 15's column): `Editor_HotkeyActionInfo::gesture`,
  "2 fingers" on the four fly moves, "pinch" on fly down / up, "twist" on turn
  left / right. `Editor_HotkeysHint` takes `touch`; with it the tool's
  pointer rows say the finger's word ("[tap] tile", "[drag] marquee";
  Shift+click and the keyboard rows drop out), then "[2 fingers] pan",
  "[pinch] zoom", "[twist] turn", "[hold] menu". The status bar passes the
  touch flag (`EditorUi::input_touch`, `Editor_UiSetTouchInput`, set each
  frame from `ToriRS_InputLastWasTouch`). docs/MAPED_HOTKEYS.md says so under
  Camera.

Tests (2026-11-05, this container, objects in build_w61*):
test-chrome-widgets 370 checks (340 before), 0 failed, dom ok (gesture
routing over a GRAPH, a MODELVIEW and a VLIST, over nothing not taken, the
part row, a modal, the GESTURE intent and a malformed one, 2x; the DOM's
touch pair, ctrl+wheel, a tap left alone, the model's one-finger orbit);
**test-editor-world-gesture** (new, pure) 38, 0 failed (pan keeps the ground
under the fingers within a px, pinch keeps its point and halves the
distance, the clamps, twist keeps the look-at centred and turns clockwise,
one finger is not the camera, a recorded pinch replays bit for bit);
test-editor-hotkeys 1734 (1696), 0 failed; test-editor-groups 2080 (2068), 0
failed (the touch strip, Editor_UiGesture over a panel and over the World);
test-editor-chords 501, test-editor-dropdown 36, test-editor-cmd 62,
test-uitree, test-chrome-browser-exec, test-chrome-web-skin,
test-chrome-surface-sdl all pass; check-tree-walks and check-pt-switch PASS;
the web channel's node tests pass but chrome_layout_test.js (src/Makefile
case, as before). test-editor-panels needs the OSRS-Content squares; its
content-free tests plus test_picker and test_preview ran from a scratch copy
of its main against an empty content dir: 1311 checks, 3 failed, the three
storage checks of test_picker that list maps/ and the lanes (content); the
Preview's new orbit, slow-orbit and pinch checks pass. The picker's new
frame checks live in test_chrome (test_picker_gestures), which needs the
squares: run against zero-byte square files they pass (a PINCH frame zooms
by its ratio about its centre within a pixel, a PAN frame pans with the
fingers), beside 11 content failures and a crash further on in
test_chrome's content-only part.

Each new check was seen to fail on a mutant: in a scratch worktree for the
chrome routing (Gesture returning 0: 17 failures; no part row: 5; the
graph pan's x unflipped: 1; the GESTURE intent dropped: 3; the model view's
pinch dropped: 1; the DOM's graph gestures unbound: the dom test asserts),
the World helper (pan the wrong way, the pinch ratio inverted, the twist
without the eye's turn, one finger let through: 1, 1, 1, 2), the hotkeys
(touch ignored: 15), the status bar (touch not passed: 5) and Editor_UiGesture
(returning 0: 1); in this private worktree, temporarily, for the Preview's
event hook (3) and the picker's pan sign and pinch (2), each restored with
git checkout and rebuilt before the clean runs above. test-touch fails 7 of 45 here exactly as on
64310269 (not this change). `make all` links the client; torirsmaped's link
fails on Linux for want of -lm, as on 64310269.

Open:

- The bus half (the other slice): until it lands no frame reaches any of
  this in the running client, and `ToriRS_InputLastWasTouch` is 0, so the
  touch strip only shows under a test. The two-finger arrow-key synthesis
  in torirs_touch.c still runs with the editor open; once PAN frames flow,
  the integrator should stop it there too, or the World pans and turns at
  once.
- Three-finger drag (pitch) and the HUD chip "one finger pans" are not
  built; the tilt actions have no gesture spelling.
- Picker zoom and Preview orbit have no table action, so their gesture
  words are in the Preview's tooltip only, not the table; the Hotkeys panel
  shows no gesture column yet.
- The follow camera (online, out of a cutscene) and a parallel projection
  take no World gestures.
- The Preview's orbit direction follows its keys (right is the right
  arrow's yaw, down the down arrow's pitch); not checked by eye.

## Workbench steps 6.0b and 6.1 (2026-11-05)

Five slices merged into v3-workbench (each `--no-ff`, in this order): the
rational chrome scale (scale-core), the skin and font tiers (bake-tiers),
scale selection with the View rows, Dock and panel-only windows
(scale-controls), the gesture frames and their producers (gesture-frames),
and the gesture consumers. The parts above ("6.0b, part 2", "6.0b, part 3",
"6.1, the consumers") are the slices' own notes; this section is what the
merged tree is. The design is in [docs/CHROME_SCALE.md](CHROME_SCALE.md) (the
scale, the tiers, the bake) and [docs/GESTURES.md](GESTURES.md) (the frames,
the producers per platform, how the editor consumes them).

What landed:

- **Rational chrome scale.** The chrome scale is a quarter unit `scale_q`
  (4..12): `ToriRSChrome_SetScaleQ` / `ToriRSChrome_ScaleQ`; `SetScale(s)` is
  `SetScaleQ(4 * s)` and `ToriRSChrome_Scale` / `ScaleTier` the tier at or
  above it. One rounding function, `ToriRSChrome_ScalePx(q, px)` (inverse
  `ToriRSChrome_UnscalePx`), behind `DBG_PX`, the menu layout, pointer
  mapping and `ToriRSChrome_Px(ui, px)`; quarter measures
  `ToriRSChrome_MeasureTextQ`, `FontLineHeightQ`, `FontLineBoxQ`,
  `WrapTextQ`, `GlyphAdvanceQ`. New executor command
  `TORIRS_CHROME_CMD_SCALE = 29` (journaled `TORIRS_CHROME_CHANGE_SCALE`;
  none at 1x); the DOM runtime zooms the shell by CSS `zoom`.
- **Bake** (src/ui/torirs_chrome_bake.{h,c}): `ToriRSChromeGlyphs_Rasterise`
  (hinted rows, whole-pixel stems, quarter coverage on one-row edges),
  `ToriRSChromeBake_Prepare` / `ToriRSChromeBake_SetScaleQ`,
  `ToriRSChromeBake_Face`, `ToriRSChromeBake_Skin`, `ToriRSChromeBake_TierFor`,
  `ToriRSChromeBake_Present`.
- **Skin and font tiers.** `ToriRSChromeSkin_SpriteTier(slot, tier)` and
  `ToriRSChromeSkin_ClassOf(slot)` over src/engine/torirs_chrome_skin_tiers.c
  (69 slots, 48 GLYPH / 21 TEXTURED, tools/chrome_skin_tiers.py),
  `ToriRSChromeFont_Tier(face, tier)`, tools/chrome_font_tiers.py, the web's
  `skinTiers` and res/plugin_chrome/skin@2x, skin@3x. `make -C src
  chrome-skin` checks it all without a cache; `chrome-skin-rebake` re-bakes.
- **Scale selection.** `main_dynamic_chrome_scale` snaps the display's
  density (`PlatformWindow_ChromeDensityPercent`) to 4, 5, 6, 7, 8, 10, 12
  quarters, touch never below 8; `TORIRS_CHROME_SCALE` and `[ui:boot]
  chrome_scale=` take decimals (ui/torirs_chrome_scale_q.h). View > Chrome
  scale (Auto, 100..300 %) and View > Text size (Normal, Large) persist in
  `editor_prefs.ini`. The Dock button (`ToriRSChrome_PanelSetDockable`,
  `ToriRSChrome_TakeDock`, `TORIRSMAPED_STATE_DOCK_PANEL = 9`) and
  panel-only `--editor-panel` windows that skip the world load.
- **Gesture frames.** `TORIRS_CMD_GESTURE_PAN / _PINCH / _TWIST` (112..114,
  `CmdBus_PushGesture*`), decoded into `LibToriRS_Input::gestures`
  (`struct LibToriRS_Gesture`, 32 a frame); `ToriRS_GestureStream*` phases
  (src/input/torirs_gesture.{h,c}); SDL2 trackpad wheel, Ctrl+wheel,
  SDL_MULTIGESTURE, AppKit magnify and the web page's listener as producers;
  `TORIRS_SIM_GESTURE`; `ToriRS_TouchSetGestureFrames`.
- **Consumers.** `ToriRSChrome_Gesture` / `ToriRSChrome_GestureWidget`
  (GRAPH scroll and pinch, MODELVIEW `VIEW_ORBIT` / `VIEW_ZOOM`, lists and
  panels by pixels; `TORIRS_CHROME_INTENT_GESTURE = 17` on the web),
  `EditorWorldGesture_Apply` (World pan, pinch, twist), the picker and
  Preview, and the hint strip's touch words.

The joins made here:

- **The scale shim is the real call.** `Editor_ChromeSetScaleQ`,
  `Editor_ChromeScaleQ` and `Editor_UiPx` now always call
  `ToriRSChrome_SetScaleQ`, `ToriRSChrome_ScaleQ` and `ToriRSChrome_Px`; the
  `TORIRS_CHROME_SCALEQ` switch and the integer-rounding fallback are gone,
  and `Editor_ChromeScaleInteger` is the tier at or above the scale (so
  `App_SetChromeScale` never resets the quarter scale). Static asserts tie
  `TORIRS_CHROME_SCALEQ_*` (host steps) to `TORIRS_CHROME_SCALE_Q_*` (model).
- **The tiers feed the bake's skin cache.** scale-core's 1x stand-in
  `ToriRSChromeSkin_SpriteTier` (and its `TORIRS_CHROME_SKIN_TIERS` guard) is
  removed; the bake reads the real tiers, so at q = 5..7 it area-averages
  down from 2x, at q = 10 from 3x, and at q = 8 and 12 it uses the tier
  itself.
- **One gesture struct, one reader.** src/input/torirs_gesture_stub.c and the
  consumers' `struct ToriRS_GestureFrame` are deleted. Every consumer reads
  `struct LibToriRS_Gesture` through `ToriRS_InputGestures(input, &frames)`
  (implemented in torirs_input.c over `input->gestures`);
  `ToriRS_InputLastWasTouch` reads the new `gesture_last_touch`, set by
  `LibToriRS_Input_PushGesture` from the last frame's TOUCH flag.
  `Editor_UiGesture` maps the input's kind (PAN = 0) and phase onto the
  chrome's own `struct ToriRSChromeGesture` (PAN = 1, the executor's
  numbering); `EditorWorldGesture_Apply` takes the float frame directly
  (`EditorWorldGesture_TwistYaw` now takes radians).
- **Touch frames on in the editor.** New
  `PlatformWindow_SetTouchGestureFrames(platform, on)` (SDL2, Win32 GDI,
  Android); main.c sets it each frame before the poll from
  `App_EditorOwnsTouch`, so with the editor's panel up two fingers are PAN /
  PINCH / TWIST frames and not the game's wheel notches and arrow keys.
- **ALSO_LEGACY frames stay on the legacy route.** `app_map_editor_gestures`
  skips frames flagged `LIBTORIRS_GESTURE_FLAG_ALSO_LEGACY`: the chrome's
  `ToriRSChrome_WheelFrame` and the camera's wheel already took that motion
  this frame, so the trackpad and ctrl+wheel behave as in 6.0 and only the
  touchscreen's frames (and a touchpad's twist) reach the new consumers.
- **The chrome's gesture code is on the rational scale**: a MODELVIEW
  orbit divides by `scale_q / 4`, and a DOM list pan converts through
  `ToriRSChrome_ScalePx`.
- `runtime.js` / `runtime-ie8.js` regenerated from the merged
  runtime-source.js (`tsc --ignoreDeprecations 6.0 --alwaysStrict false
  --allowJs --target ES5 --module none`; that command reproduces the
  pre-merge runtime.js byte for byte, where tsc 6's default adds a
  "use strict" line).
- `torirsmaped` links `-lm` (pow / floor in the accounts code); it now links
  on Linux.

Tests (2026-11-05, this container, build_opt, after the merge):
test-chrome-widgets 915 checks, 0 failed, DOM test ok;
test-debug-overlay-visual passed (the 1x / 2x / 3x chrome goldens in
src/ui/test/golden/chrome_scale_{1,2,3}x.prims.txt still match);
test-chrome-skin-tiers 922 passed; `chrome-skin` passes;
test-editor-workbench 269, 0 failed (its draw-list goldens at 1x, 2x, 3x
match; one fewer check than the slice, whose integer-fallback branch is
gone); test-editor-world-gesture 38, 0 failed; test-gesture 177, 0 failures;
test-touch 47, 0 failures; test-cmdbus 9 passed; test-keymap 409, 0
failures; test-sdl-gesture 38, 0 failures; test-sdl-pointer-map 698, 0
failures at both densities; test-sdl-chrome-geometry, test-sdl-text-input,
test-uitree-input-signature ok; test-editor-hotkeys 1784, test-editor-groups
2080, test-editor-chords 501, test-editor-cmd 62, test-editor-dropdown 36,
test-editor-areas 136, test-editor-panels-palette 50,
test-editor-panels-accounts 48: all 0 failed; test-chrome-surface-sdl,
test-chrome-browser-exec, test-chrome-web-skin ok; test-uitree passed
(`ulimit -s unlimited`); check-pt-switch and check-tree-walks PASS;
bootmanifest_test PASS (linked against build_opt's rscache objects:
3rd/rscache/build is not built here); test-editor-views-web 9 cases ok;
plugin_chrome and web node tests ok except web/test/chrome_layout_test.js
(opens `src/Makefile`, case, as before). `make -C src all`, `torirsmaped`
and `torirsmapedctl` build.

Photographs (the Catalog-shaped panel through `ToriRSChromeBake_Present`,
written by test-chrome-widgets): src/build_opt/scale_q4.bmp, scale_q5.bmp,
scale_q6.bmp, scale_q7.bmp, scale_q8.bmp, scale_q12.bmp. At q = 6 (1.5x)
every text row is crisp: whole-pixel stems, even rows and baselines, no
smeared glyphs; some letters' stems are one pixel and some two, which is
1.5x of a 1-pixel stem rounded per glyph. q = 4 is byte-identical to the
scale-core slice's photograph; q = 5..12 differ from it because the skin is
now drawn from the 2x / 3x tiers rather than from 1x.

Open:

- **The check and cross marks lose their dark ring at 2x.** Through the
  tiers, the Catalog photograph's on / off marks (GLYPH slots, mask route:
  CheckOn / CheckOff or CheckBoxOn / CheckBoxOff by check style) come out as
  a flat disc with the mark, where 1x doubled showed an outlined ring (seen
  in scale_q8.bmp against the scale-core slice's). This joins the six multi-ink GLYPH
  slots already listed as flattening; reclassify them as TEXTURED or draw
  their tiers by hand. Until then the web host draws the 1x skin
  (`skinTiers: 1`, review fixes below).
- ~~Engine: `UITreeSceneBridge_EnsureDebugFont` / `EnsureChromeSkin` still
  load the integer tiers~~ **Closed (6.0b scale bridge, integrated with
  6.2-6.4 below):** the native chrome draws the whole scale.
  `UITreeSceneBridge_SetChromeScaleQ` registers each face's hinted atlas at
  q (`UITREE_SCENE_DEBUG_FONT_Q_ID`) and the skin at its own size, main.c
  passes q straight to `App_SetChromeScaleQ` (`ToriRSChromeScaleQ_Whole` is
  off the native path), and the Soft3D / GL3 blitters blend coverage. Since
  the vector-face merge that atlas is the synthesized vector face's ("Vector
  face merged (2026-11-05)" under 6.2-6.4).
- ~~At 1.5x some letters' stems are one pixel and some two, the bowls
  lumpy~~ **Closed (vector-face merge):** between the tiers the native path
  draws the vector face, every stem the same profile on every row.
- ~~View > "Text rendering: smooth always" reaches the web pages only~~
  **Closed (vector-face merge):** `App_SetChromeTextSmooth` hands it to the
  scene bridge, which draws the vector face at 1x, 2x and 3x too; the row
  lost its "(web pages only)".
- ~~The host does not yet call `ToriRSChromeBake_SetScaleQ`~~ **Closed:**
  `UITreeSceneBridge_SetChromeScaleQ` runs `ToriRSChromeBake_Prepare(q)`, the
  one place the client bakes.
- Editor and app code still compute some layout as
  `ToriRSChrome_Scale(ui) * N` (editor_ui.c, the edpanel_* files beyond
  `edp_px`, live_panel.c, torirs_plugin_panel.u.c, app_chrome.c,
  app_editor_view.c, `ToriRSChromePanelDraw_Transform`): at a fractional q
  that is the tier. **Editor half closed** at the 6.5-6.8 integration (see
  "Workbench steps 6.5 to 6.8"); torirs_plugin_panel.u.c and
  `App_ChromeScale` remain.
- The layout file's `scale` key stays an integer tier.
- Trackpad frames (ALSO_LEGACY) are not consumed (above); moving the
  trackpad onto them means dropping the legacy wheel / scroll / pinch for
  that frame before `ToriRSChrome_WheelFrame` and the camera's wheel see it.
- Under the runner split view (app_render.c) the pointer commands go to the
  game's input and `TORIRS_CMD_INPUT_TOUCH_BEGAN` to the editor's, so there
  the hint strip's touch words are not cleared by the mouse (review fixes
  below).
- The DOM zooms the whole shell by CSS `zoom`; the gesture intent's
  coordinates under a fractional zoom and the custom-well `scaleMilli` were
  not checked in a real browser.
- Not run here: test-editor-panels and test-editor-views in full (need the
  OSRS-Content squares); the web build (no emcc); the native client by eye.
- Everything the slices' own notes above list as open and not closed here
  (Win32 GDI trackpad gestures, macOS twist, Linux touchpad pinch, the font
  hand-tuning queue, the macOS / WebView2 / MSHTML hosts at 1x, three-finger
  pitch, the gesture column) is still open.

Review fixes (2026-11-05, after the integration; each finding checked
against the code first). (1, 8) The DOM drew at DPR squared: the plugin
chrome's q already holds the display's ratio, and the page's CSS pixel is
already devicePixelRatio device pixels. `ToriRSChrome_SetPageScale(ui, q,
density_percent)` now tells the sync the presentation's own density
(main.c passes `PlatformWindow_ChromeDensityPercent` each frame), and
`TORIRS_CHROME_CMD_SCALE` carries (q x 100) / (4 x density), reduced by
gcd(100, density). Auto is no zoom on any display (8 / 8 at DPR 2, so a 2x
chrome on a 2x page sends no SCALE at all), and View 200 % on a 1x page is
8 / 4. The sync holds the zoom as a ratio (`scale_value` / `scale_den`).
(2, 11) The native draw path cannot draw a fraction yet, so
`main_chrome_scale_apply` hands App_SetChromeScale and both chrome models
`ToriRSChromeScaleQ_Whole(q)`, which rounds halves up the way PixelDensity
does (125 % draws 1x, 150 % and 175 % 2x, 250 % 3x). Only the DOM gets the
asked q, through the page scale, and the View rows keep their fractional
choices. (3) `App_EditorOwnsGestureFrames` now gates
`PlatformWindow_SetTouchGestureFrames`. It is true when the editor owns the
touch AND the free camera owns the World (offline, a cutscene, or
`camera_unlocked`). Online with the follow camera, the touch layer's wheel
notches and arrow keys turn and zoom it, as on the base commit, and
`app_map_editor_world_gesture` treats `camera_unlocked` as the free camera
(app_camera.c's test). (4, 14) The runtime's skin tier covers
devicePixelRatio times the zoom (`deviceScale`), and `applyScale` repaints
every skinned node (`repaintSkins`, split out of `applyTheme`). (5)
`ToriRSChromeScaleQ_Parse` asserts its text. (6) `ToriRSChromeScaleQ_Round`
and its test lines are gone, replaced by `_Whole`, which has a caller. The
header now says the model takes q exactly and the tier is
`ToriRSChrome_ScaleTier`. (7) `ToriRSChrome_TakeDock` and `_TakePopOut`
assert before they read. (9) A touchscreen pinch over a GRAPH zoomed twice.
`PlatformSdl_MultiGestureRoute` drops the legacy `TORIRS_CMD_INPUT_PINCH`
for a DIRECT device while the touch policy makes frames
(`ToriRS_TouchGestureFrames`, a new getter). (10) The web host's theme says
`skinTiers: 1`, because the derived GLYPH tiers flatten the marks; `make web`
still stages them. (12) A TWIST never carries ALSO_LEGACY:
`gesture_stream_pair` clears it, since no legacy route carries a turn. A
touchpad's twist now reaches the editor, as the docs said. (13) A twist over
a MODELVIEW orbits its yaw (`TORIRS_CHROME_EVENT_VIEW_ORBIT` with `value` =
the radians q16; edpanel_preview turns it into yaw steps with the World's
sign), in the canvas and through the DOM's GESTURE intent. One departure
from the design's table is kept: a two-finger pan over the Preview orbits
rather than panning the well, because the Preview's camera is a turntable
with no pan. (15) The runtime sets `.tpc-dpr-fractional` on the root when
devicePixelRatio times the zoom is not whole, and modern.css drops
`pixelated` under it. (16) New `TORIRS_CMD_INPUT_TOUCH_BEGAN` (115, no
payload) is pushed by the touch policy on every finger landing.
`LibToriRS_Input::last_touch` (was `gesture_last_touch`) is set by it and by
TOUCH frames. It is cleared by an unflagged frame, or by a mouse move,
click, wheel or scroll that arrives with no finger down (`touch_down`,
cleared by MOUSE_LEAVE); keys leave it alone. A one-finger tap now shows the
touch words, and mouse use takes them away. (17)
`Editor_UiChromeScaleFor(ui, auto_q, touch)` holds a View choice under 2x
at 2x on a touch device (the choice itself is kept), the rows under 2x say
"(touch: 200 %)", and a manifest pin is floored the same way. The env pin
is a developer's override and is not floored, and a lane with no
`chrome_scale` key keeps its integer density. (18)
`TORIRSMAPED_STATE_DOCK_PANEL` carries the popped window's panel scroll
(logical px) and its first list's first row, and `ui_dock_heard` applies
them; a two-value dock from an older build moves nothing. (19)
docs/CHROME_SCALE.md says the GLYPH tiers are derived (scale2x / scale3x of
a two-ink quantisation, no outline, nothing hand-drawn) and lists the check
and radio marks among the slots that flatten. It also documents the page
zoom, the zoom-aware tier and `.tpc-dpr-fractional`. (20)
editor_world_gesture_test's `test_replay` records the frames off the
command bus and replays the file through `CmdReplay_PumpFrame` and
`ToriRS_Input_ApplyCmd` into a second camera, bit for bit. (21)
chrome_widget_sync_glyphs asserts that every slot has all three tiers and is
sampled from the tier at or above its scale (`ToriRSChrome_ScaleTier`), not
merely from TierFor over whichever tiers exist. Tests after the fixes (this
container, build_opt): `make -C src all`, torirsmaped and torirsmapedctl
build. test-chrome-widgets 924 checks, 0 failed, DOM test ok (new: the page
scale at DPR 1, 1.5 and 2, the zoom-aware tier, the fractional class, the
Preview twist). test-debug-overlay-visual passed (the 1x / 2x / 3x goldens
match). test-chrome-skin-tiers 922 passed; `chrome-skin` passes.
test-editor-workbench 284, 0 failed (its 1x / 2x / 3x draw-list goldens
match; new: `_Whole`, the touch floor, the Dock's scroll).
test-editor-world-gesture 42, 0 failed. test-gesture 191, 0 failures (new:
the twist's flags, the last pointer). test-touch 47, test-cmdbus 9,
test-keymap 409. test-sdl-gesture 42 (new: the multigesture routing per
device; SDL's dummy driver cannot stage fingers). test-sdl-pointer-map 698
at both densities. test-sdl-chrome-geometry, test-sdl-text-input and
test-uitree-input-signature ok. test-editor-hotkeys 1784, test-editor-groups
2080, test-editor-chords 501, test-editor-cmd 62, test-editor-dropdown 36,
test-editor-areas 136, test-editor-panels-palette 50 and
test-editor-panels-accounts 48, all 0 failed. test-chrome-surface-sdl,
test-chrome-browser-exec and test-chrome-web-skin ok. test-uitree passed.
check-pt-switch and check-tree-walks PASS. bootmanifest_test PASS (against
build_opt's rscache objects). test-editor-views-web 9 cases ok. The
plugin_chrome and web node tests are ok except web/test/chrome_layout_test.js
(`src/Makefile`, as before). test-uitree and the two editor-panels tests
need `ulimit -s unlimited`: test_panel holds an Editor (10 MB) and a
ToriRSChrome (5 MB) on its stack. Not checked by eye: the page zoom and the
fractional class in a real browser, the Preview twist's direction, and a
touchscreen pinch on hardware.

### Vector face (for the integrator)

Text between the bitmap tiers is a synthesized vector face now, not the 1x
cells scaled (docs/CHROME_SCALE.md, "The vector face"). The judges' winner,
depixelize, ships as `tools/chrome_font_vector.py`, with two of their defect
notes fixed in it before the bake: Menu's x (an hourglass: now two crossing
strokes with V notches) and Menu's / (bowed into an S: every staircase of two
or more steps is now one straight line, so /, \\, v, N, k and Body's / are
straight). It writes `src/engine/fonts/ToriRS{Small,Body,Menu}-Vector.ttf`, the
web faces `res/plugin_chrome/font/ToriRS*-Vector.{ttf,woff,eot}` and
`src/engine/torirs_chrome_font_vector_baked.{c,h}`; `make -C src
chrome-font-vector` fails on any byte of difference (also run by
`chrome-skin`). `ToriRSChromeGlyphs_Rasterise` now takes `(font_slot, q,
smooth, out)` and rasterises the vector face (stb_truetype, vendored in
`3rd/stb/stb_truetype.h`) at a fractional q, with the zones (descender,
baseline, x-height, cap height, ascender, line box) on the layout's rows,
horizontal 3x oversampling and a 1.45 stem-darkening gamma; the old cell path is
`ToriRSChromeGlyphs_RasteriseCells` and still gives 1x, 2x and 3x byte for
byte (the goldens are unchanged). New: `ToriRSChromeBake_PrepareSmooth`,
`ToriRSChromeBake_FaceSmooth`, `ToriRSChromeBake_SetTextSmooth`,
`ToriRSChromeBake_GlyphAtlasBuilds`; `ToriRSChrome_SetTextSmooth` /
`ToriRSChrome_TextSmooth`; View > "Text rendering: bitmap at whole scales /
smooth always" (`text_rendering` in editor_prefs.ini, `view.text_bitmap` /
`view.text_smooth`); SCALE's `h` and the snapshots' `scaleText`;
`.tpc-text-smooth`. Link `engine/torirs_chrome_font_vector_baked.c` (and
`-lm`) wherever `ui/torirs_chrome_bake.c` is linked: the client's SRCS and
test-chrome-widgets have it. ~~Still to do: the scene bridge draws the
integer tiers only~~ **Done by the merge** ("Vector face merged
(2026-11-05)" under 6.2-6.4): the native canvas draws the face at every
fractional q, and at 1x / 2x / 3x under "smooth always"; the View row's
"(web pages only)" is gone. Not
checked in a real browser or on XP: the Vector WOFF/EOT rendering, and that
IE8 accepts the EOT (built by the same `write_eot` as the bitmap faces).

## Workbench step 6.2, the loc finder: the client half (date 2026-11-05)

Build-order step 6.2 ("Catalog as loc finder"; design section 6, "Finding
locs"), the panel's half. The server's half (shape classes and size as
filter keys, prefix matching, scoped "all", the sliced warm build) is
another slice; this one codes against CATALOG_QUERY and the summary fields
it already returns (`shapeK=N,<model>`, `models=`, `width=`, `length=`,
`category=`, `op*`, `multivar*`, `dialogue`).

What landed:

- **VLIST cells** (src/ui; the one chrome change): `ToriRSChrome_VListCells`
  makes a VLIST whose rows are five fields split by `TORIRS_CHROME_CELL_SEP`
  (0x1f): a 16 px scene-sprite thumbnail, the name, a dimmed part, up to
  `TORIRS_CHROME_CELL_KEYS_MAX` (8) keycaps in a column a third of the row
  wide (96 px at most), and a right field six digits wide. Name and dim part
  are measured and ellipsised (R2); keys that do not fit end in "+n". The
  label is the header row in the same columns. A click on a keycap is a CELL
  (`ToriRSChrome_VListPickedCell`, 1-based; the body is 0);
  `ToriRSChrome_VListHoverRow` says the row and cell under the pointer. Cells
  is shape: `WIDGET_ADD` carries `w = TORIRS_CHROME_VLIST_CELLS`, a PICK
  intent's `x` is the cell. The DOM runtime renders the same cells
  (`renderCells`: a picture well, name and dim spans that ellipsise in CSS
  with the row's title saying them whole, `<kbd>` keycaps whose click posts
  PICK with `x`, the right span; modern and IE8 CSS); runtime.js /
  runtime-ie8.js regenerated with tsc 6.0.2. A page has no pixels for a scene
  sprite, so the DOM's picture well stays empty.
- **The model** (edpanel_catalog_model.{c,h}, pure): filter chips
  (`Editor_CatalogChipsTake / Add / Remove / Compose / ChipLabel / ChipTip`;
  a shape NUMBER composes to `shape=N,*`, the head of the cache's
  `shapeK=N,model`, which matches today; a shape CLASS, `size=`, `spawn=`
  pass through for the server slice), the "+ filter" menu
  (`Editor_CatalogFilterMenu*`), shape classes (`Editor_CatalogShapeClass`),
  a row's placeable shapes and size from its summary fields
  (`Editor_CatalogRowShapeMask`, widened as `Editor_LocShapeMask` widens a
  config), the continuous window (`Editor_CatalogModelWantWindow`: the anchor
  page, the next page prefetched a quarter page before the window's end,
  `EDITOR_CATALOG_CACHE_PAGES` 4 held besides the anchor, a window moving
  onto a held page swaps it in with no round trip; `Editor_CatalogModelNextPage`
  says what Refresh asks next; `answers` / `key_serial` tell a landed page
  from a new list), sources (the index, or a local list filtered by Find's
  words and the shape chips), the sort (best match, name, id, category, size;
  over the index once every match is held, gathered while it is five pages
  or fewer, else the count says best match), `Editor_CatalogModelRangeText`
  ("1 to 100 of 212 loc"), `Editor_CatalogCellsText`,
  `Editor_CatalogViewShapeAt` (the keycap's shape), `Editor_CatalogRecentsPush`
  (20, newest first, a repeat moves to the front), `Editor_CatalogPreviewFollow`,
  and `Editor_CatalogModelFeed` (a test gives the model rows).
- **The FAVOURITES document** (src/editor/editor_favourites.{c,h}):
  `editor/favourites.txt`, `favourite <kind>:<name> [| shape S rot R]`, one
  person's, in the AREAS family's line-preserving style (parse -> emit byte
  for byte; an add appends one line, the header lines into an empty file; a
  remove takes its line, the header with the last one; a re-pose rewrites the
  line in place). Not a new document kind: a TEXT document under key
  `editor:favourites.txt` (editor_doc.c's key check, editor_host_local.c's
  path), every edit one TEXT_SPLICE through the authority, so nothing on the
  server or the wire changed and an undo is the splice's own inverse.
- **The Catalog panel** (edpanel_catalog.c): Kind, Find (a `key=value` word
  becomes a chip once a space ends it, a duplicate leaves the box too), the
  count row, the filter chip strip (each with its x; tooltips list a class's
  numbers), "+ filter" (a dropdown whose title row reads "+ filter": a whole
  chip, or `key=` put in Find for category / op), Sort, the shortcut chips
  (favourites (N), recent (N), in square (N), same category (the pick's)),
  and the cells list: 12 rows of 100-row pages, thumbnails asked for the rows
  on screen only, the row tooltip saying the row whole or what a keycap's
  click does. A row click picks (a favourite's or recent pick's pose comes
  with it); a keycap click picks at that shape and arms Place. Refusals go
  through `Editor_UiStatus`. The chip strips are one helper,
  src/editor/panels/edpanel_chips.{c,h}: chips over one GRAPH well (keyed
  rects, so the canvas and the DOM draw the same), wrapping onto its lines,
  captions measured and ellipsised, overflow as a "+n" chip whose tooltip
  names the rest, an optional x key and keycap, per-chip hover tooltips,
  restated only when the labels or the width moved.
- **The Preview pane** (edpanel_preview.c): the subject follows
  `Editor_CatalogPreviewFollow` -- the pick while placing, the primary
  selected loc while selecting (fix: the old fallback looked at the selection
  only until the first pick), the multiloc rung the Shell editor's readout
  chose (`Editor_Catalog_PreviewRung`). Under the well: the pick's shapes as
  chips (the current one lit, a click sets Place's shape), the rotation row
  (0 1 2 3 and Fit, the walls-to-neighbours toggle) -- the Locs panel's
  Place pose, the same tools state, through the chip helper -- the facts
  (size, layer, ops), and the actions as two lines of chips, each bound one
  wearing its chord as a keycap from the live table (tool.place,
  palette.add_pick, view.triggers): Place, the favourite star (lit while
  the pick is one), Palette, Record, Triggers, Find in world (every
  placement in the open squares selected, the camera over the first), Copy
  to new (to the Catalog's New name). Chips, not seven rows of buttons: the
  buttons made the floating Preview 417 px tall, over the bottom group at
  1530x1006 (test-editor-groups caught it). The panel scrolls when a dock
  gives it less than all of that.
- **Pick from world** (Shift+I, app_editor_hotkeys.c):
  `Editor_Catalog_PickFromWorld` makes the hovered placement's loc the pick
  at its exact shape and rotation, arms Place, sets Find to its record name
  (the chips off), and selects and scrolls its row into view once it lands.
- **Thumbnails** (app_map_editor.c, `app_services.loc_thumbnail`): one loc at
  one shape rastered straight down through the Preview's pipeline
  (`app_shape_thumb_raster`, now given its pitch) into a 16 x 16 scene sprite
  in its own id block (0x40002000 + slot), 256 slots cached by (id, shape),
  least recently asked evicted; a config or model still loading is asked for
  once and answers 0 until it lands; at most 4 rasters per 16 ms. Not on the
  web (no scene sprites in a page).

Tests (2026-11-05, this container, objects in build_opt; OPT=0 for the new
test too): **test-editor-catalog-finder** (new, content-free: the model fed
synthetic rows, the favourites codec, then the Catalog and the Preview over an
embedded ToriRSMapEd on a synthetic 150-loc tree with shape lines) 140
checks, 0 failed, at OPT=1 and OPT=0; test-chrome-widgets 941 (924 on 43c1a4ca), 0 failed, dom
ok (the cells list's add, its rows, the drawn picture, keycaps and
ellipsis, nothing past the list, a keycap click and hover, PICK's cell; the
DOM's cells row, header, keycap PICK and a plain list untouched);
test-editor-chords 501, test-editor-groups 2080, test-editor-hotkeys 1784,
test-editor-panels-palette 50, test-editor-panels-accounts 48,
test-editor-areas 136, test-editor-cmd 62, test-editor-dropdown 36,
test-editor-palette 97, all 0 failed; check-tree-walks and check-pt-switch
PASS; `make all` links. test-editor-panels needs the OSRS-Content squares:
its content-free cases plus test_catalog_model, test_catalog_panel and
test_preview ran from a scratch copy of its main against a fixture-only
content dir: 1359 checks, 5 failed, the same five as 43c1a4ca run the same
way (opening m50_50 and m50_51, and the three underlay-paging checks that
need configs/all.underlay). Not run here (they read the content tree):
test-editor-jm2, test-catalog-perf's real-tree half, the square round trips,
test-catalog.

Each new check was seen to fail on a mutant, in a scratch worktree at this
change (restored by copy and touched): the old Preview rule (2 failures), no
prefetch (5), the keycap's cell ignored (6), the last favourite leaving its
header (1), a duplicate chip word kept in the box (2), a shape number
composed without its prefix (2), favourites not resolved from the catalog
(2); in the chrome a keycap click read as the body (2), the name drawn
unelided (2), PICK's x dropped (1); and the DOM test against the runtime.js
of 43c1a4ca (fails at the cells list's first check).

Open:

- The count sits on the row under Find, not in the box's right end: a
  TEXTINPUT has no right-end slot, and this slice's chrome change was the
  cells list only.
- "+ filter" is a dropdown whose first row reads "+ filter", not a chip that
  opens one; its rows say a class's numbers in the label (dropdown rows have
  no tooltips).
- Waiting on the server slice: `shape=<class>`, `size=WxL` and `spawn=yes`
  match nothing until CATALOG_QUERY learns them (a shape NUMBER chip works
  today); a value is still matched whole, not as a prefix; the sort orders
  the index's rows only once they are all held (five pages), the server has
  no sort key; "most placed" is not offered.
- The Locs panel still has its own shape strip and Place pose dropdowns; the
  Preview's pose rows use the new chip helper, and moving Locs onto it is
  the placement slice's (6.3) file.
- In-square names cost one CATALOG_RECORD per distinct loc when the list is
  shown (128 at most, the rest by id); favourites one per favourite on each
  change of the file.
- The thumbnail raster is compiled and linked but checked by no test here
  (no cache in this container); its framing at 16 px is not looked at by eye.
- editor_panels_test's count-row check now reads "1 to 2 of 2 loc"; its
  content-only parts were not run here.
- Favourites are per user by being one file the tree does not share
  (`editor/favourites.txt`); no per-account file name.

## Workbench step 6.3, part 1: the Place and Select tools' direct manipulation (date 2026-11-05)

Build order 6.3 ("Placement feel"), the first slice: design section 7 "Place
tool" and "Select tool", section 4 "Selection and ghost drawing" for the run
preview, R7 (the Client shares Fit) and R8 (a refusal says why). The gizmo,
Alt-duplicate, the level move, the Locs panel as a selection inspector and
stamps are the other slice.

What landed:

- **Runs** (src/editor/panels/edpanel_ops.{h,c}, pure). `struct
  Editor_LocRunPath`, `Editor_LocRunBegin` / `Editor_LocRunTo`: the tiles a
  Place drag crossed, each one orthogonal step from the last (a diagonal step
  is two, x then z); going back over the path takes the tail off; `constrain`
  rebuilds it as the straight line from the first tile along the axis the
  pointer moved further on; capped at `EDITOR_LOC_RUN_MAX` (256).
  `Editor_LocRunPlanMake` turns the path into a `struct Editor_LocRunPlan`:
  with a wall pose (0, 1, 2, 3, 9) and a loc that has the straight wall, a
  WALL run -- one piece per tile on the side of the path the pose stands on
  (the left when it stands on neither), the ends straight, a turn away from
  the wall an L (2), a turn toward it a corner post (3), both at
  `Editor_LocWallCornerRotation` (the r with {r, r+1} = the two edges; Fit's L
  now uses the same function); a loc without shape 3 leaves the inside corner
  empty (`EDITOR_LOC_RUN_NONE`), one without 2 takes the straight piece.
  Anything else puts the pose on every tile. Each piece is judged against the
  document: PLACE, KEEP (already there), CLOSED (square not open) or TAKEN
  (another loc on the key: for a wall the tile's wall layer, shapes 0..3, for
  the rest the tile and shape); this loc's own piece in another pose is
  re-posed (`has_holder`: a replace, or a remove and an add when the shape
  changes). `Editor_LocRunCommands`, `Editor_LocRunRefusal` ("Place refused:
  tile 3230,3240 L0 holds a centrepiece of that shape (loc 500); press E to
  swap", "... is in m51_50, which is not open, and 2 more tiles of the run").
  `Editor_LocShapeLayer` (world.h's `World_LocShapeToLayer`, asserted equal
  over all 23 shapes by the test: the diagonal wall is ground layer).
- **The drag** (edpanel_actions.c, declared in edpanel_common.h):
  `edp_run_begin` / `edp_run_to` / `edp_run_end` / `edp_run_cancel` over a
  `struct Editor_PlaceRun` the panel holds (`Editor_Panel::run`). The plan is
  re-made when the path changes, with the ghost's pose at the first tile (Fit
  applied); the first refusal of a run is said once (`Editor_UiStatus`); the
  release re-plans against the document, applies the commands as ONE undo
  step ("wall run" / "place run" in History) and selects what it placed; a
  press that never left its tile returns -1 and is the click it always was.
- **Select** (pure: `Editor_LocFootprint`, `Editor_LocsTouchingRect`,
  `Editor_LocsOfId`, `Editor_LocSelectModeOf`, `Editor_LocSelectionMerge`;
  actions: `edp_select_rect`, `edp_select_same`, `edp_select_refs`): the
  marquee selects every loc on the rectangle's level whose footprint (a
  centrepiece's record size, turned) touches it; Shift adds, Ctrl toggles,
  Alt subtracts; select same is every loc of an id on a level in the open
  squares. The selection stays document addresses.
- **The World** (app_map_editor.c, editor_panel.{h,c}): a Place press with a
  loc pick opens a run (`Editor_PanelRunBegin`), the drag carries it
  (`Editor_PanelRunTo`, Shift = the table's `place.constrain`), the release
  commits it (`Editor_PanelRunEnd`) and the ghost lands as a click's does. A
  Select press on empty ground (no loc pointed) becomes the marquee once the
  pointer leaves its tile: it IS the Rect tool's rectangle (`tools.rect`,
  `rect_dragging`), so copy, fill and clear act on it, and its release
  selects (`Editor_PanelMarqueeSelect`). A click with Shift / Ctrl / Alt adds,
  toggles or takes out the pointed loc (`Editor_PanelSelectPointed`; Shift
  used to toggle); a plain click still latches the tile; a second click on
  the same loc within 400 ms (the chrome's clock) is select same
  (`Editor_PanelSelectSameAt`). A press and release in one frame stay a
  click. Escape drops a run or a pending marquee; the editor closing does too.
- **The preview** (app_overlay.c `app_overlay_build_editor_run`): while a run
  is held, every piece of its plan in the ghost colour -- a straight piece as
  the edge it stands on, an L as both, a post as a small square on its corner,
  a non-wall pose as its footprint block -- and a refused tile filled and
  outlined in the footprint red.
- **Fit per Client.** `EditorUiTools::place_fit` (1 at boot) is value 14 of
  STATE_TOOLS (`EDITOR_UI_TOOLS_VALUES` = `TORIRSMAPED_TOOLS_VALUES` = 15);
  `edp_place_fit_enabled(ui)` / `edp_place_fit_set(ui, on)` read and write it,
  so every window's ghost, Locs panel checkbox and hotkey agree. The file-scope
  `g_place_fit` is gone. edpanel_locs.c changed only at its three call sites
  (the signature).
- **Refusals say why** (R8): place (no pick, a shape the loc lacks, a closed
  square), nudge / move / rotate / delete / fit with nothing selected, a
  reshape or duplicate onto a held key, swap with no pick or no selection, a
  loc still loading, the Rotate / Delete tools on an empty tile, a marquee
  over 4096 tiles, select same with nothing to name: one sentence each through
  `Editor_UiStatus`, "<Verb> refused: ...". Successful swaps stay console
  lines.
- **Two chords** (editor_hotkeys.{h,c}, docs/MAPED_HOTKEYS.md):
  `edit.select_same` Alt+A (W, L: the hovered loc's id, else the primary
  selected loc's -- the menu row's and a button's path, `Editor_HotkeyRun`),
  and `place.constrain` Shift held (W). Constrain is a QUALIFIER
  (`Editor_HotkeyIsQualifier`: Fly faster, Snap while dragging, Constrain):
  two qualifiers on one bare modifier are not a clash, so the default table
  stays conflict-free with Shift both flying faster and keeping a run
  straight; an edge action on a bare Shift still clashes, as does a qualifier
  rebound onto an ordinary chord. The Hotkeys panel captures a bare modifier
  for every qualifier. Hint strip: Select gains "[Alt+A] same", Place "[drag]
  wall run" and "[Shift] one axis" (the touch strip "[drag] wall run").

Tests (2026-11-05, this container, build_opt; OPT=0 with asserts as noted):
**test-editor-placement** (new, content-free: maped/placement.mk; synthetic
squares under $(OBJ_DIR)/tmp/placement, an embedded ToriRSMapEd, a second
session for the Client) 118 checks, 0 failed -- the path, the layers against
world.h, the corner rule, the footprint, the merge's four modes; the 6-tile
wall run with two corners (post 3 rot 0 inside, L 2 rot 0 outside, straight
ends rot 1, Fit's own answer for that L agreeing), nothing in the document
before release, six locs and one undo group after, one undo taking all six
with the .jl2 byte-identical, redo, the authority's square re-read by a second
session byte-identical to the mirror (the content-free stand-in for the
STATIC rebake), the old straight end re-posed as the next run's L and its
undo, a postless loc's empty inside corner, a cancelled run, a click; a
centrepiece run refusing the crate's key (said once a run, not again on
release), keeping its own, one undo; another loc's L refusing a wall piece; a
closed square named; the marquee by footprint (a 2x3, a turned one, level
filtered) with replace / add / toggle / subtract and the 4096-tile refusal;
select same and its Alt+A path; Fit heard by the Client's other window both
ways; six refusals each said once. test-editor-hotkeys 1824 (1784), 0 failed
(Alt+A, held Shift's two qualifiers firing nothing, the clash rules, the hint
rows, the doc rows); test-editor-cmd 66 (62), 0 failed (a run's remove + add
stroke undone whole, byte-identical); test-editor-groups 2080,
test-editor-chords 501, test-editor-workbench 284, test-editor-panels-palette
50, test-editor-panels-accounts 48 (`ulimit -s unlimited`), test-editor-areas
136, test-editor-world-gesture 42, test-editor-dropdown 36: all 0 failed.
OPT=0 (asserts on, objects in build/): test-editor-placement 118,
test-editor-hotkeys 1824, test-editor-cmd 66, test-editor-groups 2080, all 0
failed. `make -C src all` links; check-tree-walks and check-pt-switch PASS.

edit_world_test gains sequence 14 (content): the 6-tile castlewall run with two
corners through edp_run_* with the world flushed and held to a STATIC bake
after the run, its undo (.jl2 byte-identical, nothing left on the corner's
tile) and redo; a run of a pool centrepiece over a key Q holds; the marquee's
three modifiers over the placed pieces; select same over every castlewall.
Its two older window UIs get a chrome (the status sentence's clock) and Fit on.
It compiles and links here; it cannot run (no OSRS-Content checkout, no
cache.osrs241), nor can test-editor-panels or test-editor-jm2.

Each new check was seen to fail on a mutant, in this private worktree on
the committed change, each restored with git checkout and rebuilt before the
clean runs above: the inside / outside corner test inverted (7 failures), the
corner rule always the first edge (2), a diagonal step taken whole (3), no
retrace (2), Shift ignored (6), the footprint not turned (4), Ctrl's toggle
as an add (3), Fit left out of the STATE_TOOLS pack (2), the refusal said on
every tile (2), the release one undo group per command (7), the wall run
judging the shape and not the wall layer (4), a refusal logged and not said
(3), the qualifier exemption removed (3, test-editor-hotkeys). The two chords'
and the hint rows' checks name actions the base tree does not have. The
editor_cmd_test addition pins the command layer's existing behaviour (it
passes on the base too). The App glue (press / drag / release, the double
click, Escape) and the overlay's drawing have no content-free test: they
need an App and a world.

Open:

- The Catalog pane does not show the Fit bit yet: the Catalog panel is 6.2's
  file. It reads `edp_place_fit_enabled(ui)` (shared) when it does.
- A plain click on a loc still latches the TILE, not the loc (the design's
  "Click a loc selects it" changes what every Select click does; the
  minimenu's rows and Shift / Ctrl / Alt pick locs).
- The refused-drop red that lasts a second after a single click's refusal
  (design section 4) is not built; a run's red lasts while it is held.
- A wall run's ends do not Fit to perpendicular walls already there (they are
  straight, as the design says); its side follows the first segment's pose
  only, and a run whose pose stands parallel to the first step goes left.
- The run preview is drawn with the overlay's segments at the ground's
  height; the "fitted" chip and the rotation arrow (section 4) are not drawn.
- The marquee holds 64x64 tiles (4096) at most, the Rect tool's limit; past
  `EDITOR_LOC_SELECTION_MAX` (256) locs it says how many were left out.
- No photograph of a run or a marquee was taken (no content checkout).

## Workbench step 6.3, part 2: the gizmo, level moves, Alt-duplicate, stamps (date 2026-11-05)

The second half of build-order step 6.3 ("Placement feel"; design section
7, "Move, rotate, delete", "The Locs panel as a selection inspector",
"Stamps"). Wall runs, the marquee and per-Client Fit are part 1's.

What landed:

- **The move gizmo** (pure: src/editor/loc_editor_selection.{h,c},
  `LocEditorGizmo_*`; glue: app_map_editor.c's gizmo section; drawing:
  app_overlay.c `app_overlay_build_editor_gizmo`). On the primary selected
  loc under Select or Move: four arrows (N/S green, W/E red), a ring and a
  centre handle (yellow), computed in world units around the loc's placed
  footprint and projected through the World panel's camera every frame
  (`App_MapEditorGizmoScreen`), so it follows the panel. A press is
  hit-tested against the same points BEFORE the world click (centre, then
  arrows, then ring; 6 px slop), so a handle never also latches the tile.
  An arrow proposes whole tiles passed along its axis, never backwards (a
  click is one step); the ring the pointer's sweep about the centre, a
  quarter per 90 degrees clockwise (a click is a quarter, a whole turn is
  none); the centre the tile under the pointer. Nothing moves until the
  release: then ONE action, one undo step -- `edp_locs_nudge` for an arrow
  or the centre (the whole selection; the swap rule of 2026-11-02 applies),
  `edp_locs_duplicate_by` with Alt held on an arrow (the copies become the
  selection), `edp_locs_rotate_by` for the ring. While held, the multi-piece
  ghost draws each selected loc's destination footprint, orange where the
  key is held by a loc the move does not carry along (the swap shown before
  release); a ring drag says "Turn 180 degrees: release to apply". The
  handle under the pointer is drawn white and wider and named in the status
  bar (R5).
- **Level moves and far nudges** (hotkey table, docs/MAPED_HOTKEYS.md):
  `edit.level_up` / `edit.level_down` Ctrl+PageUp / Ctrl+PageDown (context
  L) move the selection a level, each loc keeping its tile, shape and
  rotation (a stack moves top first); ALL OR NOTHING, refused with a status
  sentence where a loc would leave levels 0..3, its square is not open, or
  the key above / below holds a loc the selection does not
  (`Editor_LocOpsLevelMove`, `LocEditorSelection_LevelTarget`).
  `edit.nudge_far_*` Shift+arrows (context M) nudge by 8 tiles, the same
  swap rule.
- **The Locs panel as a selection inspector** (edpanel_locs.c): a Find box
  over the list (R3; loc id, shape name or tile), the primary first and
  marked "> ", Follow (a row's click or Enter puts the camera on that loc,
  `camera_to_tile`), the footprint row ("Footprint 2x1 | ground layer |
  blocks: no", the placed size from the App's record, turned with the loc),
  the Multiloc row ("shell of 3 (varbit 4153)" / "no shell"), Up a level /
  Down a level (wearing Ctrl+PgUp / Ctrl+PgDn), Find same (every loc of the
  primary's id on its level in the open squares, primary first), Stamp name
  + "with the terrain under it" / "with the spawns on it" / Overwrite + Save
  as stamp.... The shape strip is a GRAPH now (it was a MODELVIEW): the App
  hands back its rendered picture (`Editor_Locs_ShapeStripSprite`) and the
  panel draws it under a keyed slot per shape, so a THUMBNAIL click reshapes
  (or sets the Place pose) as its button does; the hovered slot brightens
  and the tooltip says "Reshape to 11 centrepiece diagonal" with Shift+Q.
  The strip and pose arrays hold all 23 shapes (they were 16 and
  `Editor_LocShapeList` could write past them).
- **Stamps** (src/editor/editor_stamps.{h,c}, the codec; panels/
  edpanel_ops_stamps.{h,c}, the ops and actions; panels/edpanel_stamps.c,
  the list). A stamp is `editor/stamps/<name>.stamp`, a line-preserving TEXT
  document (key `stamp:<name>`; the list is `editor/stamps/index.txt`, key
  `stamps:index`; `Editor_DocKeyValid` and `Editor_HostDocPath` know both):
  `stamp <name> [| note]`, then `loc dx dz dlevel id shape rot [WxH]`,
  `tile dx dz dlevel [h u o/s/r f]`, `spawn npc|obj name dx dz dlevel
  [count]`, relative to the primary's tile and level. Parse -> emit is byte
  exact; a stamp is written as TEXT_SPLICE commands planned from the old
  text to the new (`Editor_StampSplices`, the common head and tail kept,
  chunked to the command's 1024 bytes), the stamp and its index line in ONE
  undo step, then saved. The Catalog wears a "Stamps..." chip beside Kind
  (its one hunk, in `Editor_Catalog_Register`) that opens module "stamps":
  a Find box, "3 of 12 stamps", a VLIST whose Enter / click (or Place) arms
  the stamp for the Place tool. Armed, the multi-piece ghost shows every loc
  piece's footprint at the hovered tile (orange where it would replace a
  loc), Q turns the whole stamp a quarter about its origin (each offset
  (x, z) -> (z, -x), a footprint's SW corner following, its size swapping,
  each loc's and overlay's rotation stepping), a click or Space drops it as
  ONE undo step (tiles, then locs -- a replace onto a taken key, as Place
  --, then spawns) and the placed locs become the selection; another tool
  or another catalog pick disarms it, Esc drops the ghost.
- **The unnamed stamp**: Ctrl+C on the rectangle also copies its locs on its
  level (`Editor_StampCaptureRect`) and Ctrl+V pastes them AFTER the tile
  commands, which are exactly the tile paste's, in the same undo step.
- **The multi-piece ghost** (map_editor_ghost.h `MapEditorPieceGhost`, a
  sibling in `struct MapEditorGhost`): footprints with a taken flag, keyed
  on what built them so a quiet frame rebuilds nothing; drawn, not placed.

Tests (2026-11-05, this container, objects in build_opt; the ops test also
at OPT=0 with asserts in build_w63): **test-editor-stamps** (new, pure) 91
checks, 0 failed; **test-editor-stamps-ops** (new, content-free: an
embedded ToriRSMapEd over a synthetic tree it writes -- one hand-written
square, faked loc records; save, arm, turn, drop as one step, undo back to
the .jl2 and .jm2 byte for byte, paste with locs, the level move and its
refusals, Alt-copy and ring turn, Find same, the inspector rows, Follow,
the strip's thumbnail click, the Catalog chip and the Stamps list) 81, 0
failed; test-loc-editor-selection (the gizmo's geometry, hit test, arrow,
Alt, ring and centre semantics, the level target) all passed;
test-editor-ghost OK (the piece ghost); test-editor-chords 537 (501), 0
failed (Up / Down a level wear their chords; no new string is cut);
test-editor-hotkeys 1832 (1734), test-editor-groups 2212, test-editor-cmd
62, test-editor-palette 97, test-editor-areas 136, test-editor-dropdown 36,
test-editor-panels-palette 50, test-editor-panels-accounts 48,
test-editor-world-gesture 42, all 0 failed; check-tree-walks and
check-pt-switch PASS; `make all` links. Each new check was seen to fail on
a mutant in this private worktree, restored with git checkout and its
objects deleted before the clean runs: an arrow allowed backwards (1), the
ring truncating (1), the turn ignoring a footprint (2), the splice keeping
no tail (1), the piece ghost ignoring the turn (1), the level move without
its taken check (1), paste without its locs (2), the strip without its
event hook (1), Up a level wearing another chord (3).

Could not run here: test-editor-panels, test-editor-jm2 and the content
half of test-catalog-perf (no OSRS-Content checkout), and nothing was
photographed (no running client with content): the gizmo's look, its hit
slop at 2x and the stamp ghost are unchecked by eye.

Open:

- The stamp ghost draws footprints, not the translucent models the one-loc
  ghost places; a drag's preview likewise.
- The Multiloc row says whether the primary is a shell; "N shells use it"
  needs the server catalog to index multiloc children (it counts a block's
  `multiloc` slots only), so the count is not shown.
- A stamp's tile heights drop verbatim (the tile paste shifts them by the
  anchor difference); the rectangle's unnamed stamp carries only locs on its
  level; stamp spawns are the edited files' rows only.
- No delete / rename of a stamp; the Stamps panel registers with the
  Catalog, so a window without App services (a controller) has no list.
  Save as stamp also writes the files at once: its undo reverts the
  documents, and the files follow on the next save.
- The gizmo keeps its place during a drag, its slop is not scaled by the
  chrome scale, and it has no touch spelling beyond a one-finger drag
  (which is a left drag). "Refuse where the tile above is not open" is read
  as no level / square not open / key held; there is no floor-presence test.
- The hint strip does not list Shift+arrows, Ctrl+PgUp / PgDn or the gizmo.

## Workbench step 6.4, part 1: the picker's drawing and loading (date 2026-11-05)

Build-order step 6.4 (design section 5, "Square picker"), the half that is
what the atlas DRAWS and when its pictures LOAD. Navigation (keyboard, the
open-set strip, the detail pane, the Area dropdown itself) is the other
slice's, in src/editor/panels/edpanel_squares.c, which this part does not
touch.

What landed:

- **Level of detail by zoom** (src/editor/panels/edpanel_picker.h
  `Editor_PickerLodAt`, a pure zoom -> what-draws table, `struct
  Editor_PickerLod`): under 12 px a square the atlas row's average colour and
  the page's outline once (no picture, no label, nothing asked of the map);
  12 to 40 the world-map picture, no icons, no labels (a lane's square gets a
  blue 1 px inset, `Editor_PickerColors.lane`); 40 to 96 the picture, the
  open-here / unsaved / created outlines, a dot per 10 spawns (at most 12 a
  cell), the coordinate label in the hovered cell only (on a plate,
  measured and ellipsised to the cell, never clipped); 96 and up the map's
  icons (opaque) and names (large and medium at once, small fading in to 128),
  "61 locs" in each cell, the 8x8 zone lines faint. The selection, hover, the
  empty cell New would make and the view frames draw at every zoom. The map
  (src/editor/editor_worldmap.c) follows the same table: below 12 it asks for
  nothing and clears its underlay; icons and names are asked for from 96.
  The old 40-64 icon fade and the 6 / 16 / 40 label thresholds are gone (the
  design's "icons wait"), so a 60 px photograph shows no icon.
- **The item budget** (`EDITOR_PICKER_ITEMS_BUDGET` 2600): `Editor_PickerDraw`
  runs twice, counting then drawing. The user's marks (selection, hover and
  its label, the empty cell, frames, the page outline) have room kept for
  them; the rest of the essentials (cells, pictures, outlines, axis labels)
  take what the marks leave; decoration (spawn dots, loc counts, zone lines,
  the map's icons and names, hatching) only what is left over. Whatever is
  dropped is counted by kind (`Editor_Picker.dropped[]`, `Editor_PickerDropName`)
  and said once per change: `drop_text` ("picker: 2600-item budget at 40.0 px
  a square, dropped 14924 spawn dots") on stderr through TORIRS_LOG, and
  `truncated` keeps the readout's "zoom in: not all drawn".
- **Visible first** (editor_worldmap.c): a window the view JUMPED to (a cold
  open, a search, a zoom out: less than half of it shared with the last whole
  window polled) is polled middle half first (`Editor_WorldMapCore`), so its
  geography loads queue ahead of the edges', and whole the next tick; a pan
  polls the whole window at once. One poll a tick (the renderer's bake and
  load budget is per poll). Once the view reports nothing missing, the rest
  of the page follows a `EDITOR_WORLDMAP_SLICE` (8) square slice a tick,
  nearest the view first (`Editor_WorldMapNextSlice`, pure), at one pixel a
  tile, a slice still loading asked again after 15 ticks; a slice that took
  more than `EDITOR_WORLDMAP_PREFETCH_US` (2 ms, the new optional
  `EditorUiAppServices.now_us`) waits its excess out in ticks. The geography
  load task is unchanged: the order its loads are QUEUED in is what changed.
- **The cold open, measured**: from the first (middle-half) poll of a cold
  area until its view reports nothing missing, every frame's work
  (`EditorUiAppServices.last_frame_us` = `App_LastFrameUs`) is counted
  against 33 ms; `TORIRS_WORLDMAP_STATS=1` prints "worldmap picker: cold open
  N frames, slowest X ms, K over 33 ms".
- **Instance space** (torirs_maped_catalog.h `TORIRSMAPED_INSTANCE_X0..Z1` =
  0..19 x 100..255, `ToriRSMapEdCatalog_InInstanceSpace`; the design's section
  13): atlas rows with no area there are flagged
  `TORIRSMAPED_SQUARE_INSTANCE` (64, a flag bit: no wire change). The picker
  always has an "Instance space" page (`EDITOR_PICKER_TAB_INSTANCE`,
  `Editor_PickerInstanceTab`), just before Unmapped, framing the whole space;
  since the Area dropdown lists the tab titles it is reachable from there.
  On the Instance space, Unmapped and Full map pages instance space is
  hatched ("/" lines 8 px apart, anchored to the global pixel space) where no
  square is, its squares filled over the hatching.
- **Loc count** (additive, on the wire): FACT_CATALOG_SQUARES rows end with
  `u32 loc_count` (the locs the `.jl2` places, one a line after the header;
  `cat_parse_jl2` now counts instead of stopping at the first);
  `EditorHost_CatalogSquare.loc_count`, `Editor_PickerSquare.loc_count`,
  `ToriRSMapEdCatalogSquare.loc_count`. ~~A client and a server must be built
  from the same tree~~ **Closed (6.2-6.4 review fixes, below):** gated by
  `TORIRSMAPED_CAP_CATALOG_SQUARE_LOCS`; a row carries the count only when
  flagged `TORIRSMAPED_SQUARE_LOC_COUNT`.
- **The web shows the map** (design: "the web shows no map today"): the
  worldmap services are no longer compiled out under `__EMSCRIPTEN__`;
  `ui_service_image_publish` also hands each 128x128 picture to
  `ToriRSChromeExecWeb_GraphImage` (src/ui/torirs_chrome_exec_web.h), which
  calls `window.torirsChromeGraphImage`; the host (src/web/torirs_chrome.js
  `graphImage`) sends a `graph.image` envelope keyed by scene id (RGBA base64)
  and keeps the last one per id, replayed to a replaced iframe; the runtime
  (src/plugin_chrome/runtime-source.js `applyGraphImage`,
  `renderGraphImages`) draws every IMAGE item (GRAPH item kind 4: scene id in
  `v`, transparency in `c`) onto one `canvas.tpc-graph-canvas`, the well's
  first child, under its other items, unsmoothed. Pictures outlive page
  snapshots. Envelope documented in src/plugin_chrome/HOST_BRIDGE.md.
  runtime.js and runtime-ie8.js regenerated with tsc 6.0.2.

Tests, content-free (this checkout has no OSRS-Content):

- `make -C src test-editor-worldmap`: 161 checks (31 before). It now links the
  picker and the real chrome: the LOD table at every band edge; each band's
  picture from a synthetic atlas (colours and the page outline at 8, the
  picture and the lane inset and no outline at 24, the unsaved outline / two
  dots for 25 spawns / the hover label and no icon at 60, the icon / name /
  "61 locs" / zone lines at 128, an ellipsised label within its 32 px at 40);
  the budget at 14 zooms of an 81x81 atlas with 200 spawns and 1000 locs a
  square in an 1800x1200 well (never past 2600, the selection kept, every drop
  said), dots dropped and no cell at 40, cells dropped but the selection and
  hover kept at 12 in a 3000x2400 well; the Instance space page, its bounds,
  hatching inside the grid, its square filled over it, none on the surface;
  the pure slice order and core; and visible first against fake services (the
  middle half then the whole window, the view alone while it loads, the cold
  open measured with one 41 ms frame, one poll a tick, slices nearest first,
  a 5 ms slice sitting out two ticks, every slice once then quiet, a pan
  whole, a jump middle first, nothing at 8 px, nothing on Instance space).
- `make -C src test-catalog-perf` (synthetic half): 67 checks (59 before):
  a three-square `.jl2` tree's loc counts, the instance flag, and every
  CATALOG_SQUARES row ending with its loc count.
- `make -C src test-chrome-widgets`: dom ok, with the graph.image case: the
  native 128x128 ARGB words through the real host and runtime onto the
  canvas, whose pixels over the item hash (FNV-1a) to the native sprite's;
  trans 128 at alpha 127/255; pictures kept across a page snapshot; bad
  envelopes refused; the replay to a new runtime.
- Seen to fail before the fix (a throwaway worktree, its own objdir): icons
  in the 40 band; no budget reserve (the selection dropped at 10 zooms); no
  middle-half poll; no slow-slice wait; no prefetch; no LOD gate in the map;
  no hatching; the old first-loc jl2 parse and no instance flag; the old
  runtime.js under the new DOM test.
- editor_panels_test's `test_worldmap` / `test_picker` cases were updated to
  the new bands (icons at 128, none at 60 or 20; the open-here mark checked at
  48; the Full map's area asked once zoomed in; the fake's view poll kept apart
  from the background slices; the Area dropdown's last area is tab_count - 3)
  and compile, but cannot run here (they copy squares from the content tree).

Not done here / open:

- No frame was measured over a real cold open: the OSRS cache and content are
  not in this checkout. The measurement is in (`TORIRS_WORLDMAP_STATS=1`), the
  33 ms check is not yet answered.
- Under 12 px a page of more than ~2500 squares (the surface at Fit in a large
  well) still cannot draw every cell within the 2600 items, as before; it is
  now counted and said. A one-image colour atlas (a pixel a square) would
  remove that.
- The web: no emscripten toolchain here, so the wasm side
  (`ToriRSChromeExecWeb_GraphImage`, the services enabled under
  `__EMSCRIPTEN__`) is compiled natively only; the map's element ICONS are
  scene sprites with no pixels sent, so the web map has no icons yet.
- The squares panel's readout shows "not all drawn" but not `drop_text`; the
  Area dropdown's "lane: name" entries, Find's `instance:` and the "New or
  Instance can make one" action on a hatched cell are the navigation slice's.
- The design's "white dashed" camera window and "yellow: open here" colours
  are not applied (the selection keeps yellow, open-here its cyan inset).

## Workbench step 6.4, part 2: the picker's navigation and panes (date 2026-11-05)

Design section 5 ("Navigation", the open-set strip, the detail pane, the Area
dropdown's lanes), section 2 (R2 text never clips, R5 hover tells, R8 nothing
without a word), section 16 step 6.4 in part. The picker's drawing
(edpanel_picker.c: level of detail, visible-first pictures, GRAPH images on
the web) is the other half of 6.4 and is not touched here; this half only
calls it.

What landed:

- **Find** (src/editor/panels/edpanel_picker_find.{h,c}, pure).
  `Editor_PickerFindParse` reads the text alone: `m50_50`, `50 50` / `50,50`,
  `3222 3218 [level]` (a tile: the square holding it, PINNED), a name,
  `lane:<name>`, `instance:` / `instance:<name>`; it refuses `m50`, a square
  past 255, a level past 3, `lane:` alone, four numbers, one number.
  `Editor_PickerFindResolve` answers it against an atlas and the AREAS
  document's collection names: a cell (an empty one is where New makes it;
  from a lane's page the lane's copy first), an Area page (title, internal or
  display name, exact then contained), a collection (likewise), a lane's
  storage, the instance space (`EDITOR_PICKER_INSTANCE_X0..X1 x Z0..Z1`,
  0..19 x 100..255, design section 13). The Squares panel applies the match:
  a cell is shown at least `EDITOR_PICKER_ZOOM_SHOW` (24) px a square,
  centred, focused, and the well takes the keyboard; a page switches the
  Area dropdown; a collection is `Editor_SquarePicker_SetCollection`; a lane
  filters to its storage on the Full map page and frames its squares; the
  instance space is framed (as far as the page's zoom range lets it). A miss
  says why in the status bar. The tile pin is shown in the detail pane's
  Tiles row ("pin 3222,3218 L0"); drawing a marker on the atlas is the
  drawing half's.
- **Keyboard: context K** (editor_hotkeys.{h,c}, docs/MAPED_HOTKEYS.md
  "Picker"). `EDITOR_HK_CTX_K` (bit 8; `EDITOR_HK_CTX_COUNT` 9, `_ALL`
  0x1ff) is "the picker has focus"; it excludes every other context, G
  included, because while the Squares well holds the keyboard it is a field
  (the App's dispatcher gates on `app_chrome_holds_keyboard` and resolves
  nothing). The Picker rows: `picker.north/south/west/east` (arrows,
  repeat), `picker.open` (Return), `picker.add` (Shift+Return),
  `picker.back` (Backspace), `picker.clear_find` (Escape). The well resolves
  its own keys against the K rows with the new `Editor_HotkeysMatch(hk,
  chord, contexts)` (a clash goes to the later binding, as the resolver's),
  so a rebind re-routes them and relabels the pane's Open / Add keycaps on
  the next frame. `Editor_HotkeyRun` sends a Picker action to
  `Editor_SquarePicker_Key`. The Hotkeys panel gained the "Picker" group.
- **The chrome hands the well its keys.** The focused GRAPH now forwards
  Enter, Backspace and Escape as GRAPH_KEY (negated `ToriRSChromeKey`, as the
  arrows were) and Escape still lets go of the focus afterwards; a GRAPH_KEY
  event's `col` carries modifier bits (1 Ctrl, 2 Shift, 4 Alt) when the
  presentation knows them. The DOM executor (runtime-source.js; runtime.js
  and runtime-ie8.js regenerated with tsc 6.0.2) binds `keydown` on the well
  and posts `TORIRS_CHROME_INTENT_GRAPH` with value GRAPH_KEY (10), x the key,
  y the modifiers; torirs_chrome_exec.c accepts it. The native host uses its
  own shift (`ui->shift_held`).
- **Focus** (edpanel_picker_nav.{h,c}, pure). `Editor_PickerFocusCell`,
  `Editor_PickerFocusSet` (the square there, the page's storage first, else
  the empty cell), `Editor_PickerFocusStep` (north is +z, clamped to the
  world; from nothing it starts at the well's middle) and
  `Editor_PickerKeepInView` (the least pan that brings the cell in). The
  Squares panel draws the focus with the selection outline in white
  (`EDITOR_SQUARES_FOCUS_RGB`, `colors.selected`); the arrows no longer pan
  (they move the focus, and the view follows it).
- **The open-set strip** (a second GRAPH, under the well so the well stays
  the panel's first). `struct Editor_PickerOpenSet` follows the picker's
  OPEN_HERE / DIRTY_HERE flags in open order, by full key
  (`Editor_PickerOpenSetSync`, `_Find`, `_Move`); one chip per square
  ("m50_50", "mylane/m16_101" by `Editor_PickerChipText`), a dot on an
  unsaved one, its text fitted to 120 px. `Editor_PickerStripLay` is the
  collapse rule: every chip while they fit, else as many as fit beside a
  "+N" chip, never a chip past the track; "+N"'s tooltip names the rest.
  Click goes to the square (focus, and `camera_to_tile` to its centre; one
  not in this window's world is added first); Shift+click closes it through
  the save guard (new `EDITOR_UI_GUARD_CLOSE_SQUARE`, the new
  `EditorUiHooks::close_square`, the App's `sq_close_pending` drained in
  `app_map_editor_open_pending_square`: the world reloads with every other
  square `WorldEdit_SquareInWindow` says it shows; the only square, or one
  not shown, is refused with the reason); a drag reorders. Backspace goes to
  the open square before the focused one, wrapping.
- **The detail pane** (under the atlas): a heading ("area - key - state") and
  six key / value rows, Square (key and storage), Area (most specific, "and
  more"; unmapped, instance space), Tiles (and the pin), Contents (from this
  window's mirror when it holds the square: locs, npc and obj spawns from its
  spawn file, levels used; else the atlas's spawn count), State (open in
  this window, unsaved, created, in another window, on disk), Collections.
  It answers for the focused cell, else the hovered one, and is made again
  only when what it reads moved (a hash). Every row is fitted to its track
  (`Editor_PickerFitText`: the widget's laid-out width less the KEY_VALUE
  row's pads, key and name gap, in device px of the row face), and a cut
  row's tooltip is the whole text. Actions: Open and Add to open set
  (wearing picker.open's and picker.add's keycaps), Open in new view, Copy
  to..., Instance... (a stand-in: the New square dialog with its storage
  question, and the status bar says the wizard is step 6.5), Clear... (the
  existing confirm), Delete... (refused with the reason: the server has no
  square delete); a Recent dropdown of the last 8 squares opened
  (`Editor_PickerRecentPush`), which goes to the one chosen. When the
  layout gives the panel a height that would leave the atlas under 200 px,
  the rows past the heading (and the five actions past Open and Add, and
  Recent) fold away; content-sized they are open.
- **The Area dropdown** lists the atlas's pages, then "lane: <name>" per lane
  storage, then "Instance space" (rows built with the atlas; the dropdown
  borrows them). A lane row filters to the lane on the Full map page; a page
  row afterwards drops the lane's filter.
- Entry points for a headless window and the tests:
  `Editor_SquarePicker_TakeAtlas`, `_Find`, `_Key`, `_PaneRow`, `_Strip`,
  `_Recent`.

Tests (content-free):

- `make -C src test-editor-picker` (new, src/maped/picker.mk,
  src/editor/test/editor_picker_test.c; 396 checks): the parser over every
  form and the refusals; the resolver over a synthetic atlas (the surface
  m48..53 x 48..53, a dungeon with a 60-character name, Unmapped, a square in
  instance space, a lane with two squares); focus over that atlas, pure and
  through the registered panel's well (the chrome's own key path): arrows,
  Enter, Shift+Enter (host shift and the DOM's `col`), Enter on an empty cell
  (New square), Backspace, Escape (clears Find and the pin, lets go of the
  keyboard), the white outline; the collapse rule (exact fit, a pixel short,
  all folded, 64 chips at 70 widths never past the track); the strip's chips
  in open order, the dot, click / Shift+click / drag, "+N" and its tooltip
  with 37 open; every pane row measured against its track at the default
  width, on an empty cell, in a narrow panel and a tall one, folding; the
  pane's refusals; Recent; the Area dropdown's lane and instance rows.
- test-editor-hotkeys: `test_picker_context` (K excludes every context and
  itself only, the K rows resolve through `Editor_HotkeysMatch` and nowhere
  else, a rebind re-routes, the World keeps its chords) and the doc test over
  the new rows. test-chrome-widgets: the GRAPH_KEY intent and the native
  well's Enter / Backspace / Escape, and the DOM's keydown in
  chrome_widgets_dom_test.js. test-editor-chords: the pane's Open / Add and
  New square wear their keycaps.
- test-editor-workbench: the 1x registry golden was recorded again (the
  Squares panel docked in that layout gained the strip and the pane's
  heading; the column's shares moved). Probed against the base tree's build:
  only the dock column and the Preview beside it moved.

Seen to fail before their fix, in a scratch copy of the tree (five
mutations in one build: the strip never folds, the pane's rows set unfitted,
no `lane:` form, the focused well keeping Enter, K not exclusive):
test-editor-picker 101 of 384 failed (the parser's and resolver's lane rows,
the collapse rule, Enter / Shift+Enter / New square on an empty cell, every
too-long pane row, "+N" and its tooltip), test-editor-hotkeys 11 (12
clashes, K overlapping every context), test-chrome-widgets 1 (Enter not
handed on).

Not run here: test-editor-panels (needs the OSRS-Content tree; two of its
checks were updated to this step: the arrows now move the focus, and the
selection outline is `EDITOR_SQUARES_FOCUS_RGB`).

Left open:

- The pane sits UNDER the atlas at every width: the chrome lays a panel's
  widgets in one column, so the design's "340 px right of the atlas" form
  needs a two-column row (or the pane drawn inside the well, which is the
  drawing half's file). The fold rule above is the narrow form.
- A plain click on a square still opens it (design: click focuses,
  double-click opens) and the right-click minimenu is the old one; the
  pointer rows are not in this slice.
- The tile pin has no marker on the atlas (drawing half).
- Closing a square keeps it loaded in the document mirror, so a later Add
  (which reopens every loaded square, as before this step) brings it back.
- Delete... refuses: there is no authority command that removes a square's
  files.
- Escape typed in the Find box itself is the text field's (it lets go of the
  field); Escape clears Find from the well.

## Workbench steps 6.2 to 6.4 (2026-11-05)

The integration of seven slices onto v3-workbench 43c1a4ca, merged with
`--no-ff` in this order: the native fractional-scale bridge (closes 6.0b),
the catalog index as a loc finder (6.2, server), the Catalog as a loc finder
(6.2, client), wall runs / marquee / select same / Fit per Client (6.3 part
1), the gizmo / level moves / Alt-duplicate / stamps (6.3 part 2), the
picker's drawing and loading (6.4 part 1) and its navigation and panes (6.4
part 2). Each slice's own section above says what it built; this one says
what landed together, how the seams were joined and what was run.

What landed:

- **6.0b closed: the native chrome draws the whole scale.**
  `UITreeSceneBridge_SetChromeScaleQ` / `_ChromeScaleQ` (the old
  `SetChromeScale(s)` is `SetChromeScaleQ(4 s)`) run
  `ToriRSChromeBake_Prepare(q)`, the client's one bake. At q = 4, 8, 12 the
  faces are the baked tiers under their old ids; at any other q each face is
  the bake's hinted atlas under `UITREE_SCENE_DEBUG_FONT_Q_ID(slot, q)`,
  built once per q and unloaded (FONT_UNLOAD) when the scale leaves it. The
  skin entry `UITREE_SCENE_CHROME_SKIN_ID` holds `ToriRSChromeBake_Skin(slot,
  q)` at each slot's own size, replaced in place; a component naming a
  chrome sprite in interface pixels gets `UITREE_SCENE_CHROME_SKIN_1X_ID`.
  `App_SetChromeScaleQ` sets both chrome models, the bridge, the font ids
  and the parchment step; `main_chrome_scale_apply` passes q straight to it.
  `ToriDraw_Font.coverage`, Soft3D's byte-blended glyphs, GL3 text mode 2
  and `ToriDraw_SceneFontRemove` draw it. Photographs (Soft3D, headless,
  editor manifest over an empty cache and content dir; scratch files of the
  scale-bridge slice, not committed): q5.png, q6.png / q6_crop.png, q7.png /
  q7_crop.png, q8.png. At 1.5x and 1.75x the stems are whole pixels (2 px)
  and the parchment tiles 132x90; the log reads `chrome: scale q=6 (150%),
  bridge q=6, tile 132x90`; 1x is byte-identical to the base binary.
- **6.2, the loc finder.** Server: prefix-narrowing filter values, `shape=`
  classes and numbers resolved against the placeable shapes, the derived loc
  fields `shapes`, `size`, `layer`, the scoped "all" query
  (`TORIRSMAPED_CATALOG_PAGE_SCOPED`), the sliced warm build and
  `TORIRSMAPED_CATALOG_PROGRESS` / `TORIRSMAPED_CAP_CATALOG_PROGRESS` (16)
  with its `page_flags, done, total` tail. Client: the cells list (16 px
  thumbnail, name and dim display name, shape keycaps, id), the continuous
  window and its prefetch, filter and shortcut chips (`edpanel_chips`),
  Sort, favourites in the TEXT document `editor:favourites.txt`
  (`editor/favourites.txt`, `favourite <kind>:<name> [| shape S rot R]`,
  editor_favourites.h), the shape-key pick, Shift+I (Pick from world) and
  the Preview that follows the pick or the primary selection.
- **6.3, placement feel.** Wall runs and centrepiece runs on a Place drag
  (one undo step), the marquee with Shift / Ctrl / Alt, select same
  (double-click, Alt+A), Fit in STATE_TOOLS (15 values: every window of a
  Client must run the same build), refusals said in the status bar; the
  gizmo (`LocEditorGizmo_*`), `edit.nudge_far_*` (Shift+arrows) and
  `edit.level_up` / `edit.level_down` (Ctrl+PgUp / PgDn,
  `Editor_LocOpsLevelMove`, all or nothing), Alt-duplicate, the Locs panel
  as a selection inspector, and stamps: TEXT documents `stamp:<name>`
  (`editor/stamps/<name>.stamp`) and `stamps:index`
  (`editor/stamps/index.txt`), codec editor_stamps.{h,c}, ops
  edpanel_ops_stamps.{h,c}, the Stamps panel (edpanel_stamps.c), the
  multi-piece ghost `MapEditorPieceGhost`.
- **6.4, the picker.** `Editor_PickerLodAt` (under 12 / 12-40 / 40-96 / 96+
  px a square), the 2600-item budget with the marks' reserve and counted
  drops (`Editor_Picker.dropped`, `drop_text`), visible-first loads,
  instance space (`TORIRSMAPED_INSTANCE_*`, `TORIRSMAPED_SQUARE_INSTANCE`,
  the "Instance space" page `EDITOR_PICKER_TAB_INSTANCE`), atlas rows ending
  in `u32 loc_count` (gated by a capability bit since the review fixes below),
  GRAPH images on
  the web (`graph.image`); then Find (edpanel_picker_find), context K and
  its eight Picker rows (`Editor_HotkeysMatch`), the open-set strip, the
  detail pane and the Area dropdown's lane rows.

Seams joined at the integration:

- **The Catalog reads the server's derived fields.**
  `Editor_CatalogRowShapeMask` takes the `shapes` field when the row has it
  (the server already applied the Locs panel's rule), `Editor_CatalogRowSize`
  takes `size` ("WxL"), and `Editor_CatalogView.layer` carries `layer`,
  which the row tooltip says ("door 700, 1x1 wall"). A shape-number chip now
  composes as `shape=N` (it was `shape=N,*`, a prefix of the `shapeK=` value,
  which the server's `shape=` resolution would not read): `shape=wall`,
  `shape=c` and `shape=2` all resolve server-side.
- **The partial index reaches the panel.** `EditorHost_CatalogPage` gains
  `page_flags`, `progress_done`, `progress_total`.
  `Editor_HostMapEdCatalogQuery` sends `TORIRSMAPED_CATALOG_PROGRESS` only to
  a server with `TORIRSMAPED_CAP_CATALOG_PROGRESS` (it drops the flag
  otherwise, so the answer is the v2 one) and reads the three-u32 tail. The
  Catalog model asks with the flag; `Editor_CatalogModelPartial` says the
  held answer came from the partial index, the count reads "1 to 12 of 40
  loc so far, index 25% read", and Refresh asks the page in view again every
  `EDITOR_CATALOG_PARTIAL_REASK_MS` (250 ms) until a whole answer lands (a
  re-ask replaces every held page but keeps the list's scroll and
  selection). A scoped total reads "212+".
- **Fit is the shared bit everywhere.** The Preview's Fit chip (6.2) calls
  the new `edp_place_fit_enabled(ui)` / `edp_place_fit_set(ui, on)` of 6.3,
  so the Catalog's pane, the Locs panel and the G key show and set the same
  STATE_TOOLS value.
- **The Stamps row sits in the reworked Catalog**, between Kind and Find
  ("Stamps..." opens the Stamps panel); a press with an armed stamp under
  the Place tool no longer opens a wall / centrepiece run
  (`Editor_PanelRunBegin` defers to the stamp, whose drop is one piece).
  The gizmo is hit-tested before the run and marquee press, and a gizmo
  drag holds the pointer ahead of the stroke / rect / run / marquee release.
- **Navigation uses the drawing's instance page.** The Area dropdown lists
  "Instance space" once, as the picker's own tab (it listed a second row of
  its own before), and `instance:` / that row show
  `Editor_PickerInstanceTab`; the nav's own framing stays only for a picker
  with no atlas. `EDITOR_PICKER_INSTANCE_*` are now the catalog's
  `TORIRSMAPED_INSTANCE_*`. The Squares readout's tooltip says what the
  budget dropped ("not drawn: 340 cells, 12 map icons").
- docs/MAPED_HOTKEYS.md lists `edit.select_same`, the far nudges, the level
  moves and `place.constrain` together (the doc test passes).
- The 6.0b open rows "EnsureDebugFont / EnsureChromeSkin still load the
  integer tiers" and "the host does not yet call ToriRSChromeBake_SetScaleQ"
  are closed (marked in that section).

Tests (this container, build_opt unless said; all content-free):

- `make -C src all` (client, torirsmaped, torirsmapedctl) builds.
- New with the slices: test-scene-bridge-chrome 130 PASS;
  test-catalog-finder 176, test-catalog-perf 133 (synthetic half; the
  real-tree half skips), test-editor-catalog-finder 149 (140 + 9 for the
  seams: the derived fields, the count's partial and scoped wording, the
  PROGRESS tail read from the embedded server); test-editor-placement 118;
  test-editor-stamps 91, test-editor-stamps-ops 81; test-editor-worldmap
  161; test-editor-picker 397 (396 + the instance page) -- all 0 failed.
- test-editor-hotkeys 2005 (doc test included), test-editor-chords 591,
  test-editor-groups 2190, test-editor-cmd 66, test-editor-dropdown 36,
  test-editor-palette 97, test-editor-panels-palette 50,
  test-editor-panels-accounts 48, test-editor-areas 136,
  test-editor-world-gesture 42 -- all 0 failed.
- test-chrome-widgets 946, 0 failed, DOM test ok; test-chrome-skin-tiers 922
  and `chrome-skin` pass; test-debug-overlay-visual passes (the 1x / 2x / 3x
  chrome goldens match); test-uitree passes (`ulimit -s unlimited`);
  test-font-markup, test-cache-trim, test-chrome-browser-exec,
  test-chrome-web-skin, check-app-boundary, check-tree-walks and
  check-pt-switch pass; test-world-builder passes (its cache half skips).
  The node tests of test-web-channel pass except web/test/chrome_layout_test.js
  (`src/Makefile`, as before).
- test-editor-workbench 284, 0 failed, after its three registry digests were
  recorded again (1x 1751176799 -> 2131301703, 2x 2175812620 -> 3539872907,
  3x 3489523192 -> 3420398390). Checked slice by slice: the catalog-rows tip
  alone moves 1x and 2x, the gizmo-stamps tip alone moves all three (its 3x
  is exactly the merged 3x), picker-atlas leaves them; the scene digests (the
  chrome's draw path) are unchanged at every scale.
- With asserts on (OPT=0, a scratch objdir): test-editor-catalog-finder,
  -picker, -stamps-ops and -placement pass.
- Not run here (no OSRS-Content, no dat2 cache): test-editor-panels (builds
  and links; the copy of its squares fails), test-edit-world (builds; no
  cache), test-catalog and test-editor-jm2, the real-tree half of
  test-catalog-perf, maped_embed_test, check-scan-meter (boots the real
  client), the web build (no emcc), and the native client by eye.

Perf (the catalog slice's synthetic tree, 63,003 records; 'all' before ->
after the scoping): '' 4.5 ms / 63,003 tested -> 0.08 ms / 100; 'o' 5.2 ->
0.08 ms; 'oa' 2.6 -> 0.11 ms / 694; 'door' 1.9 -> 0.15 ms / 1,277; '700' 2.2
-> 1.9 ms / 50,702; 'op=Chop' (nothing matches) ~11 -> ~11 ms / 63,003, no
gain: scoped or not, every record is tested (review fix: the "~22 ->" figure
was not this test's before/after; see the Review fixes below). Warm build:
the longest 2 ms slice 27 ms -> ~3-4 ms; with 256 KB parts 24-27 parts,
3,005 sources merged one at a time, ~26 partial answers (slowest ~3.6-4.8
ms). A sweep that catches an edit still runs one ~21-26 ms full merge. The
picker's 33 ms cold-open check was not answered (no real frames here).

Open (beyond each slice's own list above):

- The partial re-ask keeps the list's selection by row index: a row picked
  while the index was still loading can name another record once a whole
  answer lands. CATALOG_RECORD and CATALOG_SQUARES still finish the warm
  build first.
- `spawn=yes` finds nothing (the server's catalog has no spawn field);
  'most placed' is not offered; sort applies only once every match is held.
- The Stamps opener is a row ("Stamps..."), not a shortcut chip, and the
  Locs panel keeps its own shape strip (not yet `edpanel_chips`).
- The Squares readout's drop tooltip has no test; the picker's design
  colours (white dashed camera window, yellow open-here) are not applied,
  and the focused square is the selection outline drawn white.
- Native 2x / 3x draw the derived GLYPH skin tiers, so the flattened marks
  (ToolDelete, ToolPlace, the check and radio marks) now show natively; the
  web keeps `skinTiers: 1`. GL3 text mode 2 is unverified on hardware.
  torirs_chrome_surface_sdl.c (the plugin window's SDL surface) still draws
  the 1x skin at an integer scale.
- ~~Editor layout computed as `ToriRSChrome_Scale(ui) * N` sits tight at
  fractional q (a slider over "Strength Shift+[]" at 1.5x).~~ **Fixed** at
  the 6.5-6.8 integration (see "Workbench steps 6.5 to 6.8").
- ~~src/editor/panels/edpanel_triggers.c:713 (`Editor_Triggers_SetPaths`)
  passes `realpath` a buffer under PATH_MAX~~ **Fixed** with the vector-face
  merge (below): it resolves into a PATH_MAX buffer and asserts the result
  fits the panel's own.
- Wire: STATE_TOOLS has 15 values; client and server must come from the
  same tree for it. (The atlas rows' `loc_count` no longer needs that: see
  the review fixes.)

Review fixes (2026-11-05, the 6.2-6.4 review; each finding checked against
the code and a test before it was changed):

- **The atlas loc counts are gated.** A new client against an older daemon
  (or the reverse) read a square's next field as `loc_count` and dropped the
  whole atlas. Now the server advertises
  `TORIRSMAPED_CAP_CATALOG_SQUARE_LOCS` (32); `Editor_HostMapEdCatalogSquares`
  sends CATALOG_SQUARES a `u32 flags` body (`TORIRSMAPED_CATALOG_SQUARES_LOCS`)
  only to a server with the bit (an older one ends a session that sends a
  body); the server remembers the ask per connection
  (`ToriRSMapEdSession.catalog_square_locs`) and gives that connection, in its
  answer and in every `_CHANGED` broadcast, rows flagged
  `TORIRSMAPED_SQUARE_LOC_COUNT` (128) that end with the count, and every
  other connection the v2 rows (`catalog_atlas_changed` encodes each form at
  most once). The flag rides in the row, so a broadcast that crossed the
  request decodes either way; the client strips it from `flags` and leaves
  `loc_count` 0 without it. HELLO stays version 2. Tests: catalog_perf_test
  reads both forms and refuses an unknown flag or a long body (143);
  editor_catalog_finder_test over the embedded server: the asking connection
  reads m50_50's 2 locs, a peer that never asked decodes its `_CHANGED` in
  the v2 form.
- **op=Chop's perf row restated**: about 11 ms unscoped and 11 ms scoped
  (63,003 tested both ways, three runs 10.8-11.9 ms); "~22 -> 10.6" and
  "22 ms -> 12 ms" were not this test's before/after and no pre-branch run is
  recorded. Fixed above and under Catalog performance; the other rows match.
- **Picker pictures keep their canvas on the web.** `renderGraphImages`
  (runtime-source.js, regenerated runtime.js / runtime-ie8.js with
  `tsc ... --ignoreDeprecations 6.0 --alwaysStrict false`, byte-identical to
  the committed output before the change) assigns `canvas.width` / `height`
  only when the size changed, and skips the redraw when the IMAGE items and
  the pictures they name are the ones on the canvas, so a hover-outline or
  pan frame no longer reallocates a well-sized bitmap. chrome_widgets_dom_test
  counts the side assignments (fails on the old runtime).
- **Chip tooltips on every frame.** `edp_chips_push` reset each chip's
  layout and `edp_chips_end` skips an unchanged strip, so from the second
  frame no chip had a box and every tooltip was "". Push now keeps the
  layout; End clears it only when it lays out again.
- **Chips at 125/150/175 %.** The chrome draws GRAPH items at the integer
  tier (`dbg_draw_graph`, `ui->scale`) but the strip laid out and
  hit-tested at q, and so did the chrome's own GRAPH pointer mapping
  (`dbg_graph_push`, the hover key, `ToriRSChrome_Scroll`): at q=6 a chip past
  the edge was drawn and clipped and a press or hover named another item.
  Both now use the draw's factor (`chips_unscale`; `dbg_graph_q` in the
  chrome), which every other GRAPH host already assumed
  (`edpanel_squares.c`); integer scales are unchanged (workbench digests
  equal). The first chip on the last line is now elided to leave the "+n"
  chip its room when more follow (it could push "+n" past the edge at any
  scale). editor_catalog_finder_test `test_chip_strip` at q=4 and q=6, wide
  and narrow: each drawn chip inside the well, the chrome's GRAPH_HOVER key
  and the strip's tooltip naming the chip under the pointer on three
  consecutive frames (fails on the old edpanel_chips.c).
- **Filters compose with favourites / recent / in square.** A local row now
  answers `category=` and `size=` ("WxL") the way the server's
  `cat_value_match` does (prefix of a whole word, a number not stopping
  inside a longer one), `shape=` with class prefixes and `*`
  (`shape=c` is centre), `name=` and `id=`; what a row does not know yet
  (a favourite not asked: id -1) passes. Keys no local row holds (op,
  multivar, ...) filter nothing and the count says so: "1 to 3 of 3
  favourites; op= not checked in this list". Greying such a chip is not done.
- **Delete says it in the status bar** (design section 7): "Deleted Oak tree
  at 3245,3210 L0; Ctrl+Z undoes it" (the cache name once resident, else
  "loc <id>"), "Deleted N locs; Ctrl+Z undoes it" for a set, from both
  `edp_locs_delete` and `Editor_PanelDeleteLocAt` (the Delete tool and K).
  editor_placement_test `test_delete_said`.
- **The Select tool's empty Shift/Ctrl/Alt click** says "Select refused: no
  loc under the pointer to add, toggle or take out" in the status bar (R8),
  not only the console (app_map_editor.c; no test drives the App click).

Rerun after the fixes (this container, build_opt): `make -C src all`;
test-scene-bridge-chrome 130, test-catalog-finder 176, test-catalog-perf 143,
test-editor-catalog-finder 202, test-editor-placement 123, test-editor-stamps
91, -stamps-ops 81, -worldmap 161, -picker 397, -hotkeys 2005, -chords 591,
-groups 2190, -cmd 66, -dropdown 36, -palette 97, -panels-palette 50,
-panels-accounts 48, -areas 136, -world-gesture 42, test-editor-workbench 284
(digests unchanged) -- all 0 failed; test-chrome-widgets 946 and the DOM
test, test-chrome-skin-tiers 922, chrome-skin, test-debug-overlay-visual,
test-uitree, test-font-markup, test-cache-trim, test-chrome-browser-exec,
test-chrome-web-skin, check-app-boundary, check-tree-walks, check-pt-switch,
test-world-builder pass; test-web-channel as before (only
web/test/chrome_layout_test.js fails, on `src/Makefile`; the tests after it
pass when run by hand). With asserts on (OPT=0, a scratch objdir):
test-editor-catalog-finder, -picker, -stamps-ops and -placement pass.

### Vector face merged (2026-11-05)

`worktree-wf_2e50ba62-bae-7` (the synthesized vector face, c54b2a5d and
682c6fe5) merged into v3-workbench over the 6.0b scale bridge. What the
NATIVE path draws now, per q: at 125, 150, 175, 225, 250 and 275 % the
vector face's atlas (`ToriRSChromeGlyphs_Rasterise(slot, q, smooth)`: the
outline with vertical hinting, 3x horizontal oversampling, stem darkening;
`ToriDraw_Font.coverage` set) under `UITREE_SCENE_DEBUG_FONT_Q_ID(slot, q)`;
at 100, 200 and 300 % the baked bitmap tiers under their historic ids, unless
View > "Text rendering: smooth always" is on, in which case the vector atlas
at that q under the same Q_ID block. The pref reaches the native path through
`App_SetChromeTextSmooth` (App_SetChromeScaleQ's sibling, called by
`main_chrome_scale_apply` every frame and a no-op when nothing moved): both
chrome models' `ToriRSChromeBake_SetTextSmooth`, the bridge's
`UITreeSceneBridge_SetChromeTextSmooth` (a whole q's vector faces dropped by
FONT_UNLOAD when it turns off) and the font ids re-resolved. The bake builds
an atlas once per (face, q) for the process; the bridge copies it once per
registration; the unload / re-register once per q is as before. The View
row reads "Text rendering: smooth always". 1x / 2x / 3x with the pref off
are unchanged: the goldens in src/ui/test/golden are untouched and
test-debug-overlay-visual compares them byte for byte. Tests:
test-scene-bridge-chrome (166) now holds the q 6 registration to
`Rasterise(q 6)` and not the cells, the stems to one profile per row, and
the smooth-always register / drop / no-rebuild / fractional no-op cycle.

Photographs (Soft3D, SDL dummy drivers, `--window 1280x800`, the mapeditor
manifest pointed at an empty dat2 cache (empty main_file_cache.dat2 and
.idx255) and an empty content dir, `TORIRS_PLUGINS=0`,
`TORIRS_PRESENT_BMP_FRAME=90`; scratch, not committed):
editor_scale_1.png, editor_scale_1.5.png, editor_scale_1.75.png,
editor_scale_1.5_panel_crop_2x.png (the Terrain panel, nearest-neighbour)
and editor_scale_1_smooth_always.png (`TORIRS_EDITOR_PREFS` with
`text_rendering = smooth`). At 1.5x the text is the vector face: smooth
bowls on a, e, o, s, and every vertical stem the same profile on every row
(one solid column and a ~60 % one, since the face hints vertically only);
the log reads `chrome: scale q=6 (150%), bridge q=6, tile 132x90`. With the
pref on at 1x it reads `chrome: text rendering smooth always (q=4)` and the
frame's text is antialiased.

**Stems hinted horizontally (2026-11-05).** The note above (one solid
column and a ~60 % one at 1.5x) is fixed: the vector bake carries a stem table
per face and glyph (`tools/chrome_font_vector.py` `face_stems`, emitted beside
the unchanged TTF bytes in `src/engine/torirs_chrome_font_vector_baked.c`:
Small 98 stems, Body 110, Menu 108), and
`ToriRSChromeGlyphs_RasteriseVector` warps each glyph's flattened outline in
x so every stem's left edge is a whole device pixel and its width
max(1, round(q / 4 x width)), linear between the stems, ending at the
advance (docs/CHROME_SCALE.md, "Horizontal hinting"). The stem-darkening
gamma (1/1.45) became a light one (1/1.2) for the bowls and diagonals.
Tests: test-chrome-widgets (1099) reads every stem of every Small, Body and
Menu glyph at q 5, 6, 7, 9, 10, 11 back from the atlas -- its columns 255
over the rows where the outline is its rectangle, the column beside it under
40 where the 1x glyph has a counter there, its width the rounded one, never
shrunk -- and builds each atlas twice to the same bytes; test-scene-bridge-
chrome (187) holds the registered q 5..7 faces' l and I (Body, Small, Menu)
to exactly their rounded width of 255 columns and nothing grey beside them.
Both fail with the stem knots taken out of the warp (checked in a scratch
copy). 1x / 2x / 3x and the goldens are unchanged (the vector path is off at
a whole q unless "smooth always"); `chrome-font-vector` reproduces. The
photographs (scratch): the Catalog panel through the test rasteriser at
q 5, 6, 7, before and after, and the native Soft3D recipe above at
TORIRS_CHROME_SCALE=1.5. Open: the web pages draw the same TTFs through the
browser's rasteriser, which knows nothing of the stem table; a hinted web
face would need the table baked into the TTF as TrueType instructions, which
this slice did not do. Short stem rectangles (one 1x pixel, the arms of {
and }) hold no whole device row at 1.25x, so the test reads them from 1.5x.

The editor's boot with a content dir no longer aborts:
`Editor_Triggers_SetPaths` (edpanel_triggers.c) resolved `realpath` into
the panel's 1024-byte buffer, which _FORTIFY_SOURCE stops as an overflow; it
now resolves into a PATH_MAX buffer, keeps the given path when the root does
not exist yet, and asserts the result fits.

## Workbench step 6.5, part 1: scenes and the Lanes panel (date 2026-11-06)

The first half of build-order step 6.5 ("Scenes, scratch lanes, instance
helper"; design section 12, "Lanes, scenes, stamps"). The instance helper
(section 13) is part 2's.

What landed:

- **The SCENES document** (src/editor/editor_scenes.{h,c}, pure; EDITOR_DOC
  kind `EDITOR_DOC_SCENES` = 7, key "scenes", `<content_dir>/editor/scenes.txt`;
  commands `EDITOR_CMD_SCENE_SET` / `_REMOVE` = 16 / 17, riding in `u.area`
  as the AREAS and PALETTES commands do, so the v2 wire form is theirs). The
  grammar is section 12's: `scene <name> | squares <key>... | camera
  <x>,<z>,<level>,<yaw>,<pitch>,<zoom>`, `| level <0..3|auto> [above] | lane
  <name> | preview <var>=<value>... | account <name> at <x>,<z>,<level>`, `|
  note <text>`. A scene is the union of its lines; each segment kind is held
  by at most one line of a scene (a bare `scene <name>` header at most once),
  which is the key a command finds a line by. The camera's x / z are tiles
  with a fraction held as fine units (128 a tile) and written with the fewest
  decimals that read back exactly (3222.5; at most seven). Line-preserving
  like the palettes: parse -> emit byte-identical, a new line of a scene goes
  after its last, a removed one comes back at its index, the four header
  lines come with the first record and go with the last. The editor writes a
  scene as up to three lines (where it looks, what it pins, the note).
  Plans: `Editor_ScenePlanSave` (new: lines at the end; existing: its first
  line keeps its place, a line already as the editor writes it is kept, the
  rest are rewritten after the first; nothing to do is 0),
  `Editor_ScenePlanRename`, `Editor_ScenePlanDelete`, each applied as ONE
  stroke. `Editor_SceneMerge` / `_Clear` are a partial edit's halves.
  Shared by the Client through the authority: editor.c opens it with the
  AREAS and PALETTES documents in every window.
- **The Scenes panel** (src/editor/panels/edpanel_scenes.c, module "scenes",
  View > Scenes, hidden until opened; `TORIRS_EDITOR_SCENES=show` for
  photographs): "editor/scenes.txt | 5 scenes", Find over a VLIST (R3) of
  "name | m50_50 m50_51 | L1 above | working", the Name box, the Holds chips
  (squares, camera, level, var preview, account, lane: what Save and Update
  capture; an `edpanel_chips` strip, so the DOM draws it as the GRAPH it
  is), Save scene (wearing Ctrl+Shift+K), Enter, Update, Rename, Delete. A
  click picks a row (its name to Name); a second activation of the picked
  row (Enter, a double-click) enters it.
  - **Save scene** (`Editor_Scenes_Save`, Ctrl+Shift+K `file.save_scene`):
    the window's state -- the squares loaded in its mirror, the World's
    camera (`app_services.world_camera`), `edit_level` and Above, the lane
    (`ui->lane`), STATE_VAR_PREVIEW as last heard (decoded to `varp<id>` /
    `varb<id>`), the Live model's play account and tile -- as a new scene
    named in Name, else `scene-<n>`. Refused, saying why, for a taken or bad
    name, a read-only session, a line too long (never cut). **Update**
    rewrites the picked scene from the held parts the window has (the rest
    stay); **Rename** takes Name; **Delete**. Each verb is ONE undo step
    named by the verb ("Save scene smithy", "Update scene smithy", "Rename
    scene smithy to forge", "Delete scene forge") and the document is saved
    at once: a scene is session state and survives a restart without Save
    all (an undo leaves the document dirty, as a stamp's does). Saving never
    bakes.
  - **Enter** (`Editor_Scenes_Enter`): through the save guard
    (`EDITOR_UI_GUARD_ENTER_SCENE`, the modal says "before entering scene
    X") when the scene has squares, then `Editor_Scenes_EnterNow`: opens its
    squares (the first replacing the open set, the others added; a lane
    storage resolved to the picker's index), pins the level and Above
    (`edp_level_set`, `edp_level_show_above`, Above re-asserted for three
    ticks because the menu bar follows a pin made behind its back from the
    old cap first), publishes STATE_VAR_PREVIEW from its preview list
    (`Builder_PreviewEncode`), switches the lane when it names another
    (`Editor_Lanes_Switch`: staged, see below), teleports its account
    through the live link when the lane is attached (new
    `Editor_Live_Teleport`, the Live panel's `::tele`; otherwise "bob not
    moved (the live lane is down)"), and puts the World's camera at its
    orbit pose through the new `app_services.world_camera_set`
    (`EditorView_AppWorldCameraSet`, app_editor_view.c's `view_apply_to_app`)
    once the world is not busy and every square of the scene is loaded in
    the mirror (15 s, then a status sentence). One status sentence says what
    it did: "Entered smithy: 2 squares, level 1 above, preview of 1 var,
    camera".
  - **The command palette's hook**: `Editor_ScenesList(ui, out)` fills
    `struct Editor_SceneList` (name, summary; 128 rows, `more` past them)
    from the mirror's document, panel or not; Enter on a row is
    `Editor_Scenes_Enter(ui, row->name)`.
- **The Lanes panel** (edpanel_publish.c widened; module still "publish" so
  saved layouts and File > Publish... keep working, title "Lanes"): "Lane:
  working | 3 changed", its base, Find over a VLIST of every lane with its
  change count and, for a tree another editor holds, the holder ("main | the
  tree | locked: pid 43893"), the M / A / D chips filtering the change list
  (lit: shown), the change list, Publish to... (wearing Ctrl+Shift+U),
  Scratch lane (Ctrl+Shift+N), Switch... (the lane picked in the list),
  Discard... (a second press within 4 s confirms; the first says what it will
  drop), New lane, Refresh. Refusals go through `Editor_UiStatus`.
- **Lane switching is staged.** The daemon cannot re-point its lane without a
  restart: its tree lock, documents and undo history, catalog, live dir
  (`TORIRSMAPED_LIVE_DIR`) and launched servers are the lane's, every session
  mirrors them, and `ToriRSMapEd_Open` memsets the whole server. So
  `LANE_SWITCH` (request 87) writes `<repo>/build/lanes/lane.ini` (`lane =
  <name>`, `ToriRSMapEdLanes_SetBoot`: the lanes directory's own descriptor,
  which no lane name can be), and every boot reads it
  (`ToriRSMapEdLanes_BootLane`, in torirsmaped's main and in
  `ToriRSMapEd_EmbedStartLane`): the lane it names wins over the manifest's
  `lane=` / `--lane`, the log says so ("lane 'x' (build/lanes/lane.ini,
  switched in the editor; the boot asked for 'working')"), a lane it names
  that is gone falls back to the boot's with a line. It persists until the
  next switch (switching back writes the boot's lane; deleting the file
  restores the manifest's rule). The answer is FACT_LANE_PUBLISHED kind 4
  (`TORIRSMAPED_LANE_PUBLISHED_SWITCH`); the status bar says "Switched to X
  for the next editor boot (build/lanes/lane.ini); this session stays on Y: a
  running server cannot re-point its lane".
- **Scratch lane** (`Editor_Lanes_Scratch`, Ctrl+Shift+N
  `file.scratch_lane`): asks the lane list afresh, names the lane
  `scratch-<yymmdd><letter>` (`Editor_LanesScratchName`, pure: the first
  letter a..z no lane holds, local date; 0 when the day's 26 exist), makes
  it from main through LANE_CREATE, and on its answer switches to it
  (staged). The panel then lists it with its count.
- **The lane chip.** `ui->lane` (`struct EditorUiLane`: name, is_main,
  writable, change count, next boot, serial) is kept by the Lanes module in
  every window whether its panel shows or not: one LANE_INFO on the first
  frame, then the count alone -- LANE_CHANGES with a `u32 flags` body,
  `TORIRSMAPED_LANE_CHANGES_COUNT_ONLY`, answered by the new FACT_LANE_COUNT
  (205: name, is_main, modified, added, deleted, next boot) -- every
  `EDITOR_UI_LANE_COUNT_MS` (15 s), after a save (the document going
  clean) and after a publish, discard, create or switch. The status bar
  (edpanel_statusbar.c) shows `Editor_Lanes_ChipText`, "working | 3
  changed", "working | clean", "main", with " | next: <lane>" while a switch
  waits, at its right end (whole, else the lane name alone, else not at
  all); its tooltip says what a click does or what the next boot serves, and
  a click opens the Lanes panel (`Editor_Lanes_Show`). The baked face has no
  middle dot, so the design's "working · 3 changed" is spelled with " | ",
  the bar's own separator.
- **Wire, gated**: `TORIRSMAPED_CAP_LANES_V2` (64) in FACT_HELLO. FACT_LANE_INFO
  ends with `string next_boot`; FACT_LANE_LIST ends with `u32 n, n * (i32
  changes, string held)` (each non-main lane's count is one changes scan, on
  the ask only); a client sends LANE_SWITCH or a LANE_CHANGES body only to a
  server with the bit (an older one ends a session on either) and reads both
  tails as optional.
- **torirsmapedctl**: `scenes`, `scene-set <scene> [squares ...] [camera
  ...] [level <n|auto> [above]] [lane ...] [preview <var>=<v>...] [account
  <name> at <x>,<z>,<l>] [note ...]` (the segments given replace the scene's
  own, `<segment> -` drops one, one undo step), `scene-set <scene> rename
  <new>`, `scene-remove <scene>`, `lane-switch <name>`; `lane` prints the next
  boot's lane. The doc-kind and command-kind name tables grew
  ("scenes", "sceneset", "scenerm").
- **Hotkeys** (editor_hotkeys.{h,c}, docs/MAPED_HOTKEYS.md): `file.save_scene`
  Ctrl+Shift+K and `file.scratch_lane` Ctrl+Shift+N (File, context G; both
  File menu rows), `view.scenes` (unbound, panel "scenes"). The table has no
  conflict with them (the hotkeys test's conflict pass).

Tests (2026-11-06, this container, objects in build_opt):

- **test-editor-scenes** (new, pure) 507 checks, 0 failed: the design's
  example and hand-written shapes byte-identical, 22 refusals at their line,
  every fine unit 0..383 written and read back, the editor's three lines,
  a too-long scene refused not cut, Save into an empty file (the header) and
  beside others, Update in place (the first line kept, an unchanged line
  kept), a hand-written scene's update, Rename, Delete (the last scene takes
  the header), each applied and undone byte for byte, stale and clashing
  commands refused, the History labels, both kinds over the v2 wire.
- **test-editor-scenes-ops** (new, content-free: an embedded ToriRSMapEd on
  lane `work` of a synthetic tree, a repo root with build/lanes, two windows
  of one Client with the full registry and a faked App) 80 checks, 0
  failed: Save scene (both lines, the History row, the status sentence, the
  file in the lane's tree at once, the base untouched, one undo / redo, a
  taken and a bad name refused); the second window lists it through
  `Editor_ScenesList` and enters it (its squares opened, level 1 Above, the
  status sentence, no camera while its world is busy then the exact pose,
  STATE_VAR_PREVIEW heard by the first window, an account not moved while
  the live lane is down, an unknown scene refused); Update (first line in
  place, nothing-to-do said), Rename, Delete, their undos; Ctrl+Shift+K's
  `scene-1`; the Save button's keycap; the list's click, then Enter through
  the save guard's modal ("before entering scene smithy") and Don't save;
  the editor closed and reopened on the lane: the scene there and entered to
  the same squares, camera, level, Above and preview; the scratch name rule
  (a, the first free letter, all 26 taken, the next day); the status bar's
  chip "work | 1 changed" (the lane's own scenes.txt is its change; the
  count is the lane module's own) and its click opening the Lanes panel;
  Ctrl+Shift+N making scratch-<today>a and staging it in
  build/lanes/lane.ini, the session staying on work, the chip "work | 1
  changed | next: scratch-<today>a", the Lanes list's three rows with
  their counts; and the next boot serving the scratch lane ("... | clean",
  no scenes in it: it was made from main).
- test-lanes 76 (63 + 13: the boot lane, SetBoot's refusals and its file, the
  list ignoring build/lanes/lane.ini, back to main, a vanished lane), 0
  failed.
- test-editor-hotkeys 2036 (the doc test included), test-editor-chords 591,
  test-editor-groups 2190, test-editor-workbench 295 (the registry digests
  unchanged: the new panel starts hidden), test-editor-cmd 66,
  test-editor-palette 97, test-editor-areas 136, test-editor-stamps 91,
  test-editor-stamps-ops 81, test-editor-panels-palette 50,
  test-editor-panels-accounts 48 (`ulimit -s unlimited`),
  test-editor-catalog-finder 202, test-editor-placement 123,
  test-editor-picker 397, test-editor-world-gesture 42,
  test-editor-worldmap 161, test-catalog-finder 176, test-catalog-perf 143
  (synthetic half), test-chrome-widgets 1009, test-editor-ghost OK -- all 0
  failed; check-tree-walks and check-pt-switch PASS; `make -C src all`
  links.
- Each new check was seen to fail on a mutant in this private worktree (the
  file restored and its object deleted after each): an Update keeping no unchanged line (4), no segment-kind clash (4), the
  camera's fraction dropped (every fractional fine unit), Enter without its
  Above follow-up (5), the camera put while the world is busy (1), no save at
  once (1), no lane chip (5), the scratch lane made but not switched (6), the
  embedded server ignoring build/lanes/lane.ini (3), no LANE_LIST tail (1),
  Enter skipping the save guard (1), BootLane ignoring the file (3 in
  test-lanes), the Save scene row missing from MAPED_HOTKEYS.md (1 in
  test-editor-hotkeys).
- torirsmapedctl against a real daemon on a synthetic tree (`--lane work`,
  a scratch repo root; by hand, not a make target): scene-set (save, a
  partial update with `note -`, rename, an unknown segment refused), scenes,
  save writing the lane's editor/scenes.txt, scene-remove (and again,
  refused), lane-create, lane-switch (staged, the sentence; an unknown lane
  refused), `lane` naming the next boot, and the restarted daemon serving
  scratch-x with its log line.
- Could not run here (no OSRS-Content): test-editor-cfg (3 failed),
  test-editor-alloc (133), test-editor-vars (5), test-editor-if (2) and
  test-editor-script (asserts in Builder_SscStage) fail with the same counts
  on the base commit 40ca3878 (checked in a scratch worktree); test-builder
  does not link here (pow / floor: its recipe links no `-lm`, as
  torirsmaped's did in 6.0); test-editor-views, test-editor-panels,
  test-torirsmaped-embed, test-torirsmaped-daemon, test-editor-jm2,
  test-editor-save need the content tree's squares. No photograph: there is
  no running client with content here.

Open:

- The changes count is a scan of the lane's mirror (`ToriRSMapEdLanes_Changes`,
  an lstat walk) done synchronously in the server's handler; with the
  embedded server that is the client's main thread, every 15 s and after a
  save. It is cheap on the synthetic tree; on the real OSRS-Content mirror it
  is unmeasured and may exceed a frame -- the next step is a server-side
  cache invalidated by the saves the server itself makes, or a BUILD-style
  job.
- Switching is staged, never live; Enter's lane switch and Scratch lane say
  so. A live re-point (close the documents, re-open the tree, move the live
  dir, tell every session to reload) is not built.
- Enter puts the camera through `world_camera_set` only in a window that
  shows a world; a panel-only window relays its squares (STATE_OPEN_SQUARE)
  but not the pose. The pose read back by `world_camera` is the App's
  reconstruction (`EditorView_PoseFromEye` with the previous zoom), so a
  scene's zoom is the reconstruction's.
- The command palette itself is 6.8's: only its hook exists. The Scenes list
  rows are "name | summary" text, not the design's two-column rows with the
  age ("now", "2 h").
- Holds is a chip strip (as the Catalog's filters), not the design's
  checkbox row; Note has no box in the panel (ctl's `scene-set ... note`
  writes one; Update keeps it).
- A scene's account is the Live model's play account and tile only when the
  live link reported them; there is no picker for another account.
- The status bar's RUN chip and the design's lane chip glyph are not built.

## Workbench step 6.5, part 2: the instance helper (date 2026-11-06)

Build-order step 6.5's instance half (design section 13 "Instance helper";
section 5 "New, Copy, Instance"; section 2 R2, R8). Scenes, scratch lanes and
the Lanes panel are the other part's.

What landed:

- **The planner** (src/editor/editor_instance.{h,c}, pure). A plan is up to
  16 source squares (full keys, storage kept), ordered south-west first (z,
  then x), their bounding box, the levels copied and the destination box.
  `Editor_InstanceFindFree` scans instance space (0..19 x 100..255, equal to
  `TORIRSMAPED_INSTANCE_*`, statically asserted) for the first w x h box with
  no square of ANY storage in it, from the lowest coordinates: z rows from
  100 upward, x from 0 within a row; `Editor_InstanceFreeCount` says how
  many squares are free. Rebasing moves a key, an absolute tile, a spawn row
  (a suppression row, a row outside every source square or on a level not
  copied is dropped) by the whole-square offset, so a tile keeps its local
  x, z and level. `Editor_InstanceCopyCommands` turns a DOC_CREATE'd square
  into what the Copy row asked: with terrain the COPY template, then the
  other levels' tiles cleared and (locs off, or on another level) the locs
  removed; without terrain the EMPTY template and the source's locs added on
  the copied levels. Coords both ways ("0_16_101_32_32", "x z [level]").
  The trigger text (`Editor_InstanceTriggerText`): a header naming the file
  and its writer, `[oploc1,<loc>]`, `p_telejump(<coord>);` -- or, for the
  runtime entry, `map_instance_alloc(w*8, h*8)`, a setchunk per zone per
  level from the destination's own static squares, `map_instance_build`,
  `p_telejump(map_instance_coord(...))` (at most 2x2 squares, the
  commands' 16x16 zones).
- **The .wma codec** (src/editor/editor_wma.{h,c}; TEXT key "wma:<name>",
  `worldmap/areas/<name>.wma`, in `Editor_DocKeyValid` and
  `Editor_HostDocPath`). The shape the catalog's atlas reader parses
  (`cat_parse_worldmap`: `[name]`, `external=`, `main=yes|no`,
  `section<N>=` 18 integers whose type-0 source rectangle claims squares) in
  the field order cachepack writes (`worldmap_emit_area`). Line-preserving:
  parse -> emit byte-identical (comments, CRLF, no final newline); refused
  with its line: a header without a name or named twice, a section that is
  not exactly 18 integers, `main=` neither yes nor no. `Editor_WmaNewAreaText`
  writes a new area: a header comment naming the writer, the block, one
  type-0 section claiming and drawn at the destination box, the origin the
  box's middle tile packed.
- **One undo step for DOC_CREATE + the rest.** DOC_CREATE was always its own
  undo step ("whatever stroke a client has open"). Its template word now
  takes `TORIRSMAPED_DOC_CREATE_IN_STROKE` (0x100), sent only to a server
  with the new `TORIRSMAPED_CAP_DOC_CREATE_STROKE` (128, set by every
  ToriRSMapEd built from this tree; an older server refuses the template, it
  does not drop the session): the fill then joins the open stroke.
  `EditorHost_DocCreate.join_stroke`; `Editor_CreateSquare` asserts the
  capability and an open stroke, and keeps the History row the caller's.
- **The dialog** (src/editor/panels/edpanel_instance.{h,c}, module
  "instance", a modal registered after New square). The picker's detail pane
  Instance... now opens it (the stub's body replaced; the stand-in that
  opened New square is gone). Rows: Source (Picker selection (N): the
  shift-clicked squares, else the focus; Open set (N); "collection: <name>"
  per AREAS collection) and the keys it names; Copy terrain / locs / spawns /
  named areas; Levels (0..3, or one); Destination (typed "m16_101", "16
  101") and Find free..., which types the box's south-west square, says
  "instance space 0..19 x 100..255 has N free" and marks the box in the
  picker (`Editor_Picker.mark_*`, outlined at every zoom in the created
  colour; `Editor_SquarePicker_Mark`, `Editor_SquarePicker_Sources`);
  Stored in (maps/, every lane, New lane...); Name; Map area (add a .wma /
  none); Entry (teleport trigger / runtime map instance / none); Trigger loc;
  Lands at (empty: the first square's middle). Every change re-checks the
  form (`Editor_InstanceValidate`, chrome-free) and the check row says why
  not, or what Create makes ("creates 2 squares at m16_101 in maps/, a .wma,
  a trigger to 0_16_101_32_32; unsaved until Save"); Create is disabled until
  it is good. Refused, with the reason: no source, more than 16, a key named
  twice, no destination, a box past 255, a destination square taken in any
  storage ("m0_100 is taken (ported/testlane/maps, unmapped)"), a lane with
  no name, a name not [a-z0-9_], no loc, a bad or outside landing coord, the
  runtime entry without the capability or past 2x2; at Create, a .wma or a
  trigger file that already has text, a collection of that name, a server
  without DOC_CREATE_STROKE, an open stroke.
- **Create**: ONE stroke, one undo step, one History row ("instance
  <name>"): a DOC_CREATE per square (the copy template, `join_stroke`), the
  trim commands, a SPAWN batch rebased to the new tiles (the edited file's
  rows and the generated file's that no edited suppression removes), the
  AREAS areas lying in one source square moved and named "<name>: <area>"
  (Copy named areas), a collection `<name>` of the new squares (what Find
  "instance:<name>" finds while there is no .wma), the .wma and the .rs2
  (`rs2:edited/<name>/scripts/<name>.rs2`) through `Builder_TextSync` inside
  the stroke (the builders' text path). Nothing is written until Save. A
  refusal mid-way undoes the half-made step whole. The first square opens in
  the world; the picker refreshes and keeps the box marked.
- **Runtime map instance** (design: "when the lane's server reports the
  capability (FACT_HELLO bit)"). The server side exists: `ss_opcode.h`
  11009..11014, `map_instance_alloc/setchunk/build/coord/free/find`, reached
  from content (docs/map_instances.md; `[debugproc,mapinstance]`). What does
  not exist is the report: the live link's CTL_FACT_HELLO carries no
  capability word, so the editor server cannot learn it of the game server
  it attaches to. `TORIRSMAPED_CAP_MAP_INSTANCE` (256) is defined and set by
  nothing; the chip is disabled, its tooltip says "The lane's server reports
  no map instances", picking it anyway says so in the status bar and falls
  back to the teleport. The runtime trigger text is written and tested as
  text, never compiled (no sscompile content here).
- **Scenes**: the SCENES document is not in this base. `instance_scene_hook`
  in edpanel_instance.c is the TODO the integrator fills (a scene "instance:
  <name>" through that document's SET inside the same stroke); until then the
  collection is what finds the instance again.

Tests (2026-11-06, this container, objects in build_opt):

- **test-editor-instance** (new, pure; src/maped/instance.mk) 76 checks, 0
  failed: the free box over synthetic atlases (an empty space, a taken
  square in the way, a full row, a one-square hole, too wide), the free
  count, the south-west order, rebasing (keys with storage, tiles, spawn
  rows, a level range, suppressions, outside rows), coords both ways and
  their refusals, the copy commands for every Copy choice over hand-built
  squares, both trigger texts, the .wma codec (byte-identical round trip,
  six refusals with their lines, a new area parsed back to what it claims).
- **test-editor-instance-ops** (new, content-free: an embedded ToriRSMapEd
  over a synthetic tree it writes -- two squares with locs on two levels, an
  edited and a generated spawn file, a lane square at m0_100, an AREAS file
  with an area and a collection, a .wma already named taken_area) 78 checks,
  0 failed: Instance... over the picker's selection; every refusal above
  through the real chrome (the check row, the disabled Create, the status
  bar); the runtime chip disabled and why; Find free landing beside the
  lane's square at m1_100 and marking it; Create as ONE History group whose
  two squares equal their sources level by level, three spawns rebased, the
  area moved and the spanning one not, the collection, the .wma and the
  trigger texts; nothing on disk; Undo emptying every document; Redo; Save
  writing the files (the .wma byte for byte); the catalog's atlas reader
  over the saved tree putting m1_100 and m1_101 in area tutorial_cave and
  out of instance space; a second instance from the collection, level 0
  only, no locs, no .wma, no trigger, one group, undone.
- Run again, 0 failed: test-editor-picker 398 (its Instance... check now
  asserts the instance helper opens over the focused square, not New
  square), test-editor-stamps-ops 81, test-editor-workbench 295 (the 1x
  registry golden unchanged: the dialog is a hidden modal),
  test-editor-chords 591, test-editor-hotkeys 2015 (no hotkey row added),
  test-editor-cmd 66, test-catalog-perf 143 (synthetic half),
  test-editor-catalog-finder 202, test-editor-areas 136, test-editor-groups
  2190, test-chrome-widgets 1009 + dom ok, test-lanes 63,
  test-editor-panels-accounts 48, test-editor-stamps 91, test-editor-worldmap
  161, test-editor-placement 123; check-pt-switch and check-tree-walks PASS;
  `make all`, torirsmaped and torirsmapedctl link.
- Do not link here, at this base too (checked in a detached worktree of
  40ca3878): test-catalog, test-torirsmaped-embed, test-builder and
  test-live-editor stop at `pow` / `floor` undefined from the accounts module
  (`acct_values_stat`), a missing -lm unrelated to this slice; the
  DOC_CREATE wire change is covered by test-editor-instance-ops' embedded
  authority instead.

Seen to fail before their fix, in a throwaway worktree of this commit with
its own objdir (build_mut), two builds of mutations, removed afterwards:
test-editor-instance 8 of 76 failed under five (the free box checking only
its corner square, no south-west order, a spawn of an uncopied level kept,
the codec always ending with a newline, the uncopied levels not cleared);
test-editor-instance-ops 11 of 78 under five (the server ignoring
DOC_CREATE_IN_STROKE: the mirror's History still shows one row, but Undo
takes back only the last of the authority's groups -- caught by the Undo,
"squares empty again", Redo and the trim's Undo checks; no taken-destination
refusal; Find free not marking the picker; the new area's section a
one-square type: m1_101 not claimed by the reader; the generated spawn file
not read).

Could not run here: test-editor-panels and the content half of
test-catalog-perf (no OSRS-Content checkout); section 16's check for 6.5
("an instance created from two squares packs and the play client teleports
into it on RUN") needs the content tree, the cache and a live lane, none of
which are here; the trigger was not compiled by sscompile; nothing was
photographed.

Open:

- The .wma reaches the catalog's atlas and the picker's Area pages, not the
  game's world map: cachepack packs the details archive from `details.wma`
  (with ids in details.compack), so a separate `<name>.wma` is not packed.
  Merging the block into details.wma under a fresh id is the packer's half.
- The runtime map instance chip waits on a capability word in the live
  link's CTL_FACT_HELLO (a torirsserver control-protocol change).
- The scene "instance: <name>" waits on the SCENES document (the TODO hook).
- The marked box cannot be dragged elsewhere in the picker yet (type the
  destination instead); Create / Cancel wear no keycaps (the chrome's modal
  has no default-button Enter / Escape binding, and no hotkey action was
  added); no torirsmapedctl verb for the .wma document.
- Levels are copied in place or trimmed to one; a trimmed level is not moved
  to level 0.
- Spawns come from the source squares' edited and generated files only; an
  area copies only when each of its rectangles lies in one source square.

## Workbench step 6.7, part 1: the encounter builder (date 2026-11-06)

The first slice of build-order step 6.7 ("Encounters, shops, quests"; design
section 11, "Encounter builder": the Set / Stats / Combat / Drops / Test
tabs). The shop builder and the quest board are part 2's.

What landed:

- **The ENCOUNTERS document** (src/editor/editor_encounters.{h,c}):
  `<content_dir>/editor/encounters.txt`, TEXT key `editor:encounters.txt`
  (`Editor_DocKeyValid` knows it; `Editor_HostDocPath` already maps
  `editor:<file>`). `encounter <name> [| colour <0..7>] [| spawn
  <npc|obj>:<name> <x> <z> <level> [<count>]]... [| note <text>]`, segments
  in any order, spawn repeated, names unique. Line-preserving like AREAS,
  PALETTES and STAMPS: parse -> emit byte-identical (comments, blank lines,
  CRLF, no final newline); a refused line is kept as text and named. A verb
  is the document's new text (`Editor_EncountersTextWith` / `Without`; the
  first record brings the two header lines, removing the last takes them
  back) written as TEXT_SPLICE commands (`Editor_StampSplices`), ONE undo
  step. `Editor_EncounterCentre` (rounded mean, the majority level),
  `Editor_EncounterNpcNames`.
- **The stats ledger codec** (src/editor/editor_npcstats.{h,c}):
  `npc_stats/<shard>/<gameval>.stats` as docs/NPC_WIKI_STATS_PLAN.md section 4
  and tools/gen_npc_stats.py (read_ledger, shard_of, write_ledger) define it,
  TEXT key `stats:<shard>/<gameval>` (`Editor_DocKeyValid` checks the shard
  is the name's; `Editor_HostDocPath` maps it). Parse -> emit byte-identical;
  `Editor_NpcStatsTextSet` rewrites only a value's bytes (the key column and
  a trailing `// note` kept, the note's column too where the width allows),
  adds a missing key after the last stat line before "// Checks" in that
  line's key column, removes a line for "", and sets `source = authored` --
  the pin that makes the generator leave the file. A ledger the generator
  never wrote starts with a header naming its writer.
- **The derived cells** (editor_npcstats.h): `Editor_NpcMaxHit` ((level + 9)
  * (bonus + 64) + 320) / 640 and `Editor_NpcCombatLevel` (ToriRSServer_
  CombatLevel's integers, no prayer). Not "read from torirs_server_combat.c":
  that file says the swing's formulas are content (`skill_combat/
  combat_stats.rs2`, not in this checkout), so the max hit restates the rule
  as the tree's own worked figures state it
  (tools/generate_quest_combat_manifest.py: Sourhog 30 / 0 -> 4, the Arzinian
  Avatar of Ranging 110 / 10 -> 14; the goblin's 1 / -15 -> 1 agrees with the
  wiki), and each cell's tooltip names the proc and the formula (under 64
  bytes, so none is cut). The cache's vislevel is the record's own field
  (SS_OP_NC_VISLEVEL), shown beside the computed level.
- **The panel** (src/editor/panels/edpanel_encounter.c, module "encounter",
  View > Encounters (`view.encounter`, unbound), Spawns' Group...). Header:
  Set, Name, New set / Rename / Delete set / Save set (writes the set file and
  the ledgers the panel edited; records save with File > Save; saving never
  bakes). Set tab: the set's spawns as a VLIST with Find; Add at latched tile
  (a SPAWN and the set's line, one undo step), From Spawns selection, Remove
  from set, Colour; the selected row's npc: respawnrate, wanderrange,
  maxrange, huntmode and the params huntrange, attackrate, damagetype from its
  record's layers (catalog sources -> CFG documents -> `Editor_CfgRecord`, the
  Record panel's path), Apply fields (one undo step, each field planned
  against the edit before it), Record... (the Record panel on it), Show rings.
  A field whose winning layer is GENERATED is written into the record's top
  authored overlay as an override; with no authored overlay the edit is
  refused with a sentence and nothing is written. Stats tab: the six levels
  and thirteen bonus params from the record, the ledger named with its
  source, Apply stats (the ledger with `source = authored` and the record in
  ONE undo step; a generated-only stat goes to the ledger alone, for
  gen_npc_stats.py to carry, and the status says so), the derived cells
  following the inputs as typed. Combat tab: melee (`~npc_meleeattack;`),
  caster (the body of npc/scripts/dark_wizard.rs2's first `[ai_opplayer2]` /
  `[ai_applayer2]`, read from the tree -- refused, never invented, when the
  file is absent), ranger (`~npc_rangeattack;`), healer (the default swing
  then `npc_statheal(hitpoints, 2, 0);`); Open in step builder opens the NPC
  builder on the npc and adds the template as its `[ai_opplayer2]` trigger's
  RAW steps (server/scripts/edited/<npc>/scripts/<npc>.rs2, the builder's own
  file, written by its own commit). Drops tab: READ-ONLY rows -- the tree has
  no drop-table document (docs/NPC_DROPTABLES_TODO.md: drops are the npc's
  `[ai_queue3,<npc>]` RuneScript); `Editor_DropsRead` reads the shape
  tools/wiki_droptable.py writes (Always lines, one `random(<den>)` and its
  cumulative `if ($dropint < k)` walk, the guarded `npc_param(death_drop)`),
  follows a `@label` / `~proc` call in the same file or through the catalog's
  trigger index, counts what it does not recognise, and the row says where
  the rows came from. Test tab: RUN (`Editor_Live_Run`, wearing file.run's
  chord), Place player at set (`Editor_Accounts_PlacePlayer` at the set's
  centre, wearing tool.place_player's), Follow the play account (the World's
  camera to the live model's play tile when it moves), and the fight log: the
  live link's lines naming a hit (the server's `HIT tick=` line) or a hunt,
  else -- the row says so -- its lines naming the set's npcs
  (`Editor_Encounter_FightLog`). Every refusal is an `Editor_UiStatus`
  sentence. Starts closed; opens with the editor again only if it was open.
- **The step builder's ai trigger** (builder_model.{h,c}, builder_emit.c,
  builder_panels.c): `Builder_Trigger.op == BUILDER_OP_AI` with `ai`
  ("ai_opplayer2"); the .interaction INI section `[ai,<trigger>]`; the
  emitter writes `[<trigger>,<npc>]` and its steps after the use-on
  triggers; its name joins `Builder_ScriptTriggerNames` (the conflict check);
  a loc's is an error; the trigger list reads "[ai_opplayer2] combat".
- **Rings** (app_overlay.c, one hunk): `Editor_Encounter_Rings` -- each set's
  wander square washed and hunt square outlined in the set's colour, the
  shown set's brighter, for the npcs whose records the panel holds (no
  catalog request from a draw), drawn like the wander preview.
- **Spawns' Group...** (edpanel_spawns.c: the button, its field and its
  activate line -- three small hunks, not the one asked for: the panel's
  struct is private, so the field and the dispatch need their own): `Editor_Encounter_GroupSelection` opens the panel beside
  Spawns and puts the selection into the shown set (a new `group_<n>` when
  none is shown), one undo step.
- **torirsmapedctl** (src/torirsmaped/torirs_mapedctl_encounters.{h,c}, wired
  in torirs_mapedctl_main.c): `encounters`, `encounter-set <name> [colour
  <0..7|->] [note <text|->] [rename <new>] [clear] [spawn|unspawn
  <npc|obj>:<name> <x> <z> <level> [count]]...`, `encounter-remove <name>`;
  one stroke of splices each, unsaved until `save`. Split out of the main file
  so the test drives the verbs over an embedded server.

Tests (2026-11-06, this container; objects in build_opt, and at OPT=0 with
asserts in build): **test-editor-encounters** (new, pure) 121 checks, 0 failed;
**test-editor-encounter-panel** (new, content-free: an embedded ToriRSMapEd
over a synthetic tree -- an npc with a cache block, a generated and an
authored overlay, its ledger, a death-drop script, a set; the panel's rows,
fields, rings, Apply fields and its refusal, Add at latched tile, Group...,
Colour, the Stats tab's cells and tooltips, Apply stats and its undo byte for
byte, Save set, the Drops rows, the caster's refusal, the melee template
into the NPC builder and its script, the Test tab's chords and the fight
log, and the ctl verbs over a second connection: refusals, undo, save byte
for byte) 91 checks, 0 failed (both builds). Regressions, all 0 failed: test-editor-hotkeys 2020 (the doc
test lists `view.encounter`), test-editor-groups 2190, test-editor-chords 591,
test-editor-stamps 91, test-editor-stamps-ops 81, test-editor-panels-palette
50, test-editor-panels-accounts 48, test-editor-cmd 66, test-editor-areas
136, test-editor-palette 97, test-editor-workbench 295, test-editor-picker
397, test-editor-catalog-finder 202, test-editor-world-gesture 42,
test-editor-ghost OK, test-loc-editor-selection all passed; check-tree-walks
and check-pt-switch PASS; `make all` links (torirsmapedctl with the verbs).
Every new check family was seen to fail on a mutant in a throwaway copy of
src/ and 3rd/ under the session scratchpad, never this tree (each restored
from the commit before the next): the emit always ending in a newline (1),
a taken name not refused (2), the ledger edit keeping the old gap so the note
moves (1), the max hit floored instead of rounded (3), a prayer term in the
combat level (1; the third hand case, 3.975 -> 3, was added because the
first two survived it), the drop walk reading cumulative thresholds as
shares (1), the emitter dropping ai triggers (1), the panel writing a
generated layer (5), Apply fields without its stroke (26), the ctl verb
without its stroke (4).

Could not run here: test-editor-cfg (3 of 110) and test-editor-vars (5 of 48)
fail only their content checks (every configs/all.* file read, the tree's
varbits), as on the base; test-builder (it reads OSRS-Content's all.npc before any
of its content-free checks, and sscompile does not link at OPT=0 without
-lm), test-editor-panels, test-torirsmaped-embed and -daemon (the sample
square), and nothing was photographed (no running client with content): the
rings' look and the panel's layout are unchecked by eye. No content-tree
npc was edited, so the 6.7 check ("an encounter set saved and RUN spawns the
npcs with the edited huntrange; the fight log shows the hunt") is not run:
it needs the content and a lane.

Open:

- The design's "DPS against the play account's current gear (from
  Accounts)" cell is not built: the defence roll is content
  (`~player_defence_roll`), not in this checkout to restate.
- The max hit restates combat_stats.rs2's rule from the tree's worked
  figures; nothing compiles it against the script, so a change there drifts
  silently from the cell.
- The fight log reads what the live link carries: the server prints its
  `HIT` line to stderr only under its debug switch and sends no hunt line,
  so on a normal lane the tab shows the npc-name fallback (and says so).
  Wiring those lines onto the link is the server's half.
- Rings show for npcs whose records the panel holds (the shown set's, and
  any looked at); another set's spawns ring once their npc is selected.
  `huntrange` with no statement draws no hunt ring (the engine default lives
  in content).
- The Set dropdown is a dropdown, not a VLIST with a search box, past 20
  sets.
- A generated-only field with no authored overlay is refused rather than
  creating one (which file, and its pack/npc.server membership, is a
  decision the Record panel's "override in" makes by hand).
- The caster template copies dark_wizard.rs2's first swing verbatim, its
  trigger kept; a swing longer than four step texts (2 KB) is refused.
- Group rows ("goblin x4, 3246..3252") are per spawn ("(1 of 4)"), not one
  row per npc.

## Workbench step 6.7, part 2: the shop builder and the quest board (date 2026-11-06)

Design section 11 ("Shop builder", "Quest board"), section 2 (R2 text never
clips, R3 lists over 20 rows, R4 chords at the action, R8 nothing without a
word), section 16 step 6.7 in part. The encounter builder is part 1 and is
not touched here.

What landed:

- **The shop model** (src/editor/editor_shop.{h,c}, pure). A shop is the pair
  tools/gen_shop_scripts.py writes and nothing else: one `.rs2` (the citation
  header, `[opnpc<op>,<owner>] @<key>_open;` per owner, `[label,<key>_open]`
  and `~openshop(<inv>, <buy>, <sell>, <delta>, "<title>");`) and one `.inv`
  (`[<cache inv>]`, scope / restock / stackall, `allstock=yes` for a general
  store, `stock<N>=<obj>,<count>,<rate>`). The multipliers are passed inline
  to `~openshop` as the generator does (SHOPS_PLAN 4.1), not as npc params.
  `Editor_ShopEmitRs2` / `_EmitInv` are render_rs2 / render_inv line for line
  (the owner line's quirk "`// Owner: x. .`" for a shop with no `special`,
  the patched-stub note sorted, the blank line in place of no triggers, `"`
  in the title written `'`); `Editor_ShopParse` reads back exactly what
  either mark's emitter wrote and refuses the rest with its line;
  `Editor_ShopSaveRefusal` refuses the generator's own pair ("change the wiki
  catalogue and regenerate"). Prices are LostCity's calc_shop_value
  (`Editor_ShopValue`: scale(max(100, m - clamp(-5000, 1000, d * delta)),
  1000, cost), truncated; `Editor_ShopSellsAt` floors at 1, `_BuysAt` does
  not). `Editor_ShopCheck` says what the model alone can (key, owners, op,
  inv name, the three multipliers' ranges, buy over sell, each row, a
  repeated obj, no stock). `Editor_ShopWikiStock` reads one shop's rows of
  wiki/shop_stock.csv (RFC 4180, review / unresolved rows skipped and
  counted). The builder writes into its own directory,
  `server/scripts/shop/edited/<key>/{scripts,configs}/` (TEXT documents
  `rs2:shop/edited/<key>/...`), under its own mark (`EDITOR_SHOP_MARK`).
- **tools/gen_shop_scripts.py fixed**: the owner line's `owner_note` had been
  written into render_inv (which has no owners), so render_inv raised
  NameError on `owner_gamevals` and render_rs2 on `owner_note`, and `--write`
  could not run. It is computed in render_rs2 now; render_inv lost the dead
  lines. Its output is otherwise unchanged.
- **The Shop builder** (module "shop", src/editor/panels/edpanel_shop.c, View
  > Shop builder, `view.shop` unbound): Shop key + Open (the builder's pair,
  else the generator's found through the catalog's
  `[label,<key>_open]` TRIGGER row and shown read-only, else a new shop) and
  New; Owner npc (space-separated) + Pick owner (the Spawns selection's npc,
  else the Catalog's npc pick); the op dropdown named from the owner's record
  (`op3 Trade` by default); Sell / Buy / Delta per mille, general store,
  members, Cache inv, Title; the stock VLIST under a Find box ("N · obj ·
  count · rate  sells at X gp · buys at Y gp", the obj's `cost` asked of the
  catalog four records a frame, 1 when the record states none), the row
  editor (obj + Pick obj, count, rate, Set row, Remove row), + stock, From
  wiki catalogue... (disabled with the reason when the content tree has no
  wiki/shop_stock.csv); Test, Check, Save wearing `file.run`,
  `file.check_scripts` and `file.save_all`'s keycaps; a Problems VLIST.
  Check adds the catalog's refusals: the owner is an npc and has the op's
  menu text, every obj exists, the inv is a cache inv with room for the rows
  (SHOPS_PLAN 2.4) that no other `.inv` stocks, and no other file declares
  `[opnpc<op>,<owner>]` (named with its file and line). Save runs Check, then
  both documents through the authority as ONE stroke (Builder_TextSync, one
  History row), then saves them; an undo or a redo of it is followed (the
  panel re-reads the pair when either document's revision moves). Test:
  Save, the Live panel's RUN, and once RUN lands a LIVE_CHEAT `::goto` (the
  engine's own teleport) one tile east of the owner's spawn -- the Spawns
  selection's row of it, else a row of an open square's spawn file -- for
  the lane's play account, and the status sentence "Test: <account> is
  beside <owner> at x,z; right-click them and choose Trade"; a RUN that
  stopped says where. Every refusal is a status sentence.
- **The quest board's reader** (src/editor/editor_quest.{h,c}, file reads
  only). Editor_QuestRead fills the rows of design section 11's figure from
  test/quests/QUEUE.tsv (test_id -> quest_dir), tools/quest_gate/
  quest_inventory.tsv (display name, `%varp...` var, the constants' prefix),
  the catalog (a `struct Editor_QuestCatalog` the caller binds: the DBROW
  record named quest_dir, the TRIGGER rows under server/scripts/quests/
  <dir>/), the quest's `.constant` file, its scripts (the catalog's files and
  its scripts/ directory): **start** is the block that sets the first stage
  after not_started, walked up its `@label` / `~proc` callers to the op
  trigger ("start: opnpc1 cook"), its dialogue the npc builder's node count
  when edited/<npc>/<npc>.interaction exists, else the dialogue calls it and
  the labels it jumps to make; **each stage** between not_started and
  complete is the first block that tests it (a comparison, or a
  `case ^c` under `switch_int (%var)`), "ok" when the test file names it
  (`expect_stage("<s>")`, `quest.stage.<s>`) else "no test row";
  **complete** the block that sets it; **journal** a `[questjournal,...]`
  block or a `*_journal` proc and its string lines; **test** the Lua's row
  and shot call sites (t.exec / t.expect / t.check / t.step / t.blocked,
  expect_complete's four rows and its scroll shot), the ledger
  (build/quest_gate/<id>/ledger.tsv: rows, SUMMARY, shots) and gate.py's word
  from build/maped_quest_board/<id>.gate. The state words: ok, missing, not
  indexed (no catalog), no test row, not run, not gated (a ledger and no
  gate run), stale (the gate's word older than the ledger), green, red,
  blocked. `Editor_QuestShotPath` cuts a long shot name as gate.py's
  shot_file_stem does (FNV-1a). `Editor_QuestCanScaffold` / `_CanGate` say
  why the tools cannot run here.
- **Quest BUILD jobs** (torirsmaped/torirs_maped_build.{h,c},
  src/maped/shop-quest.mk). `TORIRSMAPED_BUILD_QUEST_SCAFFOLD` (16) and
  `_QUEST_GATE` (17), numbered past the reserved kinds; the BUILD frame takes
  an optional `string quest` after `what` (`Editor_LiveEncodeBuildQuest`);
  a quest kind without one, another kind with one, or a quest that is not
  `[a-z0-9_]` (`ToriRSMapEd_BuildQuestValid`, inline so the web can check
  it) is answered REFUSED. `ToriRSMapEd_BuildCommand` spells `make -C
  <repo>/src --no-print-directory maped-quest-scaffold|maped-quest-gate
  MAPED_QUEST=<id> TORIRSSERVER_CONTENT_DIR=... OBJVERIFY_SKIP=1`. The make
  targets: maped-quest-scaffold runs new_quest.py (`--qh-root` from
  MAPED_QUEST_HELPER_ROOT / TORIRS_QUEST_HELPER_ROOT); maped-quest-gate runs
  run.py --no-build (its exit is not the verdict), then gate.py into
  build/maped_quest_board/<id>.gate under a header naming the target (the
  directory is the target's), and fails only when gate.py printed no verdict.
- **The Quest board** (module "quest", src/editor/panels/edpanel_quest.c,
  View > Quest board, `view.quest` unbound): Quest id + Open + Refresh; a
  cells VLIST of the parts (part, where, state) under a Find box; Open part
  (the NPC builder on the start npc, Triggers on a script file, the Record
  panel for a loc / npc / obj record; a read-only CODE view under the board
  for what no panel here edits: the dbrow's block, the constants -- there is
  no Vars panel in this build -- and the test's Lua); Scaffold test and Run
  gate as BUILD jobs (refused with the reason; the job's newest line streams
  into the board and its end reads the board again, "quest gate for <id>
  finished: the test is green"); Ledger... (the rows in a VLIST under a Find
  box; a picked row's first screenshot as a MODELVIEW sprite through the new
  `EditorUiAppServices.png_publish` -- engine/png_decode.h, box-filtered to
  fit, scene ids `UITREE_SCENE_EDITOR_SHOT_BASE` 0x4D000000 + slot, native
  only -- else its path); RUN wearing `file.run`'s keycap.
- Both panels are registered after Triggers, hidden until the View menu
  opens them; `TORIRS_EDITOR_SHOP=<key>` / `TORIRS_EDITOR_QUEST=<id>` open
  them with the editor (photographs). editor_panel.c gives the board the
  manifest's content dir and repo_root, the shop builder the content dir.

Tests (this container, content-free; OPT and OPT=0 with asserts):

- `make -C src test-editor-shop` (new, src/maped/shop-quest.mk; 115 checks):
  editor/test/editor_shop_oracle.py imports gen_shop_scripts and calls its
  own render_rs2 / render_inv on the four shops of
  editor/test/shop_samples.tsv (SHOPS_PLAN 4.3's Lumbridge General Store with
  its 15 rows and two owners; a members' specialty shop whose title holds `"`
  and two of whose three owners are patched stubs; a quest-gated shop with no
  owner; a shop with no `special`), and editor_shop.c must emit the same
  bytes for the same rows; the generator's pairs parse back and re-emit, the
  builder's own pair round-trips, damaged text is refused with its line;
  prices by hand (a pot, cost 1, at 1300 / 400 sells for 1 and buys for 0;
  bread, cost 12, sells for 15 and buys for 4; the floor; 115 off baseline;
  the clamps); the model's refusals; the wiki rows.
- `make -C src test-editor-quest` (new; 54 checks): the reader over a
  synthetic repository and content tree (Cook's Assistant by hand): every
  row's text, file:line and state word, the switch_int stage, the journal in
  a file only the catalog names, the counts; green, stale, red, not gated,
  not run, missing in turn; the shot stems against gate.py's for an 84-byte
  name; the Scaffold / Run gate refusals; no index.
- `make -C src test-editor-shop-quest-panels` (new; 79 checks): both panels
  over an embedded ToriRSMapEd (its catalog indexing a synthetic tree, its
  BUILD runner the test runner): a shop typed in and priced from the
  catalog's costs, Check clean, Save one undo step whose files are the
  emitter's bytes, undo / redo followed; Check's refusals (an inv too small,
  another file's `[opnpc3,...]` with its line, an unknown obj) and Save
  refusing them; the generated shop read-only; the wiki rows; Test refused
  with no spawn, RUN sent and "stopped at reload" said, a landed RUN sending
  the account; the keycaps; the board's rows through the catalog, Open part
  into Triggers / the CODE view, Run gate's refusal, its make line and its
  verdict read back, Scaffold's refusal, the ledger and the shot through
  png_publish or its path; the BUILD frame's three refusals and the
  scaffold's make line; the two View actions.
- Regression, all 0 failed: test-editor-hotkeys 2025 (the doc test over the
  two new rows), test-editor-chords 591, test-editor-groups 2190,
  test-editor-stamps 91, test-editor-stamps-ops 81, test-editor-workbench
  295, check-tree-walks and check-pt-switch PASS; test-live-editor 172 and
  test-live-panel 34 (both run with a synthetic m50_50 for the square they
  copy, and test-live-editor linked with `-lm` by hand: its recipe links
  none, as torirsmaped's, not from this change). `make -C src all` links.

Seen to fail before their fix, each in a scratch worktree: the base tree's
gen_shop_scripts.py (the oracle dies on NameError); the emitter writing
allstock for every shop (4 of 115), the patched note unsorted (2), prices
rounded instead of truncated (2), no floor of 1 (1); the gate's word never
stale (1 of 54), a switch_int case not a test (1), the start not walked up
to its op trigger (2); Save without its stroke (2 of 79: two undo steps),
Save without the catalog's Check (7), the board not read again when its job
lands (1), the BUILD frame running a quest kind with no quest or a bad one
(3).

Not run here: test-editor-panels (needs the OSRS-Content squares), and any
real quest run (run.py needs the content tree, the quest client and a
quest-helper checkout; the board's job is exercised through the test
runner). Section 16's checks for 6.7 that need the play client -- a shop
built from the panel opening in game at the computed prices, the board's
gate row matching a real gate.py run -- are not done here.

Left open:

- Save runs the catalog's Check synchronously (a few record requests per
  stock row it has not priced yet); a 40-row shop asked cold is ~45
  requests on the click. The stock list itself asks four a frame.
- The Test teleport needs the owner's spawn in the Spawns selection or an
  open square's EDITED spawn file: the catalog counts spawn rows per square
  and does not say where an npc stands, so a shop whose keeper is only in
  the generated areas/world spawns needs its square opened (or Spawns) first.
- A new shop's inv must be a cache inv (Check refuses another); allocating
  one in pack/inv.alloc from the panel is not offered.
- The shop builder edits one owner op for all owners; per-owner Talk-to
  dialogue (SHOPS_PLAN 4.3's `_talk` label) is the NPC builder's.
- The board finds stages by the var's assignments and comparisons in the
  quest's own files; a quest that moves its var only through a shared proc
  (`~set_quest_stage(...)`) shows those stages as "no trigger reads or sets
  it". The journal is found by name (`[questjournal,...]`, `*_journal`).
- No Vars panel exists to open (the constants are shown in the CODE view);
  the Record panel takes loc / npc / obj only (the dbrow's block is shown).
- Decoding a screenshot runs on the click that picks the row (one PNG,
  native only); the web board lists paths.
- The dbrow row quotes no column: the catalog's record holds a repeated
  `data=` key once, so the points shown are the .constant's.

## Workbench step 6.8: workspaces, the command palette, the Hotkeys panel, the chord moves (date 2026-11-06)

Design section 3 ("Workspaces", "Command palette"), section 15 (figure 5,
"Chord spelling", "Resolved collisions"), section 16 step 6.8. Built on
v3-workbench `40ca3878` (6.0 to 6.4); the 6.5 to 6.7 slices are other
sessions', so nothing here calls their APIs.

What landed:

- **Workspaces** (editor_ui.{c,h}, editor_layout.{c,h}). `enum
  Editor_UiWorkspace` Build, Terrain, Script, Playtest;
  `Editor_UiSetWorkspace`, `Editor_UiWorkspaceName` / `Title` / `Find`,
  `Editor_UiWorkspacePanels` (the design table as module names). Build is
  the modules' own default set (the boot layout, unchanged); Terrain shows
  Terrain and Palette (Terrain in front), Squares, the Inspector (the front
  tab of right-top), Console and History; Script Record, Triggers, the Loc
  and NPC builders, the Shell editor and the Console; Playtest Live, Accounts
  and the Console. A table panel this window has not registered is skipped
  with one console line ("editor: workspace Script: no vars, problems,
  dialogue_preview panel in this window, skipped"). The dock column takes the
  workspace's own panels first (`ui_dock_order`, per workspace; Build's is the
  old `k_dock_order`, so the boot layout and its golden did not move). A
  switch captures the workspace being left (`EditorUi.workspace_saved[]`),
  gives the target its saved layout when it has one (the console says "(as
  you left it)") else its default; View > Reset layout resets the CURRENT
  workspace to its default (Build's as before). The strips' boot state is kept
  (`EditorUiGroup.boot_open` / `boot_shown`) so Build's default gives it back.
- **The layout file** gains `workspace = <name>` in `[layout]` and one
  `[workspace <name>]` section per workspace: `surface = W H S` and a line per
  panel (`panel.<module> = visible user_placed x y w h maximised`) and per
  group (`group.<name> = ... shown`), "-" for an unstated field, a trailing
  comment cut (`EditorLayoutWorkspace`, `EditorLayout_FindWorkspace` /
  `AddWorkspace` / `WorkspaceFromTop` / `TopFromWorkspace`). The top-level
  `[panel]` / `[group]` sections stay the current workspace's live layout, so
  a file from before workspaces is Build's and applies as before; an unknown
  workspace name is kept. Byte-exact through decode and encode. The
  fingerprint includes the workspace and a switch serial, so the host saves
  after a switch.
- **Menu bar chips**: "Workspace" Build, Terrain, Script, Playtest after the
  Level row, the current one pressed, each tooltip naming its chord. They are
  NOT at the bar's right end: the MENUBAR lays its items out left to right and
  has no right-aligned run.
- **The command palette** (new src/editor/panels/edpanel_palette_cmd.c,
  module "cmdpalette", a MODAL, off the View menu; Ctrl+K). A search box over
  one cells VLIST grouped under headers: Actions (every edge / repeat action
  of the table, the chord as a keycap, an unbound View action's panel in the
  right column), Panels (the View menu's; the View chord), Squares (CATALOG
  kind SQUARE, plus a typed tile "3222 3218" for the picker's Find), Records
  (CATALOG_QUERY over loc, npc and obj with `TORIRSMAPED_CATALOG_PROGRESS`,
  so a warm build answers from its partial index and never makes the frame
  wait; `loc:` `npc:` `obj:` narrow to one kind; the id in the right column),
  Accounts (the Accounts panel's list). **Scenes and Lanes are left out**: the
  6.5 hook `Editor_ScenesList` is not in this base. The model is pure
  (`Editor_CmdPalList`: `Add`, `AddUnseen` for a page's total, `Finish`,
  `RowText`, `Step`, `NextGroup`, `Matches`): a group keeps at most
  `EDITOR_CMDPAL_GROUP_MAX` (100) rows and ends with a footer row "Actions:
  and N more (type to narrow)". The catalog is asked once per change of the
  search, never per frame. While open the App's dispatcher hands it the
  navigation keys (`Editor_CmdPalette_Feed`, beside the Hotkeys capture):
  Up / Down skip headers and footers, PageUp / PageDown by the window, Tab the
  next group (wrapping), Enter runs the row, Escape closes; no chord resolves
  under it. A click on a row runs it. A World-bound action (the camera, Place
  at the hovered tile) is refused with the reason in the status bar.
- **The Hotkeys panel** (edpanel_hotkeys.c, design section 15 figure 5): the
  TREE rows are gone; one wide cells VLIST (20 rows, search above) of every
  action: name, "<group> <context letters>" dimmed, the two chords as two
  keycap CELLS ("-" an empty slot, "press..." the one being captured), the
  gesture spelling in the right column (`Editor_HotkeyActionInfo::gesture`).
  A summary ("161 actions, 0 rebound, 0 conflicts (161 shown)"), Group and
  Context dropdowns, the changed / conflicts chips (an x on a lit one), Sort
  chips over the six columns (group = the table's order; a second press
  reverses; empty values last either way), Reset all. Clicking chord cell n
  captures into slot n ALONE (`Editor_HotkeysSetSlot`; Backspace unbinds that
  slot, a first slot left empty takes the second): the one-chord rebind of
  6.0 is closed. A clash in a context that can be live at once marks both
  rows "!" (the alert ink), the captured row "*". New API:
  `Editor_Hotkeys_SetSort`, `SetFilter`, `Rows`, `ListWidget`, `ResetAll`,
  `BeginCaptureSlot`, `CaptureSlot`; the row's tooltip names the action and
  its id, or what it clashes with.
- **Chrome: three cells-row behaviours** (uitree_debug_overlay, the executor
  shape, the DOM runtime, modern and IE8 CSS): `ToriRSChrome_VListCellsWide`
  (`TORIRS_CHROME_VLIST_CELLS_WIDE` 0x2 in WIDGET_ADD's shape: the keycap
  column half the row, uncapped, so "Ctrl+Shift+Z Ctrl+Y" are two whole
  cells); a sprite field of "-" draws no picture well; a sixth field, the
  mark, "!" draws name, dim part and keycaps in `TORIRS_CHROME_CELL_MARK_INK`
  and "*" the name in the accent. The shape bits moved beside
  `ToriRSChrome_VListCells` in uitree_debug_overlay.h (the model reads them).
  runtime.js and runtime-ie8.js regenerated with tsc 6.0.2 (the recipe
  reproduces the old runtime.js byte for byte from the old source).
- **The chord moves** (editor_hotkeys.{c,h}, docs/MAPED_HOTKEYS.md): the level
  pins on Alt+0..3, Level Auto Alt+4, Show above Alt+5; Ctrl+0 and Ctrl+5 stay
  as Pin level 0's and Show above's second chord for one release, and when
  one fires the console says so (`Editor_HotkeyRetiredNote`: "editor: Ctrl+0
  is the old chord of Pin level 0: Alt+0 from now on; the old one goes next
  release"). Ctrl+1..4 could not stay as aliases: they are the workspaces
  (`view.workspace_build` .. `_playtest`). New ids: `view.command_palette`
  Ctrl+K, `scene.save` Ctrl+Shift+K and `lane.scratch` Ctrl+Shift+N (both
  refuse with the reason: the Scenes and Lanes panels are 6.5's),
  `graph.auto_layout` Ctrl+L in the new context **Q** (a GRAPH well holds the
  chrome's focus: `Editor_HotkeyContextFacts.graph_focus`, set by the App;
  refuses: auto-layout is 6.6's), and the RESERVED layer toggles
  `view.layer_locs` / `_npcs` / `_objs` / `_collision` / `_grid` on
  Alt+L/N/O/C/G (the World HUD is not in this tree; they say so). Ctrl+PgUp /
  PgDn were already 6.3's `edit.level_up` / `level_down`. New groups "Scene"
  and "Graph"; `Editor_HotkeyContextLetters`.
- **Keycap spelling**: `Editor_ChordSpell(chord, apple, glyphs)` writes the
  symbol spelling (⌥⇧⌘Z on Apple, the arrows, ↵ ⌦ ⌫ ⇥) and the words without
  the glyphs; `Editor_ChordDisplay` is it with `EDITOR_HK_APPLE` and
  `EDITOR_HK_SMALL_FACE_GLYPHS`, which is 0: the Small face is the cache's
  p11 and adding the glyphs is a bake from the cache, which this container
  does not have (tools/fontbake_atlas.py, chrome-skin-rebake). The keycaps
  keep the words.

Tests (2026-11-06, this container, build_opt; the editor tests with
`ulimit -s unlimited`):

- **test-editor-workspaces** (new, content-free; maped/hotkeys.mk) 395
  checks, 0 failed: each workspace's panels, front tabs and dock (nothing the
  layout docked covers the World), the skip line, the chips, a drag kept per
  workspace across switches, the layout file's sections and a byte-exact
  re-encode, a restart from the file, Reset layout per workspace, a file from
  before workspaces, a hand-edited row; the palette model over a synthetic
  table (250 actions, 3 panels, a record page with 40 unseen: the headers,
  the 100-row cap, "and 150 more", "and 40 more", Up / Down / Tab), the
  module (Ctrl+K, an empty search's footer, 'fit walls' with its G keycap,
  Tab to Panels, Enter runs and closes, an action row switching workspace, a
  World-bound refusal, nothing to run, Escape); the Hotkeys panel (every
  action listed, the row's cells, sort by action both ways and by chord 2,
  the Group / Context / changed / conflicts filters, the two-slot capture by
  a click on cell 2, Backspace on slot 1, Esc, both rows of a clash marked,
  Reset all).
- test-editor-hotkeys 2268 (was 2015; `test_workbench68`: every new id's
  defaults and context, zero conflicts with the aliases, what fires where,
  Q from the facts, the retired note, one-slot binds, the spelling; the doc
  test over the new rows), test-editor-groups 2190, test-editor-chords 591,
  test-editor-workbench 295 (its three registry digests recorded again for the
  menu bar's chips alone: 1x 2131301703 -> 1167870673, 2x 3539872907 ->
  444441760, 3x 3420398390 -> 129691160; the scenes unchanged; with every 6.8
  change but the chips the old digests held), test-chrome-widgets 1019 (was
  1009) and the DOM test, the runtime / legacy / compat node tests:
  all 0 failed. check-tree-walks and check-pt-switch PASS. `make -C src all`
  builds.
- editor_panels_test's `test_hotkeys` was rewritten for the list. Its
  content-free subset (layout, world panel, popout, shared tools, open relay,
  layout codec, saved layout, maximise, hotkeys, status bar), from a scratch
  copy against an empty content dir: 1292 checks, 3 failed -- the two square
  opens the empty dir cannot serve, and "Reset layout after a saved layout
  returns to the default set" (line 4590), which fails the same way on the
  base `40ca3878` (scratch worktree, 1279 checks, the same 3 failed): not
  from this change.
- Each new check was seen to fail on a mutant in this private worktree, the
  file restored byte for byte after each (scratchpad py/mutate.py): an alias
  that clashes (Ctrl+1 kept on Pin level 1), the pin defaults, the retired
  note, SetSlot clearing the other slot, the glyph spelling, a doc row
  removed, the cells mark ink, the wide column, the DOM's alert and wide
  classes; the workspace not saved on leaving, the default ignoring its list,
  Reset layout going to Build's set, the layout encoder writing 0 for an
  unstated field (it survived the first test set; the hand-edited row's
  re-encode was added for it), the palette's footer, its 100-row cap and
  Tab, the sort's reverse, a capture binding both slots, and the clash
  mark. Every mutant failed its test.
- Not run: test-editor-panels in full and every test reading the content
  tree (OSRS-Content is not checked out); the web build (no emcc); no
  photograph of each workspace (section 16's check; no cache, no display).

Open:

- Scenes and Lanes groups of the palette wait for the 6.5 slice's
  `Editor_ScenesList` (and a lanes list); Ctrl+Shift+K / Ctrl+Shift+N refuse
  until its panels exist; Ctrl+L refuses until 6.6's auto-layout; the
  Alt+L/N/O/C/G layer keys are reserved for a World HUD this tree lacks.
- Groups do not change per workspace: Script's Record and Triggers are two
  windows of the column, not one tab strip with Vars (the three 6.0 groups
  stay, test-editor-groups asserts them). The Record panel shows only once a
  record is open (its own rule), so a fresh Script workspace has no Record
  window yet. The builders open their own outline and dialogue windows over
  the world, as they do from the View menu.
- The workspace chips sit after the Level row, not at the bar's right end.
- Sorting is by chips; the list's header row is drawn, not pressed.
- The palette asks the catalog synchronously, once per keystroke (as the
  Catalog's own model does); with PROGRESS it never waits for the warm build.
  A Square row runs the picker's Find, which needs the Squares panel.
- Keycap symbols wait for the Small face's bake (a cache).

## Workbench steps 6.5 to 6.8 (2026-11-05)

The integration of the slices of build-order steps 6.5 to 6.8 onto
v3-workbench 40ca3878, merged with `--no-ff` in this order: the vector chrome
face's horizontal stem hinting (6.0b), scenes and the Lanes panel (6.5 part
1), the instance helper (6.5 part 2), the records-and-problems slice (6.6
part 1: only its first commit exists, see below), the encounter builder (6.7
part 1), the shop builder and the quest board (6.7 part 2), and workspaces /
the command palette / the Hotkeys panel / the chord moves (6.8). The
builders-and-graph slice (6.6 part 2) has no branch: its worktree was gone
before it ran. Each slice's own section above says what it built; this one
says what landed together, how the seams were joined, the fractional-scale
fix, and what was run.

What landed:

- **6.0b, stems hinted horizontally.** `face_stems` in
  tools/chrome_font_vector.py bakes a stem table per face and glyph beside
  the TTF bytes (src/engine/torirs_chrome_font_vector_baked.{c,h}: Small 98,
  Body 110, Menu 108 stems; the TTF / WOFF / EOT bytes unchanged);
  `ToriRSChromeGlyphs_RasteriseVector` warps x so every stem's edges land on
  whole device pixels (width `max(1, ScalePx(q, w))`), and
  `ToriRSChromeGlyphs_VectorStems` says where each lands. The stem-darkening
  gamma is now 1/1.2.
- **6.5, scenes.** The SCENES document (`EDITOR_DOC_SCENES`, key `scenes`,
  `editor/scenes.txt`, editor_scenes.{h,c}; commands `SCENE_SET` /
  `SCENE_REMOVE`), the Scenes panel (module `scenes`, edpanel_scenes.c:
  Find, Name, the Holds chips, Save scene Ctrl+Shift+K, Enter through the
  save guard `EDITOR_UI_GUARD_ENTER_SCENE`, Update, Rename, Delete),
  `Editor_Live_Teleport`, the `world_camera_set` app service, and
  `Editor_ScenesList` for the palette.
- **6.5, lanes.** The Lanes panel (module still `publish`, title "Lanes"):
  every lane with its change count and lock holder, M/A/D chips, Publish
  to... (Ctrl+Shift+U), Scratch lane (Ctrl+Shift+N,
  `scratch-<yymmdd><letter>`), Switch..., Discard...; `LANE_SWITCH` (87)
  stages the switch in `build/lanes/lane.ini` for the next boot
  (`ToriRSMapEdLanes_BootLane`); a count-only `LANE_CHANGES` answered by
  `FACT_LANE_COUNT` (205) keeps the status bar's lane chip current. The new
  frames and the trailing fields of FACT_LANE_INFO / FACT_LANE_LIST are
  gated by `TORIRSMAPED_CAP_LANES_V2` (64).
- **6.5, instances.** editor_instance.{h,c} (the planner:
  `Editor_InstanceFindFree`, rebasing, `Editor_InstanceCopyCommands`, the
  trigger text), the .wma codec editor_wma.{h,c} (TEXT key `wma:<name>`,
  `worldmap/areas/<name>.wma`), the Instance dialog (module `instance`,
  edpanel_instance.{h,c}) behind the picker's Instance..., and DOC_CREATE
  inside an open stroke (`TORIRSMAPED_DOC_CREATE_IN_STROKE` 0x100,
  `EditorHost_DocCreate.join_stroke`).
- **6.6 part 1, the Record panel's own file.** The one commit of
  workbench-6.6a: the Record half of edpanel_catalog.c moved whole to
  edpanel_record.c (`record_host`); no behaviour changed. The Problems
  panel, the Record rework (tabs, typed editor, Where used) and the
  `check` / `cs2` BUILD kinds of that slice were not built.
- **6.7, encounters.** The ENCOUNTERS text document `editor:encounters.txt`
  (editor_encounters.{h,c}), the stats ledger `stats:<shard>/<name>`
  (`npc_stats/<shard>/<name>.stats`, editor_npcstats.{h,c}), the encounter
  builder (module `encounter`, edpanel_encounter.c: Set, Stats, Combat,
  Drops, Test), the step builder's `[ai,<trigger>]` type, rings in
  app_overlay.c, and Spawns' Group....
- **6.7, shops and quests.** editor_shop.{h,c} (the .rs2 + .inv pair
  tools/gen_shop_scripts.py writes, prices by calc_shop_value; that tool's
  `owner_note` NameError fixed), the Shop builder (module `shop`), the
  quest board reader editor_quest.{h,c} and panel (module `quest`), BUILD
  kinds `TORIRSMAPED_BUILD_QUEST_SCAFFOLD` (16) / `_QUEST_GATE` (17) with
  make targets maped-quest-scaffold / maped-quest-gate, and the native
  `png_publish` app service.
- **6.8.** Four workspaces (`Editor_UiSetWorkspace`, Ctrl+1..4, the menu
  bar's chips, `[workspace <name>]` sections in the layout file), the
  command palette (module `cmdpalette`, Ctrl+K), the Hotkeys panel's sorted
  list with two capture cells a row, the level pins on Alt+0..5 (Ctrl+0 and
  Ctrl+5 kept as second chords), `graph.auto_layout` (Ctrl+L, context Q),
  the reserved World HUD layer chords Alt+L/N/O/C/G, and the cells list's
  keycap column and row marks natively and in runtime.js / runtime-ie8.js.

Conflicts and how they were resolved:

- **Capability bits.** Scenes took 64 for `TORIRSMAPED_CAP_LANES_V2`, the
  instance slice 64 for `TORIRSMAPED_CAP_DOC_CREATE_STROKE` and 128 for
  `TORIRSMAPED_CAP_MAP_INSTANCE`. They are now LANES_V2 = 64,
  DOC_CREATE_STROKE = 128, MAP_INSTANCE = 256 (still set by nothing); the
  server sets the first two (torirs_maped.c), and the instance section above
  says the new numbers.
- **The hotkey table** (editor_hotkeys.{c,h}, MAPED_HOTKEYS.md): the View
  rows are `view.scenes`, `view.encounter`, `view.shop`, `view.quest` in
  that order, then 6.8's. 6.8 had added `scene.save` (Ctrl+Shift+K) and
  `lane.scratch` (Ctrl+Shift+N) in a new Scene group as stand-ins that
  refused until 6.5 landed; 6.5's `file.save_scene` and `file.scratch_lane`
  already own those chords, run the real panels and sit in the File menu, so
  the stand-ins are gone (the enum entries, their refusals in
  edpanel_hotkeys.c, the Hotkeys panel's "Scene" group, the doc's Scene
  table and list line). 6.8's hotkey test checks the File ids on those
  chords instead. Zero conflicts in the table (the doc test passes).
- **edpanels.h, torirs_mapedctl_main.c, editor_host_local.c and the
  status doc** took both sides (each slice's declarations, verbs, document
  paths and section).

Seams joined at the integration:

- **The instance's scene.** `instance_scene_hook` (edpanel_instance.c) now
  records the scene `instance:<name>` -- the new squares, the landing tile's
  level pinned, the camera on the landing tile (the World's yaw, pitch and
  zoom when the window has one) and the note "made by the instance helper"
  -- through the new `Editor_Scenes_AddInStroke` (edpanel_scenes.c), INSIDE
  the Create's stroke: it undoes with the rest, and unlike Save scene it is
  not written at once (Save all writes it with the instance's other
  documents). The status line says ", a scene".
- **The palette's Scenes and Lanes groups.** `EDITOR_CMDPAL_SCENES` and
  `EDITOR_CMDPAL_LANES` (between Records and Accounts), kinds
  `EDITOR_CMDPAL_KIND_SCENE` (Enter runs `Editor_Scenes_Enter`, the save
  guard included) and `EDITOR_CMDPAL_KIND_LANE` (Enter runs
  `Editor_Lanes_Switch`: staged for the next boot). Scenes come from
  `Editor_ScenesList` (the summary is the dim text); lanes from the new
  `Editor_LanesList` (edpanel_publish.c: what the Lanes panel last heard,
  its row text as the dim text, "this lane" / "switch (next boot)" on the
  right).
- **Test through the Live panel.** New `Editor_Live_Cheat(ui, player, line)`
  (live_panel.{h,c}) sends a LIVE_CHEAT through the panel's transport, only
  to an attached lane, and logs it in the console as the panel's own cheats;
  `Editor_Live_Teleport` is now that with `tele <coord>`. The Shop builder's
  Test sends its `::goto` that way (it went straight to the host before, to
  a lane that may not be attached) and, on a lane that is not attached,
  says where to go instead. The encounter builder's Test already ran RUN
  through `Editor_Live_Run` and placed the player through the Accounts
  panel's ACCOUNT_PLACE (online `::tele`, offline the save); unchanged.
- **Every new module in its workspace.** Script now also shows the Shop
  builder and the Quest board; Playtest shows Scenes and the encounter
  builder (they were skipped as "not in this tree"); both columns dock them
  (`k_dock_script`, `k_dock_playtest`). Only Vars, Problems, Dialogue
  preview (6.6) and Live log are still skipped with a console line. The
  three panel groups are unchanged: no slice added a member to them, and the
  bottom strip's Problems / Live log members do not exist.
- **Not joined: the builder's Problems rows.** There is no Problems panel
  (6.6 part 1 built only its first commit) and no graph builder (6.6 part 2
  has no branch), so there is nothing to join.

Fractional-scale layout (the open row of 6.2-6.4):

- **The slider row measures its caption with the chord suffix.** The
  Terrain panel's sliders carry "Strength  Shift+[ ]" as their label;
  `dbg_slider_label_w` (uitree_debug_overlay_widgets.u.c) capped the column
  at the row's fixed label column, so at 1.5x the caption ran on under the
  track. A caption longer than the column now widens it as far as the row
  keeps `DBG_SLIDER_TRACK_MIN` (three knobs) of track, and what is still too
  long is ellipsised where it is drawn (`dbg_elide_text`, R2). A caption that
  fits lays out exactly as before, so the 1x / 2x / 3x goldens are
  unchanged.
- **Panel code on the quarter scale.** The Live panel's and RUN strip's
  origins and the strip's placement (live_panel.c), the Lanes panel's
  origin (edpanel_publish.c) and the popped-out World toolbar's pad
  (app_editor_view.c) go through `edp_px` / `ToriRSChrome_Px`; the status
  bar measures its runs with `ToriRSChrome_MeasureTextQ` at
  `ToriRSChrome_ScaleQ` (as the menu bar draws them) and refits when q
  changes; `Editor_UiLayoutFingerprint` hashes q, so 125 % and 150 % (one
  tier) are different layouts to save.
- **Left on the tier, on purpose:** edpanel_squares.c, edpanel_catalog_palette.c
  and edpanel_chips.c divide and multiply by `ToriRSChrome_Scale` for GRAPH
  item coordinates, which the chrome draws and hit-tests at the integer tier
  (`dbg_draw_graph`'s `ui->scale`; see "Chips at 125/150/175 %" above).
  torirs_plugin_panel.u.c (not editor code) and `App_ChromeScale` keep the
  tier too.

torirsmapedctl verbs added by the slices: `scenes`, `scene-set <scene>
[squares ...] [camera ...] [level ...] [lane ...] [preview ...] [account
...] [note ...]` (`<segment> -` drops one; `scene-set <scene> rename
<new>`), `scene-remove`, `lane-switch <name>` (and `lane` shows the next
boot's lane), `encounters`, `encounter-set`, `encounter-remove`.

Tests (this container, build_opt, all content-free; 'ulimit -s unlimited'):

- `make -C src all` (client, torirsmaped, torirsmapedctl) builds; the
  `torirsmaped` and `torirsmapedctl` targets build on their own too.
- Chrome and the face: test-chrome-widgets 1134 (the slices' 1099 + 10, and
  25 new in `cw_test_slider_long_label`: at q = 4, 5, 6, 7, 8 the Terrain
  panel's "Strength  Shift+[ ]" is drawn whole and ends before the track,
  and a caption no row holds is ellipsised, ends before the track and leaves
  three knobs of travel; 14 of them fail on the old column rule) plus the
  DOM test; test-scene-bridge-chrome 187 PASS; test-debug-overlay-visual
  passes (1x / 2x / 3x goldens unchanged); chrome-font-vector reproduces 14
  files; test-chrome-skin-tiers 922; chrome-skin.
- 6.5: test-editor-scenes 507; test-editor-scenes-ops 85 (80 + 5: the
  palette's Scenes group lists the scene and Enter runs from it, the palette
  closes, the Lanes group lists main, work and the scratch lane);
  test-lanes 76; test-editor-instance 76; test-editor-instance-ops 83 (78 +
  5: the scene `instance:tutorial_cave` in the Create's step with the two
  squares and the camera on the landing tile, not on disk before Save, gone
  on Undo).
- 6.7: test-editor-encounters 121; test-editor-encounter-panel 91;
  test-editor-shop-quest: editor_shop_test 115, editor_quest_test 54,
  editor_shop_quest_panels_test 79 (the landed-RUN check now expects the
  not-attached sentence; see Seams).
- 6.8 and the table: test-editor-workspaces 396 (the Script and Playtest
  skip counts follow which panels the harness registers);
  test-editor-hotkeys 2288 with the doc test, zero conflicts;
  test-editor-chords 591; test-editor-groups 2190; test-editor-workbench 295
  (the three registry digests recorded again, for the slider column alone:
  the merged tree without it drew the old ones; the scene digests did not
  move).
- Also: test-editor-cmd 66, test-editor-palette 97, test-editor-areas 136,
  test-editor-stamps 91, test-editor-stamps-ops 81,
  test-editor-panels-palette 50, test-editor-panels-accounts 48,
  test-editor-catalog-finder 202, test-editor-placement 123,
  test-editor-picker 398, test-editor-worldmap 161, test-catalog-finder 176,
  test-catalog-perf 143 (synthetic half), test-editor-ghost OK,
  test-editor-dropdown 36, test-uitree passes, test-editor-world-gesture 42,
  test-gesture 191, test-touch 47, test-sdl-gesture 42; check-pt-switch and
  check-tree-walks PASS.
- Not run here, for want of the OSRS-Content checkout: test-live-panel (it
  copies maps/m50_50 from the tree), test-editor-panels, test-editor-views,
  test-editor-cfg / alloc / vars / if / script, test-editor-save,
  test-editor-jm2, test-torirsmaped-embed / -daemon. test-builder does not
  link here (no `-lm` in its recipe; known before this change).

What is open:

- **6.6 is not built.** Records and problems (the Problems panel, the Record
  panel's tabs / typed editor / search / changed-only / Where used, the
  `check` and `cs2` BUILD kinds) and builders and graph (the dialogue graph,
  its auto-layout -- Ctrl+L still refuses with its reason -- and the
  builder's Problems rows) need their own runs from this commit. (Both parts
  are now built and integrated: "Workbench step 6.6 (2026-11-06)".)
- Everything each slice lists as open in its own section, in particular:
  the lane change count is a synchronous scan on the embedded server's
  thread; lane switching is staged for the next boot, never live; the .wma
  reaches the catalog's atlas but not the game's world map; the runtime map
  instance chip stays disabled (nothing sets `TORIRSMAPED_CAP_MAP_INSTANCE`);
  the live link carries no HIT / hunt lines, so the encounter's fight log
  falls back to npc names; Save runs the shop's catalog Check synchronously;
  web faces are not hinted horizontally.
- The Shop builder's "beside the owner, choose Trade" sentence now needs an
  attached lane, which the panel test cannot fake (its live status fact
  resets the state each frame); it checks the not-attached sentence instead.
  Section 16's in-game shop check covers the rest.
- No photographs here (no cache, no display, no OSRS-Content), and no web
  build (no emcc).

Review fixes (2026-11-06). Six findings on the integration; each was checked
against the code (and, where a test can reach it, by the new test failing on
ca53a114 in a scratch worktree) before it was fixed or recorded:

- **The Quest board lost a job that ended while it was hidden** (real, fixed).
  `quest_tick` returned before its job block when the panel was hidden, and
  the Live model keeps only the last build's `what`, so a Run gate that
  finished while Playtest showed, followed by a Ctrl+Return RUN, left
  `job_what` set: Scaffold and Run gate stayed disabled and refused ("a build
  or a RUN is in flight") until a restart. `quest_watch_job` now runs every
  frame, shown or not. The job ends on a DONE of its own kind; a single DONE
  of another kind since the send is a build queued before it, so the board
  keeps waiting; two or more DONEs mean its own is among them, since the
  server runs one build at a time, and the board says it ended and points at
  Live. The board is read again on the end without being shown
  (`quest_open(..., show 0)`; `Editor_Quest_Open` still shows it).
  test-editor-shop-quest-panels: 7 new checks (Run gate, hide the board, the
  gate ends, a RUN ends after it, show the board: Run gate is enabled and
  runs again). 3 of them fail on ca53a114.
- **The lane change count walked the mirror on the UI thread on a timer**
  (real, fixed). The server now caches each lane's counts
  (`struct ToriRSMapEdLaneCountCache`, `ToriRSMapEd.lane_counts`). COUNT_ONLY
  and the LIST's counts answer from the cache. An entry holds until a request
  that can write a tree arrives (`note_lane_writes` in torirs_maped.c: the
  saves, a bake, DOC_CREATE, BUILD / RUN and their DONE, an accounts frame;
  a publish, a discard or a lane made invalidates every lane). Another lane's
  entry, or this lane's while another server holds it, also expires after
  `TORIRSMAPED_LANE_COUNT_STALE_S` (60 s). The rows' LANE_CHANGES walk stores
  this lane's counts too. The panel now asks for it before the LIST, so a
  panel open costs one walk of this lane, not two. The Lanes panel's Refresh
  sends the new optional LANE_LIST flags `TORIRSMAPED_LANE_LIST_RESCAN`,
  which walks every lane again; an older server ignores the body. So the
  status bar's 15 s ask, and every popped-out window's, costs no walk unless
  a write came first. The cost of the walk itself is still unmeasured on
  OSRS-Content, and it still happens once after each save. An edit made
  outside every editor (a text editor, git) shows on the next save or
  Refresh, not within 15 s. test-editor-scenes-ops: 6 new checks (a file
  written into the work mirror behind the server's back is not in the chip
  after the periodic ask; Refresh shows it in the chip and the list row;
  removing it and pressing Refresh again shows it gone). 1 of them fails on
  ca53a114.
- **Shop Save, the encounter builder's Apply fields / Apply stats / Add to
  set, and scenes_submit left half a step applied on a refusal** (real,
  fixed). Each now uses the instance helper's pattern. It records
  `Editor_HistoryAppliedCommands` before the stroke, and on a refusal undoes
  the step once after StrokeEnd when it applied anything, so the .rs2 / .inv
  pair, the record, the ledger or scenes.txt is left as it was. The status
  sentence then says "nothing was applied". Inside a stroke another caller
  opened (the encounter panel's `was_open`, `ui->tools.stroke_open` for
  scenes), the edits stay in that step and the sentence says so.
  `scenes_submit` also stops at the first refused command. No harness here
  can make the authority refuse partway through (one window, an embedded
  server, no fault hook), so these paths have no test. The existing shop,
  encounter, scenes and instance tests pass.
- **The palette's Squares group could not find a square by area** (real,
  fixed). A new function, `Editor_SquarePicker_Area` (edpanels.h), returns
  the picker's world-map areas. A window whose picker never fetched its
  atlas asks CATALOG_SQUARES once there. `cmdpal_gather_areas` lists the
  areas whose internal or display name matches the query as Squares rows
  ("area, N squares"). Enter runs the picker's Find on the internal name,
  which opens the area's page. test-editor-picker: 9 new checks (`cellar`
  and `gielinor surf` find their areas; Enter moves the picker from the
  surface's page to the cellar's and closes the palette). 4 of them fail on
  ca53a114; the page-move check was made to start from the surface's page
  after it passed there.
- **The shop builder's Test did not open the shop** (real, fixed). After the
  `::goto`, Test sends a second LIVE_CHEAT, `::talk <owner> <op>`: the
  engine's own way (torirs_server_world.c) to fire `[opnpc<op>]` on an npc
  without the right-click. That is the trigger the builder wrote to open the
  shop. The status sentence becomes "Test: <account> is beside <owner> at
  x,z and its Trade opened the shop (::talk <owner> 3)". If that cheat
  cannot be sent, Test falls back to the right-click sentence. Like the
  `::goto`, it needs an attached lane, which the panel test cannot fake, so
  section 16's in-game shop check is what covers it.

Tests after the fixes (same container, build_opt, 'ulimit -s unlimited'):
- `make -C src all` builds, and the `torirsmaped` and `torirsmapedctl`
  targets build on their own.
- The changed counts: test-editor-shop-quest-panels 86 (79 + 7),
  test-editor-scenes-ops 91 (85 + 6), test-editor-picker 407 (398 + 9).
- Everything else the integration ran, with the same counts as before:
  test-chrome-widgets 1134 plus the DOM test; test-scene-bridge-chrome 187;
  test-debug-overlay-visual passes; chrome-font-vector reproduces 14 files;
  test-chrome-skin-tiers 922; chrome-skin; test-editor-scenes 507;
  test-lanes 76; test-editor-instance 76; test-editor-instance-ops 83;
  test-editor-encounters 121; test-editor-encounter-panel 91;
  editor_shop_test 115; editor_quest_test 54; test-editor-workspaces 396;
  test-editor-hotkeys 2288; test-editor-chords 591; test-editor-groups 2190;
  test-editor-workbench 295; test-editor-cmd 66; test-editor-palette 97;
  test-editor-areas 136; test-editor-stamps 91; test-editor-stamps-ops 81;
  test-editor-panels-palette 50; test-editor-panels-accounts 48;
  test-editor-catalog-finder 202; test-editor-placement 123;
  test-editor-worldmap 161; test-catalog-finder 176; test-catalog-perf 143;
  test-editor-ghost OK; test-editor-dropdown 36; test-uitree passes;
  test-editor-world-gesture 42; test-gesture 191; test-touch 47;
  test-sdl-gesture 42.
- check-pt-switch and check-tree-walks pass.
- Not run, for the same reasons as above: the content-tree tests.

## Workbench step 6.6, part 1: typed record rows, Where used, async checks, the Problems panel (date 2026-11-06)

Design section 9 ("Records and triggers"), section 2 (the contract), section
16 step 6.6. Built on v3-workbench `03a006ed` (6.0 to 6.5, 6.7, 6.8 and the
6.6a move of the Record panel into edpanel_record.c). Part 2 (the builders'
graph, page breaks, chat-box preview, auto-layout, the shell preview slider
in the world's resolve) is another session's; this part leaves the seams it
needs (below).

What landed:

- **The register's types** (editor_cfg.{c,h}). `Editor_CfgRegisterField`
  now keeps the rest of the register's grammar (`type`, `ref`, `client`,
  `server`, `param`, verbatim). `enum Editor_CfgFieldKind` TEXT / NUMBER /
  BOOL / NAME / ENUM / PARAM; `Editor_CfgRegisterValueKind` (values = ->
  enum, `type = boolean` -> bool, a `ref` or a config-type `type` -> name, `type
  = int` or a u1 / u2 / u4 band home -> number, else text),
  `Editor_CfgRegisterRange` (u1 0..255, u2 0..65535, u4, int32),
  `_Words`, `_RefType`, `_ClientSide` (`scope = client`, `client = native`,
  `client = param:<p>`), `Editor_CfgValueIssue`. A key the register does
  not list is typed by its value (an integer a number, yes / no a bool).
- **Typed rows.** `Editor_CfgRecordRow` gained `reg`, `kind`, `value_kind`,
  `client_side` (the winning layer is the cache half, or the register says
  the client reads it), `in_overlay` and `issue`; `Editor_CfgRecordType`
  (run by Resolve; run again with the params the catalog knows) sets them.
  Issues: UNKNOWN_KEY (a SERVER OVERLAY states a key the register lacks --
  the cache half's own opcodes never are), UNKNOWN_PARAM, NOT_NUMBER,
  OUT_OF_RANGE, NOT_BOOL, NOT_WORD. Also `Editor_CfgRecordRowName`,
  `_RowMatches` (the search), `_RowChanged` / `_RemovedSince` ("changed
  only" against an earlier resolve) and `_LayerStates` (does a layer state a
  row, at which line).
- **The Record panel** (edpanel_record.c), rebuilt over that model. Tabs:
  Cache block, one "Server overlay <file>" per overlay that states the
  record, Triggers, Shell, Where used (`Editor_Record_TabCount` / `_TabTitle`
  / `_ShowTab`). A layer's tab lists what that layer states (the top layer's
  adds the register's unstated fields, where Set puts them) as a cells VLIST:
  field, value, source badge (cache / generated / overlay, "shadowed" with
  the winner's file and value), "!" for an issue, "*" for a changed field.
  Above it Find (the search inside the record), the "changed only" chip and
  "dirty, N fields   needs a client pack on RUN". The typed editor for the
  picked row: a number's -1 / +1 step buttons with the range as their
  tooltip (and a Set past the range refused with the range), a bool's
  checkbox (keeping the record's true / false spelling), an enum's dropdown
  of its words, a param's two columns (name, value), a name picker -- a
  cells VLIST the catalog fills with records of the kind the field names as
  you type (a param's name picks from the catalog's params); Write to, Set
  field, Remove field as before, one undo step each. Triggers tab: the
  subject's triggers (`Editor_Triggers_ForSubject`), Open in editor, Check
  (wears Ctrl+Shift+B), Triggers... (Ctrl+E). Shell tab: the cache block's
  multivar / multiloc table and `Builder_ShellSummary`, and Shell editor...
  (`Builder_PanelOpenSubject` on the shell editor). Where used tab (below).
  Every issue is a Problems row (source "record") with the overlay's file
  and line; its click opens the record at the field, on its layer's tab.
  An overlay's `param=<p>` the register lacks is asked of the catalog once
  at open (an exact param query); a param the tree defines is not unknown.
- **CATALOG_USES** (torirs_maped_catalog.{c,h}, request 44, fact 165,
  `TORIRSMAPED_CAP_CATALOG_USES` 512, set with the catalog; client
  `Editor_HostMapEdCatalogUses` / `_CatalogUsesFree`). Where a record is
  named, read from the files the index lists: SPAWN (.spawn rows per
  square), PLACEMENT (a loc's .jl2 rows by id, per square: the atlas's loc
  counts per loc), TRIGGER (an .rs2 header), SCRIPT, INTERFACE, CS2, CONFIG
  (another overlay record's field; a block's own header is not a use),
  PALETTE (editor/palettes.txt), STAMP (editor/stamps/*.stamp), MORE (a
  file's lines past 8). A search, not an index: one request reads for about
  4 ms (at least one file) and at most 256 rows and answers with the cursor
  to go on from; the Record panel asks once a frame while the tab shows, so
  no frame holds more than a slice. The tab's list has its own Find.
- **Check and CS2 compile are BUILD jobs** (torirs_maped_build.{c,h}).
  `TORIRSMAPED_BUILD_CHECK` (3, was the never-offered SCRIPTS_LINT) runs
  `make -C src maped-script-check` (src/maped/records.mk): the new tool
  maped_script_check (editor/builder/builder_ssc_main.c) stages
  server/scripts under `<live>/check` with builder_ssc.c, writes the
  server's unsaved `rs2:` documents (the server puts them in
  `<live>/check/puts` just before the job) over the stage, runs sscompile and
  prints `<file>:<line>: [warning: ]<message>` lines and a `check: ok, 0.6 s`
  / `check: N error(s), M warning(s), 0.6 s` summary
  (`Builder_SscFormatDiag` / `_FormatSummary`; `Builder_SscTakeLine` reads
  them back). `TORIRSMAPED_BUILD_CS2` (4) takes the script name
  (`ToriRSMapEd_BuildWhatTakesArg` / `_BuildArgValid`: [A-Za-z0-9_.-], not
  starting with '.' or '-') and runs `make -C src maped-cs2-compile`: `cs2
  compile` of the one script the server wrote to `<live>/cs2/src` -- its
  `cs2:<name>` document with unsaved edits, else scripts/<name>.cs2 on disk;
  ABSENT when neither exists. `TORIRSMAPED_CAP_BUILD_CHECK` (1024) rides
  with the runner. The Live model tags each log row with the build it came
  from (`Editor_LiveLogWhat`, `EDITOR_LIVE_LOG_DONE`) and
  `Editor_LiveEncodeBuildArg` sends a kind with an argument.
- **The client never waits.** `Editor_BuildJobSend` / `_Poll` / `_Why`
  (edpanel_problems.c): send a job, hear its own lines, oldest first, and its
  end on a later frame. The Triggers panel's Check is a job now
  (`Editor_Triggers_Check` returns at once; `Editor_Triggers_CheckRunning`),
  its lines fill its Problems list as they arrive, and the status says
  "check: ok, 0.6 s" or "check: 1 error(s), 0.6 s; first <file>:<line>:
  <message>". The interface editor's Compile asks (iface_panels.c
  `compile_pending`) and iface_register.c sends the CS2 job and shows the
  result when it ends (`EditorCs2_CompileBegin` / `_End`, so a job's lines
  read like the old synchronous run's). `EditorCs2_Compile` and
  `Builder_SscRun` stay for tests and the tool.
- **The Problems panel** (module "problems", edpanel_problems.c), the bottom
  strip's third member (group "Console / History / Problems"), in front in
  the Script workspace, View > Problems (`view.problems`, unbound). Rows
  (source, file, line, severity, text, and for a record its type, name and
  field) from the Live model's line stream -- every build line that reads as
  a diagnostic (`Editor_ProblemsParseLine`: sscompile's located lines, cs2's
  `FAIL <name>: <why>`, `error:` / `warning:` lines; make's own lines are
  not), filed under the build's name ("check", "cs2 compile", "packs"); a
  new build of a kind clears that kind's rows; a failed build that said
  nothing a row reads leaves its DONE line -- and from the push API
  `Editor_Problems_Report(ui, source, file, line, severity, text)`,
  `_ReportRecord`, `_Clear(ui, source)` (a duplicate is one row). Errors
  list first; Find filters as you type; "errors only"; a click opens a
  record at its field, else the script in your editor at the line (the
  Triggers panel's Open in editor); Open in editor, Open record, Clear.

The seams for part 2 and the other builders:

- The builder panel's Check (builder_panels.c, part 2's file) still calls
  `Builder_SscRun` synchronously, over text it emits from the model and puts
  on its own stage. As a job: the CHECK job writes every unsaved `rs2:`
  document the server holds over its stage, so once the panel's generated
  script is in its document (`Builder_FileKey`, what Regenerate edits) the
  panel needs only `Editor_BuildJobSend(ui, &job, TORIRSMAPED_BUILD_CHECK,
  NULL, ...)`, `Builder_SscTakeLine` on each line and its rows from the
  result. Text not in a document (an un-regenerated model, the generated
  .npc / .dbrow, which are not `rs2:`) is not in the job. Its lint rows go
  to `Editor_Problems_Report(ui, "lint", ...)`.
- The quest board's and the shop builder's Check can report their issues
  with `Editor_Problems_Report(ui, "quest" / "shop", ...)`; neither does yet
  (their files were not this slice's).

Tests (this container, build_opt, content-free; 'ulimit -s unlimited'):

- New: `make -C src test-editor-records` (editor/test/editor_records_test.c)
  78 checks: the register's kinds, ranges, words, ref types and client
  side over a synthetic fields/npc.ini; a synthetic record over a cache block
  and two overlays (every row's field, kind, client side and issue; the
  cache half never unknown; typed guesses; a known param); the search;
  "changed only"; Check's job lines formatted and read back.
- New: `make -C src test-editor-records-panels`
  (editor/test/editor_records_panels_test.c) 121 checks, over an embedded
  server, a synthetic tree and the test BUILD runner: the Record panel's six
  tabs, rows and badges, issues, the typed editor per kind, Set and its
  range refusal, the name picker filtering as you type, the client-pack
  chip, the search and "changed only", undo back to clean; Problems from the
  record, a click back to the field, the push API, Find and "errors only",
  the View action and the group; Check returning before any line and its
  slow job still running a frame later, the unsaved script the server wrote
  for it, its rows, a clean Check clearing them, a silent failed build's
  DONE row, CS2 of the disk's script and of the open document, the
  refusals; Where used over the tree a slice a frame (trigger, script,
  spawns per square, an overlay, a palette; a loc's placements and stamp;
  320 lines past one answer's rows; a longer word is not a use).
- New: `make -C src test-maped-script-check`
  (editor/test/maped_script_check_test.sh) 10 checks: the Check job's tool
  over a synthetic tree with a stand-in sscompile -- an unsaved put staged
  and its error a job line with the stage-relative path, the put gone, the
  next run clean, a work dir inside the content tree refused and nothing
  written there.
- Seen to fail before their fix (mutations in this private worktree, the
  panels test built into its own object base, build_mut): dropping the
  unknown-key rule fails the records test ("an overlay key the register
  lacks is an unknown key"); a new build that does not clear its kind's
  rows fails the panels test ("a clean one clears its rows"); a Where used
  match inside a longer word fails it twice ("goblins" counted as a CONFIG
  use; "[proc,goblin_helper]" a TRIGGER use).
- Changed because the tree changed: test-editor-groups 2192 (the bottom
  group's three members), test-editor-workspaces 397 (Script skips two
  panels now, not three), test-editor-workbench 295 (the 1x registry golden
  re-recorded: 2691440990 -> 2593871087, the bottom strip's third tab; the
  2x and 3x registries and every scene unchanged).
- Unchanged and passing: test-editor-chords 591, test-editor-hotkeys 2293
  (with the doc test; `view.problems` added to the table and
  docs/MAPED_HOTKEYS.md), test-editor-shop-quest-panels 86,
  test-editor-scenes-ops 91, test-editor-instance-ops 83,
  test-editor-encounter-panel 91, test-editor-picker 407,
  test-editor-catalog-finder 202, test-catalog-finder 176, test-catalog-perf
  143, test-editor-cmd 66, test-editor-palette 97, test-editor-panels-palette
  50, test-editor-panels-accounts 48, test-editor-stamps-ops 81,
  test-editor-placement 123, test-editor-dropdown 36, test-chrome-widgets
  1134; `make -C src` (client and torirsmaped) builds.
- Not run here, for want of the OSRS-Content checkout: test-editor-panels
  (its Record and Triggers sections; `Editor_Triggers_Check` now answers 1
  for "sent", not an error count), test-iface-editor, test-editor-script,
  test-live-editor (its CS2 command expectation was updated: CS2 now has a
  make line), test-builder (not touched; its recipe still lacks -lm).
  The make targets `maped-script-check` and `maped-cs2-compile` themselves
  were not run here (no content tree to stage, no cache for cs2); the
  check tool is run by test-maped-script-check.

What is open:

- The builder panel's Check is synchronous (part 2's file; the seam above).
- The CS2 job's `--names` is RuneStar's tables when they are on the machine;
  the panel's old run wrote the content's own script names over them
  (`EditorCs2Catalog_WriteNamesDir`), which the server-side job does not, so
  a script calling a content proc by name may decline where the old run
  compiled. The compiled script's path is the lane's, which the client does
  not name (`output_path` is empty after a job).
- Where used reads files, it does not index them: a cold search of the
  owner's tree (~41,000 sources) at 4 ms a frame takes on the order of 10 s
  of frames to finish (unmeasured here), and configs/all.<type> is never
  read (another cache record naming it is not found). Placements are the
  .jl2 files on disk; an open square's unsaved locs are not counted.
- Not added to the DOM executor: nothing new needed it (the rows are cells
  VLISTs, the tabs a TABSTRIP, the editors existing widgets shown and
  hidden), so runtime.js / runtime-ie8.js were not regenerated.
- The Record panel's Triggers and Shell lists have no search box; a shell
  can hold 257 slots (rule R3 wants one past 20 rows).
- The quest board and the shop builder do not push their Check rows into
  Problems yet; "Copy to new..." and "Find in world" from the figure are not
  built.

## Workbench step 6.6, part 2: the dialogue graph, the chat box, the shell preview in the world (date 2026-11-06)

Design section 10 ("Graph ergonomics", "Steps and shells"), section 2 (R2
text measured, R8 refusals say why, R3 lists over 20 rows), section 16 step
6.6 in part. Built from 03a006ed. Part 1 (records, the Problems panel, the
`check` / `cs2` BUILD kinds) was built in parallel; the seam between the two
is the Problems API below.

What is built:

- **Graph ports and links** (builder_panels.c, builder_model.{h,c}). A
  node's out-ports are keyed GRAPH items on its foot (`Builder_NodePortCount`
  / `Builder_NodePortNext`: a choice's options, a branch's arms then its
  default, none on an End, `next` on the rest); a filled port is linked. A
  press on a port and a drag draws the link to the pointer; released on a
  node (its body or a port) it links (`Builder_ModelLink`, one regeneration,
  "Linked bye port 1 to thanks"); released on empty space it opens the
  **new-node chip row** there, drawn in the well itself (one keyed chip per
  kind: NPC, Player, Mesbox, Objbox, Choice, Branch, Step, Quest, End), so the
  DOM executor shows it with no new entry point; a chip makes the node at the
  drop and links it; Escape or a press elsewhere closes the row. A drag on a
  node moves it; **Shift+drag** moves it with everything it reaches
  (`Builder_ModelReach`), as one piece clamped at the well's edge; a drag is
  one regeneration on release, a press without movement none. Right-click on
  empty space still adds a node of the dropdown's kind.
- **Auto-layout** (`Builder_ModelAutoLayout`, Ctrl+L, the Auto-layout button
  with its keycap): left to right by depth -- a breadth-first walk from the
  start, ports in port order, a column per depth (150 px) and a row per node
  in reading order (44 px); the orphans follow as roots of their own walks in
  model order. A pure function of the links (a shuffled layout lays out to
  the same picture; a second Ctrl+L moves nothing). `graph.auto_layout` no
  longer refuses: `Builder_GraphHotkey` runs it on the NPC builder's graph
  (focused, or its window open), or says why it cannot.
- **pos=**: positions were already written and read per node; they now
  survive every gesture (each drag and Ctrl+L is a regeneration of the
  .interaction through the authority), and a damaged `pos=` is refused with
  its line.
- **Delete and New node.** Delete with the graph's well holding the keyboard
  deletes the selected node; the chrome now hands Delete to a focused GRAPH
  well (natively in `dbg_ext_key_edit`, in the DOM as keyCode 46 ->
  GRAPH_KEY -DELETE) and the panel matches it against `edit.delete`'s own
  chords (a rebind moves it; `edit.delete` lives in L | S, which a focused
  well is in neither of, so no new chord clashes). The sentence names what
  lost its last way in from the start: "Deleted work (Branch on var): job,
  start and nowork have no way in from the start now (orphaned; Undo brings
  it back)", or "nothing was orphaned". `edit.delete` from the App
  (Builder_GraphHotkey first, only with the well's focus) does the same.
  New action **`graph.new_node`** (N, context Q, MAPED_HOTKEYS.md): a node of
  the Node kind dropdown's kind after the selected one; the New node and
  Delete node buttons wear N and Delete.
- **The outline is the same model.** Its node rows select the node (graph,
  form, pages and preview follow) and the graph's press selects the row
  (`bp_select_node`); orphans now have rows too, under "Orphans (not reached
  from the start)".
- **The chat box's line rule** (builder_chat.{h,c}, new): the client's
  splitter as torirs_server_split.c's `find_split` has it (`|` and `<br>`
  hard, tags widthless, `<lt>`/`<gt>` their glyphs, the last space once the
  width passes the box, a hyphen breaking after itself), lines filling pages
  `page_lines` at a time, each line's source offset kept so a page break
  points into the node's own text. **The numbers are the editor's, not the
  content's**: the dialogue procs page through `split_init(text, width, lines,
  p12_full)` in OSRS-Content, which is not checked out here, so the width
  (380 px) and lines a page (4) are this tree's reading of the chat box, and
  the advances are the BAKED p12_full metrics (archive 495, the chrome's Body
  face at 1x, `Builder_ChatMetricsBaked`), not the cache's fontmetrics.
  `{player}` lays out as a twelve-character stand-in (the widest name), so
  the breaks shown are the earliest any player sees. The uitree's chatview
  (src/ui/uitree_chatview.h) is the message ring, not the chatnpc layout,
  and the chatnpc interface needs the cache, so this is the fallback the
  slice names.
- **Pages and the preview.** Under the node's Text field a GRAPH well shows
  the text as the chat box breaks it, a line a row, a thin rule and "page N"
  where the client starts the next page, the box's right edge marked; it
  follows the field as it is typed (the tick re-lays it when the text
  changes; a field at its 191-byte cap shows the model's longer page). A
  click on a page selects it. The **chat box preview** (a GRAPH well) draws
  that page as the client would: the parchment box, the speaker in dark red
  over the text column (the record's name, the speaker override, or the
  stand-in for a player line), the page's lines centred, "Click here to
  continue"; a choice is its title and options. Text in both wells goes out
  as measured 31-byte runs, so nothing is cut; a node caption that does not
  fit its box drops the kind, then ends in an ellipsis (the full id is in the
  outline and the form). The **head** is a nullable service,
  `Builder_Services.chat_head` (a host-rendered scene sprite drawn as a
  GRAPH_IMAGE); nothing binds it yet, so the box is drawn empty and the label
  says "no head: no preview pipeline is bound".
- **Lint rows** (builder_lint.{h,c}, new): literal player name (a U+00A0, or
  the session's own name as a whole word), orphan node, a choice with fewer
  than two options, unknown var (a branch, a quest start, an if / set step),
  unknown proc (a CALL step), each with the node (or trigger and step) it
  names. Vars and procs are asked of the catalog (`Builder_Services.name_known`,
  bound in builder_session.c to exact CATALOG_QUERYs: VARP then VARBIT, and
  TRIGGER `proc,<name>`), once per name per subject; "cannot say" (no
  catalog) leaves the kind unchecked and the label says so. The dialogue
  window has a Problems section: a find box and a VLIST (R3) of the rows; a
  click or Enter selects what the row names. Every lint run reports the rows
  to Problems when they changed.
- **The Problems seam** (src/editor/panels/edpanel_problems_api.h, new):
  `Editor_Problems_Report(ui, source, rows, count, open, user)` and
  `Editor_Problems_Clear(ui, source)` did not exist in this base, so they are
  declared there with a WEAK default in builder_panels.c that writes each
  row to the console (and, on a compiler without weak symbols, unless
  `EDITOR_HAVE_PROBLEMS_PANEL` is defined). Part 1's panel joins by defining
  the two; the source is `npcbuilder:<npc>`, `open` selects the row's node.
  builder_test defines them strongly, which is how the seam is checked.
- **The shell preview, live and in the world.** The Shell editor's slider
  now publishes STATE_VAR_PREVIEW as it is dragged (the tick, every value it
  passes; the release still does too), opening a shell publishes nothing,
  and another window's preview moves the knob without echoing back.
  `VarPManager_SetPreview` (varp_manager.{h,c}) holds up to five previewed
  vars; `VarPManager_ResolveTransform` resolves a table keyed on exactly a
  previewed var with its value (a varbit preview does not move a shell keyed
  on its base varp, or the reverse), so every multiloc and multinpc resolve
  in the client honours it -- the EDIT world's placements, the runtime loc
  refresh, the npc multi resolve -- while every other reader sees the
  player's values. Setting it announces each old and new var's base varp
  through the change callback, so `app_varp_change` re-resolves the placed
  locs and npcs that depend on it: scrubbing changes the placed multiloc's
  child. The client feeds it from `app_editor_on_state`
  (`app_map_editor_var_preview`, App-free) and clears it at the editor's
  shutdown. `WorldBuilder_ResolveLocId` exposes the placement's own resolve
  for the test.
- builder_emit.c is unchanged: the emitter writes a page's text verbatim but
  `{player}` (as `<displayname>`, which the stand-in measures for), so the
  script breaks where the pages well shows; a test pins it.

Tests (this container, build_opt, content-free; 'ulimit -s unlimited'):

- `make -C src all` builds (client, torirsmaped, torirsmapedctl).
- test-builder: its recipe gains `-lm` (it did not link here) and the
  editor_hotkeys / keymap / edpanel_common sources; without the content tree
  the sweep over every real shell and the compile section skip and say so.
  243 checks, 0 failed: the sections that ran before (ini, golden, dialogue,
  lint, conflicts, splices, shell's synthetic half, preview, authority, all
  passing here once linked) plus 4 new sections -- graph (ports,
  link, reach, orphans, the auto-layout's exact grid for the fixture,
  stability, a shuffled layout's identity, an orphan as a root, pos= round
  trip and its refusal), chat (the line rule by hand at synthetic metrics,
  page-break offsets, `|` / `<br>` / tags / hyphen / `{player}`, the baked
  p12_full against an independent greedy wrap, emit fidelity), lintrows (each
  of the five kinds at its node or step; unchecked without a catalog and on
  "cannot say"), graphpanel (the NPC builder driven by GRAPH events over an
  embedded authority: chords on the buttons, port drag to a node links and
  regenerates, drop on empty space opens the keyed chip row and a chip makes
  and links the node, Escape closes it, Shift+drag moves the subtree and a
  plain drag the node, pos= in the document, Ctrl+L, outline <-> graph
  selection, the pages well's rule and page click, the preview's page, a
  long caption fitted, lint rows to Problems and a click back to the node,
  Delete only with the well's focus and its orphan sentence, Undo, N) -- and
  3 new checks in the shell editor's preview (live scrub, no publish on a
  still knob, no echo).
- test-varp: the new `test_resolve_preview` passes with the rest.
- test-edit-world-shell (new; `edit_world_test --synthetic-shell`, also
  run at the end of the full test-edit-world): a synthetic multiloc in a
  cache-less provider, STATE_VAR_PREVIEW through `app_map_editor_var_preview`
  into the varps of an EDIT builder: 12 checks, 0 failed.
- test-chrome-widgets 1135 (1134 + "...and Delete") and its DOM test
  (Delete is GRAPH_KEY -DELETE); runtime.js and runtime-ie8.js regenerated
  with tsc 6.0.2 (the three-line diff of the source); the plugin_chrome node
  tests pass.
- test-editor-hotkeys 2296 with the doc test, zero conflicts;
  test-editor-workspaces 396 (the Q filter's check now expects both graph
  rows); test-editor-chords 591; test-editor-groups 2190;
  test-editor-workbench 295 (its digests unchanged); test-editor-picker 407; test-editor-scenes-ops 91;
  test-editor-encounters 121; test-editor-shop-quest 115 / 54 / 86;
  test-editor-catalog-finder 202; test-debug-overlay-visual; test-uitree;
  check-tree-walks and check-pt-switch pass.
- Seen to fail before the fix (mutations in this private worktree, each
  restored byte for byte): the preview loop removed (test-varp 5 checks,
  test-edit-world-shell 3), Delete not handed on (chrome widgets 1, the DOM
  test with the base runtime.js), and fifteen builder mutations each failing
  its own checks (ports walked in reverse, pos= not written, the rule at >=,
  a page per line, one-arm and orphan rows off, a drop that does not link,
  Shift ignored, Ctrl+L refusing, no orphan names, the slider only on
  release, no Problems report, the outline not selecting, no page rule, a
  caption not fitted).
- Not run, for want of the OSRS-Content checkout: builder_test's shell
  sweep and compile section (so no generated .rs2 was compiled by sscompile
  here: section 16's "the generated .rs2 compiles" check is open), the full
  test-edit-world, test-editor-panels and the other content-tree tests. No
  photograph (section 16's "a preview slider value changes the multiloc
  child in a photograph"): no cache, no display.

What is open:

- The head model in the preview: `chat_head` is not bound (it wants the
  npc's chathead models and an expression seq rendered to a scene sprite,
  the catalog preview's raster path).
- The chat box's numbers (380 px, 4 lines, p12_full) are the editor's until
  someone reads `~chatnpc`'s `split_init` in OSRS-Content; the line rule
  itself is the client's.
- The Problems seam is a stand-in: the integrator joins it to part 1's panel
  (and may move the header); the loc builder runs no lint (it has no graph
  and no lint list), so only the NPC builder reports.
- The node's Text field is a one-line input (191 bytes, pre-existing); the
  pages well shows the model's longer page when the field is at its cap but
  such a page cannot be edited in the form.
- The graph has no zoom or pan of its own (a graph bigger than the well
  scrolls with the panel); port drags are the chrome's existing GRAPH events,
  so no new executor command was needed.
- A subtree move takes everything a node reaches, a loop back to an
  ancestor included.
- Check still runs sscompile synchronously (part 1's BUILD kinds are where
  that moves).

## Workbench step 6.6 (2026-11-06)

The integration of build-order step 6.6 onto v3-workbench `03a006ed`, merged
with `--no-ff` in this order: records and problems (part 1,
`worktree-wf_e50eb21e-54f-1` e0c0983e) then the dialogue graph, the chat box
and the shell preview (part 2, `worktree-wf_e50eb21e-54f-2` d04523f2). The
two parts' sections above say what each built; this one says what landed
together, how the seams were joined and what was run. With it, design
section 9 ("Records and triggers") and, of section 10, "Graph ergonomics"
and the Shell preview of "Steps and shells" are built, minus what "What is
open" lists (the step list's drag reordering, op sections and templates are
not: see there).

What landed:

- **Typed record rows** (editor_cfg.{c,h}): `enum Editor_CfgFieldKind`
  (TEXT / NUMBER / BOOL / NAME / ENUM / PARAM), `Editor_CfgRegisterValueKind`,
  `_Range`, `_Words`, `_RefType`, `_ClientSide`, `Editor_CfgValueIssue`,
  `Editor_CfgRecordType`, `Editor_CfgRecordRowMatches`, `_RowChanged` /
  `_RemovedSince`, `_LayerStates`. Each row carries its register field, kind,
  client side and issue (UNKNOWN_KEY only for a server overlay's key,
  UNKNOWN_PARAM, NOT_NUMBER, OUT_OF_RANGE, NOT_BOOL, NOT_WORD).
- **The Record panel** (edpanel_record.c): Cache block, one tab per server
  overlay file, Triggers, Shell, Where used (`Editor_Record_TabCount` /
  `_TabTitle` / `_ShowTab` / `_SelectField`); source badges, "!" and "*";
  Find, "changed only", the dirty line; the typed editor per kind; each Set
  or Remove one undo step. Its Triggers tab's Check is the Triggers panel's
  CHECK job (`Editor_Triggers_Check`).
- **CATALOG_USES** (request 44, fact 165, `TORIRSMAPED_CAP_CATALOG_USES`
  512; `Editor_HostMapEdCatalogUses` / `_CatalogUsesFree`): a 4 ms, 256-row
  slice per request with a cursor, asked once a frame by Where used.
- **Check and CS2 compile as BUILD jobs**: `TORIRSMAPED_BUILD_CHECK` (3,
  `make maped-script-check`, the tool editor/builder/builder_ssc_main.c) and
  `TORIRSMAPED_BUILD_CS2` (4, `make maped-cs2-compile`, one script name;
  `ToriRSMapEd_BuildWhatTakesArg` / `_BuildArgValid`),
  `TORIRSMAPED_CAP_BUILD_CHECK` 1024. The client helper
  `Editor_BuildJobSend` / `_Poll` / `_Why`; the Live log's per-line build
  mark `Editor_LiveLogWhat` / `EDITOR_LIVE_LOG_DONE`;
  `Editor_LiveEncodeBuildArg`. The Triggers panel's Check and the interface
  editor's Compile return at once.
- **The Problems panel** (module "problems", edpanel_problems.c; the bottom
  group Console / History / Problems, in front in Script; `view.problems`).
  Build lines that read as diagnostics (`Editor_ProblemsParseLine`) are rows
  filed under the build's name; the push API is `Editor_Problems_Report`,
  `_ReportRecord`, `_ReportOpen` (new here, below), `_Clear`, `_Count`,
  `_Row`, `_CountOf`, `_Activate`.
- **The dialogue graph** (builder_panels.c, builder_model.{h,c}): out-ports,
  drag a port to link, drop on empty space for the new-node chip row,
  Shift+drag moves a subtree, Ctrl+L auto-layout by depth (`pos=` in the
  `.interaction`), `graph.new_node` (N, context Q), Delete on a focused graph
  (`edit.delete`'s chords, native and DOM; runtime.js / runtime-ie8.js
  regenerated), outline and graph selection in step, chords on the buttons,
  measured captions.
- **The chat box** (builder_chat.{h,c}): the client's splitter over p12_full,
  380 px and 4 lines a page, `{player}` as a 12-character name; the pages
  well and the page preview under the Text field.
- **Lint** (builder_lint.{h,c}): literal player name, orphan node, choice
  with one arm, unknown var, unknown proc (the `name_known` service in
  builder_session.c; unchecked without a catalog), with the dialogue
  window's find box and list.
- **The shell preview in the world**: the Shell editor's slider publishes
  STATE_VAR_PREVIEW while dragged; `VarPManager_SetPreview` makes every
  multiloc / multinpc resolve use the previewed value for exactly that var
  and re-resolves placed locs and npcs (`app_map_editor_var_preview`,
  `WorldBuilder_ResolveLocId`).

How the seams were joined:

- **Part 2's Problems stand-in is gone.** src/editor/panels/
  edpanel_problems_api.h and builder_panels.c's weak console default are
  removed. They declared `Editor_Problems_Report(ui, source, rows, count,
  open, user)` and a `void Editor_Problems_Clear`, a different ABI from part
  1's `Editor_Problems_Report(ui, source, file, line, severity, text)` and
  `int Editor_Problems_Clear`. builder_panels.c included only the stand-in
  header, so the merged tree compiled and the client would have linked part
  1's strong definitions to part 2's calls. The two never met in one test.
  The builder now includes edpanels.h.
- **`Editor_Problems_ReportOpen(ui, source, file, line, severity, text,
  open, user, index)`** (edpanels.h, new): a row whose click runs the
  reporter's own `Editor_ProblemOpenFn` with its index, before the record /
  file opens `Editor_Problems_Activate` already did. A row's source is now
  `EDITOR_PROBLEM_SOURCE_MAX` (96) bytes, so "npcbuilder:<npc>" fits. The
  reporter keeps `user` valid until it clears its source; every reporter
  clears it on destroy.
- **The builders' lint rows**: the NPC builder clears its source
  ("npcbuilder:<npc>") and reports each lint row as
  "<where>: <kind>: <message>", and only when the rows changed (the hash part
  2 kept). A click selects the row's node, or its trigger step.
- **The shop builder's Check** (edpanel_shop.c): every problem Check (or a
  refused Save) finds is a "shop" row, "shop <key>: <problem>", with the
  shop's .rs2 as its file ("No catalog: ..." a note). A click brings the
  Shop builder to the front with that row picked. Opening another shop, a
  Save and the panel's destroy take the rows back.
- **The quest board's gate job**: the board reads each of its job's own
  lines (by the Live log's build mark, oldest first, not only the newest).
  gate.py's findings ("    - <finding>" under a RED quest) are error rows of
  "quest:<id>", its "    ~ blocked: <row>" lines warnings; a gate that ends
  red or blocked with no finding line leaves one row that says so. A new Run
  gate takes the last run's rows back. Located diagnostics in the job's
  lines are not reported again: the Problems panel's line stream already
  files them under "quest gate". A click brings the board to the front.
- **The Record panel's Triggers tab** already called the Triggers panel's
  Check (part 1); a test now presses it and sees the CHECK job sent.
- **MAPED_HOTKEYS.md**: part 1's View row (`view.problems`) and part 2's
  Graph row (`graph.new_node`) conflicted on adjacent lines; both are kept.
  The doc test passes.
- **Fixed in the merge**: `bp_new_node`'s `linked` was unused under
  NDEBUG (a warning in `make all`); the quest board's verdict row, inserted
  between an `else if` and its `else`, made a successful gate report
  "failed" (test-editor-shop-quest-panels caught it, 2 checks).

The test-editor aggregate:

- **It links here.** test-accounts, test-catalog, test-live-editor and
  test-torirsmaped-embed failed to link with `undefined reference to pow /
  floor` (the accounts module's XP table in the authority); their recipes
  now end in `-lm`. test-builder's already did (part 2).
- **Content members are skipped by name.** Each fragment now adds its
  content-tree members to `EDITOR_TREE_TESTS` (or `EDITOR_CACHE_TESTS` for
  tree and cache) instead of naming them as `test-editor` prerequisites;
  src/makefile adds them to the aggregate only when
  `$(EDITOR_CONTENT_DIR)/maps/m50_50.jm2` (and `$(EDITOR_CACHE_DIR)`) exist,
  and otherwise prints one line per member: `test-editor: SKIPPED
  test-editor-panels (needs the OSRS-Content tree: no .../m50_50.jm2)`. Tree
  members: test-catalog, test-editor-cfg, test-editor-alloc,
  test-editor-vars, test-spawn-codec, test-editor-if, test-iface-editor,
  test-live-editor, test-cp-incremental, test-live-panel, test-editor-script,
  test-editor-panels, test-editor-views, test-editor-jm2, test-editor-save,
  test-torirsmaped-embed, test-torirsmaped-daemon. Tree and cache:
  test-edit-world, test-editor-derive. Every one stays its own target and
  still fails when asked for without the tree. test-builder stays in: its
  content sections skip themselves.

Tests (this container, build_opt, no OSRS-Content, no cache; 'ulimit -s
unlimited'):

- `make -C src all torirsmaped torirsmapedctl` builds; no new warning in a
  file either part or the merge touched.
- `make -C src test-editor` passes (exit 0) with the 19 content members
  named as skipped. Its members, with counts: test-accounts 140,
  test-editor-panels-accounts 48, test-editor-areas 136, test-builder 243
  (content sections skipped), test-editor-catalog-finder 202,
  test-catalog-perf 143, test-catalog-finder 176, test-chrome-widgets 1135
  plus the DOM test, test-chrome-skin-tiers 922, test-edit-world-shell 12,
  test-editor-groups 2192, test-editor-encounters 121,
  test-editor-encounter-panel 91, test-spawn-suppress 12,
  test-editor-world-gesture 42, test-editor-hotkeys 2301 (the doc test
  included), test-editor-chords 591, test-editor-workspaces 397,
  test-editor-instance 76, test-editor-instance-ops 83, test-lanes 76,
  test-editor-palette 97, test-editor-panels-palette 50, test-editor-picker
  407, test-editor-placement 123, test-editor-records 78,
  test-maped-script-check 10, test-editor-records-panels 125,
  test-editor-scenes 507, test-editor-scenes-ops 91, test-editor-shop 115,
  test-editor-quest 54, test-editor-shop-quest-panels 93,
  test-editor-stamps 91, test-editor-stamps-ops 81, test-editor-workbench
  295, test-editor-worldmap 161, test-editor-cmd 66, test-editor-dropdown
  36, test-convex-hull 19, test-polygon-fill 14, test-editor-views-web.
- New checks here: test-editor-records-panels 121 -> 125 (a pushed row's
  own click with its index and a 46-byte source; its Clear; the Record
  panel's Check sends the CHECK job), test-editor-shop-quest-panels 86 -> 93
  (each shop Check problem a "shop" row, named, its click back to the Shop
  builder, another shop clearing them; the gate's finding and blocked lines
  as "quest:<id>" rows, a click bringing the board up, a new run clearing
  them). test-editor-hotkeys is 2301: part 1's 2293 and part 2's 2296 over
  2288, both rows. builder_test's Problems stand-ins now define
  `Editor_Problems_ReportOpen` / `_Clear` with the panel's signatures.
- The joins were mutated in a throwaway worktree (never this checkout):
  dropping the ReportOpen branch in `Editor_Problems_Activate` fails 1
  records-panels check; dropping the shop's report fails 3 shop checks;
  the quest board's broken else chain failed 2.
- Also run and passing: test-varp, test-debug-overlay-visual, chrome-skin
  922, check-pt-switch, check-tree-walks, check-app-boundary.
- Once, of nine runs, test-lanes (run as test-torirsmaped-daemon's
  prerequisite) failed 12 checks, every one about the change set's deletion.
  Six direct reruns and both aggregate runs passed; neither part touches
  torirs_maped_lanes.c. Noted, not chased.
- Not run (they need OSRS-Content or the cache): the 19 skipped members,
  builder_test's shell sweep and compile section, the photographs.

What is open:

- ~~The builder panel's Check (NPC and loc) still runs `Builder_SscRun`
  synchronously.~~ Now the CHECK job (Review fixes below). Its sscompile
  diagnostics are the builder's issues list; the Problems panel files the
  same job lines under "check" by itself.
- Design section 10's step list is not built: steps still move with Move up
  / Move down (no drag reordering), op1-op5 / ap1-ap5 / use-on are not
  collapsible sections, and there is no + menu of templates ("door",
  "ladder", "require level then give", "teleport with anim").
- The shell preview changes every world view of the App, not only the EDIT
  world, while it lasts: the Shell editor takes its var out of the preview
  on Stop preview, on a close and on opening another shell (Review fixes),
  but a window that quits with a preview published leaves it with the other
  windows until one of them clears it or enters a scene with an empty
  preview list.
- The loc builder runs no lint; only the NPC builder reports to Problems.
- The chat-box numbers (380 px, 4 lines a page, p12_full) are the editor's
  reading: replace them with ~chatnpc's `split_init` arguments from
  OSRS-Content. The chat-box head model hook (`Builder_Services.chat_head`)
  has no app-side renderer.
- The node Text field is a one-line input capped at 191 bytes; the graph
  has no zoom or pan of its own; a subtree move includes loops back to
  ancestors.
- ~~The server-side CS2 job passes only RuneStar's names tables~~ (the job
  now writes the content's script names first: Review fixes). The client is
  not told where the lane put the compiled script (`output_path` is empty).
- Where used reads files rather than an index (a cold search of the full
  tree is unmeasured, likely ~10 s of 4 ms frames); configs/all.<type> is
  never read; unsaved locs in open squares are not counted.
- The Record panel's Triggers and Shell lists have no search box (a shell
  can have 257 slots); "Copy to new..." and "Find in world" are not built.
- `Editor_Triggers_Check` returns 1 for "sent", not an error count;
  test-editor-panels (not run here) may expect the old value.
- The quest board's Problems rows are the gate's own finding lines; gate.py
  names no file or line, so a click brings the board up rather than a
  script. The shop's rows name the shop's .rs2 without a line.
- Nothing that needs OSRS-Content or the cache ran (list above);
  maped-script-check and maped-cs2-compile have never run for real; no
  generated .rs2 was compiled; no photographs.

Review fixes (2026-11-06). Nine findings on the integration; each was checked
against the code before it was fixed or recorded, and the four panel fixes'
new checks were each shown to fail with that fix taken back out, in a scratch
worktree (never this checkout):

- **N and Ctrl+L never reached the dialogue graph from the keyboard** (real,
  fixed). A press on the graph's well is a chrome focus (`dbg_ui.focus`), so
  `app_text_input_focused` was true and the App's hotkey tick returned before
  `Editor_HotkeysResolve` every time context Q could be live; the chrome
  hands letters back (`dbg_ext_key_char`) and the well's GRAPH_KEY carries
  only Escape / Delete / Backspace. builder_test only ever called
  `Builder_GraphHotkey` directly. Now `Builder_GraphKeys(ui, in, out)`
  (builder_panels.h) resolves the frame's down edges, with the held
  modifiers, against the Q rows alone (`Editor_HotkeysMatch`) while the
  dialogue graph holds the keyboard, and runs each through
  `Builder_GraphHotkey`. `app_hk_tick` calls it ahead of the text-field gate
  when the graph is focused, no plugin field or game text field
  (`app_text_input_focused_outside_chrome`, new) or model view holds the
  keyboard and no menu drop or minimenu is open; it logs and consumes as the
  resolver's path does. The App half has no harness; builder_test drives the
  rest through the real chrome model: the well focused, `ToriRSChrome_KeyChar
  ('n')` handed back, N and Ctrl+L from an `Editor_HotkeyInput`, a key no Q
  row binds left alone. The menu / palette path (`Editor_HotkeyRun`) is
  unchanged, and its EDIT_DELETE -> `Builder_GraphHotkey` branch stays (it
  answers 0 without the graph's focus); the well's own GRAPH_KEY is what
  deletes.
- **Any GRAPH well's focus made context Q live** (real, fixed with the
  above). The facts now set `graph_focus` from `Builder_GraphFocused(ui)`:
  the NPC builder's `w_graph`, not a palette's, a chip strip's, the Locs
  strip's or the picker's well (a press on the Palette and then N would have
  added a dialogue node, had the keys reached the resolver). New check: the
  pages well (another GRAPH) focused, N fires nothing and the dialogue is
  unchanged. MAPED_HOTKEYS.md's Q and Graph text and editor_hotkeys.h say so.
- **The NPC and loc builders' Check compiled on the UI thread** (real,
  fixed). `bp_check` regenerates the model's files into their `rs2:`
  documents (an unchanged model applies nothing) and sends
  `Editor_BuildJobSend(TORIRSMAPED_BUILD_CHECK)`; the server writes every
  unsaved `rs2:` document, the builder's generated ones among them, over the
  job's stage. `bp_watch_check` runs every tick, shown or not, takes the
  lines with `Builder_SscTakeLine`, and at the end files this subject's
  diagnostics (`edited/<name>/...`) as the builder's issues and counts the
  rest as "(more in Problems)"; the status lines fit the chrome's 64-byte label. A second Check while one runs is
  refused. The panels' own compile paths are gone (`Builder_PanelsSetCompile`,
  the panel's `Builder_Ssc`, the TORIRS_BUILDER_* fallback in
  builder_session.c and editor_panel.c's call); so builder_ssc.h's and
  torirs_maped_build.c's "no window calls Builder_SscRun" now hold.
  builder_test stands in for the job helper (`Editor_BuildJobSend` / `_Poll`
  / `_Why`, as it does for Problems) and checks the send, the immediate
  return, the refusal, lines arriving mid-job and the end landing while the
  builder is hidden.
- **The CS2 job lost the content's script names** (real, fixed). The job's
  argv now carries `MAPED_CS2_SCRIPTS=<tree>/scripts`; the
  `maped-cs2-compile` recipe first runs a new tool, `maped_cs2_names`
  (editor/iface/iface_cs2_names_main.c, built on demand like
  maped_script_check), which is `EditorCs2Catalog_Load` +
  `EditorCs2Catalog_WriteNamesDir` into `MAPED_CS2_DIR/names`: RuneStar's
  tables linked when `RUNESTAR_CS2_NAMES` exists, script-names.tsv from the
  content's .cs2 headers. `--names` is that directory; without
  `MAPED_CS2_SCRIPTS` it is RuneStar's alone, as before. Done in the job's
  process rather than in `jobs_build_prepare`, so the server's pump never
  walks the scripts directory and torirs_maped.c's many link lines take no
  new source. New: test-maped-cs2-names (a test-editor member, content-free)
  and a records-panels check that the job names the scripts directory.
  `cs2 compile` itself still has never run here.
- **Escape mid-drag left the dragged positions in the model** (real, fixed).
  Escape with a node or subtree drag in progress puts every dragged node back
  at `drag_orig_x/y`, says "Move cancelled: N node(s) back where they were"
  and redraws; the release then commits nothing. New builder_test check.
- **A failed Check's puts were staged again** (real, fixed).
  `jobs_build_prepare` empties `<live>/check/puts` recursively (symlinks
  removed, never followed) before it writes the dirty documents.
  records-panels: a planted stale put is gone after the next Check, the
  still-unsaved script is put again.
- **`Editor_CfgValueIssue` took a NULL field** (real, fixed). It asserts
  `field`; both production callers already test for a register row first.
  The NULL-pinning lines in editor_records_test are gone; the bool check uses
  the register's `nomove` row.
- **The integration claimed design section 10's "Steps and shells"** (real,
  fixed in this doc): the claim is narrowed above and the step list's
  missing drag reordering, op sections and templates are an open item.
- **The shell preview outlived the Shell editor** (real, fixed in part). The
  panel remembers the var it published; Stop preview (new button), closing
  the Shell editor and opening another shell publish the preview list
  without it, so every world view resolves that var from its real value
  again. builder_test: Stop, a second Stop publishing nothing, a close, an
  open. What still lingers is in "What is open".

Tests after the fixes (same container and flags as above): `make -C src all
torirsmaped torirsmapedctl` builds with no new warning; test-builder 266
(243 + 23: the keyboard path 9, Escape 3, Check 6, the preview 5),
test-editor-records 78 (the two NULL checks gone, the bool one on a register
row), test-editor-records-panels 128 (+3), test-maped-cs2-names (new),
test-editor-hotkeys 2301, test-editor-chords 591 (it caught the first Stop
preview tooltip at 84 bytes, cut to 63; shortened). `make -C src test-editor`
passes (exit 0) with the same 19 content members named as skipped, and every
other member at the integration's count. Also passing: test-varp,
test-debug-overlay-visual, chrome-skin 922, check-pt-switch,
check-tree-walks, check-app-boundary. Not run, as before: the content and
cache members, builder_test's shell sweep and compile section, and any real
`maped-script-check` / `maped-cs2-compile` run.

## Windows run (2026-11-06)

The first run of the workbench on a real machine with the content tree and
the dat2 cache: Windows 11 Pro (26200), 2560x1080 at 100 %, a precision
touchpad and no touchscreen, OSRS-Content at 426eb88 checked out by Git for
Windows (core.autocrlf=true, so the tree is CRLF), the win64 lane
(`build_windows.ps1 -Opt`, MinGW-w64 GCC 16.1, D3D9). Photographs are in
`build/photos/` (not committed; `w*.png` the editor, `p*.png` the play
client, `v*.png` a popped-out window).

### The owner's direction

The editor must run on modern Linux, macOS, Windows (x86_64) and the web,
through the platform layers the repository already has, not beside them. On
this branch the editor server called POSIX directly behind a
TORIRSMAPED_JOBS_NATIVE that meant "not Windows", so on Windows it compiled
to "not available": RUN, BUILD, Check, the live link, the play client, Pop
out and the torirsmaped daemon. It now uses:

- **Processes**: `LaunchSessions_Spawner` (platform/launch_sessions.h:
  posix_spawn on Linux and macOS, CreateProcess in a job object on Windows),
  handed in by the host (`ToriRSMapEd_SetSpawner`, `Builder_SscSetSpawner`,
  `EditorCs2_SetSpawner`). A test or the web that sets none binds no native
  runner, as before.
- **Sockets**: the game server's pattern (torirsserver/torirs_server_host.c):
  winsock beside BSD sockets in the same file, a socket an int either way.
- **Files**: MoveFileEx(REPLACE_EXISTING) for an atomic replace, as
  platform_x_io.c and the server's savefile already did.
- The web is unchanged: a page is a client of the daemon.

### What was found, and fixed

Each was found by running the editor or a RUN, reproduced, fixed at the
cause, and is in the commit named.

- **The win64 lane did not compile** (torirs_maped_build.c: `build_read_whole`
  behind the POSIX-only guard). e77a6895e.
- **The editor disabled itself at boot**: a lane is symbolic links, which an
  ordinary Windows account may not make (Developer Mode off), so the
  working lane came up empty. The mirror now falls back to a directory
  junction and a hard link; lstat reads a junction, and a file with more than
  one link, as a link, so the changes scan is unchanged. The hard-link
  difference: a publish that replaces a base file leaves other lanes' links
  on the old bytes. 8595b7956.
- **Every save of an existing file failed** (rename onto an existing target,
  which Win32 refuses): the host's document and square saves, the accounts
  file, the build's map list, the lane's file copies. 8595b7956.
- **A save rewrote every line of a CRLF file**: a one-tile brush on
  m50_50.jm2 went from 121244 bytes CRLF to 115020 LF. The host now reads
  CRLF as LF and writes CRLF back when the file it replaces is CRLF; the same
  brush is a 39-line change of the same size. This holds on every platform
  (a tree copied from a Windows checkout is CRLF anywhere). 0c04ddbf2.
- **A dead editor's lock left every later session read-only on Windows**
  (`lock_pid_alive` answered "alive" for every pid). 0c04ddbf2.
- **The lane's live dir was 'C:\repo/C:/repo/build/lanes/working/live'**: a
  drive-letter path was read as relative. 233820ecd.
- **RUN's build died in the Python step** ('The handle is invalid'): the
  Windows spawner gave children a NULL stdin. e241b09f5.
- **cachepack relinked on every make** (its rule named no .exe) and, under
  RUN's -j2, while the other branch was running it. 0c495d046.
- **build_packs.py could not start sscompile** (a relative '/' path,
  WinError 2), **sscompile overflowed Windows' 1 MB stack** (0xC00000FD),
  and **a '\'-separated --src made the base pack take the lanes' scripts**
  (100 units failed). 0c495d046.
- **The lane's game server refused the control link** ("not supported on
  this platform": winsock cannot dup()); it keeps a WSADuplicateSocket copy.
  0c495d046.
- **Pop out answered 'unsupported'**: the launch service refused every verb on
  Windows because the party link is not ported; only the party verbs need it.
  e241b09f5.
- **The eight job-object slots were never freed** (a long-lived editor
  server would stop spawning at its ninth build). e241b09f5.
- **The Lanes panel's three tooltips were cut at 63 bytes** (the console said
  so at every boot); shortened. 266e13351.
- **The Stamps panel was open at every boot**, floating over the World:
  Editor_UiSetVisible shows every titled panel and Stamps had no set_visible
  hook to keep itself closed (Record, Triggers, Lanes have one). Found with a
  headless probe of the chords test's boot; editor_chords_test now checks it,
  and editor_workbench_test's three registry digests, recorded with Stamps
  open since 6.3 part 2, were recorded again (Linux and Windows agree).
  266e13351.
- **"Open in editor" (Triggers, Record) answered unsupported on Windows.** It
  now runs the template through cmd.exe, detached: `{path}` is "..." there,
  and the default without VS Code is `start "" {path}`. 1fa728e75.
- **`make` recipes and Windows**: GNU make on Windows runs a line with no shell
  metacharacter itself, so `! grep ...` (records.mk) was "file not found";
  written as `if ...; then exit 1; fi`. d72684cd6.
- **Windows displays above 100 %**: the backend was not DPI aware, so Windows
  stretched the finished frame and every glyph blurred, whatever the vector
  face did. It is now per-monitor DPI aware (looked up at run time; the XP
  lane unchanged): the window opens at points x DPI, the density reports the
  monitor's scale, WM_DPICHANGED takes Windows' suggested size. Not seen at
  125-175 % here (see below). c597cf202.
- **Every object rebuilt on every make, on Windows**: tools/objverify.py split
  a .d at its first colon, which on Windows is the drive's (`C:/.../x.o:`), so
  each object was its own missing prerequisite and was deleted. The tools'
  cachepack then read as a new binary each run and the incremental client
  pack repacked every type (test-cp-incremental, "all reused"). c404d7d6e.
- **The CS2 compile saw no obj/npc/loc names on Windows** (the names
  directory's RuneStar links were POSIX-only; a link, a hard link, else a
  copy now), and **the checked-in cs2.exe predated the osrs241 profile**
  ("Unknown revision profile"). 5ab706f2f.
- **Prebuilt binaries were checked in**: 127 build outputs (the rscache tools,
  tests, benchmarks, servers), mostly macOS arm64 builds. On any other OS a
  checkout restores a wrong-format file stamped newer than its objects, so
  make never relinks it: Linux ran a Mach-O cachepack ("Exec format error"),
  Windows the stale cs2.exe. Untracked and ignored (the vendored third-party
  DLLs, the JDK installer and blobs/ stay); test-iface-editor, the one rule
  that ran cs2 without building it, builds it. 10d8e48d9, 4e137c76a.
- **editor_panels_test had never passed with content**: 94 failures and a
  glibc abort on Linux, 99 on Windows, at the pushed tip. Two editor bugs
  under most of them: **Problems opened as a floating window with every
  editor show**, over the World panel's title bar (its Pop out and drag
  landed on Problems; it now opens on demand, as Stamps), and **the default
  layout hid the square picker at 1280x800** (its least height counted the
  detail pane that folds, and the well's least left it 31 px over the 758 px
  column under Terrain and Palette; the folding rows count with the well and
  the well's least is 88). The rest were checks behind deliberate changes
  (the Area dropdown's lane rows, Publish titled Lanes, Check as a BUILD
  job), the picker checks' assumptions about a tall well, and a `realpath`
  into 2048 bytes that glibc's fortify aborts. 4f577f8f2.
- **A killed play client read "exited (status -1)"**: the spawner port lost
  the signal (the live code's contract is minus the signal). 76b66da0a.
- **cfg-copy (Copy to new) crashed the editor server** in any tree with no
  `ported/` lanes: the allocator sorted a NULL lane list with qsort, whose
  base is declared nonnull, and under LTO GCC dropped the NULL test in the
  inlined ToriMem_Free after it (a crash at -O3, fine at -O0; found with an
  ASan build of the daemon). The same shape in editor_vars.c. And **make
  never relinked torirsmaped**: the target shares its name with the sources'
  directory, so the daemon test ran whatever server was built last; phony
  now. 5df2288ec.

### What now runs on Windows, seen in the window

- Boot: the working lane mirrors (the first time, while the window waits:
  about three minutes for ~150 k links here), the world renders, the catalog
  indexes (63,307 locs), every panel group draws.
- F1 (Hotkeys, 167 actions, 0 conflicts), the brush tools (Raise r2 with its
  footprint), Ctrl+S, the picker's Find "3222 3218" (pins the tile, Enter
  opens it), the Squares atlas.
- RUN (Ctrl+Return): save, the packs (base and lanes script packs, the
  server pack) and the client cache, then the lane's torirsserver and
  js5_server launched, the control link opened, the play client logged in as
  maped43620 and in the world. A second RUN after a brush edit: "reload: 1
  square(s) current, 1 player(s) owed a REBUILD; client cache: 1 archive(s)
  invalidated, 1 send(s)", in the same play client, no relog.
- Pop out (the Catalog group): a view process on the loopback listener,
  "a popped-out catalog panel of Client 1", the whole catalog in its window.
- torirsserver, torirsmaped and torirsmapedctl build; Linux still builds the
  same sources (checked in a WSL clone).

### Open, from this run

- **Display scales 125-175 %** were not tried: this machine's display is at
  100 %, and changing the Windows display setting is not something this
  session does. View > Chrome scale works in-app; the OS-scale pass is the
  owner's.
- **A floating panel draws under a docked group's title bar and tab strip**
  (the Hotkeys panel over the Console group: its rows behind the strip are
  hidden). The chrome draws, and hit-tests, panels in array order, and a
  group's frame is created at layout time, after the floating panel. A fix
  is one z-order list shared by Build and the eight reverse hit-test walks
  in uitree_debug_overlay.c (and the web executor), with a raise on open and
  on press.
- **The first lane mirror blocks the window** for minutes on Windows (hard
  links one at a time, the UI thread).
- **The game server's party link** (ToriRSServer_EmbedPartyListen) is still
  POSIX-only; only the launch service's party verbs refuse.
- **An empty environment variable cannot be set on Windows** (the C runtime
  removes a variable set to ""), so the "set but empty = off" convention
  (TORIRS_EDITOR_PREFS, TORIRS_EDITOR_LAYOUT, TORIRSMAPED_LIVE_DIR, ...)
  cannot be spelled there; such a variable reads as unset.
- **The test suites on Windows**: below.

### Built from the "What is open" lists

- **The Record panel's Triggers and Shell lists have a Find box** (6.6): one
  input over whichever list shows, case-insensitive over the row's text (a
  trigger's kind, subject, file; a slot's value or child), with a shown-row
  map so Open in editor still opens the trigger picked.
- **The Record panel's "Find in world" and "Copy to new..."** (6.6): in its
  header on every tab, on the open record. Find makes the record the pick
  (Editor_Catalog_PickRecord) and runs the Catalog's Find in world, leaving
  the tool as it was; Copy is Editor_Catalog_CopyRecord to New name.
  editor_records_panels_test covers both and the Find (140 checks). 762031b25.

### The path painter (the owner's request, 2026-11-06)

"A terrain painter tool that helps paint paths ... dirt paths (irregular
shapes) and more straight pavement like paths ... put the overlay shape so it
doesn't look jagged."

**What the content's paths are.** Every surface square's level 0 was read
and the overlays ranked by how path-like they are (many tiles whose short
run is 1..6 tiles across with a long run well past it); the water overlays
lead (rivers are paths too), then the browns (jm2 ids 14, 22, 26, 80, 106:
dirt tracks) and the greys (10, Lumbridge's stone road). Rendered top-down
with the client's own tile geometry (each shape's overlay faces from
world_decode_tile.c, rotated as the client rotates them), the road is two to
three tiles across, its bends made of half diagonals, the 1:2 slopes (shapes
2-5) and fans; a trail is under a tile across, half squares and diagonals.
About a third of a track's tiles are edge shapes; every shape but 11 (the
trapezium) is used on them, the half diagonal most.

**The planner** (src/editor/editor_path.{h,c}, no document access): the
path is a region in continuous tile space -- a centreline and a width --
and every tile it touches is sampled on an 8 x 8 grid and given the overlay
shape and rotation whose coverage matches best (or left bare). Then a few
passes let each tile change to the shape that both covers it and carries its
neighbours' edges on (the sample rows along each shared side compared,
weighted 2.5 against coverage, chosen by eye: whole tiles stop sticking out
of a sloping edge, and a heavier weight starts to spike edges out). A tile
that already holds the overlay is joined: its coverage and the new coverage
are fitted together, so a second stroke or a fork meets the first with no
seam.

- **Dirt**: the drag's tiles, corner-cut three times (Chaikin) into a curve,
  resampled every quarter tile; the centre drifts sideways and each side's
  half width breathes on its own (seeded two-octave value noise along the
  arc, Roughness 0-100 %); every shape 0-10.
- **Pavement**: the drag's corners (Douglas-Peucker, one tile), each leg
  turned to the nearest 45 degrees and stepped in whole tiles from a start
  whose edges sit on whole tiles across the first leg; a diagonal leg's width
  rounded so its edges run corner to corner; bends mitred; the ends square to
  the far edge of the tiles clicked. Only shapes 0, 1, 6, 7, 8: a straight
  road is whole rows (a half width ends in a row of half squares), a
  diagonal one half diagonals round whole tiles.

**The tool**: Paint path (`tool.paint_path`, H, on the rail). The drag
collects the tiles crossed (a jump between frames is filled in); the overlay
previews the plan (edge tiles stronger); release lays it with the palette's
overlay as one undo step; Escape drops it. The Terrain panel shows its
section while it is armed, in the brush's place: dirt / pavement, Width in
half tiles (`[` `]` step it), Roughness, New edges (another seed; each laid
path also moves the seed on). Tiles in squares not open are left out and
counted in the status line.

Tests: editor_path_test (`make -C src test-editor-path`, 249 checks: every
shape's coverage and its turn, each style's shapes fit themselves, pavement's
3-wide / 2-wide / 1.5-wide / diagonal roads and a mitred bend, a dirt track
continuous with a third of its tiles edge shapes, seeded, a one-wide trail,
the same stroke again changing nothing and a fork taking nothing from the
track; `PATH_PLANS=<file>` writes the plans out). By hand on m50_50: the
road's overlay picked with I, H, a drag from the road to the river: "Path:
35 tiles laid, dirt 2.0 wide", joined to the road without a seam, one
Ctrl+Z restoring all 35 tiles.

### Walls, fences, scatter, roughen, plateau (the owner's request, 2026-11-06)

"Make sure this editor supports other common features in game editors like
the Warcraft 3 editor ... walls, fences builder, etc." and "a tool for
creating flat areas for buildings ... a mound or hole that creates a flat
area with smoothed mounding on the edges."

What the editor already had against that list: terrain brushes (raise,
lower, flatten, smooth, with radius, falloff, step, strength), the paint
brushes and the path painter, the rectangle (copy, paste, fill, clear,
stamps that turn), the Place tool's WALL RUN (a drag lays one wall piece a
tile, corners at the turns), loc multi-select with nudge, rotate, reshape,
duplicate and swap-to-pick, spawns, areas (regions), scenes (camera
bookmarks), the catalog palette (a doodad palette), triggers, the object
editor (Record panel), the interface editor, undo and history. What it
lacked, built here:

- **Fences and walls meet what is there** (edpanel_ops.c, the wall run's
  judge): with Fit on (G), a straight piece landing on a tile where the same
  loc already stands as a straight wall on a perpendicular edge becomes the
  L of the two edges -- a run's end joins a wall already built, and a run
  crossing one keeps both -- and a piece landing on its own L that already
  carries the run's edge is kept. Without Fit the piece replaces, as before.
  `Editor_LocRunPlanMake` takes `fit`; the plan records `fit`, `mask`.
- **Diagonal fences** (`constrain` 2): with the diagonal wall (shape 9)
  picked, Shift keeps the drag to a diagonal -- `Editor_LocRunTo` builds
  the staircase from the first tile -- and the plan lays shape 9 on every
  second tile, turned to the diagonal (rotation 1 north-west / south-east,
  0 south-west / north-east, as Fit reads diagonal neighbours), nothing on
  the tiles between. The same pick on a plain drag is the wall run it was.
- **Roughen** (`tool.noise`, U; `EDITOR_TOOL_NOISE`): seeded height noise
  under the brush, the step its amplitude, the falloff fading it to the rim;
  one seed is one relief (the same plan twice is the same relief), and each
  stroke draws a new seed (`tools.noise_seed`), so a second drag is not the
  first undone. Rides in `Editor_BrushCommands` as the other brushes do.
- **Scatter** (`tool.scatter`, B; `EDITOR_TOOL_SCATTER`;
  `Editor_ScatterCommands`): the doodad brush. The SET is the selected locs'
  distinct ids (select a few trees, then paint a mix of them), else the
  catalog's pick; each tile under the brush draws from (seed, tile) and
  below Density x falloff takes a loc of the set, turned at random when
  "Random rotation" is on, placed as the record's first shape with the tile
  as its footprint's south-west corner -- unless the footprint touches a
  closed square, a tile this stroke already planned, or a loc there (a
  centrepiece covers its footprint, read with the set's sizes). So a drag
  never stacks, and the same stroke over it again adds nothing. Alt-drag
  ERASES: every loc of the set on the brush's tiles. Terrain shows Density,
  Random rotation and New seed while it is armed (Shift+[ ] step the
  density); the status line says when there is nothing to scatter.
- **Plateau** (`tool.plateau`, V; `EDITOR_TOOL_PLATEAU`;
  `Editor_PlateauCommands`): the owner's flat area for a building. The
  brush's disc goes to the height of the tile the stroke began on (held over
  the drag, as Flatten's target is), and a SKIRT of N tiles around it (0..8,
  Terrain's "Skirt (tiles)", Shift+[ ]) is eased from that height to the
  ground as it is: a tile d tiles out goes to own + (top - own) x
  smoothstep((N + 1 - d) / (N + 1)), so the slope leaves the top flat and
  meets the ground tangentially -- a mound where the ground is lower, a
  hole where it is higher. The Rect panel's **Plateau** button does the same
  for the rectangle, to the latched tile's height (Select's click; else the
  rectangle's first corner). So the workflow asked for: raise or lower a
  tile to the level wanted, latch it, Plateau the rectangle (or drag the
  brush from it).

Tests (editor_placement_test, 169 checks): a run ending on a perpendicular
wall becomes the L with Fit and the straight piece without, a third run
keeps the L, one undo restores the wall; a Shift diagonal drag plans seven
tiles with four shape-9 pieces at rotation 0, the south-east one rotation 1,
a plain drag with the diagonal picked stays a wall run; Roughen moves every
tile of the disc within the step (plus the ladder's snap), the same seed
plans the same and another seed another relief; Scatter at 100 % fills the
disc as far as footprints allow with adds of the set, none crossing, some
turned, a second pass adds nothing, erase takes exactly what was dropped,
each stroke one undo step, a tenth of the density far fewer and none turned
with rotation off; Plateau puts the nine tiles of the top at the height, the
skirt eases to the ground and stops at it, one undo, and the rectangle's
corners are on top with the tile two out untouched. editor_hotkeys_test
(2504) covers the three actions, their chords and their doc rows;
editor_panels_test (1933) and editor_workbench_test (294, the registry's
goldens re-baselined for the three new rail buttons) pass on Windows; on
Linux the same four pass (169, 2504, 1934, 295). c8403a210.

By hand on m50_50 (dist/win64/torirs.exe, 1280x800): the Terrain panel
shows "Scatter r2 smooth 20%" with Density, Random rotation and New seed
while B is armed, and "Plateau r2 skirt 3" with the Skirt slider while V
is; oaktree picked in the Catalog, a Scatter drag over the grass dropped
oaks (3x3 footprints, none crossing) along it, one undo step; a Plateau
press on a raised hill applied (the top-down camera hides the relief --
the skirt's shape is the planner's test). With nothing selected or picked
the status line reads "Scatter refused: select locs to scatter, or pick
one in the catalog".

Found on the way: with 21 buttons the rail's default layout at 2x
overlapped the console strip at 1280x800 (editor_panels_test) -- the
column count was chosen from the one-column height divided by the columns,
which undercounts the strip's padding. The layout now counts the rows the
buttons wrap into (`Editor_ToolRail_ButtonCount`) at the button pitch plus
the measured padding.

Open: the brush footprint drawn under the pointer shows the radius, not
the Plateau's skirt; Scatter reads a loc's size from its record only once
it is resident (one tile until then); a Scatter set is the window's
selection, not shared with other windows.

### Underlay blending: the colour picker, the blend strip, the mix (the owner's request, 2026-11-06)

"Do you have a color picker for overlay and underlays to help with
blending underlays? Look at the underlay painter in other editors for
games and the LostCity map editor."

What blending IS here. An underlay record is one colour
(configs/all.underlay `colour=0xRRGGBB`, no name in the OSRS content), but
the ground is never that colour: the world builder averages the HSL of
every underlay within five tiles (engine/world_builder/blendmap.c, an
11x11 window, the hue as a chroma / luminance ratio), packs the average to
the client's HSL16 (64 hues, 8 saturations, 128 lightnesses) and draws it
through the palette table at brightness 0.8. So two underlays "blend" when
that average walks a short, even path between them; a seam between a
green and a purple is a muddy band five tiles wide. The painter's work is
choosing records whose packed colours sit close, and laying the seam so
the average has room. LostCity's own editor is not public (its
organisation lists no map editor), so the comparison is with the Warcraft
3 / Hammer-style texture painters: a palette of every tile type with
thumbnails, a soft brush that blends one type into another, and a
readout of what is under the pointer. What the Palette had: the open
squares' underlay and overlay ids as colour swatches, the shape thumbnails,
an id field, and the eyedropper (I).

What was built (`workbench 6.10`, src/editor/editor_floorcolor.{h,c},
src/editor/panels/edpanel_palette.c):

- **The floor colour maths, headless.** `Editor_FloorHslOfRgb` (the
  client's HSL24), `Editor_FloorHsl16` (its packing), `Editor_FloorRgbOf
  Hsl16` (the palette table's colour, with the gamma), `Editor_FloorShown`
  (a record's colour as a plane of it is drawn: through the blend, whose
  hue is the chroma / luminance ratio, a unit off the HSL24 hue now and
  then), `Editor_FloorDistance` (lightness double, hue by the lesser
  saturation) and `Editor_FloorBlendStrip` (the ground across a straight
  seam between two planes, tile by tile, the exact window sums). Copied
  from the client rather than linked so the panel needs no palette table;
  editor_panels_test checks the HSL24 and HSL16 against palette.c's.
- **Hover readouts.** A hover over any swatch names it in the tooltip:
  "underlay 12  #807048  hue 12 sat 3 light 44" -- the id, the record's
  bytes, and the hue / sat / light the client packs it to (the RuneScape
  wiki's 0..63 / 0..7 / 0..127 scale).
- **Every id** (checkbox) lists every record, not only the open squares'
  ones: ids probed to 256 underlays and 1024 overlays (a hash lookup
  each), and the ones the window has not loaded asked for through a new
  hook, `hooks.floor_request` (64 of each kind a tick; the App queues the
  provider's per-id load task, whose first run decodes the whole config
  group, so the rest are lookups), the wells rebuilt every 15 ticks while
  the answers land; **Sort** (by id / by hue / by lightness) orders a
  well, so by hue a well reads as a colour wheel and a swatch's neighbours
  are the records that blend with it (greys sorted last, by lightness; a
  record not loaded last, by id).
- **The blend strip.** Under the wells: 23 cells of the ground across a
  seam between the pick before and the pick now, as the client draws it,
  the seam marked, and a row "Blend 12 -> 40  hue 3 sat 0 light 12 apart"
  in packed units. The workflow: I over the ground, then click a
  candidate: the strip is the join you would get. The "from" id stays
  listed in the well so the two can be swapped back and forth.
- **The colour picker.** Hue / Sat / Light sliders (degrees and percent)
  or a hex field (#rrggbb, Enter); a tall swatch of that colour, and the
  nine nearest underlays (top row) and overlays (bottom row), each a
  swatch that picks on a click, the ids listed under. A pick from a well,
  a match or the eyedropper sets the picker to the pick's colour, so the
  matches are then the records nearest what is on the ground; a record of
  the exact colour ranks first ahead of any the client packs alike.
- **Mix** for Paint underlay (Terrain panel's slider, 10..100 by 10;
  Shift+[ / Shift+] with the brush armed): under 100 the brush paints that
  share of its tiles, thinning with the falloff, drawn from a seed moved
  on per stroke (`Editor_BrushOp.paint_mix`, the noise seed's lane 3), a
  dither the client's five-tile average turns into a gradient between the
  underlay there and the one painted -- the soft texture brush of the
  other editors. The readout says "Paint underlay  r4 Linear mix 50%".
- **Textured overlays** show the texture's average colour in their swatch
  once the texture is resident (the record's own `colour` is a
  placeholder, often magenta) -- `hook_floor_rgb` in editor_panel.c.

Found on the way: eleven tooltips (five new, six older) ran past the
chrome's 63-byte cut; all trimmed. The first by-hand run of Every id
listed only the open squares' records after all -- the provider holds a
floor record only once a square needed it, so a probe of the rest found
nothing; hence the request hook above. A load task for an id past the
records used to log "Failed to load dat2 underlay N" as an error; it is a
lookup miss now (TORIRS_LOG), since the palette probes past the end on
purpose.

By hand (win64, osrs241, 2026-11-06): I over the grass picked underlay
65, the picker took #654d0b, the match rows filled (the overlay row shows
magenta for textured overlays whose textures are not resident, as the
minimap would); a click on a green swatch gave "Blend 65 -> 48  hue 9 sat
0 light 3 apart" with the strip running brown to green; the hover tooltip
read "underlay 48  #35720a  hue 16 sat 6 light 31"; Every id + by hue
re-ordered the wells brown, yellow, green, blue, grey. Mix: the Terrain
slider at 50 read "Paint underlay  r2 smooth mix 50%"; the river bank's
underlay (64) dragged over the grass left a mottled band the client
blended (build/photos/blend_*.png).

Tests: editor_panels_test `test_palette_colour` (the maths against
palette.c, the strip's ends and middle and its climb, the distance order,
the two picks as the strip's ends, the hex field and the matches, the
hover tooltip, a swatch press arming the brush, Every id and both sorts,
the sliders and the hex following each other, the Mix slider shown for
Paint underlay only and the chord stepping it); editor_placement_test
`test_mix` (100 paints the disc, 50 about half of it and only its tiles,
the same seed the same tiles, another seed another dither, the falloff
thinning it, one undo). Windows: placement 176, panels 1967, hotkeys 2504,
workbench 294 (goldens re-baselined for the new rows), all 0 failed;
Linux (the WSL clone) 176, 1968, 2504, 295, all 0 failed; check-pt-switch
and check-tree-walks PASS.

Open: the picker matches by the record's colour, not the ground's
(a blend of it with its neighbours); the match rows show at most nine of
each kind; no names for the records (the OSRS content has none); the
blend strip models a straight seam, not a corner or a one-tile speck.

### Playtest from the rail: RUN beside the tools, Place player before the first login, a window's welcome (the owner's report, 2026-11-06)

The owner's screenshot (Mac, the `working` lane, Place player armed on
m50_50 level 1): "accounts: placing maped43620 at 3206,3213 level 1
(offline: the save)" answered by "no save .../live/saves/maped43620.ini
(refused)"; "many lag when placing the play character"; the run icon
wanted in the tool palette "like debuggers"; and "it lagged and crashed
when trying to play".

**Run on Windows, by hand** (`dist/win64/torirs.exe --manifest
manifests/manifest_osrs242_mapeditor.ini`, `server=embed`, `lane=working`,
the window at 1296x839, driven by winctl): with the play account's save
removed to match the screenshot, Place player now writes "offline: wrote
saves/maped43620.ini (position 3231,3238 level 0; the play account's first
save, from the lane's fixture)" (build/photos/play_seed.png, the magenta
hover marker on the tile and the new RUN button at the foot of the rail).
RUN from the rail's button built the packs and the client cache (the
progress bar: "RUN 29%: build 2/4: enum (24 s, ~118 s left)"), launched
torirsserver and js5_server, started the play client as maped43620, which
logged into Lumbridge (play_client.png); the status bar reads "live
attached  RUN done" (play_rail_run.png). Place player on the attached
lane answered "online: ::tele 0_50_50_31_39 for maped43620". View > Live
(play_live.png) then its pop-out, which on the desktop is a second
`torirs` process: the new window said "play: logged in as maped43620 at
m50_50 (3231,3238)" within its first seconds (play_welcome.png); Stop
ended the play client and Play started another (new pid, a fresh
play.log). CPU over 4 s with the cursor over the world: the editor ~0.21
cores with Select armed, ~0.21 with Place player armed, the same with the
play client running beside it (the play client ~0.21, torirsserver
~0.01). `TORIRS_PERF=1` with Place player armed and the cursor moving
over the world for 20 s, then a place: 575 frames, p50 5.0 ms, p95 6.2 ms,
4 over the 20 ms budget (0.7 %) -- the frame is not where a lag would
come from at this size. Neither the lag nor the crash reproduced on
Windows.

**What was fixed**

- **RUN in the tool rail** (src/editor/panels/edpanel_toolrail.c): an icon
  button under Save (`TORIRS_CHROME_SKIN_RUN`, "Run") calls
  `Editor_Live_Run` from any workspace, is disabled while
  `Editor_Live_Busy` (a RUN or a build in flight), and its tooltip ends
  with the Ctrl+Return keycap. `Editor_ToolRail_ButtonCount` is one more,
  so the rail's column count still follows the window's height. The
  workbench goldens re-baselined (the registry row of each scale).
- **Place player before the first login** (torirs_maped_accounts.c
  `acct_seed_play_save`): the play account has no save until the play
  client's first start copies the lane's fixture
  (`play_copy_fixture`), so an offline edit of it -- Place player above
  all -- refused with "no save". It now seeds the save the same way (the
  fixture with `name = <account>`; a new character when the lane's fixture
  file is absent; the lane's play password remembered in accounts.ini
  [passwords]) and then applies the edit; the answer says so. Any other
  account without a save is still ABSENT.
- **A window's welcome** (torirs_maped_live.c `ToriRSMapEd_LiveWelcome`,
  called from the HELLO handler): FACT_LIVE_STATUS and FACT_LIVE_PLAY were
  broadcast only when they changed, so a window that connected while a
  lane ran -- a popped-out panel on the desktop is a second process --
  showed "detached" until the next status poll and "play: not started"
  until the next play change. Every v2 HELLO is now followed by both
  facts for that session (the host keeps them for the panels that take
  them once they exist). The raw peers in maped_embed_test and
  views_embed_test take the two frames after their HELLO; the ws probe
  speaks v1 and gets none.
- **The port probe** (`play_service`): while the play client waited for
  the lane's game and JS5 ports, `play_native_ready` opened a blocking
  `connect()` to each on every pump -- every frame of an embedding window.
  Now at most four times a second (`PLAY_PROBE_PERIOD_MS`), the first
  probe at once.
- The Accounts panel's Refresh tooltip was 64 bytes, one over the
  chrome's cut (the console said so during the run).

**Found on the way, not fixed:** the play client's log carries "Failed to
load dat2 idk 307" at login (the lane's cache holds idk 0..306; a
`TORIRS_ERR` from the client, not the editor; the character draws); the
owner's screenshot also shows "DOC_CREATE m50_50 refused: it exists there,
or is open from another storage" from Save square as new... into a new
lane while the square is open from the working lane -- the refusal is by
design (one storage per open key) but a copy into a fresh lane is a
reasonable ask; an open item under Documents.

**The Mac lag and crash were not reproduced** here, so they are not
fixed. What would decide them: the editor's stderr and
`<live>/logs/play.log` (and `play.prev.log`) from the Mac run -- the play
client's own crash lands in play.log; the editor's in the terminal that
started it (a Mac crash report under ~/Library/Logs/DiagnosticReports
names the frame). `TORIRS_PERF=1` on the editor prints per-stage frame
times at exit (src/perf/torirs_perf.h), which says whether the lag is the
frame (a Retina window renders four times the pixels of this run's
1280x800) or a stall in the embedded server's pump. Plausible causes
without evidence: the editor, the play client (a second software
rasteriser at Retina size), torirsserver and js5_server all on one
laptop; the per-frame port probe above while Play waited for a lane that
was not coming up.

**On Linux (the WSL clone), the POSIX side of the same flow.** The lane's
e2e (`make -C src maped-live-e2e-play`: the real torirsmaped, torirsserver,
js5_server and a headless play client) first ran against a content
mirror whose 102k files are symlinks into the Windows filesystem: RUN (A)
took 672 s, and "the play client logs into the lane" FAILED after 300 s
with the client parked at "Connecting to server..." (its frames
captured with `TORIRS_BMP_SERIES`). Not the editor and not the control
link: the lane's server started by hand, with no torirsmaped at all,
parks the client the same way, and gdb shows where it sits:
`ToriRSServer_PackCheck -> fingerprint_collect -> fingerprint_walk ->
stat()` over every file of the content tree (torirs_server_packcheck.c),
while the port is already listening (torirs_server_main.c binds before
the loaders on purpose, so a client started alongside is parked in the
backlog rather than refused). A stat through one of those symlinks costs
~7 ms on WSL (2000 of them: 13.7 s; 2000 local files: 4.2 s with the
same shell loop), so the walk alone is minutes, and until it ends the
play client shows "Connecting to server..." with nothing said anywhere.
That is a "lag when trying to play" wherever the content tree is on a
slow filesystem; the same server on Windows (NTFS, the tree local) logs
"login user='maped43620' session=ok" within seconds. With the content
copied into the Linux disk (`rsync` of OSRS-Content/osrs-content into
`~/OSRS-Content-local`, the clone's symlink pointed there) the same e2e
passes outright: 27 ok, 0 failed, RUN (A) 25 s, the play client logged in
3 s after it, RUN (B) 5 s with the square reloaded in the running client
(no relog), RUN (C) restarting a killed client, LIVE_DETACH stopping the
lane. The POSIX play flow -- posix_spawn of the lane's servers and the
client, the control link, the readiness probe, Play/Stop, the relog --
works; WSL's filesystem was the whole difference.

The editor itself on Linux, by hand but headless (WSLg gives it no window,
below): `SDL_VIDEODRIVER=dummy TORIRS_EDITOR_LIVE=show` with
`TORIRS_SIM_HOTKEY` / `TORIRS_SIM_CLICK_AT` driving it and
`TORIRS_BMP_SERIES` photographing every 200th frame (765x503, the default
window). P then a world click: "accounts: placing maped43620 ... /
offline: wrote ..." in the console, the magenta marker on the tile
(build/photos/play_linux_seed.png). Ctrl+Return: "RUN done: the lane was
not up: launched it on the new packs and cache; play client started as
maped43620 on the launched lane", then "play: logged in as maped43620 at
m50_50 (3205,3235)" (play_linux_run.png; the lane's server.log: "login
user='maped43620' session=ok") -- posix_spawn of torirsserver, js5_server
and the client from the editor process, as the Mac does it. Stop: "play:
not started -- stopped" (play_linux_stop.png). A panel's pop-out: "launch:
view 1 is pid ..., editor view: ok" -- the second process the desktop
pop-out is, through posix_spawn. Stop then Play in a later run: the
client restarted and logged in again -- "play: logged in as maped43620
at m50_50 (3227,3231)", the tile an earlier offline Place had written to
the save, and the server's second "login user='maped43620' session=ok"
(play_linux_play.png).

Found on the way on Linux: under WSLg the editor segfaults at window
creation in libwayland-client (SDL2's Wayland backend, "Failed to load
plugin 'libdecor-gtk.so'"); `SDL_VIDEODRIVER=x11` runs it. A WSLg
packaging matter, not ours, but worth knowing before reading a Linux
"crash at start" as the editor's.

Tests. Windows: editor_chords_test 613 (the rail: Run follows Save, chord
in the tooltip only), editor_panels_test 2865, editor_workbench_test 294
(goldens re-baselined), maped_embed_test 165 (the welcome's two frames),
all 0 failed; the POSIX-only suites are skipped there. Linux: accounts_test
144 (the play account's first Place seeds the save, the second edits it,
a stranger is still ABSENT, the seeded account deletes), live_editor_test
176 (a second window's HELLO followed by the lane's state),
views_embed_test 182, maped_embed_test 165, editor_chords_test 613,
editor_panels_test 2866, editor_workbench_test 295, all 0 failed;
check-pt-switch and check-tree-walks PASS.

### The test suites

`make -C src test-editor` on Windows (win64, OPT=1) and on Linux (a WSL clone
of the same commit, gcc 15). The editor tests now build on Windows:

- Every editor test binary links `$(MAPED_TEST_LDFLAGS)` (src/makefile):
  ws2_32 and a 256 MB stack on Windows, nothing elsewhere. The suites run
  with `ulimit -s unlimited` on Linux; Windows' 1 MB overflowed
  editor_panels_accounts and editor_panels_palette (0xC00000FD).
- The tests that read the tree, a Python oracle's output or the repository's
  goldens directly and compare bytes with the editor's emit read through
  editor/test/editor_test_text.h, the host's own CRLF rule: the content tree,
  the oracles' text-mode files and this checkout's goldens are all CRLF on a
  Windows checkout. editor_jm2_test went from 5898 of 5898 files failing to
  none; editor_if_test, editor_alloc_test, editor_shop_test, builder_test's
  goldens likewise.
- The tests that drive the editor server's native runner, sscompile or the
  CS2 compiler hand them the system spawner, as the hosts do.
- lanes_test asks the lanes code whether an entry is a link
  (ToriRSMapEdLanes_IsLink): a hard link or a junction is one on Windows.
- **POSIX harnesses, named skipped on Windows**: test-accounts,
  test-live-editor, test-live-panel, test-editor-views,
  test-torirsmaped-daemon and test-maped-script-check start /bin/sh scripts
  as fake programs (the client, the game server, sscompile) and fork()
  daemons; Windows cannot start a script as a program. They run on Linux
  (results below); on Windows the paths they cover -- RUN, the live link,
  the play client, Pop out, the daemon -- were run by hand above.
- **Results**: Windows, every member passes but those six (60 passing
  members, test_win_full3); Linux, every member passes (65, with the two DOM
  tests run on the Windows node through WSL interop, as WSL here has none).
  At the pushed tip before this run, Linux failed editor_panels (an abort),
  the daemon's cfg-copy (8), live-editor (1), cp-incremental, iface-editor
  and the embed test's link.

## Scratch squares and Open squares (2026-11-07)

The owner (2026-11-07): the New square dialog asked for a map x and z nobody
has memorised, and picking a place for a square was hard in general. New now
makes a scratch square to shape first, and its Save picks the place on the
Square map; File > Open squares sweeps up to 3x3 squares off the map as the
working set.

- **Scratch squares** (`EDITOR_DOC_SCRATCH_STORAGE`, src/editor/editor_doc.h).
  New square... asks only what it starts as (Empty, Copy of selection, Flat
  fill; Flat fill is chosen, on the palette's underlay or else u48, Lumbridge's
  grass -- an Empty or underlay-0 square alone in the world is a black void) and,
  since 2026-11-08, Empty is workable too: a scratch or blank square draws a grid
  where it has no floor (`app_overlay_build_editor_void_grid`), a pick that met
  no surface takes the floorless level 0 tile under the pointer from the height
  field (`app_map_editor_void_hover`), and Empty arms u48 when the palette is on
  underlay 0, which would paint void onto void. Flat fill at height 0 writes no
  `h` token, so the square takes the perlin ground. It
  DOC_CREATEs it in storage `scratch` on the first free scratch
  slot (`Editor_DocScratchSlot`: eight squares at x 90..96, z 240..242, two
  apart; the content tree stops at x 65, and the server's instance pool, whose login rescue sends a character to the respawn tile, starts at x 100). It opens at once as the working set,
  through the save guard only when a save has something to write (a scratch
  square's own work is no reason to ask: a save never writes it). The server holds it like any created square, but
  the file layer has no path for it (`Editor_HostDocPath`), SAVE refuses it,
  SAVE_ALL and RUN/PUBLISH's save skip it (RUN's build still plays one holding
  work: `jobs_run_scratch_write` puts its files in maps/ at the slot for the
  length of the build only, and it stays unsaved), DOC_CREATE refuses a scratch slot
  to any other storage and the scratch storage to any other square, and the
  picker leaves it off the map (its storage stays in the atlas, so an index
  still resolves).
- **Save places it.** Save all (Ctrl+S) saves everything else, then, when a
  scratch square holds work (`Editor_NewSquare_UnplacedScratch`), opens the
  Square map in Place (`Editor_SquarePicker_Place`): a bar over the well
  with the instruction, Stored in (maps/, every lane, New lane...) and
  Cancel. The hovered cell is checked as a destination with
  `Editor_NewSquareValidate` and outlined green, or in the accent with the
  reason in the bar; a click DOC_CREATEs a copy there, clears the scratch
  slot (its own undo step), saves, and opens the placed square as the working
  set. The next unplaced scratch square, if any, is asked about next. Save
  square as new on a scratch square is its Place too.
- **A blank scratch square is a free slot**: DOC_CREATE reuses it, and
  `Editor_DocHasUnsaved` / `Editor_DocUnsavedCount` do not count it, so a
  placed (cleared) slot never trips the save guard. A scratch square still
  holding work is unsaved; the guard's Save leaves it in the session, and
  New square... offers "Open unplaced scratch/m240_240" to go back to it.
- **Copy to...** (the map's menu, the pane) is the same Place without the
  save or the clearing; the copy joins the open set, as before.
- **An empty cell** clicked on the map is focused (the pane names it empty)
  instead of opening New with its coordinates.
- **Open squares** (File, Ctrl+O, `file.open_squares`): the Square map's
  sweep mode. A drag sweeps up to 3x3 cells (`Editor_PickerSweepRect`, kept
  against the first cell), the bar counts the squares under it, and Open
  opens them together (`Editor_UiOpenSquareSet`, the `open_square_set` hook,
  `EDITOR_UI_GUARD_OPEN_SET` past the save guard) in place of what is open;
  empty cells are skipped; the camera goes to the square nearest the middle.
  The app drains the set like a single open (`app_map_editor_open_square_set`).
  Wheel and two-finger scroll still zoom and pan; Escape or Cancel leaves.

Photographed (private OPT client, a mirror of the content tree, headless at
1280x800; the scratchpad, not committed): New's modal; the scratch square
opened alone as flat grass (status bar m240_240, 1 unsaved); Ctrl+S opening
the Square map in Place with its bar; a taken cell refused in the bar
("m31_36 already exists (maps/, Gielinor Surface)"); a free one offered
("Click: creates m13_63 in maps/") and placed (its .jm2/.jl2/.filepack
written, the strip m13_63, nothing unsaved); Ctrl+O's bar, a 3x3 sweep
("m36_44 .. m38_46: 9 squares", outlined green) and Open (the world at the
swept squares, the strip m36_44, m36_45, m36_46, +6). Found on the way: a
flat fill on u139 draws black (a dark underlay, not a fault) -- why the
default underlay is one that draws.

Checked: test-editor-panels (New makes a scratch square that opens at once;
Save all writes none of it; Save opens Place; a taken cell is refused where
it is hovered; a free one places the copy with the scratch's work, clears the
slot, saves and opens it as the working set; New reuses the slot; Open
squares sweeps 2x2 and opens the squares there together; the sweep clamp and
the scratch-slot refusal), test-editor-hotkeys (the doc row), test-catalog,
test-editor-catalog-finder.

Open:
- Open squares works only in the window that shows the world; a popped-out
  Square map says so instead of relaying the set
  (`Editor_UiOpenSquareSet`).
- A scratch square's spawns are not copied by Place (DOC_CREATE's copy
  carries tiles and locs).
- The open-set strip does not list a scratch square (the picker hides it).
- The Square map opens as a flyout that runs under the right column's
  panels (the Catalog draws over its east part) at 1280x800; a cell there is
  out of reach until the map is moved. Its placement is `squares_size_map`
  (899aee439), not this change.

## Catalog pictures, the Gallery and the account builder (2026-11-08)

Owner: "Improve the catalog picker. Make the catalog search a more expressive
search with preview icons for objects and locs ... the rich preview should be
its own panel with a search and filter on type. Additionally, the account
builder is still not great. I set an object as an equipment slot and it looks
like it used an NPC instead of the object id." Design first, as an artifact
(https://claude.ai/artifact/NpdDiPWN9n6VYwufqC6kGU); the owner took the
defaults on its open questions (no per-kind counts; one shared query under two
views; tile size M; the server refuses a wrong wear slot; the Catalog palette
moved onto the shared renderer).

**The whip that became a dust devil.** Obj 4145 is `slayerguide_dustdevil` (a
slayer guide page whose inventory icon is a dust devil): an obj, not an npc.
The save and the checkpoint on disk both held `worn.3 = 4151`, so the view
the panel showed did not match the file the server wrote; not reproduced
from the files. Two facts made the class of fault easy: three objs share the
display name "Abyssal whip" (4151, cert_waa_helper 4178, br_abyssal_whip
20405) and the resolver took the first hash hit; and the lane's game server
has no `::checkpoint`, so an online edit's view falls back to the save. The
builder now shows what it will set, sends an id, and checks what came back.

**Server (src/torirsmaped/torirs_maped_accounts.c).** `acct_obj_find`: `#<id>`
is an id and nothing else; a bare number an id; else the exact pack name;
else a display name exactly one obj carries, several refused with the
candidates ("'Bronze sword' names 3 objs: bronze_sword (1277), ...; use the
pack name or #<id>"). The value is `<obj text> [count]` with the count the
trailing whole number, so a display name with spaces reaches the resolver.
`worn.<slot>` refuses an obj whose `wearpos` summary field is not the slot
("is worn in slot 3 (weapon), not 0 (head)") or that has none; an id before
the index is built passes. FACT_ACCOUNT_STATUS ends with `u32 obj, u32 count`
(0xFFFFFFFF 0 for a non-item edit; old readers stop at the string).

**Account builder, Items tab (src/editor/accounts/accounts_panel.c).** The
Slot / Obj inputs, "From Catalog pick" and Set are gone. A pressed slot's line
names what it holds by the catalog's pack name ("weapon (slot 3): bronze_sword
(1277) x1"). PUT HERE is a settled catalog query over objs (cells rows with
the game's icons, the wear slot as a cap, the id), narrowed to the slot's wear
position by a "wear: weapon" chip while the Equipment shows (its x widens),
plus a "Catalog pick: name (id)" chip; a row click SETS AT ONCE with `#<id>
<count>`, Return sets the first match, a wrong wear slot is refused in the
panel before anything is sent; once the view lands the slot is checked
against the request: a green "view agrees with the last Set" or a red "view
shows X, asked for Y" line (and Log row). `Editor_Accounts_PutObj` puts an obj
in the selected slot for the Gallery.

**Catalog (src/editor/panels/edpanel_catalog.c, edpanel_catalog_model.c).** A
TYPE strip of chips (all, loc, npc, obj, seq, spotanim, vars, interface, cs2,
trigger, square; "more" shows the Kind dropdown, which keeps every other kind
and the env driver); Find takes `obj:` / `npc:` / `loc:` (any kind name) as
the kind, `#4151` as the id, and the chips `wear=weapon` (-> wearpos=3),
`cat=`, `level=` (vislevel), `stack=` (stackable); the "+ filter" menu is the
kind's (objs: the eleven wear slots, stackable, tradeable, members, category,
op; npcs: level, size, dialogue, spawn, category, op; others: category); obj
rows draw the game's inventory icon (app_services.obj_icon: the cache's
zoom2d and 2D angles) with the wear slot and "stack" as caps that add their
chip on a click, npc rows the record renderer's figure with "lvl N" and the
footprint; the header's cap column reads shapes / wear / level by kind;
"list | grid" chips open the Gallery beside.

**Gallery (src/editor/panels/edpanel_gallery.c; module "gallery", View >
Gallery, Shift+F8, registered by the Catalog after the Preview, docked after
it).** The same query as pictures: its Find box and the Catalog's edit one
text (the last changed wins), its kind chips (all / loc / npc / obj) set the
Catalog's kind, the rows are the shared model's (the Catalog drives the query
while either panel shows, `Editor_Gallery_Visible`). A GRAPH well filling the
panel (`stretch`), tiles at S / M / L = 32 / 48 / 64 px (device pref
`gallery_tile`, editor_prefs.ini), the pack name under each; an obj's picture
is the game's icon, a loc's or npc's the record renderer's. Click = the shared
pick (published, the Preview follows); double-click = an obj into the account
builder's selected slot when the Accounts panel is open on Items, else Place
armed; right-click = pick + the Preview beside; wheel / trackpad / Up, Down,
Home, End scroll by tile rows; "list" opens the Catalog beside.

**One renderer (src/app/app_record_thumbs.c).** `EditorUiAppServices.record_thumb
(kind, id, px)`: a fixed LRU of 384 entries keyed (kind, id, size), two
renders a frame beside the Preview's update, assets asked once per wait, a
record given up after 600 frames; scene sprites 0x4C002000+entry. The Catalog
palette's slots (app_catalog_palette.c) now ask it at 64 px, closing the "one
helper wanted" note above.

Tests: accounts_test 183 (`#id`, ambiguous and unique display names, wrong
slot, no wearpos, the status fields, a count after a display name),
editor_panels_accounts_test 83 (the slot line, Put here narrowed by the wear
chip and widened by its x, a helm into the weapon slot refused here, a row
click setting coins x100 with `#id`, the verify line, the pick chip, Return
with `#995`), editor_catalog_finder_test 223 (the grammar and aliases, the
kind prefix, the per-kind menus, obj and npc cells and their chips, the
headers), editor_panels_test 3211 (the Type strip and `loc:crate`, "more";
the Gallery: registry order, chord, open alone with the Catalog driving the
query, one Find text, the count, tiles keyed, click = pick, double-click =
Place, the L size relaying the grid, the obj chip, list | grid both ways),
editor_panels_palette_test 50; `make -C src check-tree-walks` PASS.
editor_hotkeys_test's one failure ("Ctrl+0 ... names Alt+0" against a note
that spells Cmd+0 on this Mac) predates this work. Not done: a drag out of
the Gallery onto a slot or the world; TORIRS_EDITOR_GALLERY photographs; a
"Gallery..." chip in the Items tab (the double-click path covers it).

## Ctrl+drag moves the thing under the pointer (2026-11-08)

Owner: "when dealing with spawns, locs, objs, players; the command+drag
should MOVE the thing." In Select, with Ctrl (Command on a Mac) held, a press
on a loc, a previewed spawn (npc or obj) or the play account's preview,
dragged off its tile, moves it (src/app/app_map_editor.c, the
"Ctrl+drag" section): a loc through the gizmo's centre drag -- the loc joins
the selection first when it was not in it; the same footprint preview and
the same one-step move on release (`edp_locs_nudge`); a spawn as "Move:
click a tile" moves the Spawns selection (the dragged spawn made the
selection when it was not in it; `Editor_Spawns_MoveTo` with the drag's
offset, one undo step "move spawn"); the player as the Place player tool
places the play account (`Editor_Accounts_PlacePlayer`, ACCOUNT_PLACE). The
destination footprint is the piece ghost (MAP_EDITOR_PIECE_GHOST_DRAG) under
the pointer, and -- owner, the same day: "The cmd+drag should be previewed
as dragging, just like placing" -- the thing itself is drawn there,
translucent, through the ghosts Place already has: a dragged spawn is the
spawn ghost (`app_map_editor_spawn_ghost_update` wants it while
`g_app_move` is active on a spawn, with the kind and id the press read off
the preview item), the dragged player is the Place player ghost
(`app_map_editor_player_ghost_update`, the drag's level), and a gizmo centre
drag -- the Ctrl+drag on a loc is one -- puts the loc ghost at the first
selected loc's landing tile (`app_map_editor_ghost_update`, the gizmo
branch; `LocEditorGizmo_DragResult`'s dx/dz, in scene bounds, never a zero
move), as the Move tool previews it. None is drawn on the thing's own tile
(a translucent double of itself reads as flicker). The press is pending
until the pointer leaves its tile; let go on it, the frame falls through to
the click it was (Ctrl+click toggles the selection). docs/MAPED_HOTKEYS.md
"Select: Ctrl+drag".

Also today: the Gallery's chips are one line (kinds, S M L, list | grid) and
its well is sized in chrome pixels from `view_h` as the squares map is (the
mix of logical and chrome pixels made the well relayout every frame: the
owner's "the gallery is flickering", and tile captions that read another
well's chip labels; six consecutive headless frames are now byte-identical);
the Catalog's Type strip is three lines holding the kinds, "more" and
list | grid (two lines cut it to "+2", hiding "more"); the Accounts wells
are sized in chrome pixels too (`edp_px(ACCT_WELL_H)`: at a 2x chrome scale
the worn layout's lower half was cut off); the Catalog's hotkey test spells
the retired chord per platform; editor_panels_accounts_test presses a slot
on a scrolled panel at scale 1 and 2 (the chrome maps both right).

**Spawn preview in the world** (owner: "show spawns in the world doesn't
work"; the feature is the other session's, 9b98a6032 / d321a1145): run
headless on a mirror of OSRS-Content with `TORIRS_EDITOR_SPAWN_PREVIEW=1`,
m50_50's 175 rows all landed ("175 shown, 0 loading, 0 not found" on the
View tab, and `TORIRS_SPAWN_PREVIEW_LOG=1` prints the counts and every
landed item to stderr once). Two things were wrong on the way: (1) a
previewed npc was spawned as soon as its config was known, and
`app_world_spawn_npc_now` gives an npc whose models are not resident an
EMPTY body that the game fills from NpcBodyLand, which a preview never
gets -- the preview now asks for the models once and lands when they are
in (`app_spawn_preview_land_npc`); (2) a name looked up while the catalog
index was still building was held as "not found" until the rows changed --
a record request on a partial index now answers EDITOR_HOST_BUSY and the
resolver treats BUSY as "ask again" (`Editor_Spawns_PreviewResolve`). In the
photographs the markers that still show no body are model-less npcs
(deadman_guard_lumbridge, veos_lumbridge: multinpc shells) and spawns on
levels 1 and 2 under the castle roof, drawn as markers over it.

## The NPC editor (2026-11-09)

docs/NPC_EDITOR_DESIGN.md, built from its defect catalogue (D1-D14) and
phases 1 and 2. The three builder windows (NPC builder, NPC outline,
Dialogue) are one tabbed, dockable panel, module `npcbuilder`, titled
"NPC editor", docked after Spawns in every workspace column
(`src/editor/builder/builder_panels.c`); the loc builder is one window too,
its outline a TREE under an Outline heading.

- **Tabs**: Movement (the minimap well, the Mode radio, Leash / Wander range
  sliders, Restrict / Block walk / Facing dropdowns, Respawn and Timer, the
  route list with Pause, Add at latched tile, Insert after, Delete,
  up / down, Play, "Writes to"), Spawns (this npc's rows across the open
  squares: Go to, Select in Spawns, Place at latched tile, Delete /
  suppress), Interactions (the tree's triggers listed read-only with Open in
  editor, then the builder's triggers, outline, step and server fields),
  Dialogue (the graph, node fields, outline tree, lint).
- **The rule** (`src/editor/editor_npc.c`, pure, test-editor-npc): patrol
  rows win, then a stated defaultmode, then wanderrange; nomove zeroes the
  radius. `Editor_NpcPreviewRects` draws the EFFECTIVE behaviour: a
  patroller's numbered waypoints and legs (the closing leg dim, a leg to
  another level red), a wanderer's square and leash, a nomove pin. The
  Spawns panel's preview and the World overlay (`app_overlay.c`) draw it;
  the header line says it ("will: patrols 10 waypoints, 73 ticks a lap;
  wanderrange 5 is unused").
- **Writes** (`src/editor/panels/edpanel_npcdoc.c`, bound by
  builder_session.c): the highest AUTHORED overlay stating the field, else
  the authored overlay holding the record, else the editor's own
  `edited/<npc>/configs/<npc>.npc` -- never a generated file
  (`Editor_CfgRecordWriteLayerFor`). A route edit is one undo step: the
  patrol rows (`Editor_NpcRoutePlan`), `defaultmode`, and the movement
  ledger `npc_movement/<shard>/<name>.move` set to `movement = patrols`
  (or wanders / stationary) with `source = authored`
  (`Editor_NpcMoveTextSet`, line-preserving; TEXT key `move:<shard>/<name>`).
  `tools/gen_npc_movement.py` reads `patrols` as "do not decide".
- **Record panel**: tab titles `cache` / `generated` / `<file>.npc`; stated
  rows first, the register's unstated rows after them on the write layer's
  tab; a tab switch clears the pick and the scroll; Write to lists the cache
  and the authored files only (default by the rule above); a record with no
  authored file gets `edited/<name>/configs/<name>.npc`.
- **Spawns panel**: a Find box; rows show the display name; the list scrolls
  to the selection; "Edit npc..." opens the NPC editor on the row
  (`Editor_Spawns_EditNpc`); row-level `Editor_Spawns_RowsOf` /
  `_PlaceRow` / `_DeleteRow` for the editor's Spawns tab.
- **App services**: `tile_flags` and `path_steps` (the loaded scene's
  collision and its BFS) for the minimap and the route legs.
- **Headless**: `TORIRS_EDITOR_NPC=<name>[,<tab>]` opens an npc in the
  editor at the first frame with a session.
- Tests: test-editor-npc (the rule, spellings, route plan, ledger),
  test-builder `movement` (the tab over fake services: legs, Add / Move /
  Delete on the well, Pause, a slider, Mode Wander), the records and panels
  tests' rows; the workbench 1x golden moved.

Not built (phase 3 of the design): the Behaviour, Look and Problems tabs,
From recording..., per-spawn facing (decision 3.9), moving the Encounters
Set tab's per-npc fields. The minimap is a flat collision picture (open /
blocked / roofed / wall edges), not the world-map bake the design named;
the World's route overlay draws straight legs, the panel's legs are the
client's BFS step counts.

## The friction pass: the editor used, not photographed (2026-11-09)

The owner's report was that the editor is hard to use and does not look
professional. Every earlier check of it was a posed photograph
(`TORIRS_EDITOR_<PANEL>=`, `TORIRS_PRESENT_BMP`) or a headless test, and both
were green. So this pass USED it: a short quest built in a lane of its own
(an npc, a crate loc, a varp, a dialogue, a placed scratch square), then work
on a fully custom island, driven from outside through `tools/editor_drive`
and, once it existed, the agent API. About a hundred faults came out of that,
none of which a posed frame shows. What was done about them:

- **The interface editor is gone.** The CS2 / interface panels were a second
  tool living inside this one (`src/editor/iface/`, `src/app/app_iface_editor.*`,
  `TORIRSMAPED_BUILD_CS2`, View > Interfaces / CS2: all deleted; build kind 4
  is reserved, not reused). The `.if` codec stays (`editor_if*.c`,
  `src/maped/iface-codec.mk`) because Save still round-trips those files.
- **Scripts are written in the owner's editor.** The Triggers panel no
  longer tries to be one: Open in editor, Show in Explorer / Finder / Open
  folder (`Editor_HostLocalRevealInFileManager`, `TORIRS_EDITOR_REVEAL_CMD`),
  Copy path, Copy name, New script file, and a Snippets dropdown with a
  preview and Copy snippet (`src/editor/editor_snippets.c`, 17 snippets;
  `src/platform/platform_clipboard.c`). The Script workspace is Triggers, the
  open record and Problems; it no longer opens the Loc builder, the NPC
  editor, the Shell editor, the Shop builder and the Quest board (they are on
  the View menu). The dialogue and loc builders are left as they were and are
  not to be grown.
- **Custom maps.** File > New and the New square dialog make a flat square
  or "Copy of mX_Z" with its spawns; a rectangle Copy / Paste carries the
  spawns in it ("with spawns"); a scratch square can be cleared; the camera
  follows a Place; a tile grid overlay (Alt+G: auto / on / off, auto shows
  it on a flat square that has nothing to see) and a Height hint; a record
  made by Copy to new that the client cannot draw yet is refused with the
  reason ("is new: RUN builds it, then it can be placed").
- **The look** (docs/CHROME_SCALE.md "The UI faces"). Vendored faces under
  `src/engine/fonts/vendor/` with their licences: Inter 4.1 (Regular,
  SemiBold), JetBrains Mono 2.304 and a 58-glyph subset of Lucide as
  `ToriRSIcons.ttf` (`tools/chrome_ui_fonts_vendor.py`,
  `chrome_ui_icons_vendor.py`; ids in `src/ui/torirs_chrome_icons.h`). They
  are baked as TrueType bytes (`tools/chrome_ui_fonts.py`,
  `make chrome-ui-fonts-check`) and drawn by stb_truetype in five font slots
  after the game's three (`TORIRS_CHROME_FONT_UI`, `_UI_SMALL`, `_UI_BOLD`,
  `_UI_MONO`, `_ICON`). `torirs_chrome_theme_editor` -- flat controls, a dark
  palette, 22 px rows, an accent line under the active tab -- is what an
  editor boot wears (`TORIRS_CHROME_THEME=osrs|flat|editor`); the game client
  and the plugin window keep the game's skin and upload no UI atlas. The tool
  rail draws icons (`ToriRSChrome_IconButtonSetGlyph`), a menu is as tall as
  its rows, chip strips are the theme's buttons in a bare well
  (`ToriRSChrome_GraphSetBare`), and View > Text size Large, which is the
  game's bold face, says to use Chrome scale instead.
- **The toolkit.** Escape closes an open list or menu; the click that
  dismisses one is consumed and no longer paints the world under it; a closed
  dropdown ignores the wheel; one notch scrolls three rows; floating windows
  draw above the dock and below the bars (`TORIRS_CHROME_LAYER_*`), and a
  floating window's Close no longer closes World under it; a text field takes
  Ctrl+A / C / V / X and a double-click; no tooltip over the field being
  typed in; every keycapped button fills its row. A GRAPH whose items moved
  in the pool is painted again (`dbg_graph_release`, `ToriRSChrome_GraphEnd`):
  the prims of the last Build point at an item's text in the pool, and a
  strip laid out again with the same picture left every later strip's chips
  reading another chip's caption until something else dirtied the panel
  (found by editor_workbench_test.c's "the list the boot leaves is the list
  a Build emits", which the new default column was the first to trip).
- **The column.** It opens with the Catalog group, the Preview and the
  Gallery; the terrain group, Squares and Areas are on demand
  (`k_dock_on_demand`, editor_ui.c) and DOCK when asked for, and arming a
  terrain tool asks. The Catalog is Find first, one row of kinds, and its
  list is the part of the column that stretches (`catalog_stretch`,
  `catalog_list_fit`: it fills its panel down to the picked record's first
  line, never fewer than six rows). The bottom strip has a floor of its own
  (`EDP_STRIP_MIN_H`): at the column's it was parked at every boot of a
  900 px window once rows were 22 px. The tool rail runs the whole height
  between the bars. A flown-out window is kept between the bars and beside
  the column. Problems no longer gives itself a second title bar inside the
  strip.
- **The panels.** Picking a palette colour no longer changes the armed tool;
  selecting in the world no longer rewrites the Catalog or steals the front
  tab; lists, sliders and wells no longer shut the hotkeys off; WASD flies
  while a list has the focus. RUN ends in one sentence, raises Problems when
  it failed, and says when the lane server must be restarted (Restart lane).
  A Place says where it went ("Placed loc 354 at 3226,3231 L0", and what it
  replaced when it replaced something); Undo and Redo say what they did in
  the status bar, which otherwise went on showing the place just undone.
  The Squares picker's atlas is kept current while its panel is closed: the
  instance helper's Find free and the Areas filter read it, and with Squares
  closed by default Find free offered the square the last Create had made.
- **Noise.** 76,431 "WebView2 bootstrap failed" lines a session became one
  (`platform_win32_webview2.c`); the labels and tooltips the chrome was
  cutting at 63 bytes were shortened, and a boot prints no cut line.
- **An agent API** (docs/AGENT_API.md, `src/agent/`, `--agent-port`,
  `tools/agent/torirs_agent.py`): JSON lines over loopback; the chrome as
  data (`ui`, `ui_click`, `ui_set`), the editor's state and its actions by
  id, tiles to and from the screen, `world_click`, `camera`, `shot`, and for
  a client in the game `game`, `npcs` and `npc_click`. A list row is pressed
  by number through `ToriRSChrome_VListRowRect`.

Tests (a private object directory, `PLATFORM_OBJ_BASE=build_edsess`, win64):
55 editor, chrome and agent suites; 52 pass, one is skipped here and two
fail as they do at the committed HEAD (below). Updated for the
deliberate changes: editor_panels_test.c (the boot column and the full one
are both laid out at thirteen sizes: 2175 checks became 3829),
editor_groups_test.c, editor_chords_test.c, editor_workspaces_test.c,
editor_scenes_ops_test.c, editor_catalog_finder_test.c and the workbench
goldens (the six scene values moved with the title-bar furniture alone;
the registry values with the column). test-editor-worldmap links again: it
had not since scratch squares (2026-11-07), for want of one symbol in its
stubs. New: test-editor-snippets, test-agent-json. test-editor-if ("all 982
interfaces read") and test-editor-vars ("every all.varbit record was read")
fail one check each against the live content tree, and fail the same way
built from the committed HEAD: the content moved, not the codecs.
test-editor-views is a POSIX harness and is skipped on Windows.

## Open issues

Every issue the phase reports left open, by area, with where to start.

### From the friction pass (2026-11-09)

- A record made by Copy to new cannot be placed in the session that made it:
  Place refuses it ("is new: RUN builds it, then it can be placed"), and after
  RUN the editor's own client still reads the base cache, so it draws nothing
  for the new id. The test quest's crate was placed by editing the `.jl2` by
  hand. Start at the editor client's cache provider against the lane's
  client cache (`build/lanes/<lane>/live`), beside the live reload of added
  squares.
- A NEW varp, varbit or varc in a lane fails RUN: `tools/ss_allocate.py:726`
  runs `<tree>/../tools/var_prefix_names.py`, which a lane does not have, and
  that script rewrites `OSRS-Content/osrs-content` whatever tree it was asked
  for. Until it takes a tree, spell the block with its id yourself
  (`[varp9517_fenwick_lantern]`): the allocator adopts a prefixed name whose
  id is free.
- The Square map opens a square on ONE click of its cell (no confirm when
  nothing is unsaved), so a stray click moves the session across the world:
  src/editor/panels/edpanel_squares.c `squares_click`. A click that selects
  and a double-click (or Enter, or Open) that opens is the file-list habit.
  Its hover tooltip also sits over the map.
- `::checkpoint` and the other `::` lines the quest driver types are echoed
  into the play client's chat: the fix is server-side
  (src/torirsserver/torirs_server_world.c, torirs_server_control.c) and was
  not applied.
- The Catalog's picked-record rows (its files, five buttons) repeat what the
  Preview's chips do and are as tall as the rest of the panel; they sit below
  the fold and the panel scrolls inside the column, a list inside a scrolling
  panel. Moving them out (into the Preview, or the Inspector tab) would let
  the panel stop scrolling: src/editor/panels/edpanel_catalog.c
  `Editor_Catalog_Register`.
- The dialogue builder and the loc builder keep their defects from the pass
  (a typed line is lost unless Apply is pressed, a half-built dialogue emits
  a script that does not compile): they are not being grown, see above.
- Host-drawn colours that do not follow the theme: the Hotkeys panel's
  capture colour (src/editor/panels/edpanel_hotkeys.c:50), the picker's dirty
  and selected cells (edpanel_picker.c:1266); the Squares pane's height is
  budgeted from an 18 px row (edpanel_squares.c:138) and the editor theme's
  is 22.
- The UI faces draw the 94 printable ASCII bytes; a Latin-1 byte (and the
  pound sign) is a space, until `TORIDRAW_FONT_GLYPH_COUNT` grows. The DOM
  presentation (`src/plugin_chrome/`) has no editor theme, no UI faces and
  never hears an icon glyph or `VListSetRows`.
- The play client RUN starts has no agent port (the editor clears the two
  agent variables before it starts a window): to drive it, start a second
  client on the lane by hand with `--agent-port 0 --agent-port-file`, as the
  command line of the one RUN started shows. The agent API has no inventory,
  loc list or varps.
- A strip's chips that do not fit fold into a "+N" chip, which shows them
  in its tooltip and cannot be pressed: at 2x the Catalog's "same category"
  and "grid" are in "+2". `edp_chips_end` also lays a strip out at 200 px
  for the frame its row's width reads 0, so the fold flickers while a flown
  Catalog settles: src/editor/panels/edpanel_chips.c.
- The Output console's Clear button is below the fold of the bottom strip at
  1530x1006 (the strip's body is 94 px and scrolls) since the strip has its
  own floor (`EDP_STRIP_MIN_H`).
- A resize grip that lies under another panel cannot be reached: the dismiss
  and layer rules stop a press at the covering panel
  (`dbg_furniture_at`). Intended, but the World's grip is under the column
  when the column is full; drag the World's edge from the left of it.
- `make -C src check-app-boundary` is red at the committed HEAD on three
  counts this pass did not add to (`struct App` 385 fields against 378,
  app.c 1462 lines against 1457, src/editor/editor_panel_region.c not in the
  baseline); the pass added only the four `src/agent/*.c` units to
  tools/appc_map_baseline.txt.

### Documents, protocol, editor server

- DOC_CLOSE never unloads a document (no linger policy):
  src/torirsmaped/torirs_maped.c:909 `handle_doc_close`.
- Save square as new... into a NEW lane refuses while the square is open
  from the working lane ("DOC_CREATE m50_50 refused: it exists there, or is
  open from another storage", the owner's screenshot of 2026-11-06): one
  storage per open key is the rule, but a copy into a fresh lane is a
  reasonable ask; the dialog could close the open one first, or the server
  could allow a copy whose key is open elsewhere.
- No DOC_DELETE; the picker's Delete clears the square instead:
  src/editor/panels/edpanel_squares.c:845.
- npc_stats/*.stats, npc_combat/*.combat and npc_movement/*.move are not
  record layers in the Record panel (the catalog lists the first two as
  sources; the panel edits `configs/all.<type>` and `server/scripts` overlays
  only): src/editor/panels/edpanel_catalog.c `Editor_Record_Open`.
- The Record panel finds overlays through the catalog (the record's
  sources); without a catalog only `configs/all.<type>` is edited, and an
  overlay that does not state the record yet is not offered (override goes
  into the layers listed).
- The wander preview's catalog values are the SAVED files'; an unsaved edit
  shows only while the Record panel holds that npc.
- SAVE_AS copies only loc / npc / obj records, and only their cache block
  (configs/all.<type>): server overlay fields are not copied, and the copy
  is written into configs/all.<type>, not a server overlay file. Other kinds
  answer UNSUPPORTED: src/torirsmaped/torirs_maped.c `handle_save_as`.
- CFG_BLOCK add/remove leaves a separator blank line on undo when the file
  ended in one: src/editor/editor_cfg.c.
- IF_COMPONENT remove only takes back blocks the stub added: src/editor/editor_if.c.
- A created square carries no `trailing=` line: DOC_CREATE in
  src/torirsmaped/torirs_maped.c.
- CFG names over 63 bytes cannot be addressed (EDITOR_CMD_NAME_MAX):
  src/editor/editor_cmd.h:132.
- IF_FIELD values over 190 bytes are read only (the 192-byte before/after
  fields): src/editor/editor_cmd.h.
- The catalog's first build (~1 s warm, ~6 s cold) runs inside the server
  pump: src/torirsmaped/torirs_maped_catalog.c.
- The allocator is wired only to SAVE_AS; the vars bit map is not wired to
  the protocol or a panel: src/editor/editor_alloc.c, src/editor/editor_vars.c.
- 3 non-positional compacks are not locked in the mirror: src/editor/editor_if.c.

### Square picker world map

- The map is its own window since 2026-11-07 (owner: "too cramped on another
  panel"): "Square map" (module `squaremap`) holds the well and the hover
  readout; the Squares panel keeps Find, the dropdowns, the open-set strip,
  the detail pane, Fit and Refresh, and gained Map. One module
  (edpanel_squares.c) drives both windows. The map opens on demand (Map, a
  Find, the tool rail's Squares, View > Square map), as a flyout beside the
  column at 3/4 of the world's box each way until it is moved or resized
  (`squares_size_map`). No hotkey of its own yet; F4 still toggles Squares.
- The web shows no map: the DOM executor (src/plugin_chrome/runtime*.js
  `renderGraph`) skips a `TORIRS_CHROME_GRAPH_IMAGE` item and the App does not
  fill the map services under `__EMSCRIPTEN__`, so the web picker keeps its
  filled cells. To show it there the executor needs the pixels: a chrome
  command carrying each 128x128 picture (or a URL the page can fetch from the
  bridge) keyed by the scene id the item already sends, drawn into a canvas
  behind the item list; and a GRAPH_KEY intent for the zoom keys.
- The pictures are logical pixels stretched by the chrome scale (sharp but
  pixel-doubled at 2x): device-pixel pictures of a large well would need more
  than half of GL3's shared 2048-square 2D atlas. A renderer with its own
  texture per large sprite would lift it.
- Cold load blocks one frame for ~0.4 s while the runner reads all the
  surface regions' tiles (table 18 plus a ground PNG per region,
  src/engine/dat2/task_dat2_worldmap_geography_load.c); later opens are
  instant. A first poll of the visible squares only, or the PNG decode off
  the frame, would cut it.
- The overview is a process-lifetime static in src/app/app_map_editor.c
  (`g_picker_worldmap`; the App holds no field by check-app-boundary). The
  one-pixel bakes are never freed (~25 MB the surface, ~134 MB once the Full
  map has seen every region); the close-up pool is capped at 64 MB. The
  regions' decoded tiles are bounded since 2026-11-03 (`OVERVIEW_REGIONS_IN_FLIGHT`,
  "Performance" above): the Full map held ~590 MB of them at once before.
- At 48 px a square the surface is crowded with icons (595's farthest zoom is
  the same); there is no per-category filter like 595's key panel.
- A content edit does not redraw the map: it is the CACHE's world map. A
  square created or re-terrained in the editor shows the cache's old picture
  (or its own colour) until the client cache is rebuilt and the bakes dropped
  (no hook drops them yet).
- Ocean tints differ by region (teal, blue, purple patches east of Al Kharid
  and south of Zeah): the hue is the renderer's; not yet compared against a
  595 dump of the same sea.
- Full map overlaps: where two areas place a region on the same true square
  the main area (then the cache's order) wins; nothing chooses between two
  dungeon areas deliberately.

### Areas and collections

- A record line holds 511 bytes at most (two ride in one command,
  EDITOR_CMD_SPLICE_MAX): about 16 rectangles or ~60 plain square keys per
  record; the panel and ctl refuse what does not fit. A hand-written longer
  row parses and shows but cannot be edited or removed by a command:
  src/editor/editor_areas.h EDITOR_AREAS_LINE_MAX.
- A document that does not parse (a hand edit gone wrong) is read only: the
  panel says which line, every command is refused until the file is fixed
  and re-opened: src/editor/panels/edpanel_areas.c `areas_refresh`.
- An area on every level (`level all`) is drawn on the edit level (0 when it
  follows the pick); rectangles are hulls of their four corners for the wash
  and 8 ground samples per edge for the outline, so a rect over steep ground
  can float or sink between samples: src/app/app_overlay.c
  `app_overlay_build_editor_areas`.
- Go to keeps the current yaw and pitch and puts the eye 2600 units from the
  centre; a square it has to open first is ADDED to the window's set (one
  world load), and the move waits up to 600 frames for it.
- Open all squares opens every key as a document at once but hands the
  window one square per world load (the window holds one pending open), so a
  big collection takes several loads; the world shows 16 at once.
- The picker's collection filter matches full keys: a collection key without
  a storage ("m50_50") does not match the same square in a lane.
- No ctl verb sets an area's colour without restating its rectangles
  (area-set replaces the rectangles); the panel has no colour picker yet (the
  colour comes from the file or the area's place in it).
- The Areas panel joins the default dock column after Squares; on a short
  surface it is hidden (View > Areas opens it beside the column).

### World, views, chrome, panels

- Active level: the Move tool keeps each selected loc's own level (the
  destination TILE is picked on the pinned plane, the level is not
  rewritten): `Editor_LocOpsMoveTo`. The minimenu's "Select terrain/wall"
  rows still latch the picked element's own level (src/app/app_minimenu.c).
  Viewer windows keep their own Levels cap (editor_view.c), they do not
  follow STATE_EDIT_LEVEL. The checked state of the Level row is drawn by the
  native chrome only; the DOM executor draws the row as plain menu items.
  The pinned re-pick searches 16 tiles around the pick; with nothing drawn
  under the pointer (sky, past the scene) there is no hover. The camera's
  level shift uses the heights under the eye's column, not the look-at point.
  "Above" is forgotten when the level goes back to Auto.

- EDIT local flush gaps: occluders, minimap icons, LINK_BELOW rebakes:
  src/engine/world_builder/edit/.
- The ghost still displaces through runtime loc changes under EDIT:
  src/editor/map_editor_ghost.c.
- Layer toggles are not read by the painter: src/editor/editor_view.c:359
  (`EditorView_LayerShown` has no caller in src/app/app_world_paint.c).
- (resolved 2026-11-06, workbench 6.6 part 2) STATE_VAR_PREVIEW is honoured
  by the world's multiloc/multinpc resolve: src/varp/varp_manager.c
  `VarPManager_SetPreview`, fed by src/app/app_map_editor.c
  `app_map_editor_var_preview`.
- The embedded editor server still answers SPAWN_VIEW UNSUPPORTED (no
  launch binding inside it): Open in new view and Pop out go through the
  client's own launch service and the embed's loopback listener instead
  (src/app/app_editor_view.c `EditorView_AppSpawnModule`). The listener stays
  open, idle, until the session closes once anything popped out.
- Shared state: the Client shares the tool, brush, palette, flags,
  placement shape, view level and selection; it does NOT share the loc
  multi-selection beyond its primary, the rectangle, the flatten target or
  any panel's own list/scroll/tab state (each window keeps its own). Two
  windows changing the same shared tool in the same frame end on the
  server's order (both converge). A plain viewer (Open in new view) still
  ignores clicks; pop out World for an editing window.
- Pop out: a popped-out window is a whole client process (cache, renderer,
  EDIT world): several out at once cost that footprint each. Its window is
  the OS window -- closing it is the only way to end it; there is no "pop
  back in" from the popped-out side (View > <panel> in the main window shows
  the panel there, and the other window keeps running). A panel window loads
  the world around the camera's square although it draws none. A pinned
  "Above" is not carried (STATE_EDIT_LEVEL holds only the pin): the popped-out
  World caps at the pinned level. The framing of the popped-out World
  differs from the panel's by the two viewports' sizes (same eye, yaw,
  pitch). Flyouts a solo panel opens (a Record panel, a builder) appear at
  their registration place over it. The web needs a daemon (`maped=`); with
  the page's embedded server Pop out says so and keeps the panel.
- Panel furniture: no cursor change over a resizable edge (the chrome has
  no cursor hint); the border band is the frame (6 px at 1x) or 4 px,
  whichever is wider. The DOM executor mirrors none of it -- it has no
  multi-panel dock layout at all (below); the web build draws the editor
  through the native chrome. What it would need: a close and a pop-out
  intent per panel (TORIRS_CHROME_INTENT_CLOSE exists for the single plugin
  window; a TORIRS_CHROME_INTENT_POPOUT beside it), the panel boxes and
  an edge-drag gesture reported as a rect change, i.e. the dock layout.
- The viewer window and Open in new view have not been run against a daemon
  in an open window (only in tests and headless boots).
- The DOM chrome has no multi-panel dock layout: src/plugin_chrome/runtime-source.js.
- The saved layout is written a few frames after a change settles and once
  more at shutdown (`app_map_editor_layout_flush`, run by hand on its own IO
  batch from App_Shutdown before Editor_PanelFree, like app_prefs_flush); a
  drag still in progress at quit is not written. Viewer windows the daemon
  spawns go through the launch service's `view_environment`, which sets
  `TORIRS_EDITOR_LAYOUT=` like the party seats, so they never read or write
  the device's layout file.
- World overlays: everything that marks the world (brush footprint, spawn and
  loc ghosts, selection, hover; src/app/app_overlay.c and
  src/editor/map_editor_ghost.c) projects through `app->world_emit_desc`
  (src/app/app_world_project.c), which `App_MapEditorWorldView` sets to the
  World panel's body each frame -- a new overlay that projects the same way
  follows the panel with no extra code; one that draws in screen space must
  clip to `Editor_UiWorldRect`.
- (closed in phase 5) the picker's tab strip is gone (the Area dropdown picks
  the area) and the default layout no longer overlaps; the atlas re-fits to the
  panel until the user zooms or pans. Still open: the viewer window's toolbar
  never calls `ToriRSChrome_SetSnap`; the web executor's sprite list lacks the
  two new slider slots (src/plugin_chrome/runtime*.js draws an HTML range input).
- The npc placement ghost is a real scene npc, so it appears under the cursor
  in the right-click menu while it is up: src/app/app_map_editor.c.
- `TORIRS_EDITOR_HOVER=<x>,<z>[,<tool>[,npc|obj|loc,<id>]]` parks the editor
  hover for photographs (documented in src/app/app_map_editor.c); there is still
  no pointer injection for a scripted click.
- IF1 type 8 (peng_emote) cannot be mounted: src/app/app_iface_editor.c.
- The builder panel's Check (sscompile) runs synchronously on the UI thread:
  src/editor/builder/builder_panels.c `Builder_SscRun`. The Triggers panel's
  Check and the interface editor's CS2 compile are the editor server's
  BUILD jobs since workbench 6.6 part 1 (see its section for the seam).
- builder_test uses stand-ins for parts of the tree: src/editor/test/builder_test.c.
- test-bootmanifest cannot run in this worktree (symlinked content root);
  test-world's 19 edge-band failures predate phase 2.
- (resolved 2026-11-02) `make -C src check-app-boundary` passes. What this
  branch added now takes parameters: the catalog, inspector and areas panels
  include no app.h and name no `struct App` -- they ask through
  `ui->app_services` (loc record, loc load request, hover tile + edit level,
  scene window, select terrain, camera to a tile, preview reset, world busy,
  redraw; every member NULL in a headless test, which behaves as before);
  the view's App glue (`EditorView_AppBoot/AppFrame/AppActivate/AppOnState/
  AppLevelMask/AppSpawn`, the camera apply/read-back, the web's
  `torirs_editor_view_spawn`) moved from src/editor/editor_view.c to
  src/app/app_editor_view.c, declared in app/app_internal.h; App_Shutdown's
  editor lines are one `app_map_editor_shutdown` call. What was already red
  at v3 19d7d7b26 was re-baselined in tools/appc_map_baseline.txt with the
  reason recorded: src/plugin/torirs_plugin_drive_record.c (provider,
  seq_source, the npc pool) added to the reach-in list, `struct-App-fields`
  332 -> 378 (this branch added none), `app.c-lines` 1331 -> 1457 (1452 at
  v3, plus the editor's two boot calls and the shutdown call).
- STATE_LIVE_FOCUS is not followed by viewer windows (design #run: a viewer
  locks its camera to the followed player): src/app/app_editor_view.c
  `EditorView_AppOnState`. The position is always -1 today anyway (below).

### Catalog performance

Measured 2026-11-02 on the owner's tree (63,307 locs, 405,868 records,
~100,000 files under the scanned roots), private no-LTO client
(`PROFILE=1 OPT_RELEASE_CFLAGS=-DNDEBUG OPT_RELEASE_LDFLAGS=`), `sample` on
the running process. Recipe (one action every 150 frames, ~3 s, the pace
that exposed it):

    TORIRS_PLUGINS=0 TORIRS_HIDPI=0 TORIRS_EDITOR_LAYOUT= TORIRS_EDITOR_HOTKEYS= \
    TORIRS_CATALOG_STATS=1 TORIRS_MAX_FRAMES=1600 \
    TORIRS_EDITOR_CATALOG="loc:oak@200,every=150,pick=0@800,pick=1@950,pick=2@1100,edit=1250,next=1400" \
      <client> --manifest manifests/manifest_osrs242_mapeditor.ini

(`TORIRS_EDITOR_CATALOG=<kind>:<find>[@<frame>][,every=<frames>][,pick=<row>@<frame>...][,next=<frame>][,edit=<frame>]`
opens the Catalog, types the find text a character at a time, clicks rows,
Next page and Edit record...; `TORIRS_CATALOG_STATS=1` prints one line per
CATALOG_QUERY / CATALOG_RECORD round trip with its ms and every frame's wall
time from the open to 30 frames after the last action, plus any frame over
33 ms: src/editor/panels/edpanel_catalog.c, edpanel_catalog_model.c.)

| action | before | after |
|---|---|---|
| keystroke ('o', 'oa', 'oak') | 174-186 ms frame (and 0.2-1.8 s in other runs) | 2.6-9 ms |
| click a row (record) | 168-1375 ms | 0.4-3.3 ms |
| Next page | 566 ms | 2.3-7 ms |
| Edit record... | 303 ms | 22-45 ms |
| first open at frame 200 (~4 s after boot) | 2.7-3.8 s | 2.6-3.3 s (the warm build's remainder) |
| first open at frame 1500 (~30 s after boot) | 3.5 s | 6.7 ms |

The cause (sample, 20 s window over typed keystrokes): 1,376 of 1,384 ms in
`Editor_HostMapEdCatalogQuery` were `ToriRSMapEdCatalog_Refresh` ->
`cat_refresh` -> `cat_scan` -> `cat_walk` (stat + getdirentries over the
whole tree), the query itself 8 ms. Every request re-stat'ed ~100k files
when two seconds had passed since the last refresh, synchronously on the UI
thread (the embedded server runs inside `await_reply`). The fix
(src/torirsmaped/torirs_maped_catalog.c): requests call
`cat_request_index` -- build only when there is no index, else start a
sweep; `ToriRSMapEdCatalog_Service` (from `ToriRSMapEd_Service`, every pump;
the daemon polls at 20 ms while `ToriRSMapEd_Busy`) walks the roots with an
explicit directory stack a budget at a time, then compacts, parses what
changed and merges in one step (a record's refs index its sources'
contributions, so no request may see half a re-parse). The held query
(`cat_query_hits`): same (kind, text, flags, revision) answers from the
held hits; a query whose filters restate the held ones and whose terms
contain the held terms (`cat_query_implies`; a numeric term never implies,
it also matches by id) tests only the held hits. A record whose file
changed is re-read alone (`cat_refresh_record`). Test:
`make -C src test-catalog-perf` (src/editor/test/editor_catalog_perf_test.c,
58 checks over a 60,000-loc, 3,000-script synthetic tree: requests past the
interval stat at most their records' own files, the sweep is sliced and
catches an edit, narrowing tests only the held hits and answers what a cold
query answers, a record view reads one block; then the real tree's numbers,
printed).

**Workbench step 6.2, server half (2026-11-05)** (torirs_maped_catalog.{c,h};
tests `make -C src test-catalog-perf` and `test-catalog-finder`):

- Filter values narrow as a prefix, tested and ranked whole, then a prefix
  of a whole word (words split at spaces and commas; the old '_' rule is the
  first word's case), then a written-out `value*`; a number never stops
  inside a longer number (`shapes=2` is not 22). Typing on narrows the held
  hits where the longer value implies the shorter (`cat_value_implies`).
- `shape=wall|decor|centre|roof|ground` (or a prefix, or a number) resolves
  server-side against the loc's placeable shapes. A loc row carries
  `shapes` (the Locs panel's rule, in its offer order), `size` ("WxH") and
  `layer` (wall, decor, main, ground; world.c's layer per shape).
- Kind "all" on the wire is scoped: kinds in order (loc, npc, obj, the
  rest), each capped at the page, stopping once the page is full; a numeric
  term is probed by id in every kind first. Synthetic 63,003-record tree
  (this machine, `test-catalog-perf`): '' 4.4 ms / 63,003 tested -> 0.08 ms
  / 100; 'o' 5.2 -> 0.08 ms; 'oa' 2.5 -> 0.14 ms / 694; 'door' 1.8 -> 0.18
  ms / 1,277; '700' 1.9 -> 1.6 ms / 50,702 (the hundredth hit is far in);
  'op=Chop' (nothing matches) ~11 ms -> ~11 ms, no gain: scoping still
  tests every record when nothing matches (the derived loc text is
  formatted only when a filter reads it). This read "22 ms -> 12 ms" until
  the 6.2-6.4 review; the test's own before/after never showed it and no
  run of a pre-branch build is recorded. The
  in-process `ToriRSMapEdCatalog_Query` still answers every match.
- The warm build never waits on one file: configs/all.<type>, its .compack
  and overlays over 64 KB are parsed in parts of at most 4 MB
  (`TORIRSMAPED_CATALOG_SLICE_BYTES`) that also stop at the slice's
  deadline at a block boundary; each source is merged into a live index as
  soon as it is parsed (no full merge in the warm build), locs first, then
  npcs, objs and the rest; the derived fields are sliced over records. The
  synthetic build's longest 2 ms slice went from 27 ms to ~3-4 ms. A
  CATALOG_QUERY with `TORIRSMAPED_CATALOG_PROGRESS` (server bit
  `TORIRSMAPED_CAP_CATALOG_PROGRESS`) answers from the partial index with
  `TORIRSMAPED_CATALOG_PAGE_PARTIAL` and done/total KiB appended after the
  rows; without the flag the payload is the v2 one and the query finishes
  the build first, as before. The client half landed with the 6.2-6.4
  integration (below): editor_host_remote.c sends the flag only to a server
  with the bit and reads the tail, and the Catalog asks with it.

Still slow, with where to start:

- (Fixed 2026-11-03, "Performance" above, and merged with the workbench's
  partial answers 2026-11-06.) A Catalog opened in the first ~15 s after the
  editor opens used to wait for the rest of the warm build (~6 s of work on
  this tree: scan 0.5-1.8 s, parse 2.5-3.6 s -- cs2 935 ms, jm2 951 ms, jl2
  527 ms, rs2 357 ms -- merge 80-130 ms) at 6 ms a frame
  (`TORIRSMAPED_CATALOG_WARM_SLICE_MS`, still the shape under
  `TORIRS_CATALOG_WARM_THREAD=0` and on the web/win32 lanes, where the
  PROGRESS query answers from the partial index). On the worker thread a
  PROGRESS query gets an empty partial page and the progress until the
  thread is done.
- Not measured on the owner's tree here (the content submodule is not
  checked out): the real-tree half of `test-catalog-perf` prints it.
- A record view (CATALOG_RECORD) asked during the warm build still finishes
  the build first: it carries no flag. The atlas asks with
  `TORIRSMAPED_CATALOG_SQUARES_NOWAIT` and is answered BUSY while the thread
  builds (a sliced build still finishes it first).
- A sweep that finds a changed file merges the whole index in one step,
  ~80-100 ms on this tree (`cat_merge`; ~21-26 ms on the synthetic tree):
  one hitch per change seen, while the Catalog is being used (sweeps start
  only from requests). Records point into their sources' pools, so a
  re-parsed source's records cannot be patched in place; an incremental
  sweep would need per-source record ownership.
- A sweep lags: an external edit shows in a query once a sweep has seen it
  (a sweep is ~180-650 ms of 2 ms slices, ~1.5-5 s at 50 fps). A record view
  is always fresh for its own files.
- A key=value filter that matches little still collects every record's
  fields of the kinds it tests ('op=Chop' was 18 ms (loc), 100 ms (all
  kinds) per keystroke on 405k records before the scoping): no field index.
- A live merge that outgrows the warm build's hash estimate (id tables'
  bytes / 16) rehashes in one step.
- Edit record... 22-45 ms: each layer's CFG document is loaded by reading
  the whole file and scanning it for the block (src/editor/editor_host_local.c
  `local_doc_load` / `cfg_block_extent`; configs/all.loc is 86 MB). The
  catalog knows every block's offset; a block-offset index there would make
  it a few ms.

### Preview panel

- A well larger than 640x512 (`EDITOR_PREVIEW_SPRITE_W/H`,
  src/editor/editor_preview_camera.h) frames the model at that size, centred:
  the sprite is one fixed size so the GL3 2D atlas reuses its tile
  (src/platform/platform_sdl2_renderer_gl3.c `gl3_sprite_upload_rgba` never
  frees a tile whose sprite changed size). A bigger cap costs atlas space for
  the life of the window; freeing a resized sprite's tile in the renderer
  would lift it.
- The well renders 640x512 for every orbit frame, about 28 times the old
  120x96 well's pixels (calloc, raster, ARGB pass, upload).
- A popped-out Preview follows the pick, not the multiloc rung: the variant
  dropdown is the Catalog's own state and is not relayed
  (`Editor_Catalog_PreviewId`).
- A popped-out Preview draws a pick it hears AFTER its boot slowly when the
  model is not resident there: in the photograph runs (this Mac, 1280x800)
  hans picked 30 main-window frames after the pop-out still showed the
  previous model at the window's frame 500 and appeared later, while hans
  picked before the pop-out (handed over by the boot STATE_SYNC) was drawn by
  frame 500, and the main window draws a non-resident npc (black_dragon) in
  about 2 s. The subject row names the pick at once. Probably the config and
  model loads queue behind the window's own world load; not profiled:
  src/app/app_map_editor.c `app_map_editor_preview_update`.
- The selected-loc fallback applies only until the first pick: a pick is
  never cleared (`ui->tools.pick_kind` stays), so after one the Preview keeps
  showing the pick while locs are selected.
- The auto-open treats a Catalog shown by a saved layout at boot as an
  opening, so a session that starts with the Catalog open opens the Preview
  beside it (unless the column docks it).
- A Catalog the user left open as a flyout at a surface whose column has no
  room for it comes back closed from the saved layout (the layout re-admits
  dock-order panels by room). Older than the Preview; editor_panels_test
  `test_preview` loads the file with and without "preview" and they agree:
  src/editor/editor_ui.c `Editor_UiLayout`.

### Hotkeys

- A rebind from the panel replaces every chord of the action with the one
  captured (Redo / Delete selection lose their second chord until Reset to
  defaults or a hand edit of the ini): the TREE row has one click target.
  src/editor/panels/edpanel_hotkeys.c `Editor_Hotkeys_Feed`.
- A chord is claimed from `[debug:hotkeys]` only while its context is live
  (MAPED_HOTKEYS.md says "Ctrl+S, Ctrl+D and Ctrl+A are claimed"): Ctrl+A is
  claimed over the World panel only, Ctrl+D only with a selection; elsewhere
  the debug flight still takes them. src/editor/editor_hotkeys.c
  `Editor_HotkeysResolve`.
- The dispatcher runs before the chrome routes this frame's keys, so the
  gates read last frame's text focus and hover; a click into a search box and
  a keystroke in the same frame would reach the table (not seen in practice).
  The pinned-plane hover the eyedroppers and Ctrl+A read is last frame's too
  (`app_map_editor_pin_hover` runs in the world click, later in the frame).
  src/app/app_frame.c `app_editor_hotkeys_frame`.
- The DOM chrome executor shows the menu rows' tab as a space (no right
  alignment there): src/plugin_chrome/runtime*.js.
- The clash check compares exact chords; a held action's tolerated Fly-faster
  modifier (Shift+W still flies) is not counted against an edge chord bound
  to Shift+W. src/editor/editor_hotkeys.c `Editor_HotkeysClash`.
- Escape with a menu drop or the minimenu open is theirs (the gates close);
  the table's Deselect ends a stroke (kept as its one undo step) or drops a
  rectangle drag, then clears the selection and the rectangle; it does not
  cancel an armed spawn "Move: click a tile".
- The Hotkeys panel shows group headers and one row per action
  ("name (context)", chord right-aligned); it has no sortable columns.
- Viewer windows (`--editor-view`, panels hidden) resolve nothing: the gates
  require the editor panel open. Their camera keys are the game's.
- No photograph of an open File/Edit menu (there is no pointer injection to
  open one headless); the right-aligned rows are covered by the chrome change
  and editor_panels_test's label checks.

### Triggers panel

- Refresh re-indexes through the catalog's own sweep, which a query runs at
  most every 2 s (torirs_maped_catalog.c `refresh_interval_s`): a Refresh
  within 2 s of the last query lists the old rows until the next one. An
  exact refresh needs a CATALOG_REFRESH request (force, or one path) in
  src/torirsmaped, which the editor side does not own. The panel's own file
  mode and the "edited outside" test read the disk directly and are exact.
- "Reload from disk" is splices from the panel (the authority keeps a closed
  document loaded, so a close + open would return its stale copy): the
  document ends up holding the disk's bytes but marked dirty, and Save
  writes the same bytes back. A DOC_RELOAD request in the authority would
  clear it properly.
- Open in editor assumes the content root is on this machine (embedded
  server, or a daemon on localhost); against a remote daemon the path is
  the daemon's. Windows has no launcher yet (UNSUPPORTED, the path shown).
  src/editor/editor_host_local.c `Editor_HostLocalOpenInEditor`.
- The open command is an environment variable, not `[editor:boot]
  open_cmd=`: the boot manifest parser (src/bootmanifest) is not the
  editor's. Moving it is a one-line read there plus a setter.
- (resolved 2026-11-06, workbench 6.6) Check is the editor server's CHECK
  BUILD job: the window no longer waits, and the server's unsaved `rs2:`
  documents are written over the stage. The NPC / loc builder's own Check
  still runs sscompile synchronously (World, views, chrome, panels above).
- Follow mode does not follow the hovered loc, only a selection, the
  Spawns selection or the catalog pick: the hover is App state and the panel
  stays App-free (headless-testable).
- Add trigger appends to the subject's edited script only; a trigger meant
  for an existing area file is added there by hand (Open in editor).

### Accounts

- No Varcs tab: a varc is client state; no server op or cheat writes one for
  a logged-in player and a save holds none. Not faked.
- Online: one slot cannot be emptied and the bank cannot be edited (no cheat
  does either); xp only grows (`::xp` adds); a varp or varbit is set by NAME
  (`::setvar`), so while the catalog is still indexing the content the daemon
  refuses with "cannot be resolved yet".
- The online view is the last checkpoint (refreshed after each edit and by
  Refresh), not a live subscription; `::checkpoint` is refused in a dialogue
  or a fight, and then the view falls back to the save after 4 s.
- A server attached by ADDRESS keeps its own saves directory; the module
  reads and writes the lane's.
- `accounts.ini` holds the passwords of accounts made here in plain text.
- The Hotkeys panel's Place player chord is P; the Items tab's "Set" and the
  Vars tab's "Set" are two buttons of the same caption (a test finds them by
  order).
- `TORIRS_EDITOR_HOVER=<x>,<z>,<tool>` never armed a two-word tool
  (`place_player`, `paint_underlay`): the name compare lower-cased the env's
  `_` as well (fixed with this work, src/app/app_map_editor.c). The tool's
  marker photograph: `TORIRS_EDITOR_LEVEL=0,3222,3218,0,400,1800
  TORIRS_EDITOR_HOVER=3222,3218,place_player`.

### Live lane: editor side

- Ease of running (2026-11-02), what remains:
  - The wasm still links the embedded ToriRSMapEd (torirs_maped*.o, 187 KB of
    objects); it is unreachable on the web (`Editor_HostOpenMapEdEmbed`
    refuses under `__EMSCRIPTEN__`), not compiled out. Compiling it out means
    `TORIRS_MAPED_NO_EMBED` for the web objects and moving the names the
    client takes from torirs_maped_build.c (`ToriRSMapEd_RunStepName`, ...)
    into a client-side unit: src/makefile, src/editor/live/live_panel.c.
  - The web link is one synchronous XHR per window per frame (~1 ms on
    loopback) and io_server's single worker serves every window's request in
    turn; a request that waits for the daemon (await_reply) holds the worker
    up to 250 ms. Fine on one machine; not meant for a remote editor. The
    real fix is an asynchronous editor host (no await_reply on the web) over
    the daemon's WebSocket: src/editor/editor_host_remote.c.
  - The first RUN on an empty lane builds everything into it (41 s on the
    mirror, 2026-11-02); the lane is not seeded from the tree's built packs.
  - Headless Playwright closed the editor's popped-out and play tabs after
    a while (the whole browser context went away twice); the tabs were
    photographed while open and the play tab's login is in the lane's
    server.log. Not seen as a page fault; unexplained.
  - The lock: a pid reused by an unrelated process after a crash reads as a
    live holder (it is named, so a person can tell); no start-time check.
    Windows never calls a lock stale.
  - The osrs239 profiles' lanes still build and launch osrs241
    (`ToriRSMapEd_LaneDefaults` rev): src/torirsmaped/torirs_maped_build.c.
  - The Live panel's label rows are 64 characters: a RUN stop or a lock
    holder is cut there; the whole text is in the panel's log (wrapped) and
    the Console.
- The control link reports players now (CTL_PLAYERS, row 16), but only the
  play client uses it: the followed player is still typed by name and
  STATE_LIVE_FOCUS x/z/level stay -1, so viewer windows do not lock to a
  position; the player rows reach the Live panel's model
  (`Editor_LiveModel.player_rows`, 16 kept) and no list shows them:
  src/editor/live/live_panel.c.
- The play client: Play on one that is logged in cannot raise its window (it
  answers "already logged in"); `--pass` is on its command line (a dev
  account, visible in `ps`); one per lane, so two editor servers on one lane
  each start their own as the same account; the account's save follows the
  player (the fixture is copied only once); an attached lane the editor did
  NOT launch is still checked by account name, and a client the editor did
  not start but logged in as that account counts as "the running client"
  (FACT_LIVE_PLAY says so); RUN reports a play client that did not start as
  a failure at RUN_STEP_PLAY after the reload has landed:
  src/torirsmaped/torirs_maped_live.c `jobs_run_play_decide`.
- LIVE_PLAY browser is per editor server, not per window: a web window and a
  native one on the same daemon share "a browser plays this lane", and no
  request turns it back off (restart the daemon).
- The scriptrun e2e lane runs with no play client (`TORIRSMAPED_LIVE_PLAY_BIN=`
  in run.sh): a scriptrun server refuses a real client.
- RUN sends a `(255, group)` row for every server-pack archive whose manifest
  hash changed (resolved 2026-11-01). The diff is taken from the lane's
  `servpack/pack.manifest`; a build that rewrites the manifest without changing
  an archive's hash sends nothing, by design.
- (resolved 2026-11-06, workbench 6.6 part 1) BUILD "check" (3, was
  "scripts lint") and "cs2 compile" (4) are jobs: `maped-script-check` and
  `maped-cs2-compile` (src/maped/records.mk).
- The client cache build is incremental for config types only
  (3rd/rscache/tools/cachepack/cp_incremental.c:1988
  `cp_pack_client_incremental`), plus saved squares by archive. Interfaces,
  CS2 and other assets are not repacked by BUILD client cache.
- cp_pack_client_incremental keeps pack-on-base semantics: a record deleted
  from the text stays in the archive, and routing's "in the base cache" reads
  the live cache, so a record whose `pack/<ns>.client` line was removed can
  stay client-side where a fresh full pack would drop it.
- The client global key hashes the source directory as a string
  (cp_incremental.c:1177): the same tree by another path repacks every type
  (writing nothing when no record differs).
- One CTL_SUBSCRIBE per editor server; the last LIVE_SUBSCRIBE from any window
  wins: src/torirsmaped/torirs_maped_live.c:233 `live_send_subscribe`.
- The Restart offer for restart-needed changes is not built; restart rows
  reach FACT_RUN_DONE and the Live panel's log only: src/editor/live/live_panel.c.
- RUN saves every dirty document of every Client (SAVE_ALL) first; a failed
  save stops RUN at "save" but rows that did save are still broadcast:
  src/torirsmaped/torirs_maped_build.c:1363.
- The pending reload set is bounded (512 squares, 128 spawn files, 4096
  archives); an overflow is logged and the entry dropped:
  src/torirsmaped/torirs_maped_build.c:1038 `jobs_note_saved`.
- Native runner and launch are compiled out on the web and Windows
  (TORIRSMAPED_JOBS_NATIVE 0, src/torirsmaped/torirs_maped_build.c:25/37); no
  Windows or Linux compile was run. Linux relies on platform.mk's
  -D_GNU_SOURCE for posix_spawn and realpath.
- The client has no manifest key for a live lane; it binds only through
  `$TORIRSMAPED_LIVE_DIR` (src/torirsmaped/torirs_maped_live.c
  `ToriRSMapEd_BindLiveFromEnv`); the daemon also takes `--live-dir`.
- The tracked binary 3rd/rscache/tools/cachepack/cachepack was rebuilt with
  `--incremental` and is committed.

### Live lane: game server and client

- Web client: archive invalidation is done (row 16). What it leaves: the
  executor's retired reference tables are never freed (a few per RUN, a dev
  lane's cost, as the client's retired records); a stored group the new table
  no longer lists stays in IndexedDB (never read: the executor checks the
  listing first); the re-prime's `cache invalidate: js5 fetched ...` and the
  executor's `io ...` lines go to the browser console (console.log), not the
  page's CLIENT panel, where only the C side's lines appear.
- A CACHE_INVALIDATE that lands while the play client's JS5 is still filling
  an empty cache in the background (the first minute after a first login)
  drops the client's game session: it reconnects, the server refuses the
  reclaim ("reconnect for maped43633 has no session to reclaim"), and RUN's
  play step, finding the account gone, restarts the client (it then logs in
  on the new content, so the RUN still ends in a client showing it). Seen with
  map squares since index 5 started evicting (row 16); the loc/npc/obj
  invalidation re-primes the same way and is presumably exposed too. With the
  fill finished the reload lands in place (run_play.sh waits 75 s before
  RUN (B), `LIVE_E2E_SETTLE`). Cause not pinned (the client's net narration is
  compiled out of OPT builds): a suspect is the REBUILD parking the packet
  pipeline on the square while the re-primed index waits behind the fill's
  queued requests: src/js5/js5.c `Js5ClientInvalidateArchive`,
  src/app/app_net.c `app_net_link_watch`.
- Client scripts (12) and mounted interfaces still wait for the next login
  (scripts cannot be freed under a running CS2VM2_Frame, cache_provider.c
  `CacheProvider_ClientScriptAdd`). Decoded terrain and scenery rely on the
  forced REBUILD.
- On-screen npc and ground-stack action rows are copies made when the type is
  applied; they refresh on respawn or transform, or at the next login.
- Collision stamped into loaded squares from old loc records is not re-stamped
  by a loc reload; squares loaded afterwards, or a square reload, pick up a
  changed blockwalk or footprint. Per-npc state taken from the def at spawn
  (hitpoints, wander radius) holds until the npc respawns.
- Evicted client records are retired until shutdown (grows a little per RUN in
  a dev lane). Native: a second invalidate while a re-prime's reference fetch is
  still in flight against an older master can fail CRC checks (the web client
  serialises its re-primes and does not have this).
- param, enum, varp, inv, idk, varbit, dbrow, dbtable and the name-table groups
  (128 and up) still answer restart-needed, each with its reason in the restart
  row: they are read at boot with the content symbols or derived into the bank
  and varp layout (src/torirsserver/torirs_server_control.c
  `ToriRSServer_ReloadConfigKind`).
- Both live proofs (native and web) log `Failed to load dat2 idk 307` once
  after login on the `fresh_lumbridge` fixture account, before any RUN: the
  fixture's appearance names an identikit the lane's cache does not hold.
  Not caused by the reload; unexamined.
- The main torirsserver builds no world, and so answers no control link, until
  its first login: a launched lane's link stays "connecting" (no timeout once
  HELLO is out) until the play client logs in, and a lane launched with no
  play client never attaches. `make -C src maped-live-e2e-play` is the
  real-client e2e (run_play.sh).
- A login now swaps in a rebuilt script pack (shadow, then swap; the same
  script.dat is a no-op), so a long-running server picks up a pack another
  session rebuilt at the next login; the renumbering check refuses a pack built
  with --renumber: src/torirsserver/torirs_server_scripts.c:955
  `ToriRSServer_ScriptsLoad`.
- Retired script tables are freed when no player, npc, world-queue slot or
  command state has a frame in them; the keys are file-static (16 retired
  tables, 8 stamped servers) because torirs_server.h is closed; over 16 held,
  a reload is refused: src/torirsserver/torirs_server_scripts.c:478.
- Square reload re-reads only the named squares; instance copies made from a
  reloaded source square are not rebuilt, and dynamic loc changes (opened
  doors) in it reset to the cache's state; the reload=1 mark is per pool slot
  and file-static: src/torirsserver/torirs_server_world.c:21434
  `ToriRSServer_WorldReloadSquares`.
- CTL_STATUS stale_count is always 0 (ToriRSServer_PackCheck keeps no list):
  src/torirsserver/torirs_server_control.c:874.
- The control link is pumped once per tick (each round trip up to 600 ms); not
  supported on win32 (winsock sockets cannot be dup()ed) or the browser server;
  the connection table is process-wide (8 slots):
  src/torirsserver/torirs_server_control.c:557 `ToriRSServer_ControlAdopt`,
  :1240 `ToriRSServer_ControlTick`.
- torirs_server_control.c is compiled by an `#include` at the end of
  torirs_server_ws.c:658 because src/makefile is closed; moving it into
  TORIRSSERVER_CORE_SRCS needs ws.c's sock_would_block, sock_interrupted and
  sock_set_nonblocking made non-static.
- Spawn reload pairs by (id, x, z, level); a file new since boot merges last,
  so its order can differ from a cold boot; the generated lane
  (areas/world/configs) is refused as restart-needed:
  src/torirsserver/torirs_server_world.c:21585, torirs_server_content.c:2237.
- live_reload_test compiles its packs from a stage of the LIVE content tree;
  other sessions' half-done edits can break that compile, so after one
  success it reuses the packs (`packs.ok`,
  src/torirsserver/test/live_reload_test.c:167; LIVE_RELOAD_RESTAGE=1
  rebuilds). It is not on test-editor (boots a whole server).
- test-torirsserver-embed (21 failures) and test-torirsserver-servpack-boot
  fail identically on the phase-3 HEAD (content and scratch-path causes).
