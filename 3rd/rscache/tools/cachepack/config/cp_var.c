#include "cachepack.h"

#include "datatypes/dat2_config_var.h"

#include <assert.h>
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
 *
 * Every key is written for every record, `key=default` when the stream does not
 * state it, straight from the decoder's presence bits. A varbit's opcode 1 carries
 * three keys (`basevar`, `startbit`, `endbit`); they are stated together or not at
 * all, and the packer refuses a block that states some of them.
 */

/* A `yes` flag key: the opcode carries no payload, so its presence is the value.
 * `no` would be a third state the stream cannot express. */
static int
parse_flag(const char* value)
{
    return strcmp(value, "yes") == 0;
}

/* ---- varbit ------------------------------------------------------------- */

/* In the order they are written. basevar/startbit/endbit are RSCACHE_VARBIT_FIELD_BITS. */
const struct CP_KeySpec cp_varbit_keys[] = {
    { "basevar", 0, NULL },
    { "startbit", 0, NULL },
    { "endbit", 0, NULL },
    { "debugname", 0, NULL },
    { NULL, 0, NULL },
};

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

    if( RSCache_PresenceHas(&entry.present, RSCACHE_VARBIT_FIELD_BITS) )
    {
        cp_emit_name(ctx, out, "basevar", CP_TYPE_VARP, entry.basevar);
        cp_lines_addf(out, "startbit=%d", entry.startbit);
        cp_lines_addf(out, "endbit=%d", entry.endbit);
    }
    else
    {
        cp_lines_add_default(out, "basevar");
        cp_lines_add_default(out, "startbit");
        cp_lines_add_default(out, "endbit");
    }
    if( RSCache_PresenceHas(&entry.present, RSCACHE_VARBIT_FIELD_DEBUGNAME) )
        cp_lines_add_str(out, "debugname", entry.debugname);
    else
        cp_lines_add_default(out, "debugname");

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
    /* The empty-record decode gives the client defaults and no presence: a field
     * is written only if a line below states it. */
    RSCache_Dat2ConfigVarbitDecodeInplace(&entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    /* Opcode 1 is three keys; count how many of them the block states. */
    int bits_stated = 0;
    uint32_t written = 0;
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;

        if( cp_value_is_default(value) )
            continue;

        if( strcmp(key, "basevar") == 0 )
        {
            bits_stated++;
            ok = cp_resolve_ref(ctx, CP_TYPE_VARP, value, &entry.basevar) &&
                 entry.basevar >= 0 && entry.basevar <= 0xFFFF;
        }
        else if( strcmp(key, "startbit") == 0 )
        {
            bits_stated++;
            ok = cp_parse_int(value, &entry.startbit) && entry.startbit >= 0 &&
                 entry.startbit <= 255;
        }
        else if( strcmp(key, "endbit") == 0 )
        {
            bits_stated++;
            ok = cp_parse_int(value, &entry.endbit) && entry.endbit >= 0 && entry.endbit <= 255;
        }
        else if( strcmp(key, "debugname") == 0 )
        {
            char buf[1024];
            RSCache_PresenceSet(&entry.present, RSCACHE_VARBIT_FIELD_DEBUGNAME);
            cp_unescape(value, buf, sizeof(buf));
            free(entry.debugname);
            entry.debugname = strdup(buf);
            assert(entry.debugname);
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
    if( bits_stated != 0 && bits_stated != 3 )
    {
        fprintf(stderr,
                "cachepack: varbit [%s]: basevar, startbit and endbit are one opcode — "
                "state all three or none\n",
                config->debugname);
        goto done;
    }
    if( bits_stated == 3 )
        RSCache_PresenceSet(&entry.present, RSCACHE_VARBIT_FIELD_BITS);
    written = RSCache_Dat2ConfigVarbitEncode(&entry, out, out_capacity);
done:
    RSCache_Dat2ConfigVarbitFreeInplace(&entry);
    return written;
}

/* ---- varplayer ---------------------------------------------------------- */

const struct CP_KeySpec cp_varp_keys[] = {
    { "clientcode", 0, NULL },
    { NULL, 0, NULL },
};

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

    if( RSCache_PresenceHas(&entry.present, RSCACHE_VARP_FIELD_CLIENTCODE) )
        cp_lines_addf(out, "clientcode=%d", entry.clientcode);
    else
        cp_lines_add_default(out, "clientcode");
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

        if( cp_value_is_default(value) )
            continue;

        if( strcmp(key, "clientcode") == 0 )
        {
            RSCache_PresenceSet(&entry.present, RSCACHE_VARP_FIELD_CLIENTCODE);
            if( !cp_parse_int(value, &entry.clientcode) || entry.clientcode < 0 ||
                entry.clientcode > 0xFFFF )
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

/*
 * `type` is text-only: the script compiler reads it (`type=string` on the string
 * varcs, which the content tree states by hand) and no opcode carries it. It is
 * still a key of the type, so the unpacker states it -- always `type=default`,
 * because the cache cannot say otherwise, and the default is int.
 */
const struct CP_KeySpec cp_varc_keys[] = {
    { "persist", 0, NULL },
    { "opcode3", 0, NULL },
    { "type", 0, NULL },
    { NULL, 0, NULL },
};

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

    if( RSCache_PresenceHas(&entry.present, RSCACHE_VARC_FIELD_PERSIST) )
        cp_lines_addf(out, "persist=yes");
    else
        cp_lines_add_default(out, "persist");
    /* Written under its opcode number because the library does not claim to know
     * what it means; renaming it here would invent knowledge the format has not
     * given up. */
    if( RSCache_PresenceHas(&entry.present, RSCACHE_VARC_FIELD_OPCODE_3) )
        cp_lines_addf(out, "opcode3=%d", entry.opcode_3);
    else
        cp_lines_add_default(out, "opcode3");
    cp_lines_add_default(out, "type");
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

        if( cp_value_is_default(value) )
            continue;

        if( strcmp(key, "persist") == 0 )
        {
            RSCache_PresenceSet(&entry.present, RSCACHE_VARC_FIELD_PERSIST);
            ok = parse_flag(value);
            entry.persist = 1;
        }
        else if( strcmp(key, "opcode3") == 0 )
        {
            RSCache_PresenceSet(&entry.present, RSCACHE_VARC_FIELD_OPCODE_3);
            ok = cp_parse_int(value, &entry.opcode_3) && entry.opcode_3 >= 0 &&
                 entry.opcode_3 <= 0xFFFF;
        }
        /* Source-only: which script opcodes the varc takes (`%name` compiles to
         * the varc-string or varc-int push by it). The config encodes nothing
         * for it — see cs2_seed_varc_names. */
        else if( strcmp(key, "type") == 0 )
            ok = strcmp(value, "int") == 0 || strcmp(value, "string") == 0;
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
