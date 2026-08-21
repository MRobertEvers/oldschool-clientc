# Chrome executors and the plugin window

How `ToriRSChrome` widgets reach a screen, and how a Lua plugin gets a tab with
settings that survive a reload.

Two things are described here and they are separable: the **executor seam**,
which lets one widget model be presented in more than one place, and the
**plugin window**, which is the first surface to use it.

---

## 1. Why a seam at all

`ToriRSChrome` (`src/ui/uitree_debug_overlay.h`) is a retained widget model that
does not rasterise. It emits a flat display list of three primitive kinds --
RECT, TEXT, SPRITE -- which the existing pipeline carries to whichever backend
is live:

```
ToriRSChrome_Build  →  ToriRSChromePrim[]  →  UITree emit  →  ToriRS_Frame  →  Soft3D / GL3 / WebGL1 / D3D9
```

That list is the right altitude for a rasteriser and the wrong one for anything
native. A DOM `<input>`, a Win32 `EDIT` control or a cache interface component
cannot be reconstructed from rectangles. So the seam emits the chrome's *own*
vocabulary -- panels, tabs, widgets, properties -- and each executor maps a
checkbox onto whatever a checkbox is where it lives.

**The model stays authoritative.** An executor is a projection, never a second
copy of the truth: commands flow out, intents flow back, and an intent is
applied by mutating the model exactly as an in-canvas click would
(`ToriRSChromeIntent_Apply`). That is what keeps several presentations of one
panel agreeing, and what lets the whole thing be tested with no window at all.

## 2. Two kinds of executor

Both share `struct ToriRSChromeExec`; which entries an implementation fills says
which kind it is.

| | Surface executor | Native-widget executor |
|---|---|---|
| What it does | Puts the chrome's own rasterised output somewhere other than the game canvas | Rebuilds the model out of foreign controls |
| Fills | `present`, `surface_input`, `is_surface` | `apply`, `poll` |
| Uses the command stream | No | Yes -- it is the whole contract |
| Widgets are | ToriRSChrome's, drawn by ToriRSChrome | The platform's own |
| Examples | `buffer` (in-canvas), `sdl` (second OS window) | `web` (DOM), `gdi` (comctl32), `cs2` (game interfaces) |

One table rather than two, because a host drives them identically: bring it up,
hand it this frame, take back what the user did, tear it down. Making the
difference two interfaces would put the choice in the host, which is exactly
where it does not belong.

### Deltas, not declarations

`ToriRSChromeSync_Run` diffs the model against a shadow of what *this* executor
was last told, and emits only what changed. Re-declaring a panel whenever
anything in it moved would be far simpler and would also destroy and recreate
native controls on every keystroke -- losing focus, losing the caret, making a
text field impossible to type into. The shadow is the price of native controls
that survive their own updates.

Three orderings are load-bearing and all three are tested:

- **Panel closes before panel opens.** An executor may assume a widget's panel
  exists when it hears about the widget.
- **Widget removals before widget additions.** A freed handle can be recycled by
  an add in the same frame; an executor told `add 7` before `remove 7` would end
  up with neither.
- **Widgets are announced in ROW order, not handle order.** The sync walks each
  panel's own row list. Array order is *allocation* order, and the free list
  makes the two diverge the moment a panel is cleared and rebuilt -- an
  executor creating controls in arrival order would then lay the window out in
  the order rows were first created. The symptom was a Save button above the
  settings it commits.

### A handle is not an identity

`ToriRSChromeWidget::serial` is. Handles come off a free list, so handle 5 removed
and handle 5 added are two different widgets wearing one number, and a shadow
that diffed on `(handle, kind, panel)` concluded "nothing changed" for a row
that had in fact been replaced. The serial is monotonic, never reused, and never
reset by a panel clear -- it only ever has to be comparable and distinct.

### Strings are copied at the seam

The chrome deliberately borrows -- prim text points into its widget, a
dropdown's options point at the caller's array. Those lifetimes are fine inside
one process and wrong the moment a command is queued, posted to a browser tab,
or written to a recording. Every string entering a command is copied into it.

## 3. Choosing one

`[chrome] executor=buffer|sdl|web|gdi|cs2` in the boot manifest, overridden by
`TORIRS_CHROME_EXECUTOR` -- the same precedence `TORIRS_CHROME_THEME` has over
the theme beside it, so a lane can ship a default a developer steps past without
editing it.

**With no key and no env var**, the gameframe picks: a gameframe that mounts the
popout strip (interface 728, the column with XP Tracker and Loot Tracker) gets
`cs2`, so the plugin window arrives as a fourth tracker rather than as a panel
floating over the game; every other gameframe gets `buffer`.

That is a default and nothing more. Naming an executor -- in the manifest or in
the environment -- wins over it, including when the name is what would have been
chosen anyway. This needs the *presence* of the key tracked separately from its
value (`BootManifest::chrome_executor_set`), because `buffer` is both the zero
value and a legitimate answer; without that flag the strip's preference read as
unconditional and `[chrome] executor=` was inert on every lane that has a strip.
The client prints what it bound and why on the frame the window first opens:

```
chrome: plugin window executor = sdl (configured)
chrome: plugin window executor = cs2 (default)
```

A manifest naming something that is not an executor at all is a **load-time
error**, next to the line that asked. A name this *build* has no executor for is
not: every lane carries a different set, and the chooser answers that with the
in-canvas one and a message.

The shell chooses, because choosing needs the platform handle and `struct App`
is deliberately platform-free (`App_Render` is handed a pixel buffer rather than
a window for the same reason). What crosses into the App is a **vtable, not a
started executor** -- `App_SetPluginChromeExec` -- and it is brought up the
first time the plugin window is opened. A session that never opens it never
opens a second OS window either.

Failure is not an error at any stage:

| Situation | Result |
|---|---|
| Name is not an executor | message, then `buffer` |
| Executor is not compiled into this lane | message, then `buffer` |
| `begin()` refuses (no display, blocked popup, missing control library) | message, then `buffer` |

`buffer` is what every build has, so the fallback is always available. Only the
**plugin window** is bound to an executor; the developer overlay, the loc editor
and the map editor stay in-canvas, because they are in-canvas tools and giving
them a choice would be four more consumers to keep working for nobody.

## 4. What is implemented

| Executor | Kind | Lanes | State |
|---|---|---|---|
| `buffer` | surface | every | **Done.** The in-canvas prim path; the default and the fallback everywhere. |
| `sdl` | surface | macOS, Linux | **Done.** One auxiliary OS window. See COMMON-CHROME-001. |
| `web` | native-widget | web | **Done.** Real DOM controls, built by the page from the command stream. |
| `gdi` | native-widget | win32, win64 | **Written, not run.** Compiles only on Windows; parsed here by `make -C src check-gdi-syntax`. |
| `cs2` | native-widget | every | **Done.** The window as game interface components. |

### What each native-widget executor does

**All three wear the same art.** `web`, `gdi` and `cs2` draw their controls out
of one bake -- `engine/torirs_chrome_skin_baked.c` -- on one set of metrics,
`ui/torirs_chrome_metrics.h`. What differs is only how a platform is *told*: the
CS2 executor is handed a scene id, the page is handed base64 RGBA it turns into
data: URLs, and Windows gets a DIB it composites in software. The semantic slot
enum and the bake's own order are the same numbers, which all of them rely on
and none of them used to check; `ui/torirs_chrome_skin.h` is the static
assertion that they agree, and the page's copy of the same table is pinned by
`web/test/chrome_enum_sync_test.js`.

- **web** (`src/ui/torirs_chrome_exec_web.c` + `src/web/torirs_chrome.js`) --
  every crossing is an `EM_JS` call onto a `window.torirsChrome*` hook,
  following the existing `web_editor_open_panel_tab` pattern: C asks, the page
  owns the DOM. Commands go out as JSON (not the channel's packed frames --
  this direction has no need to be handed to a wasm bus, the volume is a
  handful per tab build, and a page you can debug by reading its console beats
  one whose messages have to be unpacked first). Intents come back the same
  way. The hooks' *presence* is the availability test, so a cached `index.html`
  without them degrades to in-canvas chrome instead of a window that silently
  does nothing.

  **The art crosses too, once, at open.** `torirsChromeSkinMetrics` carries the
  numbers out of `torirs_chrome_metrics.h`; `torirsChromeSkinSprite` carries
  each baked image as base64 RGBA, which the page turns into a data: URL
  through an ImageData and a canvas. Raw pixels rather than PNG, because a PNG
  encoder in C would exist for this alone and the platform already has a
  decoder. About 70KB, once, and nothing per frame.

  The page builds the panel out of it: tradebacking behind the window and every
  field, the interfaces' tick and cross as an `appearance:none` checkbox's
  background, the scrollbar's own down arrow on a `<select>`,
  `::-webkit-scrollbar` wearing the bar's track and grip, and the nine-slice
  frame as a `border-image` -- composed from the eight pieces into one 9x9,
  because `border-image` takes a single source. Every rule is an OVERRIDE of a
  flat one, scoped to `.skinned`, and the class goes on only when every sprite
  the sheet names has arrived: a nine-slice frame around a flat black panel
  reads as a fault, where a complete flat window reads as a theme.

  The shuffle from the bake's `0xAARRGGBB` words to ImageData's R,G,B,A bytes
  happens in C, and `make -C src test-chrome-web-skin` is what keeps it honest
  -- it decodes the real bake back and asserts the ON slot is a *green* tick and
  the OFF slot a *red* cross. A red/blue swap decodes, blits and looks
  deliberate; the hue is the only thing that catches it.

  **It sits in an iframe beside the canvas, exactly as tall.** Not a corner
  overlay -- which is what it was, and it covered the part of the frame the
  player was looking at. The plugin window is read *while* the game is played,
  so it belongs next to the picture; every executor with a real window of its
  own had already answered this the other way. An `<iframe>` rather than a
  `<div>` for the same reason those get a window: the chrome is then in a
  document of its own, so the host page's stylesheet cannot reach its controls
  and typing into a settings field cannot reach the canvas's key listeners and
  walk the player. The iframe is created with **no `src`** -- an iframe left
  without one keeps the about:blank document it is born with, and writing into
  that is synchronous, where `src="about:blank"` navigates it and replaces
  what you built a tick later. The height is the canvas's, tracked with a
  `ResizeObserver`, so the page's scaled modes, the browser window and the
  game's own Display setting all move it. So is the **top edge**: the scaled
  modes centre the letterboxed canvas inside its stage, so the picture starts
  below the row the window is in, and matching only the height left the window
  hanging above it. It is corrected as a delta off where the frame currently
  measures, which keeps the parent's padding and alignment out of the sum, and
  the canvas's *container* is observed as well -- growing the stage in the
  dimension the fit is not limited by moves the canvas without resizing it.
  The frame's width is published to the page as `--torirs-dock-width` (`0px`
  when nothing is docked), which is how `index.html` keeps its full-canvas
  corner toggles off the window's title bar without knowing anything else
  about it. A page with no `#canvas` falls back to the old corner overlay.

  **A pop-out mark moves it into a tab of its own**, and moves it back. The
  tab is about:blank built by this same page, not a URL with a script of its
  own: the widget state lives in the client and the intent queue lives in the
  page's host object, both one same-origin property access from the popped-out
  document, where a second page would need the channel, a HELLO and a second
  copy of every branch in `makeWidget`. The nodes are **adopted, not rebuilt**
  (`adoptNode`), because the seam emits deltas -- a rebuilt page would sit
  empty until something in the model happened to change -- and adopting keeps
  the listeners, values and caret. Where a presentation puts its pixels is its
  own business, so none of this reaches the model, exactly as the SDL window's
  position never does. Closing the popped-out tab is reported as a `CLOSE`,
  like the SDL window's X.

  **Its close mark was dead for a release**, and the cause is worth keeping:
  Ok and Close name a panel, and the page latched which panel that was when a
  `TABSTRIP` arrived. The plugin window is *paged*, not tabbed, so no strip
  ever arrives: both buttons addressed panel `-1`, `PanelSetVisible` validated
  that away, and every layer reported success. It is latched on `PANEL_OPEN`
  now -- the one command every window gets -- and `ToriRSChromeIntent_Apply`
  answers a close naming a panel the model does not have with **0** rather
  than the 1 it used to give unasked, so a drain that changed nothing can no
  longer read as one that worked. The page's own test grew a window with no
  tab strip to catch it; the fixture that hid it was one that always built a
  strip first.

- **gdi** (`src/ui/torirs_chrome_exec_gdi.c`) -- an owned
  `WS_EX_TOOLWINDOW` whose children are USER32 controls: `BUTTON` with
  `BS_AUTOCHECKBOX`, `EDIT`, `COMBOBOX`, `STATIC`. **No comctl32**: a real
  `WC_TABCONTROL` would mean linking it, shipping a v6 common-controls
  manifest, and adding both to the lane's import audit, where a row of
  `BS_PUSHBUTTON`s is the same affordance with none of that. The XP lane's
  one-file artifact contract (`WINXP-ABI-001`) is what makes that trade worth
  taking.

- **cs2** (`src/ui/torirs_chrome_exec_cs2.c`) -- the window as real interface
  components, built through the same `UITree_PushBuildComponent` any
  programmatic panel uses and clicked through the same hit test as any
  cache-authored interface. It needs no platform support at all, which is why
  it is the one executor available on every lane.

  Three things about it are worth knowing before touching it, because each was
  a bug first:

  1. **Component ids come from group `0x7FFE`**, not a private high range. A
     root in any other unmounted group is deliberately dropped by
     `UITree_RootIsDisplayable` -- that filter exists so a CS2 script
     auto-mounting an interface for property access cannot cover the gameframe
     -- so ids picked for being far away render *nothing*. `0x7FFE` is the
     tree's own "app-overlay chrome" group.
  2. **It mounts into the gameframe's popout strip**, `popout:container`
     (728:9) -- the slot XP Tracker, Loot Tools and Hiscores mount into. Those
     panels are authored pure fill-parent with no chrome of their own (the
     nine-slice frame is a sibling under `popout:frame`), so building there
     inherits the strip's frame, sizing and collapse behaviour and reads as a
     fourth tracker. Mounting as its own root (`-1`) was the first attempt and
     is what a gameframe with no strip would need; it is not what this does.
  3. **The tree owns its text.** `UITree_PushBuildComponent` strdups the
     string and node teardown frees it, so assigning a pointer of your own into
     `u.rs_text.text` afterwards is a heap corruption on the next rebuild, not
     a leak. Hand strings to the *builder*.
  4. **Its art is baked in, not loaded.** The furniture -- tradebacking behind
     panels and fields, the six pieces `~script31` builds a scrollbar out of,
     and the nine-slice panel frame -- comes from
     `engine/torirs_chrome_skin_baked.c`, the same bake the in-canvas chrome
     draws, handed over as one multi-frame scene sprite whose frames are
     `enum ToriRSChromeSkinSlot`. So does the wrench on the strip button.
     Components reach it through `UIBuildComponent::graphic_scene_id` +
     `graphic_atlas_index`, which bypass the sprite resolver entirely.

     It used to ask the cache for archives 297/773/788/792/789/790/791 and 785,
     and that was wrong three ways: nothing could be drawn until the loads
     landed, so the panel opened as flat boxes and rebuilt when each piece
     arrived; those ids name unrelated images on any cache but the OSRS one
     they were chosen from, so a different lane got a confidently wrong
     picture; and the launcher button could not be built at all until its icon
     resolved. None of those failure modes exist now -- the images are `.rdata`
     and the only question left is whether this *build* baked a skin.

  **Its metrics are not its own either.** The row grid, the label column, the
  control sizes and the palette all come from `ui/torirs_chrome_metrics.h`,
  which the in-canvas chrome reads too -- see §8.0 of
  `src/ui/README_DEBUG_OVERLAY.md`. Two files implementing one picture from two
  sets of numbers is exactly how the panel came to look different depending on
  which executor was bound to it, so there is now one set. A number that
  belongs only to this presentation (the tab caption's approximated advance,
  the component-id blocks) still lives here.

  **The frame is the strip's, or its own.** Mounted in `popout:container` the
  panel draws no border at all -- `popout:frame` already drew one, and a second
  inside the first is a box in a box. Standalone it draws the nine-slice itself,
  which is the same border `ToriRSChrome_PanelSetFramed` puts on the in-canvas
  window, so the two presentations are the same window either way.

  The scrollbar is drawn the way `~script31` assembles one -- track, grip,
  arrow sprites -- but only the ARROWS take a click, because a grip drag is a
  press held across frames and this toolkit has no drag. The grip is still
  positioned from the scroll offset, so the bar reads and moves like the
  game's.

  **Editing works, by routing the keyboard and nothing else.** A click on a
  field arrives as a component click and becomes an ACTIVATE intent, which the
  intent layer turns into FOCUS on the model's own text input; the host then
  routes key events (and only key events) into that model, so typing lands in
  the focused field and mirrors back per keystroke. The MOUSE is deliberately
  not routed there: the in-canvas window still lays out and hit-tests at its
  floating position even though nothing draws it, so mouse routing would
  deliver every click to that ghost as well. The field shows no caret -- the
  text component has none -- but it takes and shows typing.

  Dropdowns **cycle** on click rather than opening a list: a popup is an open
  state held across frames plus its own hit test, and stepping to the next
  option is the whole affordance a declared enum needs.

  The focused field is **outlined** in the accent, from `WIDGET_FOCUS`. Before
  that command existed the rows took typing perfectly well and gave no sign of
  it, so they read as read-only -- which is exactly how they were reported.

  A colour row builds its **axis bars as components** when the model says its
  picker is open: three rows of 32 cells each, in a block of ids of their own.
  Thirty-two rather than one per value, because 64 hues plus 128 lightnesses
  would be two hundred nodes in the interface tree for one open popup; the cell
  a click lands in maps back onto the axis's full range, so every value is
  still reachable and only the pointer is coarser.

## 5. The SDL executor

A *surface* executor: the widgets are still ToriRSChrome's, laid out by
ToriRSChrome and rasterised by the same software path that draws them in the
canvas. Only the destination and the pointer's origin change -- which is why it
is pixel-identical to the panel it replaces rather than a second look.

The platform API under it (`PlatformSDL2_Aux*`) is deliberately the smallest one
that serves exactly this: open, close, a pixel buffer, resize, present, an input
drain and a close request. It is the first thing in the tree to want a second OS
window, and it is refusable, which is what keeps it from becoming a general
multi-window layer that every backend has to answer for.

Rasterising is **lent by the App** (`App_ChromeRasterise`), not reimplemented:
drawing needs the scene the baked fonts and chrome skin were registered in, the
frame translator and a software backend, and a second rasteriser here would be a
second set of rounding, a second baseline convention, and a second place for the
chrome to be almost right.

Input is routed by SDL **window id**, not by "is the pointer over it": a drag
that started in one window keeps delivering to that window, which is what makes
a grip drag work when the cursor leaves the frame -- and what would otherwise
let a click in the plugin window also walk the player.

### A window of its own is filled, not floated in

A panel's authored box -- (8,72), 320 wide -- is a box that floats over the
**game canvas**, which is what it has to float over. Put the same box in a
window that holds nothing else and it is a window inside a window: three bands
of empty background around it, and dragging the frame wider grows the
background rather than the settings.

So a presentation whose surface is a window of its own gets the panel stretched
over the whole of it, every frame, tracking the OS window as the user drags it.
The rule is `ToriRSChromeSync_FillSurface`'s, in the seam -- not the SDL
executor's -- and an executor opts into it by answering `surface_size`. That
answer *is* the declaration: a flag beside a getter would be two things that
can disagree, and this is the table where which entries an implementation fills
already says which kind it is. The buffer executor shares the canvas with the
game and has neither, so in-canvas chrome goes on floating; a native-widget
executor lays its own controls out and never sees this at all.

Filling takes the panel's **drag and grip** with it (`ToriRSChromePanel::filled`).
Both write geometry that the next fill overwrites, so leaving them is a title
bar that takes the cursor and gives nothing back, and a corner that snaps.
The OS window's own frame is what moves and resizes it now.

`test-uitree` pins it through the recorder, which grows a window when a test
sets `surface_w`/`surface_h` -- so the fill, the resize, the two dead
affordances and the in-canvas panel that must NOT be touched are all checked on
a machine with no display.

### No OS frame: dragging the window by the chrome in it

`[chrome] borderless = 1` (or `TORIRS_CHROME_BORDERLESS=1`) opens the window
with no OS frame at all, and the panel's own title bar and the empty tail of its
tab strip move it instead. Off by default: a frameless window is a look, and one
a lane has to ask for.

Taking the frame off takes four things with it -- the title bar that moved the
window, the border that resized it, the buttons that closed and minimised it,
and the double-click that zoomed it. Two of those come back through SDL's window
hit test (`SDL_SetWindowHitTest`), which asks us, per point, what that pixel is:

- a band `SDL_BORDERLESS_RESIZE` points deep along each edge resizes, corners
  first. **Tested before the drag handles, and it has to be:** the strip runs to
  the top edge, and a window whose top edge drags instead of resizing can never
  be made shorter from the top again;
- anything the chrome published as a handle drags;
- everything else is an ordinary press.

Closing comes back through the panel's own Close button, which it already had.

**A draggable region swallows the press that starts the drag.** The application
is never told about a mouse-down inside one, so every control drawn inside a
handle has to be punched back out of it or it silently stops being clickable.
That is what `struct ToriRSChromeDragRegion` is: handles, minus holes. The holes
are the tab run, a closable panel's Ok and Close, and any dropdown list or
colour picker floating over the strip while it is open.

The tab run is **one** hole rather than one per tab, because tabs are laid out
contiguously from the strip's left edge -- their union is the run, and the tail
behind it is the only part of the strip that is not already a control. A strip
whose tabs have been compressed to fill its width therefore offers no handle at
all, correctly. The title bar is the handle that is always there.

Why a **published region** rather than a callback into the model: SDL calls the
hit test from inside its event pump, while the window manager is deciding what a
press even is, and the model is the frame thread's. A callback that walked
panels and widgets would be reading a tree the frame it interrupted is halfway
through rebuilding. So the host publishes a dozen rectangles once a frame --
`ToriRSChromeSync_PublishDragRegion`, **after** Build, unlike `FillSurface`
before it, because these are laid-out boxes and publishing them a frame early is
a drag band sitting where the panel used to be. An empty region is published
too: an executor that simply stopped hearing about handles would go on offering
the last set it was told.

If the video driver has no hit test, `PlatformSDL2_AuxSetBorderless` **refuses
and keeps the frame**, with a line on stderr saying why. Every way a user has of
moving or resizing a window runs through the frame or the hit test; a window
with neither is pinned where it opened, at the size it opened, for the rest of
the session -- a worse answer than the frame it was asked to hide. `dummy`
refuses; Cocoa, Windows and X11 do not.

The same platform calls exist for the **game** window
(`PlatformSDL2_SetBorderless`, `PlatformSDL2_SetDragHandleProvider`, which takes
canvas coordinates with the letterbox already undone). Nothing wires them: the
game window has no drawn top bar to grab, and hiding its frame without one would
leave it movable only from its resize edges.

## 5a. Opening it: the sidebar Plugin button

A client-built component that toggles the window, sitting with the gameframe's
own sidebar furniture. Also reachable by the `plugin_panel_toggle` debug action.

It is **not** a sidebar tab, and that is a finding rather than a shortcut. A tab
would be a `tab_icon` + `redstone_tab` + `sidebar` triple in a layout INI --
which is how the rev-245 lane declares its tabs, and which this lane has none
of: its gameframe is a *cache* interface (`interface_id=161`), so the tab strip
on screen is cache components with cache hooks and there is no INI triple to add
one to. Declaring a builtin `tab_icon` anyway would render, and then route its
click through `SET_SELECTED_TAB` -- selecting a tabno the gameframe does not
have, which blanks its sidebar.

So the button is built the way the CS2 executor builds its panel: a component in
the app-overlay chrome group, its own root, clicked through the same
interception. That works on every executor and every revision, needs no cache
sprite, and leaves the game's tab machinery alone.

Two things about its placement were bugs first:

- **It must wait for the gameframe.** The plugin tick runs before the BOOTING
  early-out -- deliberately, so the window is usable while a cache loads --
  which means the first frames see an *empty* tree. A button built there becomes
  component 0 and is wiped by the first real tree build.
- **Position comes from the canvas, not the layout root.** They are different
  spaces: on this lane the layout root is 1224 wide while the canvas is 765, so
  a button placed from the root sits four hundred pixels off the right edge.

### Closing it takes the presentation down with it

Opening the window **binds** an executor and closing it **unbinds** one. The
two are the same lever, and for a while only one end of it was wired: Close hid
the panel, the executor went on being driven for a panel with nothing in it,
and the plugin window stayed on screen -- empty -- while the client insisted it
was closed. The next press of the toggle then spent itself re-closing something
the user had already closed.

The chain is worth stating because no link in it is obviously the owner:

1. A close arrives. From the panel's own close box (any presentation), from
   the window's title bar (SDL, GDI), from a popped-out tab being closed
   (web), or from the toggle.
2. It lands on the **model**: `CLOSE` and `CONFIRM` both end at
   `PanelSetVisible(0)`, which is what keeps five presentations of one panel
   from disagreeing about whether it is up.
3. The host reconciles its own flag against the model each frame -- the model
   is the only thing every presentation shares, so it is the only place the
   answer can be read -- and calls `app_plugin_window_set_open(app, 0)`.
4. That calls `ToriRSChromeSync_Shutdown`, and the executor's own `end()`
   decides what closing means: SDL destroys its aux window, GDI its HWND, the
   web one calls the page's close hook, the CS2 one clears the nodes it built.

The host knows none of those four things, which is the point. The next open
re-binds from scratch, so nothing carries a shadow of a window nobody can see
-- and a window taken down by *its* side is openable again, because `live` is
the guard the bind reads.

A presentation can also lose its window from its own side, and each says so in
the model's vocabulary rather than going quiet: the web page reports a
`CLOSE` when the popped-out tab is gone (polled in `takeIntent`, because the
client already calls that once a frame and `unload` fires on a reload too), and
the SDL executor reports its title-bar X.

The SDL one **also drops the window at once**.
That is a deliberate departure from the GDI rule ("report it, and let the model
decide"): a Win32 window is still there to wait in, an aux window whose
`SDL_Window` is gone is not, and presenting into it would be drawing into a
window that no longer exists. It is the only intent a surface executor sends --
every other gesture goes back raw through `surface_input` for the chrome to hit
test itself -- and it needs the panel handle, which is the one thing that file
reads out of the command stream (`PANEL_OPEN`).

## 6. The plugin window

One window, **paged**. The roster lists every plugin as a row carrying its
name, its last fault, a switch and -- when it has anything to configure -- a
way into its own page; that page holds its settings and whatever controls it
declared, under a Back button. That is the sandbox rule made concrete: plugins
share ONE extra window, and `api->win_request` claims a **page** in it rather
than a window of its own.

### Pages, not a tab per plugin

This was a tab strip first, and a strip is the wrong shape for a roster: it
lays its destinations out across one row, so eight plugins already compress
their captions past reading and the ninth has nowhere to go. RuneLite's plugin
panel answers the same problem with a scrolling list of rows -- each with its
own switch -- and a drill-down into the one you asked about, so that is the
navigation this borrows. What it does **not** borrow is the look: the rows are
drawn in the game's own chrome by whichever executor is live.

The row is its own widget kind (`TORIRS_CHROME_W_LISTROW`) rather than a checkbox
with extras, because it has three zones and **two** outcomes -- flip the
switch, or open the page -- and the model has to be able to say which one
fired. It does that with `ToriRSChrome_ActivationWasAction` beside the ordinary
activation latch, and `TORIRS_CHROME_INTENT_ACTION` carries the same
distinction back from a native-widget executor. Putting the row in the MODEL is
what gets every executor the same list, and the page state is the host's, so
nothing below the seam knows a page changed.

A row whose plugin has nothing to configure gets no affordance, and then its
whole width toggles -- no zone is ever inert. "Nothing to configure" means no
LABELLED config key and no declared control; an unlabelled key is state the
plugin persists for itself, not something to hand-edit.

It is its own `ToriRSChrome` instance rather than a panel in the developer
chrome, because it is the one piece of chrome a *player* uses: it must be
openable beside the game without the editors' claim on the keyboard, it needs
capacity for every plugin's rows at once, and it is the surface an executor is bound to
while the developer chrome stays in-canvas. The old objection -- "a second
chrome is a second of all the focus, damage and scale handling" -- stopped
applying once input routing became one shared call taking the instance
(`app_chrome_route_input`).

Toggle with the `plugin_panel_toggle` debug action (`p` in
`manifests/manifest_osrs239_torirs.ini`).

### Two kinds of row, on purpose

- **Settings**, generated from the plugin's declared config schema, are
  **staged**. Typing changes nothing until Save. *The chrome is the staging
  buffer* -- a retained widget already holds exactly "what the user typed and
  has not committed", so no third copy of a pending edit exists anywhere.
- **Controls the plugin declared itself** are dispatched the moment they are
  used, because a plugin's own button is an action rather than a setting.

### Dropdowns need storage neither side owns

A config schema's `choices` and a plugin's `win_set_options` are both one
`"a|b|c"` string owned by the plugin. The chrome's dropdown *borrows* a
`char const* const*` whose strings must outlive the widget. Neither shape fits,
which is why both used to render as text fields with the choices printed in the
label.

The window now splits them into a pool on `struct App`, reset once per panel
rebuild so the slices live exactly as long as the rows pointing into them. A
declared enum is then a real dropdown -- which matters beyond looks: a text
field that accepts anything for a key that accepts three things is a typo
waiting to be saved.

Save reads a dropdown's chosen **option**, not its text field; a dropdown's text
is empty, and reading it wrote every enum key blank.

### Colours are picked on the game's own axes

A declared `CFG_COLOR` key is an **HSL16 picker** -- a swatch, a typeable hex,
and a popup of three bars that are the palette's own axes: 64 hues, 8
saturations, 128 lightnesses.

Those axes rather than RGB, because a model face is not RGB. It is a 6-bit
hue / 3-bit saturation / 7-bit lightness index into the revision's palette
(which is why the plugin api has `hsl_from_rgb` at all), so a 24-bit picker
would be lying about its own precision: two hexes the user can tell apart in
the field land on one entry on screen. Picking on the axes makes every
reachable value one the renderer can produce, and puts the quantisation where
the user can see it -- the field shows the entry the colour landed on, not the
colour that was asked for.

The hex stays editable, because a colour usually arrives as a hex out of a wiki
page or another client. Typing one commits on Enter or on blur, never per
keystroke: half a hex is not a colour, and a swatch flickering through six
wrong ones while the right one is typed reads as a fault.

**The store round trip has to hold still, and with the reference quantiser it
did not.** `hsl_from_rgb` is the function Jagex's toolchain used to turn
authored art into palette indices; it is not an inverse of the palette and
cannot be made into one (the palette puts hue *h* at `(h + 0.5) / 64` and it
recovers hue with `ceil(hue * 64) % 63`). Measured: 63813 of the 65536 entries
fail to survive one `hsl -> rgb -> hsl` trip. Since Save writes the hex and the
next open reads it back, a marker colour drifted a shade per session. The
picker therefore uses `ToriRSChrome_Hsl16NearestRgb`, an exact nearest-entry
search -- an entry matching at distance zero always exists, so the trip is
stable by construction. Both conversions are kept, and
`test-debug-overlay-visual` pins the difference so a future simplification back
onto one of them fails there rather than in a settings panel months later.

Each executor maps the widget onto its own idiom, the way it already does for
dropdowns:

| | How a colour row appears |
|---|---|
| `buffer` / `sdl` | The swatch, the hex, and the three bars; a press-sweep-release along a bar moves that axis, and the wheel steps it one value |
| `cs2` | The swatch, the hex, and the bars as components -- see §4 |
| `web` | `<input type="color">` beside the hex. The browser's picker is 24-bit; what comes back is a TEXT intent the MODEL quantises, so the swatch visibly snaps to a palette entry |
| `gdi` | A coloured `STATIC` beside the `EDIT`. Not `ChooseColor`: it is modal, and an executor may not block the frame loop |

The seam carries no new command for any of it. The value rides
`WIDGET_SELECTED` (a colour *is* a selection out of a palette), the hex rides
`WIDGET_TEXT`, and "the axis popup is open" rides `WIDGET_CHECKED` -- which is
what lets a native-widget executor draw its own bars without being told in a
vocabulary of its own. The two zones of the row are told apart by *which*
intent arrives: `ACTIVATE` is the swatch, `ACTION` is the field. A coordinate
would have made every executor carry the row's geometry, which is the thing the
seam exists to avoid.

### The plugin API (ABI 5)

```lua
function M.on_ui_build(api)          -- raised whenever the tab is EMPTY
  api.window.request("Beam Demo")
  api.window.widget("checkbox", "live", "live preview")
  api.window.widget("input", "note", "note")
  api.window.widget("button", "reset", "Reset counter")
  api.window.set_checked("live", true)
end

function M.on_ui(api, ev)            -- ev.widget, ev.action, ev.value, ev.on, ev.text
  if ev.widget == "reset" then ... end
end
```

Controls are named by plugin-scoped **string ids**, not handles, because a
reload rebuilds the tab from nothing: an id survives that and a handle does not.

`on_ui_build` rather than `on_start` is where a tab is declared, and the host
re-raises it whenever the tab is empty -- after a reload, after a re-enable,
when the window is first opened. One declaration site covers every way the tab
can come back. A plugin that built its tab only in `on_start` would come back
from a reload with a blank one.

`PluginHost_WinBuild` is called for **every** plugin, not only those that
already have a tab. "Already has a tab" is a deadlock: claiming the tab is the
first thing a plugin does inside the build handler.

### Chrome is not game content, and the game had to be told

Two ways the CS2 window leaked into machinery that has no business seeing it,
both reported as "the inputs just say Continue and cannot be edited":

- **The minimenu and the mouseover text.** The window's rows are real interface
  components, armed for clicks so the executor hears about them -- and that
  arming is exactly what `RS_Minimenu_Build` reads. So every field grew a
  right-click menu offering "Continue" (the generic verb the reference gives a
  component a script enabled), and the mouseover text said "Continue" with the
  pointer anywhere over the panel. `add_component_rows` now returns nothing for
  the chrome group, which covers the right-click menu, the left-click default
  row and the mouseover text in one test, because all three are that one build.

- **The keyboard.** A chrome field under the caret is the model's, and the host
  routes keys into it long before the game's own key handling runs -- but
  nothing downstream knew. So typing a colour into a plugin's field ALSO ran
  every armed `onKey` script and typed the same characters into the chat line,
  and an Enter meant to commit the field sent whatever was in the chat box.
  `app_text_input_focused` had the same hole: it named `dbg_ui` and not
  `plugin_ui`, so a plugin field's keystrokes fired debug hotkeys too. Both now
  go through `app_chrome_holds_keyboard`, which names both instances.

The second of these is why the fields *looked* uneditable: they were not. They
took typing and showed it, with no caret, no focus ring, and the same
keystrokes going into the chat line underneath. See `WIDGET_FOCUS` in §4.

## 7. Save and reload

Save writes the staged settings and then **reloads the plugin**. The reload is
the point rather than a courtesy: a plugin reads its config in `on_start` and
caches what it found, so writing a key underneath a running plugin leaves it
running on the old value with the panel showing the new one.

```
Save  →  ConfigSet per staged row  →  PluginHost_Reload
             │                            │
             │                            ├── teardown: EV_STOP, shutdown, drop subs,
             │                            │            world objects, assets, window tab
             │                            ├── def->reload(ctx)      ← the adapter rebuilds
             │                            ├── re-seed schema, PRESERVING saved values
             │                            └── Start → EV_START      ← on_start sees the new config
             └── prefs file written by the existing settle-delayed task
```

Two properties are tested and both matter:

- **Saved values survive the reload.** A reload that re-seeded defaults over the
  store would make Save a button that resets the plugin.
- **Keys the reloaded source newly declares arrive with their defaults**, so a
  script that gained a setting comes back with it populated.

`def->reload` exists because a scripted plugin's reload is something only its
adapter can do: the host can drop subscriptions and call `init` again, but
`init` for a Lua script re-subscribes the *same* function references, so the
file's text is never re-executed and the reload changes nothing. The Lua adapter
keeps each script's source on the C heap (the per-script Lua arena is exactly
what a reload throws away) and rebuilds the VM from it through the same
`lua_script_build` path a first load takes -- anything a reload did differently
would be a difference that only shows up after a reload.

A disabled plugin is left alone: it is already torn down, and reloading it would
switch it back on behind the user's back.

## 8. Testing

| Target | Covers |
|---|---|
| `make -C src test-uitree` | Chrome model (tabs, buttons, scrolling, widget removal) and the executor seam (deltas, ordering, intents, refusal) |
| `make -C src test-debug-overlay-visual` | What the chrome looks like, as BMPs into `src/build/` -- including a tabbed, scrolling panel |
| `make -C src test-plugin-host` | The window registry, dispatch, and reload, against a fake engine with no window at all |
| `make -C src test-web-channel` | The web executor's DOM half, driven against a fake document -- node only, no browser |
| `make -C src check-gdi-syntax` | That `torirs_chrome_exec_gdi.c` still parses, on a machine with no Windows SDK |

End-to-end, headlessly:

```sh
TORIRSSERVER_ALLOW_STALE_SCRIPTS=1 \
TORIRS_PLUGIN_MANIFEST=plugins/_windemo.ini TORIRS_PLUGIN_PREFS= \
TORIRS_SIM_HOTKEY="150,p" TORIRS_SIM_CLICK_AT="200,110,100" \
TORIRS_EXIT_BMP=/tmp/win.bmp TORIRS_MAX_FRAMES=280 \
./src/torirs --soft3d --manifest manifests/manifest_osrs239_torirs.ini
```

Add `TORIRS_CHROME_EXECUTOR=sdl TORIRS_CHROME_DEBUG=1` and
`TORIRS_SIM_HOTKEY="150,p;220,p;280,p"` to watch the window open, close and
open again: the debug line reports each `set_open`, and the aux window's own
lifetime has to follow it. That toggle is the same host path a click on Close
takes, which is why it is the one worth simulating -- an aux window's own
pointer cannot be simulated at all.

Add `TORIRS_CHROME_BORDERLESS=1` to open that window with no OS frame. Under
`SDL_VIDEODRIVER=dummy` it prints the refusal (`no window hit test`) and keeps
the frame, which is the branch worth having in a headless run; on a real driver
it prints `plugin window has no OS frame`. What cannot be simulated either way
is the pointer inside that window, so the handles and holes themselves are
pinned in `test-uitree` against the model instead --
`test_chrome_exec_drag_region`, which asserts both halves of every box: this one
drags the window, and that one still reaches the control drawn on it.

`script/plugins/_windemo.lua` is a probe plugin that exists only to exercise the
window API -- it draws nothing and touches no world state, so what it proves is
the surface the executors have to carry and nothing else.

To A/B an executor against the canvas, run the same command with
`TORIRS_CHROME_EXECUTOR=buffer` and `=sdl` and sample canvas pixels where the
in-canvas panel would be: panel body under `buffer`, world under `sdl`. That is
the verification COMMON-CHROME-001 asks for.

## 9. Where things live

| Path | What |
|---|---|
| `src/ui/uitree_debug_overlay.{h,c}` | The chrome: model, layout, display list, input |
| `src/ui/torirs_chrome_exec.{h,c}` | The seam: commands, intents, sync, buffer + recorder executors, the chooser |
| `src/ui/torirs_chrome_mirror.{h,c}` | Handle→native map, row order and the intent queue -- shared by all three native-widget executors |
| `src/ui/torirs_chrome_exec_sdl.c` | The SDL surface executor |
| `src/ui/torirs_chrome_exec_web.c` | The web executor's C half |
| `src/web/torirs_chrome.js` | The web executor's DOM half |
| `src/ui/torirs_chrome_exec_gdi.c` | The Win32 executor |
| `src/ui/torirs_chrome_exec_cs2.c` | The CS2 executor |
| `src/platform/platform_sdl2.{h,c}` | `PlatformSDL2_Aux*` -- the auxiliary window; `*_SetBorderless` / `*_SetDragHandleProvider` -- the frameless one |
| `src/plugin/torirs_plugin.h` | The plugin contract, ABI 5: `win_*`, `EV_UI`, `EV_UI_BUILD` |
| `src/plugin/torirs_plugin_host.{h,c}` | The window registry, dispatch, `PluginHost_Reload` |
| `src/plugin/torirs_plugin_lua.c` | `api.window.*`, `on_ui` / `on_ui_build`, rebuild-from-source |
| `src/plugin/torirs_plugin_panel.u.c` | The plugin window itself (included into `app.c`) |
| `docs/platform_quirks.md` | COMMON-CHROME-001, and the WINDOWS-HOST-001 amendment |
