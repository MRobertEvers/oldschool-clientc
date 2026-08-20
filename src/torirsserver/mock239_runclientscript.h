#ifndef SRC_NET_MOCK_MOCK239_RUNCLIENTSCRIPT_H
#define SRC_NET_MOCK_MOCK239_RUNCLIENTSCRIPT_H

#include <stdint.h>

struct RSAreaBuf;

enum
{
    MOCK239_RUNCLIENTSCRIPT_ARG_MAX = 28,
    /* The golden rev239 decoder reads a varuint count, but indexes both W/X
     * arrays with a byte local. At 128 that local wraps negative and the
     * compilable authoritative client indexes -128. Refuse the crash boundary
     * even though the count encoding itself could spell a larger value. */
    MOCK239_RUNCLIENTSCRIPT_ARRAY_MAX = 127,
};

/*
 * One RUNCLIENTSCRIPT argument in the rev239 wire type alphabet.
 *
 * The type string chooses the active member: `s` is string_value, `W` is an
 * int array, `X` is a string array, byte 0xcf is long_value, and every other
 * byte is int_value. Array pointers are borrowed for the duration of encode.
 */
struct Mock239ClientScriptArg
{
    int32_t int_value;
    int64_t long_value;
    char const* string_value;
    int32_t const* int_array;
    char const* const* string_array;
    int array_count;
};

/**
 * Write the authoritative revision-239 RUNCLIENTSCRIPT payload (not its
 * opcode/variable-length prefix). Arguments are written in reverse order,
 * exactly as the golden client pops them.
 *
 * Returns 1 when the argument model is valid and the destination did not
 * overflow, 0 for a negative/oversized count, invalid pointer, or short buffer.
 */
int
mock239_encode_runclientscript(
    struct RSAreaBuf* buf,
    int script_id,
    char const* types,
    struct Mock239ClientScriptArg const* args,
    int argc);

#endif
