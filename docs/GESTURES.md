# Gesture frames

Workbench step 6.1, part 1 (the producers). The design is
`torirsed-workbench.html` section 14, "Mouse, trackpad, touch": the client
already turns fingers into notches and keys for the game; the editor needs
continuous values and the same five verbs on every surface, so the command bus
carries three phased gesture frames and the old synthesis stays for the game.

## The frames

| type | value | payload | motion |
| --- | --- | --- | --- |
| `TORIRS_CMD_GESTURE_PAN` | 112 | `struct ToriRS_CmdGesturePan` | `dx_q16`, `dy_q16`: the content's travel since the previous PAN, canvas px x65536, +x right, +y down; `fingers` |
| `TORIRS_CMD_GESTURE_PINCH` | 113 | `struct ToriRS_CmdGesturePinch` | `scale_q16`: the ratio since the previous PINCH, 65536 = 1, > 1 fingers apart |
| `TORIRS_CMD_GESTURE_TWIST` | 114 | `struct ToriRS_CmdGestureTwist` | `radians_q16`: the turn since the previous TWIST, + clockwise on screen |

Every payload also has `x`, `y` (the centre the gesture is about, canvas
pixels: the fingers' centroid, or the pointer on a trackpad), `phase` and
`flags`. The input range 16..31 is full, so the three sit at 112..114; nothing
dispatches on ranges.

* **Phases** (`enum LibToriRS_GesturePhase`): BEGIN, UPDATE..., END, per kind.
  A pan, a pinch and a twist of the same fingers are three gestures that
  overlap. BEGIN carries the motion that made it a gesture (everything since
  the fingers landed); UPDATE the motion since the previous frame of its kind;
  END no motion at all (0 travel, scale 1, 0 radians; the encoders assert it).
* **Flags**: `LIBTORIRS_GESTURE_FLAG_ALSO_LEGACY` -- the same motion also
  reached this frame the old way (the integer wheel, `TORIRS_CMD_INPUT_SCROLL`
  or `TORIRS_CMD_INPUT_PINCH`), so a consumer that takes the gesture drops
  those. `LIBTORIRS_GESTURE_FLAG_TOUCH` -- fingers on a touchscreen (the hint
  strip's "the last input was a touch").
* **16.16 everywhere**, so a replay decodes to the same floats bit for bit.
  Encoders: `CmdBus_PushGesturePan` / `_Pinch` / `_Twist` (src/cmd/cmdbus.h).
  They are ordinary bus commands, teed to the recording by `CmdBus_Push` and
  pumped back by `CmdReplay_PumpFrame`, so a touch session records and replays
  like a mouse one.

## Where they land

`ToriRS_Input_ApplyCmd` (src/input/torirs_input_cmd.c, the single decode
point, reached by `App_DrainCommands` with no App change) appends each frame
to `LibToriRS_Input::gestures` as a `struct LibToriRS_Gesture` (kind, phase,
fingers, flags, x, y, dx, dy, scale, radians; the fields a kind does not use
hold their identity). Frames no encoder could write -- a phase out of range, a
pan of no fingers, a ratio of zero, a short payload -- are dropped, as a short
payload always was.

The list has `key_events`' lifetime: `LibToriRS_Input_Begin` clears it,
`LibToriRS_Input_Continue` keeps it for the frame that consumes it. It holds
`LIBTORIRS_GESTURE_MAX` (32) frames. Past that an UPDATE folds into the last
open entry of its kind (travel added, ratios multiplied, turns added: only the
granularity is lost); a BEGIN, an END or an UPDATE with nothing to fold into is
dropped and counted in `gesture_dropped` (this frame) and
`gesture_dropped_total` (since Init).

Consumers walk `gestures[0 .. gesture_count)` once a frame.

## Phases for producers that have none

`struct ToriRS_GestureStream` (src/input/torirs_gesture.h) owns the phases:

* `ToriRS_GestureStreamPan` / `_Pinch` / `_Twist`: one kind, no threshold
  (a wheel event is already deliberate). BEGIN the first time, UPDATE after;
  a pan whose finger count changes ends and begins again.
* `ToriRS_GestureStreamPair`: all three from one step of a finger pair. Each
  kind begins only once its own motion since the fingers landed passes its
  threshold -- pan 12 px (the tap slop), pinch 8%, twist 0.15 rad -- and that
  BEGIN carries all of it. So a pinch leaves no jitter of twists behind it, and
  nothing before the lock-in is lost.
* `ToriRS_GestureStreamPairFinal`: the step just before the fingers change,
  reporting the begun kinds only. A finger rolls as it lifts; begun there, that
  roll would be a pinch that starts and ends in the frame the pan stopped.
* `ToriRS_GestureStreamEndAll` when the producer knows the fingers left;
  `ToriRS_GestureStreamIdle` once a frame for the ones that never say so: a
  kind quiet for `TORIRS_GESTURE_IDLE_END_MS` (120 ms) ends.

## Producers

### Touchscreens: the touch policy (every backend)

`ToriRS_TouchSetGestureFrames(touch, on)` (src/input/torirs_touch.h), **off by
default**. Off, the policy is the game's exactly as before: a pinch is wheel
notches, a two-finger pan arrow keys, and no gesture frame is pushed. On, two
or more fingers are gesture frames instead, with no wheel notch and no arrow
key synthesised:

* the centroid of every live finger is the PAN, `fingers` = how many are down
  (three fingers dragging are a pan of three: the touch pitch);
* the spread of the first two is the PINCH, their direction the TWIST;
* the pair is measured once a tick (`ToriRS_TouchTick`), so a 240 Hz panel
  costs the bus no more than a 60 Hz one, through `ToriRS_GestureStreamPair`;
* a finger landing or lifting closes the gesture (`PairFinal`, then ENDs) and
  the next tick takes a new baseline;
* every frame is flagged `LIBTORIRS_GESTURE_FLAG_TOUCH` and not ALSO_LEGACY;
* tap, long press and the one-finger drag are unchanged either way;
* turning the flag off mid-gesture ends that gesture on the next event or tick.

**Who turns it on**: `PlatformWindow_SetTouchGestureFrames(platform, on)`
(src/platform/platform_window.h; SDL2, Win32 GDI and Android each forward to
`ToriRS_TouchSetGestureFrames(&p->touch, on)`). main.c calls it every frame,
before the poll and beside `PlatformWindow_SetTouchViewport`, with
`App_EditorOwnsTouch` -- on while the map editor's panel is up, off for the
game. It is compare-then-set, so the per-frame call costs nothing.

### SDL2 desktop (src/platform/platform_sdl2.c, the event drain)

* **Wheel** (`PlatformSdl_PushWheel`), beside the integer wheel and
  `TORIRS_CMD_INPUT_SCROLL`, which are unchanged:
  * zoom modifier held (Ctrl; Ctrl or Cmd on a Mac): a PINCH of 1.25 a notch
    about the pointer, any device, natural scrolling undone the way the integer
    wheel undoes it (a wheel pushed away zooms in either way);
  * a trackpad (fractional deltas): a PAN, `fingers` = 2, the content's travel
    in canvas pixels -- SDL's +y "scroll up" is the content moving down, its +x
    "scroll right" the content moving left -- with the platform's direction
    setting kept;
  * a mouse wheel: nothing. It stays the wheel it is.
  Deltas from `preciseX/Y`, or the integer `x/y` when an event has no precise
  fields. All flagged ALSO_LEGACY; phases from the wheel's stream, ENDed by the
  idle time after each poll.
* **SDL_MULTIGESTURE** (`PlatformSdl_PushMultiGesture`) from an *indirect*
  device (a touchpad SDL reports as touches): PINCH is the fingers' spread
  ratio (SDL's `dDist` is one finger's change of distance from the centroid,
  not a ratio, so the ratio comes from the fingers as the existing
  `TORIRS_CMD_INPUT_PINCH` does), TWIST is `dTheta`, PAN the centroid's travel
  scaled by the window (a touchpad has no position on the canvas). The PAN is
  left out while the wheel is panning: a touchpad that also scrolls is the same
  two fingers twice. Through `ToriRS_GestureStreamPair`, flagged ALSO_LEGACY,
  ENDed when a finger lands or lifts. A *direct* device (a touchscreen) is the
  touch policy's, never this path's, so a touch is never reported twice.
* **macOS**, after the batch: AppKit's magnify is a PINCH (ALSO_LEGACY: the
  `TORIRS_CMD_INPUT_PINCH` it always was carries it too); AppKit's precise
  scroll goes through the same wheel mapping as above.

### The web (the page listener in platform_sdl2.c's EM_JS, emscripten's SDL)

The page's input glue for gestures is the one capture listener
`web_gesture_install` installs; src/web/ has none of its own.

* A trackpad wheel (the listener's classification) is a PAN, exactly as on
  the desktop.
* Ctrl+wheel -- Chromium's and Firefox's trackpad pinch, sent as a wheel with
  `ctrlKey` and no key ever down -- is a PINCH of the listener's exact
  `exp(-deltaY / 100)`, never a pan, flagged ALSO_LEGACY (the wheel and the
  `TORIRS_CMD_INPUT_PINCH` carry it too).
* Safari's `gesturechange` is a PINCH (its `scale`) and a TWIST (its
  `rotation`, degrees to radians, clockwise positive); `gestureend` ENDs both.
  Skipped while fingers are on the glass: iOS Safari reports a touch pinch as
  `gesturechange` AND as touches, and the touches are the ones that count.
* Touch pairs arrive as SDL finger events and go through the touch policy
  above, flag and all.

### TORIRS_SIM_GESTURE (src/main.c, beside TORIRS_SIM_HOTKEY)

```
TORIRS_SIM_GESTURE="<frame>,<kind>,<x>,<y>,<a>[,<b>][;...]"
  pan     a = dx, b = dy (canvas px), two fingers
  pan<N>  the same with N fingers, 1..9 (pan3: the touch pitch)
  pinch   a = that frame's ratio (2 = twice as far apart)
  twist   a = that frame's radians, + clockwise
  touch-<kind>   the same, flagged LIBTORIRS_GESTURE_FLAG_TOUCH
```

Entries of one kind on consecutive frames are one gesture: the first its
BEGIN, the rest UPDATEs, the END on the first frame after the run. Keyed on
`frame_count`, so a held frame does not push twice. The sim moves no pointer.
Each frame logs `sim_gesture: frame=... <kind> <phase> at x,y ...` on stderr; a
malformed entry stops the run (exit 2), as a bad chord does. The grammar and
the stepping live in src/input/torirs_gesture.c (`ToriRS_SimGestureParse`,
`ToriRS_SimGestureStep`), where the test holds them.

## What each platform cannot report

* **Linux, SDL2 (X11 and Wayland)**: no touchpad pinch or twist -- SDL2 does
  not expose libinput's gestures -- so a touchpad there zooms with Ctrl+scroll
  only. A touchpad's two-finger scroll is a PAN only where the driver delivers
  fractional (smooth) wheel deltas; integer notches classify as a mouse wheel.
  A touchpad SDL does report as an indirect touch device reaches the
  SDL_MULTIGESTURE path.
* **Windows, SDL2 lane**: a precision touchpad's scroll is fractional wheel
  deltas (a PAN); its pinch reaches SDL only as the Ctrl+wheel Windows
  synthesises for applications that read no gestures, and only when SDL's
  modifier state sees that Ctrl (a PINCH of 1.25 a notch, not the exact
  ratio); no twist. Not verified on hardware here.
* **macOS**: no twist yet -- AppKit's NSEventTypeRotate is not monitored
  (platform_macos_gesture.m has magnify and precise scroll only). SDL's own
  trackpad touches are skipped (they are AppKit's gestures already).
* **The web**: Chromium and Firefox report no twist (a wheel has no rotation);
  desktop Safari does. The listener classifies only the latest wheel before a
  poll, so a poll that drains a mouse notch and a trackpad delta together reads
  both as the latter's kind (the same limit `TORIRS_CMD_INPUT_SCROLL`'s
  trackpad flag already had).
* **Win32 GDI (platform_win32gdi.c): noted, not implemented.** Its fingers
  already reach the touch policy through WM_TOUCH (`ToriRS_TouchEvent` /
  `ToriRS_TouchTick`), so a touchscreen's PAN / PINCH / TWIST work there as
  soon as the platform exposes the flag. What it does not produce is the
  trackpad half: WM_MOUSEWHEEL's fractional deltas become
  `TORIRS_CMD_INPUT_SCROLL` but no PAN, Ctrl+wheel no PINCH, and WM_GESTURE
  (GID_ZOOM / GID_PAN / GID_ROTATE, Windows 7+) and WM_POINTER frames (8+) are
  not read at all. The XP lane has neither message, so either would need a
  runtime check.
* **Android**: the touch policy, as on any touchscreen; no trackpad path.

## The consumers (workbench 6.1, integrated)

* `ToriRS_InputGestures(input, &frames)` (src/input/torirs_gesture.h,
  implemented in torirs_input.c) is the one reader: it hands back
  `input->gestures`, the decoded `struct LibToriRS_Gesture` list itself.
  `ToriRS_InputLastWasTouch(input)` answers from `gesture_last_touch`, which
  `LibToriRS_Input_PushGesture` sets from the last frame's
  `LIBTORIRS_GESTURE_FLAG_TOUCH` and Begin leaves alone.
* `app_map_editor_gestures` (src/app/app_map_editor.c), once a frame before
  the editor's tick, offers each frame to the chrome (`Editor_UiGesture`,
  which maps the input's kind and phase onto the chrome's own
  `struct ToriRSChromeGesture`) and then to the World camera
  (`EditorWorldGesture_Apply`).
* **ALSO_LEGACY frames are skipped there.** Their motion already reached the
  chrome through `ToriRSChrome_WheelFrame` (in `app_loc_editor_tick`, which
  runs first) and the World through the camera's wheel, so taking the frame
  as well would scroll a list twice and pan the World while the wheel zooms
  it. A trackpad and ctrl+wheel therefore behave as they did in 6.0; the
  touchscreen's frames (never ALSO_LEGACY) and a touchpad's twist come
  through. Moving the trackpad onto the frames means dropping the legacy
  wheel / scroll / pinch for the frame first.
* Split view: src/app/app_render.c's `app_view_split_command_is_input` does
  not list the gesture types, so under the split view they reach the
  editor's input and not the game's physical input. That is kept on purpose:
  every consumer of a gesture frame is the editor's.

## Tests

* `make -C src test-gesture` (src/input/test/torirs_gesture_test.c): the three
  frames recorded and replayed bit for bit (2/3 rounded to 43691/65536), the
  decode's guards, the list's lifetime, fold and drop counts, the stream's
  phases and idle END, the pair's thresholds and the final step, the touch
  policy's PINCH / PAN / TWIST / three-finger pan under the flag with no wheel
  notch or key among them, the same fingers' notches and arrow key without it,
  a tap under the flag, the flag going off mid-gesture, and
  TORIRS_SIM_GESTURE's grammar and run phases.
* `make -C src test-sdl-gesture` (src/platform/test/sdl_gesture_test.c, the
  SDL drain): trackpad wheel to PAN (sign, size, fingers, flag, BEGIN then
  UPDATE), mouse wheel to nothing, Ctrl+wheel to PINCH 1.25 (natural scrolling
  undone), the idle END after a quiet poll, and no frame from a fingerless
  multigesture.
* `make -C src test-touch`: the policy without the flag, unchanged.
