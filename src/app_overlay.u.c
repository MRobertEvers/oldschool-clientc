/*
 * The entity overlay STAGE -- everything drawn over the world that is not part
 * of the world: overhead chat, hitsplats, health bars, headicons, the outline
 * pass, and the plugin-authored polygons and labels that share the same list.
 *
 * A unity fragment of app.c, not a module. It is textually part of app.c's
 * translation unit and included at exactly the point it was cut from, so every
 * helper here stays static and every App field it reads stays where it was.
 * The split is for the reader: this is one subject, it is a fifth of the file,
 * and nothing above or below it needs to be read to follow it.
 *
 * What is NOT here is the part that has left: the overhead chat effect colours
 * are RS_Chat_EffectColourArgb's and the scripted `_7200` overlay store is
 * rs_entity_overlay.c's. What stays is the staging -- where each item goes,
 * which entity it belongs to, and in what order the list is built -- because
 * that is projection and App state, and neither travels.
 *
 * @see src/app.c
 */

static void
app_overlay_push(
    struct App* app,
    struct UITreeEntityOverlay const* item)
{
    if( app->plugin_draw_canvas == APP_PLUGIN_SURFACE_PANEL )
    {
        struct UITreeEntityOverlay* out;

        /* PANEL is a staged, panel-local target. If the application did not
         * prepare an exact custom well, drawing is dropped rather than falling
         * through into the world list. That is the isolation boundary. */
        if( !app->panel_overlay_stage_active )
            return;
        if( app->panel_overlay_stage_count >= APP_PLUGIN_PANEL_OVERLAYS_MAX )
        {
            app->panel_overlay_stage_overflow = 1;
            return;
        }

        out = &app->panel_overlay_stage[app->panel_overlay_stage_count];
        if( !ToriRSChromePanelDraw_Transform(
                item,
                app->panel_overlay_origin_x,
                app->panel_overlay_origin_y,
                app->panel_overlay_scale,
                app->panel_overlay_clip,
                out) )
            return;
        app->panel_overlay_stage_count++;
        return;
    }

    /*
     * Which list depends on the draw window that is open, and nothing above
     * this has to know which one that is.
     *
     * Every built-in overlay -- health bars, hitsplats, overhead chat, the
     * editor marks -- is built with no window open at all, so `canvas` is 0
     * for all of them and they land in the world list exactly as before. Only
     * an explicit plugin draw event flips it; PANEL returned above and can
     * never enter one of these game lists.
     */
    if( app->plugin_draw_canvas == APP_PLUGIN_SURFACE_CANVAS )
    {
        int cap = (int)(sizeof(app->canvas_overlays) / sizeof(app->canvas_overlays[0]));
        if( app->canvas_overlay_count >= cap )
            return;
        app->canvas_overlays[app->canvas_overlay_count++] = *item;
        return;
    }

    int cap = (int)(sizeof(app->entity_overlays) / sizeof(app->entity_overlays[0]));
    if( app->entity_overlay_count >= cap )
        return;
    app->entity_overlays[app->entity_overlay_count++] = *item;
}

/** How many items the open draw window has pushed, so a draw verb can report
 *  its own cost without knowing which list it landed in. */
static int
app_overlay_count(struct App const* app)
{
    assert(app);
    switch( app->plugin_draw_canvas )
    {
    case APP_PLUGIN_SURFACE_CANVAS:
        return app->canvas_overlay_count;
    case APP_PLUGIN_SURFACE_PANEL:
        return app->panel_overlay_stage_active ? app->panel_overlay_stage_count : 0;
    default:
        return app->entity_overlay_count;
    }
}

/* One entity's overlay set. combat/damage state lives on the shared facet, so
 * players and NPCs go through the same body (reference drawEntities treats
 * them identically). */
/* The overhead chat effect colours are RS_Chat_EffectColourArgb's; this is the
 * spelling that reads the scene cycle they flash on. */
static uint32_t
app_overlay_chat_colour(
    struct App* app,
    int chat_colour,
    int timer)
{
    assert(app);
    return RS_Chat_EffectColourArgb(
        chat_colour, timer, app->world ? app->world->cycle : 0);
}

/* Overhead chat: a black shadow then the (colour-resolved) message, centred
 * above the model top (reference drawEntities, Client.ts:4871/4958). Effects
 * (wave/scroll) fall back to plain centred text — the styled variants need
 * per-glyph font passes the overlay descs don't carry yet. */
static void
app_overlay_build_chat(
    struct App* app,
    int element_id,
    struct WorldEntityFacet_Chat const* chat,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int font_id)
{
    int height = app_entity_model_height(app, element_id);
    int screen_x, screen_y;

    assert(chat);
    if( chat->timer <= 0 || chat->message[0] == '\0' || font_id < 0 )
        return;
    if( !app_world_project_actor(
            app,
            placement,
            actor_level,
            (int)draw_position->x,
            (int)draw_position->z,
            height,
            &screen_x,
            &screen_y) )
        return;

    struct UITreeEntityOverlay shadow = {
        .kind = UITREE_ENTITY_OVERLAY_TEXT,
        .x = screen_x,
        .y = screen_y + 1,
        .font_id = font_id,
        .color = 0xff000000u,
    };
    snprintf(shadow.text, sizeof(shadow.text), "%s", chat->message);
    app_overlay_push(app, &shadow);

    struct UITreeEntityOverlay body = shadow;
    body.y = screen_y;
    body.color = app_overlay_chat_colour(app, chat->colour, chat->timer);
    app_overlay_push(app, &body);
}

/* Overhead prayer/skull headicons (reference drawEntities, Client.ts:4849).
 * `headicons` is a bitmask; each set bit plots sprite[icon] from the headicons
 * pack stacked upward above the model top (start 30px up, 25px per icon).
 * Projection is at `entity.height + 15`, same as the health bar.
 *
 * The mask is walked to 31, not to 8. Eight was the width of the classic wire
 * field, but the pack it indexes is 24 frames deep at rev 239 and 30 with the
 * Ancient Curses lane's six overheads appended (Deflect ×4, Wrath, Soul Split
 * at 24..29). A loop that stops at 8 does not draw a smaller icon for those —
 * it draws nothing, and the curse reads as having no overhead at all. */
static void
app_overlay_build_player_headicons(
    struct App* app,
    int element_id,
    int headicons,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int headicons_scene)
{
    int height = app_entity_model_height(app, element_id);
    int screen_x, screen_y;
    int y_off = 30;

    if( headicons == 0 || headicons_scene <= 0 )
        return;
    if( !app_world_project_actor(
            app,
            placement,
            actor_level,
            (int)draw_position->x,
            (int)draw_position->z,
            height + 15,
            &screen_x,
            &screen_y) )
        return;

    for( int icon = 0; icon < 31; icon++ )
    {
        if( (headicons & (0x1 << icon)) == 0 )
            continue;
        struct UITreeEntityOverlay spr = {
            .kind = UITREE_ENTITY_OVERLAY_SPRITE,
            .x = screen_x - 12,
            .y = screen_y - y_off,
            .w = 0,
            .h = 0,
            .scene_id = headicons_scene,
            .atlas_index = icon,
        };
        app_overlay_push(app, &spr);
        y_off -= 25;
    }
}

/*
 * The HINT ARROW -- the server pointing at something.
 *
 * `HINT_ARROW` (server prot 50) has been parsed into `app->hint_arrow` for as
 * long as the packet existed and drawn nowhere, which `app.h` recorded as
 * "drawing is a flagged follow-on". This is that follow-on.
 *
 * It is also the only mechanism this revision has for two All Settings rows:
 *
 *   272  Clue scroll helper - Worldmap marker
 *   273  Clue scroll helper - World arrows
 *
 * Neither has a reader in the cache and neither has one in the NXT engine --
 * the only marker family the reference carries is
 * `GraphicsDefaults::GetSpriteHintMapMarkersID` / `...HintHeadIconsID` /
 * `...HintMapEdgeID`, which is this. So the payload is one coord the server
 * sends, and the two rows are the server's choice of whether to send it; the
 * client's job is to draw the arrow it is given.
 *
 * ## The three subject kinds
 *
 * The wire's `type` byte selects what `id`/`z` mean, and the reference's own
 * values are 1 = a COORD (id is x, z is z, and the height byte is how far above
 * the tile the arrow floats), 2 = an NPC by slot, 10 = a PLAYER by pid. 255
 * clears, which `rs_gameproto_exec.c` already normalises to 0.
 *
 * A subject that is out of view is not an error and not a clear: an npc can
 * walk behind the camera and come back. It simply does not project this frame,
 * which is the same distinction the scripted-overlay reaper had to learn.
 */
static void
app_overlay_build_hint_arrow(struct App* app)
{
    int const type = app->hint_arrow.type;
    int hint_scene;
    int screen_x, screen_y;
    int world_x, world_z, height;
    /* A homed rider's draw position is deck-local; the arrow anchors through
     * the same actor projection as their health bar. NULL = a root point. */
    struct WorldEntityFacet_ViewPlacement const* placement = NULL;
    int actor_level = -1;

    if( type <= 0 || !app->world )
        return;

    hint_scene = UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_HEADICONS_HINT);
    if( hint_scene <= 0 )
        return;

    switch( type )
    {
    case APP_HINT_ARROW_COORD:
    {
        /*
         * A tile, in ABSOLUTE world coordinates, converted to the scene's own
         * frame through the same origin `SET_MAP_FLAG`'s absolute form uses.
         *
         * Absolute rather than scene-local because the arrow's whole purpose is
         * to point somewhere the player is not, and a scene-local coord cannot
         * name a tile outside the loaded window. `ToriRSServer_SendHintArrowCoord`
         * sends it that way; a third-party server that sends scene-local coords
         * would put the arrow near the map corner, which is the symptom to look
         * for.
         *
         * The packet's `height` is in the projector's units above the tile, not
         * a pixel offset -- the reference floats a coord arrow clear of the
         * ground so it stays readable over scenery.
         */
        int const base_x = (app->rebuild_zone_x - 6) * 8;
        int const base_z = (app->rebuild_zone_z - 6) * 8;

        world_x = ((app->hint_arrow.target - base_x) << 7) + 64;
        world_z = ((app->hint_arrow.tile_z - base_z) << 7) + 64;
        height = app->hint_arrow.height * 2;
        break;
    }
    case APP_HINT_ARROW_NPC:
    {
        struct WorldEntity_NPC* npc = World_NpcGetByServerSlot(app->world, app->hint_arrow.target);

        if( !npc || npc->element_id < 0 )
            return;
        world_x = (int)npc->draw_position.x;
        world_z = (int)npc->draw_position.z;
        placement = &npc->view_placement;
        height = app_entity_model_height(app, npc->element_id) + 15;
        break;
    }
    case APP_HINT_ARROW_PLAYER:
    {
        struct WorldEntity_Player* player =
            World_PlayerGetByServerPid(app->world, app->hint_arrow.target);

        if( !player || player->element_id < 0 )
            return;
        world_x = (int)player->draw_position.x;
        world_z = (int)player->draw_position.z;
        placement = &player->view_placement;
        actor_level = player->grid_position.level;
        height = app_entity_model_height(app, player->element_id) + 15;
        break;
    }
    default:
        /* An unknown subject kind, which is a server sending something this
         * revision does not define. Silent: it is not this frame's business to
         * decide, and a log line per frame would be the whole log. */
        return;
    }

    if( !app_world_project_actor(
            app, placement, actor_level, world_x, world_z, height, &screen_x, &screen_y) )
        return;

    {
        /*
         * Frame 0: the solid downward arrow that sits over the subject.
         *
         * There is no screen-EDGE form to draw, and that is a fact about this
         * cache rather than a gap here. `headicons_hint` (sprite archive 441)
         * declares six 25x25 frames and **only two have any pixels**: frame 0 is
         * the solid arrow and frame 1 is the same arrow in outline. Frames 2..5
         * are entirely transparent.
         *
         * The reference's edge form comes from a different pack --
         * `GraphicsDefaults::GetSpriteHintMapEdgeID`, beside
         * `...HintMapMarkersID` -- and this cache's sprite gameval table names
         * no such group: `headicons_hint` is its only hint asset. So an
         * off-screen arrow here would need artwork invented for it.
         *
         * Re-derive with:
         *   3rd/rscache/tools/spritebake/spritebake --rev osrs239 cache.osrs239 \
         *       --list --probe headicons_hint
         */
        struct UITreeEntityOverlay spr = {
            .kind = UITREE_ENTITY_OVERLAY_SPRITE,
            .x = screen_x - 12,
            .y = screen_y - 48,
            .w = 0,
            .h = 0,
            .scene_id = hint_scene,
            .atlas_index = 0,
        };
        app_overlay_push(app, &spr);
    }
}

/*
 * The sprite-group id of `headicons_prayer`.
 *
 * An npc's opcode-102 icon names its group as a NUMBER, and the client
 * resolves that pack by NAME (static_sprites.c, STATIC_SPRITE_HEADICONS_PRAYER)
 * — the provider offers no synchronous name -> group-id lookup to close the
 * gap with, only an async load task. So the number is stated here, from
 * `OSRS-Content/osrs239-content/pack/8_sprites.pack` line 441, where it is the
 * only group any of this cache's 77 headicon-bearing npc records names.
 *
 * Failure mode if a future cache renumbers it: npcs stop drawing overheads.
 * That is the deliberate direction — a record naming an unrecognised group is
 * skipped rather than drawn out of the prayer pack, because an icon that says
 * "Protect from Magic" when the record meant something else is worse than no
 * icon at all.
 */
#define APP_HEADICONS_PRAYER_GROUP 440

/*
 * Overhead prayer icon for an NPC (reference drawEntities, NpcType.headicon).
 *
 * Where a player carries an eight-bit MASK and stacks every set bit, an npc
 * carries ONE frame from one sprite group and plots it in the first slot. That
 * asymmetry is the reference's, not a simplification: the player's icons are a
 * live prayer set, the npc's is a property of which record it currently is.
 * Which is exactly how a prayer-switching npc works — `npc_changetype` between
 * records that differ only in this field is what makes the overhead change.
 *
 * The group is a sprite-archive id. 440 (`headicons_prayer`) is the only one
 * cache.osrs239 uses on an npc, and the client already resolves that pack for
 * the player pass, so it is passed in rather than looked up again here; a
 * record naming any other group draws nothing rather than drawing the wrong
 * pack's frame.
 */
static void
app_overlay_build_npc_headicon(
    struct App* app,
    int element_id,
    struct ToriRS_Npctype const* npctype,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int prayer_scene,
    int prayer_group)
{
    int height;
    int screen_x, screen_y;

    if( !npctype || npctype->head_icon_index < 0 || prayer_scene <= 0 )
        return;
    if( npctype->head_icon_group >= 0 && npctype->head_icon_group != prayer_group )
        return;
    height = app_entity_model_height(app, element_id);
    if( !app_world_project_actor(
            app,
            placement,
            -1,
            (int)draw_position->x,
            (int)draw_position->z,
            height + 15,
            &screen_x,
            &screen_y) )
        return;

    {
        struct UITreeEntityOverlay spr = {
            .kind = UITREE_ENTITY_OVERLAY_SPRITE,
            .x = screen_x - 12,
            .y = screen_y - 30,
            .w = 0,
            .h = 0,
            .scene_id = prayer_scene,
            .atlas_index = npctype->head_icon_index,
        };
        app_overlay_push(app, &spr);
    }
}

/* Push one projected world segment as a LINE overlay (box + diagonal). */
static void
app_overlay_push_segment(
    struct App* app,
    int screen_x0,
    int screen_y0,
    int screen_x1,
    int screen_y1,
    uint32_t color)
{
    struct UITreeEntityOverlay seg = {
        .kind = UITREE_ENTITY_OVERLAY_LINE,
        .x = screen_x0 < screen_x1 ? screen_x0 : screen_x1,
        .y = screen_y0 < screen_y1 ? screen_y0 : screen_y1,
        .w = screen_x0 < screen_x1 ? screen_x1 - screen_x0 : screen_x0 - screen_x1,
        .h = screen_y0 < screen_y1 ? screen_y1 - screen_y0 : screen_y0 - screen_y1,
        .color = color,
        .line_width = 2,
        /* Direction 0 = TL->BR. The segment runs that diagonal when x and y
         * grow together; otherwise it is the other one. */
        .line_direction = ((screen_x0 < screen_x1) != (screen_y0 < screen_y1)) ? 1 : 0,
    };
    app_overlay_push(app, &seg);
}

/*
 * TORIRS_HOVER_FOOTPRINT=1: outline the hovered loc's footprint tiles in red.
 *
 * The painter orders scenery by its FOOTPRINT (size_x x size_z from the loc
 * config, orientation-swapped), while the model draws wherever its vertices
 * land — and nothing on screen says which tiles the painter believed the loc
 * covered. When a model overhangs its footprint, terrain on the overhung
 * tiles legitimately draws later and paints over it, which reads as "the
 * painter is broken" while every ordering rule is being honoured. This makes
 * the footprint visible so model-vs-footprint mismatches are a hover, not an
 * afternoon (the multiloc trap of loc-placement-debug fame).
 *
 * Each footprint tile is outlined at terrain height through the same
 * projector the health bars use, so the outline hugs the contour.
 */
/**
 * Emit a convex polygon as a closed outline.
 *
 * The overlay's own primitives are boxes and box-diagonals, so a polygon is
 * expanded here into one LINE per edge rather than reaching the draw layer as a
 * single command. That keeps the emit walk's one-item-one-command stepping
 * intact — a real multi-segment render command would need a sub-step counter
 * threaded through every backend, which is worth doing only when a highlight
 * needs to be FILLED rather than outlined.
 *
 * Degenerate hulls are drawn as what they are: two points are a single
 * segment (a footprint seen edge-on), and one point draws nothing rather than a
 * zero-length line the rasteriser would have to special-case.
 */
/**
 * Emit a convex polygon as a FILL: a begin / point... / end run.
 *
 * The run is three kinds of overlay item rather than one item holding an array
 * so that each still maps to exactly one render command — the emit walk is one
 * command per step, and bracketing is what lets a variable-length primitive
 * through it without a sub-step counter in the walk and in all four backends.
 *
 * @param trans 0 opaque .. 255 invisible. A highlight is a wash over the model
 *        it marks, so an opaque fill would hide the thing being highlighted.
 */
static void
app_overlay_push_polygon_filled(
    struct App* app,
    const int* points_x,
    const int* points_y,
    int point_count,
    uint32_t color,
    int trans)
{
    struct UITreeEntityOverlay item;

    assert(app);
    assert(points_x);
    assert(points_y);

    /* Under three points there is no area to fill. The caller still draws the
     * outline, so a hull seen edge-on degrades to a line rather than vanishing. */
    if( point_count < 3 )
        return;

    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_POLY_BEGIN;
    item.color = color;
    item.trans = trans;
    app_overlay_push(app, &item);

    for( int i = 0; i < point_count; i++ )
    {
        memset(&item, 0, sizeof(item));
        item.kind = UITREE_ENTITY_OVERLAY_POLY_POINT;
        item.x = points_x[i];
        item.y = points_y[i];
        app_overlay_push(app, &item);
    }

    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_POLY_END;
    app_overlay_push(app, &item);
}

static void
app_overlay_push_polygon(
    struct App* app,
    const int* points_x,
    const int* points_y,
    int point_count,
    uint32_t color)
{
    assert(app);
    assert(points_x);
    assert(points_y);
    assert(point_count >= 0);

    if( point_count < 2 )
        return;

    if( point_count == 2 )
    {
        app_overlay_push_segment(app, points_x[0], points_y[0], points_x[1], points_y[1], color);
        return;
    }

    for( int i = 0; i < point_count; i++ )
    {
        int const next = (i + 1) % point_count;
        app_overlay_push_segment(
            app, points_x[i], points_y[i], points_x[next], points_y[next], color);
    }
}

/**
 * Outline the MODEL of a scene element: a silhouette that wraps the thing in
 * three dimensions, not a quad on the ground under it.
 *
 * Renderer-independent by construction, which is the constraint that shapes it.
 * The projection is the app's own integer camera transform and the output is
 * the LINE primitives the overlay pass already carries, so soft3d, gl3 and
 * gl3zb all draw this without knowing it exists. Anything that reached into a
 * renderer — a stencil pass, an edge filter over the depth buffer, a shader —
 * would have to be written three times and would not exist at all in the
 * software rasteriser.
 *
 * The shape projected is the model's bounds CYLINDER as an eight-corner box:
 * `radius` either way in x and z, `min_y`..`max_y` vertically. The cylinder is
 * what the renderer itself culls and sorts against, so an outline drawn from it
 * agrees with what is on screen; and because a cylinder has no orientation in
 * xz, this needs no yaw and is correct for a loc at any angle and for an npc
 * mid-turn.
 *
 * Hulling eight corners rather than the mesh's vertices is a deliberate stop:
 * it is one outline that always wraps the model, at fixed cost per entity per
 * frame. Hugging the mesh exactly means projecting every vertex, which is the
 * same code with a bigger input — see ToriDraw_ConvexHullScratch, which exists
 * for that and has no point cap.
 *
 * @param fill_trans 0 opaque .. 255 invisible, or -1 for no fill at all. The
 *        hover and editor marks pass APP_OUTLINE_FILL_TRANS; a plugin picks
 *        its own, because a highlight over a crowd of npcs wants to be lighter
 *        than one over a single latched selection -- or absent entirely.
 * @return 1 when an outline was emitted.
 */
static int
app_overlay_outline_element_model_trans(
    struct App* app,
    int element_id,
    uint32_t color,
    int fill_trans)
{
    struct ToriDraw_SceneElement* element;
    struct ToriDraw_BoundsCylinder* bounds;
    int px[8];
    int py[8];
    int hull_x[8];
    int hull_y[8];
    int count = 0;
    int hull_size;
    int ox;
    int oy;
    int oz;
    int radius;

    assert(app);

    if( !app->scene || element_id < 0 )
        return 0;
    if( !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return 0;

    element = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !element )
        return 0;

    bounds = ToriDraw_ModelGetBoundsCylinder(element->model);
    /* No bounds is not a failure: a handle that is not a full model (a sprite
     * billboard, an empty slot) has none, and there is nothing to outline. */
    if( !bounds )
        return 0;

    ox = element->world_position.x;
    oy = element->world_position.y;
    oz = element->world_position.z;
    radius = bounds->radius;
    if( radius <= 0 )
        return 0;

    for( int corner = 0; corner < 8; corner++ )
    {
        /* Bit 0 = east, bit 1 = south, bit 2 = the model's top edge. `min_y` is
         * the TOP in scene space, where y grows downward. */
        int const wx = ox + ((corner & 1) ? radius : -radius);
        int const wz = oz + ((corner & 2) ? radius : -radius);
        int const wy = oy + ((corner & 4) ? bounds->min_y : bounds->max_y);
        int screen_x;
        int screen_y;

        if( !app_world_project_at(app, wx, wz, wy, &screen_x, &screen_y) )
            continue;
        px[count] = screen_x;
        py[count] = screen_y;
        count++;
    }

    if( count < 2 )
        return 0;

    hull_size = ToriDraw_ConvexHull(px, py, count, hull_x, hull_y);
    /* Fill first, outline over it: the wash says "this one" at a glance and the
     * outline gives it a definite edge, which a translucent fill alone does not
     * have against busy ground. */
    if( fill_trans >= 0 )
        app_overlay_push_polygon_filled(app, hull_x, hull_y, hull_size, color, fill_trans);
    app_overlay_push_polygon(app, hull_x, hull_y, hull_size, color);
    return 1;
}

/**
 * Directions sampled around a projected mesh when reducing it to a hull.
 *
 * The reduction is what makes a mesh outline affordable. The exact hull of a
 * few thousand screen points costs an angular sort over all of them; the
 * extreme point along a FIXED direction is one multiply-add and one compare
 * per vertex. Every such extreme is a vertex of the true hull, so the polygon
 * built from them is inscribed in it — tighter than the real silhouette by at
 * most the sagitta of a 360/(2*N) degree arc, never looser — and it is capped
 * at 2*N points, which is what keeps a highlight's cost to the overlay budget
 * bounded no matter how detailed the model is.
 *
 * 16 directions is an 11.25 degree gap between samples: under half a percent
 * of the silhouette's radius, which is sub-pixel on anything short of a boss
 * filling the viewport.
 */
#define APP_OUTLINE_HULL_MESH_DIRECTIONS 16

/**
 * Outline the MESH of a scene element: the model's own posed vertices, rather
 * than the box that contains them.
 *
 * The bounds outline above is the cylinder — `radius` in every horizontal
 * direction — so an npc is wrapped at the radius of whatever sticks out
 * furthest: a halberd, a cape, a wing. That reads on screen as a square around
 * every npc regardless of its shape, which is exactly what this is for. Here
 * the geometry that is actually drawn is what gets hulled, so a thin thing
 * outlines thin and a turning thing narrows as it turns.
 *
 * The vertices read are the LIVE ones (`vertices_*`, never
 * `original_vertices_*`): the animation frame, the post-transform placement
 * and any merged spot graphic are already applied to them, which is what keeps
 * the outline on the pose being rendered instead of the bind pose.
 *
 * Placement is re-derived here the way the projector derives it — roll, then
 * pitch, then yaw about the model's own origin, then the element's world
 * position — because a model's vertices are stored in its own frame and only
 * the projector has ever combined them with the element's angles.
 *
 * Cost is one projection per vertex per frame against the bounds outline's
 * eight, which is why the shape is the caller's choice and not the only mode.
 *
 * @param fill_trans 0 opaque .. 255 invisible, or -1 for no fill at all.
 * @return 1 when an outline was emitted.
 */
static int
app_overlay_outline_element_mesh_trans(
    struct App* app,
    int element_id,
    uint32_t color,
    int fill_trans)
{
    enum
    {
        DIRECTIONS = APP_OUTLINE_HULL_MESH_DIRECTIONS,
        CANDIDATES = DIRECTIONS * 2
    };
    struct ToriDraw_SceneElement* element;
    vertexint_t const* vertices_x;
    vertexint_t const* vertices_y;
    vertexint_t const* vertices_z;
    int vertex_count;
    int sin_dir[DIRECTIONS];
    int cos_dir[DIRECTIONS];
    long long extreme[CANDIDATES];
    int extreme_x[CANDIDATES];
    int extreme_y[CANDIDATES];
    int extreme_seen[CANDIDATES];
    int px[CANDIDATES];
    int py[CANDIDATES];
    int hull_x[CANDIDATES];
    int hull_y[CANDIDATES];
    int count = 0;
    int hull_size;
    int ox;
    int oy;
    int oz;
    int yaw;
    int pitch;
    int roll;
    int sin_yaw;
    int cos_yaw;
    int sin_pitch;
    int cos_pitch;
    int sin_roll;
    int cos_roll;

    assert(app);

    if( !app->scene || element_id < 0 )
        return 0;
    if( !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return 0;

    element = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !element )
        return 0;

    vertex_count = ToriDraw_ModelGetVertexCount(element->model);
    vertices_x = ToriDraw_ModelGetVerticesX(element->model);
    vertices_y = ToriDraw_ModelGetVerticesY(element->model);
    vertices_z = ToriDraw_ModelGetVerticesZ(element->model);
    /* No mesh is not a failure, for the same reason no bounds cylinder is not:
     * a handle that is not a full model (a sprite billboard, an empty slot)
     * has no vertices and there is nothing to outline. */
    if( vertex_count <= 0 || !vertices_x || !vertices_y || !vertices_z )
        return 0;

    for( int d = 0; d < DIRECTIONS; d++ )
    {
        /* Half a turn of directions, not a whole one: the minimum along a
         * direction IS the maximum along its opposite, so the other half would
         * ask every vertex the same question a second time. */
        int const angle = d * (2048 / (DIRECTIONS * 2));
        sin_dir[d] = ToriDraw_Sin(angle);
        cos_dir[d] = ToriDraw_Cos(angle);
        extreme_seen[d * 2] = 0;
        extreme_seen[d * 2 + 1] = 0;
    }

    ox = element->world_position.x;
    oy = element->world_position.y;
    oz = element->world_position.z;
    yaw = element->world_position.yaw;
    pitch = element->world_position.pitch;
    roll = element->world_position.roll;
    sin_yaw = ToriDraw_Sin(yaw);
    cos_yaw = ToriDraw_Cos(yaw);
    sin_pitch = ToriDraw_Sin(pitch);
    cos_pitch = ToriDraw_Cos(pitch);
    sin_roll = ToriDraw_Sin(roll);
    cos_roll = ToriDraw_Cos(roll);

    for( int v = 0; v < vertex_count; v++ )
    {
        /* 64-bit intermediates. A vertex coordinate is a signed 16-bit
         * quantity and the trig tables are 16.16, so one product alone reaches
         * 2^31 and the sum of two passes it -- the same shape the projection
         * kernels carry, but they are fed a model that has already been culled
         * against the scene's capacity while this runs on whatever the
         * element holds. The >>16 result is identical wherever int would not
         * have overflowed. */
        long long vx = vertices_x[v];
        long long vy = vertices_y[v];
        long long vz = vertices_z[v];
        int screen_x;
        int screen_y;
        long long tmp;

        /* graphics/projection.u.c project_orthographic order: roll (Z), pitch
         * (X), yaw (Y). Any other order puts the outline somewhere the model
         * is not the moment two of the three are non-zero. */
        if( roll != 0 )
        {
            tmp = (vy * sin_roll + vx * cos_roll) >> 16;
            vy = (vy * cos_roll - vx * sin_roll) >> 16;
            vx = tmp;
        }
        if( pitch != 0 )
        {
            tmp = (vy * cos_pitch - vz * sin_pitch) >> 16;
            vz = (vy * sin_pitch + vz * cos_pitch) >> 16;
            vy = tmp;
        }
        if( yaw != 0 )
        {
            tmp = (vz * sin_yaw + vx * cos_yaw) >> 16;
            vz = (vz * cos_yaw - vx * sin_yaw) >> 16;
            vx = tmp;
        }

        /* A vertex behind the near plane is dropped rather than clamped: the
         * hull of what IS on screen is a smaller mark, while a clamped one is
         * a wrong mark. */
        if( !app_world_project_at(
                app, ox + (int)vx, oz + (int)vz, oy + (int)vy, &screen_x, &screen_y) )
            continue;

        for( int d = 0; d < DIRECTIONS; d++ )
        {
            /* 64-bit: screen coordinates run to six figures once a model is
             * close to the camera, and a 16.16 direction multiplies that past
             * 2^32. A wrapped dot product picks the wrong vertex and the
             * outline folds through itself. */
            long long const dot =
                (long long)screen_x * cos_dir[d] + (long long)screen_y * sin_dir[d];
            int const hi = d * 2;
            int const lo = d * 2 + 1;

            if( !extreme_seen[hi] || dot > extreme[hi] )
            {
                extreme_seen[hi] = 1;
                extreme[hi] = dot;
                extreme_x[hi] = screen_x;
                extreme_y[hi] = screen_y;
            }
            if( !extreme_seen[lo] || dot < extreme[lo] )
            {
                extreme_seen[lo] = 1;
                extreme[lo] = dot;
                extreme_x[lo] = screen_x;
                extreme_y[lo] = screen_y;
            }
        }
    }

    /* Distinct points only. One vertex is the extreme in many directions at
     * once — on a small model, in nearly all of them — and repeated points
     * make the scan's collinear tie-break decide a turn between two copies of
     * the same coordinate. */
    for( int i = 0; i < CANDIDATES; i++ )
    {
        int duplicate = 0;

        if( !extreme_seen[i] )
            continue;
        for( int j = 0; j < count; j++ )
        {
            if( px[j] == extreme_x[i] && py[j] == extreme_y[i] )
            {
                duplicate = 1;
                break;
            }
        }
        if( duplicate )
            continue;
        px[count] = extreme_x[i];
        py[count] = extreme_y[i];
        count++;
    }

    if( count < 2 )
        return 0;

    hull_size = ToriDraw_ConvexHull(px, py, count, hull_x, hull_y);
    if( fill_trans >= 0 )
        app_overlay_push_polygon_filled(app, hull_x, hull_y, hull_size, color, fill_trans);
    app_overlay_push_polygon(app, hull_x, hull_y, hull_size, color);
    return 1;
}

/* The mark the hover footprint and the editor selection both draw: the
 * silhouette with the standard wash under it. */
static int
app_overlay_outline_element_model(
    struct App* app,
    int element_id,
    uint32_t color)
{
    return app_overlay_outline_element_model_trans(app, element_id, color, APP_OUTLINE_FILL_TRANS);
}

static void
app_overlay_outline_scenery(
    struct App* app,
    struct WorldEntity_Scenery const* scenery)
{
    int base_x = scenery->grid_position.x;
    int base_z = scenery->grid_position.z;
    int size_x = scenery->debug.draw_size_x > 0 ? scenery->debug.draw_size_x : 1;
    int size_z = scenery->debug.draw_size_z > 0 ? scenery->debug.draw_size_z : 1;
    int plane_y;

    /* One flat plane at the SW corner's ground height, the height the loc was
     * placed against — not per-corner terrain samples. Sampling each corner's
     * own column bends the outline over every slope and, on the raised ground
     * an overhung footprint reaches into, floats it clear of the loc it is
     * meant to describe. */
    plane_y = app_world_height(app, base_x * 128, base_z * 128, scenery->grid_position.level);

    /*
     * The SILHOUETTE of the footprint, not a box per tile.
     *
     * Outlining each tile separately draws every internal edge — a 3x3 loc came
     * out as nine overlapping quads, which reads as a grid laid over the loc
     * rather than as the loc being highlighted. Hulling the projected corners
     * collapses that to the one closed outline the eye is looking for, and it
     * costs less to draw: four segments instead of thirty-six.
     *
     * The corners are projected first and hulled in SCREEN space, not hulled on
     * the ground and then projected. A footprint is convex on the ground, but
     * "convex after projection" is what makes the outline enclose the pixels,
     * and the two only agree for an axis-aligned camera.
     */
    {
        /* Corner order SW, SE, NE, NW; fine coords are tile * 128. */
        static const int corner[4][2] = {
            { 0, 0 },
            { 1, 0 },
            { 1, 1 },
            { 0, 1 }
        };
        int px[TORIDRAW_CONVEX_HULL_MAX_POINTS];
        int py[TORIDRAW_CONVEX_HULL_MAX_POINTS];
        int hull_x[TORIDRAW_CONVEX_HULL_MAX_POINTS];
        int hull_y[TORIDRAW_CONVEX_HULL_MAX_POINTS];
        int count = 0;
        int hull_size;

        for( int tz = base_z; tz < base_z + size_z; tz++ )
        {
            for( int tx = base_x; tx < base_x + size_x; tx++ )
            {
                for( int c = 0; c < 4; c++ )
                {
                    int screen_x;
                    int screen_y;

                    if( count >= TORIDRAW_CONVEX_HULL_MAX_POINTS )
                        break;
                    /* A corner behind the camera projects to nothing usable, so
                     * it is dropped rather than clamped: the hull of what IS in
                     * front is still the right outline for the visible part,
                     * where a clamped point would drag an edge across the
                     * screen. */
                    if( !app_world_project_at(
                            app,
                            (tx + corner[c][0]) * 128,
                            (tz + corner[c][1]) * 128,
                            plane_y,
                            &screen_x,
                            &screen_y) )
                        continue;
                    px[count] = screen_x;
                    py[count] = screen_y;
                    count++;
                }
            }
        }

        if( count == 0 )
            return;

        hull_size = ToriDraw_ConvexHull(px, py, count, hull_x, hull_y);
        /* Why an outline looks wrong, in one line: too few corners means the
         * projection dropped some (behind the camera), and hull < corners is
         * the interior points being discarded, which is the point. */
        if( getenv("TORIRS_HULL_DEBUG") )
            TORIRS_LOG(
                "hull: loc %d footprint %dx%d corners=%d hull=%d\n",
                scenery->loc_id,
                size_x,
                size_z,
                count,
                hull_size);
        app_overlay_push_polygon(app, hull_x, hull_y, hull_size, APP_OUTLINE_COLOR_FOOTPRINT);
    }
}

static void
app_overlay_build_hover_footprint(struct App* app)
{
    /* 0 = off; 1 = the hovered loc; >1 = every instance of that LOC ID.
     * The id form exists for headless runs: TORIRS_SIM_HOVER parks the mouse
     * before the frame loop, so an exit screenshot has no hover to read.
     *
     * The live mode is App state rather than a static resolved once, because
     * the hover_footprint hotkey turns it on and off during a session. The env
     * var still chooses WHICH mode, and app_hover_footprint_toggle restores it
     * — see App::hover_footprint_mode. */
    int mode = app->hover_footprint;

    if( !mode || !app->world )
        return;

    if( mode == 1 )
    {
        /*
         * The pickset is this frame's under-mouse set, nearest hits first (the
         * same order the minimenu consumes), so the first loc or npc in it is
         * the one the cursor is actually on.
         *
         * Npcs are outlined as well as locs because an editor is placing both
         * against each other, and "what am I about to act on" is the same
         * question for either. The model outline is the same call for both —
         * they are both scene elements — which is why this does not need to
         * know what kind of entity it found beyond where to read the id.
         */
        struct World_Picked const* hit = NULL;
        for( int i = 0; i < app->world_pickset.count && !hit; i++ )
        {
            enum World_PickType const type = app->world_pickset.items[i].type;
            if( type == WORLD_PICK_SCENERY || type == WORLD_PICK_NPC )
                hit = &app->world_pickset.items[i];
        }
        if( !hit )
            return;

        /* Model silhouette first. It is the outline that reads as "this thing
         * is selected"; the ground footprint below says which TILES it owns,
         * which is what an editor needs when placing something beside it. */
        app_overlay_outline_element_model(app, hit->element_id, APP_OUTLINE_COLOR_HOVER);

        if( hit->type == WORLD_PICK_SCENERY )
        {
            struct WorldEntity_Scenery* scenery =
                World_SceneryGetByElementId(app->world, hit->element_id);
            if( scenery )
                app_overlay_outline_scenery(app, scenery);
        }
        return;
    }

    struct World_EntityPool* pool = &app->world->entities.scenery;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Scenery* scenery = World_EntityPoolGet(pool, i);
        if( scenery && scenery->loc_id == mode )
        {
            /* Both marks, the same pair the hover path draws. The two modes
             * showing different things would make the by-id form useless for
             * checking the hover form — which is what it is for, since a
             * headless run has no cursor to hover with. */
            app_overlay_outline_element_model(app, scenery->element_id, APP_OUTLINE_COLOR_HOVER);
            app_overlay_outline_scenery(app, scenery);
        }
    }
}

/**
 * The map editor SELECT tool's latch (editor_panel.sel_kind) -- distinct from
 * the hover footprint above: hover follows the mouse every frame, this stays
 * on what was latched even after the cursor moves off it, matching what
 * panel_refresh (editor_panel.c) is reading for the readout at the same time.
 */
static void
app_overlay_build_editor_selection(struct App* app)
{
    struct Editor_Panel const* panel = &app->editor_panel;

    /* Whatever tool is active: the selection is tool-independent (pick with
     * Select, then rotate/move/reshape with the others), so its highlight
     * must not vanish the moment the tool that will act on it is chosen --
     * the Move tool with an invisible subject is aiming blind. The old
     * SELECT-only gate predates select-then-operate. */
    if( !panel->visible )
        return;

    /* The Place-loc ghost's FOOTPRINT: the translucent model says what it
     * looks like, this says which tiles it will own -- the question that
     * decides whether it fits beside the wall. Same silhouette the hover
     * footprint draws, fed by the ghost's own scenery entity, so a 3x2 loc
     * shows 3x2 here without anything re-deriving sizes. Before the
     * selection early-out: a ghost exists with or without a selection. */
    if( app->map_ghost.active && app->world )
    {
        int const idx = World_SceneryFindAt(
            app->world,
            app->map_ghost.spec.scene_x,
            app->map_ghost.spec.scene_z,
            app->map_ghost.spec.level,
            app->map_ghost.spec.shape);
        if( idx >= 0 )
        {
            struct WorldEntity_Scenery* ghost =
                World_EntityPoolGet(&app->world->entities.scenery, idx);
            if( ghost )
                app_overlay_outline_scenery(app, ghost);
        }
    }

    if( panel->sel_kind == EDITOR_SELECTION_NONE || !app->world )
        return;

    if( panel->sel_kind == EDITOR_SELECTION_LOC )
    {
        struct WorldEntity_Scenery* scenery =
            World_SceneryGetByElementId(app->world, panel->sel_element_id);

        /* A reshape/swap deleted the element this selection pointed at (a loc
         * change is delete + add, and the add is async). Re-find the NEW
         * element by tile and shape, and heal the selection -- transiently
         * absent while the add is still in flight, which draws no highlight
         * for a frame or two rather than the wrong one forever. */
        if( !scenery )
        {
            int const idx = World_SceneryFindAt(
                app->world,
                panel->sel_scene_x,
                panel->sel_scene_z,
                panel->sel_level,
                panel->sel_shape);
            if( idx >= 0 )
                scenery = World_EntityPoolGet(&app->world->entities.scenery, idx);
            if( scenery )
                app->editor_panel.sel_element_id = scenery->element_id;
        }
        if( !scenery )
            return;
        app_overlay_outline_element_model(
            app, panel->sel_element_id, APP_OUTLINE_COLOR_EDITOR_SELECT);
        app_overlay_outline_scenery(app, scenery);
        return;
    }

    /* Terrain: the same single-plane, hulled-corners outline
     * app_overlay_outline_scenery draws for a loc's footprint, for the one
     * latched tile -- there is no WorldEntity_Scenery here to read a size
     * from, so the four corners are built directly instead of looped per
     * tile. */
    {
        int const base_x = panel->sel_scene_x;
        int const base_z = panel->sel_scene_z;
        static int const corner[4][2] = {
            { 0, 0 },
            { 1, 0 },
            { 1, 1 },
            { 0, 1 }
        };
        int px[4];
        int py[4];
        int hull_x[4];
        int hull_y[4];
        int count = 0;
        int hull_size;
        int const plane_y = app_world_height(app, base_x * 128, base_z * 128, panel->sel_level);

        for( int c = 0; c < 4; c++ )
        {
            int screen_x;
            int screen_y;

            if( !app_world_project_at(
                    app,
                    (base_x + corner[c][0]) * 128,
                    (base_z + corner[c][1]) * 128,
                    plane_y,
                    &screen_x,
                    &screen_y) )
                continue;
            px[count] = screen_x;
            py[count] = screen_y;
            count++;
        }
        if( count == 0 )
            return;

        hull_size = ToriDraw_ConvexHull(px, py, count, hull_x, hull_y);
        app_overlay_push_polygon_filled(
            app,
            hull_x,
            hull_y,
            hull_size,
            APP_OUTLINE_COLOR_EDITOR_SELECT,
            APP_OUTLINE_FILL_TRANS);
        app_overlay_push_polygon(app, hull_x, hull_y, hull_size, APP_OUTLINE_COLOR_EDITOR_SELECT);
    }
}
