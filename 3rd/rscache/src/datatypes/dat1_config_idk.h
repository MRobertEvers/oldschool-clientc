#ifndef RSCACHE_DATATYPES_DAT1_CONFIG_IDK_H
#define RSCACHE_DATATYPES_DAT1_CONFIG_IDK_H

#include "../rsbuffer.h"

#include <stdbool.h>
// Identity Kit.
// type: number = -1;
// models: Int32Array | null = null;
// recol_s: Int32Array = new Int32Array(6);
// recol_d: Int32Array = new Int32Array(6);
// heads: Int32Array = new Int32Array(5).fill(-1);
// disable: boolean = false;
struct RSCache_Dat1ConfigIdk
{
    int type;
    int* models;
    int models_count;
    int recol_s[10];
    int recol_d[10];
    int heads[10];
    bool disable;
    /**
     * The opcodes in the order the record carried them.
     *
     * Replayed by the encoder, because dat1 idk records are *not* in ascending
     * opcode order — cache254's first record runs 1, 60, 2 — and the order is not
     * recoverable from the fields. Same device `RSCache_Dat2ConfigHitsplat` uses
     * for the same reason.
     *
     * 23 slots covers every opcode this type can carry: 1, 2, 3 and the three
     * ten-wide ranges are 33 in principle, but a record states each range slot at
     * most once and the corpus maximum is far lower; the encoder stops at
     * `opcode_count`.
     */
    int opcodes[64];
    int opcode_count;
};

struct RSCache_Dat1ConfigIdkList
{
    struct RSCache_Dat1ConfigIdk** idks;
    int idks_count;
};

struct RSCache_Dat1ConfigIdkList*
RSCache_Dat1ConfigIdkListNewDecode(
    void* jagfile_idkdat_data,
    int jagfile_idkdat_data_size);

/**
 * Bring a zeroed allocation to this type's defaults.
 *
 * Every idk default *is* zero, which is worth stating rather than leaving to a
 * memset: `type` 0 is a real body-part slot, not "absent", so there is nothing
 * here to seed to -1 and no field where zero would be a wrong answer. Named so
 * the registry's `record_init` and the list decoder share one definition — the
 * split that cost dat2 npc 99 byte-exact records when it drifted.
 */
void
RSCache_Dat1ConfigIdkInit(struct RSCache_Dat1ConfigIdk* idk);

/**
 * Handle one opcode, advancing `buffer` past its payload.
 *
 * False means "not mine", which ends the stream — the width of an unknown
 * opcode cannot be guessed. See `opcode_codec.h`.
 */
bool
RSCache_Dat1ConfigIdkDecodeOp(
    struct RSCache_Dat1ConfigIdk* idk,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

/** Release what the record owns, leaving the struct itself to the caller. */
void
RSCache_Dat1ConfigIdkFreeInplace(struct RSCache_Dat1ConfigIdk* idk);

void
RSCache_Dat1ConfigIdkFree(struct RSCache_Dat1ConfigIdk* idk);

/**
 * Encode one dat1 idk record. Returns bytes written, or 0.
 *
 * Zero-valued recolour and head slots are omitted — see the note in the .c, and
 * `test_dat1_encode` for how many records that costs.
 */
uint32_t
RSCache_Dat1ConfigIdkEncode(
    const struct RSCache_Dat1ConfigIdk* idk,
    uint8_t* out,
    uint32_t out_capacity);

/** An upper bound on what `RSCache_Dat1ConfigIdkEncode` will write. */
uint32_t
RSCache_Dat1ConfigIdkEncodeBound(const struct RSCache_Dat1ConfigIdk* idk);

#endif