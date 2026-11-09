# ToriRSMapEd hotkeys

The editor is driven from the keyboard as completely as from the mouse: every
menu row, tool, brush control, level button and panel has an action, every
action may carry a chord, the default chords are below, and the user may rebind
any of them from the Hotkeys panel (F1). Chords are shown beside the menu rows,
as keycaps on the panel buttons, as badges on the tool rail, after the tool
tips, in the status bar's hint strip, and in a panel group's tab titles
("Catalog (F8)", plain text), and they follow a rebind.

This file is the reference for the user. `docs/MAPED_STATUS.md` records how
it was built and what is open.

## Rules

- **One dispatcher.** All editor key handling goes through one table
  (`src/editor/editor_hotkeys.{h,c}`: actions, default chords, chord parse and
  format, the `editor_hotkeys.ini` codec, resolution of one frame's key edges
  against the table) and one App glue unit (`src/app/app_editor_hotkeys.c`)
  that runs the gates and maps each action onto the function the menu row or
  button already calls. No other unit reads a bare editor key.
- **Gates.** A chord fires only when the editor panel is open, no text field
  has focus, no model view has focus, no menu drop or minimenu is open. A chord
  that fires consumes the frame (`input_frame_consumed`) and marks its key in
  `hotkey_consumed`, so the manifest's debug hotkeys, the plugins and the
  world's own keys do not see it. A chord the table CLAIMS (bound, in a live
  context) is withheld from the manifest's `[debug:hotkeys]` for that frame
  whether or not it acted, so Ctrl+S saves and never also flies the camera.
- **Edge, not held**, except the camera (held) and the repeaters (brush size,
  nudge: first press, then repeat after 300 ms at 20 Hz while held).
- **The arrows and W/A/S/D are the camera's** over the World, under every
  tool and selection: the bare arrows turn and tilt, W/A/S/D fly. Anything
  else on those keys takes Ctrl (Cmd on a Mac): the loc nudges are
  Ctrl+arrows (Ctrl+Shift+arrows for 8 tiles), the debug loc editor's nudges
  Ctrl+W/A/S/D. A focused field, model view, picker or command palette still
  takes the keys it types or moves with.
- **Context.** An action lives in one context; a chord may be reused across
  contexts that cannot both be live (a loc selected vs none). Within a context
  two actions never share a chord: the panel shows the clash in red and the
  later binding wins until one is changed. The one exception is a
  **qualifier**: a held action whose chord may be a modifier alone (Fly
  faster, Snap while dragging, Constrain a drag). It fires nothing itself; it
  changes how its own gesture goes, so two qualifiers on one bare modifier
  are not a clash -- Shift both speeds the flight and keeps a wall run
  straight.
- **Modifiers** are Ctrl, Shift, Alt. On Apple platforms Command is reported as
  Ctrl too (the platform does it), and the hints read "Cmd".
- **Persistence.** Rebinds are device-local like the dock layout:
  `editor_hotkeys.ini` beside `editor_layout.ini`, `TORIRS_EDITOR_HOTKEYS`
  with the same rule (unset = that file, IndexedDB on the web; empty = no file
  read or written; else that path), read at boot through the App's task queue,
  written when a binding changes. Only non-default bindings are written, as
  `<action>=<chord>` (`<action>=` for unbound). Key names are
  `LibToriRS_KeyCodeName`'s: `ctrl+shift+z`, `lbracket`, `f1`, `pageup`.
- **Headless.** `TORIRS_SIM_HOTKEY="<frame>,<chord>[,hold=<frames>][;...]"`
  presses a chord at a main-loop frame (modifiers held through the press,
  released two frames later or after `hold`), writing both the platform key
  arrays and the OSRS arrays, so a photograph can prove any binding.

## Default chords

Context: **G** always (editor open) · **W** pointer over the World panel ·
**L** one or more locs selected · **R** the Rect tool holds a rectangle ·
**S** the Spawns panel holds a selection and no loc is selected ·
**M** locs selected and the Select or Move tool armed (the nudges) ·
**N** no loc selected, or a tool other than Select / Move armed ·
**P** the Place tool armed with a loc pick and no loc selected ·
**K** the picker has focus (the Squares well holds the keyboard: a press in
it, or a Find that matched a square, gives it; Escape in the well, or a click
elsewhere, takes it away). K excludes every other context, G included: while the well holds the
keyboard it is a field, nothing else resolves, and its keys are the K rows
alone (the Squares panel reads them from the table, so a rebind relabels and
re-routes them) ·
**Q** the NPC builder's dialogue graph holds the keyboard focus (a press in
the well gives it; any other GRAPH well, a palette's, a chip strip's, the
Locs strip's or the picker's, is not Q). Like K it is a chrome focus, so the
text-field gate holds every other row back: the App resolves the frame's keys
against the Q rows alone (`Builder_GraphKeys`). Workbench 6.8: Ctrl+L lays it
out there; Ctrl+L is free everywhere else. ·
**D** the default camera scheme · **U** the Unity camera scheme (exactly one
of the two is live; the Camera table below says which keys each has).

### File
| Action | Chord | Ctx |
|---|---|---|
| New square... | Ctrl+N | G |
| Save all | Ctrl+S | G |
| Save square as new... | Ctrl+Shift+S | G |
| Bake cache (slow) | unbound | G |
| Close editor | unbound | G |
| Check the scripts on disk (sscompile) | Ctrl+Shift+B | G |
| Run: save all, build, reload live | Ctrl+Return | G |
| Publish the lane's changes to a lane | Ctrl+Shift+U | G |
| Save the session's state as a scene (the open squares, the World's camera, the pinned level and Above, the lane, the var preview, the live account; the Scenes panel's Holds chips choose; one undo step, saved at once) | Ctrl+Shift+K | G |
| Make a scratch lane from main; it opens at the next launch (`scratch-<yymmdd><letter>`, a clean mirror of main as it is now; the switch waits for the next launch: a running server cannot re-point its lane, so quit and launch the editor again) | Ctrl+Shift+N | G |
| Open up to 3x3 squares, swept on the Square map (a drag over the map picks them; Open opens them together as the working set, in place of what is open) | Ctrl+O | G |

File also lists **Recent squares**: the last ten squares opened in this
window (newest first, a lane square as `<lane>/m50_50`), remembered per user
in `editor_recent.ini` beside `editor_layout.ini` (`TORIRS_EDITOR_RECENT`,
the same path rule; never in the content tree). A row opens that square as
the picker's Open does (the save guard first). The command palette lists the
same squares when its box is empty.

### Edit
| Action | Chord | Ctx |
|---|---|---|
| Undo | Ctrl+Z | G |
| Redo | Ctrl+Shift+Z, Ctrl+Y | G |
| Deselect / cancel (open menu, drag or rectangle in progress, then EVERY selection: the locs, the rectangle, the Spawns selection, the active player's mark and the latched tile; with the Place tool armed and nothing selected, drops the ghost: back to Select, the pick kept) | Escape | G |
| Delete selection (locs; else the Spawns selection) | Delete, Backspace | L, S |
| Duplicate (locs; else the Spawns selection) | Ctrl+D | L, S |
| Rotate (selected locs; else the Place ghost -- the ghost whenever the Place tool is armed) | Q | G |
| Reshape (selected loc; else the Place ghost -- within the loc's own shapes only) | Shift+Q | G |
| Apply the armed tool to the selection | Return | L |
| Select every loc of the hovered tile's square on the edit level | Ctrl+A | W |
| Select every loc of the hovered loc's id (select same; with no loc under the pointer, the primary selected loc's) in the open squares on the edit level | Alt+A | W, L |
| Nudge selection N / S / W / E one tile (onto a tile holding a loc of that shape: the two swap) | Ctrl+Up / Ctrl+Down / Ctrl+Left / Ctrl+Right | M |
| Nudge selection 8 tiles N / S / W / E (a zone; the same swap rule) | Ctrl+Shift+Up / Ctrl+Shift+Down / Ctrl+Shift+Left / Ctrl+Shift+Right | M |
| Move the selected locs a level up / down (each keeps its tile; refused, with the reason in the status bar, where a loc would leave levels 0..3, its square is not open, or the key above / below holds another loc) | Ctrl+PageUp / Ctrl+PageDown | L |
| Copy tiles (and the rectangle's locs on its level: the unnamed stamp) | Ctrl+C | R |
| Paste tiles at the hovered tile (then the copied locs, in the same undo step) | Ctrl+V | W |
| Fill tiles | Ctrl+Shift+F | R |
| Clear tiles | Shift+Delete | R |
| Mark area from rect | Ctrl+Shift+M | R |
| Select every loc of the catalog's picked loc in the open squares (else of the selection's first loc; on the pinned level, else the selection's) | Ctrl+Shift+L | G |
| Invert selection (over the open squares' locs on the selection's level) | Ctrl+I | G |

### Place
| Action | Chord | Ctx |
|---|---|---|
| Place the catalog's loc at the hovered tile (the ghost's pose) | Return | P |
| Fit walls to neighbours on/off (Place tool) | G | G |
| Swap the selected locs to the catalog's pick | E | L |
| Keep a Place drag (a wall run) to one axis (hold) | Shift | W |

The ghost's turn and shape are Edit's Rotate (Q) and Reshape (Shift+Q); Escape
drops the ghost (Deselect / cancel). Fit is shared by every window of the
Client (it rides STATE_TOOLS with the pose).

The pointer (workbench 6.3, design section 7):

- **Place: drag along tiles** is a run. With a wall pick, one piece per tile
  along the path (a diagonal step is two orthogonal steps; going back over
  the path takes the tiles after it off), on the side of the path the ghost's
  pose stands on; the two ends are straight walls and each turn a corner at
  the rotation Fit computes: an L (shape 2) outside the turn, a corner post
  (shape 3) inside it. With any other pick, one at the ghost's pose on every
  tile crossed. The World shows every piece before release; a tile the
  release would not fill (its square closed, another loc on its key) is red
  and the status bar says why once a run. The release places the run as one
  undo step and selects what it placed. Hold Shift to keep it to one axis;
  Escape drops it.
- **Select: drag on empty ground** is a marquee: the Rect tool's rectangle
  (so copy, fill and clear act on it), selecting every loc on the edit level
  whose footprint it touches. Shift adds, Ctrl toggles, Alt subtracts -- for
  a marquee and for a click on a loc alike (a plain click latches the tile).
  A **double-click** on a loc is select same (Alt+A).

- **Select: Ctrl+drag (Cmd+drag on a Mac) on a thing moves it.** A press on
  a loc, a previewed spawn or the play account's preview, dragged off its
  tile: the loc (or the loc selection it is in) moves as the gizmo's centre
  drag moves it, previewed and applied in one step on release; the spawn
  moves the Spawns selection as "Move: click a tile" does ("move spawn", one
  undo step); the player is placed as the Place player tool places it. An
  unselected loc or spawn becomes the selection first. While the drag is
  off its tile the thing is previewed where it would land, as Place previews
  a pick: the loc's body (the first selected one; every loc's footprint), the
  spawn's npc or ground item, the account's body, translucent, under the
  pointer. Let go on the same tile and it is the Ctrl+click it was (the
  selection toggles).

- **Any panel: Shift+Alt+drag (Shift+Option on a Mac) moves it.** With both
  held, a press anywhere on a panel -- a row, a button, its grip, its
  window's body -- picks the panel up as its title bar does, so a panel
  whose title bar is off the top of the window or under another panel can
  still be brought back. A group (a tab strip with its window) moves as one;
  the World view's body is the world and falls through as always. The chrome
  reads the modifiers the app hands it each frame
  (ToriRSChrome_SetModifiers, src/app/app_loc_editor.c).

### Tools
| Action | Chord | Ctx |
|---|---|---|
| Select | 1 | G |
| Rectangle | 2 | G |
| Height | 3 | G |
| Flatten | 5 | G |
| Smooth | 6 | G |
| Paint underlay | 7 | G |
| Paint overlay | 8 | G |
| Paint flags | 9 | G |
| Place loc | 0 | G |
| Move loc | M | G |
| Rotate loc (tool) | T | G |
| Delete loc (tool) | X | G |
| Place player (the lane's play account onto the clicked tile) | P | G |
| Paint path (the palette's overlay laid along a drag) | H | G |
| Roughen (seeded height noise under the brush, the step its amplitude) | U | G |
| Scatter locs (the selected locs' ids, else the catalog's pick, dropped on the brush's free tiles at a density; Alt-drag takes them off) | B | G |
| Plateau (the brush flattened to the first tile's height, a smoothed skirt around it: a mound or a hole with a flat top) | V | G |
| Region brush (the palette's overlay under a swept disc, its edge cut to the tile shapes; Alt-drag takes it off) | O | G |
| Lasso (a drawn loop, or clicked corners, filled with the palette's overlay, its edge cut to the tile shapes) | J | G |
| Tidy (click an overlay: its run's stepped edge smoothed and refitted to the tile shapes) | Y | G |
| Previous tool (swap with the last armed) | Grave (`) | G |
| Pick the hovered tile's paint into the palette (underlay, overlay, shape, flags) | I | W |
| Pick the hovered loc into the catalog | Shift+I | W |
| Place the catalog's loc on click (hold) | L | W |
| Delete the loc under the click (hold) | K | W |
| Clear every loc on the clicked tile (hold) | C | W |

### Brush (height and paint tools)
| Action | Chord | Ctx |
|---|---|---|
| Radius - / + (Path: width by half a tile; Region brush: radius by half a tile) | [ / ] | G |
| Strength - / + (Scatter: density by 5; Plateau: skirt by 1; Paint underlay: mix by 10; Region, Lasso, Tidy: hardness by 10) | Shift+[ / Shift+] | G |
| Step - / + | Ctrl+[ / Ctrl+] | G |

### Level
| Action | Chord | Ctx |
|---|---|---|
| Level down / up | PageDown / PageUp | G |
| Pin level 0..3 (again: Auto) | Alt+0 .. Alt+3 (Pin level 0 also Ctrl+0, for one release) | G |
| Level Auto | Alt+4 | G |
| Show above on/off | Alt+5 (also Ctrl+5, for one release) | G |

Workbench 6.8 moved the level pins from Ctrl+0..5 to Alt+0..5 (design
section 15, "Resolved collisions"): Ctrl+1..4 are the workspaces now. The two
old chords nothing took, Ctrl+0 and Ctrl+5, stay as the second chord of Pin
level 0 and Show above for one release, and the console says so each time
they fire ("editor: Ctrl+0 is the old chord of Pin level 0: Alt+0 from now
on; the old one goes next release"). Ctrl+1, Ctrl+2, Ctrl+3 and Ctrl+4 could
not stay as aliases: they would clash with the workspaces in G.

### Catalog palette
| Action | Chord | Ctx |
|---|---|---|
| Palette slot 1 | Shift+1 | G |
| Palette slot 2 | Shift+2 | G |
| Palette slot 3 | Shift+3 | G |
| Palette slot 4 | Shift+4 | G |
| Palette slot 5 | Shift+5 | G |
| Palette slot 6 | Shift+6 | G |
| Palette slot 7 | Shift+7 | G |
| Palette slot 8 | Shift+8 | G |
| Palette slot 9 | Shift+9 | G |
| Palette slot 10 | Shift+0 | G |
| Add the pick to the palette | Ctrl+Shift+A | G |
| Next palette | Alt+] | G |
| Previous palette | Alt+[ | G |

A slot makes its record the catalog pick and arms placement with the slot's
remembered shape and rotation (docs/MAPED_STATUS.md row 3d).

### Camera
| Action | Chord | Ctx |
|---|---|---|
| Fly forward / back / left / right (held) | W / S / A / D | G |
| Fly down / up (held) | F / R | D (down) / G (up) |
| Fly faster (held with the above) | Shift | G |
| Turn left / right, tilt up / down (held) | Left / Right / Up / Down | G |
| Frame the selection, else the hovered square | Home | G |
| Frame the current square's centre | Ctrl+Home | G |
| Frame the selection, else the hovered square (Unity scheme) | F | U |
| Camera scheme: default / Unity (toggle) | unbound | G |
| Pan: drag the ground with the left button (held) | Space (also Alt+Ctrl+drag, Option+Cmd on a Mac, under the Unity scheme) | W |

The manifest's `[debug:hotkeys]` flight (Ctrl+W/A/S/D/R/F) keeps working
where the editor claims no chord; Ctrl+S, Ctrl+D and Ctrl+A are claimed.

**Camera schemes.** The default scheme is the one above: WASD / RF fly, the
arrows turn, the middle button orbits, the wheel dollies along the view. The
**Unity** scheme (the Camera scheme toggle -- the Hotkeys panel's row, or the
command palette's "Camera scheme" row -- remembered in `editor_recent.ini`) makes
the mouse work as in Unity's scene view: **right-drag looks** around (yaw /
pitch; a right CLICK still opens the minimenu, a drag past a few px closes
it), **middle-drag pans** the ground plane (faster the higher the camera),
the **wheel zooms toward the tile under the cursor** (never closer than two
tiles), **Alt+left-drag orbits** the selection (else the latched tile, else
the tile under the pointer), **Alt+Ctrl+left-drag pans** like the middle
button (Option+Cmd on a Mac: Unity's one-button pan, checked before the
orbit), and **F frames** the selection (Home frames in both schemes; Fly down
is R's twin only under the default scheme). The keys otherwise stay the
table's.

**Pan (Space).** Under either scheme, hold Space with the pointer over the
World and left-drag: the ground follows the pointer, at the middle-drag pan's
rate (faster the higher the camera). The pan cursor shows from the moment the
key (or, under Unity, Alt+Ctrl) is held over the World, before any press, and
stays for the whole drag; a middle-drag pan shows it too. It is the system's
"move" cursor: four arrows on Windows, Linux and the web, the closed hand on
macOS. The drag keeps the pointer until the button comes up, wherever it
goes; letting go of Space mid-drag does not drop it. While Space is held a
left press never reaches the armed tool. Space was Place here's second chord
until 2026-11-07; a Space tap with the Place tool armed and no drag says so in
the status bar, and Return or a click still places. **Double-click a tile with no loc** in the World
panel (Select tool) recentres the camera on it at the distance it has, under
either scheme; a double-click on a loc is Select same (every loc of its id on
the edit level).
Every move respects the camera clamp: the eye never leaves the loaded squares.

Gestures (trackpad and touch, workbench 6.1; the table's gesture spelling,
`Editor_HotkeyActionInfo::gesture`): the fly moves are **2 fingers** (the
look-at point slides along the ground, yaw and pitch kept), fly down / up is
**pinch** (the eye distance, about the ground under the pinch), turn left /
right is **twist** (about the look-at point). With the editor open a
one-finger drag is the armed tool's (the marquee, a brush stroke), never the
camera's. The Squares picker pans under two fingers and zooms about a pinch;
the Preview orbits under a drag and zooms on a pinch. While the last input
was a finger the status bar's hint strip says the gestures instead of the
mouse words: "[tap] tile", "[2 fingers] pan", "[pinch] zoom", "[hold] menu".

### Picker
| Action | Chord | Ctx |
|---|---|---|
| Move the focused square N / S / W / E (an empty cell too; the view follows) | Up / Down / Left / Right | K |
| Open the focused square (an empty one: New square) | Return | K |
| Add the focused square to the open set | Shift+Return | K |
| Go to the previous square of the open set (its chip, the camera to its centre) | Backspace | K |
| Clear Find (and its pin); the well then lets go of the keyboard | Escape | K |

The focused square has the white outline and the detail pane under the atlas
follows it. `+` and `-` zoom about the well's middle as before (the well's
own keys, not the table's).

### View
| Action | Chord | Ctx |
|---|---|---|
| Hotkeys panel | F1 | G |
| Terrain | F2 | G |
| Palette | F3 | G |
| Squares | F4 | G |
| Locs | F5 | G |
| Spawns | F6 | G |
| Show every spawn of the open squares in the world, on/off | Shift+F6 | G |
| Inspector | F7 | G |
| Catalog | F8 | G |
| Preview (the Catalog pick's model) | Ctrl+Shift+P | G |
| Gallery (the Catalog's search as pictures) | Shift+F8 | G |
| Catalog palette (slots of locs, npcs and objs) | unbound | G |
| Console | F9 | G |
| History | F10 | G |
| Areas | F11 | G |
| World | F12 | G |
| Triggers | Ctrl+E | G |
| Hide / show every panel but World | Tab | G |
| Reset layout | Ctrl+Shift+R | G |
| Snap to grid (toggle) | Ctrl+G | G |
| Compact panels: dock members collapse to their title bars when the column is short (toggle; automatic on small windows, Reset layout returns to automatic) | Ctrl+Shift+C | G |
| Snap while dragging a panel (hold; the toggle is untouched) | Ctrl | G |
| Pop out the panel under the pointer into its own window (World when over none) | Shift+F12 | G |
| Hide the panel under the pointer (the View menu brings it back) | Ctrl+W | G |
| Chrome scale: Auto (the display's device pixel ratio) | unbound | G |
| Chrome scale: 100 % | unbound | G |
| Chrome scale: 125 % | unbound | G |
| Chrome scale: 150 % | unbound | G |
| Chrome scale: 175 % | unbound | G |
| Chrome scale: 200 % | unbound | G |
| Chrome scale: 250 % | unbound | G |
| Chrome scale: 300 % | unbound | G |
| Text size: Normal (the Body face, 18 px rows) | unbound | G |
| Text size: Large (the Menu face, 22 px rows) | unbound | G |
| Text rendering: the pixel font at 100, 200 and 300 %, smooth between | unbound | G |
| Text rendering: the smooth vector face at every scale | unbound | G |
| Workspace: Build (placing locs and spawns; the default) | Ctrl+1 | G |
| Workspace: Terrain (heights, colours, shapes, flags) | Ctrl+2 | G |
| Workspace: Script (records, triggers, builders) | Ctrl+3 | G |
| Workspace: Playtest (RUN, live, accounts) | Ctrl+4 | G |
| Command palette (actions, panels, squares, records, accounts) | Ctrl+K, Alt+Space (Option+Space on a Mac) | G |
| Go to tile... (the palette prefilled with coordinates) | Ctrl+Shift+G | G |
| World layer Locs on/off (reserved) | Alt+L | G |
| World layer NPCs on/off (reserved) | Alt+N | G |
| World layer Objs on/off (reserved) | Alt+O | G |
| World layer Collision on/off (reserved) | Alt+C | G |
| World layer Grid on/off (reserved) | Alt+G | G |

The five layer toggles are the World HUD's (design section 4), which this
tree does not have yet: the chords are claimed so nothing else takes them,
and pressing one says so in the status bar.

**Workspaces** (design section 3): four named layouts, switched with Ctrl+1 to
Ctrl+4, the chips at the right end of the menu bar, or the command palette.
A workspace is the panels it shows, where they sit and which tab of each
group is in front. Build is the editor's default set; Terrain shows Terrain
and Palette (Terrain in front), Squares and the Inspector; Script shows
Record, Triggers, the Loc and NPC builders, the Shell editor, the Shop
builder and the Quest board, with Problems in front in the bottom strip;
Playtest shows Live, Accounts, Scenes and the encounter builder; the Console
strip stays in all four. A panel the design names that this build has not
got (Vars, Dialogue preview, Live log) is skipped with a console line. Switching saves
the workspace you leave first, so coming back finds it as you left it; the
layout file keeps each one in its own `[workspace <name>]` section. View >
Reset layout resets the workspace you are in.

**The command palette** (Ctrl+K or Alt+Space -- Option+Space on a Mac; module `cmdpalette`): one search over the
actions (each with its chord in the right column), the panels, the squares
the picker knows, the catalog's records (`loc:`, `npc:`, `obj:` narrow to one
kind), the scenes (Enter enters one, through the save guard), the lanes the
Lanes panel knows (Enter stages a switch for the next boot) and the lane's
accounts. The arrows move, Tab goes to the next group,
Enter runs the row, Escape closes it. A group shows at most 100 rows and says
how many more there are ("and 212 more"). Ahead of them all the **Recent**
group lists the squares opened most recently (every one with the box empty;
File > Recent squares lists them too, from `editor_recent.ini`). An absolute
tile ("3222,3218", "3222 3218 1") is a Squares row that brings the camera
there, opening its square first when the scene does not hold it, the camera
landing on the tile; two numbers under 256 are a square for the picker's
Find. **Go to tile...** (Ctrl+Shift+G) opens the palette prefilled with the
hovered tile (else the latched tile, else the first selected loc). An action
only the App can perform (the camera, the eyedroppers, Paste at the hovered
tile) runs on the next frame, as its chord would.

### Graph
| Action | Chord | Ctx |
|---|---|---|
| Lay the focused dialogue graph out | Ctrl+L | Q |
| New dialogue node after the selected one | N | Q |

Workbench 6.6: Ctrl+L lays the NPC builder's dialogue graph out left to right
by depth (the positions are saved as `pos=` in the `.interaction` file); N
adds a node of the Node kind dropdown's kind after the selected one. With the
graph's well holding the keyboard, Delete (`edit.delete`'s chord, so a rebind
moves it too) deletes the selected node, and the status bar names the nodes
it orphaned. Escape closes the new-node chip row a port dropped on empty
space opens, and mid-drag puts the dragged nodes back. Away from the graph,
Delete is the World's again; N and Ctrl+L from the command palette or the
menu still act on the open dialogue graph.

Every other panel the View menu lists (Record, New, builders, Live, Accounts,
Scenes, Encounters, the Shop builder, the Quest board, Problems, ...) is an action too, unbound by default. The Chrome scale and Text size rows
(workbench 6.0b) are radio rows between the panel rows and Hide / show panels:
the chosen one says "on" in its right-hand column, Auto names the scale the
display gives ("Chrome scale: Auto (150 %)"), and both are kept per device in
`editor_prefs.ini` beside `editor_layout.ini` (`TORIRS_EDITOR_PREFS`), never
shared with the other windows of the Client; a window popped out reads the
same file when it opens. On the web, F11 and F12 may be taken by the
browser; rebind them there.

## The Hotkeys panel (F1)

A list of every action: its name, its group and context (dimmed), chord 1 and
chord 2 as two keycap cells ("-" for an empty slot), and the gesture (the
trackpad or touch gesture that does the same, from the table's gesture
spelling). The summary above it reads "161 actions, 2 rebound, 0 conflicts
(161 shown)". A search box filters by any column; under it, a **Group** and a
**Context** dropdown, and the **changed** (only what differs from the
defaults) and **conflicts** (only clashing rows) chips, a lit chip's x
turning it off. **Sort** chips order the list by group (the tables' order),
action, context, chord 1, chord 2 or gesture (a second press reverses it; an
empty cell sorts last either way). Clicking a chord cell captures the next
chord into THAT slot alone (the cell says "press..."), keeping the other;
Backspace unbinds that slot; Esc cancels. A clash with an action in a context
that can be live at once turns both rows red. Hovering a row names the action
and its id, or what it clashes with. **Reset all** restores the table above.
Rebinds take effect at once: the menu rows, the keycaps and badges, the tool
tips and the status bar's hints re-label on the next frame.

## Action ids (`editor_hotkeys.ini`)

The file holds only what differs from the table above, one action per line
under `[hotkeys]`: `<action> = <chord>[, <chord>]`, or `<action> =` for
unbound. A line naming an unknown action, an unparsable chord or more than two
chords is skipped and named in the console (`editor: editor_hotkeys.ini: line
N: ...`); the rest of the file still applies. The ids, in the order of the
tables (the Hotkeys panel calls them by the name after the id):

- **File**: `file.new` New square..., `file.save_all` Save all, `file.save_as` Save square as new..., `file.bake` Bake cache (slow), `file.close` Close editor, `file.check_scripts` Check scripts, `file.run` Run (save, build, reload), `file.publish` Publish..., `file.save_scene` Save scene, `file.scratch_lane` Scratch lane, `file.open_squares` Open squares...
- **Edit**: `edit.undo` Undo, `edit.redo` Redo, `edit.deselect` Deselect / cancel, `edit.delete` Delete selection, `edit.duplicate` Duplicate, `edit.rotate` Rotate, `edit.reshape` Reshape, `edit.apply` Apply tool to selection, `edit.select_square` Select the square's locs, `edit.select_same` Select same, `edit.nudge_n` Nudge north, `edit.nudge_s` Nudge south, `edit.nudge_w` Nudge west, `edit.nudge_e` Nudge east, `edit.nudge_far_n` Nudge north 8, `edit.nudge_far_s` Nudge south 8, `edit.nudge_far_w` Nudge west 8, `edit.nudge_far_e` Nudge east 8, `edit.level_up` Selection up a level, `edit.level_down` Selection down a level, `edit.copy` Copy tiles, `edit.paste` Paste tiles, `edit.fill` Fill tiles, `edit.clear` Clear tiles, `edit.mark_area` Mark area from rect, `edit.select_kind` Select all of the picked kind, `edit.invert` Invert selection
- **Place**: `place.here` Place at the hovered tile, `place.fit` Fit walls to neighbours (toggle), `place.swap` Swap to catalog pick, `place.constrain` Constrain a drag to one axis (hold)
- **Palette**: `palette.slot_1` Palette slot 1, `palette.slot_2` Palette slot 2, `palette.slot_3` Palette slot 3, `palette.slot_4` Palette slot 4, `palette.slot_5` Palette slot 5, `palette.slot_6` Palette slot 6, `palette.slot_7` Palette slot 7, `palette.slot_8` Palette slot 8, `palette.slot_9` Palette slot 9, `palette.slot_10` Palette slot 10, `palette.add_pick` Add the pick to the palette, `palette.next` Next palette, `palette.previous` Previous palette
- **Tools**: `tool.select` Select, `tool.rect` Rectangle, `tool.height` Height, `tool.flatten` Flatten, `tool.smooth` Smooth, `tool.paint_underlay` Paint underlay, `tool.paint_overlay` Paint overlay, `tool.paint_flags` Paint flags, `tool.place` Place loc, `tool.move` Move loc, `tool.rotate` Rotate loc (tool), `tool.delete` Delete loc (tool), `tool.place_player` Place player, `tool.paint_path` Paint path, `tool.noise` Roughen, `tool.scatter` Scatter locs, `tool.plateau` Plateau, `tool.region` Region brush, `tool.lasso` Lasso, `tool.tidy` Tidy, `tool.previous` Previous tool, `tool.pick_paint` Pick the tile's paint, `tool.pick_loc` Pick the hovered loc, `hold.place` Click places a loc (hold), `hold.delete` Click deletes a loc (hold), `hold.clear` Click clears the tile (hold)
- **Brush**: `brush.radius_down` Radius -, `brush.radius_up` Radius +, `brush.strength_down` Strength -, `brush.strength_up` Strength +, `brush.step_down` Step -, `brush.step_up` Step +
- **Level**: `level.down` Level down, `level.up` Level up, `level.pin0` Pin level 0, `level.pin1` Pin level 1, `level.pin2` Pin level 2, `level.pin3` Pin level 3, `level.auto` Level Auto, `level.above` Show above
- **Camera**: `camera.forward` Fly forward, `camera.back` Fly back, `camera.left` Fly left, `camera.right` Fly right, `camera.down` Fly down, `camera.up` Fly up, `camera.fast` Fly faster, `camera.turn_left` Turn left, `camera.turn_right` Turn right, `camera.tilt_up` Tilt up, `camera.tilt_down` Tilt down, `camera.frame` Frame the selection, `camera.frame_square` Frame the square's centre, `camera.frame_key` Frame the selection (Unity scheme), `camera.scheme` Camera scheme: default / Unity (toggle), `camera.pan` Pan (hold, then drag)
- **Picker**: `picker.north` Focus north, `picker.south` Focus south, `picker.west` Focus west, `picker.east` Focus east, `picker.open` Open the focused square, `picker.add` Add the focused square, `picker.back` Previous open square, `picker.clear_find` Clear Find
- **Graph**: `graph.auto_layout` Auto-layout the graph, `graph.new_node` New node
- **View**: `view.hotkeys` Hotkeys panel, `view.terrain` Terrain, `view.palette` Palette, `view.squares` Squares, `view.locs` Locs, `view.spawns` Spawns, `view.spawn_preview` Show spawns in the world (toggle), `view.inspector` Inspector, `view.catalog` Catalog, `view.preview` Preview, `view.gallery` Gallery, `view.catalog_palette` Catalog palette, `view.console` Console (Output), `view.history` History, `view.areas` Areas, `view.world` World, `view.toolrail` Tools (rail), `view.rect` Rectangle panel, `view.record` Record, `view.locbuilder` Loc builder, `view.npcbuilder` NPC builder, `view.shelleditor` Shell editor, `view.iface` Interface editor, `view.cs2` CS2, `view.live` Live, `view.accounts` Accounts, `view.triggers` Triggers, `view.scenes` Scenes, `view.encounter` Encounters, `view.shop` Shop builder, `view.quest` Quest board, `view.problems` Problems, `view.hide_panels` Hide / show panels, `view.reset_layout` Reset layout, `view.snap` Snap to grid, `view.compact` Compact panels, `view.snap_hold` Snap while dragging (hold), `view.popout` Pop out panel, `view.close_panel` Close panel, `view.scale_auto` Chrome scale Auto, `view.scale_100` Chrome scale 100 %, `view.scale_125` Chrome scale 125 %, `view.scale_150` Chrome scale 150 %, `view.scale_175` Chrome scale 175 %, `view.scale_200` Chrome scale 200 %, `view.scale_250` Chrome scale 250 %, `view.scale_300` Chrome scale 300 %, `view.text_normal` Text size Normal, `view.text_large` Text size Large, `view.text_bitmap` Text rendering bitmap, `view.text_smooth` Text rendering smooth, `view.workspace_build` Workspace Build, `view.workspace_terrain` Workspace Terrain, `view.workspace_script` Workspace Script, `view.workspace_playtest` Workspace Playtest, `view.command_palette` Command palette, `view.goto_tile` Go to tile..., `view.layer_locs` Layer Locs (reserved), `view.layer_npcs` Layer NPCs (reserved), `view.layer_objs` Layer Objs (reserved), `view.layer_collision` Layer Collision (reserved), `view.layer_grid` Layer Grid (reserved)

Example:

    [hotkeys]
    version = 1
    file.bake = ctrl+b
    edit.redo = ctrl+y, ctrl+shift+z
    tool.height =

## Where the build differs from the plan above

- **Capture binds the clicked slot** (workbench 6.8; before, a capture
  replaced both chords). Unbinding the first slot of an action that has two
  moves the second up: the table never holds a second chord without a first.
- **Sorting is by chips**, not by clicking a column header: the list's header
  row is drawn, not pressed.
- **Claims follow the context.** A chord is withheld from `[debug:hotkeys]`
  only while it is bound in a LIVE context, as the rules say; so Ctrl+A is
  claimed over the World panel only and Ctrl+D only while a loc or spawn
  selection exists. Ctrl+S (G) is always claimed. Elsewhere the debug flight
  keeps Ctrl+A / Ctrl+D.
- **Held actions take Fly faster on top.** The camera and the click
  accelerators (L / K / C) stay active with the Fly faster key (Shift) held as
  well, so Shift+W flies at four times the speed; any other extra modifier
  stops them (Ctrl+W is the debug flight's). They do not consume the frame: a
  click still paints while the camera flies.
- **Escape.** A menu drop or minimenu open closes the gates (they own the
  keyboard), so Escape there is theirs. Otherwise it ends a brush stroke in
  progress (the stroke stays as the one undo step it already is) or drops a
  rectangle drag, and then clears the loc selection and the rectangle.
- **Menu Paste vs the chord.** Edit > Paste tiles pastes at the rectangle (the
  menu is clicked away from the world); Ctrl+V pastes at the hovered tile.
- **Extra menu rows.** Edit gains "Mark area from rect", View gains
  "Hide / show panels"; both show their chords.
- **Panel buttons wear keycaps** (workbench 6.0). A button whose action has
  a chord draws its first chord as a keycap after the caption, and its
  tooltip ends with every chord when there are two: Locs (nudges, Rotate,
  Reshape, Duplicate, Delete, Swap), Spawns (Duplicate, Delete / suppress),
  Rectangle (Copy, Paste, Fill, Clear, Mark area), History (Undo, Redo),
  Catalog and Catalog palette (add the pick), Live (RUN) and Triggers (Check,
  Run). Spawns' Move and Place buttons have no action, so no keycap. Menu
  rows show the chord right-aligned after the label.
- **Nothing is cut silently.** A caption that does not fit beside its keycap
  (or, with none, in its plate) is shortened with "...", and hovering the
  button shows it whole: the tooltip leads with the caption ("Redo: Redo the
  step undone last") or is the caption when the button has no tip. A chord
  wider than its keycap or badge shows the key it ends on ("...+1" for
  Ctrl+Shift+1); the tooltip names it whole.
- **Tool rail badges.** Each tool's icon carries its chord as a small badge in
  the corner; its tooltip says what the click does and ends with the chord.
  Save, New square and Squares name theirs in the tooltip only.
- **Group tabs.** A panel group's tab reads "Catalog (F8)": the panel's View
  chord in parentheses, as text (no keycap).
- **Brush sliders.** The Terrain sliders' labels end with their two
  steppers' chords: "Radius  [ ]", "Step  Ctrl+[ ]", "Strength  Shift+[ ]".
  The shared modifier is said once only when both chords hold the same
  modifiers; otherwise each is said whole ("Step  Ctrl+Shift+[ Ctrl+]").
- **Status bar.** The strip across the foot lists the armed tool's live
  chords ("[Q] rotate", "[arrows] nudge" while Select or Move holds a
  selection), each with a tooltip naming the action; the chords are plain
  text there, not keycaps. Live means live for the keys this frame: the
  strip reads the contexts the dispatcher resolved them in.
- **A Spawns selection behind a tab** of its group is still the keys': Del,
  Ctrl+D and the strip act on it while Catalog, Locs or the Inspector is in
  front. Closing the Spawns tab takes it from them.
- **Chord spelling** is the word form ("Ctrl+Shift+Z", "Del"; "Cmd" for
  Ctrl on Apple): the chrome's Small face is the cache's p11, with no ⌘ ⇧ ⌥
  or arrow glyphs, and adding them is a bake from the cache this tree's
  build machine does not have. The symbol spelling is written
  (`Editor_ChordSpell`: "⌥⇧⌘Z" on Apple, the arrows, ↵ ⌦ ⌫ ⇥) and waits for
  the face (`EDITOR_HK_SMALL_FACE_GLYPHS`).
- **Check and Run** (Triggers panel, docs/MAPED_STATUS.md row 7d) are File
  rows too. Check compiles the scripts ON DISK (the user edits them in their
  own editor; unsaved script documents are not in it) and runs synchronously:
  the window waits for sscompile. Run is the Live panel's RUN and is refused
  while a RUN or build is in flight. Like every chord, neither fires while a
  text field (Subject, File, Op text) has the keyboard.
- **Repeats are not logged**: the edge logs `editor: <action> (<chord>)`, the
  20 Hz repeats do not.
