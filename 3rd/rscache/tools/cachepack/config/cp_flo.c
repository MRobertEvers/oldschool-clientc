#include "cachepack.h"

#include "datatypes/dat2_config_flo.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * Floor configs: underlay (config group 1) and overlay (group 4).
 *
 * Rev 254 has one `.flo` type; OldSchool split it in two, which is why the two
 * live in one file here for the same reason the library's decoder does — they are
 * one family, and opcode 3 means different things on each side of the split.
 *
 * Both are tiny and fully retained by the decoder, so both round-trip byte-exactly
 * (the overlay's opcode order apart: the cache writes its fields in author order,
 * and the encoder in a fixed one).
 *
 * Every key is written for every record, `key=default` when the stream does not
 * state it, straight from the decoder's presence bits.
 *
 * The RS2 (643) shape decodes into fields the OldSchool encoder cannot write
 * (`rs2_*`, and a u16 texture); they are not in the text, and an RS2 cache does
 * not round-trip through it.
 */

/* ---- underlay ----------------------------------------------------------- */

const struct CP_KeySpec cp_underlay_keys[] = {
    { "colour", 0, NULL, NULL },
    { NULL, 0, NULL, NULL },
};

int
cp_unpack_underlay(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigUnderlay entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigUnderlayDecodeInplaceFlags(
        &entry, (char*)record, record_size, RSCache_Dat2ConfigFloFlags(&ctx->profile));

    if( RSCache_PresenceHas(&entry.present, RSCACHE_UNDERLAY_FIELD_COLOUR) )
        cp_lines_addf(out, "colour=0x%06X", entry.rgb_color & 0xFFFFFF);
    else
        cp_lines_add_default(out, "colour");
    return 1;
}

uint32_t
cp_pack_underlay(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigUnderlay entry;
    memset(&entry, 0, sizeof(entry));
    /* The empty-record decode gives the client defaults and no presence. */
    RSCache_Dat2ConfigUnderlayDecodeInplaceFlags(
        &entry,
        (char*)cp_empty_record,
        (int)sizeof(cp_empty_record),
        RSCache_Dat2ConfigFloFlags(&ctx->profile));
    entry._id = id;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;

        if( cp_value_is_default(value) )
            continue;

        if( strcmp(key, "colour") == 0 )
        {
            RSCache_PresenceSet(&entry.present, RSCACHE_UNDERLAY_FIELD_COLOUR);
            if( !cp_parse_int(value, &entry.rgb_color) )
            {
                fprintf(stderr, "cachepack: underlay [%s]: bad value for colour\n",
                        config->debugname);
                return 0;
            }
        }
        else
        {
            cp_warn(ctx, &ctx->warn_unknown_key, "underlay [%s]: unknown key %s",
                    config->debugname, key);
        }
    }
    return RSCache_Dat2ConfigUnderlayEncode(&entry, out, out_capacity);
}

/* ---- overlay ------------------------------------------------------------ */

const struct CP_KeySpec cp_overlay_keys[] = {
    { "colour", 0, NULL, NULL },
    { "texture", 0, NULL, NULL },
    { "hideunderlay", 0, NULL, NULL },
    { "flotype", 0, NULL, NULL },
    { "flotypename", 0, NULL, NULL },
    { "blendcolour", 0, NULL, NULL },
    { NULL, 0, NULL, NULL },
};

int
cp_unpack_overlay(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    const struct RSCache_Presence* has;
    struct RSCache_Dat2ConfigOverlay entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigOverlayDecodeInplaceFlags(
        &entry, (char*)record, record_size, RSCache_Dat2ConfigFloFlags(&ctx->profile));
    has = &entry.present;

    if( RSCache_PresenceHas(has, RSCACHE_OVERLAY_FIELD_COLOUR) )
        cp_lines_addf(out, "colour=0x%06X", entry.rgb_color & 0xFFFFFF);
    else
        cp_lines_add_default(out, "colour");
    if( RSCache_PresenceHas(has, RSCACHE_OVERLAY_FIELD_TEXTURE) )
        cp_lines_addf(out, "texture=%d", entry.texture);
    else
        cp_lines_add_default(out, "texture");
    /*
     * `hide_underlay` defaults to **true** and opcode 5 is what clears it, so the
     * only value the stream can state is `no`. Writing the line only when the flag
     * is true -- the reflex for a boolean -- silently dropped opcode 5 from every
     * record that carried it, and the repack hid an underlay the source showed.
     */
    if( RSCache_PresenceHas(has, RSCACHE_OVERLAY_FIELD_SHOW_UNDERLAY) )
        cp_lines_addf(out, "hideunderlay=no");
    else
        cp_lines_add_default(out, "hideunderlay");
    if( RSCache_PresenceHas(has, RSCACHE_OVERLAY_FIELD_FLOTYPE) )
        cp_lines_addf(out, "flotype=yes");
    else
        cp_lines_add_default(out, "flotype");
    if( RSCache_PresenceHas(has, RSCACHE_OVERLAY_FIELD_FLOTYPE_NAME) )
        cp_lines_add_str(out, "flotypename", entry.flotype_name);
    else
        cp_lines_add_default(out, "flotypename");
    if( RSCache_PresenceHas(has, RSCACHE_OVERLAY_FIELD_BLEND_COLOUR) )
        cp_lines_addf(out, "blendcolour=0x%06X", entry.secondary_rgb_color & 0xFFFFFF);
    else
        cp_lines_add_default(out, "blendcolour");

    RSCache_Dat2ConfigOverlayFreeInplace(&entry);
    return 1;
}

uint32_t
cp_pack_overlay(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigOverlay entry;

    /* The encoder writes the OldSchool shape only: an RS2 texture is a u16 under
     * opcode 3, which opcode 2's u8 cannot carry. Refuse rather than truncate. */
    if( RSCache_Dat2ConfigFloFlags(&ctx->profile) & RSCACHE_CONFIG_FLO_DECODE_RS2 )
    {
        fprintf(stderr, "cachepack: overlay [%s]: rscache has no RS2 overlay encoder\n",
                config->debugname);
        return 0;
    }

    memset(&entry, 0, sizeof(entry));
    /* The empty-record decode gives the client defaults and no presence: a field
     * is written only if a line below states it. */
    RSCache_Dat2ConfigOverlayDecodeInplaceFlags(
        &entry,
        (char*)cp_empty_record,
        (int)sizeof(cp_empty_record),
        RSCache_Dat2ConfigFloFlags(&ctx->profile));
    entry._id = id;

#define OVERLAY_SET(field) RSCache_PresenceSet(&entry.present, RSCACHE_OVERLAY_FIELD_##field)

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;

        if( cp_value_is_default(value) )
            continue;

        if( strcmp(key, "colour") == 0 )
        {
            OVERLAY_SET(COLOUR);
            ok = cp_parse_int(value, &entry.rgb_color);
        }
        else if( strcmp(key, "texture") == 0 )
        {
            OVERLAY_SET(TEXTURE);
            ok = cp_parse_int(value, &entry.texture) && entry.texture >= 0 &&
                 entry.texture <= 255;
        }
        else if( strcmp(key, "blendcolour") == 0 )
        {
            OVERLAY_SET(BLEND_COLOUR);
            ok = cp_parse_int(value, &entry.secondary_rgb_color);
        }
        else if( strcmp(key, "hideunderlay") == 0 )
        {
            /* Opcode 5 can only say `no`; `yes` is the default, not a statement. */
            OVERLAY_SET(SHOW_UNDERLAY);
            ok = strcmp(value, "no") == 0;
            entry.hide_underlay = false;
        }
        else if( strcmp(key, "flotype") == 0 )
        {
            OVERLAY_SET(FLOTYPE);
            ok = strcmp(value, "yes") == 0;
            entry.flotype_overlay = true;
        }
        else if( strcmp(key, "flotypename") == 0 )
        {
            char buf[512];
            OVERLAY_SET(FLOTYPE_NAME);
            cp_unescape(value, buf, sizeof(buf));
            free(entry.flotype_name);
            entry.flotype_name = strdup(buf);
            assert(entry.flotype_name);
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "overlay [%s]: unknown key %s",
                    config->debugname, key);
        if( !ok )
        {
            fprintf(stderr, "cachepack: overlay [%s]: bad value for %s\n", config->debugname, key);
            RSCache_Dat2ConfigOverlayFreeInplace(&entry);
            return 0;
        }
    }

#undef OVERLAY_SET

    uint32_t written = RSCache_Dat2ConfigOverlayEncode(&entry, out, out_capacity);
    RSCache_Dat2ConfigOverlayFreeInplace(&entry);
    return written;
}
