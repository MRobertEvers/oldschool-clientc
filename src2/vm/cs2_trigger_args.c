#include "cs2_trigger_args.h"

#include <string.h>

bool
cs2_trigger_args_parse(
    struct CS2_InvokeCtx* ctx,
    struct CS2TriggerArgs* out)
{
    if( out )
        memset(out, 0, sizeof(*out));
    if( !ctx || !out )
        return false;

    char* raw_signature = NULL;
    if( !cs2vm_host_try_pop_string(ctx, &raw_signature) || !raw_signature )
        return false;

    char signature_buf[64];
    strncpy(signature_buf, raw_signature, sizeof(signature_buf) - 1);
    signature_buf[sizeof(signature_buf) - 1] = '\0';
    char* signature = signature_buf;

    int trigger_count = 0;
    int triggers[CS2_TRIGGER_COUNT_MAX] = { 0 };
    size_t sig_len = strlen(signature);
    if( sig_len > 0 && signature[sig_len - 1] == 'Y' )
    {
        signature[sig_len - 1] = '\0';
        if( !cs2vm_host_try_pop_int(ctx, &trigger_count) )
            return false;
        if( trigger_count > CS2_TRIGGER_COUNT_MAX )
            trigger_count = CS2_TRIGGER_COUNT_MAX;
        for( int i = trigger_count - 1; i >= 0; i-- )
        {
            if( !cs2vm_host_try_pop_int(ctx, &triggers[i]) )
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
            if( !cs2vm_host_try_pop_int(ctx, &argv[i]) )
                return false;
        }
        else
        {
            char* ignored = NULL;
            if( !cs2vm_host_try_pop_string(ctx, &ignored) )
                return false;
        }
    }

    int script_id = 0;
    if( !cs2vm_host_try_pop_int(ctx, &script_id) )
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
