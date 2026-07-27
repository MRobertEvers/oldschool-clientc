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
        }
        else if( opcode == 2 )
        {
            int texture = g1(&buffer);
            overlay->texture = texture;
        }
        else if( opcode == 3 && !(flags & RSCACHE_CONFIG_FLO_DECODE_RS2) )
        {
            overlay->flotype_overlay = true;
        }
        else if( opcode == 5 )
        {
            overlay->hide_underlay = false;
        }
        else if( opcode == 6 && !(flags & RSCACHE_CONFIG_FLO_DECODE_RS2) )
        {
            overlay->flotype_name = gstringnewline(&buffer);
        }
        else if( opcode == 7 )
        {
            int secondary_color = g3(&buffer);
            overlay->secondary_rgb_color = secondary_color;
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
                break;
            }
            case 8:  /* flag, no operand */
            case 10: /* flag: blockShadow = false */
            case 12: /* flag: underlayOverrides = true */
                break;
            case 9:
                overlay->rs2_scale = g2(&buffer) << 2;
                break;
            case 11:
                overlay->rs2_opcode_11 = g1(&buffer);
                break;
            case 13:
                overlay->rs2_water_colour = g3(&buffer);
                break;
            case 14:
                overlay->rs2_water_scale = g1(&buffer) << 2;
                break;
            case 15:
            {
                /* secondaryTextureId — must be consumed so later opcodes stay aligned. */
                int texture = g2(&buffer);
                overlay->rs2_secondary_texture = texture == 65535 ? -1 : texture;
                break;
            }
            case 16:
                overlay->rs2_water_intensity = g1(&buffer);
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
    if( !overlay || !out )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    struct RSCache_Dat2ConfigOverlay defaults;
    init_overlay(&defaults);

    if( overlay->rgb_color != defaults.rgb_color )
    {
        p1(&buffer, 1);
        p3(&buffer, overlay->rgb_color);
    }
    if( overlay->texture != defaults.texture )
    {
        p1(&buffer, 2);
        p1(&buffer, overlay->texture);
    }
    /* Opcodes 3 and 5 are payload-free flags. hide_underlay defaults to true, so
     * opcode 5 is what clears it. */
    if( overlay->flotype_overlay )
        p1(&buffer, 3);
    if( !overlay->hide_underlay )
        p1(&buffer, 5);
    if( overlay->flotype_name )
    {
        p1(&buffer, 6);
        /* dat1-era field: newline terminated, matching gstringnewline. */
        pjstr(&buffer, overlay->flotype_name, RSCACHE_JSTR_TERMINATOR_NEWLINE);
    }
    if( overlay->secondary_rgb_color != defaults.secondary_rgb_color )
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
    if( !underlay || !out )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* An underlay is a single colour. Colour 0 is a legitimate value (black), but
     * it is also the zeroed default, so a record whose colour is 0 encodes as a
     * bare terminator — which decodes back to 0. */
    if( underlay->rgb_color != 0 )
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
                break;
            }
            case 3:
                underlay->rs2_scale = g2(&buffer) << 2;
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
