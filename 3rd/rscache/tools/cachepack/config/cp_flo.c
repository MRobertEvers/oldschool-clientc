#include "cachepack.h"

#include "datatypes/dat2_config_flo.h"

#include <stdlib.h>
#include <string.h>

/*
 * Floor configs: underlay (config group 1) and overlay (group 4).
 *
 * Rev 254 has one `.flo` type; OldSchool split it in two, which is why the two
 * live in one file here for the same reason the library's decoder does — they are
 * one family, and opcode 3 means different things on each side of the split.
 *
 * Both are tiny and fully retained by the decoder, so both round-trip byte-exactly.
 */

/* ---- underlay ----------------------------------------------------------- */

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

    if( entry.rgb_color )
        cp_lines_addf(out, "colour=0x%06X", entry.rgb_color & 0xFFFFFF);
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
    entry._id = id;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        if( strcmp(key, "colour") == 0 )
        {
            if( !cp_parse_int(value, &entry.rgb_color) )
                return 0;
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

int
cp_unpack_overlay(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    int flags = RSCache_Dat2ConfigFloFlags(&ctx->profile);

    struct RSCache_Dat2ConfigOverlay entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigOverlayDecodeInplaceFlags(&entry, (char*)record, record_size, flags);

    struct RSCache_Dat2ConfigOverlay defaults;
    memset(&defaults, 0, sizeof(defaults));
    RSCache_Dat2ConfigOverlayDecodeInplaceFlags(
        &defaults, (char*)cp_empty_record, (int)sizeof(cp_empty_record), flags);

    if( entry.rgb_color != defaults.rgb_color )
        cp_lines_addf(out, "colour=0x%06X", entry.rgb_color & 0xFFFFFF);
    if( entry.texture != defaults.texture )
        cp_lines_addf(out, "texture=%d", entry.texture);
    /*
     * `hide_underlay` defaults to **true** and opcode 5 is what clears it, so the
     * line has to be written when the flag is false. Writing it only when true —
     * the reflex for a boolean — silently drops opcode 5 from every record that
     * carries it, and the repack then hides an underlay the source showed.
     */
    if( entry.hide_underlay != defaults.hide_underlay )
        cp_lines_addf(out, "hideunderlay=%s", entry.hide_underlay ? "yes" : "no");
    if( entry.flotype_overlay != defaults.flotype_overlay )
        cp_lines_addf(out, "flotype=%s", entry.flotype_overlay ? "yes" : "no");
    if( entry.flotype_name )
        cp_lines_add_str(out, "flotypename", entry.flotype_name);
    if( entry.secondary_rgb_color != defaults.secondary_rgb_color )
        cp_lines_addf(out, "blendcolour=0x%06X", entry.secondary_rgb_color & 0xFFFFFF);

    RSCache_Dat2ConfigOverlayFreeInplace(&defaults);
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
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigOverlayDecodeInplaceFlags(
        &entry,
        (char*)cp_empty_record,
        (int)sizeof(cp_empty_record),
        RSCache_Dat2ConfigFloFlags(&ctx->profile));
    entry._id = id;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;
        if( strcmp(key, "colour") == 0 )
            ok = cp_parse_int(value, &entry.rgb_color);
        else if( strcmp(key, "texture") == 0 )
            ok = cp_parse_int(value, &entry.texture);
        else if( strcmp(key, "blendcolour") == 0 )
            ok = cp_parse_int(value, &entry.secondary_rgb_color);
        else if( strcmp(key, "hideunderlay") == 0 )
            ok = cp_parse_bool(value, &entry.hide_underlay);
        else if( strcmp(key, "flotype") == 0 )
            ok = cp_parse_bool(value, &entry.flotype_overlay);
        else if( strcmp(key, "flotypename") == 0 )
        {
            char buf[512];
            cp_unescape(value, buf, sizeof(buf));
            free(entry.flotype_name);
            entry.flotype_name = strdup(buf);
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
    uint32_t written = RSCache_Dat2ConfigOverlayEncode(&entry, out, out_capacity);
    RSCache_Dat2ConfigOverlayFreeInplace(&entry);
    return written;
}
