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
    snprintf(out->name, sizeof(out->name), "%s", scenery->name);

    /* The ops are a facet array with a per-slot name; a slot with no name is
     * an op the loc does not offer. Packed to a bitmask here because that is
     * the only question a plugin asks of them -- the TEXT of an op is the
     * minimenu's business, and a plugin that wants it reads the menu build. */
    for( int i = 0; i < 5; i++ )
        if( scenery->actions[i].name[0] != '\0' )
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

/*
 * Does an OP GROUP name this thing?
 *
 * The 7040 family's subject is a right-click NAME and applies across the pools
 * -- `_7041("Cow", 9)` is about every Cow, npc or not -- so every entity pass
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
 * Rebuild the resolved list.
 *
 * One pass per kind, and each pass walks the pool it needs at most once: a
 * member list is short (the largest real one is the 109 loctypes the cache
 * marks) and the pools are long, so the inner test is against the members and
 * the outer walk is the pool.
 */
static void
app_plugin_highlights_rebuild(struct App* app)
{
    struct RS_HighlightState const* hl;
    struct ToriRS_PluginHighlightItem proto;

    assert(app);

    app->plugin_highlight_count = 0;
    if( !app->world )
        return;
    hl = &app->host.highlight;

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
                int const group = app_plugin_opgroup_group(hl, npc->name);
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

    /* ---- locs: by type anywhere, or by type at one coord. ---- */
    {
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
                    snprintf(proto.name, sizeof(proto.name), "%s", loc->name);
                    proto.tile_x = tile_x;
                    proto.tile_z = tile_z;
                    proto.level = loc->grid_position.level;
                    proto.size_x = loc->size_x > 0 ? loc->size_x : 1;
                    proto.size_z = loc->size_z > 0 ? loc->size_z : 1;
                    proto.flags |= m->flags;
                    if( !app_plugin_highlight_push(app, &proto) )
                        return;
                }
            }

            {
                int const group = app_plugin_opgroup_group(hl, loc->name);
                if( group >= 0 &&
                    app_plugin_highlight_begin(app, RS_HIGHLIGHT_OPGROUP, group, &proto) )
                {
                    proto.kind = TORIRS_PLUGIN_HL_LOC;
                    proto.element_id = loc->element_id;
                    proto.overhead_height = app_plugin_element_height(app, loc->element_id);
                    proto.fine_x = (loc->grid_position.x * 128) + 64;
                    proto.fine_z = (loc->grid_position.z * 128) + 64;
                    snprintf(proto.name, sizeof(proto.name), "%s", loc->name);
                    proto.tile_x = tile_x;
                    proto.tile_z = tile_z;
                    proto.level = loc->grid_position.level;
                    proto.size_x = loc->size_x > 0 ? loc->size_x : 1;
                    proto.size_z = loc->size_z > 0 ? loc->size_z : 1;
                    if( !app_plugin_highlight_push(app, &proto) )
                        return;
                }
            }
        }
    }

    /* ---- ground items ---- */
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
                int const group = app_plugin_opgroup_group(hl, stack->name);
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
    if( !getenv("TORIRS_HIGHLIGHT_DEBUG") )
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

/**
 * The topmost region covering a canvas point, or -1.
 *
 * Walked backwards: regions are recorded in DRAW order, so the last one to
 * cover the point is the one drawn on top, and the one a click belongs to.
 */
static int
app_plugin_region_at(struct App const* app, int x, int y)
{
    assert(app);

    for( int i = app->plugin_region_count - 1; i >= 0; i-- )
    {
        struct AppPluginRegion const* region = &app->plugin_regions[i];
        if( x < region->x || x >= region->x + region->w )
            continue;
        if( y < region->y || y >= region->y + region->h )
            continue;
        return i;
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
static int
app_plugin_if_click(void* user, int component_id, int op)
{
    struct App* app = (struct App*)user;
    struct UIMinimenu scratch;
    struct UIMinimenu saved;
    struct UIMinimenuPick pick;
    int32_t node;
    int action;
    int action_index;
    int button_type;

    assert(app);

    if( !app->tree )
        return 0;
    /* A component id this tree does not hold is bad input -- a config key
     * naming a button that is not on this cache -- not a broken contract. */
    node = UITree_FindByComponentId(app->tree, component_id);
    if( node < 0 )
        return 0;

    memset(&pick, 0, sizeof(pick));
    pick.kind = UI_MINIMENU_PICK_UI;
    pick.id = component_id;

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
    if( out_x )
        *out_x = c->position.abs_x;
    if( out_y )
        *out_y = c->position.abs_y;
    if( out_w )
        *out_w = c->position.abs_w;
    if( out_h )
        *out_h = c->position.abs_h;
    return 1;
}

/*
 * The three boxes a plugin may anchor to. @see ToriRS_PluginApi::anchor_rect.
 *
 * MODAL is the interesting one, and it is resolved in two steps because the
 * two gameframe families state it in two different places. A dat1 frame
 * DECLARES a region for it -- `[component:main_modal_region] slot=main_modal`
 * -- and that is an answer available from boot, before anything has opened. A
 * dat2 frame declares nothing: the server names the host component in
 * IF_OPENSUB, and it is a different one in the fixed frame than in the
 * resizable one, so the only thing that knows is the mount. App records it
 * there (App::modal_host_uid) and this reads it back.
 */
static int
app_plugin_anchor_rect(
    void* user, int which, int* out_x, int* out_y, int* out_w, int* out_h)
{
    struct App* app = (struct App*)user;

    assert(app);

    switch( which )
    {
    case TORIRS_PLUGIN_ANCHOR_CANVAS:
        if( out_x )
            *out_x = 0;
        if( out_y )
            *out_y = 0;
        if( out_w )
            *out_w = UITREE_LAYOUT_ROOT_W;
        if( out_h )
            *out_h = UITREE_LAYOUT_ROOT_H;
        return 1;

    case TORIRS_PLUGIN_ANCHOR_VIEWPORT:
        if( !app->world_view_valid || app->world_emit_desc.w <= 0 ||
            app->world_emit_desc.h <= 0 )
            return 0;
        if( out_x )
            *out_x = app->world_emit_desc.x;
        if( out_y )
            *out_y = app->world_emit_desc.y;
        if( out_w )
            *out_w = app->world_emit_desc.w;
        if( out_h )
            *out_h = app->world_emit_desc.h;
        return 1;

    case TORIRS_PLUGIN_ANCHOR_MODAL:
        if( app_plugin_node_rect(
                app, app->slots.main_modal_index, out_x, out_y, out_w, out_h) )
            return 1;
        if( app->modal_host_uid < 0 || !app->tree )
            return 0;
        return app_plugin_node_rect(
            app,
            UITree_FindByComponentId(app->tree, app->modal_host_uid),
            out_x,
            out_y,
            out_w,
            out_h);

    default:
        return 0;
    }
}

/* ------------------------------------------------------------ the gameframe */

/*
 * The three overlay lists and the host's three draw surfaces are one
 * numbering. app.h restates it rather than including the host's
 * implementation file, so this is where the two are held together.
 */
_Static_assert(
    (int)APP_PLUGIN_SURFACE_WORLD == 0 && (int)APP_PLUGIN_SURFACE_CANVAS == 1 &&
        (int)APP_PLUGIN_SURFACE_FRAME == 2,
    "AppPluginSurface must match the host's PluginDrawSurface");

/*
 * The host hands out image handles and the scene stores the pixels, so a
 * handle past the scene's range is an image that decodes and never draws --
 * reported as "would not decode", which is the one thing it is not. This is
 * the only file that sees both numbers.
 */
_Static_assert(
    TORIRS_PLUGIN_IMAGES_MAX <= UITREE_SCENE_PLUGIN_IMAGE_SLOTS,
    "every plugin image handle needs a scene slot to publish into");

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
}

static int
app_plugin_layout_slot(void* user, int slot, int x, int y, int w, int h)
{
    struct App* app = (struct App*)user;
    struct AppPluginLayoutSlot* out;

    assert(app);
    assert(slot >= 0 && slot < TORIRS_PLUGIN_SLOT_COUNT);

    out = &app->plugin_layout_slots[slot];
    out->placed = 1;
    out->x = x;
    out->y = y;
    out->w = w;
    out->h = h;
    /* The answer is about the FRAME and not about the recording: a plugin asks
     * "did that land on anything" so it knows whether to draw the housing for
     * it, and a frame with no compass should get no compass ring. */
    return app->tree && UITree_FrameSlotNode(app->tree, slot) >= 0;
}

static void
app_plugin_layout_end(void* user)
{
    struct App* app = (struct App*)user;
    struct UITreeFrameSlotRect rects[TORIRS_PLUGIN_SLOT_COUNT];

    assert(app);
    if( !app->tree )
        return;

    for( int i = 0; i < TORIRS_PLUGIN_SLOT_COUNT; i++ )
    {
        rects[i].placed = app->plugin_layout_slots[i].placed;
        rects[i].x = app->plugin_layout_slots[i].x;
        rects[i].y = app->plugin_layout_slots[i].y;
        rects[i].w = app->plugin_layout_slots[i].w;
        rects[i].h = app->plugin_layout_slots[i].h;
    }
    UITree_FrameApply(app->tree, rects, app_plugin_layout_root_group(app));
    app->plugin_layout_w = UITREE_LAYOUT_ROOT_W;
    app->plugin_layout_h = UITREE_LAYOUT_ROOT_H;
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
    for( int i = app->plugin_region_count - 1; i >= 0; i-- )
    {
        struct AppPluginRegion const* region = &app->plugin_regions[i];
        struct UIMinimenuPick pick;

        if( region->op_count <= 0 )
            continue;
        if( click_x < region->x || click_x >= region->x + region->w )
            continue;
        if( click_y < region->y || click_y >= region->y + region->h )
            continue;

        memset(&pick, 0, sizeof(pick));
        pick.kind = UI_MINIMENU_PICK_NONE;
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
        break;
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
    engine.varbit = app_plugin_varbit;
    engine.varp = app_plugin_varp;
    engine.cache_id = app_plugin_cache_id;
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
    engine.anchor_rect = app_plugin_anchor_rect;
    engine.layout_set = app_plugin_layout_set;
    engine.layout_begin = app_plugin_layout_begin;
    engine.layout_end = app_plugin_layout_end;
    engine.layout_slot = app_plugin_layout_slot;
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
