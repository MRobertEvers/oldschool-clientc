#include "app.h"

#include "bootmanifest/bootmanifest.h"

#include "cmd/cmdbus.h"
#include "cs2vm2/cs2vm2.h"
#include "engine/entity_model_build.h"
#include "engine/task_obj_model_load.h"
#include "net/jbase37.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat1/dat1_tasks.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/player_appearance.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/uitree_builder/task_interface_open.h"
#include "engine/uitree_cmd_render.h"
#include "engine/world_builder/task_world_load.h"
#include "engine/world_builder/world_builder.h"
#include "game/rs_clientcode.h"
#include "game/rs_cs2_dispatch.h"
#include "game/rs_gameproto_exec.h"
#include "game/rs_minimenu_build.h"
#include "game/task_gameproto_exec.h"
#include "net/net.h"
#include "net/net_out.h"
#include "net/rev/gameproto_parse.h"
#include "game/rs_worldmap.h"
#include "game/rs_minimenu_cross.h"
#include "game/task_cs1_run.h"
#include "input/torirs_input_cmd.h"
#include "input/torirs_keymap.h"
#include "painters/painters.h"
#include "platform/platform_sdl2_renderer_soft3d.h"
#include "render/torirs_frame.h"
#include "render/torirs_pick.h"
#include "toridraw.h"
#include "ui/uitree_layout.h"
#include "world/world.h"

#include "bmp.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    APP_LOGIC_TICK_MS = 20,
    APP_MAX_CATCHUP_TICKS = 5,
    /* Mouseover text origin inside the viewport. The reference container puts
     * its text child at (0,0); the classic client drew the same line at
     * (4, 15) — one padded cell in, with the baseline a line down. Ours is a
     * text box, so the baseline offset comes from the font ascent. */
    APP_HOVERTEXT_INSET_X = 4,
    APP_HOVERTEXT_INSET_Y = 2,
};

static struct RS_ChatFilters
app_chat_filters(struct App const* app)
{
    struct RS_ChatFilters filters = {
        .public_mode = app->slots.chat_filter_mode[RS_UI_CHAT_FILTER_PUBLIC],
        .private_mode = app->slots.chat_filter_mode[RS_UI_CHAT_FILTER_PRIVATE],
        .trade_mode = app->slots.chat_filter_mode[RS_UI_CHAT_FILTER_TRADE],
        .social = &app->social,
    };
    return filters;
}

/* Chat node geometry + font, resolved through the slot index so nothing here
 * names coordinates or interface ids. Returns 0 when no chat region exists. */
static int
app_chat_region(
    struct App const* app,
    int* out_x,
    int* out_y,
    int* out_font_id)
{
    struct UITreeComponent const* node;
    int x = 0, y = 0, w = 0, h = 0;

    if( app->slots.chat_index < 0 )
        return 0;
    node = &app->tree->components[app->slots.chat_index];
    UITree_LayoutGetBounds(&node->position, &x, &y, &w, &h);
    if( out_x )
        *out_x = x;
    if( out_y )
        *out_y = y;
    if( out_font_id )
        *out_font_id = node->u.chat.font_id > 0 ? node->u.chat.font_id : 1;
    return 1;
}

/* True when a canvas-space point lands inside the chat region's bounds. Used to
 * decide chat input focus on a left click. */
static int
app_point_in_chat(struct App const* app, int x, int y)
{
    struct UITreeComponent const* node;
    int bx = 0, by = 0, bw = 0, bh = 0;

    if( app->slots.chat_index < 0 )
        return 0;
    node = &app->tree->components[app->slots.chat_index];
    UITree_LayoutGetBounds(&node->position, &bx, &by, &bw, &bh);
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

/* Rebuild the flattened chat draw model (called before every emit). */
static void
app_chat_build_view(struct App* app)
{
    struct RS_ChatFilters filters = app_chat_filters(app);
    int font_id = 1;

    if( !app_chat_region(app, NULL, NULL, &font_id) )
    {
        memset(&app->chat_view, 0, sizeof(app->chat_view));
        return;
    }
    RS_Chat_BuildView(
        &app->chat,
        &filters,
        &app->ui_host,
        font_id,
        app->slots.chat_com_id != -1,
        &app->chat_view);
}

/* RS_MinimenuChatSource seam: sender under a canvas-space click. */
static int
app_chat_line_at(
    void* user,
    int x,
    int y,
    char* out_sender,
    int sender_cap,
    int* out_chat_type)
{
    struct App* app = (struct App*)user;
    struct RS_ChatFilters filters = app_chat_filters(app);
    int rx = 0;
    int ry = 0;

    if( !app_chat_region(app, &rx, &ry, NULL) )
        return 0;
    return RS_Chat_LineAt(
        &app->chat, &filters, x - rx, y - ry, out_sender, sender_cap, out_chat_type);
}

static void
app_send_if_button(void* user, int com_id);
static void
app_send_resume_pausebutton(void* user, int com_id);
static void
app_send_close_modal(void* user);
static void
app_world_bind_pending_seqs(struct App* app);
static void
app_world_sync_entity_animations(struct App* app);
static void
app_world_sync_entity_spotanims(struct App* app);
static void
app_entity_spotanim_drop(struct App* app, int body_element_id);

/* Send an outbound packet built by a net_out_* builder, gated on networking.
 * The builder writes into a scratch buffer using the game out-cipher; the
 * bytes then queue to the socket via the subsystem's SEND_DATA ring. */
#define APP_NET_SEND(app, builder_call)                                                          \
    do                                                                                           \
    {                                                                                            \
        if( (app)->net && (app)->net->state == TORIRS_NET_GAME )                                 \
        {                                                                                        \
            uint8_t _nsbuf[512];                                                                 \
            int _nslen = builder_call;                                                           \
            if( _nslen > 0 )                                                                     \
                ToriRS_Network_SendRaw((app)->net, _nsbuf, _nslen);                              \
        }                                                                                        \
    } while( 0 )

/* Server-synced local player entity (esync pid), or NULL (offline / not yet
 * spawned). Shared by camera follow, minimap centering, and roof check. */
static struct WorldEntity_Player*
app_local_player(struct App* app)
{
    int world_idx;
    if( !app->world )
        return NULL;
    if( !RS_EntitySync_FindPlayer(
            &app->esync,
            app->esync.local_pid >= 0 ? app->esync.local_pid : 2047,
            &world_idx,
            NULL) )
        return NULL;
    return World_EntityPoolGet(&app->world->entities.player, world_idx);
}

/* One reference minimapDrawDot: rotate the entity's player-relative offset by
 * the camera yaw into widget pixels (4 px/tile => fine units / 32), cull past
 * the ring (dist^2 > 6400), store the sprite's top-left center-relative. */
static void
app_minimap_push_dot(
    struct App* app,
    int rel_fx,
    int rel_fz,
    int scene_id,
    int atlas_index)
{
    int dx = rel_fx / 32;
    int dy = rel_fz / 32;
    int yaw, x, y;
    int w = 4, h = 4;
    struct UITreeMinimapDot* dot;

    if( app->minimap_dot_count >=
        (int)(sizeof(app->minimap_dots) / sizeof(app->minimap_dots[0])) )
        return;
    if( dx * dx + dy * dy > 6400 )
        return;
    yaw = ToriDraw_NormalizeAngle(app->world_camera.yaw);
    {
        int sin = ToriDraw_Sin(yaw);
        int cos = ToriDraw_Cos(yaw);
        x = (dy * sin + dx * cos) >> 16;
        y = (dy * cos - dx * sin) >> 16;
    }
    {
        int count = 0;
        struct ToriDraw_Sprite** frames = ToriDraw_SceneSpriteGet(app->scene, scene_id, &count);
        if( frames && atlas_index >= 0 && atlas_index < count && frames[atlas_index] )
        {
            w = frames[atlas_index]->width;
            h = frames[atlas_index]->height;
        }
    }
    dot = &app->minimap_dots[app->minimap_dot_count++];
    dot->dx = x - w / 2;
    dot->dy = -y - h / 2;
    dot->w = w;
    dot->h = h;
    dot->scene_id = scene_id;
    dot->atlas_index = atlas_index;
    dot->color = 0;
}

/* Reference minimapDraw overlay: ground objs (yellow), NPCs, other players
 * (white), the destination flag, then the local-player 3x3 white square.
 * mapdots frames: 0 obj, 1 npc, 2 player, 3 friend; mapmarker frame 0 flag. */
static int
app_minimap_build_dots(
    struct App* app,
    struct UITreeMinimapDot const** out_dots)
{
    struct WorldEntity_Player* local = app_local_player(app);
    struct World* world = app->world;
    struct World_EntityPool* pool;
    int px, pz;
    int dots_scene, marker_scene;

    app->minimap_dot_count = 0;
    *out_dots = app->minimap_dots;
    if( !world || !world->load_complete || !local )
        return 0;
    px = (int)local->draw_position.x;
    pz = (int)local->draw_position.z;
    dots_scene = UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPDOTS);
    marker_scene = UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPMARKER);

    /* Loc mapfunction icons first, so entity dots draw on top (reference
     * minimapDraw order). Gathered at scene build into world->mapfuncs. */
    {
        int mapfunc_scene =
            UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPFUNCTION);
        if( mapfunc_scene > 0 )
        {
            for( int i = 0; i < world->mapfunc_count; i++ )
            {
                struct World_MapFunctionIcon const* icon = &world->mapfuncs[i];
                if( icon->level != local->grid_position.level )
                    continue;
                app_minimap_push_dot(
                    app,
                    icon->x * 128 + 64 - px,
                    icon->z * 128 + 64 - pz,
                    mapfunc_scene,
                    icon->func);
            }
        }
    }

    if( dots_scene > 0 )
    {
        pool = &world->entities.obj_stack;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, i);
            if( !stack || stack->grid_position.level != local->grid_position.level )
                continue;
            app_minimap_push_dot(
                app,
                stack->grid_position.x * 128 + 64 - px,
                stack->grid_position.z * 128 + 64 - pz,
                dots_scene,
                0);
        }
        pool = &world->entities.npc;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
            /* Reference gates on NpcType.minimap; the flag is not decoded
             * into ToriRS_Npctype yet, so every NPC dot draws. */
            if( !npc || npc->grid_position.level != local->grid_position.level )
                continue;
            app_minimap_push_dot(
                app,
                (int)npc->draw_position.x - px,
                (int)npc->draw_position.z - pz,
                dots_scene,
                1);
        }
        pool = &world->entities.player;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
            if( !player || player == local ||
                player->grid_position.level != local->grid_position.level )
                continue;
            app_minimap_push_dot(
                app,
                (int)player->draw_position.x - px,
                (int)player->draw_position.z - pz,
                dots_scene,
                2);
        }
    }

    if( marker_scene > 0 && app->minimap_flag_x >= 0 )
        app_minimap_push_dot(
            app,
            app->minimap_flag_x * 128 + 64 - px,
            app->minimap_flag_z * 128 + 64 - pz,
            marker_scene,
            0);

    /* Local player: white 3x3 square at the widget center (fillRect 97,78). */
    if( app->minimap_dot_count <
        (int)(sizeof(app->minimap_dots) / sizeof(app->minimap_dots[0])) )
    {
        struct UITreeMinimapDot* dot = &app->minimap_dots[app->minimap_dot_count++];
        dot->dx = -1;
        dot->dy = -1;
        dot->w = 3;
        dot->h = 3;
        dot->scene_id = 0;
        dot->atlas_index = 0;
        dot->color = 0xFFFFFFFFu;
    }
    return app->minimap_dot_count;
}

/* ---------------------------------------------------------------------- */
/* Entity overlays: health bars + hitsplats (reference drawEntities)       */
/* ---------------------------------------------------------------------- */

/* Defined with the other cache/scene helpers further down. */
static int
app_world_height(void* userdata, int world_x, int world_z, int level);
static int
app_hitsplat_font_scene_id(struct App* app);
static int
app_minimenu_font_scene_id(struct App* app);

/*
 * Reference getOverlayPos (Client.ts:5253): rotate the entity's
 * camera-relative fine offset by yaw then pitch and divide by depth.
 * `<< 9` is the same UNIT_SCALE_SHIFT the 3D raster projects with, and
 * `origin` is the viewport centre. Returns 0 when the point is behind the
 * near plane (reference sets projectX = -1) or off the map.
 */
static int
app_world_project(
    struct App* app,
    int fine_x,
    int fine_z,
    int height_above_ground,
    int* out_x,
    int* out_y)
{
    int dx, dy, dz, tmp;
    int sin_pitch, cos_pitch, sin_yaw, cos_yaw;
    int ground_y;
    int level = 0;

    if( !app->world || !app->world_view_valid )
        return 0;
    if( fine_x < 128 || fine_z < 128 )
        return 0;
    {
        struct WorldEntity_Player* local = app_local_player(app);
        if( local )
            level = local->grid_position.level;
    }
    ground_y = app_world_height(app, fine_x, fine_z, level);

    dx = fine_x - app->world_camera_pos.x;
    dy = (ground_y - height_above_ground) - app->world_camera_pos.y;
    dz = fine_z - app->world_camera_pos.z;

    sin_pitch = ToriDraw_Sin(app->world_camera.pitch);
    cos_pitch = ToriDraw_Cos(app->world_camera.pitch);
    sin_yaw = ToriDraw_Sin(app->world_camera.yaw);
    cos_yaw = ToriDraw_Cos(app->world_camera.yaw);

    tmp = (dz * sin_yaw + dx * cos_yaw) >> 16;
    dz = (dz * cos_yaw - dx * sin_yaw) >> 16;
    dx = tmp;

    tmp = (dy * cos_pitch - dz * sin_pitch) >> 16;
    dz = (dy * sin_pitch + dz * cos_pitch) >> 16;
    dy = tmp;

    if( dz < 50 )
        return 0;

    *out_x = app->world_emit_desc.x + app->world_emit_desc.w / 2 + ((dx << 9) / dz);
    *out_y = app->world_emit_desc.y + app->world_emit_desc.h / 2 + ((dy << 9) / dz);
    return 1;
}

/* Reference ClientEntity.height = model.minY, which Client-TS accumulates as
 * `max(-vertexY)` — a POSITIVE magnitude measuring up from the model origin.
 * ToriDraw's bounds cylinder stores the true minimum instead (negative, since
 * up is -y), so it has to be negated here. Getting this wrong collapses the
 * health bar onto the entity's feet. */
static int
app_entity_model_height(
    struct App* app,
    int element_id)
{
    struct ToriDraw_SceneElement* el;
    struct ToriDraw_BoundsCylinder* bounds;

    if( element_id < 0 || !app->scene || !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return 0;
    el = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !el || el->model.kind != TORIDRAWMK_MODEL )
        return 0;
    bounds = ToriDraw_ModelGetBoundsCylinder(el->model);
    return bounds ? -bounds->min_y : 0;
}

static void
app_overlay_push(
    struct App* app,
    struct UITreeEntityOverlay const* item)
{
    int cap = (int)(sizeof(app->entity_overlays) / sizeof(app->entity_overlays[0]));
    if( app->entity_overlay_count >= cap )
        return;
    app->entity_overlays[app->entity_overlay_count++] = *item;
}

/* One entity's overlay set. combat/damage state lives on the shared facet, so
 * players and NPCs go through the same body (reference drawEntities treats
 * them identically). */
/* Resolve a reference chatColour/chatTimer pair to an ARGB colour. Static
 * palette entries (< 6) map straight through; flashing/rainbow effects (6-11)
 * animate off the scene cycle / remaining timer (reference Client.ts:4962). */
static uint32_t
app_overlay_chat_colour(struct App* app, int chat_colour, int timer)
{
    static const int CHAT_COLOURS[6] = {
        0xffff00, /* YELLOW */
        0xff0000, /* RED */
        0x00ff00, /* GREEN */
        0x00ffff, /* CYAN */
        0xff00ff, /* MAGENTA */
        0xffffff, /* WHITE */
    };
    int cyc = app->world ? app->world->cycle : 0;
    int rgb = 0xffff00;
    int delta = 150 - timer;

    if( chat_colour >= 0 && chat_colour < 6 )
        rgb = CHAT_COLOURS[chat_colour];
    else if( chat_colour == 6 )
        rgb = (cyc % 20 < 10) ? 0xff0000 : 0xffff00;
    else if( chat_colour == 7 )
        rgb = (cyc % 20 < 10) ? 0x0000ff : 0x00ffff;
    else if( chat_colour == 8 )
        rgb = (cyc % 20 < 10) ? 0x00b000 : 0x80ff80;
    else if( chat_colour == 9 )
    {
        if( delta < 50 )
            rgb = delta * 1280 + 0xff0000;
        else if( delta < 100 )
            rgb = 0xffff00 - (delta - 50) * 327680;
        else if( delta < 150 )
            rgb = (delta - 100) * 5 + 0x00ff00;
    }
    else if( chat_colour == 10 )
    {
        if( delta < 50 )
            rgb = delta * 5 + 0xff0000;
        else if( delta < 100 )
            rgb = 0xff00ff - (delta - 50) * 327680;
        else if( delta < 150 )
            rgb = (delta - 100) * 327680 + 0x0000ff - (delta - 100) * 5;
    }
    else if( chat_colour == 11 )
    {
        if( delta < 50 )
            rgb = 0xffffff - delta * 327685;
        else if( delta < 100 )
            rgb = (delta - 50) * 327685 + 0x00ff00;
        else if( delta < 150 )
            rgb = 0xffffff - (delta - 100) * 327680;
    }
    return 0xff000000u | (uint32_t)(rgb & 0xffffff);
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
    int font_id)
{
    int height = app_entity_model_height(app, element_id);
    int screen_x, screen_y;

    if( !chat || chat->timer <= 0 || chat->message[0] == '\0' || font_id < 0 )
        return;
    if( !app_world_project(
            app, (int)draw_position->x, (int)draw_position->z, height, &screen_x, &screen_y) )
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
 * `headicons` is an 8-bit mask; each set bit plots sprite[icon] from the
 * headicons pack stacked upward above the model top (start 30px up, 25px per
 * icon). Projection is at `entity.height + 15`, same as the health bar. */
static void
app_overlay_build_player_headicons(
    struct App* app,
    int element_id,
    int headicons,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    int headicons_scene)
{
    int height = app_entity_model_height(app, element_id);
    int screen_x, screen_y;
    int y_off = 30;

    if( headicons == 0 || headicons_scene <= 0 )
        return;
    if( !app_world_project(
            app,
            (int)draw_position->x,
            (int)draw_position->z,
            height + 15,
            &screen_x,
            &screen_y) )
        return;

    for( int icon = 0; icon < 8; icon++ )
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

static void
app_overlay_build_entity(
    struct App* app,
    int element_id,
    struct WorldEntityFacet_Combat const* combat,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    int font_id,
    int hitmarks_scene)
{
    int cycle = app->world->cycle;
    int height = app_entity_model_height(app, element_id);
    int screen_x, screen_y;

    /* Health bar: 30px wide, green/red split, 15px above the model top.
     * `combatCycle > loopCycle + 100` is the reference's "recently in combat"
     * window (set to loopCycle + 400 on every hit). */
    if( combat->combat_cycle > cycle + 100 && combat->total_health > 0 &&
        app_world_project(
            app,
            (int)draw_position->x,
            (int)draw_position->z,
            height + 15,
            &screen_x,
            &screen_y) )
    {
        int filled = (combat->health * 30) / combat->total_health;
        if( filled > 30 )
            filled = 30;
        if( filled < 0 )
            filled = 0;
        struct UITreeEntityOverlay bar = {
            .kind = UITREE_ENTITY_OVERLAY_RECT,
            .x = screen_x - 15,
            .y = screen_y - 3,
            .w = filled,
            .h = 5,
            .color = 0xFF00FF00u, /* Colour.GREEN */
        };
        app_overlay_push(app, &bar);
        bar.x = screen_x - 15 + filled;
        bar.w = 30 - filled;
        bar.color = 0xFFFF0000u; /* Colour.RED */
        app_overlay_push(app, &bar);
    }

    /* Hitsplats: up to 4 concurrent, each alive for 70 cycles, positioned by
     * slot (reference nudges slots 1-3 off the centre). */
    for( int i = 0; i < WORLD_ENTITY_DAMAGE_SLOTS; i++ )
    {
        char text[UITREE_ENTITY_OVERLAY_TEXT_LEN];

        if( combat->damage_cycles[i] <= cycle )
            continue;
        if( !app_world_project(
                app,
                (int)draw_position->x,
                (int)draw_position->z,
                height / 2,
                &screen_x,
                &screen_y) )
            continue;

        if( i == 1 )
            screen_y -= 20;
        else if( i == 2 )
        {
            screen_x -= 15;
            screen_y -= 10;
        }
        else if( i == 3 )
        {
            screen_x += 15;
            screen_y -= 10;
        }

        if( hitmarks_scene > 0 )
        {
            struct UITreeEntityOverlay spr = {
                .kind = UITREE_ENTITY_OVERLAY_SPRITE,
                .x = screen_x - 12,
                .y = screen_y - 12,
                .w = 0,
                .h = 0,
                .scene_id = hitmarks_scene,
                .atlas_index = combat->damage_types[i],
            };
            app_overlay_push(app, &spr);
        }
        snprintf(text, sizeof(text), "%d", combat->damage_values[i]);
        if( font_id >= 0 )
        {
            /* Black shadow then white, offset by one px — reference draws the
             * number twice (Client.ts:4931-4932). */
            struct UITreeEntityOverlay num = {
                .kind = UITREE_ENTITY_OVERLAY_TEXT,
                .x = screen_x,
                .y = screen_y + 4,
                .font_id = font_id,
                .color = 0xFF000000u,
            };
            snprintf(num.text, sizeof(num.text), "%s", text);
            app_overlay_push(app, &num);
            num.x = screen_x - 1;
            num.y = screen_y + 3;
            num.color = 0xFFFFFFFFu;
            app_overlay_push(app, &num);
        }
    }
}

static int
app_build_entity_overlays(
    struct App* app,
    struct UITreeEntityOverlay const** out_items)
{
    struct World* world = app->world;
    struct World_EntityPool* pool;
    int font_id;
    int hitmarks_scene;

    app->entity_overlay_count = 0;
    *out_items = app->entity_overlays;
    if( !world || !world->load_complete || !app->world_view_valid )
        return 0;

    font_id = app_hitsplat_font_scene_id(app);
    hitmarks_scene = UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_HITMARKS);
    int headicons_scene =
        UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_HEADICONS);
    pool = &world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        if( !npc || npc->element_id < 0 )
            continue;
        app_overlay_build_entity(
            app, npc->element_id, &npc->combat, &npc->draw_position, font_id, hitmarks_scene);
    }

    pool = &world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
        if( !player || player->element_id < 0 )
            continue;
        app_overlay_build_entity(
            app, player->element_id, &player->combat, &player->draw_position, font_id,
            hitmarks_scene);
        app_overlay_build_player_headicons(
            app, player->element_id, player->headicon, &player->draw_position, headicons_scene);
    }

    /* Overhead chat is a second pass so it layers above every entity's health
     * bar and hitsplats (reference draws chatX/chatY after the entity loop).
     * It uses b12, the bold chat font, not the p11 hitsplat font. */
    {
        int chat_font = app_minimenu_font_scene_id(app);

        pool = &world->entities.npc;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
            if( !npc || npc->element_id < 0 )
                continue;
            app_overlay_build_chat(
                app, npc->element_id, &npc->chat, &npc->draw_position, chat_font);
        }

        pool = &world->entities.player;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
            if( !player || player->element_id < 0 )
                continue;
            app_overlay_build_chat(
                app, player->element_id, &player->chat, &player->draw_position, chat_font);
        }
    }

    /* TORIRS_OVERLAY_DEBUG=1: the primitives this frame, plus the two assets
     * they need — a missing p11 (font -1) or hitmarks pack is the usual
     * reason a hit lands but nothing is drawn. */
    if( getenv("TORIRS_OVERLAY_DEBUG") && app->entity_overlay_count > 0 )
    {
        fprintf(
            stderr,
            "overlay: %d items font=%d hitmarks=%d\n",
            app->entity_overlay_count,
            font_id,
            hitmarks_scene);
        for( int i = 0; i < app->entity_overlay_count; i++ )
        {
            struct UITreeEntityOverlay const* item = &app->entity_overlays[i];
            fprintf(
                stderr,
                "  overlay[%d] kind=%d at %d,%d %dx%d \"%s\"\n",
                i,
                item->kind,
                item->x,
                item->y,
                item->w,
                item->h,
                item->text);
        }
    }
    return app->entity_overlay_count;
}

static int
app_host_request(
    void* user,
    struct UITreeHostRequest* req)
{
    struct App* app = (struct App*)user;
    struct InvSlot slot;

    assert(req);
    assert(app);

    switch( req->kind )
    {
    case UITREE_HOST_GET_SCROLLBAR_SCENE:
        return UITreeSceneBridge_ScrollbarSceneId(&app->bridge);
    case UITREE_HOST_GET_STATIC_SPRITE_SCENE:
        return UITreeSceneBridge_StaticSpriteSceneId(
            &app->bridge, (enum StaticSpriteSlot)req->u.static_sprite.slot);
    case UITREE_HOST_GET_ENTITY_OVERLAYS:
        *req->u.get_entity_overlays.out_clip_x = app->world_emit_desc.x;
        *req->u.get_entity_overlays.out_clip_y = app->world_emit_desc.y;
        *req->u.get_entity_overlays.out_clip_w = app->world_emit_desc.w;
        *req->u.get_entity_overlays.out_clip_h = app->world_emit_desc.h;
        return app_build_entity_overlays(app, req->u.get_entity_overlays.out_items);
    case UITREE_HOST_GET_CROSS_ACTIVE:
        return UICross_IsActive(&app->cross) ? 1 : 0;
    case UITREE_HOST_GET_CROSS_ATLAS_FRAME:
        return UICross_AtlasFrame(&app->cross);
    case UITREE_HOST_GET_CROSS_POSITION:
        if( req->u.get_cross_position.out_x )
            *req->u.get_cross_position.out_x = app->cross.x;
        if( req->u.get_cross_position.out_y )
            *req->u.get_cross_position.out_y = app->cross.y;
        return 1;
    case UITREE_HOST_GET_MINIMENU_VISIBLE:
        return app->interact.minimenu.visible ? 1 : 0;
    case UITREE_HOST_GET_MINIMENU_STATE:
        assert(req->u.get_minimenu_state.out);
        *req->u.get_minimenu_state.out = &app->interact.minimenu;
        return 1;
    case UITREE_HOST_GET_HOVERTEXT_STATE:
        assert(req->u.get_hovertext_state.out);
        *req->u.get_hovertext_state.out = &app->hover_text;
        return 1;
    case UITREE_HOST_MEASURE_TEXT:
    {
        struct ToriDraw_Font* font = ToriDraw_SceneFontGet(app->scene, req->u.measure_text.font_id);
        if( !font || !req->u.measure_text.text )
            return 0;
        return ToriDraw2D_MeasureString(font, req->u.measure_text.text);
    }
    /* Compass/minimap rotation, in the 0..2047 units the rotated sprite blit
     * takes. Normalized because ToriDraw_Sin/Cos assert that range. */
    case UITREE_HOST_GET_CAMERA_YAW:
        return ToriDraw_NormalizeAngle(app->world_camera.yaw);
    /* Minimap: the baked world map plus the camera's pivot inside it. The
     * widget box is fixed, so the map scrolls by moving this source anchor. */
    case UITREE_HOST_GET_MINIMAP_STATE:
    {
        /* Reference centers the minimap on the local player (minimapDraw
         * anchors at player.x/32), not the orbit eye; free-cam (offline)
         * keeps the eye anchor. */
        struct WorldEntity_Player* local_player = app_local_player(app);
        int anchor_x = local_player ? (int)local_player->draw_position.x
                                    : app->world_camera_pos.x;
        int anchor_z = local_player ? (int)local_player->draw_position.z
                                    : app->world_camera_pos.z;
        if( app->world_map_scene_id <= 0 || !app->world || !app->world->minimap )
            return -1;
        minimap_compute_camera_src_anchor(
            anchor_x,
            anchor_z,
            app->world_map_w,
            app->world_map_h,
            app->world->minimap->width,
            app->world->minimap->height,
            req->u.get_minimap_state.out_src_anchor_x,
            req->u.get_minimap_state.out_src_anchor_y);
        return app->world_map_scene_id;
    }
    case UITREE_HOST_GET_MINIMAP_DOTS:
        return app_minimap_build_dots(app, req->u.get_minimap_dots.out_dots);
    case UITREE_HOST_GET_INV_SOURCE_SLOT:
        assert(req->u.get_inv_source_slot.out);
        if( !InvManager_GetSlot(
                &app->invs,
                req->u.get_inv_source_slot.source_id,
                req->u.get_inv_source_slot.slot,
                &slot) )
            return 0;
        req->u.get_inv_source_slot.out->obj_id = slot.obj_id;
        req->u.get_inv_source_slot.out->obj_count = slot.obj_count;
        req->u.get_inv_source_slot.out->scene_id = slot.scene_id;
        req->u.get_inv_source_slot.out->atlas_index = slot.atlas_index;
        return 1;
    /* CS1 answers come from the per-tick evaluation cached on each node, so
     * drawing never runs the VM and never has to handle a mid-frame yield. */
    case UITREE_HOST_IS_ACTIVE:
        if( !req->u.is_active.component )
            return 0;
        return req->u.is_active.component->cs1_active ? 1 : 0;
    case UITREE_HOST_EVAL_TEXT_PLACEHOLDER:
        if( !req->u.eval_text_placeholder.component ||
            req->u.eval_text_placeholder.script_idx < 0 ||
            req->u.eval_text_placeholder.script_idx >= UITREE_CS1_VALUE_MAX )
            return 0;
        return req->u.eval_text_placeholder.component
            ->cs1_values[req->u.eval_text_placeholder.script_idx];
    /* Tab + privacy-bar state lives on RS_UISlots (reference sideTab /
     * sideOverlayId / chat*Mode). A side modal suppresses the tab subtree by
     * answering -1, which no sidebar tabno matches. */
    case UITREE_HOST_GET_SELECTED_TAB:
        if( app->slots.side_modal_id != -1 )
            return -1;
        return app->slots.side_tab;
    case UITREE_HOST_SET_SELECTED_TAB:
        if( app->slots.side_tab != req->u.set_selected_tab.tabno )
        {
            app->slots.side_tab = req->u.set_selected_tab.tabno;
            app->need_redraw = 1;
        }
        return 1;
    case UITREE_HOST_GET_TAB_ENABLED:
        return RS_UISlots_TabEnabled(&app->slots, req->u.tab_enabled.tabno);
    case UITREE_HOST_GET_CHAT_FILTER_MODE:
        if( req->u.chat_filter.filter < 0 || req->u.chat_filter.filter >= RS_UI_CHAT_FILTER_COUNT )
            return 0;
        return app->slots.chat_filter_mode[req->u.chat_filter.filter];
    case UITREE_HOST_CYCLE_CHAT_FILTER_MODE:
        app->need_redraw = 1;
        return RS_UISlots_CycleChatFilter(&app->slots, req->u.chat_filter.filter);
    case UITREE_HOST_APPLY_BUTTON_CLICK:
        if( !req->u.apply_button_click.component )
            return 0;
        return RS_IF1_ApplyButtonClick(
            app,
            req->u.apply_button_click.component->component_id,
            RS_Minimenu_IfButtonActionForType(
                req->u.apply_button_click.component->behavior.button_type));
    case UITREE_HOST_GET_CHAT_STATE:
        assert(req->u.get_chat_state.out);
        *req->u.get_chat_state.out = &app->chat_view;
        return 1;
    case UITREE_HOST_GET_OBJ_NAME:
    {
        struct ToriRS_Objtype const* obj =
            CacheProvider_ObjtypeGet(app->provider, req->u.get_obj_name.obj_id);
        if( !obj || !req->u.get_obj_name.out || req->u.get_obj_name.cap <= 0 )
            return 0;
        strncpy(req->u.get_obj_name.out, obj->name, (size_t)req->u.get_obj_name.cap - 1);
        req->u.get_obj_name.out[req->u.get_obj_name.cap - 1] = '\0';
        if( req->u.get_obj_name.out_stackable )
            *req->u.get_obj_name.out_stackable = obj->stackable ? 1 : 0;
        return 1;
    }
    case UITREE_HOST_GET_INV_DRAG:
        /* Reports the armed slot even on a non-draggable grid: the pressed item
         * still fades to trans 128 while held (dx/dy stay 0 there, so it never
         * moves — the held tick bails before computing an offset), only the
         * drag/swap is suppressed. */
        if( app->inv_drag_com_id < 0 )
            return 0;
        if( req->u.get_inv_drag.out_source_id )
            *req->u.get_inv_drag.out_source_id = app->inv_drag_source_id;
        if( req->u.get_inv_drag.out_slot )
            *req->u.get_inv_drag.out_slot = app->inv_drag_from_slot;
        if( req->u.get_inv_drag.out_dx )
            *req->u.get_inv_drag.out_dx = app->inv_drag_dx;
        if( req->u.get_inv_drag.out_dy )
            *req->u.get_inv_drag.out_dy = app->inv_drag_dy;
        return 1;
    case UITREE_HOST_GET_INV_COUNT_FONT:
        /* Reference draws stack counts with the client's p11 — same font (and
         * same load-on-miss self-heal) as the hitsplat numbers. */
        return app_hitsplat_font_scene_id(app);
    case UITREE_HOST_GET_INV_SELECT_ICON:
        /* Reference TYPE_INV draw: only the slot armed for "Use" (useMode==1,
         * matching objSelectedSlot + objSelectedComId) gets the white outline.
         * The model is already resident (its plain icon is on screen), so the
         * white variant bakes on first request and is cached thereafter. */
        if( !app->objsel.active )
            return 0;
        if( app->objsel.component_id != req->u.get_inv_select_icon.com_id )
            return 0;
        if( app->objsel.slot != req->u.get_inv_select_icon.slot )
            return 0;
        return UITreeSceneBridge_EnsureObjIconSelected(
            &app->bridge,
            req->u.get_inv_select_icon.obj_id,
            req->u.get_inv_select_icon.count > 0 ? req->u.get_inv_select_icon.count : 1);
    default:
        return 0;
    }
}

/* Demo content until real state sync exists: seed the worn/backpack/bank
 * containers so item-bearing interfaces have something to show. */
static void
seed_inv_defaults(struct InvManager* invs)
{
    static int const k_worn_items[] = { 1153, 1007, 1725, 1333, 1115, 1201,
                                        1189, 1063, 1067, 2564, 882 };
    static int const k_backpack_items[] = { 1333 };
    /* Bank contents so interface 12 renders its item grid + tab row. */
    static int const k_bank_items[] = { 995,  1333, 1153, 1007, 1725, 1115, 1201, 1189, 1063,
                                        1067, 2564, 882,  4151, 1305, 1319, 1215, 1231, 1147,
                                        1163, 1079, 1093, 861,  1163, 1704, 2550, 6585, 1725,
                                        3105, 1387, 1275, 1291, 4587, 1215, 1333, 995,  1038 };

    assert(invs);
    assert(InvManager_ResolveSource(invs, INV_MANAGER_SOURCE_NAME_WORN) >= 0);
    assert(InvManager_ResolveSource(invs, INV_MANAGER_SOURCE_NAME_BACKPACK) >= 0);

    assert(InvManager_ApplyFull(
        invs,
        INV_MANAGER_CONTAINER_WORN,
        k_worn_items,
        NULL,
        (int)(sizeof(k_worn_items) / sizeof(k_worn_items[0]))));
    assert(InvManager_ApplyFull(
        invs,
        INV_MANAGER_CONTAINER_BACKPACK,
        k_backpack_items,
        NULL,
        (int)(sizeof(k_backpack_items) / sizeof(k_backpack_items[0]))));
    assert(InvManager_EnsureContainer(invs, INV_MANAGER_CONTAINER_BANK, 800, "bank") >= 0);
    assert(InvManager_ApplyFull(
        invs,
        INV_MANAGER_CONTAINER_BANK,
        k_bank_items,
        NULL,
        (int)(sizeof(k_bank_items) / sizeof(k_bank_items[0]))));
}

/* ---- Inventory obj-icon reconcile ------------------------------------- *
 *
 * Server UPDATE_INV_FULL/PARTIAL (rs_gameproto_exec.c) write item ids into the
 * inv containers but leave scene_id = INV_MANAGER_NO_SCENE_ID, because the
 * inventory model may not be resident and rasterizing needs it loaded. The
 * emit path (emit_rs_inv_slots) only draws a slot when scene_id >= 0, so those
 * items never appear. This mirrors task_interface_open's seed-time icon step
 * (load the models, then UITreeSceneBridge_EnsureObjIcon and stamp the scene id
 * back) but is driven per tick off the live containers, so items that arrive
 * after the interface is open still get icons — the missing lazy path the
 * exec handlers' comment promised.
 *
 * A slot whose model can never be built is stamped with a distinct sentinel so
 * the per-tick scan stops re-enqueueing it; the server replacing the item
 * resets scene_id to NO_SCENE_ID and re-arms the reconcile. */
#define APP_INV_ICON_BATCH_MAX 64
#define APP_INV_ICON_SCENE_FAILED (-2)

/* True while any item slot still needs a first rasterization attempt. */
static int
app_inv_needs_icons(struct App const* app)
{
    for( int ci = 0; ci < app->invs.container_count; ci++ )
    {
        struct InvContainer const* c = &app->invs.containers[ci];
        if( !c->slots )
            continue;
        for( int s = 0; s < c->slot_count; s++ )
            if( c->slots[s].obj_id > 0 && c->slots[s].scene_id == INV_MANAGER_NO_SCENE_ID )
                return 1;
    }
    return 0;
}

struct Task_InvIconReconcile
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int obj_ids[APP_INV_ICON_BATCH_MAX];
    int counts[APP_INV_ICON_BATCH_MAX];
    int n;
};

static int
Task_InvIconReconcile_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_InvIconReconcile* self = (struct Task_InvIconReconcile*)base;
    struct App* app = self->app;
    (void)io;

    PT_BEGIN(&self->pt);

    /* Collect the batch that still needs a model load (bounded; leftovers are
     * caught by the next tick's scan once this pass stamps its slots). */
    self->n = 0;
    for( int ci = 0; ci < app->invs.container_count && self->n < APP_INV_ICON_BATCH_MAX; ci++ )
    {
        struct InvContainer const* c = &app->invs.containers[ci];
        if( !c->slots )
            continue;
        for( int s = 0; s < c->slot_count && self->n < APP_INV_ICON_BATCH_MAX; s++ )
        {
            struct InvSlot const* slot = &c->slots[s];
            if( slot->obj_id > 0 && slot->scene_id == INV_MANAGER_NO_SCENE_ID )
            {
                self->obj_ids[self->n] = slot->obj_id;
                self->counts[self->n] = slot->obj_count > 0 ? slot->obj_count : 1;
                self->n++;
            }
        }
    }
    if( self->n > 0 )
        TASK_AWAITSELF_IF(
            CreateTask_ObjModelLoad(app->provider, self->obj_ids, self->counts, self->n));

    /* Rasterize every pending slot from whatever is now resident and stamp the
     * scene id (or the failed sentinel) back onto the slot. */
    for( int ci = 0; ci < app->invs.container_count; ci++ )
    {
        struct InvContainer* c = &app->invs.containers[ci];
        if( !c->slots )
            continue;
        for( int s = 0; s < c->slot_count; s++ )
        {
            struct InvSlot* slot = &c->slots[s];
            int scene_id;
            if( slot->obj_id <= 0 || slot->scene_id != INV_MANAGER_NO_SCENE_ID )
                continue;
            scene_id = UITreeSceneBridge_EnsureObjIcon(
                &app->bridge, slot->obj_id, slot->obj_count > 0 ? slot->obj_count : 1);
            slot->scene_id = scene_id >= 0 ? scene_id : APP_INV_ICON_SCENE_FAILED;
            slot->atlas_index = 0;
        }
    }

    app->inv_icon_reconcile_inflight = 0;
    app->need_redraw = 1;
    PT_END(&self->pt);
}

static void
Task_InvIconReconcile_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_InvIconReconcile_VTable = {
    .run = Task_InvIconReconcile_Run,
    .free = Task_InvIconReconcile_Free,
};

/* Per-tick hook: enqueue one reconcile if any item icon is still unresolved and
 * none is already running. Serial on the exec pipeline so it applies after the
 * inventory packets that dirtied the slots. */
static void
app_inv_icon_reconcile_tick(struct App* app)
{
    struct Task_InvIconReconcile* task;

    if( app->inv_icon_reconcile_inflight || !app_inv_needs_icons(app) )
        return;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_InvIconReconcile_VTable;
    strncpy(task->task.name, "InvIconReconcile", sizeof(task->task.name) - 1);
    task->app = app;
    PT_INIT(&task->pt);
    app->inv_icon_reconcile_inflight = 1;
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Wrapper protothread that owns one CS1 evaluation pass: awaits the eval
 * task (which may itself yield for pack loads), then clears the in-flight
 * gate and requests a redraw when a cached result changed. */
struct Task_AppCS1Eval
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
};

static int
Task_AppCS1Eval_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_AppCS1Eval* self = (struct Task_AppCS1Eval*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);
    app->cs1_host.eval_dirty = false;
    TASK_AWAITSELF_IF(CreateTask_CS1Eval(&app->cs1_host));
    app->cs1_eval_inflight = 0;
    if( app->cs1_host.eval_dirty )
        app->need_redraw = 1;
    PT_END(&self->pt);
}

static void
Task_AppCS1Eval_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_AppCS1Eval_VTable = {
    .run = Task_AppCS1Eval_Run,
    .free = Task_AppCS1Eval_Free,
};

/* Request a CS1 evaluation pass; at most one is ever in flight (the tick
 * re-requests every 20ms anyway, so a busy pass simply coalesces). Never
 * blocks — the frame pump drives it. */
static void
app_request_cs1_eval(struct App* app)
{
    struct Task_AppCS1Eval* task;

    if( app->cs1_eval_inflight )
        return;
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppCS1Eval_VTable;
    strncpy(task->task.name, "AppCS1Eval", sizeof(task->task.name) - 1);
    task->app = app;
    PT_INIT(&task->pt);
    app->cs1_eval_inflight = 1;
    ToriRS_TaskQueue_Add(app->runner.queue, &task->task);
}

/* Scene models reference textures by face id, but the ToriDraw texture map
 * starts empty (reference: textures load on demand and faces skip-render
 * until they land). The ids come from model construction itself
 * (ToriDraw_ModelTextureWantsTake) — whatever built a model reported the
 * textures it needs — so this costs nothing per tick when no geometry was
 * built. Queue the loads and remember the ids; app_sync_textures_poll
 * publishes them into the scene as the loads land. Ids that fail stay marked
 * in the bridge and are never re-requested. */
static void
app_sync_textures(struct App* app)
{
    int ids[256];
    int id_count;

    id_count = ToriDraw_ModelTextureWantsTake(ids, 256);
    if( id_count == 0 )
        return;
    if( getenv("TORIRS_TEX_DEBUG") )
    {
        fprintf(stderr, "tex_wants drained %d:", id_count);
        for( int i = 0; i < id_count; i++ )
            fprintf(stderr, " %d", ids[i]);
        fprintf(stderr, "\n");
    }

    for( int i = 0; i < id_count; i++ )
    {
        int const id = ids[i];
        if( id >= 0 && id < 2048 && app->bridge.texture_failed[id] )
            continue;
        if( UITreeSceneBridge_TextureResident(&app->bridge, id) )
            continue;
        if( !CacheProvider_TextureHas(app->provider, id) )
        {
            struct ToriRS_Task* task = CreateTask_TextureLoad(app->provider, id);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
        }
        /* Track for the publish poll (dedupe; the set is tiny). */
        {
            int seen = 0;
            for( int p = 0; p < app->tex_pending_count; p++ )
                if( app->tex_pending[p] == id )
                {
                    seen = 1;
                    break;
                }
            if( !seen && app->tex_pending_count < 512 )
                app->tex_pending[app->tex_pending_count++] = id;
        }
    }
}

/* Per-frame: publish any pending textures that finished loading; keep only
 * the ones still in flight (present in neither the provider nor the bridge's
 * failed set). */
static void
app_sync_textures_poll(struct App* app)
{
    int kept = 0;

    if( app->tex_pending_count == 0 )
        return;

    if( UITreeSceneBridge_PublishTextures(&app->bridge, app->tex_pending, app->tex_pending_count) )
        app->need_redraw = 1;

    for( int i = 0; i < app->tex_pending_count; i++ )
    {
        int id = app->tex_pending[i];
        int failed = (id >= 0 && id < 2048) ? app->bridge.texture_failed[id] : 1;
        if( !CacheProvider_TextureHas(app->provider, id) && !failed )
            app->tex_pending[kept++] = id;
    }
    app->tex_pending_count = kept;
}

/*
 * Server varp update -> CS2 host, so the tick's var-transmit pump re-dispatches
 * the hooks that list this varp as a trigger. Userdata is the app, not the host,
 * because the same callback routes client-code varps (the sound volume setting)
 * to their subsystems.
 *
 * Deliberately NOT wired to the plain value-change callback, and not wired to
 * varcs at all. The reference feeds its changed-varp ring only from the
 * VARP_SMALL / VARP_LARGE / VARP_RESET packet handlers: a script-side write
 * (CS2 SETVARP, IF1 button, varbit set) updates the varp and notifies the
 * server but never enters the ring, and `Varcs` writes touch nothing beyond
 * their own map. Wiring either of those in makes the dispatch self-feeding —
 * a hook whose script writes a var bumps the change serial, which re-triggers
 * that same hook next tick, forever. That is what had rev230's gameframe
 * rebuilding the popout strip, the world-hop list (601 dynamic children) and
 * the 161|36 listener from scratch every ~8 frames, and it is why the hovered
 * component id climbed without end: every rebuild hands the same three popout
 * icons brand-new dynamic uids.
 */
static void
app_varp_server_update(void* userdata, int varp_id)
{
    struct App* app = (struct App*)userdata;

    RS_CS2Host_NotifyVarChanged(&app->host, varp_id);

    /* Client-code varps are settings the client acts on rather than displays.
     * Code 4 is the sound-effect volume slider (reference Client.updateVarp:
     * 0..3 pick 128/96/64/32, 4 mutes) — the only one audio cares about, and the
     * only reason the player's volume choice reaches the platform at all. */
    if( VarPManager_GetClientcode(&app->varps, varp_id) == 4 )
        RS_Audio_SetVolumeLevel(
            &app->audio, VarPManager_GetVarp(&app->varps, varp_id), &app->audio_out);
}

/**
 * Tell the provider which cache it is reading.
 *
 * The profile is what rscache's decoders consult instead of a bare revision number.
 * Resolving it here, once, is the point: era information used to reach decoders as
 * whichever JS5 archive counter the record happened to come from — a per-archive value
 * whose units differ between eras — or as a flag constant spelled out at the call site.
 *
 * The manifest states all four identity fields (game, epoch, revision, quirks).
 * RSCache_ProfileForIdentity returns them verbatim and borrows codec pins from the
 * revision registry on an exact match. There is no nearest-lower fallback and no
 * guessing from the container alone.
 */
static void
app_provider_set_cache_profile(
    struct App* app,
    struct AppConfig const* cfg)
{
    assert(app);
    assert(app->provider);
    assert(cfg->cache_identity_set && "manifest must state [cache:boot] identity");

    struct RSCache profile = RSCache_ProfileForIdentity(
        cfg->cache_game, cfg->cache_epoch, cfg->cache_revision, cfg->cache_quirks);

    char quirks_buf[32];
    RSCache_QuirksName(profile.quirks, quirks_buf, (int)sizeof(quirks_buf));
    printf(
        "app: cache profile epoch=%s game=%s revision=%d quirks=%s\n",
        RSCache_EpochName(profile.epoch),
        RSCache_GameName(profile.game),
        profile.revision,
        quirks_buf);

    /* The disk resolves logical table names to ids and decides map XTEA, so it
     * needs the same identity the decoders got. Without this it answers as
     * unset, which on a 643 cache means every logical table is ABSENT. */
    if( app->dat2_disk )
        RSCache_Dat2DiskSetProfile(app->dat2_disk, &profile);

    CacheProvider_SetProfile(app->provider, &profile);
}

void
App_Init(
    struct App* app,
    struct AppConfig const* cfg)
{
    assert(app);
    assert(cfg);
    memset(app, 0, sizeof(*app));
    app->cfg = *cfg;

    ToriDraw_Init();

    /* Phase 1: task runtime + disk. The runner owns the async pipeline every
     * other phase loads through. */
    app->runner.io = ToriRS_IO_New();
    app->runner.queue = ToriRS_TaskQueue_New();
    app->runner.px = PlatformX_IO_New();
    assert(app->runner.px != NULL);

    /* Serial game-action pipeline: own queue + io slots, SHARED platform
     * pump (there is exactly one IO backend). */
    app->exec_runner.io = ToriRS_IO_New();
    app->exec_runner.queue = ToriRS_TaskQueue_New();
    app->exec_runner.px = app->runner.px;

    if( cfg->cache_kind == APP_CACHE_DAT1 )
    {
        app->dat1_disk = RSCache_Dat1DiskNewFromDirectory(cfg->cache_dir);
        if( !app->dat1_disk )
            fprintf(
                stderr,
                "app: no dat1 cache at %s (expected main_file_cache.dat; pass --dat2 for a "
                "js5 cache)\n",
                cfg->cache_dir);
        assert(app->dat1_disk != NULL);
        PlatformX_IO_InitDat1Disk(app->runner.px, app->dat1_disk);
        /* No xtea step: dat1 archives are not encrypted. */
    }
    else
    {
        app->dat2_disk = RSCache_Dat2DiskNewFromDirectory(cfg->cache_dir);
        if( !app->dat2_disk )
            fprintf(
                stderr,
                "app: no dat2 cache at %s (expected main_file_cache.dat2; pass --dat1 for a "
                "317-era cache)\n",
                cfg->cache_dir);
        assert(app->dat2_disk != NULL);
        /* Map archives may be xtea-encrypted (OldSchool below 237; RS2 dat2
         * from 414). Keys load into the rscache global table the disk layer
         * consults on archive fetch — only when the identity gate says so. */
        {
            struct RSCache probe = RSCache_ProfileForIdentity(
                cfg->cache_game, cfg->cache_epoch, cfg->cache_revision, cfg->cache_quirks);
            if( RSCache_MapLocsEncrypted(&probe) )
            {
                char xtea_path[1024];
                snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", cfg->cache_dir);
                if( RSCache_XteaConfigLoadKeys(xtea_path) <= 0 )
                    fprintf(
                        stderr, "app: no xtea keys at %s (world maps may fail)\n", xtea_path);
            }
            else
            {
                printf("app: map archives are unencrypted at this revision\n");
            }
        }
        PlatformX_IO_InitDat2Disk(app->runner.px, app->dat2_disk);
    }
    PlatformX_IO_InitConfigPath(app->runner.px, cfg->config_dir);
    PlatformX_IO_InitScriptPath(app->runner.px, cfg->script_dir);

    /* Phase 2: asset pipeline (provider is a view over the build cache). */
    if( cfg->cache_kind == APP_CACHE_DAT1 )
    {
        app->dat1_bc = dat1_buildcache_new();
        app->provider = dat1_buildcache_as_provider(app->dat1_bc);
    }
    else
    {
        app->dat2_bc = dat2_buildcache_new();
        app->provider = dat2_buildcache_as_provider(app->dat2_bc);
    }
    app_provider_set_cache_profile(app, cfg);

    /* Phase 3: renderer scene + id bridge (bridge needs scene + provider). */
    app->scene = ToriDraw_SceneNew(0);
    assert(app->scene);
    UITreeSceneBridge_Init(&app->bridge, app->scene, app->provider);

    /* Phase 4: game state (host needs tree + provider + invs + varps, then
     * the bridge for icon rasterization). */
    app->tree = UITree_New(256);
    assert(app->tree);
    InvManager_Init(&app->invs);
    VarPManager_Init(&app->varps);
    VarCManager_Init(&app->varcs);

    /* Phase 4b: world sim + builder. The World is a pure simulation that
     * references scene elements/assets by integer id; the builder keeps it in
     * sync with the shared scene from cache data. */
    app->world = World_New();
    assert(app->world);
    app->world_builder = WorldBuilder_New(app->world, app->provider, app->scene, &app->varps);
    assert(app->world_builder);
    app->painter_buffer = painter_buffer_new();
    assert(app->painter_buffer);
    /* v1 GameRunescape camera defaults; repositioned on world load complete. */
    app->world_camera.fov_rpi2048 = 512;
    app->world_camera.near_plane_z = 50;
    app->world_camera.pitch = 148;
    app->world_camera_pos.z = -800;
    app->orbit_pitch = 128; /* reference orbitCameraPitch default */
    app->orbit_yaw = 0;
    app->world_hover_tile_x = -1;
    app->world_hover_tile_z = -1;
    app->world_hover_tile_level = 0;
    app->world_map_scene_id = -1;
    app->minimap_flag_x = -1;
    app->minimap_flag_z = -1;
    app->proj_src_tile_x = -1;
    app->proj_src_tile_z = -1;
    app->proj_src_tile_level = 0;
    /* Element id 0 is valid, so free entity-spotanim slots must be -1. */
    for( size_t i = 0; i < sizeof(app->entity_spotanims) / sizeof(app->entity_spotanims[0]); i++ )
        app->entity_spotanims[i].body_element_id = -1;

    seed_inv_defaults(&app->invs);
    RS_EntitySync_Init(&app->esync);
    RS_Audio_Init(&app->audio);
    ToriRS_AudioQueue_Reset(&app->audio_out);
    /* State the starting volume once, so a backend is never left guessing at a
     * gain the game already has an opinion about (reference Client.waveVolume). */
    {
        struct ToriRS_AudioCommand volume_command;
        memset(&volume_command, 0, sizeof(volume_command));
        volume_command.kind = TORIRS_AUDIO_CMD_SET_VOLUME;
        volume_command.sound_id = -1;
        volume_command.volume = app->audio.volume;
        ToriRS_AudioQueue_Push(&app->audio_out, &volume_command);
    }
    app->cam_script.shake_axis = -1;
    app->inv_drag_com_id = -1;
    app->reboot_ticks = 0;
    RS_Social_Init(&app->social);
    RS_Social_SeedDefaults(&app->social);
    RS_Chat_Init(&app->chat, "Player");
    /* No hardcoded welcome line: the server sends the real "Welcome to
     * RuneScape." MESSAGE_GAME packet on login (reference has no client-side
     * welcome message; its only "Welcome to RuneScape" is the login title). */
    app->chat_source.line_at = app_chat_line_at;
    app->chat_source.user = app;
    RS_CS2Host_Init(&app->host, app->tree, app->provider, &app->invs, &app->varps, &app->varcs);
    RS_CS2Host_SetBridge(&app->host, &app->bridge);
    /* Close the reactive loop: a varp update from the *server* flags a
     * var-transmit re-dispatch on the host, so interfaces react to value changes
     * and not only to unhide. See app_varp_server_update for why script-side
     * writes and varcs stay out. */
    VarPManager_SetServerUpdateCallback(&app->varps, app_varp_server_update, app);
    RS_PlayerStats_Init(&app->stats);
    RS_CS1Host_Init(&app->cs1_host, app->tree, app->provider, &app->invs, &app->varps, &app->stats);

    /* Phase 5: frame state. */
    UITree_EmitBufferInit(&app->emit);
    UITree_HostInit(&app->ui_host);
    app->ui_host.user = app;
    app->ui_host.request = app_host_request;
    UIInteraction_Init(&app->interact);
    UIHoverText_Reset(&app->hover_text);
    app->hover_com_id = -1;
    app->clicked_com_id = -1;
    app->need_redraw = 1;

    /* Phase 6: networking (opt-in). The default RSA key is the rev_245_2 Lost
     * City pair (v0 tori_rs_init); TORIRS_RSA_EXP/MOD override it. */
    if( cfg->connect_target && cfg->connect_target[0] )
    {
        /* RSA key precedence: env > manifest (cfg) > built-in default pair. */
        char const* rsa_e = getenv("TORIRS_RSA_EXP");
        char const* rsa_n = getenv("TORIRS_RSA_MOD");
        if( !rsa_e )
            rsa_e = cfg->rsa_exp;
        if( !rsa_n )
            rsa_n = cfg->rsa_mod;
        if( !rsa_e )
            rsa_e = "81f390b2cf8ca7039ee507975951d5a0b15a87bf8b3f99c966834118c50fd94d";
        if( !rsa_n )
            rsa_n =
                "88c38748a58228f7261cdc340b5691d7d0975dee0ecdb717609e6bf971eb3fe723ef9d130e468"
                "6813739768ad9472eb46d8bfcc042c1a5fcb05e931f632eea5d";

        char const* rev_name = cfg->rev_name;
        if( !rev_name || !rev_name[0] )
            rev_name = getenv("TORIRS_REV");
        struct GameProtoRevTable const* rev =
            rev_name && rev_name[0] ? GameProtoRev_ByName(rev_name) : GameProtoRev_LC254();
        if( !rev )
        {
            fprintf(stderr, "app: unknown protocol rev '%s', using lc254\n", rev_name);
            rev = GameProtoRev_LC254();
        }

        /* Manifest login params override the table defaults, but env still wins
         * (the lazy TORIRS_JAG_CRC parse in the rev getter already ran). */
        if( cfg->jag_crc_set && !getenv("TORIRS_JAG_CRC") )
            GameProtoRev_SetJagChecksums(rev, cfg->jag_crc);
        if( cfg->client_version > 0 )
            GameProtoRev_SetClientVersion(rev, cfg->client_version);

        app->net = calloc(1, sizeof(struct ToriRS_Network));
        assert(app->net);
        ToriRS_Network_Init(app->net, rev, rsa_e, rsa_n);
        app->net_enabled = 1;
        /* IF1 button clicks now notify the server (reference IF_BUTTON /
         * RESUME_PAUSEBUTTON). */
        app->button_sink.user = app;
        app->button_sink.if_button = app_send_if_button;
        app->button_sink.resume_pausebutton = app_send_resume_pausebutton;
        app->button_sink.close_modal = app_send_close_modal;
        ToriRS_Network_ConnectLogin(
            app->net,
            cfg->connect_target,
            cfg->connect_user ? cfg->connect_user : "guest",
            cfg->connect_pass ? cfg->connect_pass : "");
    }
}

int
App_UiLogic(struct App const* app)
{
    assert(app);
    if( app->cfg.ui_logic == APP_UI_LOGIC_CS1 || app->cfg.ui_logic == APP_UI_LOGIC_CS2 )
        return app->cfg.ui_logic;
    /* DEFAULT: derive from cache format (bit-identical to the legacy keying). */
    return app->cfg.cache_kind == APP_CACHE_DAT1 ? APP_UI_LOGIC_CS1 : APP_UI_LOGIC_CS2;
}

void
App_Shutdown(struct App* app)
{
    assert(app);
    if( app->net )
    {
        ToriRS_Network_Free(app->net);
        free(app->net);
        app->net = NULL;
    }
    UITree_EmitBufferFree(&app->emit);
    RS_CS2Host_Free(&app->host);
    if( app->painter_buffer )
    {
        free(app->painter_buffer->commands);
        free(app->painter_buffer);
    }
    RS_Audio_Shutdown(&app->audio);
    RS_EntitySync_Free(&app->esync);
    WorldBuilder_Free(app->world_builder);
    World_Free(app->world);
    VarPManager_Free(&app->varps);
    VarCManager_Free(&app->varcs);
    InvManager_Free(&app->invs);
    UITree_Free(app->tree);
    if( app->builder_active )
        UITreeBuilder_Free(&app->builder);
    UITreeSceneBridge_Free(&app->bridge);
    ToriDraw_SceneFree(app->scene);
    /* Only the pair matching cfg.cache_kind was ever created; both frees assert
     * on NULL, so the unused side must not be handed to them. */
    if( app->dat2_bc )
        dat2_buildcache_free(app->dat2_bc);
    if( app->dat1_bc )
        dat1_buildcache_free(app->dat1_bc);
    PlatformX_IO_Free(app->runner.px);
    if( app->dat2_disk )
        RSCache_Dat2DiskFree(app->dat2_disk);
    if( app->dat1_disk )
        RSCache_Dat1DiskFree(app->dat1_disk);
    ToriRS_TaskQueue_Free(app->exec_runner.queue);
    ToriRS_IO_Free(app->exec_runner.io);
    ToriRS_TaskQueue_Free(app->runner.queue);
    ToriRS_IO_Free(app->runner.io);
    free(app->if_heads);
}

/* Scene font id for the minimenu (reference uses bold-12; dat2 fonts-table
 * archive 496 in this cache era, e.g. bank title font). Dat1 has no fonts
 * table: its fonts live in the title jagfile and are pinned at cache-font
 * slots 0-3 by RevConfig, where b12 is slot 2. Falls back to any text node's
 * already-resolved scene font when b12 cannot load. */
enum
{
    APP_FONT_B12_CACHE_ID = 496,
    APP_FONT_B12_DAT1_SLOT = 2,
    /* Hitsplat numbers use p11 (reference `this.p11.centreString`). RevConfig
     * orders the dat1 title jagfile fonts p11/p12/b12/q8 as slots 0-3; the
     * dat2 fonts table keeps them adjacent with b12 at 496. */
    APP_FONT_P11_CACHE_ID = 494,
    APP_FONT_P11_DAT1_SLOT = 0,
};

static int
app_font_b12_cache_id(struct App const* app)
{
    return app->cfg.cache_kind == APP_CACHE_DAT1 ? APP_FONT_B12_DAT1_SLOT : APP_FONT_B12_CACHE_ID;
}

/* Scene font for hitsplat numbers; queues the load on a miss the same way
 * app_minimenu_font_scene_id does, and returns -1 until it lands.
 *
 * -1, not 0: scene font ids ARE cache font ids, and dat1 p11 is cache id 0 —
 * the same trap that once left every p11 label invisible. */
static int
app_hitsplat_font_scene_id(struct App* app)
{
    int font_cache_id =
        app->cfg.cache_kind == APP_CACHE_DAT1 ? APP_FONT_P11_DAT1_SLOT : APP_FONT_P11_CACHE_ID;
    int scene_id = UITreeSceneBridge_EnsureFont(&app->bridge, font_cache_id);
    if( scene_id < 0 )
    {
        struct ToriRS_Task* task = CreateTask_FontLoad(app->provider, font_cache_id);
        if( task )
            ToriRS_TaskQueue_Add(app->runner.queue, task);
    }
    return scene_id;
}

static int
app_minimenu_font_scene_id(struct App* app)
{
    int font_cache_id = app_font_b12_cache_id(app);
    int scene_id = UITreeSceneBridge_EnsureFont(&app->bridge, font_cache_id);
    if( scene_id <= 0 )
    {
        /* Queue the load (no blocking drain — the boot task awaits this font
         * before the overlays are pushed, so at runtime a miss just falls
         * through to the text-node scan below until the load lands). */
        struct ToriRS_Task* task = CreateTask_FontLoad(app->provider, font_cache_id);
        if( task )
            ToriRS_TaskQueue_Add(app->runner.queue, task);
    }
    if( scene_id <= 0 )
    {
        for( uint32_t i = 0; i < app->tree->component_count; i++ )
        {
            struct UITreeComponent const* node = &app->tree->components[i];
            if( !node->freed && node->type == UIELEM_RS_TEXT && node->u.rs_text.font_id > 0 )
            {
                scene_id = node->u.rs_text.font_id;
                break;
            }
        }
    }
    return scene_id;
}

/* Overlay chrome the interface pack does not provide: the click cross and the
 * minimenu popup live as late root siblings (drawn/hit-tested above the
 * interface) and stay invisible until the host reports them active. */
static void
app_push_builtin_overlay_nodes(struct App* app)
{
    struct UITreeNodeSpec spec;
    int font_id = app_minimenu_font_scene_id(app);

    /* Entity overlays first: health bars and hitsplats belong above the world
     * but below the cross/hovertext/minimenu, and the emit walk draws root
     * siblings in push order. The desc clips itself to the world box, so the
     * late position never lets them paint over the chrome. */
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_BUILTIN_ENTITY_OVERLAY;
    spec.component_id = APP_COM_ID_ENTITY_OVERLAY;
    assert(UITree_Push(app->tree, -1, &spec) >= 0);

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_BUILTIN_CROSS;
    spec.component_id = APP_COM_ID_CROSS;
    spec.width = 16;
    spec.height = 16;
    assert(UITree_Push(app->tree, -1, &spec) >= 0);

    /* Pushed before the minimenu so the popup draws over the hover line — the
     * two are never both up (4726 returns early on minimenu_isopen), but the
     * ordering keeps that invariant from mattering. */
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_BUILTIN_HOVERTEXT;
    spec.component_id = APP_COM_ID_HOVERTEXT;
    spec.u.hovertext.font_id = font_id;
    assert(UITree_Push(app->tree, -1, &spec) >= 0);

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_BUILTIN_MINIMENU;
    spec.component_id = APP_COM_ID_MINIMENU;
    spec.u.minimenu.font_id = font_id;
    assert(UITree_Push(app->tree, -1, &spec) >= 0);

    app->interact.minimenu.font_id = font_id;
    app->hover_text.font_id = font_id;
}

/* World_HeightFn: projectiles/movers track terrain height (world units).
 *
 * Line port of Client-TS getAvH (Client.ts:5288), bridge clause included: the
 * scene push-down moves a bridge column's *geometry* from cache level 1 into
 * paint level 0 (WorldBuilder_RebuildCenterzoneEnd, reference World.pushDown),
 * but the heightmap keeps raw cache levels. So a mover standing on a
 * LinkBelow column has to sample level+1 or it sinks to the underpass floor —
 * the "player walks under the bridge" symptom. */
static int
app_world_height(
    void* userdata,
    int world_x,
    int world_z,
    int level)
{
    struct App* app = (struct App*)userdata;
    int real_level = level;

    if( !app->world || !app->world->heightmap )
        return 0;

    /* getAvH out-of-scene guard (Client.ts:5296): a tile outside [0,scene_size)
     * has no heightmap column, so the reference returns a flat 0 rather than
     * sampling. Without this an entity spawned/projected past the scene edge
     * (e.g. a border NPC at tile 105 in a 104-wide scene) drives an unguarded
     * base-corner read in heightmap_get_interpolated straight off the array. */
    {
        int tile_x = world_x >> 7;
        int tile_z = world_z >> 7;
        int scene_size = app->world->_scene_size;
        if( tile_x < 0 || tile_z < 0 || tile_x >= scene_size || tile_z >= scene_size )
            return 0;
    }

    if( level < WORLD_MAP_TERRAIN_LEVELS - 1 &&
        (World_TileFlagGet(app->world, world_x >> 7, world_z >> 7, 1) &
         RSCACHE_FLOFLAG_LINK_BELOW) != 0 )
        real_level = level + 1;
    return heightmap_get_interpolated(app->world->heightmap, world_x, world_z, real_level);
}

/* World_SeqSource getters: seq timing resolved from the scene animation
 * registry (ToriDraw_Animation carries the seq-config meta). Unloaded ids
 * return the world_cycle defaults, which freeze that track until the lazy
 * seq load lands (app_request_entity_seq). */
static struct ToriDraw_Animation*
app_seq_anim(
    void* userdata,
    int seq_id)
{
    struct App* app = (struct App*)userdata;
    if( !app->scene || seq_id < 0 )
        return NULL;
    return ToriDraw_SceneAnimationGet(app->scene, seq_id);
}

static int
app_seq_frame_count(void* userdata, int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim ? anim->frame_count : 0;
}

static int
app_seq_frame_duration(void* userdata, int seq_id, int frame)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    /* Skeletal seqs carry no per-frame lengths — their curves are sampled one
     * tick per client cycle, so every frame is a single cycle long. */
    if( !anim || !anim->frames || frame < 0 || frame >= anim->frame_count )
        return 1;
    return anim->frames[frame].delay > 0 ? anim->frames[frame].delay : 1;
}

static int
app_seq_frame_step(void* userdata, int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim ? anim->frame_step : 0;
}

static int
app_seq_max_loops(void* userdata, int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim && anim->max_loops > 0 ? anim->max_loops : 99;
}

static int
app_seq_priority(void* userdata, int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim ? anim->priority : 5;
}

static int
app_seq_duplicate_behavior(void* userdata, int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim ? anim->duplicate_behavior : -1;
}

static int
app_seq_preanim_move(void* userdata, int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim ? anim->preanim_move : 0;
}

static int
app_seq_stretches(void* userdata, int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim ? anim->stretches : 0;
}

/* World_SeqSource.spotanim_seq: resolve a spotanim id to its animation seq id so
 * the world can step an entity's attached-graphic frame. -1 when the id is
 * invalid or the spotanimtype is not yet resident (the world then waits). */
static int
app_spotanim_seq(void* userdata, int spotanim_id)
{
    struct App* app = (struct App*)userdata;
    struct ToriRS_Spotanimtype* spot =
        spotanim_id >= 0 ? CacheProvider_SpotanimtypeGet(app->provider, spotanim_id) : NULL;
    return spot ? spot->seq : -1;
}

/* Plot one loc mapscene Pix8 into the baked minimap ARGB (reference drawDetail
 * + Pix8.plotSprite). The C bake places tile (sx,sz)'s top-left at
 * (sx*4, (height-sz)*4) and a loc extends north (+z) over `loc_l` tiles, so the
 * footprint's top pixel row is (height - sz - (loc_l-1))*4. The sprite is
 * centered in the loc_w x loc_l footprint (reference offsetX/offsetY) and shifted
 * by its own crop origin (Pix8.xof/yof). Transparent (alpha 0) source pixels,
 * i.e. palette index 0, are skipped like the reference plot. */
static void
app_plot_mapscene_sprite(
    uint32_t* dst,
    int pw,
    int ph,
    struct ToriDraw_Sprite const* spr,
    int sx,
    int sz,
    int map_height,
    int loc_w,
    int loc_l)
{
    int base_x = sx * 4 + (loc_w * 4 - spr->width) / 2 + spr->crop_x;
    int base_y =
        (map_height - sz - (loc_l - 1)) * 4 + (loc_l * 4 - spr->height) / 2 + spr->crop_y;

    for( int y = 0; y < spr->height; y++ )
    {
        int dy = base_y + y;
        uint32_t const* src_row;
        uint32_t* dst_row;
        if( dy < 0 || dy >= ph )
            continue;
        src_row = spr->pixels_argb + (size_t)y * spr->width;
        dst_row = dst + (size_t)dy * pw;
        for( int x = 0; x < spr->width; x++ )
        {
            int dx = base_x + x;
            uint32_t px = src_row[x];
            if( dx < 0 || dx >= pw || (px >> 24) == 0 )
                continue;
            dst_row[dx] = px;
        }
    }
}

/* Reference drawDetail's mapscene pass: after the tile/wall bake, plot each loc
 * mapscene sprite gathered at scene build (world->mapscenes) for the level being
 * baked. The mapscene atlas lives in the scene, so this runs in app.c rather than
 * the leaf minimap layer. Level selection matches the tile bake's VisBelow
 * composition (minimap_bake_argb): an icon on the baked level draws unless its
 * tile is a hole onto the level below, and an icon one level up draws where that
 * tile is VisBelow (balcony/overhang showing the floor beneath). */
static void
app_bake_mapscenes(struct App* app, uint32_t* argb, int pw, int ph, int level)
{
    struct World* world = app->world;
    int mapscene_scene;
    int count = 0;
    struct ToriDraw_Sprite** frames;
    int scene_size, plane;
    uint8_t const* flags;

    if( !world || world->mapscene_count <= 0 || !world->minimap )
        return;
    mapscene_scene = UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPSCENE);
    if( mapscene_scene <= 0 )
        return;
    frames = ToriDraw_SceneSpriteGet(app->scene, mapscene_scene, &count);
    if( !frames || count <= 0 )
        return;

    scene_size = world->_scene_size;
    plane = scene_size * scene_size;
    flags = world->tile_flags;

    for( int i = 0; i < world->mapscene_count; i++ )
    {
        struct World_MapSceneIcon const* icon = &world->mapscenes[i];
        struct ToriDraw_Sprite* spr;
        int idx, draw = 0;

        if( icon->mapscene < 0 || icon->mapscene >= count )
            continue;
        if( icon->x < 0 || icon->x >= scene_size || icon->z < 0 || icon->z >= scene_size )
            continue;
        spr = frames[icon->mapscene];
        if( !spr || !spr->pixels_argb || spr->width <= 0 || spr->height <= 0 )
            continue;

        idx = icon->x + icon->z * scene_size;
        if( icon->level == level &&
            (!flags ||
             (flags[idx + level * plane] &
              (MINIMAP_FLAG_VIS_BELOW | MINIMAP_FLAG_FORCE_HIGH_DETAIL)) == 0) )
            draw = 1;
        else if( flags && icon->level == level + 1 && level + 1 < world->minimap->levels &&
                 (flags[idx + (level + 1) * plane] & MINIMAP_FLAG_VIS_BELOW) != 0 )
            draw = 1;
        if( !draw )
            continue;

        app_plot_mapscene_sprite(
            argb, pw, ph, spr, icon->x, icon->z, world->minimap->height, icon->width,
            icon->length);
    }
}

/* Bake the loaded world's minimap tiles into a single scene sprite the minimap
 * widget blits from (v1 GameRunescape_RebuildWorldMap). SceneSpriteAdd frees any
 * previous entry, so the reload hotkey just overwrites in place.
 *
 * The bake is per level (reference minimapBuildBuffer(minusedlevel)), so it has
 * to be redone whenever the local player changes floor — app_world_map_poll. */
static void
app_rebuild_world_map(struct App* app, int level)
{
    int pixel_w = 0;
    int pixel_h = 0;
    uint32_t* argb;
    struct ToriDraw_Sprite* sprite;
    struct ToriDraw_Sprite** sprites;

    assert(app);
    assert(app->world);

    if( !app->world->minimap )
        return;

    argb = minimap_bake_argb(
        app->world->minimap, level, app->world->tile_flags, &pixel_w, &pixel_h);
    if( !argb )
        return;

    /* Reference drawDetail plots loc mapscene sprites (trees, rocks, altars, …)
     * into the same minimap image as the tiles/walls. */
    app_bake_mapscenes(app, argb, pixel_w, pixel_h, level);

    /* TORIRS_MINIMAP_BMP=path: the baked map straight to disk. The on-screen
     * minimap is a rotated, camera-anchored crop of this and needs a local
     * player to center on, so offline runs can only inspect the bake here. */
    if( getenv("TORIRS_MINIMAP_BMP") )
    {
        bmp_write_file(getenv("TORIRS_MINIMAP_BMP"), (int*)argb, pixel_w, pixel_h);
        fprintf(
            stderr,
            "minimap: wrote %s (%dx%d level=%d)\n",
            getenv("TORIRS_MINIMAP_BMP"),
            pixel_w,
            pixel_h,
            level);
    }

    sprite = ToriDraw_SpriteNewFromArgbOwned(argb, pixel_w, pixel_h);
    if( !sprite )
    {
        free(argb);
        return;
    }

    sprites = malloc(sizeof(*sprites));
    if( !sprites )
    {
        ToriDraw_SpriteFree(sprite);
        return;
    }
    sprites[0] = sprite;

    ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_WORLD_MAP_SPRITE_ID, sprites, 1);
    app->world_map_scene_id = UITREE_SCENE_WORLD_MAP_SPRITE_ID;
    app->world_map_w = pixel_w;
    app->world_map_h = pixel_h;
    app->world_map_level = level;
}

/* Reference checkMinimap/minimapBuildBuffer trigger (Client.ts:5331): rebake
 * whenever the level the map was baked for stops matching the player's, or a
 * runtime loc change edited the wall/door bits (world->minimap_seq — an opened
 * door's red line has to move on the baked sprite). */
static void
app_world_map_poll(struct App* app)
{
    static unsigned baked_minimap_seq = 0;
    struct WorldEntity_Player* local;

    if( !app->world || !app->world->load_complete || app->world_map_scene_id <= 0 )
        return;
    local = app_local_player(app);
    if( !local )
        return;
    if( local->grid_position.level == app->world_map_level &&
        baked_minimap_seq == app->world->minimap_seq )
        return;
    baked_minimap_seq = app->world->minimap_seq;
    app_rebuild_world_map(app, local->grid_position.level);
    app->need_redraw = 1;
}

static void
app_send_map_build_complete(struct App* app);

/* Task_WorldLoad on_done trampoline: adapts the void* hook to App_WorldLoadFinish. */
static void
app_world_load_finish_cb(void* userdata)
{
    App_WorldLoadFinish((struct App*)userdata);
}

/* Queue Task_WorldLoad for a chunk list; never blocks. App_WorldLoadFinish runs
 * as the task's on_done the moment the load lands (no polling). Reused by the
 * reload hotkey and the first-load trigger; assets already cached make a reload
 * near-instant. chunks == NULL -> the configured/default map. The REBUILD_NORMAL
 * packet task queues its own load (it awaits it) rather than calling here. */
static void
app_world_load_begin(
    struct App* app,
    int const* chunks_xz,
    int chunk_pair_count)
{
    int chunks[2] = { 50, 50 };
    struct ToriRS_Task* task;

    if( !chunks_xz )
    {
        char const* env;

        /*
         * Spawn square precedence: TORIRS_WORLD_MAP, then the manifest's `[cache:boot] spawn`,
         * then the client default of 50,50.
         *
         * The manifest layer matters because 50,50 is not universally loadable. A cache carries
         * XTEA keys only for the squares it was dumped with, and cache.643 has no key for
         * 50,50 (nor 49,49 / 50,49 / 51,49 / 51,50 — a hole right over Lumbridge). Terrain is
         * unencrypted, so an unkeyed square still renders ground and then **zero locs**, which
         * looks like a broken renderer rather than absent data.
         */
        if( app->cfg.spawn_x >= 0 && app->cfg.spawn_z >= 0 )
        {
            chunks[0] = app->cfg.spawn_x;
            chunks[1] = app->cfg.spawn_z;
        }
        env = getenv("TORIRS_WORLD_MAP");
        if( env )
        {
            int env_x;
            int env_z;
            if( sscanf(env, "%d,%d", &env_x, &env_z) == 2 )
            {
                chunks[0] = env_x;
                chunks[1] = env_z;
            }
            else
            {
                fprintf(
                    stderr,
                    "TORIRS_WORLD_MAP must be \"x,z\", got '%s' - using %d,%d\n",
                    env,
                    chunks[0],
                    chunks[1]);
            }
        }
        chunks_xz = chunks;
        chunk_pair_count = 1;
    }

    app->world_load_attempted = 1;
    app->world_load_inflight = 1;
    task = CreateTask_WorldLoad(
        app->provider, app->world_builder, chunks_xz, chunk_pair_count,
        app_world_load_finish_cb, app);
    ToriRS_TaskQueue_Add(app->runner.queue, task);
    app->need_redraw = 1;
}

/* Post-load wiring, split from the old synchronous app_world_load: camera
 * placement, height fn, texture requests, minimap bake, and the server ack
 * when the load was REBUILD_NORMAL-driven. */
void
App_WorldLoadFinish(struct App* app)
{
    app->world_load_inflight = 0;

    if( app->world->load_complete )
    {
        app->world_active = 1;
        World_SetHeightFn(app->world, app_world_height, app);
        {
            struct World_SeqSource seq_source = {
                .userdata = app,
                .frame_count = app_seq_frame_count,
                .frame_duration = app_seq_frame_duration,
                .frame_step = app_seq_frame_step,
                .max_loops = app_seq_max_loops,
                .priority = app_seq_priority,
                .duplicate_behavior = app_seq_duplicate_behavior,
                .preanim_move = app_seq_preanim_move,
                .stretches = app_seq_stretches,
                .spotanim_seq = app_spotanim_seq,
            };
            World_SetSeqSource(app->world, &seq_source);
        }
        /* v1 scene-reset camera: scene center, above ground, looking down. */
        app->world_camera_pos.x = app->world->_scene_size / 2 * 128 + 64;
        app->world_camera_pos.z = app->world->_scene_size / 2 * 128 + 64;
        app->world_camera_pos.y = -2000;
        app->world_camera.pitch = 450;
        app->world_camera.yaw = 0;
        /* TORIRS_WORLD_CAM=x,y,z,pitch,yaw: place the camera explicitly (scene coords, y negative
         * above ground). The startup camera looks near-straight down, where same-tile locs barely
         * overlap, so the headless BMP path cannot otherwise reproduce an oblique view — the sim
         * harness only injects letters/digits and pitch is bound to the arrow keys. */
        {
            char const* cam = getenv("TORIRS_WORLD_CAM");
            int cx, cy, cz, cpitch, cyaw;
            if( cam && sscanf(cam, "%d,%d,%d,%d,%d", &cx, &cy, &cz, &cpitch, &cyaw) == 5 )
            {
                app->world_camera_pos.x = cx;
                app->world_camera_pos.y = cy;
                app->world_camera_pos.z = cz;
                app->world_camera.pitch = cpitch;
                app->world_camera.yaw = cyaw;
            }
        }
        /* World scenery models reference textures; the bridge scan walks the
         * scene elements the rebuild just created. */
        app_sync_textures(app);
        {
            struct WorldEntity_Player* local = app_local_player(app);
            app_rebuild_world_map(app, local ? local->grid_position.level : 0);
        }

        if( app->world_load_server_driven )
        {
            app->world_load_server_driven = 0;
            app_send_map_build_complete(app);
        }
    }
    else
    {
        app->world_load_server_driven = 0;
        fprintf(stderr, "app: world load incomplete\n");
    }
    app->need_redraw = 1;
}

/* Cache the WORLD node's emit desc: the mouse gate rect and the viewport the
 * frame emitter draws with (pick/render parity comes from sharing it). Also the
 * "is a world on screen this frame" flag every world subsystem gates on. Uses
 * the previous frame's emit buffer — the world box only changes on relayout. */
/* TORIRS_POS_DEBUG=1: the local player's authoritative tile in every frame of
 * reference at once — scene tile, absolute world tile (compare against the
 * server's ::getcoord), fine draw position, and the camera the painter used.
 * The one-liner for "is the player where the server thinks it is". */
static void
app_debug_log_position(struct App* app)
{
    struct WorldEntity_Player* local;

    if( !getenv("TORIRS_POS_DEBUG") || !app->world )
        return;
    local = app_local_player(app);
    if( !local )
        return;
    fprintf(
        stderr,
        "pos: scene=%d,%d route0=%d,%d abs=%d,%d level=%d draw=%u,%u y=%d flags=%02x/%02x "
        "cam=%d,%d,%d yaw=%d pitch=%d\n",
        local->grid_position.x,
        local->grid_position.z,
        local->pathing.route_x[0],
        local->pathing.route_z[0],
        app->world->_base_tile_x + local->pathing.route_x[0],
        app->world->_base_tile_z + local->pathing.route_z[0],
        local->grid_position.level,
        local->draw_position.x,
        local->draw_position.z,
        /* Ground y under the player + the land settings of its column at the
         * player's level / level 1 — the bridge bump (LinkBelow 0x02 at
         * level 1) is only visible here. */
        app_world_height(
            app,
            (int)local->draw_position.x,
            (int)local->draw_position.z,
            local->grid_position.level),
        (unsigned)World_TileFlagGet(
            app->world, local->grid_position.x, local->grid_position.z,
            local->grid_position.level),
        (unsigned)World_TileFlagGet(
            app->world, local->grid_position.x, local->grid_position.z, 1),
        app->world_camera_pos.x,
        app->world_camera_pos.y,
        app->world_camera_pos.z,
        app->world_camera.yaw,
        app->world_camera.pitch);
}

/* TORIRS_BRIDGE_DEBUG=1: list every LinkBelow column in the loaded scene with
 * the level-0/level-1 ground heights the getAvH bump chooses between. The
 * "am I standing on the deck or under it" one-liner. Prints once per load. */
static void
app_debug_log_bridges(struct App* app)
{
    static unsigned logged_seq = 0;
    int count = 0;

    if( !getenv("TORIRS_BRIDGE_DEBUG") || !app->world || !app->world->load_complete )
        return;
    if( app->world->load_seq == logged_seq )
        return;
    logged_seq = app->world->load_seq;

    for( int x = 0; x < app->world->_scene_size; x++ )
    {
        for( int z = 0; z < app->world->_scene_size; z++ )
        {
            if( (World_TileFlagGet(app->world, x, z, 1) & RSCACHE_FLOFLAG_LINK_BELOW) == 0 )
                continue;
            count++;
            if( count > 40 )
                continue;
            fprintf(
                stderr,
                "bridge: scene=%d,%d abs=%d,%d y0=%d y1=%d\n",
                x,
                z,
                app->world->_base_tile_x + x,
                app->world->_base_tile_z + z,
                heightmap_get_interpolated(app->world->heightmap, x * 128 + 64, z * 128 + 64, 0),
                heightmap_get_interpolated(app->world->heightmap, x * 128 + 64, z * 128 + 64, 1));
        }
    }
    fprintf(stderr, "bridge: %d link-below columns in scene\n", count);
}

static void
app_update_world_viewport(struct App* app)
{
    app_debug_log_position(app);
    app_debug_log_bridges(app);
    app->world_view_valid = 0;
    app->minimap_view_valid = 0;
    if( getenv("TORIRS_WORLD_VIEW_DEBUG") )
    {
        int kinds[24] = { 0 };
        for( int i = 0; i < app->emit.count; i++ )
            if( app->emit.cmds[i].kind >= 0 && app->emit.cmds[i].kind < 24 )
                kinds[app->emit.cmds[i].kind]++;
        fprintf(stderr, "worldview: node_index=%d emit_count=%d kinds:", App_WorldNodeIndex(app), app->emit.count);
        for( int k = 0; k < 24; k++ )
            if( kinds[k] )
                fprintf(stderr, " %d:%d", k, kinds[k]);
        int wx = 0, wy = 0, ww = 0, wh = 0, widx = -1;
        for( int i = 0; i < app->emit.count; i++ )
            if( app->emit.cmds[i].kind == UITREE_EMIT_WORLD )
            {
                wx = app->emit.cmds[i].x; wy = app->emit.cmds[i].y;
                ww = app->emit.cmds[i].w; wh = app->emit.cmds[i].h; widx = i;
            }
        fprintf(stderr, " WORLDRECT=%d,%d %dx%d idx=%d\n", wx, wy, ww, wh, widx);
        /* Anything drawn after the world that covers a meaningful slice of it. */
        for( int i = widx + 1; i < app->emit.count; i++ )
        {
            struct UITreeEmitDesc* d = &app->emit.cmds[i];
            int ox = d->x > wx ? d->x : wx, oy = d->y > wy ? d->y : wy;
            int ex = (d->x + d->w) < (wx + ww) ? (d->x + d->w) : (wx + ww);
            int ey = (d->y + d->h) < (wy + wh) ? (d->y + d->h) : (wy + wh);
            int area = (ex - ox) > 0 && (ey - oy) > 0 ? (ex - ox) * (ey - oy) : 0;
            if( area > (ww * wh) / 20 )
                fprintf(
                    stderr,
                    "  occluder idx=%d kind=%d comp=%d node=%d rect=%d,%d %dx%d trans=%d "
                    "overlap=%d%%\n",
                    i, d->kind, d->component_id, d->node_index, d->x, d->y, d->w, d->h,
                    d->trans, area * 100 / (ww * wh));
        }
    }
    for( int i = 0; i < app->emit.count; i++ )
    {
        if( app->emit.cmds[i].kind == UITREE_EMIT_WORLD )
        {
            app->world_emit_desc = app->emit.cmds[i];
            app->world_view_valid = 1;
        }
        else if( app->emit.cmds[i].kind == UITREE_EMIT_MINIMAP )
        {
            app->minimap_emit_desc = app->emit.cmds[i];
            app->minimap_view_valid = 1;
        }
    }
}

int32_t
App_WorldNodeIndex(struct App const* app)
{
    assert(app);
    for( uint32_t i = 0; i < app->tree->component_count; i++ )
    {
        struct UITreeComponent const* node = &app->tree->components[i];
        if( !node->freed && node->type == UIELEM_BUILTIN_WORLD )
            return (int32_t)i;
    }
    return -1;
}

/* The async boot protothread: builds the root tree (RevConfig or cache
 * interface open), awaits the overlay font, then runs the synchronous
 * seeding steps and flips the app READY. All IO flows through the platform
 * pump via the per-frame task stepping — nothing here blocks the frame loop. */
struct Task_AppBoot
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
};

static int
Task_AppBoot_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_AppBoot* self = (struct Task_AppBoot*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    app->boot_progress = 10;

    /*
     * Varbit types, before anything that can run a script.
     *
     * A varbit read happens deep inside CS2 execution with nowhere to yield to a
     * load, so the table has to be resident first. Without it every varbit reads 0
     * and any script branching on one silently takes the zero path.
     *
     * Both eras, different storage: dat2 splits config group 14 into one file per id,
     * dat1 packs them into a single `varbit.dat` inside the config jagfile. CS1 reads
     * varbits through the same VarPManager as CS2, so both need it.
     */
    if( app->cfg.cache_kind == APP_CACHE_DAT1 )
        TASK_AWAITSELF_IF(CreateTask_Dat1VarbitLoad(app->provider, &app->varps));
    else
        TASK_AWAITSELF_IF(CreateTask_Dat2VarbitLoad(app->provider, &app->varps));

    /* `[ui:varc]` — the var writes that accompany the login IF_OPENSUB burst.
     * Before the tree opens, because the root's onLoad scripts branch on them.
     * Skipped when networked, for the same reason [ui:gameframe] is. */
    if( app->cfg.varc_seed_count > 0 && !app->net_enabled )
    {
        for( int i = 0; i < app->cfg.varc_seed_count; i++ )
            VarCManager_SetInt(
                &app->varcs, app->cfg.varc_seeds[i].id, app->cfg.varc_seeds[i].value);
        printf("varc: seeded %d client vars from the manifest\n", app->cfg.varc_seed_count);
    }

    if( app->builder_active )
    {
        TASK_AWAITSELF(CreateTask_UITreeBuild(&app->builder));
        printf(
            "RevConfigBuild done: ui=%s tree_components=%u sprites=%d fonts=%d onloads=%d\n",
            app->cfg.revconfig_ui_ini,
            app->tree->component_count,
            app->builder.sprite_count,
            app->builder.font_count,
            app->builder.onload_count);
    }
    else
    {
        /* Open the requested interface directly as the tree root (TS parity:
         * WidgetManager.setRootInterface(groupId) — any group can be the root;
         * no hardcoded 161 chrome required). */
        TASK_AWAITSELF(CreateTask_InterfaceOpen(
            app->provider,
            app->tree,
            &app->host,
            &app->invs,
            &app->bridge,
            app->boot_interface_id,
            &app->open_stats));
        printf(
            "InterfaceOpen done: iface=%d pack_components=%d tree_components=%u onloads=%d "
            "inv_hooks=%d var_hooks=%d mounts=%d\n",
            app->open_stats.interface_id,
            app->open_stats.pack_component_count,
            app->tree->component_count,
            app->open_stats.onload_count,
            app->host.inv_transmit_hook_count,
            app->host.var_transmit_hook_count,
            app->tree->interface_parent_count);
    }

    app->boot_progress = 60;

    /* Minimenu/hovertext font before the overlay nodes are pushed. */
    TASK_AWAITSELF_IF(CreateTask_FontLoad(app->provider, app_font_b12_cache_id(app)));

    app->boot_progress = 75;

    if( getenv("TORIRS_ANIM_DEBUG") )
    {
        for( uint32_t i = 0; i < app->tree->component_count; i++ )
        {
            struct UITreeComponent const* node = &app->tree->components[i];
            if( node->freed || node->type != UIELEM_RS_MODEL )
                continue;
            fprintf(
                stderr,
                "anim_debug: com=0x%x model=%d seq=%d\n",
                node->component_id,
                node->u.rs_model.gamecache_model_id,
                node->u.rs_model.anim_seq_id);
        }
    }

    app_push_builtin_overlay_nodes(app);

    /*
     * `[ui:gameframe]` — the login IF_OPENSUB burst, from the manifest.
     *
     * A gameframe root is a frame of empty slots; every panel in it (chat box,
     * orbs, the sidebar tabs, the inventory) arrives as a separate IF_OPENSUB
     * once the server has the player. Offline there is no server, so the frame
     * renders as chrome around nothing. Listing the mounts in the manifest
     * reproduces the burst without inventing a client-side default: the values
     * are whatever the era's server actually sends (for rev 634 they are read
     * off Void's `openGamframe` — see docs/RS2_634_CLIENT_REFERENCES.md).
     *
     * Skipped when networked: the server sends the real sequence, and mounting
     * on top of it would fight whatever it opens.
     */
    if( app->cfg.gameframe_mount_count > 0 && !app->net_enabled )
    {
        for( int i = 0; i < app->cfg.gameframe_mount_count; i++ )
        {
            struct BootManifestGameframeMount const* mount = &app->cfg.gameframe_mounts[i];
            int parent = mount->parent_interface_id > 0 ? mount->parent_interface_id
                                                        : app->boot_interface_id;
            App_OpenSubInterface(
                app, (parent << 16) | (mount->component & 0xFFFF), mount->interface_id, 0);
        }
        printf(
            "gameframe: queued %d sub-interface mounts under iface %d\n",
            app->cfg.gameframe_mount_count,
            app->boot_interface_id);
    }

    /* Tab/interface-slot state seeds from the baked tree (INI componentno= and
     * selected= drive it; nothing here is hardcoded). */
    RS_UISlots_InitFromTree(&app->slots, app->tree);

    /* Queue model-widget sequences (they land through the frame pump and
     * render at rest pose meanwhile) and apply whatever is already loaded. */
    UITreeAnim_RequestMissing(
        app->tree, app->scene, app->provider, app->runner.queue, &app->seq_loads);
    UITreeAnim_Advance(app->tree, app->scene, 0);

    app_sync_textures(app);

    /* No viewport component in the opened interface -> no map at all. Trees
     * that grow one later (a mounted interface) load lazily in App_RunOnce.
     * When networked, the server's REBUILD_NORMAL is the sole world-load driver
     * (its region + scene base match the entities); a default region 50,50 load
     * here would race it and clobber the rebuilt scene, so skip it. */
    if( App_WorldNodeIndex(app) >= 0 && !app->net_enabled )
        app_world_load_begin(app, NULL, 0);

    app_chat_build_view(app);
    app->emit.count = 0;
    UITree_EmitWalk(app->tree, &app->ui_host, &app->emit, -1);
    /* Prime the viewport cache so the first App_Render can draw the world
     * without waiting for a App_RunOnce pass to latch it. */
    app_update_world_viewport(app);

    app->boot_progress = 100;
    app->app_state = APP_STATE_READY;
    app->pending_tree_refresh = 1;
    app->need_redraw = 1;

    PT_END(&self->pt);
}

static void
Task_AppBoot_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_AppBoot_VTable = {
    .run = Task_AppBoot_Run,
    .free = Task_AppBoot_Free,
};

void
App_OpenRootInterface(
    struct App* app,
    int interface_id)
{
    struct Task_AppBoot* task;

    assert(app);
    memset(&app->open_stats, 0, sizeof(app->open_stats));

    if( getenv("TORIRS_CS2_TRACE") )
        g_cs2_trace_mode = 2;

    if( app->cfg.revconfig_ui_ini && app->cfg.revconfig_ui_ini[0] )
    {
        /* RevConfig build: the INI names the whole gameframe (chrome widgets
         * plus the cache interface packs mounted under them), so it replaces
         * the interface open rather than wrapping it. The CS2 host is passed
         * only for dat2 — dat1 interface packs carry IF1 scripts, which the
         * CS1 host evaluates on the tick instead. */
        UITreeBuilder_InitEx(
            &app->builder,
            app->provider,
            app->tree,
            &app->invs,
            App_UiLogic(app) == APP_UI_LOGIC_CS1 ? NULL : &app->host,
            app->cfg.revconfig_ui_ini,
            app->cfg.revconfig_cache_ini);
        /* Bake remaps sprite/font ids to scene ids so the tree renders directly. */
        app->builder.bridge = &app->bridge;
        app->builder_active = 1;
    }

    app->app_state = APP_STATE_BOOTING;
    app->boot_interface_id = interface_id;
    app->boot_progress = 0;
    /* The booted gameframe root is what IF_GETTOP reports: this client mounts
     * every server sub-interface into it and treats a differing server
     * IF_OPENTOP as informational (see rs_gameproto_exec), so the two agree. */
    app->host.top_interface_id = interface_id;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppBoot_VTable;
    strncpy(task->task.name, "AppBoot", sizeof(task->task.name) - 1);
    task->app = app;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->runner.queue, &task->task);
}

/* IF_OPENSUB wrapper: mount a cache interface pack under a component slot of an
 * already-open root, then relayout + re-request CS1 over the new subtree. Runs
 * on the serial exec pipeline so a mount a packet triggers completes before the
 * next packet is popped (packet order holds), mirroring rs_ui_slots' slot mount.
 * type -1 means close (unmount via CreateTask_InterfaceOpenSub with iface<=0). */
struct Task_OpenSubRefresh
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int target_uid;
    int interface_id;
    int type;
};

static int
Task_OpenSubRefresh_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_OpenSubRefresh* self = (struct Task_OpenSubRefresh*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);
    if( self->interface_id > 0 )
    {
        TASK_AWAITSELF_IF(CreateTask_InterfaceOpenSub(
            app->provider,
            app->tree,
            &app->host,
            &app->invs,
            &app->bridge,
            self->target_uid,
            self->interface_id,
            self->type,
            &app->open_stats));
    }
    else
    {
        /* IF_CLOSESUB: drop the mount record and hide the slot's content. The
         * mount task asserts interface_id>0, so a close never routes through it. */
        UITree_InterfaceParentClear(app->tree, self->target_uid);
        UITree_ApplyHide(app->tree, self->target_uid, 1);
    }
    App_RefreshAfterTreeMutation(app);
    PT_END(&self->pt);
}

static void
Task_OpenSubRefresh_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_OpenSubRefresh_VTable = {
    .run = Task_OpenSubRefresh_Run,
    .free = Task_OpenSubRefresh_Free,
};

static void
app_enqueue_open_sub(
    struct App* app,
    int target_uid,
    int interface_id,
    int type)
{
    struct Task_OpenSubRefresh* task;

    assert(app);
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_OpenSubRefresh_VTable;
    strncpy(task->task.name, "OpenSubRefresh", sizeof(task->task.name) - 1);
    task->app = app;
    task->target_uid = target_uid;
    task->interface_id = interface_id;
    task->type = type;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

void
App_OpenSubInterface(
    struct App* app,
    int target_uid,
    int interface_id,
    int type)
{
    assert(app);
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "if-opensub: mount iface=%d under uid=0x%08x (%d<<16|%d) type=%d\n",
            interface_id,
            (unsigned)target_uid,
            (target_uid >> 16) & 0xffff,
            target_uid & 0xffff,
            type);
    app_enqueue_open_sub(app, target_uid, interface_id, type);
}

void
App_CloseSubInterface(
    struct App* app,
    int target_uid)
{
    assert(app);
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "if-closesub: unmount uid=0x%08x (%d<<16|%d)\n",
            (unsigned)target_uid,
            (target_uid >> 16) & 0xffff,
            target_uid & 0xffff);
    /* iface_id <= 0 makes the mount task just clear the slot. */
    app_enqueue_open_sub(app, target_uid, -1, 0);
}

/* Shared per-frame completion polls for async work (world load, textures,
 * deferred seq binds, tree refresh). Not run while BOOTING. */
static void
app_async_polls(struct App* app)
{
    /* World-load completion is no longer polled here: Task_WorldLoad runs
     * App_WorldLoadFinish itself at its synchronous tail (via on_done, or the
     * REBUILD_NORMAL path's inline call after it awaits the load). */
    app_world_map_poll(app);
    app_sync_textures_poll(app);
    app_world_bind_pending_seqs(app);

    if( app->pending_tree_refresh )
    {
        app->pending_tree_refresh = 0;
        UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        app_request_cs1_eval(app);
        app->need_redraw = 1;
    }
}

void
App_BootWait(struct App* app)
{
    /* Headless harnesses/tests only: step both pipelines until the boot task
     * AND every load it queued (world, anims, textures) settle. IO still runs
     * exclusively inside the platform pump; the interactive loop never calls
     * this — it renders the loading state instead. */
    long guard = 1000000;

    assert(app);
    while( guard-- > 0 )
    {
        enum TaskRunnerStat main_stat = TaskRunner_Step(&app->runner);
        enum TaskRunnerStat exec_stat = TaskRunner_Step(&app->exec_runner);
        if( app->app_state == APP_STATE_READY )
            app_async_polls(app);
        if( main_stat == TASK_RUNNER_IDLE && exec_stat == TASK_RUNNER_IDLE &&
            app->app_state == APP_STATE_READY && !app->pending_tree_refresh &&
            !app->world_load_inflight )
            break;
    }
    if( guard <= 0 )
        fprintf(stderr, "app: boot wait exceeded step guard\n");
}

/* One 20ms client tick: clock, widget timers, animation loads + advance. */
static int
app_logic_tick(struct App* app)
{
    int redraw = 0;

    app->logic_cycle++;

    /* Pump the serial game-action pipeline: run whatever packet task /
     * slot mount / spawn is at the head, and only pop a NEW packet off the
     * network FIFO when the queue is idle — this is what preserves wire
     * order even when a packet's exec enqueues follow-on tasks (a mount
     * enqueued by IF_OPENSIDE runs before the next packet is popped). Step
     * budget bounds a frame's work; a long chain (world rebuild) simply
     * spans frames while gating everything behind it. */
    {
        int pops = 0;
        for( int steps = 0; steps < 64; steps++ )
        {
            if( TaskRunner_Step(&app->exec_runner) == TASK_RUNNER_IDLE )
            {
                struct RevPacket packet;
                if( !app->net || pops >= 10 ||
                    !ToriRS_Network_PopPacket(app->net, &packet) )
                    break;
                ToriRS_TaskQueue_Add(
                    app->exec_runner.queue, CreateTask_GameProtoExec(app, &packet));
                pops++;
                redraw = 1;
            }
        }
    }

    /* Rasterize inventory item icons that the server's inv packets left
     * unresolved (queued on the same serial pipeline, so it runs after the
     * packets that dirtied the slots). */
    app_inv_icon_reconcile_tick(app);

    if( app->net )
    {
        /* Keepalive once per tick while in the game world (reference sends
         * NO_TIMEOUT to keep the connection from idling out). */
        if( app->net->state == TORIRS_NET_GAME )
            APP_NET_SEND(app, net_out_no_timeout(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf)));

        /* TORIRS_NET_CHEAT="tele 0,50,50,21,21;give bronze_sword": send ::
         * commands (';'-separated) right after login — headless harness hook
         * (the dev server grants staffmod, so tele/give work). */
        if( app->net->state == TORIRS_NET_GAME && !app->net_cheat_sent )
        {
            char const* cheat = getenv("TORIRS_NET_CHEAT");
            app->net_cheat_sent = 1;
            while( cheat && cheat[0] )
            {
                char one[96] = { 0 };
                char const* sep = strchr(cheat, ';');
                size_t len = sep ? (size_t)(sep - cheat) : strlen(cheat);
                if( len >= sizeof(one) )
                    len = sizeof(one) - 1;
                memcpy(one, cheat, len);
                if( one[0] )
                    APP_NET_SEND(
                        app,
                        net_out_client_cheat(
                            app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), one));
                cheat = sep ? sep + 1 : NULL;
            }
        }
    }

    /* Sound queue on the client tick: the server's play delays are in ticks and
     * the reference runs its queue from the same clock (soundsDoQueue). */
    RS_Audio_Tick(&app->audio, app->provider, &app->runner, &app->audio_out);

    RS_CS2Host_Tick(&app->host);

    /* clientCode-populated components (friends rows, list sizes, design
     * preview) refresh from live state each tick (reference clientComponent
     * runs inside the draw; ours is a tick pass so emit stays pure). This is an
     * old-gen (IF1/CS1) mechanism; modern UI drives the same state via CS2. */
    if( App_UiLogic(app) == APP_UI_LOGIC_CS1 &&
        RS_ClientCode_Tick(app->tree, &app->social, app->logic_cycle) )
        redraw = 1;

    /* World map panning and element flashing advance on the client tick, the
     * same clock the map's own onTimer scripts run on. */
    if( RS_WorldMap_Cycle(app->host.worldmap) )
        redraw = 1;

    /* onTimer fires once per client tick for every component with a timer
     * hook (reference processWidgetTimers). Component ids are snapshotted
     * first because a hook can CC_CREATE children and realloc the array. */
    {
        int timer_ids[256];
        int timer_n = 0;
        for( uint32_t ti = 0; ti < app->tree->component_count && timer_n < 256; ti++ )
        {
            struct UITreeComponent const* tc = &app->tree->components[ti];
            if( tc->component_id >= 0 && tc->runtime_hooks.on_timer.script_id > 0 )
                timer_ids[timer_n++] = tc->component_id;
        }
        for( int i = 0; i < timer_n; i++ )
        {
            int32_t idx = UITree_FindByComponentId(app->tree, timer_ids[i]);
            if( idx < 0 )
                continue;
            RS_CS2_DispatchHook(
                &app->host,
                &app->runner,
                timer_ids[i],
                &app->tree->components[idx].runtime_hooks.on_timer);
            redraw = 1;
        }
    }

    /* Widgets-loaded transmit traversal, once per tick (TS processWidgetTransmits).
     * Early-outs unless a widget was unhidden this tick; per-hook serial gating
     * means already-fired hooks run nothing. */
    RS_CS2_PumpTransmits(&app->host, &app->runner);

    /* CS1 (IF1) value scripts drive active state and %N text. The reference
     * re-evaluates them at draw time; here a task does it once per tick so the
     * VM's asset yields can be serviced asynchronously, and the emit pass just
     * reads the cached results (the task sets need_redraw on change). */
    app_request_cs1_eval(app);

    /* TORIRS_STATS=1: periodic growth diagnostics — component_count must stay
     * flat under the CC_DELETEALL/CC_CREATE rebuild pattern (reclamation). */
    {
        static int stats_enabled = -1;
        static int stats_tick = 0;
        if( stats_enabled < 0 )
            stats_enabled = getenv("TORIRS_STATS") != NULL;
        if( stats_enabled && ++stats_tick % 250 == 0 )
            fprintf(
                stderr,
                "torirs_stats: tick=%d components=%u free_head=%d inv_hooks=%d var_hooks=%d\n",
                stats_tick,
                app->tree->component_count,
                app->tree->free_head,
                app->host.inv_transmit_hook_count,
                app->host.var_transmit_hook_count);
    }

    {
        static int anim_dbg = -1;
        static int anim_dbg_tick = 0;
        if( anim_dbg < 0 )
            anim_dbg = getenv("TORIRS_ANIM_DEBUG") != NULL;
        if( anim_dbg && ++anim_dbg_tick % 25 == 0 )
        {
            for( uint32_t i = 0; i < app->tree->component_count; i++ )
            {
                struct UITreeComponent const* node = &app->tree->components[i];
                if( node->freed || node->type != UIELEM_RS_MODEL )
                    continue;
                if( node->u.rs_model.anim_seq_id < 0 )
                    continue;
                fprintf(
                    stderr,
                    "anim_tick t=%d com=0x%x seq=%d frame=%d\n",
                    anim_dbg_tick,
                    node->component_id,
                    node->u.rs_model.anim_seq_id,
                    node->u.rs_model.anim_frame);
            }
        }
    }

    /* Animations: request missing sequences (async), apply what's loaded.
     * In-flight sequences render at rest pose until they land. */
    UITreeAnim_RequestMissing(
        app->tree, app->scene, app->provider, app->runner.queue, &app->seq_loads);
    if( UITreeAnim_Advance(app->tree, app->scene, 1) )
        redraw = 1;

    /* CS2 hooks this tick may have ensured new textured models. */
    app_sync_textures(app);

    if( UICross_IsActive(&app->cross) )
    {
        UICross_Tick(&app->cross, APP_LOGIC_TICK_MS);
        redraw = 1;
    }

    return redraw;
}

/* Movers (players/npcs) and in-flight projectiles push their sim positions
 * into the scene elements the frame emitter draws (v1 synced projectiles;
 * movers were spawn-time only there because nothing pathed them). */
static void
app_world_sync_positions(struct App* app)
{
    struct World* world = app->world;
    struct World_EntityPool* pool;

    pool = &world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        if( !player || player->element_id < 0 )
            continue;
        /* Rebuild-parked movers sit outside the scene until the server's
         * next info packet removes them; the heightmap has no data there. */
        if( player->grid_position.x < 0 || player->grid_position.z < 0 ||
            player->grid_position.x >= world->_scene_size ||
            player->grid_position.z >= world->_scene_size )
            continue;
        int wx = (int)player->draw_position.x;
        int wz = (int)player->draw_position.z;
        int wy = app_world_height(app, wx, wz, player->grid_position.level);
        ToriDraw_SceneElementSetPosition(
            app->scene, player->element_id, wx, wy, wz, player->orientation.yaw);
    }

    pool = &world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        if( !npc || npc->element_id < 0 )
            continue;
        if( npc->grid_position.x < 0 || npc->grid_position.z < 0 ||
            npc->grid_position.x >= world->_scene_size ||
            npc->grid_position.z >= world->_scene_size )
            continue;
        int wx = (int)npc->draw_position.x;
        int wz = (int)npc->draw_position.z;
        int wy = app_world_height(app, wx, wz, npc->grid_position.level);
        ToriDraw_SceneElementSetPosition(
            app->scene, npc->element_id, wx, wy, wz, npc->orientation.yaw);
    }

    pool = &world->entities.projectile;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Projectile* proj = World_EntityPoolGet(pool, i);
        if( !proj || proj->element_id < 0 || !proj->launched )
            continue;
        ToriDraw_SceneElementSetPositionPitchYaw(
            app->scene,
            proj->element_id,
            (int)proj->x,
            (int)proj->y,
            (int)proj->z,
            proj->orientation.pitch,
            proj->orientation.yaw);
    }
}

/* One client tick of scene-element animation frames. UITreeAnim only advances
 * UI model widgets; world scene elements (scenery + entities) advance here
 * (v1 GameRunescape_TickAnimations, both classic and skeletal branches). */
static void
app_world_tick_animations(struct App* app)
{
    /* Only elements with a seq bound, rather than every slot in a pool that is
     * overwhelmingly static scenery. The list is a hint (it can hold ids that
     * have since died or lost their seq), so the per-element checks below still
     * stand — they are just no longer paid once per slot per cycle. */
    int anim_count = 0;
    int const* anim_ids = ToriDraw_SceneAnimatedElements(app->scene, &anim_count);
    for( int k = 0; k < anim_count; k++ )
    {
        int element_id = anim_ids[k];
        struct ToriDraw_SceneElement* element;

        if( !ToriDraw_SceneElementIsLive(app->scene, element_id) )
            continue;
        element = ToriDraw_SceneElementGet(app->scene, element_id);
        if( !element || element->anim_seq_id == -1 )
            continue;
        /* Entity elements: the world sim steps their frames (delay/loop/
         * priority semantics) — the naive modulo tick must not touch them. */
        if( element->anim_external )
            continue;

        if( element->is_skeletal )
        {
            const struct ToriDraw_SkeletalAnim* skeletal = element->skeletal_animation;
            int play_frames;
            if( !skeletal || skeletal->frame_count <= 0 )
                continue;
            play_frames = element->skeletal_play_frames;
            if( play_frames <= 0 || play_frames > skeletal->frame_count )
                play_frames = skeletal->frame_count;
            element->anim_cycle++;
            if( element->anim_cycle >= 1 )
            {
                element->anim_frame = (element->anim_frame + 1) % play_frames;
                element->anim_cycle = 0;
            }
        }
        else
        {
            const struct ToriDraw_Animation* anim = element->animation;
            if( !anim || anim->frame_count <= 0 || !anim->frames )
                continue;
            element->anim_cycle++;
            if( element->anim_cycle >= anim->frames[element->anim_frame].delay )
            {
                element->anim_frame = (element->anim_frame + 1) % anim->frame_count;
                element->anim_cycle = 0;
            }
        }
    }
}

enum
{
    APP_CAMERA_MOVEMENT_SPEED = 70, /* v1 RUNESCAPE_CAMERA_MOVEMENT_SPEED */
    APP_CAMERA_ROTATION_SPEED = 10,
};

static int
app_world_drawable(struct App* app)
{
    /* world_view_valid == a WORLD desc survived the last emit walk, so a hidden
     * or absent viewport component costs nothing: no paint, no 3D, no pick. */
    return app->world_view_valid && app->world && app->world->load_complete &&
           app->world->painter && app->painter_buffer;
}

/* Reference roofCheck (Client.ts 4713): walk the camera->player tile line;
 * if any stepped tile (endpoints included) carries the remove-roof land flag
 * (0x4), cut drawing down to the player's level, else draw all 4. Only armed
 * at low pitch (< 310) — the high look-down orbit clears roofs anyway.
 * Scripted cams use the simpler roofCheck2 height test. */
static int
app_world_roof_check(struct App* app)
{
    struct WorldEntity_Player* player = app_local_player(app);
    struct World* world = app->world;
    int top = 3;
    int level;

    if( !player || !world || !world->tile_flags )
        return 3;
    level = player->grid_position.level;

    if( app->cam_script.scripted )
    {
        int cam_tx = app->world_camera_pos.x >> 7;
        int cam_tz = app->world_camera_pos.z >> 7;
        int ground_y =
            app_world_height(app, app->world_camera_pos.x, app->world_camera_pos.z, level);
        if( ground_y - app->world_camera_pos.y >= 800 ||
            (World_TileFlagGet(world, cam_tx, cam_tz, level) & 0x4) == 0 )
            return 3;
        return level;
    }

    if( app->world_camera.pitch < 310 )
    {
        int cam_tx = app->world_camera_pos.x >> 7;
        int cam_tz = app->world_camera_pos.z >> 7;
        int ply_tx = (int)player->draw_position.x >> 7;
        int ply_tz = (int)player->draw_position.z >> 7;
        int delta_x = ply_tx > cam_tx ? ply_tx - cam_tx : cam_tx - ply_tx;
        int delta_z = ply_tz > cam_tz ? ply_tz - cam_tz : cam_tz - ply_tz;

        if( World_TileFlagGet(world, cam_tx, cam_tz, level) & 0x4 )
            top = level;

        if( delta_x > delta_z )
        {
            int delta = delta_x ? (delta_z * 65536) / delta_x : 0;
            int accumulator = 32768;
            while( cam_tx != ply_tx )
            {
                cam_tx += cam_tx < ply_tx ? 1 : -1;
                if( World_TileFlagGet(world, cam_tx, cam_tz, level) & 0x4 )
                    top = level;
                accumulator += delta;
                if( accumulator >= 65536 )
                {
                    accumulator -= 65536;
                    if( cam_tz != ply_tz )
                        cam_tz += cam_tz < ply_tz ? 1 : -1;
                    if( World_TileFlagGet(world, cam_tx, cam_tz, level) & 0x4 )
                        top = level;
                }
            }
        }
        else if( delta_z > 0 )
        {
            int delta = (delta_x * 65536) / delta_z;
            int accumulator = 32768;
            while( cam_tz != ply_tz )
            {
                cam_tz += cam_tz < ply_tz ? 1 : -1;
                if( World_TileFlagGet(world, cam_tx, cam_tz, level) & 0x4 )
                    top = level;
                accumulator += delta;
                if( accumulator >= 65536 )
                {
                    accumulator -= 65536;
                    if( cam_tx != ply_tx )
                        cam_tx += cam_tx < ply_tx ? 1 : -1;
                    if( World_TileFlagGet(world, cam_tx, cam_tz, level) & 0x4 )
                        top = level;
                }
            }
        }
    }

    if( World_TileFlagGet(
            world, (int)player->draw_position.x >> 7, (int)player->draw_position.z >> 7, level) &
        0x4 )
        top = level;
    return top;
}

/* Fill the painter buffer for the current camera. Called by App_Render once
 * per frame (painter_paint_bucket resets command_count, so repainting is
 * safe). */
static void
app_world_paint(struct App* app)
{
    /* >>7, not /128: the orbit eye can sit at negative coords past the scene
     * edge, and truncation toward zero would mis-seed the bucket flood-fill
     * origin by a tile. Clamp into the scene — the bucket's distance metric
     * and adjacency tests assume an in-bounds origin. */
    int cam_sx = app->world_camera_pos.x >> 7;
    int cam_sz = app->world_camera_pos.z >> 7;
    int cam_slevel = 0; /* painter_paint_bucket ignores it (iterates levels) */
    if( app->world )
    {
        int max_tile = app->world->_scene_size - 1;
        if( cam_sx < 0 )
            cam_sx = 0;
        if( cam_sx > max_tile )
            cam_sx = max_tile;
        if( cam_sz < 0 )
            cam_sz = 0;
        if( cam_sz > max_tile )
            cam_sz = max_tile;
    }
    /* The viewport component owns the level mask (RevConfig `levels=`); older
     * nodes leave it 0, which would draw nothing — treat that as all levels. */
    uint8_t level_mask = app->world_emit_desc.world_level_mask;
    if( cam_slevel < 0 )
        cam_slevel = 0;
    if( cam_slevel > 3 )
        cam_slevel = 3;
    if( !level_mask )
        level_mask = 0xF;
    /* Roof hiding: the per-frame camera->player roofCheck caps the top drawn
     * level; config (RevConfig levels=) can still restrict further. */
    level_mask &= (uint8_t)((1u << (app_world_roof_check(app) + 1)) - 1);
    /* CAM_SHAKE jitter (reference cutscene shake, per-axis sine). */
    if( app->cam_script.shake_axis >= 0 )
    {
        int jitter = (app->cam_script.shake_amplitude *
                      ToriDraw_Sin((int)(app->logic_cycle * app->cam_script.shake_speed) & 0x7ff)) >>
                     16;
        switch( app->cam_script.shake_axis )
        {
        case 0:
            cam_sx += jitter >> 7;
            break;
        case 1:
            app->world_camera_pos.y += jitter;
            break;
        case 2:
            cam_sz += jitter >> 7;
            break;
        case 3:
            app->world_camera.yaw = (app->world_camera.yaw + jitter) & 0x7ff;
            break;
        default:
            break;
        }
    }
    painter_set_camera_angles(app->world->painter, app->world_camera.pitch, app->world_camera.yaw);
    painter_set_level_mask(app->world->painter, level_mask);

    painter_paint_bucket(app->world->painter, app->painter_buffer, cam_sx, cam_sz, cam_slevel);

    /* TORIRS_PAINT_DEBUG: what the painter actually emitted this frame, by kind.
     * Scene elements existing is not the same as being painted — the bucket
     * flood-fill, the level mask and the cull map each drop work silently. */
    if( getenv("TORIRS_PAINT_DEBUG") )
    {
        int by_kind[16] = { 0 };
        for( int i = 0; i < app->painter_buffer->command_count; i++ )
            by_kind[app->painter_buffer->commands[i]._bf_kind & 0xF]++;
        fprintf(
            stderr,
            "paint: cam=%d,%d campos=(%d,%d,%d) pitch=%d yaw=%d level_mask=0x%x roof=%d "
            "commands=%d kinds:",
            cam_sx,
            cam_sz,
            (int)app->world_camera_pos.x,
            (int)app->world_camera_pos.y,
            (int)app->world_camera_pos.z,
            app->world_camera.pitch,
            app->world_camera.yaw,
            level_mask,
            app_world_roof_check(app),
            app->painter_buffer->command_count);
        for( int k = 0; k < 16; k++ )
            if( by_kind[k] )
                fprintf(stderr, " %d:%d", k, by_kind[k]);
        fprintf(stderr, "\n");
    }
}

/* "Only hittest the world if the mouse is over the world element": inside the
 * world emit clip rect with no *clickable* UI on top. Script-hover targets
 * (layers with only on_mouse_repeat / on_mouse_over — e.g. iface 548 child 40
 * on cache.643, which blankets the viewport and hammers a broken CS2) must NOT
 * block world pick: they are pass-through for clicks. Use HitTestInteractive,
 * not the CS2 hover walk (hover_com_id). */
static int
app_world_mouse_gate(
    struct App* app,
    int mouse_x,
    int mouse_y)
{
    struct UITreeEmitClip const* clip;
    struct UITreeEmitDesc const* desc;

    if( !app->world_active || !app->world_view_valid )
        return 0;
    /* A viewport interface (reference mainModalId) owns the entire viewport
     * rect: buildMinimenu adds that modal's component options there and NEVER
     * world options (Client.ts:2772 `if (mainModalId === -1) addWorldOptions
     * else addComponentOptions`). So while one is mounted, world picking stops
     * and the mouse cannot hittest the scene through the gaps between the
     * modal's components (e.g. the empty space between a shop's item slots). */
    if( app->slots.main_modal_id != -1 )
        return 0;
    /* Clickable UI wins over the world; pass-through layers with hover scripts
     * do not. */
    if( app->tree &&
        UITree_HitTestInteractive(app->tree, &app->ui_host, mouse_x, mouse_y) >= 0 )
        return 0;
    /* Gate on the world WIDGET rect, not just its clip: an unclipped world
     * node inherits a full-canvas clip, which let sidebar/chat clicks count
     * as "in world" — right-clicking an inventory item offered "Walk here"
     * (reference buildMinimenu adds world options only inside the viewport
     * rect 4..516 x 4..338). */
    desc = &app->world_emit_desc;
    if( desc->w > 0 && desc->h > 0 &&
        (mouse_x < desc->x || mouse_x >= desc->x + desc->w || mouse_y < desc->y ||
         mouse_y >= desc->y + desc->h) )
        return 0;
    clip = &app->world_emit_desc.clip;
    return mouse_x >= clip->x && mouse_x < clip->x + clip->w && mouse_y >= clip->y &&
           mouse_y < clip->y + clip->h;
}

/* Reference Client.tryMove for ground (type 0) / minimap (type 1) clicks:
 * BFS route on the local player's level from the player's final route tile
 * (routeX[0]) to the clicked scene tile with the try-nearest 3x3 fallback,
 * send the MOVE_* waypoint packet, latch the minimap flag from the routed
 * destination (route[0]). The local player is NOT moved here — the
 * PLAYER_INFO echo drives movement, exactly like the reference; the old
 * World_PlayerPathJump prediction fought the echo and made the player jump
 * around. Offline keeps the jump as scripted-scene feedback. Returns 1 when
 * a route was found and a packet sent (or offline feedback applied). */
static int
app_try_move(
    struct App* app,
    int dst_x,
    int dst_z,
    int type,
    int click_x,
    int click_y,
    int yaw,
    int ctrl_held)
{
    /* Reference routeX/routeZ scratch is 4000 entries (Client.ts:409). */
    static int route_x[4000];
    static int route_z[4000];
    struct World* world = app->world;
    struct WorldEntity_Player* player;
    struct CollisionMap* cm;
    int level, route_len, nearest = 0;
    bool online = app->net && app->net->state == TORIRS_NET_GAME;

    if( !world || !world->load_complete )
        return 0;
    if( dst_x < 0 || dst_z < 0 || dst_x >= world->_scene_size || dst_z >= world->_scene_size )
        return 0;

    player = app_local_player(app);
    if( !player )
    {
        /* Offline / not yet pid-synced: keep the old local jump so scripted
         * scenes still move the first spawned player. */
        int head = World_EntityPoolHead(&world->entities.player);
        if( !online && head != WORLD_ENTITY_NIL )
        {
            World_PlayerPathJump(world, head, false, dst_x, dst_z);
            app->minimap_flag_x = dst_x;
            app->minimap_flag_z = dst_z;
            return 1;
        }
        return 0;
    }

    level = player->grid_position.level;
    if( level < 0 )
        level = 0;
    if( level >= COLLISION_LEVELS )
        level = COLLISION_LEVELS - 1;
    cm = world->collision_maps[level];
    if( !cm )
        return 0;

    route_len = collision_map_try_route(
        cm,
        player->pathing.route_x[0],
        player->pathing.route_z[0],
        dst_x,
        dst_z,
        true,
        route_x,
        route_z,
        (int)(sizeof(route_x) / sizeof(route_x[0])),
        &nearest);
    if( route_len < 1 )
        return 0;

    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "trymove: type=%d src=%d,%d dst=%d,%d route_len=%d nearest=%d dest=%d,%d\n",
            type,
            player->pathing.route_x[0],
            player->pathing.route_z[0],
            dst_x,
            dst_z,
            route_len,
            nearest,
            route_x[0],
            route_z[0]);

    if( type == 1 )
        APP_NET_SEND(
            app,
            net_out_move_minimapclick(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                world->_base_tile_x,
                world->_base_tile_z,
                route_x,
                route_z,
                route_len,
                ctrl_held,
                click_x,
                click_y,
                yaw,
                0,
                0,
                (int)player->draw_position.x,
                (int)player->draw_position.z,
                nearest));
    else
        APP_NET_SEND(
            app,
            net_out_move_gameclick(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                world->_base_tile_x,
                world->_base_tile_z,
                route_x,
                route_z,
                route_len,
                ctrl_held));

    /* Reference tryMove latches the flag from the routed destination. */
    app->minimap_flag_x = route_x[0];
    app->minimap_flag_z = route_z[0];
    app->need_redraw = 1;
    return 1;
}

/* Reference tryMove type 2 (interactWithLoc / obj doAction): pathfind toward a
 * loc/obj using its approach footprint (tryNearest = false), and — when a route
 * exists — emit MOVE_OPCLICK. Unlike a ground click this arrives on an approach
 * tile beside the loc, not the loc tile itself. The caller sends the
 * OP(LOC|OBJ|NPC) afterwards regardless of the return, matching the reference
 * (the walk is best-effort; the interaction is always requested on the same
 * click). A reachable click always emits — even a zero-delta route when the
 * player already stands on an approach tile. Returns 1 when a route was found
 * (so an obj can skip its 1x1 fallback), 0 when unreachable. */
static int
app_try_move_op(
    struct App* app,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach,
    int ctrl_held)
{
    static int route_x[4000];
    static int route_z[4000];
    struct World* world = app->world;
    struct WorldEntity_Player* player;
    struct CollisionMap* cm;
    int level, route_len;

    if( !world || !world->load_complete )
        return 0;
    if( dst_x < 0 || dst_z < 0 || dst_x >= world->_scene_size || dst_z >= world->_scene_size )
        return 0;

    player = app_local_player(app);
    if( !player )
        return 0;

    level = player->grid_position.level;
    if( level < 0 )
        level = 0;
    if( level >= COLLISION_LEVELS )
        level = COLLISION_LEVELS - 1;
    cm = world->collision_maps[level];
    if( !cm )
        return 0;

    route_len = collision_map_try_route_op(
        cm,
        player->pathing.route_x[0],
        player->pathing.route_z[0],
        dst_x,
        dst_z,
        approach,
        route_x,
        route_z,
        (int)(sizeof(route_x) / sizeof(route_x[0])));
    if( route_len < 1 )
        return 0;

    APP_NET_SEND(
        app,
        net_out_move_opclick(
            app->net->rev,
            app->net->random_out,
            _nsbuf,
            sizeof(_nsbuf),
            world->_base_tile_x,
            world->_base_tile_z,
            route_x,
            route_z,
            route_len,
            ctrl_held));

    app->minimap_flag_x = route_x[0];
    app->minimap_flag_z = route_z[0];
    app->need_redraw = 1;
    return 1;
}

/* Reference tryMove type 2 toward an NPC (Client.ts OP_NPC1..5 / USEHELD_ONNPC /
 * TGT_NPC all do `tryMove(src, npc.routeX[0], npc.routeZ[0], false, 1, 1, 0, 0,
 * 0, 2)`): pathfind to a 1x1 approach beside the NPC's current route tile and
 * emit MOVE_OPCLICK, so the player walks into interaction/cast range on the same
 * click. Best-effort — the OP(NPC|NPCU|NPCT) packet is sent by the caller
 * regardless of the walk result, matching the reference. */
static int
app_try_move_npc(struct App* app, struct WorldEntity_NPC const* npc, int ctrl_held)
{
    struct CollisionApproach approach = { .loc_width = 1, .loc_length = 1 };
    if( !npc )
        return 0;
    return app_try_move_op(
        app, npc->pathing.route_x[0], npc->pathing.route_z[0], &approach, ctrl_held);
}

/* Build the op-click approach for a loc (reference interactWithLoc, Client.ts
 * 5817-5833). Centrepieces and ground decor approach by footprint (testLoc,
 * size already angle-swapped at register time); walls / wall decorations
 * approach an adjacent facing tile (testWall/testWDecor, locShape = shape + 1).
 * forceapproach stays 0 — LocType opcode 69 is not decoded yet, and it is 0 for
 * the vast majority of locs. */
static struct CollisionApproach
app_scenery_approach(struct WorldEntity_Scenery const* scenery)
{
    struct CollisionApproach approach = { 0 };
    int shape = scenery->shape;

    if( shape == RSCACHE_LOC_SHAPE_SCENERY || shape == RSCACHE_LOC_SHAPE_SCENERY_DIAGONAL ||
        shape == RSCACHE_LOC_SHAPE_FLOOR_DECORATION )
    {
        approach.loc_width = scenery->size_x;
        approach.loc_length = scenery->size_z;
    }
    else
    {
        approach.loc_angle = scenery->angle;
        approach.loc_shape = shape + 1;
    }
    return approach;
}

/* Reference tryMove type 2 toward a loc (Client.ts interactWithLoc, shared by
 * OP_LOC1..5 / USEHELD_ONLOC / TGT_LOC): pathfind to the loc's footprint/wall
 * approach and emit MOVE_OPCLICK. Best-effort — the caller sends the OP packet
 * regardless of the route result. */
static void
app_try_move_loc(struct App* app, int element_id, int tile_x, int tile_z)
{
    struct WorldEntity_Scenery const* scenery =
        World_SceneryGetByElementId(app->world, element_id);
    if( scenery )
    {
        struct CollisionApproach approach = app_scenery_approach(scenery);
        app_try_move_op(app, tile_x, tile_z, &approach, 0);
    }
}

/* Reference tryMove type 2 toward a ground obj (Client.ts OP_OBJ1..5 /
 * USEHELD_ONOBJ / TGT_OBJ): pathfind to the exact tile, and on failure retry a
 * 1x1 approach so an adjacent tile still arrives, then emit MOVE_OPCLICK.
 * Best-effort — the caller sends the OP packet regardless of the route. */
static void
app_try_move_obj(struct App* app, int tile_x, int tile_z)
{
    struct CollisionApproach exact = { 0 };
    if( !app_try_move_op(app, tile_x, tile_z, &exact, 0) )
    {
        struct CollisionApproach one = { .loc_width = 1, .loc_length = 1 };
        app_try_move_op(app, tile_x, tile_z, &one, 0);
    }
}

/* Minimap click-to-walk (reference minimapLoop, Client.ts 2990-3032): map the
 * click through the same rotation the blit drew with into a player-relative
 * tile, then MOVE_MINIMAPCLICK with the 14-byte anticheat trailer. The sin/cos
 * tables are 16.16, so >>11 leaves fine units directly (32 fine units per
 * minimap pixel at 4 px/tile). Returns 1 when the click was consumed. */
static int
app_minimap_click(
    struct App* app,
    int mouse_x,
    int mouse_y,
    int ctrl_held)
{
    struct UITreeEmitDesc const* desc = &app->minimap_emit_desc;
    struct WorldEntity_Player* player;
    int center_x, center_y, yaw, rel_x, rel_y;
    int tile_x, tile_z;

    if( !app->minimap_view_valid || !app->world || !app->world->load_complete )
        return 0;
    if( mouse_x < desc->x || mouse_x >= desc->x + desc->w || mouse_y < desc->y ||
        mouse_y >= desc->y + desc->h )
        return 0;
    player = app_local_player(app);
    if( !player )
        return 0;

    center_x = mouse_x - (desc->x + desc->w / 2);
    center_y = mouse_y - (desc->y + desc->h / 2);
    yaw = desc->rotation_r2pi2048 & 0x7ff;
    {
        int sin = ToriDraw_Sin(yaw);
        int cos = ToriDraw_Cos(yaw);
        rel_x = (center_y * sin + center_x * cos) >> 11;
        rel_y = (center_y * cos - center_x * sin) >> 11;
    }
    tile_x = ((int)player->draw_position.x + rel_x) >> 7;
    tile_z = ((int)player->draw_position.z - rel_y) >> 7;
    if( tile_x < 0 || tile_z < 0 || tile_x >= app->world->_scene_size ||
        tile_z >= app->world->_scene_size )
        return 0;

    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "minimap: click=%d,%d rel=%d,%d scene=%d,%d abs=%d,%d\n",
            center_x,
            center_y,
            rel_x,
            rel_y,
            tile_x,
            tile_z,
            app->world->_base_tile_x + tile_x,
            app->world->_base_tile_z + tile_z);
    app_try_move(app, tile_x, tile_z, 1, center_x, center_y, yaw, ctrl_held);
    app->need_redraw = 1;
    return 1;
}

/* Classify the raw hits the render pass collected into the app pickset +
 * hover tile. Runs after ToriRS_Soft3D_RenderFrame when the pick was armed. */
static void
app_world_pick_finish(
    struct App* app,
    struct ToriRS_PickHits const* hits)
{
    struct ToriRS_PickResult result;
    struct WorldEntity_Player* player = app_local_player(app);
    int player_level = player ? player->grid_position.level : -1;

    ToriRS_PickHitsClassify(app->world, hits, player_level, &app->world_pickset, &result);
    if( result.hover_tile_valid )
    {
        app->world_hover_tile_x = result.hover_tile_x;
        app->world_hover_tile_z = result.hover_tile_z;
        app->world_hover_tile_level = result.hover_tile_level;
    }
    else
    {
        app->world_hover_tile_x = -1;
        app->world_hover_tile_z = -1;
    }

    if( getenv("TORIRS_WORLD_PICK_DEBUG") )
    {
        fprintf(
            stderr,
            "world_pick: mouse=%d,%d count=%d hover_tile=%d,%d,%d\n",
            app->world_mouse_x,
            app->world_mouse_y,
            app->world_pickset.count,
            result.hover_tile_valid ? result.hover_tile_x : -1,
            result.hover_tile_valid ? result.hover_tile_z : -1,
            result.hover_tile_valid ? result.hover_tile_level : -1);
        for( int i = 0; i < app->world_pickset.count; i++ )
        {
            /* Loc id/name/footprint turn an element id into something you can look up in the
             * cache — the difference between "element 4345 draws late" and "the plinth is a
             * separate 1x1 loc one tile nearer than the statue". */
            struct WorldEntity_Scenery* scenery =
                World_SceneryGetByElementId(app->world, app->world_pickset.items[i].element_id);
            fprintf(
                stderr,
                "world_pick:  [%d] element=%d type=%d tile=%d,%d,%d loc=%d size=%dx%d origin=%d,%d,%d '%s'\n",
                i,
                app->world_pickset.items[i].element_id,
                (int)app->world_pickset.items[i].type,
                app->world_pickset.items[i].tile_x,
                app->world_pickset.items[i].tile_z,
                app->world_pickset.items[i].tile_level,
                scenery ? scenery->loc_id : -1,
                scenery ? scenery->size_x : -1,
                scenery ? scenery->size_z : -1,
                scenery ? scenery->grid_position.x : -1,
                scenery ? scenery->grid_position.z : -1,
                scenery ? scenery->grid_position.level : -1,
                scenery ? scenery->name : "");
        }
    }
}

static void
app_camera_move_forward(
    struct App* app,
    int amount)
{
    int direction_x = ToriDraw_Sin(app->world_camera.yaw);
    int direction_z = ToriDraw_Cos(app->world_camera.yaw);
    app->world_camera_pos.x -= (direction_x * amount) >> 16;
    app->world_camera_pos.z += (direction_z * amount) >> 16;
}

static void
app_camera_move_left(
    struct App* app,
    int amount)
{
    int direction_x = ToriDraw_Cos(app->world_camera.yaw);
    int direction_z = ToriDraw_Sin(app->world_camera.yaw);
    app->world_camera_pos.x += (direction_x * amount) >> 16;
    app->world_camera_pos.z += (direction_z * amount) >> 16;
}

/* World camera keys (v1 mapping): W/S forward/back, A/D strafe, R/F up/down,
 * arrows yaw/pitch. Suppressed while any visible onKey target wanted the
 * keyboard this frame, so typing in the UI never flies the camera. */
static void
app_world_camera_keys(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out)
{
    const int move = APP_CAMERA_MOVEMENT_SPEED;
    const int rotate = APP_CAMERA_ROTATION_SPEED;

    /* No key_target gating: the reference broadcasts every key to onKey
     * scripts AND moves the camera in the same frame; there is no focused
     * text-input concept to defer to yet. The viewport still has to be on
     * screen — with no world drawn these keys belong to the interface. */
    if( !app->world_active || !app->world_view_valid )
        return;
    /* Suppressed while the chat input line (or a modal text prompt) has focus,
     * so typing a message never flies the camera. */
    if( app->chat_input_active || app->chat.social_input_open || app->chat.dialog_input_open )
        return;
    (void)out;

    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_W) )
        app_camera_move_forward(app, move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_S) )
        app_camera_move_forward(app, -move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_A) )
        app_camera_move_left(app, -move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_D) )
        app_camera_move_left(app, move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_R) )
        app->world_camera_pos.y -= move;
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_F) )
        app->world_camera_pos.y += move;
    /* Arrows drive the orbit camera (reference keyHeld[1..4]); the follow
     * step consumes these next frame. When the follow cam is off (offline or
     * scripted) fall back to the free-cam direct rotate. */
    app->cam_key_left = LibToriRS_Input_IsKeyHeld(input, TORIRSK_LEFT);
    app->cam_key_right = LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT);
    app->cam_key_up = LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP);
    app->cam_key_down = LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN);
    if( app->cam_script.scripted || !app->net )
    {
        if( app->cam_key_left )
            app->world_camera.yaw = ToriDraw_AddAngle(app->world_camera.yaw, rotate);
        if( app->cam_key_right )
            app->world_camera.yaw = ToriDraw_AddAngle(app->world_camera.yaw, -rotate);
        if( app->cam_key_up )
            app->world_camera.pitch = ToriDraw_AddAngle(app->world_camera.pitch, rotate);
        if( app->cam_key_down )
            app->world_camera.pitch = ToriDraw_AddAngle(app->world_camera.pitch, -rotate);
    }

    /* M: reload the world through the task system (assets cached -> fast;
     * rebuild clears world scene elements incl. spawned entities). */
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_M) && App_WorldNodeIndex(app) >= 0 )
        app_world_load_begin(app, NULL, 0);
}

/* Classic human animation set (players; INTERFACE_PLAYER_IDLE_SEQ parity). */
enum
{
    APP_PLAYER_SEQ_READY = 808,
    APP_PLAYER_SEQ_WALK = 819,
    APP_PLAYER_SEQ_WALK_B = 820,
    APP_PLAYER_SEQ_WALK_L = 821,
    APP_PLAYER_SEQ_WALK_R = 822,
    APP_PLAYER_SEQ_TURN = 823,
    APP_PLAYER_SEQ_RUN = 824,
};

/* Wrap a freshly built (owned) model in a new dynamic scene element. The
 * element owns the model from here (SceneElementRemove frees it), which is
 * why spawns copy registry models instead of sharing handles. */
static int
app_world_scene_element_create(
    struct App* app,
    struct ToriDraw_Model* model,
    int world_x,
    int world_y,
    int world_z)
{
    struct ToriDraw_ModelHandle hnd;
    int element_id = ToriDraw_SceneElementAddPool(app->scene, TORIDRAW_SCENE_POOL_DYNAMIC);

    if( element_id < 0 )
    {
        ToriDraw_ModelFree(model);
        return -1;
    }
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;
    ToriDraw_SceneElementSetModel(app->scene, element_id, hnd);
    ToriDraw_SceneElementSetPosition(app->scene, element_id, world_x, world_y, world_z, 0);
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        if( el )
        {
            el->dynamic = true;
            /*
             * Entities pick per-face, like locs do.
             *
             * The reference sets Model.useAABBMouseCheck on exactly these
             * (ObjType.getWorldModel:359, ClientPlayer:321/395, NpcType:227),
             * and this followed it — but a screen-space box around a large
             * model is enormously bigger than the model. TzKal-Zuk is the case
             * that made it untenable: his box swallows most of the arena, so
             * clicking the floor near him hits him instead. The box is also
             * built over *every* projected vertex, including geometry that is
             * never drawn, which inflates it further.
             *
             * ToriDraw_ProjectedModelContainsPoint still uses the AABB as its
             * cheap reject before walking faces, so this costs a triangle scan
             * only on models the cursor is actually over.
             */
            el->pick_aabb = false;
        }
    }
    return element_id;
}

/* A registered animation that can actually pose a model: either a classic
 * frame/framemap track or a skeletal (Animaya) matrix palette. Everything else
 * is the empty sentinel a failed load leaves behind. */
static int
app_anim_playable(struct ToriDraw_Animation const* anim)
{
    if( !anim || anim->frame_count <= 0 )
        return 0;
    return (anim->frames && anim->base) || anim->skeletal != NULL;
}

/* Point a scene element at an animation, selecting the pose path. Skeletal
 * sequences carry no bones, so the element has to be flagged for
 * ToriDraw_ModelAnimateSkeletal instead of the frame animator. */
static void
app_element_set_anim(
    struct ToriDraw_SceneElement* el,
    struct ToriDraw_Animation* anim)
{
    el->animation = anim;
    el->is_skeletal = anim && anim->skeletal != NULL;
    el->skeletal_animation = anim ? anim->skeletal : NULL;
    el->skeletal_play_frames = el->is_skeletal ? anim->frame_count : 0;
}

/* Try binding a loaded scene animation onto an element. Returns 1 when bound
 * OR permanently unbindable (failed/empty sentinel), 0 while still loading. */
static int
app_world_try_bind_seq(
    struct App* app,
    int element_id,
    int seq_id)
{
    struct ToriDraw_Animation* anim;

    if( !ToriDraw_SceneAnimationHas(app->scene, seq_id) )
        return 0;
    /* Bind the resolved animation onto the element — the tick loop and
     * frame emitter read element->animation, which SetAnimationSeq alone
     * leaves NULL. Skip the empty sentinel (failed seqs). */
    anim = ToriDraw_SceneAnimationGet(app->scene, seq_id);
    if( app_anim_playable(anim) )
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        ToriDraw_SceneElementSetAnimationSeq(app->scene, element_id, seq_id);
        ToriDraw_SceneElementSetAnimation(app->scene, element_id, anim, true);
        if( el )
            app_element_set_anim(el, anim);
        if( getenv("TORIRS_ANIM_DEBUG") )
            fprintf(
                stderr, "seq_bind: element=%d seq=%d frames=%d skeletal=%d\n", element_id, seq_id,
                anim->frame_count, anim->skeletal ? 1 : 0);
    }
    else if( getenv("TORIRS_ANIM_DEBUG") )
        fprintf(
            stderr, "seq_bind: element=%d seq=%d UNBINDABLE (anim=%p frames=%d)\n", element_id,
            seq_id, (void*)anim, anim ? anim->frame_count : -1);
    return 1;
}

/* Queue a sequence load (no-op when cached) and attach it to the element —
 * immediately when already resident, else via the per-frame bind poll. */
static void
app_world_apply_seq(
    struct App* app,
    int element_id,
    int seq_id)
{
    struct ToriRS_Task* task;

    if( seq_id < 0 )
        return;
    task = CreateTask_SequenceLoad(app->provider, app->scene, seq_id);
    if( task )
        ToriRS_TaskQueue_Add(app->runner.queue, task);

    if( app_world_try_bind_seq(app, element_id, seq_id) )
        return;
    if( app->seq_bind_pending_count < (int)(sizeof(app->seq_bind_pending) /
                                            sizeof(app->seq_bind_pending[0])) )
    {
        app->seq_bind_pending[app->seq_bind_pending_count].element_id = element_id;
        app->seq_bind_pending[app->seq_bind_pending_count].seq_id = seq_id;
        app->seq_bind_pending_count++;
    }
}

/* Per-frame: bind deferred element/sequence pairs whose loads landed. */
static void
app_world_bind_pending_seqs(struct App* app)
{
    int kept = 0;
    for( int i = 0; i < app->seq_bind_pending_count; i++ )
    {
        struct AppSeqBindPending* pend = &app->seq_bind_pending[i];
        if( !ToriDraw_SceneElementIsLive(app->scene, pend->element_id) )
            continue; /* element despawned while loading */
        if( app_world_try_bind_seq(app, pend->element_id, pend->seq_id) )
        {
            app->need_redraw = 1;
            continue;
        }
        app->seq_bind_pending[kept++] = *pend;
    }
    app->seq_bind_pending_count = kept;
}

/* Config-driven color/texture swaps for a built model (npc/loc style). NULL
 * where the caller has none. */
struct AppModelRecolorSpec
{
    const int* recolors_from;
    const int* recolors_to;
    int recolor_count;
    const int* retextures_from;
    const int* retextures_to;
    int retexture_count;
};

/* Convert + merge + recolor + scale + light one drawable model from cache model ids.
 * SYNCHRONOUS: the models must already be resident (callers await
 * CreateTask_ModelLoad first — the spawn tasks do). Returns an owned model
 * or NULL when any part is missing.
 *
 * scale_xz/scale_y are 128 == 1.0 (pass 128,128 for none). Applied before the
 * rest-pose capture, because animation frames reset vertices from the capture —
 * a scale applied after it would vanish on the first animated tick. The
 * reference re-scales the animated copy every frame instead (NpcModelLoader);
 * scaling the base is equivalent for rotation frames and off by the scale
 * factor only on a frame's translate deltas, which nothing visible exercises. */
static struct ToriDraw_Model*
app_world_build_model(
    struct App* app,
    const int* model_ids,
    int count,
    const struct AppModelRecolorSpec* recolors,
    int scale_xz,
    int scale_y)
{
    struct ToriDraw_Model* parts[16];
    struct ToriDraw_Model* model = NULL;
    int part_count = 0;

    for( int i = 0; i < count && part_count < 16; i++ )
    {
        struct ToriRS_Model* rs = CacheProvider_ModelGet(app->provider, model_ids[i]);
        struct ToriDraw_Model* part = rs ? ToriDraw_ModelFromToriRS(rs) : NULL;
        if( part )
            parts[part_count++] = part;
    }
    if( part_count == 0 )
        return NULL;

    if( part_count > 1 )
    {
        model = ToriDraw_ModelMerge(parts, part_count);
        for( int i = 0; i < part_count; i++ )
            ToriDraw_ModelFree(parts[i]);
    }
    else
        model = parts[0];
    if( !model )
        return NULL;

    /* Recolor before lighting: lighting bakes face colors into per-vertex
     * shaded colors, so a swap afterwards would be a no-op (same order as
     * scenery apply_transforms and the obj icon path in the scene bridge). */
    if( recolors )
    {
        for( int i = 0; i < recolors->recolor_count; i++ )
            ToriDraw_ModelRecolor(model, recolors->recolors_from[i], recolors->recolors_to[i]);
        for( int i = 0; i < recolors->retexture_count; i++ )
            ToriDraw_ModelRetexture(model, recolors->retextures_from[i], recolors->retextures_to[i]);
        /* Swapped-in ids are new to the loader's registry — see
         * ToriDraw_ModelNoteTextureWants. */
        if( recolors->retexture_count > 0 )
            ToriDraw_ModelNoteTextureWants(model);
    }

    if( scale_xz != 128 || scale_y != 128 )
        ToriDraw_ModelScale(model, scale_xz, scale_xz, scale_y);

    /* HD-only textures off before lighting — ModelData.light()'s isSd gate.
     * Without this, every face whose material is HD-only keeps a texture id,
     * lighting stores 0–127 lightness (not HSL16), and the software raster
     * skips the face when the texture map has no entry. Steel titan (30469)
     * is entirely HD-textured (materials 238/288/241, all valid=0). */
    ToriDraw_ModelDropNonSdTextures(app->provider, model);
    ToriDraw_ModelNoteTextureWants(model);

    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        ToriDraw_LightModelDefaultPreScaled(hnd, 0, 0);
    }
    ToriDraw_ModelSetBoundsCylinder(model);
    ToriDraw_ModelCaptureOriginalVertices(model);
    return model;
}

/* Build the drawable model for a spotanim (reference SpotType.getTempModel2 +
 * MapSpotAnim.getTempModel static transforms): a single model, recoloured/
 * retextured, resized, angle-rotated and lit with the config ambient/contrast.
 * The seq animation itself is bound onto the element and stepped per-tick by
 * app_world_tick_animations (the projectile path), so only the static
 * transforms are baked here. SYNCHRONOUS — the model must already be resident.
 * Returns an owned model or NULL. */
static struct ToriDraw_Model*
app_world_build_spotanim_model(struct App* app, const struct ToriRS_Spotanimtype* spot)
{
    struct ToriRS_Model* rs = CacheProvider_ModelGet(app->provider, spot->model);
    struct ToriDraw_Model* model = rs ? ToriDraw_ModelFromToriRS(rs) : NULL;
    int retextured = 0;
    if( !model )
        return NULL;

    /* Recolour: reference guards the whole 6-pair loop on recol_s[0] != 0. */
    if( spot->recol_s[0] != 0 )
    {
        for( int i = 0; i < 6; i++ )
            ToriDraw_ModelRecolor(model, spot->recol_s[i], spot->recol_d[i]);
    }
    /* Retexture (dat2 only; dat1 leaves these zero). */
    for( int i = 0; i < 6; i++ )
    {
        if( spot->retex_s[i] != 0 )
        {
            ToriDraw_ModelRetexture(model, spot->retex_s[i], spot->retex_d[i]);
            retextured = 1;
        }
    }
    /* Swapped-in ids are new to the loader's registry — see
     * ToriDraw_ModelNoteTextureWants. */
    if( retextured )
        ToriDraw_ModelNoteTextureWants(model);

    /* Resize: reference model.resize(resizeh, resizev, resizeh) scales x by
     * resizeh, y by resizev, z by resizeh. ToriDraw_ModelScale is (x, z, y). */
    if( spot->resizeh != 128 || spot->resizev != 128 )
        ToriDraw_ModelScale(model, spot->resizeh, spot->resizeh, spot->resizev);

    /* Angle: rotate90 applied angle/90 times (0/90/180/270). */
    if( spot->angle != 0 )
        ToriDraw_ModelOrient(model, spot->angle / 90);

    /* HD-only textures off before lighting — same isSd gate as scenery/NPC. */
    ToriDraw_ModelDropNonSdTextures(app->provider, model);
    ToriDraw_ModelNoteTextureWants(model);

    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        /* Reference lights with 64+ambient / 850+contrast; the engine's house
         * lighting base is 768 (used pre-scaled for every model), so pass the
         * config offsets straight through — the engine-native equivalent. */
        ToriDraw_LightModelDefaultPreScaled(hnd, spot->contrast, spot->ambient);
    }
    ToriDraw_ModelSetBoundsCylinder(model);
    ToriDraw_ModelCaptureOriginalVertices(model);
    return model;
}

/* Hotkey 9 body: default player model on the hovered tile. SYNCHRONOUS —
 * the appearance kit + ready seq must be resident (Task_AppSpawn awaits).
 * Returns the world player-pool index, or -1. */
static int
app_world_spawn_player_now(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    int scene_model_id;
    struct ToriDraw_ModelHandle reg;
    struct ToriDraw_Model* copy;
    int world_x = tile_x * 128 + 64;
    int world_z = tile_z * 128 + 64;
    int world_y;
    int element_id;

    int idx;

    scene_model_id = UITreeSceneBridge_EnsurePlayerModel(&app->bridge);
    if( scene_model_id <= 0 )
    {
        fprintf(stderr, "spawn_player: player model unavailable\n");
        return -1;
    }
    reg = ToriDraw_SceneModelGet(app->scene, scene_model_id);
    if( reg.kind != TORIDRAWMK_MODEL || !reg.u.model.model )
        return -1;
    copy = ToriDraw_ModelCopy(reg.u.model.model);
    if( !copy )
        return -1;
    ToriDraw_ModelSetBoundsCylinder(copy);
    ToriDraw_ModelCaptureOriginalVertices(copy);

    world_y = app_world_height(app, world_x, world_z, level);
    element_id = app_world_scene_element_create(app, copy, world_x, world_y, world_z);
    if( element_id < 0 )
        return -1;
    app_world_apply_seq(app, element_id, APP_PLAYER_SEQ_READY);
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        if( el )
            el->anim_external = true;
        ToriDraw_SceneAnimListInvalidate(app->scene);
    }

    {
        struct WorldEntityFacet_IdleAnimations idle = {
            .readyanim = APP_PLAYER_SEQ_READY,
            .walkanim = APP_PLAYER_SEQ_WALK,
            .turnanim = APP_PLAYER_SEQ_TURN,
            .runanim = APP_PLAYER_SEQ_RUN,
            .walkanim_b = APP_PLAYER_SEQ_WALK_B,
            .walkanim_r = APP_PLAYER_SEQ_WALK_R,
            .walkanim_l = APP_PLAYER_SEQ_WALK_L,
        };
        idx = World_PlayerSpawn(app->world, element_id, level, tile_x, tile_z, idle);
    }
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(&app->world->entities.player, idx);
        if( player )
            player->server_pid = -1;
    }
    fprintf(
        stderr,
        "spawn_player: element=%d tile=%d,%d level=%d\n",
        element_id,
        tile_x,
        tile_z,
        level);
    app_sync_textures(app);
    app->need_redraw = 1;
    return idx;
}

/* Hotkey 8 body: npc on the hovered tile. SYNCHRONOUS — the npc config and
 * its models must be resident (Task_AppSpawn awaits them first).
 * Returns the world npc-pool index, or -1. */
static int
app_world_spawn_npc_now(
    struct App* app,
    int npc_id,
    int tile_x,
    int tile_z,
    int level)
{
    struct ToriRS_Npctype* npctype;
    struct ToriDraw_Model* model;
    int size;
    int world_x, world_z, world_y;
    int element_id;
    int idx;

    npctype = CacheProvider_NpctypeGet(app->provider, npc_id);
    if( !npctype || npctype->models_count <= 0 )
    {
        fprintf(stderr, "spawn_npc: npc %d unavailable\n", npc_id);
        return -1;
    }

    {
        struct AppModelRecolorSpec recolors = {
            .recolors_from = npctype->recolors_from,
            .recolors_to = npctype->recolors_to,
            .recolor_count = npctype->recolor_count,
            .retextures_from = npctype->retextures_from,
            .retextures_to = npctype->retextures_to,
            .retexture_count = npctype->retexture_count,
        };
        model = app_world_build_model(
            app,
            npctype->models,
            npctype->models_count,
            &recolors,
            npctype->width_scale,
            npctype->height_scale);
    }
    if( !model )
    {
        fprintf(stderr, "spawn_npc: npc %d models failed to load\n", npc_id);
        return -1;
    }

    size = npctype->size > 0 ? npctype->size : 1;
    world_x = tile_x * 128 + size * 64;
    world_z = tile_z * 128 + size * 64;
    world_y = app_world_height(app, world_x, world_z, level);
    element_id = app_world_scene_element_create(app, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return -1;
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        if( el )
            el->anim_external = true;
        ToriDraw_SceneAnimListInvalidate(app->scene);
    }

    {
        /* Config movement anims (dat1 has no turn/run for npcs; the reference
         * walkanim_l/r swap applies here at spawn like at CHANGE_TYPE). */
        struct WorldEntityFacet_IdleAnimations idle = {
            .readyanim = npctype->readyanim,
            .walkanim = npctype->walkanim,
            .turnanim = -1,
            .runanim = -1,
            .walkanim_b = npctype->walkanim_b,
            .walkanim_r = npctype->walkanim_l,
            .walkanim_l = npctype->walkanim_r,
        };
        idx = World_NpcSpawn(app->world, element_id, npc_id, level, tile_x, tile_z, size, idle);
    }
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, idx);
        if( npc )
            npc->server_slot = -1;
    }
    app_world_apply_seq(app, element_id, npctype->readyanim);
    /* Spawn does not carry menu data; the minimenu rows read it off the
     * entity, so copy name/actions/level from the config here. */
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, idx);
        if( npc )
        {
            npc->combat_level = npctype->combat_level;
            npc->alwaysontop = npctype->alwaysontop;
            npc->facing.turn_speed = npctype->turn_speed;
            snprintf(npc->name, sizeof(npc->name), "%s", npctype->name);
            for( int i = 0; i < 5; i++ )
                snprintf(
                    npc->actions[i].name, sizeof(npc->actions[i].name), "%s", npctype->actions[i]);
        }
    }
    fprintf(
        stderr,
        "spawn_npc: npc=%d element=%d tile=%d,%d level=%d size=%d recolors=%d retextures=%d\n",
        npc_id,
        element_id,
        tile_x,
        tile_z,
        level,
        size,
        npctype->recolor_count,
        npctype->retexture_count);
    app_sync_textures(app);
    app->need_redraw = 1;
    return idx;
}

/* Hotkey 0 fire body: launch source -> destination. SYNCHRONOUS — the
 * projectile model must be resident (Task_AppSpawn awaits it). Arc math
 * lives in World_ProjectileSetTarget/Move (TS reference parity). `target` is
 * the wire target-entity id when a synced NPC sits on the destination tile,
 * WORLD_PROJECTILE_TARGET_NONE for a plain tile shot. */
static void
app_world_spawn_projectile_now(
    struct App* app,
    int model_id,
    int seq_id,
    int src_tile_x,
    int src_tile_z,
    int src_level,
    int tile_x,
    int tile_z,
    int target)
{
    int model_ids[1];
    struct ToriDraw_Model* model;
    int src_x, src_z, dst_x, dst_z, src_y;
    int range, t2;
    int element_id;

    model_ids[0] = model_id;
    model = app_world_build_model(app, model_ids, 1, NULL, 128, 128);
    if( !model )
    {
        fprintf(stderr, "spawn_projectile: model %d failed to load\n", model_id);
        return;
    }

    src_x = src_tile_x * 128 + 64;
    src_z = src_tile_z * 128 + 64;
    dst_x = tile_x * 128 + 64;
    dst_z = tile_z * 128 + 64;
    /* World y is negative-up: start slightly above the source ground. */
    src_y = app_world_height(app, src_x, src_z, src_level) - 160;

    range = abs(tile_x - src_tile_x);
    if( abs(tile_z - src_tile_z) > range )
        range = abs(tile_z - src_tile_z);
    t2 = 60 + range * 5; /* ticks: base flight + per-tile stretch */

    element_id = app_world_scene_element_create(app, model, src_x, src_y, src_z);
    if( element_id < 0 )
        return;

    World_ProjectileSpawn(
        app->world,
        element_id,
        src_level,
        src_x,
        src_z,
        dst_x,
        dst_z,
        src_y,
        144, /* end height above target ground (36 * 4) */
        0,
        t2,
        15, /* launch slope (1/2048 circle units) */
        64,
        target);
    /* Bind the spotanim's sequence so the projectile model animates in flight
     * (v1 Task_*ProjectileAdd loads the seq, then ElementSetSequenceId). The
     * element is left non-external, so app_world_tick_animations advances the
     * frame each tick — matching ClientProj.move's plain frame loop. */
    app_world_apply_seq(app, element_id, seq_id);
    fprintf(
        stderr,
        "spawn_projectile: element=%d %d,%d -> %d,%d t2=%d target=%d\n",
        element_id,
        src_tile_x,
        src_tile_z,
        tile_x,
        tile_z,
        t2,
        target);
    app_sync_textures(app);
    app->need_redraw = 1;
}

/* Server-driven projectile (reference ClientProj / MAP_PROJANIM). Builds the
 * transformed spotanim model (recolour/resize/angle/lighting), spawns the world
 * projectile with the wire trajectory params, and binds the spotanim seq so the
 * model animates in flight. SYNCHRONOUS — the spotanimtype, its model and its
 * seq must already be resident (Task_AppSpawn awaits them). src_height/dst_height
 * are raw wire bytes (×4, matching Client.ts h1/h2). */
static void
app_world_spawn_projectile_spot_now(
    struct App* app,
    int spotanim_id,
    int src_tile_x,
    int src_tile_z,
    int src_level,
    int dst_tile_x,
    int dst_tile_z,
    int dst_level,
    int src_height,
    int dst_height,
    int start_delay,
    int end_delay,
    int peak,
    int arc,
    int target)
{
    struct ToriRS_Spotanimtype* spot;
    struct ToriDraw_Model* model;
    int src_x, src_z, dst_x, dst_z, src_y;
    int element_id;

    spot = CacheProvider_SpotanimtypeGet(app->provider, spotanim_id);
    if( !spot )
    {
        fprintf(stderr, "spawn_projectile_spot: spotanim %d not resident\n", spotanim_id);
        return;
    }

    model = app_world_build_spotanim_model(app, spot);
    if( !model )
    {
        fprintf(
            stderr,
            "spawn_projectile_spot: spotanim %d model %d failed\n",
            spotanim_id,
            spot->model);
        return;
    }

    src_x = src_tile_x * 128 + 64;
    src_z = src_tile_z * 128 + 64;
    dst_x = dst_tile_x * 128 + 64;
    dst_z = dst_tile_z * 128 + 64;
    /* World y is negative-up: reference y = getAvH(src) - h1, h1 = src_height*4. */
    src_y = app_world_height(app, src_x, src_z, src_level) - src_height * 4;

    element_id = app_world_scene_element_create(app, model, src_x, src_y, src_z);
    if( element_id < 0 )
        return;

    /* peak -> angle, arc -> startpos, end_height = dst_height*4 (World computes
     * dst y as height_fn(dst) - end_height, matching getAvH(dst) - h2). `target`
     * goes through in its wire encoding so World re-aims the arc at that
     * entity's live position every cycle (reference addProjectiles). */
    World_ProjectileSpawn(
        app->world,
        element_id,
        dst_level,
        src_x,
        src_z,
        dst_x,
        dst_z,
        src_y,
        dst_height * 4,
        start_delay,
        end_delay,
        peak,
        arc,
        target);
    app_world_apply_seq(app, element_id, spot->seq);

    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "spawn_projectile_spot: element=%d spotanim=%d model=%d seq=%d "
            "%d,%d -> %d,%d t1=%d t2=%d target=%d\n",
            element_id,
            spotanim_id,
            spot->model,
            spot->seq,
            src_tile_x,
            src_tile_z,
            dst_tile_x,
            dst_tile_z,
            start_delay,
            end_delay,
            target);
    app_sync_textures(app);
    app->need_redraw = 1;
}

/* Total client cycles for one loop of a seq — the sum of (frame delay + 1) per
 * frame, matching MapSpotAnim.update which subtracts getDuration(frame)+1 each
 * advance. Drives the free-standing spotanim's single-shot lifetime. */
static int
app_seq_total_duration(struct App* app, int seq_id)
{
    int frames = app_seq_frame_count(app, seq_id);
    int total = 0;
    if( frames <= 0 )
        return 1;
    for( int f = 0; f < frames; f++ )
        total += app_seq_frame_duration(app, seq_id, f) + 1;
    return total > 0 ? total : 1;
}

/* Free-standing spotanim (reference MapSpotAnim / MAP_ANIM). SYNCHRONOUS — the
 * spotanimtype, its model and its seq must already be resident (Task_AppSpawn
 * awaits them). Builds the transformed model, spawns the world entity with a
 * single-shot lifetime equal to one seq loop, and binds the seq so the element
 * animates per-tick. */
static void
app_world_spawn_spotanim_now(
    struct App* app,
    int spotanim_id,
    int tile_x,
    int tile_z,
    int level,
    int height,
    int delay)
{
    struct ToriRS_Spotanimtype* spot;
    struct ToriDraw_Model* model;
    int world_x, world_z, world_y;
    int element_id;
    int lifetime;

    spot = CacheProvider_SpotanimtypeGet(app->provider, spotanim_id);
    if( !spot )
    {
        fprintf(stderr, "spawn_spotanim: spotanim %d not resident\n", spotanim_id);
        return;
    }

    model = app_world_build_spotanim_model(app, spot);
    if( !model )
    {
        fprintf(stderr, "spawn_spotanim: spotanim %d model %d failed\n", spotanim_id, spot->model);
        return;
    }

    world_x = tile_x * 128 + 64;
    world_z = tile_z * 128 + 64;
    /* Reference y = getAvH(x,z) - height; world y is negative-up so subtracting
     * raises the effect above the ground by `height`. */
    world_y = app_world_height(app, world_x, world_z, level) - height;

    element_id = app_world_scene_element_create(app, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return;

    lifetime = app_seq_total_duration(app, spot->seq);

    World_SpotanimSpawn(
        app->world, element_id, level, world_x, world_z, world_y, 0, delay, lifetime);
    app_world_apply_seq(app, element_id, spot->seq);

    fprintf(
        stderr,
        "spawn_spotanim: id=%d element=%d tile=%d,%d level=%d model=%d seq=%d "
        "life=%d delay=%d\n",
        spotanim_id,
        element_id,
        tile_x,
        tile_z,
        level,
        spot->model,
        spot->seq,
        lifetime,
        delay);
    app_sync_textures(app);
    app->need_redraw = 1;
}

/* Async spawn driver for the three debug hotkeys: awaits the cache loads the
 * synchronous spawn bodies assume, then runs them. Enqueued on the serial
 * exec pipeline so spawns interleave cleanly with packet exec + mounts. */
enum AppSpawnKind
{
    APP_SPAWN_PLAYER = 0,
    APP_SPAWN_NPC,
    APP_SPAWN_PROJECTILE,
    APP_SPAWN_PROJECTILE_SPOT,
    APP_SPAWN_OBJ,
    APP_SPAWN_SPOTANIM,
    APP_SPAWN_ENTITY_SPOTANIM,
    APP_SPAWN_LOC_CHANGE,
};

struct Task_AppSpawn
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    enum AppSpawnKind kind;
    int tile_x;
    int tile_z;
    int level;
    int npc_id;
    int obj_id;
    int model_id;
    int seq_id;
    int src_tile_x;
    int src_tile_z;
    int src_level;
    int model_i;
    int spotanim_id;
    int spotanim_height;
    int spotanim_delay;
    /* APP_SPAWN_ENTITY_SPOTANIM: the body scene element of the player/npc the
     * attached graphic belongs to (stable, scene-unique key to re-find the live
     * entity when the async load lands). */
    int entity_element_id;
    /* MAP_PROJANIM (spotanim-based projectile) trajectory params. Source and
     * destination tiles reuse src_tile_x/z and tile_x/z; src_level and level
     * carry the source and destination levels. */
    int proj_src_height;
    int proj_dst_height;
    int proj_start_delay;
    int proj_end_delay;
    int proj_peak;
    int proj_arc;
    int proj_target;
    /* APP_SPAWN_LOC_CHANGE (zone LOC_ADD_CHANGE / LOC_DEL): the replacement loc
     * (-1 = pure delete), its map shape/angle, and the nested model-list cursor
     * (loc models are [shape_entry][model]; both indices must survive awaits). */
    int loc_id;
    int loc_shape;
    int loc_angle;
    int loc_model_j;
};

static int
Task_AppSpawn_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_AppSpawn* self = (struct Task_AppSpawn*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    if( self->kind == APP_SPAWN_PLAYER )
    {
        TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider));
        TASK_AWAITSELF_IF(CreateTask_SequenceLoad(app->provider, app->scene, APP_PLAYER_SEQ_READY));
        app_world_spawn_player_now(app, self->tile_x, self->tile_z, self->level);
    }
    else if( self->kind == APP_SPAWN_NPC )
    {
        TASK_AWAITSELF_IF(CreateTask_NpcLoad(app->provider, self->npc_id));
        for( self->model_i = 0;; self->model_i++ )
        {
            {
                struct ToriRS_Npctype* npctype =
                    CacheProvider_NpctypeGet(app->provider, self->npc_id);
                if( !npctype || self->model_i >= npctype->models_count )
                    break;
            }
            TASK_AWAITSELF_IF(CreateTask_ModelLoad(
                app->provider,
                CacheProvider_NpctypeGet(app->provider, self->npc_id)->models[self->model_i]));
        }
        /* Idle/walk(/turn) seqs — same set Task_ExecNpcInfo awaits.
         * Re-fetch npctype each iteration: locals do not survive PT_YIELD. */
        for( self->model_i = 0; self->model_i < 5; self->model_i++ )
        {
            int seq_id = -1;
            {
                struct ToriRS_Npctype* npctype =
                    CacheProvider_NpctypeGet(app->provider, self->npc_id);
                if( npctype )
                {
                    int seqs[5] = {
                        npctype->readyanim,
                        npctype->walkanim,
                        npctype->walkanim_b,
                        npctype->walkanim_r,
                        npctype->walkanim_l,
                    };
                    seq_id = seqs[self->model_i];
                }
            }
            if( seq_id >= 0 )
                TASK_AWAITSELF_IF(
                    CreateTask_SequenceLoad(app->provider, app->scene, seq_id));
        }
        app_world_spawn_npc_now(app, self->npc_id, self->tile_x, self->tile_z, self->level);
    }
    else if( self->kind == APP_SPAWN_OBJ )
    {
        TASK_AWAITSELF_IF(CreateTask_ObjLoad(app->provider, self->obj_id));
        {
            struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, self->obj_id);
            if( obj && obj->inventory_model_id > 0 )
                self->model_id = obj->inventory_model_id;
        }
        if( self->model_id > 0 )
        {
            TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
            App_WorldObjStackAdd(app, self->tile_x, self->tile_z, self->level, self->obj_id, 1);
        }
        else
            fprintf(stderr, "spawn_obj: obj %d has no inventory model\n", self->obj_id);
    }
    else if( self->kind == APP_SPAWN_SPOTANIM )
    {
        TASK_AWAITSELF_IF(CreateTask_SpotanimLoad(app->provider, self->spotanim_id));
        {
            struct ToriRS_Spotanimtype* spot =
                CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
            self->model_id = (spot && spot->model > 0) ? spot->model : -1;
        }
        if( self->model_id > 0 )
            TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
        {
            struct ToriRS_Spotanimtype* spot =
                CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
            self->seq_id = spot ? spot->seq : -1;
        }
        if( self->seq_id >= 0 )
            TASK_AWAITSELF_IF(
                CreateTask_SequenceLoad(app->provider, app->scene, self->seq_id));
        app_world_spawn_spotanim_now(
            app,
            self->spotanim_id,
            self->tile_x,
            self->tile_z,
            self->level,
            self->spotanim_height,
            self->spotanim_delay);
    }
    else if( self->kind == APP_SPAWN_ENTITY_SPOTANIM )
    {
        /* Attached graphic (SPOTANIM mask): load the same asset chain as the
         * free-standing spotanim. No completion callback — once resident,
         * app_world_sync_entity_spotanims combines synchronously next frame. */
        TASK_AWAITSELF_IF(CreateTask_SpotanimLoad(app->provider, self->spotanim_id));
        {
            struct ToriRS_Spotanimtype* spot =
                CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
            self->model_id = (spot && spot->model > 0) ? spot->model : -1;
        }
        if( self->model_id > 0 )
            TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
        {
            struct ToriRS_Spotanimtype* spot =
                CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
            self->seq_id = spot ? spot->seq : -1;
        }
        if( self->seq_id >= 0 )
            TASK_AWAITSELF_IF(
                CreateTask_SequenceLoad(app->provider, app->scene, self->seq_id));
        app->need_redraw = 1;
    }
    else if( self->kind == APP_SPAWN_PROJECTILE_SPOT )
    {
        TASK_AWAITSELF_IF(CreateTask_SpotanimLoad(app->provider, self->spotanim_id));
        {
            struct ToriRS_Spotanimtype* spot =
                CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
            self->model_id = (spot && spot->model > 0) ? spot->model : -1;
        }
        if( self->model_id > 0 )
            TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
        {
            struct ToriRS_Spotanimtype* spot =
                CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
            self->seq_id = spot ? spot->seq : -1;
        }
        if( self->seq_id >= 0 )
            TASK_AWAITSELF_IF(
                CreateTask_SequenceLoad(app->provider, app->scene, self->seq_id));
        app_world_spawn_projectile_spot_now(
            app,
            self->spotanim_id,
            self->src_tile_x,
            self->src_tile_z,
            self->src_level,
            self->tile_x,
            self->tile_z,
            self->level,
            self->proj_src_height,
            self->proj_dst_height,
            self->proj_start_delay,
            self->proj_end_delay,
            self->proj_peak,
            self->proj_arc,
            self->proj_target);
    }
    else if( self->kind == APP_SPAWN_LOC_CHANGE )
    {
        /* Reference locChangeDoQueue (Client.ts:7701): a zone loc change only
         * applies once changeLocAvailable — the loc config and every model it
         * references are resident (an open-door variant is usually absent from
         * the static map build's preload). LOC_DEL (loc_id < 0) has nothing to
         * load but still runs through this task so same-tile changes apply in
         * packet order on the serial exec FIFO. */
        if( self->loc_id >= 0 )
        {
            TASK_AWAITSELF_IF(CreateTask_LocLoad(app->provider, self->loc_id));
            for( self->model_i = 0;; self->model_i++ )
            {
                {
                    struct ToriRS_Location* cfg =
                        CacheProvider_LocationGet(app->provider, self->loc_id);
                    int entries = 0;
                    if( cfg && cfg->models && cfg->lengths )
                        entries = cfg->shapes ? cfg->shapes_and_model_count : 1;
                    if( self->model_i >= entries )
                        break;
                }
                for( self->loc_model_j = 0;; self->loc_model_j++ )
                {
                    {
                        struct ToriRS_Location* cfg =
                            CacheProvider_LocationGet(app->provider, self->loc_id);
                        if( !cfg || self->loc_model_j >= cfg->lengths[self->model_i] )
                            break;
                        self->model_id = cfg->models[self->model_i][self->loc_model_j];
                    }
                    if( self->model_id >= 0 )
                        TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
                }
            }
            {
                struct ToriRS_Location* cfg =
                    CacheProvider_LocationGet(app->provider, self->loc_id);
                self->seq_id = cfg ? cfg->seq_id : -1;
            }
            if( self->seq_id >= 0 )
                TASK_AWAITSELF_IF(
                    CreateTask_SequenceLoad(app->provider, app->scene, self->seq_id));
        }
        if( app->world_builder && app->world && app->world->load_complete )
        {
            WorldBuilder_ApplyLocChange(
                app->world_builder,
                self->tile_x,
                self->tile_z,
                self->level,
                self->loc_id,
                self->loc_shape,
                self->loc_angle);
            app_sync_textures(app);
            app->need_redraw = 1;
        }
    }
    else
    {
        TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
        app_world_spawn_projectile_now(
            app,
            self->model_id,
            self->seq_id,
            self->src_tile_x,
            self->src_tile_z,
            self->src_level,
            self->tile_x,
            self->tile_z,
            self->proj_target);
    }

    PT_END(&self->pt);
}

static void
Task_AppSpawn_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_AppSpawn_VTable = {
    .run = Task_AppSpawn_Run,
    .free = Task_AppSpawn_Free,
};

/* Interface chathead: load the npctype/appearance + head models, composite the
 * head into the scene (UITreeSceneBridge_Ensure*Head), and bind it onto the
 * MODEL widget the dialogue set (reference IfType.getModel type 2/3, resolved
 * lazily — here via the async provider). */
enum AppIfHeadKind
{
    APP_IFHEAD_NPC = 0,
    APP_IFHEAD_PLAYER,
    /* IF_SETOBJECT (reference IfType model1Type 4): the obj's lit inventory
     * model bound to a MODEL widget — e.g. the combat-tab weapon. npc_id
     * carries the obj id; zoom carries the wire zoom. */
    APP_IFHEAD_OBJ,
};

#define APP_IFHEAD_MAX_HEADS 24

struct Task_AppIfHead
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    enum AppIfHeadKind kind;
    int component_id;
    int npc_id;
    int model_i;
    int slot_i;
    int head_ids[APP_IFHEAD_MAX_HEADS]; /* player: idk + worn-obj head model ids to load */
    int head_count;
};

static int
Task_AppIfHead_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_AppIfHead* self = (struct Task_AppIfHead*)base;
    struct App* app = self->app;
    int scene_id = -1;

    PT_BEGIN(&self->pt);

    if( self->kind == APP_IFHEAD_NPC )
    {
        TASK_AWAITSELF_IF(CreateTask_NpcLoad(app->provider, self->npc_id));
        /* Load each head model (re-derived from persistent model_i; -1 slots
         * are skipped — reference NpcType.getHead ignores them). */
        for( self->model_i = 0;; self->model_i++ )
        {
            struct ToriRS_Npctype* npc = CacheProvider_NpctypeGet(app->provider, self->npc_id);
            if( !npc || self->model_i >= npc->heads_count )
                break;
            if( npc->heads[self->model_i] < 0 )
                continue;
            TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, npc->heads[self->model_i]));
        }
        scene_id = UITreeSceneBridge_EnsureNpcHead(&app->bridge, self->npc_id);
    }
    else if( self->kind == APP_IFHEAD_OBJ )
    {
        /* IF_SETOBJECT: objtype + its inventory model, then the lit interface
         * model (npc_id carries the obj id). */
        TASK_AWAITSELF_IF(CreateTask_ObjLoad(app->provider, self->npc_id));
        {
            struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, self->npc_id);
            if( obj && obj->inventory_model_id > 0 )
                TASK_AWAITSELF_IF(
                    CreateTask_ModelLoad(app->provider, obj->inventory_model_id));
        }
        scene_id = UITreeSceneBridge_EnsureObjModel(&app->bridge, self->npc_id);
    }
    else
    {
        /* Load the local player's real-appearance idk configs + head models,
         * then composite (reference ClientPlayer.getHeadModel). The idk configs
         * are usually already resident from the world body build; await the
         * appearance load first as a baseline. */
        TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider));
        /* Ensure worn-equipment obj configs are resident so their head-model
         * ids (manhead/womanhead) can be gathered below — the body build loads
         * the wear models but never the head models (reference getHeadModel
         * pulls ObjType.getHeadModelNoCheck for slots >= 512). */
        for( self->slot_i = 0; self->slot_i < 12; self->slot_i++ )
        {
            struct WorldEntity_Player* lp = app_local_player(app);
            if( lp && lp->appearance.slots[self->slot_i] >= 0x200 )
                TASK_AWAITSELF_IF(CreateTask_ObjLoad(
                    app->provider, lp->appearance.slots[self->slot_i] - 0x200));
        }
        {
            struct WorldEntity_Player* lp = app_local_player(app);
            self->head_count = lp ? PlayerHeadModel_CollectHeadModelIds(
                                        app->provider, lp->appearance.slots, lp->gender,
                                        self->head_ids, APP_IFHEAD_MAX_HEADS)
                                  : 0;
        }
        for( self->model_i = 0; self->model_i < self->head_count; self->model_i++ )
            TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->head_ids[self->model_i]));
        {
            struct WorldEntity_Player* lp = app_local_player(app);
            if( lp )
                scene_id = UITreeSceneBridge_EnsurePlayerHead(
                    &app->bridge, lp->appearance.slots, lp->appearance.colors, lp->gender);
        }
    }

    /* Compose only — app_if_head_poll binds the scene model onto the MODEL node
     * once it is mounted (the head packet usually precedes the interface). Force
     * a redraw so the poll runs now that the head is composited. */
    if( scene_id >= 0 )
        app->need_redraw = 1;
    else if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "if-head: component=0x%x kind=%d could not composite head (npc=%d)\n",
            (unsigned)self->component_id,
            (int)self->kind,
            self->npc_id);

    PT_END(&self->pt);
}

static void
Task_AppIfHead_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_AppIfHead_VTable = {
    .run = Task_AppIfHead_Run,
    .free = Task_AppIfHead_Free,
};

static void
app_if_head_enqueue(
    struct App* app,
    enum AppIfHeadKind kind,
    int component_id,
    int npc_id)
{
    struct Task_AppIfHead* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppIfHead_VTable;
    strncpy(task->task.name, "AppIfHead", sizeof(task->task.name) - 1);
    task->app = app;
    task->kind = kind;
    task->component_id = component_id;
    task->npc_id = npc_id;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Persist the head request keyed by component id (reference IfType.list keeps
 * model1Type/model1Id): re-applied by app_if_head_poll whenever the interface
 * (re)mounts, so it survives a head packet that lands before its chat interface
 * exists. Resets applied_gen so the next poll rebinds. */
static void
app_if_head_store(
    struct App* app,
    enum AppIfHeadKind kind,
    int com_id,
    int npc_id)
{
    int i;
    for( i = 0; i < app->if_head_count; i++ )
        if( app->if_heads[i].com_id == com_id )
            break;
    if( i == app->if_head_count )
    {
        if( app->if_head_count == app->if_head_cap )
        {
            int cap = app->if_head_cap ? app->if_head_cap * 2 : 16;
            app->if_heads = realloc(app->if_heads, (size_t)cap * sizeof(*app->if_heads));
            assert(app->if_heads);
            app->if_head_cap = cap;
        }
        app->if_heads[i].anim_id = -1; /* preserved across a head update (below) */
        app->if_head_count++;
    }
    app->if_heads[i].com_id = com_id;
    app->if_heads[i].kind = (int)kind;
    app->if_heads[i].npc_id = npc_id;
    app->if_heads[i].zoom = 0;
    app->if_heads[i].applied_gen = 0;
    app->need_redraw = 1;
}

void
App_SetInterfaceNpcHead(
    struct App* app,
    int component_id,
    int npc_id)
{
    assert(app);
    if( npc_id < 0 )
        return;
    app_if_head_store(app, APP_IFHEAD_NPC, component_id, npc_id);
    app_if_head_enqueue(app, APP_IFHEAD_NPC, component_id, npc_id);
}

void
App_SetInterfacePlayerHead(
    struct App* app,
    int component_id)
{
    assert(app);
    app_if_head_store(app, APP_IFHEAD_PLAYER, component_id, -1);
    app_if_head_enqueue(app, APP_IFHEAD_PLAYER, component_id, -1);
}

void
App_SetInterfaceObjModel(
    struct App* app,
    int component_id,
    int obj_id,
    int zoom)
{
    assert(app);
    if( obj_id <= 0 )
        return;
    app_if_head_store(app, APP_IFHEAD_OBJ, component_id, obj_id);
    /* store resets zoom to 0; stamp the wire zoom for the poll's angle apply. */
    for( int i = 0; i < app->if_head_count; i++ )
        if( app->if_heads[i].com_id == component_id )
        {
            app->if_heads[i].zoom = zoom;
            break;
        }
    app_if_head_enqueue(app, APP_IFHEAD_OBJ, component_id, obj_id);
}

/* Bind any composited heads onto their MODEL nodes. Runs each redraw: an entry
 * applies once its scene model is ready (the load task has composited it) AND
 * its component is mounted, then only re-applies when the tree generation
 * changes (remount/rebuild) — mirroring the reference resolving getModel every
 * draw. Cheap: Ensure* is a cache hit after the first composite, and applied
 * entries at the current generation are skipped. */
static void
app_if_head_poll(struct App* app)
{
    if( app->if_head_count == 0 || !app->tree )
        return;

    for( int i = 0; i < app->if_head_count; i++ )
    {
        struct AppIfHead* head = &app->if_heads[i];
        int scene_id;

        if( head->applied_gen == app->tree->generation )
            continue;

        if( head->kind == APP_IFHEAD_PLAYER )
        {
            struct WorldEntity_Player* lp = app_local_player(app);
            scene_id = lp ? UITreeSceneBridge_EnsurePlayerHead(
                                &app->bridge, lp->appearance.slots, lp->appearance.colors, lp->gender)
                          : -1;
        }
        else if( head->kind == APP_IFHEAD_OBJ )
        {
            scene_id = UITreeSceneBridge_EnsureObjModel(&app->bridge, head->npc_id);
        }
        else
        {
            scene_id = UITreeSceneBridge_EnsureNpcHead(&app->bridge, head->npc_id);
        }
        if( scene_id < 0 )
            continue; /* assets not composited yet — retry next frame */

        if( UITree_ApplyModel(app->tree, head->com_id, scene_id) )
        {
            /* Reference IF_SETOBJECT: modelXAn/YAn from the objtype, modelZoom
             * = zoom2d * 100 / wire zoom (Client.ts:6342). */
            if( head->kind == APP_IFHEAD_OBJ )
            {
                struct ToriRS_Objtype* obj =
                    CacheProvider_ObjtypeGet(app->provider, head->npc_id);
                if( obj && head->zoom > 0 )
                    UITree_ApplyModelAngle(
                        app->tree,
                        head->com_id,
                        obj->xan2d,
                        obj->yan2d,
                        (obj->zoom2d > 0 ? obj->zoom2d : 2000) * 100 / head->zoom);
            }
            if( head->anim_id >= 0 )
                UITree_ApplyModelAnim(app->tree, head->com_id, head->anim_id);
            head->applied_gen = app->tree->generation;
        }
        else if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "if-head: reapply com=%d npc=%d gen=%u missed (node not mounted?)\n",
                head->com_id,
                head->npc_id,
                app->tree->generation);
    }
}

void
App_SetInterfaceModelAnim(
    struct App* app,
    int component_id,
    int anim_id)
{
    assert(app);
    /* Persist onto a matching head entry so it re-applies with the head after a
     * (re)mount (reference modelAnim lives on the same IfType as the head), and
     * apply immediately for the already-mounted / plain-model-widget case. */
    for( int i = 0; i < app->if_head_count; i++ )
    {
        if( app->if_heads[i].com_id == component_id )
        {
            app->if_heads[i].anim_id = anim_id;
            app->if_heads[i].applied_gen = 0;
            app->need_redraw = 1;
            break;
        }
    }
    UITree_ApplyModelAnim(app->tree, component_id, anim_id);
}

static struct Task_AppSpawn*
app_spawn_task_new(
    struct App* app,
    enum AppSpawnKind kind,
    int tile_x,
    int tile_z,
    int level)
{
    struct Task_AppSpawn* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppSpawn_VTable;
    strncpy(task->task.name, "AppSpawn", sizeof(task->task.name) - 1);
    task->app = app;
    task->kind = kind;
    task->tile_x = tile_x;
    task->tile_z = tile_z;
    task->level = level;
    PT_INIT(&task->pt);
    return task;
}

static void
app_world_spawn_player(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    struct Task_AppSpawn* task = app_spawn_task_new(app, APP_SPAWN_PLAYER, tile_x, tile_z, level);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Spawn-hotkey id precedence: env TORIRS_SPAWN_* > manifest [spawn:hotkeys] > built-in. */
static int
app_spawn_id(int builtin, int cfg_value, char const* env_name)
{
    char const* env;
    int value = cfg_value >= 0 ? cfg_value : builtin;
    assert(env_name);
    env = getenv(env_name);
    if( env )
        value = (int)strtol(env, NULL, 0);
    return value;
}

static void
app_world_spawn_npc(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    struct Task_AppSpawn* task = app_spawn_task_new(app, APP_SPAWN_NPC, tile_x, tile_z, level);
    task->npc_id = app_spawn_id(3106 /* OSRS-era "Man" */, app->cfg.spawn_npc_id, "TORIRS_SPAWN_NPC");
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Hotkey 7: ground item on the hovered tile — the same App_WorldObjStackAdd
 * the zone OBJ_ADD packet drives, so the right-click rows it produces are the
 * live path. TORIRS_SPAWN_OBJ overrides the id. */
static void
app_world_spawn_obj(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    struct Task_AppSpawn* task = app_spawn_task_new(app, APP_SPAWN_OBJ, tile_x, tile_z, level);
    task->obj_id =
        app_spawn_id(1265 /* bronze pickaxe: named, with ground ops */, app->cfg.spawn_obj_id, "TORIRS_SPAWN_OBJ");
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Free-standing spotanim spawn (reference MapSpotAnim / MAP_ANIM zone packet):
 * enqueue an async spawn that awaits the spotanimtype + its model + seq, then
 * builds the world entity. Public so the MAP_ANIM executor can drive it. */
void
App_WorldSpotanimSpawn(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int spotanim_id,
    int height,
    int delay)
{
    struct Task_AppSpawn* task;
    assert(app);
    task = app_spawn_task_new(app, APP_SPAWN_SPOTANIM, scene_x, scene_z, level);
    task->spotanim_id = spotanim_id;
    task->spotanim_height = height;
    task->spotanim_delay = delay;
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Server-driven projectile (reference ClientProj / MAP_PROJANIM). Public so the
 * zone-packet executor can drive it. Enqueues the spotanim + model + seq load,
 * then spawns the world projectile with the wire trajectory params. */
void
App_WorldProjectileSpawn(
    struct App* app,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    int level,
    int spotanim_id,
    int src_height,
    int dst_height,
    int start_delay,
    int end_delay,
    int peak,
    int arc,
    int target)
{
    struct Task_AppSpawn* task;
    assert(app);
    /* Destination tile and level go through the task's tile_x, tile_z and level
     * (seeded by app_spawn_task_new); source tile and level go through
     * src_tile_x, src_tile_z and src_level. */
    task = app_spawn_task_new(app, APP_SPAWN_PROJECTILE_SPOT, dst_x, dst_z, level);
    task->spotanim_id = spotanim_id;
    task->src_tile_x = src_x;
    task->src_tile_z = src_z;
    task->src_level = level;
    task->proj_src_height = src_height;
    task->proj_dst_height = dst_height;
    task->proj_start_delay = start_delay;
    task->proj_end_delay = end_delay;
    task->proj_peak = peak;
    task->proj_arc = arc;
    task->proj_target = target;
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Zone LOC_ADD_CHANGE / LOC_DEL (reference locChangeCreate + locChangeDoQueue):
 * enqueue an async change that awaits the loc config + its models (+ seq), then
 * applies it via WorldBuilder_ApplyLocChange. loc_id < 0 = delete. Public so the
 * zone-packet executor can drive it. */
void
App_WorldLocChange(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle)
{
    struct Task_AppSpawn* task;
    assert(app);
    task = app_spawn_task_new(app, APP_SPAWN_LOC_CHANGE, scene_x, scene_z, level);
    task->loc_id = loc_id;
    task->loc_shape = shape;
    task->loc_angle = angle;
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Hotkey 5: spawn a free-standing spotanim on the hovered tile.
 * TORIRS_SPAWN_SPOTANIM / _HEIGHT / _DELAY override the defaults. */
static void
app_world_spawn_spotanim(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    int spotanim_id =
        app_spawn_id(74 /* a small, visible default effect */, app->cfg.spawn_spotanim_id, "TORIRS_SPAWN_SPOTANIM");
    int height = app_spawn_id(92, app->cfg.spawn_spotanim_height, "TORIRS_SPAWN_SPOTANIM_HEIGHT");
    int delay = app_spawn_id(0, app->cfg.spawn_spotanim_delay, "TORIRS_SPAWN_SPOTANIM_DELAY");
    App_WorldSpotanimSpawn(app, tile_x, tile_z, level, spotanim_id, height, delay);
}

/* Wire target-entity id (npc slot + 1) for a *synced* npc standing on a tile,
 * WORLD_PROJECTILE_TARGET_NONE when there is none. Only server-synced npcs can
 * be named: the wire encoding is the server's slot space, and offline spawns
 * deliberately sit outside it with server_slot -1. */
static int
app_world_npc_target_at_tile(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    struct World_EntityPool* pool;

    if( !app->world )
        return WORLD_PROJECTILE_TARGET_NONE;

    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        if( !npc || npc->server_slot < 0 )
            continue;
        if( npc->grid_position.x == tile_x && npc->grid_position.z == tile_z &&
            npc->grid_position.level == level )
            return npc->server_slot + 1;
    }
    return WORLD_PROJECTILE_TARGET_NONE;
}

/* Hotkey 0, two-press latch: first press marks the hovered tile as source,
 * second launches source -> hovered (same-tile press clears the latch). Firing
 * onto a synced npc targets *that entity*, so the arc follows it as it walks —
 * the tracking the MAP_PROJANIM target id drives against a live server. */
static void
app_world_spawn_projectile(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    struct Task_AppSpawn* task;

    if( app->proj_src_tile_x < 0 )
    {
        app->proj_src_tile_x = tile_x;
        app->proj_src_tile_z = tile_z;
        app->proj_src_tile_level = level;
        fprintf(stderr, "spawn_projectile: source latched at %d,%d\n", tile_x, tile_z);
        return;
    }
    if( app->proj_src_tile_x == tile_x && app->proj_src_tile_z == tile_z )
    {
        app->proj_src_tile_x = -1;
        app->proj_src_tile_z = -1;
        fprintf(stderr, "spawn_projectile: latch cleared\n");
        return;
    }

    task = app_spawn_task_new(app, APP_SPAWN_PROJECTILE, tile_x, tile_z, level);
    task->model_id = app_spawn_id(
        3081 /* v1 spawn-test spotanim model */, app->cfg.spawn_proj_model_id, "TORIRS_SPAWN_PROJ_MODEL");
    task->seq_id = app_spawn_id(
        659 /* v1 spawn-test spotanim sequence (RUNESCAPE_PROJECTILE_SEQ_ID) */,
        app->cfg.spawn_proj_seq_id,
        "TORIRS_SPAWN_PROJ_SEQ");
    task->src_tile_x = app->proj_src_tile_x;
    task->src_tile_z = app->proj_src_tile_z;
    task->src_level = app->proj_src_tile_level;
    task->proj_target = app_world_npc_target_at_tile(app, tile_x, tile_z, level);
    app->proj_src_tile_x = -1;
    app->proj_src_tile_z = -1;
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Hotkey 6: hit every live player/npc for a test hitsplat + half health and
 * give each an overhead chat line, so the whole overlay pass (health bars,
 * hitmarks and overhead chat) can be exercised offline. Goes through the same
 * World_*AddHitmark / World_*SetChat the NPC_INFO/PLAYER_INFO ops use. */
static void
app_world_damage_test(struct App* app)
{
    struct World_EntityPool* pool;
    int damage = 1 + (app->logic_cycle % 30);

    if( !app->world )
        return;
    pool = &app->world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        World_PlayerAddHitmark(app->world, i, damage % 2, damage, 5, 10);
        World_PlayerSetChat(app->world, i, "Hello there!", 0, 0);
        /* Exercise the overhead headicon pass too: icons 0 + 2 stacked. */
        {
            struct WorldEntity_Player* tpl = World_EntityPoolGet(pool, i);
            if( tpl )
                tpl->headicon = 0x5;
        }
    }
    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        World_NpcAddHitmark(app->world, i, damage % 2, damage, 5, 10);
        World_NpcSetChat(app->world, i, "Grrr!", 0, 0);
    }
}

/* Hotkey 4: apply an attached graphic (SPOTANIM mask) to every spawned entity —
 * the reference impact effect a projectile lands on its target. Exercises the
 * entity-spotanim companion-element path headlessly. TORIRS_SPAWN_SPOTANIM /
 * _HEIGHT / _DELAY reuse the free-standing overrides. */
static void
app_world_entity_spotanim_test(struct App* app)
{
    struct World_EntityPool* pool;
    int spotanim_id =
        app_spawn_id(74, app->cfg.spawn_spotanim_id, "TORIRS_SPAWN_SPOTANIM");
    int height = app_spawn_id(92, app->cfg.spawn_spotanim_height, "TORIRS_SPAWN_SPOTANIM_HEIGHT");
    int delay = app_spawn_id(0, app->cfg.spawn_spotanim_delay, "TORIRS_SPAWN_SPOTANIM_DELAY");

    if( !app->world )
        return;
    pool = &app->world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
        World_PlayerSetSpotanim(app->world, i, spotanim_id, height, delay);
    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
        World_NpcSetSpotanim(app->world, i, spotanim_id, height, delay);
}

/* Spawn test hotkeys (readme): 9 player, 8 npc, 7 ground item, 0 projectile,
 * 6 test hitsplat — all act on the tile under the mouse, so they no-op when
 * nothing is hovered. */
static void
app_world_hotkeys(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out)
{
    /* Spawn hotkeys gate on the hovered world tile, not on onKey targets —
     * under the real gameframe there is always some visible onKey component
     * and gating on it made every press suppress itself. */
    (void)out;
    if( !app->world_active || !app->world_view_valid )
        return;
    /* Suppressed while composing chat, so spawn-digit keys type instead. */
    if( app->chat_input_active || app->chat.social_input_open || app->chat.dialog_input_open )
        return;
    if( app->world_hover_tile_x < 0 || app->world_hover_tile_z < 0 )
        return;

    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_9) )
        app_world_spawn_player(
            app, app->world_hover_tile_x, app->world_hover_tile_z, app->world_hover_tile_level);
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_8) )
        app_world_spawn_npc(
            app, app->world_hover_tile_x, app->world_hover_tile_z, app->world_hover_tile_level);
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_6) )
        app_world_damage_test(app);
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_4) )
        app_world_entity_spotanim_test(app);
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_5) )
        app_world_spawn_spotanim(
            app, app->world_hover_tile_x, app->world_hover_tile_z, app->world_hover_tile_level);
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_7) )
        app_world_spawn_obj(
            app, app->world_hover_tile_x, app->world_hover_tile_z, app->world_hover_tile_level);
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_0) )
        app_world_spawn_projectile(
            app, app->world_hover_tile_x, app->world_hover_tile_z, app->world_hover_tile_level);
}

/* Per-frame world step: sim cycles, event drain (entity removals -> scene),
 * position sync, animation ticks. Runs every frame (cycles may be 0) so the
 * painter dynamic set stays fresh, and forces a redraw while active — but only
 * while a viewport is actually on screen; an unshown world does not tick. */
/* Reference followCamera + camFollow (Client-TS 3459/4669): a 1/16-eased
 * orbit anchor trails the player, arrow keys accumulate yaw/pitch velocity,
 * a terrain scan raises pitch so the eye stays above nearby ground, then the
 * eye is placed `pitch*3+600` behind the anchor along pitch/yaw. Sin/cos are
 * 16.16 (same tables as Pix3D). */
static void
app_world_camera_follow(struct App* app)
{
    int world_idx;
    struct WorldEntity_Player* player;
    int target_x, target_y, target_z;
    int pitch, yaw, distance;
    int inv_pitch, inv_yaw;
    int off_x, off_y, off_z;

    if( app->cam_script.scripted || !app->net )
        return;
    if( !RS_EntitySync_FindPlayer(
            &app->esync,
            app->esync.local_pid >= 0 ? app->esync.local_pid : 2047,
            &world_idx,
            NULL) )
        return;
    player = World_EntityPoolGet(&app->world->entities.player, world_idx);
    if( !player )
        return;

    target_x = (int)player->draw_position.x;
    target_z = (int)player->draw_position.z;

    /* Anchor: snap when >500 units out (teleport), else ease 1/16. */
    if( app->orbit_x - target_x < -500 || app->orbit_x - target_x > 500 ||
        app->orbit_z - target_z < -500 || app->orbit_z - target_z > 500 )
    {
        app->orbit_x = target_x;
        app->orbit_z = target_z;
    }
    if( app->orbit_x != target_x )
        app->orbit_x += (target_x - app->orbit_x) / 16;
    if( app->orbit_z != target_z )
        app->orbit_z += (target_z - app->orbit_z) / 16;

    /* Arrow keys -> yaw/pitch velocity (impulse 24/12, halved decay). */
    if( app->cam_key_left )
        app->orbit_yaw_vel += (-app->orbit_yaw_vel - 24) / 2;
    else if( app->cam_key_right )
        app->orbit_yaw_vel += (24 - app->orbit_yaw_vel) / 2;
    else
        app->orbit_yaw_vel = app->orbit_yaw_vel / 2;

    if( app->cam_key_up )
        app->orbit_pitch_vel += (12 - app->orbit_pitch_vel) / 2;
    else if( app->cam_key_down )
        app->orbit_pitch_vel += (-app->orbit_pitch_vel - 12) / 2;
    else
        app->orbit_pitch_vel = app->orbit_pitch_vel / 2;

    app->orbit_yaw = (app->orbit_yaw + app->orbit_yaw_vel / 2) & 0x7ff;
    app->orbit_pitch += app->orbit_pitch_vel / 2;
    if( app->orbit_pitch < 128 )
        app->orbit_pitch = 128;
    if( app->orbit_pitch > 383 )
        app->orbit_pitch = 383;

    /* Terrain pitch clamp: scan the 9x9 tile block around the anchor for
     * ground higher than the anchor's; raise the minimum pitch so the eye
     * clears it. cameraPitchClamp is 24.8 fixed (clamp/256 = pitch units). */
    {
        struct Heightmap* hm = app->world ? app->world->heightmap : NULL;
        int level = player->grid_position.level;
        int orbit_tile_x = app->orbit_x >> 7;
        int orbit_tile_z = app->orbit_z >> 7;
        int orbit_y = app_world_height(app, app->orbit_x, app->orbit_z, level);
        int max_y = 0;
        int clamp;

        if( hm && orbit_tile_x > 3 && orbit_tile_z > 3 && orbit_tile_x < hm->size_x - 4 &&
            orbit_tile_z < hm->size_z - 4 )
        {
            for( int x = orbit_tile_x - 4; x <= orbit_tile_x + 4; x++ )
                for( int z = orbit_tile_z - 4; z <= orbit_tile_z + 4; z++ )
                {
                    /* Reference also bumps to level+1 on VisBelow (bridge)
                     * tiles; tile flags are applied at build time here, so
                     * the stored heights already match what is drawn. */
                    int y = orbit_y - heightmap_get(hm, x, z, level);
                    if( y > max_y )
                        max_y = y;
                }
        }
        clamp = max_y * 192;
        if( clamp > 98048 )
            clamp = 98048;
        if( clamp < 32768 )
            clamp = 32768;
        if( clamp > app->camera_pitch_clamp )
            app->camera_pitch_clamp += (clamp - app->camera_pitch_clamp) / 24;
        else if( clamp < app->camera_pitch_clamp )
            app->camera_pitch_clamp += (clamp - app->camera_pitch_clamp) / 80;
    }

    pitch = app->orbit_pitch;
    if( app->camera_pitch_clamp / 256 > pitch )
        pitch = app->camera_pitch_clamp / 256;
    yaw = app->orbit_yaw & 0x7ff;
    distance = pitch * 3 + 600;
    off_x = 0;
    off_y = 0;
    off_z = distance;

    target_y = app_world_height(app, target_x, target_z, player->grid_position.level) - 50;
    target_x = app->orbit_x;
    target_z = app->orbit_z;

    inv_pitch = (2048 - pitch) & 0x7ff;
    inv_yaw = (2048 - yaw) & 0x7ff;
    if( inv_pitch != 0 )
    {
        int sin = ToriDraw_Sin(inv_pitch);
        int cos = ToriDraw_Cos(inv_pitch);
        int tmp = (off_y * cos - distance * sin) >> 16;
        off_z = (off_y * sin + distance * cos) >> 16;
        off_y = tmp;
    }
    if( inv_yaw != 0 )
    {
        int sin = ToriDraw_Sin(inv_yaw);
        int cos = ToriDraw_Cos(inv_yaw);
        int tmp = (off_z * sin + off_x * cos) >> 16;
        off_z = (off_z * cos - off_x * sin) >> 16;
        off_x = tmp;
    }

    app->world_camera_pos.x = target_x - off_x;
    app->world_camera_pos.y = target_y - off_y;
    app->world_camera_pos.z = target_z - off_z;
    app->world_camera.pitch = pitch;
    app->world_camera.yaw = yaw;
}

static void
app_world_frame(
    struct App* app,
    int cycles)
{
    struct World* world = app->world;

    if( !app->world_active || !app->world_view_valid || !world )
        return;

    /* Mirror the local pid so the render cycle's dynamic pass can register the
     * local player first (reference addPlayers(true) precedence). */
    world->local_pid = app->esync.local_pid;

    World_Cycle(world, cycles);

    {
        int count = World_EventsCount(world);
        for( int i = 0; i < count; i++ )
        {
            const struct World_Event* ev = World_EventsPeek(world, i);
            if( ev->kind == WorldEventKind_EntityRemoved && ev->element_id >= 0 )
            {
                /* Free any attached-graphic combine snapshots keyed to this
                 * element before the element (and its model) goes away. */
                app_entity_spotanim_drop(app, ev->element_id);
                ToriDraw_SceneElementRemove(app->scene, ev->element_id);
            }
        }
        World_EventsClear(world);
    }

    app_world_sync_positions(app);
    app_world_camera_follow(app);
    app_world_sync_entity_animations(app);
    app_world_sync_entity_spotanims(app);

    for( int c = 0; c < cycles; c++ )
        app_world_tick_animations(app);

    /* Texture scroll (water/lava): dat2 texture defs carry direction/speed;
     * the map advances them per elapsed cycle (v1 runescape.c:3893). */
    if( cycles > 0 )
    {
        struct ToriDraw_TextureState* tex_state = ToriDraw_SceneTexState(app->scene);
        if( tex_state )
            ToriDraw_TextureMapAnimate(&tex_state->texture_map, cycles);
    }

    app->need_redraw = 1;
}

static int
app_measure_text_cb(
    void* user,
    int font_id,
    char const* text)
{
    struct App* app = (struct App*)user;
    struct ToriDraw_Font* font = ToriDraw_SceneFontGet(app->scene, font_id);
    if( !font || !text )
        return 0;
    return ToriDraw2D_MeasureString(font, text);
}

/* Snapshot the armed use/target selection for the minimenu builder (reference
 * useMode/targetMode). objsel and targetsel are mutually exclusive — arming one
 * clears the other. */
static struct RS_MinimenuSelection
app_minimenu_selection(struct App const* app)
{
    struct RS_MinimenuSelection sel = { .mode = RS_MINIMENU_SELECT_NONE };
    if( app->objsel.active )
    {
        sel.mode = RS_MINIMENU_SELECT_USE_ITEM;
        snprintf(sel.obj_name, sizeof(sel.obj_name), "%s", app->objsel.name);
        sel.obj_slot = app->objsel.slot;
        sel.obj_com_id = app->objsel.component_id;
    }
    else if( app->targetsel.active )
    {
        sel.mode = RS_MINIMENU_SELECT_TARGET;
        snprintf(sel.target_op, sizeof(sel.target_op), "%s", app->targetsel.op);
        sel.target_mask = app->targetsel.mask;
    }
    return sel;
}

/*
 * Mouseover text, rebuilt every frame from a scratch menu at the pointer.
 *
 * This is the client half of what the reference gets from the cache: script
 * 4726 (re-armed each cycle by 4725) bails on minimenu_isopen, reads
 * minimenu_entry / _numops / _type, and has proc 4727 draw one line. Building
 * the same menu the right click would show is exactly what those opcodes
 * report, so both the line drawn here and the CS2 snapshot come from one pass.
 */
static void
app_hover_text_update(
    struct App* app,
    int mouse_x,
    int mouse_y)
{
    struct UIMinimenu scratch;
    char prev[UITREE_HOVERTEXT_LEN];
    bool const prev_visible = app->hover_text.visible;
    int click_in_world;

    snprintf(prev, sizeof(prev), "%s", app->hover_text.text);

    /* 4726's first gate: no hover line while the Choose Option popup is up. */
    if( app->interact.minimenu.visible || mouse_x < 0 || mouse_y < 0 ||
        mouse_x >= UITREE_LAYOUT_ROOT_W || mouse_y >= UITREE_LAYOUT_ROOT_H )
    {
        app->hover_text.visible = false;
        app->hover_text.text[0] = '\0';
    }
    else
    {
        click_in_world =
            app_world_mouse_gate(app, mouse_x, mouse_y) && app_world_drawable(app);
        {
            struct RS_MinimenuBuildCtx mctx = {
                .tree = app->tree,
                .ui_host = &app->ui_host,
                .provider = app->provider,
                .runner = &app->runner,
                .invs = &app->invs,
                .chat = &app->chat_source,
                .selection = app_minimenu_selection(app),
                .world = app->world,
                /* Same rule the click paths use: world rows only when the
                 * pointer is over bare viewport. */
                .world_pickset = click_in_world ? &app->world_pickset : NULL,
                .click_in_world = click_in_world != 0,
            };
            UIMinimenu_Reset(&scratch);
            scratch.font_id = app->hover_text.font_id;
            RS_Minimenu_Build(&mctx, mouse_x, mouse_y, &scratch);
        }
        UIHoverText_Compose(&scratch, &app->hover_text);
    }

    /* Anchor at the world viewport's top-left (4726's container origin), or
     * the canvas when no viewport is on screen. */
    if( app->world_view_valid )
    {
        app->hover_text.x = app->world_emit_desc.clip.x + APP_HOVERTEXT_INSET_X;
        app->hover_text.y = app->world_emit_desc.clip.y + APP_HOVERTEXT_INSET_Y;
        app->hover_text.w = app->world_emit_desc.clip.w - APP_HOVERTEXT_INSET_X;
    }
    else
    {
        app->hover_text.x = APP_HOVERTEXT_INSET_X;
        app->hover_text.y = APP_HOVERTEXT_INSET_Y;
        app->hover_text.w = UITREE_LAYOUT_ROOT_W - APP_HOVERTEXT_INSET_X;
    }

    if( app->hover_text.visible != prev_visible || strcmp(app->hover_text.text, prev) != 0 )
        app->need_redraw = 1;
}

/* Build + show the minimenu for a right click (reference openMenu: width from
 * the widest row, centered on the click, clamped to the canvas). The tree
 * node stays unpositioned — emit and the interact gesture read the model. */
static void
app_minimenu_open(
    struct App* app,
    int click_x,
    int click_y,
    int click_in_world)
{
    struct RS_MinimenuBuildCtx mctx = {
        .tree = app->tree,
        .ui_host = &app->ui_host,
        .provider = app->provider,
        .runner = &app->runner,
        .invs = &app->invs,
        .chat = &app->chat_source,
        .selection = app_minimenu_selection(app),
        .world = app->world,
        .world_pickset = &app->world_pickset,
        .click_in_world = click_in_world != 0,
    };
    struct UIMinimenu* menu = &app->interact.minimenu;
    struct UIMinimenuLayout layout;
    int content_w = 0;
    int line_box = 0;

    RS_Minimenu_Build(&mctx, click_x, click_y, menu);

    /* TORIRS_MINIMENU_DEBUG=1: the world pickset that fed the rows plus every
     * row built from it — the one place to see why a loc/obj came up bare. */
    if( getenv("TORIRS_MINIMENU_DEBUG") )
    {
        fprintf(
            stderr,
            "minimenu: open at %d,%d in_world=%d picks=%d\n",
            click_x,
            click_y,
            click_in_world,
            click_in_world ? app->world_pickset.count : 0);
        if( click_in_world )
            for( int i = 0; i < app->world_pickset.count; i++ )
            {
                struct World_Picked const* picked = &app->world_pickset.items[i];
                fprintf(
                    stderr,
                    "  pick[%d] type=%d element=%d tile=%d,%d,%d\n",
                    i,
                    (int)picked->type,
                    picked->element_id,
                    picked->tile_x,
                    picked->tile_z,
                    picked->tile_level);
            }
        for( int i = 0; i < menu->option_count; i++ )
            fprintf(
                stderr,
                "  row[%d] action=%d op=%d kind=%d id=%d \"%s\"\n",
                i,
                menu->options[i].action,
                menu->options[i].action_index,
                (int)menu->options[i].pick.kind,
                menu->options[i].pick.id,
                menu->options[i].text);
    }

    {
        struct ToriDraw_Font* font = ToriDraw_SceneFontGet(app->scene, menu->font_id);
        if( font )
            line_box = ToriDraw_FontLineBoxHeight(font);
    }
    if( UIMinimenu_PrepareShow(menu, line_box, app_measure_text_cb, app, &layout, &content_w) )
    {
        UIMinimenu_ShowAt(
            menu, layout, content_w, click_x, click_y, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        app->need_redraw = 1;
    }
}

/* Execute one selected (or defaulted) menu row: cross feedback + hook
 * dispatch with the row's op index (v1 ui_click_use_minimenu_option). The
 * cross colour comes from the action alone (RS_Minimenu_CrossModeForAction,
 * reference doAction) and is decided once: an action the reference gives no
 * cross — UI buttons, inventory ops, Examine, Cancel — leaves a cross already
 * in flight running rather than clearing or recolouring it. Returns nonzero
 * when a CS2 hook was dispatched. */
/* Send the held-item / component op for the current inventory pick. Returns 1
 * when a use-mode selection or examine consumed the click without a packet. */
static int
app_minimenu_inv_action(
    struct App* app,
    struct UIMinimenuOption const* opt)
{
    int obj_id = opt->pick.tertiary_id;
    int slot = opt->pick.secondary_id;
    int com_id = opt->pick.id;

    /* "Use <held> with <this item>": the previous click armed objsel; this
     * click on an inventory item completes it (reference OPHELDU). */
    if( app->objsel.active )
    {
        APP_NET_SEND(
            app,
            net_out_opheldu(
                app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                obj_id, slot, com_id, app->objsel.obj_id, app->objsel.slot,
                app->objsel.component_id));
        app->objsel.active = 0;
        return 1;
    }

    /* "<spell> <this item>": cast a selected spell on the inventory item
     * (reference TGT_HELD → OPHELDT). */
    if( opt->action == REVCONFIG_MINIMENU_TGT_HELD && app->targetsel.active )
    {
        APP_NET_SEND(
            app,
            net_out_opheldt(
                app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                obj_id, slot, com_id, app->targetsel.component_id));
        app->targetsel.active = 0;
        return 1;
    }

    switch( opt->action )
    {
    case REVCONFIG_MINIMENU_OPHELD1:
    case REVCONFIG_MINIMENU_OPHELD2:
    case REVCONFIG_MINIMENU_OPHELD3:
    case REVCONFIG_MINIMENU_OPHELD4:
    case REVCONFIG_MINIMENU_OPHELD5:
        APP_NET_SEND(
            app,
            net_out_opheld(
                app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                opt->action_index + 1, obj_id, slot, com_id));
        return 0;
    case REVCONFIG_MINIMENU_INV_BUTTON1:
    case REVCONFIG_MINIMENU_INV_BUTTON2:
    case REVCONFIG_MINIMENU_INV_BUTTON3:
    case REVCONFIG_MINIMENU_INV_BUTTON4:
    case REVCONFIG_MINIMENU_INV_BUTTON5:
        APP_NET_SEND(
            app,
            net_out_inv_button(
                app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                opt->action_index + 1, obj_id, slot, com_id));
        return 0;
    case REVCONFIG_MINIMENU_OPHELDT_START:
    {
        /* "Use <item>": enter selection mode; the next click targets it. */
        struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
        app->objsel.active = 1;
        app->objsel.obj_id = obj_id;
        app->objsel.slot = slot;
        app->objsel.component_id = com_id;
        snprintf(app->objsel.name, sizeof(app->objsel.name), "%s",
                 obj && obj->name[0] ? obj->name : "item");
        app->need_redraw = 1;
        return 1;
    }
    case REVCONFIG_MINIMENU_OPHELD6:
    {
        /* Examine: client-side chat print (no packet). Reference doAction
         * OP_HELD6: huge stacks show the exact count, else the config desc,
         * else the generic fallback. */
        struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
        char const* name = (obj && obj->name[0]) ? obj->name : "item";
        int count = opt->pick.quaternary_id;
        char line[TORIRS_DESC_MAX + 32];
        if( count >= 100000 )
            snprintf(line, sizeof(line), "%d x %s", count, name);
        else if( obj && obj->desc[0] )
            snprintf(line, sizeof(line), "%s", obj->desc);
        else
            snprintf(line, sizeof(line), "It's a %s.", name);
        RS_Chat_AddMessage(&app->chat, RS_CHAT_TYPE_GAME, NULL, line);
        app->need_redraw = 1;
        return 1;
    }
    default:
        return 0;
    }
}

#include "ui/uitree_inv_view.h"

/* Resolve the RS_INV node under a canvas point + the slot within it. Returns
 * the tree index (or -1) and fills *out_slot / *out_source_id. Goes through
 * UITree_CollectNodesAt (visibility, clipping, selected sidebar tab, and the
 * per-slot hit rule): an inv component's own layout bounds are cols x rows
 * PIXELS (4x7 for the backpack), so gating on the node box never matches a
 * slot click — the same trap fixed in collect_inv_grid_slot_hit. */
static int32_t
app_inv_node_at(
    struct App* app,
    int px,
    int py,
    int* out_slot,
    int* out_source_id)
{
    int32_t hits[16];
    int hit_count =
        UITree_CollectNodesAt(app->tree, &app->ui_host, px, py, hits, 16);

    for( int i = 0; i < hit_count; i++ )
    {
        struct UITreeComponent const* node = &app->tree->components[hits[i]];
        struct UITreeInvGridLayout layout;
        int bx = 0, by = 0, bw = 0, bh = 0, offx = 0, offy = 0, slot;

        if( node->type != UIELEM_RS_INV )
            continue;
        UITree_LayoutGetBounds(&node->position, &bx, &by, &bw, &bh);

        layout.cols = node->u.rs_inv.cols;
        layout.rows = node->u.rs_inv.rows;
        layout.margin_x = node->u.rs_inv.margin_x;
        layout.margin_y = node->u.rs_inv.margin_y;
        layout.offset_x = node->u.rs_inv.inv_slot_offset_x;
        layout.offset_y = node->u.rs_inv.inv_slot_offset_y;
        UITree_AccumScrollOffset(app->tree, hits[i], &offx, &offy);
        slot = UITree_InvViewGridHitTest(bx, by, &layout, px + offx, py + offy);
        if( slot < 0 )
            continue;
        if( out_slot )
            *out_slot = slot;
        if( out_source_id )
            *out_source_id = node->u.rs_inv.inv_source_id;
        return hits[i];
    }
    return -1;
}

static int
app_minimenu_run_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y);

static int
app_minimenu_use_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y);

/* Run the default (top) menu row for a click at (x, y) — the reference
 * doAction(menuNumEntries - 1) short-click path. UI rows only (no world
 * pickset): used by the inventory slot machine below, where the click is by
 * construction over a component. */
static void
app_run_default_ui_row(struct App* app, int click_x, int click_y)
{
    struct RS_MinimenuBuildCtx mctx = {
        .tree = app->tree,
        .ui_host = &app->ui_host,
        .provider = app->provider,
        .runner = &app->runner,
        .invs = &app->invs,
        .chat = &app->chat_source,
        .world = app->world,
        .world_pickset = NULL,
        .click_in_world = false,
    };
    struct UIMinimenu scratch;
    int default_idx;

    mctx.selection = app_minimenu_selection(app);
    UIMinimenu_Reset(&scratch);
    scratch.font_id = app->interact.minimenu.font_id;
    RS_Minimenu_Build(&mctx, click_x, click_y, &scratch);
    default_idx = RS_Minimenu_DefaultOptionIndex(&scratch);
    if( default_idx >= 0 )
    {
        struct UIMinimenu saved = app->interact.minimenu;
        app->interact.minimenu = scratch;
        app_minimenu_use_option(app, default_idx, click_x, click_y);
        app->interact.minimenu = saved;
    }
}

/* Inventory slot press/drag/click (reference objDrag* machine, Client.ts
 * mouseLoop 8584 + gameLoop 2476):
 *  - left press over a FILLED slot arms; nothing fires on the down edge.
 *  - each held cycle: cycles++, >5px of travel sets the grab threshold, and
 *    the emit offset dx/dy is recomputed (+-5px deadzone, zero before 5
 *    cycles) so the armed icon follows the mouse at trans 128.
 *  - release: real drag (threshold && cycles >= 5) resolves the slot under
 *    the mouse — same source, different slot → optimistic local swap +
 *    INV_BUTTOND(com, src, dst, 0); anything else is a SHORT CLICK and runs
 *    the default menu row (how a left click submits OPHELD*).
 * While armed the generic node drag is suppressed (tree->anti_drag; the
 * reference freezes mouseLoop/buildMinimenu during objDragArea != 0) so the
 * grid's ancestors can never pick the press up as a whole-panel drag. */
static void
app_inv_drag_tick(
    struct App* app,
    struct LibToriRS_Input* input)
{
    int mx = input->curr.mouse_x;
    int my = input->curr.mouse_y;

    /* Never arm while the right-click popup is open: its option rows overlap
     * the grid and the reference routes those clicks through the menu first. */
    if( app->inv_drag_com_id < 0 && !app->interact.minimenu.visible &&
        LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) )
    {
        int slot = -1, source_id = -1;
        int32_t idx = app_inv_node_at(app, mx, my, &slot, &source_id);
        if( idx >= 0 )
        {
            struct InvSlot inv_slot;
            if( source_id >= 0 && InvManager_GetSlot(&app->invs, source_id, slot, &inv_slot) &&
                inv_slot.obj_id > 0 )
            {
                app->inv_drag_com_id = app->tree->components[idx].component_id;
                app->inv_drag_can_drag = app->tree->components[idx].u.rs_inv.can_drag;
                app->inv_drag_from_slot = slot;
                app->inv_drag_source_id = source_id;
                app->inv_drag_cycles = 0;
                app->inv_drag_grab_x = mx;
                app->inv_drag_grab_y = my;
                app->inv_drag_threshold = 0;
                app->inv_drag_dx = 0;
                app->inv_drag_dy = 0;
                app->need_redraw = 1; /* armed slot renders trans-128 now */
            }
        }
    }

    if( app->inv_drag_com_id < 0 )
    {
        if( app->tree )
            app->tree->anti_drag = 0;
        return;
    }
    if( app->tree )
        app->tree->anti_drag = 1;

    if( !input->curr.mouse_button_up[TORIRSM_LEFT] &&
        LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT) )
    {
        int dx = mx - app->inv_drag_grab_x;
        int dy = my - app->inv_drag_grab_y;

        /* Non-draggable grid (equipment/worn: objSwap && objReplace both
         * false): the held item still fades to trans 128 in place (dx/dy stay
         * 0, so it never moves — GET_INV_DRAG still reports the slot), the
         * press still counts as a click on release, but it never promotes to a
         * drag — no threshold, no offset, no swap. */
        if( !app->inv_drag_can_drag )
            return;

        app->inv_drag_cycles++;
        if( dx > 5 || dx < -5 || dy > 5 || dy < -5 )
            app->inv_drag_threshold = 1;

        /* Visual offset: reference zeroes each axis inside +-5px and both
         * until the 5-cycle dead time passes. */
        if( dx < 5 && dx > -5 )
            dx = 0;
        if( dy < 5 && dy > -5 )
            dy = 0;
        if( app->inv_drag_cycles < 5 )
        {
            dx = 0;
            dy = 0;
        }
        if( dx != app->inv_drag_dx || dy != app->inv_drag_dy )
        {
            app->inv_drag_dx = dx;
            app->inv_drag_dy = dy;
            app->need_redraw = 1;
        }
        return;
    }

    /* Released. */
    if( app->inv_drag_threshold && app->inv_drag_cycles >= 5 )
    {
        int to_slot = -1, to_source = -1;
        int32_t idx = app_inv_node_at(app, mx, my, &to_slot, &to_source);
        if( idx >= 0 && to_source == app->inv_drag_source_id &&
            to_slot != app->inv_drag_from_slot )
        {
            InvManager_SwapSlots(
                &app->invs, app->inv_drag_source_id, app->inv_drag_from_slot, to_slot);
            /* The swap is applied locally so the drag feels instant; the CS2
             * paint script still has to be asked to repaint it. */
            RS_CS2Host_NotifyInvChanged(
                &app->host,
                InvManager_ContainerForSource(&app->invs, app->inv_drag_source_id));
            APP_NET_SEND(
                app,
                net_out_inv_buttond(
                    app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                    app->inv_drag_com_id, app->inv_drag_from_slot, to_slot, 0));
        }
    }
    else
    {
        app_run_default_ui_row(app, mx, my);
    }
    app->inv_drag_com_id = -1;
    app->inv_drag_dx = 0;
    app->inv_drag_dy = 0;
    if( app->tree )
        app->tree->anti_drag = 0;
    app->need_redraw = 1;
}

static int
app_minimenu_run_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y)
{
    struct UIMinimenu* menu = &app->interact.minimenu;
    struct UIMinimenuOption opt;

    if( option_index < 0 || option_index >= menu->option_count )
        return 0;
    opt = menu->options[option_index];
    UIMinimenu_Hide(menu);
    app->need_redraw = 1;

    /* Reference doAction has no CANCEL branch at all: dismissing the menu is
     * not an interaction and must not disturb a running cross. */
    if( opt.action == REVCONFIG_MINIMENU_CANCEL )
        return 0;

    {
        enum UICrossMode cross_mode = RS_Minimenu_CrossModeForAction(opt.action);
        if( cross_mode != UI_CROSS_OFF )
            UICross_Show(&app->cross, cross_mode, click_x, click_y);
    }

    /* Examine (OPLOC6/OPNPC6/OPOBJ6): reference resolves these locally from the
     * config desc and sends no packet (Client-TS doAction OP_LOC6/OP_NPC6/
     * OP_OBJ6). Look the desc up from the config by the entity's type id, and
     * fall back to "It's a <name>." when the config carries none (e.g. dat2
     * NPCs, whose examine is server-driven). Must run before the pick.kind
     * switch or the scenery/NPC cases would mis-send OPLOC1/OPNPC1. */
    if( opt.action == REVCONFIG_MINIMENU_OPLOC6 || opt.action == REVCONFIG_MINIMENU_OPNPC6 ||
        opt.action == REVCONFIG_MINIMENU_OPOBJ6 )
    {
        char const* name = NULL;
        char const* desc = NULL;
        char line[TORIRS_DESC_MAX + 32];
        if( app->world && opt.action == REVCONFIG_MINIMENU_OPLOC6 )
        {
            struct WorldEntity_Scenery* scenery =
                World_SceneryGetByElementId(app->world, opt.pick.id);
            if( scenery )
            {
                struct ToriRS_Location* loc =
                    CacheProvider_LocationGet(app->provider, scenery->loc_id);
                if( loc && loc->desc[0] )
                    desc = loc->desc;
                if( scenery->name[0] )
                    name = scenery->name;
            }
        }
        else if( app->world && opt.action == REVCONFIG_MINIMENU_OPOBJ6 )
        {
            struct WorldEntity_ObjStack* stack =
                World_ObjStackGetByElementId(app->world, opt.pick.id);
            if( stack )
            {
                struct ToriRS_Objtype* obj =
                    CacheProvider_ObjtypeGet(app->provider, stack->obj_id);
                if( obj && obj->desc[0] )
                    desc = obj->desc;
                if( stack->name[0] )
                    name = stack->name;
            }
        }
        else if( app->world )
        {
            struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, opt.pick.id, NULL);
            if( npc )
            {
                struct ToriRS_Npctype* npctype =
                    CacheProvider_NpctypeGet(app->provider, npc->npc_id);
                if( npctype && npctype->desc[0] )
                    desc = npctype->desc;
                if( npc->name[0] )
                    name = npc->name;
            }
        }
        if( desc )
            snprintf(line, sizeof(line), "%s", desc);
        else
            snprintf(line, sizeof(line), "It's a %s.", name ? name : "mystery");
        RS_Chat_AddMessage(&app->chat, RS_CHAT_TYPE_GAME, NULL, line);
        app->need_redraw = 1;
        return 1;
    }

    /* Arm target mode from a spell/prayer button (reference TGT_BUTTON): the
     * next click on a valid target casts. objsel and targetsel are mutually
     * exclusive. */
    if( opt.action == REVCONFIG_MINIMENU_TGT_BUTTON )
    {
        int32_t idx = UITree_FindByComponentId(app->tree, opt.pick.id);
        app->objsel.active = 0;
        app->targetsel.active = 1;
        app->targetsel.component_id = opt.pick.id;
        app->targetsel.mask = 0;
        app->targetsel.op[0] = '\0';
        if( idx >= 0 )
        {
            struct UITreeComponent const* node = &app->tree->components[idx];
            /* targetMask rode in as click_mask on the BUTTON_TARGET node. */
            app->targetsel.mask = node->behavior.click_mask;
            /* Reference targetOp = "<verb-prefix> <base> <verb-suffix>". */
            {
                char const* verb = node->menu_options.target_verb;
                char const* base = node->menu_options.target_base;
                char const* space = strchr(verb, ' ');
                if( space )
                    snprintf(
                        app->targetsel.op, sizeof(app->targetsel.op), "%.*s %s%s",
                        (int)(space - verb), verb, base, space);
                else
                    snprintf(
                        app->targetsel.op, sizeof(app->targetsel.op), "%s %s", verb, base);
            }
        }
        app->need_redraw = 1;
        return 1;
    }

    /* "Use <held> with <world target>" (reference USEHELD_ONLOC/NPC/OBJ) and
     * "<spell> <world target>" (TGT_LOC/NPC/OBJ). Both read the world pick and
     * the armed selection; handled here so the pick.kind switch stays the
     * plain-op path. */
    if( app->world &&
        (opt.action == REVCONFIG_MINIMENU_USEHELD_ONLOC ||
         opt.action == REVCONFIG_MINIMENU_USEHELD_ONNPC ||
         opt.action == REVCONFIG_MINIMENU_USEHELD_ONOBJ ||
         opt.action == REVCONFIG_MINIMENU_TGT_LOC || opt.action == REVCONFIG_MINIMENU_TGT_NPC ||
         opt.action == REVCONFIG_MINIMENU_TGT_OBJ) )
    {
        int abs_x = opt.pick.tertiary_id + app->world->_base_tile_x;
        int abs_z = opt.pick.quaternary_id + app->world->_base_tile_z;
        switch( opt.action )
        {
        case REVCONFIG_MINIMENU_USEHELD_ONLOC:
            app_try_move_loc(app, opt.pick.id, opt.pick.tertiary_id, opt.pick.quaternary_id);
            APP_NET_SEND(
                app, net_out_oplocu(
                         app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), abs_x, abs_z,
                         opt.pick.secondary_id, app->objsel.obj_id, app->objsel.slot,
                         app->objsel.component_id));
            break;
        case REVCONFIG_MINIMENU_TGT_LOC:
            app_try_move_loc(app, opt.pick.id, opt.pick.tertiary_id, opt.pick.quaternary_id);
            APP_NET_SEND(
                app, net_out_oploct(
                         app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), abs_x, abs_z,
                         opt.pick.secondary_id, app->targetsel.component_id));
            break;
        case REVCONFIG_MINIMENU_USEHELD_ONOBJ:
            app_try_move_obj(app, opt.pick.tertiary_id, opt.pick.quaternary_id);
            APP_NET_SEND(
                app, net_out_opobju(
                         app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), abs_x, abs_z,
                         opt.pick.secondary_id, app->objsel.obj_id, app->objsel.slot,
                         app->objsel.component_id));
            break;
        case REVCONFIG_MINIMENU_TGT_OBJ:
            app_try_move_obj(app, opt.pick.tertiary_id, opt.pick.quaternary_id);
            APP_NET_SEND(
                app, net_out_opobjt(
                         app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), abs_x, abs_z,
                         opt.pick.secondary_id, app->targetsel.component_id));
            break;
        case REVCONFIG_MINIMENU_USEHELD_ONNPC:
        {
            struct WorldEntity_NPC* npc =
                World_NpcGetByElementId(app->world, opt.pick.id, NULL);
            if( npc && npc->server_slot >= 0 )
            {
                app_try_move_npc(app, npc, 0);
                APP_NET_SEND(
                    app, net_out_opnpcu(
                             app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                             npc->server_slot, app->objsel.obj_id, app->objsel.slot,
                             app->objsel.component_id));
            }
            break;
        }
        case REVCONFIG_MINIMENU_TGT_NPC:
        {
            struct WorldEntity_NPC* npc =
                World_NpcGetByElementId(app->world, opt.pick.id, NULL);
            if( npc && npc->server_slot >= 0 )
            {
                app_try_move_npc(app, npc, 0);
                APP_NET_SEND(
                    app, net_out_opnpct(
                             app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                             npc->server_slot, app->targetsel.component_id));
            }
            break;
        }
        default:
            break;
        }
        app->objsel.active = 0;
        app->targetsel.active = 0;
        return 0;
    }

    switch( opt.pick.kind )
    {
    case UI_MINIMENU_PICK_UI:
    case UI_MINIMENU_PICK_INV_SLOT:
    {
        int32_t idx = UITree_FindByComponentId(app->tree, opt.pick.id);
        struct UITreeRuntimeScriptHook const* hook;
        struct UITreeRuntimeScriptHook hook_copy;
        int hook_com_id = -1;

        if( idx < 0 )
            return 0;
        hook = UITree_ResolveClickHook(app->tree, idx, &hook_com_id);
        if( !hook || hook->script_id <= 0 )
        {
            /* Inventory item ops go straight to the server (reference
             * OPHELD/INV_BUTTON) — no CS2 hook is involved. */
            if( opt.pick.kind == UI_MINIMENU_PICK_INV_SLOT &&
                app_minimenu_inv_action(app, &opt) )
                return 0;
            if( opt.pick.kind == UI_MINIMENU_PICK_INV_SLOT &&
                (opt.action >= REVCONFIG_MINIMENU_OPHELD1 &&
                 opt.action <= REVCONFIG_MINIMENU_OPHELD6) )
                return 0;
            if( opt.pick.kind == UI_MINIMENU_PICK_INV_SLOT &&
                opt.action >= REVCONFIG_MINIMENU_INV_BUTTON1 &&
                opt.action <= REVCONFIG_MINIMENU_INV_BUTTON5 )
                return 0;
            /* IF1-style static buttons have no CS2 hook — the button engine
             * applies buttonType/varp semantics locally (+ server notify via
             * the sink once networking attaches). */
            if( RS_IF1_ApplyButtonClick(app, opt.pick.id, opt.action) )
                return 0;
            fprintf(
                stderr,
                "minimenu: no hook for com=0x%x action=%d op=%d\n",
                opt.pick.id,
                opt.action,
                opt.action_index);
            return 0;
        }
        hook_copy = *hook;
        RS_CS2_SetEventOp(&app->host, opt.action_index >= 0 ? opt.action_index + 1 : 1, 0);
        RS_CS2_SetEventMouse(&app->host, click_x, click_y);
        RS_CS2_DispatchHook(&app->host, &app->runner, hook_com_id, &hook_copy);
        RS_CS2_SetEventOp(&app->host, 1, 0);
        RS_CS2_PumpTransmits(&app->host, &app->runner);
        return 1;
    }
    case UI_MINIMENU_PICK_TERRAIN:
        /* Walk here (reference tryMove type 0): BFS route + MOVE_GAMECLICK
         * waypoints; no local prediction — the PLAYER_INFO echo moves the
         * player. */
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "minimenu: walk-click scene=%d,%d abs=%d,%d\n",
                opt.pick.secondary_id,
                opt.pick.tertiary_id,
                app->world ? app->world->_base_tile_x + opt.pick.secondary_id : -1,
                app->world ? app->world->_base_tile_z + opt.pick.tertiary_id : -1);
        app_try_move(app, opt.pick.secondary_id, opt.pick.tertiary_id, 0, 0, 0, 0, 0);
        return 0;
    case UI_MINIMENU_PICK_NPC:
    {
        struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, opt.pick.id, NULL);
        if( !npc || npc->server_slot < 0 )
            return 0; /* not yet server-synced */
        /* Reference OP_NPC1..5 / USEHELD_ONNPC walk toward the NPC (tryMove
         * type 2) on the same click; the OP is sent regardless of the route. */
        app_try_move_npc(app, npc, 0);
        if( app->objsel.active )
        {
            APP_NET_SEND(
                app,
                net_out_opnpcu(
                    app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                    npc->server_slot, app->objsel.obj_id, app->objsel.slot,
                    app->objsel.component_id));
            app->objsel.active = 0;
        }
        else
        {
            APP_NET_SEND(
                app,
                net_out_opnpc(
                    app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                    opt.action_index + 1, npc->server_slot));
        }
        return 0;
    }
    case UI_MINIMENU_PICK_SCENERY:
    {
        int abs_x = opt.pick.tertiary_id + app->world->_base_tile_x;
        int abs_z = opt.pick.quaternary_id + app->world->_base_tile_z;
        int loc_id = opt.pick.secondary_id;
        /* Reference interactWithLoc: pathfind toward the loc (tryMove type 2)
         * on the same click, then send the OP below regardless of the walk
         * result. The scene tile is the pick's (tertiary,quaternary). */
        app_try_move_loc(app, opt.pick.id, opt.pick.tertiary_id, opt.pick.quaternary_id);
        if( app->objsel.active )
        {
            APP_NET_SEND(
                app,
                net_out_oplocu(
                    app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                    abs_x, abs_z, loc_id, app->objsel.obj_id, app->objsel.slot,
                    app->objsel.component_id));
            app->objsel.active = 0;
        }
        else
        {
            APP_NET_SEND(
                app,
                net_out_oploc(
                    app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                    opt.action_index + 1, abs_x, abs_z, loc_id));
        }
        return 0;
    }
    case UI_MINIMENU_PICK_OBJ:
    {
        /* Ground item (reference OP_OBJ1..5 / USEHELD_ONOBJ): the wire
         * carries the tile + obj id, not the scene element. */
        int abs_x = opt.pick.tertiary_id + app->world->_base_tile_x;
        int abs_z = opt.pick.quaternary_id + app->world->_base_tile_z;
        int obj_id = opt.pick.secondary_id;
        /* Reference obj doAction: pathfind to the exact tile (tryMove type 2),
         * and on failure retry a 1x1 approach so an adjacent tile still arrives;
         * the OP is sent below on the same click either way. */
        app_try_move_obj(app, opt.pick.tertiary_id, opt.pick.quaternary_id);
        if( app->objsel.active )
        {
            APP_NET_SEND(
                app,
                net_out_opobju(
                    app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                    abs_x, abs_z, obj_id, app->objsel.obj_id, app->objsel.slot,
                    app->objsel.component_id));
            app->objsel.active = 0;
        }
        else
        {
            APP_NET_SEND(
                app,
                net_out_opobj(
                    app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                    opt.action_index + 1, abs_x, abs_z, obj_id));
        }
        return 0;
    }
    default:
        fprintf(stderr, "minimenu: unhandled pick kind %d\n", (int)opt.pick.kind);
        return 0;
    }
}

/* Execute one menu row, then apply the reference doAction tail
 * (Client.ts:9506): every executed row clears useMode/targetMode EXCEPT the two
 * arming rows (USEHELD_START / TGT_BUTTON), which return early. So any click
 * that isn't "arm Use" or "arm spell" cancels a pending selection — Walk here, a
 * plain op, a UI button, Cancel — i.e. clicking off anything that can't be a use
 * target drops the white outline. The arming rows set the selection inside
 * run_option and must survive; they alone skip the clear. */
static int
app_minimenu_use_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y)
{
    struct UIMinimenu* menu = &app->interact.minimenu;
    int action = -1;
    int had_selection;
    int result;

    if( option_index >= 0 && option_index < menu->option_count )
        action = menu->options[option_index].action;

    had_selection = app->objsel.active || app->targetsel.active;
    result = app_minimenu_run_option(app, option_index, click_x, click_y);

    if( action != REVCONFIG_MINIMENU_OPHELDT_START &&
        action != REVCONFIG_MINIMENU_TGT_BUTTON )
    {
        app->objsel.active = 0;
        app->targetsel.active = 0;
        if( had_selection )
            app->need_redraw = 1;
    }
    return result;
}

void
App_PlaySound(
    struct App* app,
    int sound_id,
    int loops,
    int delay)
{
    assert(app);
    RS_Audio_Synth(&app->audio, sound_id, loops, delay);
}

int
App_DrainAudio(
    struct App* app,
    struct ToriRS_AudioCommand* out,
    int max)
{
    assert(app);
    return ToriRS_AudioQueue_Drain(&app->audio_out, out, max);
}

void
App_DrainCommands(
    struct App* app,
    struct ToriRS_CmdBus* bus,
    struct LibToriRS_Input* input)
{
    struct ToriRS_CmdHeader header;
    static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];

    assert(app);
    assert(bus);
    assert(input);

    while( CmdBus_Pop(bus, &header, payload) )
    {
        if( ToriRS_Input_ApplyCmd(input, &header, payload) )
            continue;

        switch( header.type )
        {
        case TORIRS_CMD_FRAME:
            break; /* record/replay delimiter only */
        case TORIRS_CMD_NET_CONNECT:
        case TORIRS_CMD_NET_RECV:
        case TORIRS_CMD_NET_STATUS:
            if( app->net )
                ToriRS_Network_HandleCmd(app->net, header.type, payload, header.length);
            break;
        default:
            fprintf(stderr, "cmdbus: unhandled command type %u\n", header.type);
            break;
        }
    }
}

int
App_RunOnce(
    struct App* app,
    uint64_t now_ms,
    struct LibToriRS_Input* input)
{
    struct UIInteractOut out;
    int ran_cs2 = 0;

    assert(app);
    assert(input);

    /* Pump the async pipeline (bounded — never a blocking drain). While
     * BOOTING the budget is generous so a native boot converges in a few
     * frames; once READY a small budget keeps frame pacing. */
    {
        int budget = app->app_state == APP_STATE_BOOTING ? 512 : 32;
        enum TaskRunnerStat stat = TASK_RUNNER_IDLE;
        for( int i = 0; i < budget; i++ )
        {
            stat = TaskRunner_Step(&app->runner);
            if( stat == TASK_RUNNER_IDLE )
                break;
        }
        /* Tree-affecting async work (CS2 hooks/transmits) finished: refresh. */
        if( app->runner_had_work && stat == TASK_RUNNER_IDLE )
        {
            app->runner_had_work = 0;
            app->pending_tree_refresh = 1;
        }
    }

    if( app->app_state == APP_STATE_BOOTING )
    {
        /* Loading screen frame; no logic/interaction until the tree exists. */
        app->last_logic_ms = now_ms;
        app->need_redraw = 0;
        return 1;
    }

    /* Async completions: world load finish, texture publish, deferred seq
     * binds, and any queued tree refresh (relayout + CS1 + redraw). */
    app_async_polls(app);

    /* Logic ticks at 20ms with bounded catch-up after a stall. */
    if( app->last_logic_ms == 0 )
        app->last_logic_ms = now_ms;
    {
        int ticks = (int)((now_ms - app->last_logic_ms) / APP_LOGIC_TICK_MS);
        if( ticks > 0 )
        {
            app->last_logic_ms += (uint64_t)ticks * APP_LOGIC_TICK_MS;
            if( ticks > APP_MAX_CATCHUP_TICKS )
                ticks = APP_MAX_CATCHUP_TICKS;
            for( int t = 0; t < ticks; t++ )
            {
                if( app_logic_tick(app) )
                {
                    app->need_redraw = 1;
                    ran_cs2 = 1;
                }
            }
        }
        else
        {
            ticks = 0;
        }

        /* World sim cycles == client 20ms ticks (v1 world_cycle cadence). */
        app_world_frame(app, ticks);
    }

    /* Per-frame interaction: returns intents; the app applies event context
     * and dispatches each hook through the game layer. */
    /* Publish this frame's key state before any hook runs, so KEYHELD and
     * KEYPRESSED answer about the frame the script is reacting to. */
    RS_CS2_SyncKeyState(&app->host, input);
    RS_CS2_SyncMouseState(&app->host, input);

    UITree_InteractFrame(&app->interact, app->tree, &app->ui_host, input, now_ms, &out);

    /* World hover: gate on the mouse being over the world element. The pick
     * itself runs inside App_Render (hittest right after each visible model
     * projects), so here we only latch the mouse point the next render picks
     * at; hover tile and pickset are the last rendered frame's (world frames
     * always mark need_redraw, so at most one frame stale). */
    app_update_world_viewport(app);
    /* A viewport that only appeared now (mounted interface, unhidden layer)
     * pulls the map in on first sight — the map is loaded iff the tree has a
     * world element, never eagerly. Networked boots wait for the server's
     * REBUILD_NORMAL instead (a default region load would race/clobber it). */
    if( app->world_view_valid && !app->world_load_attempted && !app->net_enabled )
        app_world_load_begin(app, NULL, 0);
    app->world_mouse_in_viewport =
        app_world_mouse_gate(app, input->curr.mouse_x, input->curr.mouse_y);
    app->world_mouse_x = input->curr.mouse_x;
    app->world_mouse_y = input->curr.mouse_y;
    if( !app->world_mouse_in_viewport )
    {
        app->world_hover_tile_x = -1;
        app->world_hover_tile_z = -1;
        World_PickSetReset(&app->world_pickset);
    }

    /* Mouseover text before any click handling: the reference recomputes it
     * every cycle from the same menu the click paths build. */
    app_hover_text_update(app, input->curr.mouse_x, input->curr.mouse_y);

    /* Minimenu gesture results (see interact_minimenu): option selected on
     * mousedown -> dispatch; right press with no menu open -> build + show. */
    if( out.minimenu_select >= 0 )
    {
        if( app_minimenu_use_option(
                app, out.minimenu_select, input->curr.mouse_x, input->curr.mouse_y) )
            ran_cs2 = 1;
    }
    if( out.right_click )
    {
        /* The menu ctx reads app->world_pickset — the set the last rendered
         * frame hittested at the hover point (v1-style pickset-during-draw). */
        int click_in_world =
            app_world_mouse_gate(app, out.right_click_x, out.right_click_y);
        if( !click_in_world || !app_world_drawable(app) )
            World_PickSetReset(&app->world_pickset);
        app_minimenu_open(app, out.right_click_x, out.right_click_y, click_in_world);
    }

    /* Left click executes the DEFAULT menu entry (reference
     * chooseDefaultMenuEntry): build the same menu the right click would show
     * and run its top normal-priority row, suppressing the legacy click
     * intent. Components with no menu rows keep the legacy hook path — for
     * them the scratch menu is Cancel-only and default_idx is -1. */
    /* Minimap click-to-walk (chrome gesture from interact_click). */
    if( out.minimap_click && !out.minimenu_closed && out.minimenu_select < 0 )
        app_minimap_click(
            app,
            out.minimap_click_x,
            out.minimap_click_y,
            LibToriRS_Input_IsKeyHeld(input, TORIRSK_CTRL));

    /* A left press over a filled inventory slot is owned by the slot machine
     * (app_inv_drag_tick): the reference freezes mouseLoop while objDragArea
     * is armed, so the release must not ALSO fire the generic default-entry
     * path here — the machine runs the default row itself on a short click. */
    if( app->inv_drag_com_id < 0 && out.clicked_com_id >= 0 && !out.minimenu_closed &&
        out.minimenu_select < 0 )
    {
        struct RS_MinimenuBuildCtx mctx = {
            .tree = app->tree,
            .ui_host = &app->ui_host,
            .provider = app->provider,
            .runner = &app->runner,
            .invs = &app->invs,
            .chat = &app->chat_source,
            .world = app->world,
            .world_pickset = NULL, /* UI hit: mouse was over a component */
            .click_in_world = false,
            /* Honour an armed "Use"/spell selection so the left-click default
             * row matches the right-click menu (reference doAction runs the
             * same chooseDefaultMenuEntry over the useMode/targetMode menu). */
            .selection = app_minimenu_selection(app),
        };
        struct UIMinimenu scratch;
        int default_idx;

        UIMinimenu_Reset(&scratch);
        scratch.font_id = app->interact.minimenu.font_id;
        RS_Minimenu_Build(&mctx, out.clicked_x, out.clicked_y, &scratch);
        default_idx = RS_Minimenu_DefaultOptionIndex(&scratch);
        if( default_idx >= 0 )
        {
            /* Steal the row set: use_option consumes interact.minimenu. */
            struct UIMinimenu saved = app->interact.minimenu;
            app->interact.minimenu = scratch;
            if( app_minimenu_use_option(app, default_idx, out.clicked_x, out.clicked_y) )
                ran_cs2 = 1;
            app->interact.minimenu = saved;

            /* Drop the legacy click intent (the only intent kind carrying
             * neither event-mouse nor drag context) so the hook does not run
             * twice; hover/wheel/hold intents pass through untouched. */
            {
                int kept = 0;
                for( int i = 0; i < out.intent_count; i++ )
                {
                    struct UIIntent const* intent = &out.intents[i];
                    if( !intent->has_event_mouse && !intent->has_drag_target &&
                        (intent->component_id == out.clicked_com_id || intent->component_id < 0) )
                        continue;
                    out.intents[kept++] = out.intents[i];
                }
                out.intent_count = kept;
            }
        }
        else if( app->objsel.active || app->targetsel.active )
        {
            /* The click landed on a component that offers no menu row (an empty
             * inventory slot, sidebar chrome — the general hit test resolves
             * these to the pass-through RS_INV/panel, so clicked_com_id is set
             * but the scratch menu is Cancel-only, default_idx < 0). The
             * reference still runs doAction on that Cancel row and its tail
             * (Client.ts:9506) clears useMode/targetMode; torirs's
             * DefaultOptionIndex returns -1 for a Cancel-only menu, so nothing
             * ran. Drop the armed selection here — clicking off any surface that
             * can't be a "use" target cancels and clears the white outline. */
            app->objsel.active = 0;
            app->targetsel.active = 0;
            app->need_redraw = 1;
        }
    }

    /* Left click over bare world (no UI component hit): run the default menu
     * entry from world rows only — Walk here / nearest entity op (reference
     * chooseDefaultMenuEntry over the last rendered frame's pickset). */
    if( getenv("TORIRS_NET_DEBUG") && (out.left_click_miss || out.clicked_com_id >= 0) )
        fprintf(
            stderr,
            "click: miss=%d (%d,%d) com=0x%x gate=%d drawable=%d picks=%d\n",
            out.left_click_miss,
            out.left_click_miss_x,
            out.left_click_miss_y,
            out.clicked_com_id,
            out.left_click_miss ? app_world_mouse_gate(
                                      app,
                                      out.left_click_miss_x,
                                      out.left_click_miss_y)
                                : -1,
            app_world_drawable(app),
            app->world_pickset.count);
    if( app->inv_drag_com_id < 0 && out.left_click_miss && !out.minimenu_closed &&
        out.minimenu_select < 0 &&
        app_world_mouse_gate(app, out.left_click_miss_x, out.left_click_miss_y) &&
        app_world_drawable(app) )
    {
        struct RS_MinimenuBuildCtx mctx = {
            .tree = app->tree,
            .ui_host = &app->ui_host,
            .provider = app->provider,
            .runner = &app->runner,
            .invs = &app->invs,
            .chat = &app->chat_source,
            .world = app->world,
            .world_pickset = &app->world_pickset,
            .click_in_world = true,
            /* Honour an armed "Use"/spell selection so a left-click on a world
             * target casts/uses (the TGT and USEHELD_ON rows) instead of
             * falling back to the target's default op. Without this a left-click
             * on an NPC with a spell armed built the plain ops and defaulted to
             * Attack (walk-to-melee "run up"), while the right-click menu cast. */
            .selection = app_minimenu_selection(app),
        };
        struct UIMinimenu scratch;
        int default_idx;

        UIMinimenu_Reset(&scratch);
        scratch.font_id = app->interact.minimenu.font_id;
        RS_Minimenu_Build(&mctx, out.left_click_miss_x, out.left_click_miss_y, &scratch);
        default_idx = RS_Minimenu_DefaultOptionIndex(&scratch);
        if( default_idx >= 0 )
        {
            struct UIMinimenu saved = app->interact.minimenu;
            app->interact.minimenu = scratch;
            if( app_minimenu_use_option(
                    app, default_idx, out.left_click_miss_x, out.left_click_miss_y) )
                ran_cs2 = 1;
            app->interact.minimenu = saved;
        }
        else if( app->objsel.active || app->targetsel.active )
        {
            /* A world click with a use/spell mode armed but no valid target:
             * "Walk here" is suppressed while armed (rs_minimenu_world.c), so
             * the scratch menu is Cancel-only and default_idx < 0 — nothing
             * ran. The reference still runs doAction on that Cancel row, whose
             * tail (Client.ts:9506) clears useMode/targetMode. Without this the
             * selection stays armed forever: Walk here never returns, so every
             * later world click is also inert and the world reads as
             * "unclickable". Drop the armed selection and its white outline. */
            app->objsel.active = 0;
            app->targetsel.active = 0;
            app->need_redraw = 1;
        }
    }

    /* Left click on empty, non-world space — an inventory gap, the sidebar
     * chrome, the chat area — hits no component (RS_INV is pass-through, §21.4)
     * and no world default row runs, so none of the doAction paths above fire.
     * The reference still runs doAction there on a Cancel-only menu, whose tail
     * (Client.ts:9506) clears useMode/targetMode. Mirror just that: a plain left
     * click off anything that can be a "use" target drops the armed selection
     * and its white outline. (A world miss with a default row is consumed above
     * and already cleared; a filled slot is owned by the drag machine.) */
    if( app->inv_drag_com_id < 0 && out.left_click_miss && !out.minimenu_closed &&
        out.minimenu_select < 0 && (app->objsel.active || app->targetsel.active) &&
        !(app_world_mouse_gate(
              app, out.left_click_miss_x, out.left_click_miss_y) &&
          app_world_drawable(app)) )
    {
        app->objsel.active = 0;
        app->targetsel.active = 0;
        app->need_redraw = 1;
    }

    app->hover_com_id = out.hover_com_id;
    if( out.clicked_com_id >= 0 )
        app->clicked_com_id = out.clicked_com_id;
    if( out.need_redraw )
        app->need_redraw = 1;

    /* Snapshot hooks by value before dispatching anything: intent->hook points
     * into tree->components[], and an earlier intent's script can CC_CREATE
     * (realloc) or CC_DELETEALL (reclaim/reuse the slot), dangling the pointer. */
    {
        struct UITreeRuntimeScriptHook hook_copies[UI_INTENT_MAX];
        for( int i = 0; i < out.intent_count; i++ )
            if( out.intents[i].hook )
                hook_copies[i] = *out.intents[i].hook;

        for( int i = 0; i < out.intent_count; i++ )
        {
            struct UIIntent const* intent = &out.intents[i];
            /* Set the op index explicitly per intent rather than relying on the
             * host default, so one intent's op cannot leak into the next.
             * Unset (0) means the primary left-click op, which is what every
             * mouse-driven dispatch reports; op-key matches carry their own. */
            RS_CS2_SetEventOp(&app->host, intent->op_index > 0 ? intent->op_index : 1, 0);
            if( intent->has_event_mouse )
                RS_CS2_SetEventMouse(&app->host, intent->event_mouse_x, intent->event_mouse_y);
            if( intent->has_drag_target )
                RS_CS2_SetEventDragTarget(&app->host, app->tree, intent->drag_target_id);
            RS_CS2_DispatchHook(
                &app->host,
                &app->runner,
                intent->component_id,
                intent->hook ? &hook_copies[i] : NULL);
            ran_cs2 = 1;
        }
    }

    /* Keyboard broadcast: every event this frame times every visible onKey
     * handler (reference OsrsClient key dispatch). Unlike the intent loop above
     * this re-resolves each component id immediately before dispatching it
     * rather than snapshotting hooks up front -- a broadcast runs many scripts
     * in one frame, and an earlier one can CC_CREATE (realloc components[]) or
     * CC_DELETEALL (reclaim the slot), so a target collected during the scan may
     * be gone by its turn. Same reasoning as the on_timer loop. */
    for( int e = 0; e < out.key_event_count; e++ )
    {
        for( int t = 0; t < out.key_target_count; t++ )
        {
            struct UIKeyTarget const* target = &out.key_targets[t];
            int32_t idx = UITree_FindByComponentId(app->tree, target->component_id);
            if( idx < 0 )
                continue;
            /* Re-check the hook too: the id may have been reclaimed and handed
             * to a different node since collection. */
            if( app->tree->components[idx].runtime_hooks.on_key.script_id <= 0 )
                continue;
            RS_CS2_SetEventMouse(
                &app->host, out.key_mouse_x - target->abs_x, out.key_mouse_y - target->abs_y);
            RS_CS2_SetEventKey(
                &app->host, out.key_events[e].key_typed, out.key_events[e].key_pressed);
            if( getenv("TORIRS_KEY_DEBUG") )
                fprintf(
                    stderr,
                    "key_dispatch: com=0x%08x script=%d typed=%d pressed=%d\n",
                    target->component_id,
                    app->tree->components[idx].runtime_hooks.on_key.script_id,
                    out.key_events[e].key_typed,
                    out.key_events[e].key_pressed);
            RS_CS2_DispatchHook(
                &app->host,
                &app->runner,
                target->component_id,
                &app->tree->components[idx].runtime_hooks.on_key);
            ran_cs2 = 1;
        }
    }

    /* Chat input: typed characters/backspace/return feed whichever chat
     * input line is open (reference handleInputKey — typing goes to the chat
     * line even while op-key bindings also fire). Only when a chat region
     * exists (dat1 gameframe). */
    if( app->slots.chat_index >= 0 )
    {
        /* Chat input focus: a left press inside the chat region focuses it, a
         * press anywhere else unfocuses. Only while focused do typed keys feed
         * the chat line -- otherwise they are left for the debug camera/world
         * hotkeys. Modal prompts (add-friend name, amount dialog) always
         * capture regardless of focus. */
        if( LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) )
            app->chat_input_active =
                app_point_in_chat(app, input->curr.mouse_x, input->curr.mouse_y);

        int chat_captures = app->chat_input_active || app->chat.social_input_open ||
                            app->chat.dialog_input_open;

        /* Straight from the input queue: out.key_events only fills when some
         * component carries an onKey hook, but chat typing must work without
         * any (the dat1 packs have none). A public-chat message submitted this
         * frame (input line empties, a new PUBLIC message from us appears at
         * the front) is forwarded to the server. */
        for( int e = 0; e < input->key_event_count; e++ )
        {
            /* Escape releases chat focus (reference: Esc cancels the input
             * line) without feeding the key to the chat handler. */
            if( app->chat_input_active &&
                input->key_events[e].key_typed == TORIRS_OSRSKEY_ESCAPE )
            {
                app->chat_input_active = 0;
                chat_captures = app->chat.social_input_open || app->chat.dialog_input_open;
                app->need_redraw = 1;
                continue;
            }
            /* Enter with the line unfocused grabs focus instead of submitting,
             * so the keyboard alone can start a message. */
            if( !chat_captures &&
                input->key_events[e].key_typed == TORIRS_OSRSKEY_ENTER )
            {
                app->chat_input_active = 1;
                chat_captures = 1;
                app->need_redraw = 1;
                continue;
            }
            if( !chat_captures )
                continue;

            int had_input = app->chat.input[0] != '\0';
            /* Snapshot the input state a Return might submit, since HandleKey
             * clears it: a public line, a "::" cheat, a social prompt, or the
             * count/amount dialog. */
            char input_copy[sizeof(app->chat.input)];
            char social_copy[sizeof(app->chat.social_input)];
            char dialog_copy[sizeof(app->chat.dialog_input)];
            int was_social = app->chat.social_input_open;
            int social_type = app->chat.social_input_type;
            int was_dialog = app->chat.dialog_input_open;
            snprintf(input_copy, sizeof(input_copy), "%s", app->chat.input);
            snprintf(social_copy, sizeof(social_copy), "%s", app->chat.social_input);
            snprintf(dialog_copy, sizeof(dialog_copy), "%s", app->chat.dialog_input);

            if( RS_Chat_HandleKey(
                    &app->chat,
                    &app->social,
                    input->key_events[e].key_typed,
                    input->key_events[e].key_pressed) )
            {
                app->need_redraw = 1;

                /* Count/amount dialog submitted (was open, now closed). */
                if( was_dialog && !app->chat.dialog_input_open && dialog_copy[0] )
                    APP_NET_SEND(
                        app,
                        net_out_resume_countdialog(
                            app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                            (int)atol(dialog_copy)));
                /* Social prompt submitted: the local store op already ran in
                 * HandleKey; also notify the friend server. */
                else if( was_social && !app->chat.social_input_open && social_copy[0] )
                {
                    int64_t name37 = (int64_t)strtobase37(social_copy);
                    switch( social_type )
                    {
                    case RS_CHAT_SOCIAL_ADD_FRIEND:
                        APP_NET_SEND(app, net_out_friendlist_add(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), name37));
                        break;
                    case RS_CHAT_SOCIAL_DEL_FRIEND:
                        APP_NET_SEND(app, net_out_friendlist_del(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), name37));
                        break;
                    case RS_CHAT_SOCIAL_ADD_IGNORE:
                        APP_NET_SEND(app, net_out_ignorelist_add(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), name37));
                        break;
                    case RS_CHAT_SOCIAL_DEL_IGNORE:
                        APP_NET_SEND(app, net_out_ignorelist_del(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), name37));
                        break;
                    default:
                        break;
                    }
                }
                /* Public chat line submitted (had text, now cleared). "::" is
                 * a client-cheat command (reference), not a public message. */
                else if( had_input && app->chat.input[0] == '\0' && input_copy[0] )
                {
                    if( input_copy[0] == ':' && input_copy[1] == ':' )
                        APP_NET_SEND(
                            app,
                            net_out_client_cheat(
                                app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                                input_copy + 2));
                    else if( app->chat.message_count > 0 &&
                             app->chat.messages[0].type == RS_CHAT_TYPE_PUBLIC )
                    {
                        APP_NET_SEND(
                            app,
                            net_out_message_public(
                                app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf),
                                app->chat.messages[0].text));

                        /* Reference sets localPlayer.chatMessage on submit
                         * (Client.ts:3405) so our own overhead line shows
                         * immediately, before the server echoes it back through
                         * PLAYER_INFO. colour/effect default to 0/0 (no chat
                         * style selector ported yet). */
                        {
                            int local_idx = -1;
                            if( app->world &&
                                RS_EntitySync_FindPlayer(
                                    &app->esync,
                                    app->esync.local_pid >= 0 ? app->esync.local_pid : 2047,
                                    &local_idx,
                                    NULL) )
                                World_PlayerSetChat(
                                    app->world, local_idx, app->chat.messages[0].text, 0, 0);
                        }
                    }
                }
            }
        }

        /* Chat scrollbar: held left button over the scrollbar column drives the
         * arrows / grip (reference doScrollbar, gated on no chat dialog open —
         * the dialog pack replaces the message column + scrollbar). */
        {
            int rx = 0;
            int ry = 0;
            int left_held = LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT);
            if( left_held && app->slots.chat_com_id == -1 &&
                app_chat_region(app, &rx, &ry, NULL) )
            {
                struct RS_ChatFilters filters = app_chat_filters(app);
                app->chat_scroll_cycle++;
                if( RS_Chat_ScrollbarInput(
                        &app->chat,
                        &filters,
                        input->curr.mouse_x - rx,
                        input->curr.mouse_y - ry,
                        app->chat_scroll_cycle) )
                    app->need_redraw = 1;
            }
            else
            {
                app->chat_scroll_cycle = 0;
                app->chat.scroll_grabbed = 0;
            }
        }

        /* Wheel over the chat region scrolls the message history. */
        if( input->curr.mouse_wheel_y != 0 )
        {
            int rx = 0;
            int ry = 0;
            if( app_chat_region(app, &rx, &ry, NULL) )
            {
                struct UITreeComponent const* node =
                    &app->tree->components[app->slots.chat_index];
                int bx = 0, by = 0, bw = 0, bh = 0;
                UITree_LayoutGetBounds(&node->position, &bx, &by, &bw, &bh);
                if( input->curr.mouse_x >= bx && input->curr.mouse_x < bx + bw &&
                    input->curr.mouse_y >= by && input->curr.mouse_y < by + bh )
                {
                    struct RS_ChatFilters filters = app_chat_filters(app);
                    RS_Chat_Scroll(&app->chat, &filters, input->curr.mouse_wheel_y);
                    app->need_redraw = 1;
                }
            }
        }
    }

    /* Click handlers can unhide tabs; pump immediately so the freshly visible
     * widgets populate this frame instead of one tick later. Early-outs when
     * nothing was unhidden. */
    if( out.intent_count > 0 || out.key_target_count > 0 )
        RS_CS2_PumpTransmits(&app->host, &app->runner);

    app_world_camera_keys(app, input, &out);
    app_world_hotkeys(app, input, &out);
    app_inv_drag_tick(app, input);

    /* Idle timer (reference IDLE_TIMER after ~90s of no input). */
    if( input->key_event_count > 0 || input->curr.mouse_button_down[TORIRSM_LEFT] ||
        input->curr.mouse_button_down[TORIRSM_RIGHT] )
    {
        app->idle_frames = 0;
        app->idle_timer_sent = 0;
    }
    else if( ++app->idle_frames > 4500 && !app->idle_timer_sent )
    {
        app->idle_timer_sent = 1;
        APP_NET_SEND(
            app, net_out_idle_timer(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf)));
    }

    if( ran_cs2 )
    {
        UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        /* The dispatched scripts run asynchronously; refresh the tree again
         * when the runner queue next drains so late mutations land. */
        app->runner_had_work = 1;
    }

    if( app->need_redraw )
    {
        /* Mounts/bakes bump tree->generation; server texts that landed while
         * the target interface was unmounted re-apply onto the fresh nodes
         * (reference: IF_SETTEXT persists on IfType.list). */
        if( app->if_text_count > 0 && app->tree->generation != app->if_text_applied_gen )
        {
            app->if_text_applied_gen = app->tree->generation;
            for( int i = 0; i < app->if_text_count; i++ )
            {
                bool ok =
                    UITree_ApplyText(app->tree, app->if_texts[i].com_id, app->if_texts[i].text);
                if( !ok && getenv("TORIRS_NET_DEBUG") )
                    fprintf(
                        stderr,
                        "if_settext: reapply com=%d gen=%u missed\n",
                        app->if_texts[i].com_id,
                        app->tree->generation);
            }
        }
        /* Rebind chatheads onto their (possibly newly mounted) MODEL nodes. */
        app_if_head_poll(app);
        app_chat_build_view(app);
        /* TORIRS_FORCE_SHOW_SLOT=<component_id> (debug): clear the hide flag on one
         * mounted node each frame so a panel the gameframe keeps hidden can still be
         * rendered for inspection — the sidebar tab-reveal CS2 is still a follow-on,
         * so e.g. the magic tab (161|82 = 0x00a10052) is otherwise never visible.
         * Only this node's own flag is forced; the subtree's own hide state stands. */
        {
            char const* force_show = getenv("TORIRS_FORCE_SHOW_SLOT");
            if( force_show )
            {
                int want = (int)strtol(force_show, NULL, 0);
                int32_t idx = UITree_FindByComponentId(app->tree, want);
                if( idx >= 0 )
                    app->tree->components[idx].behavior.hide = 0;
            }
        }
        app->emit.count = 0;
        UITree_EmitWalk(app->tree, &app->ui_host, &app->emit, app->hover_com_id);
        app->need_redraw = 0;
        return 1;
    }
    return 0;
}

void
App_IfTextSet(
    struct App* app,
    int com_id,
    char const* text)
{
    int i;
    assert(app);
    for( i = 0; i < app->if_text_count; i++ )
        if( app->if_texts[i].com_id == com_id )
            break;
    if( i == app->if_text_count )
    {
        if( app->if_text_count == app->if_text_cap )
        {
            int cap = app->if_text_cap ? app->if_text_cap * 2 : 64;
            app->if_texts = realloc(app->if_texts, (size_t)cap * sizeof(*app->if_texts));
            assert(app->if_texts);
            app->if_text_cap = cap;
        }
        app->if_texts[i].com_id = com_id;
        app->if_texts[i].text = NULL;
        app->if_text_count++;
    }
    free(app->if_texts[i].text);
    app->if_texts[i].text = strdup(text ? text : "");
    {
        bool applied = UITree_ApplyText(app->tree, com_id, text);
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "if_settext: com=%d text='%s' applied=%d\n",
                com_id,
                text ? text : "",
                (int)applied);
    }
    app->need_redraw = 1;
}

static void
app_send_if_button(void* user, int com_id)
{
    struct App* app = (struct App*)user;
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(stderr, "if_button: com=%d\n", com_id);
    APP_NET_SEND(app, net_out_if_button(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), com_id));
}

static void
app_send_resume_pausebutton(void* user, int com_id)
{
    struct App* app = (struct App*)user;
    APP_NET_SEND(
        app, net_out_resume_pausebutton(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), com_id));
}

static void
app_send_close_modal(void* user)
{
    struct App* app = (struct App*)user;
    APP_NET_SEND(app, net_out_close_modal(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf)));
}

/* Server ack after a REBUILD_NORMAL-driven world load finishes. */
static void
app_send_map_build_complete(struct App* app)
{
    APP_NET_SEND(
        app, net_out_map_build_complete(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf)));
}

/* Request a sequence load once (deduped through the entity seq tracker). */
static void
app_request_entity_seq(
    struct App* app,
    int seq_id)
{
    struct ToriRS_Task* task;

    if( seq_id < 0 || ToriDraw_SceneAnimationHas(app->scene, seq_id) )
        return;
    for( int i = 0; i < app->entity_seq_loads.count; i++ )
        if( app->entity_seq_loads.seq_ids[i] == seq_id )
            return;
    if( app->entity_seq_loads.count <
        (int)(sizeof(app->entity_seq_loads.seq_ids) / sizeof(app->entity_seq_loads.seq_ids[0])) )
        app->entity_seq_loads.seq_ids[app->entity_seq_loads.count++] = seq_id;
    task = CreateTask_SequenceLoad(app->provider, app->scene, seq_id);
    if( task )
        ToriRS_TaskQueue_Add(app->runner.queue, task);
}

/* Bind one entity's World animation state onto its scene element (reference
 * getTempModel2 selection: primary when playing and undelayed — with the
 * secondary bound alongside for the walkmerge blend when it is a real walk —
 * else the secondary alone). */
static void
app_world_apply_entity_anim_tracks(
    struct App* app,
    int element_id,
    struct WorldEntityFacet_Animation const* anim,
    struct WorldEntityFacet_IdleAnimations const* idle)
{
    struct ToriDraw_SceneElement* el;
    int primary_active = anim->primary.anim_id != (uint16_t)-1 && anim->primary.anim_id != 0;
    int secondary_active = anim->secondary.anim_id != (uint16_t)-1 && anim->secondary.anim_id != 0;

    if( element_id < 0 || !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return;
    el = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !el )
        return;

    if( primary_active )
        app_request_entity_seq(app, anim->primary.anim_id);
    if( secondary_active )
        app_request_entity_seq(app, anim->secondary.anim_id);

    if( primary_active && anim->primary.delay == 0 )
    {
        struct ToriDraw_Animation* pa =
            ToriDraw_SceneAnimationGet(app->scene, anim->primary.anim_id);
        if( app_anim_playable(pa) )
        {
            app_element_set_anim(el, pa);
            el->anim_seq_id = anim->primary.anim_id;
            el->anim_frame = anim->primary.frame < pa->frame_count ? anim->primary.frame : 0;
            /* The walkmerge blend is a frame-animator operation (it masks
             * transform groups), so a skeletal primary never takes a secondary. */
            if( !pa->skeletal && secondary_active && idle &&
                anim->secondary.anim_id != (uint16_t)idle->readyanim )
            {
                struct ToriDraw_Animation* sa =
                    ToriDraw_SceneAnimationGet(app->scene, anim->secondary.anim_id);
                if( sa && sa->frame_count > 0 && sa->frames )
                {
                    el->secondary_animation = sa;
                    el->anim2_seq_id = anim->secondary.anim_id;
                    el->anim2_frame =
                        anim->secondary.frame < sa->frame_count ? anim->secondary.frame : 0;
                    return;
                }
            }
            el->secondary_animation = NULL;
            el->anim2_seq_id = -1;
            return;
        }
    }

    el->secondary_animation = NULL;
    el->anim2_seq_id = -1;
    if( secondary_active )
    {
        struct ToriDraw_Animation* sa =
            ToriDraw_SceneAnimationGet(app->scene, anim->secondary.anim_id);
        if( app_anim_playable(sa) )
        {
            app_element_set_anim(el, sa);
            el->anim_seq_id = anim->secondary.anim_id;
            el->anim_frame = anim->secondary.frame < sa->frame_count ? anim->secondary.frame : 0;
            return;
        }
    }

    app_element_set_anim(el, NULL);
    el->anim_seq_id = -1;
    el->anim_frame = 0;
}

/* Held-item hiding/replacement (players only — NPCs render npctype models with
 * no worn slots). Reference ClientPlayer.getSequencedModel: while the PRIMARY
 * seq is actually driving frames (delay 0), its replaceheldleft/right override
 * the left-hand (appearance slot 5) / right-hand (slot 3) worn item before the
 * model is composited. A value >= 0 but below the obj range (< 0x200) draws no
 * model there, i.e. the held item is hidden (e.g. many emotes drop the weapon
 * and shield); a value >= 0x200 swaps in a different obj. The appearance model
 * is built once and cached on the scene element, so rebuild it only when the
 * effective override changes (anim start/stop), keyed by held_*_applied. */
static void
app_set_player_element_model(
    struct App* app,
    int element_id,
    int const slots[12],
    int const colors[5],
    int gender);

static void
app_world_apply_player_held_items(struct App* app, struct WorldEntity_Player* player)
{
    struct WorldEntityFacet_Animation const* anim = &player->animation;
    int want_left = -1;
    int want_right = -1;

    if( anim->primary.anim_id != (uint16_t)-1 && anim->primary.anim_id != 0 &&
        anim->primary.delay == 0 )
    {
        struct ToriDraw_Animation* prim =
            ToriDraw_SceneAnimationGet(app->scene, anim->primary.anim_id);
        if( prim && prim->frame_count > 0 )
        {
            if( prim->replaceheldleft >= 0 )
                want_left = prim->replaceheldleft;
            if( prim->replaceheldright >= 0 )
                want_right = prim->replaceheldright;
        }
    }

    if( want_left == player->held_left_applied && want_right == player->held_right_applied )
        return;

    player->held_left_applied = want_left;
    player->held_right_applied = want_right;

    {
        int slots[12];
        memcpy(slots, player->appearance.slots, sizeof(slots));
        if( want_right >= 0 )
            slots[3] = want_right;
        if( want_left >= 0 )
            slots[5] = want_left;
        app_set_player_element_model(
            app, player->element_id, slots, player->appearance.colors, player->gender);
    }
}

/* Push World animation state to the entity scene elements each frame (the
 * per-element modulo tick skips anim_external elements). */
static void
app_world_sync_entity_animations(struct App* app)
{
    struct World_EntityPool* pool;

    pool = &app->world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        if( player )
        {
            app_world_apply_entity_anim_tracks(
                app, player->element_id, &player->animation, &player->idle_animations);
            app_world_apply_player_held_items(app, player);
        }
    }

    pool = &app->world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        if( npc )
            app_world_apply_entity_anim_tracks(
                app, npc->element_id, &npc->animation, &npc->idle_animations);
    }
}

/* ---- Entity attached-graphic (SPOTANIM mask): reference-accurate per-frame
 * Model.combine (ClientNpc/ClientPlayer.getTempModel). While an entity's
 * graphic is active its scene element's model is merge(body, spot): the body
 * part keeps its bones so the element's bound seq keeps animating it live in
 * the renderer, the spot part is pre-posed to the world-stepped spot frame
 * with its bones cleared (reference temp.labelFaces/labelVertices = null, so
 * the body seq cannot drive spot vertices) and raised by the wire height.
 * Assets load once through the async task pipeline; with both models resident
 * the combine itself is synchronous. Re-merged only when the spot frame
 * changes — between merges the visual is identical because the renderer poses
 * the body part live and the spot pose only advances with spot->frame. State
 * lives in app->entity_spotanims keyed by the body element id. ---- */

static struct AppEntitySpotanim*
app_entity_spotanim_find(struct App* app, int body_element_id)
{
    int count = (int)(sizeof(app->entity_spotanims) / sizeof(app->entity_spotanims[0]));
    for( int i = 0; i < count; i++ )
        if( app->entity_spotanims[i].body_element_id == body_element_id )
            return &app->entity_spotanims[i];
    return NULL;
}

/* End the combine: restore the entity's own model (ownership of the pristine
 * body snapshot moves back to the element) and free the spot base. `restore`
 * is false when the body element is already gone (despawn/scene teardown). */
static void
app_entity_spotanim_detach(struct App* app, struct AppEntitySpotanim* entry, bool restore)
{
    if( restore && entry->body &&
        ToriDraw_SceneElementIsLive(app->scene, entry->body_element_id) )
    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = entry->body;
        /* The renderer's per-frame AnimateReset needs captured originals. */
        ToriDraw_ModelCaptureOriginalVertices(entry->body);
        ToriDraw_SceneElementSetModel(app->scene, entry->body_element_id, hnd);
        entry->body = NULL; /* ownership moved to the element */
        app->need_redraw = 1;
    }
    if( entry->body )
        ToriDraw_ModelFree(entry->body);
    if( entry->spot )
        ToriDraw_ModelFree(entry->spot);
    *entry = (struct AppEntitySpotanim){ .body_element_id = -1 };
}

/* EntityRemoved drain hook: the entity element (and with it the combined
 * model) is going away — free the snapshots without touching the element. */
static void
app_entity_spotanim_drop(struct App* app, int body_element_id)
{
    struct AppEntitySpotanim* entry = app_entity_spotanim_find(app, body_element_id);
    if( entry )
        app_entity_spotanim_detach(app, entry, false);
}

static void
app_world_sync_one_entity_spotanim(
    struct App* app,
    struct WorldEntityFacet_EntitySpotanim const* spot,
    int element_id)
{
    struct World* world = app->world;
    struct AppEntitySpotanim* entry = app_entity_spotanim_find(app, element_id);
    struct ToriRS_Spotanimtype* type;
    struct ToriDraw_Animation* anim;
    struct ToriDraw_SceneElement* el;
    int active = spot->id != -1 && world->cycle >= spot->last_cycle && spot->frame >= 0;
    int frame;
    int first_combine;

    if( !active )
    {
        if( entry )
            app_entity_spotanim_detach(app, entry, true);
        return;
    }
    /* Graphic replaced mid-flight: restore the body, rebuild for the new id. */
    if( entry && entry->spotanim_id != spot->id )
    {
        app_entity_spotanim_detach(app, entry, true);
        entry = NULL;
    }
    if( !entry )
    {
        entry = app_entity_spotanim_find(app, -1);
        if( !entry )
            return; /* table full */
        *entry = (struct AppEntitySpotanim){
            .body_element_id = element_id,
            .spotanim_id = spot->id,
            .applied_frame = -1,
        };
    }

    /* Asset gate: spotanimtype + model + seq must be resident. Kick the async
     * load chain once; the frame it lands the combine below runs synchronously
     * (reference precondition: SpotType.getTempModel2 assumes loaded). */
    type = CacheProvider_SpotanimtypeGet(app->provider, spot->id);
    anim = (type && type->seq >= 0) ? ToriDraw_SceneAnimationGet(app->scene, type->seq) : NULL;
    if( !type || !CacheProvider_ModelGet(app->provider, type->model) || !anim ||
        anim->frame_count <= 0 || !anim->frames || !anim->base )
    {
        if( !entry->load_enqueued )
        {
            struct Task_AppSpawn* task =
                app_spawn_task_new(app, APP_SPAWN_ENTITY_SPOTANIM, 0, 0, 0);
            task->spotanim_id = spot->id;
            task->entity_element_id = element_id;
            ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
            entry->load_enqueued = 1;
        }
        return;
    }

    if( !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return;
    el = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !el || el->model.kind != TORIDRAWMK_MODEL || !el->model.u.model.model )
        return;

    /* Snapshot the pristine body. Also re-snapshot when the element's model
     * changed under us — a held-item/appearance rebuild SetModel'd a fresh
     * body over our combined (`combined` is compared as an identity only,
     * never dereferenced: SetModel freed it). */
    if( !entry->body || el->model.u.model.model != entry->combined )
    {
        if( entry->body )
            ToriDraw_ModelFree(entry->body);
        /* The renderer poses the element model in place each draw; reset to
         * the rest pose so the snapshot is the true base. */
        ToriDraw_ModelAnimateReset(el->model.u.model.model);
        entry->body = ToriDraw_ModelCopy(el->model.u.model.model);
        entry->combined = NULL;
        entry->applied_frame = -1;
        if( !entry->body )
            return;
    }

    if( !entry->spot )
    {
        entry->spot = app_world_build_spotanim_model(app, type);
        if( !entry->spot )
            return;
    }

    frame = spot->frame < anim->frame_count ? spot->frame : anim->frame_count - 1;
    if( frame == entry->applied_frame && entry->combined )
        return; /* merged model already at this spot frame; body animates live */
    first_combine = entry->applied_frame < 0;

    {
        struct ToriDraw_Model* posed = ToriDraw_ModelCopy(entry->spot);
        struct ToriDraw_Model* parts[2];
        struct ToriDraw_Model* merged;
        if( !posed )
            return;
        /* Hole frames (missing archive) hold the rest pose, like the renderer. */
        if( anim->frames[frame].length > 0 )
            ToriDraw_ModelAnimateFrame(posed, anim->base, &anim->frames[frame]);
        /* Reference nulls the spot copy's labels before Model.combine. */
        ToriDraw_BonesFree(posed->vertex_bones);
        posed->vertex_bones = NULL;
        ToriDraw_BonesFree(posed->face_bones);
        posed->face_bones = NULL;
        /* Model y is negative-up: reference temp.translate(-spotanimHeight,0,0). */
        if( spot->height != 0 )
            ToriDraw_ModelTranslate(posed, 0, -spot->height, 0);

        parts[0] = entry->body;
        parts[1] = posed;
        merged = ToriDraw_ModelMerge(parts, 2);
        ToriDraw_ModelFree(posed);
        if( !merged )
            return;
        ToriDraw_ModelCaptureOriginalVertices(merged);
        {
            struct ToriDraw_ModelHandle hnd;
            memset(&hnd, 0, sizeof(hnd));
            hnd.kind = TORIDRAWMK_MODEL;
            hnd.u.model.model = merged;
            ToriDraw_SceneElementSetModel(app->scene, element_id, hnd);
        }
        entry->combined = merged;
        entry->applied_frame = frame;
        app->need_redraw = 1;
        if( first_combine && getenv("TORIRS_ANIM_DEBUG") )
            fprintf(
                stderr,
                "entity_spotanim: combine id=%d element=%d seq=%d frame=%d height=%d\n",
                spot->id,
                element_id,
                type->seq,
                frame,
                spot->height);
    }
}

static void
app_world_sync_entity_spotanims(struct App* app)
{
    struct World_EntityPool* pool;
    int count = (int)(sizeof(app->entity_spotanims) / sizeof(app->entity_spotanims[0]));

    if( !app->world )
        return;

    /* Entries whose body element died outside the event drain (scene
     * teardown): free the snapshots. */
    for( int i = 0; i < count; i++ )
    {
        struct AppEntitySpotanim* entry = &app->entity_spotanims[i];
        if( entry->body_element_id >= 0 &&
            !ToriDraw_SceneElementIsLive(app->scene, entry->body_element_id) )
            app_entity_spotanim_detach(app, entry, false);
    }

    pool = &app->world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        if( player && player->element_id >= 0 )
            app_world_sync_one_entity_spotanim(app, &player->spotanim, player->element_id);
    }

    pool = &app->world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        if( npc && npc->element_id >= 0 )
            app_world_sync_one_entity_spotanim(app, &npc->spotanim, npc->element_id);
    }
}

int
App_WorldSpawnSyncedPlayer(
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    return app_world_spawn_player_now(app, scene_x, scene_z, level);
}

int
App_WorldSpawnSyncedNpc(
    struct App* app,
    int npc_id,
    int scene_x,
    int scene_z,
    int level)
{
    return app_world_spawn_npc_now(app, npc_id, scene_x, scene_z, level);
}

/* Ground item stacks (zone OBJ_* packets). The objtype + its inventory
 * model must already be cached (the packet task awaits the loads). */
int
App_WorldObjStackAdd(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int obj_id,
    int count)
{
    struct ToriRS_Objtype* obj;
    struct ToriDraw_Model* model;
    int model_ids[1];
    int world_x = scene_x * 128 + 64;
    int world_z = scene_z * 128 + 64;
    int world_y;
    int element_id;
    int existing;

    assert(app);
    existing = World_ObjStackFind(app->world, scene_x, scene_z, level, obj_id);
    if( existing >= 0 )
    {
        World_ObjStackSetCount(app->world, existing, count);
        app->need_redraw = 1;
        return existing;
    }

    obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
    if( !obj || obj->inventory_model_id <= 0 )
        return -1;
    model_ids[0] = obj->inventory_model_id;
    {
        struct AppModelRecolorSpec recolors = {
            .recolors_from = obj->recolors_from,
            .recolors_to = obj->recolors_to,
            .recolor_count = obj->recolor_count,
        };
        model = app_world_build_model(app, model_ids, 1, &recolors, 128, 128);
    }
    if( !model )
        return -1;

    world_y = app_world_height(app, world_x, world_z, level);
    element_id = app_world_scene_element_create(app, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return -1;

    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "objstack: obj=%d tile=%d,%d,%d element=%d\n",
            obj_id,
            scene_x,
            scene_z,
            level,
            element_id);
    app_sync_textures(app);
    app->need_redraw = 1;
    {
        /* ToriRS actions are [5][64]; the entity facet stores [5][32] —
         * repack at the matching stride (same gotcha as the scenery path). */
        char actions32[5][32];
        for( int a = 0; a < 5; a++ )
            snprintf(actions32[a], sizeof(actions32[a]), "%s", obj->ground_actions[a]);
        return World_ObjStackAdd(
            app->world,
            element_id,
            scene_x,
            scene_z,
            level,
            obj_id,
            count,
            obj->name,
            actions32);
    }
}

void
App_WorldRebuildShift(
    struct App* app,
    int base_dx,
    int base_dz)
{
    struct World* world;
    struct World_EntityPool* pool;

    assert(app);
    world = app->world;
    if( !world )
        return;

    World_ShiftEntities(world, base_dx, base_dz);
    World_ClearProjectilesAndSpotanims(world);

    /* Obj stacks: Client-TS shifts the groundObj grid and nulls entries that
     * fall off it; here the surviving stacks' elements also need their world
     * position re-derived from the new scene's heightmap. Deletion releases
     * the pool node, so grab next first. */
    pool = &world->entities.obj_stack;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; )
    {
        int next = World_EntityPoolNext(pool, i);
        struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, i);
        if( stack )
        {
            int scene_x = stack->grid_position.x;
            int scene_z = stack->grid_position.z;
            if( scene_x < 0 || scene_z < 0 || scene_x >= world->_scene_size ||
                scene_z >= world->_scene_size )
                World_ObjStackDel(world, i);
            else if( stack->element_id >= 0 )
            {
                int world_x = scene_x * 128 + 64;
                int world_z = scene_z * 128 + 64;
                ToriDraw_SceneElementSetPosition(
                    app->scene,
                    stack->element_id,
                    world_x,
                    app_world_height(app, world_x, world_z, stack->grid_position.level),
                    world_z,
                    0);
            }
        }
        i = next;
    }

    /* Destination flag (reference minimapFlagX -= dx). */
    if( app->minimap_flag_x >= 0 )
    {
        app->minimap_flag_x -= base_dx;
        app->minimap_flag_z -= base_dz;
        if( app->minimap_flag_x < 0 || app->minimap_flag_z < 0 ||
            app->minimap_flag_x >= world->_scene_size ||
            app->minimap_flag_z >= world->_scene_size )
        {
            app->minimap_flag_x = -1;
            app->minimap_flag_z = -1;
        }
    }

    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(stderr, "rebuild_shift: dx=%d dz=%d\n", base_dx, base_dz);
    app->need_redraw = 1;
}

void
App_WorldObjStackDel(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int obj_id)
{
    int idx;
    assert(app);
    idx = World_ObjStackFind(app->world, scene_x, scene_z, level, obj_id);
    if( idx >= 0 )
    {
        World_ObjStackDel(app->world, idx);
        app->need_redraw = 1;
    }
}

/** LOC_ANIM: attach a sequence to the scenery element on a tile. */
void
App_WorldSceneryAnim(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_shape,
    int seq_id)
{
    int idx;
    assert(app);
    idx = World_SceneryFindAt(app->world, scene_x, scene_z, level, loc_shape);
    if( idx >= 0 )
    {
        struct WorldEntity_Scenery* scenery =
            World_EntityPoolGet(&app->world->entities.scenery, idx);
        if( scenery && scenery->element_id >= 0 )
        {
            app_world_apply_seq(app, scenery->element_id, seq_id);
            app->need_redraw = 1;
        }
    }
}

void
App_WorldApplyNpcType(
    struct App* app,
    int world_idx,
    int element_id,
    int npc_type)
{
    struct ToriRS_Npctype* npctype;
    struct ToriDraw_Model* model;

    assert(app);
    npctype = CacheProvider_NpctypeGet(app->provider, npc_type);
    if( !npctype )
        return;

    {
        struct AppModelRecolorSpec recolors = {
            .recolors_from = npctype->recolors_from,
            .recolors_to = npctype->recolors_to,
            .recolor_count = npctype->recolor_count,
            .retextures_from = npctype->retextures_from,
            .retextures_to = npctype->retextures_to,
            .retexture_count = npctype->retexture_count,
        };
        model = app_world_build_model(
            app,
            npctype->models,
            npctype->models_count,
            &recolors,
            npctype->width_scale,
            npctype->height_scale);
    }
    if( model && element_id >= 0 && ToriDraw_SceneElementIsLive(app->scene, element_id) )
    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        ToriDraw_SceneElementSetModel(app->scene, element_id, hnd);
        {
            struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
            if( el )
                el->anim_external = true;
            ToriDraw_SceneAnimListInvalidate(app->scene);
        }
    }
    else if( model )
    {
        ToriDraw_ModelFree(model);
    }

    {
        /* Reference CHANGETYPE swaps walkanim_l/r (Client.ts 8460-8462). */
        struct WorldEntityFacet_IdleAnimations idle = {
            .readyanim = npctype->readyanim,
            .walkanim = npctype->walkanim,
            .turnanim = -1,
            .runanim = -1,
            .walkanim_b = npctype->walkanim_b,
            .walkanim_r = npctype->walkanim_l,
            .walkanim_l = npctype->walkanim_r,
        };
        World_NpcSetType(
            app->world, world_idx, npc_type, npctype->size > 0 ? npctype->size : 1, &idle);
    }
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, world_idx);
        if( npc )
        {
            npc->combat_level = npctype->combat_level;
            npc->alwaysontop = npctype->alwaysontop;
            npc->facing.turn_speed = npctype->turn_speed;
            snprintf(npc->name, sizeof(npc->name), "%s", npctype->name);
            for( int i = 0; i < 5; i++ )
                snprintf(
                    npc->actions[i].name, sizeof(npc->actions[i].name), "%s", npctype->actions[i]);
        }
    }
    app_sync_textures(app);
    app->need_redraw = 1;
}

/* Build the player appearance model from slots/colors/gender and hand it to the
 * scene element (SceneElementSetModel disposes the previous model; the element's
 * animation binding survives, so the current seq keeps driving the new model).
 * Shared by the appearance packet and the per-frame held-item swap. */
static void
app_set_player_element_model(
    struct App* app,
    int element_id,
    int const slots[12],
    int const colors[5],
    int gender)
{
    struct ToriDraw_Model* model =
        PlayerModel_BuildFromAppearance(app->provider, slots, colors, gender);
    if( model && element_id >= 0 && ToriDraw_SceneElementIsLive(app->scene, element_id) )
    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        ToriDraw_SceneElementSetModel(app->scene, element_id, hnd);
    }
    else if( model )
    {
        ToriDraw_ModelFree(model);
    }
}

void
App_WorldApplyPlayerAppearance(
    struct App* app,
    int world_idx,
    int element_id,
    struct PktPlayerAppearance const* appearance)
{
    assert(app && appearance);

    app_set_player_element_model(
        app, element_id, appearance->slots, appearance->colors, appearance->gender);

    {
        struct WorldEntityFacet_IdleAnimations idle = {
            .readyanim = appearance->readyanim,
            .walkanim = appearance->walkanim,
            .turnanim = appearance->turnanim,
            .runanim = appearance->runanim,
            .walkanim_b = appearance->walkanim_b,
            .walkanim_r = appearance->walkanim_r,
            .walkanim_l = appearance->walkanim_l,
        };
        World_PlayerSetAppearance(
            app->world,
            world_idx,
            appearance->slots,
            appearance->colors,
            &idle,
            appearance->name,
            appearance->combat_level,
            appearance->gender);

        /* Overhead prayer/skull headicon bitmask (reference
         * ClientPlayer.headicons, appearance g1). SetAppearance carries no
         * headicon field, so copy it onto the entity directly for the overlay
         * pass. */
        {
            struct WorldEntity_Player* ent =
                World_EntityPoolGet(&app->world->entities.player, world_idx);
            if( ent )
            {
                ent->headicon = appearance->headicon;
                /* The element now holds the un-overridden base model; the
                 * per-frame held-item pass will rebuild if a seq demands it. */
                ent->held_left_applied = -1;
                ent->held_right_applied = -1;
            }
        }

        /* Local player's real name now known: sync it to the chatbox so the
         * public-chat local echo shows the login name instead of the default
         * "Player". The reference echoes with this.localPlayer.name
         * (Client.ts:3405-3417); the server echoes the same name through
         * PLAYER_INFO CHAT, so the two must match. */
        if( appearance->name[0] )
        {
            int local_idx = -1;
            if( RS_EntitySync_FindPlayer(
                    &app->esync,
                    app->esync.local_pid >= 0 ? app->esync.local_pid : 2047,
                    &local_idx,
                    NULL) &&
                local_idx == world_idx )
                strncpy(
                    app->chat.username,
                    appearance->name,
                    sizeof(app->chat.username) - 1);
        }
    }
    app_sync_textures(app);
    app->need_redraw = 1;
}

void
App_RefreshAfterTreeMutation(struct App* app)
{
    assert(app);
    UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    app_request_cs1_eval(app);
    app->need_redraw = 1;
}

bool
App_IsBooting(
    struct App* app,
    int* out_progress)
{
    assert(app);
    if( out_progress )
        *out_progress = app->boot_progress;
    return app->app_state == APP_STATE_BOOTING;
}

bool
App_BuildFrame(
    struct App* app,
    struct ToriRS_Frame* frame,
    int width,
    int height)
{
    assert(app);
    assert(frame);

    if( app->app_state == APP_STATE_BOOTING )
        return false;

    ToriRS_FrameInit(frame);
    ToriRS_FrameSetScene(frame, app->scene);
    ToriRS_FrameSetCanvas(frame, width, height);
    ToriRS_FrameSetEmitBuffer(frame, &app->emit);

    /* World pass: paint the visibility-ordered command list for the current
     * camera and attach it so UITREE_EMIT_WORLD opens the 3D pass. */
    if( app_world_drawable(app) )
    {
        app_world_paint(app);
        ToriRS_FrameSetWorld(
            frame,
            app->world,
            app->painter_buffer,
            &app->world_camera,
            app->world_camera_pos.x,
            app->world_camera_pos.y,
            app->world_camera_pos.z);
    }
    return true;
}

void
App_PickFinish(
    struct App* app,
    struct ToriRS_PickHits const* hits)
{
    assert(app);
    assert(hits);
    app_world_pick_finish(app, hits);
}

void
App_Render(
    struct App* app,
    int* pixels,
    int width,
    int height)
{
    struct ToriRS_Frame frame;
    struct ToriRS_Soft3D soft;

    assert(app);
    assert(pixels);

    if( !App_BuildFrame(app, &frame, width, height) )
    {
        /* Font-free loading screen: dark clear + centered progress bar.
         * Deliberately independent of every asset pipeline (they are what is
         * still loading). */
        int bar_w = width / 3;
        int bar_h = 12;
        int bar_x = (width - bar_w) / 2;
        int bar_y = (height - bar_h) / 2;
        int fill_w = bar_w * (app->boot_progress < 0 ? 0 : app->boot_progress) / 100;

        for( int i = 0; i < width * height; i++ )
            pixels[i] = 0x000000;
        for( int y = bar_y - 1; y <= bar_y + bar_h; y++ )
            for( int x = bar_x - 1; x <= bar_x + bar_w; x++ )
            {
                if( y < 0 || y >= height || x < 0 || x >= width )
                    continue;
                int border = (y < bar_y || y >= bar_y + bar_h || x < bar_x || x >= bar_x + bar_w);
                int filled = !border && (x - bar_x) < fill_w;
                pixels[y * width + x] = border ? 0x8b0000 : (filled ? 0x8b0000 : 0x000000);
            }
        return;
    }

    ToriRS_Soft3D_Init(&soft, app->scene, pixels, width, height);

    /* World hittest rides the render: each visible model is tested against
     * the mouse point right after it projects (the only window where the
     * scene scratch holds its projection), then the raw hits classify into
     * the pickset + hover tile the click/hotkey paths consume next frame. */
    if( app_world_drawable(app) && app->world_mouse_in_viewport )
        ToriRS_Soft3D_SetPick(&soft, app->world_mouse_x, app->world_mouse_y);

    ToriRS_Soft3D_RenderFrame(&soft, &frame);

    if( getenv("TORIRS_FRAME_DEBUG") )
        fprintf(
            stderr,
            "frame: draws element=%d terrain=%d dropped not_live=%d no_model=%d\n",
            frame.dbg_emit_element,
            frame.dbg_emit_terrain,
            frame.dbg_drop_not_live,
            frame.dbg_drop_no_model);

    if( soft.pick_enabled )
        App_PickFinish(app, &soft.pick_hits);
}

int
App_WriteBmp(
    struct App* app,
    char const* path,
    int width,
    int height)
{
    assert(app);
    return UITreeCmd_WriteBmp(app->scene, app->emit.cmds, app->emit.count, path, width, height);
}
