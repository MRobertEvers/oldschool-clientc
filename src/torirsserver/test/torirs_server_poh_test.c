#include "../torirs_server_poh.h"

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
        fprintf(stderr, "ToriRSServer_PohTest: FAILED: %s\n", message);
    }
}

int
main(void)
{
    struct ToriRSServerPohState poh;
    int garden;
    int parlour;

    ToriRSServer_PohInit(&poh);
    check(ToriRSServer_PohValidate(&poh), "the empty default record is valid");
    check(poh.schema_version == TORIRSSERVER_POH_SCHEMA_VERSION,
          "new records use the current schema");
    check(poh.grid_size == 3 && poh.servant_last_task == -1 &&
              poh.family_crest == -1 && poh.tip_notify == 1 &&
              poh.tip_auto_bank == 0,
          "new records receive non-zero defaults");

    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_OWNS_HOUSE, 1),
          "ownership can be enabled");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_LOCATION, 0),
          "a house location can be stored");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_STYLE, 1),
          "a house style can be stored");
    check(!ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_GRID_SIZE, 8),
          "oversized layouts are rejected");
    check(!ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_LOCKED, 2),
          "boolean fields reject non-booleans");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_FAMILY_CREST, 12) &&
              ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_FAMILY_CREST) == 12,
          "a family crest round-trips");
    check(!ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_FAMILY_CREST, 16),
          "unknown family crests are rejected");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_HEAD_TROPHIES, 0x105) &&
              ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_HEAD_TROPHIES) == 0x105,
          "the account head-trophy ledger round-trips");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_FISH_TROPHIES, 0x9) &&
              ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_FISH_TROPHIES) == 0x9,
          "the account fish-trophy ledger round-trips");
    check(!ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_HEAD_TROPHIES, 0x200) &&
              !ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_FISH_TROPHIES, 0x10),
          "unknown trophy bits are rejected");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_SPICE_RED, 17) &&
              ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_SPICE_ORANGE, 23) &&
              ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_SPICE_BROWN, 31) &&
              ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_SPICE_YELLOW, 47),
          "all four spice-rack dose counters can be stored");
    check(ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_SPICE_RED) == 17 &&
              ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_SPICE_ORANGE) == 23 &&
              ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_SPICE_BROWN) == 31 &&
              ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_SPICE_YELLOW) == 47,
          "all four spice-rack dose counters round-trip");
    check(!ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_SPICE_RED, -1),
          "negative spice doses are rejected");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_MONEY_BAG,
                          TORIRSSERVER_POH_MONEY_BAG_MAX) &&
              ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_MONEY_BAG) ==
                  TORIRSSERVER_POH_MONEY_BAG_MAX,
          "the servant money bag stores its exact 3,000,000 coin cap");
    check(!ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_MONEY_BAG,
                           TORIRSSERVER_POH_MONEY_BAG_MAX + 1),
          "the servant money bag rejects values above its Wiki cap");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_TIP_COINS, 7500000) &&
              ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_TIP_PLATINUM, 1234) &&
              ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_TIP_COINS) == 7500000 &&
              ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_TIP_PLATINUM) == 1234,
          "the two shared tip-jar balances round-trip independently");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_TIP_NOTIFY, 0) &&
              ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_TIP_AUTO_BANK, 1),
          "tip notification and logout-bank settings are durable");
    check(!ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_TIP_COINS, -1) &&
              !ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_TIP_NOTIFY, 2),
          "tip balances reject negatives and settings reject non-booleans");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_TREASURE_COINS,
                          TORIRSSERVER_POH_TREASURE_MAX) &&
              ToriRSServer_PohSet(&poh,
                              TORIRSSERVER_POH_FIELD_TREASURE_READY_MINUTE,
                              30000000) &&
              ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_TREASURE_COINS) ==
                  TORIRSSERVER_POH_TREASURE_MAX &&
              ToriRSServer_PohGet(
                  &poh, TORIRSSERVER_POH_FIELD_TREASURE_READY_MINUTE) == 30000000,
          "the shared treasure reward and durable cooldown round-trip");
    check(!ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_TREASURE_COINS,
                           TORIRSSERVER_POH_TREASURE_MAX + 1) &&
              !ToriRSServer_PohSet(
                  &poh, TORIRSSERVER_POH_FIELD_TREASURE_READY_MINUTE, -1),
          "treasure storage rejects over-cap rewards and negative clocks");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_BOSS_JARS, 0x4121) &&
              ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_BOSS_JARS) == 0x4121,
          "the Boss lair jar ledger round-trips");
    check(!ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_BOSS_JARS, 0x8000),
          "unknown Boss lair jar bits are rejected");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_DUMMY_VARIANTS, 0x15) &&
              ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_DUMMY_VARIANTS) == 0x15,
          "the ornate combat-dummy unlock ledger round-trips");
    check(!ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_DUMMY_VARIANTS, 0x20),
          "unknown ornate combat-dummy unlock bits are rejected");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_GAMES_PRIZE_COINS,
                          TORIRSSERVER_POH_GAMES_PRIZE_MAX) &&
              ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_GAMES_PRIZE_COINS) ==
                  TORIRSSERVER_POH_GAMES_PRIZE_MAX,
          "the Games-room prize balance round-trips");
    check(!ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_GAMES_PRIZE_COINS,
                           TORIRSSERVER_POH_GAMES_PRIZE_MAX + 1),
          "the Games-room prize balance rejects over-cap coins");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_STYLE_UNLOCKS, 0x7) &&
              ToriRSServer_PohGet(&poh, TORIRSSERVER_POH_FIELD_STYLE_UNLOCKS) == 0x7,
          "the permanent house-style entitlement ledger round-trips");
    check(!ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_STYLE_UNLOCKS, 0x8),
          "unknown house-style entitlement bits are rejected");
    check(!ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_STYLE, 12),
          "a house style absent from revision 239 is rejected");
    check(ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_SERVANT_TYPE, 8) &&
              ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_SERVANT_PAID, 7) &&
              ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_SERVANT_LAST_TASK, 20),
          "the Demon butler and final remembered-material ordinal round-trip");
    check(!ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_SERVANT_TYPE, 2) &&
              !ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_SERVANT_TYPE, 9) &&
              !ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_SERVANT_PAID, 8) &&
              !ToriRSServer_PohSet(&poh, TORIRSSERVER_POH_FIELD_SERVANT_LAST_TASK, 21),
          "unknown servants and impossible service counters are rejected");

    garden = ToriRSServer_PohRoomAdd(&poh, 100, 4, 3, 1, 0, 1);
    parlour = ToriRSServer_PohRoomAdd(&poh, 101, 4, 4, 1, 2, 4);
    check(garden == 0 && parlour == 1, "starter rooms receive stable slots");
    check(ToriRSServer_PohRoomAdd(&poh, 102, 4, 4, 1, 0, 0) < 0,
          "two rooms cannot occupy one cell and plane");
    check(ToriRSServer_PohRoomGet(&poh, parlour, TORIRSSERVER_POH_ROOM_ROTATION) == 2,
          "room fields round-trip");
    check(ToriRSServer_PohRoomGet(&poh, 99, TORIRSSERVER_POH_ROOM_DBROW) == -1,
          "invalid room slots are rejected");

    check(ToriRSServer_PohDecorationSet(&poh, parlour, 7, 200, 1, 3),
          "a hotspot decoration can be added");
    check(ToriRSServer_PohDecorationGet(
              &poh, parlour, 7, TORIRSSERVER_POH_DECOR_FURNITURE_DBROW) == 200,
          "decoration furniture round-trips");
    check(ToriRSServer_PohDecorationSet(&poh, parlour, 7, 201, 3, 4),
          "an existing hotspot decoration can be upgraded");
    check(poh.decoration_count == 1 &&
              ToriRSServer_PohDecorationGet(
                  &poh, parlour, 7, TORIRSSERVER_POH_DECOR_FURNITURE_DBROW) == 201,
          "upgrading does not duplicate the hotspot");
    check(!ToriRSServer_PohDecorationSet(&poh, 99, 7, 202, 0, 0),
          "decorations cannot reference a missing room");
    check(ToriRSServer_PohDecorationSet(&poh, parlour, 7, -1, 0, 0),
          "negative furniture removes a decoration");
    check(poh.decoration_count == 0 &&
              ToriRSServer_PohDecorationGet(
                  &poh, parlour, 7, TORIRSSERVER_POH_DECOR_FURNITURE_DBROW) == -1,
          "removed decorations are no longer addressable");
    check(ToriRSServer_PohValidate(&poh), "a populated record validates");

    check(ToriRSServer_PohRoomSet(&poh, parlour, TORIRSSERVER_POH_ROOM_ROTATION, 3) &&
              ToriRSServer_PohRoomGet(&poh, parlour, TORIRSSERVER_POH_ROOM_ROTATION) == 3,
          "a room can be rotated in place");
    check(!ToriRSServer_PohRoomSet(&poh, parlour, TORIRSSERVER_POH_ROOM_Z, 3),
          "moving a room onto an occupied cell is rejected");
    check(!ToriRSServer_PohRoomSet(&poh, 99, TORIRSSERVER_POH_ROOM_X, 1),
          "an invalid room cannot be edited");

    {
        int upper = ToriRSServer_PohRoomAdd(
            &poh, 102, 4, 4, TORIRSSERVER_POH_UPPER_LEVEL, 0, 4);

        check(upper >= 0, "an upper room can be added over ground-floor support");
        check(ToriRSServer_PohRoomAdd(
                  &poh, 103, 1, 1, TORIRSSERVER_POH_UPPER_LEVEL, 0, 0) < 0,
              "an unsupported upper room is rejected");
        check(ToriRSServer_PohRoomAdd(&poh, 104, 4, 4, 3, 0, 0) < 0,
              "a third-storey room is rejected");
        check(!ToriRSServer_PohRoomRemove(&poh, parlour),
              "a supporting ground-floor room cannot be removed");
        check(!ToriRSServer_PohRoomSet(&poh, parlour, TORIRSSERVER_POH_ROOM_X, 3),
              "a supporting ground-floor room cannot move away");
        check(ToriRSServer_PohRoomRemove(&poh, upper),
              "an upper room can be removed before its support");
    }

    check(ToriRSServer_PohDecorationSet(&poh, garden, 2, 300, 0, 0) &&
              ToriRSServer_PohDecorationSet(&poh, parlour, 3, 301, 0, 0),
          "room-removal decorations can be staged");
    check(ToriRSServer_PohRoomRemove(&poh, garden), "a room can be removed");
    check(poh.room_count == 1 && poh.decoration_count == 1 &&
              poh.decorations[0].room == 0 &&
              poh.decorations[0].furniture_dbrow == 301,
          "removing a room drops its furniture and reindexes later rooms");
    check(!ToriRSServer_PohRoomRemove(&poh, 9),
          "an invalid room cannot be removed");

    check(ToriRSServer_PohRoomAdd(&poh, 102, 1, 1, 1, 0, 0) == 1,
          "a second room can be added after compaction");

    poh.rooms[1].x = poh.rooms[0].x;
    poh.rooms[1].z = poh.rooms[0].z;
    check(!ToriRSServer_PohValidate(&poh), "validation catches overlapping rooms");
    ToriRSServer_PohReset(&poh);
    check(ToriRSServer_PohValidate(&poh) && !poh.owns_house && poh.room_count == 0,
          "reset returns to a valid empty record");

    if( failures )
    {
        fprintf(stderr, "ToriRSServer_PohTest: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("ToriRSServer_PohTest: %d checks passed\n", checks);
    return 0;
}
