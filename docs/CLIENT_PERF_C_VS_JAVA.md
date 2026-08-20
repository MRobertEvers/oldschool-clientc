# C client vs Java client — performance investigation

> Started 2026-08-04. **Status: in progress.** This is the working log for a
> measurement effort, not a landed result. Every number here has to name the
> build, the scenario and the machine state it came from, because the two
> clients are only comparable when those match. Where a number is not yet
> measured the row says so rather than carrying an estimate.

**The goal.** The C client (`torirs`, this repo) must be *faster than the Java
client* — the real OldSchool client, revision 239, running under RuneLite. Not
"fast enough": faster. The two render the same scene from the same cache
against the same server, so the comparison is meaningful at the frame level and
at the stage level.

**The scope of this effort is measurement.** No optimisation is implemented
here. The output is a ranked list of places where the C client loses to Java,
each backed by a measurement that names its build and scenario. Two areas get
the depth:

1. **UITree interaction and rendering** — the C client's interface tree
   (`src/ui/`) against the Java client's widget tree.
2. **The software 3D rasterizer** — `src/torirs/` (Soft3D/ToriDraw) against the
   Java client's scanline rasterizer.

**Both optimisation levels are in scope.** `-O0` is what the C client's own
harness gates on (`docs/PERF_HARNESS.md`: p95 < 20 ms at `-O0` with
`TORIDRAW_OPT=1`), and it is what a developer runs. `-O3` (`make OPT=1`) is what
ships. The Java client is JIT-compiled and has no equivalent knob, so it is
measured warm and compared against both.

---

## 1. Why the Java client has to be instrumented at all

The Java client is obfuscated. There is no source, no symbols, and no profiler
hook that maps onto the C client's stage taxonomy. A sampling profiler
(async-profiler, JFR) gives leaf costs against obfuscated names, which answers
"what is hot" but not "how long did *the widget draw pass* take" — and the
stage-level answer is the one the comparison needs, because the C harness is
built on exactly that.

So the Java client gets a source-level harness with the same shape as
`src/perf/torirs_perf.h`: named stages, named counters, percentiles, and a CSV
in the same schema so `tools/perf/compare.py` can read both.

That requires a deob that **recompiles**, which is where this started.

### 1.1 The deob and its two trees

`Deobfuscator/src_osrs239_rl1_12_33/` is the readable deobfuscation of
RuneLite's `injected-client-1.12.33.jar` (revision 239). 723 classes, 197k
lines, obfuscated names (`class308`, `method4452`) but real control flow.

It does not compile — 500 javac errors. Two of the Deobfuscator's Vineflower
settings account for most of that, and **neither of them is about readability**:

| flag | what it drops | why it matters |
| --- | --- | --- |
| `-rsy=1` | synthetic fields | `this$0`, `val$*` and `$assertionsDisabled` in the flattened inner classes. 147 errors. |
| `-rbr=1` | bridge methods | **RuneLite's entire injected API surface.** `getModel`, `getChildren`, `createMenuEntry`, … — 215 methods over 40 classes. |

The bridge-method one is the dangerous half, and it fails silently: the client
still *builds* without those methods, still *starts*, and then any plugin that
touches the API dies with an `AbstractMethodError` a long way from the cause.
They are bridges because RuneLite's injector adds a forwarding method
(`getModel()` → `method4505()`) rather than renaming the original, so they carry
`ACC_BRIDGE | ACC_SYNTHETIC` and every decompiler hides them by default.

Re-decompiling the same jar with `-rsy=0 -rbr=0 -din=1 -dgs=1` gives
`Deobfuscator/instr/src/` — 199 errors, all type-erasure damage.

### 1.2 Decompiler settings, measured

The question "is there a decompiler setting that avoids this" was worth asking
and the answer is: two of them, and then it stops.

| configuration | javac errors |
| --- | ---: |
| Vineflower, as the deob profile ships (`-rsy=1 -rbr=1`) | 507 |
| Vineflower `-rsy=0 -rbr=0 -din=1 -dgs=1` | **199** |
| … the above, plus every dependency jar on `-e=` and `-iib=1` | 199 (no change) |
| CFR 0.152 `--recovertypeclash --recovertypehints --hidebridgemethods false` | 3509 |

The floor is 199 and it is not a decompiler limitation: **neither jar carries
generic `Signature` attributes.** Jagex compiles without them and RuneLite's
injector does not add them, so the generic types were erased before any
decompiler saw the bytecode. Vineflower recovers the element type from each
`checkcast` and writes source that is *more* informative than the bytecode and
that javac therefore rejects (`for (class100 v : rawCollection)`). Recovering it
means adding casts. No setting can do that.

The remaining 199 are all typing repairs — casts, corrected local types,
covariant returns, qualified names. None of them may change behaviour, and the
audit that enforces that is described in §1.3.

### 1.3 The equivalence gate

A recompiled client that is subtly not the same client measures nothing. Three
checks, in order of how much they catch:

1. **Class and method surface** vs the original jar — every class present,
   every method signature present. Catches the dropped-bridge class of failure
   at build time instead of at plugin-load time.
2. **Semantic audit of every source edit** — the repairs are typing-only by
   construction, and each one is reviewed against the bytecode
   (`javap -c -p`) before it counts as done.
3. **It runs** — RuneLite reaches the login screen, logs into the ToriRSServer
   server at revision 239, and renders a scene.

---

## 2. What gets instrumented on the Java side

Mirroring `src/perf/torirs_perf.h` so the CSVs are directly comparable
(`kind,name,mean_ns,p50_ns,p95_ns,max_ns,total,per_frame`).

| area | what it answers |
| --- | --- |
| frame stages | where the frame goes, at the same granularity the C harness uses |
| world render | scene traversal, per-tile draw, model projection, scanline fill |
| 2D UI | widget tree walk, per-widget draw, clip changes, sprite blits |
| rasterizer | triangle counts by kind (flat / gouraud / textured), pixels touched |
| data flow | cache reads, packet decode, CS2 script execution, widget tree size |

Plus an autonomy harness — screenshots, input injection, and a control channel
— so scenarios can be driven and compared without a human at the keyboard.

---

## 3. Trial log

Newest last. Every entry states what was run, what came back, and what it
changed.

### T1 — 2026-08-04 — can the deob be recompiled at all?

Compiled `src_osrs239_rl1_12_33/` with javac 21 against
`runelite-api-1.12.33-runtime.jar`.

- 723 files, **32 errors**, all of them Vineflower `???` markers in 3 classes.
  That looked like a 3-file fix.
- It was not. `-Xmaxerrs 10000` after removing those 3 files: **500 errors**.
  The 32 were *parse* errors, and javac never reached attribution — a parse
  failure hides every semantic error behind it. Worth remembering: an error
  count is only a floor until the file parses.

### T2 — 2026-08-04 — the readable tree is missing methods

Compared the bytecode's method set against the source, per class: **215 methods
absent from the source across 40 classes**, concentrated in `client` (62),
`class308` (44), `class136` (13). Every one an accessor: `getModel`,
`getChildren`, `createMenuEntry`, `getRenderable2`, …

Cause: they are `ACC_BRIDGE | ACC_SYNTHETIC` forwarding methods added by
RuneLite's injector, and Vineflower hides bridges by default. Not a
Deobfuscator bug and not visible in the source at all — the source simply
doesn't mention them.

### T3 — 2026-08-04 — decompiler settings

See §1.2 for the table. `-rsy=0` (keep synthetic) and `-rbr=0` (keep bridges)
take 507 → 199. Full classpath and `-iib=1` change nothing. CFR is 18× worse.
Vineflower with those two flags is the configuration of record; it is written
into `instr/tools/decompile.sh` with the reasoning next to it.

### T4 — 2026-08-04 — the 199 typing repairs

Nine agents on disjoint file groups, then nine more auditing the diffs against
`javap -c`. Families and counts:

| family | n | repair |
| --- | ---: | --- |
| erased generics | 74 | cast the iterable / the value |
| int↔boolean stack slots | 34 | read every use, then retype or convert literals |
| stripped `throws` | ~30 | restore the clause (compile-time only) |
| covariant bridge returns | 11 | declare the real return type |
| ambiguous `java.lang` names | 7 | qualify |
| blank finals | 2 | drop `final` |

One audit finding, corrected: a cross-class assertion-flag read had been turned
into a per-call `desiredAssertionStatus()`. Value-identical, but the original
cached it in a static and the replacement did not, so the field was restored
instead.

Three things bytecode permits that Java source cannot express, all handled
without paraphrasing the bytecode:

- **Eight bouncycastle stubs** ship in RuneLite's own jar with
  `throw new Exception()` constructors and no `Exceptions` attribute. Copied
  verbatim into the output (`instr/prebuilt/`) rather than rewritten.
- **`class444`** widens a `throws` over a stub base. Its catch is unreachable
  (a `DataOutputStream` over a `ByteArrayOutputStream` cannot throw) and the
  code says so.
- **`rl4.method10273`** rethrows a checked exception it does not declare. A
  sneaky throw compiles to the same bare `athrow`; declaring the clause instead
  pushed `throws InterruptedException` five frames up and out through an
  override that cannot widen it.

### T5 — 2026-08-04 — the API-surface gate

`instr/tools/verify_api.py` compares class/method/field sets against the
original jar. **723/723 classes, every member present.** Two benign differences
it knows about and names rather than ignoring:

- javac regenerates the assertion flag as `$assertionsDisabled` where the deob
  renamed it `fieldNNNN` (12 classes).
- `class439`'s static initializer is `static {}; 0: return` — an empty
  `<clinit>` javac does not generate for a class with no static state.

### T6 — 2026-08-04 — the probes, and one that was pointing at the wrong thing

14 probe sites placed from a declarative spec (`instr/tools/probe.py`), chosen
from an independently re-derived map of this deob. **The class map in the
Deobfuscator README does not apply to this tree** — it is from a different deob
run; it calls `class561` the 2D rasterizer, and here `class561` is a geometry
class.

The map was built by five agents and then *confirmed* by five more, and the
confirmation earned its keep: `client.method1589` sits on the tick path,
`synchronized` on a socket-ish object, and looks exactly like the inbound
packet pump. It is the **JS5 cache** pump — `Statics.field4385` is `class551`,
whose callee emits `"js5io"` and `"js5crc"`. The real game-packet drain is
`Statics.method1844`. The `S_NET` probe had already been placed on the wrong
one; a stage named `net` that measured cache I/O would have been worse than no
stage at all.

### T7 — 2026-08-04 — it compiled, it verified, and it hung

First launch: RuneLite loaded the rebuilt client (so the jar *was* being used)
and then sat at 100% CPU with no output for six minutes. No exception, no log
line. `jstack` put it in `class236.<init>` under `client.<clinit>`:

```java
byte var4 = 1;
while (var4 + var4 < var1) { var4 += var4; }
```

`var4 += var4` is a compound assignment, so Java re-narrows to `byte` on every
step: 64 → 128 wraps to −128 → wraps to 0, and `0 + 0 < var1` is true forever.
The bytecode is `iconst_1; istore 4` over plain `iadd` — **a plain int local.**
The JVM has no byte locals; Vineflower had narrowed it to the smallest type the
initial constant fits. `class236` is the NodeCache, so every cache in the client
is built by that constructor and static init never finishes.

This is the finding that matters most about the method: **it compiled cleanly
and passed the API-surface gate.** Neither gate can see it. Running it is the
only check that can.

It also generalises, so it got a scanner rather than a note
(`instr/tools/scan_narrowed_locals.py`) — every `byte`/`short` local grown from
its own value, scoped per method body, then decided against the bytecode: if
the enclosing method contains no `i2b`/`i2s` at all, nothing in it narrows and
the local is an int.

| | |
| --- | ---: |
| candidates (name-matched, no scoping) | 1645 — useless, matches across methods |
| candidates (scoped to the declaring method) | 26 |
| of those, methods with no narrowing instruction → **must be int** | **20** |
| methods that genuinely narrow → left alone | 6 |

20 latent hangs/corruptions, one of which was already costing a boot.

### T8 — 2026-08-04 — the deobfuscation is not behaviour-preserving

Three separate debugging sessions in this effort had one root cause, and it is
worth stating plainly: **the deob jar is not equivalent to the jar it was made
from, and nothing said so.** Every failure surfaces far from its cause and
reads as a bug in the rebuild.

`Deobfuscator/instr/tools/deob_audit.py` measures it. Input jar vs deob jar:

| feature | input | deob |
| --- | ---: | ---: |
| string concat (`makeConcatWithConstants`) | 51 | **0** |
| other `invokedynamic` (lambdas) | 12 | **0** |
| `@javax.inject.Inject` | 5 | **0** |
| `@Nullable` / `@Nonnull` / `@Deprecated` | 17 | **0** |

Two transformers in `deob.toml` do this:

- **`DeleteInvokeDynamic`** replaces every `invokedynamic` with `aconst_null`
  and leaves its operands computed and popped. Java 9+ compiles `a + b` on
  strings to `StringConcatFactory.makeConcatWithConstants`, so all 51 concats
  and all 12 lambdas become nulls. The decompiler renders that faithfully,
  javac accepts it, and the first one to fire is on the `client()` constructor
  path — the client threw NPE before anything ran.
- **`AnnotationRemover`** takes `@Inject` along with everything else, so
  Guice's `injectMembers(client)` wires nothing and `getCallbacks()` returns
  null. The client then dies inside `init()` naming `Callbacks`, which looks
  like a RuneLite bug.

The fix is `Deobfuscator/instr/deob.toml` — the same profile with both
transformers removed and each removal carrying its reason. The stock profile
stays correct for a jar that is only ever *read*; the two goals are different
artifacts and the tree now keeps both.

A follow-on lesson: restoring `@Inject` alone was not enough. Guice needs the
**binding** annotation too, and without `@Named("runeLiteDir")` it fails with
`Could not find a suitable constructor in java.io.File`. Half an annotation set
moves the failure rather than fixing it.

### T9 — 2026-08-04 — the probe inserter commented out its own probes

`probe.py` emitted `// JPROF-BEGIN JProf.frameBegin();` — marker and statement
on one line, so the statement was inside a line comment. Every stage read
**0.00 ms**, which is exactly what a fast stage reads.

It was caught only because `frameEnd()` sits in the `finally`, outside the
comment: the frame *counter* advanced (55 frames) while every stage and every
counter stayed at zero. That asymmetry is the whole signal.

`probe.py`'s own docstring warns that a stage reading 0.00 ms is
indistinguishable from a fast one, and it then did precisely that. Markers are
block comments now and `--unprobe` exists so placement can be redone.

### T10 — 2026-08-04 — the Java client runs, instrumented

| check | result |
| --- | --- |
| compiles | 0 errors |
| API surface vs original jar | complete, 723/723 classes |
| loads under RuneLite | yes — `Client initialization took 1571ms` |
| `JProf` reports real per-frame times | yes — frame mean 49 µs, p50 13.8 µs, 55 frames |
| `JCtl` control channel | yes — `canvas=765x503 fb=765x503` |
| screenshot from the client's own framebuffer | yes, 765×503 PNG |

A watchdog (`tools/perf/watchdog.sh`, 30 s default) now guards every run. It
earned its place immediately: the client had hung twice, once spinning at 100%
CPU and once as a dead JVM held open by AWT's non-daemon threads after RuneLite
had already failed. Both look identical from outside — live process, no output.
The watchdog distinguishes them, captures a thread dump plus the busiest
threads, and exits early when the log already shows a startup failure rather
than burning the deadline on a corpse.

### T11 — 2026-08-04 — not the JS5 server: the rebuilt client's own bzip2

The instrumented client died in the cache path with a reference-table protocol
error, which looked server-side. It was not, and proving that took the right
measurement rather than more reading:

1. Probed the mock server for all 25 reference tables and decompressed each.
   **Every one decodes as protocol 7.** Archives 16 and 23 are genuine holes —
   `idx255` records are all-zero — which matches the live cache.
2. Probed `oldschool1.runescape.com` at revisions 239–242 for the master index.
   It answers with the **same shape** the mock server sends: an uncompressed
   205-byte container, 200-byte body, 25 × (p4 crc, p4 version). So the mock
   JS5 master index is correct.
3. Added a diagnostic at the client's own throw site (it is our source now):
   `archive=17, raw=791B, decompressed=6372B, protocol=20, first24=1400...`
   Decompressing the same 791 bytes independently gave `07 6a5f6b14 ...` —
   protocol 7. Same input, same output length, **different bytes**.

The client's bzip2 decoder was corrupt. `class605.method13053` had
`byte var96 = 1; … var96 *= 2;` — the RUNA/RUNB run-length multiplier, doubling
per bit, wrapping at 128. Bytecode: `iload 96; iconst_2; imul; istore 96`, no
`i2b`, a plain int slot. Every archive decompressed to the declared length and
the wrong content.

**The mock JS5 server was right the whole time.** The lesson is the one this
log keeps re-learning: when a rebuilt client disagrees with a server, suspect
the rebuild first, and measure both sides rather than reading either.

### T12 — 2026-08-05 — logged in and rendering

Eight more defects between the bzip2 fix and a rendered scene, every one from
the deobfuscation and every one invisible to the compile and API gates. Full
catalogue in `Deobfuscator/instr/DEOB_DEFECTS.md`; the procedure to reproduce
is `Deobfuscator/instr/RUNNING.md`.

The client now logs in through the control channel and renders the world:
player, terrain, lava, orbs, inventory, minimap, "Welcome to the ToriRSServer world".

Two that generalise beyond this client:

- **`for (byte i = 0; i < 256; i += 8)`** — four ISAAC counters, wrapping to
  −128 and indexing a 256-entry array. A `for` counter is never legitimately
  `byte` in Java source, so all 39 in the tree were widened wholesale.
- **The narrowed-local scanner was wrong twice before it was right**, and each
  wrong version looked fine. "Does the enclosing method contain `i2b`?" proves
  *some* local narrows, not *this* one — that version left the bzip2 bug in
  place. The exact test is per slot.

### T13 — 2026-08-04 — (superseded) blocked: the mock JS5 server, not the client

The instrumented client reaches the game loop and then dies in the cache path:

```
java.lang.RuntimeException
  at class535.method11932(:499)   <- archive format version not in 5..7
  at class535.method11925(:268)
  at class551.method12249(:341)   <- the JS5 client service loop
  at client.method1589(:5969)
  at client.method1743 / class510.run
```

`class535.method11932` parses a JS5 **reference table** and rejects it because
the decompressed format byte is outside 5–7. Seeding `~/jagexcache/torirs239`
from `cache.osrs239` does not avoid it — the client still fetches reference
tables over JS5.

This is server-side (`src/torirsserver/mock_js5.c` / `ToriRSServer`), on the
`rsprot-osrs239` branch with uncommitted changes, i.e. work in flight. It is
not a deob or instrumentation problem: everything above it works.

**Consequence for this study:** the Java client cannot yet be driven to a
logged-in scene, so the stage-level comparison below has C numbers and no Java
numbers. The C side is measured and recorded; the Java side is one server fix
away.

---

## 4. C client baselines (measured)

Build: `EMBED_SERVER=1`, `manifest_osrs230_embed.ini`, `--uncapped --soft3d`,
1200 frames, headless (`SDL_VIDEODRIVER=dummy`), scenario `idle`.
`-O0` also carries `TORIDRAW_OPT=1` (Soft3D at `-O2`), which is the tree's
standard developer build; `-O3` is `OPT=1`, the whole tree.

CSVs: `tools/perf/results/cvj/torirs-O{0,3}-idle.csv`.

| stage | −O0 p50 | −O3 p50 | −O0 p95 | −O3 p95 | O0/O3 (p50) |
| --- | ---: | ---: | ---: | ---: | ---: |
| **frame** | 1533 µs | **706 µs** | 2781 µs | 1940 µs | 2.17× |
| render | 672 | 411 | 1665 | 1489 | 1.63× |
| paint | 319 | 85 | 545 | 104 | 3.75× |
| present | 147 | 135 | 253 | 143 | 1.09× |
| emit | 107 | 37 | 215 | 104 | 2.89× |
| interact | 20 | 5 | 40 | 10 | 4.00× |
| layout | 0 | 0 | 191 | 55 | — |
| eff fps (1/mean) | 72.6 | 108.1 | | | |

Two things to read out of this before any comparison:

- **`render` gains least from `-O3` (1.63×) because it is already optimised at
  `-O0`.** `TORIDRAW_OPT=1` builds Soft3D at `-O2` in the `-O0` client, so the
  rasterizer is the one stage that is *not* being measured at `-O0`. `paint`
  (3.75×) and `interact` (4.00×) are the honest `-O0`→`-O3` ratios.
- **`render` dominates both builds**: 44% of frame p50 at `-O0`, 58% at `-O3`.
  Whatever the Java comparison eventually says, the rasterizer is where the C
  client's frame time lives, and it is already the most-optimised part.

## 4b. A blocker for the rendering comparison, found on the way

**The C client draws the 3-D scene 2.68× magnified.** Its world projection
scale is a compile-time constant (`src/app.c:2853`, `fov_rpi2048 = 512`, set
once at world creation); the official recomputes it every layout from the
viewport height (`class159.method5357`: `scale = viewportHeight * zoom / 334`).
Measured side by side, same server/cache/canvas/account: official scale **191**
(and `(int)(503*127/334) = 191` exactly), C **512**.

This was found while diagnosing the Inferno "orange wedge", which turns out not
to be a spurious object at all — it is the Zuk alcove floor, the same loc models
in the same colour in both clients, 2.7× too large. Full write-up, evidence and
the three proposed edits: [`ORANGE_WEDGE.md`](ORANGE_WEDGE.md).

**Why it belongs in this log:** pixel count scales with the square of the
projection scale, so until this is fixed the two clients are not rasterizing
comparable scenes and **no render-stage number is meaningful**. Fix it before
trusting a `render` comparison.

---

### Java stage data (Inferno, 2667 frames, harness corrected)

Measured after the frame-boundary fix (see below). Logged in, Inferno arena
after `::zuk`, unlocked fps, 52 s.

| stage | mean | p50 | p95 |
| --- | ---: | ---: | ---: |
| **frame** | 5252 µs | 3534 µs | 8588 µs |
| uidraw | 3394 | 0 | 7811 |
| render | 3215 | 0 | 7440 |
| logic | 564 | 136 | 623 |
| paint | 214 | 0 | 485 |
| cs2 | 167 | 0 | 173 |
| net | 104 | 0 | 7 |
| present | 91 | 11 | 43 |
| cache | 91 | 0 | 0 |
| layout | 12 | 0 | 27 |
| interact | — | — | — |

`p50 = 0` on the draw-side stages is not an error: at unlocked fps the pacer
sets `shouldDraw = false` on most loop iterations, so more than half of all
frames are tick-only and draw nothing. Read the means for those.

`interact` has no probe yet, so it is blank rather than zero — a stage with no
probe and a stage that is fast must not look the same.

**An earlier version of this table was wrong and is worth recording.** It read
`logic`/`cs2`/`net` as 0.00 ms and `uidraw` p50 465 µs. The frame probe sat on
the *draw* (`client.method1926`) and zeroed the per-frame accumulator there,
after the tick had already run. The tell was that `uidraw`'s **mean exceeded
`frame`'s** — impossible for nested scopes. The boundary now sits in
`class510.run`, wrapping tick + draw + present and excluding the pacing sleep.

### Why there is still no side-by-side table

The two halves above are **not comparable**, for three independent reasons, and
combining them into one table would be the most misleading thing this document
could do:

| | C (§4) | Java (above) |
| --- | --- | --- |
| scene | Lumbridge, idle | Inferno arena |
| projection scale | 512 | 191 — the C client renders 2.68× magnified |
| pacing | `--uncapped`, every frame draws | unlocked fps, most frames skip the draw |

Pixel count goes as the square of the projection scale, so the render and paint
stages are measuring different amounts of work by a factor of ~7 before any
code is compared. The scale fix (`ORANGE_WEDGE.md` §4) is a prerequisite for
the comparison, not a side quest.

**What a valid comparison needs**, in order:

1. the projection-scale fix applied and verified,
2. both clients on the same scene, same camera, same canvas,
3. both drawing every frame (C `--uncapped`; Java with the fps cap on so the
   pacer stops skipping draws, or the draw-side stages compared per *drawn*
   frame rather than per loop iteration),
4. and the C client's `emit`/`build` stages mapped onto the Java taxonomy —
   they have no direct counterpart and currently have nowhere to land.

## 5. What remains

1. **Unblock the Java client** — the JS5 reference-table version above. Until
   then there is no Java stage data to compare against §4.
2. Drive both clients through the same scenario via their control channels
   (`JCtl` / `TORIRS_SIM_*`) so the comparison is like-for-like.
3. Then the two focus areas: UITree interaction+rendering, and the software
   rasterizer.

## 6. Repos and branches

| repo | branch | contains |
| --- | --- | --- |
| `3draster` | `perf-c-vs-java-239` | this doc, `tools/perf/run_cvj.sh`, `run_java_client.sh`, `watchdog.sh`, C baselines |
| `Deobfuscator` | `perf-instrumentation` | `instr/` — the compilable tree, JProf/JCtl, the tooling, and `instr/README.md` |

