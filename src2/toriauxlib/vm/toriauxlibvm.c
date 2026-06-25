#include "toriauxlibvm.h"

#include "ui/uitree.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/varp_varbit_manager.h"
#include "vm/csvm.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriAuxLibVM
{
    struct CSVM* csvm;
    struct VarPVarBitManager varp_varbit;
};

static int
tal_vm_get_varp(
    void* ud,
    int id)
{
    struct ToriAuxLibVM* vm = ud;
    return varp_varbit_get_varp(&vm->varp_varbit, id);
}

static int
tal_vm_get_varbit(
    void* ud,
    int id)
{
    struct ToriAuxLibVM* vm = ud;
    return varp_varbit_get_varbit(&vm->varp_varbit, id);
}

static void
tal_vm_state_fill(
    struct ToriAuxLibVM* vm,
    struct CSVM_State* out)
{
    if( !out )
        return;
    out->get_varp = tal_vm_get_varp;
    out->get_varbit = tal_vm_get_varbit;
    out->ud = vm;
}

struct ToriAuxLibVM*
ToriAuxLibVM_New(void)
{
    struct ToriAuxLibVM* vm = calloc(1, sizeof(struct ToriAuxLibVM));
    if( !vm )
        return NULL;
    vm->csvm = csvm_new();
    if( !vm->csvm )
    {
        free(vm);
        return NULL;
    }
    varp_varbit_init(&vm->varp_varbit);
    return vm;
}

void
ToriAuxLibVM_Free(struct ToriAuxLibVM* vm)
{
    if( !vm )
        return;
    varp_varbit_free(&vm->varp_varbit);
    csvm_free(vm->csvm);
    free(vm);
}

int
ToriAuxLibVM_EvalScript(
    struct ToriAuxLibVM* vm,
    struct StaticUIComponent const* c,
    int script_id)
{
    if( !vm || !c || script_id < 0 || script_id >= c->behavior.scripts_count )
        return -2;
    if( !c->behavior.scripts || !c->behavior.scripts[script_id] )
        return -1;
    struct CSVM_State state;
    tal_vm_state_fill(vm, &state);
    return csvm_eval(vm->csvm, c->behavior.scripts[script_id], &state);
}

bool
ToriAuxLibVM_IsActive(
    struct ToriAuxLibVM* vm,
    struct StaticUIComponent const* c)
{
    if( !vm || !c || !c->behavior.script_comparator || !c->behavior.script_operand )
        return false;

    int count = c->behavior.scripts_count;
    if( count <= 0 )
        return false;

    struct CSVM_State state;
    tal_vm_state_fill(vm, &state);
    for( int i = 0; i < count; i++ )
    {
        if( !c->behavior.scripts || !c->behavior.scripts[i] )
            return false;

        int value = csvm_eval(vm->csvm, c->behavior.scripts[i], &state);
        int operand = c->behavior.script_operand[i];
        int comp = c->behavior.script_comparator[i];
        if( !csvm_compare(comp, value, operand) )
            return false;
    }
    return true;
}

int
ToriAuxLibVM_GetVarp(
    struct ToriAuxLibVM* vm,
    int id)
{
    if( !vm )
        return 0;
    return varp_varbit_get_varp(&vm->varp_varbit, id);
}

int
ToriAuxLibVM_GetVarbit(
    struct ToriAuxLibVM* vm,
    int id)
{
    if( !vm )
        return 0;
    return varp_varbit_get_varbit(&vm->varp_varbit, id);
}

void
ToriAuxLibVM_SetVarpOptimistic(
    struct ToriAuxLibVM* vm,
    int id,
    int value)
{
    if( !vm )
        return;
    varp_varbit_set_varp_optimistic(&vm->varp_varbit, id, value);
}

bool
ToriAuxLibVM_LoadConfig(
    struct ToriAuxLibVM* vm,
    struct RSCacheShared_FileListDat* config_jagfile)
{
    if( !vm )
        return false;
    return varp_varbit_load_from_config_jagfile(&vm->varp_varbit, config_jagfile);
}

void
ToriAuxLibVM_ApplyButtonClickOptimistic(
    struct ToriAuxLibVM* vm,
    struct StaticUIComponent const* c)
{
    assert(!(!vm || !c));

    struct StaticUIBehavior const* b = &c->behavior;
    if( !b->scripts || b->scripts_count < 1 || !b->scripts[0] )
        return;

    int* script = b->scripts[0];
    int opcode = script[0];
    struct VarPVarBitManager* mgr = &vm->varp_varbit;

    if( opcode == 5 )
    {
        int varp = script[1];
        int current = varp_varbit_get_varp(mgr, varp);

        if( b->button_type == COMPONENT_BUTTON_TYPE_TOGGLE )
            varp_varbit_set_varp_optimistic(mgr, varp, 1 - current);
        else if( b->button_type == COMPONENT_BUTTON_TYPE_SELECT && b->script_operand )
        {
            int operand = b->script_operand[0];
            if( current != operand )
                varp_varbit_set_varp_optimistic(mgr, varp, operand);
        }
    }
    else if( opcode == 14 && b->button_type == COMPONENT_BUTTON_TYPE_TOGGLE )
    {
        int varbit_id = script[1];
        int current = varp_varbit_get_varbit(mgr, varbit_id);
        int new_val = 1 - current;

        if( varbit_id < 0 || varbit_id >= mgr->varbit_count )
            return;
        struct VarBitType const* vb = &mgr->varbit_types[varbit_id];
        if( vb->basevar < 0 || vb->basevar >= mgr->varp_count )
            return;

        int bit_count = vb->endbit - vb->startbit;
        if( bit_count <= 0 || bit_count >= VARP_VARBIT_READBIT_MAX )
            return;

        int mask = mgr->readbit[bit_count];
        int base_val = varp_varbit_get_varp(mgr, vb->basevar);
        int cleared = base_val & ~(mask << vb->startbit);
        int updated = cleared | ((new_val & mask) << vb->startbit);
        varp_varbit_set_varp_optimistic(mgr, vb->basevar, updated);
    }
    else if( opcode == 14 && b->button_type == COMPONENT_BUTTON_TYPE_SELECT && b->script_operand )
    {
        int varbit_id = script[1];
        int operand = b->script_operand[0];
        int current = varp_varbit_get_varbit(mgr, varbit_id);
        if( current == operand )
            return;

        if( varbit_id < 0 || varbit_id >= mgr->varbit_count )
            return;
        struct VarBitType const* vb = &mgr->varbit_types[varbit_id];
        if( vb->basevar < 0 || vb->basevar >= mgr->varp_count )
            return;

        int bit_count = vb->endbit - vb->startbit;
        if( bit_count <= 0 || bit_count >= VARP_VARBIT_READBIT_MAX )
            return;

        int mask = mgr->readbit[bit_count];
        int base_val = varp_varbit_get_varp(mgr, vb->basevar);
        int cleared = base_val & ~(mask << vb->startbit);
        int updated = cleared | ((operand & mask) << vb->startbit);
        varp_varbit_set_varp_optimistic(mgr, vb->basevar, updated);
    }
}

struct CSVM*
ToriAuxLibVM_CSVM(struct ToriAuxLibVM* vm)
{
    return vm ? vm->csvm : NULL;
}

struct VarPVarBitManager*
ToriAuxLibVM_VarPVarBit(struct ToriAuxLibVM* vm)
{
    return vm ? &vm->varp_varbit : NULL;
}
