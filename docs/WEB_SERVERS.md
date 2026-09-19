# The servers a browser run needs

> Build them together: `make -C src servers`. Both web targets (`web`,
> `web-debug`) depend on it, so a build that produces the module also produces
> the processes that feed it.

A desktop client opens a cache directory and a socket. A browser tab can do
neither. The cache moves into the browser -- one record per archive in
IndexedDB, filled on demand by a *producer* that speaks the cache's own
protocol -- and everything the tab cannot reach directly is answered by a
native process.

```
              ┌────────────────── the browser tab ──────────────────┐
              │ index.html ── torirs_host.js ── torirs.wasm         │
              │       torirs_hostio.js  (files, boot, one archive)  │
              │       torirs_idb.js     (IndexedDB: groups/files)   │
              │       torirs_js5.js     torirs_ondemand.js          │
              └──────┬───────────────────┬────────────────┬─────────┘
   GET /             │        JS5 over   │     emscripten │  game
   GET /boot/<path>  │        WebSocket  │     socket     │  (WebSocket)
   GET /cache/dat1/… │                   │                │
                     ▼                   ▼                ▼
             ┌──────────────┐    ┌───────────────┐  ┌────────────────┐
             │  io_server   │    │ JS5 source:   │  │ game server    │
             │  HTTP        │    │ torirsserver  │  │ torirsserver / │
             │              │    │ (game port)   │  │ LostCity       │
             │              │    │ or js5_server │  │                │
             └──────┬───────┘    └───────┬───────┘  └────────────────┘
                    └── the cache on disk ┘
```

Who answers what, by kind of read:

| the client asks for | where it comes from |
| --- | --- |
| a dat2 group or reference table (osrs239, osrs230) | IndexedDB, else **JS5** over a WebSocket -- `torirsserver` serves JS5 on its game port (first byte picks), or a standalone `js5_server` |
| a dat1 container or map square (LostCity rs254 / rs289) | IndexedDB, else **io_server's dat1 proxy**, `GET /cache/dat1/…`: a page cannot speak the 2004 on-demand protocol (raw TCP) and the jag archives have no CORS |
| the manifest, RevConfig INIs, plugin scripts | IndexedDB with a validator, revalidated against **io_server** `GET /boot/<path>` |
| the player's own files | IndexedDB only |

The page and the module come from `io_server` too. So a run always has
`io_server` plus a game server, and for a dat2 world the game server is also
the JS5 source. There is one web lane; the "wire" lane, in which the browser
held no cache and every read was a `POST /io` to io_server, was removed (see
the note in `src/platform/platform.mk`). io_server still answers `POST /io`
and `make -C src test-io-wire` still checks the codec, but no client build
sends to it.

---

## io_server

```sh
./src/build/io_server --manifest manifests/manifest_osrs239.ini    # http://localhost:8088/
./src/build/io_server --root build-web --boot-root . --port 8099
```

| route | method | what it does |
| --- | --- | --- |
| `/` and everything else | GET | static files under `--root` (default `build-web`) |
| `/boot/<path>` | GET | a file the client opens by name, under `--boot-root`, with a validator |
| `/cache/dat1/<table>/<archive>` | GET | one raw dat1 container, proxied off the LostCity server the manifest names |
| `/cache/dat1/batch` | POST | several of those, pipelined on one connection |
| `/stats` | GET | one line: which caches are open, what was served |
| `/status` | GET | the same, as a page |
| `/io` | POST | an `IOWire` batch; kept for `test-io-wire`, unused by the client |

`--boot-root` is separate from `--root` on purpose: one is build output, the
other is the source tree the manifests live in, and a server that conflated
them would serve either the wrong file or the whole repository.

### Caches and worlds are opened on demand

`--manifest` is only a preopen. A dat1 proxy request names the manifest it is
booting -- the same path the page fetched through `/boot/` moments earlier --
and the server opens that world's on-demand connection on first use and keeps
it. One process serves every world at once, and changing the manifest in the
page's URL needs no restart. Cache directories arrive from another process, so
they are treated as input: resolved under `--boot-root`, and rejected if
absolute or escaping it.

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
  without a validator — a proxied dat1 container, `/stats` — still say
  `no-store`, because those genuinely may not be reused.
- **`Access-Control-Expose-Headers`.** A cross-origin response's `ETag` is
  hidden from script unless it is exposed, and a validator nobody can read is
  the same as no validator.
- **mtime + size is not a content hash** and does not claim to be. A file
  rewritten within the same second at exactly the same length is missed. For a
  manifest someone is editing, that is a rounding error against re-reading it
  on every boot.

The host's side of this is in [WEB_CACHE_INDEXEDDB.md](WEB_CACHE_INDEXEDDB.md).

---

## JS5: torirsserver's game port, or js5_server

```sh
./src/build_opt/torirsserver 43594 --rev osrs239          # game + JS5 on one socket
./src/build_opt/js5_server --cache cache.osrs239 --revision 239 --port 43594
```

The osrs239 launch profile has no separate JS5 process: `torirsserver` serves
JS5 on its game port, and the page's default JS5 port is that game port.
`js5_server` is the standalone form of the same service -- a read-only
revision-239 cache service. The protocol is documented in full in
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

## Running it

```sh
./launch run osrs239-web        # torirsserver + io_server, then the page URL
```

By hand, the same two processes:

```sh
make -C src web                                  # module + both servers
TORIRSSERVER_CACHE=cache.osrs239 ./src/build_opt/torirsserver 43594 --rev osrs239 &
./src/build/io_server --manifest manifests/manifest_osrs239.ini --root build-web --boot-root .
```

```
http://localhost:8088/?args=--manifest,manifests/manifest_osrs239.ini
```

`run-live.sh web <manifest> …` drives the same pair from a script, starting
`io_server` as its own child so a stopped script does not leave a process
holding the port, and a native `ToriRSServer` for a local live world.

## Ports and where the page dials

| | default | changed with |
| --- | --- | --- |
| `io_server` HTTP | 8088 | `--port`; the page derives `/boot`, `/stats`, `/cache/dat1` from its own URL, `?io=` overrides |
| JS5 | `ws://<page host>:43594` | `?js5_host=`, `?js5_port=`, or `?js5_url=` |
| game socket | the manifest's `ws_host:ws_port` | `?ws=` |

Two WebSockets leave the page, JS5 and the game, and both default to a plain
`ws://host:port`. That is right on a LAN and wrong behind anything that
terminates TLS: an `https:` page may not open `ws:` (mixed content, refused
before connecting), and a reverse proxy reaches a server by path, not port.
`?ws=<url-or-path>` names the game socket; JS5 then dials the same URL, since
the server picks JS5 or game off the first byte. `?js5_url=` names JS5
separately if the two ever split.

---

## Deploying it: one zip

```sh
make -C src deploy-osrs239          # -> build/deploy/torirs-osrs239-<date>-<sha>.zip
python tools/deploy/build_osrs239_package.py --skip-build   # restage what is built
```

`tools/deploy/build_osrs239_package.py` builds the script pack (if stale), the
web client and both servers, then stages `bin/`, `build-web/`,
`cache.osrs239/`, the slice of `OSRS-Content/osrs239-content` torirsserver
opens (not the 11 GB selftest corpus, and only the `.jm2` maps), the manifest
rewritten for that layout, `revconfig/`, `script/` and start scripts, and zips
it. The servers are native binaries, so run it on the OS that will host the
package. The zip's README covers running it; `install-windows-task.ps1`
registers a startup task and the firewall rules on Windows.

Behind a TLS-terminating reverse proxy: `ws=ws` on `https://host/torirs/`
sets the game socket to `wss://host/torirs/ws`, and JS5 follows it; the proxy
pipes that path's upgrade raw to the game port, where the server terminates
the WebSocket itself. Every HTTP default (`/boot`, `/stats`) is relative to the
page's directory, so a prefix mount needs no other configuration. This was
learned the hard way: the first public deployment routed the game socket and
not JS5, and every cache read came back empty.

