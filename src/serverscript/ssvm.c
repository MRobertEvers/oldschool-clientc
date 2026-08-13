#include "ssvm.h"

#include "ss_opcode.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* java.util.Random                                                    */
/* ------------------------------------------------------------------ */

/*
 * Ported rather than substituted so RANDOM/RANDOMINC reproduce the reference's
 * sequence for a given seed. That is not nostalgia: it is what lets a mock
 * session replay identically, which every deterministic test downstream needs.
 */

#define JAVA_RANDOM_MULTIPLIER 0x5DEECE66Dull
#define JAVA_RANDOM_ADDEND 0xBull
#define JAVA_RANDOM_MASK ((1ull << 48) - 1)

static int32_t
java_next(struct SSVM_Env* env, int bits)
{
    env->rng = (env->rng * JAVA_RANDOM_MULTIPLIER + JAVA_RANDOM_ADDEND) & JAVA_RANDOM_MASK;
    return (int32_t)(env->rng >> (48 - bits));
}

static double
java_next_double(struct SSVM_Env* env)
{
    int64_t hi = (int64_t)java_next(env, 26) << 27;
    int64_t lo = java_next(env, 27);

    return (double)(hi + lo) / (double)(1ll << 53);
}

/* ------------------------------------------------------------------ */
/* Small numeric helpers                                               */
/* ------------------------------------------------------------------ */

/* JavaScript ToInt32: truncate toward zero, then wrap into signed 32 bits. The
 * reference's pushInt applies this to every value, so arithmetic that overflows
 * wraps there and has to wrap here too. */
static int32_t
to_int32(double value)
{
    double truncated;

    if( value != value ) /* NaN */
        return 0;
    truncated = value < 0 ? -(double)(uint64_t)(-value) : (double)(uint64_t)value;
    return (int32_t)(uint32_t)(int64_t)truncated;
}

static int32_t
floor_div(int32_t numerator, int32_t denominator)
{
    int32_t quotient = numerator / denominator;

    if( (numerator % denominator != 0) && ((numerator < 0) != (denominator < 0)) )
        quotient--;
    return quotient;
}

static int32_t
int_sqrt(int32_t value)
{
    int32_t root = 0;

    if( value <= 0 )
        return 0;
    while( (int64_t)(root + 1) * (root + 1) <= value )
        root++;
    return root;
}

static int32_t
int_cbrt(int32_t value)
{
    int32_t root = 0;
    int negative = value < 0;
    int64_t magnitude = negative ? -(int64_t)value : value;

    while( (int64_t)(root + 1) * (root + 1) * (root + 1) <= magnitude )
        root++;
    return negative ? -root : root;
}

/* ------------------------------------------------------------------ */
/* Abort and backtrace                                                 */
/* ------------------------------------------------------------------ */

void
SSVM_Abort(
    struct SSVM_State* state,
    const char* fmt,
    ...)
{
    va_list args;

    if( !state )
        return;

    va_start(args, fmt);
    vsnprintf(state->err.message, sizeof(state->err.message), fmt, args);
    va_end(args);

    state->err.script_id = state->script ? state->script->id : -1;
    state->err.offset = state->pc;
    state->execution = SSVM_ABORTED;
}

const char*
SSVM_Backtrace(struct SSVM_State* state)
{
    /* One shared buffer: only one abort is ever being read at a time, and
     * keeping ~1 KB out of every parked state matters more than reentrancy on
     * a pure diagnostic path. */
    static char buffer[1024];
    size_t used = 0;
    int32_t frame;

    buffer[0] = '\0';
    if( !state || state->execution != SSVM_ABORTED )
        return buffer;

    used += (size_t)snprintf(buffer + used, sizeof(buffer) - used, "%s\n", state->err.message);

    if( state->script )
    {
        used += (size_t)snprintf(
            buffer + used, sizeof(buffer) - used, "  at %s - %s:%d (pc %d, op %s)\n",
            state->script->name ? state->script->name : "?", SSVM_ScriptFileName(state->script),
            SSVM_ScriptLineNumber(state->script, state->pc), state->pc,
            (state->pc >= 0 && state->pc < state->script->op_count)
                ? SSVM_OpcodeName(state->script->opcodes[state->pc])
                : "-");
    }

    for( frame = state->fp - 1; frame >= 0 && used + 64 < sizeof(buffer); frame-- )
    {
        const struct SSVM_Script* script = state->frames[frame].script;

        if( !script )
            continue;
        used += (size_t)snprintf(
            buffer + used, sizeof(buffer) - used, "  from %s - %s:%d\n",
            script->name ? script->name : "?", SSVM_ScriptFileName(script),
            SSVM_ScriptLineNumber(script, state->frames[frame].pc));
    }
    return buffer;
}

/* ------------------------------------------------------------------ */
/* Stacks                                                              */
/* ------------------------------------------------------------------ */

int
SSVM_PushInt(
    struct SSVM_State* state,
    int32_t value)
{
    if( state->isp >= SSVM_INT_STACK_MAX )
    {
        SSVM_Abort(state, "int stack overflow (%d entries)", SSVM_INT_STACK_MAX);
        return 0;
    }
    state->int_stack[state->isp++] = value;
    return 1;
}

int
SSVM_PopInt(
    struct SSVM_State* state,
    int32_t* out)
{
    if( state->isp <= 0 )
    {
        /* The reference returns 0 here, because `intStack[--isp]` past the
         * bottom is undefined and its popInt treats falsy as 0. Silently
         * substituting 0 turns a stack-discipline bug into a wrong answer
         * hundreds of instructions later, so this aborts instead. */
        SSVM_Abort(state, "int stack underflow");
        if( out )
            *out = 0;
        return 0;
    }
    if( out )
        *out = state->int_stack[--state->isp];
    else
        state->isp--;
    return 1;
}

static int
push_str_borrowed(struct SSVM_State* state, const char* text)
{
    if( state->ssp >= SSVM_STR_STACK_MAX )
    {
        SSVM_Abort(state, "string stack overflow (%d entries)", SSVM_STR_STACK_MAX);
        return 0;
    }
    state->str_stack[state->ssp++] = text ? text : "";
    return 1;
}

int
SSVM_PopStr(
    struct SSVM_State* state,
    const char** out)
{
    if( state->ssp <= 0 )
    {
        SSVM_Abort(state, "string stack underflow");
        if( out )
            *out = "";
        return 0;
    }
    if( out )
        *out = state->str_stack[--state->ssp];
    else
        state->ssp--;
    return 1;
}

int
SSVM_PushStr(
    struct SSVM_State* state,
    const char* text)
{
    char* copy;

    /* Copied because a host's buffer is usually a local. A pointer that came
     * out of the pool or out of a script's constants can be pushed with
     * push_str_borrowed instead. */
    copy = SSVM_StrPoolDup(&state->pool, text);
    if( !copy )
    {
        SSVM_Abort(state, "string pool exhausted");
        return 0;
    }
    return push_str_borrowed(state, copy);
}

int
SSVM_PushStrFmt(
    struct SSVM_State* state,
    const char* fmt,
    ...)
{
    va_list args;
    char* text;

    va_start(args, fmt);
    {
        char scratch[512];

        vsnprintf(scratch, sizeof(scratch), fmt, args);
        text = SSVM_StrPoolDup(&state->pool, scratch);
    }
    va_end(args);

    if( !text )
    {
        SSVM_Abort(state, "string pool exhausted");
        return 0;
    }
    return push_str_borrowed(state, text);
}

/* ------------------------------------------------------------------ */
/* Active entities                                                     */
/* ------------------------------------------------------------------ */

void*
SSVM_Active(
    struct SSVM_State* state,
    enum SSVM_EntKind kind)
{
    return state->active[kind][state->dot ? SSVM_SECONDARY : SSVM_PRIMARY];
}

void*
SSVM_ActiveSlot(
    struct SSVM_State* state,
    enum SSVM_EntKind kind,
    int slot)
{
    return state->active[kind][slot ? SSVM_SECONDARY : SSVM_PRIMARY];
}

void
SSVM_SetActive(
    struct SSVM_State* state,
    enum SSVM_EntKind kind,
    int slot,
    void* entity)
{
    static const uint32_t k_bits[SSVM_ENT_KIND_COUNT][2] = {
        { SSVM_PTR_ACTIVE_PLAYER, SSVM_PTR_ACTIVE_PLAYER2 },
        { SSVM_PTR_ACTIVE_NPC, SSVM_PTR_ACTIVE_NPC2 },
        { SSVM_PTR_ACTIVE_LOC, SSVM_PTR_ACTIVE_LOC2 },
        { SSVM_PTR_ACTIVE_OBJ, SSVM_PTR_ACTIVE_OBJ2 },
    };
    int index = slot ? SSVM_SECONDARY : SSVM_PRIMARY;

    state->active[kind][index] = entity;
    if( entity )
        state->pointers |= k_bits[kind][index];
    else
        state->pointers &= ~k_bits[kind][index];
}

void
SSVM_PointerAdd(
    struct SSVM_State* state,
    uint32_t bits)
{
    state->pointers |= bits;
}

void
SSVM_PointerRemove(
    struct SSVM_State* state,
    uint32_t bits)
{
    state->pointers &= ~bits;
}

void
SSVM_Suspend(
    struct SSVM_State* state,
    enum SSVM_Exec status)
{
    state->execution = status;
}

/* ------------------------------------------------------------------ */
/* Frames                                                              */
/* ------------------------------------------------------------------ */

static void*
locals_alloc(struct SSVM_State* state, size_t bytes)
{
    void* out;

    /* Keep every allocation pointer-aligned; the arena hands out both int32
     * arrays and char* arrays. */
    bytes = (bytes + 7u) & ~(size_t)7u;
    if( state->locals_used + bytes > SSVM_LOCALS_ARENA_BYTES )
        return NULL;

    out = state->locals_arena + state->locals_used;
    state->locals_used += bytes;
    memset(out, 0, bytes);
    return out;
}

/**
 * Bind a script's locals and arguments and make it the running script.
 *
 * Arguments come off the stack in reverse so their source order is preserved:
 * `~proc(1, 2)` pushes 1 then 2, and the callee sees $arg0 == 1.
 */
static int
setup_new_script(struct SSVM_State* state, const struct SSVM_Script* script)
{
    int32_t* int_locals = NULL;
    char** str_locals = NULL;
    int i;

    if( script->int_local_count )
    {
        int_locals = (int32_t*)locals_alloc(state, script->int_local_count * sizeof(int32_t));
        if( !int_locals )
        {
            SSVM_Abort(state, "locals arena exhausted entering %s", script->name);
            return 0;
        }
    }
    if( script->string_local_count )
    {
        str_locals = (char**)locals_alloc(state, script->string_local_count * sizeof(char*));
        if( !str_locals )
        {
            SSVM_Abort(state, "locals arena exhausted entering %s", script->name);
            return 0;
        }
    }

    for( i = script->int_arg_count - 1; i >= 0; i-- )
    {
        int32_t value;

        if( !SSVM_PopInt(state, &value) )
            return 0;
        int_locals[i] = value;
    }
    for( i = script->string_arg_count - 1; i >= 0; i-- )
    {
        const char* value;

        if( !SSVM_PopStr(state, &value) )
            return 0;
        str_locals[i] = (char*)value;
    }

    state->script = script;
    state->pc = -1;
    state->int_locals = int_locals;
    state->str_locals = str_locals;
    return 1;
}

static int
gosub_frame(struct SSVM_State* state, const struct SSVM_Script* script)
{
    struct SSVM_Frame* frame;

    if( state->fp >= SSVM_MAX_FRAMES )
    {
        SSVM_Abort(state, "gosub stack overflow (%d frames)", SSVM_MAX_FRAMES);
        return 0;
    }

    frame = &state->frames[state->fp++];
    frame->script = state->script;
    frame->pc = state->pc;
    frame->int_locals = state->int_locals;
    frame->str_locals = state->str_locals;
    frame->locals_mark = state->locals_used;

    return setup_new_script(state, script);
}

static void
pop_frame(struct SSVM_State* state)
{
    struct SSVM_Frame* frame = &state->frames[--state->fp];

    state->script = frame->script;
    state->pc = frame->pc;
    state->int_locals = frame->int_locals;
    state->str_locals = frame->str_locals;
    state->locals_used = frame->locals_mark;
}

/** JUMP: replace the whole call stack rather than pushing onto it. */
static int
goto_frame(struct SSVM_State* state, const struct SSVM_Script* script)
{
    /* Rewinding the arena is what stops a `@label` loop leaking a frame's worth
     * of locals per iteration. Dropping fp alone would keep every allocation
     * alive until the state was released. */
    state->fp = 0;
    state->locals_used = 0;
    return setup_new_script(state, script);
}

/* ------------------------------------------------------------------ */
/* The loud stub                                                       */
/* ------------------------------------------------------------------ */

/**
 * Every opcode neither the VM nor the host implements ends up here.
 *
 * Two cases, and the difference matters:
 *
 *   known == 0  the arity is genuinely unknown, so there is no way to leave the
 *               stack consistent. Abort.
 *
 *   known == 1  the signature is known but nothing implements the behaviour.
 *               Pop what it declares, push zeros for what it returns, and say
 *               so once. This is where src/cs2vm2's equivalent stays silent,
 *               and staying silent is how oc_examine shipped returning an int
 *               where content expected a string. A server that quietly pretends
 *               inv_total returned 0 produces a bug report about content.
 */
static int
unimplemented_stub(struct SSVM_State* state, int opcode)
{
    const struct SSVM_OpcodeMeta* meta = SSVM_OpcodeMeta(opcode);
    int i;

    if( !meta->known )
    {
        SSVM_Abort(
            state, "opcode %d (%s) has no known signature and cannot be executed", opcode,
            SSVM_OpcodeName(opcode));
        return 0;
    }

    if( state->env->reported && opcode >= 0 && opcode < SS_OPCODE_MAX &&
        !state->env->reported[opcode] )
    {
        state->env->reported[opcode] = 1;
        fprintf(
            stderr, "ssvm: %s is not implemented; returning defaults (in %s)\n",
            SSVM_OpcodeName(opcode), state->script && state->script->name ? state->script->name : "?");
    }

    if( meta->variadic )
    {
        SSVM_Abort(
            state, "opcode %d (%s) is variadic and cannot be stubbed", opcode,
            SSVM_OpcodeName(opcode));
        return 0;
    }

    for( i = 0; i < meta->str_in; i++ )
    {
        if( !SSVM_PopStr(state, NULL) )
            return 0;
    }
    for( i = 0; i < meta->int_in; i++ )
    {
        if( !SSVM_PopInt(state, NULL) )
            return 0;
    }
    for( i = 0; i < meta->int_out; i++ )
    {
        if( !SSVM_PushInt(state, 0) )
            return 0;
    }
    for( i = 0; i < meta->str_out; i++ )
    {
        if( !push_str_borrowed(state, "") )
            return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Opcode helpers                                                      */
/* ------------------------------------------------------------------ */

/* Pop n ints into out[0..n-1] in source order: out[0] was pushed first. */
static int
pop_ints(struct SSVM_State* state, int32_t* out, int count)
{
    int i;

    for( i = count - 1; i >= 0; i-- )
    {
        if( !SSVM_PopInt(state, &out[i]) )
            return 0;
    }
    return 1;
}

static int
pop_strs(struct SSVM_State* state, const char** out, int count)
{
    int i;

    for( i = count - 1; i >= 0; i-- )
    {
        if( !SSVM_PopStr(state, &out[i]) )
            return 0;
    }
    return 1;
}

#define BINARY_INT_OP(expr)                                                    \
    do                                                                         \
    {                                                                          \
        int32_t v[2];                                                          \
                                                                               \
        if( !pop_ints(state, v, 2) )                                           \
            return 0;                                                          \
        return SSVM_PushInt(state, (expr));                                    \
    } while( 0 )

/* ------------------------------------------------------------------ */
/* Core language                                                       */
/* ------------------------------------------------------------------ */

static int
run_core_op(struct SSVM_State* state, int opcode, int32_t operand, const char* str_operand)
{
    switch( opcode )
    {
    case SS_OP_PUSH_CONSTANT_INT:
        return SSVM_PushInt(state, operand);

    case SS_OP_PUSH_CONSTANT_STRING:
        /* Borrowed straight from the script: constants are immutable and the
         * provider outlives every state, so copying would be pure waste. */
        return push_str_borrowed(state, str_operand);

    case SS_OP_PUSH_INT_LOCAL:
        if( operand < 0 || operand >= state->script->int_local_count )
        {
            SSVM_Abort(state, "int local %d out of range", operand);
            return 0;
        }
        return SSVM_PushInt(state, state->int_locals[operand]);

    case SS_OP_POP_INT_LOCAL:
    {
        int32_t value;

        if( operand < 0 || operand >= state->script->int_local_count )
        {
            SSVM_Abort(state, "int local %d out of range", operand);
            return 0;
        }
        if( !SSVM_PopInt(state, &value) )
            return 0;
        state->int_locals[operand] = value;
        return 1;
    }

    case SS_OP_PUSH_STRING_LOCAL:
        if( operand < 0 || operand >= state->script->string_local_count )
        {
            SSVM_Abort(state, "string local %d out of range", operand);
            return 0;
        }
        return push_str_borrowed(state, state->str_locals[operand]);

    case SS_OP_POP_STRING_LOCAL:
    {
        const char* value;

        if( operand < 0 || operand >= state->script->string_local_count )
        {
            SSVM_Abort(state, "string local %d out of range", operand);
            return 0;
        }
        if( !SSVM_PopStr(state, &value) )
            return 0;
        state->str_locals[operand] = (char*)value;
        return 1;
    }

    case SS_OP_POP_INT_DISCARD:
        return SSVM_PopInt(state, NULL);

    case SS_OP_POP_STRING_DISCARD:
        return SSVM_PopStr(state, NULL);

    /* Branch operands are instruction-index deltas. pc += delta here, then the
     * loop's ++pc lands on delta + 1 — which is why a byte-offset reading works
     * on a hand-built test and fails on every real script. */
    case SS_OP_BRANCH:
        state->pc += operand;
        return 1;

    case SS_OP_BRANCH_NOT:
    case SS_OP_BRANCH_EQUALS:
    case SS_OP_BRANCH_LESS_THAN:
    case SS_OP_BRANCH_GREATER_THAN:
    case SS_OP_BRANCH_LESS_THAN_OR_EQUALS:
    case SS_OP_BRANCH_GREATER_THAN_OR_EQUALS:
    {
        int32_t v[2];
        int taken;

        if( !pop_ints(state, v, 2) )
            return 0;

        switch( opcode )
        {
        case SS_OP_BRANCH_NOT:
            taken = v[0] != v[1];
            break;
        case SS_OP_BRANCH_EQUALS:
            taken = v[0] == v[1];
            break;
        case SS_OP_BRANCH_LESS_THAN:
            taken = v[0] < v[1];
            break;
        case SS_OP_BRANCH_GREATER_THAN:
            taken = v[0] > v[1];
            break;
        case SS_OP_BRANCH_LESS_THAN_OR_EQUALS:
            taken = v[0] <= v[1];
            break;
        default:
            taken = v[0] >= v[1];
            break;
        }
        if( taken )
            state->pc += operand;
        return 1;
    }

    case SS_OP_SWITCH:
    {
        int32_t key;
        const struct SSVM_SwitchTable* table;
        int i;

        if( !SSVM_PopInt(state, &key) )
            return 0;
        if( operand < 0 || operand >= state->script->switch_table_count )
            return 1; /* the reference returns quietly on a missing table */

        table = &state->script->switch_tables[operand];
        for( i = 0; i < table->case_count; i++ )
        {
            if( table->cases[i].key != key )
                continue;
            /* Delta 0 means "no such case" in the reference (`if (result)`), so
             * a real table never contains one — test-ss-corpus checks that
             * across the whole corpus. Matching the behaviour keeps a
             * hand-built or miscompiled table falling through rather than
             * looping on itself. */
            if( table->cases[i].delta )
                state->pc += table->cases[i].delta;
            return 1;
        }
        return 1;
    }

    case SS_OP_JOIN_STRING:
    {
        const char* parts[SSVM_STR_STACK_MAX];
        size_t total = 0;
        char* joined;
        int i;

        if( operand < 0 || operand > SSVM_STR_STACK_MAX )
        {
            SSVM_Abort(state, "join_string count %d out of range", operand);
            return 0;
        }
        if( !pop_strs(state, parts, operand) )
            return 0;

        for( i = 0; i < operand; i++ )
            total += strlen(parts[i]);

        joined = SSVM_StrPoolAlloc(&state->pool, total);
        if( !joined )
        {
            SSVM_Abort(state, "string pool exhausted joining %d strings", operand);
            return 0;
        }
        total = 0;
        for( i = 0; i < operand; i++ )
        {
            size_t length = strlen(parts[i]);

            memcpy(joined + total, parts[i], length);
            total += length;
        }
        joined[total] = '\0';
        return push_str_borrowed(state, joined);
    }

    case SS_OP_RETURN:
        if( state->fp == 0 )
        {
            state->execution = SSVM_FINISHED;
            return 1;
        }
        pop_frame(state);
        return 1;

    case SS_OP_GOSUB:
    case SS_OP_GOSUB_WITH_PARAMS:
    case SS_OP_JUMP:
    case SS_OP_JUMP_WITH_PARAMS:
    {
        int32_t script_id = operand;
        const struct SSVM_Script* target;
        int is_jump = (opcode == SS_OP_JUMP || opcode == SS_OP_JUMP_WITH_PARAMS);

        /* The bare forms are commands that take the script id on the stack;
         * the _WITH_PARAMS forms carry it as the operand. */
        if( opcode == SS_OP_GOSUB || opcode == SS_OP_JUMP )
        {
            if( !SSVM_PopInt(state, &script_id) )
                return 0;
        }

        /* Resolved inside the VM rather than through the host: the provider is
         * engine-agnostic, so routing a pure bytecode operation through the
         * game would buy nothing. */
        target = SSVM_ProviderGet(state->env->provider, script_id);
        if( !target )
        {
            SSVM_Abort(state, "%s: no script with id %d",
                       is_jump ? "jump" : "gosub", script_id);
            return 0;
        }
        return is_jump ? goto_frame(state, target) : gosub_frame(state, target);
    }

    case SS_OP_DEFINE_ARRAY:
    case SS_OP_PUSH_ARRAY_INT:
    case SS_OP_POP_ARRAY_INT:
        /* The reference throws 'unimplemented' on all three and the corpus
         * never emits them, so there is no behaviour to be compatible with. */
        SSVM_Abort(state, "%s is not implemented (the reference throws too)",
                   SSVM_OpcodeName(opcode));
        return 0;

    default:
        return -1; /* not a core op */
    }
}

/* ------------------------------------------------------------------ */
/* Arithmetic                                                          */
/* ------------------------------------------------------------------ */

static int
run_number_op(struct SSVM_State* state, int opcode)
{
    switch( opcode )
    {
    case SS_OP_ADD:
        BINARY_INT_OP(to_int32((double)v[0] + v[1]));
    case SS_OP_SUB:
        BINARY_INT_OP(to_int32((double)v[0] - v[1]));
    case SS_OP_MULTIPLY:
        BINARY_INT_OP(to_int32((double)v[0] * v[1]));
    case SS_OP_AND:
        BINARY_INT_OP(v[0] & v[1]);
    case SS_OP_OR:
        BINARY_INT_OP(v[0] | v[1]);
    case SS_OP_MIN:
        BINARY_INT_OP(v[0] < v[1] ? v[0] : v[1]);
    case SS_OP_MAX:
        BINARY_INT_OP(v[0] > v[1] ? v[0] : v[1]);
    case SS_OP_SETBIT:
        BINARY_INT_OP(v[0] | (int32_t)(1u << (v[1] & 31)));
    case SS_OP_CLEARBIT:
        BINARY_INT_OP(v[0] & ~(int32_t)(1u << (v[1] & 31)));
    case SS_OP_TESTBIT:
        BINARY_INT_OP((v[0] & (int32_t)(1u << (v[1] & 31))) ? 1 : 0);
    case SS_OP_TOGGLEBIT:
        BINARY_INT_OP(v[0] ^ (int32_t)(1u << (v[1] & 31)));

    case SS_OP_DIVIDE:
    case SS_OP_MODULO:
    {
        int32_t v[2];

        if( !pop_ints(state, v, 2) )
            return 0;
        if( v[1] == 0 )
        {
            /* The reference yields 0 here (JS Infinity/NaN through ToInt32).
             * Aborting instead: a divide by zero in content is a bug, and the
             * whole point of this engine is to surface those rather than
             * quietly hand back a plausible number. */
            SSVM_Abort(state, "%s by zero", SSVM_OpcodeName(opcode));
            return 0;
        }
        return SSVM_PushInt(state, opcode == SS_OP_DIVIDE ? v[0] / v[1] : v[0] % v[1]);
    }

    case SS_OP_SCALE:
    {
        int32_t v[3];

        if( !pop_ints(state, v, 3) )
            return 0;
        if( v[1] == 0 )
        {
            SSVM_Abort(state, "scale by zero");
            return 0;
        }
        return SSVM_PushInt(state, to_int32((double)v[0] * v[2] / v[1]));
    }

    case SS_OP_ADDPERCENT:
    {
        int32_t v[2];

        if( !pop_ints(state, v, 2) )
            return 0;
        /* ((num * percent) / 100 + num) | 0 — the division is floating point in
         * the reference and only the final sum is truncated, so doing the
         * divide in integers first would round differently. */
        return SSVM_PushInt(state, to_int32(((double)v[0] * v[1]) / 100.0 + v[0]));
    }

    case SS_OP_INTERPOLATE:
    {
        int32_t v[5];

        if( !pop_ints(state, v, 5) )
            return 0;
        if( v[3] == v[2] )
        {
            SSVM_Abort(state, "interpolate over a zero-width range");
            return 0;
        }
        /* Math.floor, not truncation — they differ for negative slopes. */
        return SSVM_PushInt(
            state, to_int32((double)floor_div(v[1] - v[0], v[3] - v[2]) * (v[4] - v[2]) + v[0]));
    }

    case SS_OP_POW:
    {
        int32_t v[2];
        int64_t result = 1;
        int i;

        if( !pop_ints(state, v, 2) )
            return 0;
        /* Math.pow with a negative exponent gives a fraction, which ToInt32
         * truncates to 0. Integer exponentiation reproduces every other case
         * exactly and keeps this file free of libm. */
        if( v[1] < 0 )
            return SSVM_PushInt(state, 0);
        for( i = 0; i < v[1]; i++ )
        {
            result *= v[0];
            if( result > 0xffffffffll || result < -0xffffffffll )
                result = (int32_t)result; /* wrap as ToInt32 would */
        }
        return SSVM_PushInt(state, (int32_t)result);
    }

    case SS_OP_INVPOW:
    {
        int32_t v[2];

        if( !pop_ints(state, v, 2) )
            return 0;
        if( v[0] == 0 || v[1] == 0 )
            return SSVM_PushInt(state, 0);
        switch( v[1] )
        {
        case 1:
            return SSVM_PushInt(state, v[0]);
        case 2:
            return SSVM_PushInt(state, int_sqrt(v[0]));
        case 3:
            return SSVM_PushInt(state, int_cbrt(v[0]));
        case 4:
            /* floor(sqrt(floor(sqrt(n)))) == floor(n^(1/4)) for n >= 0. */
            return SSVM_PushInt(state, int_sqrt(int_sqrt(v[0])));
        default:
            /* Higher roots need a real pow(); nothing in the corpus asks for
             * one, so let the stub say so rather than approximating. */
            return -1;
        }
    }

    case SS_OP_ABS:
    {
        int32_t value;

        if( !SSVM_PopInt(state, &value) )
            return 0;
        return SSVM_PushInt(state, value < 0 ? -value : value);
    }

    case SS_OP_BITCOUNT:
    {
        int32_t value;
        int count = 0;
        uint32_t bits;

        if( !SSVM_PopInt(state, &value) )
            return 0;
        for( bits = (uint32_t)value; bits; bits &= bits - 1 )
            count++;
        return SSVM_PushInt(state, count);
    }

    case SS_OP_SETBIT_RANGE:
    case SS_OP_CLEARBIT_RANGE:
    case SS_OP_GETBIT_RANGE:
    {
        int32_t v[3];
        uint32_t mask;

        if( !pop_ints(state, v, 3) )
            return 0;
        if( v[1] < 0 || v[2] > 31 || v[1] > v[2] )
        {
            SSVM_Abort(state, "bit range %d..%d is invalid", v[1], v[2]);
            return 0;
        }
        if( opcode == SS_OP_GETBIT_RANGE )
        {
            int shift = 31 - v[2];

            return SSVM_PushInt(state, (int32_t)(((uint32_t)v[0] << shift) >> (v[1] + shift)));
        }
        mask = (v[2] - v[1] == 31) ? 0xffffffffu : (((1u << (v[2] - v[1] + 1)) - 1u) << v[1]);
        return SSVM_PushInt(
            state, opcode == SS_OP_SETBIT_RANGE ? (int32_t)((uint32_t)v[0] | mask)
                                                : (int32_t)((uint32_t)v[0] & ~mask));
    }

    case SS_OP_SETBIT_RANGE_TOINT:
    {
        int32_t v[4];
        uint32_t mask;
        uint32_t maximum;
        uint32_t value;

        if( !pop_ints(state, v, 4) )
            return 0;
        if( v[2] < 0 || v[3] > 31 || v[2] > v[3] )
        {
            SSVM_Abort(state, "bit range %d..%d is invalid", v[2], v[3]);
            return 0;
        }
        maximum = (v[3] - v[2] == 31) ? 0xffffffffu : ((1u << (v[3] - v[2] + 1)) - 1u);
        mask = maximum << v[2];
        value = (uint32_t)v[1] > maximum ? maximum : (uint32_t)v[1];
        return SSVM_PushInt(state, (int32_t)(((uint32_t)v[0] & ~mask) | (value << v[2])));
    }

    case SS_OP_RANDOM:
    case SS_OP_RANDOMINC:
    {
        int32_t bound;

        if( !SSVM_PopInt(state, &bound) )
            return 0;
        if( opcode == SS_OP_RANDOMINC )
            bound += 1;
        return SSVM_PushInt(state, to_int32(java_next_double(state->env) * bound));
    }

    default:
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* Strings                                                             */
/* ------------------------------------------------------------------ */

static int
push_cat(struct SSVM_State* state, const char* left, const char* right)
{
    char* joined = SSVM_StrPoolCat(&state->pool, left, right);

    if( !joined )
    {
        SSVM_Abort(state, "string pool exhausted");
        return 0;
    }
    return push_str_borrowed(state, joined);
}

static int
run_string_op(struct SSVM_State* state, int opcode)
{
    switch( opcode )
    {
    case SS_OP_APPEND:
    {
        const char* parts[2];

        if( !pop_strs(state, parts, 2) )
            return 0;
        return push_cat(state, parts[0], parts[1]);
    }

    case SS_OP_APPEND_NUM:
    case SS_OP_APPEND_SIGNNUM:
    case SS_OP_APPEND_CHAR:
    {
        const char* text;
        int32_t value;
        char suffix[24];

        if( !SSVM_PopStr(state, &text) )
            return 0;
        if( !SSVM_PopInt(state, &value) )
            return 0;

        if( opcode == SS_OP_APPEND_CHAR )
            snprintf(suffix, sizeof(suffix), "%c", (char)value);
        else if( opcode == SS_OP_APPEND_SIGNNUM && value >= 0 )
            snprintf(suffix, sizeof(suffix), "+%d", value);
        else
            snprintf(suffix, sizeof(suffix), "%d", value);
        return push_cat(state, text, suffix);
    }

    case SS_OP_TOSTRING:
    {
        int32_t value;

        if( !SSVM_PopInt(state, &value) )
            return 0;
        return SSVM_PushStrFmt(state, "%d", value);
    }

    case SS_OP_LOWERCASE:
    {
        const char* text;
        char* lowered;
        size_t i;
        size_t length;

        if( !SSVM_PopStr(state, &text) )
            return 0;
        length = strlen(text);
        lowered = SSVM_StrPoolDupLen(&state->pool, text, length);
        if( !lowered )
        {
            SSVM_Abort(state, "string pool exhausted");
            return 0;
        }
        for( i = 0; i < length; i++ )
        {
            if( lowered[i] >= 'A' && lowered[i] <= 'Z' )
                lowered[i] = (char)(lowered[i] - 'A' + 'a');
        }
        return push_str_borrowed(state, lowered);
    }

    case SS_OP_STRING_LENGTH:
    {
        const char* text;

        if( !SSVM_PopStr(state, &text) )
            return 0;
        return SSVM_PushInt(state, (int32_t)strlen(text));
    }

    case SS_OP_COMPARE:
    {
        const char* parts[2];
        size_t i;
        size_t left_length;
        size_t right_length;
        size_t limit;

        if( !pop_strs(state, parts, 2) )
            return 0;
        /* java.lang.String.compareTo: first differing char, else the length
         * difference. Not strcmp, which is only guaranteed to agree on sign. */
        left_length = strlen(parts[0]);
        right_length = strlen(parts[1]);
        limit = left_length < right_length ? left_length : right_length;
        for( i = 0; i < limit; i++ )
        {
            if( parts[0][i] != parts[1][i] )
                return SSVM_PushInt(
                    state, (int32_t)((unsigned char)parts[0][i] - (unsigned char)parts[1][i]));
        }
        return SSVM_PushInt(state, (int32_t)left_length - (int32_t)right_length);
    }

    case SS_OP_TEXT_SWITCH:
    {
        const char* parts[2];
        int32_t value;

        if( !SSVM_PopInt(state, &value) )
            return 0;
        if( !pop_strs(state, parts, 2) )
            return 0;
        return push_str_borrowed(state, value == 1 ? parts[0] : parts[1]);
    }

    case SS_OP_SUBSTRING:
    {
        const char* text;
        int32_t v[2];
        int32_t start;
        int32_t end;
        int32_t length;

        if( !SSVM_PopStr(state, &text) )
            return 0;
        if( !pop_ints(state, v, 2) )
            return 0;

        /* JS String.substring clamps both ends into range and swaps them when
         * start > end. Reproduce that rather than trusting content to be
         * well-behaved. */
        length = (int32_t)strlen(text);
        start = v[0] < 0 ? 0 : (v[0] > length ? length : v[0]);
        end = v[1] < 0 ? 0 : (v[1] > length ? length : v[1]);
        if( start > end )
        {
            int32_t swap = start;

            start = end;
            end = swap;
        }
        {
            char* slice = SSVM_StrPoolDupLen(&state->pool, text + start, (size_t)(end - start));

            if( !slice )
            {
                SSVM_Abort(state, "string pool exhausted");
                return 0;
            }
            return push_str_borrowed(state, slice);
        }
    }

    case SS_OP_STRING_INDEXOF_CHAR:
    case SS_OP_STRING_INDEXOF_STRING:
    {
        const char* text;
        const char* found;
        char needle[2];

        if( !SSVM_PopStr(state, &text) )
            return 0;

        if( opcode == SS_OP_STRING_INDEXOF_CHAR )
        {
            int32_t value;

            if( !SSVM_PopInt(state, &value) )
                return 0;
            needle[0] = (char)value;
            needle[1] = '\0';
            found = strstr(text, needle);
        }
        else
        {
            const char* target;

            if( !SSVM_PopStr(state, &target) )
                return 0;
            found = strstr(text, target);
        }
        return SSVM_PushInt(state, found ? (int32_t)(found - text) : -1);
    }

    default:
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

static int
run_op(struct SSVM_State* state, int opcode, int32_t operand, const char* str_operand)
{
    const struct SSVM_OpcodeMeta* meta;
    int result;

    result = run_core_op(state, opcode, operand, str_operand);
    if( result >= 0 )
        return result;

    result = run_number_op(state, opcode);
    if( result >= 0 )
        return result;

    result = run_string_op(state, opcode);
    if( result >= 0 )
        return result;

    /* Pointer requirements come from the generated table, so the ~239 opcodes
     * that declare one are checked without a line of handwritten code. The
     * reference re-states these by hand at every call site. */
    meta = SSVM_OpcodeMeta(opcode);
    {
        uint32_t required = state->dot ? meta->require2 : meta->require;

        if( (state->pointers & required) != required )
        {
            SSVM_Abort(
                state, "%s requires an active entity the script does not have",
                SSVM_OpcodeName(opcode));
            return 0;
        }
    }

    if( state->env->host.command &&
        state->env->host.command(state, opcode, state->dot) )
        return state->execution == SSVM_ABORTED ? 0 : 1;

    return unimplemented_stub(state, opcode);
}

/* ------------------------------------------------------------------ */
/* Execute                                                             */
/* ------------------------------------------------------------------ */

/*
 * Instructions retired, read by the embedded server's tick breakdown
 * (mock230_world.c). A tick that runs long is either executing a great many
 * instructions or sitting inside one engine op, and nothing short of this
 * counter tells those two apart -- the wall clock alone looks identical.
 */
uint64_t g_ssvm_ops;

/*
 * TORIRS_SSVM_OPS=1: charge each instruction's wall time to its opcode, so the
 * breakdown can name the engine op a slow tick sat in.
 *
 * Instructions retired already separates "ran a lot of bytecode" from "sat in
 * one op", but it stops there -- 16 instructions and 16 ms says an op is slow
 * without saying which. Off by default and deliberately so: two clock reads
 * bracket work that is often a single array store, which is a large multiple of
 * a cheap op even though it is nothing next to the slow one being hunted.
 */
uint64_t g_ssvm_op_us[SSVM_OP_TIMING_MAX];
int g_ssvm_op_hits[SSVM_OP_TIMING_MAX];
static int g_ssvm_op_timing = -1;

static uint64_t
ssvm_now_us(void)
{
    struct timespec ts;

    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0;
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

enum SSVM_Exec
SSVM_Execute(struct SSVM_State* state)
{
    if( !state )
        return SSVM_ABORTED;

    /* Re-entering a settled state is a no-op rather than a restart: the engine
     * calls this from a tick phase that does not always know a parked script
     * already finished. */
    if( state->execution == SSVM_FINISHED || state->execution == SSVM_ABORTED )
        return state->execution;

    if( g_ssvm_op_timing < 0 )
    {
        char const* v = getenv("TORIRS_SSVM_OPS");

        g_ssvm_op_timing = (v && v[0] && v[0] != '0') ? 1 : 0;
    }

    state->execution = SSVM_RUNNING;

    while( state->execution == SSVM_RUNNING )
    {
        int opcode;
        int32_t operand;
        const char* str_operand;

        if( !state->script || state->pc < -1 || state->pc >= state->script->op_count )
        {
            SSVM_Abort(state, "program counter %d is out of range", state->pc);
            break;
        }

        /* Not reset when a suspended state resumes, matching the reference. A
         * script that suspends a thousand times still shares one budget, which
         * is what makes this an anti-runaway guarantee rather than a per-call
         * formality — and the intuitive implementation resets it. */
        if( state->opcount > SSVM_MAX_OPS )
        {
            SSVM_Abort(state, "exceeded %d instructions", SSVM_MAX_OPS);
            break;
        }
        state->opcount++;
        g_ssvm_ops++;

        state->pc++;
        if( state->pc >= state->script->op_count )
        {
            /* Every compiled script ends in RETURN, so falling off the end
             * means the bytecode is malformed, not that the script finished.
             * The reference errors here too. */
            SSVM_Abort(state, "ran past the last instruction without a return");
            break;
        }

        opcode = state->script->opcodes[state->pc];
        operand = state->script->int_operands[state->pc];
        str_operand = state->script->string_operands[state->pc];

        /* Commands carry a one-byte operand that is the secondary-pointer
         * flag. Latched once here rather than re-derived on every access, which
         * is what the reference does and what breaks as soon as a handler moves
         * pc. */
        state->dot = (opcode > 100) ? (operand != 0) : 0;

        if( g_ssvm_op_timing )
        {
            uint64_t op_t0 = ssvm_now_us();
            int op_ok = run_op(state, opcode, operand, str_operand);
            unsigned slot = (unsigned)opcode < SSVM_OP_TIMING_MAX ? (unsigned)opcode : 0;

            g_ssvm_op_us[slot] += ssvm_now_us() - op_t0;
            g_ssvm_op_hits[slot]++;
            if( !op_ok )
            {
                if( state->execution == SSVM_RUNNING )
                    SSVM_Abort(state, "%s failed", SSVM_OpcodeName(opcode));
                break;
            }
        }
        else if( !run_op(state, opcode, operand, str_operand) )
        {
            if( state->execution == SSVM_RUNNING )
                SSVM_Abort(state, "%s failed", SSVM_OpcodeName(opcode));
            break;
        }
    }

    return state->execution;
}

/* ------------------------------------------------------------------ */
/* State lifecycle                                                     */
/* ------------------------------------------------------------------ */

struct SSVM_State*
SSVM_StateAlloc(
    struct SSVM_Env* env,
    const struct SSVM_Script* script,
    const int32_t* int_args,
    int int_arg_count,
    const char* const* str_args,
    int str_arg_count)
{
    struct SSVM_State* state;
    int i;

    if( !env || !script )
        return NULL;

    if( int_arg_count != script->int_arg_count || str_arg_count != script->string_arg_count )
        return NULL;

    if( env->free_list )
    {
        state = env->free_list;
        env->free_list = state->pool_next;
    }
    else
    {
        state = (struct SSVM_State*)malloc(sizeof(*state));
        if( !state )
            return NULL;
        state->locals_arena = (char*)malloc(SSVM_LOCALS_ARENA_BYTES);
        if( !state->locals_arena )
        {
            free(state);
            return NULL;
        }
    }

    {
        char* arena = state->locals_arena;

        memset(state, 0, sizeof(*state));
        state->locals_arena = arena;
    }

    state->env = env;
    state->execution = SSVM_RUNNING;
    state->pc = -1;
    state->trigger = SSVM_ScriptTrigger(script);
    state->last_int = 0;
    SSVM_StrPoolInit(&state->pool);
    SSVM_ErrorClear(&state->err);

    /* Push the caller's arguments so setup_new_script binds them exactly the
     * way a gosub would — one code path for both entry routes. */
    for( i = 0; i < int_arg_count; i++ )
    {
        if( !SSVM_PushInt(state, int_args[i]) )
        {
            SSVM_StateRelease(state);
            return NULL;
        }
    }
    for( i = 0; i < str_arg_count; i++ )
    {
        if( !SSVM_PushStr(state, str_args[i]) )
        {
            SSVM_StateRelease(state);
            return NULL;
        }
    }

    if( !setup_new_script(state, script) )
    {
        SSVM_StateRelease(state);
        return NULL;
    }
    return state;
}

void
SSVM_StateRelease(struct SSVM_State* state)
{
    struct SSVM_Env* env;

    if( !state )
        return;

    env = state->env;
    SSVM_StrPoolFree(&state->pool);

    if( env )
    {
        state->pool_next = env->free_list;
        env->free_list = state;
    }
    else
    {
        free(state->locals_arena);
        free(state);
    }
}

/* ------------------------------------------------------------------ */
/* Environment                                                         */
/* ------------------------------------------------------------------ */

void
SSVM_EnvInit(
    struct SSVM_Env* env,
    struct SSVM_Provider* provider)
{
    if( !env )
        return;

    memset(env, 0, sizeof(*env));
    env->provider = provider;
    env->reported = (uint8_t*)calloc(SS_OPCODE_MAX, 1);
    SSVM_EnvSeed(env, 0);
}

void
SSVM_EnvSeed(
    struct SSVM_Env* env,
    uint64_t seed)
{
    env->rng = (seed ^ JAVA_RANDOM_MULTIPLIER) & JAVA_RANDOM_MASK;
}

void
SSVM_EnvBindHost(
    struct SSVM_Env* env,
    void* user,
    SSVM_HostCommand command)
{
    env->host.user = user;
    env->host.command = command;
}

void
SSVM_EnvFree(struct SSVM_Env* env)
{
    struct SSVM_State* state;

    if( !env )
        return;

    state = env->free_list;
    while( state )
    {
        struct SSVM_State* next = state->pool_next;

        free(state->locals_arena);
        free(state);
        state = next;
    }
    env->free_list = NULL;
    free(env->reported);
    env->reported = NULL;
}

int32_t
SSVM_Random(
    struct SSVM_Env* env,
    int32_t bound)
{
    if( bound <= 0 )
        return 0;
    return to_int32(java_next_double(env) * bound);
}
