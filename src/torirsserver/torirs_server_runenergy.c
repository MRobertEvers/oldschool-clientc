#include "torirs_server_runenergy.h"

#include "features/features.h"

#include <assert.h>

/* The ceiling both models share: past 64 kg more weight costs nothing. */
enum
{
    RUN_ENERGY_WEIGHT_MAX_KG = 64,
    /* Wiki Graceful_outfit: the six pieces add 20% between them and wearing
     * all six adds another 10%. */
    RUN_ENERGY_GRACEFUL_SET_PIECES = 6,
    RUN_ENERGY_GRACEFUL_SET_BONUS = 10,
};

static int
clamp_weight_kg(int weight_kg)
{
    if( weight_kg < 0 )
        return 0;
    if( weight_kg > RUN_ENERGY_WEIGHT_MAX_KG )
        return RUN_ENERGY_WEIGHT_MAX_KG;
    return weight_kg;
}

static int
clamp_agility(int agility_level)
{
    return agility_level < 1 ? 1 : agility_level;
}

int
ToriRSServer_RunEnergyDrain(
    int model,
    int weight_kg,
    int agility_level)
{
    int weight = clamp_weight_kg(weight_kg);
    int agility = clamp_agility(agility_level);

    assert(model == TORIRS_RUN_ENERGY_CLASSIC || model == TORIRS_RUN_ENERGY_OSRS_2025);

    if( model == TORIRS_RUN_ENERGY_OSRS_2025 )
    {
        /*
         * floor(floor(60 + 67 * weight / 64) * (1 - agility / 300)).
         *
         * Floored TWICE, the weight term first. The wiki writes the floor
         * around `60 + 67*weight/64` alone (Energy, "UnitsLost"), and
         * RuneLite's run-energy plugin computes the same term with integer
         * division before scaling by Agility. This used to floor once at the
         * end, which reads up to a unit a tick high: 63 kg at level 1 cost
         * 125 where OldSchool charges 124.
         */
        int base = 60 + (67 * weight) / 64;
        return base * (300 - agility) / 300;
    }

    /* LostCity: 67..134 across the weight range, and no agility term at all. */
    return 67 + (67 * weight) / 64;
}

int
ToriRSServer_RunEnergyRestore(
    int model,
    int agility_level)
{
    int agility = clamp_agility(agility_level);

    assert(model == TORIRS_RUN_ENERGY_CLASSIC || model == TORIRS_RUN_ENERGY_OSRS_2025);

    if( model == TORIRS_RUN_ENERGY_OSRS_2025 )
        return agility / 10 + 15;
    return agility / 6 + 8;
}

int
ToriRSServer_RunEnergyDrainModified(
    int drain,
    int stamina_active,
    int ring_of_endurance_passive)
{
    assert(drain >= 0);

    /* Wiki Energy: floor(0.3 * UnitsLost) under a stamina dose, else
     * floor(0.85 * UnitsLost) with a ring of endurance of 500+ charges. The
     * ring "does not stack with the stamina potion effect" (wiki
     * Ring_of_endurance), so the potion wins outright. */
    if( stamina_active )
        return drain * 3 / 10;
    if( ring_of_endurance_passive )
        return drain * 85 / 100;
    return drain;
}

int
ToriRSServer_RunEnergyGracefulBoost(
    int piece_percent,
    int pieces)
{
    assert(piece_percent >= 0);
    assert(pieces >= 0);

    if( pieces >= RUN_ENERGY_GRACEFUL_SET_PIECES )
        return piece_percent + RUN_ENERGY_GRACEFUL_SET_BONUS;
    return piece_percent;
}

int
ToriRSServer_RunEnergyRestoreBoosted(
    int restore,
    int boost_percent)
{
    assert(restore >= 0);
    assert(boost_percent >= 0);

    /* Wiki Energy: floor(1.3 * R) with the full set, the same floor for a
     * partial one. */
    return restore * (100 + boost_percent) / 100;
}
