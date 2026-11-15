#ifndef SRC_TORIRSSERVER_WIRE_OSRS239_OSRS239_VARP_H
#define SRC_TORIRSSERVER_WIRE_OSRS239_OSRS239_VARP_H

#include "rsprot_buf.h"

/* Exact revision-239 VarpSmallEncoder body. */
void
osrs239_encode_varp_small(
    struct RsprotBuf* buf,
    int varp,
    int value);

#endif
