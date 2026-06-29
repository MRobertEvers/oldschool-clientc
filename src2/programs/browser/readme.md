# Emscripten build for browser.c → dist/browser.js + dist/browser.wasm

`make` also copies `public/shell.html` to `dist/index.html` and copies every `public/*.js` into `dist/`.

After `make`, serve `dist/`, for example:

    python3 -m http.server -d dist 8080

Then open http://localhost:8080/ or http://localhost:8080/index.html

## Memtrace (heap instrumentation)

Build with malloc tracing and the in-browser heap viewer:

```bash
make MEMTRACE=1 clean all
python3 -m http.server -d dist 8080
```

Click **Memtrace** in the page header to open the viewer in a new tab. See [`../../../tools/memtrace/README.md`](../../../tools/memtrace/README.md) for how tracing works, the binary format, native decoding, and viewer features.
