#include "cs2_trigger_args.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

bool
cs2_trigger_args_parse(
    struct CS2VMX* vm,
    struct CS2TriggerArgs* out)
{
    assert(vm && out);
    memset(out, 0, sizeof(*out));

    char* raw_signature = NULL;
    if( CS2VMX_PopStr(vm, &raw_signature) != CS2VM_EXECNO_OK || !raw_signature )
        return false;

    char signature_buf[64];
    strncpy(signature_buf, raw_signature, sizeof(signature_buf) - 1);
    signature_buf[sizeof(signature_buf) - 1] = '\0';
    free(raw_signature);
    char* signature = signature_buf;

    int trigger_count = 0;
    int triggers[CS2_TRIGGER_COUNT_MAX] = { 0 };
    size_t sig_len = strlen(signature);
    if( sig_len > 0 && signature[sig_len - 1] == 'Y' )
    {
        signature[sig_len - 1] = '\0';
        if( CS2VMX_PopInt(vm, &trigger_count) != CS2VM_EXECNO_OK )
            return false;
        if( trigger_count > CS2_TRIGGER_COUNT_MAX )
            trigger_count = CS2_TRIGGER_COUNT_MAX;
        for( int i = trigger_count - 1; i >= 0; i-- )
        {
            if( CS2VMX_PopInt(vm, &triggers[i]) != CS2VM_EXECNO_OK )
                return false;
        }
    }

    int argv[CS2_TRIGGER_ARGV_MAX] = { 0 };
    int argc = (int)strlen(signature);
    if( argc > CS2_TRIGGER_ARGV_MAX )
        argc = CS2_TRIGGER_ARGV_MAX;
    for( int i = argc - 1; i >= 0; i-- )
    {
        if( signature[i] == 'i' )
        {
            if( CS2VMX_PopInt(vm, &argv[i]) != CS2VM_EXECNO_OK )
                return false;
        }
        else
        {
            char* ignored = NULL;
            if( CS2VMX_PopStr(vm, &ignored) != CS2VM_EXECNO_OK )
                return false;
            free(ignored);
        }
    }

    int script_id = 0;
    if( CS2VMX_PopInt(vm, &script_id) != CS2VM_EXECNO_OK )
        return false;

    out->script_id = script_id;
    out->argc = argc;
    memcpy(out->argv, argv, (size_t)argc * sizeof(int));
    out->trigger_count = trigger_count;
    memcpy(out->triggers, triggers, (size_t)trigger_count * sizeof(int));
    out->is_clear = script_id < 0;
    out->ok = true;
    return true;
}
