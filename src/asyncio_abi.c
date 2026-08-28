/*
 * The IO queue's own binary layout, published for an executor that is not
 * written in C.
 *
 * The architecture is [game -> IO Queue] :> [platform IO executor], and on the
 * browser that executor is JavaScript (platform/platform_web_io.js). It reads
 * the queue out of the wasm heap directly -- that is what "reads the queue"
 * means when the reader has no struct declarations -- so it needs the offset of
 * every field it touches.
 *
 * WHY THIS LIVES WITH THE QUEUE, not with the platform. The layout is the
 * QUEUE's contract, not any one executor's: a second non-C executor would need
 * the same numbers, and a field added to ToriRS_IOItem changes them for
 * everybody. Putting it beside the platform that happens to read it today would
 * make the next platform copy it.
 *
 * WHY IT IS REPORTED AT RUNTIME rather than generated at build time. These
 * offsets come from the very module that is running, so they cannot be stale.
 * A build-time generator can be run against different flags than the module it
 * is paired with -- a different -m32/-m64, a different packing, an
 * #if that moved a field -- and the failure mode is not a build error but a
 * silently misread queue: an item whose `kind` is read out of the middle of a
 * path, dispatched to the wrong loader, filling the wrong slot.
 *
 * The reader checks TORIRS_IO_ABI_MAGIC and TORIRS_IO_ABI_COUNT before trusting
 * any of it, so a field added here without updating the reader stops the page
 * with a message instead of corrupting reads.
 */

#include "asyncio.h"

#include <stddef.h>
#include <stdint.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

/*
 * Only the browser has a non-C executor today, so only there is this reachable.
 * It is not #if'd because it would not COMPILE elsewhere -- it is portable C --
 * but because an exported symbol nothing calls is dead weight in a desktop
 * link, and the guard says which platform is expected to grow one next.
 */
#if defined(__EMSCRIPTEN__)

/*
 * Slot order. Append only, and bump TORIRS_IO_ABI_COUNT with the reader.
 *
 * Deliberately a flat int array rather than a struct: the whole point is to
 * describe a layout to something that cannot read a struct declaration, so
 * answering with one would need a second layout description for the answer.
 */
enum
{
    TORIRS_IO_ABI_MAGIC_SLOT = 0,
    TORIRS_IO_ABI_IO_SIZE,
    TORIRS_IO_ABI_IO_SLOTS_OFF,
    TORIRS_IO_ABI_IO_ACTIVE_OFF,
    TORIRS_IO_ABI_IO_ACTIVE_COUNT_OFF,
    TORIRS_IO_ABI_MAX_ITEMS,
    TORIRS_IO_ABI_ITEM_SIZE,
    TORIRS_IO_ABI_ITEM_KIND_OFF,
    TORIRS_IO_ABI_ITEM_ERROR_OFF,
    TORIRS_IO_ABI_ITEM_DATA_OFF,
    TORIRS_IO_ABI_ITEM_DATA_SIZE_OFF,
    TORIRS_IO_ABI_ITEM_U_OFF,
    TORIRS_IO_ABI_CACHE_EPOCH_OFF,
    TORIRS_IO_ABI_CACHE_TABLE_OFF,
    TORIRS_IO_ABI_CACHE_ARCHIVE_OFF,
    TORIRS_IO_ABI_CACHE_FLAGS_OFF,
    TORIRS_IO_ABI_CONFIG_PATH_OFF,
    TORIRS_IO_ABI_SCRIPT_PATH_OFF,
    TORIRS_IO_ABI_REFTABLE_TABLE_OFF,
    TORIRS_IO_ABI_FILE_PATH_OFF,
    TORIRS_IO_ABI_MAX_PATH,

    TORIRS_IO_ABI_COUNT
};

/* Changes with the slot order above, so a reader built against a different
 * order refuses rather than misreads. */
/*
 * IOA2, not IOA1: `io_slots` and `active` used to BE the arrays and are now
 * POINTERS to them (the table grows on demand). A reader that indexes the old
 * way would read the item table out of two pointers and a length -- so the
 * magic changes, and a stale reader stops with a message instead.
 */
#define TORIRS_IO_ABI_MAGIC 0x494f4132 /* "IOA2" */

/**
 * Fill `out` with TORIRS_IO_ABI_COUNT int32 values, in the order above.
 *
 * The caller allocates; this writes exactly that many and nothing else, which
 * is why the count is a compile-time constant the reader also knows. It reads
 * no state and may be called before anything is initialised -- these are
 * properties of the TYPES, not of any queue.
 */
EMSCRIPTEN_KEEPALIVE void
ToriRS_IO_DescribeAbi(int32_t* out)
{
    if( !out )
        return;

    out[TORIRS_IO_ABI_MAGIC_SLOT] = TORIRS_IO_ABI_MAGIC;

    out[TORIRS_IO_ABI_IO_SIZE] = (int32_t)sizeof(struct ToriRS_IO);
    out[TORIRS_IO_ABI_IO_SLOTS_OFF] = (int32_t)offsetof(struct ToriRS_IO, io_slots);
    out[TORIRS_IO_ABI_IO_ACTIVE_OFF] = (int32_t)offsetof(struct ToriRS_IO, active);
    out[TORIRS_IO_ABI_IO_ACTIVE_COUNT_OFF] =
        (int32_t)offsetof(struct ToriRS_IO, active_count);
    /* The table's opening size, not a ceiling -- the reader takes the base
     * pointer out of the struct on every access and never needs the length. */
    out[TORIRS_IO_ABI_MAX_ITEMS] = TORIRS_IO_MAX_ITEMS;

    out[TORIRS_IO_ABI_ITEM_SIZE] = (int32_t)sizeof(struct ToriRS_IOItem);
    out[TORIRS_IO_ABI_ITEM_KIND_OFF] = (int32_t)offsetof(struct ToriRS_IOItem, kind);
    out[TORIRS_IO_ABI_ITEM_ERROR_OFF] = (int32_t)offsetof(struct ToriRS_IOItem, error_code);
    out[TORIRS_IO_ABI_ITEM_DATA_OFF] = (int32_t)offsetof(struct ToriRS_IOItem, data);
    out[TORIRS_IO_ABI_ITEM_DATA_SIZE_OFF] = (int32_t)offsetof(struct ToriRS_IOItem, data_size);
    out[TORIRS_IO_ABI_ITEM_U_OFF] = (int32_t)offsetof(struct ToriRS_IOItem, u);

    /* Offsets WITHIN the union member, so the reader adds them to ITEM_U_OFF.
     * Reported separately rather than folded in because a union member is a
     * type of its own and may gain a field without the union moving. */
    out[TORIRS_IO_ABI_CACHE_EPOCH_OFF] = (int32_t)offsetof(struct IOItem_Cache, epoch);
    out[TORIRS_IO_ABI_CACHE_TABLE_OFF] = (int32_t)offsetof(struct IOItem_Cache, table_id);
    out[TORIRS_IO_ABI_CACHE_ARCHIVE_OFF] = (int32_t)offsetof(struct IOItem_Cache, archive_id);
    out[TORIRS_IO_ABI_CACHE_FLAGS_OFF] = (int32_t)offsetof(struct IOItem_Cache, flags);
    out[TORIRS_IO_ABI_CONFIG_PATH_OFF] = (int32_t)offsetof(struct IOItem_ConfigFile, path);
    out[TORIRS_IO_ABI_SCRIPT_PATH_OFF] = (int32_t)offsetof(struct IOItem_Script, path);
    out[TORIRS_IO_ABI_REFTABLE_TABLE_OFF] =
        (int32_t)offsetof(struct IOItem_ReferenceTable, table_id);
    out[TORIRS_IO_ABI_FILE_PATH_OFF] = (int32_t)offsetof(struct IOItem_File, path);
    out[TORIRS_IO_ABI_MAX_PATH] = TORIRS_IOITEM_MAX_PATH;
}

/** How many int32 slots ToriRS_IO_DescribeAbi writes. Asked first, so the
 *  reader can allocate without agreeing the count in advance -- the magic is
 *  what catches an ORDER change, this catches a LENGTH change. */
EMSCRIPTEN_KEEPALIVE int
ToriRS_IO_DescribeAbiCount(void)
{
    return TORIRS_IO_ABI_COUNT;
}

#endif /* __EMSCRIPTEN__ */
