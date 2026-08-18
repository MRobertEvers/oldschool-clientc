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
              poh.family_crest == -1 && poh.tip_notify == 1 &&
              poh.tip_auto_bank == 0,
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
    check(mock230_poh_set(&poh, MOCK230_POH_FIELD_HEAD_TROPHIES, 0x105) &&
              mock230_poh_get(&poh, MOCK230_POH_FIELD_HEAD_TROPHIES) == 0x105,
          "the account head-trophy ledger round-trips");
    check(mock230_poh_set(&poh, MOCK230_POH_FIELD_FISH_TROPHIES, 0x9) &&
              mock230_poh_get(&poh, MOCK230_POH_FIELD_FISH_TROPHIES) == 0x9,
          "the account fish-trophy ledger round-trips");
    check(!mock230_poh_set(&poh, MOCK230_POH_FIELD_HEAD_TROPHIES, 0x200) &&
              !mock230_poh_set(&poh, MOCK230_POH_FIELD_FISH_TROPHIES, 0x10),
          "unknown trophy bits are rejected");
    check(mock230_poh_set(&poh, MOCK230_POH_FIELD_SPICE_RED, 17) &&
              mock230_poh_set(&poh, MOCK230_POH_FIELD_SPICE_ORANGE, 23) &&
              mock230_poh_set(&poh, MOCK230_POH_FIELD_SPICE_BROWN, 31) &&
              mock230_poh_set(&poh, MOCK230_POH_FIELD_SPICE_YELLOW, 47),
          "all four spice-rack dose counters can be stored");
    check(mock230_poh_get(&poh, MOCK230_POH_FIELD_SPICE_RED) == 17 &&
              mock230_poh_get(&poh, MOCK230_POH_FIELD_SPICE_ORANGE) == 23 &&
              mock230_poh_get(&poh, MOCK230_POH_FIELD_SPICE_BROWN) == 31 &&
              mock230_poh_get(&poh, MOCK230_POH_FIELD_SPICE_YELLOW) == 47,
          "all four spice-rack dose counters round-trip");
    check(!mock230_poh_set(&poh, MOCK230_POH_FIELD_SPICE_RED, -1),
          "negative spice doses are rejected");
    check(mock230_poh_set(&poh, MOCK230_POH_FIELD_MONEY_BAG,
                          MOCK230_POH_MONEY_BAG_MAX) &&
              mock230_poh_get(&poh, MOCK230_POH_FIELD_MONEY_BAG) ==
                  MOCK230_POH_MONEY_BAG_MAX,
          "the servant money bag stores its exact 3,000,000 coin cap");
    check(!mock230_poh_set(&poh, MOCK230_POH_FIELD_MONEY_BAG,
                           MOCK230_POH_MONEY_BAG_MAX + 1),
          "the servant money bag rejects values above its Wiki cap");
    check(mock230_poh_set(&poh, MOCK230_POH_FIELD_TIP_COINS, 7500000) &&
              mock230_poh_set(&poh, MOCK230_POH_FIELD_TIP_PLATINUM, 1234) &&
              mock230_poh_get(&poh, MOCK230_POH_FIELD_TIP_COINS) == 7500000 &&
              mock230_poh_get(&poh, MOCK230_POH_FIELD_TIP_PLATINUM) == 1234,
          "the two shared tip-jar balances round-trip independently");
    check(mock230_poh_set(&poh, MOCK230_POH_FIELD_TIP_NOTIFY, 0) &&
              mock230_poh_set(&poh, MOCK230_POH_FIELD_TIP_AUTO_BANK, 1),
          "tip notification and logout-bank settings are durable");
    check(!mock230_poh_set(&poh, MOCK230_POH_FIELD_TIP_COINS, -1) &&
              !mock230_poh_set(&poh, MOCK230_POH_FIELD_TIP_NOTIFY, 2),
          "tip balances reject negatives and settings reject non-booleans");
    check(mock230_poh_set(&poh, MOCK230_POH_FIELD_TREASURE_COINS,
                          MOCK230_POH_TREASURE_MAX) &&
              mock230_poh_set(&poh,
                              MOCK230_POH_FIELD_TREASURE_READY_MINUTE,
                              30000000) &&
              mock230_poh_get(&poh, MOCK230_POH_FIELD_TREASURE_COINS) ==
                  MOCK230_POH_TREASURE_MAX &&
              mock230_poh_get(
                  &poh, MOCK230_POH_FIELD_TREASURE_READY_MINUTE) == 30000000,
          "the shared treasure reward and durable cooldown round-trip");
    check(!mock230_poh_set(&poh, MOCK230_POH_FIELD_TREASURE_COINS,
                           MOCK230_POH_TREASURE_MAX + 1) &&
              !mock230_poh_set(
                  &poh, MOCK230_POH_FIELD_TREASURE_READY_MINUTE, -1),
          "treasure storage rejects over-cap rewards and negative clocks");
    check(mock230_poh_set(&poh, MOCK230_POH_FIELD_BOSS_JARS, 0x4121) &&
              mock230_poh_get(&poh, MOCK230_POH_FIELD_BOSS_JARS) == 0x4121,
          "the Boss lair jar ledger round-trips");
    check(!mock230_poh_set(&poh, MOCK230_POH_FIELD_BOSS_JARS, 0x8000),
          "unknown Boss lair jar bits are rejected");
    check(mock230_poh_set(&poh, MOCK230_POH_FIELD_DUMMY_VARIANTS, 0x15) &&
              mock230_poh_get(&poh, MOCK230_POH_FIELD_DUMMY_VARIANTS) == 0x15,
          "the ornate combat-dummy unlock ledger round-trips");
    check(!mock230_poh_set(&poh, MOCK230_POH_FIELD_DUMMY_VARIANTS, 0x20),
          "unknown ornate combat-dummy unlock bits are rejected");
    check(mock230_poh_set(&poh, MOCK230_POH_FIELD_GAMES_PRIZE_COINS,
                          MOCK230_POH_GAMES_PRIZE_MAX) &&
              mock230_poh_get(&poh, MOCK230_POH_FIELD_GAMES_PRIZE_COINS) ==
                  MOCK230_POH_GAMES_PRIZE_MAX,
          "the Games-room prize balance round-trips");
    check(!mock230_poh_set(&poh, MOCK230_POH_FIELD_GAMES_PRIZE_COINS,
                           MOCK230_POH_GAMES_PRIZE_MAX + 1),
          "the Games-room prize balance rejects over-cap coins");

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

    {
        int upper = mock230_poh_room_add(
            &poh, 102, 4, 4, MOCK230_POH_UPPER_LEVEL, 0, 4);

        check(upper >= 0, "an upper room can be added over ground-floor support");
        check(mock230_poh_room_add(
                  &poh, 103, 1, 1, MOCK230_POH_UPPER_LEVEL, 0, 0) < 0,
              "an unsupported upper room is rejected");
        check(mock230_poh_room_add(&poh, 104, 4, 4, 3, 0, 0) < 0,
              "a third-storey room is rejected");
        check(!mock230_poh_room_remove(&poh, parlour),
              "a supporting ground-floor room cannot be removed");
        check(!mock230_poh_room_set(&poh, parlour, MOCK230_POH_ROOM_X, 3),
              "a supporting ground-floor room cannot move away");
        check(mock230_poh_room_remove(&poh, upper),
              "an upper room can be removed before its support");
    }

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
