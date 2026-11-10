/*
 * The agent server's game verb (docs/AGENT_API.md): what a player would read
 * off the screen of a client that is in the game -- whether it is logged in,
 * where the local player stands, the right-click menu, the last chat lines,
 * and the dialogue's options.
 *
 * The tree has a driver that reads exactly these for quest tests
 * (src/plugin/torirs_plugin_drive*.c, docs/QUEST_DRIVER_DESIGN.md), but every
 * one of its units compiles to nothing without the embedded server
 * (TORIRS_EMBED_SERVER, off in an ordinary client build), so its readers
 * cannot be called from here. What they read is not the server's, though: it
 * is the App's own state. So this reads the same fields the same way, each
 * named beside its read:
 *
 *   the player's tile   App_LocalPlayerTiles (app.h, built everywhere)
 *   chat                app->chat.messages, as DriveState_Messages does
 *   dialogue options    the dialog_options_* roles, as DriveChat_Options does
 *   the npcs            app->world->entities.npc, as drive.npc_pose walks it,
 *                       each placed by App_NpcElementScreenPosition (app.h)
 *
 * What the driver gets FROM the embedded server -- server varps, the content
 * pack's symbols for a widget, an npc's dialogue text by component -- has no
 * source here, and the verb leaves it out rather than guess: there is no
 * "text" of the page of dialogue an npc is speaking.
 */

#include "agent/agent_verbs.h"

#if !defined(__EMSCRIPTEN__)

#include "app.h"
#include "engine/cache_provider.h"
#include "game/rs_chat.h"
#include "input/torirs_input.h"
#include "net/net.h"
#include "ui/uitree.h"
#include "ui/uitree_role.h"
#include "world/world.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Rows a chat menu can hold (the driver's DRIVE_CHAT_OPTIONS_ROW_MAX). */
#define AGENT_DIALOG_ROWS 5

/** The text of the node a revconfig role names, when the role is live and is
 *  a text node; else NULL. */
static const char*
agent_role_text(
    struct App* app,
    const char* role)
{
    const struct UITreeComponent* node;
    int32_t index;

    if( !app->tree )
        return NULL;
    index = UITree_RoleNodeByName(app->tree, &app->ui_roles, role);
    if( index < 0 )
        return NULL;
    node = &app->tree->components[index];
    if( node->type != UIELEM_RS_TEXT )
        return NULL;
    return node->u.rs_text.text ? node->u.rs_text.text : "";
}

static const char*
agent_screen_name(int screen)
{
    if( screen == APP_SCREEN_GAME )
        return "game";
    if( screen == APP_SCREEN_CONNECTING )
        return "connecting";
    if( screen == APP_SCREEN_TITLE )
        return "title";
    return "boot";
}

static enum AgentVerbResult
verb_game(struct AgentCtx* ctx)
{
    struct App* app = ctx->app;
    struct AgentJsonWriter* out;
    int chat_lines = 10;
    int true_x = 0;
    int true_z = 0;
    int level = 0;
    int dest_x = 0;
    int dest_z = 0;
    int flag_x = 0;
    int flag_z = 0;
    int draw_x = 0;
    int draw_z = 0;
    bool idle = false;

    if( Agent_ArgInt(ctx, "chat_lines", &chat_lines) < 0 )
        return AGENT_VERB_DONE;
    if( chat_lines < 0 || chat_lines > RS_CHAT_MESSAGE_MAX )
    {
        Agent_ReplyError(ctx, "\"chat_lines\" is 0..%d, not %d", RS_CHAT_MESSAGE_MAX, chat_lines);
        return AGENT_VERB_DONE;
    }

    out = Agent_ReplyBegin(ctx);
    /* In the game: a session is up and the gameframe is rooted. An offline
     * boot (no connection) never is. */
    AgentJsonW_KBool(out, "logged_in",
        app->net && app->net->state == TORIRS_NET_GAME && app->screen == APP_SCREEN_GAME);
    AgentJsonW_KStr(out, "screen", agent_screen_name(app->screen));
    AgentJsonW_KBool(out, "connected", app->net != NULL && app->net->state != TORIRS_NET_DISCONNECTED);

    /* Absent until the local player has spawned. */
    if( App_LocalPlayerTiles(app, &true_x, &true_z, &level, &dest_x, &dest_z, &flag_x, &flag_z, &draw_x, &draw_z) )
    {
        AgentJsonW_Key(out, "player");
        AgentJsonW_ObjBegin(out);
        AgentJsonW_KStr(out, "name", app->chat.username);
        /* The server's whole tile, not where the walking figure is drawn. */
        AgentJsonW_KInt(out, "x", true_x);
        AgentJsonW_KInt(out, "z", true_z);
        AgentJsonW_KInt(out, "level", level);
        if( App_LocalPlayerIdle(app, &idle) )
            AgentJsonW_KBool(out, "idle", idle);
        /* Where the walk in progress ends, when it is not here. */
        if( dest_x != true_x || dest_z != true_z )
        {
            int const dest[2] = { dest_x, dest_z };
            AgentJsonW_KInts(out, "walking_to", dest, 2);
        }
        AgentJsonW_ObjEnd(out);
    }

    AgentJsonW_Key(out, "minimenu");
    Agent_WriteMinimenu(ctx, out);

    /* Oldest first, the newest last, as the chat box reads. */
    AgentJsonW_Key(out, "chat");
    AgentJsonW_ArrBegin(out);
    {
        int const count = app->chat.message_count < chat_lines ? app->chat.message_count : chat_lines;
        for( int i = count - 1; i >= 0; i-- )
        {
            const struct RS_ChatMessage* message = &app->chat.messages[i];
            AgentJsonW_ObjBegin(out);
            AgentJsonW_KInt(out, "type", message->type);
            if( message->sender[0] )
                AgentJsonW_KStr(out, "sender", message->sender);
            AgentJsonW_KStr(out, "text", message->text);
            AgentJsonW_ObjEnd(out);
        }
    }
    AgentJsonW_ArrEnd(out);
    /* The cursor `log` (source chat) continues from. */
    AgentJsonW_KInt(out, "chat_cursor", app->chat.message_count > 0 ? app->chat.messages[0].serial : 0);

    /* A dialogue's menu: its title and its rows, when the first two rows are
     * both live (one alone can answer mid-rebuild; the driver's readiness
     * floor). Absent when no menu is up. */
    if( agent_role_text(app, "dialog_options_row_1") && agent_role_text(app, "dialog_options_row_2") )
    {
        const char* title = agent_role_text(app, "dialog_options_title");

        AgentJsonW_Key(out, "dialog");
        AgentJsonW_ObjBegin(out);
        if( title )
            AgentJsonW_KStr(out, "title", title);
        AgentJsonW_Key(out, "options");
        AgentJsonW_ArrBegin(out);
        for( int row = 1; row <= AGENT_DIALOG_ROWS; row++ )
        {
            char role[40];
            const char* text;

            snprintf(role, sizeof(role), "dialog_options_row_%d", row);
            text = agent_role_text(app, role);
            if( !text )
                break;
            AgentJsonW_Str(out, text);
        }
        AgentJsonW_ArrEnd(out);
        AgentJsonW_ObjEnd(out);
    }
    Agent_ReplyEnd(ctx);
    return AGENT_VERB_DONE;
}

/* ---- npcs ----------------------------------------------------------------------
 *
 * An npc wanders, so a point read off a screenshot is stale by the time it is
 * clicked: on 2026-11-09 a quest could only be played from outside by sweeping
 * the pointer over the scene and reading the hover text until it named the
 * npc. `npcs` lists the npcs the client has, each with its tile and where its
 * body is drawn now; `npc_click` finds one by name and presses on it in the
 * same breath, and refuses instead of pressing the ground when it walked off.
 */

/** Rows `npcs` gives unless asked for more, and the most it will. */
#define AGENT_NPCS_DEFAULT 64
#define AGENT_NPCS_MAX 512
/** Frames the pointer rests on an npc before the press (the game picks what
 *  is under the pointer on a frame of its own). */
#define AGENT_NPC_HOVER_FRAMES 3
/** How far an npc's body may move under the resting pointer and still be
 *  what the press lands on. */
#define AGENT_NPC_MOVED_PX 14

enum
{
    NC_ELEMENT,
    NC_SLOT,
    NC_ID,
    NC_PX,
    NC_PY,
    NC_TILE_X,
    NC_TILE_Z,
    NC_LEVEL,
};

/** An npc's name as its menu rows read: the entity's own, else its type's,
 *  with the <col=...> tags taken out. */
static void
agent_npc_name(
    struct App* app,
    const struct WorldEntity_NPC* npc,
    char* out,
    size_t out_size)
{
    const char* name = npc->name;
    size_t used = 0;
    int in_tag = 0;

    assert(out_size > 0);
    if( !name[0] && app->provider )
    {
        struct ToriRS_Npctype const* type = CacheProvider_NpctypeGet(app->provider, npc->npc_id);
        if( type )
            name = type->name;
    }
    for( const char* c = name; *c && used + 1 < out_size; c++ )
    {
        if( *c == '<' )
            in_tag = 1;
        else if( *c == '>' && in_tag )
            in_tag = 0;
        else if( !in_tag )
            out[used++] = *c;
    }
    out[used] = '\0';
}

/** 1 when `text` holds `part`, letter case aside. An empty `part` is in
 *  everything. */
static int
agent_text_holds(
    const char* text,
    const char* part)
{
    size_t const n = strlen(part);

    if( n == 0 )
        return 1;
    for( ; *text; text++ )
    {
        size_t i = 0;
        while( i < n && text[i] && tolower((unsigned char)text[i]) == tolower((unsigned char)part[i]) )
            i++;
        if( i == n )
            return 1;
    }
    return 0;
}

/** 1 when `npc` is one the request names: by `id` when one was given (>= 0),
 *  by `name` when one was (non-NULL), every synced npc with neither. */
static int
agent_npc_wanted(
    struct App* app,
    const struct WorldEntity_NPC* npc,
    const char* name,
    int id)
{
    char plain[64];

    if( npc->server_slot < 0 )
        return 0;
    if( id >= 0 && npc->npc_id != id && npc->base_npc_id != id )
        return 0;
    if( !name )
        return 1;
    agent_npc_name(app, npc, plain, sizeof(plain));
    return agent_text_holds(plain, name);
}

static enum AgentVerbResult
verb_npcs(struct AgentCtx* ctx)
{
    struct App* app = ctx->app;
    struct AgentJsonWriter* out;
    struct World_EntityPool* pool;
    const char* name = NULL;
    int id = -1;
    int limit = AGENT_NPCS_DEFAULT;
    int matched = 0;
    int written = 0;

    /* "npc_id", not "id": that member is the request's own number. */
    if( Agent_ArgString(ctx, "name", &name) < 0 || Agent_ArgInt(ctx, "npc_id", &id) < 0
        || Agent_ArgInt(ctx, "limit", &limit) < 0 )
        return AGENT_VERB_DONE;
    if( limit < 1 || limit > AGENT_NPCS_MAX )
    {
        Agent_ReplyError(ctx, "\"limit\" is 1..%d, not %d", AGENT_NPCS_MAX, limit);
        return AGENT_VERB_DONE;
    }
    if( !app->world )
    {
        Agent_ReplyError(ctx, "no world is loaded yet: wait until \"ready\" and ask again");
        return AGENT_VERB_DONE;
    }

    out = Agent_ReplyBegin(ctx);
    AgentJsonW_Key(out, "npcs");
    AgentJsonW_ArrBegin(out);
    pool = &app->world->entities.npc;
    /* The npc entity pool (a few hundred), walked once per request. */
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC const* npc = World_EntityPoolGet(pool, i);
        char plain[64];
        int px = 0;
        int py = 0;
        int on_screen;

        if( !npc || !agent_npc_wanted(app, npc, name, id) )
            continue;
        matched++;
        if( written >= limit )
            continue;
        written++;
        agent_npc_name(app, npc, plain, sizeof(plain));
        on_screen = App_NpcElementScreenPosition(app, npc->element_id, &px, &py) >= 0;
        AgentJsonW_ObjBegin(out);
        AgentJsonW_KInt(out, "slot", npc->server_slot);
        AgentJsonW_KInt(out, "id", npc->npc_id);
        AgentJsonW_KStr(out, "name", plain);
        AgentJsonW_KInt(out, "x", app->world->_base_tile_x + npc->grid_position.x);
        AgentJsonW_KInt(out, "z", app->world->_base_tile_z + npc->grid_position.z);
        AgentJsonW_KInt(out, "level", npc->grid_position.level);
        /* Where its body is drawn now, when that is in the World view and
         * nothing of the client's is drawn over it. */
        if( on_screen )
        {
            int const point[2] = { px, py };
            AgentJsonW_KInts(out, "screen", point, 2);
        }
        AgentJsonW_KBool(out, "visible", on_screen && !App_ChromePointerOwned(app, px, py));
        AgentJsonW_Key(out, "ops");
        AgentJsonW_ArrBegin(out);
        for( int op = 0; op < 5; op++ )
            if( npc->actions[op].name[0] && (npc->visible_ops & (1u << op)) )
                AgentJsonW_Str(out, npc->actions[op].name);
        AgentJsonW_ArrEnd(out);
        AgentJsonW_ObjEnd(out);
    }
    AgentJsonW_ArrEnd(out);
    AgentJsonW_KInt(out, "count", matched);
    if( matched > written )
        AgentJsonW_KBool(out, "truncated", 1);
    Agent_ReplyEnd(ctx);
    return AGENT_VERB_DONE;
}

/** The script's CALL before the press: the npc must still be under the
 *  resting pointer. */
static int
npc_click_check(
    struct AgentCtx* ctx,
    struct AgentOp* op,
    int value)
{
    int x = 0;
    int y = 0;

    (void)value;
    if( App_NpcElementScreenPosition(ctx->app, op->ints[NC_ELEMENT], &x, &y) < 0 )
    {
        Agent_ReplyError(ctx, "the npc left the view before the press (nothing was clicked): ask again");
        return 0;
    }
    if( abs(x - op->ints[NC_PX]) > AGENT_NPC_MOVED_PX || abs(y - op->ints[NC_PY]) > AGENT_NPC_MOVED_PX )
    {
        Agent_ReplyError(ctx, "the npc walked from (%d, %d) to (%d, %d) before the press (nothing was clicked): ask "
                              "again",
            op->ints[NC_PX], op->ints[NC_PY], x, y);
        return 0;
    }
    return 1;
}

static void
npc_click_finish(
    struct AgentCtx* ctx,
    struct AgentOp* op)
{
    struct AgentJsonWriter* out = Agent_ReplyBegin(ctx);

    AgentJsonW_Key(out, "npc");
    AgentJsonW_ObjBegin(out);
    AgentJsonW_KInt(out, "slot", op->ints[NC_SLOT]);
    AgentJsonW_KInt(out, "id", op->ints[NC_ID]);
    AgentJsonW_KStr(out, "name", op->text);
    AgentJsonW_KInt(out, "x", op->ints[NC_TILE_X]);
    AgentJsonW_KInt(out, "z", op->ints[NC_TILE_Z]);
    AgentJsonW_KInt(out, "level", op->ints[NC_LEVEL]);
    AgentJsonW_ObjEnd(out);
    AgentJsonW_KInt(out, "x", op->ints[NC_PX]);
    AgentJsonW_KInt(out, "y", op->ints[NC_PY]);
    /* A right click opens the menu: its rows, for ui_click to press one. */
    AgentJsonW_Key(out, "minimenu");
    Agent_WriteMinimenu(ctx, out);
    Agent_ReplyEnd(ctx);
}

static enum AgentVerbResult
verb_npc_click(struct AgentCtx* ctx)
{
    struct App* app = ctx->app;
    struct World_EntityPool* pool;
    struct WorldEntity_NPC const* best = NULL;
    struct WorldEntity_NPC const* off_screen = NULL;
    struct AgentOp* op;
    const char* name = NULL;
    unsigned mods = 0;
    int button = TORIRSM_LEFT;
    int id = -1;
    int slot = -1;
    int best_x = 0;
    int best_y = 0;
    long best_distance = 0;
    int centre_x;
    int centre_y;

    if( Agent_ArgString(ctx, "name", &name) < 0 || Agent_ArgInt(ctx, "npc_id", &id) < 0
        || Agent_ArgInt(ctx, "slot", &slot) < 0 || Agent_ArgButton(ctx, &button) < 0
        || Agent_ArgMods(ctx, &mods) < 0 )
        return AGENT_VERB_DONE;
    if( !name && id < 0 && slot < 0 )
    {
        Agent_ReplyError(ctx, "say which npc: \"name\" (part of it will do), \"npc_id\" (its type) or \"slot\" "
                              "(one copy, from npcs)");
        return AGENT_VERB_DONE;
    }
    if( !app->world )
    {
        Agent_ReplyError(ctx, "no world is loaded yet: wait until \"ready\" and ask again");
        return AGENT_VERB_DONE;
    }

    /* Of the copies on screen, the one nearest the middle of the view: a body
     * at the edge is mostly clipped (App_NpcScreenPosition's rule). */
    centre_x = app->world_emit_desc.x + app->world_emit_desc.w / 2;
    centre_y = app->world_emit_desc.y + app->world_emit_desc.h / 2;
    pool = &app->world->entities.npc;
    /* The npc entity pool (a few hundred), walked once per request. */
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC const* npc = World_EntityPoolGet(pool, i);
        long distance;
        int x = 0;
        int y = 0;

        if( !npc || !agent_npc_wanted(app, npc, name, id) || (slot >= 0 && npc->server_slot != slot) )
            continue;
        if( App_NpcElementScreenPosition(app, npc->element_id, &x, &y) < 0 || App_ChromePointerOwned(app, x, y) )
        {
            off_screen = npc;
            continue;
        }
        distance = (long)(x - centre_x) * (x - centre_x) + (long)(y - centre_y) * (y - centre_y);
        if( best && distance >= best_distance )
            continue;
        best = npc;
        best_distance = distance;
        best_x = x;
        best_y = y;
    }
    if( !best )
    {
        if( off_screen )
            Agent_ReplyError(ctx, "that npc is at tile %d,%d level %d but not in view (or a window covers it): walk "
                                  "or turn the camera to it, then ask again",
                app->world->_base_tile_x + off_screen->grid_position.x,
                app->world->_base_tile_z + off_screen->grid_position.z, off_screen->grid_position.level);
        else
            Agent_ReplyError(ctx, "no such npc is in the scene: npcs lists the ones that are");
        return AGENT_VERB_DONE;
    }

    op = Agent_ScriptArm(ctx, npc_click_check, npc_click_finish);
    op->ints[NC_ELEMENT] = best->element_id;
    op->ints[NC_SLOT] = best->server_slot;
    op->ints[NC_ID] = best->npc_id;
    op->ints[NC_PX] = best_x;
    op->ints[NC_PY] = best_y;
    op->ints[NC_TILE_X] = app->world->_base_tile_x + best->grid_position.x;
    op->ints[NC_TILE_Z] = app->world->_base_tile_z + best->grid_position.z;
    op->ints[NC_LEVEL] = best->grid_position.level;
    agent_npc_name(app, best, op->text, sizeof(op->text));
    Agent_ScriptModsDown(ctx, op, mods);
    Agent_ScriptMove(op, best_x, best_y);
    Agent_ScriptFrames(op, AGENT_NPC_HOVER_FRAMES);
    /* The press goes down only while the npc is still under the pointer. */
    Agent_ScriptCall(op, 0);
    Agent_ScriptDown(op, button, best_x, best_y);
    Agent_ScriptFrames(op, 1);
    Agent_ScriptUp(op, button, best_x, best_y);
    Agent_ScriptModsUp(ctx, op, mods);
    return AGENT_VERB_PENDING;
}

const struct AgentVerb g_agent_verbs_game[] = {
    { "game", "chat_lines", 0, verb_game,
        "The game as a player reads it: logged in, the local player's tile, the right-click menu, the last chat "
        "lines, a dialogue's options." },
    { "npcs", "name npc_id limit", 0, verb_npcs,
        "The npcs the client has: name, type id, tile, where each is drawn now, and its menu options." },
    { "npc_click", "name npc_id slot button mods", AGENT_VERB_INPUT, verb_npc_click,
        "Click an npc by name, type id or slot, where it stands this frame; refuses if it walked off first." },
    { NULL, NULL, 0, NULL, NULL },
};

#else /* __EMSCRIPTEN__ */

const struct AgentVerb g_agent_verbs_game[] = {
    { NULL, NULL, 0, NULL, NULL },
};

#endif
