#include <rscache.h>
#include <toridraw.h>

#include <assert.h>
#include <stdio.h>

int
main(void)
{
    ToriDraw_InitMath();

    const int* toridraw_builtin = ToriDraw_GetCosTable();
    const int* rscache_builtin = RSCache_NoiseGetCosTable();

    assert(toridraw_builtin != NULL);
    assert(rscache_builtin != NULL);
    assert(toridraw_builtin[0] == 65536);
    assert(rscache_builtin[0] == 65536);

    ToriDraw_SetCosTable(rscache_builtin);
    assert(ToriDraw_GetCosTable() == rscache_builtin);
    assert(ToriDraw_ReadCosTable(0) == rscache_builtin[0]);
    assert(ToriDraw_Cos(0) == rscache_builtin[0]);

    ToriDraw_InitCosTable();
    assert(ToriDraw_GetCosTable() == toridraw_builtin);

    RSCache_NoiseSetCosTable(toridraw_builtin);
    assert(RSCache_NoiseGetCosTable() == toridraw_builtin);
    (void)RSCache_NoisePerlinNoise(100, 200, 64);

    RSCache_NoiseSetCosTable(NULL);
    assert(RSCache_NoiseGetCosTable() == rscache_builtin);

    puts("trig_tables_share: ok");
    return 0;
}
