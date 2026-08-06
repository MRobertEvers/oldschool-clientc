# rsprot (C port)

A C port of [RSProt](https://github.com/2004Scape/rsprot)'s wire layer — the
buffer semantics, crypto adapters, chat compression codecs, and per-revision
protocol tables that a RuneScape server/client speak. Unity build, single
public header (`include/rsprot.h`), standalone `makefile` in this directory
plus (once wired in — see "Status" below) linkage through `src/Makefile` the
same way `3rd/rscache` is built.

```sh
make -C 3rd/rsprot            # compile the unity TU (warnings on)
make -C 3rd/rsprot test       # unit tests
```

## Why this exists

`src/net/rev/osrs230/` is a **hybrid**: roughly a third of its opcodes were
assigned by this project rather than transcribed from RSProt, and its
PLAYER_INFO/NPC_INFO streams are bound to the classic lc254 bitstreams instead
of the v5 streams the real revision carries. It is what this project's own
client and test suite run on, and it can only ever talk to itself.

`src/net/rev/osrs239/` is transcribed from RSProt, but by hand, field at a
time, against the Kotlin source read in an editor. `docs/RSPROT_OSRS239_PORT.md`
§5a documents three wrong writers that transcription caught only because
someone checked byte-for-byte; a generator that reads the same Kotlin and
never gets tired is a stronger guarantee than a careful reading, and it scales
to every revision RSProt vendors instead of the one this project has hand-
copied so far.

This library is that generator's *target*: a C runtime shaped so a codec
transcribed (by hand or generated) from RSProt's Kotlin reads the same
regardless of which language it is in — same function names (`g1`, `p2Alt3`,
`pSmart1or2`, …), same semantics, same edge cases.

## Layout

```
3rd/rsprot/
  include/rsprot.h        single public header
  src/
    rsprot_buf.{h,c}       JagByteBuf port: byte/bit access, all Alt1-3 orders,
                            smart/varint encodings, strings, CRC-32
    rsprot_crypto.{h,c}    ISAAC / XTEA / RSA — ADAPTER over src/net/isaac.c,
                            3rd/xteas, src/net/rsa.c (see "What's reused" below)
    rsprot_compression.{h,c}  Huffman chat codec + Base37 name codec — full port
    rsprot_tables_def.h    shared types for the generated per-revision tables
  gen/
    rev<NNN>_prot.h         one file per vendored revision (221..239): every
                            prot's {name, opcode, size}, GENERATED
    rsprot_tables.h         umbrella + rsprot_rev_table(revision) lookup, GENERATED
  test/                    test_buf.c, test_crypto_compression.c, test_tables.c
  rsprot_unity.c           unity build anchor
  makefile                 standalone build (see 3rd/rscache/makefile for the
                            pattern this follows)
```

Regenerate the tables after RSProt updates its vendored source
(`~/Documents/git_repos/rsprot`):

```sh
tools/rsprot_gen_tables.py           # all 19 revisions
tools/rsprot_gen_tables.py 239       # one revision
```

## What's reused instead of re-ported

RSProt's `crypto` module (ISAAC stream cipher, XTEA, RSA modPow) is the same
three algorithms this project already carries: `src/net/isaac.c` (byte-
identical scramble/init to `IsaacRandom.kt` — both are transcribed from
OpenRS2), `3rd/xteas/xteas.c` (same delta/rounds/key schedule as
`XteaByteBufExt.kt`), and `src/net/rsa.c` over `3rd/tommath` (same modPow
operation as `Rsa.kt`'s BigInteger/JNA-GMP path). `rsprot_crypto.h` gives
these RSProt's own names and call shapes (`StreamCipher`/`StreamCipherPair` →
`RsprotCipher`/`RsprotCipherPair`) rather than duplicating the primitives — a
security algorithm is the one thing worth never copying twice.

RSProt's `compression` module is **not** bzip/gzip (this project already has
those via `3rd/bzip` and `3rd/miniz` for cache archives, untouched here) — it
is the client chat Huffman codec and Base37 name encoding. Those are ported in
full (`rsprot_compression.c`), because the existing `src/net/jbase37.c` does
not replicate RSProt's specific truncation and case-restoration behaviour and
a ~300-line algorithm was cheaper to port correctly than to audit for
equivalence.

## Status

**Done, tested, verified against `docs/RSPROT_OSRS239_PORT.md`'s documented
facts:**

- `rsprot_buf` — full port of `JagByteBuf` + `JagexByteBufExtensions.kt`:
  every byte/short/medium/int width in every Alt1-3 order, both smart
  encodings, MIDI varlen, LEB128 varint, the 1-or-2-byte type encoding,
  strings (`jstr`/`jstrnull`/`jstr2`), bulk data in all four byte orders,
  combined-id packing, the bit-cursor mode (`BitBuf`/`WrappedBitBuf`), and
  CRC-32. Errors are **sticky** rather than thrown (`RsprotBuf.err`), since a C
  decoder cannot unwind a Kotlin-style `require()` — see the header for why
  that is a different contract than the original, not an incomplete one.
- `rsprot_crypto` — ISAAC/XTEA/RSA adapters, tested for cross-implementation
  agreement (two independently-seeded ISAAC streams matching) and a textbook
  RSA modPow fixture (65^17 mod 3233 = 2790).
- `rsprot_compression` — Huffman codec (table build + encode + decode) and
  Base37 (encode/decode/decodeWithCase, including the underscore→NBSP
  case-restoration rule), full round-trip tested.
- Per-revision prot tables — **all 19 vendored revisions** (221..239),
  generated from RSProt's own Kotlin, cross-checked in `test_tables.c` against
  facts independently measured and written down in
  `docs/RSPROT_OSRS239_PORT.md` before this library existed (rev239's highest
  server opcode is 148; `IF_OPENTOP` is opcode 96, 2-byte payload; `IF_SETEVENTS_V2`
  is 16 bytes) — the tables agreeing with a doc nobody updated for this
  purpose is closer to independent verification than a self-consistency check
  would be.

**Not done — this is the large remaining slice, scoped deliberately narrower
than "port all of RSProt" for one pass:**

- **Message structs, encoders, decoders.** RSProt's osrs-239 module alone is
  ~197 encoders + ~95 decoders (`osrs-239-desktop`, ~10k lines) plus ~376
  message-class files (`osrs-239-model`, ~38k lines). None of that is
  generated or hand-ported yet. The prot tables above give you *only*
  `{name, opcode, size}` — not the field layout inside a payload. Building the
  codec generator (parse an `Encoder.kt`'s `encode()` body → an
  `rsprot_buf`-shaped C function) is the next real chunk of work, and it is
  naturally scoped **per revision this project actually speaks** (239 first)
  rather than all 19 — the tables are cheap because they are pure data; the
  codec bodies are not, and generating 19 revisions × ~300 packets of C nobody
  will compile is not a good trade against generating 239's now and re-running
  the same generator later when another revision is needed.
- **Info streams** (`PLAYER_INFO`/`NPC_INFO` v5, `WorldEntityInfo`, the
  extended-info blocks — appearance, movement, hits, etc.). This is the
  hardest and highest-value slice: `docs/RSPROT_OSRS239_PORT.md` §5c-6
  documents that this project's own `mock239_playerinfo.c` already implements
  a hand-written PLAYER_INFO v5 encoder (verified against RSProt's *decoder*,
  not its encoder, deliberately), and NPC_INFO v5 is not written at all. This
  library does not yet touch that area; when it does, `mock239_playerinfo.c`
  is the thing to fold *into* it, not duplicate.
- **Login/JS5 message shapes** — the login block layout
  (`src/net/rev/osrs239/loginblock.h`) and JS5 protocol are hand-transcribed
  in this project already and out of scope for this pass.

## Relationship to the hand-written wire code (deliberately untouched)

Per the scoping decision this library was built under: it is **standalone**.
`mock230_wire.c`, `src/net/rev/osrs239/`, and `3rd/rsareabuf` are not modified
and do not depend on this library yet. Wiring osrs239's ten hand-transcribed
payload writers onto `rsprot_buf` (proving the buffer port against a real
client, per the existing `MOCK230_REV=osrs239` selftest path) is a natural
next step but a separate one — it changes behavior on a path the regression
suite runs, where this library so far only adds new files.
