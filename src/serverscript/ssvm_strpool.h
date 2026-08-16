#ifndef SRC_SERVERSCRIPT_SSVM_STRPOOL_H
#define SRC_SERVERSCRIPT_SSVM_STRPOOL_H

/*
 * Bump allocator for every string a running script derives.
 *
 * Scripts produce strings constantly (join, append, tostring) and free none of
 * them, so individual malloc/free is the wrong shape: the natural lifetime is
 * "as long as the script runs". A pool makes that lifetime explicit and turns
 * teardown into one pass over a block list.
 *
 * The lifetime is the STATE, not the call — and that is the one place this
 * differs from src/cs2vm2/cs2vm2_strpool.c, which resets at script start. A CS2
 * script runs to completion inside a single call. A ServerScript state gets
 * parked across ticks: a chat dialogue builds a page of text, suspends on
 * p_pausebutton, and reads that text back when the player clicks minutes later.
 * Resetting at script start would free it out from under the resumed script.
 *
 * Rules for anything holding a `char*` that came from here:
 *   - never free it; the pool owns it
 *   - never keep it past SSVM_StrPoolFree
 *   - a host that wants to retain one must copy it
 */

#include <stddef.h>

enum
{
    /* Big enough that ordinary scripts use one block. */
    SSVM_STRPOOL_BLOCK_BYTES = 8192,

    /* A runaway `while` doing append() can allocate without bound inside the
     * 500,000-op budget, so the pool needs its own ceiling. Hitting it is an
     * error, not a silent truncation. */
    SSVM_STRPOOL_MAX_BYTES = 1 << 20,
};

struct SSVM_StrPoolBlock;

struct SSVM_StrPool
{
    struct SSVM_StrPoolBlock* head;
    size_t total_bytes;
    /** Sticky: the pool refused an allocation. The VM turns this into an
     *  aborted script rather than letting a NULL propagate. */
    int exhausted;
};

void
SSVM_StrPoolInit(struct SSVM_StrPool* pool);

/** Releases every block. Any pointer handed out becomes invalid. */
void
SSVM_StrPoolFree(struct SSVM_StrPool* pool);

/** `length` bytes plus a terminator, zeroed. NULL when exhausted. */
char*
SSVM_StrPoolAlloc(
    struct SSVM_StrPool* pool,
    size_t length);

/** NULL in gives the empty string, never NULL out (unless exhausted). */
char*
SSVM_StrPoolDup(
    struct SSVM_StrPool* pool,
    const char* text);

char*
SSVM_StrPoolDupLen(
    struct SSVM_StrPool* pool,
    const char* text,
    size_t length);

/** Concatenate two strings into one pool allocation. */
char*
SSVM_StrPoolCat(
    struct SSVM_StrPool* pool,
    const char* left,
    const char* right);

char*
SSVM_StrPoolFmt(
    struct SSVM_StrPool* pool,
    const char* fmt,
    ...);

#endif
