/*
 * The two run-energy models the era feature table selects between
 * (features->run_energy_model), against the numbers their references state.
 *
 * Links nothing but the formulas and the feature table: no server, no player,
 * no cache. That is the point of torirs_server_runenergy.c existing separately —
 * "how much energy does a tick cost" is arithmetic, and arithmetic that can
 * only be observed by running a world is arithmetic nobody checks.
 *
 * References:
 *   classic  — LostCity Player.ts:705-713
 *   osrs2025 — https://oldschool.runescape.wiki/w/Run_energy (8 Jan 2025)
 */

#include "torirsserver/torirs_server_runenergy.h"

#include "features/features.h"

#include <stdio.h>

static int failures;

#define CHECK(cond, ...)                                                                  \
    do                                                                                    \
    {                                                                                     \
        if( !(cond) )                                                                     \
        {                                                                                 \
            fprintf(stderr, "FAIL: ");                                                    \
            fprintf(stderr, __VA_ARGS__);                                                 \
            fprintf(stderr, "\n");                                                        \
            failures++;                                                                   \
        }                                                                                 \
    } while( 0 )

static void
check_drain(
    int model,
    int weight_kg,
    int agility,
    int want)
{
    int got = ToriRSServer_RunEnergyDrain(model, weight_kg, agility);
    CHECK(got == want,
          "%s drain(weight=%d, agility=%d) = %d, want %d",
          ToriRS_Features_RunEnergyModelName(model),
          weight_kg,
          agility,
          got,
          want);
}

static void
check_restore(
    int model,
    int agility,
    int want)
{
    int got = ToriRSServer_RunEnergyRestore(model, agility);
    CHECK(got == want,
          "%s restore(agility=%d) = %d, want %d",
          ToriRS_Features_RunEnergyModelName(model),
          agility,
          got,
          want);
}

/* 67 + 67*kg/64, and no agility term anywhere in the drain. */
static void
test_classic_drain(void)
{
    check_drain(TORIRS_RUN_ENERGY_CLASSIC, 0, 1, 67);
    check_drain(TORIRS_RUN_ENERGY_CLASSIC, 64, 1, 134);
    check_drain(TORIRS_RUN_ENERGY_CLASSIC, 32, 1, 100);
    /* Level 99 pays exactly what level 1 pays. This is the fact the 2025
     * rework changed, so it is the one worth pinning. */
    check_drain(TORIRS_RUN_ENERGY_CLASSIC, 0, 99, 67);
    check_drain(TORIRS_RUN_ENERGY_CLASSIC, 64, 99, 134);
}

/* agility/6 + 8. */
static void
test_classic_restore(void)
{
    check_restore(TORIRS_RUN_ENERGY_CLASSIC, 1, 8);
    check_restore(TORIRS_RUN_ENERGY_CLASSIC, 60, 18);
    check_restore(TORIRS_RUN_ENERGY_CLASSIC, 99, 24);
}

/*
 * floor((60 + 67*kg/64) * (1 - agility/300)), floored once.
 *
 *   (0 kg, 1)   -> 60 * 299/300           = 59
 *   (0 kg, 99)  -> 60 * 201/300           = 40
 *   (64 kg, 1)  -> 127 * 299/300          = 126
 *   (64 kg, 99) -> 127 * 201/300          = 85
 *   (32 kg, 50) -> (60*64 + 67*32)/64 = 93.5, * 250/300 = 77.9 -> 77
 */
static void
test_osrs2025_drain(void)
{
    check_drain(TORIRS_RUN_ENERGY_OSRS_2025, 0, 1, 59);
    check_drain(TORIRS_RUN_ENERGY_OSRS_2025, 0, 99, 40);
    check_drain(TORIRS_RUN_ENERGY_OSRS_2025, 64, 1, 126);
    check_drain(TORIRS_RUN_ENERGY_OSRS_2025, 64, 99, 85);
    check_drain(TORIRS_RUN_ENERGY_OSRS_2025, 32, 50, 77);
}

/* floor(agility/10) + 15. */
static void
test_osrs2025_restore(void)
{
    check_restore(TORIRS_RUN_ENERGY_OSRS_2025, 1, 15);
    check_restore(TORIRS_RUN_ENERGY_OSRS_2025, 60, 21);
    check_restore(TORIRS_RUN_ENERGY_OSRS_2025, 99, 24);
}

/*
 * The single flooring step. Computing the weight term first — the shape the
 * classic formula has — gives floor(93) * 250/300 = 77 here too, so a case
 * where the two disagree is needed or the rounding rule is untested:
 * 63 kg at level 1 is (60*64 + 67*63)/64 = 125.95..., times 299/300.
 *   one floor : floor(125.95 * 0.99666) = floor(125.53) = 125
 *   two floors: floor(125) * 299/300    = floor(124.58) = 124
 */
static void
test_osrs2025_rounds_once(void)
{
    check_drain(TORIRS_RUN_ENERGY_OSRS_2025, 63, 1, 125);
}

/* The clamps both models share, exercised through the public entry points so a
 * caller may hand over a weight-reducing set's negative total. */
static void
test_clamps(void)
{
    check_drain(TORIRS_RUN_ENERGY_CLASSIC, -25, 1, 67);
    check_drain(TORIRS_RUN_ENERGY_CLASSIC, 1000, 1, 134);
    check_drain(TORIRS_RUN_ENERGY_OSRS_2025, -25, 1, 59);
    check_drain(TORIRS_RUN_ENERGY_OSRS_2025, 1000, 99, 85);
    /* An unset stat level reads as 0 and must not divide the drain by more
     * than a level-1 player's share. */
    check_drain(TORIRS_RUN_ENERGY_OSRS_2025, 0, 0, 59);
    check_restore(TORIRS_RUN_ENERGY_OSRS_2025, 0, 15);
}

/*
 * The flag has to be observable, not merely present: if the two models ever
 * returned the same numbers, every test above would still pass while the
 * feature field did nothing.
 */
static void
test_models_differ(void)
{
    CHECK(ToriRSServer_RunEnergyDrain(TORIRS_RUN_ENERGY_CLASSIC, 0, 99) >
              ToriRSServer_RunEnergyDrain(TORIRS_RUN_ENERGY_OSRS_2025, 0, 99),
          "a level-99 player must burn less under osrs2025 than under classic");
    CHECK(ToriRSServer_RunEnergyRestore(TORIRS_RUN_ENERGY_OSRS_2025, 1) >
              ToriRSServer_RunEnergyRestore(TORIRS_RUN_ENERGY_CLASSIC, 1),
          "a level-1 player must recover faster under osrs2025 than under classic");
}

/* The era tables' own answer, and the name round-trip the env override uses. */
static void
test_feature_table_wiring(void)
{
    CHECK(ToriRS_Features_LostCity()->run_energy_model == TORIRS_RUN_ENERGY_CLASSIC,
          "lostcity era must select the classic model");
    CHECK(ToriRS_Features_OSRS()->run_energy_model == TORIRS_RUN_ENERGY_OSRS_2025,
          "osrs era must select the 2025 model");
    CHECK(ToriRS_Features_ServerRouted()->run_energy_model == TORIRS_RUN_ENERGY_OSRS_2025,
          "server_routed era must select the 2025 model");

    CHECK(ToriRS_Features_RunEnergyModelByName("classic") == TORIRS_RUN_ENERGY_CLASSIC,
          "\"classic\" must resolve");
    CHECK(ToriRS_Features_RunEnergyModelByName("osrs2025") == TORIRS_RUN_ENERGY_OSRS_2025,
          "\"osrs2025\" must resolve");
    CHECK(ToriRS_Features_RunEnergyModelByName("osrs_2025") == -1,
          "an unknown name must report -1 rather than reading as the zero model");
    CHECK(ToriRS_Features_RunEnergyModelByName("") == -1, "an empty name must report -1");

    for( int model = TORIRS_RUN_ENERGY_CLASSIC; model <= TORIRS_RUN_ENERGY_OSRS_2025; model++ )
    {
        char const* name = ToriRS_Features_RunEnergyModelName(model);
        CHECK(ToriRS_Features_RunEnergyModelByName(name) == model,
              "model %d must round-trip through its name (\"%s\")",
              model,
              name);
    }
}

int
main(void)
{
    test_classic_drain();
    test_classic_restore();
    test_osrs2025_drain();
    test_osrs2025_restore();
    test_osrs2025_rounds_once();
    test_clamps();
    test_models_differ();
    test_feature_table_wiring();

    if( failures )
    {
        fprintf(stderr, "run_energy_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("run_energy_test: ok\n");
    return 0;
}
