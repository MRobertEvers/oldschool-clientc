# JS5 incremental cache

The C client can boot against an **empty directory** and build its cache while it
runs. There is no pre-seeding step: point `--js5` at a JS5 server, hand the
client a directory that contains nothing, and it fetches the reference tables it
needs to start, then fills groups on demand and in the background.

This is a platform-side feature. The core `App`, task runner, cache providers and
cache-read interfaces are unchanged, and a cache hit is still a synchronous read.
With JS5 disabled, the original full-cache path is exactly what it was.

Verified end to end on macOS against `cache.osrs239`: a client given an empty
directory renders Lumbridge indistinguishably from one given the complete cache
(62 differing pixels out of 384,795, against 51 for two runs of the *same*
full-cache configuration), and the cache it builds is a container-exact mirror of
the source across all **116,749 groups**.

Layout:

| Path | What it is |
|---|---|
| [`src/js5/js5.c`](src/js5/js5.c) | Protocol + incremental-cache state machine. Depends only on callback tables. |
| [`src/js5/js5_rscache.c`](src/js5/js5_rscache.c) | Persistence adapter; the only client-side JS5 code that knows rscache. |
| [`src/js5/server/`](src/js5/server/) | Standalone read-only JS5 service. |
| [`src/platform/platform_x_io_js5_cache.c`](src/platform/platform_x_io_js5_cache.c) | Executor-facing bridge. |
| [`src/main.c`](src/main.c) | Boot ordering: pre-prime, `App_Init`, attach, pump. |
| [`tools/js5_cache_verify.py`](tools/js5_cache_verify.py) | Compares a built cache against its source, group by group. |

Protocol fidelity, the pinned RuneLite/GamePack provenance, and the
source-to-C mapping are in [`src/js5/README.md`](src/js5/README.md). Server
limits and deployment are in [`docs/JS5_SERVER.md`](docs/JS5_SERVER.md).

---

## TODO

Ordered roughly by how much each one would hurt.

- [ ] **JS5 is mandatory once enabled.** The metadata barrier always requests
      `255/255` from the server, so a *complete* local cache plus an unreachable
      server still refuses to boot. A client with everything it needs should be
      able to start offline. Needs a "cache is already complete, tolerate a dead
      server" path distinct from "cache is empty, the server is the only source".
- [ ] **There is no demand-only mode.** `platform_x_io_js5_cache.c` registers
      every addressable archive with `background=true`, so an idle client
      downloads the entire cache (~216 MB) whether or not it needs it. Fine for a
      LAN fixture, wrong for a thin client on a metered link. The urgent/on-miss
      path already works and would be the whole mechanism; what is missing is a
      switch that leaves the background lane empty.
- [ ] **Verify the `bzip_decompress` signature change on mingw, Emscripten and
      NXDK.** Only macOS/clang was built and run here. The change is
      source-compatible for every in-tree caller, but those toolchains were not
      exercised.
- [ ] **No automated test covers the corrupt-reference-table boot.** It is the
      bug that motivated the ordering fix and it is verified only by the manual
      procedure below. It belongs in `js5_test.c` as a fixture.
- [ ] **A built cache is ~5% larger than its source** (226 MB vs 216 MB, same
      116,749 groups). `RSCache_Dat2DiskWriteArchive` appends and re-points, so
      groups land in arrival order with orphaned slack. Harmless, but there is no
      compaction pass; writing to a fresh directory is the only way to compact.
- [ ] **The pre-prime opens a second connection at boot.** It is one connect and
      a local-CRC pass (208 bytes against a warm cache), but the two phases could
      share a client if `PlatformX_IO` could adopt an already-primed one.
- [ ] **A short (non-zero) gzip decompress still publishes an uninitialised
      tail.** Only a return of 0 is treated as failure, matching the existing
      contract. A strict `== uncompressed_length` check is probably right but was
      not made, because no cache in the tree was observed to need it and a
      speculative tightening could reject valid data.
- [ ] **XTEA-encrypted groups are preserved but untested.** JS5 stores container
      bytes verbatim and never decodes them, so encryption should be transparent;
      revision 239 maps are unencrypted, so nothing here exercised it.
- [ ] Pre-existing and unrelated, recorded so they are not re-diagnosed:
      `test_cs2` fails 3 threshold checks and `cachepack-fidelity` reports 3
      missing fonts. Both were confirmed byte-identical on a pristine worktree at
      `HEAD` — see the pristine-comparison procedure.

---

## Procedures

### Start the JS5 server

```bash
make -C src js5-server
src/build/js5_server --cache cache.osrs239 --revision 239 --bind 127.0.0.1 --port 43594
# prints: READY 127.0.0.1 43594 239
```

It never writes to the cache it serves.

### Boot the client from nothing

```bash
mkdir -p /tmp/sparse239                       # must exist; JS5 will not create it
src/torirs /tmp/sparse239 --manifest manifest_osrs239.ini \
  --offline --js5 --js5-host 127.0.0.1 --js5-port 43594 --js5-fallback-port 0
```

`--offline` suppresses a manifest-enabled producer unless `--js5` is also given
explicitly, which is what makes a cache-only test possible.

### Boot it headlessly and capture a frame

```bash
SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=400 TORIRS_EXIT_BMP=/tmp/boot.bmp \
  src/torirs /tmp/sparse239 --manifest manifest_osrs239.ini --offline \
  --js5 --js5-host 127.0.0.1 --js5-port 43594 --js5-fallback-port 0
sips -s format png /tmp/boot.bmp --out /tmp/boot.png    # macOS
```

`TORIRS_MAX_FRAMES` is required headlessly: the dummy video driver never
delivers a quit event, so without it the client runs until killed. A run that
"hangs" under `SDL_VIDEODRIVER=dummy` is usually this and not a JS5 stall.

### Verify a built cache against its source

```bash
python3 tools/js5_cache_verify.py cache.osrs239 /tmp/sparse239
```

Compares the JS5 *container* of every group, splitting off the local version
trailer using the container's own self-describing length. A partially filled
cache reports `missing` and still passes; add `--require-complete` to demand a
full mirror. Byte-comparing the `.dat2` files instead will always disagree and
mean nothing — see "Container vs. trailer".

### Corrupt a group on purpose, to exercise repair

Flip a byte inside a sector *payload*, past the 8-byte sector header, so the
chain still walks but the container no longer matches its CRC:

```python
import os
d, archive, group, SEC = "/tmp/sparse239", 255, 2, 520
idx = open(os.path.join(d, f"main_file_cache.idx{archive}"), "rb").read()
sector = int.from_bytes(idx[group*6+3:group*6+6], "big")
f = open(os.path.join(d, "main_file_cache.dat2"), "r+b")
pos = sector*SEC + 8 + 4
f.seek(pos); b = f.read(1); f.seek(pos); f.write(bytes([b[0] ^ 0xFF])); f.close()
```

Then boot with JS5 and re-run the verifier: it should come back `PASS`.
`archive=255` corrupts a *reference table*, which is the case that used to be
fatal.

### Measure whether two frames really differ

Any two runs differ slightly (animation phase), so a raw `cmp` is not evidence.
Compare a candidate against a control, then compare two controls against each
other, and read the first number against the second:

```bash
python3 - control.bmp candidate.bmp <<'EOF'
import sys, struct
a = open(sys.argv[1],'rb').read(); b = open(sys.argv[2],'rb').read()
off, w, h = 54, 765, 503
row = ((w*32+31)//32)*4
d = sum(1 for y in range(h) for x in range(w)
        if a[off+y*row+x*4:off+y*row+x*4+4] != b[off+y*row+x*4:off+y*row+x*4+4])
print(f"{d} pixels ({100.0*d/(w*h):.3f}%)")
EOF
```

### Run the suites

```bash
make -C src test-js5                              # 64 checks
make -C src test-js5-server JS5_TEST_PYTHON=python3   # 121 + 8 checks
make -C 3rd/rscache test                          # storage layer
```

`JS5_TEST_PYTHON=python3` is needed wherever `python` is not on `PATH`.

### Decide whether a failure is yours (pristine comparison)

Do not `git stash` in this repo. Use a worktree, and symlink the data the tests
need but git does not track:

```bash
git worktree add --detach /tmp/pristine HEAD
ln -s "$PWD/cache.osrs239"  /tmp/pristine/cache.osrs239
rm -rf /tmp/pristine/OSRS-Content     # git leaves an empty submodule dir behind
ln -s "$PWD/OSRS-Content" /tmp/pristine/OSRS-Content
cd /tmp/pristine/3rd/rscache && make test
```

Without those symlinks the suite *skips* the tests that need them and the
comparison silently means nothing — a skip and a pass look similar in the tail
of the log. Diff the result lines, not the exit codes. Some suites are
env-gated: `test_cs2` needs `CS2_REFERENCE` pointing at a RuneStar/cs2 checkout,
which it otherwise finds as a sibling directory that a worktree does not have.

---

## Major discoveries

### 1. A corrupt reference table bricked the cache; a corrupt group did not

**What.** Any group whose bzip2 container was damaged took the whole process
down — `bzip_fatal` called `exit(1)`. For an ordinary data group that never
mattered, because JS5 validates and refetches before anything decodes it. For a
**reference table** it was fatal and unrecoverable: `App_Init` decodes reference
tables itself, and it ran *before* JS5 attached, so the producer whose job is to
repair them had not been created yet. The only recovery was deleting the cache
by hand.

That is the wrong way round for an incremental cache, whose very first boot
writes 23 reference tables — the window where an interrupted download can leave
one torn.

**How it was found.** Not by reading the code. Corrupting 8 groups across
different archives and booting produced `bzip error: Unexpected input EOF` and
exit 1, and the client never reached `world_load`. Corrupting groups *one at a
time* separated the two cases cleanly:

| corrupted | exit | metadata reached | repaired |
|---|---|---|---|
| `255/2` (a reference table) | 1 | no | no |
| `2/10` (a data group) | 0 | yes | yes |
| `7/100` (a data group) | 0 | yes | yes |

The single-target sweep is what turned "corruption breaks it" into "reference
tables specifically break it, before the barrier".

**Fix.** Two parts, because there were two independent defects:

- [`main.c`](src/main.c): run the metadata barrier *before* `App_Init`, in
  `executor_prime_js5_reference_tables`. `PlatformXIOJs5Cache_New` takes a disk
  handle, not a `PlatformX_IO`, so the barrier does not need anything `App_Init`
  builds. The client attached afterwards re-validates the same tables locally —
  **208 bytes**, measured — so the ordering costs one connection and nothing else.
- [`bzip.c`](3rd/bzip/bzip.c): `bzip_decompress` reports and returns instead of
  exiting, and [`archive.c`](3rd/rscache/src/archive.c),
  [`filelist.c`](3rd/rscache/src/filelist.c) and
  [`compression.c`](3rd/rscache/src/compression.c) now check the result. The
  existing `continue` in `init_reference_tables` — which already intended to skip
  an unloadable table — became reachable for the first time.

**Result.** Corrupting `255/2` now boots (`exit 0`), loads the world, and leaves
the cache a byte-exact mirror again. The 8-group case repairs all 8.

### 2. `bzip_decompress` could write past the end of its output buffer

Found while making the above non-fatal. The loop called
`read_bunzip(bd, out + offset, IOBUF_SIZE)` until the stream ended, with **no
reference to the output buffer's size**. A container declaring a small
decompressed length but carrying a longer stream would walk straight off the
heap allocation. Nothing checked it because, before this work, corrupt input
could not return — it exited.

It matters more now than it did: an incremental cache decodes bytes that arrived
over a network and bytes left behind by an interrupted write.

`bzip_decompress` now takes `file_capacity` and bounds every write.

**One trap, caught by the test suite rather than by reading.** The first version
treated "output full" as success only if a probe read returned
`RETVAL_LAST_BLOCK`. `read_bunzip` signals the end *two* ways — `RETVAL_LAST_BLOCK`
once `writeCount` has gone negative, or a `gotcount` of `0` when this call is the
one that discovers the final block — so exact-fit streams were reported corrupt.
It surfaced as `cachepack-fidelity` losing 3 fonts, four layers away from the
change. Only a positive count means bytes were still coming.

### 3. The `sparse` test failure was a contract violation in the test

`make -C 3rd/rscache test` reported `sparse: 3/61 checks FAILED` before any of
this work, in `test_sparse.c` — the suite covering the exact storage primitives
JS5 writes through. That is an alarming place to have a failing test.

**How it was diagnosed.** A standalone repro of the failing sequence (create
sparse disk → install reference table → read it back) **passed**, which ruled out
the obvious "the reader's handle is stale" theory. What differed in the suite was
that it deletes and recreates the cache directory *between groups*. A second
repro of exactly that:

```
round 1 write = 0
  dat2 = 1040 bytes, idx7 = 258 bytes
after delete+recreate: dat2 = -1 bytes
round 2 write = 0                      <- reports success
  dat2 = -1 bytes                      <- the real file does not exist
RESULT: BUG — the write went to the deleted inode
```

`RSCache_Dat2DiskWriteArchive` caches its `.dat2`/`.idxN` handles keyed on the
directory *path*, so after a delete-and-recreate it happily writes into the
unlinked inode and returns success.

**It is not a library bug.** [`dat2disk.c:391`](3rd/rscache/src/dat2disk.c#L391)
states the contract outright: callers that delete or move a cache file mid-run
must call `RSCache_Dat2DiskWriteFlush` first. The test did not. Adding that call
to `cleanup_directory` took the suite from **3 failures to 65/65**.

Worth keeping in mind for JS5 itself: any future "clear the cache and refetch"
flow must drop those handles, or it will write into a ghost file and report
success while doing it.

### 4. What "incremental" actually costs

All measured on loopback against `cache.osrs239`, 400-frame headless runs:

| | cold (empty dir) | warm (complete) | control (full cache, no JS5) |
|---|---|---|---|
| boot to first frame | 9.8 s | 9.4 s | 8.8 s |
| metadata network bytes | 1,068,415 | 208 | — |
| dat2 growth on the run | +153 MB | 0 | — |

The **208 bytes** is the useful number: on a warm cache the whole metadata phase
is the master index plus local CRC validation of all 23 reference tables. Nothing
is refetched. The master index is requested every boot by design, which is also
why a dead server blocks a boot that would otherwise need nothing (see TODO).

A dead server fails in **0.02 s** with a diagnostic naming the error, state, and
port, after 4 connect attempts — it does not hang.

### 5. The built cache is exact, and the disk file is not

All **116,749 groups** match the source byte for byte at the container level —
`mismatched: 0, missing: 0, extra: 0`. But the files differ: 226 MB against
216 MB.

**Container vs. trailer.** Two reasons the bytes on disk cannot match, both
expected:

- Groups arrive in network order, so sector chains are laid out differently, and
  `RSCache_Dat2DiskWriteArchive` appends rather than packs.
- The client writes a **32-bit** version trailer after each container, following
  the revision-239 GamePack, where an older cache may carry a 16-bit one.

So the comparison that means something is container-level, which is why
`tools/js5_cache_verify.py` splits the trailer off using the container's own
5-byte header (compression type + compressed length, plus 4 more for the
decompressed length when compressed) rather than guessing a trailer width.

### 6. Two portability facts, each of which cost a build

- **`_POSIX_C_SOURCE` alone hides `mkdtemp` on Darwin.** It lives in the BSD
  extensions block of `unistd.h`, so the three JS5 test files failed to compile
  with "call to undeclared function". They now define `_DARWIN_C_SOURCE` as well.
  This code had never been built on macOS.
- **zsh does not word-split unquoted expansions.** `run $FLAGS` passes one
  argument containing spaces, and the client rejected the whole string as a
  single flag. Use an array: `FLAGS=(--js5 --js5-port 43594)` then `"${FLAGS[@]}"`.
  Worth recording because the symptom — an "invalid command-line argument" naming
  every flag at once — reads like a client bug.

---

## Decisions

**The metadata barrier runs before `App_Init`, rather than making reference-table
reads tolerant.** Making the read path treat a corrupt table as absent would also
have worked and touched less shared code. The barrier was chosen because it makes
true the invariant the design already claimed — "no game task is stepped before
the cache has server-authoritative reference metadata" — instead of adding a
second, weaker guarantee beside it. The cost was measured before committing to
it: 208 bytes and one connection.

**`bzip_decompress` returns a status instead of exiting.** The alternative was a
new `bzip_decompress_checked` alongside the old one. There is exactly one
in-tree caller, so a second entry point would have meant leaving a
process-killing function in place for no one, and any future caller would have
picked it by coin flip.

**Failure is reported, not silenced.** A corrupt container still prints
`bzip error: …`. Recovering from corruption without a trace would make a cache
that quietly refetches the same broken group forever look healthy.

**The `sparse` fix went in the test, not the library.** The handle-cache
behaviour is documented and deliberate, and the caching exists because
open/close per archive is expensive on Windows — six figures of archives per
pack. Changing the library to fstat-compare inode identity on every write would
have "fixed" a test by weakening a documented contract.

**A short gzip decompress was left alone.** Tightening it to
`== uncompressed_length` is probably correct, but no cache in the tree needs it,
and a speculative tightening of a shared decode path risks rejecting valid data
for a fault nothing has exhibited. It is in the TODO with the reasoning rather
than done quietly.

**The verifier compares containers, not files.** Byte-comparing `.dat2` files
would report a difference on every correct run (see discovery 5), which is worse
than no check at all: a test that always fails gets ignored.

---

## Verification status

| Check | Result |
|---|---|
| `make -C src test-js5` | 64 checks passed |
| `make -C src test-js5-server` | 121 + 8 checks passed |
| `make -C 3rd/rscache test` — `sparse` | 65/65 (was 3 failures) |
| `make -C 3rd/rscache test` — rest | unchanged vs pristine `HEAD` |
| Boot from empty directory | renders, `exit 0` |
| Frame vs full-cache control | 62/384,795 px, against 51 for control-vs-control |
| Built cache vs source | 116,749/116,749 groups exact |
| Warm reuse | 208 network bytes, 0 bytes of dat2 growth |
| Dead server | `exit 1` in 0.02 s with a diagnostic |
| Corrupt data group | repaired |
| Corrupt reference table | repaired (previously fatal) |
| 8 simultaneous corruptions | all repaired |

Not verified: mingw/Emscripten/NXDK builds, and encrypted (XTEA) groups.

**One caveat on the last build.** The final one-line change — the
`reference tables primed` progress print — compiles (`build/main.o`, clean) but
was not run, because `src/app.c` and `src/audio/torirs_audio.h` in the working
tree are mid-refactor and do not link (`TORIRS_AUDIO_CMD_SET_VOLUME` vs
`TORIRS_AUDIO_CMD_BUS_VOLUME`). That is unrelated in-progress work and was left
alone. Every behavioural result above was produced by a binary containing the
ordering and bzip fixes.

---

## Log

Chronological, including the parts that went wrong.

**Found the work already largely done.** The first surprise was that
`src/js5/` already held a ~3,300-line implementation across two commits, with a
server, tests, and docs — written on Windows, judging by the `C:\Users\…` paths
throughout `src/js5/README.md`. So the task was not "implement JS5" but "find out
whether this actually works here, and fix what doesn't". Reading first, building
second.

**It had never been compiled on this platform.** `make -C src test-js5` failed
immediately on `mkdtemp`. That set the tone: treat every claim in the existing
docs as unverified until a command reproduces it.

**The first real question was whether the feature works at all.** Rather than
audit the protocol against the deob, boot it: server on 43594, empty directory,
client. It rendered Lumbridge. That reordered everything — the feature was
essentially working, so the valuable work was finding the edges where it wasn't,
not re-deriving the parts that were.

The 5-minute "hang" on that first run was `SDL_VIDEODRIVER=dummy` never
delivering a quit event, not JS5. Worth the note in Procedures because it looks
exactly like a stalled download.

**Establishing a control was the single most useful step.** The first screenshot
showed a half-drawn gameframe, which looked like missing cache content. Running
the *full* cache with `--no-js5` produced the same picture, including the same
`hitmark` warning — so it was ordinary offline behaviour. Without that control I
would have spent the session chasing a JS5 bug that did not exist.

Same reasoning one level down: the two frames differed by 62 pixels, which is
suspicious until you run the control twice and get 51. A difference is only
evidence against a baseline of the noise.

**Verifying "the cache is correct" needed a tool, not an eyeball.** Comparing
`.dat2` files fails by construction. Writing `js5_cache_verify.py` to compare
containers — using the container's self-describing length to split the trailer —
turned a vague "seems to work" into 116,749/116,749. That number is what makes
the rest of the session's claims about repair meaningful, because it gives every
later check a known-good reference.

**Then hunt for the edges.** Dead server, warm reuse, corruption. Warm reuse at
208 bytes was a pleasant confirmation. Corruption found the real bug.

**The corruption bug took three attempts to locate correctly.**
First attempt: corrupt 8 groups, boot, observe exit 1 — knew something was wrong,
learned nothing about what. Second: corrupt one group at a time, which split data
groups (survivable, repaired) from reference tables (fatal). Third: read the boot
order and find `App_Init` at `main.c:2441` running before the barrier at 2443.
The one-at-a-time sweep was the step that mattered; the aggregate test had all
the information and none of the signal.

**Fixing it in the right place took two attempts.** Moving the barrier before
`App_Init` did not help on its own, because the crash was in `bzip_fatal`'s
`exit(1)` during the *disk open* that the barrier itself needs. Both defects had
to go. Worth remembering that "I fixed the ordering and it still fails" meant the
diagnosis was incomplete, not wrong.

**I introduced a regression and the suite caught it.** The bounded-output rewrite
of `bzip_decompress` rejected exact-fit streams, which showed up as
`cachepack-fidelity` losing 3 fonts. My first instinct was that it was
pre-existing, since the failure was nowhere near my change — and checking against
a pristine worktree seemed to confirm it. That check was wrong: the worktree had
*skipped* the test for want of a content tree, and a skip in a long log looks
like a pass. Only after symlinking `OSRS-Content` and `cache.osrs239` into the
worktree did the comparison mean anything, at which point 3 fonts were failing on
both sides — genuinely pre-existing — and my separate exact-fit bug was a real
regression I fixed on its own merits. The lesson is in Procedures: when comparing
against a baseline, confirm the baseline actually *ran* the test.

**The `sparse` failure was a detour worth taking.** A failing test in the storage
layer JS5 writes through is not something to document around. Two repros — one
that passed, one that failed — narrowed it to the delete-and-recreate sequence,
and the answer turned out to be a contract the library documents and the test
ignored. The temptation was to "fix" the library, since the symptom is silent
data loss; the comment at `dat2disk.c:391` and the performance reason behind the
handle cache both argue the other way. Fixed the test, and wrote the hazard into
the discoveries so a future JS5 cache-clearing feature does not walk into it.

**Left alone deliberately:** the user's in-flight audio refactor, which broke the
link near the end. Reverting or "fixing" someone's uncommitted work to get a
green build would have been the wrong trade — the affected code has nothing to do
with JS5, and every behavioural claim here was already produced by a binary
carrying the fixes. Recorded in Verification status instead.

**What I would do next**, in order: make JS5 optional-once-complete so a finished
cache boots offline (the most user-visible gap), then add a demand-only mode so
the background lane is a choice rather than the only behaviour, then push the
corrupt-reference-table case into `js5_test.c` so it stops depending on a manual
procedure.
