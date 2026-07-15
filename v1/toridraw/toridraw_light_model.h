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

#endif
