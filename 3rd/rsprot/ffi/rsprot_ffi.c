/* rsprot_ffi. See rsprot_ffi.h. */
#include "rsprot_ffi.h"

#include "../gen/rsprot_tables.h"
#include "../include/rsprot_exec.h"
#include "../include/rsprot_registry.h"
#include "../src/rsprot_compression.h"
#include "../include/rsprot_trace.h"
#include "../streams/rsprot_info.h"

#include "pkt_codec_storage.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
rsprot_ffi_abi(void)
{
    return RSPROT_FFI_ABI;
}

int
rsprot_ffi_revision_first(void)
{
    return rsprot_all_revisions[0].revision;
}

int
rsprot_ffi_revision_last(void)
{
    return rsprot_all_revisions[RSPROT_REVISION_COUNT - 1].revision;
}

int
rsprot_ffi_prot(int revision, int client, int i, const char **name, int *opcode, int *size,
                const char **codec)
{
    const RsprotRevTable *t = rsprot_rev_table(revision);
    const RsprotProtDef *row;

    if (!t || i < 0 || i >= (client ? t->client_count : t->server_count))
        return -1;
    row = client ? &t->client[i] : &t->server[i];
    *name = row->name;
    *opcode = row->code;
    *size = row->size;
    *codec = row->codec;
    return 0;
}

const char *
rsprot_ffi_zone_prot(int revision, int ordinal)
{
    const RsprotRevTable *t = rsprot_rev_table(revision);

    return t ? rsprot_zone_prot_name(t, ordinal) : NULL;
}

/* The row `prot` names at `revision`, for its codec field; NULL when the
 * table has no such row (a zone sub-prot, or a name a host spells itself). */
static const RsprotProtDef *
row_named(int revision, int client, const char *prot)
{
    const RsprotRevTable *t = rsprot_rev_table(revision);

    if (!t)
        return NULL;
    return client ? rsprot_find_client_prot(t, prot) : rsprot_find_server_prot(t, prot);
}

/* Large enough for every message (CLANSETTINGS_FULL's is ~120 KB). */
static _Thread_local union
{
    long double align;
    uint8_t bytes[1 << 20];
} g_msg;

static int
decode(int revision, int client, const char *prot, const uint8_t *data, int len, int whole,
       int final, uint8_t *out, int cap, int *consumed, char *err, int err_cap)
{
    RsprotRegistryDir dir = client ? RSPROT_REG_IN : RSPROT_REG_OUT;
    const RsprotProtDef *row = row_named(revision, client, prot);
    const char *file = prot;
    RsprotCodecFn fn;
    RsprotExec x;
    RsprotBuf b;
    RsprotTrace trace;

    fn = row ? rsprot_registry_resolve_def(row, dir, revision, &file)
             : rsprot_registry_resolve(prot, dir, revision);
    if (!fn)
        return -1;
    /* Decoding transfers every field before any branch reads it, so the
     * message needs no clearing beyond what the storage provider points at. */
    memset(g_msg.bytes, 0, 4096);
    pkt_codec_storage_provide(file, g_msg.bytes, 0);
    rsprot_buf_wrap_read(&b, data, len);
    rsprot_exec_decode(&x, &b);
    trace.out = out;
    trace.cap = cap;
    if (final)
        rsprot_exec_trace_final(&x, &trace);
    else
        rsprot_exec_trace(&x, &trace);
    fn(&x, g_msg.bytes);
    if (consumed)
        *consumed = b.rpos;
    if (!rsprot_exec_ok(&x) || b.err || (whole && b.rpos != len)) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "%s at %d: %s (%d of %d bytes)", prot, revision,
                     x.error_reason ? x.error_reason
                     : b.err        ? "the payload ran out"
                                    : "bytes left over",
                     b.rpos, len);
        return -2;
    }
    if (trace.overflow)
        return -3;
    return trace.len;
}

int
rsprot_ffi_decode(int revision, int client, const char *prot, const uint8_t *data, int len,
                  int whole, uint8_t *out, int cap, int *consumed, char *err, int err_cap)
{
    return decode(revision, client, prot, data, len, whole, 0, out, cap, consumed, err, err_cap);
}

int
rsprot_ffi_decode_fields(int revision, int client, const char *prot, const uint8_t *data,
                         int len, int whole, uint8_t *out, int cap, int *consumed, char *err,
                         int err_cap)
{
    return decode(revision, client, prot, data, len, whole, 1, out, cap, consumed, err, err_cap);
}

static RsprotHuffman g_huffman;
static int g_huffman_loaded;

int
rsprot_ffi_huffman_load(const uint8_t *table, int len)
{
    if (len != 256)
        return -1;
    if (g_huffman_loaded) {
        rsprot_huffman_destroy(&g_huffman);
        g_huffman_loaded = 0;
    }
    if (!rsprot_huffman_create(&g_huffman, table))
        return -1;
    g_huffman_loaded = 1;
    return 0;
}

int
rsprot_ffi_huffman_decode(const uint8_t *data, int len, uint8_t *out, int cap)
{
    RsprotBuf b;
    int32_t n;

    if (!g_huffman_loaded)
        return -1;
    rsprot_buf_wrap_read(&b, data, len);
    /* `out` gets the terminator too. */
    n = rsprot_huffman_decode(&g_huffman, &b, (char *)out, cap);
    if (n < 0 || b.err)
        return -2;
    return n;
}

/* --- the info streams ------------------------------------------------------ */

#define INFO_OPS 32768
#define INFO_BYTES (1 << 20)
#define INFO_BLOCKS (4 << 20)

/* One read's op list and its block trace. */
static _Thread_local struct
{
    RsprotInfoOp ops[INFO_OPS];
    uint8_t bytes[INFO_BYTES];
    uint8_t blocks[INFO_BLOCKS];
} g_info;

static void
info_begin(RsprotInfoList *list, RsprotTrace *blocks)
{
    rsprot_info_list_init(list, g_info.ops, INFO_OPS, g_info.bytes, INFO_BYTES);
    memset(blocks, 0, sizeof(*blocks));
    blocks->out = g_info.blocks;
    blocks->cap = INFO_BLOCKS;
    list->trace = blocks;
}

static const char *
stop_reason(int reason)
{
    switch (reason) {
    case RSPROT_STOP_ERROR: return "the payload ran out, or a bit section was malformed";
    case RSPROT_STOP_ILLEGAL_FLAG: return "an extended-info flag RSProx throws on";
    case RSPROT_STOP_BLOCK: return "an extended-info block did not read to its end";
    case RSPROT_STOP_TRAILING: return "bytes left over";
    default: return "stopped";
    }
}

/* The read is over: a stop is an error, else the block trace follows `head`. */
static int
info_end(const RsprotInfoList *list, RsprotTrace *head, const RsprotTrace *blocks,
         const char *prot, int *blocks_at, char *err, int err_cap)
{
    if (list->stopped) {
        const RsprotInfoStop *stop = &list->ops[list->count - 1].stop;

        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "%s: %s (kind %d, flags 0x%x, at byte %d)", prot,
                     stop_reason(stop->reason), stop->kind, stop->flags, stop->rpos);
        return -2;
    }
    if (list->overflow || head->overflow || blocks->overflow || head->cap - head->len < blocks->len)
        return -3;
    *blocks_at = head->len;
    memcpy(head->out + head->len, blocks->out, (size_t)blocks->len);
    return head->len + blocks->len;
}

void *
rsprot_ffi_players_new(int local_index)
{
    RsprotPlayerInfo *pi = malloc(sizeof(*pi));

    assert(pi);
    rsprot_player_info_reset(pi, local_index);
    return pi;
}

void
rsprot_ffi_players_free(void *players)
{
    free(players);
}

void
rsprot_ffi_players_reset(void *players, int local_index)
{
    assert(players);
    rsprot_player_info_reset(players, local_index);
}

void
rsprot_ffi_players_init(void *players, const uint8_t *gpi, int len)
{
    assert(players);
    rsprot_player_info_init(players, gpi, len);
}

int
rsprot_ffi_player_info(void *players, int revision, const uint8_t *data, int len,
                       uint8_t *out, int cap, int *blocks_at, char *err, int err_cap)
{
    RsprotPlayerInfo *pi = players;
    RsprotPlayerInfoReadFn read = player_info_osrs(revision);
    RsprotInfoList list;
    RsprotTrace blocks;
    RsprotTrace head = { out, cap, 0, 0, 0, NULL, 0 };

    assert(pi);
    if (!read)
        return -1;
    info_begin(&list, &blocks);
    read(pi, data, len, &list);
    for (int idx = 1; idx < RSPROT_INFO_PLAYER_SLOTS; idx++) {
        int type = pi->update_type[idx];
        int low = type == RSPROT_PLAYER_LOW_RESOLUTION_MOVEMENT ||
                  type == RSPROT_PLAYER_HIGH_RESOLUTION_TO_LOW_RESOLUTION;

        if (type == RSPROT_PLAYER_LOW_RESOLUTION_IDLE)
            continue;
        rsprot_trace_prim(&head, 0, idx, "index");
        rsprot_trace_prim(&head, 0, type, "type");
        rsprot_trace_prim(&head, 0, low ? pi->low_res_pos[idx] : pi->coord[idx], "coord");
    }
    return info_end(&list, &head, &blocks, "PLAYER_INFO", blocks_at, err, err_cap);
}

static void
trace_we_ext(RsprotTrace *t, const RsprotInfoWeExt *e)
{
    rsprot_trace_prim(t, 0, e->flags, "ext_flags");
    rsprot_trace_prim(t, 0, e->has_sequence, "has_sequence");
    rsprot_trace_prim(t, 0, e->sequence_id, "sequence_id");
    rsprot_trace_prim(t, 0, e->sequence_delay, "sequence_delay");
    rsprot_trace_prim(t, 0, e->has_ops, "has_ops");
    rsprot_trace_prim(t, 0, e->ops, "ops");
}

/* One op of the movement half, by name; the extended-info ops are the block
 * trace's, and are left out. */
static void
trace_op(RsprotTrace *t, const RsprotInfoOp *op)
{
    switch (op->kind) {
    case RSPROT_OP_SET_LOCAL_PLAYER:
    case RSPROT_OP_ADD_NEW:
    case RSPROT_OP_ADD_OLD:
    case RSPROT_OP_SET_EXT:
    case RSPROT_OP_CLEAR_OLD:
    case RSPROT_OP_REMOVE_PID:
    case RSPROT_OP_COUNT_RESET:
    case RSPROT_OP_INFO:
    case RSPROT_OP_WALKDIR:
    case RSPROT_OP_RUNDIR:
    case RSPROT_OP_CRAWLDIR:
    case RSPROT_OP_NPCTYPE:
    case RSPROT_OP_SPAWN_CYCLE:
    case RSPROT_OP_WE_COUNT:
        rsprot_trace_prim(t, 0, op->kind, "#op");
        rsprot_trace_long(t, (int64_t)op->value, "value");
        break;
    case RSPROT_OP_DELTA_XZ:
        rsprot_trace_prim(t, 0, op->kind, "#op");
        rsprot_trace_prim(t, 0, op->delta_xz.dx, "dx");
        rsprot_trace_prim(t, 0, op->delta_xz.dz, "dz");
        rsprot_trace_prim(t, 0, op->delta_xz.jump, "jump");
        break;
    case RSPROT_OP_FACE_ANGLE:
        /* Only the entering-view record's: a block's is in the block trace. */
        if (!op->face_angle.spawn)
            break;
        rsprot_trace_prim(t, 0, op->kind, "#op");
        rsprot_trace_prim(t, 0, op->face_angle.angle, "angle");
        break;
    case RSPROT_OP_WE_REMOVE:
        rsprot_trace_prim(t, 0, op->kind, "#op");
        rsprot_trace_prim(t, 0, op->who, "who");
        break;
    case RSPROT_OP_WE_IDLE:
    case RSPROT_OP_WE_MOVE:
        rsprot_trace_prim(t, 0, op->kind, "#op");
        rsprot_trace_prim(t, 0, op->who, "who");
        rsprot_trace_prim(t, 0, op->we_move.dx, "dx");
        rsprot_trace_prim(t, 0, op->we_move.dy, "dy");
        rsprot_trace_prim(t, 0, op->we_move.dz, "dz");
        rsprot_trace_prim(t, 0, op->we_move.dangle, "dangle");
        rsprot_trace_prim(t, 0, op->we_move.teleport, "teleport");
        trace_we_ext(t, &op->we_move.ext);
        break;
    case RSPROT_OP_WE_ADD:
        rsprot_trace_prim(t, 0, op->kind, "#op");
        rsprot_trace_prim(t, 0, op->who, "who");
        rsprot_trace_prim(t, 0, op->we_add.id, "id");
        rsprot_trace_prim(t, 0, op->we_add.size_x, "size_x");
        rsprot_trace_prim(t, 0, op->we_add.size_z, "size_z");
        rsprot_trace_prim(t, 0, op->we_add.has_level, "has_level");
        rsprot_trace_prim(t, 0, op->we_add.level, "level");
        rsprot_trace_prim(t, 0, op->we_add.priority, "priority");
        rsprot_trace_prim(t, 0, op->we_add.x, "x");
        rsprot_trace_prim(t, 0, op->we_add.y, "y");
        rsprot_trace_prim(t, 0, op->we_add.z, "z");
        rsprot_trace_prim(t, 0, op->we_add.angle, "angle");
        rsprot_trace_prim(t, 0, op->we_add.has_centre, "has_centre");
        rsprot_trace_prim(t, 0, op->we_add.centre_x, "centre_x");
        rsprot_trace_prim(t, 0, op->we_add.centre_z, "centre_z");
        trace_we_ext(t, &op->we_add.ext);
        break;
    default:
        break;
    }
}

static int
trace_ops(const RsprotInfoList *list, RsprotTrace *head)
{
    for (int i = 0; i < list->count; i++)
        trace_op(head, &list->ops[i]);
    return head->len;
}

static _Thread_local RsprotFfiNpcLengths g_lengths;

static int
npc_lengths(void *ctx, int who, int head, int *recolours, int *retextures)
{
    (void)ctx;
    return g_lengths && g_lengths(who, head, recolours, retextures);
}

int
rsprot_ffi_npc_info(int revision, const char *prot, const uint8_t *data, int len,
                    RsprotFfiNpcLengths lengths, uint8_t *out, int cap, int *blocks_at,
                    char *err, int err_cap)
{
    RsprotNpcInfoReadFn read = NULL;
    RsprotInfoList list;
    RsprotTrace blocks;
    RsprotTrace head = { out, cap, 0, 0, 0, NULL, 0 };

    if (strcmp(prot, "NPC_INFO_SMALL_V4") == 0)
        read = npc_info_small_v4_osrs(revision);
    else if (strcmp(prot, "NPC_INFO_LARGE_V4") == 0)
        read = npc_info_large_v4_osrs(revision);
    else if (strcmp(prot, "NPC_INFO_SMALL_V5") == 0)
        read = npc_info_small_v5_osrs(revision);
    else if (strcmp(prot, "NPC_INFO_LARGE_V5") == 0)
        read = npc_info_large_v5_osrs(revision);
    else if (strcmp(prot, "NPC_INFO_SMALL_V6") == 0)
        read = npc_info_small_v6_osrs(revision);
    else if (strcmp(prot, "NPC_INFO_LARGE_V6") == 0)
        read = npc_info_large_v6_osrs(revision);
    if (!read)
        return -1;
    info_begin(&list, &blocks);
    g_lengths = lengths;
    read(data, len, lengths ? npc_lengths : NULL, NULL, &list);
    g_lengths = NULL;
    trace_ops(&list, &head);
    return info_end(&list, &head, &blocks, prot, blocks_at, err, err_cap);
}

int
rsprot_ffi_worldentity_info(int revision, const char *prot, const uint8_t *data, int len,
                            uint8_t *out, int cap, int *blocks_at, char *err, int err_cap)
{
    RsprotWorldEntityInfoReadFn read = NULL;
    RsprotInfoList list;
    RsprotTrace blocks;
    RsprotTrace head = { out, cap, 0, 0, 0, NULL, 0 };
    static const struct
    {
        const char *prot;
        RsprotWorldEntityInfoReadFn (*lookup)(int revision);
    } k_versions[] = {
        { "WORLDENTITY_INFO_V1", worldentity_info_v1_osrs },
        { "WORLDENTITY_INFO_V2", worldentity_info_v2_osrs },
        { "WORLDENTITY_INFO_V3", worldentity_info_v3_osrs },
        { "WORLDENTITY_INFO_V4", worldentity_info_v4_osrs },
        { "WORLDENTITY_INFO_V5", worldentity_info_v5_osrs },
        { "WORLDENTITY_INFO_V6", worldentity_info_v6_osrs },
        { "WORLDENTITY_INFO_V7", worldentity_info_v7_osrs },
    };

    for (size_t i = 0; i < sizeof(k_versions) / sizeof(k_versions[0]); i++)
        if (strcmp(prot, k_versions[i].prot) == 0)
            read = k_versions[i].lookup(revision);
    if (!read)
        return -1;
    info_begin(&list, &blocks);
    read(data, len, &list);
    trace_ops(&list, &head);
    return info_end(&list, &head, &blocks, prot, blocks_at, err, err_cap);
}

int
rsprot_ffi_appearance(int revision, const uint8_t *data, int len, uint8_t *out, int cap,
                      char *err, int err_cap)
{
    RsprotTrace t = { out, cap, 0, 0, 0, NULL, 0 };

    if (!rsprot_info_appearance_trace(revision, data, len, &t)) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "the appearance block did not read (%d bytes, rev %d)",
                     len, revision);
        return -2;
    }
    return t.overflow ? -3 : t.len;
}
