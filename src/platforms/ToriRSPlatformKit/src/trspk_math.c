#include "../include/ToriRSPlatformKit/trspk_math.h"

#include "graphics/dash_hsl16.h"

/** Keep TRSPK sentinels in sync with graphics/dash_hsl16.h and batch_add_model.h. */
_Static_assert(
    (uint32_t)TRSPK_HSL16_FLAT == (uint32_t)DASHHSL16_FLAT, "TRSPK_HSL16_FLAT must match DASHHSL16_FLAT");
_Static_assert(
    (uint32_t)TRSPK_HSL16_HIDDEN == (uint32_t)DASHHSL16_HIDDEN,
    "TRSPK_HSL16_HIDDEN must match DASHHSL16_HIDDEN");

float
trspk_dash_yaw_to_radians(int32_t yaw_2048)
{
    return ((float)yaw_2048 * 2.0f * TRSPK_PI) / 2048.0f;
}

float
trspk_texture_animation_signed(int animation_direction, int animation_speed)
{
    if( animation_direction == 0 )
        return 0.0f;
    float speed = ((float)animation_speed) / 128.0f;
    if( animation_direction == 2 || animation_direction == 4 )
        return speed;
    return -speed;
}
