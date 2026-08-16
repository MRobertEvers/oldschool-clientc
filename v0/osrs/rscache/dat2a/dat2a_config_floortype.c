#include "dat2a_config_floortype.h"

#include "../shared/shared_rs_buffer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// void
// config_floortype_overlay_init(struct RSCacheDat2A_ConfigOverlay* overlay)
// {
//     // this.primaryRgb = 0;
//     // this.textureId = -1;
//     // this.secondaryTextureId = -1;
//     // this.hideUnderlay = true;
//     // this.secondaryRgb = -1;
//     // this.hue = 0;
//     // this.saturation = 0;
//     // this.lightness = 0;
//     // this.hueBlend = 0;
//     // this.hueMultiplier = 0;
//     // this.secondaryHue = 0;
//     // this.secondarySaturation = 0;
//     // this.secondaryLightness = 0;
//     // this.textureSize = 128;
//     // this.blockShadow = true;
//     // this.textureBrightness = 8;
//     // this.blendTexture = false;
//     // this.underwaterColor = 0x122b3d;
//     // this.waterOpacity = 16;
// }

// void
// config_floortype_underlay_init(struct RSCacheDat2A_ConfigUnderlay* underlay)
// {
//     //  this.rgbColor = 0;
//     // this.hue = 0;
//     // this.saturation = 0;
//     // this.lightness = 0;
//     // this.hueMultiplier = 0;
//     // this.isOverlay = false;
//     // this.textureId = -1;
//     // this.textureSize = 128;
//     // this.blockShadow = true;

//     underlay->rgb_color = 0;
//     underlay->hue = 0;
//     underlay->saturation = 0;
//     underlay->lightness = 0;
//     underlay->hue_multiplier = 0;
//     underlay->is_overlay = false;
//     underlay->texture_id = -1;
//     underlay->texture_size = 128;
//     underlay->block_shadow = true;
// }

static void
init_overlay(struct RSCacheDat2A_ConfigOverlay* overlay)
{
    memset(overlay, 0, sizeof(struct RSCacheDat2A_ConfigOverlay));
    overlay->rgb_color = 0;
    overlay->texture = -1;
    overlay->secondary_rgb_color = -1;
    overlay->hide_underlay = true;

    overlay->flotype_overlay = false;
}

struct RSCacheDat2A_ConfigOverlay*
config_floortype_overlay_new_decode(
    char* data,
    int data_size)
{
    struct RSCacheShared_RSBuffer buffer = {
        .data = (uint8_t*)(data),
        .size = (uint32_t)(data_size),
        .position = 0,
    };

    struct RSCacheDat2A_ConfigOverlay* overlay =
        (struct RSCacheDat2A_ConfigOverlay*)malloc(sizeof(struct RSCacheDat2A_ConfigOverlay));
    if( !overlay )
    {
        fprintf(stderr, "Failed to allocate memory for overlay\n");
        return NULL;
    }

    config_floortype_overlay_decode_inplace(overlay, data, data_size);

    return overlay;
}

int
config_floortype_overlay_decode_inplace(
    struct RSCacheDat2A_ConfigOverlay* overlay,
    char* data,
    int data_size)
{
    memset(overlay, 0, sizeof(struct RSCacheDat2A_ConfigOverlay));

    init_overlay(overlay);

    struct RSCacheShared_RSBuffer buffer = {
        .data = (uint8_t*)(data),
        .size = (uint32_t)(data_size),
        .position = 0,
    };

    while( true )
    {
        int opcode = RSCacheShared_RSBufferG1(&buffer);
        if( opcode == 0 )
            break;

        if( opcode == 1 )
        {
            int color = RSCacheShared_RSBufferG3(&buffer);
            overlay->rgb_color = color;
        }
        else if( opcode == 2 )
        {
            int texture = RSCacheShared_RSBufferG1(&buffer);
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
            int secondary_color = RSCacheShared_RSBufferG3(&buffer);
            overlay->secondary_rgb_color = secondary_color;
        }
        else
        {
            assert(false);
        }
    }

    return buffer.position;
}

void
config_floortype_overlay_free(struct RSCacheDat2A_ConfigOverlay* overlay)
{
    if( !overlay )
        return;

    config_floortype_overlay_free_inplace(overlay);
    free(overlay);
}

void
config_floortype_overlay_free_inplace(struct RSCacheDat2A_ConfigOverlay* overlay)
{
    if( !overlay )
        return;
    free(overlay->flotype_name);
}

struct RSCacheDat2A_ConfigUnderlay*
config_floortype_underlay_new_decode(
    char* data,
    int data_size)
{
    // for (;;)
    // {
    // 	int opcode = is.readUnsignedByte();
    // 	if (opcode == 0)
    // 	{
    // 		break;
    // 	}

    // 	if (opcode == 1)
    // 	{
    // 		int color = is.read24BitInt();
    // 		def.setColor(color);
    // 	}
    // }

    struct RSCacheDat2A_ConfigUnderlay* underlay =
        (struct RSCacheDat2A_ConfigUnderlay*)malloc(sizeof(struct RSCacheDat2A_ConfigUnderlay));
    if( !underlay )
    {
        fprintf(stderr, "Failed to allocate memory for underlay\n");
        return NULL;
    }

    config_floortype_underlay_decode_inplace(underlay, data, data_size);

    return underlay;
}

void
config_floortype_underlay_decode_inplace(
    struct RSCacheDat2A_ConfigUnderlay* underlay,
    char* data,
    int data_size)
{
    memset(underlay, 0, sizeof(struct RSCacheDat2A_ConfigUnderlay));

    struct RSCacheShared_RSBuffer buffer = {
        .data = (uint8_t*)(data),
        .size = (uint32_t)(data_size),
        .position = 0,
    };

    while( true )
    {
        int opcode = RSCacheShared_RSBufferG1(&buffer);
        if( opcode == 0 )
            break;

        if( opcode == 1 )
        {
            int color = RSCacheShared_RSBufferG3(&buffer);
            underlay->rgb_color = color;
        }
    }
}

void
config_floortype_underlay_free(struct RSCacheDat2A_ConfigUnderlay* underlay)
{
    if( !underlay )
        return;

    config_floortype_underlay_free_inplace(underlay);

    free(underlay);
}

void
config_floortype_underlay_free_inplace(struct RSCacheDat2A_ConfigUnderlay* underlay)
{
    if( !underlay )
        return;
}