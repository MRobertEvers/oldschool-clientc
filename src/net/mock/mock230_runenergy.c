#include "mock230_runenergy.h"

#include "features/features.h"

#include <assert.h>

/* The ceiling both models share: past 64 kg more weight costs nothing. */
enum
{
    RUN_ENERGY_WEIGHT_MAX_KG = 64,
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
mock230_run_energy_drain(
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
         * floor((60 + 67 * weight / 64) * (1 - agility / 300)).
         *
         * Scaled by 64 * 300 and floored ONCE at the end, which is how the
         * wiki writes it: flooring the weight term first (the shape the
         * classic line below happens to have) rounds twice and loses up to a
         * unit a tick, which is a couple of seconds of running over a full
         * bar.
         */
        int scaled = (60 * 64 + 67 * weight) * (300 - agility);
        return scaled / (64 * 300);
    }

    /* LostCity: 67..134 across the weight range, and no agility term at all. */
    return 67 + (67 * weight) / 64;
}

int
mock230_run_energy_restore(
    int model,
    int agility_level)
{
    int agility = clamp_agility(agility_level);

    assert(model == TORIRS_RUN_ENERGY_CLASSIC || model == TORIRS_RUN_ENERGY_OSRS_2025);

    if( model == TORIRS_RUN_ENERGY_OSRS_2025 )
        return agility / 10 + 15;
    return agility / 6 + 8;
}
