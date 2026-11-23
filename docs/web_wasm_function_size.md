# Wasm function size and Safari

JavaScriptCore, the engine in Safari and in every browser on an iPhone or iPad,
optimizes a wasm function only when its body is at most **100,000 bytes**. A
bigger function stays in the baseline tier for good. The web build checks this
on every release link.

## The limit

JavaScriptCore has three tiers for wasm: an interpreter, a baseline compiler
(BBQ) and an optimizing compiler (OMG). It promotes a hot function to OMG only
when the function's cost, which is the size of its body in the code section,
is at most `maximumOMGCandidateCost`. The default is 100000. A function over
that runs in BBQ forever, however hot it is. In BBQ every wasm local is a slot
in the machine stack frame, so each `local.get` and `local.set` is a memory
access. Nothing reports this: the page works, just slowly.

V8 (Chrome) and SpiderMonkey (Firefox) have no such limit.

## How it was found

On 2026-11-22 the CS2 JIT work was timed in wasm under node (V8), the macOS
`jsc` shell (the JavaScriptCore framework Safari uses) and SpiderMonkey's shell.
Running the recorded bank build, JavaScriptCore took 2.5x native, against 1.5x
for V8. The cause was `cs2vm2_run_script_body`. binaryen's `-O3`, run by emcc at
link, inlines any function that has only one caller, whatever its size. It had
folded `CS2VM2_RunOp`, the general path's opcode switch, and every handler that
only the switch calls into the run loop. The result was 116,614 bytes at v3
6e1422b98, 121,492 bytes with the JIT, and 123,015 bytes in the real web client
(all over 100,000). In clang's own object the run loop is 9 KB.

The threshold was confirmed by moving it. With `jsc
--maximumOMGCandidateCost=N`, the run loop stayed in BBQ at N = 121,000
(11.3 ms per bank pass) and was optimized at N = 122,000 (5.6 ms). With the
limit lifted, JavaScriptCore was the fastest of the three engines (1.37x
native). This was measured on the Mac's own `jsc`. That iOS uses the same
default is an inference (it is the same engine) and has not been measured on a
device.

## The split (wasm only)

`src/cs2vm2/cs2vm2.c`, at `CS2VM2_RunOp`. `CS2VM2_RUNOP_SPLIT` is 1 when
`__wasm__` is defined and 0 otherwise. It can be overridden with
`-DCS2VM2_RUNOP_SPLIT=0|1`.

- The switch is still written once, as `CS2VM2_RunOp`. Under the split a
  `#define` compiles that definition as `cs2vm2_run_op_body`, a `static inline
  __attribute__((always_inline))` function, and two functions inline it:
  - `cs2vm2_run_op_split` is what the loop calls. It runs every opcode outside
    `CS2VM2_RUNOP_OUTLINED` through its own copy of the body. binaryen inlines
    it, and that copy, into `cs2vm2_run_script_body`. These opcodes are
    vm-core (0..99: return, gosub, the var and local reads, the string stack),
    the component setters (100..2999) and the string group (4100..4199).
  - `cs2vm2_run_op_outlined` is a separate function. It runs
    `CS2VM2_RUNOP_OUTLINED`: 3000 and up, except 4100..4199. These are the
    client state, inventory, enum, obj, camera, world and later groups. On the
    recording they are 16% of general-path ops. Nearly all of the common ones
    (INV_GETOBJ, INV_GETNUM, ENUM, CLIENTCLOCK, OC_*) ask the host, which costs
    far more than one call.
- Each copy is told its range: the split's `if` for one, and an `assert` plus
  `__builtin_unreachable()` in `cs2vm2_run_op_outlined` for the other. clang
  then drops the other range's cases from each copy. In a debug build an
  opcode that reaches the wrong copy fails the assert. It never reaches
  `default:`.
- binaryen ignores clang's `noinline`. `cs2vm2_run_op_outlined` is kept out of
  line by `__attribute__((export_name("cs2vm2_run_op_outlined")))`. An export
  is a use that binaryen cannot remove, so the function has two uses and is too
  big to inline. The cost is one extra export, `Module._cs2vm2_run_op_outlined`,
  which nothing in JavaScript calls. Two other ways were rejected:
  `-sBINARYEN_EXTRA_PASSES=--no-inline=...` runs after `-O3`, so it comes too
  late, and `--one-caller-inline-max-function-size` would change inlining for
  the whole module.
- Native builds compile the same code as before. A call per out-of-line opcode
  costs native code 2.5-3%, and native has no size limit. With `__LINE__`
  values masked, the preprocessed `cs2vm2.c` is token-identical before and
  after for OPT=1, OPT=1 EMBED_SERVER=1 and OPT=0. The objects differ only in
  line numbers: debug info, assert messages, and one `ToriMem_AllocAt` site tag.
- The yield lint (`check-cs2-yield`) knows the alias: `cs2vm2_run_op_body` is
  the switch, and both entries are part of the loop chain.

Function sizes in the real web client (`make PLATFORM=web OPT=1`, worktree
cs2-jit):

| function | before | after |
|---|---|---|
| `cs2vm2_run_script_body` | 123,015 | 71,271 |
| `cs2vm2_run_op_outlined` | (inlined) | 39,066 |

The wasm replayer (tools harness, JIT tree) went from 121,492 to 69,963 +
38,894.

Timing for the recorded bank build, VM side (three bank opens, 1.7M ops). The
numbers are microseconds per pass: the median of 7 interleaved rounds on one
Mac (M4 Max) with a spinner running. "Before" is the same tree built with
`-DCS2VM2_RUNOP_SPLIT=0`.

| engine | JIT on, before | JIT on, after | change | after, x native |
|---|---|---|---|---|
| JavaScriptCore (`jsc`) | 10,420 | 5,160 | **-50.5%** | 1.34x (was 2.71x) |
| V8 (node 24) | 6,089 | 6,214 | +2.0% | 1.61x |
| SpiderMonkey 160 | 7,132 | 7,266 | +1.9% | 1.89x |
| native | 3,844 | 3,850 | +0.2% (same code) | 1.00 |

With the JIT off, JavaScriptCore went from 12,440 to 6,380 (-48.7%). Over the
whole recording it went from 22,180 to 12,200 with the JIT on (-45.0%). The
split cost V8 and SpiderMonkey 1.6-2.3% in every arm.

## The check

`tools/wasm_func_size_check.py`, run at the end of every `PLATFORM=web OPT=1`
link (the `$(TARGET)` recipe in `src/makefile`). To rerun it on the last link:
`make -C src PLATFORM=web OPT=1 check-wasm-func-size`.

- It reads the module the lane ships (`build-web/torirs.wasm`, or the
  `PLATFORM_TARGET`'s `.wasm`).
- A release link strips function names (`-g0`), so the link also passes
  `--emit-symbol-map`. emcc then writes `<target>.symbols` (`index:name`,
  imports first) from the same link, and the makefile moves the file into
  `$(OBJ_DIR)`. The map changes no code. It does change the function order in
  the module and the minified import keys, so the map has to come from the
  same link as the module the check measures. A module with a name section
  (`PROFILE=1`) needs no map.
- It **fails** when a function in `tools/wasm_hot_functions.txt` is over
  **90,000** bytes. It also fails when a function marked `required` is missing
  from the module, because a missing function means binaryen inlined it again.
  On failure it deletes the target, so the next `make` relinks.
- Moving the map and running the check are one shell command with one failure
  branch, so a missing map deletes the target too. The makefile has no
  `.DELETE_ON_ERROR`: a step that failed without deleting the target would
  leave a complete module newer than its objects, and the next `make` would
  ship it unchecked. A command-line `LDFLAGS` or `PLATFORM_LDFLAGS` drops
  `--emit-symbol-map`, so that case is a real one. The link also deletes the
  previous link's map first, so `check-wasm-func-size` cannot pair a new module
  with an old map. `make -C src test-wasm-link-recipe`
  (`tools/test_wasm_link_recipe.py`) runs the recipe with a stub compiler and
  checks all of this.
- It **warns**, without failing, about every other function over 100,000
  bytes.

When the check fails on the VM, move more of the switch out of line by
widening `CS2VM2_RUNOP_OUTLINED`, and pick rare opcodes, not the vm-core
ones: making every general opcode a call (`noinline` RunOp, measured) cost V8
and SpiderMonkey 3-12%.

## The frame (wasm only)

`App_RunOnce` was 114,322 bytes in the web module, though it is 1,325 bytes in
clang's own object. binaryen had folded about 390 single-caller functions into
it: the frame body (`app_run_once_body`, 16 KB) and everything under it that
has one caller. Two subtrees were over half of it:

| subtree | bytes before binaryen | what it is |
|---|---|---|
| `app_plugin_panel_tick` | 36,194 | the plugin window and the developer chrome's sync |
| `app_world_frame` | 28,251 | the frame's world step: movers, camera, every npc and player body |

Both are now functions of their own in wasm. `APP_WASM_OWN_FUNCTION(name)`
(`src/app/app_internal.h`) goes on the prototype and expands to
`export_name` plus `noinline` when `__wasm__` is defined, the same mechanism as
the VM's out-of-line half, and to nothing in a native build. The two source
files preprocess to the same tokens before and after for a native OPT=1 build.
`-DAPP_WASM_OWN_FUNCTIONS=0` builds the web client without it, for measuring.

| function | before | after |
|---|---|---|
| `App_RunOnce` | 114,322 | 55,714 |
| `app_world_frame` | (inlined) | 25,258 |
| `app_plugin_panel_tick` | (inlined) | 32,761 |

All three are on `tools/wasm_hot_functions.txt` as `required`, so a web link
fails when one is folded back into its caller or passes 90,000 bytes.

**What it bought: nothing that can be measured yet.** `App_RunOnce` itself is
little of a frame. In the logged-in client standing still it takes about
0.37 ms of a 1.7 ms frame in WebKit, and most of that is spent in functions it
calls (the UI tree, the scripts), which were never part of the oversized
body. See the frame rows in the table below. The split is kept because it
costs nothing in Chrome or natively and because the world step loops over the
scene, so its cost grows with the number of npcs and players in view, which
the test scene (one player at a bank chest) does not have.

## In the real browser, logged in

2026-11-23. The web client was built four ways, logged in to a private game
server with the 400-item bank and driven through 60 measured bank opens per
session (the first is dropped), in Playwright WebKit (webkit-2359,
JavaScriptCore; four interleaved sessions per arm) and in headless Chrome 158
(three). Every run of every arm ran the same opcodes (198,262 and 184,553). A
bank open is the script time of `bankmain_init` plus two `bankmain_build`
runs. The frame rows are the mean time of one `App_RunOnce` call over 10 s
standing still and over 8 s with the bank open. Values are the median of the
session means, with the range over sessions in brackets.

| WebKit | today's client (6e1422b98) | JIT | JIT + VM split | JIT + VM split + frame split |
|---|---|---|---|---|
| bank open, ms | 5.794 (5.737..6.253) | 4.750 (4.706..5.044) | 3.056 (3.024..3.066) | 3.028 (3.015..3.078) |
| against the column to its left | | -18.0% | **-35.7%** | -0.9% |
| against today's client | | -18.0% | -47.3% | **-47.7%** |
| `App_RunOnce` standing still, ms per frame | 0.379 (0.370..0.403) | 0.375 (0.362..0.393) | 0.375 (0.369..0.379) | 0.360 (0.332..0.382) |
| `App_RunOnce` with the bank open, ms per frame | 0.433 (0.400..0.437) | 0.436 (0.398..0.442) | 0.426 (0.417..0.433) | 0.425 (0.407..0.434) |

| Chrome | today's client | JIT | JIT + VM split | JIT + VM split + frame split |
|---|---|---|---|---|
| bank open, ms | 3.833 (3.776..3.898) | 3.228 (3.216..3.429) | 3.267 (3.264..3.364) | 3.254 (3.211..3.301) |
| against today's client | | -15.8% | -14.8% | -15.1% |
| `App_RunOnce` standing still, ms per frame | 0.388 (0.388..0.402) | 0.408 (0.396..0.410) | 0.388 (0.379..0.407) | 0.392 (0.382..0.395) |
| `App_RunOnce` with the bank open, ms per frame | 0.464 (0.455..0.471) | 0.459 (0.454..0.463) | 0.449 (0.440..0.452) | 0.448 (0.424..0.460) |

- The VM split takes a WebKit bank open from 4.75 to 3.06 ms. With both
  changes WebKit opens the bank faster than Chrome does (3.03 against 3.25 ms),
  where before it took 1.5 times as long.
- The frame split's effect on `App_RunOnce` in WebKit is -3.9% standing still
  and -0.1% with the bank open. The four paired rounds standing still were
  -6.8, +3.5, -12.2 and -0.8%: that is inside the session-to-session spread,
  so it is not a result.
- In Chrome neither split changes anything outside the spread.
- The two browsers ran at the same time on one Mac (M4 Max), on separate
  accounts of one game server. The clock is `performance.now()` on a
  cross-origin isolated page: 20 us in WebKit, 5 us in Chrome.
- This is Playwright's WebKit build on macOS, not an iPhone, an iPad or
  Apple's Safari.

## Still over the limit

The check's one warning on the real client:

| function | bytes | runs |
|---|---|---|
| `main` | 136,104 | once, at boot (it ends in `emscripten_set_main_loop`) |

Close to the limit:

| function | bytes | runs |
|---|---|---|
| `frame_loop_tick` | 91,461 | every frame (the browser's main-loop callback; `frame_loop_step` is inlined into it). Under JavaScriptCore's 100,000, over the check's 90,000, and not on the hot list: nothing fails if it grows past the limit |
| `app_map_editor_side_app_frame` | 84,755 | every frame, map editor only |
| `RS_CS2Host_Exec` | 64,327 | every host call; `rs_cs2_host_exec_dispatch`, the 900-way switch, is inlined into it |

The renderer and the UI tree are well under the limit: their largest functions
are `toridraw_rszbuf_texture_triangle` (20,094 bytes) and
`Task_UITreeBuild_Run` (13,230).
