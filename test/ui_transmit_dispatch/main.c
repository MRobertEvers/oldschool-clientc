#include "vm/cs2vm.h"
#include "vm/cs2_opcode.h"
#include "vm/cs2_host_ui.h"
#include "vm/cs2_script.h"
#include "ui/uitree.h"
#include "ui/ui_behavior.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/vm/toriauxlibvm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ToriAuxLibCache;
struct ToriAuxLibCore_ClientScript;

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCache_ClientScriptResolve(
    struct ToriAuxLibCache* cache,
    int script_id)
{
    (void)cache;
    (void)script_id;
    return NULL;
}

#define TEST_ASSERT(cond, msg) \
    do \
    { \
        if( !(cond) ) \
        { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            return 1; \
        } \
    } while( 0 )

static int const k_component_id = 42;

static struct ToriAuxLibCore_ClientScript*
make_hide_script(void)
{
    struct ToriAuxLibCore_ClientScript* script = calloc(1, sizeof(*script));
    if( !script )
        return NULL;
    script->script.op_count = 5;
    script->script.opcodes = malloc(5 * sizeof(uint16_t));
    script->script.int_operands = malloc(5 * sizeof(int));
    if( !script->script.opcodes || !script->script.int_operands )
    {
        free(script->script.opcodes);
        free(script->script.int_operands);
        free(script);
        return NULL;
    }
    script->script.opcodes[0] = CS2_OP_PUSH_CONSTANT_INT;
    script->script.opcodes[1] = CS2_OP_PUSH_CONSTANT_INT;
    script->script.opcodes[2] = CS2_OP_IF_SETHIDE;
    script->script.opcodes[3] = CS2_OP_PUSH_CONSTANT_INT;
    script->script.opcodes[4] = CS2_OP_RETURN;
    script->script.int_operands[0] = k_component_id;
    script->script.int_operands[1] = 1;
    script->script.int_operands[2] = 0;
    script->script.int_operands[3] = 0;
    script->script.int_operands[4] = 0;
    return script;
}

static void
free_hide_script(struct ToriAuxLibCore_ClientScript* script)
{
    if( !script )
        return;
    cs2_script_free(&script->script);
    free(script);
}

static int
test_cs2_host_if_sethide(void)
{
    struct UITree* tree = uitree_new(4);
    TEST_ASSERT(tree != NULL, "uitree_new");

    struct UINodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = k_component_id;
    int32_t idx = uitree_push(tree, -1, &spec);
    TEST_ASSERT(idx >= 0, "uitree_push");

    struct ToriAuxLibVM* vm = ToriAuxLibVM_New();
    struct CS2VM* cs2vm = cs2vm_new();
    struct CS2Host cs2host;
    struct CS2HostUIInitArgs args = {
        .vm = vm,
        .tree = tree,
    };
    cs2_host_ui_init(&cs2host, &args);

    struct ToriAuxLibCore_ClientScript* script = make_hide_script();
    TEST_ASSERT(script != NULL, "make_hide_script");
    (void)cs2vm_run(cs2vm, &script->script, &cs2host, NULL);
    TEST_ASSERT(tree->components[idx].behavior.hide == 1, "IF_SETHIDE applied");

    cs2vm_free(cs2vm);
    ToriAuxLibVM_Free(vm);
    uitree_free(tree);
    free_hide_script(script);
    fprintf(stderr, "ok: cs2 host IF_SETHIDE mutates tree\n");
    return 0;
}

static int
test_inv_transmit_dispatch(void)
{
    struct ToriAuxLibCore* core = ToriAuxLibCore_New();
    TEST_ASSERT(core != NULL, "ToriAuxLibCore_New");

    struct ToriAuxLibCore_Component* component = calloc(1, sizeof(*component));
    TEST_ASSERT(component != NULL, "component alloc");
    component->id = k_component_id;
    component->on_inv_transmit.argc = 2;
    component->on_inv_transmit.argv[0] = 9001;
    component->inventory_triggers_count = 1;
    component->inventory_triggers[0] = 93;
    ToriAuxLibCore_ComponentAdd(core, k_component_id, component);

    struct ToriAuxLibCore_ClientScript* script = make_hide_script();
    TEST_ASSERT(script != NULL, "make_hide_script");
    ToriAuxLibCore_ClientScriptAdd(core, 9001, script);

    struct UITree* tree = uitree_new(4);
    struct UINodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = k_component_id;
    int32_t idx = uitree_push(tree, -1, &spec);
    TEST_ASSERT(idx >= 0, "uitree_push");

    struct ToriAuxLibVM* vm = ToriAuxLibVM_New();
    struct CS2VM* cs2vm = cs2vm_new();
    struct CS2Host cs2host;
    struct CS2HostUIInitArgs args = {
        .core = core,
        .vm = vm,
        .tree = tree,
    };
    cs2_host_ui_init(&cs2host, &args);

    struct UITreeBehaviorHost host = {
        .cs2vm = cs2vm,
        .cs2host = cs2host,
        .varp_varbit = ToriAuxLibVM_VarPVarBit(vm),
    };

    uitree_behavior_dispatch_inv_transmit(&host, core, NULL, tree, 93);
    TEST_ASSERT(tree->components[idx].behavior.hide == 1, "onInvTransmit hook ran");

    uitree_behavior_dispatch_inv_transmit(&host, core, NULL, tree, 94);
    TEST_ASSERT(tree->components[idx].behavior.hide == 1, "unmatched container ignored");

    cs2vm_free(cs2vm);
    ToriAuxLibVM_Free(vm);
    uitree_free(tree);
    ToriAuxLibCore_Free(core);
    fprintf(stderr, "ok: onInvTransmit dispatch\n");
    return 0;
}

static int
test_varp_transmit_dispatch(void)
{
    struct ToriAuxLibCore* core = ToriAuxLibCore_New();
    struct ToriAuxLibCore_Component* component = calloc(1, sizeof(*component));
    component->id = k_component_id;
    component->on_varp_transmit.argc = 2;
    component->on_varp_transmit.argv[0] = 9002;
    component->varp_triggers_count = 1;
    component->varp_triggers[0] = 7;
    ToriAuxLibCore_ComponentAdd(core, k_component_id, component);

    struct ToriAuxLibCore_ClientScript* script = make_hide_script();
    ToriAuxLibCore_ClientScriptAdd(core, 9002, script);

    struct UITree* tree = uitree_new(4);
    struct UINodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = k_component_id;
    int32_t idx = uitree_push(tree, -1, &spec);

    struct ToriAuxLibVM* vm = ToriAuxLibVM_New();
    struct CS2VM* cs2vm = cs2vm_new();
    struct CS2Host cs2host;
    struct CS2HostUIInitArgs args = { .core = core, .vm = vm, .tree = tree };
    cs2_host_ui_init(&cs2host, &args);

    struct UITreeBehaviorHost host = {
        .cs2vm = cs2vm,
        .cs2host = cs2host,
        .varp_varbit = ToriAuxLibVM_VarPVarBit(vm),
    };

    uitree_behavior_dispatch_varp_transmit(&host, core, NULL, tree, 7);
    TEST_ASSERT(tree->components[idx].behavior.hide == 1, "onVarpTransmit hook ran");

    cs2vm_free(cs2vm);
    ToriAuxLibVM_Free(vm);
    uitree_free(tree);
    ToriAuxLibCore_Free(core);
    fprintf(stderr, "ok: onVarpTransmit dispatch\n");
    return 0;
}

int
main(void)
{
    int failures = 0;
    failures += test_cs2_host_if_sethide();
    failures += test_inv_transmit_dispatch();
    failures += test_varp_transmit_dispatch();
    if( failures == 0 )
    {
        printf("All ui_transmit_dispatch tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d test group(s) failed.\n", failures);
    return 1;
}
