#ifndef OSRS_REVS_LC245_2_REVISION_LC245_2_H
#define OSRS_REVS_LC245_2_REVISION_LC245_2_H

#include "osrs/core/revision.h"
#include "osrs/core/serverprot_packets.h"

struct GGame;

struct RevServerProtPacket_Item
{
    struct RevServerProtPacket packet;

    struct RevServerProtPacket_Item* next_nullable;
};

struct RevisionLC245_2
{
    struct RevServerProtPacket_Item* pending_head;
};

/** Allocate LC245_2 revision state; `impl` is a `struct RevisionLC245_2*`. */
struct Revision
revision_lc245_2_new(void);

void
revision_lc245_2_free(struct Revision* rev);

bool
gameproto_rev245_2_has_pending(struct RevisionLC245_2* self);

void
gameproto_rev245_2_enqueue(
    struct RevisionLC245_2* self,
    struct RevServerProtPacket* packet);

/** Run exec for every queued packet and free queue nodes. */
void
gameproto_rev245_2_drain_pending(
    struct RevisionLC245_2* self,
    struct GGame* game);

#endif
