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
#include "osrs/gamecache/gamecache.h"
#include "osrs/core/game_cache_tag.h"
#include "osrs/core/revision.h"
#include "osrs/chat.h"
#include "osrs/clientscript_vm.h"
#include "osrs/ginput.h"
#include "osrs/core/packetbuffer.h"
#include "osrs/core/serverprot_packets.h"
#include "osrs/revs/lc245_2/revision_lc245_2.h"
#include "osrs/painters.h"
#include "osrs/player_stats.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "tori_rs_frame_state.h"
#include "osrs/rs_component_state.h"
#include "osrs/rsa.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/tables_dat/pixfont.h"
#include "osrs/scene2.h"
#include "osrs/script_queue.h"
#include "osrs/minimenu_regions.h"
#include "osrs/minimenu_state.h"
#include "osrs/world.h"
#include "osrs/world_option_set.h"
#include "osrs/zone_state.h"
#include "tori_rs_render.h"
#include "world_pickset.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct FileListDat;
struct GInput;
struct MinimapRenderCommandBuffer;
struct MinimenuRenderCommandBuffer;
struct ToriRSRenderCommandBuffer;
struct InterfaceState;
struct UIInventoryPool;
struct RevConfigBuffer;

#define ACTIVE_PLAYER_SLOT 2047

#define GAME_SENT_PATH_MAX 25

struct GameSentPath
{
    int     length;
    uint8_t tile_x[GAME_SENT_PATH_MAX];
    uint8_t tile_z[GAME_SENT_PATH_MAX];
};

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
    /** High-level minimenu rects/text; translated to ToriRS in `minimenu_game_translate_commands`. */
    struct MinimenuRenderCommandBuffer* minimenu_commands;
    /** Right-click menu: mirrors Client.ts menuVisible / options; iface-viewport coords.
     * Populated by minimenu_game_show from option_set; hit-tested after FrameEnd. */
    struct MinimenuState minimenu;
    /** From `[component:minimenu]` UI INI (openMenu regions + placement clamps). */
    struct MinimenuIniRegions minimenu_regions;

    int tile_clicked_x;
    int tile_clicked_z;
    int tile_clicked_level;
    /** After MOVE_OPCLICK from minimenu, skip one MOVE_GAMECLICK from local `game_try_move`. */
    int suppress_move_gameclick_once;
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
    /** When true, world step emits TORIRS_GFX_DRAW_LINE path overlays (Soft3D handles draw).
     * Defaults to true in LibToriRS_GameNew; set false to disable. */
    bool debug_entity_pathing;
    /** When true, world step emits TORIRS_GFX_DRAW_LINE collision map edges at terrain height.
     * Enabled by [component:collisionmap_overlay] in the UI INI; toggled via Nuklear debug panel. */
    bool debug_collisionmap_overlay;
    /** Most recently sent BFS walk path (scene-local tiles); length==0 when none. */
    struct GameSentPath sent_path;

    struct RingBuf* netin;

    struct ToriRSNetSharedBuffer* net_shared;

    enum GameNetState net_state;
    /** From login response code 2 (Client.ts staffmodlevel); used for @cr1@/@cr2@ chat sender prefix. */
    int staff_mod_level;
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

    /** Set in uitree_resolve_game_uiscene_sprite_ids after cache INI load; -1 = unresolved. */
    int hitmarks_uiscene_element_id;
    int hitsplat_damage_font_id;

    int cycles_elapsed;
    int cycle;
    int next_notimeout_cycle;

    // Old

    int next_rebuild;

    /** Per-frame mouse input state in game/buffer coordinates (transform applied). */
    struct MouseSnapshot mouse;

    int mouse_cycle;
    int interface_consumed_click; /* 1 if click was handled by interface (tab, sidebar, etc.) */
    /** Increments each game tick while left button is held; reset to 0 on release.
     * Mirrors Client.ts scrollCycle: scroll delta = scroll_cycle * 4 per tick. */
    int scroll_cycle;

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
    struct GameCache* gamecache;
    struct BuildCache* buildcache;
    /** Tagged ownership for BuildCacheDat vs BuildCache (distinct from rscache `struct Cache`). */
    struct GameCacheTag game_cache_tag;

    struct InterfaceState* iface;
    struct Chat* chat; /* managed by chat.c; NULL until chat_init() */
    /** From rev INI [component:chat_messages] (+ optional chat_input_line_y from chat_input). */
    struct ChatUILayout chat_layout;
    int chat_layout_valid;

    /** From `[magic_tab:*]` UI INI; spellbook redstone tab (default 6 in LibToriRS_GameNew). */
    int magic_tab_spellbook_sidebar_tabno;
    /** From `[magic_tab:*]`; PKTOUT IF_BUTTON for inv_transmit; 0 = do not send. */
    int magic_tab_inv_transmit_if_button_comp;

    /* Per-frame client settings written by clientscript_vm_drain_clientcodes(). */
    int settings_brightness;
    int settings_music_vol;
    int settings_wave_vol;
    int settings_one_mouse;
    int settings_bank;

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
    /** Terrain plane 0..3 for local player (PLAYER_INFO local XZ+level); zone LOC uses this. */
    uint8_t local_player_plane;

    /* Player stats: updated by UPDATE_STAT */
    int player_stat_xp[PLAYER_STAT_COUNT];
    int player_stat_level[PLAYER_STAT_COUNT];

    /* Run energy / weight: updated by UPDATE_RUNENERGY / UPDATE_RUNWEIGHT */
    int run_energy;  /* 0-100 */
    int run_weight;  /* grams, can be negative (negative = lighter than threshold) */

    /* In-multicombat zone flag: updated by SET_MULTIWAY */
    int in_multiway;

    /** Per-slot player right-click op labels (Client.ts playerOp / SET_PLAYER_OP). */
    char player_menu_op[5][64];
    /** When true, add MINIMENU_ACTION_PRIORITY_OFFSET (Client.ts playerOpPriority). */
    bool player_menu_op_deprioritize[5];

    /* Reboot timer: set by UPDATE_REBOOT_TIMER, -1 = not active */
    int reboot_timer_ticks;

    /* Friend / ignore lists (base37 encoded usernames + online world).
     * world[i] == 0 means offline.  Counts are the number of populated slots. */
#define GAME_SOCIAL_LIST_MAX 200
    int64_t friend_usernames[GAME_SOCIAL_LIST_MAX];
    int     friend_worlds[GAME_SOCIAL_LIST_MAX];
    int     friend_count;
    int64_t ignore_usernames[GAME_SOCIAL_LIST_MAX];
    int     ignore_count;

    /* Tutorial state: tab flashing + open interface */
    int tutorial_tab_flash;       /* sidebar tab index to flash, -1 = none */
    int tutorial_interface;       /* open tutorial interface component_id, -1 = none */

    /* Minimap flag tile: set by REBUILD_NORMAL carryover, cleared by UNSET_MAP_FLAG */
    int minimap_flag_x;
    int minimap_flag_z;
    int minimap_flag_has;

    /* Hint arrow: set by HINT_ARROW, type 0 = none */
    int hint_arrow_type;
    int hint_arrow_entity_id;
    int hint_arrow_tile_x;
    int hint_arrow_tile_z;

    /* Zone-packet runtime state (loc_changes, map anims — obj_stacks live on World). */
    struct LocChangeEntry*  loc_changes_head;
    struct MapAnimEntry*    map_anims_head;
    struct MapProjAnimEntry* map_projanims_head;

    struct DashGraphics* sys_dash;
    struct PaintersBuffer* sys_painter_buffer;

    /** Scratch for tile hover convex hull (projected points + hull output). */
    float tile_hover_scratch[32];
    /** Dynamic flat mesh: convex hull of hovered tile footprint, drawn after world 3D. */
    struct DashModel* tile_hover_overlay_model;
    /** Per-frame: tile hover overlay pass already attempted (see uielem_world_step). */
    bool tile_hover_overlay_emitted;
    /** Per-frame: tile_hover_overlay_model contains a valid hull built this frame. */
    bool tile_hover_hull_buffered;
    /** Camera-relative position of the hovered tile's SW corner (model-local origin). */
    struct DashPosition tile_hover_overlay_pos;

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
game_player_menu_ops_reset(struct GGame* game);

void
game_add_message(
    struct GGame* game,
    int type,
    const char* text,
    const char* sender);

/** Fill display name for chat input line (local player or login username). */
void
game_chat_input_screen_name(struct GGame* game, char* out, size_t cap);

/** Keyboard/chat buffer (Enter to open/send, printable keys while open). */
void
game_chat_process_input(struct GGame* game, struct GInput* input);

bool
game_chat_is_typing(struct GGame const* game);

/** True when Enter / public chat input is allowed (in game, no chat IF, no minimenu, etc.). */
bool
game_chat_public_input_eligible(struct GGame const* game);

void
game_npc_add(
    struct GGame* game,
    int npc_type_id);

void
game_npc_remove(
    struct GGame* game,
    int npc_type_id);

#endif