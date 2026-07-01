#include "dat2a_clientscript.h"

#include "../shared/shared_rs_buffer.h"

#include <stdlib.h>
#include <string.h>

static int
read_u16_at(
    const uint8_t* data,
    int size,
    int pos)
{
    if( pos < 0 || pos + 1 >= size )
        return 0;
    return ((int)data[pos] << 8) | (int)data[pos + 1];
}

struct RSCacheDat2A_ClientScript*
RSCacheDat2A_ClientScriptNewDecode(
    int script_id,
    const uint8_t* data,
    int data_size)
{
    if( !data || data_size < 16 )
        return NULL;

    int trailer_len = read_u16_at(data, data_size, data_size - 2);
    int trailer_pos = data_size - 14 - trailer_len;
    if( trailer_pos < 0 || trailer_pos > data_size )
        return NULL;

    int op_count = 0;
    {
        struct RSCacheShared_RSBuffer trailer;
        RSCacheShared_RSBufferInit(&trailer, (uint8_t*)data + trailer_pos, (uint32_t)(data_size - trailer_pos));
        op_count = RSCacheShared_RSBufferG4(&trailer);
        if( op_count <= 0 || op_count > 65536 )
            return NULL;
    }

    struct RSCacheDat2A_ClientScript* script = calloc(1, sizeof(*script));
    if( !script )
        return NULL;

    script->script_id = script_id;
    script->op_count = op_count;
    script->instructions = calloc((size_t)op_count, sizeof(int));
    script->int_operands = calloc((size_t)op_count, sizeof(int));
    if( !script->instructions || !script->int_operands )
    {
        RSCacheDat2A_ClientScriptFree(script);
        return NULL;
    }

    struct RSCacheShared_RSBuffer body;
    RSCacheShared_RSBufferInit(&body, (uint8_t*)data, (uint32_t)data_size);
    (void)RSCacheShared_RSBufferReadStringNullTerminated(&body);

    int op = 0;
    while( body.position < (uint32_t)trailer_pos && op < op_count )
    {
        int opcode = RSCacheShared_RSBufferG2(&body);
        script->instructions[op] = opcode;

        if( opcode == 3 )
        {
            char* s = RSCacheShared_RSBufferReadStringNullTerminated(&body);
            free(s);
            script->int_operands[op] = 0;
        }
        else if( opcode >= 100 || opcode == 21 || opcode == 38 || opcode == 39 )
            script->int_operands[op] = (int)RSCacheShared_RSBufferG1b(&body);
        else
            script->int_operands[op] = RSCacheShared_RSBufferG4(&body);

        op++;
    }

    script->op_count = op;
    return script;
}

void
RSCacheDat2A_ClientScriptFree(struct RSCacheDat2A_ClientScript* script)
{
    if( !script )
        return;
    free(script->instructions);
    free(script->int_operands);
    free(script);
}
