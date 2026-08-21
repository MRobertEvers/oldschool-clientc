/*
 * The engine side of the plugin seam.
 *
 * Included into app.c rather than compiled on its own, the way
 * world_builder.c carries world_terrain.u.c: everything here is written in
 * terms of app.c's static overlay and projection helpers, and exporting those
 * so a separate translation unit could reach them would widen app.c's public
 * surface for no gain. The host never sees any of it -- it holds a vtable of
 * these functions and a void* App, which is what keeps the host compilable and
 * testable without the client.
 *
 * Two conversions happen at this boundary and nowhere else:
 *
 *   - Tiles become ABSOLUTE (scene tile + world base tile). A scene rebuild
 *     renumbers every scene-local tile, so a plugin that stored one would find
 *     it pointing somewhere else after a walk across a map square edge.
 *   - Fine positions stay SCENE-RELATIVE, because their only use is being
 *     handed back to app_world_project, which expects exactly that.
 */

/*
 * Assets and world objects are declared here and defined far below, beside the
 * spawn machinery they are built out of: materialising a plugin's model needs
 * the model builder, the scene element allocator and the async spawn task, all
 * of which live down with the client's own graphics. Only the vtable is
 * assembled here.
 */
static int app_plugin_asset_read(void* user, char const* plugin, char const* name);
static int
app_plugin_asset_write(void* user, char const* plugin, char const* name, void const* data, int size);
static int
app_plugin_screenshot(void* user, char const* plugin, char const* dir, char const* name);
static int app_plugin_object_create(void* user);
static void app_plugin_object_destroy(void* user, int handle);
static void app_plugin_object_set_model(void* user, int handle, int source, int id);
static void app_plugin_object_recolor(void* user, int handle, int hsl_from, int hsl_to);
static void app_plugin_object_clear_recolors(void* user, int handle);
static void app_plugin_object_set_anim(void* user, int handle, int seq_id, int loop);
static void app_plugin_object_set_light(void* user, int handle, int ambient, int contrast);
static void app_plugin_object_set_position(
    void* user,
    int handle,
    int tile_x,
    int tile_z,
    int level,
    int height,
    int yaw);
static void app_plugin_object_set_active(void* user, int handle, int active);
static int app_plugin_object_ready(void* user, int handle);
/* Re-place every plugin object after a scene rebuild. Called from the
 * world-loaded seam, which runs long before the definition. */
static void app_plugin_objects_rebuild(struct App* app);

/* Reference the local player's route[0], not the draw position: the draw
 * position is interpolated between tiles every frame, and the server thinks in
 * whole tiles. route[0] is the authoritative one (entity_facets.h). */
static void
app_plugin_fill_player(
    struct App* app,
    struct WorldEntity_Player const* player,
    struct ToriRS_PluginPlayerSnap* out)
{
    int const base_x = app->world->_base_tile_x;
    int const base_z = app->world->_base_tile_z;

    assert(player);
    assert(out);

    memset(out, 0, sizeof(*out));

    if( player->pathing.route_length > 0 )
    {
        out->true_x = base_x + player->pathing.route_x[0];
        out->true_z = base_z + player->pathing.route_z[0];
    }
    else
    {
        out->true_x = base_x + player->grid_position.x;
        out->true_z = base_z + player->grid_position.z;
    }
    out->level = player->grid_position.level;

    out->fine_x = (int)player->draw_position.x;
    out->fine_z = (int)player->draw_position.z;

    /*
     * Where the walk ends is the MAP FLAG, and only the map flag.
     *
     * The route queue cannot answer it. `route` is a history buffer the draw
     * position interpolates through -- route[0] is the newest tile and the
     * entries above it are the ones being slid away from -- so its far end
     * trails BEHIND the player rather than leading. Under server-authoritative
     * pathing it is never more than a step or two long either, because the
     * server hands out one tile per tick; the destination is not in it at any
     * point in the walk.
     *
     * The flag is what the client is actually told: it is set from the routed
     * destination on the click (or by the server's SET_MAP_FLAG) and cleared
     * on arrival, which is exactly the lifetime a destination marker wants.
     * RuneLite's tile indicator draws this same value -- Client.
     * getLocalDestinationLocation() is the flag, not a path.
     *
     * It belongs to the local player alone. Every other player's snapshot
     * reports itself as its own destination, which is what "not walking
     * anywhere I can see" has to look like.
     */
    if( player == app_local_player(app) && app->minimap_flag_x >= 0 )
    {
        out->flag_x = base_x + app->minimap_flag_x;
        out->flag_z = base_z + app->minimap_flag_z;
        out->dest_x = out->flag_x;
        out->dest_z = out->flag_z;
    }
    else
    {
        out->flag_x = -1;
        out->flag_z = -1;
        out->dest_x = out->true_x;
        out->dest_z = out->true_z;
    }

    out->server_pid = player->server_pid;
    out->element_id = player->element_id;
    out->combat_level = player->combat_level;
    snprintf(out->name, sizeof(out->name), "%s", player->name);
}

static void
app_plugin_fill_npc(
    struct App* app,
    struct WorldEntity_NPC const* npc,
    struct ToriRS_PluginNpcSnap* out)
{
    int const base_x = app->world->_base_tile_x;
    int const base_z = app->world->_base_tile_z;

    assert(npc);
    assert(out);

    memset(out, 0, sizeof(*out));

    if( npc->pathing.route_length > 0 )
    {
        out->true_x = base_x + npc->pathing.route_x[0];
        out->true_z = base_z + npc->pathing.route_z[0];
    }
    else
    {
        out->true_x = base_x + npc->grid_position.x;
        out->true_z = base_z + npc->grid_position.z;
    }
    out->level = npc->grid_position.level;

    out->fine_x = (int)npc->draw_position.x;
    out->fine_z = (int)npc->draw_position.z;

    out->server_slot = npc->server_slot;
    out->npc_id = npc->npc_id;
    out->base_npc_id = npc->base_npc_id;
    out->combat_level = npc->combat_level;
    out->size = npc->size > 0 ? npc->size : 1;
    out->element_id = npc->element_id;
    out->visible_ops = npc->visible_ops;
    /* The name was copied onto the entity at spawn/retype; there is no cache
     * fetch here, the same way the minimenu builder does not do one. */
    snprintf(out->name, sizeof(out->name), "%s", npc->name);
}

static void
app_plugin_fill_obj(
    struct App* app,
    struct WorldEntity_ObjStack const* stack,
    struct ToriRS_PluginObjSnap* out)
{
    assert(app);
    assert(stack);
    assert(out);

    memset(out, 0, sizeof(*out));
    out->obj_id = stack->obj_id;
    out->count = stack->count;
    out->tile_x = app->world->_base_tile_x + stack->grid_position.x;
    out->tile_z = app->world->_base_tile_z + stack->grid_position.z;
    out->level = stack->grid_position.level;
    out->element_id = stack->element_id;
    /* The name was snapshotted onto the stack at add time, the same way the
     * npc's was -- no cache fetch on this path. */
    snprintf(out->name, sizeof(out->name), "%s", stack->name);

    /*
     * `cost` is not on the stack entity, so it does come from the provider --
     * but only ever as a HIT. The objtype was loaded to build the stack's
     * model, and the cache is not consulted for one that is not there: a miss
     * leaves 0 rather than queuing a load, because this runs inside a snapshot
     * a plugin asked for and a snapshot must not start IO.
     */
    {
        struct ToriRS_Objtype* type =
            stack->obj_id >= 0 ? CacheProvider_ObjtypeGet(app->provider, stack->obj_id) : NULL;
        out->cost = type ? type->cost : 0;
    }
}

static int
app_plugin_obj_next(void* user, int iter, struct ToriRS_PluginObjSnap* out)
{
    struct App* app = (struct App*)user;
    struct World_EntityPool* pool;
    int at;

    assert(app);
    assert(out);

    if( !app->world )
        return -1;
    pool = &app->world->entities.obj_stack;

    at = iter < 0 ? World_EntityPoolHead(pool) : World_EntityPoolNext(pool, iter);
    while( at != WORLD_ENTITY_NIL )
    {
        struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, at);
        if( stack )
        {
            app_plugin_fill_obj(app, stack, out);
            return at;
        }
        at = World_EntityPoolNext(pool, at);
    }
    return -1;
}

/*
 * The plugin contract restates a few key codes so that nothing built on it has
 * to include an engine header. This is the seam where both definitions are in
 * scope, so this is where they are held together: move the enum and the client
 * stops compiling, rather than every script silently gating on the wrong key.
 */
_Static_assert(TORIRS_PLUGIN_KEY_SHIFT == TORIRSK_SHIFT, "plugin SHIFT keycode drifted");
_Static_assert(TORIRS_PLUGIN_KEY_CTRL == TORIRSK_CTRL, "plugin CTRL keycode drifted");
_Static_assert(TORIRS_PLUGIN_KEY_TAB == TORIRSK_TAB, "plugin TAB keycode drifted");
_Static_assert(TORIRS_PLUGIN_KEY_SPACE == TORIRSK_SPACE, "plugin SPACE keycode drifted");
_Static_assert(TORIRS_PLUGIN_KEY_ESCAPE == TORIRSK_ESCAPE, "plugin ESCAPE keycode drifted");

/* ---------------------------------------------------------------- queries */

static int
app_plugin_world_cycle(void* user)
{
    struct App* app = (struct App*)user;
    assert(app);
    return app->world ? app->world->cycle : 0;
}

static uint64_t
app_plugin_frame_ms(void* user)
{
    struct App* app = (struct App*)user;
    assert(app);
    return app->last_frame_ms;
}

static int
app_plugin_local_player(void* user, struct ToriRS_PluginPlayerSnap* out)
{
    struct App* app = (struct App*)user;
    struct WorldEntity_Player* player;

    assert(app);
    assert(out);

    if( !app->world )
        return 0;
    player = app_local_player(app);
    if( !player )
        return 0;

    app_plugin_fill_player(app, player, out);
    return 1;
}

static int
app_plugin_npc_next(void* user, int iter, struct ToriRS_PluginNpcSnap* out)
{
    struct App* app = (struct App*)user;
    struct World_EntityPool* pool;
    int at;

    assert(app);
    assert(out);

    if( !app->world )
        return -1;
    pool = &app->world->entities.npc;

    /* WORLD_ENTITY_NIL is -1, so the pool's own sentinel is already the
     * iterator's "done" value and the two never need translating. */
    at = iter < 0 ? World_EntityPoolHead(pool) : World_EntityPoolNext(pool, iter);
    while( at != WORLD_ENTITY_NIL )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, at);
        if( npc && npc->element_id >= 0 )
        {
            app_plugin_fill_npc(app, npc, out);
            return at;
        }
        at = World_EntityPoolNext(pool, at);
    }
    return -1;
}

static int
app_plugin_npc_by_slot(void* user, int server_slot, struct ToriRS_PluginNpcSnap* out)
{
    struct App* app = (struct App*)user;
    struct WorldEntity_NPC* npc;

    assert(app);
    assert(out);

    if( !app->world || server_slot < 0 )
        return 0;
    npc = World_NpcGetByServerSlot(app->world, server_slot);
    if( !npc )
        return 0;

    app_plugin_fill_npc(app, npc, out);
    return 1;
}

static int
app_plugin_player_next(void* user, int iter, struct ToriRS_PluginPlayerSnap* out)
{
    struct App* app = (struct App*)user;
    struct World_EntityPool* pool;
    int at;

    assert(app);
    assert(out);

    if( !app->world )
        return -1;
    pool = &app->world->entities.player;

    at = iter < 0 ? World_EntityPoolHead(pool) : World_EntityPoolNext(pool, iter);
    while( at != WORLD_ENTITY_NIL )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, at);
        if( player && player->element_id >= 0 )
        {
            app_plugin_fill_player(app, player, out);
            return at;
        }
        at = World_EntityPoolNext(pool, at);
    }
    return -1;
}

static int
app_plugin_key_held(void* user, int keycode)
{
    struct App* app = (struct App*)user;
    assert(app);

    /* No input frame yet (boot, or a headless run that never pumped one) is a
     * legitimate state, not a contract violation: nothing is held. */
    if( !app->plugin_input )
        return 0;
    if( keycode < 0 || keycode >= TORIRSK_COUNT )
        return 0;
    return LibToriRS_Input_IsKeyHeld(app->plugin_input, (enum LibToriRS_KeyCode)keycode) ? 1 : 0;
}

/*
 * The tile the pointer is over, from the pick that rode the last rendered
 * frame. Absolute, like every other tile a plugin sees.
 *
 * app.c clears the scene-local latch to -1 the moment the pointer leaves the
 * world viewport, so "no hover" and "hovering tile 0,0 of the scene" are never
 * confused -- and a plugin drawing a cursor highlight stops drawing it the
 * frame the mouse moves onto the inventory.
 *
 * The level is converted to the WALKED plane on the way out. The pick latch
 * holds the MESH level -- the plane the floor was authored on, which the map
 * editor wants because it edits the cache -- but every level a plugin is ever
 * handed is a walked one (a player snap's, an npc snap's), and draw_tile's
 * height sample assumes the same. Handing a raw mesh level across meant a
 * bridge deck's marker was drawn at level 1, app_world_height added the
 * bridge's own +1 on top, and the tile-indicator's hover quad floated 240
 * units above the bridge it was marking.
 */
static int
app_plugin_hover_tile(void* user, int* out_tile_x, int* out_tile_z, int* out_level)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(out_tile_x);
    assert(out_tile_z);
    assert(out_level);

    if( !app->world )
        return 0;
    if( app->world_hover_tile_x < 0 || app->world_hover_tile_z < 0 )
        return 0;

    *out_tile_x = app->world->_base_tile_x + app->world_hover_tile_x;
    *out_tile_z = app->world->_base_tile_z + app->world_hover_tile_z;
    *out_level = World_TerrainWalkLevel(
        app->world,
        app->world_hover_tile_x,
        app->world_hover_tile_z,
        app->world_hover_tile_level);
    return 1;
}

static int
app_plugin_project(void* user, int fine_x, int fine_z, int height, int* out_x, int* out_y)
{
    struct App* app = (struct App*)user;
    assert(app);
    assert(out_x);
    assert(out_y);
    return app_world_project(app, fine_x, fine_z, height, out_x, out_y);
}

/* ---------------------------------------------------------------- drawing */

/*
 * The plugin contract says a colour is 0xRRGGBB and that alpha is never part
 * of it -- fill strength is the separate 0..255 argument. The overlay
 * primitives underneath disagree: ToriDraw2D_FillRect, which every LINE and
 * FILL_RECT lands in, reads the top byte as alpha and returns immediately on
 * zero. So a plugin outline drawn in the colour the plugin actually named is
 * dropped without a mark on screen -- which is what every one of the client's
 * own callers avoids by spelling its constant 0xFFFF0000 rather than 0xFF0000.
 *
 * Opaque is what the contract already promises, so it is supplied here, at the
 * one boundary where a plugin colour becomes an overlay ARGB, rather than
 * asked of every plugin author.
 */
static uint32_t
app_plugin_overlay_argb(uint32_t rgb)
{
    return rgb | 0xFF000000u;
}

/*
 * Every draw entry point reports how many overlay items it pushed, by
 * measuring the shared pool before and after rather than counting them itself.
 * That is what lets the host hold a plugin to a per-frame budget without the
 * host or the plugin having to know that a filled hull costs one item per hull
 * vertex plus two brackets plus an outline segment per edge.
 */

static int
app_plugin_draw_tile(
    void* user,
    int tile_x,
    int tile_z,
    int level,
    uint32_t rgb,
    int fill_alpha)
{
    struct App* app = (struct App*)user;
    static const int CORNER[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    int px[4];
    int py[4];
    int hull_x[4];
    int hull_y[4];
    int count = 0;
    int hull_size;
    int scene_x;
    int scene_z;
    int plane_y;
    int const before = app ? app->entity_overlay_count : 0;

    assert(app);

    if( !app->world )
        return 0;

    /* Back to scene-local: the projector works in the scene's frame, and a
     * plugin only ever speaks absolute. */
    scene_x = tile_x - app->world->_base_tile_x;
    scene_z = tile_z - app->world->_base_tile_z;

    /*
     * One flat plane at the tile's own SW corner height, not a per-corner
     * terrain sample.
     *
     * A tile marker has to sit ON the tile. Sampling each corner's own column
     * is right for a footprint that spans several tiles, but for a single
     * tile it bends the quad along whatever the neighbouring columns do,
     * which on a slope lifts one corner clear of the ground it is marking.
     */
    plane_y = app_world_height(app, scene_x * 128, scene_z * 128, level);

    for( int c = 0; c < 4; c++ )
    {
        int screen_x;
        int screen_y;
        if( !app_world_project_at(
                app,
                (scene_x + CORNER[c][0]) * 128,
                (scene_z + CORNER[c][1]) * 128,
                plane_y,
                &screen_x,
                &screen_y) )
            continue;
        px[count] = screen_x;
        py[count] = screen_y;
        count++;
    }
    if( count < 2 )
        return 0;

    hull_size = ToriDraw_ConvexHull(px, py, count, hull_x, hull_y);
    if( fill_alpha > 0 )
        app_overlay_push_polygon_filled(
            app,
            hull_x,
            hull_y,
            hull_size,
            app_plugin_overlay_argb(rgb),
            255 - (fill_alpha > 255 ? 255 : fill_alpha));
    app_overlay_push_polygon(app, hull_x, hull_y, hull_size, app_plugin_overlay_argb(rgb));
    return app->entity_overlay_count - before;
}

static int
app_plugin_draw_hull(void* user, int element_id, uint32_t rgb, int fill_alpha, int shape)
{
    struct App* app = (struct App*)user;
    int before;

    assert(app);
    assert(shape == TORIRS_PLUGIN_HULL_BOUNDS || shape == TORIRS_PLUGIN_HULL_MESH);
    before = app->entity_overlay_count;
    /* Either silhouette the client already knows how to draw. Their fill
     * transparency is fixed at APP_OUTLINE_FILL_TRANS for the hover and editor
     * marks; here the plugin chooses, so an outline-only highlight is
     * possible. */
    if( shape == TORIRS_PLUGIN_HULL_MESH )
        app_overlay_outline_element_mesh_trans(
            app,
            element_id,
            app_plugin_overlay_argb(rgb),
            fill_alpha > 0 ? 255 - (fill_alpha > 255 ? 255 : fill_alpha) : -1);
    else
        app_overlay_outline_element_model_trans(
            app,
            element_id,
            app_plugin_overlay_argb(rgb),
            fill_alpha > 0 ? 255 - (fill_alpha > 255 ? 255 : fill_alpha) : -1);
    return app->entity_overlay_count - before;
}

static int
app_plugin_draw_line(void* user, int x0, int y0, int x1, int y1, uint32_t rgb)
{
    struct App* app = (struct App*)user;
    int before;

    assert(app);
    before = app->entity_overlay_count;
    app_overlay_push_segment(app, x0, y0, x1, y1, app_plugin_overlay_argb(rgb));
    return app->entity_overlay_count - before;
}

/* Centred on x with y as the baseline: that is what the layer's TEXT primitive
 * does (torirs_frame.c sets center and baseline unconditionally), so the api
 * offers no alignment knob rather than one that would be ignored. */
static int
app_plugin_draw_text(void* user, int x, int y, char const* text, uint32_t rgb)
{
    struct App* app = (struct App*)user;
    struct UITreeEntityOverlay item;
    int before;

    assert(app);
    assert(text);

    before = app->entity_overlay_count;
    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_TEXT;
    item.x = x;
    item.y = y;
    item.color = rgb;
    item.font_id = app_hitsplat_font_scene_id(app);
    snprintf(item.text, sizeof(item.text), "%s", text);
    app_overlay_push(app, &item);
    return app->entity_overlay_count - before;
}

static int
app_plugin_draw_rect(
    void* user,
    int x,
    int y,
    int w,
    int h,
    uint32_t rgb,
    int fill_alpha)
{
    struct App* app = (struct App*)user;
    struct UITreeEntityOverlay item;
    int before;

    assert(app);
    before = app->entity_overlay_count;

    if( fill_alpha > 0 )
    {
        memset(&item, 0, sizeof(item));
        item.kind = UITREE_ENTITY_OVERLAY_RECT;
        item.x = x;
        item.y = y;
        item.w = w;
        item.h = h;
        item.color = app_plugin_overlay_argb(rgb);
        item.trans = 255 - (fill_alpha > 255 ? 255 : fill_alpha);
        app_overlay_push(app, &item);
    }
    else
    {
        /* The overlay layer has no rectangle OUTLINE primitive, only a filled
         * box and a box-diagonal, so an unfilled rect is four segments. */
        uint32_t const argb = app_plugin_overlay_argb(rgb);
        app_overlay_push_segment(app, x, y, x + w, y, argb);
        app_overlay_push_segment(app, x + w, y, x + w, y + h, argb);
        app_overlay_push_segment(app, x + w, y + h, x, y + h, argb);
        app_overlay_push_segment(app, x, y + h, x, y, argb);
    }
    return app->entity_overlay_count - before;
}

/* ----------------------------------------------------------------- colour */

/*
 * 0xRRGGBB -> the packed HSL a model face is actually coloured with
 * (6-bit hue << 10 | 3-bit saturation << 7 | 7-bit luminance).
 *
 * Delegated to the chrome's copy, which is the ONLY one now. It used to be
 * written out here as well, and two ports of rgbToHSL is one more than the
 * number that can be verified: the quantisation is the whole point -- the
 * ceilings and the `% 63` are what make a chosen colour land on the same
 * palette entry the client's own art does -- and a second copy is a second
 * place for a beam to come out visibly the wrong hue. The chrome's is the one
 * that has a test behind it (test-debug-overlay-visual walks all 32768 entries
 * against the rasteriser's table), so the chrome's is the one that survives.
 */
static int
app_plugin_hsl_from_rgb(void* user, uint32_t rgb)
{
    (void)user;
    return ToriRSChrome_Hsl16FromRgb(rgb);
}

static uint32_t
app_plugin_hsl_to_rgb(void* user, int hsl)
{
    (void)user;
    /* Through the rasteriser's own table, so the round trip agrees with what
     * is on screen rather than with a second conversion of our own. */
    return (uint32_t)ToriDraw_Hsl16ToRgb((uint16_t)(hsl & 0xffff)) & 0xffffffu;
}

/* ------------------------------------------------------------------- menu */

static int
app_plugin_menu_add(void* user, void* cursor, char const* text, int action_id)
{
    struct UIMinimenu* menu = (struct UIMinimenu*)cursor;

    (void)user;
    assert(menu);
    assert(text);

    if( menu->option_count >= UITREE_MINIMENU_MAX_OPTIONS )
        return 0;

    /* pick.kind stays NONE: the row is the plugin's, and the engine's
     * dispatcher must not try to resolve a target for it. Routing happens on
     * the action id, which the host allocated. */
    struct UIMinimenuPick pick;
    memset(&pick, 0, sizeof(pick));
    pick.kind = UI_MINIMENU_PICK_NONE;
    return UIMinimenu_AddOption(menu, text, action_id, -1, pick) ? 1 : 0;
}

/*
 * Hand a freshly built minimenu to the plugins, then re-sort.
 *
 * Called from every site that builds a menu -- the right-click open, the two
 * left-click scratch builds, and the per-frame hover-text rebuild -- because a
 * row a plugin adds has to exist in all of them or the hover line and the menu
 * disagree about what is on offer. The hover pass is flagged so a handler can
 * cheaply opt out of the one that runs every frame.
 *
 * The re-sort is not optional: UIMinimenu_AddOption appends, and rows draw
 * bottom-to-top after SortPriorityActions has put the Cancel/Examine group in
 * its place. Appending after the engine's own sort and not re-running it lands
 * plugin rows in the wrong half of the menu.
 */
static void
app_plugin_menu_build(struct App* app, struct UIMinimenu* menu, int hover_pass)
{
    struct ToriRS_PluginEvMenuBuild ev;

    assert(app);
    assert(menu);

    if( !app->plugins )
        return;

    memset(&ev, 0, sizeof(ev));
    ev.row_count = menu->option_count < TORIRS_PLUGIN_MENU_ROWS_MAX
                       ? menu->option_count
                       : TORIRS_PLUGIN_MENU_ROWS_MAX;

    for( int i = 0; i < ev.row_count; i++ )
    {
        struct UIMinimenuOption const* opt = &menu->options[i];
        struct ToriRS_PluginMenuRow* row = &ev.rows[i];

        row->text = opt->text;
        row->action = UIMinimenu_ActionNormalize(opt->action);
        row->pick_kind = (int)opt->pick.kind;
        row->npc_slot = -1;
        row->player_pid = -1;
        row->target_id = -1;

        /* The pick carries a scene element id; a plugin speaks in server
         * slots and pids, which are the ids that survive a scene rebuild. */
        if( opt->pick.kind == UI_MINIMENU_PICK_NPC && app->world )
        {
            struct WorldEntity_NPC* npc =
                World_NpcGetByElementId(app->world, opt->pick.id, NULL);
            if( npc )
                row->npc_slot = npc->server_slot;
            row->target_id = opt->pick.secondary_id;
        }
        else if( opt->pick.kind == UI_MINIMENU_PICK_PLAYER )
        {
            row->player_pid = opt->pick.secondary_id;
        }
        else if(
            opt->pick.kind == UI_MINIMENU_PICK_SCENERY ||
            opt->pick.kind == UI_MINIMENU_PICK_OBJ )
        {
            row->target_id = opt->pick.secondary_id;
        }
    }

    PluginHost_MenuBuild(app->plugins, menu, &ev, hover_pass != 0);
    UIMinimenu_SortPriorityActions(menu);
}

static struct ToriRS_PluginEngine
app_plugin_engine(struct App* app)
{
    struct ToriRS_PluginEngine engine;

    assert(app);

    memset(&engine, 0, sizeof(engine));
    engine.user = app;
    engine.world_cycle = app_plugin_world_cycle;
    engine.frame_ms = app_plugin_frame_ms;
    engine.local_player = app_plugin_local_player;
    engine.npc_next = app_plugin_npc_next;
    engine.npc_by_slot = app_plugin_npc_by_slot;
    engine.player_next = app_plugin_player_next;
    engine.obj_next = app_plugin_obj_next;
    engine.key_held = app_plugin_key_held;
    engine.hover_tile = app_plugin_hover_tile;
    engine.project = app_plugin_project;
    engine.draw_tile = app_plugin_draw_tile;
    engine.draw_hull = app_plugin_draw_hull;
    engine.draw_line = app_plugin_draw_line;
    engine.draw_text = app_plugin_draw_text;
    engine.draw_rect = app_plugin_draw_rect;
    engine.menu_add = app_plugin_menu_add;
    engine.asset_read = app_plugin_asset_read;
    engine.asset_write = app_plugin_asset_write;
    engine.screenshot = app_plugin_screenshot;
    engine.object_create = app_plugin_object_create;
    engine.object_destroy = app_plugin_object_destroy;
    engine.object_set_model = app_plugin_object_set_model;
    engine.object_recolor = app_plugin_object_recolor;
    engine.object_clear_recolors = app_plugin_object_clear_recolors;
    engine.object_set_anim = app_plugin_object_set_anim;
    engine.object_set_light = app_plugin_object_set_light;
    engine.object_set_position = app_plugin_object_set_position;
    engine.object_set_active = app_plugin_object_set_active;
    engine.object_ready = app_plugin_object_ready;
    engine.hsl_from_rgb = app_plugin_hsl_from_rgb;
    engine.hsl_to_rgb = app_plugin_hsl_to_rgb;
    return engine;
}
