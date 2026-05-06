#include "osrs/revs/lc245_2/revision_lc245_2.h"

#include "osrs/game.h"
#include "osrs/revs/lc245_2/gamenet_rev245_2_exec.h"
#include "osrs/revs/lc245_2/serverprot_netrev245_2_parse.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Revision
revision_lc245_2_new(void)
{
    struct RevisionLC245_2* impl = calloc(1, sizeof(struct RevisionLC245_2));
    struct Revision r = { REVISION_KIND_LC245_2, impl };
    return r;
}

void
revision_lc245_2_free(struct Revision* rev)
{
    if( !rev || rev->kind != REVISION_KIND_LC245_2 || !rev->impl )
        return;

    struct RevisionLC245_2* self = (struct RevisionLC245_2*)rev->impl;

    while( self->pending_head )
    {
        struct RevServerProtPacket_Item* item = self->pending_head;
        self->pending_head = item->next_nullable;
        gameproto_free_lc245_2_item(item);
        free(item);
    }

    free(self);
    rev->impl = NULL;
    rev->kind = REVISION_KIND_INVALID;
}

bool
gameproto_rev245_2_has_pending(struct RevisionLC245_2* self)
{
    return self && self->pending_head != NULL;
}

void
gameproto_rev245_2_enqueue(
    struct RevisionLC245_2* self,
    struct RevServerProtPacket* packet)
{
    assert(self && packet);

    struct RevServerProtPacket_Item* item = malloc(sizeof(struct RevServerProtPacket_Item));
    memset(item, 0, sizeof(struct RevServerProtPacket_Item));

    item->packet = *packet;
    if( !self->pending_head )
    {
        self->pending_head = item;
    }
    else
    {
        struct RevServerProtPacket_Item* list = self->pending_head;
        while( list->next_nullable )
        {
            list = list->next_nullable;
        }
        list->next_nullable = item;
    }
}

void
gameproto_rev245_2_drain_pending(
    struct RevisionLC245_2* self,
    struct GGame* game)
{
    if( !self || !game )
        return;

    while( self->pending_head )
    {
        struct RevServerProtPacket_Item* item = self->pending_head;
        self->pending_head = item->next_nullable;
        item->next_nullable = NULL;

        gamenet_rev245_2_exec_dispatch_v1(game, &item->packet);
        gameproto_free_lc245_2_item(item);
        free(item);
    }
}
