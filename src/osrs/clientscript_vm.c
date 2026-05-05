#include "clientscript_vm.h"

#include "osrs/chat.h"
#include "osrs/game.h"
#include "osrs/player_stats.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/config_varp.h"
#include "osrs/world.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

struct ClientScriptVM*
clientscript_vm_new(void)
{
    struct ClientScriptVM* vm = calloc(1, sizeof(struct ClientScriptVM));
    if( !vm )
        return NULL;
    for( int i = 0; i < VARP_VARBIT_READBIT_MAX; i++ )
        vm->readbit[i] = (1 << i) - 1;
    return vm;
}

void
clientscript_vm_free(struct ClientScriptVM* vm)
{
    if( !vm )
        return;
    free(vm->varp_types);
    free(vm->varbit_types);
    free(vm->var);
    free(vm->var_serv);
    free(vm);
}

bool
clientscript_vm_load_types(
    struct ClientScriptVM* vm,
    struct FileListDat*    config_jagfile)
{
    if( !vm || !config_jagfile )
        return false;

    /* -- Varps ----------------------------------------------------------- */
    int varp_idx = filelist_dat_find_file_by_name(config_jagfile, "varp.dat");
    if( varp_idx != -1 )
    {
        struct CacheDatConfigVarpList* list = cache_dat_config_varp_list_new_decode(
            config_jagfile->files[varp_idx],
            config_jagfile->file_sizes[varp_idx]);

        if( list )
        {
            int n = list->varps_count;
            vm->varp_types = malloc((size_t)n * sizeof(struct VarPType));
            vm->var        = calloc((size_t)n, sizeof(int));
            vm->var_serv   = calloc((size_t)n, sizeof(int));
            if( vm->varp_types && vm->var && vm->var_serv )
            {
                vm->varp_count = n;
                for( int i = 0; i < n; i++ )
                    vm->varp_types[i].clientcode =
                        (enum VarClientCodeKind)list->varps[i]->clientcode;
            }
            else
            {
                free(vm->varp_types); vm->varp_types = NULL;
                free(vm->var);        vm->var        = NULL;
                free(vm->var_serv);   vm->var_serv   = NULL;
            }
            cache_dat_config_varp_list_free(list);
        }
    }

    /* -- Varbits --------------------------------------------------------- */
    int varbit_idx = filelist_dat_find_file_by_name(config_jagfile, "varbit.dat");
    if( varbit_idx != -1 )
    {
        void* data = config_jagfile->files[varbit_idx];
        int   size = config_jagfile->file_sizes[varbit_idx];
        if( data && size > 2 )
        {
            struct RSBuffer buf;
            rsbuf_init(&buf, (int8_t*)data, size);
            int n = g2(&buf);
            vm->varbit_types = calloc((size_t)n, sizeof(struct VarBitType));
            if( vm->varbit_types )
            {
                vm->varbit_count = n;
                for( int i = 0; i < n && (int)buf.position < buf.size; i++ )
                {
                    vm->varbit_types[i].basevar = -1;
                    while( (int)buf.position < buf.size )
                    {
                        int code = g1(&buf);
                        if( code == 0 )
                            break;
                        if( code == 1 )
                        {
                            /* VarBitType.ts decode: basevar=g2, startbit=g1, endbit=g1 */
                            vm->varbit_types[i].basevar  = g2(&buf);
                            vm->varbit_types[i].startbit = g1(&buf);
                            vm->varbit_types[i].endbit   = g1(&buf);
                        }
                        else if( code == 10 )
                        {
                            /* debug name string — skip null-terminated */
                            while( (int)buf.position < buf.size && g1(&buf) != 0 );
                        }
                        /* else: skip unknown opcodes without consuming extra bytes */
                    }
                }
            }
        }
    }

    return true;
}

/* ── Dirty-queue helpers ───────────────────────────────────────────────── */

static void
vm_enqueue_dirty(struct ClientScriptVM* vm, int id)
{
    if( vm->dirty_varp_count < CLIENT_VM_DIRTY_VARP_QUEUE )
        vm->dirty_varps[vm->dirty_varp_count++] = id;
}

static void
vm_apply_value(struct ClientScriptVM* vm, int variable, int value)
{
    if( variable < 0 || variable >= vm->varp_count )
        return;
    vm->var_serv[variable] = value;
    if( vm->var[variable] != value )
    {
        vm->var[variable] = value;
        vm_enqueue_dirty(vm, variable);
    }
}

/* ── Var / varbit read ─────────────────────────────────────────────────── */

int
clientscript_vm_get_var(const struct ClientScriptVM* vm, int id)
{
    if( !vm || id < 0 || id >= vm->varp_count )
        return 0;
    return vm->var[id];
}

int
clientscript_vm_get_varbit(const struct ClientScriptVM* vm, int id)
{
    if( !vm || id < 0 || id >= vm->varbit_count )
        return 0;
    const struct VarBitType* vb = &vm->varbit_types[id];
    if( vb->basevar < 0 || vb->basevar >= vm->varp_count )
        return 0;
    int bit_count = vb->endbit - vb->startbit;
    if( bit_count <= 0 || bit_count >= VARP_VARBIT_READBIT_MAX )
        return 0;
    return (vm->var[vb->basevar] >> vb->startbit) & vm->readbit[bit_count];
}

/* ── Network packet handlers ───────────────────────────────────────────── */

void
clientscript_vm_apply_varp_small(struct ClientScriptVM* vm, int variable, int value)
{
    if( vm )
        vm_apply_value(vm, variable, value);
}

void
clientscript_vm_apply_varp_large(struct ClientScriptVM* vm, int variable, int value)
{
    if( vm )
        vm_apply_value(vm, variable, value);
}

void
clientscript_vm_apply_varp_sync(struct ClientScriptVM* vm)
{
    if( !vm )
        return;
    for( int i = 0; i < vm->varp_count; i++ )
    {
        if( vm->var[i] != vm->var_serv[i] )
        {
            vm->var[i] = vm->var_serv[i];
            vm_enqueue_dirty(vm, i);
        }
    }
}

void
clientscript_vm_set_var_optimistic(struct ClientScriptVM* vm, int variable, int value)
{
    if( !vm || variable < 0 || variable >= vm->varp_count )
        return;
    if( vm->var[variable] != value )
    {
        vm->var[variable] = value;
        vm_enqueue_dirty(vm, variable);
    }
}

/* ── Frame drain ───────────────────────────────────────────────────────── */

void
clientscript_vm_drain_clientcodes(struct ClientScriptVM* vm, struct GGame* game)
{
    if( !vm || !game )
        return;

    for( int i = 0; i < vm->dirty_varp_count; i++ )
    {
        int id = vm->dirty_varps[i];
        if( id < 0 || id >= vm->varp_count )
            continue;
        int v = vm->var[id];
        switch( vm->varp_types[id].clientcode )
        {
        case VAR_CLIENTCODE_BRIGHTNESS:
            game->settings_brightness = v;
            break;
        case VAR_CLIENTCODE_MUSIC_VOLUME:
            game->settings_music_vol = v;
            break;
        case VAR_CLIENTCODE_WAVE_VOLUME:
            game->settings_wave_vol = v;
            break;
        case VAR_CLIENTCODE_ONE_MOUSE:
            game->settings_one_mouse = v;
            break;
        case VAR_CLIENTCODE_CHAT_EFFECTS:
            if( game->chat )
                game->chat->chat_effects = v;
            break;
        case VAR_CLIENTCODE_SPLIT_PRIVATE_CHAT:
            if( game->chat )
                game->chat->split_private = v;
            break;
        case VAR_CLIENTCODE_BANK_ARRANGE:
            game->settings_bank = v;
            break;
        case VAR_CLIENTCODE_NONE:
        default:
            break;
        }
    }
    vm->dirty_varp_count = 0;
}

/* ── Script evaluation ─────────────────────────────────────────────────── */

/** Fetch next word from bytecode; returns false at end. */
static bool
vm_fetch(const int* code, int len, int* pc, int* out)
{
    if( *pc >= len )
        return false;
    *out = code[(*pc)++];
    return true;
}

int
clientscript_vm_eval(
    struct ClientScriptVM*         vm,
    const struct UIScriptBytecode* script)
{
    if( !script || !script->code || script->len <= 0 )
        return -2;

    int acc        = 0;
    int pc         = 0;
    int arithmetic = 0;

    while( 1 )
    {
        int register_val   = 0;
        int next_arithmetic = 0;
        int opcode = 0;

        if( !vm_fetch(script->code, script->len, &pc, &opcode) )
            return acc;
        if( opcode == 0 )
            return acc;

        switch( opcode )
        {
        case 1:
        {
            /* stat_level {skill} — effective (boosted/drained) level */
            int skill = 0;
            if( !vm_fetch(script->code, script->len, &pc, &skill) )
                return acc;
            if( vm->game_ref && skill >= 0 && skill < PLAYER_STAT_COUNT )
                register_val = vm->game_ref->player_stat_level[skill];
            break;
        }
        case 2:
        {
            /* stat_base_level {skill} — base level (we track a single level, use it) */
            int skill = 0;
            if( !vm_fetch(script->code, script->len, &pc, &skill) )
                return acc;
            if( vm->game_ref && skill >= 0 && skill < PLAYER_STAT_COUNT )
                register_val = player_stats_xp_to_level(vm->game_ref->player_stat_xp[skill]);
            break;
        }
        case 3:
        {
            /* stat_xp {skill} */
            int skill = 0;
            if( !vm_fetch(script->code, script->len, &pc, &skill) )
                return acc;
            if( vm->game_ref && skill >= 0 && skill < PLAYER_STAT_COUNT )
                register_val = vm->game_ref->player_stat_xp[skill];
            break;
        }
        case 4:
        {
            /* inv_count {interface_comp_id} {obj_id_0based} */
            int com_id = 0, obj_id = 0;
            if( !vm_fetch(script->code, script->len, &pc, &com_id) ||
                !vm_fetch(script->code, script->len, &pc, &obj_id) )
                return acc;
            /* obj_id is 0-based in the script; UIInventoryItem.obj_id is stored 1-based */
            int target_obj_1based = obj_id + 1;
            if( vm->game_ref && vm->game_ref->inv_pool && vm->game_ref->ui_root_buffer )
            {
                int inv_idx = uitree_find_inv_index_by_component_id(
                    vm->game_ref->ui_root_buffer, com_id, NULL);
                if( inv_idx >= 0 && inv_idx < vm->game_ref->inv_pool->count )
                {
                    struct UIInventory* inv = &vm->game_ref->inv_pool->inventories[inv_idx];
                    for( int si = 0; si < inv->item_count; si++ )
                    {
                        if( inv->items[si].obj_id == target_obj_1based )
                            register_val += inv->items[si].obj_count > 0
                                            ? inv->items[si].obj_count : 1;
                    }
                }
            }
            break;
        }
        case 5:
        {
            /* pushvar {id} */
            int vid = 0;
            if( !vm_fetch(script->code, script->len, &pc, &vid) )
                return acc;
            register_val = clientscript_vm_get_var(vm, vid);
            break;
        }
        case 6:
        {
            /* stat_xp_remaining {skill} — XP needed to reach the next level */
            int skill = 0;
            if( !vm_fetch(script->code, script->len, &pc, &skill) )
                return acc;
            if( vm->game_ref && skill >= 0 && skill < PLAYER_STAT_COUNT )
            {
                int base_level = player_stats_xp_to_level(vm->game_ref->player_stat_xp[skill]);
                if( base_level >= 1 && base_level <= 98 )
                    register_val = g_player_level_experience[base_level - 1]
                                   - vm->game_ref->player_stat_xp[skill];
                else
                    register_val = 0;
            }
            break;
        }
        case 7:
        {
            /* (var[id] * 100) / 46875 */
            int vid = 0;
            if( !vm_fetch(script->code, script->len, &pc, &vid) )
                return acc;
            register_val = (clientscript_vm_get_var(vm, vid) * 100) / 46875;
            break;
        }
        case 8:
            /* combat_level — local player (not tracked separately yet) */
            break;
        case 9:
        {
            /* total_level — sum base levels for first 19 combat/skills */
            if( vm->game_ref )
            {
                for( int si = 0; si < 19 && si < PLAYER_STAT_COUNT; si++ )
                {
                    if( si == 18 )
                        continue; /* skip slot 18 (runecrafting at slot 20) */
                    register_val += player_stats_xp_to_level(vm->game_ref->player_stat_xp[si]);
                }
            }
            break;
        }
        case 10:
        {
            /* inv_contains {interface_comp_id} {obj_id_0based} */
            int com_id = 0, obj_id = 0;
            if( !vm_fetch(script->code, script->len, &pc, &com_id) ||
                !vm_fetch(script->code, script->len, &pc, &obj_id) )
                return acc;
            int target_obj_1based = obj_id + 1;
            if( vm->game_ref && vm->game_ref->inv_pool && vm->game_ref->ui_root_buffer )
            {
                int inv_idx = uitree_find_inv_index_by_component_id(
                    vm->game_ref->ui_root_buffer, com_id, NULL);
                if( inv_idx >= 0 && inv_idx < vm->game_ref->inv_pool->count )
                {
                    struct UIInventory* inv = &vm->game_ref->inv_pool->inventories[inv_idx];
                    for( int si = 0; si < inv->item_count; si++ )
                    {
                        if( inv->items[si].obj_id == target_obj_1based )
                        {
                            register_val = 999999999;
                            break;
                        }
                    }
                }
            }
            break;
        }
        case 11:
            /* runenergy */
            if( vm->game_ref )
                register_val = vm->game_ref->run_energy;
            break;
        case 12:
            /* runweight */
            if( vm->game_ref )
                register_val = vm->game_ref->run_weight;
            break;
        case 13:
        {
            /* testbit {varp} {bit} */
            int vi = 0, lsb = 0;
            if( !vm_fetch(script->code, script->len, &pc, &vi) ||
                !vm_fetch(script->code, script->len, &pc, &lsb) )
                return acc;
            register_val = (clientscript_vm_get_var(vm, vi) & (1 << lsb)) ? 1 : 0;
            break;
        }
        case 14:
        {
            /* push_varbit {varbit} */
            int vb = 0;
            if( !vm_fetch(script->code, script->len, &pc, &vb) )
                return acc;
            register_val = clientscript_vm_get_varbit(vm, vb);
            break;
        }
        case 15: next_arithmetic = 1; break; /* subtract */
        case 16: next_arithmetic = 2; break; /* divide   */
        case 17: next_arithmetic = 3; break; /* multiply */
        case 18:
        {
            /* coordx — local player world tile X */
            if( vm->game_ref && vm->game_ref->world )
            {
                struct PlayerEntity* pl = world_player(vm->game_ref->world, ACTIVE_PLAYER_SLOT);
                if( pl && pl->alive )
                    register_val = pl->draw_position.x >> 7;
            }
            break;
        }
        case 19:
        {
            /* coordz — local player world tile Z */
            if( vm->game_ref && vm->game_ref->world )
            {
                struct PlayerEntity* pl = world_player(vm->game_ref->world, ACTIVE_PLAYER_SLOT);
                if( pl && pl->alive )
                    register_val = pl->draw_position.z >> 7;
            }
            break;
        }
        case 20:
        {
            /* push_constant */
            if( !vm_fetch(script->code, script->len, &pc, &register_val) )
                return acc;
            break;
        }
        default:
            break;
        }

        if( next_arithmetic == 0 )
        {
            if( arithmetic == 0 )      acc += register_val;
            else if( arithmetic == 1 ) acc -= register_val;
            else if( arithmetic == 2 && register_val != 0 ) acc = acc / register_val;
            else if( arithmetic == 3 ) acc = acc * register_val;
            arithmetic = 0;
        }
        else
        {
            arithmetic = next_arithmetic;
        }
    }
}

bool
clientscript_vm_active(
    struct ClientScriptVM*         vm,
    const struct UIScriptBytecode* scripts,
    int                            scripts_count,
    const uint8_t*                 script_comparator,
    const int*                     script_operand)
{
    if( !scripts || scripts_count <= 0 || !script_comparator || !script_operand )
        return false;

    for( int i = 0; i < scripts_count; i++ )
    {
        int value   = clientscript_vm_eval(vm, &scripts[i]);
        int operand = script_operand[i];
        int comp    = (int)script_comparator[i];

        if( comp == 2 )      { if( value >= operand ) return false; }
        else if( comp == 3 ) { if( value <= operand ) return false; }
        else if( comp == 4 ) { if( value == operand ) return false; }
        else                 { if( value != operand ) return false; }
    }
    return true;
}

/* ── Legacy shims (CacheDatConfigComponent* overloads) ────────────────── */

int
clientscript_vm_if_var(
    struct ClientScriptVM*          vm,
    struct GGame*                   game,
    struct CacheDatConfigComponent* component,
    int                             script_id)
{
    (void)game;
    if( !component->scripts || script_id < 0 ||
        script_id >= component->scripts_count )
        return -2;
    if( !component->scripts[script_id] )
        return -1;
    if( !component->scripts_lengths )
        return -2;

    struct UIScriptBytecode bc;
    bc.code = component->scripts[script_id];
    bc.len  = component->scripts_lengths[script_id];
    if( bc.len <= 0 )
        return -2;
    return clientscript_vm_eval(vm, &bc);
}

bool
clientscript_vm_if_active(
    struct ClientScriptVM*          vm,
    struct GGame*                   game,
    struct CacheDatConfigComponent* component)
{
    (void)game;
    if( !component->scriptComparator || !component->scriptOperand ||
        component->scripts_count <= 0 )
        return false;

    for( int i = 0; i < component->scripts_count; i++ )
    {
        if( !component->scriptOperand )
            return false;

        struct UIScriptBytecode bc;
        bc.code = component->scripts ? component->scripts[i] : NULL;
        bc.len  = component->scripts_lengths ? component->scripts_lengths[i] : 0;

        int value   = clientscript_vm_eval(vm, &bc);
        int operand = component->scriptOperand[i];
        int comp    = (int)component->scriptComparator[i];

        if( comp == 2 )      { if( value >= operand ) return false; }
        else if( comp == 3 ) { if( value <= operand ) return false; }
        else if( comp == 4 ) { if( value == operand ) return false; }
        else                 { if( value != operand ) return false; }
    }
    return true;
}
