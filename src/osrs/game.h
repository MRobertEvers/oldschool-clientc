#ifndef GAME_H
#define GAME_H

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
#include "datastruct/ringbuf.h"
#include "game_entity.h"
#include "graphics/dash.h"
#include "osrs/buildcache.h"
#include "osrs/buildcachedat.h"
#include "osrs/ginput.h"
#include "osrs/gio.h"
#include "osrs/packetbuffer.h"
#include "osrs/packets/revpacket_lc245_2.h"
#include "osrs/painters.h"
#include "osrs/player_stats.h"
#include "osrs/revconfig/static_ui.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/rsa.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/tables_dat/pixfont.h"
#include "osrs/script_queue.h"
#include "osrs/varp_varbit_manager.h"
#include "osrs/world.h"
#include "osrs/world_option_set.h"
#include "osrs/zone_state.h"
#include "world_pickset.h"

#include <stdbool.h>
#include <stdint.h>

struct FileListDat;

#define MAX_PLAYERS 2048
#define MAX_NPCS 8192

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
    int rebuilt;

    int tile_clicked_x;
    int tile_clicked_z;
    int tile_clicked_level;

    int awaiting_models;
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
    struct StaticUIBuffer* static_ui;

    int cycles_elapsed;
    int cycle;
    int next_notimeout_cycle;

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

    struct VarPVarBitManager varp_varbit;

    /* Local player stats (UPDATE_STAT, UPDATE_RUNENERGY) */
    int player_stat_xp[PLAYER_STAT_COUNT];
    int player_stat_effective_level[PLAYER_STAT_COUNT];
    int player_stat_base_level[PLAYER_STAT_COUNT];
    int player_run_energy;

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
    struct Minimap* sys_minimap;
    struct Painter* sys_painter;
    struct PaintersBuffer* sys_painter_buffer;

    struct DashModel* model;
    struct DashPosition* position;
    struct DashViewPort* view_port;
    struct DashViewPort* iface_view_port;
    struct DashCamera* camera;
    struct DashPixFont* pixfont_b12;
    struct DashPixFont* pixfont_p11;
    struct DashPixFont* pixfont_p12;
    struct DashPixFont* pixfont_q8;

    struct DashSprite* sprite_invback;
    struct DashSprite* sprite_chatback;
    struct DashSprite* sprite_mapback;
    struct DashSprite* sprite_backbase1;
    struct DashSprite* sprite_backbase2;
    struct DashSprite* sprite_backhmid1;
    struct DashSprite* sprite_sideicons[13];
    struct DashSprite* sprite_compass;
    struct DashSprite* sprite_mapedge;
    struct DashSprite* sprite_mapscene[50];
    struct DashSprite* sprite_mapfunction[50];
    struct DashSprite* sprite_hitmarks[20];
    struct DashSprite* sprite_headicons[20];
    struct DashSprite* sprite_mapmarker0;
    struct DashSprite* sprite_mapmarker1;
    struct DashSprite* sprite_cross[8];
    struct DashSprite* sprite_mapdot0;
    struct DashSprite* sprite_mapdot1;
    struct DashSprite* sprite_mapdot2;
    struct DashSprite* sprite_mapdot3;
    struct DashSprite* sprite_scrollbar0;
    struct DashSprite* sprite_scrollbar1;
    struct DashSprite* sprite_redstone1;
    struct DashSprite* sprite_redstone2;
    struct DashSprite* sprite_redstone3;
    struct DashSprite* sprite_redstone1h;
    struct DashSprite* sprite_redstone2h;
    struct DashSprite* sprite_redstone1v;
    struct DashSprite* sprite_redstone2v;
    struct DashSprite* sprite_redstone3v;
    struct DashSprite* sprite_redstone1hv;
    struct DashSprite* sprite_redstone2hv;

    struct DashSprite* sprite_modicons[2];
    struct DashSprite* sprite_backleft1;
    struct DashSprite* sprite_backleft2;
    struct DashSprite* sprite_backright1;

    struct DashSprite* sprite_backright2;
    struct DashSprite* sprite_backtop1;
    struct DashSprite* sprite_backvmid1;
    struct DashSprite* sprite_backvmid2;
    struct DashSprite* sprite_backvmid3;
    struct DashSprite* sprite_backhmid2;

    // Interface IDs (which interface is currently shown in each area)
    int viewport_interface_id;
    int sidebar_interface_id;
    int chat_interface_id;

    // Chat privacy (Client.ts chatPublicMode, chatPrivateMode, chatTradeMode; packet sync)
    int chat_public_mode;  /* 0=On, 1=Friends, 2=Off, 3=Hide */
    int chat_private_mode; /* 0=On, 1=Friends, 2=Off */
    int chat_trade_mode;   /* 0=On, 1=Friends, 2=Off */

    // Tab system
    int selected_tab;
    int tab_interface_id[14];

    // Scroll position per component (for scrollable layers). Index by component id.
#define MAX_COMPONENT_SCROLL_IDS 16384
    int component_scroll_position[MAX_COMPONENT_SCROLL_IDS];

    // Hovered component id for current draw area (viewport/sidebar/chat). Set before drawing;
    // components with hide==true are only drawn when component->id == current_hovered_interface_id.
    int current_hovered_interface_id;

    // For scrollbar arrow hold: cycles_elapsed when we last applied scroll (so step = rate *
    // delta).
    int scroll_arrow_hold_cycles_last;

    // Item selection (for inventory clicks)
    int selected_item;
    int selected_interface;
    int selected_area;
    int selected_cycle;

    /* Terrain tile click (set during frame when a drawn tile contains mouse; consumed in cycle) */
    int clicked_tile_x;     /* scene-local tile X (add scene_base_tile_x for world) */
    int clicked_tile_z;     /* scene-local tile Z (add scene_base_tile_z for world) */
    int clicked_tile_valid; /* 1 if click was on a tile this frame, 0 otherwise */

    /* Scene SW world tile (set on rebuild normal); add to clicked_tile to get world tile for server
     */
    int scene_base_tile_x;
    int scene_base_tile_z;

    /* Zone packet base: set by UPDATE_ZONE_PARTIAL_FOLLOWS / UPDATE_ZONE_FULL_FOLLOWS. Used for
     * LOC_ADD_CHANGE, OBJ_ADD, etc. x = zone_base_x + (pos>>4)&7, z = zone_base_z + pos&7 */
    int zone_base_x;
    int zone_base_z;

    /* Dynamic zone state: obj stacks and loc changes (Client.ts objStacks, locChanges).
     * Cleared on REBUILD_NORMAL. Full impl adds dynamic scene elements. */
#define ZONE_SCENE_SIZE 104
#define ZONE_LEVELS 4
    struct ObjStackEntry* obj_stacks[ZONE_LEVELS][ZONE_SCENE_SIZE][ZONE_SCENE_SIZE];
    struct SceneElement* obj_stack_elements[ZONE_LEVELS][ZONE_SCENE_SIZE][ZONE_SCENE_SIZE];
    struct LocChangeEntry* loc_changes_head;

/* BFS path for move (scene-local tiles); drawn as line overlay. Cleared when highlight cleared. */
#define GAME_PATH_TILE_MAX 26
    int path_tile_x[GAME_PATH_TILE_MAX];
    int path_tile_z[GAME_PATH_TILE_MAX];
    int path_tile_count; /* 0 = no path to draw */

    /* Hovered interactable loc (Client.ts pickedBitsets for entityType 2). Last hit in draw order.
     */
    struct SceneElement* hovered_scene_element;

    /* Hovered dash model (Scene2Element) for debug AABB drawing. Last hit in draw order. */
    struct Scene2Element* hovered_scene2_element;

    /* Viewport offset in screen coords (Client.ts menuArea 0: mouseX-=4, mouseY-=4). */
    int viewport_offset_x;
    int viewport_offset_y;

    /* Minimenu (right-click context menu). Client.ts menuVisible, menuX, menuOption, etc. */
#define MINIMENU_MAX_OPTIONS 32
#define MINIMENU_OPTION_LEN 64
    int menu_visible;
    int menu_area; /* 0=viewport, 1=sidebar, 2=chatbox */
    int menu_x;
    int menu_y;
    int menu_width;
    int menu_height;
    int menu_size;
    char menu_options[MINIMENU_MAX_OPTIONS][MINIMENU_OPTION_LEN];
    int menu_option_action[MINIMENU_MAX_OPTIONS];
    int menu_walk_click_y;
    int cross_mode;
    int cross_x;
    int cross_y;
#define GAME_CHAT_MAX 100
#define GAME_CHAT_SENDER_LEN 64
#define GAME_CHAT_TEXT_LEN 128
    int message_type[GAME_CHAT_MAX];
    char message_sender[GAME_CHAT_MAX][GAME_CHAT_SENDER_LEN];
    char message_text[GAME_CHAT_MAX][GAME_CHAT_TEXT_LEN];

    /* Chat input and scroll (Client.ts chatTyped, chatScrollOffset, chatScrollHeight) */
#define GAME_CHAT_TYPED_LEN 128
    char chat_typed[GAME_CHAT_TYPED_LEN];
    int chat_scroll_offset;
    int chat_scroll_height;
    int chat_input_focused; /* 1=typing to chat (keys go to chat, not movement); 0=unfocused */
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