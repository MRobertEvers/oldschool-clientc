/*
 * The extended-info block readers -- see rsprot_info_blocks.h.
 *
 * Shapes are transcribed from RSProx's PlayerInfoClient / NpcInfoClient (the
 * decode<Name> functions tools/rsprox_gen_info.py maps to each one). The
 * generator has checked that every revision's function reads these fields in
 * this order; only the encodings come from the revision. What each block
 * writes is the executor's vocabulary, converted as the reference client
 * converts it.
 */
#include "rsprot_info_blocks.h"

#include <assert.h>
#include <string.h>

/* --- the list ----------------------------------------------------------------- */

void
rsprot_info_list_init(RsprotInfoList *list, RsprotInfoOp *ops, int cap, uint8_t *bytes,
                      int bytes_cap)
{
    assert(list);
    assert(ops);
    assert(bytes);
    memset(list, 0, sizeof(*list));
    list->ops = ops;
    list->cap = cap;
    list->bytes = bytes;
    list->bytes_cap = bytes_cap;
}

int
rsprot_info_list_complete(const RsprotInfoList *list)
{
    assert(list);
    return !list->stopped && !list->overflow;
}

void
rsprot_info_list_begin(RsprotInfoList *out)
{
    assert(out);
    assert(out->ops);
    out->count = 0;
    out->bytes_used = 0;
    out->stopped = 0;
    out->overflow = 0;
}

RsprotInfoOp *
rsprot_info_emit(RsprotInfoList *out, RsprotInfoOpKind kind)
{
    static RsprotInfoOp scratch;
    RsprotInfoOp *op;

    if (out->count >= out->cap) {
        out->overflow = 1;
        op = &scratch;
    } else {
        op = &out->ops[out->count++];
    }
    memset(op, 0, sizeof(*op));
    op->kind = (uint8_t)kind;
    op->who = -1;
    return op;
}

void
rsprot_info_emit_value(RsprotInfoList *out, RsprotInfoOpKind kind, uint64_t value)
{
    rsprot_info_emit(out, kind)->value = value;
}

static uint8_t *
arena(RsprotInfoList *out, int32_t len)
{
    uint8_t *p;

    if (len < 0 || out->bytes_used + len > out->bytes_cap) {
        out->overflow = 1;
        return NULL;
    }
    p = out->bytes + out->bytes_used;
    out->bytes_used += len;
    return p;
}

const char *
rsprot_info_text(RsprotInfoList *out, const char *s, int32_t len)
{
    char *p = (char *)arena(out, len + 1);

    if (!p)
        return "";
    memcpy(p, s, (size_t)len);
    p[len] = '\0';
    return p;
}

const uint8_t *
rsprot_info_blob(RsprotInfoList *out, const uint8_t *src, int32_t len, int order)
{
    uint8_t *p = arena(out, len > 0 ? len : 1);
    RsprotBuf b;

    if (!p)
        return NULL;
    if (len <= 0)
        return p;
    rsprot_buf_wrap_read(&b, src, len);
    switch (order) {
    case RSPROT_IP_DATA_ALT1: rsprot_gdata_alt1(&b, p, len); break;
    case RSPROT_IP_DATA_ALT2: rsprot_gdata_alt2(&b, p, len); break;
    case RSPROT_IP_DATA_ALT3: rsprot_gdata_alt3(&b, p, len); break;
    default: rsprot_gdata(&b, p, len); break;
    }
    return p;
}

RsprotInfoOp *
rsprot_info_stop(RsprotBuf *b, RsprotInfoList *out, int reason, int who)
{
    static RsprotInfoOp s_again;
    RsprotInfoOp *op;

    if (out->stopped) {
        memset(&s_again, 0, sizeof(s_again));
        return &s_again; /* the first reason stands */
    }
    out->stopped = 1;
    op = rsprot_info_emit(out, RSPROT_OP_STOP);
    op->who = who;
    op->stop.reason = reason;
    op->stop.rpos = b->rpos;
    return op;
}

int
rsprot_info_finish(RsprotBuf *b, RsprotInfoList *out, int32_t len)
{
    if (!out->stopped && b->err)
        rsprot_info_stop(b, out, RSPROT_STOP_ERROR, -1);
    else if (!out->stopped && b->rpos != len)
        rsprot_info_stop(b, out, RSPROT_STOP_TRAILING, -1);
    return out->count;
}

/* --- the flag word ------------------------------------------------------------ */

int
rsprot_info_flags(RsprotInfoBlocks *t, int who, const uint32_t ext[3], uint32_t illegal)
{
    uint32_t flag = (uint32_t)rsprot_g1(t->b);

    for (int e = 0; e < 3 && ext[e]; e++)
        if ((flag & ext[e]) != 0)
            flag |= (uint32_t)rsprot_g1(t->b) << (8 * (e + 1));
    if (t->b->err)
        return 0;
    if ((flag & illegal) != 0) {
        rsprot_info_stop(t->b, t->out, RSPROT_STOP_ILLEGAL_FLAG, who)->stop.flags = (int32_t)flag;
        return 0;
    }
    t->who = who;
    t->flag = flag;
    return 1;
}

/* --- shared pieces ------------------------------------------------------------- */

/* A block that did not read to its end is retracted with everything it wrote:
 * every op delivered is one that was read whole. */
static int
block_end(RsprotInfoBlocks *t, int before, RsprotInfoKind kind, uint32_t bit)
{
    RsprotInfoOp *stop;

    if (!t->b->err)
        return 1;
    t->out->count = before;
    stop = rsprot_info_stop(t->b, t->out, RSPROT_STOP_BLOCK, t->who);
    stop->stop.flags = (int32_t)t->flag;
    stop->stop.kind = kind;
    stop->stop.block_flag = (int32_t)bit;
    return 0;
}

/* Borrow `len` bytes of payload at the cursor. */
static const uint8_t *
borrow_blob(RsprotBuf *b, int32_t len)
{
    const uint8_t *p = b->data + b->rpos;

    if (len < 0 || rsprot_buf_readable(b) < len) {
        b->err |= RSPROT_BUF_ERR_UNDERFLOW;
        return NULL;
    }
    b->rpos += len;
    return p;
}

/* A string at the cursor, into the list's bytes. */
static const char *
read_text(RsprotInfoBlocks *t)
{
    int32_t n = 0;
    const char *s = rsprot_gjstr(t->b, &n);

    return s ? rsprot_info_text(t->out, s, n) : "";
}

static int32_t
id_or_none(int32_t v)
{
    return v == 65535 ? -1 : v;
}

/* A headbar: 0x7FFF removes it; an end time of 0 has no end fill. The
 * executor takes one bar per block -- the first -- as the hand-written readers
 * did; the rest are read and dropped. */
static void
read_bar(RsprotInfoBlocks *t, RsprotRead type, RsprotRead end_time, RsprotRead start_time,
         RsprotRead start_fill, RsprotRead end_fill, int keep)
{
    int32_t ty = type(t->b);
    int32_t end = end_time(t->b);
    int32_t start = -1;
    int32_t sfill = -1;
    int32_t efill = -1;
    RsprotInfoOp *op;

    if (end != 0x7FFF) {
        start = start_time(t->b);
        sfill = start_fill(t->b);
        efill = end > 0 ? end_fill(t->b) : sfill;
    }
    if (!keep)
        return;
    op = rsprot_info_emit(t->out, RSPROT_OP_HEADBAR);
    op->headbar.type = ty;
    if (end == 0x7FFF) {
        op->headbar.remove = true;
        return;
    }
    op->headbar.duration = end;
    op->headbar.start_delay = start;
    op->headbar.start_fill = (uint8_t)sfill;
    op->headbar.end_fill = (uint8_t)efill;
}

/* One hit: 0x7FFE is a delay with no splat; a soak hit is two splats on one
 * delay, the main and the soaked. */
static void
emit_hit(RsprotInfoList *out, int32_t type, int32_t value, int32_t delay, int32_t limit,
         int32_t soak_type, int32_t soak_value)
{
    RsprotInfoOp *op;

    if (type == 0x7FFE)
        return;
    op = rsprot_info_emit(out, RSPROT_OP_DAMAGE);
    op->damage.damage_type = type;
    op->damage.damage = value;
    op->damage.delay = (uint16_t)delay;
    op->damage.slots = (uint16_t)(limit < 0 ? 0 : limit);
    if (soak_type >= 0) {
        op = rsprot_info_emit(out, RSPROT_OP_DAMAGE);
        op->damage.damage_type = soak_type;
        op->damage.damage = soak_value;
        op->damage.delay = (uint16_t)delay;
    }
}

/* `count` entries of `read`, discarded (colours, textures and ident kits the
 * executor does not carry). Returns the count. */
static int32_t
skip_items(RsprotInfoBlocks *t, RsprotRead count, RsprotRead read)
{
    int32_t n = count(t->b);

    for (int32_t i = 0; i < n && !t->b->err; i++)
        (void)read(t->b);
    return n;
}

/* --- strings ---------------------------------------------------------------------- */

int
rsprot_info_say(RsprotInfoBlocks *t, uint32_t bit)
{
    int before = t->out->count;
    const char *text = read_text(t);

    rsprot_info_emit(t->out, RSPROT_OP_SAY)->say.text = text;
    return block_end(t, before, RSPROT_IK_SAY, bit);
}

int
rsprot_info_name_change(RsprotInfoBlocks *t, uint32_t bit)
{
    int before = t->out->count;
    const char *name = read_text(t);

    rsprot_info_emit(t->out, RSPROT_OP_NAME_CHANGE)->name_change.name = name;
    return block_end(t, before, RSPROT_IK_NAME_CHANGE, bit);
}

/* Three strings of global UI state in the reference; nothing for the
 * executor. */
int
rsprot_info_name_extras(RsprotInfoBlocks *t, uint32_t bit)
{
    int before = t->out->count;
    int32_t n = 0;

    for (int i = 0; i < 3; i++)
        (void)rsprot_gjstr(t->b, &n);
    return block_end(t, before, RSPROT_IK_NAME_EXTRAS, bit);
}

/* --- one field -------------------------------------------------------------------- */

/* The traversal amends the move this slot made this cycle, as
 * class109.field1570 does: the persistent one also becomes the slot's, and a
 * temporary one -- or a snap -- decides whether that move jumps. */
static int
move_speed(RsprotInfoBlocks *t, uint32_t bit, RsprotInfoKind kind, const RsprotInfoValueEnc *e,
           int persistent)
{
    int before = t->out->count;
    int8_t speed = (int8_t)e->value(t->b);
    RsprotPlayerPacket *pp = t->player;
    int op_index;

    assert(pp);
    if (t->b->err)
        return block_end(t, before, kind, bit);
    if (persistent)
        pp->pi->move_speed[t->who] = speed;
    op_index = pp->movement_op[t->who];
    if (op_index >= 0 && op_index < t->out->count) {
        RsprotInfoXZLevel *m = &t->out->ops[op_index].xz_level;

        m->has_move_speed = true;
        m->move_speed = speed;
        if (!persistent || speed == RSPROT_TRAVERSAL_SNAP)
            m->jump = speed == RSPROT_TRAVERSAL_SNAP;
    }
    return 1;
}

int
rsprot_info_move_speed(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoValueEnc *e)
{
    return move_speed(t, bit, RSPROT_IK_MOVE_SPEED, e, 1);
}

int
rsprot_info_temp_move_speed(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoValueEnc *e)
{
    return move_speed(t, bit, RSPROT_IK_TEMP_MOVE_SPEED, e, 0);
}

/* The player's reset: no executor state. */
int
rsprot_info_reset(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoValueEnc *e)
{
    int before = t->out->count;

    (void)e->value(t->b);
    return block_end(t, before, RSPROT_IK_RESET, bit);
}

int
rsprot_info_transformation(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoValueEnc *e)
{
    int before = t->out->count;
    int32_t type = e->value(t->b);

    rsprot_info_emit(t->out, RSPROT_OP_CHANGE_TYPE)->change_type.npc_type = id_or_none(type);
    return block_end(t, before, RSPROT_IK_TRANSFORMATION, bit);
}

int
rsprot_info_level_change(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoValueEnc *e)
{
    int before = t->out->count;
    int32_t level = e->value(t->b);

    rsprot_info_emit_value(t->out, RSPROT_OP_LEVEL_CHANGE, (uint32_t)level);
    return block_end(t, before, RSPROT_IK_LEVEL_CHANGE, bit);
}

int
rsprot_info_enabled_ops(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoValueEnc *e)
{
    int before = t->out->count;
    int32_t mask = e->value(t->b);

    rsprot_info_emit_value(t->out, RSPROT_OP_VISIBLE_OPS, (uint64_t)mask);
    return block_end(t, before, RSPROT_IK_ENABLED_OPS, bit);
}

/* --- flat runs ------------------------------------------------------------------------ */

int
rsprot_info_sequence(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoSequenceEnc *e)
{
    int before = t->out->count;
    int32_t id = e->id(t->b);
    int32_t delay = e->delay(t->b);
    RsprotInfoOp *op = rsprot_info_emit(t->out, RSPROT_OP_SEQUENCE);

    op->sequence.sequence_id = id_or_none(id);
    op->sequence.delay = (uint8_t)delay;
    return block_end(t, before, RSPROT_IK_SEQUENCE, bit);
}

/* `var index = g2..; index += g1.. shl 16` (227-238): an npc index, or
 * 0x10000 + a player index; 0xFFFFFF faces nothing. */
int
rsprot_info_face_pathing_entity(
    RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoFacePathingEntityEnc *e)
{
    int before = t->out->count;
    int32_t v = e->index(t->b);
    int32_t id;

    v += e->index_hi(t->b) << 16;
    id = v == 0xFFFFFF || v < 0 ? -1 : v >= 0x10000 ? 32768 + (v - 0x10000) : v;
    rsprot_info_emit(t->out, RSPROT_OP_FACE_ENTITY)->face_entity.entity_id = id;
    return block_end(t, before, RSPROT_IK_FACE_PATHING_ENTITY, bit);
}

/* The player's (227-238): one angle, never instant. */
int
rsprot_info_face_angle(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoFaceAngleEnc *e)
{
    int before = t->out->count;
    int32_t angle = e->angle(t->b);

    rsprot_info_emit(t->out, RSPROT_OP_FACE_ANGLE)->face_angle.angle = (uint16_t)angle;
    return block_end(t, before, RSPROT_IK_FACE_ANGLE, bit);
}

/* The npc's (231-238): an absolute angle, optionally instant. */
int
rsprot_info_face_angle_instant(
    RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoFaceAngleInstantEnc *e)
{
    int before = t->out->count;
    int32_t angle = e->angle(t->b);
    int32_t instant = e->instant(t->b);
    RsprotInfoOp *op = rsprot_info_emit(t->out, RSPROT_OP_FACE_ANGLE);

    op->face_angle.angle = (uint16_t)angle;
    op->face_angle.instant = instant == 1;
    return block_end(t, before, RSPROT_IK_FACE_ANGLE, bit);
}

/* The npc's (227-231): the centre of an absolute tile -- half-tiles for the
 * op, whose 0, 0 is no target, as 0xFFFF is on the wire. */
int
rsprot_info_face_coord(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoFaceCoordEnc *e)
{
    int before = t->out->count;
    int32_t x = e->x(t->b);
    int32_t z = e->z(t->b);
    int32_t instant = e->instant(t->b);
    RsprotInfoOp *op = rsprot_info_emit(t->out, RSPROT_OP_FACE_COORD);

    if (x != 0xFFFF) {
        op->face_coord.x = (int16_t)(x * 2 + 1);
        op->face_coord.z = (int16_t)(z * 2 + 1);
        op->face_coord.instant = instant == 1;
    }
    return block_end(t, before, RSPROT_IK_FACE_COORD, bit);
}

static void
exact_move(RsprotInfoBlocks *t, const RsprotInfoExactMoveEnc *e)
{
    int32_t dx1 = e->dx1(t->b);
    int32_t dz1 = e->dz1(t->b);
    int32_t dx2 = e->dx2(t->b);
    int32_t dz2 = e->dz2(t->b);
    int32_t delay1 = e->delay1(t->b);
    int32_t delay2 = e->delay2(t->b);
    int32_t direction = e->direction(t->b);
    RsprotInfoOp *op = rsprot_info_emit(t->out, RSPROT_OP_EXACT_MOVE);

    op->exact_move.start_x = (int16_t)dx1;
    op->exact_move.start_z = (int16_t)dz1;
    op->exact_move.end_x = (int16_t)dx2;
    op->exact_move.end_z = (int16_t)dz2;
    op->exact_move.end_cycle_delta = (uint16_t)delay1;
    op->exact_move.start_cycle_delta = (uint16_t)delay2;
    op->exact_move.facing = (uint16_t)direction;
    op->exact_move.facing_is_yaw = true;
    op->exact_move.relative = true;
}

int
rsprot_info_exact_move(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoExactMoveEnc *e)
{
    int before = t->out->count;

    exact_move(t, e);
    return block_end(t, before, RSPROT_IK_EXACT_MOVE, bit);
}

int
rsprot_info_player_exact_move(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoExactMoveEnc *e)
{
    int before = t->out->count;
    int op_index;

    assert(t->player);
    exact_move(t, e);
    if (t->b->err)
        return block_end(t, before, RSPROT_IK_EXACT_MOVE, bit);
    op_index = t->player->movement_op[t->who];
    if (op_index >= 0 && op_index < t->out->count)
        t->out->ops[op_index].xz_level.route_only = true;
    return 1;
}

int
rsprot_info_tinting(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoTintingEnc *e)
{
    int before = t->out->count;
    int32_t start = e->start(t->b);
    int32_t end = e->end(t->b);
    int32_t hue = e->hue(t->b);
    int32_t saturation = e->saturation(t->b);
    int32_t lightness = e->lightness(t->b);
    int32_t weight = e->weight(t->b);
    RsprotInfoOp *op = rsprot_info_emit(t->out, RSPROT_OP_TINT);

    op->tint.start_cycle_delta = (uint16_t)start;
    op->tint.end_cycle_delta = (uint16_t)end;
    op->tint.hue = (int8_t)hue;
    op->tint.saturation = (int8_t)saturation;
    op->tint.lightness = (int8_t)lightness;
    op->tint.weight = (int8_t)weight;
    return block_end(t, before, RSPROT_IK_TINTING, bit);
}

int
rsprot_info_transparency(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoTransparencyEnc *e)
{
    int before = t->out->count;
    int32_t start = e->start(t->b);
    int32_t end = e->end(t->b);
    int32_t from = e->alpha_start(t->b);
    int32_t to = e->alpha_end(t->b);
    int32_t use_start = e->use_start(t->b);
    RsprotInfoOp *op = rsprot_info_emit(t->out, RSPROT_OP_TRANSPARENCY);

    op->transparency.start_cycle_delta = (int16_t)start;
    op->transparency.end_cycle_delta = (int16_t)end;
    op->transparency.from = (uint8_t)from;
    op->transparency.to = (uint8_t)to;
    op->transparency.use_start = use_start != 0;
    return block_end(t, before, RSPROT_IK_TRANSPARENCY, bit);
}

/* No executor state. */
int
rsprot_info_freeze(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoFreezeEnc *e)
{
    int before = t->out->count;

    (void)e->delay(t->b);
    (void)e->duration(t->b);
    (void)e->cancel(t->b);
    return block_end(t, before, RSPROT_IK_FREEZE, bit);
}

/* --- branching shapes -------------------------------------------------------------- */

/* decodeFacing (238+): a header byte -- walk type (bits 0-2), face kind
 * (3-5), instant (6) -- then the face kind's fields. An entity face at a world
 * entity has no registry here: the gamepack's missing-target path applies the
 * fallback angle. */
int
rsprot_info_facing(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoFacingEnc *e)
{
    int before = t->out->count;
    int32_t flag = e->flag(t->b);
    int kind = (flag >> 3) & 0x7;
    bool instant = ((flag >> 6) & 1) != 0;
    uint8_t mode = (uint8_t)(flag & 0x7);
    int32_t type = 0;
    int32_t index = -1;
    int32_t angle = 0;
    int32_t x = 0;
    int32_t z = 0;
    int32_t size = 0;
    RsprotInfoOp *op;

    if (kind == 0) {
        type = e->entity_type(t->b);
        index = e->index(t->b);
        angle = e->fallback_angle(t->b);
        if (type == 3)
            kind = 2;
    } else if (kind == 1) {
        x = e->x(t->b);
        z = e->z(t->b);
        size = e->size(t->b);
    } else if (kind == 2) {
        angle = e->angle(t->b);
    } else if (kind != 3) {
        t->b->err |= RSPROT_BUF_ERR_RANGE; /* RSProx: error("Unknown kind") */
        return block_end(t, before, RSPROT_IK_FACING, bit);
    }
    if (kind == 1) {
        op = rsprot_info_emit(t->out, RSPROT_OP_FACE_COORD);
        op->face_coord.x = (int16_t)(x * 2 + (size & 0xf));
        op->face_coord.z = (int16_t)(z * 2 + ((size >> 4) & 0xf));
        op->face_coord.instant = instant;
        op->face_coord.movement_mode = mode;
        op->face_coord.modern = true;
    } else if (kind == 2) {
        op = rsprot_info_emit(t->out, RSPROT_OP_FACE_ANGLE);
        op->face_angle.angle = (uint16_t)angle;
        op->face_angle.instant = instant;
        op->face_angle.movement_mode = mode;
        op->face_angle.modern = true;
    } else {
        op = rsprot_info_emit(t->out, RSPROT_OP_FACE_ENTITY);
        op->face_entity.entity_id = kind == 3 ? -1
                                    : type == 1 ? index
                                    : type == 2 && index >= 0 ? 32768 + index
                                                              : -1;
        op->face_entity.fallback_angle = (uint16_t)(kind == 3 ? 0 : angle);
        op->face_entity.has_fallback_angle = kind != 3;
        op->face_entity.instant = kind != 3 && instant;
        op->face_entity.movement_mode = mode;
        op->face_entity.modern = true;
    }
    return block_end(t, before, RSPROT_IK_FACING, bit);
}

/* decodeChat: packed colour/effects, mod icon, autotyper, huffman length, the
 * huffman bytes, then one pattern colour per step when the colour is a pattern
 * (13..20). */
int
rsprot_info_chat(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoChatEnc *e)
{
    int before = t->out->count;
    int32_t colour_effects = e->colour_effects(t->b);
    int32_t mod_icon = e->mod_icon(t->b);
    int32_t len;
    const uint8_t *data;
    int32_t colour;
    int patterns;
    RsprotInfoOp *op;

    (void)e->autotyper(t->b);
    len = e->data_len(t->b);
    data = borrow_blob(t->b, len);
    colour = (int32_t)((uint32_t)colour_effects >> 8);
    patterns = colour >= 13 && colour <= 20 ? colour - 12 : 0;
    for (int i = 0; i < patterns; i++)
        (void)e->pattern(t->b);
    if (t->b->err)
        return block_end(t, before, RSPROT_IK_CHAT, bit);
    op = rsprot_info_emit(t->out, RSPROT_OP_CHAT);
    op->chat.colour_effect = (uint16_t)colour_effects;
    op->chat.type = (uint8_t)mod_icon;
    op->chat.length = (uint8_t)len;
    op->chat.data = len > 0 ? rsprot_info_blob(t->out, data, len, e->data_order) : NULL;
    return 1;
}

static void
spot(RsprotInfoList *out, int32_t slot, int32_t id, int32_t height_delay, int32_t loop)
{
    RsprotInfoOp *op = rsprot_info_emit(out, RSPROT_OP_SPOTANIM);

    op->spotanim.slot = (uint8_t)slot;
    op->spotanim.spotanim_id = id_or_none(id);
    op->spotanim.height_delay = height_delay;
    op->spotanim.loop = loop == 1;
}

int
rsprot_info_spotanim(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoSpotanimEnc *e)
{
    int before = t->out->count;
    int32_t n = e->count(t->b);

    for (int32_t i = 0; i < n && !t->b->err; i++) {
        int32_t slot = e->slot(t->b);
        int32_t id = e->id(t->b);
        int32_t height_delay = e->height_delay(t->b);

        spot(t->out, slot, id, height_delay, 0);
    }
    return block_end(t, before, RSPROT_IK_SPOTANIM, bit);
}

int
rsprot_info_spotanim_loop(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoSpotanimLoopEnc *e)
{
    int before = t->out->count;
    int32_t n = e->count(t->b);

    for (int32_t i = 0; i < n && !t->b->err; i++) {
        int32_t slot = e->slot(t->b);
        int32_t id = e->id(t->b);
        int32_t height_delay = e->height_delay(t->b);
        int32_t loop = e->loop(t->b);

        spot(t->out, slot, id, height_delay, loop);
    }
    return block_end(t, before, RSPROT_IK_SPOTANIM, bit);
}

/* decodeHit (227-237): hits by type -- 0x7FFE a bare delay, 0x7FFF a soak hit
 * (main + soak parts), anything else a plain one -- then the headbars. */
int
rsprot_info_hits(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoHitsEnc *e)
{
    int before = t->out->count;
    int32_t n = e->count(t->b);

    for (int32_t i = 0; i < n && !t->b->err; i++) {
        int32_t type = e->type(t->b);

        if (type == 0x7FFE) {
            (void)e->delay(t->b);
        } else if (type == 0x7FFF) {
            int32_t main_type = e->main_type(t->b);
            int32_t value = e->value(t->b);
            int32_t soak_type = e->soak_type(t->b);
            int32_t soak_value = e->soak_value(t->b);
            int32_t delay = e->delay(t->b);

            emit_hit(t->out, main_type, value, delay, -1, soak_type, soak_value);
        } else {
            int32_t value = e->value(t->b);
            int32_t delay = e->delay(t->b);

            emit_hit(t->out, type, value, delay, -1, -1, -1);
        }
    }
    n = e->count2(t->b);
    for (int32_t i = 0; i < n && !t->b->err; i++)
        read_bar(t, e->type, e->end_time, e->start_time, e->start_fill, e->end_fill, i == 0);
    return block_end(t, before, RSPROT_IK_HITS, bit);
}

int
rsprot_info_hitmarks(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoHitmarksEnc *e)
{
    int before = t->out->count;
    int32_t n = e->count(t->b);

    for (int32_t i = 0; i < n && !t->b->err; i++) {
        int32_t type = e->type(t->b);
        int32_t value = e->value(t->b);
        int32_t delay = e->delay(t->b);
        int32_t limit = e->limit(t->b);

        emit_hit(t->out, type, value, delay, limit, -1, -1);
    }
    return block_end(t, before, RSPROT_IK_HITMARKS, bit);
}

int
rsprot_info_headbars(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoHeadbarsEnc *e)
{
    int before = t->out->count;
    int32_t n = e->count(t->b);

    for (int32_t i = 0; i < n && !t->b->err; i++)
        read_bar(t, e->type, e->end_time, e->start_time, e->start_fill, e->end_fill, i == 0);
    return block_end(t, before, RSPROT_IK_HEADBARS, bit);
}

int
rsprot_info_appearance(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoAppearanceEnc *e)
{
    int before = t->out->count;
    int32_t len = e->data_len(t->b);
    const uint8_t *data = borrow_blob(t->b, len);
    RsprotInfoOp *op;

    if (t->b->err)
        return block_end(t, before, RSPROT_IK_APPEARANCE, bit);
    op = rsprot_info_emit(t->out, RSPROT_OP_APPEARANCE);
    op->appearance.appearance = rsprot_info_blob(t->out, data, len, e->data_order);
    op->appearance.len = len;
    return 1;
}

/* A 32-bit mask, then one animation per set bit, in bit order. */
int
rsprot_info_bas_change(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoBasChangeEnc *e)
{
    int before = t->out->count;
    int32_t mask = e->flag(t->b);
    int32_t a[15];
    RsprotInfoOp *op;

    for (int i = 0; i < 15; i++)
        a[i] = (mask & (1 << i)) != 0 ? id_or_none(e->anims[i](t->b)) : -1;
    op = rsprot_info_emit(t->out, RSPROT_OP_BAS_CHANGE);
    op->bas_change.mask = (uint32_t)mask;
    op->bas_change.turnanim = a[0];
    op->bas_change.turnanim_r = a[1];
    op->bas_change.walkanim = a[2];
    op->bas_change.walkanim_b = a[3];
    op->bas_change.walkanim_l = a[4];
    op->bas_change.walkanim_r = a[5];
    op->bas_change.runanim = a[6];
    op->bas_change.runanim_b = a[7];
    op->bas_change.runanim_l = a[8];
    op->bas_change.runanim_r = a[9];
    op->bas_change.crawlanim = a[10];
    op->bas_change.crawlanim_b = a[11];
    op->bas_change.crawlanim_l = a[12];
    op->bas_change.crawlanim_r = a[13];
    op->bas_change.readyanim = a[14];
    return block_end(t, before, RSPROT_IK_BAS_CHANGE, bit);
}

/* The body customisation the op carries: 0x1 resets to the type's models, a
 * block with no model list leaves them, and the models the list names are
 * appended (as many as the op holds). */
static RsprotInfoBodyCustom *
body(RsprotInfoBlocks *t, int32_t flag)
{
    RsprotInfoBodyCustom *b = &rsprot_info_emit(t->out, RSPROT_OP_BODY_CUSTOM)->body;

    b->reset = (flag & 1) != 0 || (flag & 2) == 0;
    return b;
}

static void
body_model(RsprotInfoBodyCustom *b, int32_t model)
{
    if (b && b->model_count < RSPROT_BODY_CUSTOM_MODELS_MAX)
        b->models[b->model_count++] = model;
}

/* Flag byte: 0x1 reset (nothing follows), 0x2 counted models, 0x4 / 0x8
 * recolours / retextures as many as the npc TYPE has, 0x10 a mirror byte. The
 * head form carries nothing the executor holds. */
static int
custom_typed_block(RsprotInfoBlocks *t, uint32_t bit, RsprotInfoKind kind,
                   const RsprotInfoCustomTypedEnc *e)
{
    int before = t->out->count;
    int32_t flag = e->flag(t->b);
    RsprotInfoBodyCustom *b = kind == RSPROT_IK_CUSTOM_TYPED ? body(t, flag) : NULL;

    if ((flag & 0x1) != 0)
        return block_end(t, before, kind, bit);
    if ((flag & 0x2) != 0) {
        int32_t n = e->model_count(t->b);

        for (int32_t i = 0; i < n && !t->b->err; i++) {
            int32_t model = e->model(t->b);

            body_model(b, model == e->model_none ? -1 : model);
        }
    }
    if ((flag & (0x4 | 0x8)) != 0) {
        int recol = 0;
        int retex = 0;

        if (!t->lengths ||
            !t->lengths(t->ctx, t->who, kind == RSPROT_IK_HEAD_CUSTOM_TYPED, &recol, &retex) ||
            recol < 0 || retex < 0) {
            t->b->err |= RSPROT_BUF_ERR_RANGE; /* no way to step an array of unknown length */
            return block_end(t, before, kind, bit);
        }
        if ((flag & 0x4) != 0)
            for (int i = 0; i < recol && !t->b->err; i++)
                (void)e->recolour(t->b);
        if ((flag & 0x8) != 0)
            for (int i = 0; i < retex && !t->b->err; i++)
                (void)e->retexture(t->b);
    }
    if ((flag & 0x10) != 0)
        (void)e->mirror(t->b);
    return block_end(t, before, kind, bit);
}

int
rsprot_info_custom_typed(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoCustomTypedEnc *e)
{
    return custom_typed_block(t, bit, RSPROT_IK_CUSTOM_TYPED, e);
}

int
rsprot_info_head_custom_typed(
    RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoCustomTypedEnc *e)
{
    return custom_typed_block(t, bit, RSPROT_IK_HEAD_CUSTOM_TYPED, e);
}

/* BodyCustomisationV3: counted arrays; V4 reads a recolour-all form first, and
 * a recolour-all of null or 65535 still means a list follows. */
static int
custom_block(RsprotInfoBlocks *t, uint32_t bit, RsprotInfoKind kind, const RsprotInfoCustomEnc *e)
{
    int before = t->out->count;
    int32_t flag = e->flag(t->b);
    RsprotInfoBodyCustom *b = body(t, flag);

    if ((flag & 0x1) != 0)
        return block_end(t, before, kind, bit);
    if ((flag & 0x2) != 0) {
        int32_t n = e->model_count(t->b);

        for (int32_t i = 0; i < n && !t->b->err; i++)
            body_model(b, e->model(t->b));
    }
    if ((flag & 0x4) != 0) {
        int32_t recol_all = -1;

        if (kind == RSPROT_IK_CUSTOM_V4 && e->recol_all_flag(t->b) == 1)
            recol_all = e->recol_all(t->b);
        if (recol_all == -1 || recol_all == 65535)
            skip_items(t, e->recolour_count, e->recolour);
    }
    if ((flag & 0x8) != 0)
        skip_items(t, e->retexture_count, e->retexture);
    if ((flag & 0x20) != 0) {
        (void)e->body_type(t->b);
        skip_items(t, e->identkit_count, e->identkit);
    }
    return block_end(t, before, kind, bit);
}

int
rsprot_info_custom_v3(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoCustomEnc *e)
{
    return custom_block(t, bit, RSPROT_IK_CUSTOM_V3, e);
}

int
rsprot_info_custom_v4(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoCustomEnc *e)
{
    return custom_block(t, bit, RSPROT_IK_CUSTOM_V4, e);
}

int
rsprot_info_headicon_custom(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoHeadiconCustomEnc *e)
{
    int before = t->out->count;
    int32_t mask = e->flag(t->b);
    RsprotInfoHeadiconCustom *h = &rsprot_info_emit(t->out, RSPROT_OP_HEADICON_CUSTOM)->headicon;

    h->mask = (uint8_t)mask;
    for (int i = 0; i < 8; i++) {
        h->group[i] = -1;
        h->index[i] = -1;
    }
    for (int i = 0; i < 8 && !t->b->err; i++) {
        if ((mask & (1 << i)) != 0) {
            h->group[i] = e->group(t->b);
            h->index[i] = (int16_t)e->index(t->b);
        }
    }
    return block_end(t, before, RSPROT_IK_HEADICON_CUSTOM, bit);
}
