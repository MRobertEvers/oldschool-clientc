#ifndef TORIRSSERVER_RUNENERGY_H
#define TORIRSSERVER_RUNENERGY_H

/*
 * The two run-energy models, selected by `run_energy_model` in the era feature
 * table (src/features/features.h, enum ToriRS_RunEnergyModel).
 *
 * Split out of torirs_server_world.c's tick so both halves can be evaluated without a
 * server, a player or a cache: these are the arithmetic, nothing else. The tick
 * keeps everything that is state — the 2-step branch, the stamina varbit, the
 * toggle that goes dark at zero — because none of that changed between eras.
 *
 * Units are the server's own: energy is 0..TORIRSSERVER_RUN_ENERGY_MAX (10000), so
 * both return hundredths of a percent per tick, and weight is whole kilograms.
 */

/**
 * Energy spent by one tick of running.
 *
 * `weight_kg` is clamped to 0..64 here, so a caller may pass the raw carried
 * weight including the negative totals weight-reducing gear produces.
 * `agility_level` is the BASE level (an agility potion does not make you run
 * further) and is clamped to at least 1.
 */
int
ToriRSServer_RunEnergyDrain(
    int model,
    int weight_kg,
    int agility_level);

/**
 * Energy recovered by one tick spent walking or standing. `agility_level` is
 * the base level, clamped to at least 1 as above.
 */
int
ToriRSServer_RunEnergyRestore(
    int model,
    int agility_level);

#endif
