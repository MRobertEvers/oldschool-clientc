#include "dat2_config_flo.h"

#include "../rscache_profile.h"

#include <assert.h>

int
RSCache_Dat2ConfigFloCodecVersion(const struct RSCache* cache)
{
    assert(cache);
    int derived =
        RSCache_IsRs2Dat2(cache) ? RSCACHE_CODEC_FLO_RS2 : RSCACHE_CODEC_FLO_OSRS;
    /* Underlay and overlay always move together, so one slot speaks for both. */
    return RSCache_CodecVersionOr(cache, RSCACHE_TYPE_OVERLAY, derived);
}

int
RSCache_Dat2ConfigFloFlags(const struct RSCache* cache)
{
    return RSCache_Dat2ConfigFloCodecVersion(cache) == RSCACHE_CODEC_FLO_RS2
               ? RSCACHE_CONFIG_FLO_DECODE_RS2
               : 0;
}

#include "../rsbuffer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
init_overlay(struct RSCache_Dat2ConfigOverlay* overlay)
{
    memset(overlay, 0, sizeof(struct RSCache_Dat2ConfigOverlay));
    overlay->rgb_color = 0;
    overlay->texture = -1;
    overlay->secondary_rgb_color = -1;
    overlay->rs2_secondary_texture = -1;
    overlay->hide_underlay = true;

    overlay->flotype_overlay = false;
}

struct RSCache_Dat2ConfigOverlay*
RSCache_Dat2ConfigOverlayNewDecode(
    char* data,
    int data_size)
{
    struct RSCache_Dat2ConfigOverlay* overlay =
        (struct RSCache_Dat2ConfigOverlay*)malloc(sizeof(struct RSCache_Dat2ConfigOverlay));
    assert(overlay);

    RSCache_Dat2ConfigOverlayDecodeInplace(overlay, data, data_size);

    return overlay;
}

int
RSCache_Dat2ConfigOverlayDecodeInplace(
    struct RSCache_Dat2ConfigOverlay* overlay,
    char* data,
    int data_size)
{
    return RSCache_Dat2ConfigOverlayDecodeInplaceFlags(overlay, data, data_size, 0);
}

int
RSCache_Dat2ConfigOverlayDecodeInplaceFlags(
    struct RSCache_Dat2ConfigOverlay* overlay,
    char* data,
    int data_size,
    int flags)
{
    init_overlay(overlay);

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);

    while( true )
    {
        if( buffer.position >= buffer.size )
            break;

        int opcode = g1(&buffer);
        if( opcode == 0 )
            break;

        if( opcode == 1 )
        {
            int color = g3(&buffer);
            overlay->rgb_color = color;
            RSCache_PresenceSet(&overlay->present, RSCACHE_OVERLAY_FIELD_COLOUR);
        }
        else if( opcode == 2 )
        {
            int texture = g1(&buffer);
            overlay->texture = texture;
            RSCache_PresenceSet(&overlay->present, RSCACHE_OVERLAY_FIELD_TEXTURE);
        }
        else if( opcode == 3 && !(flags & RSCACHE_CONFIG_FLO_DECODE_RS2) )
        {
            overlay->flotype_overlay = true;
            RSCache_PresenceSet(&overlay->present, RSCACHE_OVERLAY_FIELD_FLOTYPE);
        }
        else if( opcode == 5 )
        {
            overlay->hide_underlay = false;
            RSCache_PresenceSet(&overlay->present, RSCACHE_OVERLAY_FIELD_SHOW_UNDERLAY);
        }
        else if( opcode == 6 && !(flags & RSCACHE_CONFIG_FLO_DECODE_RS2) )
        {
            free(overlay->flotype_name);
            overlay->flotype_name = gstringnewline(&buffer);
            RSCache_PresenceSet(&overlay->present, RSCACHE_OVERLAY_FIELD_FLOTYPE_NAME);
        }
        else if( opcode == 7 )
        {
            int secondary_color = g3(&buffer);
            overlay->secondary_rgb_color = secondary_color;
            RSCache_PresenceSet(&overlay->present, RSCACHE_OVERLAY_FIELD_BLEND_COLOUR);
        }
        else if( flags & RSCACHE_CONFIG_FLO_DECODE_RS2 )
        {
            /*
             * RS2 (643) opcodes, per void's OverlayDecoder.kt. Note opcode 3 *conflicts*
             * with the older shape: here it is a u16 texture with 65535 meaning "none",
             * where the dat1/OSRS reading treats it as a bare flag. That conflict is why
             * this needs an era flag rather than just extra cases — and it is what the
             * old unconditional assert(false) was tripping on.
             */
            switch( opcode )
            {
            case 3:
            {
                int texture = g2(&buffer);
                overlay->texture = texture == 65535 ? -1 : texture;
                RSCache_PresenceSet(&overlay->present, RSCACHE_OVERLAY_FIELD_TEXTURE);
                break;
            }
            case 8:  /* flag, no operand */
            case 10: /* flag: blockShadow = false */
            case 12: /* flag: underlayOverrides = true */
                break;
            case 9:
                overlay->rs2_scale = g2(&buffer) << 2;
                RSCache_PresenceSet(&overlay->present, RSCACHE_OVERLAY_FIELD_RS2_SCALE);
                break;
            case 11:
                overlay->rs2_opcode_11 = g1(&buffer);
                RSCache_PresenceSet(&overlay->present, RSCACHE_OVERLAY_FIELD_RS2_OPCODE_11);
                break;
            case 13:
                overlay->rs2_water_colour = g3(&buffer);
                RSCache_PresenceSet(&overlay->present, RSCACHE_OVERLAY_FIELD_RS2_WATER_COLOUR);
                break;
            case 14:
                overlay->rs2_water_scale = g1(&buffer) << 2;
                RSCache_PresenceSet(&overlay->present, RSCACHE_OVERLAY_FIELD_RS2_WATER_SCALE);
                break;
            case 15:
            {
                /* secondaryTextureId — must be consumed so later opcodes stay aligned. */
                int texture = g2(&buffer);
                overlay->rs2_secondary_texture = texture == 65535 ? -1 : texture;
                RSCache_PresenceSet(&overlay->present,
                                    RSCACHE_OVERLAY_FIELD_RS2_SECONDARY_TEXTURE);
                break;
            }
            case 16:
                overlay->rs2_water_intensity = g1(&buffer);
                RSCache_PresenceSet(&overlay->present, RSCACHE_OVERLAY_FIELD_RS2_WATER_INTENSITY);
                break;
            default:
                /* Unknown width: stop rather than misalign the rest. */
                goto overlay_done;
            }
        }
        else
        {
            goto overlay_done;
        }
    }

overlay_done:
    return buffer.position;
}

uint32_t
RSCache_Dat2ConfigOverlayEncode(
    const struct RSCache_Dat2ConfigOverlay* overlay,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(overlay);
    assert(out);
    assert(out_capacity >= RSCache_Dat2ConfigOverlayEncodeBound(overlay));

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* Stated, not inferred: this used to compare each field with its decode
     * default, so a record that stated colour 0 lost its opcode 1. The order is a
     * fixed one; the cache's own varies record to record (author order), which is
     * the `same-len` column of `cachepack verify`. */
    if( RSCache_PresenceHas(&overlay->present, RSCACHE_OVERLAY_FIELD_COLOUR) )
    {
        p1(&buffer, 1);
        p3(&buffer, overlay->rgb_color);
    }
    if( RSCache_PresenceHas(&overlay->present, RSCACHE_OVERLAY_FIELD_TEXTURE) )
    {
        assert(overlay->texture >= 0);
        assert(overlay->texture <= 255);
        p1(&buffer, 2);
        p1(&buffer, overlay->texture);
    }
    /* Opcodes 3 and 5 are payload-free flags. hide_underlay defaults to true, so
     * opcode 5 is what clears it. */
    if( RSCache_PresenceHas(&overlay->present, RSCACHE_OVERLAY_FIELD_FLOTYPE) )
        p1(&buffer, 3);
    if( RSCache_PresenceHas(&overlay->present, RSCACHE_OVERLAY_FIELD_SHOW_UNDERLAY) )
        p1(&buffer, 5);
    if( RSCache_PresenceHas(&overlay->present, RSCACHE_OVERLAY_FIELD_FLOTYPE_NAME) )
    {
        assert(overlay->flotype_name);
        p1(&buffer, 6);
        /* dat1-era field: newline terminated, matching gstringnewline. */
        pjstr(&buffer, overlay->flotype_name, RSCACHE_JSTR_TERMINATOR_NEWLINE);
    }
    if( RSCache_PresenceHas(&overlay->present, RSCACHE_OVERLAY_FIELD_BLEND_COLOUR) )
    {
        p1(&buffer, 7);
        p3(&buffer, overlay->secondary_rgb_color);
    }

    p1(&buffer, 0);
    return buffer.position;
}

uint32_t
RSCache_Dat2ConfigUnderlayEncode(
    const struct RSCache_Dat2ConfigUnderlay* underlay,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(underlay);
    assert(out);
    assert(out_capacity >= RSCache_Dat2ConfigUnderlayEncodeBound(underlay));

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* An underlay is a single colour. Colour 0 is a legitimate value (black) that
     * one osrs239 record states explicitly; testing the value instead of presence
     * wrote that record as a bare terminator. */
    if( RSCache_PresenceHas(&underlay->present, RSCACHE_UNDERLAY_FIELD_COLOUR) )
    {
        p1(&buffer, 1);
        p3(&buffer, underlay->rgb_color);
    }

    p1(&buffer, 0);
    return buffer.position;
}

void
RSCache_Dat2ConfigOverlayFree(struct RSCache_Dat2ConfigOverlay* overlay)
{
    if( !overlay )
        return;

    RSCache_Dat2ConfigOverlayFreeInplace(overlay);
    free(overlay);
}

void
RSCache_Dat2ConfigOverlayFreeInplace(struct RSCache_Dat2ConfigOverlay* overlay)
{
    if( !overlay )
        return;
    free(overlay->flotype_name);
}

struct RSCache_Dat2ConfigUnderlay*
RSCache_Dat2ConfigUnderlayNewDecode(
    char* data,
    int data_size)
{
    struct RSCache_Dat2ConfigUnderlay* underlay =
        (struct RSCache_Dat2ConfigUnderlay*)malloc(sizeof(struct RSCache_Dat2ConfigUnderlay));
    assert(underlay);

    RSCache_Dat2ConfigUnderlayDecodeInplace(underlay, data, data_size);

    return underlay;
}

void
RSCache_Dat2ConfigUnderlayDecodeInplace(
    struct RSCache_Dat2ConfigUnderlay* underlay,
    char* data,
    int data_size)
{
    RSCache_Dat2ConfigUnderlayDecodeInplaceFlags(underlay, data, data_size, 0);
}

void
RSCache_Dat2ConfigUnderlayDecodeInplaceFlags(
    struct RSCache_Dat2ConfigUnderlay* underlay,
    char* data,
    int data_size,
    int flags)
{
    memset(underlay, 0, sizeof(struct RSCache_Dat2ConfigUnderlay));

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);

    while( true )
    {
        if( buffer.position >= buffer.size )
            break;

        int opcode = g1(&buffer);
        if( opcode == 0 )
            break;

        if( opcode == 1 )
        {
            int color = g3(&buffer);
            underlay->rgb_color = color;
            RSCache_PresenceSet(&underlay->present, RSCACHE_UNDERLAY_FIELD_COLOUR);
        }
        else if( flags & RSCACHE_CONFIG_FLO_DECODE_RS2 )
        {
            /* RS2 (643), per void's UnderlayDecoder.kt. Opcode 2 is a **u16** here; the
             * older reading does not implement it at all, so the widths are kept behind
             * the flag rather than shared. */
            switch( opcode )
            {
            case 2:
            {
                int texture = g2(&buffer);
                underlay->rs2_texture = texture == 65535 ? -1 : texture;
                RSCache_PresenceSet(&underlay->present, RSCACHE_UNDERLAY_FIELD_RS2_TEXTURE);
                break;
            }
            case 3:
                underlay->rs2_scale = g2(&buffer) << 2;
                RSCache_PresenceSet(&underlay->present, RSCACHE_UNDERLAY_FIELD_RS2_SCALE);
                break;
            case 4: /* flag: blockShadow = false */
            case 5: /* flag */
                break;
            default:
                return; /* unknown width: stop rather than misalign */
            }
        }
    }
}

void
RSCache_Dat2ConfigUnderlayFree(struct RSCache_Dat2ConfigUnderlay* underlay)
{
    if( !underlay )
        return;

    RSCache_Dat2ConfigUnderlayFreeInplace(underlay);

    free(underlay);
}

void
RSCache_Dat2ConfigUnderlayFreeInplace(struct RSCache_Dat2ConfigUnderlay* underlay)
{
    if( !underlay )
        return;
    (void)underlay;
}

uint32_t
RSCache_Dat2ConfigOverlayEncodeBound(const struct RSCache_Dat2ConfigOverlay* overlay)
{
    /* Every scalar opcode plus the one string field, which uses a newline
     * terminator rather than a NUL — same length either way. */
    uint32_t need = 64u;

    assert(overlay);
    if( overlay->flotype_name )
        need += (uint32_t)strlen(overlay->flotype_name) + 2u;
    return need;
}

uint32_t
RSCache_Dat2ConfigUnderlayEncodeBound(const struct RSCache_Dat2ConfigUnderlay* underlay)
{
    /* A single opcode-1 colour, or a bare terminator. */
    (void)underlay;
    return 16u;
}
