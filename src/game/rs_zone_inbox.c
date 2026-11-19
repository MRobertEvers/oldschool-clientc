#include "rs_zone_inbox.h"

#include <assert.h>

void
RS_ZoneInbox_Reset(struct RS_ZoneInbox* inbox)
{
    assert(inbox);
    inbox->base_x = 0;
    inbox->base_z = 0;
    inbox->level = 0;
    inbox->held_count = 0;
}

void
RS_ZoneInbox_SetBase(
    struct RS_ZoneInbox* inbox,
    int base_x,
    int base_z,
    int level)
{
    assert(inbox);
    inbox->base_x = base_x;
    inbox->base_z = base_z;
    inbox->level = level;
}

int
RS_ZoneInbox_BaseX(struct RS_ZoneInbox const* inbox)
{
    assert(inbox);
    return inbox->base_x;
}

int
RS_ZoneInbox_BaseZ(struct RS_ZoneInbox const* inbox)
{
    assert(inbox);
    return inbox->base_z;
}

int
RS_ZoneInbox_Level(struct RS_ZoneInbox const* inbox)
{
    assert(inbox);
    return inbox->level;
}

bool
RS_ZoneInbox_IsFull(struct RS_ZoneInbox const* inbox)
{
    assert(inbox);
    return inbox->held_count >= RS_ZONE_INBOX_MAX;
}

void
RS_ZoneInbox_Hold(
    struct RS_ZoneInbox* inbox,
    struct PktZoneSubPacket const* pkt,
    int base_x,
    int base_z,
    int level,
    int view)
{
    struct RS_ZoneInboxEntry* entry;

    assert(inbox);
    assert(pkt);
    assert(inbox->held_count < RS_ZONE_INBOX_MAX);
    entry = &inbox->held[inbox->held_count];
    entry->pkt = *pkt;
    entry->base_x = base_x;
    entry->base_z = base_z;
    entry->level = level;
    entry->view = view;
    inbox->held_count++;
}

int
RS_ZoneInbox_HeldCount(struct RS_ZoneInbox const* inbox)
{
    assert(inbox);
    return inbox->held_count;
}

void
RS_ZoneInbox_Drain(
    struct RS_ZoneInbox* inbox,
    RS_ZoneInboxApplyFn apply,
    void* user)
{
    assert(inbox);
    assert(apply);
    for( int i = 0; i < inbox->held_count; i++ )
        apply(user, &inbox->held[i]);
    inbox->held_count = 0;
}

void
RS_ZoneInbox_Discard(struct RS_ZoneInbox* inbox)
{
    assert(inbox);
    inbox->held_count = 0;
}
