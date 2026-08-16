/*
 * CS1 integration tests: the real UITree build path, the real game host reading
 * real inv/varp managers, and the real emit pass. Covers the wiring the VM unit
 * tests cannot — that script arrays survive the build into the tree, that the
 * host resolves state, and that active-state and %N substitution reach the
 * emit descriptors.
 */

#include "game/rs_cs1_host.h"
#include "inv/inv_manager.h"
#include "ui/uitree.h"
#include "ui/uitree_build.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_host.h"
#include "ui/uitree_layout.h"
#include "varp/varp_manager.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* Varp reads/writes are bounded by the registered type count, so a test must
 * declare a varp space before it can set one. */
static void
cs1_test_declare_varps(struct VarPManager* varps)
{
    static struct VarPType types[128];
    VarPManager_SetVarpTypes(varps, types, 128);
}

/* Emit host: answers the two CS1 requests from the cached per-node results,
 * exactly as app_host_request does. */
static int
cs1_test_host_request(void* user, struct UITreeHostRequest* req)
{
    (void)user;
    switch( req->kind )
    {
    case UITREE_HOST_IS_ACTIVE:
        if( !req->u.is_active.component )
            return 0;
        return req->u.is_active.component->cs1_active ? 1 : 0;
    case UITREE_HOST_EVAL_TEXT_PLACEHOLDER:
        if( !req->u.eval_text_placeholder.component ||
            req->u.eval_text_placeholder.script_idx < 0 ||
            req->u.eval_text_placeholder.script_idx >= UITREE_CS1_VALUE_MAX )
            return 0;
        return req->u.eval_text_placeholder.component
            ->cs1_values[req->u.eval_text_placeholder.script_idx];
    default:
        return 0;
    }
}

/* Evaluate every scripted node into its cache — the task's inner loop, minus
 * the async plumbing (nothing here yields: no inv opcode names a missing node). */
static void
cs1_test_eval_tree(
    struct RS_CS1Host* host,
    struct UITree* tree)
{
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent* component = &tree->components[i];
        if( component->freed || component->behavior.scripts_count <= 0 )
            continue;

        bool active = false;
        if( RS_CS1Host_ComponentActive(host, component, &active) == CS1VM_EVAL_DONE )
            component->cs1_active = active ? 1 : 0;

        for( int s = 0; s < UITREE_CS1_VALUE_MAX; s++ )
        {
            int value = 0;
            if( RS_CS1Host_ComponentValue(host, component, s, &value) == CS1VM_EVAL_DONE )
                component->cs1_values[s] = value;
        }
    }
}

/* A text component carrying one CS1 value script + comparator, pushed through
 * the real build path so UITree_SetBehavior does the deep copy. */
static int32_t
push_scripted_text(
    struct UITree* tree,
    int component_id,
    char const* text,
    char const* text_active,
    int* script,
    int script_len,
    int comparator,
    int operand)
{
    static int* scripts[1];
    static int lengths[1];
    static int comparators[1];
    static int operands[1];

    scripts[0] = script;
    lengths[0] = script_len;
    comparators[0] = comparator;
    operands[0] = operand;

    struct UIBuildComponent comp;
    memset(&comp, 0, sizeof(comp));
    comp.id = component_id;
    comp.type = UIBUILD_TEXT;
    comp.parent_id = -1;
    comp.base_width = 100;
    comp.base_height = 20;
    comp.text = text;
    comp.text_active = text_active;
    comp.color = 0x111111;
    comp.active_color = 0xff0000;
    comp.scripts_count = 1;
    comp.scripts = scripts;
    comp.scripts_lengths = lengths;
    comp.comparator_count = 1;
    comp.script_comparator = comparators;
    comp.script_operand = operands;

    return UITree_PushBuildComponent(tree, -1, &comp, NULL, NULL, NULL);
}

static void
test_scripts_reach_the_tree(void)
{
    printf("TEST: build path carries CS1 scripts into the tree\n");

    struct UITree* tree = UITree_New(8);
    int script[] = { CS1_OP_PUSH_VARP, 42, CS1_OP_RETURN };

    int32_t idx = push_scripted_text(
        tree, 100, "plain", "active", script, 3, CS1VM_COMPARATOR_EQUAL, 7);
    TEST_ASSERT(idx >= 0, "component pushed");

    struct UITreeComponent const* component = &tree->components[idx];
    TEST_ASSERT(component->behavior.scripts_count == 1, "scripts_count carried");
    TEST_ASSERT(component->behavior.comparator_count == 1, "comparator_count carried");
    TEST_ASSERT(component->behavior.scripts != NULL, "scripts array copied");
    TEST_ASSERT(component->behavior.scripts[0] != script, "scripts deep-copied, not aliased");
    TEST_ASSERT(component->behavior.scripts_lengths[0] == 3, "script length carried");
    TEST_ASSERT(component->behavior.scripts[0][1] == 42, "script contents carried");
    TEST_ASSERT(component->behavior.script_comparator[0] == CS1VM_COMPARATOR_EQUAL,
                "comparator carried");
    TEST_ASSERT(component->behavior.script_operand[0] == 7, "operand carried");

    UITree_Free(tree);
}

static void
test_active_state_from_varp(void)
{
    printf("TEST: varp drives active state through the host\n");

    struct UITree* tree = UITree_New(8);
    struct InvManager invs;
    struct VarPManager varps;
    struct RS_PlayerStats stats;
    struct RS_CS1Host host;

    InvManager_Init(&invs);
    VarPManager_Init(&varps);
    cs1_test_declare_varps(&varps);
    RS_PlayerStats_Init(&stats);
    RS_CS1Host_Init(&host, tree, NULL, &invs, &varps, &stats);

    /* Active when varp 42 == 7. */
    int script[] = { CS1_OP_PUSH_VARP, 42, CS1_OP_RETURN };
    int32_t idx = push_scripted_text(
        tree, 100, "off", "on", script, 3, CS1VM_COMPARATOR_EQUAL, 7);

    struct UITreeComponent* component = &tree->components[idx];
    bool active = true;

    VarPManager_SetVarpOptimistic(&varps, 42, 0);
    TEST_ASSERT(RS_CS1Host_ComponentActive(&host, component, &active) == CS1VM_EVAL_DONE,
                "eval completes with varp mismatched");
    TEST_ASSERT(!active, "varp 0 != 7 -> inactive");

    VarPManager_SetVarpOptimistic(&varps, 42, 7);
    TEST_ASSERT(RS_CS1Host_ComponentActive(&host, component, &active) == CS1VM_EVAL_DONE,
                "eval completes with varp matched");
    TEST_ASSERT(active, "varp 7 == 7 -> active");

    InvManager_Free(&invs);
    VarPManager_Free(&varps);
    UITree_Free(tree);
}

static void
test_emit_swaps_active_text_and_color(void)
{
    printf("TEST: emit swaps to active text/colour\n");

    struct UITree* tree = UITree_New(8);
    struct InvManager invs;
    struct VarPManager varps;
    struct RS_PlayerStats stats;
    struct RS_CS1Host host;
    struct UITreeHost ui_host;

    InvManager_Init(&invs);
    VarPManager_Init(&varps);
    cs1_test_declare_varps(&varps);
    RS_PlayerStats_Init(&stats);
    RS_CS1Host_Init(&host, tree, NULL, &invs, &varps, &stats);

    UITree_HostInit(&ui_host);
    ui_host.user = NULL;
    ui_host.request = cs1_test_host_request;

    int script[] = { CS1_OP_PUSH_VARP, 42, CS1_OP_RETURN };
    int32_t idx = push_scripted_text(
        tree, 100, "Inactive", "Active", script, 3, CS1VM_COMPARATOR_EQUAL, 7);
    (void)idx;

    UITree_LayoutResolve(tree, 0, 0, 800, 600);

    /* Inactive: base text and colour. */
    VarPManager_SetVarpOptimistic(&varps, 42, 0);
    cs1_test_eval_tree(&host, tree);

    struct UITreeEmitBuffer buf;
    UITree_EmitBufferInit(&buf);
    UITree_EmitWalk(tree, &ui_host, &buf, -1);
    TEST_ASSERT(buf.count == 1, "one text emitted");
    TEST_ASSERT(strcmp(buf.cmds[0].text, "Inactive") == 0, "inactive text used");
    TEST_ASSERT(buf.cmds[0].color == 0x111111, "inactive colour used");
    UITree_EmitBufferFree(&buf);

    /* Active: swaps to text_active and active_color. */
    VarPManager_SetVarpOptimistic(&varps, 42, 7);
    cs1_test_eval_tree(&host, tree);

    UITree_EmitBufferInit(&buf);
    UITree_EmitWalk(tree, &ui_host, &buf, -1);
    TEST_ASSERT(buf.count == 1, "one text emitted when active");
    TEST_ASSERT(strcmp(buf.cmds[0].text, "Active") == 0, "active text used");
    TEST_ASSERT(buf.cmds[0].color == 0xff0000, "active colour used");
    UITree_EmitBufferFree(&buf);

    InvManager_Free(&invs);
    VarPManager_Free(&varps);
    UITree_Free(tree);
}

static void
test_emit_substitutes_placeholder(void)
{
    printf("TEST: emit substitutes %%N from the script value\n");

    struct UITree* tree = UITree_New(8);
    struct InvManager invs;
    struct VarPManager varps;
    struct RS_PlayerStats stats;
    struct RS_CS1Host host;
    struct UITreeHost ui_host;

    InvManager_Init(&invs);
    VarPManager_Init(&varps);
    cs1_test_declare_varps(&varps);
    RS_PlayerStats_Init(&stats);
    RS_CS1Host_Init(&host, tree, NULL, &invs, &varps, &stats);

    UITree_HostInit(&ui_host);
    ui_host.user = NULL;
    ui_host.request = cs1_test_host_request;

    /* Comparator never matches, so the base text with the placeholder is used. */
    int script[] = { CS1_OP_PUSH_VARP, 42, CS1_OP_RETURN };
    push_scripted_text(
        tree, 100, "Level: %1", NULL, script, 3, CS1VM_COMPARATOR_EQUAL, -12345);

    UITree_LayoutResolve(tree, 0, 0, 800, 600);

    VarPManager_SetVarpOptimistic(&varps, 42, 73);
    cs1_test_eval_tree(&host, tree);

    struct UITreeEmitBuffer buf;
    UITree_EmitBufferInit(&buf);
    UITree_EmitWalk(tree, &ui_host, &buf, -1);
    TEST_ASSERT(buf.count == 1, "one text emitted");
    TEST_ASSERT(strcmp(buf.cmds[0].text_formatted, "Level: 73") == 0,
                "%1 replaced with the script value");
    UITree_EmitBufferFree(&buf);

    /* The infinity sentinel renders as "*" (reference inf()). */
    VarPManager_SetVarpOptimistic(&varps, 42, CS1VM_VALUE_INFINITY);
    cs1_test_eval_tree(&host, tree);

    UITree_EmitBufferInit(&buf);
    UITree_EmitWalk(tree, &ui_host, &buf, -1);
    TEST_ASSERT(buf.count == 1, "one text emitted for sentinel");
    TEST_ASSERT(strcmp(buf.cmds[0].text_formatted, "Level: *") == 0,
                "infinity renders as *");
    UITree_EmitBufferFree(&buf);

    InvManager_Free(&invs);
    VarPManager_Free(&varps);
    UITree_Free(tree);
}

static void
test_inv_count_opcode(void)
{
    printf("TEST: inv_count reads the bound container\n");

    struct UITree* tree = UITree_New(8);
    struct InvManager invs;
    struct VarPManager varps;
    struct RS_PlayerStats stats;
    struct RS_CS1Host host;

    InvManager_Init(&invs);
    VarPManager_Init(&varps);
    cs1_test_declare_varps(&varps);
    RS_PlayerStats_Init(&stats);

    int source_id = InvManager_ResolveSource(&invs, INV_MANAGER_SOURCE_NAME_BACKPACK);
    TEST_ASSERT(source_id >= 0, "backpack source resolves");

    static int const items[] = { 995, 995, 1333 };
    TEST_ASSERT(
        InvManager_ApplyFull(&invs, INV_MANAGER_CONTAINER_BACKPACK, items, NULL, 3),
        "backpack seeded");

    /* An inv grid the script can name, plus a text node holding the script. */
    struct UITreeNodeSpec inv_spec;
    memset(&inv_spec, 0, sizeof(inv_spec));
    inv_spec.type = UIELEM_RS_INV;
    inv_spec.component_id = 200;
    inv_spec.width = 100;
    inv_spec.height = 100;
    inv_spec.u.rs_inv.inv_source_id = source_id;
    inv_spec.u.rs_inv.cols = 4;
    inv_spec.u.rs_inv.rows = 7;
    TEST_ASSERT(UITree_Push(tree, -1, &inv_spec) >= 0, "inv grid pushed");

    RS_CS1Host_Init(&host, tree, NULL, &invs, &varps, &stats);

    /* Active when the backpack holds exactly 2 of obj 995. */
    int script[] = { CS1_OP_INV_COUNT, 200, 995, CS1_OP_RETURN };
    int32_t idx = push_scripted_text(
        tree, 100, "off", "on", script, 4, CS1VM_COMPARATOR_EQUAL, 2);

    struct UITreeComponent* component = &tree->components[idx];
    bool active = false;
    TEST_ASSERT(RS_CS1Host_ComponentActive(&host, component, &active) == CS1VM_EVAL_DONE,
                "inv count evaluates without yielding for a mounted component");
    TEST_ASSERT(active, "two coins counted");

    InvManager_Free(&invs);
    VarPManager_Free(&varps);
    UITree_Free(tree);
}

static void
test_inv_yields_for_unmounted_component(void)
{
    printf("TEST: inv opcode yields once for an unmounted component\n");

    struct UITree* tree = UITree_New(8);
    struct InvManager invs;
    struct VarPManager varps;
    struct RS_PlayerStats stats;
    struct RS_CS1Host host;

    InvManager_Init(&invs);
    VarPManager_Init(&varps);
    cs1_test_declare_varps(&varps);
    RS_PlayerStats_Init(&stats);
    RS_CS1Host_Init(&host, tree, NULL, &invs, &varps, &stats);

    /* Component 9999 is not in the tree: the host must stage a load. */
    int script[] = { CS1_OP_INV_COUNT, 9999, 995, CS1_OP_RETURN };
    int32_t idx = push_scripted_text(
        tree, 100, "off", "on", script, 4, CS1VM_COMPARATOR_EQUAL, 0);

    struct UITreeComponent* component = &tree->components[idx];
    bool active = false;

    TEST_ASSERT(RS_CS1Host_ComponentActive(&host, component, &active) == CS1VM_EVAL_YIELDED,
                "missing component yields");
    TEST_ASSERT(host.has_pending, "yield staged a pending request");
    TEST_ASSERT(host.pending.kind == CS1VM_HOST_REQUEST_INV_COUNT, "pending kind is the inv read");
    TEST_ASSERT(host.pending.u.inv.component_id == 9999, "pending names the missing component");

    /* Driver services the load; the component still does not exist, so the
     * await is spent and the retry must complete with a default of 0. */
    host.has_pending = false;
    TEST_ASSERT(RS_CS1Host_ComponentActive(&host, component, &active) == CS1VM_EVAL_DONE,
                "retry completes instead of yielding again");
    TEST_ASSERT(active, "absent inventory counts as 0, matching the operand");

    InvManager_Free(&invs);
    VarPManager_Free(&varps);
    UITree_Free(tree);
}

static void
test_stat_defaults(void)
{
    printf("TEST: stat opcodes read the player store\n");

    struct UITree* tree = UITree_New(8);
    struct InvManager invs;
    struct VarPManager varps;
    struct RS_PlayerStats stats;
    struct RS_CS1Host host;

    InvManager_Init(&invs);
    VarPManager_Init(&varps);
    cs1_test_declare_varps(&varps);
    RS_PlayerStats_Init(&stats);
    RS_CS1Host_Init(&host, tree, NULL, &invs, &varps, &stats);

    /* Hitpoints starts at 10 on a fresh account. */
    int script[] = { CS1_OP_STAT_BASE_LEVEL, RS_SKILL_HITPOINTS, CS1_OP_RETURN };
    int32_t idx = push_scripted_text(
        tree, 100, "hp", NULL, script, 3, CS1VM_COMPARATOR_EQUAL, 10);

    struct UITreeComponent* component = &tree->components[idx];
    bool active = false;
    RS_CS1Host_ComponentActive(&host, component, &active);
    TEST_ASSERT(active, "default hitpoints base level is 10");

    stats.base_level[RS_SKILL_HITPOINTS] = 42;
    RS_CS1Host_ComponentActive(&host, component, &active);
    TEST_ASSERT(!active, "stat reads follow the store");

    /* The xp table is the classic curve: level 2 needs 83 xp. */
    /* level_xp[n] is the xp needed for level n + 2. */
    TEST_ASSERT(stats.level_xp[0] == 83, "xp table level 2 threshold");
    TEST_ASSERT(stats.level_xp[97] == 13034431, "xp table level 99 threshold");

    InvManager_Free(&invs);
    VarPManager_Free(&varps);
    UITree_Free(tree);
}

int
main(void)
{
    test_scripts_reach_the_tree();
    test_active_state_from_varp();
    test_emit_swaps_active_text_and_color();
    test_emit_substitutes_placeholder();
    test_inv_count_opcode();
    test_inv_yields_for_unmounted_component();
    test_stat_defaults();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }

    printf("All CS1 integration tests passed.\n");
    return 0;
}
