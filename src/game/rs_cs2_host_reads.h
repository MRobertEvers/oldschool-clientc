#ifndef GAME_RS_CS2_HOST_READS_H
#define GAME_RS_CS2_HOST_READS_H

/*
 * THE CHEAP READS: the game side of the VM's second way into the host.
 *
 * WHAT IT IS. RS_CS2Host_Read, bound as the VM's host_read
 * (CS2VM2_BindHostReads, next to every CS2VM2_BindHost(.., RS_CS2Host_Exec)
 * that runs the client's scripts: task_cs2_run.c and the plugin bridge). It
 * answers INV_GETOBJ, INV_GETNUM and PUSH_VARBIT and nothing else, and the
 * three reads themselves live here, so RS_CS2Host_Exec's own cases for them
 * (kept for a VM bound without the reader) give the same answers.
 *
 * WHAT IT REPLACES. Those three went through RS_CS2Host_Exec like every
 * request: its entry (the perf counters and the timing depth), then a switch
 * over ~900 kinds, to load one int. 16,800 such reads per bank open cost as
 * much in the trip as in the read.
 *
 * WHAT IT ASSUMES. None of the three can yield: an inventory and the varps
 * are in memory. A read therefore never needs the await record
 * (thread->has_awaited) that RS_CS2Host_Exec retires on every non-yield
 * answer: an instruction that yielded is retried through RS_CS2Host_Exec,
 * which retires it there. Nothing is remembered, so nothing invalidates.
 */

#include "game/rs_cs2_host.h"
#include "inv/inv_manager.h"
#include "varp/varp_manager.h"

#include <assert.h>

struct CS2VM2_Thread;
struct CS2VM_HostRequest;

/* INV_GETOBJ: the obj in `slot` of `inv_id`, -1 for none (obj 0 is a real
 * item; InvManager's empty sentinel is -1 too). */
static inline int
RS_CS2Host_InvGetObj(
    struct RS_CS2Host* host,
    int inv_id,
    int slot)
{
    int obj;
    assert(host);
    assert(host->invs);
    if( inv_id < 0 || slot < 0 )
        return -1;
    obj = InvManager_GetObj(host->invs, inv_id, slot);
    /* Scripts expect -1 for empty (reference INV_GETOBJ pushes -1 when the
     * inv or slot has no item). */
    if( obj <= INV_MANAGER_EMPTY_OBJ_ID )
        return -1;
    return obj;
}

/* INV_GETNUM: the count in `slot` of `inv_id`, 0 for none. */
static inline int
RS_CS2Host_InvGetNum(
    struct RS_CS2Host* host,
    int inv_id,
    int slot)
{
    assert(host);
    assert(host->invs);
    if( inv_id < 0 || slot < 0 )
        return 0;
    return InvManager_GetNum(host->invs, inv_id, slot);
}

/* PUSH_VARBIT: the varbit's value. */
static inline int
RS_CS2Host_VarbitGet(
    struct RS_CS2Host* host,
    int varbit_id)
{
    assert(host);
    assert(host->varps);
    return VarPManager_GetVarbit(host->varps, varbit_id);
}

/* The VM's host_read for the client's scripts (see above). Expects
 * CS2VM_USER(thread) == RS_CS2Host* and one of the three kinds. */
int
RS_CS2Host_Read(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request);

#endif
