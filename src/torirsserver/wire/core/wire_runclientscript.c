/*
 * RUNCLIENTSCRIPT: what this server refuses to send, then rsprot's encoder.
 *
 * The bytes are rsprot's (3rd/rsprot/packets/runclientscript.c,
 * packet_runclientscript_v1_out, one layout for 221..242). What is left here is
 * the server's side of it: the golden client's limits, and turning the
 * server's argument list into rsprot's message.
 */

#include "torirsserver/wire/core/wire_runclientscript.h"

#include "packets/runclientscript.h"
#include "rsprot_buf.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int
wire_encode_runclientscript(
    struct RsprotBuf* buf,
    int script_id,
    char const* types,
    struct WireClientScriptArg const* args,
    int argc)
{
    char type_string[WIRE_RUNCLIENTSCRIPT_ARG_MAX + 1];
    MsgRunClientScriptArg margs[WIRE_RUNCLIENTSCRIPT_ARG_MAX];
    MsgRunClientScript m;
    RsprotExec x;

    if( !buf || argc < 0 || argc > WIRE_RUNCLIENTSCRIPT_ARG_MAX || (argc > 0 && !args) ||
        (types && strlen(types) != (size_t)argc) )
        return 0;

    memset(margs, 0, sizeof(margs));
    for( int i = 0; i < argc; i++ )
    {
        char const type = types && types[i] ? types[i] : 'i';
        struct WireClientScriptArg const* arg = &args[i];
        MsgRunClientScriptArg* out = &margs[i];

        type_string[i] = type;
        if( type == 'W' || type == 'X' )
        {
            if( arg->array_count < 0 || arg->array_count > WIRE_RUNCLIENTSCRIPT_ARRAY_MAX )
                return 0;
            if( arg->array_count > 0 &&
                ((type == 'W' && !arg->int_array) || (type == 'X' && !arg->string_array)) )
                return 0;
            out->elem_count = arg->array_count;
            /* The encoder only reads these; rsprot's message is shared with the
             * decoder, which is why its pointers are not const. */
            out->ints = (int32_t*)(uintptr_t)arg->int_array;
            out->strs = (char const**)(uintptr_t)arg->string_array;
        }
        out->ival = arg->int_value;
        out->lval = arg->long_value;
        out->sval = arg->string_value;
    }
    type_string[argc] = '\0';

    m.types = type_string;
    m.id = script_id;
    m.args = margs;
    rsprot_exec_encode(&x, buf);
    packet_runclientscript_v1_out(&x, &m);
    return rsprot_exec_ok(&x) && rsprot_buf_ok(buf) ? 1 : 0;
}
