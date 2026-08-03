#ifndef TORIDRAW_LIGHT_MODEL_H
#define TORIDRAW_LIGHT_MODEL_H

#include "toridraw_types.h"

/**
 * Process-wide light profiles for the two reference regimes:
 *
 *   actor  — players, NPCs, spotanims, projectiles, chatheads
 *            default: calculateNormals(64, 850, -30, -50, -30)
 *   scene  — locs, objs, widget models, sharelight
 *            default: calculateNormals(64, 768, -50, -10, -50)
 *
 * Boot may override via ToriDraw_LightSetProfiles (manifest [render:light]).
 * NULL arguments leave that profile at its compiled-in default.
 */
struct ToriDraw_LightProfile
{
    int ambient;
    int attenuation;
    int src_x;
    int src_y;
    int src_z;
};

void
ToriDraw_LightSetProfiles(
    struct ToriDraw_LightProfile const* actor,
    struct ToriDraw_LightProfile const* scene);

/** Read the current scene profile (sharelight inlines these constants). */
struct ToriDraw_LightProfile const*
ToriDraw_LightSceneProfile(void);

/** Read the current actor profile. */
struct ToriDraw_LightProfile const*
ToriDraw_LightActorProfile(void);

/**
 * Scene/widget light — profile ambient+ambient_off, attenuation+contrast,
 * profile light direction.
 *
 * Both offsets are *signed* config values added as they arrive; a config that
 * darkens its model passes a negative ambient. Contrast arrives already
 * pre-scaled by whatever multiplier its config opcode uses, so do not
 * re-scale or mask it.
 */
void
ToriDraw_LightModelScene(
    struct ToriDraw_ModelHandle hnd,
    int model_contrast,
    int model_ambient);

/**
 * Actor light — same offset rules as LightModelScene, but against the actor
 * profile (players / NPCs / spotanims / projectiles / chatheads).
 */
void
ToriDraw_LightModelActor(
    struct ToriDraw_ModelHandle hnd,
    int model_contrast,
    int model_ambient);

/** Deprecated alias for ToriDraw_LightModelScene — older trees still compile. */
void
ToriDraw_LightModelDefault(
    struct ToriDraw_ModelHandle hnd,
    int model_contrast,
    int model_ambient);

/**
 * Raw Model.calculateNormals(ambient, attenuation, lightsrcX, lightsrcY,
 * lightsrcZ) — for call sites that pass absolute light parameters rather
 * than a regime + config offsets.
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
