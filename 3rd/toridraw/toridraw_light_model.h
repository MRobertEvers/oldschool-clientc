#ifndef TORIDRAW_LIGHT_MODEL_H
#define TORIDRAW_LIGHT_MODEL_H

#include "toridraw_types.h"

void
ToriDraw_LightModelDefault(
    struct ToriDraw_ModelHandle hnd,
    int model_contrast,
    int model_ambient);

/** Like ToriDraw_LightModelDefault but contrast is already pre-scaled (dat2 decode). */
void
ToriDraw_LightModelDefaultPreScaled(
    struct ToriDraw_ModelHandle hnd,
    int model_contrast,
    int model_ambient);

/**
 * Raw Model.calculateNormals(ambient, attenuation, lightsrcX, lightsrcY,
 * lightsrcZ) — for the call sites where the reference passes light parameters
 * other than the widget/scene defaults (e.g. the player-design preview's
 * 64, 850, -30, -50, -30).
 */
void
ToriDraw_LightModelParams(
    struct ToriDraw_ModelHandle hnd,
    int light_ambient,
    int light_attenuation,
    int lightsrc_x,
    int lightsrc_y,
    int lightsrc_z);

#endif
