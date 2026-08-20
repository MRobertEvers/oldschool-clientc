# Buffer accessors — the full inventory, and a byte-order-explicit naming

Every `gX`/`pX` buffer accessor defined anywhere in this tree, where it lives,
and what its name should be once naming states the byte order instead of an
opaque `Alt` ordinal.

Produced by walking every `.c`/`.h` under each area for accessor *definitions*
(functions and macros, `static` included) — not calls. Re-run it rather than
trusting the counts below:

```sh
python3 tools/audit_buffer_accessors.py
```

## 1. The inventory

| area | definitions | distinct names | files | how many are `static` |
|---|---|---|---|---|
| `src/net` | 40 | 39 | 8 | **40 — all of them** |
| `3rd/rscache` | 26 | 26 | 2 | 2 |
| `3rd/rsprot` | 96 | 96 | 1 | 0 |
| `3rd/rsareabuf` | 44 | 44 | 1 | 0 |
| `v0` | 17 | 17 | 3 | 2 |
| **total** | **223** | | | |

`src/serverscript`, `src/engine`, `src/cs2vm2` and `3rd/trspk` define none.

### 1.1 `src/net` — 40 accessors, every one file-local

This is the finding. The net stack has no buffer library; it has eight private
ones.

| file | count | names |
|---|---|---|
| `net/rev/osrs239/osrs239_parse.c` | 17 | `g1` `g1_alt1` `g1_alt2` `g1_alt3` `g1b` `g1s` `g2` `g2_alt1` `g2_alt2` `g2_alt3` `g3` `g3_alt2` `g4` `g4_alt1` `g4_alt2` `g4_alt3` `gjstr_nul` `gsmart1or2` |
| `net/net_out.c` | 10 | `out_p1_alt1..3` `out_p2_alt1..3` `out_p4_alt1..3` `out_p8` |
| `net/rev/osrs230/osrs230_parse.c` | 6 | `c_g1` `c_g1alt2` `c_g2` `c_g4` `c_g4alt1` `c_gsmart` |
| `net/rev/packets/pkt_player_appearance.c` | 2 | (local `g`-helpers) |
| `torirsserver/torirs_server_wire.c` | 2 | `w239_p3_alt2` `w239_psmart1or2` |
| `torirsserver/torirs_server_encode.c` | 1 | `ext_psmart1or2` |
| `torirsserver/mock239_playerinfo.c` | 1 | `v5_psmart1or2` |
| `torirsserver/mock239_interface_inbound.c` | 1 | `anim_g2` |

**The same smart-encoding is written five times** under five names —
`gsmart1or2`, `c_gsmart`, `ext_psmart1or2`, `v5_psmart1or2`,
`w239_psmart1or2` — each private to its file, none tested against the others.
That is the concrete cost of there being no `RSProt_Buffer`: not verbosity, but
five independent chances to get one variable-width encoding wrong.

### 1.2 `3rd/rscache` — 24 in the library, 2 strays

`src/rsbuffer.h` holds the real API (`RSCache_BufferG1`, `RSCache_BufferP1`,
`RSCache_BufferG1b`, … 24 of them). Two escaped into a decoder:

- `3rd/rscache/src/datatypes/dat2_config_loc.c` — `gstringfl`, `loc_pstringfl`

Those two belong in `rsbuffer.h`. `v0/osrs/rscache/dat2a/dat2a_config_locs.c`
carries the same `gstringfl` stray, which is where it was copied from.

## 2. The naming problem `Alt` hides

The rule asked for is: state the byte order in the name, `_be`/`_le` when the
order is standard. That works cleanly for the wide widths — and does not
survive contact with the narrow ones, because **`Alt` is not one concept.**

Reading the implementations (`3rd/rsprot/src/rsprot_buf.c`), numbering bytes by
significance with 1 = most significant:

### Width 3 and 4 — pure byte permutation. The rule works.

| current | bytes written | order | new name |
|---|---|---|---|
| `p4` | `v>>24, v>>16, v>>8, v` | 1,2,3,4 | `p4_be` |
| `p4_alt1` | `v, v>>8, v>>16, v>>24` | 4,3,2,1 | `p4_le` |
| `p4_alt2` | `v>>8, v, v>>24, v>>16` | 3,4,1,2 | `p4_3412` |
| `p4_alt3` | `v>>16, v>>24, v, v>>8` | 2,1,4,3 | `p4_2143` |
| `p3` | `v>>16, v>>8, v` | 1,2,3 | `p3_be` |
| `p3_alt1` | `v, v>>8, v>>16` | 3,2,1 | `p3_le` |
| `p3_alt2` | `v>>16, v, v>>8` | 1,3,2 | `p3_132` |
| `p3_alt3` | `v>>8, v>>16, v` | 2,1,3 | `p3_213` |

### Width 1 — pure value transform. There is no order.

| current | writes | new name |
|---|---|---|
| `p1` | `v` | `p1` |
| `p1_alt1` | `v + 128` | `p1_add128` |
| `p1_alt2` | `-v` | `p1_neg` |
| `p1_alt3` | `128 - v` | `p1_sub128` |

A one-byte field has one order. Naming these `_be` would make four different
encodings share a name.

### Width 2 — **both at once**, which is the trap.

| current | bytes written | order | transform | new name |
|---|---|---|---|---|
| `p2` | `v>>8, v` | 1,2 | none | `p2_be` |
| `p2_alt1` | `v, v>>8` | 2,1 | none | `p2_le` |
| `p2_alt2` | `v>>8, v+128` | 1,2 | low byte +128 | `p2_be_add128` |
| `p2_alt3` | `v+128, v>>8` | 2,1 | low byte +128 | `p2_le_add128` |

**A byte-order-only name would give `p2` and `p2_alt2` the same name**, and
they are different bytes. This is exactly the failure the rename is meant to
prevent — `docs/RSPROT_OSRS239_PORT.md` §5a's "frames perfectly, means
something else" — so the scheme has to carry the transform too.

### The scheme, stated

```
<g|p><width>[_<order>][_<transform>]
```

- `<order>` — `be` when bytes ascend by significance, `le` when they descend,
  otherwise the explicit digit string (`3412`). Omitted at width 1.
- `<transform>` — `add128` / `sub128` / `neg`, naming what is applied to the
  byte that carries it (always the least-significant byte in the 1- and 2-byte
  forms). Omitted when the value passes through.

Both parts are derived from the implementation, so a name can be checked
against the code mechanically rather than believed.

## 3. The libraries, built

`3rd/rsprot/src/rsprot_buffer.h` — `RSProt_Buffer`, the net-stack counterpart
to `RSCache_Buffer`. ~110 accessors under the scheme above. It is a typedef
over `RsprotBuf`, not a second copy: one cursor, one error latch, one bit-mode
implementation, two vocabularies over it. The `Alt`-spelled names in
`rsprot_buf.h` stay, because a codec transcribed from RSProt's Kotlin should
read line-for-line against its original; they now forward here.

`3rd/rsareabuf/rsareabuf.h` — the same names added as aliases. Every one was
checked against its `put()` sequence before aliasing: rsareabuf's `_alt1/2/3`
agree with rsprot's shift for shift at all four widths.

**`3rd/rsprot/test/test_buffer_names.c` is what makes the names worth having.**
It does not round-trip — a round trip passes for any self-consistent encoding,
including one whose name is a lie. It pins the literal bytes implied by each
name (`p4_3412` on `0x12345678` must emit `56 78 12 34`), then separately
checks each reader inverts its writer. Verified to fail: pointing `P4_3412` at
`p4_alt3` produces two immediate failures naming both halves.

## 4. Migration result

**`src/net`: 40 accessors in 8 files → 4 in 3.** Re-run
`tools/audit_buffer_accessors.py` to confirm.

| file | was | now |
|---|---|---|
| `net/rev/osrs239/osrs239_parse.c` | 17 | 1 |
| `net/net_out.c` | 10 | 1 |
| `net/rev/osrs230/osrs230_parse.c` | 6 | 0 |
| `net/rev/packets/pkt_player_appearance.c` | 2 | 2 |
| `torirsserver/torirs_server_wire.c` | 2 | 0 |
| `torirsserver/torirs_server_encode.c` | 1 | 0 |
| `torirsserver/mock239_playerinfo.c` | 1 | 0 |
| `torirsserver/mock239_interface_inbound.c` | 1 | 0 |

Two private cursors are gone — `struct Cur` (osrs239) and
`struct Osrs230Cursor` — replaced by `RSProt_Buffer`. All five copies of
pSmart1or2 are now one call. Three files also had byte orders written out
*inline* rather than as functions, so the audit never counted them:
`mock239_interface_inbound.c` open-coded p2Alt3, a big-endian u16, a
little-endian u32 and an 8-byte big-endian read against a raw pointer with no
cursor at all; those are now cursor reads that latch an error on a short
payload instead of running off the end.

### What deliberately stayed, and why

Four definitions remain and none is a duplicated byte accessor:

- `anim_g2` (`pkt_player_appearance.c`) — reads a u16 *and applies the
  wire's sentinel*: 65535 means "no animation", returned as -1. That rule is
  appearance-block knowledge, not a byte order.
- `g1s` (`pkt_player_appearance.c`) — now `#define g1s(b) ((int)g1b(b))`, a
  name over rsbuffer's own signed-byte reader rather than a second
  sign-extension.
- `out_p_com` (`net_out.c`) — writes a component id as 2 or 4 bytes depending
  on the revision. A dispatch over two library calls.
- `gjstr_nul` (`osrs239_parse.c`) — returns a **malloc'd** string, because
  `RevPacket` owns its strings and `gameproto_free` frees them, while
  RSProt_Buffer's readers borrow into the payload (`rsprot_exec.h`: "a copy per
  string per tick is a cost with no payer"). Handing a borrow to a `RevPacket`
  would free a pointer into a network buffer. This is an ownership adapter, and
  the one contract difference between the two buffer libraries worth knowing
  before moving code between them.

`3rd/rscache/src/datatypes/dat2_config_loc.c`'s `gstringfl` / `loc_pstringfl`
also stay. They are **not** duplicates: they choose between `gstringnewline`
and `gcstring` — both already rsbuffer's — on a `RSCACHE_CONFIG_LOC_DECODE_DAT`
flag. The primitive is in the library already; what is local is the era choice,
which is loc-config knowledge. Moving it into the buffer would put cache-config
flags inside a byte cursor.

### One structural fix this forced

`rsprot_buf.c` is no longer part of `rsprot_unity.c`. It is the only
dependency-free piece of the library and the piece everything wants, but while
it shared a TU with `rsprot_crypto.c` it dragged ISAAC, RSA and tommath onto
every link — so a test that decodes two bytes failed to link against a bignum
library, three separate times. It is now its own object.

## 5. Original plan (superseded by §4)

| | accessors | files |
|---|---|---|
| `src/net` at audit time | 40 | 8 |
| `src/net` now | **38** | **7** |

Done — `src/torirsserver/torirs_server_wire.c`, now clean:

- `w239_psmart1or2` → `rsab_psmart`. Byte-identical in range, and the library
  version *latches an overflow* where the copy silently truncated.
- `w239_p3_alt2` → `rsab_p3_132`. A comment claimed rsareabuf "has p3 but not
  this permutation, so it lives here rather than widening the buffer library" —
  `rsab_p3_alt2` had that exact body all along. The copy was byte-identical, so
  nothing ever failed, which is how a duplicate survives.

Remaining, largest first:

| file | accessors | notes |
|---|---|---|
| `net/rev/osrs239/osrs239_parse.c` | 17 | **257 call sites**, and a private `struct Cur` to swap for `RSProt_Buffer`. Covered by `test-rsprot-bridge` (8 packets × 32 seeds) and `test-mock239-inbound` (57 checks), so a mistake here is caught. |
| `net/net_out.c` | 10 | `out_p*` writers |
| `net/rev/osrs230/osrs230_parse.c` | 6 | `c_g*`, own cursor |
| `net/rev/packets/pkt_player_appearance.c` | 2 | |
| `torirsserver/torirs_server_encode.c` | 1 | `ext_psmart1or2` → `rsab_psmart` |
| `torirsserver/mock239_playerinfo.c` | 1 | `v5_psmart1or2` → `rsab_psmart` |
| `torirsserver/mock239_interface_inbound.c` | 1 | `anim_g2` |
| `3rd/rscache/.../dat2_config_loc.c` | 2 | `gstringfl`, `loc_pstringfl` → `rsbuffer.h` |

Three of the five duplicate smart encoders are still out there
(`ext_psmart1or2`, `v5_psmart1or2`, and `gsmart1or2` in `osrs239_parse.c`); all
three collapse onto `rsab_psmart` or `RSProt_BufferGSmart1or2`.
