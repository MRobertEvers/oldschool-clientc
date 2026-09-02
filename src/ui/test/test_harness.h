#ifndef UITREE_TEST_HARNESS_H
#define UITREE_TEST_HARNESS_H

#include "uitree.h"
#include <assert.h>
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

void test_roles(void);

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
    struct ToriRSChrome const* debug_overlay;
    /** When set, GET_ENTITY_OVERLAYS hands back this list and reports the
     * world rect below as the scene clip. NULL = no entity overlays, and the
     * builtin emits nothing. */
    struct UITreeEntityOverlay const* entity_overlays;
    int entity_overlay_count;
    struct UITreeEntityOverlay const* canvas_overlays;
    int canvas_overlay_count;
    struct UITreeEntityOverlay const* frame_overlays;
    int frame_overlay_count;
    int request_count[UITREE_HOST_REQUEST_COUNT];
    int entity_overlay_clip_x;
    int entity_overlay_clip_y;
    int entity_overlay_clip_w;
    int entity_overlay_clip_h;
    struct UITreeRoleOverlayGroup const* role_overlay_groups;
    int role_overlay_group_count;
    int role_anchor_seen;
    int role_clip_updates;
    int32_t role_clip_node;
    uint32_t role_clip_incarnation;
    struct UITreeScrollClip role_clip;
    /** When >= 0, GET_OBJ_NAME answers for that obj id and reports it as a bank
     *  placeholder — the one fact that suppresses an item cell's count text. */
    int placeholder_obj_id;
    /* Server-driven viewport state: MINIMAP_TOGGLE, SET_MULTIWAY and the
     * UPDATE_REBOOT_TIMER line. All three default off, which is what a client
     * with no session is actually in. */
    int minimap_hidden;
    /** Scene id GET_MINIMAP_STATE answers with; 0 leaves it at "no baked map",
     *  which the emit treats the same as any other not-ready asset. */
    int minimap_scene_id;
    int multiway;
    char const* reboot_timer_text;
    /* Optional stub slots for UITREE_HOST_GET_INV_SOURCE_SLOT (tests). */
    int inv_source_id;
    struct UIInvSlotData inv_slots[UI_INV_SLOT_OFFSET_MAX];
    uint8_t inv_slot_valid[UI_INV_SLOT_OFFSET_MAX];
};

static inline int
UITree_TestHostRequest(void* user, struct UITreeHostRequest* req)
{
    struct TestHostState* st = (struct TestHostState*)user;
    if( !st )
        return 0;
    assert(req);
    if( req->kind >= 0 && req->kind < UITREE_HOST_REQUEST_COUNT )
        st->request_count[req->kind]++;

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
        struct ToriRSChromePrim const* prims;
        if( !st->debug_overlay || !req->u.get_debug_overlay.out_prims )
            return 0;
        prims = ToriRSChrome_Prims(st->debug_overlay, &count);
        *req->u.get_debug_overlay.out_prims = prims;
        return count;
    }
    case UITREE_HOST_GET_ENTITY_OVERLAYS:
        if( !st->entity_overlays || !req->u.get_entity_overlays.out_items )
            return 0;
        *req->u.get_entity_overlays.out_items = st->entity_overlays;
        *req->u.get_entity_overlays.out_clip_x = st->entity_overlay_clip_x;
        *req->u.get_entity_overlays.out_clip_y = st->entity_overlay_clip_y;
        *req->u.get_entity_overlays.out_clip_w = st->entity_overlay_clip_w;
        *req->u.get_entity_overlays.out_clip_h = st->entity_overlay_clip_h;
        return st->entity_overlay_count;
    case UITREE_HOST_GET_ROLE_OVERLAY_GROUPS:
        if( req->u.get_role_overlay_groups.out_groups )
            *req->u.get_role_overlay_groups.out_groups = st->role_overlay_groups;
        if( req->u.get_role_overlay_groups.out_anchor_seen )
            *req->u.get_role_overlay_groups.out_anchor_seen = st->role_anchor_seen;
        return st->role_overlay_group_count;
    case UITREE_HOST_SET_ROLE_OVERLAY_CLIP:
        st->role_clip_updates++;
        st->role_clip_node = req->u.set_role_overlay_clip.node_index;
        st->role_clip_incarnation = req->u.set_role_overlay_clip.node_incarnation;
        st->role_clip.clip_x = req->u.set_role_overlay_clip.clip_x;
        st->role_clip.clip_y = req->u.set_role_overlay_clip.clip_y;
        st->role_clip.clip_w = req->u.set_role_overlay_clip.clip_w;
        st->role_clip.clip_h = req->u.set_role_overlay_clip.clip_h;
        return 1;
    case UITREE_HOST_GET_CANVAS_OVERLAYS:
        if( !st->canvas_overlays || !req->u.get_entity_overlays.out_items )
            return 0;
        *req->u.get_entity_overlays.out_items = st->canvas_overlays;
        return st->canvas_overlay_count;
    case UITREE_HOST_GET_FRAME_OVERLAYS:
        if( !st->frame_overlays || !req->u.get_entity_overlays.out_items )
            return 0;
        *req->u.get_entity_overlays.out_items = st->frame_overlays;
        return st->frame_overlay_count;
    case UITREE_HOST_GET_MINIMAP_HIDDEN:
        return st->minimap_hidden;
    case UITREE_HOST_GET_MINIMAP_STATE:
        if( st->minimap_scene_id <= 0 )
            return -1;
        if( req->u.get_minimap_state.out_src_anchor_x )
            *req->u.get_minimap_state.out_src_anchor_x = 0;
        if( req->u.get_minimap_state.out_src_anchor_y )
            *req->u.get_minimap_state.out_src_anchor_y = 0;
        return st->minimap_scene_id;
    case UITREE_HOST_GET_MULTIWAY:
        return st->multiway;
    case UITREE_HOST_GET_REBOOT_TIMER:
        if( !st->reboot_timer_text || !req->u.get_reboot_timer.out_text )
            return 0;
        *req->u.get_reboot_timer.out_text = st->reboot_timer_text;
        return 1;
    /* A harness tree is not on the title screen: -1 is the honest answer, and
     * it is what makes every title widget draw nothing here. */
    case UITREE_HOST_GET_TITLE_SCREEN:
        return -1;
    case UITREE_HOST_GET_TITLE_FIELD:
    case UITREE_HOST_GET_TITLE_MESSAGE:
    case UITREE_HOST_GET_TITLE_PROGRESS:
    case UITREE_HOST_TITLE_ACTION:
        return 0;
    case UITREE_HOST_IS_ACTIVE:
        return 0;
    case UITREE_HOST_SET_SELECTED_TAB:
        st->selected_tab = req->u.set_selected_tab.tabno;
        return 0;
    case UITREE_HOST_GET_OBJ_NAME:
        /* Only the placeholder fixture answers; every other test leaves
         * placeholder_obj_id at 0 from the memset and gets the unknown-obj
         * answer this host gave before. */
        if( st->placeholder_obj_id <= 0 ||
            req->u.get_obj_name.obj_id != st->placeholder_obj_id )
            return 0;
        if( req->u.get_obj_name.out && req->u.get_obj_name.cap > 0 )
            req->u.get_obj_name.out[0] = '\0';
        if( req->u.get_obj_name.out_stackable )
            *req->u.get_obj_name.out_stackable = 0;
        if( req->u.get_obj_name.out_placeholder )
            *req->u.get_obj_name.out_placeholder = 1;
        return 1;
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
void test_scripted_entity_overlay(void);
void test_scripted_entity_overlay_clipped(void);
void test_scripted_overlay_arc(void);
void test_dirty_marking(void);
void test_walk_topology(void);
void test_mounted_world_resize(void);
void test_hover_input(void);
void test_click_event_coords(void);
void test_pointer_owner_blocks_tree(void);
void test_layout_build(void);
void test_mutate_emit(void);
void test_apply_object_silhouette(void);
void test_drag_composite(void);
void test_drag_scrollbar_ondrag_held(void);
void test_drag_scrollbar_inplace_emit(void);
void test_drag_scrollbar_137_geometry(void);
void test_drag_cc_dragpickup_seeds(void);
void test_press_repeat_and_release(void);
void test_frame_hidden_cancels_active_input(void);
void test_scroll_hit(void);
void test_wheel_stops_at_interface(void);
void test_drag_scrolled(void);
void test_emit_icons(void);
void test_emit_stack_count_zero(void);
void test_emit_stack_count_placeholder(void);
void test_emit_golden(void);
void test_minimenu(void);
void test_key_dispatch(void);
void test_same_frame_press_release_clicks(void);
void test_touch_swipe_scrolls_layer(void);
void test_feedback_overlay_never_takes_a_click(void);
void test_id_index(void);
void test_child_subid(void);
void test_menu_submenus(void);
void test_component_params(void);
void test_inkwell_spec_copy(void);
void test_open_close_steady(void);
void test_mounted_component_inherits_container_hidden(void);
void test_clear_hooks_preserves_sibling_on_op(void);
void test_mount_slot_reclaim_no_shadow_text(void);
void test_live_node_sets(void);
void test_debug_overlay(void);
void test_chrome_exec(void);
void test_entity_overlay_draw_order(void);
void test_server_driven_viewport_widgets(void);
void test_frame_replacement(void);

#endif
