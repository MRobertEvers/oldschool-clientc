/*
 * PARKED WORK, PROJECTILES AND SPOT ANIMATIONS.
 *
 * One translation unit of the App layer, and one family of the parked lookup
 * (game/rs_parked.h, app/app_parked.h). Everything here may read and write
 * `struct App`; what other units call into this family is declared in this
 * family's own section of app/app_parked.h.
 *
 * What belongs here: a scripted or plain projectile's models and sequences
 * (SCRIPTEDPROJ_ADD / _CHANGE, PROJANIM*) -- the projectile exists at packet
 * time, keyed on its slot and serial, and its flight counts from arrival --
 * and a spot animation's type, model and sequence (MAP_ANIM, an entity's
 * attached graphics), keyed on its view tile or its entity and spawn
 * serial.
 *
 * A kind of this family takes its id from PARK_KINDS_EFFECTS_FIRST ..
 * PARK_KINDS_EFFECTS_LAST, is registered from App_ParkedEffectsRegister below
 * with its landing function and its policy row, and keeps that row in this
 * file next to the kind. A landing looks its target up again by the key (an
 * id plus a generation, never a pointer), checks it still applies, and
 * applies it or drops it; it reads the asset from the cache at that moment.
 */

#include "app/app_internal.h"

void
App_ParkedEffectsRegister(struct App* app)
{
    assert(app);
    assert(app->parked);
    /* No kinds yet: the family's step registers them here. */
    (void)app;
}
