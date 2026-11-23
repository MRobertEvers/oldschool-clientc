#ifndef TASK_CS2_JIT_PACK_H
#define TASK_CS2_JIT_PACK_H

/*
 * The two tasks of the CS2 jit's platform cache (docs/CS2_JIT.md 4.1, 4.7):
 * the boot read of the pack and the write-behind store. Both are ORDINARY
 * tasks -- never added with TaskRunner_AddRenderBlockingSerialTask -- so
 * neither sets `render_blocking` or `frame_settle_pending`, and a pack read or
 * write still on its way never holds a frame or the CS2 settle gate. Nothing
 * joins either of them, and no script ever waits for them: a script that
 * loads first is given an overlay built in memory.
 *
 * Both are protothreads with no switch between PT_BEGIN and PT_END
 * (check-pt-switch), and each resumes only once its one IO item is answered.
 */

#include "asyncio.h"
#include "engine/cs2_jit_cache.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Read the pack (DERIVED_READ) and hand the answer to the cache. For the
 * parallel frame queue, queued once at boot after the cache profile is set.
 * The cache must outlive the task's last run; the task owns nothing of it.
 */
struct ToriRS_Task*
CreateTask_CS2JitPackLoad(struct CS2JitCache* cache);

/*
 * Plan, serialize in units of CS2JIT_STORE_UNIT_SCRIPTS (yielding between
 * units when the background queue's slice is spent), DERIVED_WRITE, then
 * CS2JitCache_StoreDone. Writes nothing when nothing changed. The caller has
 * called CS2JitCache_StoreBegin (CS2JitCache_MaybeStore does).
 */
struct ToriRS_Task*
CreateTask_CS2JitPackStore(
    struct CS2JitCache* cache,
    CS2JitScriptLookup lookup,
    void* lookup_user);

/*
 * Once per frame: when CS2JitCache_StoreWanted says so, begin a store and
 * queue its task on `background`. Returns whether it did.
 */
bool
CS2JitCache_MaybeStore(
    struct CS2JitCache* cache,
    uint64_t now_ms,
    struct ToriRS_TaskQueue* background,
    CS2JitScriptLookup lookup,
    void* lookup_user);

#endif /* TASK_CS2_JIT_PACK_H */
