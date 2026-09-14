/*
 * Projecting world points and actors to the screen, and the heights overlays hang off.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/*
 * Reference getOverlayPos (Client.ts:5253): rotate the entity's
 * camera-relative fine offset by yaw then pitch and divide by depth, from the
 * viewport centre. Returns 0 when the point is behind the near plane
 * (reference sets projectX = -1) or off the map.
 *
 * The linear scale is the camera's own, NOT the reference's `<< 9`:
 * Client-TS could shift by UNIT_SCALE_SHIFT because its world scale was the
 * constant 512, and ours stopped being one in §15. A hardcoded 512 here
 * re-creates the §1 wedge for every overlay — outlines, health bars,
 * hitsplats and overhead chat all landing 512/scale times too far from the
 * viewport centre.
 */
/* Project a world point at an ABSOLUTE height. The height-above-ground
 * spelling below samples terrain per point, which is right for entities but
 * wrong for anything that must stay coplanar — a footprint outline on sloped
 * ground warps if each corner samples its own column. */
int
app_world_project_at(
    struct App* app,
    int fine_x,
    int fine_z,
    int world_y,
    int* out_x,
    int* out_y)
{
    if( !app->world || !app->world_view_valid )
        return 0;
    if( fine_x < 128 || fine_z < 128 )
        return 0;
    /* Keep the reference's overlay near plane at 50. The renderer camera may
     * be lowered experimentally, but accepting a point closer than the overlay
     * contract did before this extraction would be an appearance change. */
    return ToriRS_WorldProjectPoint(
        &app->world_camera,
        &app->world_camera_pos,
        app->world_emit_desc.x,
        app->world_emit_desc.y,
        app->world_emit_desc.w,
        app->world_emit_desc.h,
        50,
        fine_x,
        world_y,
        fine_z,
        out_x,
        out_y);
}

int
app_world_project(
    struct App* app,
    int fine_x,
    int fine_z,
    int height_above_ground,
    int* out_x,
    int* out_y)
{
    int ground_y;
    int level = 0;

    if( !app->world || !app->world_view_valid )
        return 0;
    if( fine_x < 128 || fine_z < 128 )
        return 0;
    /* The effective ROOT plane, not the local player's own: aboard, the
     * player's level is a DECK plane (the planking is authored at plane 1),
     * and sampling root terrain there put every shore npc's health bar and
     * hitsplat at the level-1 heightmap's idea of the ground. Ashore the two
     * are the same number. Same authority as the movers and the minimap. */
    level = app_cinema_level(app);
    ground_y = app_world_height(app, fine_x, fine_z, level);
    return app_world_project_at(app, fine_x, fine_z, ground_y - height_above_ground, out_x, out_y);
}

/*
 * Project an ACTOR's overlay anchor. A root actor is app_world_project
 * verbatim. A HOMED rider's draw position is DECK-LOCAL fine units, so the
 * root spelling composes the way the emit path does: the point out through
 * the hull (Wev_ParentFromDeck) at the height the descent draws them — the
 * deck world's heightmap at the actor's own plane, plus the hull's y and
 * bob. The deob never converts at all: each world's actors project through
 * that world's own composed transform (its 2D overlays anchor on the screen
 * coordinates that projection produced), which is what makes a splat above a
 * rider ride the hull. Projected (footprint-routed) actors keep root draw
 * positions and take the root arm.
 */
int
app_world_project_actor(
    struct App* app,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int fine_x,
    int fine_z,
    int height_above_ground,
    int* out_x,
    int* out_y)
{
    struct Wev* wev;
    struct Worldview* view;
    struct WevDeckBox box;
    int root_fx;
    int root_fz;
    int ground_y;
    int deck_level;

    if( !placement || placement->view_id == WORLDVIEW_ROOT ||
        !Wevs_IsLive(&app->wevs, placement->view_id) ||
        !WorldviewRegistry_IsLive(&app->worldviews, placement->view_id) )
        return app_world_project(app, fine_x, fine_z, height_above_ground, out_x, out_y);
    wev = Wevs_Get(&app->wevs, placement->view_id);
    /* Model population and its overlay have the same visibility contract:
     * flattened/skipped passengers must not leave floating names or bars. */
    if( wev->flattened || !wev->render_visible )
        return 0;
    if( wev->parent_view_id != WORLDVIEW_ROOT )
        return app_world_project(app, fine_x, fine_z, height_above_ground, out_x, out_y);
    view = WorldviewRegistry_Get(&app->worldviews, placement->view_id);
    if( placement->home_view == 0 )
    {
        /* Projected crew retain root-wire draw positions, but their borrowed
         * render placement already contains the correct deck-local point. */
        fine_x = placement->x;
        fine_z = placement->z;
    }
    app_wev_deck_box(app, wev, app->world, &box);
    Wev_ParentFromDeck(&box, fine_x, fine_z, &root_fx, &root_fz);
    deck_level = actor_level;
    if( deck_level < 0 )
        deck_level = app_wev_deck_level(app, placement->view_id);
    if( deck_level >= COLLISION_LEVELS )
        deck_level = COLLISION_LEVELS - 1;
    ground_y = wev->y + wev->bob_y + World_HeightAt(view->world, fine_x, fine_z, deck_level);
    return app_world_project_at(
        app, root_fx, root_fz, ground_y - height_above_ground, out_x, out_y);
}

/* Reference ClientEntity.height = model.minY, which Client-TS accumulates as
 * `max(-vertexY)` — a POSITIVE magnitude measuring up from the model origin.
 * ToriDraw's bounds cylinder stores the true minimum instead (negative, since
 * up is -y), so it has to be negated here. Getting this wrong collapses the
 * health bar onto the entity's feet.
 *
 * ClientNpc/ClientPlayer.getTempModel() sets `this.height = model.minY` from
 * the entity's OWN model, then — only after that assignment — combines in the
 * attached graphic for rendering (ClientNpc.ts:34 runs before the spotanim
 * branch below it). `height` never sees the combined mesh.
 *
 * A live attached graphic (`app_entity_spotanim_find` non-NULL) means this
 * element's current model is that combined mesh: `app_world_sync_one_entity_
 * spotanim` merges the spot graphic's posed geometry into it every frame the
 * spot animation advances and calls `ToriDraw_ModelMerge`, which recomputes
 * the bounds cylinder over every vertex in the merge. Reading that live bounds
 * here pulled the spot graphic's own (frequently rescaled, always moving)
 * geometry into the entity's reported height, so the health bar / hitsplat /
 * chat / headicon position — everything anchored on this — tracked the spot
 * animation's pose instead of standing still on the entity. `entry->body` is
 * the pristine pre-combine snapshot (`ToriDraw_ModelCopy` sets its own bounds
 * cylinder), the port's equivalent of the reference's separate `height` field. */
int
app_entity_model_height(
    struct App* app,
    int element_id)
{
    struct ToriDraw_SceneElement* el;
    struct ToriDraw_BoundsCylinder* bounds;
    struct AppEntitySpotanim* spot_entry = app_entity_spotanim_find(app, element_id, 0);

    if( spot_entry && spot_entry->body )
    {
        struct ToriDraw_ModelHandle body_hnd = { .kind = TORIDRAWMK_MODEL };
        body_hnd.u.model.model = spot_entry->body;
        bounds = ToriDraw_ModelGetBoundsCylinder(body_hnd);
        return bounds ? -bounds->min_y : 0;
    }

    if( element_id < 0 || !app->scene || !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return 0;
    el = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !el || !ToriDraw_ModelKindIsFull(el->model.kind) )
        return 0;
    bounds = ToriDraw_ModelGetBoundsCylinder(el->model);
    return bounds ? -bounds->min_y : 0;
}

int
app_entity_overlay_height(
    struct App* app,
    int element_id,
    int type_height)
{
    int height;

    if( type_height >= 0 )
        return type_height;
    height = app_entity_model_height(app, element_id);
    return height > 0 ? height : APP_OVERLAY_DEFAULT_LOGICAL_HEIGHT;
}

/**
 * Pixel size of a sprite already resident in the scene.
 *
 * The scene is the only place a decoded sprite's dimensions exist -- the
 * healthbar config names an id, not a size -- and this runs inside the
 * per-frame overlay build, where there is nowhere to yield to a load. The boot
 * preload in task_dat2_healthbar_load.c is what makes the answer available;
 * false here means it is not, and the caller falls back to the declared width.
 */
bool
app_scene_sprite_size(
    struct App* app,
    int scene_id,
    int* out_w,
    int* out_h)
{
    struct ToriDraw_Sprite** sprites;
    int count = 0;

    assert(app);
    assert(out_w);
    assert(out_h);
    if( scene_id <= 0 )
        return false;
    sprites = ToriDraw_SceneSpriteGet(app->scene, scene_id, &count);
    if( !sprites || count <= 0 || !sprites[0] )
        return false;
    *out_w = sprites[0]->width;
    *out_h = sprites[0]->height;
    return true;
}
