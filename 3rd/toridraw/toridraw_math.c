#include "toridraw_math.h"

#include "graphics/shared_tables.h"

void
ToriDraw_InitMath(void)
{
    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    ToriDraw_InitTanTable();
    init_reciprocal16();
}
