# The agent control API

A way for a program to USE the client and its map editor: ask what is on
screen, press a button by its label, type into a field, click a world tile,
take a picture, and be told in a sentence when something was refused.

It is for an AI agent, a test script or a shell loop. Before it, a program
that wanted to drive the editor posted Win32 window messages at it and read
screenshots (`tools/editor_drive/drive.py`), or set one of the `TORIRS_EDITOR_*`
variables that pose a single photograph.

- The server: `src/agent/` (`agent_server.h` is the overview).
- The client for it: `tools/agent/torirs_agent.py` (a command line and a Python class).
- This file: every verb, with what it takes and what it answers.

> The exchanges in this file are written from the server's source. The member
> names and shapes are the code's; the VALUES are made up for illustration and
> were not captured from a run. `help` on a live server is the authority for
> the verb list.

Contents: [Start](#start) · [The protocol](#the-protocol) ·
[Things to know first](#things-to-know-first) · [Core verbs](#core-verbs) ·
[Editor verbs](#editor-verbs) · [Game verbs](#game-verbs) ·
[Driving the play client](#driving-the-play-client) ·
[A worked session](#a-worked-session) · [The Python client](#the-python-client) ·
[What is not built](#what-is-not-built)

---

## Start

The server is inside the client process and is off unless asked for.

| How | Meaning |
| --- | --- |
| `--agent-port <N>` | Listen on `127.0.0.1:<N>`. `0` picks a free port. |
| `--agent-port-file <path>` | Write the port (decimal, one line) to `<path>` once listening. The file is removed when the client exits cleanly. |
| `TORIRS_AGENT_PORT=<N>` | The same as the flag, when the flag is not given. |
| `TORIRS_AGENT_PORT_FILE=<path>` | The same as `--agent-port-file`. |

When it listens the client prints one line to stderr:

    agent: listening on 127.0.0.1:53127

`--agent-port` with something that is not a number 0..65535 stops the client
at once with a sentence. A bad `TORIRS_AGENT_PORT`, or a port that cannot be
bound, leaves the client running with the server off and says why on stderr
(a line that starts `agent:`). It binds to loopback only, never another
interface. There
is no password: anything on the machine that can reach the port can drive the
client, so do not forward it.

The easy way, from the repository root:

    python tools/agent/torirs_agent.py start manifests/manifest_osrs242_mapeditor.ini --window 1600x900

That starts `src/torirs_win64.exe` (Windows) or `src/torirs` (elsewhere) with
`--agent-port 0 --agent-port-file <temp>/torirs_agent.port`, waits until it
listens and has booted, and prints its `hello`. The client's own output goes
to `<port file>.log`. Every later `torirs_agent.py <verb>` finds the port
through that file with no flags. Add client arguments after `--`:

    python tools/agent/torirs_agent.py start manifests/manifest_osrs242_mapeditor.ini -- --soft3d

By hand:

    src/torirs_win64.exe --manifest manifests/manifest_osrs242_mapeditor.ini --agent-port 0 --agent-port-file agent.port
    python tools/agent/torirs_agent.py --port-file agent.port hello

It needs no window focus and works with the window behind others, because
input does not go through the OS (below).

One server per session: the window that was started with the port. A panel
the editor pops out into a window of its own is another process and has no
server (the two variables are cleared once the leader listens, so the windows
it starts do not inherit them).

The one exception is the PLAY CLIENT an editor's RUN starts: an editor that
has an agent port gives that client one of its own, so the agent that built a
lane can go on to play it. See [Driving the play client](#driving-the-play-client).

---

## The protocol

TCP, one JSON object per line each way, UTF-8.

    -> {"id": 7, "verb": "ui", "panel": "Catalog"}
    <- {"id": 7, "ok": true, "panels": [...]}

    -> {"id": 8, "verb": "ui_click", "label": "Nope"}
    <- {"id": 8, "ok": false, "error": "nothing that can be pressed is labelled \"Nope\": ui lists what is there"}

- A request is ONE flat object: strings, numbers, `true`/`false`, `null`, and
  arrays of those. No nested objects.
- `verb` names what to do. `id` is yours (a number or a string) and comes back
  on the reply; leave it out and the reply's is `null`. Those two names are the
  protocol's: no verb has an argument called `id` or `verb`, and a name given
  twice in one object is refused.
- Every other member is an argument of the verb. A member the verb does not
  take is refused by name (`"mouse does not take \"buton\": it takes x y action button mods"`),
  so a misspelt argument never silently does the default thing.
- A reply is `"ok": true` with the verb's members, or `"ok": false` with
  `"error"`: one sentence saying what was refused and what to do next.
- One reply per request, in the order the requests were sent on that
  connection. A verb that has to wait (frames, a click's release, a condition)
  holds its reply until it is done; the next request on the SAME connection
  waits behind it. Other connections are served meanwhile.
- Up to 8 connections. A request line is at most 1 MB.
- A blank line is ignored. Closing your sending half (`echo '{...}' | nc`) is
  fine: what you sent is still answered.

From a shell, without the Python client:

    printf '{"verb":"hello"}\n' | nc 127.0.0.1 53127

---

## Things to know first

**Frames.** The client runs a loop, 50 times a second when idle-capped. One
turn of it is a *frame*. The server is polled once per frame, before that
frame's input is handled. `hello` and several replies carry `frame`, the count
since the server started.

**Coordinates.** Every `x`, `y` and `rect` (`[x, y, w, h]`) is in SURFACE
pixels: the layout the pointer lives in, origin top-left. `hello` gives its
size. It is the window's client area when the window is not scaled, which is
how the editor runs.

**Input goes through the command bus, not the OS.** The window procedure turns
OS events into commands on a ring the client drains each frame. The input
verbs push the same commands on the same ring. So a click from here is what a
person's click is: it needs no focus, it does not move the real cursor, it
works under a headless video driver, and it is recorded and replayed with the
rest of a session.

**A click takes frames.** The pointer moves, a frame passes, the button goes
down, a frame passes, it comes up, and the reply follows two frames after the
last event, so what the click caused has been handled and drawn. A slow
consequence (a square loading, a RUN) is not finished by then: use
`wait until=idle`.

**One input script at a time.** Pointer and keyboard verbs from two
connections would interleave on one pointer, so the second waits for the
first to finish.

**Nothing an agent presses outlives the agent.** Keys and buttons held with
`key action=down` or `mouse action=down` are let go when the connection
closes. Modifiers given as `mods` are pressed for that verb only.

**Widget ids are handles, not names.** `ui` gives each widget an `id`. It
stays valid until its panel rebuilds its rows (a list refilled, a page
switched), when the number may be given to another widget. A verb that finds
its widget replaced says so. Prefer labels, and read `ui` again after anything
that changes a panel.

**The editor says what it did.** A refused action is a sentence in the status
bar and the console ("Place refused: tile 3222,3218 holds a wall of that
shape"). `action` and `world_click` return the lines said since they began;
`editor.status` is the status bar; `log` is the console.

---

## Core verbs

### hello

No arguments.

    -> {"id": 1, "verb": "hello"}
    <- {"id": 1, "ok": true, "version": 1, "mode": "editor", "ready": true, "editor": true,
        "frame": 412, "pid": 18244, "surface": {"w": 1600, "h": 900}, "chrome_scale": 1,
        "verbs": ["hello", "help", "wait", "mouse", "drag", "wheel", "key", "type", "shot", "ui",
                  "ui_click", "ui_set", "log", "quit", "editor", "actions", "action", "tile_to_screen",
                  "screen_to_tile", "world_click", "camera", "game"]}

| Member | Meaning |
| --- | --- |
| `version` | Raised when a verb's meaning changes. Read `verbs` to see what exists. |
| `mode` | `"editor"` when the client was booted with the map editor, else `"game"`. |
| `ready` | The client has booted (its interface is up). |
| `editor` | The editor's panels are up, so the editor verbs answer. |
| `surface` | The size of the space every `x`, `y` is in. |
| `chrome_scale` | The editor chrome's scale (1, 1.25, 1.5 ... 3). |
| `pid` | The client's process id. |

### help

No arguments. `{"verbs": [{"name", "args", "about", "editor_only"?}]}`: every
verb, the argument names it takes (space separated), and one line about it.

### wait

| Argument | Meaning |
| --- | --- |
| `frames` | Reply after this many frames (1..100000). |
| `until` | `"idle"` or `"ready"`. Give this or `frames`, not both. |
| `timeout_ms` | With `until`: give up after this long (default 10000). |

`ready`: the client has booted and, in an editor boot, its panels are up.

`idle`: for three frames in a row the client is booted, has no loads in
flight, the world is not loading, no RUN, build or Check of the scripts is in
flight, the lane's catalog index has been read, and no chrome panel is
waiting on its layout.

    -> {"id": 2, "verb": "wait", "until": "idle", "timeout_ms": 120000}
    <- {"id": 2, "ok": true, "frames": 318, "ms": 6371, "frame": 1204}

    <- {"id": 2, "ok": false, "error": "still waiting after 120000 ms: a RUN or a build is in flight"}

The catalog index is what an editor spends its first minute or two on: its
server reads the lane's whole content tree once after it starts. Until it has,
the Catalog's lists are partial, the Square map has no atlas, the instance
dialog cannot check a destination and Save cannot place a new square. So after
`start`, wait for it before the first step that needs any of those:

    -> {"id": 2, "verb": "wait", "until": "idle", "timeout_ms": 300000}

    <- {"id": 2, "ok": false, "error": "still waiting after 10000 ms: the lane's catalog index is still being read (37%): until it is, lists are partial and Save cannot place a new square (it takes a minute or two after the editor starts)"}

### mouse

| Argument | Meaning |
| --- | --- |
| `x`, `y` | The point. Required. |
| `action` | `"click"` (default), `"double"`, `"move"`, `"down"`, `"up"`. |
| `button` | `"left"` (default), `"right"`, `"middle"`. |
| `mods` | Any of `"shift"`, `"ctrl"`, `"alt"`, held for this verb. |

`down` leaves the button held until an `up` (or the connection closing). A
press must be on the surface; a `move` or an `up` may be off it.

    -> {"id": 3, "verb": "mouse", "x": 640, "y": 410, "button": "right"}
    <- {"id": 3, "ok": true, "x": 640, "y": 410, "frame": 1290}

### drag

| Argument | Meaning |
| --- | --- |
| `from`, `to` | `[x, y]` each. Required. |
| `steps` | Pointer moves between them, one a frame (default 12, 1..600). |
| `button`, `mods` | As `mouse`. |

Replies when the button is released: `{x, y, frame}` (where the pointer ended).

    -> {"id": 4, "verb": "drag", "from": [700, 300], "to": [900, 420], "steps": 20}

### wheel

| Argument | Meaning |
| --- | --- |
| `x`, `y` | Where the pointer is while the wheel turns. Required. |
| `notches` | -200..200, not 0. Positive is away from the user (scroll up). One notch a frame. |
| `mods` | As `mouse` (`ctrl`+wheel zooms a map well, moves the Height brush's grid). |

### key

| Argument | Meaning |
| --- | --- |
| `chord` | `[ctrl+][shift+][alt+]<key>`. Required. |
| `action` | `"tap"` (default), `"down"`, `"up"`. |
| `hold` | With `tap`: frames the key stays down (default 2). |

Key names: `a`..`z`, `0`..`9`, `f1`..`f12`, `escape`, `return` (or `enter`),
`backspace`, `delete`, `insert`, `tab`, `space`, `left`, `right`, `up`, `down`,
`pageup`, `pagedown`, `home`, `end`, `comma`, `period`, `minus`, `equals`,
`lbracket`, `rbracket`, `semicolon`, `apostrophe`, `grave`, `slash`,
`backslash`, and `shift`, `ctrl`, `alt` alone. `cmd` is `ctrl`. These are the
names the editor's hotkey file and `actions` use, so a chord read from
`actions` can be pressed here.

A key press pushes what a real one does: the key's edge, its OSRS key state
and key event, and (with no Ctrl or Alt held) the character a US keyboard
types for it. So `key chord=a` with a field focused types an `a`, and with
nothing focused runs whatever the editor binds to `A`.

    -> {"id": 5, "verb": "key", "chord": "ctrl+z"}
    <- {"id": 5, "ok": true}

To hold a key across other verbs (the camera's flight, Space for the grab
pan): `key chord=w action=down`, then later `key chord=w action=up`.

### type

| Argument | Meaning |
| --- | --- |
| `text` | What to type. Required. |

Typed into whatever has the keyboard, one character a frame. Each character
is a typed-character event with no key, so typing never fires a hotkey. A
newline is the Return key and a tab the Tab key. Characters U+0020..U+00FF;
the editor's own fields keep only U+0020..U+007E of those.

    -> {"id": 6, "verb": "type", "text": "crate"}
    <- {"id": 6, "ok": true, "characters": 5, "focus": 211}

`focus` is the widget that has the keyboard after it (-1: none, so the text
went nowhere). To fill a field, `ui_set` is one call instead of three.

### shot

| Argument | Meaning |
| --- | --- |
| `path` | Where to write. Ends in `.png` or `.bmp`. Absolute, or relative to the client's working directory (the repository root under `start`). Required. |
| `rect` | `[x, y, w, h]` of the picture to keep. |

Writes the PRESENTED frame: the game area as the window shows it, after two
more frames have been drawn so the picture includes what the last request
caused. Replies once the file exists. The folder is made when it does not
exist (and the folders above it); one that cannot be made is refused at once,
by name:

    <- {"id": 7, "ok": false, "error": "the folder \"Q:/nowhere/shots\" does not exist and could not be made (No such file or directory): give a \"path\" in a folder this client can write"}

    -> {"id": 7, "verb": "shot", "path": "C:/tmp/editor.png"}
    <- {"id": 7, "ok": true, "path": "C:/tmp/editor.png", "w": 1600, "h": 900, "format": "png",
        "surface": {"w": 1600, "h": 900}, "frame": 1410}

When `w`, `h` equal `surface` (and no `rect` was given), a pixel of the
picture is a point of the surface. They differ when the window scales or
letterboxes the frame (interface scaling, a HighDPI display).

Which windows can: Windows with the software renderer (always) and with D3D9
(Windows 8.1 or later, window not minimised); SDL with the software renderer.
An SDL window presenting through OpenGL is refused with a sentence.

### ui

The editor's chrome as data: every panel, and in it every widget with its id,
kind, label, value and box.

| Argument | Meaning |
| --- | --- |
| `panel` | Only panels this names: a title, a module name (`catalog`), or part of a title. |
| `text` | Only widgets that hold this text (label, value, tooltip, an option, a list row), and only panels that have one. |
| `visible_only` | `true`: leave out closed panels and widgets that are not on screen. |

Without arguments every panel is listed. A CLOSED panel is listed without its
widgets (there are dozens of them); name it with `panel` to see inside. In an
open panel, a widget that is not on screen is listed with `"visible": false`
and `"why"`, and has no `rect`.

    -> {"id": 8, "verb": "ui", "panel": "catalog", "visible_only": true}
    <- {"id": 8, "ok": true, "frame": 1502, "surface": {"w": 1600, "h": 900}, "focus": -1, "modal": -1,
        "pointer": [640, 410],
        "panels": [
          {"index": 14, "title": "Catalog", "module": "catalog", "style": "window", "visible": true,
           "rect": [1280, 44, 320, 420], "scroll": 0, "scroll_max": 180,
           "widgets": [
             {"id": 211, "kind": "textinput", "label": "Find", "text": "crate", "rect": [1286, 66, 300, 18]},
             {"id": 214, "kind": "vlist", "label": "\tname\t\tshapes\tid", "value": -1, "row_count": 37,
              "first": 0, "window": 12,
              "rows": [
                {"row": 0, "text": "-\tcrate\t\t10\t355", "rect": [1286, 108, 300, 15]},
                {"row": 1, "text": "-\tcrates\t\t10\t356", "rect": [1286, 123, 300, 15]}
              ],
              "rect": [1286, 88, 300, 202]},
             {"id": 228, "kind": "textinput", "label": "New name", "text": "", "rect": [1286, 300, 300, 18]},
             {"id": 230, "kind": "button", "label": "Copy to new", "rect": [1286, 322, 140, 18]}
           ]}
        ]}

Top level:

| Member | Meaning |
| --- | --- |
| `focus` | The widget with the keyboard, or -1. |
| `modal` | The panel index of an open dialog, or -1. While one is open it takes every press. |
| `pointer` | Where the chrome last saw the pointer. |
| `stale` | Present and `true` when a panel changed since the boxes were resolved; they move next frame. |
| `dropdown` | Present while a dropdown's list is open (below). |
| `minimenu` | Present while the right-click menu is open (below). |

A panel: `index`, `title`, `module` (the registry name the View menu and the
hotkeys use), `group` (for the frame of a tab group), `style` (`window`,
`menubar`, `toolbar`, `menu`), `visible`, `rect`, and when they apply
`collapsed`, `modal`, `maximised`, `scroll` / `scroll_max` (a panel that
scrolls), `tabs` / `active_tab` (a panel with a tab strip), `widgets`.

A widget: `id`, `kind`, `rect`, and what applies of:

| Member | Meaning |
| --- | --- |
| `label` | Its caption: what a person calls it, and what `label` arguments match. |
| `text` | Its value: a field's content, a key-value label's value, a dropdown's chosen option. |
| `value` | A number: a dropdown's chosen index, a slider's value, a list's selected row, a strip's active tab. |
| `checked` | A checkbox, radio, switch row, pressed icon button, selected tree row. |
| `disabled` | Present and `true` when it cannot be used now. |
| `focused` | Present and `true` when it has the keyboard. |
| `chord` | The hotkey it wears or its menu row names. |
| `tooltip` | Its hover text. |
| `visible`, `why` | `false` and one of `panel_closed`, `panel_collapsed`, `hidden`, `other_tab` (with `tab`), `scrolled_out`, `off_window` (past the window's edge: a panel taller or wider than the window that does not scroll; a person cannot press it, so `ui_click` refuses it too), `cut_off`. |

Kinds: `label` (with `style`: `heading`, `paragraph`, `key_value`, `progress`,
`error`), `button`, `iconbutton`, `menuitem`, `checkbox`, `radio`, `listrow`,
`textinput`, `textarea`, `colorpick`, `slider` (`min`, `max`, `step`),
`dropdown` and `menu` (`options`: the first 64 labels; `option_count` when
there are more; `open`), `tabstrip` (`tabs`: `[{tab, label, rect}]`), `tree`
(`depth`, `expander`: `leaf` / `closed` / `open`), `vlist` and `code`
(`row_count`, `first`, `window`, `rows`: `[{row, text, rect}]` for the rows on
screen; a row's cells are separated by tabs), `graph`, `custom`, `modelview`.

The open dropdown:

    "dropdown": {"widget": 240, "label": "Shape", "option_count": 23, "first": 0,
                 "rows": [{"option": 0, "label": "wall", "rect": [1290, 150, 120, 14]},
                          {"option": 1, "label": "wall decor", "rect": [1290, 170, 120, 14]}]}

It shows ten options from `first`. To reach another, turn the wheel over one
of its rows. A menu's rows carry `chord` when they name one, and `disabled`.

The right-click menu (the game's, and the editor's Select / Delete rows):

    "minimenu": {"visible": true, "rect": [600, 380, 150, 64],
                 "entries": [{"index": 0, "text": "Cancel", "rect": [600, 428, 150, 15]},
                             {"index": 1, "text": "Select Wall", "rect": [600, 413, 150, 15]}]}

### ui_click

Click something by what it is rather than where it is.

| Argument | Meaning |
| --- | --- |
| `widget` | A widget's `id`. |
| `label` | Or its caption (case aside). The whole caption is tried first, then a part of it. |
| `panel` | With `label`: only in the panel this names. |
| `tab` | With a tab strip: which tab (index). A tab's title also works as `label`. |
| `row` | With a list (`vlist`): which row (absolute index). |
| `option` | An option (index) of the OPEN dropdown. An option's label also works as `label`. |
| `menu` | A row (index) of the OPEN right-click menu. A row's text also works as `label`. |
| `button`, `mods` | As `mouse`. |
| `double` | `true`: click twice. |

What it does: if the widget's row is scrolled out of its panel, the panel is
scrolled to it (a list is scrolled to the row); the pointer goes to it; the
server checks that the chrome itself has that widget, row or option under the
pointer; then it presses and releases.

A `label` is looked for where a click would land: among the open dropdown's
rows first, then the open right-click menu's, then the tabs and widgets of the
open panels.

    -> {"id": 9, "verb": "ui_click", "label": "Copy to new", "panel": "Catalog"}
    <- {"id": 9, "ok": true, "clicked": "widget", "widget": 230, "x": 1356, "y": 331,
        "panel_open": true, "focus": -1, "frame": 1560}

("Copy to new" opens no dialog: it copies the picked record to the name in
the "New name" field above it, so fill that first with `ui_set label="New
name" panel=Catalog text=my_crate`, and read `editor.status` for what it
made.) A button that does open one says so:

    -> {"id": 9, "verb": "ui_click", "label": "New square..."}
    <- {"id": 9, "ok": true, "clicked": "widget", "widget": 41, "x": 60, "y": 31,
        "panel_open": true, "focus": 251, "dialog": "New square", "frame": 1570}

The reply says what was pressed (`clicked`: `widget`, `tab`, `row`, `option`,
`menu`; `widget`; `index`; the point) and what the chrome says of it after:
`checked`, `active_tab`, `value`, `text`, `panel_open`, `focus`, `dropdown`
(a list is now open), `dialog` (a dialog is now up).

Refusals name the cause:

    "2 things are labelled \"Find\" (textinput 211 \"Find\" in Catalog; textinput 402 \"Find\" in Squares): name one by \"widget\", or narrow it with \"panel\""
    "\"Teleport to selection\" is in the panel \"Live\", which is closed: open it first (the View menu, or its view.* action)"
    "\"Rotation\" is on the tab \"Locs\" of the panel \"Catalog\", which is not the tab showing: click that tab first"
    "button 230 \"Copy to new\" in Catalog was not clicked: at its point (1356, 331) the dialog \"Save changes?\" is open and takes every press until it is answered"
    "option 17 \"roof edge\" of the open list is outside the list's window (options 0..9 of 23 show): turn the wheel over the list to bring it in, then click it"

A label is read, not pressed: `ui_click` refuses one.

### ui_set

Put text in a field.

| Argument | Meaning |
| --- | --- |
| `text` | What it should hold. Required. `""` clears it. At most 191 characters, U+0020..U+007E (a `textarea` also takes newlines). |
| `widget` or `label` (with `panel`) | The field: a `textinput`, `textarea` or `colorpick`, or a `slider`. |
| `enter` | `false`: do not press Return after (default `true`). |

A `slider` takes a whole number in its `min`..`max`, snapped to its `step`,
and is set at once, as its arrow keys set it; the reply is `{widget, value}`
(the value it holds now). Out of range or not a number is refused.

    -> {"id": 9, "verb": "ui_set", "label": "Width (half tiles)", "panel": "Terrain", "text": "6"}
    <- {"id": 9, "ok": true, "widget": 93, "value": 6}

For a text field it clicks the field, checks the field took the keyboard, holds Backspace until
it is empty, types the text, checks the field holds exactly that, and presses
Return to commit. A `textarea` is not committed (Return is a new line there).

    -> {"id": 10, "verb": "ui_set", "label": "Find", "panel": "Catalog", "text": "crate"}
    <- {"id": 10, "ok": true, "clicked": "widget", "widget": 211, "x": 1511, "y": 75, "text": "crate",
        "panel_open": true, "focus": 211, "frame": 1622}

    <- {"id": 10, "ok": false, "error": "textinput 260 \"Height\" in Terrain holds \"12\" after typing \"12x\": the field refused or cut part of it, and it was not committed"}

### log

| Argument | Meaning |
| --- | --- |
| `source` | `"console"` (the editor's Output panel; the default in an editor), `"live"` (its build and live lines), `"chat"` (the chat box; the default in a game). |
| `since` | A `cursor` from an earlier reply: only lines after it. |
| `limit` | At most this many lines, the newest (default 200, 1..2000). |

    -> {"id": 11, "verb": "log", "since": 88}
    <- {"id": 11, "ok": true, "source": "console",
        "lines": ["editor: Save all (Ctrl+S)", "saved 2 documents"], "cursor": 90}

`lines` are oldest first. `cursor` goes back in as `since`. `dropped` is how
many lines after `since` are not in `lines` (older than the console keeps, 512
lines, or past `limit`). For `live` the cursor only says whether anything is
new: an unchanged cursor gets no lines, a changed one the newest `limit`.

### quit

| Argument | Meaning |
| --- | --- |
| `discard` | `true`: quit even with unsaved documents. |

Ends the client the way closing its window does, on the next frame, after this
reply is sent. Refused while documents are unsaved, unless `discard`.

    <- {"id": 12, "ok": false, "error": "2 open documents have unsaved edits: save first (action file.save_all), or pass \"discard\": true to quit without them"}

---

## Editor verbs

Only in an editor window. Elsewhere: `"not an editor window: ..."`.

### editor

No arguments. What a person reads off the status bar and the panels.

    -> {"id": 20, "verb": "editor"}
    <- {"id": 20, "ok": true, "panels_open": true, "tool": 8, "tool_name": "Place", "level": -1,
        "workspace": "build", "lane": "working", "lane_changes": 3, "writable": true, "unsaved": 1,
        "squares_open": [{"x": 50, "z": 50, "unsaved": true}],
        "selection": {"locs": 1,
                      "first_loc": {"id": 355, "x": 3222, "z": 3218, "level": 0, "shape": 10, "rotation": 1},
                      "spawns": 0},
        "pick": {"kind": "loc", "id": 355, "name": "crate"},
        "hover_tile": [3224, 3219, 0],
        "world_rect": [34, 44, 1240, 700], "scene": {"x": 3168, "z": 3168, "size": 104},
        "camera": {"x": 3222, "z": 3218, "level": 0, "yaw": 0, "pitch": 383, "zoom": 2600},
        "status": "Placed loc 355 at 3222,3218 L0", "run": "idle", "live": "detached", "console_cursor": 91}

| Member | Meaning |
| --- | --- |
| `panels_open` | The editor's panels are showing (its toggle key hides them all). |
| `tool`, `tool_name` | The armed tool: what the next world click does. |
| `level` | The edit level: -1 is Auto (the plane under the pointer), 0..3 a pinned plane. |
| `workspace` | `build`, `terrain`, `script`, `playtest`. |
| `lane`, `lane_changes` | The editor lane, and how many documents differ from its base. |
| `writable` | The server can save. When false, `read_only_reason` says why. |
| `unsaved` | Open documents with edits no save has taken. |
| `squares_open` | The open map squares: `x`, `z` (square numbers: tile / 64), `storage` when not the content tree's `maps/`, `unsaved`. |
| `selection` | `locs` (count), `first_loc`, `tile` `[x, z, level]`, `rect` `[x0, z0, x1, z1, level]`, `spawns` (count). |
| `pick` | The catalog's pick, what Place stamps: `kind` (`loc`, `npc`, `obj`), `id`, `name`. |
| `hover_tile` | `[x, z, level]` under the pointer; absent when it is over no tile. |
| `world_rect` | The 3D view's box on the surface. All zero while the World panel is closed. |
| `scene` | The loaded scene: its south-west tile and its size in tiles. |
| `camera` | As the `camera` verb. |
| `status` | The status bar's sentence (for 6 s after it was said), else `""`. |
| `run` | `idle`, `running`, `done`, `stopped` (the last RUN failed; `run_message` says where). |
| `live` | The live link: `detached`, `connecting`, `attached`, `failed` (`live_message` says why). |
| `console_cursor` | A `log` cursor for now. |

Tiles are absolute world tiles everywhere in this API.

### actions

| Argument | Meaning |
| --- | --- |
| `text` | Only actions whose id, name or group holds this (case matters). |

Every hotkey action: every menu row, tool, brush control, level button, camera
move and View-menu panel the keyboard can reach.

    -> {"id": 21, "verb": "actions", "text": "file."}
    <- {"id": 21, "ok": true, "actions": [
         {"id": "file.new", "group": "File", "name": "New square...", "chords": ["ctrl+n"], "enabled": true},
         {"id": "file.save_all", "group": "File", "name": "Save all", "chords": ["ctrl+s"], "enabled": true},
         {"id": "file.bake", "group": "File", "name": "Bake cache (slow)", "chords": [], "enabled": true},
         {"id": "file.run", "group": "File", "name": "Run (save, build, reload)", "chords": ["ctrl+return"],
          "enabled": true}]}

`chords` are the live bindings in `key`'s spelling (empty when unbound).
`enabled` is whether the chord would fire NOW: the action lives in a context
that is live (a loc is selected, the pointer is over the World, a rectangle is
held). `held: true` marks a key held over the World (the camera's flight, the
click accelerators): use `key action=down` / `up` for those, not `action`.

### action

| Argument | Meaning |
| --- | --- |
| `action` | An action's `id` from `actions` (`"file.save_all"`). Required. Named `action`, not `id`, because a request's `id` is the protocol's own. |

Runs it the way the command palette's row does (Ctrl+K, type its name, Enter):
where the menu bar would, or, for the ones only the app can perform (the
camera, the eyedroppers, Paste at the hovered tile), by the app's key
dispatcher on its next frame. It runs whatever `enabled` says. Replies two
frames later with what the editor said.

    -> {"id": 22, "verb": "action", "action": "file.save_all"}
    <- {"id": 22, "ok": true, "action": "file.save_all", "name": "Save all", "by": "editor",
        "status": "", "lines": ["saved m50_50 (map, locs)"], "console_cursor": 93}

    -> {"id": 23, "verb": "action", "action": "level.above"}
    <- {"id": 23, "ok": true, "action": "level.above", "name": "Show above", "by": "editor",
        "status": "Show above refused: it needs a pinned level (Alt+0..3)",
        "lines": ["Show above refused: it needs a pinned level (Alt+0..3)"], "console_cursor": 94}

`by` is who ran it: `editor` (the panels, at once) or `app` (the ones that
need the world under the pointer, a frame later).

`ok` means the action was run, not that it did what you hoped: read `status`
and `lines`. An action that works on what is under the pointer (Paste, the
eyedroppers, Place here) reads the real hover, so put the pointer there first
(`tile_to_screen`, then `mouse action=move`). The editor's panels must be open.
A side effect of an `app` one: the keyboard focus a field had is dropped.

### tile_to_screen

| Argument | Meaning |
| --- | --- |
| `x`, `z` | The tile. Required. |
| `level` | 0..3 (default: the pinned level, else 0). |
| `height` | 0..4096 fine units above the ground (128 a tile, default 0): the point of something standing on the tile. |

Where the tile's centre is on the surface, on that level's ground (or
`height` above it). It also answers in a GAME window, the play client
included, over its 3D view: there a tile outside the scene the player has
loaded is refused. To use a loc in the game, right-click its tile's point;
when the menu shows only "Walk here" the ground point missed the loc's
model, so ask again with `height=60` (a chest, a churn) or more.

    -> {"id": 24, "verb": "tile_to_screen", "x": 3222, "z": 3218}
    <- {"id": 24, "ok": true, "x": 640, "y": 410, "visible": true, "level": 0}

    <- {"id": 24, "ok": true, "x": 1420, "y": 96, "visible": false, "why": "covered", "level": 0}

`why`: `behind_camera`, `off_view` (outside the World panel), `covered` (under
a panel drawn over the World), `scene_edge` (the loaded scene's first row or
column, which is not placed). A tile outside the loaded scene is `ok: false`,
naming the square to open.

### screen_to_tile

| Argument | Meaning |
| --- | --- |
| `x`, `y` | A point of the World view. Required. |

The tile the editor hovers there: the same answer its status bar and its tools
use, on the pinned plane when a level is pinned. It MOVES THE POINTER to the
point and reads the hover three frames later.

    -> {"id": 25, "verb": "screen_to_tile", "x": 640, "y": 410}
    <- {"id": 25, "ok": true, "x": 3222, "z": 3218, "level": 0}

### world_click

| Argument | Meaning |
| --- | --- |
| `x`, `z` | The tile. Required. |
| `level` | 0..3 (default: the pinned level, else 0). |
| `button`, `mods` | As `mouse`. |
| `force` | `true`: click the tile's point even when the editor hovers another tile there. |

Clicks the tile with whatever tool is armed. The pointer goes to the tile's
point; the server waits for the editor's hover; the press goes down only when
the editor hovers THAT tile on THAT level.

    -> {"id": 26, "verb": "world_click", "x": 3222, "z": 3218}
    <- {"id": 26, "ok": true, "tile": [3222, 3218, 0], "x": 640, "y": 410,
        "status": "Placed loc 355 at 3222,3218 L0", "lines": ["Placed loc 355 at 3222,3218 L0"],
        "locs_selected": 0}

    <- {"id": 26, "ok": false, "error": "tile 3222,3218 level 1 was not clicked: the pointer at its point (640, 372) is on tile 3222,3220 level 0 -- something nearer the camera is in front, or the edit level is another; pin the level (action level.pin1), move the camera, or pass \"force\": true to click the point anyway"}

A right click opens the menu: the reply carries `"minimenu": true`, `ui` lists
its entries, and `ui_click` with the row's text presses one.

### camera

| Argument | Meaning |
| --- | --- |
| `x`, `z` | The tile to look at (together). |
| `level` | The plane that tile is on, 0..3. |
| `yaw` | 0..2047, a full turn. |
| `pitch` | Clamped to what the view allows. |
| `zoom` | The eye's distance from the tile, in fine units (128 a tile), 64..100000. |

With no arguments it reads the pose. With any, it changes those parts and
answers the pose as it landed.

    -> {"id": 27, "verb": "camera", "x": 3222, "z": 3218, "zoom": 1800}
    <- {"id": 27, "ok": true, "camera": {"x": 3222, "z": 3218, "level": 0, "yaw": 0, "pitch": 383, "zoom": 1800}}

A tile outside the loaded scene is refused, naming the square to open first.

---

## Game verbs

They answer in any window that is in the game: a client started on its own,
an editor window, and the play client an editor's RUN starts (next section).

### game

| Argument | Meaning |
| --- | --- |
| `chat_lines` | How many of the newest chat lines (default 10, 0..100). |

    -> {"id": 30, "verb": "game"}
    <- {"id": 30, "ok": true, "logged_in": true, "screen": "game", "connected": true,
        "player": {"name": "asdf", "x": 3222, "z": 3218, "level": 0, "instance": false, "idle": true},
        "minimenu": {"visible": false},
        "chat": [{"type": 0, "text": "Welcome to RuneScape."}], "chat_cursor": 1,
        "dialog": {"title": "Select an Option", "options": ["Yes.", "No."]},
        "page": {"kind": "npc", "speaker": "Hans", "text": "Hello. What are you doing here?",
                 "continue": "Click here to continue"}}

| Member | Meaning |
| --- | --- |
| `logged_in` | A session is up and the gameframe is showing. |
| `screen` | `boot`, `title`, `connecting`, `game`. |
| `player` | Absent until the local player has spawned. `x`, `z` are the server's tile; `walking_to` is where the walk in progress ends. `instance` is `true` while the tile is in the server's map-instance pool (map square x >= 100, tile x >= 6400): the player stands in a runtime copy, not on the real map. |
| `minimenu` | As in `ui`. |
| `chat` | Oldest first. `log source=chat since=<chat_cursor>` continues from here. |
| `dialog` | Present while a dialogue's menu is up: its `title` and its `options`. |
| `page` | Present while the chat box shows a page of dialogue: `kind` (`npc`, `player`, `mesbox`, `objbox`), `speaker` (the name over an npc's or the player's line, a titled message box's title), `text` (what it says, on one line: a `<br>` reads as a space, other tags are dropped), and `continue` (the line that goes on, "Click here to continue", when the page has one). |

To press things in the game, use `mouse`, `key`, `ui_click` with `menu` or a
menu row's text, and `npc_click`. To go on from a `page`, press Space
(`key chord=space`), as a player does. To say something, use `chat`.

### chat

| Argument | Meaning |
| --- | --- |
| `text` | The line: what to say, or `::` and a cheat (`::tele 0_50_50_22_18`). Required. At most 500 bytes. |

Says the line the way the chat box's Enter does, without typing it: a line
that starts `::` goes to the server as a cheat (the rest of the line), any
other as public chat. Refused when the client is not in the game.

    -> {"id": 33, "verb": "chat", "text": "::tele 0_50_50_22_18"}
    <- {"id": 33, "ok": true, "sent": "cheat", "text": "tele 0_50_50_22_18", "chat_cursor": 14}

    <- {"id": 33, "ok": false, "error": "this client is not in the game (its screen is \"title\"): nothing would hear the line"}

The line leaves with that frame's packets; what the server does with it shows
a tick or two later (`game.player`, and `log source=chat since=<chat_cursor>`
for what it said). A cheat the server does not know says so in chat, or says
nothing: read the lane's `[debugproc,...]` scripts for the ones it has.

`type` is still how a FIELD of the game is filled (a "Enter amount" prompt, a
search box): it types where the game's keyboard focus is. Typed into the chat
box (`type text="::telehere\n"`) a line is said only while nothing else has
the keys -- its reply's `"focus": -1` -- and a dialogue page, a prompt or a
focused field takes them first, silently. `chat` does not depend on focus:
use it to say a line or run a cheat.

### npcs

| Argument | Meaning |
| --- | --- |
| `name` | Only npcs whose name holds this (letter case aside). |
| `npc_id` | Only npcs of this type. (`id` is the request's own number, so the type is `npc_id`.) |
| `limit` | Most rows to give (default 64, 1..512). |

The npcs the client has been sent, each where it is now.

    -> {"id": 31, "verb": "npcs", "name": "pip"}
    <- {"id": 31, "ok": true, "count": 1, "npcs": [
         {"slot": 12, "id": 27410, "name": "Pip", "x": 4534, "z": 3927, "level": 0,
          "screen": [612, 344], "visible": true, "ops": ["Talk-to"]}]}

| Member | Meaning |
| --- | --- |
| `slot` | The server's slot for this copy: what tells two npcs of one type apart. |
| `id` | Its type as this client shows it (a quest form of a multi-form npc is the form). |
| `x`, `z`, `level` | Its tile. |
| `screen` | Where its body is drawn this frame. Absent when that is outside the World view. |
| `visible` | `screen` is there and nothing is drawn over it: no window of the client's, and none of the game's interfaces (the play client's chat box and side panel lie over its 3D view; a body drawn under them is not `visible`, and `npc_click` passes it by). |
| `ops` | Its menu options, as the right-click menu will list them. |
| `count` | How many matched; `truncated` is there when that is more than `limit`. |

### npc_click

| Argument | Meaning |
| --- | --- |
| `name`, `npc_id`, `slot` | Which npc: at least one. Of several that match, the one nearest the middle of the view. |
| `button`, `mods` | As `mouse`. |

An npc wanders, so a point read from a screenshot or from `npcs` is stale by
the time it is pressed. This finds the npc, rests the pointer on it for three
frames, and presses only if it is still there.

    -> {"id": 32, "verb": "npc_click", "name": "pip", "button": "right"}
    <- {"id": 32, "ok": true, "npc": {"slot": 12, "id": 27410, "name": "Pip", "x": 4534, "z": 3927, "level": 0},
        "x": 612, "y": 344,
        "minimenu": {"visible": true, "rect": [600, 380, 150, 64],
                     "entries": [{"index": 0, "text": "Talk-to Pip"}, {"index": 1, "text": "Examine Pip"}]}}

    <- {"id": 32, "ok": false, "error": "the npc walked from (612, 344) to (640, 351) before the press (nothing was clicked): ask again"}
    <- {"id": 32, "ok": false, "error": "that npc is at tile 4534,3927 level 0 but not in view (or a window covers it): walk or turn the camera to it, then ask again"}

A left click does the npc's first option, as it does for a player. The Python
`npc_click` asks again (five times) when the npc walked off first.

---

## Driving the play client

RUN (`action file.run`, the Live panel's button) saves, builds the lane,
starts or reloads its servers and starts a PLAY CLIENT: a game client logged
into the lane as the lane's own account. That client is a second process. When
the editor was started with an agent port, the play client is started with one
too:

    <play client> ... --agent-port 0 --agent-port-file <the editor's port file>.play

(an editor listening without a port file: `<lane>/live/play/agent.port`), and
the editor's `editor` reply carries it while that client runs:

    "play": {"state": "logged in", "pid": 23120, "account": "maped43620", "port": 53190,
             "port_file": "C:/tmp/agent.port.play"},
    "lane_ports": {"game": 43620, "js5": 43621, "io": 43622}

| Member | Meaning |
| --- | --- |
| `play.state` | `starting` (spawned, not on the server yet), `logged in`, `logged out` (the process runs, the account left). Absent: no play client runs. |
| `play.pid` | Its process id. |
| `play.account` | The account it plays (the Live panel's "Teleport to selection" and Place player move this one). |
| `play.port` | Its agent port, once it listens. Absent for the first moments after RUN, and when it was started without one. |
| `play.port_file` | Where it writes that port. |
| `lane_ports` | The ports the lane's game server, JS5 server and io server listen on, for a script that wants to connect something of its own. |

Connect to `play.port` and every core and game verb works there: `hello` says
`"mode": "game"`, `game` says where the player is and what the chat box shows,
`npcs` / `npc_click` find and press an npc, `mouse` clicks the world, `chat`
says a line or runs a cheat, `shot` photographs it. The editor verbs are
refused there (it is not an editor window).

From the command line, `--play` sends a verb to the play client of the editor
the other flags name:

    A action file.run
    A wait until=idle timeout_ms=300000
    A --play game
    A --play npc_click name=hans
    A --play game                       # "page": what Hans said
    A --play key space                  # Click here to continue
    A --play chat "::tele 0_50_50_22_18"
    A --play shot C:/tmp/play.png

In Python, `agent.play(wait=20)` returns a `Client` for it (waiting up to that
long for it to listen):

```python
agent.action("file.run")
agent.wait(until="idle", timeout_ms=300000)
with agent.play(wait=30) as play:
    play.wait(until="ready", timeout_ms=120000)
    print(play.game()["player"])        # {"x": 3222, "z": 3218, "level": 0, "instance": false, ...}
```

A RUN that finds the play client already online reloads the server under it
and leaves the client running, so the port stays the same; a Restart starts a
new one on a new port, so read `editor.play.port` again after it.

The moves a tester makes most:

- **Go somewhere.** In the EDITOR: arm Place player (`action tool.place_player`)
  and `world_click` the tile, or select a tile and press the Live panel's
  "Teleport to selection". Both move the play account at once, whatever the
  play client is doing. In the PLAY CLIENT: `chat text="::tele <level>_<mx>_<mz>_<lx>_<lz>"`.
- **Read where it is.** `game.player`: `x`, `z`, `level`, and `instance`.
- **Talk.** `npc_click name=...`, then `game.page` for each page and
  `key chord=space` to go on; `game.dialog.options` when a menu is up, and
  `key chord=1`..`5` to pick a row.
- **Use a loc.** `tile_to_screen` its tile (with `height=60` when the
  ground point misses the model), `mouse` on it (a right click and
  `ui_click` on the menu row's text when the first option is not the one),
  then read `game.player` again. A tile the reply calls `off_view` is too far
  for the play window: `::tele` next to it first.
- **Inside an instance.** `game.player.instance` is `true` and `x` is 6400 or
  more. A tile of the copy is the template's tile moved by the same whole
  squares: the copy's square is `x / 64`, `z / 64` of where the player
  landed, and `::tele 0_<that x square>_<that z square>_<lx>_<lz>` moves
  within it (a copy of m1_100's tile 37,26 in the copy at square 100,1 is
  `::tele 0_100_1_37_26`).

`TORIRSMAPED_PLAY_ENV_TORIRS_AGENT_PORT` (with `..._AGENT_PORT_FILE`) in the
editor's environment is the older way and still overrides this: the play
client then takes its port from those two.

---

## A worked session

Open the editor, find the Catalog's Find box, type `crate`, read the result
rows, press RUN, wait until idle, read the console. Each line is one request;
`A` is `python tools/agent/torirs_agent.py`.

**1. Start it and wait for the boot.**

    A start manifests/manifest_osrs242_mapeditor.ini --window 1600x900

It prints the port, the pid, the log file and `hello`. `"editor": true` there
means the panels are up.

**2. See where things are.**

    A editor

Read `panels_open`, `world_rect`, `squares_open`, and keep `console_cursor`
(say 88) for step 8.

**3. Make sure the Catalog is open.**

    A ui panel=catalog

If its `"visible"` is `false`, open it. `view.catalog` is the View menu's row
for it (F8), and it toggles, so only run it when the panel is closed:

    A action view.catalog

**4. Type into its Find box.**

    A set Find crate panel=Catalog

That is `ui_set label=Find panel=Catalog text=crate`. The reply's `text` is
what the field holds now. Without `panel=Catalog` this is refused when two
panels have a Find box, naming both.

**5. Let the list fill, and read it.**

    A wait until=idle
    A ui panel=catalog text=crate

The `vlist` widget's `rows` are the results on screen (`row`, `text`, `rect`),
`row_count` how many there are in all. To see further down, turn the wheel
over the list (`A wheel x=1436 y=190 notches=-5`) and ask again.

**6. Pick one.**

    A ui_click widget=214 row=0

(`214` being the list's `id` from step 5.) The reply's `value` is the selected
row. `A editor` now shows it as `pick`.

**7. Press RUN.**

    A action file.run

or, when the Live panel is open, its button: `A click RUN panel=Live`.

**8. Wait for it, and read what it said.**

    A wait until=idle timeout_ms=300000
    A editor
    A log since=88

`editor.run` is `done` or `stopped` (with `run_message`). `log` gives the
console lines since step 2. The build's own output is `A log source=live`.

**9. Look at it.**

    A shot C:/tmp/after_run.png

**10. Leave.**

    A action file.save_all
    A quit

The same, as a script:

```python
import sys
sys.path.insert(0, "tools/agent")
from torirs_agent import start, AgentError

agent, info = start("manifests/manifest_osrs242_mapeditor.ini", window="1600x900")
cursor = agent.editor()["console_cursor"]

catalog = agent.ui(panel="catalog")["panels"][0]
if not catalog["visible"]:
    agent.action("view.catalog")

agent.ui_set("crate", label="Find", panel="Catalog")
agent.wait(until="idle")
for panel in agent.ui(panel="catalog", text="crate")["panels"]:
    for widget in panel["widgets"]:
        for row in widget.get("rows", ()):
            print(row["row"], row["text"])

agent.action("file.run")
try:
    agent.wait(until="idle", timeout_ms=300000)
except AgentError as refusal:
    print("still busy:", refusal)
print(agent.editor()["run"])
print("\n".join(agent.log(since=cursor)["lines"]))
agent.shot("after_run.png")
```

---

## The Python client

`tools/agent/torirs_agent.py`, Python 3, standard library only.

Command line:

    python tools/agent/torirs_agent.py [--port N | --port-file F] [--timeout S] <verb> [name=value ...]

It prints the reply as JSON and exits 0, 1 when the reply is `ok: false`, or 2
for a usage or connection error. A value is JSON when it parses as JSON
(`x=120`, `visible_only=true`, `from=[10,20]`), a list of whole numbers when it
is `10,20`, and a string otherwise. `label`, `text`, `panel`, `path`, `chord`
and `id` are always strings. `mods=shift,ctrl` is a list.

Shorthands: `start <manifest>`, `click <label>`, `set <label> <text>`,
`shot <path>`, `key <chord>`, `type <text>`, `action <id>`,
`npc_click <name>`,
`select <dropdown label> <option label>`.

As a module, `Client` has one method per verb (`hello`, `help`, `wait`,
`mouse`, `drag`, `wheel`, `key`, `type`, `shot`, `ui`, `ui_click`, `ui_set`,
`log`, `quit`, `editor`, `actions`, `action`, `tile_to_screen`,
`screen_to_tile`, `world_click`, `camera`, `game`, `chat`, `npcs`, `npc_click`), and `play(wait=0)`, a
`Client` for the play client an editor's RUN started. Each returns the reply as a
dict and raises `AgentError` (its `.reply` is the reply) on `ok: false`;
`call(verb, **args)` returns the reply either way. Made of those:

| Method | What it does |
| --- | --- |
| `click(label, panel=None)` | `ui_click` by caption. |
| `select(option, widget=None, label=None, panel=None)` | Opens a dropdown and picks an option by label or index, turning the wheel over the list when the option is outside its ten rows. |
| `widgets(text=None, panel=None)` | Every on-screen widget, flat, each with its panel's title. |
| `wait_idle(timeout_ms=10000)` | `wait until=idle`. |

`start(manifest, window=None, ...)` returns `(Client, info)`.

---

## What is not built

- **The web build.** A page cannot listen on a socket; the flag is refused
  there with a sentence. Android builds the server but has no `shot`.
- **The game's own interfaces as data.** `ui` is the editor's chrome
  (`struct ToriRSChrome`, the developer overlay included). The game's
  interface tree (the inventory, the tabs, a bank) is not listed: use `shot`
  and `mouse`. `game` gives the right-click menu, chat, a dialogue's options
  and the page of dialogue showing, and nothing else of it. The plugin
  window's panel is not listed either.
- **More of the game.** No inventory, no object (loc) list, no `loc_click`
  (click a loc by point: turn the camera to it first), and no varps. The
  tree's quest driver reads those, but through the embedded server and its
  content pack, which an ordinary client build does not have.
- **Inside a drawn well.** A `graph` (the Squares picker's map, the dialogue
  graph), a `custom` well and a `modelview` are listed with their boxes and
  nothing inside them. Click in them by point.
- **Setting a checkbox or a dropdown by value.** Click a checkbox
  (`ui_click`, and read `checked` in the reply); for a dropdown click it,
  then click the option (the Python `select` does both). A slider is set by
  value with `ui_set`.
- **Scrolling the open dropdown for you.** `ui_click` on an option outside the
  list's ten rows is refused and says to turn the wheel. The Python `select`
  does that.
- **Events.** Nothing is pushed. Poll: `wait`, `log since=`, `editor`.
- **A second chord parser.** `key` takes the editor's chord spelling. A key
  the editor's key table does not name cannot be pressed.
- **Layouts other than US** for the character a key types. `type` and `ui_set`
  send the characters themselves and are not affected.
- **A password.** Loopback only, and anything local can connect.
- **`shot` of an SDL window that presents through OpenGL**, and of a D3D9
  window before Windows 8.1.
- **`action` exactly as its chord, for four actions.** It runs an action as
  the command palette does. The app's key dispatcher does a little more when
  the CHORD of one of these fires: Deselect (it first drops a drag in
  progress), Select same (it takes the loc under the pointer before the
  selected one), Level up / down (it steps from the plane under the pointer,
  not from 0), Go to tile (it prefills the tile under the pointer). For those,
  press the chord with `key`.
