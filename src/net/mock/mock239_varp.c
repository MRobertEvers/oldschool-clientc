#include "mock239_varp.h"

/* VarpSmallEncoder: p1Alt1(value), p2Alt3(id). */
void
mock239_encode_varp_small(
    struct RSAreaBuf* buf,
    int varp,
    int value)
{
    rsab_p1_alt1(buf, value);
    rsab_p2_alt3(buf, varp);
}
