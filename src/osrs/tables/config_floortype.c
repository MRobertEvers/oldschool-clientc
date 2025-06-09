#include "config_floortype.h"

#include "osrs/rsbuf.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// void
// config_floortype_overlay_init(struct Overlay* overlay)
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
// config_floortype_underlay_init(struct Underlay* underlay)
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

struct Overlay*
config_floortype_overlay_new_decode(char* data, int data_size)
{
    struct RSBuffer buffer = {
        .data = data,
        .size = data_size,
        .position = 0,
    };

    struct Overlay* overlay = (struct Overlay*)malloc(sizeof(struct Overlay));
    if( !overlay )
    {
        fprintf(stderr, "Failed to allocate memory for overlay\n");
        return NULL;
    }

    config_floortype_overlay_decode_inplace(overlay, data, data_size);

    return overlay;
}

void
config_floortype_overlay_decode_inplace(struct Overlay* overlay, char* data, int data_size)
{
    memset(overlay, 0, sizeof(struct Overlay));

    overlay->texture = -1;
    overlay->hide_underlay = true;
    overlay->secondary_rgb_color = -1;

    struct RSBuffer buffer = {
        .data = data,
        .size = data_size,
        .position = 0,
    };

    while( true )
    {
        int opcode = rsbuf_g1(&buffer);
        if( opcode == 0 )
            break;

        if( opcode == 1 )
        {
            int color = rsbuf_g3(&buffer);
            overlay->rgb_color = color;
        }
        else if( opcode == 2 )
        {
            int texture = rsbuf_g1(&buffer);
            overlay->texture = texture;
        }
        else if( opcode == 5 )
        {
            overlay->hide_underlay = false;
        }
        else if( opcode == 7 )
        {
            int secondary_color = rsbuf_g3(&buffer);
            overlay->secondary_rgb_color = secondary_color;
        }
        else
        {
            assert(false);
        }
    }
}

void
config_floortype_overlay_free(struct Overlay* overlay)
{
    if( !overlay )
        return;

    free(overlay);
}

struct Underlay*
config_floortype_underlay_new_decode(char* data, int data_size)
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

    struct Underlay* underlay = (struct Underlay*)malloc(sizeof(struct Underlay));
    if( !underlay )
    {
        fprintf(stderr, "Failed to allocate memory for underlay\n");
        return NULL;
    }

    config_floortype_underlay_decode_inplace(underlay, data, data_size);

    return underlay;
}

void
config_floortype_underlay_decode_inplace(struct Underlay* underlay, char* data, int data_size)
{
    memset(underlay, 0, sizeof(struct Underlay));

    struct RSBuffer buffer = {
        .data = data,
        .size = data_size,
        .position = 0,
    };

    while( true )
    {
        int opcode = rsbuf_g1(&buffer);
        if( opcode == 0 )
            break;

        if( opcode == 1 )
        {
            int color = rsbuf_g3(&buffer);
            underlay->rgb_color = color;
        }
    }
}

void config_floortype_underlay_free(struct Underlay* underlay);