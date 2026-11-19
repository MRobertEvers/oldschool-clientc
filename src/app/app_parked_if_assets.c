/*
 * PARKED WORK, INTERFACE MODELS, CHATHEADS, INVENTORY ICONS.
 *
 * One translation unit of the App layer, and one family of the parked lookup
 * (game/rs_parked.h, app/app_parked.h). Everything here may read and write
 * `struct App`; what other units call into this family is declared in this
 * family's own section of app/app_parked.h.
 *
 * What belongs here: what IF_SETMODEL, IF_SETOBJECT, IF_SETNPCHEAD(_ACTIVE),
 * IF_SETPLAYERHEAD and IF_SETANIM put on a widget once its models are in, and
 * an inventory's item icons -- keyed on the component and its interface's
 * close serial, checked at landing against what the widget asks for now.
 *
 * A kind of this family takes its id from PARK_KINDS_IF_ASSETS_FIRST ..
 * PARK_KINDS_IF_ASSETS_LAST, is registered from App_ParkedIfAssetsRegister below
 * with its landing function and its policy row, and keeps that row in this
 * file next to the kind. A landing looks its target up again by the key (an
 * id plus a generation, never a pointer), checks it still applies, and
 * applies it or drops it; it reads the asset from the cache at that moment.
 */

#include "app/app_internal.h"

void
App_ParkedIfAssetsRegister(struct App* app)
{
    assert(app);
    assert(app->parked);
    /* No kinds yet: the family's step registers them here. */
    (void)app;
}
