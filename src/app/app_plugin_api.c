/*
 * What plugins may ask of the world: simulated ops, screen positions, tile
 * queries, and the game-event notifications.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_dispatch_game_event(
    struct App* app,
    struct RS_GameEvent const* ev);

void
App_LootNotifyKill(
    struct App* app,
    char const* source_name,
    int obj_id,
    int qty)
{
    assert(app);
    assert(source_name);

    /*
     * Script 7166 only mounts a source into the Drops-mode info slots when
     * _7604(name) != 0. That opcode is the per-source kill count; seed one
     * kill via a fresh event_id. Dat2 objtypes default cost to 1; when the
     * type is not yet resident (common right after login for a lootkill
     * cheat) treat value the same way and queue the load so later OC_* ops
     * see the real record.
     */
    int cost = 1;
    struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
    if( obj )
    {
        cost = obj->cost;
    }
    else if( app->provider )
    {
        struct ToriRS_Task* load = CreateTask_ObjLoad(app->provider, obj_id);
        if( load )
            ToriRS_TaskQueue_Add(app->runner.queue, load);
    }

    int event_id = app->loot.next_event_id++;
    LootStore_AddKillLoot(&app->loot, source_name, obj_id, qty, cost, event_id);

    /*
     * Clientscript 7159: (int killDelta, int qty, string sourceName).
     * The engine fills the store FIRST, then pushes 7159 so CS2 can read it
     * back. 7159 → 7162 adds killDelta onto the source's scroll height
     * ("Name x N"); pass 1 per kill, never the obj id.
     *
     * Argument layout: intv[0] = killDelta, intv[1] = qty; str_mask bit 2
     * marks argument 2 as a string; str_args[0] = sourceName (compacted).
     */
    {
        int intv[3] = { 1, qty, 0 };
        char const* str_args[1] = { source_name };
        uint64_t str_mask = 1u << 2;

        RS_CS2_RunScript(&app->host, &app->runner, 7159, intv, 3, str_mask, str_args, 1);
    }
}

void
App_SimulateLocOp(
    struct App* app,
    int op_num,
    int abs_x,
    int abs_z,
    int loc_id)
{
    assert(app);
    APP_NET_SEND(
        app,
        net_out_oploc(
            app->net->rev,
            app->net->random_out,
            _nsbuf,
            sizeof(_nsbuf),
            op_num,
            abs_x,
            abs_z,
            loc_id));
}

int
App_SimulateNpcOp(
    struct App* app,
    int op_num,
    int npc_id)
{
    assert(app);
    if( !app->world )
        return -1;
    /*
     * Addressed by npc TYPE rather than by server slot, which is the only id a
     * test can state up front: slots are handed out by the server as npcs enter
     * the build area and are not stable between runs. The first live entity of
     * that type wins, the same one a click would land on when there is only one
     * — a familiar, a spawned quest actor.
     */
    struct World_EntityPool* pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);

        if( !npc || npc->npc_id != npc_id || npc->server_slot < 0 )
            continue;
        APP_NET_SEND(
            app,
            net_out_opnpc(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                op_num,
                npc->server_slot));
        return npc->server_slot;
    }
    return -1;
}

int
App_NpcScreenPosition(
    struct App* app,
    int npc_id,
    int* out_x,
    int* out_y,
    int* out_type)
{
    /* Inside the world viewport with this much to spare: a body projected on
     * the viewport's edge is half under the frame, and the frame takes the
     * click. */
    enum
    {
        MARGIN = 12
    };
    struct UITreeEmitDesc const* viewport;

    assert(app);
    assert(out_x);
    assert(out_y);
    assert(out_type);
    if( !app->world || !app->world_view_valid )
        return -1;
    viewport = &app->world_emit_desc;
    struct World_EntityPool* pool = &app->world->entities.npc;
    /* Of the candidates on screen, the one nearest the viewport's centre: a
     * body at the corner is mostly clipped, and a hull drawn round it later
     * is a few pixels that prove nothing. */
    int best_slot = -1;
    long best_distance = 0;
    int const centre_x = viewport->x + viewport->w / 2;
    int const centre_y = viewport->y + viewport->h / 2;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        int x;
        int y;
        long distance;

        /* npc_id < 0 takes any npc that is on screen. */
        if( !npc || npc->server_slot < 0 || (npc_id >= 0 && npc->npc_id != npc_id) )
            continue;
        /* Mid-body rather than the feet: the feet of an npc standing behind a
         * table project onto the table, and a click there is the table's. */
        if( !app_world_project_actor(
                app,
                &npc->view_placement,
                npc->grid_position.level,
                (int)npc->draw_position.x,
                (int)npc->draw_position.z,
                60,
                &x,
                &y) )
            continue;
        if( x < viewport->x + MARGIN || x >= viewport->x + viewport->w - MARGIN ||
            y < viewport->y + MARGIN || y >= viewport->y + viewport->h - MARGIN )
            continue;
        distance = (long)(x - centre_x) * (x - centre_x) + (long)(y - centre_y) * (y - centre_y);
        if( best_slot >= 0 && distance >= best_distance )
            continue;
        best_slot = npc->server_slot;
        best_distance = distance;
        *out_x = x;
        *out_y = y;
        *out_type = npc->npc_id;
    }
    return best_slot;
}

bool
App_LocalPlayerTiles(
    struct App* app,
    int* true_x,
    int* true_z,
    int* level,
    int* dest_x,
    int* dest_z,
    int* flag_x,
    int* flag_z,
    int* draw_x,
    int* draw_z)
{
    struct WorldEntity_Player* player;
    int base_x;
    int base_z;

    assert(app);
    assert(true_x);
    assert(true_z);
    assert(level);
    assert(dest_x);
    assert(dest_z);
    assert(flag_x);
    assert(flag_z);
    assert(draw_x);
    assert(draw_z);
    player = app_local_player(app);
    if( !player || !app->world )
        return false;
    base_x = app->world->_base_tile_x;
    base_z = app->world->_base_tile_z;
    /* The interpolated model position in fine units (128 per tile), scene
     * relative: where the figure is DRAWN, against the whole tile above. */
    *draw_x = (int)player->draw_position.x;
    *draw_z = (int)player->draw_position.z;
    /* route[0] is the server's whole tile; the draw position slides between
     * tiles every frame. Same reading as the plugin bridge's player snapshot. */
    if( player->pathing.route_length > 0 )
    {
        *true_x = base_x + player->pathing.route_x[0];
        *true_z = base_z + player->pathing.route_z[0];
    }
    else
    {
        *true_x = base_x + player->grid_position.x;
        *true_z = base_z + player->grid_position.z;
    }
    *level = player->grid_position.level;
    if( app->minimap.flag_tile_x >= 0 )
    {
        *flag_x = base_x + app->minimap.flag_tile_x;
        *flag_z = base_z + app->minimap.flag_tile_z;
        *dest_x = *flag_x;
        *dest_z = *flag_z;
    }
    else
    {
        *flag_x = -1;
        *flag_z = -1;
        *dest_x = *true_x;
        *dest_z = *true_z;
    }
    return true;
}

void
App_TraceWorldEntities(struct App* app)
{
    int base_x;
    int base_z;

    assert(app);
    if( !app->world )
        return;
    base_x = app->world->_base_tile_x;
    base_z = app->world->_base_tile_z;
    {
        struct World_EntityPool* pool = &app->world->entities.npc;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_NPC const* npc = World_EntityPoolGet(pool, i);
            if( !npc || npc->server_slot < 0 )
                continue;
            TORIRS_REPORT(
                "NATIVE_NPC slot=%d type=%d tile=%d,%d,%d\n",
                npc->server_slot,
                npc->npc_id,
                base_x + npc->grid_position.x,
                base_z + npc->grid_position.z,
                npc->grid_position.level);
        }
    }
    {
        struct World_EntityPool* pool = &app->world->entities.obj_stack;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_ObjStack const* stack = World_EntityPoolGet(pool, i);
            if( !stack )
                continue;
            TORIRS_REPORT(
                "NATIVE_GROUND_STACK tile=%d,%d,%d id=%d count=%d name=%s\n",
                base_x + stack->grid_position.x,
                base_z + stack->grid_position.z,
                stack->grid_position.level,
                stack->obj_id,
                stack->count,
                stack->name);
        }
    }
}

void
App_PluginObjectCounts(
    struct App* app,
    int* in_use,
    int* active,
    int* built)
{
    assert(app);
    assert(in_use);
    assert(active);
    assert(built);
    *in_use = 0;
    *active = 0;
    *built = 0;
    for( int i = 0; i < APP_PLUGIN_OBJECTS_MAX; i++ )
    {
        struct AppPluginObject const* object = &app->plugin_objects[i];

        if( !object->in_use )
            continue;
        (*in_use)++;
        if( object->active )
            (*active)++;
        if( object->element_id >= 0 )
            (*built)++;
    }
}

bool
App_MinimenuRowCenter(
    struct App* app,
    char const* prefix,
    int* out_x,
    int* out_y,
    char* out_text,
    size_t out_text_capacity)
{
    struct UIMinimenu const* menu;
    size_t prefix_len;

    assert(app);
    assert(prefix);
    assert(out_x);
    assert(out_y);
    menu = &app->interact.minimenu;
    if( !menu->visible )
        return false;
    prefix_len = strlen(prefix);
    for( int i = 0; i < menu->option_count; i++ )
    {
        if( strncmp(menu->options[i].text, prefix, prefix_len) != 0 )
            continue;
        *out_x = menu->x + menu->width / 2;
        *out_y = UIMinimenu_OptionY(menu, i);
        if( out_text && out_text_capacity > 0 )
            snprintf(out_text, out_text_capacity, "%s", menu->options[i].text);
        return true;
    }
    return false;
}

/* ------------------------------------------------- notable moments */

/*
 * The plugin layer's window onto "something worth reacting to happened".
 *
 * Three entry points because the game genuinely announces things three ways --
 * in the chatbox, on an interface, and as a number in UPDATE_STAT -- and one
 * recogniser behind them, so every plugin agrees about what a boss kill is.
 * See game/rs_game_events.c for why a level-up is NOT read out of prose.
 */
static void
app_dispatch_game_event(
    struct App* app,
    struct RS_GameEvent const* ev)
{
    char const* kind;

    assert(app);
    assert(ev);

    kind = RS_GameEvent_KindName(ev->kind);
    /* A recognised event with no name would be a recogniser that grew a kind
     * without naming it -- the one thing a plugin cannot work around. */
    assert(kind);
    PluginHost_GameEvent(app->plugins, kind, ev->subject, ev->value, ev->text);
}

void
App_NotifyChatMessage(
    struct App* app,
    int type,
    char const* sender,
    char const* text)
{
    struct RS_GameEvent ev;

    assert(app);
    if( !app->plugins )
        return;

    PluginHost_ChatMessage(app->plugins, type, sender, text);

    /*
     * Only the GAME channel is recognised, and that is a security property
     * rather than a filter.
     *
     * Every pattern the recogniser knows is a sentence a player can type. If
     * public chat fed it, standing in a bank and saying "Your Zulrah kill
     * count is: 122." would fire a boss kill in everyone's client -- taking
     * their screenshots, tripping their notifications, and writing to their
     * disk. The game channel is the server talking, and nobody else can put a
     * line on it.
     */
    if( type != RS_CHAT_TYPE_GAME )
        return;
    if( RS_GameEvent_FromText(RS_GAME_EVENT_SRC_CHAT, text, &ev) )
        app_dispatch_game_event(app, &ev);
}

void
App_NotifyInterfaceText(
    struct App* app,
    char const* text)
{
    struct RS_GameEvent ev;

    assert(app);
    if( !app->plugins )
        return;

    /* Not forwarded as a chat message: this is every journal line and every
     * button caption in the game, and a plugin reading "chat" must not have to
     * filter the interface out of it. Only the recogniser sees it, and it
     * matches exactly one pattern here. */
    if( RS_GameEvent_FromText(RS_GAME_EVENT_SRC_INTERFACE, text, &ev) )
        app_dispatch_game_event(app, &ev);
}

void
App_NotifyStatLevel(
    struct App* app,
    int skill,
    int base_level)
{
    struct RS_GameEvent ev;
    int previous;

    assert(app);
    if( skill < 0 || skill >= RS_PLAYER_STATS_SKILL_COUNT )
        return;

    /* Recorded whether or not anyone is listening: a plugin enabled halfway
     * through a session must not see its first stat update as a level-up. */
    previous = app->stats.last_seen_level[skill];
    app->stats.last_seen_level[skill] = base_level;

    if( !app->plugins )
        return;
    if( RS_GameEvent_FromStat(skill, previous, base_level, &ev) )
        app_dispatch_game_event(app, &ev);
}
