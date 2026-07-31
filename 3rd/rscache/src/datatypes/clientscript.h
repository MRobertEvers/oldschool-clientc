#ifndef RSCACHE_DATATYPES_CLIENTSCRIPT_H
#define RSCACHE_DATATYPES_CLIENTSCRIPT_H

#include "../dat2disk.h"
#include "../rscache_profile.h"
#include "cs2_script.h"

#include <stdint.h>

#define RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_MODERN 0
#define RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY 1

/**
 * OR into the decode flags to suppress the "decode failed" diagnostic.
 *
 * For a caller that tries one trailer family and falls back to the other: the
 * first attempt refusing is the normal path, not an error, and reporting it
 * names a script that goes on to load fine. The second attempt should stay loud.
 *
 * Decode-only. `RSCache_ClientScriptEncodeFlags` never sees this bit.
 */
#define RSCACHE_CLIENTSCRIPT_DECODE_QUIET 2

/**
 * Trailer flag for this cache.
 *
 * OSRS >= 237 uses the modern 16-byte trailer (long locals/args). Everything
 * else — including unidentified profiles — stays on the legacy 12-byte trailer.
 * The decoder is a *try* that validates the footer, so a wrong guess still fails
 * cleanly rather than silently misparsing the body.
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
 * `PUSH_CONSTANT_LONG` (LCONST) is stored in `long_operands` as a full int64.
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

/**
 * Release what a script owns, without freeing the script.
 *
 * For one the caller did not allocate — `RSCache_CS2_Compile` fills a struct the
 * caller supplies, and that struct is usually on the stack. Passing it to
 * RSCache_ClientScriptFree frees a pointer malloc never returned.
 */
void
RSCache_ClientScriptFreeInplace(struct RSCache_ClientScript* script);

#endif
