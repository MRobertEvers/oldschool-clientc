#include "cachepack.h"

/* `cp_asset`, for naming the asset namespace a hitsplat sprite reference missed. */
#include "cp_assets.h"

#include "datatypes/dat2_config_healthbar.h"
#include "datatypes/dat2_config_hitsplat.h"
#include "datatypes/dat2_config_inv.h"
#include "datatypes/dat2_config_mapelement.h"
#include "datatypes/dat2_config_param.h"
#include "datatypes/dat2_config_struct.h"

#include <stdlib.h>
#include <string.h>

/*
 * The small OldSchool-era types, none of which rev 254 has: inv, param, struct,
 * healthbar, hitsplat and mapelement.
 *
 * Five of the six are a handful of opcodes and round-trip byte-exactly. mapelement
 * is the exception and is marked lossy in the register — the library's decoder keeps
 * four of its two dozen fields, so its text is the client-visible view of the record
 * rather than the record.
 */

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

    cp_lines_addf(out, "size=%d", entry.size);
    cp_emit_params(ctx, out, &entry.params);
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
    RSCache_Dat2ConfigInvDecodeInplace(&entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    uint32_t written = 0;
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;
        if( strcmp(key, "size") == 0 )
            ok = cp_parse_int(value, &entry.size);
        else if( strcmp(key, "param") == 0 )
            ok = cp_parse_param(ctx, &entry.params, value);
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

int
cp_unpack_param(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigParam entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigParamDecodeInplace(&entry, record, record_size);

    struct RSCache_Dat2ConfigParam defaults;
    memset(&defaults, 0, sizeof(defaults));
    RSCache_Dat2ConfigParamDecodeInplace(
        &defaults, cp_empty_record, (int)sizeof(cp_empty_record));

    /*
     * `type` is the script var-type character (`i` int, `s` string, and the rest of
     * the ScriptVarType alphabet). It is written as the character rather than its
     * byte because that is how every reference spells it, and it is the field that
     * decides how a param *value* is read wherever one is used.
     */
    if( entry.type )
        cp_lines_addf(out, "type=%c", entry.type);
    if( entry.default_int )
        cp_lines_addf(out, "default=%d", entry.default_int);
    if( entry.default_long )
        cp_lines_addf(out, "defaultlong=%lld", (long long)entry.default_long);
    if( entry.default_string )
        cp_lines_add_str(out, "defaultstr", entry.default_string);
    /* Defaults to 1; opcode 4 is what clears it. Same trap as the overlay's
     * hide_underlay — writing the line only when the flag is set drops the opcode
     * from every record that actually carries it. */
    if( entry.auto_disable != defaults.auto_disable )
        cp_lines_addf(out, "autodisable=%s", entry.auto_disable ? "yes" : "no");

    RSCache_Dat2ConfigParamFreeInplace(&defaults);

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
    RSCache_Dat2ConfigParamDecodeInplace(&entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    /*
     * `type` first, in its own pass.
     *
     * `default` is read *through* it — `default=bones` is obj 526 only because the
     * type is `namedobj` — so a file that happens to write `default=` above
     * `type=` would otherwise resolve the name against a type that was still zero.
     * The machine export writes them in order and a person need not.
     */
    {
        const char* type_text = cp_config_get(config, "type");

        if( type_text )
            entry.type = cp_param_type_char(type_text);
    }

    uint32_t written = 0;
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;
        if( strcmp(key, "type") == 0 )
        {
            /* Both spellings: the machine export writes `i`, content writes
             * `int`. See cp_param_type_char for why an unknown name is refused
             * rather than defaulted. */
            entry.type = cp_param_type_char(value);
            ok = entry.type != 0;
        }
        else if( strcmp(key, "default") == 0 )
        {
            /*
             * The authored grammar (LostCity's lookupParamValue), which keys the
             * whole reading on the declared type — which is why this runs after
             * it:
             *
             * `[death_drop] type=namedobj default=bones` is obj 526, and the only
             * thing that says so is the type — which is why this runs after it.
             * Falling back to `cp_parse_int` would turn `bones` into a failure and
             * `0` into obj 0, and those are different mistakes.
             *
             * Three spellings below are the author's rather than the exporter's.
             * The machine export writes `default=` only for the *int* slot and
             * puts a string in `defaultstr=`, so each of these used to fail the
             * whole record — 20 of them in this tree, and a param that fails to
             * encode is a param the cache does not carry at all.
             */
            int ref = cp_param_ref_type(entry.type);

            if( entry.type == 's' )
            {
                /*
                 * A string param's default is a string, and `default=` is how a
                 * person writes it. Routed to the string slot (opcode 5) because
                 * an *integer* default on a string type has no meaning — so there
                 * is nothing for this to be ambiguous with. Export still writes
                 * `defaultstr=`; both spellings read back the same.
                 */
                char buf[4096];
                cp_unescape(value, buf, sizeof(buf));
                free(entry.default_string);
                entry.default_string = strdup(buf);
                ok = entry.default_string != NULL;
            }
            else if( strcmp(value, "null") == 0 )
            {
                /*
                 * "no value" — see cp_resolve_ref_or_null for why it is -1.
                 *
                 * Accepted on any non-string type, not only a reference one:
                 * `[gnome_cooking_type] type=int default=null` is in this tree,
                 * and the sentinel means the same thing there. The string case is
                 * already handled above, where `null` would be a literal string.
                 */
                entry.default_int = -1;
            }
            else if( entry.type == '1' )
            {
                /* `yes`/`no` as well as 0/1 — the same two spellings autodisable
                 * below already accepts, and what content authors write. */
                bool flag = false;
                if( cp_parse_bool(value, &flag) )
                    entry.default_int = flag ? 1 : 0;
                else
                    ok = cp_parse_int(value, &entry.default_int);
            }
            else if( ref >= 0 && !cp_parse_int(value, &entry.default_int) )
                ok = cp_resolve_ref(ctx, (enum CP_TypeId)ref, value, &entry.default_int);
            else
                ok = cp_parse_int(value, &entry.default_int);
        }
        else if( strcmp(key, "defaultlong") == 0 )
        {
            int64_t tmp = 0;
            ok = cp_parse_i64(value, &tmp);
            entry.default_long = (long long)tmp;
        }
        else if( strcmp(key, "defaultstr") == 0 )
        {
            char buf[4096];
            cp_unescape(value, buf, sizeof(buf));
            free(entry.default_string);
            entry.default_string = strdup(buf);
        }
        else if( strcmp(key, "autodisable") == 0 )
        {
            bool flag = false;
            ok = cp_parse_bool(value, &flag);
            entry.auto_disable = flag ? 1 : 0;
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
    cp_emit_params(ctx, out, &entry.params);
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
    RSCache_Dat2ConfigStructDecodeInplace(&entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    uint32_t written = 0;
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        if( strcmp(key, "param") == 0 )
        {
            if( !cp_parse_param(ctx, &entry.params, config->lines[i].value) )
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

    /* The keys keep their opcode numbers even though the fields no longer do:
     * they are the on-disk format of every unpacked `.healthbar` in the content
     * tree, and renaming them would strand those files. */
    if( entry.has_draw_order )
        cp_lines_addf(out, "opcode2=%d", entry.draw_order);
    if( entry.has_evict_priority )
        cp_lines_addf(out, "opcode3=%d", entry.evict_priority);
    if( entry.has_persist_cycles )
        cp_lines_addf(out, "opcode5=%d", entry.persist_cycles);
    if( entry.front_sprite_id >= 0 )
        cp_lines_addf(out, "sprite_a=%d", entry.front_sprite_id);
    if( entry.back_sprite_id >= 0 )
        cp_lines_addf(out, "sprite_b=%d", entry.back_sprite_id);
    if( entry.has_fade_threshold )
        cp_lines_addf(out, "opcode11=%d", entry.fade_threshold);
    if( entry.has_width )
        cp_lines_addf(out, "opcode14=%d", entry.width);
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
    RSCache_Dat2ConfigHealthbarDecodeInplace(
        &entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;
        if( strcmp(key, "opcode2") == 0 )
        {
            ok = cp_parse_int(value, &entry.draw_order);
            entry.has_draw_order = true;
        }
        else if( strcmp(key, "opcode3") == 0 )
        {
            ok = cp_parse_int(value, &entry.evict_priority);
            entry.has_evict_priority = true;
        }
        else if( strcmp(key, "opcode5") == 0 )
        {
            ok = cp_parse_int(value, &entry.persist_cycles);
            entry.has_persist_cycles = true;
        }
        else if( strcmp(key, "sprite_a") == 0 )
            ok = cp_parse_int(value, &entry.front_sprite_id);
        else if( strcmp(key, "sprite_b") == 0 )
            ok = cp_parse_int(value, &entry.back_sprite_id);
        else if( strcmp(key, "opcode11") == 0 )
        {
            ok = cp_parse_int(value, &entry.fade_threshold);
            entry.has_fade_threshold = true;
        }
        else if( strcmp(key, "opcode14") == 0 )
        {
            ok = cp_parse_int(value, &entry.width);
            entry.has_width = true;
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "healthbar [%s]: unknown key %s",
                    config->debugname, key);
        if( !ok )
        {
            fprintf(stderr, "cachepack: healthbar [%s]: bad value for %s\n",
                    config->debugname, key);
            return 0;
        }
    }
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

/** `key=<asset name>` unless `id` is absent. Falls back to the id, as refs do. */
static void
hitsplat_emit_asset(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const char* key,
    enum CP_AssetId asset,
    int id)
{
    if( id < 0 )
        return;
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
 * of the two the record asks, and a -1 variant means "draw no splat at all". A
 * dropped line would read as "this record does not have one".
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
    cp_emit_ref(ctx, out, key, type, id, -1);
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
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigHitsplatDecodeInplace(
        &entry, record, record_size,
        (unsigned)RSCache_Dat2ConfigHitsplatFlags(ctx ? &ctx->profile : NULL));
    if( entry._consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "hitsplat %d: consumed %d of %d bytes", id,
                entry._consumed, record_size);

    /* The four sprites in the order the renderer lays them out, left to right:
     * icon, left cap, the tiled body, right cap. See `dat2_config_hitsplat.h`. */
    hitsplat_emit_asset(ctx, out, "iconsprite", CP_ASSET_SPRITE,
                        entry.has_icon_sprite ? entry.icon_sprite_id : -1);
    hitsplat_emit_asset(ctx, out, "leftsprite", CP_ASSET_SPRITE,
                        entry.has_left_sprite ? entry.left_sprite_id : -1);
    hitsplat_emit_asset(ctx, out, "sprite", CP_ASSET_SPRITE, entry.sprite_id);
    hitsplat_emit_asset(ctx, out, "rightsprite", CP_ASSET_SPRITE,
                        entry.has_right_sprite ? entry.right_sprite_id : -1);

    hitsplat_emit_asset(ctx, out, "font", CP_ASSET_FONT, entry.has_font ? entry.font_id : -1);
    /* Hex because a colour read as 16711680 is a number and one read as 0xFF0000
     * is a colour. `cp_parse_int` takes either. */
    if( entry.has_text_colour )
        cp_lines_addf(out, "textcolour=0x%06X", entry.text_colour & 0xFFFFFF);
    /* Opcode 8 is a string, not the u16 this tool used to emit — see
     * `dat2_config_hitsplat.h`. The marker byte rides with it because it is what
     * makes the repack byte-identical, and it is 0 on every record measured. */
    if( entry.has_text )
    {
        cp_lines_addf(out, "text=%s", entry.text);
        if( entry.text_marker != 0 )
            cp_lines_addf(out, "textmarker=%d", entry.text_marker);
    }
    if( entry.has_text_offset_y )
        cp_lines_addf(out, "texty=%d", entry.text_offset_y);
    if( entry.has_duration )
        cp_lines_addf(out, "duration=%d", entry.duration);
    if( entry.has_drift_x )
        cp_lines_addf(out, "driftx=%d", entry.drift_x);
    if( entry.has_drift_up )
        cp_lines_addf(out, "driftup=%d", entry.drift_up);
    /* Opcode 11 is `fadeafter=0` written as one byte; the two spellings are kept
     * apart so the record re-encodes to the byte it carried. */
    if( entry.has_fade_flag )
        cp_lines_addf(out, "fade=yes");
    if( entry.has_fade_after )
        cp_lines_addf(out, "fadeafter=%d", entry.fade_after);
    if( entry.has_slot_policy )
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
    if( entry.variant_opcode )
    {
        /* Opcode 17/18's payload IS structured — the reference reads
         * `u16, u16, [u16], u8 count, u16[count+1]`. It used to be emitted as
         * eleven opaque hex bytes because that structure was only guessed at, and
         * then as three bare ids because nothing said what they were: a varbit,
         * a varp and a fallback splat.
         *
         * `variantdefault` is the fallback AND the opcode: 18 reads one, 17 does
         * not, so the line's presence is what states which opcode to write. Both
         * var lines are always emitted, `null` included, so a selector record can
         * be read without knowing which of the two carries the question. */
        char list[RSCACHE_HITSPLAT_MAX_VARIANTS * 96];
        size_t used = 0;
        int ok = 1;

        list[0] = '\0';
        for( int i = 0; i < entry.variant_count; i++ )
        {
            const char* name =
                entry.variants[i] < 0 ? "null"
                                      : cp_name_ensure(ctx, CP_TYPE_HITSPLAT, entry.variants[i]);
            char number[16];
            if( !name )
            {
                snprintf(number, sizeof(number), "%d", entry.variants[i]);
                name = number;
            }
            if( !hitsplat_list_append(list, sizeof(list), &used, name) )
            {
                ok = 0;
                break;
            }
        }
        if( !ok )
            cp_warn(ctx, &ctx->warn_short_decode,
                    "hitsplat %d: %d variant names do not fit one line", id,
                    entry.variant_count);

        hitsplat_emit_ref_or_null(ctx, out, "variantvarbit", CP_TYPE_VARBIT, entry.variant_varbit);
        hitsplat_emit_ref_or_null(ctx, out, "variantvarp", CP_TYPE_VARP, entry.variant_varp);
        if( entry.variant_opcode == 18 )
            hitsplat_emit_ref_or_null(ctx, out, "variantdefault", CP_TYPE_HITSPLAT,
                                      entry.variant_fallback);
        cp_lines_addf(out, "variants=%s", list);
    }

    /*
     * Records do not share a packing order and the encoder replays whatever it is
     * given, so the order is data. Without it a repack is still a valid hitsplat,
     * just not the same bytes. Written in the field names rather than the opcode
     * numbers, so the line says which fields the record carries.
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
            if( !key )
            {
                snprintf(number, sizeof(number), "%d", entry.opcodes[i]);
                key = number;
            }
            if( !hitsplat_list_append(order, sizeof(order), &used, key) )
                break;
        }
        cp_lines_addf(out, "opcodeorder=%s", order);
    }
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
    RSCache_Dat2ConfigHitsplatDecodeInplace(
        &entry, cp_empty_record, (int)sizeof(cp_empty_record),
        RSCACHE_CONFIG_HITSPLAT_DECODE_OSRS);
    entry.id = id;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;
        if( strcmp(key, "sprite") == 0 )
            ok = hitsplat_resolve_asset(ctx, CP_ASSET_SPRITE, value, &entry.sprite_id);
        else if( strcmp(key, "iconsprite") == 0 )
        {
            ok = hitsplat_resolve_asset(ctx, CP_ASSET_SPRITE, value, &entry.icon_sprite_id);
            entry.has_icon_sprite = true;
        }
        else if( strcmp(key, "leftsprite") == 0 )
        {
            ok = hitsplat_resolve_asset(ctx, CP_ASSET_SPRITE, value, &entry.left_sprite_id);
            entry.has_left_sprite = true;
        }
        else if( strcmp(key, "rightsprite") == 0 )
        {
            ok = hitsplat_resolve_asset(ctx, CP_ASSET_SPRITE, value, &entry.right_sprite_id);
            entry.has_right_sprite = true;
        }
        else if( strcmp(key, "font") == 0 )
        {
            ok = hitsplat_resolve_asset(ctx, CP_ASSET_FONT, value, &entry.font_id);
            entry.has_font = true;
        }
        else if( strcmp(key, "textcolour") == 0 )
        {
            ok = cp_parse_int(value, &entry.text_colour);
            entry.has_text_colour = true;
        }
        else if( strcmp(key, "text") == 0 )
        {
            if( strlen(value) >= RSCACHE_HITSPLAT_MAX_TEXT )
                ok = 0;
            else
            {
                snprintf(entry.text, sizeof(entry.text), "%s", value);
                entry.has_text = true;
            }
        }
        else if( strcmp(key, "textmarker") == 0 )
        {
            int marker = 0;
            ok = cp_parse_int(value, &marker);
            entry.text_marker = (uint8_t)marker;
        }
        else if( strcmp(key, "texty") == 0 )
        {
            ok = cp_parse_int(value, &entry.text_offset_y);
            entry.has_text_offset_y = true;
        }
        else if( strcmp(key, "duration") == 0 )
        {
            ok = cp_parse_int(value, &entry.duration);
            entry.has_duration = true;
        }
        else if( strcmp(key, "driftx") == 0 )
        {
            ok = cp_parse_int(value, &entry.drift_x);
            entry.has_drift_x = true;
        }
        else if( strcmp(key, "driftup") == 0 )
        {
            ok = cp_parse_int(value, &entry.drift_up);
            entry.has_drift_up = true;
        }
        else if( strcmp(key, "fade") == 0 )
        {
            bool flag = false;
            ok = cp_parse_bool(value, &flag);
            entry.has_fade_flag = flag;
            if( flag )
                entry.fade_after = 0;
        }
        else if( strcmp(key, "fadeafter") == 0 )
        {
            ok = cp_parse_int(value, &entry.fade_after);
            entry.has_fade_after = true;
        }
        else if( strcmp(key, "slotpolicy") == 0 )
        {
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
            entry.has_slot_policy = true;
        }
        else if( strcmp(key, "variantvarbit") == 0 )
            ok = cp_resolve_ref_or_null(ctx, CP_TYPE_VARBIT, value, &entry.variant_varbit);
        else if( strcmp(key, "variantvarp") == 0 )
            ok = cp_resolve_ref_or_null(ctx, CP_TYPE_VARP, value, &entry.variant_varp);
        /* The fallback is opcode 18's alone, so stating one is how a record says
         * it is an 18. A 17 states no `variantdefault` line and keeps -1, which
         * the reference reads as "draw no splat" — a real answer, not a gap. */
        else if( strcmp(key, "variantdefault") == 0 )
        {
            ok = cp_resolve_ref_or_null(ctx, CP_TYPE_HITSPLAT, value, &entry.variant_fallback);
            entry.variant_opcode = 18;
        }
        else if( strcmp(key, "variants") == 0 )
        {
            char scratch[RSCACHE_HITSPLAT_MAX_VARIANTS * 96];
            char* fields[RSCACHE_HITSPLAT_MAX_VARIANTS];
            if( strlen(value) >= sizeof(scratch) )
                ok = 0;
            else
            {
                int n = cp_split(value, scratch, fields, RSCACHE_HITSPLAT_MAX_VARIANTS);
                entry.variant_count = 0;
                for( int f = 0; f < n && ok; f++ )
                    ok = cp_resolve_ref_or_null(ctx, CP_TYPE_HITSPLAT, fields[f],
                                                &entry.variants[entry.variant_count++]);
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

    /* A selector with no `variantdefault` is an opcode 17. Settled after the loop
     * because the two lines that state it can arrive in either order, and so can
     * `opcodeorder`, whose `variants` entry is written back here for the same
     * reason: the key names one field and the record picks the opcode. */
    if( entry.variant_count > 0 && entry.variant_opcode == 0 )
        entry.variant_opcode = 17;
    for( int i = 0; i < entry.opcode_count; i++ )
        if( entry.opcodes[i] == 17 || entry.opcodes[i] == 18 )
            entry.opcodes[i] = (uint8_t)(entry.variant_opcode ? entry.variant_opcode : 18);

    return RSCache_Dat2ConfigHitsplatEncode(&entry, out, out_capacity);
}

/* ---- mapelement --------------------------------------------------------- */

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
    RSCache_MapElementDecodeInplace(&entry, record, record_size);

    if( entry.name )
        cp_lines_add_str(out, "name", entry.name);
    if( entry.sprite_id )
        cp_lines_addf(out, "sprite=%d", entry.sprite_id);
    if( entry.text_size )
        cp_lines_addf(out, "textsize=%d", entry.text_size);
    if( entry.category )
        cp_lines_addf(out, "category=%d", entry.category);

    RSCache_MapElementFreeInplace(&entry);
    return 1;
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
    memset(&entry, 0, sizeof(entry));
    entry.id = id;

    uint32_t written = 0;
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;
        if( strcmp(key, "name") == 0 )
        {
            char buf[1024];
            cp_unescape(value, buf, sizeof(buf));
            free(entry.name);
            entry.name = strdup(buf);
        }
        else if( strcmp(key, "sprite") == 0 )
            ok = cp_parse_int(value, &entry.sprite_id);
        else if( strcmp(key, "textsize") == 0 )
            ok = cp_parse_int(value, &entry.text_size);
        else if( strcmp(key, "category") == 0 )
            ok = cp_parse_int(value, &entry.category);
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
    written = RSCache_MapElementEncode(&entry, out, out_capacity);
done:
    RSCache_MapElementFreeInplace(&entry);
    return written;
}
