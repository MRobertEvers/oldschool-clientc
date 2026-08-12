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

All wire behavior below follows revision 239. Byte order is big-endian
throughout, and the connection is a plain TCP stream with no length framing of
its own: every structure is fixed-size or self-describing.

---

## Wire protocol

A session has exactly three phases: handshake, status, then an unbounded
request/response phase that lasts until one side closes the socket.

```
client ──21-byte handshake──────────────────────────────► server
client ◄──1 status byte───────────────────────────────── server
client ──4-byte packets (requests + controls), pipelined─► server
client ◄──block-framed group responses, in service order─ server
```

### Handshake — 21 bytes, client to server

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | opcode `15` |
| 1 | 4 | client revision (`239`) |
| 5 | 16 | four 32-bit seed words |

Opcode 15 is the JS5 branch of the OldSchool login handshake; opcode 14 is the
game-login branch that this dedicated executable does not implement. The seed
words exist because a combined endpoint uses them to derive the login session's
XTEA keys. JS5 itself never uses them: the server reads them for framing, zeroes
the buffer immediately, and never logs them.

The first byte is checked as soon as it arrives, so a game client that dials
this port with opcode 14 is rejected without waiting for 20 more bytes. The
remaining bytes may arrive in any number of TCP segments;
`Js5ServerSessionFeed` accumulates them.

### Status — 1 byte, server to client

| Value | Meaning | Server action |
| --- | --- | --- |
| `0` | OK | enter the request phase |
| `6` | out of date — `revision != --revision` | send the byte, then close |
| `7` | server full — connection cap reached | send the byte, then close |

The status byte is always plaintext. It precedes any opportunity to negotiate
XOR, so it is never masked. On a non-zero status the session moves to `CLOSING`;
it flushes that one byte and closes. Any further client bytes on a rejected
session are a protocol error.

Status 7 is produced without opening a cache handle: an over-cap connection gets
a session constructed with `server_full` set and no `Js5ServerCache*`.

### Request and control packets — 4 bytes, client to server

Every post-handshake client packet is exactly four bytes. There is no
per-packet length, so a desync is unrecoverable and terminates the session.

| Opcode | Name | Bytes 1..3 | Effect |
| --- | --- | --- | --- |
| `0` | request, normal priority | archive, group hi, group lo | append to the normal queue |
| `1` | request, urgent priority | archive, group hi, group lo | append to the urgent queue |
| `2` | logged in | must be `0,0,0` | record the client as in-game |
| `3` | logged out | must be `0,0,0` | record the client as out-of-game |
| `4` | set XOR key | key, `0`, `0` | mask subsequent responses |

Any other opcode, or a non-zero reserved byte on opcodes 2/3/4, fails the
session immediately. The archive byte is `0..255` and the group is a 16-bit
value, which is why `255/255` — the master index — is expressible.

**Opcodes 2 and 3** are advisory. The authoritative client sends them so a live
service can throttle background downloads for players who are actually in the
world. This server records the flag in its per-session stats and does not
otherwise act on it; both lanes are always served at full speed.

**Opcode 4** sets a single-byte XOR mask applied to every response byte,
including the 3-byte response header and the `0xFF` continuation markers. Key
`0` disables masking. The key is captured **per request at the moment the
request is enqueued**, not at the moment the response is written. A key change
therefore affects only requests received after the opcode-4 packet, and any
already-queued responses still go out under the old key. The client relies on
this: it changes keys and reconnects as a recovery step after repeated CRC
failures, so a stale key must never leak into a new request's response.

**Priority.** The urgent queue is drained completely before the normal queue is
consulted, checked afresh before each response. A long normal-lane backlog
cannot delay an urgent group by more than one in-flight response.

**Requests are never deduplicated.** The same `archive/group` may be queued
twice and is answered twice. This is required, not tolerated: when the
authoritative client promotes an already-in-flight normal request to urgent, it
sends a *second* packet on the urgent lane rather than cancelling the first. A
server that collapsed the pair would leave the client waiting for a response
that never comes.

**Backpressure.** Each lane holds at most `--max-pending` (default 200)
requests, matching the client's own per-lane in-flight cap. Overflowing a lane
fails the session rather than silently dropping a request, because a dropped
request is indistinguishable from a hung server on this protocol.

### Response framing — server to client

A response is a 3-byte header followed by the group's exact JS5 container,
interrupted by a `0xFF` marker byte at every 512-byte boundary of the response
stream.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | archive |
| 1 | 2 | group |
| 3 | .. | JS5 container bytes |

The container is self-describing, which is what lets the client size the
response without a length prefix:

| Offset in container | Size | Field |
| --- | --- | --- |
| 0 | 1 | compression: `0` none, `1` BZIP2, `2` GZIP |
| 1 | 4 | compressed length |
| 5 | 4 | uncompressed length — **only when compression != 0** |
| 5 or 9 | compressed length | payload |

So the total container size is `5 + compressedLength` for compression `0` and
`9 + compressedLength` otherwise, and a client that has read the first 8 bytes
of a response (3 header + 5 container prefix) knows exactly how many more bytes
to expect.

Blocking is applied to the wire stream, not the container. The first block
carries the 3 header bytes plus 509 container bytes; every following block is
one `0xFF` marker plus 511 container bytes. Concretely, wire offsets `512`,
`1024`, `1536`, ... are always `0xFF`. The marker is masked by the XOR key like
every other byte.

Responses are strictly serialized per connection: the next group's header is
never interleaved with the current group's payload.

### What the server does *not* send

The container is served exactly as it exists on disk, with two adjustments and
one deliberate omission.

- **Local version trailers are stripped.** Natively built and
  incrementally-downloaded caches append the group's reference-table version
  after the container — 4 bytes on this implementation, 2 bytes on
  RuneLite-written caches. That trailer is local bookkeeping. It is validated at
  serve time against the reference table and then removed, because the CRC the
  client checks covers the container alone.
- **`255/255` is synthesized.** No cache stores a master index; see below.
- **There is no error packet.** JS5 has no way to say "that group does not
  exist" or "that group is corrupt". A request the server cannot satisfy closes
  the session. Silently dropping the response instead would leave the client
  blocked until its own network timeout, which reads as a hang rather than a
  cache problem.

### The master index — `255/255`

Requesting archive `255`, group `255` returns a container the server builds in
memory at startup:

```
[0]        compression = 0 (never compressed)
[1..4]     body length = 8 * N
[5..]      N entries of: CRC32 (u32), version (u32)
```

`N` is `highest present index + 1`. Indices with no `main_file_cache.idxN` are
zero-filled so entry *i* always describes archive *i*. Each entry's CRC is
computed over the archive's **reference-table container** as stored in idx255 —
the same bytes the server will later return for `255/i` — and the version is
that table's decoded version field.

The master is not itself CRC-protected on the wire; the client validates it
structurally (compression `0`, length a multiple of 8, at most 256 entries) and
then trusts it as the root of the chain. Everything below it is verified against
it.

Requesting `255/N` for any other `N` returns that archive's reference table
container, which lists the CRC, version, and child-file layout of every group in
archive `N`.

### Startup validation

`Js5ServerCacheOpen` refuses to start rather than serve a cache that will fail a
client mid-download. It requires `main_file_cache.idx255`, at least one physical
index, and a decodable reference table for **every** `main_file_cache.idxN`
present on disk. At serve time each group is re-validated against its reference
table entry — container CRC exact, version trailer (if present) matching —
before any byte reaches the socket.

### Timeouts

| Option | Applies to |
| --- | --- |
| `--handshake-timeout-ms` | 21 bytes not completed since accept |
| `--idle-timeout-ms` | fully drained session with no new input |
| `--output-timeout-ms` | a pending response making no write progress |

The idle timer only runs when both queues are empty, nothing is being written,
and no bytes are buffered, so a client with a slow but healthy download is never
disconnected as idle.

---

## How a client builds its cache

The protocol is a fetch primitive; the cache-building policy is the client's.
The order below is what [`src/js5/js5.c`](../src/js5/js5.c) implements and what
the authoritative revision-239 client does. It produces a **sparse cache**: a
DAT2 store that starts nearly empty and fills in on demand, rather than a
full download before first frame.

### 1. Fetch the root of trust

Handshake, read the status byte, then request `255/255` urgently. Decode it into
a per-archive `(CRC, version)` table. Nothing else can be validated until this
exists, so a client should treat repeated failure here as fatal rather than
retrying forever.

### 2. Reconcile every reference table

For each archive the master lists, read the local `255/N` from disk if present
and check it against the master's CRC and version. On mismatch or absence,
request `255/N` from the server and persist the container.

A reference table must be decodable *and* agree with the master: this
implementation additionally rejects tables carrying whirlpool or name-hash
flags, which revision 239 does not use.

**Do not step any game task until every required reference table is ready.**
This is the metadata barrier. Without it a group request cannot be validated,
because the expected CRC lives in the reference table.

### 3. Scan what is already on disk

Walk each archive's group list with a bounded per-tick budget and mark each
group ready or missing. A group is ready when its stored container's CRC matches
the reference table entry.

The CRC boundary matters: **CRC is computed over the container only**, never
including the local version trailer. The trailer is checked separately as an
integer comparison. Getting this backwards makes every locally stored group look
corrupt, and the symptom is a client that re-downloads the entire cache on every
boot.

### 4. Queue the gaps on the normal lane

Missing or CRC-failing groups go out as opcode `0`. This is background work and
should not compete with anything the running client is blocking on.

### 5. Promote real cache misses to urgent

When the game core synchronously asks for a group that is not on disk, park that
read and request the group with opcode `1`. If a normal-lane request for the
same group is already in flight, send the urgent packet anyway and let the
duplicate response arrive — do not try to cancel or reorder the first one.

### 6. Validate, persist, then re-read through the normal path

On a completed response, check the container CRC against the reference table
entry. Only then write it to disk, and only then complete the parked read — by
re-running the *same* rscache read the core would have done. The client never
hands network bytes directly to the core; persistence is the completion
boundary, so a crash mid-download leaves a merely incomplete cache rather than
an inconsistent one.

Store the group as `container || version_be32`, taking the version from the
reference table rather than from the wire. Reference tables are stored as the
bare container with no trailer.

**Never decode the payload to validate it.** Compressed and XTEA-encrypted group
bytes are stored byte-exact; JS5's job ends at the container.

### 7. Recover without losing the cache

- **CRC failure** is treated as stream corruption, not cache corruption: pick a
  new random XOR key, reconnect, and replay the outstanding requests. After four
  consecutive failures, give up with a terminal error rather than looping.
- **Reconnect** re-sends the handshake and re-queues everything that was in
  flight. Already-persisted groups are not re-requested.
- **Partial reads and writes** are normal. Both the request and response paths
  must survive arbitrary segmentation — a response header can split across four
  TCP segments, and a 4-byte request can be written one byte at a time.

### Client-side invariants worth asserting

| Invariant | Failure symptom if broken |
| --- | --- |
| CRC covers the container, excludes the trailer | full re-download every boot |
| No task steps before the metadata barrier | group requests with no expected CRC |
| Duplicate urgent request is sent, not suppressed | parked read never completes |
| Payload bytes stored verbatim | XTEA-encrypted groups become unreadable |
| Persist before completing the parked read | inconsistent cache after a crash |

---

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
