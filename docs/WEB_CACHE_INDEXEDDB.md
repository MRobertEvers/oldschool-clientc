# The browser's cache: IndexedDB behind a dat2 facade

> This is the web lane: `make -C src web` builds it, together with both
> servers. See [web_build.md](web_build.md) for the build and
> [WEB_SERVERS.md](WEB_SERVERS.md) for what each server does. An earlier "wire"
> lane, in which the browser held no cache and every read crossed a socket to
> `io_server`, has been removed; some history below still contrasts with it.

The web client has always had a cache-shaped hole in it. `PlatformX_IO_LoadItem`
is the one place the desktop build touches a file, and a browser has no file.
The wire lane moved that call across a socket to `io_server` and let a native
process hold the cache, which meant a page could not run without a server that
had the cache on disk.

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

`io_server`'s `/io` route is never called — nothing in the client sends to it.
It is still what should serve the page, because it is the only server in the
tree that answers the conditional requests the boot files are revalidated with
(see Staleness), and for a dat1 world it is the proxy the on-demand producer
reads through. Both processes are built by `make -C src web`; see
[WEB_SERVERS.md](WEB_SERVERS.md).

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

In IndexedDB, and nowhere else. Records are read from the database when an
item asks for them and returned; there is no JavaScript `Map` in front of it
and no hydrate pass before the first frame.

An earlier design had both, for one reason: the C side reached the store
through a synchronous facade and could not await a database request, so the
resident set was walked into the JS heap before `main()`. Everything that
touches the store is asynchronous now -- the executor awaits the host, the
host awaits the database -- so the reason is gone, and with it a whole-cache
cursor walk at boot and the entire resident cache held twice, in the database
and again on the heap, for the life of the tab. IndexedDB is itself an indexed
on-disk key/value store with its own page cache; a hand-rolled one in front of
it is a pessimisation until something profiled says otherwise, and nothing
has.

Two properties survive from that design and are still intended:

- The bytes are not in the wasm heap. Only the container being decoded right
  now is copied in, and the caller frees it -- so a cache larger than the
  wasm32 4 GB ceiling is not itself a reason the module dies.
- A record the database does not hold reads as **absent**, not as an error.
  That is the same answer an empty cache gives, so the producer fetches it
  and writes it. A lost record costs bandwidth; it cannot produce a wrong
  archive.

### Schema

Database `torirs-cache`, version 1.

| store | key | value |
| --- | --- | --- |
| `groups` | `"<cache>\|<table>\|<archive>"` | `{k, c: cache, t: table, a: archive, d: ArrayBuffer}`, index `by_cache` on `c` |
| `files` | the client's path | `{k, d: ArrayBuffer}` — the player's saved options |
| `boot` | the config path | `{k, d: ArrayBuffer, e: ETag}` — manifest and RevConfig INIs, with the validator to revalidate them |

`groups` is scoped by cache; `files` and `boot` are not. Records are scoped by
cache key (the manifest's `[cache:boot] dir=`), so switching manifest in the
URL cannot mix an osrs239 archive into an osrs230 boot, and `by_cache` is what
lets `?cache_reset=1` drop one generation instead of every cache the browser
has ever held.

## Boot order

The reference tables must be readable before anything opens the cache:
`App_Init` decodes them itself and is not a tolerant reader (see
[JS5_INCREMENTAL_CACHE.md](JS5_INCREMENTAL_CACHE.md)). On this lane they are
ordinary reads: the first task that asks for table 255/`<n>` parks like any
other, the executor fetches the container over JS5, and the task resumes when
it is filled. Nothing is primed ahead of `main()`.

```
runtime initialized
  └─ boot.load()               fetch or revalidate the manifest and the INIs
  │                            it names, into MEMFS -- and read the cache key
  │                            out of the manifest on the way
  └─ Module.callMain(argv)     main() starts; every cache read from here on is
                               an item on the queue, answered by the executor
```

`boot.load()` runs from `Module.onRuntimeInitialized`, one step after
`preRun`, with `Module.noInitialRun` holding `main()` back until it is done.
It cannot be a `preRun` run-dependency: `preRun` runs *before* `initRuntime`,
so a dependency taken there also holds `initRuntime` back and nothing native
can be called yet. With assertions on, emscripten says so --
`native function called before runtime initialization`; without them it is a
wasm trap at a nonsense address, several layers from the cause.

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
in the boot sequence look circular — the cache records are keyed by cache
name, the name is in the manifest, and the manifest is itself a boot file that
must be revalidated first. Hence the manifest is read before the cache key is.

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
make -C src web                        # module + both servers

TORIRSSERVER_CACHE=cache.osrs239 ./src/build_opt/torirsserver 43594 --rev osrs239 &   # game + JS5
./src/build/io_server --root build-web --boot-root . --port 8099
```

or, with one command, `./launch run osrs239-web`.

Any static server can serve the page, but `io_server` is the one to use: it is
the only one in the tree that answers the conditional requests the boot files
are revalidated with (see Staleness above), so anything else re-downloads the
manifest on every load.

Then open the page with the manifest on the query string:

```
http://localhost:8099/index.html?arg=--manifest&arg=manifests/manifest_osrs239.ini&arg=--offline
```

Web-only knobs, beyond the ones in [web_build.md](web_build.md):

| | |
|---|---|
| `?js5_host=H` | where the JS5 server is (default: the page's own host) |
| `?js5_port=N` | its port (default 43594, torirsserver's game port) |
| `?js5_url=U` | a complete socket URL instead, absolute or page-relative; defaults to `?ws=` when that names the game socket, since the server serves both on one port |
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
65536 or a protocol that can carry a wider one. The removed wire lane had no
such limit; with it gone, those caches cannot be run in a browser today.
