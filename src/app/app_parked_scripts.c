/*
 * PARKED WORK, VISUAL SCRIPT OPERATIONS.
 *
 * One translation unit of the App layer, and one family of the parked lookup
 * (game/rs_parked.h, app/app_parked.h). Everything here may read and write
 * `struct App`; what other units call into this family is declared in this
 * family's own section of app/app_parked.h.
 *
 * What belongs here: the client-script operations that only put something
 * on screen (a model set on a widget, an object on a MODEL widget, a
 * component's models, a loc model): the operation writes what was asked for
 * onto the widget and returns, and the asset is parked here, keyed on the
 * component and its interface's close serial. Operations whose value the
 * script reads stay required and wait, as today.
 *
 * A kind of this family takes its id from PARK_KINDS_SCRIPTS_FIRST ..
 * PARK_KINDS_SCRIPTS_LAST, is registered from App_ParkedScriptsRegister below
 * with its landing function and its policy row, and keeps that row in this
 * file next to the kind. A landing looks its target up again by the key (an
 * id plus a generation, never a pointer), checks it still applies, and
 * applies it or drops it; it reads the asset from the cache at that moment.
 */

#include "app/app_internal.h"

void
App_ParkedScriptsRegister(struct App* app)
{
    assert(app);
    assert(app->parked);
    /* No kinds yet: the family's step registers them here. */
    (void)app;
}
