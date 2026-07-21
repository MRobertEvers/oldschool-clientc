/*
 * CS1VM — yield-capable ClientScript 1 (IF1) value-script interpreter.
 * Reference: Client-TS Client.getIfVar / Client.getIfActive.
 *
 * Architecture mirrors cs2vm2: the core is engine-agnostic, every state read
 * goes through CS1VM_HostExec_Fn, and a host that needs async work returns
 * CS1VM_EXECNO_YIELD after staging its request. The VM then rolls back to the
 * start of the yielding opcode. CS1 scripts are short and re-evaluated from
 * scratch each pass, so "resume" is simply re-entry after the driver has
 * serviced the load.
 */

#include "cs1vm.h"

#include <assert.h>
#include <string.h>

void
CS1VM_Init(struct CS1VM* vm)
{
    assert(vm);
    memset(vm, 0, sizeof(*vm));
    vm->yield_halt_script_index = -1;
    vm->yield_halt_pc = -1;
    vm->last_error_opcode = -1;
    vm->last_error_pc = -1;
}

void
CS1VM_BindHost(
    struct CS1VM* vm,
    void* user,
    CS1VM_HostExec_Fn host_exec)
{
    assert(vm);
    vm->user = user;
    vm->host_exec = host_exec;
}

static void
cs1vm_save_yield_checkpoint(
    struct CS1VM* vm,
    struct CS1VM_YieldCheckpoint* cp)
{
    assert(vm);
    assert(cp);

    cp->acc = vm->acc;
    cp->pending_arith = vm->pending_arith;
    cp->pc = vm->pc;
}

static void
cs1vm_restore_yield_checkpoint(
    struct CS1VM* vm,
    struct CS1VM_YieldCheckpoint const* cp)
{
    assert(vm);
    assert(cp);

    vm->acc = cp->acc;
    vm->pending_arith = cp->pending_arith;
    vm->pc = cp->pc;
}

static void
cs1vm_clear_yield_halt(struct CS1VM* vm)
{
    assert(vm);
    vm->yield_halt_script_index = -1;
    vm->yield_halt_pc = -1;
    vm->yield_halt_count = 0;
}

/**
 * One opcode, one yield: the host must load everything a request needs in that
 * single round-trip and complete on the retry (with a default if the load
 * failed). A second yield at the same site means a host handler broke that
 * contract. Returns false when the budget is exceeded.
 */
static bool
cs1vm_check_yield_halt(
    struct CS1VM* vm,
    int op_pc)
{
    assert(vm);

    if( vm->yield_halt_script_index == vm->script_index && vm->yield_halt_pc == op_pc )
        vm->yield_halt_count++;
    else
    {
        vm->yield_halt_script_index = vm->script_index;
        vm->yield_halt_pc = op_pc;
        vm->yield_halt_count = 1;
    }

    return vm->yield_halt_count <= 1;
}

/** Reads the next inline operand. Returns false when the stream is truncated. */
static bool
cs1vm_read_operand(
    struct CS1VM* vm,
    struct CS1VM_Script const* script,
    int* out)
{
    assert(vm);
    assert(script);
    assert(out);

    if( vm->pc < 0 || vm->pc >= script->op_count )
        return false;

    *out = script->ops[vm->pc];
    vm->pc += 1;
    return true;
}

/** Issues a host request; on CS1VM_EXECNO_OK the read value lands in *out. */
static int
cs1vm_host_read(
    struct CS1VM* vm,
    enum CS1VM_HostRequestKind kind,
    struct CS1VM_HostRequest* request,
    int* out)
{
    assert(vm);
    assert(request);
    assert(out);

    if( !vm->host_exec )
    {
        *out = 0;
        return CS1VM_EXECNO_OK;
    }

    request->kind = kind;
    request->out_value = 0;

    int rc = vm->host_exec(vm, request);
    if( rc == CS1VM_EXECNO_OK )
        *out = request->out_value;
    return rc;
}

/** Folds `value` into the accumulator using the pending arithmetic register. */
static void
cs1vm_fold(
    struct CS1VM* vm,
    int value)
{
    assert(vm);

    switch( vm->pending_arith )
    {
    case CS1VM_ARITH_SUB:
        vm->acc -= value;
        break;
    case CS1VM_ARITH_DIV:
        /* Reference skips the fold entirely rather than trapping. */
        if( value != 0 )
            vm->acc /= value;
        break;
    case CS1VM_ARITH_MUL:
        vm->acc *= value;
        break;
    default:
        vm->acc += value;
        break;
    }

    vm->pending_arith = CS1VM_ARITH_ADD;
}

/**
 * Executes one opcode. Returns CS1VM_EXECNO_OK (value produced or arithmetic
 * register set), CS1VM_EXECNO_DONE (script returned), CS1VM_EXECNO_YIELD, or
 * CS1VM_EXECNO_ERROR (truncated stream).
 *
 * On OK, *out_value carries the value to fold and *out_folds says whether this
 * opcode produced one at all — ops 15/16/17 set the arithmetic register for the
 * *next* value instead of contributing one.
 */
static int
cs1vm_run_op(
    struct CS1VM* vm,
    struct CS1VM_Script const* script,
    int opcode,
    int* out_value,
    bool* out_folds)
{
    struct CS1VM_HostRequest request;
    int operand = 0;
    int operand2 = 0;
    int rc;

    assert(vm);
    assert(script);
    assert(out_value);
    assert(out_folds);

    memset(&request, 0, sizeof(request));
    *out_value = 0;
    *out_folds = true;

    switch( opcode )
    {
    case CS1_OP_RETURN:
        return CS1VM_EXECNO_DONE;

    case CS1_OP_STAT_LEVEL:
    case CS1_OP_STAT_BASE_LEVEL:
    case CS1_OP_STAT_XP:
    case CS1_OP_STAT_XP_REMAINING:
    {
        if( !cs1vm_read_operand(vm, script, &operand) )
            return CS1VM_EXECNO_ERROR;
        request.u.stat.skill = operand;

        enum CS1VM_HostRequestKind kind = CS1VM_HOST_REQUEST_STAT_LEVEL;
        if( opcode == CS1_OP_STAT_BASE_LEVEL )
            kind = CS1VM_HOST_REQUEST_STAT_BASE_LEVEL;
        else if( opcode == CS1_OP_STAT_XP )
            kind = CS1VM_HOST_REQUEST_STAT_XP;
        else if( opcode == CS1_OP_STAT_XP_REMAINING )
            kind = CS1VM_HOST_REQUEST_STAT_XP_REMAINING;

        return cs1vm_host_read(vm, kind, &request, out_value);
    }

    case CS1_OP_INV_COUNT:
    case CS1_OP_INV_CONTAINS:
    {
        if( !cs1vm_read_operand(vm, script, &operand) )
            return CS1VM_EXECNO_ERROR;
        if( !cs1vm_read_operand(vm, script, &operand2) )
            return CS1VM_EXECNO_ERROR;

        request.u.inv.component_id = operand;
        request.u.inv.obj_id = operand2;

        return cs1vm_host_read(
            vm,
            opcode == CS1_OP_INV_COUNT ? CS1VM_HOST_REQUEST_INV_COUNT
                                       : CS1VM_HOST_REQUEST_INV_CONTAINS,
            &request,
            out_value);
    }

    case CS1_OP_PUSH_VARP:
    {
        if( !cs1vm_read_operand(vm, script, &operand) )
            return CS1VM_EXECNO_ERROR;
        request.u.varp.varp_id = operand;
        return cs1vm_host_read(vm, CS1VM_HOST_REQUEST_VARP, &request, out_value);
    }

    case CS1_OP_PUSH_VARP_SCALED:
    {
        if( !cs1vm_read_operand(vm, script, &operand) )
            return CS1VM_EXECNO_ERROR;
        request.u.varp.varp_id = operand;
        rc = cs1vm_host_read(vm, CS1VM_HOST_REQUEST_VARP, &request, out_value);
        if( rc == CS1VM_EXECNO_OK )
            *out_value = (*out_value) * 100 / 46875;
        return rc;
    }

    case CS1_OP_VARP_TESTBIT:
    {
        if( !cs1vm_read_operand(vm, script, &operand) )
            return CS1VM_EXECNO_ERROR;
        if( !cs1vm_read_operand(vm, script, &operand2) )
            return CS1VM_EXECNO_ERROR;
        request.u.varp.varp_id = operand;
        rc = cs1vm_host_read(vm, CS1VM_HOST_REQUEST_VARP, &request, out_value);
        if( rc == CS1VM_EXECNO_OK )
        {
            if( operand2 < 0 || operand2 >= 32 )
                *out_value = 0;
            else
                *out_value = ((*out_value) & (1 << operand2)) != 0 ? 1 : 0;
        }
        return rc;
    }

    case CS1_OP_PUSH_VARBIT:
    {
        if( !cs1vm_read_operand(vm, script, &operand) )
            return CS1VM_EXECNO_ERROR;
        request.u.varbit.varbit_id = operand;
        return cs1vm_host_read(vm, CS1VM_HOST_REQUEST_VARBIT, &request, out_value);
    }

    case CS1_OP_COMBAT_LEVEL:
        return cs1vm_host_read(vm, CS1VM_HOST_REQUEST_COMBAT_LEVEL, &request, out_value);
    case CS1_OP_TOTAL_LEVEL:
        return cs1vm_host_read(vm, CS1VM_HOST_REQUEST_TOTAL_LEVEL, &request, out_value);
    case CS1_OP_RUNENERGY:
        return cs1vm_host_read(vm, CS1VM_HOST_REQUEST_RUNENERGY, &request, out_value);
    case CS1_OP_RUNWEIGHT:
        return cs1vm_host_read(vm, CS1VM_HOST_REQUEST_RUNWEIGHT, &request, out_value);
    case CS1_OP_COORD_X:
        return cs1vm_host_read(vm, CS1VM_HOST_REQUEST_COORD_X, &request, out_value);
    case CS1_OP_COORD_Z:
        return cs1vm_host_read(vm, CS1VM_HOST_REQUEST_COORD_Z, &request, out_value);

    case CS1_OP_ARITH_SUB:
        vm->pending_arith = CS1VM_ARITH_SUB;
        *out_folds = false;
        return CS1VM_EXECNO_OK;
    case CS1_OP_ARITH_DIV:
        vm->pending_arith = CS1VM_ARITH_DIV;
        *out_folds = false;
        return CS1VM_EXECNO_OK;
    case CS1_OP_ARITH_MUL:
        vm->pending_arith = CS1VM_ARITH_MUL;
        *out_folds = false;
        return CS1VM_EXECNO_OK;

    case CS1_OP_PUSH_CONSTANT:
        if( !cs1vm_read_operand(vm, script, out_value) )
            return CS1VM_EXECNO_ERROR;
        return CS1VM_EXECNO_OK;

    default:
        /* Reference falls through its opcode chain with value 0 rather than
         * treating an unknown opcode as fatal. */
        return CS1VM_EXECNO_OK;
    }
}

/** Runs one value script from vm->pc to its CS1_OP_RETURN. */
static int
cs1vm_run_script(
    struct CS1VM* vm,
    struct CS1VM_Script const* script,
    int* out_value)
{
    assert(vm);
    assert(script);
    assert(out_value);

    int cycles = 0;
    while( cycles++ < CS1VM_MAX_CYCLES )
    {
        if( vm->pc < 0 || vm->pc >= script->op_count )
        {
            /* Stream ended without an explicit return: yield what we have. */
            *out_value = vm->acc;
            return CS1VM_EXECNO_DONE;
        }

        int opcode = script->ops[vm->pc];
        int op_pc = vm->pc;

        struct CS1VM_YieldCheckpoint yield_cp;
        cs1vm_save_yield_checkpoint(vm, &yield_cp);

        vm->pc += 1;

        int value = 0;
        bool folds = true;
        int result = cs1vm_run_op(vm, script, opcode, &value, &folds);

        switch( result )
        {
        case CS1VM_EXECNO_OK:
            if( vm->yield_halt_script_index == vm->script_index && vm->yield_halt_pc == op_pc )
                cs1vm_clear_yield_halt(vm);
            if( folds )
                cs1vm_fold(vm, value);
            break;

        case CS1VM_EXECNO_DONE:
            *out_value = vm->acc;
            return CS1VM_EXECNO_DONE;

        case CS1VM_EXECNO_YIELD:
            if( !cs1vm_check_yield_halt(vm, op_pc) )
            {
                vm->last_error_opcode = opcode;
                vm->last_error_pc = op_pc;
                return CS1VM_EXECNO_ERROR;
            }
            cs1vm_restore_yield_checkpoint(vm, &yield_cp);
            return CS1VM_EXECNO_YIELD;

        default:
            vm->last_error_opcode = opcode;
            vm->last_error_pc = op_pc;
            return CS1VM_EXECNO_ERROR;
        }
    }

    vm->last_error_opcode = -1;
    vm->last_error_pc = vm->pc;
    return CS1VM_EXECNO_ERROR;
}

enum CS1VM_EvalStatus
CS1VM_EvalValue(
    struct CS1VM* vm,
    struct CS1VM_ComponentScripts const* set,
    int script_index,
    int* out_value)
{
    struct CS1VM_Script script;

    assert(vm);
    assert(set);
    assert(out_value);

    if( script_index < 0 || script_index >= set->scripts_count )
    {
        *out_value = CS1VM_VALUE_ERR_NO_SCRIPT;
        return CS1VM_EVAL_DONE;
    }

    if( CS1VM_ComponentScriptGet(set, script_index, &script) != 0 )
    {
        *out_value = CS1VM_VALUE_ERR_EXCEPTION;
        return CS1VM_EVAL_DONE;
    }

    vm->acc = 0;
    vm->pending_arith = CS1VM_ARITH_ADD;
    vm->pc = 0;
    vm->script_index = script_index;

    int rc = cs1vm_run_script(vm, &script, out_value);
    switch( rc )
    {
    case CS1VM_EXECNO_DONE:
        return CS1VM_EVAL_DONE;
    case CS1VM_EXECNO_YIELD:
        return CS1VM_EVAL_YIELDED;
    default:
        break;
    }

    /* A malformed stream is a script-level failure, not a VM contract breach:
     * the reference catches it and evaluates to -1. CS1VM_EVAL_ERROR is
     * reserved for a host that yields twice at the same site. */
    if( vm->yield_halt_count > 1 )
    {
        *out_value = CS1VM_VALUE_ERR_EXCEPTION;
        return CS1VM_EVAL_ERROR;
    }

    *out_value = CS1VM_VALUE_ERR_EXCEPTION;
    return CS1VM_EVAL_DONE;
}

enum CS1VM_EvalStatus
CS1VM_EvalActive(
    struct CS1VM* vm,
    struct CS1VM_ComponentScripts const* set,
    bool* out_active)
{
    assert(vm);
    assert(set);
    assert(out_active);

    *out_active = false;

    if( !set->comparator || !set->operand || set->comparator_count <= 0 )
        return CS1VM_EVAL_DONE;

    /* Iterate the comparator array, not the script array: the two are sized
     * independently in the cache, and a comparator with no matching script
     * evaluates to the CS1VM_VALUE_ERR_NO_SCRIPT sentinel and is compared like
     * any other value (so it normally fails, leaving the component inactive). */
    for( int i = 0; i < set->comparator_count; i++ )
    {
        int value = 0;
        enum CS1VM_EvalStatus status = CS1VM_EvalValue(vm, set, i, &value);
        if( status != CS1VM_EVAL_DONE )
            return status;

        int target = set->operand[i];
        switch( set->comparator[i] )
        {
        case CS1VM_COMPARATOR_LESS:
            if( value >= target )
                return CS1VM_EVAL_DONE;
            break;
        case CS1VM_COMPARATOR_GREATER:
            if( value <= target )
                return CS1VM_EVAL_DONE;
            break;
        case CS1VM_COMPARATOR_NOT_EQUAL:
            if( value == target )
                return CS1VM_EVAL_DONE;
            break;
        default:
            if( value != target )
                return CS1VM_EVAL_DONE;
            break;
        }
    }

    *out_active = true;
    return CS1VM_EVAL_DONE;
}
