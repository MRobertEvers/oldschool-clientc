# The RSProt / OSRS-239 protocol port

> Written 2026-08-04. Companion to [`MULTI_GENERATIONAL_PARITY.md`](MULTI_GENERATIONAL_PARITY.md),
> which owns the net-stack seams. This doc owns one question: **what does it
> take for an unmodified OldSchool client to talk to this server**, and how far
> along that is.

## 0. The one-paragraph state

The revision-239 wire tables, the framing change they need, the login block and
the JS5 (cache download) service are implemented and measured. A real RuneLite
was launched against them and its vanilla client **downloaded 2,733 cache
groups / 7.9 MB from this server** before stopping. It stopped for a reason
outside this repo: RuneLite's shipped client is revision **240**, and both
RSProt's newest vendored protocol and every archived cache are **239**. The
game-protocol half (the mock server speaking 239, and the v5 entity streams)
is not done — see §6.

## 1. Why "the latest rsprot protocol" is a different thing from `osrs230`

`src/net/rev/osrs230/` is a **hybrid, by design and by its own admission**.
Roughly a third of its opcodes were assigned by this project rather than
transcribed, because RSProt's tables were not vendored here; and its
PLAYER_INFO / NPC_INFO opcodes are bound to the classic lc254 bitstreams rather
than the v5 streams revision 230 actually carries. That pairing is
self-consistent, it is what the whole regression suite runs on, and it can only
ever talk to this project's own client.

`src/net/rev/osrs239/` is the first table in the tree where **every number is
real**. That is the entire reason it exists.

## 2. The tables are generated, not transcribed

```sh
tools/rsprot_dump_prot.py 239                       # RSProt Kotlin -> JSON
tools/rsprot_gen_rev.py 239 --out src/net/rev/osrs239
```

produces `packetin.h` (158 server prots), `packetout.h` (101 client prots) and
`zoneprot.h`. A revision bump is a re-run, not a re-transcription.

Two things the dumper learned the hard way, both now hard failures rather than
shorter tables:

- sizes appear both symbolically (`Prot.VAR_SHORT`) and as the bare int
  (`-2`), and a pattern that only accepted `[\w.]+` silently dropped every
  literal-negative row — `LOC_ADD_CHANGE_V2` among them;
- ktlint wraps long entries over four lines, which dropped one more.

The generator now asserts parsed-row-count against a count of enum entries and
names what it missed.

The only hand-written part is `CANON_IN` / `CANON_OUT` in the generator: which
RSProt prot answers to which of this engine's canonical names. A mapping there
is a claim that the **payload** matches, and a `_V<n>` suffix in RSProt is the
revision telling you the layout moved. Check the encoder before carrying a
mapping forward.

## 3. What actually changed 230 → 239

| | 230 | 239 |
|---|---|---|
| opcode encoding | one byte | `pSmart1Or2` — two cipher bytes once the opcode reaches `0x80` |
| highest server opcode | 127 | **148** |
| login header | version, subVersion, clientType… | plus `serverVersion` (new at 237) |
| login body | username + password, and that is all | RSA envelope + **XTEA body**: window state, uuid, host platform stats, 23 archive CRCs |
| proof of work | none | server may interpose a SHA-256 challenge |
| REBUILD_NORMAL | carries the XTEA key array | **V2 drops it entirely** (maps are stored plain at ≥237) and reorders the rest |
| IF_SETEVENTS | one 32-bit mask, 12 bytes | **two** masks, 16 bytes, different field order |
| UPDATE_PID | present | **gone** — the local index rides in the login response |
| zone sub-packets | addressed by top-level opcode | addressed by an **enum ordinal** that is neither the opcode nor the prot id |

The opcode change is the one that is not survivable if missed. A revision whose
table passes 127 and leaves `opcode_smart2` at 0 does not drop the high
packets — it reads each one's second byte as the next opcode and never
resynchronises. `packetbuffer.c` parks in `PKTBUF_READ_OPCODE_LOW` rather than
reading ahead, because the first byte's ISAAC step cannot be replayed.

## 4. JS5, and how its unknown was settled

JS5 is not a separate server: a client opens one socket and picks with its
first byte (`14` game login, `15` JS5). A vanilla client will not reach the
login screen without it, which is why a private server that implements only the
game protocol shows a client that never starts.

The service serves the archive **exactly as it sits on disk**, minus the 2-byte
version trailer — not a decode-and-re-encode. `src/net/mock/mock_js5.c` reads
`.idx`/`.dat2` sectors itself for that reason, and checks all four sector-header
self-check fields so a chain that drifts is caught rather than returning a
plausible mixture of two archives.

**The master index (255/255) is the one response no cache stores** — the server
computes it. Rather than guess its layout, `tools/js5_probe.py` asked a live
revision-239 server:

```
$ tools/js5_probe.py --host oldschool1.runescape.com --rev 239 --archive 255 --group 255
  compression 0, container 205 bytes
  master index body: 200 bytes
    reads as 25 archives of (p4 crc, p4 version)
```

No format byte, no count prefix; absent archives (16 and 23 on live) keep their
slot as a `(0, 0)` pair, so the count is *highest index present + 1*. Our
builder reproduces that shape exactly, and **archives 11, 14 and 15 come out
with CRCs identical to the live server's** — those three had not changed between
the archived snapshot and live, which independently validates the CRC
computation rather than just the layout.

That probe also validated the whole JS5 handshake end to end, since it is the
same code path a client uses.

## 5. Connecting RuneLite — what has to give, and what does not

Two things stop a stock RuneLite, and only one is a setting.

**Where it connects** is a setting: RuneLite already takes `--jav_config=<url>`,
and the client derives the game host from that config's `codebase`.
`tools/torirs_javconfig.py` serves one.

**Whose key it encrypts the login block with** is not. The RSA modulus is
compiled into the client, so without replacing it the login block is readable
only by Jagex. `tools/runelite_patch.py` replaces it. It finds the constant
*structurally* — the 256-character lowercase-hex `CONSTANT_Utf8` in the same
class as the exponent `10001` — because the obfuscated class name changes every
revision (it is `bq.class` in 1.12.34.1). Both moduli are 1024-bit, so that
edit is a byte-for-byte overwrite.

Three further obstacles surfaced only by running it, each with a one-line fix:

1. `--developer-mode` without `-ea` is a **fatal error dialog**, not a warning.
2. RuneLite rejects a jav_config whose host does not end in `.jagex.com` or
   `.runescape.com`. The patcher rewrites the first suffix to `.0.0.1`, which a
   loopback address ends with, and leaves the second alone so a patched client
   still accepts the real config. This one is not same-length, so it rewrites
   the constant's `u2` length too — safe because a class file addresses
   constants by pool index, never by byte offset.
3. `client-*.jar` is **signed**. Editing a class in it yields
   `SecurityException: SHA-256 digest error` at class-load time — and the
   manifest's per-entry digests must go along with `META-INF/*.SF|RSA`, because
   the JVM checks the digest even when no signature file survives.

A fourth was mine: leaving two `client-*.jar` versions on the classpath silently
ran the older one.

## 5a. The wire adapter — how the server chooses a revision

`src/net/mock/mock230_wire.h` is the fourth vtable seam in the net stack and is
deliberately shaped like the other three: `Mock230Transport` (where the bytes
go), `NetLoginVTable` (how the handshake runs), `GameProtoRevTable` (what the
client reads). Struct of function pointers, one instance per implementation, a
`_by_name` resolver, NULL slots meaning "classic".

```sh
src/build/mock230 43594 --rev osrs239     # or MOCK230_REV=osrs239
```

Default is `osrs230`, so everything that says nothing behaves exactly as before.

**Why an enum could not stay.** `mock230_encode.c` held 50 wire opcodes as
literals. That hid three things the moment there were two revisions: opcodes
move (IF_OPENTOP is 60 at 230, 96 at 239); opcodes *disappear* (there is no
UPDATE_PID and no P_COUNTDIALOG at 239, and a literal cannot express absent);
and **payloads move even when the size does not**. That last one is the
expensive one — IF_OPENTOP is a 2-byte interface id at both revisions, written
`p2Alt1` at one and `p2Alt2` at the other, and IF_OPENSUB is 7 bytes at both
with its three fields in the opposite order. Such a packet frames perfectly,
passes every length assert, and arrives meaning something else.

So `payload` is a *whole* writer set, not a sparse list of overrides: a packet
the revision's set does not name is **refused**, not written with the other
revision's layout. Refusing is visible; a wrong layout is not. Same choice
`osrs239_parse.c` makes on the way in.

Three details worth carrying forward:

- **The refactor is checkable, not merely plausible.** The `OP_*` enum became
  aliases for canonical names, so all 140 call sites are untouched;
  `mock230_send` resolves the name and records the **resolved wire opcode** in
  the packet capture. The selftest's assertions are written against wire
  numbers (120, 108, 90, 35, 94) and still mean what they meant. Measured: the
  same 13 pre-existing failures before and after, no new ones.
- **Zone sub-packets are a separate question from opcodes.** Inside
  UPDATE_ZONE_PARTIAL_ENCLOSED, 230 uses the top-level opcode and 239 uses the
  ordinal of RSProt's `IndexedZoneProtEncoder` — a third numbering, neither the
  opcode nor RSProt's own `OldSchoolZoneProt` id. Missing this is invisible at
  the frame level (the blob is length-prefixed as a whole) and it is exactly
  what four of the six transient selftest failures during this work were.
- **Transcribe each writer from its own encoder; never infer one from its
  neighbour.** A first pass wrote `p2Alt1` for VARP_SMALL's id and put
  VARP_LARGE's value before its id. Both plausible, both wrong, neither
  detectable downstream — every varp write would have landed on a different
  varp at full speed with no error. Checking against RSProt found three of six
  wrong, UPDATE_STAT_V2 being reordered entirely.

JS5 also moved into `mock230_session`'s handshake, where it belongs: one socket,
opcode 14 game vs 15 JS5. `MOCK230_JS5_REV` sets the revision to accept and
`MOCK230_JS5_CACHE` the directory. The standalone `mock-js5` binary stays as a
test fixture.

That integration immediately exposed a real bug it had been masking:
**mock230 did not ignore SIGPIPE.** It barely mattered while every packet was a
few hundred bytes, but a cache download is megabytes in a tight loop, and a
client closing its update connection the moment it has what it wants — the
normal way that connection ends — killed the process mid-write. Exit code 141,
which from outside is indistinguishable from a crash.

## 5b. The rev-239 handshake, verified end to end

The client's login driver and the server's block reader are written from ONE
statement of the layout (`src/net/rev/osrs239/loginblock.h`). This is what
checks that they agree:

```sh
src/build/mock230 43596 --rev osrs239 &
src/torirs --manifest manifest_osrs239_net.ini --user testc --pass test
```

```
mock230: login user='testc' session=ok
mock230: -> IF_OPENTOP       op=96  payload=2
mock230: -> IF_OPENSUB       op=7   payload=7
mock230: -> IF_CLOSESUB      op=23  payload=4
mock230: -> IF_SETEVENTS     op=108 payload=16
mock230: -> VARP_SMALL       op=97  payload=3
mock230: -> VARP_LARGE       op=12  payload=6
mock230: -> UPDATE_RUNENERGY op=64  payload=2
mock230: -> UPDATE_RUNWEIGHT op=31  payload=2
mock230: -> UPDATE_STAT      op=46  payload=7
mock230: -> REBUILD_NORMAL   op=49  payload=6
```

Every opcode and every size there is revision 239's. `user='testc'` is the
strongest single line in it: at 239 the username is not in the RSA block at all,
it is the first field of the **XTEA body**, so recovering it proves the RSA
envelope, the four seeds and the XTEA decrypt all agree between two halves
written independently from the same spec.

Two bugs this run caught that nothing else would have:

- **`serve()` memsets the server struct**, which erased the wire set at the call
  site. Every login block was then read as revision 230, and the symptom was
  `rsa decrypt failed` — a message pointing at the key rather than at the four
  bytes of `serverVersion` it had failed to skip.
- The RSA block now checks its **encryption-check byte** and says so. It is the
  one place a key mismatch announces itself; without it the failure surfaces
  later as a garbage username or seeds that make every subsequent packet
  unreadable, both of which read as protocol bugs.

`MOCK230_REV=osrs239 --selftest` runs the 239 writers now (it previously ran the
230 ones whatever the variable said, which is how three of the first six writers
were wrong with nothing to catch them). It reports **205 failures** against
**13** at 230 — that gap is the distance to parity, and it is a number that
comes down as writers land rather than a pass/fail.

## 6. The measured result, and what is left

```
js5: client at revision 240 accepted
js5: 255/255 -> 208 bytes
js5: 255/4 -> 88578 bytes
...
js5: session ended, 2733 groups / 7920704 bytes served
```

archives 12 (interfaces, 1155 groups), 0 (animations, 723), 8 (sprites, 651),
1, 21, 7, 2, 5, 3, 10, 17 and every reference table. Then the client crashed
inside its own decode (`error_game_crash`, in `tq.kz`).

**The blocker is a version skew this repo does not control.** RuneLite's shipped
client is revision **240**; RSProt's newest module is `osrs-239`; OpenRS2's
newest archived OldSchool cache is 239 (2026-07-29). Serving 239 content to a
240 client is expected to fail exactly where it did — in the interface/sprite
decode. The JS5 server takes its revision as an argument precisely so this can
be re-run the day a 240 cache and an `osrs-240` module exist.

Re-run with the integrated server (`--rev osrs239`, JS5 on the same port)
reached the same place: `client connected` -> `JS5 session opened at revision
240` -> the client crashes in its own decode. The server now survives the
disconnect cleanly rather than dying on SIGPIPE.

Not done, in the order that unblocks the most:

1. **PLAYER_INFO / NPC_INFO v5 are not written.** No vanilla client reaches the
   world without them. The largest remaining piece, and the one
   `MULTI_GENERATIONAL_PARITY.md` §5.4 already calls "the biggest new build".
   They are absent from the 239 writer set, so the server refuses them rather
   than sending a 230 bitstream a 239 client would read as garbage.
2. ~~**The server's login block is still the 230 shape.**~~ **Done** — see §5b.
   The server reads `serverVersion`, the OTP discriminator and the XTEA body,
   and checks the RSA encryption-check byte. It does not verify the 23 archive
   CRCs; that is a decision (this server has no cache-version policy) rather
   than an omission, and the fields are documented for whenever one appears.
3. **The 239 payload writer set covers 10 packets.** What is missing is listed
   in `k_transcribed_osrs239` and refused at send.
4. **The selftest cannot reach the 239 writers.** `--rev` / `MOCK230_REV` is
   read in `mock230_main`'s accept loop, and `mock230_world_selftest()` builds
   its own server without one, so `MOCK230_REV=osrs239 --selftest` still
   exercises the 230 path. The 239 writers are therefore verified by
   transcription against RSProt and by the length check, not by a test. Giving
   the selftest the selector is the cheapest next guard, and it is what would
   have caught the three wrong writers automatically instead of by reading.
5. **The client's own 239 parse is partial.** `osrs239_parse.c` delegates to the
   230 parser and explicitly refuses the eight packets whose layout moved,
   rather than decoding them wrongly. Filling those in is client parity, not a
   blocker for the above.

## 7. Reproducing

```sh
make -C src mock-js5
src/build/mock_js5 cache.osrs239 43594 239 &
python3 tools/torirs_javconfig.py --host 127.0.0.1 --port 8080 --revision 239 &

python3 tools/runelite_patch.py --modulus <MOCK230_RSA_PUBLIC_MODULUS>
python3 tools/runelite_patch.py --print-launch     # then run what it prints
```

`tools/js5_probe.py` points at either end, which is what makes a disagreement
between them a diff rather than an argument.
