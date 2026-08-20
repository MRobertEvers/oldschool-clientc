# Incremental JS5 cache loading

This file is the configuration and protocol-fidelity reference. The working
document — current TODOs, procedures, measured discoveries and the decision log
— is [`JS5_INCREMENTAL_CACHE.md`](../JS5_INCREMENTAL_CACHE.md) at the repository
root.

For the pinned RuneLite/GamePack source provenance and a source-to-C mapping,
see [`src/js5/README.md`](../src/js5/README.md).

The native executor can opt into a revision-239 JS5 producer while the core
`App`, task runner, cache providers, and cache-read requests keep their existing
interfaces. JS5 is off by default.

## Boot contract

The executor opens or creates a non-truncating sparse `main_file_cache.dat2` and
then blocks on the metadata-prime barrier: the server's `255/255` master index
and every present reference table are validated and installed before anything
else runs. Only then does it call `App_Init`, after which it attaches JS5 to the
native `PlatformX_IO` instance for the rest of the session.

The barrier runs **before** `App_Init`, not after it. `App_Init` decodes
reference tables itself, so a torn or corrupt `255/N` container reached the
decompressor before the producer that exists to repair it had been attached, and
took the process down. Priming first is what makes the stated invariant — no
game task stepped before the cache has server-authoritative reference metadata —
actually hold. The client attached after `App_Init` re-validates the same tables
against the same master index; on a warm cache that second pass is a local CRC
check costing 208 bytes, so the ordering costs one extra connection and nothing
else. See the root document's "Major discoveries" for the measurements.

After the barrier, the frame loop pumps JS5 in bounded non-blocking steps. A
normal DAT2 read remains synchronous on a cache hit. A miss is parked by the
platform executor, promoted to the urgent JS5 lane, persisted, and then loaded
through the same rscache path into the original IO slot. Background requests
validate and fill the rest of the registered physical indexes.

JS5 owns a separate socket from the game transport. It depends on the rscache
storage adapter and the platform socket layer; it has no dependency on `App`,
the task runner, providers, or game code.

## Configuration

CLI flags:

```text
--js5
--no-js5
--js5-host HOST
--js5-port PORT
--js5-fallback-port PORT
--js5-revision REVISION
```

The equivalent boot-manifest section is:

```ini
[js5:boot]
enabled=true
host=127.0.0.1
port=43594
fallback_port=443
revision=239
```

Omitted host, port, and revision inherit the finalized game endpoint and cache
identity. Port 43594 gets the historical 443 fallback by default; a custom
primary has no implicit fallback. `fallback_port=0` disables fallback. JS5
requires a DAT2 cache and an existing cache directory. `--offline` disables a
manifest-enabled producer unless the command line explicitly includes `--js5`,
which is useful for cache-only testing.

For example, with the dedicated JS5 service on localhost and an already
created empty target directory:

```powershell
src\build\js5_server --cache cache.osrs239 --revision 239 --port 43594
src\torirs C:\temp\cache.osrs239.sparse --manifest manifests/manifest_osrs239.ini `
  --offline --js5 --js5-host 127.0.0.1 --js5-port 43594 `
  --js5-fallback-port 0
```

See `docs/JS5_SERVER.md` for server limits, deployment notes, and tests.

## Revision-239 fidelity

The implementation follows the supplied GamePack deob, principally:

- `class551.method12249`: urgent/normal send and response queues, 200 in-flight
  limits per lane, the 8-byte response header, 512-byte blocks, `0xFF`
  continuation markers, CRC recovery, and XOR reconnect control.
- `class551.method12252`: request deduplication and urgent promotion, including
  the duplicate urgent request retained beside an already in-flight normal
  request.
- `class535.method11932` and `method11936`: local validation followed by
  background repair of missing or invalid groups.
- `class535.method11943`: persistence as the exact JS5 container plus a
  big-endian 32-bit group version.
- `Statics.method12256`: `255/255` master entries as `(crc, version)` pairs and
  reference-table requests at `255/N`.

The handshake is the revision-239 21-byte form: opcode 15, revision, and four
32-bit seeds. Responses preserve compressed and XTEA-encrypted bytes exactly;
CRC covers only the framed container. Local group validation accepts the
modern 32-bit version trailer and the legacy 16-bit fallback used by older
caches.

## Verification

`make -C src test-js5 JS5_TEST_CACHE=<canonical-cache>` uses the canonical
cache only as a read-only mock-server fixture. The test creates its own OS temp
directory and covers fragmented reads/writes, 512-byte framing, mandatory
master/reference requests, persistence reuse, corruption repair, priority
promotion, reconnect replay, and XOR recovery.

The lower storage boundary is covered by `3rd/rscache/test/test_sparse.c`.
Manifest composition is covered by `make -C src test-bootmanifest`, and
`make -C src test-io-wire` verifies that the pre-existing non-JS5 executor wire
path remains linkable and unchanged.
