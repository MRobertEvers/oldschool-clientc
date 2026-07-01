#include "instance_revconfig_context.h"

#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat1_buildcache_ui.h"
#include "buildcache/dat2_buildcache.h"
#include "buildcache/dat2_buildcache_ui.h"
#include "games/runescape.h"
#include "osrs/minimenu_action.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toridraw/toridraw_scene.h"
#include "toridraw/toridraw_sprite.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"
#include "ui/ui_behavior.h"
#include "vm/cs2vm.h"
#include "ui/ui_debug.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
instance_revconfig_resolve_rs_text_font_id(
    struct InstanceRevConfigContext const* ctx,
    int cache_font_ref)
{
    int font_id = -1;

    if( cache_font_ref >= 0 && cache_font_ref <= 3 )
        font_id = ui_font_lookup_find_by_cache_font_id(&ctx->font_lookup, cache_font_ref);
    else if( cache_font_ref > 3 )
        font_id = ui_font_lookup_find_by_archive_id(&ctx->font_lookup, cache_font_ref);

    if( font_id < 0 )
        font_id = ui_font_lookup_resolve_cache_font_index(&ctx->font_lookup, cache_font_ref);
    if( font_id < 0 )
        font_id = ui_font_lookup_find(&ctx->font_lookup, "b12");
    if( font_id < 0 && ctx->font_lookup.count > 0 )
        font_id = ctx->font_lookup.entries[0].font_id;
    if( font_id < 0 )
    {
        fprintf(
            stderr,
            "RS text font not loaded (cache_font_ref=%d) — add p11/p12/b12/q8 to cache ini\n",
            cache_font_ref);
        return 0;
    }
    return font_id;
}

struct InstanceRevConfigRSSubtree*
instance_revconfig_rs_subtree_find(
    struct InstanceRevConfigContext* ctx,
    char const* owner_component)
{
    if( !ctx || !owner_component || owner_component[0] == '\0' )
        return NULL;
    for( int i = 0; i < ctx->rs_subtree_count; i++ )
    {
        if( strcmp(ctx->rs_subtrees[i].owner_component, owner_component) == 0 )
            return &ctx->rs_subtrees[i];
    }
    return NULL;
}

struct InstanceRevConfigRSSubtree*
instance_revconfig_rs_subtree_get_or_create(
    struct InstanceRevConfigContext* ctx,
    char const* owner_component)
{
    assert(
        ctx && owner_component && owner_component[0] != '\0' &&
        ctx->rs_subtree_count < INSTANCE_RC_MAX_RS_SUBTREES);
    struct InstanceRevConfigRSSubtree* existing =
        instance_revconfig_rs_subtree_find(ctx, owner_component);
    if( existing )
        return existing;

    struct InstanceRevConfigRSSubtree* subtree = &ctx->rs_subtrees[ctx->rs_subtree_count++];
    memset(subtree, 0, sizeof(*subtree));
    strncpy(subtree->owner_component, owner_component, sizeof(subtree->owner_component) - 1);
    return subtree;
}

bool
instance_revconfig_rs_subtree_append(
    struct InstanceRevConfigRSSubtree* subtree,
    int component_id,
    int parent_id,
    int rel_x,
    int rel_y)
{
    assert(subtree && component_id >= 0);
    for( int i = 0; i < subtree->item_count; i++ )
    {
        if( subtree->component_ids[i] == component_id )
            return true;
    }
    assert(subtree->item_count < INSTANCE_RC_MAX_RS_SUBTREE_ITEMS);
    int const idx = subtree->item_count++;
    subtree->component_ids[idx] = component_id;
    subtree->parent_ids[idx] = parent_id;
    subtree->rel_x[idx] = rel_x;
    subtree->rel_y[idx] = rel_y;
    return true;
}

static void
instance_revconfig_assert_sprite_atlas(
    struct InstanceRevConfigContext const* ctx,
    int scene_id,
    int atlas_index,
    char const* sprite_ref,
    char const* context)
{
    if( scene_id < 0 )
        return;

    int sprite_count = 0;
    struct ToriDraw_Sprite** sprites =
        ctx && ctx->scene ? ToriDraw_SceneSpriteGet(ctx->scene, scene_id, &sprite_count) : NULL;
    if( atlas_index < 0 || atlas_index >= sprite_count || !sprites || !sprites[atlas_index] ||
        !sprites[atlas_index]->pixels_argb )
    {
        fprintf(stderr,
            "instance_revconfig_assert_sprite_atlas: invalid atlas scene_id=%d "
            "atlas_index=%d count=%d sprite_ref=%s (%s)\n",
            scene_id,
            atlas_index,
            sprite_count,
            sprite_ref && sprite_ref[0] ? sprite_ref : "(none)",
            context ? context : "(null)");
        assert(
            sprites && atlas_index >= 0 && atlas_index < sprite_count &&
            sprites[atlas_index] && sprites[atlas_index]->pixels_argb);
    }
}

static int
instance_revconfig_register_baked_sprite(
    struct InstanceRevConfigContext* ctx,
    struct ToriAuxLibCore_Sprite* sprite)
{
    assert(ctx && ctx->cache && sprite && ctx->game && ctx->game->td);

    int element_id = ctx->next_element_id++;
    ToriAuxLibCache_SubmitSprite(ctx->cache, element_id, sprite);
    ToriAuxLibTD_Sprite(ctx->game->td, element_id);
    return element_id;
}

static void
instance_revconfig_assert_model_in_scene(
    struct InstanceRevConfigContext const* ctx,
    int model_id,
    int component_id)
{
    assert(ctx && ctx->td && ctx->scene);

    if( !ToriDraw_SceneModelHas(ctx->scene, model_id) )
        ToriAuxLibTD_Model(ctx->td, model_id);

    if( !ToriDraw_SceneModelHas(ctx->scene, model_id) )
    {
        fprintf(stderr,
            "instance_revconfig_assert_model_in_scene: model not in scene "
            "model_id=%d component_id=%d\n",
            model_id,
            component_id);
        assert(
            ToriDraw_SceneModelHas(ctx->scene, model_id) &&
            "MODEL component model not in scene after RS component load");
    }
}

static int32_t
instance_revconfig_bake_rs_component(
    struct InstanceRevConfigContext* ctx,
    int32_t parent_idx,
    struct ToriAuxLibCore_Component const* info,
    int inv_index)
{
    assert(ctx && ctx->tree && info);

    struct StaticUIBehavior behavior;
    struct StaticUIBehavior* behavior_ptr = NULL;
    memset(&behavior, 0, sizeof(behavior));
    behavior.hide = info->hide;
    behavior.button_type = info->button_type;
    behavior.client_code = info->client_code;
    behavior.over_layer_id = info->over_layer_id;
    behavior.script_kind = info->script_kind;
    behavior.over_color = info->over_color;
    behavior.active_color = info->active_color;
    behavior.active_over_color = info->active_over_color;
    if( info->scripts_count > 0 )
    {
        behavior.scripts_count = info->scripts_count;
        behavior.scripts = info->scripts;
        behavior.scripts_lengths = info->scripts_lengths;
        behavior.script_comparator = info->script_comparator;
        behavior.script_operand = info->script_operand;
    }
    behavior_ptr = &behavior;

    struct UINodeSpec spec = { 0 };
    spec.component_id = info->id;
    spec.x = info->rel_x;
    spec.y = info->rel_y;
    spec.width = info->width;
    spec.height = info->height;
    spec.behavior = behavior_ptr;
    if( info->option[0] != '\0' )
        strncpy(spec.menu_options.option, info->option, sizeof(spec.menu_options.option) - 1);
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
    {
        if( info->ops[i][0] != '\0' )
        {
            strncpy(
                spec.menu_options.ops[i],
                info->ops[i],
                sizeof(spec.menu_options.ops[i]) - 1);
        }
    }
    if( info->type == TORIAUXLIBCORE_COMPONENT_INV && info->ops[0][0] != '\0' )
    {
        assert(
            spec.menu_options.ops[0][0] != '\0' &&
            "instance_revconfig_bake_rs_component: core INV ops not copied to menu_options");
    }

    int inv_bg_sid[UI_INV_SLOT_OFFSET_MAX];
    int inv_bg_ai[UI_INV_SLOT_OFFSET_MAX];

    switch( info->type )
    {
    case TORIAUXLIBCORE_COMPONENT_LAYER:
        spec.type = UIELEM_RS_LAYER;
        spec.u.rs_layer.scroll_height = info->scroll_height;
        spec.u.rs_layer.scroll_width = info->scroll_width;
        break;
    case TORIAUXLIBCORE_COMPONENT_GRAPHIC:
    {
        int atlas = 0;
        int atlas_a = 0;
        int sid = ui_sprite_lookup_resolve_ref(&ctx->sprite_lookup, info->sprite_ref, &atlas);
        int sid_a =
            ui_sprite_lookup_resolve_ref(&ctx->sprite_lookup, info->sprite_active_ref, &atlas_a);
        if( sid < 0 && sid_a < 0 )
        {
            if( !info->graphic_hitbox_only )
            {
                fprintf(stderr,
                    "instance_revconfig_bake_rs_component: sprite resolve failed "
                    "component_id=%d ref=%s active_ref=%s\n",
                    info->id,
                    info->sprite_ref,
                    info->sprite_active_ref);
                return -1;
            }
            spec.type = UIELEM_RS_GRAPHIC;
            spec.u.rs_graphic.scene_id = -1;
            spec.u.rs_graphic.atlas_index = 0;
            spec.u.rs_graphic.scene_id_active = -1;
            spec.u.rs_graphic.atlas_index_active = 0;
            spec.u.rs_graphic.graphic_hitbox_only = 1;
            break;
        }
        spec.type = UIELEM_RS_GRAPHIC;
        if( sid >= 0 )
        {
            spec.u.rs_graphic.scene_id = sid;
            spec.u.rs_graphic.atlas_index = atlas;
        }
        else
        {
            spec.u.rs_graphic.scene_id = -1;
            spec.u.rs_graphic.atlas_index = 0;
        }
        if( sid_a >= 0 && (sid < 0 || sid_a != sid) )
        {
            spec.u.rs_graphic.scene_id_active = sid_a;
            spec.u.rs_graphic.atlas_index_active = atlas_a;
        }
        else
        {
            spec.u.rs_graphic.scene_id_active = -1;
            spec.u.rs_graphic.atlas_index_active = 0;
        }
        spec.u.rs_graphic.graphic_hitbox_only = 0;
        ui_active_debug_log(
            "bake rs_graphic id=%d ref='%s' active_ref='%s' sid=%d sid_a=%d "
            "scene_id=%d scene_id_active=%d",
            info->id,
            info->sprite_ref,
            info->sprite_active_ref,
            sid,
            sid_a,
            spec.u.rs_graphic.scene_id,
            spec.u.rs_graphic.scene_id_active);
        if( sid < 0 && sid_a >= 0 && !info->graphic_hitbox_only )
        {
            ui_active_debug_log(
                "bake rs_graphic id=%d: inactive ref unresolved; active gated by getIfActive",
                info->id);
        }
        if( spec.u.rs_graphic.scene_id >= 0 )
            instance_revconfig_assert_sprite_atlas(
                ctx,
                spec.u.rs_graphic.scene_id,
                spec.u.rs_graphic.atlas_index,
                info->sprite_ref,
                "rs_graphic");
        if( spec.u.rs_graphic.scene_id_active >= 0 )
            instance_revconfig_assert_sprite_atlas(
                ctx,
                spec.u.rs_graphic.scene_id_active,
                spec.u.rs_graphic.atlas_index_active,
                info->sprite_active_ref,
                "rs_graphic active");
        break;
    }
    case TORIAUXLIBCORE_COMPONENT_RECT:
        spec.type = UIELEM_RS_RECT;
        spec.u.rs_rect.color = info->color;
        spec.u.rs_rect.filled = info->filled;
        break;
    case TORIAUXLIBCORE_COMPONENT_TEXT:
        spec.type = UIELEM_RS_TEXT;
        spec.u.rs_text.font_id =
            instance_revconfig_resolve_rs_text_font_id(ctx, info->font_id);
        spec.u.rs_text.color = info->color;
        spec.u.rs_text.center = info->center;
        spec.u.rs_text.shadowed = info->shadowed;
        spec.u.rs_text.text = info->text[0] != '\0' ? info->text : NULL;
        spec.u.rs_text.text_active =
            info->active_text[0] != '\0' ? info->active_text : NULL;
        break;
    case TORIAUXLIBCORE_COMPONENT_INV_TEXT:
        spec.type = UIELEM_RS_INV_TEXT;
        spec.u.rs_inv_text.inv_index = inv_index;
        spec.u.rs_inv_text.cols = info->inv_cols;
        spec.u.rs_inv_text.rows = info->inv_rows;
        spec.u.rs_inv_text.margin_x = info->margin_x;
        spec.u.rs_inv_text.margin_y = info->margin_y;
        spec.u.rs_inv_text.font_id =
            instance_revconfig_resolve_rs_text_font_id(ctx, info->font_id);
        spec.u.rs_inv_text.color = info->color;
        spec.u.rs_inv_text.center = info->center;
        spec.u.rs_inv_text.shadowed = info->shadowed;
        break;
    case TORIAUXLIBCORE_COMPONENT_MODEL:
        assert(info->model_id > 0 && "MODEL component has no model_id");
        if( info->model_type == 1 )
        {
            instance_revconfig_assert_model_in_scene(ctx, info->model_id, info->id);

            int raster_w = info->width > 0 ? info->width : 32;
            int raster_h = info->height > 0 ? info->height : 32;

            char widget_ref[96];
            snprintf(
                widget_ref,
                sizeof(widget_ref),
                "widget_model:%d:%d:%d:%d:%d:%d",
                info->model_id,
                info->model_zoom,
                info->model_xan,
                info->model_yan,
                raster_w,
                raster_h);

            int atlas = 0;
            int existing = ui_sprite_lookup_resolve_ref(&ctx->sprite_lookup, widget_ref, &atlas);
            if( existing >= 0 )
            {
                instance_revconfig_assert_sprite_atlas(
                    ctx, existing, atlas, widget_ref, "rs_model sprite (cached)");

                spec.type = UIELEM_RS_GRAPHIC;
                spec.u.rs_graphic.scene_id = existing;
                spec.u.rs_graphic.atlas_index = atlas;
                spec.u.rs_graphic.scene_id_active = -1;
                spec.u.rs_graphic.atlas_index_active = 0;
                spec.u.rs_graphic.graphic_hitbox_only = 0;
                break;
            }

            struct ToriAuxLibCore_Sprite* sprite = NULL;

            if( ctx->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
            {
                assert(ctx->dat1_bc && ctx->scene);
                sprite = dat1_buildcache_widget_model_sprite(
                    ctx->dat1_bc,
                    ctx->scene,
                    info->model_id,
                    info->model_zoom,
                    info->model_xan,
                    info->model_yan,
                    raster_w,
                    raster_h);
            }
            else
            {
                assert(ctx->dat2_bc && ctx->scene);
                sprite = dat2_buildcache_widget_model_sprite(
                    ctx->dat2_bc,
                    ctx->scene,
                    info->model_id,
                    info->model_zoom,
                    info->model_xan,
                    info->model_yan,
                    raster_w,
                    raster_h);
            }

            if( !sprite )
            {
                fprintf(stderr,
                    "instance_revconfig_bake_rs_component: widget model sprite failed "
                    "component_id=%d model_id=%d model_type=%d\n",
                    info->id,
                    info->model_id,
                    info->model_type);
                assert(sprite && "MODEL model_type 1 sprite raster failed");
                return -1;
            }

            int scene_id = instance_revconfig_register_baked_sprite(ctx, sprite);
            if( scene_id < 0 )
            {
                ToriAuxLibCore_SpriteFree(sprite);
                assert(scene_id >= 0 && "MODEL model_type 1 sprite register failed");
                return -1;
            }

            instance_revconfig_assert_sprite_atlas(
                ctx, scene_id, 0, widget_ref, "rs_model sprite");

            ui_sprite_lookup_add(&ctx->sprite_lookup, widget_ref, scene_id, 1);

            spec.type = UIELEM_RS_GRAPHIC;
            spec.u.rs_graphic.scene_id = scene_id;
            spec.u.rs_graphic.atlas_index = 0;
            spec.u.rs_graphic.scene_id_active = -1;
            spec.u.rs_graphic.atlas_index_active = 0;
            spec.u.rs_graphic.graphic_hitbox_only = 0;
        }
        else if( info->model_type == 2 || info->model_type == 3 )
        {
            spec.type = UIELEM_RS_MODEL;
            spec.u.rs_model.gamecache_model_id = info->model_id;
            spec.u.rs_model.zoom = info->model_zoom > 0 ? info->model_zoom : 100;
            spec.u.rs_model.xan = info->model_xan;
            spec.u.rs_model.yan = info->model_yan;
            instance_revconfig_assert_model_in_scene(ctx, info->model_id, info->id);
        }
        else
        {
            fprintf(stderr,
                "instance_revconfig_bake_rs_component: unsupported MODEL model_type=%d "
                "component_id=%d model_id=%d\n",
                info->model_type,
                info->id,
                info->model_id);
            assert(false && "unsupported MODEL model_type");
            return -1;
        }
        break;
    case TORIAUXLIBCORE_COMPONENT_INV:
        spec.type = UIELEM_RS_INV;
        spec.u.rs_inv.inv_index = inv_index;
        spec.u.rs_inv.cols = info->inv_cols;
        spec.u.rs_inv.rows = info->inv_rows;
        spec.u.rs_inv.margin_x = info->margin_x;
        spec.u.rs_inv.margin_y = info->margin_y;

        for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
        {
            inv_bg_sid[si] = -1;
            inv_bg_ai[si] = 0;
            if( info->inv_slot_sprite_ref[si][0] == '\0' )
                continue;

            int sid = ui_sprite_lookup_resolve_ref(
                &ctx->sprite_lookup, info->inv_slot_sprite_ref[si], &inv_bg_ai[si]);
            if( sid < 0 )
            {
                fprintf(
                    stderr,
                    "instance_revconfig_bake_rs_component: inv slot sprite resolve failed "
                    "component_id=%d slot=%d ref=%s\n",
                    info->id,
                    si,
                    info->inv_slot_sprite_ref[si]);
                assert(sid >= 0);
            }
            inv_bg_sid[si] = sid;
            instance_revconfig_assert_sprite_atlas(
                ctx,
                sid,
                inv_bg_ai[si],
                info->inv_slot_sprite_ref[si],
                "rs_inv slot bg");
        }

        spec.u.rs_inv.inv_slot_offset_x = info->inv_slot_offset_x;
        spec.u.rs_inv.inv_slot_offset_y = info->inv_slot_offset_y;
        spec.u.rs_inv.inv_slot_bg_scene_id = inv_bg_sid;
        spec.u.rs_inv.inv_slot_bg_atlas_index = inv_bg_ai;
        break;
    default:
        return -1;
    }

    if( ui_minimenu_debug_enabled() )
    {
        bool core_ops_nonempty = false;
        for( int i = 0; i < TORIAUXLIBCORE_MENU_ACTION_SLOTS; i++ )
        {
            if( info->ops[i][0] != '\0' )
            {
                core_ops_nonempty = true;
                break;
            }
        }
        bool const core_has_menu = info->option[0] != '\0' || core_ops_nonempty;
        ui_minimenu_debug_log(
            "bake rs_id=%d parent_idx=%d core_type=%d core_option='%s' core_ops_nonempty=%d",
            info->id,
            parent_idx,
            (int)info->type,
            info->option,
            core_ops_nonempty ? 1 : 0);
        ui_minimenu_debug_log_menu_options(
            "bake",
            info->id,
            -1,
            (int)spec.type,
            &spec.menu_options,
            behavior.button_type,
            behavior.client_code);

        struct StaticUIComponent probe = {
            .type = spec.type,
            .menu_options = spec.menu_options,
            .behavior = behavior,
        };
        if( uitree_component_expects_minimenu_rows(&probe) &&
            !uitree_component_has_menu_options(&probe) &&
            behavior.button_type != COMPONENT_BUTTON_TYPE_CLOSE &&
            !(behavior.button_type == COMPONENT_BUTTON_TYPE_OK && behavior.client_code == 0) )
        {
            ui_minimenu_debug_log(
                "WARN bake rs_id=%d expects minimenu rows but menu_options empty "
                "(button_type=%d client_code=%d)",
                info->id,
                behavior.button_type,
                behavior.client_code);
        }
        if( core_has_menu && !uitree_component_has_menu_options(&probe) )
        {
            ui_minimenu_debug_log(
                "WARN bake rs_id=%d core had option/ops but baked menu_options empty",
                info->id);
        }
    }

    return uitree_push(ctx->tree, parent_idx, &spec);
}

struct InstanceRevConfigRsBakeIdMap
{
    int component_id;
    int32_t uitree_index;
};

static int32_t
instance_revconfig_rs_bake_lookup(
    struct InstanceRevConfigRsBakeIdMap const* map,
    int map_count,
    int component_id)
{
    if( component_id < 0 || !map )
        return -1;

    for( int i = 0; i < map_count; i++ )
    {
        if( map[i].component_id == component_id )
            return map[i].uitree_index;
    }
    return -1;
}

void
instance_revconfig_bake_rs_subtree(
    struct InstanceRevConfigContext* ctx,
    struct RevConfigUIComponentItem const* comp,
    int32_t owner_uitree_index)
{
    assert(ctx && comp && owner_uitree_index >= 0);

    struct InstanceRevConfigRSSubtree* subtree =
        instance_revconfig_rs_subtree_find(ctx, comp->name);
    assert(subtree && subtree->item_count > 0);

    int inv_index = -1;
    if( comp->inv[0] != '\0' && ctx->inv_pool )
        inv_index = uitree_inv_pool_find_by_name(ctx->inv_pool, comp->inv);
    if( comp->inv[0] != '\0' )
        assert(inv_index >= 0 && "sidebar inv= name not found in inv pool");

    struct InstanceRevConfigRsBakeIdMap id_map[INSTANCE_RC_MAX_RS_SUBTREE_ITEMS];
    int id_map_count = 0;

    int baked_count = 0;
    for( int i = 0; i < subtree->item_count; i++ )
    {
        int component_id = subtree->component_ids[i];
        struct ToriAuxLibCore_Component* info =
            ctx->core ? ToriAuxLibCore_ComponentGet(ctx->core, component_id) : NULL;
        assert(
            info &&
            "instance_revconfig_bake_rs_subtree: missing core component in rs_subtrees capture");

        ToriAuxLibCore_ComponentApplyWalkLayout(
            info,
            subtree->parent_ids[i],
            subtree->rel_x[i],
            subtree->rel_y[i]);

        int32_t parent_idx = owner_uitree_index;
        if( subtree->parent_ids[i] >= 0 )
        {
            int32_t mapped =
                instance_revconfig_rs_bake_lookup(id_map, id_map_count, subtree->parent_ids[i]);
            if( mapped >= 0 )
                parent_idx = mapped;
            else
            {
                fprintf(
                    stderr,
                    "instance_revconfig_bake_rs_subtree: parent lookup failed owner=%s "
                    "component_id=%d parent_id=%d (attaching to sidebar owner)\n",
                    comp->name,
                    info->id,
                    subtree->parent_ids[i]);
            }
        }

        if( ui_minimenu_debug_enabled() && info->type == TORIAUXLIBCORE_COMPONENT_TEXT &&
            subtree->parent_ids[i] >= 0 && ctx->tree &&
            parent_idx >= 0 && (uint32_t)parent_idx < ctx->tree->component_count )
        {
            enum StaticUIComponentType parent_type = ctx->tree->components[parent_idx].type;
            if( parent_type != UIELEM_RS_LAYER )
            {
                ui_minimenu_debug_log(
                    "bake: text rs_id=%d parent_idx=%d parent_type=%d "
                    "(expected UIELEM_RS_LAYER=%d)",
                    info->id,
                    parent_idx,
                    (int)parent_type,
                    (int)UIELEM_RS_LAYER);
            }
        }

        int32_t idx = instance_revconfig_bake_rs_component(ctx, parent_idx, info, inv_index);
        if( idx < 0 )
        {
            if( info->type == TORIAUXLIBCORE_COMPONENT_MODEL )
            {
                fprintf(
                    stderr,
                    "instance_revconfig_bake_rs_subtree: MODEL bake failed id=%d model_type=%d "
                    "model_id=%d\n",
                    info->id,
                    info->model_type,
                    info->model_id);
                assert(idx >= 0 && "instance_revconfig_bake_rs_subtree: MODEL bake failed");
            }

            char const* reason = "uitree_push failed";
            if( info->type == TORIAUXLIBCORE_COMPONENT_GRAPHIC &&
                (info->sprite_ref[0] != '\0' || info->sprite_active_ref[0] != '\0') )
                reason = "sprite resolve failed";
            else if( info->type != TORIAUXLIBCORE_COMPONENT_LAYER &&
                      info->type != TORIAUXLIBCORE_COMPONENT_GRAPHIC &&
                      info->type != TORIAUXLIBCORE_COMPONENT_RECT &&
                      info->type != TORIAUXLIBCORE_COMPONENT_TEXT &&
                      info->type != TORIAUXLIBCORE_COMPONENT_INV_TEXT &&
                      info->type != TORIAUXLIBCORE_COMPONENT_MODEL &&
                      info->type != TORIAUXLIBCORE_COMPONENT_INV )
                reason = "unknown type";
            fprintf(
                stderr,
                "instance_revconfig_bake_rs_subtree: skip component id=%d type=%d (%s)\n",
                info->id,
                (int)info->type,
                reason);
            continue;
        }

        baked_count++;
        assert(id_map_count < INSTANCE_RC_MAX_RS_SUBTREE_ITEMS);
        id_map[id_map_count].component_id = info->id;
        id_map[id_map_count].uitree_index = idx;
        id_map_count++;
    }

    if( baked_count == 0 )
    {
        fprintf(
            stderr,
            "instance_revconfig_bake_rs_subtree: no RS nodes baked for owner=%s componentno=%d\n",
            comp->name,
            comp->componentno);
    }
}

void
instance_revconfig_context_release_build_state(struct InstanceRevConfigContext* ctx)
{
    assert(ctx);
    if( ctx->dat1_bc )
        dat1_buildcache_set_interfaces(ctx->dat1_bc, NULL);
    if( ctx->dat2_bc )
        dat2_buildcache_interfaces_cleanup(ctx->dat2_bc);
}

void
instance_revconfig_assert_fonts_in_scene(struct InstanceRevConfigContext* ctx)
{
    assert(ctx);
    assert(ctx->scene && "revconfig font verify requires ToriDraw scene");
    for( int i = 0; i < ctx->font_lookup.count; i++ )
    {
        int const font_id = ctx->font_lookup.entries[i].font_id;
        if( !ToriDraw_SceneFontHas(ctx->scene, font_id) )
        {
            fprintf(
                stderr,
                "revconfig font missing from scene after load: %s (id=%d)\n",
                ctx->font_lookup.entries[i].name,
                font_id);
        }
    }
}

void
instance_revconfig_attach_scrollbar_sprites(
    struct InstanceRevConfigContext* ctx,
    struct GameRunescape* game)
{
    if( !ctx || !game )
        return;

    int atlas = 0;
    int id = ui_sprite_lookup_resolve_ref(&ctx->sprite_lookup, "scrollbar0", &atlas);
    if( id >= 0 )
        game->ui_scrollbar0_scene_id = id;
    id = ui_sprite_lookup_resolve_ref(&ctx->sprite_lookup, "scrollbar1", &atlas);
    if( id >= 0 )
        game->ui_scrollbar1_scene_id = id;
}

void
instance_revconfig_assert_sprites_in_scene(struct InstanceRevConfigContext* ctx)
{
    assert(ctx);
    assert(ctx->scene && "revconfig sprite verify requires ToriDraw scene");
    for( int i = 0; i < ctx->sprite_lookup.count; i++ )
    {
        int const element_id = ctx->sprite_lookup.entries[i].element_id;
        if( !ToriDraw_SceneSpriteHas(ctx->scene, element_id) )
        {
            fprintf(
                stderr,
                "revconfig sprite missing from scene after load: %s (id=%d)\n",
                ctx->sprite_lookup.entries[i].name,
                element_id);
            continue;
        }
        int sprite_count = 0;
        struct ToriDraw_Sprite** sprites =
            ToriDraw_SceneSpriteGet(ctx->scene, element_id, &sprite_count);
        int const atlas_count = ctx->sprite_lookup.entries[i].atlas_count;
        if( !sprites || sprite_count <= 0 || atlas_count <= 0 || sprite_count < atlas_count )
        {
            fprintf(
                stderr,
                "revconfig sprite atlas count mismatch after load: %s (scene=%d atlas=%d)\n",
                ctx->sprite_lookup.entries[i].name,
                sprite_count,
                atlas_count);
            continue;
        }
        for( int j = 0; j < atlas_count; j++ )
        {
            if( !sprites[j] || !sprites[j]->pixels_argb )
            {
                fprintf(stderr,
                    "revconfig sprite atlas frame missing pixels after load: "
                    "name=%s element_id=%d frame=%d/%d scene_count=%d\n",
                    ctx->sprite_lookup.entries[i].name,
                    element_id,
                    j,
                    atlas_count,
                    sprite_count);
                assert(
                    sprites[j] && sprites[j]->pixels_argb &&
                    "revconfig sprite atlas frame missing pixels after load");
            }
        }
    }
}

void
instance_revconfig_context_init(struct InstanceRevConfigContext* ctx)
{
    assert(ctx);
    memset(ctx, 0, sizeof(*ctx));
    ui_sprite_lookup_init(&ctx->sprite_lookup);
    ui_font_lookup_init(&ctx->font_lookup);
    ctx->next_element_id = 1;
    ctx->layout_group = "fixed";
    for( int i = 0; i < 1024; i++ )
        ctx->panel_root_id[i] = -1;
    for( int i = 0; i < INSTANCE_RC_MAX_LAYOUTS; i++ )
        ctx->layout_node_index[i] = -1;
}

struct RevConfigUIComponentItem const*
instance_revconfig_find_component(
    struct InstanceRevConfigContext const* ctx,
    char const* name)
{
    assert(ctx && name);
    for( int i = 0; i < ctx->component_count; i++ )
    {
        if( strcmp(ctx->components[i].name, name) == 0 )
            return &ctx->components[i];
    }
    return NULL;
}

int32_t
instance_revconfig_layout_parent_index(
    struct InstanceRevConfigContext const* ctx,
    char const* parent_name)
{
    assert(ctx && parent_name);
    if( parent_name[0] == '\0' )
        return -1;

    for( int i = 0; i < ctx->layout_count; i++ )
    {
        if( strcmp(ctx->layouts[i].name, parent_name) == 0 )
            return ctx->layout_node_index[i];
    }
    return -1;
}

static enum StaticUIComponentType
component_type_from_string(char const* type)
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

static uint8_t
parse_paint_levels_mask(char const* str)
{
    assert(str);
    if( str[0] == '\0' )
        return 0xFu;
    unsigned m = 0u;
    char const* p = str;
    while( *p != '\0' )
    {
        while( *p == ' ' || *p == '\t' )
            p++;
        if( *p == '\0' )
            break;
        char* end = NULL;
        long lo = strtol(p, &end, 10);
        if( end == p )
        {
            while( *p && *p != ',' )
                p++;
            if( *p == ',' )
                p++;
            continue;
        }
        p = end;
        if( lo >= 0 && lo < 8 )
            m |= 1u << (unsigned)lo;
        while( *p == ' ' || *p == '\t' )
            p++;
        if( *p == ',' )
            p++;
    }
    return m == 0u ? 0xFu : (uint8_t)m;
}

static void
apply_layout_position(
    struct RevConfigUILayoutItem const* le,
    struct RevConfigUIComponentItem const* comp,
    struct StaticUIElemPosition* pos)
{
    if( le->left || le->right || le->top || le->bottom )
    {
        pos->kind = UIPOS_RELATIVE;
        pos->relative_flags = 0;
        if( le->left )
            pos->relative_flags |= STATIC_UI_RELATIVE_FLAG_LEFT;
        if( le->right )
            pos->relative_flags |= STATIC_UI_RELATIVE_FLAG_RIGHT;
        if( le->top )
            pos->relative_flags |= STATIC_UI_RELATIVE_FLAG_TOP;
        if( le->bottom )
            pos->relative_flags |= STATIC_UI_RELATIVE_FLAG_BOTTOM;
        pos->left = le->left;
        pos->right = le->right;
        pos->top = le->top;
        pos->bottom = le->bottom;
        pos->width = le->width > 0 ? le->width : comp->width;
        pos->height = le->height > 0 ? le->height : comp->height;
    }
    else
    {
        pos->kind = UIPOS_XY;
        pos->x = le->x;
        pos->y = le->y;
        pos->width = le->width > 0 ? le->width : comp->width;
        pos->height = le->height > 0 ? le->height : comp->height;
    }
    if( le->has_anchor )
    {
        pos->anchor_x = le->anchor_x;
        pos->anchor_y = le->anchor_y;
    }
    else
    {
        pos->anchor_x = comp->anchor_x;
        pos->anchor_y = comp->anchor_y;
    }
}

static int
component_id_from_item(struct RevConfigUIComponentItem const* comp)
{
    assert(comp);
    if( comp->componentno < 0 )
        return -1;
    if( comp->componentno >= 0 )
        return comp->componentno;
    return -1;
}

static struct RSCacheDat1A_ConfigComponent*
dat1_ifaces_get_component(
    struct RSCacheDat1A_ConfigComponentList* list,
    int component_id)
{
    if( !list || component_id < 0 )
        return NULL;

    if( component_id < list->components_count && list->components[component_id] )
    {
        struct RSCacheDat1A_ConfigComponent* at_index = list->components[component_id];
        if( at_index->id == component_id )
            return at_index;
    }

    for( int i = 0; i < list->components_count; i++ )
    {
        struct RSCacheDat1A_ConfigComponent* c = list->components[i];
        if( c && c->id == component_id )
            return c;
    }
    return NULL;
}

int
instance_revconfig_resolve_walk_root_id(
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    int componentno)
{
    struct RSCacheDat1A_ConfigComponent* direct = dat1_ifaces_get_component(ifaces, componentno);
    if( !direct )
        return componentno;

    int root = componentno;

    if( direct->type == COMPONENT_TYPE_LAYER )
        root = direct->id;
    else
    {
        int layer_id = direct->layer;
        for( int depth = 0; layer_id >= 0 && depth < 32; depth++ )
        {
            struct RSCacheDat1A_ConfigComponent* layer_comp =
                dat1_ifaces_get_component(ifaces, layer_id);
            if( !layer_comp )
                break;
            if( layer_comp->type == COMPONENT_TYPE_LAYER )
            {
                root = layer_comp->id;
                break;
            }
            layer_id = layer_comp->layer;
        }

        if( root == componentno )
        {
            if( direct->layer >= 0 )
                root = direct->layer;
            else
                root = direct->id;
        }
    }

    if( root == 0 )
        return -1;
    return root;
}

static void
instance_revconfig_resolve_panel_root_for(
    struct InstanceRevConfigContext* ctx,
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    int componentno)
{
    if( componentno < 0 || componentno >= 1024 )
        return;
    if( ctx->panel_root_id[componentno] != INSTANCE_RC_PANEL_ROOT_UNSET )
        return;

    struct RSCacheDat1A_ConfigComponent* direct = dat1_ifaces_get_component(ifaces, componentno);
    if( !direct )
        return;

    int root = instance_revconfig_resolve_walk_root_id(ifaces, componentno);
    ctx->panel_root_id[componentno] =
        root >= 0 ? root : INSTANCE_RC_PANEL_ROOT_INVALID;
}

void
instance_revconfig_resolve_panel_roots(struct InstanceRevConfigContext* ctx)
{
    assert(ctx && ctx->dat1_bc);

    struct RSCacheDat1A_ConfigComponentList* ifaces = dat1_buildcache_get_interfaces(ctx->dat1_bc);
    assert(ifaces);

    for( int i = 0; i < ctx->component_count; i++ )
    {
        struct RevConfigUIComponentItem const* sidebar = &ctx->components[i];
        if( strcmp(sidebar->type, "sidebar") != 0 || sidebar->componentno < 0 )
            continue;
        instance_revconfig_resolve_panel_root_for(ctx, ifaces, sidebar->componentno);
    }
}

static void
instance_revconfig_copy_revconfig_menu_options(
    struct RevConfigUIComponentItem const* comp,
    struct StaticUIMenuOptions* dst)
{
    assert(comp && dst);
    if( comp->option[0] != '\0' )
        strncpy(dst->option, comp->option, sizeof(dst->option) - 1);
    for( int i = 0; i < REVCONFIG_MENU_OPTION_SLOTS; i++ )
    {
        if( comp->ops[i][0] != '\0' )
            strncpy(dst->ops[i], comp->ops[i], sizeof(dst->ops[i]) - 1);
        dst->op_actions[i] = comp->op_actions[i];
    }
    dst->option_action = comp->option_action;
}

static void
instance_revconfig_apply_chat_template(
    char const* src,
    int src_action,
    int default_action,
    char* dst,
    size_t dst_len,
    int* out_action)
{
    if( !dst || !out_action )
        return;
    if( src && src[0] != '\0' )
    {
        strncpy(dst, src, dst_len - 1);
        dst[dst_len - 1] = '\0';
        *out_action = src_action != 0 ? src_action : default_action;
    }
}

static void
instance_revconfig_copy_chat_minimenu_config(
    struct RevConfigUIComponentItem const* comp,
    struct StaticUIChatMinimenuConfig* dst)
{
    assert(comp && dst);
    instance_revconfig_apply_chat_template(
        comp->chat_op_report_abuse,
        comp->chat_op_report_abuse_action,
        MINIMENU_ACTION_REPORT_ABUSE,
        dst->op_report_abuse,
        sizeof(dst->op_report_abuse),
        &dst->op_report_abuse_action);
    instance_revconfig_apply_chat_template(
        comp->chat_op_add_ignore,
        comp->chat_op_add_ignore_action,
        MINIMENU_ACTION_IGNORELIST_ADD,
        dst->op_add_ignore,
        sizeof(dst->op_add_ignore),
        &dst->op_add_ignore_action);
    instance_revconfig_apply_chat_template(
        comp->chat_op_add_friend,
        comp->chat_op_add_friend_action,
        MINIMENU_ACTION_FRIENDLIST_ADD,
        dst->op_add_friend,
        sizeof(dst->op_add_friend),
        &dst->op_add_friend_action);
    instance_revconfig_apply_chat_template(
        comp->chat_op_accept_trade,
        comp->chat_op_accept_trade_action,
        MINIMENU_ACTION_OPPLAYER_TRADEREQ,
        dst->op_accept_trade,
        sizeof(dst->op_accept_trade),
        &dst->op_accept_trade_action);
    instance_revconfig_apply_chat_template(
        comp->chat_op_accept_duel,
        comp->chat_op_accept_duel_action,
        MINIMENU_ACTION_OPPLAYER_DUELREQ,
        dst->op_accept_duel,
        sizeof(dst->op_accept_duel),
        &dst->op_accept_duel_action);
}

static void
instance_revconfig_copy_chat_button_config(
    struct InstanceRevConfigContext const* ctx,
    struct RevConfigUIComponentItem const* comp,
    struct StaticUIChatButtonConfig* dst)
{
    assert(comp && dst);
    memset(dst, 0, sizeof(*dst));

    if( comp->chat_button_filter >= 0 && comp->chat_button_filter <= 3 )
        dst->filter = (enum StaticUIChatButtonFilter)comp->chat_button_filter;
    else
        dst->filter = STATIC_UI_CHAT_BUTTON_PUBLIC;

    strncpy(dst->label, comp->chat_button_label, sizeof(dst->label) - 1);
    dst->label[sizeof(dst->label) - 1] = '\0';

    if( comp->chat_button_label_y != 0 )
        dst->label_y = comp->chat_button_label_y;
    else if( dst->filter == STATIC_UI_CHAT_BUTTON_REPORT )
        dst->label_y = 19;
    else
        dst->label_y = 14;

    if( comp->chat_button_mode_y != 0 )
        dst->mode_y = comp->chat_button_mode_y;
    else
        dst->mode_y = 27;

    if( comp->has_font_ref && comp->font_ref[0] && ctx )
    {
        int font_id = ui_font_lookup_find(&ctx->font_lookup, comp->font_ref);
        if( font_id >= 0 )
            dst->font_id = font_id;
    }
    else if( comp->font >= 0 && ctx )
        dst->font_id = instance_revconfig_resolve_rs_text_font_id(ctx, comp->font);
    else
        dst->font_id = 1;

    dst->center = comp->center ? 1 : 0;
    dst->shadowed = comp->shadowed ? 1 : 0;

    for( int i = 0; i < 4; i++ )
    {
        if( comp->chat_button_mode_label[i][0] != '\0' )
        {
            strncpy(
                dst->mode_label[i],
                comp->chat_button_mode_label[i],
                sizeof(dst->mode_label[i]) - 1);
            dst->mode_label[i][sizeof(dst->mode_label[i]) - 1] = '\0';
        }
        dst->mode_color[i] = comp->chat_button_mode_color[i];
    }
}

static int32_t
instance_revconfig_build_layout_node(
    struct InstanceRevConfigContext* ctx,
    struct RevConfigUILayoutItem const* layout,
    int32_t parent_index)
{
    assert(ctx && ctx->tree && layout && layout->component[0] != '\0');
    struct RevConfigUIComponentItem const* comp =
        instance_revconfig_find_component(ctx, layout->component);
    assert(comp);

    int sprite_id = -1;
    int atlas_index = 0;
    int sprite_active_id = -1;
    int atlas_active_index = 0;

    if( comp->sprite[0] != '\0' )
    {
        sprite_id = ui_sprite_lookup_resolve_ref(&ctx->sprite_lookup, comp->sprite, &atlas_index);
        if( sprite_id < 0 )
        {
            fprintf(stderr,
                "instance_revconfig_build_layout_node: sprite resolve failed "
                "component=%s sprite=%s\n",
                comp->name,
                comp->sprite);
        }
    }

    if( comp->sprite_active[0] != '\0' )
    {
        sprite_active_id = ui_sprite_lookup_resolve_ref(
            &ctx->sprite_lookup, comp->sprite_active, &atlas_active_index);
        if( sprite_active_id < 0 )
        {
            fprintf(stderr,
                "instance_revconfig_build_layout_node: active sprite resolve failed "
                "component=%s sprite_active=%s\n",
                comp->name,
                comp->sprite_active);
        }
    }

    if( sprite_id >= 0 )
        instance_revconfig_assert_sprite_atlas(
            ctx, sprite_id, atlas_index, comp->sprite, comp->name);
    if( sprite_active_id >= 0 )
        instance_revconfig_assert_sprite_atlas(
            ctx, sprite_active_id, atlas_active_index, comp->sprite_active, comp->name);

    enum StaticUIComponentType static_type = component_type_from_string(comp->type);
    int const component_id = component_id_from_item(comp);

    struct UINodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = static_type;
    spec.component_id = component_id;
    apply_layout_position(layout, comp, &spec.position);
    spec.has_position = 1;
    if( layout->dirty )
        spec.always_dirty = 1;

    switch( static_type )
    {
    case UIELEM_BUILTIN_COMPASS:
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
        if( comp->has_font_ref && comp->font_ref[0] )
        {
            int font_id = ui_font_lookup_find(&ctx->font_lookup, comp->font_ref);
            if( font_id >= 0 )
                spec.u.minimenu.font_id = font_id;
        }
        else if( comp->font >= 0 )
        {
            int font_id = instance_revconfig_resolve_rs_text_font_id(ctx, comp->font);
            if( font_id >= 0 )
                spec.u.minimenu.font_id = font_id;
        }
        break;
    case UIELEM_BUILTIN_MINIMAP:
        spec.always_dirty = 1;
        spec.u.minimap.scene_id = sprite_id;
        break;
    case UIELEM_BUILTIN_CHAT:
        instance_revconfig_copy_chat_minimenu_config(comp, &spec.u.chat.minimenu);
        break;
    case UIELEM_BUILTIN_CHAT_BUTTON:
        instance_revconfig_copy_chat_button_config(ctx, comp, &spec.u.chat_button);
        break;
    case UIELEM_BUILTIN_WORLD:
        spec.u.world.level_mask = parse_paint_levels_mask(comp->paint_levels);
        break;
    case UIELEM_BUILTIN_REDSTONE_TAB:
        spec.u.redstone_tab.tabno = comp->tabno;
        spec.u.redstone_tab.scene_id = sprite_id;
        spec.u.redstone_tab.atlas_index = atlas_index;
        spec.u.redstone_tab.scene_id_active = sprite_active_id;
        spec.u.redstone_tab.atlas_index_active = atlas_active_index;
        break;
    case UIELEM_BUILTIN_TAB_ICONS:
        spec.u.tab_icon.tabno = comp->tabno;
        spec.u.tab_icon.scene_id = sprite_id;
        spec.u.tab_icon.atlas_index = atlas_index;
        break;
    case UIELEM_BUILTIN_SIDEBAR:
    {
        int inv_index = -1;
        if( comp->inv[0] != '\0' && ctx->inv_pool )
            inv_index = uitree_inv_pool_find_by_name(ctx->inv_pool, comp->inv);
        if( comp->inv[0] != '\0' && ctx->rs_capture_enabled )
            assert(inv_index >= 0 && "sidebar inv= name not found in inv pool");
        spec.u.sidebar.tabno = comp->tabno;
        spec.u.sidebar.componentno = comp->componentno;
        spec.u.sidebar.inv_index = inv_index;
        break;
    }
    case UIELEM_RS_GRAPHIC:
        spec.u.rs_graphic.scene_id = sprite_id;
        spec.u.rs_graphic.atlas_index = atlas_index;
        spec.u.rs_graphic.scene_id_active = sprite_active_id;
        spec.u.rs_graphic.atlas_index_active = atlas_active_index;
        break;
    case UIELEM_RS_TEXT:
    {
        spec.u.rs_text.font_id =
            instance_revconfig_resolve_rs_text_font_id(ctx, comp->font);
        spec.u.rs_text.color = comp->color;
        spec.u.rs_text.center = comp->center ? 1 : 0;
        spec.u.rs_text.shadowed = comp->shadowed ? 1 : 0;
        spec.u.rs_text.text = comp->text[0] != '\0' ? comp->text : NULL;
        break;
    }
    case UIELEM_RS_RECT:
        spec.u.rs_rect.color = comp->color;
        spec.u.rs_rect.filled = comp->filled ? 1 : 0;
        break;
    case UIELEM_RS_LAYER:
        spec.u.rs_layer.scroll_height = 0;
        spec.u.rs_layer.scroll_width = 0;
        break;
    case UIELEM_RS_MODEL:
        spec.u.rs_model.gamecache_model_id = component_id >= 0 ? component_id : 0;
        spec.u.rs_model.zoom = 100;
        spec.u.rs_model.xan = 0;
        spec.u.rs_model.yan = 0;
        break;
    case UIELEM_RS_INV:
    {
        int inv_index = -1;
        if( comp->inv[0] != '\0' && ctx->inv_pool )
            inv_index = uitree_inv_pool_find_by_name(ctx->inv_pool, comp->inv);
        spec.u.rs_inv.inv_index = inv_index;
        spec.u.rs_inv.cols = comp->width > 0 ? comp->width : 4;
        spec.u.rs_inv.rows = comp->height > 0 ? comp->height : 7;
        spec.u.rs_inv.margin_x = 0;
        spec.u.rs_inv.margin_y = 0;
        break;
    }
    case UIELEM_RS_LINE:
        spec.u.rs_line.color = comp->color;
        spec.u.rs_line.line_width = 1;
        spec.u.rs_line.horizontal = comp->filled ? 1 : 0;
        break;
    case UIELEM_BUILTIN_SPRITE:
        spec.u.sprite.scene_id = sprite_id;
        spec.u.sprite.atlas_index = atlas_index;
        break;
    default:
        break;
    }

    instance_revconfig_copy_revconfig_menu_options(comp, &spec.menu_options);

    struct StaticUIBehavior owner_behavior;
    if( comp->button_type != 0 || comp->client_code != 0 )
    {
        memset(&owner_behavior, 0, sizeof(owner_behavior));
        owner_behavior.button_type = comp->button_type;
        owner_behavior.client_code = comp->client_code;
        spec.behavior = &owner_behavior;
    }

    int32_t idx = uitree_push(ctx->tree, parent_index, &spec);
    assert(idx >= 0);
    if( comp->componentno >= 0 )
    {
        struct InstanceRevConfigRSSubtree* subtree =
            instance_revconfig_rs_subtree_find(ctx, comp->name);
        if( strcmp(comp->type, "sidebar") == 0 )
        {
            if( ctx->rs_capture_enabled && subtree && subtree->item_count > 0 )
                instance_revconfig_bake_rs_subtree(ctx, comp, idx);
            else if( subtree && subtree->item_count > 0 )
                instance_revconfig_bake_rs_subtree(ctx, comp, idx);
        }
        else if( subtree && subtree->item_count > 0 )
        {
            instance_revconfig_bake_rs_subtree(ctx, comp, idx);
        }
    }

    return idx;
}

bool
instance_revconfig_build_tree(struct InstanceRevConfigContext* ctx)
{
    assert(ctx && ctx->tree);

    for( int i = 0; i < ctx->layout_count; i++ )
        ctx->layout_node_index[i] = -1;

    char const* active_layout_group = ctx->layout_group;
    bool progress = true;
    int built = 0;
    int guard = 0;
    int active_count = 0;

    for( int i = 0; i < ctx->layout_count; i++ )
    {
        struct RevConfigUILayoutItem const* le = &ctx->layouts[i];
        if( active_layout_group && active_layout_group[0] != '\0' && le->layout_group[0] != '\0' &&
            strcmp(le->layout_group, active_layout_group) != 0 )
            continue;
        active_count++;
    }
    if( active_count <= 0 )
        return false;

    while( built < active_count && progress && guard < active_count * 4 )
    {
        progress = false;
        guard++;

        for( int i = 0; i < ctx->layout_count; i++ )
        {
            struct RevConfigUILayoutItem const* le = &ctx->layouts[i];
            if( active_layout_group && active_layout_group[0] != '\0' &&
                le->layout_group[0] != '\0' && strcmp(le->layout_group, active_layout_group) != 0 )
                continue;

            if( ctx->layout_node_index[i] >= 0 )
                continue;

            int32_t parent_index = instance_revconfig_layout_parent_index(ctx, le->parent);
            if( le->parent[0] != '\0' && parent_index < 0 )
                continue;

            int32_t idx = instance_revconfig_build_layout_node(ctx, le, parent_index);
            assert(idx >= 0);

            ctx->layout_node_index[i] = idx;
            built++;
            progress = true;
        }
    }

    uitree_layout_resolve(ctx->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    uitree_mark_all_dirty(ctx->tree);

    return true;
}
