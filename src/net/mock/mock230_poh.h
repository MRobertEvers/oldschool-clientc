#ifndef SRC_NET_MOCK_MOCK230_POH_H
#define SRC_NET_MOCK_MOCK230_POH_H

/*
 * Durable Player-owned House state.
 *
 * This is storage, not Construction policy. RuneScript decides which room may
 * be built, what it costs, which hotspot accepts which furniture, and when a
 * guest may enter. The host owns the part content cannot express: a bounded,
 * versioned record that survives logout and can be addressed while an instance
 * is being assembled.
 */

enum
{
    MOCK230_POH_SCHEMA_VERSION = 1,
    MOCK230_POH_ROOM_MAX = 38,
    MOCK230_POH_DECORATION_MAX = 512,
    MOCK230_POH_GRID_MAX = 8,
};

enum Mock230PohField
{
    MOCK230_POH_FIELD_SCHEMA_VERSION = 0,
    MOCK230_POH_FIELD_OWNS_HOUSE = 1,
    MOCK230_POH_FIELD_LOCATION = 2,
    MOCK230_POH_FIELD_STYLE = 3,
    MOCK230_POH_FIELD_LOCKED = 4,
    MOCK230_POH_FIELD_DOOR_MODE = 5,
    MOCK230_POH_FIELD_TELEPORT_INSIDE = 6,
    MOCK230_POH_FIELD_DEFAULT_BUILD_MODE = 7,
    MOCK230_POH_FIELD_GRID_SIZE = 8,
    MOCK230_POH_FIELD_SERVANT_TYPE = 9,
    MOCK230_POH_FIELD_SERVANT_PAID = 10,
    MOCK230_POH_FIELD_SERVANT_LAST_TASK = 11,
    MOCK230_POH_FIELD_MONEY_BAG = 12,
};

enum Mock230PohRoomField
{
    MOCK230_POH_ROOM_DBROW = 0,
    MOCK230_POH_ROOM_X = 1,
    MOCK230_POH_ROOM_Z = 2,
    MOCK230_POH_ROOM_LEVEL = 3,
    MOCK230_POH_ROOM_ROTATION = 4,
    MOCK230_POH_ROOM_DOOR_MASK = 5,
};

enum Mock230PohDecorationField
{
    MOCK230_POH_DECOR_FURNITURE_DBROW = 0,
    MOCK230_POH_DECOR_ROTATION = 1,
    MOCK230_POH_DECOR_FLAGS = 2,
};

struct Mock230PohRoom
{
    int dbrow;
    int x;
    int z;
    int level;
    int rotation;
    int door_mask;
};

struct Mock230PohDecoration
{
    int room;
    int hotspot;
    int furniture_dbrow;
    int rotation;
    int flags;
};

struct Mock230PohState
{
    int schema_version;
    int owns_house;
    int location;
    int style;
    int locked;
    int door_mode;
    int teleport_inside;
    int default_build_mode;
    int grid_size;
    int servant_type;
    int servant_paid;
    int servant_last_task;
    int money_bag;

    struct Mock230PohRoom rooms[MOCK230_POH_ROOM_MAX];
    int room_count;
    struct Mock230PohDecoration decorations[MOCK230_POH_DECORATION_MAX];
    int decoration_count;
};

void
mock230_poh_init(struct Mock230PohState* poh);

void
mock230_poh_reset(struct Mock230PohState* poh);

int
mock230_poh_get(
    const struct Mock230PohState* poh,
    int field);

int
mock230_poh_set(
    struct Mock230PohState* poh,
    int field,
    int value);

int
mock230_poh_room_add(
    struct Mock230PohState* poh,
    int dbrow,
    int x,
    int z,
    int level,
    int rotation,
    int door_mask);

int
mock230_poh_room_get(
    const struct Mock230PohState* poh,
    int room,
    int field);

int
mock230_poh_decoration_set(
    struct Mock230PohState* poh,
    int room,
    int hotspot,
    int furniture_dbrow,
    int rotation,
    int flags);

int
mock230_poh_decoration_get(
    const struct Mock230PohState* poh,
    int room,
    int hotspot,
    int field);

int
mock230_poh_validate(const struct Mock230PohState* poh);

#endif
