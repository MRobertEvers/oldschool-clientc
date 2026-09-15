/* The full revision-239 RUNCLIENTSCRIPT argument codec. */

#include "mock239_runclientscript.h"

#include <rsareabuf.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* class617.method13149: little-endian groups of seven payload bits. */
static void
put_varuint(
    struct RSAreaBuf* buf,
    uint32_t value)
{
    do
    {
        uint32_t byte = value & 0x7fu;
        value >>= 7;
        if( value )
            byte |= 0x80u;
        rsab_p1(buf, (int32_t)byte);
    } while( value );
}

/* class617.method13213 reverses this with `u >>> 1 ^ -(u & 1)`. */
static uint32_t
zigzag32(int32_t value)
{
    return ((uint32_t)value << 1) ^ (uint32_t)-(value < 0);
}

int
mock239_encode_runclientscript(
    struct RSAreaBuf* buf,
    int script_id,
    char const* types,
    struct Mock239ClientScriptArg const* args,
    int argc)
{
    if( !buf || argc < 0 || argc > MOCK239_RUNCLIENTSCRIPT_ARG_MAX || (argc > 0 && !args) ||
        (types && strlen(types) != (size_t)argc) )
        return 0;

    for( int i = 0; i < argc; i++ )
        rsab_p1(buf, types && types[i] ? (uint8_t)types[i] : (uint8_t)'i');
    rsab_p1(buf, 0);

    for( int i = argc - 1; i >= 0; i-- )
    {
        uint8_t type = types && types[i] ? (uint8_t)types[i] : (uint8_t)'i';
        struct Mock239ClientScriptArg const* arg = &args[i];

        if( type == (uint8_t)'W' || type == (uint8_t)'X' )
        {
            if( arg->array_count < 0 || arg->array_count > MOCK239_RUNCLIENTSCRIPT_ARRAY_MAX )
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
                    rsab_pjstr(buf, arg->string_array[j] ? arg->string_array[j] : "",
                               RSAB_JSTR_NUL);
            }
        }
        else if( type == (uint8_t)'s' )
        {
            rsab_pjstr(buf, arg->string_value ? arg->string_value : "", RSAB_JSTR_NUL);
        }
        else if( type == 0xcfu )
        {
            rsab_p8(buf, arg->long_value);
        }
        else
        {
            rsab_p4(buf, arg->int_value);
        }
    }

    rsab_p4(buf, script_id);
    return rsab_ok(buf) ? 1 : 0;
}
