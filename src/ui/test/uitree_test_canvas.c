#include "test_harness.h"
#include "ui/uitree_canvas_measure.h"

static void check_widths(struct UITree* tree,int w,int h)
{
    struct UITreeCanvasWidths a=UITree_CanvasMeasureReference(tree,w,h);
    struct UITreeCanvasWidths b=UITree_CanvasMeasureCompact(tree,w,h);
    TEST_ASSERT(a.strip==b.strip && a.core==b.core,"canvas candidate/reference parity");
}
void test_canvas_queries(void)
{
    printf("TEST: canvas candidate queries and mutation lifetime\n");
    struct UITree* t=UITree_New(4);
    int root=UITree_TestPushXy(t,-1,UIELEM_RS_LAYER,1,0,0,0,0);
    UITree_SetSizeModesAt(t,root,0,0,1,1);
    int strip=UITree_TestPushXy(t,root,UIELEM_RS_LAYER,2,0,0,42,0);
    UITree_SetSizeModesAt(t,strip,42,0,0,1);
    UITree_SetPositionModesAt(t,strip,0,0,2,0);
    int area=UITree_TestPushXy(t,root,UIELEM_RS_LAYER,3,0,0,42,0);
    UITree_SetSizeModesAt(t,area,42,0,1,1);
    int core=UITree_TestPushXy(t,area,UIELEM_RS_RECT,4,0,0,765,100);
    UITree_SetSizeModesAt(t,core,765,100,0,0);
    UITree_LayoutResolve(t,0,0,720,480);
    struct UITreeCanvasWidths initial=UITree_CanvasMeasureCompact(t,720,480);
    TEST_ASSERT(initial.strip==42 && initial.core==765,"strip and overflowing native width");
    UITree_Reparent(t,core,root);
    check_widths(t,720,480);
    TEST_ASSERT(UITree_CanvasMeasureCompact(t,720,480).core==0,"reparent removes lane overflow");
    UITree_Reparent(t,core,area);
    UITree_LayoutResolve(t,0,0,720,480);
    check_widths(t,720,480);
    TEST_ASSERT(UITree_CanvasMeasureCompact(t,720,480).core==765,"reparent restores lane overflow");
    /* Simulate an old cached generation colliding after counter wrap. Explicit
     * topology invalidation must defeat equality of generation and node count. */
    t->generation=UINT32_MAX;
    UITree_CanvasMeasureCompact(t,720,480);
    UITree_Reparent(t,core,root);
    t->canvas_candidate_generation=t->generation;
    check_widths(t,720,480);
    TEST_ASSERT(UITree_CanvasMeasureCompact(t,720,480).core==0,"topology wrap cannot retain stale membership");
    UITree_Reparent(t,core,area);
    UITree_LayoutResolve(t,0,0,720,480);
    check_widths(t,720,480);
    uint32_t members=t->canvas_candidate_count;
    for( int n=0;n<128;n++ ) {
        /* Visibility and resolved geometry must be live even while candidate
         * membership and the topology generation remain unchanged. */
        UITree_SetHideAt(t,root,n&1);
        check_widths(t,720,480);
        UITree_SetHideAt(t,root,0);
        UITree_SetSizeAt(t,core,600+n*2,100);
        check_widths(t,720,480); /* query before resolving pending layout */
        UITree_LayoutResolve(t,0,0,720,480);
        check_widths(t,720,480);
    }
    TEST_ASSERT(t->canvas_candidate_count==members,"geometry changes do not grow candidate set");
    UITree_SetPositionModesAt(t,strip,0,0,0,0);
    check_widths(t,720,480);
    UITree_SetPositionModesAt(t,strip,0,0,2,0);
    UITree_LayoutResolve(t,0,0,720,480);
    check_widths(t,720,480);
    UITree_SetSizeModesAt(t,area,42,0,0,1);
    check_widths(t,720,480); /* parent modes change core eligibility */
    UITree_SetSizeModesAt(t,area,42,0,1,1);
    check_widths(t,720,480);
    for( int n=0;n<65;n++ ) {
        int node=UITree_TestPushXy(t,root,UIELEM_RS_LAYER,100+n,0,0,n+1,0);
        UITree_SetSizeModesAt(t,node,n+1,0,0,1);
        UITree_SetPositionModesAt(t,node,0,0,2,0);
        UITree_LayoutResolve(t,0,0,720,480);
        check_widths(t,720,480);
    }
    UITree_SetSizeModesAt(t,area,65,0,1,1); /* widest strip added above */
    UITree_LayoutResolve(t,0,0,720,480);
    int late=UITree_TestPushXy(t,UITREE_PARENT_UNLINKED,UIELEM_RS_RECT,900,0,0,900,100);
    UITree_SetSizeModesAt(t,late,900,100,0,0);
    check_widths(t,720,480); /* query while a freshly baked node is unlinked */
    UITree_LinkUnderParent(t,area,late);
    UITree_LayoutResolve(t,0,0,720,480);
    check_widths(t,720,480);
    TEST_ASSERT(UITree_CanvasMeasureCompact(t,720,480).core==900,"late link rebuilds candidate membership");
    UITree_LayoutResolve(t,0,0,1000,700);
    check_widths(t,1000,700);
    UITree_Clear(t);
    check_widths(t,720,480);
    root=UITree_TestPushXy(t,-1,UIELEM_RS_LAYER,700,0,0,0,0);
    UITree_SetSizeModesAt(t,root,0,0,1,1);
    check_widths(t,720,480); /* reclaimed slot incarnation */
    UITree_Free(t);
}
