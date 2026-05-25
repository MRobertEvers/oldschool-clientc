#include "toridraw_math.h"

#include "graphics/shared_tables.h"

void
toridraw_init_math(void)
{
    init_sin_table();
    init_cos_table();
    init_tan_table();
    init_reciprocal16();
}
