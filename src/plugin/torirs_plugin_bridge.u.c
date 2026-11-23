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
int app_plugin_asset_read(void* user, char const* plugin, char const* name);
int app_plugin_script_read(void* user, char const* path, int serial,
    void (*deliver)(void* deliver_user, int serial, char const* path, void* data, int size), void* deliver_user);
int
app_plugin_asset_write(void* user, char const* plugin, char const* name, void const* data, int size);
int
app_plugin_screenshot(
    void* user,
    char const* plugin,
    char const* dir,
    char const* name,
    char* out_path,
    int out_path_size);
int app_plugin_capture_root(void* user, char const* plugin, char const* dir, char* out, int out_size);
int app_plugin_files_request(void* user, int serial, char const* verb, char const* body, int size);
int app_plugin_capture_read(void* user, int serial, char const* path);
int app_plugin_model_publish(void* user, int model, void const* data, int size);
void app_plugin_model_release(void* user, int model);
int app_plugin_mesh_create(void* user);
void app_plugin_mesh_destroy(void* user, int mesh);
int app_plugin_mesh_vertex(void* user, int mesh, int x, int y, int z);
int app_plugin_mesh_face(void* user, int mesh, int a, int b, int c, int hsl, int alpha);
int app_plugin_object_create(void* user);
void app_plugin_object_destroy(void* user, int handle);
void app_plugin_object_set_model(void* user, int handle, int source, int id);
void app_plugin_object_recolor(void* user, int handle, int hsl_from, int hsl_to);
void app_plugin_object_clear_recolors(void* user, int handle);
void app_plugin_object_set_anim(void* user, int handle, int seq_id, int loop);
void app_plugin_object_set_light(void* user, int handle, int ambient, int contrast);
void app_plugin_object_set_position(
    void* user,
    int handle,
    int tile_x,
    int tile_z,
    int level,
    int height,
    int yaw);
void app_plugin_object_set_active(void* user, int handle, int active);
int app_plugin_object_ready(void* user, int handle);
void app_plugin_object_set_fine_position(
    void* user, int handle, int fine_x, int fine_z, int level, int height, int yaw);
int app_plugin_object_element(void* user, int handle);
int app_plugin_object_hit(void* user, int handle);
/* Re-place every plugin object after a scene rebuild. Called from the
 * world-loaded seam, which runs long before the definition. */
void app_plugin_objects_rebuild(struct App* app);

static size_t
app_plugin_memory_bytes(void* user)
{
    uint64_t bytes = 0;
    (void)user;
    if( !PlatformMemory_FootprintBytes(&bytes) )
        return 0;
    return bytes > SIZE_MAX ? SIZE_MAX : (size_t)bytes;
}

/* The Actor fields both snapshots share (orientation, action seq,
 * face-entity lock): RuneLite's getOrientation/getAnimation/getInteracting. */
static void
app_plugin_fill_actor(
    struct WorldEntityFacet_Orientation const* orientation,
    struct WorldEntityFacet_Animation const* animation,
    struct WorldEntityFacet_Facing const* facing,
    int* out_orientation,
    int* out_anim_id,
    int* out_interacting)
{
    assert(orientation);
    assert(animation);
    assert(facing);
    assert(out_orientation);
    assert(out_anim_id);
    assert(out_interacting);
    *out_orientation = orientation->yaw & 2047;
    /* The engine's own "is a primary playing" rule (app_world_apply.c): an
     * entity that never had one carries 0, a finished one (uint16_t)-1. */
    *out_anim_id = (animation->primary.anim_id == (uint16_t)-1 || animation->primary.anim_id == 0)
                       ? -1
                       : (int)animation->primary.anim_id;
    *out_interacting = facing->entity_id < 0 ? -1 : facing->entity_id;
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
    if( player == app_local_player(app) && app_minimap_flag_abs(app, &out->flag_x, &out->flag_z) )
    {
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
    out->gender = player->gender;
    app_plugin_fill_actor(
        &player->orientation, &player->animation, &player->facing,
        &out->orientation, &out->anim_id, &out->interacting);
    /*
     * A NAME, or nothing -- never the decoder's failure report.
     *
     * An appearance block carries the name as a packed base37 word, and
     * base37tostr answers a word that is not a name with the literal string
     * "invalid_name" (reference `fromBase37`). The entity keeps that, because
     * the nameplate and the right-click row are TEXT and showing the
     * reference's own token is the reference's own behaviour.
     *
     * This snapshot is not text: it is the plugin API's answer to "who is
     * this", and plugins use it as an identity. The screenshot plugin files
     * captures under it, and on the CS1 lane every capture in the programme
     * landed in a folder called `invalid-name` -- past a guard that reads
     * `name ~= ""`, which the sentinel is not. Two accounts would have shared
     * that folder. So the sentinel becomes the empty string, which is what
     * "this client does not know the name" already means to every caller, and
     * the knowledge that it IS a sentinel stays in jbase37.h where it is
     * written.
     */
    if( !Base37_IsInvalidName(player->name) )
        snprintf(out->name, sizeof(out->name), "%s", player->name);
}

void
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
    app_plugin_fill_actor(
        &npc->orientation, &npc->animation, &npc->facing,
        &out->orientation, &out->anim_id, &out->interacting);

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

void
app_plugin_fill_npc(
    struct App* app,
    struct WorldEntity_NPC const* npc,
    struct ToriRS_NpcSnapshot* out)
{
    assert(app);
    assert(app->world);
    app_plugin_fill_npc_for_world(app, app->world, npc, out);
}

void
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
app_plugin_fill_loc_in(
    struct World const* world,
    struct WorldEntity_Scenery const* scenery,
    struct ToriRS_ScenerySnapshot* out);

static void
app_plugin_fill_loc(
    struct App* app,
    struct WorldEntity_Scenery const* scenery,
    struct ToriRS_ScenerySnapshot* out)
{
    assert(app);
    assert(app->world);
    app_plugin_fill_loc_in(app->world, scenery, out);
}

/* A loc of `world` (the root, or a world entity's own world: its base is the
 * staging base, so the tiles come out ABSOLUTE staging tiles). */
static void
app_plugin_fill_loc_in(
    struct World const* world,
    struct WorldEntity_Scenery const* scenery,
    struct ToriRS_ScenerySnapshot* out)
{
    assert(world);
    assert(scenery);
    assert(out);

    memset(out, 0, sizeof(*out));
    out->loc_id = scenery->loc_id;
    out->tile_x = world->_base_tile_x + scenery->grid_position.x;
    out->tile_z = world->_base_tile_z + scenery->grid_position.z;
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
/*
 * The recent LOC_ANIMs (App_DriveLocAnimNote's ring, fed from every applied
 * packet in play, not only under the driver), oldest first. `iter` is the
 * ring index; the ring is rewritten as rows retire, so a plugin reads it
 * whole within one callback.
 */
static int
app_plugin_loc_anim_next(void* user, int iter, struct ToriRS_LocAnimSnapshot* out)
{
    struct App* app = (struct App*)user;
    struct App_DriveRing const* ring;
    struct App_DriveLocAnim const* a;
    int i = iter < 0 ? 0 : iter;

    assert(app);
    assert(out);
    if( !app->world )
        return -1;
    ring = &app->drive_events;
    if( i >= ring->loc_anim_count )
        return -1;
    a = &ring->loc_anims[i];
    out->tile_x = a->x;
    out->tile_z = a->z;
    out->level = a->level;
    out->shape = a->shape;
    out->seq_id = a->seq;
    out->cycle = a->cycle;
    return i + 1;
}

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
    return PluginHighlights_Push(&app->resolved_highlights, proto);
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

    return PluginHighlights_LocNeedsFull(
        &app->resolved_highlights,
        app->host.highlight.revision,
        app->world->scenery_changed_overflow);
}

/* Push one resolved loc into the cache. Bounded by the same cap as the live
 * list, so a cache that fills cannot outrun what the list could hold. */
static struct ToriRS_HighlightItem*
app_plugin_highlight_loc_cache_push(
    struct App* app,
    struct ToriRS_HighlightItem const* proto)
{
    return PluginHighlights_LocPush(&app->resolved_highlights, proto);
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
    PluginHighlights_LocForget(&app->resolved_highlights, element_id);
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

    incremental_count = PluginHighlights_LocCount(&app->resolved_highlights);
    for( i = 0; i < incremental_count; i++ )
        incremental_sum += PluginHighlights_LocItem(&app->resolved_highlights, i)->element_id;

    app_plugin_highlight_loc_cache_build(app);
    for( i = 0; i < PluginHighlights_LocCount(&app->resolved_highlights); i++ )
        rebuilt_sum += PluginHighlights_LocItem(&app->resolved_highlights, i)->element_id;

    if( incremental_count != PluginHighlights_LocCount(&app->resolved_highlights) ||
        incremental_sum != rebuilt_sum )
        fprintf(
            stderr,
            "highlight-loc-cache: MISMATCH incremental=%d (sum %d) rebuilt=%d (sum %d)\n",
            incremental_count,
            incremental_sum,
            PluginHighlights_LocCount(&app->resolved_highlights),
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

    PluginHighlights_LocClear(&app->resolved_highlights);

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

    PluginHighlights_Clear(&app->resolved_highlights);
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
            PluginHighlights_LocBuilt(&app->resolved_highlights, hl->revision);
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
        for( int i = 0; i < PluginHighlights_LocCount(&app->resolved_highlights); i++ )
        {
            struct ToriRS_HighlightItem* out = app_plugin_highlight_push(
                app, PluginHighlights_LocItem(&app->resolved_highlights, i));
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
        int const gated = PluginHighlights_Count(&app->resolved_highlights);
        bool const all[APP_PLUGIN_HL_POOL_COUNT] = { true, true, true, true };

        app_plugin_highlights_rebuild_pools(app, all);
        if( gated != PluginHighlights_Count(&app->resolved_highlights) )
            fprintf(
                stderr,
                "highlight-gate: MISMATCH gated=%d ungated=%d "
                "(npc %d, player %d, loc %d, obj %d)\n",
                gated,
                PluginHighlights_Count(&app->resolved_highlights),
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

    for( int i = 0; i < PluginHighlights_Count(&app->resolved_highlights); i++ )
    {
        int const kind = (int)PluginHighlights_Item(&app->resolved_highlights, i)->kind;
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
        PluginHighlights_Count(&app->resolved_highlights),
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
    if( next >= PluginHighlights_Count(&app->resolved_highlights) )
        return -1;
    *out = *PluginHighlights_Item(&app->resolved_highlights, next);
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
    return (int)RS_ClientPhase_Screen(&app->phase);
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
    return FrameClockSerial_LastFrameMs(&app->frame_clock.serial);
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
#include "torirs_env.h"
struct ToriRS_WidgetRef app_widget_ref(struct UITree const*,int32_t);
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
    return CS2VM2_StrCStr(thread->strs_stack[thread->strs_stack_top-1-(int)index]);
}
static bool app_script_set_string(void* user,size_t index,char const* value)
{
    struct CS2VM2_Thread* thread=user;
    struct CS2VM2_Str* copy=CS2VM2_StrDup(thread,value);
    if( !copy ) return false;
    thread->strs_stack[thread->strs_stack_top-1-(int)index]=copy;
    return true;
}
/*
 * The script a named plugin callback can be raised FROM on this lane, or -1.
 *
 * `groundItemCaption` is raised by the cache's ground-item caption script --
 * but only by a copy of that script which
 * tools/plugin_engine_script_hooks.py has patched: the stock OldSchool script
 * carries no RUNELITE_CALLBACK opcode at all, so on a stock cache the callback
 * cannot be raised however much CS2 the lane runs. That is a fact about the
 * CACHE and not about the ui logic, so the PROFILE declares it --
 * `[script:ground_items_caption]` -- exactly as `[script:highlight_hover_tile]`
 * declares the highlight lane's, and "undeclared means absent" is the same
 * contract the rest of revconfig runs on. The bytes are still pinned at the
 * hook site below by the fingerprint the header was generated from: a profile
 * may say a cache is hooked, it may not say WHICH hook.
 *
 * This is the question `cs2_scripts` was being asked and cannot answer. It
 * answers "this client runs CS2 ui logic", which all four CS2 lanes are, and
 * the native-caption half then waited for a callback no cache in this tree can
 * raise -- with the latch suppressing and nothing saying so.
 */
static int
app_plugin_script_callback_script(struct App* app, char const* name)
{
    assert(app);
    assert(name);
    if( strcmp(name, "groundItemCaption") != 0 )
        return -1;
    if( App_UiLogic(app) != APP_UI_LOGIC_CS2 )
        return -1;
    /* The captions hang off the overlay this client drives per dirty tile; a
     * profile that declares no overlay has no rows for a caption to be. */
    if( RevConfigRefs_Get(&app->revconfig_refs, "script", "ground_items_overlay") <= 0 )
        return -1;
    return RevConfigRefs_Get(&app->revconfig_refs, "script", "ground_items_caption");
}

/* A "::" command, before the client sends it: true when a plugin consumed
 * it (on_command). `body` is the text after the "::". */
bool
app_plugin_command(struct App* app, char const* body)
{
    assert(app);
    assert(body);
    if( !app->plugins )
        return false;
    return PluginHost_Command(app->plugins, body) != 0;
}

/* RS_CS2Host::item_search: the GE search, offered to the plugins. */
int app_item_search(void* user, char const* query, bool tradeable_only, int* out, int cap)
{
    struct App* app = user;
    assert(app);
    assert(query);
    assert(out);
    if( !app->plugins )
        return -1;
    return PluginHost_ItemSearch(app->plugins, query, tradeable_only, out, cap);
}

void app_script_callback(void* user,struct CS2VM2_Thread* thread,char const* name)
{
    struct App* app=user;
    if( !app->plugins || App_UiLogic(app)!=APP_UI_LOGIC_CS2 || thread->frame_sp<=0 ) return;
    struct CS2VM2_Script const* script=CS2VM_FRAME(thread)->script;
    if( strcmp(name,"groundItemCaption")!=0 ) return;
    if( script->script_id!=app_plugin_script_callback_script(app,name) ||
        script->script_id!=TORIRS_GROUND_CAPTION_SCRIPT ||
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

/*
 * The script hooks (CS2VM2_SetScriptHook): ScriptPreFired / ScriptPostFired
 * for the ids the plugins watch. Two views of the event's values, both "index
 * 0 is the last": the top of the VM's stacks (an invoked script's arguments
 * before its frame takes them, every POST's return values), or the entry
 * frame's own locals (a top-level script's arguments, which its starter
 * already moved in).
 */
struct AppScriptHookView
{
    struct CS2VM2_Thread* thread;
    int int_count;
    int string_count;
};
static int32_t app_script_hook_stack_get_int(void* user, size_t index)
{
    struct AppScriptHookView* view = user;
    return view->thread->ints_stack[view->thread->ints_stack_top - 1 - (int)index];
}
static void app_script_hook_stack_set_int(void* user, size_t index, int32_t value)
{
    struct AppScriptHookView* view = user;
    view->thread->ints_stack[view->thread->ints_stack_top - 1 - (int)index] = value;
}
static char const* app_script_hook_stack_get_string(void* user, size_t index)
{
    struct AppScriptHookView* view = user;
    return CS2VM2_StrCStr(view->thread->strs_stack[view->thread->strs_stack_top - 1 - (int)index]);
}
static bool app_script_hook_stack_set_string(void* user, size_t index, char const* value)
{
    struct AppScriptHookView* view = user;
    struct CS2VM2_Str* copy = CS2VM2_StrDup(view->thread, value);
    assert(copy);
    view->thread->strs_stack[view->thread->strs_stack_top - 1 - (int)index] = copy;
    return true;
}
static int32_t app_script_hook_local_get_int(void* user, size_t index)
{
    struct AppScriptHookView* view = user;
    return CS2VM2_GetIntCurrentFrameLocal(view->thread, view->int_count - 1 - (int)index);
}
static void app_script_hook_local_set_int(void* user, size_t index, int32_t value)
{
    struct AppScriptHookView* view = user;
    (void)CS2VM2_SetIntCurrentFrameLocal(view->thread, view->int_count - 1 - (int)index, value);
}
static char const* app_script_hook_local_get_string(void* user, size_t index)
{
    struct AppScriptHookView* view = user;
    return CS2VM2_StrText(
        CS2VM2_GetStringCurrentFrameLocal(view->thread, view->string_count - 1 - (int)index));
}
static bool app_script_hook_local_set_string(void* user, size_t index, char const* value)
{
    struct AppScriptHookView* view = user;
    return CS2VM2_SetStringCurrentFrameLocal(view->thread, view->string_count - 1 - (int)index, value) ==
           CS2VM_EXECNO_OK;
}

static void
app_script_hook(
    void* user,
    struct CS2VM2_Thread* thread,
    int script_id,
    enum CS2VM2_ScriptHookPhase phase,
    int invoked,
    int int_count,
    int string_count)
{
    struct App* app = user;
    struct AppScriptHookView view;
    struct PluginScriptStack stack;
    bool locals;
    bool saved_running;

    assert(app);
    assert(thread);
    if( !app->plugins || App_UiLogic(app) != APP_UI_LOGIC_CS2 )
        return;
    /* Never more than the stacks hold: a script that returned past what its
     * frame started from is still bounded by the stack it left. */
    locals = phase == CS2VM2_SCRIPT_HOOK_PRE && !invoked;
    if( !locals )
    {
        if( int_count > thread->ints_stack_top )
            int_count = thread->ints_stack_top;
        if( string_count > thread->strs_stack_top )
            string_count = thread->strs_stack_top;
    }
    view = (struct AppScriptHookView){thread, int_count, string_count};
    memset(&stack, 0, sizeof(stack));
    stack.user = &view;
    stack.int_count = (size_t)int_count;
    stack.string_count = (size_t)string_count;
    stack.writable_ints = int_count >= 64 ? UINT64_MAX : ((UINT64_C(1) << int_count) - 1);
    stack.writable_strings = string_count >= 64 ? UINT64_MAX : ((UINT64_C(1) << string_count) - 1);
    stack.get_int = locals ? app_script_hook_local_get_int : app_script_hook_stack_get_int;
    stack.set_int = locals ? app_script_hook_local_set_int : app_script_hook_stack_set_int;
    stack.get_string = locals ? app_script_hook_local_get_string : app_script_hook_stack_get_string;
    stack.set_string = locals ? app_script_hook_local_set_string : app_script_hook_stack_set_string;
    /* A top-level listener's source: the component the VM made active. */
    if( !invoked && app->tree && thread->active_component_id >= 0 )
        stack.widget = app_widget_ref(app->tree, UITree_FindByComponentId(app->tree, thread->active_component_id));
    /* The VM is mid-script: a plugin's scripts.run from the hook is refused
     * (app_plugin_script_run tests this), as a RUNELITE_CALLBACK's is. */
    saved_running = app->host.script_callback_running;
    app->host.script_callback_running = true;
    PluginHost_ScriptHook(app->plugins, phase == CS2VM2_SCRIPT_HOOK_POST, script_id, invoked != 0, &stack);
    app->host.script_callback_running = saved_running;
}

static bool
app_plugin_script_watch(void* user, int script_id, bool on)
{
    (void)user;
    return CS2VM2_ScriptWatch(script_id, on);
}

/*
 * The twelve bonuses out of a record's own param table. Ids 0..11 are the
 * equipment bonuses and 14 is the attack rate in ticks -- an OldSchool
 * convention (OpenRune's ParamMapper documents it, and the server reads the
 * same table through read_combat_params) -- so a client running an OldSchool
 * cache answers a weapon's stats with no hand-written bonus table anywhere.
 * A string param's value is a `char*`, so reading one as an int would be a
 * wild dereference and not a wrong number: those are skipped by kind.
 *
 * Non-zero when this record carried ANY of them, which is both the item's
 * `has_bonuses` and the engine fact behind the `item_bonuses` capability.
 */
static int
app_plugin_objtype_fill_bonuses(
    struct ToriRS_Objtype const* type,
    struct ToriRS_ItemInfo* out)
{
    int found = 0;

    assert(type);
    assert(out);
    for( int i = 0; i < type->param_count; i++ )
    {
        struct ToriRS_Param const* param = &type->params[i];
        if( param->string_value )
            continue;
        if( param->key >= 0 && param->key < TORIRS_EQUIPMENT_BONUS_COUNT )
        {
            out->bonus[param->key] = param->int_value;
            found = 1;
        }
        else if( param->key == 14 )
        {
            out->attack_rate = param->int_value;
            found = 1;
        }
        /* Ranged strength, from whichever of the two ids this record uses. */
        else if( param->key == 12 || param->key == 189 )
        {
            out->ranged_strength += param->int_value;
            found = 1;
        }
    }
    return found;
}

/* CacheProvider_ObjtypeFindResident's predicate: the same fill, over a
 * scratch record nobody reads, asked only for its answer. */
static bool
app_plugin_objtype_has_bonuses(struct ToriRS_Objtype const* type, void* user)
{
    struct ToriRS_ItemInfo scratch;

    assert(type);
    (void)user;
    memset(&scratch, 0, sizeof(scratch));
    scratch.attack_rate = -1;
    return app_plugin_objtype_fill_bonuses(type, &scratch) != 0;
}

/* The wire opcode this revision gives a canonical packet name, or -1 when the
 * table has no row for it -- which is how every other caller asks whether a
 * lane carries a packet at all. @see GameProtoRevTable::packetin_wire. */
static int
app_plugin_packet_wire(struct App const* app, int pkt_name)
{
    assert(app);
    if( !app->net || !app->net->rev || !app->net->rev->packetin_wire )
        return -1;
    return app->net->rev->packetin_wire(pkt_name);
}

/*
 * One host/platform/engine fact per name, and every one of them is an
 * EXPRESSION OVER AN ENGINE FACT: a packet this revision's wire table
 * carries, a ref the profile declares, a role the live tree resolves, a param
 * the loaded cache carries. Never a lane, revision or lineage name -- a
 * plugin that asked "is this osrs239" would be carrying a copy of a decision
 * this client already made, and it would be wrong on the next profile that
 * declares the same thing.
 *
 *   widgets.geometry     always: the widget API reports geometry everywhere
 *   scripts.callbacks    CS2 ui logic               (kept: the old spelling)
 *   cs2_scripts          CS2 ui logic               (the same fact, named)
 *   script_callback:<n>  this client has a hook site that can raise the plugin
 *                        callback <n>: CS2 ui logic AND the profile declares
 *                        both the script the callback hangs off and the hooked
 *                        script itself. NOT `cs2_scripts`: the hook lives in
 *                        patched CACHE BYTES, so a CS2 lane on a stock cache
 *                        raises nothing at all.
 *   highlight_groups     CS2 ui logic && [script:highlight_hover_tile]
 *   varbit:<name>        [varbit:<name>] declared by this profile
 *   varp:<name>          [varp:<name>] declared by this profile
 *   server_tick          always: the tick is raised on every lane now
 *   server_tick.fenced   the wire carries SERVER_TICK_END, so the tick is a
 *                        real end-of-tick fence and not PLAYER_INFO's edge
 * `loot_events` USED TO BE HERE, answered `App_UiLogic == CS2`. It was a lane
 * test in a table whose rule forbids one, and the loot tracker read it as "is
 * the client's loot record mine to read". The record is filled by
 * App_LootNotifyKill, which every lane reaches, so on CS1 the answer was no
 * over a store holding two kills. A plugin asks the RECORD now
 * (game->loot_source_next); there is no capability to ask.
 *
 *   native_layout        this lane HAS native top-level chromes to choose
 *                        between, and a way to ask for one: the profile names
 *                        the roots and the settings panel that selects them.
 *                        A PROFILE fact, so it is answerable at on_start --
 *                        whether the SERVER armed the row is a session fact,
 *                        and frame.native_layout_select's own answer.
 *   item_bonuses         a resident objtype carries equipment-bonus params
 *   native_orbs          the live tree resolves the `orb_run` role
 *   if_settab            the wire carries IF_SETTAB
 *   tab_select           app_plugin_tab_select can act: the cache lane needs
 *                        [script:sidebar_switch], the dat1 lane owns the slots
 *   touch                the application is on the touch UI/input policy
 *   web                  the Emscripten lane
 *   browser              this build has the embedded BROWSER transport
 *
 * `native_chat_filters` is deliberately NOT here: how many filters a lane's
 * chat bar carries is a COUNT and not a bool, and a plugin already counts
 * them -- widgets.find_all("chat_buttons", NULL, 0, &count) answers the
 * count alone, one past the highest member this frame has.
 */
static int
app_plugin_capability(void* user, char const* name)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(name);
    if( strcmp(name, "widgets.geometry") == 0 ) return 1;
    if( strcmp(name,"scripts.callbacks")==0 ) return App_UiLogic(app)==APP_UI_LOGIC_CS2;
    if( strcmp(name, "cs2_scripts") == 0 )
        return App_UiLogic(app) == APP_UI_LOGIC_CS2;
    if( strncmp(name, "script_callback:", 16) == 0 )
        return app_plugin_script_callback_script(app, name + 16) > 0;
    if( strcmp(name, "highlight_groups") == 0 )
        return App_UiLogic(app) == APP_UI_LOGIC_CS2 &&
               RevConfigRefs_Get(
                   &app->revconfig_refs, "script", "highlight_hover_tile") >= 0;
    if( strncmp(name, "varbit:", 7) == 0 )
        return RevConfigRefs_Get(&app->revconfig_refs, "varbit", name + 7) >= 0;
    if( strncmp(name, "varp:", 5) == 0 )
        return RevConfigRefs_Get(&app->revconfig_refs, "varp", name + 5) >= 0;
    if( strcmp(name, "server_tick") == 0 )
        return 1;
    if( strcmp(name, "server_tick.fenced") == 0 )
        return app_plugin_packet_wire(app, PKT_NAME_SERVER_TICK_END) >= 0;
    if( strcmp(name, "native_layout") == 0 )
        return App_UiLogic(app) == APP_UI_LOGIC_CS2 &&
               RevConfigRefs_Get(&app->revconfig_refs, "iface", "toplevel_fixed") >= 0 &&
               RevConfigRefs_Get(&app->revconfig_refs, "iface", "settings_side") >= 0;
    if( strcmp(name, "item_bonuses") == 0 )
        return app->provider &&
               CacheProvider_ObjtypeFindResident(
                   app->provider, app_plugin_objtype_has_bonuses, NULL) >= 0;
    if( strcmp(name, "native_orbs") == 0 )
        return app->tree &&
               UITree_RoleNodeByName(app->tree, &app->ui_roles, "orb_run") >= 0;
    if( strcmp(name, "if_settab") == 0 )
        return app_plugin_packet_wire(app, PKT_NAME_IF_SETTAB) >= 0;
    if( strcmp(name, "tab_select") == 0 )
    {
        /* app_plugin_tab_select's own condition, said once: the cache lane
         * flips a tab by running the profile's switch script and refuses
         * without it, while the dat1 lane owns the slot table itself. */
        if( App_UiLogic(app) == APP_UI_LOGIC_CS2 )
            return RevConfigRefs_Get(
                       &app->revconfig_refs, "script", "sidebar_switch") > 0;
        return 1;
    }
    /*
     * A BOOT fact, and it must be answerable at on_start.
     *
     * The touch policy used to be written from frame_loop_step, which runs after
     * PluginHost_Start: a plugin asking here at on_start -- the only place a
     * key declaration can be made, and the only place ExpectAbsent used to be
     * legal -- was told false on every lane and true from frame one, with
     * nobody listening by then. Measured before the move:
     *
     *     CAPPROBE at=start    touch=0
     *     CAPPROBE at=frame60  touch=1
     *
     * It is resolved in App_Init now, beside the clientscript identity it
     * follows. @see platform/device_caps.h.
     *
     * THE SHAPE TO WATCH FOR. A capability answered from a field the BOOT
     * fills lies at on_start if the boot fills it late. The two answers above
     * that still change -- `item_bonuses` and `native_orbs` -- are not that
     * shape: they are facts about state that ARRIVES (an objtype resident in
     * the cache, a published tree), so false at on_start is the honest
     * answer and a plugin that needs them asks again at a fence. Anything
     * else that moves between at=start and at=frame60 is this defect again.
     */
    if( strcmp(name, "touch") == 0 )
        return DeviceCaps_TouchUi(&app->device_caps) != 0;
    /*
     * The DEVICE has keys to raise -- not the same question as `touch`, and
     * the pair is easy to confuse, which is why both are spelled out here.
     * `touch` is an input POLICY that a login clienttype or TORIRS_TOUCH_UI
     * can move; this is the platform's own answer and nothing overrules it.
     * A chrome offering a keyboard switch wants THIS one: a desk with
     * TORIRS_TOUCH_UI=1 has no keys to summon. @see platform/device_caps.h.
     */
    if( strcmp(name, "input.screen_keyboard") == 0 )
        return DeviceCaps_ScreenKeyboard(&app->device_caps) != 0;
    /*
     * There is a plugin WINDOW to open -- for a frame carrying a launcher.
     *
     * Constant true in the client, and that is the point: this is what tells a
     * focused harness apart from a client, not one lane from another. A full
     * App always has the window (the BUFFER fallback draws it in-canvas when
     * no executor will start, so "no presentation" is not "no window"); a test
     * that builds a host with no window answers 0 for every key it does not
     * know and the launcher stands down instead of pressing into nothing.
     */
    if( strcmp(name, "client.plugin_window") == 0 )
        return 1;
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
    RS_GroundItemsDirty_MarkAll(&app->ground_items_dirty);
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

/*
 * One zone of the kept rebuild grid. The scene is 13 zones a side centred on
 * the rebuild's zone with the south-west six zones off (world.c's
 * `zone_center - 52/8`), which is the packet's own indexing.
 */
static bool
app_plugin_instance_zone(
    void* user, int level, int zone_x, int zone_z, struct ToriRS_InstanceZone* out)
{
    struct App* app = user;
    int32_t packed;

    assert(app);
    assert(out);
    packed = RS_RebuildRegion_InstanceTemplate(&app->rebuild_region, level, zone_x, zone_z);
    if( packed == 0 )
        return false;
    out->rotation = (packed >> 1) & 3;
    out->template_tile_z = ((packed >> 3) & 0x7ff) * 8;
    out->template_tile_x = ((packed >> 14) & 0x3ff) * 8;
    out->template_level = (packed >> 24) & 3;
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
    if( !ToriRS_FrameModifiersInput(&app->frame_modifiers) )
        return 0;
    if( keycode < 0 || keycode >= TORIRSK_COUNT )
        return 0;
    return LibToriRS_Input_IsKeyHeld(
               ToriRS_FrameModifiersInput(&app->frame_modifiers), (enum LibToriRS_KeyCode)keycode)
               ? 1
               : 0;
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
    if( App_PresentedView(app)->world_hover_view != 0 &&
        WorldviewRegistry_IsLive(&app->worldviews, App_PresentedView(app)->world_hover_view) )
    {
        struct Worldview const* view =
            WorldviewRegistry_Get(&app->worldviews, App_PresentedView(app)->world_hover_view);

        *out_tile_x = view->base_x + App_PresentedView(app)->world_hover_view_x;
        *out_tile_z = view->base_z + App_PresentedView(app)->world_hover_view_z;
        *out_level = view->world ? World_TerrainWalkLevel(
                                       view->world,
                                       App_PresentedView(app)->world_hover_view_x,
                                       App_PresentedView(app)->world_hover_view_z,
                                       App_PresentedView(app)->world_hover_view_level)
                                 : App_PresentedView(app)->world_hover_view_level;
        return 1;
    }

    if( App_PresentedView(app)->world_hover_tile_x < 0 || App_PresentedView(app)->world_hover_tile_z < 0 )
        return 0;

    *out_tile_x = app->world->_base_tile_x + App_PresentedView(app)->world_hover_tile_x;
    *out_tile_z = app->world->_base_tile_z + App_PresentedView(app)->world_hover_tile_z;
    *out_level = World_TerrainWalkLevel(
        app->world,
        App_PresentedView(app)->world_hover_tile_x,
        App_PresentedView(app)->world_hover_tile_z,
        App_PresentedView(app)->world_hover_tile_level);
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
int
app_plugin_hover_entity(void* user, struct ToriRS_HoverTarget* out)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(out);

    if( !app->world )
        return 0;

    for( int i = 0; i < App_PresentedView(app)->world_pickset.count; i++ )
    {
        struct World_Picked const* hit = &App_PresentedView(app)->world_pickset.items[i];
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

/* ---------------------------------------------- the Quest Helper port's seams
 *
 * Client vars, config records, the chatbox, the hint arrow, a sound, three
 * numbers and a synchronous clientscript run -- each the client's OWN state,
 * read or written where the client's own code reads or writes it. Nothing
 * here is a packet. */

static int
app_plugin_varc_int(void* user, int id)
{
    struct App* app = (struct App*)user;
    assert(app);
    return VarCManager_GetInt(&app->varcs, id);
}

static char const*
app_plugin_varc_str(void* user, int id)
{
    struct App* app = (struct App*)user;
    assert(app);
    return VarCManager_GetString(&app->varcs, id);
}

static void
app_plugin_set_varc_int(void* user, int id, int value)
{
    struct App* app = (struct App*)user;
    assert(app);
    VarCManager_SetInt(&app->varcs, id, value);
}

static void
app_plugin_set_varc_str(void* user, int id, char const* value)
{
    struct App* app = (struct App*)user;
    assert(app);
    assert(value);
    VarCManager_SetString(&app->varcs, id, value);
}

/* client-side, as a clientscript's own varbit write: the base varp's local
 * copy and its transmit listeners, never the server. */
static int
app_plugin_set_varbit(void* user, int varbit_id, int value)
{
    struct App* app = (struct App*)user;
    assert(app);
    if( VarPManager_VarbitBaseVar(&app->varps, varbit_id) < 0 )
        return 0;
    RS_CS2Host_ScriptWriteVarbit(&app->host, varbit_id, value);
    return 1;
}

/* client.open_url: through the slot the server's URL_OPEN and main.c's
 * PlatformWindow_OpenUrl already share, so it goes out at this frame's end. */
static int
app_plugin_open_url(void* user, char const* url)
{
    struct App* app = (struct App*)user;
    assert(app);
    assert(url);
    if( !app->plugin_open_url_ok )
        return TORIRS_RESULT_UNSUPPORTED;
    if( app->host.client.pending_open_url[0] )
        return TORIRS_RESULT_CONFLICT;
    assert(strlen(url) < sizeof(app->host.client.pending_open_url));
    snprintf(app->host.client.pending_open_url, sizeof(app->host.client.pending_open_url), "%s", url);
    TORIRS_LOG("plugin: open_url %s\n", url);
    return TORIRS_RESULT_OK;
}

/*
 * The load memo (App::plugin_loads). Kinds below APP_PLUGIN_LOAD_PLAN are
 * the def records; a script run's awaits are keyed by their assets-table key
 * (APP_PLUGIN_LOAD_PLAN + kind * APP_PLUGIN_LOAD_PLAN + variant, and the id),
 * so the two halves of a paired yield are remembered apart.
 */
enum
{
    APP_PLUGIN_LOAD_ENUM = 1,
    APP_PLUGIN_LOAD_NPC,
    APP_PLUGIN_LOAD_LOC,
    APP_PLUGIN_LOAD_SCRIPT,
    APP_PLUGIN_LOAD_SEQ,
    APP_PLUGIN_LOAD_PLAN = 64,
};

#define APP_PLUGIN_LOAD_MEMOS ((int)(sizeof(((struct App*)0)->plugin_loads) / sizeof(((struct App*)0)->plugin_loads[0])))

static struct AppPluginLoadMemo*
app_plugin_load_find(struct App* app, int kind, int id)
{
    assert(app);
    for( int i = 0; i < APP_PLUGIN_LOAD_MEMOS; i++ )
    {
        struct AppPluginLoadMemo* memo = &app->plugin_loads[i];
        if( memo->kind == kind && memo->id == id )
            return memo;
    }
    return NULL;
}

/*
 * Await `key` on the assets table and remember it: true when a load is now
 * counted on a memo. A memo whose load is still out is never reused (the
 * table holds a pointer to its count). False when
 * there is nothing to wait for (the asset is here, or its failure is
 * remembered), and when every slot's load is still out -- the caller is told
 * to ask again either way.
 */
static bool
app_plugin_load_await(struct App* app, int kind, struct AssetKey key)
{
    struct AppPluginLoadMemo* slot = NULL;

    assert(app);
    assert(app->assets);
    for( int i = 0; i < APP_PLUGIN_LOAD_MEMOS; i++ )
    {
        struct AppPluginLoadMemo* memo = &app->plugin_loads[i];
        if( memo->join > 0 )
            continue;
        if( !slot || memo->stamp < slot->stamp )
            slot = memo;
    }
    if( !slot )
        return true;
    slot->join = 0;
    if( !Assets_Await(app->assets, key, &slot->join) )
        return false;
    slot->kind = kind;
    slot->id = key.id;
    slot->id2 = -1;
    slot->stamp = ++app->plugin_load_stamp;
    return true;
}

/* A def record the cache does not hold: PENDING while its load is out (or
 * was just queued), MISSING once a load of it has ended without it. */
static enum ToriRS_AssetState
app_plugin_record_miss(struct App* app, int kind, int id)
{
    struct AppPluginLoadMemo* memo;
    struct AssetKey key;

    assert(app);
    assert(app->provider);
    memo = app_plugin_load_find(app, kind, id);
    if( memo )
        return memo->join > 0 ? TORIRS_ASSET_PENDING : TORIRS_ASSET_MISSING;
    if( kind == APP_PLUGIN_LOAD_ENUM )
        key = AssetKey_Make(ASSET_KIND_CONFIG, id, ASSET_CONFIG_ENUM);
    else if( kind == APP_PLUGIN_LOAD_NPC )
        key = AssetKey_Make(ASSET_KIND_CONFIG, id, ASSET_CONFIG_NPC);
    else if( kind == APP_PLUGIN_LOAD_LOC )
        key = AssetKey_Make(ASSET_KIND_CONFIG, id, ASSET_CONFIG_LOC);
    else if( kind == APP_PLUGIN_LOAD_SEQ )
        key = AssetKey_Make(ASSET_KIND_SEQUENCE, id, 0);
    else
    {
        assert(kind == APP_PLUGIN_LOAD_SCRIPT);
        key = AssetKey_Make(ASSET_KIND_CONFIG, id, ASSET_CONFIG_SCRIPT);
    }
    /* Nothing to wait for while the record is absent: the table remembers
     * this cache cannot serve it. */
    if( !app_plugin_load_await(app, kind, key) )
        return TORIRS_ASSET_MISSING;
    return TORIRS_ASSET_PENDING;
}

static void
app_plugin_def_text(char* out, size_t capacity, char const* text)
{
    assert(out);
    assert(capacity > 0);
    (void)snprintf(out, capacity, "%s", text ? text : "");
}

static void
app_plugin_def_list(struct ToriRS_DefList* list, int const* values, int count)
{
    assert(list);
    if( !values || count < 0 )
        count = 0;
    list->count = count;
    for( int i = 0; i < count && i < list->capacity; i++ )
        list->values[i] = values[i];
}

/* The multinpc / multiloc child the client's varps select now, the
 * resolution the world itself makes (VarPManager_ResolveTransform). */
static int
app_plugin_def_impostor(
    struct App* app,
    int const* transforms,
    int transform_count,
    int transform_varbit,
    int transform_varp)
{
    assert(app);
    if( transform_count <= 0 || !transforms )
        return -1;
    return VarPManager_ResolveTransform(
        &app->varps, transforms, transform_count, transform_varbit, transform_varp);
}

static int
app_plugin_enum_def(
    void* user,
    int id,
    struct ToriRS_EnumDef* out,
    struct ToriRS_EnumEntry* entries,
    int capacity)
{
    struct App* app = (struct App*)user;
    struct ToriRS_Enum* e;

    assert(app);
    assert(out);
    if( capacity > 0 )
        assert(entries);
    if( !app->provider )
        return TORIRS_ASSET_MISSING;
    e = CacheProvider_EnumGet(app->provider, id);
    if( !e )
        return app_plugin_record_miss(app, APP_PLUGIN_LOAD_ENUM, id);

    memset(out, 0, sizeof(*out));
    out->id = id;
    out->key_type = (unsigned char)e->input_type;
    /* The decoded record keeps only whether its values are strings, not the
     * output type character, so an obj- or coord-valued enum reads 'i'. */
    out->value_type = e->output_is_string ? 's' : 'i';
    out->value_is_string = e->output_is_string;
    out->default_int = e->default_int;
    app_plugin_def_text(out->default_string, sizeof(out->default_string), e->default_string);
    out->count = e->count;
    for( int i = 0; i < e->count && i < capacity; i++ )
    {
        entries[i].key = e->keys ? e->keys[i] : 0;
        entries[i].int_value = !e->output_is_string && e->int_values ? e->int_values[i] : 0;
        app_plugin_def_text(
            entries[i].string_value,
            sizeof(entries[i].string_value),
            e->output_is_string && e->string_values ? e->string_values[i] : "");
    }
    return TORIRS_ASSET_READY;
}

static int
app_plugin_npc_def(void* user, int id, struct ToriRS_NpcDef* out)
{
    struct App* app = (struct App*)user;
    struct ToriRS_Npctype* npc;

    assert(app);
    assert(out);
    if( !app->provider )
        return TORIRS_ASSET_MISSING;
    npc = CacheProvider_NpctypeGet(app->provider, id);
    if( !npc )
        return app_plugin_record_miss(app, APP_PLUGIN_LOAD_NPC, id);

    out->id = id;
    app_plugin_def_text(out->name, sizeof(out->name), npc->name);
    for( int i = 0; i < TORIRS_DEF_ACTIONS; i++ )
        app_plugin_def_text(out->actions[i], sizeof(out->actions[i]), npc->actions[i]);
    out->size = npc->size;
    out->combat_level = npc->combat_level;
    out->transform_varbit = npc->transform_varbit;
    out->transform_varp = npc->transform_varp;
    out->impostor = app_plugin_def_impostor(
        app, npc->transforms, npc->transform_count, npc->transform_varbit, npc->transform_varp);
    app_plugin_def_list(&out->transforms, npc->transforms, npc->transform_count);
    app_plugin_def_list(&out->models, npc->models, npc->models_count);
    app_plugin_def_list(&out->recolor_from, npc->recolors_from, npc->recolor_count);
    app_plugin_def_list(&out->recolor_to, npc->recolors_to, npc->recolor_count);
    return TORIRS_ASSET_READY;
}

static int
app_plugin_loc_def(void* user, int id, struct ToriRS_LocDef* out)
{
    struct App* app = (struct App*)user;
    struct ToriRS_Location* loc;

    assert(app);
    assert(out);
    if( !app->provider )
        return TORIRS_ASSET_MISSING;
    loc = CacheProvider_LocationGet(app->provider, id);
    if( !loc )
        return app_plugin_record_miss(app, APP_PLUGIN_LOAD_LOC, id);

    out->id = id;
    app_plugin_def_text(out->name, sizeof(out->name), loc->name);
    for( int i = 0; i < TORIRS_DEF_ACTIONS; i++ )
        app_plugin_def_text(out->actions[i], sizeof(out->actions[i]), loc->actions[i]);
    out->size_x = loc->size_x;
    out->size_z = loc->size_z;
    out->transform_varbit = loc->transform_varbit;
    out->transform_varp = loc->transform_varp;
    out->impostor = app_plugin_def_impostor(
        app, loc->transforms, loc->transform_count, loc->transform_varbit, loc->transform_varp);
    out->map_scene = loc->map_scene_id;
    out->map_icon = loc->map_function_id;
    app_plugin_def_list(&out->transforms, loc->transforms, loc->transform_count);
    return TORIRS_ASSET_READY;
}

_Static_assert(TORIRS_DEF_ACTIONS == TORIRS_MENU_ACTION_SLOTS, "def action slots");
_Static_assert(TORIRS_PLUGIN_CHAT_TYPE_MAX == RS_CHAT_TYPE_MAX, "plugin chat types");

/* The line goes where a MESSAGE_GAME line goes, through the host so the
 * chat-transmit channel repaints the chatbox. Deliberately NOT through
 * App_NotifyChatMessage: that is the server speaking, and its game-event
 * recogniser must never hear a plugin's text. */
static void
app_plugin_chat_add(void* user, int type, char const* sender, char const* text)
{
    struct App* app = (struct App*)user;
    assert(app);
    assert(sender);
    assert(text);
    RS_CS2Host_ChatAdd(&app->host, type, sender[0] ? sender : NULL, sender[0] ? sender : NULL, text);
}

/*
 * The HINT_ARROW packet's own state, set the way the packet sets it
 * (RS_HintArrow_Set with the wire's type byte), so every pass that draws an
 * arrow draws a plugin's. A tile arrow keeps the arrow's height, as the
 * reference's setHintArrow(WorldPoint) does.
 */
static int
app_plugin_hint_arrow_set(void* user, int kind, int a, int b)
{
    struct App* app = (struct App*)user;
    assert(app);
    if( kind == TORIRS_HINT_ARROW_NONE )
    {
        RS_HintArrow_Set(&app->hint_arrow, RS_HINT_ARROW_WIRE_CLEAR, 0, 0, 0);
        return 1;
    }
    if( kind == TORIRS_HINT_ARROW_TILE )
    {
        RS_HintArrow_Set(&app->hint_arrow, RS_HINT_ARROW_COORD, a, b, app->hint_arrow.height);
        return 1;
    }
    assert(kind == TORIRS_HINT_ARROW_NPC);
    if( !app->world || !World_NpcGetByServerSlot(app->world, a) )
        return 0;
    RS_HintArrow_Set(&app->hint_arrow, RS_HINT_ARROW_NPC, a, 0, 0);
    return 1;
}

static int
app_plugin_hint_arrow_get(void* user, struct ToriRS_HintArrowInfo* out)
{
    struct App* app = (struct App*)user;
    assert(app);
    assert(out);
    if( app->hint_arrow.type == RS_HINT_ARROW_NPC )
    {
        out->kind = TORIRS_HINT_ARROW_NPC;
        out->npc_slot = app->hint_arrow.target;
        return 1;
    }
    if( app->hint_arrow.type == RS_HINT_ARROW_COORD )
    {
        out->kind = TORIRS_HINT_ARROW_TILE;
        out->tile_x = app->hint_arrow.target;
        out->tile_z = app->hint_arrow.tile_z;
        return 1;
    }
    if( app->hint_arrow.type == RS_HINT_ARROW_PLAYER )
    {
        out->kind = TORIRS_HINT_ARROW_PLAYER;
        out->player_pid = app->hint_arrow.target;
        return 1;
    }
    return 0;
}

static void
app_plugin_sound(void* user, int sound_id, int loops, int delay)
{
    struct App* app = (struct App*)user;
    assert(app);
    App_PlaySound(app, sound_id, loops, delay);
}

static int
app_plugin_weight(void* user)
{
    struct App* app = (struct App*)user;
    assert(app);
    return app->stats.run_weight;
}

/* The mask WORLDFLAGS pushes (RS_CS2ClientState::world_flags), which is all
 * this client knows of the world it is on. */
static int
app_plugin_world_types(void* user)
{
    struct App* app = (struct App*)user;
    assert(app);
    return app->host.client.world_flags;
}

static int
app_plugin_tick(void* user)
{
    struct App* app = (struct App*)user;
    assert(app);
    return (int)app->server_tick.serial;
}

/* What a plugin-run script's yield waits for, answered against the memo. */
enum AppPluginScriptAwait
{
    APP_PLUGIN_SCRIPT_RESUME,
    APP_PLUGIN_SCRIPT_PENDING,
    APP_PLUGIN_SCRIPT_BLOCKED,
};

static enum AppPluginScriptAwait
app_plugin_script_await(struct App* app, struct CS2VM_HostRequest const* request)
{
    struct CS2RunAwait await;

    assert(app);
    assert(request);
    if( !CS2Run_RequestAwait(&app->host, request, &await) )
        return APP_PLUGIN_SCRIPT_BLOCKED;
    for( int i = 0; i < await.count; i++ )
    {
        struct AssetKey const key = await.keys[i];
        int const kind = APP_PLUGIN_LOAD_PLAN + key.kind * APP_PLUGIN_LOAD_PLAN + key.variant;
        struct AppPluginLoadMemo* memo = app_plugin_load_find(app, kind, key.id);

        assert(key.variant >= 0);
        assert(key.variant < APP_PLUGIN_LOAD_PLAN);
        if( memo && memo->join > 0 )
            return APP_PLUGIN_SCRIPT_PENDING;
        /* A load of this record has already ENDED and the host still yields
         * on it: there is no such record, and the resume answers the miss. */
        if( memo )
            continue;
        /* Through the assets table, as a script task's own wait: one load in
         * flight per asset whoever wants it. */
        if( app_plugin_load_await(app, kind, key) )
            return APP_PLUGIN_SCRIPT_PENDING;
    }
    return APP_PLUGIN_SCRIPT_RESUME;
}

static int
app_plugin_script_results(struct CS2VM2_Thread* thread, struct ToriRS_ScriptResult* out)
{
    assert(thread);
    assert(out);
    if( thread->ints_stack_top > TORIRS_SCRIPT_RUN_INTS_MAX ||
        thread->strs_stack_top > TORIRS_SCRIPT_RUN_STRINGS_MAX )
        return TORIRS_CONTRACT_BUDGET_EXCEEDED;
    for( int i = 0; i < thread->strs_stack_top; i++ )
    {
        char const* s = CS2VM2_StrText(thread->strs_stack[i]);
        if( strlen(s) >= TORIRS_SCRIPT_RUN_STRING_LEN )
            return TORIRS_CONTRACT_BUDGET_EXCEEDED;
    }
    out->int_count = (size_t)thread->ints_stack_top;
    for( int i = 0; i < thread->ints_stack_top; i++ )
        out->ints[i] = thread->ints_stack[i];
    out->string_count = (size_t)thread->strs_stack_top;
    for( int i = 0; i < thread->strs_stack_top; i++ )
        (void)snprintf(out->strings[i], sizeof(out->strings[i]), "%s",
            CS2VM2_StrText(thread->strs_stack[i]));
    return TORIRS_CONTRACT_OK;
}

/*
 * One clientscript, run to completion inside the caller's frame, on the
 * client's own host (RS_CS2Host_Exec) and a VM of its own from the pool --
 * Task_CS2Run's start and resume loop without the waiting.
 *
 * "Idle" is what the host can see: no script's ThreadRun on the stack
 * (trace_script_id, which Task_CS2Run sets around every call), no named
 * callback in progress, no yield half taken. A script parked on a load is
 * not running and holds its own VM, so it does not count.
 *
 * Every yield is answered here and never awaited: a load is queued and the
 * run abandoned (PENDING), a load that already ran is resumed into its miss,
 * and a request only Task_CS2Run's resume can satisfy is NATIVE_BLOCKED.
 * The named-callback hook is detached for the run, so a RUNELITE_CALLBACK
 * opcode in it does not re-enter the plugin dispatch that called us.
 */
static int
app_plugin_script_run(
    void* user,
    int script_id,
    struct ToriRS_ScriptArg const* args,
    int arg_count,
    struct ToriRS_ScriptResult* out)
{
    struct App* app = (struct App*)user;
    struct RS_CS2Host* host;
    struct CS2VM2_Script* script;
    struct CS2VM2* vm;
    struct CS2VM2_Thread* thread;
    void (*saved_callback)(void* user, struct CS2VM2_Thread*, char const* name);
    int (*saved_item_search)(void* user, char const* query, bool tradeable_only, int* out, int cap);
    int ints = 0;
    int strings = 0;
    int result = TORIRS_CONTRACT_FAILED;

    assert(app);
    assert(out);
    assert(arg_count >= 0);
    if( arg_count > 0 )
        assert(args);
    if( App_UiLogic(app) != APP_UI_LOGIC_CS2 || !app->provider )
        return TORIRS_CONTRACT_UNAVAILABLE;
    host = &app->host;
    if( host->trace_script_id >= 0 || host->script_callback_running || host->has_pending )
        return TORIRS_CONTRACT_WRONG_CONTEXT;

    script = CacheProvider_ClientScriptGet(app->provider, script_id);
    if( !script )
        return app_plugin_record_miss(app, APP_PLUGIN_LOAD_SCRIPT, script_id) == TORIRS_ASSET_PENDING
                   ? TORIRS_CONTRACT_PENDING
                   : TORIRS_CONTRACT_INVALID_ARGUMENT;
    for( int i = 0; i < arg_count; i++ )
    {
        if( args[i].is_string )
            strings++;
        else
            ints++;
    }
    if( ints > script->int_argument_count || strings > script->string_argument_count )
        return TORIRS_CONTRACT_INVALID_ARGUMENT;

    vm = CS2VM2_Acquire();
    assert(vm);
    CS2VM2_BindHost(vm, host, RS_CS2Host_Exec);
    thread = CS2VM2_ThreadMain(vm);
    CS2VM2_ThreadSetCanvas(
        thread, host->viewport_w > 0 ? host->viewport_w : 765, host->viewport_h > 0 ? host->viewport_h : 503);
    CS2VM2_ThreadSetWindowMode(thread, host->window_mode, host->default_window_mode);
    CS2VM2_ThreadStart(thread, script);
    ints = 0;
    strings = 0;
    for( int i = 0; i < arg_count; i++ )
    {
        if( args[i].is_string )
            (void)CS2VM2_SetStringCurrentFrameLocal(
                thread, strings++, args[i].string_value ? args[i].string_value : "");
        else
            (void)CS2VM2_SetIntCurrentFrameLocal(thread, ints++, args[i].int_value);
    }
    CS2VM2_SetActiveAndDotComponentId(thread, -1);

    saved_callback = host->script_callback;
    host->script_callback = NULL;
    /* Nor the GE search hook: a plugin's own run is already inside a plugin
     * callback, and the search it reaches is the client's. */
    saved_item_search = host->item_search;
    host->item_search = NULL;
    for( int resumes = 0;; resumes++ )
    {
        struct CS2VM2_ThreadError err = { 0 };
        enum CS2VM2_ThreadStatus status;
        enum AppPluginScriptAwait await;
        struct CS2VM_HostRequest request;

        /* A request whose yield never marks its await spent would resume into
         * the same yield for ever; Task_CS2Run would spin on it as well, but
         * across frames. Here it is one call, so it is bounded. */
        if( resumes > 256 )
        {
            CS2VM2_ResetRuntime(thread);
            result = TORIRS_CONTRACT_FAILED;
            break;
        }
        host->trace_script_id = script_id;
        status = CS2VM2_ThreadRun(thread, &err);
        host->trace_script_id = -1;
        if( status == CS2VM2_THREAD_DONE )
        {
            result = app_plugin_script_results(thread, out);
            break;
        }
        if( status != CS2VM2_THREAD_YIELDED || !host->has_pending )
        {
            TORIRS_ERR("plugin scripts.run: script %d failed at opcode %d pc %d (in script %d)\n",
                script_id, thread->last_error_opcode, thread->last_error_pc, thread->last_error_script_id);
            host->has_pending = false;
            CS2VM2_ResetRuntime(thread);
            result = TORIRS_CONTRACT_FAILED;
            break;
        }
        request = host->pending;
        host->has_pending = false;
        await = app_plugin_script_await(app, &request);
        if( await == APP_PLUGIN_SCRIPT_RESUME )
            continue;
        CS2VM2_ResetRuntime(thread);
        result = await == APP_PLUGIN_SCRIPT_PENDING ? TORIRS_CONTRACT_PENDING : TORIRS_CONTRACT_NATIVE_BLOCKED;
        break;
    }
    host->script_callback = saved_callback;
    host->item_search = saved_item_search;
    CS2VM2_Release(vm);
    return result;
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
    /**
     * How the engine resolves this field's SENTINEL, or NULL where the field
     * is its own meaning.
     *
     * A settings page names a value to a person, and for one flag the number
     * in the field is not the number anything acts on: `draw_distance` stores
     * 0 for "this era states no preference" and every reader of it goes
     * through ToriRS_Features_PainterDrawDistance, which answers 25. The page
     * cannot know that, and must not guess it -- pointing at the engine's own
     * function is how the row and the painter cannot disagree.
     */
    int (*effective)(int stored);
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
        NULL,
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
        NULL,
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
        NULL,
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
        NULL,
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
        NULL,
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
        NULL,
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
        NULL,
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
        NULL,
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
        NULL,
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
        NULL,
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
        NULL,
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
        NULL,
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
        /* The one flag in this table whose stored 0 is not a distance. */
        ToriRS_Features_PainterDrawDistanceOf,
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
        NULL,
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
        NULL,
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
        NULL,
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
    assert(ToriRS_Features_Resolved(&app->features));

    (void)PluginBootConfig_CaptureFeatures(
        &app->plugin_boot, &app->features, &app->revconfig_profile.camera);
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
            boot ? PluginBootConfig_FeatureTable(&app->plugin_boot) : &app->features;
        return (int*)((char*)table + desc->offset);
    }
    case APP_PLUGIN_FEATURE_SLOT_CAMERA:
    case APP_PLUGIN_FEATURE_SLOT_CAMERA_BIT:
    {
        struct RevConfigCameraItem* camera =
            boot ? PluginBootConfig_FeatureCamera(&app->plugin_boot) : &app->revconfig_profile.camera;
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
    return boot ? PluginBootConfig_FeatureCamera(&app->plugin_boot) : &app->revconfig_profile.camera;
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

    RS_Audio_SetFeatures(&app->audio, &app->features);
    ToriRS_FeatureFacts_LightFromTable(&app->feature_facts, &app->features);
    app->bridge.npc_light_uses_type_ambient_contrast =
        ToriRS_FeatureFacts_NpcTypeAmbientContrast(&app->feature_facts);
    app->bridge.player_head_light_ambient =
        ToriRS_FeatureFacts_PlayerHeadAmbient(&app->feature_facts);
    RedrawRequest_Raise(&app->redraw_request);
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
    /* What the field holds, and what the engine makes of it. The two differ
     * only where the descriptor says how -- @see AppPluginFeatureDesc::effective. */
    out->effective = desc->effective ? desc->effective(out->value) : out->value;
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
            App_PresentedView(app)->world_cam_zoom = app->revconfig_profile.camera.rest;
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
        App_PresentedView(app)->world_cam_zoom = RevConfigProfile_CameraClampZoom(
            &app->revconfig_profile, App_PresentedView(app)->world_cam_zoom);
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
 * and 15, plus the client-owned client-scaling options 30..33 no cache has a
 * row for -- and deliberately: a lane with that panel would otherwise show one
 * value in two places and disagree with itself, and the whole reason these
 * verbs exist is the lane WITHOUT it. A 2004 dat1 cache has no All Settings
 * interface at all, so before this there was no way to reach interface
 * scaling on one -- the frame sat at 1:1 device pixels on a high-density
 * display and nothing in the client could say otherwise.
 *
 * Persistence needs nothing here: RS_Prefs_CaptureFromHost polls the option
 * store every tick and writes preferences.ini when it moves.
 */
#include "render/torirs_renderer_kind.h"
/* The plugin contract restates the renderer list rather than including the
 * render header; these hold the two spellings, and the store's range, to one
 * list.
 *
 * Both sides are cast to int because they are two DIFFERENT enum types, which
 * is the whole point of the assertion and also what -Wenum-compare warns
 * about. The cast silences the warning without weakening anything: these are
 * integer constant expressions either way, and a divergence is still a build
 * failure. */
_Static_assert((int)TORIRS_RENDERER_SOFTWARE == (int)TORIRS_RENDERER_KIND_SOFTWARE, "renderer ids");
_Static_assert((int)TORIRS_RENDERER_OPENGL3 == (int)TORIRS_RENDERER_KIND_OPENGL3, "renderer ids");
_Static_assert((int)TORIRS_RENDERER_OPENGL3_DEPTH == (int)TORIRS_RENDERER_KIND_OPENGL3_DEPTH, "renderer ids");
_Static_assert((int)TORIRS_RENDERER_GLES2 == (int)TORIRS_RENDERER_KIND_GLES2, "renderer ids");
_Static_assert((int)TORIRS_RENDERER_GLES2_DEPTH == (int)TORIRS_RENDERER_KIND_GLES2_DEPTH, "renderer ids");
_Static_assert((int)TORIRS_RENDERER_D3D9 == (int)TORIRS_RENDERER_KIND_D3D9, "renderer ids");
_Static_assert((int)TORIRS_RENDERER_D3D9_DEPTH == (int)TORIRS_RENDERER_KIND_D3D9_DEPTH, "renderer ids");
_Static_assert((int)TORIRS_RENDERER_WEBGL2 == (int)TORIRS_RENDERER_KIND_WEBGL2, "renderer ids");
_Static_assert(
    (int)TORIRS_RENDERER_WEBGL2_DEPTH == (int)TORIRS_RENDERER_KIND_WEBGL2_DEPTH, "renderer ids");
_Static_assert((int)TORIRS_RENDERER_WEBGL1 == (int)TORIRS_RENDERER_KIND_WEBGL1, "renderer ids");
_Static_assert(
    (int)TORIRS_RENDERER_WEBGL1_DEPTH == (int)TORIRS_RENDERER_KIND_WEBGL1_DEPTH, "renderer ids");
_Static_assert((int)TORIRS_RENDERER_GLES3 == (int)TORIRS_RENDERER_KIND_GLES3, "renderer ids");
_Static_assert(
    (int)TORIRS_RENDERER_GLES3_DEPTH == (int)TORIRS_RENDERER_KIND_GLES3_DEPTH, "renderer ids");
_Static_assert(
    (int)TORIRS_RENDERER_SOFTWARE_DEPTH == (int)TORIRS_RENDERER_KIND_SOFTWARE_DEPTH, "renderer ids");
_Static_assert(
    (int)TORIRS_RENDERER_SOFTWARE_RSZBUF == (int)TORIRS_RENDERER_KIND_SOFTWARE_RSZBUF, "renderer ids");
_Static_assert((int)TORIRS_RENDERER_COUNT == (int)TORIRS_RENDERER_KIND_COUNT, "renderer ids");
_Static_assert((int)RS_CS2_RENDERER_MAX == (int)TORIRS_RENDERER_KIND_COUNT, "option 36 stores kind + 1");

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
    case TORIRS_DISPLAY_STRETCH_MODE:
        value = RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_CLIENT_FIT);
        min = 0;
        max = RS_CS2_CLIENT_FIT_MAX;
        break;
    case TORIRS_DISPLAY_MAX_PIXEL_HEIGHT:
        value = RS_CS2Host_GetOption(
            &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_MAX_PIXEL_HEIGHT);
        min = 0;
        max = RS_CS2_MAX_PIXEL_HEIGHT_MAX;
        break;
    case TORIRS_DISPLAY_FRAME_FILTER:
        value = RS_CS2Host_GetOption(
            &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_OUTPUT_FILTER);
        min = 0;
        max = RS_CS2_OUTPUT_FILTER_MAX;
        break;
    case TORIRS_DISPLAY_MAX_PIXEL_WIDTH:
        value = RS_CS2Host_GetOption(
            &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_MAX_PIXEL_WIDTH);
        min = 0;
        max = RS_CS2_MAX_PIXEL_WIDTH_MAX;
        break;
    case TORIRS_DISPLAY_HIGH_DPI:
        value = RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_HIGH_DPI);
        min = RS_CS2_HIGH_DPI_AUTO;
        max = RS_CS2_HIGH_DPI_MAX;
        break;
    case TORIRS_DISPLAY_DENSITY:
        value = App_DisplayDensityPercent(app);
        min = 0;
        max = INT32_MAX;
        break;
    case TORIRS_DISPLAY_HIGH_DPI_IN_FORCE:
        value = (int)App_HighDpiMode(app) + RS_CS2_HIGH_DPI_DEVICE_PIXELS;
        min = RS_CS2_HIGH_DPI_DEVICE_PIXELS;
        max = RS_CS2_HIGH_DPI_MAX;
        break;
    case TORIRS_DISPLAY_WINDOW_FIXED:
        value = App_WindowMode(app) == CS2VM_WINDOW_MODE_FIXED ? 1 : 0;
        min = 0;
        max = 1;
        break;
    case TORIRS_DISPLAY_RENDERER:
    case TORIRS_DISPLAY_RENDERER_ACTIVE:
    case TORIRS_DISPLAY_RENDERERS_AVAILABLE:
    case TORIRS_DISPLAY_RENDERER_REFUSED:
        /* The shell has not started a renderer yet: nothing to say, and a
         * zero would name Software. */
        if( app->host.renderer_active < 0 )
            return 0;
        min = 0;
        switch( setting )
        {
        case TORIRS_DISPLAY_RENDERER:
            value = RS_CS2Host_RendererRequest(&app->host);
            max = RS_CS2_RENDERER_MAX;
            break;
        case TORIRS_DISPLAY_RENDERER_ACTIVE:
            value = app->host.renderer_active;
            max = TORIRS_RENDERER_COUNT - 1;
            break;
        case TORIRS_DISPLAY_RENDERERS_AVAILABLE:
            value = (int)app->host.renderer_available;
            max = (1 << TORIRS_RENDERER_COUNT) - 1;
            break;
        default:
            value = app->host.renderer_refused;
            max = RS_CS2_RENDERER_MAX;
            break;
        }
        break;
    case TORIRS_DISPLAY_EFFECTIVE_UI_SCALE:
    case TORIRS_DISPLAY_LAYOUT_WIDTH:
    case TORIRS_DISPLAY_LAYOUT_HEIGHT:
    case TORIRS_DISPLAY_RENDER_WIDTH:
    case TORIRS_DISPLAY_RENDER_HEIGHT:
    case TORIRS_DISPLAY_OUTPUT_WIDTH:
    case TORIRS_DISPLAY_OUTPUT_HEIGHT:
    case TORIRS_DISPLAY_SCALE_ADJUSTED:
    {
        struct UICanvasScale const* scale = App_ClientScale(app);
        /* Nothing has been presented yet: there is no answer to give, and a
         * zero would read as a real size. */
        if( !scale->present_known )
            return 0;
        min = 0;
        max = INT32_MAX;
        switch( setting )
        {
        case TORIRS_DISPLAY_EFFECTIVE_UI_SCALE:
            value = scale->shown_percent;
            break;
        case TORIRS_DISPLAY_LAYOUT_WIDTH:
            value = UITREE_LAYOUT_ROOT_W;
            break;
        case TORIRS_DISPLAY_LAYOUT_HEIGHT:
            value = UITREE_LAYOUT_ROOT_H;
            break;
        case TORIRS_DISPLAY_RENDER_WIDTH:
            value = scale->present.render_w;
            break;
        case TORIRS_DISPLAY_RENDER_HEIGHT:
            value = scale->present.render_h;
            break;
        case TORIRS_DISPLAY_OUTPUT_WIDTH:
            value = scale->present.output.w;
            break;
        case TORIRS_DISPLAY_OUTPUT_HEIGHT:
            value = scale->present.output.h;
            break;
        default:
            value = (scale->layout.rounded_by_integer ? TORIRS_DISPLAY_ADJUSTED_INTEGER_ROUNDED : 0) |
                    (scale->layout.raised_by_limit ? TORIRS_DISPLAY_ADJUSTED_LIMIT_RAISED : 0) |
                    (scale->present.integer_fell_back ? TORIRS_DISPLAY_ADJUSTED_INTEGER_FELL_BACK : 0) |
                    (scale->layout.lowered_to_fit ? TORIRS_DISPLAY_ADJUSTED_LOWERED_TO_FIT : 0);
            max = TORIRS_DISPLAY_ADJUSTED_INTEGER_ROUNDED | TORIRS_DISPLAY_ADJUSTED_LIMIT_RAISED |
                  TORIRS_DISPLAY_ADJUSTED_INTEGER_FELL_BACK | TORIRS_DISPLAY_ADJUSTED_LOWERED_TO_FIT;
            break;
        }
        break;
    }
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
    case TORIRS_DISPLAY_STRETCH_MODE:
        option = RS_CS2_DEVICEOPTION_CLIENT_FIT;
        break;
    case TORIRS_DISPLAY_MAX_PIXEL_HEIGHT:
        option = RS_CS2_DEVICEOPTION_MAX_PIXEL_HEIGHT;
        break;
    case TORIRS_DISPLAY_FRAME_FILTER:
        option = RS_CS2_DEVICEOPTION_OUTPUT_FILTER;
        break;
    case TORIRS_DISPLAY_MAX_PIXEL_WIDTH:
        option = RS_CS2_DEVICEOPTION_MAX_PIXEL_WIDTH;
        break;
    case TORIRS_DISPLAY_HIGH_DPI:
        option = RS_CS2_DEVICEOPTION_HIGH_DPI;
        break;
    case TORIRS_DISPLAY_RENDERER:
        /* Only a renderer this lane offers: a pick of another would sit in
         * the store, be persisted, and be refused by the shell at every
         * launch. */
        if( app->host.renderer_active < 0 || value < RS_CS2_RENDERER_LAUNCH_DEFAULT ||
            value > RS_CS2_RENDERER_MAX )
            return 0;
        if( value != RS_CS2_RENDERER_LAUNCH_DEFAULT &&
            !(app->host.renderer_available & (1u << (unsigned)(value - 1))) )
            return 0;
        option = RS_CS2_DEVICEOPTION_RENDERER;
        app->host.renderer_picked = true;
        break;
    default:
        /* Includes every read-only readout. */
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
 * The twelve bonuses come out of the record's own param table.
 * @see app_plugin_objtype_fill_bonuses.
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

    out->has_bonuses = app_plugin_objtype_fill_bonuses(type, out) ? 1 : 0;
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
/*
 * Any container the client holds, by its wire id -- the id
 * on_inventory_changed reports (shops, trade, group storage, ...). A
 * container the server never sent answers 0, as does a slot past its end.
 */
static int
app_plugin_container_slot(
    void* user,
    int container_id,
    int slot,
    int* out_obj_id,
    int* out_count)
{
    struct App* app = (struct App*)user;
    struct InvContainer const* held;

    assert(app);

    if( container_id < 0 || slot < 0 )
        return 0;
    held = InvManager_FindContainer(&app->invs, container_id);
    if( !held || slot >= InvManager_Size(&app->invs, container_id) )
        return 0;

    if( out_obj_id )
        *out_obj_id = InvManager_GetObj(&app->invs, container_id, slot);
    if( out_count )
        *out_count = InvManager_GetNum(&app->invs, container_id, slot);
    return 1;
}

static int
app_plugin_container_size(void* user, int container_id)
{
    struct App* app = (struct App*)user;

    assert(app);

    if( container_id < 0 || !InvManager_FindContainer(&app->invs, container_id) )
        return 0;
    return InvManager_Size(&app->invs, container_id);
}

/*
 * World entities (sailing boats) for WorldView.worldEntities(). An entity is
 * listed once its view is live; its staging rectangle is the view's, 0,0
 * until REBUILD_WORLDENTITY has built it.
 */
static int
app_plugin_world_entity_next(void* user, int iter, struct ToriRS_WorldEntitySnapshot* out)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(out);

    for( int id = iter < 1 ? 1 : iter + 1; id < WORLDVIEW_MAX; id++ )
    {
        struct Wev const* wev;
        struct Worldview const* view;

        if( !Wevs_IsLive(&app->wevs, id) || !WorldviewRegistry_IsLive(&app->worldviews, id) )
            continue;
        wev = Wevs_Get(&app->wevs, id);
        if( !wev->config )
            continue;
        view = WorldviewRegistry_Get(&app->worldviews, id);
        memset(out, 0, sizeof(*out));
        out->id = id;
        out->parent_view = wev->parent_view_id;
        out->config_id = wev->config_id;
        out->fine_x = wev->x;
        out->fine_z = wev->z;
        out->angle = wev->angle & 2047;
        out->level = wev->parent_level;
        out->deck_level = app_wev_deck_level(app, id);
        out->base_x = view->base_x;
        out->base_z = view->base_z;
        out->size_x = view->size_x_tiles;
        out->size_z = view->size_z_tiles;
        return id;
    }
    return -1;
}

/*
 * The locs of entity `id`'s own world: its deck and hull pieces, which live
 * in the VIEW world's scenery table (the root's loc_next never sees them).
 * The view world's base is the staging base, so the snapshot's tiles are
 * absolute staging tiles -- what world_entity_to_main carries to the root
 * after the base is taken off.
 */
static int
app_plugin_world_entity_loc_next(void* user, int id, int iter, struct ToriRS_ScenerySnapshot* out)
{
    struct App* app = (struct App*)user;
    struct Worldview const* view;
    struct World_EntityPool* pool;
    int at;

    assert(app);
    assert(out);
    if( id <= 0 || id >= WORLDVIEW_MAX || !Wevs_IsLive(&app->wevs, id) ||
        !WorldviewRegistry_IsLive(&app->worldviews, id) )
        return -1;
    view = WorldviewRegistry_Get(&app->worldviews, id);
    if( !view->world )
        return -1;
    pool = &view->world->entities.scenery;
    at = iter < 0 ? World_EntityPoolHead(pool) : World_EntityPoolNext(pool, iter);
    while( at != WORLD_ENTITY_NIL )
    {
        struct WorldEntity_Scenery* scenery = World_EntityPoolGet(pool, at);
        if( scenery )
        {
            app_plugin_fill_loc_in(view->world, scenery, out);
            return at;
        }
        at = World_EntityPoolNext(pool, at);
    }
    return -1;
}

/*
 * A point of entity `id`'s own world (fine, relative to its staging base) to
 * ABSOLUTE root fine: through the deck box of each carrier in turn, the same
 * descent that draws the deck (a boat carried by a boat goes through both).
 */
static int
app_plugin_world_entity_to_main(
    void* user,
    int id,
    int deck_fine_x,
    int deck_fine_z,
    int* out_fine_x,
    int* out_fine_z)
{
    struct App* app = (struct App*)user;
    int x = deck_fine_x;
    int z = deck_fine_z;

    assert(app);
    assert(out_fine_x);
    assert(out_fine_z);

    for( int depth = 0; depth < 8; depth++ )
    {
        struct Wev const* wev;
        struct World const* parent_world;
        struct WevDeckBox box;
        int parent_x;
        int parent_z;

        if( id <= 0 || id >= WORLDVIEW_MAX || !Wevs_IsLive(&app->wevs, id) ||
            !WorldviewRegistry_IsLive(&app->worldviews, id) )
            return 0;
        wev = Wevs_Get(&app->wevs, id);
        if( !wev->config )
            return 0;
        if( wev->parent_view_id == 0 )
            parent_world = app->world;
        else if( WorldviewRegistry_IsLive(&app->worldviews, wev->parent_view_id) )
            parent_world = WorldviewRegistry_Get(&app->worldviews, wev->parent_view_id)->world;
        else
            return 0;
        if( !parent_world )
            return 0;
        app_wev_deck_box(app, wev, parent_world, &box);
        Wev_ParentFromDeck(&box, x, z, &parent_x, &parent_z);
        /* scene-local to the parent world -> absolute */
        x = parent_x + (parent_world->_base_tile_x << 7);
        z = parent_z + (parent_world->_base_tile_z << 7);
        if( wev->parent_view_id == 0 )
        {
            *out_fine_x = x;
            *out_fine_z = z;
            return 1;
        }
        /* the carrier's own world: relative to ITS staging base, then on */
        {
            struct Worldview const* carrier =
                WorldviewRegistry_Get(&app->worldviews, wev->parent_view_id);
            x -= carrier->base_x << 7;
            z -= carrier->base_z << 7;
        }
        id = wev->parent_view_id;
    }
    return 0;
}

static int
app_plugin_inv_slot(void* user, int inv, int slot, int* out_obj_id, int* out_count)
{
    int container = app_plugin_inv_container(inv);

    if( container == INV_MANAGER_CONTAINER_NONE )
        return 0;
    return app_plugin_container_slot(user, container, slot, out_obj_id, out_count);
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

/*
 * Is this marker one the scene can hold, and where does it go if so?
 *
 * Two questions the caller cannot skip either half of. TORIRS_TILE_ON_TOP is
 * the answer for every marker whose group asked to be on top, and it is also
 * the answer whenever the scene cannot ANSWER the other one -- a marked tile
 * outside the scene's own grid has no ground command to be drawn after, and a
 * deck tile is addressed in a boat's own tile numbers, which collide with the
 * root's. Saying "on top" there is not a fallback that hides a bug: it is the
 * only ordering that exists for a tile the root paint never puts down, and it
 * is what the marker did before this parameter existed.
 */
static bool
app_plugin_tile_mark_key(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int depth,
    int* out_key)
{
    assert(app);
    assert(out_key);
    if( depth != TORIRS_TILE_IN_SCENE )
        return false;
    if( !app->world )
        return false;
    if( scene_x < 0 || scene_z < 0 )
        return false;
    if( scene_x >= app->world->_scene_size || scene_z >= app->world->_scene_size )
        return false;
    if( scene_x >= TILE_MARK_STAGE_SCENE_MAX ||
        scene_z >= TILE_MARK_STAGE_SCENE_MAX )
        return false;
    if( level < 0 || level >= WORLD_MAP_TERRAIN_LEVELS )
        return false;
    *out_key = TileMarkStage_Key(scene_x, scene_z, level);
    return true;
}

static int
app_plugin_draw_tile(
    void* user,
    int tile_x,
    int tile_z,
    int level,
    uint32_t rgb,
    int outline_width,
    uint32_t fill_rgb,
    int fill_alpha,
    int depth)
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
    int mark_key = 0;
    bool in_scene = false;
    int before;

    assert(app);
    assert(depth == TORIRS_TILE_ON_TOP || depth == TORIRS_TILE_IN_SCENE);

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
                     ? World_HeightAt(view->world, local_x * 128, local_z * 128, level)
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
    in_scene =
        app_plugin_tile_mark_key(app, scene_x, scene_z, level, depth, &mark_key);

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

    /*
     * The stage decides WHERE the next few pushes land, and it is opened
     * around the pushes rather than consulted inside them, so that the
     * polygon helpers below stay the two the overlay list has always used.
     * The marker is the same quad, the same wash and the same border on both
     * sides of this; only the list differs, and with it the moment the raster
     * sees it.
     */
    if( in_scene )
        app_world_tile_mark_begin(
            app,
            mark_key,
            app->world_views.emit_desc.x,
            app->world_views.emit_desc.y,
            app->world_views.emit_desc.w,
            app->world_views.emit_desc.h);
    /* Taken AFTER the stage is open: the count is the open list's, and a
     * marker drawn into the scene has to bill the frame the same items the
     * same marker drawn over it would. */
    before = app_overlay_count(app);

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
    /*
     * The border, at the thickness the caller asked for and NOT at all when
     * that is zero.
     *
     * This call was unconditional and the thickness was the overlay's own
     * constant two, which is two separate wrongs in one line: the cache's
     * hovered-tile group is `flags 2|8, thickness 0` -- a wash with no border,
     * which the reference draws as exactly that -- and wore a hard opaque rim
     * here; and the current-tile group's thickness of 2 was indistinguishable
     * from it because both arrived at the same constant. push_polygon declines
     * a width of 0 rather than this deciding it, so every caller gets the same
     * rule.
     */
    app_overlay_push_polygon(
        app, hull_x, hull_y, hull_size, app_plugin_overlay_argb(rgb), outline_width);
    {
        int const spent = app_overlay_count(app) - before;
        if( in_scene )
            app_world_tile_mark_end(app);
        return spent;
    }
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
    app_overlay_push_segment(
        app, x0, y0, x1, y1, app_plugin_overlay_argb(rgb), APP_OVERLAY_SEGMENT_WIDTH);
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
        app_overlay_push_segment(app, x, y, x + w, y, argb, APP_OVERLAY_SEGMENT_WIDTH);
        app_overlay_push_segment(app, x + w, y, x + w, y + h, argb, APP_OVERLAY_SEGMENT_WIDTH);
        app_overlay_push_segment(app, x + w, y + h, x, y + h, argb, APP_OVERLAY_SEGMENT_WIDTH);
        app_overlay_push_segment(app, x, y + h, x, y, argb, APP_OVERLAY_SEGMENT_WIDTH);
    }
    return app_overlay_count(app) - before;
}

/* ------------------------------------------------------------- polygons */

#include "render/torirs_polygon.h"

/*
 * Convex, the way ToriRS_PolygonFillConvex needs: every turn the same way
 * (collinear runs allowed) and the outline crossing each axis direction at
 * most twice -- the second half is what refuses a star, whose turns all agree
 * but which winds round twice.
 */
static bool
app_plugin_polygon_convex(
    int const* xs,
    int const* ys,
    int count)
{
    int sign = 0;
    int x_flips = 0;
    int y_flips = 0;
    int last_dx = 0;
    int last_dy = 0;

    assert(xs);
    assert(ys);
    for( int i = 0; i < count; i++ )
    {
        int const j = (i + 1) % count;
        int const k = (i + 2) % count;
        int64_t const ex = (int64_t)xs[j] - xs[i];
        int64_t const ey = (int64_t)ys[j] - ys[i];
        int64_t const fx = (int64_t)xs[k] - xs[j];
        int64_t const fy = (int64_t)ys[k] - ys[j];
        int64_t const cross = ex * fy - ey * fx;
        int const dx = ex > 0 ? 1 : ex < 0 ? -1 : 0;
        int const dy = ey > 0 ? 1 : ey < 0 ? -1 : 0;

        if( cross != 0 )
        {
            int const s = cross > 0 ? 1 : -1;
            if( sign != 0 && s != sign )
                return false;
            sign = s;
        }
        if( dx != 0 )
        {
            if( last_dx != 0 && dx != last_dx )
                x_flips++;
            last_dx = dx;
        }
        if( dy != 0 )
        {
            if( last_dy != 0 && dy != last_dy )
                y_flips++;
            last_dy = dy;
        }
    }
    /* The walk above does not compare the last direction with the first, so
     * a closed convex outline shows at most two flips per axis, and so does
     * the closing pair it skipped -- three or more is a second lap. */
    return x_flips <= 2 && y_flips <= 2;
}

/*
 * One piece of a concave fill, built slab by slab.
 *
 * The fill rule is java.awt.Polygon's EVEN-ODD. A horizontal slab between two
 * consecutive vertex rows crosses the outline at a fixed set of edges with no
 * vertex inside it, so sorted by x the crossings pair up into trapezoids, and
 * the pairs ARE the even-odd spans. Trapezoids in different slabs share no
 * row (the fill is half-open in y) and the pairs in one slab share no pixel
 * but a vertex they meet at, so a translucent wash has no seams -- which a
 * triangle fan, whose inclusive spans overlap along every diagonal, would.
 *
 * Trapezoids that meet edge to edge down the slabs are chained into one
 * piece. A chain is Y-MONOTONE -- every row crosses it exactly twice -- and
 * that, not convexity, is what ToriRS_PolygonFillConvex needs: it takes the
 * min and max crossing of each row, and for two crossings those are the span.
 * So a piece only ends where the outline forks or closes, or at the renderer's
 * point cap; a perimeter projected over terrain, jagged on every side, stays a
 * handful of runs instead of one per vertex row.
 *
 * Each chain point is an ORIGINAL vertex where it can be: a chain that carries
 * on down the same edge moves its last point instead of adding one, so the
 * piece's sides are the polygon's own edges and land on the pixels the whole
 * polygon would have.
 */
enum
{
    APP_PLUGIN_POLY_CHAIN_MAX = TORIRS_POLYGON_MAX_POINTS / 2,
    APP_PLUGIN_POLY_PIECES_MAX = 64,
    /* Vertex rows, plus the rows a self-crossing outline is split at. */
    APP_PLUGIN_POLY_ROWS_MAX = TORIRS_PLUGIN_ENGINE_POLYGON_POINTS_MAX * 2,
};

struct AppPluginPolyChain
{
    int count;
    int last_edge;
    int x[APP_PLUGIN_POLY_CHAIN_MAX];
    int y[APP_PLUGIN_POLY_CHAIN_MAX];
};

struct AppPluginPolyPiece
{
    bool alive;
    bool continued;
    struct AppPluginPolyChain left;
    struct AppPluginPolyChain right;
};

struct AppPluginPolyCrossing
{
    int edge;
    int x_top;
    int x_bottom;
    double x_mid;
};

/* File scope: the slab walk holds every vertex row and every piece, which is
 * far past what a frame callback should put on the stack. The overlay is built
 * on the one frame thread. */
static int g_app_plugin_poly_rows[APP_PLUGIN_POLY_ROWS_MAX];
static struct AppPluginPolyCrossing g_app_plugin_poly_cross[TORIRS_PLUGIN_ENGINE_POLYGON_POINTS_MAX];
static struct AppPluginPolyPiece g_app_plugin_poly_pieces[APP_PLUGIN_POLY_PIECES_MAX];

static int
app_plugin_poly_int_compare(void const* lhs, void const* rhs)
{
    int const a = *(int const*)lhs;
    int const b = *(int const*)rhs;
    return a < b ? -1 : a > b ? 1 : 0;
}

static int
app_plugin_poly_cross_compare(void const* lhs, void const* rhs)
{
    struct AppPluginPolyCrossing const* a = lhs;
    struct AppPluginPolyCrossing const* b = rhs;
    return a->x_mid < b->x_mid ? -1 : a->x_mid > b->x_mid ? 1 : 0;
}

/* Edge `i` of the outline with its ends ordered top to bottom. */
static void
app_plugin_poly_edge(
    int const* xs,
    int const* ys,
    int count,
    int i,
    int* x0,
    int* y0,
    int* x1,
    int* y1)
{
    int const j = (i + 1) % count;
    if( ys[i] <= ys[j] )
    {
        *x0 = xs[i];
        *y0 = ys[i];
        *x1 = xs[j];
        *y1 = ys[j];
    }
    else
    {
        *x0 = xs[j];
        *y0 = ys[j];
        *x1 = xs[i];
        *y1 = ys[i];
    }
}

/* The same integer step ToriRS_PolygonFillConvex takes along an edge, so a
 * piece's corner lands on the pixel the whole polygon's edge would have. */
static int
app_plugin_poly_edge_x(int x0, int y0, int x1, int y1, int y)
{
    return (int)((int64_t)x0 + (int64_t)(x1 - x0) * (int64_t)(y - y0) / (int64_t)(y1 - y0));
}

/* Append a chain point -- or, when the chain is carrying on down the same
 * edge, move its last point there instead. */
static void
app_plugin_poly_chain_push(struct AppPluginPolyChain* chain, int edge, int x, int y)
{
    if( chain->count >= 2 && chain->last_edge == edge )
    {
        chain->x[chain->count - 1] = x;
        chain->y[chain->count - 1] = y;
        return;
    }
    assert(chain->count < APP_PLUGIN_POLY_CHAIN_MAX);
    chain->x[chain->count] = x;
    chain->y[chain->count] = y;
    chain->count++;
    chain->last_edge = edge;
}

/* Left chain down, right chain back up: the piece's outline in order. */
static int
app_plugin_poly_piece_outline(
    struct AppPluginPolyPiece const* piece,
    int* xs,
    int* ys)
{
    int n = 0;
    for( int i = 0; i < piece->left.count; i++ )
    {
        xs[n] = piece->left.x[i];
        ys[n] = piece->left.y[i];
        n++;
    }
    for( int i = piece->right.count - 1; i >= 0; i-- )
    {
        if( n > 0 && xs[n - 1] == piece->right.x[i] && ys[n - 1] == piece->right.y[i] )
            continue;
        xs[n] = piece->right.x[i];
        ys[n] = piece->right.y[i];
        n++;
    }
    if( n > 1 && xs[n - 1] == xs[0] && ys[n - 1] == ys[0] )
        n--;
    return n;
}

static void
app_plugin_poly_piece_flush(
    struct App* app,
    struct AppPluginPolyPiece* piece,
    uint32_t argb,
    int trans)
{
    int xs[TORIRS_POLYGON_MAX_POINTS];
    int ys[TORIRS_POLYGON_MAX_POINTS];
    int const n = app_plugin_poly_piece_outline(piece, xs, ys);

    app_overlay_push_polygon_filled(app, xs, ys, n, argb, trans);
    piece->alive = false;
}

/* Does a chain have room for one more row on this edge? */
static bool
app_plugin_poly_chain_room(struct AppPluginPolyChain const* chain, int edge)
{
    return chain->last_edge == edge || chain->count < APP_PLUGIN_POLY_CHAIN_MAX;
}

/*
 * The row two edges cross at, when they cross strictly inside (top, bottom):
 * an outline that crosses itself swaps the order of its crossings mid-slab,
 * and the pairing is only right on either side of the swap.
 */
static bool
app_plugin_poly_split_row(
    int const* xs,
    int const* ys,
    int count,
    int edge_a,
    int edge_b,
    int top,
    int bottom,
    int* out_row)
{
    int ax0, ay0, ax1, ay1, bx0, by0, bx1, by1;
    double sa, sb, row;

    app_plugin_poly_edge(xs, ys, count, edge_a, &ax0, &ay0, &ax1, &ay1);
    app_plugin_poly_edge(xs, ys, count, edge_b, &bx0, &by0, &bx1, &by1);
    sa = (double)(ax1 - ax0) / (double)(ay1 - ay0);
    sb = (double)(bx1 - bx0) / (double)(by1 - by0);
    if( sa == sb )
        return false;
    /* ax0 + sa*(y - ay0) == bx0 + sb*(y - by0) */
    row = ((double)bx0 - (double)ax0 + sa * ay0 - sb * by0) / (sa - sb);
    if( !(row > (double)top + 0.5 && row < (double)bottom - 0.5) )
        return false;
    *out_row = (int)(row + 0.5);
    return *out_row > top && *out_row < bottom;
}

static void
app_plugin_fill_polygon(
    struct App* app,
    int const* xs,
    int const* ys,
    int count,
    uint32_t argb,
    int trans)
{
    int* rows = g_app_plugin_poly_rows;
    struct AppPluginPolyCrossing* cross = g_app_plugin_poly_cross;
    struct AppPluginPolyPiece* pieces = g_app_plugin_poly_pieces;
    int row_count = 0;

    assert(app);
    assert(xs);
    assert(ys);
    assert(count >= 3);
    assert(count <= TORIRS_PLUGIN_ENGINE_POLYGON_POINTS_MAX);

    if( count <= TORIRS_POLYGON_MAX_POINTS && app_plugin_polygon_convex(xs, ys, count) )
    {
        app_overlay_push_polygon_filled(app, xs, ys, count, argb, trans);
        return;
    }

    for( int i = 0; i < count; i++ )
        rows[i] = ys[i];
    qsort(rows, (size_t)count, sizeof(rows[0]), app_plugin_poly_int_compare);
    for( int i = 0; i < count; i++ )
        if( row_count == 0 || rows[row_count - 1] != rows[i] )
            rows[row_count++] = rows[i];
    for( int p = 0; p < APP_PLUGIN_POLY_PIECES_MAX; p++ )
        pieces[p].alive = false;

    for( int s = 0; s + 1 < row_count; s++ )
    {
        int const top = rows[s];
        int bottom = rows[s + 1];
        double mid;
        int crossings;
        int split = 0;

    slab:
        mid = ((double)top + (double)bottom) * 0.5;
        crossings = 0;
        for( int i = 0; i < count; i++ )
        {
            int x0, y0, x1, y1;
            app_plugin_poly_edge(xs, ys, count, i, &x0, &y0, &x1, &y1);
            if( y0 == y1 || y0 > top || y1 < bottom )
                continue;
            cross[crossings].edge = i;
            cross[crossings].x_top = app_plugin_poly_edge_x(x0, y0, x1, y1, top);
            cross[crossings].x_bottom = app_plugin_poly_edge_x(x0, y0, x1, y1, bottom);
            cross[crossings].x_mid =
                (double)x0 + (double)(x1 - x0) * (mid - (double)y0) / (double)(y1 - y0);
            crossings++;
        }
        qsort(cross, (size_t)crossings, sizeof(cross[0]), app_plugin_poly_cross_compare);
        /* Two neighbours in the wrong order at either end of the slab cross
         * inside it: split the slab at that row and pair each half alone. */
        for( int c = 0; c + 1 < crossings; c++ )
        {
            if( (cross[c].x_top > cross[c + 1].x_top || cross[c].x_bottom > cross[c + 1].x_bottom) &&
                row_count < APP_PLUGIN_POLY_ROWS_MAX &&
                app_plugin_poly_split_row(
                    xs, ys, count, cross[c].edge, cross[c + 1].edge, top, bottom, &split) )
            {
                memmove(&rows[s + 2], &rows[s + 1], sizeof(rows[0]) * (size_t)(row_count - s - 1));
                rows[s + 1] = split;
                row_count++;
                bottom = split;
                goto slab;
            }
        }

        for( int p = 0; p < APP_PLUGIN_POLY_PIECES_MAX; p++ )
            pieces[p].continued = false;
        for( int c = 0; c + 1 < crossings; c += 2 )
        {
            struct AppPluginPolyCrossing const* l = &cross[c];
            struct AppPluginPolyCrossing const* r = &cross[c + 1];
            struct AppPluginPolyPiece* piece = NULL;

            /* The piece this trapezoid hangs under: its bottom row is this
             * slab's top row, corner for corner. */
            for( int p = 0; p < APP_PLUGIN_POLY_PIECES_MAX; p++ )
            {
                struct AppPluginPolyPiece* open = &pieces[p];
                if( !open->alive || open->continued )
                    continue;
                if( open->left.y[open->left.count - 1] == top &&
                    open->left.x[open->left.count - 1] == l->x_top &&
                    open->right.x[open->right.count - 1] == r->x_top )
                {
                    piece = open;
                    break;
                }
            }
            if( piece && app_plugin_poly_chain_room(&piece->left, l->edge) &&
                app_plugin_poly_chain_room(&piece->right, r->edge) )
            {
                app_plugin_poly_chain_push(&piece->left, l->edge, l->x_bottom, bottom);
                app_plugin_poly_chain_push(&piece->right, r->edge, r->x_bottom, bottom);
                piece->continued = true;
                continue;
            }
            if( piece )
                app_plugin_poly_piece_flush(app, piece, argb, trans);
            piece = NULL;
            for( int p = 0; p < APP_PLUGIN_POLY_PIECES_MAX; p++ )
                if( !pieces[p].alive )
                {
                    piece = &pieces[p];
                    break;
                }
            if( !piece )
            {
                /* More simultaneous spans than pieces: this trapezoid goes out
                 * on its own, unchained. Same pixels, more items. */
                int tx[4] = { l->x_top, r->x_top, r->x_bottom, l->x_bottom };
                int ty[4] = { top, top, bottom, bottom };
                app_overlay_push_polygon_filled(app, tx, ty, 4, argb, trans);
                continue;
            }
            piece->alive = true;
            piece->continued = true;
            piece->left.count = 0;
            piece->left.last_edge = -1;
            piece->right.count = 0;
            piece->right.last_edge = -1;
            app_plugin_poly_chain_push(&piece->left, l->edge, l->x_top, top);
            app_plugin_poly_chain_push(&piece->left, l->edge, l->x_bottom, bottom);
            app_plugin_poly_chain_push(&piece->right, r->edge, r->x_top, top);
            app_plugin_poly_chain_push(&piece->right, r->edge, r->x_bottom, bottom);
        }
        /* A piece nothing continued has ended: the outline closed under it,
         * or forked. */
        for( int p = 0; p < APP_PLUGIN_POLY_PIECES_MAX; p++ )
            if( pieces[p].alive && !pieces[p].continued )
                app_plugin_poly_piece_flush(app, &pieces[p], argb, trans);
    }
    for( int p = 0; p < APP_PLUGIN_POLY_PIECES_MAX; p++ )
        if( pieces[p].alive )
            app_plugin_poly_piece_flush(app, &pieces[p], argb, trans);
}

static int
app_plugin_draw_polygon(
    void* user,
    int const* xs,
    int const* ys,
    int count,
    uint32_t fill_rgb,
    int fill_alpha,
    uint32_t outline_rgb,
    int outline_width)
{
    struct App* app = (struct App*)user;
    int before;

    assert(app);
    assert(xs);
    assert(ys);
    assert(count > 0);
    assert(count <= TORIRS_PLUGIN_ENGINE_POLYGON_POINTS_MAX);

    before = app_overlay_count(app);
    if( fill_alpha > 0 && count >= 3 )
        app_plugin_fill_polygon(
            app,
            xs,
            ys,
            count,
            app_plugin_overlay_argb(fill_rgb),
            255 - (fill_alpha > 255 ? 255 : fill_alpha));
    /* push_polygon declines a width of 0 and a single point, and draws two
     * points as one segment -- the same rule every border here follows. */
    if( outline_width > 0 )
        app_overlay_push_polygon(
            app, xs, ys, count, app_plugin_overlay_argb(outline_rgb), outline_width);
    return app_overlay_count(app) - before;
}

/* --------------------------------------------------------- text measuring */

/* The font is draw_text's own (app_plugin_draw_text): the cache's p11, the
 * hitsplat face. Asking for it also queues its load, exactly as drawing does. */
static int
app_plugin_text_width(void* user, char const* text)
{
    struct App* app = (struct App*)user;
    int font_id;

    assert(app);
    assert(text);
    font_id = app_hitsplat_font_scene_id(app);
    if( font_id < 0 || !ToriDraw_SceneFontGet(app->scene, font_id) )
        return -1;
    return app_measure_text_cb(app, font_id, text);
}

static int
app_plugin_text_height(void* user)
{
    struct App* app = (struct App*)user;
    struct ToriDraw_Font* font;
    int font_id;

    assert(app);
    font_id = app_hitsplat_font_scene_id(app);
    font = font_id >= 0 ? ToriDraw_SceneFontGet(app->scene, font_id) : NULL;
    if( !font )
        return -1;
    return font->line_height;
}

/* ---------------------------------------------------------------- minimap */

/*
 * The minimap's projection is the dots' own, call for call: the player's
 * position pushed out through a hull when aboard (App_MinimapBuildDots), the
 * offset turned by the angle the emit walk blitted the map at, and refused
 * exactly where MinimapView_PlaceDot refuses a dot -- past the ring -- or
 * where the dot scissor would cut it -- outside the map's box. A zero-sized
 * sprite makes PlaceDot's top-left the point itself.
 */
static int
app_plugin_minimap_project(void* user, int fine_x, int fine_z, int* out_x, int* out_y)
{
    struct App* app = (struct App*)user;
    struct WorldEntity_Player* local;
    struct MinimapRotation rotation;
    int yaw;
    int px;
    int pz;
    int dx = 0;
    int dy = 0;
    int64_t rel_x;
    int64_t rel_z;

    assert(app);
    assert(out_x);
    assert(out_y);
    if( !app->minimap.valid || !app->world || !app->world->load_complete )
        return 0;
    local = app_local_player(app);
    if( !local )
        return 0;
    px = (int)local->draw_position.x;
    pz = (int)local->draw_position.z;
    app_wev_actor_root_fine(app, &local->view_placement, &px, &pz);
    rel_x = (int64_t)fine_x - px;
    rel_z = (int64_t)fine_z - pz;
    /* Far past the ring either way; keeps PlaceDot's squares in range. */
    if( rel_x < -(1 << 20) || rel_x > (1 << 20) || rel_z < -(1 << 20) || rel_z > (1 << 20) )
        return 0;
    yaw = MinimapView_Yaw(&app->minimap);
    rotation.sin = ToriDraw_Sin(yaw);
    rotation.cos = ToriDraw_Cos(yaw);
    if( !MinimapView_PlaceDot(&rotation, (int)rel_x, (int)rel_z, 0, 0, &dx, &dy) )
        return 0;
    *out_x = app->minimap.x + app->minimap.w / 2 + dx;
    *out_y = app->minimap.y + app->minimap.h / 2 + dy;
    return *out_x >= app->minimap.x && *out_x < app->minimap.x + app->minimap.w &&
           *out_y >= app->minimap.y && *out_y < app->minimap.y + app->minimap.h;
}

static int
app_plugin_minimap_bounds(void* user, struct ToriRS_MinimapBounds* out)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(out);
    if( !app->minimap.valid )
        return 0;
    out->box = (struct ToriRS_Rect){ app->minimap.x, app->minimap.y, app->minimap.w, app->minimap.h };
    out->centre_x = app->minimap.x + app->minimap.w / 2;
    out->centre_y = app->minimap.y + app->minimap.h / 2;
    out->radius = MINIMAP_DOT_RADIUS_PIXELS;
    return 1;
}

/* -------------------------------------------------------------- sequences */

static int
app_plugin_seq_def(void* user, int seq_id, struct ToriRS_SeqDef* out)
{
    struct App* app = (struct App*)user;
    struct ToriDraw_Animation* anim;

    assert(app);
    assert(out);
    if( !app->provider || !app->scene )
        return TORIRS_ASSET_MISSING;
    anim = ToriDraw_SceneAnimationGet(app->scene, seq_id);
    if( !anim )
        return app_plugin_record_miss(app, APP_PLUGIN_LOAD_SEQ, seq_id);
    /* The loader registers an empty animation for an id the cache has no
     * record of (so a second ask does not load it again): no frames is no
     * such seq. */
    if( anim->frame_count <= 0 )
        return TORIRS_ASSET_MISSING;
    out->id = seq_id;
    out->frame_count = anim->frame_count;
    out->frame_step = anim->frame_step > 0 ? anim->frame_step : -1;
    out->skeletal = anim->skeletal != NULL;
    out->duration = 0;
    out->frame_lengths.count = anim->frame_count;
    for( int i = 0; i < anim->frame_count; i++ )
    {
        int const length = anim->frames ? anim->frames[i].delay : 1;
        out->duration += length;
        if( i < out->frame_lengths.capacity )
            out->frame_lengths.values[i] = length;
    }
    return TORIRS_ASSET_READY;
}

/* -------------------------------------------------------------- world map */

/*
 * The open world map's view, read the way app_worldmap_build_tiles lays the
 * regions out: the box the emit walk drew the surface into, the display
 * position at its centre, and a region's width at the LIVE zoom -- mid-zoom
 * included, which is what the regions are drawn at too.
 */
static bool
app_plugin_worldmap_view(
    struct App* app,
    struct ToriRS_WorldMapArea const** out_area,
    int* out_display_x,
    int* out_display_y,
    int* out_region_px)
{
    struct RS_WorldMapState* map = app->host.worldmap;
    int scale_fp;

    assert(out_area);
    assert(out_display_x);
    assert(out_display_y);
    assert(out_region_px);
    if( !map || !app_worldmap_surface_live(app) )
        return false;
    if( app->worldmap.drag.box_w <= 0 || app->worldmap.drag.box_h <= 0 )
        return false;
    *out_area = RS_WorldMap_CurrentArea(map);
    if( !*out_area )
        return false;
    scale_fp = RS_WorldMap_ZoomScaleFp(map);
    if( scale_fp <= 0 )
        scale_fp = RS_WORLDMAP_ZOOM_SCALE_ONE;
    *out_region_px = WORLD_MAP_TERRAIN_X * scale_fp / RS_WORLDMAP_ZOOM_SCALE_ONE;
    if( *out_region_px <= 0 )
        *out_region_px = 1;
    RS_WorldMap_DisplayPosition(map, out_display_x, out_display_y);
    return *out_display_x >= 0 && *out_display_y >= 0;
}

static int
app_plugin_worldmap_project(
    void* user,
    int tile_x,
    int tile_z,
    int level,
    int* out_x,
    int* out_y)
{
    struct App* app = (struct App*)user;
    struct ToriRS_WorldMapArea const* area = NULL;
    int display_x;
    int display_y;
    int region_px;
    int map_x;
    int map_y;
    int64_t x;
    int64_t y;

    assert(app);
    assert(out_x);
    assert(out_y);
    if( !app_plugin_worldmap_view(app, &area, &display_x, &display_y, &region_px) )
        return 0;
    if( !ToriRS_WorldMapArea_Position(area, level, tile_x, tile_z, &map_x, &map_y) )
        return 0;
    /* The tile's centre: half a tile in from the corner the regions and icons
     * are placed by, so the offsets are in half tiles. y runs up the map. */
    x = (int64_t)app->worldmap.drag.box_x + app->worldmap.drag.box_w / 2 +
        ((int64_t)(map_x - display_x) * 2 + 1) * region_px / (2 * WORLD_MAP_TERRAIN_X);
    y = (int64_t)app->worldmap.drag.box_y + app->worldmap.drag.box_h / 2 -
        ((int64_t)(map_y - display_y) * 2 + 1) * region_px / (2 * WORLD_MAP_TERRAIN_Z);
    if( x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX )
        return 0;
    *out_x = (int)x;
    *out_y = (int)y;
    return 1;
}

static int
app_plugin_worldmap_bounds(void* user, struct ToriRS_Rect* out)
{
    struct App* app = (struct App*)user;
    struct ToriRS_WorldMapArea const* area = NULL;
    int display_x;
    int display_y;
    int region_px;

    assert(app);
    assert(out);
    if( !app_plugin_worldmap_view(app, &area, &display_x, &display_y, &region_px) )
        return 0;
    *out = (struct ToriRS_Rect){
        app->worldmap.drag.box_x,
        app->worldmap.drag.box_y,
        app->worldmap.drag.box_w,
        app->worldmap.drag.box_h,
    };
    return 1;
}

/* WorldMap.setWorldMapPositionTarget: the map's own jumptosourcecoord (a
 * glide, not the _INSTANT form), in the area it shows now. */
static int
app_plugin_worldmap_jump(void* user, int tile_x, int tile_z, int level)
{
    struct App* app = (struct App*)user;
    struct RS_WorldMapState* map;
    int packed;
    int display_x;
    int display_y;

    assert(app);
    map = app->host.worldmap;
    if( !map || !app_worldmap_surface_live(app) || !RS_WorldMap_CurrentArea(map) )
        return TORIRS_RESULT_UNSUPPORTED;
    packed = ToriRS_WorldMapPackCoord(level, tile_x, tile_z);
    if( !RS_WorldMap_SourceToDisplay(map, packed, &display_x, &display_y) )
        return TORIRS_RESULT_NOT_FOUND;
    RS_WorldMap_JumpToSourceCoord(map, packed, false);
    return TORIRS_RESULT_OK;
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
    if( !PngDecode_Argb(ToriMem_Frame(), data, size, &w, &h, &pixels) )
        return 0;

    scene_id = UITreeSceneBridge_PublishPluginImage(&app->bridge, slot, w, h, pixels);
    ToriMem_Free(ToriMem_Frame(), pixels);
    if( scene_id < 0 )
        return 0;

    *out_w = w;
    *out_h = h;
    return 1;
}

/*
 * The publish above, shrunk to fit a box: a capture is the whole client
 * canvas and its gallery cell is a tenth of that. An area average, not a
 * nearest pick -- a frame of 1-pixel text and tile lines sampled every tenth
 * pixel is noise. Never enlarged: a picture already smaller than the box is
 * published as it is.
 */
static int
app_plugin_image_publish_fit(
    void* user,
    int slot,
    void const* data,
    int size,
    int max_w,
    int max_h,
    int* out_w,
    int* out_h)
{
    struct App* app = (struct App*)user;
    uint32_t* pixels = NULL;
    uint32_t* fitted;
    int w = 0;
    int h = 0;
    int fw;
    int fh;
    int scene_id;

    assert(app);
    assert(data);
    assert(max_w > 0);
    assert(max_h > 0);
    assert(out_w);
    assert(out_h);

    if( !PngDecode_Argb(ToriMem_Frame(), data, size, &w, &h, &pixels) )
        return 0;
    /* The larger of the two shrinks decides, so the whole picture fits. */
    if( (long long)w * max_h > (long long)h * max_w )
    {
        fw = w < max_w ? w : max_w;
        fh = (int)((long long)h * fw / w);
    }
    else
    {
        fh = h < max_h ? h : max_h;
        fw = (int)((long long)w * fh / h);
    }
    if( fw < 1 )
        fw = 1;
    if( fh < 1 )
        fh = 1;

    fitted = ToriMem_Alloc(ToriMem_Frame(), (size_t)fw * (size_t)fh * sizeof(*fitted));
    assert(fitted);
    for( int y = 0; y < fh; y++ )
    {
        int const y0 = (int)((long long)y * h / fh);
        int y1 = (int)((long long)(y + 1) * h / fh);

        if( y1 <= y0 )
            y1 = y0 + 1;
        for( int x = 0; x < fw; x++ )
        {
            int const x0 = (int)((long long)x * w / fw);
            int x1 = (int)((long long)(x + 1) * w / fw);
            uint32_t sum[4] = { 0, 0, 0, 0 };
            uint32_t count;

            if( x1 <= x0 )
                x1 = x0 + 1;
            for( int sy = y0; sy < y1; sy++ )
                for( int sx = x0; sx < x1; sx++ )
                {
                    uint32_t const p = pixels[sy * w + sx];
                    sum[0] += p >> 24;
                    sum[1] += (p >> 16) & 0xFF;
                    sum[2] += (p >> 8) & 0xFF;
                    sum[3] += p & 0xFF;
                }
            count = (uint32_t)((y1 - y0) * (x1 - x0));
            fitted[y * fw + x] = ((sum[0] / count) << 24) | ((sum[1] / count) << 16) |
                                 ((sum[2] / count) << 8) | (sum[3] / count);
        }
    }
    ToriMem_Free(ToriMem_Frame(), pixels);

    scene_id = UITreeSceneBridge_PublishPluginImage(&app->bridge, slot, fw, fh, fitted);
    ToriMem_Free(ToriMem_Frame(), fitted);
    if( scene_id < 0 )
        return 0;
    *out_w = fw;
    *out_h = fh;
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
        RedrawRequest_Raise(&app->redraw_request);
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
     * The bridge RASTERISES an icon; it does not fetch what an icon is made
     * of. `bridge_ensure_obj_icon` answers -1 when the objtype or its
     * inventory model is not resident and queues nothing -- so this comment's
     * old claim, that "the bridge has already asked for both", was simply
     * false, and an icon nothing else in the client had already drawn was
     * PENDING for ever. The Loot Tracker's drop cells were empty from the day
     * they were written for exactly this reason: a goblin's coins and bones
     * are not in your backpack, so nobody had loaded their models.
     *
     * The inventory does not rely on the bridge either -- a slot's Task_InvIcon
     * awaits its obj's whole model chain (ASSET_KIND_OBJ_MODEL) before it
     * rasterises anything. This is that step, for the plugin lane.
     *
     * The request is made from a plugin's COMPOSE, at the plugin's own
     * cadence, and goes through the assets table: one load of the chain in
     * flight however often it is asked.
     */
    if( scene_id < 0 )
    {
        enum AssetState const state = Assets_Want(
            app->assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, obj_id, count > 0 ? count : 1));

        /*
         * HERE means the whole model chain is resident (ObjModelLoad_NeedsWork
         * is false) and the icon still will not build; FAILED is a chain the
         * table remembers it could not load. Both are terminal, and saying
         * "not yet" for them is what makes a caller ask for the rest of the
         * session.
         */
        if( state == ASSET_STATE_HERE || state == ASSET_STATE_FAILED )
            return -1;
        return 0;
    }

    /* Past here the bridge HAS the icon, so anything that goes wrong is about
     * this slot rather than about the obj, and is terminal for the caller:
     * asking again cannot change the answer. */
    sprites = ToriDraw_SceneSpriteGet(app->scene, scene_id, &found);
    if( !sprites || found <= 0 )
        return -1;
    sprite = sprites[0];
    if( !sprite || !sprite->pixels_argb || sprite->width <= 0 || sprite->height <= 0 )
        return -1;

    if( UITreeSceneBridge_PublishPluginImage(
            &app->bridge, slot, sprite->width, sprite->height, sprite->pixels_argb) < 0 )
        return -1;

    *out_w = sprite->width;
    *out_h = sprite->height;
    return 1;
}

/*
 * One frame of a cache sprite, for cache.sprite_image. Bound through the
 * bridge the way a script's cc_setgraphic is (UITreeSceneBridge_BindSprite):
 * a sprite not resident yet gets its scene id now and the bridge's pump fills
 * it when it lands, so "not yet" is 0 and asking again finishes it. The
 * pixels are the scene sprite's own, read before anything else runs.
 */
static int
app_plugin_sprite_pixels(
    void* user,
    int sprite_id,
    int frame,
    uint32_t const** out_argb,
    int* out_w,
    int* out_h)
{
    struct App* app = (struct App*)user;
    struct ToriDraw_Sprite** sprites;
    struct ToriDraw_Sprite const* sprite;
    int scene_id;
    int found = 0;

    assert(app);
    assert(out_argb);
    assert(out_w);
    assert(out_h);
    assert(sprite_id >= 0);
    assert(frame >= 0);
    scene_id = UITreeSceneBridge_BindSprite(&app->bridge, sprite_id);
    if( scene_id <= 0 )
        return -1;
    sprites = ToriDraw_SceneSpriteGet(app->scene, scene_id, &found);
    if( !sprites || found <= 0 )
        return 0;
    if( frame >= found )
        return -1;
    sprite = sprites[frame];
    if( !sprite || !sprite->pixels_argb || sprite->width <= 0 || sprite->height <= 0 )
        return -1;
    *out_argb = sprite->pixels_argb;
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
     *
     * The same statement covers a plugin that COVERS a native control and
     * delegates to it: the minimap orbs hide the lane's own orb under their
     * plate, and the button they press is a child of it, so the hide they
     * applied themselves is on this walk too.
     */
    pick.allow_plugin_hidden = 1;

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
    scratch.font_id = app->world_views.frame->minimenu->font_id;
    if( !UIMinimenu_AddOption(&scratch, "", action, action_index, pick) )
        return 0;

    /* 0,0 as the click point, and it is never read: the cross is painted per
     * ACTION (rs_minimenu_cross.h), and a UI button's is UI_CROSS_OFF -- the
     * reference's doAction does not touch crossMode for one either. A
     * synthesised press has no click point to give, and this is the reason it
     * does not need one. */
    saved = *app->world_views.frame->minimenu;
    *app->world_views.frame->minimenu = scratch;
    app_minimenu_run_option(app, 0, 0, 0);
    *app->world_views.frame->minimenu = saved;
    return 1;
}

/* opnpc/oploc/opobj_action_for_slot, exported by verbs-pointer
 * (src/game/rs_minimenu_world.h) for app_plugin_world_op below -- not reached
 * by anything else in this file's existing include chain. */
#include "game/rs_minimenu_world.h"

/*
 * The quest driver's engine seam (verbs-pointer, docs/ARCHITECT.md), which
 * owns this trio of functions inside "the app_plugin_world_op case" of this
 * shared file (S1/S5). Deliberately NOT `static`: torirs_plugin_drive_pointer.c
 * is a separate translation unit outside the app/ layer and forward-declares
 * these three itself (app.h is closed; app/app_internal.h is layer-private --
 * see that file's own banner comment for the fuller account).
 *
 * app_plugin_world_op generalises the app_plugin_click_node scratch-menu
 * trick above to a world NPC/SCENERY/OBJSTACK pick: it fabricates the same
 * one-row UIMinimenu a real row would carry -- id = element_id, secondary_id
 * = the entity's own config type id, tertiary/quaternary = its current SCENE
 * tile, exactly as add_npc_rows/add_scenery_rows/add_obj_rows build them
 * (src/game/rs_minimenu_world.c) -- swaps it in and lets
 * app_minimenu_run_option do everything a real click does: walk, latch,
 * packet. `option` >= 1 is a numbered op slot (1-based; op*_action_for_slot
 * wants slot-1); `option` <= 0 is Examine (OP*6). This is the LOGGED BYPASS
 * behind drive.op and is never the default path -- every call is reported.
 * Returns 0 when `element_id` does not resolve to a live entity of `kind`,
 * 1 once the dispatcher has run (which is "dispatched", not "succeeded" --
 * the same accepted reading docs/QUEST_DRIVER_PLAN.md S7 U5 gives inv_op).
 */
int
app_plugin_world_op(struct App* app, enum DrivePickKind kind, int element_id, int option)
{
    struct UIMinimenu scratch;
    struct UIMinimenu saved;
    struct UIMinimenuPick pick;
    int action;
    int slot;

    assert(app);
    if( !app->world )
        return 0;
    memset(&pick, 0, sizeof(pick));
    slot = option - 1;

    switch( kind )
    {
    case DRIVE_PICK_NPC:
    {
        struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, element_id, NULL);
        if( !npc || npc->server_slot < 0 )
            return 0;
        pick.kind = UI_MINIMENU_PICK_NPC;
        pick.id = element_id;
        pick.secondary_id = npc->npc_id;
        pick.tertiary_id = npc->grid_position.x;
        pick.quaternary_id = npc->grid_position.z;
        action = option >= 1 ? opnpc_action_for_slot(slot) : REVCONFIG_MINIMENU_OPNPC6;
        break;
    }
    case DRIVE_PICK_LOC:
    {
        struct WorldEntity_Scenery* loc = World_SceneryGetByElementId(app->world, element_id);
        if( !loc )
            return 0;
        pick.kind = UI_MINIMENU_PICK_SCENERY;
        pick.id = element_id;
        pick.secondary_id = loc->loc_id;
        pick.tertiary_id = loc->grid_position.x;
        pick.quaternary_id = loc->grid_position.z;
        action = option >= 1 ? oploc_action_for_slot(slot) : REVCONFIG_MINIMENU_OPLOC6;
        break;
    }
    case DRIVE_PICK_OBJ:
    {
        struct WorldEntity_ObjStack* stack = World_ObjStackGetByElementId(app->world, element_id);
        if( !stack )
            return 0;
        pick.kind = UI_MINIMENU_PICK_OBJ;
        pick.id = element_id;
        pick.secondary_id = stack->obj_id;
        pick.tertiary_id = stack->grid_position.x;
        pick.quaternary_id = stack->grid_position.z;
        action = option >= 1 ? opobj_action_for_slot(slot) : REVCONFIG_MINIMENU_OPOBJ6;
        break;
    }
    default:
        return 0;
    }

    TORIRS_REPORT(
        "quest-driver: drive.op bypass kind=%d element=%d option=%d action=%d\n",
        (int)kind,
        element_id,
        option,
        action);

    UIMinimenu_Reset(&scratch);
    scratch.font_id = app->world_views.frame->minimenu->font_id;
    if( !UIMinimenu_AddOption(&scratch, "", action, option >= 1 ? slot : 0, pick) )
        return 0;
    saved = *app->world_views.frame->minimenu;
    *app->world_views.frame->minimenu = scratch;
    app_minimenu_run_option(app, 0, 0, 0);
    *app->world_views.frame->minimenu = saved;
    return 1;
}

/*
 * OPLOC `option` (1..5) on a loc the SERVER has placed at (`abs_x`,`abs_z`)
 * that this client's scenery does not show yet: a zone change waits on its
 * models before the scene lands it (App_DriveLocChangesPending), so there is no
 * element to pick and app_plugin_world_op cannot reach it. The wire wants only
 * the op, the tile and the id (net_out_oploc); the server paths, as it does for
 * every op under the server-authoritative era (app_try_move_op sends no route
 * there). This is what scriptrun's d_world_op sends for the same press, on the
 * tick its packet-built loc list shows the change -- a live seat pressing the
 * Verzik throne door the tick it opened acted two ticks late otherwise (relay
 * f1, 2026-10-12). Returns 0 when there is no game connection or a "Use"
 * selection is armed (an OPLOCU needs the scene's pick path).
 */
int
app_plugin_loc_op_at(struct App* app, int loc_id, int abs_x, int abs_z, int option)
{
    assert(app);
    assert(loc_id >= 0);
    assert(option >= 1);
    assert(option <= 5);
    if( !app->net || app->net->state != TORIRS_NET_GAME || app->target_select.obj.active )
        return 0;
    TORIRS_REPORT(
        "quest-driver: drive.op bypass pending loc=%d at %d,%d option=%d\n", loc_id, abs_x, abs_z,
        option);
    APP_NET_SEND(
        app,
        net_out_oploc(
            app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), option, abs_x, abs_z, loc_id));
    return 1;
}

/*
 * The backpack half of the same bypass (verbs-pointer, ARCHITECT.md S1: this
 * function sits inside "the app_plugin_world_op case" of this shared file).
 *
 * app_plugin_click_node above fabricates a UI_MINIMENU_PICK_UI pick, and
 * app_minimenu_run_option's UI case sends IF_BUTTON -- which on rev-239's
 * backpack is the shift-click-drop script, not the item's op. The OPHELD
 * ladder lives behind app_minimenu_inv_action, and the ONLY branch that
 * reaches it is the UI_MINIMENU_PICK_INV_SLOT one, so this builds that pick
 * instead: id = the inv node's component id, secondary = the cell index,
 * tertiary = the obj in it, quaternary = its stack count, exactly as
 * pick_inv_slot does (src/game/rs_minimenu_build.c).
 *
 * `option` 1..5 is OPHELD1..5, 0 is Examine (OPHELD6) -- the same "<= 0 means
 * examine" reading app_plugin_world_op gives -- and a NEGATIVE option arms
 * the held-item selection (OPHELDT_START, the "Use" row), which is use_on's
 * phase 1 and carries no op number.
 *
 * Returns 0 when the component is not in the tree, the option is not one of
 * those, or the dispatcher would refuse the pick -- and `*out_reason` names
 * which, always, on every 0.
 *
 * THAT LAST CASE USED TO BE INVISIBLE, and it cost a quest apiece.
 * app_minimenu_run_option validates the pick itself (app_minimenu_ui_pick_live
 * rejects a cell whose node or ancestor is display-hidden, which is the
 * ordinary state while the container's tab is not the shown one) and then
 * simply RETURNS: no packet, no cross, no message, nothing written anywhere.
 * This function reported that as the same 1 a real dispatch gets, so the quest
 * driver's held-item op answered `timeout ... -> 1 left` with no chat line and
 * no effect, and two quests (Making History's Castle Wars dig, Roving Elves'
 * consecration seed) were filed BLOCKED against content for a press that never
 * left this process -- content's own `~displaymessage(^dm_default)` fallback
 * was missing for the same reason every other effect was.  So the pick is
 * asked BEFORE it is run (app_minimenu_pick_refusal), and a refusal is
 * returned as a sentence instead of being run into the ground.
 *
 * The caller's own completion check still separates "dispatched" from
 * "happened": see quest_driver/pointer.lua, which waits for the effect rather
 * than trusting this 1.
 */
int
app_plugin_inv_op(
    struct App* app,
    int component_id,
    int slot,
    int obj_id,
    int count,
    int option,
    int subop,
    char const** out_reason)
{
    /* OPHELD1..5 in op order. app_minimenu_inv_action switches on these five
     * ids; there is no exported opheld_action_for_slot to borrow, and the
     * ladder is five entries rather than a formula. */
    static int const opheld[5] = {
        REVCONFIG_MINIMENU_OPHELD1,
        REVCONFIG_MINIMENU_OPHELD2,
        REVCONFIG_MINIMENU_OPHELD3,
        REVCONFIG_MINIMENU_OPHELD4,
        REVCONFIG_MINIMENU_OPHELD5,
    };
    struct UIMinimenu scratch;
    struct UIMinimenu saved;
    struct UIMinimenuPick pick;
    int action;
    int action_index;

    assert(app);
    assert(out_reason);
    assert(subop >= 0);
    *out_reason = NULL;
    if( option > 5 )
    {
        *out_reason = "option above 5 is not an OPHELD row";
        return 0;
    }
    /* A submenu entry belongs to an ObjType op (OPHELD1..5), never to Use
     * (option < 0) or Examine (0). */
    if( subop > 0 && option < 1 )
    {
        *out_reason = "a submenu entry needs an OPHELD op 1..5";
        return 0;
    }
    if( UITree_FindByComponentId(app->tree, component_id) < 0 )
    {
        *out_reason = "that component id is not in the interface tree";
        return 0;
    }
    memset(&pick, 0, sizeof(pick));
    pick.kind = UI_MINIMENU_PICK_INV_SLOT;
    pick.id = component_id;
    pick.secondary_id = slot;
    pick.tertiary_id = obj_id;
    pick.quaternary_id = count;
    if( option < 0 )
    {
        action = REVCONFIG_MINIMENU_OPHELDT_START;
        action_index = 0;
    }
    else if( option == 0 )
    {
        action = REVCONFIG_MINIMENU_OPHELD6;
        action_index = 0;
    }
    else
    {
        action = opheld[option - 1];
        action_index = option - 1;
    }
    TORIRS_REPORT(
        "quest-driver: inv op bypass com=%d slot=%d obj=%d count=%d option=%d subop=%d action=%d\n",
        component_id,
        slot,
        obj_id,
        count,
        option,
        subop,
        action);

    /* Ask before pressing. app_minimenu_run_option's own call is the one that
     * counts, but it answers only by doing nothing; this reads the same
     * verdict out loud while there is still a caller to tell. */
    *out_reason = app_minimenu_pick_refusal(app, &pick);
    if( *out_reason )
        return 0;

    UIMinimenu_Reset(&scratch);
    scratch.font_id = app->world_views.frame->minimenu->font_id;
    if( !UIMinimenu_AddOption(&scratch, "", action, action_index, pick) )
    {
        *out_reason = "the scratch minimenu would not take the row";
        return 0;
    }
    scratch.options[scratch.option_count - 1].subop = subop;
    saved = *app->world_views.frame->minimenu;
    *app->world_views.frame->minimenu = scratch;
    app_minimenu_run_option(app, 0, 0, 0);
    *app->world_views.frame->minimenu = saved;
    return 1;
}

/*
 * player.walk_to's engine seam: app_try_move on an ABSOLUTE tile, converted
 * scene-local against the world's own base
 * (docs/QUEST_DRIVER_PLAN.md S5.2). A 0 return is a refusal with no route
 * registered -- app_try_move's own contract, passed straight through.
 */
int
app_plugin_world_walk_to(struct App* app, int abs_x, int abs_z)
{
    assert(app);
    if( !app->world )
        return 0;
    return app_try_move(
        app,
        abs_x - app->world->_base_tile_x,
        abs_z - app->world->_base_tile_z,
        0,
        0,
        0,
        0,
        ToriRS_FrameModifiersCtrl(&app->frame_modifiers));
}

/*
 * player.walk_near's engine seam: app_try_move_npc / app_try_move_loc WITHOUT
 * sending any op packet -- the approach-routing half of the NPC/SCENERY
 * dispatch cases in app_minimenu_run_option, called standalone. The caller
 * (script/plugins/quest_driver/pointer.lua) re-issues this only when the
 * target's tile has changed, because app_try_move_npc/_loc already do the
 * approach-tile routing and repeating an unchanged request only adds packets.
 */
int
app_plugin_world_walk_near(struct App* app, enum DrivePickKind kind, int element_id)
{
    assert(app);
    if( !app->world )
        return 0;
    switch( kind )
    {
    case DRIVE_PICK_NPC:
    {
        struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, element_id, NULL);
        if( !npc || npc->server_slot < 0 )
            return 0;
        return app_try_move_npc(app, npc, ToriRS_FrameModifiersCtrl(&app->frame_modifiers));
    }
    case DRIVE_PICK_LOC:
    {
        struct WorldEntity_Scenery* loc = World_SceneryGetByElementId(app->world, element_id);
        if( !loc )
            return 0;
        return app_try_move_loc(
            app, element_id, loc->grid_position.x, loc->grid_position.z, ToriRS_FrameModifiersCtrl(&app->frame_modifiers));
    }
    default:
        return 0;
    }
}

static void
app_plugin_text_input(void* user, int on)
{
    struct App* app = (struct App*)user;

    assert(app);
    /* Raised as a pending flag rather than acted on: the App has no platform,
     * and the shell already drains statements like this beside the window-mode
     * change. @see App_TakeTextInputChange. */
    TextInputGate_SetPluginRequest(&app->text_input_gate, on);
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
        UIChatLine_SetInputActive(&app->chat_line, 1);
        /* Even when the line was already focused: the keyboard request is
         * edge-triggered, and the player may have dismissed the keyboard with
         * the focus still held -- a re-tap must bring it back. Forgetting the
         * last pushed state makes the next take push the answer again. */
        TextInputGate_Invalidate(&app->text_input_gate);
        RedrawRequest_Raise(&app->redraw_request);
        return;
    }
    if( !UIChatLine_InputActive(&app->chat_line) )
        return;
    UIChatLine_SetInputActive(&app->chat_line, 0);
    /* The keyboard follows by itself: app_wants_text_input reads this flag,
     * and the shell drains the change. */
    RedrawRequest_Raise(&app->redraw_request);
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
 * The value is the host's TORIRS_PLUGIN_ENGINE_DRAW_* mode, carried verbatim
 * (plugin/torirs_plugin_host.h). */
/* The stage works in surfaces; the plugin's draw window is a plugin-facing
 * enum. One translation, here, so nothing that pushes has to know they are the
 * same three values in the same order -- which they need not stay. */
static enum OverlaySurface
app_plugin_draw_surface(int canvas)
{
    switch( canvas )
    {
    case TORIRS_PLUGIN_ENGINE_DRAW_CANVAS: return OVERLAY_SURFACE_CANVAS;
    case TORIRS_PLUGIN_ENGINE_DRAW_PANEL: return OVERLAY_SURFACE_PANEL;
    default: return OVERLAY_SURFACE_WORLD;
    }
}

static void
app_plugin_draw_select_canvas(void* user, int canvas)
{
    struct App* app = (struct App*)user;
    assert(app);
    OverlayStage_Open(&app->overlays, app_plugin_draw_surface(canvas));
}

/* ------------------------------------------------------- chrome + the player */

static int
app_plugin_mouse_pos(void* user, int* out_x, int* out_y)
{
    struct App* app = (struct App*)user;

    assert(app);
    /* The same point every click path reads, latched once per frame from the
     * input drain (App_RunOnce). Before the first frame it is 0,0, which is a
     * legal position -- so the answer is the position, not a validity flag.
     * A plugin's pointer is the PERSON's: the presented view's (the watcher's
     * while a script is attached -- the runner camera split). */
    if( out_x )
        *out_x = App_PresentedView(app)->world_mouse_x;
    if( out_y )
        *out_y = App_PresentedView(app)->world_mouse_y;
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

    if( !PluginFrameBinding_SlotNodesCurrent(&app->plugin_frame_binding, app->tree->generation) )
    {
        for( int i = 0; i < TORIRS_HOST_SURFACE_PLACEABLE_COUNT; i++ )
            PluginFrameBinding_SetSlotNode(
                &app->plugin_frame_binding, i, UITree_FrameSlotNode(app->tree, i));
        PluginFrameBinding_SlotNodesResolved(&app->plugin_frame_binding, app->tree->generation);
    }
    return PluginFrameBinding_SlotNode(&app->plugin_frame_binding, slot);
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

/*
 * `<slot>:<member>` -- ONE member of a frame slot, spelled the way a profile
 * already spells it in `match=slot(chat_buttons, report)`.
 *
 * find_all has answered a slot's members since the frame binder learned to
 * number them, but find and watch_state could not name one, and a watch is
 * the only way to follow an element's geometry. That gap is why a portable
 * caller could reach the four 2004 chat filters and the fourteen 2004 tab
 * stones only through an authored role NAME, which two of the four filters
 * do not have: `slot(chat_buttons, private)` is declared by nobody, because
 * nothing had a reason to name it until now.
 *
 * The member is written either way the role grammar writes it -- a name for
 * the slots whose members have names, a number for the rest -- so
 * `chat_buttons:private` and `chat_buttons:1` are the same element, and
 * `sidebar:3` is the tab-3 mount on whichever lane is up.
 */
static int32_t
app_plugin_slot_member_node(struct App* app, char const* role)
{
    char slot_name[TORIRS_UI_NAME_MAX];
    char const* colon;
    size_t length;
    int slot;
    int member;

    assert(app);
    assert(app->tree);
    assert(role);

    colon = strchr(role, ':');
    if( !colon || colon == role || colon[1] == '\0' )
        return -1;
    length = (size_t)(colon - role);
    if( length >= sizeof(slot_name) )
        return -1;
    memcpy(slot_name, role, length);
    slot_name[length] = '\0';
    slot = UITree_RoleSlotFromName(slot_name);
    if( slot < 0 || slot >= TORIRS_HOST_SURFACE_PLACEABLE_COUNT )
        return -1;
    member = UITree_RoleSlotMemberFromName(slot, colon + 1);
    if( member < 0 || member >= UITREE_FRAME_SLOT_NODES_MAX )
        return -1;
    UITree_FrameBind(app->tree);
    return UITree_FrameSlotMemberNode(app->tree, slot, member);
}

/* The node a semantic role resolves to for the widget API's find: a frame
 * slot's bound node, one member of one, or the profile role's. CANVAS is a
 * rectangle and not a node, so it has no answer here. */
static int32_t
app_plugin_role_node(struct App* app, char const* role)
{
    int slot;

    assert(app);
    assert(role);
    if( !app->tree )
        return -1;

    slot = app_plugin_role_slot(role);
    /*
     * The sidebar's container is where its tab mounts actually hang, and that
     * is a question about the TREE rather than about the lane.
     *
     * Both callers used to ask it as `App_UiLogic(app) == APP_UI_LOGIC_CS2`,
     * which is the one shape this whole layer exists to keep out of plugins --
     * and it was in the bridge, where it is worse, because every plugin
     * inherits it without being able to see it. What the branch was really
     * standing in for is that on a cache gameframe the block a profile names
     * `sidebar` is the side-modal region, while the fourteen numbered tab
     * mounts hang together under the container a plugin means when it says
     * "the sidebar".
     *
     * Their common parent is that container, it is derived from the frame
     * binder's own numbering rather than from a lane name, and where the frame
     * can answer it the answer is right on every lane. Where it cannot -- the
     * mounts disagree about their parent, or the frame numbered none -- the
     * declared block is still the best available answer and nothing changes.
     */
    if( slot == TORIRS_HOST_SURFACE_SIDEBAR )
    {
        int32_t const group = UITree_FrameSlotGroupNode(app->tree, UITREE_FRAME_SLOT_SIDEBAR);
        if( group >= 0 )
            return group;
    }
    if( slot >= 0 && slot < TORIRS_HOST_SURFACE_PLACEABLE_COUNT )
        return app_plugin_slot_node_cached(app, slot);
    if( slot >= 0 )
        return -1;

    /* A profile role first: the name a profile declared wins over the
     * positional spelling, the way a declaration wins over a derivation
     * everywhere else in this table. */
    {
        int32_t const node = UITree_RoleNodeByName(app->tree, &app->ui_roles, role);
        if( node >= 0 )
            return node;
    }
    return app_plugin_slot_member_node(app, role);
}

_Static_assert((int)TORIRS_WIDGET_RELATION_NATIVE==(int)UITREE_WIDGET_RELATION_NATIVE &&
               (int)TORIRS_WIDGET_RELATION_OVER==(int)UITREE_WIDGET_RELATION_OVER &&
               (int)TORIRS_WIDGET_RELATION_BEHIND==(int)UITREE_WIDGET_RELATION_BEHIND &&
               (int)TORIRS_WIDGET_RELATION_REPLACE==(int)UITREE_WIDGET_RELATION_REPLACE,
               "public widget relations must match the tree's anchor relations");
_Static_assert(TORIRS_WIDGET_OP_LABEL_MAX==UITREE_MENU_OPTION_LEN,
    "public owned-control label capacity must match native menu option storage");
/* Porcelain is a plugin-side library and cannot see uitree_minimenu.h, so it
 * mirrors the one pick kind it reads. This is the translation unit that sees
 * both, and the only thing that keeps the mirror honest. */
_Static_assert((int)PORCELAIN_MENU_PICK_INV_SLOT==(int)UI_MINIMENU_PICK_INV_SLOT,
    "Porcelain's mirrored container-cell pick kind must match the tree's");
/* A pure table of constants, pulled in here for the one pin below: this is
 * the translation unit that sees the chrome's ceiling and the panel's. */
#include "ui/torirs_chrome_metrics.h"
/* TWO ceilings on the same well, and only one of them was pinned. The host
 * clamps a CUSTOM row to TORIRS_PANEL_CUSTOM_HEIGHT_MAX and the chrome that
 * presents it clips at TORIRS_CHROME_M_CUSTOM_H_MAX; a well is cut off by
 * whichever is LOWER, and the lower one leaves no trace at all -- the row is
 * the height the plugin asked for and the picture is short. The porcelain
 * test pins the host's from the plugin side; this is the translation unit
 * that sees the chrome's too. */
_Static_assert(TORIRS_CHROME_M_CUSTOM_H_MAX==TORIRS_PANEL_CUSTOM_HEIGHT_MAX,
    "the panel's custom-well ceiling and the chrome's must be raised together");

struct ToriRS_WidgetRef
app_widget_ref(struct UITree const* tree, int32_t index)
{
    struct UITreeNodeRef ref = UITree_RefAt(tree, index);
    if( !ref.incarnation ) return (struct ToriRS_WidgetRef){0};
    return (struct ToriRS_WidgetRef){{ref.tree_instance, (uint64_t)ref.index + 1, ref.incarnation}};
}

static void app_widget_actions(struct App* app,int32_t node,struct UIMinimenu* menu)
{
    struct RS_MinimenuBuildCtx ctx={.tree=app->tree,.ui_host=&app->ui_host,
        .provider=app->provider,.assets=app->assets,.invs=&app->invs,
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
    PA_INC(getenv_calls);
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

static int app_plugin_tab_active(void* user);

/*
 * The ids the facets below are spelled with, resolved once.
 *
 * Every one of them is a NAME on the way in -- a `[varbit:…]` row, a
 * `[varp:…]` row, a role -- and a name costs a walk of the ref table with a
 * strcmp per row, or an intern lookup. The facets run per watched element per
 * layout fence, which is a hundred lookups a frame for four numbers that
 * change when a profile is loaded and never again.
 *
 * Keyed the way the frame binder keys its own interned role ids: on the SIZE
 * of each table behind it. A revconfig row or a role can only appear by the
 * table growing, so a count that has not moved is a table that has not gained
 * the thing that was missing.
 */
static void
app_plugin_facet_ids(struct App* app)
{
    struct PluginFacetIds ids;

    assert(app);
    if( PluginFrameBinding_FacetIdsCurrent(
            &app->plugin_frame_binding, app->revconfig_refs.count, app->ui_roles.count) )
        return;

    /* The stone lookup reads every entry of the module's table, so the table
     * and the frame's member count must be the same size. */
    _Static_assert(
        (int)(sizeof(ids.role_sidetab) / sizeof(ids.role_sidetab[0])) == UITREE_FRAME_SLOT_NODES_MAX,
        "the sidetab role cache covers every member a frame slot can have");
    memset(&ids, 0, sizeof(ids));
    for( int tab = 0; tab < UITREE_FRAME_SLOT_NODES_MAX; tab++ )
    {
        char role[UITREE_ROLE_NAME_MAX];
        snprintf(role, sizeof(role), "sidetab_%d", tab);
        ids.role_sidetab[tab] = UITree_RoleFind(&app->ui_roles, role);
    }
    ids.varbit_sidebar_flash =
        RevConfigRefs_Get(&app->revconfig_refs, "varbit", "varb3756_sidebar_flash_tab");
    ids.varbit_cutscene =
        RevConfigRefs_Get(&app->revconfig_refs, "varbit", "varb542_cutscene_status");
    ids.varp_run_mode =
        RevConfigRefs_Get(&app->revconfig_refs, "varp", "varp173_run_mode");
    ids.varp_special_armed =
        RevConfigRefs_Get(&app->revconfig_refs, "varp", "varp301_special_attack_armed");
    ids.role_orb_run = UITree_RoleFind(&app->ui_roles, "orb_run");
    ids.role_orb_spec = UITree_RoleFind(&app->ui_roles, "orb_spec");
    PluginFrameBinding_StoreFacetIds(
        &app->plugin_frame_binding, app->revconfig_refs.count, app->ui_roles.count, &ids);
}

/*
 * Which sidebar tab this node IS, or -1.
 *
 * The two spellings of one thing, and neither is a fallback for the other: a
 * 2004 frame's tab is a builtin carrying its number in its own union, and a
 * cache gameframe's `sideN` is a plain layer the frame binder stamped with a
 * slot tag and a member number. UITree_FrameSlotIndex already reads both --
 * this adds only the question it deliberately does not answer, which is
 * whether the node is a tab at all. @see frame_node_is_slot.
 */
/*
 * Which node each tab STONE is, resolved once per publication.
 *
 * Asking the role table per element would be a memo hit per tab per element,
 * which is a hundred elements' worth of lookups for an answer that changes
 * when the tree does. The stones move only when the tree is rebuilt or a
 * script recycles an id, which is exactly what the role memo itself keys on,
 * so this caches on the same two counters.
 */
static void
app_plugin_facet_sidetab_nodes(struct App* app)
{
    struct PluginFrameBinding* binding;

    assert(app);
    assert(app->tree);
    binding = &app->plugin_frame_binding;
    if( PluginFrameBinding_SidetabNodesCurrent(
            binding, app->tree->generation, app->tree->id_generation) )
        return;
    PluginFrameBinding_BeginSidetabNodes(binding, app->tree->generation, app->tree->id_generation);
    for( int tab = 0; tab < UITREE_FRAME_SLOT_NODES_MAX; tab++ )
    {
        uint16_t const role = PluginFrameBinding_FacetIds(binding)->role_sidetab[tab];
        PluginFrameBinding_SetSidetabNode(
            binding, tab, role ? UITree_RoleNode(app->tree, &app->ui_roles, role) : -1);
    }
}

/* The stone, by node identity. -1 when this node is not one. */
static int
app_plugin_node_stone_tabno(struct App* app, int32_t idx)
{
    assert(app);
    app_plugin_facet_sidetab_nodes(app);
    return PluginFrameBinding_SidetabOf(&app->plugin_frame_binding, idx);
}

static int
app_plugin_node_tabno(struct UITreeComponent const* c)
{
    assert(c);
    if( c->type == UIELEM_BUILTIN_SIDEBAR )
        return c->u.sidebar.componentno >= 0 ? c->u.sidebar.tabno : -1;
    if( c->slot_tag != UITREE_SLOT_SIDE_MODAL || !c->frame_member_plus1 )
        return -1;
    return (int)c->frame_member_plus1 - 1;
}

/*
 * The sidebar tab the game has FLAGGED to flash, or -1.
 *
 * Two sources and they are not alternatives: a dat1 lane flags the tab in the
 * TUT_FLASH packet, which lands in `slots.flash_tab`; a cache lane flags it in
 * a varbit its own `toplevel_flashicon` reads, which the profile names. Both
 * are folded here so the two things that need the answer -- the FLASHING facet
 * on a tab's element and `cache.tab_flash_hidden` for a frame that has
 * replaced the stones -- cannot drift apart, and so neither of them names a
 * lane.
 *
 * The off-by-one is the cache script's own: it computes the tab from
 * `varbit - 1`, which is what makes 0 mean "nothing is flashing" rather than
 * "tab 0 is". Subtracting it here is the same arithmetic on the same varbit,
 * not a guess about the encoding.
 *
 * A lane whose profile declares no such varbit has no cache-side flag, which
 * is a fact about the revision and not a gap in the answer.
 */
static int
app_plugin_tab_flagged_flash(struct App* app)
{
    assert(app);
    app_plugin_facet_ids(app);
    if( app->slots.flash_tab >= 0 )
        return app->slots.flash_tab;
    if( PluginFrameBinding_FacetIds(&app->plugin_frame_binding)->varbit_sidebar_flash >= 0 )
        return VarPManager_GetVarbit(
                   &app->varps,
                   PluginFrameBinding_FacetIds(&app->plugin_frame_binding)->varbit_sidebar_flash) -
               1;
    return -1;
}

/*
 * Lane-derived facets for one node: the single place that answers "what does
 * the GAME say about this thing" for a plugin that dresses it.
 *
 * ONE function, and no lane name anywhere in it. What it keys on is what the
 * profile declared (a varbit row exists or it does not) and what the node
 * itself carries (its builtin type, its slot stamp, the role it resolves to).
 * A lane that cannot derive a facet reads zero for it -- which is why the two
 * spellings of "is this tab hidden" live behind RS_UISlots_TabGiven and the
 * selection behind app_plugin_tab_active rather than being restated here: each
 * of those already holds both halves, and a second copy is how the two drift.
 *
 * Cost: no tree walk, no strcmp, no snprintf per element beyond the one
 * RS_UISlots_TabGiven does for a node that is actually a tab. The ids are
 * app_plugin_facet_ids' single resolution; the orb roles memoise on the tree's
 * own generations; the minimap permissions are three host requests, the same
 * three the engine's own minimap and compass ask.
 */
static uint32_t
app_plugin_widget_facets(struct App* app, int32_t idx)
{
    struct PluginFacetIds const* ids;
    struct UITreeComponent const* c;
    uint32_t facets = 0;
    int tabno;

    assert(app);
    assert(idx >= 0);
    assert(app->tree);
    assert((uint32_t)idx < app->tree->component_count);

    app_plugin_facet_ids(app);
    ids = PluginFrameBinding_FacetIds(&app->plugin_frame_binding);
    c = &app->tree->components[idx];

    /* A fact about the SCREEN, so it is reported on everything: a plugin's
     * own decoration has to fold away with the HUD it is drawn over. A lane
     * whose profile declares no cutscene varbit has no cutscene. */
    if( ids->varbit_cutscene >= 0 &&
        VarPManager_GetVarbit(&app->varps, ids->varbit_cutscene) )
        facets |= TORIRS_WIDGET_FACET_HIDDEN_BY_CUTSCENE;

    /* The panel a tab opens, or the stone that opens it: a plugin dressing
     * either one is asking about the same tab. */
    tabno = app_plugin_node_tabno(c);
    if( tabno < 0 )
        tabno = app_plugin_node_stone_tabno(app, idx);
    if( tabno >= 0 )
    {
        if( RS_UISlots_TabGiven(app, tabno) )
            facets |= TORIRS_WIDGET_FACET_GIVEN;
        if( app_plugin_tab_active(app) == tabno )
            facets |= TORIRS_WIDGET_FACET_SELECTED;
        /* The flash FLAG, not the blink. @see app_plugin_tab_flagged_flash. */
        if( app_plugin_tab_flagged_flash(app) == tabno )
            facets |= TORIRS_WIDGET_FACET_FLASHING;
    }

    if( c->type == UIELEM_BUILTIN_MINIMAP || c->type == UIELEM_BUILTIN_COMPASS )
    {
        /* Through the host and not RS_MinimapPermissions, deliberately: the
         * engine's minimap and compass gate their own paint on exactly these
         * requests, so the facet and the pixels cannot disagree. */
        struct UITreeHostRequest req;
        memset(&req, 0, sizeof(req));
        req.kind = c->type == UIELEM_BUILTIN_MINIMAP ? UITREE_HOST_GET_MINIMAP_HIDDEN
                                                     : UITREE_HOST_GET_COMPASS_HIDDEN;
        if( !UITree_Host(&app->ui_host, &req) )
            facets |= TORIRS_WIDGET_FACET_DRAWN;
        memset(&req, 0, sizeof(req));
        req.kind = UITREE_HOST_GET_COMPASS_HIDDEN;
        if( !UITree_Host(&app->ui_host, &req) )
            facets |= TORIRS_WIDGET_FACET_ORIENTED;
        memset(&req, 0, sizeof(req));
        req.kind = UITREE_HOST_GET_MINIMAP_WALK;
        if( UITree_Host(&app->ui_host, &req) )
            facets |= TORIRS_WIDGET_FACET_WALKABLE;
    }

    /* An orb's own toggle, and only for the orb it belongs to: the run orb
     * does not light up because the special is armed. */
    if( ids->varp_run_mode >= 0 && ids->role_orb_run &&
        UITree_RoleNode(app->tree, &app->ui_roles, ids->role_orb_run) == idx &&
        VarPManager_GetVarp(&app->varps, ids->varp_run_mode) )
        facets |= TORIRS_WIDGET_FACET_ACTIVE;
    if( ids->varp_special_armed >= 0 && ids->role_orb_spec &&
        UITree_RoleNode(app->tree, &app->ui_roles, ids->role_orb_spec) == idx &&
        VarPManager_GetVarp(&app->varps, ids->varp_special_armed) )
        facets |= TORIRS_WIDGET_FACET_ACTIVE;

    return facets;
}

/* ------------------------------------------------------------------------ */
/* Engine news for on_interface / on_inventory_changed / on_var_changed       */
/* ------------------------------------------------------------------------ */

void
app_plugin_mount_changed(void* user, int group_id, int open)
{
    struct App* app = user;
    assert(app);
    /* The host queues only for a running subscriber, so a mount costs a
     * plugin-less client this pointer test and nothing else. */
    if( app->plugins && group_id > 0 )
        PluginHost_InterfaceChanged(app->plugins, group_id, open != 0);
}

void
app_plugin_inventory_changed(struct App* app, int container_id)
{
    assert(app);
    if( app->plugins && container_id >= 0 )
        PluginHost_InventoryChanged(app->plugins, container_id);
}

void
app_plugin_loc_changed(
    struct App* app,
    struct World const* world,
    int scene_x,
    int scene_z,
    int level,
    int shape,
    int angle,
    int loc_id,
    int old_loc_id,
    int old_shape,
    int old_angle)
{
    struct ToriRS_LocChangedEvent ev;

    assert(app);
    assert(world);
    if( !app->plugins || world != app->world )
        return;
    ev.tile_x = world->_base_tile_x + scene_x;
    ev.tile_z = world->_base_tile_z + scene_z;
    ev.level = level;
    ev.shape = shape;
    ev.angle = angle;
    ev.loc_id = loc_id;
    ev.old_loc_id = old_loc_id;
    ev.old_shape = old_loc_id >= 0 ? old_shape : -1;
    ev.old_angle = old_loc_id >= 0 ? old_angle : 0;
    PluginHost_LocChanged(app->plugins, &ev);
}

void
App_PluginMapAnim(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int spotanim,
    int height,
    int delay)
{
    struct ToriRS_MapAnimEvent ev;

    assert(app);
    assert(app->world);
    if( !app->plugins )
        return;
    ev.tile_x = app->world->_base_tile_x + scene_x;
    ev.tile_z = app->world->_base_tile_z + scene_z;
    ev.level = level;
    ev.spotanim = spotanim;
    ev.height = height;
    ev.delay = delay;
    ev.cycle = (int)app->world->cycle;
    PluginHost_MapAnim(app->plugins, &ev);
}

static int
app_plugin_varp_values(void* user, int32_t const** out)
{
    struct App* app = user;
    assert(app);
    assert(out);
    *out = app->varps.var;
    return app->varps.var ? app->varps.varp_count : 0;
}

/* ------------------------------------------------------------------------ */
/* Widget detail, RuneLite's child lists, listeners, owned items              */
/* ------------------------------------------------------------------------ */

/* RuneLite's component id for a node: a cc_create child is named by the
 * component it hangs from. */
static int
app_widget_rl_id(struct UITree const* tree, int32_t idx)
{
    struct UITreeComponent const* c = &tree->components[idx];
    if( c->dynamic && c->parent >= 0 )
        return tree->components[c->parent].component_id;
    return c->component_id;
}

/* net.runelite.api.widgets.WidgetType for a node, -1 for a client-built one. */
static int
app_widget_rl_type(struct UITreeComponent const* c)
{
    switch( c->type )
    {
    case UIELEM_RS_LAYER: return 0;
    case UIELEM_RS_INV: return 2;
    case UIELEM_RS_RECT: return 3;
    case UIELEM_RS_TEXT: return 4;
    case UIELEM_RS_GRAPHIC: return 5;
    case UIELEM_CC_OBJ: return 5;
    case UIELEM_RS_MODEL: return 6;
    case UIELEM_RS_INV_TEXT: return 7;
    case UIELEM_RS_LINE: return 9;
    default: return -1;
    }
}

static void
app_widget_detail(struct App* app, int32_t idx, struct ToriRS_WidgetDetail* d)
{
    struct UITree* tree = app->tree;
    struct UITreeComponent const* c = &tree->components[idx];
    uint32_t const declared = d->struct_size;

    memset(d, 0, sizeof(*d));
    d->struct_size = declared;
    d->id = app_widget_rl_id(tree, idx);
    d->index = c->dynamic ? c->dynamic_child_index : -1;
    d->type = app_widget_rl_type(c);
    d->content_type = c->behavior.client_code;
    d->item_id = c->item_id >= 0 ? c->item_id : -1;
    d->item_quantity = c->item_id >= 0 ? c->item_count : 0;
    d->sprite_id = -1;
    d->model_id = -1;
    d->animation_id = -1;
    d->font_id = -1;
    d->text_color = c->colour;
    if( c->type == UIELEM_RS_GRAPHIC && c->item_id < 0 && c->u.rs_graphic.scene_id > 0 )
        d->sprite_id = UITreeSceneBridge_SpriteCacheIdForScene(&app->bridge, c->u.rs_graphic.scene_id);
    else if( c->type == UIELEM_BUILTIN_SPRITE && c->u.sprite.scene_id > 0 )
        d->sprite_id = UITreeSceneBridge_SpriteCacheIdForScene(&app->bridge, c->u.sprite.scene_id);
    if( c->type == UIELEM_RS_MODEL )
    {
        d->model_id = c->u.rs_model.gamecache_model_id;
        d->animation_id = c->u.rs_model.anim_seq_id;
        d->model_type = d->model_id >= 0 ? 1 : 0;
    }
    /* What IF_SETNPCHEAD / IF_SETPLAYERHEAD / IF_SETOBJECT / IF_SETMODEL bound,
     * as RuneLite's WidgetModelType names it. */
    {
        int const head = UIIfModels_HeadIndex(&app->if_models, c->component_id);
        if( head >= 0 )
        {
            struct UIIfHead const* h = &app->if_models.heads[head];
            if( h->kind == UI_IFHEAD_NPC ) { d->model_type = 2; d->model_id = h->npc_id; }
            else if( h->kind == UI_IFHEAD_PLAYER ) d->model_type = 3;
            else if( h->kind == UI_IFHEAD_OBJ ) { d->model_type = 4; d->model_id = h->npc_id; }
            else if( h->kind == UI_IFHEAD_MODEL ) { d->model_type = 1; d->model_id = h->npc_id; }
        }
    }
    if( c->type == UIELEM_RS_TEXT )
    {
        d->text_color = c->u.rs_text.color;
        /* A cache font's scene id IS its cache id (UITreeSceneBridge_EnsureFont);
         * the chrome's own faces live in the reserved band above it. */
        if( c->u.rs_text.font_id >= 0 && c->u.rs_text.font_id < 0x40000000 )
            d->font_id = c->u.rs_text.font_id;
    }
    else if( c->type == UIELEM_RS_RECT )
        d->text_color = c->u.rs_rect.color;
    {
        char const* text = c->type == UIELEM_RS_TEXT ? c->u.rs_text.text : NULL;
        if( !text )
            text = c->data_text;
        snprintf(d->text, sizeof(d->text), "%s", text ? text : "");
    }
    {
        struct UITreeMenuOptions const* opts = UITree_MenuOptions(c);
        snprintf(d->name, sizeof(d->name), "%s", opts->option);
        for( int i = 0; i < TORIRS_WIDGET_DETAIL_ACTIONS && i < UITREE_MENU_OPTION_SLOTS; i++ )
        {
            snprintf(d->actions[i], sizeof(d->actions[i]), "%s", opts->ops[i]);
            if( opts->ops[i][0] )
                d->action_count = i + 1;
        }
    }
    d->hidden = UITree_NodeOrAncestorDisplayHidden(tree, idx) ||
                !UITree_NodeNativeVisible(tree, &app->ui_host, idx, app->interact.published_hover_com_id);
    d->self_hidden = c->behavior.hide != 0;
    UITree_EnsureLayoutFor(tree, idx);
    {
        int x = 0, y = 0, w = 0, h = 0;
        UITree_NodeDrawnBounds(tree, idx, &x, &y, &w, &h);
        d->x = x; d->y = y; d->width = w; d->height = h;
    }
    d->original_x = c->position.x;
    d->original_y = c->position.y;
    d->original_width = c->position.width;
    d->original_height = c->position.height;
    d->relative_x = c->position.abs_x;
    d->relative_y = c->position.abs_y;
    if( c->parent >= 0 )
    {
        d->relative_x -= tree->components[c->parent].position.abs_x;
        d->relative_y -= tree->components[c->parent].position.abs_y;
    }
    d->scroll_x = c->scroll_x;
    d->scroll_y = c->scroll_y;
    if( c->type == UIELEM_RS_LAYER )
    {
        d->scroll_width = c->u.rs_layer.scroll_width;
        d->scroll_height = c->u.rs_layer.scroll_height;
    }
    d->opacity = c->trans;
    d->parent_id = c->parent >= 0 ? app_widget_rl_id(tree, c->parent) : -1;
}

enum AppWidgetChildList
{
    APP_WIDGET_CHILDREN_DYNAMIC,
    APP_WIDGET_CHILDREN_STATIC,
    APP_WIDGET_CHILDREN_NESTED,
};

static bool
app_widget_child_in(struct UITree const* tree, int32_t parent, int32_t child, enum AppWidgetChildList which)
{
    struct UITreeComponent const* c = &tree->components[child];
    struct UITreeComponent const* p = &tree->components[parent];
    if( c->freed || c->plugin_owner )
        return false;
    if( which == APP_WIDGET_CHILDREN_DYNAMIC )
        return c->dynamic != 0;
    if( c->dynamic )
        return false;
    /* A root of ANOTHER interface hanging here is a mount: nested. */
    bool const nested = c->component_id >= 0 && p->component_id >= 0 &&
                        (c->component_id >> 16) != (p->component_id >> 16);
    return which == APP_WIDGET_CHILDREN_NESTED ? nested : !nested;
}

/* Sort scratch for the dynamic list: RuneLite answers it in child-index
 * order, and a replace-in-slot cc_create appends out of that order. A list
 * longer than this is answered in draw order. */
#define APP_WIDGET_CHILD_SORT_MAX 4096
static struct { int32_t key, node; } g_widget_child_sort[APP_WIDGET_CHILD_SORT_MAX];

static int
app_widget_child_sort_cmp(void const* a, void const* b)
{
    int32_t const ka = ((int32_t const*)a)[0], kb = ((int32_t const*)b)[0];
    return ka < kb ? -1 : ka > kb ? 1 : 0;
}

static enum ToriRS_ContractResult
app_widget_child_list(
    struct App* app,
    int32_t idx,
    enum AppWidgetChildList which,
    struct PluginWidgetRequest* r)
{
    struct UITree* tree = app->tree;
    int n = 0;
    bool sortable = which == APP_WIDGET_CHILDREN_DYNAMIC;
    for( int32_t child = tree->components[idx].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        if( !app_widget_child_in(tree, idx, child, which) )
            continue;
        if( sortable )
        {
            if( n < APP_WIDGET_CHILD_SORT_MAX )
            {
                g_widget_child_sort[n].key = tree->components[child].dynamic_child_index;
                g_widget_child_sort[n].node = child;
            }
            else
                sortable = false;
        }
        if( !sortable )
        {
            if( *r->count < r->capacity ) r->refs[*r->count] = app_widget_ref(tree, child);
        }
        ++n;
        ++*r->count;
    }
    if( sortable )
    {
        qsort(g_widget_child_sort, (size_t)n, sizeof(g_widget_child_sort[0]), app_widget_child_sort_cmp);
        for( int i = 0; i < n && (size_t)i < r->capacity; i++ )
            r->refs[i] = app_widget_ref(tree, g_widget_child_sort[i].node);
    }
    else if( which == APP_WIDGET_CHILDREN_DYNAMIC && n > APP_WIDGET_CHILD_SORT_MAX )
    {
        /* Past the scratch: the walk above filled refs in draw order from the
         * first child that did not fit; restart it whole so the order is one
         * order and not two. */
        *r->count = 0;
        for( int32_t child = tree->components[idx].first_child; child >= 0;
             child = tree->components[child].next_sibling )
        {
            if( !app_widget_child_in(tree, idx, child, which) )
                continue;
            if( *r->count < r->capacity ) r->refs[*r->count] = app_widget_ref(tree, child);
            ++*r->count;
        }
    }
    return *r->count > r->capacity ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
}

/* RuneLite's listener names, and the hook slot behind each. -1: a name this
 * client knows of and does not keep (onLoad runs at the mount). */
static struct { char const* name; int slot; } const APP_WIDGET_LISTENERS[] = {
    {"onClick", (int)offsetof(struct UITreeRuntimeHooks, on_click)},
    {"onHold", (int)offsetof(struct UITreeRuntimeHooks, on_hold)},
    {"onMouseOver", (int)offsetof(struct UITreeRuntimeHooks, on_mouse_over)},
    {"onMouseLeave", (int)offsetof(struct UITreeRuntimeHooks, on_mouse_leave)},
    {"onMouseRepeat", (int)offsetof(struct UITreeRuntimeHooks, on_mouse_repeat)},
    {"onRelease", (int)offsetof(struct UITreeRuntimeHooks, on_release)},
    {"onTargetEnter", (int)offsetof(struct UITreeRuntimeHooks, on_target_enter)},
    {"onTargetLeave", (int)offsetof(struct UITreeRuntimeHooks, on_target_leave)},
    {"onDrag", (int)offsetof(struct UITreeRuntimeHooks, on_drag)},
    {"onDragComplete", (int)offsetof(struct UITreeRuntimeHooks, on_drag_complete)},
    {"onScrollWheel", (int)offsetof(struct UITreeRuntimeHooks, on_scroll_wheel)},
    {"onKey", (int)offsetof(struct UITreeRuntimeHooks, on_key)},
    {"onOp", (int)offsetof(struct UITreeRuntimeHooks, on_op)},
    {"onTimer", (int)offsetof(struct UITreeRuntimeHooks, on_timer)},
    {"onVarTransmit", (int)offsetof(struct UITreeRuntimeHooks, on_var_transmit)},
    {"onInvTransmit", (int)offsetof(struct UITreeRuntimeHooks, on_inv_transmit)},
    {"onDialogAbort", (int)offsetof(struct UITreeRuntimeHooks, on_dialog_abort)},
    {"onLoad", -1},
};

/*
 * An inv / var transmit listener is not on the node: IF_SETONINVTRANSMIT and
 * IF_SETONVARTRANSMIT arm the CS2 host's own registries (the transmit pump
 * walks them, not the tree), so RuneLite's getOnInvTransmitListener reads
 * there. Same Object[] shape as a node hook. UNAVAILABLE when none is armed.
 */
static enum ToriRS_ContractResult
app_widget_transmit_listener(int component_id, int script_id, int const* int_args, int int_arg_count,
                             uint64_t str_arg_mask, int str_arg_count,
                             char const str_args[CS2VM_SETON_STR_ARG_MAX][CS2VM_SETON_STR_ARG_LEN],
                             struct PluginWidgetRequest* r)
{
    (void)component_id;
    if( script_id <= 0 )
        return TORIRS_CONTRACT_UNAVAILABLE;
    *r->count = (size_t)int_arg_count + 1;
    if( r->capacity )
        r->listener_args[0] = (struct ToriRS_WidgetListenerArg){.is_string = false, .int_value = script_id};
    int string_at = 0;
    for( int i = 0; i < int_arg_count; i++ )
    {
        bool const is_string = i < 64 && (str_arg_mask & (UINT64_C(1) << i)) != 0;
        if( (size_t)i + 1 < r->capacity )
        {
            struct ToriRS_WidgetListenerArg* a = &r->listener_args[i + 1];
            memset(a, 0, sizeof(*a));
            a->is_string = is_string;
            if( is_string )
                snprintf(a->string_value, sizeof(a->string_value), "%s",
                         string_at < str_arg_count ? str_args[string_at] : "");
            else
                a->int_value = int_args[i];
        }
        if( is_string )
            string_at++;
    }
    return *r->count > r->capacity ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
app_widget_listener(struct App* app, struct UITreeComponent const* c, struct PluginWidgetRequest* r)
{
    int slot = -2;
    assert(app);
    if( !c->plugin_owner && c->component_id >= 0 && strcmp(r->name, "onInvTransmit") == 0 )
    {
        for( int i = 0; i < app->host.inv_transmit_hook_count; i++ )
        {
            struct RS_CS2InvTransmitHook const* h = &app->host.inv_transmit_hooks[i];
            if( h->component_id == c->component_id && h->script_id > 0 )
                return app_widget_transmit_listener(h->component_id, h->script_id, h->int_args, h->int_arg_count,
                                                    h->str_arg_mask, h->str_arg_count, h->str_args, r);
        }
    }
    if( !c->plugin_owner && c->component_id >= 0 && strcmp(r->name, "onVarTransmit") == 0 )
    {
        for( int i = 0; i < app->host.var_transmit_hook_count; i++ )
        {
            struct RS_CS2VarTransmitHook const* h = &app->host.var_transmit_hooks[i];
            if( h->component_id == c->component_id && h->script_id > 0 )
                return app_widget_transmit_listener(h->component_id, h->script_id, h->int_args, h->int_arg_count,
                                                    h->str_arg_mask, h->str_arg_count, h->str_args, r);
        }
    }
    for( size_t i = 0; i < sizeof(APP_WIDGET_LISTENERS) / sizeof(APP_WIDGET_LISTENERS[0]); i++ )
        if( strcmp(APP_WIDGET_LISTENERS[i].name, r->name) == 0 )
        {
            slot = APP_WIDGET_LISTENERS[i].slot;
            break;
        }
    if( slot == -2 )
        return TORIRS_CONTRACT_INVALID_ARGUMENT;
    if( slot < 0 || c->plugin_owner )
        return TORIRS_CONTRACT_UNAVAILABLE;
    struct UITreeRuntimeScriptHook const* hook =
        (struct UITreeRuntimeScriptHook const*)((char const*)UITree_Hooks(c) + slot);
    if( !UITree_HookIsSet(hook) )
        return TORIRS_CONTRACT_UNAVAILABLE;
    *r->count = (size_t)hook->argc + 1;
    if( r->capacity )
    {
        r->listener_args[0] = (struct ToriRS_WidgetListenerArg){.is_string = false, .int_value = hook->script_id};
    }
    int string_at = 0;
    for( int i = 0; i < hook->argc; i++ )
    {
        bool const is_string = i < 64 && (hook->str_mask & (UINT64_C(1) << i)) != 0;
        if( (size_t)i + 1 < r->capacity )
        {
            struct ToriRS_WidgetListenerArg* a = &r->listener_args[i + 1];
            memset(a, 0, sizeof(*a));
            a->is_string = is_string;
            if( is_string )
                snprintf(a->string_value, sizeof(a->string_value), "%s",
                         string_at < hook->str_argc ? UITree_HookStr(hook, string_at) : "");
            else
                a->int_value = UITree_HookArg(hook, i);
        }
        if( is_string )
            string_at++;
    }
    return *r->count > r->capacity ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
}

/*
 * Owned item controls whose obj icon could not be baked yet: the obj is on
 * the node already, the picture binds here once its model lands. Bounded,
 * process-wide (there is one tree), and one test when empty.
 */
#define APP_WIDGET_ITEM_PENDING_MAX 128
#define APP_WIDGET_ITEM_PENDING_REQUEUE 60
#define APP_WIDGET_ITEM_PENDING_GIVE_UP 1800
struct AppWidgetItemPending
{
    struct UITreeNodeRef ref;
    int obj_id, count, pumps;
    /* 0 for an owned item control; the editing plugin for a NATIVE cell's
     * re-lay, whose icon is bound by restating that owner's edit. */
    uint64_t owner;
    /* The count rule the item draws with (item_num_mode; -1 the cell's own). */
    int num_mode;
};
static struct AppWidgetItemPending g_widget_item_pending[APP_WIDGET_ITEM_PENDING_MAX];
static int g_widget_item_pending_count;

static void
app_widget_item_queue_load(struct App* app, int obj_id, int count)
{
    (void)Assets_Want(
        app->assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, obj_id, count > 0 ? count : 1));
}

static void
app_widget_item_pending_drop(struct UITreeNodeRef ref)
{
    for( int i = 0; i < g_widget_item_pending_count; i++ )
        if( g_widget_item_pending[i].ref.index == ref.index &&
            g_widget_item_pending[i].ref.incarnation == ref.incarnation &&
            g_widget_item_pending[i].ref.tree_instance == ref.tree_instance )
        {
            g_widget_item_pending[i] = g_widget_item_pending[--g_widget_item_pending_count];
            return;
        }
}

/*
 * Owned MODEL controls whose model is not composited yet: the load is queued
 * at set_model and the scene model binds here once it is. Same bounds and
 * cadence as the item icons.
 */
struct AppWidgetModelPending
{
    struct UITreeNodeRef ref;
    int kind, id, pumps;
};
static struct AppWidgetModelPending g_widget_model_pending[APP_WIDGET_ITEM_PENDING_MAX];
static int g_widget_model_pending_count;

static void
app_widget_model_pending_drop(struct UITreeNodeRef ref)
{
    for( int i = 0; i < g_widget_model_pending_count; i++ )
        if( g_widget_model_pending[i].ref.index == ref.index &&
            g_widget_model_pending[i].ref.incarnation == ref.incarnation &&
            g_widget_model_pending[i].ref.tree_instance == ref.tree_instance )
        {
            g_widget_model_pending[i] = g_widget_model_pending[--g_widget_model_pending_count];
            return;
        }
}

static void
app_plugin_widget_models_pump(struct App* app)
{
    for( int i = g_widget_model_pending_count - 1; i >= 0; i-- )
    {
        struct AppWidgetModelPending* row = &g_widget_model_pending[i];
        int32_t const idx = UITree_ResolveRef(app->tree, row->ref);
        bool done = idx < 0;
        if( !done )
        {
            bool const requeue = ++row->pumps % APP_WIDGET_ITEM_PENDING_REQUEUE == 0;
            int const scene = App_PluginModelScene(app, row->kind, row->id, requeue);
            if( scene >= 0 )
            {
                (void)UITree_SetModelAt(app->tree, idx, scene);
                RedrawRequest_Raise(&app->redraw_request);
                done = true;
            }
            else if( row->pumps >= APP_WIDGET_ITEM_PENDING_GIVE_UP )
                done = true; /* A model that never composites: the control stays empty. */
        }
        if( done )
            g_widget_model_pending[i] = g_widget_model_pending[--g_widget_model_pending_count];
    }
}

/* set_model on an owned MODEL control. */
static enum ToriRS_ContractResult
app_widget_set_model(struct App* app, struct UITreeNodeRef ref, int32_t idx, int kind, int id)
{
    int head_kind;
    int scene;

    app_widget_model_pending_drop(ref);
    if( kind == TORIRS_WIDGET_MODEL_NONE )
    {
        (void)UITree_SetModelAt(app->tree, idx, -1);
        return TORIRS_CONTRACT_OK;
    }
    head_kind = kind == TORIRS_WIDGET_MODEL_NPC ? UI_IFHEAD_NPC
              : kind == TORIRS_WIDGET_MODEL_OBJ ? UI_IFHEAD_OBJ
              : kind == TORIRS_WIDGET_MODEL_MODEL ? UI_IFHEAD_MODEL
                                                  : UI_IFHEAD_PLAYER;
    scene = App_PluginModelScene(app, head_kind, id, true);
    (void)UITree_SetModelAt(app->tree, idx, scene);
    if( scene >= 0 )
        return TORIRS_CONTRACT_OK;
    if( g_widget_model_pending_count < APP_WIDGET_ITEM_PENDING_MAX )
        g_widget_model_pending[g_widget_model_pending_count++] =
            (struct AppWidgetModelPending){ref, head_kind, id, 0};
    return TORIRS_CONTRACT_PENDING;
}

void
app_plugin_widget_items_pump(struct App* app)
{
    assert(app);
    if( g_widget_model_pending_count > 0 && app->tree )
        app_plugin_widget_models_pump(app);
    if( g_widget_item_pending_count == 0 || !app->tree )
        return;
    for( int i = g_widget_item_pending_count - 1; i >= 0; i-- )
    {
        int32_t const idx = UITree_ResolveRef(app->tree, g_widget_item_pending[i].ref);
        bool done = idx < 0 || app->tree->components[idx].item_id != g_widget_item_pending[i].obj_id;
        if( !done )
        {
            int const scene = UITreeSceneBridge_EnsureObjIcon(
                &app->bridge, g_widget_item_pending[i].obj_id,
                g_widget_item_pending[i].count > 0 ? g_widget_item_pending[i].count : 1);
            if( scene > 0 )
            {
                if( g_widget_item_pending[i].owner )
                    (void)UITree_WidgetSetNativeObject(app->tree, g_widget_item_pending[i].ref,
                                                       g_widget_item_pending[i].owner,
                                                       g_widget_item_pending[i].obj_id,
                                                       g_widget_item_pending[i].count, scene,
                                                       g_widget_item_pending[i].num_mode);
                else
                    (void)UITree_SetObjectAt(app->tree, idx, g_widget_item_pending[i].obj_id,
                                             g_widget_item_pending[i].count, scene, 0,
                                             g_widget_item_pending[i].num_mode >= 0
                                                 ? g_widget_item_pending[i].num_mode : 0);
                RedrawRequest_Raise(&app->redraw_request);
                done = true;
            }
            else if( ++g_widget_item_pending[i].pumps >= APP_WIDGET_ITEM_PENDING_GIVE_UP )
                done = true; /* An obj whose model never comes: it stays iconless. */
            else if( g_widget_item_pending[i].pumps % APP_WIDGET_ITEM_PENDING_REQUEUE == 0 )
                app_widget_item_queue_load(app, g_widget_item_pending[i].obj_id, g_widget_item_pending[i].count);
        }
        if( done )
            g_widget_item_pending[i] = g_widget_item_pending[--g_widget_item_pending_count];
    }
}

/* RuneLite's ItemQuantityMode (0 NEVER, 1 ALWAYS, 2 STACKABLE; -1 the
 * cell's own) as the tree's item_num_mode (0 stackable-only, 1 always,
 * 2 never; -1 kept). */
static int
app_widget_num_mode(int quantity_mode)
{
    assert(quantity_mode >= -1);
    assert(quantity_mode <= 2);
    if( quantity_mode < 0 )
        return -1;
    return quantity_mode == 0 ? 2 : quantity_mode == 1 ? 1 : 0;
}

static enum ToriRS_ContractResult
app_widget_set_item(struct App* app, struct UITreeNodeRef ref, int32_t idx, int obj_id, int count, int quantity_mode)
{
    struct UITree* tree = app->tree;
    int const num_mode = app_widget_num_mode(quantity_mode);
    int const drawn_mode = num_mode >= 0 ? num_mode : 0;
    app_widget_item_pending_drop(ref);
    if( obj_id < 0 )
    {
        (void)UITree_SetObjectAt(tree, idx, -1, 0, -1, 0, 0);
        return TORIRS_CONTRACT_OK;
    }
    int const scene = UITreeSceneBridge_EnsureObjIcon(&app->bridge, obj_id, count > 0 ? count : 1);
    if( scene > 0 )
    {
        (void)UITree_SetObjectAt(tree, idx, obj_id, count, scene, 0, drawn_mode);
        return TORIRS_CONTRACT_OK;
    }
    /* The obj is the control's now (readers see it); the picture follows. */
    (void)UITree_SetObjectAt(tree, idx, obj_id, count, -1, 0, drawn_mode);
    app_widget_item_queue_load(app, obj_id, count);
    if( g_widget_item_pending_count < APP_WIDGET_ITEM_PENDING_MAX )
        g_widget_item_pending[g_widget_item_pending_count++] =
            (struct AppWidgetItemPending){ref, obj_id, count, 0, 0, drawn_mode};
    return TORIRS_CONTRACT_PENDING;
}

/* set_item on a NATIVE cell: the owner's retained re-lay (written through,
 * restored by reset), its count rule riding with it. The icon follows the
 * same way an owned control's does. */
static enum ToriRS_ContractResult
app_widget_set_native_item(struct App* app, struct UITreeNodeRef ref, uint64_t owner, int obj_id, int count,
                           int quantity_mode)
{
    struct UITree* tree = app->tree;
    int const num_mode = app_widget_num_mode(quantity_mode);
    app_widget_item_pending_drop(ref);
    if( obj_id < 0 )
        return UITree_WidgetSetNativeObject(tree, ref, owner, -1, 0, -1, -1) ? TORIRS_CONTRACT_OK
                                                                             : TORIRS_CONTRACT_BUDGET_EXCEEDED;
    int const scene = UITreeSceneBridge_EnsureObjIcon(&app->bridge, obj_id, count > 0 ? count : 1);
    if( !UITree_WidgetSetNativeObject(tree, ref, owner, obj_id, count, scene > 0 ? scene : -1, num_mode) )
        return TORIRS_CONTRACT_BUDGET_EXCEEDED;
    if( scene > 0 )
        return TORIRS_CONTRACT_OK;
    app_widget_item_queue_load(app, obj_id, count);
    if( g_widget_item_pending_count < APP_WIDGET_ITEM_PENDING_MAX )
        g_widget_item_pending[g_widget_item_pending_count++] =
            (struct AppWidgetItemPending){ref, obj_id, count, 0, owner, num_mode};
    return TORIRS_CONTRACT_PENDING;
}

static enum ToriRS_ContractResult
app_plugin_widget_request(void* user, uint64_t owner, struct PluginWidgetRequest* r)
{
    struct App* app = user;
    struct UITree* tree = app->tree;
    PA_INC(widget_request_calls);
    if( r->kind == PLUGIN_WIDGET_RESET_OWNER )
    {
        UITree_WidgetResetOwner(tree, owner);
        RedrawRequest_Raise(&app->redraw_request);
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
        PA_INC(find_all_calls);
        uint64_t const pa_fa0 = PerfAudit_Now();
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
            PA_ADD(find_all_iters, UITREE_FRAME_SLOT_NODES_MAX);
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
                PA_ADD(find_all_ns, PerfAudit_Now() - pa_fa0);
                return *r->count>r->capacity ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
            }
        }
        int32_t idx=app_plugin_role_node(app,r->name);
        PA_ADD(find_all_ns, PerfAudit_Now() - pa_fa0);
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
        if( !UITree_WidgetSetOperation(tree,ref,owner,r->registration,r->op,r->name) ) return TORIRS_CONTRACT_FAILED;
        break;
    case PLUGIN_WIDGET_VISIBLE:
        *r->flag=!UITree_NodeOrAncestorDisplayHidden(tree,idx) &&
            UITree_NodeNativeVisible(tree,&app->ui_host,idx,app->interact.published_hover_com_id);
        return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_ACTIONS:
    case PLUGIN_WIDGET_INVOKE:
    {
        /* The LANE's fences, not the plugin layer's. Both verbs name a
         * component and ask what it offers, which is the same kind of
         * question a synthesised press asks -- so a region a plugin is not
         * showing is not a reason to answer "nothing", while a hide the cache
         * or a script authored still is. A plugin that covers a native
         * control and delegates to it hid that control ITSELF, and the strict
         * fence answered its own hide back to it: minimap-orbs read
         * `actions` as empty and `invoke` as blocked on the very button its
         * cover exists to press. @see UITree_NodeOrAncestorDisplayHiddenEx. */
        if( UITree_NodeOrAncestorDisplayHiddenEx(tree,idx,1) ||
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
        /* run_option validates the pick a second time, and the row was built
         * by a walk that knows nothing of where the press came from -- so
         * stamp it with the statement the gate above already made, or the
         * press is resolved, built and then dropped without a word.
         * @see app_plugin_click_node. */
        menu.options[op].pick.allow_plugin_hidden=1;
        struct UIMinimenu saved = *app->world_views.frame->minimenu;
        *app->world_views.frame->minimenu = menu;
        app_minimenu_run_option(app,op,0,0);
        *app->world_views.frame->minimenu = saved;
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
    case PLUGIN_WIDGET_STATE:
    {
        /* One read of every answer a follower would otherwise poll for. The
         * geometry pair is BOUNDS and LOCAL_BOUNDS verbatim (layout first, as
         * they do); `presented` is the VISIBLE answer verbatim. The two hide
         * bits stay APART: `own_hidden` is what a script or the cache said,
         * `native_hidden` is what the engine said, and a plugin that means to
         * un-hide a stone has to know which one refused. */
        UITree_EnsureLayoutFor(tree, idx);
        struct ToriRS_WidgetState* state = r->state;
        int32_t scene_id = 0, atlas_index = 0;
        bool const display_hidden = UITree_NodeOrAncestorDisplayHidden(tree, idx) != 0;
        UITree_NodeDrawnBounds(tree, idx, &state->bounds.x, &state->bounds.y,
                               &state->bounds.width, &state->bounds.height);
        state->local = (struct ToriRS_WidgetBounds){
            c->position.abs_x, c->position.abs_y, c->position.abs_w, c->position.abs_h};
        if( c->parent >= 0 )
        {
            state->local.x -= tree->components[c->parent].position.abs_x;
            state->local.y -= tree->components[c->parent].position.abs_y;
        }
        state->presented = !display_hidden &&
            UITree_NodeNativeVisible(tree, &app->ui_host, idx, app->interact.published_hover_com_id);
        state->own_hidden = c->behavior.hide != 0;
        state->native_hidden = c->native_hide != 0;
        /* What the LANE offers here, which is not the same question as
         * `presented` above and must not fold in the plugin layer's own
         * hiding. A plugin that covers a native control and keeps its action
         * -- the minimap orbs over the lane's own orb -- hides that control
         * itself, and a fence that counted its own hide answered "there is
         * nothing to press" about the button its cover delegates to, one
         * fence after it drew it. A hide the cache or a script authored still
         * says no, because that one means the game says the button is gone. */
        state->input_present = UITree_NodeNativeInputPresent(tree, &app->ui_host, idx) &&
            !UITree_NodeOrAncestorDisplayHiddenEx(tree, idx, 1);
        if( c->type == UIELEM_RS_GRAPHIC )
        { scene_id = c->u.rs_graphic.scene_id; atlas_index = c->u.rs_graphic.atlas_index; }
        else if( c->type == UIELEM_BUILTIN_SPRITE || c->type == UIELEM_BUILTIN_COMPASS )
        { scene_id = c->u.sprite.scene_id; atlas_index = c->u.sprite.atlas_index; }
        /* A CHANGE token and nothing else: art that differs gives a different
         * number, and no caller may read a scene id back out of it. */
        state->graphic_token =
            (uint32_t)(((uint32_t)scene_id << 8) ^ ((uint32_t)atlas_index & 0xFFu));
        /* What a REPLACE of this widget would CONSUME. The token above cannot
         * answer it -- the art of a button is on a child, so the token is zero
         * on every one of them -- and the walk is only reached for a node that
         * is presented, which is where it costs nothing on a node the lane has
         * already put away. @see UITree_NodeSubtreePaintsArt. */
        state->paints_own_art =
            state->presented &&
            UITree_NodeSubtreePaintsArt(tree, &app->ui_host, idx, app->interact.published_hover_com_id);
        state->text_hash = UITree_NodeTextHash(tree, idx);
        state->facets = app_plugin_widget_facets(app, idx);
        state->incarnation = r->ref.opaque[2];
        return TORIRS_CONTRACT_OK;
    }
    case PLUGIN_WIDGET_TEXT:
    {
        if( c->type != UIELEM_RS_TEXT ) return TORIRS_CONTRACT_UNAVAILABLE;
        char const* text = c->u.rs_text.text ? c->u.rs_text.text : "";
        *r->count = strlen(text) + 1;
        if( r->capacity ) snprintf(r->text, r->capacity, "%s", text);
        return *r->count > r->capacity ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
    }
    case PLUGIN_WIDGET_MODEL:
    {
        /* The head entry's kind is an enum UIIfHeadKind (ui/uitree_if_models.h),
         * the same mapping torirs_plugin_drive_read.c's DriveRead_WidgetModel
         * uses. */
        r->model->kind = TORIRS_WIDGET_MODEL_NONE;
        r->model->id = -1;
        {
            int const i = UIIfModels_HeadIndex(&app->if_models, c->component_id);
            if( i >= 0 )
            {
                struct UIIfHead const* head = &app->if_models.heads[i];
                switch( head->kind )
                {
                case UI_IFHEAD_NPC: r->model->kind = TORIRS_WIDGET_MODEL_NPC; break;
                case UI_IFHEAD_OBJ: r->model->kind = TORIRS_WIDGET_MODEL_OBJ; break;
                case UI_IFHEAD_MODEL: r->model->kind = TORIRS_WIDGET_MODEL_MODEL; break;
                default: break; /* player: no raw identity to report */
                }
                if( r->model->kind != TORIRS_WIDGET_MODEL_NONE )
                    r->model->id = head->npc_id;
            }
        }
        return TORIRS_CONTRACT_OK;
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
    case PLUGIN_WIDGET_CREATE_ITEM:
    {
        /* An item control is an owned graphic carrying an obj (setItemId on a
         * GRAPHIC child): the same node create_image makes, so the two share
         * one key space. */
        int child=UITree_WidgetCreateGraphic(tree,ref,owner,r->name);
        *r->refs=app_widget_ref(tree,child);
        if( child<0 ) return TORIRS_CONTRACT_FAILED;
        break;
    }
    case PLUGIN_WIDGET_CREATE_RECT:
    case PLUGIN_WIDGET_CREATE_MODEL:
    {
        int child = r->kind == PLUGIN_WIDGET_CREATE_RECT ? UITree_WidgetCreateRect(tree, ref, owner, r->name)
                                                         : UITree_WidgetCreateModel(tree, ref, owner, r->name);
        *r->refs = app_widget_ref(tree, child);
        if( child < 0 ) return TORIRS_CONTRACT_FAILED;
        break;
    }
    case PLUGIN_WIDGET_SET_RECT:
        if( c->plugin_owner != owner || c->type != UIELEM_RS_RECT ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        (void)UITree_SetColourAt(tree, idx, r->a);
        (void)UITree_SetNativeIntAt(tree, idx, UITREE_NATIVE_FILL, r->b);
        break;
    case PLUGIN_WIDGET_SET_MODEL:
    {
        if( c->plugin_owner != owner || c->type != UIELEM_RS_MODEL ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        enum ToriRS_ContractResult const result = app_widget_set_model(app, ref, idx, r->a, r->b);
        RedrawRequest_Raise(&app->redraw_request);
        return result;
    }
    case PLUGIN_WIDGET_SET_MODEL_POSE:
        if( c->plugin_owner != owner || c->type != UIELEM_RS_MODEL ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        (void)UITree_SetModelPoseAt(tree, idx, 0, 0, r->a, r->b, r->id, r->op);
        break;
    case PLUGIN_WIDGET_NATIVE_HIDDEN:
        if( c->plugin_owner ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( !UITree_WidgetSetNativeHidden(tree, ref, owner, r->a != 0) ) return TORIRS_CONTRACT_BUDGET_EXCEEDED;
        break;
    case PLUGIN_WIDGET_SCROLL_SIZE:
        if( c->plugin_owner || c->type != UIELEM_RS_LAYER ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( !UITree_SetScrollSizeAt(tree, idx, r->a, r->b) ) return TORIRS_CONTRACT_FAILED;
        break;
    case PLUGIN_WIDGET_SET_MODEL_ANIM:
        if( c->plugin_owner != owner || c->type != UIELEM_RS_MODEL ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        (void)UITree_SetModelAnimationAt(tree, idx, r->a, 0, 0, 0);
        break;
    case PLUGIN_WIDGET_DETAIL:
        app_widget_detail(app, idx, r->detail);
        return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_DYNAMIC_CHILDREN:
        return app_widget_child_list(app, idx, APP_WIDGET_CHILDREN_DYNAMIC, r);
    case PLUGIN_WIDGET_STATIC_CHILDREN:
        return app_widget_child_list(app, idx, APP_WIDGET_CHILDREN_STATIC, r);
    case PLUGIN_WIDGET_NESTED_CHILDREN:
        return app_widget_child_list(app, idx, APP_WIDGET_CHILDREN_NESTED, r);
    case PLUGIN_WIDGET_LISTENER:
        return app_widget_listener(app, c, r);
    case PLUGIN_WIDGET_NATIVE_TEXT:
        if( c->plugin_owner || c->type!=UIELEM_RS_TEXT ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( !UITree_WidgetSetNativeText(tree,ref,owner,r->name) ) return TORIRS_CONTRACT_BUDGET_EXCEEDED;
        break;
    case PLUGIN_WIDGET_NATIVE_TEXT_COLOR:
        if( c->plugin_owner || c->type!=UIELEM_RS_TEXT ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( !UITree_WidgetSetNativeTextColor(tree,ref,owner,r->a) ) return TORIRS_CONTRACT_BUDGET_EXCEEDED;
        break;
    case PLUGIN_WIDGET_SET_ITEM:
    {
        if( !c->plugin_owner && (c->type==UIELEM_RS_GRAPHIC || c->type==UIELEM_CC_OBJ) )
        {
            enum ToriRS_ContractResult const native=app_widget_set_native_item(app,ref,owner,r->a,r->b,r->id);
            RedrawRequest_Raise(&app->redraw_request);
            return native;
        }
        if( c->plugin_owner!=owner || c->type!=UIELEM_RS_GRAPHIC ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        enum ToriRS_ContractResult const result=app_widget_set_item(app,ref,idx,r->a,r->b,r->id);
        RedrawRequest_Raise(&app->redraw_request);
        return result;
    }
    case PLUGIN_WIDGET_SET_SPRITE:
    {
        /* Bound even while the sprite loads: the bridge fills the scene id in
         * when it lands, as it does for a script's cc_setgraphic. */
        int const scene=UITreeSceneBridge_BindSprite(&app->bridge,r->a);
        if( scene<=0 ) return TORIRS_CONTRACT_UNAVAILABLE;
        if( c->plugin_owner==owner && c->type==UIELEM_RS_GRAPHIC )
        {
            if( !UITree_SetGraphicAt(tree,idx,scene,0) ) return TORIRS_CONTRACT_FAILED;
            /* "The control's art at its size": an IF3 graphic scales its
             * sprite to the box (a non-IF3 one blits it 1:1, so a 96x?? backing
             * sprite on a 384x2 rule painted a band over everything under it). */
            (void)UITree_SetNativeIntAt(tree,idx,UITREE_NATIVE_IF3,1);
        }
        else if( !c->plugin_owner &&
                 (c->type==UIELEM_BUILTIN_SPRITE || c->type==UIELEM_RS_GRAPHIC || c->type==UIELEM_BUILTIN_COMPASS) )
        {
            if( !UITree_WidgetSetArt(tree,ref,owner,scene) ) return TORIRS_CONTRACT_FAILED;
        }
        else return TORIRS_CONTRACT_NATIVE_BLOCKED;
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
        RedrawRequest_Raise(&app->redraw_request);
        break;
    case PLUGIN_WIDGET_SET_MASK:
        if( c->plugin_owner ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( c->type!=UIELEM_BUILTIN_MINIMAP && c->type!=UIELEM_BUILTIN_COMPASS && c->type!=UIELEM_BUILTIN_SPRITE )
            return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( !UITree_WidgetSetMask(tree,ref,owner,r->id<0 ? 0 : UITREE_SCENE_PLUGIN_IMAGE_BASE+r->id) ) return TORIRS_CONTRACT_FAILED;
        RedrawRequest_Raise(&app->redraw_request);
        break;
    case PLUGIN_WIDGET_OPACITY:
        if( !c->plugin_owner )
        {
            /* A native node: the retained transparency edit (setOpacity on a
             * game widget -- the bank tab's missing-item cells). */
            if( !UITree_WidgetSetNativeTransparency(tree,ref,owner,255-r->a) ) return TORIRS_CONTRACT_BUDGET_EXCEEDED;
            RedrawRequest_Raise(&app->redraw_request);
            break;
        }
        if( c->plugin_owner!=owner ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( !UITree_WidgetSetTransparency(tree,ref,owner,255-r->a) ) return TORIRS_CONTRACT_FAILED;
        RedrawRequest_Raise(&app->redraw_request);
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
        RedrawRequest_Raise(&app->redraw_request);
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
    case PLUGIN_WIDGET_CANVAS_POSITION:
        if( c->plugin_owner != owner ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( !UITree_WidgetSetCanvasPosition(tree, ref, owner, r->a, r->b) )
            return TORIRS_CONTRACT_FAILED;
        break;
    case PLUGIN_WIDGET_MOVE_AFTER:
    {
        struct UITreeNodeRef sibling;
        if( c->plugin_owner != owner ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
        if( !r->target.opaque[1] || r->target.opaque[1] > INT32_MAX ) return TORIRS_CONTRACT_STALE_REFERENCE;
        sibling = (struct UITreeNodeRef){.tree_instance=r->target.opaque[0],
            .index=(int32_t)r->target.opaque[1]-1, .incarnation=r->target.opaque[2]};
        if( UITree_ResolveRef(tree, sibling) < 0 ) return TORIRS_CONTRACT_STALE_REFERENCE;
        if( !UITree_WidgetMoveAfter(tree, ref, owner, sibling) ) return TORIRS_CONTRACT_INVALID_ARGUMENT;
        break;
    }
    case PLUGIN_WIDGET_REVALIDATE:
        UITree_EnsureLayout(tree);
        break;
    case PLUGIN_WIDGET_RESET:
        if( !UITree_WidgetReset(tree, ref, owner) ) return TORIRS_CONTRACT_FAILED;
        break;
    default: return TORIRS_CONTRACT_UNAVAILABLE;
    }
    RedrawRequest_Raise(&app->redraw_request);
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
 * claiming to be `side3`. What the pass remembers between runs, and when it
 * may skip, is plugin/plugin_frame_binding.h.
 */

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
    uint16_t role_id,
    uint8_t tag,
    int member,
    int32_t* next,
    int* next_count)
{
    int32_t node;
    struct UITreeComponent* c;

    assert(app);
    assert(tree);
    assert(next);
    assert(next_count);
    PA_INC(frame_bind_stamp_calls);
    if( role_id == 0 || *next_count >= PLUGIN_FRAME_STAMP_MAX )
        return;
    node = UITree_RoleNode(tree, &app->ui_roles, role_id);
    if( node < 0 || (uint32_t)node >= tree->component_count )
        return;
    c = &tree->components[node];
    if( c->freed )
        return;
    UITree_FrameStamp(tree, node, tag, (uint8_t)(member + 1));
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
    /* A linear read of the whole pack (@see UITREE_SCAN_METER). */
    if( pack )
        UITREE_SCAN_METER_NODES(pack->component_count);
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
    UITREE_SCAN_METER_NODES(e->count);
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
    /* Up to two linear reads of the chat pack (@see UITREE_SCAN_METER). */
    UITREE_SCAN_METER_NODES(pack->component_count);
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
    /* -3, not -1, while the pack or enum that decides the answer is not
     * resident: UITree_RoleNode remembers a -1, and a "no" read off data that
     * had not loaded yet would stay "no" until the tree next changed. */
    if( strncmp(name, "chat_plate_", 11) == 0 &&
        RevConfigRefs_Get(&app->revconfig_refs, "enum", "chat_filters") >= 0 )
    {
        char* end;
        long filter = strtol(name + 11, &end, 10);
        if( end == name + 11 || *end || filter < 0 || filter > 255 ) return -1;
        if( !CacheProvider_EnumGet(
                app->provider, RevConfigRefs_Get(&app->revconfig_refs, "enum", "chat_filters")) ||
            !CacheProvider_ComponentPackGet(
                app->provider, RevConfigRefs_Get(&app->revconfig_refs, "iface", "chat")) )
            return -3;
        int uid = app_plugin_chat_plate_expected(app, (int)filter);
        return uid < 0 ? -1 : UITree_FindByComponentId(tree, uid);
    }
    if( strncmp(name, "frame_", 6) && strncmp(name, "sidetab_", 8) ) return -2;
    /* The declines first: they read nothing, and the enum lookup below reads
     * the toplevel's pack. */
    int key = app_plugin_frame_role_key_161(table, role), uid = -1, control = -1;
    if( key < 0 || RevConfigRefs_Get(&app->revconfig_refs, "script", "frame_init") < 0 ) return -2;
    int root = app_plugin_frame_root(app);
    if( root >= 0 && app->provider && !CacheProvider_ComponentPackGet(app->provider, root) )
        return -3;
    int element_map = app_plugin_frame_role_enum_id(app, root, &control);
    if( element_map < 0 ) return -1;
    if( !CacheProvider_EnumGet(app->provider, element_map) ) return -3;
    if( !app_plugin_frame_enum_value(app, element_map, key, &uid) || uid < 0 ) return -1;
    int32_t node = UITree_FindByComponentId(tree, uid);
    if( node >= 0 && torirs_env_frame_role_audit() )
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

void
app_plugin_frame_bind(struct UITree* tree, void* user)
{
    struct App* app = (struct App*)user;
    int32_t next[PLUGIN_FRAME_STAMP_MAX];
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
        PluginFrameBinding_DropBound(&app->plugin_frame_binding);
    }

    /* Every answer below is a function of the role memo's two keys: a role
     * can only move when the tree is rebuilt (`generation`) or a node is
     * pushed or recycled (`id_generation`). When neither has moved since the
     * last pass the stamps are already right; running the pass again would
     * recompute 106 constants with 7,000 string compares to reach the same
     * table. A role interned after the last pass (the table grew) is the one
     * other input, and it re-arms the pass through the intern table below. */
    if( PluginFrameBinding_BoundCurrent(
            &app->plugin_frame_binding,
            tree,
            tree->generation,
            tree->id_generation,
            app->ui_roles.count) )
        return;

    /* The frame role names are compile-time constants; intern them once per
     * role table and stamp by id. Rebuilt when the table grows, since a role
     * a later profile line declares must start answering. */
    _Static_assert(UITREE_FRAME_SLOT_COUNT <= PLUGIN_FRAME_BINDING_SLOTS, "frame slot table too small");
    _Static_assert(
        UITREE_FRAME_SLOT_NODES_MAX <= PLUGIN_FRAME_BINDING_MEMBERS, "frame member table too small");
    if( !PluginFrameBinding_RoleIdsCurrent(&app->plugin_frame_binding, app->ui_roles.count) )
    {
        for( int slot = 0; slot < UITREE_FRAME_SLOT_COUNT; slot++ )
        {
            char const* name = UITree_RoleSlotName(slot);
            char role[UITREE_ROLE_NAME_MAX];

            PluginFrameBinding_ClearRoleIds(&app->plugin_frame_binding, slot);
            if( app_plugin_frame_slot_tag(slot) == UITREE_SLOT_NONE || !name )
                continue;
            snprintf(role, sizeof(role), "frame_%s", name);
            PluginFrameBinding_SetRoleId(
                &app->plugin_frame_binding, slot, 0, UITree_RoleFind(&app->ui_roles, role));
            if( !app_plugin_frame_slot_has_members(slot) )
                continue;
            for( int member = 0; member < UITREE_FRAME_SLOT_NODES_MAX; member++ )
            {
                snprintf(role, sizeof(role), "frame_%s_%d", name, member);
                PluginFrameBinding_SetRoleId(
                    &app->plugin_frame_binding,
                    slot,
                    1 + member,
                    UITree_RoleFind(&app->ui_roles, role));
            }
        }
        /* Not a `frame_<slot>` rung and not a numbered member of one: the
         * compass's hit region is a second node of the compass ROLE, which is
         * why it is named rather than numbered. @see
         * UITree_FrameCompassClickNode. */
        PluginFrameBinding_SetCompassClickRole(
            &app->plugin_frame_binding, UITree_RoleFind(&app->ui_roles, "frame_compass_click"));
        PluginFrameBinding_RoleIdsInterned(&app->plugin_frame_binding, app->ui_roles.count);
    }
    for( int slot = 0; slot < UITREE_FRAME_SLOT_COUNT; slot++ )
    {
        uint8_t const tag = app_plugin_frame_slot_tag(slot);
        uint16_t const* ids = PluginFrameBinding_RoleIds(&app->plugin_frame_binding, slot);

        if( tag == UITREE_SLOT_NONE || !ids[0] )
            continue;
        app_plugin_frame_stamp_role(app, tree, ids[0], tag, -1, next, &next_count);
        if( !app_plugin_frame_slot_has_members(slot) )
            continue;
        for( int member = 0; member < UITREE_FRAME_SLOT_NODES_MAX; member++ )
            app_plugin_frame_stamp_role(app, tree, ids[1 + member], tag, member, next, &next_count);
    }
    app_plugin_frame_stamp_role(
        app, tree, PluginFrameBinding_CompassClickRole(&app->plugin_frame_binding),
        UITREE_SLOT_COMPASS_CLICK, -1, next, &next_count);

    /* One audit per root, when asked for: the rungs are hand-copied from a
     * table the cache also ships, and a mistyped one is otherwise silent. */
    {
        static int audited_root = -1;
        static uint64_t audited_incarnation;
        static struct UITree const* audited_tree;
        int const root = app_plugin_frame_root(app);
        if( root > 0 && torirs_env_frame_role_audit() )
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
    for( int i = 0; i < PluginFrameBinding_StampCount(&app->plugin_frame_binding); i++ )
    {
        struct PluginFrameStamp const stamp = PluginFrameBinding_Stamp(&app->plugin_frame_binding, i);
        int32_t const idx = stamp.node;
        struct UITreeComponent* c;
        int again = 0;

        if( idx < 0 || (uint32_t)idx >= tree->component_count )
            continue;
        c = &tree->components[idx];
        if( c->freed || c->incarnation != stamp.incarnation )
            continue;
        for( int n = 0; n < next_count && !again; n++ )
            again = next[n] == idx;
        if( again )
            continue;
        UITree_FrameStamp(tree, idx, UITREE_SLOT_NONE, 0);
    }
    PluginFrameBinding_ClearStamps(&app->plugin_frame_binding);
    for( int n = 0; n < next_count; n++ )
        PluginFrameBinding_PushStamp(
            &app->plugin_frame_binding, next[n], tree->components[next[n]].incarnation);
    /* One line per pass the guard let through. The matrix counts them
     * against the distinct (generation, id_generation) pairs of the run: a
     * guard that stops working shows as one line per layout tick. */
    {
        static int trace = -1;
        if( trace < 0 )
            trace = getenv("TORIRS_TRACE_NATIVE_UI") != NULL;
        if( trace )
            TORIRS_REPORT("PLUGIN_FRAME_BIND generation=%u id_generation=%u stamps=%d\n",
                          tree->generation, tree->id_generation, next_count);
    }
    PluginFrameBinding_MarkBound(
        &app->plugin_frame_binding, tree, tree->generation, tree->id_generation);
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
                           "the plugin drive cannot switch sidebar tabs\n");
            return 0;
        }
        args[0] = tabno;
        RS_CS2_RunScript(&app->host, &app->frame_runner, script, args, 1, 0, NULL, 0);
        /* THE TAB IS SWITCHED NOW, not over the next frames (the owner,
         * 2026-10-09: "Interface tab changes do not need to wait for a tick
         * boundary"). The switch script mounts the panel through the task
         * runner and the frame's settle lays it out, so a press or a held op
         * on it in the same resume found no laid-out node and went nowhere
         * -- where scriptrun, which has no frames, pressed it. The same
         * settle the frame runs, run here to its fixed point; a settle that
         * stays pending on a real platform read is finished by the frame as
         * before. The drive pump resumes in on_frame_start, outside any
         * task, so this cannot re-enter the runner. */
        for( int i = 0; i < 8; i++ )
            if( app_settle_cs2_frame(app) == TASK_RUNNER_IDLE )
                break;
        RedrawRequest_Raise(&app->redraw_request);
        return 1;
    }
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

static int
app_plugin_menu_count(void* user, void* cursor)
{
    struct UIMinimenu const* menu = (struct UIMinimenu const*)cursor;

    (void)user;
    assert(menu);
    return menu->option_count;
}

static void
app_plugin_menu_set_text(void* user, void* cursor, int index, char const* text)
{
    struct UIMinimenu* menu = (struct UIMinimenu*)cursor;

    (void)user;
    assert(menu);
    assert(text);
    assert(index >= 0);
    assert(index < menu->option_count);
    snprintf(menu->options[index].text, sizeof(menu->options[index].text), "%s", text);
}

/* Rebuild the rows as order[0..count): the host lists each current index at
 * most once, so a scratch copy of the old rows is all it takes. Heap, not
 * stack: a full menu is 400 rows of ~200 bytes. */
static void
app_plugin_menu_permute(void* user, void* cursor, int const* order, int count)
{
    struct UIMinimenu* menu = (struct UIMinimenu*)cursor;
    struct UIMinimenuOption* old;
    int const was = menu ? menu->option_count : 0;

    (void)user;
    assert(menu);
    assert(order);
    assert(count >= 0);
    assert(count <= was);
    if( was == 0 )
        return;
    old = ToriMem_Alloc(ToriMem_Plugins(), sizeof(*old) * (size_t)was);
    assert(old);
    memcpy(old, menu->options, sizeof(*old) * (size_t)was);
    for( int i = 0; i < count; i++ )
    {
        assert(order[i] >= 0);
        assert(order[i] < was);
        menu->options[i] = old[order[i]];
    }
    menu->option_count = count;
    ToriMem_Free(ToriMem_Plugins(), old);
}

/* The payload row for the menu's row `index` as it stands now: what the host
 * hands a later plugin after an earlier one moved the menu. */
_Static_assert(UITREE_MINIMENU_MAX_OPTIONS <= TORIRS_PLUGIN_MENU_ROWS_MAX,
               "a plugin's menu payload carries every row a minimenu holds");
static void
app_plugin_menu_row_at(void* user, void* cursor, int index, struct ToriRS_MenuRow* out)
{
    struct App* app = (struct App*)user;
    struct UIMinimenu const* menu = (struct UIMinimenu const*)cursor;

    assert(app);
    assert(menu);
    assert(out);
    assert(index >= 0);
    assert(index < menu->option_count);
    app_plugin_menu_row(app, &menu->options[index], out);
}

static int
app_plugin_menu_shown(void* user, struct ToriRS_Rect* out)
{
    struct App* app = (struct App*)user;
    struct UIMinimenu const* menu;

    assert(app);
    assert(out);
    menu = app->world_views.frame ? app->world_views.frame->minimenu : NULL;
    if( !menu || !menu->visible )
        return 0;
    *out = (struct ToriRS_Rect){ menu->x, menu->y, menu->width, menu->height };
    return 1;
}

/*
 * RuneLite's MenuAction id for a row's normalized action, or -1 when RuneLite
 * names no action for it. `if3` says which family a widget button row is: a
 * cache IF3 component's op is CC_OP (57), an IF1 button WIDGET_TYPE_1 (24).
 * The numbers are net.runelite.api.MenuAction's getId() values.
 */
static int
app_plugin_runelite_menu_type(struct App const* app, int action, bool if3)
{
    switch( action )
    {
    case REVCONFIG_MINIMENU_OPNPC1: return 9;
    case REVCONFIG_MINIMENU_OPNPC2: return 10;
    case REVCONFIG_MINIMENU_OPNPC3: return 11;
    case REVCONFIG_MINIMENU_OPNPC4: return 12;
    case REVCONFIG_MINIMENU_OPNPC5: return 13;
    case REVCONFIG_MINIMENU_OPNPC6: return 1003;
    case REVCONFIG_MINIMENU_OPLOC1: return 3;
    case REVCONFIG_MINIMENU_OPLOC2: return 4;
    case REVCONFIG_MINIMENU_OPLOC3: return 5;
    case REVCONFIG_MINIMENU_OPLOC4: return 6;
    case REVCONFIG_MINIMENU_OPLOC5: return 1001;
    case REVCONFIG_MINIMENU_OPLOC6: return 1002;
    case REVCONFIG_MINIMENU_OPOBJ1: return 18;
    case REVCONFIG_MINIMENU_OPOBJ2: return 19;
    case REVCONFIG_MINIMENU_OPOBJ3: return 20;
    case REVCONFIG_MINIMENU_OPOBJ4: return 21;
    case REVCONFIG_MINIMENU_OPOBJ5: return 22;
    case REVCONFIG_MINIMENU_OPOBJ6: return 1004;
    case REVCONFIG_MINIMENU_OPPLAYER1: return 44;
    case REVCONFIG_MINIMENU_OPPLAYER2: return 45;
    case REVCONFIG_MINIMENU_OPPLAYER3: return 46;
    case REVCONFIG_MINIMENU_OPPLAYER4: return 47;
    case REVCONFIG_MINIMENU_OPPLAYER5: return 48;
    case REVCONFIG_MINIMENU_WALK: return 23;
    case REVCONFIG_MINIMENU_CANCEL: return 1006;
    case REVCONFIG_MINIMENU_IF_BUTTON: return if3 ? 57 : 24;
    case REVCONFIG_MINIMENU_IF_BUTTON_TOGGLE: return 28;
    case REVCONFIG_MINIMENU_IF_BUTTON_SELECT: return 29;
    case REVCONFIG_MINIMENU_RESUME_PAUSEBUTTON: return 30;
    case REVCONFIG_MINIMENU_CLOSE_MODAL: return 26;
    case REVCONFIG_MINIMENU_INV_BUTTON1: return 39;
    case REVCONFIG_MINIMENU_INV_BUTTON2: return 40;
    case REVCONFIG_MINIMENU_INV_BUTTON3: return 41;
    case REVCONFIG_MINIMENU_INV_BUTTON4: return 42;
    case REVCONFIG_MINIMENU_INV_BUTTON5: return 43;
    case REVCONFIG_MINIMENU_OPHELD1: return 33;
    case REVCONFIG_MINIMENU_OPHELD2: return 34;
    case REVCONFIG_MINIMENU_OPHELD3: return 35;
    case REVCONFIG_MINIMENU_OPHELD4: return 36;
    case REVCONFIG_MINIMENU_OPHELD5: return 37;
    case REVCONFIG_MINIMENU_OPHELD6: return 1005;
    case REVCONFIG_MINIMENU_OPHELDT_START: return 38;
    case REVCONFIG_MINIMENU_USEHELD_ONLOC: return 1;
    case REVCONFIG_MINIMENU_USEHELD_ONNPC: return 7;
    case REVCONFIG_MINIMENU_USEHELD_ONOBJ: return 16;
    case REVCONFIG_MINIMENU_USEHELD_ONPLAYER: return 14;
    case REVCONFIG_MINIMENU_USEHELD_ONHELD: return 31;
    case REVCONFIG_MINIMENU_TGT_BUTTON: return 25;
    case REVCONFIG_MINIMENU_TGT_LOC: return 2;
    case REVCONFIG_MINIMENU_TGT_NPC: return 8;
    case REVCONFIG_MINIMENU_TGT_OBJ: return 17;
    case REVCONFIG_MINIMENU_TGT_PLAYER: return 15;
    case REVCONFIG_MINIMENU_TGT_HELD: return 32;
    default: break;
    }
    /* A plugin's own row is RuneLite's RUNELITE. */
    if( app->plugins && PluginHost_OwnsMenuAction(app->plugins, action) )
        return 1500;
    return -1;
}

void
app_plugin_menu_row(
    struct App* app,
    struct UIMinimenuOption const* opt,
    struct ToriRS_MenuRow* row)
{
    assert(app);
    assert(opt);
    assert(row);

    memset(row, 0, sizeof(*row));
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

    /*
     * RuneLite's spelling of the same row. A widget row names a component the
     * way RuneLite does: a cc_create child by its PARENT's id and its own
     * index, which is what the node identity the build stamped resolves to.
     */
    bool if3 = true;
    int widget_component = row->component_id;
    int widget_child = -1;
    if( (opt->pick.kind == UI_MINIMENU_PICK_UI || opt->pick.kind == UI_MINIMENU_PICK_INV_SLOT) &&
        opt->pick.has_node_identity && app->tree )
    {
        struct UITreeNodeRef const ref = {
            .tree_instance = app->tree->instance_id,
            .index = opt->pick.node_index,
            .incarnation = opt->pick.node_incarnation};
        int32_t const idx = UITree_ResolveRef(app->tree, ref);
        if( idx >= 0 )
        {
            struct UITreeComponent const* c = &app->tree->components[idx];
            if3 = c->if3 != 0;
            if( c->dynamic && c->parent >= 0 )
            {
                widget_component = app->tree->components[c->parent].component_id;
                widget_child = c->dynamic_child_index;
            }
        }
    }
    row->type = app_plugin_runelite_menu_type(app, row->action, if3);
    switch( opt->pick.kind )
    {
    case UI_MINIMENU_PICK_NPC:
        row->identifier = row->npc_slot >= 0 ? row->npc_slot : 0;
        break;
    case UI_MINIMENU_PICK_PLAYER:
        row->identifier = row->player_pid >= 0 ? row->player_pid : 0;
        break;
    case UI_MINIMENU_PICK_SCENERY:
    case UI_MINIMENU_PICK_OBJ:
        row->identifier = opt->pick.secondary_id;
        row->param0 = opt->pick.tertiary_id;
        row->param1 = opt->pick.quaternary_id;
        break;
    case UI_MINIMENU_PICK_TERRAIN:
        row->param0 = opt->pick.secondary_id;
        row->param1 = opt->pick.tertiary_id;
        break;
    case UI_MINIMENU_PICK_UI:
    case UI_MINIMENU_PICK_INV_SLOT:
        /* A component op is numbered as the cache numbers it (1-based); an
         * item row is about its obj. */
        if( row->type == 57 || row->type == 24 )
            row->identifier = opt->action_index >= 0 ? opt->action_index + 1 : 0;
        else if( opt->pick.kind == UI_MINIMENU_PICK_INV_SLOT )
            row->identifier = row->target_id;
        row->param0 = opt->pick.kind == UI_MINIMENU_PICK_INV_SLOT ? row->slot : widget_child;
        row->param1 = widget_component;
        break;
    default:
        break;
    }
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
void
app_plugin_menu_build(struct App* app, struct UIMinimenu* menu, int hover_pass)
{
    struct ToriRS_MenuBuildEvent ev;

    assert(app);
    assert(menu);

    if( !app->plugins )
        return;

    memset(&ev, 0, sizeof(ev));
    /* Every row: the payload is the whole menu (TORIRS_PLUGIN_MENU_ROWS_MAX is
     * the minimenu's own capacity, asserted at app_plugin_menu_row_at). */
    assert(menu->option_count <= TORIRS_PLUGIN_MENU_ROWS_MAX);
    ev.row_count = menu->option_count;

    for( int i = 0; i < ev.row_count; i++ )
        app_plugin_menu_row(app, &menu->options[i], &ev.rows[i]);

    /* A plugin's menu.set_order is the menu as it stands (RuneLite's
     * setMenuEntries): sorting it again would undo the order it asked for. */
    if( !PluginHost_MenuBuild(app->plugins, menu, &ev, hover_pass != 0) )
        UIMinimenu_SortPriorityActions(menu);
}

struct ToriRS_PluginEngine
app_plugin_engine(struct App* app)
{
    struct ToriRS_PluginEngine engine;

    assert(app);

    memset(&engine, 0, sizeof(engine));
    engine.user = app;
    engine.widget_request = app_plugin_widget_request;
    engine.screen = app_plugin_screen;
    engine.world_cycle = app_plugin_world_cycle;
    engine.frame_ms = app_plugin_frame_ms;
    engine.frame_work_us = app_plugin_frame_work_us;
    engine.capability = app_plugin_capability;
    engine.script_invalidate = app_script_invalidate;
    engine.memory_bytes = app_plugin_memory_bytes;
    engine.local_player = app_plugin_local_player;
    engine.scene_origin = app_plugin_scene_origin;
    engine.instance_zone = app_plugin_instance_zone;
    engine.npc_next = app_plugin_npc_next;
    engine.npc_by_slot = app_plugin_npc_by_slot;
    engine.player_next = app_plugin_player_next;
    engine.obj_next = app_plugin_obj_next;
    engine.loc_next = app_plugin_loc_next;
    engine.loc_anim_next = app_plugin_loc_anim_next;
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
    engine.varc_int = app_plugin_varc_int;
    engine.varc_str = app_plugin_varc_str;
    engine.set_varc_int = app_plugin_set_varc_int;
    engine.set_varc_str = app_plugin_set_varc_str;
    engine.enum_def = app_plugin_enum_def;
    engine.npc_def = app_plugin_npc_def;
    engine.loc_def = app_plugin_loc_def;
    engine.sprite_pixels = app_plugin_sprite_pixels;
    engine.chat_add = app_plugin_chat_add;
    engine.hint_arrow_set = app_plugin_hint_arrow_set;
    engine.hint_arrow_get = app_plugin_hint_arrow_get;
    engine.sound = app_plugin_sound;
    engine.weight = app_plugin_weight;
    engine.world_types = app_plugin_world_types;
    engine.tick = app_plugin_tick;
    engine.varp_values = app_plugin_varp_values;
    engine.script_run = app_plugin_script_run;
    engine.script_watch = app_plugin_script_watch;
    /* One hook for every VM thread; it answers nothing until a plugin
     * watches a script. Cleared by App_Free with the host. */
    CS2VM2_SetScriptHook(app_script_hook, app);
    engine.inv_slot = app_plugin_inv_slot;
    engine.inv_size = app_plugin_inv_size;
    engine.container_slot = app_plugin_container_slot;
    engine.container_size = app_plugin_container_size;
    engine.world_entity_next = app_plugin_world_entity_next;
    engine.world_entity_to_main = app_plugin_world_entity_to_main;
    engine.world_entity_loc_next = app_plugin_world_entity_loc_next;
    engine.project = app_plugin_project;
    engine.draw_tile = app_plugin_draw_tile;
    engine.draw_hull = app_plugin_draw_hull;
    engine.draw_line = app_plugin_draw_line;
    engine.draw_text = app_plugin_draw_text;
    engine.draw_rect = app_plugin_draw_rect;
    engine.draw_polygon = app_plugin_draw_polygon;
    engine.text_width = app_plugin_text_width;
    engine.text_height = app_plugin_text_height;
    engine.minimap_project = app_plugin_minimap_project;
    engine.minimap_bounds = app_plugin_minimap_bounds;
    engine.worldmap_project = app_plugin_worldmap_project;
    engine.worldmap_bounds = app_plugin_worldmap_bounds;
    engine.draw_select_canvas = app_plugin_draw_select_canvas;
    engine.image_publish = app_plugin_image_publish;
    engine.image_publish_argb = app_plugin_image_publish_argb;
    engine.image_publish_fit = app_plugin_image_publish_fit;
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
    engine.component_rect = app_plugin_component_rect;
    engine.menu_drop = app_plugin_menu_drop;
    engine.menu_count = app_plugin_menu_count;
    engine.menu_set_text = app_plugin_menu_set_text;
    engine.menu_permute = app_plugin_menu_permute;
    engine.menu_row = app_plugin_menu_row_at;
    engine.menu_shown = app_plugin_menu_shown;
    engine.set_varbit = app_plugin_set_varbit;
    engine.seq_def = app_plugin_seq_def;
    engine.open_url = app_plugin_open_url;
    engine.worldmap_jump = app_plugin_worldmap_jump;
    engine.stat = app_plugin_stat;
    engine.stat_xp = app_plugin_stat_xp;
    engine.skill_name = app_plugin_skill_name;
    engine.run_energy = app_plugin_run_energy;
    engine.menu_add = app_plugin_menu_add;
    engine.asset_read = app_plugin_asset_read;
    engine.script_read = app_plugin_script_read;
    engine.asset_write = app_plugin_asset_write;
    engine.screenshot = app_plugin_screenshot;
    engine.capture_root = app_plugin_capture_root;
    engine.files_request = app_plugin_files_request;
    engine.capture_read = app_plugin_capture_read;
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
    engine.object_set_fine_position = app_plugin_object_set_fine_position;
    engine.object_element = app_plugin_object_element;
    engine.object_hit = app_plugin_object_hit;
    engine.hsl_from_rgb = app_plugin_hsl_from_rgb;
    engine.hsl_to_rgb = app_plugin_hsl_to_rgb;
    return engine;
}

/*
 * Test-only seams for the quest driver (src/plugin/torirs_plugin_drive_ui.c).
 *
 * `app_plugin_if_click` and `app_plugin_tab_select` are the dispatchers every
 * real click already agrees on, and the driver must use THOSE rather than a
 * second copy that could drift. The gated api_* wrappers in
 * torirs_plugin_host.c cannot serve it: their dispatch_event allow-list has no
 * entry for "a coroutine resumed on a bare frame tick", which is exactly what
 * a quest test is, and the owner's decision on that (plan 5.8, U9) is that the
 * test-only module may call the raw seam. These two wrappers are the whole of
 * that exception, and they are named App_PluginDrive* so a grep for the
 * bypass finds every user of it.
 */
int
App_PluginDriveIfClick(struct App* app, int component_id, int op)
{
    assert(app);
    return app_plugin_if_click(app, component_id, op);
}

int
App_PluginDriveIfClickNode(struct App* app, int32_t node, int op)
{
    assert(app);
    assert(app->tree);
    assert(node >= 0);
    return app_plugin_click_node(app, node, op);
}

/*
 * A click on a cache stone also reports itself: IF_BUTTON1 on the stone, when
 * the stone's events arm op 1 (the cache's `clickmask=2` arms every one), which
 * scriptrun's tab verb sends too. app_plugin_tab_select runs only the switch
 * script, so the packet goes here -- not the stone's own op, whose script
 * would run the switch a second time on a later frame. The stone is the
 * profile's `sidestone_<n>`; `sidetab_<n>` is the icon, which arms no op.
 */
int
App_PluginDriveTabSelect(struct App* app, int tabno)
{
    char role[UITREE_ROLE_NAME_MAX];
    int32_t stone;
    int target;
    int sub;

    assert(app);
    if( !app_plugin_tab_select(app, tabno) )
        return 0;
    if( App_UiLogic(app) != APP_UI_LOGIC_CS2 )
        return 1;
    assert(app->tree);
    snprintf(role, sizeof(role), "sidestone_%d", tabno);
    stone = UITree_RoleNodeByName(app->tree, &app->ui_roles, role);
    if( stone < 0 )
        return 1;
    if( !(App_IfEventsGetEffective(app, app->tree->components[stone].component_id) & (1u << 1)) )
        return 1;
    UIIfEventTable_ButtonTarget(app->tree, app->tree->components[stone].component_id, &target, &sub);
    APP_NET_SEND(
        app,
        net_out_if_button_op(
            app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), 1, target, sub));
    return 1;
}
