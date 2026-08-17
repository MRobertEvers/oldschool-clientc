#include "mock230_poh.h"

#include <string.h>

static int
boolean_value(int value)
{
    return value == 0 || value == 1;
}

void
mock230_poh_init(struct Mock230PohState* poh)
{
    memset(poh, 0, sizeof(*poh));
    poh->schema_version = MOCK230_POH_SCHEMA_VERSION;
    poh->grid_size = 3;
    poh->servant_last_task = -1;
    poh->family_crest = -1;
}

void
mock230_poh_reset(struct Mock230PohState* poh)
{
    mock230_poh_init(poh);
}

int
mock230_poh_get(
    const struct Mock230PohState* poh,
    int field)
{
    switch( field )
    {
    case MOCK230_POH_FIELD_SCHEMA_VERSION:
        return poh->schema_version;
    case MOCK230_POH_FIELD_OWNS_HOUSE:
        return poh->owns_house;
    case MOCK230_POH_FIELD_LOCATION:
        return poh->location;
    case MOCK230_POH_FIELD_STYLE:
        return poh->style;
    case MOCK230_POH_FIELD_LOCKED:
        return poh->locked;
    case MOCK230_POH_FIELD_DOOR_MODE:
        return poh->door_mode;
    case MOCK230_POH_FIELD_TELEPORT_INSIDE:
        return poh->teleport_inside;
    case MOCK230_POH_FIELD_DEFAULT_BUILD_MODE:
        return poh->default_build_mode;
    case MOCK230_POH_FIELD_GRID_SIZE:
        return poh->grid_size;
    case MOCK230_POH_FIELD_SERVANT_TYPE:
        return poh->servant_type;
    case MOCK230_POH_FIELD_SERVANT_PAID:
        return poh->servant_paid;
    case MOCK230_POH_FIELD_SERVANT_LAST_TASK:
        return poh->servant_last_task;
    case MOCK230_POH_FIELD_MONEY_BAG:
        return poh->money_bag;
    case MOCK230_POH_FIELD_FAMILY_CREST:
        return poh->family_crest;
    default:
        return 0;
    }
}

int
mock230_poh_set(
    struct Mock230PohState* poh,
    int field,
    int value)
{
    switch( field )
    {
    case MOCK230_POH_FIELD_OWNS_HOUSE:
        if( !boolean_value(value) )
            return 0;
        poh->owns_house = value;
        return 1;
    case MOCK230_POH_FIELD_LOCATION:
        if( value < 0 || value > 31 )
            return 0;
        poh->location = value;
        return 1;
    case MOCK230_POH_FIELD_STYLE:
        if( value < 0 || value > 31 )
            return 0;
        poh->style = value;
        return 1;
    case MOCK230_POH_FIELD_LOCKED:
        if( !boolean_value(value) )
            return 0;
        poh->locked = value;
        return 1;
    case MOCK230_POH_FIELD_DOOR_MODE:
        if( value < 0 || value > 2 )
            return 0;
        poh->door_mode = value;
        return 1;
    case MOCK230_POH_FIELD_TELEPORT_INSIDE:
        if( !boolean_value(value) )
            return 0;
        poh->teleport_inside = value;
        return 1;
    case MOCK230_POH_FIELD_DEFAULT_BUILD_MODE:
        if( !boolean_value(value) )
            return 0;
        poh->default_build_mode = value;
        return 1;
    case MOCK230_POH_FIELD_GRID_SIZE:
        if( value < 3 || value > 7 )
            return 0;
        poh->grid_size = value;
        return 1;
    case MOCK230_POH_FIELD_SERVANT_TYPE:
        if( value < 0 || value > 15 )
            return 0;
        poh->servant_type = value;
        return 1;
    case MOCK230_POH_FIELD_SERVANT_PAID:
        if( value < 0 )
            return 0;
        poh->servant_paid = value;
        return 1;
    case MOCK230_POH_FIELD_SERVANT_LAST_TASK:
        if( value < -1 )
            return 0;
        poh->servant_last_task = value;
        return 1;
    case MOCK230_POH_FIELD_MONEY_BAG:
        if( value < 0 )
            return 0;
        poh->money_bag = value;
        return 1;
    case MOCK230_POH_FIELD_FAMILY_CREST:
        if( value < -1 || value > 15 )
            return 0;
        poh->family_crest = value;
        return 1;
    case MOCK230_POH_FIELD_SCHEMA_VERSION:
    default:
        return 0;
    }
}

int
mock230_poh_room_add(
    struct Mock230PohState* poh,
    int dbrow,
    int x,
    int z,
    int level,
    int rotation,
    int door_mask)
{
    struct Mock230PohRoom* room;
    int index;

    if( poh->room_count < 0 || poh->room_count >= MOCK230_POH_ROOM_MAX )
        return -1;
    if( dbrow < 0 || x < 0 || x >= MOCK230_POH_GRID_MAX || z < 0 ||
        z >= MOCK230_POH_GRID_MAX || level < 0 || level > 3 || rotation < 0 ||
        rotation > 3 || door_mask < 0 || door_mask > 15 )
        return -1;
    for( int i = 0; i < poh->room_count; i++ )
    {
        if( poh->rooms[i].x == x && poh->rooms[i].z == z &&
            poh->rooms[i].level == level )
            return -1;
    }

    index = poh->room_count++;
    room = &poh->rooms[index];
    room->dbrow = dbrow;
    room->x = x;
    room->z = z;
    room->level = level;
    room->rotation = rotation;
    room->door_mask = door_mask;
    return index;
}

int
mock230_poh_room_get(
    const struct Mock230PohState* poh,
    int room,
    int field)
{
    const struct Mock230PohRoom* value;

    if( room < 0 || room >= poh->room_count )
        return -1;
    value = &poh->rooms[room];
    switch( field )
    {
    case MOCK230_POH_ROOM_DBROW:
        return value->dbrow;
    case MOCK230_POH_ROOM_X:
        return value->x;
    case MOCK230_POH_ROOM_Z:
        return value->z;
    case MOCK230_POH_ROOM_LEVEL:
        return value->level;
    case MOCK230_POH_ROOM_ROTATION:
        return value->rotation;
    case MOCK230_POH_ROOM_DOOR_MASK:
        return value->door_mask;
    default:
        return -1;
    }
}

int
mock230_poh_room_set(
    struct Mock230PohState* poh,
    int room,
    int field,
    int value)
{
    struct Mock230PohRoom candidate;

    if( room < 0 || room >= poh->room_count )
        return 0;
    candidate = poh->rooms[room];
    switch( field )
    {
    case MOCK230_POH_ROOM_DBROW:
        if( value < 0 )
            return 0;
        candidate.dbrow = value;
        break;
    case MOCK230_POH_ROOM_X:
        if( value < 0 || value >= MOCK230_POH_GRID_MAX )
            return 0;
        candidate.x = value;
        break;
    case MOCK230_POH_ROOM_Z:
        if( value < 0 || value >= MOCK230_POH_GRID_MAX )
            return 0;
        candidate.z = value;
        break;
    case MOCK230_POH_ROOM_LEVEL:
        if( value < 0 || value > 3 )
            return 0;
        candidate.level = value;
        break;
    case MOCK230_POH_ROOM_ROTATION:
        if( value < 0 || value > 3 )
            return 0;
        candidate.rotation = value;
        break;
    case MOCK230_POH_ROOM_DOOR_MASK:
        if( value < 0 || value > 15 )
            return 0;
        candidate.door_mask = value;
        break;
    default:
        return 0;
    }

    for( int i = 0; i < poh->room_count; i++ )
    {
        if( i == room )
            continue;
        if( poh->rooms[i].x == candidate.x && poh->rooms[i].z == candidate.z &&
            poh->rooms[i].level == candidate.level )
            return 0;
    }
    poh->rooms[room] = candidate;
    return 1;
}

int
mock230_poh_room_remove(
    struct Mock230PohState* poh,
    int room)
{
    int write = 0;

    if( room < 0 || room >= poh->room_count )
        return 0;

    /* Decorations name their owning room by slot. Drop the removed room's
     * decorations and shift later references alongside the compacted room
     * array. */
    for( int read = 0; read < poh->decoration_count; read++ )
    {
        struct Mock230PohDecoration decoration = poh->decorations[read];

        if( decoration.room == room )
            continue;
        if( decoration.room > room )
            decoration.room--;
        poh->decorations[write++] = decoration;
    }
    if( write < poh->decoration_count )
        memset(&poh->decorations[write], 0,
               (size_t)(poh->decoration_count - write) *
                   sizeof(poh->decorations[0]));
    poh->decoration_count = write;

    for( int i = room; i + 1 < poh->room_count; i++ )
        poh->rooms[i] = poh->rooms[i + 1];
    memset(&poh->rooms[poh->room_count - 1], 0, sizeof(poh->rooms[0]));
    poh->room_count--;
    return 1;
}

int
mock230_poh_decoration_set(
    struct Mock230PohState* poh,
    int room,
    int hotspot,
    int furniture_dbrow,
    int rotation,
    int flags)
{
    int found = -1;

    if( room < 0 || room >= poh->room_count || hotspot < 0 || hotspot > 255 ||
        rotation < 0 || rotation > 3 || flags < 0 )
        return 0;
    for( int i = 0; i < poh->decoration_count; i++ )
    {
        if( poh->decorations[i].room == room && poh->decorations[i].hotspot == hotspot )
        {
            found = i;
            break;
        }
    }
    if( furniture_dbrow < 0 )
    {
        if( found < 0 )
            return 1;
        poh->decorations[found] = poh->decorations[poh->decoration_count - 1];
        memset(&poh->decorations[poh->decoration_count - 1], 0,
               sizeof(poh->decorations[0]));
        poh->decoration_count--;
        return 1;
    }
    if( found < 0 )
    {
        if( poh->decoration_count >= MOCK230_POH_DECORATION_MAX )
            return 0;
        found = poh->decoration_count++;
    }
    poh->decorations[found].room = room;
    poh->decorations[found].hotspot = hotspot;
    poh->decorations[found].furniture_dbrow = furniture_dbrow;
    poh->decorations[found].rotation = rotation;
    poh->decorations[found].flags = flags;
    return 1;
}

int
mock230_poh_decoration_get(
    const struct Mock230PohState* poh,
    int room,
    int hotspot,
    int field)
{
    const struct Mock230PohDecoration* value = NULL;

    for( int i = 0; i < poh->decoration_count; i++ )
    {
        if( poh->decorations[i].room == room && poh->decorations[i].hotspot == hotspot )
        {
            value = &poh->decorations[i];
            break;
        }
    }
    if( !value )
        return -1;
    switch( field )
    {
    case MOCK230_POH_DECOR_FURNITURE_DBROW:
        return value->furniture_dbrow;
    case MOCK230_POH_DECOR_ROTATION:
        return value->rotation;
    case MOCK230_POH_DECOR_FLAGS:
        return value->flags;
    default:
        return -1;
    }
}

int
mock230_poh_validate(const struct Mock230PohState* poh)
{
    if( poh->schema_version != MOCK230_POH_SCHEMA_VERSION ||
        !boolean_value(poh->owns_house) || poh->location < 0 || poh->location > 31 ||
        poh->style < 0 || poh->style > 31 || !boolean_value(poh->locked) ||
        poh->door_mode < 0 || poh->door_mode > 2 ||
        !boolean_value(poh->teleport_inside) ||
        !boolean_value(poh->default_build_mode) || poh->grid_size < 3 ||
        poh->grid_size > 7 || poh->servant_type < 0 || poh->servant_type > 15 ||
        poh->servant_paid < 0 || poh->servant_last_task < -1 || poh->money_bag < 0 ||
        poh->family_crest < -1 || poh->family_crest > 15 ||
        poh->room_count < 0 || poh->room_count > MOCK230_POH_ROOM_MAX ||
        poh->decoration_count < 0 ||
        poh->decoration_count > MOCK230_POH_DECORATION_MAX )
        return 0;

    for( int i = 0; i < poh->room_count; i++ )
    {
        const struct Mock230PohRoom* room = &poh->rooms[i];

        if( room->dbrow < 0 || room->x < 0 || room->x >= MOCK230_POH_GRID_MAX ||
            room->z < 0 || room->z >= MOCK230_POH_GRID_MAX || room->level < 0 ||
            room->level > 3 || room->rotation < 0 || room->rotation > 3 ||
            room->door_mask < 0 || room->door_mask > 15 )
            return 0;
        for( int j = 0; j < i; j++ )
        {
            if( poh->rooms[j].x == room->x && poh->rooms[j].z == room->z &&
                poh->rooms[j].level == room->level )
                return 0;
        }
    }
    for( int i = 0; i < poh->decoration_count; i++ )
    {
        const struct Mock230PohDecoration* decoration = &poh->decorations[i];

        if( decoration->room < 0 || decoration->room >= poh->room_count ||
            decoration->hotspot < 0 || decoration->hotspot > 255 ||
            decoration->furniture_dbrow < 0 || decoration->rotation < 0 ||
            decoration->rotation > 3 || decoration->flags < 0 )
            return 0;
        for( int j = 0; j < i; j++ )
        {
            if( poh->decorations[j].room == decoration->room &&
                poh->decorations[j].hotspot == decoration->hotspot )
                return 0;
        }
    }
    return 1;
}
