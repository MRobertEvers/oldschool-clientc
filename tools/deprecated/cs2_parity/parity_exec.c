#include "parity_exec.h"

#include "cs2_runner.h"
#include "games/ie_enum_lookup.h"
#include "parity_iface.h"
#include "toriauxlib/cache/toriauxlibcache_clientscript_convert.h"
#include "ui/uitree.h"
#include "vm/cs2_opcode.h"
#include "vm/cs2vmx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARITY_SCRIPT_CACHE_MAX 64

struct ParityScriptEntry
{
    int script_id;
    struct ToriAuxLibCore_ClientScript* loaded;
};

/* Captured instruction trace for the artifact's "trace" field. 200k is well
 * past any script in the cache; the longest in osrs239 is under 2000 ops. */
#define PARITY_TRACE_MAX 200000
static struct CS2VM2_TraceRecord s_trace[PARITY_TRACE_MAX];
static int s_trace_count;

static struct RSCacheDat2Disk* s_cache;
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
parity_iface_root_size(
    int iface_id,
    int* out_w,
    int* out_h)
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
    struct ToriAuxLibCore_ClientScript* loaded = ToriAuxLibCache_ClientScriptNewFromDat2Archive2(
        archive, script_id, interface161_cs2_clientscript_decode_flags());
    if( !loaded || loaded->script.op_count <= 0 )
        return;
    s_scripts[s_script_count].script_id = script_id;
    s_scripts[s_script_count].loaded = loaded;
    s_script_count++;
}

static struct CS2_Script*
parity_resolve_script(int script_id)
{
    parity_cache_script(script_id);
    for( int i = 0; i < s_script_count; i++ )
    {
        if( s_scripts[i].script_id == script_id )
            return &s_scripts[i].loaded->script;
    }
    return &k_empty_script;
}

static int
parity_resolve_active_component(struct ParityCs2Case const* cs_case)
{
    if( cs_case->iface > 0 && cs_case->component_file >= 0 )
        return (cs_case->iface << 16) | (cs_case->component_file & 0xffff);
    return cs_case->active_component;
}

static void
parity_build_synthetic_script(
    char const* case_id,
    int parent_component,
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
    else if( strcmp(case_id, "cc_dot_sethide") == 0 )
    {
        /* .cc_create (operand 1) graphic child, then .cc_sethide (operand 1) on dot target. */
        opcodes[0] = CS2_OP_PUSH_CONSTANT_INT;
        opcodes[1] = CS2_OP_PUSH_CONSTANT_INT;
        opcodes[2] = CS2_OP_PUSH_CONSTANT_INT;
        opcodes[3] = CS2_OP_PUSH_CONSTANT_INT;
        opcodes[4] = CS2_OP_CC_CREATE;
        opcodes[5] = CS2_OP_PUSH_CONSTANT_INT;
        opcodes[6] = CS2_OP_CC_SETHIDE;
        opcodes[7] = CS2_OP_RETURN;
        operands[0] = parent_component;
        operands[1] = 5; /* TYPE_GRAPHIC */
        operands[2] = 0; /* child index */
        operands[3] = 0; /* is_nested */
        operands[4] = 1; /* dot cc_create */
        operands[5] = 1; /* hide */
        operands[6] = 1; /* dot cc_sethide */
        operands[7] = 0;
        *op_count = 8;
    }
    script->op_count = *op_count;
    script->opcodes = opcodes;
    script->int_operands = operands;
}

static void
parity_json_escape(
    FILE* fp,
    char const* text)
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
    struct CS2VMX* vm,
    int status)
{
    FILE* fp = fopen(out_path, "w");
    if( !fp )
        return -1;

    int int_n = vm->ints_stack_top < 256 ? vm->ints_stack_top : 256;
    int str_n = vm->strs_stack_top < 256 ? vm->strs_stack_top : 256;

    fprintf(fp, "{\n");
    fprintf(fp, "  \"caseId\": \"%s\",\n", cs_case->id);
    if( cs_case->script_id >= 0 )
        fprintf(fp, "  \"scriptId\": %d,\n", cs_case->script_id);
    else
        fprintf(fp, "  \"scriptId\": null,\n");
    fprintf(fp, "  \"status\": %d,\n", status);
    fprintf(fp, "  \"opcount\": 0,\n");
    fprintf(fp, "  \"intStack\": [");
    for( int i = 0; i < int_n; i++ )
        fprintf(fp, "%s%d", i ? ", " : "", vm->ints_stack[i]);
    fprintf(fp, "],\n");
    fprintf(fp, "  \"stringStack\": [");
    for( int i = 0; i < str_n; i++ )
    {
        if( i )
            fprintf(fp, ", ");
        if( vm->strs_stack[i] )
            parity_json_escape(fp, vm->strs_stack[i]);
        else
            fprintf(fp, "null");
    }
    fprintf(fp, "],\n");
    /* The trace was captured during the run; see parity_exec_case. Emitting it
     * is what lets a divergence be located at an instruction instead of
     * inferred from a final stack that is already several ops downstream. */
    fprintf(fp, "  \"trace\": [\n");
    for( int i = 0; i < s_trace_count; i++ )
    {
        fprintf(
            fp,
            "    {\"step\": %d, \"pc\": %d, \"opcode\": %d, \"intSp\": %d, "
            "\"strSp\": %d, \"topInt\": %d}%s\n",
            i,
            s_trace[i].pc,
            s_trace[i].opcode,
            s_trace[i].ints_top,
            s_trace[i].strs_top,
            s_trace[i].top_int,
            i + 1 < s_trace_count ? "," : "");
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

static int
parity_bare_host_exec(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request)
{
    if( !request )
        return CS2VM_EXECNO_ERROR;
    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_ENUM_STRING:
    case CS2VM_HOST_REQUEST_ENUM:
        return CS2VMX_PushInt(vm, request->u.enum_lookup.key);
    case CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT:
        return CS2VMX_PushInt(vm, 0);
    case CS2VM_HOST_REQUEST_GOSUB_WITH_PARAMS:
    {
        struct CS2_Script* script = parity_resolve_script(request->u.push_script.script_id);
        if( !script )
            return CS2VM_EXECNO_ERROR;
        return CS2VMX_PushCallScript(vm, script);
    }
    default:
        return CS2VM_EXECNO_OK;
    }
}

int
parity_exec_case(
    struct RSCacheDat2Disk* cache,
    struct ParityCs2Case const* cs_case,
    char const* out_path)
{
    s_cache = cache;
    s_script_count = 0;
    CS2VM2_TraceCaptureBegin(s_trace, PARITY_TRACE_MAX);
    s_trace_count = 0;

    static uint16_t opcodes[32];
    static int operands[32];
    struct CS2_Script script;
    int op_count = 0;

    int const active_component = parity_resolve_active_component(cs_case);

    if( cs_case->iface > 0 )
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
            parity_build_synthetic_script(
                cs_case->id, active_component, &script, opcodes, operands, &op_count);
        }
        else
        {
            parity_cache_script(cs_case->script_id);
            struct CS2_Script* loaded = parity_resolve_script(cs_case->script_id);
            if( !loaded || loaded->op_count <= 0 )
            {
                interface161_cs2_context_free(&ctx);
                parity_iface_free(&iface);
                return -1;
            }
            script = *loaded;
        }

        int status = interface161_cs2_run_script(
            &ctx,
            &script,
            active_component > 0 ? active_component : 0,
            cs_case->int_argc > 0 ? cs_case->int_argv : NULL,
            cs_case->int_argc);

        if( strcmp(cs_case->id, "cc_dot_sethide") == 0 && ctx.tree )
        {
            int32_t const parent_idx = uitree_find_by_component_id(ctx.tree, active_component);
            int const parent_hide =
                parent_idx >= 0 ? ctx.tree->components[parent_idx].behavior.hide : -1;
            int32_t const child_idx =
                parent_idx >= 0
                    ? uitree_find_child_by_subid(ctx.tree, parent_idx, active_component, 0)
                    : -1;
            int const child_hide =
                child_idx >= 0 ? ctx.tree->components[child_idx].behavior.hide : -1;
            if( parent_idx < 0 || parent_hide )
                status = CS2VM_EXECNO_ERROR;
            else if( child_idx < 0 || !child_hide )
                status = CS2VM_EXECNO_ERROR;
        }

        s_trace_count = CS2VM2_TraceCaptureEnd();
        int rc = parity_write_exec_artifact(out_path, cs_case, &ctx.cs2vm, status);

        interface161_cs2_context_free(&ctx);
        parity_iface_free(&iface);
        return rc != 0 ? -1 : 0;
    }

    /* Non-iface synthetic cases (math_add / enum_lookup). */
    struct CS2VMX vm;
    memset(&vm, 0, sizeof(vm));
    CS2VMX_BindHost(&vm, NULL, parity_bare_host_exec);

    if( cs_case->synthetic )
    {
        parity_build_synthetic_script(
            cs_case->id, active_component, &script, opcodes, operands, &op_count);
    }
    else
    {
        parity_cache_script(cs_case->script_id);
        struct CS2_Script* loaded = parity_resolve_script(cs_case->script_id);
        if( !loaded || loaded->op_count <= 0 )
            return -1;
        script = *loaded;
    }

    CS2VMX_ResetRuntime(&vm);
    if( CS2VMX_PushCallScript(&vm, &script) != CS2VM_EXECNO_OK )
        return -1;
    int status = CS2VMX_RunScript(&vm);
    s_trace_count = CS2VM2_TraceCaptureEnd();
    return parity_write_exec_artifact(out_path, cs_case, &vm, status) != 0 ? -1 : 0;
}
