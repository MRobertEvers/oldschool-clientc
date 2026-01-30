#ifndef GAME_H
#define GAME_H

#include "datastruct/ringbuf.h"
#include "datastruct/vec.h"
#include "game_entity.h"
#include "lclogin.h"
#include "osrs/buildcache.h"
#include "osrs/buildcachedat.h"
#include "osrs/gametask.h"
#include "osrs/ginput.h"
#include "osrs/gio.h"
#include "osrs/packetbuffer.h"
#include "osrs/packets/revpacket_lc245_2.h"
#include "osrs/painters.h"
#include "osrs/rscache/tables_dat/pixfont.h"
#include "osrs/scene.h"
#include "osrs/scenebuilder.h"

#include <stdbool.h>
#include <stdint.h>

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
    bool running;
    int at_render_command_index;
    int at_painters_command_index;
    int rebuilt;

    int awaiting_models;
    int build_player;
    int cc;
    bool latched;

    struct RingBuf* netin;
    struct RingBuf* netout;

    enum GameNetState net_state;
    struct LCLogin* login;
    uint8_t outbound_buffer[4096];
    int outbound_size;
    struct PacketBuffer* packet_buffer;

    int cycles_elapsed;
    int cycle;
    int next_notimeout_cycle;

    int next_rebuild;

    int mouse_cycle;
    int mouse_clicked;
    int mouse_clicked_x;
    int mouse_clicked_y;
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

    struct PlayerEntity players[MAX_PLAYERS];
    uint32_t active_players[MAX_PLAYERS];
    int player_count;
    struct NPCEntity npcs[MAX_NPCS];
    uint32_t active_npcs[MAX_NPCS];
    int npc_count;

    struct BuildCacheDat* buildcachedat;
    struct BuildCache* buildcache;

    struct SceneBuilder* scenebuilder;

    uint64_t tick_ms;
    uint64_t next_tick_ms;

    struct RevPacket_LC245_2_Item* packets_lc245_2;
    struct RevPacket_LC245_2_Item* packets_lc245_2_inflight;

    struct Vec* scene_elements;
    struct Scene* scene;

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
    struct GIOQueue* io;
    struct GameTask* tasks_nullable;

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
};

#endif