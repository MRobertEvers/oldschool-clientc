#include "cs2vm.h"

#include "cs2_opcode.h"
#include "cs2_opcode_meta.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS2_RT_MAX_FRAMES 32
#define CS2_RT_MAX_LOCALS 128
#define CS2_RT_MAX_ARRAYS 32
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
    struct CS2VMArray arrays[CS2_RT_MAX_ARRAYS];
};

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

static int
cs2_rt_pop_int(struct CS2VM* rt)
{
    assert(rt);
    assert(rt->int_sp > 0);
    return rt->int_stack[--rt->int_sp];
}

static void
cs2_rt_push_int(
    struct CS2VM* rt,
    int value)
{
    assert(rt);
    assert(rt->int_sp < CS2_SCRIPT_STACK_MAX);
    rt->int_stack[rt->int_sp++] = value;
}

static char*
cs2_rt_pop_str(struct CS2VM* rt)
{
    assert(rt);
    assert(rt->str_sp > 0);
    return rt->str_stack[--rt->str_sp];
}

static void
cs2_rt_push_str(
    struct CS2VM* rt,
    char* value)
{
    assert(rt);
    assert(rt->str_sp < CS2_SCRIPT_STACK_MAX);
    rt->str_stack[rt->str_sp++] = value;
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
    };
    host->invoke(host->ud, &ctx);
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
        return true;
    }

    switch( opcode )
    {
    case CS2_OP_PUSH_CONSTANT_INT:
        cs2_rt_push_int(rt, operand);
        break;
    case CS2_OP_PUSH_CONSTANT_STRING:
        cs2_rt_push_str(rt, cs2_rt_alloc_string(rt, str_operand ? str_operand : ""));
        break;
    case CS2_OP_PUSH_VAR:
        cs2_rt_push_int(rt, host && host->get_varp ? host->get_varp(host->ud, operand) : 0);
        break;
    case CS2_OP_POP_VAR:
        if( host && host->set_varp )
            host->set_varp(host->ud, operand, cs2_rt_pop_int(rt));
        else
            (void)cs2_rt_pop_int(rt);
        break;
    case CS2_OP_PUSH_VARBIT:
        cs2_rt_push_int(rt, host && host->get_varbit ? host->get_varbit(host->ud, operand) : 0);
        break;
    case CS2_OP_POP_VARBIT:
        if( host && host->set_varbit )
            host->set_varbit(host->ud, operand, cs2_rt_pop_int(rt));
        else
            (void)cs2_rt_pop_int(rt);
        break;
    case CS2_OP_PUSH_VARC_INT:
        cs2_rt_push_int(rt, host && host->get_varc_int ? host->get_varc_int(host->ud, operand) : 0);
        break;
    case CS2_OP_POP_VARC_INT:
        if( host && host->set_varc_int )
            host->set_varc_int(host->ud, operand, cs2_rt_pop_int(rt));
        else
            (void)cs2_rt_pop_int(rt);
        break;
    case CS2_OP_PUSH_VARC_STRING:
        cs2_rt_push_str(
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
        cs2_rt_push_int(rt, frame->int_locals[operand]);
        break;
    case CS2_OP_POP_INT_LOCAL:
        assert(operand >= 0 && operand < CS2_RT_MAX_LOCALS);
        frame->int_locals[operand] = cs2_rt_pop_int(rt);
        break;
    case CS2_OP_PUSH_STRING_LOCAL:
        assert(operand >= 0 && operand < CS2_RT_MAX_LOCALS);
        cs2_rt_push_str(rt, frame->str_locals[operand]);
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
        cs2_rt_push_str(rt, cs2_rt_alloc_string(rt, buf));
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
        int cond = cs2_rt_pop_int(rt);
        if( !cond )
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
                    frame->pc = sw->cases[i].target_pc;
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
        cs2_rt_push_int(rt, a + b);
        break;
    }
    case CS2_OP_SUB:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        cs2_rt_push_int(rt, a - b);
        break;
    }
    case CS2_OP_MULTIPLY:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        cs2_rt_push_int(rt, a * b);
        break;
    }
    case CS2_OP_DIV:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        cs2_rt_push_int(rt, b != 0 ? a / b : 0);
        break;
    }
    case CS2_OP_MOD:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        cs2_rt_push_int(rt, b != 0 ? a % b : 0);
        break;
    }
    case CS2_OP_AND:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        cs2_rt_push_int(rt, a & b);
        break;
    }
    case CS2_OP_OR:
    {
        int b = cs2_rt_pop_int(rt);
        int a = cs2_rt_pop_int(rt);
        cs2_rt_push_int(rt, a | b);
        break;
    }
    case CS2_OP_TESTBIT:
    {
        int bit = cs2_rt_pop_int(rt);
        int value = cs2_rt_pop_int(rt);
        cs2_rt_push_int(rt, (value & (1 << bit)) != 0);
        break;
    }
    case CS2_OP_GOSUB_WITH_PARAMS:
    {
        assert(host);
        assert(host->resolve_script);
        assert(rt->frame_sp < CS2_RT_MAX_FRAMES);
        struct CS2_Script* callee = host->resolve_script(host->ud, operand);
        assert(callee);
        struct CS2VMFrame* caller = frame;
        caller->return_pc = caller->pc;
        caller->return_frame = rt->frame_sp - 1;
        struct CS2VMFrame* next = &rt->frames[rt->frame_sp++];
        memset(next, 0, sizeof(*next));
        next->script = callee;
        next->pc = 0;
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
        assert(operand >= 0 && operand < CS2_RT_MAX_ARRAYS);
        if( size < 0 )
            size = 0;
        if( size > CS2_RT_ARRAY_CAPACITY )
            size = CS2_RT_ARRAY_CAPACITY;
        rt->arrays[operand].defined = true;
        rt->arrays[operand].size = size;
        memset(rt->arrays[operand].values, 0, sizeof(rt->arrays[operand].values));
        break;
    }
    case CS2_OP_PUSH_ARRAY_INT:
    {
        int index = cs2_rt_pop_int(rt);
        int value = 0;
        if( operand >= 0 && operand < CS2_RT_MAX_ARRAYS && rt->arrays[operand].defined &&
            index >= 0 && index < rt->arrays[operand].size )
            value = rt->arrays[operand].values[index];
        cs2_rt_push_int(rt, value);
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
        (void)cs2_rt_pop_int(rt);
        (void)cs2_rt_pop_int(rt);
        cs2_rt_push_int(rt, 0);
        break;
    }
    case CS2_OP_RETURN:
    {
        int ret_val = cs2_rt_pop_int(rt);
        if( rt->frame_sp > 1 )
        {
            rt->frame_sp--;
            struct CS2VMFrame* caller = cs2_rt_current_frame(rt);
            assert(caller);
            assert(caller->script);
            assert(caller->return_pc >= 0);
            assert(caller->return_pc <= caller->script->op_count);
            caller->pc = caller->return_pc;
            cs2_rt_push_int(rt, ret_val);
            return true;
        }
        return false;
    }
    default:
        cs2_rt_host_invoke(rt, host, opcode, operand);
        break;
    }
    return true;
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
    memset(rt->arrays, 0, sizeof(rt->arrays));

    struct CS2VMFrame* frame = &rt->frames[rt->frame_sp++];
    memset(frame, 0, sizeof(*frame));
    frame->script = script;
    frame->pc = 0;

    if( args )
    {
        int iarg = 0;
        int sarg = 0;
        for( int i = 0; i < script->int_argument_count + script->string_argument_count; i++ )
        {
            if( i < script->string_argument_count )
            {
                char const* s =
                    args->string_argv && sarg < args->string_argc ? args->string_argv[sarg] : "";
                frame->str_locals[i] = cs2_rt_alloc_string(rt, s ? s : "");
                sarg++;
            }
            else
            {
                int v = args->int_argv && iarg < args->int_argc ? args->int_argv[iarg] : 0;
                frame->int_locals[i - script->string_argument_count] = v;
                iarg++;
            }
        }
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

    cs2_rt_reset(rt, script, args);

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

        if( !cs2_rt_exec_opcode(rt, host, opcode, operand, str_operand) )
            break;
    }
    return CS2VM_OK;
}

int
cs2vm_host_pop_int(struct CS2_InvokeCtx* ctx)
{
    assert(ctx);
    assert(ctx->vm);
    assert(ctx->vm->int_sp > 0);
    return ctx->vm->int_stack[--ctx->vm->int_sp];
}

void
cs2vm_host_push_int(
    struct CS2_InvokeCtx* ctx,
    int value)
{
    assert(ctx);
    assert(ctx->vm);
    assert(ctx->vm->int_sp < CS2_SCRIPT_STACK_MAX);
    ctx->vm->int_stack[ctx->vm->int_sp++] = value;
}

char*
cs2vm_host_pop_string(struct CS2_InvokeCtx* ctx)
{
    assert(ctx);
    assert(ctx->vm);
    assert(ctx->vm->str_sp > 0);
    return ctx->vm->str_stack[--ctx->vm->str_sp];
}

void
cs2vm_host_push_string(
    struct CS2_InvokeCtx* ctx,
    char* value)
{
    assert(ctx);
    assert(ctx->vm);
    assert(ctx->vm->str_sp < CS2_SCRIPT_STACK_MAX);
    ctx->vm->str_stack[ctx->vm->str_sp++] = value;
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
