# CS2 DOM: the official client is the preview

Status: **Landed.** All five phases are in. The retired stack and the JavaScript
engine are deleted, the command channel is in the client, and the preview is the
official client compiled to WebAssembly with a working edit loop.
Scope: `tools/cs2dom`, plus `src/cmd`, `src/main.c` and `src/web` in the client.
Supersedes `CS2_DOM_REDESIGN_PLAN.md` and, through it,
`CS2_DOM_ARCHITECTURE_PLAN.md`. Both are kept for their measurements.

---

## Why

cs2dom was two tools fused into one.

The first is a **translation layer**, and it works. CS2 bytecode becomes
JavaScript ahead of time — 9,724 of 9,724 scripts lower, parse and run with
signed-32 semantics pinned to the C VM's own handlers. Cache interfaces become
editable TSX and go back byte-identical — 968 of 968, `.if` and `.compack`. Every
generated `.cs2` is handed to the real compiler before it is written. These are
the paths the tool exists for, and they are gated.

The second was a **from-scratch reimplementation of the client's interface
runtime**: the UITree, the IF3 layout, the emit walk, the hit tests, a canvas
painter, an asset store, a model-render bridge and a host kernel answering 999
opcodes. It was measured against the C client and it went green — 881 of 881
interfaces produced a matching draw list, command for command, dynamic component
ids included.

And the picture was still wrong.

The device-pixel transform was reset every frame, so on a 2x display the whole
interface painted into a quarter of the canvas and every pointer coordinate was
off by two. Models rendered into the widget box instead of the clip, so
pirate_combilock's dials were cropped to slivers. Sprites the painter needed but
no script ever asked for were never loaded, so bankmain drew five empty buttons.
An IF3 graphic stretches to its box and a cache graphic does not, and one code
path served both.

None of that is visible to an emit gate, because **an emit list cannot see a
painter**. The response was a second harness, at pixel level, driving headless
Chrome against the C client's own BMPs. That harness never recorded a green
interface. It was the right instrument pointed at the wrong problem: the cost was
not in the measurement, it was in maintaining a parallel renderer at all.

So: stop. There is already a renderer that is correct by definition, and it
already builds for the browser.

## What replaces it

```
if_binary + cs2_binaries          (real cache: table 3 + table 12)
      ↕  cachepack / cs2                       [C kernels, byte-exact]
if_text + cs2_text                (OSRS-Content .if + .cs2)
      ↕  cs2dom import / export
TSX + JavaScript                  (authored components, scripts as JS)
      ↓  bake
   the official client, in WebAssembly, rendering the result
```

The preview is `build-web/torirs.{js,wasm}` — the same C client, from the same
sources, built by `make -C src web`. It renders with the real toridraw, runs the
real CS2 VM against the real UITree, and hit-tests with the client's own code.
The host page drives it over a command channel.

Parity stops being a thing that is measured and starts being a thing that is
true.

### The three seams that already exist

1. **Interface-only boot, no world.** `torirs <cache> --rev <rev> <iface_id>`
   routes through `App_OpenRootInterface` (`src/main.c:3577`). Offline, the
   CLI/manifest id is the only root and no region is loaded. On the web lane the
   query string is the command line (`?arg=…`, `docs/web_build.md`).
2. **The command bus.** Every input the client accepts — keyboard, mouse,
   network bytes, window resize — arrives as a `[u32 type][u16 length][payload]`
   frame on `ToriRS_CmdBus` (`src/cmd/cmdbus.h`) and is drained once per
   iteration by `App_DrainCommands`. The ring's byte layout *is* the record file
   format, so anything sent this way records and replays for free.
3. **The browser side of that bus.** `src/web/torirs_channel.js` already speaks
   the same wire over `postMessage`, with the same 6-byte header and field
   order, explicitly so "a frame produced here can be handed straight to the
   wasm build's bus". Its `attach(win, kind)` already accepts an iframe, and
   `src/web/index.html:338` leaves the receiving hook as a named stub.

What is missing is roughly a tenth of the work: UI frame types on the bus, one
exported push function, and the host-side forwarding.

**Note.** The `TORIRS_SIM_*` env harnesses do not work on the web lane — several
call `App_BootWait`, which spins on `TaskRunner_Step` and never terminates
against an asynchronous IO backend (`docs/web_build.md`, "Not ported"). That is
precisely why the channel must be frame-loop-integrated rather than pre-loop.

---

## Part 1 — the host→client command channel (C side)

Files: `src/cmd/cmdbus.{h,c}`, `src/main.c`, `src/app.c`, `src/platform/platform.mk`,
`src/cmd/test/cmdbus_test.c`.

New frame family, packed structs in the style of `ToriRS_CmdKey`:

| type | payload | lands on |
|---|---|---|
| `TORIRS_CMD_UI_OPEN_ROOT` = 64 | `{ int32 interface_id }` | `App_OpenRootInterface` |
| `TORIRS_CMD_UI_SET_VARP` = 65 | `{ int32 id; int32 value }` | the varp side-effect path |
| `TORIRS_CMD_UI_SET_VARBIT` = 66 | `{ int32 id; int32 value }` | same |
| `TORIRS_CMD_UI_RUNSCRIPT` = 67 | `{ int32 script_id; int32 argc; int32 args[] }` | `App_RunClientScript` |
| `TORIRS_CMD_EXEC_TEXT` = 68 | debugproc text | `App_SendCommand` |

`EXEC_TEXT` is cheap and brings the whole `::` vocabulary with it.

Drain them in `App_DrainCommands` beside the input and network cases. One export
in `src/main.c`:

```c
EMSCRIPTEN_KEEPALIVE int torirs_cmdbus_push_bytes(const uint8_t* p, int len);
```

walking the batch into the file-scope `static struct ToriRS_CmdBus bus`
(`src/main.c:709`), with the symbol added to `EXPORTED_FUNCTIONS` in the web
lane's block of `platform.mk`. A malformed frame from our own codec is a caller
bug, so it asserts (see `CLAUDE.md`).

Client→host status is one optional `EM_JS` hook, `window.torirsOnClientEvent(json)`
— boot complete, root opened, CS2 error — following
`torirs_chrome_exec_web.c`: a page that defines no hook degrades, and nothing
ever makes the renderer wait on the page.

**`-sMODULARIZE=0` stays.** The preview embeds the stock shell in an **iframe**
rather than importing a module into the dev page: no build-lane change, globals
isolated per instance, and the channel already attaches to iframes.

Proof before any browser is involved: extend `cmdbus_test.c`, then replay a
`.trscmd` containing `UI_OPEN_ROOT` + `UI_SET_VARP` through the native client
under `TORIRS_EXIT_BMP` and look at the BMP.

## Part 2 — the dev tool

`src/dev_canvas.js` + `src/dev_page_canvas.js` are replaced by a slimmer
`src/dev_client.js` + `src/dev_page_client.js`, still reached as `cs2dom dev`.

**Server.** Serves the dev page and the client assets out of `build-web/`,
refusing to start without `torirs.wasm` and naming `make -C src web`. Spawns
`src/build/io_server` against the preview cache — the same child-process shape
`src/model.js` used for `ev_server`, and the tested cache backend. Watches the
project's sources and rebakes on change. Keeps three things the current server
got right: the build stamp in the status line, the generation guard that stops a
stale tab running old code, and a closed stdin answering no rather than hanging.

**Page.** The preview pane is the client iframe. Interactivity is the client's
own — SDL owns the canvas, so hover, clicks, menus and scrolling need no ported
hit test. The state pane encodes bus frames with `torirs_channel.js` and forwards
them in, where a small shim calls `_torirs_cmdbus_push_bytes`: varp and varbit
setters, open-by-id, run script, and a `::` box. A rebake reloads the iframe with
the same query; hot group-reload over the channel is a later question, not this
plan's.

The records pane keeps showing a script as `.if`, `.cs2` and JavaScript — that is
text-level translation and needs no runtime.

## Part 3 — the deletion sweep

**Phase 0 (landed).** The whole retired stack came out: the C VM in the browser
and its bridge, the engine router, the TypeScript interpreter and bytecode
decoder, the worker protocol and its controller, the tree store, the 6,951-line
host runtime, the three React renderers (the only React importers in the tool),
the cooperative font runtime, the old dev server and page, and the schema
migration artifacts that no longer had a generator. With them went their test
suites, their make targets, and React, react-dom and esbuild from `package.json`.
About 23,000 lines. `RETIRED.md` is gone too — it existed to say what to delete,
and git is the record now.

**Phase 4.** The JavaScript engine itself, once the wasm preview stands:
`uitree.js`, `layout.js`, `emit.js`, `hit_test.js`, `painter.js`, `assets.js`,
`asset_loader.js`, `session.js`, `browser_runtime.js`, the model-render chain
(`model_source.js`, `model_render_{worker,controller,protocol}.js`, `model.js`),
`cs2_driver.js`, `transmit_pump.js`, every `host_*.js`, and `emit_parity.js`.

Both harnesses go with it — `capture_emit_reference.mjs`,
`verify_emit_parity.mjs`, `verify_pixel_parity.mjs`, `probe_canvas.mjs` and the
emit fixtures. **`scripts/cdp.mjs` stays**: a 173-line dependency-free Chrome
DevTools client is exactly what the new smoke test needs. The C-side dump
env vars stay too; other tools use them.

**What is kept, and is the point of the tool:** `cs2_js_emit.js`,
`cs2_intrinsics.js`, `js_to_cs2.js`, `verify.js`, the generated host surface and
park tables with their generators, `emit_corpus_js.mjs`, `tsx_import.js`,
`if_record.js`, `ir.js`, `emit_if.js`, `emit_cs2.js`, `transform.js`, `expr.js`,
`runtime.js`, `components.js`, `ops.js`, `loader.js`, `export.js`, `ledger.js`,
`build.js`, `cachegen.js`, `dat2.js`, `content*.js`, `pack.js`, `png.js`, and the
round-trip gates.

## Phases

- **Phase 0 — clear the dead weight.** *Landed.*
- **Phase 1 — the channel, C side.** *Landed.* `TORIRS_CMD_UI_OPEN_ROOT`,
  `UI_SET_VARP`, `UI_SET_VARBIT`, `UI_RUNSCRIPT` and `EXEC_TEXT` on the bus,
  drained in `App_DrainCommands` onto the `App_*` calls their SIM harnesses
  already use; `torirs_cmdbus_push_bytes` exported from the web lane, validating
  its batch rather than asserting it, because those bytes are written by another
  implementation and `OPT=1` compiles asserts out. Proved by a `.trscmd` replay:
  interface 600 renders in full, and a varp frame reads back its value. The same
  work found `test-cmdbus` passing vacuously — `assert()` is how it checks and
  `-DNDEBUG` was compiling every check out — now fixed with `-UNDEBUG`.
- **Phase 2 — the preview.** *Landed.* `src/dev_client.js` serves `build-web/`,
  spawns an io_server and proxies its cache traffic so the iframe stays
  same-origin; `src/dev_page_client.js` is the three-pane page. `cmd_frames.js`
  encodes bus frames, `test/cmd_frames_test.js` pins their octets, and
  `verify_cmd_wire.mjs` proves C reads them. `smoke_client.mjs` replaced both
  parity harnesses.

  Three bugs, all the same shape — something failed without saying so. io_server
  refuses an absolute cache directory outright, exited, and my stderr filter kept
  only lines matching /error|failed/ so the one sentence explaining it was
  dropped. The client's cache directory is its first positional and omitting it
  slid the interface id into its place, booting against a cache named "600". And
  the smoke test failed a healthy boot on console.error, because the client is a
  C program and its stderr arrives that way.

- **Phase 3 — the edit loop.** *Landed.* Watch → cachepack `--asset-only` into a
  copied preview cache → restart io_server → reboot the client with
  `cache_reset=1`. Every one of those three is load-bearing: io_server holds the
  cache open, the client keeps archives in IndexedDB, and a failed bake must not
  reboot or the last good bytes look like the edit. `verify_edit_loop.mjs` is the
  only gate that can catch a preview reading a cache nobody writes to.

- **Phase 4 — delete the JavaScript engine.** *Landed.* The tree went from ~78
  modules to 26 and from 21 test files to 5. Gone: `uitree`, `layout`, `emit`,
  `hit_test`, `painter`, `assets`, `asset_loader`, `session`, `browser_runtime`,
  the model-render chain, `cs2_driver`, `transmit_pump`, every `host_*`, the old
  dev server and page, the content/asset readers that fed them, both parity
  harnesses and the benches that measured them.

## Gates

| gate | command | asks |
|---|---|---|
| the translation suites and the `.if` round trip | `make -C tools/cs2dom test` | 1,936 identical, 0 differing |
| every decompilable script lowers | `make -C tools/cs2dom corpus-aot` | 9,724 / 9,724 |
| the bus frames | `make -C src test-cmdbus` | 9 passed |
| does C read what JavaScript writes | `make -C tools/cs2dom verify-cmd-wire` | the wire |
| does the preview boot and draw | `make -C tools/cs2dom smoke-client` | liveness |
| does an edit reach the screen | `make -C tools/cs2dom verify-edit-loop` | the loop |

The smoke test replaces both parity harnesses: start the dev server, open an
interface, wait for the boot fact, screenshot the canvas, and assert that
something was drawn and nothing errored. It is a liveness check, not a
correctness one — correctness is the client's now.

## Notes

- `io_server` with the wire-cache lane is the recommended cache path. `web-idb`
  works but complicates invalidation on rebake.
- Reload time is the edit-loop latency. Offline interface-only boot is the
  cheapest boot the client has; if it is still too slow, hot reload over the
  channel is the follow-up.
- Web-lane rendering defects now surface in the preview instead of painter
  defects. `TORIRS_EXIT_BMP` and `TORIRS_GL3_READBACK` on the native build stay
  the debugging oracle.
