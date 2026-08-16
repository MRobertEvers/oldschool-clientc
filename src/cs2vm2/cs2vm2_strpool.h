#ifndef CS2VM2_STRPOOL_H
#define CS2VM2_STRPOOL_H

#include <stdarg.h>
#include <stddef.h>

/*
 * CS2VM2_StrPool — bump allocator for the strings a running script produces.
 *
 * Every CS2 string that reaches the operand stack, a frame's string locals, or a
 * host request is heap allocated, and the VM handed ownership around by hand:
 * PopStr transferred ownership to the popping opcode, which then had to free the
 * buffer, push it on again, or hand it to the host as a borrow. That convention
 * is unenforceable across ~200 opcode handlers and ~30 host push sites, so in
 * practice it leaked (StringLength, Append, Compare, Substring and every frame
 * local dropped by PushCallScript's frame memset all discarded their inputs
 * without freeing) and in a couple of places it needed delicate reasoning to
 * avoid the opposite failure — see the old CS2VM2_Op_OC_Find comment about
 * keeping a popped string alive across a yield-and-replay.
 *
 * Instead: strings live in a pool owned by the thread that made them, and the
 * pool is released as a unit when a script starts (CS2VM2_ResetRuntime, which
 * CS2VM2_ThreadStart calls) or when the VM is torn down (CS2VM2_Free). A script
 * runs for at most CS2VM_MAX_CYCLES opcodes and the pool's lifetime brackets it,
 * so nothing needs to decide who frees what:
 *
 *   - allocate with CS2VM2_StrDup / _StrFmt / _StrAlloc (see cs2vm2.h) instead
 *     of strdup / malloc,
 *   - never free a string that came off the string stack or out of a frame local,
 *   - a host may borrow a request's strings for the length of the call; to keep
 *     one past the script it must copy it (UITree_ApplyText, VarCManager_SetString
 *     and friends already do).
 *
 * Allocation is a pointer bump inside a block, so it is also cheaper than the
 * per-string malloc/free pairs it replaces.
 */

/* Payload bytes per regular block. Sized so a typical script's strings (widget
 * text, item names, joined labels — tens of bytes each) fit in one or two
 * blocks. Requests larger than this get an exact-fit block of their own. */
#define CS2VM2_STRPOOL_BLOCK_BYTES 8192u

struct CS2VM2_StrPoolBlock;

struct CS2VM2_StrPool
{
    /* Block list, newest first; the head is the one being bumped. */
    struct CS2VM2_StrPoolBlock* blocks;

    /* Stats, all since the last reset except bytes_peak (since Init). */
    size_t bytes_used;
    size_t bytes_reserved;
    size_t bytes_peak;
    int alloc_count;
    int block_count;
    int reset_count;
};

/* Prepare a pool whose memory is uninitialised (does not read the old state, so
 * it is safe on a freshly malloc'd VM — see the comment on Task_CS2Run.vm). */
void
CS2VM2_StrPool_Init(struct CS2VM2_StrPool* pool);

/* Release every string in the pool. One regular block is retained for reuse, so
 * a VM that runs script after script stops re-mallocing its first block. */
void
CS2VM2_StrPool_Reset(struct CS2VM2_StrPool* pool);

/* Release every string and every block; leaves the pool usable (as if Init'd). */
void
CS2VM2_StrPool_Free(struct CS2VM2_StrPool* pool);

/*
 * A writable buffer for a string of `len` characters: len + 1 bytes with
 * result[len] already NUL. The `len` characters themselves are uninitialised —
 * the caller fills them. Never returns NULL (asserts if the block malloc fails,
 * matching how the VM already treats allocation failure).
 */
char*
CS2VM2_StrPool_Alloc(
    struct CS2VM2_StrPool* pool,
    size_t len);

/* strdup into the pool. NULL in, NULL out — callers rely on this to propagate a
 * NULL string local without a ternary. */
char*
CS2VM2_StrPool_Dup(
    struct CS2VM2_StrPool* pool,
    char const* text);

/* Dup exactly `len` bytes (no embedded-NUL scan) and terminate. */
char*
CS2VM2_StrPool_DupLen(
    struct CS2VM2_StrPool* pool,
    char const* text,
    size_t len);

/* printf into the pool, sized to fit — no truncation. */
char*
CS2VM2_StrPool_Fmt(
    struct CS2VM2_StrPool* pool,
    char const* fmt,
    ...);

char*
CS2VM2_StrPool_VFmt(
    struct CS2VM2_StrPool* pool,
    char const* fmt,
    va_list args);

/* A fresh, writable "". Distinct storage per call on purpose: opcodes such as
 * UPPERCASE mutate their string in place, so no two stack slots may alias. */
char*
CS2VM2_StrPool_Empty(struct CS2VM2_StrPool* pool);

#endif /* CS2VM2_STRPOOL_H */
