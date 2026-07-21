#ifndef UITREE_TEST_HARNESS_H
#define UITREE_TEST_HARNESS_H

#include "uitree.h"
#include "uitree_build.h"
#include "uitree_emit.h"
#include "uitree_host.h"
#include "uitree_hover.h"
#include "uitree_input.h"
#include "uitree_layout.h"
#include "uitree_scroll.h"

#include <stdio.h>
#include <string.h>

extern int g_failures;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

struct TestHostState
{
    int selected_tab;
    int cross_active;
    int minimenu_visible;
    int camera_yaw;
};

static inline int
UITree_TestHostRequest(void* user, struct UITreeHostRequest* req)
{
    struct TestHostState* st = (struct TestHostState*)user;
    if( !st || !req )
        return 0;

    switch( req->kind )
    {
    case UITREE_HOST_GET_SELECTED_TAB:
        return st->selected_tab;
    case UITREE_HOST_GET_CROSS_ACTIVE:
        return st->cross_active;
    case UITREE_HOST_GET_MINIMENU_VISIBLE:
        return st->minimenu_visible;
    case UITREE_HOST_GET_CAMERA_YAW:
        return st->camera_yaw;
    case UITREE_HOST_IS_ACTIVE:
        return 0;
    case UITREE_HOST_SET_SELECTED_TAB:
        st->selected_tab = req->u.set_selected_tab.tabno;
        return 0;
    default:
        return 0;
    }
}

static inline void
UITree_TestHostInit(struct UITreeHost* host, struct TestHostState* state)
{
    memset(state, 0, sizeof(*state));
    UITree_HostInit(host);
    host->user = state;
    host->request = UITree_TestHostRequest;
}

static inline int32_t
UITree_TestPushXy(
    struct UITree* tree,
    int32_t parent,
    enum UITreeComponentType type,
    int component_id,
    int x,
    int y,
    int w,
    int h)
{
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = type;
    spec.component_id = component_id;
    spec.x = x;
    spec.y = y;
    spec.width = w;
    spec.height = h;
    if( type == UIELEM_RS_RECT )
    {
        spec.u.rs_rect.color = 0xFF0000;
        spec.u.rs_rect.filled = 1;
    }
    return UITree_Push(tree, parent, &spec);
}

static inline void
UITree_TestResolve(struct UITree* tree)
{
    UITree_LayoutResolve(tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
}

/* Unit tests */
void test_dirty_marking(void);
void test_walk_topology(void);
void test_hover_input(void);
void test_layout_build(void);
void test_mutate_emit(void);
void test_apply_object_silhouette(void);
void test_drag_composite(void);
void test_scroll_hit(void);
void test_drag_scrolled(void);
void test_emit_icons(void);
void test_emit_golden(void);

#endif
