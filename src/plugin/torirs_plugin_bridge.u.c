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

static size_t
app_plugin_memory_bytes(void* user)
{
    uint64_t bytes = 0;
    (void)user;
    if( !PlatformMemory_FootprintBytes(&bytes) )
        return 0;
    return bytes > SIZE_MAX ? SIZE_MAX : (size_t)bytes;
}

/* Reference the local player's route[0], not the draw position: the draw
 * position is interpolated between tiles every frame, and the server thinks in
 * whole tiles. route[0] is the authoritative one (entity_facets.h). */
static void
app_plugin_fill_player(
    struct App* app,
    struct WorldEntity_Player const* player,
    struct ToriRS_PlayerSnapshot* out)
{
    int base_x = app->world->_base_tile_x;
    int base_z = app->world->_base_tile_z;

    assert(player);
    assert(out);

    memset(out, 0, sizeof(*out));

    /* A wire-homed rider's route/grid coordinates are DECK-LOCAL; their
     * absolute address is the view's STAGING rectangle, not the root scene —
     * root-base + deck-local names an unrelated corner of the map. draw_tile
     * round-trips staging addresses through the boat's transform, so the
     * true-tile marker lands on the planking. */
    if( player->view_placement.home_view != 0 &&
        WorldviewRegistry_IsLive(&app->worldviews, player->view_placement.home_view) )
    {
        struct Worldview const* view =
            WorldviewRegistry_Get(&app->worldviews, player->view_placement.home_view);

        base_x = view->base_x;
        base_z = view->base_z;
    }

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
app_plugin_fill_npc_for_world(
    struct App* app,
    struct World const* world,
    struct WorldEntity_NPC const* npc,
    struct ToriRS_NpcSnapshot* out)
{
    int const base_x = world->_base_tile_x;
    int const base_z = world->_base_tile_z;

    assert(app);
    assert(world);
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

    /*
     * The health bar, if the server has ever sent one.
     *
     * `healthbar_end_fill` and not `start_fill`: the pair describe a fill
     * ANIMATING from one value to another, and the end is where the server
     * said it is going -- which is the value the reference's getHealthRatio
     * reports and the one that reads 0 on the tick something dies. Reporting
     * the start would say "still half full" for the whole of the bar's travel
     * down to empty.
     *
     * The scale is the TYPE's width, which is a denominator and not a pixel
     * count (see src/game/rs_healthbar.h -- conflating the two is what made
     * boss bars screen-wide). TypeFor never returns NULL, so a cache with no
     * healthbar group still answers with the reference's own defaults.
     */
    if( npc->combat.healthbar_type >= 0 )
    {
        struct RS_HealthbarType const* type =
            RS_Healthbars_TypeFor(&app->healthbars, npc->combat.healthbar_type);
        out->health_ratio = npc->combat.healthbar_end_fill;
        out->health_scale = type->width > 0 ? type->width : RS_HEALTHBAR_DEFAULT_WIDTH;
    }
    else
    {
        out->health_ratio = -1;
        out->health_scale = -1;
    }

    /* The name was copied onto the entity at spawn/retype; there is no cache
     * fetch here, the same way the minimenu builder does not do one. */
    snprintf(out->name, sizeof(out->name), "%s", npc->name);
}

static void
app_plugin_fill_npc(
    struct App* app,
    struct WorldEntity_NPC const* npc,
    struct ToriRS_NpcSnapshot* out)
{
    assert(app);
    assert(app->world);
    app_plugin_fill_npc_for_world(app, app->world, npc, out);
}

static void
app_plugin_fill_obj(
    struct App* app,
    struct WorldEntity_ObjStack const* stack,
    struct ToriRS_GroundItemSnapshot* out)
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
app_plugin_obj_next(void* user, int iter, struct ToriRS_GroundItemSnapshot* out)
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
    struct ToriRS_ScenerySnapshot* out)
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
app_plugin_loc_next(void* user, int iter, struct ToriRS_ScenerySnapshot* out)
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
    struct ToriRS_HighlightItem* proto)
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

static struct ToriRS_HighlightItem*
app_plugin_highlight_push(struct App* app, struct ToriRS_HighlightItem const* proto)
{
    struct ToriRS_HighlightItem* item;

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
app_plugin_highlight_loc_cache_needs_full(struct App const* app)
{
    assert(app);
    assert(app->world);

    return !app->plugin_highlight_loc_valid ||
        app->plugin_highlight_loc_revision != app->host.highlight.revision ||
        app->world->scenery_changed_overflow;
}

/* Push one resolved loc into the cache. Bounded by the same cap as the live
 * list, so a cache that fills cannot outrun what the list could hold. */
static struct ToriRS_HighlightItem*
app_plugin_highlight_loc_cache_push(
    struct App* app,
    struct ToriRS_HighlightItem const* proto)
{
    struct ToriRS_HighlightItem* item;

    if( app->plugin_highlight_loc_count >= APP_PLUGIN_HIGHLIGHTS_MAX )
        return NULL;
    item = &app->plugin_highlight_loc[app->plugin_highlight_loc_count++];
    *item = *proto;
    return item;
}

/*
 * Does this one loc match anything highlighted? If so, cache it.
 *
 * The single definition of the test, shared by the full walk and the
 * incremental update, so the two can never drift into disagreeing about
 * what counts as a match.
 */
static void
app_plugin_highlight_loc_resolve_one(
    struct App* app,
    struct WorldEntity_Scenery* loc)
{
    struct RS_HighlightState const* hl = &app->host.highlight;
    struct ToriRS_HighlightItem proto;
    /* Asked once per entity here rather than once per pool walk, because an
     * incremental update has no pool walk to hoist it out of. It is a scan of
     * a list that is empty in every ordinary session. */
    bool const has_opgroup = app_plugin_highlight_named_any(hl, RS_HIGHLIGHT_OPGROUP);

    assert(app);
    assert(app->world);
    assert(loc);

    int tile_x;
    int tile_z;
    int coord;

    if( !loc || loc->element_id < 0 )
        return;
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
            /* CONTINUE, not return: these are per-member tests inside a walk
             * of every member. Returning here ended the whole resolve at the
             * first member that named some other loc type -- so once any
             * loctype group held members (the Blast Furnace group does at
             * every login), no loc of any other group ever resolved, and the
             * mouse-over loc highlight recorded its subject but drew nothing.
             * Found by the nxt-highlight native capture. */
            if( m->key != loc->loc_id )
                continue;
            /* The placed form pins a coord as well as a type; the type
             * form marks every instance. */
            if( k == RS_HIGHLIGHT_LOC && m->coord != coord )
                continue;
            if( !app_plugin_highlight_begin(app, k, m->group, &proto) )
                continue;
            proto.kind = TORIRS_HIGHLIGHT_LOC;
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
            proto.kind = TORIRS_HIGHLIGHT_LOC;
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

/* Drop every cached entry for one element. A re-registered loc is removed
 * and then re-tested, so this runs for changes as well as removals. */
static void
app_plugin_highlight_loc_cache_forget(
    struct App* app,
    int element_id)
{
    int out = 0;
    int i;

    for( i = 0; i < app->plugin_highlight_loc_count; i++ )
    {
        if( app->plugin_highlight_loc[i].element_id == element_id )
            continue;
        if( out != i )
            app->plugin_highlight_loc[out] = app->plugin_highlight_loc[i];
        out++;
    }
    app->plugin_highlight_loc_count = out;
}

/*
 * Bring the cache up to date from the world's list of changed scenery.
 *
 * This is the point of the cache: the resolved list is MAINTAINED as locs
 * come and go, not recomputed by walking every entity in the pool. The cost
 * is proportional to what changed -- usually nothing, and a handful of
 * entities while moving -- instead of to the size of the scene. Without it a
 * cache keyed on "did the scenery change" would thrash: one door opening
 * would buy back the whole 23k walk, and walking around opens a lot of doors.
 *
 * Each changed element is forgotten and then re-tested, which covers all
 * three cases in one shape: an element that appeared is absent from the cache
 * and gets added; one that vanished is dropped and fails the active test; one
 * that morphed is dropped and re-tested against its new loc_id.
 */
static void
app_plugin_highlight_loc_cache_apply(struct App* app)
{
    struct World_EntityPool* pool;
    int n;
    int i;

    assert(app);
    assert(app->world);

    n = app->world->scenery_changed_count;
    if( n <= 0 )
        return;
    pool = &app->world->entities.scenery;

    for( i = 0; i < n; i++ )
    {
        int const element_id = app->world->scenery_changed[i];
        int const idx = ToriDraw_ElementIndexOfRaw(element_id);
        struct WorldEntity_Scenery* loc;

        app_plugin_highlight_loc_cache_forget(app, element_id);

        if( idx < 0 || !World_EntityPoolIsActive(pool, idx) )
            continue;
        loc = World_EntityPoolGet(pool, idx);
        if( !loc || loc->element_id != element_id )
            continue;
        app_plugin_highlight_loc_resolve_one(app, loc);
    }
}

/* Both defined below; the audit sits next to what it audits rather than
 * next to its dependencies. */
static bool
app_plugin_highlight_debug(void);
static void
app_plugin_highlight_loc_cache_build(struct App* app);

/*
 * Under TORIRS_HIGHLIGHT_DEBUG, check the maintained list against a full
 * rebuild and say so when they disagree.
 *
 * An incrementally maintained cache fails silently. It does not crash; it
 * just quietly stops drawing a highlight, or keeps drawing one that should
 * be gone, and from outside that is indistinguishable from the plugin never
 * asking for it. The only honest check is to do the work the long way and
 * compare, which is what the gate check in app_plugin_highlights_rebuild
 * already does for its own question.
 *
 * The REBUILT list is what is left in place: if the two ever disagree the
 * drawn frame stays correct and only the log says anything.
 */
static void
app_plugin_highlight_loc_cache_audit(struct App* app)
{
    int incremental_count;
    int incremental_sum = 0;
    int rebuilt_sum = 0;
    int i;

    if( !app_plugin_highlight_debug() )
        return;

    incremental_count = app->plugin_highlight_loc_count;
    for( i = 0; i < incremental_count; i++ )
        incremental_sum += app->plugin_highlight_loc[i].element_id;

    app_plugin_highlight_loc_cache_build(app);
    for( i = 0; i < app->plugin_highlight_loc_count; i++ )
        rebuilt_sum += app->plugin_highlight_loc[i].element_id;

    if( incremental_count != app->plugin_highlight_loc_count ||
        incremental_sum != rebuilt_sum )
        fprintf(
            stderr,
            "highlight-loc-cache: MISMATCH incremental=%d (sum %d) rebuilt=%d (sum %d)\n",
            incremental_count,
            incremental_sum,
            app->plugin_highlight_loc_count,
            rebuilt_sum);
}

/*
 * Resolve every LOC highlight in the scene, from scratch.
 *
 * The whole scenery pool -- ~23k entities in an ordinary map square -- tested
 * against the loc and loctype member lists. Runs only when the QUESTION
 * changes (a highlight turned on, off or restyled), or when the world says it
 * stopped tracking which locs changed. Ordinary scene churn is handled
 * incrementally by app_plugin_highlight_loc_cache_apply.
 */
static void
app_plugin_highlight_loc_cache_build(struct App* app)
{
    struct World_EntityPool* pool;

    assert(app);
    assert(app->world);

    app->plugin_highlight_loc_count = 0;

    pool = &app->world->entities.scenery;
    for( int at = World_EntityPoolHead(pool); at != WORLD_ENTITY_NIL;
         at = World_EntityPoolNext(pool, at) )
    {
        struct WorldEntity_Scenery* loc = World_EntityPoolGet(pool, at);

        if( !loc || loc->element_id < 0 )
            continue;
        app_plugin_highlight_loc_resolve_one(app, loc);
    }
}

static void
app_plugin_highlights_rebuild_pools(
    struct App* app,
    bool const want[APP_PLUGIN_HL_POOL_COUNT])
{
    struct RS_HighlightState const* hl;
    struct ToriRS_HighlightItem proto;

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
        proto.kind = TORIRS_HIGHLIGHT_TILE;
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
                    proto.kind = TORIRS_HIGHLIGHT_NPC;
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
                    proto.kind = TORIRS_HIGHLIGHT_NPC;
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
                proto.kind = TORIRS_HIGHLIGHT_PLAYER;
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
        if( app_plugin_highlight_loc_cache_needs_full(app) )
        {
            app_plugin_highlight_loc_cache_build(app);
            app->plugin_highlight_loc_revision = hl->revision;
            app->plugin_highlight_loc_valid = true;
        }
        else
        {
            app_plugin_highlight_loc_cache_apply(app);
            app_plugin_highlight_loc_cache_audit(app);
        }
        /* Drained either way: the full walk has just seen everything, and the
         * incremental pass has just applied it. */
        app->world->scenery_changed_count = 0;
        app->world->scenery_changed_overflow = false;
        for( int i = 0; i < app->plugin_highlight_loc_count; i++ )
        {
            struct ToriRS_HighlightItem* out =
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
                    proto.kind = TORIRS_HIGHLIGHT_OBJ;
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
                    proto.kind = TORIRS_HIGHLIGHT_OBJ;
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
        tally[TORIRS_HIGHLIGHT_TILE],
        tally[TORIRS_HIGHLIGHT_NPC],
        tally[TORIRS_HIGHLIGHT_LOC],
        tally[TORIRS_HIGHLIGHT_OBJ],
        tally[TORIRS_HIGHLIGHT_PLAYER]);
}

static int
app_plugin_highlight_next(void* user, int iter, struct ToriRS_HighlightItem* out)
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
_Static_assert(TORIRS_KEY_SHIFT == TORIRSK_SHIFT, "plugin SHIFT keycode drifted");
_Static_assert(TORIRS_KEY_CTRL == TORIRSK_CTRL, "plugin CTRL keycode drifted");
_Static_assert(TORIRS_KEY_TAB == TORIRSK_TAB, "plugin TAB keycode drifted");
_Static_assert(TORIRS_KEY_SPACE == TORIRSK_SPACE, "plugin SPACE keycode drifted");
_Static_assert(TORIRS_KEY_ESCAPE == TORIRSK_ESCAPE, "plugin ESCAPE keycode drifted");

/* ---------------------------------------------------------------- queries */

static int
app_plugin_screen(void* user)
{
    struct App* app = (struct App*)user;
    assert(app);
    /* Handed straight across: the plugin header states enum AppScreen's own
     * values, and torirs_plugin_host.c static_asserts that it still does. */
    return (int)app->screen;
}

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

static uint64_t
app_plugin_frame_work_us(void* user)
{
    struct App* app = (struct App*)user;
    assert(app);
    /* The developer overlay's own measurement, which the shell closes before
     * the pacing sleep. A host that never calls App_NoteFrameTime -- a test
     * harness, a headless run -- reports 0 here, and a plugin reading it gets
     * "not measured" rather than a fabricated frame rate. */
    return App_LastFrameUs(app);
}

#include "plugin/native_script_hooks.gen.h"
static struct ToriRS_WidgetRef app_widget_ref(struct UITree const*,int32_t);
static void app_script_hash_word(uint64_t* hash,uint32_t value)
{
    for( int i=0;i<4;++i ) { *hash=(*hash^(value&255))*UINT64_C(1099511628211);value>>=8; }
}
static uint64_t app_script_fingerprint(struct CS2VM2_Script const* script)
{
    uint64_t hash=UINT64_C(14695981039346656037);
    int const fields[]={script->script_id,script->op_count,script->local_int_count,
        script->local_string_count,script->local_long_count,script->int_argument_count,
        script->string_argument_count,script->long_argument_count};
    for( size_t i=0;i<sizeof(fields)/sizeof(fields[0]);++i ) app_script_hash_word(&hash,(uint32_t)fields[i]);
    for( int i=0;i<script->op_count;++i )
    {
        app_script_hash_word(&hash,script->opcodes[i]);
        app_script_hash_word(&hash,script->opcodes[i]==CS2_OP_PUSH_CONSTANT_STRING ? 0 : (uint32_t)script->int_operands[i]);
        char const* text=script->opcodes[i]==CS2_OP_PUSH_CONSTANT_STRING ? script->string_operands[i] : "";
        if( text ) for( unsigned char const* p=(unsigned char const*)text;*p;++p ) hash=(hash^*p)*UINT64_C(1099511628211);
        hash=hash*UINT64_C(1099511628211);
    }
    app_script_hash_word(&hash,script->switch_table_count);
    for( int i=0;i<script->switch_table_count;++i )
    {
        struct CS2VM2_ScriptSwitch const* table=&script->switch_tables[i];
        app_script_hash_word(&hash,table->case_count);
        for( int j=0;j<table->case_count;++j )
        { app_script_hash_word(&hash,table->cases[j].key);app_script_hash_word(&hash,table->cases[j].target_pc); }
    }
    return hash;
}

static int32_t app_script_get_int(void* user,size_t index)
{
    struct CS2VM2_Thread* thread=user;
    return thread->ints_stack[thread->ints_stack_top-1-(int)index];
}
static void app_script_set_int(void* user,size_t index,int32_t value)
{
    struct CS2VM2_Thread* thread=user;
    thread->ints_stack[thread->ints_stack_top-1-(int)index]=value;
}
static char const* app_script_get_string(void* user,size_t index)
{
    struct CS2VM2_Thread* thread=user;
    return thread->strs_stack[thread->strs_stack_top-1-(int)index];
}
static bool app_script_set_string(void* user,size_t index,char const* value)
{
    struct CS2VM2_Thread* thread=user;
    char* copy=CS2VM2_StrDup(thread,value);
    if( !copy ) return false;
    thread->strs_stack[thread->strs_stack_top-1-(int)index]=copy;
    return true;
}
static void app_script_callback(void* user,struct CS2VM2_Thread* thread,char const* name)
{
    struct App* app=user;
    if( !app->plugins || App_UiLogic(app)!=APP_UI_LOGIC_CS2 || thread->frame_sp<=0 ) return;
    struct CS2VM2_Script const* script=CS2VM_FRAME(thread)->script;
    if( strcmp(name,"groundItemCaption")!=0 ) return;
    if( script->script_id!=TORIRS_GROUND_CAPTION_SCRIPT ||
        app_script_fingerprint(script)!=TORIRS_GROUND_CAPTION_FINGERPRINT ||
        thread->ints_stack_top<TORIRS_GROUND_CAPTION_INTS || thread->strs_stack_top<1 )
    {
        TORIRS_REPORT("script_callback: rejected incompatible groundItemCaption script=%d\n",script->script_id);
        return;
    }
    struct PluginScriptStack stack={.user=thread,
        .int_count=TORIRS_GROUND_CAPTION_INTS,.string_count=1,.writable_ints=TORIRS_GROUND_CAPTION_WRITE_INTS,.writable_strings=1,
        .get_int=app_script_get_int,.set_int=app_script_set_int,
        .get_string=app_script_get_string,.set_string=app_script_set_string};
    stack.widget=app_widget_ref(app->tree,app->tree ? UITree_FindByComponentId(app->tree,thread->active_component_id) : -1);
    PluginHost_ScriptCallback(app->plugins,name,CS2VM_FRAME(thread)->script->script_id,&stack);
}

static int
app_plugin_capability(void* user, char const* name)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(name);
    if( strcmp(name, "widgets.geometry") == 0 ) return 1;
    if( strcmp(name,"scripts.callbacks")==0 ) return App_UiLogic(app)==APP_UI_LOGIC_CS2;
    if( strcmp(name, "touch") == 0 )
        return app->touch_ui != 0;
    if( strcmp(name, "web") == 0 )
    {
#if defined(TORIRS_PLATFORM_WEB)
        return 1;
#else
        return 0;
#endif
    }
    if( strcmp(name, "browser") == 0 )
    {
#if defined(TORIRS_CHROME_EXEC_BROWSER_AVAILABLE)
        return 1;
#else
        return 0;
#endif
    }
    return 0;
}

static bool app_script_invalidate(void* user,char const* name)
{
    struct App* app=user;
    if( App_UiLogic(app)!=APP_UI_LOGIC_CS2 || strcmp(name,"groundItemCaption")!=0 ||
        app->host.script_ground_items_overlay<=0 ) return false;
    app->ground_items_refresh_all=1;
    return true;
}

static bool app_plugin_scene_origin(void* user,int* x,int* z)
{
    struct App* app=user;
    if( !app->world || !x || !z ) return false;
    *x=app->world->_base_tile_x;
    *z=app->world->_base_tile_z;
    return true;
}

static int
app_plugin_local_player(void* user, struct ToriRS_PlayerSnapshot* out)
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
app_plugin_npc_next(void* user, int iter, struct ToriRS_NpcSnapshot* out)
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
            static int trace_count;
            if( getenv("TORIRS_TRACE_PLUGIN_WORLD") && trace_count++<32 )
                TORIRS_REPORT("PLUGIN_NPC slot=%d base=%d type=%d element=%d\n",
                    out->server_slot,out->base_npc_id,out->npc_id,out->element_id);
            return at;
        }
        at = World_EntityPoolNext(pool, at);
    }
    return -1;
}

static int
app_plugin_npc_by_slot(void* user, int server_slot, struct ToriRS_NpcSnapshot* out)
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
app_plugin_player_next(void* user, int iter, struct ToriRS_PlayerSnapshot* out)
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
    /* The harness reads this beside the painted chat line: a plugin notice is
     * a product output, and the capture rules key on the text. */
    if( getenv("TORIRS_TRACE_NATIVE_UI") )
        TORIRS_REPORT("PLUGIN_NOTIFY text=%s\n", text);
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

    /* A hovered DECK tile outranks the root hover: the ray only reaches root
     * ground by passing the hull, so the deck is the depth-nearer surface —
     * the same preference the Walk-here row applies. Reported as the deck's
     * STAGING-ABSOLUTE tile (view base + local), which is how deck tiles are
     * addressed everywhere a plugin can hand one back (draw_tile detects the
     * staging band and draws the marker through the boat's transform). */
    if( app->world_hover_view != 0 &&
        WorldviewRegistry_IsLive(&app->worldviews, app->world_hover_view) )
    {
        struct Worldview const* view =
            WorldviewRegistry_Get(&app->worldviews, app->world_hover_view);

        *out_tile_x = view->base_x + app->world_hover_view_x;
        *out_tile_z = view->base_z + app->world_hover_view_z;
        *out_level = view->world ? World_TerrainWalkLevel(
                                       view->world,
                                       app->world_hover_view_x,
                                       app->world_hover_view_z,
                                       app->world_hover_view_level)
                                 : app->world_hover_view_level;
        return 1;
    }

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
app_plugin_hover_entity(void* user, struct ToriRS_HoverTarget* out)
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
            kind = TORIRS_HOVER_SCENERY;
            break;
        case WORLD_PICK_NPC:
            kind = TORIRS_HOVER_NPC;
            break;
        case WORLD_PICK_PLAYER:
            kind = TORIRS_HOVER_PLAYER;
            break;
        case WORLD_PICK_OBJSTACK:
            kind = TORIRS_HOVER_OBJ;
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
    APP_PLUGIN_FEATURE_SLOT_CAMERA_BIT,
    /** The whole zoom band -- zoom_closest, zoom_furthest and rest at once, by
     *  preset id rather than by offset. @see APP_PLUGIN_ZOOM_BANDS. */
    APP_PLUGIN_FEATURE_SLOT_CAMERA_ZOOM_BAND
};

/**
 * The zoom band, as one choice instead of three numbers.
 *
 * These three fields are not independent: a `min` above `max` takes the wheel
 * away entirely (app_world_camera_zooms reads `min < max` as "this revision
 * zooms"), and a `height` outside the band is a rest position the first wheel
 * notch snaps away from. Three number rows made the page's reader responsible
 * for keeping a triple consistent, and every inconsistent triple was reachable
 * in one click. A band is the thing somebody actually means to choose.
 *
 * `wheel` is deliberately NOT part of a preset. Whether the wheel is live
 * is the player's, with the revision supplying the default; it has its own row
 * directly above this one, and a preset that silently flipped it would make
 * that row disagree with the click that changed it. Range and on/off are two
 * questions and the page asks them separately.
 */
enum
{
    /** Whatever this boot resolved. Never in `values` -- the page's own
     *  "Revision default" entry is this one, and it arrives as
     *  TORIRS_FEATURE_UNSET. */
    APP_PLUGIN_ZOOM_BAND_REVISION = 0,
    APP_PLUGIN_ZOOM_BAND_STANDARD,
    APP_PLUGIN_ZOOM_BAND_CLOSE,
    APP_PLUGIN_ZOOM_BAND_UNLIMITED
};

struct AppPluginZoomBand
{
    int id;
    /*
     * Percent of the REVISION's own rest height, not eye heights.
     *
     * A preset naming absolute numbers is a preset that means something
     * different on every lane -- "OSRS: 360..1600" is a sensible band around a
     * rest of 600 and an absurd one around a rest of 200 -- and the page
     * offered it identically on all of them. As a ratio it is the same amount
     * of travel wherever it is picked. @see revconfig_camera_default_band.
     *
     * 0/0 is the exception and means the whole range this bridge accepts,
     * which is what "Unlimited" is: not a ratio of anything.
     */
    int min_pct;
    int max_pct;
};

static struct AppPluginZoomBand const APP_PLUGIN_ZOOM_BANDS[] = {
    /* What every revision gets when nothing says otherwise -- the same ratio
     * revconfig_camera_default_band hands a profile, so picking "Standard"
     * lands exactly where a default boot already was. */
    { APP_PLUGIN_ZOOM_BAND_STANDARD,
      REVCONFIG_CAMERA_ZOOM_CLOSEST_PCT,
      REVCONFIG_CAMERA_ZOOM_FURTHEST_PCT },
    /*
     * OSRS's own wheel spans zoom scale 200..1004 about a 512 rest, and this
     * tree's unit is the inverse of that scale -- an eye HEIGHT. 512/1004 and
     * 512/200 are 51% and 256%; rounded to the 60%/267% that reproduce the
     * 360..1600 these two rows already named on a rest of 600.
     */
    { APP_PLUGIN_ZOOM_BAND_CLOSE, 60, 267 },
    /* The whole band this bridge will accept, which is wider than any revision
     * states because a profile is a statement about a revision and this is a
     * person moving a camera. */
    { APP_PLUGIN_ZOOM_BAND_UNLIMITED, 0, 0 },
};

#define APP_PLUGIN_ZOOM_BAND_COUNT \
    ((int)(sizeof(APP_PLUGIN_ZOOM_BANDS) / sizeof(APP_PLUGIN_ZOOM_BANDS[0])))

/**
 * A preset's ratio, resolved against one revision's rest height.
 *
 * The same arithmetic answers both directions -- what to WRITE when the player
 * picks a preset, and which preset the current band IS -- so the page cannot
 * show one thing and set another.
 */
static void
app_plugin_zoom_band_absolute(
    struct AppPluginZoomBand const* band,
    int rest,
    int* out_closest,
    int* out_furthest)
{
    assert(band);
    assert(out_closest);
    assert(out_furthest);

    if( band->min_pct <= 0 || band->max_pct <= 0 )
    {
        *out_closest = APP_PLUGIN_FEATURE_ZOOM_MIN;
        *out_furthest = APP_PLUGIN_FEATURE_ZOOM_MAX;
        return;
    }
    *out_closest = rest * band->min_pct / 100;
    *out_furthest = rest * band->max_pct / 100;
    if( *out_closest < APP_PLUGIN_FEATURE_ZOOM_MIN )
        *out_closest = APP_PLUGIN_FEATURE_ZOOM_MIN;
    if( *out_furthest > APP_PLUGIN_FEATURE_ZOOM_MAX )
        *out_furthest = APP_PLUGIN_FEATURE_ZOOM_MAX;
    if( *out_furthest <= *out_closest )
        *out_furthest = *out_closest + 1;
}

/** Which preset a camera item is sitting on, or REVISION when none names it. */
static int
app_plugin_zoom_band_of(struct RevConfigCameraItem const* camera)
{
    assert(camera);

    for( int i = 0; i < APP_PLUGIN_ZOOM_BAND_COUNT; i++ )
    {
        struct AppPluginZoomBand const* band = &APP_PLUGIN_ZOOM_BANDS[i];
        int min = 0;
        int max = 0;

        app_plugin_zoom_band_absolute(band, camera->rest, &min, &max);
        if( camera->zoom_closest == min && camera->zoom_furthest == max )
            return band->id;
    }
    return APP_PLUGIN_ZOOM_BAND_REVISION;
}

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
    /** enum ToriRS_FeatureKind. */
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
    int values[TORIRS_FEATURE_VALUES_MAX];
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
        APP_PLUGIN_FEATURE_CAMERA_OFF(wheel),
        TORIRS_FEATURE_ENUM,
        0,
        0,
        "Mouse wheel|Fixed",
        { REVCONFIG_CAMERA_WHEEL_LIVE, REVCONFIG_CAMERA_WHEEL_PINNED },
        2,
    },
    {
        "camera_zoom_band",
        "Zoom range",
        "",
        APP_PLUGIN_FEATURE_SLOT_CAMERA_ZOOM_BAND,
        0,
        TORIRS_FEATURE_ENUM,
        0,
        0,
        /* "Revision default" is the entry the page prepends to every row; it
         * restores the boot band. Named for how far the wheel travels rather
         * than for a revision, because each is a ratio of whatever THIS
         * revision rests at and so means the same thing on all of them. */
        "Standard|Close|Unlimited",
        { APP_PLUGIN_ZOOM_BAND_STANDARD,
          APP_PLUGIN_ZOOM_BAND_CLOSE,
          APP_PLUGIN_ZOOM_BAND_UNLIMITED },
        3,
    },
    {
        "camera_wheel_step",
        "Wheel step",
        "",
        APP_PLUGIN_FEATURE_SLOT_CAMERA,
        APP_PLUGIN_FEATURE_CAMERA_OFF(wheel_step),
        TORIRS_FEATURE_INT,
        1,
        1024,
        "Fine (20)|Small (40)|Normal (60)|Large (120)|Fastest (240)",
        { 20, 40, 60, 120, 240 },
        5,
    },
    {
        "camera_distance_scale",
        "Camera distance",
        "",
        APP_PLUGIN_FEATURE_SLOT_CAMERA,
        APP_PLUGIN_FEATURE_CAMERA_OFF(distance_scale),
        TORIRS_FEATURE_INT,
        10,
        400,
        /* A percentage of the whole follow distance, which is the only zoom
         * that still bites when the camera is looking straight down -- the
         * band above moves the additive term and overhead the pitch term is
         * nine tenths of the distance. Named for what the view does, since
         * "70%" is not a thing anybody can picture. */
        "Reference (100)|Closer (85)|Close (70)|Closest (55)|Further (130)",
        { 100, 85, 70, 55, 130 },
        5,
    },
    {
        "camera_pitch_distance",
        "Overhead distance",
        "",
        APP_PLUGIN_FEATURE_SLOT_CAMERA,
        APP_PLUGIN_FEATURE_CAMERA_OFF(pitch_distance),
        TORIRS_FEATURE_INT,
        0,
        16,
        /* Fine units of eye distance per angle unit of pitch. Named for the
         * angle it actually moves: the term is `pitch * this`, so at the
         * steepest angle it is worth 383 units a step and at the flattest 128.
         * The row above scales every angle; this one buys the top-down view
         * almost alone. */
        "Reference (3)|Closer overhead (2)|Closest overhead (1)|Flat (0)",
        { 3, 2, 1, 0 },
        4,
    },
    {
        "camera_arrow_keys",
        "Arrow keys orbit",
        "",
        APP_PLUGIN_FEATURE_SLOT_CAMERA_BIT,
        REVCONFIG_CAMERA_CONTROL_ARROW_KEYS,
        TORIRS_FEATURE_ENUM,
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
        TORIRS_FEATURE_ENUM,
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
        TORIRS_FEATURE_ENUM,
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
        TORIRS_FEATURE_INT,
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
        TORIRS_FEATURE_ENUM,
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
        TORIRS_FEATURE_ENUM,
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
        TORIRS_FEATURE_ENUM,
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
        TORIRS_FEATURE_INT,
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
        TORIRS_FEATURE_ENUM,
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
        TORIRS_FEATURE_INT,
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
        TORIRS_FEATURE_ENUM,
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
    case APP_PLUGIN_FEATURE_SLOT_CAMERA_ZOOM_BAND:
        /* Three fields at once and a preset id that is in none of them.
         * app_plugin_feature_read/_set answer it without coming here. */
        break;
    }
    assert(0 && "unhandled feature slot");
    return NULL;
}

/** The camera item a flag reads, this boot's snapshot or the live one. */
static struct RevConfigCameraItem*
app_plugin_feature_camera(struct App* app, int boot)
{
    assert(app);
    return boot ? &app->plugin_feature_boot_camera : &app->revconfig_profile.camera;
}

static int
app_plugin_feature_read(
    struct App* app, struct AppPluginFeatureDesc const* desc, int boot)
{
    assert(app);
    assert(desc);

    if( desc->slot == APP_PLUGIN_FEATURE_SLOT_CAMERA_ZOOM_BAND )
        return app_plugin_zoom_band_of(app_plugin_feature_camera(app, boot));

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
app_plugin_feature_next(void* user, int iter, struct ToriRS_FeatureInfo* out)
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
        return TORIRS_FEATURE_UNSET;
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
    if( value == TORIRS_FEATURE_UNSET )
        value = app_plugin_feature_read(app, desc, 1);
    else if( desc->kind == TORIRS_FEATURE_ENUM )
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

    if( desc->slot == APP_PLUGIN_FEATURE_SLOT_CAMERA_ZOOM_BAND )
    {
        struct RevConfigCameraItem* camera = app_plugin_feature_camera(app, 0);
        struct RevConfigCameraItem const* boot_camera =
            app_plugin_feature_camera(app, 1);
        struct AppPluginZoomBand const* band = NULL;

        for( int i = 0; i < APP_PLUGIN_ZOOM_BAND_COUNT; i++ )
        {
            if( APP_PLUGIN_ZOOM_BANDS[i].id == value )
                band = &APP_PLUGIN_ZOOM_BANDS[i];
        }

        /*
         * No preset owns REVISION: it is the boot's own three numbers, which
         * nothing outside app.c can reconstruct. It arrives here whenever the
         * restore above resolved to it -- either the page picked "Revision
         * default", or this boot's band already matched no preset.
         */
        if( band )
        {
            /* The REST stays the revision's. How far the wheel may travel is
             * not a claim about where the camera sits when nobody has touched
             * it, and a preset that moved both changed the view as a
             * side-effect of widening the range. */
            app_plugin_zoom_band_absolute(
                band, camera->rest, &camera->zoom_closest, &camera->zoom_furthest);
        }
        else
        {
            camera->zoom_closest = boot_camera->zoom_closest;
            camera->zoom_furthest = boot_camera->zoom_furthest;
            camera->rest = boot_camera->rest;
        }
    }
    else
    {
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
         * Switching the wheel OFF pins the eye at the revision's rest, rather
         * than freezing it wherever the wheel happened to leave it -- "the eye
         * rests at that height and nothing moves it" is what Fixed means, and
         * a Fixed camera parked at some arbitrary scroll position is not that.
         *
         * Switching it ON needs nothing done here. Every revision now resolves
         * with a real band whether or not its mode is CLAMPED, so the room the
         * wheel needs is already there. @see revconfig_camera_default_band.
         */
        if( desc->slot == APP_PLUGIN_FEATURE_SLOT_CAMERA &&
            desc->offset == APP_PLUGIN_FEATURE_CAMERA_OFF(wheel) &&
            value == REVCONFIG_CAMERA_WHEEL_PINNED )
            app->world_cam_zoom = app->revconfig_profile.camera.rest;
    }

    /*
     * A band arrives whole, so min > max is no longer reachable from this
     * page -- the presets state ordered pairs and the restore states the
     * boot's. That is the reason the band is one row: app_world_camera_zooms
     * reads `min < max` as "this revision zooms at all", and the three-number
     * page could take the wheel away between two edits.
     */
    if( desc->slot == APP_PLUGIN_FEATURE_SLOT_CAMERA ||
        desc->slot == APP_PLUGIN_FEATURE_SLOT_CAMERA_ZOOM_BAND )
    {
        assert(
            app->revconfig_profile.camera.zoom_closest <=
            app->revconfig_profile.camera.zoom_furthest);
        /* The live eye height is a position inside the band, so a band that
         * moved under it has to pull it back in or the next wheel notch
         * starts from outside it. */
        app->world_cam_zoom = RevConfigProfile_CameraClampZoom(
            &app->revconfig_profile, app->world_cam_zoom);
    }

    /* The write as the engine now holds it, for the capture harness: the
     * plugin's pick, the store's value, both from the same read the page uses. */
    if( getenv("TORIRS_TRACE_NATIVE_UI") )
        TORIRS_REPORT("PLUGIN_FEATURE_SET key=%s value=%d readback=%d default=%d\n",
            key, value, app_plugin_feature_read(app, desc, 0), app_plugin_feature_read(app, desc, 1));
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
    case TORIRS_DISPLAY_UI_SCALE:
        value = RS_CS2Host_UiScalePercent(&app->host);
        min = RS_CS2_UI_SCALE_MIN;
        max = RS_CS2_UI_SCALE_MAX;
        break;
    case TORIRS_DISPLAY_UI_SCALE_FILTER:
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
    case TORIRS_DISPLAY_UI_SCALE:
        option = RS_CS2_DEVICEOPTION_UI_SCALE;
        break;
    case TORIRS_DISPLAY_UI_SCALE_FILTER:
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
app_plugin_frame_preference(
    void* user,
    char* out,
    int out_size,
    int* out_migration_version)
{
    struct App* app = (struct App*)user;

    _Static_assert(
        TORIRS_PLUGIN_FRAME_ID_MAX == RS_PREFS_FRAME_ID_MAX,
        "plugin frame ids and persistent frame ids must have one bound");

    assert(app);
    assert(out);
    assert(out_size > 0);
    snprintf(out, (size_t)out_size, "%s", app->prefs.preferred_frame);
    if( out_migration_version )
        *out_migration_version = app->prefs.frame_migration_version;
    return app->prefs.preferred_frame_present ? 1 : 0;
}

static int
app_plugin_frame_preference_set(
    void* user,
    char const* id,
    int migration_version)
{
    struct App* app = (struct App*)user;
    int changed;

    assert(app);
    assert(id);
    changed = RS_Prefs_SetPreferredFrame(&app->prefs, id);
    if( !app->prefs.preferred_frame_present )
        return 0;
    if( migration_version > app->prefs.frame_migration_version )
    {
        app->prefs.frame_migration_version = migration_version;
        changed = 1;
    }
    if( changed )
        app->prefs_dirty_cycle = app->logic_cycle ? app->logic_cycle : 1;
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
app_plugin_lane(void* user, struct ToriRS_LaneInfo* out)
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
        out->game = TORIRS_GAME_OLDSCHOOL;
        break;
    case RSCACHE_GAME_RS2:
        out->game = TORIRS_GAME_RS2;
        break;
    default:
        out->game = TORIRS_GAME_UNKNOWN;
        break;
    }
    switch( profile->epoch )
    {
    case RSCACHE_EPOCH_DAT1:
        out->epoch = TORIRS_CACHE_EPOCH_DAT1;
        break;
    case RSCACHE_EPOCH_DAT2:
        out->epoch = TORIRS_CACHE_EPOCH_DAT2;
        break;
    default:
        out->epoch = TORIRS_CACHE_EPOCH_UNKNOWN;
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
app_plugin_obj_info(void* user, int obj_id, struct ToriRS_ItemInfo* out)
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
        if( param->key >= 0 && param->key < TORIRS_EQUIPMENT_BONUS_COUNT )
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
    case TORIRS_INVENTORY_BACKPACK:
        return INV_MANAGER_CONTAINER_BACKPACK;
    case TORIRS_INVENTORY_WORN:
        return INV_MANAGER_CONTAINER_WORN;
    case TORIRS_INVENTORY_BANK:
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

    /*
     * A DECK tile first (SAILING: boat-aware markers). Deck tiles are
     * addressed by their STAGING-ABSOLUTE coordinates — the view's base plus
     * the deck-local tile, the same address every aboard seam speaks — and
     * those never collide with root scene tiles, so the detection is the
     * address itself. The quad is built in DECK space (the view world's own
     * heightmap at the deck plane) and each corner is pushed through the
     * hull's live transform — interpolated position, yaw and the bob —
     * exactly the descent that draws the deck, so the marker rides the boat.
     */
    {
        int local_x;
        int local_z;
        int view_id =
            App_WevHomeViewForAbsTile(app, tile_x, tile_z, &local_x, &local_z);

        if( view_id != 0 && Wevs_IsLive(&app->wevs, view_id) &&
            WorldviewRegistry_IsLive(&app->worldviews, view_id) )
        {
            struct Wev* wev = Wevs_Get(&app->wevs, view_id);
            struct Worldview* view = WorldviewRegistry_Get(&app->worldviews, view_id);
            struct WevDeckBox box;

            app_wev_deck_box(app, wev, app->world, &box);
            plane_y =
                (view->world
                     ? app_world_height_in(view->world, local_x * 128, local_z * 128, level)
                     : 0) +
                wev->y + wev->bob_y;
            for( int c = 0; c < 4; c++ )
            {
                int root_fx;
                int root_fz;
                int screen_x;
                int screen_y;

                Wev_ParentFromDeck(
                    &box,
                    (local_x + CORNER[c][0]) * 128,
                    (local_z + CORNER[c][1]) * 128,
                    &root_fx,
                    &root_fz);
                if( !app_world_project_at(app, root_fx, root_fz, plane_y, &screen_x, &screen_y) )
                    continue;
                px[count] = screen_x;
                py[count] = screen_y;
                count++;
            }
            if( count < 2 )
                return 0;
            goto emit;
        }
    }

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

emit:

    hull_size = ToriDraw_ConvexHull(px, py, count, hull_x, hull_y);
    /* The wash is the caller's fill colour, which is not always the outline's
     * -- see draw_tile in torirs_plugin_api.h. */
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
    assert(shape == TORIRS_HULL_BOUNDS || shape == TORIRS_HULL_MESH);
    before = app_overlay_count(app);
    /* Either silhouette the client already knows how to draw. Their fill
     * transparency is fixed at APP_OUTLINE_FILL_TRANS for the hover and editor
     * marks; here the plugin chooses, so an outline-only highlight is
     * possible. */
    if( shape == TORIRS_HULL_MESH )
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
    static int trace_count;
    if( getenv("TORIRS_TRACE_PLUGIN_WORLD") && trace_count++<32 )
        TORIRS_REPORT("PLUGIN_HULL element=%d shape=%d emitted=%d\n",
            element_id,shape,app_overlay_count(app)-before);
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
    /* An owned image control still showing this slot would otherwise draw
     * whatever the next publish puts there. */
    if( app->tree && (UITree_WidgetClearGraphic(app->tree, UITREE_SCENE_PLUGIN_IMAGE_BASE + slot) > 0 ||
                      UITree_WidgetClearSkin(app->tree, UITREE_SCENE_PLUGIN_IMAGE_BASE + slot) > 0) )
        app->need_redraw = 1;
}

/*
 * The client's own inventory icon for one item, copied into a plugin slot.
 *
 * Two caches sit on top of each other here and both earn their place. The
 * SCENE bridge already keeps every icon it has rasterised (obj_icon_map), so
 * the render itself is paid once per (obj, count, style) whoever asks; the
 * plugin host keeps a bounded, evicting set of the ones a plugin is drawing,
 * so the copy below is paid once per icon a plugin holds rather than per
 * frame. Neither is redundant: the first is about not re-rendering a model,
 * the second is about not filling the resident image table with pictures of
 * every item that has ever dropped.
 *
 * A COPY and not a second reference to the bridge's sprite, because a plugin
 * image handle IS a slot number -- draw_image resolves it as
 * UITREE_SCENE_PLUGIN_IMAGE_BASE + slot -- so lending the icon's own scene id
 * would mean teaching the whole image path a second kind of handle. The pixels
 * are 36x32; the copy is not what this costs.
 */
/*
 * The client's own loot record, walked.
 *
 * A COPY per step rather than a pointer into the store, for the reason every
 * other snapshot in this seam is a copy: the store grows on demand and a
 * plugin holding a row pointer across a drop would be holding freed memory.
 */
static int
app_plugin_loot_source_next(
    void* user, int iter, struct ToriRS_LootSource* out)
{
    struct App* app = (struct App*)user;
    int const next = iter + 1;

    assert(app);
    assert(out);

    if( next < 0 || next >= LootStore_SourceCount(&app->loot) )
        return -1;
    memset(out, 0, sizeof(*out));
    {
        struct LootSource const* src = &app->loot.sources[next];
        out->id = src->id;
        snprintf(out->name, sizeof(out->name), "%s", src->name ? src->name : "");
        out->row_count = src->row_count;
        out->kill_count = src->kill_count;
    }
    return next;
}

static int
app_plugin_loot_row_next(
    void* user, int source_id, int iter, struct ToriRS_LootRow* out)
{
    struct App* app = (struct App*)user;
    int const next = iter + 1;

    assert(app);
    assert(out);

    for( int i = 0; i < app->loot.source_count; i++ )
    {
        struct LootSource const* src = &app->loot.sources[i];

        if( src->id != source_id )
            continue;
        if( next < 0 || next >= src->row_count )
            return -1;
        memset(out, 0, sizeof(*out));
        out->obj_id = src->rows[next].obj_id;
        out->quantity = src->rows[next].qty;
        out->value = src->rows[next].value;
        return next;
    }
    return -1;
}

static uint64_t
app_plugin_loot_revision(void* user)
{
    struct App* app = (struct App*)user;
    assert(app);
    return LootStore_Revision(&app->loot);
}

static int
app_plugin_loot_source_clear(void* user, int source_id)
{
    struct App* app = (struct App*)user;
    assert(app);
    for( int i = 0; i < app->loot.source_count; i++ )
        if( app->loot.sources[i].id == source_id )
        {
            LootStore_RemoveById(&app->loot, source_id);
            return 1;
        }
    return 0;
}

static int
app_plugin_obj_image(
    void* user,
    int slot,
    int obj_id,
    int count,
    int style,
    int* out_w,
    int* out_h)
{
    struct App* app = (struct App*)user;
    struct ToriDraw_Sprite** sprites;
    struct ToriDraw_Sprite const* sprite;
    int scene_id;
    int found = 0;

    assert(app);
    assert(out_w);
    assert(out_h);

    switch( style )
    {
    case TORIRS_ITEM_ICON_PLAIN:
        scene_id = UITreeSceneBridge_EnsureObjIconPlain(&app->bridge, obj_id, count);
        break;
    case TORIRS_ITEM_ICON_SELECTED:
        scene_id = UITreeSceneBridge_EnsureObjIconSelected(&app->bridge, obj_id, count);
        break;
    default:
        scene_id = UITreeSceneBridge_EnsureObjIconBordered(&app->bridge, obj_id, count);
        break;
    }
    /*
     * -1 is the ORDINARY answer while the objtype or its inventory model is
     * still coming off the cache, and the bridge has already asked for both.
     * Nothing to report: the caller asks again next frame, exactly as the
     * client's own inventory reconcile does.
     */
    if( scene_id < 0 )
        return 0;

    sprites = ToriDraw_SceneSpriteGet(app->scene, scene_id, &found);
    if( !sprites || found <= 0 )
        return 0;
    sprite = sprites[0];
    if( !sprite || !sprite->pixels_argb || sprite->width <= 0 || sprite->height <= 0 )
        return 0;

    if( UITreeSceneBridge_PublishPluginImage(
            &app->bridge, slot, sprite->width, sprite->height, sprite->pixels_argb) < 0 )
        return 0;

    *out_w = sprite->width;
    *out_h = sprite->height;
    return 1;
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

    /* One line per move, not per frame: a tooltip that follows
     * the pointer reports where it landed each time it moves, and a still
     * one reports once. The harness reads this beside the pixels. */
    if( getenv("TORIRS_TRACE_NATIVE_UI") )
    {
        static int last_slot = -1, last_x, last_y, last_w, last_h;
        if( slot != last_slot || x != last_x || y != last_y || w != last_w || h != last_h )
        {
            last_slot = slot; last_x = x; last_y = y; last_w = w; last_h = h;
            TORIRS_REPORT("PLUGIN_CANVAS_IMAGE slot=%d box=%d,%d,%d,%d\n", slot, x, y, w, h);
        }
    }

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
    /*
     * The frame plugin's own suppression is not a fence. Cache/script hiding
     * and any rebuild during menu interception remain hard lifetime fences.
     *
     * A gameframe plugin hides what it is not showing -- the mobile drawer
     * puts every sidebar panel away when it is shut -- and that flag is a
     * statement about PIXELS. This press is not a click on pixels: it names a
     * component and asks for its action, which is exactly what the orb, the
     * plugin's own tab stone and every other synthesised press mean. Leaving
     * the flag as a fence is what made the minimap orbs do nothing until the
     * player opened a sidetab by hand: the run toggle lives in the controls
     * panel, which the shut drawer had hidden, so the press resolved the node,
     * built the row and was dropped without a word. A hide the CACHE or a
     * script authored (behavior.hide) still refuses, which is the difference
     * that matters -- that one means the game says this button is not there.
     */
    pick.allow_frame_hidden = 1;

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

static void
app_plugin_text_input(void* user, int on)
{
    struct App* app = (struct App*)user;

    assert(app);
    /* Raised as a pending flag rather than acted on: the App has no platform,
     * and the shell already drains statements like this beside the window-mode
     * change. @see App_TakeTextInputChange. */
    app->text_input_on = on ? 1 : 0;
    app->text_input_dirty = 1;
}

static void
app_plugin_chat_focus(void* user, int on)
{
    struct App* app = (struct App*)user;

    assert(app);
    /* A lane with no client-drawn chat line has no focus to hand out -- the
     * cache-era chatbox routes its own keys -- so this is the documented
     * no-op, the same gate app_chat_focus_tick opens with. */
    if( app_chat_node_index(app) < 0 )
        return;
    if( on )
    {
        app->chat_input_active = 1;
        /* Even when the line was already focused: the keyboard request is
         * edge-triggered, and the player may have dismissed the keyboard with
         * the focus still held -- a re-tap must bring it back. Forgetting the
         * last pushed state makes the next take push the answer again. */
        app->text_input_effective = -1;
        app->need_redraw = 1;
        return;
    }
    if( !app->chat_input_active )
        return;
    app->chat_input_active = 0;
    /* The keyboard follows by itself: app_wants_text_input reads this flag,
     * and the shell drains the change. */
    app->need_redraw = 1;
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
_Static_assert(
    APP_PLUGIN_SURFACE_PANEL == TORIRS_PLUGIN_ENGINE_DRAW_PANEL,
    "App and plugin host panel draw surfaces drifted");
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
 * The size the LANE gave a surface, before this frame moved it.
 * @see slot_native_size.
 *
 * Only the placeable roles, and not because the derived ones are hard: CANVAS
 * and SAFE are computed from the window and from what plugins have reserved,
 * so "the size it would be if nobody placed it" is not a question about them.
 * A layout plugin that asks anyway gets 0 and has lost nothing -- it already
 * knows the canvas, it was handed it.
 */
static int
app_plugin_slot_native_size(void* user, int slot, int* out_w, int* out_h)
{
    struct App* app = (struct App*)user;

    assert(app);
    if( !app->tree )
        return 0;
    if( slot < 0 || slot >= TORIRS_HOST_SURFACE_PLACEABLE_COUNT )
        return 0;
    return UITree_FrameSlotNativeSize(app->tree, slot, out_w, out_h);
}

/*
 * Where a component is. @see component_rect.
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
 * The frame slot a role names, cached per tree generation.
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
    assert(slot >= 0 && slot < TORIRS_HOST_SURFACE_PLACEABLE_COUNT);

    if( app->plugin_slot_node_gen != app->tree->generation )
    {
        for( int i = 0; i < TORIRS_HOST_SURFACE_PLACEABLE_COUNT; i++ )
            app->plugin_slot_node[i] = UITree_FrameSlotNode(app->tree, i);
        app->plugin_slot_node_gen = app->tree->generation;
    }
    return app->plugin_slot_node[slot];
}

/* The engine slot a role name stands for, or -1 for a profile role. */
static int
app_plugin_role_slot(char const* role)
{
    assert(role);
    if( strcmp(role, "canvas") == 0 )
        return TORIRS_HOST_SURFACE_CANVAS;
    return UITree_RoleSlotFromName(role);
}

/* The node a semantic role resolves to for the widget API's find: a frame
 * slot's bound node, or the profile role's. CANVAS is a rectangle and not a
 * node, so it has no answer here. */
static int32_t
app_plugin_role_node(struct App* app, char const* role)
{
    int slot;

    assert(app);
    assert(role);
    if( !app->tree )
        return -1;

    slot = app_plugin_role_slot(role);
    if( slot >= 0 && slot < TORIRS_HOST_SURFACE_PLACEABLE_COUNT )
        return app_plugin_slot_node_cached(app, slot);
    if( slot >= 0 )
        return -1;

    return UITree_RoleNodeByName(app->tree, &app->ui_roles, role);
}

_Static_assert((int)TORIRS_WIDGET_RELATION_NATIVE==(int)UITREE_WIDGET_RELATION_NATIVE &&
               (int)TORIRS_WIDGET_RELATION_OVER==(int)UITREE_WIDGET_RELATION_OVER &&
               (int)TORIRS_WIDGET_RELATION_BEHIND==(int)UITREE_WIDGET_RELATION_BEHIND &&
               (int)TORIRS_WIDGET_RELATION_REPLACE==(int)UITREE_WIDGET_RELATION_REPLACE,
               "public widget relations must match the tree's anchor relations");
_Static_assert(TORIRS_WIDGET_OP_LABEL_MAX==UITREE_MENU_OPTION_LEN,
    "public owned-control label capacity must match native menu option storage");

static struct ToriRS_WidgetRef
app_widget_ref(struct UITree const* tree, int32_t index)
{
    struct UITreeNodeRef ref = UITree_RefAt(tree, index);
    if( !ref.incarnation ) return (struct ToriRS_WidgetRef){0};
    return (struct ToriRS_WidgetRef){{ref.tree_instance, (uint64_t)ref.index + 1, ref.incarnation}};
}

static void app_widget_actions(struct App* app,int32_t node,struct UIMinimenu* menu)
{
    struct RS_MinimenuBuildCtx ctx={.tree=app->tree,.ui_host=&app->ui_host,
        .provider=app->provider,.runner=&app->runner,.invs=&app->invs,
        .events_for_component=app_minimenu_events_for_component,.events_user=app};
    UIMinimenu_Reset(menu);
    RS_Minimenu_AddWidgetRows(&ctx,node,menu);
    UIMinimenu_SortPriorityActions(menu);
}

/*
 * PLUGIN_FIND_ALL, for the harness: the numbering a find_all caller was
 * handed -- count, and the members inside it whose slot holds an invalid
 * reference. Once per answer that CHANGED for an (owner, role), so a layout
 * pass that asks every frame reports each role once and a rebuild that
 * grows a hole reports it again.
 */
static void
app_plugin_trace_find_all(
    struct App const* app, uint64_t owner, char const* role, size_t count, uint32_t missing)
{
    static struct
    {
        uint64_t owner;
        char role[32];
        size_t count;
        uint32_t missing;
    } seen[32];
    static int seen_count;
    char list[UITREE_FRAME_SLOT_NODES_MAX * 4];
    size_t used = 0;
    int at;

    assert(app);
    assert(role);
    if( !getenv("TORIRS_TRACE_PLUGIN_WORLD") )
        return;
    for( at = 0; at < seen_count; at++ )
        if( seen[at].owner == owner && strcmp(seen[at].role, role) == 0 )
            break;
    if( at < seen_count && seen[at].count == count && seen[at].missing == missing )
        return;
    if( at == seen_count && seen_count < (int)(sizeof(seen) / sizeof(seen[0])) )
        seen_count++;
    if( at < (int)(sizeof(seen) / sizeof(seen[0])) )
    {
        seen[at].owner = owner;
        snprintf(seen[at].role, sizeof(seen[at].role), "%s", role);
        seen[at].count = count;
        seen[at].missing = missing;
    }
    list[0] = '\0';
    for( int member = 0; member < UITREE_FRAME_SLOT_NODES_MAX; member++ )
        if( missing & (1u << member) )
            used += (size_t)snprintf(list + used, sizeof(list) - used, "%s%d", used ? "," : "", member);
    TORIRS_REPORT("PLUGIN_FIND_ALL owner=%s role=%s count=%zu missing=%s\n",
        app->plugins && owner >= 1 ? PluginHost_Name(app->plugins, (int)owner - 1) : "-",
        role, count, list);
}

static enum ToriRS_ContractResult
app_plugin_widget_request(void* user, uint64_t owner, struct PluginWidgetRequest* r)
{
    struct App* app = user;
    struct UITree* tree = app->tree;
    if( r->kind == PLUGIN_WIDGET_RESET_OWNER )
    {
        UITree_WidgetResetOwner(tree, owner);
        app->need_redraw = 1;
        return TORIRS_CONTRACT_OK;
    }
    if( !tree ) return TORIRS_CONTRACT_UNAVAILABLE;
    if( r->kind==PLUGIN_WIDGET_FIND_ALL && strcmp(r->name,"ground_item_labels")==0 )
    {
        /* The pinned CS2 ground-items script uses coordinate-overlay slot 0.
         * Its item captions and overflow caption use the parent-relative
         * 108-pixel inset; timer text uses absolute widths. Preserve the
         * operational graphics and timer widgets alongside these captions.
         * Older adapters have no such native overlay and report unavailable. */
        if( App_UiLogic(app)!=APP_UI_LOGIC_CS2 || app->host.script_ground_items_overlay<=0 )
            return TORIRS_CONTRACT_UNAVAILABLE;
        for( int i=0;i<RS_OVERLAY_MAX;++i )
        {
            struct RS_Overlay const* overlay=RS_OverlayGet(&app->host.overlay,i);
            if( !overlay || overlay->anchor!=RS_OVERLAY_ANCHOR_STATIC ||
                overlay->static_type!=RS_OVERLAY_TYPE_COORD || overlay->slot!=0 ) continue;
            int32_t idx=UITree_FindByComponentId(tree,overlay->component_id);
            if( idx<0 ) continue;
            for( int child=tree->components[idx].first_child;child>=0;child=tree->components[child].next_sibling )
            {
                struct UITreeComponent const* c=&tree->components[child];
                if( c->freed || c->type!=UIELEM_RS_TEXT || c->position.width_mode!=1 ||
                    c->position.width!=108 ) continue;
                if( *r->count<r->capacity ) r->refs[*r->count]=app_widget_ref(tree,child);
                ++*r->count;
            }
        }
        return *r->count>r->capacity ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
    }
    if( r->kind==PLUGIN_WIDGET_FIND_ALL )
    {
        /* A frame role spread over MEMBERS -- the four chat filters, the
         * fourteen side panels, the orb block's children -- answers them in
         * the role's own numbering: slot m IS member m, a member this frame
         * does not have is an invalid reference in its slot and never closed
         * over, and count is one past the highest member present. A caller
         * seating tab 9 or filter 3 indexes straight in; one that iterates
         * skips the invalid slots. A role with no numbering answers its one
         * node. */
        int const slot=app_plugin_role_slot(r->name);
        *r->count=0;
        if( slot>=0 && slot<TORIRS_HOST_SURFACE_PLACEABLE_COUNT )
        {
            uint32_t missing=0;
            UITree_FrameBind(tree);
            for( int member=0; member<UITREE_FRAME_SLOT_NODES_MAX; ++member )
            {
                int32_t node=UITree_FrameSlotMemberNode(tree,slot,member);
                if( (size_t)member<r->capacity )
                    r->refs[member]=node<0 ? (struct ToriRS_WidgetRef){{0}} : app_widget_ref(tree,node);
                if( node>=0 ) *r->count=(size_t)member+1;
                else missing|=1u<<member;
            }
            if( *r->count )
            {
                app_plugin_trace_find_all(app,owner,r->name,*r->count,missing&((1u<<*r->count)-1));
                return *r->count>r->capacity ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
            }
        }
        int32_t idx=app_plugin_role_node(app,r->name);
        if( strcmp(r->name,"sidebar")==0 && App_UiLogic(app)==APP_UI_LOGIC_CS2 )
            idx=UITree_FrameSlotGroupNode(tree,UITREE_FRAME_SLOT_SIDEBAR);
        if( idx<0 ) return TORIRS_CONTRACT_UNAVAILABLE;
        *r->count=1;
        app_plugin_trace_find_all(app,owner,r->name,1,0);
        if( !r->capacity ) return TORIRS_CONTRACT_BUDGET_EXCEEDED;
        r->refs[0]=app_widget_ref(tree,idx);return TORIRS_CONTRACT_OK;
    }
    if( r->kind == PLUGIN_WIDGET_FIND || r->kind == PLUGIN_WIDGET_GET )
    {
        int32_t idx = r->kind == PLUGIN_WIDGET_FIND
            ? app_plugin_role_node(app, r->name) : UITree_FindByComponentId(tree, r->id);
        if( r->kind == PLUGIN_WIDGET_FIND && strcmp(r->name,"sidebar") == 0 && App_UiLogic(app) == APP_UI_LOGIC_CS2 )
            idx = UITree_FrameSlotGroupNode(tree,UITREE_FRAME_SLOT_SIDEBAR);
        *r->refs = app_widget_ref(tree, idx);
        return r->refs->opaque[2] ? TORIRS_CONTRACT_OK : TORIRS_CONTRACT_UNAVAILABLE;
    }
    if( !r->ref.opaque[1] || r->ref.opaque[1] > INT32_MAX ) return TORIRS_CONTRACT_STALE_REFERENCE;
    struct UITreeNodeRef ref = {.tree_instance=r->ref.opaque[0],
        .index=(int32_t)r->ref.opaque[1] - 1, .incarnation=r->ref.opaque[2]};
    int32_t idx = UITree_ResolveRef(tree, ref);
    if( idx < 0 ) return TORIRS_CONTRACT_STALE_REFERENCE;
    struct UITreeComponent const* c = &tree->components[idx];
    if( r->kind >= PLUGIN_WIDGET_POSITION && c->plugin_owner && c->plugin_owner != owner )
        return TORIRS_CONTRACT_NATIVE_BLOCKED;
    switch( r->kind )
    {
    case PLUGIN_WIDGET_SET_ON_OP:
        /* Native widgets keep their native operations; only owned controls
         * take a plugin operation. */
        if( c->plugin_owner!=owner ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( !UITree_WidgetSetOperation(tree,ref,owner,r->registration,r->name) ) return TORIRS_CONTRACT_FAILED;
        break;
    case PLUGIN_WIDGET_VISIBLE:
        *r->flag=!UITree_NodeOrAncestorDisplayHidden(tree,idx) &&
            UITree_NodeNativeVisible(tree,&app->ui_host,idx,app->hover_com_id);
        return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_ACTIONS:
    case PLUGIN_WIDGET_INVOKE:
    {
        if( UITree_NodeOrAncestorDisplayHidden(tree,idx) ||
            !UITree_NodeNativeInputPresent(tree,&app->ui_host,idx) ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        struct UIMinimenu menu;
        app_widget_actions(app,idx,&menu);
        if( r->kind==PLUGIN_WIDGET_ACTIONS )
        {
            *r->count=(size_t)menu.option_count;
            for( size_t i=0;i<*r->count && i<r->capacity;++i )
            {
                r->actions[i].ref=(struct ToriRS_WidgetActionRef){r->ref,i+1,RS_Minimenu_WidgetActionRevision(&menu.options[i])};
                snprintf(r->actions[i].label,sizeof(r->actions[i].label),"%s",menu.options[i].text);
            }
            return *r->count>r->capacity ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
        }
        int op=RS_Minimenu_WidgetActionIndex(&menu,r->action.operation,r->action.revision);
        if( op<0 ) return TORIRS_CONTRACT_STALE_REFERENCE;
        /* The normal dispatcher repeats native availability and event checks.
         * A local action returns zero too; OK means dispatched, not server ack. */
        struct UIMinimenu saved=app->interact.minimenu;
        app->interact.minimenu=menu;
        app_minimenu_run_option(app,op,0,0);
        app->interact.minimenu=saved;
        return TORIRS_CONTRACT_OK;
    }
    case PLUGIN_WIDGET_PARENT:
        *r->refs=app_widget_ref(tree,c->parent);
        return r->refs->opaque[2] ? TORIRS_CONTRACT_OK : TORIRS_CONTRACT_UNAVAILABLE;
    case PLUGIN_WIDGET_PROJECTION_HEIGHT:
        if( tree->entity_overlay_index<0 || c->parent!=tree->entity_overlay_index || c->type!=UIELEM_RS_LAYER ) return TORIRS_CONTRACT_UNSUPPORTED_LAYOUT;
        if( !UITree_WidgetSetProjectionHeight(tree,ref,owner,r->a) ) return TORIRS_CONTRACT_FAILED;
        break;
    case PLUGIN_WIDGET_TEXT_OUTLINE:
        if( !UITree_WidgetSetTextOutline(tree,ref,owner,r->a!=0) ) return TORIRS_CONTRACT_FAILED;
        break;
    case PLUGIN_WIDGET_HIDDEN:
        if( !UITree_WidgetSetHidden(tree,ref,owner,r->a!=0) ) return TORIRS_CONTRACT_FAILED;
        break;
    case PLUGIN_WIDGET_CHILDREN:
        for( int32_t child = c->first_child; child >= 0; child = tree->components[child].next_sibling )
        {
            if( tree->components[child].freed ) continue;
            if( *r->count < r->capacity ) r->refs[*r->count] = app_widget_ref(tree, child);
            ++*r->count;
        }
        return *r->count > r->capacity ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_BOUNDS:
        UITree_EnsureLayoutFor(tree, idx);
        UITree_NodeDrawnBounds(tree, idx, &r->bounds->x, &r->bounds->y, &r->bounds->width, &r->bounds->height);
        return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_LOCAL_BOUNDS:
        UITree_EnsureLayoutFor(tree, idx);
        *r->bounds = (struct ToriRS_WidgetBounds){c->position.abs_x,c->position.abs_y,c->position.abs_w,c->position.abs_h};
        if( c->parent >= 0 )
        {
            r->bounds->x -= tree->components[c->parent].position.abs_x;
            r->bounds->y -= tree->components[c->parent].position.abs_y;
        }
        return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_TEXT:
    {
        if( c->type != UIELEM_RS_TEXT ) return TORIRS_CONTRACT_UNAVAILABLE;
        char const* text = c->u.rs_text.text ? c->u.rs_text.text : "";
        *r->count = strlen(text) + 1;
        if( r->capacity ) snprintf(r->text, r->capacity, "%s", text);
        return *r->count > r->capacity ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
    }
    case PLUGIN_WIDGET_CREATE_TEXT:
    {
        int font=UITreeSceneBridge_EnsureDebugFont1x(&app->bridge,TORIRS_CHROME_FONT_SMALL);
        int child=UITree_WidgetCreateText(tree,ref,owner,r->name,font);
        *r->refs=app_widget_ref(tree,child);
        if( child<0 ) return TORIRS_CONTRACT_FAILED;
        break;
    }
    case PLUGIN_WIDGET_CREATE_IMAGE:
    {
        int child=UITree_WidgetCreateGraphic(tree,ref,owner,r->name);
        *r->refs=app_widget_ref(tree,child);
        if( child<0 ) return TORIRS_CONTRACT_FAILED;
        break;
    }
    case PLUGIN_WIDGET_SET_IMAGE:
        /* r->id is a validated plugin image slot; the tree draws it at the
         * scene id the publish used (UITreeSceneBridge_PublishPluginImage:
         * UITREE_SCENE_PLUGIN_IMAGE_BASE + slot). */
        if( c->plugin_owner==owner && c->type==UIELEM_RS_GRAPHIC )
        {
            if( !UITree_WidgetSetGraphic(tree,ref,owner,UITREE_SCENE_PLUGIN_IMAGE_BASE+r->id,r->a,r->b) ) return TORIRS_CONTRACT_FAILED;
        }
        else if( !c->plugin_owner &&
                 (c->type==UIELEM_BUILTIN_SPRITE || c->type==UIELEM_RS_GRAPHIC || c->type==UIELEM_BUILTIN_COMPASS) )
        {
            /* Native re-skin: the widget keeps its own geometry; size is set_size's. */
            if( r->a || r->b ) return TORIRS_CONTRACT_INVALID_ARGUMENT;
            if( !UITree_WidgetSetArt(tree,ref,owner,UITREE_SCENE_PLUGIN_IMAGE_BASE+r->id) ) return TORIRS_CONTRACT_FAILED;
        }
        else return TORIRS_CONTRACT_NATIVE_BLOCKED;
        app->need_redraw=1;
        break;
    case PLUGIN_WIDGET_SET_MASK:
        if( c->plugin_owner ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( c->type!=UIELEM_BUILTIN_MINIMAP && c->type!=UIELEM_BUILTIN_COMPASS && c->type!=UIELEM_BUILTIN_SPRITE )
            return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( !UITree_WidgetSetMask(tree,ref,owner,r->id<0 ? 0 : UITREE_SCENE_PLUGIN_IMAGE_BASE+r->id) ) return TORIRS_CONTRACT_FAILED;
        app->need_redraw=1;
        break;
    case PLUGIN_WIDGET_OPACITY:
        if( c->plugin_owner!=owner ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( !UITree_WidgetSetTransparency(tree,ref,owner,255-r->a) ) return TORIRS_CONTRACT_FAILED;
        app->need_redraw=1;
        break;
    case PLUGIN_WIDGET_ANCHOR:
    {
        struct UITreeNodeRef target={0};
        if( r->a!=TORIRS_WIDGET_RELATION_NATIVE )
        {
            if( !r->target.opaque[1] || r->target.opaque[1]>INT32_MAX ) return TORIRS_CONTRACT_STALE_REFERENCE;
            target=(struct UITreeNodeRef){.tree_instance=r->target.opaque[0],
                .index=(int32_t)r->target.opaque[1]-1,.incarnation=r->target.opaque[2]};
        }
        switch( UITree_WidgetSetAnchor(tree,ref,owner,target,(enum UITreeWidgetRelation)r->a) )
        {
        case UITREE_WIDGET_ANCHOR_OK: break;
        case UITREE_WIDGET_ANCHOR_STALE: return TORIRS_CONTRACT_STALE_REFERENCE;
        case UITREE_WIDGET_ANCHOR_BLOCKED: return TORIRS_CONTRACT_NATIVE_BLOCKED;
        case UITREE_WIDGET_ANCHOR_BUDGET: return TORIRS_CONTRACT_BUDGET_EXCEEDED;
        default: return TORIRS_CONTRACT_INVALID_ARGUMENT;
        }
        app->need_redraw=1;
        break;
    }
    case PLUGIN_WIDGET_SET_TEXT:
    case PLUGIN_WIDGET_TEXT_COLOR:
    case PLUGIN_WIDGET_TEXT_ALIGN:
        if( c->plugin_owner!=owner || c->type!=UIELEM_RS_TEXT ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( r->kind==PLUGIN_WIDGET_SET_TEXT ) UITree_SetTextAt(tree,idx,r->name);
        else if( r->kind==PLUGIN_WIDGET_TEXT_COLOR ) UITree_SetColourAt(tree,idx,r->a);
        else UITree_SetTextAlignAt(tree,idx,r->a,r->b,0);
        break;
    case PLUGIN_WIDGET_REMOVE:
        if( c->plugin_owner!=owner ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( !UITree_WidgetRemove(tree,ref,owner) ) return TORIRS_CONTRACT_FAILED;
        break;
    case PLUGIN_WIDGET_POSITION:
    case PLUGIN_WIDGET_SIZE:
        if( !(r->kind == PLUGIN_WIDGET_POSITION
                ? UITree_WidgetSetPosition(tree, ref, owner, r->a, r->b)
                : UITree_WidgetSetSize(tree, ref, owner, r->a, r->b)) )
            return TORIRS_CONTRACT_FAILED;
        break;
    case PLUGIN_WIDGET_REVALIDATE:
        UITree_EnsureLayout(tree);
        break;
    case PLUGIN_WIDGET_RESET:
        if( !UITree_WidgetReset(tree, ref, owner) ) return TORIRS_CONTRACT_FAILED;
        break;
    default: return TORIRS_CONTRACT_UNAVAILABLE;
    }
    app->need_redraw = 1;
    return TORIRS_CONTRACT_OK;
}

/*
 * Name the frame roles a cache gameframe leaves unnamed.
 *
 * A revconfig frame spells its regions in the tree; an OldSchool toplevel
 * spells only the three the client already reads by clientCode (world,
 * minimap, compass). Its chat container, side panels, modal box and orb
 * column are ordinary layers, numbered differently on each of its toplevels.
 *
 * The engine states no name and no number for any of them. The RULE is the
 * whole of what it states: a profile role called `frame_<slot>` names the
 * node that IS that frame slot, and `frame_<slot>_<member>` names one member
 * of it, where `<slot>` is the slot vocabulary's own spelling (uitree_role.c:
 * `chat`, `sidebar`, `main_modal`, `chat_buttons`, `orbs`) and the member is
 * the role's own numbering. Which nodes those are is the profile's, one rung
 * per toplevel (`[role:frame_sidebar_3] match=id(if(161, 79))`), and a lane
 * whose profile names none pays a table lookup per name and stamps nothing.
 *
 * Stamps `slot_tag` and `frame_member_plus1`, which is what the frame layer
 * reads (frame_node_is_slot / UITree_FrameSlotIndex). Called by the tree
 * before every frame collection and by the layout tick every READY frame;
 * role resolution is memoised per generation, so it is cheap. A node stamped
 * on the last pass that this pass does not name again loses its stamp, so a
 * role that re-resolves after a CS2 rebuild does not leave two nodes both
 * claiming to be `side3`.
 */
#define APP_FRAME_STAMP_MAX 64

/** The tag a profile-named node of `slot` is stamped with, or NONE for a slot
 *  the tree recognises by widget type alone (world, minimap, compass). */
static uint8_t
app_plugin_frame_slot_tag(int slot)
{
    switch( slot )
    {
    case UITREE_FRAME_SLOT_CHAT:
        return UITREE_SLOT_CHAT;
    case UITREE_FRAME_SLOT_SIDEBAR:
        return UITREE_SLOT_SIDE_MODAL;
    case UITREE_FRAME_SLOT_MAIN_MODAL:
        return UITREE_SLOT_MAIN_MODAL;
    case UITREE_FRAME_SLOT_CHAT_BUTTONS:
        return UITREE_SLOT_CHAT_BUTTON;
    case UITREE_FRAME_SLOT_ORBS:
        return UITREE_SLOT_ORBS;
    default:
        return UITREE_SLOT_NONE;
    }
}

/** Does this role's numbering have members a profile may name one by one? */
static int
app_plugin_frame_slot_has_members(int slot)
{
    return slot == UITREE_FRAME_SLOT_SIDEBAR || slot == UITREE_FRAME_SLOT_CHAT_BUTTONS ||
           slot == UITREE_FRAME_SLOT_ORBS;
}

/* One name resolved and stamped, or nothing. */
static void
app_plugin_frame_stamp_role(
    struct App* app,
    struct UITree* tree,
    char const* role,
    uint8_t tag,
    int member,
    int32_t* next,
    int* next_count)
{
    uint16_t const role_id = UITree_RoleFind(&app->ui_roles, role);
    int32_t node;
    struct UITreeComponent* c;

    assert(app);
    assert(tree);
    assert(role);
    assert(next);
    assert(next_count);
    if( role_id == 0 || *next_count >= APP_FRAME_STAMP_MAX )
        return;
    node = UITree_RoleNode(tree, &app->ui_roles, role_id);
    if( node < 0 || (uint32_t)node >= tree->component_count )
        return;
    c = &tree->components[node];
    if( c->freed )
        return;
    c->slot_tag = tag;
    c->frame_member_plus1 = (uint8_t)(member + 1);
    next[(*next_count)++] = node;
}

static int
app_plugin_frame_root(void* user);

/* Read the control's authored LOAD arguments from the decoded cache pack.
 * Runtime hooks are a different store and do not carry authored onload. */
static int
app_plugin_frame_role_enum_id(struct App* app, int root_group, int* control)
{
    int value = -1;
    if( !app->provider || root_group < 0 ) return -1;
    int const init = RevConfigRefs_Get(&app->revconfig_refs, "script", "frame_init");
    struct ToriRS_ComponentPack const* pack =
        CacheProvider_ComponentPackGet(app->provider, root_group);
    if( init <= 0 || !ToriRS_ComponentPackLoadInt(pack, init, 2, control, &value) )
        return -1;
    return value;
}

/* A present -1 is an authored absence, distinct from a missing enum/key. */
static int
app_plugin_frame_enum_value(struct App* app, int enum_id, int key, int* value)
{
    struct ToriRS_Enum const* e = CacheProvider_EnumGet(app->provider, enum_id);
    if( !e || e->output_is_string || !e->keys || !e->int_values ) return 0;
    for( int i = 0; i < e->count; i++ )
        if( e->keys[i] == key )
        {
            *value = e->int_values[i];
            return 1;
        }
    return 0;
}

/* The plate is the unique authored graphic child of a filter container.
 * Caption/state siblings must never be substituted for that decoration. */
static int
app_plugin_chat_plate_expected(struct App* app, int filter)
{
    int const enum_id = RevConfigRefs_Get(&app->revconfig_refs, "enum", "chat_filters");
    int const chat = RevConfigRefs_Get(&app->revconfig_refs, "iface", "chat");
    struct ToriRS_Enum const* filters = CacheProvider_EnumGet(app->provider, enum_id);
    struct ToriRS_ComponentPack const* pack = CacheProvider_ComponentPackGet(app->provider, chat);
    int container = -1, graphic = -1;
    if( !filters || !pack ) return -1;
    if( !app_plugin_frame_enum_value(app, enum_id, filter, &container) )
    {
        /* Report is not in the filter enum. It is the sole other actionable
         * child of the filters' authored parent, so no copied component id
         * or translated caption is needed to identify it. */
        int first = -1;
        struct ToriRS_Component const* first_component;
        if( filter != filters->count ||
            !app_plugin_frame_enum_value(app, enum_id, 0, &first) ) return -1;
        first_component = CacheProvider_ComponentGet(app->provider, first);
        if( !first_component ) return -1;
        for( int i = 0; i < pack->component_count; i++ )
        {
            struct ToriRS_Component const* c = &pack->components[i];
            int listed = 0, actionable = 0;
            if( c->parent_id != first_component->parent_id ) continue;
            for( int j = 0; j < filters->count; j++ )
                if( filters->int_values[j] == c->id ) listed = 1;
            for( int j = 0; j < TORIRS_MENU_ACTION_SLOTS; j++ )
                if( c->ops[j][0] ) actionable = 1;
            if( listed || !actionable ) continue;
            if( container >= 0 ) return -1;
            container = c->id;
        }
    }
    if( container < 0 ) return -1;
    for( int i = 0; i < pack->component_count; i++ )
    {
        struct ToriRS_Component const* c = &pack->components[i];
        if( c->parent_id != container || c->type != TORIRS_COMPONENT_GRAPHIC ) continue;
        if( graphic >= 0 ) return -1;
        graphic = c->id;
    }
    return graphic;
}

/**
 * The interface-161 uid a role's chain names, which is the key the cache's own
 * scripts address that element by. -1 when the profile declares no 161 rung.
 *
 * 161 is the canonical spelling because that is the interface every rung in
 * enum 1129/1130/1131/1745 is keyed on -- the cache reaches the chat as
 * `interface_161:96` whichever toplevel is up, and the enum turns that into
 * 548:11, 164:93 or 601:49. A profile that carries the 161 rung therefore
 * carries the key, and the other three rungs are derivable rather than
 * copied.
 */
static int
app_plugin_frame_role_key_161(struct UITreeRoleTable const* table, uint16_t role_id)
{
    struct UITreeRoleEntry const* entry;

    assert(table);
    if( role_id == 0 || (int)role_id > table->count )
        return -1;
    entry = &table->entries[role_id - 1];
    for( int i = 0; i < entry->matcher_count; i++ )
        if( entry->matchers[i].kind == UITREE_ROLE_MATCH_ID &&
            ((entry->matchers[i].uid >> 16) & 0xffff) == 161 )
            return entry->matchers[i].uid;
    return -1;
}

/* Native metadata is authoritative for compatible roots and shared chat
 * pieces. Explicit profile values remain independently audited for drift. */
static int32_t
app_plugin_frame_role_fallback(struct UITree const* tree, struct UITreeRoleTable const* table,
                               uint16_t role, void* user)
{
    struct App* app = user;
    if( role == 0 || role > table->count ) return -2;
    char const* name = table->entries[role - 1].name;
    if( strncmp(name, "chat_plate_", 11) == 0 &&
        RevConfigRefs_Get(&app->revconfig_refs, "enum", "chat_filters") >= 0 )
    {
        char* end;
        long filter = strtol(name + 11, &end, 10);
        if( end == name + 11 || *end || filter < 0 || filter > 255 ) return -1;
        int uid = app_plugin_chat_plate_expected(app, (int)filter);
        return uid < 0 ? -1 : UITree_FindByComponentId(tree, uid);
    }
    if( strncmp(name, "frame_", 6) && strncmp(name, "sidetab_", 8) ) return -2;
    int key = app_plugin_frame_role_key_161(table, role), uid = -1, control = -1;
    int root = app_plugin_frame_root(app);
    int element_map = app_plugin_frame_role_enum_id(app, root, &control);
    if( key < 0 || RevConfigRefs_Get(&app->revconfig_refs, "script", "frame_init") < 0 ) return -2;
    if( element_map < 0 ) return -1;
    if( !app_plugin_frame_enum_value(app, element_map, key, &uid) || uid < 0 ) return -1;
    int32_t node = UITree_FindByComponentId(tree, uid);
    if( node >= 0 && getenv("TORIRS_FRAME_ROLE_AUDIT") )
        TORIRS_LOG("frameroles: derived %s root=%d enum=%d key=(161|%d) -> (%d|%d)\n",
                   name, root, element_map, key & 65535, uid >> 16, uid & 65535);
    return node;
}

static int
app_plugin_frame_role_audit(struct App* app, struct UITree* tree)
{
    int const root = app_plugin_frame_root(app);
    int control = -1;
    int const enum_id = app_plugin_frame_role_enum_id(app, root, &control);
    int const chat = RevConfigRefs_Get(&app->revconfig_refs, "iface", "chat");
    int checked = 0, mismatched = 0, unbound = 0, absent = 0;
    struct ToriRS_ComponentPack const* chat_pack = CacheProvider_ComponentPackGet(app->provider, chat);
    /* The chat is server-mounted after the root; wait for the pack before
     * auditing its plates rather than permanently reporting startup misses. */
    if( root <= 0 || enum_id < 0 || !CacheProvider_EnumGet(app->provider, enum_id) ||
        !chat_pack || chat_pack->component_count == 0 ||
        UITree_FindByComponentId(tree, chat_pack->components[0].id) < 0 ||
        app_plugin_chat_plate_expected(app, 0) < 0 ) return 0;
    TORIRS_REPORT("frameroles: root %d control=(%d|%d) authored enum=%d\n",
               root, (control >> 16) & 0xffff, control & 0xffff, enum_id);
    for( int i = 0; i < app->ui_roles.count; i++ )
    {
        struct UITreeRoleEntry const* entry = &app->ui_roles.entries[i];
        int want = -1, known = 0, key = -1;
        int32_t node;
        if( strncmp(entry->name, "chat_plate_", 11) == 0 )
        {
            char* end;
            long filter = strtol(entry->name + 11, &end, 10);
            if( end == entry->name + 11 || *end || filter < 0 || filter > 255 ) continue;
            want = app_plugin_chat_plate_expected(app, (int)filter);
            known = want >= 0;
        }
        else if( strncmp(entry->name, "frame_", 6) == 0 ||
                 strncmp(entry->name, "sidetab_", 8) == 0 )
        {
            key = app_plugin_frame_role_key_161(&app->ui_roles, (uint16_t)(i + 1));
            if( key < 0 ) continue; /* Shared-pack members have no toplevel enum key. */
            known = app_plugin_frame_enum_value(app, enum_id, key, &want);
        }
        else continue;
        checked++;
        node = UITree_RoleNode(tree, &app->ui_roles, (uint16_t)(i + 1));
        int const got = node < 0 || (uint32_t)node >= tree->component_count ||
                        tree->components[node].freed ? -1 : tree->components[node].component_id;
        int declared = got;
        if( key >= 0 || strncmp(entry->name, "chat_plate_", 11) == 0 )
            for( int m = 0; m < entry->matcher_count; m++ )
                if( (entry->matchers[m].kind == UITREE_ROLE_MATCH_ID || entry->matchers[m].kind == UITREE_ROLE_MATCH_IFACE) &&
                    ((entry->matchers[m].uid >> 16) & 65535) == (key >= 0 ? root : chat) )
                { declared = entry->matchers[m].uid; break; }
        if( known && declared >= 0 && declared != want )
        {
            mismatched++;
            TORIRS_REPORT("frameroles: %s MISMATCH declared=(%d|%d) expected=(%d|%d) root=%d enum=%d\n",
                       entry->name, declared >> 16, declared & 65535, want >> 16, want & 65535, root, enum_id);
            continue;
        }
        if( known && want == -1 && got == -1 ) { absent++; continue; }
        if( got == -1 )
        {
            unbound++;
            TORIRS_REPORT("frameroles: %s UNBOUND on root %d expected=(%d|%d)\n",
                       entry->name, root, (want >> 16) & 0xffff, want & 0xffff);
        }
        else if( !known || want != got )
        {
            mismatched++;
            TORIRS_REPORT("frameroles: %s MISMATCH root=%d got=(%d|%d) expected=(%d|%d) enum=%d key=(%d|%d) known=%d\n",
                       entry->name, root, (got >> 16) & 0xffff, got & 0xffff,
                       (want >> 16) & 0xffff, want & 0xffff, enum_id,
                       (key >> 16) & 0xffff, key & 0xffff, known);
        }
    }
    TORIRS_REPORT("frameroles: root %d, %d roles checked, %d absent, %d unbound, %d mismatched\n",
               root, checked, absent, unbound, mismatched);
    return 1;
}

static void
app_plugin_frame_bind(struct UITree* tree, void* user)
{
    struct App* app = (struct App*)user;
    int32_t next[APP_FRAME_STAMP_MAX];
    int next_count = 0;

    assert(tree);
    assert(app);

    /* A revconfig frame authors its own regions; stamping over them would
     * hand a builtin's job to whatever a stray role happened to match. */
    if( App_UiLogic(app) != APP_UI_LOGIC_CS2 )
        return;

    if( app->ui_roles.fallback != app_plugin_frame_role_fallback || app->ui_roles.fallback_user != app )
    {
        app->ui_roles.fallback = app_plugin_frame_role_fallback;
        app->ui_roles.fallback_user = app;
        for( int i = 0; i < app->ui_roles.count; i++ ) app->ui_roles.entries[i].memo_valid = 0;
    }
    for( int slot = 0; slot < UITREE_FRAME_SLOT_COUNT; slot++ )
    {
        uint8_t const tag = app_plugin_frame_slot_tag(slot);
        char const* name = UITree_RoleSlotName(slot);
        char role[UITREE_ROLE_NAME_MAX];

        if( tag == UITREE_SLOT_NONE || !name )
            continue;
        snprintf(role, sizeof(role), "frame_%s", name);
        app_plugin_frame_stamp_role(app, tree, role, tag, -1, next, &next_count);
        if( !app_plugin_frame_slot_has_members(slot) )
            continue;
        for( int member = 0; member < UITREE_FRAME_SLOT_NODES_MAX; member++ )
        {
            snprintf(role, sizeof(role), "frame_%s_%d", name, member);
            app_plugin_frame_stamp_role(app, tree, role, tag, member, next, &next_count);
        }
    }

    /* One audit per root, when asked for: the rungs are hand-copied from a
     * table the cache also ships, and a mistyped one is otherwise silent. */
    {
        static int audited_root = -1;
        static uint64_t audited_incarnation;
        static struct UITree const* audited_tree;
        int const root = app_plugin_frame_root(app);
        if( root > 0 && getenv("TORIRS_FRAME_ROLE_AUDIT") )
        {
            int control = -1;
            (void)app_plugin_frame_role_enum_id(app, root, &control);
            int32_t const node = UITree_FindByComponentId(tree, control);
            uint64_t const incarnation = node >= 0 ? tree->components[node].incarnation : 0;
            if( (tree != audited_tree || root != audited_root || incarnation != audited_incarnation) &&
                app_plugin_frame_role_audit(app, tree) )
            {
                audited_tree = tree;
                audited_root = root;
                audited_incarnation = incarnation;
            }
        }
    }

    /* Take back the stamps of last pass's nodes that are still alive and were
     * not named again. By incarnation: a recycled index is a different node. */
    for( int i = 0; i < app->plugin_frame_stamp_count; i++ )
    {
        int32_t const idx = app->plugin_frame_stamp[i].node;
        struct UITreeComponent* c;
        int again = 0;

        if( idx < 0 || (uint32_t)idx >= tree->component_count )
            continue;
        c = &tree->components[idx];
        if( c->freed || c->incarnation != app->plugin_frame_stamp[i].incarnation )
            continue;
        for( int n = 0; n < next_count && !again; n++ )
            again = next[n] == idx;
        if( again )
            continue;
        c->slot_tag = UITREE_SLOT_NONE;
        c->frame_member_plus1 = 0;
    }
    for( int n = 0; n < next_count; n++ )
    {
        app->plugin_frame_stamp[n].node = next[n];
        app->plugin_frame_stamp[n].incarnation = tree->components[next[n]].incarnation;
    }
    app->plugin_frame_stamp_count = next_count;
}

/**
 * The live gameframe's root interface group, or -1 on a revconfig frame.
 * @see frame_root.
 */
static int
app_plugin_frame_root(void* user)
{
    struct App* app = (struct App*)user;

    assert(app);
    if( App_UiLogic(app) != APP_UI_LOGIC_CS2 )
        return -1;
    return app->host.top_interface_id > 0 ? app->host.top_interface_id : -1;
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

/* The canvas less the soft keyboard, for ToriRS_GameframeEvent.safe. The
 * layout's own answer, not a second computation off app->keyboard_inset: a
 * provider hanging a strip from the bottom edge must be dodging exactly the
 * band a profile-authored `safe_area=os:bottom` row dodges, and the clamping
 * (a keyboard can never cover the WHOLE canvas) lives there. */
static int
app_plugin_platform_safe_rect(void* user, int* out_x, int* out_y, int* out_w, int* out_h)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(out_x);
    assert(out_y);
    assert(out_w);
    assert(out_h);
    (void)app;
    *out_x = 0;
    *out_y = 0;
    *out_w = UITREE_LAYOUT_ROOT_W;
    *out_h = UITree_LayoutSafeBottomEdge();
    return 1;
}

static void
app_plugin_frame_activate(void* user, int active, int canvas, int fixed_w, int fixed_h)
{
    struct App* app = (struct App*)user;

    assert(app);
    app->plugin_frame_active = active ? 1 : 0;
    app->plugin_layout_canvas = canvas;
    app->plugin_layout_fixed_w = fixed_w;
    app->plugin_layout_fixed_h = fixed_h;
    /* A committed selection change invalidates the frame on screen: native
     * restores the lane's chrome, while a plugin offer supplies its complete
     * validated declaration. */
    app->plugin_layout_dirty = 1;
    if( !app->plugin_frame_active && app->tree )
        UITree_FrameRelease(app->tree);
    App_SyncPluginLayoutCanvas(app);
}

static void
app_plugin_frame_provide(void* user, uint64_t owner)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(owner);
    if( !app->tree )
        return;
    if( !app->plugin_frame_active )
    {
        UITree_FrameRelease(app->tree);
        return;
    }
    UITree_FrameProvide(app->tree, app_plugin_layout_root_group(app), owner);
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
    /*
     * On a cache gameframe the selection is the cache's: its tab script
     * (`toplevel_sidebutton_switch`) hides every side panel but the chosen
     * one and writes a varc this client does not read. The panel that is
     * NOT hidden is therefore the answer, read off the sidebar members the
     * frame binder named. A frame with no named panels -- the roles are the
     * profile's, and a boot before the tree exists has none -- falls through
     * to the 2004 client's own selection.
     */
    if( App_UiLogic(app) == APP_UI_LOGIC_CS2 && app->tree )
    {
        int named = 0;
        for( int tab = 0; tab < RS_UI_SLOTS_TAB_MAX; tab++ )
        {
            int32_t const node =
                UITree_FrameSlotMemberNode(app->tree, TORIRS_HOST_SURFACE_SIDEBAR, tab);
            if( node < 0 )
                continue;
            named = 1;
            if( !app->tree->components[node].behavior.hide )
                return tab;
        }
        if( named )
            return -1;
    }
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
    /*
     * A cache gameframe switches tabs by SCRIPT: the stone's op runs the
     * cache's own switch, which unhides the panel, lights the stone and
     * records the choice in a varc the rest of its scripts read. Running that
     * script with the tab number is what a click on the stone would have
     * done, and it is the only way the cache's own state agrees with the
     * frame afterwards. The profile names the script; a lane whose profile
     * does not is a lane where a plugin stone cannot switch tabs, said once.
     */
    if( App_UiLogic(app) == APP_UI_LOGIC_CS2 )
    {
        int const script = RevConfigRefs_Get(&app->revconfig_refs, "script", "sidebar_switch");
        int args[1];

        if( !RS_UISlots_TabGiven(app, tabno) )
            return 0;
        if( script <= 0 )
        {
            static int said;
            if( !said++ )
                TORIRS_LOG("plugin: no [script:sidebar_switch] in this profile; "
                           "a plugin frame cannot switch sidebar tabs\n");
            return 0;
        }
        args[0] = tabno;
        RS_CS2_RunScript(&app->host, &app->runner, script, args, 1, 0, NULL, 0);
        app->need_redraw = 1;
        return 1;
    }
    if( !RS_UISlots_TabEnabled(&app->slots, tabno) )
        return 0;
    RS_UISlots_SetSideTab(app, tabno);
    return 1;
}

/*
 * Has the server given this player that tab?
 *
 * Asked on behalf of a plugin gameframe that has replaced the client's own
 * chrome and inherited its duty: neither the icon nor the pressed stone is
 * drawn for a tab the player has not got. Every lane rule is
 * RS_UISlots_TabGiven's -- this is the seam, not the knowledge.
 */
static int
app_plugin_tab_enabled(void* user, int tabno)
{
    struct App* app = (struct App*)user;

    assert(app);
    return RS_UISlots_TabGiven(app, tabno);
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
    /*
     * A skill the server has not stated yet has no reading, and saying so is
     * the whole contract: RS_PlayerStats_Init seeds a FRESH ACCOUNT (level 1,
     * 10 hitpoints) so that CS1 value scripts evaluate before a sync exists,
     * which means the pre-login table is not empty -- it is someone else's.
     * A tracker that seeded its session from it would take the login burst,
     * where all 25 skills arrive at once, as one enormous gain. That is what
     * last_seen_level exists to tell apart. @see struct RS_PlayerStats.
     */
    if( app->stats.last_seen_level[skill] == 0 )
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

static int
app_plugin_menu_drop(void* user, void* cursor, int index)
{
    struct UIMinimenu* menu = (struct UIMinimenu*)cursor;

    (void)user;
    assert(menu);
    if( index < 0 || index >= menu->option_count )
        return 0;
    /* Compacted in place, as the region trim above does: rows above the
     * dropped one move down, and a caller dropping several walks from the
     * top. The final SortPriorityActions puts Cancel back where it belongs. */
    for( int i = index + 1; i < menu->option_count; i++ )
        menu->options[i - 1] = menu->options[i];
    menu->option_count--;
    return 1;
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
    struct ToriRS_MenuBuildEvent ev;

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
        struct ToriRS_MenuRow* row = &ev.rows[i];

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
    engine.widget_request = app_plugin_widget_request;
    engine.screen = app_plugin_screen;
    engine.platform_safe_rect = app_plugin_platform_safe_rect;
    engine.world_cycle = app_plugin_world_cycle;
    engine.frame_ms = app_plugin_frame_ms;
    engine.frame_work_us = app_plugin_frame_work_us;
    engine.capability = app_plugin_capability;
    engine.script_invalidate = app_script_invalidate;
    engine.memory_bytes = app_plugin_memory_bytes;
    engine.local_player = app_plugin_local_player;
    engine.scene_origin = app_plugin_scene_origin;
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
    engine.frame_preference = app_plugin_frame_preference;
    engine.frame_preference_set = app_plugin_frame_preference_set;
    engine.varbit = app_plugin_varbit;
    engine.varp = app_plugin_varp;
    engine.cache_id = app_plugin_cache_id;
    engine.lane = app_plugin_lane;
    engine.frame_root = app_plugin_frame_root;
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
    engine.obj_image = app_plugin_obj_image;
    engine.loot_source_next = app_plugin_loot_source_next;
    engine.loot_row_next = app_plugin_loot_row_next;
    engine.loot_revision = app_plugin_loot_revision;
    engine.loot_source_clear = app_plugin_loot_source_clear;
    engine.draw_image = app_plugin_draw_image;
    engine.if_click = app_plugin_if_click;
    engine.text_input = app_plugin_text_input;
    engine.chat_focus = app_plugin_chat_focus;
    engine.mouse_pos = app_plugin_mouse_pos;
    engine.slot_native_size = app_plugin_slot_native_size;
    engine.component_rect = app_plugin_component_rect;
    engine.menu_drop = app_plugin_menu_drop;
    engine.frame_activate = app_plugin_frame_activate;
    engine.frame_provide = app_plugin_frame_provide;
    engine.tab_active = app_plugin_tab_active;
    engine.tab_select = app_plugin_tab_select;
    engine.tab_enabled = app_plugin_tab_enabled;
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
