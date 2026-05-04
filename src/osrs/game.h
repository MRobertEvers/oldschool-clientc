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
#include "osrs/core/game_cache_tag.h"
#include "osrs/core/revision.h"
#include "osrs/clientscript_vm.h"
#include "osrs/ginput.h"
#include "osrs/core/packetbuffer.h"
#include "osrs/revs/lc245_2/gameproto_rev245_2_packets.h"
#include "osrs/revs/lc245_2/revision_lc245_2.h"
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
#include "tori_rs_render.h"
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

enum GameNetState
{
    GAME_NET_STATE_DISCONNECTED,
    GAME_NET_STATE_LOGIN,
    GAME_NET_STATE_GAME,
};

#define UITREE_TRAVERSAL_STACK_MAX 64

/** File-scope type so MSVC lays out `GGame` identically in C vs C++ (nested struct differed). */
struct UITraversalFrame
{
    int32_t parent_index; /* index of the parent node in ui_root_buffer->components */
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
    /** Tracks emitted TORIRS_GFX pass across UI component steps (see frame_emit_pass). */
    enum FramePassKind frame_pass;

    struct MinimapRenderCommandBuffer* minimap_dynamic_commands;

    int tile_clicked_x;
    int tile_clicked_z;
    int tile_clicked_level;
    int mouse_tile_x;
    int mouse_tile_z;
    int mouse_tile_level;

    /** Click cross overlay (Client.ts crossX/crossY/crossMode/crossCycle). */
    int cross_x;
    int cross_y;
    int cross_mode;  /* 0=off, 1=walk (yellow), 2=interact (red) */
    int cross_cycle; /* 0..399, +20 per frame when mode != 0 */

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

    /** Per-level frame state pushed when descending into a child list. */
    struct UITraversalFrame uitree_stack[UITREE_TRAVERSAL_STACK_MAX];
    int uitree_stack_top;            /* -1 = empty */
    int32_t uitree_current;          /* uitree node index, -1 when traversal done */

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
    /** Tagged ownership for BuildCacheDat vs BuildCache (distinct from rscache `struct Cache`). */
    struct GameCacheTag game_cache_tag;

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
    uint64_t next_camera_save_ms;

    struct Revision revision;

    struct World* world;
    struct WorldPickSet pickset;
    struct WorldOptionSet option_set;

    /* Zone state: set by UPDATE_ZONE_* packets; used by zone sub-packets (OBJ/LOC). */
    int zone_base_x;
    int zone_base_z;

    /* Player stats: updated by UPDATE_STAT */
    int player_stat_xp[PLAYER_STAT_COUNT];
    int player_stat_level[PLAYER_STAT_COUNT];

    /* Run energy / weight: updated by UPDATE_RUNENERGY / UPDATE_RUNWEIGHT */
    int run_energy;  /* 0-100 */
    int run_weight;  /* grams, can be negative (negative = lighter than threshold) */

    /* In-multicombat zone flag: updated by SET_MULTIWAY */
    int in_multiway;

    /* Minimap flag tile: set by REBUILD_NORMAL carryover, cleared by UNSET_MAP_FLAG */
    int minimap_flag_x;
    int minimap_flag_z;
    int minimap_flag_has;

    /* Hint arrow: set by HINT_ARROW, type 0 = none */
    int hint_arrow_type;
    int hint_arrow_entity_id;
    int hint_arrow_tile_x;
    int hint_arrow_tile_z;

    /* Zone-packet runtime state (obj_stacks allocated lazily as flat
     * [MAP_TERRAIN_LEVELS * ZONE_SCENE_SIZE * ZONE_SCENE_SIZE] pointer array).
     * loc_changes_head: singly-linked list of pending loc-change entries. */
    struct ObjStackEntry*** obj_stacks;
    struct LocChangeEntry*  loc_changes_head;
    struct MapAnimEntry*    map_anims_head;
    struct MapProjAnimEntry* map_projanims_head;

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