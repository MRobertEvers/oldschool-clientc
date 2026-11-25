#ifndef CS2VM2_CALLEE_H
#define CS2VM2_CALLEE_H

/*
 * REMEMBERED GOSUB CALLEES.
 *
 * WHAT IT IS. A side table on a script (script->callees, one slot per pc,
 * made at load by CS2VM2_ScriptPrepare for a script that has a
 * GOSUB_WITH_PARAMS): the script the host pushed the last time the gosub at
 * that pc ran. The script's own bytes are untouched -- this is derived data
 * beside them, like prepared_ops and the jit overlay.
 *
 * WHAT IT REPLACES. Every GOSUB_WITH_PARAMS asked the host which script its
 * id names (CS2VM2_Ask -> RS_CS2Host_Exec -> exec_push_script ->
 * CacheProvider_ClientScriptGet -> CS2VM2_PushCallScript): 10,211 trips per
 * bank open, each to learn the answer it got the time before. The first call
 * at a pc still asks -- a callee that is not resident yields for its load
 * exactly as it always did, and nothing is remembered until a host has
 * actually pushed one. Every later call pushes the remembered script in the
 * VM (CS2VM2_Op_GosubWithParams).
 *
 * WHY IT CANNOT OUTLIVE ITS CALLEE. A provider never replaces a cached script
 * (CacheProvider_ClientScriptAdd keeps the first decode); scripts only die
 * through CS2VM2_ScriptFree, and every ScriptFree moves one process-wide
 * epoch (CS2VM2_CalleeScriptRetired). A table is good for the epoch it was
 * written under: once any script has been freed, every table reads empty
 * until its own next ask refills it (CS2VM2_CalleeRemember clears a stale
 * table first). Freeing is rare -- a provider's teardown, a duplicate decode
 * dropped on arrival -- so the cost is one ask per gosub site after one. A
 * caller's own table dies with the caller (CS2VM2_CalleeFree from
 * CS2VM2_ScriptFree), and CS2VM2_ScriptPrepare, which a test calls again
 * after changing an opcode, empties it.
 *
 * WHAT IS ASSUMED OF A HOST. That while no script has been freed, a gosub
 * from the same instruction resolves to the same script -- true of a host
 * that answers from a CacheProvider, which is every host in the tree.
 */

#include "cs2vm2_script.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/* Moves whenever any script is freed (CS2VM2_CalleeScriptRetired). */
extern uint32_t g_cs2vm2_script_epoch;

/* The callee remembered for the gosub at `pc` of `script`, or NULL: none yet,
 * or a script has been freed since it was written. `script` has a table
 * whenever it has a gosub (CS2VM2_ScriptPrepare). */
static inline struct CS2VM2_Script*
CS2VM2_CalleeRemembered(
    struct CS2VM2_Script const* script,
    int pc)
{
    assert(script->callees);
    assert(pc >= 0);
    assert(pc < script->op_count);
    if( script->callees_epoch != g_cs2vm2_script_epoch )
        return NULL;
    return script->callees[pc];
}

/* Remember that the gosub at `pc` of `script` pushed `callee`. */
void
CS2VM2_CalleeRemember(
    struct CS2VM2_Script* script,
    int pc,
    struct CS2VM2_Script* callee);

/* Load time (CS2VM2_ScriptPrepare): give `script` an empty table when it has a
 * GOSUB_WITH_PARAMS, none when it has not; empty an existing one. */
void
CS2VM2_CalleePrepare(struct CS2VM2_Script* script);

/* `script` is being freed: release its table and make every remembered callee
 * anywhere stale (it may have been one). */
void
CS2VM2_CalleeScriptRetired(struct CS2VM2_Script* script);

#endif
