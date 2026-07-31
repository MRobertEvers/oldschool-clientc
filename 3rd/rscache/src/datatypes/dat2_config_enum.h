#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_ENUM_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_ENUM_H

#include <stdint.h>

struct RSCache_Dat2ConfigEnum
{
    int id;
    /**
     * The ScriptVarType characters opcodes 1 and 2 carry, or 0 when absent.
     *
     * **Promoted out of the lossy list on 2026-07-30**, for the reason
     * `docs/CONTENT_PACK_PLAN.md` §10 gives: an opcode is modelled when a feature
     * needs it, and authoring an enum needs both. A record built from a content
     * tree that omits them is one the client cannot interpret — the input type is
     * what says whether the key is an obj id or a component id, and nothing else
     * in the record carries that.
     *
     * `output_is_string` stays because it is the *encoder's* switch: which of the
     * three value arrays is populated decides whether opcode 5, 6 or 7 is
     * written, and that has to keep working for a record whose `output_type` is 0
     * because the source never had opcode 2.
     */
    char input_type;
    char output_type;
    int output_is_string; /* bool as int */
    int default_int;
    int64_t default_long;
    char* default_string;
    int* keys;
    int* int_values;
    int64_t* long_values;
    char** string_values;
    int count;
    /** Bytes consumed. Equal to the record size for a fully understood record.
     *
     *  Added with the stop-on-unknown fix: this decoder skipped opcodes it did
     *  not know, so it had no way to report that it had lost the thread. */
    int _consumed;
};

void
RSCache_Dat2ConfigEnumDecodeInplace(
    struct RSCache_Dat2ConfigEnum* entry,
    const void* data,
    int data_size);

/**
 * Encode an enum record.
 *
 * Not byte-exact for records carrying opcode 1 — the decoder consumes it without
 * keeping the key type, so that information is already gone. Semantically exact
 * otherwise (including long maps via opcodes 7/8).
 */
uint32_t
RSCache_Dat2ConfigEnumEncode(
    const struct RSCache_Dat2ConfigEnum* entry,
    uint8_t* out,
    uint32_t out_capacity);

void
RSCache_Dat2ConfigEnumFree(struct RSCache_Dat2ConfigEnum* entry);

void
RSCache_Dat2ConfigEnumFreeInplace(struct RSCache_Dat2ConfigEnum* entry);

/** An upper bound on what `RSCache_Dat2ConfigEnumEncode` will write. */
uint32_t
RSCache_Dat2ConfigEnumEncodeBound(const struct RSCache_Dat2ConfigEnum* entry);

#endif
