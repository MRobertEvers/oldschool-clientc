# The servers a browser run needs

> Build them together: `make -C src servers`. Every web target
> (`web`, `web-debug`, `web-idb`, `web-idb-debug`) depends on it, so a build
> that produces the module also produces the processes that feed it.

A desktop client opens a cache directory and a socket. A browser tab can do
neither, so both jobs move into servers, and which servers depends on where the
client's cache lives.

```
                    ┌──────────────── the browser tab ────────────────┐
                    │                                                 │
                    │  index.html ── torirs_host.js ── torirs.wasm    │
                    │                     │                │          │
                    └─────────────────────┼────────────────┼──────────┘
                                          │                │
                    GET /                 │                │  emscripten
                    GET /boot/<path>      │                │  socket
                    POST /io  (wire lane) │                │  (WebSocket)
                                          ▼                ▼
                            ┌──────────────────┐   ┌──────────────────┐
                            │    io_server     │   │    js5_server    │
                            │  HTTP, native    │   │  JS5, native     │
                            └────────┬─────────┘   └────────┬─────────┘
                                     │                      │
                                     └──── the cache on disk ┘
```

Two processes, deliberately. They have different jobs, different lifetimes and
very different exposure — `js5_server` hands out every byte of a cache to
anything that connects, and `io_server` serves a source tree — so they are
separate executables with separate bind addresses. What is unified is the
*build*: one target produces both, because needing two and being told about one
is how a web build ends up half-runnable.

## Which servers each lane needs

| lane | `io_server` | `js5_server` | why |
| --- | --- | --- | --- |
| `make -C src web` (wire) | **required** | no | there is no cache in the browser; every read is a `POST /io` |
| `make -C src web-idb` | for the page and boot files | **required** | the browser holds its own cache and fills it over JS5 |

On the IndexedDB lane `io_server`'s `/io` route is never called — the client
does not link the wire backend at all. It is still the right thing to serve the
page from, because it also serves `/boot/`, and `/boot/` is where the staleness
checking lives (below).

---

## io_server

```sh
./src/build/io_server --manifest manifest_osrs239.ini    # http://localhost:8088/
./src/build/io_server --root build-web --boot-root . --port 8099
```

| route | method | what it does |
| --- | --- | --- |
| `/` and everything else | GET | static files under `--root` (default `build-web`) |
| `/boot/<path>` | GET | a file the client opens by name, under `--boot-root` |
| `/io` | POST | one `IOWire` batch — the wire lane's cache reads |
| `/stats` | GET | what it has served, and which caches it has open |

`--boot-root` is separate from `--root` on purpose: one is build output, the
other is the source tree the manifests live in, and a server that conflated
them would serve either the wrong file or the whole repository.

### Caches are opened on demand, one per identity

Every `/io` batch carries a cache descriptor — epoch, game, revision, quirks,
directory — and the server opens what it is asked for on first use and keeps it
open. One server therefore answers clients booting different generations, and
changing the manifest in the page's URL needs no restart.

Each open cache gets its own `PlatformX_IO`, which is what makes the
decompressed-archive LRU inside it correct rather than a hazard: a group cached
for one generation must never answer a read against another.

Cache directories arrive from another process, so they are treated as input —
resolved under `--boot-root`, and rejected if absolute or containing `..`.

### Staleness: conditional GETs

Boot files are the client's *configuration* — the manifest and the RevConfig
INIs it names — and they are edited by hand between runs. A page that trusted
its stored copy would boot yesterday's configuration; a page that re-downloaded
every file every time would work but would make an offline start impossible.
The answer is the one HTTP already has.

Every file response carries a validator:

```
ETag: "1786554865-5590"          mtime and size
Cache-Control: no-cache          store it, but ask every time
Access-Control-Expose-Headers: ETag
```

and a request that presents `If-None-Match` with a matching tag gets `304 Not
Modified` with no body.

Three details that are load-bearing:

- **`no-cache`, not `no-store`.** `no-store` forbids the browser from keeping
  the copy it would revalidate, which defeats the whole mechanism. Responses
  without a validator — an `/io` batch, `/stats` — still say `no-store`,
  because those genuinely may not be reused.
- **`Access-Control-Expose-Headers`.** A cross-origin response's `ETag` is
  hidden from script unless it is exposed, and a validator nobody can read is
  the same as no validator.
- **mtime + size is not a content hash** and does not claim to be. A file
  rewritten within the same second at exactly the same length is missed. For a
  manifest someone is editing, that is a rounding error against re-reading it
  on every boot.

The host's side of this is in [WEB_CACHE_INDEXEDDB.md](WEB_CACHE_INDEXEDDB.md).

---

## js5_server

```sh
./src/build_opt/js5_server --cache cache.osrs239 --revision 239 --port 43594
```

A read-only revision-239 cache service. The protocol is documented in full in
[JS5_SERVER.md](JS5_SERVER.md); what matters here is how it fits the browser.

### One port, two framings

The first byte decides, and nothing else does: an HTTP upgrade opens with `'G'`,
a JS5 stream with opcode 15. So a desktop client and a browser reach the same
port, and the session state machine never learns which transport it is on.

The WebSocket branch is not a convenience. Emscripten implements BSD sockets as
WebSockets, so the browser build's `connect()` *is* an upgrade request — a
server that speaks only raw TCP is unreachable from a page, with no bridge in
front of it.

The framing lives in `js5/server/js5_server_conn.[ch]`, which has no socket in
it. That is what lets the same connection type be hosted by a different loop
later without duplicating the handshake.

### Staleness: the cache can change underneath it

Everything the server answers with is a snapshot taken when the cache was
opened — the master index, the CRC and version each archive is validated
against, and the decoded reference tables. Repack the cache while the server
runs and that snapshot describes a file that is no longer there: it would hand
out a master its own dat2 disagrees with, and every group read would fail its
CRC with nothing saying why.

So the reactor re-stamps the cache once a second (two `stat` calls on the dat2
and idx255 — every archive write touches one, every reference-table write the
other) and reloads when they move:

```
js5_server: cache.osrs239 changed on disk — reloaded, master=205 bytes
```

Three properties worth knowing:

- **A failed reload is not fatal.** A repack in progress genuinely looks
  corrupt — the writer is mid-file — so the rebuild goes into a scratch store
  and is swapped in only on success. On failure the previous contents keep
  being served and the next check tries again. Verified by truncating a dat2
  under a running server: it says so once, keeps answering, and reloads cleanly
  when the file is restored.
- **The swap is in place.** A session holds a pointer to the store for its
  lifetime, so the contents are replaced inside the object rather than the
  object being replaced. Blobs already handed to a session are owned copies.
- **Sessions mid-download are not told.** JS5 has no message for "the cache
  moved". A client holding the previous master fails a CRC on its next group,
  drops the connection and re-primes — which is exactly the recovery path a
  corrupt local copy already takes.

### What it does not do

No authentication, no origin check, no encryption, and it exposes every group
in the cache. Accepting WebSockets means a browser can reach it directly, which
makes the loopback default matter more rather than less: any page the browser
loads can open a socket to it. `--bind 0.0.0.0` only where the surrounding host
controls who can reach it.

---

## Running the pair

```sh
make -C src web-idb          # module + both servers

./src/build_opt/js5_server --cache cache.osrs239 --revision 239 --port 43594 &
./src/build/io_server --root build-web --boot-root . --port 8099
```

```
http://localhost:8099/index.html?arg=--manifest&arg=manifest_osrs239.ini&arg=--offline
```

The wire lane is one command, since it needs no cache server:

```sh
make -C src web
./src/build/io_server --manifest manifest_osrs239.ini    # http://localhost:8088/
```

`run-live.sh web <manifest> …` drives the wire lane end to end, starting
`io_server` as its own child so a stopped script does not leave a process
holding the port. For a local live `osrs230`/`osrs239` manifest it also starts
native `mock230` on the game port; the browser reaches it over WebSocket.

## Ports

| | default | changed with |
| --- | --- | --- |
| `io_server` HTTP | 8088 | `--port`, or `TORIRS_WEB_PORT` via `run-live.sh` |
| `js5_server` | 43594 | `--port`, and `?js5_port=` on the page |

The page derives the JS5 host from its own origin, so serving the page and the
cache from one machine needs no configuration; `?js5_host=` overrides it when
they are not.
