# GameIO Architecture Diagram

## System Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                           Browser Environment                        │
│                                                                      │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │                     Your Application                        │   │
│  │                                                              │   │
│  │  const gameIO = new GameIO("ws://localhost:8080/");       │   │
│  │  await gameIO.initialize();                                │   │
│  │  const data = await gameIO.requestArchive(5, 0);          │   │
│  └──────────────────────┬───────────────────────────────────────┘   │
│                         │                                            │
│                         ↓                                            │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    GameIO Class                              │   │
│  │  ┌──────────────┐           ┌──────────────┐               │   │
│  │  │  IndexedDB   │           │  WebSocket   │               │   │
│  │  │   Manager    │           │   Manager    │               │   │
│  │  └──────┬───────┘           └──────┬───────┘               │   │
│  │         │                           │                        │   │
│  │         ↓                           ↓                        │   │
│  │  ┌──────────────┐           ┌──────────────┐               │   │
│  │  │ Check Cache  │           │ Send Request │               │   │
│  │  │ Get/Put      │           │ Parse Response│               │   │
│  │  └──────────────┘           └──────────────┘               │   │
│  └───────────┬───────────────────────┬────────────────────────┘   │
│              │                       │                             │
│              ↓                       ↓                             │
│  ┌──────────────────────┐  ┌──────────────────────┐              │
│  │   IndexedDB API      │  │   WebSocket API      │              │
│  │  (Browser Native)    │  │  (Browser Native)    │              │
│  └──────────────────────┘  └──────────┬───────────┘              │
│                                        │                           │
└────────────────────────────────────────┼───────────────────────────┘
                                         │
                                         │ ws://localhost:8080/
                                         │
                    ┌────────────────────┴──────────────────────┐
                    │                                            │
┌───────────────────▼────────────────────┐  ┌───────────────────▼─────────┐
│       WebSocket Bridge                 │  │    Asset Server              │
│       (websockify)                     │  │    (asset_server.c)          │
│                                        │  │                              │
│  - Port: 8080                         │  │  - Port: 4949                │
│  - Protocol: WebSocket ↔ TCP         │──►│  - Protocol: TCP             │
│  - Function: Bridge only              │  │  - Function: Serve archives  │
└────────────────────────────────────────┘  └──────────────┬───────────────┘
                                                            │
                                                            │ reads from
                                                            ↓
                                            ┌────────────────────────────┐
                                            │    Cache Directory         │
                                            │    ../cache/               │
                                            │                            │
                                            │  - main_file_cache.dat2   │
                                            │  - main_file_cache.idx255 │
                                            │  - xteas.json              │
                                            └────────────────────────────┘
```

## Request Flow Diagram

```
┌─────────────┐
│   Browser   │
│ Application │
└──────┬──────┘
       │
       │ 1. requestArchive(5, 0)
       ↓
┌──────────────────────────────────────────────────────┐
│              GameIO.requestArchive()                 │
│                                                      │
│  ┌────────────────────────────────────────┐        │
│  │  Check IndexedDB Cache                 │        │
│  │  loadArchiveFromIndexedDB(5, 0)       │        │
│  └──────────────┬─────────────────────────┘        │
│                 │                                   │
│                 ↓                                   │
│       ┌─────────┴─────────┐                        │
│       │ Found?            │                        │
│       └─┬───────────────┬─┘                        │
│         │ YES           │ NO                       │
│         │               │                          │
│         ↓               ↓                          │
│   ┌─────────┐    ┌───────────────────────┐        │
│   │ Return  │    │ Request from Server   │        │
│   │ Cached  │    │                       │        │
│   │ Data    │    │ 2. Add to queue       │        │
│   └─────────┘    │ 3. Send request       │        │
│                  └───────────┬───────────┘        │
│                              │                    │
└──────────────────────────────┼────────────────────┘
                               │
                               │ WebSocket
                               │ [request_code(1), table(1), archive_id(4)]
                               ↓
                    ┌──────────────────┐
                    │  Asset Server    │
                    │                  │
                    │  1. Parse request│
                    │  2. Load archive │
                    │  3. Send response│
                    └────────┬─────────┘
                             │
                             │ [status(4), size(4), data(size)]
                             ↓
┌─────────────────────────────────────────────────────┐
│         GameIO.handleWebSocketMessage()             │
│                                                     │
│  ┌────────────────────────────────────┐            │
│  │  State Machine                     │            │
│  │  ┌──────────────────────────────┐ │            │
│  │  │ 1. ReadingStatus (4 bytes)   │ │            │
│  │  │    ↓                          │ │            │
│  │  │ 2. ReadingSize (4 bytes)     │ │            │
│  │  │    ↓                          │ │            │
│  │  │ 3. ReadingData (size bytes)  │ │            │
│  │  └──────────────────────────────┘ │            │
│  └─────────────┬──────────────────────┘            │
│                │                                   │
│                ↓                                   │
│   ┌────────────────────────────┐                  │
│   │  Save to IndexedDB         │                  │
│   │  saveArchiveToIndexedDB()  │                  │
│   └────────────┬───────────────┘                  │
│                │                                   │
│                ↓                                   │
│   ┌────────────────────────────┐                  │
│   │  Resolve Promise           │                  │
│   │  Return data to app        │                  │
│   └────────────────────────────┘                  │
└─────────────────────────────────────────────────────┘
       │
       │ 4. Return ArrayBuffer
       ↓
┌──────────────┐
│   Browser    │
│ Application  │
└──────────────┘
```

## State Machine Detail

```
Response Parser State Machine:

Initial State: ReadingStatus

┌─────────────────┐
│ ReadingStatus   │◄───────────────┐
└────────┬────────┘                │
         │                         │
         │ Have 4 bytes?           │
         ↓                         │
    ┌─────────┐                    │
    │  Yes    │                    │
    └────┬────┘                    │
         │                         │
         │ Read status             │
         │ (big endian uint32)     │
         ↓                         │
┌─────────────────┐                │
│  ReadingSize    │                │
└────────┬────────┘                │
         │                         │
         │ Have 4 bytes?           │
         ↓                         │
    ┌─────────┐                    │
    │  Yes    │                    │
    └────┬────┘                    │
         │                         │
         │ Read size               │
         │ (big endian uint32)     │
         ↓                         │
    ┌─────────┐                    │
    │ Size=0? │                    │
    └────┬────┘                    │
         │                         │
    ┌────┴────┐                    │
    │ Yes│ No │                    │
    └────┼────┘                    │
         │    │                    │
         │    ↓                    │
         │ ┌──────────────┐        │
         │ │ ReadingData  │        │
         │ └──────┬───────┘        │
         │        │                │
         │        │ Have size bytes?
         │        ↓                │
         │   ┌─────────┐           │
         │   │  Yes    │           │
         │   └────┬────┘           │
         │        │                │
         │        │ Read data      │
         │        │ Save to DB     │
         │        │                │
         ↓        ↓                │
    ┌────────────────┐             │
    │ Complete       │             │
    │ Resolve Promise│             │
    │ Process Next   │─────────────┘
    └────────────────┘

Note: If at any point we don't have enough bytes,
we return and wait for more data from WebSocket.
```

## IndexedDB Schema

```
Database: GameArchiveCache (version 1)

┌────────────────────────────────────────────┐
│         Object Store: archives             │
├────────────────────────────────────────────┤
│  Key Path: [tableId, archiveId]           │
│                                            │
│  Indexes:                                  │
│    - tableId (non-unique)                  │
│                                            │
│  Record Structure:                         │
│  {                                         │
│    tableId: number,        // 0-255        │
│    archiveId: number,      // 0-...        │
│    data: ArrayBuffer,      // raw bytes    │
│    timestamp: number       // Date.now()   │
│  }                                         │
└────────────────────────────────────────────┘

Example Queries:

- Get single archive:
  get([5, 0])  // table 5, archive 0

- Get all archives for a table:
  index("tableId").getAll(5)

- Count total archives:
  count()

- Clear all:
  clear()
```

## Protocol Specification

### Request Packet (6 bytes)

```
Byte Offset:  0        1        2     3     4     5
             ┌────┬─────────┬───────────────────────┐
             │ RC │  Table  │     Archive ID        │
             └────┴─────────┴───────────────────────┘
Size:         1      1              4 bytes
Encoding:    u8     u8         big-endian u32

RC = Request Code (always 1)
Table = Table ID (0-254 normal, 255 reference)
Archive ID = Archive number within table
```

### Response Packet (8+ bytes)

```
Byte Offset:  0  1  2  3    4  5  6  7    8...N
             ┌───────────┬───────────┬───────────┐
             │  Status   │   Size    │   Data    │
             └───────────┴───────────┴───────────┘
Size:         4 bytes     4 bytes      N bytes
Encoding:  big-endian  big-endian    raw binary
             u32         u32

Status = 1 (success) or 0 (error)
Size = Number of data bytes
Data = Raw archive data (if size > 0)
```

## Caching Strategy

```
Request Archive
      ↓
┌─────────────────┐
│ Check Cache     │
└────────┬────────┘
         │
    ┌────┴────┐
    │ Found?  │
    └────┬────┘
         │
    ┌────┴────┐
    │ Yes│ No │
    └────┼────┘
         │    │
         │    ├─→ Request from Server
         │    │        ↓
         │    │   Receive Response
         │    │        ↓
         │    └──→ Save to Cache
         │             │
         ↓             ↓
     ┌───────────────────┐
     │  Return Data      │
     └───────────────────┘

Cache Hit:  ~1-5ms    (IndexedDB)
Cache Miss: ~20-50ms  (Network + IndexedDB)
```

## Concurrency Model

```
Multiple Requests Arrive:

Request A ────┐
Request B ────┼──► Queue ──► Process One at a Time
Request C ────┘                     │
                                    │
                            ┌───────┴────────┐
                            │                │
                            ↓                │
                    Send Request A           │
                            │                │
                            ↓                │
                    Wait for Response        │
                            │                │
                            ↓                │
                    Resolve Promise A        │
                            │                │
                            ├────────────────┘
                            ↓
                    Send Request B
                            │
                            ↓
                    Wait for Response
                            │
                            ↓
                    Resolve Promise B
                            │
                            ↓
                          (etc.)

Why Sequential?
- Protocol requires request/response pairing
- Simplifies state management
- Still fast due to caching
```

## Error Handling

```
Request Archive
      ↓
┌─────────────────────┐
│ WebSocket Open?     │
└──────────┬──────────┘
           │
      ┌────┴────┐
      │ Yes│ No │──► Throw Error: "WebSocket not connected"
      └────┬────┘
           │
           ↓
┌─────────────────────┐
│ Check Cache         │
└──────────┬──────────┘
           │
      ┌────┴────┐
      │ Error?  │──► Log & Continue (fallback to network)
      └────┬────┘
           │
           ↓
┌─────────────────────┐
│ Send Request        │
└──────────┬──────────┘
           │
           ↓
┌─────────────────────┐
│ Receive Response    │
└──────────┬──────────┘
           │
      ┌────┴────┐
      │ Status? │
      └────┬────┘
           │
      ┌────┴────┐
      │ 1  │ 0  │──► Return null (archive not found)
      └────┬────┘
           │
           ↓
┌─────────────────────┐
│ Save to Cache       │
└──────────┬──────────┘
           │
      ┌────┴────┐
      │ Error?  │──► Log & Continue (still return data)
      └────┬────┘
           │
           ↓
┌─────────────────────┐
│ Return Data         │
└─────────────────────┘
```

## Performance Characteristics

```
Operation              First Load    Cached Load
─────────────────────────────────────────────────
Initialize             ~100-200ms    N/A
Request Archive        ~20-50ms      ~1-5ms
Request Ref Table      ~30-100ms     ~1-5ms
Get Cache Stats        N/A           ~10-20ms
Clear Cache            N/A           ~50-100ms

Network Breakdown:
- WebSocket send:      ~1ms
- Server processing:   ~5-15ms
- WebSocket receive:   ~1-5ms
- Parsing:             ~1-2ms
- IndexedDB write:     ~2-10ms

Cache Breakdown:
- IndexedDB read:      ~1-3ms
- Data copying:        ~1-2ms
```

## Browser Compatibility Matrix

```
Feature           Chrome  Firefox  Safari  Edge
─────────────────────────────────────────────────
WebSocket         ✓ 16+   ✓ 11+   ✓ 6+    ✓ 12+
IndexedDB         ✓ 24+   ✓ 16+   ✓ 10+   ✓ 12+
ArrayBuffer       ✓ 7+    ✓ 4+    ✓ 5.1+  ✓ 12+
ES2020            ✓ 80+   ✓ 74+   ✓ 14+   ✓ 80+
Promises          ✓ 32+   ✓ 29+   ✓ 8+    ✓ 12+
async/await       ✓ 55+   ✓ 52+   ✓ 11+   ✓ 15+

Recommended: Latest versions of any modern browser
```

---

This architecture provides a robust, efficient, and scalable solution for loading game archives in the browser with automatic caching.

