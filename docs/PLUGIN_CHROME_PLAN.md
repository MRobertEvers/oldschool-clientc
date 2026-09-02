# Cross-platform plugin chrome plan

## Status and decision

This is an implementation plan, not a description of code that already ships.

The client will have exactly one host-owned **plugin chrome shell** outside the
game canvas. That is the one plugin window shared by every plugin; a plugin can
register an entry but cannot create another shell or window. Wherever the host
can allocate child content, this shell is embedded in the game's existing
top-level application window like RuneLite's sidebar. On native desktop this is
the required default, not an auxiliary-window fallback. The shell contains a
small navigation rail that remains when the content pane is collapsed. Clicking
a plugin icon expands the one shared pane; clicking that selected icon again or
using Close collapses it. When expanded, only the most recently selected
plugin's active page renders. Plugins describe pages with a small semantic UI
API and receive semantic events. They do not create a window, Android `View`,
DOM node, Cocoa object, Win32 `HWND`, or SDL surface.

The shell has two required presentations:

1. **split** -- game and plugin page are visible beside one another; and
2. **exclusive** -- the page temporarily takes the application's content area
   on a compact window, with Back/Escape returning to the game.

Neither presentation draws over the gameframe. A platform may additionally
offer **detached**, but only after an explicit user action. Detaching moves this
same shared shell; it does not make a second plugin window, is never selected as
an automatic fallback, and is not part of the plugin contract. Failure to create
a popup or auxiliary window must leave the page attached in split or exclusive
mode.

Native facilities are preferred where they are dependable: Android Views, web
DOM, Win32 child controls, and AppKit on macOS. Linux keeps the existing SDL
renderer as its guaranteed implementation because the current Linux client has
no required desktop widget toolkit; a GTK presenter may be built when GTK is a
declared dependency. The same retained model drives every presenter.

No native build embeds web technology. There is no Android `WebView`, Apple
`WKWebView`, Windows WebView2/CEF, or Linux WebKitGTK. Normal DOM is used only
by the actual web build.

## Goals

- A plugin can add a RuneLite-like side-rail entry and an interactive page on
  Android, web, Linux, macOS, and Windows.
- All plugins share one shell; selection replaces its content, and only the
  selected plugin can render or receive page input.
- The narrow rail remains usable while the pane is collapsed. Expansion and
  collapse are first-class shell states, not opening and destroying unrelated
  windows.
- On Linux, macOS, and Windows the default shell is a child region of the
  existing game window. Opening it expands that window while preserving the
  game presentation when the window manager permits.
- Opening plugin chrome never covers or becomes part of the game canvas. On a
  small display the game is wholly replaced, not partly obscured.
- Plugin source normally contains no platform test. It may respond to the
  allocated page size and whether the game is concurrently visible.
- Native text editing, focus, accessibility, scrolling, selection, and IME
  behavior are used where a platform provides them.
- The system retains the current single-authority command/intent design and is
  testable without a window system.
- Existing `win_*`, `TORIRS_PLUGIN_EV_UI`, config-schema, and plugin reload
  behavior continue to work while plugins migrate.
- One faulty or overactive plugin cannot use the chrome API to cover the game,
  open many windows, monopolize controls, or block on a platform UI thread.

## Non-goals

- Arbitrary HTML, CSS, Java/Kotlin, Objective-C, GTK widgets, Win32 classes, or
  native handles supplied by a plugin.
- An embedded browser as a portable native UI layer.
- A complete cross-platform desktop application toolkit.
- More than one expanded plugin page at a time.
- Process isolation for native C plugins. Existing in-process code can still
  crash or block the client; this plan bounds resources owned by the chrome
  host, not arbitrary instructions executed by a plugin.
- Pixel-identical native controls across operating systems. Behavior and data
  are portable; the host owns styling.
- Making detached windows line up with the main window on every desktop. In
  particular, Wayland clients do not know their surfaces' global positions,
  so a second toplevel cannot be reliably attached to the first.
- Replacing the separate gameframe chrome ownership system in
  [`PLUGIN_CHROME.md`](../PLUGIN_CHROME.md). `chrome_claim` dresses the game;
  this plan concerns application chrome outside it.

## What exists and what is missing

The repository already contains most of the model/presenter seam:

- `ToriRSChrome` is an authoritative retained widget model.
- [`src/ui/torirs_chrome_exec.h`](../src/ui/torirs_chrome_exec.h) defines copied,
  pointer-free delta commands and idempotent intents.
- `ToriRSChromeSync` maintains a per-presenter shadow so native fields are not
  recreated on every keystroke.
- [`src/plugin/torirs_plugin.h`](../src/plugin/torirs_plugin.h) exposes
  plugin-scoped string IDs, `win_*`, `EV_UI_BUILD`, and `EV_UI`.
- [`src/plugin/torirs_plugin_panel.u.c`](../src/plugin/torirs_plugin_panel.u.c)
  builds the roster, generated settings, plugin controls, Save, Revert, and
  reload flow.
- The `web` executor builds DOM in an iframe beside the canvas. SDL and GDI
  executors use auxiliary desktop windows. `buffer` draws in the game canvas.

The gaps are structural:

1. Android is explicitly clamped to `buffer`, so its plugin window covers the
   game.
2. Desktop auxiliary windows are not an attached side panel and cannot be made
   reliably attached on all Linux window systems.
3. `buffer` is an overlay fallback, which violates the central no-overlap rule.
4. `win_*` provides settings-sized controls but no rail registration, page
   visibility/size lifecycle, or bounded custom drawing area.
5. Presentation is selected at boot instead of adapting when the application
   window becomes compact, maximized, split-screen, fullscreen, or folded.
6. The current command stream assumes calls are applied synchronously. Android
   Views must instead be mutated on the UI thread without blocking the native
   frame thread.

The implementation should extend these pieces rather than start a second UI
framework.

## User experience contract

### Shell anatomy

```text
expanded / split

+---------------------------+------+----------------------+
|                           | rail | active plugin page   |
|      game presentation    | icon | title        close  |
|   (game canvas unchanged) | icon |----------------------|
|                           | ...  | scrollable content   |
+---------------------------+------+----------------------+

compact / exclusive

+------+---------------------------+   +------+---------------------------+
| rail | game presentation         |   | rail | active plugin page        |
|      |                           |<->|      | Back, title, content      |
+------+---------------------------+   +------+---------------------------+
```

- The rail exists only when at least one plugin page or the Manage Plugins page
  is available. It consumes application layout space; it is never painted over
  the game.
- In the collapsed state, only the small rail is visible and no plugin renders.
  The shell remembers the most recently selected entry so the same icon can be
  highlighted and reopened without rebuilding unrelated plugins.
- Clicking an inactive entry selects it and expands the pane. Clicking the
  selected entry while expanded collapses the pane. Clicking a different entry
  while expanded replaces the content in place without opening another pane.
  Close collapses; it does not unregister or disable the plugin.
- Back/Escape closes a popup within the page first, then collapses the pane,
  then resumes the game's normal Back/Escape handling.
- The shell owns title, icon, close/back, selected state, scrolling, resize
  divider, focus traversal, and error presentation.
- Selecting a rail entry makes it the sole active plugin page. The previously
  selected plugin loses focus and pointer capture, receives `visible = false`,
  and cannot emit presenter commands or receive draw/input events. Its plugin
  state remains alive; its UI is rebuilt if it is selected again.
- The built-in Manage Plugins roster is a permanent final rail entry. Existing
  generated settings remain reachable there even if a plugin has no custom
  page.
- A plugin may set a short badge or request attention. It may not open its page,
  detach it, reorder the rail, or steal focus without a user action.
- Disabling, unloading, or faulting the active plugin removes its page and
  returns to the roster (split) or game (exclusive).

### Layout policy

The host, not a plugin, chooses the presentation from current available space:

```text
if game_minimum + rail + panel_minimum fits:
    split
else:
    exclusive
```

`game_minimum` is the minimum *presented* game size after density and interface
scale, not a device-name check. The panel defaults to 320 logical chrome units,
is user-resizable between 280 and 480, and is clamped to the current content
box. Exact values should be tuned with the probe page before being frozen.

Placement follows this fixed preference order:

1. **attached-grow** -- in an ordinary desktop window, request an outer client
   width equal to the current game region plus rail and panel. Keep the game
   region the same size and put the new child pane beside it, matching RuneLite.
2. **attached-fit** -- if the outer window cannot grow, divide its existing
   client area between game and pane without overlap.
3. **attached-exclusive** -- if both regions cannot remain usable, keep the
   shell in the same top-level window and temporarily replace the game region.
4. **detached** -- only when the user explicitly asks and the platform supports
   it. This is not selected to solve a failed resize.

Collapse reverses attached-grow: remove only the panel width from the outer
window, preserve the rail and game region, and remember the last successfully
expanded game size. Repeated expand/collapse must not accumulate width. If the
window manager changed the window independently while expanded, clamp the
shrink so the game never ends smaller than the size the user chose.

The shell state machine is explicit:

```text
COLLAPSED(last_selected)
  -- click plugin A --> EXPANDED(selected=A)
EXPANDED(selected=A)
  -- click plugin A / Close / Back --> COLLAPSED(last_selected=A)
  -- click plugin B --> EXPANDED(selected=B)
```

Only the `EXPANDED` states mount controls or issue `EV_PANEL_DRAW`.

The window manager owns final placement. The client requests a larger size but
does not require global coordinates or force the window to remain on a
particular monitor. The game presentation must keep its prior size if the
window manager accepts the growth request. If it refuses, or the window is
maximized or fullscreen, the shell immediately chooses attached-fit or
attached-exclusive. An OS resize is always advisory; SDL explicitly documents
that the returned window size can differ from the request.

Opening or closing plugin chrome is not a game-canvas resize. The game's logical
canvas and UITree layout remain stable; only the rectangle into which that
canvas is presented changes. If the rectangle is smaller, the existing
interface scaling/letterbox path fits it. A real user resize may still change a
resizable game's logical canvas under `COMMON-WINDOW-001`.

The host publishes logical content width and height after title, rail, safe-area,
and IME insets. Semantic widgets reflow without plugin help. Only a custom draw
region needs to react to exact dimensions.

## Architecture

```text
plugin (C or Lua)
  |  page registration, semantic nodes, property updates
  v
PluginHost page registry + one active ToriRSChrome authoritative model
  |  versioned SYNC_BEGIN ... deltas ... SYNC_END
  v
PluginChromeHost (selection, attached placement, optional detach, persistence)
  |                         ^
  | commands                | idempotent intents
  v                         |
platform presenter / mirror
  |- Android Views
  |- web DOM + embedded canvas
  |- Win32 child controls
  |- AppKit split view and controls
  |- GTK when explicitly built
  `- ToriRSChrome surface fallback / Linux default
```

There are four distinct responsibilities:

1. **PluginHost** owns plugin identity, inert rail metadata for every registered
   page, quotas, events, and teardown. It mounts page content for only the
   selected plugin.
2. **ToriRSChrome** owns widget values, focus identity, row order, and the
   display list used by surface presenters.
3. **PluginChromeHost** owns application navigation and placement. It sees
   available rectangles and presenter capabilities, but never game state.
4. A **presenter** mirrors the model using native widgets or raster output. It
   never calls a plugin directly.

The model remains authoritative. For example, a native toggle reports “widget
`show_loot` is now on”; the frame thread applies that result to the model and
then raises the plugin event. A presenter update that the model rejects is
corrected by the next delta.

## Plugin-facing API

### Registration and lifecycle

Append a panel API to the current plugin ABI; do not insert fields into the
existing function table. `panel_*` is used because `chrome_*` already means
claims on parts of the gameframe.

A plugin calls `panel_request` from `EV_START` to register rail metadata. That
operation does not build, render, select, or open the page. Only a later user
selection permits the host to raise `EV_PANEL_BUILD` for that plugin.

Illustrative C shapes (names may be adjusted during implementation):

```c
struct ToriRS_PluginPanelDesc {
    char const *title;
    char const *icon_asset;       /* optional; host supplies a fallback */
    int preferred_width;          /* logical units; a hint, 0 = default */
};

bool (*panel_request)(ctx, struct ToriRS_PluginPanelDesc const *desc);
bool (*panel_widget)(ctx, int kind, char const *id, char const *label);
bool (*panel_set_text)(ctx, char const *id, char const *text);
bool (*panel_set_value)(ctx, char const *id, int value);
bool (*panel_set_options)(ctx, char const *id, char const *choices, int selected);
bool (*panel_set_badge)(ctx, char const *text);
void (*panel_clear)(ctx);
void (*panel_invalidate)(ctx, char const *custom_view_id);
```

Add events equivalent to:

- `EV_PANEL_BUILD`: the authoritative page model is empty and must be declared.
  It is raised when that plugin is selected, or when the selected plugin is
  reloaded or re-enabled. Work for a nonselected plugin is deferred until
  selection. A presenter resynchronization reads the retained active model and
  does not call the plugin.
- `EV_PANEL_ACTION`: one semantic control or custom hit region was used. It
  carries plugin-local ID, result-state value, and copied text.
- `EV_PANEL_LAYOUT`: visibility or allocation changed. It carries width,
  height, scale, size class, `visible`, and `game_visible`.
- `EV_PANEL_DRAW`: a visible dirty custom region needs primitives. It carries
  that region's local logical rectangle and a scoped draw target.

There is deliberately no `platform` field. `size_class` is `COMPACT`, `MEDIUM`,
or `EXPANDED` based on the allocated page, and `game_visible` lets an animated
tool reduce work when the compact presentation has replaced the game. Those are
the questions a plugin can legitimately answer without learning why they are
true.

Selection is a host transaction:

1. mark the old plugin invisible and cancel its focus, IME, pointer capture,
   pending custom draw, and animation subscription;
2. clear the one active `ToriRSChrome` page and set the new active plugin ID;
3. raise `EV_PANEL_BUILD` only for the new plugin; and
4. publish one atomic presenter sync.

Nonselected plugins retain ordinary plugin/game state, but no native controls,
DOM nodes, panel bitmap, or custom draw list. Their `panel_set_*` calls may mark
their page data stale for the next build; they never produce presenter work.

Collapse is similar but retains the selected plugin ID as
`last_selected_plugin`: it marks that plugin invisible, clears the active model,
and tears down presenter controls. Re-expansion builds only that remembered
plugin. Registration metadata for all rail icons remains resident in either
state.

Lua mirrors the same contract:

```lua
function M.on_start(api)
  api.panel.request({ title = "Loot", icon = "loot.png" })
end

function M.on_panel_build(api)
  api.panel.section("session", "Session")
  api.panel.label("kills", "Kills", "0")
  api.panel.checkbox("valuable", "Valuable drops only", true)
  api.panel.button("clear", "Clear session")
  api.panel.custom("chart", { preferred_height = 120, label = "Loot value chart" })
end

function M.on_panel_action(api, ev)
  if ev.id == "clear" then
    -- clear state
  end
end
```

### Guaranteed semantic controls

Keep the guaranteed vocabulary intentionally small and column-oriented:

- section heading, label/paragraph, separator, key/value readout;
- button, checkbox/toggle, one-line input, multiline input, and dropdown;
- list row, image, progress/readout, and host-generated error text; and
- custom draw region.

The host lays out and scrolls these. A plugin cannot position ordinary controls
with pixels, which is what allows Android, DOM, AppKit, Win32, and raster rows to
stay equivalent. Control IDs are plugin-scoped stable strings. All labels have
an accessible text representation even when an icon is shown.

The existing `win_*` vocabulary maps directly onto these controls. During
migration, `api.window` continues to populate the plugin's detail under Manage
Plugins; `api.panel` opts into a rail page. Both use the same internal widget
records and event router.

### Custom draw regions

A custom region is the escape hatch for charts, timelines, previews, and tools
that are not forms. It is not an escape hatch into platform code.

- Drawing uses the existing portable rect, line, text, sprite, and image
  primitives against a panel-local target.
- Coordinates are logical chrome units. The event supplies exact size and
  scale; the presenter performs final device-pixel conversion.
- Each interactive hit region has a plugin-local action ID, role, accessible
  label, and optional current value. Events return local coordinates and an
  input-neutral action (`activate`, `drag`, `scroll`, `key`).
- Output is clipped to the custom region. Pointer capture cannot escape it.
- Native presenters embed the resulting bitmap in a native image/canvas view:
  Android custom `View`, web `<canvas>`, AppKit `NSView`, a Win32 owner-drawn
  child, or the SDL surface directly.
- A region draws only after invalidation, resize, asset completion, or while an
  explicit bounded animation subscription is active. Hidden pages do not draw.
- Text entry, dropdowns, and other standard controls must use semantic widgets,
  not a custom region. This preserves IME, keyboard, and accessibility behavior.

Do not allow plugin-provided HTML, shaders, JavaScript, or native callbacks.

## Command, intent, and threading contract

Extend the current command vocabulary with shell/page operations (rail entry
add/remove/update, active entry, page metadata, viewport, and full reset). Keep
the existing widget commands and ordering rules.

Every sync batch has `protocol_version`, `presenter_generation`, and monotonically
increasing `model_revision`:

```text
SYNC_BEGIN(revision)
  shell and widget deltas
SYNC_END(revision)
```

Required behavior:

- Commands remain fixed-size POD with copied strings. No pointer crosses the
  executor, JNI, DOM, or queued-thread boundary.
- A presenter applies a complete batch atomically. Native layout and repaint
  happen once at `SYNC_END`, preserving focus and avoiding intermediate frames.
- Widget handle reuse remains distinguished by serial/generation.
- Stateful intents carry results rather than edits. Every intent also carries a
  presenter-generation/sequence pair; the host drops a duplicate before a
  momentary button action can be dispatched twice.
- Platform UI threads enqueue intents; they never invoke the plugin or mutate
  the model.
- The frame thread drains intents before generating the next command batch.
- Queues are bounded and nonblocking. If a command batch will not fit, drop the
  entire batch, mark the presenter stale, and send `RESET` plus a full snapshot
  on the next opportunity. Never apply half a transaction.
- A hidden or newly attached presenter begins with an empty shadow and receives
  a full snapshot. The plugin is not asked to rebuild merely because a presenter
  changed.
- Commands carry exactly one active plugin ID. A stale command or intent naming
  an older selection generation is dropped, so a late Android/DOM event cannot
  act on the newly selected page.
- Unknown optional commands are ignored only after a version/capability
  handshake says that is safe. A missing required control causes presenter
  refusal and fallback, not a blank row.

The Android UI thread/native frame-thread split makes this mandatory, and it is
also useful for DOM batching and future remote debugging.

## Application-shell and game isolation

The platform window must expose three rectangles in physical drawable pixels:
`game`, `rail`, and `panel`. Only `game` is mapped into game-canvas coordinates.
The presenter owns the other two.

Add an application-shell API below `App`, rather than teaching game layout
about sidebars. Conceptually:

```c
PlatformWindow_SetChromeState(platform, rail_px, panel_px, presentation);
PlatformWindow_GameRect(platform, &rect);
PlatformWindow_ChromeRects(platform, &rail, &panel);
```

The concrete API may differ per backend, but the following invariants may not:

- UITree sees the game canvas only; plugin pane width never enters a layout
  slot or `safe_gamechrome` calculation.
- Software, GL3, GLES2/WebGL1, and D3D9 restrict game present/viewport/scissor
  to `game`. They clear/present the application background outside it.
- Mouse/touch is classified in physical shell coordinates first. Events in
  rail/panel go only to the presenter; game events are then mapped through the
  game rectangle and letterbox.
- Keyboard focus belongs to either chrome or game. While a native editor owns
  it, no chat, clientscripts, debug hotkey, or movement binding sees its keys.
- Closing chrome cancels pointer capture, dropdowns, composition, and IME before
  returning focus to the game.
- Safe-area, display-cutout, system-bar, and IME insets are applied by the shell
  before it reports a page viewport.
- The shell is lazy. It allocates native controls and custom-region buffers only
  when first opened, and releases presentation resources on activity/window
  teardown while retaining model state.
- There is one live presenter mirror. Detaching reparents or rebinds that same
  shell and closes its former container; it never shows docked and detached
  copies simultaneously.

The old floating `buffer` executor remains available only for developer tools.
Plugin chrome falls back to a new shell surface presenter: split when it fits,
exclusive otherwise. There must be no production path from presenter failure
back to an overlay on the game.

## Platform plans and limitations

### Android

Constraints:

- The client is one `Activity` containing a `SurfaceView`; Android does not give
  this app the desktop assumption of an adjacent movable toplevel. System
  overlays would require unrelated privileges and are not appropriate.
- The available application window can change during split screen, desktop
  windowing, folding, rotation, and IME display. Android's guidance explicitly
  says to derive adaptive layout from current window size rather than device
  type. The current app supports API 21, so raw measured dp breakpoints are the
  dependency-free implementation of that rule.
- `SurfaceView` surface lifetime normally follows visibility. Exclusive mode
  must therefore treat hiding the game view as an expected surface loss rather
  than assuming its `ANativeWindow` remains valid.
- Android Views may only be changed on the UI thread; the game loop runs on its
  native frame thread.

Implementation:

1. Replace `ClientActivity`'s simple `FrameLayout` composition with a
   `PluginChromeLayout` containing the existing `ClientSurfaceView`, a native
   rail, a native pane, and the existing keyboard-dismiss affordance.
2. In split mode, lay out the SurfaceView and pane as siblings. In exclusive
   mode, give the pane the content rectangle and make the game rectangle empty;
   hide the SurfaceView and let the existing `surfaceDestroyed`/NULL-window path
   stop presentation. Game logic continues. Separate audio foreground state
   from Surface availability so opening a tool page does not mute the game.
3. Implement an Android native-widget presenter with framework Views only, to
   preserve the project's no-AndroidX/API-21 build: `TextView`, `Button`,
   `CheckBox`/`Switch`, `EditText`, `Spinner`, `ScrollView`, `ImageView`, and a
   small custom View for custom regions. The host theme may skin them, but their
   focus, IME, selection, and accessibility stay native.
4. Batch commands in JNI, post one `Runnable` at `SYNC_END`, and push copied
   intents into the existing mutex-protected native queue. Never hold the
   Android platform mutex while calling Java.
5. Derive compact/split from the root's current measured width and height in dp.
   Recompute on every layout; do not branch on phone/tablet or orientation.
6. Combine system bars, cutout, gesture, and IME insets for the pane. The game
   keeps receiving its existing keyboard inset only when the game owns the
   editor.
7. Android Back closes dropdown/IME, then the plugin page, then follows the
   existing Back-to-Escape route.

Primary constraints: Android documents that window size classes are dynamic and
window-based, not device-based ([window size classes](https://developer.android.com/develop/adaptive-apps/guides/use-window-size-classes)); it also documents the default visibility-coupled `SurfaceView` lifetime
([`SurfaceView`](https://developer.android.com/reference/android/view/SurfaceView.html))
and distinct IME/system inset types
([`WindowInsets.Type`](https://developer.android.com/reference/android/view/WindowInsets.Type)).

### Web

Constraints:

- A popup is optional: `window.open()` is subject to user activation and popup
  policy, so it cannot be the only way to reach a plugin page.
- Only content inside the fullscreened element remains in the fullscreen
  presentation. The application root, not the game canvas alone, must be the
  fullscreen element.
- Host CSS and key listeners must not leak into a plugin form. The page and
  canvas can resize without a browser `window.resize` event when surrounding
  layout changes.

Implementation:

1. Evolve the current `torirs_chrome.js` iframe into the shell's DOM presenter.
   Keep the iframe (or an equivalent isolated shadow root) so form styles and
   keyboard events are contained.
2. Change the host page to a CSS grid/flex application root with game, rail, and
   pane tracks. Split adds the pane track; exclusive hides the game track and
   gives the pane the content track. No absolute panel over the canvas.
3. Keep native HTML controls and browser scrolling/IME/accessibility. Embed a
   DPR-sized `<canvas>` only for custom regions.
4. Observe the application root, game stage, and pane with `ResizeObserver`;
   report the pane's content box in logical CSS pixels and resize a custom
   canvas from `devicePixelContentBoxSize` when available.
5. Fullscreen the application root so the rail and pane remain reachable.
6. Keep pop-out as a user-invoked optional action. Adopt the live DOM when
   possible; otherwise prime the new presenter from a full model snapshot. If
   blocked or closed, return to the prior split/exclusive page without changing
   plugin state.
7. Prevent pane pointer and key events from reaching Emscripten's canvas input.
   Restore canvas focus only after the pane closes.

Primary constraints: the HTML standard tracks transient user activation for
activation-gated APIs such as popups
([HTML user activation](https://html.spec.whatwg.org/multipage/interaction.html#tracking-user-activation)); the Fullscreen standard defines one document fullscreen element in the top layer
([Fullscreen API](https://fullscreen.spec.whatwg.org/)); and Resize Observer is
defined specifically for element box changes
([Resize Observer](https://www.w3.org/TR/resize-observer/)).

### Windows

Constraints:

- Both current Windows lanes are SDL-free and include an XP-compatible lane.
  The solution must not add WebView, .NET, Direct2D/DirectWrite, or a post-XP
  loader-time dependency.
- The D3D9 device remains attached to the existing main `HWND`; plugin chrome
  must not create a second render device.

Implementation:

1. Refactor the existing GDI mirror so it can target a `WS_CHILD` pane in the
   main window instead of only an owned tool-window toplevel. Win32 explicitly
   supports dividing a parent client area into functional areas with child
   windows.
2. Use existing USER32 controls (`BUTTON`, `EDIT`, `COMBOBOX`, `STATIC`) and
   existing owner-drawn controls where the base set has no suitable native
   widget. Preserve the no-comctl32 XP contract unless a control can be compiled
   out of that lane.
3. Have the main `WM_SIZE` path allocate rail/pane child rectangles and give the
   remainder to GDI or D3D9 presentation. D3D9 changes viewport/scissor in the
   same device; Soft3D blits only into the game rectangle.
4. Keep the existing GDI toplevel as the optional detached presenter. It mirrors
   the same model and cannot be opened by a plugin. Moving there removes the
   docked instance; it is not a second plugin window.
5. Route `WM_COMMAND`, focus, wheel, DPI, and accessibility through the child
   presenter. A single `BeginDeferWindowPos`-style batch is committed at sync
   end.

Primary constraint: Microsoft documents `WS_CHILD` windows as the standard way
to divide a parent client area into functional regions
([Win32 child windows](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#child-windows)).

### macOS

Constraints:

- The game window is currently owned by SDL. AppKit may be used, but it must not
  replace SDL event handling or create a second GL context.
- A detached `NSPanel` has independent fullscreen/Spaces behavior and is not a
  dependable attached sidebar.

Implementation:

1. Add a small Objective-C AppKit presenter. Obtain SDL's `NSWindow` through
   `SDL_SysWMinfo`, retain no borrowed Cocoa object past the SDL window lifetime,
   and perform AppKit work on the main thread.
2. Put the SDL content view and a native plugin pane under an `NSSplitView` (or
   an equivalent AppKit container). AppKit owns divider interaction and pane
   constraints; SDL continues drawing only in its resized game view.
3. Map semantic nodes to native labels, buttons, checkboxes, text fields/views,
   popup buttons, scroll views, and an `NSView` for custom drawing.
4. Verify early that wrapping/reparenting SDL's Cocoa view survives software and
   GL3 creation, fullscreen, Retina scale changes, and SDL shutdown. If the
   spike fails, ship the same-window SDL surface presenter first; do not fall
   back to a floating overlay.
5. Keep the current SDL auxiliary window as optional detached mode.

Neither the AppKit presenter nor the detached mode uses `WKWebView`.

AppKit's `NSSplitView` is specifically a native linear pane container and
supports collapsible arranged subviews and saved divider positions
([`NSSplitView`](https://developer.apple.com/documentation/appkit/nssplitview)).
SDL exposes its Cocoa `NSWindow` through the documented system-window-info
structure ([`SDL_SysWMinfo`](https://wiki.libsdl.org/SDL2/SDL_SysWMinfo)).

### Linux

Constraints:

- The supported baseline is SDL2, not a particular desktop environment or
  widget toolkit.
- On Wayland a client does not know the global position of its surfaces. A
  second toplevel therefore cannot be kept visually attached to the game window
  in the general case.
- GTK cannot be assumed to exist merely because the program runs under GNOME,
  and embedding an independently created SDL Wayland surface in GTK is not a
  portable fallback.

Implementation:

1. Make the same-window SDL surface presenter the required Linux path. The main
   window is partitioned into game/rail/pane; ToriRSChrome rasterizes the latter
   into the same software present or GL composition without another game render
   target.
2. Route SDL events by the physical shell rectangles before game coordinate
   mapping. Retain SDL text input behavior for fields.
3. Provide a `PLATFORM_CHROME_EXEC_GTK` build option only for distributions that
   deliberately add GTK and own its event-loop integration. It may map the same
   semantic model to `GtkPaned` and native controls, but it is not required for
   compatibility and is never selected by a plugin. It does not use WebKitGTK.
4. Detached SDL remains optional and independent. Do not promise adjacency,
   remembered global position, or always-on-top behavior on Wayland.

The Wayland protocol description states that clients do not know the global
position of their surfaces
([Wayland protocol and model](https://wayland.freedesktop.org/docs/book/Protocol.html#sect-Protocol-Wayland)); GTK's native split container is `GtkPaned`
([GTK 4 `GtkPaned`](https://docs.gtk.org/gtk4/class.Paned.html)).

## State and persistence

Host state, stored per installation rather than in plugin config:

- selected page, collapsed/expanded state, and last nonexclusive panel width;
- rail ordering/pins if user customization is added;
- detached preference and detached size where that presenter supports it; and
- no physical screen coordinates.

Persist width in logical units and clamp it on every restore. Do not persist
split versus exclusive; it is derived from current space. Do not let a stale
selected plugin prevent startup—fall back to Manage Plugins or the game.

Plugin widget state follows the current rules: settings are staged until Save;
explicit plugin controls dispatch immediately; plugin-owned durable data uses
the plugin store. Presenter replacement, resize, detach, or compact-mode changes
must preserve typed text, selection, focus identity when possible, page scroll,
and custom page model state.

## Resource, safety, and failure policy

- One shell, one live presenter mirror, and one expanded page per client, never
  one native window per plugin.
- Keep fixed per-plugin limits for nodes, option bytes, text bytes, images,
  custom regions, accessible hit regions, commands per sync, and animation
  frequency. Log the exact exhausted limit and show a host error row.
- Asset decode is size-limited. Icons are copied/decoded by the host and scaled
  into a fixed rail slot; a plugin cannot supply an executable platform asset.
- Labels and text are data, never markup. External links, if later added, need a
  host confirmation and platform URL service.
- Presenter errors are isolated. Destroy the mirror, increment its generation,
  and bind the shell surface presenter from a fresh snapshot.
- A recoverable scripted-plugin error while building shows a host-owned fault
  page with Disable and Back. It does not leave a blank selected rail entry.
- A recoverable scripted custom-draw error retains the last good frame and
  disables animation until another explicit invalidation.
- On suspend/context loss, discard presentation resources but retain the model.
  Resume creates a new generation and full sync.
- No executor callback may block on another UI thread, popup, window manager, or
  plugin. Native work is posted or polled.

## Implementation sequence

### Phase 0 -- freeze contracts and probes

- Add this feature's invariants to
  [`docs/platform_quirks.md`](platform_quirks.md): plugin chrome is outside the
  game rect; attachment to the existing application window is the default;
  split/exclusive are required; detached is explicit and optional.
- Add a probe plugin containing every semantic control, long/short content,
  validation errors, live updates, an accessible custom chart, and a deliberate
  command flood.
- Extend the recorder executor tests first: page registry, transactions,
  overflow/full-resync, handle reuse, presentation switch, and idempotent
  intents.
- Record game-canvas size, layout revision, and a screenshot before/after page
  open. These become the no-interference oracle.

### Phase 1 -- shell model and compatible API

- Add `PluginChromeHost` and the rail/page registry above the existing plugin
  panel.
- Add the active-selection generation and enforce that only its plugin is
  built, drawn, and sent input.
- Append the panel API/events to the current ABI and Lua adapter.
- Implement semantic nodes with `win_*` compatibility adapters.
- Add visibility/layout events, badge/attention, quotas, persistence, and
  teardown behavior.
- Extend command/mirror/recorder support with versioned atomic batches and full
  reset.

Deliverable: the probe page works in the existing web/SDL/GDI presentations,
although desktop attachment and Android are not yet enabled.

### Phase 2 -- universal non-overlay shell surface

- Partition the existing top-level platform window into game/rail/pane child
  rectangles; do not create an auxiliary window.
- Update Soft3D, GL3, GLES2/WebGL1, and D3D9 present paths and input mapping.
- Implement attached-grow, attached-fit, attached-exclusive, and
  stable-game-canvas behavior, including reversible grow/shrink with a
  persistent collapsed rail.
- Add the shell surface presenter and remove plugin use of the floating `buffer`
  fallback.

Deliverable: Linux, macOS, and Windows have a same-window attached panel with no
new native widget dependency, and every later native presenter has a safe
fallback.

### Phase 3 -- web DOM shell

- Refactor the current iframe implementation into rail plus responsive pane.
- Add exclusive layout, fullscreen-root behavior, DPR custom canvases, atomic
  model reset, and popup-failure recovery.
- Add real-browser tests in addition to the current fake-DOM tests.

Deliverable: web uses native DOM controls and never needs a popup.

### Phase 4 -- Android Views shell

- Add `PluginChromeLayout`, native widgets, JNI transaction delivery, intent
  queue, insets/focus/Back handling, and custom View rendering.
- Remove the Android clamp to the in-canvas buffer executor.
- Exercise API 21 and current API devices before changing the default.

Deliverable: phones use exclusive mode when necessary; tablets, foldables, and
large/resizable windows use split mode without plugin changes.

### Phase 5 -- native desktop presenters

- Refactor GDI into a Win32 child presenter and run it on both XP and win64.
- Spike, then implement, the AppKit split presenter; keep the surface presenter
  if SDL view integration fails a shipping gate.
- Add GTK only as an explicit Linux build feature with a declared dependency.
- Keep SDL/GDI detached presentations optional and user-controlled.

Deliverable: native controls are used where the host can support them without
weakening the all-platform guarantee.

### Phase 6 -- custom-region and accessibility completion

- Add invalidation-driven custom drawing and semantic hit regions.
- Map accessibility roles/names/values into Android, DOM, AppKit, Win32, and GTK
  presenters; provide keyboard focus traversal in the surface presenter.
- Add performance telemetry for sync bytes, native apply time, custom raster
  time, dropped transactions, and hidden-page work.

## Verification matrix

Every row is required for release unless marked optional.

| Area | Required checks |
| --- | --- |
| Model | full snapshot equals incremental result; transaction truncation never applies; duplicate intent dispatches once; recycled handle is a new node |
| Isolation | toggling a page does not change game canvas dimensions/layout revision; no pane input reaches game; no game pointer reaches pane |
| Responsive | default uses the existing top-level window; attached-grow preserves game size when accepted; collapse removes exactly the pane width but keeps the rail; repeated toggles do not drift; resize across fit/exclusive threshold; maximize/fullscreen; panel width restore is clamped |
| Lifecycle | enable/disable/reload active plugin; presenter refusal and reset; suspend/resume; renderer/context recreation |
| Focus | type and compose text; Tab/Shift-Tab; dropdown keyboard use; IME open/close; Back/Escape order; game/chat receives no focused-pane keys |
| Accessibility | every semantic control has role/name/state; rail entries are navigable; custom hit regions have a labelled fallback |
| Performance | hidden page emits/draws nothing; idle visible page emits no deltas; flood cannot block frame loop; custom animation honors cap |
| Android | API 21 and current API; compact/expanded; system bars/cutout; IME; activity pause/resume; multi-window resize |
| Web | Chromium, Firefox, WebKit; narrow/wide; CSS/fullscreen; DPR change; popup blocked/closed; embedded host styling isolation |
| Windows | XP Soft3D, XP D3D9, win64 Soft3D/D3D9; 100/200% DPI; child-control focus; optional detach |
| macOS | Soft3D and GL3; Retina/non-Retina; fullscreen/Spaces; AppKit spike fallback; optional detach |
| Linux | SDL Soft3D and GL3 on X11 and Wayland; scaling; Wayland detach without adjacency assumptions; optional GTK build |

Use the current headless recorder and golden-image targets for the model and
surface presenter. Add Android instrumentation, browser automation, and native
smoke harnesses only for behavior that cannot be proved below the platform
boundary.

## Release acceptance criteria

The feature is complete only when all of the following are true:

1. The same unmodified probe plugin registers, renders, accepts input, and
   preserves state on Android, web, Linux, macOS, Windows XP, and modern
   Windows.
2. Ten registered probe pages still create exactly one plugin shell and one
   presenter; selecting the newest one stops all build/draw/input work for the
   previous selection before the new page is shown.
3. Linux, macOS, and Windows open the shell inside the existing game window by
   default and create no additional toplevel. An ordinary resizable window
   grows by the rail/pane width when the window manager accepts the request,
   while the game region keeps its prior dimensions.
4. Clicking the selected rail icon, Close, or Back collapses the pane to the
   narrow rail, stops all plugin rendering/input, and reverses accepted window
   growth without cumulative size drift; clicking again expands the remembered
   selection.
5. No required path presents plugin chrome over any portion of the gameframe.
6. No native build links or instantiates an embedded browser engine.
7. Compact windows offer the exclusive page and a deterministic return to the
   game; they never make the page unreachable.
8. A native presenter refusal, command overflow, popup block, context loss, or
   plugin reload recovers from the authoritative model without process restart.
9. Focused plugin text controls do not trigger game input on any platform.
10. Plugins contain no platform branches. The probe may branch only on neutral
   allocation/capability facts supplied by the panel API.
11. Idle collapsed chrome has no plugin raster work and negligible sync work;
   opening it does not create a second game renderer or device.
12. Existing `win_*` plugins and saved configuration continue to behave as
   before through the compatibility adapter.

These criteria deliberately make the portable shell presenter part of the
product rather than an error screen. Native presentation improves the result
where available; it never determines whether plugin chrome exists.
