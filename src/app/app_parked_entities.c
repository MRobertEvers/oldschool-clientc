/*
 * PARKED WORK, ENTITY BODIES AND ANIMATIONS.
 *
 * One translation unit of the App layer, and one family of the parked lookup
 * (game/rs_parked.h, app/app_parked.h). Everything here may read and write
 * `struct App`; what other units call into this family is declared in this
 * family's own section of app/app_parked.h.
 *
 * What belongs here: a player's or npc's body (the npc's composite, a
 * player's kit and wear models, a transmog, held items) and the sequences it
 * plays -- the entity exists at packet time and draws nothing until its body
 * lands -- keyed on the entity, its type and its spawn serial, never on a
 * scene element id (those are recycled).
 *
 * A kind of this family takes its id from PARK_KINDS_ENTITIES_FIRST ..
 * PARK_KINDS_ENTITIES_LAST, is registered from App_ParkedEntitiesRegister below
 * with its landing function and its policy row, and keeps that row in this
 * file next to the kind. A landing looks its target up again by the key (an
 * id plus a generation, never a pointer), checks it still applies, and
 * applies it or drops it; it reads the asset from the cache at that moment.
 */

#include "app/app_internal.h"

void
App_ParkedEntitiesRegister(struct App* app)
{
    assert(app);
    assert(app->parked);
    /* No kinds yet: the family's step registers them here. */
    (void)app;
}
