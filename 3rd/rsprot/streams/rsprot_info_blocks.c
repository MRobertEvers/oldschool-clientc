/*
 * The extended-info blocks of PLAYER_INFO and NPC_INFO: one shape per
 * RsprotInfoKind, each read with the primitives its revision's steps give, and
 * written out as a BLOCK op followed by its list entries.
 *
 * Shapes are transcribed from RSProx's PlayerInfoClient / NpcInfoClient (the
 * decode<Name> functions tools/rsprox_gen_info.py maps to each kind); the
 * generator has already checked that every revision's function reads exactly
 * the variables, in exactly the order, the shape below does.
 */
#include "rsprot_info.h"
#include "rsprot_info_internal.h"

#include <assert.h>
#include <string.h>

int32_t
rsprot_info_read(RsprotBuf *b, RsprotInfoPrim prim)
{
    switch (prim) {
    case RSPROT_IP_G1: return rsprot_g1(b);
    case RSPROT_IP_G1_ALT1: return rsprot_g1_alt1(b);
    case RSPROT_IP_G1_ALT2: return rsprot_g1_alt2(b);
    case RSPROT_IP_G1_ALT3: return rsprot_g1_alt3(b);
    case RSPROT_IP_G1S: return rsprot_g1s(b);
    case RSPROT_IP_G1S_ALT1: return rsprot_g1s_alt1(b);
    case RSPROT_IP_G1S_ALT2: return rsprot_g1s_alt2(b);
    case RSPROT_IP_G1S_ALT3: return rsprot_g1s_alt3(b);
    case RSPROT_IP_G2: return rsprot_g2(b);
    case RSPROT_IP_G2_ALT1: return rsprot_g2_alt1(b);
    case RSPROT_IP_G2_ALT2: return rsprot_g2_alt2(b);
    case RSPROT_IP_G2_ALT3: return rsprot_g2_alt3(b);
    case RSPROT_IP_G2S: return rsprot_g2s(b);
    case RSPROT_IP_G2S_ALT1: return rsprot_g2s_alt1(b);
    case RSPROT_IP_G2S_ALT2: return rsprot_g2s_alt2(b);
    case RSPROT_IP_G2S_ALT3: return rsprot_g2s_alt3(b);
    case RSPROT_IP_G4: return rsprot_g4(b);
    case RSPROT_IP_G4_ALT1: return rsprot_g4_alt1(b);
    case RSPROT_IP_G4_ALT2: return rsprot_g4_alt2(b);
    case RSPROT_IP_G4_ALT3: return rsprot_g4_alt3(b);
    case RSPROT_IP_SMART: return rsprot_gsmart1or2(b);
    case RSPROT_IP_SMARTNULL: return rsprot_gsmart1or2null(b);
    case RSPROT_IP_SMART2OR4NULL: return rsprot_gsmart2or4null(b);
    default:
        /* A string, a blob, or NONE: the generator never emits one as a
         * scalar step, so reaching here is a table this file cannot read. */
        assert(0 && "rsprot_info_read: not a scalar prim");
        return 0;
    }
}

int
rsprot_info_prim_width(RsprotInfoPrim prim)
{
    if (prim >= RSPROT_IP_G1 && prim <= RSPROT_IP_G1S_ALT3)
        return 1;
    if (prim >= RSPROT_IP_G2 && prim <= RSPROT_IP_G2S_ALT3)
        return 2;
    if (prim >= RSPROT_IP_G4 && prim <= RSPROT_IP_G4_ALT3)
        return 4;
    return 0;
}

void
rsprot_info_copy_data(uint8_t *dst, const uint8_t *src, int32_t len, int order)
{
    RsprotBuf b;

    assert(dst);
    assert(src);
    rsprot_buf_wrap_read(&b, src, len);
    switch (order) {
    case RSPROT_IP_DATA_ALT1: rsprot_gdata_alt1(&b, dst, len); break;
    case RSPROT_IP_DATA_ALT2: rsprot_gdata_alt2(&b, dst, len); break;
    case RSPROT_IP_DATA_ALT3: rsprot_gdata_alt3(&b, dst, len); break;
    default: rsprot_gdata(&b, dst, len); break;
    }
}

RsprotInfoOp *
rsprot_info_emit(RsprotInfoOut *out, RsprotInfoOpKind kind, int who)
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
    op->who = who;
    return op;
}

/* The prim the steps give `var`, or NONE. */
static RsprotInfoPrim
prim_of(const RsprotInfoBlockDesc *d, RsprotInfoVar var)
{
    for (int i = 0; i < d->step_count; i++)
        if (d->steps[i].var == var)
            return (RsprotInfoPrim)d->steps[i].prim;
    return RSPROT_IP_NONE;
}

static int32_t
rd(RsprotBuf *b, const RsprotInfoBlockDesc *d, RsprotInfoVar var)
{
    RsprotInfoPrim p = prim_of(d, var);

    if (p == RSPROT_IP_NONE) {
        /* The template guarantees the variable; a table without it is not
         * one this reader was generated against. */
        b->err |= RSPROT_BUF_ERR_RANGE;
        return 0;
    }
    return rsprot_info_read(b, p);
}

/* Borrow `len` bytes of blob at the cursor. */
static const uint8_t *
borrow(RsprotBuf *b, int32_t len)
{
    const uint8_t *p = b->data + b->rpos;

    if (len < 0 || rsprot_buf_readable(b) < len) {
        b->err |= RSPROT_BUF_ERR_UNDERFLOW;
        return NULL;
    }
    b->rpos += len;
    return p;
}

static const char *
read_text(RsprotBuf *b)
{
    int32_t n = 0;
    const char *s = rsprot_gjstr(b, &n);

    return s ? s : "";
}

static void
read_bar(RsprotBuf *b, const RsprotInfoBlockDesc *d, RsprotInfoHeadbar *bar)
{
    bar->type = rd(b, d, RSPROT_IV_TYPE);
    bar->end_time = rd(b, d, RSPROT_IV_END_TIME);
    if (bar->end_time == 0x7FFF) {
        bar->start_time = -1;
        bar->start_fill = -1;
        bar->end_fill = -1;
        return;
    }
    bar->start_time = rd(b, d, RSPROT_IV_START_TIME);
    bar->start_fill = rd(b, d, RSPROT_IV_START_FILL);
    bar->end_fill = bar->end_time > 0 ? rd(b, d, RSPROT_IV_END_FILL) : bar->start_fill;
}

static void
bars(RsprotBuf *b, const RsprotInfoBlockDesc *d, int32_t n, RsprotInfoOut *out, int who)
{
    for (int32_t i = 0; i < n && !b->err; i++)
        read_bar(b, d, &rsprot_info_emit(out, RSPROT_IO_BAR, who)->bar);
}

static int32_t
items(
    RsprotBuf *b, const RsprotInfoBlockDesc *d, RsprotInfoVar count_var, RsprotInfoVar elem,
    RsprotInfoOpKind kind, RsprotInfoOut *out, int who)
{
    int32_t n = rd(b, d, count_var);

    for (int32_t i = 0; i < n && !b->err; i++)
        rsprot_info_emit(out, kind, who)->item = rd(b, d, elem);
    return n;
}

static int32_t
model_value(const RsprotInfoBlockDesc *d, int32_t v)
{
    /* RSProx: a two-byte model id of 0xFFFF is "none" (-1); the four-byte
     * form (237+) carries -1 itself. */
    return rsprot_info_prim_width(prim_of(d, RSPROT_IV_MODEL)) == 2 && v == 0xFFFF ? -1 : v;
}

/*
 * One block. The BLOCK op is written first and filled as the block reads, so
 * its list entries land after it in wire order.
 */
static int
block(
    RsprotBuf *b, const RsprotInfoSide *side, const RsprotInfoBlockDesc *d, int who,
    RsprotInfoOut *out, RsprotNpcTypeLengths lengths, void *ctx)
{
    RsprotInfoOp *op = rsprot_info_emit(out, RSPROT_IO_BLOCK, who);
    RsprotInfoBlockOp *k = &op->block;
    int32_t v[RSPROT_IV_COUNT_ALL];
    /* Loops count from a local: past the list's capacity `k` and the entry
     * ops share one scratch op, and an entry would overwrite the count. */
    int32_t n;

    k->kind = d->kind;
    k->flag = d->flag;

    switch ((RsprotInfoKind)d->kind) {
    case RSPROT_IK_SAY:
    case RSPROT_IK_NAME_CHANGE:
        k->say.text = read_text(b);
        break;

    case RSPROT_IK_NAME_EXTRAS:
        k->name_extras.before = read_text(b);
        k->name_extras.after = read_text(b);
        k->name_extras.after_level = read_text(b);
        break;

    /* One field. */
    case RSPROT_IK_MOVE_SPEED:
    case RSPROT_IK_TEMP_MOVE_SPEED:
    case RSPROT_IK_RESET:
    case RSPROT_IK_TRANSFORMATION:
    case RSPROT_IK_LEVEL_CHANGE:
    case RSPROT_IK_ENABLED_OPS:
        k->value.value = rsprot_info_read(b, (RsprotInfoPrim)d->steps[0].prim);
        break;

    /* A flat run, read in the order the steps list. */
    case RSPROT_IK_SEQUENCE:
    case RSPROT_IK_FACE_ANGLE:
    case RSPROT_IK_FACE_COORD:
    case RSPROT_IK_EXACT_MOVE:
    case RSPROT_IK_TINTING:
    case RSPROT_IK_TRANSPARENCY:
    case RSPROT_IK_FREEZE:
        memset(v, 0, sizeof(v));
        for (int i = 0; i < d->step_count; i++)
            v[d->steps[i].var] = rsprot_info_read(b, (RsprotInfoPrim)d->steps[i].prim);
        switch ((RsprotInfoKind)d->kind) {
        case RSPROT_IK_SEQUENCE:
            k->sequence.id = v[RSPROT_IV_ID];
            k->sequence.delay = v[RSPROT_IV_DELAY];
            break;
        case RSPROT_IK_FACE_ANGLE:
            k->face_angle.angle = v[RSPROT_IV_ANGLE];
            k->face_angle.instant = v[RSPROT_IV_INSTANT];
            break;
        case RSPROT_IK_FACE_COORD:
            k->face_coord.x = v[RSPROT_IV_X];
            k->face_coord.z = v[RSPROT_IV_Z];
            k->face_coord.instant = v[RSPROT_IV_INSTANT];
            break;
        case RSPROT_IK_EXACT_MOVE:
            k->exact_move.dx1 = v[RSPROT_IV_DX1];
            k->exact_move.dz1 = v[RSPROT_IV_DZ1];
            k->exact_move.dx2 = v[RSPROT_IV_DX2];
            k->exact_move.dz2 = v[RSPROT_IV_DZ2];
            k->exact_move.delay1 = v[RSPROT_IV_DELAY1];
            k->exact_move.delay2 = v[RSPROT_IV_DELAY2];
            k->exact_move.direction = v[RSPROT_IV_DIRECTION];
            break;
        case RSPROT_IK_TINTING:
            k->tinting.start = v[RSPROT_IV_START];
            k->tinting.end = v[RSPROT_IV_END];
            k->tinting.hue = v[RSPROT_IV_HUE];
            k->tinting.saturation = v[RSPROT_IV_SATURATION];
            k->tinting.lightness = v[RSPROT_IV_LIGHTNESS];
            k->tinting.weight = v[RSPROT_IV_WEIGHT];
            break;
        case RSPROT_IK_TRANSPARENCY:
            k->transparency.start = v[RSPROT_IV_START];
            k->transparency.end = v[RSPROT_IV_END];
            k->transparency.alpha_start = v[RSPROT_IV_ALPHA_START];
            k->transparency.alpha_end = v[RSPROT_IV_ALPHA_END];
            k->transparency.use_start = v[RSPROT_IV_USE_START];
            break;
        default:
            k->freeze.delay = v[RSPROT_IV_DELAY];
            k->freeze.duration = v[RSPROT_IV_DURATION];
            k->freeze.cancel = v[RSPROT_IV_CANCEL];
            break;
        }
        break;

    /* `var index = g2..; index += g1.. shl 16`. */
    case RSPROT_IK_FACE_PATHING_ENTITY:
        k->face_pathing_entity.index = rd(b, d, RSPROT_IV_INDEX);
        k->face_pathing_entity.index += rd(b, d, RSPROT_IV_INDEX_HI) << 16;
        break;

    /* decodeFacing (238+): a header byte, then the face kind's smarts. */
    case RSPROT_IK_FACING:
    {
        int kind;

        k->facing.flag = rd(b, d, RSPROT_IV_FLAG);
        k->facing.index = -1;
        kind = (k->facing.flag >> 3) & 0x7;
        if (kind == 0) {
            k->facing.entity_type = rd(b, d, RSPROT_IV_ENTITY_TYPE);
            k->facing.index = rd(b, d, RSPROT_IV_INDEX);
            k->facing.fallback_angle = rd(b, d, RSPROT_IV_FALLBACK_ANGLE);
        } else if (kind == 1) {
            k->facing.x = rd(b, d, RSPROT_IV_X);
            k->facing.z = rd(b, d, RSPROT_IV_Z);
            k->facing.size = rd(b, d, RSPROT_IV_SIZE);
        } else if (kind == 2) {
            k->facing.angle = rd(b, d, RSPROT_IV_ANGLE);
        } else if (kind != 3) {
            return 0; /* RSProx: error("Unknown kind") */
        }
        break;
    }

    /* decodeChat: packed colour/effects, mod icon, autotyper, huffman length,
     * the huffman bytes, then one pattern colour per step when the colour is
     * a pattern (13..20). */
    case RSPROT_IK_CHAT:
    {
        int32_t colour;

        k->chat.colour_effects = rd(b, d, RSPROT_IV_COLOUR_EFFECTS);
        k->chat.mod_icon = rd(b, d, RSPROT_IV_MOD_ICON);
        k->chat.autotyper = rd(b, d, RSPROT_IV_AUTOTYPER);
        k->chat.data_len = rd(b, d, RSPROT_IV_DATA_LEN);
        k->chat.data_order = (uint8_t)prim_of(d, RSPROT_IV_DATA);
        k->chat.data = borrow(b, k->chat.data_len);
        colour = (int32_t)((uint32_t)k->chat.colour_effects >> 8);
        k->chat.pattern_count = (uint8_t)(colour >= 13 && colour <= 20 ? colour - 12 : 0);
        for (int i = 0; i < k->chat.pattern_count; i++)
            k->chat.pattern[i] = (uint8_t)rd(b, d, RSPROT_IV_PATTERN);
        break;
    }

    case RSPROT_IK_SPOTANIM:
    {
        int has_loop = prim_of(d, RSPROT_IV_LOOP) != RSPROT_IP_NONE;

        n = k->list.count = rd(b, d, RSPROT_IV_COUNT);
        for (int32_t i = 0; i < n && !b->err; i++) {
            RsprotInfoSpot *s = &rsprot_info_emit(out, RSPROT_IO_SPOT, who)->spot;

            s->slot = rd(b, d, RSPROT_IV_SLOT);
            s->id = rd(b, d, RSPROT_IV_ID);
            s->height_delay = rd(b, d, RSPROT_IV_HEIGHT_DELAY);
            s->loop = has_loop ? rd(b, d, RSPROT_IV_LOOP) : 0;
        }
        break;
    }

    /* decodeHit (227-237): hits by type -- 0x7FFE a bare delay, 0x7FFF a soak
     * hit (main + soak parts), anything else a plain one -- then the
     * headbars. */
    case RSPROT_IK_HITS:
        n = k->list.count = rd(b, d, RSPROT_IV_COUNT);
        for (int32_t i = 0; i < n && !b->err; i++) {
            RsprotInfoHit *h = &rsprot_info_emit(out, RSPROT_IO_HIT, who)->hit;
            int32_t type = rd(b, d, RSPROT_IV_TYPE);

            h->soak_type = -1;
            h->soak_value = -1;
            h->limit = -1;
            if (type == 0x7FFE) {
                h->type = 0x7FFE;
                h->value = -1;
                h->delay = rd(b, d, RSPROT_IV_DELAY);
            } else if (type == 0x7FFF) {
                h->type = rd(b, d, RSPROT_IV_MAIN_TYPE);
                h->value = rd(b, d, RSPROT_IV_VALUE);
                h->soak_type = rd(b, d, RSPROT_IV_SOAK_TYPE);
                h->soak_value = rd(b, d, RSPROT_IV_SOAK_VALUE);
                h->delay = rd(b, d, RSPROT_IV_DELAY);
            } else {
                h->type = type;
                h->value = rd(b, d, RSPROT_IV_VALUE);
                h->delay = rd(b, d, RSPROT_IV_DELAY);
            }
        }
        n = rd(b, d, RSPROT_IV_COUNT2);
        k->list.count2 = n;
        bars(b, d, n, out, who);
        break;

    case RSPROT_IK_HITMARKS:
        n = k->list.count = rd(b, d, RSPROT_IV_COUNT);
        for (int32_t i = 0; i < n && !b->err; i++) {
            RsprotInfoHit *h = &rsprot_info_emit(out, RSPROT_IO_HIT, who)->hit;

            h->type = rd(b, d, RSPROT_IV_TYPE);
            h->value = rd(b, d, RSPROT_IV_VALUE);
            h->delay = rd(b, d, RSPROT_IV_DELAY);
            h->limit = rd(b, d, RSPROT_IV_LIMIT);
            h->soak_type = -1;
            h->soak_value = -1;
        }
        break;

    case RSPROT_IK_HEADBARS:
        k->list.count = rd(b, d, RSPROT_IV_COUNT);
        bars(b, d, k->list.count, out, who);
        break;

    /* The length and the body's byte order are the revision's. */
    case RSPROT_IK_APPEARANCE:
        k->appearance.data_len = rsprot_info_read(b, (RsprotInfoPrim)side->appearance_len_prim);
        k->appearance.data_order = side->appearance_data_prim;
        k->appearance.data = borrow(b, k->appearance.data_len);
        break;

    /* A 32-bit mask, then one animation per set bit, in bit order. */
    case RSPROT_IK_BAS_CHANGE:
        k->bas.mask = rd(b, d, RSPROT_IV_FLAG);
        for (int bit = 0; bit < 15; bit++)
            k->bas.anims[bit] = (k->bas.mask & (1 << bit)) != 0
                                    ? rd(b, d, (RsprotInfoVar)(RSPROT_IV_BAS0 + bit))
                                    : -1;
        break;

    /* Flag byte: 0x1 reset (nothing follows), 0x2 counted models, 0x4 / 0x8
     * recolours / retextures as many as the npc TYPE has, 0x10 a mirror byte. */
    case RSPROT_IK_CUSTOM_TYPED:
    case RSPROT_IK_HEAD_CUSTOM_TYPED:
        k->custom.flag = rd(b, d, RSPROT_IV_FLAG);
        k->custom.model_count = -1;
        k->custom.recolour_count = -1;
        k->custom.retexture_count = -1;
        k->custom.identkit_count = -1;
        k->custom.mirror = -1;
        k->custom.body_type = -1;
        k->custom.recol_all = -1;
        if ((k->custom.flag & 0x1) != 0)
            break;
        if ((k->custom.flag & 0x2) != 0) {
            n = k->custom.model_count = rd(b, d, RSPROT_IV_MODEL_COUNT);
            for (int32_t i = 0; i < n && !b->err; i++)
                rsprot_info_emit(out, RSPROT_IO_MODEL, who)->item =
                    model_value(d, rd(b, d, RSPROT_IV_MODEL));
        }
        if ((k->custom.flag & (0x4 | 0x8)) != 0) {
            int recol = 0;
            int retex = 0;
            int head = d->kind == RSPROT_IK_HEAD_CUSTOM_TYPED;

            if (!lengths || !lengths(ctx, who, head, &recol, &retex) || recol < 0 || retex < 0)
                return 0;
            if ((k->custom.flag & 0x4) != 0) {
                k->custom.recolour_count = recol;
                for (int i = 0; i < recol && !b->err; i++)
                    rsprot_info_emit(out, RSPROT_IO_RECOLOUR, who)->item =
                        rd(b, d, RSPROT_IV_RECOLOUR);
            }
            if ((k->custom.flag & 0x8) != 0) {
                k->custom.retexture_count = retex;
                for (int i = 0; i < retex && !b->err; i++)
                    rsprot_info_emit(out, RSPROT_IO_RETEXTURE, who)->item =
                        rd(b, d, RSPROT_IV_RETEXTURE);
            }
        }
        if ((k->custom.flag & 0x10) != 0)
            k->custom.mirror = rd(b, d, RSPROT_IV_MIRROR) == 1;
        break;

    case RSPROT_IK_CUSTOM_V3:
    case RSPROT_IK_CUSTOM_V4:
    {
        int v4 = d->kind == RSPROT_IK_CUSTOM_V4;

        k->custom.flag = rd(b, d, RSPROT_IV_FLAG);
        k->custom.model_count = -1;
        k->custom.recolour_count = -1;
        k->custom.retexture_count = -1;
        k->custom.identkit_count = -1;
        k->custom.mirror = -1;
        k->custom.body_type = -1;
        k->custom.recol_all = -1;
        if ((k->custom.flag & 0x1) != 0)
            break;
        if ((k->custom.flag & 0x2) != 0)
            k->custom.model_count = items(b, d, RSPROT_IV_MODEL_COUNT, RSPROT_IV_MODEL,
                                          RSPROT_IO_MODEL, out, who);
        if ((k->custom.flag & 0x4) != 0) {
            int list = 1;

            if (v4) {
                if (rd(b, d, RSPROT_IV_RECOL_ALL_FLAG) == 1)
                    k->custom.recol_all = rd(b, d, RSPROT_IV_RECOL_ALL);
                list = k->custom.recol_all == -1 || k->custom.recol_all == 65535;
            }
            if (list)
                k->custom.recolour_count = items(b, d, RSPROT_IV_RECOLOUR_COUNT,
                                                 RSPROT_IV_RECOLOUR, RSPROT_IO_RECOLOUR, out, who);
        }
        if ((k->custom.flag & 0x8) != 0)
            k->custom.retexture_count = items(b, d, RSPROT_IV_RETEXTURE_COUNT,
                                              RSPROT_IV_RETEXTURE, RSPROT_IO_RETEXTURE, out, who);
        k->custom.mirror = (k->custom.flag & 0x10) != 0;
        if ((k->custom.flag & 0x20) != 0) {
            k->custom.body_type = rd(b, d, RSPROT_IV_BODY_TYPE);
            k->custom.identkit_count = items(b, d, RSPROT_IV_IDENTKIT_COUNT, RSPROT_IV_IDENTKIT,
                                             RSPROT_IO_IDENTKIT, out, who);
        }
        break;
    }

    case RSPROT_IK_HEADICON_CUSTOM:
        k->headicon.mask = rd(b, d, RSPROT_IV_FLAG);
        for (int i = 0; i < 8 && !b->err; i++) {
            if ((k->headicon.mask & (1 << i)) != 0) {
                RsprotInfoOp *h = rsprot_info_emit(out, RSPROT_IO_HEADICON, who);

                h->headicon.slot = i;
                h->headicon.group = rd(b, d, RSPROT_IV_GROUP);
                h->headicon.index = rd(b, d, RSPROT_IV_INDEX);
            }
        }
        break;

    case RSPROT_IK_NONE:
    case RSPROT_IK_ILLEGAL:
    case RSPROT_IK_COUNT:
        return 0;
    }
    return !b->err;
}

int
rsprot_info_tail(
    RsprotBuf *b, const RsprotInfoSide *side, const int *who, int count, RsprotInfoOut *out,
    RsprotNpcTypeLengths lengths, void *ctx)
{
    for (int i = 0; i < count; i++) {
        uint32_t flag = (uint32_t)rsprot_g1(b);

        for (int e = 0; e < 3 && side->ext[e]; e++)
            if ((flag & side->ext[e]) != 0)
                flag |= (uint32_t)rsprot_g1(b) << (8 * (e + 1));
        if (b->err)
            return 0;
        /* A bit with no known layout cannot be stepped over -- block lengths
         * are not on the wire -- and an illegal one is RSProx's throw. Either
         * ends the tail; everything before it has been written. */
        if ((flag & ~side->known) != 0 || (flag & side->illegal) != 0)
            return 0;

        rsprot_info_emit(out, RSPROT_IO_EXT_BEGIN, who[i]);
        for (int k = 0; k < side->block_count; k++) {
            const RsprotInfoBlockDesc *d = &side->blocks[k];
            int before = out->count;

            if ((flag & d->flag) == 0)
                continue;
            /* A block that does not read to its end is retracted with its
             * entries: every op delivered is one that was read whole (a blob
             * that ran out of payload has no bytes to point at). */
            if (!block(b, side, d, who[i], out, lengths, ctx)) {
                out->count = before;
                return 0;
            }
        }
    }
    return 1;
}
