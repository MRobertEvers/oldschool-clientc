/*
 * PARKED WORK, GROUND ITEMS AND WIDGET ICONS.
 *
 * One translation unit of the App layer, and one family of the parked lookup
 * (game/rs_parked.h, app/app_parked.h). Everything here may read and write
 * `struct App`; what other units call into this family is declared in this
 * family's own section of app/app_parked.h.
 *
 * What belongs here: a ground stack's model (OBJ_ADD, OBJ_REVEAL, OBJ_COUNT,
 * OBJ_CUSTOMISE and their _SPECIFIC forms) -- the row exists in the world at
 * packet time and only its model is parked, keyed on the stack's view, tile
 * and level -- and a widget's item icon (the widget icon park), keyed on the
 * component and its interface's close serial. The ground-item faults G1-G3
 * of the fault list are fixed here.
 *
 * A kind of this family takes its id from PARK_KINDS_GROUND_FIRST ..
 * PARK_KINDS_GROUND_LAST, is registered from App_ParkedGroundRegister below
 * with its landing function and its policy row, and keeps that row in this
 * file next to the kind. A landing looks its target up again by the key (an
 * id plus a generation, never a pointer), checks it still applies, and
 * applies it or drops it; it reads the asset from the cache at that moment.
 */

#include "app/app_internal.h"

void
App_ParkedGroundRegister(struct App* app)
{
    assert(app);
    assert(app->parked);
    /* No kinds yet: the family's step registers them here. */
    (void)app;
}
