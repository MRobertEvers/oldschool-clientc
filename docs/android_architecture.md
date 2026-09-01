# The Android lane

The client on Android: a raw `ANativeWindow` and EGL with no windowing library,
the tree's own software rasterizer, and GLES2 as the opt-in GPU path.

This is the counterpart of `docs/web_build.md` and of the `win32` block in
`src/platform/platform.mk`. Read that file first if you have not: it is the only
place in the tree that knows a platform exists, and the Android lane is one
block in it.

---

## 1. Where the seam is

The client above `platform/` is **unchanged**. Not "mostly unchanged" — the same
`main.c`, the same frame loop, the same rasterizer, the same gesture policy.
Android is a new implementation of an interface that already had three.

```
                      ┌──────────────────────────────────────────┐
                      │  main.c   while( frame_loop_step() )      │
                      │  app.c    world, UI tree, CS2 VM, net     │
                      │  3rd/toridraw   the software rasterizer   │
                      └───────────────────┬──────────────────────┘
                                          │  programs against
                                          ▼
                      ┌──────────────────────────────────────────┐
                      │      platform/platform_window.h          │
                      │  the host window, canvas, input and      │
                      │  present interface (PlatformWindow_*)    │
                      └───────────────────┬──────────────────────┘
                    ┌─────────────┬───────┴───────┬───────────────┐
                    ▼             ▼               ▼               ▼
            platform_sdl2.c  platform_      platform_      (your next host)
            macos / linux    win32gdi.c     android.c
            / web            win32 / win64  android
                 │                │               │
            desktop window    Win32+GDI     ANativeWindow
              library                       + EGL/GLES2
```

Each backend is one file and owns its windowing entirely; nothing above
`platform/` knows which one it is running on. The Android one names no
windowing library anywhere: not in its headers, not in its comments, and the
build proves it of the linked artifact (§9).

---

## 2. The three Android files, and why it is three

```
src/platform/
  platform_android.h        the seam between the two halves below
  platform_android_jni.c    knows Java. Owns the thread, the Surface handoff,
                            the input queue, stdout->logcat. DRAWS NOTHING.
  platform_android.c        knows drawing. Implements platform_window.h over
                            ANativeWindow: the ARGB canvas, the letterbox,
                            the blit, key/touch translation. KNOWS NO JAVA.
  platform_android_gl.c     the EGL context, as platform_gl_context.h.
```

The split is not decoration. `platform_android.c` is ordinary C that can be read
without knowing what a `jobject` is, and `platform_android_jni.c` has no opinion
about how a frame is composed. Everything they share is the handful of functions
in `platform_android.h`, and every one of those crosses a thread boundary.

---

## 3. Two threads, one mutex

Android's UI thread cannot host the frame loop: `while( frame_loop_step() )`
blocks, and a blocked UI thread is an ANR in five seconds. So the JNI half
starts a thread and calls `main()` on it — the same loop shape every native lane
uses. (The web lane had to invert its loop into `requestAnimationFrame`; doing
that a second time would put two loop shapes in one file.)

```
   Android UI thread                        frame thread (started by nativeStart)
   ─────────────────                        ──────────────────────────────────────
   surfaceChanged(surface,w,h)
     └─ ANativeWindow_fromSurface
        PlatformAndroid_SetWindow ─────┐
                                       │        main(argc, argv)
   onTouchEvent(MotionEvent)           │          └─ PlatformWindow_Init
     └─ nativeTouch ──────────┐        │               └─ PlatformAndroid_AwaitWindow
                              │        │                    ...blocks until ────┘
   onKeyDown(KeyEvent)        │        │
     └─ nativeKey ────────────┤        │        while( frame_loop_step() ):
                              ▼        ▼          PollCommands  ← drains the queue
                    ╔═══════════════════════╗     App_Render    → the ARGB canvas
                    ║  g_lock (pthread mutex)║     Present       → ANativeWindow_lock
                    ║   window + size        ║                     blit + unlockAndPost
                    ║   density, quit flag   ║
                    ║   keyboard inset (px)  ║
                    ║   event ring (256)     ║
                    ╚═══════════════════════╝
   onDestroy
     └─ nativeStop
          ├─ PlatformAndroid_RequestQuit
          └─ pthread_join ─────────────────────►  loop ends, App_Shutdown, exit
```

Three consequences worth stating:

- **The lock is held only long enough to move a value.** Never across a blit,
  never across a frame.
- **Input is queued, not translated on arrival.** The gesture policy
  (`src/input/torirs_touch.c`) mutates state the frame thread owns, and it is
  shared with every other backend. The UI thread only appends.
- **`nativeStop` joins.** The loop finishes its frame and `App_Shutdown` runs.
  Letting the process die instead is how a preference file or an incremental
  cache gets corrupted mid-write.

### The surface comes and goes

Android destroys the Surface whenever the activity stops and hands back a
*different* one on resume. The frame loop keeps running across that: the world
ticks and the network drains, and `Present` simply has nowhere to put a picture
until a surface returns. `surfaceDestroyed` publishes the NULL **before** it
returns, because Android destroys the surface the moment it does.

---

## 4. The frame: two paths

```
                      App_Render(app, pixels, W, H)
                                  │
              ┌───────────────────┴────────────────────┐
              ▼                                        ▼
   SOFTWARE (default)                        GLES2 (--gles2[-zbuffer], opt-in)
   toridraw rasterises into                  platform_renderer_gles2_*.c
   the ARGB8888 canvas                       draws into the EGL surface
              │                                        │
   PlatformWindow_Present                      PlatformWindow_PresentGL
              │                                        │
   ANativeWindow_lock                          eglSwapBuffers
   letterbox + swizzle + scale
   ANativeWindow_unlockAndPost
```

### The GPU path is the GLES2 renderer, shared with the browser

`platform_renderer_gles2_{core,ui,painter,zbuffer}.c` is OpenGL ES 2.0
core with **no extensions**, and it is shaped after the Windows D3D9 renderer's
retained model rather than after either desktop GL renderer. The web lane
links the same four files against WebGL1 (`--webgl1` / `--webgl1-zbuffer`),
which is why nothing in them may say "Android" any more than it may say the
name of a windowing library:

- geometry is baked once into Batch16 chunks for the scene (packed densely
  into one static buffer) and a paged arena for everything else, and addressed
  with 16-bit indices relative to wherever the attributes are bound; a window
  change is an attribute re-point, never a base-vertex draw;
- on the painter path (`--gles2`, the default) the static models being drawn
  live in a **resident window**: a 65,536-vertex GPU ring a model is copied
  into the first time it is drawn (one sequential upload, staged per frame)
  and indexed from every frame after, until the ring wraps over it. A frame
  draws ~40k static vertices out of a ~960k-vertex loaded region, so the
  window holds the whole visible set and a still camera places nothing.
  Actors are baked into a per-frame stream in sorted order; anything that
  cannot be resident is gathered into that stream. Measured on the Moto X
  against the previous whole-frame gather: 12.9k faces/frame indexed, ~300
  gathered, `memcpy` from 17% of the frame to 2%;
- on the depth path (`--gles2-zbuffer`) a pose whose faces are all opaque is a
  contiguous run of triangles and is drawn with `glDrawArrays` -- no index
  stream at all for most of the static world -- while mixed poses go through
  per-page index buckets and only genuinely blended faces are sorted;
- every world texture lives in one 2048² atlas. The vertex carries the tile and
  the scroll speed (28 bytes a vertex, `TRSPK_VertexGLES2`), and the fragment
  shader wraps/clamps the local coordinate per fragment, so the world pass binds
  one texture and never switches for scrolling water or lava;
- the UI is a retained sprite atlas, `GL_LUMINANCE_ALPHA` font atlases and one
  streamed vertex ring; the minimap/compass rotmask is a single two-sampler
  draw.

The only thing it needs from the platform is a context, and that seam is
`platform/platform_gl_context.h` -- nine functions, implemented twice:

| lane | implementation | backing |
|---|---|---|
| macos, linux, web | `platform_gl_context_sdl.c` | the desktop window library |
| android | `platform_android_gl.c` | EGL |

The renderers contain no windowing symbol at all; the Android lane's post-link
probe (§9) checks the shipped library for exactly that.

### The one pixel-format subtlety

`App_Render` writes ARGB8888 — on a little-endian machine, bytes `B,G,R,A`.
Every 32-bit `ANativeWindow` format is byte-order `R,G,B,A`. So the present pass
swaps R and B (`swizzle_argb_to_rgba`, with a `vld4`/`vst4` NEON twin that gets
the swap for free by storing the de-interleaved planes in a different order).

Building the whole client at `TORIDRAW_PF_ABGR8888` would avoid the swap, but it
would change the format every sprite, font and texture is composed in — on the
lane least able to absorb a subtle divergence. The swizzle rides inside a copy
that is already memory-bound.

### Damage rectangles are deliberately ignored

`ANativeWindow_lock` returns one of a *rotating* set of buffers, so the pixels
outside a damage box are not last frame's — they are some older frame's, or
uninitialised. A partial copy would leave those visible. The interface still
accepts the damage state (so nothing above needs a per-platform arm); this
backend just presents the whole canvas.

---

## 5. NEON: `neon32` and `neon64` are different instruction sets

Bringing this lane up on armv7 found a real defect, and it is worth knowing
about because the naming now encodes it.

ARM NEON in the **A32** (armv7) encoding and NEON in the **A64** (aarch64)
encoding are not the same instruction set. Several kernels here use intrinsics
that exist only in A64:

- `vmull_high_s32` / `vmlsl_high_s32` — widening high-half multiply
- `vcgtq_s64` — A32 has no 64-bit vector compare at all
- `vaddvq_*`, `vminvq_*`, `vmaxvq_*` — horizontal reductions
- `vqtbl1q_u8` — the full 16-byte table lookup (A32 has only the 8-byte `vtbl`)

`facesort.bitonic_radix.small` was guarded with `#if defined(__ARM_NEON)`, which
armv7 satisfies. It did not run slower there — **it did not compile**. So the
width is in the filename now, and `tools/kernel_names.py` (the naming authority)
enforces it:

| suffix | meaning |
|---|---|
| `neon32` | the A32 NEON baseline. Runs on armv7 **and** aarch64. |
| `neon64` | requires aarch64 — A64-only intrinsics, or wraps aarch64 assembly. |

| kernel | lane |
|---|---|
| `projection.parallel.plain` | `neon32` |
| `projection.perspective.plain` | `neon32` |
| `projection.zdiv` | `neon32` |
| `span.gouraudhsllightness.alpha` | `neon32` |
| `span.tex` | `neon32` |
| `facesort.bitonic_radix.small` | `neon64` |
| `projection.bound` | `neon64` |
| `projection.perspective.prepared` | `neon64` (wraps `projection16.aarch64.S`) |

On armv7 the `neon64` lanes fall through their dispatch ladder to the scalar
kernel, which is correct and is what every non-SIMD host already uses.

---

## 6. Data on the device

The client reads its cache with ordinary stdio (`platform_x_io.c`). **An APK
asset is not a file** — it is a compressed range inside the `.apk` that only
`AssetManager` can open — so the data cannot be bundled, quite apart from a
rev-239 cache being 218 MB. It is pushed to the device instead.

The device layout **mirrors the repo**, because a manifest states its cache and
RevConfig as paths relative to *itself*:

```
repo                                  device
────                                  ──────
manifests/manifest_osrs239_bench.ini  /sdcard/Android/data/com.torirs.client/files/
  dir=../cache.osrs239                  manifests/manifest_osrs239_bench.ini
  revconfig_ui=../revconfig/...         revconfig/osrs239/...
cache.osrs239/                          cache.osrs239/
revconfig/osrs239/
```

So every manifest resolves on the phone exactly as it does on the desktop,
**unedited**. Rewriting paths during the push would mean the manifest on the
device is not the manifest in the tree, and a path bug would be visible only on
the device.

`Android/data/<pkg>/files` needs no storage permission on any API level, is
reachable by `adb push`, and is removed on uninstall — the right lifetime for a
cache. `tools/android_push_data.sh` does the push.

### Working directory and `$HOME`

Neither is a client flag; both are ambient process state the client reads on
every host, so the JNI layer sets them before calling `main()`
(`place_process()`):

- **cwd** — `game/rs_prefs.c` opens `"preferences.ini"` by that relative name.
  An Android process starts at `/`, which is read-only.
- **`$HOME`** — `bootmanifest.c` derives the default streamed-cache location
  from it. Android sets none at all.

---

## 7. The boot menu

A phone has no command line, and `--manifest <path>` is how this client is told
which world to boot. Without a menu, changing profile would mean rebuilding the
APK.

```
   BootActivity                                  ClientActivity
   ────────────                                  ──────────────
   BootProfile.discover()
     scan  <files>/manifests/*.ini
     for each: read ONE key, [cache:boot] dir=
       resolve it relative to the manifest
       does that directory exist and is it non-empty?
                 │
         ┌───────┴────────┐
         ▼                ▼
    bootable         greyed out, with the reason
    (white)          ("cache missing: cache.osrs239.sparse")
         │
   default = last profile booted (SharedPreferences)
   4s countdown ──── any touch cancels it ────► user taps a row
         │                                              │
         └──────────────► startActivity(EXTRA_MANIFEST) ◄┘
                                    │
                          nativeStart(argv, dataRoot)
                          argv = ["torirs", "--manifest", <path>, ...extra_args.txt]
```

The two activities take **different orientations**, which is the one place they
disagree: `BootActivity` is `fullSensor` (a vertical list shows about twice as
many rows in portrait, and a phone picked up to choose something is usually held
upright), while `ClientActivity` is locked landscape because its canvas is a
765x503 landscape frame. The menu also carries `configChanges` for orientation,
so a rotation re-lays-out the view tree instead of recreating the activity and
restarting the countdown from the top.

Three decisions worth naming:

- **It lists unbootable profiles rather than hiding them.** A missing cache is
  the single most likely thing to be wrong on a fresh device; a menu that
  silently omitted the profile would leave you wondering where it went.
- **The manifest read is one key deep.** The *client* parses manifests. A second
  full parser in Java would be a second set of opinions about the format,
  drifting the moment the real one gains a key.

`<files>/extra_args.txt` (one argument per line) is appended to argv, so a
profile can be tried with `--gles2` or `--offline` without rebuilding the APK.

---

## 8. What is deliberately not here

| | why |
|---|---|
| **A windowing library** | The point of the lane. No header, no library, no such symbol in the `.so`, and no mention of one in the lane's sources. |
| **A CMakeLists.txt** | `src/platform/platform.mk` is the only thing that knows what a platform is. A second build description would restate every source and every `-D`, and drift silently — a stale duplicate still compiles. Gradle consumes the `.so`; it does not build C. |
| **The embedded server** | `EMBED_SERVER` stays 0. Android is a *client*: it dials a real server over TCP or WebSocket. Linking ToriRSServer in would put a second world simulation on the phone — needing the compiled script pack and the server's own copy of the cache on the device — to serve one player already in the process. `net_transport_embed.c` compiles to a **silent stub** without it, so a `transport=embed` manifest would come up and connect to nothing; the boot menu refuses those by name instead. |
| **Audio** | `platform_audio_null.c`, exactly as both Windows lanes do today. |
| **AndroidX** | Two Activities and a directory listing, against framework classes that have existed since API 1. |
| **A `GLSurfaceView`** | It would bring a second render thread and a second GL context with its own opinion about when a frame starts. |
| **Runtime storage permission** | Everything lives under the app's own external files directory. |

---

## 9. Touch: what a finger does

| gesture | result |
|---|---|
| tap | left click |
| long press (400 ms, inside the slop) | right click — the minimenu |
| **drag starting on the 3D viewport** | **turns the camera** |
| drag elsewhere | pointer moves, no click |
| pinch | wheel — the zoom |
| two-finger pan | arrow keys — the camera |

The camera drag is **synthesised as a middle-button drag**, not reimplemented.
The desktop already turns the camera that way (`app_world_camera_mouse`), and
that path carries the revision's `[camera] controls=` gate, the follow-cam
split, and the screen-space sign convention that keeps free and orbit cameras
agreeing. A second implementation would be three things to keep true instead of
none. The platform publishes the viewport box each frame
(`PlatformWindow_SetTouchViewport`); a drag is tested against where the finger
*started*, so one that wanders onto the interface is still the same drag.

The button goes down only once the finger passes the slop, so a tap on the world
is still a walk-here click.

### The inkwell

`UICross` is shown by the paths that **did** something — a walk was routed, an
interaction was sent. A tap on a widget, a tap that missed, or a tap during a
modal shows nothing, which is fine on a desktop where the pointer is visible.
On a touchscreen a tap that draws nothing is indistinguishable from a tap the
digitiser dropped, and the user taps again.

So the inkwell fires for **every** touch, before anything has interpreted it,
and the colour is refined afterwards (`UIInk_SetColour`) without restarting the
animation. Three styles — `splash`, `blot`, `ripple` — authored procedurally in
`ui/torirs_chrome_inkwell.c` because `spritebake` extracts *existing* cache
sprites and no revision ever shipped a touch marker. All 48 frames (3 styles x
2 colours x 8 frames) upload as one scene entry, so a style is an atlas index
and never an upload.

```ini
[component:cross]           ; every platform
type=cross

[component:cross@mobile]    ; touch only, and it OVERRIDES the above
[camera@mobile]             ; the nameless sections take the tag on the type;
zoom_closest=60             ; the phone's own band floor, past the desktop's
distance_scale=70           ; the whole distance, every angle
pitch_distance=2            ; and the pitch term alone, which is nearly the
                            ; whole of the overhead view. Every [camera] key
                            ; states exactly one number --
                            ; docs/CAMERA_CONFIG.md is the breakdown.
type=inkwell
style=splash
walk_color=yellow
interact_color=red
```

The `@tag` suffix is stripped before the name is stored, so both declarations
are the **same** element and the later one wins. A non-matching tag skips the
section *whole* — a half-applied override would leave a component with some
mobile fields and some desktop ones. `TORIRS_REVCONFIG_PLATFORM=mobile` forces
it on a desktop, so the mobile layout is testable with no device attached.

Colours are revconfig keys rather than constants because "yellow walks, red
interacts" is a *revision's* convention, not a law.

## 10. What the lane check enforces

`make -C src lane-check PLATFORM=android` is not decoration — three of its
requirements have failed quietly before:

| flag | what its absence does |
|---|---|
| `-mfpu=neon` | armv7 does not enable NEON by default, and the kernels select their SIMD lane with `#if defined(__ARM_NEON)` at **compile** time. Without it every one silently takes the scalar fallback — no symptom but a slower frame. |
| `-fPIC` | fails, but deep in the linker naming a *tommath* symbol rather than the cause. |
| `TORIRS_HAVE_GLES2` | the GLES2 renderer (shared with the web lane); `TORIRS_HAVE_GL3` and `TORIRS_GL_ES2` are forbidden, so `main.c` cannot hand this lane a desktop GL renderer or the retired WebGL1 fork's switch. |

The desktop window library's link flags are **forbidden** by name in
`platform_check.mk` (`LANE_FORBID_android`), not merely absent — the way the
rule would be lost is someone adding them to a *shared* variable to fix another
host. And because a flag list cannot prove what is in a binary, the lane has a
post-link probe on the artifact itself:

```
lane-check: PLATFORM=android ok
lane-check: android artifact carries no SDL symbol
```

---

## 11. Build and run

See **`android/README.md`** for the commands. In short:

```sh
make -C src PLATFORM=android ANDROID_ABI=armeabi-v7a OPT=1 all   # the .so
cd android && ./gradlew installDebug                              # the APK
tools/android_push_data.sh cache.osrs239                          # the data
adb logcat -s torirs                                              # stdout/stderr
```
