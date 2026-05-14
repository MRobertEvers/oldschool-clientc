# Emscripten build for browser.c → dist/browser.js + dist/browser.wasm

`make` also copies `public/shell.html` to `dist/index.html` and copies every `public/*.js` into `dist/`.

After `make`, serve `dist/`, for example:

    python3 -m http.server -d dist 8080

Then open http://localhost:8080/ or http://localhost:8080/index.html
