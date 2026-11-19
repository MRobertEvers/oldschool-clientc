#ifndef SRC_APP_APP_PARKED_H
#define SRC_APP_APP_PARKED_H

/*
 * The App's parked lookup (game/rs_parked.h): app->parked, its landing point,
 * and one unit per family of parked work.
 *
 * App layer only: every unit under src/app/ (and src/app.c) may include this;
 * nothing outside the layer may. app/app_internal.h includes it.
 *
 * Each family has its own section below and its own unit, already in
 * src/makefile and already called from App_ParkedRegisterAll. A family
 * declares here, in its own section, what other units of the layer call into
 * it (the packet handlers that park its kinds), picks its kind ids from its
 * own range in game/rs_parked.h, and registers them from its own register
 * function. No family edits another's section, unit or range.
 */

#include "game/rs_parked.h"

struct App;

/* ---- app_parked.c: the lookup, the registration list, the landing point - */

/** Landings per frame: the rest stay ready for the next frame, so a burst of
 *  arrivals (an interface's eighty icons) is spread over frames instead of
 *  landing in one. */
#define APP_PARKED_LAND_BUDGET 32

/** Make app->parked (after app->assets) and register every family's kinds. */
void
App_ParkedInit(struct App* app);

/** Before the assets table is freed: drops every waiter the lookup holds. */
void
App_ParkedFree(struct App* app);

/** The one registration list: every family's register function, in order. */
void
App_ParkedRegisterAll(struct App* app);

/**
 * THE LANDING POINT (see the frame order in app/app_frame.c). Lands the
 * lookup's arrivals in parked arrival order under APP_PARKED_LAND_BUDGET; if a
 * landing queued a client script, runs the script settle once more. Returns 0
 * when that settle did not reach a fixed point (the frame is withheld, as at
 * every other settle site), 1 otherwise. Not while a packet task is in flight:
 * then nothing lands and the arrivals wait for a frame where none is.
 */
int
App_ParkedLandingPoint(struct App* app);

/** The client cycle entries arrive and land on (the logic cycle). */
uint32_t
App_ParkedCycle(struct App const* app);

/* ---- app_parked_ground.c: ground items and widget icons ------------------ */

void
App_ParkedGroundRegister(struct App* app);

/* ---- app_parked_effects.c: projectiles and spot animations -------------- */

void
App_ParkedEffectsRegister(struct App* app);

/* ---- app_parked_locs.c: loc changes, loc animations, merges, zone resets,
 *      zone packets for unbuilt views ----------------------------------- */

void
App_ParkedLocsRegister(struct App* app);

/* ---- app_parked_entities.c: entity bodies and animations ---------------- */

void
App_ParkedEntitiesRegister(struct App* app);

/* ---- app_parked_if_assets.c: interface models, chatheads, inventory icons */

void
App_ParkedIfAssetsRegister(struct App* app);

/* ---- app_parked_mounts.c: the visual parts of interface mounts, player
 *      models -------------------------------------------------------------- */

void
App_ParkedMountsRegister(struct App* app);

/* ---- app_parked_scripts.c: visual script operations --------------------- */

void
App_ParkedScriptsRegister(struct App* app);

#endif /* SRC_APP_APP_PARKED_H */
