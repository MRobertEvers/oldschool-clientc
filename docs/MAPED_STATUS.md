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
  the tools): 37 GLYPH slots (tool icons, marks and boxes, scroll and tree
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
- GLYPH slots with more than two inks flatten in the mask route (ToolPlace,
  ToolDelete, ToolPaintUnderlay, ToolSmooth, SliderKnob, SaveButton): the
  skin's hand-drawing queue, or candidates to reclassify.
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
  (58 slots, 37 GLYPH / 21 TEXTURED, tools/chrome_skin_tiers.py),
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
  that is the tier.
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
- Editor layout computed as `ToriRSChrome_Scale(ui) * N` sits tight at
  fractional q (a slider over "Strength Shift+[]" at 1.5x).
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

The editor's boot with a content dir no longer aborts:
`Editor_Triggers_SetPaths` (edpanel_triggers.c) resolved `realpath` into
the panel's 1024-byte buffer, which _FORTIFY_SOURCE stops as an overflow; it
now resolves into a PATH_MAX buffer, keeps the given path when the root does
not exist yet, and asserts the result fits.

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

- Not measured on the owner's tree here (the content submodule is not
  checked out): the real-tree half of `test-catalog-perf` prints it.
- A record view (CATALOG_RECORD) or the atlas asked during the warm build
  still finishes the build first: those requests carry no flag.
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
