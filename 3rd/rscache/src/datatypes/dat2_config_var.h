#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_VAR_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_VAR_H

#include "../rsbuffer.h"

/*
 * The variable config types: varbit (group 14), varplayer (group 16) and
 * varclient (group 19).
 *
 * One file because they are one family and each is a handful of opcodes — the same
 * reason underlay and overlay share dat2_config_flo.c, and dbrow and dbtable share
 * dat2_config_db.c.
 *
 * ## Where these formats come from
 *
 * Every opcode below was established by dumping all records of the group in five
 * caches (kronos, osrs184, osrs230, osrs239, jan2026) and brute-forcing operand
 * widths over 0-4. In each case **exactly one** assignment consumes 100% of records,
 * so the widths are pinned rather than assumed. varbit and varplayer additionally
 * agree with `Client-TS/src/config/VarBitType.ts` and `VarpType.ts`, which are the
 * dat1 form of the same types.
 *
 * The same sweep proves the opcode *sets* complete for this corpus: a record
 * containing any opcode outside the ones listed would have failed to parse, and none
 * did. The dat1 varplayer record additionally uses opcodes 1, 2, 3, 4, 6, 7, 8, 10
 * and 11 (mostly server-side fields the client skips); no dat2 record in the corpus
 * carries any of them, so their dat2 widths are unverified and they are deliberately
 * **not** implemented. A record using one is reported through `_consumed` rather than
 * parsed on a guess — see the note on the decoders.
 *
 * ## varclient_string (group 15) is not here
 *
 * Deliberately. The group is absent from cache.osrs230 and cache.jan2026, empty in
 * cache.kronos, and the 8 records in cache.osrs239 do not look like client string
 * variables at all — 27 to 113 bytes, with runs of ascending u16s and opcodes up to
 * 0x21. cache.643 has 351 records but maps table 2 differently, so it cannot
 * corroborate. There is nothing here to validate a decoder against, and guessing one
 * would be worse than the gap. See B13 in EXCEPTIONS.md.
 */

/**
 * A varbit: a named bit range inside a varplayer.
 *
 * This is how the game packs many small values — quest stages, setting toggles,
 * diary progress — into one shared player variable. Reading a varbit is
 * `(varp[basevar] >> startbit) & ((1 << (endbit - startbit + 1)) - 1)`.
 */
struct RSCache_Dat2ConfigVarbit
{
    int id;
    /** The varplayer this varbit lives in. -1 when the record named none. */
    int basevar;
    int startbit;
    int endbit;
    /** Opcode 10. Present in dat1 caches; no dat2 record in the corpus carries it. */
    char* debugname;
    /** Bytes consumed. Equal to the record size for a fully understood record. */
    int _consumed;
};

/**
 * A varplayer's type. Server-synced state; the client only cares about one field.
 *
 * `clientcode` ties the variable to built-in client behaviour — run energy, weight,
 * chat filters and so on — rather than to a script.
 */
struct RSCache_Dat2ConfigVarplayer
{
    int id;
    /** Opcode 5. 0 when the record named none; observed values are 1..22. */
    int clientcode;
    int _consumed;
};

/**
 * A varclient's type. Client-only state, never synced to the server: selected tab,
 * scroll offsets, options. CS2 reads and writes these constantly.
 */
struct RSCache_Dat2ConfigVarclient
{
    int id;
    /** Opcode 2, a bare flag with no operand. Whether the value survives a logout. */
    int persist;
    /**
     * Opcode 3's u16 payload.
     *
     * Carried under its opcode number because its meaning is not established: it
     * appears on 60 records across the corpus, always alongside opcode 2, with only
     * two distinct values (0 and 33). Naming it speculatively would be worse than
     * leaving it plainly labelled.
     */
    int opcode_3;
    /** Whether opcode 3 was present. Needed separately from `opcode_3` because 0 is
     *  a value the wire carries explicitly — 40 records do. */
    bool has_opcode_3;
    int _consumed;
};

/*
 * Decoders come in two forms, and the split is the point of the structure:
 *
 *  - `Decode` takes a cursor, so it can be called repeatedly over a buffer holding
 *    back-to-back records. That is exactly how the dat1 era stores these: one
 *    `varbit.dat` blob of `u16 count` then `count` terminator-delimited records. The
 *    per-record shape is identical between eras, so one codec serves both.
 *  - `DecodeInplace` takes a whole dat2 record, where the container already split the
 *    group into one file per id, and records `_consumed`.
 *
 * On an opcode the decoder does not know it stops rather than guessing a width, which
 * leaves `_consumed` short of the record size. That is the signal the round-trip
 * harness asserts on, so an unrecognised field shows up as a test failure instead of
 * silently misaligning everything after it.
 */

void
RSCache_Dat2ConfigVarbitDecode(
    struct RSCache_Dat2ConfigVarbit* entry,
    struct RSCache_Buffer* buffer);

void
RSCache_Dat2ConfigVarbitDecodeInplace(
    struct RSCache_Dat2ConfigVarbit* entry,
    const void* data,
    int data_size);

/**
 * Set the type's non-zero defaults on an already-zeroed record.
 *
 * Must run before any `DecodeOp` call: `basevar` defaults to -1, because a varbit
 * that named no base variable is not one pointing at varplayer 0.
 */
void
RSCache_Dat2ConfigVarbitInit(struct RSCache_Dat2ConfigVarbit* entry);

/*
 * Per-opcode handlers. True when consumed, false when unknown — false being the
 * extension point a server-side record delegates through. See `opcode_codec.h`.
 */

bool
RSCache_Dat2ConfigVarbitDecodeOp(
    struct RSCache_Dat2ConfigVarbit* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

bool
RSCache_Dat2ConfigVarplayerDecodeOp(
    struct RSCache_Dat2ConfigVarplayer* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

bool
RSCache_Dat2ConfigVarclientDecodeOp(
    struct RSCache_Dat2ConfigVarclient* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

void
RSCache_Dat2ConfigVarplayerDecode(
    struct RSCache_Dat2ConfigVarplayer* entry,
    struct RSCache_Buffer* buffer);

void
RSCache_Dat2ConfigVarplayerDecodeInplace(
    struct RSCache_Dat2ConfigVarplayer* entry,
    const void* data,
    int data_size);

void
RSCache_Dat2ConfigVarclientDecode(
    struct RSCache_Dat2ConfigVarclient* entry,
    struct RSCache_Buffer* buffer);

void
RSCache_Dat2ConfigVarclientDecodeInplace(
    struct RSCache_Dat2ConfigVarclient* entry,
    const void* data,
    int data_size);

/*
 * Encoders. All three are byte-exact on every record in the corpus: the records are
 * fixed-shape, so there is no opcode ordering to reproduce and nothing the decode
 * discards.
 *
 * Each returns bytes written, or 0 on failure.
 */

uint32_t
RSCache_Dat2ConfigVarbitEncode(
    const struct RSCache_Dat2ConfigVarbit* entry,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat2ConfigVarbitEncodeBound(const struct RSCache_Dat2ConfigVarbit* entry);

uint32_t
RSCache_Dat2ConfigVarplayerEncode(
    const struct RSCache_Dat2ConfigVarplayer* entry,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat2ConfigVarplayerEncodeBound(const struct RSCache_Dat2ConfigVarplayer* entry);

uint32_t
RSCache_Dat2ConfigVarclientEncode(
    const struct RSCache_Dat2ConfigVarclient* entry,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat2ConfigVarclientEncodeBound(const struct RSCache_Dat2ConfigVarclient* entry);

void
RSCache_Dat2ConfigVarbitFreeInplace(struct RSCache_Dat2ConfigVarbit* entry);

void
RSCache_Dat2ConfigVarbitFree(struct RSCache_Dat2ConfigVarbit* entry);

#endif // RSCACHE_DATATYPES_DAT2_CONFIG_VAR_H
