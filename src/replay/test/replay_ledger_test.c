/*
 * test-replay-ledger: the world ledger (replay/replay_ledger.h) over a
 * synthetic rev-241 recording, built here byte for byte in RSProx's BinaryBlob
 * layout (as net/test/rsprox_replay_test.c builds its own).
 *
 * The recording: a login REBUILD carrying the player-info init block, then 700
 * ticks of PLAYER_INFO (the server's own writer, torirsserver/mock239_playerinfo
 * with the 241 tail: players entering with an appearance, moving, leaving),
 * NPC_INFO (written here bit by bit to the 241 reader's layout: kept, walking,
 * removed, added and re-added npcs), and zone packets through RSProt's own
 * encoders (UPDATE_ZONE_PARTIAL_FOLLOWS + LOC_ADD_CHANGE / LOC_DEL,
 * UPDATE_ZONE_PARTIAL_ENCLOSED of OBJ_ADD / OBJ_DEL / OBJ_COUNT / LOC_*,
 * UPDATE_ZONE_FULL_FOLLOWS), with a second REBUILD at tick 300 that moves the
 * build area 48 tiles east.
 *
 * Checked: the interval invariants; at every tick the live ops equal a replay
 * of the whole history; at every tick a key advanced to it equals a fold from
 * tick 0; the npc list order, types and tiles against what the generator
 * wrote; the players against what it wrote, appearance blobs included; and the
 * session's player table (net/proto/proto_info.c) byte-identical before and
 * after.
 */
#include "replay/replay_ledger.h"
#include "replay/replay_ledger_verify.h"

#include "mem/torirs_mem.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/revpacket.h"
#include "net/rsprox_recording.h"
#include "rsareabuf.h"
#include "rsprot_buf.h"
#include "rsprot_exec.h"
#include "rsprot_rev.h"
#include "rsprot_tables.h"
#include "packets/loc_add_change_v2.h"
#include "packets/loc_del.h"
#include "packets/npc_headicon_specific.h"
#include "packets/obj_add.h"
#include "packets/obj_enabled_ops_specific.h"
#include "packets/obj_count.h"
#include "packets/obj_del.h"
#include "packets/update_zone_full_follows.h"
#include "packets/update_zone_partial_enclosed.h"
#include "packets/update_zone_partial_follows.h"
#include "streams/rsprot_info.h"
#include "torirsserver/mock239_playerinfo.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CHECK(cond)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);                     \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

#define LOCAL_INDEX 827
#define TICKS 700
#define REBUILD_TICK 300
#define MAX_NPCS 32
#define REMOTES 5

/* --- writing a recording ------------------------------------------------- */

struct Rec
{
    uint8_t* data;
    size_t len;
    size_t cap;
    uint64_t last_ms;
};

static void
w1(struct Rec* r, int v)
{
    if( r->len == r->cap )
    {
        r->cap = r->cap ? r->cap * 2 : 65536;
        r->data = ToriMem_Realloc(ToriMem_Net(), r->data, r->cap);
        assert(r->data);
    }
    r->data[r->len++] = (uint8_t)v;
}

static void
w2(struct Rec* r, int v)
{
    w1(r, v >> 8);
    w1(r, v);
}

static void
w4(struct Rec* r, uint32_t v)
{
    w2(r, (int)(v >> 16));
    w2(r, (int)(v & 0xffff));
}

static void
wstr(struct Rec* r, char const* s)
{
    while( *s )
        w1(r, *s++);
    w1(r, 0);
}

static void
w_header(struct Rec* r)
{
    w4(r, 1);   /* headerVersion */
    w4(r, 241); /* revision */
    w4(r, 1);   /* subRevision */
    w1(r, 1);   /* clientType */
    w1(r, 5);   /* platformType */
    w4(r, 0x0000019a);
    w4(r, 0x12345678); /* timestamp */
    w2(r, 631);        /* worldId */
    w4(r, 1);          /* worldFlags */
    w1(r, 0);          /* worldLocation */
    wstr(r, "oldschool331.runescape.com");
    wstr(r, "-");
    w2(r, LOCAL_INDEX);
    w2(r, 3); /* accountHash */
    w1(r, 0xaa);
    w1(r, 0xbb);
    w1(r, 0xcc);
    wstr(r, "RuneLite (Win)");
    w4(r, 2); /* js5MasterIndex */
    w1(r, 0);
    w1(r, 0);
}

static void
w_packet_head(
    struct Rec* r,
    uint64_t t_ms,
    int opcode)
{
    uint32_t const packed = (uint32_t)((t_ms - r->last_ms) << 1) | 1u;
    uint8_t groups[5];
    int n = 0;

    r->last_ms = t_ms;
    groups[n++] = (uint8_t)(packed & 0x7f);
    for( uint32_t v = packed >> 7; v; v >>= 7 )
        groups[n++] = (uint8_t)((v & 0x7f) | 0x80);
    while( n > 0 )
        w1(r, groups[--n]);
    if( opcode >= 0x80 )
    {
        w1(r, 0x80 | (opcode >> 8));
        w1(r, opcode & 0xff);
    }
    else
        w1(r, opcode);
}

static struct GameProtoRevTable const* g_rev;
static uint64_t g_ms;

static void
w_server(
    struct Rec* r,
    int pkt_name,
    uint8_t const* payload,
    int len)
{
    int const op = g_rev->packetin_wire(pkt_name);
    int const size = g_rev->packetin_size(op);

    TEST_CHECK(op >= 0);
    w_packet_head(r, g_ms, op);
    if( size == PKTIN_LENGTH_VARU8 )
        w1(r, len);
    else if( size == PKTIN_LENGTH_VARU16 )
        w2(r, len);
    else
        TEST_CHECK(size == len);
    for( int i = 0; i < len; i++ )
        w1(r, payload[i]);
}

/* REBUILD_NORMAL_V2's six bytes (StaticRebuildV2Decoder): g2 zoneZ, g2Alt1
 * zoneX, g2s worldArea. */
static void
rebuild_tail(
    uint8_t* out,
    int zone_x,
    int zone_z)
{
    out[0] = (uint8_t)(zone_z >> 8);
    out[1] = (uint8_t)zone_z;
    out[2] = (uint8_t)zone_x;
    out[3] = (uint8_t)(zone_x >> 8);
    out[4] = 0;
    out[5] = 0;
}

/* --- RSProt's encoders ---------------------------------------------------- */

/* One packet through its RSProt encoder; returns the bytes written. */
static int
encode(
    RsprotCodecFn codec,
    void* msg,
    uint8_t* out,
    int cap)
{
    RsprotBuf b;
    RsprotExec x;

    rsprot_buf_wrap(&b, out, cap);
    rsprot_exec_encode(&x, &b);
    codec(&x, msg);
    TEST_CHECK(b.err == 0);
    TEST_CHECK(rsprot_exec_ok(&x));
    return (int)b.wpos;
}

#define ENCODE(codec, msg, out, cap) encode((RsprotCodecFn)(codec), (msg), (out), (int)(cap))

static int
zone_ordinal(char const* prot)
{
    RsprotRevTable const* t = rsprot_rev_table(rsprot_osrs(241));

    TEST_CHECK(t);
    for( int i = 0; i < t->zone_count; i++ )
        if( strcmp(t->zone_order[i], prot) == 0 )
            return i;
    TEST_CHECK(!"a zone prot 241 does not enclose");
    return -1;
}

static int
enc_loc_add(uint8_t* out, int cap, int pos, int shape, int angle, int loc_id)
{
    MsgLocAddChangeV2 m;

    memset(&m, 0, sizeof(m));
    m.coord_in_zone_packed = pos;
    m.loc_properties_packed = shape << 2 | angle;
    m.op_flags = 0x1f;
    m.id = loc_id;
    return ENCODE(packet_loc_add_change_v2_osrs241_out, &m, out, cap);
}

static int
enc_loc_del(uint8_t* out, int cap, int pos, int shape, int angle)
{
    MsgLocDel m;

    memset(&m, 0, sizeof(m));
    m.coord_in_zone_packed = pos;
    m.loc_properties_packed = shape << 2 | angle;
    return ENCODE(packet_loc_del_osrs241_out, &m, out, cap);
}

static int
enc_obj_add(uint8_t* out, int cap, int pos, int obj, int count)
{
    MsgObjAdd m;

    memset(&m, 0, sizeof(m));
    m.coord_in_zone_packed = pos;
    m.id = obj;
    m.quantity = count;
    m.op_flags = 0x1f;
    return ENCODE(packet_obj_add_osrs241_out, &m, out, cap);
}

static int
enc_obj_del(uint8_t* out, int cap, int pos, int obj)
{
    MsgObjDel m;

    memset(&m, 0, sizeof(m));
    m.coord_in_zone_packed = pos;
    m.id = obj;
    m.quantity = 1;
    return ENCODE(packet_obj_del_osrs241_out, &m, out, cap);
}

static int
enc_obj_count(uint8_t* out, int cap, int pos, int obj, int old_count, int new_count)
{
    MsgObjCount m;

    memset(&m, 0, sizeof(m));
    m.coord_in_zone_packed = pos;
    m.id = obj;
    m.old_quantity = old_count;
    m.new_quantity = new_count;
    return ENCODE(packet_obj_count_osrs241_out, &m, out, cap);
}

/* --- the generator --------------------------------------------------------- */

static uint32_t g_rng = 0x2545f491u;

static uint32_t
rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static int
chance(int percent)
{
    return (int)(rnd() % 100u) < percent;
}

static int
range(
    int lo,
    int hi)
{
    return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

/* World_CoordStep: 0 north-west .. 7 south-east. */
static int const k_dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static int const k_dz[8] = { 1, 1, 1, 0, 0, -1, -1, -1 };

struct GenNpc
{
    int slot;
    int type;
    int x;
    int z;
};

struct GenPlayer
{
    int pid;
    int visible;
    int x;
    int z;
    uint8_t look[24];
};

/* What the generator meant, per tick: the state in front of tick + 1. */
struct Expect
{
    int npc_count;
    struct GenNpc npcs[MAX_NPCS];
    int local_x;
    int local_z;
    struct GenPlayer players[REMOTES];
    uint8_t local_look[24];
};

static struct Expect g_expect[TICKS];

struct Bits
{
    uint8_t buf[2048];
    int pos;
};

static void
bits(struct Bits* b, int n, uint32_t v)
{
    for( int i = n - 1; i >= 0; i-- )
    {
        assert(b->pos < (int)sizeof(b->buf) * 8);
        if( (v >> i) & 1u )
            b->buf[b->pos >> 3] |= (uint8_t)(0x80 >> (b->pos & 7));
        b->pos++;
    }
}

static int32_t
coord(int x, int z, int level)
{
    return (int32_t)(((level & 3) << 28) | ((x & 16383) << 14) | (z & 16383));
}

struct Gen
{
    struct Rec rec;
    struct Mock239PlayerInfoState server;
    int local_x;
    int local_z;
    uint8_t local_look[24];
    struct GenPlayer players[REMOTES];
    struct GenNpc npcs[MAX_NPCS];
    int npc_count;
    int next_slot;
    int base_x;
    int base_z;
    int zones[4][2]; /* absolute zone corners */
    int revived;
    int walked;
    int removed;
    /** SPECIFIC_TICK's NPC_HEADICON_SPECIFIC: the npc it named (list head). */
    int headicon_npc;
    /** SPECIFIC_TICK's stack, absolute, and the ops it was given a tick later. */
    int stack_x;
    int stack_z;
};

/* After the rebuild: an obj stack added, its enabled ops set a tick later by
 * OBJ_ENABLED_OPS_SPECIFIC, and a head icon set on the npc list's head. */
#define SPECIFIC_TICK 400
#define SPECIFIC_OBJ 4151
#define SPECIFIC_OPS 0x05

static void
look_for(
    uint8_t* look,
    int pid,
    int tick)
{
    for( int i = 0; i < 24; i++ )
        look[i] = (uint8_t)(pid * 7 + tick * 13 + i * 31 + 1);
}

static void
gen_player_info(
    struct Gen* g,
    int tick)
{
    static uint8_t bytes[16384];
    struct Mock239PlayerUpdate updates[REMOTES + 1];
    struct RSAreaBuf buf;
    int n = 0;
    int teleport = tick == REBUILD_TICK;

    memset(updates, 0, sizeof(updates));
    updates[n].index = LOCAL_INDEX;
    updates[n].visible = 1;
    if( teleport )
    {
        g->local_x += 48;
        updates[n].movement = MOCK239_PLAYER_TELEPORT;
        updates[n].movement_value = coord(48, 0, 0);
    }
    updates[n].coord = coord(g->local_x, g->local_z, 0);
    if( tick == 0 )
    {
        look_for(g->local_look, LOCAL_INDEX, tick);
        updates[n].appearance = g->local_look;
        updates[n].appearance_len = 24;
    }
    n++;
    for( int i = 0; i < REMOTES; i++ )
    {
        struct GenPlayer* p = &g->players[i];
        int entering = 0;

        if( p->visible ? chance(4) : chance(8) )
        {
            p->visible = !p->visible;
            entering = p->visible;
            if( entering )
            {
                p->x = g->local_x + range(-12, 12);
                p->z = g->local_z + range(-12, 12);
                look_for(p->look, p->pid, tick);
            }
        }
        if( !p->visible )
            continue;
        updates[n].index = p->pid;
        updates[n].visible = 1;
        if( entering )
        {
            updates[n].appearance = p->look;
            updates[n].appearance_len = 24;
        }
        else if( teleport || chance(20) )
        {
            int dx = teleport ? 48 : range(-2, 2);
            int dz = teleport ? 0 : range(-2, 2);

            p->x += dx;
            p->z += dz;
            updates[n].movement = MOCK239_PLAYER_TELEPORT;
            updates[n].movement_value = coord(dx, dz, 0);
        }
        updates[n].coord = coord(p->x, p->z, 0);
        n++;
    }
    rsab_wrap(&buf, bytes, sizeof(bytes));
    mock239_playerinfo_write_world(&buf, &g->server, updates, n);
    TEST_CHECK(rsab_ok(&buf));
    w_server(&g->rec, PKT_NAME_PLAYER_INFO, bytes, (int)rsab_len(&buf));
}

/* NPC_INFO_SMALL_V6 at 241: the bit sections (rsprot_info_npc.c) and 241's
 * record order (npc_info_osrs241.c: dz, jump, angle, spawn cycle, extended,
 * type, dx); no extended blocks. */
static void
gen_npc_info(struct Gen* g)
{
    static struct Bits b;
    struct GenNpc kept[MAX_NPCS];
    struct GenNpc added[MAX_NPCS * 2];
    int kept_count = 0;
    int added_count = 0;

    memset(&b, 0, sizeof(b));
    bits(&b, 8, (uint32_t)g->npc_count);
    for( int i = 0; i < g->npc_count; i++ )
    {
        struct GenNpc* npc = &g->npcs[i];
        int r = range(0, 99);

        if( r < 70 )
        {
            bits(&b, 1, 0);
            kept[kept_count++] = *npc;
        }
        else if( r < 85 )
        {
            int dir = range(0, 7);

            bits(&b, 1, 1);
            bits(&b, 2, 1);
            bits(&b, 3, (uint32_t)dir);
            bits(&b, 1, 0);
            npc->x += k_dx[dir];
            npc->z += k_dz[dir];
            kept[kept_count++] = *npc;
            g->walked++;
        }
        else
        {
            bits(&b, 1, 1);
            bits(&b, 2, 3);
            g->removed++;
            /* Removed and added back in the same packet: the client keeps the
             * entity (task_exec_entity_info.c:1354) and it goes to the end. */
            if( r >= 95 )
            {
                struct GenNpc again = *npc;

                again.type = range(100, 3000);
                added[added_count++] = again;
                g->revived++;
            }
        }
    }
    while( kept_count + added_count < 24 && chance(30) )
    {
        struct GenNpc npc;

        npc.slot = g->next_slot++;
        npc.type = range(100, 3000);
        added[added_count++] = npc;
    }
    for( int i = 0; i < added_count; i++ )
    {
        struct GenNpc* npc = &added[i];
        int dx = range(-10, 10);
        int dz = range(-10, 10);

        npc->x = g->local_x + dx;
        npc->z = g->local_z + dz;
        bits(&b, 16, (uint32_t)npc->slot);
        bits(&b, 6, (uint32_t)dz & 63u);
        bits(&b, 1, 0);                    /* jump */
        bits(&b, 3, (uint32_t)range(0, 7)); /* spawn angle */
        bits(&b, 1, 0);                    /* no spawn cycle */
        bits(&b, 1, 0);                    /* no extended info */
        bits(&b, 2, 0);                    /* 12-bit type */
        bits(&b, 12, (uint32_t)npc->type);
        bits(&b, 6, (uint32_t)dx & 63u);
    }
    memcpy(g->npcs, kept, (size_t)kept_count * sizeof(kept[0]));
    memcpy(g->npcs + kept_count, added, (size_t)added_count * sizeof(added[0]));
    g->npc_count = kept_count + added_count;
    TEST_CHECK(g->npc_count <= MAX_NPCS);
    w_server(&g->rec, PKT_NAME_NPC_INFO, b.buf, (b.pos + 7) / 8);
}

static void
gen_zones(struct Gen* g)
{
    static int const objs[3] = { 995, 526, 1511 };
    static int const tiles[3] = { 0x12, 0x34, 0x56 };
    uint8_t payload[512];
    int which = range(0, 3);
    int zx = g->zones[which][0] - g->base_x;
    int zz = g->zones[which][1] - g->base_z;
    int len = 0;

    TEST_CHECK(zx >= 0 && zz >= 0 && zx < 104 && zz < 104);
    if( chance(4) )
    {
        MsgUpdateZoneFullFollows m = { .zone_x = zx, .zone_z = zz, .level = 0 };

        len = ENCODE(packet_update_zone_full_follows_osrs241_out, &m, payload, sizeof(payload));
        w_server(&g->rec, PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS, payload, len);
        return;
    }
    if( chance(40) )
    {
        MsgUpdateZonePartialFollows m = { .zone_x = zx, .zone_z = zz, .level = 0 };
        int pos = tiles[range(0, 2)];

        len = ENCODE(packet_update_zone_partial_follows_osrs241_out, &m, payload,
                     sizeof(payload));
        w_server(&g->rec, PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS, payload, len);
        if( chance(60) )
        {
            len = enc_loc_add(payload, sizeof(payload), pos, range(0, 1) ? 10 : 0, range(0, 3),
                              range(1, 4000));
            w_server(&g->rec, PKT_NAME_LOC_ADD_CHANGE, payload, len);
        }
        else
        {
            len = enc_loc_del(payload, sizeof(payload), pos, range(0, 1) ? 10 : 0, 0);
            w_server(&g->rec, PKT_NAME_LOC_DEL, payload, len);
        }
        return;
    }
    {
        uint8_t subs[400];
        int sub_len = 0;
        int entries = range(1, 4);
        MsgDesktopUpdateZonePartialEnclosed m;

        for( int e = 0; e < entries; e++ )
        {
            int pos = tiles[range(0, 2)];
            int obj = objs[range(0, 2)];
            int kind = range(0, 9);
            char const* prot;
            int n;

            if( kind < 4 )
            {
                prot = "OBJ_ADD";
                n = enc_obj_add(subs + sub_len + 1, 64, pos, obj, range(1, 4));
            }
            else if( kind < 6 )
            {
                prot = "OBJ_DEL";
                n = enc_obj_del(subs + sub_len + 1, 64, pos, obj);
            }
            else if( kind < 8 )
            {
                prot = "OBJ_COUNT";
                n = enc_obj_count(subs + sub_len + 1, 64, pos, obj, range(1, 4), range(1, 4));
            }
            else if( kind < 9 )
            {
                prot = "LOC_ADD_CHANGE_V2";
                n = enc_loc_add(subs + sub_len + 1, 64, pos, 10, range(0, 3), range(1, 4000));
            }
            else
            {
                prot = "LOC_DEL";
                n = enc_loc_del(subs + sub_len + 1, 64, pos, 10, 0);
            }
            subs[sub_len] = (uint8_t)zone_ordinal(prot);
            sub_len += 1 + n;
        }
        memset(&m, 0, sizeof(m));
        m.zone_x = zx;
        m.zone_z = zz;
        m.level = 0;
        m.payload = subs;
        m.payload_length = sub_len;
        len = ENCODE(packet_update_zone_partial_enclosed_osrs241_out, &m, payload,
                     sizeof(payload));
        w_server(&g->rec, PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED, payload, len);

        /* The generator's own check: 241 parses what it wrote, whole. */
        {
            struct RevPacket pkt;
            int op = g_rev->packetin_wire(PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED);

            memset(&pkt, 0, sizeof(pkt));
            TEST_CHECK(g_rev->parse_wire(g_rev, op, payload, len, &pkt) == 1);
            TEST_CHECK(pkt._update_zone_enclosed.count == entries);
            TEST_CHECK(pkt._update_zone_enclosed.base_x == zx);
            gameproto_free(&pkt);
        }
    }
}

/* The *_SPECIFIC packets the ledger folds: no rng drawn, so the rest of the
 * recording is what it was. The stack's zone is none of the four gen_zones
 * writes, so nothing else touches it. */
static void
gen_specific(
    struct Gen* g,
    int tick)
{
    uint8_t payload[128];
    int len;

    if( tick == SPECIFIC_TICK )
    {
        MsgDesktopUpdateZonePartialEnclosed z;
        MsgNpcHeadIconSpecific h;
        uint8_t sub[64];
        int sub_len;

        g->stack_x = (g->local_x & ~7) + 16 + 2;
        g->stack_z = (g->local_z & ~7) + 16 + 3;
        sub[0] = (uint8_t)zone_ordinal("OBJ_ADD");
        sub_len = 1 + enc_obj_add(sub + 1, (int)sizeof(sub) - 1, (2 << 4) | 3, SPECIFIC_OBJ, 1);
        memset(&z, 0, sizeof(z));
        z.zone_x = ((g->local_x & ~7) + 16) - g->base_x;
        z.zone_z = ((g->local_z & ~7) + 16) - g->base_z;
        z.level = 0;
        z.payload = sub;
        z.payload_length = sub_len;
        len = ENCODE(packet_update_zone_partial_enclosed_osrs241_out, &z, payload, sizeof(payload));
        w_server(&g->rec, PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED, payload, len);

        TEST_CHECK(g->npc_count > 0);
        g->headicon_npc = g->npcs[0].slot;
        memset(&h, 0, sizeof(h));
        h.index = g->headicon_npc;
        h.head_icon_slot = 2;
        h.sprite_group = 440;
        h.sprite_index = 3;
        len = ENCODE(packet_npc_headicon_specific_osrs241_out, &h, payload, sizeof(payload));
        w_server(&g->rec, PKT_NAME_NPC_HEADICON_SPECIFIC, payload, len);
        /* An npc this client does not track: skipped, as the executor's
         * lookup misses. */
        h.index = 65000;
        len = ENCODE(packet_npc_headicon_specific_osrs241_out, &h, payload, sizeof(payload));
        w_server(&g->rec, PKT_NAME_NPC_HEADICON_SPECIFIC, payload, len);
    }
    else if( tick == SPECIFIC_TICK + 1 )
    {
        MsgObjEnabledOpsSpecific m;

        memset(&m, 0, sizeof(m));
        m.coord_grid_packed = coord(g->stack_x, g->stack_z, 0);
        m.id = SPECIFIC_OBJ;
        m.op_flags = SPECIFIC_OPS;
        len = ENCODE(packet_obj_enabled_ops_specific_osrs241_out, &m, payload, sizeof(payload));
        w_server(&g->rec, PKT_NAME_OBJ_ENABLED_OPS_SPECIFIC, payload, len);
    }
}

static void
set_zones(struct Gen* g)
{
    int lx = g->local_x & ~7;
    int lz = g->local_z & ~7;
    int const off[4][2] = { { -8, -8 }, { 0, 0 }, { 8, -8 }, { -16, 8 } };

    for( int i = 0; i < 4; i++ )
    {
        g->zones[i][0] = lx + off[i][0];
        g->zones[i][1] = lz + off[i][1];
    }
}

static void
build_recording(struct Gen* g)
{
    static uint8_t login[8192];
    struct RSAreaBuf buf;
    int len;

    memset(g, 0, sizeof(*g));
    g->local_x = 3200;
    g->local_z = 3200;
    g->next_slot = 100;
    for( int i = 0; i < REMOTES; i++ )
        g->players[i].pid = (int[]){ 5, 99, 400, 1203, 2000 }[i];
    w_header(&g->rec);

    /* The login REBUILD: the init block, then the six bytes. */
    rsab_wrap(&buf, login, sizeof(login));
    mock239_playerinfo_write_init(&buf, LOCAL_INDEX, coord(g->local_x, g->local_z, 0));
    len = (int)rsab_len(&buf);
    rebuild_tail(login + len, g->local_x >> 3, g->local_z >> 3);
    g_ms = 3;
    w_server(&g->rec, PKT_NAME_REBUILD_NORMAL, login, len + 6);
    g->base_x = ((g->local_x >> 3) - 6) * 8;
    g->base_z = ((g->local_z >> 3) - 6) * 8;
    set_zones(g);
    mock239_playerinfo_state_init(&g->server, LOCAL_INDEX, coord(g->local_x, g->local_z, 0));
    g->server.wire_revision = 241;

    for( int tick = 0; tick < TICKS; tick++ )
    {
        struct Expect* e = &g_expect[tick];

        g_ms = 600u * (uint64_t)tick + 10;
        gen_player_info(g, tick);
        if( tick == REBUILD_TICK )
        {
            uint8_t tail[6];

            rebuild_tail(tail, g->local_x >> 3, g->local_z >> 3);
            w_server(&g->rec, PKT_NAME_REBUILD_NORMAL, tail, 6);
            g->base_x = ((g->local_x >> 3) - 6) * 8;
            g->base_z = ((g->local_z >> 3) - 6) * 8;
            set_zones(g);
        }
        gen_npc_info(g);
        for( int k = range(0, 3); k > 0; k-- )
            gen_zones(g);
        gen_specific(g, tick);
        w_server(&g->rec, PKT_NAME_SERVER_TICK_END, NULL, 0);

        e->npc_count = g->npc_count;
        memcpy(e->npcs, g->npcs, (size_t)g->npc_count * sizeof(g->npcs[0]));
        e->local_x = g->local_x;
        e->local_z = g->local_z;
        memcpy(e->players, g->players, sizeof(g->players));
        memcpy(e->local_look, g->local_look, sizeof(e->local_look));
    }
}

/* --- checks ------------------------------------------------------------------ */

static void
check_keyframe(
    struct ReplayLedger* ledger,
    struct RsproxRecording const* rec,
    int tick)
{
    struct ReplayWorldKeyframe kf;
    struct Expect const* e = &g_expect[tick - 1];
    int visible = 1;

    (void)rec;

    TEST_CHECK(ReplayLedger_WorldAt(ledger, tick, &kf));
    TEST_CHECK(kf.tick == tick);
    TEST_CHECK(kf.world_entity == 0);
    TEST_CHECK(kf.scene >= 0);
    TEST_CHECK(kf.scene_row.base_x == ((e->local_x >> 3) - 6) * 8);
    TEST_CHECK(kf.local_index == LOCAL_INDEX);
    TEST_CHECK(kf.local_tile == (uint32_t)coord(e->local_x, e->local_z, 0));
    TEST_CHECK(kf.player_table_size > 0);

    /* Players: the local one and every visible remote, at their tiles, with
     * the appearance they entered with -- read from the recording's bytes. */
    for( int i = 0; i < REMOTES; i++ )
        visible += e->players[i].visible;
    TEST_CHECK(kf.player_count == visible);
    for( int i = 0; i < kf.player_count; i++ )
    {
        struct ReplayWorldPlayer const* p = &kf.players[i];
        uint8_t const* look = NULL;
        int x = e->local_x;
        int z = e->local_z;

        if( p->pid != LOCAL_INDEX )
        {
            int j = 0;

            while( j < REMOTES && e->players[j].pid != p->pid )
                j++;
            TEST_CHECK(j < REMOTES);
            TEST_CHECK(e->players[j].visible);
            look = e->players[j].look;
            x = e->players[j].x;
            z = e->players[j].z;
        }
        else
            look = e->local_look;
        TEST_CHECK(p->x == x);
        TEST_CHECK(p->z == z);
        TEST_CHECK(p->op_count == 1);
        TEST_CHECK(kf.ops[p->op_first].kind == RSPROT_OP_APPEARANCE);
        TEST_CHECK(kf.ops[p->op_first].appearance.len == 24);
        TEST_CHECK(memcmp(kf.ops[p->op_first].appearance.appearance, look, 24) == 0);
    }

    /* Npcs: in list order, at their tiles, with their spawn facing. */
    TEST_CHECK(kf.npc_count == e->npc_count);
    for( int i = 0; i < kf.npc_count; i++ )
    {
        struct ReplayWorldNpc const* npc = &kf.npcs[i];

        TEST_CHECK(npc->slot == e->npcs[i].slot);
        TEST_CHECK(npc->type == e->npcs[i].type);
        TEST_CHECK(npc->x == e->npcs[i].x);
        TEST_CHECK(npc->z == e->npcs[i].z);
        TEST_CHECK(npc->level == 0);
        TEST_CHECK(npc->op_count >= 1);
        TEST_CHECK(kf.ops[npc->op_first].kind == RSPROT_OP_FACE_ANGLE);
    }

    /* Zone ops decode, inside the scene, in birth order. */
    for( int i = 0; i < kf.zone_op_count; i++ )
    {
        struct ReplayWorldZoneOp const* z = &kf.zone_ops[i];

        TEST_CHECK(z->base_x >= 0 && z->base_x < 104);
        TEST_CHECK(z->base_z >= 0 && z->base_z < 104);
        TEST_CHECK(i == 0 || kf.zone_ops[i - 1].op < z->op);
    }

    /* No head icon before SPECIFIC_TICK's packet; after it, only on an npc the
     * list still holds. */
    if( tick <= SPECIFIC_TICK )
        TEST_CHECK(kf.npc_headicon_count == 0);
    for( int i = 0; i < kf.npc_headicon_count; i++ )
    {
        int held = 0;

        for( int j = 0; j < kf.npc_count; j++ )
            held += kf.npcs[j].slot == kf.npc_headicons[i].npc_slot;
        TEST_CHECK(held == 1);
    }
    ReplayWorldKeyframe_Free(&kf);
}

/* SPECIFIC_TICK's packets, at the keyframes a tick and two after them. */
static void
check_specific(
    struct ReplayLedger* ledger,
    struct Gen const* g)
{
    struct ReplayWorldKeyframe kf;
    int found = 0;

    /* In front of SPECIFIC_TICK + 1: the head icon, on the list's head, once
     * (the untracked npc's was skipped). */
    TEST_CHECK(ReplayLedger_WorldAt(ledger, SPECIFIC_TICK + 1, &kf));
    TEST_CHECK(kf.npc_count > 0);
    TEST_CHECK(kf.npcs[0].slot == g->headicon_npc);
    TEST_CHECK(kf.npc_headicon_count == 1);
    TEST_CHECK(kf.npc_headicons[0].npc_slot == g->headicon_npc);
    TEST_CHECK(kf.npc_headicons[0].icon_slot == 2);
    TEST_CHECK(kf.npc_headicons[0].group == 440);
    TEST_CHECK(kf.npc_headicons[0].index == 3);
    ReplayWorldKeyframe_Free(&kf);

    /* In front of SPECIFIC_TICK + 2: the stack, and its enabled ops as the
     * OBJ_REVEAL form exec_obj_specific turns the packet into. */
    TEST_CHECK(ReplayLedger_WorldAt(ledger, SPECIFIC_TICK + 2, &kf));
    for( int i = 0; i < kf.zone_op_count; i++ )
    {
        struct ReplayWorldZoneOp const* z = &kf.zone_ops[i];
        int x;
        int tz;

        if( z->pkt.name != PKT_NAME_OBJ_REVEAL || !z->pkt._obj_reveal.has_enabled_ops )
            continue;
        x = kf.scene_row.base_x + z->base_x + ((z->pkt._obj_reveal.pos >> 4) & 7);
        tz = kf.scene_row.base_z + z->base_z + (z->pkt._obj_reveal.pos & 7);
        TEST_CHECK(x == g->stack_x);
        TEST_CHECK(tz == g->stack_z);
        TEST_CHECK(z->pkt._obj_reveal.obj_id == SPECIFIC_OBJ);
        TEST_CHECK(z->pkt._obj_reveal.enabled_ops == SPECIFIC_OPS);
        found++;
    }
    TEST_CHECK(found == 1);
    ReplayWorldKeyframe_Free(&kf);
}

int
main(void)
{
    static struct Gen gen;
    struct RsproxRecording rec;
    struct ReplayLedger* ledger;
    struct ReplayLedgerStats st;
    static uint32_t frames[TICKS + 1];
    static int ticks[TICKS + 1];
    char err[256];
    char why[512];
    uint8_t* before;
    uint8_t* after;
    size_t size;
    int died = 0;
    int superseded = 0;
    int alive_end;

    g_rev = GameProtoRev_Osrs(241);
    TEST_CHECK(g_rev);
    build_recording(&gen);
    memset(&rec, 0, sizeof(rec));
    TEST_CHECK(RsproxRecording_Open(&rec, gen.rec.data, gen.rec.len, err, sizeof(err)) == 0);
    TEST_CHECK(RsproxRecording_Index(&rec, g_rev, err, sizeof(err)) == 0);
    TEST_CHECK(rec.tick_count == TICKS);
    rsprox_recording_resolve_reconnects(g_rev, &rec);

    /* The session's table holds something of its own while the ledger works. */
    size = g_rev->player_info_state_size();
    before = malloc(size);
    after = malloc(size);
    TEST_CHECK(before && after);
    g_rev->player_info_set_local(17);
    {
        uint8_t block[4608];

        for( int i = 0; i < (int)sizeof(block); i++ )
            block[i] = (uint8_t)(i * 37 + 11);
        g_rev->player_info_init(block, (int)sizeof(block));
    }
    g_rev->player_info_state_save(before);

    ledger = ReplayLedger_Build(&rec, g_rev);
    ReplayLedger_Stats(ledger, &st);
    TEST_CHECK(st.scenes == 2);
    TEST_CHECK(ReplayLedger_Scene(ledger, 0)->login == 1);
    TEST_CHECK(ReplayLedger_Scene(ledger, 1)->login == 0);
    TEST_CHECK(st.player_info_frames == TICKS);
    TEST_CHECK(st.player_info_incomplete == 0);
    TEST_CHECK(st.npc_info_frames == TICKS);
    TEST_CHECK(st.npc_info_incomplete == 0);
    TEST_CHECK(st.keys == TICKS / REPLAY_LEDGER_KEY_TICKS + 1);
    /* Every appearance is a reference into the recording, not a copy. */
    TEST_CHECK(st.appearance_refs > 0);
    TEST_CHECK(st.appearance_copies == 0);
    TEST_CHECK(st.zone_ops > 0);
    TEST_CHECK(st.zone_clears > 0);
    TEST_CHECK(gen.revived > 0);
    TEST_CHECK(gen.walked > 0);
    TEST_CHECK(gen.removed > 0);

    /* Intervals: the invariants, and that every kind of end happened. */
    TEST_CHECK(ReplayLedgerVerify_Intervals(ledger, why, sizeof(why)) || (puts(why), 0));
    for( int i = 0; i < st.zone_ops; i++ )
    {
        struct ReplayZoneOp const* op = ReplayLedger_ZoneOp(ledger, i);

        if( op->died != REPLAY_LEDGER_NEVER && op->died > op->born )
            superseded++;
        if( op->died == op->born )
            died++;
    }
    TEST_CHECK(superseded > 0);
    TEST_CHECK(died > 0);
    {
        /* The rebuild ended the ops of the two zones that left the area. */
        uint32_t rebuild = ReplayLedger_Scene(ledger, 1)->frame;
        int ended_by_rebuild = 0;

        for( int i = 0; i < st.zone_ops; i++ )
            if( ReplayLedger_ZoneOp(ledger, i)->died == rebuild )
                ended_by_rebuild++;
        TEST_CHECK(ended_by_rebuild > 0);
    }

    /* Every tick boundary: the live set against the whole history, and a key
     * advanced against a fold from tick 0. */
    for( int t = 0; t <= TICKS; t++ )
    {
        ticks[t] = t;
        frames[t] = ReplayLedger_FirstFrame(ledger, t);
    }
    if( !ReplayLedgerVerify_Zones(ledger, frames, TICKS + 1, why, sizeof(why)) )
    {
        fprintf(stderr, "FAIL zones: %s\n", why);
        abort();
    }
    if( !ReplayLedgerVerify_Entities(ledger, ticks, TICKS + 1, why, sizeof(why)) )
    {
        fprintf(stderr, "FAIL entities: %s\n", why);
        abort();
    }

    /* The npc list's order through removals, re-adds and adds, every tick. */
    {
        struct ReplayEntityState* fold = ReplayLedger_EntityNew(ledger);
        int slots[REPLAY_LEDGER_KEY_TICKS];

        for( int t = 1; t <= TICKS; t++ )
        {
            struct Expect const* e = &g_expect[t - 1];
            int n;

            ReplayLedger_EntityAdvance(ledger, fold, t);
            n = ReplayEntityState_NpcSlots(fold, slots, (int)(sizeof(slots) / sizeof(slots[0])));
            TEST_CHECK(n == e->npc_count);
            for( int i = 0; i < n; i++ )
                TEST_CHECK(slots[i] == e->npcs[i].slot);
        }
        ReplayEntityState_Free(fold);
    }

    /* Keyframes on and around the keys, and either side of the rebuild. */
    {
        int const at[] = { 1, 2, 100, 255, 256, 257, REBUILD_TICK, REBUILD_TICK + 1, 511, 512,
                           513, 650, TICKS };

        for( int i = 0; i < (int)(sizeof(at) / sizeof(at[0])); i++ )
            check_keyframe(ledger, &rec, at[i]);
        for( int t = SPECIFIC_TICK - 1; t <= SPECIFIC_TICK + 40; t++ )
            check_keyframe(ledger, &rec, t);
        check_specific(ledger, &gen);
    }
    {
        uint32_t* alive = malloc((size_t)(ReplayLedger_ZoneOpsAliveMax(ledger) + 1) * sizeof(*alive));

        TEST_CHECK(alive);
        alive_end = ReplayLedger_ZoneOpsAlive(ledger, ReplayLedger_FirstFrame(ledger, TICKS),
                                              alive, ReplayLedger_ZoneOpsAliveMax(ledger) + 1);
        free(alive);
    }
    TEST_CHECK(ReplayLedger_InWorldEntitySpan(ledger, 10) == 0);
    TEST_CHECK(ReplayLedger_LocalTile(ledger, TICKS) == (uint32_t)coord(gen.local_x, gen.local_z, 0));

    g_rev->player_info_state_save(after);
    TEST_CHECK(memcmp(before, after, size) == 0);

    printf("test-replay-ledger: %d frames, %d ticks, %d zone ops (%d superseded, %d left nothing, "
           "%d alive at the end, %d at most), %d clears, %d keys x %zu bytes, %d npcs walked, "
           "%d removed, %d re-added: ok\n",
           st.frames, st.ticks, st.zone_ops, superseded, died, alive_end, st.zone_ops_live_max,
           st.zone_clears, st.keys, st.bytes_per_key, gen.walked, gen.removed, gen.revived);

    ReplayLedger_Free(ledger);
    RsproxRecording_Free(&rec);
    free(before);
    free(after);
    return 0;
}
