#include "cs2vm.h"

#include <stdlib.h>
#include <string.h>

struct CS2VM
{
    int stack[256];
    int stack_ptr;
};

struct CS2VM*
cs2vm_new(void)
{
    return calloc(1, sizeof(struct CS2VM));
}

void
cs2vm_free(struct CS2VM* vm)
{
    free(vm);
}

static int
cs2vm_pop(struct CS2VM* vm)
{
    if( !vm || vm->stack_ptr <= 0 )
        return 0;
    return vm->stack[--vm->stack_ptr];
}

static void
cs2vm_push(struct CS2VM* vm, int value)
{
    if( !vm || vm->stack_ptr >= 256 )
        return;
    vm->stack[vm->stack_ptr++] = value;
}

static int
cs2vm_get_varp(struct CS2VM_State const* state, int id)
{
    return state && state->get_varp ? state->get_varp(state->ud, id) : 0;
}

static int
cs2vm_get_varbit(struct CS2VM_State const* state, int id)
{
    return state && state->get_varbit ? state->get_varbit(state->ud, id) : 0;
}

static int
cs2vm_get_varc(struct CS2VM_State const* state, int id)
{
    return state && state->get_varc ? state->get_varc(state->ud, id) : 0;
}

int
cs2vm_eval(
    struct CS2VM* vm,
    int const* script,
    struct CS2VM_State const* state)
{
    if( !vm || !script )
        return 0;

    vm->stack_ptr = 0;
    int pc = 0;

    while( 1 )
    {
        int opcode = script[pc++];
        if( opcode == 0 )
            return vm->stack_ptr > 0 ? vm->stack[vm->stack_ptr - 1] : 0;

        switch( opcode )
        {
        case 1:
            cs2vm_push(vm, script[pc++]);
            break;
        case 2:
            cs2vm_push(vm, cs2vm_get_varp(state, script[pc++]));
            break;
        case 3:
            cs2vm_push(vm, cs2vm_get_varbit(state, script[pc++]));
            break;
        case 4:
            cs2vm_push(vm, cs2vm_get_varc(state, script[pc++]));
            break;
        case 10:
        {
            int b = cs2vm_pop(vm);
            int a = cs2vm_pop(vm);
            cs2vm_push(vm, a + b);
            break;
        }
        case 11:
        {
            int b = cs2vm_pop(vm);
            int a = cs2vm_pop(vm);
            cs2vm_push(vm, a - b);
            break;
        }
        case 12:
        {
            int b = cs2vm_pop(vm);
            int a = cs2vm_pop(vm);
            cs2vm_push(vm, a * b);
            break;
        }
        case 13:
        {
            int b = cs2vm_pop(vm);
            int a = cs2vm_pop(vm);
            cs2vm_push(vm, b != 0 ? a / b : 0);
            break;
        }
        case 20:
        {
            int b = cs2vm_pop(vm);
            int a = cs2vm_pop(vm);
            cs2vm_push(vm, a == b);
            break;
        }
        case 21:
        {
            int b = cs2vm_pop(vm);
            int a = cs2vm_pop(vm);
            cs2vm_push(vm, a != b);
            break;
        }
        case 22:
        {
            int b = cs2vm_pop(vm);
            int a = cs2vm_pop(vm);
            cs2vm_push(vm, a < b);
            break;
        }
        case 23:
        {
            int b = cs2vm_pop(vm);
            int a = cs2vm_pop(vm);
            cs2vm_push(vm, a > b);
            break;
        }
        case 30:
        {
            int cond = cs2vm_pop(vm);
            int target = script[pc++];
            if( !cond )
                pc = target;
            break;
        }
        case 31:
        {
            int target = script[pc++];
            pc = target;
            break;
        }
        case 40:
        {
            int value = cs2vm_pop(vm);
            int varp = script[pc++];
            if( state && state->set_varp_optimistic )
                state->set_varp_optimistic(state->ud, varp, value);
            break;
        }
        case 50:
            return cs2vm_pop(vm);
        default:
            return 0;
        }
    }
}

bool
cs2vm_compare(
    int comparator,
    int value,
    int operand)
{
    switch( comparator )
    {
    case 0:
        return value > operand;
    case 1:
        return value < operand;
    case 2:
        return value == operand;
    case 3:
        return value != operand;
    default:
        return false;
    }
}
