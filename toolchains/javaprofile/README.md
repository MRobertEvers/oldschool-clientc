# Profiling the Java client on the Windows XP target

Everything needed to reproduce the Java-side numbers in `docs/java_parity/`:
frame rate, per-kernel draw census, per-kernel ablation, and raw JIT-compiled
machine code.

Nothing here is downloaded at build time. `hsdis-i386.dll` is vendored beside
this file.

---

## 0. What is in this directory

| file | what it is |
|---|---|
| `hsdis-i386.dll` | HotSpot's external disassembler plugin, 32-bit x86 |
| `hsdis-COPYING.txt` | its licence |

`hsdis-i386.dll` is the **FCML 1.3.0** `win32-x86` build
(<https://github.com/swojtasiak/fcml-lib/releases/tag/v1.3.0>). FCML is a
clean-room disassembler, which is why this binary is redistributable — the
binutils-based hsdis builds are GPLv3 and cannot be shipped with a GPLv2 JVM.

**Do not substitute the build from `chriswhocodes.com/hsdis/hsdis-i386.dll`.**
Despite the name it is a **PE32+ x86-64** image and will not load into the
32-bit JVM. Check before trusting any hsdis binary:

```powershell
# prints "machine = i386 (32-bit)" for a usable one
$fs=[IO.File]::OpenRead("hsdis-i386.dll"); $br=New-Object IO.BinaryReader($fs)
$fs.Position=0x3C; $pe=$br.ReadInt32(); $fs.Position=$pe+4
"machine = 0x{0:X}" -f $br.ReadUInt16(); $br.Close(); $fs.Close()
```

`0x14C` is i386, `0x8664` is x86-64.

---

## 1. The environment

* **XP box** `rpdxp` at `http://10.10.10.2:8088`, single core, 1 GB RAM.
  Endpoints used below: `/fs/put`, `/fs/get`, `/scripts/put`, `/scripts/run`
  (**POST**, capped at 120 s).
* **Staging** `C:\dev\mem289` — holds `client.jar`, the cache manifests and the
  built clients.
* **JVM** `C:\Program Files\Java\jdk1.8.0_151`. It reports
  `Java HotSpot(TM) Client VM`, i.e. **32-bit, C1 only, no C2**. Every piece of
  assembly below is C1 output.
* **Server** LostCity `Engine-TS` branch `289` with `engine.revision 289`,
  web 80, game 43594. The client hardcodes `127.0.0.1` for its code base, so the
  XP box needs the port proxy described in `docs/2004Scape_Memory_Usage.md` §1.4.

---

## 2. Building an instrumented Java client

The client source is in `LostCity_Server/javaclient`. **Gradle is not needed** —
the repository ships a built `build/libs/client.jar` and a matching
`build/classes/java/main` tree, so one patched class can be compiled against it
and swapped into a copy of the jar.

Patch scripts live in `tools/mem/`:

| script | what it adds |
|---|---|
| `java_pix3d_census_patch.py` | triangle / span / pixel counters in `Pix3D`, printed every 2 s |
| `java_pix3d_ablate_patch.py` | per-kernel ablation switches in `Pix3D` |

```sh
JC=/path/to/LostCity_Server/javaclient
T=/tmp/jprof && mkdir -p $T/jsrc/jagex2/dash3d $T/jout

# Patch a COPY. Never edit the checked-out client.
cp $JC/src/main/java/jagex2/dash3d/Pix3D.java $T/jsrc/jagex2/dash3d/Pix3D.java
python tools/mem/java_pix3d_census_patch.py $T/jsrc/jagex2/dash3d/Pix3D.java

# Compile that one class against the prebuilt tree, targeting Java 8.
javac --release 8 -nowarn -cp "$JC/build/classes/java/main" \
      -d $T/jout $T/jsrc/jagex2/dash3d/Pix3D.java

# Swap it into a copy of the jar.
cp $JC/build/libs/client.jar $T/client_census.jar
( cd $T/jout && jar uf $T/client_census.jar jagex2/dash3d/Pix3D.class )

# Ship it.
curl -s -X POST --data-binary @$T/client_census.jar \
  "http://10.10.10.2:8088/fs/put?path=C:/dev/mem289/client_census.jar"
```

### Measuring the frame rate — do this first, always

`Pix2D.cls()` is called exactly once per frame from `gameDrawMain`, so counting
it counts frames. **This is not optional.** The Java client renders **31 fps**
on this box, not the 50 everyone assumes, and until that was measured every
"% of one core" comparison against it was wrong. It also decides whether an
ablation is readable at all — see §5.

```sh
cp $JC/src/main/java/jagex2/graphics/Pix2D.java $T/jsrc/jagex2/graphics/Pix2D.java
python tools/mem/java_pix2d_fps_patch.py $T/jsrc/jagex2/graphics/Pix2D.java
javac --release 8 -nowarn -cp "$JC/build/classes/java/main" \
      -d $T/jout $T/jsrc/jagex2/graphics/Pix2D.java
( cd $T/jout && jar uf $T/client_census.jar jagex2/graphics/Pix2D.class )
```

---

## 3. Booting it on the XP box and logging in

The Java client takes no user/pass on its command line, so the login is typed at
it. `tools/mem/xp_java_login_measure.py` does the whole thing; it reads
`C:\dev\memjob.json`:

```json
{"exe": "C:/Program Files/Java/jdk1.8.0_151/bin/java.exe",
 "args": ["-Xmx64m", "-jar", "C:/dev/mem289/client_census.jar",
          "10", "0", "highmem", "members", "32"],
 "cwd": "C:/dev/mem289", "label": "jcensus",
 "username": "someFreshName", "password": "a",
 "detach": true, "env": {}}
```

```sh
curl -s -X POST --data-binary @memjob.json \
  "http://10.10.10.2:8088/fs/put?path=C:/dev/memjob.json"
curl -s -X POST "http://10.10.10.2:8088/scripts/run?name=xpjava.py"
```

Four things that each cost a wasted run:

* **Fresh account every time.** The previous character stays in the world and a
  repeat login is answered `reply=5`, leaving the client on the title screen.
* **`/scripts/run` needs POST.** GET returns 404.
* **Single core** — never leave the other client running while measuring.
* **Verify in-world before believing any number.** Working set does not tell you:
  Java sits at ~82 MB on the title screen too, because the cache archives load
  either way. Use the census — in-world is ~250,000 tris/s, the title screen is
  0. An arm that failed to log in draws the title-screen flames at 5-10 % of a
  core, which reads as a spectacular ablation win.

---

## 4. Getting raw JIT-compiled assembly

Install the plugin where the **Client** VM will find it (note: `client`, not
`server` — this JVM runs the client compiler):

```sh
curl -s -X POST --data-binary @toolchains/javaprofile/hsdis-i386.dll \
  "http://10.10.10.2:8088/fs/put?path=C%3A%2FProgram%20Files%2FJava%2Fjdk1.8.0_151%2Fjre%2Fbin%2Fclient%2Fhsdis-i386.dll"
```

The path must be URL-encoded — `/fs/put` rejects the raw spaces in
`Program Files`.

Then run the client with `CompileCommand=print` for the methods you want.
Restrict it to named methods: `-XX:+PrintAssembly` over the whole VM is tens of
megabytes of startup code.

```
-XX:+UnlockDiagnosticVMOptions
-XX:CompileCommand=print,jagex2/graphics/Pix2D.cls
-XX:CompileCommand=print,jagex2/dash3d/Pix3D.gouraudRaster
-XX:CompileCommand=print,jagex2/dash3d/Pix3D.textureRaster
-XX:CompileCommand=print,jagex2/dash3d/Pix3D.flatRaster
```

`tools/mem/xp_java_asm.py` wraps launch, login, a 50 s soak so the JIT settles,
and a clean shutdown. Output lands in `C:\dev\mem289\java_asm.log`; fetch it:

```sh
curl -s "http://10.10.10.2:8088/fs/get?path=C:/dev/mem289/java_asm.log" \
     --output java_asm.log
grep -n "Decoding compiled method\|# {method}" java_asm.log
```

A working dump contains `Decoding compiled method` blocks. If hsdis did not
load you get the method headers with no instructions and no error.

Without hsdis these still work and need no plugin:

* `-XX:+PrintCompilation` — what was compiled, at which tier, and deopts. `%`
  marks an on-stack-replacement compile, i.e. a hot loop.
* `-XX:+PrintInlining` (with `-XX:+UnlockDiagnosticVMOptions`) — what was
  inlined into what.
* `-XX:+LogCompilation -XX:LogFile=<path>` — the same as XML.

A copy of the dump this produced is committed at
`docs/java_parity/data/java_jit_assembly.txt`.

---

## 5. Ablating a Java kernel, correctly

`java_pix3d_ablate_patch.py` makes each span kernel return at entry under its
own environment variable. Two traps, both of which produced wrong answers here
before being understood:

**The flag must be `volatile`.** A `static final boolean` is a compile-time
constant: HotSpot folds it, the method body becomes dead, it inlines to nothing,
and the JIT then dead-code-eliminates the *callers'* argument computation — the
whole edge walk, not just the fill. Java appeared to fall from 50.3 % to 4.2 %
of a core, which is not a rasterisation measurement.

**The client must hit its frame cap in every arm, or the deltas are
meaningless.** Java's baseline runs at 31 fps, i.e. it is *missing* its cap, so a
client made cheaper spends the saving on frame rate rather than on CPU.
Ablating gouraud alone moved its CPU by **zero** (50.3 % → 50.3 %) while
`gspans/s` went 1,608,944 → 0 and pixels fell 41 %. Only the all-kernels arm
crossed the cap (49.5 fps) and showed a cost.

So: **report fps in every arm, and compare CPU milliseconds per frame, never
CPU %.**

---

## 6. Reference results

From `docs/java_parity/`, XP target, in-world, rev-289 world:

| | fps | CPU ms/frame | pixels/frame | raster ms/frame | ns/px |
|---|---|---|---|---|---|
| torirs | 50.0 | 15.15 | 341,692 | 6.40 | 18.7 |
| Java | 31.0 | 16.23 | 355,744 | 15.26 | 42.9 |
