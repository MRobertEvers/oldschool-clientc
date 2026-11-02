# ToriRSMapEd hotkeys

The editor is driven from the keyboard as completely as from the mouse: every
menu row, tool, brush control, level button and panel has an action, every
action may carry a chord, the default chords are below, and the user may rebind
any of them from the Hotkeys panel (F1). Chords are shown beside the menu rows
and in the tool tips, and they follow a rebind.

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
- **Context.** An action lives in one context; a chord may be reused across
  contexts that cannot both be live (a loc selected vs none). Within a context
  two actions never share a chord: the panel shows the clash in red and the
  later binding wins until one is changed.
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
**S** the Spawns panel holds a selection and no loc is selected.

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

### Edit
| Action | Chord | Ctx |
|---|---|---|
| Undo | Ctrl+Z | G |
| Redo | Ctrl+Shift+Z, Ctrl+Y | G |
| Deselect / cancel (open menu, drag or rectangle in progress, then the selection) | Escape | G |
| Delete selection (locs; else the Spawns selection) | Delete, Backspace | L, S |
| Duplicate (locs; else the Spawns selection) | Ctrl+D | L, S |
| Rotate (selected locs; else the Place ghost) | Q | G |
| Reshape (selected loc; else the Place ghost) | Shift+Q | G |
| Apply the armed tool to the selection | Return | L |
| Select every loc of the hovered tile's square on the edit level | Ctrl+A | W |
| Nudge selection N / S / W / E one tile | Up / Down / Left / Right | L |
| Copy tiles | Ctrl+C | R |
| Paste tiles at the hovered tile | Ctrl+V | W |
| Fill tiles | Ctrl+Shift+F | R |
| Clear tiles | Shift+Delete | R |
| Mark area from rect | Ctrl+Shift+M | R |

### Tools
| Action | Chord | Ctx |
|---|---|---|
| Select | 1 | G |
| Rectangle | 2 | G |
| Raise | 3 | G |
| Lower | 4 | G |
| Flatten | 5 | G |
| Smooth | 6 | G |
| Paint underlay | 7 | G |
| Paint overlay | 8 | G |
| Paint flags | 9 | G |
| Place loc | 0 | G |
| Move loc | M | G |
| Rotate loc (tool) | T | G |
| Delete loc (tool) | X | G |
| Previous tool (swap with the last armed) | Grave (`) | G |
| Pick the hovered tile's paint into the palette (underlay, overlay, shape, flags) | I | W |
| Pick the hovered loc into the catalog | Shift+I | W |
| Place the catalog's loc on click (hold) | L | W |
| Delete the loc under the click (hold) | K | W |
| Clear every loc on the clicked tile (hold) | C | W |

### Brush (height and paint tools)
| Action | Chord | Ctx |
|---|---|---|
| Radius - / + | [ / ] | G |
| Strength - / + | Shift+[ / Shift+] | G |
| Step - / + | Ctrl+[ / Ctrl+] | G |

### Level
| Action | Chord | Ctx |
|---|---|---|
| Level down / up | PageDown / PageUp | G |
| Pin level 0..3 (again: Auto) | Ctrl+0 .. Ctrl+3 | G |
| Level Auto | Ctrl+4 | G |
| Show above on/off | Ctrl+5 | G |

### Camera
| Action | Chord | Ctx |
|---|---|---|
| Fly forward / back / left / right (held) | W / S / A / D | G |
| Fly down / up (held) | F / R | G |
| Fly faster (held with the above) | Shift | G |
| Turn left / right, tilt up / down (held) | Left / Right / Up / Down | G, no loc selected |
| Frame the selection, else the hovered square | Home | G |
| Frame the current square's centre | Ctrl+Home | G |

The manifest's `[debug:hotkeys]` flight (Ctrl+W/A/S/D/R/F) keeps working
where the editor claims no chord; Ctrl+S, Ctrl+D and Ctrl+A are claimed.

### View
| Action | Chord | Ctx |
|---|---|---|
| Hotkeys panel | F1 | G |
| Terrain | F2 | G |
| Palette | F3 | G |
| Squares | F4 | G |
| Locs | F5 | G |
| Spawns | F6 | G |
| Inspector | F7 | G |
| Catalog | F8 | G |
| Console | F9 | G |
| History | F10 | G |
| Areas | F11 | G |
| World | F12 | G |
| Triggers | Ctrl+E | G |
| Hide / show every panel but World | Tab | G |
| Reset layout | Ctrl+Shift+R | G |
| Snap to grid (toggle) | Ctrl+G | G |
| Snap while dragging a panel (hold; the toggle is untouched) | Ctrl | G |

Every other panel the View menu lists (Record, New, builders, Live, ...) is an
action too, unbound by default. On the web, F11 and F12 may be taken by the
browser; rebind them there.

## The Hotkeys panel (F1)

One row per action: group, name, context, chord. A search box filters by any
of them. Clicking a row's chord enters capture ("press a chord... Esc cancels,
Backspace unbinds"); the next chord pressed binds, a clash in the same context
is shown in red on both rows. "Reset to defaults" restores the table above.
Rebinds take effect at once: the menu rows and tool tips re-label.

## Action ids (`editor_hotkeys.ini`)

The file holds only what differs from the table above, one action per line
under `[hotkeys]`: `<action> = <chord>[, <chord>]`, or `<action> =` for
unbound. A line naming an unknown action, an unparsable chord or more than two
chords is skipped and named in the console (`editor: editor_hotkeys.ini: line
N: ...`); the rest of the file still applies. The ids, in the order of the
tables (the Hotkeys panel calls them by the name after the id):

- **File**: `file.new` New square..., `file.save_all` Save all, `file.save_as` Save square as new..., `file.bake` Bake cache (slow), `file.close` Close editor, `file.check_scripts` Check scripts, `file.run` Run (save, build, reload)
- **Edit**: `edit.undo` Undo, `edit.redo` Redo, `edit.deselect` Deselect / cancel, `edit.delete` Delete selection, `edit.duplicate` Duplicate, `edit.rotate` Rotate, `edit.reshape` Reshape, `edit.apply` Apply tool to selection, `edit.select_square` Select the square's locs, `edit.nudge_n` Nudge north, `edit.nudge_s` Nudge south, `edit.nudge_w` Nudge west, `edit.nudge_e` Nudge east, `edit.copy` Copy tiles, `edit.paste` Paste tiles, `edit.fill` Fill tiles, `edit.clear` Clear tiles, `edit.mark_area` Mark area from rect
- **Tools**: `tool.select` Select, `tool.rect` Rectangle, `tool.raise` Raise, `tool.lower` Lower, `tool.flatten` Flatten, `tool.smooth` Smooth, `tool.paint_underlay` Paint underlay, `tool.paint_overlay` Paint overlay, `tool.paint_flags` Paint flags, `tool.place` Place loc, `tool.move` Move loc, `tool.rotate` Rotate loc (tool), `tool.delete` Delete loc (tool), `tool.previous` Previous tool, `tool.pick_paint` Pick the tile's paint, `tool.pick_loc` Pick the hovered loc, `hold.place` Click places a loc (hold), `hold.delete` Click deletes a loc (hold), `hold.clear` Click clears the tile (hold)
- **Brush**: `brush.radius_down` Radius -, `brush.radius_up` Radius +, `brush.strength_down` Strength -, `brush.strength_up` Strength +, `brush.step_down` Step -, `brush.step_up` Step +
- **Level**: `level.down` Level down, `level.up` Level up, `level.pin0` Pin level 0, `level.pin1` Pin level 1, `level.pin2` Pin level 2, `level.pin3` Pin level 3, `level.auto` Level Auto, `level.above` Show above
- **Camera**: `camera.forward` Fly forward, `camera.back` Fly back, `camera.left` Fly left, `camera.right` Fly right, `camera.down` Fly down, `camera.up` Fly up, `camera.fast` Fly faster, `camera.turn_left` Turn left, `camera.turn_right` Turn right, `camera.tilt_up` Tilt up, `camera.tilt_down` Tilt down, `camera.frame` Frame the selection, `camera.frame_square` Frame the square's centre
- **View**: `view.hotkeys` Hotkeys panel, `view.terrain` Terrain, `view.palette` Palette, `view.squares` Squares, `view.locs` Locs, `view.spawns` Spawns, `view.inspector` Inspector, `view.catalog` Catalog, `view.console` Console (Output), `view.history` History, `view.areas` Areas, `view.world` World, `view.toolrail` Tools (rail), `view.rect` Rectangle panel, `view.record` Record, `view.locbuilder` Loc builder, `view.npcbuilder` NPC builder, `view.shelleditor` Shell editor, `view.iface` Interface editor, `view.cs2` CS2, `view.live` Live, `view.triggers` Triggers, `view.hide_panels` Hide / show panels, `view.reset_layout` Reset layout, `view.snap` Snap to grid, `view.snap_hold` Snap while dragging (hold)

Example:

    [hotkeys]
    version = 1
    file.bake = ctrl+b
    edit.redo = ctrl+y, ctrl+shift+z
    tool.raise =

## Where the build differs from the plan above

- **Rebinding from the panel binds ONE chord.** A capture replaces every chord
  of the action with the one pressed (Redo, Delete selection then have one);
  the second chord comes back with "Reset to defaults", or can be stated in
  the file (`edit.redo = ctrl+shift+z, ctrl+y`). Capturing a second slot would
  need a second click target per row the TREE rows do not have.
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
- **Panel buttons.** The Locs and Spawns buttons keep plain captions (a
  button caption is one label column wide) and carry their chord in the
  tooltip; menu rows show it right-aligned after the label.
- **Check and Run** (Triggers panel, docs/MAPED_STATUS.md row 7d) are File
  rows too. Check compiles the scripts ON DISK (the user edits them in their
  own editor; unsaved script documents are not in it) and runs synchronously:
  the window waits for sscompile. Run is the Live panel's RUN and is refused
  while a RUN or build is in flight. Like every chord, neither fires while a
  text field (Subject, File, Op text) has the keyboard.
- **Repeats are not logged**: the edge logs `editor: <action> (<chord>)`, the
  20 Hz repeats do not.
