/*
 * CS1VM script container helpers.
 */

#include "cs1vm_script.h"

#include <assert.h>
#include <stddef.h>

int
CS1VM_ComponentScriptGet(
    struct CS1VM_ComponentScripts const* set,
    int index,
    struct CS1VM_Script* out)
{
    assert(set);
    assert(out);

    out->ops = NULL;
    out->op_count = 0;

    if( index < 0 || index >= set->scripts_count )
        return -1;
    if( !set->scripts || !set->scripts_lengths )
        return -1;
    if( !set->scripts[index] || set->scripts_lengths[index] <= 0 )
        return -1;

    out->ops = set->scripts[index];
    out->op_count = set->scripts_lengths[index];
    return 0;
}
