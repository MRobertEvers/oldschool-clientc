# RuneLite on the Windows XP machine

How the repo's RuneLite lane (patched RuneLite fork + Deob-recompiled instr
gamepack + ToriRSServer) was built on the Windows 11 machine (10.10.10.1) and
deployed to the Windows XP machine (10.10.10.2), connecting back to the game
server running on the Windows 11 machine. Everything on the XP box is driven
remotely through RemoteProxyDesktopXP (rpdxp) at `http://10.10.10.2:8088`.

## The problem

The XP box has JRE/JDK 1.8.0_151 — the newest Java that runs on XP. Java 8
loads class-file version **52** and below. The RuneLite fork
(`C:\Users\mrobe\Documents\git_repos\Runelite`, branch torirs-mock230) builds
at `--release 11` (class 55), and the Deob instr gamepack
(`C:\Users\mrobe\Documents\git_repos\Deob`) compiles at the JDK 21 default
(class 65). Neither source tree can simply be retargeted to `--release 8`:

* the fork uses `var`, `List.of`/`Set.of`/`Map.of`, `String.repeat`,
  `InputStream.transferTo`, `Files.readString` — hundreds of sites;
* the vanilla client inside the gamepack references Java 9+ APIs
  (`sun.misc.Unsafe` via `jdk.unsupported`, `ProcessHandle`,
  `InaccessibleObjectException`) on runtime-guarded, lazily-resolved paths.
  `--release 8` refuses to compile them even though they never execute on
  JRE 8.

So the strategy is: **build everything at its native release, then
bytecode-downgrade the produced jars to class-file 52 with
[JvmDowngrader](https://github.com/unimined/JvmDowngrader) 2.0.1**
(`xyz.wagyourtail.jvmdowngrader:jvmdowngrader:2.0.1:all` from Maven Central).

## 1. Build the RuneLite fork (Windows 11)

Stock build, JDK 21, default release 11:

```sh
cd /c/Users/mrobe/Documents/git_repos/Runelite
./gradlew.bat :client:jar -x test -x checkstyleMain -x checkstyleTest
```

Notes:

* The client project's task path is `:client`, not `:runelite-client`
  (`settings.gradle.kts` maps `project(":client")` onto the `runelite-client`
  directory). `runelite-api` and `jshell` are included builds and build as
  dependencies of `:client:jar`.
* `common.settings.gradle.kts` gained a `-PjavaRelease=N` knob and
  `cache/src/main/java` had 19 Java-9+-isms replaced with Java-8-compatible
  forms (all still valid Java 11) from an earlier "compile at release 8"
  attempt that was abandoned. Both changes are inert at the default
  (`javaRelease` defaults to 11).

To collect the exact resolved runtime classpath, a `copyXpRuntime` task was
appended to `runelite-client/build.gradle.kts`:

```kotlin
tasks.register<Copy>("copyXpRuntime") {
    from(configurations.runtimeClasspath)
    into(layout.buildDirectory.dir("xp-runtime"))
}
```

`./gradlew.bat :client:copyXpRuntime` materializes all 47 dependency jars into
`runelite-client/build/xp-runtime/`.

## 2. Build the instr gamepack (Windows 11)

`Deob/instr/build_xp.sh` is `instr/build.sh` adapted for Git Bash on Windows:
`cygpath -m` paths (Windows javac reads `/c/...` as relative), `;` classpath
separators, and **no `--release 8`** (see above). It compiles `instr/src`
against `tmp/deob_run_rl1_12_33/runelite-api-1.12.33-runtime.jar` (staged from
`oldschool-clientc/toolchains/java-toolchain-osrs239.zip` → `deob/`), copies
the prebuilt bouncycastle stubs in, runs the `VarbitMaskRegression` gate, and
writes `instr/build/out/injected-client-1.12.33-instr.jar` (740 classes).

```sh
cd /c/Users/mrobe/Documents/git_repos/Deob
bash instr/build_xp.sh          # "ok revision-239 varbit masks"
python instr/tools/verify_api.py   # "API surface complete."
```

The RSA modulus compiled into the gamepack already matches
`manifests/manifest_osrs239.ini`'s `rsa_mod` (checked against
`set_modulus.py --show`), so no key work was needed.

### The gamepack's host allowlist

The deob client validates the jav_config `codebase` host
(`instr/src/class510.java`, `method11205`): `jagex.com`, `*.jagex.com`,
`runescape.com`, `*.runescape.com`, hosts ending `127.0.0.1`, and — after
stripping trailing digits — `192.168.1.*`. Anything else shows
**error_game_invalidhost** in the applet. The crossover-link host `10.10.10.1`
failed this, so `class510.java` gained a `10.10.10.` branch mirroring the
existing `192.168.1.` one. (The RuneLite fork itself has no such allowlist —
that check lives in the gamepack.)

### The gamepack's ProcessHandle telemetry

`instr/src/class571.java` `method12456` read the process and parent-process
names via `ProcessHandle.current()` (Java 9+) for Jagex telemetry. After the
downgrade that resolves to JvmDowngrader's
`xyz.wagyourtail.jvmdg.j9.intl.WindowsProcessHandle`, whose `readInfo` shells
out to read process info and **blocks forever on XP** — the Client thread
hangs at boot, right after "Prepared Sound Engine", inside
`BufferedReader.readLine`. (Found with a remote `jstack -l`; the JDK on the
box, `jdk1.8.0_151`, provides it.) The two ProcessHandle blocks were removed
from `method12456` — the fields are telemetry this server never receives —
keeping the `RuntimeMXBean` JVM-args part, which is plain Java 8.

## 3. Downgrade the jars to class-file 52

Of the 47 runtime jars + client jar + instr jar, most are old enough already.
A scan of every `.class` header found exactly these above 52:

| jar | class version |
|---|---|
| flatlaf-3.2.5-rl4, flatlaf-extras-3.2.5-rl4, jsvg-1.2.0 | 53 |
| client-1.12.35-SNAPSHOT, runelite-api-1.12.35-SNAPSHOT, jshell-1.12.35-SNAPSHOT | 55 |
| injected-client-1.12.33-instr | 65 |

Each was downgraded **unshaded**, with the full jar set on `--classpath` so
cross-jar references resolve during stack-frame computation:

```sh
java -jar jvmdowngrader-all.jar -c 52 downgrade \
    --target in.jar out.jar --classpath "<all jars ; separated>"
```

Two support jars complete the runtime:

* `jvmdg-api-52.jar` — the downgraded Java-API stub jar, generated by
  `java -jar jvmdowngrader-all.jar -c 52 debug downgradeApi jvmdg-api-52.jar`.
  Downgraded code calls stubs like
  `xyz.wagyourtail.jvmdg.j9.stub.java_base.J_L_I_MethodHandles`; this jar
  provides them.
* `jvmdg-runtime-support.jar` — `xyz/wagyourtail/jvmdg/exc/**` and
  `xyz/wagyourtail/jvmdg/util/**` extracted from the CLI jar (18 classes,
  class 51). The api jar does **not** contain these, and downgraded code
  references `exc.MissingStubError` wherever a Java 9+ API had no stub (the
  gamepack's guarded `Unsafe`/`ProcessHandle` paths) and `util.Utils` from the
  stubs themselves.

**Do not use the `shade` subcommand for this.** The first attempt chained
`downgrade ... - shade --prefix xpdg... - out.jar` per jar; the shaded
`J_L_I_MethodHandles` still referenced the *unshaded*
`xyz.wagyourtail.jvmdg.util.Utils`, which the shade pass had not copied in,
and every config-default-method call on the XP box died with
`NoClassDefFoundError` in its static initializer. Unshaded + the two support
jars on the classpath is the arrangement that works.

Two mac-only unresolved-class warnings (`com/apple/eawt/FullScreenAdapter`)
are expected and harmless on Windows.

The verified deploy set is **38 jars** (~28 MB): the 36-jar client set (the
xp-runtime set with the vanilla `injected-client-1.12.35-SNAPSHOT.jar`
**replaced** by the instr gamepack, and only the `windows-x86` lwjgl natives
kept — the XP box is 32-bit; linux/macos/arm64/win-x64 natives dropped) plus
the two support jars. Nothing above class 52 — re-scan after every downgrade.

## 4. Server side (Windows 11, 10.10.10.1)

Three pieces, all reachable from the XP box:

1. **ToriRSServer** — `src/build_win64_opt/torirsserver.exe 43596 --rev
   osrs239`, run **from the repo root**: the defaults `cache.osrs239` and
   `OSRS-Content/osrs239-content` resolve relative to the cwd, and from the
   wrong cwd the process dies before its (block-buffered when redirected)
   banner ever flushes — i.e. silently. It serves both the game protocol and
   JS5 on one socket (first byte 14 = game, 15 = JS5), binds
   `127.0.0.1:43596` only, and is up when it logs
   `listening on 127.0.0.1:43596, wire osrs239`. Its lifetime is tied to
   whatever console/session started it — when it dies, the forwarder starts
   answering `connection refused` upstream and the XP client shows "error
   connecting to server" at login (JS5/title screen may still have worked
   moments earlier from the same process).
2. **TCP forwarder** — bridges `0.0.0.0:43594` → `127.0.0.1:43596`
   (`tcp_forward.py`, a dumb byte pipe; both protocols are opaque streams on
   one socket). Port 43594 is load-bearing: the client computes it from
   `environment=0` ("live fixed ports"), which keeps `world_id=1` clean.
3. **jav_config server** — `tools/torirs_javconfig.py` gained a `--bind`
   option (it previously hard-bound 127.0.0.1):

   ```sh
   python tools/torirs_javconfig.py --bind 0.0.0.0 --host 10.10.10.1 \
       --port 8080 --revision 239 --world-id 1 --environment 0 \
       --cachedir torirs239
   ```

**Firewall:** the crossover interface ("Ethernet 3", 10.10.10.1) is on the
*Public* profile, default-inbound-Block, and the session was not elevated. The
existing inbound allow rule for
`C:\users\mrobe\appdata\local\python\pythoncore-3.14-64\python.exe` (any port,
any profile) is what lets both python listeners accept from the XP box — so
both **must** run under that exact python.exe. No new rules, no elevation.

Sanity checks from the Windows 11 side, through the real 10.10.10.1 address:

```sh
curl http://10.10.10.1:8080/jav_config.ws            # config text
# JS5 handshake: p1 15, p4 revision, p4 seed x4 -> p1 status (0 = OK)
python -c "import socket; s=socket.create_connection(('10.10.10.1',43594));
s.sendall(bytes([15])+(239).to_bytes(4,'big')+bytes(16)); print(s.recv(1))"
```

No client-side cache seeding was done: the client fills
`jagexcache\torirs239\LIVE` over JS5 from the server (first boot is slower;
that's all).

## 5. Deploy to the XP box (rpdxp)

Everything goes through `http://10.10.10.2:8088`. GETs: `/info`,
`/fs/list?path=`, `/fs/get?path=`. POSTs: `/fs/mkdir?path=`, `/fs/put?path=`
(raw streamed body), `/scripts/put?name=`, `/scripts/run?name=` (streams
stdout, **120 s timeout** — anything long-running must detach).

```sh
curl -s -X POST http://10.10.10.2:8088/fs/mkdir?path=C:%5Cdev%5Crunelite
curl -s -X POST http://10.10.10.2:8088/fs/put?path=C:%5Cdev%5Crunelite%5C<name>.jar \
    --data-binary @<name>.jar -H Expect: --max-time 300
```

Deploy gotchas, each of which cost one round:

* `-H Expect:` is required — curl sends `Expect: 100-continue` for bodies
  over 1 KB and rpdxp's server never answers it (empty reply, curl exit 52).
* `/fs/put` into a directory that does not exist also fails with an empty
  reply, not an error body. `mkdir` first.
* URL-encode backslashes (`%5C`).

All 38 jars land in `C:\dev\runelite\`. A jar cannot be overwritten while a
JVM has it loaded — Windows holds the file locked and `/fs/put` dies
mid-stream (curl exit 56, connection reset); run `kill_java.py` first. The XP profile in use is
`C:\Documents and Settings\new`, so the client's cache appears at
`C:\Documents and Settings\new\jagexcache\torirs239\LIVE` on its own.

## 6. Launch (detached) and iterate

`run_runelite_detached.py` (uploaded via `/scripts/put`, started via
`/scripts/run`) follows the `docs/winxp_profiles/run_torirs_perf_detached.py`
pattern exactly: the launcher re-execs itself `--child` with
`CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP` and returns immediately
(beating the 120 s script timeout); the child waits on the JVM with
stdout+stderr redirected to `C:\dev\runelite\runelite.log` and writes
`runelite.rc` on exit — the marker distinguishing "stopped" from "running".
`CREATE_NEW_CONSOLE` (not `DETACHED_PROCESS`) for the same reason as the perf
runner: console-less children die silently.

The launch line:

```
C:\Program Files\Java\jre1.8.0_151\bin\java.exe -ea -Xmx512m
    -cp C:\dev\runelite\*
    net.runelite.client.RuneLite
    --jav_config=http://10.10.10.1:8080/jav_config.ws
```

`-Xmx512m` respects the box's RAM; the `\*` classpath wildcard is
Java-8-native and safe because the vanilla injected-client was never deployed
(no shadowing of the instr gamepack). Early bring-up runs added
`--safe-mode`; it was removed once boot was stable so plugins run normally
(the title bar no longer says "safe mode").

Restart cycle: `kill_java.py` (`taskkill /F /IM java.exe`) via `/scripts/run`,
re-upload changed jars, run the launcher again, then pull
`C:\dev\runelite\runelite.log` with `/fs/get` and read the first
non-`runelite.net` stack trace.

Expected offline noise in the log — harmless, RuneLite falls back to
defaults: `UnknownHostException` for `static.runelite.net` /
`api.runelite.net` (runtime config, session, feed).

## 7. The GPU plugin: not possible on this hardware

The fork's GpuPlugin hard-requires **OpenGL 3.3**
(`runelite-client/.../gpu/GpuPlugin.java` throws
`"OpenGL 3.3 is required but not available"` when `glCapabilities.OpenGL33`
is false). The XP box reports (wmic `win32_videocontroller`):

```
Name=ATI Radeon X300/X550/X1050 Series
AdapterRAM=134217728          (128 MB)
DriverVersion=6.14.10.6925
```

That is an RV370 (2004, R300 architecture). Its ceiling is OpenGL 2.0 — no
driver ever shipped, or could ship, GL 3.3 for it; the shader model and
buffer features simply do not exist in the silicon. Independently, LWJGL 3.3.x
(which the plugin uses to create the context) supports Windows 7+ only.
Enabling the plugin fails its GL version check and RuneLite drops back to the
software renderer, which is what the box runs. XP itself is not the blocker —
NVIDIA shipped GL 4.x XP drivers for Fermi/Kepler-era cards — the 2004 GPU
is. Everything else (client, plugins that don't need GL, JS5, login) works
without it.

## Failure log (chronological)

| symptom | cause | fix |
|---|---|---|
| gradle can't find `:runelite-client:jar` | project path is `:client` | use `:client:jar` |
| javac "file not found: \c\Users\..." | Git Bash paths in `@sources.txt` | `cygpath -m` in build_xp.sh |
| instr `--release 8`: 15 errors | vanilla client's guarded Java 9+ refs | build at 21, downgrade bytecode |
| empty reply on every `/fs/put` | `Expect: 100-continue` ≥1 KB; missing dest dir | `-H Expect:`; `mkdir` first |
| `error_game_invalidhost` on the applet | gamepack host allowlist | `10.10.10.` branch in class510.java |
| `NoClassDefFoundError: …jvmdg.util.Utils` (shaded prefix) | shade pass omitted its own util class | drop shading; api + support jars on classpath |
| `ClassNotFoundException: …jvmdg.exc.MissingStubError` | api jar lacks `exc`/`util` packages | `jvmdg-runtime-support.jar` from the CLI jar |
| hang after "Prepared Sound Engine" (Client thread in `WindowsProcessHandle.readInfo`) | `ProcessHandle.current()` stub shells out, blocks forever on XP | remove ProcessHandle telemetry from class571.java |
| `/fs/put` of the gamepack: curl exit 56 mid-upload | running JVM holds the jar locked | `kill_java.py` before re-uploading |
| "error connecting to server" at login, title screen fine | torirsserver died; forwarder upstream refused | restart it from the repo root (see §4) |
| server aborts at login: `Assertion failed: free_i >= 0 && "ToriRSServerItem var table full"` (torirs_server_container.c:73) | server binary predated commit `1348e222a`, which adds `ToriRSServer_ItemClearVars` to `BankInitPlayer` — a calloc'd bank slot's var keys read as "obj id 0", so all four entries look taken and the first `inv_setvar` a login script aims at a banked item aborts | rebuild: `.\make.ps1 -j OPT=1 torirsserver`. Old binaries also wrote junk `<slot> 0 = 0` rows into `[bank_var]` in saves; those are harmless under the fixed binary (one key-0 entry per slot, three still free) |
| first cold login: "Loading – please wait", then "Error connecting to server. Please try using a different world." | server accepted the login (`session=ok`, scene built) but the client was still filling its empty jagexcache over JS5; the cold fill outlasts the game-connection timeout and the server drops the half-loaded client | just Try Again — the cache is warm by then and the second login lands in-game |
