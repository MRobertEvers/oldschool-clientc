#include "cachepack.h"

#include "datatypes/dat2_config_var.h"

#include <stdlib.h>
#include <string.h>

/*
 * The three variable families: varbit (group 14), varplayer (16) and varclient (19).
 *
 * Rev 254 has varp and varbit; varclient is new here, and so is the fact that a
 * varbit's base is a named varp rather than a number — the gameval index names both
 * sides, so `basevar=` reads as the varp it packs into.
 *
 * All three are fixed-shape records the library encodes byte-exactly.
 */

/* ---- varbit ------------------------------------------------------------- */

int
cp_unpack_varbit(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigVarbit entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigVarbitDecodeInplace(&entry, record, record_size);
    if( entry._consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "varbit %d: consumed %d of %d bytes", id,
                entry._consumed, record_size);

    cp_emit_ref(ctx, out, "basevar", CP_TYPE_VARP, entry.basevar, -1);
    cp_lines_addf(out, "startbit=%d", entry.startbit);
    cp_lines_addf(out, "endbit=%d", entry.endbit);
    if( entry.debugname )
        cp_lines_add_str(out, "debugname", entry.debugname);

    RSCache_Dat2ConfigVarbitFreeInplace(&entry);
    return 1;
}

uint32_t
cp_pack_varbit(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigVarbit entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigVarbitDecodeInplace(&entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    uint32_t written = 0;
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;
        if( strcmp(key, "basevar") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_VARP, value, &entry.basevar);
        else if( strcmp(key, "startbit") == 0 )
            ok = cp_parse_int(value, &entry.startbit);
        else if( strcmp(key, "endbit") == 0 )
            ok = cp_parse_int(value, &entry.endbit);
        else if( strcmp(key, "debugname") == 0 )
        {
            char buf[1024];
            cp_unescape(value, buf, sizeof(buf));
            free(entry.debugname);
            entry.debugname = strdup(buf);
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "varbit [%s]: unknown key %s",
                    config->debugname, key);
        if( !ok )
        {
            fprintf(stderr, "cachepack: varbit [%s]: bad value for %s\n", config->debugname, key);
            goto done;
        }
    }
    written = RSCache_Dat2ConfigVarbitEncode(&entry, out, out_capacity);
done:
    RSCache_Dat2ConfigVarbitFreeInplace(&entry);
    return written;
}

/* ---- varplayer ---------------------------------------------------------- */

int
cp_unpack_varp(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigVarplayer entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigVarplayerDecodeInplace(&entry, record, record_size);
    if( entry._consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "varp %d: consumed %d of %d bytes", id,
                entry._consumed, record_size);

    if( entry.clientcode )
        cp_lines_addf(out, "clientcode=%d", entry.clientcode);
    return 1;
}

uint32_t
cp_pack_varp(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigVarplayer entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigVarplayerDecodeInplace(
        &entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        if( strcmp(key, "clientcode") == 0 )
        {
            if( !cp_parse_int(value, &entry.clientcode) )
            {
                fprintf(stderr, "cachepack: varp [%s]: bad clientcode\n", config->debugname);
                return 0;
            }
        }
        else
        {
            cp_warn(ctx, &ctx->warn_unknown_key, "varp [%s]: unknown key %s",
                    config->debugname, key);
        }
    }
    return RSCache_Dat2ConfigVarplayerEncode(&entry, out, out_capacity);
}

/* ---- varclient ---------------------------------------------------------- */

int
cp_unpack_varc(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigVarclient entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigVarclientDecodeInplace(&entry, record, record_size);
    if( entry._consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "varc %d: consumed %d of %d bytes", id,
                entry._consumed, record_size);

    if( entry.persist )
        cp_lines_addf(out, "persist=yes");
    /* Written under its opcode number because the library does not claim to know
     * what it means; renaming it here would invent knowledge the format has not
     * given up. */
    if( entry.has_opcode_3 )
        cp_lines_addf(out, "opcode3=%d", entry.opcode_3);
    return 1;
}

uint32_t
cp_pack_varc(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigVarclient entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigVarclientDecodeInplace(
        &entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;
        if( strcmp(key, "persist") == 0 )
        {
            bool flag = false;
            ok = cp_parse_bool(value, &flag);
            entry.persist = flag ? 1 : 0;
        }
        else if( strcmp(key, "opcode3") == 0 )
        {
            ok = cp_parse_int(value, &entry.opcode_3);
            entry.has_opcode_3 = true;
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "varc [%s]: unknown key %s",
                    config->debugname, key);
        if( !ok )
        {
            fprintf(stderr, "cachepack: varc [%s]: bad value for %s\n", config->debugname, key);
            return 0;
        }
    }
    return RSCache_Dat2ConfigVarclientEncode(&entry, out, out_capacity);
}
