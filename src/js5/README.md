# JS5 incremental cache

This directory contains the revision-239 JS5 implementation used to build a
sparse DAT2 cache while the C client is running. JS5 is opt-in and is owned by
the native executor: the core `App`, task runner, cache providers, and cache
request interfaces did not need to change.

The implementation is split into three deliberately narrow layers:

- `js5.[ch]` is the protocol and incremental-cache state machine. Its public
  dependencies are callback tables for transport and storage.
- `js5_rscache.[ch]` is the persistence adapter. It is the only client-side JS5
  code that knows how to install data through rscache.
- `server/` is a standalone, read-only JS5 service. Its protocol session layer
  has no socket dependency, and its reactor owns the platform network calls.

The executor-facing bridge lives in
[`platform_x_io_js5_cache.c`](../platform/platform_x_io_js5_cache.c) and
[`platform_x_io.c`](../platform/platform_x_io.c). Broader usage and deployment
details are in
[`docs/JS5_INCREMENTAL_CACHE.md`](../../docs/JS5_INCREMENTAL_CACHE.md) and
[`docs/JS5_SERVER.md`](../../docs/JS5_SERVER.md).

The browser build uses this same client against a cache that is not a dat2 file
— IndexedDB records behind a dat2 facade — with the metadata barrier driven by
the page rather than by a spin loop, and the background fill off by default. See
[`docs/WEB_CACHE_INDEXEDDB.md`](../../docs/WEB_CACHE_INDEXEDDB.md). Nothing in
`js5.[ch]` is web-specific; the storage adapter and the executor around it are
what differ.

## Source provenance

The review used the existing local checkouts below. The commit IDs are recorded
instead of branch names so the source comparison is reproducible.

| Source | Local checkout | Origin | Reviewed commit |
| --- | --- | --- | --- |
| RuneLite fork | `C:\Users\mrobe\Documents\git_repos\Runelite` | `https://github.com/MRobertEvers/Runelite.git` | [`4f3ff3e2ad6aa239d5738ee5b492cd48208db163`](https://github.com/MRobertEvers/Runelite/commit/4f3ff3e2ad6aa239d5738ee5b492cd48208db163) |
| Official GamePack deob | `C:\Users\mrobe\Documents\git_repos\Deob` | `https://github.com/MRobertEvers/Deob.git` | [`74594dd4a77c43a84f7fc745aebd581496a75f9d`](https://github.com/MRobertEvers/Deob/commit/74594dd4a77c43a84f7fc745aebd581496a75f9d) |

The GamePack source reviewed for this revision is
`Deob/src_osrs239_rl1_12_33/deob/`. To recreate the two source checkouts:

```powershell
git clone https://github.com/MRobertEvers/Runelite.git
git -C Runelite checkout 4f3ff3e2ad6aa239d5738ee5b492cd48208db163

git clone https://github.com/MRobertEvers/Deob.git
git -C Deob checkout 74594dd4a77c43a84f7fc745aebd581496a75f9d
```

No RuneLite or GamePack Java source is copied, compiled, generated into, or
linked by this project. The C implementation is a manual translation of the
observed behavior into the interfaces in this repository.

### Which source is authoritative

Source precedence matters because the inspected RuneLite checkout is a cache
tooling implementation, not the revision-239 client runtime:

1. The revision-239 Official GamePack deob is authoritative for the handshake,
   request queues, response framing, retry behavior, validation, and write
   semantics.
2. RuneLite's `cache` module corroborates the JS5 container, reference-table,
   IDX, and DAT2 formats and provides an independent interoperability reference.
3. The supplied `cache.osrs239` is an empirical, read-only compatibility
   fixture. It is not treated as a protocol specification.

When the sources differ, the GamePack behavior wins. In particular, the
reviewed RuneLite [`Container.compress`](https://github.com/MRobertEvers/Runelite/blob/4f3ff3e2ad6aa239d5738ee5b492cd48208db163/cache/src/main/java/net/runelite/cache/fs/Container.java#L51-L90)
writes a 16-bit revision trailer, while the revision-239 GamePack
[`class535.method11943`](https://github.com/MRobertEvers/Deob/blob/74594dd4a77c43a84f7fc745aebd581496a75f9d/src_osrs239_rl1_12_33/deob/class535.java#L261-L290)
writes all four big-endian version bytes. Downloaded C-client groups therefore
use a 32-bit trailer. Both the GamePack validator and this implementation accept
the 32-bit form first and the legacy 16-bit form as a fallback, which preserves
compatibility with older caches.

## Source-to-implementation map

### Official GamePack deob

| Reference | Behavior carried into C | C implementation |
| --- | --- | --- |
| [`class551.method12249`](https://github.com/MRobertEvers/Deob/blob/74594dd4a77c43a84f7fc745aebd581496a75f9d/src_osrs239_rl1_12_33/deob/class551.java#L106) | Nonblocking pump, urgent and normal lanes, 200 in-flight requests per lane, response headers, 512-byte blocks and `0xFF` continuation markers, CRC recovery, and XOR reconnect state | [`js5.c`](js5.c) |
| [`class551.method12252`](https://github.com/MRobertEvers/Deob/blob/74594dd4a77c43a84f7fc745aebd581496a75f9d/src_osrs239_rl1_12_33/deob/class551.java#L792) | Deduplication and priority promotion, including an urgent duplicate when the normal copy is already in flight | [`js5.c`](js5.c) |
| [`class535.method11932`](https://github.com/MRobertEvers/Deob/blob/74594dd4a77c43a84f7fc745aebd581496a75f9d/src_osrs239_rl1_12_33/deob/class535.java#L431) and [`method11936`](https://github.com/MRobertEvers/Deob/blob/74594dd4a77c43a84f7fc745aebd581496a75f9d/src_osrs239_rl1_12_33/deob/class535.java#L321) | Local group CRC/version validation, bounded scanning, and background repair | [`js5.c`](js5.c) |
| [`class535.method11943`](https://github.com/MRobertEvers/Deob/blob/74594dd4a77c43a84f7fc745aebd581496a75f9d/src_osrs239_rl1_12_33/deob/class535.java#L261) | Persist an ordinary group as the exact JS5 container followed by its big-endian 32-bit version | [`js5_rscache.c`](js5_rscache.c) |
| [`class535.method11955`](https://github.com/MRobertEvers/Deob/blob/74594dd4a77c43a84f7fc745aebd581496a75f9d/src_osrs239_rl1_12_33/deob/class535.java#L612) and [`method11939`](https://github.com/MRobertEvers/Deob/blob/74594dd4a77c43a84f7fc745aebd581496a75f9d/src_osrs239_rl1_12_33/deob/class535.java#L597) | Activate a validated reference table and begin local validation | [`js5.c`](js5.c), [`js5_rscache.c`](js5_rscache.c) |
| [`Statics.method12256`](https://github.com/MRobertEvers/Deob/blob/74594dd4a77c43a84f7fc745aebd581496a75f9d/src_osrs239_rl1_12_33/deob/Statics.java#L25458) | Decode master `(CRC, version)` pairs and request each `255/N` reference table | [`js5.c`](js5.c) |

### RuneLite cache module

| Reference | What it corroborated | Corresponding local code |
| --- | --- | --- |
| [`Container.java`](https://github.com/MRobertEvers/Runelite/blob/4f3ff3e2ad6aa239d5738ee5b492cd48208db163/cache/src/main/java/net/runelite/cache/fs/Container.java) | Container lengths, compression envelope, CRC boundary, and readable revision trailers | [`js5.c`](js5.c), rscache container parsing |
| [`DiskStorage.java`](https://github.com/MRobertEvers/Runelite/blob/4f3ff3e2ad6aa239d5738ee5b492cd48208db163/cache/src/main/java/net/runelite/cache/fs/jagex/DiskStorage.java) | Reference tables in index 255 and propagation of archive CRC/version metadata | [`js5_rscache.c`](js5_rscache.c), [`js5_server_cache.c`](server/js5_server_cache.c) |
| [`DataFile.java`](https://github.com/MRobertEvers/Runelite/blob/4f3ff3e2ad6aa239d5738ee5b492cd48208db163/cache/src/main/java/net/runelite/cache/fs/jagex/DataFile.java) | DAT2 sector chains | rscache `dat2disk` implementation |
| [`IndexFile.java`](https://github.com/MRobertEvers/Runelite/blob/4f3ff3e2ad6aa239d5738ee5b492cd48208db163/cache/src/main/java/net/runelite/cache/fs/jagex/IndexFile.java) | Six-byte IDX entries | rscache `dat2disk` implementation |
| [`IndexData.java`](https://github.com/MRobertEvers/Runelite/blob/4f3ff3e2ad6aa239d5738ee5b492cd48208db163/cache/src/main/java/net/runelite/cache/index/IndexData.java) | Reference-table protocols 5/6/7, delta-coded IDs, CRCs, versions, and child IDs | rscache reference-table parser and [`js5.c`](js5.c) |

## Incremental client behavior

The client starts with a sparse, non-truncating DAT2 store and builds it in this
order:

1. Send the revision-239 21-byte handshake: opcode 15, revision, and four
   32-bit seed words.
2. Request `255/255`, validate the master index, and read its `(CRC, version)`
   entry for every archive.
3. Load a valid local `255/N` reference or request and persist it. No game task
   is stepped until every required reference table is ready.
4. Scan existing groups with a bounded per-tick budget. CRC is calculated over
   the JS5 container only; the trailing group version is checked separately.
5. Queue invalid or absent groups in the normal background lane. A synchronous
   cache miss from the core is parked by the executor and requested urgently.
6. Persist a validated network response, then retry the original read through
   the same rscache path the core already uses.

The pump is nonblocking after the metadata barrier. It supports partial socket
reads and writes, independent urgent/normal queues, duplicate promotion,
connection inactivity timeouts, primary/fallback ports, replay after reconnect,
CRC-triggered XOR recovery, and progress/completion reporting. Compressed or
XTEA-encrypted group bytes are preserved exactly; JS5 validates and stores the
framed container without decoding the group's contents.

The protocol core only sees `Js5TransportOps` and `Js5StorageOps`. The default
transport uses the platform `SockStream` abstraction, while the storage adapter
uses rscache. Tests replace both callback tables with deterministic in-memory
implementations.

## Executor boundary and pre-prime

Opt-in configuration is resolved in
[`executor_config.c`](../executor_config.c), and orchestration remains in
[`main.c`](../main.c). The executor:

1. Opens or creates the sparse cache before `App_Init`.
2. Runs `App_Init` with the existing core interfaces.
3. Attaches JS5 and pumps the master/reference-table metadata barrier.
4. Calls `App_OpenRootInterface` only after metadata is ready.
5. Pumps JS5 in bounded steps during the existing frame loop.

This pre-primes the reference tables before anything steps the game, without
teaching the core client about JS5 or asynchronous cache construction. Cache
hits remain synchronous. On a miss,
[`platform_x_io.c`](../platform/platform_x_io.c) owns the pending executor IO
slot, asks JS5 for the group, and completes that same slot after persistence.
With JS5 disabled, the original full-cache path is unchanged.

## Dedicated server

The server is a separate executable and never writes to the served cache:

- [`js5_server_cache.c`](server/js5_server_cache.c) opens DAT2 through rscache's
  read-only API, validates reference/group metadata, strips local version
  trailers from responses, and synthesizes `255/255`.
- [`js5_server_session.c`](server/js5_server_session.c) is a socket-free state
  machine for fragmented handshakes, requests, controls, priority queues,
  response framing, XOR, and per-client limits.
- [`js5_server.c`](server/js5_server.c) is the cross-platform nonblocking socket
  reactor. It prevents one stalled connection from blocking other clients.
- [`js5_server_main.c`](server/js5_server_main.c) owns CLI parsing and process
  startup.

It is intentionally a dedicated JS5 endpoint. It does not multiplex the game
login protocol, authenticate clients, or encrypt traffic. The default bind is
therefore loopback. The legacy `mock-js5` build target now builds this same
production implementation under a compatibility executable name.

## Build and run

From the repository root on Windows:

```powershell
mingw32-make -C src js5-server

src\build\js5_server.exe `
  --cache C:\Users\mrobe\Documents\git_repos\oldschool-clientc\cache.osrs239 `
  --revision 239 `
  --bind 127.0.0.1 `
  --port 43594
```

The stable readiness line is `READY 127.0.0.1 43594 239`. To opt the C client
into that endpoint:

```powershell
src\torirs C:\temp\cache.osrs239.sparse --manifest manifests/manifest_osrs239.ini `
  --offline --js5 --js5-host 127.0.0.1 --js5-port 43594 `
  --js5-fallback-port 0
```

`--js5` is required for an explicit offline test; otherwise `--offline`
suppresses a manifest-enabled network producer. The equivalent manifest keys
and all server limits are documented in the two guides linked at the top.

## Verification

The supplied cache is used as a read-only server fixture:

```powershell
$cache = 'C:/Users/mrobe/Documents/git_repos/oldschool-clientc/cache.osrs239'

mingw32-make -C src test-js5 JS5_TEST_CACHE=$cache
mingw32-make -C src test-js5-server JS5_TEST_CACHE=$cache
mingw32-make -C src test-io-wire
mingw32-make -C src test-bootmanifest
```

The JS5 and loopback tests download into disposable OS temporary directories;
they do not mutate the canonical cache. The fixture's
`main_file_cache.dat2` SHA-256 observed during implementation was:

```text
3925F52B275B4010FC79925BA9C4DD6A6C70BB8108434418F1C66F959D651B9E
```

Test coverage includes fragmented and partial IO, 512-byte framing, mandatory
master/reference sequencing, cache reuse, corruption repair, priority
promotion, reconnect replay, XOR recovery, queue/deadline limits, two-client
backpressure, server-to-real-client population of a sparse cache, the original
executor IO wire path, and boot-manifest composition.

## Updating for another revision

For a future cache revision:

1. Pin and record the matching GamePack deob commit and source directory.
2. Re-audit the handshake, control opcodes, master layout, block framing,
   request promotion, CRC boundary, and local version trailer.
3. Compare RuneLite's cache readers/writers at a pinned commit for disk-format
   interoperability, without treating them as the runtime authority.
4. Run both client and server suites against an immutable cache for the target
   revision and record its DAT2 hash.
5. Keep revision-specific policy in the executor/configuration layer unless the
   wire format itself requires a protocol-core change.
