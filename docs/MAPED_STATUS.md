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
palettes, square picker, New, locs, spawns, inspector, catalog, console,
builders, interface and CS2 tools, Live) is in the same window; the View menu
opens the hidden ones. The 3D view is itself a panel, **World**: View > World
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
relay stay mirrored. World pops out as the viewer window looking exactly
where the World panel looked (same pose; the pinned level becomes its
level cap through the boot STATE_SYNC); any other module pops out as that
panel alone, filling the window (no menu bar, no world drawn; the world is
still loaded around the camera's square so App-bound panels answer). The
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
  `TORIRSMAPED_LIVE_DIR` (the live lane, below).
- `TORIRS_EDITOR_POPOUT=<module>[,<module>...][@<frame>]` -- for a
  photograph: at main-window editor frame `<frame>` (default 300) pop each
  named panel out, as its title-bar button does
  (src/app/app_editor_view.c). `TORIRS_EDITOR_RESIZE=<module>,<l|r|t|b>,<dx>,<dy>[@<frame>]`
  presses that panel's border on that edge, moves it by (dx, dy) and lets
  go (default frame 200). A popped-out window inherits its leader's
  environment, TORIRS_PRESENT_BMP included: `TORIRS_EDITOR_VIEW_BMP=<dir>[@<frame>]`
  makes each write `<dir>/view_<module>.bmp` at its own frame (default 400)
  instead.
- `TORIRS_EDITOR_OPEN_CMD` -- the Triggers panel's and the catalog's "Open
  in editor" command, `{path}` (the absolute path, shell-quoted) and
  `{line}` placeholders, e.g. `code --goto {path}:{line}` or
  `subl {path}:{line}`. Unset: `code --goto {path}:{line}` when `code` is on
  PATH, else `open {path}` (macOS) / `xdg-open {path}`. Run through
  `/bin/sh -c '<cmd> &'` (src/editor/editor_host_local.c
  `Editor_HostLocalOpenInEditor`), never waited on; the command line is
  logged to the console. An environment variable rather than a manifest key
  (`[editor:boot] open_cmd=`) because src/bootmanifest is not the editor's.
- `TORIRS_EDITOR_TRIGGERS=<subject>[,file][,record][,add=<kind>[:<op
  text>]][,check]` -- for a photograph: once the layout ran, dock the
  Triggers panel over the right half of the world on record `<subject>`
  (with `file`, `<subject>` is a path under server/scripts and the panel
  lists that file); `record` opens the Record panel beside it, `add=` runs
  Add trigger, `check` runs Check (src/editor/panels/edpanel_triggers.c).

**No plugin rail.** An editor window -- the main one and every popped-out
panel or view -- carries no plugin rail: the Manage Plugins wrench and the
plugin strip are game chrome. main.c hides the SDL strip before the window
opens (`PlatformWindow_ChromeSetRailHidden`) and the plugin panel publishes
an empty rail snapshot for an editor App, which keeps the web page's rail
empty too (owner, 2026-11-02).

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
`live-cheat`, `build packs|client`, `run`, and for spawns and records:

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

**Web**: `make -C src servers && make -C src web`, then
`src/build/io_server --manifest manifests/manifest_osrs241_mapeditor.ini` and open
`http://localhost:8088/` (single tab; `panel=tab` in the manifest gives the
two-tab layout). A web viewer tab boots with
`?editor_view=1&maped=wss://<daemon>&group=N[&camera=...]`. The web build
carries no BuildRunner or launch (TORIRSMAPED_JOBS_NATIVE 0), so BUILD/RUN/
LIVE_ATTACH launch answer UNSUPPORTED there; the native daemon over its
WebSocket transport is the editor server for a web client.

A web client PLAYING on a live lane (a RUN goes live in it, row 16): serve the
page with io_server (`--root` the web build, `--boot-root` a tree whose
manifest's `[net:boot]` names the lane's game port and whose `dir=` resolves to
a dat2 cache, e.g. the lane's), and open
`/?args=--manifest,manifests/manifest_osrs241.ini,--connect,127.0.0.1:<game>,--user,<name>,--pass,<letters+digits>&ws=ws://127.0.0.1:<game>/&js5_url=ws://127.0.0.1:<js5>/&idb=<fresh name>`
(add `&env=TORIRS_CACHE_INVALIDATE_DEBUG%3D1&env=TORIRS_LOC_CFG%3D<id>` to
see the reload). The lane's server builds its world, and answers the daemon's
control link, only after that first login.

**The launched live lane** (edit, RUN, the live server and clients update):

    torirsmaped ... --content-dir <writable tree> --repo-root . --live-dir <live dir>

`--live-dir` (or `$TORIRSMAPED_LIVE_DIR`) binds the native BuildRunner and
LiveLink. Everything a build writes lands under the live dir:
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
the content dir of a lane must be writable.

The whole loop, scripted, is `make -C src maped-live-e2e`
(src/maped/test/live_e2e/run.sh, below).

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
| 8 | Asset catalog | server-side index of packs, allocs, compacks, config blocks, overlays, interfaces, cs2/rs2 headers, spawns, maps; CATALOG_QUERY/RECORD (a record's fields in the pack's merge order: all.<type>, `.generated.` overlays, authored overlays, path order within a rank); mtime refresh; the Catalog panel queries it (every kind, settled find, paged list, record view, Edit record..., Copy to new; edpanel_catalog_model.c) | first build blocks the server pump (~1 s warm, ~6 s cold) |
| 9a | Active level | menu bar Level row after View: `Auto 0 1 2 3 Above`, the pressed button is `tools.edit_level`, Above (pinned only) draws every level; keys PageUp/PageDown step and clamp 0..3 (from Auto, from the plane under the pointer), Ctrl+0..3 pin, the same chord again = Auto; World panel title "World - level N"; a pin caps the view at itself (`vis_level` follows, levels above and roofs not drawn), Auto draws every level as before; `edp_level_*` in src/editor/panels/edpanel_common.c; synced as STATE_EDIT_LEVEL (another window's pin moves the cap too, menu bar tick); with a level pinned the hover, every tool's click (select included), the brush footprint, ghosts, rectangle and inspector use the PINNED plane's tile under the pointer (`app_map_editor_pin_hover` re-picks against that level's height field when the pick met another plane; `Editor_PanelHoverTile`); the free camera's eye shifts by the height between planes on a pin; an empty upper level takes paint, heights, locs and spawns (flushed locally) | see open issues (World, views) |
| 9 | Terrain tools | brushes with radius/falloff/step, palettes with shape thumbnails, flags, level radio, rectangle select/copy/paste/fill/clear; brush footprint under the pointer (`Editor_Terrain_BrushFootprint`, same weights as the stroke, falloff shown as wash intensity, follows a drag; >121 tiles as nested washes) | — |
| 9b | Square picker and New square | atlas (storages, world-map areas incl. Unmapped instance regions, colour, spawns, flags), picker GRAPH with area tabs, zoom, hover, search, open/shift-open/right-click; New modal (coordinates, storage maps/ or a lane with lane creation, template empty/copy/flat) -> DOC_CREATE. **World map under the grid** (2026-11-02): the page's area drawn as the CS2 world map (595) draws it -- the client's own renderer (game/rs_worldmap_render.c, new `RS_WorldMapRender_RegionPixels`: a region baked into a caller buffer, same loads/waits/request table as `RegionSprite`, plus `SetFrameBudget`), kept at one pixel per tile per region for the process (game/rs_worldmap_overview.{h,c}, ~16 KB a region, never evicted) and box-filtered into a window of WORLD squares through the area's compositemap records (whole regions and 8x8 chunks, so a dungeon page shows its squares where they are in the world). The editor side (editor/editor_worldmap.{h,c}) asks the App through four `EditorUiAppServices` members (`worldmap_area` by the atlas area's internal name, -2 while the cache's areas load; `worldmap_poll`; `worldmap_render`; `image_publish`, two scene sprites `UITREE_SCENE_EDITOR_PICKER_MAP_ID(0/1)` taken in turn), for the visible squares plus a quarter of the view each side; the picture is keyed on area, cell, pixels per square, window and what the page shows (`Editor_Picker.atlas_serial`, page, storage filter, collection) and rendered again only when that key changes or while regions are still arriving (polled every tick, redrawn at most every 10 ticks, then once more when the last lands) -- a pan inside the window only moves it. Squares the page does not show (no map in any storage, another page, filtered out) are dimmed, black where the map has nothing, a faint grid from 8 px a square; the cells are no longer filled over the map, the outlines (selected, open, unsaved, created), hover, the empty-cell mark, axis labels and view frames are drawn over it. The chrome's GRAPH gained `TORIRS_CHROME_GRAPH_IMAGE` / `ToriRSChrome_GraphImage` (a scene-sprite prim stretched over the box, clipped to the well; the DOM executor gets the scene id in the item's value). A click on an EMPTY square now opens New square prefilled with it. No services (headless harness, web -- not filled under `__EMSCRIPTEN__`): the picker as before. Unmapped page: no area, no map. Cold full surface (1632 regions, osrs241, this Mac, private OPT build): 0.73-0.80 s from the first ask to the last bake (about 0.4 s of it one frame reading the regions' tiles, the rest four polls of up to 512 bakes at ~0.12 ms each); afterwards instant. `TORIRS_WORLDMAP_STATS=1` prints each poll's regions-to-come and elapsed ms (stderr); `TORIRS_EDITOR_PICKER=<area>[,<cell>][,<x>,<z>][,new]` shows a page at a cell centred on a square (`new` clicks it) for photographs (src/editor/panels/edpanel_squares.c). Tests: editor_worldmap_test (22: the mapping, whole region and chunk, box filter, window edges; the composition), editor_panels_test `test_worldmap` (fake App: area asked once then after the retry, only the visible window plus margin asked for, the picture reused on 20 quiet ticks, a one-square pan renders nothing, a zoom renders once, empty-square click -> New square prefilled, Unmapped and no-services fall back to filled cells). Photographed 2026-11-02 (whole surface at 3 px, Lumbridge/Varrock at 24 px, New square from empty m63_44) | picker Delete only clears the square (no DOC_DELETE); a NEW square has no `pack/5_maps.pack` line, so BUILD client cache cannot import it yet; see open issues (Square picker world map) |
| 10 | Placement tools | loc multi-select, nudge (cross-border as one undo), rotate, reshape, duplicate; spawn place/delete/suppress, spawn multi-select and Duplicate (Ctrl+D); translucent npc/obj placement ghost with footprint (`map_editor_ghost.c` spawn ghost, Place click writes the spawn row via `Editor_Locs_PlaceSpawn`); Move tool outlines every selected loc's destination; **spawn edit form** (Spawns > Edit tab: kind, name -- pack symbol, `#<id>`, or a display name the catalog resolves, refused when unknown -- abs X/Z, level, count, Suppressed; Apply = one undo step "edit spawn"/"move spawn" via `Editor_SpawnEditPlan`: SPAWN_EDIT in place, delete+add across a square or kind, suppress+add for a generated row; a multi-selection takes each changed field, X/Z/Level as an offset); **spawn move** typed or "Move: click a tile" (the next world click, `editor_panel.c`), the destination footprint and wander square previewed under the pointer; **wander preview** ("Show wander range", default on: each selected npc's wanderrange square filled and maxrange leash outlined on its level, also around the npc spawn ghost; values from the Record panel when it holds the npc, else the catalog's merged record, else 5/7; `MapEditorWander_Rects`, drawn in `app_overlay.c`); orphan `-name` rows listed and editable; ctl `spawn-edit`/`spawn-move` | arrow nudges commit per press (no preview); the move preview is footprint + wander square, not a translucent npc model; a delete+add edit's undo re-adds the row at its section's end |
| 11 | Loc interaction builder | `.interaction` model, loc emitter (oploc/oplocu/aploc), step templates, sscompile Check | Check runs synchronously on the UI thread |
| 12 | NPC interaction and dialogue builder | npc emitter, dialogue graph to nested if/else, multiloc/multinpc shell editor; **Record panel** (Catalog > Edit record..., Spawns > Edit npc record...): one loc/npc/obj record over every file that states it -- `configs/all.<type>` and each server overlay the catalog lists, as block-scoped CFG documents -- every field resolved to the layer that wins it (rank 0/1/2, path order, `param=<name>,..` per name), plus every key of `fields/<type>.ini` the record states nowhere; Set field writes where the field is stated or "override in <layer>" (opening the block there with CFG_BLOCK if needed), Remove field shows the layer beneath; one undo step "edit record"; `editor_cfg.h` Editor_CfgRecord; ctl `cfg-set` | STATE_VAR_PREVIEW not honoured by the world's multiloc resolve |
| 13 | Varp and varbit editor | editor_vars bit map, overlap check, naming; editor_alloc mirrors ss_allocate.py | allocator and vars not wired to the protocol or a panel; constants; live ::getvar/::setvar |
| 14 | CS2 and the interface editor | `.if`/`.compack` codec (982 interfaces byte exact), component tree, inspector, hooks, CS2 run/trace/watch, CODE editor with `cs2 compile`, App mount/pick/refill | IF1 type 8 not mountable; IF_FIELD values over 190 bytes read only; cs2 compile synchronous |
| 15 | Save, save as new, export, pack | atomic text saves, FACT_SAVED; SAVE_AS of a loc/npc/obj record (fresh id in pack/<type>.alloc, .client line, every field; one undo step; catalog and shell editor "Copy to new...", `torirsmapedctl cfg-copy`); BUILD packs (`make torirsserver-packs` into the live dir); BUILD client cache (`cachepack pack --incremental`, only changed archives written, reference table bumped, plus saved squares by archive) | Save as new for squares (DOC_CREATE copy covers it), interfaces and builder subjects; export/import lane; incremental repack of interfaces, CS2 and other assets in BUILD client cache; scripts lint and cs2 compile as BUILD kinds |
| 16 | RUN and the live lane | RUN = SAVE_ALL -> packs -> client cache -> CTL_RELOAD -> FACT_RUN_DONE/LIVE_STATUS, stop at first failure, pending set kept on a failed reload; control link (0xED) with gate ticket; script hot reload (shadow table, swap at the tick boundary, old table freed when no frame holds it); sequence/healthbar/struct config groups live; square reload with one REBUILD per covering player and reload=1 on REBUILD_REGION; edited-lane spawn reload; CACHE_INVALIDATE (our opcode 237) sent at 239/241 before the REBUILD; Live panel; launch and attach; scriptrun `--control-port` **Config groups go live (phase 9, 2026-11-01):** the editor snapshots `servpack/pack.manifest` before the packs build and diffs its `archive` lines after it (`ToriRSMapEd_BuildServPackDiff`), sending one reload row `(255, group)` per changed group beside the script pack and client rows (build log `servpack: N archive(s) changed in M group(s)`). The server (`ToriRSServer_ReloadConfigKind`, `ToriRSServer_ContentReload{Locs,Npcs,Objs}`) checks the whole kind against the open pack first and keeps the running table on refusal; loc replaces the info tables, scene records, defs, categories and op overlays and owes every logged-in player one REBUILD; npc re-points every live `npc->def` by type in one bounded pass while the old array is held; obj replaces the info table and requirement rows; seq, healthbar and struct as before. A RESTART_NEEDED answer finishes the RUN with restart rows instead of failing every later RUN. **Client:** CACHE_INVALIDATE on 2/6, 2/9, 2/10 evicts the decoded loc/npc/obj records (retired, never freed under a holder) and the dat2 group cache, then the provider's archive hook (registered in main.c, native) drops the platform's decompressed copy and re-primes JS5 -- 255/255, 255/<index>, the rewritten group (`Js5ClientInvalidateArchive`; js5_server re-checks its cache on every master request); the owed REBUILD is forced past the same-zone early-out so placed scenery re-decodes. Proven: e2e RUN (c) loc op3 and (d) npc op4 pressed by the never-relogged bot (16 s / 13 s); a real client logged into a real torirsserver lane re-decoded blueflower's op3 as the new text after one RUN, no relog (`TORIRS_CACHE_INVALIDATE_DEBUG=1`, `TORIRS_LOC_CFG=<id>`). **Web client (2026-11-01):** main.c registers the same hook on the emscripten build (`PlatformWeb_IO_ArchiveInvalidateHook`, platform_web_api.c); `PlatformWeb_IO_InvalidateArchive` (platform_web_io.js) releases the decoded-archive LRU entry, drops the resident container and the index's reference-table memo (owned decode retired, never freed) and moves the table's generation so a read already out keeps nothing; the host's `invalidateArchive` (torirs_hostio.js -> torirs_js5.js) parks the index's outstanding requests (socket reopened when one was on the wire), fetches 255/255, 255/<index> when the stored table no longer matches the master, re-validates every IndexedDB-stored group of the index by its container CRC against the new table (rscache decode, `ToriRS_WebApi_ReferenceTableArchiveCrc`) and overwrites only the rewritten ones; reads of the index wait meanwhile; re-primes are serialised. Proven on a real torirsserver lane in Chromium: one RUN, no relog, the console showed `io 2/6: 1 decompressed copy dropped`, `js5 fetched 255/255`, `255/2`, the 8 groups the client build rewrote, then `loc_cfg 1187 decoded: name='Flower' ops [] [] [Smell] [] []` (`?env=TORIRS_CACHE_INVALIDATE_DEBUG%3D1&env=TORIRS_LOC_CFG%3D1187`). Tests: web_io_invalidate_test 5, js5_invalidate_test 7 (`make -C src test-web-cache-invalidate`, in test-web-channel). | player list and position in the control protocol; Restart offer; stale list (always 0) |
| 17 | The web | wasm editor client, viewer tab boot, capabilities gate the menus | browser BuildRunner/LiveLink (answer UNSUPPORTED); content tree host binding |
| 3c | Areas and Collections | **AREAS document** (kind 5, key `areas`, `<content_dir>/editor/areas.txt`: in the content tree so the team shares and reviews it, outside every pack input so naming an area never changes a build; header comment in src/editor/editor_areas.h): one record per line, `area <name> \| level <0..3\|all> \| rects <x0> <z0> <x1> <z1>[, ...] [\| colour <0..7>] [\| note <text>]` and `collection <name> \| squares <key> ... [\| note <text>]`, names unique per kind; line-preserving codec (src/editor/editor_areas.c: comments, blank lines, CRLF, no final newline byte-identical; the first record brings a 3-line header comment, the last removed takes it back, an emptied document's save removes the file); opened by every session at `Editor_OpenHost`, so every window mirrors it; held as text, applied by parse -> edit -> emit; **commands** AREA_SET 10, AREA_REMOVE 11, COLLECTION_SET 12, COLLECTION_REMOVE 13 (`u.area`: `int line`, the record line before and after, each <= 511 bytes; wire `i32 line, string before, string after`; a SET with a before replaces by name, a rename when the names differ; a REMOVE's undo re-inserts at its old line; refused when the bytes do not match or a name is taken), History rows from the codec ("Mark area Lumbridge castle", "Rename area A to B", "Set level of area A to 1", "Add rect to area A", "New collection C", "Add 1 square to collection C", "Delete area A"); **Areas panel** (module "areas", View menu, dock order after "squares", admitted when the column has room): Areas tab (list name/level/rects, rect status, Name, New from rect -- the Rect tool's rectangle on its level, "no rectangle" said in the panel --, Add rect (to the selected), Rename, Level + Set level, Delete, Go to -- the free camera to the area's centre, opening its square first when it is not loaded --, Show outlines (on), Hide others) and Collections tab (list name/squares, current square = the picker's focus else the latched tile's square, From open (squares), From picker (the shift-click set), Add / Remove current, Rename, Delete, Open all -- each as a document, into the world one load at a time, a confirm past 16); the Rect panel's "Mark area from rect"; the Squares picker's **Collection** dropdown (`all` + every collection: only its squares, on any Area page, Fit frames them; follows edits; off when the collection goes); **world overlay** (app_overlay.c `app_overlay_build_editor_areas` from `Editor_Areas_Overlay`: each rect washed in its colour on its level, outlined along the ground, the name in the overlay's TEXT at the NW corner of the first rect, the selected area brighter; through `world_emit_desc`, so it follows the World panel); ctl `areas`, `area-set`, `area-remove`, `collection-set`, `collection-remove`; tests editor_areas_test, editor_panels_test `test_areas`, maped_embed_test, maped_daemon_test.sh (9) | see open issues (Areas and collections) |
| 3b | History panel | `Editor_History` (editor_cmd.c) rebuilt from the FACT_CMD stream + own undo/redo; rows newest first with names ("Raise 21 tiles m50_50 L0", "New square m1_1 (copy of m50_50)"), undone prefix, current marker, filter by key, click-to-jump; rebuilds only on serial change | a late-joining window lists only steps since it connected; another window's long drag may show as several rows until an undo re-cuts them |
| 5b | Default layout | `Editor_UiLayout` (editor_ui.c) at boot/resize/scale/Reset: menu bar full width, tool rail wraps to 2+ columns, one right column 22% (200..400 px at 1x; order terrain, palette, locs, spawns, rect, inspector, squares, areas, catalog, history), panels admitted while >= 160 px, spare height shared, console strip under the world when <= 1/4 height, the World panel docked over what is left; a saved layout (`editor_layout.ini`) applied before the first pass; panels that do not fit are hidden (View menu); dragged panels keep their place until View > Reset layout; View > Snap to grid | tested 1280x800..2560x1440 at scale 1 and 2 headless + photographed at 1280/1530/1920 |
| 7c | Hotkeys | docs/MAPED_HOTKEYS.md is the user's reference. **One table** (src/editor/editor_hotkeys.{h,c}): 96 actions (File, Edit, Tools, Brush, Level, Camera, View -- one per View-menu panel module, the unbound ones included), each with an ini id, group, name, doc row, context mask (G, W, L, R, S, N = no loc; L excludes S and N) and kind (edge, repeat after 300 ms at 20 Hz, held); two chords per action at most; chord parse/format (`ctrl+shift+z` <-> "Ctrl+Shift+Z", "Cmd" on Apple); `editor_hotkeys.ini` codec (non-default bindings only, `<action> =` unbound, a bad line skipped and reported); conflicts per context; `Editor_HotkeysResolve` over a plain snapshot (edges, held keys, live contexts) -> fired actions, active held actions, CLAIMED keys; the later binding wins a clash. **One dispatcher** (src/app/app_editor_hotkeys.c, called first in the frame, before the developer overlay): snapshot from LibToriRS_Input, gates (editor open, no text field / model view / menu drop / minimenu), Hotkeys-panel capture, resolve, dispatch through `Editor_HotkeyRun` (src/editor/panels/edpanel_hotkeys.c: the one entry point the File / Edit / View rows also call) or the App-bound actions (camera flight and turn read in app_camera.c, eyedropper I / Shift+I, Ctrl+A select the hovered square's locs on the edit level, Return apply to selection, Ctrl+V paste at the hovered tile, Home / Ctrl+Home framing through the Areas panel's camera move, Escape ending a drag, PageUp/Down from the plane under the pointer); a fired chord sets `input_frame_consumed`, marks `hotkey_consumed` (re-marked after app_ui_hotkeys clears it) and logs `editor: <action> (<chord>)`; claimed chords are skipped by `app_debug_key_down/held` and the world debug keys. Previous tool (`EditorUi.previous_tool`), Tab hide/show every panel but World (`Editor_UiToggleAllPanels`, remembers what was open), brush radius/step/strength steppers that move the Terrain sliders (`edp_brush_step`), Ctrl+4 Auto, Ctrl+5 Above, Shift flies 4x. **Hints** from the table, re-labelled on every rebind (`hotkeys.serial`): menu rows `Name\tChord` drawn right-aligned by the chrome (menu-mode dropdown rows and TREE rows split at a tab), tool rail / Level / Locs / Spawns / Terrain slider tips end with the chord; the old `[D]/[R]/[S]/[Del]` literals are gone. **Hotkeys panel** (module "hotkeys", View, F1): search, capture (Esc cancels, Backspace unbinds, a lone modifier only for Fly faster), clashes red on both rows, Reset to defaults. **Persistence** `editor_hotkeys.ini` / `TORIRS_EDITOR_HOTKEYS` through task_editor_hotkeys_io.c (the layout's IO pattern). Tests: editor_hotkeys_test (786 checks, incl. every action's id and row in the doc), editor_panels_test `test_hotkeys`. Photographed 2026-10-31 (F1 panel, `3` arming Raise, F12 hiding World, Ctrl+Z undoing a `torirsmapedctl tile-height` edit with the console line) | see open issues (Hotkeys) |
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

What the end to end does NOT cover: a real game client (src/torirs) logged
into the lane (the scriptrun bot decodes with the client's parser but has no
cache provider, so CacheProvider_InvalidateArchive is exercised only by
live_reload_test's decode check); io_server (no io manifest in the lane); the
Live panel driving the RUN (torirsmapedctl drives the same frames; the panel
is covered by test-live-panel against the python mock peer); a gated server.

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
  `renderGraph`) skips a `TORIRS_CHROME_GRAPH_IMAGE` item, and the App does
  not fill the four map services under `__EMSCRIPTEN__`, so the web picker
  keeps its filled cells. To show it there the executor needs the pixels:
  e.g. a new chrome command carrying the picture (or a URL the page can fetch
  from the bridge) keyed by the scene id the item already sends, drawn into
  a canvas behind the item list.
- Map element icons (banks, altars...) are not drawn: the overview keeps
  pixels only (`RS_WorldMapRender_RegionPixels` passes no slot). The landmark
  map scenes (trees, rocks) are baked in when the bridge has the mapscene
  pack resident.
- Cold load blocks one frame for ~0.4 s while the runner reads all 1632
  surface regions' tiles (table 18 plus the table 20 ground PNG per region,
  src/engine/dat2/task_dat2_worldmap_geography_load.c); a later open is
  instant. A smaller first window (the visible squares only, the margin
  later) or decoding the ground PNGs off the frame would cut it.
- The overview's bakes are a process-lifetime static in
  src/app/app_map_editor.c (`g_picker_worldmap`; the App holds no field for
  them by check-app-boundary) and are never freed or evicted (~25 MB for the
  whole surface, more as dungeon pages are visited).
- A content edit does not redraw the map: the picture is the CACHE's world
  map (what 595 shows), not the content tree's squares. A square created or
  re-terrained in the editor shows the cache's old picture (or black) until
  the client cache is rebuilt and the bakes dropped (no hook drops them yet).
- Ocean tints differ by region on the surface (teal, blue and purple
  patches east of Al Kharid and south of Zeah). The dimming only scales
  brightness, so the hue is the renderer's; the land matches a headless 595
  dump of Lumbridge (TORIRS_WORLDMAP_ZOOM=25) but the open sea has not been
  compared against 595 at the same place yet.

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

### Live lane: editor side

- The control protocol has no player list and no player position (CTL_STATUS
  carries a count), so the Live panel shows a count, the followed player is
  typed by name, and STATE_LIVE_FOCUS x/z/level are -1. Needs a CTL_PLAYERS
  frame: src/torirsserver/torirs_server_control.c:857 `ctl_send_status`,
  src/torirsmaped/torirs_maped_live.c.
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
  its first login; attaching an editor to an empty real server times out. The
  e2e has no real-client step; the real-client proof was run by hand.
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
