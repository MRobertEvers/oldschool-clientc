/*
 * PARKED WORK, THE VISUAL PARTS OF INTERFACE MOUNTS, PLAYER MODELS.
 *
 * One translation unit of the App layer, and one family of the parked lookup
 * (game/rs_parked.h, app/app_parked.h). Everything here may read and write
 * `struct App`; what other units call into this family is declared in this
 * family's own section of app/app_parked.h.
 *
 * What belongs here: the visual half of an interface mount (IF_OPENTOP /
 * OPENSUB / MOVESUB / RESYNC and old-format slot mounts): the pack's models,
 * kit models and seed inventory models, which the mount task no longer waits
 * for (the pack, the bake, the load scripts and the transmits stay required);
 * and a player-model widget's build (IF_SETPLAYERMODEL_*), keyed on its
 * component and a composition generation so a stale build never overwrites a
 * newer one, the last built model kept until the new one lands.
 *
 * A kind of this family takes its id from PARK_KINDS_MOUNTS_FIRST ..
 * PARK_KINDS_MOUNTS_LAST, is registered from App_ParkedMountsRegister below
 * with its landing function and its policy row, and keeps that row in this
 * file next to the kind. A landing looks its target up again by the key (an
 * id plus a generation, never a pointer), checks it still applies, and
 * applies it or drops it; it reads the asset from the cache at that moment.
 */

#include "app/app_internal.h"

void
App_ParkedMountsRegister(struct App* app)
{
    assert(app);
    assert(app->parked);
    /* No kinds yet: the family's step registers them here. */
    (void)app;
}
