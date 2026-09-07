#include "test_harness.h"

struct MutationHost {
    struct TestHostState state;
    struct UITree* tree;
    int node, mutate;
};
static int mutation_host(void* user, struct UITreeHostRequest* req)
{
    struct MutationHost* m = user;
    int result = UITree_TestHostRequest(&m->state, req);
    if( m->mutate && req->kind == UITREE_HOST_BEGIN_OVERLAYS )
    {
        m->mutate = 0;
        UITree_SetHideAt(m->tree, m->node, !m->tree->components[m->node].behavior.hide);
    }
    return result;
}

static void
motion_prime(struct UITree* tree, struct UITreeHost* host, struct UITreeEmitBuffer* emit,
    struct UITreeEmitRetainGate* gate)
{
    emit->count = 0;
    UITree_EmitWalk(tree, host, emit, -1);
    UITree_EmitRetainGateCapture(tree, emit, -1, gate);
}

static void
motion_compare(struct UITree* tree, struct UITreeHost* host, struct UITreeEmitBuffer* emit)
{
    struct UITreeEmitBuffer reference;
    UITree_EmitBufferInit(&reference);
    UITree_EmitWalk(tree, host, &reference, -1);
    TEST_ASSERT(emit->count == reference.count, "partial/full emitted count");
    if( emit->count == reference.count )
        TEST_ASSERT(!memcmp(emit->cmds, reference.cmds, (size_t)emit->count*sizeof(*emit->cmds)),
            "partial/full exact descriptors, clipping and painter order");
    UITree_EmitBufferFree(&reference);
}

void test_overlay_retention(void)
{
    printf("TEST: projected overlay motion retains stable UI commands\n");
    UITree_EmitSetOverlayRetain(1);
    struct UITree* tree = UITree_New(8);
    int root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 1, 0, 0, 765, 503);
    UITree_TestPushXy(tree, root, UIELEM_BUILTIN_WORLD, 2, 4, 4, 512, 334);
    int panel = UITree_TestPushXy(tree, root, UIELEM_RS_RECT, 3, 500, 0, 265, 503);
    int overlay = UITree_TestPushXy(tree, -1, UIELEM_BUILTIN_ENTITY_OVERLAY, 4, 4, 4, 512, 334);
    int layer = UITree_EntityOverlayCreateLayer(tree, 0, 60, 60);
    int child = UITree_TestPushXy(tree, layer, UIELEM_RS_RECT, 5, 3, 7, 36, 32);
    struct UITreeHost host; struct TestHostState state;
    UITree_TestHostInit(&host, &state);
    struct UITreeEntityOverlay bar = {.kind=UITREE_ENTITY_OVERLAY_RECT,.x=10,.y=10,.w=20,.h=4};
    struct UITreeEmitBuffer emit; UITree_EmitBufferInit(&emit);
    struct UITreeEmitRetainGate gate = {0}, moved;
    int hovered = -1;
    motion_prime(tree, &host, &emit, &gate);
    TEST_ASSERT(emit.overlay_range_valid, "plain root overlay range captured");
    for( int n=0; n<80; ++n )
    {
        TEST_ASSERT(UITree_EmitOverlayMotionBegin(tree,&host,&emit,hovered,&gate), "motion begin on quiet tree");
        UITree_EntityOverlaySetLayerPosition(tree,layer,n*17-200,n*11-100);
        TEST_ASSERT(UITree_EmitOverlayMotionEnd(tree,&gate,&moved), "accounted moves resolve once");
        /* Test insertion/removal before the scripted range and after it. */
        state.entity_overlays = (n&1) ? &bar : NULL;
        state.entity_overlay_count = n&1;
        state.entity_overlay_clip_x=4; state.entity_overlay_clip_y=4;
        state.entity_overlay_clip_w=512; state.entity_overlay_clip_h=334;
        state.frame_overlays = (n&2) ? &bar : NULL; state.frame_overlay_count = !!(n&2);
        TEST_ASSERT(UITree_EmitOverlayMotionRefresh(tree,&host,&emit,&hovered,&moved), "partial motion refresh succeeds");
        motion_compare(tree,&host,&emit);
        UITree_EmitRetainGateCapture(tree,&emit,hovered,&gate);
    }
    TEST_ASSERT(UITree_EmitOverlayMotionBegin(tree,&host,&emit,hovered,&gate), "start mixed mutation window");
    UITree_EntityOverlaySetLayerPosition(tree,layer,20,30);
    UITree_SetHideAt(tree,panel,1);
    TEST_ASSERT(!UITree_EmitOverlayMotionEnd(tree,&gate,&moved), "unclassified visibility change rejects retention");
    motion_prime(tree,&host,&emit,&gate);
    TEST_ASSERT(UITree_EmitOverlayMotionBegin(tree,&host,&emit,hovered,&gate), "start layout invalidation window");
    UITree_EntityOverlaySetLayerPosition(tree,layer,21,31);
    UITree_LayoutInvalidateBoxes(tree);
    TEST_ASSERT(!UITree_EmitOverlayMotionEnd(tree,&gate,&moved), "unclassified layout invalidation rejects retention");
    motion_prime(tree,&host,&emit,&gate);
    TEST_ASSERT(UITree_EmitOverlayMotionBegin(tree,&host,&emit,hovered,&gate), "start host-change window");
    UITree_EntityOverlaySetLayerPosition(tree,layer,22,32);
    TEST_ASSERT(UITree_EmitOverlayMotionEnd(tree,&gate,&moved), "host-change window resolves");
    UITree_HostInputsChanged(&host,UITREE_HOST_INPUT_ALL);
    TEST_ASSERT(!UITree_EmitOverlayMotionRefresh(tree,&host,&emit,&hovered,&moved), "new host inputs reject partial publication");
    motion_prime(tree,&host,&emit,&gate);
    TEST_ASSERT(UITree_EmitOverlayMotionBegin(tree,&host,&emit,hovered,&gate), "start reparent window");
    UITree_EntityOverlaySetLayerPosition(tree,layer,23,33);
    UITree_Reparent(tree,child,root);
    TEST_ASSERT(!UITree_EmitOverlayMotionEnd(tree,&gate,&moved), "reparent rejects partial publication");
    motion_prime(tree,&host,&emit,&gate);
    TEST_ASSERT(emit.overlay_range_valid && emit.overlay_range_count==0, "empty child range remains addressable");
    TEST_ASSERT(UITree_EmitOverlayMotionBegin(tree,&host,&emit,hovered,&gate), "start empty range motion");
    UITree_EntityOverlaySetLayerPosition(tree,layer,24,34);
    TEST_ASSERT(UITree_EmitOverlayMotionEnd(tree,&gate,&moved), "empty range resolves");
    state.entity_overlays=NULL; state.entity_overlay_count=0;
    TEST_ASSERT(UITree_EmitOverlayMotionRefresh(tree,&host,&emit,&hovered,&moved), "empty range refresh");
    motion_compare(tree,&host,&emit);
    UITree_EmitRetainGateCapture(tree,&emit,hovered,&gate);
    UITree_SetSizeAt(tree,overlay,400,300);
    TEST_ASSERT(!UITree_EmitOverlayMotionBegin(tree,&host,&emit,hovered,&gate), "viewport mutation rejects begin");
    motion_prime(tree,&host,&emit,&gate);
    struct MutationHost mut = {.tree=tree,.node=panel};
    struct UITreeHost changing = {.user=&mut,.request=mutation_host};
    motion_prime(tree,&changing,&emit,&gate);
    TEST_ASSERT(UITree_EmitOverlayMotionBegin(tree,&changing,&emit,hovered,&gate), "start callback mutation window");
    UITree_EntityOverlaySetLayerPosition(tree,layer,25,35);
    TEST_ASSERT(UITree_EmitOverlayMotionEnd(tree,&gate,&moved), "callback window resolves");
    mut.mutate=1;
    TEST_ASSERT(!UITree_EmitOverlayMotionRefresh(tree,&changing,&emit,&hovered,&moved), "callback mutation revokes partial publication");
    motion_prime(tree,&host,&emit,&gate);
    TEST_ASSERT(UITree_EmitOverlayMotionBegin(tree,&host,&emit,hovered,&gate), "start canvas mutation window");
    UITree_EntityOverlaySetLayerPosition(tree,layer,26,36);
    TEST_ASSERT(UITree_EmitOverlayMotionEnd(tree,&gate,&moved), "canvas window resolves");
    int old_width=UITREE_LAYOUT_ROOT_W, old_height=UITREE_LAYOUT_ROOT_H;
    UITree_LayoutSetRootSize(old_width+40,old_height);
    TEST_ASSERT(!UITree_EmitOverlayMotionRefresh(tree,&host,&emit,&hovered,&moved), "same-frame global canvas resize rejects partial publication");
    UITree_LayoutSetRootSize(old_width,old_height);
    UITree_Clear(tree);
    TEST_ASSERT(!UITree_EmitOverlayMotionBegin(tree,&host,&emit,hovered,&gate), "tree clear rejects old range");
    UITree_EmitBufferFree(&emit); UITree_Free(tree);
    UITree_EmitSetOverlayRetain(0);
}
