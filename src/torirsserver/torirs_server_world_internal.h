#ifndef TORIRS_SERVER_WORLD_INTERNAL_H
#define TORIRS_SERVER_WORLD_INTERNAL_H

/*
 * The seam between torirs_server_world.c and torirs_server_world_selftest.c.
 *
 * Everything here was file-static back when the self-test lived at the bottom
 * of the world's own translation unit. Splitting the two files could not keep
 * it that way, but nothing else changed: this is not server API. Only those
 * two files may include this header, and anything that acquires a third caller
 * belongs in torirs_server.h under a ToriRSServer_ name instead.
 *
 * The self-test needs these because it drives the world by hand rather than
 * through a socket: it seeds a container, spawns an npc, runs one tick phase,
 * or feeds an inbound packet body straight to its handler, and then asserts on
 * what the encoders produced.
 */

#include <stddef.h>
#include <stdint.h>

struct ToriRSServer;
struct ToriRSServerInteraction;
struct ToriRSServerMusicRegion;
struct ToriRSServerNpc;
struct ToriRSServerPlayer;

/* The tile a session logs in on. See ToriRSServer_WorldSetHome. */
extern int g_home_x;
extern int g_home_z;

/* The tile a character with NO SAVE FILE starts on -- Tutorial Island. Read
 * once per account, at the login that finds no save. See
 * ToriRSServer_WorldSetTutorialHome. */
extern int g_tutorial_home_x;
extern int g_tutorial_home_z;

/* The player's containers and worn kit. */
void
inv_set(
    struct ToriRSServerPlayer* player,
    int slot,
    int obj_id,
    int count);

int
inv_first_free(const struct ToriRSServerPlayer* player);

void
worn_set(
    struct ToriRSServerPlayer* player,
    int slot,
    int obj_id,
    int count);

void
unequip_slot(
    struct ToriRSServer* srv,
    int slot);

int
player_weight_grams(const struct ToriRSServerPlayer* player);

/* Movement: the step queue and the tiles an actor occupies. */
void
steps_clear(struct ToriRSServerPlayer* player);

int
player_travel_extra(void);

int
npc_travel_extra(struct ToriRSServerNpc const* npc);

void
npc_queue_waypoint(struct ToriRSServerNpc* npc, int x, int z);

void
npc_clear_waypoints(struct ToriRSServerNpc* npc);

void
npc_set_occupancy(struct ToriRSServerNpc const* npc, int add);

void
player_set_occupancy(struct ToriRSServerPlayer const* player, int add);

void
world_occupancy_restamp(struct ToriRSServer* srv);

int
distance_to_rect(
    int from_x,
    int from_z,
    int rect_x,
    int rect_z,
    int size_x,
    int size_z);

int
npc_player_range(
    const struct ToriRSServerNpc* npc,
    const struct ToriRSServerPlayer* player);

int
npc_run_mode(
    struct ToriRSServer* srv,
    struct ToriRSServerNpc* npc,
    int slot);

/* The tick's own phases, driven a piece at a time. */
void
advance_player(struct ToriRSServer* srv);

void
advance_npcs(struct ToriRSServer* srv);

void
maybe_rebuild(struct ToriRSServer* srv);

/* Npcs and ground objects. */
int
npc_spawn(
    struct ToriRSServer* srv,
    int type,
    int x,
    int z,
    int level);

void
ground_clear(
    struct ToriRSServer* srv,
    int slot);

/* Interaction. */
int
interaction_category(const struct ToriRSServerInteraction* interaction);

void
interaction_path_to_pathing_target(struct ToriRSServer* srv);

int
player_action_packet(int name);

int
player_stun_blocks_packet(int name);

/* Inbound packet handlers, called without a socket. */
void
handle_cheat(
    struct ToriRSServer* srv,
    const uint8_t* payload,
    int len);

void
handle_if_button_op(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len);

void
handle_worn_inv_button(
    struct ToriRSServer* srv,
    int component,
    int op_num);

int
cheat_obj_from_name(
    const char* arg,
    char* suggest,
    size_t suggest_size);

int
cheat_npc_from_name(
    const char* arg,
    char* suggest,
    size_t suggest_size);

/* Login side-effects, music and ambience. */
void
ToriRSServer_WorldSocialLogin(struct ToriRSServerPlayer* player);

const struct ToriRSServerMusicRegion*
ToriRSServer_MusicForRegion(int region);

void
ToriRSServer_MusicEnterRegion(
    struct ToriRSServerPlayer* player,
    int map_x,
    int map_z);

void
ToriRSServer_AmbientEnterRegion(
    struct ToriRSServerPlayer* player,
    int map_x,
    int map_z);

#endif /* TORIRS_SERVER_WORLD_INTERNAL_H */
