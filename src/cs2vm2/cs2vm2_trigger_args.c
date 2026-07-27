#include "cs2vm2_trigger_args.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

bool
CS2VM2_TriggerArgsParse(
    struct CS2VM2_Thread* thread,
    struct CS2TriggerArgs* out)
{
    assert(thread && out);
    memset(out, 0, sizeof(*out));

    char* raw_signature = NULL;
    if( CS2VM2_PopStr(thread, &raw_signature) != CS2VM_EXECNO_OK || !raw_signature )
        return false;

    /* Copied because the parse below rewrites the trailing 'Y'; the pool owns
     * the popped string either way. */
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
        if( CS2VM2_PopInt(thread, &trigger_count) != CS2VM_EXECNO_OK )
            return false;
        if( trigger_count > CS2_TRIGGER_COUNT_MAX )
            trigger_count = CS2_TRIGGER_COUNT_MAX;
        for( int i = trigger_count - 1; i >= 0; i-- )
        {
            if( CS2VM2_PopInt(thread, &triggers[i]) != CS2VM_EXECNO_OK )
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
            if( CS2VM2_PopInt(thread, &argv[i]) != CS2VM_EXECNO_OK )
                return false;
        }
        else
        {
            /* String args are not used here, only consumed off the stack. */
            char* ignored = NULL;
            if( CS2VM2_PopStr(thread, &ignored) != CS2VM_EXECNO_OK )
                return false;
        }
    }

    int script_id = 0;
    if( CS2VM2_PopInt(thread, &script_id) != CS2VM_EXECNO_OK )
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
