/* The cheap reads, asked past RS_CS2Host_Exec: see rs_cs2_host_reads.h. */
#include "game/rs_cs2_host_reads.h"

#include "cs2vm2/cs2vm2.h"

int
RS_CS2Host_Read(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct RS_CS2Host* const host = (struct RS_CS2Host*)CS2VM_USER(thread);

    assert(thread);
    assert(request);
    assert(host && "CS2VM_USER(thread) must be RS_CS2Host*");

    /* No switch: three kinds, the commonest first. */
    if( request->kind == CS2VM_HOST_REQUEST_INV_GETOBJ )
        return CS2VM2_PushInt(
            thread,
            RS_CS2Host_InvGetObj(host, request->u.INV_GETOBJ.inv_id, request->u.INV_GETOBJ.slot));
    if( request->kind == CS2VM_HOST_REQUEST_INV_GETNUM )
        return CS2VM2_PushInt(
            thread,
            RS_CS2Host_InvGetNum(host, request->u.INV_GETNUM.inv_id, request->u.INV_GETNUM.slot));
    assert(request->kind == CS2VM_HOST_REQUEST_PUSH_VARBIT);
    return CS2VM2_PushInt(thread, RS_CS2Host_VarbitGet(host, request->u.PUSH_VARBIT.varbit_id));
}
