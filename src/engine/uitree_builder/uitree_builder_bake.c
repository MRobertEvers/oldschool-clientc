#include "uitree_builder_bake.h"

#include "uitree_builder.h"
#include "uitree_builder_inv.h"
#include "uitree_builder_manifest.h"

#include "engine/cache_provider.h"
#include "engine/torirs_component_hook.h"
#include "engine/torirs_types.h"
#include "engine/uitree_from_component.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_cs2_host.h"
#include "input/torirs_keymap.h"
#include "inv/inv_manager.h"
#include "ui/uitree.h"
#include "ui/uitree_build.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static enum UITreeComponentType
type_from_string(char const* type)
{
    assert(type);
    if( strcmp(type, "compass") == 0 )
        return UIELEM_BUILTIN_COMPASS;
    if( strcmp(type, "cross") == 0 )
        return UIELEM_BUILTIN_CROSS;
    if( strcmp(type, "minimenu") == 0 )
        return UIELEM_BUILTIN_MINIMENU;
    if( strcmp(type, "minimap") == 0 )
        return UIELEM_BUILTIN_MINIMAP;
    if( strcmp(type, "world") == 0 )
        return UIELEM_BUILTIN_WORLD;
    if( strcmp(type, "sidebar") == 0 )
        return UIELEM_BUILTIN_SIDEBAR;
    if( strcmp(type, "chat") == 0 )
        return UIELEM_BUILTIN_CHAT;
    if( strcmp(type, "chat_button") == 0 )
        return UIELEM_BUILTIN_CHAT_BUTTON;
    if( strcmp(type, "sprite") == 0 )
        return UIELEM_BUILTIN_SPRITE;
    if( strcmp(type, "redstone_tab") == 0 )
        return UIELEM_BUILTIN_REDSTONE_TAB;
    if( strcmp(type, "builtin_tab_icons") == 0 || strcmp(type, "tab_icon") == 0 )
        return UIELEM_BUILTIN_TAB_ICONS;
    if( strcmp(type, "debug_overlay") == 0 )
        return UIELEM_BUILTIN_DEBUG_OVERLAY;
    /*
     * `rs_iface` is a mount point, not a widget: the node itself draws nothing
     * and the cache pack is baked underneath it (bake_rs_subtree_for_op). A
     * plain container layer is exactly that — uitree_emit skips a layer with no
     * if3 and no scroll extents, and it does not eat clicks.
     */
    if( strcmp(type, "rs_iface") == 0 )
        return UIELEM_RS_LAYER;
    if( strcmp(type, "rs_model") == 0 )
        return UIELEM_RS_MODEL;
    if( strcmp(type, "rs_inv") == 0 )
        return UIELEM_RS_INV;
    if( strcmp(type, "rs_line") == 0 )
        return UIELEM_RS_LINE;
    if( strcmp(type, "rs_graphic") == 0 )
        return UIELEM_RS_GRAPHIC;
    if( strcmp(type, "rs_layer") == 0 )
        return UIELEM_RS_LAYER;
    if( strcmp(type, "rs_text") == 0 )
        return UIELEM_RS_TEXT;
    if( strcmp(type, "rs_rect") == 0 )
        return UIELEM_RS_RECT;
    return UIELEM_BUILTIN_SPRITE;
}

static enum UITreeSlotTag
slot_tag_from_string(char const* slot)
{
    if( !slot || !slot[0] )
        return UITREE_SLOT_NONE;
    if( strcmp(slot, "main_modal") == 0 )
        return UITREE_SLOT_MAIN_MODAL;
    if( strcmp(slot, "main_overlay") == 0 )
        return UITREE_SLOT_MAIN_OVERLAY;
    if( strcmp(slot, "side_modal") == 0 )
        return UITREE_SLOT_SIDE_MODAL;
    if( strcmp(slot, "chat") == 0 )
        return UITREE_SLOT_CHAT;
    if( strcmp(slot, "tut") == 0 )
        return UITREE_SLOT_TUT;
    fprintf(stderr, "revconfig: unknown slot tag '%s'\n", slot);
    return UITREE_SLOT_NONE;
}

static void
apply_layout_position(
    struct UIBuilderTreeOp const* op,
    struct UITreeElemPosition* pos)
{
    assert(op && pos);
    memset(pos, 0, sizeof(*pos));

    if( op->left || op->right || op->top || op->bottom )
    {
        pos->kind = UIPOS_RELATIVE;
        pos->relative_flags = 0;
        if( op->left )
            pos->relative_flags |= UITREE_RELATIVE_FLAG_LEFT;
        if( op->right )
            pos->relative_flags |= UITREE_RELATIVE_FLAG_RIGHT;
        if( op->top )
            pos->relative_flags |= UITREE_RELATIVE_FLAG_TOP;
        if( op->bottom )
            pos->relative_flags |= UITREE_RELATIVE_FLAG_BOTTOM;
        pos->left = op->left;
        pos->right = op->right;
        pos->top = op->top;
        pos->bottom = op->bottom;
        pos->width = op->width;
        pos->height = op->height;
    }
    else
    {
        pos->kind = UIPOS_XY;
        pos->x = op->x;
        pos->y = op->y;
        pos->width = op->width;
        pos->height = op->height;
    }
    pos->anchor_x = op->anchor_x;
    pos->anchor_y = op->anchor_y;
}

static void
copy_menu_options(
    struct UIBuilderTreeOp const* op,
    struct UITreeMenuOptions* dst)
{
    assert(op && dst);
    memset(dst, 0, sizeof(*dst));
    strncpy(dst->option, op->option, sizeof(dst->option) - 1);
    dst->option_action = op->option_action;
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
    {
        strncpy(dst->ops[i], op->ops[i], sizeof(dst->ops[i]) - 1);
        dst->op_actions[i] = op->op_actions[i];
    }
}

static struct UIBuilderSpriteEntry const*
find_sprite_entry(
    struct UITreeBuilder const* builder,
    char const* ref)
{
    assert(builder && ref);
    char name[UITREE_BUILDER_NAME_MAX];
    char const* bracket = strchr(ref, '[');
    if( bracket )
    {
        size_t nlen = (size_t)(bracket - ref);
        if( nlen >= sizeof(name) )
            nlen = sizeof(name) - 1;
        memcpy(name, ref, nlen);
        name[nlen] = '\0';
    }
    else
    {
        strncpy(name, ref, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
    for( int i = 0; i < builder->sprite_count; i++ )
    {
        if( strcmp(builder->sprites[i].name, name) == 0 )
            return &builder->sprites[i];
    }
    return NULL;
}

static int
resolve_sprite_required(
    struct UITreeBuilder* builder,
    char const* ref,
    int* out_atlas)
{
    if( !ref || ref[0] == '\0' )
    {
        if( out_atlas )
            *out_atlas = 0;
        return -1;
    }
    if( !find_sprite_entry(builder, ref) )
    {
        fprintf(
            stderr,
            "uitree_builder_bake: missing sprite '%s' (registered=%d)\n",
            ref,
            builder->sprite_count);
        assert(0 && "required sprite name missing after assets load");
    }
    int atlas = 0;
    int id = UITreeBuilder_ResolveSpriteRef(builder, ref, &atlas);
    if( out_atlas )
        *out_atlas = atlas;
    if( builder->bridge && id >= 0 )
        id = UITreeSceneBridge_EnsureSprite(builder->bridge, id);
    return id;
}

static int
bake_resolve_sprite(void* ud, int graphic_id)
{
    struct UITreeBuilder* builder = (struct UITreeBuilder*)ud;
    assert(builder && builder->provider);
    if( graphic_id <= 0 )
        return -1;
    if( !CacheProvider_SpriteHas(builder->provider, graphic_id) )
    {
        /* Pack-internal sprites are not always prefetched yet — leave unbound. */
        fprintf(stderr, "uitree_builder_bake: RS sprite %d not loaded\n", graphic_id);
        return -1;
    }
    if( builder->bridge )
        return UITreeSceneBridge_EnsureSprite(builder->bridge, graphic_id);
    return graphic_id;
}

static int
bake_resolve_font(void* ud, int font_id)
{
    struct UITreeBuilder* builder = (struct UITreeBuilder*)ud;
    assert(builder);
    if( font_id < 0 )
        return -1;
    int resolved = UITreeBuilder_ResolveFontArchive(builder, font_id);
    /* Scene font ids equal cache font ids; Ensure is an upload side effect. */
    if( builder->bridge && resolved >= 0 )
        UITreeSceneBridge_EnsureFont(builder->bridge, resolved);
    return resolved;
}

static void
collect_onload(
    struct UITreeBuilder* builder,
    struct ToriRS_Component const* src)
{
    assert(builder && src);
    struct ToriRS_ScriptHook const* on_load =
        ToriRS_ComponentHookPeek(src, TORIRS_COMPONENT_HOOK_LOAD);
    if( !on_load || on_load->argc <= 0 )
        return;
    int script_id = on_load->argv[0];
    if( script_id <= 0 )
        return;
    {
        char const* strp[TORIRS_COMPONENT_HOOK_STR_MAX];
        for( int i = 0; i < TORIRS_COMPONENT_HOOK_STR_MAX; i++ )
            strp[i] = on_load->strv[i];
        UITreeBuilder_AddOnLoad(
            builder,
            src->id,
            script_id,
            on_load->argv,
            on_load->argc,
            on_load->str_mask,
            strp,
            on_load->str_argc);
    }
}

void
uitree_builder_bake_pack_under_owner(
    struct UITree* tree,
    struct UITreeBuilder* builder,
    struct ToriRS_ComponentPack const* pack,
    int32_t owner_idx,
    int inv_source_id)
{
    assert(tree && builder && pack);
    if( pack->component_count <= 0 )
        return;

    int32_t* index_map = calloc((size_t)pack->component_count, sizeof(int32_t));
    assert(index_map);
    for( int i = 0; i < pack->component_count; i++ )
        index_map[i] = -1;

    /* Pass 1: insert under owner so forward pack-internal parents can be resolved
     * after every index_map slot is filled. */
    for( int i = 0; i < pack->component_count; i++ )
    {
        struct ToriRS_Component const* src = &pack->components[i];
        struct UIBuildComponent build;
        UITree_FillBuildFromToriRS(&build, src);

        int32_t idx = UITree_PushBuildComponent(
            tree, owner_idx, &build, bake_resolve_sprite, bake_resolve_font, builder);
        assert(idx >= 0);
        index_map[i] = idx;

        if( inv_source_id >= 0 &&
            (build.type == UIBUILD_INV || build.type == UIBUILD_INV_TEXT) )
        {
            struct UITreeComponent* node = &tree->components[idx];
            if( node->type == UIELEM_RS_INV )
                node->u.rs_inv.inv_source_id = inv_source_id;
            else if( node->type == UIELEM_RS_INV_TEXT )
                node->u.rs_inv_text.inv_source_id = inv_source_id;
        }

        collect_onload(builder, src);
        /* The record's own transmit hooks, armed here rather than by a script.
         * They must exist before the builder's initial var/inv dispatch or the
         * mount paints from the onload and then goes deaf. */
        if( builder->host )
            RS_CS2_RegisterCacheTransmitHooks(builder->host, src);
    }

    /* Pass 2: reparent using full pack id scan (forward layer refs). */
    for( int i = 0; i < pack->component_count; i++ )
    {
        if( index_map[i] < 0 )
            continue;

        struct ToriRS_Component const* src = &pack->components[i];
        int32_t parent_idx = owner_idx;
        if( src->parent_id >= 0 )
        {
            parent_idx = -1;
            for( int j = 0; j < pack->component_count; j++ )
            {
                if( pack->components[j].id == src->parent_id )
                {
                    parent_idx = index_map[j];
                    break;
                }
            }
            if( parent_idx < 0 )
                parent_idx = UITree_FindByComponentId(tree, src->parent_id);
            if( parent_idx < 0 )
                parent_idx = owner_idx;
        }

        if( tree->components[index_map[i]].parent != parent_idx )
            UITree_Reparent(tree, index_map[i], parent_idx);
    }

    /* Pass 3: upload MODEL widgets into the scene (runestones, quest journal
     * scroll, combat spec bars). The pack-assets task loaded the model into
     * the provider; without this Ensure the frame translate finds no scene
     * model and silently drops the widget. Mirrors the font/sprite Ensure
     * side effects — task_interface_open's upload_model_nodes only covered
     * its own demo path. */
    if( builder->bridge )
    {
        for( int i = 0; i < pack->component_count; i++ )
        {
            struct UITreeComponent* node;
            int cache_id;
            int scene_id;
            if( index_map[i] < 0 )
                continue;
            node = &tree->components[index_map[i]];
            if( node->type != UIELEM_RS_MODEL )
                continue;
            cache_id = node->u.rs_model.gamecache_model_id;
            if( cache_id < 0 )
            {
                /* Local-player preview (client_code 327/328): composite the
                 * default avatar and pose it at readyanim frame 0. The
                 * reference (CC_DESIGN_PREVIEW) poses the composite once at
                 * SeqType.list[readyanim].frames[0] and never advances it —
                 * only modelYAn spins — so hold the frame. */
                if( node->behavior.client_code == 327 || node->behavior.client_code == 328 )
                {
                    scene_id = UITreeSceneBridge_EnsurePlayerModel(builder->bridge);
                    if( scene_id >= 0 )
                    {
                        node->u.rs_model.gamecache_model_id = scene_id;
                        if( node->u.rs_model.anim_seq_id < 0 )
                            node->u.rs_model.anim_seq_id = 808; /* human readyanim */
                        node->u.rs_model.anim_frame = 0;
                        node->u.rs_model.anim_frame_cycle = 0;
                        node->u.rs_model.anim_hold = 1;
                    }
                }
                continue;
            }
            scene_id = UITreeSceneBridge_EnsureModel(builder->bridge, cache_id);
            if( scene_id >= 0 )
                node->u.rs_model.gamecache_model_id = scene_id;
            else if( getenv("TORIRS_ANIM_DEBUG") )
                fprintf(
                    stderr,
                    "bake: model widget com=0x%x cache_id=%d not loadable\n",
                    (unsigned)node->component_id,
                    cache_id);
        }
    }

    /*
     * The pack's own handlers, which this path used to drop on the floor.
     *
     * `UITree_BuildFromComponentPack` bakes them and this function does not,
     * and the two are not interchangeable: every *sidebar tab* mounts through
     * here (`task_slot_mount.c`), so interface 182's "Click here to logout" and
     * "World Switcher" carried `onmouseover`/`onmouseleave` in the cache,
     * decoded them correctly, and arrived in the tree with no hooks at all —
     * the text never changed colour under the pointer. The combat tab's
     * buttons lost their `onmouserepeat` tooltips the same way.
     *
     * Last, not with the insert pass: hooks are resolved by component id, so
     * every node has to be in the tree first.
     */
    UITree_BakePackRuntimeHooks(tree, pack);

    free(index_map);
}

static int32_t
find_op_node(
    struct UIBuilderTreeOp const* ops,
    int32_t const* node_index,
    int op_count,
    char const* name)
{
    if( !name || name[0] == '\0' )
        return -1;
    for( int i = 0; i < op_count; i++ )
    {
        if( strcmp(ops[i].name, name) == 0 )
            return node_index[i];
    }
    return -1;
}

static int32_t
push_builtin_op(
    struct UITree* tree,
    struct UITreeBuilder* builder,
    struct UIBuilderTreeOp const* op,
    int32_t parent_index,
    struct InvManager* invs)
{
    assert(tree && builder && op);

    enum UITreeComponentType type = type_from_string(op->type);
    int is_iface_mount = strcmp(op->type, "rs_iface") == 0;
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = type;
    /*
     * An rs_iface owner keeps componentno as the group it mounts, not as its own
     * uid: the pack's own child 0 already owns (group << 16), and an unpacked
     * group id would collide with some other interface's uid in
     * UITree_FindByComponentId. -1 also keeps the owner out of the
     * unmounted-spillover sweep, which is what it is there for.
     */
    spec.component_id = is_iface_mount ? -1 : op->componentno;
    apply_layout_position(op, &spec.position);
    spec.has_position = 1;
    spec.slot_tag = (uint8_t)slot_tag_from_string(op->slot);
    if( op->dirty )
        spec.always_dirty = 1;
    copy_menu_options(op, &spec.menu_options);
    /* Advertised hotkey effects (revconfig hotkey= lines) are a property of the
     * component, not of its type, so they resolve here rather than in the type
     * switch. An unknown effect name asserts: a silently-ignored one leaves a
     * key that quietly does nothing, and the set is closed and hard-coded. */
    for( int i = 0; i < op->hotkey_count && i < REVCONFIG_COMPONENT_HOTKEY_MAX; i++ )
    {
        uint32_t effect = UITree_HotkeyEffectFromName(op->hotkeys[i]);
        assert(effect != 0 && "component hotkey= names an unknown effect");
        spec.hotkey_effects |= effect;
    }

    int sprite_id = -1;
    int atlas_index = 0;
    int sprite_active_id = -1;
    int atlas_active = 0;

    if( op->sprite_ref[0] )
        sprite_id = resolve_sprite_required(builder, op->sprite_ref, &atlas_index);
    if( op->sprite_active_ref[0] )
        sprite_active_id =
            resolve_sprite_required(builder, op->sprite_active_ref, &atlas_active);

    struct UITreeBehavior behavior;
    memset(&behavior, 0, sizeof(behavior));
    if( op->button_type != 0 || op->client_code != 0 )
    {
        behavior.button_type = op->button_type;
        behavior.client_code = op->client_code;
        spec.behavior = &behavior;
    }

    switch( type )
    {
    case UIELEM_BUILTIN_COMPASS:
    case UIELEM_BUILTIN_SPRITE:
        spec.u.sprite.scene_id = sprite_id;
        spec.u.sprite.atlas_index = atlas_index;
        break;
    case UIELEM_BUILTIN_CROSS:
        spec.always_dirty = 1;
        spec.u.sprite.scene_id = sprite_id;
        spec.u.sprite.atlas_index = 0;
        break;
    case UIELEM_BUILTIN_MINIMENU:
        spec.always_dirty = 1;
        if( op->has_font_ref && op->font_ref[0] )
        {
            int font_id = UITreeBuilder_ResolveFontName(builder, op->font_ref);
            assert(font_id >= 0 && "minimenu font missing");
            spec.u.minimenu.font_id = font_id;
        }
        else if( op->font >= 0 )
        {
            spec.u.minimenu.font_id = UITreeBuilder_ResolveFontArchive(builder, op->font);
        }
        break;
    case UIELEM_BUILTIN_MINIMAP:
        spec.always_dirty = 1;
        spec.u.minimap.scene_id = sprite_id;
        break;
    case UIELEM_BUILTIN_DEBUG_OVERLAY:
        /* The overlay's own dirty tracking decides what it repaints; the tree
         * only has to keep asking it, so the node never goes clean. Fonts are
         * baked in rather than named by the config: the overlay has to work on
         * a cache that failed to open, which is when it is most wanted. */
        spec.always_dirty = 1;
        spec.u.debug_overlay.font_id_small = -1;
        spec.u.debug_overlay.font_id_menu = -1;
        if( builder->bridge )
        {
            spec.u.debug_overlay.font_id_small =
                UITreeSceneBridge_EnsureDebugFont(builder->bridge, TORIDBG_FONT_SMALL);
            spec.u.debug_overlay.font_id_menu =
                UITreeSceneBridge_EnsureDebugFont(builder->bridge, TORIDBG_FONT_MENU);
        }
        break;
    case UIELEM_BUILTIN_CHAT:
        if( op->has_font_ref && op->font_ref[0] )
        {
            int font_id = UITreeBuilder_ResolveFontName(builder, op->font_ref);
            assert(font_id >= 0 && "chat font missing");
            spec.u.chat.font_id = font_id;
        }
        strncpy(
            spec.u.chat.minimenu.op_report_abuse,
            op->chat_op_report_abuse,
            sizeof(spec.u.chat.minimenu.op_report_abuse) - 1);
        spec.u.chat.minimenu.op_report_abuse_action = op->chat_op_report_abuse_action;
        strncpy(
            spec.u.chat.minimenu.op_add_ignore,
            op->chat_op_add_ignore,
            sizeof(spec.u.chat.minimenu.op_add_ignore) - 1);
        spec.u.chat.minimenu.op_add_ignore_action = op->chat_op_add_ignore_action;
        strncpy(
            spec.u.chat.minimenu.op_add_friend,
            op->chat_op_add_friend,
            sizeof(spec.u.chat.minimenu.op_add_friend) - 1);
        spec.u.chat.minimenu.op_add_friend_action = op->chat_op_add_friend_action;
        strncpy(
            spec.u.chat.minimenu.op_accept_trade,
            op->chat_op_accept_trade,
            sizeof(spec.u.chat.minimenu.op_accept_trade) - 1);
        spec.u.chat.minimenu.op_accept_trade_action = op->chat_op_accept_trade_action;
        strncpy(
            spec.u.chat.minimenu.op_accept_duel,
            op->chat_op_accept_duel,
            sizeof(spec.u.chat.minimenu.op_accept_duel) - 1);
        spec.u.chat.minimenu.op_accept_duel_action = op->chat_op_accept_duel_action;
        break;
    case UIELEM_BUILTIN_CHAT_BUTTON:
    {
        struct UITreeChatButtonConfig* cb = &spec.u.chat_button;
        if( op->chat_button_filter >= 0 && op->chat_button_filter <= 3 )
            cb->filter = (enum UITreeChatButtonFilter)op->chat_button_filter;
        else
            cb->filter = UITREE_CHAT_BUTTON_PUBLIC;
        strncpy(cb->label, op->chat_button_label, sizeof(cb->label) - 1);
        /* label_y/mode_y are box-top offsets, while the reference numbers
         * (redrawPrivacySettings: label 14 / report 19, mode 27 below the
         * node top) are baselines, so the defaults carry the same
         * baseline - p12 ascent shift the INI values use. */
        cb->label_y = op->chat_button_label_y != 0
                          ? op->chat_button_label_y
                          : (cb->filter == UITREE_CHAT_BUTTON_REPORT ? 19 - 12 : 14 - 12);
        cb->mode_y = op->chat_button_mode_y != 0 ? op->chat_button_mode_y : 27 - 12;
        if( op->has_font_ref && op->font_ref[0] )
        {
            int font_id = UITreeBuilder_ResolveFontName(builder, op->font_ref);
            assert(font_id >= 0 && "chat_button font missing");
            cb->font_id = font_id;
        }
        else if( op->font >= 0 )
        {
            cb->font_id = UITreeBuilder_ResolveFontArchive(builder, op->font);
        }
        cb->center = op->center;
        cb->shadowed = op->shadowed;
        for( int i = 0; i < 4; i++ )
        {
            strncpy(
                cb->mode_label[i],
                op->chat_button_mode_label[i],
                sizeof(cb->mode_label[i]) - 1);
            cb->mode_color[i] = op->chat_button_mode_color[i];
        }
        break;
    }
    case UIELEM_BUILTIN_WORLD:
        spec.u.world.level_mask = (uint8_t)op->level_mask;
        spec.u.world.mmb_rotate = op->mmb_rotate ? 1u : 0u;
        spec.u.world.wheel_zoom = op->wheel_zoom ? 1u : 0u;
        break;
    case UIELEM_BUILTIN_REDSTONE_TAB:
        spec.u.redstone_tab.tabno = op->tabno;
        spec.u.redstone_tab.scene_id = sprite_id;
        spec.u.redstone_tab.atlas_index = atlas_index;
        spec.u.redstone_tab.scene_id_active = sprite_active_id;
        spec.u.redstone_tab.atlas_index_active = atlas_active;
        break;
    case UIELEM_BUILTIN_TAB_ICONS:
        spec.u.tab_icon.tabno = op->tabno;
        spec.u.tab_icon.scene_id = sprite_id;
        spec.u.tab_icon.atlas_index = atlas_index;
        break;
    case UIELEM_BUILTIN_SIDEBAR:
    {
        int inv_source_id = UITREE_INV_SOURCE_INVALID;
        if( op->inv_name[0] && invs )
            inv_source_id = InvManager_ResolveSource(invs, op->inv_name);
        spec.u.sidebar.tabno = op->tabno;
        spec.u.sidebar.componentno = op->componentno;
        spec.u.sidebar.inv_source_id = inv_source_id;
        spec.u.sidebar.selected = op->selected ? 1 : 0;
        break;
    }
    case UIELEM_RS_GRAPHIC:
        spec.u.rs_graphic.scene_id = sprite_id;
        spec.u.rs_graphic.atlas_index = atlas_index;
        spec.u.rs_graphic.scene_id_active = sprite_active_id;
        spec.u.rs_graphic.atlas_index_active = atlas_active;
        break;
    case UIELEM_RS_TEXT:
        if( op->has_font_ref && op->font_ref[0] )
        {
            int font_id = UITreeBuilder_ResolveFontName(builder, op->font_ref);
            assert(font_id >= 0 && "rs_text font missing");
            spec.u.rs_text.font_id = font_id;
        }
        else
        {
            spec.u.rs_text.font_id = UITreeBuilder_ResolveFontArchive(builder, op->font);
        }
        spec.u.rs_text.color = op->color;
        spec.u.rs_text.center = op->center ? 1 : 0;
        spec.u.rs_text.shadowed = op->shadowed ? 1 : 0;
        spec.u.rs_text.text = op->text[0] ? op->text : NULL;
        break;
    case UIELEM_RS_RECT:
        spec.u.rs_rect.color = op->color;
        spec.u.rs_rect.filled = op->filled ? 1 : 0;
        break;
    case UIELEM_RS_LAYER:
        spec.u.rs_layer.scroll_height = 0;
        spec.u.rs_layer.scroll_width = 0;
        break;
    case UIELEM_RS_MODEL:
        spec.u.rs_model.gamecache_model_id = op->componentno >= 0 ? op->componentno : 0;
        spec.u.rs_model.zoom = 100;
        break;
    case UIELEM_RS_INV:
    {
        int inv_source_id = UITREE_INV_SOURCE_INVALID;
        if( op->inv_name[0] && invs )
            inv_source_id = InvManager_ResolveSource(invs, op->inv_name);
        spec.u.rs_inv.inv_source_id = inv_source_id;
        spec.u.rs_inv.cols = op->width > 0 ? op->width : 4;
        spec.u.rs_inv.rows = op->height > 0 ? op->height : 7;
        /* INI-built grids carry no objSwap flag; keep them draggable (prior
         * behaviour). Real equipment/worn grids come from the cache mount,
         * which decodes the reference objSwap || objReplace. */
        spec.u.rs_inv.can_drag = 1;
        break;
    }
    case UIELEM_RS_LINE:
        spec.u.rs_line.color = op->color;
        spec.u.rs_line.line_width = 1;
        spec.u.rs_line.horizontal = op->filled ? 1 : 0;
        break;
    default:
        break;
    }

    int32_t idx = UITree_Push(tree, parent_index, &spec);
    assert(idx >= 0);
    return idx;
}

static void
bake_rs_subtree_for_op(
    struct UITree* tree,
    struct UITreeBuilder* builder,
    struct UIBuilderTreeOp const* op,
    int32_t owner_idx,
    struct InvManager* invs)
{
    assert(tree && builder && op && owner_idx >= 0);
    assert(op->componentno >= 0);

    int packed = uibuilder_pack_component_id(op->componentno);
    int iface_id = packed >> 16;
    struct ToriRS_ComponentPack* pack =
        CacheProvider_ComponentPackGet(builder->provider, iface_id);
    assert(pack && "RS component pack missing after assets load");

    int inv_source_id = UITREE_INV_SOURCE_INVALID;
    if( op->inv_name[0] && invs )
        inv_source_id = InvManager_ResolveSource(invs, op->inv_name);

    uitree_builder_bake_pack_under_owner(tree, builder, pack, owner_idx, inv_source_id);
}

/*
 * Resolve the [hotkey:…] bindings against the nodes this bake just produced.
 *
 * A binding names a COMPONENT, and a component can be placed by more than one
 * layout entry (a fixed and a resizable gameframe both mounting the same tab
 * icon), so every op built from that component gets its own binding — whichever
 * copy is on screen answers the key.
 *
 * Three ways a binding is dropped, all quietly, because a revision's INI can
 * legitimately name chrome it does not lay out: an unknown key name, a
 * component with no layout entry, and — the one that matters — a component that
 * never advertised the effect. That last check is why `hotkey=` exists on the
 * component at all: a key can only reach behaviour the component opted into.
 */
static void
bake_hotkeys(
    struct UITree* tree,
    struct UIBuilderManifest const* manifest,
    int32_t const* node_index)
{
    assert(tree && manifest);

    tree->hotkey_count = 0;
    for( int i = 0; i < manifest->hotkey_count; i++ )
    {
        struct UIBuilderHotkey const* binding = &manifest->hotkeys[i];
        int osrs_key = LibToriRS_OsrsKeyFromName(binding->key_name);
        uint32_t effect = UITree_HotkeyEffectFromName(binding->effect);

        if( osrs_key < 0 || effect == 0 )
            continue;

        for( int op = 0; op < manifest->op_count; op++ )
        {
            if( strcmp(manifest->ops[op].component_name, binding->component_name) != 0 )
                continue;
            if( node_index[op] < 0 )
                continue;
            if( (tree->components[node_index[op]].hotkey_effects & effect) == 0 )
                continue;
            if( tree->hotkey_count >= UITREE_HOTKEY_MAX )
                return;
            tree->hotkeys[tree->hotkey_count].osrs_key = osrs_key;
            tree->hotkeys[tree->hotkey_count].node_index = node_index[op];
            tree->hotkeys[tree->hotkey_count].effect = effect;
            tree->hotkey_count++;
        }
    }
}

void
uitree_builder_bake(
    struct UITree* tree,
    struct UITreeBuilder* builder,
    struct UIBuilderManifest const* manifest,
    struct InvManager* invs)
{
    assert(tree);
    assert(builder);
    assert(manifest);
    assert(invs);

    uitree_builder_inv_seed(invs, manifest);

    if( manifest->op_count <= 0 )
        return;

    int32_t* node_index = calloc((size_t)manifest->op_count, sizeof(int32_t));
    assert(node_index);
    for( int i = 0; i < manifest->op_count; i++ )
        node_index[i] = -1;

    int built = 0;
    int guard = 0;
    int progress = 1;
    while( built < manifest->op_count && progress && guard < manifest->op_count * 4 )
    {
        progress = 0;
        guard++;
        for( int i = 0; i < manifest->op_count; i++ )
        {
            if( node_index[i] >= 0 )
                continue;

            struct UIBuilderTreeOp const* op = &manifest->ops[i];
            int32_t parent_index = -1;
            if( op->parent_name[0] != '\0' )
            {
                parent_index =
                    find_op_node(manifest->ops, node_index, manifest->op_count, op->parent_name);
                if( parent_index < 0 )
                    continue;
            }

            int32_t idx = push_builtin_op(tree, builder, op, parent_index, invs);
            node_index[i] = idx;
            built++;
            progress = 1;

            if( op->kind == UIBUILDER_OP_PUSH_RS_SUBTREE && op->componentno >= 0 )
                bake_rs_subtree_for_op(tree, builder, op, idx, invs);
        }
    }

    assert(built == manifest->op_count && "incomplete layout bake (parent cycle?)");

    uitree_builder_inv_bind_tree(tree, builder, manifest, invs);
    bake_hotkeys(tree, manifest, node_index);
    free(node_index);
}

/*
 * True when `group` hosts at least one mounted sub-interface.
 *
 * Such a group is part of the live tree by definition — something is mounted
 * inside it — so it is never spillover, however many levels down the mount
 * target happens to be.
 */
static int
group_hosts_a_mount(
    struct UITree const* tree,
    int group)
{
    for( int i = 0; i < tree->interface_parent_count; i++ )
    {
        if( ((tree->interface_parents[i].container_uid >> 16) & 0xffff) == group )
            return 1;
    }
    return 0;
}

void
uitree_builder_hide_unmounted_spillover(
    struct UITree* tree,
    int opening_group,
    int target_uid)
{
    int32_t root;
    assert(tree);
    for( root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        int cid = tree->components[root].component_id;
        int group = (cid >> 16) & 0xffff;
        if( cid < 0 || group <= 0 )
            continue;
        /* App-pushed chrome (world viewport, cross, minimenu) lives in the
         * reserved group 0x7FFE — never interface spillover. */
        if( group == 0x7FFE )
            continue;
        if( opening_group >= 0 && group == opening_group )
            continue;
        if( UITree_InterfaceParentIsMountedGroup(tree, group) )
            continue;
        /* Keep already-mounted groups and chrome that parents them. A pack baked
         * under a revconfig `rs_iface` owner is parented by construction, which
         * is what makes a declared mount immune to this sweep. */
        if( tree->components[root].parent >= 0 )
            continue;
        /* Never hide the active toplevel root group (e.g. 161) while subs are
         * mounted into it — only hide accidental sibling spillover packs.
         *
         * The mount target's own group is the obvious case, but it is not the
         * only one: mounting into a *nested* sub-interface leaves every group
         * above it unprotected. Opening the chat dialogue (231) into 162:561
         * gave host_group 162 and then hid 161 — the entire gameframe — which
         * renders as a blank frame with no error anywhere.
         *
         * Hosting a mount is the general form of "part of the live tree", and
         * it covers the immediate host as well, so one test replaces both. */
        if( group_hosts_a_mount(tree, group) )
            continue;
        if( target_uid >= 0 )
        {
            int host_group = (target_uid >> 16) & 0xffff;
            if( group == host_group )
                continue;
        }
        /* Mark it as ours: a pack the CS2 runtime baked ahead of its mount is
         * hidden here, and mount_pack_under_target must be able to tell that
         * hide apart from a cache/script one when the mount finally lands. */
        /* Kept: this is exactly the print that identified the bug above, and
         * "which root did the tree just hide" is invisible from anywhere else. */
        if( getenv("TORIRS_SPILLOVER_DEBUG") )
            fprintf(
                stderr,
                "spillover: hiding root group %d (opening %d, target %d:%d)\n",
                group,
                opening_group,
                (target_uid >> 16) & 0xffff,
                target_uid & 0xffff);
        if( !tree->components[root].behavior.hide )
            tree->components[root].behavior.hide_unmounted = 1;
        tree->components[root].behavior.hide = 1;
    }
}

void
uitree_builder_reassert_player_idle_anim(struct UITree* tree)
{
    int mi;
    assert(tree);
    for( mi = 0; mi < tree->models.count; mi++ )
    {
        int32_t i = tree->models.slots[mi];
        struct UITreeComponent* c;
        assert(i >= 0 && (uint32_t)i < tree->component_count);
        c = &tree->components[i];
        if( c->type != UIELEM_RS_MODEL )
            continue;
        if( c->behavior.client_code != 327 && c->behavior.client_code != 328 )
            continue;
        if( c->u.rs_model.gamecache_model_id != UITREE_SCENE_PLAYER_MODEL_ID )
            continue;
        c->u.rs_model.anim_seq_id = UITREE_BUILDER_PLAYER_IDLE_SEQ;
        c->u.rs_model.anim_frame = 0;
        c->u.rs_model.anim_frame_cycle = 0;
        c->u.rs_model.anim_hold = 1;
    }
}
