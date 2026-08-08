#ifndef UITREE_TEST_HARNESS_H
#define UITREE_TEST_HARNESS_H

#include "uitree.h"
#include "uitree_build.h"
#include "uitree_debug_overlay.h"
#include "uitree_emit.h"
#include "uitree_host.h"
#include "uitree_hover.h"
#include "uitree_input.h"
#include "uitree_layout.h"
#include "uitree_minimenu.h"
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
    int cross_atlas_frame;
    int cross_x;
    int cross_y;
    int minimenu_visible;
    /** When set, GET_MINIMENU_STATE returns it (visible flag comes from the
     * model itself; minimenu_visible only answers GET_MINIMENU_VISIBLE). */
    struct UIMinimenu const* minimenu_state;
    int camera_yaw;
    /** When set, GET_DEBUG_OVERLAY hands back its display list. NULL = the
     * normal case, no overlay, and the pass emits nothing. */
    struct ToriDbgUI const* debug_overlay;
    /* Optional stub slots for UITREE_HOST_GET_INV_SOURCE_SLOT (tests). */
    int inv_source_id;
    struct UIInvSlotData inv_slots[UI_INV_SLOT_OFFSET_MAX];
    uint8_t inv_slot_valid[UI_INV_SLOT_OFFSET_MAX];
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
    case UITREE_HOST_GET_MINIMENU_STATE:
        if( !st->minimenu_state || !req->u.get_minimenu_state.out )
            return 0;
        *req->u.get_minimenu_state.out = st->minimenu_state;
        return 1;
    case UITREE_HOST_GET_CROSS_ATLAS_FRAME:
        return st->cross_atlas_frame;
    case UITREE_HOST_GET_CROSS_POSITION:
        if( req->u.get_cross_position.out_x )
            *req->u.get_cross_position.out_x = st->cross_x;
        if( req->u.get_cross_position.out_y )
            *req->u.get_cross_position.out_y = st->cross_y;
        return 1;
    case UITREE_HOST_GET_CAMERA_YAW:
        return st->camera_yaw;
    case UITREE_HOST_GET_DEBUG_OVERLAY:
    {
        int count = 0;
        struct ToriDbgPrim const* prims;
        if( !st->debug_overlay || !req->u.get_debug_overlay.out_prims )
            return 0;
        prims = ToriDbgUI_Prims(st->debug_overlay, &count);
        *req->u.get_debug_overlay.out_prims = prims;
        return count;
    }
    case UITREE_HOST_IS_ACTIVE:
        return 0;
    case UITREE_HOST_SET_SELECTED_TAB:
        st->selected_tab = req->u.set_selected_tab.tabno;
        return 0;
    case UITREE_HOST_GET_INV_SOURCE_SLOT:
    {
        int slot = req->u.get_inv_source_slot.slot;
        if( !req->u.get_inv_source_slot.out )
            return 0;
        if( req->u.get_inv_source_slot.source_id != st->inv_source_id )
            return 0;
        if( slot < 0 || slot >= UI_INV_SLOT_OFFSET_MAX || !st->inv_slot_valid[slot] )
            return 0;
        *req->u.get_inv_source_slot.out = st->inv_slots[slot];
        return 1;
    }
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
void test_click_event_coords(void);
void test_layout_build(void);
void test_mutate_emit(void);
void test_apply_object_silhouette(void);
void test_drag_composite(void);
void test_drag_scrollbar_ondrag_held(void);
void test_drag_scrollbar_inplace_emit(void);
void test_drag_scrollbar_137_geometry(void);
void test_drag_cc_dragpickup_seeds(void);
void test_scroll_hit(void);
void test_drag_scrolled(void);
void test_emit_icons(void);
void test_emit_golden(void);
void test_minimenu(void);
void test_key_dispatch(void);
void test_id_index(void);
void test_child_subid(void);
void test_menu_submenus(void);
void test_component_params(void);
void test_open_close_steady(void);
void test_clear_hooks_preserves_sibling_on_op(void);
void test_chatmodal_reclaim_no_shadow_text(void);
void test_live_node_sets(void);
void test_debug_overlay(void);

#endif
