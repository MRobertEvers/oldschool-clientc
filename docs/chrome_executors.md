# Plugin chrome browser executors

This document defines the production path from a plugin's retained semantic
page to the shared plugin chrome. The central rule is literal:

> One running client owns exactly one plugin-chrome browser instance/browsing
> context. Every plugin shares it, and only the most recently selected plugin
> has a page in it.

The browser is application infrastructure, not plugin infrastructure. A plugin
cannot create a browser, window, document, iframe, script context, or URL. It
registers a destination and publishes bounded semantic controls. The
application-owned runtime renders those controls with the ToriRS modern Old
School RuneScape skin.

## 1. Non-negotiable invariants

- There is one application-owned plugin-chrome browser control/browsing context
  and one persistent rail per application window. There is never one browser or
  iframe per plugin. On Web, the one shared application-owned iframe is that
  context; the application's containing page is not a second plugin-chrome
  instance.
- The browser control survives collapse and plugin selection. It is destroyed
  only with its owning application window/activity.
- At most one plugin page exists in the browser's page root. Selecting another
  destination replaces that page in place; it does not navigate or recreate
  the browser.
- Collapse removes the page, shrinks the allocation to the rail, and restores
  the game allocation. It does not destroy the browser or rail.
- Plugin UI is semantic data. Plugins do not supply HTML, JavaScript, CSS,
  native controls, browser URLs, or navigation callbacks.
- Rail and pane input is isolated from game input. An HTML editor owns text,
  selection, caret, composition, and accessibility while it is focused.
- The browser loads only application-packaged files and locally staged
  host-owned bitmap resources. Network navigation, popups, downloads,
  permissions, and external schemes are rejected.
- The retired native View/GDI/SDL presenters are reference material under
  [`old/plugin_chrome_native`](../old/plugin_chrome_native/README.md), not an
  alternate production path.

## 2. Ownership and lifetime

```text
PluginHost
  registry: all rail destinations and authored icon metadata
  selected page: one retained semantic model, generation fenced
       |
       +--> rail.snapshot / rail.icon -------------------+
       |                                                 |
       +--> page.snapshot / page.delta / custom.bitmap --+--> one browser
                                                            one document
                                                            one rail root
                                                            zero/one page root
                                                                 |
                         rail.select / widget.intent / layout / editor.focus
                                                                 |
                                                             frame thread
```

[`PluginHost`](../src/plugin/torirs_plugin_host.h) owns the complete registry,
the selected plugin, its selection generation, and the sole bounded page
model. A nonselected plugin cannot add nodes to, mutate, draw into, or receive
input from that page.

[`ToriRSChrome`](../src/ui/uitree_debug_overlay.h) is the retained semantic
model. Stable row identity, focus, values, layout, and custom-region damage
remain authoritative outside the browser. Widget handles can be recycled, so
every browser event is fenced by page generation and widget serial.

[`ToriRSChromeExec`](../src/ui/torirs_chrome_exec.h) copies the retained model
into pointer-free transactions. The browser reducer applies a complete
snapshot or a delta to its one DOM tree. Result-state intents are copied back
to the frame thread; browser callbacks never invoke plugin code directly.

[`ToriRSChromeRailSync`](../src/ui/torirs_chrome_rail.h) publishes the rail
independently of the selected page. This is why the rail can remain interactive
while the page is collapsed. Rail selection is fenced by the snapshot's
selection generation just as page input is fenced by page generation and
widget serial.

## 3. Canonical local bundle and bridge

The embedded hosts load the application bundle in
[`src/plugin_chrome`](../src/plugin_chrome/README.md):

- `modern.html`, `modern.css`, and `runtime.js` for WKWebView and WebView2;
- `legacy-ie8.html`, `legacy-ie8.css`, `runtime-ie8.js`, and `codec-es3.js` for
  Windows XP MSHTML.

Both entry points implement the same reducer and the same protocol. The legacy
page is not a different plugin API. It uses conservative table/absolute layout
and an ES3-safe DOM subset so old MSHTML can run it.

The complete protocol is specified in
[`HOST_BRIDGE.md`](../src/plugin_chrome/HOST_BRIDGE.md). Its important message
groups are:

| Direction | Envelopes | Purpose |
|---|---|---|
| Host to browser | `rail.snapshot`, `rail.icon` | Replace navigation state and revisioned authored icons |
| Host to browser | `theme` | Publish local ToriRS skin URLs |
| Host to browser | `page.snapshot`, `page.delta`, `page.close` | Replace, patch, or remove the sole page |
| Host to browser | `custom.bitmap` | Replace one generation/serial-fenced custom region |
| Browser to host | `rail.select` | Expand, collapse, or replace the selected page |
| Browser to host | `widget.intent` | Report a result-state semantic action |
| Browser to host | `layout`, `editor.focus` | Report allocation and HTML editor ownership |

Messages carry protocol version 1 and are copied across the boundary. Queues
are bounded and nonblocking. Unknown versions and stale generations are
ignored. The host calls `window.ToriRSPluginChrome.receive(...)`; the runtime
uses the host's copied post-message hook or bounded pull queue.

## 4. Platform hosts

| Platform | The one browser instance | Page | Attachment |
|---|---|---|---|
| Android | Deferred | None claimed | Internal BUFFER fallback until it has the common BROWSER transport |
| Web | One application-owned persistent iframe in the existing browser tab | `modern.html` | Normal-flow sibling of the game region; no per-plugin iframe or popup |
| macOS | `WKWebView` | `modern.html` | Child of the SDL window's Cocoa content view |
| Windows 10+ x64 | WebView2 controller | `modern.html` | Child of the existing main `HWND`'s chrome container |
| Windows XP | `IWebBrowser2` / MSHTML ActiveX control | `legacy-ie8.html` | Child of the existing main `HWND`'s chrome container |
| Linux | Deferred | None claimed | The former SDL surface presenter is not the final browser architecture |

The Web lane's top-level page owns one stable
[`plugin-chrome-mount`](../src/web/index.html). The host in
[`torirs_chrome.js`](../src/web/torirs_chrome.js) creates exactly one iframe in
that mount, loads `plugin_chrome/modern.html`, and keeps the iframe across every
selection and collapse. The iframe is the sole plugin-chrome browsing context
and document; it is not one iframe per plugin. No plugin can create or navigate
it, and the runtime must not create a nested iframe, popup, or second
plugin-chrome document.

Linux and Android browser integration is intentionally deferred. Documentation
and tests must not represent the removed SDL surface or Android-specific queue
as completion of the common BROWSER architecture.

### Web

The top-level browser document owns one persistent plugin-chrome iframe. Its
mount is a normal-flow sibling of the game region and remains a stable node
across plugin selection. The iframe loads the canonical modern bundle once.
Its rail remains mounted while collapse clears the page root and reduces the
allocated width. Compact exclusive mode may hide the game region, but it does
not create another plugin-chrome document.

Pop-out windows, nested iframes, and additional/per-plugin iframe instances are
outside this contract: each would add another plugin-chrome document lifetime
and complicate the single active page rule. The Web host and embedded runtime
stop pointer, touch, wheel, keyboard, and composition events before game
listeners can receive them.

### macOS

[`platform_macos_webview.m`](../src/platform/platform_macos_webview.m) embeds one
WKWebView in the SDL-created Cocoa window. The object is shared by all plugins,
tracks the trailing chrome allocation, and survives page close/collapse. It
loads a private staged copy of the modern bundle and skin, permits only files
below that root, rejects navigation and new windows, and returns copied JSON
through one `WKScriptMessageHandler`.

The macOS host uses the shared browser executor in
[`torirs_chrome_exec_winbrowser.c`](../src/ui/torirs_chrome_exec_winbrowser.c);
the filename reflects its first use, not Windows-only semantics. The platform
backend supplies the browser transport while the executor supplies the common
retained protocol projection.

The SDL window under it grows for the rail and the page only where it can:
never while zoomed or fullscreen, and never past the right edge of the display
it sits on (a frame with room on its left slides left by the overhang and
grows). Otherwise the pane opens inside the current frame and the game area
gives up the width, unless that would breach the game's floor; Close gives back
only what was grown and never moves the window
(`COMMON-WINDOW-003` in [`platform_quirks.md`](platform_quirks.md)).
`TORIRS_RESIZE_DEBUG=1` prints each decision with its reason.

### Modern Windows

[`platform_win32_webview2.c`](../src/platform/platform_win32_webview2.c) owns one
WebView2 controller inside the existing plugin-chrome child container. It does
not create a second top-level window or game renderer. The controller is
created once for the owning main window, resized between rail-only and
rail-plus-page allocations, and released at main-window shutdown.

The host stages the modern bundle and skin in an application-owned local root,
injects the copied post-message bridge, and blocks navigation, permissions,
downloads, new windows, and resources outside the allowed roots. D3D9 and GDI
continue to receive the game-only client rectangle; the browser owns only its
trailing allocation.

### Windows XP

[`platform_win32_mshtml.c`](../src/platform/platform_win32_mshtml.c) hosts the
system `IWebBrowser2`/MSHTML ActiveX control in the same child container used by
modern Windows. It is still exactly one browser instance and no auxiliary
top-level window. The application asks for IE8 standards mode when available,
but the legacy bundle deliberately stays within an IE6/7-safe ES3 DOM subset.

XP cannot rely on canvas, data URLs, CSS Grid, CSS variables, modern events, or
native `JSON`. The compatibility page uses table/absolute layout, the bundled
codec, ordinary local `IMG` URLs, and host-staged BMP/GIF resources. Transparent
images may use MSHTML's AlphaImageLoader path. Plugin icons and custom bitmaps
are opaque, revisioned local names under a per-process private directory; the
browser never receives an arbitrary plugin filesystem path.

The XP executable must keep its XP loader/import floor. Browser support comes
from the OS-provided COM control and must not add WebView2, CEF, or a post-XP
loader-time import.

## 5. Expand, collapse, and replacement

The visible shell has two retained roots:

- the rail root contains Manage Plugins plus up to 32 plugin destinations;
- the page root contains either no page or the most recently selected page.

State transitions do not navigate the browser:

1. Clicking an inactive rail icon selects it, expands the shell, and replaces
   the page root with that plugin's page snapshot.
2. Clicking the expanded selected icon collapses the page but retains the
   browser, rail, selection metadata, and icon cache.
3. Clicking the selected icon while collapsed rebuilds only that selected page
   in the existing page root.
4. Clicking another icon invalidates the old generation, clears focus and
   custom frames, and replaces the page in the same browser.
5. Application-window shutdown is the only normal path that destroys the
   browser instance.

In split mode the platform publishes a game rectangle and a browser rectangle
that do not overlap. In compact exclusive mode the browser receives the usable
window and the game is hidden. Browser blank space still belongs to the browser
and cannot generate game input.

## 6. Retained performance contract

The retained model is valuable even though presentation is HTML:

- A clean model performs no build, browser transaction, DOM walk, or bitmap
  upload.
- Compare-equal setters record nothing. A real setter mutation records the
  exact panel/widget identity and property bit; repeated writes coalesce.
- An executor tick drains O(number of recorded changes), never a whole-model
  shadow scan. Net-zero bursts open no browser transaction.
- Initial bind, explicit rebind/reconnect, and detected queue loss emit exactly
  one atomic full snapshot. Ordinary changes emit a delta and update existing
  DOM controls instead of recreating the page.
- The selected plugin is the only plugin allowed to build a page or receive
  page input/draw work.
- The one browser and one document are reused across every plugin selection,
  so engine startup, script parse, stylesheet parse, and skin decode are paid
  once per application-window lifetime.
- Rail snapshots and authored icons are gated by revisions. Custom bitmaps are
  gated by page generation, widget serial, and bitmap revision.
- The browser owns caret and composition state. A model echo does not replace a
  focused editor or overwrite the browser's current selection.

The semantic limits remain bounded: one selected page, at most 48 semantic
nodes authored by a plugin, 32 plugin registry entries plus Manage Plugins, and
host-capped command, JSON, icon, and custom-bitmap buffers. A browser bridge
must reject overflow rather than allocate without bound or block the game
thread.

## 7. ToriRS modern OSRS presentation

Both pages render from the shared skin in
[`res/plugin_chrome/skin`](../res/plugin_chrome/skin). Host-owned frame corners,
edges, panel body, buttons, checks, dropdown, scroll furniture, close mark, and
fallback wrench are composed as retained DOM elements. Plugin-authored icons
occupy only the icon well; hover, selected state, badge, attention treatment,
tooltip, and accessible label remain host-owned.

The rail is the gameframe's own stone side-tab column, not a toolbar: the
tiled `PanelBody` inside the interfaces' nine-slice frame, and the icons drawn
straight on that surface. An entry has no plate, well, or black box behind its
icon; the selected entry reads as the pressed stone (darker under the icon),
hover outlines it in the label orange.

It is `TORIRS_CHROME_M_RAIL_W` = 42 wide INCLUDING that frame, which is the
width of the gameframe's own popout strip (interface 728) standing beside it --
6 of frame rail, 30 of content, 6 of frame rail. Every host reserves the same
42 (mac points, Win32 device pixels, and the web dock's CSS pixels): a
host that reserves more shows a band of its own window background beside the
edge the page drew. The frame is laid INSIDE the rail's box, over its padding
rather than over a border, because the rail clips its own children to its
padding box; the page's panel, which has nothing clipping it, hangs the same
nine pieces outside itself instead.

The page and the rail share ONE border. Both reserve a 6px frame rail, so
placed side by side they draw two of them with a groove between; the pane
overlaps the rail's track by exactly that 6px so the two coincide and the rail,
being later in the document, paints the single visible edge.

A button is the interfaces' own three-piece red plate: the notched left cap,
the notched right cap and the tiled middle, at the authored 18px row height,
with a white caption that turns red under the cursor and shifts a pixel down
and right while pressed. It carries no border of the chrome's own — the art
has its own black outline and notched corners, and a border both squares the
ends off and (because a background is positioned against the padding box)
pushes the 18px art into the 16px space a 1px border leaves, clipping the
caps. The middle tile stops UNDER the caps rather than running the full width:
every one of these sprites is baked with a transparent bleed margin and the
caps have notched corners, so a tile that spans the whole button shows through
both and reads as tiling past the ends. The inset is the caps' measured opaque
width (16px), not their 18px cell, so the tile tucks beneath each cap instead
of stopping short of it and leaving a seam. Every piece of chrome text carries the reference's black drop shadow,
one pixel down and right: the game theme this page projects sets
`text_shadowed = 1`.

The page pane wears the same nine-slice stone frame as the rail beside it
(both reserve the 6px frame as padding, not a border, because `overflow:
hidden` clips to the padding box and a frame hung over a border is cut away),
and names itself the way a side panel does: the title in the settings orange
straight on the stone with a rule under it, the baked close mark in the
frame's corner, no black title bar.

The Manage Plugins roster and a plugin's generated settings page are the
retained chrome's own rows, presented as the in-canvas chrome draws them
(`torirs_chrome_metrics.h`): a roster row is the name in its own column, the
14px three-dot settings well in field chrome when the row opens a page, and
the tick/cross switch right-aligned in its 24px hit box. Everything left of
the switch opens the row; the switch toggles it; a locked row is all action.
The settings page keeps its staged form and `Save` / `Revert`, and carries a
`< Plugins` button back to the roster on every executor; while such a page is
up the rail keeps Manage Plugins pressed, since the page is reached from it.

The authored 1x metrics are shared with
[`torirs_chrome_metrics.h`](../src/ui/torirs_chrome_metrics.h): 18px rows, 3px
row gaps, 6px panel padding, a 104px label column, 17/18px checks, 20px tabs,
and 16px scroll furniture. Those values are logical CSS pixels. Device scale
applies to the whole page, and pixel-art assets use nearest-neighbor rendering
where the engine supports it.

Text uses the converted cache bake in
[`res/plugin_chrome/font`](../res/plugin_chrome/font/README.md), not a system
lookalike. Body/p12 and Menu/b12 retain their authored 12px glyphs, 16px line
box, and baked advances; Small retains 10px/12px. Modern engines load WOFF with
TTF fallback. XP's legacy page loads direct EOT Classic files rooted to
`file:///`. Text remains normal selectable/accessibility DOM text.

The legacy page may use older layout machinery, but it must preserve the same
silhouette, palette, spacing, skin pieces, and interaction states. Compatibility
is not permission to fall back to generic white browser controls.

## 8. Plugin API, icons, and custom regions

Registration occurs from `EV_START`; only the selected plugin builds during
`EV_PANEL_BUILD`:

```lua
function M.on_start(api)
  api.panel.request({
    title = "Panel Demo",
    icon_asset = "panel_icon.png",
    preferred_width = 320
  })
end

function M.on_panel_build(api)
  api.panel.widget("key_value", "count", "Button presses")
  api.panel.widget("toggle", "enabled", "Live updates")
  api.panel.widget("input", "note", "Note")
  api.panel.widget("dropdown", "mode", "Mode")
  api.panel.widget("button", "increment", "Increment")
  api.panel.widget("custom", "chart", "Activity chart")
end
```

`icon_asset` is a sandboxed plugin asset name, not a filesystem path, URL,
resource ID, SVG, or callback. The host decodes it once, caps it at 64x64 and
256KiB, and publishes a host-owned revision. Missing or invalid art selects the
baked fallback wrench.

A `custom` node remains semantic. `EV_PANEL_DRAW` draws through the portable
draw API into a bounded host-owned retained bitmap. Modern engines can consume
copied RGBA; XP receives a revisioned local BMP/GIF URL.
Clicks return node-local coordinates with the page generation and widget
serial. Collapse or replacement stops draw callbacks and invalidates old
frames.

## 9. Executor selection

Production manifests normally omit `executor`. The build selects its supported
web presenter. An explicit diagnostic override may use only:

```ini
[chrome]
executor=browser
```

`web` names the Emscripten page-DOM executor. `browser` names the common
embedded-browser executor used by macOS and Windows. `platform`, `android`,
`sdl`, `gdi`, and `buffer` are rejected by manifest/env parsing. BUFFER still
exists internally so a missing/refused web engine leaves usable in-canvas
chrome; it is a fallback result, never an external executor choice. Linux and
Android currently have no approved BROWSER transport.

## 10. Verification

| Target | Coverage |
|---|---|
| `make -C src test-uitree` | Retained identity, focus, damage, delta ordering, and clean-path behavior |
| `make -C src test-plugin-host` | Registry, selection generations, selected-only page build/input/draw, reload, and collapse |
| `make -C src test-web-channel` | Modern and legacy reducer behavior plus downlevel syntax/DOM compatibility and the Web DOM/channel suite |
| `make -C src test-chrome-browser-exec` | Shared embedded-browser snapshots/deltas, replacement, collapse, and intent fencing |
| `make -C src PLATFORM=win64 capture-win32-plugin-chrome` | Real WebView2 child, local bundle, authored icon, semantic page, and capture |

Platform runtime verification must also prove:

- one browser object before and after repeated selections and collapses;
- no page from a nonselected plugin in the DOM;
- no browser navigation, popup, download, permission, or network path;
- no pointer, wheel, keyboard, or composition leakage into the game;
- game/browser rectangles remain disjoint through resize, fullscreen, and
  compact exclusive transitions;
- the modern OSRS skin remains legible at each supported density; and
- the Windows XP compatibility page executes without modern syntax or DOM APIs.

## 11. Source map

| Path | Responsibility |
|---|---|
| [`src/plugin_chrome/`](../src/plugin_chrome/README.md) | Canonical modern/legacy application bundle and protocol |
| [`src/plugin/torirs_plugin_host.{h,c}`](../src/plugin/torirs_plugin_host.h) | Registry, selected page, generations, serials, and draw invalidation |
| [`src/ui/torirs_chrome_exec.{h,c}`](../src/ui/torirs_chrome_exec.h) | Semantic commands, intents, and executor selection |
| [`src/ui/torirs_chrome_rail.{h,c}`](../src/ui/torirs_chrome_rail.h) | Persistent rail snapshots and revisioned icon publication |
| [`src/ui/torirs_chrome_exec_winbrowser.c`](../src/ui/torirs_chrome_exec_winbrowser.c) | Common embedded-browser protocol projection |
| [`src/platform/platform_macos_webview.m`](../src/platform/platform_macos_webview.m) | One attached WKWebView transport |
| [`src/platform/platform_win32_webview2.c`](../src/platform/platform_win32_webview2.c) | One attached modern Windows WebView2 transport |
| [`src/platform/platform_win32_mshtml.c`](../src/platform/platform_win32_mshtml.c) | One attached XP IWebBrowser2/MSHTML transport |
| [`src/web/torirs_chrome.js`](../src/web/torirs_chrome.js) | One persistent Web iframe and the Wasm/protocol bridge |
| [`old/plugin_chrome_native/`](../old/plugin_chrome_native/README.md) | Archived native/surface prototype material |
