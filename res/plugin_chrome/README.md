# Plugin chrome resources

- `skin/` contains the application-owned ToriRSChrome image bake used by every
  browser host.
- `font/` contains reproducibly converted cache Body, Menu, and Small fonts in
  TTF, WOFF 1, and IE8 EOT formats.
- `screenshots/` contains real platform runtime captures. They are not design
  mockups or generated reference scenes.

| Capture | Runtime source | SHA-256 |
|---|---|---|
| `android-webview-expanded.png` | Attached Motorola XT1060, Android 5.1/API 22, Chrome 39 WebView, captured from the live device WebView debugging endpoint because the device compositor omits the hardware layer | `6c49fcca3674284106ffe47e447a8f53148596501dde6719681fee8ff3672c8e` |
| `macos-wkwebview-expanded.png` | Live local ToriRS process with WKWebView embedded in its SDL/Cocoa window | `af2d0cf1a416d75fe5838600c678bd43594d42bf60ea7a9380b657cf0770e6b2` |
| `web-chrome-expanded.png` | Actual WebAssembly client in Google Chrome with its one persistent canonical-bundle iframe | `33e2b4b9ca04908787d5b0ce2814ea1f35fcab6e738bf233f32067f31e42411f` |
| `windows-webview-expanded.png` | Authorized WireGuard Windows machine, real WebView2 `CapturePreview` composed in the production main-child allocation | `61615a6dd80c2db8a45e3dd7d74c089cbb58b82d5926ebdd5c14a386c9dd3c73` |

Linux is intentionally omitted from this capture set by user direction.
