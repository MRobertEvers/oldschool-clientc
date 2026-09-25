# Shared plugin chrome

This directory is the platform-neutral browser UI for the one ToriRS plugin
shell. See [HOST_BRIDGE.md](HOST_BRIDGE.md) for the versioned host contract.

The modern and IE8 pages deliberately do not load plugin files. Plugins publish
only bounded metadata, semantic controls, result-state intents, and—only for
the explicit low-level custom well—bitmap pixels through the native host.

`runtime-source.js` is the readable source. `runtime.js` and `runtime-ie8.js`
are identical ES5 outputs so current browser hosts and XP MSHTML execute the
same reducer. Regenerate them with:

```sh
tsc --allowJs --target ES5 --module none --outFile runtime.js runtime-source.js
cp runtime.js runtime-ie8.js
```

The runtime deliberately avoids newer library calls as well as newer syntax;
`test/compat_test.js` enforces that constraint. The separate legacy HTML/CSS
selects XP's table/absolute layout and AlphaImageLoader/host-URL image
behavior.

CSS dimensions are the authored 1x chrome metrics from
`src/ui/torirs_chrome_metrics.h`: form controls use the 18px row grid and 3px
gap, panels use 6px padding, labels use a 104px column, checks use their native
17/18px art, tabs are 20px, and scroll furniture is 16px. Semantic readouts
retain their kind: headings, inset key/value cards, progress meters, alerts,
and two-line action cards use the extra height their contents require. All are
logical CSS pixels. A DPR-2 WebView scales the complete composition to physical
pixels; hosts must not independently double individual controls.

The `font/` directory staged beside each page is generated from the same baked
cache masks and advances as native ToriRSChrome. Modern engines use Body/Menu
WOFF with TTF fallback; XP uses direct EOT sources. Small is the authored
10px/12px badge face. No page fetches a system or network font.

The real Web client mounts `modern.html` in one persistent app-owned iframe.
`src/web/torirs_chrome.js` only translates wasm hooks to protocol 1 and owns
split/exclusive allocation; it does not build plugin controls and never creates
a frame per plugin.
