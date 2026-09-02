# ToriRS on Android

The client as a native Android app: a raw `ANativeWindow` and EGL with no
windowing library, the tree's own software rasterizer, and the lane's own
GLES2 renderer as an opt-in GPU path (`--gles2` / `--gles2-zbuffer`).

For *how it works* — the threading, the surface lifecycle, the frame path, the
NEON split — see **[`docs/android_architecture.md`](../docs/android_architecture.md)**.
This file is how to build it and run it.

---

## What you need

| | |
|---|---|
| **Android SDK** | `~/Library/Android/sdk` (or `$ANDROID_HOME`) |
| **NDK 27.0.12077973** | pinned. `sdkmanager "ndk;27.0.12077973"` |
| **JDK 21** | AGP 8.7 refuses anything newer. Pinned in `gradle.properties`. |
| **A device** | API 21+ (Android 5.0). USB debugging on. |

The NDK version is pinned in **two** places that must agree:
`ANDROID_NDK_VERSION` in `src/platform/platform.mk` and `ndkVersion` in
`android/build.gradle`. If the pinned NDK is missing, the makefile warns and
falls back to the newest installed one.

If your JDK 21 is elsewhere, edit `org.gradle.java.home` in
`android/gradle.properties`. `/usr/libexec/java_home -V` lists what you have.

---

## Build and run, in four steps

```sh
# 1. the native client  (make owns this; Gradle does not build C)
make -C src PLATFORM=android ANDROID_ABI=armeabi-v7a OPT=1 all

# 2. the APK
cd android && ./gradlew installDebug && cd ..

# 3. the data -- manifests, revconfig, and whichever caches you want
tools/android_push_data.sh cache.osrs239

# 4. watch it. stdout and stderr are redirected to logcat.
adb logcat -s torirs
```

Then launch **ToriRS** on the device and pick a profile from the boot menu.

### Which ABI

`ANDROID_ABI` picks the architecture; it defaults to `armeabi-v7a` because that
is the *oldest* thing this lane targets, and a default that only works on a
modern device hides the lane's real constraint.

```sh
adb shell getprop ro.product.cpu.abilist     # what your device wants
```

| device | build |
|---|---|
| 32-bit ARM (older phones) | `ANDROID_ABI=armeabi-v7a` *(default)* |
| 64-bit ARM (most phones since ~2015) | `ANDROID_ABI=arm64-v8a` |
| emulator on an x86 host | `ANDROID_ABI=x86_64` |

Each ABI gets its own object directory, so building both never mixes objects.
`build.gradle` packages whichever `.so` files are present:

```sh
make -C src PLATFORM=android ANDROID_ABI=armeabi-v7a OPT=1 all
make -C src PLATFORM=android ANDROID_ABI=arm64-v8a  OPT=1 all
cd android && ./gradlew installDebug
```

### Debug build

`OPT=0` keeps `assert()` live — including the allocation-failure asserts, which
`NDEBUG` compiles out of a release build. On a memory-constrained device that is
the difference between an abort naming the file and line, and a bare SIGSEGV.

```sh
make -C src PLATFORM=android ANDROID_ABI=armeabi-v7a OPT=0 all
```

---

## Selecting a manifest

A manifest is how this client is told which world to boot (`--manifest <path>`),
and on a phone there is no command line to put it on. Three ways, in order of
how often you will want them:

### 1. The boot menu (normal)

Launching the app lists every manifest under
`/sdcard/Android/data/com.torirs.client/files/manifests/`:

```
ToriRS
select a profile
booting osrs239 bench in 2.3s  -  tap a profile to choose

  osrs239 bench            cache.osrs239
  osrs239 worldmap         cache.osrs239
  rs289lc                  no cache directory stated
  osrs239 rs2012           needs the embedded server - this build is a client only
  bench local              cache missing: cache.osrs239.sparse
```

- White rows are bootable. Grey rows are not, **and say why** — they are listed
  rather than hidden, because a missing cache is the most likely thing to be
  wrong on a fresh device.
- The countdown boots the default after ~4s. **Any touch cancels it.**
- The default is the last profile you booted, remembered on the device.
- **The menu rotates; the client does not.** The menu is a vertical list and
  shows about twice as many rows in portrait, so its orientation is free
  (`fullSensor`). The client is locked landscape because its canvas is a
  765x503 landscape frame, which portrait would letterbox into a band.
  Rotating the menu does not restart the countdown.

### 2. Push a different manifest

The menu is a directory listing, so adding a profile is adding a file:

```sh
adb push manifests/manifest_osrs239_worldmap.ini \
    /sdcard/Android/data/com.torirs.client/files/manifests/
```

Re-running `tools/android_push_data.sh` with no cache arguments re-pushes all
manifests and the revconfig tree in about a second — do that after editing one.

### 3. Extra arguments

`extra_args.txt` in the data root is appended to the client's argv, one argument
per line. This is how you try a profile with a flag without rebuilding anything:

```sh
cat > /tmp/extra_args.txt <<'EOF'
# the GLES2 GPU renderer instead of the software rasterizer
# (--gles2-zbuffer for its depth-buffered world pass)
--gles2
EOF
adb push /tmp/extra_args.txt /sdcard/Android/data/com.torirs.client/files/
```

Blank lines and `#` comments are ignored.

---

## Touch, and the profile editor

**Gestures.** Tap = click, long-press = right-click, **drag on the 3D viewport =
camera**, pinch = zoom, two-finger pan = camera. Enabled for every revision; no
profile has to opt in.

**The inkwell** is the touch marker, shown for *every* touch — unlike the X,
which only appears when a click resulted in something. Configured per profile:

```ini
[component:cross@mobile]
type=inkwell
style=splash          ; splash | blot | ripple
walk_color=yellow
interact_color=red
```

`@mobile` sections load only on touch devices and override the unsuffixed
section above them. To compare the styles on a desktop:

```sh
TORIRS_REVCONFIG_PLATFORM=mobile ./src/torirs --manifest manifests/manifest_osrs239_bench.ini
```

**The keyboard** comes up when a text field takes focus (login form, chat line,
or a plugin asking for input) and goes away with the **Hide keyboard** button
that appears while it is up. That button exists because Back also closes the
keyboard on most devices — but Back is *also* the client's Escape, so pressing
it would close the interface you were typing into.

While the keyboard is up, the activity reports how much of the surface it
covers (`nativeKeyboardInset`, from the API 30+ ime inset or the older
visible-frame comparison), and the client slides its chat sheet and the login
stone box above it. On a device where neither signal fires the report stays 0
and the layout simply keeps its old bottom-pinned behaviour.

**The gear** in the boot menu edits each profile's server host/port, cache/CRC
host/port, cache directory, IO server host/port, and renderer. Useful because a
server's DHCP lease moves and every profile pointing at it goes stale, with no
other way to fix it from the device.

Prefer **names over addresses** in `host=`: your router serves DNS for its own
clients, so `matthewllm` follows the lease. Note `.local`/mDNS does **not**
resolve on Android 5.1; the bare name and the `.lan` suffix both do.

## Why the data is pushed and not bundled

The client reads its cache with ordinary stdio. **An APK asset is not a file** —
it is a compressed range inside the `.apk` that only `AssetManager` can open —
so bundling would not work even before you consider that a rev-239 cache is
218 MB.

The device layout **mirrors the repo**, because a manifest names its cache and
RevConfig relative to *itself* (`dir=../cache.osrs239`). So every manifest
resolves on the phone exactly as on the desktop, unedited:

```
/sdcard/Android/data/com.torirs.client/files/
    manifests/       ← the boot menu reads this
    revconfig/       ← what revconfig_ui= points at
    cache.osrs239/   ← what dir= points at
    extra_args.txt   ← optional
    preferences.ini  ← written by the client
```

That directory needs no storage permission on any API level and is removed when
the app is uninstalled.

---

## Renderer

The **software rasterizer is the default** and needs nothing from the device.

The GPU path is the **GLES2 renderer**
(`src/platform/platform_renderer_gles2_*.c`), opted into with `--gles2`
(painter order) or `--gles2-zbuffer` (hardware depth) via `extra_args.txt` or
the profile editor. OpenGL ES 2.0 core, no extensions; shaped after the Windows
D3D9 renderer's retained model rather than either desktop GL renderer. The web
lane links the same four files against WebGL1. See
[`docs/android_architecture.md`](../docs/android_architecture.md) §4.

`--opengl3` names the desktop GL 3.2 renderer and `--webgl1` is the browser's
spelling for this same renderer on a WebGL1 context. Both are refused here and
say so -- the WebGL1 flag is not aliased, so a manifest written for the browser
cannot run on a phone unnoticed; a device manifest carrying
`arg=--webgl1-zbuffer` must say `arg=--gles2-zbuffer`.

---

## Sound

Audio is on, through **OpenSL ES** (`src/platform/platform_audio_opensles.c`),
and needs nothing turned on and nothing pushed: the clips and the music come
out of whichever cache the profile named. The player sits on the **media**
stream, so the volume rocker adjusts it while the client is in front, and it
**pauses when you leave the app** and resumes when you come back -- the frame
loop keeps running without a Surface, so nothing else would stop it.

AAudio is not used. It does not exist below API 26 and this lane's floor is 21;
see ANDROID-AUDIO-001 in
[`docs/platform_quirks.md`](../docs/platform_quirks.md).

To check it from the host rather than by ear:

```sh
# one active track for the client, 22050Hz, Type 3 (music), no underruns
adb shell dumpsys media.audio_flinger | grep -A4 Tracks
```

`TORIRS_AUDIO_TRACE=1` in `env.txt` prints the mixer's ledger when the client
exits, and `TORIRS_AUDIO_WAV=<path under the data root>` tees everything the
device was given into a WAV you can pull off and open -- which is the only way
to answer "it sounds wrong" on a phone. `TORIRS_SIM_SONG=<id>` /
`TORIRS_SIM_SOUND=<id>` play something without a server to fire it.

---

## No embedded server

This lane is a **client**. `EMBED_SERVER` stays 0, so a manifest carrying
`[net:boot] transport=embed` cannot boot here — the boot menu refuses it by name
rather than letting it start and connect to nothing (`net_transport_embed.c`
compiles to a silent stub without the server).

Use a manifest with `transport=tcp` or `ws` and point it at a server you are
running, or an offline manifest with no `[net:boot]` at all
(`manifest_osrs239_bench.ini`, `manifest_osrs239_worldmap.ini`).

### Pointing a profile at a server on your LAN

Most manifests say `host=localhost`, which on a phone means *the phone*. Two
ways to redirect, and the first is usually what you want:

**Override on the command line.** `extra_args.txt` beats the manifest
(precedence is CLI > manifest > defaults), and `--connect` sets **both** the
game connection and the on-demand cache host — `App_Init` reads
`cfg->connect_target` for the dat1 on-demand stream, so one flag covers both:

```sh
printf -- '--connect\n192.168.1.236\n' > /tmp/extra_args.txt
adb push /tmp/extra_args.txt /sdcard/Android/data/com.torirs.client/files/
```

Note that `extra_args.txt` applies to **every** profile, so remove it before
booting an offline one — a `--connect` on an offline manifest turns networking
on.

**Or forward the port**, when the server is on this development machine:

```sh
adb reverse tcp:43594 tcp:43594     # phone's localhost:43594 -> this machine
```

`adb reverse` only reaches *this* machine. For a server on another host, use
`--connect` and make sure the phone is on the same network — check with
`adb shell ip addr show wlan0`, and `adb shell svc wifi enable` if WiFi is off.

### `source=ondemand` profiles need their server to boot at all

`manifest_rs289lc.ini` and friends set `[cache:boot] source=ondemand`: the cache
itself is streamed from the LostCity server. With no server reachable the client
cannot read a single archive and `App_Init` **asserts** rather than limping —
correct behaviour, but it means such a profile has nothing to show offline.

The boot menu marks these in amber ("streams its cache from …") and **never
auto-selects one** for the countdown while a self-contained profile exists. You
can still tap it, which is the right split: an explicit tap is someone who knows
their server is up.

---

## Troubleshooting

**Nothing in the boot menu.** The data was never pushed. The screen says so and
names the directory it looked in. Run `tools/android_push_data.sh`.

**`android_push_data.sh` says the app directory does not exist.** Android
creates `Android/data/<pkg>/` on install. Install the APK once first.

**A profile is grey.** The row says why: a missing cache (push it), or an
`embed` transport (see above).

**Black screen after choosing a profile.** `adb logcat -s torirs` — the client's
stdout and stderr go there, so whatever it would have printed in a terminal is
visible.

**`UnsatisfiedLinkError`.** The APK has no `.so` for the device's ABI. Check
`adb shell getprop ro.product.cpu.abilist` and build that one.

**A native crash.** The `.so` keeps its debug symbols, so:

```sh
adb logcat | $ANDROID_HOME/ndk/27.0.12077973/prebuilt/darwin-x86_64/bin/ndk-stack \
    -sym android/src/main/jniLibs/armeabi-v7a
```

An `OPT=1` build is compiled with LTO, which moves functions around and makes
`addr2line` unreliable. Reproduce with `OPT=0` before trusting a symbol name —
and `OPT=0` also keeps the `assert()`s that name a failed allocation.

---

## What Gradle does and does not do

It builds the APK. **It does not build C** — there is deliberately no
`CMakeLists.txt`. `src/platform/platform.mk` is the only thing in this tree that
knows what a platform is; a second build description would have to restate every
source file, every `-D` and every include path, and the two would drift silently
because a stale duplicate still compiles.

So `make` writes `android/src/main/jniLibs/<abi>/libtorirs.so` and Gradle
packages whatever is there. **After a native change, re-run `make` before
`./gradlew`** — Gradle cannot know the `.so` is stale.
