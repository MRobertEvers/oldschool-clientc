#ifndef SRC_NET_REV_RSPROT_ENTITY_STATE_H
#define SRC_NET_REV_RSPROT_ENTITY_STATE_H

/*
 * The session's OSRS player table, for every revision table whose PLAYER_INFO /
 * NPC_INFO readers are 3rd/rsprot/streams' (<prot>_osrs<N>): the data-driven
 * family (net/rev/rsprox/) and the osrs239 / osrs241 tables. The readers write
 * the list the entity-info executor reads; this file only holds the table they
 * walk and hands the right one to them.
 *
 * One table per process, as there is one session: a host that decodes several
 * sessions parks and restores it (rsprot_entity_state_save / _load).
 */

#include "streams/rsprot_info.h"

#include <stddef.h>
#include <stdint.h>

/** PLAYER_INFO through `read` (a player_info_osrs<N>), over the table. */
int
rsprot_entity_player_read(
    RsprotPlayerInfoReadFn read,
    uint8_t const* data,
    int len,
    RsprotInfoList* out);

/** NPC_INFO through `read` (an npc_info_<size>_<version>_osrs<N>). There is no
 *  npc type lookup: a CUSTOM_TYPED block with recolours or retextures ends the
 *  tail, as it did in the hand-written readers. */
int
rsprot_entity_npc_read(
    RsprotNpcInfoReadFn read,
    uint8_t const* data,
    int len,
    RsprotInfoList* out);

/** A fresh table owned by `local_index` (the login's player index). */
void
rsprot_entity_set_local(int local_index);

/** Seed the table from the GPI init block (the login REBUILD's front, or
 *  RECONNECT_OK's body). */
void
rsprot_entity_init(
    uint8_t const* data,
    int len);

size_t
rsprot_entity_state_size(void);
void
rsprot_entity_state_save(void* out);
void
rsprot_entity_state_load(void const* in);

#endif
