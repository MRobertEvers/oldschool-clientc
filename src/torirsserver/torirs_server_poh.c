#include "torirs_server_poh.h"

#include <string.h>

static int
boolean_value(int value)
{
    return value == 0 || value == 1;
}

static int
servant_type_value(int value)
{
    /* Exact poh_servant_type cache-varbit ordinals. */
    return value == 0 || value == 1 || value == 3 || value == 5 ||
           value == 6 || value == 8;
}

void
ToriRSServer_PohInit(struct ToriRSServerPohState* poh)
{
    memset(poh, 0, sizeof(*poh));
    poh->schema_version = TORIRSSERVER_POH_SCHEMA_VERSION;
    poh->grid_size = 3;
    poh->servant_last_task = -1;
    poh->family_crest = -1;
    poh->tip_notify = 1;
}

void
ToriRSServer_PohReset(struct ToriRSServerPohState* poh)
{
    ToriRSServer_PohInit(poh);
}

int
ToriRSServer_PohGet(
    const struct ToriRSServerPohState* poh,
    int field)
{
    switch( field )
    {
    case TORIRSSERVER_POH_FIELD_SCHEMA_VERSION:
        return poh->schema_version;
    case TORIRSSERVER_POH_FIELD_OWNS_HOUSE:
        return poh->owns_house;
    case TORIRSSERVER_POH_FIELD_LOCATION:
        return poh->location;
    case TORIRSSERVER_POH_FIELD_STYLE:
        return poh->style;
    case TORIRSSERVER_POH_FIELD_LOCKED:
        return poh->locked;
    case TORIRSSERVER_POH_FIELD_DOOR_MODE:
        return poh->door_mode;
    case TORIRSSERVER_POH_FIELD_TELEPORT_INSIDE:
        return poh->teleport_inside;
    case TORIRSSERVER_POH_FIELD_DEFAULT_BUILD_MODE:
        return poh->default_build_mode;
    case TORIRSSERVER_POH_FIELD_GRID_SIZE:
        return poh->grid_size;
    case TORIRSSERVER_POH_FIELD_SERVANT_TYPE:
        return poh->servant_type;
    case TORIRSSERVER_POH_FIELD_SERVANT_PAID:
        return poh->servant_paid;
    case TORIRSSERVER_POH_FIELD_SERVANT_LAST_TASK:
        return poh->servant_last_task;
    case TORIRSSERVER_POH_FIELD_MONEY_BAG:
        return poh->money_bag;
    case TORIRSSERVER_POH_FIELD_FAMILY_CREST:
        return poh->family_crest;
    case TORIRSSERVER_POH_FIELD_HEAD_TROPHIES:
        return poh->head_trophies;
    case TORIRSSERVER_POH_FIELD_FISH_TROPHIES:
        return poh->fish_trophies;
    case TORIRSSERVER_POH_FIELD_SPICE_RED:
        return poh->spice_red;
    case TORIRSSERVER_POH_FIELD_SPICE_ORANGE:
        return poh->spice_orange;
    case TORIRSSERVER_POH_FIELD_SPICE_BROWN:
        return poh->spice_brown;
    case TORIRSSERVER_POH_FIELD_SPICE_YELLOW:
        return poh->spice_yellow;
    case TORIRSSERVER_POH_FIELD_TIP_COINS:
        return poh->tip_coins;
    case TORIRSSERVER_POH_FIELD_TIP_PLATINUM:
        return poh->tip_platinum;
    case TORIRSSERVER_POH_FIELD_TIP_NOTIFY:
        return poh->tip_notify;
    case TORIRSSERVER_POH_FIELD_TIP_AUTO_BANK:
        return poh->tip_auto_bank;
    case TORIRSSERVER_POH_FIELD_TREASURE_COINS:
        return poh->treasure_coins;
    case TORIRSSERVER_POH_FIELD_TREASURE_READY_MINUTE:
        return poh->treasure_ready_minute;
    case TORIRSSERVER_POH_FIELD_BOSS_JARS:
        return poh->boss_jars;
    case TORIRSSERVER_POH_FIELD_DUMMY_VARIANTS:
        return poh->dummy_variants;
    case TORIRSSERVER_POH_FIELD_GAMES_PRIZE_COINS:
        return poh->games_prize_coins;
    case TORIRSSERVER_POH_FIELD_STYLE_UNLOCKS:
        return poh->style_unlocks;
    default:
        return 0;
    }
}

int
ToriRSServer_PohSet(
    struct ToriRSServerPohState* poh,
    int field,
    int value)
{
    switch( field )
    {
    case TORIRSSERVER_POH_FIELD_OWNS_HOUSE:
        if( !boolean_value(value) )
            return 0;
        poh->owns_house = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_LOCATION:
        if( value < 0 || value > 31 )
            return 0;
        poh->location = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_STYLE:
        if( value < 0 || value > TORIRSSERVER_POH_STYLE_MAX )
            return 0;
        poh->style = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_LOCKED:
        if( !boolean_value(value) )
            return 0;
        poh->locked = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_DOOR_MODE:
        if( value < 0 || value > 2 )
            return 0;
        poh->door_mode = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_TELEPORT_INSIDE:
        if( !boolean_value(value) )
            return 0;
        poh->teleport_inside = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_DEFAULT_BUILD_MODE:
        if( !boolean_value(value) )
            return 0;
        poh->default_build_mode = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_GRID_SIZE:
        if( value < 3 || value > 7 )
            return 0;
        poh->grid_size = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_SERVANT_TYPE:
        if( !servant_type_value(value) )
            return 0;
        poh->servant_type = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_SERVANT_PAID:
        if( value < 0 || value > TORIRSSERVER_POH_SERVANT_SERVICE_MAX )
            return 0;
        poh->servant_paid = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_SERVANT_LAST_TASK:
        if( value < -1 || value > TORIRSSERVER_POH_SERVANT_TASK_MAX )
            return 0;
        poh->servant_last_task = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_MONEY_BAG:
        if( value < 0 || value > TORIRSSERVER_POH_MONEY_BAG_MAX )
            return 0;
        poh->money_bag = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_FAMILY_CREST:
        if( value < -1 || value > 15 )
            return 0;
        poh->family_crest = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_HEAD_TROPHIES:
        if( value < 0 || value > 0x1ff )
            return 0;
        poh->head_trophies = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_FISH_TROPHIES:
        if( value < 0 || value > 0xf )
            return 0;
        poh->fish_trophies = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_SPICE_RED:
        if( value < 0 )
            return 0;
        poh->spice_red = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_SPICE_ORANGE:
        if( value < 0 )
            return 0;
        poh->spice_orange = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_SPICE_BROWN:
        if( value < 0 )
            return 0;
        poh->spice_brown = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_SPICE_YELLOW:
        if( value < 0 )
            return 0;
        poh->spice_yellow = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_TIP_COINS:
        if( value < 0 )
            return 0;
        poh->tip_coins = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_TIP_PLATINUM:
        if( value < 0 )
            return 0;
        poh->tip_platinum = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_TIP_NOTIFY:
        if( !boolean_value(value) )
            return 0;
        poh->tip_notify = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_TIP_AUTO_BANK:
        if( !boolean_value(value) )
            return 0;
        poh->tip_auto_bank = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_TREASURE_COINS:
        if( value < 0 || value > TORIRSSERVER_POH_TREASURE_MAX )
            return 0;
        poh->treasure_coins = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_TREASURE_READY_MINUTE:
        if( value < 0 )
            return 0;
        poh->treasure_ready_minute = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_BOSS_JARS:
        if( value < 0 || value > TORIRSSERVER_POH_BOSS_JAR_MASK_MAX )
            return 0;
        poh->boss_jars = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_DUMMY_VARIANTS:
        if( value < 0 || value > TORIRSSERVER_POH_DUMMY_VARIANT_MASK_MAX )
            return 0;
        poh->dummy_variants = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_GAMES_PRIZE_COINS:
        if( value < 0 || value > TORIRSSERVER_POH_GAMES_PRIZE_MAX )
            return 0;
        poh->games_prize_coins = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_STYLE_UNLOCKS:
        if( value < 0 || value > TORIRSSERVER_POH_STYLE_UNLOCK_MASK_MAX )
            return 0;
        poh->style_unlocks = value;
        return 1;
    case TORIRSSERVER_POH_FIELD_SCHEMA_VERSION:
    default:
        return 0;
    }
}

int
ToriRSServer_PohRoomAdd(
    struct ToriRSServerPohState* poh,
    int dbrow,
    int x,
    int z,
    int level,
    int rotation,
    int door_mask)
{
    struct ToriRSServerPohRoom* room;
    int index;

    if( poh->room_count < 0 || poh->room_count >= TORIRSSERVER_POH_ROOM_MAX )
        return -1;
    if( dbrow < 0 || x < 0 || x >= TORIRSSERVER_POH_GRID_MAX || z < 0 ||
        z >= TORIRSSERVER_POH_GRID_MAX || level < 0 ||
        level > TORIRSSERVER_POH_UPPER_LEVEL || rotation < 0 ||
        rotation > 3 || door_mask < 0 || door_mask > 15 )
        return -1;
    for( int i = 0; i < poh->room_count; i++ )
    {
        if( poh->rooms[i].x == x && poh->rooms[i].z == z &&
            poh->rooms[i].level == level )
            return -1;
    }
    if( level == TORIRSSERVER_POH_UPPER_LEVEL )
    {
        int supported = 0;

        for( int i = 0; i < poh->room_count; i++ )
            if( poh->rooms[i].x == x && poh->rooms[i].z == z &&
                poh->rooms[i].level == TORIRSSERVER_POH_GROUND_LEVEL )
                supported = 1;
        if( !supported )
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
ToriRSServer_PohRoomGet(
    const struct ToriRSServerPohState* poh,
    int room,
    int field)
{
    const struct ToriRSServerPohRoom* value;

    if( room < 0 || room >= poh->room_count )
        return -1;
    value = &poh->rooms[room];
    switch( field )
    {
    case TORIRSSERVER_POH_ROOM_DBROW:
        return value->dbrow;
    case TORIRSSERVER_POH_ROOM_X:
        return value->x;
    case TORIRSSERVER_POH_ROOM_Z:
        return value->z;
    case TORIRSSERVER_POH_ROOM_LEVEL:
        return value->level;
    case TORIRSSERVER_POH_ROOM_ROTATION:
        return value->rotation;
    case TORIRSSERVER_POH_ROOM_DOOR_MASK:
        return value->door_mask;
    default:
        return -1;
    }
}

int
ToriRSServer_PohRoomSet(
    struct ToriRSServerPohState* poh,
    int room,
    int field,
    int value)
{
    struct ToriRSServerPohRoom candidate;

    if( room < 0 || room >= poh->room_count )
        return 0;
    candidate = poh->rooms[room];
    switch( field )
    {
    case TORIRSSERVER_POH_ROOM_DBROW:
        if( value < 0 )
            return 0;
        candidate.dbrow = value;
        break;
    case TORIRSSERVER_POH_ROOM_X:
        if( value < 0 || value >= TORIRSSERVER_POH_GRID_MAX )
            return 0;
        candidate.x = value;
        break;
    case TORIRSSERVER_POH_ROOM_Z:
        if( value < 0 || value >= TORIRSSERVER_POH_GRID_MAX )
            return 0;
        candidate.z = value;
        break;
    case TORIRSSERVER_POH_ROOM_LEVEL:
        if( value < 0 || value > TORIRSSERVER_POH_UPPER_LEVEL )
            return 0;
        candidate.level = value;
        break;
    case TORIRSSERVER_POH_ROOM_ROTATION:
        if( value < 0 || value > 3 )
            return 0;
        candidate.rotation = value;
        break;
    case TORIRSSERVER_POH_ROOM_DOOR_MASK:
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
    if( candidate.level == TORIRSSERVER_POH_UPPER_LEVEL )
    {
        int supported = 0;

        for( int i = 0; i < poh->room_count; i++ )
            if( i != room && poh->rooms[i].x == candidate.x &&
                poh->rooms[i].z == candidate.z &&
                poh->rooms[i].level == TORIRSSERVER_POH_GROUND_LEVEL )
                supported = 1;
        if( !supported )
            return 0;
    }
    if( poh->rooms[room].level == TORIRSSERVER_POH_GROUND_LEVEL &&
        (candidate.level != TORIRSSERVER_POH_GROUND_LEVEL ||
         candidate.x != poh->rooms[room].x || candidate.z != poh->rooms[room].z) )
    {
        for( int i = 0; i < poh->room_count; i++ )
            if( i != room && poh->rooms[i].x == poh->rooms[room].x &&
                poh->rooms[i].z == poh->rooms[room].z &&
                poh->rooms[i].level == TORIRSSERVER_POH_UPPER_LEVEL )
                return 0;
    }
    poh->rooms[room] = candidate;
    return 1;
}

int
ToriRSServer_PohRoomRemove(
    struct ToriRSServerPohState* poh,
    int room)
{
    int write = 0;

    if( room < 0 || room >= poh->room_count )
        return 0;
    if( poh->rooms[room].level == TORIRSSERVER_POH_GROUND_LEVEL )
        for( int i = 0; i < poh->room_count; i++ )
            if( i != room && poh->rooms[i].x == poh->rooms[room].x &&
                poh->rooms[i].z == poh->rooms[room].z &&
                poh->rooms[i].level == TORIRSSERVER_POH_UPPER_LEVEL )
                return 0;

    /* Decorations name their owning room by slot. Drop the removed room's
     * decorations and shift later references alongside the compacted room
     * array. */
    for( int read = 0; read < poh->decoration_count; read++ )
    {
        struct ToriRSServerPohDecoration decoration = poh->decorations[read];

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
ToriRSServer_PohDecorationSet(
    struct ToriRSServerPohState* poh,
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
        if( poh->decoration_count >= TORIRSSERVER_POH_DECORATION_MAX )
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
ToriRSServer_PohDecorationGet(
    const struct ToriRSServerPohState* poh,
    int room,
    int hotspot,
    int field)
{
    const struct ToriRSServerPohDecoration* value = NULL;

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
    case TORIRSSERVER_POH_DECOR_FURNITURE_DBROW:
        return value->furniture_dbrow;
    case TORIRSSERVER_POH_DECOR_ROTATION:
        return value->rotation;
    case TORIRSSERVER_POH_DECOR_FLAGS:
        return value->flags;
    default:
        return -1;
    }
}

int
ToriRSServer_PohValidate(const struct ToriRSServerPohState* poh)
{
    if( poh->schema_version != TORIRSSERVER_POH_SCHEMA_VERSION ||
        !boolean_value(poh->owns_house) || poh->location < 0 || poh->location > 31 ||
        poh->style < 0 || poh->style > TORIRSSERVER_POH_STYLE_MAX ||
        !boolean_value(poh->locked) ||
        poh->door_mode < 0 || poh->door_mode > 2 ||
        !boolean_value(poh->teleport_inside) ||
        !boolean_value(poh->default_build_mode) || poh->grid_size < 3 ||
        poh->grid_size > 7 || !servant_type_value(poh->servant_type) ||
        poh->servant_paid < 0 ||
        poh->servant_paid > TORIRSSERVER_POH_SERVANT_SERVICE_MAX ||
        poh->servant_last_task < -1 ||
        poh->servant_last_task > TORIRSSERVER_POH_SERVANT_TASK_MAX ||
        poh->money_bag < 0 ||
        poh->money_bag > TORIRSSERVER_POH_MONEY_BAG_MAX ||
        poh->family_crest < -1 || poh->family_crest > 15 ||
        poh->head_trophies < 0 || poh->head_trophies > 0x1ff ||
        poh->fish_trophies < 0 || poh->fish_trophies > 0xf ||
        poh->spice_red < 0 || poh->spice_orange < 0 ||
        poh->spice_brown < 0 || poh->spice_yellow < 0 ||
        poh->tip_coins < 0 || poh->tip_platinum < 0 ||
        !boolean_value(poh->tip_notify) ||
        !boolean_value(poh->tip_auto_bank) ||
        poh->treasure_coins < 0 ||
        poh->treasure_coins > TORIRSSERVER_POH_TREASURE_MAX ||
        poh->treasure_ready_minute < 0 ||
        poh->boss_jars < 0 ||
        poh->boss_jars > TORIRSSERVER_POH_BOSS_JAR_MASK_MAX ||
        poh->dummy_variants < 0 ||
        poh->dummy_variants > TORIRSSERVER_POH_DUMMY_VARIANT_MASK_MAX ||
        poh->games_prize_coins < 0 ||
        poh->games_prize_coins > TORIRSSERVER_POH_GAMES_PRIZE_MAX ||
        poh->style_unlocks < 0 ||
        poh->style_unlocks > TORIRSSERVER_POH_STYLE_UNLOCK_MASK_MAX ||
        poh->room_count < 0 || poh->room_count > TORIRSSERVER_POH_ROOM_MAX ||
        poh->decoration_count < 0 ||
        poh->decoration_count > TORIRSSERVER_POH_DECORATION_MAX )
        return 0;

    for( int i = 0; i < poh->room_count; i++ )
    {
        const struct ToriRSServerPohRoom* room = &poh->rooms[i];

        if( room->dbrow < 0 || room->x < 0 || room->x >= TORIRSSERVER_POH_GRID_MAX ||
            room->z < 0 || room->z >= TORIRSSERVER_POH_GRID_MAX || room->level < 0 ||
            room->level > TORIRSSERVER_POH_UPPER_LEVEL || room->rotation < 0 ||
            room->rotation > 3 ||
            room->door_mask < 0 || room->door_mask > 15 )
            return 0;
        for( int j = 0; j < i; j++ )
        {
            if( poh->rooms[j].x == room->x && poh->rooms[j].z == room->z &&
                poh->rooms[j].level == room->level )
                return 0;
        }
        if( room->level == TORIRSSERVER_POH_UPPER_LEVEL )
        {
            int supported = 0;

            for( int j = 0; j < poh->room_count; j++ )
                if( poh->rooms[j].x == room->x && poh->rooms[j].z == room->z &&
                    poh->rooms[j].level == TORIRSSERVER_POH_GROUND_LEVEL )
                    supported = 1;
            if( !supported )
                return 0;
        }
    }
    for( int i = 0; i < poh->decoration_count; i++ )
    {
        const struct ToriRSServerPohDecoration* decoration = &poh->decorations[i];

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
