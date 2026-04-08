#ifndef GAME_H
#define GAME_H

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
#include "command_buffer.h"
#include "datastruct/ringbuf.h"
#include "game_entity.h"
#include "graphics/dash.h"
#include "osrs/buildcache.h"
#include "osrs/buildcachedat.h"
#include "osrs/clientscript_vm.h"
#include "osrs/ginput.h"
#include "osrs/packetbuffer.h"
#include "osrs/packets/revpacket_lc245_2.h"
#include "osrs/painters.h"
#include "osrs/player_stats.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/rs_component_state.h"
#include "osrs/rsa.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/tables_dat/pixfont.h"
#include "osrs/scene2.h"
#include "osrs/script_queue.h"
#include "osrs/varp_varbit_manager.h"
#include "osrs/world.h"
#include "osrs/world_option_set.h"
#include "osrs/zone_state.h"
#include "world_pickset.h"

#include <stdbool.h>
#include <stdint.h>

struct FileListDat;
struct MinimapRenderCommandBuffer;
struct ToriRSRenderCommandBuffer;
struct InterfaceState;
struct UIInventoryPool;
struct RevConfigBuffer;

#define ACTIVE_PLAYER_SLOT 2047

struct RevPacket_LC245_2_Item
{
    struct RevPacket_LC245_2 packet;

    struct RevPacket_LC245_2_Item* next_nullable;
};
enum GameNetState
{
    GAME_NET_STATE_DISCONNECTED,
    GAME_NET_STATE_LOGIN,
    GAME_NET_STATE_GAME,
};

struct GGame
{
    struct ScriptQueue script_queue;
    struct ScriptQueueItem* lua_current_script_item; /* script we're running from queue */

    bool running;
    int at_render_command_index;
    int at_painters_command_index;
    int at_ui_render_command_index;
    int rebuilt;

    struct ToriRSRenderCommandBuffer* uiscene_queued_commands;
    struct MinimapRenderCommandBuffer* minimap_dynamic_commands;

    int tile_clicked_x;
    int tile_clicked_z;
    int tile_clicked_level;

    int build_player;
    int cc;
    bool latched;

    struct RingBuf* netin;

    struct ToriRSNetSharedBuffer* net_shared;

    enum GameNetState net_state;
    char login_username[64];
    char login_password[64];
    struct PacketBuffer* packet_buffer;
    struct LoginProto* loginproto;
    struct Isaac* random_in;
    struct Isaac* random_out;
    struct rsa rsa;

    struct UIScene* ui_scene;
    /** Scene2 used for world 3D; textures load here before world exists. world->scene2 points here.
     */
    struct Scene2* scene2;
    struct UITree* ui_root_buffer;
    struct UITree* ui_stack;
    struct ClientScriptVM* clientscript_vm;
    struct RSComponentStatePool* rs_component_state;
    struct UIInventoryPool* inv_pool;
    struct RevConfigBuffer* pending_revconfig;

#define UITREE_TRAVERSAL_STACK_MAX 64
    int32_t uitree_stack[UITREE_TRAVERSAL_STACK_MAX];
    int uitree_stack_top;   /* -1 = empty */
    int32_t uitree_current; /* uitree node index, -1 when traversal done */

    int uiscene_command_idx;

    int cycles_elapsed;
    int cycle;
    int next_notimeout_cycle;

    // Old

    int next_rebuild;

    int mouse_cycle;
    int mouse_clicked;
    int mouse_clicked_x;
    int mouse_clicked_y;
    int mouse_clicked_right;
    int mouse_clicked_right_x;
    int mouse_clicked_right_y;
    int interface_consumed_click; /* 1 if click was handled by interface (tab, sidebar, etc.) */
    int mouse_button_down;        /* 1 while left button held, 0 on release */
    int mouse_x;
    int mouse_y;

    int camera_world_x;
    int camera_world_y;
    int camera_world_z;
    int camera_yaw;
    int camera_pitch;
    int camera_roll;
    int camera_fov;
    int camera_movement_speed;
    int camera_rotation_speed;

    struct BuildCacheDat* buildcachedat;
    struct BuildCache* buildcache;

    struct InterfaceState* iface;

    struct VarPVarBitManager varp_varbit;

    /* Media filelist kept after cache_media so we can load component sprites when interfaces load
     */
    struct FileListDat*
        media_filelist; /* forward decl; include osrs/rscache/filelist.h when using */

    /* Used by init_scene (BuildCache path) when driving from Lua; NULL when not in use */
    struct DashMap* init_scenery_configmap;
    struct DashMap* init_texture_definitions_configmap;
    struct DashMap* init_sequences_configmap;

    uint64_t tick_ms;
    uint64_t next_tick_ms;

    struct RevPacket_LC245_2_Item* packets_lc245_2;

    struct World* world;
    struct WorldPickSet pickset;
    struct WorldOptionSet option_set;

    struct DashGraphics* sys_dash;
    struct PaintersBuffer* sys_painter_buffer;

    struct DashPosition* position;
    struct DashViewPort* view_port;
    struct DashViewPort* iface_view_port;
    struct DashCamera* camera;

    int viewport_offset_x;
    int viewport_offset_y;

    /* SDL Soft3D: map window-space mouse to soft buffer (last SDL_RenderCopy dst rect).
     * False on Emscripten (PollEvents maps mouse before GameProcessInput). */
    bool soft3d_mouse_from_window;
    int soft3d_present_dst_x;
    int soft3d_present_dst_y;
    int soft3d_present_dst_w;
    int soft3d_present_dst_h;
    int soft3d_buffer_w;
    int soft3d_buffer_h;
};

void
game_add_message(
    struct GGame* game,
    int type,
    const char* text,
    const char* sender);

void
game_npc_add(
    struct GGame* game,
    int npc_type_id);

void
game_npc_remove(
    struct GGame* game,
    int npc_type_id);

#endif