/*
 * The UITree host request switch: everything the retained tree asks the client for during an emit walk.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

int
app_host_request(
    void* user,
    struct UITreeHostRequest* req)
{
    struct App* app = (struct App*)user;
    struct InvSlot slot;

    assert(req);
    assert(app);

    switch( req->kind )
    {
    case UITREE_HOST_BEGIN_OVERLAYS:
        /* A retained refresh which discovers its first semantic anchor is
         * abandoned in favour of a full walk. Both passes issue BEGIN in the
         * same App_RunOnce; preserve the already-built Canvas list across the
         * fallback so plugin callbacks and their per-frame draw budget run
         * exactly once. App_RunOnce clears this latch for the next frame. */
        if( !app->overlays.batch_started )
        {
            app->overlays.batch_started = 1;
            app->overlays.canvas_prepared = 0;
        }
        return 0;
    case UITREE_HOST_GET_SCROLLBAR_SCENE:
        return UITreeSceneBridge_ScrollbarSceneId(&app->bridge);
    case UITREE_HOST_GET_INKWELL:
    {
        /*
         * The component supplies the artwork it was configured with and the
         * app supplies the marker's live state; neither knows the other's
         * half. -1 from the profile means "unstated", and the defaults here
         * are the reference client's convention: yellow walks, red interacts.
         */
        int const style =
            req->u.get_inkwell.style >= 0 ? req->u.get_inkwell.style : TORIRS_INKWELL_SPLASH;
        int const walk = req->u.get_inkwell.walk_color >= 0 ? req->u.get_inkwell.walk_color
                                                            : TORIRS_INKWELL_YELLOW;
        int const interact = req->u.get_inkwell.interact_color >= 0
                                 ? req->u.get_inkwell.interact_color
                                 : TORIRS_INKWELL_RED;
        int colour;

        if( !UIInk_IsActive(&app->ink) )
            return 0;
        colour = app->ink.colour == TORIRS_INKWELL_RED ? interact : walk;
        if( req->u.get_inkwell.out_x )
            *req->u.get_inkwell.out_x = app->ink.x;
        if( req->u.get_inkwell.out_y )
            *req->u.get_inkwell.out_y = app->ink.y;
        if( req->u.get_inkwell.out_atlas_index )
            *req->u.get_inkwell.out_atlas_index =
                ToriRSInkwell_AtlasIndex(style, colour, UIInk_Frame(&app->ink));
        return 1;
    }
    case UITREE_HOST_GET_INKWELL_SCENE:
        return UITreeSceneBridge_EnsureInkwell(&app->bridge);
    case UITREE_HOST_GET_STATIC_SPRITE_SCENE:
        return UITreeSceneBridge_StaticSpriteSceneId(
            &app->bridge, (enum StaticSpriteSlot)req->u.static_sprite.slot);
    case UITREE_HOST_GET_ENTITY_OVERLAYS:
        *req->u.get_entity_overlays.out_clip_x = app->world_emit_desc.x;
        *req->u.get_entity_overlays.out_clip_y = app->world_emit_desc.y;
        *req->u.get_entity_overlays.out_clip_w = app->world_emit_desc.w;
        *req->u.get_entity_overlays.out_clip_h = app->world_emit_desc.h;
        return app_build_entity_overlays(app, req->u.get_entity_overlays.out_items);
    case UITREE_HOST_GET_CANVAS_OVERLAYS:
        /* The canvas, not the world viewport -- that difference IS this
         * surface. Built here rather than beside the world list because the
         * two are asked for at different points of the emit walk, and the
         * plugin drawing into either has to see the same frame's state. */
        *req->u.get_entity_overlays.out_clip_x = 0;
        *req->u.get_entity_overlays.out_clip_y = 0;
        *req->u.get_entity_overlays.out_clip_w = UITREE_LAYOUT_ROOT_W;
        *req->u.get_entity_overlays.out_clip_h = UITREE_LAYOUT_ROOT_H;
        return app_build_canvas_overlays(app, req->u.get_entity_overlays.out_items);
    case UITREE_HOST_GET_CROSS_ACTIVE:
        return UICross_IsActive(&app->cross) ? 1 : 0;
    case UITREE_HOST_GET_CROSS_ATLAS_FRAME:
        return UICross_AtlasFrame(&app->cross);
    case UITREE_HOST_GET_CROSS_POSITION:
        if( req->u.get_cross_position.out_x )
            *req->u.get_cross_position.out_x = app->cross.x;
        if( req->u.get_cross_position.out_y )
            *req->u.get_cross_position.out_y = app->cross.y;
        return 1;
    case UITREE_HOST_GET_MINIMENU_VISIBLE:
        return app->interact.minimenu.visible ? 1 : 0;
    case UITREE_HOST_GET_MINIMENU_STATE:
        assert(req->u.get_minimenu_state.out);
        *req->u.get_minimenu_state.out = &app->interact.minimenu;
        return 1;
    case UITREE_HOST_GET_HOVERTEXT_STATE:
        assert(req->u.get_hovertext_state.out);
        *req->u.get_hovertext_state.out = &app->hover_text;
        return 1;
    case UITREE_HOST_MEASURE_TEXT:
    {
        struct ToriDraw_Font* font = ToriDraw_SceneFontGet(app->scene, req->u.measure_text.font_id);
        if( !font || !req->u.measure_text.text )
            return 0;
        return ToriDraw2D_MeasureString(font, req->u.measure_text.text);
    }
    /* Compass/minimap rotation, in the 0..2047 units the rotated sprite blit
     * takes. Normalized because ToriDraw_Sin/Cos assert that range. */
    case UITREE_HOST_GET_CAMERA_YAW:
        return ToriDraw_NormalizeAngle(app->world_camera.yaw);
    /* Minimap: the baked world map plus the camera's pivot inside it. The
     * widget box is fixed, so the map scrolls by moving this source anchor. */
    case UITREE_HOST_GET_MINIMAP_STATE:
    {
        /* Reference centers the minimap on the local player (minimapDraw
         * anchors at player.x/32), not the orbit eye; free-cam (offline)
         * keeps the eye anchor. Aboard, the player's own coordinates are
         * deck-local — pan by their position pushed out through the hull,
         * the same frame the dots and their centre use. */
        struct WorldEntity_Player* local_player = app_local_player(app);
        int anchor_x = local_player ? (int)local_player->draw_position.x : app->world_camera_pos.x;
        int anchor_z = local_player ? (int)local_player->draw_position.z : app->world_camera_pos.z;
        if( local_player )
            app_wev_actor_root_fine(app, &local_player->view_placement, &anchor_x, &anchor_z);
        if( app->world_map_scene_id <= 0 || !app->world || !app->world->minimap )
            return -1;
        minimap_compute_camera_src_anchor(
            anchor_x,
            anchor_z,
            app->world_map_w,
            app->world_map_h,
            app->world->minimap->width,
            app->world->minimap->height,
            req->u.get_minimap_state.out_src_anchor_x,
            req->u.get_minimap_state.out_src_anchor_y);
        return app->world_map_scene_id;
    }
    case UITREE_HOST_GET_MINIMAP_HIDDEN:
        return !(RS_MinimapPermissions(app->minimap_state) & RS_MINIMAP_DRAW_MAP);
    case UITREE_HOST_GET_COMPASS_HIDDEN:
        return !(RS_MinimapPermissions(app->minimap_state) & RS_MINIMAP_DRAW_COMPASS);
    case UITREE_HOST_GET_MULTIWAY:
        return app->multiway == 1;
    case UITREE_HOST_GET_REBOOT_TIMER:
        assert(req->u.get_reboot_timer.out_text);
        *req->u.get_reboot_timer.out_text = app_reboot_timer_text(app);
        return *req->u.get_reboot_timer.out_text != NULL;
    case UITREE_HOST_GET_TITLE_SCREEN:
        /* -1 rather than 0: 0 is a real screen (the front menu), so "not on
         * the title screen at all" needs a value of its own. */
        if( app->screen != APP_SCREEN_TITLE && app->screen != APP_SCREEN_CONNECTING )
            return -1;
        return (int)app->title.screen;
    case UITREE_HOST_GET_TITLE_FIELD:
        assert(req->u.get_title_field.config);
        assert(req->u.get_title_field.out_text);
        return app_title_field_line(app, req);
    case UITREE_HOST_GET_TITLE_MESSAGE:
    {
        int index = req->u.get_title_message.index;
        assert(req->u.get_title_message.out_text);
        if( index < 0 || index >= RS_TITLE_MESSAGE_LINES )
            return 0;
        *req->u.get_title_message.out_text = app->title.messages[index];
        return app->title.messages[index][0] != '\0';
    }
    case UITREE_HOST_GET_TITLE_TOGGLE:
    {
        int toggle = req->u.get_title_toggle.toggle;
        if( toggle < 0 || toggle >= RS_TITLE_TOGGLE_COUNT )
            return 0;
        return app->title.toggles[toggle];
    }
    case UITREE_HOST_GET_TITLE_PROGRESS:
        assert(req->u.get_title_progress.out_percent);
        assert(req->u.get_title_progress.out_text);
        *req->u.get_title_progress.out_percent = app->title.progress_percent;
        *req->u.get_title_progress.out_text = app->title.progress_text;
        return app->title.progress_percent >= 0;
    case UITREE_HOST_GET_TITLE_FLAMES:
    {
        int side = req->u.get_title_flames.side;
        struct TitleFlameGeometry geometry;
        assert(req->u.get_title_flames.out_scene_id);
        if( !app->flames || side < 0 || side >= TORIRS_FLAME_SIDES )
            return 0;

        /*
         * Where this era leans its fire, restated every frame because the
         * node is the only thing that knows and the simulation is shared.
         * Cheap -- four ints -- and it keeps the numbers in the profile
         * where the two revisions disagree about them.
         */
        geometry.bias = req->u.get_title_flames.bias;
        geometry.sway = req->u.get_title_flames.sway;
        geometry.run = req->u.get_title_flames.run;
        geometry.row = req->u.get_title_flames.row;
        TitleFlames_SetGeometry(app->flames, (enum TitleFlameSide)side, &geometry);
        /* Shared by both braziers -- the simulation is one field -- so the
         * two nodes state the same value and either may set it. */
        TitleFlames_SetBlur(app->flames, req->u.get_title_flames.blur);
        *req->u.get_title_flames.out_scene_id = side == TORIRS_FLAME_LEFT
                                                    ? UITREE_SCENE_TITLE_FLAME_LEFT_ID
                                                    : UITREE_SCENE_TITLE_FLAME_RIGHT_ID;
        return ToriDraw_SceneSpriteHas(app->scene, *req->u.get_title_flames.out_scene_id);
    }
    case UITREE_HOST_TITLE_ACTION:
    {
        enum RS_TitleAction action = (enum RS_TitleAction)req->u.title_action.action;
        /*
         * A tap on a field re-asks for the soft keyboard even when the focus
         * did not move -- and it usually has not, because the form always has
         * a focused field. The keyboard request is edge-triggered
         * (App_TakeTextInputChange pushes only changes), so after the player
         * hides the keyboard, "wanted" never changes and no tap could bring
         * it back. Forgetting what was last pushed makes the next take push
         * the current answer again; on a desktop that re-push is a no-op.
         */
        if( action == RS_TITLE_ACTION_FOCUS_USERNAME || action == RS_TITLE_ACTION_FOCUS_PASSWORD )
            app->text_input_effective = -1;
        if( RS_Title_HandleAction(&app->title, action) )
        {
            /* The form is greeted with a prompt rather than an empty box, and
             * the words are the profile's -- this era invites an email as
             * well as a display name, the 2004 one does not. A login reply
             * replaces it afterwards, which is the reference's behaviour and
             * why this is a message line rather than a static label. */
            if( action == RS_TITLE_ACTION_EXISTING_USER )
                RS_Title_SetMessages(
                    &app->title,
                    RS_LoginReplies_String(&app->login_replies, "enter_credentials"),
                    NULL,
                    NULL);
            app_title_state_changed(app);
        }
        return 0;
    }
    case UITREE_HOST_GET_MINIMAP_DOTS:
        return App_MinimapBuildDots(app, req->u.get_minimap_dots.out_dots);
    case UITREE_HOST_GET_WORLDMAP_TILES:
        return app_worldmap_build_tiles(app, req);
    case UITREE_HOST_GET_WORLDMAP_OVERVIEW:
        return app_worldmap_build_overview(app, req);
    case UITREE_HOST_GET_INV_SOURCE_SLOT:
        assert(req->u.get_inv_source_slot.out);
        if( !InvManager_GetSlot(
                &app->invs,
                req->u.get_inv_source_slot.source_id,
                req->u.get_inv_source_slot.slot,
                &slot) )
            return 0;
        req->u.get_inv_source_slot.out->obj_id = slot.obj_id;
        req->u.get_inv_source_slot.out->obj_count = slot.obj_count;
        req->u.get_inv_source_slot.out->scene_id = slot.scene_id;
        req->u.get_inv_source_slot.out->atlas_index = slot.atlas_index;
        return 1;
    /* CS1 answers come from the per-tick evaluation cached on each node, so
     * drawing never runs the VM and never has to handle a mid-frame yield. */
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
    /* Tab + privacy-bar state lives on RS_UISlots (reference sideTab /
     * sideOverlayId / chat*Mode). A side modal suppresses the tab subtree by
     * answering -1, which no sidebar tabno matches. */
    case UITREE_HOST_GET_SELECTED_TAB:
        if( app->slots.side_modal_id != -1 )
            return -1;
        return app->slots.side_tab;
    case UITREE_HOST_SET_SELECTED_TAB:
        if( app->slots.side_tab != req->u.set_selected_tab.tabno )
        {
            app->slots.side_tab = req->u.set_selected_tab.tabno;
            app->need_redraw = 1;
        }
        /* Opening the tab the tutorial was pointing at is the instruction being
         * followed, so the blink stops. The reference notices this while
         * drawing the sidebar; noticing it at the selection itself is the same
         * rule asked once instead of every frame. */
        if( app->slots.flash_tab == req->u.set_selected_tab.tabno )
            app->slots.flash_tab = -1;
        return 1;
    case UITREE_HOST_GET_TAB_ENABLED:
        return RS_UISlots_TabEnabled(&app->slots, req->u.tab_enabled.tabno);
    case UITREE_HOST_GET_TAB_FLASH_HIDDEN:
        /* logic_cycle is the reference loopCycle, and the 20/10 split is its
         * own: visible for ten client ticks, hidden for ten. */
        return RS_UISlots_TabFlashHidden(&app->slots, req->u.tab_enabled.tabno, app->logic_cycle);
    case UITREE_HOST_GET_CHAT_FILTER_MODE:
        if( req->u.chat_filter.filter < 0 || req->u.chat_filter.filter >= RS_UI_CHAT_FILTER_COUNT )
            return 0;
        return app->slots.chat_filter_mode[req->u.chat_filter.filter];
    case UITREE_HOST_CYCLE_CHAT_FILTER_MODE:
        app->need_redraw = 1;
        return RS_UISlots_CycleChatFilter(&app->slots, req->u.chat_filter.filter);
    case UITREE_HOST_APPLY_BUTTON_CLICK:
        if( !req->u.apply_button_click.component )
            return 0;
        return RS_IF1_ApplyButtonClick(
            app,
            req->u.apply_button_click.component->component_id,
            RS_Minimenu_IfButtonActionForType(
                req->u.apply_button_click.component->behavior.button_type));
    case UITREE_HOST_GET_CHAT_STATE:
        assert(req->u.get_chat_state.out);
        *req->u.get_chat_state.out = &app->chat_view;
        return 1;
    case UITREE_HOST_GET_OBJ_NAME:
    {
        struct ToriRS_Objtype const* obj =
            CacheProvider_ObjtypeGet(app->provider, req->u.get_obj_name.obj_id);
        if( !obj || !req->u.get_obj_name.out || req->u.get_obj_name.cap <= 0 )
            return 0;
        strncpy(req->u.get_obj_name.out, obj->name, (size_t)req->u.get_obj_name.cap - 1);
        req->u.get_obj_name.out[req->u.get_obj_name.cap - 1] = '\0';
        if( req->u.get_obj_name.out_stackable )
            *req->u.get_obj_name.out_stackable = obj->stackable ? 1 : 0;
        /* Same two fields `oc_placeholder` reads (rs_cs2_host.c): a bank
         * placeholder is the record that carries a template. */
        if( req->u.get_obj_name.out_placeholder )
            *req->u.get_obj_name.out_placeholder = obj->placeholder_template >= 0 ? 1 : 0;
        return 1;
    }
    case UITREE_HOST_GET_INV_DRAG:
        /* Reports the slot only while it should ghost (trans 128), which is
         * from the press that armed it (reference Client.ts:8589 / :10207). */
        if( !app_inv_drag_ghosting(app) )
            return 0;
        if( req->u.get_inv_drag.out_source_id )
            *req->u.get_inv_drag.out_source_id = app->inv_drag.source_id;
        if( req->u.get_inv_drag.out_slot )
            *req->u.get_inv_drag.out_slot = app->inv_drag.from_slot;
        if( req->u.get_inv_drag.out_dx )
            *req->u.get_inv_drag.out_dx = app->inv_drag.dx;
        if( req->u.get_inv_drag.out_dy )
            *req->u.get_inv_drag.out_dy = app->inv_drag.dy;
        if( req->u.get_inv_drag.out_component_id )
            *req->u.get_inv_drag.out_component_id = app->inv_drag.component_id;
        return 1;
    case UITREE_HOST_GET_INV_COUNT_FONT:
        /* Reference draws stack counts with the client's p11 — same font (and
         * same load-on-miss self-heal) as the hitsplat numbers. */
        return app_hitsplat_font_scene_id(app);
    case UITREE_HOST_GET_INV_SELECTION:
        /* The armed (component, slot), for a cell that cannot name its own
         * addressing — a CS2 `cc_create`d item child, whose protocol identity
         * is its static parent's uid plus its index. Same shape as
         * GET_INV_DRAG: report the identity, let emit match the node. */
        if( !app->objsel.active )
            return 0;
        if( req->u.get_inv_selection.out_component_id )
            *req->u.get_inv_selection.out_component_id = app->objsel.component_id;
        if( req->u.get_inv_selection.out_slot )
            *req->u.get_inv_selection.out_slot = app->objsel.slot;
        return 1;
    case UITREE_HOST_GET_INV_SELECT_ICON:
        /* Reference TYPE_INV draw: only the slot armed for "Use" (useMode==1,
         * matching objSelectedSlot + objSelectedComId) gets the white outline.
         * The model is already resident (its plain icon is on screen), so the
         * white variant bakes on first request and is cached thereafter. */
        if( !app->objsel.active )
            return 0;
        if( app->objsel.component_id != req->u.get_inv_select_icon.com_id )
            return 0;
        if( app->objsel.slot != req->u.get_inv_select_icon.slot )
            return 0;
        return UITreeSceneBridge_EnsureObjIconSelected(
            &app->bridge,
            req->u.get_inv_select_icon.obj_id,
            req->u.get_inv_select_icon.count > 0 ? req->u.get_inv_select_icon.count : 1);
    case UITREE_HOST_GET_OBJ_ICON_PLAIN:
        return UITreeSceneBridge_EnsureObjIconPlain(
            &app->bridge,
            req->u.get_obj_icon_plain.obj_id,
            req->u.get_obj_icon_plain.count > 0 ? req->u.get_obj_icon_plain.count : 1);
    case UITREE_HOST_GET_OBJ_ICON_BORDERED:
        return UITreeSceneBridge_EnsureObjIconBordered(
            &app->bridge,
            req->u.get_obj_icon_bordered.obj_id,
            req->u.get_obj_icon_bordered.count > 0 ? req->u.get_obj_icon_bordered.count : 1);
    case UITREE_HOST_GET_IF_EVENTS:
        return (int)App_IfEventsGetEffective(app, req->u.get_if_events.com_id);
    /* The developer overlay's display list, handed over by pointer — the array
     * is owned by app->dbg_ui and outlives the frame. With the panel hidden the
     * list is empty and this returns 0, which is the whole cost of a declared
     * but switched-off overlay. */
    case UITREE_HOST_GET_DEBUG_OVERLAY:
    {
        int count = 0;
        if( !req->u.get_debug_overlay.out_prims )
            return 0;
        /*
         * Two chrome instances, one display list.
         *
         * The emit layer takes a single pointer-and-count for the whole
         * overlay, so the alternative to concatenating here would be a second
         * overlay NODE -- a second entry in the tree, a second pass, a second
         * z-order question to answer. Concatenation answers it instead: the
         * plugin window's prims go last and therefore draw on top, which is
         * what a window a player opened should do over a developer readout.
         *
         * The merge only runs when one of the two actually rebuilt; a steady
         * frame is still the one pointer copy the retained design promises.
         */
        *req->u.get_debug_overlay.out_prims = app_chrome_merged_prims(app, &count);
        return count;
    }
    default:
        return 0;
    }
}
