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

## How to run it

### Quick start (2026-11-02)

**Desktop, one command:**

    ./launch run osrs241-mapeditor

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

    ./launch run osrs241-mapeditor-web      # then open http://localhost:8088/

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
(the lane's processes); `./launch logs osrs241-mapeditor-web [torirsmaped|
io_server|js5_server]` (the web run's services); the editor's Console panel
and the Live panel's log (build lines, RUN outcomes, a dead server's log
tail).

**The lock message.** One editor server writes a content tree at a time:
`<content_dir>/.editor-session.lock` names its holder (`pid`, `since`,
`cmd`). A second editor on the same tree opens READ-ONLY and says who holds
it -- in the Console at boot, in the Inspector's first row, and in RUN's stop:

    read-only: /.../OSRS-Content/osrs-content is held by pid 43893
    (torirs --manifest manifests/manifest_osrs241_mapeditor.ini, since 16:22)
    -- close it, or run this window against the daemon

Close that process, or point this window at the daemon that holds the tree
(`server=tcp`, `osrs241-mapeditor-tcp`). A lock whose pid no longer runs (a
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
    src/torirs --manifest manifests/manifest_osrs241_mapeditor.ini

or through the launcher, which builds the client and prepares the lane first:

    ./launch run osrs241-mapeditor          # one window, embedded ToriRSMapEd
    ./launch run osrs241-mapeditor-tcp      # the window attached to a shared daemon (started for you, port 43610)
    ./launch run osrs241-mapeditor-view     # a viewer window of that session (start -tcp first)

(`profiles/osrs241-mapeditor*.ini`; `./launch list` names the osrs239 twins.
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
        [--view-binary src/torirs --view-manifest manifests/manifest_osrs241_mapeditor.ini] \
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

    src/torirs --manifest manifests/manifest_osrs241_mapeditor.ini --editor-view \
        --maped tcp://127.0.0.1:43610 --maped-group <client id> --camera x,z,level,yaw,pitch,zoom

or "Open in new view" in the picker (the daemon spawns it when started with
`--view-binary/--view-manifest`, else the client's launch service does).

**Web** (the Quick start's `./launch run osrs241-mapeditor-web`, by hand):

    make -C src servers torirsmaped && make -C src web
    src/build_opt/torirsmaped 43610 --content-dir OSRS-Content/osrs-content --repo-root . --live-dir build/live/default &
    src/build/io_server --manifest manifests/manifest_osrs241_mapeditor.ini --root build-web \
        --maped-port 43610 --page-query 'args=--manifest,manifests/manifest_osrs241_mapeditor.ini&js5_port=43595'
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
`/?args=--manifest,manifests/manifest_osrs241.ini,--connect,127.0.0.1:<game>,--user,<name>,--pass,<letters+digits>&ws=ws://127.0.0.1:<game>/&js5_url=ws://127.0.0.1:<js5>/&idb=<fresh name>`
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
| 12 | NPC interaction and dialogue builder | npc emitter, dialogue graph to nested if/else, multiloc/multinpc shell editor; **Record panel** (Catalog > Edit record..., Spawns > Edit npc record...): one loc/npc/obj record over every file that states it -- `configs/all.<type>` and each server overlay the catalog lists, as block-scoped CFG documents -- every field resolved to the layer that wins it (rank 0/1/2, path order, `param=<name>,..` per name), plus every key of `fields/<type>.ini` the record states nowhere; Set field writes where the field is stated or "override in <layer>" (opening the block there with CFG_BLOCK if needed), Remove field shows the layer beneath; one undo step "edit record"; `editor_cfg.h` Editor_CfgRecord; ctl `cfg-set` | STATE_VAR_PREVIEW not honoured by the world's multiloc resolve |
| 13 | Varp and varbit editor | editor_vars bit map, overlap check, naming; editor_alloc mirrors ss_allocate.py | allocator and vars not wired to the protocol or a panel; constants; live ::getvar/::setvar |
| 14 | CS2 and the interface editor | `.if`/`.compack` codec (982 interfaces byte exact), component tree, inspector, hooks, CS2 run/trace/watch, CODE editor with `cs2 compile`, App mount/pick/refill | IF1 type 8 not mountable; IF_FIELD values over 190 bytes read only; cs2 compile synchronous |
| 15 | Save, save as new, export, pack | atomic text saves, FACT_SAVED; SAVE_AS of a loc/npc/obj record (fresh id in pack/<type>.alloc, .client line, every field; one undo step; catalog and shell editor "Copy to new...", `torirsmapedctl cfg-copy`); BUILD packs (`make torirsserver-packs` into the live dir); BUILD client cache (`cachepack pack --incremental`, only changed archives written, reference table bumped, plus saved squares by archive) | Save as new for squares (DOC_CREATE copy covers it), interfaces and builder subjects; export/import lane; incremental repack of interfaces, CS2 and other assets in BUILD client cache; scripts lint and cs2 compile as BUILD kinds |
| 16 | RUN and the live lane | RUN = SAVE_ALL -> build (packs and client cache at once since 2026-11-02, `TORIRSMAPED_BUILD_RUN`; see "RUN speed") -> CTL_RELOAD -> play client -> FACT_RUN_DONE/LIVE_STATUS, with FACT_RUN_PROGRESS rows (a bar in the Live panel and at the foot of the main window), stop at first failure, pending set kept on a failed reload; control link (0xED) with gate ticket; script hot reload (shadow table, swap at the tick boundary, old table freed when no frame holds it); sequence/healthbar/struct config groups live; square reload with one REBUILD per covering player and reload=1 on REBUILD_REGION; edited-lane spawn reload; CACHE_INVALIDATE (our opcode 237) sent at 239/241 before the REBUILD; Live panel; launch and attach; scriptrun `--control-port` **Config groups go live (phase 9, 2026-11-01):** the editor snapshots `servpack/pack.manifest` before the packs build and diffs its `archive` lines after it (`ToriRSMapEd_BuildServPackDiff`), sending one reload row `(255, group)` per changed group beside the script pack and client rows (build log `servpack: N archive(s) changed in M group(s)`). The server (`ToriRSServer_ReloadConfigKind`, `ToriRSServer_ContentReload{Locs,Npcs,Objs}`) checks the whole kind against the open pack first and keeps the running table on refusal; loc replaces the info tables, scene records, defs, categories and op overlays and owes every logged-in player one REBUILD; npc re-points every live `npc->def` by type in one bounded pass while the old array is held; obj replaces the info table and requirement rows; seq, healthbar and struct as before. A RESTART_NEEDED answer finishes the RUN with restart rows instead of failing every later RUN. **Client:** CACHE_INVALIDATE on 2/6, 2/9, 2/10 evicts the decoded loc/npc/obj records (retired, never freed under a holder) and the dat2 group cache, then the provider's archive hook (registered in main.c, native) drops the platform's decompressed copy and re-primes JS5 -- 255/255, 255/<index>, the rewritten group (`Js5ClientInvalidateArchive`; js5_server re-checks its cache on every master request); the owed REBUILD is forced past the same-zone early-out so placed scenery re-decodes. Proven: e2e RUN (c) loc op3 and (d) npc op4 pressed by the never-relogged bot (16 s / 13 s); a real client logged into a real torirsserver lane re-decoded blueflower's op3 as the new text after one RUN, no relog (`TORIRS_CACHE_INVALIDATE_DEBUG=1`, `TORIRS_LOC_CFG=<id>`). **Web client (2026-11-01):** main.c registers the same hook on the emscripten build (`PlatformWeb_IO_ArchiveInvalidateHook`, platform_web_api.c); `PlatformWeb_IO_InvalidateArchive` (platform_web_io.js) releases the decoded-archive LRU entry, drops the resident container and the index's reference-table memo (owned decode retired, never freed) and moves the table's generation so a read already out keeps nothing; the host's `invalidateArchive` (torirs_hostio.js -> torirs_js5.js) parks the index's outstanding requests (socket reopened when one was on the wire), fetches 255/255, 255/<index> when the stored table no longer matches the master, re-validates every IndexedDB-stored group of the index by its container CRC against the new table (rscache decode, `ToriRS_WebApi_ReferenceTableArchiveCrc`) and overwrites only the rewritten ones; reads of the index wait meanwhile; re-primes are serialised. Proven on a real torirsserver lane in Chromium: one RUN, no relog, the console showed `io 2/6: 1 decompressed copy dropped`, `js5 fetched 255/255`, `255/2`, the 8 groups the client build rewrote, then `loc_cfg 1187 decoded: name='Flower' ops [] [] [Smell] [] []` (`?env=TORIRS_CACHE_INVALIDATE_DEBUG%3D1&env=TORIRS_LOC_CFG%3D1187`). Tests: web_io_invalidate_test 5, js5_invalidate_test 7 (`make -C src test-web-cache-invalidate`, in test-web-channel). **Play client (2026-11-02):** RUN = ... reload -> RUN_STEP_PLAY (6, numbered after DONE) -> DONE: the lane's play client (torirs_maped_live.h "The play client"; How to run it) is started when there is none, left alone when the server lists its account ("reloaded in the running client (<account> at x,z, no relog)"), started again when it exited or logged out; a lane that is down at the reload is LAUNCHED on what the RUN built and the client started (no reload owed), a lane that cannot launch says "Launch the lane first". The editor server owns the process like the lane's servers (a `ToriRSMapEdPlayLauncher` seam: native posix_spawn + waitpid; the test binds a fake), so RUN from any window, the Live panel's play line + **Play** / **Stop**, and `torirsmapedctl play | play-restart | play-stop` drive one client, headless or not; LIVE_PLAY (59) / FACT_LIVE_PLAY (179). **Control link:** CTL_PLAYERS (7) -> CTL_FACT_PLAYERS (134): logins since boot (the pool slots' login generations summed), active count, and per player account, display name, slot, login generation, tile; sent once after an accepted HELLO (which is how the editor knows to ask: an older server closes the link on an unknown frame), then polled with the status (every second while the client logs in). The lane's launched server now keeps its saves in `<live>/saves`, skips the welcome screen, and logs to `<live>/logs/`. **Client:** CACHE_INVALIDATE on index 5 (a map square) now evicts too (`CacheProvider_InvalidateArchive`: the split group, the IO layer's copies through the archive hook -- JS5 re-primes and refetches the square -- and the provider's decoded squares); before, a real client rebuilt the scene from the square it already held and a terrain edit waited for the next login (the scriptrun bot, having no cache provider, never showed it). Proven by `make -C src maped-live-e2e-play` (below) and photographed (a RUN from the editor window launched the lane and started the client; a second RUN raised a second spike in the same, never-relogged client). Tests: live_editor_test 146 (play section: launcher on a fake client binary -- manifest, fixture save, environment -- and the state machine: lane down, online, logged out, died, Play/Stop, detach, a server without CTL_PLAYERS, browser), live_panel_test 34, cache_provider_invalidate_test 25. | the play client's position does not reach STATE_LIVE_FOCUS (viewers do not lock to it); Play cannot raise an existing window (it reports "already logged in"); Restart offer; stale list (always 0) |
| 16b | Accounts and the Place player tool (2026-11-02) | **Accounts panel** (module "accounts", View > Accounts, starts closed, pops out, every window shares the same facts; src/editor/accounts/accounts_panel.c over the model in accounts_client.c): the lane's accounts are the saves under `<live_dir>/saves/*.ini`, listed by the editor server (only it knows the live dir; windows never touch a file). Per account a dropdown + status row ("online at 3222,3218 level 0: edits are cheats; values from the server's checkpoint" / "offline: edits write the save"), Refresh, **Use for RUN / Play** (the lane's play account at runtime, remembered in `<live_dir>/accounts.ini` [lane] so a daemon restart keeps it; FACT_LIVE_PLAY relays it to every window), Duplicate, Delete (offline only), Name + Password (+ Fixture) + New account (the save from the fixture, default the lane's play fixture; the password is claimed at the first login -- the server's rule -- and remembered in accounts.ini [passwords]); tabs **Position** (x z level + Move, To selection, the Place player tool), **Stats** (25 skills with level and xp; Set level, Set XP, Max all, Reset all), **Vars** (Find = a settled catalog query over varps and varbits by name or id; the save's varps with their catalog names; a varbit's base varp and bits from the catalog; Value + Set), **Quests** (every `quest` dbrow with [x]/[ ]/[?]; Complete = `::complete`, Mark complete = `::questmark`, Mark all complete; offline these are QUEUED per account and flushed 3 s after its login; the queue is shown), **Items** (Backpack / Worn / Bank slot lists; Slot + Obj by id or name or From Catalog pick + Count; Set, Remove, Clear backpack), and a Log of the server's answers (+ the Live log's lines while an online cheat is answered). **Every edit applies at once** (no Apply/Revert): the server chooses the path from CTL_FACT_PLAYERS -- ONLINE = a cheat on the control link (`setlevel`, `xp` (grows only), `maxstats`, `setvar <name>`, `item <id> <n>` (+ `wield`), `tele <coord>`, `complete`, `questmark`, `clearinv`) followed by `::checkpoint <leg>` whose file `saves/checkpoints/<leg>.ini` is read back as the view's SNAPSHOT (flag; "values from the server's checkpoint"); OFFLINE = the save edited in place with a line-preserving codec (comments and unknown keys kept; a varbit spliced into its base varp's bits; stats as `<boosted> <xp_tenths>`). **Daemon module** src/torirsmaped/torirs_maped_accounts.{h,c} (compiled into torirs_maped.c after the live half): requests 65..73 (ACCOUNTS_LIST 65, ACCOUNT_VIEW 66, CREATE 67, DELETE 68, DUPLICATE 69, USE 70, SET 71, PLACE 72, ACCOUNTS_QUESTS 73; range 65..80), facts 191..194 (FACT_ACCOUNTS 191, FACT_ACCOUNT 192, FACT_ACCOUNT_STATUS 193, FACT_QUESTS 194; range 191..200); one SET key vocabulary for the panel and the ctl (`player.pos`, `stats.<stat>[.xp]`, `stats.all max|reset`, `varps.<id|name>`, `varbits.<id|name>`, `vars.<name>`, `inv|worn|bank.<slot> <obj> [n] | -`, `inv.clear`, `quests.<dbrow> complete|mark`, `quests.all mark`, `cheat <line>`, `refresh`); the quest index = the `::complete` arms of quests/scripts/quest_cheat.rs2 (var + `^constant`, the constants read from every `.constant` under server/scripts) joined to the `quest` rows of configs/all.dbrow; the service pass (every pump + the daemon's idle loop) restores the play account, watches logins (queue flush), reads landed checkpoints (4 s timeout -> "wrote no checkpoint"). **Place player tool** (EDITOR_TOOL_PLACE_PLAYER, rail icon, hotkey P, `tool.place_player`): a world click sends ACCOUNT_PLACE for the lane's play account; a magenta tile marker follows the hover; the console says "online: ::tele ..." or "offline: wrote saves/...". The arrows and every other key stay the camera's / the table's. **ctl**: `accounts`, `account-view <name>`, `account-new <name> <password> [fixture]`, `account-dup <name> <new> <password>`, `account-delete <name>`, `account-use <name>`, `account-set <name> <section.key> <value...>`, `account-place <name> <x> <z> <level>`, `quests`. `TORIRS_EDITOR_ACCOUNTS=show[,tab=<n>][,account=<name>]`. Tests: accounts_test (140: codec byte for byte, quest arms, every SET key offline, queue, use/dup/delete, the ONLINE path against the mock control peer incl. the flush and a landed checkpoint), maped_daemon_test.sh (11), editor_panels_accounts_test (48), `make -C src maped-live-e2e-accounts` (the real server + play client: 26 ok, 0 failed). | varcs: no server op or cheat writes a varc for a player and a save holds none, so the panel has no Varcs tab (said here rather than faked); online, one slot cannot be emptied and the bank cannot be edited (no cheat; the Items tab says so); online xp only grows (`::xp` adds); `::setvar` needs the catalog's name (the daemon refuses while the catalog is still indexing); the view online is the last checkpoint, refreshed after each edit, not live-subscribed; a server attached by address keeps its own saves dir (the module reads the lane's); `::checkpoint` is refused in a dialogue or a fight (the Log says so and the view falls back to the save); accounts.ini holds passwords in plain text (a dev lane); the quest states of the four proc-arm quests are [?] |
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
copy-on-write MIRROR of its base (mirror.py's rules, ported to C in
`ToriRSMapEdLanes_Mirror`): real directories, one symlink per file,
directory symlinks for the pure-asset trees (models, sprites, ...), real
copies of `pack/` (the id allocation rewrites its ledgers), no pack outputs
and no locks. A save renames a temporary over the symlink, so the edited
file is a real file in the lane and the base is untouched; the lane's
**changes** are its real files that differ from the base (modified /
added) and the base's files it no longer has (deleted). 102 k entries
mirror in about two seconds; a changes scan reads only the real files.

- **Serving a lane**: `torirsmaped ... --lane <name>` (or
  `$TORIRSMAPED_LANE`), the manifest's `[editor:boot] lane=<name>` for the
  embedded server (`ToriRSMapEd_OpenLane`, `ToriRSMapEd_EmbedStartLane`,
  `Editor_HostOpenMapEdEmbedLane`; bootmanifest `editor_lane`), the launcher's
  `[service:torirsmaped] lane=` (else the manifest's). The lane is made on
  first use; its live dir is bound for BUILD, RUN and the servers. `main`
  (or no lane) is the shape before: the tree itself. Both editor manifests
  say `lane=working`.
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
- Tests: lanes_test (62: names, the mirror's rules, create / lookup / list,
  the change scan, publish to a new lane / another lane / main with reset,
  the lock refusal and force, discard), maped_daemon_test.sh section 12 (a
  daemon on `--lane work`: an edit saved through it never touches the tree,
  publish-new, publish main resets the lane, discard), editor_hotkeys_test.

Open: the Publish panel has no per-file pick (all or nothing); a lane's base
is always the main tree (no lane of a lane); a lane does not notice its base
moving under it (publish from main into a lane is the way to refresh one);
the mirror is per file, so a directory added to the tree later is not seen
by an older lane until it is re-made.

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

Tests (2026-11-04, this container, objects in build_wb60*):
test-chrome-widgets 328 checks, 0 failed, dom ok; test-editor-hotkeys 1689,
0 failed; test-editor-groups 2051, 0 failed; **test-editor-chords** (new,
content-free: every wired button wears and draws its keycap, the rail badges,
the slider labels, a rebind / unbind / reset relabelling them, a 300-byte
label logged once, a full ring) 363, 0 failed; the last three also at OPT=0
with asserts. test-editor-dropdown 36, test-editor-cmd 62, test-editor-areas
136, test-editor-panels-palette 50 and -accounts 48 (with `ulimit -s
unlimited`), all 0 failed; the web channel's node tests pass;
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
  src/app/app_editor_hotkeys.c still derives the contexts itself instead of
  calling `Editor_HotkeyContextsFrom`.
- Groups: dragging a tab out of a strip or a title bar onto one is not wired
  (`Editor_UiLeaveGroup` / `JoinGroup` exist; a LeaveGroup is not saved);
  Shift+Pop out of a whole group; the red attention count on tabs. Group
  titles are "A / B / C" (the baked font has no middle dot).
- The Small face has no ⌃ ⇧ ⌥ glyphs yet, so chords use the word spelling;
  a chord cut at 23 bytes can split a UTF-8 symbol, as labels can.
- Panel titles and dropdown / structured option labels still truncate
  without a record: they are not widget fields, so the ring does not see them.
- The DOM button keeps its red art, so its keycap sits a few pixels from the
  canvas one; the IE8 keycap and badge CSS is unchecked in MSHTML.
- `make -C src torirsmaped` fails to link here (pow / floor: its recipe links
  no `-lm`); the same objects link with `-lm` added. Not from this change.

## Open issues

Every issue the phase reports left open, by area, with where to start.

### Documents, protocol, editor server

- DOC_CLOSE never unloads a document (no linger policy):
  src/torirsmaped/torirs_maped.c:909 `handle_doc_close`.
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
  one-pixel bakes are never freed (~25 MB the surface, ~64 MB once the Full
  map has seen every area); the close-up pool is capped at 64 MB.
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
- STATE_VAR_PREVIEW is published (src/editor/builder/) but not honoured by the
  world's multiloc/multinpc resolve.
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
- Builder Check (sscompile) and CS2 compile run synchronously on the UI
  thread: src/editor/builder/builder_ssc.c:624 `Builder_SscRun`,
  src/editor/iface/iface_cs2.c:112.
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
      <client> --manifest manifests/manifest_osrs241_mapeditor.ini

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

Still slow, with where to start:

- A Catalog opened in the first ~15 s after the editor opens waits for the
  rest of the warm build (~6 s of work on this tree: scan 0.5-1.8 s, parse
  2.5-3.6 s -- cs2 935 ms, jm2 951 ms, jl2 527 ms, rs2 357 ms -- merge
  80-130 ms) at 6 ms a frame (`TORIRSMAPED_CATALOG_WARM_SLICE_MS`). The warm
  build's own slices cost one ~70 ms frame (configs/all.loc, 86 MB, is one
  file's parse) and one ~90 ms frame (the merge).
- A sweep that finds a changed file merges the whole index in one step,
  ~80-100 ms on this tree (`cat_merge`): one hitch per change seen, while
  the Catalog is being used (sweeps start only from requests). An
  incremental merge would remove it.
- A sweep lags: an external edit shows in a query once a sweep has seen it
  (a sweep is ~180-650 ms of 2 ms slices, ~1.5-5 s at 50 fps). A record view
  is always fresh for its own files.
- Kind "all kinds" queries test every record: 22 ms for '', 17 ms for 'o',
  12 ms for 'oa' on 405k records (loc: 1-2.5 ms); a key=value filter
  collects every record's fields: 'op=Chop' 18 ms (loc), 100 ms (all kinds)
  per keystroke, and a filter value never narrows (it is matched whole).
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
- Check compiles the whole tree on disk synchronously (about a second warm,
  the first stage copies ~40 MB); the window waits. Unsaved script documents
  are not in it (the owner's rule: scripts are edited outside).
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
- BUILD "scripts lint" (3) and "cs2 compile" (4) answer UNSUPPORTED:
  src/torirsmaped/torirs_maped_build.c:151.
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
</content>
</invoke>
