# The Android lane

The client on Android: a raw `ANativeWindow` and EGL with no windowing library,
the tree's own software rasterizer, and two opt-in GPU paths -- OpenGL ES 2.0
and OpenGL ES 3.0.

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
              library                       + EGL/GLES2/GLES3
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

That NULL is also what stops the sound. Nothing else would: the loop above
keeps stepping, so a client that did not read it would go on playing music from
the recents list. `PlatformAudio_Update` reads `PlatformAndroid_Window()` once
a frame and pauses or resumes the OpenSL ES player to match.

### The audio device has its own thread, and its own mutex

`platform_audio_opensles.c` is a third thread the lane does not start: OpenSL
ES calls back on one of its own when a buffer completes, and the mix
(`ToriRS_Mixer`) runs there rather than on the frame thread. It has to. This is
the lane where a world rebuild costs hundreds of milliseconds, and a mixer fed
from the frame loop would turn every one of them into a gap in the music.

So the mixer is shared state, under a mutex of its own — nothing to do with
`g_lock`, which the audio thread never takes. The frame thread holds it across
a whole batch of submitted commands; the audio thread holds it across one 20ms
render. Neither allocates while holding it, and the audio thread calls no JNI
at all: it is not a JVM thread.

---

## 4. The frame: two paths

```
                      App_Render(app, pixels, W, H)
                                  │
              ┌───────────────────┴────────────────────┐
              ▼                                        ▼
   SOFTWARE (default)                        GLES2 (--gles2[-zbuffer])
   toridraw rasterises into                  GLES3 (--gles3[-zbuffer])
   the ARGB8888 canvas                       platform_renderer_es{2,3}_*.c
                                             draw into the EGL surface
              │                                        │
   PlatformWindow_Present                      PlatformWindow_PresentGL
              │                                        │
   ANativeWindow_lock                          eglSwapBuffers
   letterbox + swizzle + scale
   ANativeWindow_unlockAndPost
```

### The first GPU path is the ES 2.0 core, shared with the browser

`platform_renderer_es2_{core,ui,painter,zbuffer}.c` is OpenGL ES 2.0
core with **no extensions**, and it is shaped after the Windows D3D9 renderer's
retained model rather than after either desktop GL renderer. The web lane
links the same four files against WebGL1, which is why nothing in them may say
"Android" any more than it may say the name of a windowing library.

What names it is the **lane file**: `platform_androidarmv7_renderer_opengles2.c` here,
`platform_web_renderer_webgl1.c` in the browser. Each is a thin translation unit
whose opaque handle simply *is* the core -- no wrapper object, no indirection
-- and it does three things the core must not: it gives the core the lane's
name, so a logcat line says `GLES2` and never `WebGL1`; it asks
`platform_gl_context.h` for `TORIRS_GL_CLIENT_ES2`; and it is what `--gles2` /
`--gles2-zbuffer` and the "OpenGL ES 2" entry in Client Settings select. The
lane check forbids each lane's file on the other lane by name: a core may be
shared, a lane's name may not.

The core itself:

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

### The second GPU path is the ES 3.0 core, shared with WebGL2

`platform_renderer_es3_{core,ui,painter,zbuffer}.c` is the same renderer
rewritten against OpenGL ES 3.00 and GLSL ES 3.00, selected by `--gles3` /
`--gles3-zbuffer` and offered as "OpenGL ES 3". Its lane file is
`platform_androidarmv7_renderer_opengles3.c`; the browser's name for the same core is
`platform_web_renderer_webgl2.c`.

The device supports it: the XT1060's Adreno 320 reports `ro.opengles.version`
196608, which is ES 3.0 exactly -- no 3.1 and no Vulkan, so this is the ceiling
on this phone, not a step towards a higher one. The EGL config has to follow the
client version: EGL requires an ES3 context to come from a config advertising
`EGL_OPENGL_ES3_BIT_KHR`, and a lenient driver hands one back from an ES2
config anyway -- which is what makes getting it wrong a trap that works on the
device in front of you and fails on the next one. `platform_android_gl.c` asks
for
`EGL_RENDERABLE_TYPE` `0x0040` (`EGL_OPENGL_ES3_BIT_KHR`, spelled out because
it is an extension token an older header may not define) and client version 3
when the lane says `TORIRS_GL_CLIENT_ES3`.

What ES 3.0 buys over the ES 2.0 core, and why the core is a rewrite rather
than a flag:

- **32-bit indices.** The ES 2.0 core's whole resident-ring machinery --
  placement serials, an overwrite guard, fragmentation compaction -- exists
  because 16-bit indices cannot reach past 65,536 vertices. With
  `GL_UNSIGNED_INT` the index is absolute and the ring is simply deleted.
- VAOs, so a window change is one bind instead of a re-point per attribute;
- a std140 uniform block for the world matrix and clock;
- `glVertexAttribIPointer` and `texelFetch`, sized internal formats
  (`GL_RGBA8`, `GL_R8`), `GL_UNPACK_ROW_LENGTH`, `glDrawRangeElements`,
  `glInvalidateFramebuffer` and `GL_DEPTH_COMPONENT24`.

One thing the shared core may **not** use, and the reason the audit exists:
WebGL2 is not all of ES 3.0. `GL_TEXTURE_SWIZZLE_*` is absent there, so the
font mask is expanded in the fragment shader instead
(`vec4(1.0, 1.0, 1.0, texture(s_mask, uv).r)`). `tools/webgl_lane_audit.py`
enforces both directions: ES3-only tokens are forbidden in the ES 2.0 core, and
extensions are forbidden in both. See WEB-GL2-000 and ANDROID-GLES3-001 in
[`platform_quirks.md`](platform_quirks.md).

The only thing it needs from the platform is a context, and that seam is
`platform/platform_gl_context.h` -- whose `Create` takes the client version
(`TORIRS_GL_CLIENT_ES2` or `_ES3`), implemented twice:

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
| **AAudio** | Audio is OpenSL ES (`platform_audio_opensles.c`), not the newer API. AAudio does not exist below API 26 and this lane's floor is 21, on a phone from 2013 — a second backend the target device could never take. See ANDROID-AUDIO-001 in [`platform_quirks.md`](platform_quirks.md). |
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
| `TORIRS_HAVE_GLES2`, `TORIRS_HAVE_GLES3` | the two GPU lanes, **by their own source files** -- `platform_androidarmv7_renderer_opengles2.c` and `platform_androidarmv7_renderer_opengles3.c`, plus the `platform_renderer_es2_core.c` / `platform_renderer_es3_core.c` they call into. The browser's names for the same cores, `platform_web_renderer_webgl1.c` and `platform_web_renderer_webgl2.c`, are **forbidden** here, and vice versa on the web lane: a core may be shared, a lane's name may not. `TORIRS_HAVE_GL3` and `TORIRS_GL_ES2` are forbidden too, so `main.c` cannot hand this lane a desktop GL renderer. |
| `-lOpenSLES` **and** `platform_audio_opensles.c` | the audio device. Reverting it is a one-word edit — `platform_audio_null.c` defines exactly the same functions — and the result builds, boots and is silent. So the *source* is named too: `LANE_EFFECTIVE` carries `PLATFORM_SRCS`, and the null backend is forbidden here by name. Which implementation of a shared interface a lane picked is invisible in the flags. |

The desktop window library's link flags are **forbidden** by name in
`platform_check.mk` (`LANE_FORBID_android`), not merely absent — the way the
rule would be lost is someone adding them to a *shared* variable to fix another
host. And because a flag list cannot prove what is in a binary, the lane has a
post-link probe on the artifact itself:

```
lane-check: PLATFORM=android ok
lane-check: android artifact carries no SDL symbol
```

Two more checks read the renderer **sources**, because carrying both cores
means the link no longer proves either one's ceiling:
`lane-check-webgl1-es2` fails if an ES 3.0-only token appears in the ES 2.0
core, and `lane-check-webgl2-no-extensions` fails on an extension token in
either. Both run on the web lane too -- they are properties of the cores, not
of a host (`tools/webgl_lane_audit.py`).

---

## 11. Build and run

See **`android/README.md`** for the commands. In short:

```sh
make -C src PLATFORM=android ANDROID_ABI=armeabi-v7a OPT=1 all   # the .so
cd android && ./gradlew installDebug                              # the APK
tools/android_push_data.sh cache.osrs239                          # the data
adb logcat -s torirs                                              # stdout/stderr
```

### Plugins do not have to be pushed

`android_push_data.sh` copies `script/` and `config/` to the data root, and for
a long time that was the only way a phone got them. It is no longer. The lane's
IO executor is `platform_x_io.c`, whose `stored_file_read` has **two legs**:
the data root first, then an io_server over HTTP if one is named. That second
leg is not browser-only -- it is in the shared executor, and `platform_x_http.c`
is linked on this lane -- so Android fetches the plugin manifest, the Lua each
entry names, and each shipped asset *as a plugin asks for it*, exactly the way
the web lane does through `/boot/<path>`.

Name the server either way:

```sh
# one-off: environment, in the data root's env.txt (this wins)
adb shell "run-as com.torirs.client sh -c 'echo TORIRS_IO_SERVER=192.168.1.148:8390 >> files/env.txt'"
```

```ini
; permanent: the world's boot manifest states its file server like its game server
[io]
host=192.168.1.148
port=8390
```

and serve it from the machine that has the tree:

```sh
src/build/io_server --rev osrs239 cache.osrs239 \
    --boot-root . --script script --config config --port 8390 -v
```

Verified on the XT1060 on 2026-09-19 with `script/plugins` renamed away on the
device: the server logged `200 ./script/plugins/plugins.ini`, then
`performance_display.lua` and every asset, each fetched exactly once, and the
overlay drew. Nothing is written back to the data root -- see the
`stored_file_read` comment in `platform_x_io.c` for why a local copy is a
liability rather than a cache.

Both sides default to port 8088, so `TORIRS_IO_SERVER=192.168.1.148` alone
works when `io_server` is left on its default. Name the port on both or on
neither -- a `host` with no `:port` does not mean "whatever is listening".

## Plugin chrome: there is none on this lane

> **This section described a WebView that no longer exists.** The measurements
> below are kept because they are real and they are the reason the thing was
> removed, but `platform.mk` now gives Android an EMPTY
> `PLATFORM_CHROME_EXEC_SRC`, so `torirs_chrome_exec.c` falls back to its
> internal BUFFER sink. This is enforced, not merely the current state:
> `LANE_FORBID_android` names `TORIRS_CHROME_EXEC_WEB_AVAILABLE`,
> `TORIRS_CHROME_EXEC_BROWSER_AVAILABLE` and both of their sources, and
> `CHROME_EXEC_FORBID_LEGACY` names `ui/torirs_chrome_exec_android.c` and its
> define. Every external executor is forbidden on this lane by name, so the
> deleted WebView cannot come back by accident.

That is not the same as having no plugin window. BUFFER is "an internal sink
for that stream because the same model already draws itself in the game
canvas": `app->plugin_ui` is a full `ToriRSChrome` with widgets, dropdowns and
focus, and `app_chrome.c` emits its primitives into the frame like any other
chrome. What Android is missing is a **launcher** -- something that calls
`app_plugin_window_set_open`. There are three, and on this lane all three are
absent:

| launcher | why not here |
|---|---|
| the rail's own destinations | page allocation is presenter-owned (`app->plugin_rail_layout`), and only `torirs_chrome_exec_web.c`, `torirs_chrome_exec_winbrowser.c` and `platform_win32gdi.c` publish a `ToriRSChromeRailIntent`. BUFFER publishes none, so there is no rail to press. |
| the pop-out nav column | needs `[role:plugin_nav_column]` (interface 728 child 6) **on screen**, and the mobile toplevel mounts 728 hidden. A hidden column hands its destinations back to the rail by design. This is why Linux -- BUFFER chrome too -- is fine: there the column is visible. |
| a profile-authored button | `option_action=PLUGIN_PANEL` on a component the profile places, which `app_minimenu.c` turns into the toggle. Only `revconfig/rs245_2lc` authors one (`manage_plugins_button`, in the logout tab). The osrs239 dat2 profile authors none, because it has the nav column. |

There is also `TORIRS_CMD_PLUGIN_CHROME_TOGGLE` on the command bus, wired
straight through to the same call in `app_frame.c` -- with no producer anywhere
in the tree.

### The Stone Drawer carries one

The mobile gameframe now puts a third switch beside its chat and keyboard
switches: the OSRS wrench, which opens and closes the plugin window through
`client.plugin_window_show`. A frame that has replaced the lane's chrome
inherits the ways into the client's own windows the way it already inherits the
tab strip and the tutorial's blink, and it is gated on
`core.capability("client.plugin_window")` so a harness with no window behind it
gets no button rather than a dead one.

Verified on the XT1060, 2026-09-19: the switch draws, the tap logs `chrome:
plugin window executor = buffer (default)`, and the roster renders in-canvas
with working toggles. Two things to know:

- the window covers the switch row on a phone, so the switch cannot close what
  it opened -- the window's own title-bar X does;
- **this is the Stone Drawer's switch, and `auto` is not the Stone Drawer.** On
  an OldSchool cache `auto` deliberately means the cache's own mobile toplevel
  (601), so the phone's default configuration still has no launcher. Select it
  with `preferred_frame=mobile-gameframe/stone-drawer` in `preferences.ini` --
  qualified, or the host rejects it -- and `TORIRS_FRAME_ROLE_AUDIT=1` prints
  which frame won. Giving the cache's own frame a launcher wants the third kind
  above: a profile-authored `option_action=PLUGIN_PANEL` component.

A screen-space overlay draws itself and needs none of this, which is why the
performance display works on the phone and the roster does not. The full
accounting is ANDROID-CHROME-001 in
[`platform_quirks.md`](platform_quirks.md).

## Plugin chrome cost (measured 2026-09-02, XT1060 / API 22, WebView since removed)

Whole-process `simpleperf` windows of 10 s in Lumbridge, `--gles2-dualcore`,
one Lua plugin (Panel Demo) loaded, three arms: the WebView present with the
rail collapsed, the WebView expanded on the Manage Plugins roster, and no
WebView at all (`no_plugin_chrome` file in the data root -- that switch is
gone too; the third arm is simply what the lane does now).

| Arm | Samples (1 kHz) | WebView threads in sample | Frame | PSS |
|---|---:|---|---:|---:|
| no WebView | 10481 | none (21 threads) | 21.5 ms | 526 MB |
| WebView, rail collapsed | 9764 | none (49 threads) | 22.0 ms | 560 MB |
| WebView, roster open, before the gate | 6360 | none; frame thread 90% painting a destroyed surface | 23.0 ms | 586 MB |
| WebView, roster open, with the gate | 3901 | none; frame thread at the logic floor | 20.5 ms | 417 MB |

The browser costs memory and threads, not CPU: in steady state its threads
never appear. The one real waste was the game's own frame thread: a
phone-width page is exclusive, the SurfaceView is hidden and its surface
destroyed, and the client kept rendering the world into nothing at about 0.6
of a core. `PlatformWindow_CanPresent` now answers false without a surface and
the frame loop skips the draw while the world and network keep ticking. The
WebView itself holds 60 fps (requestAnimationFrame mean 16.5 ms) with the rail
alone and while scrolling the open roster.
