# ToriRSMapEd as a game editor: audit and roadmap

Owner, 2026-11-03: "I want the game editor to be a fully fledged game
editor. It should work on many machines and screen sizes. It should be fast
and efficient. Look for opportunities to make it more ergonomic to use as a
game editor. Take inspiration from game editors for other games."

This is the standing plan for that. docs/MAPED_STATUS.md says what is built
and how it is tested; this file says where the editor stands against the
editors people already know (Unity, Unreal, Godot, Tiled, Hammer, the
RuneScape private-server map editors) and what comes next, in order. Update
it when a row moves.

## 1. Where it stands (2026-11-03)

| Area | Other editors | ToriRSMapEd today |
|---|---|---|
| Scene editing | viewport with gizmos, box select, duplicate, snap, undo history | world view with select / move / nudge (swap on a taken tile), rect select, duplicate, rotate, reshape within a loc's shapes, Fit walls, snap grid, History panel, undo/redo of locs and tiles |
| Terrain | brushes with radius / strength / falloff, flatten, smooth, flags | Terrain panel: brushes (linear / smooth / flat), radius, step, strength, flags brush, underlay / overlay palette |
| Asset browser | thumbnails, search, drag to scene, favourites | Catalog (every config kind, settled search), Preview (3D well), Catalog palette (named 10-slot strips, thumbnails), Place tool with ghost |
| Hierarchy / outliner | tree of the scene's objects | Locs and Spawns panels of the open square, Inspector under the cursor; no cross-square outliner |
| Project navigation | project browser, recent files, "go to" | Squares picker over the real world map (zoom, Full map, search), New square, a command palette (Ctrl+P: actions, panels, squares, tiles, catalog records, recents), Go to tile (Ctrl+Shift+G), File > Recent squares |
| Play-in-editor | Play button, in-editor player, console | RUN (one build, progress bar, hot reload, launches the lane's server and play client), Accounts panel (stats, vars, quests, items, online or offline), Place player tool, Live panel (cheats, follow a player's vars) |
| Version / branches | source control integrations, scene merging | editor lanes: a copy-on-write working lane, Publish to main / a lane / a new lane, Discard; no per-file pick yet |
| Scripting | script editor, visual scripting | Triggers panel (add / open in your editor / check / run), builders for loc and npc interactions, shell editor, interface editor, CS2 tools; code is edited in your editor, not here |
| Layout | dockable, tabbed, draggable panels; saved layouts; multiple viewports | one dock column with fly-outs, drag / resize / maximise / pop out to a window (web: a tab), layout file, reset; a compact mode (panels collapse to title bars, automatic below 1280x800 logical, Ctrl+Shift+C), a status bar (tile, selection, tool, level, lane and unsaved count, live and RUN state), the window box remembered |
| Input | Unity / Unreal camera schemes, rebindable keys | WASD / RF fly, arrows orbit, middle-drag orbit, wheel zoom, trackpad pinch and scroll on the map, every action in a rebindable hotkey table with a Hotkeys panel and a doc; a Unity scheme (right-drag look, middle-drag pan, wheel to the cursor's tile, Alt+left-drag orbit, F frames, double-click recentres; `camera.scheme`), select all of a kind (Ctrl+Shift+L), invert selection (Ctrl+I) |
| Platforms | Windows, macOS, Linux | macOS and Linux through SDL2, the web as a client of the native daemon, pop-out views as windows or tabs, the Windows client links under mingw (not yet run on Windows; a lane there needs a symlink-free mirror, so `lane=main`), the window box persists |
| Performance | 60 fps viewports, async asset loading | `TORIRS_EDITOR_FRAME_STATS=1` percentiles per phase; idle frame 2.4 ms p50 (was 8.4: the catalog's warm build moved to a worker thread), the world drawn 0.12 s after start with the picker open (was 8 s), Full map peak 850 MB (was 1355), the lane change scan on a worker thread; the frame is now the software rasteriser |

## 2. The first batch (landed 2026-11-03)

1. Ergonomics: command palette (Ctrl+P), Go to tile, Recent squares, a
   Unity-style camera scheme (right-drag look, middle-drag pan, Alt-orbit,
   wheel to cursor, Frame selection, double-click recentre), select-all-of-
   kind, invert selection. Hide selected was skipped: the scenery has no
   per-loc visibility flag, so a hide would be a remove-and-restore.
2. Screens and portability: compact dock mode (collapse to title bars,
   chosen over tabs because the chrome's tab strip is per panel and the
   modules' widgets would have to be re-parented), every layout size from
   960x600 up tested with the world kept at 60 % of the surface, a status
   bar, the Windows client built under mingw (not run), the web editor
   photographed at 1024x600 and 1366x768, the window box persisted.
3. Performance: `TORIRS_EDITOR_FRAME_STATS=1` percentiles per phase,
   measured scenarios (idle, pan, catalog, picker, brush, pop-out, startup,
   RSS), `sample` profiles, fixes with before/after numbers (the table is in
   docs/MAPED_STATUS.md "Performance"). What was measured and left alone:
   the chrome build (already retained), the hover pick (noise), the brush
   remesh (cheap), the Preview raster (already on change only).

## 3. Next, in order

1. Docking like Unity: drag a panel's tab onto another panel to group them,
   split the column, save named layouts (Terrain, Scripting, Playtest) and
   switch with a hotkey. The chrome has tabs and docked extents; the layout
   engine knows one column.
2. A cross-square outliner: every loc / npc / obj spawn in the loaded squares
   as a filterable tree, click to select and frame, with "find usages" of a
   catalog record (which squares place this loc).
3. Transform gizmos in the viewport: a move handle for locs and spawns, a
   rotation ring, snapping to the grid and to neighbours; today the keys do
   it (arrows, Q, Shift+Q).
4. Multi-square editing: a brush that crosses square boundaries, selections
   spanning squares, copy / paste of rectangles between squares (Rect
   copies within one square today).
5. Autosave and crash recovery: a periodic snapshot of dirty documents into
   the lane, offered on the next boot; the lane makes this cheap.
6. Publish per file and a diff view: pick which changes go, show a text
   diff of a config file and a before / after picture of a square.
7. Prefabs: a catalog of loc groups (a house, a fence run) placed as one,
   stored as a document in the lane like palettes.
8. Script editing in the editor for small things: inline edit of a trigger's
   script with syntax colouring and the compiler's errors, Run from the
   editor (the Triggers panel checks and runs today).
9. Themes and accessibility: font scale separate from the chrome scale, a
   light theme, colour-blind safe selection colours, every control reachable
   by keyboard.
10. Collaboration: two editors on one daemon already see each other's edits
    live; show who is editing which square and lock a square while a drag
    is in flight.

## 4. What stays out

- Editing the baked cache: the editor edits the content tree's text sources
  and bakes; the cache is an output.
- A script editor that replaces yours: the Triggers panel opens your editor
  (`TORIRS_EDITOR_OPEN_CMD`).
- The game client's own UI (interfaces) is edited by the interface editor,
  a separate owner; this plan is the world editor's.
