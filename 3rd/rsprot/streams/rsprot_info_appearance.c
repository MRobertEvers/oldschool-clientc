/*
 * PLAYER_INFO's appearance blob, read whole -- see RsprotAppearance in
 * rsprot_info.h. Transcribed from RSProx's PlayerInfoClient.decodeAppearance;
 * the four versions differ only in a worn slot's obj customisation (slot_v1 ..
 * slot_v4).
 */
#include "rsprot_info.h"

#include <assert.h>
#include <string.h>

/* A string, pointing into the blob (which holds it NUL-terminated). */
static const char *
str(RsprotBuf *b)
{
    int32_t n = 0;
    const char *s = rsprot_gjstr(b, &n);

    return s ? s : "";
}

/* v1-v3: an index byte; each nibble not 15 names a g2 that follows. */
static void
index_pair(RsprotBuf *b, uint8_t *indices, int32_t value[2])
{
    *indices = (uint8_t)rsprot_g1(b);
    if ((*indices & 15) != 15)
        value[0] = rsprot_g2(b);
    if (((*indices >> 4) & 15) != 15)
        value[1] = rsprot_g2(b);
}

static void
models(RsprotBuf *b, RsprotAppearanceObj *o, int wide)
{
    int32_t (*read)(RsprotBuf *) = wide ? rsprot_g4 : rsprot_g2;

    if ((o->flag & 0x4) != 0) {
        o->man_wear = read(b);
        o->woman_wear = read(b);
    }
    if ((o->flag & 0x8) != 0) {
        o->man_head = read(b);
        o->woman_head = read(b);
    }
}

static void
appearance_pairs(RsprotBuf *b, RsprotAppearanceObj *o)
{
    if ((o->flag & 0x1) != 0)
        index_pair(b, &o->recolour_indices, o->recolour);
    if ((o->flag & 0x2) != 0)
        index_pair(b, &o->retexture_indices, o->retexture);
}

static void
slot_v1(RsprotBuf *b, RsprotAppearanceObj *o)
{
    appearance_pairs(b, o);
}

static void
slot_v2(RsprotBuf *b, RsprotAppearanceObj *o)
{
    appearance_pairs(b, o);
    models(b, o, 0);
}

static void
slot_v3(RsprotBuf *b, RsprotAppearanceObj *o)
{
    appearance_pairs(b, o);
    models(b, o, 1);
}

static uint8_t
replacements(RsprotBuf *b, RsprotAppearanceReplacement *out)
{
    int32_t n = rsprot_g1(b);

    for (int32_t i = 0; i < n && !b->err; i++) {
        out[i].index = (uint8_t)rsprot_g1(b);
        out[i].value = (uint16_t)rsprot_g2(b);
    }
    return (uint8_t)n;
}

static void
slot_v4(RsprotBuf *b, RsprotAppearanceObj *o)
{
    if ((o->flag & 0x1) != 0)
        o->recolour_count = replacements(b, o->recolours);
    if ((o->flag & 0x2) != 0)
        o->retexture_count = replacements(b, o->retextures);
    if ((o->flag & 0x10) != 0) {
        o->override_colour = (uint8_t)rsprot_g1(b);
        o->colour = (uint16_t)rsprot_g2(b);
    }
    models(b, o, 1);
}

static int
appearance_read(const uint8_t *data, int32_t len, RsprotAppearance *a,
     void (*slot)(RsprotBuf *, RsprotAppearanceObj *))
{
    RsprotBuf b;

    assert(data);
    assert(a);
    memset(a, 0, sizeof(*a));
    a->npc_id = -1;
    for (int i = 0; i < RSPROT_APPEARANCE_SLOTS; i++) {
        RsprotAppearanceObj *o = &a->objs[i];

        o->recolour[0] = o->recolour[1] = -1;
        o->retexture[0] = o->retexture[1] = -1;
        o->man_wear = o->woman_wear = o->man_head = o->woman_head = -1;
    }
    rsprot_buf_wrap_read(&b, data, len);
    a->gender = (int8_t)rsprot_g1s(&b);
    a->skull_icon = (int8_t)rsprot_g1s(&b);
    a->overhead_icon = (int8_t)rsprot_g1s(&b);
    for (int i = 0; i < RSPROT_APPEARANCE_SLOTS && !b.err; i++) {
        int32_t flag = rsprot_g1(&b);

        if (flag == 0)
            continue;
        a->kits[i] = (uint16_t)((flag << 8) + rsprot_g1(&b));
        if (i == 0 && a->kits[i] == 65535) {
            a->npc_id = rsprot_g2(&b);
            break;
        }
    }
    for (int i = 0; i < RSPROT_APPEARANCE_SLOTS && !b.err; i++) {
        int32_t flag = rsprot_g1(&b);

        if (flag != 0)
            a->interface_kits[i] = (uint16_t)((flag << 8) + rsprot_g1(&b));
    }
    for (int i = 0; i < 5; i++)
        a->colours[i] = (uint8_t)rsprot_g1(&b);
    a->ready_anim = (uint16_t)rsprot_g2(&b);
    a->turn_anim = (uint16_t)rsprot_g2(&b);
    a->walk_anim = (uint16_t)rsprot_g2(&b);
    a->walk_back_anim = (uint16_t)rsprot_g2(&b);
    a->walk_left_anim = (uint16_t)rsprot_g2(&b);
    a->walk_right_anim = (uint16_t)rsprot_g2(&b);
    a->run_anim = (uint16_t)rsprot_g2(&b);
    a->name = str(&b);
    a->combat_level = (uint8_t)rsprot_g1(&b);
    a->skill_level = (uint16_t)rsprot_g2(&b);
    a->hidden = (uint8_t)rsprot_g1(&b);
    a->customisation = (uint16_t)rsprot_g2(&b);
    if (a->customisation > 0 && a->customisation != 32768) {
        for (int i = 0; i < RSPROT_APPEARANCE_SLOTS && !b.err; i++) {
            if (((a->customisation >> (12 - i)) & 1) != 0) {
                a->objs[i].flag = (uint8_t)rsprot_g1(&b);
                slot(&b, &a->objs[i]);
            }
        }
    }
    a->before_name = str(&b);
    a->after_name = str(&b);
    a->after_combat_level = str(&b);
    a->text_gender = (int8_t)rsprot_g1s(&b);
    return !b.err;
}

int
rsprot_appearance_v1(const uint8_t *data, int32_t len, RsprotAppearance *out)
{
    return appearance_read(data, len, out, slot_v1);
}

int
rsprot_appearance_v2(const uint8_t *data, int32_t len, RsprotAppearance *out)
{
    return appearance_read(data, len, out, slot_v2);
}

int
rsprot_appearance_v3(const uint8_t *data, int32_t len, RsprotAppearance *out)
{
    return appearance_read(data, len, out, slot_v3);
}

int
rsprot_appearance_v4(const uint8_t *data, int32_t len, RsprotAppearance *out)
{
    return appearance_read(data, len, out, slot_v4);
}

RsprotAppearanceReadFn
rsprot_appearance_reader(int revision)
{
    if (revision < 223 || revision > 241)
        return NULL;
    if (revision <= 225)
        return rsprot_appearance_v1;
    if (revision <= 236)
        return rsprot_appearance_v2;
    if (revision <= 240)
        return rsprot_appearance_v3;
    return rsprot_appearance_v4;
}
