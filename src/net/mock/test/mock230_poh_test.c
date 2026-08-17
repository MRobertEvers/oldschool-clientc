#include "../mock230_poh.h"

#include <stdio.h>

static int checks;
static int failures;

static void
check(int ok, const char* message)
{
    checks++;
    if( !ok )
    {
        failures++;
        fprintf(stderr, "mock230_poh_test: FAILED: %s\n", message);
    }
}

int
main(void)
{
    struct Mock230PohState poh;
    int garden;
    int parlour;

    mock230_poh_init(&poh);
    check(mock230_poh_validate(&poh), "the empty default record is valid");
    check(poh.schema_version == MOCK230_POH_SCHEMA_VERSION,
          "new records use the current schema");
    check(poh.grid_size == 3 && poh.servant_last_task == -1 &&
              poh.family_crest == -1,
          "new records receive non-zero defaults");

    check(mock230_poh_set(&poh, MOCK230_POH_FIELD_OWNS_HOUSE, 1),
          "ownership can be enabled");
    check(mock230_poh_set(&poh, MOCK230_POH_FIELD_LOCATION, 0),
          "a house location can be stored");
    check(mock230_poh_set(&poh, MOCK230_POH_FIELD_STYLE, 1),
          "a house style can be stored");
    check(!mock230_poh_set(&poh, MOCK230_POH_FIELD_GRID_SIZE, 8),
          "oversized layouts are rejected");
    check(!mock230_poh_set(&poh, MOCK230_POH_FIELD_LOCKED, 2),
          "boolean fields reject non-booleans");
    check(mock230_poh_set(&poh, MOCK230_POH_FIELD_FAMILY_CREST, 12) &&
              mock230_poh_get(&poh, MOCK230_POH_FIELD_FAMILY_CREST) == 12,
          "a family crest round-trips");
    check(!mock230_poh_set(&poh, MOCK230_POH_FIELD_FAMILY_CREST, 16),
          "unknown family crests are rejected");

    garden = mock230_poh_room_add(&poh, 100, 4, 3, 1, 0, 1);
    parlour = mock230_poh_room_add(&poh, 101, 4, 4, 1, 2, 4);
    check(garden == 0 && parlour == 1, "starter rooms receive stable slots");
    check(mock230_poh_room_add(&poh, 102, 4, 4, 1, 0, 0) < 0,
          "two rooms cannot occupy one cell and plane");
    check(mock230_poh_room_get(&poh, parlour, MOCK230_POH_ROOM_ROTATION) == 2,
          "room fields round-trip");
    check(mock230_poh_room_get(&poh, 99, MOCK230_POH_ROOM_DBROW) == -1,
          "invalid room slots are rejected");

    check(mock230_poh_decoration_set(&poh, parlour, 7, 200, 1, 3),
          "a hotspot decoration can be added");
    check(mock230_poh_decoration_get(
              &poh, parlour, 7, MOCK230_POH_DECOR_FURNITURE_DBROW) == 200,
          "decoration furniture round-trips");
    check(mock230_poh_decoration_set(&poh, parlour, 7, 201, 3, 4),
          "an existing hotspot decoration can be upgraded");
    check(poh.decoration_count == 1 &&
              mock230_poh_decoration_get(
                  &poh, parlour, 7, MOCK230_POH_DECOR_FURNITURE_DBROW) == 201,
          "upgrading does not duplicate the hotspot");
    check(!mock230_poh_decoration_set(&poh, 99, 7, 202, 0, 0),
          "decorations cannot reference a missing room");
    check(mock230_poh_decoration_set(&poh, parlour, 7, -1, 0, 0),
          "negative furniture removes a decoration");
    check(poh.decoration_count == 0 &&
              mock230_poh_decoration_get(
                  &poh, parlour, 7, MOCK230_POH_DECOR_FURNITURE_DBROW) == -1,
          "removed decorations are no longer addressable");
    check(mock230_poh_validate(&poh), "a populated record validates");

    check(mock230_poh_room_set(&poh, parlour, MOCK230_POH_ROOM_ROTATION, 3) &&
              mock230_poh_room_get(&poh, parlour, MOCK230_POH_ROOM_ROTATION) == 3,
          "a room can be rotated in place");
    check(!mock230_poh_room_set(&poh, parlour, MOCK230_POH_ROOM_Z, 3),
          "moving a room onto an occupied cell is rejected");
    check(!mock230_poh_room_set(&poh, 99, MOCK230_POH_ROOM_X, 1),
          "an invalid room cannot be edited");

    check(mock230_poh_decoration_set(&poh, garden, 2, 300, 0, 0) &&
              mock230_poh_decoration_set(&poh, parlour, 3, 301, 0, 0),
          "room-removal decorations can be staged");
    check(mock230_poh_room_remove(&poh, garden), "a room can be removed");
    check(poh.room_count == 1 && poh.decoration_count == 1 &&
              poh.decorations[0].room == 0 &&
              poh.decorations[0].furniture_dbrow == 301,
          "removing a room drops its furniture and reindexes later rooms");
    check(!mock230_poh_room_remove(&poh, 9),
          "an invalid room cannot be removed");

    check(mock230_poh_room_add(&poh, 102, 1, 1, 1, 0, 0) == 1,
          "a second room can be added after compaction");

    poh.rooms[1].x = poh.rooms[0].x;
    poh.rooms[1].z = poh.rooms[0].z;
    check(!mock230_poh_validate(&poh), "validation catches overlapping rooms");
    mock230_poh_reset(&poh);
    check(mock230_poh_validate(&poh) && !poh.owns_house && poh.room_count == 0,
          "reset returns to a valid empty record");

    if( failures )
    {
        fprintf(stderr, "mock230_poh_test: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("mock230_poh_test: %d checks passed\n", checks);
    return 0;
}
