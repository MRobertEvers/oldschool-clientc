# rsprot (C)

A C port of the OSRS wire layer that [RSProt](https://github.com/2004Scape/rsprot)
and RSProx implement in Kotlin. It covers OSRS revisions 221-241 and the three
LostCity builds (rs245.2, rs254, rs289), and depends on
nothing outside this directory: no Kotlin, no JVM, no other library in this
tree. Programs that read or write the protocol depend on it: the 3draster
client and server, rsprox-index's native indexer, and the rsprox-c fork
through `ffi/`.

```sh
make -C 3rd/rsprot test            # unit tests
make -C 3rd/rsprot lib             # build/librsprot.a
make -C 3rd/rsprot ffi             # build/librsprot_ffi.{dylib,so,dll}
python3 3rd/rsprot/tools/rsprot_gen.py           # regenerate from spec/
python3 3rd/rsprot/tools/rsprot_gen.py --check   # exit 1 if anything is stale
```

## Layout

```
spec/                  the protocol as data, in INI -- the generator's only input
  packets/<file>.ini     a codec: message structs; per version, its reads in
                         wire order and the revisions it covers
  tables/<rev>.ini       revision <rev>'s prots: name, opcode, size, the version
                         each row calls where that is not its name
  streams/<stream>.ini   PLAYER_INFO / NPC_INFO / WORLDENTITY_INFO: each
                         version's constants shape, each revision's flag bits,
                         block order, field transforms and record readers
tools/rsprot_spec.py   reads spec/ (the INI dialect and every section is
                       documented there)
tools/rsprot_gen.py    spec/ -> the GENERATED files below
src/                   buffer, crypto (ISAAC, XTEA, RSA), Huffman / Base37,
                       the codec executor (rsprot_exec.c), codec storage, the
                       registry lookups
include/               the executor's vocabulary, the registry, codec tails
packets/               one codec per prot: generated from spec/packets, or
                       hand-written where a spec's reads cannot say the layout
streams/               the info streams: shared section and block readers
                       (hand-written) and per-version / per-revision files
                       (generated)
gen/                   the prot tables (generated)
ffi/                   a C ABI over all of it, for a foreign host
```

## Revisions

A revision is a family and a number: `osrs239` (Old School) or `rs254`,
`rs245.2` (the 2004-era RS2 builds LostCity serves). The two families' numbers
overlap, so nothing takes a bare number: the C type is `RsprotRev`
(`include/rsprot_rev.h`, `rsprot_osrs(239)`, `rsprot_rev_parse("rs245.2")`), and
a spec range names its family (`revs = osrs223-228, osrs230`). A range covers
the revisions between its ends that have a `spec/tables` file.

## How a revision reads a packet

Every revision calls a version of each packet: `gen/osrs<N>_prot.h` names the
prot and, where the codec is filed under another name, the version it calls
(`CAM_TARGET` at 223 calls `CAM_TARGET_V2`). Each packet header maps the
revision to its layout (`packet_<file>_osrs<N>_<dir>`), and the info streams
have an entry point per revision (`npc_info_small_v5_osrs239`) that calls its
version with that revision's constants. The rs family's streams are one shape
across three builds and are hand-written (`streams/info_rs.h`:
`player_info_rs254` calls `player_info_rs_v1`).

The info streams are buffer readers: each writes an op list
(`streams/rsprot_info.h`) carrying every value as sent, and the host applies
it.

## Changing the protocol

Edit the spec and run the generator; never hand-edit a file that says
GENERATED. A new revision is a new `spec/tables/<rev>.ini`, its versions'
`revs` in the packet specs, and its entry in each stream spec.

A packet spec is one `[layout]` per version, its reads in wire order:

```ini
[packet]
prot = CAM_TARGET_V2
dir = out

[struct:MsgCamTargetV2]
field = int32_t type
field = int32_t index
field = int32_t camera_locked_player_index

; v1
[layout]
revs = osrs223-228
U1 = type
U2_12 = index
U2_12 = camera_locked_player_index
```

A read's name is its bytes: `U2_12` is big-endian, `U2_21` little-endian,
`U4_3412` swaps the halves, `U2_12_ADD128` sends the low byte + 128, and
`U1_ADD128` / `U1_NEG` / `U1_SUB128` are the one-byte transforms
(`src/rsprot_buf.h` has the table, and the Kotlin `Alt` each replaces).

and a table row is `NAME = opcode size [the version it calls]`:

```ini
[server]
CAM_TARGET = 125 5 CAM_TARGET_V2
IF_SETTEXT = 13 VARU16
```
