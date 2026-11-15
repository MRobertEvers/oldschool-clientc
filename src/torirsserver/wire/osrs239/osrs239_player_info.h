#ifndef SRC_TORIRSSERVER_WIRE_OSRS239_OSRS239_PLAYER_INFO_H
#define SRC_TORIRSSERVER_WIRE_OSRS239_OSRS239_PLAYER_INFO_H

/*
 * Revision 239's PLAYER_INFO: the shared GPI bit sections
 * (wire/core/wire_player_info.c) followed by 239's extended-info tail.
 */

#include "torirsserver/wire/core/wire_player_info.h"

#include <stdint.h>

struct RsprotBuf;

/** One tick carrying only the local player (see wire_player_info_write_local). */
void
osrs239_player_info_write(
    struct RsprotBuf* buf,
    int local_index,
    enum WirePlayerMovement movement,
    int32_t movement_value,
    int low_res_inactive,
    const uint8_t* appearance,
    int appearance_len,
    const struct WirePlayerExt* ext);

/** One tick of the whole world, against the observer's GPI `state`. */
void
osrs239_player_info_write_world(
    struct RsprotBuf* buf,
    struct WirePlayerInfoState* state,
    const struct WirePlayerUpdate* updates,
    int count);

#endif
