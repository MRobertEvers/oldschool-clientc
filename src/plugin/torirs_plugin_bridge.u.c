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
app_plugin_screenshot(
    void* user,
    char const* plugin,
    char const* dir,
    char const* name,
    char* out_path,
    int out_path_size);
static int app_plugin_model_publish(void* user, int model, void const* data, int size);
static void app_plugin_model_release(void* user, int model);
static int app_plugin_mesh_create(void* user);
static void app_plugin_mesh_destroy(void* user, int mesh);
static void app_plugin_mesh_clear(void* user, int mesh);
static int app_plugin_mesh_vertex(void* user, int mesh, int x, int y, int z);
static int app_plugin_mesh_face(void* user, int mesh, int a, int b, int c, int hsl, int alpha);
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

static void
app_plugin_fill_loc(
    struct App* app,
    struct WorldEntity_Scenery const* scenery,
    struct ToriRS_PluginLocSnap* out)
{
    assert(app);
    assert(scenery);
    assert(out);

    memset(out, 0, sizeof(*out));
    out->loc_id = scenery->loc_id;
    out->tile_x = app->world->_base_tile_x + scenery->grid_position.x;
    out->tile_z = app->world->_base_tile_z + scenery->grid_position.z;
    out->level = scenery->grid_position.level;
    out->size_x = scenery->size_x > 0 ? scenery->size_x : 1;
    out->size_z = scenery->size_z > 0 ? scenery->size_z : 1;
    out->shape = scenery->shape;
    out->angle = scenery->angle;
    out->element_id = scenery->element_id;
    out->interactive = scenery->interactive;
    snprintf(out->name, sizeof(out->name), "%s", scenery->info->name);

    /* The ops are a facet array with a per-slot name; a slot with no name is
     * an op the loc does not offer. Packed to a bitmask here because that is
     * the only question a plugin asks of them -- the TEXT of an op is the
     * minimenu's business, and a plugin that wants it reads the menu build. */
    for( int i = 0; i < 5; i++ )
        if( scenery->info->actions[i].name[0] != '\0' )
            out->visible_ops |= (uint8_t)(1u << i);
}

/*
 * Every loc in the loaded scene.
 *
 * SCENE-SCOPED, and the snapshot says so: the pool is rebuilt from nothing on
 * each scene load, so there is no iterator that survives one. That is not a
 * limitation to work around -- a loc has no server-side identity a client can
 * hold on to, unlike an npc's slot, so its tile IS its identity and a plugin
 * that wants to remember one remembers that.
 */
static int
app_plugin_loc_next(void* user, int iter, struct ToriRS_PluginLocSnap* out)
{
    struct App* app = (struct App*)user;
    struct World_EntityPool* pool;
    int at;

    assert(app);
    assert(out);

    if( !app->world )
        return -1;
    pool = &app->world->entities.scenery;

    at = iter < 0 ? World_EntityPoolHead(pool) : World_EntityPoolNext(pool, iter);
    while( at != WORLD_ENTITY_NIL )
    {
        struct WorldEntity_Scenery* scenery = World_EntityPoolGet(pool, at);
        if( scenery )
        {
            app_plugin_fill_loc(app, scenery, out);
            return at;
        }
        at = World_EntityPoolNext(pool, at);
    }
    return -1;
}

/* Defined below, beside the other overlay helpers; the highlight resolver
 * above it wants the same anchor a name would hang off. */
static int app_plugin_element_height(void* user, int element_id);

/* ---------------------------------------------------------- highlights ---
 *
 * The cache's highlight groups, resolved to things on the screen.
 *
 * The groups name SUBJECTS -- "every loc of type 23138", "the tile at this
 * coord", "the npc with this uid" -- and resolving one means finding what in
 * the scene that is. That walk is here, not in the plugin, for the reason
 * every other snapshot is: the entity pools are the engine's, and a plugin
 * that had to walk them itself would need an api for each pool anyway and
 * would get the coord packing wrong once per plugin instead of once.
 */

/** Nothing to resolve for a group that is off. */
static bool
app_plugin_highlight_begin(
    struct App* app,
    enum RS_HighlightKind kind,
    int group,
    struct ToriRS_PluginHighlightItem* proto)
{
    struct RS_HighlightStyle const* style;

    if( !RS_HighlightGroupLive(&app->host.highlight, kind, group) )
        return false;

    style = &app->host.highlight.style[kind][group];
    memset(proto, 0, sizeof(*proto));
    proto->element_id = -1;
    proto->size_x = 1;
    proto->size_z = 1;
    proto->rgb = (uint32_t)style->colour & 0x00FFFFFFu;
    proto->opacity = style->opacity;
    proto->outline_width = style->outline_width;
    proto->flags = style->flags;
    return true;
}

static struct ToriRS_PluginHighlightItem*
app_plugin_highlight_push(struct App* app, struct ToriRS_PluginHighlightItem const* proto)
{
    struct ToriRS_PluginHighlightItem* item;

    if( app->plugin_highlight_count >= APP_PLUGIN_HIGHLIGHTS_MAX )
        return NULL;
    item = &app->plugin_highlights[app->plugin_highlight_count++];
    *item = *proto;
    return item;
}

/* The entity pools the rebuild walks, one per pass that has a pool. */
enum
{
    APP_PLUGIN_HL_POOL_NPC = 0,
    APP_PLUGIN_HL_POOL_PLAYER,
    APP_PLUGIN_HL_POOL_LOC,
    APP_PLUGIN_HL_POOL_OBJ,
    APP_PLUGIN_HL_POOL_COUNT
};

/*
 * Does any name-keyed subject belong to this kind?
 *
 * PLAYER and OPGROUP share the one `named` list, so "is anything named" has to
 * ask which kind rather than just whether the list is non-empty.
 *
 * `kind` is an int because that is what the member stores -- the enum's
 * underlying type is unsigned here, and comparing the two directly is a
 * signedness warning for no gain.
 */
static bool
app_plugin_highlight_named_any(struct RS_HighlightState const* hl, int kind)
{
    assert(hl);

    for( int i = 0; i < hl->named_count; i++ )
        if( hl->named[i].kind == kind )
            return true;
    return false;
}

/*
 * Does an OP GROUP name this thing?
 *
 * The 7040 family's subject is a right-click NAME and applies across the pools
 * -- `HIGHLIGHT_OPGROUP_ON("Cow", 9)` is about every Cow, npc or not -- so
 * every entity pass
 * asks this in addition to its own kind's members. Returns the group, or -1.
 *
 * Nothing in this cache calls the family's ON, so this walk is over an empty
 * list in practice; it is here because the state it reads is real and the
 * alternative is a kind that records subjects nobody draws.
 */
static int
app_plugin_opgroup_group(struct RS_HighlightState const* hl, char const* name)
{
    for( int i = 0; i < hl->named_count; i++ )
        if( hl->named[i].kind == RS_HIGHLIGHT_OPGROUP &&
            strcmp(hl->named[i].name, name) == 0 )
            return hl->named[i].group;
    return -1;
}

/*
 * Which pools have anything to look for.
 *
 * A pool walk exists only to test that pool's entities against a subject list,
 * so an empty list makes the entire walk dead work -- and with nothing
 * highlighted, which is the ordinary state, every list is empty while the
 * scenery pool alone is ~23k entities to step through, once per frame, to
 * produce nothing.
 *
 * Answered per pool rather than once for the whole rebuild: tagging a single
 * npc must not put the loc and obj pools back on the frame.
 *
 * This cannot be hoisted to `hl->revision` instead. The resolved list carries
 * draw positions, and those move every frame without anything being said -- a
 * revision cache would pin a highlight to where its npc stood when the op was
 * picked, which is the same bug the npc pass avoids by leaving the member's
 * coord out of the match.
 *
 * OPGROUP is keyed by right-click NAME and applies across the npc, loc and obj
 * pools (see app_plugin_opgroup_group), so it re-arms all three.
 */
static void
app_plugin_highlight_pools_wanted(
    struct RS_HighlightState const* hl,
    bool want[APP_PLUGIN_HL_POOL_COUNT])
{
    assert(hl);
    assert(want);

    bool const opgroup_any = app_plugin_highlight_named_any(hl, RS_HIGHLIGHT_OPGROUP);

    want[APP_PLUGIN_HL_POOL_NPC] = hl->member_count[RS_HIGHLIGHT_NPC] > 0 ||
                                   hl->member_count[RS_HIGHLIGHT_NPCTYPE] > 0 ||
                                   opgroup_any;
    want[APP_PLUGIN_HL_POOL_PLAYER] =
        app_plugin_highlight_named_any(hl, RS_HIGHLIGHT_PLAYER);
    want[APP_PLUGIN_HL_POOL_LOC] = hl->member_count[RS_HIGHLIGHT_LOC] > 0 ||
                                   hl->member_count[RS_HIGHLIGHT_LOCTYPE] > 0 ||
                                   opgroup_any;
    want[APP_PLUGIN_HL_POOL_OBJ] = hl->member_count[RS_HIGHLIGHT_OBJ] > 0 ||
                                   hl->member_count[RS_HIGHLIGHT_OBJTYPE] > 0 ||
                                   opgroup_any;
}

/*
 * Rebuild the resolved list.
 *
 * One pass per kind, and each pass walks the pool it needs at most once: a
 * member list is short (the largest real one is the 109 loctypes the cache
 * marks) and the pools are long, so the inner test is against the members and
 * the outer walk is the pool.
 *
 * `want` skips the pools with nothing to look for; passing all-true walks
 * every pool, which is what the debug cross-check in the caller compares
 * against.
 */
/*
 * Is the cached LOC answer still the right one?
 *
 * Two inputs, and nothing else. `revision` covers the member lists AND the
 * styles -- a SETUP writes the style and bumps it in the same breath. The
 * pool epoch covers which scenery exists and what it is, including a loc
 * that morphs, because re-registering an element bumps it too.
 *
 * What deliberately is NOT an input is time: a loc does not move. That is
 * the whole reason this pool can be cached when the npc and player pools
 * cannot -- their draw positions change every frame with nothing said.
 */
static bool
app_plugin_highlight_loc_cache_fresh(struct App const* app)
{
    assert(app);
    assert(app->world);

    return app->plugin_highlight_loc_valid &&
        app->plugin_highlight_loc_revision == app->host.highlight.revision &&
        app->plugin_highlight_loc_epoch == app->world->entities.scenery.epoch;
}

/* Push one resolved loc into the cache. Bounded by the same cap as the live
 * list, so a cache that fills cannot outrun what the list could hold. */
static struct ToriRS_PluginHighlightItem*
app_plugin_highlight_loc_cache_push(
    struct App* app,
    struct ToriRS_PluginHighlightItem const* proto)
{
    struct ToriRS_PluginHighlightItem* item;

    if( app->plugin_highlight_loc_count >= APP_PLUGIN_HIGHLIGHTS_MAX )
        return NULL;
    item = &app->plugin_highlight_loc[app->plugin_highlight_loc_count++];
    *item = *proto;
    return item;
}

/*
 * Resolve every LOC highlight in the scene, into the cache.
 *
 * This is the walk that used to run every frame: the whole scenery pool --
 * ~23k entities in an ordinary map square -- tested against the loc and
 * loctype member lists. With nothing highlighted, which is the ordinary
 * state, it stepped all of them to produce nothing.
 *
 * Runs only when app_plugin_highlight_loc_cache_fresh says the answer
 * could have changed.
 */
static void
app_plugin_highlight_loc_cache_build(struct App* app)
{
    struct RS_HighlightState const* hl = &app->host.highlight;
    struct ToriRS_PluginHighlightItem proto;

    assert(app);
    assert(app->world);

    /* Asked ONCE, not once per entity.
     *
     * app_plugin_opgroup_group is a linear scan of the named list, and every
     * pass called it for every entity it walked -- so the cost was
     * entities x named, paid to discover that the list holds no OPGROUP at
     * all, which is its normal state (nothing in this cache calls the
     * family's ON). Hoisted, the per-entity cost is one predictable branch. */
    bool const has_opgroup = app_plugin_highlight_named_any(hl, RS_HIGHLIGHT_OPGROUP);

    app->plugin_highlight_loc_count = 0;

    struct World_EntityPool* pool = &app->world->entities.scenery;
    for( int at = World_EntityPoolHead(pool); at != WORLD_ENTITY_NIL;
         at = World_EntityPoolNext(pool, at) )
    {
        struct WorldEntity_Scenery* loc = World_EntityPoolGet(pool, at);
        int tile_x;
        int tile_z;
        int coord;

        if( !loc || loc->element_id < 0 )
            continue;
        tile_x = app->world->_base_tile_x + loc->grid_position.x;
        tile_z = app->world->_base_tile_z + loc->grid_position.z;
        coord = RS_HIGHLIGHT_COORD(loc->grid_position.level, tile_x, tile_z);

        for( int kind = 0; kind < 2; kind++ )
        {
            enum RS_HighlightKind const k =
                kind == 0 ? RS_HIGHLIGHT_LOCTYPE : RS_HIGHLIGHT_LOC;
            for( int i = 0; i < hl->member_count[k]; i++ )
            {
                struct RS_HighlightMember const* m = &hl->member[k][i];
                if( m->key != loc->loc_id )
                    continue;
                /* The placed form pins a coord as well as a type; the type
                 * form marks every instance. */
                if( k == RS_HIGHLIGHT_LOC && m->coord != coord )
                    continue;
                if( !app_plugin_highlight_begin(app, k, m->group, &proto) )
                    continue;
                proto.kind = TORIRS_PLUGIN_HL_LOC;
                proto.element_id = loc->element_id;
                proto.overhead_height = app_plugin_element_height(app, loc->element_id);
                proto.fine_x = (loc->grid_position.x * 128) + 64;
                proto.fine_z = (loc->grid_position.z * 128) + 64;
                snprintf(proto.name, sizeof(proto.name), "%s", loc->info->name);
                proto.tile_x = tile_x;
                proto.tile_z = tile_z;
                proto.level = loc->grid_position.level;
                proto.size_x = loc->size_x > 0 ? loc->size_x : 1;
                proto.size_z = loc->size_z > 0 ? loc->size_z : 1;
                proto.flags |= m->flags;
                if( !app_plugin_highlight_loc_cache_push(app, &proto) )
                    return;
            }
        }

        {
            int const group =
                    has_opgroup ? app_plugin_opgroup_group(hl, loc->info->name) : -1;
            if( group >= 0 &&
                app_plugin_highlight_begin(app, RS_HIGHLIGHT_OPGROUP, group, &proto) )
            {
                proto.kind = TORIRS_PLUGIN_HL_LOC;
                proto.element_id = loc->element_id;
                proto.overhead_height = app_plugin_element_height(app, loc->element_id);
                proto.fine_x = (loc->grid_position.x * 128) + 64;
                proto.fine_z = (loc->grid_position.z * 128) + 64;
                snprintf(proto.name, sizeof(proto.name), "%s", loc->info->name);
                proto.tile_x = tile_x;
                proto.tile_z = tile_z;
                proto.level = loc->grid_position.level;
                proto.size_x = loc->size_x > 0 ? loc->size_x : 1;
                proto.size_z = loc->size_z > 0 ? loc->size_z : 1;
                if( !app_plugin_highlight_loc_cache_push(app, &proto) )
                    return;
            }
        }
    }

}

static void
app_plugin_highlights_rebuild_pools(
    struct App* app,
    bool const want[APP_PLUGIN_HL_POOL_COUNT])
{
    struct RS_HighlightState const* hl;
    struct ToriRS_PluginHighlightItem proto;

    assert(app);
    assert(want);

    app->plugin_highlight_count = 0;
    if( !app->world )
        return;
    hl = &app->host.highlight;

    bool const want_npc = want[APP_PLUGIN_HL_POOL_NPC];
    bool const want_player = want[APP_PLUGIN_HL_POOL_PLAYER];
    bool const want_loc = want[APP_PLUGIN_HL_POOL_LOC];
    bool const want_obj = want[APP_PLUGIN_HL_POOL_OBJ];

    /* Asked ONCE, not once per entity.
     *
     * app_plugin_opgroup_group is a linear scan of the named list, and every
     * pass called it for every entity it walked -- so the cost was
     * entities x named, paid to discover that the list holds no OPGROUP at
     * all, which is its normal state (nothing in this cache calls the
     * family's ON). Hoisted, the per-entity cost is one predictable branch. */
    bool const has_opgroup = app_plugin_highlight_named_any(hl, RS_HIGHLIGHT_OPGROUP);

    /* ---- tiles: the member IS the thing, no pool to walk. ---- */
    for( int i = 0; i < hl->member_count[RS_HIGHLIGHT_TILE]; i++ )
    {
        struct RS_HighlightMember const* m = &hl->member[RS_HIGHLIGHT_TILE][i];
        if( !app_plugin_highlight_begin(app, RS_HIGHLIGHT_TILE, m->group, &proto) )
            continue;
        proto.kind = TORIRS_PLUGIN_HL_TILE;
        proto.tile_x = RS_HIGHLIGHT_COORD_X(m->coord);
        proto.tile_z = RS_HIGHLIGHT_COORD_Z(m->coord);
        proto.level = RS_HIGHLIGHT_COORD_PLANE(m->coord);
        proto.fine_x = ((proto.tile_x - app->world->_base_tile_x) * 128) + 64;
        proto.fine_z = ((proto.tile_z - app->world->_base_tile_z) * 128) + 64;
        proto.flags |= m->flags;
        if( !app_plugin_highlight_push(app, &proto) )
            return;
    }

    /* ---- npcs: by type, and by UID.
     *
     * `highlight_npc_on` names an npc uid, and the uid it names came from
     * `_6751` -- the client-op context, which this client answers with the
     * npc's SERVER SLOT (see RS_ClientOpContext::uid). The value never leaves
     * the client, so the only requirement is that the two sides agree, and
     * they are the only two sides there are.
     *
     * The member's coord is deliberately NOT part of the match. It is the tile
     * the npc stood on when the op was picked, and matching on it would drop
     * the highlight the moment the npc took a step -- which is the opposite of
     * what tagging one is for. ---- */
    if( want_npc )
    {
        struct World_EntityPool* pool = &app->world->entities.npc;
        for( int at = World_EntityPoolHead(pool); at != WORLD_ENTITY_NIL;
             at = World_EntityPoolNext(pool, at) )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, at);
            int tile_x;
            int tile_z;

            if( !npc || npc->element_id < 0 )
                continue;
            tile_x = app->world->_base_tile_x + npc->grid_position.x;
            tile_z = app->world->_base_tile_z + npc->grid_position.z;

            for( int kind = 0; kind < 2; kind++ )
            {
                enum RS_HighlightKind const k =
                    kind == 0 ? RS_HIGHLIGHT_NPCTYPE : RS_HIGHLIGHT_NPC;
                for( int i = 0; i < hl->member_count[k]; i++ )
                {
                    struct RS_HighlightMember const* m = &hl->member[k][i];
                    bool const hit = k == RS_HIGHLIGHT_NPCTYPE
                                         ? (m->key == npc->npc_id ||
                                            m->key == npc->base_npc_id)
                                         : m->key == npc->server_slot;
                    if( !hit )
                        continue;
                    if( !app_plugin_highlight_begin(app, k, m->group, &proto) )
                        continue;
                    proto.kind = TORIRS_PLUGIN_HL_NPC;
                    proto.element_id = npc->element_id;
                    proto.overhead_height = app_plugin_element_height(app, npc->element_id);
                    proto.fine_x = (int)npc->draw_position.x;
                    proto.fine_z = (int)npc->draw_position.z;
                    snprintf(proto.name, sizeof(proto.name), "%s", npc->name);
                    proto.tile_x = tile_x;
                    proto.tile_z = tile_z;
                    proto.level = npc->grid_position.level;
                    proto.size_x = npc->size > 0 ? npc->size : 1;
                    proto.size_z = proto.size_x;
                    proto.flags |= m->flags;
                    if( !app_plugin_highlight_push(app, &proto) )
                        return;
                }
            }

            /* ...and by right-click NAME, which is the OP GROUP kind. */
            {
                int const group =
                    has_opgroup ? app_plugin_opgroup_group(hl, npc->name) : -1;
                if( group >= 0 &&
                    app_plugin_highlight_begin(app, RS_HIGHLIGHT_OPGROUP, group, &proto) )
                {
                    proto.kind = TORIRS_PLUGIN_HL_NPC;
                    proto.element_id = npc->element_id;
                    proto.overhead_height = app_plugin_element_height(app, npc->element_id);
                    proto.fine_x = (int)npc->draw_position.x;
                    proto.fine_z = (int)npc->draw_position.z;
                    snprintf(proto.name, sizeof(proto.name), "%s", npc->name);
                    proto.tile_x = tile_x;
                    proto.tile_z = tile_z;
                    proto.level = npc->grid_position.level;
                    proto.size_x = npc->size > 0 ? npc->size : 1;
                    proto.size_z = proto.size_x;
                    if( !app_plugin_highlight_push(app, &proto) )
                        return;
                }
            }
        }
    }

    /* ---- players: by NAME.
     *
     * The only kind whose subject is a string (see RS_HighlightNamedMember).
     * The name the cache put in the group came from `_6900`, this client's own
     * report of a player's name, so the compare is exact -- and the local
     * player is in this pool too, which is what makes the developer op's
     * "highlight yourself" work. ---- */
    if( want_player )
    {
        struct World_EntityPool* pool = &app->world->entities.player;
        for( int at = World_EntityPoolHead(pool); at != WORLD_ENTITY_NIL;
             at = World_EntityPoolNext(pool, at) )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(pool, at);
            if( !player || player->element_id < 0 )
                continue;

            for( int i = 0; i < hl->named_count; i++ )
            {
                struct RS_HighlightNamedMember const* m = &hl->named[i];
                if( m->kind != RS_HIGHLIGHT_PLAYER || strcmp(m->name, player->name) != 0 )
                    continue;
                if( !app_plugin_highlight_begin(app, RS_HIGHLIGHT_PLAYER, m->group, &proto) )
                    continue;
                proto.kind = TORIRS_PLUGIN_HL_PLAYER;
                proto.element_id = player->element_id;
                proto.overhead_height = app_plugin_element_height(app, player->element_id);
                proto.fine_x = (int)player->draw_position.x;
                proto.fine_z = (int)player->draw_position.z;
                snprintf(proto.name, sizeof(proto.name), "%s", player->name);
                proto.tile_x = app->world->_base_tile_x + player->grid_position.x;
                proto.tile_z = app->world->_base_tile_z + player->grid_position.z;
                proto.level = player->grid_position.level;
                if( !app_plugin_highlight_push(app, &proto) )
                    return;
            }
        }
    }

    /* ---- locs: by type anywhere, or by type at one coord.
     *
     * Answered from the cache. The walk itself is in
     * app_plugin_highlight_loc_cache_build and runs only when the highlight
     * state or the scenery set has changed. ---- */
    if( want_loc )
    {
        if( !app_plugin_highlight_loc_cache_fresh(app) )
        {
            app_plugin_highlight_loc_cache_build(app);
            app->plugin_highlight_loc_revision = hl->revision;
            app->plugin_highlight_loc_epoch = app->world->entities.scenery.epoch;
            app->plugin_highlight_loc_valid = true;
        }
        for( int i = 0; i < app->plugin_highlight_loc_count; i++ )
        {
            struct ToriRS_PluginHighlightItem* out =
                app_plugin_highlight_push(app, &app->plugin_highlight_loc[i]);
            if( !out )
                return;
            /* Recomputed rather than cached: a loc's model can finish loading
             * after the highlight resolved, and the height would otherwise stay
             * pinned at whatever it was while nothing was loaded yet. */
            out->overhead_height = app_plugin_element_height(app, out->element_id);
        }
    }

    /* ---- ground items ---- */
    if( want_obj )
    {
        struct World_EntityPool* pool = &app->world->entities.obj_stack;
        for( int at = World_EntityPoolHead(pool); at != WORLD_ENTITY_NIL;
             at = World_EntityPoolNext(pool, at) )
        {
            struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, at);
            int tile_x;
            int tile_z;
            int coord;

            if( !stack || stack->element_id < 0 )
                continue;
            tile_x = app->world->_base_tile_x + stack->grid_position.x;
            tile_z = app->world->_base_tile_z + stack->grid_position.z;
            coord = RS_HIGHLIGHT_COORD(stack->grid_position.level, tile_x, tile_z);

            for( int kind = 0; kind < 2; kind++ )
            {
                enum RS_HighlightKind const k =
                    kind == 0 ? RS_HIGHLIGHT_OBJTYPE : RS_HIGHLIGHT_OBJ;
                for( int i = 0; i < hl->member_count[k]; i++ )
                {
                    struct RS_HighlightMember const* m = &hl->member[k][i];
                    if( m->key != stack->obj_id )
                        continue;
                    if( k == RS_HIGHLIGHT_OBJ && m->coord != coord )
                        continue;
                    if( !app_plugin_highlight_begin(app, k, m->group, &proto) )
                        continue;
                    proto.kind = TORIRS_PLUGIN_HL_OBJ;
                    proto.element_id = stack->element_id;
                    proto.overhead_height = app_plugin_element_height(app, stack->element_id);
                    proto.fine_x = (stack->grid_position.x * 128) + 64;
                    proto.fine_z = (stack->grid_position.z * 128) + 64;
                    snprintf(proto.name, sizeof(proto.name), "%s", stack->name);
                    proto.tile_x = tile_x;
                    proto.tile_z = tile_z;
                    proto.level = stack->grid_position.level;
                    proto.flags |= m->flags;
                    if( !app_plugin_highlight_push(app, &proto) )
                        return;
                }
            }

            {
                int const group =
                    has_opgroup ? app_plugin_opgroup_group(hl, stack->name) : -1;
                if( group >= 0 &&
                    app_plugin_highlight_begin(app, RS_HIGHLIGHT_OPGROUP, group, &proto) )
                {
                    proto.kind = TORIRS_PLUGIN_HL_OBJ;
                    proto.element_id = stack->element_id;
                    proto.overhead_height = app_plugin_element_height(app, stack->element_id);
                    proto.fine_x = (stack->grid_position.x * 128) + 64;
                    proto.fine_z = (stack->grid_position.z * 128) + 64;
                    snprintf(proto.name, sizeof(proto.name), "%s", stack->name);
                    proto.tile_x = tile_x;
                    proto.tile_z = tile_z;
                    proto.level = stack->grid_position.level;
                    if( !app_plugin_highlight_push(app, &proto) )
                        return;
                }
            }
        }
    }
}

/*
 * Is the highlight debug channel on?
 *
 * Cached: this is asked once per frame per caller, and getenv on this platform
 * is a linear scan of the environment block.
 */
static bool
app_plugin_highlight_debug(void)
{
    static int on = -1;

    if( on < 0 )
        on = getenv("TORIRS_HIGHLIGHT_DEBUG") != NULL;
    return on != 0;
}

/*
 * Rebuild, walking only the pools with something to look for.
 *
 * Under TORIRS_HIGHLIGHT_DEBUG the rebuild is repeated with every pool armed
 * and the two results compared, so "the gate skipped a pool that had a
 * highlight in it" reports itself instead of looking like a highlight the
 * script never asked for -- which is indistinguishable from outside, and is
 * exactly the failure this whole debug channel exists for. The ungated result
 * is the one left in place: if they ever disagree, the drawn frame stays
 * correct and only the log says so.
 */
static void
app_plugin_highlights_rebuild(struct App* app)
{
    bool want[APP_PLUGIN_HL_POOL_COUNT];

    assert(app);

    app_plugin_highlight_pools_wanted(&app->host.highlight, want);
    app_plugin_highlights_rebuild_pools(app, want);

    if( app_plugin_highlight_debug() )
    {
        int const gated = app->plugin_highlight_count;
        bool const all[APP_PLUGIN_HL_POOL_COUNT] = { true, true, true, true };

        app_plugin_highlights_rebuild_pools(app, all);
        if( gated != app->plugin_highlight_count )
            fprintf(
                stderr,
                "highlight-gate: MISMATCH gated=%d ungated=%d "
                "(npc %d, player %d, loc %d, obj %d)\n",
                gated,
                app->plugin_highlight_count,
                (int)want[APP_PLUGIN_HL_POOL_NPC],
                (int)want[APP_PLUGIN_HL_POOL_PLAYER],
                (int)want[APP_PLUGIN_HL_POOL_LOC],
                (int)want[APP_PLUGIN_HL_POOL_OBJ]);

        /* Which pools the gate left armed, and how long the list each armed
         * pool tests against is. An armed pool that resolves to nothing still
         * pays its whole walk -- entities x members -- so this is the number
         * that says whether the gate reached the cost or only the tail of it. */
        static int last_state = -1;
        int const state =
            (int)want[APP_PLUGIN_HL_POOL_NPC] | ((int)want[APP_PLUGIN_HL_POOL_PLAYER] << 1) |
            ((int)want[APP_PLUGIN_HL_POOL_LOC] << 2) | ((int)want[APP_PLUGIN_HL_POOL_OBJ] << 3) |
            (app->host.highlight.revision << 4);
        if( state != last_state )
        {
            struct RS_HighlightState const* hl = &app->host.highlight;
            last_state = state;
            fprintf(
                stderr,
                "highlight-gate: armed npc=%d player=%d loc=%d obj=%d | members"
                " npc=%d npctype=%d loc=%d loctype=%d obj=%d objtype=%d tile=%d"
                " named=%d rev=%d\n",
                (int)want[APP_PLUGIN_HL_POOL_NPC],
                (int)want[APP_PLUGIN_HL_POOL_PLAYER],
                (int)want[APP_PLUGIN_HL_POOL_LOC],
                (int)want[APP_PLUGIN_HL_POOL_OBJ],
                hl->member_count[RS_HIGHLIGHT_NPC],
                hl->member_count[RS_HIGHLIGHT_NPCTYPE],
                hl->member_count[RS_HIGHLIGHT_LOC],
                hl->member_count[RS_HIGHLIGHT_LOCTYPE],
                hl->member_count[RS_HIGHLIGHT_OBJ],
                hl->member_count[RS_HIGHLIGHT_OBJTYPE],
                hl->member_count[RS_HIGHLIGHT_TILE],
                hl->named_count,
                hl->revision);
        }
    }
}

/*
 * What the groups RESOLVED to, on TORIRS_HIGHLIGHT_DEBUG.
 *
 * The op trace says what the cache asked for and the member counts say what
 * was recorded; neither says whether a recorded subject was FOUND in the
 * scene. A player highlight keyed on a name nobody in the pool answers to, or
 * a loctype that is not in this map square, is recorded and drawn as nothing,
 * and the two look identical from the opcode side. Printed only when the tally
 * changes -- the walk runs every frame.
 */
static void
app_plugin_highlights_report(struct App* app)
{
    static int last_signature = -1;
    int tally[5] = { 0, 0, 0, 0, 0 };
    int signature = 0;

    assert(app);
    if( !app_plugin_highlight_debug() )
        return;

    for( int i = 0; i < app->plugin_highlight_count; i++ )
    {
        int const kind = (int)app->plugin_highlights[i].kind;
        if( kind >= 0 && kind < (int)(sizeof(tally) / sizeof(tally[0])) )
            tally[kind]++;
    }
    for( int i = 0; i < (int)(sizeof(tally) / sizeof(tally[0])); i++ )
        signature = signature * 251 + tally[i];
    if( signature == last_signature )
        return;
    last_signature = signature;
    fprintf(
        stderr,
        "highlight-resolve: %d drawn (tile %d, npc %d, loc %d, obj %d, player %d)\n",
        app->plugin_highlight_count,
        tally[TORIRS_PLUGIN_HL_TILE],
        tally[TORIRS_PLUGIN_HL_NPC],
        tally[TORIRS_PLUGIN_HL_LOC],
        tally[TORIRS_PLUGIN_HL_OBJ],
        tally[TORIRS_PLUGIN_HL_PLAYER]);
}

static int
app_plugin_highlight_next(void* user, int iter, struct ToriRS_PluginHighlightItem* out)
{
    struct App* app = (struct App*)user;
    int next;

    assert(app);
    assert(out);

    /* The start of a walk is the only place the list is rebuilt: see the api
     * declaration. A cursor into a list that moved underneath it would skip or
     * repeat, so the rebuild must not happen mid-walk. */
    if( iter < 0 )
    {
        app_plugin_highlights_rebuild(app);
        app_plugin_highlights_report(app);
    }

    next = iter + 1;
    if( next >= app->plugin_highlight_count )
        return -1;
    *out = app->plugin_highlights[next];
    return next;
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

static void
app_plugin_notify(void* user, char const* text)
{
    struct App* app = (struct App*)user;
    assert(app);
    assert(text);
    RS_CS2Host_ChatAdd(&app->host, RS_CHAT_TYPE_GAME, NULL, NULL, text);
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

/*
 * The nearest thing under the pointer, out of the frame's pickset.
 *
 * The pickset is already ordered nearest-first -- it is the same list the
 * minimenu consumes, in the same order -- so "the entity the cursor is on" is
 * the first entry that is not terrain. Terrain is skipped rather than reported
 * as a kind of its own: the ground under the pointer is hover_tile's question,
 * it is answered even over open grass, and a caller asking this one wants to
 * know whether there is a THING there.
 *
 * Projectiles are skipped too. They are picked (they are scene elements like
 * any other) but nothing can be done with one, so offering it as the hovered
 * entity would make an arrow in flight steal the highlight off the npc it is
 * flying at.
 */
static int
app_plugin_hover_entity(void* user, struct ToriRS_PluginHoverEntity* out)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(out);

    if( !app->world )
        return 0;

    for( int i = 0; i < app->world_pickset.count; i++ )
    {
        struct World_Picked const* hit = &app->world_pickset.items[i];
        int kind;

        switch( hit->type )
        {
        case WORLD_PICK_SCENERY:
            kind = TORIRS_PLUGIN_HOVER_SCENERY;
            break;
        case WORLD_PICK_NPC:
            kind = TORIRS_PLUGIN_HOVER_NPC;
            break;
        case WORLD_PICK_PLAYER:
            kind = TORIRS_PLUGIN_HOVER_PLAYER;
            break;
        case WORLD_PICK_OBJSTACK:
            kind = TORIRS_PLUGIN_HOVER_OBJ;
            break;
        default:
            continue;
        }

        memset(out, 0, sizeof(*out));
        out->kind = kind;
        out->element_id = hit->element_id;
        out->tile_x = app->world->_base_tile_x + hit->tile_x;
        out->tile_z = app->world->_base_tile_z + hit->tile_z;
        out->level =
            World_TerrainWalkLevel(app->world, hit->tile_x, hit->tile_z, hit->tile_level);
        return 1;
    }
    return 0;
}

/*
 * The overhead anchor, shared with the client's own health bars and chat
 * heads: app_entity_model_height is the same call they make.
 *
 * The reference's `logicalHeight` default is applied here rather than left to
 * the caller, for the reason it exists there: an entity whose model has not
 * been built yet reports no bounds, and a name that collapsed to the floor for
 * the first few frames of every spawn would be worse than one that starts
 * slightly high and settles. A type's own `height` (npc opcode 124) is not
 * consulted, because a plugin names an ELEMENT and not an npc; the two differ
 * only for the handful of records that state one.
 */
static int
app_plugin_element_height(void* user, int element_id)
{
    struct App* app = (struct App*)user;
    int height;

    assert(app);

    if( element_id < 0 || !app->scene || !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return 0;
    height = app_entity_model_height(app, element_id);
    return height > 0 ? height : APP_OVERLAY_DEFAULT_LOGICAL_HEIGHT;
}

/*
 * The client's var state, read-only.
 *
 * VarPManager_GetVarbit / _GetVarp already answer 0 for an id this revision
 * does not define, which is the contract this api states -- so there is no
 * range test here to duplicate and get subtly different.
 */
static int
app_plugin_varbit(void* user, int varbit_id)
{
    struct App* app = (struct App*)user;
    assert(app);
    return VarPManager_GetVarbit(&app->varps, varbit_id);
}

static int
app_plugin_varp(void* user, int varp_id)
{
    struct App* app = (struct App*)user;
    assert(app);
    return VarPManager_GetVarp(&app->varps, varp_id);
}

/* ------------------------------------------------- the client's feature flags */

/*
 * What a plugin may reach of src/features/features.h and the revision's
 * `[camera]` profile, and nothing else.
 *
 * THE LIST IS THE WHOLE ENFORCEMENT. A flag absent from FEATURE_FLAGS has no
 * key, so no plugin can name it and none can be talked into publishing it --
 * which is the point, because most of that table is not a preference at all.
 * The pathing mode, the approach model, npc_approach_uses_size, the
 * under-target route-out, the op-click nearest range and its ranking, the
 * ground-click nearest model and its unbounded extension, the route window,
 * symmetric PvP line of sight and the run-energy model are read by BOTH halves
 * of this tree -- the client for its local BFS and the mock server for every
 * route it answers with. A client holding a different value from its server
 * does not get its own experience, it gets a broken one: tiles flagged inside
 * a large npc, routes the server will not honour, an energy bar draining at a
 * rate nothing else believes. `era` is the same thing one level up, since it
 * carries all of them at once. None of those are here.
 *
 * `varbit_interface_resizing` is absent for a different reason: it is read
 * exactly once, at the boot that seeds the varbit, so a control over it would
 * do nothing until the next launch and say nothing about why.
 *
 * Every flag is applied LIVE. The call sites read through `app->features` and
 * `app->revconfig_profile.camera` each time they need an answer, so the two
 * that had latched a copy at boot -- the audio device's monophony rule and the
 * bridge's lighting regime -- are re-pushed by the setter below rather than
 * left to drift from the table they came from.
 */

enum
{
    /** Eye heights. The band this tree ships is 240..2160 around 600; the
     *  bounds here are wider than any profile states because a profile is a
     *  statement about a revision and this is a person moving a camera. */
    APP_PLUGIN_FEATURE_ZOOM_MIN = 64,
    APP_PLUGIN_FEATURE_ZOOM_MAX = 16384,
};

/** How a flag reaches its home. */
enum AppPluginFeatureSlot
{
    /** int field of struct ToriRS_FeatureTable, by byte offset. */
    APP_PLUGIN_FEATURE_SLOT_TABLE = 0,
    /** int field of struct RevConfigCameraItem, by byte offset. */
    APP_PLUGIN_FEATURE_SLOT_CAMERA,
    /** One bit of RevConfigCameraItem::controls; `offset` is the mask. */
    APP_PLUGIN_FEATURE_SLOT_CAMERA_BIT
};

struct AppPluginFeatureDesc
{
    char const* key;
    /** Short: it shares a 320-pixel row with a dropdown. */
    char const* label;
    /** Heading, or "" to continue the one before. */
    char const* section;
    enum AppPluginFeatureSlot slot;
    /** Byte offset into the owning struct, or the bit mask for a CAMERA_BIT. */
    size_t offset;
    /** enum ToriRS_PluginFeatureKind. */
    int kind;
    /** FEATURE_INT: the real range. A value outside `values` is still legal;
     *  a settings file may carry one and the panel shows it. */
    int min;
    int max;
    /**
     * The values worth NAMING, and their names.
     *
     * Named rather than left to a number field because these are the numbers
     * that mean something -- Client-TS's 600 eye height, the deob's 70-tile
     * pick ceiling, xrsps's 128 chathead ambient, the official 25..90 draw
     * band. A list of those is a settings row somebody can use; a box wanting
     * an integer in 64..16384 is a quiz.
     */
    char const* choices;
    int values[TORIRS_PLUGIN_FEATURE_VALUES_MAX];
    int value_count;
};

#define APP_PLUGIN_FEATURE_TABLE_OFF(field) offsetof(struct ToriRS_FeatureTable, field)
#define APP_PLUGIN_FEATURE_CAMERA_OFF(field) offsetof(struct RevConfigCameraItem, field)

static struct AppPluginFeatureDesc const APP_PLUGIN_FEATURES[] = {
    /* ---- camera: what the revision lets the player do with the view ------ */
    {
        "camera_zoom",
        "Zoom",
        "Camera",
        APP_PLUGIN_FEATURE_SLOT_CAMERA,
        APP_PLUGIN_FEATURE_CAMERA_OFF(zoom_mode),
        TORIRS_PLUGIN_FEATURE_ENUM,
        0,
        0,
        "Mouse wheel|Fixed",
        { REVCONFIG_CAMERA_ZOOM_CLAMPED, REVCONFIG_CAMERA_ZOOM_FIXED },
        2,
    },
    {
        "camera_zoom_min",
        "Closest",
        "",
        APP_PLUGIN_FEATURE_SLOT_CAMERA,
        APP_PLUGIN_FEATURE_CAMERA_OFF(zoom_min),
        TORIRS_PLUGIN_FEATURE_INT,
        APP_PLUGIN_FEATURE_ZOOM_MIN,
        APP_PLUGIN_FEATURE_ZOOM_MAX,
        "120|240|360|480|600",
        { 120, 240, 360, 480, 600 },
        5,
    },
    {
        "camera_zoom_max",
        "Furthest",
        "",
        APP_PLUGIN_FEATURE_SLOT_CAMERA,
        APP_PLUGIN_FEATURE_CAMERA_OFF(zoom_max),
        TORIRS_PLUGIN_FEATURE_INT,
        APP_PLUGIN_FEATURE_ZOOM_MIN,
        APP_PLUGIN_FEATURE_ZOOM_MAX,
        "900|1200|1600|2160|3200|4800",
        { 900, 1200, 1600, 2160, 3200, 4800 },
        6,
    },
    {
        "camera_zoom_height",
        "Fixed height",
        "",
        APP_PLUGIN_FEATURE_SLOT_CAMERA,
        APP_PLUGIN_FEATURE_CAMERA_OFF(zoom_height),
        TORIRS_PLUGIN_FEATURE_INT,
        APP_PLUGIN_FEATURE_ZOOM_MIN,
        APP_PLUGIN_FEATURE_ZOOM_MAX,
        "400|600 (2004)|900|1200",
        { 400, 600, 900, 1200 },
        4,
    },
    {
        "camera_wheel_step",
        "Wheel step",
        "",
        APP_PLUGIN_FEATURE_SLOT_CAMERA,
        APP_PLUGIN_FEATURE_CAMERA_OFF(wheel_step),
        TORIRS_PLUGIN_FEATURE_INT,
        1,
        1024,
        "Fine (20)|Small (40)|Normal (60)|Large (120)|Fastest (240)",
        { 20, 40, 60, 120, 240 },
        5,
    },
    {
        "camera_arrow_keys",
        "Arrow keys orbit",
        "",
        APP_PLUGIN_FEATURE_SLOT_CAMERA_BIT,
        REVCONFIG_CAMERA_CONTROL_ARROW_KEYS,
        TORIRS_PLUGIN_FEATURE_ENUM,
        0,
        0,
        "Off|On",
        { 0, 1 },
        2,
    },
    {
        "camera_mmb",
        "Middle-button drag",
        "",
        APP_PLUGIN_FEATURE_SLOT_CAMERA_BIT,
        REVCONFIG_CAMERA_CONTROL_MMB,
        TORIRS_PLUGIN_FEATURE_ENUM,
        0,
        0,
        "Off|On",
        { 0, 1 },
        2,
    },

    /* ---- clicking ------------------------------------------------------- */
    {
        "ground_click_offmap",
        "Click off the map",
        "Clicking",
        APP_PLUGIN_FEATURE_SLOT_TABLE,
        APP_PLUGIN_FEATURE_TABLE_OFF(ground_click_offmap_nearest),
        TORIRS_PLUGIN_FEATURE_ENUM,
        0,
        0,
        "Does nothing|Nearest tile",
        { 0, 1 },
        2,
    },
    {
        "ground_click_clamp",
        "Click reach",
        "",
        APP_PLUGIN_FEATURE_SLOT_TABLE,
        APP_PLUGIN_FEATURE_TABLE_OFF(ground_click_clamp_tiles),
        TORIRS_PLUGIN_FEATURE_INT,
        0,
        104,
        "No limit|25 tiles|50 tiles|70 tiles (deob)|104 tiles",
        { 0, 25, 50, 70, 104 },
        5,
    },
    {
        "attack_options",
        "Attack options",
        "",
        APP_PLUGIN_FEATURE_SLOT_TABLE,
        APP_PLUGIN_FEATURE_TABLE_OFF(attack_option_model),
        TORIRS_PLUGIN_FEATURE_ENUM,
        0,
        0,
        "Level bump (2004)|Dropdowns (OSRS)",
        { TORIRS_ATTACK_OPTION_MODEL_CLASSIC, TORIRS_ATTACK_OPTION_MODEL_SETTINGS },
        2,
    },
    {
        "target_mask_held",
        "Held-item spell bit",
        "",
        APP_PLUGIN_FEATURE_SLOT_TABLE,
        APP_PLUGIN_FEATURE_TABLE_OFF(target_mask_held),
        TORIRS_PLUGIN_FEATURE_ENUM,
        0,
        0,
        "0x10 (2004)|0x20 (OSRS)",
        { 0x10, 0x20 },
        2,
    },

    /* ---- what the scene looks like -------------------------------------- */
    {
        "mover",
        "Movement",
        "Scene",
        APP_PLUGIN_FEATURE_SLOT_TABLE,
        APP_PLUGIN_FEATURE_TABLE_OFF(mover_model),
        TORIRS_PLUGIN_FEATURE_ENUM,
        0,
        0,
        "Per cycle (2004)|Per frame (OSRS)",
        { TORIRS_MOVER_CYCLE_INTEGER, TORIRS_MOVER_FRAME_DELTA },
        2,
    },
    {
        "draw_distance",
        "Draw distance",
        "",
        APP_PLUGIN_FEATURE_SLOT_TABLE,
        APP_PLUGIN_FEATURE_TABLE_OFF(painter_draw_distance),
        TORIRS_PLUGIN_FEATURE_INT,
        /* The official band, not the field's own range. 0 is a real value of
         * the field -- it is how an era says "Client-TS's fixed 25" -- but it
         * is not a distance anyone means to pick, and 1..24 is nothing at all.
         * Revision default is how you get back to the 0. */
        TORIRS_PAINTER_DRAW_DISTANCE_MIN,
        TORIRS_PAINTER_DRAW_DISTANCE_MAX,
        "25 tiles|32 tiles|40 tiles|50 tiles|60 tiles|70 tiles|80 tiles|90 tiles",
        { 25, 32, 40, 50, 60, 70, 80, 90 },
        8,
    },
    {
        "npc_light_type",
        "NPC type lighting",
        "",
        APP_PLUGIN_FEATURE_SLOT_TABLE,
        APP_PLUGIN_FEATURE_TABLE_OFF(npc_light_uses_type_ambient_contrast),
        TORIRS_PLUGIN_FEATURE_ENUM,
        0,
        0,
        "Ignored (2004)|Applied (xrsps)",
        { 0, 1 },
        2,
    },
    {
        "player_head_ambient",
        "Chathead light",
        "",
        APP_PLUGIN_FEATURE_SLOT_TABLE,
        APP_PLUGIN_FEATURE_TABLE_OFF(player_head_light_ambient),
        TORIRS_PLUGIN_FEATURE_INT,
        0,
        255,
        "Scene regime|96|128 (xrsps)|160|192",
        { 0, 96, 128, 160, 192 },
        5,
    },

    /* ---- sound ----------------------------------------------------------- */
    {
        "effects_monophonic",
        "Sound effects",
        "Sound",
        APP_PLUGIN_FEATURE_SLOT_TABLE,
        APP_PLUGIN_FEATURE_TABLE_OFF(effects_monophonic),
        TORIRS_PLUGIN_FEATURE_ENUM,
        0,
        0,
        "Mix freely|One at a time (2004)",
        { 0, 1 },
        2,
    },
};

#define APP_PLUGIN_FEATURE_COUNT \
    ((int)(sizeof(APP_PLUGIN_FEATURES) / sizeof(APP_PLUGIN_FEATURES[0])))

/**
 * This boot's own values, captured before any plugin touched one.
 *
 * Lazily, because the plugin host is built in App_Init well BEFORE the era is
 * resolved -- and it has to be, so a plugin's config is readable the moment
 * anything asks. Nothing can reach these functions before PluginHost_Start,
 * which the plugin-prefs IO task runs long after App_Init has finished, so the
 * first call here always sees the resolved table.
 */
static void
app_plugin_feature_capture(struct App* app)
{
    assert(app);
    assert(app->features);

    if( app->plugin_feature_boot_valid )
        return;
    app->plugin_feature_boot = *app->features;
    app->plugin_feature_boot_camera = app->revconfig_profile.camera;
    app->plugin_feature_boot_valid = 1;
}

static struct AppPluginFeatureDesc const*
app_plugin_feature_desc(char const* key)
{
    assert(key);

    for( int i = 0; i < APP_PLUGIN_FEATURE_COUNT; i++ )
    {
        if( strcmp(APP_PLUGIN_FEATURES[i].key, key) == 0 )
            return &APP_PLUGIN_FEATURES[i];
    }
    return NULL;
}

/** The live int a flag names. `boot` selects this boot's snapshot instead. */
static int*
app_plugin_feature_cell(
    struct App* app, struct AppPluginFeatureDesc const* desc, int boot)
{
    assert(app);
    assert(desc);

    switch( desc->slot )
    {
    case APP_PLUGIN_FEATURE_SLOT_TABLE:
    {
        struct ToriRS_FeatureTable* table =
            boot ? &app->plugin_feature_boot : &app->features_storage;
        return (int*)((char*)table + desc->offset);
    }
    case APP_PLUGIN_FEATURE_SLOT_CAMERA:
    case APP_PLUGIN_FEATURE_SLOT_CAMERA_BIT:
    {
        struct RevConfigCameraItem* camera =
            boot ? &app->plugin_feature_boot_camera : &app->revconfig_profile.camera;
        /* A BIT names the mask in `offset`, so its cell is always `controls`;
         * the caller does the masking. */
        if( desc->slot == APP_PLUGIN_FEATURE_SLOT_CAMERA_BIT )
            return &camera->controls;
        return (int*)((char*)camera + desc->offset);
    }
    }
    assert(0 && "unhandled feature slot");
    return NULL;
}

static int
app_plugin_feature_read(
    struct App* app, struct AppPluginFeatureDesc const* desc, int boot)
{
    assert(app);
    assert(desc);

    int const cell = *app_plugin_feature_cell(app, desc, boot);
    if( desc->slot == APP_PLUGIN_FEATURE_SLOT_CAMERA_BIT )
        return (cell & (int)desc->offset) != 0;
    return cell;
}

/*
 * Re-push the two flags something else keeps a copy of.
 *
 * Both copies are made once at boot and would otherwise outlive the table they
 * were made from: RS_Audio holds `effects_monophonic` as its own bool, and the
 * scene bridge holds the lighting regime as two ints app.c merged `[render:light]`
 * into. Everything else in the list is read through `app->features` or the
 * camera item at the moment it is needed and needs nothing here.
 */
static void
app_plugin_feature_repush(struct App* app)
{
    assert(app);

    RS_Audio_SetFeatures(&app->audio, app->features);
    app->npc_light_uses_type_ambient_contrast =
        app->features->npc_light_uses_type_ambient_contrast;
    app->player_head_light_ambient = app->features->player_head_light_ambient;
    app->bridge.npc_light_uses_type_ambient_contrast =
        app->npc_light_uses_type_ambient_contrast;
    app->bridge.player_head_light_ambient = app->player_head_light_ambient;
    app->need_redraw = 1;
}

static int
app_plugin_feature_next(void* user, int iter, struct ToriRS_PluginFeature* out)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(out);

    app_plugin_feature_capture(app);

    int const at = iter < 0 ? 0 : iter + 1;
    if( at >= APP_PLUGIN_FEATURE_COUNT )
        return -1;

    struct AppPluginFeatureDesc const* desc = &APP_PLUGIN_FEATURES[at];
    memset(out, 0, sizeof(*out));
    snprintf(out->key, sizeof(out->key), "%s", desc->key);
    snprintf(out->label, sizeof(out->label), "%s", desc->label);
    snprintf(out->section, sizeof(out->section), "%s", desc->section);
    out->kind = desc->kind;
    out->min = desc->min;
    out->max = desc->max;
    if( desc->choices )
        snprintf(out->choices, sizeof(out->choices), "%s", desc->choices);
    out->value_count = desc->value_count;
    for( int i = 0; i < desc->value_count; i++ )
        out->values[i] = desc->values[i];
    out->value = app_plugin_feature_read(app, desc, 0);
    out->is_default = out->value == app_plugin_feature_read(app, desc, 1);
    return at;
}

static int
app_plugin_feature_get(void* user, char const* key)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(key);

    app_plugin_feature_capture(app);

    struct AppPluginFeatureDesc const* desc = app_plugin_feature_desc(key);
    if( !desc )
        return TORIRS_PLUGIN_FEATURE_UNSET;
    return app_plugin_feature_read(app, desc, 0);
}

static int
app_plugin_feature_set(void* user, char const* key, int value)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(key);

    app_plugin_feature_capture(app);

    struct AppPluginFeatureDesc const* desc = app_plugin_feature_desc(key);
    if( !desc )
        return 0;

    /* The sentinel is a RESTORE, not a value: whatever this boot resolved from
     * the era table, the manifest and the revconfig, which nothing outside
     * app.c can reconstruct. */
    if( value == TORIRS_PLUGIN_FEATURE_UNSET )
        value = app_plugin_feature_read(app, desc, 1);
    else if( desc->kind == TORIRS_PLUGIN_FEATURE_ENUM )
    {
        /* An enum is its list and nothing else. An INT is its RANGE -- the
         * named values are the ones worth offering, not the only ones legal,
         * so a settings file carrying an unnamed number keeps it. */
        int legal = 0;
        for( int i = 0; i < desc->value_count; i++ )
            legal |= desc->values[i] == value;
        if( !legal )
            return 0;
    }
    else if( value < desc->min || value > desc->max )
        return 0;

    int* cell = app_plugin_feature_cell(app, desc, 0);
    if( desc->slot == APP_PLUGIN_FEATURE_SLOT_CAMERA_BIT )
    {
        if( value )
            *cell |= (int)desc->offset;
        else
            *cell &= ~(int)desc->offset;
    }
    else
        *cell = value;

    /*
     * A zoom band is two numbers and one of them arrives first, so a band
     * typed in either order passes through a moment where min > max. Ordering
     * them here rather than refusing the first edit: app_world_camera_zooms
     * reads `min < max` as "this revision zooms at all", and an inverted band
     * would silently take the wheel away.
     */
    if( desc->slot == APP_PLUGIN_FEATURE_SLOT_CAMERA )
    {
        if( app->revconfig_profile.camera.zoom_min >
            app->revconfig_profile.camera.zoom_max )
        {
            int const swap = app->revconfig_profile.camera.zoom_min;
            app->revconfig_profile.camera.zoom_min =
                app->revconfig_profile.camera.zoom_max;
            app->revconfig_profile.camera.zoom_max = swap;
        }
        /* The live eye height is a position inside the band, so a band that
         * moved under it has to pull it back in or the next wheel notch
         * starts from outside it. */
        app->world_cam_height = RevConfigProfile_CameraClampHeight(
            &app->revconfig_profile, app->world_cam_height);
    }

    app_plugin_feature_repush(app);
    return 1;
}

/*
 * The client's own display preferences, as a plugin page may edit them.
 *
 * The SAME store the cache's All Settings panel writes -- device options 27
 * and 15 -- and deliberately: a lane with that panel would otherwise show one
 * value in two places and disagree with itself, and the whole reason these
 * verbs exist is the lane WITHOUT it. A 2004 dat1 cache has no All Settings
 * interface at all, so before this there was no way to reach interface
 * scaling on one -- the frame sat at 1:1 device pixels on a high-density
 * display and nothing in the client could say otherwise.
 *
 * Persistence needs nothing here: RS_Prefs_CaptureFromHost polls the option
 * store every tick and writes preferences.ini when it moves.
 */
static int
app_plugin_display_setting(
    void* user,
    int setting,
    int* out_value,
    int* out_min,
    int* out_max)
{
    struct App* app = (struct App*)user;
    int value;
    int min;
    int max;

    assert(app);
    switch( setting )
    {
    case TORIRS_PLUGIN_DISPLAY_UI_SCALE:
        value = RS_CS2Host_UiScalePercent(&app->host);
        min = RS_CS2_UI_SCALE_MIN;
        max = RS_CS2_UI_SCALE_MAX;
        break;
    case TORIRS_PLUGIN_DISPLAY_UI_SCALE_FILTER:
        value = RS_CS2Host_UiScaleMode(&app->host);
        min = RS_CS2_UI_SCALE_MODE_NEAREST;
        max = RS_CS2_UI_SCALE_MODE_BICUBIC;
        break;
    default:
        return 0;
    }

    if( out_value )
        *out_value = value;
    if( out_min )
        *out_min = min;
    if( out_max )
        *out_max = max;
    return 1;
}

static int
app_plugin_display_setting_set(void* user, int setting, int value)
{
    struct App* app = (struct App*)user;
    int option;

    assert(app);
    switch( setting )
    {
    case TORIRS_PLUGIN_DISPLAY_UI_SCALE:
        option = RS_CS2_DEVICEOPTION_UI_SCALE;
        break;
    case TORIRS_PLUGIN_DISPLAY_UI_SCALE_FILTER:
        option = RS_CS2_DEVICEOPTION_UI_SCALE_MODE;
        break;
    default:
        return 0;
    }
    /* Clamped by the store rather than refused here, which is what every other
     * writer of these two gets -- the cache's own settings scripts included.
     * @see RS_CS2Host_SetOption. */
    RS_CS2Host_SetOption(&app->host, RS_CS2_OPTION_DEVICE, option, value);
    return 1;
}

static int
app_plugin_cache_id(void* user, char const* kind, char const* name)
{
    struct App* app = (struct App*)user;
    assert(app);
    assert(kind);
    assert(name);
    return RevConfigRefs_Get(&app->revconfig_refs, kind, name);
}

/*
 * What `[cache:boot]` stated, translated out of rscache's enums into the
 * plugin header's own.
 *
 * Translated and not cast, even though the two sets agree value for value
 * today. The plugin header names no engine type -- that is what makes the
 * layer language-agnostic -- so its enum is free to be renumbered, and a cast
 * would turn that into a plugin quietly reading RS2 for OldSchool rather than
 * a compile error.
 *
 * The PROVIDER's copy rather than the AppConfig it came from, because the
 * provider's is the one the decoders read: a plugin that disagreed with them
 * about which client this is would be answering a question nothing else in the
 * process answers the same way.
 */
static int
app_plugin_lane(void* user, struct ToriRS_PluginLane* out)
{
    struct App* app = (struct App*)user;
    struct RSCache const* profile;

    assert(app);
    assert(out);

    memset(out, 0, sizeof(*out));
    /* Not yet identified is a real state and not a fault: the host is built in
     * App_Init before the cache profile is set, so anything that asks in
     * between gets UNKNOWN and is documented to wait rather than decide. */
    if( !app->provider || !RSCache_ProfileIsIdentified(&app->provider->profile) )
        return 0;

    profile = CacheProvider_Profile(app->provider);
    switch( profile->game )
    {
    case RSCACHE_GAME_OLDSCHOOL:
        out->game = TORIRS_PLUGIN_GAME_OLDSCHOOL;
        break;
    case RSCACHE_GAME_RS2:
        out->game = TORIRS_PLUGIN_GAME_RS2;
        break;
    default:
        out->game = TORIRS_PLUGIN_GAME_UNKNOWN;
        break;
    }
    switch( profile->epoch )
    {
    case RSCACHE_EPOCH_DAT1:
        out->epoch = TORIRS_PLUGIN_EPOCH_DAT1;
        break;
    case RSCACHE_EPOCH_DAT2:
        out->epoch = TORIRS_PLUGIN_EPOCH_DAT2;
        break;
    default:
        out->epoch = TORIRS_PLUGIN_EPOCH_UNKNOWN;
        break;
    }
    out->revision = profile->revision;
    return 1;
}

/*
 * One objtype, as the cache states it.
 *
 * A HIT and never a load, for the reason app_plugin_fill_obj gives: this runs
 * inside a frame -- a hover, a draw -- and a snapshot verb that started IO
 * would stall the one thing it is supposed to be cheap enough for.
 *
 * The twelve bonuses come out of the record's own param table. Ids 0..11 are
 * the equipment bonuses and 14 is the attack rate in ticks -- an OldSchool
 * convention (OpenRune's ParamMapper documents it, and the server reads the
 * same table through read_combat_params) -- so a client running an OldSchool
 * cache answers a weapon's stats with no hand-written bonus table anywhere.
 * A string param's value is a `char*`, so reading one as an int would be a
 * wild dereference and not a wrong number: those are skipped by kind.
 */
static int
app_plugin_obj_info(void* user, int obj_id, struct ToriRS_PluginObjInfo* out)
{
    struct App* app = (struct App*)user;
    struct ToriRS_Objtype* type;

    assert(app);
    assert(out);

    if( obj_id < 0 || !app->provider )
        return 0;
    type = CacheProvider_ObjtypeGet(app->provider, obj_id);
    if( !type )
        return 0;

    memset(out, 0, sizeof(*out));
    out->obj_id = obj_id;
    snprintf(out->name, sizeof(out->name), "%s", type->name);
    out->cost = type->cost;
    out->stackable = type->stackable ? 1 : 0;
    out->cert_link = type->cert_template >= 0 ? type->cert_link : -1;
    out->wearpos = type->wearpos;
    out->wearpos2 = type->wearpos2;
    out->wearpos3 = type->wearpos3;
    out->attack_rate = -1;

    for( int i = 0; i < type->param_count; i++ )
    {
        struct ToriRS_Param const* param = &type->params[i];
        if( param->string_value )
            continue;
        if( param->key >= 0 && param->key < TORIRS_PLUGIN_BONUS_COUNT )
        {
            out->bonus[param->key] = param->int_value;
            out->has_bonuses = 1;
        }
        else if( param->key == 14 )
        {
            out->attack_rate = param->int_value;
            out->has_bonuses = 1;
        }
        /* Ranged strength, from whichever of the two ids this record uses. */
        else if( param->key == 12 || param->key == 189 )
        {
            out->ranged_strength += param->int_value;
            out->has_bonuses = 1;
        }
    }
    return 1;
}

/* The client's container ids, which is where they already live: a plugin
 * naming one by number would be carrying a copy of a constant it cannot
 * check. -1 for an `inv` outside the enum, which the callers below turn into
 * "no such container" rather than reading slot 0 of the backpack. */
static int
app_plugin_inv_container(int inv)
{
    switch( inv )
    {
    case TORIRS_PLUGIN_INV_BACKPACK:
        return INV_MANAGER_CONTAINER_BACKPACK;
    case TORIRS_PLUGIN_INV_WORN:
        return INV_MANAGER_CONTAINER_WORN;
    case TORIRS_PLUGIN_INV_BANK:
        return INV_MANAGER_CONTAINER_BANK;
    default:
        return INV_MANAGER_CONTAINER_NONE;
    }
}

/*
 * One container slot.
 *
 * An empty slot answers 1 with an obj id of -1, and that is the distinction
 * the return value exists to make: "the shield slot is empty" and "this client
 * has never been told about a worn container" are different facts, and a
 * plugin diffing against worn equipment does different things with them.
 */
static int
app_plugin_inv_slot(void* user, int inv, int slot, int* out_obj_id, int* out_count)
{
    struct App* app = (struct App*)user;
    int container = app_plugin_inv_container(inv);
    struct InvContainer const* held;

    assert(app);

    if( container == INV_MANAGER_CONTAINER_NONE || slot < 0 )
        return 0;
    held = InvManager_FindContainer(&app->invs, container);
    if( !held || slot >= InvManager_Size(&app->invs, container) )
        return 0;

    if( out_obj_id )
        *out_obj_id = InvManager_GetObj(&app->invs, container, slot);
    if( out_count )
        *out_count = InvManager_GetNum(&app->invs, container, slot);
    return 1;
}

static int
app_plugin_inv_size(void* user, int inv)
{
    struct App* app = (struct App*)user;
    int container = app_plugin_inv_container(inv);

    assert(app);

    if( container == INV_MANAGER_CONTAINER_NONE )
        return 0;
    return InvManager_Size(&app->invs, container);
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
    uint32_t fill_rgb,
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
    int const before = app ? app_overlay_count(app) : 0;

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
    /* The wash is the caller's fill colour, which is not always the outline's
     * -- see draw_tile in torirs_plugin.h. */
    if( fill_alpha > 0 )
        app_overlay_push_polygon_filled(
            app,
            hull_x,
            hull_y,
            hull_size,
            app_plugin_overlay_argb(fill_rgb),
            255 - (fill_alpha > 255 ? 255 : fill_alpha));
    app_overlay_push_polygon(app, hull_x, hull_y, hull_size, app_plugin_overlay_argb(rgb));
    return app_overlay_count(app) - before;
}

static int
app_plugin_draw_hull(void* user, int element_id, uint32_t rgb, int fill_alpha, int shape)
{
    struct App* app = (struct App*)user;
    int before;

    assert(app);
    assert(shape == TORIRS_PLUGIN_HULL_BOUNDS || shape == TORIRS_PLUGIN_HULL_MESH);
    before = app_overlay_count(app);
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
    return app_overlay_count(app) - before;
}

static int
app_plugin_draw_line(void* user, int x0, int y0, int x1, int y1, uint32_t rgb)
{
    struct App* app = (struct App*)user;
    int before;

    assert(app);
    before = app_overlay_count(app);
    app_overlay_push_segment(app, x0, y0, x1, y1, app_plugin_overlay_argb(rgb));
    return app_overlay_count(app) - before;
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

    before = app_overlay_count(app);
    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_TEXT;
    item.x = x;
    item.y = y;
    item.color = rgb;
    item.font_id = app_hitsplat_font_scene_id(app);
    snprintf(item.text, sizeof(item.text), "%s", text);
    app_overlay_push(app, &item);
    return app_overlay_count(app) - before;
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
    before = app_overlay_count(app);

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
    return app_overlay_count(app) - before;
}

/* ------------------------------------------------------------------ images */

/*
 * A plugin image, decoded once and published as a scene sprite.
 *
 * The decode lives here rather than in the host for the same reason the draw
 * verbs do: it needs the ToriDraw scene the sprite has to be registered in,
 * and the scene is the App's. What crosses the seam is a slot number and a
 * geometry, which is all the host needs to answer image_size with.
 */
static int
app_plugin_image_publish(
    void* user,
    int slot,
    void const* data,
    int size,
    int* out_w,
    int* out_h)
{
    struct App* app = (struct App*)user;
    uint32_t* pixels = NULL;
    int w = 0;
    int h = 0;
    int scene_id;

    assert(app);
    assert(data);
    assert(out_w);
    assert(out_h);

    /* ARGB, not RGB: interface art is a cut-out, and an orb whose alpha was
     * dropped arrives as a black square with a disc drawn on it. */
    if( !PngDecode_Argb(data, size, &w, &h, &pixels) )
        return 0;

    scene_id = UITreeSceneBridge_PublishPluginImage(&app->bridge, slot, w, h, pixels);
    free(pixels);
    if( scene_id < 0 )
        return 0;

    *out_w = w;
    *out_h = h;
    return 1;
}

/*
 * Pixels the plugin composed. The publish above with the decode taken out.
 *
 * Refusing an absurd geometry rather than asserting it, for the reason the
 * decode path refuses one: the numbers come from a plugin, and the honest
 * answer to "that is not a picture" is that the image did not become one.
 */
static int
app_plugin_image_publish_argb(void* user, int slot, int w, int h, uint32_t const* argb)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(argb);

    if( w <= 0 || h <= 0 )
        return 0;
    return UITreeSceneBridge_PublishPluginImage(&app->bridge, slot, w, h, argb) >= 0;
}

static int
app_plugin_image_read(void* user, int slot, uint32_t* out, int max)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(out);
    return UITreeSceneBridge_ReadPluginImage(&app->bridge, slot, out, max);
}

static void
app_plugin_image_release(void* user, int slot)
{
    struct App* app = (struct App*)user;
    assert(app);
    UITreeSceneBridge_ReleasePluginImage(&app->bridge, slot);
}

static int
app_plugin_draw_image(
    void* user,
    int slot,
    int x,
    int y,
    int w,
    int h,
    int clip_x,
    int clip_y,
    int clip_w,
    int clip_h,
    int trans)
{
    struct App* app = (struct App*)user;
    struct UITreeEntityOverlay item;
    int before;

    assert(app);
    before = app_overlay_count(app);

    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_SPRITE;
    item.scene_id = UITREE_SCENE_PLUGIN_IMAGE_BASE + slot;
    item.atlas_index = 0;
    item.x = x;
    item.y = y;
    item.w = w;
    item.h = h;
    item.trans = trans < 0 ? 0 : (trans > 255 ? 255 : trans);
    /* A zero w or h in the item means "no extra clip", which is exactly what
     * the contract says a zero clip_w/clip_h means, so it travels unchanged. */
    item.clip_x = clip_x;
    item.clip_y = clip_y;
    item.clip_w = clip_w;
    item.clip_h = clip_h;
    app_overlay_push(app, &item);
    return app_overlay_count(app) - before;
}

/* ------------------------------------------------------- canvas hit regions */

/** The topmost live region covering a canvas point, or -1. */
static int
app_plugin_role_region_live(
    struct App const* app,
    struct AppPluginRegion const* region)
{
    struct UITreeComponent const* target;

    assert(app);
    assert(region);
    if( !region->role_anchored )
        return 1;
    if( !app->tree || region->role_node < 0 ||
        (uint32_t)region->role_node >= app->tree->component_count )
        return 0;
    target = &app->tree->components[region->role_node];
    if( target->freed || target->incarnation != region->role_incarnation )
        return 0;
    if( region->role_clip_w <= 0 || region->role_clip_h <= 0 )
        return 0;
    if( !region->role_replace )
        return !UITree_NodeOrAncestorDisplayHidden(app->tree, region->role_node);
    if( !target->replacement_hidden )
        return 0;
    return !UITree_NodeOrAncestorDisplayHiddenExceptReplacement(
        app->tree, region->role_node);
}

static int
app_plugin_role_region_occluded(
    struct App const* app,
    struct AppPluginRegion const* region,
    int x,
    int y)
{
    assert(app);
    assert(region);
    if( !region->role_anchored || !app->tree )
        return 0;
    return UITree_PointInputCoverPaintsAfterRoleBoundary(
        app->tree,
        &app->ui_host,
        x,
        y,
        region->role_node,
        region->role_incarnation,
        region->role_replace != 0);
}

static int
app_plugin_region_at(struct App const* app, int x, int y)
{
    int frame_native_cover = -1;

    assert(app);

    /* Global Canvas paints over every interface. Anchored Canvas declarations
     * were made in the same callback pass but were extracted into local tree
     * boundaries, so global Canvas outranks them regardless of declaration
     * order. FRAME is the lower chrome surface. */
    for( int z_group = 0; z_group < 3; z_group++ )
    {
        int best = -1;
        for( int i = 0; i < app->plugin_region_count; i++ )
        {
            struct AppPluginRegion const* region = &app->plugin_regions[i];
            int const region_group =
                region->surface == APP_PLUGIN_SURFACE_CANVAS
                    ? (region->role_anchored ? 1 : 0)
                    : (region->surface == APP_PLUGIN_SURFACE_FRAME ? 2 : -1);
            if( region_group != z_group )
                continue;
            if( !app_plugin_role_region_live(app, region) )
                continue;
            if( x < region->x || x >= region->x + region->w )
                continue;
            if( y < region->y || y >= region->y + region->h )
                continue;
            if( region->role_anchored &&
                (x < region->role_clip_x ||
                 x >= region->role_clip_x + region->role_clip_w ||
                 y < region->role_clip_y ||
                 y >= region->role_clip_y + region->role_clip_h) )
                continue;
            if( app_plugin_role_region_occluded(app, region, x, y) )
                continue;

            /* FRAME chrome is emitted below native interfaces. Its regions
             * must live at that same depth: an interactive widget, a blank
             * noClickThrough layer or a modal mount above this point owns the
             * pointer even when it contributes no ordinary hit node. */
            if( region->surface == APP_PLUGIN_SURFACE_FRAME )
            {
                if( frame_native_cover < 0 )
                    frame_native_cover = app->tree
                                             ? UITree_PointHasNativeInputCover(
                                                   app->tree,
                                                   &app->ui_host,
                                                   x,
                                                   y)
                                             : 0;
                if( frame_native_cover )
                    continue;
            }

            if( best < 0 )
            {
                best = i;
                continue;
            }
            if( region->role_anchored )
            {
                struct AppPluginRegion const* prior = &app->plugin_regions[best];
                /* Canvas callbacks declare in subscriber order, then semantic
                 * anchors relocate those declarations into unrelated tree
                 * boundaries. The emit-published boundary order is therefore
                 * the z key; declaration order breaks a tie at one boundary. */
                if( region->role_paint_order > prior->role_paint_order ||
                    (region->role_paint_order == prior->role_paint_order && i > best) )
                    best = i;
            }
            else if( i > best )
            {
                /* Global CANVAS and FRAME retain declaration order within
                 * their own global surface. */
                best = i;
            }
        }
        if( best >= 0 )
            return best;
    }
    return -1;
}

static int
app_plugin_hit_region(
    void* user,
    int plugin,
    int x,
    int y,
    int w,
    int h,
    char const* const* ops,
    int op_count,
    uint32_t tag)
{
    struct App* app = (struct App*)user;
    int const cap = (int)(sizeof(app->plugin_regions) / sizeof(app->plugin_regions[0]));
    struct AppPluginRegion* region;

    assert(app);

    if( app->plugin_role_anchor_active && !app->plugin_role_anchor_valid )
        return 0;
    if( app->plugin_region_count >= cap )
        return 0;

    region = &app->plugin_regions[app->plugin_region_count++];
    memset(region, 0, sizeof(*region));
    region->plugin = plugin;
    region->x = x;
    region->y = y;
    region->w = w;
    region->h = h;
    region->tag = tag;
    region->surface = (uint8_t)app->plugin_draw_canvas;
    if( app->plugin_role_anchor_active )
    {
        region->role_anchored = 1;
        region->role_replace = app->plugin_role_anchor_replace;
        region->role_node = app->plugin_role_anchor_node;
        region->role_incarnation = app->plugin_role_anchor_incarnation;
        /* Zero until emit reaches this exact subtree and publishes the same
         * parent clip as the role-local paint descriptor. */
        region->role_clip_w = 0;
        region->role_clip_h = 0;
    }
    /* Empty entries are dropped rather than kept as blank rows, so a caller
     * with a fixed-size table can hand the whole thing over. The INDEX a click
     * reports is into what was kept, which is what the plugin then switches
     * on -- see ToriRS_PluginEvCanvasClick::op. */
    for( int i = 0; i < op_count; i++ )
    {
        if( !ops[i] || !ops[i][0] )
            continue;
        snprintf(
            region->ops[region->op_count], sizeof(region->ops[0]), "%s", ops[i]);
        region->op_count++;
    }
    return 1;
}

/*
 * Press an interface button, by running the row a real click would have built.
 *
 * Not a packet send. Everything a click on a button does -- the local
 * client_code, the varp a toggle owns, the plain IF_BUTTON, the numbered
 * IF_BUTTON<op> and its events-mask gate -- lives in app_minimenu_run_option,
 * and a second path that only sent the packet would be a click that works on
 * some buttons and half-works on the rest. So the option is fabricated and the
 * client's own dispatcher runs it, which is the literal meaning of "press
 * that button".
 *
 * `action_index` is the op slot the dispatcher reads (`op_num = index + 1`);
 * op 0, the classic unnumbered button, is slot 0, which is exactly what a
 * cache-authored IF1 button's own row carries.
 */
/*
 * The node-index half of the press.
 *
 * Separate from the uid lookup because not every pressable node HAS a uid: a
 * revconfig-authored control only gets a synthetic id when it carries a menu
 * row, and a role names a node directly. So the pick carries whatever
 * `component_id` the node holds -- -1 included, which the dispatcher reads as
 * the same "no id" every builtin already has.
 */
static int
app_plugin_click_node(struct App* app, int32_t node, int op)
{
    struct UIMinimenu scratch;
    struct UIMinimenu saved;
    struct UIMinimenuPick pick;
    int action;
    int action_index;
    int button_type;

    assert(app);
    assert(app->tree);

    if( node < 0 || (uint32_t)node >= app->tree->component_count )
        return 0;
    if( app->tree->components[node].freed )
        return 0;

    memset(&pick, 0, sizeof(pick));
    pick.kind = UI_MINIMENU_PICK_UI;
    pick.id = app->tree->components[node].component_id;
    pick.has_node_identity = 1;
    pick.node_index = node;
    pick.node_incarnation = app->tree->components[node].incarnation;
    /* A semantic replacement is still allowed to delegate its native action.
     * Ignore only this node's own display:none tombstone; hidden ancestors and
     * any rebuild during menu interception remain hard lifetime fences. */
    pick.allow_own_replacement_hidden = 1;

    /*
     * The action a real click on THIS button would carry, derived from its own
     * `buttonType` -- the same derivation add_component_rows makes when it
     * builds the row for it.
     *
     * Hardcoding IF_BUTTON here worked for a plain button and silently did the
     * wrong thing for every other kind. A LostCity run toggle is
     * `buttontype=select`: the local half of pressing it is writing the varp
     * the button selects, which only IF_BUTTON_SELECT does, so an IF_BUTTON
     * sent the packet and left the client showing the old state until the
     * server's varp came back -- or, offline, for ever.
     *
     * A NUMBERED op keeps IF_BUTTON: op 1..10 is the IF3 form, where the
     * number is the whole of what the component offers and buttonType is not
     * part of the vocabulary.
     */
    button_type = app->tree->components[node].behavior.button_type;
    action = op >= 1 ? REVCONFIG_MINIMENU_IF_BUTTON
                     : RS_Minimenu_IfButtonActionForType(button_type);

    /*
     * `action_index` is the op SLOT, and the dispatcher reads it as `op_num =
     * index + 1`. So an index of 0 does not mean "no numbered op", it means
     * "op 1" -- and a classic IF1 button pressed with index 0 was being routed
     * into the IF3 numbered-op path: the client sent IF_BUTTON<1>, which a
     * 2004 protocol has no answer for, and then returned before
     * RS_IF1_ApplyButtonClick could apply the button's own semantics. The
     * press did nothing, on exactly the revisions whose buttons are classic.
     *
     * -1 is the "unnumbered" index the rest of this dispatcher already uses.
     * Which of the two a component wants is not a guess: an IF1 button says so
     * by carrying a `buttonType`, and an IF3 one says so by carrying none.
     */
    if( op >= 1 )
        action_index = op - 1;
    else if( button_type != 0 )
        action_index = -1;
    else
        action_index = 0;

    UIMinimenu_Reset(&scratch);
    scratch.font_id = app->interact.minimenu.font_id;
    if( !UIMinimenu_AddOption(&scratch, "", action, action_index, pick) )
        return 0;

    /* 0,0 as the click point, and it is never read: the cross is painted per
     * ACTION (rs_minimenu_cross.h), and a UI button's is UI_CROSS_OFF -- the
     * reference's doAction does not touch crossMode for one either. A
     * synthesised press has no click point to give, and this is the reason it
     * does not need one. */
    saved = app->interact.minimenu;
    app->interact.minimenu = scratch;
    app_minimenu_run_option(app, 0, 0, 0);
    app->interact.minimenu = saved;
    return 1;
}

static int
app_plugin_if_click(void* user, int component_id, int op)
{
    struct App* app = (struct App*)user;
    int32_t node;

    assert(app);

    if( !app->tree )
        return 0;
    /* A component id this tree does not hold is bad input -- a config key
     * naming a button that is not on this cache -- not a broken contract. */
    node = UITree_FindByComponentId(app->tree, component_id);
    if( node < 0 )
        return 0;
    return app_plugin_click_node(app, node, op);
}

/* Which overlay list the draw verbs above append to. @see app_overlay_push.
 * The value is enum PluginDrawSurface and is carried verbatim, because the
 * lists are indexed by it on the other side. */
static void
app_plugin_draw_select_canvas(void* user, int canvas)
{
    struct App* app = (struct App*)user;
    assert(app);
    app->plugin_draw_canvas = canvas;
}

/* ------------------------------------------------------- chrome + the player */

static int
app_plugin_mouse_pos(void* user, int* out_x, int* out_y)
{
    struct App* app = (struct App*)user;

    assert(app);
    /* The same point every click path reads, latched once per frame from the
     * input drain (App_RunOnce). Before the first frame it is 0,0, which is a
     * legal position -- so the answer is the position, not a validity flag. */
    if( out_x )
        *out_x = app->world_mouse_x;
    if( out_y )
        *out_y = app->world_mouse_y;
    return 1;
}

static int
app_plugin_minimap_rect(void* user, int* out_x, int* out_y, int* out_w, int* out_h)
{
    struct App* app = (struct App*)user;

    assert(app);
    /* The same test the click path makes before doing tile math on this box:
     * the desc is last frame's layout, and before the first one it holds
     * nothing. A gameframe with no minimap component never sets it at all. */
    if( !app->minimap_view_valid )
        return 0;
    if( app->minimap_emit_desc.w <= 0 || app->minimap_emit_desc.h <= 0 )
        return 0;

    if( out_x )
        *out_x = app->minimap_emit_desc.x;
    if( out_y )
        *out_y = app->minimap_emit_desc.y;
    if( out_w )
        *out_w = app->minimap_emit_desc.w;
    if( out_h )
        *out_h = app->minimap_emit_desc.h;
    return 1;
}

/** A laid-out node's box, or 0 for one that has no size. */
static int
app_plugin_node_rect(
    struct App* app, int32_t node, int* out_x, int* out_y, int* out_w, int* out_h)
{
    struct UITreeComponent const* c;

    assert(app);
    if( !app->tree || node < 0 || (uint32_t)node >= app->tree->component_count )
        return 0;
    c = &app->tree->components[node];
    /* Unresolved or zero-sized is not a rectangle. A modal region that has
     * never been laid out answers nothing rather than answering (0,0,0,0),
     * which a caller would centre things on. */
    if( c->freed || !c->position.layout_resolved )
        return 0;
    if( c->position.abs_w <= 0 || c->position.abs_h <= 0 )
        return 0;
    return UITree_NodeDrawnBounds(
        app->tree, node, out_x, out_y, out_w, out_h);
}

/*
 * A region's box, by role. @see ToriRS_PluginApi::slot_rect.
 *
 * Resolved through UITree_FrameSlotNode, which is the same role->node lookup
 * the layout WRITE path uses -- so a role a layout plugin can place is exactly
 * a role a readout can read, on every lane, with no second table to keep in
 * step. That is the whole point of collapsing the old anchor enum into this
 * one: before it, "the viewport" was two different lookups that happened to
 * agree.
 *
 * The lookup is a linear walk of the tree and its own header says "once per
 * declaration, never per frame". This IS per frame, so the answer is cached
 * against the tree revision below.
 */
/**
 * The node carrying `slot`'s role, cached for the life of a tree generation.
 *
 * UITree_FrameSlotNode is a linear walk of every component and its own header
 * says so: "affordable because of WHEN it is called -- once per declaration,
 * never per frame". Reading a region IS per frame, and on several regions at
 * once, so the walk is done once per generation and the answer kept.
 *
 * Keyed on `tree->generation`, which is bumped by any topology change, so a
 * rebuild, a mount or a reclaim all invalidate this without anything having to
 * remember to.
 */
static int32_t
app_plugin_slot_node_cached(struct App* app, int slot)
{
    assert(app);
    assert(app->tree);
    assert(slot >= 0 && slot < TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT);

    if( app->plugin_slot_node_gen != app->tree->generation )
    {
        for( int i = 0; i < TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT; i++ )
            app->plugin_slot_node[i] = UITree_FrameSlotNode(app->tree, i);
        app->plugin_slot_node_gen = app->tree->generation;
    }
    return app->plugin_slot_node[slot];
}

static int
app_plugin_slot_node_rect(
    struct App* app, int slot, int* out_x, int* out_y, int* out_w, int* out_h)
{
    int32_t node;

    assert(app);
    if( !app->tree )
        return 0;
    node = app_plugin_slot_node_cached(app, slot);
    if( node < 0 )
        return 0;
    return app_plugin_node_rect(app, node, out_x, out_y, out_w, out_h);
}

static int
app_plugin_slot_rect(
    void* user, int slot, int* out_x, int* out_y, int* out_w, int* out_h)
{
    struct App* app = (struct App*)user;

    assert(app);

    /* CANVAS is not a node and never can be: it is the surface every node is
     * laid out against. */
    if( slot == TORIRS_PLUGIN_SLOT_CANVAS )
    {
        if( out_x )
            *out_x = 0;
        if( out_y )
            *out_y = 0;
        if( out_w )
            *out_w = UITREE_LAYOUT_ROOT_W;
        if( out_h )
            *out_h = UITREE_LAYOUT_ROOT_H;
        return 1;
    }
    /* SAFE is the host's: it needs the reservation table, which lives there. */
    if( slot < 0 || slot >= TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT )
        return 0;

    /*
     * The scene's box comes from the render pass rather than from the node.
     *
     * They are the same rectangle in principle and not always in practice: the
     * emit desc is the viewport the frame was actually DRAWN with this frame,
     * gate rect and all, which is what a plugin drawing over the scene has to
     * agree with. The node is what the layout says it should be.
     */
    if( slot == TORIRS_PLUGIN_SLOT_VIEWPORT && app->world_view_valid &&
        app->world_emit_desc.w > 0 && app->world_emit_desc.h > 0 )
    {
        if( out_x )
            *out_x = app->world_emit_desc.x;
        if( out_y )
            *out_y = app->world_emit_desc.y;
        if( out_w )
            *out_w = app->world_emit_desc.w;
        if( out_h )
            *out_h = app->world_emit_desc.h;
        return 1;
    }
    if( slot == TORIRS_PLUGIN_SLOT_MINIMAP )
        return app_plugin_minimap_rect(user, out_x, out_y, out_w, out_h);

    if( app_plugin_slot_node_rect(app, slot, out_x, out_y, out_w, out_h) )
        return 1;

    /*
     * MAIN_MODAL has a second source, and it is the one that answers on a dat2
     * frame: those declare no modal region at all -- the server names the host
     * component in IF_OPENSUB, and it is a different one in the fixed frame
     * than in the resizable one -- so the only thing that knows is the mount,
     * and App records it there.
     */
    if( slot == TORIRS_PLUGIN_SLOT_MAIN_MODAL && app->modal_host_uid >= 0 &&
        app->tree )
        return app_plugin_node_rect(
            app,
            UITree_FindByComponentId(app->tree, app->modal_host_uid),
            out_x,
            out_y,
            out_w,
            out_h);
    return 0;
}

/*
 * One member of a region. @see ToriRS_PluginApi::slot_member_rect.
 *
 * UITree_FrameSlotMemberNode rather than the cached per-role node above,
 * because that one holds the answer to "any member" and a caller asking for
 * the report button wants the fourth chat filter and not whichever chat button
 * the walk saw first. The walk is a linear one and this is a per-frame read,
 * so it is the one region the CALLER is expected to be sparing with -- a
 * plugin anchoring to a member reads it once per frame, not once per drawn
 * thing.
 */
/*
 * Where a component is. @see ToriRS_PluginApi::component_rect.
 *
 * The same node->rect the region readouts end in, reached by id rather than by
 * role, so a component and a region answer with one notion of "where".
 */
static int
app_plugin_component_rect(
    void* user, int component_id, int* out_x, int* out_y, int* out_w, int* out_h)
{
    struct App* app = (struct App*)user;

    assert(app);
    if( !app->tree )
        return 0;
    return app_plugin_node_rect(
        app,
        UITree_FindByComponentId(app->tree, component_id),
        out_x,
        out_y,
        out_w,
        out_h);
}

/* ----------------------------------------------------------- roles */

/*
 * The node a role names, or -1.
 *
 * The regions are answered by the SLOT vocabulary and not by the role table,
 * even though a profile may also bind them there. Two reasons, and the second
 * is the load-bearing one: every lane has a viewport whether or not its
 * profile thought to say so, and `safe` and `canvas` are DERIVED -- there is
 * no node for "the part of the canvas nothing is sitting on", so the only
 * honest answer for them comes from the rect path below.
 */
static int
app_plugin_role_slot(char const* role)
{
    assert(role);
    if( strcmp(role, "canvas") == 0 )
        return TORIRS_PLUGIN_SLOT_CANVAS;
    if( strcmp(role, "safe") == 0 )
        return TORIRS_PLUGIN_SLOT_SAFE;
    return UITree_RoleSlotFromName(role);
}

static int32_t
app_plugin_role_node(struct App* app, char const* role)
{
    int slot;

    assert(app);
    assert(role);
    if( !app->tree )
        return -1;

    slot = app_plugin_role_slot(role);
    /* SAFE and CANVAS are rectangles and not nodes, so they have no answer
     * here at all -- the rect verb handles them and the others do not. */
    if( slot >= 0 && slot < TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT )
        return app_plugin_slot_node_cached(app, slot);
    if( slot >= 0 )
        return -1;

    return UITree_RoleNodeByName(app->tree, &app->ui_roles, role);
}

static int
app_plugin_role_rect(
    void* user, char const* role, int* out_x, int* out_y, int* out_w, int* out_h)
{
    struct App* app = (struct App*)user;
    int slot;

    assert(app);
    assert(role);
    if( !app->tree )
        return 0;

    /*
     * A region role goes straight to the region reader, which knows the three
     * things the node's own box does not: that the scene's rectangle is the
     * one the frame was DRAWN with, that the minimap's comes from the emit
     * desc, and that a dat2 modal has no declared region at all.
     */
    slot = app_plugin_role_slot(role);
    if( slot >= 0 && slot != TORIRS_PLUGIN_SLOT_SAFE )
        return app_plugin_slot_rect(user, slot, out_x, out_y, out_w, out_h);
    if( slot == TORIRS_PLUGIN_SLOT_SAFE )
        /* Derived from the others plus the reservation table, both of which
         * live in the host; it reaches this verb only through slot_rect. */
        return 0;

    return app_plugin_node_rect(
        app, app_plugin_role_node(app, role), out_x, out_y, out_w, out_h);
}

static int
app_plugin_role_visible(void* user, char const* role)
{
    struct App* app = (struct App*)user;
    int32_t node;

    assert(app);
    assert(role);
    if( !app->tree )
        return 0;

    node = app_plugin_role_node(app, role);
    if( node < 0 )
        return 0;

    /* Pack ids can cross an InterfaceParent mount edge that is not represented
     * by Component::parent. Use the tree's virtual-ancestry visibility check
     * before the local walk below; revconfig-only builtins have no id and keep
     * using the physical walk. */
    if( app->tree->components[node].component_id >= 0 &&
        UITree_ComponentOrAncestorDisplayHidden(
            app->tree, app->tree->components[node].component_id) )
        return 0;

    /*
     * Up the ancestry, because a visible child of a hidden parent is not on
     * screen. Three tests, because "hidden" is spelled three ways by three
     * owners and means one thing to the player:
     *
     *   `hide` is the cache's and the scripts'. Read directly rather than
     *   through UITree_ComponentVisibleById, whose rule that a hidden node
     *   with no id counts as visible is right for the hover-reveal it was
     *   written for and wrong here -- every revconfig builtin has no id, and
     *   this verb would call all of them visible always.
     *
     *   `frame_hidden` is a gameframe layout's suppression.
     *
     *   And the HOST's, for the builtins whose visibility is not a flag at
     *   all: a sidebar mount is on screen only while its tab is the selected
     *   one, and that lives in the client rather than on the node. Without it
     *   "is the logout screen up" would answer yes from the moment the frame
     *   was built, on every tab -- which is the whole question.
     */
    while( node >= 0 && (uint32_t)node < app->tree->component_count )
    {
        struct UITreeComponent const* c = &app->tree->components[node];
        if( c->freed || c->frame_hidden || c->replacement_hidden ||
            c->behavior.hide )
            return 0;
        if( c->type == UIELEM_BUILTIN_SIDEBAR || c->type == UIELEM_BUILTIN_REDSTONE_TAB ||
            c->type == UIELEM_BUILTIN_CROSS || c->type == UIELEM_BUILTIN_MINIMENU )
        {
            struct UITreeHoverIds hover = { 0 };
            if( !UITree_ComponentVisibleHost(c, &hover, &app->ui_host) )
                return 0;
        }
        node = c->parent;
    }
    return 1;
}

static int
app_plugin_role_click(void* user, char const* role, int op)
{
    struct App* app = (struct App*)user;
    int32_t node;

    assert(app);
    assert(role);
    if( !app->tree )
        return 0;

    node = app_plugin_role_node(app, role);
    if( node < 0 )
        return 0;
    return app_plugin_click_node(app, node, op);
}

static int
app_plugin_role_id(void* user, char const* role)
{
    struct App* app = (struct App*)user;
    int32_t node;

    assert(app);
    assert(role);
    if( !app->tree )
        return -1;

    node = app_plugin_role_node(app, role);
    if( node < 0 )
        return -1;
    /* A node with no id of its own answers -1 too: an authored control that
     * never earned a synthetic id has nothing to hand back, and inventing one
     * would be handing out a number no other verb can use. */
    return app->tree->components[node].component_id;
}

static int
app_plugin_role_replacement_find(
    struct App const* app,
    int plugin,
    char const* role)
{
    assert(app);
    assert(role);
    for( int i = 0; i < (int)(sizeof(app->plugin_role_replacements) /
                              sizeof(app->plugin_role_replacements[0])); i++ )
    {
        struct AppPluginRoleReplacement const* row =
            &app->plugin_role_replacements[i];
        if( row->role[0] && row->plugin == plugin && strcmp(row->role, role) == 0 )
            return i;
    }
    return -1;
}

static int
app_plugin_role_replacement_free(struct App const* app)
{
    assert(app);
    for( int i = 0; i < (int)(sizeof(app->plugin_role_replacements) /
                              sizeof(app->plugin_role_replacements[0])); i++ )
        if( !app->plugin_role_replacements[i].role[0] )
            return i;
    return -1;
}

static int
app_plugin_role_replacement_node_claimed(
    struct App const* app,
    int except,
    int32_t node,
    uint32_t incarnation)
{
    assert(app);
    for( int i = 0; i < (int)(sizeof(app->plugin_role_replacements) /
                              sizeof(app->plugin_role_replacements[0])); i++ )
    {
        struct AppPluginRoleReplacement const* row =
            &app->plugin_role_replacements[i];
        if( i != except && row->role[0] && row->node_index == node &&
            row->node_incarnation == incarnation )
            return 1;
    }
    return 0;
}

/* Engine half of the standing claim. The host owns arbitration; the App owns
 * this exact-incarnation fence because only it can resolve a semantic role to
 * a tree node. Repeating enabled=1 is the per-frame reconciliation path. */
static int
app_plugin_role_replace(
    void* user,
    int plugin,
    char const* role,
    int enabled)
{
    struct App* app = (struct App*)user;
    struct AppPluginRoleReplacement* row;
    int at;
    int32_t old_node;
    uint32_t old_incarnation;
    int32_t next_node = -1;
    uint32_t next_incarnation = 0;

    assert(app);
    assert(role);
    at = app_plugin_role_replacement_find(app, plugin, role);
    if( !enabled )
    {
        if( at < 0 )
            return 1;
        row = &app->plugin_role_replacements[at];
        old_node = row->node_index;
        old_incarnation = row->node_incarnation;
        memset(row, 0, sizeof(*row));
        row->node_index = -1;
        if( app->tree &&
            !app_plugin_role_replacement_node_claimed(
                app, at, old_node, old_incarnation) )
            (void)UITree_SetReplacementHidden(
                app->tree, old_node, old_incarnation, 0);
        return 1;
    }

    if( at < 0 )
    {
        at = app_plugin_role_replacement_free(app);
        if( at < 0 )
            return 0;
        row = &app->plugin_role_replacements[at];
        memset(row, 0, sizeof(*row));
        row->plugin = plugin;
        row->node_index = -1;
        snprintf(row->role, sizeof(row->role), "%s", role);
    }
    row = &app->plugin_role_replacements[at];
    old_node = row->node_index;
    old_incarnation = row->node_incarnation;

    if( app->tree )
    {
        next_node = app_plugin_role_node(app, role);
        if( next_node >= 0 && (uint32_t)next_node < app->tree->component_count &&
            !app->tree->components[next_node].freed )
            next_incarnation = app->tree->components[next_node].incarnation;
        else
            next_node = -1;
    }

    if( old_node == next_node && old_incarnation == next_incarnation )
        return next_node >= 0;

    row->node_index = next_node;
    row->node_incarnation = next_incarnation;
    if( app->tree &&
        !app_plugin_role_replacement_node_claimed(
            app, at, old_node, old_incarnation) )
        (void)UITree_SetReplacementHidden(
            app->tree, old_node, old_incarnation, 0);
    if( app->tree && next_node >= 0 )
        (void)UITree_SetReplacementHidden(
            app->tree, next_node, next_incarnation, 1);
    return next_node >= 0;
}

static int
app_plugin_role_anchor(
    void* user,
    int plugin,
    char const* role,
    int replace)
{
    struct App* app = (struct App*)user;
    int32_t node;

    (void)plugin;
    assert(app);
    if( !role )
    {
        app->plugin_role_anchor_active = 0;
        app->plugin_role_anchor_valid = 0;
        app->plugin_role_anchor_node = -1;
        app->plugin_role_anchor_incarnation = 0;
        app->plugin_role_anchor_replace = 0;
        return 1;
    }

    /* Active and invalid is intentionally distinct from no anchor: every
     * subsequent draw is dropped until this subscriber returns. */
    app->plugin_role_anchor_seen = 1;
    app->plugin_role_anchor_active = 1;
    app->plugin_role_anchor_valid = 0;
    app->plugin_role_anchor_node = -1;
    app->plugin_role_anchor_incarnation = 0;
    app->plugin_role_anchor_replace = replace ? 1 : 0;
    if( !app->tree )
        return 0;
    node = app_plugin_role_node(app, role);
    if( node < 0 || (uint32_t)node >= app->tree->component_count ||
        app->tree->components[node].freed )
        return 0;
    if( replace && !app->tree->components[node].replacement_hidden )
        return 0;
    if( replace &&
        UITree_NodeOrAncestorDisplayHiddenExceptReplacement(app->tree, node) )
        return 0;
    if( !replace && UITree_NodeOrAncestorDisplayHidden(app->tree, node) )
        return 0;

    app->plugin_role_anchor_valid = 1;
    app->plugin_role_anchor_node = node;
    app->plugin_role_anchor_incarnation =
        app->tree->components[node].incarnation;
    app_role_overlay_group_seed(
        app,
        app->plugin_role_anchor_node,
        app->plugin_role_anchor_incarnation,
        app->plugin_role_anchor_replace);
    return 1;
}

static int
app_plugin_slot_member_rect(
    void* user, int slot, int member, int* out_x, int* out_y, int* out_w, int* out_h)
{
    struct App* app = (struct App*)user;

    assert(app);
    if( !app->tree )
        return 0;
    if( slot < 0 || slot >= TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT )
        return 0;
    return app_plugin_node_rect(
        app, UITree_FrameSlotMemberNode(app->tree, slot, member), out_x, out_y, out_w, out_h);
}

/*
 * The interface group the gameframe was rooted to, or -1 on a lane whose frame
 * is revconfig builtins.
 *
 * It is what tells a cache toplevel's OWN decoration apart from the interface
 * packs mounted inside it, and getting it wrong is not subtle in either
 * direction: -1 on a dat2 lane leaves the toplevel's stones drawn under the
 * plugin's, and a wrong group id hides an inventory.
 */
static int
app_plugin_layout_root_group(struct App const* app)
{
    assert(app);
    if( App_UiLogic((struct App*)app) == APP_UI_LOGIC_CS1 )
        return -1;
    return app->host.top_interface_id > 0 ? app->host.top_interface_id : -1;
}

static void
app_plugin_layout_set(void* user, int owned, int canvas, int fixed_w, int fixed_h)
{
    struct App* app = (struct App*)user;

    assert(app);
    app->plugin_layout_owned = owned ? 1 : 0;
    app->plugin_layout_canvas = canvas;
    app->plugin_layout_fixed_w = fixed_w;
    app->plugin_layout_fixed_h = fixed_h;
    /* Whatever the claim just became, the frame on screen is no longer the one
     * that was declared -- a release has to give the lane's chrome back and a
     * claim has to take it. */
    app->plugin_layout_dirty = 1;
    if( !app->plugin_layout_owned && app->tree )
        UITree_FrameRelease(app->tree);
    App_SyncPluginLayoutCanvas(app);
}

static void
app_plugin_layout_begin(void* user)
{
    struct App* app = (struct App*)user;

    assert(app);
    memset(app->plugin_layout_slots, 0, sizeof(app->plugin_layout_slots));
    /* EV_LAYOUT is a whole declaration. A skin omitted by this declaration is
     * native again; it must not inherit six scene ids from the prior owner or
     * prior layout variant. */
    memset(app->plugin_layout_scrollbar, 0, sizeof(app->plugin_layout_scrollbar));
}

static int
app_plugin_layout_slot(void* user, int slot, int member, int x, int y, int w, int h)
{
    struct App* app = (struct App*)user;
    struct UITreeFrameRect* out;

    assert(app);
    if( slot < 0 || slot >= TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT )
        return 0;
    assert(slot >= 0 && slot < TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT);

    /* A member number out of range is a plugin's arithmetic, not a broken
     * contract: it is refused and reported as "this frame has no such
     * member", which is the same answer it gets for a member the frame really
     * does not have. */
    if( member >= UITREE_FRAME_SLOT_NODES_MAX )
        return 0;
    out = member < 0 ? &app->plugin_layout_slots[slot].all
                     : &app->plugin_layout_slots[slot].at[member];
    out->placed = 1;
    out->x = x;
    out->y = y;
    out->w = w;
    out->h = h;
    /* The answer is about the FRAME and not about the recording: a plugin asks
     * "did that land on anything" so it knows whether to draw the housing for
     * it, and a frame with no compass should get no compass ring. */
    return app->tree && UITree_FrameSlotMemberNode(app->tree, slot, member) >= 0;
}

/*
 * A plugin image slot as the scene id the tree can draw from.
 *
 * The publish put the pixels at UITREE_SCENE_PLUGIN_IMAGE_BASE + slot and the
 * host has already refused a handle whose pixels have not landed, so this is
 * arithmetic rather than a lookup -- and it is the one place the two
 * numberings meet.
 */
static int
app_plugin_image_scene_id(int image)
{
    return image < 0 ? 0 : UITREE_SCENE_PLUGIN_IMAGE_BASE + image;
}

/*
 * The scrollbar skin the standing declaration asked for, as scene ids.
 *
 * `images` is the host's six pieces in UITREE_SCROLLBAR_SKIN_* order, already
 * checked resident; a NULL one is a layout asking for the client's own painted
 * bar back, which is the same thing an unfinished art load gets. Stored beside
 * the slots and cleared with them, because it is part of the same declaration.
 */
static int
app_plugin_layout_scrollbar(void* user, int const* images, int count)
{
    struct App* app = (struct App*)user;

    assert(app);

    if( !images || count < UITREE_SCROLLBAR_SKIN_COUNT )
    {
        memset(app->plugin_layout_scrollbar, 0, sizeof(app->plugin_layout_scrollbar));
        return 1;
    }
    for( int i = 0; i < UITREE_SCROLLBAR_SKIN_COUNT; i++ )
        app->plugin_layout_scrollbar[i] = app_plugin_image_scene_id(images[i]);
    return 1;
}

static int
app_plugin_layout_slot_skin(void* user, int slot, int art, int mask)
{
    struct App* app = (struct App*)user;
    struct UITreeFrameSkin* out;

    assert(app);
    if( slot < 0 || slot >= TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT )
        return 0;
    assert(slot >= 0 && slot < TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT);
    if( slot != TORIRS_PLUGIN_SLOT_MINIMAP && slot != TORIRS_PLUGIN_SLOT_COMPASS )
        return 0;
    if( slot == TORIRS_PLUGIN_SLOT_MINIMAP && art >= 0 )
        return 0;

    out = &app->plugin_layout_slots[slot].skin;
    out->placed = 1;
    out->art_scene_id = app_plugin_image_scene_id(art);
    out->mask_scene_id = app_plugin_image_scene_id(mask);
    /* The same "did that land on anything" answer layout_slot gives, and for
     * the same reason: a frame with no compass should get no compass ring. */
    return app->tree && UITree_FrameSlotNode(app->tree, slot) >= 0;
}

static int
app_plugin_layout_slot_overlay(
    void* user,
    int slot,
    int image,
    int x,
    int y,
    int trans)
{
    struct App* app = (struct App*)user;
    struct UITreeFrameOverlay* out;

    assert(app);
    if( slot < 0 || slot >= TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT )
        return 0;

    out = &app->plugin_layout_slots[slot].overlay;
    out->placed = 1;
    out->scene_id = app_plugin_image_scene_id(image);
    out->x = x;
    out->y = y;
    out->trans = trans;
    return app->tree && UITree_FrameSlotNode(app->tree, slot) >= 0;
}

static void
app_plugin_layout_end(void* user)
{
    struct App* app = (struct App*)user;

    assert(app);
    if( !app->tree )
        return;
    /* The owner may release from inside EV_LAYOUT. Its partial declaration is
     * then abandoned; applying it after layout_set released the frame would
     * suppress native chrome under an ownerless empty frame. */
    if( !app->plugin_layout_owned )
    {
        UITree_FrameRelease(app->tree);
        return;
    }

    UITree_FrameApply(
        app->tree, app->plugin_layout_slots, app_plugin_layout_root_group(app));
    app->plugin_layout_w = UITREE_LAYOUT_ROOT_W;
    app->plugin_layout_h = UITREE_LAYOUT_ROOT_H;
    app->plugin_layout_generation = app->tree->generation;
    app->plugin_layout_dirty = 0;
}

static int
app_plugin_tab_active(void* user)
{
    struct App* app = (struct App*)user;

    assert(app);
    return app->slots.side_tab;
}

static int
app_plugin_tab_select(void* user, int tabno)
{
    struct App* app = (struct App*)user;

    assert(app);
    /* The client's own tab flip, not a varp write: which interface is on a tab
     * is the SERVER's (IF_SETTAB), and selecting one is a client-side
     * selection over that table. Refusing a tab the frame has nothing on is
     * what keeps a stone drawn for a tab this cache does not fill from
     * blanking the sidebar when it is pressed. */
    if( tabno < 0 || tabno >= RS_UI_SLOTS_TAB_MAX )
        return 0;
    if( !RS_UISlots_TabEnabled(&app->slots, tabno) )
        return 0;
    RS_UISlots_SetSideTab(app, tabno);
    return 1;
}

static int
app_plugin_stat(void* user, int skill, int* out_current, int* out_base)
{
    struct App* app = (struct App*)user;

    assert(app);
    if( skill < 0 || skill >= RS_PLAYER_STATS_SKILL_COUNT )
        return 0;
    if( out_current )
        *out_current = app->stats.current_level[skill];
    if( out_base )
        *out_base = app->stats.base_level[skill];
    return 1;
}

static int
app_plugin_stat_xp(
    void* user,
    int skill,
    int* out_xp,
    int* out_level_xp,
    int* out_next_xp)
{
    struct App* app = (struct App*)user;
    int level;

    assert(app);
    if( skill < 0 || skill >= RS_PLAYER_STATS_SKILL_COUNT )
        return 0;
    if( out_xp )
        *out_xp = app->stats.xp[skill];

    /*
     * The thresholds either side of the EARNED level, out of the client's own
     * table. `level_xp[n]` is the xp that reaches level n + 2, so the xp that
     * reached the current level is the entry two below it and the next one is
     * the entry one below -- which is also why level 1 has no entry at all and
     * starts at zero.
     */
    level = app->stats.base_level[skill];
    if( level < 1 )
        level = 1;
    if( out_level_xp )
        *out_level_xp = level >= 2 ? app->stats.level_xp[level - 2] : 0;
    if( out_next_xp )
        *out_next_xp =
            level < RS_PLAYER_STATS_LEVEL_MAX ? app->stats.level_xp[level - 1] : 0;
    return 1;
}

static char const*
app_plugin_skill_name(void* user, int skill)
{
    assert(user);
    (void)user;
    return RS_GameEvent_SkillName(skill);
}

static int
app_plugin_run_energy(void* user)
{
    struct App* app = (struct App*)user;
    assert(app);
    return app->stats.run_energy;
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
app_plugin_menu_build(
    struct App* app, struct UIMinimenu* menu, int click_x, int click_y, int hover_pass)
{
    struct ToriRS_PluginEvMenuBuild ev;

    assert(app);
    assert(menu);

    if( !app->plugins )
        return;

    /*
     * A canvas region's own row, before the plugins are asked for theirs.
     *
     * The region list is the app's, not the host's, because the app is what
     * hit-tests it -- and putting the row here rather than in a path of its
     * own is what makes ONE thing true of it: the mouseover line, the
     * right-click menu and the left-click default are all this same build, so
     * an orb that says "Toggle Run" on hover is an orb that toggles run when
     * clicked, with nothing to keep in step.
     *
     * Topmost first: regions are declared in draw order, so the last one
     * covering the point is the one on top, and it is the one whose row is
     * added.
     */
    {
        int const i = app_plugin_region_at(app, click_x, click_y);
        if( i >= 0 )
        {
            struct AppPluginRegion const* region = &app->plugin_regions[i];
            struct UIMinimenuPick pick;

            /* No verbs does not mean click-through: it is the plugin form of
             * an opaque panel. Retain the standard escape row, but discard
             * native/world actions which were built underneath its pixels. */
            if( region->op_count <= 0 )
            {
                int write = 0;
                for( int row = 0; row < menu->option_count; row++ )
                    if( menu->options[row].action == REVCONFIG_MINIMENU_CANCEL )
                    {
                        if( write != row )
                            menu->options[write] = menu->options[row];
                        write++;
                    }
                menu->option_count = write;
            }
            else
            {
                memset(&pick, 0, sizeof(pick));
                pick.kind = UI_MINIMENU_PICK_NONE;
                /* A popup survives into later frames while this rebuilt list does not.
                 * Stamp the region's logical owner and anchored node incarnation so a
                 * recycled list index can never invoke a different plugin region. */
                pick.id = region->plugin;
                pick.secondary_id = (int)region->tag;
                pick.tertiary_id = region->role_anchored ? region->role_node : -1;
                pick.quaternary_id =
                    region->role_anchored ? (int)region->role_incarnation : 0;
                /* Last op first: rows draw bottom-to-top, so adding in reverse puts
                 * op 0 on top -- the same order add_menu_ops_rows walks a component's
                 * own verbs in, and the reason op 1 is the one beside Cancel. */
                for( int op = region->op_count - 1; op >= 0; op-- )
                    UIMinimenu_AddOption(
                        menu,
                        region->ops[op],
                        RS_MINIMENU_ACTION_PLUGIN_REGION,
                        /* The region and the op, in the one field a row carries. */
                        i * TORIRS_PLUGIN_REGION_OPS_MAX + op,
                        pick);
            }
        }
    }
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
        row->component_id = -1;
        row->slot = -1;

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
        else if( opt->pick.kind == UI_MINIMENU_PICK_UI )
        {
            row->component_id = opt->pick.id;
        }
        /*
         * An inventory cell names three things and the row carries all three:
         * the grid it is in, the slot inside that grid, and the ITEM sitting
         * there. The item is the target -- a row about a cell is a row about
         * what is in it -- and the component is how a reader tells the
         * backpack from the worn tab from a bank, which no other field says.
         */
        else if( opt->pick.kind == UI_MINIMENU_PICK_INV_SLOT )
        {
            row->component_id = opt->pick.id;
            row->slot = opt->pick.secondary_id;
            row->target_id = opt->pick.tertiary_id;
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
    engine.loc_next = app_plugin_loc_next;
    engine.highlight_next = app_plugin_highlight_next;
    engine.notify = app_plugin_notify;
    engine.key_held = app_plugin_key_held;
    engine.hover_tile = app_plugin_hover_tile;
    engine.hover_entity = app_plugin_hover_entity;
    engine.element_height = app_plugin_element_height;
    engine.feature_next = app_plugin_feature_next;
    engine.feature_get = app_plugin_feature_get;
    engine.feature_set = app_plugin_feature_set;
    engine.display_setting = app_plugin_display_setting;
    engine.display_setting_set = app_plugin_display_setting_set;
    engine.varbit = app_plugin_varbit;
    engine.varp = app_plugin_varp;
    engine.cache_id = app_plugin_cache_id;
    engine.lane = app_plugin_lane;
    engine.obj_info = app_plugin_obj_info;
    engine.inv_slot = app_plugin_inv_slot;
    engine.inv_size = app_plugin_inv_size;
    engine.project = app_plugin_project;
    engine.draw_tile = app_plugin_draw_tile;
    engine.draw_hull = app_plugin_draw_hull;
    engine.draw_line = app_plugin_draw_line;
    engine.draw_text = app_plugin_draw_text;
    engine.draw_rect = app_plugin_draw_rect;
    engine.draw_select_canvas = app_plugin_draw_select_canvas;
    engine.image_publish = app_plugin_image_publish;
    engine.image_publish_argb = app_plugin_image_publish_argb;
    engine.image_read = app_plugin_image_read;
    engine.image_release = app_plugin_image_release;
    engine.draw_image = app_plugin_draw_image;
    engine.hit_region = app_plugin_hit_region;
    engine.if_click = app_plugin_if_click;
    engine.mouse_pos = app_plugin_mouse_pos;
    engine.minimap_rect = app_plugin_minimap_rect;
    engine.slot_rect = app_plugin_slot_rect;
    engine.slot_member_rect = app_plugin_slot_member_rect;
    engine.component_rect = app_plugin_component_rect;
    engine.role_rect = app_plugin_role_rect;
    engine.role_visible = app_plugin_role_visible;
    engine.role_click = app_plugin_role_click;
    engine.role_id = app_plugin_role_id;
    engine.role_replace = app_plugin_role_replace;
    engine.role_anchor = app_plugin_role_anchor;
    engine.layout_set = app_plugin_layout_set;
    engine.layout_begin = app_plugin_layout_begin;
    engine.layout_end = app_plugin_layout_end;
    engine.layout_slot = app_plugin_layout_slot;
    engine.layout_slot_skin = app_plugin_layout_slot_skin;
    engine.layout_slot_overlay = app_plugin_layout_slot_overlay;
    engine.layout_scrollbar = app_plugin_layout_scrollbar;
    engine.tab_active = app_plugin_tab_active;
    engine.tab_select = app_plugin_tab_select;
    engine.stat = app_plugin_stat;
    engine.stat_xp = app_plugin_stat_xp;
    engine.skill_name = app_plugin_skill_name;
    engine.run_energy = app_plugin_run_energy;
    engine.menu_add = app_plugin_menu_add;
    engine.asset_read = app_plugin_asset_read;
    engine.asset_write = app_plugin_asset_write;
    engine.screenshot = app_plugin_screenshot;
    engine.model_publish = app_plugin_model_publish;
    engine.model_release = app_plugin_model_release;
    engine.mesh_create = app_plugin_mesh_create;
    engine.mesh_destroy = app_plugin_mesh_destroy;
    engine.mesh_clear = app_plugin_mesh_clear;
    engine.mesh_vertex = app_plugin_mesh_vertex;
    engine.mesh_face = app_plugin_mesh_face;
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
