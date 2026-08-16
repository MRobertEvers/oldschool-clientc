#include "cs1vm.h"
#include "cs1vm_opcode.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct CS1VM
{
    int int_stack[256];
    int int_stack_ptr;
};

struct CS1VM*
cs1vm_new(void)
{
    struct CS1VM* vm = calloc(1, sizeof(struct CS1VM));
    return vm;
}

void
cs1vm_free(struct CS1VM* vm)
{
    free(vm);
}

static int
cs1vm_call_varp(
    struct CS1Host const* host,
    int id)
{
    if( host && host->get_varp )
        return host->get_varp(host->ud, id);
    return 0;
}

static int
cs1vm_call_varbit(
    struct CS1Host const* host,
    int id)
{
    if( host && host->get_varbit )
        return host->get_varbit(host->ud, id);
    return 0;
}

static int
cs1vm_call_stat_level(
    struct CS1Host const* host,
    int skill)
{
    if( host && host->get_stat_level )
        return host->get_stat_level(host->ud, skill);
    return 0;
}

static int
cs1vm_call_stat_base_level(
    struct CS1Host const* host,
    int skill)
{
    if( host && host->get_stat_base_level )
        return host->get_stat_base_level(host->ud, skill);
    return 0;
}

static int
cs1vm_call_stat_xp(
    struct CS1Host const* host,
    int skill)
{
    if( host && host->get_stat_xp )
        return host->get_stat_xp(host->ud, skill);
    return 0;
}

static int
cs1vm_call_inv_count(
    struct CS1Host const* host,
    int iface_id,
    int obj_id)
{
    if( host && host->get_inv_count )
        return host->get_inv_count(host->ud, iface_id, obj_id);
    return 0;
}

static int
cs1vm_call_inv_contains(
    struct CS1Host const* host,
    int iface_id,
    int obj_id)
{
    if( host && host->inv_contains )
        return host->inv_contains(host->ud, iface_id, obj_id);
    return 0;
}

static int
cs1vm_call_stat_xp_remaining(
    struct CS1Host const* host,
    int skill)
{
    if( host && host->get_stat_xp_remaining )
        return host->get_stat_xp_remaining(host->ud, skill);
    return 0;
}

static int
cs1vm_call_combat_level(struct CS1Host const* host)
{
    if( host && host->get_combat_level )
        return host->get_combat_level(host->ud);
    return 0;
}

static int
cs1vm_call_total_level(struct CS1Host const* host)
{
    if( host && host->get_total_level )
        return host->get_total_level(host->ud);
    return 0;
}

static int
cs1vm_call_runenergy(struct CS1Host const* host)
{
    if( host && host->get_runenergy )
        return host->get_runenergy(host->ud);
    return 0;
}

static int
cs1vm_call_runweight(struct CS1Host const* host)
{
    if( host && host->get_runweight )
        return host->get_runweight(host->ud);
    return 0;
}

static int
cs1vm_call_coord_x(struct CS1Host const* host)
{
    if( host && host->get_coord_x )
        return host->get_coord_x(host->ud);
    return 0;
}

static int
cs1vm_call_coord_z(struct CS1Host const* host)
{
    if( host && host->get_coord_z )
        return host->get_coord_z(host->ud);
    return 0;
}

static void
cs1vm_skip_opcode_operands(int opcode, int const* script, int* pc)
{
    switch( opcode )
    {
    case CS1VM_OP_STAT_LEVEL:
    case CS1VM_OP_STAT_BASE_LEVEL:
    case CS1VM_OP_STAT_XP:
    case CS1VM_OP_PUSH_VARP:
    case CS1VM_OP_STAT_XP_REMAINING:
    case CS1VM_OP_VARP_SCALE:
    case CS1VM_OP_PUSH_VARBIT:
    case CS1VM_OP_PUSH_CONSTANT:
        (*pc) += 1;
        break;
    case CS1VM_OP_INV_COUNT:
    case CS1VM_OP_INV_CONTAINS:
    case CS1VM_OP_TEST_BIT:
        (*pc) += 2;
        break;
    case CS1VM_OP_COMBAT_LEVEL:
    case CS1VM_OP_TOTAL_LEVEL:
    case CS1VM_OP_RUNENERGY:
    case CS1VM_OP_RUNWEIGHT:
    case CS1VM_OP_COORD_X:
    case CS1VM_OP_COORD_Z:
    case CS1VM_OP_SUBTRACT:
    case CS1VM_OP_DIVIDE:
    case CS1VM_OP_MULTIPLY:
        break;
    default:
        break;
    }
    (void)script;
}

int
cs1vm_script_length(int const* script)
{
    assert(script);

    int pc = 0;
    while( 1 )
    {
        int opcode = script[pc++];
        if( opcode == CS1VM_OP_END )
            return pc;
        cs1vm_skip_opcode_operands(opcode, script, &pc);
    }
}

static int
cs1vm_read_operand(int const* script, int* pc, int script_len)
{
    if( script_len > 0 && *pc >= script_len )
        return 0;
    return script[*pc];
}

static void
cs1vm_advance_pc(int* pc, int script_len)
{
    if( script_len <= 0 || *pc < script_len )
        (*pc)++;
}

int
cs1vm_eval_len(
    struct CS1VM* vm,
    int const* script,
    struct CS1Host const* host,
    int script_len)
{
    (void)vm;
    assert(script);

    int acc = 0;
    int pc = 0;
    int arithmetic = 0;

    while( 1 )
    {
        if( script_len > 0 && pc >= script_len )
            return acc;

        int register_val = 0;
        int next_arithmetic = 0;
        int opcode = cs1vm_read_operand(script, &pc, script_len);
        cs1vm_advance_pc(&pc, script_len);

        if( opcode == CS1VM_OP_END )
            return acc;

        switch( opcode )
        {
        case CS1VM_OP_STAT_LEVEL:
            register_val = cs1vm_call_stat_level(host, cs1vm_read_operand(script, &pc, script_len));
            cs1vm_advance_pc(&pc, script_len);
            break;
        case CS1VM_OP_STAT_BASE_LEVEL:
            register_val =
                cs1vm_call_stat_base_level(host, cs1vm_read_operand(script, &pc, script_len));
            cs1vm_advance_pc(&pc, script_len);
            break;
        case CS1VM_OP_STAT_XP:
            register_val = cs1vm_call_stat_xp(host, cs1vm_read_operand(script, &pc, script_len));
            cs1vm_advance_pc(&pc, script_len);
            break;
        case CS1VM_OP_INV_COUNT:
        {
            int iface_id = cs1vm_read_operand(script, &pc, script_len);
            cs1vm_advance_pc(&pc, script_len);
            int obj_id = cs1vm_read_operand(script, &pc, script_len) + 1;
            cs1vm_advance_pc(&pc, script_len);
            register_val = cs1vm_call_inv_count(host, iface_id, obj_id);
            break;
        }
        case CS1VM_OP_PUSH_VARP:
            register_val = cs1vm_call_varp(host, cs1vm_read_operand(script, &pc, script_len));
            cs1vm_advance_pc(&pc, script_len);
            break;
        case CS1VM_OP_STAT_XP_REMAINING:
            register_val =
                cs1vm_call_stat_xp_remaining(host, cs1vm_read_operand(script, &pc, script_len));
            cs1vm_advance_pc(&pc, script_len);
            break;
        case CS1VM_OP_VARP_SCALE:
            register_val =
                (cs1vm_call_varp(host, cs1vm_read_operand(script, &pc, script_len)) * 100) / 46875;
            cs1vm_advance_pc(&pc, script_len);
            break;
        case CS1VM_OP_COMBAT_LEVEL:
            register_val = cs1vm_call_combat_level(host);
            break;
        case CS1VM_OP_TOTAL_LEVEL:
            register_val = cs1vm_call_total_level(host);
            break;
        case CS1VM_OP_INV_CONTAINS:
        {
            int iface_id = cs1vm_read_operand(script, &pc, script_len);
            cs1vm_advance_pc(&pc, script_len);
            int obj_id = cs1vm_read_operand(script, &pc, script_len) + 1;
            cs1vm_advance_pc(&pc, script_len);
            register_val = cs1vm_call_inv_contains(host, iface_id, obj_id);
            break;
        }
        case CS1VM_OP_RUNENERGY:
            register_val = cs1vm_call_runenergy(host);
            break;
        case CS1VM_OP_RUNWEIGHT:
            register_val = cs1vm_call_runweight(host);
            break;
        case CS1VM_OP_TEST_BIT:
        {
            int varp_val = cs1vm_call_varp(host, cs1vm_read_operand(script, &pc, script_len));
            cs1vm_advance_pc(&pc, script_len);
            int lsb = cs1vm_read_operand(script, &pc, script_len);
            cs1vm_advance_pc(&pc, script_len);
            register_val = (varp_val & (1 << lsb)) ? 1 : 0;
            break;
        }
        case CS1VM_OP_PUSH_VARBIT:
            register_val = cs1vm_call_varbit(host, cs1vm_read_operand(script, &pc, script_len));
            cs1vm_advance_pc(&pc, script_len);
            break;
        case CS1VM_OP_SUBTRACT:
            next_arithmetic = 1;
            break;
        case CS1VM_OP_DIVIDE:
            next_arithmetic = 2;
            break;
        case CS1VM_OP_MULTIPLY:
            next_arithmetic = 3;
            break;
        case CS1VM_OP_COORD_X:
            register_val = cs1vm_call_coord_x(host);
            break;
        case CS1VM_OP_COORD_Z:
            register_val = cs1vm_call_coord_z(host);
            break;
        case CS1VM_OP_PUSH_CONSTANT:
            register_val = cs1vm_read_operand(script, &pc, script_len);
            cs1vm_advance_pc(&pc, script_len);
            break;
        default:
            cs1vm_skip_opcode_operands(opcode, script, &pc);
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
cs1vm_eval(
    struct CS1VM* vm,
    int const* script,
    struct CS1Host const* host)
{
    return cs1vm_eval_len(vm, script, host, 0);
}

bool
cs1vm_compare(
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
