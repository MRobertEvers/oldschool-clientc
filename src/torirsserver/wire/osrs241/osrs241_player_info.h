#ifndef SRC_TORIRSSERVER_WIRE_OSRS241_OSRS241_PLAYER_INFO_H
#define SRC_TORIRSSERVER_WIRE_OSRS241_OSRS241_PLAYER_INFO_H

/*
 * Revision 241's PLAYER_INFO: the shared GPI bit sections
 * (wire/core/wire_player_info.c) followed by 241's extended-info tail
 * (RSProx osrs-241 PlayerInfoClient).
 */

#include "torirsserver/wire/core/wire_player_info.h"

#include <stdint.h>

struct RsprotBuf;

/** One tick of the whole world, against the observer's GPI `state`. */
void
osrs241_player_info_write_world(
    struct RsprotBuf* buf,
    struct WirePlayerInfoState* state,
    const struct WirePlayerUpdate* updates,
    int count);

#endif
