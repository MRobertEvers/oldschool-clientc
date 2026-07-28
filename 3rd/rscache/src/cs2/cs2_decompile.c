#include "cs2_decompile.h"

#include "cs2_cfa.h"
#include "cs2_dfa.h"
#include "cs2_gen.h"
#include "cs2_interp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char*
RSCache_CS2_Decompile(
    int script_id,
    const struct RSCache_CS2_DecompileOptions* options,
    char** out_name,
    char* error,
    int error_capacity)
{
    if( out_name )
        *out_name = NULL;
    if( error && error_capacity > 0 )
        error[0] = '\0';

    /* One script at a time, as upstream's own driver does: a script whose
     * callee is missing fails, and batching would take its neighbours down
     * with it. Callees are still loaded — for their signatures — through
     * options->scripts. */
    struct RSCache_CS2_FunctionSet fs;
    RSCache_CS2_FunctionSetInit(&fs);

    char* source = NULL;
    struct RSCache_CS2_StrBuf buffer;
    RSCache_CS2_StrBufInit(&buffer);

    if( RSCache_CS2_Interpret(&fs, &script_id, 1, options, error, error_capacity) &&
        RSCache_CS2_Transform(&fs, error, error_capacity) )
    {
        struct RSCache_CS2_Function* function = RSCache_CS2_FunctionSetGet(&fs, script_id);
        if( !function )
        {
            snprintf(error, (size_t)error_capacity, "script %d produced no function", script_id);
        }
        else
        {
            struct RSCache_CS2_Construct* root =
                RSCache_CS2_Reconstruct(&fs.arena, function, error, error_capacity);
            if( root )
            {
                char name[512];
                RSCache_CS2_FunctionName(&fs, function, options->names, name, (int)sizeof(name));
                if( RSCache_CS2_Generate(
                        &fs, function, root, name, options->names, &buffer, error,
                        error_capacity) )
                {
                    const char* text = RSCache_CS2_StrBufCStr(&buffer);
                    source = (char*)malloc((size_t)buffer.length + 1);
                    if( source )
                        memcpy(source, text, (size_t)buffer.length + 1);
                    if( out_name && source )
                    {
                        *out_name = (char*)malloc(strlen(name) + 1);
                        if( *out_name )
                            memcpy(*out_name, name, strlen(name) + 1);
                    }
                }
            }
        }
    }

    RSCache_CS2_StrBufFree(&buffer);
    RSCache_CS2_FunctionSetFree(&fs);
    return source;
}
