#ifndef SRC_EDITOR_EDITOR_JM2_H
#define SRC_EDITOR_EDITOR_JM2_H

/**
 * The `.jm2` / `.jl2` text codec.
 *
 * These are the map sources in OSRS-Content: one square per pair, terrain in
 * the `.jm2` and scenery in the `.jl2`, both plain text keyed `<level> <x>
 * <z>:`. cachepack already reads and writes them (tools/cachepack/cp_decode.c)
 * on the way in and out of the binary cache; this is the same grammar spelled
 * for the editor, which needs to parse and emit them in-process rather than
 * shelling out.
 *
 * The emitter is byte-exact against cachepack's for any square it did not
 * edit — same token order (h, o, f, u), same omissions, same `trailing=` line,
 * same foreign-section passthrough. That is what makes `git diff` on a saved
 * square show the edit and nothing else, and it is what Editor_Jm2RoundTrips
 * exists to prove over the whole content tree.
 *
 * Malformed text is data, not a contract violation: the parsers return an
 * error with the offending line rather than asserting. NULL arguments are
 * contract violations and do assert.
 */

#include "editor_types.h"

#include <stddef.h>
#include <stdio.h>

enum Editor_ParseStatus
{
    EDITOR_PARSE_OK = 0,
    /** The `==== MAP ====` / `==== LOC ====` header was missing. */
    EDITOR_PARSE_BAD_HEADER,
    /** A tile/loc line did not match `<level> <x> <z>: <tokens>`. */
    EDITOR_PARSE_BAD_LINE,
    /** A coordinate was outside the square, or a level outside 0..3. */
    EDITOR_PARSE_BAD_COORD,
    /** An unrecognised token, or a value too wide for its field. */
    EDITOR_PARSE_BAD_TOKEN,
};

struct Editor_ParseResult
{
    enum Editor_ParseStatus status;
    /** 1-based line the failure was on, 0 when status is OK. */
    int line;
};

/**
 * Parse `.jm2` text into the square's authored tiles, `trailing`, and
 * `foreign`. Existing tile state is cleared first, so a re-parse is a reload.
 *
 * `text` need not be NUL-terminated; `length` bounds it.
 */
struct Editor_ParseResult
Editor_Jm2Parse(
    struct Editor_Square* square,
    const char* text,
    size_t length);

/** Parse `.jl2` text into the square's loc list, replacing it. */
struct Editor_ParseResult
Editor_Jl2Parse(
    struct Editor_Square* square,
    const char* text,
    size_t length);

/**
 * Emit the square's `.jm2`.
 *
 * Writes into `out` when it fits and always returns the length the full text
 * would be, so the caller can size a buffer with a NULL/0 probe call and then
 * emit for real — the same shape as snprintf. The result is NUL-terminated
 * whenever any byte fit.
 */
size_t
Editor_Jm2Emit(
    const struct Editor_Square* square,
    char* out,
    size_t out_capacity);

/** Emit the square's `.jl2`. Same sizing contract as Editor_Jm2Emit. */
size_t
Editor_Jl2Emit(
    const struct Editor_Square* square,
    char* out,
    size_t out_capacity);

#endif
