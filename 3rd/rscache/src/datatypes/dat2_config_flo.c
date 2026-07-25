#include "dat2_config_flo.h"

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
    init_overlay(overlay);

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);

    while( true )
    {
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
        else if( opcode == 3 )
        {
            overlay->flotype_overlay = true;
        }
        else if( opcode == 5 )
        {
            overlay->hide_underlay = false;
        }
        else if( opcode == 6 )
        {
            overlay->flotype_name = gstringnewline(&buffer);
        }
        else if( opcode == 7 )
        {
            int secondary_color = g3(&buffer);
            overlay->secondary_rgb_color = secondary_color;
        }
        else
        {
            assert(false);
        }
    }

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
    memset(underlay, 0, sizeof(struct RSCache_Dat2ConfigUnderlay));

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);

    while( true )
    {
        int opcode = g1(&buffer);
        if( opcode == 0 )
            break;

        if( opcode == 1 )
        {
            int color = g3(&buffer);
            underlay->rgb_color = color;
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
