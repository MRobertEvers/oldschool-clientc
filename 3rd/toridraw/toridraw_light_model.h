#ifndef TORIDRAW_LIGHT_MODEL_H
#define TORIDRAW_LIGHT_MODEL_H

#include "toridraw_types.h"

/**
 * Scene/widget default light — Model.calculateNormals(64 + ambient,
 * 768 + contrast, -50, -10, -50).
 *
 * Both are *signed* config values added as they arrive; a config that darkens
 * its model passes a negative ambient. Contrast arrives already pre-scaled by
 * whatever multiplier its config opcode uses, so do not re-scale or mask it.
 */
void
ToriDraw_LightModelDefault(
    struct ToriDraw_ModelHandle hnd,
    int model_contrast,
    int model_ambient);

/** Deprecated alias for ToriDraw_LightModelDefault, which no longer has a
 *  non-pre-scaled mode. Kept so the older trees still compile. */
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
