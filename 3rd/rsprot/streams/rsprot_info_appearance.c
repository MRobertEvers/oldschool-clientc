/*
 * PLAYER_INFO's appearance block, field by field -- see
 * rsprot_info_appearance_trace in rsprot_info.h.
 *
 * Transcribed from RSProx's PlayerInfoClient.decodeAppearance, which has four
 * shapes over 223-241; they differ only in the per-slot obj customisation:
 *
 *   A 223-225  recolour / retexture: an index byte, then a g2 per index not 15
 *   B 226-236  A, then the wear (0x4) and head (0x8) models, g2 each
 *   C 237-240  B with g4 models
 *   D 241      counted recolour / retexture lists of (g1 index, g2 value),
 *              a colour override (0x10: g1 flag, g2 colour), g4 models
 *
 * The names are RSProx's variables'. Every value is as read (g1s signed).
 */
#include "rsprot_info.h"

#include "../include/rsprot_trace.h"

#include <assert.h>

typedef struct Appearance
{
    RsprotBuf *b;
    RsprotTrace *t;
} Appearance;

static int32_t
u1(Appearance *a, const char *name)
{
    int32_t v = rsprot_g1(a->b);

    rsprot_trace_prim(a->t, 0, v, name);
    return v;
}

static int32_t
s1(Appearance *a, const char *name)
{
    int32_t v = rsprot_g1s(a->b);

    rsprot_trace_prim(a->t, 0, v, name);
    return v;
}

static int32_t
u2(Appearance *a, const char *name)
{
    int32_t v = rsprot_g2(a->b);

    rsprot_trace_prim(a->t, 0, v, name);
    return v;
}

static int32_t
u4(Appearance *a, const char *name)
{
    int32_t v = rsprot_g4(a->b);

    rsprot_trace_prim(a->t, 0, v, name);
    return v;
}

static void
str(Appearance *a, const char *name)
{
    int32_t n = 0;
    const char *s = rsprot_gjstr(a->b, &n);

    rsprot_trace_bytes(a->t, RSPROT_TRACE_STR, 0, (const uint8_t *)s, s ? n : -1, name);
}

/* A / B / C: an index byte; each nibble not 15 names a g2 that follows. */
static void
index_pair(Appearance *a, const char *indices, const char *first, const char *second)
{
    int32_t v = u1(a, indices);

    if ((v & 15) != 15)
        u2(a, first);
    if (((v >> 4) & 15) != 15)
        u2(a, second);
}

static void
models(Appearance *a, int32_t flag, int wide)
{
    int32_t (*read)(Appearance *, const char *) = wide ? u4 : u2;

    if ((flag & 0x4) != 0) {
        read(a, "man_wear");
        read(a, "woman_wear");
    }
    if ((flag & 0x8) != 0) {
        read(a, "man_head");
        read(a, "woman_head");
    }
}

static void
replacements(Appearance *a, const char *count, const char *index, const char *value)
{
    int32_t n = u1(a, count);

    for (int32_t i = 0; i < n && !a->b->err; i++) {
        u1(a, index);
        u2(a, value);
    }
}

static void
slot(Appearance *a, int revision)
{
    int32_t flag = u1(a, "slot_flag");

    if (revision >= 241) {
        if ((flag & 0x1) != 0)
            replacements(a, "recolour_count", "recolour_index", "recolour_value");
        if ((flag & 0x2) != 0)
            replacements(a, "retexture_count", "retexture_index", "retexture_value");
        if ((flag & 0x10) != 0) {
            u1(a, "override_colour");
            u2(a, "slot_colour");
        }
        models(a, flag, 1);
        return;
    }
    if ((flag & 0x1) != 0)
        index_pair(a, "recol_indices", "recol1", "recol2");
    if ((flag & 0x2) != 0)
        index_pair(a, "retex_indices", "retex1", "retex2");
    if (revision >= 226)
        models(a, flag, revision >= 237);
}

int
rsprot_info_appearance_trace(int revision, const uint8_t *data, int32_t len, RsprotTrace *trace)
{
    RsprotBuf b;
    Appearance a = { &b, trace };
    int32_t customisation;

    assert(trace);
    if (revision < 223 || revision > 241)
        return 0;
    rsprot_buf_wrap_read(&b, data, len);
    s1(&a, "gender");
    s1(&a, "skull_icon");
    s1(&a, "overhead_icon");
    for (int i = 0; i < 12 && !b.err; i++) {
        int32_t flag = u1(&a, "ident_flag");
        int32_t extra;

        if (flag == 0)
            continue;
        extra = u1(&a, "ident_extra");
        /* The first kit 65535: a transformed player, an npc id instead of kits. */
        if (i == 0 && (flag << 8) + extra == 65535) {
            u2(&a, "transformed_npc_id");
            break;
        }
    }
    for (int i = 0; i < 12 && !b.err; i++)
        if (u1(&a, "interface_value") != 0)
            u1(&a, "interface_extra");
    for (int i = 0; i < 5; i++)
        u1(&a, "colour");
    u2(&a, "ready_anim");
    u2(&a, "turn_anim");
    u2(&a, "walk_anim");
    u2(&a, "walk_anim_back");
    u2(&a, "walk_anim_left");
    u2(&a, "walk_anim_right");
    u2(&a, "run_anim");
    str(&a, "name");
    u1(&a, "combat_level");
    u2(&a, "skill_level");
    u1(&a, "hidden");
    customisation = u2(&a, "customisation_flag");
    if (customisation > 0 && customisation != 32768)
        for (int i = 0; i < 12 && !b.err; i++)
            if (((customisation >> (12 - i)) & 1) != 0)
                slot(&a, revision);
    str(&a, "before_name");
    str(&a, "after_name");
    str(&a, "after_combat_level");
    s1(&a, "text_gender");
    return !b.err;
}
