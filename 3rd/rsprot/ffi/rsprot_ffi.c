/* rsprot_ffi. See rsprot_ffi.h. */
#include "rsprot_ffi.h"

#include "../gen/rsprot_tables.h"
#include "../include/rsprot_exec.h"
#include "../include/rsprot_registry.h"
#include "../include/rsprot_tails.h"
#include "../src/rsprot_compression.h"
#include "../include/rsprot_trace.h"
#include "../streams/rsprot_info.h"

#include "../src/rsprot_storage.h"
#include "../packets/reflection_check_reply_results.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
rsprot_ffi_abi(void)
{
    return RSPROT_FFI_ABI;
}

/*
 * The FFI's host is RSProx, which records Old School only, so a revision here
 * is an OSRS number and is tagged on the way in.
 */
int
rsprot_ffi_revision_first(void)
{
    for (int i = 0; i < RSPROT_REVISION_COUNT; i++)
        if (rsprot_rev_family(rsprot_all_revisions[i].rev) == RSPROT_FAMILY_OSRS)
            return rsprot_rev_number(rsprot_all_revisions[i].rev);
    return 0;
}

int
rsprot_ffi_revision_last(void)
{
    for (int i = RSPROT_REVISION_COUNT - 1; i >= 0; i--)
        if (rsprot_rev_family(rsprot_all_revisions[i].rev) == RSPROT_FAMILY_OSRS)
            return rsprot_rev_number(rsprot_all_revisions[i].rev);
    return 0;
}

int
rsprot_ffi_prot(int revision, int client, int i, const char **name, int *opcode, int *size,
                const char **codec)
{
    const RsprotRevTable *t = rsprot_rev_table(rsprot_osrs(revision));
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
    const RsprotRevTable *t = rsprot_rev_table(rsprot_osrs(revision));

    return t ? rsprot_zone_prot_name(t, ordinal) : NULL;
}

/* The row `prot` names at `revision`, for its codec field; NULL when the
 * table has no such row (a zone sub-prot, or a name a host spells itself). */
static const RsprotProtDef *
row_named(int revision, int client, const char *prot)
{
    const RsprotRevTable *t = rsprot_rev_table(rsprot_osrs(revision));

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

    fn = row ? rsprot_registry_resolve_def(row, dir, rsprot_osrs(revision), &file)
             : rsprot_registry_resolve(prot, dir, rsprot_osrs(revision));
    if (!fn)
        return -1;
    /* Decoding transfers every field before any branch reads it, so the
     * message needs no clearing beyond what the storage provider points at. */
    memset(g_msg.bytes, 0, 4096);
    rsprot_storage_provide(file, g_msg.bytes, 0);
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

int
rsprot_ffi_zone_decode(int revision, const uint8_t *data, int len, uint8_t *out, int cap,
                       int *consumed, const char **prot_out, char *err, int err_cap)
{
    const char *prot;
    int used = 0;
    int n;

    if (len < 1)
        return -1;
    prot = rsprot_ffi_zone_prot(revision, data[0]);
    if (!prot)
        return -1;
    *prot_out = prot;
    n = decode(revision, 0, prot, data + 1, len - 1, 0, 0, out, cap, &used, err, err_cap);
    *consumed = 1 + used;
    return n;
}

/* CyclicRedundancyCheck.computeCrc32: CRC-32, reflected, 0xEDB88320. */
static uint32_t
crc32_of(const uint8_t *data, int len)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

int
rsprot_ffi_reflection_reply(const int32_t *check_ops, int count, const uint8_t *data, int len,
                            uint8_t *out, int cap, char *err, int err_cap)
{
    static _Thread_local MsgReflectionCheckReplyResults m;
    RsprotExec x;
    RsprotBuf b;
    RsprotTrace trace;

    memset(&m, 0, sizeof(m));
    if (count < 0 || count > MSG_REFLECTION_CHECKER_MAX_CHECKS)
        return -1;
    m.check_count = count;
    memcpy(m.check_ops, check_ops, sizeof(int32_t) * (size_t)count);
    rsprot_buf_wrap_read(&b, data, len);
    rsprot_exec_decode(&x, &b);
    trace.out = out;
    trace.cap = cap;
    rsprot_exec_trace(&x, &trace);
    packet_reflection_check_reply_results_in(&x, &m);
    if (!rsprot_exec_ok(&x) || b.err) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "REFLECTION_CHECK_REPLY: %s",
                     x.error_reason ? x.error_reason : "the payload ran out");
        return -2;
    }
    /* checkCRC32: of everything before the CRC. */
    if (crc32_of(data, len - 4) != (uint32_t)m.crc) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "CRC mismatch!");
        return -2;
    }
    return trace.overflow ? -3 : trace.len;
}

int
rsprot_ffi_zone_grid(const uint8_t *data, int len, int count, int key_count, int32_t *zones,
                     int32_t *keys)
{
    RsprotExec x;
    RsprotBuf b;

    if (count < 0 || count > RSPROT_REGION_ZONES || key_count < 0 ||
        key_count > RSPROT_REGION_KEYS_MAX)
        return 0;
    rsprot_buf_wrap_read(&b, data, len);
    rsprot_exec_decode(&x, &b);
    rsprot_tail_zone_grid(&x, zones, count);
    rsprot_tail_xtea_keys(&x, keys, key_count);
    return rsprot_exec_ok(&x) && !b.err;
}

/* --- the info streams ------------------------------------------------------ */

#define INFO_OPS 32768
#define INFO_BYTES (1 << 20)

/* One read's op list. */
static _Thread_local struct
{
    RsprotInfoOp ops[INFO_OPS];
    uint8_t bytes[INFO_BYTES];
} g_info;

static const char *
stop_reason(int reason)
{
    switch (reason) {
    case RSPROT_STOP_ERROR: return "the payload ran out, or a bit section was malformed";
    case RSPROT_STOP_ILLEGAL_FLAG: return "an extended-info flag with no layout";
    case RSPROT_STOP_BLOCK: return "an extended-info block did not read to its end";
    case RSPROT_STOP_TRAILING: return "bytes left over";
    default: return "stopped";
    }
}

#define I(name, v) rsprot_trace_prim(t, 0, (int32_t)(v), name)
#define L(name, v) rsprot_trace_long(t, (int64_t)(v), name)

static void
s(RsprotTrace *t, const char *name, const char *v)
{
    rsprot_trace_bytes(t, RSPROT_TRACE_STR, 0, (const uint8_t *)v, v ? (int32_t)strlen(v) : -1,
                       name);
}

static void
ints(RsprotTrace *t, const char *name, const int32_t *v, int32_t n)
{
    for (int32_t i = 0; v && i < n; i++)
        I(name, v[i]);
}

static void
we_ext(RsprotTrace *t, const RsprotInfoWeExt *e)
{
    I("ext.flags", e->flags);
    I("ext.has_sequence", e->has_sequence);
    I("ext.sequence_id", e->sequence_id);
    I("ext.sequence_delay", e->sequence_delay);
    I("ext.has_ops", e->has_ops);
    I("ext.ops", e->ops);
}

static void
body(RsprotTrace *t, const RsprotInfoBodyCustom *b)
{
    I("reset", b->reset);
    I("flag", b->flag);
    I("model_count", b->model_count);
    ints(t, "models", b->models, b->model_count);
    I("recolour_count", b->recolour_count);
    ints(t, "recolours", b->recolours, b->recolour_count);
    I("retexture_count", b->retexture_count);
    ints(t, "retextures", b->retextures, b->retexture_count);
    I("mirror", b->mirror);
    I("has_recolour_all", b->has_recolour_all);
    I("recolour_all", b->recolour_all);
    I("body_type", b->body_type);
    I("identkit_count", b->identkit_count);
    ints(t, "identkits", b->identkits, b->identkit_count);
}

/* One op: `#op` (its RsprotInfoOpKind), `who`, then every field of its union
 * member under the field's name. */
static void
trace_op(RsprotTrace *t, const RsprotInfoOp *op)
{
    I("#op", op->kind);
    I("who", op->who);
    switch (op->kind) {
    case RSPROT_OP_LOCAL_XZLEVEL:
    case RSPROT_OP_ABS_XZLEVEL:
        I("x", op->xz_level.x);
        I("z", op->xz_level.z);
        I("level", op->xz_level.level);
        I("jump", op->xz_level.jump);
        I("has_move_speed", op->xz_level.has_move_speed);
        I("move_speed", op->xz_level.move_speed);
        I("route_only", op->xz_level.route_only);
        I("move", op->xz_level.move);
        break;
    case RSPROT_OP_DELTA_XZ:
        I("dx", op->delta_xz.dx);
        I("dz", op->delta_xz.dz);
        I("jump", op->delta_xz.jump);
        break;
    case RSPROT_OP_LOW_RES_POS:
        I("slot", op->low_res.slot);
        I("opcode", op->low_res.opcode);
        I("level", op->low_res.level);
        I("x", op->low_res.x);
        I("z", op->low_res.z);
        break;
    case RSPROT_OP_BLOCK:
        I("kind", op->block.kind);
        I("bit", op->block.bit);
        break;
    case RSPROT_OP_APPEARANCE:
        rsprot_trace_bytes(t, RSPROT_TRACE_DATA, -1, op->appearance.appearance,
                           op->appearance.appearance ? op->appearance.len : -1, "appearance");
        break;
    case RSPROT_OP_SEQUENCE:
        I("sequence_id", op->sequence.sequence_id);
        I("delay", op->sequence.delay);
        break;
    case RSPROT_OP_FACE_ENTITY:
        I("entity_id", op->face_entity.entity_id);
        I("fallback_angle", op->face_entity.fallback_angle);
        I("has_fallback_angle", op->face_entity.has_fallback_angle);
        I("instant", op->face_entity.instant);
        I("movement_mode", op->face_entity.movement_mode);
        I("modern", op->face_entity.modern);
        I("target_type", op->face_entity.target_type);
        I("target_index", op->face_entity.target_index);
        break;
    case RSPROT_OP_SAY:
        s(t, "text", op->say.text);
        break;
    case RSPROT_OP_DAMAGE:
    case RSPROT_OP_DAMAGE2:
        I("damage_type", op->damage.damage_type);
        I("damage", op->damage.damage);
        I("soaked", op->damage.soaked);
        I("soak_type", op->damage.soak_type);
        I("soak_damage", op->damage.soak_damage);
        I("health", op->damage.health);
        I("total_health", op->damage.total_health);
        I("delay", op->damage.delay);
        I("slots", op->damage.slots);
        break;
    case RSPROT_OP_HEADBAR:
        I("type", op->headbar.type);
        I("duration", op->headbar.duration);
        I("start_delay", op->headbar.start_delay);
        I("start_fill", op->headbar.start_fill);
        I("end_fill", op->headbar.end_fill);
        I("remove", op->headbar.remove);
        I("index", op->headbar.index);
        break;
    case RSPROT_OP_FACE_COORD:
        I("x", op->face_coord.x);
        I("z", op->face_coord.z);
        I("instant", op->face_coord.instant);
        I("movement_mode", op->face_coord.movement_mode);
        I("modern", op->face_coord.modern);
        I("tile_x", op->face_coord.tile_x);
        I("tile_z", op->face_coord.tile_z);
        I("size_x", op->face_coord.size_x);
        I("size_z", op->face_coord.size_z);
        break;
    case RSPROT_OP_CHAT:
        I("colour_effect", op->chat.colour_effect);
        I("type", op->chat.type);
        I("length", op->chat.length);
        rsprot_trace_bytes(t, RSPROT_TRACE_DATA, -1, op->chat.data,
                           op->chat.data ? op->chat.length : 0, "data");
        I("autotyper", op->chat.autotyper);
        I("pattern_count", op->chat.pattern_count);
        for (int i = 0; i < op->chat.pattern_count; i++)
            I("pattern", op->chat.pattern[i]);
        break;
    case RSPROT_OP_SPOTANIM:
        I("slot", op->spotanim.slot);
        I("spotanim_id", op->spotanim.spotanim_id);
        I("height_delay", op->spotanim.height_delay);
        I("loop", op->spotanim.loop);
        I("has_loop", op->spotanim.has_loop);
        I("single", op->spotanim.single);
        break;
    case RSPROT_OP_EXACT_MOVE:
        I("start_x", op->exact_move.start_x);
        I("start_z", op->exact_move.start_z);
        I("end_x", op->exact_move.end_x);
        I("end_z", op->exact_move.end_z);
        I("start_cycle_delta", op->exact_move.start_cycle_delta);
        I("end_cycle_delta", op->exact_move.end_cycle_delta);
        I("facing", op->exact_move.facing);
        I("facing_is_yaw", op->exact_move.facing_is_yaw);
        I("relative", op->exact_move.relative);
        break;
    case RSPROT_OP_FACE_ANGLE:
        I("angle", op->face_angle.angle);
        I("instant", op->face_angle.instant);
        I("spawn", op->face_angle.spawn);
        I("movement_mode", op->face_angle.movement_mode);
        I("modern", op->face_angle.modern);
        break;
    case RSPROT_OP_TINT:
        I("start_cycle_delta", op->tint.start_cycle_delta);
        I("end_cycle_delta", op->tint.end_cycle_delta);
        I("hue", op->tint.hue);
        I("saturation", op->tint.saturation);
        I("lightness", op->tint.lightness);
        I("weight", op->tint.weight);
        break;
    case RSPROT_OP_TRANSPARENCY:
        I("start_cycle_delta", op->transparency.start_cycle_delta);
        I("end_cycle_delta", op->transparency.end_cycle_delta);
        I("from", op->transparency.from);
        I("to", op->transparency.to);
        I("use_start", op->transparency.use_start);
        break;
    case RSPROT_OP_CHANGE_TYPE:
        I("npc_type", op->change_type.npc_type);
        break;
    case RSPROT_OP_NAME_CHANGE:
        s(t, "name", op->name_change.name);
        break;
    case RSPROT_OP_BAS_CHANGE:
        I("mask", op->bas_change.mask);
        I("readyanim", op->bas_change.readyanim);
        I("walkanim", op->bas_change.walkanim);
        I("turnanim", op->bas_change.turnanim);
        I("turnanim_r", op->bas_change.turnanim_r);
        I("runanim", op->bas_change.runanim);
        I("walkanim_b", op->bas_change.walkanim_b);
        I("walkanim_r", op->bas_change.walkanim_r);
        I("walkanim_l", op->bas_change.walkanim_l);
        I("runanim_b", op->bas_change.runanim_b);
        I("runanim_l", op->bas_change.runanim_l);
        I("runanim_r", op->bas_change.runanim_r);
        I("crawlanim", op->bas_change.crawlanim);
        I("crawlanim_b", op->bas_change.crawlanim_b);
        I("crawlanim_l", op->bas_change.crawlanim_l);
        I("crawlanim_r", op->bas_change.crawlanim_r);
        break;
    case RSPROT_OP_HEADICON_CUSTOM:
        I("mask", op->headicon.mask);
        for (int i = 0; i < 8; i++) {
            I("group", op->headicon.group[i]);
            I("index", op->headicon.index[i]);
        }
        break;
    case RSPROT_OP_BODY_CUSTOM:
    case RSPROT_OP_HEAD_CUSTOM:
        body(t, &op->body);
        break;
    case RSPROT_OP_NAME_EXTRAS:
        s(t, "before_name", op->name_extras.before_name);
        s(t, "after_name", op->name_extras.after_name);
        s(t, "after_combat_level", op->name_extras.after_combat_level);
        break;
    case RSPROT_OP_FREEZE:
        I("delay", op->freeze.delay);
        I("duration", op->freeze.duration);
        I("cancel", op->freeze.cancel);
        break;
    case RSPROT_OP_STOP:
        I("reason", op->stop.reason);
        I("rpos", op->stop.rpos);
        I("flags", op->stop.flags);
        I("kind", op->stop.kind);
        I("block_flag", op->stop.block_flag);
        break;
    case RSPROT_OP_WE_IDLE:
    case RSPROT_OP_WE_MOVE:
        I("dx", op->we_move.dx);
        I("dy", op->we_move.dy);
        I("dz", op->we_move.dz);
        I("dangle", op->we_move.dangle);
        I("teleport", op->we_move.teleport);
        I("move_speed", op->we_move.move_speed);
        we_ext(t, &op->we_move.ext);
        break;
    case RSPROT_OP_WE_ADD:
        I("id", op->we_add.id);
        I("size_x", op->we_add.size_x);
        I("size_z", op->we_add.size_z);
        I("has_level", op->we_add.has_level);
        I("level", op->we_add.level);
        I("priority", op->we_add.priority);
        I("x", op->we_add.x);
        I("y", op->we_add.y);
        I("z", op->we_add.z);
        I("angle", op->we_add.angle);
        I("has_centre", op->we_add.has_centre);
        I("centre_x", op->we_add.centre_x);
        I("centre_z", op->we_add.centre_z);
        I("unknown", op->we_add.unknown);
        we_ext(t, &op->we_add.ext);
        break;
    default:
        /* The ops that carry only `value` (or nothing). */
        L("value", op->value);
        break;
    }
}

/* The read is over: the ops into `out`, or why not. */
static int
info_end(const RsprotInfoList *list, uint8_t *out, int cap, const char *prot, char *err,
         int err_cap)
{
    RsprotTrace t = { out, cap, 0, 0, 0, NULL, 0 };

    if (list->stopped) {
        const RsprotInfoStop *stop = &list->ops[list->count - 1].stop;

        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "%s: %s (kind %d, flags 0x%x, at byte %d)", prot,
                     stop_reason(stop->reason), stop->kind, stop->flags, stop->rpos);
        return -2;
    }
    if (list->overflow)
        return -3;
    for (int i = 0; i < list->count; i++)
        trace_op(&t, &list->ops[i]);
    return t.overflow ? -3 : t.len;
}

#undef I
#undef L

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
rsprot_ffi_gpi(int local_index, const uint8_t *gpi, int len, int32_t *coord, int32_t *positions)
{
    return rsprot_player_info_gpi(local_index, gpi, len, coord, positions);
}

void
rsprot_ffi_players_seed(void *players, int32_t coord, const int32_t *positions)
{
    assert(players);
    rsprot_player_info_seed(players, coord, positions);
}

int
rsprot_ffi_player_info(void *players, int revision, const uint8_t *data, int len, uint8_t *out,
                       int cap, char *err, int err_cap)
{
    RsprotPlayerInfoReadFn read = player_info_osrs(rsprot_osrs(revision));
    RsprotInfoList list;

    assert(players);
    if (!read)
        return -1;
    rsprot_info_list_init(&list, g_info.ops, INFO_OPS, g_info.bytes, INFO_BYTES);
    read(players, data, len, &list);
    return info_end(&list, out, cap, "PLAYER_INFO", err, err_cap);
}

static _Thread_local RsprotFfiNpcLengths g_lengths;

/* CUSTOM_TYPED: whose type the npc at list position `who` has, from the ops
 * read so far -- a newcomer's NPCTYPE, a CHANGE_TYPE in its own extended info
 * (as sent: 65535 is back to the spawned type), or else its position in the
 * previous packet's list -- then the host's counts for it. */
static int
npc_lengths(void *ctx, int who, int head, int *recolours, int *retextures)
{
    const RsprotInfoList *list = ctx;
    int position = 0;
    int target = -1;
    int old = -1;
    int32_t type = -1;

    for (int i = 0; i < list->count; i++) {
        const RsprotInfoOp *op = &list->ops[i];

        if (op->kind == RSPROT_OP_ADD_OLD) {
            if (position++ == who)
                old = (int)op->value;
        } else if (op->kind == RSPROT_OP_ADD_NEW) {
            position++;
        } else if (op->kind == RSPROT_OP_NPCTYPE && position - 1 == who) {
            type = (int32_t)op->value;
        } else if (op->kind == RSPROT_OP_SET_EXT) {
            target = (int)op->value;
        } else if (op->kind == RSPROT_OP_CHANGE_TYPE && target == who) {
            type = op->change_type.npc_type == -1 ? 0xFFFF : op->change_type.npc_type;
        }
    }
    if (type < 0 && old < 0)
        return 0;
    return g_lengths && g_lengths(type >= 0 ? -1 : old, type, head, recolours, retextures);
}

/* The stream version a server prot calls at `revision` (rsprot_prot_version):
 * a table name in, the reader's name out. */
static const char *
stream_version(int revision, const char *prot)
{
    const RsprotProtDef *row = row_named(revision, 0, prot);

    return row ? rsprot_prot_version(row) : prot;
}

int
rsprot_ffi_npc_info(int revision, const char *prot, const uint8_t *data, int len,
                    RsprotFfiNpcLengths lengths, uint8_t *out, int cap, char *err, int err_cap)
{
    RsprotNpcInfoReadFn read = NULL;
    RsprotInfoList list;

    prot = stream_version(revision, prot);

    if (strcmp(prot, "NPC_INFO_SMALL_V4") == 0)
        read = npc_info_small_v4_osrs(rsprot_osrs(revision));
    else if (strcmp(prot, "NPC_INFO_LARGE_V4") == 0)
        read = npc_info_large_v4_osrs(rsprot_osrs(revision));
    else if (strcmp(prot, "NPC_INFO_SMALL_V5") == 0)
        read = npc_info_small_v5_osrs(rsprot_osrs(revision));
    else if (strcmp(prot, "NPC_INFO_LARGE_V5") == 0)
        read = npc_info_large_v5_osrs(rsprot_osrs(revision));
    else if (strcmp(prot, "NPC_INFO_SMALL_V6") == 0)
        read = npc_info_small_v6_osrs(rsprot_osrs(revision));
    else if (strcmp(prot, "NPC_INFO_LARGE_V6") == 0)
        read = npc_info_large_v6_osrs(rsprot_osrs(revision));
    if (!read)
        return -1;
    rsprot_info_list_init(&list, g_info.ops, INFO_OPS, g_info.bytes, INFO_BYTES);
    g_lengths = lengths;
    read(data, len, lengths ? npc_lengths : NULL, &list, &list);
    g_lengths = NULL;
    return info_end(&list, out, cap, prot, err, err_cap);
}

int
rsprot_ffi_worldentity_info(int revision, const char *prot, const uint8_t *data, int len,
                            uint8_t *out, int cap, char *err, int err_cap)
{
    RsprotWorldEntityInfoReadFn read = NULL;
    RsprotInfoList list;
    static const struct
    {
        const char *prot;
        RsprotWorldEntityInfoReadFn (*lookup)(RsprotRev rev);
    } k_versions[] = {
        { "WORLDENTITY_INFO_V1", worldentity_info_v1_osrs },
        { "WORLDENTITY_INFO_V2", worldentity_info_v2_osrs },
        { "WORLDENTITY_INFO_V3", worldentity_info_v3_osrs },
        { "WORLDENTITY_INFO_V4", worldentity_info_v4_osrs },
        { "WORLDENTITY_INFO_V5", worldentity_info_v5_osrs },
        { "WORLDENTITY_INFO_V6", worldentity_info_v6_osrs },
        { "WORLDENTITY_INFO_V7", worldentity_info_v7_osrs },
    };

    prot = stream_version(revision, prot);
    for (size_t i = 0; i < sizeof(k_versions) / sizeof(k_versions[0]); i++)
        if (strcmp(prot, k_versions[i].prot) == 0)
            read = k_versions[i].lookup(rsprot_osrs(revision));
    if (!read)
        return -1;
    rsprot_info_list_init(&list, g_info.ops, INFO_OPS, g_info.bytes, INFO_BYTES);
    read(data, len, &list);
    return info_end(&list, out, cap, prot, err, err_cap);
}

/* A worn slot's obj customisation, every field under its name. */
static void
appearance_obj(RsprotTrace *t, const RsprotAppearanceObj *o)
{
#define I(name, v) rsprot_trace_prim(t, 0, (int32_t)(v), name)
    I("flag", o->flag);
    I("recolour_indices", o->recolour_indices);
    I("recolour", o->recolour[0]);
    I("recolour", o->recolour[1]);
    I("retexture_indices", o->retexture_indices);
    I("retexture", o->retexture[0]);
    I("retexture", o->retexture[1]);
    I("recolour_count", o->recolour_count);
    for (int i = 0; i < o->recolour_count; i++) {
        I("recolours.index", o->recolours[i].index);
        I("recolours.value", o->recolours[i].value);
    }
    I("retexture_count", o->retexture_count);
    for (int i = 0; i < o->retexture_count; i++) {
        I("retextures.index", o->retextures[i].index);
        I("retextures.value", o->retextures[i].value);
    }
    I("override_colour", o->override_colour);
    I("colour", o->colour);
    I("man_wear", o->man_wear);
    I("woman_wear", o->woman_wear);
    I("man_head", o->man_head);
    I("woman_head", o->woman_head);
#undef I
}

int
rsprot_ffi_appearance(int revision, const uint8_t *data, int len, uint8_t *out, int cap,
                      char *err, int err_cap)
{
    static _Thread_local RsprotAppearance a;
    RsprotAppearanceReadFn read = rsprot_appearance_reader(rsprot_osrs(revision));
    RsprotTrace t = { out, cap, 0, 0, 0, NULL, 0 };

    if (!read)
        return -1;
    if (!read(data, len, &a)) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "the appearance block did not read (%d bytes, rev %d)",
                     len, revision);
        return -2;
    }
#define I(name, v) rsprot_trace_prim(&t, 0, (int32_t)(v), name)
    I("gender", a.gender);
    I("skull_icon", a.skull_icon);
    I("overhead_icon", a.overhead_icon);
    for (int i = 0; i < RSPROT_APPEARANCE_SLOTS; i++)
        I("kits", a.kits[i]);
    I("npc_id", a.npc_id);
    for (int i = 0; i < RSPROT_APPEARANCE_SLOTS; i++)
        I("interface_kits", a.interface_kits[i]);
    for (int i = 0; i < 5; i++)
        I("colours", a.colours[i]);
    I("ready_anim", a.ready_anim);
    I("turn_anim", a.turn_anim);
    I("walk_anim", a.walk_anim);
    I("walk_back_anim", a.walk_back_anim);
    I("walk_left_anim", a.walk_left_anim);
    I("walk_right_anim", a.walk_right_anim);
    I("run_anim", a.run_anim);
    s(&t, "name", a.name);
    I("combat_level", a.combat_level);
    I("skill_level", a.skill_level);
    I("hidden", a.hidden);
    I("customisation", a.customisation);
    for (int i = 0; i < RSPROT_APPEARANCE_SLOTS; i++)
        appearance_obj(&t, &a.objs[i]);
    s(&t, "before_name", a.before_name);
    s(&t, "after_name", a.after_name);
    s(&t, "after_combat_level", a.after_combat_level);
    I("text_gender", a.text_gender);
#undef I
    return t.overflow ? -3 : t.len;
}
