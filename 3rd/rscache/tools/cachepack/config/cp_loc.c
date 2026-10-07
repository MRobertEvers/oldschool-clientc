#include "cachepack.h"

#include "datatypes/dat2_config_loc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*
 * Locs (scenery).
 *
 * The widest record in the cache. The library's decoder still consumes a block of
 * RS2-era opcodes without storing them (see RSCache_Dat2ConfigLocField), so the
 * type stays lossy for those; everything an OldSchool record states is kept.
 *
 * Every key is written for every record, `key=default` when the stream does not
 * state it, straight from the decoder's presence bits -- never from a value. That
 * is what the old text got wrong half the time: `active` and `raiseobject` are
 * derived after decode when unstated, so the packer wrote 19 and 75 into 60,000
 * records that never had them, and an `ambient=0` or `resizex=128` the stream did
 * state looked like a default and vanished.
 *
 * Keys and values are spelled exactly as the text always spelled them -- readers
 * outside this tool parse them (the quest gate's collision model reads
 * `blockwalk` as 0/1 and `active=1`) -- and only the decision to write one moved
 * from the value to the presence bit. Where one old key carries several opcodes
 * the value still says which: `blockwalk=0` is 17 and `blockwalk=1` is 27,
 * `blockrange=0` is 18 or the 17 that also clears it, `contourgroundtype` names
 * the contour opcode (1 = 21, 2 = 81, 3 = 93, 4 = 94, 5 = 95), and the trailing
 * `multilocN` is -1 for 77 and 92's default loc otherwise. Three new keys carry
 * what those values alone cannot:
 *
 *   blockwalkalso=0      17 was stated beneath a later 27 (`blockwalk=1`)
 *   blockrangealso=0     18 was stated beside a 17 (which already reads
 *                        `blockrange=0`)
 *   multidefaultnone=yes 92 was stated with no default loc (the trailing
 *                        `multilocN=-1` reads as 77 otherwise)
 *
 * They are `default` on every cache.osrs239 record except the 6 that state 17
 * then 27 and the 5 that state 17 and 18; none needs the third.
 *
 * The models list is the one genuinely awkward shape. A loc carries parallel
 * `shapes` / `models` / `lengths` arrays: one *shape* (wall, corner, roof, centre
 * piece, ...) owning a list of model ids, because the map's shape selector picks
 * which list to draw. Opcode 1 is written one line per shape (`shapeN=<shape>,<model>..`),
 * opcode 5 as one flat `models=` line; rev 237+ spells them 6 / 7 with int ids,
 * which is the codec's business, not the text's.
 */

static int
loc_flags(const struct CP_Ctx* ctx)
{
    return RSCache_Dat2ConfigLocFlags(&ctx->profile);
}

static int
loc_applies_oldschool(const struct CP_Ctx* ctx)
{
    return !(loc_flags(ctx) & RSCACHE_CONFIG_LOC_DECODE_RS2);
}

static int
loc_applies_rs2(const struct CP_Ctx* ctx)
{
    return (loc_flags(ctx) & RSCACHE_CONFIG_LOC_DECODE_RS2) != 0;
}

static int
loc_applies_220(const struct CP_Ctx* ctx)
{
    return (loc_flags(ctx) & RSCACHE_CONFIG_LOC_DECODE_OSRS_220) != 0;
}

static int
loc_applies_pre220(const struct CP_Ctx* ctx)
{
    return !loc_applies_220(ctx);
}

/* The ambient-sound retain byte: not in RS2, not in the Kronos build. */
static int
loc_applies_retain(const struct CP_Ctx* ctx)
{
    return !(loc_flags(ctx) &
             (RSCACHE_CONFIG_LOC_DECODE_KRONOS | RSCACHE_CONFIG_LOC_DECODE_RS2));
}

static int
loc_applies_entity_ops(const struct CP_Ctx* ctx)
{
    return (loc_flags(ctx) & RSCACHE_CONFIG_LOC_DECODE_REV237_ENTITY_OPS) != 0;
}

/* Rev 530 reads opcode 95 with no payload. */
static int
loc_contour5_bare(const struct CP_Ctx* ctx)
{
    return (loc_flags(ctx) & RSCACHE_CONFIG_LOC_DECODE_RS2_530) != 0;
}

/* In the order they are written. One key per opcode; 77/92 and 78/79 spread
 * their payloads over several keys. */
const struct CP_KeySpec cp_loc_keys[] = {
    { "name", 0, NULL, NULL },
    { "desc", 0, NULL, NULL },
    { "shape", CP_KEY_INDEXED, NULL, NULL },
    { "models", CP_KEY_LIST, NULL, NULL },
    { "width", 0, NULL, NULL },
    { "length", 0, NULL, NULL },
    { "blockwalk", 0, NULL, NULL },
    { "blockwalkalso", 0, NULL, NULL },
    { "blockrange", 0, NULL, NULL },
    { "blockrangealso", 0, NULL, NULL },
    { "active", 0, NULL, NULL },
    { "contourground", 0, NULL, NULL },
    { "contourgroundtype", 0, NULL, NULL },
    { "contourgroundparam", 0, NULL, NULL },
    { "sharelight", 0, NULL, NULL },
    { "occlude", 0, NULL, NULL },
    { "anim", 0, NULL, NULL },
    { "wallwidth", 0, NULL, NULL },
    { "ambient", 0, NULL, NULL },
    { "op1", 0, NULL, NULL },
    { "op2", 0, NULL, NULL },
    { "op3", 0, NULL, NULL },
    { "op4", 0, NULL, NULL },
    { "op5", 0, NULL, NULL },
    { "op6", 0, NULL, NULL },
    { "op7", 0, NULL, NULL },
    { "op8", 0, NULL, NULL },
    { "op9", 0, NULL, NULL },
    { "contrast", 0, NULL, NULL },
    { "recol", CP_KEY_INDEXED, NULL, NULL },
    { "retex", CP_KEY_INDEXED, NULL, NULL },
    { "mapfunction", 0, NULL, NULL },
    { "category", 0, NULL, NULL },
    { "mirror", 0, NULL, NULL },
    { "shadow", 0, NULL, NULL },
    { "resizex", 0, NULL, NULL },
    { "resizey", 0, NULL, NULL },
    { "resizez", 0, NULL, NULL },
    { "mapscene", 0, NULL, NULL },
    { "forceapproach", 0, NULL, NULL },
    { "offsetx", 0, NULL, NULL },
    { "offsety", 0, NULL, NULL },
    { "offsetz", 0, NULL, NULL },
    { "forcedecor", 0, NULL, NULL },
    { "breakroutefinding", 0, NULL, NULL },
    { "raiseobject", 0, NULL, NULL },
    { "multivarbit", 0, NULL, NULL },
    { "multivarp", 0, NULL, NULL },
    { "multiloc", CP_KEY_INDEXED, NULL, NULL },
    { "multidefaultnone", 0, NULL, NULL },
    { "soundid", 0, NULL, NULL },
    { "soundmintick", 0, NULL, NULL },
    { "soundmaxtick", 0, NULL, NULL },
    { "soundrandom", CP_KEY_INDEXED, NULL, NULL },
    { "sounddistance", 0, NULL, NULL },
    { "soundretain", 0, loc_applies_retain, NULL },
    { "randomanimstart", 0, NULL, NULL },
    { "deferanimchange", 0, loc_applies_oldschool, NULL },
    { "sounddistancefade", 0, loc_applies_220, NULL },
    { "soundfadeincurve", 0, loc_applies_220, NULL },
    { "soundfadein", 0, loc_applies_220, NULL },
    { "soundfadeoutcurve", 0, loc_applies_220, NULL },
    { "soundfadeout", 0, loc_applies_220, NULL },
    { "opcode94", 0, loc_applies_oldschool, NULL },
    { "soundvisibility", 0, loc_applies_220, NULL },
    { "raise", 0, loc_applies_220, NULL },
    { "subop", CP_KEY_LIST, loc_applies_entity_ops, NULL },
    { "condop", CP_KEY_LIST, loc_applies_entity_ops, NULL },
    { "condsubop", CP_KEY_LIST, loc_applies_entity_ops, NULL },
    { "randomanim", CP_KEY_INDEXED, NULL, NULL },
    { "campaign", CP_KEY_INDEXED, NULL, NULL },
    { "param", CP_KEY_LIST, NULL, NULL },
    { NULL, 0, NULL, NULL },
};

/* ---- unpack ---------------------------------------------------------------- */

/* A reference the wire spells 65535 for "none" is -1 in the struct and `-1` in the
 * text: the record states it, it just states nothing. */
static void
emit_ref(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const char* key,
    enum CP_TypeId type,
    int id)
{
    if( id < 0 )
        cp_lines_addf(out, "%s=%d", key, id);
    else
        cp_emit_name(ctx, out, key, type, id);
}

static void
emit_int(
    struct CP_Lines* out,
    const char* key,
    bool present,
    int value)
{
    if( present )
        cp_lines_addf(out, "%s=%d", key, value);
    else
        cp_lines_add_default(out, key);
}

/* A payload-free opcode: its presence is the value, spelled as what it means. */
static void
emit_flag(
    struct CP_Lines* out,
    const char* key,
    bool present,
    const char* word)
{
    if( present )
        cp_lines_addf(out, "%s=%s", key, word);
    else
        cp_lines_add_default(out, key);
}

static void
emit_models(
    const struct RSCache_Dat2ConfigLoc* entry,
    struct CP_Lines* out)
{
    const struct RSCache_Presence* has = &entry->present;
    bool shaped = RSCache_PresenceHas(has, RSCACHE_LOC_FIELD_MODELS);
    bool flat = RSCache_PresenceHas(has, RSCACHE_LOC_FIELD_MODELS_FLAT);

    /* The decoder states at most one: the table belongs to the opcode that
     * installed it last. */
    assert(!(shaped && flat));

    if( !shaped )
        cp_lines_add_default(out, "shape");
    if( !flat )
        cp_lines_add_default(out, "models");
    if( !shaped && !flat )
        return;

    if( entry->shapes_and_model_count == 0 )
    {
        cp_lines_add_empty(out, shaped ? "shape" : "models");
        return;
    }
    /* Opcode 1 gives every model its own shape; opcode 5 one flat list that
     * answers whatever shape the map asks for, and the decoder marks it by
     * leaving `shapes` NULL. */
    if( shaped )
        assert(entry->shapes);
    else
        assert(!entry->shapes);

    for( int s = 0; s < entry->shapes_and_model_count; s++ )
    {
        char buf[4096];
        int w = 0;
        buf[0] = '\0';
        for( int m = 0; m < entry->lengths[s]; m++ )
        {
            w += snprintf(buf + w, sizeof(buf) - (size_t)w, m ? ",%d" : "%d",
                          entry->models[s][m]);
            assert(w < (int)sizeof(buf) - 12);
        }
        if( shaped )
            cp_lines_addf(out, "shape%d=%d%s%s", s + 1, entry->shapes[s], w ? "," : "", buf);
        else if( w )
            cp_lines_addf(out, "models=%s", buf);
        else
            cp_lines_add_empty(out, "models");
    }
}

static void
emit_loc(
    struct CP_Ctx* ctx,
    const struct RSCache_Dat2ConfigLoc* entry,
    struct CP_Lines* out)
{
    const struct RSCache_Presence* has = &entry->present;

#define LOC_HAS(field) RSCache_PresenceHas(has, RSCACHE_LOC_FIELD_##field)

    if( LOC_HAS(NAME) )
        cp_lines_add_str(out, "name", entry->name);
    else
        cp_lines_add_default(out, "name");
    if( LOC_HAS(DESC) )
        cp_lines_add_str(out, "desc", entry->desc);
    else
        cp_lines_add_default(out, "desc");

    emit_models(entry, out);

    emit_int(out, "width", LOC_HAS(SIZE_X), entry->size_x);
    emit_int(out, "length", LOC_HAS(SIZE_Z), entry->size_z);
    /* 17 reads `blockwalk=0` (and clears projectiles, `blockrange=0`), 27 reads
     * `blockwalk=1`, 18 reads `blockrange=0`: the values the struct takes from
     * them before RSCache_Dat2ConfigLocFinish folds in `breakroutefinding`. A
     * 17 after a 27 is no longer stated (the decoder drops the 27), so a record
     * holding both stated 27 last. */
    bool unsolid = LOC_HAS(UNSOLID);
    bool interact_1 = LOC_HAS(INTERACT_TYPE_1);
    bool range_pass = LOC_HAS(PROJECTILES_PASS);
    emit_flag(out, "blockwalk", unsolid || interact_1, interact_1 ? "1" : "0");
    emit_flag(out, "blockwalkalso", unsolid && interact_1, "0");
    emit_flag(out, "blockrange", unsolid || range_pass, "0");
    emit_flag(out, "blockrangealso", unsolid && range_pass, "0");
    emit_int(out, "active", LOC_HAS(INTERACTIVE), entry->is_interactive);

    /* One contour opcode at most (the decoder keeps the last); its mode is
     * `contourgroundtype`, and the other two keys are the fields it sets. */
    bool contour_1 = LOC_HAS(CONTOUR_GROUND);
    bool contour_2 = LOC_HAS(CONTOUR_GROUND_HEIGHT);
    bool contour_3 = LOC_HAS(CONTOUR_TYPE_3);
    bool contour_4 = LOC_HAS(CONTOUR_TYPE_4);
    bool contour_5 = LOC_HAS(CONTOUR_TYPE_5);
    assert(contour_1 + contour_2 + contour_3 + contour_4 + contour_5 <= 1);
    emit_int(out, "contourground", contour_1 || contour_2, entry->contoured_ground);
    emit_int(out, "contourgroundtype", contour_1 || contour_2 || contour_3 || contour_4 || contour_5,
             entry->contour_ground_type);
    emit_int(out, "contourgroundparam",
             contour_2 || contour_3 || (contour_5 && !loc_contour5_bare(ctx)),
             entry->contour_ground_param);

    emit_flag(out, "sharelight", LOC_HAS(SHARELIGHT), "1");
    emit_flag(out, "occlude", LOC_HAS(OCCLUDE), "1");
    if( LOC_HAS(ANIM) )
        emit_ref(ctx, out, "anim", CP_TYPE_SEQ, entry->seq_id);
    else
        cp_lines_add_default(out, "anim");
    emit_int(out, "wallwidth", LOC_HAS(WALL_WIDTH), entry->wall_width);
    emit_int(out, "ambient", LOC_HAS(AMBIENT), entry->ambient);

    for( int i = 0; i < 9; i++ )
    {
        char key[8];
        snprintf(key, sizeof(key), "op%d", i + 1);
        if( !RSCache_PresenceHas(has, RSCACHE_LOC_FIELD_OP1 + i) )
            cp_lines_add_default(out, key);
        else if( entry->actions[i] )
            cp_lines_add_str(out, key, entry->actions[i]);
        else
            /* Stated "hidden"; the client keeps nothing, the text keeps the
             * spelling so the record writes back as it was. */
            cp_lines_add_str(out, key,
                             entry->hidden_actions[i] ? entry->hidden_actions[i] : "Hidden");
    }

    emit_int(out, "contrast", LOC_HAS(CONTRAST), entry->contrast);

    if( !LOC_HAS(RECOLOURS) )
        cp_lines_add_default(out, "recol");
    else if( entry->recolor_count == 0 )
        cp_lines_add_empty(out, "recol");
    else
        cp_emit_recols(out, entry->recolors_from, entry->recolors_to, entry->recolor_count,
                       "recol");
    if( !LOC_HAS(RETEXTURES) )
        cp_lines_add_default(out, "retex");
    else if( entry->retexture_count == 0 )
        cp_lines_add_empty(out, "retex");
    else
        cp_emit_recols(out, entry->retextures_from, entry->retextures_to,
                       entry->retexture_count, "retex");

    emit_int(out, "mapfunction", LOC_HAS(MAP_FUNCTION), entry->map_function_id);
    /* The id, not the name — see the note above `cp_resolve_category`. Config
     * opcode 61, the same id space `cp_npc.c` writes at `category=`. */
    emit_int(out, "category", LOC_HAS(CATEGORY), entry->category);
    emit_flag(out, "mirror", LOC_HAS(MIRROR), "1");
    emit_flag(out, "shadow", LOC_HAS(NO_SHADOW), "no");
    emit_int(out, "resizex", LOC_HAS(RESIZE_X), entry->resize_x);
    emit_int(out, "resizey", LOC_HAS(RESIZE_HEIGHT), entry->resize_height);
    emit_int(out, "resizez", LOC_HAS(RESIZE_Z), entry->resize_z);
    emit_int(out, "mapscene", LOC_HAS(MAP_SCENE), entry->map_scene_id);
    /* LostCity spells this one `forceapproach` in its loc configs. */
    emit_int(out, "forceapproach", LOC_HAS(FORCE_APPROACH), entry->force_approach);
    emit_int(out, "offsetx", LOC_HAS(OFFSET_X), entry->offset_x);
    emit_int(out, "offsety", LOC_HAS(OFFSET_Y), entry->offset_y);
    emit_int(out, "offsetz", LOC_HAS(OFFSET_Z), entry->offset_z);
    emit_flag(out, "forcedecor", LOC_HAS(FORCE_DECOR), "1");
    emit_flag(out, "breakroutefinding", LOC_HAS(BREAK_ROUTEFINDING), "1");
    emit_int(out, "raiseobject", LOC_HAS(RAISE_OBJECT), entry->support_items);

    /* 77 and 92: the varbit, the varp, and the locs, the last of which is 92's
     * default loc or 77's -1. */
    if( LOC_HAS(MULTI) || LOC_HAS(MULTI_DEFAULT) )
    {
        assert(entry->transforms);
        assert(entry->transform_count >= 2);
        int trailing = entry->transforms[entry->transform_count - 1];
        if( LOC_HAS(MULTI) )
            assert(trailing == -1);
        emit_ref(ctx, out, "multivarbit", CP_TYPE_VARBIT, entry->transform_varbit);
        emit_ref(ctx, out, "multivarp", CP_TYPE_VARP, entry->transform_varp);
        for( int i = 0; i < entry->transform_count; i++ )
        {
            char key[24];
            snprintf(key, sizeof(key), "multiloc%d", i + 1);
            emit_ref(ctx, out, key, CP_TYPE_LOC, entry->transforms[i]);
        }
        emit_flag(out, "multidefaultnone", LOC_HAS(MULTI_DEFAULT) && trailing == -1, "yes");
    }
    else
    {
        cp_lines_add_default(out, "multivarbit");
        cp_lines_add_default(out, "multivarp");
        cp_lines_add_default(out, "multiloc");
        cp_lines_add_default(out, "multidefaultnone");
    }

    /* 78 and 79 share distance and retain. */
    bool sound = LOC_HAS(SOUND);
    bool sound_random = LOC_HAS(SOUND_RANDOM);
    emit_int(out, "soundid", sound, entry->ambient_sound_id);
    emit_int(out, "soundmintick", sound_random, entry->ambient_sound_ticks_min);
    emit_int(out, "soundmaxtick", sound_random, entry->ambient_sound_ticks_max);
    if( !sound_random )
        cp_lines_add_default(out, "soundrandom");
    else if( entry->ambient_sound_id_count == 0 )
        cp_lines_add_empty(out, "soundrandom");
    else
    {
        for( int i = 0; i < entry->ambient_sound_id_count; i++ )
            cp_lines_addf(out, "soundrandom%d=%d", i + 1, entry->ambient_sound_ids[i]);
    }
    emit_int(out, "sounddistance", sound || sound_random, entry->ambient_sound_distance);
    if( loc_applies_retain(ctx) )
        emit_int(out, "soundretain", sound || sound_random, entry->ambient_sound_retain);

    emit_flag(out, "randomanimstart", LOC_HAS(NO_RANDOM_ANIM_START), "no");
    if( loc_applies_oldschool(ctx) )
        emit_flag(out, "deferanimchange", LOC_HAS(DEFER_ANIM_CHANGE), "yes");

    if( loc_applies_220(ctx) )
    {
        emit_int(out, "sounddistancefade", LOC_HAS(SOUND_DISTANCE_FADE),
                 entry->sound_distance_fade_curve);
        bool fade = LOC_HAS(SOUND_FADE);
        emit_int(out, "soundfadeincurve", fade, entry->sound_fade_in_curve);
        emit_int(out, "soundfadein", fade, entry->sound_fade_in_duration);
        emit_int(out, "soundfadeoutcurve", fade, entry->sound_fade_out_curve);
        emit_int(out, "soundfadeout", fade, entry->sound_fade_out_duration);
    }

    if( loc_applies_oldschool(ctx) )
        emit_flag(out, "opcode94", LOC_HAS(UNKNOWN1), "yes");

    if( loc_applies_220(ctx) )
    {
        emit_int(out, "soundvisibility", LOC_HAS(SOUND_VISIBILITY), entry->sound_visibility);
        emit_int(out, "raise", LOC_HAS(RAISE), entry->raise);
    }

    if( loc_applies_entity_ops(ctx) )
    {
        /* One opcode per entry: a list is stated exactly when it has entries,
         * so the shared emitter (which writes by count) writes the presence. */
        assert(LOC_HAS(SUB_OPS) == (entry->entity_ops.sub_ops_count > 0));
        assert(LOC_HAS(COND_OPS) == (entry->entity_ops.cond_ops_count > 0));
        assert(LOC_HAS(COND_SUB_OPS) == (entry->entity_ops.cond_sub_ops_count > 0));
        cp_emit_entity_ops(out, entry->actions, 0, &entry->entity_ops);
        if( !LOC_HAS(SUB_OPS) )
            cp_lines_add_default(out, "subop");
        if( !LOC_HAS(COND_OPS) )
            cp_lines_add_default(out, "condop");
        if( !LOC_HAS(COND_SUB_OPS) )
            cp_lines_add_default(out, "condsubop");
    }

    if( !LOC_HAS(RANDOM_ANIMS) )
        cp_lines_add_default(out, "randomanim");
    else if( entry->random_seq_id_count == 0 )
        cp_lines_add_empty(out, "randomanim");
    else
    {
        for( int i = 0; i < entry->random_seq_id_count; i++ )
            cp_lines_addf(out, "randomanim%d=%s,%d", i + 1,
                          cp_name_ensure(ctx, CP_TYPE_SEQ, entry->random_seq_ids[i]),
                          entry->random_seq_delays[i]);
    }

    if( !LOC_HAS(CAMPAIGNS) )
        cp_lines_add_default(out, "campaign");
    else if( entry->campaign_id_count == 0 )
        cp_lines_add_empty(out, "campaign");
    else
    {
        for( int i = 0; i < entry->campaign_id_count; i++ )
            cp_lines_addf(out, "campaign%d=%d", i + 1, entry->campaign_ids[i]);
    }

    if( !LOC_HAS(PARAMS) )
        cp_lines_add_default(out, "param");
    else if( entry->params.count == 0 )
        cp_lines_add_empty(out, "param");
    else
        cp_emit_params(ctx, out, &entry->params);

#undef LOC_HAS
}

int
cp_unpack_loc(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigLoc* entry =
        RSCache_Dat2ConfigLocNewDecodeProfile(&ctx->profile, (char*)record, record_size);
    assert(entry);
    if( entry->_consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "loc %d: consumed %d of %d bytes", id,
                entry->_consumed, record_size);

    emit_loc(ctx, entry, out);

    RSCache_Dat2ConfigLocFree(entry);
    return 1;
}

/* ---- pack ------------------------------------------------------------------ */

/** Shapes arrive as `shapeN=<shape>,<model>,<model>...`, one line per shape. */
struct ShapeList
{
    int* shapes;
    int** models;
    int* lengths;
    int count;
    int capacity;
};

static int
shapes_set(
    struct ShapeList* list,
    int index,
    int shape,
    const int* models,
    int model_count)
{
    if( index < 0 )
        return 0;
    if( index >= list->capacity )
    {
        int next = list->capacity ? list->capacity : 4;
        while( next <= index )
            next *= 2;
        int* shapes = realloc(list->shapes, (size_t)next * sizeof(int));
        assert(shapes);
        list->shapes = shapes;
        int** model_lists = realloc(list->models, (size_t)next * sizeof(int*));
        assert(model_lists);
        list->models = model_lists;
        int* lengths = realloc(list->lengths, (size_t)next * sizeof(int));
        assert(lengths);
        list->lengths = lengths;
        for( int i = list->capacity; i < next; i++ )
        {
            list->shapes[i] = 0;
            list->models[i] = NULL;
            list->lengths[i] = 0;
        }
        list->capacity = next;
    }
    int* copy = malloc((size_t)(model_count > 0 ? model_count : 1) * sizeof(int));
    assert(copy);
    memcpy(copy, models, (size_t)model_count * sizeof(int));
    free(list->models[index]);
    list->shapes[index] = shape;
    list->models[index] = copy;
    list->lengths[index] = model_count;
    if( index >= list->count )
        list->count = index + 1;
    return 1;
}

static void
shapes_free(struct ShapeList* list)
{
    for( int i = 0; i < list->capacity; i++ )
        free(list->models[i]);
    free(list->shapes);
    free(list->models);
    free(list->lengths);
    memset(list, 0, sizeof(*list));
}

/*
 * A payload-free flag key: the value is the one spelling the unpacker writes for
 * it. Anything else is not something the opcode can say.
 */
static int
parse_flag(
    const char* value,
    const char* word)
{
    return strcmp(value, word) == 0;
}

/* A reference that may be the wire's "none" (`-1`). */
static int
parse_ref(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* value,
    int* out)
{
    if( strcmp(value, "-1") == 0 )
    {
        *out = -1;
        return 1;
    }
    return cp_resolve_ref(ctx, type, value, out);
}

/* Keys whose opcode spreads over several lines; each group is all-or-none. */
enum LocPackGroup
{
    GROUP_MULTI_VARBIT,
    GROUP_MULTI_VARP,
    GROUP_MULTI_LOC,
    GROUP_SOUND_MINTICK,
    GROUP_SOUND_MAXTICK,
    GROUP_SOUND_RANDOM,
    GROUP_SOUND_DISTANCE,
    GROUP_SOUND_RETAIN,
    GROUP_SOUND_FADE_IN_CURVE,
    GROUP_SOUND_FADE_IN,
    GROUP_SOUND_FADE_OUT_CURVE,
    GROUP_SOUND_FADE_OUT,
    GROUP_BLOCKRANGE,
    GROUP_BLOCKWALK_ALSO,
    GROUP_BLOCKRANGE_ALSO,
    GROUP_CONTOUR_GROUND,
    GROUP_CONTOUR_PARAM,
    GROUP_MULTI_DEFAULT_NONE,
    GROUP_COUNT
};

uint32_t
cp_pack_loc(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    /* The empty-record decode gives the client defaults and no presence: a field
     * is written only if a line below states it. */
    struct RSCache_Dat2ConfigLoc* entry = RSCache_Dat2ConfigLocNewDecodeProfile(
        &ctx->profile, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    assert(entry);
    (void)id;

    struct ShapeList shapes = { 0 };
    struct CP_IntList flat_models = { 0 };
    struct CP_IntList transforms = { 0 };
    int contour_type = -1;
    struct CP_IntList sounds = { 0 };
    struct CP_IntList campaigns = { 0 };
    struct CP_IntList random_seq = { 0 }, random_delay = { 0 };
    struct CP_IntList recol_s = { 0 }, recol_d = { 0 };
    struct CP_IntList retex_s = { 0 }, retex_d = { 0 };
    bool group_stated[GROUP_COUNT] = { false };
    uint32_t written = 0;

#define LOC_SET(field) RSCache_PresenceSet(&entry->present, RSCACHE_LOC_FIELD_##field)
#define LOC_HAS(field) RSCache_PresenceHas(&entry->present, RSCACHE_LOC_FIELD_##field)

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;
        int index;
        int tmp = 0;

        /* Not stated: the field keeps its client default and no opcode is
         * written. cp_keys_check_config has already made sure it is the only
         * line for its key. */
        if( cp_value_is_default(value) )
            continue;

        if( strcmp(key, "name") == 0 )
        {
            char buf[1024];
            LOC_SET(NAME);
            cp_unescape(value, buf, sizeof(buf));
            free(entry->name);
            entry->name = strdup(buf);
            assert(entry->name);
        }
        else if( strcmp(key, "desc") == 0 )
        {
            char buf[4096];
            LOC_SET(DESC);
            cp_unescape(value, buf, sizeof(buf));
            free(entry->desc);
            entry->desc = strdup(buf);
            assert(entry->desc);
        }
        else if( strcmp(key, "shape") == 0 || (index = cp_indexed_key(key, "shape")) >= 0 )
        {
            LOC_SET(MODELS);
            if( strcmp(key, "shape") == 0 )
                ok = cp_value_is_empty(value);
            else
            {
                char big[8192];
                char* parts[256];
                if( strlen(value) >= sizeof(big) )
                    ok = 0;
                else
                {
                    int n = cp_split(value, big, parts, 256);
                    int shape = 0;
                    int models[255];
                    int model_count = 0;
                    ok = n >= 1 && cp_parse_int(parts[0], &shape);
                    for( int f = 1; f < n && ok; f++ )
                        ok = cp_parse_int(parts[f], &models[model_count++]);
                    if( ok )
                        ok = shapes_set(&shapes, index, shape, models, model_count);
                }
            }
        }
        else if( strcmp(key, "models") == 0 )
        {
            LOC_SET(MODELS_FLAT);
            if( !cp_value_is_empty(value) )
            {
                char big[8192];
                char* parts[256];
                if( strlen(value) >= sizeof(big) )
                    ok = 0;
                else
                {
                    int n = cp_split(value, big, parts, 256);
                    for( int f = 0; f < n && ok; f++ )
                    {
                        int model = 0;
                        ok = cp_parse_int(parts[f], &model);
                        cp_intlist_push(&flat_models, model);
                    }
                }
            }
        }
        else if( strcmp(key, "multiloc") == 0 || (index = cp_indexed_key(key, "multiloc")) >= 0 )
        {
            /* 77 / 92 always list at least one loc, so there is no `empty`. */
            group_stated[GROUP_MULTI_LOC] = true;
            if( strcmp(key, "multiloc") == 0 )
                ok = 0;
            else
            {
                ok = parse_ref(ctx, CP_TYPE_LOC, value, &tmp);
                cp_intlist_set(&transforms, index, tmp);
            }
        }
        else if( strcmp(key, "soundrandom") == 0 ||
                 (index = cp_indexed_key(key, "soundrandom")) >= 0 )
        {
            group_stated[GROUP_SOUND_RANDOM] = true;
            LOC_SET(SOUND_RANDOM);
            if( strcmp(key, "soundrandom") == 0 )
                ok = cp_value_is_empty(value);
            else
            {
                ok = cp_parse_int(value, &tmp);
                cp_intlist_set(&sounds, index, tmp);
            }
        }
        else if( strcmp(key, "campaign") == 0 || (index = cp_indexed_key(key, "campaign")) >= 0 )
        {
            LOC_SET(CAMPAIGNS);
            if( strcmp(key, "campaign") == 0 )
                ok = cp_value_is_empty(value);
            else
            {
                ok = cp_parse_int(value, &tmp);
                cp_intlist_set(&campaigns, index, tmp);
            }
        }
        else if( strcmp(key, "randomanim") == 0 ||
                 (index = cp_indexed_key(key, "randomanim")) >= 0 )
        {
            LOC_SET(RANDOM_ANIMS);
            if( strcmp(key, "randomanim") == 0 )
                ok = cp_value_is_empty(value);
            else
            {
                char scratch[512];
                char* fields[2];
                if( strlen(value) >= sizeof(scratch) ||
                    cp_split(value, scratch, fields, 2) != 2 )
                {
                    ok = 0;
                }
                else
                {
                    int delay = 0;
                    ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, fields[0], &tmp) &&
                         cp_parse_int(fields[1], &delay);
                    cp_intlist_set(&random_seq, index, tmp);
                    cp_intlist_set(&random_delay, index, delay);
                }
            }
        }
        else if( (index = cp_indexed_key(key, "op")) >= 0 && index < 9 )
        {
            char buf[4096];
            RSCache_PresenceSet(&entry->present, RSCACHE_LOC_FIELD_OP1 + index);
            cp_unescape(value, buf, sizeof(buf));
            free(entry->actions[index]);
            free(entry->hidden_actions[index]);
            entry->actions[index] = NULL;
            entry->hidden_actions[index] = NULL;
            /* "hidden", in any case, is held as the client holds it: no action,
             * the spelling kept for the encoder. */
            if( strcasecmp(buf, "hidden") == 0 )
            {
                entry->hidden_actions[index] = strdup(buf);
                assert(entry->hidden_actions[index]);
            }
            else
            {
                entry->actions[index] = strdup(buf);
                assert(entry->actions[index]);
            }
        }
        else if( strcmp(key, "subop") == 0 || strcmp(key, "condop") == 0 ||
                 strcmp(key, "condsubop") == 0 )
        {
            if( key[1] == 'u' )
                LOC_SET(SUB_OPS);
            else if( strcmp(key, "condop") == 0 )
                LOC_SET(COND_OPS);
            else
                LOC_SET(COND_SUB_OPS);
            ok = cp_parse_entity_op(entry->actions, 0, &entry->entity_ops, key, value);
        }
        else if( strcmp(key, "param") == 0 )
        {
            LOC_SET(PARAMS);
            if( !cp_value_is_empty(value) )
                ok = cp_parse_param(ctx, &entry->params, value);
        }
        else if( strncmp(key, "recol", 5) == 0 )
        {
            /* Pairs, collected below; `recol=empty` states a list of none. */
            LOC_SET(RECOLOURS);
            if( strcmp(key, "recol") == 0 )
                ok = cp_value_is_empty(value);
        }
        else if( strncmp(key, "retex", 5) == 0 )
        {
            LOC_SET(RETEXTURES);
            if( strcmp(key, "retex") == 0 )
                ok = cp_value_is_empty(value);
        }
        else if( strcmp(key, "width") == 0 )
        {
            LOC_SET(SIZE_X);
            ok = cp_parse_int(value, &entry->size_x);
        }
        else if( strcmp(key, "length") == 0 )
        {
            LOC_SET(SIZE_Z);
            ok = cp_parse_int(value, &entry->size_z);
        }
        else if( strcmp(key, "blockwalk") == 0 )
        {
            /* 0 is 17, 1 is 27. */
            if( strcmp(value, "0") == 0 )
                LOC_SET(UNSOLID);
            else if( strcmp(value, "1") == 0 )
                LOC_SET(INTERACT_TYPE_1);
            else
                ok = 0;
        }
        else if( strcmp(key, "blockwalkalso") == 0 )
        {
            /* A 17 stated beneath the 27 that `blockwalk=1` shows. */
            group_stated[GROUP_BLOCKWALK_ALSO] = true;
            LOC_SET(UNSOLID);
            ok = parse_flag(value, "0");
        }
        else if( strcmp(key, "blockrange") == 0 )
        {
            /* 18, or the 17 that clears projectiles too: settled below. */
            group_stated[GROUP_BLOCKRANGE] = true;
            ok = parse_flag(value, "0");
        }
        else if( strcmp(key, "blockrangealso") == 0 )
        {
            /* An 18 stated beside a 17, which already reads `blockrange=0`. */
            group_stated[GROUP_BLOCKRANGE_ALSO] = true;
            LOC_SET(PROJECTILES_PASS);
            ok = parse_flag(value, "0");
        }
        else if( strcmp(key, "active") == 0 )
        {
            LOC_SET(INTERACTIVE);
            ok = cp_parse_int(value, &entry->is_interactive);
        }
        else if( strcmp(key, "contourground") == 0 )
        {
            group_stated[GROUP_CONTOUR_GROUND] = true;
            ok = cp_parse_int(value, &entry->contoured_ground);
        }
        else if( strcmp(key, "contourgroundtype") == 0 )
        {
            /* The contour opcode, by the mode it selects: settled below. */
            ok = cp_parse_int(value, &contour_type) && contour_type >= 1 && contour_type <= 5;
            entry->contour_ground_type = contour_type;
        }
        else if( strcmp(key, "contourgroundparam") == 0 )
        {
            group_stated[GROUP_CONTOUR_PARAM] = true;
            ok = cp_parse_int(value, &entry->contour_ground_param);
        }
        else if( strcmp(key, "sharelight") == 0 )
        {
            LOC_SET(SHARELIGHT);
            ok = parse_flag(value, "1");
        }
        else if( strcmp(key, "occlude") == 0 )
        {
            LOC_SET(OCCLUDE);
            ok = parse_flag(value, "1");
        }
        else if( strcmp(key, "anim") == 0 )
        {
            LOC_SET(ANIM);
            ok = parse_ref(ctx, CP_TYPE_SEQ, value, &entry->seq_id);
        }
        else if( strcmp(key, "wallwidth") == 0 )
        {
            LOC_SET(WALL_WIDTH);
            ok = cp_parse_int(value, &entry->wall_width);
        }
        else if( strcmp(key, "ambient") == 0 )
        {
            LOC_SET(AMBIENT);
            ok = cp_parse_int(value, &entry->ambient);
        }
        else if( strcmp(key, "contrast") == 0 )
        {
            LOC_SET(CONTRAST);
            ok = cp_parse_int(value, &entry->contrast);
        }
        else if( strcmp(key, "mapfunction") == 0 )
        {
            LOC_SET(MAP_FUNCTION);
            ok = cp_parse_int(value, &entry->map_function_id);
        }
        /* The one field on a loc whose value may be spelled as a name: the export
         * writes `category=684` and an authored overlay writes
         * `category=door_closed`, both meaning an id in the same namespace. */
        else if( strcmp(key, "category") == 0 )
        {
            LOC_SET(CATEGORY);
            ok = cp_resolve_category(ctx, value, &entry->category);
        }
        else if( strcmp(key, "mirror") == 0 )
        {
            LOC_SET(MIRROR);
            ok = parse_flag(value, "1");
        }
        else if( strcmp(key, "shadow") == 0 )
        {
            LOC_SET(NO_SHADOW);
            ok = parse_flag(value, "no");
        }
        else if( strcmp(key, "resizex") == 0 )
        {
            LOC_SET(RESIZE_X);
            ok = cp_parse_int(value, &entry->resize_x);
        }
        else if( strcmp(key, "resizey") == 0 )
        {
            LOC_SET(RESIZE_HEIGHT);
            ok = cp_parse_int(value, &entry->resize_height);
        }
        else if( strcmp(key, "resizez") == 0 )
        {
            LOC_SET(RESIZE_Z);
            ok = cp_parse_int(value, &entry->resize_z);
        }
        else if( strcmp(key, "mapscene") == 0 )
        {
            LOC_SET(MAP_SCENE);
            ok = cp_parse_int(value, &entry->map_scene_id);
        }
        else if( strcmp(key, "forceapproach") == 0 )
        {
            LOC_SET(FORCE_APPROACH);
            ok = cp_parse_int(value, &entry->force_approach);
        }
        else if( strcmp(key, "offsetx") == 0 )
        {
            LOC_SET(OFFSET_X);
            ok = cp_parse_int(value, &entry->offset_x);
        }
        else if( strcmp(key, "offsety") == 0 )
        {
            LOC_SET(OFFSET_Y);
            ok = cp_parse_int(value, &entry->offset_y);
        }
        else if( strcmp(key, "offsetz") == 0 )
        {
            LOC_SET(OFFSET_Z);
            ok = cp_parse_int(value, &entry->offset_z);
        }
        else if( strcmp(key, "forcedecor") == 0 )
        {
            LOC_SET(FORCE_DECOR);
            ok = parse_flag(value, "1");
        }
        else if( strcmp(key, "breakroutefinding") == 0 )
        {
            LOC_SET(BREAK_ROUTEFINDING);
            ok = parse_flag(value, "1");
        }
        else if( strcmp(key, "raiseobject") == 0 )
        {
            LOC_SET(RAISE_OBJECT);
            ok = cp_parse_int(value, &entry->support_items);
        }
        else if( strcmp(key, "multivarbit") == 0 )
        {
            group_stated[GROUP_MULTI_VARBIT] = true;
            ok = parse_ref(ctx, CP_TYPE_VARBIT, value, &entry->transform_varbit);
        }
        else if( strcmp(key, "multivarp") == 0 )
        {
            group_stated[GROUP_MULTI_VARP] = true;
            ok = parse_ref(ctx, CP_TYPE_VARP, value, &entry->transform_varp);
        }
        else if( strcmp(key, "multidefaultnone") == 0 )
        {
            /* 92 with no default loc, which the trailing -1 alone reads as 77. */
            group_stated[GROUP_MULTI_DEFAULT_NONE] = true;
            ok = parse_flag(value, "yes");
        }
        else if( strcmp(key, "soundid") == 0 )
        {
            LOC_SET(SOUND);
            ok = cp_parse_int(value, &entry->ambient_sound_id);
        }
        else if( strcmp(key, "soundmintick") == 0 )
        {
            group_stated[GROUP_SOUND_MINTICK] = true;
            LOC_SET(SOUND_RANDOM);
            ok = cp_parse_int(value, &entry->ambient_sound_ticks_min);
        }
        else if( strcmp(key, "soundmaxtick") == 0 )
        {
            group_stated[GROUP_SOUND_MAXTICK] = true;
            LOC_SET(SOUND_RANDOM);
            ok = cp_parse_int(value, &entry->ambient_sound_ticks_max);
        }
        else if( strcmp(key, "sounddistance") == 0 )
        {
            group_stated[GROUP_SOUND_DISTANCE] = true;
            ok = cp_parse_int(value, &entry->ambient_sound_distance);
        }
        else if( strcmp(key, "soundretain") == 0 )
        {
            group_stated[GROUP_SOUND_RETAIN] = true;
            ok = cp_parse_int(value, &entry->ambient_sound_retain);
        }
        else if( strcmp(key, "randomanimstart") == 0 )
        {
            LOC_SET(NO_RANDOM_ANIM_START);
            ok = parse_flag(value, "no");
        }
        else if( strcmp(key, "deferanimchange") == 0 )
        {
            LOC_SET(DEFER_ANIM_CHANGE);
            ok = parse_flag(value, "yes");
        }
        else if( strcmp(key, "sounddistancefade") == 0 )
        {
            LOC_SET(SOUND_DISTANCE_FADE);
            ok = cp_parse_int(value, &entry->sound_distance_fade_curve);
        }
        else if( strcmp(key, "soundfadeincurve") == 0 )
        {
            group_stated[GROUP_SOUND_FADE_IN_CURVE] = true;
            LOC_SET(SOUND_FADE);
            ok = cp_parse_int(value, &entry->sound_fade_in_curve);
        }
        else if( strcmp(key, "soundfadein") == 0 )
        {
            group_stated[GROUP_SOUND_FADE_IN] = true;
            LOC_SET(SOUND_FADE);
            ok = cp_parse_int(value, &entry->sound_fade_in_duration);
        }
        else if( strcmp(key, "soundfadeoutcurve") == 0 )
        {
            group_stated[GROUP_SOUND_FADE_OUT_CURVE] = true;
            LOC_SET(SOUND_FADE);
            ok = cp_parse_int(value, &entry->sound_fade_out_curve);
        }
        else if( strcmp(key, "soundfadeout") == 0 )
        {
            group_stated[GROUP_SOUND_FADE_OUT] = true;
            LOC_SET(SOUND_FADE);
            ok = cp_parse_int(value, &entry->sound_fade_out_duration);
        }
        else if( strcmp(key, "opcode94") == 0 )
        {
            LOC_SET(UNKNOWN1);
            ok = parse_flag(value, "yes");
        }
        else if( strcmp(key, "soundvisibility") == 0 )
        {
            LOC_SET(SOUND_VISIBILITY);
            ok = cp_parse_int(value, &entry->sound_visibility);
        }
        else if( strcmp(key, "raise") == 0 )
        {
            LOC_SET(RAISE);
            ok = cp_parse_int(value, &entry->raise);
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "loc [%s]: unknown key %s",
                    config->debugname, key);

        if( !ok )
        {
            fprintf(stderr, "cachepack: loc [%s]: bad value for %s\n", config->debugname, key);
            goto done;
        }
    }

    if( !cp_collect_pairs(config, "recol", &recol_s, &recol_d) ||
        !cp_collect_pairs(config, "retex", &retex_s, &retex_d) )
    {
        fprintf(stderr, "cachepack: loc [%s]: mismatched recolour pairs\n", config->debugname);
        goto done;
    }

    /* One opcode, several keys: all of them or none. */
    {
        bool multi = group_stated[GROUP_MULTI_VARBIT] || group_stated[GROUP_MULTI_VARP] ||
                     group_stated[GROUP_MULTI_LOC];
        bool multi_all = group_stated[GROUP_MULTI_VARBIT] && group_stated[GROUP_MULTI_VARP] &&
                         group_stated[GROUP_MULTI_LOC];
        bool sound_random = LOC_HAS(SOUND_RANDOM);
        bool sound_random_all = group_stated[GROUP_SOUND_MINTICK] &&
                                group_stated[GROUP_SOUND_MAXTICK] &&
                                group_stated[GROUP_SOUND_RANDOM];
        bool any_sound = LOC_HAS(SOUND) || sound_random;
        bool retain_applies = loc_applies_retain(ctx);

        if( multi != multi_all || (group_stated[GROUP_MULTI_DEFAULT_NONE] && !multi) )
        {
            fprintf(stderr,
                    "cachepack: loc [%s]: multivarbit, multivarp and multiloc are one opcode "
                    "(77, or 92) -- state all of them or none\n",
                    config->debugname);
            goto done;
        }
        /* The last multiloc is 92's default loc, or 77's -1. */
        if( multi )
        {
            int trailing = transforms.count >= 2 ? transforms.items[transforms.count - 1] : 0;
            if( transforms.count < 2 ||
                (group_stated[GROUP_MULTI_DEFAULT_NONE] && trailing != -1) )
            {
                fprintf(stderr,
                        "cachepack: loc [%s]: multiloc lists the locs and then the default "
                        "(-1 for none); multidefaultnone=yes only with a -1 default\n",
                        config->debugname);
                goto done;
            }
            if( trailing != -1 || group_stated[GROUP_MULTI_DEFAULT_NONE] )
                LOC_SET(MULTI_DEFAULT);
            else
                LOC_SET(MULTI);
        }

        /* 17 reads blockwalk=0 and blockrange=0; an 18 beside it is
         * blockrangealso. Without a 17, blockrange=0 is the 18. */
        if( group_stated[GROUP_BLOCKWALK_ALSO] && !LOC_HAS(INTERACT_TYPE_1) )
        {
            fprintf(stderr, "cachepack: loc [%s]: blockwalkalso=0 is a 17 beneath blockwalk=1\n",
                    config->debugname);
            goto done;
        }
        if( (LOC_HAS(UNSOLID) && !group_stated[GROUP_BLOCKRANGE]) ||
            (group_stated[GROUP_BLOCKRANGE_ALSO] && !LOC_HAS(UNSOLID)) )
        {
            fprintf(stderr,
                    "cachepack: loc [%s]: blockwalk=0 (opcode 17) clears projectiles too, so "
                    "it states blockrange=0; an 18 beside it is blockrangealso=0\n",
                    config->debugname);
            goto done;
        }
        if( group_stated[GROUP_BLOCKRANGE] && !LOC_HAS(UNSOLID) )
            LOC_SET(PROJECTILES_PASS);

        /* contourgroundtype names the opcode; the other two are what it sets. */
        {
            bool bare5 = loc_contour5_bare(ctx);
            bool want_ground = contour_type == 1 || contour_type == 2;
            bool want_param =
                contour_type == 2 || contour_type == 3 || (contour_type == 5 && !bare5);
            bool era_ok = contour_type <= 2 ||
                          (contour_type == 4 ? loc_applies_rs2(ctx) : loc_applies_pre220(ctx));
            bool height_ok = contour_type != 2 ||
                             (entry->contour_ground_param == entry->contoured_ground &&
                              entry->contour_ground_param >= 0 &&
                              entry->contour_ground_param % 256 == 0 &&
                              entry->contour_ground_param / 256 <= 0xFF);
            if( want_ground != group_stated[GROUP_CONTOUR_GROUND] ||
                want_param != group_stated[GROUP_CONTOUR_PARAM] || !era_ok || !height_ok )
            {
                fprintf(stderr,
                        "cachepack: loc [%s]: contourgroundtype %d does not agree with "
                        "contourground / contourgroundparam (1 = 21: ground 0; 2 = 81: ground "
                        "and param, the same multiple of 256; 3 = 93 and 5 = 95: param; 4 = "
                        "94: neither) or with this revision\n",
                        config->debugname, contour_type);
                goto done;
            }
            if( contour_type == 1 )
                LOC_SET(CONTOUR_GROUND);
            else if( contour_type == 2 )
                LOC_SET(CONTOUR_GROUND_HEIGHT);
            else if( contour_type == 3 )
                LOC_SET(CONTOUR_TYPE_3);
            else if( contour_type == 4 )
                LOC_SET(CONTOUR_TYPE_4);
            else if( contour_type == 5 )
                LOC_SET(CONTOUR_TYPE_5);
        }

        if( LOC_HAS(SOUND_FADE) &&
            !(group_stated[GROUP_SOUND_FADE_IN_CURVE] && group_stated[GROUP_SOUND_FADE_IN] &&
              group_stated[GROUP_SOUND_FADE_OUT_CURVE] && group_stated[GROUP_SOUND_FADE_OUT]) )
        {
            fprintf(stderr,
                    "cachepack: loc [%s]: soundfadeincurve, soundfadein, soundfadeoutcurve and "
                    "soundfadeout are one opcode (93) -- state all of them or none\n",
                    config->debugname);
            goto done;
        }
        if( sound_random != sound_random_all || any_sound != group_stated[GROUP_SOUND_DISTANCE] ||
            (retain_applies && any_sound != group_stated[GROUP_SOUND_RETAIN]) )
        {
            fprintf(stderr,
                    "cachepack: loc [%s]: the ambient sound keys do not agree -- soundid (78) "
                    "or soundmintick/soundmaxtick/soundrandom (79), and sounddistance%s with "
                    "either\n",
                    config->debugname, retain_applies ? "/soundretain" : "");
            goto done;
        }
        if( LOC_HAS(MODELS) && LOC_HAS(MODELS_FLAT) )
        {
            fprintf(stderr, "cachepack: loc [%s]: shapeN and models are two forms of the one "
                            "model table -- state one\n",
                    config->debugname);
            goto done;
        }
    }

    if( LOC_HAS(MODELS_FLAT) )
    {
        /* Opcode 5's shape: one model list, no shapes. `shapes == NULL` with a
         * count of 1 is what the decoder produces and what the encoder tests. */
        entry->shapes = NULL;
        entry->lengths = flat_models.count ? &flat_models.count : NULL;
        entry->models = flat_models.count ? &flat_models.items : NULL;
        entry->shapes_and_model_count = flat_models.count ? 1 : 0;
    }
    else
    {
        entry->shapes = shapes.shapes;
        entry->models = shapes.models;
        entry->lengths = shapes.lengths;
        entry->shapes_and_model_count = shapes.count;
    }
    if( LOC_HAS(MULTI) || LOC_HAS(MULTI_DEFAULT) )
    {
        /* The default already sits last, where the decoder parks it. */
        entry->transforms = transforms.items;
        entry->transform_count = transforms.count;
    }
    entry->ambient_sound_ids = sounds.items;
    entry->ambient_sound_id_count = sounds.count;
    entry->campaign_ids = campaigns.items;
    entry->campaign_id_count = campaigns.count;
    entry->random_seq_ids = random_seq.items;
    entry->random_seq_delays = random_delay.items;
    entry->random_seq_id_count = random_seq.count;
    entry->recolors_from = recol_s.items;
    entry->recolors_to = recol_d.items;
    entry->recolor_count = recol_s.count;
    entry->retextures_from = retex_s.items;
    entry->retextures_to = retex_d.items;
    entry->retexture_count = retex_s.count;

    written = RSCache_Dat2ConfigLocEncode(&ctx->profile, entry, out, out_capacity);

#undef LOC_SET
#undef LOC_HAS

done:
    entry->shapes = NULL;
    entry->models = NULL;
    entry->lengths = NULL;
    entry->shapes_and_model_count = 0;
    entry->transforms = NULL;
    entry->ambient_sound_ids = NULL;
    entry->campaign_ids = NULL;
    entry->random_seq_ids = NULL;
    entry->random_seq_delays = NULL;
    entry->recolors_from = entry->recolors_to = NULL;
    entry->retextures_from = entry->retextures_to = NULL;
    RSCache_Dat2ConfigLocFree(entry);
    shapes_free(&shapes);
    cp_intlist_free(&flat_models);
    cp_intlist_free(&transforms);
    cp_intlist_free(&sounds);
    cp_intlist_free(&campaigns);
    cp_intlist_free(&random_seq);
    cp_intlist_free(&random_delay);
    cp_intlist_free(&recol_s);
    cp_intlist_free(&recol_d);
    cp_intlist_free(&retex_s);
    cp_intlist_free(&retex_d);
    return written;
}
