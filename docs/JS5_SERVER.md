# Dedicated JS5 server

For the pinned RuneLite/GamePack source provenance and a source-to-C mapping,
see [`src/js5/README.md`](../src/js5/README.md).

`js5_server` is a native, read-only revision-239 cache service. It is a separate
process from both the C client and the mock game server:

```powershell
mingw32-make -C src js5-server
src\build\js5_server.exe `
  --cache C:\caches\cache.osrs239 `
  --revision 239 `
  --bind 127.0.0.1 `
  --port 43594
```

After validating the cache and binding the listener it prints the stable line:

```text
READY 127.0.0.1 43594 239
```

The cache path is required. The revision defaults to 239, the bind address to
loopback, and the port to 43594. Other options are shown by `--help`:

```text
--max-clients 48
--backlog 64
--max-pending 200
--handshake-timeout-ms 10000
--idle-timeout-ms 300000
--output-timeout-ms 30000
--verbose
```

The historical `make -C src mock-js5` target remains as an executable-name
compatibility build, but it now uses this same implementation and command line.

## Architecture

The server is kept below the client/executor boundary:

- `js5_server_cache` opens the cache through rscache's read-only DAT2 handle,
  validates every physical index/reference pair, and constructs `255/255`.
- `js5_server_session` is a socket-free protocol state machine with fragmented
  input support, independent urgent and normal queues, per-session XOR state,
  bounded buffers, and deadlines.
- `js5_server` is a cross-platform nonblocking reactor. A stalled client cannot
  block accepts, request parsing, or writes for another client. Cache reads stay
  serialized in that reactor, so rscache's file cursor is never shared across
  threads.

The wire behavior follows revision 239: a 21-byte handshake, unencrypted status
byte, four-byte requests and controls, raw containers without local version
trailers, and `0xFF` continuation markers at 512-byte boundaries. Opcode 4 XORs
every subsequent response byte for that connection. Same-key requests are never
deduplicated because the authoritative urgent-promotion path can intentionally
send both a normal and urgent copy.

Missing or corrupt requested groups close the session; JS5 has no per-request
error packet, and silently dropping one would leave the client waiting until its
network timeout.

## Security and deployment

JS5 is plaintext and unauthenticated and exposes every cache group. The default
loopback bind is intentional. Use `--bind 0.0.0.0` only when network exposure is
expected and controlled by the surrounding host/firewall.

The four handshake seed words are accepted for protocol compatibility, cleared
immediately, and never logged. The service opens DAT2 and all index data with
read-only handles; it does not update or repair the served cache.

A traditional OldSchool endpoint multiplexes game login opcode 14 and JS5
opcode 15 on one port. This executable is deliberately dedicated, so a combined
deployment needs a TCP front end or the existing game-session handshake branch
to route those protocols.

## Verification

`make -C src test-js5-server JS5_TEST_CACHE=<cache>` runs the pure session/store
suite and the loopback suite. Coverage includes fragmented handshakes and
requests, statuses 0/6/7, urgent ordering, duplicate requests, XOR framing,
partial writes, queue/time limits, two-client backpressure, and an actual C JS5
client populating a disposable sparse cache.

The supplied canonical cache is used only through read-only handles; the test
target writes downloaded groups exclusively to OS-owned temporary directories.
