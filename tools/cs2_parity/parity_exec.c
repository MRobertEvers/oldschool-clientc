#include "parity_exec.h"

#include "cs2_runner.h"
#include "games/ie_enum_lookup.h"
#include "parity_iface.h"
#include "toriauxlib/c/toriauxlibcache_clientscript_convert.h"
#include "ui/uitree.h"
#include "vm/cs2_host_ui.h"
#include "vm/cs2_opcode.h"
#include "vm/cs2vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARITY_SCRIPT_CACHE_MAX 64

struct ParityScriptEntry
{
    int script_id;
    struct ToriAuxLibCore_ClientScript* loaded;
};

static struct RSCacheDat2Disk* s_cache;
static struct CS2Host s_host;
static struct ParityScriptEntry s_scripts[PARITY_SCRIPT_CACHE_MAX];
static int s_script_count;

static uint16_t k_empty_opcodes[] = { CS2_OP_RETURN };
static int k_empty_operands[] = { 0 };
static struct CS2_Script k_empty_script = {
    .op_count = 1,
    .opcodes = k_empty_opcodes,
    .int_operands = k_empty_operands,
};

static void
parity_iface_root_size(int iface_id, int* out_w, int* out_h)
{
    if( iface_id == 387 )
    {
        if( out_w )
            *out_w = 190;
        if( out_h )
            *out_h = 261;
        return;
    }
    if( out_w )
        *out_w = 765;
    if( out_h )
        *out_h = 503;
}

static void
parity_cache_script(int script_id)
{
    if( !s_cache || script_id < 0 )
        return;
    for( int i = 0; i < s_script_count; i++ )
    {
        if( s_scripts[i].script_id == script_id )
            return;
    }
    if( s_script_count >= PARITY_SCRIPT_CACHE_MAX )
        return;

    struct RSCacheDat2Disk_Archive* archive =
        RSCacheDat2Disk_ArchiveNewLoad(s_cache, RSCacheDat2Disk_Table_Clientscript, script_id);
    struct ToriAuxLibCore_ClientScript* loaded =
        ToriAuxLibCache_ClientScriptNewFromDat2Archive(
            s_cache, archive, script_id, interface161_cs2_clientscript_decode_flags());
    if( !loaded || loaded->script.op_count <= 0 )
        return;
    s_scripts[s_script_count].script_id = script_id;
    s_scripts[s_script_count].loaded = loaded;
    s_script_count++;
}

static struct CS2_Script*
parity_resolve_script(void* ud, int script_id)
{
    (void)ud;
    parity_cache_script(script_id);
    for( int i = 0; i < s_script_count; i++ )
    {
        if( s_scripts[i].script_id == script_id )
            return &s_scripts[i].loaded->script;
    }
    return &k_empty_script;
}

static void
parity_build_synthetic_script(
    char const* case_id,
    struct CS2_Script* script,
    uint16_t* opcodes,
    int* operands,
    int* op_count)
{
    memset(script, 0, sizeof(*script));
    if( strcmp(case_id, "math_add") == 0 )
    {
        opcodes[0] = CS2_OP_PUSH_CONSTANT_INT;
        opcodes[1] = CS2_OP_PUSH_CONSTANT_INT;
        opcodes[2] = CS2_OP_ADD;
        opcodes[3] = CS2_OP_RETURN;
        operands[0] = 40;
        operands[1] = 2;
        operands[2] = 0;
        operands[3] = 0;
        *op_count = 4;
    }
    else if( strcmp(case_id, "enum_lookup") == 0 )
    {
        opcodes[0] = CS2_OP_PUSH_CONSTANT_INT;
        opcodes[1] = CS2_OP_PUSH_CONSTANT_INT;
        opcodes[2] = CS2_OP_PUSH_CONSTANT_INT;
        opcodes[3] = CS2_OP_PUSH_CONSTANT_INT;
        opcodes[4] = CS2_OP_ENUM;
        opcodes[5] = CS2_OP_RETURN;
        operands[0] = 0;
        operands[1] = 0;
        operands[2] = 99;
        operands[3] = 1;
        operands[4] = 0;
        operands[5] = 0;
        *op_count = 6;
    }
    script->op_count = *op_count;
    script->opcodes = opcodes;
    script->int_operands = operands;
}

static void
parity_json_escape(FILE* fp, char const* text)
{
    fputc('"', fp);
    if( !text )
    {
        fputc('"', fp);
        return;
    }
    for( char const* p = text; *p; p++ )
    {
        if( *p == '"' || *p == '\\' )
            fputc('\\', fp);
        fputc(*p, fp);
    }
    fputc('"', fp);
}

static int
parity_write_exec_artifact(
    char const* out_path,
    struct ParityCs2Case const* cs_case,
    struct CS2VM* vm,
    int status)
{
    FILE* fp = fopen(out_path, "w");
    if( !fp )
        return -1;

    int int_sp = cs2vm_get_int_stack_size(vm);
    int str_sp = cs2vm_get_string_stack_size(vm);
    int int_stack[256];
    char const* str_stack[256];
    int int_n = int_sp < 256 ? int_sp : 256;
    int str_n = str_sp < 256 ? str_sp : 256;
    cs2vm_copy_int_stack(vm, int_stack, int_n);
    cs2vm_copy_string_stack(vm, str_stack, str_n);

    fprintf(fp, "{\n");
    fprintf(fp, "  \"caseId\": \"%s\",\n", cs_case->id);
    if( cs_case->script_id >= 0 )
        fprintf(fp, "  \"scriptId\": %d,\n", cs_case->script_id);
    else
        fprintf(fp, "  \"scriptId\": null,\n");
    fprintf(fp, "  \"status\": %d,\n", status);
    fprintf(fp, "  \"opcount\": %d,\n", cs2vm_get_opcount(vm));
    fprintf(fp, "  \"intStack\": [");
    for( int i = 0; i < int_n; i++ )
        fprintf(fp, "%s%d", i ? ", " : "", int_stack[i]);
    fprintf(fp, "],\n");
    fprintf(fp, "  \"stringStack\": [");
    for( int i = 0; i < str_n; i++ )
    {
        if( i )
            fprintf(fp, ", ");
        if( str_stack[i] )
            parity_json_escape(fp, str_stack[i]);
        else
            fprintf(fp, "null");
    }
    fprintf(fp, "],\n");
    fprintf(fp, "  \"trace\": [\n");
    int trace_count = cs2vm_get_trace_count();
    struct CS2TraceEntry trace_buf[4096];
    int copy_n = trace_count < 4096 ? trace_count : 4096;
    cs2vm_copy_trace_buffer(trace_buf, copy_n);
    for( int i = 0; i < copy_n; i++ )
    {
        fprintf(
            fp,
            "    {\"step\":%d,\"pc\":%d,\"opcode\":%d,\"intSp\":%d,\"strSp\":%d,\"topInt\":%d}%s\n",
            trace_buf[i].step,
            trace_buf[i].pc,
            trace_buf[i].opcode,
            trace_buf[i].int_sp,
            trace_buf[i].str_sp,
            trace_buf[i].top_int,
            i + 1 < copy_n ? "," : "");
    }
    fprintf(fp, "  ]\n}\n");
    fclose(fp);
    return 0;
}

int
parity_set_clientscript_decode_flags(int flags)
{
    interface161_cs2_set_clientscript_decode_flags(flags);
    return 0;
}

int
parity_exec_case(
    struct RSCacheDat2Disk* cache,
    struct ParityCs2Case const* cs_case,
    char const* out_path)
{
    s_cache = cache;
    s_script_count = 0;

    static uint16_t opcodes[32];
    static int operands[32];
    struct CS2_Script script;
    int op_count = 0;

    struct CS2_RunArgs args = {
        .int_argc = cs_case->int_argc,
        .int_argv = cs_case->int_argc > 0 ? cs_case->int_argv : NULL,
        .string_argc = cs_case->string_argc,
        .string_argv = cs_case->string_argc > 0 ? (char const* const*)cs_case->string_argv : NULL,
    };

    cs2vm_set_trace_json(true);

    if( cs_case->iface > 0 && !cs_case->synthetic )
    {
        int root_w = 0;
        int root_h = 0;
        parity_iface_root_size(cs_case->iface, &root_w, &root_h);

        struct ParityIfaceLoad iface;
        if( parity_iface_load(cache, cs_case->iface, root_w, root_h, &iface) != 0 )
            return -1;

        struct Interface161Cs2Context ctx;
        interface161_cs2_context_init(&ctx);
        ctx.cache = cache;
        if( interface161_cs2_prepare_exec_shell(
                &ctx, iface.comps, iface.comp_count, cs_case->iface, root_w, root_h) != 0 )
        {
            interface161_cs2_context_free(&ctx);
            parity_iface_free(&iface);
            return -1;
        }

        if( cs_case->synthetic )
        {
            parity_build_synthetic_script(cs_case->id, &script, opcodes, operands, &op_count);
        }
        else
        {
            parity_cache_script(cs_case->script_id);
            struct CS2_Script* loaded = parity_resolve_script(cache, cs_case->script_id);
            if( !loaded || loaded->op_count <= 0 )
            {
                interface161_cs2_context_free(&ctx);
                parity_iface_free(&iface);
                return -1;
            }
            script = *loaded;
        }

        if( cs_case->active_component > 0 )
            cs2vm_set_active_component(ctx.cs2vm, cs_case->active_component);

        int status = cs2vm_run(ctx.cs2vm, &script, ctx.cs2host, &args);
        int rc = parity_write_exec_artifact(out_path, cs_case, ctx.cs2vm, status);

        interface161_cs2_context_free(&ctx);
        parity_iface_free(&iface);
        return rc != 0 ? -1 : 0;
    }

    struct CS2VM* vm = cs2vm_new();
    struct UITree* tree = uitree_new(64);
    if( !vm || !tree )
    {
        uitree_free(tree);
        cs2vm_free(vm);
        return -1;
    }

    struct CS2HostUIInitArgs host_args = {
        .dat2_disk = cache,
        .tree = tree,
    };
    cs2_host_ui_init(&s_host, &host_args);
    s_host.resolve_script = parity_resolve_script;

    if( cs_case->synthetic )
    {
        parity_build_synthetic_script(cs_case->id, &script, opcodes, operands, &op_count);
    }
    else
    {
        parity_cache_script(cs_case->script_id);
        struct CS2_Script* loaded = parity_resolve_script(cache, cs_case->script_id);
        if( !loaded || loaded->op_count <= 0 )
        {
            uitree_free(tree);
            cs2vm_free(vm);
            return -1;
        }
        script = *loaded;
    }

    if( cs_case->active_component > 0 )
        cs2vm_set_active_component(vm, cs_case->active_component);

    int status = cs2vm_run(vm, &script, &s_host, &args);
    int rc = parity_write_exec_artifact(out_path, cs_case, vm, status);

    uitree_free(tree);
    cs2vm_free(vm);
    return rc != 0 ? -1 : 0;
}
