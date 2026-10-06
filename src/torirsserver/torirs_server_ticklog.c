/*
 * The tick log: the server's own per-tick record of an encounter.
 *
 * Why it exists. A raid test is defensible only when the numbers it claims --
 * "Maiden attacks every 10 ticks", "the Xarpus spit resolved against the tile
 * the player stood on the tick before" -- are MEASURED on our server, the same
 * way Blert measures them on the live game (docs/RAID_ORCHESTRATOR.md sections
 * 2, 4 and 6). The client cannot measure them: it sees animations a frame late
 * and has no idea which tick the server resolved a hit on. So the server
 * writes down what it did, on the tick it did it, and the quest driver reads
 * the rows back (src/plugin/torirs_plugin_drive_ticklog.c,
 * script/plugins/quest_driver/ticklog.lua).
 *
 * What is recorded is the event set tools/verify_tob_timings.py reads from
 * Blert -- npc attack starts (every npc animation goes through
 * ToriRSServer_AnimPlayNpc, C or content), projectiles, hits both ways,
 * spawns, deaths, type changes, loc and ground changes, and every sound, music
 * track and jingle sent to a player -- and the presentation the recorders read
 * off the player and the room (a player's animation and graphic, a loc's
 * animation, an npc's overhead line) -- plus the tiles: a
 * PLAYER_TILE row for every player every tick (after phase_players, so the
 * row is the tile the player's turn left them on, which is the tile every npc
 * acting on the NEXT tick will scan; ENCOUNTER_TIMING.md section 1) and an
 * NPC_TILE row only when an npc's tile changed, so Bloat's walk and the
 * Nylocas lanes are in the log without a row per idle npc per tick.
 *
 * It is OFF unless enabled, and every hook starts with the same one test, so
 * a quest run that never asks pays one branch per event.
 *
 * One world at a time. The embedded server has exactly one; the selftest
 * enables it on the world it is testing. That is what lets the two hooks that
 * are only handed an npc pointer (the animation funnel and the type change,
 * neither of which takes the world) find their slot and their tick.
 */

#include "torirs_server.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TicklogState
{
    /* The world being logged, or NULL when the log is off. */
    struct ToriRSServer* srv;
    struct ToriRSServerTicklogRow* rows;
    int count;
    int capacity;
    int start_tick;
    char* path;
    FILE* out;
    /* The last tile each npc slot was reported on, packed, plus one (0 =
     * nothing reported yet), and the slot generation it belonged to: a reused
     * slot is a different npc and its first tile is a change. */
    int32_t npc_tile_plus1[TORIRSSERVER_NPC_MAX];
    uint16_t npc_generation[TORIRSSERVER_NPC_MAX];
    int dealer_npc;
    /* Each player's `last_input_tick` as the previous RAIDER row saw it. The
     * stamp alone cannot say "this tick": an in-process client's packets are
     * handled BETWEEN ticks, stamped with the tick that just ended, after that
     * tick's row was written -- so a RAIDER row's `input 1` means "an input
     * arrived since the last row", which is the next tick's input. */
    int32_t input_seen[TORIRSSERVER_PLAYER_MAX];
};

static struct TicklogState g_ticklog = { .dealer_npc = -1, .start_tick = -1 };

static char const* const k_kind_names[TORIRSSERVER_TICKLOG_KIND_COUNT] = {
    [TORIRSSERVER_TICKLOG_START] = "start",
    [TORIRSSERVER_TICKLOG_MARK] = "mark",
    [TORIRSSERVER_TICKLOG_NPC_ANIM] = "npc_anim",
    [TORIRSSERVER_TICKLOG_NPC_SPOTANIM] = "npc_spotanim",
    [TORIRSSERVER_TICKLOG_PROJECTILE] = "projectile",
    [TORIRSSERVER_TICKLOG_MAP_SPOTANIM] = "map_spotanim",
    [TORIRSSERVER_TICKLOG_HIT_PLAYER] = "hit_player",
    [TORIRSSERVER_TICKLOG_HIT_NPC] = "hit_npc",
    [TORIRSSERVER_TICKLOG_NPC_SPAWN] = "npc_spawn",
    [TORIRSSERVER_TICKLOG_NPC_DEATH] = "npc_death",
    [TORIRSSERVER_TICKLOG_NPC_FREE] = "npc_free",
    [TORIRSSERVER_TICKLOG_NPC_RETYPE] = "npc_retype",
    [TORIRSSERVER_TICKLOG_LOC_SET] = "loc_set",
    [TORIRSSERVER_TICKLOG_OBJ_ADD] = "obj_add",
    [TORIRSSERVER_TICKLOG_PLAYER_TILE] = "player_tile",
    [TORIRSSERVER_TICKLOG_NPC_TILE] = "npc_tile",
    [TORIRSSERVER_TICKLOG_NPC_FACE] = "npc_face",
    [TORIRSSERVER_TICKLOG_SOUND] = "sound",
    [TORIRSSERVER_TICKLOG_MUSIC] = "music",
    [TORIRSSERVER_TICKLOG_JINGLE] = "jingle",
    [TORIRSSERVER_TICKLOG_PLAYER_ANIM] = "player_anim",
    [TORIRSSERVER_TICKLOG_PLAYER_SPOTANIM] = "player_spotanim",
    [TORIRSSERVER_TICKLOG_LOC_ANIM] = "loc_anim",
    [TORIRSSERVER_TICKLOG_NPC_SAY] = "npc_say",
    [TORIRSSERVER_TICKLOG_NPC_HEAL] = "npc_heal",
    [TORIRSSERVER_TICKLOG_RAIDER] = "raider",
    [TORIRSSERVER_TICKLOG_INPUT] = "input",
    [TORIRSSERVER_TICKLOG_CONSUME] = "consume",
};

/* A SOUND row's label: where the send came from (the npc source adds the
 * emitting npc's slot and type to it, "npc <slot> <type>"). */
static char const* const k_sound_source_names[TORIRSSERVER_TICKLOG_SOUND_SOURCE_COUNT] = {
    [TORIRSSERVER_TICKLOG_SOUND_SYNTH] = "synth",
    [TORIRSSERVER_TICKLOG_SOUND_AREA] = "area",
    [TORIRSSERVER_TICKLOG_SOUND_DISTANCE] = "distance",
    [TORIRSSERVER_TICKLOG_SOUND_NPC] = "npc",
};

/* A MUSIC row's label. */
static char const* const k_music_source_names[TORIRSSERVER_TICKLOG_MUSIC_SOURCE_COUNT] = {
    [TORIRSSERVER_TICKLOG_MUSIC_SCRIPT] = "script",
    [TORIRSSERVER_TICKLOG_MUSIC_REGION] = "region",
    [TORIRSSERVER_TICKLOG_MUSIC_LOGIN] = "login",
};

char const*
ToriRSServer_TicklogKindName(int kind)
{
    assert(kind >= 0);
    assert(kind < TORIRSSERVER_TICKLOG_KIND_COUNT);
    return k_kind_names[kind];
}

int
ToriRSServer_TicklogKindFromName(char const* name)
{
    assert(name);
    for( int i = 0; i < TORIRSSERVER_TICKLOG_KIND_COUNT; i++ )
    {
        if( strcmp(k_kind_names[i], name) == 0 )
            return i;
    }
    return -1;
}

static int
ticklog_on_for(const struct ToriRSServer* srv)
{
    return g_ticklog.srv != NULL && g_ticklog.srv == srv;
}

/* The slot of an npc pointer inside the logged world's pool, or -1 when the
 * log is off or the npc belongs to another world. */
static int
ticklog_npc_slot(const struct ToriRSServerNpc* npc)
{
    if( !g_ticklog.srv )
        return -1;
    assert(npc);
    if( npc < &g_ticklog.srv->npcs[0] || npc >= &g_ticklog.srv->npcs[TORIRSSERVER_NPC_MAX] )
        return -1;
    return (int)(npc - &g_ticklog.srv->npcs[0]);
}

static void
ticklog_write_row(const struct ToriRSServerTicklogRow* row)
{
    if( !g_ticklog.out )
        return;
    /* `g` LAST, after the label: tools/verify_tob_timings.py read_ticklog
     * indexes the label as column 9, and a column inserted before it would
     * have turned every label into a number there. */
    fprintf(g_ticklog.out, "%u\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%d\n",
            (unsigned)row->serial, row->tick, k_kind_names[row->kind], row->a, row->b, row->c,
            row->d, row->e, row->f, row->label, row->g);
}

/* Every row goes through here; `g` is the seventh field, which only
 * HIT_PLAYER fills (ticklog_push below passes 0 for every other kind). */
static uint32_t
ticklog_push_row(
    int kind,
    int a,
    int b,
    int c,
    int d,
    int e,
    int f,
    int g,
    char const* label)
{
    struct ToriRSServerTicklogRow* row;

    assert(g_ticklog.srv);
    if( g_ticklog.count == g_ticklog.capacity )
    {
        int capacity = g_ticklog.capacity ? g_ticklog.capacity * 2 : 4096;
        struct ToriRSServerTicklogRow* grown =
            realloc(g_ticklog.rows, (size_t)capacity * sizeof(*grown));

        assert(grown);
        g_ticklog.rows = grown;
        g_ticklog.capacity = capacity;
    }
    row = &g_ticklog.rows[g_ticklog.count++];
    memset(row, 0, sizeof(*row));
    row->serial = (uint32_t)g_ticklog.count;
    row->tick = g_ticklog.srv->tick;
    row->kind = kind;
    row->a = a;
    row->b = b;
    row->c = c;
    row->d = d;
    row->e = e;
    row->f = f;
    row->g = g;
    if( label )
    {
        /* Tabs and newlines would split the TSV row; a label is a test's
         * short note, so they become spaces rather than an error. */
        size_t n = strlen(label);

        if( n >= sizeof(row->label) )
            n = sizeof(row->label) - 1;
        for( size_t i = 0; i < n; i++ )
            row->label[i] = (label[i] == '\t' || label[i] == '\n' || label[i] == '\r')
                                ? ' '
                                : label[i];
        row->label[n] = '\0';
    }
    ticklog_write_row(row);
    return row->serial;
}

/*
 * A FILE-ONLY row (raider, input, consume): written to ticklog.tsv, never into
 * the row array. Ledgers print serials ("mark 'room start' at tick 60 (serial
 * 99)"), so a row in the array would have moved every later serial and every
 * kept room's ledger with it. Its serial column repeats the last real row's,
 * which keeps the file in order. t.ticklog.rows() does not see these.
 */
static void
ticklog_write_side(
    int kind,
    int a,
    int b,
    int c,
    int d,
    int e,
    int f,
    int g,
    char const* label)
{
    struct ToriRSServerTicklogRow row;

    assert(g_ticklog.srv);
    assert(label);
    if( !g_ticklog.out )
        return;
    memset(&row, 0, sizeof(row));
    row.serial = (uint32_t)g_ticklog.count;
    row.tick = g_ticklog.srv->tick;
    row.kind = kind;
    row.a = a;
    row.b = b;
    row.c = c;
    row.d = d;
    row.e = e;
    row.f = f;
    row.g = g;
    snprintf(row.label, sizeof(row.label), "%s", label);
    for( size_t i = 0; row.label[i] != '\0'; i++ )
    {
        if( row.label[i] == '\t' || row.label[i] == '\n' || row.label[i] == '\r' )
            row.label[i] = ' ';
    }
    ticklog_write_row(&row);
}

static uint32_t
ticklog_push(
    int kind,
    int a,
    int b,
    int c,
    int d,
    int e,
    int f,
    char const* label)
{
    return ticklog_push_row(kind, a, b, c, d, e, f, 0, label);
}

static void
ticklog_snapshot_npc_tiles(void)
{
    struct ToriRSServer* srv = g_ticklog.srv;

    memset(g_ticklog.npc_tile_plus1, 0, sizeof(g_ticklog.npc_tile_plus1));
    memset(g_ticklog.npc_generation, 0, sizeof(g_ticklog.npc_generation));
    for( int pid = 0; pid < TORIRSSERVER_PLAYER_MAX; pid++ )
        g_ticklog.input_seen[pid] = pid < srv->player_count ? srv->players[pid].last_input_tick : 0;
    for( int slot = 0; slot < srv->npc_slot_max && slot < TORIRSSERVER_NPC_MAX; slot++ )
    {
        const struct ToriRSServerNpc* npc = &srv->npcs[slot];

        if( !npc->active )
            continue;
        g_ticklog.npc_tile_plus1[slot] = ToriRSServer_CoordPack(npc->level, npc->x, npc->z) + 1;
        g_ticklog.npc_generation[slot] = npc->generation;
    }
}

int
ToriRSServer_TicklogEnable(
    struct ToriRSServer* srv,
    char const* path)
{
    assert(srv);
    if( g_ticklog.srv == srv )
        return g_ticklog.start_tick;
    ToriRSServer_TicklogDisable();
    g_ticklog.srv = srv;
    g_ticklog.start_tick = srv->tick;
    g_ticklog.dealer_npc = -1;
    if( path && *path )
    {
        g_ticklog.path = strdup(path);
        assert(g_ticklog.path);
        g_ticklog.out = fopen(path, "w");
        /* A log the test cannot write to a file is still a log: the rows are
         * kept in memory and read back through the driver. Say so once. */
        if( !g_ticklog.out )
            fprintf(stderr, "torirsserver: ticklog: cannot open %s; rows kept in memory only\n",
                    path);
        else
            fprintf(g_ticklog.out,
                    "ticklog-v1\tserial\ttick\tkind\ta\tb\tc\td\te\tf\tlabel\tg\n");
    }
    ticklog_snapshot_npc_tiles();
    ticklog_push(TORIRSSERVER_TICKLOG_START, srv->tick, 0, 0, 0, 0, 0, NULL);
    if( g_ticklog.out )
        fflush(g_ticklog.out);
    return g_ticklog.start_tick;
}

void
ToriRSServer_TicklogDisable(void)
{
    if( g_ticklog.out )
        fclose(g_ticklog.out);
    free(g_ticklog.rows);
    free(g_ticklog.path);
    g_ticklog.out = NULL;
    g_ticklog.rows = NULL;
    g_ticklog.path = NULL;
    g_ticklog.count = 0;
    g_ticklog.capacity = 0;
    g_ticklog.srv = NULL;
    g_ticklog.start_tick = -1;
    g_ticklog.dealer_npc = -1;
}

int
ToriRSServer_TicklogEnabled(const struct ToriRSServer* srv)
{
    assert(srv);
    return ticklog_on_for(srv);
}

int
ToriRSServer_TicklogStartTick(void)
{
    return g_ticklog.srv ? g_ticklog.start_tick : -1;
}

char const*
ToriRSServer_TicklogPath(void)
{
    return g_ticklog.srv && g_ticklog.out ? g_ticklog.path : NULL;
}

uint32_t
ToriRSServer_TicklogCount(void)
{
    return (uint32_t)g_ticklog.count;
}

int
ToriRSServer_TicklogRead(
    uint32_t after_serial,
    struct ToriRSServerTicklogRow* out,
    int max)
{
    int copied = 0;

    assert(out);
    assert(max > 0);
    /* Serial n lives at index n - 1: rows are never removed while the log is
     * on, so the read is a slice, not a scan. */
    for( uint32_t i = after_serial; i < (uint32_t)g_ticklog.count && copied < max; i++ )
        out[copied++] = g_ticklog.rows[i];
    return copied;
}

/* The npc slot a row is about, or -1 for a kind that names none. */
static int
ticklog_row_npc_slot(const struct ToriRSServerTicklogRow* row)
{
    switch( row->kind )
    {
    case TORIRSSERVER_TICKLOG_NPC_ANIM:
    case TORIRSSERVER_TICKLOG_NPC_SPOTANIM:
    case TORIRSSERVER_TICKLOG_HIT_NPC:
    case TORIRSSERVER_TICKLOG_NPC_SPAWN:
    case TORIRSSERVER_TICKLOG_NPC_DEATH:
    case TORIRSSERVER_TICKLOG_NPC_FREE:
    case TORIRSSERVER_TICKLOG_NPC_RETYPE:
    case TORIRSSERVER_TICKLOG_NPC_TILE:
    case TORIRSSERVER_TICKLOG_NPC_FACE:
    case TORIRSSERVER_TICKLOG_NPC_SAY:
    case TORIRSSERVER_TICKLOG_NPC_HEAL:
        return row->a;
    case TORIRSSERVER_TICKLOG_HIT_PLAYER:
        return row->b;
    case TORIRSSERVER_TICKLOG_SOUND:
        /* Only an npc's own noise names an npc; its slot rides in the label
         * because all six fields carry the send itself. */
        if( strncmp(row->label, "npc ", 4) == 0 )
            return atoi(row->label + 4);
        return -1;
    default:
        return -1;
    }
}

int
ToriRSServer_TicklogReadFiltered(
    uint32_t after_serial,
    int kind,
    int slot,
    struct ToriRSServerTicklogRow* out,
    int max,
    uint32_t* out_scanned)
{
    int copied = 0;
    uint32_t i = after_serial;

    assert(out);
    assert(max > 0);
    assert(out_scanned);
    for( ; i < (uint32_t)g_ticklog.count && copied < max; i++ )
    {
        const struct ToriRSServerTicklogRow* row = &g_ticklog.rows[i];

        if( kind >= 0 && row->kind != kind )
            continue;
        if( slot >= 0 && ticklog_row_npc_slot(row) != slot )
            continue;
        out[copied++] = *row;
    }
    /* Serial n is index n - 1, so the loop index is the last serial scanned. */
    *out_scanned = i > after_serial ? i : after_serial;
    return copied;
}

uint32_t
ToriRSServer_TicklogMark(char const* label)
{
    uint32_t serial;

    assert(label);
    if( !g_ticklog.srv )
        return 0;
    serial = ticklog_push(TORIRSSERVER_TICKLOG_MARK, 0, 0, 0, 0, 0, 0, label);
    if( g_ticklog.out )
        fflush(g_ticklog.out);
    return serial;
}

/* ------------------------------------------------------------------ hooks */

void
ToriRSServer_TicklogNpcAnim(
    const struct ToriRSServerNpc* npc,
    int seq_id,
    int delay)
{
    int slot;

    if( !g_ticklog.srv )
        return;
    slot = ticklog_npc_slot(npc);
    if( slot < 0 )
        return;
    ticklog_push(TORIRSSERVER_TICKLOG_NPC_ANIM, slot, npc->type, seq_id, delay, 0, 0, NULL);
}

void
ToriRSServer_TicklogNpcSpotanim(
    const struct ToriRSServerNpc* npc,
    int spotanim,
    int height,
    int delay)
{
    int slot;

    if( !g_ticklog.srv )
        return;
    slot = ticklog_npc_slot(npc);
    if( slot < 0 )
        return;
    ticklog_push(TORIRSSERVER_TICKLOG_NPC_SPOTANIM, slot, npc->type, spotanim, height, delay, 0,
                 NULL);
}

void
ToriRSServer_TicklogProjectile(
    const struct ToriRSServer* srv,
    int src_coord,
    int dst_coord,
    int target,
    int spotanim,
    int start_cycle,
    int end_cycle)
{
    if( !ticklog_on_for(srv) )
        return;
    ticklog_push(TORIRSSERVER_TICKLOG_PROJECTILE, src_coord, dst_coord, target, spotanim,
                 start_cycle, end_cycle, NULL);
}

void
ToriRSServer_TicklogMapSpotanim(
    const struct ToriRSServer* srv,
    int coord,
    int spotanim,
    int height,
    int delay)
{
    if( !ticklog_on_for(srv) )
        return;
    ticklog_push(TORIRSSERVER_TICKLOG_MAP_SPOTANIM, coord, spotanim, height, delay, 0, 0, NULL);
}

void
ToriRSServer_TicklogHitPlayer(
    const struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* player,
    int damage,
    int hitsplat,
    int dealer_pid,
    int raw)
{
    int npc_slot;
    int npc_type = -1;

    if( !ticklog_on_for(srv) )
        return;
    assert(player);
    npc_slot = g_ticklog.dealer_npc;
    if( npc_slot >= 0 && npc_slot < TORIRSSERVER_NPC_MAX )
        npc_type = srv->npcs[npc_slot].type;
    else
        npc_slot = -1;
    /* A script damaging from an npc runs with the victim active, and the
     * `damage` opcode then names the active player as the dealer -- the
     * victim. That is not a player-versus-player hit, so the row names only
     * the npc. */
    if( npc_slot >= 0 && dealer_pid == player->pid )
        dealer_pid = -1;
    ticklog_push_row(TORIRSSERVER_TICKLOG_HIT_PLAYER, player->pid, npc_slot, damage, hitsplat,
                     dealer_pid, npc_type, raw, NULL);
}

void
ToriRSServer_TicklogHitNpc(
    const struct ToriRSServer* srv,
    int slot,
    int damage,
    int hitsplat,
    int raw)
{
    if( !ticklog_on_for(srv) )
        return;
    assert(slot >= 0);
    assert(slot < TORIRSSERVER_NPC_MAX);
    ticklog_push(TORIRSSERVER_TICKLOG_HIT_NPC, slot, srv->npcs[slot].type, damage, hitsplat, raw, 0,
                 NULL);
}

void
ToriRSServer_TicklogNpcSpawn(
    const struct ToriRSServer* srv,
    int slot)
{
    const struct ToriRSServerNpc* npc;
    int32_t coord;

    if( !ticklog_on_for(srv) )
        return;
    assert(slot >= 0);
    assert(slot < TORIRSSERVER_NPC_MAX);
    npc = &srv->npcs[slot];
    coord = ToriRSServer_CoordPack(npc->level, npc->x, npc->z);
    /* The spawn row carries the tile, so the first NPC_TILE row is the first
     * step, not the spawn. */
    g_ticklog.npc_tile_plus1[slot] = coord + 1;
    g_ticklog.npc_generation[slot] = npc->generation;
    ticklog_push(TORIRSSERVER_TICKLOG_NPC_SPAWN, slot, npc->type, coord, 0, 0, 0, NULL);
}

void
ToriRSServer_TicklogNpcDeath(
    const struct ToriRSServer* srv,
    int slot)
{
    const struct ToriRSServerNpc* npc;

    if( !ticklog_on_for(srv) )
        return;
    assert(slot >= 0);
    assert(slot < TORIRSSERVER_NPC_MAX);
    npc = &srv->npcs[slot];
    ticklog_push(TORIRSSERVER_TICKLOG_NPC_DEATH, slot, npc->type,
                 ToriRSServer_CoordPack(npc->level, npc->x, npc->z), 0, 0, 0, NULL);
}

void
ToriRSServer_TicklogNpcFree(
    const struct ToriRSServer* srv,
    int slot)
{
    const struct ToriRSServerNpc* npc;

    if( !ticklog_on_for(srv) )
        return;
    assert(slot >= 0);
    assert(slot < TORIRSSERVER_NPC_MAX);
    npc = &srv->npcs[slot];
    g_ticklog.npc_tile_plus1[slot] = 0;
    ticklog_push(TORIRSSERVER_TICKLOG_NPC_FREE, slot, npc->type,
                 ToriRSServer_CoordPack(npc->level, npc->x, npc->z), 0, 0, 0, NULL);
}

void
ToriRSServer_TicklogNpcFace(
    const struct ToriRSServerNpc* npc,
    int x,
    int z)
{
    int slot;

    if( !g_ticklog.srv )
        return;
    slot = ticklog_npc_slot(npc);
    if( slot < 0 )
        return;
    ticklog_push(TORIRSSERVER_TICKLOG_NPC_FACE, slot, npc->type, x, z, 0, 0, NULL);
}

void
ToriRSServer_TicklogNpcRetype(
    const struct ToriRSServerNpc* npc,
    int from_type,
    int to_type,
    int duration)
{
    int slot;

    if( !g_ticklog.srv )
        return;
    slot = ticklog_npc_slot(npc);
    if( slot < 0 )
        return;
    ticklog_push(TORIRSSERVER_TICKLOG_NPC_RETYPE, slot, from_type, to_type, duration, 0, 0, NULL);
}

void
ToriRSServer_TicklogLocSet(
    const struct ToriRSServer* srv,
    int coord,
    int loc_id,
    int shape,
    int angle,
    int kind)
{
    if( !ticklog_on_for(srv) )
        return;
    ticklog_push(TORIRSSERVER_TICKLOG_LOC_SET, coord, loc_id, shape, angle, kind, 0, NULL);
}

void
ToriRSServer_TicklogObjAdd(
    const struct ToriRSServer* srv,
    int coord,
    int obj_id,
    int count,
    int receiver_pid)
{
    if( !ticklog_on_for(srv) )
        return;
    ticklog_push(TORIRSSERVER_TICKLOG_OBJ_ADD, coord, obj_id, count, receiver_pid, 0, 0, NULL);
}

/*
 * The audio rows. One row per packet per player: a sound is a per-player
 * SYNTH_SOUND, so an area sound heard by three players is three rows with the
 * same tick, id and source tile. Called only where the packet is actually
 * sent (a negative "no sound" id never reaches here), so a row is exactly
 * what the player's client was told to play.
 */
void
ToriRSServer_TicklogSound(
    const struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* player,
    int sound,
    int loops,
    int delay,
    int source,
    int coord,
    int radius,
    int npc_slot)
{
    char label[TORIRSSERVER_TICKLOG_LABEL_MAX];

    if( !ticklog_on_for(srv) )
        return;
    assert(player);
    assert(source >= 0);
    assert(source < TORIRSSERVER_TICKLOG_SOUND_SOURCE_COUNT);
    if( source == TORIRSSERVER_TICKLOG_SOUND_NPC )
    {
        assert(npc_slot >= 0);
        assert(npc_slot < TORIRSSERVER_NPC_MAX);
        snprintf(label, sizeof(label), "npc %d %d", npc_slot, srv->npcs[npc_slot].type);
    }
    else
        snprintf(label, sizeof(label), "%s", k_sound_source_names[source]);
    ticklog_push(TORIRSSERVER_TICKLOG_SOUND, player->pid, sound, loops, delay, coord, radius,
                 label);
}

void
ToriRSServer_TicklogMusic(
    const struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* player,
    int track,
    int source)
{
    if( !ticklog_on_for(srv) )
        return;
    assert(player);
    assert(source >= 0);
    assert(source < TORIRSSERVER_TICKLOG_MUSIC_SOURCE_COUNT);
    ticklog_push(TORIRSSERVER_TICKLOG_MUSIC, player->pid, track, 0, 0, 0, 0,
                 k_music_source_names[source]);
}

void
ToriRSServer_TicklogJingle(
    const struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* player,
    int jingle,
    int length_ms)
{
    if( !ticklog_on_for(srv) )
        return;
    assert(player);
    ticklog_push(TORIRSSERVER_TICKLOG_JINGLE, player->pid, jingle, length_ms, 0, 0, 0, "script");
}

/*
 * The presentation rows. Each is recorded where the mask or zone packet is
 * written, so a row is what the client was told: a player animation only once
 * it has won the priority gate (a refused one changes nothing on the wire), a
 * player graphic and a loc animation on every send (neither has a gate), an
 * npc's line as the SAY mask carries it. Bloat's stun graphic on the player,
 * Sotetseg's and Xarpus's loc loops and Xarpus's screech are these kinds.
 */
void
ToriRSServer_TicklogPlayerAnim(
    const struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* player,
    int seq_id,
    int delay)
{
    if( !ticklog_on_for(srv) )
        return;
    assert(player);
    ticklog_push(TORIRSSERVER_TICKLOG_PLAYER_ANIM, player->pid, seq_id, delay, 0, 0, 0, NULL);
}

void
ToriRSServer_TicklogPlayerSpotanim(
    const struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* player,
    int spotanim,
    int height,
    int delay)
{
    if( !ticklog_on_for(srv) )
        return;
    assert(player);
    ticklog_push(TORIRSSERVER_TICKLOG_PLAYER_SPOTANIM, player->pid, spotanim, height, delay, 0, 0,
                 NULL);
}

void
ToriRSServer_TicklogLocAnim(
    const struct ToriRSServer* srv,
    int coord,
    int loc_id,
    int shape,
    int angle,
    int seq_id)
{
    if( !ticklog_on_for(srv) )
        return;
    ticklog_push(TORIRSSERVER_TICKLOG_LOC_ANIM, coord, loc_id, shape, angle, seq_id, 0, NULL);
}

void
ToriRSServer_TicklogNpcSay(
    const struct ToriRSServerNpc* npc,
    char const* text)
{
    int slot;

    if( !g_ticklog.srv )
        return;
    assert(npc);
    assert(text);
    slot = ticklog_npc_slot(npc);
    if( slot < 0 )
        return;
    ticklog_push(TORIRSSERVER_TICKLOG_NPC_SAY, slot, npc->type,
                 ToriRSServer_CoordPack(npc->level, npc->x, npc->z), 0, 0, 0, text);
}

/*
 * An npc GAINED hitpoints (raid loop seam9): `npc_statheal` or `npc_statadd`
 * on hitpoints, recorded only when the level actually rose, so a heal that a
 * full npc's clamp ate is no row. The HIT_NPC row's mirror -- a hit is damage
 * dealt, this is damage taken back -- and the label names the script whose
 * opcode did it (the Maiden's blood-spawn absorb, Verzik's Athanatos), which is
 * the "source" a tick ledger needs to tell one heal from another.
 */
void
ToriRSServer_TicklogNpcHeal(
    const struct ToriRSServerNpc* npc,
    int gained,
    int after,
    int base,
    char const* source)
{
    int slot;

    if( !g_ticklog.srv )
        return;
    assert(npc);
    assert(source);
    slot = ticklog_npc_slot(npc);
    if( slot < 0 )
        return;
    ticklog_push(TORIRSSERVER_TICKLOG_NPC_HEAL, slot, npc->type, gained, after, base, 0, source);
}

/* NPC_TILE rows are for the encounter, not the world: the roster stands a
 * thousand wandering npcs up, and a row for each of their steps would bury the
 * room's dozen. An npc further than this from every player is not in anyone's
 * room (the largest raid room, Verzik's, spans 32 tiles). */
#define TICKLOG_NPC_TILE_RADIUS 32

static int
ticklog_near_a_player(
    const struct ToriRSServer* srv,
    int x,
    int z)
{
    for( int pid = 0; pid < srv->player_count; pid++ )
    {
        const struct ToriRSServerPlayer* player = &srv->players[pid];
        int dx;
        int dz;

        if( !player->active )
            continue;
        dx = x - player->x;
        dz = z - player->z;
        if( dx < 0 )
            dx = -dx;
        if( dz < 0 )
            dz = -dz;
        if( dx <= TICKLOG_NPC_TILE_RADIUS && dz <= TICKLOG_NPC_TILE_RADIUS )
            return 1;
    }
    return 0;
}

/* A player varp by its content symbol, or -1 when the pack does not name it. */
static int
ticklog_player_varp(
    const struct ToriRSServerPlayer* player,
    int varp)
{
    assert(player);
    if( varp < 0 || varp >= TORIRSSERVER_VARP_COUNT )
        return -1;
    return player->varps[varp];
}

/* The RAIDER row: what Blert's PLAYER_UPDATE carries, from the server's own
 * state at the end of the tick (after phase_players). */
static void
ticklog_raider_row(
    const struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* player,
    int prayer_varp,
    int style_varp,
    int spec_varp)
{
    char label[TORIRSSERVER_TICKLOG_LABEL_MAX];
    int target = -1;
    int input;

    assert(srv);
    assert(player);
    assert(player->pid >= 0);
    assert(player->pid < TORIRSSERVER_PLAYER_MAX);
    if( player->interaction.kind == TORIRSSERVER_INTERACT_NPC )
        target = player->interaction.npc_slot;
    input = player->last_input_tick != g_ticklog.input_seen[player->pid];
    g_ticklog.input_seen[player->pid] = player->last_input_tick;
    snprintf(label, sizeof(label), "hpmax %d prmax %d head %d input %d tgt %d",
             player->stat_level[TORIRSSERVER_STAT_HITPOINTS],
             player->stat_level[TORIRSSERVER_STAT_PRAYER], player->headicons,
             input, target);
    ticklog_write_side(TORIRSSERVER_TICKLOG_RAIDER, player->pid,
                       player->stat_boosted[TORIRSSERVER_STAT_HITPOINTS],
                       player->stat_boosted[TORIRSSERVER_STAT_PRAYER],
                       ticklog_player_varp(player, prayer_varp),
                       player->worn[TORIRSSERVER_WEAR_WEAPON].obj_id,
                       ticklog_player_varp(player, style_varp),
                       ticklog_player_varp(player, spec_varp), label);
}

void
ToriRSServer_TicklogInput(
    const struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* player,
    int trigger,
    int type,
    int npc_slot,
    char const* label)
{
    assert(srv);
    assert(player);
    assert(label);
    if( !ticklog_on_for(srv) )
        return;
    ticklog_write_side(TORIRSSERVER_TICKLOG_INPUT, player->pid, trigger, type, npc_slot, 0, 0, 0,
                       label);
}

void
ToriRSServer_TicklogConsume(
    const struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* player,
    int obj,
    int op,
    int hitpoints_before,
    int prayer_before,
    char const* label)
{
    assert(srv);
    assert(player);
    assert(label);
    if( !ticklog_on_for(srv) )
        return;
    ticklog_write_side(TORIRSSERVER_TICKLOG_CONSUME, player->pid, obj, op, hitpoints_before,
                       player->stat_boosted[TORIRSSERVER_STAT_HITPOINTS], prayer_before,
                       player->stat_boosted[TORIRSSERVER_STAT_PRAYER], label);
}

void
ToriRSServer_TicklogTickEnd(struct ToriRSServer* srv)
{
    int prayer_varp;
    int style_varp;
    int spec_varp;

    assert(srv);
    if( !ticklog_on_for(srv) )
        return;
    for( int pid = 0; pid < srv->player_count; pid++ )
    {
        const struct ToriRSServerPlayer* player = &srv->players[pid];

        if( !player->active )
            continue;
        ticklog_push(TORIRSSERVER_TICKLOG_PLAYER_TILE, player->pid, player->x, player->z,
                     player->level, 0, 0, NULL);
    }
    for( int slot = 0; slot < srv->npc_slot_max && slot < TORIRSSERVER_NPC_MAX; slot++ )
    {
        const struct ToriRSServerNpc* npc = &srv->npcs[slot];
        int32_t now;

        if( !npc->active )
            continue;
        if( !ticklog_near_a_player(srv, npc->x, npc->z) )
            continue;
        now = ToriRSServer_CoordPack(npc->level, npc->x, npc->z) + 1;
        if( g_ticklog.npc_tile_plus1[slot] == now &&
            g_ticklog.npc_generation[slot] == npc->generation )
            continue;
        g_ticklog.npc_tile_plus1[slot] = now;
        g_ticklog.npc_generation[slot] = npc->generation;
        /* f = the footprint, for a reader's reach test (raid_report.py). */
        ticklog_push(TORIRSSERVER_TICKLOG_NPC_TILE, slot, npc->x, npc->z, npc->level, npc->type,
                     npc->size, NULL);
    }
    /* LAST, after the tick's real rows, so a reader that skips the file-only
     * kinds sees the old file. The varps are resolved per tick: the content
     * tree loads after the world (torirs_server_world.c attack_style_varp). */
    prayer_varp = ToriRSServer_WorldVarp("varp83_prayer0");
    style_varp = ToriRSServer_WorldVarp("varp43_com_mode");
    spec_varp = ToriRSServer_WorldVarp("varp300_sa_energy");
    for( int pid = 0; pid < srv->player_count; pid++ )
    {
        const struct ToriRSServerPlayer* player = &srv->players[pid];

        if( !player->active )
            continue;
        ticklog_raider_row(srv, player, prayer_varp, style_varp, spec_varp);
    }
    if( g_ticklog.out )
        fflush(g_ticklog.out);
}

void
ToriRSServer_TicklogSetDealerNpc(int slot)
{
    g_ticklog.dealer_npc = slot;
}
