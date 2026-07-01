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
csvm_script_length(int const* script)
{
    if( !script )
        return 0;

    int pc = 0;
    while( 1 )
    {
        int opcode = script[pc++];
        if( opcode == 0 )
            return pc;

        switch( opcode )
        {
        case 1:
        case 2:
        case 3:
        case 5:
        case 7:
        case 14:
        case 20:
            pc += 1;
            break;
        case 4:
        case 10:
            pc += 2;
            break;
        case 13:
            pc += 2;
            break;
        case 6:
        case 8:
        case 9:
        case 11:
        case 12:
        case 15:
        case 16:
        case 17:
            break;
        default:
            if( opcode == 1 || opcode == 2 || opcode == 3 || opcode == 6 )
                pc += 1;
            else if( opcode == 4 || opcode == 10 )
                pc += 2;
            break;
        }
    }
}

static int
csvm_read_operand(int const* script, int* pc, int script_len)
{
    if( script_len > 0 && *pc >= script_len )
        return 0;
    return script[*pc];
}

static void
csvm_advance_pc(int* pc, int script_len)
{
    if( script_len <= 0 || *pc < script_len )
        (*pc)++;
}

int
csvm_eval_len(
    struct CSVM* vm,
    int const* script,
    struct CSVM_State const* state,
    int script_len)
{
    (void)vm;
    if( !script )
        return -1;

    int acc = 0;
    int pc = 0;
    int arithmetic = 0;

    while( 1 )
    {
        if( script_len > 0 && pc >= script_len )
            return acc;

        int register_val = 0;
        int next_arithmetic = 0;
        int opcode = csvm_read_operand(script, &pc, script_len);
        csvm_advance_pc(&pc, script_len);

        if( opcode == 0 )
            return acc;

        switch( opcode )
        {
        case 1:
            register_val = csvm_call_stat_level(state, csvm_read_operand(script, &pc, script_len));
            csvm_advance_pc(&pc, script_len);
            break;
        case 2:
            register_val = csvm_call_stat_base_level(state, csvm_read_operand(script, &pc, script_len));
            csvm_advance_pc(&pc, script_len);
            break;
        case 3:
            register_val = csvm_call_stat_xp(state, csvm_read_operand(script, &pc, script_len));
            csvm_advance_pc(&pc, script_len);
            break;
        case 5:
            register_val = csvm_call_varp(state, csvm_read_operand(script, &pc, script_len));
            csvm_advance_pc(&pc, script_len);
            break;
        case 6:
            break;
        case 7:
            register_val =
                (csvm_call_varp(state, csvm_read_operand(script, &pc, script_len)) * 100) / 46875;
            csvm_advance_pc(&pc, script_len);
            break;
        case 13:
        {
            int varp_val = csvm_call_varp(state, csvm_read_operand(script, &pc, script_len));
            csvm_advance_pc(&pc, script_len);
            int lsb = csvm_read_operand(script, &pc, script_len);
            csvm_advance_pc(&pc, script_len);
            register_val = (varp_val & (1 << lsb)) ? 1 : 0;
            break;
        }
        case 14:
            register_val = csvm_call_varbit(state, csvm_read_operand(script, &pc, script_len));
            csvm_advance_pc(&pc, script_len);
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
            register_val = csvm_read_operand(script, &pc, script_len);
            csvm_advance_pc(&pc, script_len);
            break;
        default:
            if( opcode == 1 || opcode == 2 || opcode == 3 || opcode == 6 )
            {
                csvm_read_operand(script, &pc, script_len);
                csvm_advance_pc(&pc, script_len);
            }
            else if( opcode == 4 || opcode == 10 )
            {
                csvm_read_operand(script, &pc, script_len);
                csvm_advance_pc(&pc, script_len);
                csvm_read_operand(script, &pc, script_len);
                csvm_advance_pc(&pc, script_len);
            }
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

int
csvm_eval(
    struct CSVM* vm,
    int const* script,
    struct CSVM_State const* state)
{
    return csvm_eval_len(vm, script, state, 0);
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
