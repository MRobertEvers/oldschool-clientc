#include "cs2_decompile.h"

#include "cs2_cfa.h"
#include "cs2_compile.h"
#include "cs2_dfa.h"
#include "cs2_gen.h"
#include "cs2_interp.h"
#include "cs2_lossless.h"

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
            function->generate_lossless_metadata = options->lossless;
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
                    /* Structured source intentionally normalizes bytecode shapes such as
                     * redundant returns and equivalent branch layouts. Preserve the exact
                     * decoded script only when compiling this generated source proves that
                     * normalization changed a serialized field. The fingerprint makes the
                     * snapshot edit-safe: any change to the source disables it. */
                    if( options->lossless )
                    {
                        const struct RSCache_CS2_Script* original =
                            options->scripts.load(options->scripts.user, script_id);
                        struct RSCache_CS2_CompileOptions compile_options;
                        memset(&compile_options, 0, sizeof(compile_options));
                        compile_options.scripts = options->scripts;
                        compile_options.param_types = options->param_types;
                        compile_options.db_columns = options->db_columns;
                        compile_options.names = options->names;
                        struct RSCache_ClientScript rebuilt;
                        char compile_error[512] = { 0 };
                        const char* generated = RSCache_CS2_StrBufCStr(&buffer);
                        bool compiled = RSCache_CS2_Compile(
                            generated,
                            &compile_options,
                            &rebuilt,
                            compile_error,
                            (int)sizeof(compile_error));
                        if( compiled && original &&
                            !RSCache_CS2_LosslessEqual(original, &rebuilt.script) )
                        {
                            uint64_t hash =
                                RSCache_CS2_LosslessHash(generated, (size_t)buffer.length);
                            int metadata_start = buffer.length;
                            RSCache_CS2_StrBufAppendFormat(
                                &buffer, "// @rscache-lossless-v1 %016llx ",
                                (unsigned long long)hash);
                            if( !RSCache_CS2_LosslessEncode(original, &buffer) )
                            {
                                /* Keep the readable source if an exotic script cannot be
                                 * represented by this metadata version. */
                                buffer.length = metadata_start;
                            }
                            else
                            {
                                RSCache_CS2_StrBufAppendChar(&buffer, '\n');
                            }
                        }
                        if( compiled )
                            RSCache_ClientScriptFreeInplace(&rebuilt);
                    }
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
