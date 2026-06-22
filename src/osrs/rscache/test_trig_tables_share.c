#include "dat2a/dat2a_noise.h"
#include "graphics/shared_tables.h"
#include "toridraw_math.h"

#include <assert.h>
#include <stdio.h>

int
main(void)
{
    ToriDraw_InitMath();

    const int* td_cos = ToriDraw_GetCosTable();
    const int* rs_cos_builtin = RSCacheDat2A_NoiseGetCosTable();

    assert(td_cos != NULL);
    assert(rs_cos_builtin != NULL);
    assert(td_cos[0] == 65536);
    assert(rs_cos_builtin[0] == 65536);

    RSCacheDat2A_NoiseSetCosTable(td_cos);
    assert(RSCacheDat2A_NoiseGetCosTable() == td_cos);
    int noise_with_shared_a = RSCacheDat2A_NoisePerlinNoise(100, 200, 64);
    int noise_with_shared_b = RSCacheDat2A_NoisePerlinNoise(100, 200, 64);
    assert(noise_with_shared_a == noise_with_shared_b);

    RSCacheDat2A_NoiseSetCosTable(NULL);
    assert(RSCacheDat2A_NoiseGetCosTable() == rs_cos_builtin);

    ToriDraw_SetCosTable(RSCacheDat2A_NoiseGetCosTable());
    assert(ToriDraw_GetCosTable() == RSCacheDat2A_NoiseGetCosTable());

    ToriDraw_SetSinTable(NULL);
    ToriDraw_SetCosTable(NULL);
    ToriDraw_SetTanTable(NULL);
    assert(ToriDraw_GetCosTable() == td_cos);

    printf("trig_tables_share: ok\n");
    return 0;
}
