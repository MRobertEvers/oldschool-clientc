#include "cachepack.h"

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

    /* Only opcodes 7 and 8 have an established meaning (a sprite pair); the rest
     * keep their opcode numbers, as the library does, rather than take invented
     * names that would read as knowledge. */
    if( entry.has_opcode_2 )
        cp_lines_addf(out, "opcode2=%d", entry.opcode_2);
    if( entry.has_opcode_3 )
        cp_lines_addf(out, "opcode3=%d", entry.opcode_3);
    if( entry.has_opcode_5 )
        cp_lines_addf(out, "opcode5=%d", entry.opcode_5);
    if( entry.sprite_id_a >= 0 )
        cp_lines_addf(out, "sprite_a=%d", entry.sprite_id_a);
    if( entry.sprite_id_b >= 0 )
        cp_lines_addf(out, "sprite_b=%d", entry.sprite_id_b);
    if( entry.has_opcode_11 )
        cp_lines_addf(out, "opcode11=%d", entry.opcode_11);
    if( entry.has_opcode_14 )
        cp_lines_addf(out, "opcode14=%d", entry.opcode_14);
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
            ok = cp_parse_int(value, &entry.opcode_2);
            entry.has_opcode_2 = true;
        }
        else if( strcmp(key, "opcode3") == 0 )
        {
            ok = cp_parse_int(value, &entry.opcode_3);
            entry.has_opcode_3 = true;
        }
        else if( strcmp(key, "opcode5") == 0 )
        {
            ok = cp_parse_int(value, &entry.opcode_5);
            entry.has_opcode_5 = true;
        }
        else if( strcmp(key, "sprite_a") == 0 )
            ok = cp_parse_int(value, &entry.sprite_id_a);
        else if( strcmp(key, "sprite_b") == 0 )
            ok = cp_parse_int(value, &entry.sprite_id_b);
        else if( strcmp(key, "opcode11") == 0 )
        {
            ok = cp_parse_int(value, &entry.opcode_11);
            entry.has_opcode_11 = true;
        }
        else if( strcmp(key, "opcode14") == 0 )
        {
            ok = cp_parse_int(value, &entry.opcode_14);
            entry.has_opcode_14 = true;
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

    if( entry.sprite_id >= 0 )
        cp_lines_addf(out, "sprite=%d", entry.sprite_id);
    if( entry.has_opcode_1 )
        cp_lines_addf(out, "opcode1=%d", entry.opcode_1);
    if( entry.has_colour )
        cp_lines_addf(out, "colour=%d", entry.colour);
    if( entry.has_opcode_3 )
        cp_lines_addf(out, "opcode3=%d", entry.opcode_3);
    if( entry.has_opcode_4 )
        cp_lines_addf(out, "opcode4=%d", entry.opcode_4);
    if( entry.has_opcode_6 )
        cp_lines_addf(out, "opcode6=%d", entry.opcode_6);
    if( entry.has_opcode_7 )
        cp_lines_addf(out, "opcode7=%d", entry.opcode_7);
    /* Opcode 8 is a string, not the u16 this tool used to emit — see
     * `dat2_config_hitsplat.h`. The marker byte rides with it because it is what
     * makes the repack byte-identical, and it is 0 on every record measured. */
    if( entry.has_text )
    {
        cp_lines_addf(out, "text=%s", entry.text);
        if( entry.text_marker != 0 )
            cp_lines_addf(out, "textmarker=%d", entry.text_marker);
    }
    if( entry.has_duration )
        cp_lines_addf(out, "duration=%d", entry.duration);
    if( entry.has_opcode_10 )
        cp_lines_addf(out, "opcode10=%d", entry.opcode_10);
    if( entry.has_opcode_11_flag )
        cp_lines_addf(out, "opcode11=yes");
    if( entry.has_slot_policy )
        cp_lines_addf(out, "slotpolicy=%d", entry.slot_policy);
    if( entry.has_opcode_13 )
        cp_lines_addf(out, "opcode13=%d", entry.opcode_13);
    if( entry.has_opcode_14 )
        cp_lines_addf(out, "opcode14=%d", entry.opcode_11_14);
    if( entry.variant_opcode )
    {
        /* Opcode 17/18's payload IS structured — the reference reads
         * `u16, u16, [u16], u8 count, u16[count+1]`. It used to be emitted as
         * eleven opaque hex bytes because that structure was only guessed at. */
        char list[RSCACHE_HITSPLAT_MAX_VARIANTS * 7 + 1];
        int w = 0;
        for( int i = 0; i < entry.variant_count; i++ )
            w += snprintf(list + w, sizeof(list) - (size_t)w, i ? ",%d" : "%d",
                          entry.variants[i]);
        cp_lines_addf(out, "variantop=%d", entry.variant_opcode);
        cp_lines_addf(out, "variantabc=%d,%d,%d", entry.variant_a, entry.variant_b,
                      entry.variant_c);
        cp_lines_addf(out, "variants=%s", list);
    }

    /*
     * Records do not share a packing order and the encoder replays whatever it is
     * given, so the order is data. Without it a repack is still a valid hitsplat,
     * just not the same bytes.
     */
    if( entry.opcode_count > 0 )
    {
        char order[RSCACHE_HITSPLAT_MAX_OPCODES * 4 + 1];
        int w = 0;
        for( int i = 0; i < entry.opcode_count; i++ )
            w += snprintf(order + w, sizeof(order) - (size_t)w, i ? ",%d" : "%d",
                          entry.opcodes[i]);
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
            ok = cp_parse_int(value, &entry.sprite_id);
        else if( strcmp(key, "opcode1") == 0 )
        {
            ok = cp_parse_int(value, &entry.opcode_1);
            entry.has_opcode_1 = true;
        }
        else if( strcmp(key, "colour") == 0 )
        {
            ok = cp_parse_int(value, &entry.colour);
            entry.has_colour = true;
        }
        else if( strcmp(key, "opcode3") == 0 )
        {
            ok = cp_parse_int(value, &entry.opcode_3);
            entry.has_opcode_3 = true;
        }
        else if( strcmp(key, "opcode4") == 0 )
        {
            ok = cp_parse_int(value, &entry.opcode_4);
            entry.has_opcode_4 = true;
        }
        else if( strcmp(key, "opcode6") == 0 )
        {
            ok = cp_parse_int(value, &entry.opcode_6);
            entry.has_opcode_6 = true;
        }
        else if( strcmp(key, "opcode7") == 0 )
        {
            ok = cp_parse_int(value, &entry.opcode_7);
            entry.has_opcode_7 = true;
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
        else if( strcmp(key, "duration") == 0 )
        {
            ok = cp_parse_int(value, &entry.duration);
            entry.has_duration = true;
        }
        else if( strcmp(key, "opcode10") == 0 )
        {
            ok = cp_parse_int(value, &entry.opcode_10);
            entry.has_opcode_10 = true;
        }
        else if( strcmp(key, "opcode11") == 0 )
        {
            bool flag = false;
            ok = cp_parse_bool(value, &flag);
            entry.has_opcode_11_flag = flag;
            if( flag )
                entry.opcode_11_14 = 0;
        }
        else if( strcmp(key, "slotpolicy") == 0 )
        {
            ok = cp_parse_int(value, &entry.slot_policy);
            entry.has_slot_policy = true;
        }
        else if( strcmp(key, "opcode13") == 0 )
        {
            ok = cp_parse_int(value, &entry.opcode_13);
            entry.has_opcode_13 = true;
        }
        else if( strcmp(key, "opcode14") == 0 )
        {
            ok = cp_parse_int(value, &entry.opcode_11_14);
            entry.has_opcode_14 = true;
        }
        else if( strcmp(key, "variantop") == 0 )
            ok = cp_parse_int(value, &entry.variant_opcode);
        else if( strcmp(key, "variantabc") == 0 )
        {
            char scratch[64];
            char* fields[3];
            if( strlen(value) >= sizeof(scratch) )
                ok = 0;
            else if( cp_split(value, scratch, fields, 3) != 3 )
                ok = 0;
            else
                ok = cp_parse_int(fields[0], &entry.variant_a) &&
                     cp_parse_int(fields[1], &entry.variant_b) &&
                     cp_parse_int(fields[2], &entry.variant_c);
        }
        else if( strcmp(key, "variants") == 0 )
        {
            char scratch[RSCACHE_HITSPLAT_MAX_VARIANTS * 8];
            char* fields[RSCACHE_HITSPLAT_MAX_VARIANTS];
            if( strlen(value) >= sizeof(scratch) )
                ok = 0;
            else
            {
                int n = cp_split(value, scratch, fields, RSCACHE_HITSPLAT_MAX_VARIANTS);
                entry.variant_count = 0;
                for( int f = 0; f < n && ok; f++ )
                    ok = cp_parse_int(fields[f], &entry.variants[entry.variant_count++]);
            }
        }
        else if( strcmp(key, "opcodeorder") == 0 )
        {
            char scratch[256];
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
                    int op = 0;
                    ok = cp_parse_int(fields[f], &op);
                    entry.opcodes[entry.opcode_count++] = (uint8_t)op;
                }
            }
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "hitsplat [%s]: unknown key %s",
                    config->debugname, key);
        if( !ok )
        {
            fprintf(stderr, "cachepack: hitsplat [%s]: bad value for %s\n",
                    config->debugname, key);
            return 0;
        }
    }
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
