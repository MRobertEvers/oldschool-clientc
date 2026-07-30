#ifndef RSCACHE_OPCODE_CODEC_H
#define RSCACHE_OPCODE_CODEC_H

/*
 * One interface over every config type that is an opcode stream.
 *
 * ## What an opcode stream is
 *
 * Twenty-six of this library's datatypes share one record shape: a sequence of
 * `<code:u8> <payload>` pairs terminated by a zero code. Nothing else about them
 * is shared — `loc` has 100 codes, `npc` 98, `obj` 82, `db` 2 — but the *shell*
 * is identical, and so is the pair of questions a caller asks: turn these bytes
 * into a record, turn this record back into bytes.
 *
 * Everything else in the library stays as it is. Models, sprites, maps, frames,
 * textures, fonts and scripts are not opcode streams and have no row here.
 *
 * ## Why an interface at all, when there is one implementation
 *
 * A vtable with a single implementation is just indirection. This one has two,
 * and they live in different repositories.
 *
 * A derived cache carries a *server* record beside the client one: the same npc
 * needs `hitpoints` and `respawnrate`, which no client opcode expresses. The
 * shape LostCity uses for this is one parse and two encoders
 * (`engine/tools/pack/config/PackShared.ts:137`, `ConfigDatIdx = { client, server }`).
 * This header is where that split is expressed here — with one difference that
 * is the whole reason it can stay small, described under "Not inheritance" below.
 *
 * The client implementation ships in this library. The server implementation
 * does not, and must not: teaching `RSCache_Dat2ConfigNpc` about `huntmode`
 * would put server concerns inside a vendored library whose separation is the
 * point (`tools/cachepack/cp_register.h:6-10`). What crosses the boundary is
 * this interface, not a field.
 *
 * ## Not inheritance
 *
 * LostCity's server `NpcType.decode(code, dat)` parses the *client* band as well
 * as its own — codes 1, 2, 3, 12, 13, 14 alongside 18, 74-79 and 200+ — because
 * its server record is the only npc record there is. Ours is not: the server
 * seeds every field from the decoded cache record first
 * (`npc_def_seed_from_cache`) and the server stream then overrides per opcode
 * present. So a server codec never decodes a client opcode, and the
 * `super.decode()` fallthrough that per-opcode virtualisation exists to enable
 * buys nothing.
 *
 * That is why `decode` here is whole-record rather than per-opcode. The
 * alternative would have required splitting 26 `while(1)` loops that carry
 * cross-opcode state — `dat2_config_npc.c` sets `prev_opcode` at :535 and reads
 * it at :1258 — on decoders every one of which is held to byte-exact round-trip.
 * The seam is worth having; that price was not.
 *
 * ## The asymmetry is real
 *
 * `decode` is dispatched *by* the code in the stream, so it could in principle
 * be virtual per code. `encode` cannot: it has to choose which codes to emit and
 * in what order, and the order is load-bearing. `dat2_config_db.c` writes opcode
 * 4 before 3; with those two transposed, 15,418 of 16,711 dbrow records in
 * osrs239 fail byte-identity. No per-opcode interface can express that
 * constraint, so `encode` is whole-record and this struct is asymmetric by
 * necessity rather than by shortcut.
 *
 * ## Keying
 *
 * On `(epoch, type)`, not on `type`. `obj`, `npc`, `seq`, `spotanim`, `idk` and
 * `component` each exist in both the dat1 and dat2 families with different
 * layouts, and a registry keyed on type alone would answer one of them wrongly
 * and silently. `RSCache.epoch` already carries the distinction, which is why
 * the profile is the first parameter of every entry point rather than something
 * the caller supplies alongside.
 */

#include "rsbuffer.h"
#include "rscache_profile.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * The two directions of one opcode-stream datatype.
 *
 * A row with `encode == NULL` decodes but cannot be written back. That is a
 * statement about the datatype, not a defect to route around: callers that pack
 * must check, and `RSCache_OpcodeCodecCanEncode` is the check.
 */
struct RSCache_OpcodeCodec
{
    /** Datatype name as the content tree spells it, e.g. "npc". Borrowed. */
    const char* name;
    /** Which datatype slot, for `RSCache_OpcodeCodecFor`. */
    enum RSCache_Type type;
    /** RSCACHE_EPOCH_DAT1 or RSCACHE_EPOCH_DAT2. */
    enum RSCache_Epoch epoch;

    /** `sizeof` the record struct, so a caller can allocate without naming it. */
    size_t record_size;

    /**
     * Bring a zeroed allocation to the type's *defaults*.
     *
     * Not a memset, and not optional. Several decoders default fields to -1
     * rather than 0 — `dat1_config_npc.c` sets `readyanim = -1` — and the
     * difference is not cosmetic: a dat2 npc with no opcode 13 currently yields
     * `standing_animation` 0, which reaches the world as sequence id 0, and
     * asking for sequence 0 is not the same as asking for no sequence
     * (`dat2_config_npc.h`, "Why byte-exactness is low here"). A codec that
     * skips this inherits that bug.
     */
    void (*record_init)(void* record);

    /** Release anything the record owns. The record itself is the caller's. */
    void (*record_free)(void* record);

    /**
     * Era payload flags for this type in this cache, or NULL when it has none.
     *
     * Computed once per record by the driver and handed to every `decode_op`.
     * Not optional for the types that have it: npc opcode 102 *changes shape* at
     * rev 210, so an op handler that never sees the flags decodes a modern cache
     * wrongly and silently (`dat2_config_npc.h`).
     */
    unsigned (*flags_for)(const struct RSCache* cache);

    /**
     * Handle one opcode, advancing `buffer` past its payload.
     *
     * Returns true when this codec consumed the opcode, false when it does not
     * know it. **False is not an error** — it is the extension point. A codec
     * that wraps another delegates to it first and handles only what comes back
     * unclaimed:
     *
     *     bool server_npc_decode_op(void* rec, int op, buf, flags)
     *     {
     *         struct ServerNpc* npc = rec;
     *         if( client->decode_op(&npc->base, op, buf, flags) )
     *             return true;
     *         switch( op ) { case 77: npc->hitpoints = g2(buf); return true; }
     *         return false;
     *     }
     *
     * That is what lets a server record be a client record plus fields, in one
     * struct with the client's at offset zero, and it is why this is per-opcode
     * rather than per-record. It also means an authored overlay can override a
     * *client* field by writing that client opcode into the server stream — the
     * delegation applies it to the embedded base with no extra mechanism.
     *
     * The width of an unknown opcode is unknowable, so returning false must stop
     * the stream rather than skip. The driver does that, and the short
     * `_consumed` it leaves is the signal every caller here already checks.
     */
    bool (*decode_op)(
        void* record,
        int opcode,
        struct RSCache_Buffer* buffer,
        unsigned flags);

    /**
     * Decode `size` bytes into `record`, which `record_init` has already seen.
     *
     * Returns **bytes consumed**, or -1 on failure. Consumed rather than a bare
     * success flag because exact consumption is how every decoder in this
     * library was validated in the first place: a return short of `size` means
     * the layout is wrong in a way a semantic check cannot see.
     *
     * A row that supplies `decode_op` may leave this NULL and take
     * `RSCache_OpcodeStreamDecode`, which is the same loop for every type. It is
     * kept overridable for the handful of types whose record is not purely a
     * stream — `component` carries a fixed header before its opcodes.
     */
    int (*decode)(
        const struct RSCache* cache,
        void* record,
        const uint8_t* src,
        int size);

    /**
     * Encode `record` into `out`, returning bytes written or 0 on failure.
     *
     * NULL when the type has no encoder yet.
     */
    uint32_t (*encode)(
        const struct RSCache* cache,
        const void* record,
        uint8_t* out,
        uint32_t out_capacity);

    /**
     * An upper bound on what `encode` will write, for sizing `out`.
     *
     * Takes the record because several types are unbounded in principle — an
     * `enum` holds as many entries as it holds — so a constant would either be
     * absurdly large or occasionally too small.
     */
    uint32_t (*encode_bound)(const void* record);
};

/**
 * The opcode-stream loop, shared by every type that supplies `decode_op`.
 *
 * `<code:u8> <payload>` until a zero code or the buffer runs out. Returns bytes
 * consumed. An opcode `decode_op` declines stops the loop — the width of an
 * unknown opcode cannot be guessed — which leaves the return short of `size`,
 * and that shortfall is the signal, not an error code.
 *
 * Exposed rather than kept private so a server-side codec built on this
 * interface runs the *same* loop as the client one. Two hand-written loops that
 * agree today are two loops that can disagree later.
 */
int
RSCache_OpcodeStreamDecode(
    const struct RSCache_OpcodeCodec* codec,
    const struct RSCache* cache,
    void* record,
    const uint8_t* src,
    int size);

/**
 * The codec for `type` in `cache`'s epoch, or NULL if there is none.
 *
 * **This table holds the client implementations.** It is not a global registry
 * of every codec in the process: a server-side codec that wraps one of these
 * lives in the server's own repository and is reached through its own lookup.
 * Both are `struct RSCache_OpcodeCodec`, which is the whole point — generic
 * consumers take the pointer and never learn which side supplied it. Appending
 * server rows here instead would drag server fields into a vendored library, the
 * separation `tools/cachepack/cp_register.h:6-10` exists to keep.
 *
 * NULL means "not an opcode stream in this epoch", which is a normal answer:
 * `RSCACHE_TYPE_MODEL` has no row in either family, and `RSCACHE_TYPE_HITSPLAT`
 * has one only in dat2.
 */
const struct RSCache_OpcodeCodec*
RSCache_OpcodeCodecFor(
    const struct RSCache* cache,
    enum RSCache_Type type);

/** The codec named `name` in `cache`'s epoch, or NULL. */
const struct RSCache_OpcodeCodec*
RSCache_OpcodeCodecByName(
    const struct RSCache* cache,
    const char* name);

/** Iteration, so a test or a packer can cover every registered type without a
 *  list of its own that can fall behind this one. */
int
RSCache_OpcodeCodecCount(void);

const struct RSCache_OpcodeCodec*
RSCache_OpcodeCodecAt(int index);

/** Whether `codec` can write records back. False is a legitimate state. */
static inline bool
RSCache_OpcodeCodecCanEncode(const struct RSCache_OpcodeCodec* codec)
{
    return codec && codec->encode && codec->encode_bound;
}

/**
 * Allocate, init and decode in one call. NULL on failure.
 *
 * Frees what it allocated if the decode fails, so a caller never holds a
 * half-decoded record. Release with `RSCache_OpcodeCodecRecordFree`.
 */
void*
RSCache_OpcodeCodecRecordDecode(
    const struct RSCache_OpcodeCodec* codec,
    const struct RSCache* cache,
    const uint8_t* src,
    int size,
    int* out_consumed);

void
RSCache_OpcodeCodecRecordFree(
    const struct RSCache_OpcodeCodec* codec,
    void* record);

#endif
