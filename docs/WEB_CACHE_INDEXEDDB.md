# The browser's cache: IndexedDB behind a dat2 facade

> Build it with `make -C src web-idb`, which also builds both servers. The
> plain `make -C src web` lane is unchanged and still talks to `io_server`; see
> [web_build.md](web_build.md). What each server does, and how they are run
> together, is [WEB_SERVERS.md](WEB_SERVERS.md).

The web client has always had a cache-shaped hole in it. `PlatformX_IO_LoadItem`
is the one place the desktop build touches a file, and a browser has no file, so
the wire lane moves that call across a socket to `io_server` and lets a native
process hold the cache. That works, and it means a page cannot run without a
server that has the cache on disk.

This lane closes the hole instead. The browser gets a cache of its own —
archive records in IndexedDB, presented to the rest of the client as an ordinary
`RSCache_Dat2Disk` — and fills it incrementally over JS5. Everything above the
storage layer is then the code the desktop build runs, including
`platform_x_io.c` itself.

```
  browser tab                                  your machine
  ┌────────────────────────────────────┐       ┌────────────────────┐
  │ torirs.wasm                        │       │ js5_server         │
  │   App / tasks / decoders           │       │   read-only cache  │
  │   platform_x_io.c  ── the desktop  │       └────────┬───────────┘
  │        │            backend        │                │
  │   RSCache_Dat2Disk                 │       ws:// (RFC 6455) or raw TCP
  │        │  store vtable             │                │
  │   dat2_web_store.c ──┐             │                │
  │        │             │             │  ◄─────────────┘
  │   js5.c ─────────────┼─────────────┼──── emscripten socket
  │                      │             │
  │ torirs_host.js       │             │       ┌────────────────────┐
  │   Module.torirsStore ┘             │       │ io_server          │
  │   IndexedDB:                       │◄──────┤ page, module,      │
  │     groups · files · boot          │       │ GET /boot/<path>   │
  └────────────────────────────────────┘       └────────────────────┘
```

`io_server`'s `/io` route is never called on this lane — the client does not
link the wire backend at all. It is still what should serve the page, because
it is the only server in the tree that answers the conditional requests the
boot files are revalidated with (see Staleness). Both processes are built by
`make -C src web-idb`; see [WEB_SERVERS.md](WEB_SERVERS.md).

## Why not a dat2 file in MEMFS

Emscripten has a filesystem, so the cheap answer would be to keep real
`main_file_cache.dat2` / `.idxN` files in MEMFS and change nothing. That was
rejected, for reasons that are all properties of the container rather than of
the code:

- **The sector chain solves a problem a browser does not have.** Its 520-byte
  sectors with their per-sector headers exist to pack variable-length archives
  into one file without a per-archive inode. IndexedDB is already a keyed
  store; laying a chain over it pays the header overhead for nothing.
- **Rewrites orphan sectors.** `RSCache_Dat2DiskWriteArchive` appends and
  re-points the index, exactly as the real client does, so a cache that
  re-downloads a group grows and never shrinks. In a wasm heap that never
  shrinks either, that compounds.
- **MEMFS is not storage.** It is forgotten when the tab closes, which is the
  entire thing this lane is for.

So the layout changes and the interface does not.

## The store vtable

`struct RSCache_Dat2Store` in [`3rd/rscache/src/dat2disk.h`](../3rd/rscache/src/dat2disk.h)
is where a disk's archives live. Every `RSCache_Dat2Disk` has one — the dat2
file backing is `RSCache_Dat2DiskFileStore`, installed by the `NewFromDirectory`
constructors, and it is a peer of any other implementation rather than a hidden
default.

Making it required rather than optional is deliberate. An "if there is a store,
else read the file" disk has two backings and two sets of bugs, and the file
path stays the one that is really exercised. One mandatory vtable means the
browser runs the same call sites the desktop does.

| callback | file backing | browser backing |
| --- | --- | --- |
| `get` | idx record → sector chain walk | `Map` lookup, copied into the wasm heap |
| `put` | append to dat2, re-point idx | `Map` insert + a batched IndexedDB write |
| `has_table` | does `main_file_cache.idxN` exist | does any record carry that table |
| `commit_table` | create the idxN sentinel, reopen the dat2 reader | not implemented — nothing to keep in step |
| `destroy` | not needed (state is the disk) | not needed (state is the page's) |

The unit is the exact idx-record payload: the JS5 container plus whatever local
version trailer the writer appended. That is the same blob
`RSCache_Dat2DiskArchiveNewLoadRaw` returns and `RSCache_Dat2DiskWriteArchiveTo`
accepts, so a store transports bytes the library already round-trips and is
never a second encoding to keep in step.

## Where the records live, and why

IndexedDB is asynchronous and this lane has **no ASYNCIFY** (asserted by
`make -C src lane-check PLATFORM=web`). A `store.get` that had to reach the
database could not answer the synchronous call it stands in for.

So the resident records live on the **JavaScript** side, hydrated in one cursor
pass before `main()` runs, and `get` is a map lookup. Two consequences, both
intended:

- The bytes are not in the wasm heap. Only the archive being decoded right now
  is copied in, and the caller frees it — so a cache larger than the wasm32
  4GB ceiling is not itself a reason the module dies.
- A record the hydrate did not load reads as **absent**, not as an error. That
  is the same answer an empty cache gives, so JS5 downloads it again and
  re-writes it. A partial hydrate costs bandwidth; it cannot produce a wrong
  archive.

Nothing is evicted mid-session. JS5 remembers which groups it validated, and a
group it believes is ready must still be readable when a task asks for it —
dropping one would turn a completed download into a failed cache read.

### Schema

Database `torirs-cache`, version 1.

| store | key | value |
| --- | --- | --- |
| `groups` | `"<cache>\|<table>\|<archive>"` | `{k, c: cache, t: table, a: archive, d: ArrayBuffer}`, index `by_cache` on `c` |
| `files` | the client's path | `{k, d: ArrayBuffer}` — the player's saved options |
| `boot` | the config path | `{k, d: ArrayBuffer, e: ETag}` — manifest and RevConfig INIs, with the validator to revalidate them |

`groups` is scoped by cache; `files` and `boot` are not, which is why the
hydrate happens in two steps (see The boot barrier).

`by_cache` is what lets the hydrate walk one generation instead of every cache
the browser has ever held. Records are scoped by cache key (the manifest's
`[cache:boot] dir=`), so switching manifest in the URL cannot mix an osrs239
archive into an osrs230 boot.

Writes are batched — 64 records or 250ms, whichever comes first — because a JS5
boot installs hundreds of them and a transaction each would cost more than the
download. A quota failure is reported once and does not fail the session: the
records stay resident, and what is lost is the warm start next time.

## The boot barrier

The reference tables must be installed before anything opens the cache:
`App_Init` decodes them itself and is not a tolerant reader (see
[JS5_INCREMENTAL_CACHE.md](JS5_INCREMENTAL_CACHE.md)). On the desktop that is a
spin loop. Here it cannot be — a WebSocket delivers nothing to a thread that is
spinning on it — so the loop is inverted and the page drives it.

```
runtime initialized
  └─ open IndexedDB
  └─ hydrate `files` + `boot`            not scoped to a cache
  └─ boot.load()                         fetch/revalidate the manifest + INIs
  └─ torirs_web_cache_key(manifest)      C reads it, names the cache
  └─ hydrate `groups` for that cache
  └─ torirs_web_cache_prime_begin(host, port)
  └─ setTimeout loop: torirs_web_cache_prime_step()   0 = keep going
  └─ Module.callMain(argv)               main() starts, cache underneath it
```

The order looks circular and is not: the cache records are keyed by cache name,
the name is in the manifest, and the manifest is a boot file that must be
revalidated first. Hence two hydrate steps rather than one. The wire lane runs
the same sequence and stops after `boot.load()`.

**This cannot be a `preRun` run-dependency**, which is the obvious shape and the
one that was tried first. `preRun` runs *before* `initRuntime`, so a dependency
taken there also holds `initRuntime` back and the native functions the prime is
made of cannot be called yet. With assertions on, emscripten says so —
`native function called before runtime initialization`; without them it is a
wasm trap at a nonsense address, several layers from the cause.

The seam that works is one step later: `Module.noInitialRun` tells the runtime
to finish initializing and then stop, and `Module.callMain` starts `main()` when
the barrier is done. The barrier releases `main()` on failure too, so a boot
with no metadata reports a cache it cannot read rather than leaving the page on
"loading…" — which is indistinguishable from a hang and has a different fix.

After `App_Init`, JS5 attaches and does **not** wait. The reference tables are
already in, so the attached client's second pass is a local CRC check, and a
group read that arrives first parks the ordinary way: `PlatformX_IO_Pending`
tells `TaskRunner_Step` not to resume a task whose slot is unfilled, and makes
no distinction between waiting on a download and waiting on this.

## Demand-only filling

`Js5Config::background_fill` defaults to true, which is what "incremental cache"
usually means: every group the reference tables list and the store does not have
is queued on the normal lane, and the cache converges on a complete mirror.

The web lane sets it **false**. A tab that quietly pulls a couple of hundred
megabytes is a bad citizen on someone's connection, it competes with the reads
the boot is actually blocked on, and the records have to be held resident. The
cache converges on the working set instead, which is the right target when the
storage is a browser's.

Measured on an osrs239 boot to a rendered world:

| | records | resident | metadata bytes |
| --- | --- | --- | --- |
| background fill on | 52,850 | 108 MB | 1,055,029 |
| demand-only, cold | 754 | 3.4 MB | 1,051,102 |
| demand-only, warm | 754 (all hydrated) | 3.4 MB | **208** |

The warm figure is the point of the whole design: 208 bytes of network traffic
to validate 23 reference tables against the master, and not one archive
re-fetched.

## Staleness

Three kinds of thing are cached in the browser, and each is kept current a
different way, because each has a different notion of "current".

### Cache archives — the protocol already does it

Nothing was added here; JS5's design is the staleness check.

- `255/255`, the master index, is **always** fetched from the server. Never
  read locally, on any boot.
- Every stored reference table is checked against the master's CRC and version
  for that archive. A mismatch re-downloads it.
- Every stored group is checked against its reference table entry's CRC before
  it is used. A mismatch re-downloads it.

So a cache repacked on the server is picked up on the next page load, at the
granularity of what actually changed. The 208-byte warm boot above *is* the
staleness check — 23 reference tables validated against a freshly fetched
master, and nothing else transferred.

The server side of the same question — what happens when the cache changes
while `js5_server` is running — is in [WEB_SERVERS.md](WEB_SERVERS.md).

### Boot configuration — conditional requests

The manifest and the RevConfig INIs it names are edited by hand between runs,
so a stored copy can never simply be trusted. They are kept in the `boot` store
with the `ETag` the server gave them, and each is revalidated on every load:

| server says | host does | log |
| --- | --- | --- |
| `304 Not Modified` | uses the stored copy, no body transferred | `manifest.ini (unchanged)` |
| `200` with a new ETag | takes it, stores it | `manifest.ini (changed)` |
| nothing, or `404` | uses the stored copy anyway | `manifest.ini (offline copy)` |

The third row is the reason this is a store and not just a conditional fetch: a
page whose config server has gone away still boots from what it fetched last
time, rather than failing on a file it has. It is also what makes the ordering
in the boot sequence circular-looking — the cache records are keyed by cache
name, the name is in the manifest, and the manifest is itself a boot file that
must be revalidated first. Hence two hydrate steps rather than one.

This applies to **both** web lanes: the wire build gets it too, since the boot
sequence is now shared.

### Client files — no server truth to be stale against

The player's saved options are device-local by definition. Nothing on the
server has an opinion about them, so there is nothing to revalidate; they are
read at boot and written when they change.

## Client files

`TORIRS_IOK_FILE_READ` / `FILE_WRITE` are the player's saved options. On this
lane they go to the `files` store rather than to MEMFS, because MEMFS forgets
them when the tab closes — which reproduces the "the music setting does not
save" defect [`rs_prefs.c`](../src/game/rs_prefs.c) exists to fix, one layer
lower down. The desktop path's write-then-rename is replaced rather than
emulated: a single keyed put is already atomic.

## Reaching js5_server from a browser

Emscripten implements BSD sockets as WebSockets, so the client's `connect()`
arrives at the server as an HTTP upgrade and every byte after it is inside a
frame. `js5_server` therefore sniffs the first byte and speaks either protocol
on one port — an upgrade opens with `'G'`, a JS5 stream with opcode 15. Nothing
in the client changed for this; `sockstream.c` is the same file on both hosts.

The handshake itself is shared with the mock game server through
[`net_transport_ws_handshake.h`](../src/platform/net_transport_ws_handshake.h),
a pure function over bytes so that a blocking reader (ToriRSServer) and a nonblocking
reactor (js5_server) can both use it.

One caveat worth knowing: emscripten requests the `binary` subprotocol, and a
browser fails the connection outright if the server does not confirm it. A
server that ignores the header looks, from the page, exactly like one that is
not listening.

## Running it

```sh
make -C src web-idb                    # module + both servers

./src/build_opt/js5_server --cache cache.osrs239 --revision 239 --port 43594 &
./src/build/io_server --root build-web --boot-root . --port 8099
```

Any static server can serve the page, but `io_server` is the one to use: it is
the only one in the tree that answers the conditional requests the boot files
are revalidated with (see Staleness above), so anything else re-downloads the
manifest on every load.

Then open the page with the manifest on the query string:

```
http://localhost:8099/index.html?arg=--manifest&arg=manifest_osrs239.ini&arg=--offline
```

Web-only knobs, beyond the ones in [web_build.md](web_build.md):

| | |
|---|---|
| `?js5_host=H` | where the JS5 server is (default: the page's own host) |
| `?js5_port=N` | its port (default 43594) |
| `?cache_reset=1` | drop this cache's records first — the only way to make a cold boot reproducible once a warm one has been measured |

The status line reports the store rather than the wire:

```
heap 256MB · cache cache.osrs239 754 records 3.4MB · hydrated 754 written 0
```

## Known limitation: 16-bit group ids

A JS5 request is four bytes — opcode, archive, and a **two-byte** group id — so
no group at or above 65536 can be addressed by this protocol. The client
enforces that when it validates a reference table and fails the boot with
`JS5_ERROR_REFERENCE` (11) rather than truncating the id, which would silently
serve a different archive.

This is not theoretical here. `cache.osrs239.summoning` holds model ids up to
124175 in table 7, so it **cannot be served over JS5** as built:

```
web js5: reference-table prime failed (error=11 state=6 status=0 port=43594)
cache: the JS5 server answered (1.0MB) but the client rejected its metadata —
       JS5 error 11 (reference table): a table almost certainly holds group ids
       past 65535, which a 4-byte JS5 request cannot address
```

It is a property of the cache and not of this lane: the desktop client and
`make -C src test-js5 JS5_TEST_CACHE=cache.osrs239.summoning` fail at the same
table with the same code. Serving such a cache needs either ids packed under
65536 or a protocol that can carry a wider one; the wire lane (`io_server`) has
no such limit and remains the way to run those caches in a browser.
