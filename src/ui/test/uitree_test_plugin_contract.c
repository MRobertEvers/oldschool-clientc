#include "test_harness.h"

/* M1's executable red cases use the production tree mutation/getter paths.
 * The revision harness remains the end-to-end acceptance gate in M3/M6. */
void test_plugin_contract_native(void)
{
    struct UITree* tree = UITree_New(16);
    int root = UITree_TestPushXy(tree,-1,UIELEM_RS_LAYER,0x120000,0,0,100,100);
    int node = UITree_TestPushXy(tree,root,UIELEM_RS_GRAPHIC,0x120001,0,0,20,20);
    UITree_SetHideAt(tree,node,1);
    UITree_ApplyObject(tree,0x120001,995,5,1,0,0);
    TEST_ASSERT(tree->components[node].native_hide, "content update cannot clear native hiding");
    UITree_SetSizeModesAt(tree,node,100,10,1,0);
    UITree_LayoutResolve(tree,0,0,100,100);
    TEST_ASSERT(tree->components[node].position.abs_w == 0, "fixture resolves to zero width");
    TEST_ASSERT(UITree_GetLayoutWidth(tree,0x120001) == 0, "computed zero is not replaced by requested width");
    int field = UITree_CcCreate(tree,root,0x120000,12,4);
    int id = tree->components[field].component_id;
    uint32_t incarnation = tree->components[field].incarnation;
    UITree_InputSetFocusId(tree,id);
    UITree_CcDelete(tree,field);
    tree->next_dynamic_uid = (uint16_t)(id & 0xffff); /* exercise the allocator's reuse boundary */
    int replacement = UITree_CcCreate(tree,root,0x120000,12,4);
    TEST_ASSERT(tree->components[replacement].component_id == id, "native id was recycled");
    TEST_ASSERT(tree->components[replacement].incarnation != incarnation, "replacement is another incarnation");
    TEST_ASSERT(UITree_InputFocusId(tree) < 0, "old focus cannot move to a recycled component id");
    UITree_Free(tree);
}
