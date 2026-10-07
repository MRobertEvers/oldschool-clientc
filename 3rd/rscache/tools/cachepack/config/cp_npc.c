#include "cachepack.h"

#include "datatypes/dat2_config_npc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * NPCs.
 *
 * Every key is written for every record, straight from the decoder's presence
 * bits: the value when the stream states the field, `key=default` when it does
 * not, `key=empty` for a list it states with no entries. Nothing is decided by
 * comparing a value against a default -- that is what used to drop `size=1`
 * from 6,145 osrs239 npcs, and the clear-flag opcodes 93/107/109 from every
 * record that carried them.
 *
 * Some keys share one field because they share one opcode: the three
 * `walkanim_b/_l/_r` turns are opcode 17, which also carries `walkanim` (14
 * when no turn is stated; the same for run and crawl); the five `sound*` keys
 * are opcode 134; `multivarbit`, `multivarp` and the `multinpc` list are
 * 106/118.
 *
 * The type stays lossy in the register: opcodes the decoder consumes without
 * storing have no key, and a record that states one field twice keeps only the
 * last value.
 */

static unsigned
npc_flags(const struct CP_Ctx* ctx)
{
    return (unsigned)RSCache_Dat2ConfigNpcFlags(&ctx->profile);
}

static int
npc_is_b669(const struct CP_Ctx* ctx)
{
    return (npc_flags(ctx) & RSCACHE_CONFIG_NPC_DECODE_RS2_BUILD669) != 0;
}

/* The build-669 stream consumes these opcodes (or has no such opcode) without
 * storing anything. */
static int
npc_applies_not_b669(const struct CP_Ctx* ctx)
{
    return !npc_is_b669(ctx);
}

/* 114/115/122/123 are run animations and follower flags only in OldSchool. */
static int
npc_applies_oldschool_ops(const struct CP_Ctx* ctx)
{
    return !npc_is_b669(ctx) && !(npc_flags(ctx) & RSCACHE_CONFIG_NPC_DECODE_RS2);
}

/* 111 before rev 233: isFollower + lowPriorityFollowerOps. */
static int
npc_applies_follower(const struct CP_Ctx* ctx)
{
    return !npc_is_b669(ctx) && !(npc_flags(ctx) & RSCACHE_CONFIG_NPC_DECODE_REV233_OP111);
}

/* 111 from rev 233: renderPriority 2. */
static int
npc_applies_rev233(const struct CP_Ctx* ctx)
{
    return (npc_flags(ctx) & RSCACHE_CONFIG_NPC_DECODE_REV233_OP111) != 0;
}

static int
npc_applies_rev231(const struct CP_Ctx* ctx)
{
    return (npc_flags(ctx) & RSCACHE_CONFIG_NPC_DECODE_REV231_FOOTPRINT) != 0;
}

static int
npc_applies_rev234(const struct CP_Ctx* ctx)
{
    return (npc_flags(ctx) & RSCACHE_CONFIG_NPC_DECODE_REV234_FLAGS) != 0;
}

static int
npc_applies_rev235(const struct CP_Ctx* ctx)
{
    return (npc_flags(ctx) & RSCACHE_CONFIG_NPC_DECODE_REV235_OVERLAP) != 0;
}

static int
npc_applies_rev236(const struct CP_Ctx* ctx)
{
    return (npc_flags(ctx) & RSCACHE_CONFIG_NPC_DECODE_REV236_FLAGS) != 0;
}

static int
npc_applies_rev237_ops(const struct CP_Ctx* ctx)
{
    return (npc_flags(ctx) & RSCACHE_CONFIG_NPC_DECODE_REV237_ENTITY_OPS) != 0;
}

/* In the order they are written; the comment names the RSCACHE_NPC_FIELD_*. */
const struct CP_KeySpec cp_npc_keys[] = {
    { "name", 0, NULL, NULL },                                  /* NAME */
    { "desc", 0, NULL, NULL },                                  /* DESC */
    { "model", CP_KEY_INDEXED, NULL, NULL },                    /* MODELS */
    { "head", CP_KEY_INDEXED, NULL, NULL },                     /* CHATHEADS */
    { "size", 0, NULL, NULL },                                  /* SIZE */
    { "readyanim", 0, npc_applies_not_b669, NULL },             /* READY_ANIM */
    { "walkanim", 0, npc_applies_not_b669, NULL },              /* WALK_ANIM */
    { "walkanim_b", 0, npc_applies_not_b669, NULL },            /* WALK_TURN_ANIMS */
    { "walkanim_l", 0, npc_applies_not_b669, NULL },
    { "walkanim_r", 0, npc_applies_not_b669, NULL },
    { "idleleftanim", 0, npc_applies_not_b669, NULL },          /* IDLE_LEFT_ANIM */
    { "idlerightanim", 0, npc_applies_not_b669, NULL },         /* IDLE_RIGHT_ANIM */
    { "runanim", 0, npc_applies_oldschool_ops, NULL },          /* RUN_ANIM */
    { "runanim_b", 0, npc_applies_oldschool_ops, NULL },        /* RUN_TURN_ANIMS */
    { "runanim_l", 0, npc_applies_oldschool_ops, NULL },
    { "runanim_r", 0, npc_applies_oldschool_ops, NULL },
    { "crawlanim", 0, npc_applies_not_b669, NULL },             /* CRAWL_ANIM */
    { "crawlanim_b", 0, npc_applies_not_b669, NULL },           /* CRAWL_TURN_ANIMS */
    { "crawlanim_l", 0, npc_applies_not_b669, NULL },
    { "crawlanim_r", 0, npc_applies_not_b669, NULL },
    { "op1", 0, NULL, NULL },                                   /* OP1..OP5 */
    { "op2", 0, NULL, NULL },
    { "op3", 0, NULL, NULL },
    { "op4", 0, NULL, NULL },
    { "op5", 0, NULL, NULL },
    { "subop", CP_KEY_LIST, npc_applies_rev237_ops, NULL },     /* SUB_OPS */
    { "condop", CP_KEY_LIST, npc_applies_rev237_ops, NULL },    /* COND_OPS */
    { "condsubop", CP_KEY_LIST, npc_applies_rev237_ops, NULL }, /* COND_SUB_OPS */
    { "recol", CP_KEY_INDEXED, NULL, NULL },                    /* RECOLOR */
    { "retex", CP_KEY_INDEXED, NULL, NULL },                    /* RETEXTURE */
    { "minimap", 0, NULL, NULL },                               /* MINIMAP_HIDDEN */
    { "vislevel", 0, NULL, NULL },                              /* COMBAT_LEVEL */
    { "resizeh", 0, NULL, NULL },                               /* WIDTH_SCALE */
    { "resizev", 0, NULL, NULL },                               /* HEIGHT_SCALE */
    { "alwaysontop", 0, NULL, NULL },                           /* RENDER_PRIORITY */
    { "renderpriority", 0, npc_applies_rev233, NULL },          /* RENDER_PRIORITY_HIGH */
    { "follower", 0, npc_applies_follower, NULL },              /* FOLLOWER */
    { "ambient", 0, NULL, NULL },                               /* AMBIENT */
    { "contrast", 0, NULL, NULL },                              /* CONTRAST */
    { "headicon", CP_KEY_INDEXED, NULL, NULL },                 /* HEAD_ICONS */
    { "turnspeed", 0, NULL, NULL },                             /* ROTATION_SPEED */
    { "multivarbit", 0, NULL, NULL },                           /* MULTI */
    { "multivarp", 0, NULL, NULL },
    { "multinpc", CP_KEY_INDEXED, NULL, NULL },
    { "interactable", 0, NULL, NULL },                          /* NOT_INTERACTABLE */
    { "rotationflag", 0, NULL, NULL },                          /* NO_ROTATION_FLAG */
    { "pet", 0, npc_applies_oldschool_ops, NULL },              /* PET */
    { "lowpriorityops", 0, npc_applies_oldschool_ops, NULL },   /* LOW_PRIORITY_OPS */
    { "height", 0, NULL, NULL },                                /* HEIGHT */
    { "category", 0, npc_applies_not_b669, NULL },              /* CATEGORY */
    { "soundidle", 0, NULL, NULL },                             /* MOVEMENT_SOUNDS */
    { "soundcrawl", 0, NULL, NULL },
    { "soundwalk", 0, NULL, NULL },
    { "soundrun", 0, NULL, NULL },
    { "soundradius", 0, NULL, NULL },
    { "soundvolume", 0, npc_applies_not_b669, NULL },           /* SOUND_VOLUME */
    { "stat1", 0, npc_applies_not_b669, NULL },                 /* STAT1..STAT6 */
    { "stat2", 0, npc_applies_not_b669, NULL },
    { "stat3", 0, npc_applies_not_b669, NULL },
    { "stat4", 0, npc_applies_not_b669, NULL },
    { "stat5", 0, npc_applies_not_b669, NULL },
    { "stat6", 0, npc_applies_not_b669, NULL },
    { "bastype", 0, NULL, NULL },                               /* BAS_TYPE */
    { "footprintsize", 0, npc_applies_rev231, NULL },           /* FOOTPRINT_SIZE */
    { "opcode129", 0, npc_applies_rev234, NULL },               /* UNKNOWN129 */
    { "hideforoverlap", 0, npc_applies_rev235, NULL },          /* HIDE_FOR_OVERLAP */
    { "overlaptint", 0, npc_applies_rev235, NULL },             /* OVERLAP_TINT */
    { "idleanimrestart", 0, npc_applies_rev236, NULL },         /* IDLE_ANIM_RESTART */
    { "zbuf", 0, npc_applies_rev236, NULL },                    /* ZBUF_OFF */
    { "param", CP_KEY_LIST, NULL, NULL },                       /* PARAMS */
    { NULL, 0, NULL, NULL },
};

/* ---- unpack -------------------------------------------------------------- */

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

/* A payload-free opcode: its presence is the value, spelled `yes` for a flag it
 * sets, `no` for one it clears. */
static void
emit_flag(
    struct CP_Lines* out,
    const char* key,
    bool present,
    const char* spelling)
{
    if( present )
        cp_lines_addf(out, "%s=%s", key, spelling);
    else
        cp_lines_add_default(out, key);
}

static void
emit_seq(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const char* key,
    bool present,
    int id)
{
    if( present )
        cp_emit_name(ctx, out, key, CP_TYPE_SEQ, id);
    else
        cp_lines_add_default(out, key);
}

/* A var slot in the 106/118 block. 0xFFFF on the wire (-1 here) names no
 * variable, so it is written `default` exactly as the old text omitted it:
 * readers test the key for a variable name. The block itself is still stated
 * by its `multinpc` lines, and the packer writes 0xFFFF for a `default` slot. */
static void
emit_var_slot(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const char* key,
    enum CP_TypeId type,
    int id)
{
    if( id < 0 )
        cp_lines_add_default(out, key);
    else
        cp_emit_name(ctx, out, key, type, id);
}

static void
emit_id_list(
    struct CP_Lines* out,
    const char* stem,
    bool present,
    const int* ids,
    int count)
{
    if( !present )
        cp_lines_add_default(out, stem);
    else if( count == 0 )
        cp_lines_add_empty(out, stem);
    else
    {
        for( int i = 0; i < count; i++ )
            cp_lines_addf(out, "%s%d=%d", stem, i + 1, ids[i]);
    }
}

static void
emit_pairs(
    struct CP_Lines* out,
    const char* stem,
    bool present,
    const int* from,
    const int* to,
    int count)
{
    if( !present )
        cp_lines_add_default(out, stem);
    else if( count == 0 )
        cp_lines_add_empty(out, stem);
    else
        cp_emit_recols(out, from, to, count, stem);
}

/* The rev-237 op lists, one line per entry, in cp_parse_entity_op's spelling.
 * Each list is its own opcode, so each has its own presence bit (the shared
 * cp_emit_entity_ops decides by count). */
static void
emit_entity_op_lists(
    struct CP_Lines* out,
    const struct RSCache_Presence* has,
    const struct RSCache_EntityOps* ops)
{
    char buf[1024];

    if( !RSCache_PresenceHas(has, RSCACHE_NPC_FIELD_SUB_OPS) )
        cp_lines_add_default(out, "subop");
    else if( ops->sub_ops_count == 0 )
        cp_lines_add_empty(out, "subop");
    else
    {
        for( int i = 0; i < ops->sub_ops_count; i++ )
        {
            const struct RSCache_EntitySubOp* s = &ops->sub_ops[i];
            snprintf(buf, sizeof(buf), "%d,%d,%s", s->index, s->sub_id, s->text ? s->text : "");
            cp_lines_add_str(out, "subop", buf);
        }
    }

    if( !RSCache_PresenceHas(has, RSCACHE_NPC_FIELD_COND_OPS) )
        cp_lines_add_default(out, "condop");
    else if( ops->cond_ops_count == 0 )
        cp_lines_add_empty(out, "condop");
    else
    {
        for( int i = 0; i < ops->cond_ops_count; i++ )
        {
            const struct RSCache_EntityCondOp* c = &ops->cond_ops[i];
            snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d,%s", c->index, c->varp_id, c->varbit_id,
                     c->min_value, c->max_value, c->text ? c->text : "");
            cp_lines_add_str(out, "condop", buf);
        }
    }

    if( !RSCache_PresenceHas(has, RSCACHE_NPC_FIELD_COND_SUB_OPS) )
        cp_lines_add_default(out, "condsubop");
    else if( ops->cond_sub_ops_count == 0 )
        cp_lines_add_empty(out, "condsubop");
    else
    {
        for( int i = 0; i < ops->cond_sub_ops_count; i++ )
        {
            const struct RSCache_EntityCondSubOp* c = &ops->cond_sub_ops[i];
            snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d,%d,%s", c->index, c->sub_id, c->varp_id,
                     c->varbit_id, c->min_value, c->max_value, c->text ? c->text : "");
            cp_lines_add_str(out, "condsubop", buf);
        }
    }
}

static void
emit_npc(
    struct CP_Ctx* ctx,
    const struct RSCache_Dat2ConfigNpc* entry,
    struct CP_Lines* out)
{
    const struct RSCache_Presence* has = &entry->present;

#define NPC_HAS(field) RSCache_PresenceHas(has, RSCACHE_NPC_FIELD_##field)

    if( NPC_HAS(NAME) )
        cp_lines_add_str(out, "name", entry->name);
    else
        cp_lines_add_default(out, "name");
    /* Opcode 3, the same key a loc states its examine under. No pristine npc
     * record carries one — this is here so the content tree can author it. */
    if( NPC_HAS(DESC) )
        cp_lines_add_str(out, "desc", entry->desc);
    else
        cp_lines_add_default(out, "desc");

    emit_id_list(out, "model", NPC_HAS(MODELS), entry->models, entry->models_count);
    emit_id_list(out, "head", NPC_HAS(CHATHEADS), entry->chathead_models,
                 entry->chathead_models_count);

    emit_int(out, "size", NPC_HAS(SIZE), entry->size);

    if( npc_applies_not_b669(ctx) )
    {
        bool walk_turns = NPC_HAS(WALK_TURN_ANIMS);
        bool crawl_turns = NPC_HAS(CRAWL_TURN_ANIMS);

        emit_seq(ctx, out, "readyanim", NPC_HAS(READY_ANIM), entry->standing_animation);
        /* 14 and 17 both state the walk animation; 17 adds the turns. */
        emit_seq(ctx, out, "walkanim", NPC_HAS(WALK_ANIM), entry->walking_animation);
        emit_seq(ctx, out, "walkanim_b", walk_turns, entry->rotate180_animation);
        emit_seq(ctx, out, "walkanim_l", walk_turns, entry->rotate_left_animation);
        emit_seq(ctx, out, "walkanim_r", walk_turns, entry->rotate_right_animation);
        emit_seq(ctx, out, "idleleftanim", NPC_HAS(IDLE_LEFT_ANIM),
                 entry->idle_rotate_left_animation);
        emit_seq(ctx, out, "idlerightanim", NPC_HAS(IDLE_RIGHT_ANIM),
                 entry->idle_rotate_right_animation);
        if( npc_applies_oldschool_ops(ctx) )
        {
            bool run_turns = NPC_HAS(RUN_TURN_ANIMS);
            emit_seq(ctx, out, "runanim", NPC_HAS(RUN_ANIM), entry->run_animation);
            emit_seq(ctx, out, "runanim_b", run_turns, entry->run_rotate180_animation);
            emit_seq(ctx, out, "runanim_l", run_turns, entry->run_rotate_left_animation);
            emit_seq(ctx, out, "runanim_r", run_turns, entry->run_rotate_right_animation);
        }
        emit_seq(ctx, out, "crawlanim", NPC_HAS(CRAWL_ANIM), entry->crawl_animation);
        emit_seq(ctx, out, "crawlanim_b", crawl_turns, entry->crawl_rotate180_animation);
        emit_seq(ctx, out, "crawlanim_l", crawl_turns, entry->crawl_rotate_left_animation);
        emit_seq(ctx, out, "crawlanim_r", crawl_turns, entry->crawl_rotate_right_animation);
    }

    for( int i = 0; i < 5; i++ )
    {
        char key[8];
        snprintf(key, sizeof(key), "op%d", i + 1);
        /* A stated "Hidden" decodes to an empty slot; it is still stated. */
        if( RSCache_PresenceHas(has, RSCACHE_NPC_FIELD_OP1 + i) )
            cp_lines_add_str(out, key, entry->actions[i] ? entry->actions[i] : "Hidden");
        else
            cp_lines_add_default(out, key);
    }
    if( npc_applies_rev237_ops(ctx) )
        emit_entity_op_lists(out, has, &entry->entity_ops);

    emit_pairs(out, "recol", NPC_HAS(RECOLOR), entry->recolor_to_find, entry->recolor_to_replace,
               entry->recolor_count);
    emit_pairs(out, "retex", NPC_HAS(RETEXTURE), entry->retexture_to_find,
               entry->retexture_to_replace, entry->retexture_count);

    emit_flag(out, "minimap", NPC_HAS(MINIMAP_HIDDEN), "no");
    emit_int(out, "vislevel", NPC_HAS(COMBAT_LEVEL), entry->combat_level);
    emit_int(out, "resizeh", NPC_HAS(WIDTH_SCALE), entry->width_scale);
    emit_int(out, "resizev", NPC_HAS(HEIGHT_SCALE), entry->height_scale);
    emit_flag(out, "alwaysontop", NPC_HAS(RENDER_PRIORITY), "yes");
    if( npc_applies_rev233(ctx) )
        emit_flag(out, "renderpriority", NPC_HAS(RENDER_PRIORITY_HIGH), "2");
    if( npc_applies_follower(ctx) )
        emit_flag(out, "follower", NPC_HAS(FOLLOWER), "yes");
    emit_int(out, "ambient", NPC_HAS(AMBIENT), entry->ambient);
    emit_int(out, "contrast", NPC_HAS(CONTRAST), entry->contrast);

    /* A head icon is an (archive, sprite index) pair per set bit of a bitfield, so
     * both halves go on one line — splitting them into two indexed keys would let a
     * hand-edit produce a pair that names a sprite in the wrong archive. */
    if( !NPC_HAS(HEAD_ICONS) )
        cp_lines_add_default(out, "headicon");
    else if( entry->head_icon_count == 0 )
        cp_lines_add_empty(out, "headicon");
    else
    {
        for( int i = 0; i < entry->head_icon_count; i++ )
            cp_lines_addf(out, "headicon%d=%d,%d", i + 1, entry->head_icon_archive_ids[i],
                          entry->head_icon_sprite_index[i]);
    }

    emit_int(out, "turnspeed", NPC_HAS(ROTATION_SPEED), entry->rotation_speed);

    if( !NPC_HAS(MULTI) )
    {
        cp_lines_add_default(out, "multivarbit");
        cp_lines_add_default(out, "multivarp");
        cp_lines_add_default(out, "multinpc");
    }
    else
    {
        emit_var_slot(ctx, out, "multivarbit", CP_TYPE_VARBIT, entry->varbit_id);
        emit_var_slot(ctx, out, "multivarp", CP_TYPE_VARP, entry->varp_index);
        if( entry->configs_count == 0 )
            cp_lines_add_empty(out, "multinpc");
        for( int i = 0; i < entry->configs_count; i++ )
        {
            /* A transform target of -1 is "no npc in this slot", and the slot still
             * counts — the varbit's value indexes this list positionally. The last
             * slot is 118's trailing default (-1 under 106). */
            if( entry->configs[i] < 0 )
                cp_lines_addf(out, "multinpc%d=-1", i + 1);
            else
                cp_lines_addf(out, "multinpc%d=%s", i + 1,
                              cp_name_ensure(ctx, CP_TYPE_NPC, entry->configs[i]));
        }
    }

    emit_flag(out, "interactable", NPC_HAS(NOT_INTERACTABLE), "no");
    emit_flag(out, "rotationflag", NPC_HAS(NO_ROTATION_FLAG), "no");
    if( npc_applies_oldschool_ops(ctx) )
    {
        emit_flag(out, "pet", NPC_HAS(PET), "yes");
        emit_flag(out, "lowpriorityops", NPC_HAS(LOW_PRIORITY_OPS), "yes");
    }
    emit_int(out, "height", NPC_HAS(HEIGHT), entry->height);
    if( npc_applies_not_b669(ctx) )
        emit_int(out, "category", NPC_HAS(CATEGORY), entry->category);

    emit_int(out, "soundidle", NPC_HAS(MOVEMENT_SOUNDS), entry->sound_idle);
    emit_int(out, "soundcrawl", NPC_HAS(MOVEMENT_SOUNDS), entry->sound_crawl);
    emit_int(out, "soundwalk", NPC_HAS(MOVEMENT_SOUNDS), entry->sound_walk);
    emit_int(out, "soundrun", NPC_HAS(MOVEMENT_SOUNDS), entry->sound_run);
    emit_int(out, "soundradius", NPC_HAS(MOVEMENT_SOUNDS), entry->sound_radius);
    if( npc_applies_not_b669(ctx) )
    {
        emit_int(out, "soundvolume", NPC_HAS(SOUND_VOLUME), entry->ambient_sound_volume);
        for( int i = 0; i < 6; i++ )
        {
            char key[8];
            snprintf(key, sizeof(key), "stat%d", i + 1);
            emit_int(out, key, RSCache_PresenceHas(has, RSCACHE_NPC_FIELD_STAT1 + i),
                     entry->stats[i]);
        }
    }
    emit_int(out, "bastype", NPC_HAS(BAS_TYPE), entry->bas_type_id);
    if( npc_applies_rev231(ctx) )
        emit_int(out, "footprintsize", NPC_HAS(FOOTPRINT_SIZE), entry->footprint_size);
    if( npc_applies_rev234(ctx) )
        emit_flag(out, "opcode129", NPC_HAS(UNKNOWN129), "yes");
    if( npc_applies_rev235(ctx) )
    {
        emit_flag(out, "hideforoverlap", NPC_HAS(HIDE_FOR_OVERLAP), "yes");
        emit_int(out, "overlaptint", NPC_HAS(OVERLAP_TINT), entry->overlap_tint_hsl);
    }
    if( npc_applies_rev236(ctx) )
    {
        emit_flag(out, "idleanimrestart", NPC_HAS(IDLE_ANIM_RESTART), "yes");
        emit_flag(out, "zbuf", NPC_HAS(ZBUF_OFF), "no");
    }

    if( !NPC_HAS(PARAMS) )
        cp_lines_add_default(out, "param");
    else if( entry->params.count == 0 )
        cp_lines_add_empty(out, "param");
    else
        cp_emit_params(ctx, out, &entry->params);

#undef NPC_HAS
}

int
cp_unpack_npc(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigNpc* entry =
        RSCache_Dat2ConfigNpcNewDecodeProfile(&ctx->profile, (char*)record, record_size);
    if( entry->_consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "npc %d: consumed %d of %d bytes", id,
                entry->_consumed, record_size);

    emit_npc(ctx, entry, out);

    RSCache_Dat2ConfigNpcFree(entry);
    return 1;
}

/* ---- pack ---------------------------------------------------------------- */

/*
 * Does `key` belong to the numbered family `stem`? Yes for `stem<n>` (index n-1
 * in `*index`) and for the bare stem carrying the `empty` marker (`*index` -1).
 * The `default` marker never gets here: the key loop skips it first.
 */
static int
family_key(
    const char* key,
    const char* value,
    const char* stem,
    int* index)
{
    if( strcmp(key, stem) == 0 && cp_value_is_empty(value) )
    {
        *index = -1;
        return 1;
    }
    *index = cp_indexed_key(key, stem);
    return *index >= 0;
}

/* A payload-free opcode's key accepts only the one spelling the opcode means:
 * the stream has no way to say `minimap=yes` other than not stating it. */
static int
parse_spelling(
    const char* value,
    const char* spelling)
{
    return strcmp(value, spelling) == 0;
}

uint32_t
cp_pack_npc(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    /* The empty-record decode gives the client defaults and no presence: a field
     * is written only if a line below states it. */
    struct RSCache_Dat2ConfigNpc* entry = RSCache_Dat2ConfigNpcNewDecodeProfile(
        &ctx->profile, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    const struct CP_Type* type = cp_type(CP_TYPE_NPC);

    struct CP_IntList models = { 0 }, heads = { 0 }, transforms = { 0 };
    struct CP_IntList recol_s = { 0 }, recol_d = { 0 };
    struct CP_IntList retex_s = { 0 }, retex_d = { 0 };
    struct CP_IntList icon_archive = { 0 }, icon_sprite = { 0 };
    uint32_t written = 0;
    /* A turn rides on opcode 17 (115, 117) beside its animation, so a stated
     * turn needs its animation stated too; checked after the loop. */
    bool walk = false, walk_turns = false;
    bool run = false, run_turns = false;
    bool crawl = false, crawl_turns = false;

#define NPC_SET(field) RSCache_PresenceSet(&entry->present, RSCACHE_NPC_FIELD_##field)

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
        /* Not an npc key, or one this profile's codec cannot carry. */
        if( !cp_key_spec(ctx, type, key) )
        {
            cp_warn(ctx, &ctx->warn_unknown_key, "npc [%s]: unknown key %s",
                    config->debugname, key);
            continue;
        }

        if( strcmp(key, "name") == 0 )
        {
            char buf[1024];
            NPC_SET(NAME);
            cp_unescape(value, buf, sizeof(buf));
            free(entry->name);
            entry->name = strdup(buf);
            assert(entry->name);
        }
        else if( strcmp(key, "desc") == 0 )
        {
            char buf[4096];
            NPC_SET(DESC);
            cp_unescape(value, buf, sizeof(buf));
            free(entry->desc);
            entry->desc = strdup(buf);
            assert(entry->desc);
        }
        else if( family_key(key, value, "model", &index) )
        {
            NPC_SET(MODELS);
            if( index >= 0 )
            {
                ok = cp_parse_int(value, &tmp);
                cp_intlist_set(&models, index, tmp);
            }
        }
        else if( family_key(key, value, "head", &index) )
        {
            NPC_SET(CHATHEADS);
            if( index >= 0 )
            {
                ok = cp_parse_int(value, &tmp);
                cp_intlist_set(&heads, index, tmp);
            }
        }
        else if( family_key(key, value, "headicon", &index) )
        {
            NPC_SET(HEAD_ICONS);
            if( index >= 0 )
            {
                char scratch[64];
                char* fields[2];
                if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 2) != 2 )
                {
                    ok = 0;
                }
                else
                {
                    int archive = 0, sprite = 0;
                    ok = cp_parse_int(fields[0], &archive) && cp_parse_int(fields[1], &sprite);
                    cp_intlist_set(&icon_archive, index, archive);
                    cp_intlist_set(&icon_sprite, index, sprite);
                }
            }
        }
        else if( family_key(key, value, "multinpc", &index) )
        {
            NPC_SET(MULTI);
            if( index < 0 )
            {
                /* `multinpc=empty` */
            }
            else if( strcmp(value, "-1") == 0 )
                cp_intlist_set(&transforms, index, -1);
            else
            {
                ok = cp_resolve_ref(ctx, CP_TYPE_NPC, value, &tmp);
                cp_intlist_set(&transforms, index, tmp);
            }
        }
        else if( strncmp(key, "recol", 5) == 0 )
        {
            /* `recol=empty`, or a pair half collected by cp_collect_pairs below. */
            NPC_SET(RECOLOR);
        }
        else if( strncmp(key, "retex", 5) == 0 )
        {
            NPC_SET(RETEXTURE);
        }
        else if( (index = cp_indexed_key(key, "op")) >= 0 && index < 5 )
        {
            RSCache_PresenceSet(&entry->present, RSCACHE_NPC_FIELD_OP1 + index);
            /* Stores "Hidden" as text, which the encoder writes back as itself. */
            ok = cp_parse_entity_op(entry->actions, 5, NULL, key, value);
        }
        else if( strcmp(key, "subop") == 0 )
        {
            NPC_SET(SUB_OPS);
            if( !cp_value_is_empty(value) )
                ok = cp_parse_entity_op(entry->actions, 5, &entry->entity_ops, key, value);
        }
        else if( strcmp(key, "condop") == 0 )
        {
            NPC_SET(COND_OPS);
            if( !cp_value_is_empty(value) )
                ok = cp_parse_entity_op(entry->actions, 5, &entry->entity_ops, key, value);
        }
        else if( strcmp(key, "condsubop") == 0 )
        {
            NPC_SET(COND_SUB_OPS);
            if( !cp_value_is_empty(value) )
                ok = cp_parse_entity_op(entry->actions, 5, &entry->entity_ops, key, value);
        }
        else if( (index = cp_indexed_key(key, "stat")) >= 0 && index < 6 )
        {
            RSCache_PresenceSet(&entry->present, RSCACHE_NPC_FIELD_STAT1 + index);
            ok = cp_parse_int(value, &entry->stats[index]);
        }
        else if( strcmp(key, "param") == 0 )
        {
            NPC_SET(PARAMS);
            if( !cp_value_is_empty(value) )
                ok = cp_parse_param(ctx, &entry->params, value);
        }
        else if( strcmp(key, "size") == 0 )
        {
            NPC_SET(SIZE);
            ok = cp_parse_int(value, &entry->size);
        }
        else if( strcmp(key, "readyanim") == 0 )
        {
            NPC_SET(READY_ANIM);
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->standing_animation);
        }
        else if( strcmp(key, "walkanim") == 0 )
        {
            walk = true;
            NPC_SET(WALK_ANIM);
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->walking_animation);
        }
        else if( strcmp(key, "walkanim_b") == 0 )
        {
            walk_turns = true;
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->rotate180_animation);
        }
        else if( strcmp(key, "walkanim_l") == 0 )
        {
            walk_turns = true;
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->rotate_left_animation);
        }
        else if( strcmp(key, "walkanim_r") == 0 )
        {
            walk_turns = true;
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->rotate_right_animation);
        }
        else if( strcmp(key, "idleleftanim") == 0 )
        {
            NPC_SET(IDLE_LEFT_ANIM);
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->idle_rotate_left_animation);
        }
        else if( strcmp(key, "idlerightanim") == 0 )
        {
            NPC_SET(IDLE_RIGHT_ANIM);
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->idle_rotate_right_animation);
        }
        else if( strcmp(key, "runanim") == 0 )
        {
            run = true;
            NPC_SET(RUN_ANIM);
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->run_animation);
        }
        else if( strcmp(key, "runanim_b") == 0 )
        {
            run_turns = true;
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->run_rotate180_animation);
        }
        else if( strcmp(key, "runanim_l") == 0 )
        {
            run_turns = true;
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->run_rotate_left_animation);
        }
        else if( strcmp(key, "runanim_r") == 0 )
        {
            run_turns = true;
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->run_rotate_right_animation);
        }
        else if( strcmp(key, "crawlanim") == 0 )
        {
            crawl = true;
            NPC_SET(CRAWL_ANIM);
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->crawl_animation);
        }
        else if( strcmp(key, "crawlanim_b") == 0 )
        {
            crawl_turns = true;
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->crawl_rotate180_animation);
        }
        else if( strcmp(key, "crawlanim_l") == 0 )
        {
            crawl_turns = true;
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->crawl_rotate_left_animation);
        }
        else if( strcmp(key, "crawlanim_r") == 0 )
        {
            crawl_turns = true;
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->crawl_rotate_right_animation);
        }
        else if( strcmp(key, "minimap") == 0 )
        {
            NPC_SET(MINIMAP_HIDDEN);
            ok = parse_spelling(value, "no");
            entry->is_minimap_visible = false;
        }
        else if( strcmp(key, "vislevel") == 0 )
        {
            NPC_SET(COMBAT_LEVEL);
            ok = cp_parse_int(value, &entry->combat_level);
        }
        else if( strcmp(key, "resizeh") == 0 )
        {
            NPC_SET(WIDTH_SCALE);
            ok = cp_parse_int(value, &entry->width_scale);
        }
        else if( strcmp(key, "resizev") == 0 )
        {
            NPC_SET(HEIGHT_SCALE);
            ok = cp_parse_int(value, &entry->height_scale);
        }
        else if( strcmp(key, "alwaysontop") == 0 )
        {
            NPC_SET(RENDER_PRIORITY);
            ok = parse_spelling(value, "yes");
            entry->has_render_priority = true;
            entry->render_priority = 1;
        }
        else if( strcmp(key, "renderpriority") == 0 )
        {
            NPC_SET(RENDER_PRIORITY_HIGH);
            ok = parse_spelling(value, "2");
            entry->has_render_priority = true;
            entry->render_priority = 2;
        }
        else if( strcmp(key, "follower") == 0 )
        {
            NPC_SET(FOLLOWER);
            ok = parse_spelling(value, "yes");
            entry->is_pet = true;
            entry->low_priority_follower_ops = true;
        }
        else if( strcmp(key, "ambient") == 0 )
        {
            NPC_SET(AMBIENT);
            ok = cp_parse_int(value, &entry->ambient);
        }
        else if( strcmp(key, "contrast") == 0 )
        {
            NPC_SET(CONTRAST);
            ok = cp_parse_int(value, &entry->contrast);
        }
        else if( strcmp(key, "turnspeed") == 0 )
        {
            NPC_SET(ROTATION_SPEED);
            ok = cp_parse_int(value, &entry->rotation_speed);
        }
        else if( strcmp(key, "multivarbit") == 0 )
        {
            NPC_SET(MULTI);
            ok = cp_resolve_ref(ctx, CP_TYPE_VARBIT, value, &entry->varbit_id);
        }
        else if( strcmp(key, "multivarp") == 0 )
        {
            NPC_SET(MULTI);
            ok = cp_resolve_ref(ctx, CP_TYPE_VARP, value, &entry->varp_index);
        }
        else if( strcmp(key, "interactable") == 0 )
        {
            NPC_SET(NOT_INTERACTABLE);
            ok = parse_spelling(value, "no");
            entry->is_interactable = false;
        }
        else if( strcmp(key, "rotationflag") == 0 )
        {
            NPC_SET(NO_ROTATION_FLAG);
            ok = parse_spelling(value, "no");
            entry->rotation_flag = false;
        }
        else if( strcmp(key, "pet") == 0 )
        {
            NPC_SET(PET);
            ok = parse_spelling(value, "yes");
            entry->is_pet = true;
        }
        else if( strcmp(key, "lowpriorityops") == 0 )
        {
            NPC_SET(LOW_PRIORITY_OPS);
            ok = parse_spelling(value, "yes");
            entry->low_priority_follower_ops = true;
        }
        else if( strcmp(key, "height") == 0 )
        {
            NPC_SET(HEIGHT);
            ok = cp_parse_int(value, &entry->height);
        }
        else if( strcmp(key, "category") == 0 )
        {
            NPC_SET(CATEGORY);
            ok = cp_resolve_category(ctx, value, &entry->category);
        }
        else if( strcmp(key, "soundidle") == 0 )
        {
            NPC_SET(MOVEMENT_SOUNDS);
            ok = cp_parse_int(value, &entry->sound_idle);
        }
        else if( strcmp(key, "soundcrawl") == 0 )
        {
            NPC_SET(MOVEMENT_SOUNDS);
            ok = cp_parse_int(value, &entry->sound_crawl);
        }
        else if( strcmp(key, "soundwalk") == 0 )
        {
            NPC_SET(MOVEMENT_SOUNDS);
            ok = cp_parse_int(value, &entry->sound_walk);
        }
        else if( strcmp(key, "soundrun") == 0 )
        {
            NPC_SET(MOVEMENT_SOUNDS);
            ok = cp_parse_int(value, &entry->sound_run);
        }
        else if( strcmp(key, "soundradius") == 0 )
        {
            NPC_SET(MOVEMENT_SOUNDS);
            ok = cp_parse_int(value, &entry->sound_radius);
        }
        else if( strcmp(key, "soundvolume") == 0 )
        {
            NPC_SET(SOUND_VOLUME);
            ok = cp_parse_int(value, &entry->ambient_sound_volume);
        }
        else if( strcmp(key, "bastype") == 0 )
        {
            NPC_SET(BAS_TYPE);
            ok = cp_parse_int(value, &entry->bas_type_id);
        }
        else if( strcmp(key, "footprintsize") == 0 )
        {
            NPC_SET(FOOTPRINT_SIZE);
            ok = cp_parse_int(value, &entry->footprint_size);
        }
        else if( strcmp(key, "opcode129") == 0 )
        {
            NPC_SET(UNKNOWN129);
            ok = parse_spelling(value, "yes");
            entry->unknown1 = true;
        }
        else if( strcmp(key, "hideforoverlap") == 0 )
        {
            NPC_SET(HIDE_FOR_OVERLAP);
            ok = parse_spelling(value, "yes");
            entry->can_hide_for_overlap = true;
        }
        else if( strcmp(key, "overlaptint") == 0 )
        {
            NPC_SET(OVERLAP_TINT);
            ok = cp_parse_int(value, &entry->overlap_tint_hsl);
        }
        else if( strcmp(key, "idleanimrestart") == 0 )
        {
            NPC_SET(IDLE_ANIM_RESTART);
            ok = parse_spelling(value, "yes");
            entry->idle_anim_restart = true;
        }
        else if( strcmp(key, "zbuf") == 0 )
        {
            NPC_SET(ZBUF_OFF);
            ok = parse_spelling(value, "no");
            entry->zbuf = false;
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "npc [%s]: unknown key %s",
                    config->debugname, key);

        if( !ok )
        {
            fprintf(stderr, "cachepack: npc [%s]: bad value for %s\n", config->debugname, key);
            goto done;
        }
    }

    if( walk_turns )
        NPC_SET(WALK_TURN_ANIMS);
    if( run_turns )
        NPC_SET(RUN_TURN_ANIMS);
    if( crawl_turns )
        NPC_SET(CRAWL_TURN_ANIMS);

#undef NPC_SET

    if( (walk_turns && !walk) || (run_turns && !run) || (crawl_turns && !crawl) )
    {
        fprintf(stderr,
                "cachepack: npc [%s]: a stated turn animation (`*_b/_l/_r`) is written in the "
                "same opcode as its animation, so `walkanim`/`runanim`/`crawlanim` must be "
                "stated too\n",
                config->debugname);
        goto done;
    }

    /* 106/118 always carry the list plus a trailing slot, so a stated block has
     * at least two entries. */
    if( RSCache_PresenceHas(&entry->present, RSCACHE_NPC_FIELD_MULTI) && transforms.count < 2 )
    {
        fprintf(stderr,
                "cachepack: npc [%s]: multinpc needs at least two entries (the list and its "
                "trailing default)\n",
                config->debugname);
        goto done;
    }

    if( !cp_collect_pairs(config, "recol", &recol_s, &recol_d) ||
        !cp_collect_pairs(config, "retex", &retex_s, &retex_d) )
    {
        fprintf(stderr, "cachepack: npc [%s]: mismatched recolour pairs\n", config->debugname);
        goto done;
    }

    entry->models = models.items;
    entry->models_count = models.count;
    entry->chathead_models = heads.items;
    entry->chathead_models_count = heads.count;
    entry->configs = transforms.items;
    entry->configs_count = transforms.count;
    entry->head_icon_archive_ids = icon_archive.items;
    entry->head_icon_count = icon_archive.count;

    entry->recolor_to_find = recol_s.items;
    entry->recolor_to_replace = recol_d.items;
    entry->recolor_count = recol_s.count;
    entry->retexture_to_find = retex_s.items;
    entry->retexture_to_replace = retex_d.items;
    entry->retexture_count = retex_s.count;

    if( icon_sprite.count > 0 )
    {
        short* sprites = malloc((size_t)icon_sprite.count * sizeof(short));
        assert(sprites);
        for( int i = 0; i < icon_sprite.count; i++ )
            sprites[i] = (short)icon_sprite.items[i];
        entry->head_icon_sprite_index = sprites;
    }

    written = RSCache_Dat2ConfigNpcEncodeProfile(&ctx->profile, entry, out, out_capacity);

done:
    /* The int lists own their arrays; hand ownership back so the free below does
     * not release them twice. `head_icon_sprite_index` is the one array this
     * function allocates itself, and it is freed with the struct. */
    entry->models = NULL;
    entry->chathead_models = NULL;
    entry->configs = NULL;
    entry->head_icon_archive_ids = NULL;
    entry->recolor_to_find = entry->recolor_to_replace = NULL;
    entry->retexture_to_find = entry->retexture_to_replace = NULL;
    RSCache_Dat2ConfigNpcFree(entry);
    cp_intlist_free(&models);
    cp_intlist_free(&heads);
    cp_intlist_free(&transforms);
    cp_intlist_free(&recol_s);
    cp_intlist_free(&recol_d);
    cp_intlist_free(&retex_s);
    cp_intlist_free(&retex_d);
    cp_intlist_free(&icon_archive);
    cp_intlist_free(&icon_sprite);
    return written;
}
