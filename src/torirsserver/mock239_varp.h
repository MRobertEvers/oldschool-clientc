#ifndef SRC_NET_MOCK_MOCK239_VARP_H
#define SRC_NET_MOCK_MOCK239_VARP_H

#include <rsareabuf.h>

/* Exact revision-239 VarpSmallEncoder body. */
void
mock239_encode_varp_small(
    struct RSAreaBuf* buf,
    int varp,
    int value);

#endif
