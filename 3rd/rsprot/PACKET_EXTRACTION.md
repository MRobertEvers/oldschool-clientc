# Packet extraction

How to add an Old School revision to RSProt from a client binary, when no Kotlin RSProt or RSProx decoders exist for it. In practice that means the mobile clients (`osrsm<N>`, client types 7 and 8). Desktop revisions normally come from Kotlin RSProt instead; see [Desktop revisions](#desktop-revisions).

The worked example throughout is `osrsm241`, built from Android 241.4 (24104008). Its findings are in `osclient_decompile/osclient-241.4-android-arm64-2026-10-28-FINDINGS.md`, and its opcode table is the `.tsv` beside that file.

## What changes between revisions

Consecutive revisions are close in meaning and far apart in bytes. Measured on desktop 241 to 242:

| | 241 to 242 |
|---|---|
| Server prots | 160 to 162: adds `LOC_ADD_CHANGE_V3`, `SET_WEBSITE_TOKEN`; removes none |
| Client prots | 101 to 109: adds `OPLOC1-5_V3`, `OPLOC6_V2`, `OPLOCT_V2`, `OPLOCU_V2`; removes none |
| Sizes of the prots both have | none changed |
| Server opcodes unchanged | 9 of 160 |
| Client opcodes unchanged | 7 of 101 |
| `spec/packets` layouts present in both | 85 identical, 127 re-laid |
| Of the 127 re-laid | 125 have the same fields, in a new order or with new byte transforms |

The info streams shuffle too: from 241 to 242, nearly every player-info flag bit moved and the order of the extended blocks changed.

So treat a new revision as **the same packets and fields, renumbered and re-laid**:

- **Carry over:** which packets exist, each packet's size, its field names and meaning, its struct, and which handler does what.
- **Never carry over:** opcodes, field order, byte transforms, flag bits, block order, zone order. Each must be read from the new binary.
- **Mobile is not desktop.** At 241 only 17 of 160 server opcodes matched desktop, and layouts differ (`VARP_SMALL` is `U1_SUB128 value, U2_21 id` on desktop, `U1_NEG value, U2_12 id` on Android). A desktop port of the same revision tells you the packet set and nothing about the bytes.

A layout that is still correct by coincidence looks the same as one nobody checked. Trace every packet.

## Inputs

| Input | Where | Used for |
|---|---|---|
| Android bundle (`.apkm`, `.apks` or `.xapk`) for the new revision | `~/.rsprox/android/`, the newest file there is what the launcher uses | The binary |
| A recording from that client | RSProx, or the rsprox-index server | Validation; several packets can only be settled from traffic |
| Previous mobile decompile and findings | `osclient_decompile/` | **The named reference**, see step 4 |
| Desktop layouts and tables for the revision | `spec/packets`, `spec/tables/osrs<N>.ini` | Target packet set, field names, sizes |
| Rev 216 macOS decompile, with symbols | `osclient_decompile/osclient-216-mac.c` | Real names, for a packet with no earlier match |

Check the version first. The binary's revision must match the desktop revision whose packet set you are targeting. Live servers reject a mismatch, and a client between revisions makes every later step confusing.

## Steps

### 1. Decompile

```sh
unzip -o osrs-<ver>.apkm split_config.arm64_v8a.apk -d apk
unzip -o apk/split_config.arm64_v8a.apk lib/arm64-v8a/liblibs.hal.system.osclient.so -d apk
```

Import `liblibs.hal.system.osclient.so` into a new Ghidra project in `osclient_decompile` with `analyzeHeadless`, then export C with the C/C++ exporter (`CppExporter`). Give it plenty of heap and a long timeout (`MAXMEM=24G`, `-analysisTimeoutPerFile 14400`); the 241.4 import and export finished in under an hour. A function that hits the decompile timeout is left out of the export, so check that the dispatch is there. Name the output `osclient-<ver>-android-arm64-<date>.c`.

Ghidra loads the `.so` at `0x100000`, so link address `X` is `X + 0x100000` in the dump. The library is stripped. Grep the dump and read with `sed` ranges; it is about 50 MB.

### 2. Find the landmarks

Find these by signature, because their addresses move every build:

| Landmark | How to find it |
|---|---|
| `ServerConnection::Reader_Run` | References to the string `"ServerConnection Reader"` |
| `Writer_Run` | References to `"ServerConnection Writer"` |
| `ServerProt::SIZES` | The table `Reader_Run` indexes by opcode, reached through the GOT. Check where it starts: a first attempt at 241 landed 7 entries too early. |
| `ClientProt::SIZES` | The table `MakeClientMessage` indexes by opcode, also through the GOT |
| Server dispatch | The function with the most `switch` cases (`FUN_00541dd4` at 241, about 147 cases, 207 KB of C). Payload reads are inlined in it. |
| `MakeClientMessage` | The function that reads `ClientProt::SIZES`. Its call sites are the client packets. |
| ISAAC | Called from `Reader_Run` on the opcode byte; standard ISAAC |
| Zone packet dispatcher | The function that many dispatch cases call with a small constant id (`FUN_0056edb0` at 241) |
| Info decoders | Strings such as `"FATAL - WorldEntity update is bigger than expected"`, and the largest per-tick var-short cases |

Then note anything handled outside the dispatch. At 241 these were `PACKET_GROUP_START` (read by `Reader_Run`), `URL_OPEN` (payload ISAAC-deciphered byte by byte in `Reader_Run`), and three opcodes with no handler.

### 3. Check the sizes

Compare the multiset of new sizes with the desktop table for the same revision. It must match exactly, apart from zone-only prots (which have no top-level opcode) and any mobile-only client packets. At 241: 151 server opcodes plus 9 zone-only prots equals desktop's 160, and 102 client prots equals desktop's 101 plus `MOBILE_UNKNOWN_27`.

A mismatch means the table was misread, or the packet set changed. Resolve it before going on.

### 4. Name the opcodes

The handlers are the same code from one build to the next, so the previous mobile build is the best reference there is, far closer than 216.

1. Cut each dispatch case, plus up to 4 callees, into its own file.
2. Fingerprint each case: struct offsets, constants, called functions (by their own fingerprints), field widths, and string references. Leave out opcode numbers and Ghidra's `FUN_`/`local_` names.
3. Pair new cases with previous cases inside each size class. Report each pair with its score and its margin over the runner-up.
4. Packets the desktop diff says are new have no previous case. Name them by reading the handler against the desktop port of that revision.
5. Enforce one name per opcode, and a matching size.

Two things made this work at 242, and are worth repeating:

- **A binary-wide function map.** Pair functions across the two builds by normalised body (addresses, labels and locals removed), first exact, then with literals removed so moved struct offsets still match. Propagate through call lists of pairs. At 242 that mapped 13,716 of 21,246 functions, and comparing callees through the map separates cases that constants alone can't.
- **Client packets by function and position.** Pair the functions containing `MakeClientMessage` calls by the sequence of packet sizes they send plus their fingerprint, then line the calls up by position, since source order doesn't change between builds. That named 75 of 110 client opcodes.

A fingerprint pairing is a hypothesis. Small handlers that only store a byte or two score almost the same as each other: 242 got `CAM_MODE`/`CAM_UNLOCK`, `IF_SETNPCHEAD`/`IF_SETNPCHEAD_ACTIVE`, two clan-settings packets and the `OPLOC`/`OPOBJ` senders wrong. All of them were caught in step 5, by reading the 242 handler next to the 241 one and comparing what each does. Have that step confirm or reject every name.

If a case doesn't pair, these are the fallbacks:

- **Unique size**: a size only one packet has pins it (8 packets at 241).
- **Field widths**: a size plus field-width multiset matched 20 of 96 fixed-size packets to exactly one name. The rest sat in small groups with identical shapes.
- **Behaviour**: read the case next to the named 216 `Client::TcpIn` case: the callee (`Client::PlayJingle`, `IfType::Get`), the fields written, the loops.
- **Traffic**: rebuild first, varps after `VARP_RESET`, and per-tick packets.

Record a confidence and a line of evidence for each opcode, in a new `FINDINGS.md` and `.tsv`. Some packets stay uncertain. At 241 the three hide-ops flags are adjacent bits told apart only by send order, and opcodes with no handler were named by elimination. Say so in the table.

**Client packets** work the same way, starting from the call sites of `MakeClientMessage(&msg, opcode, cipher)`. At 241, 90 of 102 opcodes were constants at a call site. The other 12 go through 3 generic senders that pick the opcode from a table or parameter (the `OPNPC`/`OPLOC`/`OPOBJ`/`IF_BUTTON` families), so work out the mapping by hand. Client packets the build never sends can only be named by elimination.

### 5. Trace each layout

For every packet, read Android's own order and transforms from the handler. Reuse the field names from the desktop layout; never guess an order. Each read must trace to an offset in the code.

**Reading inlined reads** (arm64 is little-endian; `FUN_00b40c20()` is the host-endianness test at 241, and is always true at runtime):

| Decompiled | Read name |
|---|---|
| `*(ushort *)p`, swapped under the endianness test | `U2_12` |
| `*(ushort *)p`, no swap | `U2_21` |
| `b ^ 0x80` or `b - 0x80` | `U1_ADD128` |
| `-b` | `U1_NEG` |
| `0x80 - b` | `U1_SUB128` |
| Low byte has 128 subtracted, 2-byte big-endian | `U2_12_ADD128` |
| Same, little-endian | `U2_21_ADD128` |
| 4 bytes assembled by shifts | Read the order off the shifts: `U4_1234`, `U4_4321`, `U4_3412`, `U4_2143` (digit 1 is the value's most significant byte, in wire order) |
| 3 bytes | `U3_123`, `U3_321`, `U3_132`, `U3_213` |
| Combined interface id | `COM_<order>`, keeping the desktop's choice of `COM` or `U4` |

In the client direction the client **applies** the transform as it writes: `v + 0x80` is `ADD128`, `-v` is `NEG`, `0x80 - v` is `SUB128`.

Keep the desktop layout's signedness (`U` or `I`) for a field. When a handler stores raw values in a struct, follow the callee that consumes them to learn which field is which. For fixed-size packets the widths must sum to the size in the table.

A handler that ignores a field doesn't tell you its layout. `MIDI_JINGLE` reads only the id, and its length was guessed wrong at 241 until traffic showed it (see step 7).

**Where it goes:**

- Packets with a spec: append a `[layout]` block with `revs = osrsm<N>` to `spec/packets/<name>.ini`, reads in wire order. If an existing mobile layout is identical, add the revision to its `revs` line instead.
- Hand-written codecs in `packets/`: add `packet_<file>_osrsm<N>_<dir>` and a row in its `RsprotVersionRange` table, or point the row at an existing function if the layout is byte-for-byte the same. Add the `#define` alias to the header.

### 6. Tables, zone order, streams

- `spec/tables/osrsm<N>.ini`: `[server]` and `[client]` rows of `NAME = opcode size [version]`, with RSProt's names.
- `[zone_order]` in that file: the ids from the zone dispatcher. They are this build's, so read them from the code.
- `spec/streams/{player,npc,worldentity}_info.ini`: add `[osrsm<N>:...]` sections for flag bits, block order, per-field transforms and record order. World-entity spawn order and flag bits differed from desktop at 241. Copy the previous mobile section as the starting point, then re-check every value.
- `streams/rsprot_info_appearance.c`: maps a mobile revision to its appearance reader, and tests `revision == 241` today. Extend it.
- `src/rsprot_storage.c`: a new prot with caller-owned arrays needs a pool here, or decoding it crashes (this is what segfaulted `LOC_ADD_CHANGE_V3`).

Then:

```sh
python3 3rd/rsprot/tools/rsprot_gen.py
python3 3rd/rsprot/tools/rsprot_gen.py --check
```

### 7. Validate on a real recording

A clean build proves little, because a wrong transform still decodes. Record a session that walks, opens the bank and interfaces, chats and changes music, and then:

1. **Framing:** every packet in the session parses, with no unknown opcodes and no desync.
2. **No leftovers:** index it with `RSPROX_INDEXER_STRICT=1`, which fails any codec that stops short of the end of its payload. `RSPROX_INDEXER_DUMP=1` prints every payload that failed.
3. **Compare with desktop:** take a desktop recording of the same revision and the same activity, on the same world if possible (Tutorial Island works well). Many packets should carry the same values: stats, dialogue text, interfaces, varps, objects, music. At 241 this is what caught `MIDI_JINGLE`'s `U3_132` length.
4. **Explain each difference.** Real ones exist: player and NPC indexes, ping tokens, screen size. A value out of its range (camera pitch, coordinates) means a wrong layout.
5. **Replay it** in the 3draster client.

List what the recording could not confirm, such as packets never sent or flags that were always 0.

## Wiring up a new mobile revision

| Repo | Change |
|---|---|
| RSProt | Steps 5 and 6; update the supported list in `README.md` |
| rsprox | `AndroidProtocol.kt`: add `REVISION_<N>` with both size arrays and the `encryptedServerOpcodes`, and add it to `providers()`. `AndroidClientLauncher`: re-check the library patch against the new binary. |
| rsprox-c | `scripts/sync_rsprot.sh <3draster>`, then re-check `AndroidClientLauncher`'s library patch. Opcodes, sizes and revisions come from RSProt through the FFI, so there are no tables to add here. |
| 3draster | `[osrsm<N>]` in `src/net/rev/revisions.ini`, then `tools/rev_exec_gen.py` for `src/net/rev/osrsm<N>/`; add any `prot_names.ini` and `proto_adapt` entries for new prots |
| rsprox-index | `scripts/sync_rsprot.sh <3draster>`, `make -C native`, new prots in `native/indexer/index_packets.h`, then `scripts/compare_native.py` on stored sessions. It selects `osrsm` from the client type on its own. |

The sync scripts copy committed code only, so commit RSProt first.

## Desktop revisions

Kotlin RSProt publishes each desktop revision as source, so no binary is needed. Diff the `osrs-<N-1>` and `osrs-<N>` protocol trees; a packet whose codec is unchanged just gets the new revision added to its `revs` line. Otherwise translate the Kotlin buffer calls:

| Kotlin | Plain | `Alt1` | `Alt2` | `Alt3` |
|---|---|---|---|---|
| `g1` | `U1` | `ADD128` | `NEG` | `SUB128` |
| `g2` | `U2_12` | `U2_21` | `U2_12_ADD128` | `U2_21_ADD128` |
| `g3` | `U3_123` | `U3_321` | `U3_132` | `U3_213` |
| `g4`, `gCombinedId` | `1234` | `4321` | `3412` | `2143` |

Check every codec, and read the op-family packets (`OPLOC`, `OPNPC` and so on) individually since new versions tend to appear there. Also port RSProx's decoder fixes for that revision, then re-index stored sessions to show nothing moved.
