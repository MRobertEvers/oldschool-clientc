#ifndef SRC_GAME_RS_BOOT_STATE_H
#define SRC_GAME_RS_BOOT_STATE_H

/*
 * What the boot task keeps between its bakes: where the boot bar stands, the
 * root interface the current bake mounts, whether the once-per-session
 * preamble has run, and what a headless preview's state packet said.
 *
 * The bar is a percentage the boot task moves as it reaches each stage. A
 * bake that starts walks it back to zero, except a quiet one: everything
 * after the session's first bake crosses caches the first one warmed, so it
 * leaves the bar where the loud one parked it.
 *
 * The root interface is the one the bake was started for. It is also what
 * IF_OPENTOP and IF_RESYNC compare the server's top against, so it outlives
 * the bake and is only replaced by the next one.
 *
 * The preamble latch is set once and never cleared. The boot task runs again
 * on every root remount, and its preamble is not idempotent: reloading the
 * varp types reallocates the varp value arrays and zeroes every varp the
 * server has sent. Cache type tables and device settings cannot change inside
 * a session, so the preamble runs once.
 *
 * This does not know whether a bake is quiet, which stage the boot is at, or
 * how to apply a preview packet. The caller says whether the bake is quiet,
 * sets the bar at each stage, and applies the preview through
 * ToriRSPreviewState_ApplyFileOutcome into `preview`.
 */

#include "game/preview_state.h"

#include <stdbool.h>

struct RS_BootState
{
    /** 0..100, drives the loading bar while BOOTING. */
    int progress;
    /** The root interface the current bake mounts; -1 for the title tree,
     *  which has none. */
    int interface_id;
    /** The once-per-session half of the boot task has already run. */
    int config_ready;
    /** The headless preview's state packet, when one was applied. */
    struct ToriRSPreviewStateOutcome preview;
};

/** Nothing booted: the bar at 0, no root, the preamble not run, no preview. */
void
RS_BootState_Reset(struct RS_BootState* boot);

/** A bake started for `interface_id`. A loud bake walks the bar back to 0; a
 *  `quiet` one leaves it where it is. */
void
RS_BootState_BakeStarted(
    struct RS_BootState* boot,
    int interface_id,
    bool quiet);

/** The boot reached a stage at `percent`. */
void
RS_BootState_SetProgress(
    struct RS_BootState* boot,
    int percent);

int
RS_BootState_Progress(struct RS_BootState const* boot);

/** The root interface the current (or last) bake was started for. */
int
RS_BootState_InterfaceId(struct RS_BootState const* boot);

/** The once-per-session preamble has run. */
bool
RS_BootState_ConfigReady(struct RS_BootState const* boot);

/** The preamble ran; it never runs again this session. */
void
RS_BootState_MarkConfigReady(struct RS_BootState* boot);

#endif /* SRC_GAME_RS_BOOT_STATE_H */
