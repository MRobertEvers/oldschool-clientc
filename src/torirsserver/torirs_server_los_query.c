/*
 * Line-of-sight and pack reads for the test driver.
 *
 * Owner: the waves loop's seam `los_and_pack`
 * (docs/minigames/waves_loop/SEAM_TRIAGE_2026-10-03b.md). The driver half is
 * src/plugin/torirs_plugin_drive_los.c (api.drive.server_los /
 * server_npc_pack) and script/plugins/quest_driver/world.lua
 * (t.world.los, t.npc.pack).
 *
 * Why this exists. A wave fight is decided by line of sight: the Inferno's
 * pillar safespots, Zuk's shield, the Colosseum's pillars. A test that wants
 * to say "the ranger could not see me on the tick it would have fired" must
 * read the answer the SERVER would give that tick, and the only honest way to
 * do that is to ask the server's own routines with the arguments its own
 * callers pass. Nothing in this file computes a line itself: the booleans are
 * ToriRSServer_SceneLineOfSight / LineOfWalk / Approached (torirs_server_scene.c,
 * themselves a port of LostCity's rsmod LineValidator.rayCastLine). The
 * blocker list beside them is a diagnostic -- the flagged tiles of the box the
 * ray crosses -- so a FALSE can be explained, not a second opinion on it.
 *
 * Read-only: no field of the world is written. Safe to call between ticks
 * from the driver (the embedded server runs on the same thread).
 */

#include "torirs_server.h"
#include "torirs_server_content.h"
#include "torirs_server_scene.h"

#include "engine/world_builder/collision_map.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The flags any of the three routines can stop on: the projectile wall bits
 * (SIGHT_BLOCKED_*), a LOC or LOC_PROJ_BLOCKER, and the approached test's
 * BLOCK_NPC_AND_PLAYERS. A tile with none of them cannot block a sight ray. */
#define LOS_QUERY_SIGHT_MASK                                                                     \
    (COLL_FLAG_SIGHT_BLOCKED_NORTH | COLL_FLAG_SIGHT_BLOCKED_EAST |                              \
     COLL_FLAG_SIGHT_BLOCKED_SOUTH | COLL_FLAG_SIGHT_BLOCKED_WEST | COLL_FLAG_LOC |              \
     COLL_FLAG_LOC_PROJ_BLOCKER | COLL_FLAG_BLOCK_NPC_AND_PLAYERS)

/* A box wider than this is not scanned for blockers (the booleans are still
 * the routines' own answers): a 64-tile ray is a question about the arena,
 * not a tile, and listing 16 of its walls explains nothing. */
#define LOS_QUERY_BOX_MAX 24

static int
los_footprint_gap(
    int ax,
    int az,
    int aw,
    int ah,
    int bx,
    int bz,
    int bw,
    int bh)
{
    int gx = 0;
    int gz = 0;

    if( bx > ax + aw - 1 )
        gx = bx - (ax + aw - 1);
    else if( ax > bx + bw - 1 )
        gx = ax - (bx + bw - 1);
    if( bz > az + ah - 1 )
        gz = bz - (az + ah - 1);
    else if( az > bz + bh - 1 )
        gz = az - (bz + bh - 1);
    return gx > gz ? gx : gz;
}

static int
los_imin(int a, int b)
{
    return a < b ? a : b;
}

static int
los_imax(int a, int b)
{
    return a > b ? a : b;
}

void
ToriRSServer_LosQuery(
    int level,
    int sx,
    int sz,
    int sw,
    int sh,
    int dx,
    int dz,
    int dw,
    int dh,
    struct ToriRSServerLosReading* out)
{
    int box_x0;
    int box_z0;
    int box_x1;
    int box_z1;

    assert(out);
    assert(sw >= 1);
    assert(sh >= 1);
    assert(dw >= 1);
    assert(dh >= 1);

    memset(out, 0, sizeof(*out));
    out->level = level;
    out->sx = sx;
    out->sz = sz;
    out->sw = sw;
    out->sh = sh;
    out->dx = dx;
    out->dz = dz;
    out->dw = dw;
    out->dh = dh;
    out->in_scene = ToriRSServer_SceneContains(sx, sz) &&
                    ToriRSServer_SceneContains(sx + sw - 1, sz + sh - 1) &&
                    ToriRSServer_SceneContains(dx, dz) &&
                    ToriRSServer_SceneContains(dx + dw - 1, dz + dh - 1);
    out->intersect = !(dx >= sx + sw || dx + dw <= sx || dz >= sz + sh || dz + dh <= sz);
    out->gap = los_footprint_gap(sx, sz, sw, sh, dx, dz, dw, dh);
    out->line_of_sight =
        ToriRSServer_SceneLineOfSight(level, sx, sz, dx, dz, sw, sh, dw, dh, 0) ? 1 : 0;
    out->approached = ToriRSServer_SceneApproached(level, sx, sz, dx, dz, sw, sh, dw, dh) ? 1 : 0;
    out->line_of_walk =
        ToriRSServer_SceneLineOfWalk(level, sx, sz, dx, dz, sw, sh, dw, dh, 0) ? 1 : 0;
    out->src_flags = ToriRSServer_SceneTileFlags(level, sx, sz);
    out->dst_flags = ToriRSServer_SceneTileFlags(level, dx, dz);

    box_x0 = los_imin(sx, dx);
    box_z0 = los_imin(sz, dz);
    box_x1 = los_imax(sx + sw - 1, dx + dw - 1);
    box_z1 = los_imax(sz + sh - 1, dz + dh - 1);
    if( box_x1 - box_x0 + 1 > LOS_QUERY_BOX_MAX || box_z1 - box_z0 + 1 > LOS_QUERY_BOX_MAX )
    {
        out->blockers_truncated = 1;
        return;
    }
    for( int z = box_z0; z <= box_z1; z++ )
    {
        for( int x = box_x0; x <= box_x1; x++ )
        {
            int flags = ToriRSServer_SceneTileFlags(level, x, z);

            if( (flags & LOS_QUERY_SIGHT_MASK) == 0 )
                continue;
            if( out->blocker_count >= TORIRSSERVER_LOS_BLOCKERS_MAX )
            {
                out->blockers_truncated = 1;
                return;
            }
            out->blockers[out->blocker_count].x = x;
            out->blockers[out->blocker_count].z = z;
            out->blockers[out->blocker_count].flags = flags;
            out->blocker_count++;
        }
    }
}

/* ------------------------------------------------------------------ pack */

/*
 * The newest npc_anim per world slot, read incrementally off the tick log.
 *
 * The log only grows while one world is logged; a re-enable (relog, world
 * reset) restarts it from serial 1 with a new start tick, which is what
 * `start_tick` detects. A slot's entry is dropped on its npc_spawn and
 * npc_free rows, so a recycled slot never reports the previous occupant's
 * swing.
 */
struct LosPackAnimCache
{
    int start_tick;
    uint32_t scanned;
    int seq[TORIRSSERVER_NPC_MAX];
    int tick[TORIRSSERVER_NPC_MAX];
};

static struct LosPackAnimCache g_pack_anim = { .start_tick = -2 };

#define LOS_PACK_READ_CHUNK 1024
static struct ToriRSServerTicklogRow g_pack_rows[LOS_PACK_READ_CHUNK];

static void
los_pack_anim_reset(int start_tick)
{
    g_pack_anim.start_tick = start_tick;
    g_pack_anim.scanned = 0;
    for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        g_pack_anim.seq[i] = -1;
        g_pack_anim.tick[i] = -1;
    }
}

static void
los_pack_anim_refresh(struct ToriRSServer* srv)
{
    int start_tick;

    assert(srv);
    if( !ToriRSServer_TicklogEnabled(srv) )
    {
        if( g_pack_anim.start_tick != -1 )
            los_pack_anim_reset(-1);
        return;
    }
    start_tick = ToriRSServer_TicklogStartTick();
    if( start_tick != g_pack_anim.start_tick ||
        ToriRSServer_TicklogCount() < g_pack_anim.scanned )
        los_pack_anim_reset(start_tick);
    while( g_pack_anim.scanned < ToriRSServer_TicklogCount() )
    {
        uint32_t scanned = g_pack_anim.scanned;
        int count = ToriRSServer_TicklogReadFiltered(g_pack_anim.scanned, -1, -1, g_pack_rows,
                                                     LOS_PACK_READ_CHUNK, &scanned);

        for( int i = 0; i < count; i++ )
        {
            const struct ToriRSServerTicklogRow* row = &g_pack_rows[i];
            int slot = row->a;

            if( row->kind != TORIRSSERVER_TICKLOG_NPC_ANIM &&
                row->kind != TORIRSSERVER_TICKLOG_NPC_SPAWN &&
                row->kind != TORIRSSERVER_TICKLOG_NPC_FREE )
                continue;
            if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
                continue;
            if( row->kind == TORIRSSERVER_TICKLOG_NPC_ANIM )
            {
                g_pack_anim.seq[slot] = row->c;
                g_pack_anim.tick[slot] = row->tick;
            }
            else
            {
                g_pack_anim.seq[slot] = -1;
                g_pack_anim.tick[slot] = -1;
            }
        }
        if( scanned <= g_pack_anim.scanned )
            break;
        g_pack_anim.scanned = scanned;
    }
}

static int
los_mode_holds_player(int mode)
{
    return mode >= TORIRSSERVER_NPCMODE_PLAYERESCAPE && mode <= TORIRSSERVER_NPCMODE_APPLAYER5;
}

static void
los_pack_target(
    struct ToriRSServer* srv,
    const struct ToriRSServerNpc* npc,
    struct ToriRSServerPackRow* row)
{
    row->target_kind = TORIRSSERVER_PACK_TARGET_NONE;
    row->target_pid = -1;
    row->target_slot = -1;
    row->target_type = -1;
    row->target_via_mode = 0;
    if( npc->combat_target >= 0 && npc->combat_target < TORIRSSERVER_PLAYER_MAX &&
        srv->players[npc->combat_target].active )
    {
        row->target_kind = TORIRSSERVER_PACK_TARGET_PLAYER;
        row->target_pid = npc->combat_target;
        return;
    }
    if( npc->combat_target_npc >= 0 && npc->combat_target_npc < TORIRSSERVER_NPC_MAX )
    {
        const struct ToriRSServerNpc* other = &srv->npcs[npc->combat_target_npc];

        if( other->active && other->generation == npc->combat_target_npc_gen )
        {
            row->target_kind = TORIRSSERVER_PACK_TARGET_NPC;
            row->target_slot = npc->combat_target_npc;
            row->target_type = other->type;
            return;
        }
    }
    /* A standing mode about a player (opplayer, applayer, follow, face):
     * the pid it holds, validated the way npc_mode_player does
     * (torirs_server_world.c), so a logged-out pid never reads as a target. */
    if( los_mode_holds_player(npc->mode) && npc->mode_target_gen != 0 &&
        npc->mode_target_pid >= 0 && npc->mode_target_pid < TORIRSSERVER_PLAYER_MAX )
    {
        const struct ToriRSServerPlayer* player = &srv->players[npc->mode_target_pid];

        if( player->active && player->login_generation == npc->mode_target_gen )
        {
            row->target_kind = TORIRSSERVER_PACK_TARGET_PLAYER;
            row->target_pid = npc->mode_target_pid;
            row->target_via_mode = 1;
        }
    }
}

static int
los_pack_compare(
    const void* a,
    const void* b)
{
    const struct ToriRSServerPackRow* ra = a;
    const struct ToriRSServerPackRow* rb = b;

    if( ra->gap_player != rb->gap_player )
        return ra->gap_player < rb->gap_player ? -1 : 1;
    return ra->slot < rb->slot ? -1 : (ra->slot > rb->slot ? 1 : 0);
}

int
ToriRSServer_NpcPackRead(
    struct ToriRSServer* srv,
    int pid,
    int radius,
    int only_slot,
    struct ToriRSServerPackRow* out,
    int max)
{
    const struct ToriRSServerPlayer* player;
    int count = 0;
    int slot_max;

    assert(srv);
    assert(out);
    assert(max > 0);
    assert(pid >= 0);
    assert(pid < TORIRSSERVER_PLAYER_MAX);
    player = &srv->players[pid];
    assert(player->active);

    los_pack_anim_refresh(srv);
    slot_max = srv->npc_slot_max < TORIRSSERVER_NPC_MAX ? srv->npc_slot_max : TORIRSSERVER_NPC_MAX;
    for( int slot = 0; slot < slot_max; slot++ )
    {
        const struct ToriRSServerNpc* npc = &srv->npcs[slot];
        struct ToriRSServerPackRow* row;
        int size;
        int gap;
        int reach_x;
        int reach_z;

        if( only_slot >= 0 && slot != only_slot )
            continue;
        if( !npc->active || npc->level != player->level )
            continue;
        size = npc->size > 0 ? npc->size : 1;
        gap = los_footprint_gap(npc->x, npc->z, size, size, player->x, player->z, 1, 1);
        if( gap > radius )
            continue;
        /* Full: keep the NEAREST `max`. Stopping at the first `max` slots
         * instead dropped every npc spawned late (a high slot) the moment a
         * busy area filled the buffer -- measured: Lumbridge at radius 104
         * holds more than 256 npcs and a ::spawn goblin landed on slot 1079,
         * so a pack of radius 4 around it read 0 npcs. */
        if( count < max )
            row = &out[count++];
        else
        {
            int far = 0;

            for( int i = 1; i < max; i++ )
            {
                if( out[i].gap_player > out[far].gap_player )
                    far = i;
            }
            if( gap >= out[far].gap_player )
                continue;
            row = &out[far];
        }
        memset(row, 0, sizeof(*row));
        row->slot = slot;
        row->type = npc->type;
        row->x = npc->x;
        row->z = npc->z;
        row->level = npc->level;
        row->size = size;
        row->hitpoints = npc->hitpoints;
        row->max_hitpoints = npc->max_hitpoints;
        row->dying = npc->death_tick >= 0;
        /* npc_def()'s fallback (torirs_server_combat.c): no def reads as the
         * content default record, as combat reads it. */
        row->attackrange = npc->def ? npc->def->attackrange
                                    : ToriRSServer_ContentNpcDefault()->attackrange;
        row->mode = npc->mode;
        los_pack_target(srv, npc, row);
        row->face_entity = npc->face_entity;
        row->walk_x = npc->waypoint_index >= 0 ? npc->waypoints[0].x : -1;
        row->walk_z = npc->waypoint_index >= 0 ? npc->waypoints[0].z : -1;
        row->anim_seq = g_pack_anim.seq[slot];
        row->anim_tick = g_pack_anim.tick[slot];
        if( g_pack_anim.start_tick < 0 )
        {
            row->anim_seq = -1;
            row->anim_tick = -1;
        }
        /* The exact call torirs_server_combat.c makes before an npc with
         * reach fires at a player: from the player's reach tile, 1x1, to the
         * npc's footprint. */
        ToriRSServer_PlayerReachTile(srv, player, npc->x, npc->z, &reach_x, &reach_z);
        row->sees_player = ToriRSServer_SceneApproached(npc->level, reach_x, reach_z, npc->x,
                                                        npc->z, 1, 1, size, size)
                               ? 1
                               : 0;
        row->los_player = ToriRSServer_SceneLineOfSight(npc->level, reach_x, reach_z, npc->x,
                                                        npc->z, 1, 1, size, size, 0)
                              ? 1
                              : 0;
        row->gap_player = los_footprint_gap(npc->x, npc->z, size, size, reach_x, reach_z, 1, 1);
    }
    qsort(out, (size_t)count, sizeof(*out), los_pack_compare);
    return count;
}
