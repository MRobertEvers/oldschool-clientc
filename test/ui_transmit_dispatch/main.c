#include "vm/cs2vmx.h"
#include "vm/cs2_opcode.h"
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

#define ENQUEUE_MAX 8

struct EnqueueEntry
{
    int script_id;
    int component_id;
    int int_args[16];
    int int_arg_count;
};

struct EnqueueCapture
{
    struct EnqueueEntry entries[ENQUEUE_MAX];
    int count;
};

static void
test_cs2_enqueue(
    void* ud,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count)
{
    struct EnqueueCapture* cap = ud;
    if( !cap || cap->count >= ENQUEUE_MAX )
        return;
    struct EnqueueEntry* e = &cap->entries[cap->count++];
    e->script_id = script_id;
    e->component_id = component_id;
    e->int_arg_count = int_arg_count > 16 ? 16 : int_arg_count;
    if( e->int_arg_count > 0 && int_args )
        memcpy(e->int_args, int_args, (size_t)e->int_arg_count * sizeof(int));
}

struct TestUiHost
{
    struct UITree* tree;
};

static int
test_ui_host_exec(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request)
{
    struct TestUiHost* host = CS2VM_USER(vm);
    if( !host || !request )
        return CS2VM_EXECNO_ERROR;

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_IF_SETHIDE:
        if( host->tree )
            (void)uitree_apply_hide(
                host->tree,
                request->u.if_set_hide.component_id,
                request->u.if_set_hide.hidden ? 1 : 0);
        return CS2VM_EXECNO_OK;
    default:
        return CS2VM_EXECNO_OK;
    }
}

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
    /* IF_SETHIDE: push hide, then component (component on top). */
    script->script.opcodes[0] = CS2_OP_PUSH_CONSTANT_INT;
    script->script.opcodes[1] = CS2_OP_PUSH_CONSTANT_INT;
    script->script.opcodes[2] = CS2_OP_IF_SETHIDE;
    script->script.opcodes[3] = CS2_OP_PUSH_CONSTANT_INT;
    script->script.opcodes[4] = CS2_OP_RETURN;
    script->script.int_operands[0] = 1;
    script->script.int_operands[1] = k_component_id;
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
run_script_on_tree(
    struct UITree* tree,
    struct CS2_Script* script)
{
    struct CS2VMX vm;
    struct TestUiHost host;
    memset(&vm, 0, sizeof(vm));
    memset(&host, 0, sizeof(host));
    host.tree = tree;
    CS2VMX_BindHost(&vm, &host, test_ui_host_exec);
    CS2VMX_ResetRuntime(&vm);
    if( CS2VMX_PushCallScript(&vm, script) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VMX_RunScript(&vm);
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

    struct ToriAuxLibCore_ClientScript* script = make_hide_script();
    TEST_ASSERT(script != NULL, "make_hide_script");
    TEST_ASSERT(
        run_script_on_tree(tree, &script->script) == CS2VM_EXECNO_DONE, "IF_SETHIDE run");
    TEST_ASSERT(tree->components[idx].behavior.hide == 1, "IF_SETHIDE applied");

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

    struct EnqueueCapture capture;
    memset(&capture, 0, sizeof(capture));

    struct ToriAuxLibVM* vm = ToriAuxLibVM_New();
    struct UITreeBehaviorHost host = {
        .varp_varbit = ToriAuxLibVM_VarPVarBit(vm),
        .cs2_enqueue = test_cs2_enqueue,
        .cs2_enqueue_ud = &capture,
    };

    uitree_behavior_dispatch_inv_transmit(&host, core, NULL, tree, 93);
    TEST_ASSERT(capture.count == 1, "onInvTransmit enqueued once");
    TEST_ASSERT(capture.entries[0].script_id == 9001, "inv script id");
    TEST_ASSERT(capture.entries[0].component_id == k_component_id, "inv component id");

    /* Execute the enqueued script to confirm hide still applies. */
    TEST_ASSERT(
        run_script_on_tree(tree, &script->script) == CS2VM_EXECNO_DONE, "run enqueued script");
    TEST_ASSERT(tree->components[idx].behavior.hide == 1, "onInvTransmit hook ran");

    capture.count = 0;
    uitree_behavior_dispatch_inv_transmit(&host, core, NULL, tree, 94);
    TEST_ASSERT(capture.count == 0, "unmatched container ignored");

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

    struct EnqueueCapture capture;
    memset(&capture, 0, sizeof(capture));

    struct ToriAuxLibVM* vm = ToriAuxLibVM_New();
    struct UITreeBehaviorHost host = {
        .varp_varbit = ToriAuxLibVM_VarPVarBit(vm),
        .cs2_enqueue = test_cs2_enqueue,
        .cs2_enqueue_ud = &capture,
    };

    uitree_behavior_dispatch_varp_transmit(&host, core, NULL, tree, 7);
    TEST_ASSERT(capture.count == 1, "onVarpTransmit enqueued once");
    TEST_ASSERT(capture.entries[0].script_id == 9002, "varp script id");

    TEST_ASSERT(
        run_script_on_tree(tree, &script->script) == CS2VM_EXECNO_DONE, "run enqueued script");
    TEST_ASSERT(tree->components[idx].behavior.hide == 1, "onVarpTransmit hook ran");

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
