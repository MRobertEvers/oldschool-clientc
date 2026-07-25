#ifndef RSCACHE_DATATYPES_CLIENTSCRIPT_H
#define RSCACHE_DATATYPES_CLIENTSCRIPT_H

#include "../dat2disk.h"
#include "../rscache_profile.h"
#include "cs2_script.h"

#include <stdint.h>

#define RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_MODERN 0
#define RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY 1

/**
 * Trailer flag for this cache.
 *
 * Returns LEGACY for every declared profile, which is what the client already
 * hardcoded at its one call site (with the MODERN branch commented out above
 * it). This function exists to give that decision a single home rather than to
 * change it: no revision threshold for the trailer width has been verified
 * against a cache yet, and guessing one would silently break script decode for
 * whichever side of the guess is wrong.
 *
 * Note the decoder is a *try* — cs2_script_try_decode_footer validates the
 * footer and returns NULL on a mismatch — so when the threshold is established
 * it can be added here and checked against every cache in the corpus.
 */
int
RSCache_ClientScriptFlags(const struct RSCache* cache);

struct RSCache_ClientScript
{
    struct RSCache_CS2_Script script;
};

struct RSCache_ClientScript*
RSCache_ClientScriptNewFromDecodeFlags(
    int script_id,
    const uint8_t* data,
    int data_size,
    int flags);

/**
 * Encode a clientscript.
 *
 * Layout, for reference:
 *
 *   [0]            NUL-terminated signature
 *   [after sig]    body: per op a u16 opcode then an operand whose width the opcode
 *                  selects
 *   [trailer_pos]  u32 op_count, u16 local_int, u16 local_string,
 *                  [modern: u16 local_long], u16 int_arg, u16 string_arg,
 *                  [modern: u16 long_arg], u8 switch_count,
 *                  then per switch: u16 case_count, per case u32 key + u32 target
 *   [end]          u16 trailer_len
 *
 * The decoder finds the trailer with
 * `trailer_pos = data_size - footer_size - trailer_len`, which only works if
 * **`trailer_len` is the switch-table byte count plus one** — the +1 covering the
 * switch-count byte that `footer_size` does not. Measured across every script in
 * cache.osrs230, with and without switch tables: S=0 gives 1, S=50 gives 51, S=92
 * gives 93, S=162 gives 163, S=192 gives 193, S=418 gives 419.
 *
 * One field cannot be reproduced: `PUSH_CONSTANT_LONG` carries two u32s and the
 * decode keeps only the low one, because `int_operands` is `int`. This sign-extends
 * from the low word, which restores any constant that fits in 32 bits; a genuinely
 * 64-bit constant is already lost by then. Widening `int_operands` to `int64_t` is
 * the real fix and would touch the CS2 VM.
 *
 * Returns bytes written, or 0 on failure.
 */
uint32_t
RSCache_ClientScriptEncode(
    const struct RSCache* cache,
    const struct RSCache_ClientScript* script,
    uint8_t* out,
    uint32_t out_capacity);

/** As RSCache_ClientScriptEncode, for callers holding a raw trailer flag. */
uint32_t
RSCache_ClientScriptEncodeFlags(
    const struct RSCache_ClientScript* script,
    int flags,
    uint8_t* out,
    uint32_t out_capacity);

/** Worst-case output size for RSCache_ClientScriptEncode. */
uint32_t
RSCache_ClientScriptEncodeBound(const struct RSCache_ClientScript* script);

struct RSCache_ClientScript*
RSCache_ClientScriptNewFromArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int script_id,
    int flags);

void
RSCache_ClientScriptFree(struct RSCache_ClientScript* script);

#endif
