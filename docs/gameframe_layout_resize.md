# Gameframe layout — the client canvas, window resize, and window mode

Written 2026-08-02. Topic doc for the rev-230 gameframe's *layout* half: what
size the client canvas is, who is allowed to change it, and what the cache's own
layout scripts do when it changes. Read [`PORTING_GUIDE.md`](PORTING_GUIDE.md)
§5 first — this is squarely the "modern feature with no LostCity reference"
pattern.

**LostCity reference (§2.2, run):**
`grep -rilE 'resizable|windowmode|window_mode|windowstatus'` over both
`LostCity_Server/engine/src` and `LostCity_Server/content/scripts` returns
**zero hits**. Rev 254 (2004) is fixed-512×334-only; there is no proc, no config
field and no packet to port. The client already implements the whole feature in
CS2 (scripts 901 / 907 / 909 / 3998, all present and correct in `cache.osrs239`);
what the engine owed was the *mechanism* — one canvas, one resize event, one
dispatch, and four host ops.

---

## 1. What the layout family actually is

There is no script named `gameclient*` anywhere: `grep -rin gameclient` over a
full decompile of `cache.osrs239` (9,433 of 9,725 scripts), `docs/`, `src/` and
`OSRS-Content` returns exactly one unrelated hit (`src/net/rev/osrs230/packetout.h`,
an RSProt "GameClientProt" comment). The layout family is the **toplevel**
family:

| script | name | what it is |
|---|---|---|
| 901 | `[clientscript,toplevel_init]` | the toplevel's onLoad. `if_setonresize("toplevel_resize(event_com,$enum1)", $component0)` — this is where the layout listener is registered |
| 909 | `[proc,toplevel_resize]` | **the layout script.** Sizes the viewport trackers 161:92/94 from `viewport_geteffectivesize`, the HUD containers 161:7/15 from the modal insets, and positions 161:16/19/9 |
| 907 | `[proc,toplevel_redraw]` | calls `~toplevel_resize` directly, so the layout also runs at boot with no resize event |
| 900 | `[proc,toplevel_getcomponents]` | `if_gettop` → component-remap enum: 601→1745, 161→1130, 164→1131, 548→1129, 165→1132, 80→139 |
| 3998 | `[clientscript,settings_client_mode]` | the Display panel's fixed/resizable dropdown. Its entire body is `setwindowmode` + `setdefaultwindowmode` |

The toplevels are all named in the pack
(`OSRS-Content/osrs239-content/pack/3_interfaces.pack`): 80 `toplevel_spectator`,
161 `toplevel_osrs_stretch`, 164 `toplevel_pre_eoc`, 165 `toplevel_display`,
548 `toplevel`, 601 `toplevel_osm`. `manifests/manifest_osrs230.ini` boots **161**.

**The layout script was never the gap.** It runs correctly and always did.

---

## 2. The bug: three copies of the canvas size, written independently

The canvas existed as three unrelated variables:

| copy | where | who wrote it |
|---|---|---|
| layout root | `src/ui/uitree_layout.c` `UITree_LayoutRootWidth/Height` (765×503), read through `UITREE_LAYOUT_ROOT_W/H` at ~25 sites | `UITree_LayoutSetRootSize`, called once from `TORIRS_ROOT_SIZE` in `main.c` |
| VM canvas | `RS_CS2Host.viewport_w/h`, handed to the VM by `CS2VM2_ThreadSetCanvas` and returned by `GETCANVASSIZE` **and** `VIEWPORT_GETEFFECTIVESIZE` | hardcoded 765×503 in `RS_CS2Host_Init`, **never written again** |
| backbuffer | `PlatformSDL2.width/height` | `PlatformSDL2_Init`, never again |

Measured before the fix, `TORIRS_ROOT_SIZE=1024x768`:

```
script 909 pc=63  IF_GETWIDTH            -> 1024   (correct)
script 909 pc=348 VIEWPORT_GETEFFECTIVESIZE -> 503 (stale)
BOUNDS (161|92) abs=108,132 765x503      # a 765x503 island inside a 1024x768 frame
BOUNDS (161|94) abs=108,132 765x503
```

Nothing errored. `toplevel_resize` did exactly what it was told: it asked the
host how big the viewport was, was told 503, and sized the viewport to 503. The
only way to see it was the bounds dump.

**This is the trap that will recur.** Updating two of the three reproduces it
exactly. `App_SetCanvasSize` (`src/app.h`, `src/app.c`) is now the only writer;
the backbuffer follows the canvas in `main.c` after the command drain, never the
window (see §4). Do not add a fourth copy.

---

## 3. What landed

### 3.1 One setter — `App_SetCanvasSize(app, w, h)`

`src/app.c`. Clamps to `APP_CANVAS_MIN_W/H` (765×503 — see §6), writes the
layout root *and* `host.viewport_w/h`, relayouts, dispatches every registered
onResize listener, relayouts again. Returns 1 if the size changed.

`App_Init` calls it once with the current layout root, so the `TORIRS_ROOT_SIZE`
debug knob and the boot both go through it and the host's copy can no longer
start out disagreeing.

### 3.2 A whole-tree onResize dispatch — `app_dispatch_resize_hooks`

`IF_SETONRESIZE` was registered at open time and dispatched from exactly two
places: the script-driven `if_callonresize` queue, and `IF_OPENSUB`'s step 8,
which is gated on `if( self->target_uid >= 0 )` — subs only. So an `IF_OPENTOP`
root's listener (script 901 registers it at pc=11) was never fired by an actual
resize, because there were no actual resizes.

The new dispatch collects every component with `runtime_hooks.on_resize.script_id > 0`
and runs it through `RS_CS2_DispatchHook`, the same shape as the
`if_callonresize` drain. **Component ids are snapshotted before dispatch**: the
listeners `cc_create`/`cc_delete`, which reallocates `tree->components`.

### 3.3 A resize event path

- `TORIRS_CMD_WINDOW_RESIZE` (48) on the command bus, `struct ToriRS_CmdWindowResize {w,h}`.
  On the bus rather than a direct call so a resize lands in the recorded stream
  and replays at the frame it happened.
- `SDL_WINDOWEVENT_SIZE_CHANGED` (not `RESIZED` — a programmatic
  `SDL_SetWindowSize` must relayout too) pushes it, **coalesced to one per poll
  batch**: a window drag emits one event per mouse-move and each would cost a
  relayout plus every gameframe onResize script.
- `App_DrainCommands` applies it via `App_SetCanvasSize`.
- `PlatformSDL2_SetCanvasFollowsWindow(platform, bus, follow)` is the mode gate.
  Off by default: fixed mode keeps a 765×503 backbuffer and letterboxes.

### 3.4 The four window-mode ops

`GETWINDOWMODE` (5306) and `GETDEFAULTWINDOWMODE` (5308) pushed the literal `2`.
`SETWINDOWMODE` (5307) and `SETDEFAULTWINDOWMODE` (5309) had **no case in the
dispatch at all** — they fell to `StackMetaStub`, whose generated meta for both
is `{1,0,0,0,known=1}`: pop the arg, return OK, do nothing, no assert and no
survey line. Since 3998's whole body is those two ops, the Display panel's
client-mode dropdown was inert *and looked implemented*.

Now: `RS_CS2Host.window_mode` / `.default_window_mode` are host state,
snapshotted per VM thread beside the canvas (`CS2VM2_ThreadSetWindowMode`); the
GET ops read it; the SET ops pop, mirror into the thread (so 3998's
set-then-`getdefaultwindowmode` sees its own write) and raise
`host.window_mode_dirty`; `App_TakeWindowModeChange` drains it and `main.c`
decides, because fixed-vs-resizable is a statement about the *window* and the
App has no platform. Same split as `close_modal_requested`.

`enum CS2VM_WindowMode { FIXED = 1, RESIZABLE = 2 }` lives in
`src/cs2vm2/cs2vm2_host.h`. **This is opcode surface, not content** (§2.4 item
3): the numbers belong to the CS2 dialect the client implements, alongside
`CS2_OP_*` and `clienttype`; no cache record and no content pack states them.
The authority is the dialect's own type table (runestar cs2
`windowmode-names.tsv`: `1 fixed`, `2 resizable`).

### 3.5 Three dev knobs

- `TORIRS_SIM_RESIZE="frame,WxH[;frame,WxH]"` — push a **canvas** resize onto
  the bus at a main-loop frame. It walks past the follow gate, so it exercises
  the layout scripts in either window mode and can therefore never tell the two
  modes apart. Use it to test the layout, not the mode.
- `TORIRS_SIM_WINDOW="frame,WxH[;frame,WxH]"` — resize the **window**, i.e. do
  what a user's drag does (`PlatformSDL2_SetWindowSize`). Everything after that
  is the client's own decision, so this is the only knob that tests the follow
  gate. **Correction to an earlier claim in this doc:** `SDL_VIDEODRIVER=dummy`
  does deliver `SDL_WINDOWEVENT_SIZE_CHANGED` for a programmatic
  `SDL_SetWindowSize` — measured 2026-08-02, the canvas follows it headlessly.
  What the dummy driver has no way to produce is a *user* drag; a synthesised
  one is indistinguishable from here down.
- `TORIRS_SIM_RUNSCRIPT="frame,script[,arg...]"` — run a clientscript by id.
  Needed because **nothing binds 3998**: a full decompile of `cache.osrs239`
  contains no caller, and no `onop=` in the content tree names it, so there is
  no component for `TORIRS_SIM_HOOK` to click. (§5.4 finding 1 again: "not in
  the corpus" is routine.)
- `TORIRS_RESIZE_DEBUG=1` prints each canvas change.

---

## 3A. The half that was still missing: nobody told the *platform* the mode

Written 2026-08-02, second pass, after the user reported "in resizable mode the
UI scales instead of resizing".

Everything in §3 was in place and the client still scaled. The reason is one
missing read at boot:

| | says | measured |
|---|---|---|
| `RS_CS2Host_Init` | `window_mode = RESIZABLE` | every clientscript's `getwindowmode` answers **resizable** from the first frame |
| `PlatformSDL2.canvas_follows_window` | starts `false` | a real window resize was **dropped in the poll loop** |

So the client told its own scripts it was resizable, and then letterboxed and
upscaled a 765×503 canvas into whatever window it was given. Nothing could clear
that: `PlatformSDL2_SetCanvasFollowsWindow` was only ever called from the
post-frame `App_TakeWindowModeChange` drain, which fires only when a script
*changes* the mode — and **nothing binds 3998**, so no script ever did (§3.5).
The mode was unreachable and the two halves disagreed for the whole session.

### 3A.1 What landed

- **`App_WindowMode(app)` / `App_SetBootWindowMode(app, mode)`** (`src/app.c`,
  `src/app.h`). The shell reads the mode once, right after `App_Init` and before
  `App_OpenRootInterface`, and hands it to
  `PlatformSDL2_SetCanvasFollowsWindow` — the same call the runtime switch
  makes, so the two paths cannot drift. `App_SetBootWindowMode` deliberately
  does **not** raise `window_mode_dirty`: config is not a user action.
- **A stateable mode.** `[ui:boot] windowmode = fixed|resizable` in the boot
  manifest, `--windowmode fixed|resizable` on the CLI (CLI wins). Unset keeps
  the host default, which is resizable.
- **A stateable boot size.** `[ui:boot] window = WxH`, `--window WxH`. The
  window is created from the layout root, so this sizes the *window* as well as
  the canvas. `TORIRS_ROOT_SIZE` still beats both.
- **The canvas floor is now the window's minimum size**
  (`SDL_SetWindowMinimumSize`, both modes). Below 765×503 layout is not an
  answer the client has — the canvas clamps and the present scales — so the
  window is not allowed there rather than silently scaling.
- **Fixed mode snaps the window to the floor, and resizable puts it back.**
  Entering fixed is the one window change the user did not ask for, so
  `PlatformSDL2` remembers the size it snapped away from (`resizable_w/h`) and
  restores it when the mode goes back. Without that, a round trip through the
  Display dropdown shrank the window permanently — measured, then fixed.
- **`--window` no longer needs `--windowmode`.** The boot-size branch tested
  `cfg.window_mode == RESIZABLE`, but `cfg.window_mode` is 0 when nobody stated
  one, and unstated means *the host default*, which is resizable — so a plain
  `--window 1440x900` booted at 765×503. The test is now `!= FIXED`. This is the
  same class of bug as the one above: a mode compared against the wrong copy of
  itself.

### 3A.2 What it costs

The mode is boot config, not an in-game setting. `[clientscript,settings_client_mode]`
(3998) is still unbound — re-measured this pass: no `3998` hook in the mounted
tree's 1,737 hooks, none in interface 134 (`settings`) static data
(`tools/dump_interface --iface 134 --json`), and no CS2 caller in the decompile.
The ops behind it work (§3.4) and a run-time switch through them round-trips
(§5A), but there is no button in this build to press. Wiring one is a content
job in the settings panel, not an engine job.

---

## 4. The ordering rule, and why it is a memory-safety rule

Per frame:

1. poll SDL → bus
2. `App_DrainCommands` — applies the canvas change
3. **`PlatformSDL2_Resize(sdl, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H)`** and
   `ToriRS_GL3_SetViewport` with the same pair
4. `App_RunOnce` (may raise `window_mode_dirty`)
5. render / present
6. `App_TakeWindowModeChange` → push the next resize command

Step 3 sizes the backbuffer **from the canvas, not from the window**, and that
is not a preference: `App_Render` writes exactly `ROOT_W * ROOT_H` ints into
`PlatformSDL2_Pixels()`. The canvas is clamped to a floor the window does not
respect, so sizing the backbuffer from the raw window would overrun it the
moment a user drags below 765×503. Below the floor the window letterboxes the
clamped canvas — which is what fixed mode does anyway.

`sim_render_frame`'s cached scratch buffer in `main.c` grows for the same
reason.

---

## 5. Verified (2026-08-02, `SDL_VIDEODRIVER=dummy`, live ToriRSServer on 43595)

Boot canvas unchanged — `manifests/manifest_osrs230.ini` with no knobs still gives
`(161|0) 765x503`, `(161|92) 765x503`, and a pixel-identical frame.

**The drift, gone.** `TORIRS_ROOT_SIZE=1024x768 TORIRS_DUMP_BOUNDS=161`:

```
before: BOUNDS (161|92) abs=108,132 765x503     BOUNDS (161|94) abs=108,132 765x503
after:  BOUNDS (161|92) abs=0,0  1024x768     BOUNDS (161|94) abs=0,0  1024x768
```

(Earlier dumps showed `abs=-21` here. That was not authored on 161:1 — it
fell out of script 5355 shrinking `gameframe` to canvas−42 for the popout
strip while script 909 sizes the viewport trackers to full canvas with
authored `xmode=1` (centre): `(723−765)>>1 = −21`. That **relative** overhang
is the reference IF3 centre arithmetic and must stay in
`UITree_If3AxisFromPositionMode` — a global "origin-align oversized centred
children" guard briefly forced relative `0` and broke every chatbox dialogue
whose root is larger than `chatbox:chatmodal` (see `REV230_UI_BLANK_PANELS.md`
§5).

The left 21px of that overhang sits off the canvas and clipped left-edge
overlays (`stat_boosts_hud` 708, `buff_bar`). `uitree_layout.c` now clamps
**only** `xmode==1 && abs_x < 0` up to `abs_x=0` after abs is formed: the
tracker keeps its right overhang under the popout, chat dialogues (slot
`abs_x≥20` → child still `abs_x≥0`) are untouched, and `abs_y` is never
clamped (`chat_left` universe uses `abs_y=-6`). Measured with
`TORIRS_NET_CHEAT="boost attack 10 0"` + `TORIRS_DUMP_BOUNDS=708`: content at
`abs=2,301`, tile tradebacking at `abs=13,301` with full `35×35` emit clip.)

**A live resize reflows.** `TORIRS_SIM_RESIZE="200,1024x768" TORIRS_CS2_TRACE=1`:
script 909 runs a fourth time after the injected event, and

```
run 1-3: script=909 pc=63  IF_GETWIDTH               itop=765
run 4:   script=909 pc=63  IF_GETWIDTH               itop=1024
run 1-3: script=909 pc=348 VIEWPORT_GETEFFECTIVESIZE itop=503
run 4:   script=909 pc=348 VIEWPORT_GETEFFECTIVESIZE itop=768
```

**On pixels**, the 1024×768 frame is a correct resizable gameframe: viewport
filling the frame, minimap top-right, inventory bottom-right, chatbox
bottom-left, sidebar strip on the right edge. The world renderer and the raster
paths tolerate a non-765×503 surface (this was an open unknown going in).

**Input follows.** `TORIRS_SIM_RESIZE="150,1024x768"` then
`TORIRS_SIM_CLICK_AT="300,928,451"` — the prayer tab at its *new* position —
opens the prayer panel and the hover text reads "Prayer / 1 more options".

**The mode ops round-trip.**
`TORIRS_ROOT_SIZE=1024x768 TORIRS_SIM_RUNSCRIPT="200,3998,0;400,3998,1"`:

```
canvas: 1024x768
sim_runscript: script=3998 argc=1   windowmode: fixed       canvas: 765x503
sim_runscript: script=3998 argc=1   windowmode: resizable   canvas: 1024x768
```

Tests: `cmdbus_test`, `uitree_test`, `ss_provider_test`,
`cs2_opcode_dialect_test` all pass.

---

## 5A. Verified for §3A (2026-08-02, `SDL_VIDEODRIVER=dummy`, fresh ToriRSServer)

Method: two binaries, one stimulus. **before** = `2f167941` (the commit before
the boot-mode wiring), rebuilt in a throwaway worktree with the
`TORIRS_SIM_WINDOW` knob patched in so it receives the same window resize.
**after** = this tree. Both run `manifests/manifest_osrs230.ini` (port-swapped) against
the same mock, `TORIRS_MAX_FRAMES=400`, resize injected at frame 260.

**Geometry — the window resize is now heard.**

```
before  TORIRS_SIM_WINDOW=260,1024x768   BOUNDS (161|0)  765x503    (161|92)  765x503
after   TORIRS_SIM_WINDOW=260,1024x768   BOUNDS (161|0) 1024x768    (161|92) 1024x768
before  TORIRS_SIM_WINDOW=260,1440x900   BOUNDS (161|0)  765x503    (161|11)  513x334
after   TORIRS_SIM_WINDOW=260,1440x900   BOUNDS (161|0) 1440x900    (161|11) 1188x731
```

**Pixels — the user sees it.** The before frame is a 765×503 canvas the present
upscales into the window, so the comparison nearest-upscales it to the window
size (what the GPU does) and diffs per pixel:

```
1024x768:  before vs after   702,528 / 786,432   = 89.33% of the frame differs
1440x900:  before vs after 1,192,174 / 1,296,000 = 91.99% of the frame differs
```

Noise floor for that comparison — the same binary, same knobs, two runs: **0.00%**
back-to-back, and **0.15–0.19%** across separate logins to the same server
process (idle NPC animation phase). Anything under ~0.2% at 765×503 is noise.

**No boot regression.** before vs after with no resize at all: 0.10% on the
first pair and 0.19% on the last, both inside that band. The default frame is
unchanged.

**Both routes to a size agree.** `--window 1440x900` (boot at size) vs
`TORIRS_SIM_WINDOW=260,1440x900` (drag to size): 0.17%.

**The floor holds.** `TORIRS_SIM_WINDOW=260,400x300` → no `canvas:` line, root
stays 765×503: `SDL_SetWindowMinimumSize` refused the window. `2560x1440` →
canvas 2,560×1,440, no assert, no crash; the raster paths take it.

**Mode round trip restores the window.**
`TORIRS_SIM_WINDOW="260,1280x800" TORIRS_SIM_RUNSCRIPT="330,3998,0;430,3998,1"`:

```
windowmode: boot resizable   canvas: 1280x800
windowmode: fixed            canvas: 765x503
windowmode: resizable        canvas: 1280x800     <- was 765x503 before §3A.1
```

Tests: `make -C src test-cmdbus`, `test-uitree`, `test-bootmanifest` all pass.

---

## 6. Decisions recorded

**Fixed mode letterboxes; resizable mode tracks the window.** Fixed *is*
765×503 by definition, so scaling it into a bigger window is the correct answer
and reflowing is not.

**The canvas floor is 765×503** (`APP_CANVAS_MIN_W/H`). Not cosmetic: every
rev-230 gameframe child is authored as an inset off that box —
`toplevel_resize` is `max(0, width - inset)` throughout — so a smaller canvas
produces zero-sized viewports rather than a smaller frame. The reference client
has the same floor.

**The Display-panel layout preference persists across logouts.** Mode 0/1/2 is
written on the player save and restored at login through `~gameframe_set_mode`
(same path as `WINDOW_STATUS`). Boot `--windowmode` / manifest `windowmode` is
still the process default until that restore runs; it is not a second saved
preference.

**The mode is boot config until content binds the dropdown** (§3A.2). A client
that reports "resizable" to its scripts and letterboxes anyway is the bug this
doc exists for, so the mode is stated once, in the same place the cache and the
protocol are stated, and the platform is told at boot rather than only on a
change it may never see.

**The floor is a window constraint, not just a canvas clamp.** Below 765×503 the
client has no layout answer, so the window is not permitted there
(`SDL_SetWindowMinimumSize`) instead of quietly scaling. Above it there is no
ceiling: 2560×1440 was measured working, and any cap would be invented.

---

## 7. Two things that look wrong and are correct

1. **On toplevel 548, `if_getwidth($component0)` returns 4×334, not 765×503.**
   The cache binds the onload to `toplevel:control` = 548:1, a 4×334 graphic
   strip (`interfaces/toplevel.if:13-23`). The real client does the same;
   `toplevel_resize` never uses those numbers in fixed mode.
2. **`enum(component, component, enum_1129, 161:92)` returning -1 is the cache's
   own data** — `configs/all.enum`'s `[enum_1129]` block literally contains
   `val=10551388,-1` and `val=10551390,-1`. That is what makes
   `toplevel_resize`'s `if ($component2 ! null & $component3 ! null)` guard skip
   the stretch-viewport block in fixed mode. A "helpful" null fallback in the
   `ENUM` op would blank the fixed frame.

Also: `%varbit542` (cutscene/chrome-hide) and `%varbit4606` (widescreen
viewport/FOV) are read-only in CS2 and server-written. ToriRSServer never writes
them, so they default to 0, which puts `toplevel_resize` on its `else` branch —
the correct classic-viewport path for a normal session. Nothing to do there.

---

## 8. Gameframe layout dropdown remount (landed)

LostCity has **no** layout/windowmode system (rev 254 fixed-only). The Display
panel's layout dropdown (Fixed / Resizable Classic / Resizable Modern) is a
modern feature: content owns the mount tables and remount policy; C only fills
the missing wire (`WINDOW_STATUS`, `IF_OPENTOP` root switch, `IF_MOVESUB`) and
ServerScript ops.

### 8.0 Mapping (measured)

| Dropdown (`enum_3509`) | Toplevel | `settings_client_mode` arg | Canvas |
| --- | --- | --- | --- |
| 0 Fixed – Classic | `toplevel` (548) | 0 | fixed 765×503 |
| 1 Resizable – Classic | `toplevel_osrs_stretch` (161) | 1 | follows window |
| 2 Resizable – Modern | `toplevel_pre_eoc` (164) | 2 | follows window |

Sequence (OpenRune-shaped): CS2 case 12 → `settings_client_mode` → client
`WINDOW_STATUS(mode,w,h)` (mock wire op **101**, not 10) → server
`~gameframe_set_mode` → `runclientscript(3998)` + delayed `if_opentop` → remount
from `gameframe.enum` block named after that toplevel.

### 8.1 What landed

**Content**

- `script_3967.cs2` / `script_4569.cs2`: case 12 calls `~script3998` (not
  `~settings_client_mode` — see §8.3) and sets `%varbit4607` (1 iff modern).
- `gameframe.enum`: `[toplevel]`, `[toplevel_osrs_stretch]`,
  `[toplevel_pre_eoc]` mount blocks (same HUD/tab set, different slot names).
- `gameframe_layout.rs2`: `[proc,gameframe_set_mode]` /
  `[queue,gameframe_apply_mode]` → `runclientscript*(3998)` then `if_opentop`.

**C mechanisms**

- `PKTOUT_NAME_WINDOW_STATUS` (op 101): `p1 mode`, `p2 w`, `p2 h`. Host stashes
  mode on `settings_client_mode` (script 3998) entry — including Classic↔Modern
  when canvas mode does not flip — and `main.c` drains/sends in `TORIRS_NET_GAME`.
- `IF_OPENTOP` switches root via `App_OpenRootInterface`: `UITree_Clear` tears
  down the live forest first; `IF_OPENSUB` on the exec pipeline waits out
  `APP_STATE_BOOTING` so mount slots exist. The rebuild is the manifest's
  RevConfig layout re-run with the new group as `root_interface_id` — there is
  no separate open-the-interface-as-the-root path any more, so anything else
  the layout declares at the root (a developer overlay, say) comes back at the
  slot it was declared in. The toplevel id is still stated once, by content:
  an `rs_iface` record with no `componentno=` *is* "whatever we are rooting
  to". See [`debug_overlay.md`](debug_overlay.md) §3.
- `IF_MOVESUB` (inbound op 42) + ServerScript `if_opentop` / `if_movesub`.
- Ids resolve `toplevel` / `toplevel_pre_eoc` by name. Login opens the top that
  matches the saved `client_layout_mode` (default stretch / mode 1); mid-session
  changes still arrive as `WINDOW_STATUS`.

### 8.2 Verification (re-measured 2026-08-03)

Embed client (`make -C src torirs EMBED_SERVER=1`,
`manifests/manifest_osrs230_embed.ini`), against a cache baked from the tree
(`make -C src torirsserver-cache`, §8.3):

```
TORIRS_SIM_RUNSCRIPT="400,3967,12,0;1000,3967,12,2;1600,3967,12,1"
→ windowmode: fixed      if-opentop: 161→548
→ windowmode: resizable  if-opentop: 548→164
→                        if-opentop: 164→161
```

The entry point is **3967 case 12** — `[proc,settings_set_dropdown]`,
what the Display panel's layout row actually runs — not 3998 forced by hand. It
is the difference between testing the mechanism and testing the feature: against
pristine `cache.osrs239` this same command does nothing at all, because the arm
that calls 3998 is not in that cache. After a bake that *silently declined*
3967/4569 (see §8.3), the same command again does nothing even though the tree
looks correct — always re-measure the baked archive, not the `.cs2` alone.

`TORIRS_DUMP_BOUNDS` + `TORIRS_EXIT_BMP` after each remount:

```
548  viewport (548|10)  512x334 @4,4
     minimap  (548|9)   249x163 @516,4
     chat     (548|11)  519x165 @0,338     (absolute — fixed frame)
164  mainmodal (164|16) 512x334  modes x1,y1
     chat      (164|93) 519x165 @0,338
161  mainmodal (161|16) 512x334  modes x1,y1
     chat      (161|96) 519x165 @0,338
```

`ToriRSServer_Pack --check-only`: 0 errors. `test-cmdbus` / `test-bootmanifest` /
`test-uitree` green. Selftest still pins `ids->iface_gameframe == 161` as the
**login default** pack id (session top is `player->gameframe_*`).

### 8.2a Fixed chatbox cleanup (2026-08-07)

The real RuneLite harness exposed a remount state the in-tree renderer did not:
switching to Fixed – Classic left `chatbox:mes_layer` visible. Its white backing
covered the chat scrollback and its close icon remained on top. The layer is
normally hidden by the chatbox cache record; rev 239 nevertheless retains its
runtime visibility while `IF_OPENTOP` replaces the parent root and reopens
interface 162 beneath `toplevel:chat_container`.

`[queue,gameframe_apply_mode]` now queues
`[queue,gameframe_reset_chatbox]`, which receives the selected mode and sends
`if_sethide(chatbox:mes_layer, true)` one tick after `if_opentop($dest)`.
This is content, not a client workaround: the layout transition discards the
old chat dialogue/message state, and the content-owned gameframe remount is
the operation that must restore the chatbox's normal state. The tick is
deliberate: the mounted chatbox's onload can run after the initial interface
packet burst, so an immediate hide can itself be overwritten.

Canvas size still distinguishes the frames (from earlier §8 table): 161/164
reflow; 548 does not. Remount alone is what `--windowmode fixed` was missing
for a real fixed tree.

### 8.3 The client had to boot a cache baked from the tree (landed)

The dropdown was inert with all of §8.1 already written, and the reason was one
step of the pipeline that did not exist. `cache.osrs239` is the pristine unpack
source and is frozen; every manifest booted it; so the client read the CS2 *as
downloaded* and the case-12 arm in `script_3967` / `script_4569` was simply not
in the cache it ran. No content edit to a client-visible record had ever reached
a running client.

`make -C src torirsserver-cache` is the missing step — `cachepack pack --base
cache.osrs239 --out cache.osrs239.baked --assets --binary`, the client half of
the bake PORTING_GUIDE §1 already draws. `--base` means the result is the
pristine cache plus what the tree changes, so it is not a second corpus. The
ToriRSServer manifest family points at it; the offline-render and worldmap manifests
still boot pristine, which is what they want.

Two engine faults in `cachepack` had to be fixed first, both of which made an
edited script *silently* ship its original bytes (`3rd/rscache/tools/README.md`,
"pack the way you unpacked"):

- **Script names came only from RuneStar's un-vendored `script-names.tsv`**, so
  `~on_mobile` did not resolve and 5,032 of 9,725 sources were declined on a
  machine without that corpus. The tree states this about itself — every
  `scripts/<name>.cs2` opens with its `[trigger,name]` header and
  `pack/12_clientscripts.pack` maps the file to its id — so `cs2_seed_script_names`
  reads the two together. Declines fell to 2,921 (the rest are entity names:
  `coins_995`, `p12_full`, still table-only).
- **`^windowmode_fixed` / `^windowmode_resizable` had no spelling**, which is
  what made `[clientscript,settings_client_mode]` itself uncompilable. Seeded in
  `RSCache_CS2_NamesInit` beside `true`/`false` and on the same grounds: a
  two-line dialect table is the language's own words, not recovered data.

A third fault was in the language, not the tables: `~name` required the name
table to call the target a `proc`, and 3998 is a `clientscript`. A trigger is a
fact about the table and not about the bytecode — `gosub_with_params` takes an
id and a script record has no trigger field — and `cs2_write_call_target` had
already decided this same disagreement the other way for years, writing the bare
id because "the call site is evidence". `RSCache_CS2_NamesScriptId` falls back
across triggers when exactly one script carries the name, **except**
PROC→CLIENTSCRIPT: that fallback was later excluded so a trampoline's sole
clientscript alias cannot GOSUB-self while the real proc lives only as `.cs2b`.

That exclusion is what made a later bake drop the layout dropdown again.
Hand-writing `~settings_client_mode(...)` in case 12 looks right and compiles
nowhere — pack declines 3967/4569, keeps pristine bytes, and the label still
updates (the `cc_settext` before the switch) while remount never runs. The
durable spelling is the decompiler's own escape hatch for this mismatch:
`~script3998(...)` (id path in `cs2_cc_resolve_script`, same as `~script753`
calling `[clientscript,script753]`). Measured 2026-08-03: after that spelling
change, baked idx12 archives 3967/4569 differ from pristine and contain
`GOSUB_WITH_PARAMS 3998`; the §8.2 sim then remounts 161→548→164→161.

**Hard rule when editing these two scripts:** after `torirsserver-cache`, confirm the
baked archive MD5 changed (or that decompressed bytecode contains id 3998). A
tree that says `~script3998` while the bake still matches pristine is the same
bug as before — only quieter.
### 8.3a Remaining gaps

- ~~**Persistence** of layout across logins~~ — **landed.** `client_layout_mode`
  is written to / read from the player save (`[player] client_layout_mode`);
  login keeps the loaded value (default 1), opens the matching toplevel
  immediately, and calls `~gameframe_set_mode` so the client canvas matches
  (same proc mid-session `WINDOW_STATUS` uses).
- **OpenRune intermediate hop** (fixed→pre_eoc via stretch) — deferred unless a
  measured break needs it.
- Content `.rs2` files may still *spell* `toplevel_osrs_stretch:mainmodal` /
  `:sidemodal` / `:floater`. That is fine: `ToriRSServer_SendIfOpensub` /
  `closesub` / `movesub` rewrite those role suffixes to the live top's matching
  slot (bound by `if_opentop` on the player) whenever the named component's
  interface differs from the session gameframe. No per-call-site mode table.
- Talking to a real Old School server: local `WINDOW_STATUS` opcode 101 is a
  mock convention (rev-230 RSProt op 10 collides with `OPNPC2` here).

### 8.3b Fixed mode and the popout strip

Script 5355 carves `strip_w` (42 collapsed, 312 with a panel open) out of the
canvas and docks the popout on the right. In fixed mode the classic frame is
authored for 765×503, so a canvas pinned at exactly 765 put the strip on top of
the stone edge. The shell now measures the right-docked full-height strip after
layout (`App_MeasureRightChromeStripWidth`) and grows the fixed canvas to
`765 + strip_w` (`App_SyncFixedChromeInset`), so the gameframe lays out at 765
and the strip sits outside it. Resizable mode is unchanged — it already carves
from a larger window.

### 8.4 The popout strip is gated on toplevel *identity*, not geometry

Reported as a resize symptom, measured as something else. `popout:container`
(728:9) resolves to **-6 × 491** — `widthmode=1` means "parent minus 48" and its
parent `content_desktop` (728:4) is 42 wide. At 1440×900 it is **-6 × 888**: the
strip's outer frame tracks the window (`728:4` abs 723→1398, height 503→888) and
the panel container's width does not move at all, at any size.

It cannot: the widening to 58+278 lives in `[clientscript,script7568]`, which
returns early unless `~script9336` says so, and that proc answers 1 only for
component set **1745** — which `[proc,toplevel_getcomponents]` returns only when
`if_gettop = interface_601` (`toplevel_osm`). Under 161 the answer is 1130, so
the branch is dead by identity. No window size and no resize event can reach it,
and it is **not** in the way of resizable mode: `toplevel_osrs_stretch:popout`
is a real slot of 161, the strip draws correctly at both sizes (see the frames
in §5A), and the collapsed panel is what 161 is supposed to show. Faking the
enum would mount an OSM-frame layout inside a non-OSM frame.

### 8.5 `gameframe.enum` mounts are already the explicit-open path — a stray skull icon was one row too many, not a client bug

Investigated as a client-side "the tree eagerly auto-mounts nested if3
sub-panels" bug (`pvp_icons`/interface 90 rendering a skull icon on every
ordinary, non-PVP login). It is not that — the general finding below matters
more than the one row that got removed.

**The general rule (verified against the rev-239 RuneLite deob at
`Deobfuscator/src_osrs239_rl1_12_33`, `client.java:1938-1948` and
`class415.method9495/9507/9530`):** a nested IF3 sub-interface only mounts,
and only fires its root component's `onload`, when something sends an
explicit open call for it — never from a passive walk of a parent interface's
static nested-panel layout data. `toplevel_osrs_stretch.if`'s `[pvp_icons]`
block (161:3) is a bare, empty `type=0` layer in the cache pack — verified
with `tools/dump_interface cache.osrs239.baked --iface 161 --child 3`, which
shows no scroll, no onload, no children — and interface 90 (`pvp_icons.if`,
`tools/dump_interface cache.osrs239.baked --iface 90`) is a wholly separate
55-component group. Nothing in the cache links the two; the client's own
`ToriRS_Component` struct (`src/engine/torirs_types.h`) has no name field to
match them by even if it wanted to.

**This client already obeys that rule end to end.** Every panel
`toplevel_osrs_stretch` shows — `chat_container`, `buff_bar`,
`stat_boosts_hud`, `pm_container`, `hpbar_hud`, `orbs`, `popout`,
`tli_listener`, `side0..13`, `mainmodal`, `sidemodal` — reaches the client as
a real, observable `IF_OPENSUB` packet:

- The HUD/tab set is driven by `ToriRSServer_GameframeOpentop`
  (`src/torirsserver/torirs_server_encode.c:1047`), called once from the login burst
  (`ToriRSServer_WorldLoginFinish`, `src/torirsserver/torirs_server_world.c:8859`). It
  reads the content enum named after the toplevel (§8 above —
  `player/configs/gameframe.enum`, `[toplevel_osrs_stretch]` /
  `[toplevel]` / `[toplevel_pre_eoc]`) and calls
  `ToriRSServer_SendIfOpensub` once per row, in file order.
- `mainmodal`/`sidemodal` are bound the same way but opened later, per
  destination, by content `[login,_]` procs (`orbs_login`,
  `combat_tab_login`, …) via the same `ToriRSServer_SendIfOpensub` — see the
  many `toplevel_osrs_stretch:mainmodal` call sites in `torirs_server_world.c`.
- On the client, every one of those packets runs the same explicit path:
  `PKT_NAME_IF_OPENSUB` (`rs_gameproto_exec.c:831`) →
  `App_OpenSubInterface`/`Task_OpenSubRefresh` (`app.c:5648`) →
  `CreateTask_InterfaceOpenSub` (`task_interface_open.c`), which is where a
  mounted pack's `onload` hooks actually get collected and run. There is no
  separate "bake walks nested if3 slots automatically" mechanism anywhere in
  `uitree_builder_bake.c` to remove — the recursive walk that mounts a pack's
  *own* components under its owner (`uitree_builder_bake_pack_under_owner`)
  only ever runs against the ONE pack `IF_OPENSUB`/the boot manifest named;
  it does not discover or fetch any other group.

Traced live with `TORIRS_NET_DEBUG=1 TORIRS_CS2_MOUNT_DEBUG=1
TORIRS_SPILLOVER_DEBUG=1` against the embed transport
(`manifests/manifest_osrs230_embed.ini`, `make -C src torirs EMBED_SERVER=1`): every
mount at login is an `if-opensub: iface=N target=0x00a1xxxx` line, `pvp_icons`
included — group 90 into `161:3`, from the enum row, same as every other
panel.

**The actual bug was one enum row.** `gameframe.enum`'s `pvp_icons` row
mirrored OpenRune's reference `GameframeLoader` table verbatim (§8 header),
which mounts interface 90 unconditionally on every login regardless of world
type — matching real client/server traffic. On the real game the mounted
widget's own onload chain (script 865 → `~pvp_icons_layout`, script 386,
`OSRS-Content/osrs239-content/scripts/script_386.cs2`) then reads
`deadman_world` / `wildwars_world` / `kots_world` / `clanwars_ffa_arena(coord)`
/ `wilderness_level` to pick a branch, and a normal world presumably resolves
to something that stays visually inert. ToriRSServer implements none of those
world-type signals — it has no PVP worlds, wilderness, deadman mode, FFA
arenas or KOTS — so every one of those reads always answers false/zero and
execution always falls into script 386's shared "plain world" `else`
(lines 194–283), whose only hide/show gate is `%varbit542` (cutscene status).
Outside a cutscene that branch unconditionally shows the icon container
(`interface_90:44`), so an ordinary ToriRSServer login always drew a stray PVP/skull
icon — not because the client mounted something it should not have, but
because the *server* opened a widget whose correct rendering depends entirely
on state this server doesn't model.

**Fix:** removed the `pvp_icons` row from all three `gameframe.enum` sections
(`OSRS-Content/osrs239-content/server/scripts/player/configs/gameframe.enum`).
Interface 90 is now never opened, exactly like a real non-PVP-world server
apparently leaves some login-time widget unopened rather than shipping a
script that cannot answer its own preconditions — its `onload` never fires,
`~pvp_icons_layout` never runs, nothing renders. This is a content-only
change (`.enum` files are read directly at ToriRSServer boot,
`torirs_server_content.c`'s generic `walk_configs(path, ".enum", …)`; no
`torirsserver-cache`/`cachepack` rebuild needed) and it is scoped to exactly one
row per toplevel — every other `gameframe.enum` row, and the explicit-open
mechanism itself, is untouched. Verified with `ToriRSServer --selftest` (both the
default and `TORIRSSERVER_REV=osrs239` lanes) byte-identical before/after aside
from one heap-pointer debug print, and with the embed-transport trace above
showing the other 20 `161:xx` mounts land in the same order with the same
ids as before the row was removed.

If PVP worlds are ever implemented here, the row (and the varp/varbit
plumbing script 386 needs) can come back together; until then this is the
"leave the slot unopened" side of the rule above, not a workaround for it.
