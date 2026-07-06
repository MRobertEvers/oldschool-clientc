#include "cs2vm.h"

#include "cs2_opcode.h"
#include "cs2_opcode_meta.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool s_cs2_trace = false;
static bool s_cs2_trace_json = false;
static bool s_cs2_trace_env_checked = false;

#define CS2_TRACE_BUF_MAX 65536
static struct CS2TraceEntry s_trace_buf[CS2_TRACE_BUF_MAX];
static int s_trace_count = 0;

#define CS2_RT_MAX_FRAMES 32
#define CS2_RT_MAX_LOCALS 128
#define CS2_RT_MAX_ARRAYS 128
#define CS2_RT_ARRAY_CAPACITY 256
#define CS2_RT_MAX_STEPS 1000000

struct CS2VMArray
{
    int values[CS2_RT_ARRAY_CAPACITY];
    int size;
    bool defined;
};

struct CS2VMFrame
{
    struct CS2_Script const* script;
    int pc;
    int int_locals[CS2_RT_MAX_LOCALS];
    char* str_locals[CS2_RT_MAX_LOCALS];
    int return_pc;
    int return_frame;
    int has_return;
    int return_int_count;
    int return_ints[8];
};

struct CS2VM
{
    int int_stack[CS2_SCRIPT_STACK_MAX];
    int int_sp;
    char* str_stack[CS2_SCRIPT_STACK_MAX];
    int str_sp;
    char string_pool[CS2_SCRIPT_STRING_POOL];
    int string_pool_used;
    struct CS2VMFrame frames[CS2_RT_MAX_FRAMES];
    int frame_sp;
    int active_component;
    int dot_component;
    struct CS2VMArray arrays[CS2_RT_MAX_ARRAYS];
    int run_error;
    bool halted;
    bool last_error_valid;
    int last_error_opcode;
    int last_error_pc;
    int last_error_script_id;
    int opcount;
};

void
cs2vm_set_trace(bool enabled)
{
    s_cs2_trace = enabled;
    s_cs2_trace_env_checked = true;
}

bool
cs2vm_get_trace(void)
{
    return s_cs2_trace;
}

void
cs2vm_set_trace_json(bool enabled)
{
    s_cs2_trace_json = enabled;
    s_cs2_trace_env_checked = true;
}

bool
cs2vm_get_trace_json(void)
{
    return s_cs2_trace_json;
}

int
cs2vm_get_opcount(struct CS2VM const* vm)
{
    return vm ? vm->opcount : 0;
}

int
cs2vm_get_int_stack_size(struct CS2VM const* vm)
{
    return vm ? vm->int_sp : 0;
}

int
cs2vm_get_string_stack_size(struct CS2VM const* vm)
{
    return vm ? vm->str_sp : 0;
}

void
cs2vm_copy_int_stack(
    struct CS2VM const* vm,
    int* out,
    int max_count)
{
    if( !vm || !out || max_count <= 0 )
        return;
    int n = vm->int_sp < max_count ? vm->int_sp : max_count;
    for( int i = 0; i < n; i++ )
        out[i] = vm->int_stack[i];
}

void
cs2vm_copy_string_stack(
    struct CS2VM const* vm,
    char const** out,
    int max_count)
{
    if( !vm || !out || max_count <= 0 )
        return;
    int n = vm->str_sp < max_count ? vm->str_sp : max_count;
    for( int i = 0; i < n; i++ )
        out[i] = vm->str_stack[i];
}

void
cs2vm_clear_trace_buffer(void)
{
    s_trace_count = 0;
}

int
cs2vm_get_trace_count(void)
{
    return s_trace_count;
}

void
cs2vm_copy_trace_buffer(
    struct CS2TraceEntry* out,
    int max_count)
{
    if( !out || max_count <= 0 )
        return;
    int n = s_trace_count < max_count ? s_trace_count : max_count;
    memcpy(out, s_trace_buf, (size_t)n * sizeof(struct CS2TraceEntry));
}

static void
cs2_rt_ensure_trace_env(void)
{
    if( s_cs2_trace_env_checked )
        return;
    s_cs2_trace_env_checked = true;
    char const* env = getenv("IE_CS2_TRACE");
    if( env && env[0] && env[0] != '0' )
        s_cs2_trace = true;
    char const* parity_env = getenv("CS2_PARITY_TRACE");
    if( parity_env && parity_env[0] && parity_env[0] != '0' )
        s_cs2_trace_json = true;
}

static void
cs2_rt_record_error(
    struct CS2VM* rt,
    int opcode,
    int pc,
    int script_id)
{
    if( !rt || rt->run_error == 0 || rt->last_error_valid )
        return;
    rt->last_error_valid = true;
    rt->last_error_opcode = opcode;
    rt->last_error_pc = pc;
    rt->last_error_script_id = script_id;
}

void
cs2vm_host_fail(
    struct CS2_InvokeCtx* ctx,
    int err_code)
{
    if( !ctx || !ctx->vm )
        return;
    if( ctx->vm->run_error == 0 )
        ctx->vm->run_error = err_code;
    cs2_rt_record_error(
        ctx->vm,
        ctx->opcode,
        ctx->pc,
        ctx->script && ctx->script->script_id >= 0 ? ctx->script->script_id : -1);
}

void
cs2vm_host_halt(struct CS2_InvokeCtx* ctx)
{
    if( !ctx || !ctx->vm )
        return;
    ctx->vm->halted = true;
}

void
cs2vm_get_error_info(
    struct CS2VM const* vm,
    int* out_opcode,
    int* out_pc,
    int* out_script_id)
{
    if( out_opcode )
        *out_opcode = vm && vm->last_error_valid ? vm->last_error_opcode : -1;
    if( out_pc )
        *out_pc = vm && vm->last_error_valid ? vm->last_error_pc : -1;
    if( out_script_id )
        *out_script_id = vm && vm->last_error_valid ? vm->last_error_script_id : -1;
}

char const*
cs2vm_err_name(int err_code)
{
    switch( err_code )
    {
    case CS2VM_OK:
        return "OK";
    case CS2VM_ERR_INVALID:
        return "INVALID";
    case CS2VM_ERR_PC_OOB:
        return "PC_OOB";
    case CS2VM_ERR_STEP_LIMIT:
        return "STEP_LIMIT";
    case CS2VM_ERR_STACK:
        return "STACK";
    case CS2VM_ERR_UNIMPL:
        return "UNIMPL";
    default:
        return "?";
    }
}

struct CS2VM*
cs2vm_new(void)
{
    return calloc(1, sizeof(struct CS2VM));
}

void
cs2vm_free(struct CS2VM* rt)
{
    free(rt);
}

static char*
cs2_rt_alloc_string(
    struct CS2VM* rt,
    char const* src)
{
    assert(rt);
    assert(src);
    int len = (int)strlen(src);
    assert(rt->string_pool_used + len + 1 <= CS2_SCRIPT_STRING_POOL);
    char* out = &rt->string_pool[rt->string_pool_used];
    memcpy(out, src, (size_t)len + 1);
    rt->string_pool_used += len + 1;
    return out;
}

static bool
cs2_rt_try_pop_int(
    struct CS2VM* rt,
    int* out)
{
    if( !rt || rt->int_sp <= 0 )
        return false;
    if( out )
        *out = rt->int_stack[--rt->int_sp];
    return true;
}

static int
cs2_rt_pop_int(struct CS2VM* rt)
{
    int value = 0;
    if( !cs2_rt_try_pop_int(rt, &value) && rt )
        rt->run_error = CS2VM_ERR_STACK;
    return value;
}

static bool
cs2_rt_push_int(
    struct CS2VM* rt,
    int value)
{
    assert(rt);
    if( rt->int_sp >= CS2_SCRIPT_STACK_MAX )
    {
        rt->run_error = CS2VM_ERR_STACK;
        return false;
    }
    rt->int_stack[rt->int_sp++] = value;
    return true;
}

#define CS2_RT_PUSH_INT(rt, value)                                                                 \
    do                                                                                             \
    {                                                                                              \
        if( !cs2_rt_push_int((rt), (value)) )                                                      \
            return false;                                                                          \
    } while( 0 )

#define CS2_RT_PUSH_STR(rt, value)                                                                 \
    do                                                                                             \
    {                                                                                              \
        if( !cs2_rt_push_str((rt), (value)) )                                                      \
            return false;                                                                          \
    } while( 0 )

static bool
cs2_rt_try_pop_str(
    struct CS2VM* rt,
    char** out)
{
    if( !rt || rt->str_sp <= 0 )
        return false;
    if( out )
        *out = rt->str_stack[--rt->str_sp];
    return true;
}

static char*
cs2_rt_pop_str(struct CS2VM* rt)
{
    char* value = NULL;
    if( !cs2_rt_try_pop_str(rt, &value) && rt )
        rt->run_error = CS2VM_ERR_STACK;
    return value ? value : "";
}

static bool
cs2_rt_push_str(
    struct CS2VM* rt,
    char* value)
{
    assert(rt);
    if( rt->str_sp >= CS2_SCRIPT_STACK_MAX )
    {
        rt->run_error = CS2VM_ERR_STACK;
        return false;
    }
    rt->str_stack[rt->str_sp++] = value;
    return true;
}

static struct CS2VMFrame*
cs2_rt_current_frame(struct CS2VM* rt)
{
    assert(rt);
    assert(rt->frame_sp > 0);
    return &rt->frames[rt->frame_sp - 1];
}

static void
cs2_rt_assert_pc_in_bounds(struct CS2VMFrame const* frame)
{
    assert(frame);
    assert(frame->script);
    assert(frame->pc >= 0);
    assert(frame->pc <= frame->script->op_count);
}

static void
cs2_rt_host_invoke(
    struct CS2VM* rt,
    struct CS2Host const* host,
    int opcode,
    int operand)
{
    const struct CS2_OpcodeMeta* meta = cs2_opcode_meta_lookup(opcode);
    assert(host);
    assert(host->invoke);
    if( !host->invoke )
    {
        fprintf(
            stderr,
            "cs2vm: missing host invoke for opcode %d (%s)\n",
            opcode,
            meta->name ? meta->name : "?");
        assert(host->invoke);
        return;
    }
    struct CS2VMFrame* frame = cs2_rt_current_frame(rt);
    struct CS2_InvokeCtx ctx = {
        .host_ud = host->ud,
        .vm = rt,
        .frame = frame,
        .script = frame->script,
        .pc = frame->pc,
        .opcode = opcode,
        .operand = operand,
        .active_component = rt->active_component,
        .dot_component = rt->dot_component,
    };
    host->invoke(host->ud, &ctx);
}

static int
cs2_array_define_slot(
    int operand)
{
    return operand >> 16;
}

static bool
cs2_rt_exec_opcode(
    struct CS2VM* rt,
    struct CS2Host const* host,
    int opcode,
    int operand,
    char const* str_operand)
{
    struct CS2VMFrame* frame = cs2_rt_current_frame(rt);
    assert(frame);
    assert(frame->script);
    const struct CS2_OpcodeMeta* meta = cs2_opcode_meta_lookup(opcode);
    if( meta->handler == CS2_HANDLER_HOST )
    {
        cs2_rt_host_invoke(rt, host, opcode, operand);
        return rt->run_error == 0;
    }

    switch( opcode )
    {
    case CS2_OP_PUSH_CONSTANT_INT:
        CS2_RT_PUSH_INT(rt, operand);
        break;
    case CS2_OP_PUSH_CONSTANT_STRING:
        CS2_RT_PUSH_STR(rt, cs2_rt_alloc_string(rt, str_operand ? str_operand : ""));
        break;
    case CS2_OP_PUSH_VAR:
        CS2_RT_PUSH_INT(rt, host && host->get_varp ? host->get_varp(host->ud, operand) : 0);
        break;
    case CS2_OP_POP_VAR:
        if( host && host->set_varp )
            host->set_varp(host->ud, operand, cs2_rt_pop_int(rt));
        else
            (void)cs2_rt_pop_int(rt);
        break;
    case CS2_OP_PUSH_VARBIT:
        CS2_RT_PUSH_INT(rt, host && host->get_varbit ? host->get_varbit(host->ud, operand) : 0);
        break;
    case CS2_OP_POP_VARBIT:
        if( host && host->set_varbit )
            host->set_varbit(host->ud, operand, cs2_rt_pop_int(rt));
        else
            (void)cs2_rt_pop_int(rt);
        break;
    case CS2_OP_PUSH_VARC_INT:
        CS2_RT_PUSH_INT(rt, host && host->get_varc_int ? host->get_varc_int(host->ud, operand) : 0);
        break;
    case CS2_OP_POP_VARC_INT:
        if( host && host->set_varc_int )
            host->set_varc_int(host->ud, operand, cs2_rt_pop_int(rt));
        else
            (void)cs2_rt_pop_int(rt);
        break;
    case CS2_OP_PUSH_VARC_STRING:
        CS2_RT_PUSH_STR(
            rt,
            cs2_rt_alloc_string(
                rt, host && host->get_varc_string ? host->get_varc_string(host->ud, operand) : ""));
        break;
    case CS2_OP_POP_VARC_STRING:
    {
        char* value = cs2_rt_pop_str(rt);
        if( host && host->set_varc_string )
            host->set_varc_string(host->ud, operand, value ? value : "");
        break;
    }
    case CS2_OP_PUSH_INT_LOCAL:
        assert(operand >= 0 && operand < CS2_RT_MAX_LOCALS);
        CS2_RT_PUSH_INT(rt, frame->int_locals[operand]);
        break;
    case CS2_OP_POP_INT_LOCAL:
        assert(operand >= 0 && operand < CS2_RT_MAX_LOCALS);
        frame->int_locals[operand] = cs2_rt_pop_int(rt);
        break;
    case CS2_OP_PUSH_STRING_LOCAL:
        assert(operand >= 0 && operand < CS2_RT_MAX_LOCALS);
        CS2_RT_PUSH_STR(rt, frame->str_locals[operand]);
        break;
    case CS2_OP_POP_STRING_LOCAL:
        assert(operand >= 0 && operand < CS2_RT_MAX_LOCALS);
        frame->str_locals[operand] = cs2_rt_pop_str(rt);
        break;
    case CS2_OP_JOIN_STRING:
    {
        char* b = cs2_rt_pop_str(rt);
        char* a = cs2_rt_pop_str(rt);
        char buf[512];
        snprintf(buf, sizeof(buf), "%s%s", a ? a : "", b ? b : "");
        CS2_RT_PUSH_STR(rt, cs2_rt_alloc_string(rt, buf));
        break;
    }
    case CS2_OP_POP_INT_DISCARD:
        (void)cs2_rt_pop_int(rt);
        break;
    case CS2_OP_POP_STRING_DISCARD:
        (void)cs2_rt_pop_str(rt);
        break;
    case CS2_OP_BRANCH:
        frame->pc += operand;
        cs2_rt_assert_pc_in_bounds(frame);
        return true;
    case CS2_OP_BRANCH_NOT:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        if( a != b )
        {
            frame->pc += operand;
            cs2_rt_assert_pc_in_bounds(frame);
        }
        break;
    }
    case CS2_OP_BRANCH_EQUALS:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        if( a == b )
        {
            frame->pc += operand;
            cs2_rt_assert_pc_in_bounds(frame);
        }
        break;
    }
    case CS2_OP_BRANCH_LESS_THAN:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        if( a < b )
        {
            frame->pc += operand;
            cs2_rt_assert_pc_in_bounds(frame);
        }
        break;
    }
    case CS2_OP_BRANCH_GREATER_THAN:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        if( a > b )
        {
            frame->pc += operand;
            cs2_rt_assert_pc_in_bounds(frame);
        }
        break;
    }
    case CS2_OP_BRANCH_LESS_THAN_OR_EQUALS:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        if( a <= b )
        {
            frame->pc += operand;
            cs2_rt_assert_pc_in_bounds(frame);
        }
        break;
    }
    case CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        if( a >= b )
        {
            frame->pc += operand;
            cs2_rt_assert_pc_in_bounds(frame);
        }
        break;
    }
    case CS2_OP_SWITCH:
    {
        int key = cs2_rt_pop_int(rt);
        if( operand >= 0 && operand < frame->script->switch_table_count )
        {
            struct CS2_ScriptSwitch const* sw = &frame->script->switch_tables[operand];
            for( int i = 0; i < sw->case_count; i++ )
            {
                if( sw->cases[i].key == key )
                {
                    /* target_pc is relative to this SWITCH (matches TS pc += offset). */
                    int switch_pc = frame->pc - 1;
                    frame->pc = switch_pc + sw->cases[i].target_pc + 1;
                    cs2_rt_assert_pc_in_bounds(frame);
                    return true;
                }
            }
        }
        break;
    }
    case CS2_OP_ADD:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        CS2_RT_PUSH_INT(rt, a + b);
        break;
    }
    case CS2_OP_SUB:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        CS2_RT_PUSH_INT(rt, a - b);
        break;
    }
    case CS2_OP_MULTIPLY:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        CS2_RT_PUSH_INT(rt, a * b);
        break;
    }
    case CS2_OP_DIV:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        CS2_RT_PUSH_INT(rt, b != 0 ? a / b : 0);
        break;
    }
    case CS2_OP_MOD:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        CS2_RT_PUSH_INT(rt, b != 0 ? a % b : 0);
        break;
    }
    case CS2_OP_AND:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        CS2_RT_PUSH_INT(rt, a & b);
        break;
    }
    case CS2_OP_OR:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        CS2_RT_PUSH_INT(rt, a | b);
        break;
    }
    case CS2_OP_MIN:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        CS2_RT_PUSH_INT(rt, a < b ? a : b);
        break;
    }
    case CS2_OP_MAX:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        CS2_RT_PUSH_INT(rt, a > b ? a : b);
        break;
    }
    case CS2_OP_TESTBIT:
    {
        int bit = cs2_rt_pop_int(rt);
        int value = cs2_rt_pop_int(rt);
        CS2_RT_PUSH_INT(rt, (value & (1 << bit)) != 0);
        break;
    }
    case CS2_OP_GOSUB_WITH_PARAMS:
    {
        assert(host);
        assert(host->resolve_script);
        assert(rt->frame_sp < CS2_RT_MAX_FRAMES);
        struct CS2_Script* callee =
            host->resolve_script ? host->resolve_script(host->ud, operand) : NULL;
        if( !callee )
        {
            int int_args = 0;
            int str_args = 0;
            if( host->script_arg_counts &&
                host->script_arg_counts(host->ud, operand, &int_args, &str_args) )
            {
                int const argc = int_args + str_args;
                for( int i = argc - 1; i >= 0; i-- )
                {
                    if( i < str_args )
                        (void)cs2_rt_pop_str(rt);
                    else
                        (void)cs2_rt_pop_int(rt);
                }
            }
            fprintf(
                stderr,
                "cs2vm: gosub script %d not resolved (script_id=%d pc=%d)\n",
                operand,
                frame->script ? frame->script->script_id : -1,
                frame->pc);
            rt->run_error = CS2VM_ERR_INVALID;
            return false;
        }
        struct CS2VMFrame* caller = frame;
        caller->return_pc = caller->pc;
        caller->return_frame = rt->frame_sp - 1;
        struct CS2VMFrame* next = &rt->frames[rt->frame_sp++];
        memset(next, 0, sizeof(*next));
        next->script = callee;
        next->pc = 0;
        if( s_cs2_trace )
        {
            fprintf(
                stderr,
                "cs2vm trace: GOSUB script=%d argc=%d frame_sp=%d caller_pc=%d\n",
                operand,
                callee->int_argument_count + callee->string_argument_count,
                rt->frame_sp,
                caller->return_pc);
        }
        int argc = callee->int_argument_count + callee->string_argument_count;
        for( int i = argc - 1; i >= 0; i-- )
        {
            if( i < callee->string_argument_count )
                next->str_locals[i] = cs2_rt_pop_str(rt);
            else
                next->int_locals[i - callee->string_argument_count] = cs2_rt_pop_int(rt);
        }
        return true;
    }
    case CS2_OP_DEFINE_ARRAY:
    {
        int size = cs2_rt_pop_int(rt);
        int const slot = cs2_array_define_slot(operand);
        if( slot < 0 || slot >= CS2_RT_MAX_ARRAYS )
        {
            fprintf(
                stderr,
                "cs2vm: DEFINE_ARRAY slot out of range script_id=%d slot=%d operand=%d\n",
                frame->script ? frame->script->script_id : -1,
                slot,
                operand);
            rt->run_error = CS2VM_ERR_INVALID;
            return false;
        }
        if( size < 0 )
            size = 0;
        if( size > CS2_RT_ARRAY_CAPACITY )
            size = CS2_RT_ARRAY_CAPACITY;
        rt->arrays[slot].defined = true;
        rt->arrays[slot].size = size;
        memset(rt->arrays[slot].values, 0, sizeof(rt->arrays[slot].values));
        break;
    }
    case CS2_OP_PUSH_ARRAY_INT:
    {
        int index = cs2_rt_pop_int(rt);
        int value = 0;
        if( operand >= 0 && operand < CS2_RT_MAX_ARRAYS && rt->arrays[operand].defined &&
            index >= 0 && index < rt->arrays[operand].size )
            value = rt->arrays[operand].values[index];
        CS2_RT_PUSH_INT(rt, value);
        break;
    }
    case CS2_OP_POP_ARRAY_INT:
    {
        int index = cs2_rt_pop_int(rt);
        int value = cs2_rt_pop_int(rt);
        if( operand >= 0 && operand < CS2_RT_MAX_ARRAYS && rt->arrays[operand].defined &&
            index >= 0 && index < rt->arrays[operand].size )
            rt->arrays[operand].values[index] = value;
        break;
    }
    case CS2_OP_ENUM:
    {
        int key = cs2_rt_pop_int(rt);
        int enum_id = cs2_rt_pop_int(rt);
        int output_type = cs2_rt_pop_int(rt);
        int input_type = cs2_rt_pop_int(rt);
        (void)input_type;
        if( host && host->enum_lookup_string )
        {
            char const* str =
                host->enum_lookup_string(host->ud, input_type, output_type, enum_id, key);
            if( str )
            {
                CS2_RT_PUSH_STR(rt, cs2_rt_alloc_string(rt, str));
                break;
            }
        }
        if( output_type == (int)'s' )
        {
            CS2_RT_PUSH_STR(rt, cs2_rt_alloc_string(rt, "null"));
            break;
        }
        int value = -1;
        if( host && host->enum_lookup )
            value = host->enum_lookup(host->ud, input_type, output_type, enum_id, key);
        CS2_RT_PUSH_INT(rt, value);
        break;
    }
    case CS2_OP_RETURN:
    {
        if( rt->frame_sp > 1 )
        {
            struct CS2VMFrame* returning = frame;
            if( s_cs2_trace )
            {
                fprintf(
                    stderr,
                    "cs2vm trace: RETURN script=%d frame_sp=%d->%d pc=%d\n",
                    returning->script ? returning->script->script_id : -1,
                    rt->frame_sp,
                    rt->frame_sp - 1,
                    returning->pc);
            }
            rt->frame_sp--;
            struct CS2VMFrame* caller = cs2_rt_current_frame(rt);
            assert(caller);
            assert(caller->script);
            assert(caller->return_pc >= 0);
            assert(caller->return_pc <= caller->script->op_count);
            caller->pc = caller->return_pc;
            return true;
        }
        return false;
    }
    default:
        cs2_rt_host_invoke(rt, host, opcode, operand);
        break;
    }
    return rt->run_error == 0;
}

static void
cs2_rt_reset(
    struct CS2VM* rt,
    struct CS2_Script const* script,
    struct CS2_RunArgs const* args)
{
    memset(rt->int_stack, 0, sizeof(rt->int_stack));
    memset(rt->str_stack, 0, sizeof(rt->str_stack));
    rt->int_sp = 0;
    rt->str_sp = 0;
    rt->string_pool_used = 0;
    rt->frame_sp = 0;
    rt->active_component = -1;
    rt->dot_component = -1;
    rt->run_error = 0;
    rt->halted = false;
    rt->last_error_valid = false;
    rt->last_error_opcode = -1;
    rt->last_error_pc = -1;
    rt->last_error_script_id = -1;
    memset(rt->arrays, 0, sizeof(rt->arrays));

    struct CS2VMFrame* frame = &rt->frames[rt->frame_sp++];
    memset(frame, 0, sizeof(*frame));
    frame->script = script;
    frame->pc = 0;

    if( args )
    {
        int sarg = 0;
        for( int i = 0; i < script->string_argument_count; i++ )
        {
            char const* s =
                args->string_argv && sarg < args->string_argc ? args->string_argv[sarg++] : "";
            frame->str_locals[i] = cs2_rt_alloc_string(rt, s ? s : "");
        }

        /* Match TS loadArgsIntoLocals: hook/event int args fill int_locals[0..]
         * regardless of script header int_argument_count (e.g. onLoad [3281,2775,1]). */
        if( args->int_argv && args->int_argc > 0 )
        {
            int const max_int_locals =
                script->local_int_count > 0 ? script->local_int_count : CS2_RT_MAX_LOCALS;
            int n = args->int_argc < max_int_locals ? args->int_argc : max_int_locals;
            for( int i = 0; i < n; i++ )
                frame->int_locals[i] = args->int_argv[i];
        }
    }
}

int
cs2_script_arg_substitute_int(
    int value,
    struct CS2_ScriptArgSubstitute const* sub)
{
    if( !sub )
        return value;
    switch( value )
    {
    case CS2_SCRIPT_ARG_MAGIC_WIDGET_ID:
        return sub->component_id;
    case CS2_SCRIPT_ARG_MAGIC_MOUSE_X:
    case CS2_SCRIPT_ARG_MAGIC_MOUSE_Y:
        return 0;
    case CS2_SCRIPT_ARG_MAGIC_OP_INDEX:
        return 1;
    case CS2_SCRIPT_ARG_MAGIC_WIDGET_CHILD_INDEX:
    case CS2_SCRIPT_ARG_MAGIC_DRAG_TARGET_ID:
    case CS2_SCRIPT_ARG_MAGIC_DRAG_TARGET_CHILD_INDEX:
        return -1;
    default:
        return value;
    }
}

int
cs2vm_run(
    struct CS2VM* rt,
    struct CS2_Script const* script,
    struct CS2Host const* host,
    struct CS2_RunArgs const* args)
{
    assert(rt);
    assert(script);
    assert(script->op_count > 0);

    cs2_rt_ensure_trace_env();
    int const initial_active = rt->active_component;
    int const initial_dot = rt->dot_component;
    cs2_rt_reset(rt, script, args);
    rt->active_component = initial_active;
    rt->dot_component = initial_dot;
    rt->opcount = 0;
    if( s_cs2_trace_json )
        s_trace_count = 0;

    for( int steps = 0; rt->frame_sp > 0; steps++ )
    {
        if( steps >= CS2_RT_MAX_STEPS )
        {
            struct CS2VMFrame* frame = cs2_rt_current_frame(rt);
            const struct CS2_OpcodeMeta* meta = cs2_opcode_meta_lookup(
                frame->script
                    ->opcodes[frame->pc < frame->script->op_count ? frame->pc : frame->pc - 1]);
            fprintf(
                stderr,
                "cs2vm: step limit exceeded script_id=%d frame_sp=%d pc=%d opcode=%s\n",
                frame->script->script_id,
                rt->frame_sp,
                frame->pc,
                meta->name ? meta->name : "?");
            return CS2VM_ERR_STEP_LIMIT;
        }

        struct CS2VMFrame* frame = cs2_rt_current_frame(rt);
        if( frame->pc < 0 || frame->pc >= frame->script->op_count )
        {
            fprintf(
                stderr,
                "cs2vm: pc out of bounds script_id=%d pc=%d op_count=%d\n",
                frame->script->script_id,
                frame->pc,
                frame->script->op_count);
            assert(frame->pc >= 0);
            assert(frame->pc < frame->script->op_count);
            return CS2VM_ERR_PC_OOB;
        }

        int pc = frame->pc++;
        int opcode = frame->script->opcodes[pc];
        int operand = frame->script->int_operands[pc];
        char const* str_operand =
            frame->script->string_operands ? frame->script->string_operands[pc] : NULL;

        if( s_cs2_trace )
        {
            const struct CS2_OpcodeMeta* tmeta = cs2_opcode_meta_lookup(opcode);
            fprintf(
                stderr,
                "cs2vm trace: script=%d pc=%d op=%s(%d) operand=%d int_sp=%d str_sp=%d "
                "active=0x%x\n",
                frame->script->script_id,
                pc,
                tmeta && tmeta->name ? tmeta->name : "?",
                opcode,
                operand,
                rt->int_sp,
                rt->str_sp,
                (unsigned)rt->active_component);
        }

        if( s_cs2_trace_json )
        {
            int top_int = rt->int_sp > 0 ? rt->int_stack[rt->int_sp - 1] : 0;
            if( s_trace_count < CS2_TRACE_BUF_MAX )
            {
                s_trace_buf[s_trace_count++] = (struct CS2TraceEntry){
                    .step = rt->opcount,
                    .pc = pc,
                    .opcode = opcode,
                    .int_sp = rt->int_sp,
                    .str_sp = rt->str_sp,
                    .top_int = top_int,
                };
            }
            fprintf(
                stderr,
                "{\"step\":%d,\"pc\":%d,\"opcode\":%d,\"intSp\":%d,\"strSp\":%d,\"topInt\":%d}\n",
                rt->opcount,
                pc,
                opcode,
                rt->int_sp,
                rt->str_sp,
                top_int);
        }

        rt->opcount++;

        if( !cs2_rt_exec_opcode(rt, host, opcode, operand, str_operand) )
        {
            cs2_rt_record_error(rt, opcode, pc, frame->script->script_id);
            break;
        }
        if( rt->run_error != 0 )
        {
            cs2_rt_record_error(rt, opcode, pc, frame->script->script_id);
            break;
        }
        if( rt->halted )
            break;
    }
    if( rt->run_error != 0 )
        return rt->run_error;
    return CS2VM_OK;
}

int
cs2vm_host_pop_int(struct CS2_InvokeCtx* ctx)
{
    int value = 0;
    if( !cs2vm_host_try_pop_int(ctx, &value) && ctx && ctx->vm )
        ctx->vm->run_error = CS2VM_ERR_STACK;
    return value;
}

bool
cs2vm_host_try_pop_int(
    struct CS2_InvokeCtx* ctx,
    int* out)
{
    if( !ctx || !ctx->vm || ctx->vm->int_sp <= 0 )
        return false;
    int value = ctx->vm->int_stack[--ctx->vm->int_sp];
    if( out )
        *out = value;
    return true;
}

void
cs2vm_host_push_int(
    struct CS2_InvokeCtx* ctx,
    int value)
{
    if( !ctx || !ctx->vm )
        return;
    if( ctx->vm->int_sp >= CS2_SCRIPT_STACK_MAX )
    {
        ctx->vm->run_error = CS2VM_ERR_STACK;
        return;
    }
    ctx->vm->int_stack[ctx->vm->int_sp++] = value;
}

char*
cs2vm_host_pop_string(struct CS2_InvokeCtx* ctx)
{
    char* value = NULL;
    if( !cs2vm_host_try_pop_string(ctx, &value) && ctx && ctx->vm )
    {
        ctx->vm->run_error = CS2VM_ERR_STACK;
        return "";
    }
    return value ? value : "";
}

bool
cs2vm_host_try_pop_string(
    struct CS2_InvokeCtx* ctx,
    char** out)
{
    if( !ctx || !ctx->vm || ctx->vm->str_sp <= 0 )
        return false;
    char* value = ctx->vm->str_stack[--ctx->vm->str_sp];
    if( out )
        *out = value;
    return true;
}

void
cs2vm_host_push_string(
    struct CS2_InvokeCtx* ctx,
    char* value)
{
    if( !ctx || !ctx->vm )
        return;
    if( ctx->vm->str_sp >= CS2_SCRIPT_STACK_MAX )
    {
        ctx->vm->run_error = CS2VM_ERR_STACK;
        return;
    }
    ctx->vm->str_stack[ctx->vm->str_sp++] = value;
}

char*
cs2vm_host_alloc_string(
    struct CS2_InvokeCtx* ctx,
    char const* src)
{
    if( !ctx || !ctx->vm )
        return NULL;
    return cs2_rt_alloc_string(ctx->vm, src ? src : "");
}

void
cs2vm_host_set_active_component(
    struct CS2_InvokeCtx* ctx,
    int component_id)
{
    assert(ctx);
    assert(ctx->vm);
    ctx->vm->active_component = component_id;
    ctx->active_component = component_id;
}

void
cs2vm_set_active_component(
    struct CS2VM* vm,
    int component_id)
{
    assert(vm);
    vm->active_component = component_id;
}

int
cs2vm_get_active_component(struct CS2VM const* vm)
{
    assert(vm);
    return vm->active_component;
}

void
cs2vm_host_set_dot_component(
    struct CS2_InvokeCtx* ctx,
    int component_id)
{
    assert(ctx);
    assert(ctx->vm);
    ctx->vm->dot_component = component_id;
    ctx->dot_component = component_id;
}

void
cs2vm_set_dot_component(
    struct CS2VM* vm,
    int component_id)
{
    assert(vm);
    vm->dot_component = component_id;
}

int
cs2vm_get_dot_component(struct CS2VM const* vm)
{
    assert(vm);
    return vm->dot_component;
}
