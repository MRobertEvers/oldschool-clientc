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
ToriRSChrome_Build  →  ToriDbgPrim[]  →  UITree emit  →  ToriRS_Frame  →  Soft3D / GL3 / WebGL1 / D3D9
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

Two orderings are load-bearing and both are tested:

- **Panel closes before panel opens.** An executor may assume a widget's panel
  exists when it hears about the widget.
- **Widget removals before widget additions.** A freed handle can be recycled by
  an add in the same frame; an executor told `add 7` before `remove 7` would end
  up with neither.

### Strings are copied at the seam

The chrome deliberately borrows -- prim text points into its widget, a
dropdown's options point at the caller's array. Those lifetimes are fine inside
one process and wrong the moment a command is queued, posted to a browser tab,
or written to a recording. Every string entering a command is copied into it.

## 3. Choosing one

`TORIRS_CHROME_EXECUTOR=buffer|sdl|web|gdi|cs2`, alongside the
`TORIRS_CHROME_THEME` the developer chrome already reads. Default is `buffer`.

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

| Executor | State |
|---|---|
| `buffer` | **Done.** The in-canvas prim path; the default and the fallback everywhere. |
| `sdl` | **Done.** One auxiliary OS window on the SDL lanes (macOS, Linux, and the web lane's SDL build). See COMMON-CHROME-001. |
| `web` | Not implemented. Chooser falls back to `buffer` with a message. |
| `gdi` | Not implemented. Chooser falls back to `buffer` with a message. |
| `cs2` | Not implemented. Chooser falls back to `buffer` with a message. |

The three unimplemented ones need no further seam work -- they are
native-widget executors, and `apply`/`poll` is the whole contract. What each
still needs:

- **web** -- an `EM_JS` façade following the existing `web_editor_open_panel_tab`
  pattern (C asks, the page owns the DOM), a widget builder in the page script
  over `src/web/torirs_channel.js`, and intent frames coming home on the same
  wire. `src/web/panel.html` already renders the chrome's OSRS skin in CSS.
- **gdi** -- an owned `WS_EX_TOOLWINDOW` of common controls, plus `lane-check`
  rules for the comctl32 imports. COMMON-CHROME-001 and the WINDOWS-HOST-001
  amendment already permit the window.
- **cs2** -- a sidebar Plugin button (a `tab_icon` + `redstone_tab` + `sidebar`
  triple in a layout INI) and a panel body built with `UIBuildComponent` /
  `UITree_CcCreate`. Needs no new platform code at all.

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

## 6. The plugin window

One window, tabbed. The first tab is the roster -- every plugin, its switch and
its last fault -- and every plugin that asked for one gets a tab of its own.
That is the sandbox rule made concrete: plugins share ONE extra window, and
`api->win_request` claims a **tab** in it rather than a window of its own.

It is its own `ToriRSChrome` instance rather than a panel in the developer
chrome, because it is the one piece of chrome a *player* uses: it must be
openable beside the game without the editors' claim on the keyboard, it needs
capacity for a tab per plugin, and it is the surface an executor is bound to
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

End-to-end, headlessly:

```sh
TORIRSSERVER_ALLOW_STALE_SCRIPTS=1 \
TORIRS_PLUGIN_MANIFEST=plugins/_windemo.ini TORIRS_PLUGIN_PREFS= \
TORIRS_SIM_HOTKEY="150,p" TORIRS_SIM_CLICK_AT="200,110,100" \
TORIRS_EXIT_BMP=/tmp/win.bmp TORIRS_MAX_FRAMES=280 \
./src/torirs --soft3d --manifest manifests/manifest_osrs239_torirs.ini
```

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
| `src/ui/torirs_chrome_exec_sdl.c` | The SDL surface executor |
| `src/platform/platform_sdl2.{h,c}` | `PlatformSDL2_Aux*` -- the auxiliary window |
| `src/plugin/torirs_plugin.h` | The plugin contract, ABI 5: `win_*`, `EV_UI`, `EV_UI_BUILD` |
| `src/plugin/torirs_plugin_host.{h,c}` | The window registry, dispatch, `PluginHost_Reload` |
| `src/plugin/torirs_plugin_lua.c` | `api.window.*`, `on_ui` / `on_ui_build`, rebuild-from-source |
| `src/plugin/torirs_plugin_panel.u.c` | The plugin window itself (included into `app.c`) |
| `docs/platform_quirks.md` | COMMON-CHROME-001, and the WINDOWS-HOST-001 amendment |
