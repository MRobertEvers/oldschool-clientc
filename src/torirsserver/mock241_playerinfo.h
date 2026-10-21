#ifndef SRC_TORIRSSERVER_MOCK241_PLAYERINFO_H
#define SRC_TORIRSSERVER_MOCK241_PLAYERINFO_H

/*
 * Revision 241's PLAYER_INFO extended-info tail (RSProx osrs-241
 * PlayerInfoClient). The GPI bit sections in front of it did not change and are
 * still mock239_playerinfo.c's; mock239_playerinfo_write_world calls this
 * instead of its own 239 tail when the state was armed for 241
 * (`Mock239PlayerInfoState.wire_revision`).
 */

#include "mock239_playerinfo.h"

#include <stdint.h>

struct RSAreaBuf;

void
mock241_write_player_extended(
    struct RSAreaBuf* buf,
    const uint8_t* appearance,
    int appearance_len,
    const struct Mock239PlayerExt* ext);

#endif
