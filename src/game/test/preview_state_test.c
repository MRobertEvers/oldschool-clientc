#include "game/preview_state.h"
#include "game/rs_player_stats.h"
#include "varc/varc_manager.h"
#include "varp/varp_manager.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition, message)                                               \
    do                                                                          \
    {                                                                           \
        if( !(condition) )                                                       \
        {                                                                       \
            fprintf(stderr, "preview_state_test: %s\n", (message));            \
            exit(1);                                                            \
        }                                                                       \
    } while( 0 )

static void
put_u32(unsigned char* p, uint32_t value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

static size_t
put_int_record(
    unsigned char* packet,
    size_t at,
    unsigned kind,
    int id,
    int value)
{
    packet[at] = (unsigned char)kind;
    packet[at + 1] = packet[at + 2] = packet[at + 3] = 0;
    put_u32(packet + at + 4, (uint32_t)id);
    put_u32(packet + at + 8, 4);
    put_u32(packet + at + 12, (uint32_t)value);
    return at + 16;
}

static size_t
put_string_record(
    unsigned char* packet,
    size_t at,
    int id,
    char const* value)
{
    size_t length = strlen(value);
    packet[at] = TORIRS_PREVIEW_STATE_VARC_STRING;
    packet[at + 1] = packet[at + 2] = packet[at + 3] = 0;
    put_u32(packet + at + 4, (uint32_t)id);
    put_u32(packet + at + 8, (uint32_t)length);
    memcpy(packet + at + 12, value, length);
    return at + 12 + length;
}

int
main(void)
{
    unsigned char packet[128] = { 'C', '2', 'S', 'T', 'A', 'T', 'E', '1' };
    struct VarPType* varp_types;
    struct VarBitType* varbit_types;
    struct VarPManager varps;
    struct VarCManager varcs;
    struct RS_PlayerStats stats;
    char error[192];
    size_t size = 12;
    int applied = 0;
    uint32_t stat_mask = 0;

    VarPManager_Init(&varps);
    VarCManager_Init(&varcs);
    RS_PlayerStats_Init(&stats);
    varp_types = calloc(256, sizeof(*varp_types));
    varbit_types = calloc(3959, sizeof(*varbit_types));
    CHECK(varp_types && varbit_types, "fixture allocation");
    for( int i = 0; i < 3959; i++ )
        varbit_types[i].basevar = -1;
    varbit_types[3958].basevar = 115;
    varbit_types[3958].startbit = 0;
    varbit_types[3958].endbit = 0;
    CHECK(VarPManager_SetVarpTypes(&varps, varp_types, 256), "varp types");
    CHECK(VarPManager_SetVarbitTypes(&varps, varbit_types, 3959), "varbit types");
    free(varp_types);
    free(varbit_types);

    put_u32(packet + 8, 5);
    size = put_int_record(packet, size, TORIRS_PREVIEW_STATE_VARP, 115, 4);
    size = put_int_record(packet, size, TORIRS_PREVIEW_STATE_VARBIT, 3958, 1);
    size = put_int_record(packet, size, TORIRS_PREVIEW_STATE_VARC_INT, 5, 11);
    size = put_string_record(packet, size, 359, "dragon");
    size = put_int_record(packet, size, TORIRS_PREVIEW_STATE_STAT, 3, 77);

    CHECK(
        ToriRSPreviewState_ApplyBuffer(
            packet,
            size,
            &varps,
            &varcs,
            &stats,
            &applied,
            &stat_mask,
            error,
            sizeof(error)),
        error);
    CHECK(applied == 5, "all records applied");
    CHECK(stat_mask == ((uint32_t)1u << 3), "stat change mask");
    CHECK(VarPManager_GetVarp(&varps, 115) == 5, "varp then varbit ordering");
    CHECK(VarPManager_GetVarbit(&varps, 3958) == 1, "varbit value");
    CHECK(VarCManager_GetInt(&varcs, 5) == 11, "varc int value");
    CHECK(strcmp(VarCManager_GetString(&varcs, 359), "dragon") == 0, "varc string value");
    CHECK(stats.current_level[3] == 77 && stats.base_level[3] == 77, "stat value");

    /* A malformed packet is rejected before the first record mutates state. */
    packet[13] = 1;
    CHECK(
        !ToriRSPreviewState_ApplyBuffer(
            packet,
            size,
            &varps,
            &varcs,
            &stats,
            &applied,
            &stat_mask,
            error,
            sizeof(error)),
        "non-zero reserved byte was accepted");
    CHECK(applied == 0, "invalid packet reported applied records");
    CHECK(stat_mask == 0, "invalid packet reported changed stats");
    CHECK(VarPManager_GetVarp(&varps, 115) == 5, "invalid packet changed state");

    VarCManager_Free(&varcs);
    VarPManager_Free(&varps);
    printf("preview_state_test: ok\n");
    return 0;
}
