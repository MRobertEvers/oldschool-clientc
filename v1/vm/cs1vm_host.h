#ifndef CS1VM_HOST_H
#define CS1VM_HOST_H

#include "cs1vm.h"

struct ToriAuxLibVM;

/** Fill varp/varbit accessors from ToriAuxLibVM. Other callbacks remain NULL. */
void
cs1vm_host_fill_varp_varbit(
    struct CS1Host* out,
    struct ToriAuxLibVM* vm);

#endif
