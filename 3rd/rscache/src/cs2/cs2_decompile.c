#include "cs2_decompile.h"

#include "cs2_cfa.h"
#include "cs2_compile.h"
#include "cs2_dfa.h"
#include "cs2_gen.h"
#include "cs2_gen_json.h"
#include "cs2_interp.h"
#include "cs2_lossless.h"

#include <assert.h>
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

char*
RSCache_CS2_DecompileJson(
    int script_id,
    const struct RSCache_CS2_DecompileOptions* options,
    char** out_name,
    char* error,
    int error_capacity)
{
    assert(options);

    if( out_name )
        *out_name = NULL;
    if( error && error_capacity > 0 )
        error[0] = '\0';

    /* One script at a time, for the same reason the source path does it: a
     * missing callee fails this script and must not take its neighbours with
     * it. */
    struct RSCache_CS2_FunctionSet fs;
    RSCache_CS2_FunctionSetInit(&fs);

    char* document = NULL;
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
                if( RSCache_CS2_GenerateJson(
                        &fs, function, root, name, options->names, &buffer, error,
                        error_capacity) )
                {
                    const char* text = RSCache_CS2_StrBufCStr(&buffer);
                    document = (char*)malloc((size_t)buffer.length + 1);
                    assert(document);
                    memcpy(document, text, (size_t)buffer.length + 1);
                    if( out_name )
                    {
                        *out_name = (char*)malloc(strlen(name) + 1);
                        assert(*out_name);
                        memcpy(*out_name, name, strlen(name) + 1);
                    }
                }
            }
        }
    }

    RSCache_CS2_StrBufFree(&buffer);
    RSCache_CS2_FunctionSetFree(&fs);
    return document;
}

int
RSCache_CS2_ScanCallTriggers(
    const int* ids,
    int count,
    const struct RSCache_CS2_DecompileOptions* options,
    struct RSCache_CS2_Names* out,
    int* out_both)
{
    assert(options);
    assert(out);
    if( out_both )
        *out_both = 0;
    if( count <= 0 )
        return 0;
    assert(ids);

    /* Pinned before the scan: those are the settings file's, and it wins. */
    struct RSCache_CS2_IntMap preset;
    RSCache_CS2_IntMapInit(&preset);
    struct RSCache_CS2_IntMap seen;
    RSCache_CS2_IntMapInit(&seen);
    for( int i = 0; i < out->script_triggers.capacity; i++ )
        if( out->script_triggers.entries[i].occupied )
            RSCache_CS2_IntMapPut(&preset, out->script_triggers.entries[i].key, (void*)(intptr_t)1);

    /* `seen` holds a bit set per callee: 1 = called, 2 = bound as a hook. */
    for( int i = 0; i < count; i++ )
    {
        struct RSCache_CS2_FunctionSet fs;
        RSCache_CS2_FunctionSetInit(&fs);
        char error[256];
        int id = ids[i];
        /* A script that does not interpret contributes nothing; its own
         * decompile reports why. Its callers still pin its callees. */
        RSCache_CS2_Interpret(&fs, &id, 1, options, error, (int)sizeof(error));
        for( int e = 0; e < fs.call_triggers.capacity; e++ )
        {
            if( !fs.call_triggers.entries[e].occupied )
                continue;
            int callee = fs.call_triggers.entries[e].key;
            /* A hook cleared with -1 names no script. */
            if( callee < 0 )
                continue;
            intptr_t how = (intptr_t)fs.call_triggers.entries[e].value;
            intptr_t bits = (intptr_t)RSCache_CS2_IntMapGet(&seen, callee);
            bits |= how == RSCACHE_CS2_TRIGGER_PROC ? 1 : 2;
            RSCache_CS2_IntMapPut(&seen, callee, (void*)bits);
        }
        RSCache_CS2_FunctionSetFree(&fs);
    }

    int pinned = 0;
    for( int e = 0; e < seen.capacity; e++ )
    {
        if( !seen.entries[e].occupied )
            continue;
        int callee = seen.entries[e].key;
        intptr_t bits = (intptr_t)seen.entries[e].value;
        if( bits == 3 && out_both )
            (*out_both)++;
        if( RSCache_CS2_IntMapGet(&preset, callee) )
            continue;
        RSCache_CS2_NamesSetScriptTrigger(
            out, callee,
            (bits & 1) ? RSCACHE_CS2_TRIGGER_PROC : RSCACHE_CS2_TRIGGER_CLIENTSCRIPT);
        pinned++;
    }
    RSCache_CS2_IntMapFree(&seen);
    RSCache_CS2_IntMapFree(&preset);
    return pinned;
}
