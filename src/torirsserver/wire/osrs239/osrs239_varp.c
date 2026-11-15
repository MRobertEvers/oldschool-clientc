#include "torirsserver/wire/osrs239/osrs239_varp.h"

/* VarpSmallEncoder: p1Alt1(value), p2Alt3(id). */
void
osrs239_encode_varp_small(
    struct RsprotBuf* buf,
    int varp,
    int value)
{
    rsprot_p1_add128(buf, value);
    rsprot_p2_21_add128(buf, varp);
}
