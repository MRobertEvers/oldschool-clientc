#include "clientscript_vm.h"

#include "osrs/game.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/varp_varbit_manager.h"

#include <stdlib.h>
#include <string.h>

struct ClientScriptVM*
clientscript_vm_new(void)
{
    struct ClientScriptVM* vm = malloc(sizeof(struct ClientScriptVM));
    if( !vm )
        return NULL;
    memset(vm, 0, sizeof(struct ClientScriptVM));
    return vm;
}

void
clientscript_vm_free(struct ClientScriptVM* vm)
{
    free(vm);
}

int
clientscript_vm_if_var(
    struct ClientScriptVM* vm,
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int script_id)
{
    (void)vm;
    if( !game || !component->scripts || script_id >= component->scripts_count )
        return -2;

    int* script = component->scripts[script_id];
    if( !script )
        return -1;

    int acc = 0;
    int pc = 0;
    int arithmetic = 0;

    struct VarPVarBitManager* mgr = &game->varp_varbit;

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
            /* stat_level {skill} */
        {
            int skill = script[pc++];
            (void)skill;
            break;
        }
        case 2:
            /* stat_base_level {skill} */
        {
            int skill = script[pc++];
            (void)skill;
            break;
        }
        case 3:
            /* stat_xp {skill} */
        {
            int skill = script[pc++];
            (void)skill;
            break;
        }
        case 5:
            /* pushvar {id} */
            register_val = varp_varbit_get_varp(mgr, script[pc++]);
            break;
        case 6:
            /* stat_xp_remaining {skill} */
        {
            break;
        }
        case 7:
            /* register = (var[id] * 100) / 46875 */
            register_val = (varp_varbit_get_varp(mgr, script[pc++]) * 100) / 46875;
            break;
        case 13:
        {
            /* testbit {varp} {bit: 0..31} */
            int varp_val = varp_varbit_get_varp(mgr, script[pc++]);
            int lsb = script[pc++];
            register_val = (varp_val & (1 << lsb)) ? 1 : 0;
            break;
        }
        case 14:
        {
            /* push_varbit {varbit} */
            register_val = varp_varbit_get_varbit(mgr, script[pc++]);
            break;
        }
        case 8:
            /* combat_level */
            register_val = 0;
            break;
        case 9:
            /* total_level */
            register_val = 0;
            break;
        case 11:
            /* runenergy */
            break;
        case 12:
            /* runweight */
            register_val = 0;
            break;
        case 20:
            /* push_constant */
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
clientscript_vm_if_active(
    struct ClientScriptVM* vm,
    struct GGame* game,
    struct CacheDatConfigComponent* component)
{
    (void)vm;
    if( !game || !component->scriptComparator || !component->scriptOperand )
        return false;

    int count = component->scripts_count;
    if( count <= 0 )
        return false;

    for( int i = 0; i < count; i++ )
    {
        if( !component->scriptOperand )
            return false;

        int value = clientscript_vm_if_var(vm, game, component, i);
        int operand = component->scriptOperand[i];
        int comp = component->scriptComparator[i];

        if( comp == 2 )
        {
            if( value >= operand )
                return false;
        }
        else if( comp == 3 )
        {
            if( value <= operand )
                return false;
        }
        else if( comp == 4 )
        {
            if( value == operand )
                return false;
        }
        else if( value != operand )
        {
            return false;
        }
    }
    return true;
}
