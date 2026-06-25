#include "csvm.h"

#include <stdlib.h>
#include <string.h>

struct CSVM
{
    int int_stack[256];
    int int_stack_ptr;
};

struct CSVM*
csvm_new(void)
{
    struct CSVM* vm = calloc(1, sizeof(struct CSVM));
    return vm;
}

void
csvm_free(struct CSVM* vm)
{
    free(vm);
}

static int
csvm_call_varp(
    struct CSVM_State const* state,
    int id)
{
    if( state && state->get_varp )
        return state->get_varp(state->ud, id);
    return 0;
}

static int
csvm_call_varbit(
    struct CSVM_State const* state,
    int id)
{
    if( state && state->get_varbit )
        return state->get_varbit(state->ud, id);
    return 0;
}

static int
csvm_call_stat_level(
    struct CSVM_State const* state,
    int skill)
{
    if( state && state->get_stat_level )
        return state->get_stat_level(state->ud, skill);
    return 0;
}

static int
csvm_call_stat_base_level(
    struct CSVM_State const* state,
    int skill)
{
    if( state && state->get_stat_base_level )
        return state->get_stat_base_level(state->ud, skill);
    return 0;
}

static int
csvm_call_stat_xp(
    struct CSVM_State const* state,
    int skill)
{
    if( state && state->get_stat_xp )
        return state->get_stat_xp(state->ud, skill);
    return 0;
}

int
csvm_eval(
    struct CSVM* vm,
    int const* script,
    struct CSVM_State const* state)
{
    (void)vm;
    if( !script )
        return -1;

    int acc = 0;
    int pc = 0;
    int arithmetic = 0;

    while( 1 )
    {
        int register_val = 0;
        int next_arithmetic = 0;
        int opcode = script[pc++];

        if( opcode == 0 )
            return acc;

        switch( opcode )
        {
        case 1:
            register_val = csvm_call_stat_level(state, script[pc++]);
            break;
        case 2:
            register_val = csvm_call_stat_base_level(state, script[pc++]);
            break;
        case 3:
            register_val = csvm_call_stat_xp(state, script[pc++]);
            break;
        case 5:
            register_val = csvm_call_varp(state, script[pc++]);
            break;
        case 6:
            break;
        case 7:
            register_val = (csvm_call_varp(state, script[pc++]) * 100) / 46875;
            break;
        case 13:
        {
            int varp_val = csvm_call_varp(state, script[pc++]);
            int lsb = script[pc++];
            register_val = (varp_val & (1 << lsb)) ? 1 : 0;
            break;
        }
        case 14:
            register_val = csvm_call_varbit(state, script[pc++]);
            break;
        case 8:
            register_val = 0;
            break;
        case 9:
            register_val = 0;
            break;
        case 11:
            break;
        case 12:
            register_val = 0;
            break;
        case 20:
            register_val = script[pc++];
            break;
        default:
            if( opcode == 1 || opcode == 2 || opcode == 3 || opcode == 6 )
                pc += 1;
            else if( opcode == 4 || opcode == 10 )
                pc += 2;
            else if( opcode == 15 || opcode == 16 || opcode == 17 )
                next_arithmetic = (opcode == 15) ? 1 : (opcode == 16) ? 2 : 3;
            break;
        }

        if( next_arithmetic == 0 )
        {
            if( arithmetic == 0 )
                acc += register_val;
            else if( arithmetic == 1 )
                acc -= register_val;
            else if( arithmetic == 2 && register_val != 0 )
                acc = acc / register_val;
            else if( arithmetic == 3 )
                acc = acc * register_val;
            arithmetic = 0;
        }
        else
        {
            arithmetic = next_arithmetic;
        }
    }
}

bool
csvm_compare(
    int comparator,
    int value,
    int operand)
{
    if( comparator == 2 )
        return value < operand;
    if( comparator == 3 )
        return value > operand;
    if( comparator == 4 )
        return value != operand;
    return value == operand;
}
