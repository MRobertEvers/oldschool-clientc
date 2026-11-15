# torirsserver/wire

The server's wire layer: what bytes it writes to, and reads from, each client
revision. Shaped like `3rd/rsprot`: a revision-independent core, wrapped by one
directory per revision.

```
core/                        shared by every OSRS revision the server speaks (239..242)
  wire_in.c                    the canonical client->server bodies the world handlers read,
                               one writer per body, plus the bodies that never changed
  wire_player_info.c           PLAYER_INFO's GPI bit sections
  wire_if_inbound.c            IF_BUTTONX / IF_SUBOP / IF_SCRIPT_TRIGGER / dialog decoders
  wire_runclientscript.c       RUNCLIENTSCRIPT's argument block
  wire_facing.h                the actor facing block
  wire_appearance.h            the overhead-icon index
osrs230/                     the hybrid table this repo's own client speaks
  osrs230_wire.c               opcodes only; payloads are torirs_server_encode.c's
  packetin.h packetout.h       the prot tables (shared with the client)
osrs239/ osrs241/ osrs242/   one OSRS revision each
  osrs<N>_wire.c               its adapter: opcode tables and whole writer set
  osrs<N>_inbound.c            client->server: decode with rsprot's osrs<N> codecs,
                               hand the fields to core/wire_in.c
  osrs<N>_player_info.c        PLAYER_INFO's extended-info tail, and the entry point
                               that writes the core sections and then the tail
  packetin.h packetout.h zoneprot.h
```

`../torirs_server_wire.h` is the seam: a `ToriRSServerWire` per revision
(`ToriRSServer_Wire230()` .. `ToriRSServer_Wire242()`, or `ToriRSServer_WireByName`),
whose `translate_in` and `payload` slots point into these files. Nothing above
the seam asks which revision it is.

## Adding a revision

1. `osrs<N>/packetin.h`, `packetout.h`, `zoneprot.h` from the prot tables.
2. `osrs<N>_inbound.c`: copy the nearest revision's and change the codec
   suffixes (`packet_oploc1_v2_osrs<N>_in`); every rsprot packet header names
   its per-revision entry point. A field that moved is rsprot's concern, not
   this file's; a packet that changed MEANING gets a new writer in `core/wire_in.c`.
3. `osrs<N>_player_info.c`: the tail's flag bits, block order and transforms.
4. `osrs<N>_wire.c`: the writer set, then a `ToriRSServer_Wire<N>()` and a
   line in `ToriRSServer_WireByName`.
5. Add the files to `TORIRSSERVER_WIRE_SRCS` in `src/makefile`.

## Tests

`make -C src test-wire-player-info test-osrs241-player-info test-osrs242-player-info
test-wire-if-inbound test-wire-runclientscript test-osrs239-interface-setters
test-osrs239-varp test-loc-ops-wire`
