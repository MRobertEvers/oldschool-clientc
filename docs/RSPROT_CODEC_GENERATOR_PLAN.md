# Finishing the codec generator — the last 12 packets

`osrs239_parse.c` hand-decodes 12 packets that the generator refuses. Each
refusal names a *construct*, not a packet, and the constructs group into six
features. This is the plan for those features, ordered by how many packets each
unlocks per unit of risk.

Everything here is measured from the vendored Kotlin
(`~/Documents/git_repos/rsprot`, revision 239) and from
`3rd/rsprot/gen/codec_status.txt`. Re-run the generator before trusting a count:
the last three estimates in this lane were all wrong because they came from
memory of a previous run rather than from the tool.

## 0. Two of them are not what the status file says

Worth doing first, because they are cheap and they change the shape of the
remaining work.

**`SET_NPC_UPDATE_ORIGIN` is not missing.** `SetNpcUpdateOriginEncoder.kt`
exists at 239 — it lives under `codec/npcinfo/`, and the generator excludes
that whole directory via `EXCLUDE_DIR_PARTS` alongside `playerinfo` and
`worldentity`. That exclusion is right for the info *streams*, which are
stateful per-observer machines, and wrong for a flat encoder that happens to
share their folder. Fix: exclude by *class shape* (does it extend
`MessageEncoder` with a flat `encode`?) rather than by directory, or whitelist
the known-flat encoders in those folders. **Effort: an hour. Unlocks 1.**

**`UNSET_MAP_FLAG` needs checking, not implementing.** It appears only in the
prot tables, which is consistent with a zero-payload packet handled by
`NoOpMessageEncoder`. The generator already emits zero-payload codecs
(`SERVER_TICK_END` works). Confirm that first; it may need nothing at all.

## 1. Derived fields — `RSPROT_XFORM`

**Unlocks: UPDATE_INV_FULL, UPDATE_INV_PARTIAL, UPDATE_INV_STOP_TRANSMIT,
LOC_ADD_CHANGE (partly). 3-4 packets.**

Three refusals are one missing idea: a wire field that is a *function* of a
struct field rather than the field itself.

```kotlin
buffer.p2Alt1(InventoryObject.getId(obj) + 1)   // offset
buffer.p1Alt3(count.coerceAtMost(0xFF))         // clamp, with a 4-byte escape
buffer.p1(key.toInt() - 1)                      // offset, in LOC_ADD_CHANGE
```

Today these hit `_is_lvalue_c` and are refused, correctly — a ternary or an
addition has no address for the decode direction to write through. But the
transforms are all *invertible*, which is exactly what a bidirectional codec
needs.

Add one operation to `rsprot_exec.h`:

```c
/* Encode: write f(field). Decode: field = f-inverse(read). */
void rsprot_xform(RsprotExec *x, RsprotPrim prim, int32_t *value,
                  RsprotXform kind, int32_t operand, const char *name);
```

with `RSPROT_XFORM_ADD` (operand ±n) covering the offsets. The clamp is *not* a
transform and must not be modelled as one — `min(count, 255)` loses
information, and the layout recovers it with a following 4-byte field. That is
a **conditional escape**, and it is already expressible with what exists:

```c
RSPROT_U1_ALT3(x, m->count_lo);                    /* transferred */
if (RSPROT_BRANCH(x, m->count_lo) == 255)
    RSPROT_U4_ALT3(x, m->count);                   /* legal: branches on a
                                                      field already moved */
```

The only new thing the generator needs is to recognise `coerceAtMost` as
producing a *derived* field (`count_lo`) whose encode value is computed from
`count`. That is a fourth direction concern — encode must fill it, decode must
not — so it belongs as `RSPROT_DERIVED`, computed on ENCODE and FILL, read on
DECODE.

**Risk: low.** Every case is invertible and the differential test covers it.

## 2. Sentinels — `InventoryObject.NULL`

**Unlocks: the same three inventory packets (blocks with §1).**

`if (obj == InventoryObject.NULL) { p1Alt3(0); p2Alt1(0); continue }` is a
sentinel *encoding*: the empty slot is id 0, count 0. On decode there is no
comparison to make — id 0 simply means empty, and the element struct already
holds it. The generator should recognise `== <Type>.NULL` in an if-condition
over a loop element and emit the arm's writes as ordinary transfers, with the
sentinel documented in the generated comment.

**Risk: low, but this is the one place to be careful**: `getId(obj) + 1` means
the wire's 0 is "absent" and the wire's `n` is object `n-1`. Get the offset
backwards and every item in the inventory shifts by one — a bug that renders
perfectly and is wrong everywhere.

## 3. Length-driven loops

**Unlocks: IF_RESYNC_V2, UPDATE_IGNORELIST, UPDATE_FRIENDLIST, RUNCLIENTSCRIPT.
4 packets.**

Two distinct shapes, and the generator currently refuses both under one message.

**(a) Read to end of payload.** `for (ignore in message.ignores)` with no count
written: the reader consumes until the payload is exhausted. This is safe to
decode *only* because the packet is VAR_SHORT and its length came from the
frame. Add to the vocabulary:

```c
/** Bytes left in this payload. 0 when encoding (the caller drives the count). */
int32_t rsprot_remaining(RsprotExec *x);
```

and generate `while (rsprot_remaining(x) > 0 && idx < cap)`. The `cap` is not
optional — an unbounded loop over attacker-controlled length is the exact
failure `rsprot_xcount` was written to prevent, and this must inherit that
discipline.

**(b) Count from a transferred string.** RUNCLIENTSCRIPT writes `pjstr(types)`
*first*, then loops `types.size` times reading `types[i]` as the discriminator.
The count and the discriminator both come off the wire before they are used, so
this is fully decodable — it just needs the generator to model "length of a
string field already transferred" as a loop bound, and indexing into it.

**Risk: medium for (a)** — it changes what "end of packet" means to a codec,
and a codec that over-reads by one element in a VAR_SHORT stream desynchronises
everything after it. Bound it, and test with truncated payloads specifically.

## 4. Tagged unions — `when`

**Unlocks: UPDATE_IGNORELIST, UPDATE_FRIENDLIST, RUNCLIENTSCRIPT (with §3).
3 packets.**

`rsprot_exec.h` already anticipates this: *"a tagged-union payload is a switch,
and that construct is what blocks most of the code generator's current skips."*
`RSPROT_BRANCH` exists and enforces the one rule. What is missing is the
message-struct shape and the generator's handling.

Message struct grows a discriminated union:

```c
typedef struct MsgUpdateIgnoreListEntry {
    int32_t kind;               /* transferred, then switched on */
    union {
        struct { int32_t added; const char *name, *prev, *note; } _added;
        struct { const char *name; } _removed;
    };
} MsgUpdateIgnoreListEntry;
```

Two sub-shapes, and only one is easy:

- **Kotlin `when` over a value** (`when (type) { 'W' -> ... }`, RUNCLIENTSCRIPT).
  The discriminator is a real transferred field. Direct translation to
  `switch (RSPROT_BRANCH(x, m->kind))`. **Low risk.**

- **Kotlin `when` over a sealed type** (`when (ignore) { is AddedEntry -> ... }`,
  UPDATE_IGNORELIST/FRIENDLIST). There is *no* discriminator field — the type
  is the discriminator, and each arm happens to write a distinguishing first
  byte (`0x1`/`0x0` for added, `0x4` for removed). Decoding has to read that
  byte and infer the arm from its value.

  **This is the one place in the whole plan where the generator would be
  inferring semantics rather than transcribing them**, and it is where I would
  stop and hand-write instead. A wrong inference here produces a codec that
  encodes correctly and decodes into the wrong union arm — and the differential
  test would catch it only for the shapes the fill happens to produce. If it is
  attempted, the arm-tag inference must be *stated* in the generated file and
  the packet must get a byte-pinned test, not just a round trip.

**Risk: low for value-`when`, HIGH for sealed-`when`.** Split them; do not let
the second block the first.

## 5. Map iteration — `for_each_pair`

**Unlocks: LOC_ADD_CHANGE. 1 packet.**

`for ((key, value) in ops)` over a `Map<UByte, String>`. The IR already emits a
`for_each_pair` op; `gen_op` has no arm for it. Mechanically it is `for_each`
with a two-field element struct (`key`, `value`), and combined with §1's offset
transform for `key - 1` it completes the packet.

**Risk: low.** The count is transferred first (`p1Alt1(opCount)`), so the loop
bound is already legal.

## 6. Tail-anchored payloads — REBUILD_NORMAL

**Unlocks: REBUILD_NORMAL. 1 packet. Recommend NOT doing this.**

RSProt's `RebuildLogin` and `RebuildNormal` share opcode 49 and an encoder; the
login variant prepends a GPI init block and the three shorts are the *last six
bytes*. There is no discriminator — the length is the discriminator.

A codec that seeks relative to the payload end is expressible, but it makes
"where does this codec start reading" a property of the frame rather than the
codec, which is the one invariant that has kept this layer simple. The
hand-written arm already splits the payload and hands the head to
`osrs239_playerinfo_init`; the right move is to leave the split where it is and,
if desired, run the generated codec on the trailing six bytes only.

Same reasoning for `UPDATE_ZONE_PARTIAL_ENCLOSED`: it is a header plus a
sub-packet stream. Its header can use the generated codec exactly the way the
zone sub-packets now do (`zone_run` in `osrs239_parse.c`) — that is a small,
safe change worth making, and it does not need any feature on this list.

## Sequencing

| # | feature | packets | risk | note |
|---|---|---|---|---|
| 0 | directory-exclusion fix | 1 | trivial | do first |
| 0 | confirm UNSET_MAP_FLAG | 1 | trivial | may be free |
| 6b | ENCLOSED header via codec | 0 | low | no feature needed, tidies the file |
| 5 | `for_each_pair` | 1 | low | with §1 |
| 1 | derived/offset fields | 3-4 | low | |
| 2 | sentinels | (with §1) | low | offset direction is the trap |
| 3a | read-to-end loops | 2 | medium | must be bounded |
| 3b | string-length loops | 1 | low | |
| 4a | value-`when` | 1 | low | |
| 4b | sealed-`when` | 2 | **high** | consider hand-writing |

Doing 0, 5, 1, 2, 3b and 4a lands **8 of the 12** on generated codecs with no
inference and no new failure mode. 3a adds 2 more and needs a truncation test.
4b is the only one I would argue against automating.

## How each step is verified

Unchanged from the rest of this lane, and it is the reason the plan is safe to
execute incrementally:

1. The generated codec must compile with zero warnings under `-Wall -Wextra`.
2. `make -C src test-rsprot-bridge` runs BOTH the new codec and the surviving
   hand-written arm over the same payloads and requires identical `RevPacket`s.
   Every mapping bug found in this lane — the run-energy unit, the signed rotate
   speed, the CamShake version-renamed fields — was found this way and by
   nothing else.
3. For §3a and §4b specifically, add byte-pinned cases: a round trip cannot
   catch a wrong arm or a wrong element count when both directions share the
   mistake.
4. Delete the hand-written arm only after (2) is green for that packet — the arm
   is the oracle, so removing it early removes the check.
