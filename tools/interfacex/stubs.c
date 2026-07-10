#include "osrs/datatypes/appearances.h"
#include "osrs/texture.h"
#include "toridraw/toridraw.h"

#include <stdarg.h>
#include <stdio.h>

void
appearances_decode(
    struct AppearanceOp* op,
    uint16_t* appearances,
    int slot)
{
    (void)appearances;
    (void)slot;
    if( op )
    {
        op->kind = APPEARANCE_KIND_NONE;
        op->id = 0;
    }
}

int
cs1vm_script_length(int const* script)
{
    (void)script;
    return 0;
}

struct ToriDraw_Texture*
texture_new_toridraw_from_texture_sprite(
    struct RSCacheDat1A_ConfigTexture* texture,
    int animation_direction,
    int animation_speed,
    bool upscale_to_128,
    bool half_to_64)
{
    (void)texture;
    (void)animation_direction;
    (void)animation_speed;
    (void)upscale_to_128;
    (void)half_to_64;
    return NULL;
}

struct ToriDraw_Texture*
texture_new_toridraw_from_definition_packs(
    struct RSCacheDat2A_Texture* texture_definition,
    struct RSCacheDat2A_SpritePack** packs)
{
    (void)texture_definition;
    (void)packs;
    return NULL;
}

int
ui_minimenu_debug_enabled(void)
{
    return 0;
}

void
ui_minimenu_debug_log(char const* fmt, ...)
{
    (void)fmt;
}
