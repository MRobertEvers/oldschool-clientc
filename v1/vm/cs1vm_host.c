#include "cs1vm_host.h"

#include "toriauxlib/vm/toriauxlibvm.h"

#include <string.h>
#include <assert.h>

static int
cs1vm_host_get_varp(
    void* ud,
    int id)
{
    return ToriAuxLibVM_GetVarp(ud, id);
}

static int
cs1vm_host_get_varbit(
    void* ud,
    int id)
{
    return ToriAuxLibVM_GetVarbit(ud, id);
}

void
cs1vm_host_fill_varp_varbit(
    struct CS1Host* out,
    struct ToriAuxLibVM* vm)
{
    assert(out);

    memset(out, 0, sizeof(*out));
    assert(vm);

    out->get_varp = cs1vm_host_get_varp;
    out->get_varbit = cs1vm_host_get_varbit;
    out->ud = vm;
}
