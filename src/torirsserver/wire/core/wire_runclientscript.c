/* The full revision-239 RUNCLIENTSCRIPT argument codec. */

#include "torirsserver/wire/core/wire_runclientscript.h"

#include "rsprot_buf.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* class617.method13149: little-endian groups of seven payload bits. */
static void
put_varuint(
    struct RsprotBuf* buf,
    uint32_t value)
{
    do
    {
        uint32_t byte = value & 0x7fu;
        value >>= 7;
        if( value )
            byte |= 0x80u;
        rsprot_p1(buf, (int32_t)byte);
    } while( value );
}

/* class617.method13213 reverses this with `u >>> 1 ^ -(u & 1)`. */
static uint32_t
zigzag32(int32_t value)
{
    return ((uint32_t)value << 1) ^ (uint32_t)-(value < 0);
}

int
wire_encode_runclientscript(
    struct RsprotBuf* buf,
    int script_id,
    char const* types,
    struct WireClientScriptArg const* args,
    int argc)
{
    if( !buf || argc < 0 || argc > WIRE_RUNCLIENTSCRIPT_ARG_MAX || (argc > 0 && !args) ||
        (types && strlen(types) != (size_t)argc) )
        return 0;

    for( int i = 0; i < argc; i++ )
        rsprot_p1(buf, types && types[i] ? (uint8_t)types[i] : (uint8_t)'i');
    rsprot_p1(buf, 0);

    for( int i = argc - 1; i >= 0; i-- )
    {
        uint8_t type = types && types[i] ? (uint8_t)types[i] : (uint8_t)'i';
        struct WireClientScriptArg const* arg = &args[i];

        if( type == (uint8_t)'W' || type == (uint8_t)'X' )
        {
            if( arg->array_count < 0 || arg->array_count > WIRE_RUNCLIENTSCRIPT_ARRAY_MAX )
                return 0;
            if( arg->array_count > 0 &&
                ((type == (uint8_t)'W' && !arg->int_array) ||
                 (type == (uint8_t)'X' && !arg->string_array)) )
                return 0;

            put_varuint(buf, (uint32_t)arg->array_count);
            if( type == (uint8_t)'W' )
            {
                for( int j = 0; j < arg->array_count; j++ )
                    put_varuint(buf, zigzag32(arg->int_array[j]));
            }
            else
            {
                for( int j = 0; j < arg->array_count; j++ )
                    rsprot_pjstr(buf, arg->string_array[j] ? arg->string_array[j] : "");
            }
        }
        else if( type == (uint8_t)'s' )
        {
            rsprot_pjstr(buf, arg->string_value ? arg->string_value : "");
        }
        else if( type == 0xcfu )
        {
            rsprot_p8_12345678(buf, arg->long_value);
        }
        else
        {
            rsprot_p4_1234(buf, arg->int_value);
        }
    }

    rsprot_p4_1234(buf, script_id);
    return rsprot_buf_ok(buf) ? 1 : 0;
}
