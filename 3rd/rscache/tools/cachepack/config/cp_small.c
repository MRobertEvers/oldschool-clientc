#include "cachepack.h"

/* `cp_asset`, for naming the asset namespace a hitsplat sprite reference missed. */
#include "cp_assets.h"

#include "datatypes/dat2_config_healthbar.h"
#include "datatypes/dat2_config_hitsplat.h"
#include "datatypes/dat2_config_inv.h"
#include "datatypes/dat2_config_mapelement.h"
#include "datatypes/dat2_config_param.h"
#include "datatypes/dat2_config_struct.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * The small OldSchool-era types, none of which rev 254 has: inv, param, struct,
 * healthbar, hitsplat and mapelement.
 *
 * Every key of a type is written for every record, straight from the decoder's
 * presence bits: the value when the record states the field, `key=default` when
 * it does not, `key=empty` for a param map stated with no entries. The packer
 * reads the same three states back into the bits, so what a record stated --
 * including a value equal to the client default, like the explicit
 * `default_int=0` every rev-239 int param carries -- is what gets written. The
 * key tables at the bottom of this file are the contract (see cp_keys.c).
 *
 * mapelement used to be the lossy one (its decoder kept four of its two dozen
 * fields); it now keeps every opcode and has a key for each.
 */

/* A flag key: the opcode carries no payload, so its presence is the value and
 * `yes` is the only thing to say. `no` would be a third state the stream cannot
 * express. */
static int
small_parse_flag(const char* value)
{
    return strcmp(value, "yes") == 0;
}

/* Opcode 249's map under the one `param` key: absent, stated empty, or entries. */
static void
small_emit_param_map(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    int present,
    const struct RSCache_Params* params)
{
    if( !present )
        cp_lines_add_default(out, "param");
    else if( params->count == 0 )
        cp_lines_add_empty(out, "param");
    else
        cp_emit_params(ctx, out, params);
}

/* One `param=` line into a map whose presence the caller has already set. */
static int
small_parse_param_line(
    struct CP_Ctx* ctx,
    struct RSCache_Params* params,
    const char* value)
{
    if( cp_value_is_empty(value) )
        return 1;
    return cp_parse_param(ctx, params, value);
}

/* ---- inv ---------------------------------------------------------------- */

int
cp_unpack_inv(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigInv entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigInvDecodeInplace(&entry, record, record_size);
    if( entry._consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "inv %d: consumed %d of %d bytes", id,
                entry._consumed, record_size);

    if( RSCache_PresenceHas(&entry.present, RSCACHE_INV_FIELD_SIZE) )
        cp_lines_addf(out, "size=%d", entry.size);
    else
        cp_lines_add_default(out, "size");
    small_emit_param_map(ctx, out, RSCache_PresenceHas(&entry.present, RSCACHE_INV_FIELD_PARAMS),
                         &entry.params);
    RSCache_Dat2ConfigInvFreeInplace(&entry);
    return 1;
}

uint32_t
cp_pack_inv(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigInv entry;
    memset(&entry, 0, sizeof(entry));
    /* The empty-record decode: client defaults, nothing stated. */
    RSCache_Dat2ConfigInvDecodeInplace(&entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    uint32_t written = 0;
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;

        if( cp_value_is_default(value) )
            continue;
        if( strcmp(key, "size") == 0 )
        {
            RSCache_PresenceSet(&entry.present, RSCACHE_INV_FIELD_SIZE);
            ok = cp_parse_int(value, &entry.size);
        }
        else if( strcmp(key, "param") == 0 )
        {
            RSCache_PresenceSet(&entry.present, RSCACHE_INV_FIELD_PARAMS);
            ok = small_parse_param_line(ctx, &entry.params, value);
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "inv [%s]: unknown key %s",
                    config->debugname, key);
        if( !ok )
        {
            fprintf(stderr, "cachepack: inv [%s]: bad value for %s\n", config->debugname, key);
            goto done;
        }
    }
    written = RSCache_Dat2ConfigInvEncode(&entry, out, out_capacity);
done:
    RSCache_Dat2ConfigInvFreeInplace(&entry);
    return written;
}

/* ---- param -------------------------------------------------------------- */

/*
 * `typechar` and `typeid` are the record's two statements of its type: opcode 1, the
 * ScriptVarType's character key, and opcode 8, its numeric id. A rev-239 record
 * states both (`typechar=105`, `typeid=0`); older ones only the first. The client
 * reads 8 after 1, so when both are stated the id is the one that counts --
 * which is also what `default=` is read through, below.
 *
 * `type` is the EFFECTIVE type, spelled as its ScriptVarType word (`type=namedobj`,
 * cp_value.c) -- the spelling every authored `.param` uses, so there is one: it
 * is what every reader of `type=` wants (cp_param_types_load, the server's param
 * loader). It is stated when either opcode is. Opcode 1's stored byte is not
 * always that type's character -- a record may state a byte the client then
 * overrides with opcode 8 -- so the byte rides in its own key,
 * `typechar=<decimal byte>`, and that is what the packer writes. A hand-written block with `type=` and no `typechar` line at
 * all gets the byte derived from `type`, as before; so does one whose
 * `typechar` no longer agrees with an opcode-8-less `type` (an overlay that
 * retyped the param).
 */

/* The effective type `default=` is read through: `typeid` when the block states
 * one, else `type`, else 0. */
static char
param_effective_type(const struct CP_Config* config)
{
    const char* typeid_text = cp_config_get(config, "typeid");
    const char* type_text = cp_config_get(config, "type");
    int type_id = 0;

    if( typeid_text && !cp_value_is_default(typeid_text) && cp_parse_int(typeid_text, &type_id) )
        return RSCache_Dat2ConfigParamCharForTypeId(type_id);
    if( type_text && !cp_value_is_default(type_text) )
    {
        char buf[64];
        cp_unescape(type_text, buf, sizeof(buf));
        return (char)cp_value_char_read(buf);
    }
    return 0;
}

int
cp_unpack_param(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigParam entry;
    const struct RSCache_Presence* has = &entry.present;

    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigParamDecodeInplace(&entry, record, record_size);
    if( entry._consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "param %d: consumed %d of %d bytes", id,
                entry._consumed, record_size);

    if( RSCache_PresenceHas(has, RSCACHE_PARAM_FIELD_TYPE) ||
        RSCache_PresenceHas(has, RSCACHE_PARAM_FIELD_TYPE_ID) )
    {
        char word[16];

        cp_lines_addf(out, "type=%s", cp_value_char_text((unsigned char)entry.type, word,
                                                         sizeof(word)));
    }
    else
        cp_lines_add_default(out, "type");
    if( RSCache_PresenceHas(has, RSCACHE_PARAM_FIELD_TYPE) )
        cp_lines_addf(out, "typechar=%d", entry.type_key);
    else
        cp_lines_add_default(out, "typechar");
    if( RSCache_PresenceHas(has, RSCACHE_PARAM_FIELD_TYPE_ID) )
        cp_lines_addf(out, "typeid=%d", entry.type_id);
    else
        cp_lines_add_default(out, "typeid");

    /*
     * One `default=`, spelled by the type (LostCity's grammar): a string param's
     * is its string (opcode 5), every other type's its int (opcode 2) through the
     * one table -- `default=bones`, `default=null`, `default=no`. The opcode is
     * the type's, so a record stating the other one, or a long (opcode 7, which
     * no type this table lists reads), has no text; none in any cache here
     * does, and one is refused rather than written in a second spelling.
     */
    {
        const struct CP_ValueType* type = cp_value_type_of_char((unsigned char)entry.type);
        int is_string = type && type->spell == CP_VALUE_STRING;
        int has_int = RSCache_PresenceHas(has, RSCACHE_PARAM_FIELD_DEFAULT_INT);
        int has_string = RSCache_PresenceHas(has, RSCACHE_PARAM_FIELD_DEFAULT_STRING);

        if( RSCache_PresenceHas(has, RSCACHE_PARAM_FIELD_DEFAULT_LONG) ||
            (has_int && is_string) || (has_string && !is_string) )
        {
            fprintf(stderr,
                    "cachepack: param %d: a %s default on a `%c` param has no text form\n", id,
                    RSCache_PresenceHas(has, RSCACHE_PARAM_FIELD_DEFAULT_LONG) ? "long"
                    : has_int                                                 ? "int"
                                                                              : "string",
                    entry.type ? entry.type : '?');
            RSCache_Dat2ConfigParamFreeInplace(&entry);
            return 0;
        }
        if( has_string )
        {
            char buf[8192];

            cp_value_string_escape(entry.default_string, 1, buf, sizeof(buf));
            cp_lines_addf(out, "default=%s", buf);
        }
        else if( has_int )
        {
            char number[64];

            cp_lines_addf(out, "default=%s",
                          cp_value_int_text(ctx, type, entry.default_int, CP_BOOL_YES_NO, number,
                                            sizeof(number)));
        }
        else
            cp_lines_add_default(out, "default");
    }
    /* Defaults to 1; opcode 4 is what clears it, so `no` is the only value the
     * record can state. */
    if( RSCache_PresenceHas(has, RSCACHE_PARAM_FIELD_AUTO_DISABLE) )
        cp_lines_addf(out, "autodisable=no");
    else
        cp_lines_add_default(out, "autodisable");

    RSCache_Dat2ConfigParamFreeInplace(&entry);
    return 1;
}

uint32_t
cp_pack_param(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigParam entry;
    memset(&entry, 0, sizeof(entry));
    /* The empty-record decode: client defaults, nothing stated. */
    RSCache_Dat2ConfigParamDecodeInplace(&entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    /*
     * The type first, in its own pass.
     *
     * `default` is read *through* it — `default=bones` is obj 526 only because the
     * type is `namedobj` — so a file that happens to write `default=` above
     * `type=` would otherwise resolve the name against a type that was still zero.
     * The machine export writes them in order and a person need not.
     */
    entry.type = param_effective_type(config);
    /* NULL: the block has no `typechar` line at all (hand-written), so `type=`
     * states opcode 1 itself. */
    const char* typechar_text = cp_config_get(config, "typechar");
    const char* typeid_text = cp_config_get(config, "typeid");
    char type_text_char = 0;

    /*
     * `type=` states opcode 1 itself when no other line states the type's bytes:
     * no `typechar` and no `typeid`, or (a full-key authored block) both
     * `=default`. Testing only for a `typechar` LINE made every full-key
     * `.param` -- `type=synth` above `typechar=default` -- encode with no type
     * at all.
     */
    if( typechar_text && cp_value_is_default(typechar_text) &&
        (!typeid_text || cp_value_is_default(typeid_text)) )
        typechar_text = NULL;

#define PARAM_SET(field) RSCache_PresenceSet(&entry.present, RSCACHE_PARAM_FIELD_##field)

    uint32_t written = 0;
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;

        if( cp_value_is_default(value) )
            continue;
        if( strcmp(key, "type") == 0 )
        {
            /* The type's word (cp_value.c). An unknown word is refused rather
             * than defaulted: a param whose type is silently wrong reads its
             * default back as a number that means something else. The
             * effective type was read in the pre-pass; here it only decides
             * opcode 1's byte when no `typechar` line exists to state it. */
            char buf[64];
            char code;

            cp_unescape(value, buf, sizeof(buf));
            code = (char)cp_value_char_read(buf);
            ok = code != 0;
            if( ok && !typechar_text )
            {
                PARAM_SET(TYPE);
                entry.type_key = (unsigned char)code;
            }
            else if( ok )
                type_text_char = code;
        }
        else if( strcmp(key, "typechar") == 0 )
        {
            PARAM_SET(TYPE);
            ok = cp_parse_int(value, &entry.type_key) && entry.type_key > 0 &&
                 entry.type_key <= 255;
        }
        else if( strcmp(key, "typeid") == 0 )
        {
            PARAM_SET(TYPE_ID);
            ok = cp_parse_int(value, &entry.type_id) && entry.type_id >= 0 &&
                 entry.type_id <= 255;
        }
        else if( strcmp(key, "default") == 0 )
        {
            /*
             * The authored grammar (LostCity's lookupParamValue), which keys the
             * whole reading on the declared type -- which is why the type was
             * read in its own pass. `[death_drop] type=namedobj default=bones` is
             * obj 526, and only the type says so. A string param's default is
             * its string (opcode 5); every other type's is its int (opcode 2).
             */
            const struct CP_ValueType* type = cp_value_type_of_char((unsigned char)entry.type);

            if( type && type->spell == CP_VALUE_STRING )
            {
                char buf[4096] = { 0 };

                PARAM_SET(DEFAULT_STRING);
                ok = cp_value_string_read(ctx, value, buf, sizeof(buf));
                free(entry.default_string);
                entry.default_string = strdup(buf);
                assert(entry.default_string);
            }
            else
            {
                char buf[4096];

                PARAM_SET(DEFAULT_INT);
                cp_unescape(value, buf, sizeof(buf));
                ok = cp_value_int_read(ctx, type, buf, &entry.default_int);
            }
        }
        else if( strcmp(key, "autodisable") == 0 )
        {
            /*
             * Opcode 4 clears the flag and has no operand, so `no` is the only
             * thing a record can state. `yes` is LostCity's authored spelling
             * of the client default (43 server overlays say it, over cache
             * params that carry no opcode 4): it states nothing, exactly like
             * `autodisable=default`, and a rank-1 `yes` over a rank-0 `no`
             * therefore removes the opcode -- which is what it means.
             */
            bool flag = true;
            ok = cp_parse_bool(value, &flag);
            if( ok && flag )
            {
                RSCache_PresenceClear(&entry.present, RSCACHE_PARAM_FIELD_AUTO_DISABLE);
                entry.auto_disable = 1;
            }
            else if( ok )
            {
                PARAM_SET(AUTO_DISABLE);
                entry.auto_disable = 0;
            }
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "param [%s]: unknown key %s",
                    config->debugname, key);
        if( !ok )
        {
            fprintf(stderr, "cachepack: param [%s]: bad value for %s\n", config->debugname, key);
            goto done;
        }
    }

#undef PARAM_SET

    /*
     * `typechar` is opcode 1's stored byte, kept beside `type` only because the
     * two can differ (0xD0 vs 'i' on rev 239's dbrow params, where opcode 8
     * decides). With no opcode 8 they are the same statement, and if they
     * disagree `type` was edited over a cache record -- the edit wins, or the
     * retype would be silently dropped.
     */
    if( type_text_char && RSCache_PresenceHas(&entry.present, RSCACHE_PARAM_FIELD_TYPE) &&
        !RSCache_PresenceHas(&entry.present, RSCACHE_PARAM_FIELD_TYPE_ID) &&
        RSCache_Dat2ConfigParamCharForTypeKey(entry.type_key) != type_text_char )
        entry.type_key = (unsigned char)type_text_char;

    written = RSCache_Dat2ConfigParamEncode(&entry, out, out_capacity);
done:
    RSCache_Dat2ConfigParamFreeInplace(&entry);
    return written;
}

/* ---- struct ------------------------------------------------------------- */

int
cp_unpack_struct(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigStruct entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigStructDecodeInplace(&entry, record, record_size);
    small_emit_param_map(ctx, out,
                         RSCache_PresenceHas(&entry.present, RSCACHE_STRUCT_FIELD_PARAMS),
                         &entry.params);
    RSCache_Dat2ConfigStructFreeInplace(&entry);
    return 1;
}

uint32_t
cp_pack_struct(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigStruct entry;
    memset(&entry, 0, sizeof(entry));
    /* The empty-record decode: nothing stated. */
    RSCache_Dat2ConfigStructDecodeInplace(&entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    uint32_t written = 0;
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;

        if( cp_value_is_default(value) )
            continue;
        if( strcmp(key, "param") == 0 )
        {
            RSCache_PresenceSet(&entry.present, RSCACHE_STRUCT_FIELD_PARAMS);
            if( !small_parse_param_line(ctx, &entry.params, value) )
            {
                fprintf(stderr, "cachepack: struct [%s]: bad param\n", config->debugname);
                goto done;
            }
        }
        else
        {
            cp_warn(ctx, &ctx->warn_unknown_key, "struct [%s]: unknown key %s",
                    config->debugname, key);
        }
    }
    written = RSCache_Dat2ConfigStructEncode(&entry, out, out_capacity);
done:
    RSCache_Dat2ConfigStructFreeInplace(&entry);
    return written;
}

/* ---- healthbar ---------------------------------------------------------- */

/* The keys keep their opcode numbers even though the fields no longer do: they
 * are the on-disk format of every unpacked `.healthbar` in the content tree, and
 * renaming them would strand those files. One row per field, in write order. */
static const struct
{
    const char* key;
    int field;
    size_t offset;
} HEALTHBAR_KEYS[] = {
    { "opcode2", RSCACHE_HEALTHBAR_FIELD_DRAW_ORDER,
      offsetof(struct RSCache_Dat2ConfigHealthbar, draw_order) },
    { "opcode3", RSCACHE_HEALTHBAR_FIELD_EVICT_PRIORITY,
      offsetof(struct RSCache_Dat2ConfigHealthbar, evict_priority) },
    { "opcode5", RSCACHE_HEALTHBAR_FIELD_PERSIST_CYCLES,
      offsetof(struct RSCache_Dat2ConfigHealthbar, persist_cycles) },
    { "sprite_a", RSCACHE_HEALTHBAR_FIELD_FRONT_SPRITE,
      offsetof(struct RSCache_Dat2ConfigHealthbar, front_sprite_id) },
    { "sprite_b", RSCACHE_HEALTHBAR_FIELD_BACK_SPRITE,
      offsetof(struct RSCache_Dat2ConfigHealthbar, back_sprite_id) },
    { "opcode11", RSCACHE_HEALTHBAR_FIELD_FADE_THRESHOLD,
      offsetof(struct RSCache_Dat2ConfigHealthbar, fade_threshold) },
    { "opcode14", RSCACHE_HEALTHBAR_FIELD_WIDTH,
      offsetof(struct RSCache_Dat2ConfigHealthbar, width) },
};

#define HEALTHBAR_KEY_COUNT ((int)(sizeof(HEALTHBAR_KEYS) / sizeof(HEALTHBAR_KEYS[0])))

static int*
healthbar_value(
    struct RSCache_Dat2ConfigHealthbar* entry,
    int row)
{
    return (int*)((char*)entry + HEALTHBAR_KEYS[row].offset);
}

int
cp_unpack_healthbar(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigHealthbar entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigHealthbarDecodeInplace(&entry, record, record_size);
    if( entry._consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "healthbar %d: consumed %d of %d bytes", id,
                entry._consumed, record_size);

    for( int row = 0; row < HEALTHBAR_KEY_COUNT; row++ )
    {
        if( RSCache_PresenceHas(&entry.present, HEALTHBAR_KEYS[row].field) )
            cp_lines_addf(out, "%s=%d", HEALTHBAR_KEYS[row].key, *healthbar_value(&entry, row));
        else
            cp_lines_add_default(out, HEALTHBAR_KEYS[row].key);
    }
    return 1;
}

uint32_t
cp_pack_healthbar(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigHealthbar entry;
    memset(&entry, 0, sizeof(entry));
    /* The empty-record decode: client defaults, nothing stated. */
    RSCache_Dat2ConfigHealthbarDecodeInplace(
        &entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int row = -1;

        if( cp_value_is_default(value) )
            continue;
        for( int r = 0; r < HEALTHBAR_KEY_COUNT; r++ )
        {
            if( strcmp(HEALTHBAR_KEYS[r].key, key) == 0 )
                row = r;
        }
        if( row < 0 )
        {
            cp_warn(ctx, &ctx->warn_unknown_key, "healthbar [%s]: unknown key %s",
                    config->debugname, key);
            continue;
        }
        RSCache_PresenceSet(&entry.present, HEALTHBAR_KEYS[row].field);
        if( !cp_parse_int(value, healthbar_value(&entry, row)) )
        {
            fprintf(stderr, "cachepack: healthbar [%s]: bad value for %s\n",
                    config->debugname, key);
            return 0;
        }
    }

    /* The runtime loader's mirrors of `present` (see the header). */
    entry.has_draw_order =
        RSCache_PresenceHas(&entry.present, RSCACHE_HEALTHBAR_FIELD_DRAW_ORDER);
    entry.has_evict_priority =
        RSCache_PresenceHas(&entry.present, RSCACHE_HEALTHBAR_FIELD_EVICT_PRIORITY);
    entry.has_persist_cycles =
        RSCache_PresenceHas(&entry.present, RSCACHE_HEALTHBAR_FIELD_PERSIST_CYCLES);
    entry.has_fade_threshold =
        RSCache_PresenceHas(&entry.present, RSCACHE_HEALTHBAR_FIELD_FADE_THRESHOLD);
    entry.has_width = RSCache_PresenceHas(&entry.present, RSCACHE_HEALTHBAR_FIELD_WIDTH);

    return RSCache_Dat2ConfigHealthbarEncode(&entry, out, out_capacity);
}

/* ---- hitsplat ----------------------------------------------------------- */

/*
 * Every number in a hitsplat record names something else.
 *
 * This text used to be `opcode7=12`, `sprite=1358`, `variantvar=10236,-1,26` —
 * three kinds of magic number in one record. The opcode number is not the
 * field's meaning (the meaning is in the reference's renderer, and
 * `dat2_config_hitsplat.h` now carries it); the sprite is `hitmark_0`, which
 * `pack/8_sprites.pack` has always known; and 10236 is varbit
 * `hitsplat_tint_disabled`, the All Settings row "Hitsplat tinting", which is
 * the whole reason the record exists.
 *
 * So the keys are the field names and the values are spelled in the namespace
 * they belong to — sprites through `pack/8_sprites.pack`, the selector's var
 * through `all.varbit`/`all.varp`, and its variant ids through this same file's
 * own `all.hitsplat.compack`, exactly as the `[section]` names already are. A
 * bare number is still accepted everywhere, for an id no pack lists yet.
 *
 * `opcodeorder` is written in the same key names, because the packing order is
 * the one place the opcode numbers would otherwise still show through.
 */

/** One text key and the opcode it stands for, in the order unpack emits them. */
struct hitsplat_field
{
    const char* key;
    int opcode;
};

static const struct hitsplat_field HITSPLAT_FIELDS[] = {
    { "font", 1 },       { "textcolour", 2 }, { "iconsprite", 3 }, { "leftsprite", 4 },
    { "sprite", 5 },     { "rightsprite", 6 }, { "driftx", 7 },    { "text", 8 },
    { "duration", 9 },   { "driftup", 10 },   { "fade", 11 },      { "slotpolicy", 12 },
    { "texty", 13 },     { "fadeafter", 14 }, { "variants", 18 },
};

/**
 * The key `opcodeorder` spells `opcode` as, or NULL for an opcode with no key.
 *
 * 17 and 18 share `variants` — they are the same field, and which one a record
 * carries is stated by whether it has a `variantdefault` line, not here.
 */
static const char*
hitsplat_key_for_opcode(int opcode)
{
    if( opcode == 17 )
        opcode = 18;
    for( size_t i = 0; i < sizeof(HITSPLAT_FIELDS) / sizeof(HITSPLAT_FIELDS[0]); i++ )
        if( HITSPLAT_FIELDS[i].opcode == opcode )
            return HITSPLAT_FIELDS[i].key;
    return NULL;
}

/** The opcode `key` stands for, or -1. `variants` answers 18; see above. */
static int
hitsplat_opcode_for_key(const char* key)
{
    for( size_t i = 0; i < sizeof(HITSPLAT_FIELDS) / sizeof(HITSPLAT_FIELDS[0]); i++ )
        if( strcmp(HITSPLAT_FIELDS[i].key, key) == 0 )
            return HITSPLAT_FIELDS[i].opcode;
    return -1;
}

/** Opcode 12's three values, spelled as `enum World_HitmarkSlotPolicy` reads them. */
static const struct
{
    const char* name;
    int value;
} HITSPLAT_SLOT_POLICIES[] = {
    { "discard", -1 },
    { "oldest", 0 },
    { "smallest", 1 },
};

/*
 * The spellings this file used before the fields had names.
 *
 * Refused rather than silently ignored, and for the reason `variantabc` was:
 * a tree still carrying `opcode7=12` was written by a decoder that also emitted
 * `opcode49`, and a warn-and-continue on an unknown key drops the field from
 * every record that carries it — which is how a bake once lost every hitsplat's
 * text and selector at once. The message names the replacement so the fix is
 * mechanical.
 */
static const struct
{
    const char* retired;
    const char* now;
} HITSPLAT_RETIRED_KEYS[] = {
    { "opcode1", "font" },
    { "colour", "textcolour" },
    { "opcode3", "iconsprite" },
    { "opcode4", "leftsprite" },
    { "opcode6", "rightsprite" },
    { "opcode7", "driftx" },
    { "opcode10", "driftup" },
    { "opcode11", "fade" },
    { "opcode13", "texty" },
    { "opcode14", "fadeafter" },
    { "variantop", "variantdefault (present = opcode 18, absent = opcode 17)" },
    { "variantvar", "variantvarbit, variantvarp and variantdefault" },
};

/** Append `text` to a comma-separated list, or return 0 when it would not fit. */
static int
hitsplat_list_append(
    char* list,
    size_t capacity,
    size_t* used,
    const char* text)
{
    size_t length = strlen(text);

    if( *used + length + (*used ? 1 : 0) + 1 > capacity )
        return 0;
    if( *used )
        list[(*used)++] = ',';
    memcpy(list + *used, text, length);
    *used += length;
    list[*used] = '\0';
    return 1;
}

/** `key=<asset name>` for a stated sprite or font. Falls back to the id, as refs
 *  do, and spells a (hand-built) -1 as `null`, which reads back as -1. */
static void
hitsplat_emit_asset(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const char* key,
    enum CP_AssetId asset,
    int id)
{
    if( id < 0 )
    {
        cp_lines_addf(out, "%s=null", key);
        return;
    }
    const char* name = cp_asset_name_ensure(ctx, asset, id);
    if( name )
        cp_lines_addf(out, "%s=%s", key, name);
    else
        cp_lines_addf(out, "%s=%d", key, id);
}

/** An asset reference: a name from the pack, `null`, or a bare id. */
static int
hitsplat_resolve_asset(
    struct CP_Ctx* ctx,
    enum CP_AssetId asset,
    const char* text,
    int* out_id)
{
    if( strcmp(text, "null") == 0 )
    {
        *out_id = -1;
        return 1;
    }
    int id = cp_asset_name_find(ctx, asset, text);
    if( id >= 0 )
    {
        *out_id = id;
        return 1;
    }
    if( cp_parse_int(text, out_id) )
        return 1;
    cp_warn(ctx, &ctx->warn_unresolved_name, "unknown %s reference '%s'",
            cp_asset(asset)->pack, text);
    return 0;
}

/**
 * `key=<name>`, spelling -1 as `null` rather than dropping the line.
 *
 * -1 is an answer in a selector and not an absence: an unset var slot says which
 * of the two the record asks, and a -1 variant means "draw no splat at all".
 */
static void
hitsplat_emit_ref_or_null(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const char* key,
    enum CP_TypeId type,
    int id)
{
    if( id < 0 )
    {
        cp_lines_addf(out, "%s=null", key);
        return;
    }
    cp_emit_name(ctx, out, key, type, id);
}

/** A stated sprite/font field, or `key=default`. */
static void
hitsplat_emit_asset_field(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const struct RSCache_Dat2ConfigHitsplat* entry,
    int field,
    const char* key,
    enum CP_AssetId asset,
    int id)
{
    if( RSCache_PresenceHas(&entry->present, field) )
        hitsplat_emit_asset(ctx, out, key, asset, id);
    else
        cp_lines_add_default(out, key);
}

/** A stated plain-integer field, or `key=default`. */
static void
hitsplat_emit_int_field(
    struct CP_Lines* out,
    const struct RSCache_Dat2ConfigHitsplat* entry,
    int field,
    const char* key,
    int value)
{
    if( RSCache_PresenceHas(&entry->present, field) )
        cp_lines_addf(out, "%s=%d", key, value);
    else
        cp_lines_add_default(out, key);
}

int
cp_unpack_hitsplat(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigHitsplat entry;
    const struct RSCache_Presence* has = &entry.present;

    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigHitsplatDecodeInplace(
        &entry, record, record_size,
        (unsigned)RSCache_Dat2ConfigHitsplatFlags(ctx ? &ctx->profile : NULL));
    if( entry._consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "hitsplat %d: consumed %d of %d bytes", id,
                entry._consumed, record_size);

    /* The four sprites in the order the renderer lays them out, left to right:
     * icon, left cap, the tiled body, right cap. See `dat2_config_hitsplat.h`. */
    hitsplat_emit_asset_field(ctx, out, &entry, RSCACHE_HITSPLAT_FIELD_ICON_SPRITE, "iconsprite",
                              CP_ASSET_SPRITE, entry.icon_sprite_id);
    hitsplat_emit_asset_field(ctx, out, &entry, RSCACHE_HITSPLAT_FIELD_LEFT_SPRITE, "leftsprite",
                              CP_ASSET_SPRITE, entry.left_sprite_id);
    hitsplat_emit_asset_field(ctx, out, &entry, RSCACHE_HITSPLAT_FIELD_SPRITE, "sprite",
                              CP_ASSET_SPRITE, entry.sprite_id);
    hitsplat_emit_asset_field(ctx, out, &entry, RSCACHE_HITSPLAT_FIELD_RIGHT_SPRITE,
                              "rightsprite", CP_ASSET_SPRITE, entry.right_sprite_id);
    hitsplat_emit_asset_field(ctx, out, &entry, RSCACHE_HITSPLAT_FIELD_FONT, "font",
                              CP_ASSET_FONT, entry.font_id);

    /* Hex because a colour read as 16711680 is a number and one read as 0xFF0000
     * is a colour. `cp_parse_int` takes either. */
    if( RSCache_PresenceHas(has, RSCACHE_HITSPLAT_FIELD_TEXT_COLOUR) )
        cp_lines_addf(out, "textcolour=0x%06X", entry.text_colour & 0xFFFFFF);
    else
        cp_lines_add_default(out, "textcolour");

    /* Opcode 8 is a string, not the u16 this tool used to emit — see
     * `dat2_config_hitsplat.h`. The marker byte is part of the same opcode and
     * rides with it, because it is what makes the repack byte-identical (it is 0
     * on every record measured). */
    if( RSCache_PresenceHas(has, RSCACHE_HITSPLAT_FIELD_TEXT) )
    {
        cp_lines_add_str(out, "text", entry.text);
        cp_lines_addf(out, "textmarker=%d", entry.text_marker);
    }
    else
    {
        cp_lines_add_default(out, "text");
        cp_lines_add_default(out, "textmarker");
    }

    hitsplat_emit_int_field(out, &entry, RSCACHE_HITSPLAT_FIELD_TEXT_OFFSET_Y, "texty",
                            entry.text_offset_y);
    hitsplat_emit_int_field(out, &entry, RSCACHE_HITSPLAT_FIELD_DURATION, "duration",
                            entry.duration);
    hitsplat_emit_int_field(out, &entry, RSCACHE_HITSPLAT_FIELD_DRIFT_X, "driftx", entry.drift_x);
    hitsplat_emit_int_field(out, &entry, RSCACHE_HITSPLAT_FIELD_DRIFT_UP, "driftup",
                            entry.drift_up);

    /* Opcode 11 is `fadeafter=0` written as one byte; the two spellings are kept
     * apart so the record re-encodes to the byte it carried. */
    if( RSCache_PresenceHas(has, RSCACHE_HITSPLAT_FIELD_FADE_FLAG) )
        cp_lines_addf(out, "fade=yes");
    else
        cp_lines_add_default(out, "fade");
    hitsplat_emit_int_field(out, &entry, RSCACHE_HITSPLAT_FIELD_FADE_AFTER, "fadeafter",
                            entry.fade_after);

    if( RSCache_PresenceHas(has, RSCACHE_HITSPLAT_FIELD_SLOT_POLICY) )
    {
        const char* name = NULL;
        for( size_t i = 0; i < sizeof(HITSPLAT_SLOT_POLICIES) / sizeof(HITSPLAT_SLOT_POLICIES[0]);
             i++ )
            if( HITSPLAT_SLOT_POLICIES[i].value == entry.slot_policy )
                name = HITSPLAT_SLOT_POLICIES[i].name;
        if( name )
            cp_lines_addf(out, "slotpolicy=%s", name);
        else
            cp_lines_addf(out, "slotpolicy=%d", entry.slot_policy);
    }
    else
        cp_lines_add_default(out, "slotpolicy");

    if( RSCache_PresenceHas(has, RSCACHE_HITSPLAT_FIELD_VARIANTS) )
    {
        /* Opcode 17/18's payload IS structured — the reference reads
         * `u16, u16, [u16], u8 count, u16[count+1]`: a varbit, a varp, opcode
         * 18's fallback splat, and the ids the var's value indexes.
         *
         * `variantdefault` is the fallback AND the opcode: 18 states one, 17 does
         * not, so `variantdefault=default` is exactly "this is an opcode 17" --
         * the field is not stated and the reference's -1 applies. Both var lines
         * are always written, `null` included, so a selector record can be read
         * without knowing which of the two carries the question. */
        char list[RSCACHE_HITSPLAT_MAX_VARIANTS * 96];
        size_t used = 0;

        list[0] = '\0';
        for( int i = 0; i < entry.variant_count; i++ )
        {
            const char* name =
                entry.variants[i] < 0 ? "null"
                                      : cp_name_ensure(ctx, CP_TYPE_HITSPLAT, entry.variants[i]);
            int fits;

            assert(name);
            fits = hitsplat_list_append(list, sizeof(list), &used, name);
            assert(fits);
        }

        hitsplat_emit_ref_or_null(ctx, out, "variantvarbit", CP_TYPE_VARBIT, entry.variant_varbit);
        hitsplat_emit_ref_or_null(ctx, out, "variantvarp", CP_TYPE_VARP, entry.variant_varp);
        if( RSCache_PresenceHas(has, RSCACHE_HITSPLAT_FIELD_VARIANT_FALLBACK) )
            hitsplat_emit_ref_or_null(ctx, out, "variantdefault", CP_TYPE_HITSPLAT,
                                      entry.variant_fallback);
        else
            cp_lines_add_default(out, "variantdefault");
        cp_lines_addf(out, "variants=%s", list);
    }
    else
    {
        cp_lines_add_default(out, "variantvarbit");
        cp_lines_add_default(out, "variantvarp");
        cp_lines_add_default(out, "variantdefault");
        cp_lines_add_default(out, "variants");
    }

    /*
     * Records do not share a packing order and the encoder replays whatever it is
     * given, so the order is data. Without it a repack is still a valid hitsplat,
     * just not the same bytes. Written in the field names rather than the opcode
     * numbers, so the line says which fields the record carries. A record that
     * states nothing has no order: `opcodeorder=default`, and the encoder falls
     * back to ascending.
     */
    if( entry.opcode_count > 0 )
    {
        char order[RSCACHE_HITSPLAT_MAX_OPCODES * 20];
        size_t used = 0;

        order[0] = '\0';
        for( int i = 0; i < entry.opcode_count; i++ )
        {
            const char* key = hitsplat_key_for_opcode(entry.opcodes[i]);
            char number[16];
            int fits;
            if( !key )
            {
                snprintf(number, sizeof(number), "%d", entry.opcodes[i]);
                key = number;
            }
            fits = hitsplat_list_append(order, sizeof(order), &used, key);
            assert(fits);
        }
        cp_lines_addf(out, "opcodeorder=%s", order);
    }
    else
        cp_lines_add_default(out, "opcodeorder");
    return 1;
}

uint32_t
cp_pack_hitsplat(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigHitsplat entry;
    memset(&entry, 0, sizeof(entry));
    /* The empty-record decode: the reference's defaults, nothing stated. */
    RSCache_Dat2ConfigHitsplatDecodeInplace(
        &entry, cp_empty_record, (int)sizeof(cp_empty_record),
        RSCACHE_CONFIG_HITSPLAT_DECODE_OSRS);
    entry.id = id;

#define HITSPLAT_SET(field) RSCache_PresenceSet(&entry.present, RSCACHE_HITSPLAT_FIELD_##field)

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;

        if( cp_value_is_default(value) )
            continue;
        if( strcmp(key, "sprite") == 0 )
        {
            HITSPLAT_SET(SPRITE);
            ok = hitsplat_resolve_asset(ctx, CP_ASSET_SPRITE, value, &entry.sprite_id);
        }
        else if( strcmp(key, "iconsprite") == 0 )
        {
            HITSPLAT_SET(ICON_SPRITE);
            ok = hitsplat_resolve_asset(ctx, CP_ASSET_SPRITE, value, &entry.icon_sprite_id);
        }
        else if( strcmp(key, "leftsprite") == 0 )
        {
            HITSPLAT_SET(LEFT_SPRITE);
            ok = hitsplat_resolve_asset(ctx, CP_ASSET_SPRITE, value, &entry.left_sprite_id);
        }
        else if( strcmp(key, "rightsprite") == 0 )
        {
            HITSPLAT_SET(RIGHT_SPRITE);
            ok = hitsplat_resolve_asset(ctx, CP_ASSET_SPRITE, value, &entry.right_sprite_id);
        }
        else if( strcmp(key, "font") == 0 )
        {
            HITSPLAT_SET(FONT);
            ok = hitsplat_resolve_asset(ctx, CP_ASSET_FONT, value, &entry.font_id);
        }
        else if( strcmp(key, "textcolour") == 0 )
        {
            HITSPLAT_SET(TEXT_COLOUR);
            ok = cp_parse_int(value, &entry.text_colour);
        }
        else if( strcmp(key, "text") == 0 )
        {
            char buf[RSCACHE_HITSPLAT_MAX_TEXT * 2];

            HITSPLAT_SET(TEXT);
            cp_unescape(value, buf, sizeof(buf));
            if( strlen(buf) >= RSCACHE_HITSPLAT_MAX_TEXT )
                ok = 0;
            else
                snprintf(entry.text, sizeof(entry.text), "%s", buf);
        }
        else if( strcmp(key, "textmarker") == 0 )
        {
            /* Part of opcode 8: stating it is not stating the text, so it sets no
             * bit of its own. A `textmarker` without a `text` writes nothing. */
            int marker = 0;
            ok = cp_parse_int(value, &marker) && marker >= 0 && marker <= 255;
            entry.text_marker = (uint8_t)marker;
        }
        else if( strcmp(key, "texty") == 0 )
        {
            HITSPLAT_SET(TEXT_OFFSET_Y);
            ok = cp_parse_int(value, &entry.text_offset_y);
        }
        else if( strcmp(key, "duration") == 0 )
        {
            HITSPLAT_SET(DURATION);
            ok = cp_parse_int(value, &entry.duration);
        }
        else if( strcmp(key, "driftx") == 0 )
        {
            HITSPLAT_SET(DRIFT_X);
            ok = cp_parse_int(value, &entry.drift_x);
        }
        else if( strcmp(key, "driftup") == 0 )
        {
            HITSPLAT_SET(DRIFT_UP);
            ok = cp_parse_int(value, &entry.drift_up);
        }
        else if( strcmp(key, "fade") == 0 )
        {
            HITSPLAT_SET(FADE_FLAG);
            ok = small_parse_flag(value);
            entry.fade_after = 0;
        }
        else if( strcmp(key, "fadeafter") == 0 )
        {
            HITSPLAT_SET(FADE_AFTER);
            ok = cp_parse_int(value, &entry.fade_after);
        }
        else if( strcmp(key, "slotpolicy") == 0 )
        {
            HITSPLAT_SET(SLOT_POLICY);
            ok = 0;
            for( size_t p = 0;
                 p < sizeof(HITSPLAT_SLOT_POLICIES) / sizeof(HITSPLAT_SLOT_POLICIES[0]); p++ )
            {
                if( strcmp(HITSPLAT_SLOT_POLICIES[p].name, value) != 0 )
                    continue;
                entry.slot_policy = HITSPLAT_SLOT_POLICIES[p].value;
                ok = 1;
                break;
            }
            if( !ok )
                ok = cp_parse_int(value, &entry.slot_policy);
        }
        /* The selector is one field over four keys; any of them stating a value
         * states it. The fallback is opcode 18's alone, so stating one is how a
         * record says it is an 18, and `variantdefault=default` an opcode 17. */
        else if( strcmp(key, "variantvarbit") == 0 )
        {
            HITSPLAT_SET(VARIANTS);
            ok = cp_resolve_ref_or_null(ctx, CP_TYPE_VARBIT, value, &entry.variant_varbit);
        }
        else if( strcmp(key, "variantvarp") == 0 )
        {
            HITSPLAT_SET(VARIANTS);
            ok = cp_resolve_ref_or_null(ctx, CP_TYPE_VARP, value, &entry.variant_varp);
        }
        else if( strcmp(key, "variantdefault") == 0 )
        {
            HITSPLAT_SET(VARIANTS);
            HITSPLAT_SET(VARIANT_FALLBACK);
            ok = cp_resolve_ref_or_null(ctx, CP_TYPE_HITSPLAT, value, &entry.variant_fallback);
        }
        else if( strcmp(key, "variants") == 0 )
        {
            char scratch[RSCACHE_HITSPLAT_MAX_VARIANTS * 96];
            char* fields[RSCACHE_HITSPLAT_MAX_VARIANTS];

            HITSPLAT_SET(VARIANTS);
            if( strlen(value) >= sizeof(scratch) )
                ok = 0;
            else
            {
                int n = cp_split(value, scratch, fields, RSCACHE_HITSPLAT_MAX_VARIANTS);
                entry.variant_count = 0;
                for( int f = 0; f < n && ok; f++ )
                    ok = cp_resolve_ref_or_null(ctx, CP_TYPE_HITSPLAT, fields[f],
                                                &entry.variants[entry.variant_count++]);
                if( entry.variant_count == 0 )
                    ok = 0;
            }
        }
        else if( strcmp(key, "opcodeorder") == 0 )
        {
            char scratch[RSCACHE_HITSPLAT_MAX_OPCODES * 20];
            char* fields[RSCACHE_HITSPLAT_MAX_OPCODES];
            if( strlen(value) >= sizeof(scratch) )
            {
                ok = 0;
            }
            else
            {
                int n = cp_split(value, scratch, fields, RSCACHE_HITSPLAT_MAX_OPCODES);
                entry.opcode_count = 0;
                for( int f = 0; f < n && ok; f++ )
                {
                    int op = hitsplat_opcode_for_key(fields[f]);
                    if( op < 0 )
                        ok = cp_parse_int(fields[f], &op);
                    if( ok )
                        entry.opcodes[entry.opcode_count++] = (uint8_t)op;
                }
            }
        }
        else
        {
            const char* now = NULL;
            for( size_t r = 0;
                 r < sizeof(HITSPLAT_RETIRED_KEYS) / sizeof(HITSPLAT_RETIRED_KEYS[0]); r++ )
                if( strcmp(HITSPLAT_RETIRED_KEYS[r].retired, key) == 0 )
                    now = HITSPLAT_RETIRED_KEYS[r].now;
            if( now )
            {
                fprintf(stderr,
                        "cachepack: hitsplat [%s]: `%s` is the spelling from before the "
                        "fields had names — write `%s`\n",
                        config->debugname, key, now);
                return 0;
            }
            cp_warn(ctx, &ctx->warn_unknown_key, "hitsplat [%s]: unknown key %s",
                    config->debugname, key);
        }
        if( !ok )
        {
            fprintf(stderr, "cachepack: hitsplat [%s]: bad value for %s\n", config->debugname, key);
            return 0;
        }
    }

#undef HITSPLAT_SET

    /* A selector needs its ids: `variantvarbit=` without `variants=` has nothing
     * for the value to index. */
    if( RSCache_PresenceHas(&entry.present, RSCACHE_HITSPLAT_FIELD_VARIANTS) &&
        entry.variant_count == 0 )
    {
        fprintf(stderr, "cachepack: hitsplat [%s]: a variant selector with no `variants`\n",
                config->debugname);
        return 0;
    }

    return RSCache_Dat2ConfigHitsplatEncode(&entry, out, out_capacity);
}

/* ---- mapelement --------------------------------------------------------- */

/*
 * Every opcode has a key now; see dat2_config_mapelement.h. The keys the client
 * is not known to use are named by opcode (`unknown21`) rather than guessed at.
 * A visibility is `varbit,varp,min,max` with -1 for an unset var; a polygon is
 * its vertices (`polygonpoint=x,y,flag`, one line each), its fill and its extra
 * list, three keys for the one opcode 15.
 */

static const char* const MAPELEMENT_OP_KEYS[RSCACHE_MAPELEMENT_OPS] = {
    "op1", "op2", "op3", "op4", "op5",
};

#define MAPELEMENT_HAS(entry, field)                                                               \
    RSCache_PresenceHas(&(entry)->present, RSCACHE_MAPELEMENT_FIELD_##field)

static void
mapelement_emit_int(
    struct CP_Lines* out,
    const struct RSCache_MapElement* entry,
    int field,
    const char* key,
    int value)
{
    if( RSCache_PresenceHas(&entry->present, field) )
        cp_lines_addf(out, "%s=%d", key, value);
    else
        cp_lines_add_default(out, key);
}

static void
mapelement_emit_str(
    struct CP_Lines* out,
    const struct RSCache_MapElement* entry,
    int field,
    const char* key,
    const char* value)
{
    if( RSCache_PresenceHas(&entry->present, field) )
        cp_lines_add_str(out, key, value);
    else
        cp_lines_add_default(out, key);
}

static void
mapelement_emit_colour(
    struct CP_Lines* out,
    const struct RSCache_MapElement* entry,
    int field,
    const char* key,
    int value)
{
    if( RSCache_PresenceHas(&entry->present, field) )
        cp_lines_addf(out, "%s=0x%06X", key, value & 0xFFFFFF);
    else
        cp_lines_add_default(out, key);
}

static void
mapelement_emit_visibility(
    struct CP_Lines* out,
    const struct RSCache_MapElement* entry,
    int field,
    const char* key,
    const struct RSCache_MapElementVisibility* visibility)
{
    if( RSCache_PresenceHas(&entry->present, field) )
        cp_lines_addf(out, "%s=%d,%d,%d,%d", key, visibility->varbit, visibility->varp,
                      visibility->min, visibility->max);
    else
        cp_lines_add_default(out, key);
}

int
cp_unpack_mapelement(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_MapElement entry;
    memset(&entry, 0, sizeof(entry));
    entry.id = id;
    RSCache_MapElementDecodeInplace(&entry, record, record_size);
    if( entry._consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "mapelement %d: consumed %d of %d bytes", id,
                entry._consumed, record_size);

    mapelement_emit_str(out, &entry, RSCACHE_MAPELEMENT_FIELD_NAME, "name", entry.name);
    mapelement_emit_int(out, &entry, RSCACHE_MAPELEMENT_FIELD_SPRITE, "sprite", entry.sprite_id);
    mapelement_emit_int(out, &entry, RSCACHE_MAPELEMENT_FIELD_HOVER_SPRITE, "hoversprite",
                        entry.hover_sprite_id);
    mapelement_emit_colour(out, &entry, RSCACHE_MAPELEMENT_FIELD_TEXT_COLOUR, "textcolour",
                           entry.text_colour);
    mapelement_emit_colour(out, &entry, RSCACHE_MAPELEMENT_FIELD_HOVER_TEXT_COLOUR,
                           "hovertextcolour", entry.hover_text_colour);
    mapelement_emit_int(out, &entry, RSCACHE_MAPELEMENT_FIELD_TEXT_SIZE, "textsize",
                        entry.text_size);
    mapelement_emit_int(out, &entry, RSCACHE_MAPELEMENT_FIELD_FLAGS, "flags", entry.flags);
    mapelement_emit_int(out, &entry, RSCACHE_MAPELEMENT_FIELD_RANDOMIZE_POSITION,
                        "randomizeposition", entry.randomize_position);
    mapelement_emit_visibility(out, &entry, RSCACHE_MAPELEMENT_FIELD_VISIBILITY, "visibility",
                               &entry.visibility);
    for( int i = 0; i < RSCACHE_MAPELEMENT_OPS; i++ )
        mapelement_emit_str(out, &entry, RSCACHE_MAPELEMENT_FIELD_OP1 + i, MAPELEMENT_OP_KEYS[i],
                            entry.ops[i]);

    if( MAPELEMENT_HAS(&entry, POLYGON) )
    {
        if( entry.polygon_point_count == 0 )
            cp_lines_add_empty(out, "polygonpoint");
        for( int i = 0; i < entry.polygon_point_count; i++ )
            cp_lines_addf(out, "polygonpoint=%d,%d,%d", entry.polygon_points[i * 2],
                          entry.polygon_points[i * 2 + 1], entry.polygon_point_flags[i]);
        cp_lines_addf(out, "polygonfill=%d", entry.polygon_fill);
        if( entry.polygon_extra_count == 0 )
            cp_lines_add_empty(out, "polygonextra");
        for( int i = 0; i < entry.polygon_extra_count; i++ )
            cp_lines_addf(out, "polygonextra=%d", entry.polygon_extra[i]);
    }
    else
    {
        cp_lines_add_default(out, "polygonpoint");
        cp_lines_add_default(out, "polygonfill");
        cp_lines_add_default(out, "polygonextra");
    }

    if( MAPELEMENT_HAS(&entry, UNKNOWN16) )
        cp_lines_addf(out, "unknown16=yes");
    else
        cp_lines_add_default(out, "unknown16");
    mapelement_emit_str(out, &entry, RSCACHE_MAPELEMENT_FIELD_TARGET_NAME, "targetname",
                        entry.target_name);
    mapelement_emit_int(out, &entry, RSCACHE_MAPELEMENT_FIELD_UNKNOWN18, "unknown18",
                        entry.unknown18);
    mapelement_emit_int(out, &entry, RSCACHE_MAPELEMENT_FIELD_CATEGORY, "category",
                        entry.category);
    mapelement_emit_visibility(out, &entry, RSCACHE_MAPELEMENT_FIELD_VISIBILITY2, "visibility2",
                               &entry.visibility2);
    mapelement_emit_int(out, &entry, RSCACHE_MAPELEMENT_FIELD_UNKNOWN21, "unknown21",
                        entry.unknown21);
    mapelement_emit_int(out, &entry, RSCACHE_MAPELEMENT_FIELD_UNKNOWN22, "unknown22",
                        entry.unknown22);
    if( MAPELEMENT_HAS(&entry, UNKNOWN23) )
        cp_lines_addf(out, "unknown23=%d,%d,%d", entry.unknown23[0], entry.unknown23[1],
                      entry.unknown23[2]);
    else
        cp_lines_add_default(out, "unknown23");
    if( MAPELEMENT_HAS(&entry, UNKNOWN24) )
        cp_lines_addf(out, "unknown24=%d,%d", entry.unknown24[0], entry.unknown24[1]);
    else
        cp_lines_add_default(out, "unknown24");
    mapelement_emit_int(out, &entry, RSCACHE_MAPELEMENT_FIELD_UNKNOWN25, "unknown25",
                        entry.unknown25);
    mapelement_emit_int(out, &entry, RSCACHE_MAPELEMENT_FIELD_UNKNOWN28, "unknown28",
                        entry.unknown28);
    mapelement_emit_int(out, &entry, RSCACHE_MAPELEMENT_FIELD_HORIZONTAL_ALIGN, "halign",
                        entry.horizontal_align);
    mapelement_emit_int(out, &entry, RSCACHE_MAPELEMENT_FIELD_VERTICAL_ALIGN, "valign",
                        entry.vertical_align);
    small_emit_param_map(ctx, out, MAPELEMENT_HAS(&entry, PARAMS), &entry.params);

    RSCache_MapElementFreeInplace(&entry);
    return 1;
}

/* `count` comma-separated ints into `out`; 0 unless it is exactly that many. */
static int
mapelement_parse_ints(
    const char* value,
    int* out,
    int count)
{
    char scratch[256];
    char* fields[8];

    assert(count <= 8);
    if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 8) != count )
        return 0;
    for( int i = 0; i < count; i++ )
    {
        if( !cp_parse_int(fields[i], &out[i]) )
            return 0;
    }
    return 1;
}

static int
mapelement_parse_visibility(
    const char* value,
    struct RSCache_MapElementVisibility* visibility)
{
    int v[4];

    if( !mapelement_parse_ints(value, v, 4) )
        return 0;
    visibility->varbit = v[0];
    visibility->varp = v[1];
    visibility->min = v[2];
    visibility->max = v[3];
    return 1;
}

static void
mapelement_set_string(
    char** slot,
    const char* value)
{
    char buf[1024];

    cp_unescape(value, buf, sizeof(buf));
    free(*slot);
    *slot = strdup(buf);
    assert(*slot);
}

uint32_t
cp_pack_mapelement(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_MapElement entry;
    struct CP_IntList points = { 0 };
    struct CP_IntList point_flags = { 0 };
    struct CP_IntList extra = { 0 };

    memset(&entry, 0, sizeof(entry));
    entry.id = id;
    /* The empty-record decode: the defaults, nothing stated. */
    RSCache_MapElementDecodeInplace(&entry, cp_empty_record, (int)sizeof(cp_empty_record));

#define MAPELEMENT_SET(field) RSCache_PresenceSet(&entry.present, RSCACHE_MAPELEMENT_FIELD_##field)

    uint32_t written = 0;
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int op = -1;
        int ok = 1;

        if( cp_value_is_default(value) )
            continue;
        for( int o = 0; o < RSCACHE_MAPELEMENT_OPS; o++ )
        {
            if( strcmp(key, MAPELEMENT_OP_KEYS[o]) == 0 )
                op = o;
        }

        if( strcmp(key, "name") == 0 )
        {
            MAPELEMENT_SET(NAME);
            mapelement_set_string(&entry.name, value);
        }
        else if( strcmp(key, "sprite") == 0 )
        {
            MAPELEMENT_SET(SPRITE);
            ok = cp_parse_int(value, &entry.sprite_id);
        }
        else if( strcmp(key, "hoversprite") == 0 )
        {
            MAPELEMENT_SET(HOVER_SPRITE);
            ok = cp_parse_int(value, &entry.hover_sprite_id);
        }
        else if( strcmp(key, "textcolour") == 0 )
        {
            MAPELEMENT_SET(TEXT_COLOUR);
            ok = cp_parse_int(value, &entry.text_colour);
        }
        else if( strcmp(key, "hovertextcolour") == 0 )
        {
            MAPELEMENT_SET(HOVER_TEXT_COLOUR);
            ok = cp_parse_int(value, &entry.hover_text_colour);
        }
        else if( strcmp(key, "textsize") == 0 )
        {
            MAPELEMENT_SET(TEXT_SIZE);
            ok = cp_parse_int(value, &entry.text_size);
        }
        else if( strcmp(key, "flags") == 0 )
        {
            MAPELEMENT_SET(FLAGS);
            ok = cp_parse_int(value, &entry.flags);
        }
        else if( strcmp(key, "randomizeposition") == 0 )
        {
            MAPELEMENT_SET(RANDOMIZE_POSITION);
            ok = cp_parse_int(value, &entry.randomize_position);
        }
        else if( strcmp(key, "visibility") == 0 )
        {
            MAPELEMENT_SET(VISIBILITY);
            ok = mapelement_parse_visibility(value, &entry.visibility);
        }
        else if( op >= 0 )
        {
            RSCache_PresenceSet(&entry.present, RSCACHE_MAPELEMENT_FIELD_OP1 + op);
            mapelement_set_string(&entry.ops[op], value);
        }
        else if( strcmp(key, "polygonpoint") == 0 )
        {
            MAPELEMENT_SET(POLYGON);
            if( !cp_value_is_empty(value) )
            {
                int v[3];
                ok = mapelement_parse_ints(value, v, 3);
                cp_intlist_push(&points, v[0]);
                cp_intlist_push(&points, v[1]);
                cp_intlist_push(&point_flags, v[2]);
            }
        }
        else if( strcmp(key, "polygonfill") == 0 )
        {
            MAPELEMENT_SET(POLYGON);
            ok = cp_parse_int(value, &entry.polygon_fill);
        }
        else if( strcmp(key, "polygonextra") == 0 )
        {
            MAPELEMENT_SET(POLYGON);
            if( !cp_value_is_empty(value) )
            {
                int v = 0;
                ok = cp_parse_int(value, &v);
                cp_intlist_push(&extra, v);
            }
        }
        else if( strcmp(key, "unknown16") == 0 )
        {
            MAPELEMENT_SET(UNKNOWN16);
            ok = small_parse_flag(value);
        }
        else if( strcmp(key, "targetname") == 0 )
        {
            MAPELEMENT_SET(TARGET_NAME);
            mapelement_set_string(&entry.target_name, value);
        }
        else if( strcmp(key, "unknown18") == 0 )
        {
            MAPELEMENT_SET(UNKNOWN18);
            ok = cp_parse_int(value, &entry.unknown18);
        }
        else if( strcmp(key, "category") == 0 )
        {
            MAPELEMENT_SET(CATEGORY);
            ok = cp_parse_int(value, &entry.category);
        }
        else if( strcmp(key, "visibility2") == 0 )
        {
            MAPELEMENT_SET(VISIBILITY2);
            ok = mapelement_parse_visibility(value, &entry.visibility2);
        }
        else if( strcmp(key, "unknown21") == 0 )
        {
            MAPELEMENT_SET(UNKNOWN21);
            ok = cp_parse_int(value, &entry.unknown21);
        }
        else if( strcmp(key, "unknown22") == 0 )
        {
            MAPELEMENT_SET(UNKNOWN22);
            ok = cp_parse_int(value, &entry.unknown22);
        }
        else if( strcmp(key, "unknown23") == 0 )
        {
            MAPELEMENT_SET(UNKNOWN23);
            ok = mapelement_parse_ints(value, entry.unknown23, 3);
        }
        else if( strcmp(key, "unknown24") == 0 )
        {
            MAPELEMENT_SET(UNKNOWN24);
            ok = mapelement_parse_ints(value, entry.unknown24, 2);
        }
        else if( strcmp(key, "unknown25") == 0 )
        {
            MAPELEMENT_SET(UNKNOWN25);
            ok = cp_parse_int(value, &entry.unknown25);
        }
        else if( strcmp(key, "unknown28") == 0 )
        {
            MAPELEMENT_SET(UNKNOWN28);
            ok = cp_parse_int(value, &entry.unknown28);
        }
        else if( strcmp(key, "halign") == 0 )
        {
            MAPELEMENT_SET(HORIZONTAL_ALIGN);
            ok = cp_parse_int(value, &entry.horizontal_align);
        }
        else if( strcmp(key, "valign") == 0 )
        {
            MAPELEMENT_SET(VERTICAL_ALIGN);
            ok = cp_parse_int(value, &entry.vertical_align);
        }
        else if( strcmp(key, "param") == 0 )
        {
            MAPELEMENT_SET(PARAMS);
            ok = small_parse_param_line(ctx, &entry.params, value);
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "mapelement [%s]: unknown key %s",
                    config->debugname, key);
        if( !ok )
        {
            fprintf(stderr, "cachepack: mapelement [%s]: bad value for %s\n",
                    config->debugname, key);
            goto done;
        }
    }

#undef MAPELEMENT_SET

    /* The polygon's lists are owned by the record from here; FreeInplace takes
     * them back. */
    entry.polygon_point_count = point_flags.count;
    entry.polygon_points = points.items;
    entry.polygon_point_flags = point_flags.items;
    entry.polygon_extra_count = extra.count;
    entry.polygon_extra = extra.items;
    points.items = NULL;
    point_flags.items = NULL;
    extra.items = NULL;

    written = RSCache_MapElementEncode(&entry, out, out_capacity);
done:
    free(points.items);
    free(point_flags.items);
    free(extra.items);
    RSCache_MapElementFreeInplace(&entry);
    return written;
}

/* ---- key tables --------------------------------------------------------- */

/* Each in the order its unpacker writes. One key per field, except where noted. */

const struct CP_KeySpec cp_inv_keys[] = {
    { "size", 0, NULL },
    { "param", CP_KEY_LIST, NULL },
    { NULL, 0, NULL },
};

const struct CP_KeySpec cp_param_keys[] = {
    { "type", 0, NULL },
    { "typechar", 0, NULL },
    { "typeid", 0, NULL },
    { "default", 0, NULL },
    { "autodisable", 0, NULL },
    { NULL, 0, NULL },
};

const struct CP_KeySpec cp_struct_keys[] = {
    { "param", CP_KEY_LIST, NULL },
    { NULL, 0, NULL },
};

const struct CP_KeySpec cp_healthbar_keys[] = {
    { "opcode2", 0, NULL },
    { "opcode3", 0, NULL },
    { "opcode5", 0, NULL },
    { "sprite_a", 0, NULL },
    { "sprite_b", 0, NULL },
    { "opcode11", 0, NULL },
    { "opcode14", 0, NULL },
    { NULL, 0, NULL },
};

/* `textmarker` rides on opcode 8 and the four `variant*` keys are opcode 17/18;
 * `opcodeorder` is no field at all but the record's packing order. */
const struct CP_KeySpec cp_hitsplat_keys[] = {
    { "iconsprite", 0, NULL },
    { "leftsprite", 0, NULL },
    { "sprite", 0, NULL },
    { "rightsprite", 0, NULL },
    { "font", 0, NULL },
    { "textcolour", 0, NULL },
    { "text", 0, NULL },
    { "textmarker", 0, NULL },
    { "texty", 0, NULL },
    { "duration", 0, NULL },
    { "driftx", 0, NULL },
    { "driftup", 0, NULL },
    { "fade", 0, NULL },
    { "fadeafter", 0, NULL },
    { "slotpolicy", 0, NULL },
    { "variantvarbit", 0, NULL },
    { "variantvarp", 0, NULL },
    { "variantdefault", 0, NULL },
    { "variants", 0, NULL },
    { "opcodeorder", 0, NULL },
    { NULL, 0, NULL },
};

/* `polygonpoint`, `polygonfill` and `polygonextra` are the one opcode 15. */
const struct CP_KeySpec cp_mapelement_keys[] = {
    { "name", 0, NULL },
    { "sprite", 0, NULL },
    { "hoversprite", 0, NULL },
    { "textcolour", 0, NULL },
    { "hovertextcolour", 0, NULL },
    { "textsize", 0, NULL },
    { "flags", 0, NULL },
    { "randomizeposition", 0, NULL },
    { "visibility", 0, NULL },
    { "op1", 0, NULL },
    { "op2", 0, NULL },
    { "op3", 0, NULL },
    { "op4", 0, NULL },
    { "op5", 0, NULL },
    { "polygonpoint", CP_KEY_LIST, NULL },
    { "polygonfill", 0, NULL },
    { "polygonextra", CP_KEY_LIST, NULL },
    { "unknown16", 0, NULL },
    { "targetname", 0, NULL },
    { "unknown18", 0, NULL },
    { "category", 0, NULL },
    { "visibility2", 0, NULL },
    { "unknown21", 0, NULL },
    { "unknown22", 0, NULL },
    { "unknown23", 0, NULL },
    { "unknown24", 0, NULL },
    { "unknown25", 0, NULL },
    { "unknown28", 0, NULL },
    { "halign", 0, NULL },
    { "valign", 0, NULL },
    { "param", CP_KEY_LIST, NULL },
    { NULL, 0, NULL },
};
