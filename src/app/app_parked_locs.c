/*
 * PARKED WORK, LOC CHANGES, LOC ANIMATIONS, MERGES, ZONE RESETS, ZONE
 * PACKETS FOR UNBUILT VIEWS.
 *
 * One translation unit of the App layer, and one family of the parked lookup
 * (game/rs_parked.h, app/app_parked.h). Everything here may read and write
 * `struct App`; what other units call into this family is declared in this
 * family's own section of app/app_parked.h.
 *
 * What belongs here: a loc change (LOC_ADD_CHANGE / LOC_DEL), keyed and
 * ordered PER TILE AND LAYER of its view -- the one kind whose landing writes
 * game state (scenery and collision) together with the scene, after the
 * loc's own models load; a delete skips the wait -- a loc animation (a field
 * of that tile's entry when a change is pending), a loc merge (an entry with
 * a time to land, counted in its own view), a zone reset
 * (UPDATE_ZONE_FULL_FOLLOWS removes the entries it reverts), and zone
 * packets for a view that is not built yet (entries that want that view,
 * readied when it is).
 *
 * A kind of this family takes its id from PARK_KINDS_LOCS_FIRST ..
 * PARK_KINDS_LOCS_LAST, is registered from App_ParkedLocsRegister below
 * with its landing function and its policy row, and keeps that row in this
 * file next to the kind. A landing looks its target up again by the key (an
 * id plus a generation, never a pointer), checks it still applies, and
 * applies it or drops it; it reads the asset from the cache at that moment.
 */

#include "app/app_internal.h"

void
App_ParkedLocsRegister(struct App* app)
{
    assert(app);
    assert(app->parked);
    /* No kinds yet: the family's step registers them here. */
    (void)app;
}
