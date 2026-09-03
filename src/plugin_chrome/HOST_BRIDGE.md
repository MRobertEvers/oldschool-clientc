# ToriRS plugin-chrome host bridge, protocol 1

`src/plugin_chrome/` is the canonical embedded UI bundle for every browser-backed
plugin-chrome host. Hosts load exactly one of:

- `modern.html` — current Chromium/WebView2, WKWebView, WebKitGTK, and the real
  web client. Its shipped JavaScript is still downleveled to ES5.
- `legacy-ie8.html` — Windows XP `IWebBrowser2`/MSHTML. It requests IE8 mode
  when present but uses an IE6/7-safe ES3 DOM subset and bundled JSON codec.

Both pages implement the same state machine: one persistent rail, zero or one
selected page, and no plugin-authored HTML or JavaScript.

Both pages also use the exact authored 1x metrics from
`src/ui/torirs_chrome_metrics.h`. CSS pixels are chrome logical pixels: device
pixel ratio scales the whole page on dense/touch displays. Hosts should not
inflate the 18px row, 3px gap, 6px padding, 104px label column, 17/18px checks,
20px tabs, or 16px scrollbar independently.

The local `font/` sibling is part of the bundle, not an optional system-font
hint. Body/Menu use the cache-derived 12px/16px faces (weights 400/700) and
badges use Small at 10px/12px. Modern hosts stage WOFF and TTF; XP stages EOT
with its `file:///` root string.

The real Web host keeps one `plugin_chrome/modern.html` iframe in the stable
application chrome slot. Its outer adapter translates legacy wasm call points
into these envelopes and owns split/exclusive game allocation; the iframe is
not recreated for a plugin selection or collapse, and no plugin receives a
frame of its own.

## Trust and transport boundary

The bundle is local application code. It must be served from an application
origin such as `https://torirs.local/` or loaded from packaged resources. It
performs no network requests. A host must not navigate the view and must reject
new-window, download, permission, and external-scheme requests.

XP hosts should copy the bundle into a per-process private temporary directory,
create a `bitmap/` child there, load the page from that root, and publish only
relative opaque names such as `bitmap/481-8.bmp`. This avoids both writes beside
a read-only installation and an unrestricted `file:///` URL capability. The
host owns directory permissions, atomic replacement, and shutdown cleanup.

The host injects one optional outbound function before `runtime.js` runs:

```js
window.torirsPluginChromePostMessage = function (jsonString) { /* copy it */ };
```

The runtime exposes one inbound function after boot:

```js
window.ToriRSPluginChrome.receive(messageObjectOrJsonString);
```

Every message is copied. Calls return synchronously and never wait for another
thread. The runtime also keeps a bounded 64-message queue for pull-based hosts:

```js
window.ToriRSPluginChrome.takeMessage(); // JSON string, or ""
window.ToriRSPluginChrome.takeIntent();  // legacy raw widget intent, or ""
```

A host may install the optional typed fast path. After a widget intent passes
the runtime's current-page checks it receives a new plain object, never a live
DOM/model object:

```js
window.torirsChromeIntentPosted({ k, p, w, v, text, x, y, g, s });
```

When this typed hook exists, `widget.intent` is not also sent through the JSON
post function. Rail and layout messages still use the JSON function.

## Inbound envelopes

All envelopes carry `protocol: 1` and a string `type`. Unknown versions or types
are ignored.

### `rail.snapshot`

Complete application-owned navigation state:

```js
{
  protocol: 1,
  type: "rail.snapshot",
  registryRevision: 9,
  selectionGeneration: 22,
  pageGeneration: 17,
  activePlugin: 4,
  lastSelectedPlugin: 4,
  selectedEntry: 4,       // plugin index, -2 = permanent Manage Plugins
  expanded: true,
  entries: [{
    kind: 1,              // 1 = Manage, 2 = plugin
    pluginIndex: -2,
    preferredWidth: 320,
    title: "Manage Plugins",
    iconAsset: "",
    badge: "",
    attention: false
  }]
}
```

The maximum is 33 entries: permanent Manage plus all 32 plugin slots. A
registry revision change rebuilds rail buttons. Selection-only changes update
the retained buttons in place. `expanded: false` removes page controls but not
the rail.

### `rail.icon`

One revisioned icon cache update:

```js
{
  protocol: 1,
  type: "rail.icon",
  pluginIndex: 4,
  revision: 3,
  width: 32,
  height: 32,
  url: "torirs://bitmap/rail/4/3"
}
```

`url` must resolve locally. Modern hosts may instead provide
`rgbaBase64`, containing tightly packed row-major RGBA8. IE8 hosts must provide
a local URL because IE8 has no canvas. Source dimensions are at most 64×64.
A 0×0 update or an invalid/missing URL selects the host-supplied baked wrench.

### `theme`

Host-owned local image URLs. Recognized names are `panelBody`, `pluginIcon`,
`buttonLeft`, `buttonMiddle`, `buttonRight`, `checkOn`, `checkOff`,
`checkBoxOn`, `checkBoxOff`, `dropdownBody`, `scrollUp`, `scrollDown`,
`scrollTrack`, `scrollGripTop`, `scrollGripMiddle`, `scrollGripBottom`, `close`,
`frameTopLeft`, `frameTop`, `frameTopRight`, `frameLeft`, `frameRight`,
`frameBottomLeft`, `frameBottom`, and `frameBottomRight`. IE8 tiles/stretches
these through ordinary table/absolute elements; modern CSS uses the same URLs.

```js
{ protocol: 1, type: "theme", revision: 2, assets: { /* name: local URL */ } }
```

### `page.snapshot`

Atomically replaces the sole page. `commands` is a complete command image in
the fixed `ToriRSChromeCmd` vocabulary below.

```js
{
  protocol: 1,
  type: "page.snapshot",
  pageGeneration: 17,
  panel: 3,
  title: "Loot Tracker",
  checkStyle: 0,
  commands: [ /* command objects */ ]
}
```

### `page.delta`

Patches retained controls without rebuilding them. It is accepted only when
`pageGeneration` exactly matches the active page.

```js
{ protocol: 1, type: "page.delta", pageGeneration: 17, commands: [ /* ... */ ] }
```

### `page.close`

Clears only the selected page. The persistent rail remains.

```js
{ protocol: 1, type: "page.close", pageGeneration: 17 }
```

### Command objects

Command keys intentionally mirror the fixed C POD:

```js
{ k, p, w, tab, v, c, x, y, cw, ch, label, text, detail, s }
```

`s` is the semantic widget serial copied alongside `WIDGET_ADD`; zero is
allowed for built-in/legacy controls. Command kinds are:

- 3 panel open, 4 close, 5 title, 6 rect, 7 selected tab
- 8 widget add, 9 remove, 10 label, 11 text, 12 checked, 13 hidden
- 14 color, 15 selected value, 16 focus, 17 option-count, 18 option
- 19 global checkbox style

Widget kinds on ADD are 0 label, 1 checkbox, 2 text input, 3 separator,
4 menu item, 5 dropdown, 6 model view, 7 button, 8 tab strip, 9 list row,
10 color picker, 11 textarea, 12 custom bitmap region, and 13 free/hidden.
For a list row, `cw` carries the row shape bits: 1 = the row opens a page
(the runtime adds the three-dot settings well and sends intent 2 from the name
and the well), 2 = locked (no switch; the whole row is the action). A row with
neither bit toggles from anywhere, including its name.
For a custom widget, `ch` is its bounded preferred height in 1x chrome logical
pixels; the bitmap is fitted to that retained box and its source aspect ratio
does not resize the page.

For a structured select, command 17 carries `x=1`. Each following command 18
carries its index in `v`, stable value in `text`, presentation label in
`label`, enabled state in `x`, and optional availability explanation in
`detail`. Labels may be duplicated and may contain `|`; they are never parsed
as identity. A disabled row may remain selected so an unavailable saved value
stays visible, but the runtime cannot emit a pick for it.

### `custom.bitmap`

Replaces one custom widget's retained bitmap only when all identities match:

```js
{
  protocol: 1,
  type: "custom.bitmap",
  pageGeneration: 17,
  panel: 3,
  widget: 12,
  widgetSerial: 481,
  revision: 8,
  scaleMilli: 2000,
  width: 600,
  height: 240,
  url: "torirs://bitmap/custom/17/481/8"
}
```

Modern hosts may use `rgbaBase64` instead of `url`. XP MSHTML uses `IMG` and
requires the URL form; the embedding host may use a private relative BMP/GIF or
implement the `torirs://bitmap/` resource scheme from its copied POD bitmap cache. A frame from an old page
generation or recycled widget serial is ignored.

## Outbound envelopes

### `rail.select`

```js
{
  protocol: 1,
  type: "rail.select",
  sequence: 12,
  pluginIndex: -2,
  selectionGeneration: 22
}
```

The button captures the generation of the snapshot that created/updated it.
The application rejects a stale generation. Selecting the expanded selected
entry collapses it; selecting it while collapsed expands it; selecting another
entry replaces the sole page. `-2` applies those same rules to Manage Plugins.
`-1` is never sent.

### `widget.intent`

`intent` is the copied executor intent shape:

```js
{
  protocol: 1,
  type: "widget.intent",
  sequence: 13,
  intent: { k: 3, p: 3, w: 9, v: 1, text: "", x: 0, y: 0, g: 17, s: 481 }
}
```

Intent kinds are 1 activate, 2 list-row action, 3 toggle, 4 text result,
5 dropdown pick, 6 tab, 7 close, and 8 custom-region activate. `g` is the
active page generation and `s` the widget serial captured by the listener.
The host rejects either mismatch before mutating the model or invoking a plugin.
For a structured dropdown pick, `v` is the row index and `text` is that row's
stable value. Both must still match the retained enabled row; this additionally
rejects an intent queued before an option list changed in place.

### `layout`

The view reports copied neutral facts, never a platform name:

```js
{
  protocol: 1,
  type: "layout",
  sequence: 14,
  selectionGeneration: 22,
  pageGeneration: 17,
  width: 320,
  height: 500,
  scaleMilli: 2000,
  sizeClass: 1,
  visible: true,
  gameVisible: true
}
```

The embedding host remains authoritative when it knows a more exact allocation
(for example, whether compact mode hid the game). It may replace these values
before putting the corresponding POD event on the frame-thread queue.

### `editor.focus`

The modern and legacy runtimes emit this only on a deduplicated ownership
edge for an `INPUT`, `SELECT`, or `TEXTAREA`. Focus-out is deferred one task so
moving directly between two editors does not briefly hand the IME back to the
game.

```js
{
  protocol: 1,
  type: "editor.focus",
  sequence: 15,
  focused: true,
  pageGeneration: 17
}
```

The host uses it to suppress game keyboard/inset handling while HTML owns the
editor. Closing or replacing the page always emits `focused: false` if needed.

## Host implementation checklist

1. Load the correct local page and disable navigation/network capabilities.
2. Install the outbound hook before the runtime script executes.
3. Call `receive()` only on the browser UI thread and copy every input first.
4. Publish `rail.snapshot` before page startup; keep the view/rail alive after
   `page.close`.
5. Cache `rail.icon` and `custom.bitmap` resources by revision. Never hand a
   JavaScript view a plugin-owned pointer.
6. Capture selection generation in rail events and page generation/widget
   serial in widget events; validate all three again on the frame thread.
7. Keep queues bounded and nonblocking. Coalescing layout/bitmap state is safe;
   discrete selection and activation ordering is not.
